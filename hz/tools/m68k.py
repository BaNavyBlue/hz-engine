"""A 68000 (not 010/020) instruction decoder.

decode(rom, addr) returns an Insn or raises DecodeError. Every addressing
mode is checked against what the 68000 allows for that instruction, because
the traversal leans on invalid decodes to notice it has walked into data.

Operands are kept structured (Ea) so later passes can pattern-match jump
tables and data references; text comes from fmt_ea() with a label lookup.
"""

class DecodeError(Exception):
    pass


def s8(v):
    return v - 0x100 if v & 0x80 else v


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v


CC = ['t', 'f', 'hi', 'ls', 'cc', 'cs', 'ne', 'eq',
      'vc', 'vs', 'pl', 'mi', 'ge', 'lt', 'gt', 'le']
SZ = {0: 'b', 1: 'w', 2: 'l'}

# EA classes, as bit sets over mode keys
DN, AN, IND, POST, PRE, D16, IDX, ABSW, ABSL, PCD, PCX, IMM = range(12)
ALL = set(range(12))
DATA = ALL - {AN}
ALTER = ALL - {PCD, PCX, IMM}
DATA_ALT = DATA & ALTER
MEM_ALT = ALTER - {DN, AN}
CONTROL = {IND, D16, IDX, ABSW, ABSL, PCD, PCX}
CTRL_ALT = CONTROL - {PCD, PCX}


class Ea:
    __slots__ = ('kind', 'reg', 'disp', 'xreg', 'xlong', 'addr', 'imm', 'size')

    def __init__(self, kind, reg=0, disp=0, xreg=None, xlong=False,
                 addr=None, imm=None, size=None):
        self.kind = kind
        self.reg = reg
        self.disp = disp
        self.xreg = xreg      # 'd0'..'a7'
        self.xlong = xlong
        self.addr = addr      # absolute address this EA names, if static
        self.imm = imm
        self.size = size


class Insn:
    __slots__ = ('addr', 'size', 'mn', 'ops', 'flow', 'targets', 'raw')

    def __init__(self, addr):
        self.addr = addr
        self.size = 0
        self.mn = ''
        self.ops = []         # list of Ea or plain strings
        self.flow = 'seq'     # seq, cond, jump, call, ret
        self.targets = []
        self.raw = b''

    @property
    def end(self):
        return self.addr + self.size


def areg(n):
    return 'sp' if n == 7 else 'a%d' % n


def hexs(v):
    if v < 0:
        return '-$%X' % (-v)
    return '$%X' % v


class _D:
    def __init__(self, rom, addr):
        self.rom = rom
        self.addr = addr
        self.pc = addr
        self.op = self.w()

    def w(self):
        if self.pc + 2 > len(self.rom):
            raise DecodeError('past end')
        v = (self.rom[self.pc] << 8) | self.rom[self.pc + 1]
        self.pc += 2
        return v

    def l(self):
        hi = self.w()
        return (hi << 16) | self.w()

    def imm(self, size):
        if size == 'b':
            v = self.w()
            if v & 0xFF00 and (v & 0xFF00) != 0xFF00:
                raise DecodeError('byte imm high bits')
            return v & 0xFF
        if size == 'w':
            return self.w()
        return self.l()

    def brief(self, base_is_pc, reg):
        at = self.pc
        ext = self.w()
        if ext & 0x0700:
            raise DecodeError('full-format extension (020+)')
        xn = ('a' if ext & 0x8000 else 'd') + str((ext >> 12) & 7)
        xlong = bool(ext & 0x0800)
        d = s8(ext & 0xFF)
        if base_is_pc:
            return Ea(PCX, disp=d, xreg=xn, xlong=xlong, addr=at + d)
        return Ea(IDX, reg=reg, disp=d, xreg=xn, xlong=xlong)

    def ea(self, mode, reg, size, allowed):
        if mode == 7:
            if reg > 4:
                raise DecodeError('bad mode 7 reg')
            kind = [ABSW, ABSL, PCD, PCX, IMM][reg]
        else:
            kind = mode
        if kind not in allowed:
            raise DecodeError('ea not allowed')
        if kind == AN and size == 'b':
            raise DecodeError('byte An')
        if kind in (DN, AN, IND, POST, PRE):
            return Ea(kind, reg=reg)
        if kind == D16:
            return Ea(D16, reg=reg, disp=s16(self.w()))
        if kind == IDX:
            return self.brief(False, reg)
        if kind == ABSW:
            v = s16(self.w()) & 0xFFFFFFFF
            return Ea(ABSW, addr=v)
        if kind == ABSL:
            return Ea(ABSL, addr=self.l())
        if kind == PCD:
            at = self.pc
            d = s16(self.w())
            return Ea(PCD, disp=d, addr=at + d)
        if kind == PCX:
            return self.brief(True, 0)
        if size is None:
            raise DecodeError('imm without size')
        return Ea(IMM, imm=self.imm(size), size=size)


def fmt_ea(e, label=None):
    """label(addr) -> name or None."""
    if isinstance(e, str):
        return e
    k = e.kind
    if k == DN:
        return 'd%d' % e.reg
    if k == AN:
        return areg(e.reg)
    if k == IND:
        return '(%s)' % areg(e.reg)
    if k == POST:
        return '(%s)+' % areg(e.reg)
    if k == PRE:
        return '-(%s)' % areg(e.reg)
    if k == D16:
        return '%s(%s)' % (hexs(e.disp), areg(e.reg))
    if k == IDX:
        x = '%s.%s' % (e.xreg.replace('a7', 'sp'), 'l' if e.xlong else 'w')
        d = hexs(e.disp) if e.disp else ''
        return '%s(%s,%s)' % (d, areg(e.reg), x)
    if k == ABSW:
        n = label(e.addr) if label else None
        return '(%s).w' % (n or ('$%X' % e.addr))
    if k == ABSL:
        n = label(e.addr) if label else None
        return '(%s).l' % (n or ('$%X' % e.addr))
    if k == PCD:
        n = label(e.addr) if label else None
        return '%s(pc)' % (n or ('$%X' % e.addr))
    if k == PCX:
        n = label(e.addr) if label else None
        x = '%s.%s' % (e.xreg.replace('a7', 'sp'), 'l' if e.xlong else 'w')
        return '%s(pc,%s)' % (n or ('$%X' % e.addr), x)
    if k == IMM:
        return '#$%X' % e.imm
    raise ValueError(k)


def reglist(mask, predec):
    if predec:
        mask = int('{:016b}'.format(mask)[::-1], 2)
    names = ['d%d' % i for i in range(8)] + ['a%d' % i for i in range(8)]
    out = []
    i = 0
    while i < 16:
        if mask & (1 << i):
            j = i
            while j + 1 < 16 and (mask & (1 << (j + 1))) and (j + 1) // 8 == i // 8:
                j += 1
            out.append(names[i] if i == j else '%s-%s' % (names[i], names[j]))
            i = j + 1
        else:
            i += 1
    return '/'.join(out).replace('a7', 'sp') if out else '0'


def decode(rom, addr):
    if addr & 1:
        raise DecodeError('odd address')
    d = _D(rom, addr)
    op = d.op
    i = Insn(addr)
    top = op >> 12
    mode = (op >> 3) & 7
    reg = op & 7
    r9 = (op >> 9) & 7
    opm = (op >> 6) & 7
    sz = (op >> 6) & 3

    def done(mn, *ops):
        i.mn = mn
        i.ops = list(ops)
        i.size = d.pc - addr
        i.raw = bytes(rom[addr:d.pc])
        return i

    if top == 0:
        if op & 0x0100:
            if mode == 1:
                disp = s16(d.w())
                m = Ea(D16, reg=reg, disp=disp)
                s = 'l' if opm & 1 else 'w'
                if opm & 2:
                    return done('movep.' + s, 'd%d' % r9, m)
                return done('movep.' + s, m, 'd%d' % r9)
            t = sz
            name = ['btst', 'bchg', 'bclr', 'bset'][t]
            allowed = DATA if t == 0 else DATA_ALT
            dst = d.ea(mode, reg, 'b', allowed)
            s = '.l' if dst.kind == DN else '.b'
            return done(name + s, 'd%d' % r9, dst)
        if r9 == 4:
            t = sz
            name = ['btst', 'bchg', 'bclr', 'bset'][t]
            bit = d.w()
            if bit & 0xFF00:
                raise DecodeError('bit number high bits')
            allowed = (DATA - {IMM}) if t == 0 else DATA_ALT
            dst = d.ea(mode, reg, 'b', allowed)
            s = '.l' if dst.kind == DN else '.b'
            return done(name + s, '#%d' % bit, dst)
        names = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}
        if r9 not in names:
            raise DecodeError('group0 r9')
        if op in (0x003C, 0x023C, 0x0A3C):
            v = d.w()
            if v & 0xFF00:
                raise DecodeError('ccr imm')
            return done(names[r9], '#$%X' % v, 'ccr')
        if op in (0x007C, 0x027C, 0x0A7C):
            return done(names[r9], '#$%X' % d.w(), 'sr')
        if sz == 3:
            raise DecodeError('group0 size')
        s = SZ[sz]
        src = Ea(IMM, imm=d.imm(s), size=s)
        dst = d.ea(mode, reg, s, DATA_ALT)
        return done(names[r9] + '.' + s, src, dst)

    if top in (1, 2, 3):
        s = {1: 'b', 3: 'w', 2: 'l'}[top]
        src = d.ea(mode, reg, s, ALL)
        dmode = opm
        if dmode == 1:
            if s == 'b':
                raise DecodeError('movea.b')
            return done('movea.' + s, src, areg(r9))
        dst = d.ea(dmode, r9, s, DATA_ALT)
        return done('move.' + s, src, dst)

    if top == 4:
        if op & 0x0100:
            if opm == 7:
                src = d.ea(mode, reg, None, CONTROL)
                return done('lea', src, areg(r9))
            if opm == 6:
                src = d.ea(mode, reg, 'w', DATA)
                return done('chk.w', src, 'd%d' % r9)
            raise DecodeError('group4 opm')
        sub = (op >> 8) & 0xF
        if sub in (0, 2, 4, 6):
            if sz == 3:
                if sub == 0:
                    return done('move.w', 'sr', d.ea(mode, reg, 'w', DATA_ALT))
                if sub == 4:
                    return done('move.w', d.ea(mode, reg, 'w', DATA), 'ccr')
                if sub == 6:
                    return done('move.w', d.ea(mode, reg, 'w', DATA), 'sr')
                raise DecodeError('move from ccr')
            name = {0: 'negx', 2: 'clr', 4: 'neg', 6: 'not'}[sub]
            s = SZ[sz]
            return done(name + '.' + s, d.ea(mode, reg, s, DATA_ALT))
        if sub == 8:
            if sz == 0:
                return done('nbcd', d.ea(mode, reg, 'b', DATA_ALT))
            if sz == 1:
                if mode == 0:
                    return done('swap', 'd%d' % reg)
                if mode == 1:
                    raise DecodeError('bkpt')
                return done('pea', d.ea(mode, reg, None, CONTROL))
            s = 'w' if sz == 2 else 'l'
            if mode == 0:
                return done('ext.' + s, 'd%d' % reg)
            mask = d.w()
            dst = d.ea(mode, reg, None, CTRL_ALT | {PRE})
            return done('movem.' + s, reglist(mask, dst.kind == PRE), dst)
        if sub == 0xA:
            if op == 0x4AFC:
                i.flow = 'ret'
                return done('illegal')
            if sz == 3:
                return done('tas', d.ea(mode, reg, 'b', DATA_ALT))
            s = SZ[sz]
            return done('tst.' + s, d.ea(mode, reg, s, DATA_ALT))
        if sub == 0xC:
            if sz < 2:
                raise DecodeError('mul/div long')
            s = 'w' if sz == 2 else 'l'
            mask = d.w()
            src = d.ea(mode, reg, None, CONTROL | {POST})
            return done('movem.' + s, src, reglist(mask, False))
        if sub == 0xE:
            if sz == 1:
                lo = op & 0x3F
                if lo < 0x10:
                    return done('trap', '#%d' % lo)
                if lo < 0x18:
                    return done('link', areg(reg), '#%s' % hexs(s16(d.w())))
                if lo < 0x20:
                    return done('unlk', areg(reg))
                if lo < 0x28:
                    return done('move.l', areg(reg), 'usp')
                if lo < 0x30:
                    return done('move.l', 'usp', areg(reg))
                if op == 0x4E70:
                    return done('reset')
                if op == 0x4E71:
                    return done('nop')
                if op == 0x4E72:
                    return done('stop', '#$%X' % d.w())
                if op in (0x4E73, 0x4E75, 0x4E77):
                    i.flow = 'ret'
                    return done({0x4E73: 'rte', 0x4E75: 'rts', 0x4E77: 'rtr'}[op])
                if op == 0x4E76:
                    return done('trapv')
                raise DecodeError('4E7x')
            if sz == 2 or sz == 3:
                tgt = d.ea(mode, reg, None, CONTROL)
                i.flow = 'call' if sz == 2 else 'jump'
                if tgt.kind in (ABSW, ABSL, PCD):
                    i.targets = [tgt.addr]
                return done('jsr' if sz == 2 else 'jmp', tgt)
        raise DecodeError('group4')

    if top == 5:
        if sz == 3:
            cc = (op >> 8) & 0xF
            if mode == 1:
                at = d.pc
                disp = s16(d.w())
                i.flow = 'cond'
                i.targets = [at + disp]
                name = 'dbf' if cc == 1 else 'db' + CC[cc]
                return done(name, 'd%d' % reg, Ea(PCD, disp=disp, addr=at + disp))
            return done('s' + CC[cc], d.ea(mode, reg, 'b', DATA_ALT))
        s = SZ[sz]
        q = r9 or 8
        dst = d.ea(mode, reg, s, ALTER)
        return done(('subq.' if op & 0x0100 else 'addq.') + s, '#%d' % q, dst)

    if top == 6:
        cc = (op >> 8) & 0xF
        disp8 = op & 0xFF
        at = d.pc
        if disp8 == 0:
            disp = s16(d.w())
            sfx = '.w'
        elif disp8 == 0xFF:
            raise DecodeError('bcc.l')
        else:
            disp = s8(disp8)
            sfx = '.s'
        tgt = at + disp
        i.targets = [tgt]
        if cc == 0:
            i.flow = 'jump'
            name = 'bra'
        elif cc == 1:
            i.flow = 'call'
            name = 'bsr'
        else:
            i.flow = 'cond'
            name = 'b' + CC[cc]
        return done(name + sfx, Ea(PCD, disp=disp, addr=tgt))

    if top == 7:
        if op & 0x0100:
            raise DecodeError('moveq bit8')
        return done('moveq', '#%s' % hexs(s8(op & 0xFF)), 'd%d' % r9)

    if top in (8, 9, 0xB, 0xC, 0xD):
        if top == 8 and opm in (3, 7):
            src = d.ea(mode, reg, 'w', DATA)
            return done('divu.w' if opm == 3 else 'divs.w', src, 'd%d' % r9)
        if top == 0xC and opm in (3, 7):
            src = d.ea(mode, reg, 'w', DATA)
            return done('mulu.w' if opm == 3 else 'muls.w', src, 'd%d' % r9)
        if top in (9, 0xB, 0xD) and opm in (3, 7):
            s = 'w' if opm == 3 else 'l'
            name = {9: 'suba', 0xB: 'cmpa', 0xD: 'adda'}[top]
            return done(name + '.' + s, d.ea(mode, reg, s, ALL), areg(r9))
        s = SZ[opm & 3]
        if opm < 3:
            name = {8: 'or', 9: 'sub', 0xB: 'cmp', 0xC: 'and', 0xD: 'add'}[top]
            allowed = DATA if top in (8, 0xC) else ALL
            return done(name + '.' + s, d.ea(mode, reg, s, allowed), 'd%d' % r9)
        # opm 4..6
        if mode in (0, 1):
            if top == 8:
                if opm == 4:
                    if mode == 0:
                        return done('sbcd', 'd%d' % reg, 'd%d' % r9)
                    return done('sbcd', '-(%s)' % areg(reg), '-(%s)' % areg(r9))
                raise DecodeError('pack/unpk')
            if top == 0xC:
                if opm == 4:
                    if mode == 0:
                        return done('abcd', 'd%d' % reg, 'd%d' % r9)
                    return done('abcd', '-(%s)' % areg(reg), '-(%s)' % areg(r9))
                if opm == 5 and mode == 0:
                    return done('exg', 'd%d' % r9, 'd%d' % reg)
                if opm == 5 and mode == 1:
                    return done('exg', areg(r9), areg(reg))
                if opm == 6 and mode == 1:
                    return done('exg', 'd%d' % r9, areg(reg))
                raise DecodeError('exg')
            if top == 0xB:
                if mode == 1:
                    return done('cmpm.' + s, '(%s)+' % areg(reg), '(%s)+' % areg(r9))
                return done('eor.' + s, 'd%d' % r9, Ea(DN, reg=reg))
            name = 'subx.' if top == 9 else 'addx.'
            if mode == 0:
                return done(name + s, 'd%d' % reg, 'd%d' % r9)
            return done(name + s, '-(%s)' % areg(reg), '-(%s)' % areg(r9))
        name = {8: 'or', 9: 'sub', 0xB: 'eor', 0xC: 'and', 0xD: 'add'}[top]
        allowed = DATA_ALT if top == 0xB else MEM_ALT
        return done(name + '.' + s, 'd%d' % r9, d.ea(mode, reg, s, allowed))

    if top == 0xE:
        kinds = ['as', 'ls', 'rox', 'ro']
        lr = 'l' if op & 0x0100 else 'r'
        if sz == 3:
            t = r9
            if t > 3:
                raise DecodeError('bitfield (020+)')
            return done(kinds[t] + lr + '.w', d.ea(mode, reg, 'w', MEM_ALT))
        t = (op >> 3) & 3
        s = SZ[sz]
        if op & 0x20:
            cnt = 'd%d' % r9
        else:
            cnt = '#%d' % (r9 or 8)
        return done(kinds[t] + lr + '.' + s, cnt, 'd%d' % reg)

    raise DecodeError('line A/F')


def text(insn, label=None):
    ops = ','.join(fmt_ea(o, label) for o in insn.ops)
    if ops:
        return '%-8s%s' % (insn.mn, ops)
    return insn.mn
