#!/usr/bin/env python3
"""(Session 78) The game's extractor in C (src/extract/, ./hz_assets)
against tools/hz_extract.py, its reference.

    python3 tools/hzassets_check.py C_DIR [ASSETS] [./hz_gameid]

C_DIR is what ./hz_assets wrote from the cartridge; ASSETS (default
assets) the files hz_extract.py made from it, as checked in. First the
port's own files, which no extraction makes (KEPT), are copied from
ASSETS into C_DIR, as a tree without the game's files has them. Then every
file of ASSETS must be in C_DIR and the same: text byte for byte, a PNG
the same picture (mode, size, palette, transparency, pixels: its bytes
are another deflate's) -- and C_DIR must have nothing more. With
./hz_gameid, the two folders' network game ids too (hz_file.h: the
loader's files, a PNG by its picture), which must agree for a player's
extraction to play online with this tree's. Exit 0 when all agree. `make
assets_check ROM=...` runs it. Needs Pillow (this is the tree's check, not
the player's: the game itself needs no Python).
"""
import os
import shutil
import subprocess
import sys

from PIL import Image

# The port's own files under assets/: no extraction makes them, and a tree
# without the game's files ships them as they are. path_hints.png is
# hz_extract.py's drawing of 16 arrows for Tiled, nothing of the
# cartridge's.
KEPT = ['README.md', 'data/turrets.json', 'data/repair.json',
        'sound/q_drums.json', 'maps/path_hints.png']


def files(root):
    out = set()
    for d, _, names in os.walk(root):
        for n in names:
            out.add(os.path.relpath(os.path.join(d, n), root)
                    .replace(os.sep, '/'))
    return out


def picture(path):
    with Image.open(path) as im:
        return (im.mode, im.size, im.getpalette(),
                im.info.get('transparency'), im.tobytes())


def main():
    args = sys.argv[1:]
    if not 1 <= len(args) <= 3:
        print(__doc__)
        return 2
    made = args[0]
    ref = args[1] if len(args) > 1 else 'assets'
    gameid = args[2] if len(args) > 2 else None
    for rel in KEPT:
        dest = os.path.join(made, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(os.path.join(ref, rel), dest)

    have, want = files(made), files(ref)
    bad = 0
    same = 0
    for rel in sorted(want):
        a, b = os.path.join(made, rel), os.path.join(ref, rel)
        if rel not in have:
            print('MISSING  %s' % rel)
            bad += 1
            continue
        if rel.endswith('.png'):
            ok = picture(a) == picture(b)
        else:
            ok = open(a, 'rb').read() == open(b, 'rb').read()
        if ok:
            same += 1
        else:
            print('DIFFERS  %s' % rel)
            bad += 1
    for rel in sorted(have - want):
        print('EXTRA    %s' % rel)
        bad += 1
    print('files: %d of %d the same (%d the port\'s own, copied)'
          % (same, len(want), len(KEPT)))

    if gameid is not None:
        out = subprocess.run([gameid, made, ref], capture_output=True,
                             text=True)
        ids = [ln.split()[0] for ln in out.stdout.splitlines() if ln.strip()]
        if out.returncode != 0 or len(ids) != 2:
            print('hz_gameid failed: %s' % (out.stderr.strip() or ids))
            bad += 1
        elif ids[0] != ids[1]:
            print('game ids differ: %s (C) and %s (assets)' % tuple(ids))
            bad += 1
        else:
            print('game id: %s, the same' % ids[0])
    print('assets_check: %s' % ('all the same' if not bad
                                else '%d differ' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
