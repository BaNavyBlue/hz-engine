"""A 68000 encoder for the syntax hzdis.py emits for AS.

Not a general assembler: it reads exactly the operand forms m68k.fmt_ea and
hzdis.asm_text write. Two uses:

  * hzdis.py round-trips every decoded instruction through it. Anything
    that does not come back as the original bytes is emitted as dc.w, so
    the source cannot rely on an encoding choice the assembler might make
    differently.
  * hzasm_check.py assembles the whole emitted source with it and compares
    the result with the ROM, which catches emitter mistakes before AS runs.

It encodes the way AS 1.42 (Bld 313) does, checked against real AS:
`and/or/cmp/add/sub #imm,Dn` stay in the <ea>,Dn form (the I-form only for
eor and memory destinations), `addi/subi #1-8` stay ADDI/SUBI, `0(An)`
becomes (An), and a byte immediate is stored as $00xx. An instruction the
ROM encodes otherwise comes out different and so ends up as dc.w.
"""
import re
from collections import ChainMap

CC =['t', 'f', 'hi', 'ls', 'cc', 'cs', 'ne', 'eq',
      'vc', 'vs', 'pl', 'mi', 'ge', 'lt', 'gt', 'le']
SZ = {'b': 0, 'w': 1, 'l': 2}

DN, AN, IND, POST, PRE, D16, IDX, ABSW, ABSL, PCD, PCX, IMM, EXPR, SPEC = range(14)


class AsmError(Exception):
    pass


def reg(s):
    s = s.strip().lower()
    if s == 'sp':
        return ('a', 7)
    if len(s) == 2 and s[0] in 'da' and s[1] in '01234567':
        return (s[0], int(s[1]))
    return None


_TOKEN = re.compile(r'\s*(\$[0-9A-Fa-f]+|%[01]+|\d+|[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)?|<<|>>|[-+*&|~(),])')

# A local label, .name, not the .w/.l of (x).w or d0.w.
_LOCAL = re.compile(r'(?<![A-Za-z0-9_.)$])\.([A-Za-z_][A-Za-z0-9_]*)')


def expand_locals(text, scope):
    """.name -> scope.name.

    AS composes a temporary symbol that starts with a dot with the last
    non-temporary label before it, so .loop under Foo is Foo.loop.
    """
    def full(m):
        if scope is None:
            raise AsmError('local label %s before any global label' % m.group(0))
        return '%s.%s' % (scope, m.group(1))
    return _LOCAL.sub(full, text)


def _term(tok, sym):
    if tok.startswith('$'):
        return int(tok[1:], 16)
    if tok.startswith('%'):
        return int(tok[1:], 2)
    if tok.isdigit():
        return int(tok)
    if tok in sym:
        return sym[tok]
    if sym.get('__lax__'):
        return 0
    raise AsmError('undefined symbol %s' % tok)


# The source's functions (NAME function PARAMS,EXPR, as AS defines them):
# name -> (parameter names, expression). hzasm_check.py fills it.
FUNCS = {}

# Operators by kind: one kind to a level of an expression (see value).
# No ^: AS reads it as a power, not as C's exclusive or.
_KIND = {'+': '+', '-': '+', '*': '*', '&': '&', '|': '|',
         '<<': '<<', '>>': '>>'}


def _apply(op, a, b):
    if op == '+':
        return a + b
    if op == '-':
        return a - b
    if op == '*':
        return a * b
    if op == '&':
        return a & b
    if op == '|':
        return a | b
    if op == '<<':
        return a << b
    return a >> b


class _Expr:
    def __init__(self, text, sym):
        self.text, self.sym, self.toks, self.i = text, sym, [], 0
        pos = 0
        while pos < len(text):
            m = _TOKEN.match(text, pos)
            if not m:
                raise AsmError('bad expression %s' % text)
            self.toks.append(m.group(1))
            pos = m.end()

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def take(self):
        t = self.peek()
        if t is None:
            raise AsmError('bad expression %s' % self.text)
        self.i += 1
        return t

    def expr(self):
        v = self.unary()
        kind = None
        while self.peek() in _KIND:
            op = self.take()
            if kind is not None and _KIND[op] != kind:
                raise AsmError('operators of two kinds in %s: parenthesize'
                               % self.text)
            kind = _KIND[op]
            v = _apply(op, v, self.unary())
        return v

    def unary(self):
        t = self.take()
        if t == '-':
            return -self.unary()
        if t == '~':
            return ~self.unary()
        if t == '(':
            v = self.expr()
            if self.take() != ')':
                raise AsmError('bad expression %s' % self.text)
            return v
        if t in FUNCS and self.peek() == '(':
            self.take()
            args = [self.expr()]
            while self.peek() == ',':
                self.take()
                args.append(self.expr())
            if self.take() != ')':
                raise AsmError('bad expression %s' % self.text)
            params, body = FUNCS[t]
            if len(args) != len(params):
                raise AsmError('%s takes %d arguments' % (t, len(params)))
            return value(body, ChainMap(dict(zip(params, args)), self.sym))
        if t in _KIND or t in ',)~':
            raise AsmError('bad expression %s' % self.text)
        return _term(t, self.sym)


def value(expr, sym):
    """A number ($hex, %binary or decimal), a symbol, a call of one of the
    source's functions, and those joined by + - * & | << >>, with ( )
    and unary - ~. Operators of two kinds at one level are refused unless
    parenthesized: AS ranks them otherwise than C does, so an expression
    must not lean on either order."""
    e = _Expr(expr.strip(), sym)
    if not e.toks:
        raise AsmError('empty expression')
    v = e.expr()
    if e.peek() is not None:
        raise AsmError('bad expression %s' % expr)
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


def _index(s):
    m = re.fullmatch(r'([da][0-7]|sp)\.([wl])', s.strip().lower())
    if not m:
        raise AsmError('bad index %s' % s)
    r = reg(m.group(1))
    return r, m.group(2) == 'l'


def parse_ea(s, sym):
    t = s.strip()
    tl = t.lower()
    if tl in ('sr', 'ccr', 'usp'):
        return {'k': SPEC, 'v': tl}
    if t.startswith('#'):
        return {'k': IMM, 'v': value(t[1:], sym)}
    r = reg(t)
    if r:
        return {'k': DN if r[0] == 'd' else AN, 'r': r[1]}
    m = re.fullmatch(r'-\(\s*(\w+)\s*\)', t)
    if m and reg(m.group(1)) and reg(m.group(1))[0] == 'a':
        return {'k': PRE, 'r': reg(m.group(1))[1]}
    m = re.fullmatch(r'\(\s*(\w+)\s*\)(\+?)', t)
    if m and reg(m.group(1)) and reg(m.group(1))[0] == 'a':
        return {'k': POST if m.group(2) else IND, 'r': reg(m.group(1))[1]}
    m = re.fullmatch(r'\((.+)\)\.([wlWL])', t)
    if m:
        return {'k': ABSW if m.group(2).lower() == 'w' else ABSL, 'v': value(m.group(1), sym)}
    m = re.fullmatch(r'(.*)\(([^()]*)\)', t)
    if m:
        disp = value(m.group(1), sym) if m.group(1).strip() else 0
        parts = [p.strip() for p in m.group(2).split(',')]
        base = parts[0].lower()
        if base == 'pc':
            if len(parts) == 1:
                return {'k': PCD, 'v': disp}
            x, xl = _index(parts[1])
            return {'k': PCX, 'v': disp, 'x': x, 'xl': xl}
        b = reg(base)
        if not b or b[0] != 'a':
            raise AsmError('bad base %s' % t)
        if len(parts) == 1:
            return {'k': D16, 'r': b[1], 'v': disp}
        x, xl = _index(parts[1])
        return {'k': IDX, 'r': b[1], 'v': disp, 'x': x, 'xl': xl}
    return {'k': EXPR, 'v': value(t, sym)}


def _s16ok(v):
    return -0x8000 <= v <= 0x7FFF


def _brief(e, disp, lax):
    if not -0x80 <= disp <= 0x7F and not lax:
        raise AsmError('index displacement out of range')
    x, xl = e['x'], e['xl']
    return ((1 if x[0] == 'a' else 0) << 15) | (x[1] << 12) | (int(xl) << 11) | (disp & 0xFF)


def ea_bits(e, size, ext_at, lax=False):
    """(mode, reg, [extension words]) for an operand whose extension words
    start at ext_at."""
    k = e['k']
    if k == DN:
        return 0, e['r'], []
    if k == AN:
        return 1, e['r'], []
    if k == IND:
        return 2, e['r'], []
    if k == POST:
        return 3, e['r'], []
    if k == PRE:
        return 4, e['r'], []
    if k == D16:
        if e['v'] == 0:
            return 2, e['r'], []      # what an optimising assembler does
        if not _s16ok(e['v']) and not lax:
            raise AsmError('d16 out of range')
        return 5, e['r'], [e['v'] & 0xFFFF]
    if k == IDX:
        return 6, e['r'], [_brief(e, e['v'], lax)]
    if k == ABSW:
        v = e['v'] & 0xFFFFFFFF
        if not (v <= 0x7FFF or v >= 0xFFFF8000) and not lax:
            raise AsmError('abs.w address $%X not sign-extendable' % v)
        return 7, 0, [v & 0xFFFF]
    if k == ABSL:
        v = e['v'] & 0xFFFFFFFF
        return 7, 1, [v >> 16, v & 0xFFFF]
    if k == PCD:
        d = e['v'] - ext_at
        if not _s16ok(d) and not lax:
            raise AsmError('pc displacement out of range')
        return 7, 2, [d & 0xFFFF]
    if k == PCX:
        return 7, 3, [_brief(e, e['v'] - ext_at, lax)]
    if k == IMM:
        v = e['v']
        if size == 'b':
            return 7, 4, [v & 0xFF]
        if size == 'w':
            return 7, 4, [v & 0xFFFF]
        if size == 'l':
            v &= 0xFFFFFFFF
            return 7, 4, [v >> 16, v & 0xFFFF]
        raise AsmError('immediate needs a size')
    raise AsmError('operand not usable as an effective address')


def reglist_mask(s):
    mask = 0
    for part in s.lower().split('/'):
        if '-' in part:
            a, b = part.split('-')
            ra, rb = reg(a), reg(b)
            if not ra or not rb or ra[0] != rb[0]:
                raise AsmError('bad register range %s' % part)
            base = 0 if ra[0] == 'd' else 8
            for n in range(ra[1], rb[1] + 1):
                mask |= 1 << (base + n)
        else:
            r = reg(part)
            if not r:
                raise AsmError('bad register %s' % part)
            mask |= 1 << ((0 if r[0] == 'd' else 8) + r[1])
    return mask


def _is_reglist(s):
    s = s.strip().lower()
    if '/' in s:
        return True
    if re.fullmatch(r'([da][0-7]|sp)(-([da][0-7]|sp))?', s):
        return True
    return False


def _words(ws):
    return b''.join((w & 0xFFFF).to_bytes(2, 'big') for w in ws)


IMM_OPS = {'ori': 0, 'andi': 1, 'subi': 2, 'addi': 3, 'eori': 5, 'cmpi': 6}
BIT_OPS = {'btst': 0, 'bchg': 1, 'bclr': 2, 'bset': 3}
SHIFTS = {'as': 0, 'ls': 1, 'rox': 2, 'ro': 3}
TO_IMM = {'or': 'ori', 'and': 'andi', 'sub': 'subi', 'add': 'addi',
          'cmp': 'cmpi', 'eor': 'eori'}


def encode(mn, opstr, addr, sym, lax=False):
    """Bytes for one instruction at addr."""
    base, _, sz = mn.lower().partition('.')
    raw = split_ops(opstr) if opstr else []

    def ea(e, size, before):
        return ea_bits(e, size, addr + 2 + 2 * before, lax)

    def ops():
        return [parse_ea(o, sym) for o in raw]

    # --- no operands / simple control
    fixed = {'illegal': 0x4AFC, 'reset': 0x4E70, 'nop': 0x4E71, 'rte': 0x4E73,
             'rts': 0x4E75, 'trapv': 0x4E76, 'rtr': 0x4E77}
    if base in fixed:
        return _words([fixed[base]])
    if base == 'stop':
        return _words([0x4E72, ops()[0]['v']])
    if base == 'trap':
        return _words([0x4E40 | (ops()[0]['v'] & 15)])
    if base == 'link':
        o = ops()
        return _words([0x4E50 | o[0]['r'], o[1]['v'] & 0xFFFF])
    if base == 'unlk':
        return _words([0x4E58 | ops()[0]['r']])

    # --- movem before anything parses a register list as a register
    if base == 'movem':
        s = 1 if sz == 'l' else 0
        if _is_reglist(raw[0]):
            dst = parse_ea(raw[1], sym)
            mask = reglist_mask(raw[0])
            if dst['k'] == PRE:
                mask = int('{:016b}'.format(mask)[::-1], 2)
            m, r, x = ea(dst, None, 1)
            return _words([0x4880 | (s << 6) | (m << 3) | r, mask] + x)
        src = parse_ea(raw[0], sym)
        mask = reglist_mask(raw[1])
        m, r, x = ea(src, None, 1)
        return _words([0x4C80 | (s << 6) | (m << 3) | r, mask] + x)

    # --- branches
    if base == 'bra' or base == 'bsr' or (base[0] == 'b' and base[1:] in CC[2:]):
        cc = 0 if base == 'bra' else 1 if base == 'bsr' else CC.index(base[1:])
        tgt = ops()[0]['v']
        d = tgt - (addr + 2)
        if sz == 's':
            if not lax and (not -0x80 <= d <= 0x7F or d == 0 or d == -1):
                raise AsmError('bcc.s displacement %d' % d)
            return _words([0x6000 | (cc << 8) | (d & 0xFF)])
        if sz == 'w':
            if not lax and not _s16ok(d):
                raise AsmError('bcc.w displacement')
            return _words([0x6000 | (cc << 8), d & 0xFFFF])
        raise AsmError('branch without .s/.w')
    if base in ('dbf', 'dbra') or (base.startswith('db') and base[2:] in CC):
        cc = 1 if base in ('dbf', 'dbra') else CC.index(base[2:])
        o = ops()
        d = o[1]['v'] - (addr + 2)
        if not lax and not _s16ok(d):
            raise AsmError('dbcc displacement')
        return _words([0x50C8 | (cc << 8) | o[0]['r'], d & 0xFFFF])

    # --- immediate group and to-ccr/sr
    if base in IMM_OPS:
        o = ops()
        code = IMM_OPS[base]
        if o[1]['k'] == SPEC:
            if o[1]['v'] == 'ccr':
                return _words([0x003C | (code << 9), o[0]['v'] & 0xFF])
            return _words([0x007C | (code << 9), o[0]['v'] & 0xFFFF])
        _, _, iw = ea_bits(o[0], sz, 0)
        m, r, x = ea(o[1], sz, len(iw))
        return _words([(code << 9) | (SZ[sz] << 6) | (m << 3) | r] + iw + x)

    if base in BIT_OPS:
        o = ops()
        t = BIT_OPS[base]
        if o[0]['k'] == IMM:
            m, r, x = ea(o[1], 'b', 1)
            return _words([0x0800 | (t << 6) | (m << 3) | r, o[0]['v'] & 0xFF] + x)
        m, r, x = ea(o[1], 'b', 0)
        return _words([0x0100 | (o[0]['r'] << 9) | (t << 6) | (m << 3) | r] + x)

    if base == 'movep':
        o = ops()
        if o[0]['k'] == DN:
            opm = 6 if sz == 'w' else 7
            dn, mem = o[0], o[1]
        else:
            opm = 4 if sz == 'w' else 5
            dn, mem = o[1], o[0]
        return _words([(dn['r'] << 9) | (opm << 6) | 0x08 | mem['r'], mem['v'] & 0xFFFF])

    # --- moves
    if base in ('move', 'movea'):
        o = ops()
        if o[0]['k'] == SPEC and o[0]['v'] == 'sr':
            m, r, x = ea(o[1], 'w', 0)
            return _words([0x40C0 | (m << 3) | r] + x)
        if o[1]['k'] == SPEC and o[1]['v'] in ('ccr', 'sr'):
            m, r, x = ea(o[0], 'w', 0)
            return _words([(0x44C0 if o[1]['v'] == 'ccr' else 0x46C0) | (m << 3) | r] + x)
        if o[1]['k'] == SPEC and o[1]['v'] == 'usp':
            return _words([0x4E60 | o[0]['r']])
        if o[0]['k'] == SPEC and o[0]['v'] == 'usp':
            return _words([0x4E68 | o[1]['r']])
        sc = {'b': 1, 'w': 3, 'l': 2}[sz]
        sm, sr_, sx = ea(o[0], sz, 0)
        dm, dr, dx = ea(o[1], sz, len(sx))
        return _words([(sc << 12) | (dr << 9) | (dm << 6) | (sm << 3) | sr_] + sx + dx)
    if base == 'moveq':
        o = ops()
        v = o[0]['v']
        if not lax and not -0x80 <= v <= 0x7F:
            raise AsmError('moveq range')
        return _words([0x7000 | (o[1]['r'] << 9) | (v & 0xFF)])

    # --- one-operand group
    one = {'negx': 0x4000, 'clr': 0x4200, 'neg': 0x4400, 'not': 0x4600, 'tst': 0x4A00}
    if base in one:
        m, r, x = ea(ops()[0], sz, 0)
        return _words([one[base] | (SZ[sz] << 6) | (m << 3) | r] + x)
    if base in ('nbcd', 'tas', 'pea', 'jsr', 'jmp'):
        code = {'nbcd': 0x4800, 'tas': 0x4AC0, 'pea': 0x4840, 'jsr': 0x4E80, 'jmp': 0x4EC0}[base]
        m, r, x = ea(ops()[0], 'b', 0)
        return _words([code | (m << 3) | r] + x)
    if base == 'swap':
        return _words([0x4840 | ops()[0]['r']])
    if base == 'ext':
        return _words([(0x4880 if sz == 'w' else 0x48C0) | ops()[0]['r']])
    if base == 'lea':
        o = ops()
        m, r, x = ea(o[0], None, 0)
        return _words([0x41C0 | (o[1]['r'] << 9) | (m << 3) | r] + x)
    if base == 'chk':
        o = ops()
        m, r, x = ea(o[0], 'w', 0)
        return _words([0x4180 | (o[1]['r'] << 9) | (m << 3) | r] + x)

    if base in ('addq', 'subq'):
        o = ops()
        q = o[0]['v']
        if not lax and not 1 <= q <= 8:
            raise AsmError('quick range')
        m, r, x = ea(o[1], sz, 0)
        return _words([0x5000 | ((q & 7) << 9) | ((base == 'subq') << 8) | (SZ[sz] << 6) | (m << 3) | r] + x)
    if base[0] == 's' and base[1:] in CC:
        m, r, x = ea(ops()[0], 'b', 0)
        return _words([0x50C0 | (CC.index(base[1:]) << 8) | (m << 3) | r] + x)

    if base in ('divu', 'divs', 'mulu', 'muls'):
        o = ops()
        code = {'divu': 0x80C0, 'divs': 0x81C0, 'mulu': 0xC0C0, 'muls': 0xC1C0}[base]
        m, r, x = ea(o[0], 'w', 0)
        return _words([code | (o[1]['r'] << 9) | (m << 3) | r] + x)

    if base in ('suba', 'adda', 'cmpa'):
        o = ops()
        code = {'suba': 0x9000, 'adda': 0xD000, 'cmpa': 0xB000}[base]
        m, r, x = ea(o[0], sz, 0)
        return _words([code | (o[1]['r'] << 9) | ((3 if sz == 'w' else 7) << 6) | (m << 3) | r] + x)

    if base in ('addx', 'subx', 'abcd', 'sbcd'):
        o = ops()
        code = {'addx': 0xD100, 'subx': 0x9100, 'abcd': 0xC100, 'sbcd': 0x8100}[base]
        s = SZ[sz] if sz else 0
        rm = 1 if o[0]['k'] == PRE else 0
        return _words([code | (o[1]['r'] << 9) | (s << 6) | (rm << 3) | o[0]['r']])
    if base == 'cmpm':
        o = ops()
        return _words([0xB108 | (o[1]['r'] << 9) | (SZ[sz] << 6) | o[0]['r']])
    if base == 'exg':
        o = ops()
        a, b = o
        if a['k'] == AN and b['k'] == DN:
            a, b = b, a
        if a['k'] == DN and b['k'] == DN:
            return _words([0xC140 | (a['r'] << 9) | b['r']])
        if a['k'] == AN and b['k'] == AN:
            return _words([0xC148 | (a['r'] << 9) | b['r']])
        return _words([0xC188 | (a['r'] << 9) | b['r']])

    if base in ('or', 'and', 'sub', 'add', 'cmp', 'eor'):
        o = ops()
        # AS keeps `op #imm,Dn` in the <ea>,Dn form; only eor (no <ea>,Dn
        # form) and a memory destination need the I-form
        if o[0]['k'] == IMM and (base == 'eor' or o[1]['k'] not in (AN, DN)):
            return encode(TO_IMM[base] + '.' + sz, opstr, addr, sym, lax)
        s = SZ[sz]
        code = {'or': 0x8000, 'sub': 0x9000, 'cmp': 0xB000, 'and': 0xC000,
                'add': 0xD000, 'eor': 0xB000}[base]
        if base == 'eor':
            m, r, x = ea(o[1], sz, 0)
            return _words([code | (o[0]['r'] << 9) | ((4 + s) << 6) | (m << 3) | r] + x)
        if o[1]['k'] == DN:
            m, r, x = ea(o[0], sz, 0)
            return _words([code | (o[1]['r'] << 9) | (s << 6) | (m << 3) | r] + x)
        if base == 'cmp':
            raise AsmError('cmp needs a data register destination')
        m, r, x = ea(o[1], sz, 0)
        return _words([code | (o[0]['r'] << 9) | ((4 + s) << 6) | (m << 3) | r] + x)

    for pre, t in SHIFTS.items():
        if base in (pre + 'l', pre + 'r'):
            dr = 1 if base.endswith('l') else 0
            o = ops()
            if len(o) == 1:
                m, r, x = ea(o[0], 'w', 0)
                return _words([0xE0C0 | (t << 9) | (dr << 8) | (m << 3) | r] + x)
            if o[0]['k'] == IMM:
                cnt, ir = o[0]['v'] & 7, 0
            else:
                cnt, ir = o[0]['r'], 1
            return _words([0xE000 | (cnt << 9) | (dr << 8) | (SZ[sz] << 6) | (ir << 5) | (t << 3) | o[1]['r']])

    raise AsmError('unknown mnemonic %s' % mn)
