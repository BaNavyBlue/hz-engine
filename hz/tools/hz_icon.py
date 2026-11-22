#!/usr/bin/env python3
"""The game's Windows icon: a mech in jet form, from the game's own
sprite sheet and palette.

    hz_icon.py ASSETS OUT.ico [--frame 0x30] [--line 2] [--preview OUT.png]

The frame is one of assets/tiles/mech.png's 32 x 32 mech frames ($30-$39
the jet: $30 nose up, then clockwise in 22.5-degree steps to $34, nose
right; $32 the jet at 45 degrees). The line is the CRAM line its colours
come from: 2 the red team (P1), 3 the blue (P2), as assets/palettes/
game.png holds lines 1-3. Index 0 is clear. The sizes Windows asks for:
256, 128, 64 and 32 the frame at 8, 4, 2 and 1 times, every pixel kept;
48, 24 and 16, which no whole multiple reaches, the 256 one averaged down.
The jet is centred by the pixels it covers. --preview writes the sizes
side by side on a dark and a light ground, to look at.

src/platform/win/hz.ico is this with the defaults; the build links it into
hz.exe on Windows (src/platform/win/hz.rc), so the game builds without
Pillow. Run again after choosing another frame or line."""

import argparse
import io
import struct
import sys

from PIL import Image

CELL = 32
SIZES = (256, 128, 64, 48, 32, 24, 16)


def jet(assets, frame, line):
    mech = Image.open('%s/tiles/mech.png' % assets)
    pal = Image.open('%s/palettes/game.png' % assets).convert('RGB')
    colours = [pal.getpixel((i, line - 1)) for i in range(16)]
    x0, y0 = (frame % 8) * CELL, (frame // 8) * CELL
    cell = mech.crop((x0, y0, x0 + CELL, y0 + CELL))
    out = Image.new('RGBA', (CELL, CELL), (0, 0, 0, 0))
    for y in range(CELL):
        for x in range(CELL):
            v = cell.getpixel((x, y)) & 15
            if v:
                out.putpixel((x, y), colours[v] + (255,))
    # Centred by its pixels: the frame's own placement leans to a side.
    left, top, right, bottom = out.getbbox()
    centred = Image.new('RGBA', (CELL, CELL), (0, 0, 0, 0))
    centred.paste(out.crop((left, top, right, bottom)),
                  ((CELL - (right - left)) // 2, (CELL - (bottom - top)) // 2))
    return centred


def sizes(image):
    big = image.resize((256, 256), Image.NEAREST)
    made = {}
    for n in SIZES:
        if 256 % n == 0 and n >= CELL:
            made[n] = image.resize((n, n), Image.NEAREST)
        else:
            made[n] = big.resize((n, n), Image.BOX)
    return made


def write_ico(path, made):
    """Every size as a PNG inside the .ico (Windows Vista on reads
    them)."""
    pngs = []
    for n in SIZES:
        b = io.BytesIO()
        made[n].save(b, 'PNG', optimize=True)
        pngs.append((n, b.getvalue()))
    head = struct.pack('<HHH', 0, 1, len(pngs))
    offset = len(head) + 16 * len(pngs)
    entries = b''
    for n, data in pngs:
        entries += struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, 1, 32,
                               len(data), offset)
        offset += len(data)
    with open(path, 'wb') as f:
        f.write(head + entries + b''.join(d for _, d in pngs))


def preview(path, made):
    w = sum(SIZES) + 8 * (len(SIZES) + 1)
    sheet = Image.new('RGBA', (w, 2 * (256 + 16)), (0, 0, 0, 255))
    for row, ground in enumerate(((32, 32, 32, 255), (240, 240, 240, 255))):
        y = row * (256 + 16)
        sheet.paste(Image.new('RGBA', (w, 256 + 16), ground), (0, y))
        x = 8
        for n in SIZES:
            sheet.alpha_composite(made[n], (x, y + 8))
            x += n + 8
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('assets')
    ap.add_argument('out')
    ap.add_argument('--frame', type=lambda s: int(s, 0), default=0x30)
    ap.add_argument('--line', type=int, choices=(1, 2, 3), default=2)
    ap.add_argument('--preview')
    a = ap.parse_args()
    made = sizes(jet(a.assets, a.frame, a.line))
    write_ico(a.out, made)
    if a.preview:
        preview(a.preview, made)
    return 0


if __name__ == '__main__':
    sys.exit(main())
