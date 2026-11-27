#!/usr/bin/env python3
"""The attract demos, the whole game end to end: tools/hz_demo (the port)
against the original from the same point on tools/m68krun.py.

    hzdemo_check.py ROM SYM HZ_DEMO ASSETS [--demos 0,1,...] [--frames N]
                    [--pads FILE | --start-at FRAME] [--done DIR]
                    [--keep DIR]

Each demo (DemoTable entry; default all 16) runs from where MainLoop_Title
is once MenuModule_Title has returned -1 (the title ran out), RAM as a
cold boot and the title leave it: Game_SetupMode (Demo_SetupNext),
VDP_Init, Game_LoadMap with the loading screen, Game_Start, the frame loop,
until the demo goes back to the title (or N frames). Compared frame by
frame: all of VRAM (by CRC), CRAM, VSRAM and the VDP's registers as the
frame shows them; the state behind it (by CRC, images built from the
listed fields of RAM: hz_demo's fill_*): the globals (seed, frame
counter, timers, counts, cameras, scroll, pads, the sfx queue, the
split), both mechs (+$00-$7F, the order menu's fields but its step left
out), the 100 units (+$00-$37, the unit AI's +$38-$4F left out), the
20 outpost/HQ records, v_grid and the 64 shots (+$00-$18, the owner
pointer left out); every sound command and its frame; how it ended.
At the first difference the port is run again to that frame for its
state and VRAM, and the fields (or the VRAM region) that differ are
named.

The original's run, on the interpreter, with:
  - the VDP's ports (hzordermenu_check's model, with DMA: 68k to VRAM,
    CRAM and VSRAM, fill, VRAM copy; registers as written, as the port's
    model keeps them). Registers 19-23 (a DMA's length and source) aren't
    compared: neither model counts them down as the chip does, and the
    port clears VRAM without a DMA where the 68k fills it with one
    (VDP_Fill2K), the same bytes either way;
  - the 68000's interrupts: the mask from move.w #$x700,sr; at each
    frame (VDP_WaitVBlank with v_vdp_reg1 b6, and each turn of
    Game_LoadMap's wait for the loading screen) HBlank (level 4, if the
    VDP's HInt is on) before the frame is taken and VBlank (level 6, if
    VInt is on) after, each run through its RAM stub and its rte, or held
    while masked and taken when move.w #$2000,sr unmasks (as the VDP holds
    an interrupt until it is acknowledged);
  - ReadJoypads: v_joypad1 = the script's pad for the frames gone by
    (--pads, as hz_demo's), v_joypad2 0;
  - the Z80: its bus always granted, the mailbox free, no DAC sample;
    each command written to the mailbox ($A00003 on) is logged;
  - VDP_WaitVBlankEnd returns (no frame), v_dma_trigger (code in RAM)
    writes its word.
Everything else is the original's code, LZW_Unpack included, but for the
port's CHANGED points, applied to the original as patches (as the unit
AI's and the order menu's checks apply them):
  Unit_SetSprite        d0 & $FF on entry (unit_ai.c: the turn judged by
                        the difference alone)
  OrderPatrol .dodge    d1 = 2 before its subq: in the demo too the step
                        is +-1 (the original's, in a demo, is a leftover
                        register - 1, so on the cartridge the demos' units
                        dodge otherwise from the first dodge on)
  AimAtMech .store      +$4C = the enemy mech it aimed at
  OrderSupply .approach a3 | $FF000000 before the cmpa
  OrderMenu_DrawCursor  carrying, any row other than 1 or 2 becomes 1
(No computer player in a demo: mech_ai.c's CHANGED points aren't reached.)

--start-at FRAME: Start held for two frames from FRAME (0-based, after it
is handed over): on the loading screen it aborts the load, in the demo it
ends it.

--done DIR: a file per demo that agreed, and those are skipped next time
(the sandbox restarts now and then; a run of all 16 takes a long time).
"""

import os
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import hzlayer                                          # noqa: E402
import m68k                                             # noqa: E402
import m68krun                                          # noqa: E402

RAM = 0xFF0000

# -- the state images (hz_demo.c's fill_*) -----------------------------------

GLOBALS = [  # (image offset, RAM name, offset from it, size)
    (0x00, 'v_rng_seed', 0, 4), (0x04, 'v_frame_counter', 0, 4),
    (0x08, 'v_second_timer', 0, 2), (0x0A, 'v_unit_count', 0, 2),
    (0x0C, 'v_outpost_count', 0, 4), (0x10, 'v_camera1', 0, 8),
    (0x18, 'v_scroll_x_a', 0, 8), (0x20, 'v_unit_ai_group', 0, 2),
    (0x22, 'v_joypad1', 0, 2), (0x24, 'v_sfx_priority', 0, 1),
    (0x25, 'v_sfx_queued', 0, 1), (0x26, 'v_split_enable', 0, 2),
    (0x28, 'v_split_done', 0, 1), (0x2A, 'v_game_type', 0, 2),
    (0x2C, 'v_demo_index', 0, 2)]
GLOBALS_BYTES = 0x40

MECH_FIELDS = [  # (offset, size, name)
    (0x00, 1, 'flags'), (0x01, 1, 'carried_slot'), (0x02, 1, 'carried_order'),
    (0x03, 1, 'dir'), (0x04, 1, 'turn_to'), (0x06, 1, 'transform'),
    (0x07, 1, 'transform_delay'), (0x08, 1, 'power'), (0x09, 1, 'hit_victim'),
    (0x0A, 2, 'hp'), (0x0C, 1, 'size'), (0x0D, 1, 'altitude'),
    (0x0E, 1, 'bumped'), (0x0F, 1, 'fuel_use'), (0x10, 2, 'x'), (0x12, 2, 'y'),
    (0x14, 2, 'sub_x'), (0x16, 2, 'sub_y'), (0x18, 1, 'missiles'),
    (0x19, 1, 'ammo'), (0x1A, 2, 'fuel'), (0x1C, 2, 'terrain'),
    (0x1E, 2, 'frame'), (0x20, 2, 'frame_loaded'), (0x22, 1, 'shot_flags'),
    (0x23, 1, 'blocked'), (0x25, 1, 'speed'), (0x27, 1, 'max_speed'),
    (0x28, 1, 'pad'), (0x29, 1, 'pad_prev'), (0x2A, 1, 'lead'),
    (0x2B, 1, 'terrain_flow'), (0x2C, 2, 'form'), (0x2E, 1, 'pad_last'),
    (0x2F, 1, 'dir_held'), (0x30, 2, 'vel_x'), (0x32, 2, 'vel_y'),
    (0x34, 2, 'target_vel_x'), (0x36, 2, 'target_vel_y'),
    (0x38, 2, 'screen_x'), (0x3A, 2, 'screen_y'), (0x3C, 2, 'cam_dx'),
    (0x3E, 2, 'cam_dy'), (0x40, 1, 'menu_step'), (0x42, 1, 'view'),
    (0x43, 1, 'view_index'), (0x4F, 1, 'prod_state'), (0x54, 2, 'money'),
    (0x5E, 1, 'prod_type'), (0x5F, 1, 'prod_order'), (0x60, 2, 'prod_timer'),
    (0x62, 1, 'hp_shown'), (0x63, 1, 'hq_hp_shown'), (0x64, 2, 'option2'),
    (0x66, 1, 'ammo_shown'), (0x67, 1, 'fuel_shown'),
    (0x68, 1, 'carried_refill'), (0x69, 1, 'fire_delay'),
    (0x6A, 2, 'missile_dirs'), (0x6C, 1, 'hq_alert'), (0x6E, 1, 'fuel_count'),
    (0x6F, 1, 'walk_step'), (0x70, 2, 'home_cam_x'), (0x72, 2, 'home_cam_y'),
    (0x74, 2, 'home_screen_x'), (0x76, 2, 'home_screen_y')]
MECH_BYTES = 0x80

UNIT_FIELDS = [
    (0x00, 1, 'flags'), (0x01, 1, 'type'), (0x02, 1, 'order'), (0x03, 1, 'dir'),
    (0x04, 1, 'turn_to'), (0x05, 1, 'tick'), (0x06, 1, 'phase'),
    (0x07, 1, 'fire_delay'), (0x08, 1, 'power'), (0x09, 1, 'hit_victim'),
    (0x0A, 2, 'hp'), (0x0C, 1, 'size'), (0x0D, 1, 'raw_0d'),
    (0x0E, 1, 'bumped'), (0x0F, 1, 'fuel_use'), (0x10, 2, 'x'), (0x12, 2, 'y'),
    (0x14, 2, 'sub_x'), (0x16, 2, 'sub_y'), (0x18, 1, 'missiles'),
    (0x19, 1, 'ammo'), (0x1A, 2, 'fuel'), (0x1C, 2, 'terrain'),
    (0x1E, 2, 'sprite_attr'), (0x20, 2, 'sprite_size'), (0x22, 1, 'shot_flags'),
    (0x23, 1, 'blocked'), (0x24, 2, 'speed'), (0x26, 1, 'prev_speed'),
    (0x27, 1, 'max_speed'), (0x28, 1, 'turret'), (0x29, 1, 'turret_dir'),
    (0x2A, 1, 'aim_dir'), (0x2B, 1, 'push'), (0x2C, 2, 'overlay_attr'),
    (0x2E, 2, 'grid_cell'), (0x30, 2, 'vel_x'), (0x32, 2, 'vel_y'),
    (0x34, 2, 'target_vel_x'), (0x36, 2, 'target_vel_y')]
UNIT_BYTES = 0x38
UNITS = 100
POSTS = 20
GRID = 0x1000
SHOTS = 64
SHOT_BYTES = 0x19
SHOT_FIELDS = [(0x00, 1, 'flags'), (0x01, 1, 'dir'), (0x02, 2, 'x'),
               (0x04, 2, 'y'), (0x06, 2, 'speed'), (0x08, 2, 'sub_x'),
               (0x0A, 2, 'sub_y'), (0x0C, 2, 'life'), (0x0E, 2, 'attr'),
               (0x10, 2, 'vel_x'), (0x12, 2, 'vel_y'), (0x14, 2, 'aim_x'),
               (0x16, 2, 'aim_y'), (0x18, 1, 'power')]


def image(src, base, fields, size):
    out = bytearray(size)
    for off, n, _ in fields:
        out[off:off + n] = src[base + off:base + off + n]
    return bytes(out)


def crc(b):
    return zlib.crc32(bytes(b)) & 0xFFFFFFFF


# -- the VDP ------------------------------------------------------------------

class Vdp:
    """The VDP's ports: registers, the address and code, writes to VRAM,
    CRAM and VSRAM, and DMA (68k memory, fill, copy) done at once."""

    def __init__(self, read16):
        self.vram = bytearray(0x10000)
        self.cram = [0] * 64
        self.vsram = [0] * 40
        self.regs = [0] * 24
        self.pending = None
        self.code = 1
        self.addr = 0
        self.fill = False
        self.read16 = read16
        self.held = hzlayer.Held()

    def store(self, w):
        a = self.addr
        t = self.code & 0x0F
        if t == 3:
            self.cram[(a >> 1) & 0x3F] = w & 0x0EEE
        elif t == 5:
            i = (a >> 1) & 0x3F
            if i < 40:
                self.vsram[i] = w & 0x07FF
        else:
            if a & 1:
                w = ((w << 8) | (w >> 8)) & 0xFFFF
            a &= 0xFFFE
            self.vram[a] = w >> 8
            self.vram[a + 1] = w & 0xFF
            self.held.wrote(a)
            self.held.wrote(a + 1)
        self.addr = (self.addr + self.regs[15]) & 0xFFFF

    def control(self, w):
        if self.pending is not None:
            w1, self.pending = self.pending, None
            self.code = ((w1 >> 14) & 3) | ((w >> 2) & 0x3C)
            self.addr = (w1 & 0x3FFF) | ((w & 3) << 14)
            if self.code & 0x20 and self.regs[1] & 0x10:
                self.dma()
            return
        if (w & 0xC000) == 0x8000:
            r = (w >> 8) & 0x1F
            if r < 24:
                self.regs[r] = w & 0xFF
                if r == 5:
                    self.held.table(w & 0xFF)
            return
        self.pending = w

    def dma(self):
        r = self.regs
        n = r[19] | (r[20] << 8)
        mode = r[23] >> 6
        if mode == 2:
            self.fill = True
        elif mode == 3:
            src = r[21] | (r[22] << 8)
            for _ in range(n or 0x10000):
                self.vram[self.addr] = self.vram[src]
                self.held.wrote(self.addr)
                src = (src + 1) & 0xFFFF
                self.addr = (self.addr + r[15]) & 0xFFFF
        else:
            src = ((r[23] & 0x7F) << 17) | (r[22] << 9) | (r[21] << 1)
            for _ in range(n or 0x10000):
                self.store(self.read16(src))
                src += 2

    def data(self, w):
        self.pending = None
        if self.fill:
            self.fill = False
            n = self.regs[19] | (self.regs[20] << 8)
            a = self.addr
            inc = self.regs[15] or 1
            for _ in range(n + 1):
                self.vram[a & 0xFFFF] = (w >> 8) & 0xFF
                self.held.wrote(a & 0xFFFF)
                a += inc
            self.addr = a & 0xFFFF
            return
        self.store(w)


# -- the 68000 ----------------------------------------------------------------

class Stop(Exception):
    pass


class Cpu(m68krun.M68k):
    def __init__(self, rom, sym, pads, limit, expect=None):
        super().__init__(rom)
        self.sym = sym
        self.vdp = Vdp(lambda a: (self.read(a, 2)))
        self.pads = pads
        self.limit = limit
        self.expect = expect
        self.frames = []          # (F line, R line)
        self.sound = []           # "M n bytes"
        self.cmd = None
        self.imask = 7
        self.held = {4: False, 6: False}
        self.how = None
        self.mismatch = None
        self.hooks = {
            sym['VDP_WaitVBlank']: self.wait_vblank,
            sym['VDP_WaitVBlankEnd']: self.rts,
            sym['ReadJoypads']: self.read_joypads,
            sym['v_dma_trigger'] & 0xFFFFFF: self.dma_trigger,
            sym['MainLoop_Title']: self.stop_title,
            sym['Game_Over']: self.stop_game_over,
        }
        self.patches = {
            sym['Unit_SetSprite']: self.p_setsprite,
            find(rom, sym, 'UnitAI_OrderPatrol.dodge', 'subq.l'):
                self.p_patrol,
            sym['UnitAI_AimAtMech.store']: self.p_aim_mech,
            find(rom, sym, 'UnitAI_OrderSupply.approach', 'cmpa.l'):
                self.p_supply,
            sym['OrderMenu_DrawCursor.store']: self.p_cursor,
        }
        self.hits = dict((k, 0) for k in self.patches)
        self.trace = self.patch
        self.load_wait = sym['Game_LoadMap.wait_load_screen']
        self.game_start = sym['Game_Start']
        self.started = False
        self.addr = {k: v & 0xFFFF for k, v in sym.items()
                     if k.startswith('v_')}
        self.stubs = {lvl: int.from_bytes(rom[lvl * 4 + 0x60:lvl * 4 + 0x64],
                                          'big') & 0xFFFFFF for lvl in (4, 6)}

    # -- memory ---------------------------------------------------------------
    def read(self, addr, size):
        a = addr & 0xFFFFFF
        if 0xC00000 <= a < 0xC00020:
            if a & 0x1C == 4:
                return 0            # status: no DMA, not in VBlank
            raise m68krun.M68kError('VDP data read at $%06X' % a)
        if 0xA00000 <= a < 0xA10000:
            return 0                # mailbox free, no DAC sample
        if a in (0xA11100, 0xA11101):
            return 0                # the Z80's bus granted at once
        if 0xA10000 <= a < 0xA10020:
            raise m68krun.M68kError('I/O read at $%06X (pc $%06X)'
                                    % (a, self.pc))
        return super().read(addr, size)

    def write(self, addr, size, value):
        a = addr & 0xFFFFFF
        if 0xC00000 <= a < 0xC00020:
            words = ([(value >> 16) & 0xFFFF, value & 0xFFFF] if size == 4
                     else [value & 0xFFFF])
            for w in words:
                if a & 0x1C == 0:
                    self.vdp.data(w)
                elif a & 0x1C == 4:
                    self.vdp.control(w)
                else:
                    raise m68krun.M68kError('VDP write at $%06X' % a)
            return
        if 0xA00000 <= a < 0xA10000:
            if size != 1:
                raise m68krun.M68kError('Z80 write of %d bytes' % size)
            if a == 0xA00003:
                self.flush_cmd()
                self.cmd = (len(self.frames), [value & 0xFF])
            elif 0xA00004 <= a <= 0xA00006 and self.cmd is not None:
                self.cmd[1].append(value & 0xFF)
            return
        if 0xA11100 <= a < 0xA11300:
            return
        super().write(addr, size, value)

    def flush_cmd(self):
        if self.cmd is not None:
            n, b = self.cmd
            self.sound.append('M %d %s' % (n, ' '.join('%02X' % x for x in b)))
            self.cmd = None

    def r8(self, name, off=0):
        return self.ram[self.addr[name] + off]

    def r16(self, name, off=0):
        a = self.addr[name] + off
        return (self.ram[a] << 8) | self.ram[a + 1]

    # -- interrupts -------------------------------------------------------------
    @property
    def sr(self):
        return 0x2000 | (self.imask << 8) | self.ccr

    def interrupt(self, level):
        """Run the level's handler now, to its rte."""
        ret_sp = self.a[7]
        self.push(4, self.pc)
        self.push(2, self.sr)
        self.imask = level
        stub = self.stubs[level]
        if stub >= RAM:
            if (self.read(stub, 2)) != 0x4EF9:
                raise m68krun.M68kError('no jmp in the stub at $%06X' % stub)
            self.pc = self.read(stub + 2, 4) & 0xFFFFFF
        else:
            self.pc = stub
        while self.a[7] != ret_sp:
            self.step()

    def event(self, level):
        """The VDP raises the level's interrupt (if enabled)."""
        on = (self.vdp.regs[0] & 0x10) if level == 4 else \
             (self.vdp.regs[1] & 0x20)
        if not on:
            return
        if self.imask < level:
            self.interrupt(level)
        else:
            self.held[level] = True

    def unmasked(self):
        for level in (6, 4):
            if self.held[level] and self.imask < level:
                self.held[level] = False
                self.interrupt(level)

    def rte(self):
        sr = self.pop(2)
        self.pc = self.pop(4)
        self.imask = (sr >> 8) & 7
        self.ccr = sr & 0x1F
        self.unmasked()

    # -- frames -----------------------------------------------------------------
    def frame(self):
        v = self.vdp
        cut = hzlayer.link5(v.vram, v.regs[5]) == 0
        self.event(4)
        self.flush_cmd()
        vs = b''.join(w.to_bytes(2, 'big') for w in v.vsram)
        k = len(self.frames)
        f = 'F %d %08X %s %08X %s %08X' % (
            k, v.held.crc(v.vram),
            ''.join('%03X' % (c & 0x0EEE) for c in v.cram),
            crc(vs), ''.join('%02X' % r for r in v.regs[:19]),
            hzlayer.sprites_crc(v.vram, v.regs[5], cut))
        r = 'R %d %08X %08X %08X %08X %08X %08X' % ((k,) + self.state_crcs())
        self.frames.append((f, r))
        self.pad = self.pads[k] if k < len(self.pads) else 0
        # Until Game_Start, $FF7000-$FFEFFF holds what LZW_Unpack left (the
        # port unpacks nothing): the state is compared from there on.
        if self.expect is not None and (
                k >= len(self.expect) or self.expect[k][0] != f
                or (self.started and self.expect[k][1] != r)):
            self.mismatch = k
            self.dump = self.state_images() + bytes(v.held.vram(v.vram))
            raise Stop()
        if len(self.frames) >= self.limit:
            self.how = 'frames'
            raise Stop()
        self.event(6)

    def state_images(self):
        ram = self.ram
        g = bytearray(GLOBALS_BYTES)
        for off, name, d, n in GLOBALS:
            a = self.addr[name] + d
            g[off:off + n] = ram[a:a + n]
        mechs = b''.join(image(ram, self.addr['v_mech1'] + p * 0x100,
                               MECH_FIELDS, MECH_BYTES) for p in (0, 1))
        units = b''.join(image(ram, self.addr['v_units1'] + i * 0x50,
                               UNIT_FIELDS, UNIT_BYTES) for i in range(UNITS))
        a = self.addr['v_outposts']
        posts = bytes(ram[a:a + POSTS * 8])
        a = self.addr['v_grid']
        grid = bytes(ram[a:a + GRID])
        a = self.addr['v_bullets1']
        shots = b''.join(bytes(ram[a + i * 0x20:a + i * 0x20 + SHOT_BYTES])
                         for i in range(SHOTS))
        return bytes(g) + mechs + units + posts + grid + shots

    def state_crcs(self):
        s = self.state_images()
        g, s = s[:GLOBALS_BYTES], s[GLOBALS_BYTES:]
        m, s = s[:2 * MECH_BYTES], s[2 * MECH_BYTES:]
        u, s = s[:UNITS * UNIT_BYTES], s[UNITS * UNIT_BYTES:]
        p, s = s[:POSTS * 8], s[POSTS * 8:]
        grid, shots = s[:GRID], s[GRID:]
        return crc(g), crc(m), crc(u), crc(p), crc(grid), crc(shots)

    # -- the port's CHANGED points, as patches ------------------------------------
    def patch(self, cpu, insn):
        p = self.patches.get(insn.addr)
        if p is not None:
            self.hits[insn.addr] += 1
            p()

    def p_setsprite(self):
        self.d[0] &= 0xFF

    def p_patrol(self):
        self.d[1] = 2

    def p_aim_mech(self):
        self.write(self.a[0] + 0x4C, 4, self.a[2])

    def p_supply(self):
        self.a[3] |= 0xFF000000

    def p_cursor(self):
        if self.ram[self.a[0] & 0xFFFF] & 0x08 and \
                (self.d[0] & 0xFF) not in (1, 2):
            self.d[0] = (self.d[0] & ~0xFF) | 1

    # -- hooks ------------------------------------------------------------------
    def rts(self):
        self.pc = self.pop(4)

    def wait_vblank(self):
        self.rts()
        if self.r8('v_vdp_reg1', 1) & 0x40:
            self.frame()

    def read_joypads(self):
        self.ram[self.addr['v_joypad1']] = getattr(self, 'pad', 0)
        self.ram[self.addr['v_joypad1'] + 1] = 0
        self.rts()

    def dma_trigger(self):
        self.write(self.a[6], 2, self.r16('v_dma_word'))
        self.rts()

    def stop_title(self):
        self.how = 'aborted' if self.r16('v_load_abort') else 'title'
        raise Stop()

    def stop_game_over(self):
        self.how = 'game_over'
        raise Stop()

    def step(self):
        h = self.hooks.get(self.pc)
        if h is not None:
            self.count += 1
            h()
            return
        if self.pc == self.game_start:
            self.started = True
        if self.pc == self.load_wait and self.r16('v_load_busy'):
            busy = self.r16('v_load_busy')
            self.frame()
            if self.r16('v_load_busy') == busy:
                raise m68krun.M68kError('the loading screen waits forever')
        insn = self.decode(self.pc)
        if insn.mn == 'rte':
            self.count += 1
            self.rte()
            return
        sr = self.sr_writes.get(self.pc)
        super().step()
        if sr is not None:
            self.imask = (sr >> 8) & 7
            self.unmasked()


def find(rom, sym, label, mn):
    a = sym[label]
    for _ in range(40):
        insn = m68k.decode(rom, a)
        if insn.mn == mn:
            return a
        a += insn.size
    raise SystemExit('%s not found after %s' % (mn, label))


def sr_writes(rom):
    """Every move.w #imm,sr in the code (the game's only writes to the
    status register's mask): address -> imm."""
    out = {}
    pc = 0x200
    import re
    pat = re.compile(r'^move\.w\s+#\$([0-9A-F]+),sr$')
    while pc < len(rom):
        try:
            insn = m68k.decode(rom, pc)
        except Exception:
            pc += 2
            continue
        m = pat.match(m68k.text(insn))
        if m:
            out[pc] = int(m.group(1), 16)
        pc += 2
    return out


_SR = None


def run_original(rom, sym, index, pads, limit, expect=None):
    global _SR
    if _SR is None:
        _SR = sr_writes(rom)
    cpu = Cpu(rom, sym, pads, limit, expect)
    cpu.sr_writes = _SR
    for name, v in (('v_options', 5), ('v_option2', 0xFF)):
        cpu.ram[cpu.addr[name]] = v
    cpu.ram[cpu.addr['v_hw_version'] + 1] = 0xA0
    cpu.ram[cpu.addr['v_vdp_reg1']] = 0x81
    cpu.ram[cpu.addr['v_vdp_reg1'] + 1] = 0x24
    cpu.ram[cpu.addr['v_sfx_priority']] = 0xFF
    cpu.ram[cpu.addr['v_sfx_queued']] = 0xFF
    cpu.ram[cpu.addr['v_split_done']] = 0x01   # MainLoop_Title: word $100
    cpu.ram[cpu.addr['v_game_type']] = 0xFF    # what MainLoop_Title stores
    cpu.ram[cpu.addr['v_game_type'] + 1] = 0xFF
    cpu.ram[cpu.addr['v_demo_index'] + 1] = index
    cpu.a[7] = int.from_bytes(rom[0:4], 'big')
    cpu.d[0] = 0xFFFFFFFF                       # MenuModule_Title's -1
    # Start after MainLoop_Title's jsr MenuModule_Title.
    target = '$%X' % sym['MenuModule_Title']
    pc = sym['MainLoop_Title']
    while True:
        insn = m68k.decode(rom, pc)
        pc += insn.size
        if insn.mn == 'jsr' and target in m68k.text(insn):
            break
        if pc > sym['MainLoop_Title'] + 0x80:
            raise SystemExit('MainLoop_Title: no jsr to MenuModule_Title')
    cpu.pc = pc
    try:
        while True:
            cpu.step()
    except Stop:
        pass
    cpu.flush_cmd()
    return cpu


# -- the port, and the comparison ---------------------------------------------

def regs_shown(line):
    """An F line with registers 19-23 dropped (see the module's doc)."""
    f = line.split()
    f[5] = f[5][:38]
    return ' '.join(f)


def run_port(hz_demo, assets, index, frames, pads_path, out, state=None):
    cmd = [hz_demo, assets, str(index), out, '--frames', str(frames)]
    if pads_path:
        cmd += ['--pads', pads_path]
    if state is not None:
        cmd += ['--state', str(state[0]), state[1]]
    subprocess.run(cmd, check=True)
    fl, rl, sound, end = [], [], [], None
    with open(out) as f:
        for ln in f:
            ln = ln.rstrip('\n')
            if ln.startswith('F '):
                fl.append(regs_shown(ln))
            elif ln.startswith('R '):
                rl.append(ln)
            elif ln.startswith('M '):
                sound.append(ln)
            elif ln.startswith('E '):
                end = ln.split()
    return list(zip(fl, rl)), sound, end


def name_vram(a):
    for lo, hi, what in ((0x0000, 0x4000, 'map tiles'),
                         (0x4000, 0x4400, 'the mechs\' frames'),
                         (0x4400, 0xA000, 'sprite tiles'),
                         (0xA000, 0xC000, 'HUD tiles'),
                         (0xC000, 0xD000, 'plane A'),
                         (0xD000, 0xD280, 'the sprite table'),
                         (0xD400, 0xD800, 'H scroll'),
                         (0xE000, 0xF000, 'plane B'),
                         (0xF000, 0x10000, 'the window')):
        if lo <= a < hi:
            return what
    return 'VRAM'


def explain(ours, theirs, started=True):
    """Field names where two state dumps (images + VRAM) differ (only
    VRAM's before Game_Start)."""
    out = []
    o, t = ours, theirs
    if not started:
        n = GLOBALS_BYTES + 2 * MECH_BYTES + UNITS * UNIT_BYTES + POSTS * 8 \
            + GRID + SHOTS * SHOT_BYTES
        o = bytes(n) + o[n:]
        t = bytes(n) + t[n:]
    for off, name, d, n in GLOBALS:
        if o[off:off + n] != t[off:off + n]:
            out.append('%s: port %s, original %s' % (
                name, o[off:off + n].hex(), t[off:off + n].hex()))
    base = GLOBALS_BYTES
    for p in (0, 1):
        for off, n, name in MECH_FIELDS:
            a = base + p * MECH_BYTES + off
            if o[a:a + n] != t[a:a + n]:
                out.append('mech %d %s: port %s, original %s' % (
                    p + 1, name, o[a:a + n].hex(), t[a:a + n].hex()))
    base += 2 * MECH_BYTES
    for i in range(UNITS):
        for off, n, name in UNIT_FIELDS:
            a = base + i * UNIT_BYTES + off
            if o[a:a + n] != t[a:a + n]:
                out.append('unit %d %s: port %s, original %s' % (
                    i, name, o[a:a + n].hex(), t[a:a + n].hex()))
    base += UNITS * UNIT_BYTES
    for i in range(POSTS):
        a = base + i * 8
        if o[a:a + 8] != t[a:a + 8]:
            out.append('post %d: port %s, original %s' % (
                i, o[a:a + 8].hex(), t[a:a + 8].hex()))
    base += POSTS * 8
    cells = [c for c in range(GRID) if o[base + c] != t[base + c]]
    for c in cells[:8]:
        out.append('v_grid cell %d (x %d, y %d): port %02X, original %02X' % (
            c, c & 63, c >> 6, o[base + c], t[base + c]))
    base += GRID
    for i in range(SHOTS):
        for off, n, name in SHOT_FIELDS:
            a = base + i * SHOT_BYTES + off
            if o[a:a + n] != t[a:a + n]:
                out.append('shot %d %s: port %s, original %s' % (
                    i, name, o[a:a + n].hex(), t[a:a + n].hex()))
    base += SHOTS * SHOT_BYTES
    vo, vt = o[base:base + 0x10000], t[base:base + 0x10000]
    diffs = [a for a in range(0, 0x10000) if vo[a] != vt[a]]
    if diffs:
        a = diffs[0]
        out.append('VRAM: %d bytes differ, first $%04X (%s): port %02X, '
                   'original %02X' % (len(diffs), a, name_vram(a), vo[a],
                                      vt[a]))
    return out


def describe_frame(ours, theirs, started=True):
    (of, orr), (tf, tr) = ours, theirs
    o, t = of.split(), tf.split()
    what = []
    if o[2] != t[2]:
        what.append('VRAM')
    if o[3] != t[3]:
        ca = [i for i in range(64) if o[3][i * 3:i * 3 + 3] !=
              t[3][i * 3:i * 3 + 3]]
        what.append('CRAM %s' % ca[:8])
    if o[4] != t[4]:
        what.append('VSRAM')
    if o[5] != t[5]:
        ra = [i for i in range(24) if o[5][i * 2:i * 2 + 2] !=
              t[5][i * 2:i * 2 + 2]]
        what.append('registers %s (port %s, original %s)' % (
            ra, [o[5][i * 2:i * 2 + 2] for i in ra],
            [t[5][i * 2:i * 2 + 2] for i in ra]))
    if o[6] != t[6]:
        what.append('sprites as drawn')
    names = ('globals', 'mechs', 'units', 'posts', 'grid', 'shots')
    if not started:
        names = ()
    for i, n in enumerate(names):
        if orr.split()[2 + i] != tr.split()[2 + i]:
            what.append(n)
    return ', '.join(what)


def check_demo(rom, sym, hz_demo, assets, index, frames, pads, pads_path,
               keep):
    t0 = time.time()
    out = os.path.join(keep, 'demo%d.txt' % index)
    expect, sound, end = run_port(hz_demo, assets, index, frames, pads_path,
                                  out)
    cpu = run_original(rom, sym, index, pads, frames, expect)
    took = time.time() - t0
    errors = []
    k = cpu.mismatch
    if k is not None:
        state = os.path.join(keep, 'demo%d_state.bin' % index)
        theirs = expect[k] if k < len(expect) else None
        if theirs is None:
            errors.append('frame %d: the port stopped (%s) after %d frames'
                          % (k, ' '.join(end[1:]), len(expect)))
        else:
            run_port(hz_demo, assets, index, frames, pads_path,
                     out + '.again', (k, state))
            with open(state, 'rb') as f:
                ours = f.read()
            errors.append('frame %d differs: %s' % (
                k, describe_frame(theirs, cpu.frames[k], cpu.started)))
            for ln in explain(ours, cpu.dump, cpu.started)[:24]:
                errors.append('    ' + ln)
    else:
        if len(cpu.frames) != len(expect):
            errors.append('%d frames, the port %d' % (len(cpu.frames),
                                                      len(expect)))
        if cpu.how != end[1]:
            errors.append('ended %s, the port %s' % (cpu.how, end[1]))
    upto = k if k is not None else 1 << 30
    ours = [m for m in sound if int(m.split()[1]) < upto]
    theirs = [m for m in cpu.sound if int(m.split()[1]) < upto]
    if ours != theirs:
        for i in range(max(len(ours), len(theirs))):
            a = ours[i] if i < len(ours) else '-'
            b = theirs[i] if i < len(theirs) else '-'
            if a != b:
                errors.append('sound command %d: port "%s", original "%s"'
                              % (i, a, b))
                break
    hits = ' '.join('$%X %d' % (a, n) for a, n in sorted(cpu.hits.items()))
    print('demo %2d: %d frames (%s), %d sound commands, %d instructions, '
          '%.0f s, patches hit: %s: %s' % (
              index, len(cpu.frames), cpu.how or '-', len(theirs),
              cpu.count, took, hits, 'ok' if not errors else 'DIFFERS'))
    for e in errors:
        print('    ' + e)
    sys.stdout.flush()
    return not errors


def read_sym(path):
    sym = {}
    for ln in open(path):
        f = ln.split()
        if len(f) == 2 and f[1].startswith('$'):
            sym[f[0]] = int(f[1][1:], 16)
    return sym


def main():
    args = sys.argv[1:]
    if len(args) < 4:
        sys.exit(__doc__)
    rom = open(args[0], 'rb').read()
    sym = read_sym(args[1])
    hz_demo, assets = args[2], args[3]
    demos = list(range(16))
    frames = 5000
    pads_path = None
    done = None
    keep = None
    start_at = None
    i = 4
    while i < len(args):
        a = args[i]
        if a == '--demos':
            demos = [int(x) for x in args[i + 1].split(',')]
            i += 1
        elif a == '--frames':
            frames = int(args[i + 1])
            i += 1
        elif a == '--pads':
            pads_path = args[i + 1]
            i += 1
        elif a == '--done':
            done = args[i + 1]
            i += 1
        elif a == '--keep':
            keep = args[i + 1]
            i += 1
        elif a == '--start-at':
            k = int(args[i + 1])
            start_at = k
            i += 1
        else:
            sys.exit('what is %s?' % a)
        i += 1
    pads = []
    if pads_path:
        pads = [int(w, 16) for w in open(pads_path).read().split()]
    tmp = keep or tempfile.mkdtemp(prefix='hzdemo')
    os.makedirs(tmp, exist_ok=True)
    if start_at is not None:
        pads = [0] * start_at + [0x80, 0x80]
        pads_path = os.path.join(tmp, 'start_at.txt')
        with open(pads_path, 'w') as f:
            f.write(' '.join('%X' % b for b in pads) + '\n')
    if done:
        os.makedirs(done, exist_ok=True)
    bad = 0
    for k in demos:
        mark = os.path.join(done, 'demo%d.ok' % k) if done else None
        if mark and os.path.exists(mark):
            print('demo %2d: agreed before (%s)' % (k, mark))
            continue
        if check_demo(rom, sym, hz_demo, assets, k, frames, pads, pads_path,
                      tmp):
            if mark:
                open(mark, 'w').write('ok\n')
        else:
            bad += 1
    print('all agree' if not bad else '%d demos differ' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
