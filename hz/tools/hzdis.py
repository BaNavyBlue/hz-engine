#!/usr/bin/env python3
"""Recursive-traversal disassembly of Herzog Zwei (USA, Europe).

    python3 hzdis.py hz.md -o hz.lst                     # listing, for reading
    python3 hzdis.py hz.md -o hz.lst --asm asm/hz.asm    # plus AS source

--asm also writes asm/packed/*.lzw (each packed entry) and asm/data/*.bin
(big unknown data) from the ROM; hz.asm binclude's them. Every instruction
is round-tripped through m68kenc first, and any that would not reassemble
to its original bytes is written as dc.w with the instruction as a comment.

Walks the ROM from the seeds below, following branches and calls, and writes
a listing: code with labels and xrefs, the inline arguments of the
print/format routines, resolved pointer tables, and short hex dumps of the
data between. Not rebuildable; it is for reading.

What the walk knows beyond plain control flow:

  * The autovectors go through RAM stubs built by sub_7B8, so the handlers
    ($4760 HBlank, $4788 VBlank) are seeds, and so is $8FBA, the VBlank
    handler loc_8EF0 swaps in with move.l #$8FBA,($FFF832).l.
  * $300-$338 is a table of bra.w entry points; the $53000 and $5D000
    modules call main-program routines through it by absolute address.
  * $5E230 is a return address UnitAI_Infantry pushes with
    move.l #$5E230,d0 / move.l d0,-(sp) before branching to its handlers.
  * Some routines take arguments inline after the bsr and return past them
    (INLINE below). Without this the walk decodes the strings as code.
  * movea.l #T,Ax (or lea T,Ax) ... movea.l (Ax,Dn.w),Ay ; jmp/jsr (Ay) is a
    longword pointer table at T. Entries are read while they are even ROM
    addresses and the table has not run into code.
"""
import argparse
import collections
import os
import re
import sys

import m68k
from m68k import IMM, ABSL, ABSW, PCD, IDX, IND

ROM_END = 0x80000

SEEDS = ([0x200] + list(range(0x270, 0x298, 4)) + [0x298, 0x4760, 0x4788,
         0x7B8, 0x8FBA, 0x5E230] + list(range(0x300, 0x33C, 4)))

# Hardware registers, for operand names only.
HW = {
    0xC00000: 'VDP_DATA', 0xC00004: 'VDP_CTRL', 0xC00008: 'VDP_HV',
    0xC00011: 'PSG',
    0xA10001: 'IO_VERSION', 0xA10003: 'IO_DATA1', 0xA10005: 'IO_DATA2',
    0xA10007: 'IO_DATA3', 0xA10009: 'IO_CTRL1', 0xA1000B: 'IO_CTRL2',
    0xA1000D: 'IO_CTRL3', 0xA11100: 'Z80_BUSREQ', 0xA11200: 'Z80_RESET',
    0xA14000: 'TMSS',
}


def _skip_str(header):
    def skip(rom, a):
        a += header
        while rom[a] != 0:
            a += 1
        return (a + 2) & ~1     # past the NUL, then word-aligned
    return skip


def _skip_word(rom, a):
    return a + 2


# routine -> (how far its inline arguments run, what they are)
INLINE = {
    0x0BC2: (_skip_str(3), 'plane, x, y, text'),        # VRAM text via VDP
    0x0C40: (_skip_str(3), 'word, byte, text'),         # copies to $FFF958
    0x56A64: (_skip_word, 'word'),                      # sets $FF9828 (attr)
    0x56A84: (_skip_word, 'x, y'),                      # sets $FF9824/6
    0x56AA4: (_skip_word, 'word'),                      # ORs into $FF9828
    0x56B3C: (_skip_str(0), 'text'),                    # at current x, y
    0x56B5A: (_skip_str(2), 'x, y, text'),
    0x56C22: (_skip_str(2), 'x, y, text'),
}


RAM_BASE = 0xE00000
NAMES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'hz_names.txt')


# Instructions whose #imm is a number that only happens to equal a named
# address ("$ADDR  #number" in hz_names.txt): imm_label leaves them alone.
PLAIN_IMM = set()


def load_names(path):
    """hz_names.txt -> (rom, ram, fields).

    rom and ram map addr -> (name, note); fields is a list of
    (offset, name, note) in file order, since records share offsets
    (obj_power and stat_price are both +$08). A ROM name starting with a
    dot is local to the global label before it (AsmWriter checks that).
    """
    rom, ram, fields, seen = {}, {}, [], {}
    PLAIN_IMM.clear()
    for n, line in enumerate(open(path), 1):
        text, _, note = line.partition(';')
        parts = text.split()
        if not parts:
            continue
        if len(parts) == 2 and parts[0][0] == '$' and parts[1] == '#number':
            PLAIN_IMM.add(int(parts[0][1:], 16))
            continue
        if len(parts) != 2 or parts[0][0] not in '$+':
            raise SystemExit('%s:%d: expected "$ADDR name" or "+$OFF name"' % (path, n))
        name = parts[1]
        if not re.fullmatch(r'\.?[A-Za-z_][A-Za-z0-9_]*', name):
            raise SystemExit('%s:%d: bad name %s' % (path, n, name))
        if name.startswith('.'):
            if parts[0][0] != '$' or int(parts[0][1:], 16) >= RAM_BASE:
                raise SystemExit('%s:%d: %s: only ROM labels can be local' % (path, n, name))
        else:
            if name.lower() in seen:
                raise SystemExit('%s:%d: %s already used at line %d' % (path, n, name, seen[name.lower()]))
            seen[name.lower()] = n
        if parts[0][0] == '+':
            fields.append((int(parts[0][2:], 16), name, note.strip()))
            continue
        a = int(parts[0][1:], 16)
        table = ram if a >= RAM_BASE else rom
        if a in table:
            raise SystemExit('%s:%d: $%X named twice' % (path, n, a))
        table[a] = (name, note.strip())
    return rom, ram, fields


class Dis:
    def __init__(self, rom, names=({}, {}, [])):
        self.rom = rom
        self.insns = {}
        self.owner = {}
        self.labels = {}
        self.bad = {}
        self.conflicts = []
        self.indirect = []
        self.xref = collections.defaultdict(set)
        self.queue = []
        self.tables = {}        # base -> (entries, site)
        self.inline_data = {}   # call site -> (start, end, routine)
        self.data = set()       # bytes known to be data
        self.rom_names, self.ram_names, self.fields = names

    def add(self, addr, kind, src=None):
        addr &= 0xFFFFFF
        if src is not None:
            self.xref[addr].add(src)
        if addr >= ROM_END or addr & 1:
            return
        if self.labels.get(addr) != 'sub':
            self.labels[addr] = kind
        self.queue.append(addr)

    def run(self):
        while self.queue:
            a = self.queue.pop()
            while a not in self.insns:
                if a in self.owner or a in self.data:
                    self.conflicts.append((a, self.owner.get(a)))
                    break
                try:
                    i = m68k.decode(self.rom, a)
                except m68k.DecodeError as e:
                    self.bad[a] = str(e)
                    break
                if any(b in self.owner or b in self.data for b in range(a, i.end)):
                    self.conflicts.append((a, None))
                    break
                self.insns[a] = i
                for b in range(a, i.end):
                    self.owner[b] = a
                for t in i.targets:
                    self.add(t, 'sub' if i.flow == 'call' else 'loc', a)
                if i.mn in ('jmp', 'jsr') and not i.targets:
                    self.indirect.append(a)
                if i.flow in ('jump', 'ret'):
                    break
                if i.flow == 'call' and i.targets and i.targets[0] in INLINE:
                    end = INLINE[i.targets[0]][0](self.rom, i.end)
                    self.inline_data[a] = (i.end, end, i.targets[0])
                    self.data.update(range(i.end, end))
                    a = end
                    continue
                a = i.end

    def prev(self, a, n):
        """Up to n insns running linearly into a, nearest first."""
        out = []
        for _ in range(n):
            found = None
            for back in range(2, 12, 2):
                q = self.insns.get(a - back)
                if q is not None and q.end == a:
                    found = q
                    break
            if found is None:
                break
            out.append(found)
            a = found.addr
        return out

    def referenced(self):
        """ROM addresses named by any operand: a table stops at one of these."""
        refd = set(self.tables)
        for i in self.insns.values():
            for o in i.ops:
                if isinstance(o, str):
                    continue
                if o.kind == IMM and o.size == 'l':
                    refd.add(o.imm)
                elif o.kind in (ABSL, ABSW, PCD, m68k.PCX) and o.addr is not None:
                    refd.add(o.addr & 0xFFFFFF)
        return refd

    def resolve_tables(self):
        new = 0
        self._refd = self.referenced()
        for site in self.indirect:
            t = self.insns[site].ops[0]
            if t.kind != IND:
                continue
            base = self._table_base(site, t.reg)
            if base is None or base in self.tables:
                continue
            ents = self._read_table(base)
            if not ents:
                continue
            self.tables[base] = (ents, site)
            self.data.update(range(base, base + 4 * len(ents)))
            for e in ents:
                self.add(e, 'loc', site)
            new += 1
        return new

    def _table_base(self, site, ay):
        for p in self.prev(site, 6):
            if p.mn != 'movea.l' or p.ops[1] != m68k.areg(ay):
                continue
            s = p.ops[0]
            if isinstance(s, str) or s.kind != IDX:
                return None
            ax = m68k.areg(s.reg)
            for q in self.prev(p.addr, 64):
                if q.ops and q.ops[-1] == ax:
                    s = q.ops[0]
                    if q.mn == 'movea.l' and not isinstance(s, str) and s.kind == IMM:
                        return s.imm
                    if q.mn == 'lea' and s.kind in (ABSL, ABSW, PCD):
                        return s.addr
                    return None
            return None
        return None

    def _read_table(self, base):
        ents = []
        p = base
        while p + 4 <= ROM_END:
            if p != base and (p in self.labels or p in self.insns or p in self._refd):
                break
            if p in self.owner or p in self.data:
                break
            v = int.from_bytes(self.rom[p:p + 4], 'big')
            if not (0x200 <= v < ROM_END) or v & 1:
                break
            ents.append(v)
            p += 4
            if min(ents) > base and p >= min(ents):
                break
        return ents

    def trim_tables(self):
        """Cut a table short where a later pass found something inside it.

        A table read early can run on into the next one before the code that
        names the next one has been walked ($B112 ran into $B15A this way).
        """
        refd = self.referenced()
        cut = 0
        for base, (ents, site) in list(self.tables.items()):
            for n in range(1, len(ents)):
                p = base + 4 * n
                if p in refd or p in self.labels or p in self.insns:
                    self.data.difference_update(range(p, base + 4 * len(ents)))
                    self.tables[base] = (ents[:n], site)
                    cut += 1
                    break
        return cut

    def walk(self, seeds):
        for s in seeds:
            self.add(s, 'sub')
        self.run()
        while True:
            while self.resolve_tables():
                self.run()
            if not self.trim_tables():
                break

    def label(self, addr):
        addr &= 0xFFFFFF
        if addr >= RAM_BASE and addr in self.ram_names:
            return self.ram_names[addr][0]
        k = self.labels.get(addr)
        if (k or addr in self.tables) and addr in self.rom_names:
            return self.rom_names[addr][0]
        if k == 'sub':
            return 'sub_%X' % addr
        if k == 'loc':
            return 'loc_%X' % addr
        if addr in self.tables:
            return 'tbl_%X' % addr
        return HW.get(addr)


def _dcb(data, per=16):
    for k in range(0, len(data), per):
        chunk = data[k:k + per]
        yield 'dc.b    ' + ','.join('$%02X' % b for b in chunk)


def _text_dcb(data):
    """dc.b with runs of printable ASCII quoted."""
    parts = []
    run = ''
    for b in data:
        if 0x20 <= b < 0x7F and b not in (0x22, 0x5C):
            run += chr(b)
            continue
        if run:
            parts.append('"%s"' % run)
            run = ''
        parts.append('$%02X' % b)
    if run:
        parts.append('"%s"' % run)
    return 'dc.b    ' + ','.join(parts)


def write_listing(D, out, gap_dump=64):
    rom = D.rom
    kinds = {}
    for a in D.insns:
        kinds[a] = 'insn'
    for site, (s, e, r) in D.inline_data.items():
        kinds[s] = 'inline'
    for base in D.tables:
        kinds[base] = 'table'

    w = out.write
    w('; Herzog Zwei (USA, Europe) -- traversal listing from hzdis.py\n')
    w('; %d insns, %d code bytes, %d pointer tables, %d inline-argument sites\n'
      % (len(D.insns), sum(i.size for i in D.insns.values()),
         len(D.tables), len(D.inline_data)))
    w('; Not rebuildable. Unlabelled data between code is summarised, first\n'
      '; %d bytes shown.\n\n' % gap_dump)

    a = 0x200
    while a < ROM_END:
        k = kinds.get(a)
        lab = D.label(a) if a in D.labels or a in D.tables else None
        if lab is None and a in D.rom_names:
            lab = D.rom_names[a][0]
        if lab and a in D.rom_names and D.rom_names[a][1]:
            w('\n; %s' % D.rom_names[a][1])
        if lab:
            xr = sorted(D.xref.get(a, ()))
            w('\n%s:' % lab)
            if xr:
                shown = ' '.join('%06X' % x for x in xr[:8])
                more = ' +%d' % (len(xr) - 8) if len(xr) > 8 else ''
                w('%s; xref %s%s' % (' ' * max(1, 32 - len(lab)), shown, more))
            w('\n')
        if k == 'insn':
            i = D.insns[a]
            raw = ' '.join(i.raw[j:j + 2].hex().upper() for j in range(0, i.size, 2))
            w('  %06X  %-22s  %s\n' % (a, raw, m68k.text(i, D.label)))
            a = i.end
            continue
        if k == 'inline':
            site = next(x for x, v in D.inline_data.items() if v[0] == a)
            s, e, r = D.inline_data[site]
            w('  %06X  %-22s  %s    ; %s\n' % (a, '', _text_dcb(rom[s:e]), INLINE[r][1]))
            a = e
            continue
        if k == 'table':
            ents, site = D.tables[a]
            w('                                  ; pointer table, used at %06X\n' % site)
            for n, v in enumerate(ents):
                w('  %06X  %-22s  dc.l    %s    ; %d\n'
                  % (a + 4 * n, '', D.label(v) or '$%X' % v, n))
            a += 4 * len(ents)
            continue
        # data run up to the next thing we know about
        e = a + 1
        while (e < ROM_END and e not in kinds and e not in D.rom_names
               and not (e in D.labels and not e & 1)):
            e += 1
        n = e - a
        if n <= gap_dump:
            for line in _dcb(rom[a:e]):
                w('  %06X  %-22s  %s\n' % (a, '', line))
        else:
            w('  %06X  ; ---- data $%06X-$%06X (%d bytes) ----\n' % (a, a, e - 1, n))
            for line in _dcb(rom[a:a + gap_dump]):
                w('  %06X  %-22s  %s\n' % (a, '', line))
            w('          ...\n')
        a = e


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('-o', '--out', default='hz.lst')
    ap.add_argument('--gap-dump', type=int, default=64)
    ap.add_argument('--names', default=NAMES_FILE,
                    help='label names (default: hz_names.txt beside this script)')
    ap.add_argument('--asm', metavar='FILE',
                    help='also write AS source here, with packed/ and data/ beside it')
    args = ap.parse_args()
    rom = open(args.rom, 'rb').read()
    D = Dis(rom, load_names(args.names))
    D.walk(SEEDS)
    with open(args.out, 'w') as f:
        write_listing(D, f, args.gap_dump)
    if args.asm:
        aw = AsmWriter(D)
        os.makedirs(os.path.dirname(os.path.abspath(args.asm)), exist_ok=True)
        with open(args.asm, 'w') as f:
            aw.write(f)
        aw.write_files(os.path.dirname(os.path.abspath(args.asm)))
        print('%s: %d labels, %d binclude files, %d instructions as dc.w'
              % (args.asm, len(aw.names), len(aw.files), len(aw.fallback)))
    code = sum(i.size for i in D.insns.values())
    print('%d insns, %d code bytes, %d tables, %d inline sites, %d bad, %d conflicts'
          % (len(D.insns), code, len(D.tables), len(D.inline_data),
             len(D.bad), len(D.conflicts)))
    for a, why in sorted(D.bad.items()):
        print('  bad decode at %06X (%s), from %s'
              % (a, why, ' '.join('%06X' % x for x in sorted(D.xref.get(a, ())))))
    left = [s for s in D.indirect if s not in {v[1] for v in D.tables.values()}]
    for s in left:
        print('  unresolved %06X %s' % (s, m68k.text(D.insns[s], D.label)))



# ---------------------------------------------------------------------------
# AS source

import os
import m68kenc

BIT_MN = ('btst', 'bchg', 'bclr', 'bset')

# Header fields, $100-$1FF.
HEADER = [
    (0x100, 16, 'console'), (0x110, 16, 'copyright'), (0x120, 48, 'domestic name'),
    (0x150, 48, 'overseas name'), (0x180, 14, 'serial'), (0x18E, 2, 'checksum'),
    (0x190, 16, 'I/O support'), (0x1A0, 8, 'ROM start, end'), (0x1A8, 8, 'RAM start, end'),
    (0x1B0, 12, 'SRAM'), (0x1BC, 12, 'modem'), (0x1C8, 40, 'memo'), (0x1F0, 16, 'region'),
]

# The packed tables (see hz_lzw.h): each entry's packed bytes are binclude'd.
PACKED = {0x22400: 'map_tiles', 0x35000: 'map_layout', 0x3F000: 'map_blocks',
          0x57800: 'forest'}

# Where code lives. Unknown data outside these, if big, goes to data/.
CODE_ZONES = [(0x0, 0x10000), (0x53000, 0x60000)]
BIG_DATA = 0x1000


def _in_code_zone(a):
    return any(lo <= a < hi for lo, hi in CODE_ZONES)


class AsmWriter:
    def __init__(self, D):
        self.D = D
        self.rom = D.rom
        self.names = {}         # addr -> label defined in the source
        self.blobs = {}         # start -> (end, path)
        self.fallback = []      # (addr, text) emitted as dc.w
        self.files = {}         # path -> bytes
        self.parent = {}        # .local addr -> global label it belongs to
        self.written = {}       # .local addr -> scope it was written under
        self.scope = None       # last global label written
        self.at = 0             # address being written, for errors

    # -- labels ---------------------------------------------------------
    def _splittable(self, a):
        """Can a label go at a? Not inside an instruction or a pointer."""
        D = self.D
        if a in D.insns:
            return True
        if a in D.owner:
            return False
        for base, (ents, site) in D.tables.items():
            if base <= a < base + 4 * len(ents):
                return (a - base) % 4 == 0
        return True

    def collect_labels(self):
        D = self.D
        for a in D.labels:
            if a in D.insns:
                self.names[a] = D.label(a)
        for base in D.tables:
            self.names[base] = 'tbl_%X' % base
        for a, i in D.insns.items():
            if self._is_branch(i):
                continue
            for o in i.ops:
                if isinstance(o, str) or o.kind not in (m68k.PCD, m68k.PCX):
                    continue
                t = o.addr & 0xFFFFFF
                if t < ROM_END and t not in self.names and self._splittable(t):
                    self.names[t] = 'dat_%X' % t
        for a, (name, note) in D.rom_names.items():
            if a in self.names or (0x200 <= a < ROM_END and self._splittable(a)):
                self.names[a] = name
            else:
                raise SystemExit('hz_names.txt: $%X (%s) is inside an instruction or pointer'
                                 % (a, name))
        for base, name in PACKED.items():
            self.names.setdefault(base, 'pack_' + name)
            n = self.rom[base + 1]
            for k in range(n):
                off = int.from_bytes(self.rom[base + 2 + 8 * k: base + 6 + 8 * k], 'big')
                ln = int.from_bytes(self.rom[base + 6 + 8 * k: base + 10 + 8 * k], 'big')
                s = base + off
                self.names[s] = 'pack_%s_%d' % (name, k)
                path = 'packed/%s_%d.lzw' % (name, k)
                self.blobs[s] = (s + ln, path)
                self.files[path] = self.rom[s:s + ln]
        self._find_parents()

    def _find_parents(self):
        """Each .local belongs to the last global label before it.

        That is how AS composes a temporary symbol (.loop under Foo is
        Foo.loop), so an unnamed loc_ or dat_ label in between starts a new
        scope just as a named one does.
        """
        scope, full = None, {}
        for a in sorted(self.names):
            name = self.names[a]
            if not name.startswith('.'):
                if 0x200 <= a < ROM_END:
                    scope = name
                continue
            if not 0x200 <= a < ROM_END or scope is None:
                raise SystemExit('hz_names.txt: %s ($%X) has no global label before it' % (name, a))
            key = (scope + name).lower()
            if key in full:
                raise SystemExit('hz_names.txt: %s used twice under %s ($%X and $%X)'
                                 % (name, scope, full[key], a))
            full[key] = a
            self.parent[a] = scope

    def full_name(self, a):
        """Symbol-table name: Parent.local for a .local."""
        name = self.names[a]
        return self.parent[a] + name if name.startswith('.') else name

    def _ref(self, a):
        """Name of a ROM label as an operand written at self.at."""
        name = self.names[a]
        if name.startswith('.') and self.parent[a] != self.scope:
            if str(self.scope).startswith(('loc_', 'dat_')):
                hint = 'the unnamed %s splits the routine: name it too' % self.scope
            else:
                hint = 'a label used from another routine needs a global name'
            raise SystemExit('hz_names.txt: %s ($%X) is local to %s but used at $%X under %s; %s'
                             % (name, a, self.parent[a], self.at, self.scope, hint))
        return name

    def label(self, addr):
        """Operand name for an address as encoded (abs.w arrives sign-extended)."""
        raw = addr & 0xFFFFFFFF
        addr = raw & 0xFFFFFF
        if addr >= RAM_BASE and addr in self.D.ram_names:
            name = self.D.ram_names[addr][0]
            # equates are $FFFFxxxx; an operand encoded as $00FFxxxx needs the mask
            return name if raw >= 0xFF000000 else name + '&$FFFFFF'
        if addr in self.names:
            return self._ref(addr)
        return HW.get(addr)

    def imm_label(self, v):
        """#imm of move.l/movea.l that is a named address, as a symbol."""
        v &= 0xFFFFFFFF
        a = v & 0xFFFFFF
        if a >= RAM_BASE and a in self.D.ram_names and (v >> 24) in (0, 0xFF):
            return self.label(v)
        # a .local is a branch target, never a loaded address: $E000 in
        # M_BlankPlanes is VRAM, not AI_PickUp_WatchEnemy.no_threat
        if (v < ROM_END and v in self.D.rom_names and v in self.names
                and not self.names[v].startswith('.')):
            return self._ref(v)
        return None

    @staticmethod
    def _is_branch(i):
        b = i.mn.split('.')[0]
        return i.flow in ('cond', 'jump', 'call') and b not in ('jmp', 'jsr') and b[0] in 'bd'

    # -- instructions ---------------------------------------------------
    def insn_text(self, i):
        base = i.mn.split('.')[0]
        mn = base if base in BIT_MN else i.mn
        ops = []
        for o in i.ops:
            if self._is_branch(i) and not isinstance(o, str) and o.kind == m68k.PCD:
                ops.append(self.label(o.addr) or '$%X' % o.addr)
            elif (i.mn in ('move.l', 'movea.l') and not isinstance(o, str)
                  and o.kind == m68k.IMM and i.addr not in PLAIN_IMM
                  and self.imm_label(o.imm)):
                ops.append('#' + self.imm_label(o.imm))
            else:
                ops.append(m68k.fmt_ea(o, self.label))
        return mn, ','.join(ops)

    def insn_line(self, i, sym):
        mn, ops = self.insn_text(i)
        try:
            ok = m68kenc.encode(mn, m68kenc.expand_locals(ops, self.scope), i.addr, sym) == i.raw
        except m68kenc.AsmError:
            ok = False
        if ok:
            return '\t%s\t%s' % (mn, ops) if ops else '\t%s' % mn
        words = ','.join('$%04X' % int.from_bytes(i.raw[k:k + 2], 'big')
                         for k in range(0, i.size, 2))
        shown = '%s %s' % (mn, ops) if ops else mn
        self.fallback.append((i.addr, shown))
        return '\tdc.w\t%s\t; %s' % (words, shown)

    # -- data -------------------------------------------------------------
    def data_lines(self, a, e, text=False):
        """dc.b for [a, e), or a binclude if it's big and not near code."""
        if e - a >= BIG_DATA and not _in_code_zone(a) and not text:
            name = self.names.get(a)
            path = ('data/%s.bin' % name if name and not name.startswith(('loc_', 'dat_', '.'))
                    else 'data/rom_%06X.bin' % a)
            self.files[path] = self.rom[a:e]
            return ['\tbinclude\t"%s"\t; $%X bytes' % (path, e - a)]
        out = []
        data = self.rom[a:e]
        if text:
            out.append('\t' + _text_dcb(data).replace('dc.b    ', 'dc.b\t'))
            return out
        for line in _dcb(data):
            out.append('\t' + line.replace('dc.b    ', 'dc.b\t'))
        return out

    # -- the file ---------------------------------------------------------
    def write(self, f):
        D = self.D
        rom = self.rom
        self.collect_labels()
        sym = {self.full_name(a): a for a in self.names}
        for a, n in HW.items():
            sym[n] = a
        for a, (n, note) in D.ram_names.items():
            sym[n] = 0xFF000000 | a
        for off, n, note in D.fields:
            sym[n] = off

        w = f.write
        w('; Herzog Zwei (USA, Europe) -- disassembly for AS (asl + p2bin)\n')
        w('; Rebuilds byte-identical to the original ROM; the binary files it includes\n')
        w('; are in data/ and packed/ beside it.\n')
        w(';\n')
        w('; dc.w lines carry an instruction whose text AS would encode differently\n')
        w('; from Technosoft\'s assembler; the instruction is in the comment.\n')
        w(';   move.b/cmp.b #$FF   byte immediate stored as $FFFF, not $00FF\n')
        w(';   movem.l 0(a0)       d16 form, not (a0)\n\n')
        w('\tcpu\t68000\n\tsupmode\ton\n\tpadding\toff\n\n')
        for a, n in sorted(HW.items()):
            w('%s\tequ\t$%X\n' % (n, a))
        w('\n; RAM. $FFFFxxxx so that (name).w\n'
          '; assembles; abs.l operands are written (name&$FFFFFF).l.\n')
        for a, (n, note) in sorted(D.ram_names.items()):
            w('%s\tequ\t$%X%s\n' % (n, 0xFF000000 | a, '\t; ' + note if note else ''))
        w('\n; Record fields (not yet substituted into operands), by prefix\n')
        groups = []
        for off, n, note in D.fields:
            if n.split('_')[0] not in groups:
                groups.append(n.split('_')[0])
        for off, n, note in sorted(D.fields, key=lambda t: (groups.index(t[1].split('_')[0]), t[0])):
            w('%s\tequ\t$%X%s\n' % (n, off, '\t; ' + note if note else ''))
        w('\n')

        # vectors
        w('Vectors:\n')
        for k in range(0, 0x100, 4):
            v = int.from_bytes(rom[k:k + 4], 'big')
            w('\tdc.l\t%s\n' % (self.label(v) if v in self.names else '$%X' % v))
        w('\nHeader:\n')
        for a, n, what in HEADER:
            if what == 'checksum':
                w('\tdc.w\t$%04X\t; %s\n' % (int.from_bytes(rom[a:a + 2], 'big'), what))
            elif what in ('ROM start, end', 'RAM start, end'):
                w('\tdc.l\t$%X,$%X\t; %s\n' % (int.from_bytes(rom[a:a + 4], 'big'),
                                               int.from_bytes(rom[a + 4:a + 8], 'big'), what))
            else:
                w('\t%s\t; %s\n' % (_text_dcb(rom[a:a + n]).replace('dc.b    ', 'dc.b\t'), what))

        kinds = {}
        for a in D.insns:
            kinds[a] = 'insn'
        for site, (s, e, r) in D.inline_data.items():
            kinds[s] = ('inline', e, r)
        for base, (ents, site) in D.tables.items():
            kinds[base] = ('table', base + 4 * len(ents), site)
        for s, (e, path) in self.blobs.items():
            kinds[s] = ('blob', e, path)
        stops = set(kinds) | set(self.names)
        for lo, hi in CODE_ZONES:
            stops.update((lo, hi))

        a = 0x200
        while a < ROM_END:
            self.at = a
            if a in self.names and self.names[a].startswith('.'):
                self._write_local(w, a)
            elif a in self.names:
                self.scope = self.names[a]
                xr = sorted(D.xref.get(a, ()))
                note = ''
                if xr and a in D.insns:
                    note = '\t\t\t\t; xref ' + ' '.join('%X' % x for x in xr[:6])
                    if len(xr) > 6:
                        note += ' +%d' % (len(xr) - 6)
                if a in D.rom_names:
                    what = D.rom_names[a][1]
                    w('\n; $%06X%s' % (a, '  ' + what if what else ''))
                w('\n%s:%s\n' % (self.names[a], note))
            k = kinds.get(a)
            if k == 'insn':
                i = D.insns[a]
                w(self.insn_line(i, sym) + '\n')
                a = i.end
                continue
            if isinstance(k, tuple) and k[0] == 'inline':
                e = k[1]
                cut = min([x for x in self.names if a < x < e] or [e])
                w(self.data_lines(a, cut, text=True)[0] + '\t; ' + INLINE[k[2]][1] + '\n')
                if cut < e:
                    kinds[cut] = ('inline', e, k[2])
                a = cut
                continue
            if isinstance(k, tuple) and k[0] == 'table':
                e = k[1]
                p = a
                while p < e:
                    self.at = p
                    if p != a and p in self.names and self.names[p].startswith('.'):
                        self._write_local(w, p)
                    elif p != a and p in self.names:
                        self.scope = self.names[p]
                        w('\n%s:\n' % self.names[p])
                    v = int.from_bytes(rom[p:p + 4], 'big')
                    w('\tdc.l\t%s\n' % (self.label(v) or '$%X' % v))
                    p += 4
                a = e
                continue
            if isinstance(k, tuple) and k[0] == 'blob':
                w('\tbinclude\t"%s"\n' % k[2])
                a = k[1]
                continue
            e = a + 1
            while e < ROM_END and e not in stops:
                e += 1
            for line in self.data_lines(a, e):
                w(line + '\n')
            a = e
        for a, p in sorted(self.parent.items()):
            if self.written.get(a) != p:
                raise SystemExit('hz_names.txt: %s ($%X) was not written as a label under %s'
                                 % (self.names[a], a, p))

    def _write_local(self, w, a):
        """.name: with its address and note on the label line."""
        self.written[a] = self.scope
        what = self.D.rom_names[a][1] if a in self.D.rom_names else ''
        w('\n%s:\t\t\t\t; $%06X%s\n' % (self.names[a], a, '  ' + what if what else ''))

    def write_files(self, outdir):
        for path, data in self.files.items():
            full = os.path.join(outdir, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'wb') as f:
                f.write(data)


if __name__ == '__main__':
    sys.exit(main())
