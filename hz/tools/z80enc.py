"""Encode one Z80 instruction as AS writes it, for the syntax z80dis.py
emits (Zilog mnemonics, Intel hex numbers, `name`, `name+n`, `.local`)
and hz.asm's driver adds (expressions: `1<<NAME`, `0FFh-(1<<NAME)`).

Written from Zilog's opcode tables, not from z80.py's decoder, so the
round trip (decode -> text -> encode) checks the decoder's tables too.
encode(text, sym, pc) -> bytes; sym(name) -> value or raises KeyError.
"""
import re

R8 = {'b': 0, 'c': 1, 'd': 2, 'e': 3, 'h': 4, 'l': 5, '(hl)': 6, 'a': 7}
RP = {'bc': 0, 'de': 1, 'hl': 2, 'sp': 3}
RP_PUSH = {'bc': 0, 'de': 1, 'hl': 2, 'af': 3}
CC = {'nz': 0, 'z': 1, 'nc': 2, 'c': 3, 'po': 4, 'pe': 5, 'p': 6, 'm': 7}
ALU = {'add': 0, 'adc': 1, 'sub': 2, 'sbc': 3, 'and': 4, 'xor': 5,
       'or': 6, 'cp': 7}
SHIFT = {'rlc': 0, 'rrc': 1, 'rl': 2, 'rr': 3, 'sla': 4, 'sra': 5,
         'srl': 7}
PREFIX = {'ix': 0xDD, 'iy': 0xFD}
PLAIN = {
    'nop': [0x00], 'rlca': [0x07], 'rrca': [0x0F], 'rla': [0x17],
    'rra': [0x1F], 'daa': [0x27], 'cpl': [0x2F], 'scf': [0x37],
    'ccf': [0x3F], 'halt': [0x76], 'ret': [0xC9], 'exx': [0xD9],
    'di': [0xF3], 'ei': [0xFB],
    'neg': [0xED, 0x44], 'retn': [0xED, 0x45], 'reti': [0xED, 0x4D],
    'rrd': [0xED, 0x67], 'rld': [0xED, 0x6F],
    'ldi': [0xED, 0xA0], 'cpi': [0xED, 0xA1], 'ini': [0xED, 0xA2],
    'outi': [0xED, 0xA3], 'ldd': [0xED, 0xA8], 'cpd': [0xED, 0xA9],
    'ind': [0xED, 0xAA], 'outd': [0xED, 0xAB], 'ldir': [0xED, 0xB0],
    'cpir': [0xED, 0xB1], 'inir': [0xED, 0xB2], 'otir': [0xED, 0xB3],
    'lddr': [0xED, 0xB8], 'cpdr': [0xED, 0xB9], 'indr': [0xED, 0xBA],
    'otdr': [0xED, 0xBB],
}


class EncodeError(Exception):
    pass


def number(tok):
    t = tok.strip()
    if re.fullmatch(r'[0-9][0-9A-Fa-f]*[hH]', t):
        return int(t[:-1], 16)
    if re.fullmatch(r'[0-9]+', t):
        return int(t)
    raise ValueError(tok)


_TOKEN = re.compile(r'\s*([0-9][0-9A-Fa-f]*[hH](?!\w)|[0-9]+(?!\w)'
                    r'|\.?[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)?|<<|>>|[-+*&|~()])')

# Operators by kind: one kind to a level of an expression (see value).
_KIND = {'+': '+', '-': '+', '*': '*', '&': '&', '|': '|',
         '<<': '<<', '>>': '>>'}


def value(expr, sym):
    """A number, a name, and those joined by + - * & | << >>, with ( )
    and unary + - ~. As in m68kenc.value, operators of two kinds at one
    level are refused unless parenthesized: AS ranks them otherwise than
    C does (2+1<<2 is 6)."""
    text, toks, pos = expr.strip(), [], 0
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if not m:
            raise EncodeError('bad expression %s' % expr)
        toks.append(m.group(1))
        pos = m.end()
    toks.append(None)
    i = 0

    def take():
        nonlocal i
        i += 1
        return toks[i - 1]

    def unary():
        t = take()
        if t == '+':
            return unary()
        if t == '-':
            return -unary()
        if t == '~':
            return ~unary()
        if t == '(':
            v = binary()
            if take() != ')':
                raise EncodeError('bad expression %s' % expr)
            return v
        if t is None or t in _KIND or t == ')':
            raise EncodeError('bad expression %s' % expr)
        try:
            return number(t)
        except ValueError:
            return sym(t)

    def binary():
        v = unary()
        kind = None
        while toks[i] in _KIND:
            op = take()
            if kind is not None and _KIND[op] != kind:
                raise EncodeError('operators of two kinds in %s: parenthesize'
                                  % expr)
            kind = _KIND[op]
            w = unary()
            v = {'+': v + w, '-': v - w, '*': v * w, '&': v & w,
                 '|': v | w, '<<': v << w, '>>': v >> w}[op]
        return v

    v = binary()
    if toks[i] is not None:
        raise EncodeError('bad expression %s' % expr)
    return v


def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def idx(op):
    """(ix+d) / (iy-d) -> (prefix, d) or None."""
    m = re.fullmatch(r'\((ix|iy)\s*([+-].*)?\)', op)
    if not m:
        return None
    return m.group(1), m.group(2) or '+0'


def mem(op):
    """(expr) that isn't a register form -> expr, else None. As AS reads
    it, the ( at the start must close at the end: (1<<7)|(1<<6) is a
    number, (1<<7) an address."""
    if op.startswith('(') and op.endswith(')'):
        depth = 0
        for ch in op[1:-1]:
            depth += {'(': 1, ')': -1}.get(ch, 0)
            if depth < 0:
                return None
        inner = op[1:-1].strip()
        if inner in ('hl', 'bc', 'de', 'sp', 'c', 'ix', 'iy') or \
                inner.startswith(('ix', 'iy')):
            return None
        return inner
    return None


def le16(v):
    v &= 0xFFFF
    return [v & 0xFF, v >> 8]


def disp(expr, sym):
    d = value(expr, sym)
    if not -128 <= d <= 127:
        raise EncodeError('displacement %d' % d)
    return d & 0xFF


def imm8(expr, sym):
    v = value(expr, sym)
    if not -128 <= v <= 255:
        raise EncodeError('byte %d' % v)
    return v & 0xFF


def encode(text, sym, pc):
    text = text.strip()
    mn, _, rest = text.partition('\t')
    if not rest:
        mn, _, rest = text.partition(' ')
    mn = mn.lower()
    ops = split_ops(rest) if rest.strip() else []
    lo = ops

    if mn in PLAIN and not ops:
        return bytes(PLAIN[mn])
    if mn == 'ld':
        return bytes(_ld(lo, sym))
    if mn in ('push', 'pop'):
        base = 0xC5 if mn == 'push' else 0xC1
        r = lo[0]
        if r in PREFIX:
            return bytes([PREFIX[r], base | 2 << 4])
        return bytes([base | RP_PUSH[r] << 4])
    if mn == 'ex':
        pair = (lo[0], lo[1])
        table = {('de', 'hl'): [0xEB], ('af', "af'"): [0x08],
                 ('(sp)', 'hl'): [0xE3], ('(sp)', 'ix'): [0xDD, 0xE3],
                 ('(sp)', 'iy'): [0xFD, 0xE3]}
        return bytes(table[pair])
    if mn in ALU:
        src = lo[-1]
        if len(lo) == 2 and lo[0] != 'a':
            if mn in ('add', 'adc', 'sbc'):
                return bytes(_alu16(mn, lo))
            raise EncodeError(text)
        code = ALU[mn]
        if src in R8:
            return bytes([0x80 | code << 3 | R8[src]])
        i = idx(src)
        if i:
            return bytes([PREFIX[i[0]], 0x86 | code << 3, disp(i[1], sym)])
        return bytes([0xC6 | code << 3, imm8(ops[-1], sym)])
    if mn in ('inc', 'dec'):
        r = lo[0]
        dec = mn == 'dec'
        if r in R8:
            return bytes([(0x05 if dec else 0x04) | R8[r] << 3])
        if r in RP:
            return bytes([(0x0B if dec else 0x03) | RP[r] << 4])
        if r in PREFIX:
            return bytes([PREFIX[r], 0x2B if dec else 0x23])
        i = idx(r)
        return bytes([PREFIX[i[0]], 0x35 if dec else 0x34, disp(i[1], sym)])
    if mn in ('jp', 'call'):
        if mn == 'jp' and len(lo) == 1 and lo[0] in ('(hl)', '(ix)',
                                                      '(iy)'):
            r = lo[0][1:-1]
            return bytes([0xE9] if r == 'hl' else [PREFIX[r], 0xE9])
        t = value(ops[-1], sym)
        if len(lo) == 2:
            base = 0xC2 if mn == 'jp' else 0xC4
            return bytes([base | CC[lo[0]] << 3] + le16(t))
        return bytes([0xC3 if mn == 'jp' else 0xCD] + le16(t))
    if mn in ('jr', 'djnz'):
        t = value(ops[-1], sym)
        d = t - (pc + 2)
        if not -128 <= d <= 127:
            raise EncodeError('branch out of range')
        if mn == 'djnz':
            op = 0x10
        elif len(lo) == 2:
            if lo[0] not in ('nz', 'z', 'nc', 'c'):
                raise EncodeError(text)
            op = 0x20 | CC[lo[0]] << 3
        else:
            op = 0x18
        return bytes([op, d & 0xFF])
    if mn == 'ret' and len(lo) == 1:
        return bytes([0xC0 | CC[lo[0]] << 3])
    if mn == 'rst':
        v = value(ops[0], sym)
        if v & ~0x38:
            raise EncodeError(text)
        return bytes([0xC7 | v])
    if mn == 'im':
        return bytes([0xED, {'0': 0x46, '1': 0x56, '2': 0x5E}[lo[0]]])
    if mn in SHIFT or mn in ('bit', 'res', 'set'):
        if mn in SHIFT:
            code, target = SHIFT[mn] << 3, lo[0]
        else:
            b = value(ops[0], sym)
            if not 0 <= b <= 7:
                raise EncodeError(text)
            code = {'bit': 0x40, 'res': 0x80, 'set': 0xC0}[mn] | b << 3
            target = lo[1]
        if target in R8:
            return bytes([0xCB, code | R8[target]])
        i = idx(target)
        return bytes([PREFIX[i[0]], 0xCB, disp(i[1], sym), code | 6])
    if mn == 'in':
        if lo[1] == '(c)':
            return bytes([0xED, 0x40 | R8[lo[0]] << 3])
        if lo[0] == 'a':
            return bytes([0xDB, imm8(mem(ops[1]), sym)])
    if mn == 'out':
        if lo[0] == '(c)':
            return bytes([0xED, 0x41 | R8[lo[1]] << 3])
        if lo[1] == 'a':
            return bytes([0xD3, imm8(mem(ops[0]), sym)])
    raise EncodeError('unknown: ' + text)


def _alu16(mn, lo):
    a, b = lo
    if mn == 'add':
        if a == 'hl':
            return [0x09 | RP[b] << 4]
        if a in PREFIX:
            p = 2 if b == a else RP[b]
            if b in PREFIX and b != a:
                raise EncodeError('add %s,%s' % (a, b))
            return [PREFIX[a], 0x09 | p << 4]
    if a == 'hl' and b in RP:
        return [0xED, (0x4A if mn == 'adc' else 0x42) | RP[b] << 4]
    raise EncodeError('%s %s,%s' % (mn, a, b))


def _ld(lo, sym):
    dst, src = lo
    special = {('a', 'i'): [0xED, 0x57], ('a', 'r'): [0xED, 0x5F],
               ('i', 'a'): [0xED, 0x47], ('r', 'a'): [0xED, 0x4F],
               ('a', '(bc)'): [0x0A], ('a', '(de)'): [0x1A],
               ('(bc)', 'a'): [0x02], ('(de)', 'a'): [0x12],
               ('sp', 'hl'): [0xF9], ('sp', 'ix'): [0xDD, 0xF9],
               ('sp', 'iy'): [0xFD, 0xF9]}
    if (dst, src) in special:
        return special[(dst, src)]
    if dst in R8 and src in R8:
        if dst == '(hl)' and src == '(hl)':
            raise EncodeError('ld (hl),(hl)')
        return [0x40 | R8[dst] << 3 | R8[src]]
    di, si = idx(dst), idx(src)
    if di and src in R8 and src != '(hl)':
        return [PREFIX[di[0]], 0x70 | R8[src], disp(di[1], sym)]
    if si and dst in R8 and dst != '(hl)':
        return [PREFIX[si[0]], 0x46 | R8[dst] << 3, disp(si[1], sym)]
    if di:
        return [PREFIX[di[0]], 0x36, disp(di[1], sym), imm8(src, sym)]
    md, ms = mem(dst), mem(src)
    if dst == 'a' and ms is not None:
        return [0x3A] + le16(value(ms, sym))
    if src == 'a' and md is not None:
        return [0x32] + le16(value(md, sym))
    if dst in R8:
        return [0x06 | R8[dst] << 3, imm8(src, sym)]
    if ms is not None and (dst in RP or dst in PREFIX):
        a = le16(value(ms, sym))
        if dst == 'hl':
            return [0x2A] + a
        if dst in PREFIX:
            return [PREFIX[dst], 0x2A] + a
        return [0xED, 0x4B | RP[dst] << 4] + a
    if md is not None and (src in RP or src in PREFIX):
        a = le16(value(md, sym))
        if src == 'hl':
            return [0x22] + a
        if src in PREFIX:
            return [PREFIX[src], 0x22] + a
        return [0xED, 0x43 | RP[src] << 4] + a
    if dst in RP:
        return [0x01 | RP[dst] << 4] + le16(value(src, sym))
    if dst in PREFIX:
        return [PREFIX[dst], 0x21] + le16(value(src, sym))
    raise EncodeError('ld %s,%s' % (dst, src))
