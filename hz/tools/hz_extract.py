#!/usr/bin/env python3
"""Herzog Zwei: the ROM's data into assets/, in formats a person can edit.

    python3 tools/hz_extract.py ROM [ASSETS]      (ASSETS: default assets)
    python3 tools/hz_extract.py --views [ASSETS]  re-render the views

The C port loads everything it uses from assets/ at start-up and never
reads the ROM (src/game/assets.c). What each file holds, and which ROM
data it came from, is in assets/README.md. The formats follow the PS2
port's: indexed PNG for pixels (the PNG's palette a copy for viewing, the
palette strips the source), text for nametable words and for the songs
and sound effects (docs/sound.md), Tiled .tmx for the map layouts, JSON
for records. One departure: sprite art is laid out as
the sprites it makes (a unit's five headings as five 24 x 24 pictures),
with a JSON beside each sheet saying which tiles each picture holds; the
PS2 port's sheets are in tile order, which scrambles every sprite bigger
than a tile.

Files marked "view" in the README are drawn for looking at (Tiled's block
picture, the HUD frame) and nothing loads them; --views draws them again
from the assets after an edit. `make roundtrip` proves the rest: the C
loaders read every file back into the ROM's bytes.

ROM addresses come by name from hz.sym beside the ROM (`make rom` writes
it), else the tree's own hz.sym, else tools/hz_names.txt, so this reads
what the disassembly calls each block. (Session 78) The tree's hz.sym is
checked in, the original ROM's map: an image anywhere else is read
through it, and the game's own extractor in C (src/extract/, which the
first start runs; docs/first_start.md) takes its addresses from it. This
file is that one's reference: it must make the same files. Needs Pillow,
and hzlzw.py, hzrle.py and hzsongtext.py beside this file.
"""
import json
import os
import sys

from PIL import Image, ImageDraw

import hzasm_check
import hzlzw
import hzrle
import hzsongtext

HERE = os.path.dirname(os.path.abspath(__file__))

# Map numbers 0-7, as the options screen names them.
MAP_NAMES = ['abgrund', 'vulkan', 'loch', 'strand', 'stadt', 'eisfrei',
             'waldung', 'oase']

# The LZW tables Game_LoadMap unpacks (the C side's HZ_PACK_MAP_*), where
# the original has them; hz.sym's addresses win.
PACK_TABLES = {
    'pack_map_tiles': 0x22400,   # 8 x $4000 -> VRAM $0000
    'pack_map_layout': 0x35000,  # 8 x $2000: 64 x 64 words
    'pack_map_blocks': 0x3F000,  # 8 x $4800: blocks, terrain, anim records
    'pack_forest': 0x57800,      # 1 x $8C00: the endings' forest painting
}

# Measured VA4 DAC levels (the PS2 port's, and src/core/cram.c's).
DAC = [0, 52, 87, 116, 144, 172, 206, 255]

# Sprite sheets: the groups of tiles that make known pictures, as
# (name, first tile, width, height in tiles, pictures[, order]). A
# picture's tiles run down each column in turn (VDP sprite order) unless
# the order is 'rows'. Tiles in no group go in as single tiles.
MECH_GROUPS = [
    ('mech frames: $00-$2F robot, $30-$39 jet, $3A-$3F transforming',
     0x000, 4, 4, 64),
]

SPRITE_GROUPS = [       # SpriteTiles + RadarBlank: VRAM tiles $240-$4FF
    ('AMR-51D: headings 0-4', 0x240, 3, 3, 5),
    ('TAX-52: headings 0-4', 0x26D, 3, 3, 5),
    ('SAM-42: headings 0-4', 0x29A, 3, 3, 5),
    ('ST-57U: headings 0-4', 0x2C7, 3, 3, 5),
    ('SUPPLY: headings 0-4', 0x2F4, 3, 3, 5),
    ('GRM-34A: headings 0-4', 0x321, 3, 3, 5),
    ('TAX-52 turret: headings 0-4', 0x34E, 3, 3, 5),
    ('ST-57U turret: headings 0-4', 0x37B, 3, 3, 5),
    ('order menu pictures (UnitMenuTiles): INFANTRY, TAX-52, ST-57U, FWA',
     0x3A8, 3, 3, 4),
    ('tiles $3CC-$3EB, blank: nothing draws them', 0x3CC, 1, 1, 32),
    ('FWA: headings 0-4', 0x3EC, 2, 2, 5),
    ('INFANTRY: headings 0-15', 0x400, 2, 2, 16),
    ('INFANTRY: headings 0-15, other step', 0x440, 2, 2, 16),
    ('INFANTRY dying: 15 - HP', 0x480, 2, 2, 16),
    ('radar, blank (RadarBlank)', 0x4C0, 8, 8, 1, 'rows'),
]

HUD_GROUPS = [          # HUDTiles: VRAM tiles $500-$5FF
    ('divider between the views', 0x500, 4, 4, 1),
    ('explosion (ExplosionFrames)', 0x510, 3, 3, 6),
    ('jet shadow', 0x570, 4, 4, 1),
    ('unit out of fuel or ammo', 0x590, 2, 2, 1),
    ('outpost hold bar, P1', 0x594, 1, 4, 1),
    ('outpost hold bar, P2', 0x598, 1, 4, 1),
    ('HQ hit warning', 0x5A3, 3, 3, 1),
    ('HUD bar cells: HUD_QueueBar, see the README', 0x5B0, 1, 1, 15),
    ('unit building', 0x5C0, 3, 3, 1),
    ('unit ready', 0x5C9, 3, 3, 1),
    ('missile warning arrow', 0x5D2, 2, 2, 1),
]

ORDER_ICON_GROUPS = [   # HUDIcons: 16 x 4 tiles, the order menu's icons
    ('order icons (HUDIcons): orders 0-15, by OrderInfo', 0x00, 2, 2, 16),
]

LOOSE_ACROSS = 16       # single tiles, as a plain tile sheet has them
SHEET_WIDTH = 256       # pixels: the widest a band of pictures runs


class Rom:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        # 512 KB, unless built from a changed source (the header's ROM end
        # follows RomEnd); the checksum as the game works it out
        end = int.from_bytes(self.data[0x1A4:0x1A8], 'big') + 1
        if len(self.data) != end:
            sys.exit('%s: %d bytes, the header says %d'
                     % (path, len(self.data), end))
        s = hzasm_check.checksum(self.data)
        if s != self.w16(0x18E):
            sys.exit('%s: checksum $%04X, header says $%04X'
                     % (path, s, self.w16(0x18E)))
        self.labels = dict(PACK_TABLES)
        for line in open(os.path.join(HERE, 'hz_names.txt')):
            f = line.split(';')[0].split()
            if len(f) == 2 and f[0].startswith('$') and f[1][0].isupper():
                self.labels[f[1]] = int(f[0][1:], 16)
        # Since session 21 the disassembly is named directly; `make rom`
        # writes its symbols beside the image (hz.sym), which win. (Session
        # 78) An image anywhere else (a player's own cartridge) is read
        # through the tree's hz.sym, the original ROM's, checked in.
        sym = os.path.join(os.path.dirname(os.path.abspath(path)), 'hz.sym')
        if not os.path.exists(sym):
            sym = os.path.join(HERE, '..', 'hz.sym')
        if os.path.exists(sym):
            for line in open(sym):
                f = line.split()
                if len(f) == 2 and f[1].startswith('$') and '.' not in f[0] \
                        and (f[0][0].isupper() or f[0] in PACK_TABLES):
                    self.labels[f[0]] = int(f[1][1:], 16)

    def at(self, name):
        if name not in self.labels:
            sys.exit('hz_names.txt has no %s' % name)
        return self.labels[name]

    def w16(self, a):
        return (self.data[a] << 8) | self.data[a + 1]

    def block(self, name, size):
        a = self.at(name)
        return self.data[a:a + size]

    def rle(self, table, n):
        return hzrle.rle_unpack(self.data, self.at(table), n)[0]

    def lzw(self, table, n):
        src, ln = hzlzw.table_entry(self.data, table, n)
        return bytes(hzlzw.decompress(self.data[src:src + ln], ln))


def w16(b, o):
    return (b[o] << 8) | b[o + 1]


def hexw(v):
    return '%04X' % v


# -- palettes ---------------------------------------------------------------

def cram_rgb(word):
    return (DAC[(word >> 1) & 7], DAC[(word >> 5) & 7], DAC[(word >> 9) & 7])


def palette_words(b, lines):
    return [w16(b, i * 2) for i in range(16 * lines)]


def write_palette(path, words, width=16):
    """16 wide, a row per line; pixel (x, y) is colour y * 16 + x, and
    the PNG's palette is the CRAM, through the DAC ladder. (A colour
    cycle's strip is as wide as the cycle is long.)"""
    rows = len(words) // width
    img = Image.new('P', (width, rows), 0)
    flat = []
    for w in words:
        if w & ~0x0EEE:
            sys.exit('%s: CRAM word $%04X has bits a colour can\'t hold'
                     % (path, w))
        flat += cram_rgb(w)
    img.putpalette(flat)
    for i in range(len(words)):
        img.putpixel((i % width, i // width), i)
    img.save(path)


def rgb_list(words):
    flat = []
    for w in words:
        flat += cram_rgb(w)
    return flat


# -- tiles ------------------------------------------------------------------

def tile_pixels(tiles, t):
    """64 indices of tile t, rows top to bottom, high nibble first."""
    o = t * 32
    px = []
    for b in tiles[o:o + 32]:
        px.append(b >> 4)
        px.append(b & 15)
    return px


def put_tile(img, tiles, t, x, y, hflip=False, vflip=False):
    px = tile_pixels(tiles, t)
    for ty in range(8):
        for tx in range(8):
            sx = 7 - tx if hflip else tx
            sy = 7 - ty if vflip else ty
            img.putpixel((x + tx, y + ty), px[sy * 8 + sx])


def write_tile_sheet(path, tiles, count, pal_words, transparent):
    """A plain tile sheet: count tiles, 16 across, row-major."""
    rows = (count + 15) // 16
    img = Image.new('P', (128, rows * 8), 0)
    img.putpalette(rgb_list(pal_words))
    for t in range(count):
        put_tile(img, tiles, t, (t % 16) * 8, (t // 16) * 8)
    save_indexed(img, path, transparent)


def save_indexed(img, path, transparent):
    if transparent:
        img.save(path, transparency=0)
    else:
        img.save(path)


def loose_name(first, last):
    """A gap between the groups: tiles nothing is known to draw."""
    if first == last:
        return 'tile $%X, not identified' % first
    return 'tiles $%X-$%X, not identified' % (first, last)


def sheet_layout(groups, first, count):
    """The groups in tile order, the gaps between them as single tiles,
    each group a band of the picture below the last."""
    items = []
    t = first
    for g in sorted(groups, key=lambda g: g[1]):
        name, tile, w, h, n = g[:5]
        order = g[5] if len(g) > 5 else 'columns'
        if tile < t:
            sys.exit('sheet groups overlap at $%X' % tile)
        if tile > t:
            items.append([loose_name(t, tile - 1), t, 1, 1, tile - t,
                          'rows'])
        items.append([name, tile, w, h, n, order])
        t = tile + w * h * n
    if t > first + count:
        sys.exit('sheet groups run past the region')
    if t < first + count:
        items.append([loose_name(t, first + count - 1), t, 1, 1,
                      first + count - t, 'rows'])
    out = []
    y = 0
    width = 0
    for name, tile, w, h, n, order in items:
        across = LOOSE_ACROSS if w == h == 1 else SHEET_WIDTH // (w * 8)
        across = min(across, n)
        rows = (n + across - 1) // across
        g = {'name': name, 'tile': '$%X' % tile, 'w': w, 'h': h,
             'count': n, 'across': across, 'x': 0, 'y': y}
        if order != 'columns' and not (w == 1 or h == 1):
            g['order'] = order
        out.append(g)
        width = max(width, across * w * 8)
        y += rows * h * 8
    return out, width, y


def picture_tile(g, i, k):
    """Tile number of tile k of picture i, and its place in the picture."""
    w, h = g['w'], g['h']
    t = int(g['tile'][1:], 16) + i * w * h + k
    if g.get('order', 'columns') == 'rows':
        return t, k % w, k // w
    return t, k // h, k % h


def write_sprite_sheet(folder, name, tiles, first, count, groups, pal_words):
    layout, width, height = sheet_layout(groups, first, count)
    img = Image.new('P', (width, height), 0)
    img.putpalette(rgb_list(pal_words))
    for g in layout:
        for i in range(g['count']):
            fx = g['x'] + (i % g['across']) * g['w'] * 8
            fy = g['y'] + (i // g['across']) * g['h'] * 8
            for k in range(g['w'] * g['h']):
                t, tx, ty = picture_tile(g, i, k)
                put_tile(img, tiles, t - first, fx + tx * 8, fy + ty * 8)
    save_indexed(img, os.path.join(folder, name + '.png'), True)
    sheet = {'image': name + '.png', 'first_tile': '$%X' % first,
             'tiles': count, 'groups': layout}
    write_json(os.path.join(folder, name + '.json'), sheet)


# -- text and JSON ----------------------------------------------------------

def write_text(path, header, rows):
    with open(path, 'w', newline='\n') as f:
        for line in header:
            f.write(('# ' + line).rstrip() + '\n')
        for r in rows:
            f.write(r + '\n')


def write_json(path, obj):
    """JSON, two-space indented, with any array or object of plain values
    that fits on one line (99 columns) written on one line, and longer
    arrays of plain values run together on as few lines as fit."""
    with open(path, 'w', newline='\n') as f:
        f.write(json_text(obj, 0, 0) + '\n')


def json_text(v, indent, used):
    """v as JSON starting `used` columns into a line indented `indent`."""
    plain = lambda x: not isinstance(x, (list, dict))
    if plain(v):
        return json.dumps(v)
    items = v.values() if isinstance(v, dict) else v
    one = json.dumps(v, separators=(', ', ': '))
    if all(plain(x) for x in items) and used + len(one) <= 99:
        return one
    pad = ' ' * (indent + 2)
    if isinstance(v, list) and all(plain(x) for x in v):
        lines = []
        cur = pad
        for n, x in enumerate(v):
            piece = json.dumps(x) + (',' if n < len(v) - 1 else '')
            if len(cur) + len(piece) > 79 and cur.strip():
                lines.append(cur.rstrip())
                cur = pad
            cur += piece + ' '
        lines.append(cur.rstrip())
        return '[\n' + '\n'.join(lines) + '\n' + ' ' * indent + ']'
    parts = []
    if isinstance(v, dict):
        for k, x in v.items():
            head = pad + json.dumps(k) + ': '
            parts.append(head + json_text(x, indent + 2, len(head)))
        return '{\n' + ',\n'.join(parts) + '\n' + ' ' * indent + '}'
    for x in v:
        parts.append(pad + json_text(x, indent + 2, len(pad)))
    return '[\n' + ',\n'.join(parts) + '\n' + ' ' * indent + ']'


def font_string(b):
    """A record's name bytes: printable ASCII as itself, anything else as
    {XX}. '{' never occurs in the game's names."""
    s = ''
    for c in b:
        if 0x20 <= c < 0x7F and c != ord('{'):
            s += chr(c)
        else:
            s += '{%02X}' % c
    return s


def sprite_attr(w):
    """A sprite attr word: tile in hex, then h / v for the flips, pal N and
    pri for what else is set."""
    s = '$%03X' % (w & 0x7FF)
    flips = ('h' if w & 0x800 else '') + ('v' if w & 0x1000 else '')
    if flips:
        s += ' ' + flips
    if w & 0x6000:
        s += ' pal%d' % ((w >> 13) & 3)
    if w & 0x8000:
        s += ' pri'
    return s


# -- maps -------------------------------------------------------------------

def render_blocks(tiles, blocks, pal_words):
    """The 512 blocks as 32 x 32 pictures, 16 across, for Tiled."""
    img = Image.new('P', (512, 1024), 0)
    img.putpalette(rgb_list(pal_words))
    for b in range(512):
        bx, by = (b % 16) * 32, (b // 16) * 32
        for i in range(16):
            e = blocks[b][i]
            put_tile(img, tiles, e & 0x1FF, bx + (i % 4) * 8,
                     by + (i // 4) * 8, bool(e & 0x800), bool(e & 0x1000))
    return img


def write_path_hints(path):
    """16 arrows for Tiled: 8-way directions 0 (up) to 7, clockwise; red
    for P1's layer, blue for P2's."""
    img = Image.new('RGBA', (256, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    import math
    for team, colour in ((0, (230, 40, 40, 255)), (1, (60, 110, 255, 255))):
        for k in range(8):
            cx, cy = k * 32 + 16, team * 32 + 16
            a = math.radians(k * 45)
            dx, dy = math.sin(a), -math.cos(a)
            tip = (cx + dx * 12, cy + dy * 12)
            tail = (cx - dx * 10, cy - dy * 10)
            d.line([tail, tip], fill=colour, width=3)
            left = (tip[0] - dx * 7 + dy * 6, tip[1] - dy * 7 - dx * 6)
            right = (tip[0] - dx * 7 - dy * 6, tip[1] - dy * 7 + dx * 6)
            d.polygon([tip, left, right], fill=colour)
    img.save(path)


def write_tmx(path, layout, name):
    """Tiled map: the block layer (gid = block + 1) and the two path-hint
    layers (gid = first arrow + 8-way direction, 0 where the word has no
    hint; red arrows 513-520 for P1, blue 521-528 for P2)."""
    blocks = []
    p1 = []
    p2 = []
    for w in layout:
        if not (w & 0x200) and (w & 0xFC00):
            sys.exit('%s: layout word $%04X has hint bits without bit 9'
                     % (name, w))
        blocks.append((w & 0x1FF) + 1)
        if w & 0x200:
            p1.append(513 + ((w >> 13) & 7))
            p2.append(521 + ((w >> 10) & 7))
        else:
            p1.append(0)
            p2.append(0)

    def csv(cells):
        rows = []
        for r in range(64):
            rows.append(','.join(str(c) for c in cells[r * 64:(r + 1) * 64]))
        return ',\n'.join(rows)

    with open(path, 'w', newline='\n') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<map version="1.10" tiledversion="1.10.2" '
                'orientation="orthogonal" renderorder="right-down" '
                'width="64" height="64" tilewidth="32" tileheight="32" '
                'infinite="0" nextlayerid="4" nextobjectid="1">\n')
        f.write(' <tileset firstgid="1" name="blocks" tilewidth="32" '
                'tileheight="32" tilecount="512" columns="16">\n')
        f.write('  <image source="blocks.png" width="512" height="1024"/>\n')
        f.write(' </tileset>\n')
        f.write(' <tileset firstgid="513" name="path_hints" tilewidth="32" '
                'tileheight="32" tilecount="16" columns="8">\n')
        f.write('  <image source="../path_hints.png" width="256" '
                'height="64"/>\n')
        f.write(' </tileset>\n')
        for lid, lname, cells in ((1, 'blocks', blocks),
                                  (2, 'path_p1', p1), (3, 'path_p2', p2)):
            f.write(' <layer id="%d" name="%s" width="64" height="64">\n'
                    % (lid, lname))
            f.write('  <data encoding="csv">\n')
            f.write(csv(cells) + '\n')
            f.write('</data>\n')
            f.write(' </layer>\n')
        f.write('</map>\n')


def extract_map(rom, n, root):
    name = MAP_NAMES[n]
    folder = os.path.join(root, 'maps', '%d_%s' % (n, name))
    os.makedirs(folder, exist_ok=True)

    tiles = rom.lzw(rom.at('pack_map_tiles'), n)
    layout_b = rom.lzw(rom.at('pack_map_layout'), n)
    blocks_b = rom.lzw(rom.at('pack_map_blocks'), n)
    if (len(tiles), len(layout_b), len(blocks_b)) != (0x4000, 0x2000, 0x4800):
        sys.exit('map %d: unexpected unpacked sizes' % n)
    pal = palette_words(rom.data[rom.at('MapPalettes') + n * 32:], 1)

    write_palette(os.path.join(folder, 'palette.png'), pal)
    write_tile_sheet(os.path.join(folder, 'tiles.png'), tiles, 512, pal,
                     False)

    blocks = [[w16(blocks_b, b * 32 + i * 2) for i in range(16)]
              for b in range(512)]
    terrain = [w16(blocks_b, 0x4000 + b * 2) for b in range(512)]
    write_text(os.path.join(folder, 'blocks.txt'), [
        'Map %d, %s: the 512 blocks (v_map_blocks, $FF0000), one a line:'
        % (n, name.upper()),
        '16 nametable words, 4 x 4 tiles row by row, then the block\'s',
        'terrain word (v_block_terrain, $FF4000). All in hex. What the',
        'bits mean is in assets/README.md. blocks.png is a picture of these',
        'for Tiled, drawn from them; editing it changes nothing.',
    ], ['%s  %s  # %03X' % (' '.join(hexw(w) for w in blocks[b]),
                             hexw(terrain[b]), b) for b in range(512)])

    anim = [[w16(blocks_b, 0x4400 + r * 32 + i * 2) for i in range(16)]
            for r in range(32)]
    write_text(os.path.join(folder, 'anim.txt'), [
        'Map %d, %s: the 32 animated-tile records (v_anim_tiles, $FF4400),'
        % (n, name.upper()),
        'one a line, 16 words in hex. Word 0 is the map tile animated (0: the',
        'record is off); the words from 0 on are the tiles it shows in turn',
        '(low 9 bits), up to a 0 word or the 16th. Map_AnimTilesUpdate',
        '($5738) steps them; see assets/README.md.',
    ], ['%s  # %02d' % (' '.join(hexw(w) for w in anim[r]), r)
        for r in range(32)])

    layout = [w16(layout_b, i * 2) for i in range(4096)]
    write_tmx(os.path.join(folder, 'layout.tmx'), layout, name)

    radar = rom.data[rom.at('RadarImages') + n * 0x800:][:0x800]
    img = Image.new('P', (64, 64), 0)
    img.putpalette(rgb_list(pal))
    for t in range(64):
        put_tile(img, radar, t, (t % 8) * 8, (t // 8) * 8)
    save_indexed(img, os.path.join(folder, 'radar.png'), False)


# -- the rest -------------------------------------------------------------

UNIT_FIELDS = [   # UnitStats record from +$08: (name, offset, bytes, hex?)
    ('price', 0x08, 2, False), ('hp', 0x0A, 2, False),
    ('size', 0x0C, 1, False), ('ammo', 0x0D, 1, False),
    ('fuel', 0x0E, 2, False), ('sprite_size', 0x10, 1, True),
    ('speed', 0x11, 1, False), ('overlay', 0x12, 1, True),
    ('fuel_use', 0x13, 1, False), ('shot_speed', 0x14, 1, False),
    ('shot_life', 0x15, 1, False), ('fire_delay', 0x16, 1, False),
    ('power', 0x17, 1, False), ('sight', 0x18, 1, False),
    ('build_time', 0x19, 1, False), ('missiles', 0x1A, 1, False),
    ('missile_speed', 0x1B, 1, False), ('missile_life', 0x1C, 1, False),
    ('missile_delay', 0x1D, 1, False), ('orders', 0x1E, 2, True),
]


def stat_record(r):
    rec = {'name': font_string(r[0:8])}
    for name, off, size, as_hex in UNIT_FIELDS:
        v = w16(r, off) if size == 2 else r[off]
        rec[name] = ('$%0*X' % (size * 2, v)) if as_hex else v
    return rec


def extract_data(rom, root):
    folder = os.path.join(root, 'data')
    os.makedirs(folder, exist_ok=True)

    stats = rom.at('UnitStats')
    dirs = rom.at('UnitDirTable')
    kidou = rom.at('UnitStats_KIDOU')
    units = []
    for t in range((kidou - stats) // 32):
        rec = {'type': t}
        rec.update(stat_record(rom.data[stats + t * 32:stats + t * 32 + 32]))
        if t < 10:
            rec['sprites'] = [sprite_attr(rom.w16(dirs + t * 32 + d * 2))
                              for d in range(16)]
        units.append(rec)
    mech = stat_record(rom.data[kidou:kidou + 32])
    k = rom.data[kidou + 0x20:kidou + 0x28]
    mech['max_speed'] = {'robot': k[0], 'jet': k[1], 'carrying': k[2]}
    mech['fuel_use_by_form'] = {'robot': k[3], 'jet': k[4], 'carrying': k[5]}
    mech['byte_26'] = k[6]
    mech['byte_27'] = k[7]
    explosion = rom.at('ExplosionFrames')
    write_json(os.path.join(folder, 'units.json'), {
        'units': units,
        'mech': mech,
        'explosion_frames': [sprite_attr(rom.w16(explosion + i * 2))
                             for i in range(16)],
    })

    start = rom.at('StartUnits')
    maps = []
    for n in range(8):
        entry = {'map': n, 'name': MAP_NAMES[n].upper()}
        for half, key in ((0, 'p1_cpu'), (1, 'p2_cpu')):
            recs = []
            for i in range(32):
                r = rom.data[start + n * 0x200 + half * 0x100 + i * 8:][:8]
                if not any(r):
                    if any(rom.data[start + n * 0x200 + half * 0x100 + j * 8]
                           for j in range(i, 32)):
                        sys.exit('StartUnits: a gap in map %d' % n)
                    break
                recs.append({'flags': '$%02X' % r[0], 'type': r[1],
                             'order': r[2], 'dir': r[3], 'x': w16(r, 4),
                             'y': w16(r, 6)})
            entry[key] = recs
        maps.append(entry)
    cpu = rom.at('CPUStartTypeTable')
    write_json(os.path.join(folder, 'start_units.json'), {
        'cpu_start_types': list(rom.data[cpu:cpu + 4]),
        'maps': maps,
    })

    sfx = rom.at('SfxPriority')
    prio = list(rom.data[sfx:sfx + 256])
    while prio and prio[-1] == 0xFF:
        prio.pop()
    write_json(os.path.join(folder, 'sound.json'), {'sfx_priority': prio})

    info = rom.at('OrderInfo')
    orders = []
    for n in range(16):
        r = rom.data[info + n * 16:info + n * 16 + 16]
        if any(r[12:16]):
            sys.exit('OrderInfo %d: bytes 12-15 not 0' % n)
        orders.append({'order': n, 'name': font_string(r[0:10]),
                       'fee': w16(r, 10)})
    avail = rom.at('UnitAvailByMap')
    if any(rom.data[avail + 16:avail + 32]):
        sys.exit('UnitAvailByMap: words past map 7 not 0')
    buyable = []
    for n in range(8):
        bits = rom.w16(avail + n * 2)
        buyable.append({'map': n, 'name': MAP_NAMES[n].upper(),
                        'types': [t for t in range(16) if bits >> t & 1]})
    write_json(os.path.join(folder, 'order_menu.json'), {
        'orders': orders,
        'buyable': buyable,
    })


PAD_LETTERS = 'UDLRBCAS'        # v_joypad bits 0-7: Up Down Left Right B C A Start
DEMO_SCREENS = {(1, 0x0000, 0x0101): 'split',
                (0, 0x0000, 0xFF01): 'wide P1',
                (0, 0xFF00, 0x0001): 'wide P2'}


def pad_run(buttons, frames):
    """'44 UL': frames, then the buttons held (PAD_LETTERS order), - none."""
    held = ''.join(c for i, c in enumerate(PAD_LETTERS) if buttons >> i & 1)
    return '%d %s' % (frames, held or '-')


def demo_stream(rom, map_n, b):
    """A DemoData stream's runs, merged where the recorder split them at
    255 frames (it starts a new run with the same buttons only then)."""
    runs = []
    for i in range(0, 0x380, 2):
        buttons, frames = b[i], b[i + 1]
        if frames == 0:
            if any(b[i:]):
                sys.exit('DemoData map %d: bytes after a stream\'s end'
                         % map_n)
            break
        if runs and runs[-1][0] == buttons:
            if runs[-1][2] != 255:
                sys.exit('DemoData map %d: a run split short of 255'
                         % map_n)
            runs[-1][1] += frames
            runs[-1][2] = frames
        else:
            runs.append([buttons, frames, frames])
    else:
        sys.exit('DemoData map %d: a stream without an end' % map_n)
    return [pad_run(r[0], r[1]) for r in runs]


def extract_demos(rom, root):
    """DemoTable as data/demos.json; each map's DemoData as its folder's
    demo.json (the recording's units and both pads' runs)."""
    table = rom.at('DemoTable')
    demos = []
    for k in range(16):
        r = rom.data[table + k * 8:table + k * 8 + 8]
        key = (w16(r, 2), w16(r, 4), w16(r, 6))
        if r[0] or r[1] > 7 or key not in DEMO_SCREENS:
            sys.exit('DemoTable %d: not a map and screen the port knows'
                     % k)
        demos.append({'map': r[1], 'screen': DEMO_SCREENS[key]})
    write_json(os.path.join(root, 'data', 'demos.json'), {'demos': demos})

    data = rom.at('DemoData')
    for n in range(8):
        b = rom.data[data + n * 0x800:data + (n + 1) * 0x800]
        units = []
        for i in range(32):
            r = b[i * 8:i * 8 + 8]
            if not any(r):
                if any(b[j * 8] for j in range(i, 32)):
                    sys.exit('DemoData map %d: a gap in the units' % n)
                break
            units.append({'flags': '$%02X' % r[0], 'type': r[1],
                          'order': r[2], 'dir': r[3], 'x': w16(r, 4),
                          'y': w16(r, 6)})
        folder = os.path.join(root, 'maps', '%d_%s' % (n, MAP_NAMES[n]))
        write_json(os.path.join(folder, 'demo.json'), {
            'units': units,
            'p1': demo_stream(rom, n, b[0x100:0x480]),
            'p2': demo_stream(rom, n, b[0x480:0x800]),
        })


def extract_tiles(rom, root):
    folder = os.path.join(root, 'tiles')
    os.makedirs(folder, exist_ok=True)
    game_pal = palette_words(rom.block('GamePalettes', 0x60), 3)
    line1, line2 = game_pal[0:16], game_pal[16:32]
    write_sprite_sheet(folder, 'mech', rom.block('MechFrames', 0x8000),
                       0, 0x400, MECH_GROUPS, line2)
    sprite_tiles = rom.block('SpriteTiles', 0x5800)
    write_sprite_sheet(folder, 'sprites', sprite_tiles, 0x240, 0x2C0,
                       SPRITE_GROUPS, line2)
    hud_tiles = rom.block('HUDTiles', 0x2000)
    write_sprite_sheet(folder, 'hud', hud_tiles, 0x500, 0x100, HUD_GROUPS,
                       line1)
    write_sprite_sheet(folder, 'order_icons', rom.block('HUDIcons', 0x800),
                       0, 0x40, ORDER_ICON_GROUPS, line1)
    write_tile_sheet(os.path.join(folder, 'font.png'),
                     rom.block('FontTiles', 0x9E0), 79, line1, True)
    os.makedirs(os.path.join(root, 'palettes'), exist_ok=True)
    write_palette(os.path.join(root, 'palettes', 'game.png'), game_pal)
    write_palette(os.path.join(root, 'palettes', 'cycles.png'),
                  palette_words(rom.block('ColourCycles', 0x40), 2))

    hud = rom.at('HUDFrameTiles')
    rows = [[rom.w16(hud + (r * 40 + c) * 2) for c in range(40)]
            for r in range(4)]
    folder = os.path.join(root, 'screens')
    os.makedirs(folder, exist_ok=True)
    write_text(os.path.join(folder, 'hud_frame.txt'), [
        'The HUD strip (HUDFrameTiles): window rows 0-3, 40 nametable words',
        'a row, in hex. Screen_InitGame writes them to the window plane. The',
        'tiles are VRAM tile numbers: $500-$5FF tiles/hud.png, $6B1-$6FF',
        'tiles/font.png. hud_frame.png is a picture of it; editing it',
        'changes nothing.',
    ], [' '.join(hexw(w) for w in row) for row in rows])


# -- the title and the options screen ($53000 module) ---------------------

# The SEGA logo and the copyright line (RLE_MenuGfx entry 1, VRAM
# tiles $480-$4FF), drawn by the window plane in rows: Title_SegaLogo
# (12 x 4 from $480) and Title_WaitStart (a row of 16 over a row of 24, an
# overseas machine's from $4B0, a Japanese one's from $4D8).
SEGA_GROUPS = [
    ('SEGA logo (Title_SegaLogo)', 0x480, 12, 4, 1, 'rows'),
]
COPYRIGHT_GROUPS = [
    ('copyright, overseas: top row (window (8, 22))', 0x4B0, 16, 1, 1),
    ('copyright, overseas: bottom row (window (8, 23))', 0x4C0, 24, 1, 1),
    ('copyright, Japan: top row', 0x4D8, 16, 1, 1),
    ('copyright, Japan: bottom row', 0x4E8, 24, 1, 1),
]


def write_picture(path, tiles, across, rows, pal_words):
    """A picture drawn by a nametable of consecutive tiles: `rows` rows of
    `across` tiles, in reading order. Its PNG palette is its CRAM line (the
    source: the loader reads it back)."""
    img = Image.new('P', (across * 8, rows * 8), 0)
    img.putpalette(rgb_list(pal_words))
    for t in range(across * rows):
        put_tile(img, tiles, t, (t % across) * 8, (t // across) * 8)
    img.save(path)


def menu_text(b):
    """Text in the menu font: '@' (the font's blank) as a space."""
    s = ''
    for c in b:
        if c == 0x40:
            s += ' '
        elif 0x30 <= c < 0x7F and c != ord('{'):
            s += chr(c)
        else:
            s += '{%02X}' % c
    return s


def music_titles(rom):
    """MusicTitles: the 18 songs' titles, two lines each."""
    titles = rom.at('MusicTitles')
    out = []
    for n in range(18):
        lines = []
        for k in range(2):
            b = rom.data[titles + n * 40 + k * 20:][:20]
            if b[19] != 0 or 0 in b[:19]:
                sys.exit('MusicTitles %d: not 19 characters and a NUL' % n)
            lines.append(menu_text(b[:19]))
        out.append(lines)
    return out


def extract_sound(rom, root):
    """SoundData as text (hzsongtext.py: written only once it builds back
    into the same bytes), the songs named by their titles."""
    data = rom.at('SoundData')
    titles = [' '.join(' '.join(t).split()) for t in music_titles(rom)]
    try:
        hzsongtext.extract(rom.data[data:data + hzsongtext.SIZE],
                           os.path.join(root, 'sound'), titles)
    except ValueError as e:
        sys.exit('SoundData: %s' % e)


def extract_title(rom, root):
    folder = os.path.join(root, 'title')
    os.makedirs(folder, exist_ok=True)
    photo_pal = palette_words(rom.block('TankPhotoPalette', 0x20), 1)
    write_picture(os.path.join(folder, 'tank_photo.png'),
                  rom.rle('RLE_TankPhotos', 0), 40, 28, photo_pal)
    logo = rom.rle('RLE_MenuGfx', 2)
    if any(logo[318 * 32:319 * 32]):
        sys.exit('the title logo\'s tile 318 isn\'t blank')
    write_picture(os.path.join(folder, 'title_logo.png'), logo, 29, 11,
                  [w16(logo, 319 * 32 + i * 2) for i in range(16)])
    title_pal = palette_words(rom.block('SegaLogoPalette', 0x20), 1) \
        + palette_words(rom.block('CopyrightPalette', 0x20), 1)
    gfx = rom.rle('RLE_MenuGfx', 1)
    write_sprite_sheet(folder, 'sega_logo', gfx[:48 * 32], 0x480, 48,
                       SEGA_GROUPS, title_pal[:16])
    write_sprite_sheet(folder, 'copyright', gfx[48 * 32:], 0x4B0, 80,
                       COPYRIGHT_GROUPS, title_pal[16:])
    menu_pal = palette_words(rom.block('MenuPalette', 0x80), 4)
    write_tile_sheet(os.path.join(folder, 'menu_font.png'),
                     rom.rle('RLE_MenuGfx', 0), 256, menu_pal[:16], False)

    # The game's end: WINNER and the results (Winner_Show, Results_Show).
    winner_pal = palette_words(rom.block('WinnerPalette', 0x60), 3)
    write_tile_sheet(os.path.join(folder, 'winner_font.png'),
                     rom.rle('RLE_WinnerFont', 0), 256, winner_pal[32:],
                     False)
    write_picture(os.path.join(folder, 'tank_photo2.png'),
                  rom.rle('RLE_TankPhotos', 1), 40, 28,
                  palette_words(rom.block('WinnerPhotoPalette', 0x20), 1))

    pals = os.path.join(root, 'palettes')
    os.makedirs(pals, exist_ok=True)
    write_palette(os.path.join(pals, 'menu.png'), menu_pal)
    write_palette(os.path.join(pals, 'winner.png'), winner_pal)
    write_palette(os.path.join(pals, 'title.png'), title_pal)
    write_palette(os.path.join(pals, 'sega_logo_cycle.png'),
                  [w16(rom.block('SegaLogoCycle', 62), i * 2)
                   for i in range(31)], 31)
    write_palette(os.path.join(pals, 'copyright_cycle.png'),
                  [w16(rom.block('CopyrightCycle', 46), i * 2)
                   for i in range(23)], 23)

    repeat = rom.at('MusicTestRepeat')
    songs = []
    for n, lines in enumerate(music_titles(rom)):
        r = rom.data[repeat + n]
        if r not in (0, 0xFF):
            sys.exit('MusicTestRepeat %d: $%02X' % (n, r))
        songs.append({'song': n, 'title': lines, 'repeat': r == 0xFF})
    names = rom.at('MapNames')
    maps = []
    for n in range(8):
        b = rom.data[names + n * 8:][:8]
        if b[7] != 0:
            sys.exit('MapNames %d: not 7 characters and a NUL' % n)
        maps.append(menu_text(b[:7]))
    write_json(os.path.join(root, 'data', 'menu.json'), {
        'songs': songs,
        'map_names': maps,
        'sfx_count': rom.data[rom.at('M_SfxCount')],
    })


# -- the endings (Ending_Run) ------------------------------------------------

# The story font (RLE_MenuGfx entry 3): a character c is tile c - $30. Its
# punctuation sits on other codes; the text files show what is drawn.
STORY_PUNCT = {0x3A: '.', 0x3B: ',', 0x3C: "'", 0x3D: '"', 0x3E: '!',
               0x40: ' '}


def story_text(b):
    """Codes of the story font as the characters they draw; {XX} for a code
    with no character of its own (the font's other blanks)."""
    s = ''
    for c in b:
        if c in STORY_PUNCT:
            s += STORY_PUNCT[c]
        elif 0x30 <= c <= 0x39 or 0x41 <= c <= 0x5A or 0x61 <= c <= 0x7A:
            s += chr(c)
        else:
            s += '{%02X}' % c
    return s


def extract_ending(rom, root):
    folder = os.path.join(root, 'ending')
    os.makedirs(folder, exist_ok=True)
    text_pal = palette_words(rom.block('EndingTextPalette', 0x20), 1)
    write_palette(os.path.join(root, 'palettes', 'ending_text.png'), text_pal)
    font = rom.rle('RLE_MenuGfx', 3)
    write_tile_sheet(os.path.join(folder, 'story_font.png'), font, 80,
                     text_pal, False)
    write_picture(os.path.join(folder, 'ende.png'), rom.rle('RLE_MenuGfx', 4),
                  16, 4, text_pal)
    for n, name, pal in [(0, 'still_life', 'EndingPaletteStillLife'),
                         (1, 'grave', 'EndingPaletteGrave')]:
        write_picture(os.path.join(folder, name + '.png'),
                      rom.rle('RLE_EndingPictures', n), 40, 28,
                      palette_words(rom.block(pal, 0x20), 1))
    write_picture(os.path.join(folder, 'forest.png'),
                  rom.lzw(rom.at('pack_forest'), 0), 40, 28,
                  palette_words(rom.block('ForestPalette', 0x20), 1))

    for name, label, who in [
            ('story_p1.txt', 'EndingText1', 'unless the human played P2 '
             'against the computer'),
            ('story_p2.txt', 'EndingText2', 'when the human played P2 '
             'against the computer')]:
        a = rom.at(label)
        rows = []
        for n in range(22):
            b = rom.data[a + n * 0x28:][:0x28]
            if b[38:] != b'\0\0' or 0 in b[:38]:
                sys.exit('%s line %d: not 38 characters and two 0s'
                         % (label, n))
            rows.append(story_text(b[:38]).rstrip(' '))
        write_text(os.path.join(folder, name), [
            'The endings\' story (%s), %s:' % (label, who),
            '22 lines of up to 38 characters, scrolled up over the picture.',
            'Spaces at the end of a line are added back; {XX} is a code of the',
            'story font (story_font.png) with no character of its own.',
        ], rows)

    a = rom.at('StaffText')
    rows = []
    kind = 'heading'
    while True:
        b = rom.data[a:a + 17]
        if b[16] != 0 or 0 in b[:16]:
            sys.exit('StaffText at $%X: not 16 characters and a 0' % a)
        rows.append('%-8s|%s|' % (kind, story_text(b[:16])))
        a += 17
        mark = rom.data[a]
        if mark == 0:
            break
        if mark == 1:
            kind = 'name'
            a += 1
        elif mark == 2:
            kind = 'heading'
            a += 1
    for kind, label in [('overseas', 'StaffCopyrightOverseas'),
                        ('japan', 'StaffCopyrightJapan')]:
        b = rom.data[rom.at(label):][:17]
        if b[16] != 0:
            sys.exit('%s: not 16 characters and a 0' % label)
        rows.append('%-8s|%s|' % (kind, story_text(b[:16])))
    write_text(os.path.join(folder, 'staff.txt'), [
        'The staff roll (StaffText), a line each: its kind, then 16',
        'characters between bars (fewer are padded with spaces). heading: red,',
        'name: yellow; a blank row comes before the first name after a',
        'heading, 17 before the first heading after a name. Then the last',
        'line, by the machine\'s region: overseas ("Techno Soft") or japan.',
    ], rows)

# -- views ------------------------------------------------------------------

def read_indexed(path):
    img = Image.open(path)
    if img.mode != 'P':
        sys.exit('%s: not an indexed PNG' % path)
    return img


def sheet_tiles(img, count):
    """A plain tile sheet back to packed tiles."""
    across = img.width // 8
    out = bytearray()
    px = img.load()
    for t in range(count):
        x0, y0 = (t % across) * 8, (t // across) * 8
        for y in range(8):
            for x in range(0, 8, 2):
                out.append((px[x0 + x, y0 + y] << 4) | px[x0 + x + 1, y0 + y])
    return bytes(out)


def sprite_sheet_tiles(folder, name):
    sheet = json.load(open(os.path.join(folder, name + '.json')))
    img = read_indexed(os.path.join(folder, sheet['image']))
    first = int(sheet['first_tile'][1:], 16)
    out = bytearray(sheet['tiles'] * 32)
    px = img.load()
    for g in sheet['groups']:
        for i in range(g['count']):
            fx = g['x'] + (i % g['across']) * g['w'] * 8
            fy = g['y'] + (i // g['across']) * g['h'] * 8
            for k in range(g['w'] * g['h']):
                t, tx, ty = picture_tile(g, i, k)
                o = (t - first) * 32
                for y in range(8):
                    for x in range(0, 8, 2):
                        out[o] = (px[fx + tx * 8 + x, fy + ty * 8 + y] << 4) \
                            | px[fx + tx * 8 + x + 1, fy + ty * 8 + y]
                        o += 1
    return bytes(out)


def read_palette(path):
    img = read_indexed(path)
    pal = img.getpalette()
    words = []
    for i in range(img.width * img.height):
        rgb = pal[i * 3:i * 3 + 3]
        w = 0
        for ch, shift in zip(rgb, (1, 5, 9)):
            level = min(range(8), key=lambda v: abs(DAC[v] - ch))
            w |= level << shift
        words.append(w)
    return words


def read_hex_rows(path, width):
    rows = []
    for line in open(path):
        line = line.split('#')[0].split()
        if line:
            rows.append([int(v, 16) for v in line[:width]])
    return rows


def render_views(root):
    """The pictures nothing loads, drawn from the assets as they are."""
    for n in range(8):
        folder = os.path.join(root, 'maps', '%d_%s' % (n, MAP_NAMES[n]))
        pal = read_palette(os.path.join(folder, 'palette.png'))
        tiles = sheet_tiles(read_indexed(os.path.join(folder, 'tiles.png')),
                            512)
        blocks = [r[:16] for r in read_hex_rows(
            os.path.join(folder, 'blocks.txt'), 17)]
        render_blocks(tiles, blocks, pal).save(
            os.path.join(folder, 'blocks.png'))
    write_path_hints(os.path.join(root, 'maps', 'path_hints.png'))

    tiles_dir = os.path.join(root, 'tiles')
    vram = bytearray(0x10000)
    vram[0x4800:0xA000] = sprite_sheet_tiles(tiles_dir, 'sprites')
    vram[0xA000:0xC000] = sprite_sheet_tiles(tiles_dir, 'hud')
    font = sheet_tiles(read_indexed(os.path.join(tiles_dir, 'font.png')), 79)
    vram[0xD620:0xD620 + len(font)] = font
    game_pal = read_palette(os.path.join(root, 'palettes', 'game.png'))
    rows = read_hex_rows(os.path.join(root, 'screens', 'hud_frame.txt'), 40)
    img = Image.new('P', (320, 32), 0)
    img.putpalette(rgb_list([0] * 16 + game_pal))
    for r, row in enumerate(rows):
        for c, e in enumerate(row):
            t = e & 0x7FF
            line = (e >> 13) & 3
            view = Image.new('P', (8, 8), 0)
            put_tile(view, vram[t * 32:t * 32 + 32], 0, 0, 0,
                     bool(e & 0x800), bool(e & 0x1000))
            for y in range(8):
                for x in range(8):
                    img.putpixel((c * 8 + x, r * 8 + y),
                                 line * 16 + view.getpixel((x, y)))
    img.save(os.path.join(root, 'screens', 'hud_frame.png'))


def main():
    args = sys.argv[1:]
    if args[:1] == ['--views']:
        render_views(args[1] if len(args) > 1 else 'assets')
        return
    if len(args) not in (1, 2):
        sys.exit(__doc__.split('\n\n')[1])
    rom = Rom(args[0])
    root = args[1] if len(args) > 1 else 'assets'
    os.makedirs(root, exist_ok=True)
    for n in range(8):
        extract_map(rom, n, root)
    extract_tiles(rom, root)
    extract_data(rom, root)
    extract_title(rom, root)
    extract_sound(rom, root)
    extract_ending(rom, root)
    extract_demos(rom, root)
    render_views(root)
    print('%s: written from %s' % (root, args[0]))


if __name__ == '__main__':
    main()
