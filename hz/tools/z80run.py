#!/usr/bin/env python3
"""Run the original Z80 sound driver and log what it writes to the chips.

    python3 tools/z80run.py ROM [--song N] [--sfx F:N[,F:N...]] [--seconds S]
                            [--fade F:N] [--stop F[,F...]] [--pause F[,F...]]
                            [--repeat] [--zero-time] [--vgm OUT.vgm] [--log OUT]

The model for the C port of the driver (asm/hz.asm's Z80Driver): the driver's own
bytes (`Z80Driver`, copied as `Snd_Init` copies $1980 bytes) run on a Z80
interpreter built on tools/z80.py's decoder; the bank window reads the ROM;
the YM2612's timers A and B and the VDP's VBlank interrupt are the only
clocks. The 68k is a script of mailbox commands, posted at the start of a
frame once the mailbox is free, as Snd_* do: Snd_Init's $0A/$07/$08 first,
then the song ($09 N, $01 -1 with --repeat, as the map loader) and the sfx
($02 N priority-from-SfxPriority at frame F).

Time is in the Mega Drive's master clocks (53693175 Hz NTSC): a Z80 cycle is
15, a YM sample 1008 (Timer A's step; Timer B's is 16 samples), a frame
896040. Instruction cycles are approximate (4 + 3 a byte); while the driver
idles (MainLoop, mailbox empty, no timer flag) time jumps to the next event.

--zero-time (session 25): instructions take no time, so every handler runs
whole at the moment its event happens -- the C port's model (src/audio/
hz_snd.c), which `make sound` checks against this one write for write. A
VBlank then only ever finds the driver idle, or waiting in Cmd_Pause with
interrupts off, where it is lost; the pause's wait loop counts as idle.

Output: --log, one line per chip write `t part reg value` (part 0/1 YM, P
PSG); --vgm, a VGM 1.51 file (YM2612 + SN76489) any VGM player plays.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z80  # noqa: E402

MASTER = 53693175
Z80_CYCLE = 15
YM_SAMPLE = 1008
FRAME = 896040
DRIVER_ROM = 0x20B00
DRIVER_COPY = 0x1980
SFX_PRIORITY = None          # read from the ROM by label address below

FS, FZ, FH, FPV, FN, FC = 0x80, 0x40, 0x10, 0x04, 0x02, 0x01


def parity(v):
    v ^= v >> 4
    v ^= v >> 2
    v ^= v >> 1
    return not (v & 1)


class Chips:
    """The YM2612's timers and status, the bank register, the write log."""

    def __init__(self):
        self.reg_addr = [0, 0]
        self.ta = 0
        self.tb = 0
        self.ctrl = 0
        self.flags = 0
        self.next_a = None
        self.next_b = None
        self.writes = []          # (time, part, reg, value)
        self.now = 0

    def timer_a_period(self):
        return (1024 - self.ta) * YM_SAMPLE

    def timer_b_period(self):
        return (256 - self.tb) * 16 * YM_SAMPLE

    def advance(self, t):
        """Run the timers up to time t."""
        while self.next_a is not None and self.next_a <= t:
            if self.ctrl & 4:
                self.flags |= 1
            self.next_a += self.timer_a_period()
        while self.next_b is not None and self.next_b <= t:
            if self.ctrl & 8:
                self.flags |= 2
            self.next_b += self.timer_b_period()

    def write(self, part, reg, value):
        self.writes.append((self.now, part, reg, value))
        if part == 0:
            if reg == 0x24:
                self.ta = (self.ta & 3) | value << 2
            elif reg == 0x25:
                self.ta = (self.ta & ~3) | (value & 3)
            elif reg == 0x26:
                self.tb = value
            elif reg == 0x27:
                if value & 1 and not self.ctrl & 1:
                    self.next_a = self.now + self.timer_a_period()
                if not value & 1:
                    self.next_a = None
                if value & 2 and not self.ctrl & 2:
                    self.next_b = self.now + self.timer_b_period()
                if not value & 2:
                    self.next_b = None
                if value & 0x10:
                    self.flags &= ~1
                if value & 0x20:
                    self.flags &= ~2
                self.ctrl = value


class Machine:
    def __init__(self, rom):
        self.rom = rom
        self.ram = bytearray(0x2000)
        self.ram[0:DRIVER_COPY] = rom[DRIVER_ROM:DRIVER_ROM + DRIVER_COPY]
        self.chips = Chips()
        self.bank_shift = 0
        self.bank = 0
        self.cache = {}
        self.owner = {}           # byte -> cached insn start
        self.a = self.f = self.b = self.c = self.d = self.e = 0
        self.h = self.l = 0
        self.alt = [0] * 8
        self.ix = self.iy = 0
        self.sp = 0
        self.pc = 0
        self.iff = False
        self.halted = False
        self.t = 0
        self.zero_time = False

    # memory ------------------------------------------------------------
    def rd(self, addr):
        addr &= 0xFFFF
        if addr < 0x4000:                 # RAM, mirrored at $2000
            return self.ram[addr & 0x1FFF]
        if addr == 0x4000:
            self.chips.now = self.t
            self.chips.advance(self.t)
            return self.chips.flags
        if 0x4000 <= addr < 0x6000:
            return 0
        if addr >= 0x8000:
            a = (self.bank << 15) | (addr & 0x7FFF)
            return self.rom[a] if a < len(self.rom) else 0xFF
        return 0xFF

    def wr(self, addr, v):
        addr &= 0xFFFF
        v &= 0xFF
        if addr < 0x4000:                 # RAM, mirrored at $2000
            addr &= 0x1FFF
            self.ram[addr] = v
            s = self.owner.get(addr)
            if s is not None:
                self.cache.pop(s, None)
            return
        self.chips.now = self.t
        if addr in (0x4000, 0x4002):
            self.chips.reg_addr[(addr >> 1) & 1] = v
        elif addr in (0x4001, 0x4003):
            part = (addr >> 1) & 1
            self.chips.write(part, self.chips.reg_addr[part], v)
        elif addr == 0x6000:
            self.bank_shift = (self.bank_shift >> 1) | ((v & 1) << 8)
            self.bank = self.bank_shift
        elif addr == 0x7F11:
            self.chips.write('P', 0, v)

    def rd16(self, addr):
        return self.rd(addr) | self.rd(addr + 1) << 8

    def wr16(self, addr, v):
        self.wr(addr, v & 0xFF)
        self.wr(addr + 1, v >> 8)

    def push(self, v):
        self.sp = (self.sp - 2) & 0xFFFF
        self.wr16(self.sp, v)

    def pop(self):
        v = self.rd16(self.sp)
        self.sp = (self.sp + 2) & 0xFFFF
        return v

    # registers ----------------------------------------------------------
    def get16(self, r):
        if r == 'hl':
            return self.h << 8 | self.l
        if r == 'de':
            return self.d << 8 | self.e
        if r == 'bc':
            return self.b << 8 | self.c
        if r == 'af':
            return self.a << 8 | self.f
        return getattr(self, r)

    def set16(self, r, v):
        v &= 0xFFFF
        if r == 'hl':
            self.h, self.l = v >> 8, v & 0xFF
        elif r == 'de':
            self.d, self.e = v >> 8, v & 0xFF
        elif r == 'bc':
            self.b, self.c = v >> 8, v & 0xFF
        elif r == 'af':
            self.a, self.f = v >> 8, v & 0xFF
        else:
            setattr(self, r, v)

    def get8(self, o):
        k = o.kind
        if k == 'r':
            t = o.text
            if t == '(hl)':
                return self.rd(self.get16('hl'))
            if t == '(bc)':
                return self.rd(self.get16('bc'))
            if t == '(de)':
                return self.rd(self.get16('de'))
            return getattr(self, t)
        if k == 'n':
            return o.value
        if k == 'idx':
            return self.rd(getattr(self, o.reg) + o.value)
        if k == 'mem':
            return self.rd(o.value)
        if k == 'port':
            return 0xFF
        raise ValueError(k)

    def set8(self, o, v):
        v &= 0xFF
        k = o.kind
        if k == 'r':
            t = o.text
            if t == '(hl)':
                self.wr(self.get16('hl'), v)
            elif t == '(bc)':
                self.wr(self.get16('bc'), v)
            elif t == '(de)':
                self.wr(self.get16('de'), v)
            else:
                setattr(self, t, v)
        elif k == 'idx':
            self.wr(getattr(self, o.reg) + o.value, v)
        elif k == 'mem':
            self.wr(o.value, v)
        else:
            raise ValueError(k)

    def cond(self, cc):
        f = self.f
        return {'nz': not f & FZ, 'z': bool(f & FZ), 'nc': not f & FC,
                'c': bool(f & FC), 'po': not f & FPV, 'pe': bool(f & FPV),
                'p': not f & FS, 'm': bool(f & FS)}[cc]

    # arithmetic -----------------------------------------------------------
    def szp(self, r):
        return (r & FS) | (FZ if r == 0 else 0) | (FPV if parity(r) else 0)

    def add8(self, a, b, carry=0, sub=False):
        if sub:
            r = a - b - carry
            h = (a & 0xF) - (b & 0xF) - carry < 0
            ov = ((a ^ b) & (a ^ r) & 0x80) != 0
        else:
            r = a + b + carry
            h = (a & 0xF) + (b & 0xF) + carry > 0xF
            ov = (~(a ^ b) & (a ^ r) & 0x80) != 0
        c = r < 0 or r > 0xFF
        r &= 0xFF
        self.f = (r & FS) | (FZ if r == 0 else 0) | (FH if h else 0) | \
            (FPV if ov else 0) | (FN if sub else 0) | (FC if c else 0)
        return r

    def logic(self, r, h):
        r &= 0xFF
        self.f = self.szp(r) | (FH if h else 0)
        return r

    def incdec(self, v, d):
        r = (v + d) & 0xFF
        c = self.f & FC
        if d > 0:
            h = (v & 0xF) == 0xF
            ov = v == 0x7F
        else:
            h = (v & 0xF) == 0
            ov = v == 0x80
        self.f = (r & FS) | (FZ if r == 0 else 0) | (FH if h else 0) | \
            (FPV if ov else 0) | (FN if d < 0 else 0) | c
        return r

    def shift(self, mn, v):
        c = self.f & FC
        if mn == 'rlc':
            co = v >> 7
            r = (v << 1 | co) & 0xFF
        elif mn == 'rrc':
            co = v & 1
            r = (v >> 1 | co << 7) & 0xFF
        elif mn == 'rl':
            co = v >> 7
            r = (v << 1 | c) & 0xFF
        elif mn == 'rr':
            co = v & 1
            r = (v >> 1 | c << 7) & 0xFF
        elif mn == 'sla':
            co = v >> 7
            r = (v << 1) & 0xFF
        elif mn == 'sra':
            co = v & 1
            r = (v >> 1 | (v & 0x80)) & 0xFF
        elif mn == 'srl':
            co = v & 1
            r = v >> 1
        else:
            raise ValueError(mn)
        self.f = self.szp(r) | co
        return r

    # execution --------------------------------------------------------------
    def fetch(self):
        ins = self.cache.get(self.pc)
        if ins is None:
            ins = z80.decode(self.ram, self.pc)
            self.cache[self.pc] = ins
            for i in range(ins.size):
                self.owner[self.pc + i] = self.pc
        return ins

    def step(self):
        ins = self.fetch()
        self.pc = (ins.addr + ins.size) & 0xFFFF
        if not self.zero_time:
            self.t += (4 + 3 * ins.size) * Z80_CYCLE
        mn, ops = ins.mn, ins.ops
        f = getattr(self, 'op_' + mn)
        f(ins, ops)

    def jump_target(self, ops):
        return ops[-1].value

    def op_nop(self, ins, ops):
        pass

    def op_ld(self, ins, ops):
        d, s = ops
        wide16 = (d.kind == 'r' and d.text in ('bc', 'de', 'hl', 'sp', 'ix',
                                               'iy')) or \
            (s.kind == 'r' and s.text in ('bc', 'de', 'hl', 'sp', 'ix',
                                          'iy') and d.kind == 'mem')
        if wide16:
            if d.kind == 'mem':
                self.wr16(d.value, self.get16(s.text))
            elif s.kind == 'mem':
                self.set16(d.text, self.rd16(s.value))
            elif s.kind == 'nn':
                self.set16(d.text, s.value)
            else:
                self.set16(d.text, self.get16(s.text))
            return
        self.set8(d, self.get8(s))

    def op_push(self, ins, ops):
        self.push(self.get16(ops[0].text))

    def op_pop(self, ins, ops):
        self.set16(ops[0].text, self.pop())

    def op_ex(self, ins, ops):
        a, b = ops[0].text, ops[1].text
        if a == 'de':
            hl, de = self.get16('hl'), self.get16('de')
            self.set16('hl', de)
            self.set16('de', hl)
        elif a == 'af':
            cur = self.get16('af')
            self.set16('af', self.alt[0] << 8 | self.alt[1])
            self.alt[0], self.alt[1] = cur >> 8, cur & 0xFF
        else:
            v = self.rd16(self.sp)
            self.wr16(self.sp, self.get16(b))
            self.set16(b, v)

    def op_exx(self, ins, ops):
        for i, r in enumerate(('bc', 'de', 'hl')):
            cur = self.get16(r)
            self.set16(r, self.alt[2 + 2 * i] << 8 | self.alt[3 + 2 * i])
            self.alt[2 + 2 * i], self.alt[3 + 2 * i] = cur >> 8, cur & 0xFF

    def _alu(self, mn, ops):
        src = ops[-1]
        if len(ops) == 2 and ops[0].text in ('hl', 'ix', 'iy'):
            return self._alu16(mn, ops)
        v = self.get8(src)
        a = self.a
        c = self.f & FC
        if mn == 'add':
            self.a = self.add8(a, v)
        elif mn == 'adc':
            self.a = self.add8(a, v, c)
        elif mn == 'sub':
            self.a = self.add8(a, v, 0, True)
        elif mn == 'sbc':
            self.a = self.add8(a, v, c, True)
        elif mn == 'cp':
            self.add8(a, v, 0, True)
        elif mn == 'and':
            self.a = self.logic(a & v, True)
        elif mn == 'xor':
            self.a = self.logic(a ^ v, False)
        elif mn == 'or':
            self.a = self.logic(a | v, False)

    def _alu16(self, mn, ops):
        d, s = ops[0].text, ops[1].text
        a, b = self.get16(d), self.get16(s)
        if mn == 'add':
            r = a + b
            self.f = (self.f & (FS | FZ | FPV)) | (FC if r > 0xFFFF else 0) | \
                (FH if (a & 0xFFF) + (b & 0xFFF) > 0xFFF else 0)
            self.set16(d, r)
            return
        c = self.f & FC
        if mn == 'adc':
            r = a + b + c
            ov = (~(a ^ b) & (a ^ r) & 0x8000) != 0
            sub = False
        else:
            r = a - b - c
            ov = ((a ^ b) & (a ^ r) & 0x8000) != 0
            sub = True
        cy = r < 0 or r > 0xFFFF
        r &= 0xFFFF
        self.f = (FS if r & 0x8000 else 0) | (FZ if r == 0 else 0) | \
            (FPV if ov else 0) | (FN if sub else 0) | (FC if cy else 0)
        self.set16(d, r)

    def op_add(self, ins, ops):
        self._alu('add', ops)

    def op_adc(self, ins, ops):
        self._alu('adc', ops)

    def op_sub(self, ins, ops):
        self._alu('sub', ops)

    def op_sbc(self, ins, ops):
        self._alu('sbc', ops)

    def op_cp(self, ins, ops):
        self._alu('cp', ops)

    def op_and(self, ins, ops):
        self._alu('and', ops)

    def op_xor(self, ins, ops):
        self._alu('xor', ops)

    def op_or(self, ins, ops):
        self._alu('or', ops)

    def op_inc(self, ins, ops):
        o = ops[0]
        if o.kind == 'r' and o.text in ('bc', 'de', 'hl', 'sp', 'ix', 'iy'):
            self.set16(o.text, self.get16(o.text) + 1)
        else:
            self.set8(o, self.incdec(self.get8(o), 1))

    def op_dec(self, ins, ops):
        o = ops[0]
        if o.kind == 'r' and o.text in ('bc', 'de', 'hl', 'sp', 'ix', 'iy'):
            self.set16(o.text, self.get16(o.text) - 1)
        else:
            self.set8(o, self.incdec(self.get8(o), -1))

    def op_neg(self, ins, ops):
        self.a = self.add8(0, self.a, 0, True)

    def op_cpl(self, ins, ops):
        self.a ^= 0xFF
        self.f |= FH | FN

    def op_scf(self, ins, ops):
        self.f = (self.f & (FS | FZ | FPV)) | FC

    def op_ccf(self, ins, ops):
        c = self.f & FC
        self.f = (self.f & (FS | FZ | FPV)) | (FH if c else 0) | (c ^ FC)

    def op_rlca(self, ins, ops):
        c = self.a >> 7
        self.a = (self.a << 1 | c) & 0xFF
        self.f = (self.f & (FS | FZ | FPV)) | c

    def op_rrca(self, ins, ops):
        c = self.a & 1
        self.a = (self.a >> 1 | c << 7) & 0xFF
        self.f = (self.f & (FS | FZ | FPV)) | c

    def op_rla(self, ins, ops):
        c = self.a >> 7
        self.a = (self.a << 1 | (self.f & FC)) & 0xFF
        self.f = (self.f & (FS | FZ | FPV)) | c

    def op_rra(self, ins, ops):
        c = self.a & 1
        self.a = (self.a >> 1 | (self.f & FC) << 7) & 0xFF
        self.f = (self.f & (FS | FZ | FPV)) | c

    def _shift(self, mn, ops):
        self.set8(ops[0], self.shift(mn, self.get8(ops[0])))

    def op_rlc(self, ins, ops):
        self._shift('rlc', ops)

    def op_rrc(self, ins, ops):
        self._shift('rrc', ops)

    def op_rl(self, ins, ops):
        self._shift('rl', ops)

    def op_rr(self, ins, ops):
        self._shift('rr', ops)

    def op_sla(self, ins, ops):
        self._shift('sla', ops)

    def op_sra(self, ins, ops):
        self._shift('sra', ops)

    def op_srl(self, ins, ops):
        self._shift('srl', ops)

    def op_bit(self, ins, ops):
        b = int(ops[0].text)
        v = self.get8(ops[1]) & (1 << b)
        self.f = (self.f & FC) | FH | (FZ | FPV if not v else 0) | \
            (FS if b == 7 and v else 0)

    def op_set(self, ins, ops):
        self.set8(ops[1], self.get8(ops[1]) | 1 << int(ops[0].text))

    def op_res(self, ins, ops):
        self.set8(ops[1], self.get8(ops[1]) & ~(1 << int(ops[0].text)))

    def op_jp(self, ins, ops):
        if ops[0].kind == 'r' and ops[0].text.startswith('('):
            self.pc = self.get16(ops[0].text[1:-1])
            return
        if len(ops) == 2 and not self.cond(ops[0].text):
            return
        self.pc = ops[-1].value

    def op_jr(self, ins, ops):
        if len(ops) == 2 and not self.cond(ops[0].text):
            return
        self.pc = ops[-1].value

    def op_djnz(self, ins, ops):
        self.b = (self.b - 1) & 0xFF
        if self.b:
            self.pc = ops[0].value

    def op_call(self, ins, ops):
        if len(ops) == 2 and not self.cond(ops[0].text):
            return
        self.push(self.pc)
        self.pc = ops[-1].value

    def op_rst(self, ins, ops):
        self.push(self.pc)
        self.pc = ins.targets[0]

    def op_ret(self, ins, ops):
        if ops and not self.cond(ops[0].text):
            return
        self.pc = self.pop()

    def op_di(self, ins, ops):
        self.iff = False

    def op_ei(self, ins, ops):
        self.iff = True

    def op_im(self, ins, ops):
        pass

    def op_in(self, ins, ops):
        self.set8(ops[0], 0xFF)

    def op_ldi(self, ins, ops):
        self.wr(self.get16('de'), self.rd(self.get16('hl')))
        self.set16('hl', self.get16('hl') + 1)
        self.set16('de', self.get16('de') + 1)
        bc = (self.get16('bc') - 1) & 0xFFFF
        self.set16('bc', bc)
        self.f = (self.f & (FS | FZ | FC)) | (FPV if bc else 0)

    def op_ldir(self, ins, ops):
        while True:
            self.op_ldi(ins, ops)
            if not self.zero_time:
                self.t += 21 * Z80_CYCLE
            if not self.f & FPV:
                break

    # the machine ------------------------------------------------------------
    def interrupt(self):
        self.push(self.pc)
        self.pc = 0x38
        self.iff = False


def sfx_priority(rom, names_path):
    for line in open(names_path):
        p = line.split()
        if len(p) >= 2 and p[1] == 'SfxPriority':
            return int(p[0][1:], 16)
    raise SystemExit('SfxPriority not in the names file')


PAUSE_WAIT = (0x0141, 0x0144, 0x0145)      # WaitForUnpause.wait


def run(rom, script, seconds, idle_pc=0x00B8, zero_time=False):
    m = Machine(rom)
    m.zero_time = zero_time
    end = int(seconds * MASTER)
    frame_next = FRAME
    frame = 0
    pending = list(script)      # (frame, bytes) in order
    int_pending = False
    int_deadline = 0
    steps = 0
    while m.t < end:
        # the 68k: post the next command at a frame's start, mailbox free
        if pending and pending[0][0] <= frame and m.ram[3] == 0 and \
                getattr(m, 'posted_frame', -1) != frame:
            _, data = pending.pop(0)
            for i, v in enumerate(data):
                m.ram[3 + i] = v & 0xFF
            m.posted_frame = frame
        m.chips.advance(m.t)
        if m.t >= frame_next:
            frame += 1
            frame_next += FRAME
            int_pending = True
            int_deadline = m.t + 171 * Z80_CYCLE * 2
        if int_pending:
            if m.iff:
                m.interrupt()
                int_pending = False
            elif zero_time or m.t > int_deadline:
                int_pending = False
        # a command waiting for the next frame isn't due now (one a frame)
        due = pending and pending[0][0] <= frame and \
            getattr(m, 'posted_frame', -1) != frame
        idle = (m.pc == idle_pc and m.ram[3] == 0 and not int_pending
                and not (m.chips.flags & 3) and not due)
        if zero_time and m.pc in PAUSE_WAIT and m.ram[3] == 0 and not due:
            idle = True
        if idle:
            nxt = [frame_next]
            for n in (m.chips.next_a, m.chips.next_b):
                if n is not None:
                    nxt.append(n)
            m.t = max(m.t, min(nxt))
            continue
        m.step()
        steps += 1
    return m, steps


def write_vgm(path, writes, end_t):
    """VGM 1.51: YM2612 at 7670453 Hz, SN76489 at 3579545 Hz (Sega)."""
    body = bytearray()
    pos = 0                      # in 44100 Hz samples

    def wait_to(t):
        nonlocal pos
        target = t * 44100 // MASTER
        n = target - pos
        while n > 0:
            k = min(n, 0xFFFF)
            body.extend(b'\x61' + struct.pack('<H', k))
            n -= k
        pos = max(pos, target)
    for t, part, reg, v in writes:
        wait_to(t)
        if part == 'P':
            body.extend(bytes([0x50, v]))
        else:
            body.extend(bytes([0x52 + part, reg, v]))
    wait_to(end_t)
    body.append(0x66)
    head = bytearray(0x80)
    head[0:4] = b'Vgm '
    struct.pack_into('<I', head, 0x04, 0x80 + len(body) - 4)
    struct.pack_into('<I', head, 0x08, 0x151)
    struct.pack_into('<I', head, 0x0C, 3579545)
    struct.pack_into('<I', head, 0x18, pos)
    struct.pack_into('<I', head, 0x24, 60)
    struct.pack_into('<H', head, 0x28, 0x0009)
    head[0x2A] = 16
    struct.pack_into('<I', head, 0x2C, 7670453)
    struct.pack_into('<I', head, 0x34, 0x80 - 0x34)
    open(path, 'wb').write(bytes(head) + bytes(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--song', type=int)
    ap.add_argument('--repeat', action='store_true')
    ap.add_argument('--sfx', default='')
    ap.add_argument('--fade', default='')
    ap.add_argument('--stop', default='')
    ap.add_argument('--pause', default='')
    ap.add_argument('--zero-time', action='store_true')
    ap.add_argument('--seconds', type=float, default=10.0)
    ap.add_argument('--vgm')
    ap.add_argument('--log')
    ap.add_argument('--names', default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), 'hz_names.txt'))
    args = ap.parse_args()
    rom = open(args.rom, 'rb').read()
    prio = sfx_priority(rom, args.names)
    script = [(0, [0x0A, 0x00, 0x07, 0x80, 0x00]),     # Snd_SetBank $78000
              (0, [0x07, 0x80, 0x00]),                 # Snd_SetSong $8000
              (0, [0x08, 0xFA, 0x00])]                 # Snd_SetSfxTable
    if args.song is not None:
        script.append((1, [0x09, 0x00, args.song]))    # Snd_PlayMusic
        if args.repeat:
            script.append((1, [0x01, 0xFF]))           # Snd_StartSong -1
    for item in filter(None, args.sfx.split(',')):
        f, n = (int(x, 0) for x in item.split(':'))
        script.append((f, [0x02, n, rom[prio + n]]))
    if args.fade:
        f, n = (int(x, 0) for x in args.fade.split(':'))
        script.append((f, [0x03, n, 0]))
    for item in filter(None, args.stop.split(',')):
        script.append((int(item, 0), [0x04]))          # Snd_StopSfx
    for item in filter(None, args.pause.split(',')):
        script.append((int(item, 0), [0x0C, 0, 0]))    # Snd_Pause
    script.sort(key=lambda s: s[0])
    m, steps = run(rom, script, args.seconds, zero_time=args.zero_time)
    w = m.chips.writes
    print('%d instructions, %d chip writes (%d YM, %d PSG), music bits %02X'
          % (steps, len(w), sum(1 for x in w if x[1] != 'P'),
             sum(1 for x in w if x[1] == 'P'), m.ram[0x0B]))
    if args.log:
        with open(args.log, 'w') as fh:
            for t, part, reg, v in w:
                fh.write('%d %s %02X %02X\n' % (t, part, reg, v))
    if args.vgm:
        write_vgm(args.vgm, w, m.t)


if __name__ == '__main__':
    main()
