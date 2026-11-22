#!/usr/bin/env python3
"""(Session 68) make atlas: the nightmare AI's terrain atlas
(src/game/nm_atlas.h, docs/nightmare.md "The atlas") made again.

  python3 tools/hzatlas.py ./hz_arena ASSETS [--salts N] [--frames N]
                           [--jobs N] [--out src/game/nm_atlas.c]

hz_arena --atlas on each map (N processes at once), with the default
patches and (session 71) with PATHS on as well; each side's blocks cut to
the ones it tried and written as src/game/nm_atlas.c. Exit 1 when a run
fails."""

import argparse
import concurrent.futures
import subprocess
import sys

MAPS = 8
# the default patches, and (session 71) those with PATHS (HZ_FIX_PATHS):
# its sides' names end in p
FIXES = ("37", "237")


def run_map(exe, assets, m, salts, frames, fixes):
    out = subprocess.run(
        [exe, assets, "--map", str(m), "--fixes", fixes,
         "--atlas", "%d,%d" % (salts, frames)],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        check=True).stdout.splitlines()
    sides = []
    i = 0
    while i < len(out):
        head = out[i].split()
        if not head or head[0] != "ATLAS":
            i += 1
            continue
        rows = out[i + 1:i + 65]
        if len(rows) != 64 or any(len(r) != 128 for r in rows):
            raise RuntimeError("map %d: a short atlas" % m)
        sides.append(([int(v) for v in head[1:]], rows))
        i += 65
    if len(sides) != 2:
        raise RuntimeError("map %d: %d sides" % (m, len(sides)))
    return sides


def crop(rows):
    """The box round the blocks tried: x0, y0, w, h and its rows."""
    cells = [(x, y) for y in range(64) for x in range(64)
             if rows[y][2 * x:2 * x + 2] != "00"]
    if not cells:
        return 0, 0, 0, 0, []
    x0 = min(x for x, _ in cells)
    x1 = max(x for x, _ in cells)
    y0 = min(y for _, y in cells)
    y1 = max(y for _, y in cells)
    return (x0, y0, x1 - x0 + 1, y1 - y0 + 1,
            [rows[y][2 * x0:2 * x1 + 2] for y in range(y0, y1 + 1)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("assets")
    ap.add_argument("--salts", type=int, default=3)
    ap.add_argument("--frames", type=int, default=7200)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--out", default="src/game/nm_atlas.c")
    a = ap.parse_args()
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        runs = list(pool.map(
            lambda mf: run_map(a.exe, a.assets, mf[0], a.salts, a.frames,
                               mf[1]),
            [(m, fx) for fx in FIXES for m in range(MAPS)]))
    lines = ["/* Made by `make atlas` (tools/hzatlas.py): don't edit. "
             "nm_atlas.h. */", "", '#include "nm_atlas.h"', ""]
    entries = []
    tried = good = 0
    for sides in runs:
        for (m, team, typ, salts, frames, fixes), rows in sides:
            x0, y0, w, h, box = crop(rows)
            name = "m%dt%d%s" % (m, team, "p" if fixes & 0x200 else "")
            lines.append("static const char *const %s[] = {" % name)
            for r in box:
                lines.append('    "%s",' % r)
            lines.append("};")
            entries.append("    { %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %s },"
                           % (m, team, typ, salts, frames, fixes, x0, y0, w,
                              h, name))
            for r in box:
                for x in range(0, len(r), 2):
                    v = int(r[x:x + 2], 16)
                    tried += v != 0
                    good += 0 < v < 255
    lines.append("")
    lines.append("const NmAtlas nm_atlas[] = {")
    lines += entries
    lines.append("};")
    lines.append("const int nm_atlas_count = %d;" % len(entries))
    with open(a.out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%s: %d sides, %d blocks tried, %d reach the HQ"
          % (a.out, len(entries), tried, good))
    return 0


if __name__ == "__main__":
    sys.exit(main())
