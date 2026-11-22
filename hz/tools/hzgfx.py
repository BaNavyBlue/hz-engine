#!/usr/bin/env python3
"""Extracts every graphic identified so far in Herzog Zwei to PNGs.

    python3 tools/hzgfx.py hz.md OUTDIR

Each asset is drawn the way the code that uses it draws it; the routine is
named next to it. Needs Pillow, and hzlzw.py / hzrle.py beside this file.
"""
import os
import sys

from PIL import Image, ImageDraw

import hzlzw
from hzrle import rle_unpack


def w16(b, o):
    return (b[o] << 8) | b[o + 1]


def palette(b, o):
    """16 CRAM words -> RGB. $0BGR, 3 bits per channel."""
    out = []
    for i in range(16):
        c = w16(b, o + 2 * i)
        out.append(tuple(((c >> s) & 7) * 255 // 7 for s in (1, 5, 9)))
    return out


def put_tile(img, tb, t, px, py, pal, hf=False, vf=False, transparent=False):
    o = t * 32
    for y in range(8):
        row = tb[o + (7 - y if vf else y) * 4: o + (7 - y if vf else y) * 4 + 4]
        pix = [n for v in row for n in (v >> 4, v & 15)]
        if hf:
            pix.reverse()
        for x, c in enumerate(pix):
            if c or not transparent:
                img.putpixel((px + x, py + y), pal[c])


def tile_grid(tb, n, cols, pal, bg=(40, 0, 40)):
    """n tiles row-major, cols wide (a nametable of consecutive tiles)."""
    img = Image.new('RGB', (cols * 8, (n + cols - 1) // cols * 8), bg)
    for t in range(n):
        put_tile(img, tb, t, t % cols * 8, t // cols * 8, pal)
    return img


def sprite(img, tb, tile, w, h, x, y, pal, hf=False, vf=False):
    """VDP sprite: w x h tiles, column-major, flips mirror the whole sprite."""
    for tx in range(w):
        for ty in range(h):
            dx = w - 1 - tx if hf else tx
            dy = h - 1 - ty if vf else ty
            put_tile(img, tb, tile + tx * h + ty, x + dx * 8, y + dy * 8, pal,
                     hf, vf, transparent=True)


def lzw(rom, base, n):
    src, ln = hzlzw.table_entry(rom, base, n)
    return bytes(hzlzw.decompress(rom[src:src + ln], ln))


def save(img, outdir, name, scale=1):
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(os.path.join(outdir, name))
    print(name)


def battle_maps(rom, outdir):
    """Game_LoadMap loader + loc_5E02 drawer. Layout 64x64 blocks, block 4x4
    tiles, entry & $19FF, palette line 0 = $73F00 + map*$20 (Palette_FadeInMap)."""
    for n in range(8):
        tiles = lzw(rom, 0x22400, n)
        layout = lzw(rom, 0x35000, n)
        blocks = lzw(rom, 0x3F000, n)
        pal = palette(rom, 0x73F00 + n * 32)
        img = Image.new('RGB', (2048, 2048))
        for by in range(64):
            for bx in range(64):
                blk = (w16(layout, (by * 64 + bx) * 2) & 0x1FF) * 32
                for ty in range(4):
                    for tx in range(4):
                        e = w16(blocks, blk + ty * 8 + tx * 2) & 0x19FF
                        put_tile(img, tiles, e & 0x1FF, (bx * 4 + tx) * 8,
                                 (by * 4 + ty) * 8, pal, bool(e & 0x800),
                                 bool(e & 0x1000))
        save(img, outdir, 'map%d.png' % n)
        save(tile_grid(tiles, 512, 32, pal), outdir, 'map%d_tiles.png' % n, 2)
        # Radar_LoadPage: 64 tiles, 8 wide, one pixel per block
        radar = rom[0x74000 + n * 0x800: 0x74800 + n * 0x800]
        save(tile_grid(radar, 64, 8, pal), outdir, 'radar%d.png' % n, 4)


def mech_frames(rom, outdir):
    """$10000 + frame*$200, 4x4, DMA'd to VRAM $4000/$4200 (VBlank $5446)."""
    for name, pal_at in (('red', 0x20A20), ('blue', 0x20A40)):
        pal = palette(rom, pal_at)
        img = Image.new('RGB', (16 * 34, 4 * 34), (60, 90, 60))
        for f in range(64):
            sprite(img, rom[0x10000 + f * 0x200:], 0, 4, 4,
                   f % 16 * 34 + 1, f // 16 * 34 + 1, pal)
        save(img, outdir, 'mech_frames_%s.png' % name, 2)


def units(rom, outdir):
    """Direction table $244A (Unit_SetSprite); tiles from VRAM $4800 = ROM $18000
    (tile $240). INFANTRY uses $400 + dir*4. Size byte = record +$10."""
    red = palette(rom, 0x20A20)
    blue = palette(rom, 0x20A40)
    bank = rom[0x18000 - 0x240 * 32: 0x1D800]
    img = Image.new('RGB', (70 + 16 * 34, 10 * 34), (60, 90, 60))
    d = ImageDraw.Draw(img)
    for t in range(10):
        rec = 0x5DC00 + t * 32
        size = rom[rec + 0x10]
        w, h = ((size >> 2) & 3) + 1, (size & 3) + 1
        d.text((2, t * 34 + 12), rom[rec:rec + 8].decode('latin1'),
               fill=(255, 255, 255))
        for dirn in range(16):
            e = 0x400 + dirn * 4 if t == 8 else w16(rom, 0x244A + t * 32 + dirn * 2)
            sprite(img, bank, e & 0x7FF, w, h, 70 + dirn * 34, t * 34 + 4,
                   red if dirn < 8 else blue, bool(e & 0x800), bool(e & 0x1000))
    save(img, outdir, 'units.png', 2)


def hud(rom, outdir):
    """In-game VRAM loads from Game_LoadMap and boot, palette line 1 = $20A00."""
    pal = palette(rom, 0x20A00)
    save(tile_grid(rom[0x20000:0x209E0], 79, 16, pal), outdir, 'font_D620.png', 3)
    save(tile_grid(rom[0x1E000:0x20000], 256, 16, pal), outdir, 'vram_A000.png', 3)
    save(tile_grid(rom[0x18000:0x1D800], 704, 32, palette(rom, 0x20A20)),
         outdir, 'vram_4800.png', 2)
    icons = Image.new('RGB', (16 * 18, 18), (40, 0, 40))
    for n in range(16):                     # OrderMenu_DrawCarriedIcon / OrderMenu_DrawOrder, 2x2 each
        sprite(icons, rom[0x1D800 + n * 0x80:], 0, 2, 2, n * 18 + 1, 1, pal)
    save(icons, outdir, 'icons_1D800.png', 4)


def screens(rom, outdir):
    """$53000 module. Full screens are 40x28 tiles, row-major."""
    photo = palette(rom, 0x68400)
    for n in range(2):                      # Title_LoadGfx, Winner_Show
        save(tile_grid(rle_unpack(rom, 0x68420, n)[0], 1120, 40, photo),
             outdir, 'photo_68420_%d.png' % n)
    for n, p in ((0, 0x61000), (1, 0x61020)):   # Ending_ShowPicture, by $FFF84A
        save(tile_grid(rle_unpack(rom, 0x61100, n)[0], 1120, 40,
                       palette(rom, p)), outdir, 'photo_61100_%d.png' % n)
    save(tile_grid(lzw(rom, 0x57800, 0), 1120, 40, palette(rom, 0x5C3E0)),
         outdir, 'photo_57800.png')          # Ending_UnpackForest -> $FF0000 -> $2000
    logo = rle_unpack(rom, 0x70000, 2)[0]    # Title_LoadLogo, palette at +$27E0
    save(tile_grid(logo, 319, 29, palette(logo, 0x27E0)), outdir,
         'title_logo.png', 2)
    fonts = rle_unpack(rom, 0x70000, 0)[0]   # GameEnd_Run, palette $73E00
    save(tile_grid(fonts, 256, 32, palette(rom, 0x73E00)), outdir,
         'menu_fonts.png', 2)
    big = rle_unpack(rom, 0x60000, 0)[0]     # Winner_Show, palette $5439A
    save(tile_grid(big, 256, 32, palette(rom, 0x5439A + 0x20)), outdir,
         'winner_font.png', 2)
    end = palette(rom, 0x53688)              # Ending_SetTextPalette
    save(tile_grid(rle_unpack(rom, 0x70000, 3)[0], 80, 16, end), outdir,
         'ending_font.png', 2)
    save(tile_grid(rle_unpack(rom, 0x70000, 4)[0], 64, 16, end), outdir,
         'ende.png', 2)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: hzgfx.py ROM OUTDIR')
    rom = open(sys.argv[1], 'rb').read()
    os.makedirs(sys.argv[2], exist_ok=True)
    for f in (battle_maps, mech_frames, units, hud, screens):
        f(rom, sys.argv[2])


if __name__ == '__main__':
    main()
