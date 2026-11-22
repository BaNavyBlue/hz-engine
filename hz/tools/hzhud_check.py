#!/usr/bin/env python3
"""Checks tools/hz_hud's log against a model of the HUD gauge routines,
written from the 68k code, not from the C:

  HUD_UpdateGauges ($2F26)  per mech: HP / 8 (0 destroyed), its HQ's HP /
                            32, ammo / 4, fuel / 64; each compared with its
                            shown byte as a byte (cmp.b), and on a change
                            the low byte stored and the bar queued with the
                            whole word. P2's HQ bar is queued by bra.w, so
                            when it is, P2's ammo and fuel aren't looked at
  HUD_QueueBar ($8684)      the first entry whose count word is 0; the
                            value clamped to the max; then a tile per cell
  HUD_FlushBars ($8736)     each entry with a count: its command, count + 1
                            words, the count cleared

The cells are modelled cell by cell rather than as the 68k's four loops:
for cell i, v = what of the value falls in it (0-4) and m = what of the
max does (0-4); m 0 is blank ($A5BE), m 4 is $A5B0 + v, and m 1-3 is
GaugeTiles[v][m - v] (read from the ROM). Following the loops through
(.full_cells, .value_ends, .max_goes_on, .empty_cells, .max_ends,
.blank_rest) gives the same tile in every case, and the routine writes
exactly count + 1 of them.

  hzhud_check.py ROM LOG
"""

import sys

GAUGE_TILES = 0x8716
EMPTY, NONE = 0xA5B0, 0xA5BE

# HUD_UpdateGauges' VDP commands, per player: HP, HQ, ammo, fuel (the
# move.l #...,d0 at $2F4E, $2F6C, $2F8C, $2FAA; $2FDC, $2FFA, $301A, $3038).
CMDS = ((0x71040003, 0x70840003, 0x70980003, 0x71180003),
        (0x712C0003, 0x70AC0003, 0x70C00003, 0x71400003))


def w16(b, o):
    return (b[o] << 8) | b[o + 1]


class Model:
    def __init__(self, rom):
        self.tiles = [[w16(rom, GAUGE_TILES + (v * 4 + c) * 2)
                       for c in range(4)] for v in range(4)]

    def cells(self, value, mx, last):
        value = min(value, mx)
        out = []
        for i in range(last + 1):
            v = max(0, min(4, value - 4 * i))
            m = max(0, min(4, mx - 4 * i))
            if m == 0:
                out.append(NONE)
            elif m == 4:
                out.append(EMPTY + v)
            else:
                out.append(self.tiles[v][m - v])
        return out

    def update_gauges(self, shown, inputs):
        """HUD_UpdateGauges into an empty queue (8 bars at most: never
        full). shown: the 8 shown bytes (P1's HP, HQ, ammo, fuel, then
        P2's); inputs: per player (flags, hp, hq_hp, ammo, fuel). Returns
        the shown bytes after and {bar index: cells} for the bars
        queued."""
        shown = list(shown)
        queued = {}
        for p in (0, 1):
            flags, hp, hq, ammo, fuel = inputs[p]
            values = (0 if flags & 0x20 else hp >> 3, hq >> 5, ammo >> 2,
                      fuel >> 6)
            for k in range(4):
                if values[k] & 0xFF == shown[p * 4 + k]:
                    continue
                shown[p * 4 + k] = values[k] & 0xFF
                mx, last = (0x20, 7) if k < 2 else (0x18, 5)
                queued[p * 4 + k] = self.cells(values[k], mx, last)
                if p == 1 and k == 1:
                    return shown, queued    # bra.w HUD_QueueBar: its rts
        return shown, queued

    def queue(self, q, value, mx, last, cmd):
        """q: list of 16 [last, cmd, cells]; returns the index used."""
        for i in range(16):
            if q[i][0] == 0:
                cells = self.cells(value, mx, last)
                q[i][0] = last
                q[i][1] = cmd
                q[i][2] = cells + q[i][2][len(cells):]
                return i
        return -1


def hexes(words):
    return [int(w, 16) for w in words]


def parse_bar(words):
    v = hexes(words)
    return [v[0], v[1], v[2:18]]


def check(rom, path):
    model = Model(rom)
    with open(path) as f:
        lines = [ln.split() for ln in f if ln.strip()]
    if not lines or lines[0] != ["HZH1"]:
        return ["not an hz_hud log"], {}
    errors = []
    counts = {"queue": 0, "update": 0, "queued": 0, "dropped": 0,
              "p2_cut": 0, "flush": 0, "words": 0}
    i = 1

    # Q: HUD_QueueBar into an empty queue.
    while i < len(lines) and lines[i][0] == "Q":
        ln = lines[i]
        i += 1
        value, mx, last, cmd = hexes(ln[1:5])
        index = int(ln[6])
        got = parse_bar(ln[7:])
        q = [[0, 0, [0xDEAD] * 16]] + [[0, 0, [0] * 16] for _ in range(15)]
        model.queue(q, value, mx, last, cmd)
        want_index = 0 if last != 0 else -1
        counts["queue"] += 1
        if index != want_index or got != q[0]:
            errors.append("Q %X %X %X: got %d %s, model %d %s"
                          % (value, mx, last, index, got, want_index, q[0]))

    # U: HUD_UpdateGauges.
    while i < len(lines) and lines[i][0] == "U":
        n, busy = hexes(lines[i][1:3])
        s = [hexes(lines[i + 1 + p][2:]) for p in (0, 1)]
        t = [hexes(lines[i + 3 + p][2:]) for p in (0, 1)]
        i += 5
        got = {}
        while i < len(lines) and lines[i][0] == "E":
            got[int(lines[i][1])] = parse_bar(lines[i][2:])
            i += 1
        q = [[1 if busy >> k & 1 else 0, 0, [0xDEAD] * 16]
             for k in range(16)]
        st = [list(x) for x in s]
        counts["update"] += 1
        for p in (0, 1):
            flags, hp, hq, ammo, fuel = st[p][:5]
            gauges = ((0 if flags & 0x20 else hp >> 3, 0x20, 7, 5, 0),
                      (hq >> 5, 0x20, 7, 6, 1),
                      (ammo >> 2, 0x18, 5, 7, 2),
                      (fuel >> 6, 0x18, 5, 8, 3))
            cut = False
            for value, mx, last, field, k in gauges:
                if value & 0xFF == st[p][field]:
                    continue
                st[p][field] = value & 0xFF
                idx = model.queue(q, value, mx, last, CMDS[p][k])
                counts["queued" if idx >= 0 else "dropped"] += 1
                if p == 1 and k == 1:
                    cut = True              # bra.w HUD_QueueBar: rts
                    counts["p2_cut"] += 1
                    break
            if cut:
                break
        if st != t:
            errors.append("U %X: state after %s, model %s" % (n, t, st))
        want = {k: q[k] for k in range(16) if not busy >> k & 1 and q[k][0]}
        if got != want:
            errors.append("U %X: entries %s, model %s" % (n, got, want))
        if len(errors) > 20:
            return errors, counts

    # F: HUD_FlushBars.
    while i < len(lines) and lines[i][0] == "F":
        n = int(lines[i][1], 16)
        bars = [parse_bar(lines[i + 1 + k][2:]) for k in range(16)]
        i += 17
        writes = {}
        while i < len(lines) and lines[i][0] == "W":
            a, w = hexes(lines[i][1:3])
            writes[a] = w
            i += 1
        lasts = hexes(lines[i][1:17])
        i += 1
        vram = {}
        for last, cmd, cells in bars:
            if last == 0:
                continue
            addr = ((cmd >> 16) & 0x3FFF) | ((cmd & 3) << 14)
            for k in range(last + 1):
                vram[(addr + 2 * k) & 0xFFFF] = cells[k]
        want = {a: w for a, w in vram.items() if w != (a ^ 0x5A5A)}
        counts["flush"] += 1
        counts["words"] += len(vram)
        if writes != want:
            bad = sorted(set(writes) ^ set(want)
                         | {a for a in writes if writes.get(a) != want.get(a)})
            errors.append("F %X: %d VRAM words differ, first at $%04X"
                          % (n, len(bad), bad[0]))
        if lasts != [0] * 16:
            errors.append("F %X: counts left %s" % (n, lasts))
        if len(errors) > 20:
            return errors, counts
    if i != len(lines):
        errors.append("line %d: unexpected %s" % (i + 1, lines[i][0]))
    return errors, counts


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    rom = open(argv[1], "rb").read()
    errors, c = check(rom, argv[2])
    if errors:
        print("%s: %d errors" % (argv[2], len(errors)))
        for e in errors[:10]:
            print("   ", e)
        return 1
    print("%s ok: %d HUD_QueueBar calls; %d HUD_UpdateGauges states, %d bars "
          "queued, %d dropped (queue full), %d cut short by P2's HQ bar; "
          "%d flushes, %d words" % (argv[2], c["queue"], c["update"],
                                    c["queued"], c["dropped"], c["p2_cut"],
                                    c["flush"], c["words"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
