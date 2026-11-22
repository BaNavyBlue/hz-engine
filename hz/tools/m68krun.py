#!/usr/bin/env python3
"""A 68000 interpreter for running the game's own code as the check of a
port (as tools/z80run.py is the sound driver's).

    cpu = M68k(rom)                 rom: the 512 KB image (`make rom`)
    cpu.ram[...] = ...              $FF0000-$FFFFFF, a bytearray of 64 KB
    cpu.d[7] = 0
    cpu.call(addr)                  runs a subroutine until its rts

User mode only, as the game's main loop runs: the instructions it uses,
with the condition codes the 68000 sets (X N Z V C), from tools/m68k.py's
decoder. Memory: ROM $000000-$07FFFF (read only), RAM $FF0000-$FFFFFF
(the 24-bit bus: the top byte of an address is ignored, so $FFFF8D94 and
$00FF8D94 are the same byte). Anything else (the VDP, the Z80, I/O) stops
the run with an error, as does an instruction not implemented here, a
divide by zero or an odd word address: the code under test isn't
expected to touch them.

Not modelled: timing, interrupts, supervisor state, the status register's
upper byte (move to sr / ori #..,sr keep only the CCR), trace, address
errors other than the odd access check, the prefetch.
"""

import sys

import m68k
from m68k import (DN, AN, IND, POST, PRE, D16, IDX, ABSW, ABSL, PCD, PCX,
                  IMM)

RAM_BASE = 0xFF0000
MASK = {1: 0xFF, 2: 0xFFFF, 4: 0xFFFFFFFF}
MSB = {1: 0x80, 2: 0x8000, 4: 0x80000000}
SIZE = {'b': 1, 'w': 2, 'l': 4}
SENTINEL = 0x00FFFFFE               # the return address call() pushes


class M68kError(Exception):
    pass


def sx(v, size):
    """Sign-extend v (size bytes) to a Python int."""
    v &= MASK[size]
    return v - (MASK[size] + 1) if v & MSB[size] else v


def parse_imm(s):
    s = s[1:]
    neg = s.startswith('-')
    if neg:
        s = s[1:]
    v = int(s[1:], 16) if s.startswith('$') else int(s)
    return -v if neg else v


def parse_reglist(s):
    """'d0-d2/a0-a1' -> register indices 0-15 (d0-d7, a0-a7)."""
    out = []
    if s == '0':
        return out
    for part in s.split('/'):
        part = part.replace('sp', 'a7')
        if '-' in part:
            a, b = part.split('-')
        else:
            a = b = part
        base = 0 if a[0] == 'd' else 8
        for r in range(int(a[1]), int(b[1]) + 1):
            out.append(base + r)
    return out


def reg_op(s):
    """'d3' -> ('d', 3); 'a5' / 'sp' -> ('a', n); else None."""
    if s == 'sp':
        return ('a', 7)
    if len(s) == 2 and s[0] in 'da' and s[1].isdigit():
        return (s[0], int(s[1]))
    return None


class M68k:
    def __init__(self, rom):
        self.rom = bytes(rom)
        self.rom_end = len(self.rom)     # a changed source may build a bigger one
        self.ram = bytearray(0x10000)
        self.d = [0] * 8
        self.a = [0] * 8
        self.a[7] = 0x00FFFE00           # a stack below the game's RAM top
        self.pc = 0
        self.x = self.n = self.z = self.v = self.c = 0
        self.count = 0                   # instructions run
        self.limit = 50_000_000
        self.cache = {}
        self.trace = None                # a function(cpu, insn) or None

    # -- memory ---------------------------------------------------------
    def _where(self, addr, size, write):
        addr &= 0xFFFFFF
        if size > 1 and addr & 1:
            raise M68kError('odd %s at $%06X (pc $%06X)'
                            % ('write' if write else 'read', addr, self.pc))
        if addr >= RAM_BASE:
            if addr + size > 0x1000000:
                raise M68kError('RAM access past the end at $%06X' % addr)
            return addr - RAM_BASE, True
        if addr + size <= self.rom_end and not write:
            return addr, False
        raise M68kError('%s outside ROM/RAM at $%06X (pc $%06X)'
                        % ('write' if write else 'read', addr, self.pc))

    def read(self, addr, size):
        i, ram = self._where(addr, size, False)
        mem = self.ram if ram else self.rom
        if size == 1:
            return mem[i]
        if size == 2:
            return (mem[i] << 8) | mem[i + 1]
        return int.from_bytes(mem[i:i + 4], 'big')

    def write(self, addr, size, value):
        i, _ = self._where(addr, size, True)
        value &= MASK[size]
        if size == 1:
            self.ram[i] = value
        elif size == 2:
            self.ram[i] = value >> 8
            self.ram[i + 1] = value & 0xFF
        else:
            self.ram[i:i + 4] = value.to_bytes(4, 'big')

    # -- registers ------------------------------------------------------
    def get_reg(self, kind, n, size):
        v = self.d[n] if kind == 'd' else self.a[n]
        return v & MASK[size]

    def set_d(self, n, size, value):
        m = MASK[size]
        self.d[n] = (self.d[n] & ~m & 0xFFFFFFFF) | (value & m)

    @property
    def ccr(self):
        return (self.x << 4) | (self.n << 3) | (self.z << 2) | (self.v << 1) | self.c

    @ccr.setter
    def ccr(self, v):
        self.x = (v >> 4) & 1
        self.n = (v >> 3) & 1
        self.z = (v >> 2) & 1
        self.v = (v >> 1) & 1
        self.c = v & 1

    # -- effective addresses --------------------------------------------
    def loc(self, op, size):
        """An operand as a location: ('d', n), ('a', n), ('m', addr) or
        ('i', value). Post-increment and pre-decrement happen here, once."""
        if isinstance(op, str):
            r = reg_op(op)
            if r:
                return r
            if op.startswith('#'):
                return ('i', parse_imm(op) & 0xFFFFFFFF)
            raise M68kError('operand %r at $%06X' % (op, self.pc))
        k = op.kind
        if k == DN:
            return ('d', op.reg)
        if k == AN:
            return ('a', op.reg)
        if k == IND:
            return ('m', self.a[op.reg])
        if k == POST:
            a = self.a[op.reg]
            step = 2 if (size == 1 and op.reg == 7) else size
            self.a[op.reg] = (a + step) & 0xFFFFFFFF
            return ('m', a)
        if k == PRE:
            step = 2 if (size == 1 and op.reg == 7) else size
            a = (self.a[op.reg] - step) & 0xFFFFFFFF
            self.a[op.reg] = a
            return ('m', a)
        if k == D16:
            return ('m', (self.a[op.reg] + op.disp) & 0xFFFFFFFF)
        if k in (IDX, PCX):
            base = self.a[op.reg] if k == IDX else (op.addr - op.disp)
            xk, xn = op.xreg[0], int(op.xreg[1])
            xv = self.d[xn] if xk == 'd' else self.a[xn]
            xv = sx(xv, 4) if op.xlong else sx(xv, 2)
            return ('m', (base + op.disp + xv) & 0xFFFFFFFF)
        if k in (ABSW, ABSL, PCD):
            return ('m', op.addr & 0xFFFFFFFF)
        if k == IMM:
            return ('i', op.imm & 0xFFFFFFFF)
        raise M68kError('ea kind %r' % k)

    def get(self, loc, size):
        t, v = loc
        if t == 'd':
            return self.d[v] & MASK[size]
        if t == 'a':
            return self.a[v] & MASK[size]
        if t == 'm':
            return self.read(v, size)
        return v & MASK[size]

    def put(self, loc, size, value):
        t, v = loc
        if t == 'd':
            self.set_d(v, size, value)
        elif t == 'a':
            self.a[v] = value & 0xFFFFFFFF
        elif t == 'm':
            self.write(v, size, value)
        else:
            raise M68kError('write to an immediate at $%06X' % self.pc)

    # -- flags ------------------------------------------------------------
    def nz(self, r, size):
        r &= MASK[size]
        self.n = 1 if r & MSB[size] else 0
        self.z = 1 if r == 0 else 0

    def logic(self, r, size):
        self.nz(r, size)
        self.v = self.c = 0

    def add_flags(self, s, d, size):
        m = MASK[size]
        r = (s + d) & m
        self.c = 1 if (s + d) > m else 0
        self.v = 1 if (~(s ^ d) & (r ^ d)) & MSB[size] else 0
        self.x = self.c
        self.nz(r, size)
        return r

    def sub_flags(self, s, d, size, setx=True):
        """d - s."""
        m = MASK[size]
        r = (d - s) & m
        self.c = 1 if s > d else 0
        self.v = 1 if ((s ^ d) & (r ^ d)) & MSB[size] else 0
        if setx:
            self.x = self.c
        self.nz(r, size)
        return r

    def cond(self, cc):
        if cc == 't':
            return True
        if cc == 'f':
            return False
        if cc == 'hi':
            return not self.c and not self.z
        if cc == 'ls':
            return bool(self.c or self.z)
        if cc == 'cc':
            return not self.c
        if cc == 'cs':
            return bool(self.c)
        if cc == 'ne':
            return not self.z
        if cc == 'eq':
            return bool(self.z)
        if cc == 'vc':
            return not self.v
        if cc == 'vs':
            return bool(self.v)
        if cc == 'pl':
            return not self.n
        if cc == 'mi':
            return bool(self.n)
        if cc == 'ge':
            return self.n == self.v
        if cc == 'lt':
            return self.n != self.v
        if cc == 'gt':
            return not self.z and self.n == self.v
        if cc == 'le':
            return bool(self.z) or self.n != self.v
        raise M68kError('condition %r' % cc)

    # -- running ------------------------------------------------------------
    def push(self, size, value):
        self.a[7] = (self.a[7] - size) & 0xFFFFFFFF
        self.write(self.a[7], size, value)

    def pop(self, size):
        v = self.read(self.a[7], size)
        self.a[7] = (self.a[7] + size) & 0xFFFFFFFF
        return v

    def call(self, addr, limit=None):
        """Run the subroutine at addr until it returns."""
        self.push(4, SENTINEL)
        self.pc = addr
        end = self.count + (limit or self.limit)
        while self.pc != SENTINEL:
            self.step()
            if self.count > end:
                raise M68kError('instruction limit reached (pc $%06X)' % self.pc)

    def decode(self, pc):
        insn = self.cache.get(pc)
        if insn is None:
            if pc >= self.rom_end:
                raise M68kError('execution outside ROM at $%06X' % pc)
            insn = m68k.decode(self.rom, pc)
            self.cache[pc] = insn
        return insn

    def step(self):
        insn = self.decode(self.pc)
        if self.trace:
            self.trace(self, insn)
        self.count += 1
        self.pc = insn.addr + insn.size
        mn = insn.mn
        if '.' in mn:
            base, sfx = mn.split('.')
            size = SIZE.get(sfx, 0)
        else:
            base, size = mn, 0
        if insn.flow == 'cond':
            if base.startswith('db'):
                return self.op_dbcc(insn, 'f' if base == 'dbf' else base[2:])
            return self.op_bcc(insn, base[1:])
        if base[0] == 's' and base[1:] in m68k.CC and len(insn.ops) == 1:
            return self.op_scc(insn, base[1:])
        h = getattr(self, 'op_' + base, None)
        if h is None:
            raise M68kError('not implemented: %s at $%06X'
                            % (m68k.text(insn), insn.addr))
        h(insn, size)

    # -- data movement --------------------------------------------------------
    def op_move(self, insn, size):
        src, dst = insn.ops
        if dst in ('ccr', 'sr'):
            v = self.get(self.loc(src, 2), 2)
            self.ccr = v & 0x1F
            return
        if src == 'sr':
            self.put(self.loc(dst, 2), 2, 0x2000 | self.ccr)
            return
        v = self.get(self.loc(src, size), size)
        self.put(self.loc(dst, size), size, v)
        self.logic(v, size)

    def op_movea(self, insn, size):
        src, dst = insn.ops
        v = self.get(self.loc(src, size), size)
        if size == 2:
            v = sx(v, 2) & 0xFFFFFFFF
        self.a[reg_op(dst)[1]] = v

    def op_moveq(self, insn, size):
        v = parse_imm(insn.ops[0]) & 0xFFFFFFFF
        n = reg_op(insn.ops[1])[1]
        self.d[n] = v
        self.logic(v, 4)

    def op_movem(self, insn, size):
        a, b = insn.ops
        if isinstance(a, str) and not a.startswith('#') and (reg_op(a) or '/' in a or '-' in a or a == '0'):
            regs = parse_reglist(a)          # registers to memory
            ea = b
            if ea.kind == PRE:
                addr = self.a[ea.reg]
                for r in reversed(regs):
                    addr = (addr - size) & 0xFFFFFFFF
                    v = self.d[r] if r < 8 else self.a[r - 8]
                    self.write(addr, size, v)
                self.a[ea.reg] = addr
            else:
                addr = self.loc(ea, size)[1]
                for r in regs:
                    v = self.d[r] if r < 8 else self.a[r - 8]
                    self.write(addr, size, v)
                    addr = (addr + size) & 0xFFFFFFFF
            return
        regs = parse_reglist(b)              # memory to registers
        ea = a
        if ea.kind == POST:
            addr = self.a[ea.reg]
        else:
            addr = self.loc(ea, size)[1]
        for r in regs:
            v = self.read(addr, size)
            if size == 2:
                v = sx(v, 2) & 0xFFFFFFFF
            if r < 8:
                self.d[r] = v
            else:
                self.a[r - 8] = v
            addr = (addr + size) & 0xFFFFFFFF
        if ea.kind == POST:
            self.a[ea.reg] = addr

    def op_lea(self, insn, size):
        src, dst = insn.ops
        self.a[reg_op(dst)[1]] = self.loc(src, 4)[1] & 0xFFFFFFFF

    def op_pea(self, insn, size):
        self.push(4, self.loc(insn.ops[0], 4)[1])

    def op_link(self, insn, size):
        n = 7 if insn.ops[0] == 'sp' else int(insn.ops[0][1])
        text = insn.ops[1].lstrip('#')
        neg = text.startswith('-')
        digits = text.lstrip('-')
        disp = int(digits[1:], 16) if digits.startswith('$') else int(digits)
        self.push(4, self.a[n])
        self.a[n] = self.a[7]
        self.a[7] = (self.a[7] + (-disp if neg else disp)) & 0xFFFFFFFF

    def op_unlk(self, insn, size):
        n = 7 if insn.ops[0] == 'sp' else int(insn.ops[0][1])
        self.a[7] = self.a[n]
        self.a[n] = self.pop(4)

    def op_exg(self, insn, size):
        a = reg_op(insn.ops[0])
        b = reg_op(insn.ops[1])
        va = self.get_reg(a[0], a[1], 4)
        vb = self.get_reg(b[0], b[1], 4)
        for (k, n), v in ((a, vb), (b, va)):
            if k == 'd':
                self.d[n] = v
            else:
                self.a[n] = v

    def op_swap(self, insn, size):
        n = reg_op(insn.ops[0])[1]
        v = self.d[n]
        v = ((v << 16) | (v >> 16)) & 0xFFFFFFFF
        self.d[n] = v
        self.logic(v, 4)

    def op_ext(self, insn, size):
        n = reg_op(insn.ops[0])[1]
        if size == 2:
            v = sx(self.d[n], 1) & 0xFFFF
        else:
            v = sx(self.d[n], 2) & 0xFFFFFFFF
        self.set_d(n, size, v)
        self.logic(v, size)

    def op_clr(self, insn, size):
        loc = self.loc(insn.ops[0], size)
        self.put(loc, size, 0)
        self.n = self.v = self.c = 0
        self.z = 1

    def op_tst(self, insn, size):
        self.logic(self.get(self.loc(insn.ops[0], size), size), size)

    # -- arithmetic ----------------------------------------------------------
    def _addsub(self, insn, size, sub):
        src, dst = insn.ops
        s = self.get(self.loc(src, size), size)
        dloc = self.loc(dst, size)
        if dloc[0] == 'a':                   # addq/subq to An: whole long
            d = self.a[dloc[1]]
            s = sx(s, size) & 0xFFFFFFFF
            self.a[dloc[1]] = (d - s if sub else d + s) & 0xFFFFFFFF
            return
        d = self.get(dloc, size)
        r = self.sub_flags(s, d, size) if sub else self.add_flags(s, d, size)
        self.put(dloc, size, r)

    def op_add(self, insn, size):
        self._addsub(insn, size, False)

    op_addi = op_add
    op_addq = op_add

    def op_sub(self, insn, size):
        self._addsub(insn, size, True)

    op_subi = op_sub
    op_subq = op_sub

    def op_adda(self, insn, size):
        src, dst = insn.ops
        s = self.get(self.loc(src, size), size)
        if size == 2:
            s = sx(s, 2)
        n = reg_op(dst)[1]
        self.a[n] = (self.a[n] + s) & 0xFFFFFFFF

    def op_suba(self, insn, size):
        src, dst = insn.ops
        s = self.get(self.loc(src, size), size)
        if size == 2:
            s = sx(s, 2)
        n = reg_op(dst)[1]
        self.a[n] = (self.a[n] - s) & 0xFFFFFFFF

    def op_cmp(self, insn, size):
        src, dst = insn.ops
        s = self.get(self.loc(src, size), size)
        d = self.get(self.loc(dst, size), size)
        self.sub_flags(s, d, size, setx=False)

    op_cmpi = op_cmp

    def op_cmpa(self, insn, size):
        src, dst = insn.ops
        s = self.get(self.loc(src, size), size)
        if size == 2:
            s = sx(s, 2) & 0xFFFFFFFF
        d = self.a[reg_op(dst)[1]]
        self.sub_flags(s, d, 4, setx=False)

    def op_neg(self, insn, size):
        loc = self.loc(insn.ops[0], size)
        d = self.get(loc, size)
        r = self.sub_flags(d, 0, size)
        self.put(loc, size, r)

    def op_not(self, insn, size):
        loc = self.loc(insn.ops[0], size)
        r = ~self.get(loc, size) & MASK[size]
        self.put(loc, size, r)
        self.logic(r, size)

    def _logic_op(self, insn, size, fn):
        src, dst = insn.ops
        s = self.get(self.loc(src, 1 if dst == 'ccr' else size), 1 if dst == 'ccr' else size)
        if dst in ('ccr', 'sr'):
            self.ccr = fn(self.ccr, s) & 0x1F
            return
        dloc = self.loc(dst, size)
        r = fn(self.get(dloc, size), s) & MASK[size]
        self.put(dloc, size, r)
        self.logic(r, size)

    def op_and(self, insn, size):
        self._logic_op(insn, size, lambda a, b: a & b)

    op_andi = op_and

    def op_or(self, insn, size):
        self._logic_op(insn, size, lambda a, b: a | b)

    op_ori = op_or

    def op_eor(self, insn, size):
        self._logic_op(insn, size, lambda a, b: a ^ b)

    op_eori = op_eor

    def op_ori_ccr(self, insn, size):
        pass

    def op_mulu(self, insn, size):
        src, dst = insn.ops
        s = self.get(self.loc(src, 2), 2)
        n = reg_op(dst)[1]
        r = (s * (self.d[n] & 0xFFFF)) & 0xFFFFFFFF
        self.d[n] = r
        self.logic(r, 4)

    def op_muls(self, insn, size):
        src, dst = insn.ops
        s = sx(self.get(self.loc(src, 2), 2), 2)
        n = reg_op(dst)[1]
        r = (s * sx(self.d[n], 2)) & 0xFFFFFFFF
        self.d[n] = r
        self.logic(r, 4)

    def op_divs(self, insn, size):
        src, dst = insn.ops
        s = sx(self.get(self.loc(src, 2), 2), 2)
        if s == 0:
            raise M68kError('divide by zero at $%06X' % insn.addr)
        n = reg_op(dst)[1]
        d = sx(self.d[n], 4)
        q = abs(d) // abs(s)
        if (d < 0) != (s < 0):
            q = -q
        r = d - q * s
        self.c = 0
        if q < -0x8000 or q > 0x7FFF:
            self.v = 1                       # overflow: the register kept
            return
        self.d[n] = ((r & 0xFFFF) << 16) | (q & 0xFFFF)
        self.v = 0
        self.nz(q, 2)

    def op_divu(self, insn, size):
        src, dst = insn.ops
        s = self.get(self.loc(src, 2), 2)
        if s == 0:
            raise M68kError('divide by zero at $%06X' % insn.addr)
        n = reg_op(dst)[1]
        d = self.d[n]
        q, r = divmod(d, s)
        self.c = 0
        if q > 0xFFFF:
            self.v = 1
            return
        self.d[n] = (r << 16) | q
        self.v = 0
        self.nz(q, 2)

    # -- shifts and rotates -----------------------------------------------------
    def _shift(self, insn, size, kind, left):
        if len(insn.ops) == 1:               # memory, by one, word
            loc = self.loc(insn.ops[0], 2)
            size = 2
            count = 1
        else:
            cnt, dst = insn.ops
            r = reg_op(cnt)
            count = (self.d[r[1]] & 63) if r else parse_imm(cnt)
            loc = self.loc(dst, size)
        v = self.get(loc, size)
        bits = size * 8
        m = MASK[size]
        msb = MSB[size]
        c = 0
        vflag = 0
        if kind in ('as', 'ls'):
            for _ in range(count):
                if left:
                    c = 1 if v & msb else 0
                    nv = (v << 1) & m
                    if kind == 'as' and (nv & msb) != (v & msb):
                        vflag = 1
                    v = nv
                else:
                    c = v & 1
                    if kind == 'as':
                        v = (v >> 1) | (v & msb)
                    else:
                        v >>= 1
            if count:
                self.x = c
            self.c = c if count else 0
            self.v = vflag
        elif kind == 'ro':
            for _ in range(count):
                if left:
                    c = 1 if v & msb else 0
                    v = ((v << 1) | c) & m
                else:
                    c = v & 1
                    v = (v >> 1) | (c << (bits - 1))
            self.c = c if count else 0
            self.v = 0
        else:                                # rox
            x = self.x
            for _ in range(count):
                if left:
                    c = 1 if v & msb else 0
                    v = ((v << 1) | x) & m
                else:
                    c = v & 1
                    v = (v >> 1) | (x << (bits - 1))
                x = c
            self.x = x
            self.c = x
            self.v = 0
        self.nz(v, size)
        self.put(loc, size, v)

    def op_asl(self, insn, size):
        self._shift(insn, size, 'as', True)

    def op_asr(self, insn, size):
        self._shift(insn, size, 'as', False)

    def op_lsl(self, insn, size):
        self._shift(insn, size, 'ls', True)

    def op_lsr(self, insn, size):
        self._shift(insn, size, 'ls', False)

    def op_rol(self, insn, size):
        self._shift(insn, size, 'ro', True)

    def op_ror(self, insn, size):
        self._shift(insn, size, 'ro', False)

    def op_roxl(self, insn, size):
        self._shift(insn, size, 'rox', True)

    def op_roxr(self, insn, size):
        self._shift(insn, size, 'rox', False)

    # -- bits ----------------------------------------------------------------------
    def _bit(self, insn, size, action):
        bit_op, dst = insn.ops
        r = reg_op(bit_op)
        bit = self.d[r[1]] if r else parse_imm(bit_op)
        bit &= 31 if size == 4 else 7
        loc = self.loc(dst, size)
        v = self.get(loc, size)
        self.z = 0 if v & (1 << bit) else 1
        if action == 'set':
            self.put(loc, size, v | (1 << bit))
        elif action == 'clr':
            self.put(loc, size, v & ~(1 << bit))
        elif action == 'chg':
            self.put(loc, size, v ^ (1 << bit))

    def op_btst(self, insn, size):
        self._bit(insn, size, 'tst')

    def op_bset(self, insn, size):
        self._bit(insn, size, 'set')

    def op_bclr(self, insn, size):
        self._bit(insn, size, 'clr')

    def op_bchg(self, insn, size):
        self._bit(insn, size, 'chg')

    # -- flow ----------------------------------------------------------------------
    def op_bcc(self, insn, cc):
        if self.cond(cc):
            self.pc = insn.ops[0].addr

    def op_bra(self, insn, size):
        self.pc = insn.ops[0].addr

    def op_bsr(self, insn, size):
        self.push(4, self.pc)
        self.pc = insn.ops[0].addr

    def op_dbcc(self, insn, cc):
        if self.cond(cc):
            return
        n = reg_op(insn.ops[0])[1]
        w = (self.d[n] - 1) & 0xFFFF
        self.set_d(n, 2, w)
        if w != 0xFFFF:
            self.pc = insn.ops[1].addr

    def op_scc(self, insn, cc):
        self.put(self.loc(insn.ops[0], 1), 1, 0xFF if self.cond(cc) else 0)

    def op_jmp(self, insn, size):
        self.pc = self.loc(insn.ops[0], 4)[1] & 0xFFFFFF

    def op_jsr(self, insn, size):
        target = self.loc(insn.ops[0], 4)[1] & 0xFFFFFF
        self.push(4, self.pc)
        self.pc = target

    def op_rts(self, insn, size):
        self.pc = self.pop(4) & 0xFFFFFF

    def op_nop(self, insn, size):
        pass


def load_symbols(path):
    """A `make rom` symbol file (NAME $ADDR per line) -> dict."""
    sym = {}
    for line in open(path):
        p = line.split()
        if len(p) == 2:
            sym[p[0]] = int(p[1].lstrip('$'), 16)
    return sym


if __name__ == '__main__':
    print(__doc__)
    sys.exit(0)
