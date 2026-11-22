"""A Z80 instruction decoder (documented instructions only).

decode(mem, addr) returns an Insn or raises DecodeError. Undocumented forms
(IXH/IXL, SLL, the DDCB/FDCB copies into a register, ED holes, DD/FD before
an instruction they don't change) raise DecodeError: the traversal treats
them as data, as the Herzog Zwei driver shouldn't contain any.

Operands are kept structured (Op) so the emitter can put labels in; text
comes from Insn.text(label) in AS's Zilog syntax with Intel hex (0ABh),
AS's default integer notation after `cpu z80`.

Opcode fields follow the usual x/y/z/p/q split of the opcode byte
(x = bits 7-6, y = 5-3, z = 2-0, p = y >> 1, q = y & 1).
"""


class DecodeError(Exception):
    pass


def s8(v):
    return v - 0x100 if v & 0x80 else v


def hexnum(v, width=2):
    """Intel hex as AS reads it after `cpu z80`: a leading digit, h suffix."""
    s = '%0*X' % (width, v)
    if s[0] in 'ABCDEF':
        s = '0' + s
    return s + 'h'


R = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
RP = ['bc', 'de', 'hl', 'sp']
RP2 = ['bc', 'de', 'hl', 'af']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'p', 'm']
ALU = ['add', 'adc', 'sub', 'sbc', 'and', 'xor', 'or', 'cp']
ALU_A = {'add', 'adc', 'sbc'}          # written with "a," in Zilog syntax
ROT = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', None, 'srl']   # 6 = SLL
BLOCK = [['ldi', 'cpi', 'ini', 'outi'], ['ldd', 'cpd', 'ind', 'outd'],
         ['ldir', 'cpir', 'inir', 'otir'], ['lddr', 'cpdr', 'indr', 'otdr']]


class Op:
    """kind: 'r' text, 'n' imm8, 'nn' imm16, 'mem' (nn), 'port' (n),
    'idx' (ix+d), 'jump' absolute target, 'rel' relative target, 'num'
    plain decimal-ish number (bit, im, rst)."""
    __slots__ = ('kind', 'text', 'value', 'reg')

    def __init__(self, kind, text=None, value=None, reg=None):
        self.kind = kind
        self.text = text
        self.value = value
        self.reg = reg

    def fmt(self, label=None):
        k = self.kind
        if k == 'r':
            return self.text
        if k == 'n':
            return hexnum(self.value, 2)
        if k == 'num':
            return self.text
        if k in ('nn', 'jump', 'rel'):
            name = label(self.value, k) if label else None
            return name if name else hexnum(self.value, 4)
        if k == 'mem':
            name = label(self.value, k) if label else None
            return '(%s)' % (name if name else hexnum(self.value, 4))
        if k == 'port':
            return '(%s)' % hexnum(self.value, 2)
        if k == 'idx':
            d = self.value
            if d < 0:
                return '(%s-%s)' % (self.reg, hexnum(-d, 2))
            return '(%s+%s)' % (self.reg, hexnum(d, 2))
        raise ValueError(k)


class Insn:
    __slots__ = ('addr', 'size', 'mn', 'ops', 'flow', 'targets', 'raw',
                 'reads', 'writes')

    def __init__(self, addr):
        self.addr = addr
        self.size = 0
        self.mn = ''
        self.ops = []
        self.flow = 'next'     # next jump cjump call ret cret ijump
        self.targets = []
        self.raw = b''
        self.reads = []        # (nn) addresses read
        self.writes = []       # (nn) addresses written

    def text(self, label=None):
        if not self.ops:
            return self.mn
        return self.mn + '\t' + ','.join(o.fmt(label) for o in self.ops)


def reg(t):
    return Op('r', t)


class _Reader:
    def __init__(self, mem, addr):
        self.mem = mem
        self.start = addr
        self.pos = addr

    def u8(self):
        if self.pos >= len(self.mem):
            raise DecodeError('past the end at %04X' % self.start)
        v = self.mem[self.pos]
        self.pos += 1
        return v

    def u16(self):
        lo = self.u8()
        return lo | self.u8() << 8


def decode(mem, addr):
    rd = _Reader(mem, addr)
    ins = Insn(addr)
    op = rd.u8()
    if op in (0xDD, 0xFD):
        _decode_index(rd, ins, 'ix' if op == 0xDD else 'iy')
    elif op == 0xCB:
        _decode_cb(rd, ins, None, 0)
    elif op == 0xED:
        _decode_ed(rd, ins)
    else:
        _decode_main(rd, ins, op)
    ins.size = rd.pos - addr
    ins.raw = bytes(mem[addr:rd.pos])
    return ins


def _set(ins, mn, *ops):
    ins.mn = mn
    ins.ops = list(ops)


def _rop(z, rd, ix):
    """Operand r[z]; with an index register (HL) becomes (ix+d)."""
    if z == 6 and ix:
        return Op('idx', value=s8(rd.u8()), reg=ix)
    if ix and z in (4, 5):
        raise DecodeError('undocumented %s half' % ix)
    return reg(R[z])


def _decode_main(rd, ins, op, ix=None):
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    HL = ix or 'hl'

    def rp(i):
        return HL if i == 2 else RP[i]

    def rp2(i):
        return HL if i == 2 else RP2[i]

    if x == 0:
        if z == 0:
            if ix:
                raise DecodeError('prefix on a non-HL op')
            if y == 0:
                _set(ins, 'nop')
            elif y == 1:
                _set(ins, 'ex', reg('af'), reg("af'"))
            else:
                d = s8(rd.u8())
                t = (rd.pos + d) & 0xFFFF
                if y == 2:
                    _set(ins, 'djnz', Op('rel', value=t))
                    ins.flow = 'cjump'
                elif y == 3:
                    _set(ins, 'jr', Op('rel', value=t))
                    ins.flow = 'jump'
                else:
                    _set(ins, 'jr', reg(CC[y - 4]), Op('rel', value=t))
                    ins.flow = 'cjump'
                ins.targets = [t]
        elif z == 1:
            if q == 0:
                if ix and p != 2:
                    raise DecodeError('prefix on a non-HL op')
                _set(ins, 'ld', reg(rp(p)), Op('nn', value=rd.u16()))
            else:
                _set(ins, 'add', reg(HL), reg(rp(p)))
        elif z == 2:
            if ix and p != 2:
                raise DecodeError('prefix on a non-HL op')
            if p == 0:
                if q == 0:
                    _set(ins, 'ld', reg('(bc)'), reg('a'))
                else:
                    _set(ins, 'ld', reg('a'), reg('(bc)'))
            elif p == 1:
                if q == 0:
                    _set(ins, 'ld', reg('(de)'), reg('a'))
                else:
                    _set(ins, 'ld', reg('a'), reg('(de)'))
            elif p == 2:
                nn = rd.u16()
                if q == 0:
                    _set(ins, 'ld', Op('mem', value=nn), reg(HL))
                    ins.writes = [nn]
                else:
                    _set(ins, 'ld', reg(HL), Op('mem', value=nn))
                    ins.reads = [nn]
            else:
                nn = rd.u16()
                if q == 0:
                    _set(ins, 'ld', Op('mem', value=nn), reg('a'))
                    ins.writes = [nn]
                else:
                    _set(ins, 'ld', reg('a'), Op('mem', value=nn))
                    ins.reads = [nn]
        elif z == 3:
            if ix and p != 2:
                raise DecodeError('prefix on a non-HL op')
            _set(ins, 'inc' if q == 0 else 'dec', reg(rp(p)))
        elif z in (4, 5):
            if ix and y != 6:
                raise DecodeError('prefix on a non-HL op')
            _set(ins, 'inc' if z == 4 else 'dec', _rop(y, rd, ix))
        elif z == 6:
            if ix and y != 6:
                raise DecodeError('prefix on a non-HL op')
            o = _rop(y, rd, ix)
            _set(ins, 'ld', o, Op('n', value=rd.u8()))
        else:
            if ix:
                raise DecodeError('prefix on a non-HL op')
            _set(ins, ['rlca', 'rrca', 'rla', 'rra', 'daa', 'cpl', 'scf',
                       'ccf'][y])
    elif x == 1:
        if z == 6 and y == 6:
            if ix:
                raise DecodeError('prefix on halt')
            _set(ins, 'halt')
        else:
            if ix and y != 6 and z != 6:
                raise DecodeError('prefix on a non-HL op')
            if ix and y == 6:
                _set(ins, 'ld', _rop(6, rd, ix), reg(R[z]))
            elif ix and z == 6:
                dst = reg(R[y])
                _set(ins, 'ld', dst, _rop(6, rd, ix))
            else:
                _set(ins, 'ld', reg(R[y]), reg(R[z]))
    elif x == 2:
        if ix and z != 6:
            raise DecodeError('prefix on a non-HL op')
        o = _rop(z, rd, ix)
        if ALU[y] in ALU_A:
            _set(ins, ALU[y], reg('a'), o)
        else:
            _set(ins, ALU[y], o)
    else:
        if z == 0:
            if ix:
                raise DecodeError('prefix on a non-HL op')
            _set(ins, 'ret', reg(CC[y]))
            ins.flow = 'cret'
        elif z == 1:
            if q == 0:
                if ix and p != 2:
                    raise DecodeError('prefix on a non-HL op')
                _set(ins, 'pop', reg(rp2(p)))
            else:
                if ix and p not in (2, 3):
                    raise DecodeError('prefix on a non-HL op')
                if p == 0:
                    _set(ins, 'ret')
                    ins.flow = 'ret'
                elif p == 1:
                    _set(ins, 'exx')
                elif p == 2:
                    _set(ins, 'jp', reg('(%s)' % HL))
                    ins.flow = 'ijump'
                else:
                    _set(ins, 'ld', reg('sp'), reg(HL))
        elif z == 2:
            if ix:
                raise DecodeError('prefix on a non-HL op')
            t = rd.u16()
            _set(ins, 'jp', reg(CC[y]), Op('jump', value=t))
            ins.flow = 'cjump'
            ins.targets = [t]
        elif z == 3:
            if ix and y != 4:
                raise DecodeError('prefix on a non-HL op')
            if y == 0:
                t = rd.u16()
                _set(ins, 'jp', Op('jump', value=t))
                ins.flow = 'jump'
                ins.targets = [t]
            elif y == 2:
                _set(ins, 'out', Op('port', value=rd.u8()), reg('a'))
            elif y == 3:
                _set(ins, 'in', reg('a'), Op('port', value=rd.u8()))
            elif y == 4:
                _set(ins, 'ex', reg('(sp)'), reg(HL))
            elif y == 5:
                _set(ins, 'ex', reg('de'), reg('hl'))
            elif y == 6:
                _set(ins, 'di')
            elif y == 7:
                _set(ins, 'ei')
            else:
                raise DecodeError('CB reached in main table')
        elif z == 4:
            if ix:
                raise DecodeError('prefix on a non-HL op')
            t = rd.u16()
            _set(ins, 'call', reg(CC[y]), Op('jump', value=t))
            ins.flow = 'call'
            ins.targets = [t]
        elif z == 5:
            if q == 0:
                if ix and p != 2:
                    raise DecodeError('prefix on a non-HL op')
                _set(ins, 'push', reg(rp2(p)))
            else:
                if ix:
                    raise DecodeError('double prefix')
                if p == 0:
                    t = rd.u16()
                    _set(ins, 'call', Op('jump', value=t))
                    ins.flow = 'call'
                    ins.targets = [t]
                else:
                    raise DecodeError('prefix byte reached in main table')
        elif z == 6:
            if ix:
                raise DecodeError('prefix on a non-HL op')
            o = Op('n', value=rd.u8())
            if ALU[y] in ALU_A:
                _set(ins, ALU[y], reg('a'), o)
            else:
                _set(ins, ALU[y], o)
        else:
            if ix:
                raise DecodeError('prefix on a non-HL op')
            _set(ins, 'rst', Op('num', text=hexnum(y * 8, 2)))
            ins.flow = 'call'
            ins.targets = [y * 8]


def _decode_cb(rd, ins, ix, d):
    if ix:
        d = s8(rd.u8())
    op = rd.u8()
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    if ix:
        if z != 6:
            raise DecodeError('undocumented DDCB copy')
        o = Op('idx', value=d, reg=ix)
    else:
        o = reg(R[z])
    if x == 0:
        if ROT[y] is None:
            raise DecodeError('SLL')
        _set(ins, ROT[y], o)
    else:
        _set(ins, ['', 'bit', 'res', 'set'][x], Op('num', text=str(y)), o)


def _decode_ed(rd, ins):
    op = rd.u8()
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    if x == 1:
        if z == 0:
            if y == 6:
                raise DecodeError('in (c) without a register')
            _set(ins, 'in', reg(R[y]), reg('(c)'))
        elif z == 1:
            if y == 6:
                raise DecodeError('out (c),0')
            _set(ins, 'out', reg('(c)'), reg(R[y]))
        elif z == 2:
            _set(ins, 'sbc' if q == 0 else 'adc', reg('hl'), reg(RP[p]))
        elif z == 3:
            nn = rd.u16()
            if q == 0:
                _set(ins, 'ld', Op('mem', value=nn), reg(RP[p]))
                ins.writes = [nn]
            else:
                _set(ins, 'ld', reg(RP[p]), Op('mem', value=nn))
                ins.reads = [nn]
            if p == 2:
                raise DecodeError('ED form of ld hl,(nn)')
        elif z == 4:
            if y != 0:
                raise DecodeError('NEG mirror')
            _set(ins, 'neg')
        elif z == 5:
            if y == 0:
                _set(ins, 'retn')
            elif y == 1:
                _set(ins, 'reti')
            else:
                raise DecodeError('RETN mirror')
            ins.flow = 'ret'
        elif z == 6:
            modes = {0: '0', 2: '1', 3: '2'}
            if y not in modes:
                raise DecodeError('IM mirror')
            _set(ins, 'im', Op('num', text=modes[y]))
        else:
            names = ['ld i,a', 'ld r,a', 'ld a,i', 'ld a,r', 'rrd', 'rld']
            if y > 5:
                raise DecodeError('ED hole')
            n = names[y].split(' ')
            if len(n) == 1:
                _set(ins, n[0])
            else:
                a, b = n[1].split(',')
                _set(ins, n[0], reg(a), reg(b))
    elif x == 2 and z <= 3 and y >= 4:
        _set(ins, BLOCK[y - 4][z])
    else:
        raise DecodeError('ED hole')


def _decode_index(rd, ins, ix):
    op = rd.u8()
    if op == 0xCB:
        _decode_cb(rd, ins, ix, 0)
    elif op in (0xDD, 0xFD, 0xED):
        raise DecodeError('prefix after prefix')
    else:
        _decode_main(rd, ins, op, ix)
