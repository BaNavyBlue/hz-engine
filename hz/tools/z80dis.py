#!/usr/bin/env python3
"""Disassemble Herzog Zwei's Z80 sound driver from a ROM image.

    python3 tools/z80dis.py [--rom hz.md] [--names FILE] [-l LISTING]
                            [--asm OUT.asm]

The image is the $1900 bytes at Z80Driver (ROM $20B00; hz.sym beside the
ROM gives the address in a changed one) that `Snd_Init` copies to Z80 RAM
$0000 (it copies $1980: the last $80 come from the table after the driver
and are cleared by the driver's own start-up, see the names file). A file
of $1900 bytes is taken as the driver alone.

The driver's source is asm/hz.asm's, at Z80Driver. What this writes is the
disassembly from the bytes and the names file alone: a listing (-l), or a
source that assembles by itself (--asm), without the record fields and
the other names hz.asm has since been given.

Recursive traversal from $0000 and the IM 1 interrupt at $0038, plus the
jump tables declared in the names file. Names file (tools/names/z80.txt):

    $ADDR Name ; note        a label (code or data) or, past the image, a
                             RAM equate
    $ADDR .name ; note       a local label (AS scope: the last global)
    = ; note                 a header note for the label above
    table $ADDR N code|data  N little-endian words at ADDR: pointers to code
                             (seeds) or to data
    code $ADDR               a code seed (an address only stored, e.g. a
                             routine patched into a call's operand)
    ptr $INSN                the ld rr,nn at INSN loads a low variable's
                             address: name it (below $3C, nn stays a number
                             otherwise: offsets and zeros)
    data $ADDR $END          force bytes ADDR..END-1 to stay data

Unnamed targets get loc_XXXX (jumps), sub_XXXX (calls), dat_XXXX /
z_XXXX (memory in / past the image).
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z80  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

HW = {
    0x4000: ('zYM_Addr0', 'YM2612 part I address (status on read)'),
    0x4001: ('zYM_Data0', 'YM2612 part I data'),
    0x4002: ('zYM_Addr1', 'YM2612 part II address'),
    0x4003: ('zYM_Data1', 'YM2612 part II data'),
    0x6000: ('zBankReg', 'bank register: 9 writes of bit 0, A15 first'),
    0x7F11: ('zPSG', 'SN76489 PSG'),
}


class Names:
    def __init__(self):
        self.label = {}       # addr -> name
        self.note = {}        # addr -> note
        self.header = {}      # addr -> [header notes]
        self.tables = []      # (addr, count, kind)
        self.data = []        # (start, end)
        self.code = []        # extra code seeds
        self.ptr = set()      # insns whose low immediate is an address

    @classmethod
    def load(cls, path):
        n = cls()
        if not path or not os.path.exists(path):
            return n
        last = None
        for lineno, raw in enumerate(open(path), 1):
            line = raw.rstrip('\n')
            body, _, note = line.partition(';')
            body = body.strip()
            note = note.strip()
            if not body:
                continue
            parts = body.split()
            if parts[0] == '=':
                if last is None:
                    raise SystemExit('%s:%d: header note before any label'
                                     % (path, lineno))
                n.header.setdefault(last, []).append(note)
                continue
            if parts[0] == 'table':
                n.tables.append((int(parts[1][1:], 16), int(parts[2], 0),
                                 parts[3]))
                continue
            if parts[0] == 'ptr':
                n.ptr.add(int(parts[1][1:], 16))
                continue
            if parts[0] == 'code':
                n.code.append(int(parts[1][1:], 16))
                continue
            if parts[0] == 'data':
                n.data.append((int(parts[1][1:], 16), int(parts[2][1:], 16)))
                continue
            if not parts[0].startswith('$') or len(parts) != 2:
                raise SystemExit('%s:%d: bad line' % (path, lineno))
            addr = int(parts[0][1:], 16)
            if addr in n.label:
                raise SystemExit('%s:%d: $%04X named twice' % (path, lineno,
                                                               addr))
            n.label[addr] = parts[1]
            if note:
                n.note[addr] = note
            last = addr
        low = {}
        for a, name in n.label.items():
            if name.startswith('.'):
                continue
            k = name.lower()
            if k in low:
                raise SystemExit('duplicate name %s' % name)
            low[k] = a
        return n


class Disasm:
    def __init__(self, mem, names):
        self.mem = mem
        self.names = names
        self.insns = {}
        self.code = bytearray(len(mem))
        self.jumps = set()
        self.calls = set()
        self.refs = {}         # addr -> set of insn addrs referencing it
        self.mem_refs = set()  # addresses used as (nn)
        self.ijumps = []
        self.errors = []

    def run(self, seeds):
        forced = set()
        for s, e in self.names.data:
            forced.update(range(s, e))
        work = list(seeds)
        for addr, count, kind in self.names.tables:
            for i in range(count):
                p = addr + 2 * i
                forced.update((p, p + 1))
                t = self.mem[p] | self.mem[p + 1] << 8
                self.refs.setdefault(t, set()).add(p)
                if kind == 'code':
                    work.append(t)
                    self.jumps.add(t)
        while work:
            a = work.pop()
            while 0 <= a < len(self.mem):
                if a in self.insns:
                    break
                if self.code[a] or a in forced:
                    self.errors.append('overlap at %04X' % a)
                    break
                try:
                    ins = z80.decode(self.mem, a)
                except z80.DecodeError as e:
                    self.errors.append('%04X: %s' % (a, e))
                    break
                if any(a + i in forced or self.code[a + i]
                       for i in range(ins.size)):
                    self.errors.append('overlap at %04X' % a)
                    break
                self.insns[a] = ins
                for i in range(ins.size):
                    self.code[a + i] = 1
                for o in ins.ops:
                    if o.kind in ('nn', 'mem'):
                        self.refs.setdefault(o.value, set()).add(a)
                    if o.kind == 'mem':
                        self.mem_refs.add(o.value)
                for t in ins.targets:
                    self.refs.setdefault(t, set()).add(a)
                    if ins.mn in ('call', 'rst'):
                        self.calls.add(t)
                    else:
                        self.jumps.add(t)
                    work.append(t)
                if ins.flow == 'ijump':
                    self.ijumps.append(a)
                if ins.flow in ('jump', 'ret', 'ijump'):
                    break
                a += ins.size
        return self

    def finish(self):
        """Map every byte to the instruction holding it, and find
        references into the middle of an instruction (patched operands)."""
        self.owner = {}
        for a, ins in self.insns.items():
            for i in range(ins.size):
                self.owner[a + i] = a
        self.inner = {}        # referenced addr -> its instruction's start
        for v in self.mem_refs:
            o = self.owner.get(v)
            if o is not None and o != v:
                self.inner[v] = o
        return self

    def name(self, addr, kind=None):
        n = self.names.label.get(addr)
        if n:
            return n
        if addr in HW:
            return HW[addr][0]
        if addr in self.calls:
            return 'sub_%04X' % addr
        if addr in self.jumps:
            return 'loc_%04X' % addr
        if addr in self.inner.values():
            return 'patch_%04X' % addr
        if kind in ('mem', 'nn'):
            if addr >= len(self.mem):
                return 'z_%04X' % addr if addr < 0x2000 else None
            return 'dat_%04X' % addr
        return None

    def labelled(self, addr):
        """Addresses inside the image that get a label line."""
        if addr in self.inner:
            return False
        if addr in self.names.label:
            return True
        if addr in self.jumps or addr in self.calls:
            return True
        if addr in self.inner.values():
            return True
        if addr in self.mem_refs and not self.code[addr]:
            return True
        if addr in self.refs and not self.code[addr] and \
                addr in self.names.label:
            return True
        return False


def build_scopes(d):
    """Parent global for every address (AS composes .x as Parent.x)."""
    parent = {}
    cur = ''
    for a in range(len(d.mem)):
        if d.labelled(a):
            n = d.name(a, 'mem')
            if n and not n.startswith('.'):
                cur = n
        parent[a] = cur
    return parent


def make_label(d, parent, at):
    """label(value, kind) for Op.fmt, for an instruction at `at`."""
    def label(value, kind):
        if kind == 'nn' and value < 0x3C and at not in d.names.ptr:
            return None
        if kind == 'nn':
            known = (value in d.names.label or value in d.jumps or
                     value in d.calls or
                     0x1900 <= value < 0x2000 or value in HW)
            if not known:
                return None
        if kind in ('nn', 'mem') and value >= 0x2000 and value not in HW:
            return None
        if value in d.inner and value in d.names.label:
            return d.names.label[value]
        if value in d.inner and kind == 'mem':
            base = d.inner[value]
            return '%s+%d' % (scoped(d, parent, base, at), value - base)
        if value in HW:
            return d.name(value, kind)
        if value >= len(d.mem):
            return ram_name(d, value)
        return scoped(d, parent, value, at)
    return label


def ram_name(d, value):
    """A RAM address past the image: its name, or name+k inside a named
    variable up to 8 bytes below."""
    if value in d.names.label:
        return d.names.label[value]
    for k in range(1, 8):
        if value - k in d.names.label and value - k >= len(d.mem):
            return '%s+%d' % (d.names.label[value - k], k)
    return 'z_%04X' % value


def scoped(d, parent, value, at):
    n = d.name(value, 'mem')
    if n and n.startswith('.'):
        if parent.get(at) == parent.get(value):
            return n
        return parent.get(value) + n
    return n


def emit(d, out_listing=None, out_asm=None):
    parent = build_scopes(d)
    lines = []
    asm = []
    asm.append('; Herzog Zwei -- the Z80 sound driver, for AS')
    asm.append('; Assembles by itself to the $1900 bytes at hz.asm\'s Z80Driver. '
               'Z80 RAM $0000-$1FFF:')
    asm.append('; the driver at $0000, its variables from $1980, '
               'the stack below $2000.')
    asm.append('')
    asm.append('\tcpu\tz80')
    asm.append('')
    for a, (n, note) in sorted(HW.items()):
        asm.append('%s\tequ\t%s\t; %s' % (n, z80.hexnum(a, 4), note))
    asm.append('')
    ram = sorted(set(a for a in d.names.label
                     if len(d.mem) <= a < 0x2000 or a in d.inner)
                 | set(v for v in d.refs if len(d.mem) <= v < 0x2000
                       and '+' not in ram_name(d, v)))
    for a in ram:
        n = d.name(a, 'mem') if a < len(d.mem) else ram_name(d, a)
        note = d.names.note.get(a, '')
        asm.append('%s\tequ\t%s%s' % (n, z80.hexnum(a, 4),
                                       ('\t; ' + note) if note else ''))
    a = 0
    size = len(d.mem)
    while a < size:
        if d.labelled(a):
            n = d.name(a, 'mem')
            note = d.names.note.get(a, '')
            if not n.startswith('.'):
                asm.append('')
                for h in d.names.header.get(a, []):
                    asm.append('; %s' % h)
                asm.append('; $%04X%s' % (a, ('  ' + note) if note else ''))
                asm.append('%s:' % n)
                lines.append('')
                lines.append('%s:%s' % (n, ('  ; ' + note) if note else ''))
            else:
                asm.append('')
                asm.append('%s:\t\t\t\t; $%04X%s' % (n, a,
                           ('  ' + note) if note else ''))
                lines.append('%s:%s' % (n, ('  ; ' + note) if note else ''))
        if a in d.insns:
            ins = d.insns[a]
            t = ins.text(make_label(d, parent, a))
            asm.append('\t' + t)
            lines.append('  %04X  %-10s  %s' % (a, ins.raw.hex(), t))
            a += ins.size
            continue
        b = a + 1
        while b < size and b not in d.insns and not d.labelled(b):
            b += 1
        for c in range(a, b, 16):
            chunk = d.mem[c:min(b, c + 16)]
            asm.append('\tdb\t' + ','.join(z80.hexnum(x, 2) for x in chunk))
            lines.append('  %04X  db  %s' % (c, ' '.join('%02X' % x
                                                          for x in chunk)))
        a = b
    if out_listing:
        open(out_listing, 'w').write('\n'.join(lines) + '\n')
    if out_asm:
        open(out_asm, 'w').write('\n'.join(asm) + '\n')


DRIVER_SIZE = 0x1900


def driver_bytes(path):
    """The driver's bytes: a file of DRIVER_SIZE as it is, else the ROM
    image's at Z80Driver (hz.sym beside it, else $20B00)."""
    data = open(path, 'rb').read()
    if len(data) == DRIVER_SIZE:
        return data
    at = 0x20B00
    sym = os.path.splitext(path)[0] + '.sym'
    if os.path.exists(sym):
        for line in open(sym):
            name, _, value = line.partition(' ')
            if name == 'Z80Driver':
                at = int(value.strip().lstrip('$'), 16)
    return data[at:at + DRIVER_SIZE]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default=os.path.join(ROOT, 'hz.md'))
    ap.add_argument('--names', default=os.path.join(HERE, 'names', 'z80.txt'))
    ap.add_argument('-l', '--listing')
    ap.add_argument('--asm')
    args = ap.parse_args()
    mem = driver_bytes(args.rom)
    names = Names.load(args.names)
    d = Disasm(mem, names).run([0x0000, 0x0038] + names.code)
    for a in names.code:
        d.calls.add(a)
    d.finish()
    ncode = sum(d.code)
    print('%d insns, %d code bytes of %d, %d jump targets, %d calls, '
          '%d jp (rr) sites' % (len(d.insns), ncode, len(mem), len(d.jumps),
                                len(d.calls), len(d.ijumps)))
    for e in d.errors:
        print('  ' + e)
    for a in d.ijumps:
        print('  jp (rr) at %04X' % a)
    emit(d, args.listing, args.asm)


if __name__ == '__main__':
    main()
