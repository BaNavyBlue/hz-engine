#!/usr/bin/env python3
"""The disassembly as a clickable tree: one HTML page with its data inside
it, for any static web host (no server, no build step on the host).

  hz_tree.py ASM OUT.html [--stats]

Every global label in ASM is a node: a 68k routine, a Z80 routine (the sound
driver, between its phase and dephase), a pointer table, or data. A routine's
children, in the order the source first names them:

  call    jsr/bsr; Z80 call
  jump    bra/jmp/bcc into another label; Z80 jp/jr/djnz
  falls   no rts/bra/jmp at its end: it runs on into the next label
  table   a pointer table it dispatches through (movea.l #Table / lea)
  addr    a code address it hands on (#Label: installed handlers)

and a table's children are its entries (entry). Data it names (data) and the
RAM equates it touches go to the side panel, not the tree. The tree starts at
Vectors, the 68000's vector table; routines that nothing reaches from there
(entered through RAM pointers or by the Z80's interrupt) hang off a second
root, and after them the code nothing calls at all (a note that begins
"never called"). Descriptions come from each label's `; $ADDR  note` header line.

The page itself is tools/hz_tree.html; @DATA@ in it becomes the JSON.
"""

import json
import os
import re
import sys
from collections import deque

LABEL = re.compile(r"^([A-Za-z_]\w*):(.*)$")
EQU = re.compile(r"^([A-Za-z_]\w*)\s+equ\s+(.+)$", re.I)
HEADER = re.compile(r"^;\s*\$([0-9A-Fa-f]+)\s+(.*)$")
RULE = re.compile(r"^;\s*[=-]{8,}\s*$")
IDENT = re.compile(r"(?<![\w$.'])[A-Za-z_]\w*")
STRING = re.compile(r"\"[^\"]*\"|'[^']*'")

CC = "eq ne hi ls cc cs pl mi ge lt gt le vc vs hs lo".split()
CALL68 = {"jsr", "bsr"}
JUMP68 = {"jmp", "bra"} | {"b" + c for c in CC} | {"db" + c for c in CC + ["f", "ra", "t"]}
END68 = {"rts", "rte", "rtr", "jmp", "bra"}
CALLZ = {"call"}
JUMPZ = {"jp", "jr", "djnz"}
DATA = {"dc", "ds", "dcb", "binclude", "incbin", "db", "dw", "defb", "defw", "defs"}
DIRECTIVES = DATA | {"save", "restore", "cpu", "phase", "dephase", "padding",
                     "supmode", "org", "align", "even", "cnop", "include",
                     "if", "else", "endif", "rept", "endr", "listing", "end"}

# strongest first: a target named twice keeps the stronger kind
RANK = {k: i for i, k in enumerate(["call", "jump", "falls", "table", "entry", "addr", "data"])}
TREE = {"call", "jump", "falls", "table", "entry", "addr"}

DATA_LINES = 40  # a data label's source is cut after this many lines


def split_comment(line):
    """(code, comment) at the first ; outside quotes. The Z80's af' is no quote."""
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c == ";":
            return line[:i], line[i + 1:].strip()
        elif c in "\"'" and not (c == "'" and line[max(0, i - 2):i].lower() == "af"):
            q = c
    return line, ""


def parse(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    labels, equ = [], {}
    cur, z80 = None, False
    for i, ln in enumerate(lines):
        m = LABEL.match(ln)
        if m:
            cur = {"name": m.group(1), "line": i, "z80": z80, "ins": [], "data": [], "last": None}
            labels.append(cur)
            continue
        code, com = split_comment(ln)
        s = code.strip()
        if not s:
            continue
        if not ln[0].isspace():
            e = EQU.match(code.rstrip())
            if e:
                equ[e.group(1)] = [e.group(2).strip(), com]
            continue
        parts = s.split(None, 1)
        base = parts[0].lower().split(".")[0]
        ops = parts[1] if len(parts) > 1 else ""
        if base == "phase":
            z80 = True
        elif base == "dephase":
            z80 = False
        if cur is None or base not in DATA and base in DIRECTIVES:
            continue
        if base in DATA:
            cur["data"].append(ops)
            cur["last"] = None
        else:
            cur["ins"].append((base, ops))
            cur["last"] = (base, ops)
    return lines, labels, equ


def header(lines, at):
    """A label's comment block above it: (start line, address, note, text)."""
    start = at
    while start > 0 and lines[start - 1].lstrip().startswith(";"):
        start -= 1
    block = [ln.strip() for ln in lines[start:at]]
    hit = max((i for i, ln in enumerate(block) if HEADER.match(ln)), default=None)
    clean = lambda ls: " ".join(ln.lstrip(";").strip() for ln in ls if not RULE.match(ln)).strip()
    if hit is None:
        return start, None, "", clean(block)
    h = HEADER.match(block[hit])
    text = " ".join([h.group(2).strip()] + [ln.lstrip(";").strip() for ln in block[hit + 1:]])
    return start, int(h.group(1), 16), clean(block[:hit]), text.strip()


def z80_words(data):
    """db lines as words, low byte first; [] if anything isn't a number."""
    nums = []
    for ops in data:
        for tok in ops.split(","):
            m = re.fullmatch(r"\s*([0-9][0-9A-Fa-f]*)h\s*|\s*([0-9]+)\s*", tok)
            if not m:
                return []
            nums.append(int(m.group(1), 16) if m.group(1) else int(m.group(2)))
    if len(nums) % 2 or any(n > 255 for n in nums):
        return []
    return [nums[i] | nums[i + 1] << 8 for i in range(0, len(nums), 2)]


def ends(lab):
    """Does the label's last line stop it running on into the next one?"""
    if lab["last"] is None:
        return bool(lab["data"])
    base, ops = lab["last"]
    if lab["z80"]:
        if base in ("ret", "reti", "retn"):
            return base != "ret" or not ops.strip()
        return base in ("jp", "jr") and "," not in ops
    return base in END68


def build(path):
    lines, labels, equ = parse(path)
    names = {lab["name"] for lab in labels}
    by = {lab["name"]: lab for lab in labels}

    for lab in labels:
        if lab["ins"]:
            lab["kind"] = "z80" if lab["z80"] else "code"
        elif lab["data"]:
            lab["kind"] = "data"
        else:
            lab["kind"] = "empty"
        lab["start"], lab["addr"], lab["note"], lab["text"] = header(lines, lab["line"])

    for lab in labels:
        refs, used = [], []
        calls, jumps = (CALLZ, JUMPZ) if lab["z80"] else (CALL68, JUMP68)
        for base, ops in lab["ins"]:
            ids = IDENT.findall(STRING.sub("", ops))
            used += [t for t in ids if t in equ]
            ids = [t for t in ids if t in names and t != lab["name"]]
            if ids and base in calls:
                refs.append((ids[0], "call"))
                ids = ids[1:]
            elif ids and base in jumps:
                refs.append((ids[0], "jump"))
                ids = ids[1:]
            refs += [(t, "ref") for t in ids]
        for ops in lab["data"]:
            ids = IDENT.findall(STRING.sub("", ops))
            used += [t for t in ids if t in equ]
            refs += [(t, "item") for t in ids if t in names and t != lab["name"]]
        lab["refs"], lab["used"] = refs, list(dict.fromkeys(used))

    # the driver's jump tables are db bytes: little-endian Z80 addresses
    zaddr = {lab["addr"]: lab["name"] for lab in labels
             if lab["kind"] == "z80" and lab["addr"] is not None}
    for lab in labels:
        if lab["z80"] and lab["kind"] == "data":
            words = z80_words(lab["data"])
            if len(words) >= 2 and all(w in zaddr for w in words):
                lab["refs"] += [(zaddr[w], "item") for w in words]

    code = {"code", "z80", "empty"}
    for lab in labels:
        if lab["kind"] == "data" and any(k == "item" and by[t]["kind"] in code for t, k in lab["refs"]):
            lab["kind"] = "table"

    for n, lab in enumerate(labels):
        kids = {}
        for t, k in lab["refs"]:
            tk = by[t]["kind"]
            if k == "ref":
                k = "table" if tk == "table" else "addr" if tk in code else "data"
            elif k == "item":
                k = "entry" if lab["kind"] == "table" and tk in code else "data"
            if t not in kids or RANK[k] < RANK[kids[t]]:
                kids[t] = k
        nxt = labels[n + 1] if n + 1 < len(labels) else None
        if lab["kind"] in code and not ends(lab) and nxt and nxt["kind"] in code:
            kids.setdefault(nxt["name"], "falls")
        lab["kids"] = kids

    # sizes from the next label's address, where both have one
    for n, lab in enumerate(labels):
        nxt = labels[n + 1] if n + 1 < len(labels) else None
        lab["size"] = None
        if lab["addr"] is not None and nxt and nxt["addr"] is not None and nxt["z80"] == lab["z80"]:
            if nxt["addr"] > lab["addr"]:
                lab["size"] = nxt["addr"] - lab["addr"]

    return lines, labels, equ, by


def reach(by, roots):
    seen, todo = set(roots), deque(roots)
    while todo:
        for t, k in by[todo.popleft()]["kids"].items():
            if k in TREE and t not in seen:
                seen.add(t)
                todo.append(t)
    return seen


def entry_points(labels, by, seen):
    """Unreached routines that no unreached routine leads to: the ways in
    the traversal can't see (RAM pointers, the Z80's interrupt), and the
    routines nothing calls at all (their notes begin "never called")."""
    rest = [lab for lab in labels if lab["kind"] in ("code", "z80") and lab["name"] not in seen]
    inner = {t for lab in rest for t, k in lab["kids"].items() if k in TREE}
    tops = [lab for lab in rest if lab["name"] not in inner]
    never = [lab["name"] for lab in tops if lab["text"].startswith("never called")]
    return [lab["name"] for lab in tops if lab["name"] not in never], never


def source(lines, labels):
    out = {}
    for n, lab in enumerate(labels):
        end = labels[n + 1]["start"] if n + 1 < len(labels) else len(lines)
        body = lines[lab["start"]:end]
        while body and not body[-1].strip():
            body.pop()
        if lab["kind"] in ("data", "table") and len(body) > DATA_LINES + 10:
            body = body[:DATA_LINES] + [f"; ... {len(body) - DATA_LINES} more lines"]
        out[lab["name"]] = "\n".join(body)
    return out


# The second view: (title, note, name stems). A label goes to the longest
# stem it starts with, followed by _, a capital, a digit or nothing (so M
# takes M_Init, not MainLoop_Title); every Z80 label goes to the driver.
SUBSYSTEMS = [
    ("Start-up and main loop",
     "Reset, the exception handlers, the checksum, the outer loop (title, set-up, map load, play), "
     "the frame loop and its VBlank and HBlank interrupts, pause and game over.",
     "Vectors Header EntryPoint Exc j Checksum Boot MainLoop Game Demo IntStubs HBlank VBlank IO "
     "ReadJoypads Pause Dev Debug DebugPrintHex LoadScreen GameOver Random"),
    ("Screen: VDP, sprites, palettes, text",
     "The video chip's set-up and DMA, the sprite list, palettes and fades, the HUD and text printing.",
     "VDP Sprites Screen Palette TextBuf Print NumToDec ByteToHex NibbleToHex HUD FX"),
    ("Map and terrain",
     "Loading and unpacking a map (LZW), its blocks and animated tiles, the object grid, "
     "terrain events and the outposts.",
     "Map MapFx TerrainEv TerrainEventHandlers LZW Grid Outpost"),
    ("Mechs, units and shots",
     "The two transforming mechs, the units they buy, objects, shots and hits, directions and distances.",
     "Mech Mech2 Mechs Unit Units Shot Shots Combat Obj GetDirection"),
    ("Order menu and radar",
     "The menu a mech opens at a base to buy and order units, and the radar map.",
     "OrderMenu Radar"),
    ("Computer mech AI",
     "The computer player's mech: its think layers, strategies, routes, buying and fuel sums.",
     "AI"),
    ("Unit AI ($5D000 module)",
     "Each unit's handler and orders, in the module at $5D000 that calls the main code through the "
     "entry table at $300.",
     "UnitAI"),
    ("Menus and endings ($53000 module)",
     "The title, options, password, map select, music and sound tests, the winner screens and the "
     "endings: a module with its own code, data and RAM.",
     "MenuModule M Menu Title GameEnd Ending Staff Winner Results WinRecord Options OptionsMenu "
     "OptionsP1 OptionsP2 OptionsWindow Password MusicTest SoundTest MapSelect TypeSelect"),
    ("Sound: the 68000's side",
     "Loading the Z80 driver and sending it commands through its mailbox.",
     "Snd Z80"),
    ("Sound: the Z80 driver",
     "The sound CPU's own program: commands from the 68000, music and effect tracks, the YM2612's FM "
     "channels, the PSG, the DAC and the LFO.",
     ""),
]
FAMILY_MIN = 3    # a family of names gets its own node from this many
FAMILY_FROM = 30  # in subsystems bigger than this


def subsystem(lab):
    if lab["z80"]:
        return len(SUBSYSTEMS) - 1
    name, best, at = lab["name"], -1, None
    for i, (_, _, stems) in enumerate(SUBSYSTEMS):
        for s in stems.split():
            nxt = name[len(s):len(s) + 1]
            if name.startswith(s) and (not nxt or not nxt.islower()) and len(s) > best:
                best, at = len(s), i
    return at


def family(name, heads):
    """AI_StratGoHome -> AI_Strat when AI_ is a big family, else AI."""
    if "_" not in name:
        return None
    head, rest = name.split("_", 1)
    if heads.get(head, 0) > FAMILY_FROM:
        m = re.match(r"[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[A-Za-z0-9]+", rest)
        return head + "_" + m.group(0)
    return head


def systems(labels):
    """The second view's nodes: @systems -> @sysN -> @sysN/Family -> labels."""
    s = {"@systems": {"t": "Herzog Zwei", "k": [],
                      "d": "The ROM's routines and pointer tables grouped by what they work on. "
                           "Big groups are split by name: AI_Strat* together, Track_* together."}}
    lost = []
    members = [[] for _ in SUBSYSTEMS]
    for lab in labels:
        if lab["kind"] in ("code", "z80", "table"):
            i = subsystem(lab)
            if i is None:
                lost.append(lab["name"])
            else:
                members[i].append(lab)
    for i, ((title, note, _), labs) in enumerate(zip(SUBSYSTEMS, members)):
        gid = f"@sys{i}"
        s["@systems"]["k"].append([gid, "group"])
        kids = []
        if len(labs) > FAMILY_FROM:
            heads = {}
            for lab in labs:
                h = lab["name"].split("_", 1)[0] if "_" in lab["name"] else None
                heads[h] = heads.get(h, 0) + 1
            fams = {}
            for lab in labs:
                fams.setdefault(family(lab["name"], heads), []).append(lab["name"])
            done = set()
            for lab in labs:
                f = family(lab["name"], heads)
                if f is None or len(fams[f]) < FAMILY_MIN:
                    kids.append([lab["name"], "member"])
                elif f not in done:
                    done.add(f)
                    fid = f"{gid}/{f}"
                    s[fid] = {"t": f + "_*" if f.count("_") == 0 else f + "*",
                              "d": f"{len(fams[f])} labels named {f}...",
                              "k": [[n, "member"] for n in fams[f]]}
                    kids.append([fid, "group"])
        else:
            kids = [[lab["name"], "member"] for lab in labs]
        s[gid] = {"t": title, "d": note, "k": kids, "n": len(labs)}
    return s, lost


def page_data(lines, labels, equ, by):
    seen = reach(by, ["Vectors"])
    extra, never = entry_points(labels, by, seen)
    src = source(lines, labels)
    used = set()
    r = {}
    for lab in labels:
        rec = {"k": lab["kind"], "d": lab["text"],
               "c": [[t, k] for t, k in lab["kids"].items()]}
        if lab["z80"]:
            rec["z"] = 1
        if lab["addr"] is not None:
            rec["a"] = lab["addr"]
        if lab["size"]:
            rec["n"] = lab["size"]
        if lab["note"]:
            rec["p"] = lab["note"]
        if lab["used"]:
            rec["u"] = lab["used"]
            used.update(lab["used"])
        r[lab["name"]] = rec
    # equates named in the source shown, for the hover notes
    names = set()
    for text in src.values():
        names.update(IDENT.findall(text))
    s, lost = systems(labels)
    s["@power"] = {
        "t": "Power on", "k": [["Vectors", "root"]] + [[n, "other"] for n in extra]
                              + [[n, "never"] for n in never],
        "d": "The 68000 starts at the reset vector in Vectors. Everything under Vectors is reached "
             "from there by calls, jumps and pointer tables. The rest are the ways in that the "
             "disassembly can't follow: the Z80's interrupt, and entry-table slots nothing uses; "
             "and last, code that nothing calls at all.",
    }
    data = {"R": r, "S": s, "src": src,
            "E": {k: v for k, v in equ.items() if k in names}}
    return data, seen, extra, never, lost


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        sys.exit(__doc__)
    asm, out = args
    lines, labels, equ, by = build(asm)
    data, seen, extra, never, lost = page_data(lines, labels, equ, by)

    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "hz_tree.html"), encoding="utf-8") as f:
        page = f.read()
    blob = json.dumps(data, separators=(",", ":"), ensure_ascii=False).replace("</", "<\\/")
    with open(out, "w", encoding="utf-8") as f:
        f.write(page.replace("@DATA@", blob))
    print(f"{out}: {len(labels)} labels, {len(seen)} reached from reset, "
          f"{len(extra)} other ways in, {len(never)} never called, "
          f"{os.path.getsize(out) // 1024} KB")
    if lost:
        print("not in any subsystem:", " ".join(lost), file=sys.stderr)

    if "--stats" in sys.argv:
        kinds = {}
        for lab in labels:
            kinds[lab["kind"]] = kinds.get(lab["kind"], 0) + 1
        edges = {}
        for lab in labels:
            for k in lab["kids"].values():
                edges[k] = edges.get(k, 0) + 1
        print("labels", len(labels), kinds, file=sys.stderr)
        print("edges", edges, file=sys.stderr)
        print("reached from Vectors", len(seen), "entry points besides", len(extra), extra, file=sys.stderr)
        print("never called", never, file=sys.stderr)
        print("unreached code", sum(1 for lab in labels if lab["kind"] in ("code", "z80")
                                    and lab["name"] not in reach(by, ["Vectors"] + extra + never)),
              file=sys.stderr)
        print("no header", [lab["name"] for lab in labels if lab["addr"] is None][:40], file=sys.stderr)
        for k, v in data["S"].items():
            print(" ", k, v["t"], len(v["k"]), file=sys.stderr)


if __name__ == "__main__":
    main()
