#!/usr/bin/env python3
"""Assemble hz.asm with m68kenc and compare the result with the ROM, or
write the result: the ROM image built from the disassembly alone.

    python3 hzasm_check.py asm/hz.asm hz.md       compare
    python3 hzasm_check.py asm/hz.asm -o hz.md    write (no ROM needed)
    python3 hzasm_check.py asm/hz.asm -o hz.md -s hz.sym
                                                   also the symbols, NAME $ADDR

Writing stops unless the header's checksum is the one ChecksumCheck works
out, which proves the image is the original's. A changed source (code put
in: the source is relocatable) needs its own checksum:

    python3 hzasm_check.py asm/hz.asm -o out.md -s out.sym --fix-checksum
    python3 hzasm_check.py --fix-checksum out.bin  an image from asl + p2bin

A stand-in for `asl` + `p2bin` that understands only what the source uses
(cpu/supmode/padding, equ, function, labels, .local labels, dc.b/w/l with
[count] repetitions (* in the count: the line's address), binclude,
instructions; expressions as m68kenc.value reads them). It proves the emitter accounts for every byte and every
label; it can't prove AS accepts the syntax, which is what the real build
is for.

The Z80 driver (Z80Driver) is Z80 source between `save` / `cpu z180` /
`phase 0` and `dephase` / `restore` (AS's Z180: the Z80's encodings, and
output past $FFFF): its lines (equ, labels, .local labels, db,
instructions; Intel hex) go to z80enc, its labels take the phased address,
and its symbols stay out of the symbol file (Z80 addresses).
"""
import os
import re
import sys

import m68kenc
import z80enc

LABEL = re.compile(r'^(\.?[A-Za-z_][A-Za-z0-9_]*):')
EQU = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+(.+)$', re.I)
FUNC = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s+function\s+(.+)$', re.I)
REPEAT = re.compile(r'^\[([^\]]+)\]')
PC = re.compile(r'(?<![\w)\s])\s*\*')      # * as an operand, not a product


def strip_comment(line):
    out, q = '', False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == ';' and not q:
            break
        out += ch
    return out.rstrip()


def dc_items(s):
    items, cur, q, depth = [], '', False, 0
    for ch in s:
        if ch == '"':
            q = not q
        elif not q and ch in '()':
            depth += 1 if ch == '(' else -1
        if ch == ',' and not q and depth == 0:
            items.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        items.append(cur.strip())
    return items


def z80_line(text, out, pc, sym, lax, scope):
    """One Z80 line (a label already taken off): equ, db or an
    instruction. Returns the name an equ defined, else None."""
    def lookup(name):
        key = scope + name if name.startswith('.') else name
        if key in sym:
            return sym[key]
        if lax:
            return pc           # pass 1: keeps a jr in range
        raise z80enc.EncodeError('undefined symbol %s' % key)
    m = EQU.match(text)
    if m:
        sym[m.group(1)] = z80enc.value(m.group(2), lookup)
        return m.group(1)
    body = text.strip()
    word, _, rest = body.partition('\t')
    if word == 'db':
        for item in rest.split(','):
            out.append(z80enc.value(item, lookup) & 0xFF)
    else:
        out += z80enc.encode(body, lookup, pc)
    return None


def assemble(path, sym, lax, addrs=None, z80_names=None):
    """addrs: a list to receive each line's address (index = line - 1);
    z80_names: a set to receive the Z80 driver's symbols."""
    base = os.path.dirname(os.path.abspath(path))
    out = bytearray()
    scope = None                # last global label, as AS composes .locals
    z80 = False                 # cpu z180 (or z80): the driver's lines
    saved = []                  # save / restore
    phase = None                # (the phased address, where it began)
    if z80_names is None:
        z80_names = set()
    for n, line in enumerate(open(path), 1):
        if addrs is not None:
            addrs.append(len(out))
        text = strip_comment(line)
        if not text.strip():
            continue
        here = len(out) if phase is None else phase[0] + len(out) - phase[1]
        m = LABEL.match(text)
        if m:
            name = m.group(1)
            if name.startswith('.'):
                if scope is None:
                    raise SystemExit('%s:%d: local label %s before any global label' % (path, n, name))
                name = scope + name
            else:
                scope = name
            sym[name] = here
            if z80:
                z80_names.add(name)
            text = text[m.end():]
            if not text.strip():
                continue
        parts = text.strip().split(None, 1)
        op = parts[0].lower()
        arg = parts[1].strip() if len(parts) > 1 else ''
        if op == 'save':
            saved.append(z80)
            continue
        if op == 'restore':
            z80 = saved.pop()
            continue
        if op == 'cpu':
            z80 = arg.lower() in ('z80', 'z180')
            continue
        if op == 'phase':
            v = z80enc.value(arg, sym.__getitem__) if z80 else m68kenc.value(arg, sym)
            phase = (v, len(out))
            continue
        if op == 'dephase':
            phase = None
            continue
        if z80:
            try:
                name = z80_line(text, out, here, sym, lax, scope)
            except (z80enc.EncodeError, KeyError, ValueError) as e:
                raise SystemExit('%s:%d: %s\n    %s' % (path, n, e, line.rstrip()))
            if name:
                z80_names.add(name)
            continue
        m = EQU.match(text)
        if m:
            sym[m.group(1)] = m68kenc.value(m.group(2), sym)
            continue
        m = FUNC.match(text)
        if m:
            parts = m68kenc.split_ops(m.group(2))
            m68kenc.FUNCS[m.group(1)] = ([p.strip() for p in parts[:-1]],
                                         parts[-1])
            continue
        parts = text.strip().split(None, 1)
        op = parts[0].lower()
        arg = parts[1].strip() if len(parts) > 1 else ''
        try:
            if op in ('cpu', 'supmode', 'padding'):
                continue
            if op == 'binclude':
                out += open(os.path.join(base, arg.strip('"')), 'rb').read()
            elif op in ('dc.b', 'dc.w', 'dc.l'):
                width = {'dc.b': 1, 'dc.w': 2, 'dc.l': 4}[op]
                pc = len(out)
                for it in dc_items(arg):
                    count = 1
                    m = REPEAT.match(it)
                    if m:               # [count]item, * in the count the line's address
                        count = m68kenc.value(PC.sub(str(pc), m.group(1)), sym)
                        it = it[m.end():].strip()
                    if it.startswith('"'):
                        out += it.strip('"').encode('latin-1') * count
                    else:
                        v = m68kenc.value(m68kenc.expand_locals(it, scope), sym)
                        v &= (1 << (8 * width)) - 1
                        out += v.to_bytes(width, 'big') * count
            else:
                out += m68kenc.encode(op, m68kenc.expand_locals(arg, scope), len(out), sym, lax)
        except (m68kenc.AsmError, ValueError, OSError) as e:
            raise SystemExit('%s:%d: %s\n    %s' % (path, n, e, line.rstrip()))
    return bytes(out)


def checksum(image):
    """The sum ChecksumCheck works out and compares with the header's $18E:
    the words from EntryPoint (the reset vector, $200), 4 a pass for
    (RomEnd - EntryPoint) / 8 passes, RomEnd being the header's ROM end
    ($1A4) + 1. The count goes to dbf, which takes its low word: in an image
    past $80200 the game sums fewer passes, and so does this."""
    start = int.from_bytes(image[4:8], 'big')
    end = int.from_bytes(image[0x1A4:0x1A8], 'big') + 1
    passes = ((((end - start) >> 3) - 1) & 0xFFFF) + 1
    s = 0
    for a in range(start, start + 8 * passes, 2):
        s = (s + (image[a] << 8 | image[a + 1])) & 0xFFFF
    return s


def fix_checksum(path):
    """An image AS built from a changed source: its header's checksum
    made the one the game works out."""
    image = bytearray(open(path, 'rb').read())
    s = checksum(image)
    old = image[0x18E] << 8 | image[0x18F]
    image[0x18E:0x190] = s.to_bytes(2, 'big')
    open(path, 'wb').write(image)
    print('%s: checksum $%04X (the header had $%04X)' % (path, s, old))
    return 0


def main():
    fix = '--fix-checksum' in sys.argv
    if fix:
        sys.argv.remove('--fix-checksum')
        if len(sys.argv) == 2:
            return fix_checksum(sys.argv[1])
    symfile = None
    if len(sys.argv) == 6 and sys.argv[4] == '-s':
        symfile = sys.argv[5]
        del sys.argv[4:]
    if len(sys.argv) != 3 and not (len(sys.argv) == 4 and sys.argv[2] == '-o'):
        raise SystemExit(__doc__)
    asm = sys.argv[1]
    sym = {'__lax__': True}
    z80 = set()
    assemble(asm, sym, True)                 # pass 1: label addresses
    del sym['__lax__']
    built = assemble(asm, sym, False, None, z80)    # pass 2: for real
    n68k = len(sym) - len(z80)
    if sys.argv[2] == '-o':
        header = built[0x18E] << 8 | built[0x18F]
        if fix:
            header = checksum(built)
            built = built[:0x18E] + header.to_bytes(2, 'big') + built[0x190:]
        if checksum(built) != header:
            print('checksum $%04X, header says $%04X: not written'
                  ' (--fix-checksum writes it)' % (checksum(built), header))
            return 1
        open(sys.argv[3], 'wb').write(built)
        if symfile:
            with open(symfile, 'w') as f:
                for k in sorted(sym, key=lambda k: (sym[k], k)):
                    if k not in z80:
                        f.write('%s $%06X\n' % (k, sym[k] & 0xFFFFFFFF))
        print("wrote %s: %d bytes, %d symbols (and the Z80 driver's %d),"
              " checksum $%04X" % (sys.argv[3], len(built), n68k, len(z80),
                                   header))
        return 0
    rom = open(sys.argv[2], 'rb').read()
    if built == rom:
        print("identical: %d bytes, %d symbols (and the Z80 driver's %d)"
              % (len(built), n68k, len(z80)))
        return 0
    print('DIFFERENT: built %d bytes, ROM %d' % (len(built), len(rom)))
    shown = 0
    for a in range(min(len(built), len(rom))):
        if built[a] != rom[a]:
            print('  first difference at $%06X: built %02X, ROM %02X' % (a, built[a], rom[a]))
            shown += 1
            if shown == 8:
                break
    return 1


if __name__ == '__main__':
    sys.exit(main())
