#!/usr/bin/env python3
"""The order menu (src/game/order_menu.c) against the original module.

    hzordermenu_check.py ROM SYM HZ_ORDERMENU ASSETS [--cases N] [--seed S]
                         [--keep DIR] [--quiet] [--coverage]

tools/hz_ordermenu makes the cases (real games with the menus' state
poked, see that file) and runs the port on them; this runs the original
routines from the ROM image on tools/m68krun.py's 68000, with a model of
the VDP's ports (below), frame for frame as the harness does: per mech, as
Mech_Update does, OrderMenu_Input while the menu is open (alive and not
closing) or C opening it; then OrderMenu_UpdateBoth. It compares all of
RAM (but v_textbuf and v_dma_word, the original's scratch, and the
stack) and the VDP's
VRAM, CRAM, VSRAM and registers (but 1 and the DMA registers 19-23: the
module turns the display off and on for its fills, which the port has no
need to do).

The port's CHANGED point is applied to the original as a patch:
OrderMenu_DrawCursor, carrying, takes any row other than 1 or 2 as 1 (the
original only moves row 0, and reads OrderMenuCursorPos past its end for
the rest).

The VDP model: the control port takes register writes ($8rvv) and two-word
commands (code and address); CD5 with DMA enabled (register 1 b4) and
register 23's mode $80 makes the next data write a fill: (register 19-20)
+ 1 bytes of the value's high byte from the address, stepping by register
15. Data writes go to VRAM (a word, byte-swapped at an odd address, as
tools/hz_vdp.c), CRAM or VSRAM, then the address steps by register 15.
Which VRAM bytes are a sprite table's is kept too (tools/hzlayer.py: the
front's and the game end's checks leave them out).
Reading the control port gives 0 (no DMA running). The RAM routine
v_dma_trigger ($FFF8F2: move.w #v_dma_word,(a6) / rts) is run as what it
does.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import hzlayer                                          # noqa: E402
import m68krun                                          # noqa: E402

RAM_SIZE = 0x10000
A_MECH = (0x8B94, 0x8C94)
DMA_TRIGGER = 0xFFF8F2
DMA_WORD = 0xF8F4
SKIP_RAM = [(0xF8F4, 0xF8F6, 'v_dma_word'), (0xF958, 0xF980, 'v_textbuf'),
            (0xFC00, 0x10000, 'stack')]
SKIP_REGS = {1, 19, 20, 21, 22, 23}


class Vdp:
    def __init__(self, vram, cram, vsram, regs):
        self.vram = bytearray(vram)
        self.cram = list(cram)
        self.vsram = list(vsram)
        self.regs = list(regs)
        self.pending = None
        self.code = 1
        self.addr = 0
        self.fill = False
        self.held = hzlayer.Held(self.regs[5])

    def control(self, w):
        if self.pending is not None:
            w1, self.pending = self.pending, None
            self.code = ((w1 >> 14) & 3) | ((w >> 2) & 0x3C)
            self.addr = (w1 & 0x3FFF) | ((w & 3) << 14)
            if self.code & 0x20 and self.regs[1] & 0x10:
                mode = self.regs[23] >> 6
                if mode == 2:
                    self.fill = True
                else:
                    raise m68krun.M68kError('VDP DMA mode %d not modelled'
                                            % mode)
            return
        if (w & 0xC000) == 0x8000:
            r = (w >> 8) & 0x1F
            if r < 24:
                self.regs[r] = w & 0xFF
                if r == 5:
                    self.held.table(w & 0xFF)
            return
        self.pending = w

    def data(self, w):
        self.pending = None
        inc = self.regs[15]
        if self.fill:
            self.fill = False
            n = self.regs[19] | (self.regs[20] << 8)
            a = self.addr
            for _ in range(n + 1):
                self.vram[a & 0xFFFF] = (w >> 8) & 0xFF
                self.held.wrote(a & 0xFFFF)
                a += inc if inc else 1
            self.addr = a & 0xFFFF
            return
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
        self.addr = (self.addr + inc) & 0xFFFF


class Cpu(m68krun.M68k):
    """The interpreter with the VDP's ports and v_dma_trigger."""

    def __init__(self, rom):
        super().__init__(rom)
        self.vdp = None

    def read(self, addr, size):
        a = addr & 0xFFFFFF
        if 0xC00000 <= a < 0xC00020:
            if a & 0x1C == 4:
                return 0                    # status: no DMA running
            raise m68krun.M68kError('VDP read at $%06X' % a)
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
        super().write(addr, size, value)

    def step(self):
        if self.pc == DMA_TRIGGER:
            self.count += 1
            self.write(self.a[6], 2, (self.ram[DMA_WORD] << 8)
                       | self.ram[DMA_WORD + 1])
            self.pc = self.pop(4)
            return
        super().step()


def read_cases(f_in, f_out):
    """(frames, pads, ram, vdp) before; (ram, vdp) after the port."""
    while True:
        b = f_in.read(1)
        if not b:
            return
        n = b[0]
        pads = f_in.read(n * 2)
        ram = f_in.read(RAM_SIZE)
        vdp_in = read_vdp(f_in)
        ram_out = f_out.read(RAM_SIZE)
        vdp_out = read_vdp(f_out)
        yield [(pads[i * 2], pads[i * 2 + 1]) for i in range(n)], ram, \
            vdp_in, ram_out, vdp_out


def read_vdp(f):
    vram = f.read(0x10000)
    c = f.read(128)
    v = f.read(80)
    regs = f.read(24)
    cram = [(c[i] << 8) | c[i + 1] for i in range(0, 128, 2)]
    vsram = [(v[i] << 8) | v[i + 1] for i in range(0, 80, 2)]
    return vram, cram, vsram, list(regs)


class Original:
    def __init__(self, rom, sym):
        self.rom = rom
        self.sym = sym
        self.input = sym['OrderMenu_Input']
        self.update = sym['OrderMenu_UpdateBoth']
        self.cursor_store = sym['OrderMenu_DrawCursor.store']
        self.hits = 0

    def run(self, frames, ram, vdp, seen=None):
        cpu = Cpu(self.rom)
        cpu.ram = bytearray(ram)
        cpu.vdp = Vdp(*vdp)
        store = self.cursor_store

        def trace(c, insn):
            if seen is not None:
                seen.add(insn.addr)
            if insn.addr == store:
                # CHANGED (order_menu.c draw_cursor): carrying, any row
                # other than 1 or 2 becomes 1
                if c.ram[(c.a[0] & 0xFFFF)] & 0x08 and \
                        (c.d[0] & 0xFF) not in (1, 2):
                    if (c.d[0] & 0xFF) != 0:
                        self.hits += 1
                    c.d[0] = (c.d[0] & ~0xFF) | 1
        cpu.trace = trace
        r = cpu.ram
        for pads in frames:
            for p in (0, 1):
                base = A_MECH[p]
                r[base + 0x29] = r[base + 0x28]
                r[base + 0x28] = pads[p]
                flags = r[base]
                if flags & 0x20 or r[base + 0x6D]:
                    continue
                if flags & 0x40:
                    cpu.a[0] = 0xFF0000 | base
                    cpu.call(self.input)
                elif (r[base + 0x28] ^ r[base + 0x29]) & r[base + 0x28] & 0x20:
                    r[base] = flags | 0x40
            cpu.call(self.update)
        v = cpu.vdp
        return bytes(cpu.ram), (bytes(v.vram), v.cram, v.vsram, v.regs), \
            cpu.count


def describe_ram(a):
    names = [(0x0000, 'v_map_blocks'), (0x5000, 'layout'), (0x8004, 'scroll'),
             (0x89B4, 'posts'), (0x8A66, 'cameras'), (0x8B94, 'mech1'),
             (0x8C94, 'mech2'), (0x8D94, 'units'), (0xB4D4, 'grid'),
             (0xC4D4, 'radar_buf1'), (0xCCD4, 'radar_buf2'),
             (0xD99C, 'gauge_queue'), (0xDBFC, 'unit_count'),
             (0xDC52, 'radar_sprites'), (0xF848, 'split'),
             (0xF90A, 'sfx'), (0xF926, 'v_map')]
    best = names[0]
    for n in names:
        if n[0] <= a:
            best = n
    return '$FF%04X (%s +$%X)' % (a, best[1], a - best[0])


def compare(ram_ours, vdp_ours, ram_theirs, vdp_theirs):
    diffs = []
    skip = bytearray(RAM_SIZE)
    for lo, hi, _ in SKIP_RAM:
        skip[lo:hi] = b'\x01' * (hi - lo)
    for a in range(RAM_SIZE):
        if not skip[a] and ram_ours[a] != ram_theirs[a]:
            diffs.append('RAM %s: port $%02X, original $%02X'
                         % (describe_ram(a), ram_ours[a], ram_theirs[a]))
            if len(diffs) > 8:
                return diffs
    vo, co, so, ro = vdp_ours
    vt, ct, st, rt = vdp_theirs
    for a in range(0x10000):
        if vo[a] != vt[a]:
            diffs.append('VRAM $%04X: port $%02X, original $%02X'
                         % (a, vo[a], vt[a]))
            if len(diffs) > 8:
                return diffs
    for i in range(64):
        if co[i] != ct[i]:
            diffs.append('CRAM %d: port $%04X, original $%04X'
                         % (i, co[i], ct[i]))
    for i in range(40):
        if so[i] != st[i]:
            diffs.append('VSRAM %d: port $%04X, original $%04X'
                         % (i, so[i], st[i]))
    for i in range(24):
        if i not in SKIP_REGS and ro[i] != rt[i]:
            diffs.append('register %d: port $%02X, original $%02X'
                         % (i, ro[i], rt[i]))
    return diffs


def describe_case(frames, ram):
    out = []
    split = (ram[0xF848] << 8) | ram[0xF849]
    out.append('map %d, %s' % ((ram[0xF926] << 8) | ram[0xF927],
                                'split' if split else 'wide'))
    for p in (0, 1):
        b = A_MECH[p]
        out.append('P%d: flags $%02X view $%02X step %d closing %d row %d/%d '
                   'type %d/%d order %d/%d money %d cost %d prod $%02X '
                   'radar %d/%d step %d' % (
                       p + 1, ram[b], ram[b + 0x42], ram[b + 0x40],
                       ram[b + 0x6D], ram[b + 0x48], ram[b + 0x49],
                       ram[b + 0x4A], ram[b + 0x4B], ram[b + 0x4C],
                       ram[b + 0x4D], (ram[b + 0x54] << 8) | ram[b + 0x55],
                       (ram[b + 0x5C] << 8) | ram[b + 0x5D], ram[b + 0x4F],
                       ram[b + 0x52], ram[b + 0x53], ram[b + 0x41]))
    out.append('pads: ' + ' '.join('%02X/%02X' % f for f in frames))
    return out


def report_coverage(rom, sym, seen):
    """The module's instructions no case ran."""
    import m68k
    lo, hi = sym['OrderMenu_UpdateBoth'], sym['HUD_QueueBar']
    rev = sorted((a, n) for n, a in sym.items() if lo <= a < hi)
    pc = lo
    missed = []
    while pc < hi:
        try:
            insn = m68k.decode(rom, pc)
        except Exception:
            pc += 2
            continue
        if insn.addr not in seen:
            missed.append(insn.addr)
        pc += insn.size
    print('%d instructions of the module never ran:' % len(missed))
    for a in missed:
        name = max((n for n in rev if n[0] <= a), default=(lo, '?'))[1]
        print('  $%06X  %s' % (a, name))


def main():
    args = sys.argv[1:]
    if len(args) < 4:
        print(__doc__)
        return 2
    rom_path, sym_path, harness, assets = args[:4]
    opts = args[4:]
    cases, seed, keep, quiet, coverage = 200, 1, None, False, False
    i = 0
    while i < len(opts):
        if opts[i] == '--cases':
            cases = int(opts[i + 1])
            i += 2
        elif opts[i] == '--seed':
            seed = int(opts[i + 1])
            i += 2
        elif opts[i] == '--keep':
            keep = opts[i + 1]
            i += 2
        elif opts[i] == '--quiet':
            quiet = True
            i += 1
        elif opts[i] == '--coverage':
            coverage = True
            i += 1
        else:
            print('unknown option %s' % opts[i])
            return 2
    rom = open(rom_path, 'rb').read()
    sym = m68krun.load_symbols(sym_path)
    folder = keep or os.path.join(tempfile.gettempdir(),
                                   'hzordermenu_%d' % os.getpid())
    os.makedirs(folder, exist_ok=True)
    f_in = os.path.join(folder, 'in_%d.bin' % seed)
    f_out = os.path.join(folder, 'out_%d.bin' % seed)
    subprocess.check_call([harness, assets, str(seed), str(cases), f_in,
                           f_out])
    orig = Original(rom, sym)
    seen = set() if coverage else None
    bad = 0
    n = 0
    insns = 0
    frames_run = 0
    with open(f_in, 'rb') as fi, open(f_out, 'rb') as fo:
        for frames, ram, vdp_in, ram_out, vdp_out in read_cases(fi, fo):
            try:
                ram_t, vdp_t, count = orig.run(frames, ram, vdp_in, seen)
            except m68krun.M68kError as e:
                print('case %d: the original stopped: %s' % (n, e))
                for line in describe_case(frames, ram):
                    print('   ', line)
                bad += 1
                n += 1
                continue
            insns += count
            frames_run += len(frames)
            diffs = compare(ram_out, vdp_out, ram_t, vdp_t)
            if diffs:
                bad += 1
                if bad <= 5:
                    print('case %d differs:' % n)
                    for line in describe_case(frames, ram):
                        print('   ', line)
                    for d in diffs:
                        print('   ', d)
            n += 1
    if keep is None:
        for f in (f_in, f_out):
            os.remove(f)
        os.rmdir(folder)
    print('seed %d: %d cases, %d frames, %d differ; %d instructions of the '
          'original; CHANGED patch hit %d' % (seed, n, frames_run, bad, insns,
                                              orig.hits))
    if coverage:
        report_coverage(rom, sym, seen)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
