#!/usr/bin/env python3
"""Name labels in asm/hz.asm directly (from session 21 the disassembly is
the source; tools/hz_names.txt is the record up to session 20).

  hz_name.py ASM NAMES

NAMES has one line per label:

  Routine                      start of a routine's block (its global label)
  loc_XXXX  .name  ; note      a branch target inside it -> a local label
  loc_XXXX  Name   ; note      -> a global label (its note goes on a
                                  `; $ADDR  note` line above it)
  = ; note                     a new note for the routine's own header line

and four directives, applied where they stand in the file (session 31;
`note`, session 35):

  rename Old New ; note        a global label or an equate renamed
                               everywhere (comments too); the note, if any,
                               replaces a label's header line's
  ram $FFxxxx name ; note      a RAM equate, inserted in address order; the
                               address's literal operands become the name:
                               ($FFFFxxxx).w, ($FFxxxx).l, #$FFxxxx
  data $ADDR Name ; note       a label inside a dc.b block (split there);
                               #$ADDR and ($ADDR).l operands become the name
  note Name ; note             a new note for a global label's header line
                               (none: one is added, the address from the
                               assembler, session 48) or an equate's comment

A local is renamed in its definition and in every reference inside the
routine (from its global label to the next global label); the script stops
if one is referenced outside it, if a name is already used in the routine
(case-insensitive, as AS), or if a label isn't found. Afterwards build the
ROM (`make rom`, or tools/hzasm_check.py ASM -o OUT) to prove that no byte
moved.
"""

import re
import sys

LABEL = re.compile(r"^([A-Za-z_.][\w.]*):(.*)$")


def is_global(name):
    return not name.startswith(".")


def routine_span(lines, name):
    start = None
    for i, ln in enumerate(lines):
        m = LABEL.match(ln)
        if not m:
            continue
        if start is None:
            if m.group(1) == name:
                start = i
            continue
        lab = m.group(1)
        if is_global(lab) and not lab.startswith("loc_"):
            return start, i
    if start is None:
        sys.exit("routine %s not found" % name)
    return start, len(lines)


def apply(lines, routine, entries, header_note):
    start, end = routine_span(lines, routine)
    used = set()
    for i in range(start, end):
        m = LABEL.match(lines[i])
        if m and m.group(1).startswith("."):
            used.add(m.group(1).lower())
    if header_note is not None:
        if start == 0 or not lines[start - 1].startswith("; $"):
            sys.exit("%s has no header line" % routine)
        addr = lines[start - 1].split()[1]
        lines[start - 1] = "; %s  %s" % (addr, header_note)
    for old, new, note in entries:
        word = re.compile(r"\b%s\b" % re.escape(old))
        # the definition
        found = None
        for i in range(start, end):
            m = LABEL.match(lines[i])
            if m and m.group(1) == old:
                found = i
                break
        if found is None:
            sys.exit("%s not found in %s" % (old, routine))
        # references outside the routine
        if new.startswith("."):
            for i, ln in enumerate(lines):
                if (i < start or i >= end) and word.search(ln.split(";")[0]):
                    sys.exit("%s is used outside %s (line %d): needs a "
                             "global name" % (old, routine, i + 1))
            if new.lower() in used:
                sys.exit("%s is already used in %s" % (new, routine))
            used.add(new.lower())
        addr = "$%06X" % int(old.split("_")[1], 16)
        text = "%s  %s" % (addr, note) if note else addr
        if new.startswith("."):
            lines[found] = "%s:\t\t\t\t; %s" % (new, text)
            refs = range(start, end)
        else:
            m = LABEL.match(lines[found])
            lines[found] = "%s:%s" % (new, m.group(2))
            lines.insert(found, "; %s" % text)
            end += 1
            refs = range(len(lines))
        for i in refs:
            if i == found:
                continue
            code, sep, comment = lines[i].partition(";")
            if word.search(code):
                lines[i] = word.sub(new, code) + sep + comment
    return lines


def rename(lines, old, new, note):
    word = re.compile(r"\b%s\b" % re.escape(old))
    if any(re.match(r"^%s:" % re.escape(new), ln, re.I) for ln in lines):
        sys.exit("%s already exists" % new)
    found = None
    for i, ln in enumerate(lines):
        if re.match(r"^%s(:|\tequ\t)" % re.escape(old), ln):
            found = i
    if found is None:
        sys.exit("%s not found" % old)
    lines = [word.sub(new, ln) for ln in lines]
    if note and found > 0 and lines[found - 1].startswith("; $"):
        addr = lines[found - 1].split()[1]
        lines[found - 1] = "; %s  %s" % (addr, note)
    return lines


def set_note(path, lines, name, note):
    for i, ln in enumerate(lines):
        if re.match(r"^%s\tequ\t" % re.escape(name), ln):
            lines[i] = ln.split("\t;")[0] + "\t; " + note
            return lines
        if re.match(r"^%s:" % re.escape(name), ln):
            if i > 0 and lines[i - 1].startswith("; $"):
                addr = lines[i - 1].split()[1]
                lines[i - 1] = "; %s  %s" % (addr, note)
            else:
                a = line_addresses(path, lines)[i]
                lines[i:i] = ["; $%06X  %s" % (a, note)]
            return lines
    sys.exit("%s not found" % name)


RAM_EQU = re.compile(r"^(\w+)\tequ\t\$FFFF([0-9A-F]{4})\b")


def ram_equate(lines, addr, name, note):
    a = int(addr.lstrip("$"), 16) & 0xFFFFFF
    if not 0xFF0000 <= a <= 0xFFFFFF:
        sys.exit("%s isn't RAM" % addr)
    lo = a & 0xFFFF
    at = None
    last = None
    for i, ln in enumerate(lines):
        m = RAM_EQU.match(ln)
        if not m:
            continue
        if m.group(1).lower() == name.lower():
            sys.exit("%s already exists" % name)
        v = int(m.group(2), 16)
        if v == lo:
            sys.exit("$%06X already has a name (%s)" % (a, m.group(1)))
        last = i
        if at is None and v > lo:
            at = i
    if at is None:
        at = last + 1
    text = "%s\tequ\t$FFFF%04X" % (name, lo)
    if note:
        text += "\t; " + note
    lines.insert(at, text)
    pats = [(re.compile(r"\(\$FFFF%04X\)\.w" % lo, re.I), "(%s).w" % name),
            (re.compile(r"\(\$%06X\)\.l" % a, re.I),
             "(%s&$FFFFFF).l" % name),
            (re.compile(r"#\$%06X\b" % a, re.I), "#%s&$FFFFFF" % name)]
    for i, ln in enumerate(lines):
        if i == at:
            continue
        code, sep, comment = ln.partition(";")
        for pat, rep in pats:
            code = pat.sub(rep, code)
        lines[i] = code + sep + comment
    return lines


def line_addresses(path, lines):
    """Each line's address, from the in-tree assembler's two passes."""
    import os
    import tempfile
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hzasm_check
    with tempfile.NamedTemporaryFile("w", suffix=".asm",
                                     dir=os.path.dirname(os.path.abspath(path)),
                                     delete=False) as f:
        f.write("\n".join(lines))
        tmp = f.name
    try:
        sym = {"__lax__": True}
        hzasm_check.assemble(tmp, sym, True)
        del sym["__lax__"]
        addrs = []
        hzasm_check.assemble(tmp, sym, False, addrs)
    finally:
        os.unlink(tmp)
    return addrs


def data_label(path, lines, addr, name, note):
    a = int(addr.lstrip("$"), 16)
    if any(re.match(r"^%s:" % re.escape(name), ln, re.I) for ln in lines):
        sys.exit("%s already exists" % name)
    addrs = line_addresses(path, lines)
    hit = None
    for i, ln in enumerate(lines):
        if not ln.lstrip().startswith("dc.b"):
            continue
        start = addrs[i]
        end = addrs[i + 1] if i + 1 < len(addrs) else start
        if start <= a < end:
            hit = i
            break
    if hit is None:
        sys.exit("no dc.b line holds $%06X" % a)
    head, _, rest = lines[hit].partition("dc.b")
    code, sep, comment = rest.partition(";")
    items = [it.strip() for it in code.split(",")]
    if any(it.startswith('"') for it in items):
        sys.exit("$%06X: the dc.b line has a string" % a)
    k = a - addrs[hit]
    label = ["", "; $%06X  %s" % (a, note) if note else "; $%06X" % a,
             "%s:" % name]
    new = []
    if k > 0:
        new.append("\tdc.b\t" + ",".join(items[:k]))
    new += label
    new.append("\tdc.b\t" + ",".join(items[k:]))
    lines[hit:hit + 1] = new
    pats = [(re.compile(r"#\$%X\b" % a), "#%s" % name),
            (re.compile(r"#\$0*%X\b" % a), "#%s" % name),
            (re.compile(r"\(\$0*%X\)\.l" % a), "(%s).l" % name)]
    for i, ln in enumerate(lines):
        code, sep, comment = ln.partition(";")
        for pat, rep in pats:
            code = pat.sub(rep, code)
        lines[i] = code + sep + comment
    return lines


def check_scopes(lines):
    """Unnamed loc_ labels after a routine's first local: each starts a new
    AS scope, so the locals after it would belong to it."""
    out = []
    seen_local = False
    for ln in lines:
        m = LABEL.match(ln)
        if not m:
            continue
        lab = m.group(1)
        if lab.startswith("."):
            seen_local = True
        elif lab.startswith("loc_"):
            if seen_local:
                out.append(lab)
        else:
            seen_local = False
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    lines = open(argv[1]).read().split("\n")
    routine = None
    entries = []
    header = None
    for raw in open(argv[2]):
        ln = raw.rstrip("\n")
        if not ln.strip() or ln.lstrip().startswith("#"):
            continue
        body, _, note = ln.partition(";")
        note = note.strip()
        parts = body.split()
        if parts and parts[0] in ("rename", "ram", "data", "note"):
            if routine is not None:
                lines = apply(lines, routine, entries, header)
                routine, entries, header = None, [], None
            if parts[0] == "note":
                if len(parts) != 2 or not note:
                    sys.exit("bad line: %s" % ln)
                lines = set_note(argv[1], lines, parts[1], note)
                continue
            if len(parts) != 3:
                sys.exit("bad line: %s" % ln)
            if parts[0] == "rename":
                lines = rename(lines, parts[1], parts[2], note)
            elif parts[0] == "ram":
                lines = ram_equate(lines, parts[1], parts[2], note)
            else:
                lines = data_label(argv[1], lines, parts[1], parts[2], note)
            continue
        if parts and parts[0] == "=":
            header = note
            continue
        if len(parts) == 1:
            if routine is not None:
                lines = apply(lines, routine, entries, header)
            routine, entries, header = parts[0], [], None
            continue
        if len(parts) != 2:
            sys.exit("bad line: %s" % ln)
        entries.append((parts[0], parts[1], note))
    if routine is not None:
        lines = apply(lines, routine, entries, header)
    open(argv[1], "w").write("\n".join(lines))
    left = check_scopes(lines)
    if left:
        print("warning: unnamed labels split routines with locals:")
        for ln in left[:20]:
            print("   ", ln)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
