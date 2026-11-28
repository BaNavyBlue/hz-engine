#!/usr/bin/env python3
"""Check hz_shot's frames against a direct model of the screen.

    python3 tools/hzview_check.py hz.md PATH...

Each PATH is a .raw file or a directory of them. A .raw file is 320 x 224
palette indices, then 64 CRAM words, then (hz_shot writes it) a trailer:
"HZC1", map, mode, and the cameras the frame was drawn from (later
versions add to it, below). Files without
a trailer are named map<N>_<kind>.raw and use the start cameras. For each
frame this works out, straight from the ROM and without the port's VDP
model, drawer or scroll code, what the screen should hold:

  - each view: the map's pixels from its camera (layout -> block -> tile,
    & $19FF, line 0); views start at y 32, view A at x 0 (144 wide; wide
    screen 320, from camera 2 in mode 6), view B at x 176 (144 wide). The
    start cameras are the HQ's centre minus ($48,$60) in split screen or
    ($A0,$60) in wide screen;
  - the HUD strip (y 0-31): HUDFrameTiles on the window;
  - split screen's divider: sprites of tile $500, 4 x 4, at x 144, y 32 up;
  - CRAM: line 0 the map's palette (the fade's end), lines 1-3
    GamePalettes;
  - without the HZC3 trailer (the frame before the loop's first sprite
    upload), the loading screen's two jets, still in the sprite table:
    sprites 6 and 7, MechFrames frame $30 (tiles $200 and $210), lines 2
    and 3, priority, at y 112; split screen x 56 and 232, wide screen the
    shown player's at x 144 (the other's off the left edge).

With the HZC2 trailer (the plane A/B nametables and scroll registers)
and numpy, every plane cell under a view pixel must also hold the map's
nametable word: the block word & $19FF, priority on tiles below $40 (what
Map_DrawBlockRect and the scroll queues write). That catches what pixels
can't show, the priority bit above all, which matters once sprites are
drawn.

With the HZC3 trailer (the state the frame's sprites were built from)
the mechs are drawn too, from that state and the ROM, as the game's
rules put them (read from Sprites_Build and its drawers, not from the
port):

  - a jet (mech_form not 0): in its own view (shown) at mech_screen_x/y,
    4 x 4 of its frame (KIDOU frames at $10000 + frame x $200, the frame
    word's flips, priority, P1 line 2, P2 line 3), with a shadow (tile
    $570, line 1, priority) mech_altitude pixels to the right (the
    altitude Sprites_Build found + 2, at most $10: it climbs); in the
    rival's view (shown) at obj_x/y - the rival's camera if within -$20
    up to the view's width - $20 across and -$20..$BF down, the shadow
    likewise when the altitude isn't 0;
  - a robot: the same places, no shadow, no priority and no flips: map
    tiles below $40 (priority) cover it;
  - a destroyed mech: its 16 tiles as 1 x 1 sprites at (x, y) + $C +
    radius x SineTable[MechDebrisDirs[n]] / 16, radius $C0 - |HP - $C0|,
    while under $64, in its team's view and the rival's;
  - alerts, for a mech whose view is shown (P1's, then P2's): its
    production state 1 / 2 as a 3 x 3 icon (tile $5C0 / $5C9) at (56, 34)
    in its team's view (wide: (144, 34)) while v_blink_timer >= $14; for
    each bit n of mech_missile_dirs a 2 x 2 arrow ($5D2) at entry n of
    MissileArrowsSplit (x + $B0 in view slot 1) or MissileArrowsWide
    while v_blink_timer & $1F >= 8; the HQ warning ($5A3, 3 x 3) at (92,
    34) (wide (180, 34)) while mech_hq_alert (as found) isn't 0 and
    v_blink_timer >= $10; all line 1, priority;
  - each outpost (the first 16 outpost blocks, row by row) its owner's
    hold of 4 (none if neutral) as a bar 4 cells tall beside its block:
    P1's (tile $594, line 1, priority) 8 px left of the block, P2's ($598)
    on its right edge, from 8 px above its top; drawn if the bar's x is
    within -8 up to the view's width and the outpost's centre + 8 within
    the view's height;
  - with the HZC4 trailer (the difficulty), the CPU's starting units,
    worked out from the ROM (spawn_units: StartUnits, CPUStartTypeTable,
    UnitStats, UnitDirTable, the spawn test): view A's pass, then view
    B's, each unit within -$20 up to the view's width ($B0 split, $160
    wide) - $20 across and -$20..$BF down of the view's camera: a marker
    ($590, 2 x 2) while v_blink_timer < $28 if it has no fuel or neither
    missiles nor ammo, its turret overlay, the unit (line 2 / 3, no
    priority);
  - the order, front to back: divider, jets (P2 a jet: P2's own before
    P1's, else P1's first), then the list: outpost bars (view A, B),
    shadows or debris (P1, P2), robots (P1, P2), each own view then the
    other, units, with the alerts ahead of everything in the list, every
    one drawn (session 81: no cap, no flicker, no 20 sprites or 320 dots
    a line, docs/hackable.md); game sprites only from line 32 (the HUD
    strip cut);
  - over the planes: a sprite pixel shows unless it is low priority and
    the plane pixel under it is high priority and not transparent.

With the HZC6 trailer (v_unit_ai_group as Map_AnimTilesUpdate found it)
the map animates: in a directory of frames, from the first on, every
pass whose group is 0 or 6 steps tools/hzanim_check.py's model of
Map_AnimTilesUpdate and Map_AnimTilesUpload (the records from the ROM,
the VRAM copies and the pictures put back, the colour cycle), and the
views' pixels come from the model's VRAM tiles, CRAM line 0 colours 2 and
$B from its CRAM; a frame shows the pass before it, as for the bars. A
single file with sprite state (an unknown past) leaves the pixels of
every record's tile and those two colours unchecked.

With the HZC5 trailer (each mech's ammo, fuel and HQ HP as
HUD_UpdateGauges found them) the HUD's eight bars are drawn too: in a
directory of frames, from the first (which must be the first pass's, with
no sprite state yet) on, every frame's state goes through
tools/hzhud_check.py's model of HUD_UpdateGauges and HUD_QueueBar, and a
bar's cells show what was last queued for it (HUDFrameTiles' own until
then). A frame shows the bars of the pass before it, as its sprites. A
single file with sprite state has an unknown past: its bars' cells aren't
checked. A frame with no sprite state after the first leaves the bars as
they are (check-shots' directory: first frames of separate runs).

Transparent pixels expect the backdrop (line 2, colour 0): nothing is
under the views but blank plane cells. In split screen that includes
plane B where it shows under view A's transparent pixels, which the
scroll code has to keep clear as the views move. Exit status 1 on any
mismatch. With numpy the views are cut from a whole-map image (fast
enough for thousands of frames); without it every pixel is modelled one
at a time.
"""
import copy
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hzlzw  # noqa: E402
from hzhud_check import CMDS, Model as HudModel  # noqa: E402
from hzanim_check import Anim  # noqa: E402

try:
    import numpy as np
except ImportError:
    np = None

W, H = 320, 224
BACKDROP = 0x20


def bar_cells():
    """The eight HUD bars (P1 HP, HQ, ammo, fuel, then P2's): the screen
    x, y of the first cell and the cell count, from HUD_UpdateGauges' VDP
    commands (window cells, 64 a row from $F000)."""
    out = []
    for p in (0, 1):
        for k, cmd in enumerate(CMDS[p]):
            addr = ((cmd >> 16) & 0x3FFF) | ((cmd & 3) << 14)
            row, col = (addr - 0xF000) // 0x80, (addr - 0xF000) % 0x80 // 2
            out.append((col * 8, row * 8, 8 if k < 2 else 6))
    return out


BARS = bar_cells()


def w16(b, o):
    return (b[o] << 8) | b[o + 1]


def lzw(rom, base, n):
    src, ln = hzlzw.table_entry(rom, base, n)
    return bytes(hzlzw.decompress(rom[src:src + ln], ln))


def tile_px(tiles, tile, x, y, hf=False, vf=False):
    if hf:
        x = 7 - x
    if vf:
        y = 7 - y
    b = tiles[tile * 32 + y * 4 + x // 2]
    return (b & 15) if x & 1 else (b >> 4)


class Map:
    def __init__(self, rom, n):
        self.blocks = lzw(rom, 0x3F000, n)
        self.tiles = lzw(rom, 0x22400, n)
        self.layout = lzw(rom, 0x35000, n)
        self.hq = {}
        self.outposts = []     # (x, y, P1 hold, P2 hold), the first 16
        for i in range(64 * 64):
            blk = w16(self.layout, i * 2) & 0x1FF
            ter = w16(self.blocks, 0x4000 + blk * 2)
            centre = ((i % 64) * 32 + 16, (i // 64) * 32 + 16)
            if ter & 0xFF == 3:
                p = (ter >> 8) & 1
                self.hq[p] = centre
            elif ter & 0xFF == 2 and len(self.outposts) < 16:
                owner = (ter >> 8) & 3        # b1: owned, b0: by P2
                self.outposts.append(centre + ((4, 0) if owner == 2 else
                                               (0, 4) if owner == 3 else (0, 0)))

    def word(self, mx, my):
        mx %= 2048
        my %= 2048
        blk = w16(self.layout, ((my // 32) * 64 + mx // 32) * 2) & 0x1FF
        word = w16(self.blocks, blk * 32 + ((my % 32) // 8 * 4 + (mx % 32) // 8) * 2)
        return word & 0x19FF

    def px(self, mx, my, tiles=None):
        word = self.word(mx, my)
        c = tile_px(self.tiles if tiles is None else tiles, word & 0x1FF,
                    mx % 8, my % 8, bool(word & 0x800), bool(word & 0x1000))
        return c if c else BACKDROP


def start_cameras(m, mode):
    dx = 0x48 if not (mode & 4) else 0xA0
    return [(m.hq[p][0] - dx, m.hq[p][1] - 0x60) for p in (0, 1)]


def views(mode, cams):
    """(x0, width, camera) for each view on screen."""
    if not (mode & 4):
        return [(0, 144, cams[0]), (176, 144, cams[1])]
    shown = 1 if mode == 6 else 0              # mode 6: P1 CPU, hidden
    return [(0, 320, cams[shown])]


def expected(rom, m, mode, cams=None):
    """Screen as a list (None = not checked), and the expected CRAM.
    cams None: the HUD and divider only."""
    exp = [None] * (W * H)
    split = not (mode & 4)
    if cams is not None:
        for x0, width, (cx, cy) in views(mode, cams):
            for y in range(32, H):
                for x in range(x0, x0 + width):
                    exp[y * W + x] = m.px(cx + x - x0, cy + y - 32)

    hud = 0x6C94
    for row in range(4):
        for col in range(40):
            word = w16(rom, hud + (row * 40 + col) * 2)
            for ty in range(8):
                for tx in range(8):
                    tile = word & 0x7FF
                    c = vram_tile_px(rom, m, tile, tx, ty,
                                     bool(word & 0x800), bool(word & 0x1000))
                    if c:
                        exp[(row * 8 + ty) * W + col * 8 + tx] = ((word >> 13) & 3) * 16 + c

    if split:
        for s in range(6):
            top = 32 + s * 32
            for dy in range(32):
                for dxp in range(32):
                    tile = 0x500 + (dxp // 8) * 4 + dy // 8
                    c = vram_tile_px(rom, m, tile, dxp % 8, dy % 8)
                    if c:
                        exp[(top + dy) * W + 144 + dxp] = 16 + c

    cram = [w16(rom, 0x73F00 + m.n * 32 + i * 2) for i in range(16)]
    cram += [w16(rom, 0x20A00 + i * 2) for i in range(48)]
    return exp, cram


def vram_tile_px(rom, m, tile, x, y, hf=False, vf=False, frames=None):
    """A tile of the in-game VRAM, from where Game_LoadMap loads it; tiles
    $200-$21F are the mechs' frames (VDP_UploadSprites), given `frames`."""
    if tile < 0x200:
        return tile_px(m.tiles, tile, x, y, hf, vf)
    if frames is not None and 0x200 <= tile < 0x220:
        f = frames[(tile - 0x200) // 16] & 0xFF
        return tile_px(rom[0x10000 + f * 0x200:], tile & 15, x, y, hf, vf)
    if 0x240 <= tile < 0x500:
        return tile_px(rom[0x18000:], tile - 0x240, x, y, hf, vf)
    if 0x500 <= tile < 0x600:
        return tile_px(rom[0x1E000:], tile - 0x500, x, y, hf, vf)
    if 0x6B1 <= tile < 0x700:
        return tile_px(rom[0x20000:], tile - 0x6B1, x, y, hf, vf)
    raise ValueError('tile $%X not loaded' % tile)


def map_image(m):
    """The whole 2048 x 2048 map as palette indices (numpy), built the same
    way as Map.px: layout -> block -> tile, & $19FF, flips, 0 -> backdrop."""
    tb = np.frombuffer(m.tiles, np.uint8).reshape(512, 8, 4)
    tiles = np.empty((512, 8, 8), np.uint8)
    tiles[:, :, 0::2] = tb >> 4
    tiles[:, :, 1::2] = tb & 15
    lay = np.frombuffer(m.layout[:0x2000], '>u2').reshape(64, 64) & 0x1FF
    blocks = np.frombuffer(m.blocks[:0x4000], '>u2').reshape(512, 4, 4)
    # the nametable word of every map tile, 256 x 256
    words = blocks[lay].transpose(0, 2, 1, 3).reshape(256, 256) & 0x19FF
    px = tiles[words & 0x1FF]                          # (256, 256, 8, 8)
    hf = (words & 0x800) != 0
    vf = (words & 0x1000) != 0
    px = np.where(hf[:, :, None, None], px[:, :, :, ::-1], px)
    px = np.where(vf[:, :, None, None], px[:, :, ::-1, :], px)
    img = px.transpose(0, 2, 1, 3).reshape(2048, 2048)
    return np.where(img == 0, BACKDROP, img).astype(np.int16)


def view_pixels(m, tiles, cx, cy, width):
    """A view's pixels (numpy, 192 x width) from its camera, through the
    map's nametable words, with `tiles` (512 x 8 x 8) as VRAM holds them
    now: the map animation changes them."""
    rows = (cy + np.arange(H - 32)) % 2048
    cols = (cx + np.arange(width)) % 2048
    words = m.words[np.ix_(rows >> 3, cols >> 3)] & 0x19FF
    py = np.broadcast_to((rows & 7)[:, None], words.shape)
    px = np.broadcast_to((cols & 7)[None, :], words.shape)
    py = np.where(words & 0x1000, 7 - py, py)
    px = np.where(words & 0x800, 7 - px, px)
    c = tiles[words & 0x1FF, py, px].astype(np.int16)
    return np.where(c == 0, BACKDROP, c)


def tile_array(data):
    """VRAM $0000-$3FFF as 512 x 8 x 8 pixels (numpy)."""
    tb = np.frombuffer(bytes(data), np.uint8).reshape(512, 8, 4)
    tiles = np.empty((512, 8, 8), np.uint8)
    tiles[:, :, 0::2] = tb >> 4
    tiles[:, :, 1::2] = tb & 15
    return tiles


def read_raw(path):
    data = open(path, 'rb').read()
    idx = data[:W * H]
    cram = [w16(data, W * H + i * 2) for i in range(64)]
    t = W * H + 128
    if data[t:t + 3] == b'HZC':
        n, mode = data[t + 4], data[t + 5]
        c = [w16(data, t + 6 + i * 2) for i in range(4)]
        c = [v - 0x10000 if v & 0x8000 else v for v in c]
        vdp = None
        if data[t + 3:t + 4] in (b'2', b'3', b'4', b'5', b'6'):
            o = t + 14
            vdp = {'planes': (data[o:o + 0x1000], data[o + 0x1000:o + 0x2000]),
                   'hscroll': (w16(data, o + 0x2000), w16(data, o + 0x2002)),
                   'vscroll': (w16(data, o + 0x2004), w16(data, o + 0x2006)),
                   'sprites': None}
        if data[t + 3:t + 4] in (b'3', b'4', b'5', b'6'):
            o = t + 14 + 0x2008
            difficulty = (data[o + 3 + 2 * 22]
                          if data[t + 3] in (ord('4'), ord('5'), ord('6'))
                          else None)
            gauges = None
            group = None
            if data[t + 3] in (ord('5'), ord('6')):
                g = o + 4 + 2 * 22
                gauges = [(data[g + p * 5], w16(data, g + p * 5 + 1),
                           w16(data, g + p * 5 + 3)) for p in (0, 1)]
                if data[t + 3] == ord('6'):
                    group = w16(data, g + 10)
            if data[o]:
                mechs = []
                for p in range(2):
                    q = o + 3 + p * 22
                    v = [w16(data, q + 2 + i * 2) for i in range(8)]
                    v = v[:4] + [x - 0x10000 if x & 0x8000 else x for x in v[4:]]
                    mechs.append({'flags': data[q],
                                  'view': data[q + 1] - 256 if data[q + 1] & 0x80 else data[q + 1],
                                  'form': v[0], 'frame': v[1], 'alt': v[2], 'hp': v[3],
                                  'sx': v[4], 'sy': v[5], 'x': v[6], 'y': v[7],
                                  'prod': data[q + 18], 'missiles': w16(data, q + 19),
                                  'hq_alert': data[q + 21]})
                vdp['sprites'] = {'blink': w16(data, o + 1), 'mech': mechs,
                                  'difficulty': difficulty,
                                  'gauges': gauges, 'group': group}
        return idx, cram, n, mode, [(c[0], c[1]), (c[2], c[3])], vdp
    name = os.path.basename(path)
    n = int(name[3])
    mode = {'split': 0, 'wide': 5, 'wide_p2': 6}[name[5:-4]]
    return idx, cram, n, mode, None, None


def map_words(m):
    """The nametable word every map tile should have in a plane: the block
    word & $19FF, priority on tiles below $40 (Map_DrawBlockRect's rule)."""
    lay = np.frombuffer(m.layout[:0x2000], '>u2').reshape(64, 64) & 0x1FF
    blocks = np.frombuffer(m.blocks[:0x4000], '>u2').reshape(512, 4, 4)
    words = (blocks[lay].transpose(0, 2, 1, 3).reshape(256, 256)
             & 0x19FF).astype(np.int32)
    return np.where((words & 0x1FF) < 0x40, words | 0x8000, words)


def nametable_diffs(m, mode, cams, vdp):
    """Plane cells under each view pixel whose word isn't the map's, as
    (plane, cell column, row, got, want); the count first. The plane cell
    comes from the scroll registers, the map tile from the camera."""
    bad = []
    count = 0
    for x0, width, (cx, cy) in views(mode, cams):
        p = 1 if x0 == 176 else 0
        ys, xs = np.mgrid[32:H, x0:x0 + width]
        px = (xs - vdp['hscroll'][p]) & 511
        py = (ys + vdp['vscroll'][p]) & 255
        cell = ((py >> 3) * 64 + (px >> 3)).ravel()
        tx = ((cx + xs - x0) % 2048 >> 3).ravel()
        ty = ((cy + ys - 32) % 2048 >> 3).ravel()
        plane = np.frombuffer(vdp['planes'][p], '>u2').astype(np.int32)
        got, want = plane[cell], m.words[ty, tx]
        wrong = np.unique(cell[got != want])
        count += len(wrong)
        for c in wrong[:5]:
            i = np.nonzero(cell == c)[0][0]
            bad.append((p, int(c) % 64, int(c) // 64, int(got[i]), int(want[i])))
    return count, bad


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Spr:
    """A sprite as the model places it: screen x, y; size in cells; first
    tile; palette line; priority; flips."""
    def __init__(self, x, y, w, h, tile, pal, prio, hf=False, vf=False):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.tile, self.pal, self.prio, self.hf, self.vf = tile, pal, prio, hf, vf


def model_sprites(rom, m, mode, cams, st, units=()):
    """The divider, outpost bars, the mechs' sprites and the units, front
    to back."""
    split = not (mode & 4)
    out = []
    if split:
        out += [Spr(144, 32 + s * 32, 4, 4, 0x500, 1, True) for s in range(6)]
    mechs = st['mech']
    width = 144 if split else 320          # a view's width
    x0 = [0, 176] if split else [0, 0]     # each player's view (own slot)

    def other_place(mech, q, dx=0):
        """mech seen in player q's view: (screen x, y) or None."""
        rx = s16(mech['x'] + dx - cams[q][0])
        ry = s16(mech['y'] - cams[q][1])
        if -32 <= rx < width and -32 <= ry < 192:
            return rx + x0[q], ry + 32
        return None

    def shown(p):
        return mechs[p]['view'] >= 0 and not (mechs[p]['flags'] & 0x40)

    alerts = []
    for p in (0, 1):
        mech = mechs[p]
        if mech['view'] < 0:
            continue
        team_dx = 176 if split and mech['flags'] & 0x10 else 0
        blink = st['blink']
        if blink >= 0x14 and mech['prod'] in (1, 2):
            x = 56 + team_dx if split else 144
            alerts.append(Spr(x, 34, 3, 3, 0x5C0 if mech['prod'] == 1 else 0x5C9, 1, True))
        if mech['missiles'] and blink & 0x1F >= 8:
            table = 0x5226 if split else 0x5266
            dx = 176 if split and mech['view'] != 0 else 0
            for n in range(16):
                if mech['missiles'] >> n & 1:
                    alerts.append(Spr(w16(rom, table + n * 4) - 128 + dx,
                                      w16(rom, table + n * 4 + 2) - 128, 2, 2, 0x5D2, 1, True))
        if mech['hq_alert'] and blink >= 0x10:
            x = 92 + team_dx if split else 180
            alerts.append(Spr(x, 34, 3, 3, 0x5A3, 1, True))

    bars = []
    for x0_, width_, (cx, cy) in views(mode, cams):
        p = 1 if x0_ == 176 or mode == 6 else 0
        if not shown(p):
            continue
        for ox, oy, h1, h2 in m.outposts:
            ry = s16(oy + 8 - cy)
            if not 0 <= ry < 224:
                continue
            for hold, dx, tile in ((h1, -24, 0x594), (h2, 16, 0x598)):
                rx = s16(ox - cx + dx)
                if hold and -8 <= rx < width_:
                    bars.append(Spr(rx + x0_, ry, 1, hold, tile, 1, True))
    out_bars = bars

    jets = {}          # fixed entries 6-9
    shadows = []       # the list: shadows / debris by player
    robots = []
    for p in (0, 1):
        mech, q = mechs[p], 1 - p
        base, pal = 0x200 + 16 * p, 2 + p
        hf, vf = bool(mech['frame'] & 0x800), bool(mech['frame'] & 0x1000)
        if mech['flags'] & 0x20:
            shadows.append(debris(rom, mode, cams, mechs, p))
            continue
        if mech['form'] != 0:
            alt = min(mech['alt'] + 2, 0x10)
            mine = []
            if shown(p):
                jets[6 + p] = Spr(mech['sx'] + x0[p], mech['sy'], 4, 4, base, pal, True, hf, vf)
                mine.append(Spr(mech['sx'] + x0[p] + alt, mech['sy'], 4, 4, 0x570, 1, True))
            if shown(q):
                at = other_place(mech, q)
                if at:
                    jets[8 + p] = Spr(at[0], at[1], 4, 4, base, pal, True, hf, vf)
                if alt:
                    at = other_place(mech, q, alt)
                    if at:
                        mine.append(Spr(at[0], at[1], 4, 4, 0x570, 1, True))
            shadows.append(mine)
        else:
            mine = []
            if shown(p):
                mine.append(Spr(mech['sx'] + x0[p], mech['sy'], 4, 4, base, pal, False))
            if shown(q):
                at = other_place(mech, q)
                if at:
                    mine.append(Spr(at[0], at[1], 4, 4, base, pal, False))
            robots.append(mine)
    order = (7, 6, 9, 8) if mechs[1]['form'] != 0 else (6, 7, 8, 9)
    out += [jets[e] for e in order if e in jets]
    listed = alerts + out_bars
    for group in shadows + robots:
        listed += group
    listed += unit_sprites(units, mode, cams, st, len(listed))
    return out + listed


def spawn_units(rom, m, mode, difficulty):
    """The units Game_Start leaves (Units_SpawnCPUStart), as a list of 100
    slots (team 1's 50, then team 2's), None where free, and v_grid. From
    the ROM: a CPU player against a human gets its half of the map's
    StartUnits ($52000 + map x $200, P2 + $100; 32 records of flags, type,
    order, dir, x, y; present when flags b7 is set), the GRM records (type
    5) turned into CPUStartTypeTable[difficulty] with order 5 unless that
    is GRM too, each spawned as spawn() does. Its team is the record's
    flags b4."""
    units = [None] * 100
    grid = bytearray(64 * 64)
    p1_cpu, p2_cpu = (mode >> 1) & 1, mode & 1
    if p1_cpu and p2_cpu:
        return units, grid
    new_type = rom[0x9692 + (difficulty & 3)]
    for half, cpu in ((0, p1_cpu), (1, p2_cpu)):
        if not cpu:
            continue
        base = 0x52000 + m.n * 0x200 + half * 0x100
        for i in range(32):
            r = rom[base + i * 8:base + i * 8 + 8]
            if not r[0] & 0x80:
                continue
            typ, order = r[1], r[2]
            if typ == 5 and new_type != 5:
                typ, order = new_type, 5
            spawn(rom, m, grid, units, r[0], typ, order, r[3],
                  s16(w16(r, 4)), s16(w16(r, 6)))
    return units, grid


def new_unit(rom, flags, typ, order, d, x, y):
    """The record Unit_Spawn and Unit_SetSprite leave for a new unit: from
    UnitStats (+$20 a type at $5DC00) hp, size, ammo, fuel, sprite size
    (the size/link word's high byte), speed (unit_speed, its previous
    value and the maximum), turret, fuel use, power, missiles; flags b0
    for the ST-57U; heading, turn-to, turret and aim directions all dir
    (spawned at dir + 1, turned back one step); target velocity speed x
    SineTable[heading (+ 8 when flags b1 is set)], low 16 bits; attr
    UnitDirTable[type][dir] (INFANTRY: $400 + dir x 4) | team palette
    ($4000 / $6000); a turret type (UnitStats +$12 b7) an overlay of
    UnitDirTable[overlay type][dir]."""
    typ &= 15
    d &= 15
    if typ == 3:
        flags |= 1
    st = 0x5DC00 + typ * 32
    team = (flags >> 4) & 1
    pal = 0x6000 if team else 0x4000
    speed = rom[st + 0x11]
    h = (d + 8) & 15 if flags & 2 else d
    sin, cos = s16(w16(rom, 0x25AA + h * 4)), s16(w16(rom, 0x25AA + h * 4 + 2))
    u = {'flags': flags, 'type': typ, 'order': order, 'dir': d, 'turn_to': d,
         'tick': 0, 'power': rom[st + 0x17], 'hp': w16(rom, st + 0xA),
         'size': rom[st + 0xC], 'fuel_use': rom[st + 0x13],
         'x': s16(x), 'y': s16(y), 'missiles': rom[st + 0x1A],
         'ammo': rom[st + 0xD], 'fuel': w16(rom, st + 0xE),
         'cells': rom[st + 0x10], 'sprite_size': rom[st + 0x10] << 8,
         'speed': speed, 'prev_speed': speed, 'max_speed': speed,
         'turret': rom[st + 0x12], 'turret_dir': d, 'aim_dir': d,
         'overlay': 0, 'grid_cell': 0, 'team': team,
         'tvx': (speed * sin) & 0xFFFF, 'tvy': (speed * cos) & 0xFFFF}
    if typ == 8:
        u['attr'] = (0x400 + d * 4) | pal
    else:
        u['attr'] = w16(rom, 0x244A + typ * 32 + d * 2) | pal
    if u['turret'] & 0x80:
        u['overlay'] = w16(rom, 0x244A + (u['turret'] & 0x7F) * 32 + d * 2) | pal
    return u


def spawn(rom, m, grid, units, flags, typ, order, d, x, y):
    """Unit_Spawn: (result 0 / -1, v_hit_flags, v_hit_id, slot). The first
    free slot of the team in flags b4; not placed if the team is full or
    spawn_test finds it blocked. Placed: into units, and its grid id ($40 /
    $C0 | slot + 1) into the cell of its centre, (x, y) + size / 2: row
    (y + s/2) & $FFE0 x 2, column (x + s/2) >> 5 unsigned, & $FFF."""
    team = (flags >> 4) & 1
    slot = next((k for k in range(50) if units[team * 50 + k] is None), None)
    if slot is None:
        return -1, 0, 0, None
    u = new_unit(rom, flags, typ, order, d, x, y)
    hit, hit_id = spawn_test(rom, m, grid, units, u)
    if hit:
        return -1, hit, hit_id, None
    h = u['size'] >> 1
    cell = (((u['y'] + h) & 0xFFE0) * 2 + (((u['x'] + h) & 0xFFFF) >> 5)) & 0xFFF
    u['grid_cell'] = cell
    grid[cell] = (0xC0 if team else 0x40) | (slot + 1)
    units[team * 50 + slot] = u
    return 0, 0, 0, team * 50 + slot


def spawn_test(rom, m, grid, units, u):
    """Unit_CheckSpawnBlocked: (v_hit_flags, v_hit_id). b0 (id = the
    other's grid id): another object's id in the cell of u's centre (row
    (y + s/2) & $7E0 x 2, column ((x + s/2) >> 5) & $3F); or, in the 8
    cells round it (GridNeighbourOffsets; cell numbers 0-$FFF, rows not
    wrapped), the first that overlaps it on both axes (whichever starts
    further right / down starts within the other's size). b1, tested
    either way: a tile under it (3 x 3 tiles from ((x + 4) / 8, (y + 4) /
    8), 2 x 2 for types 8 and 9, the map wrapping at 256 tiles) with b15,
    or for the ST-57U (flags b0) one without b9."""
    hit, hit_id = 0, 0
    h = u['size'] >> 1
    cell = (((u['y'] + h) & 0x7E0) * 2 + ((((u['x'] + h) & 0xFFFF) >> 5) & 0x3F)) & 0xFFF
    if grid[cell]:
        hit, hit_id = 1, grid[cell]
    else:
        for k in range(8):
            c = cell + s16(w16(rom, 0x25EA + k * 2))
            if not 0 <= c < 0x1000 or not grid[c]:
                continue
            o = units[(grid[c] >> 7) * 50 + ((grid[c] - 1) & 0x3F)]
            dx, dy = s16(o['x'] - u['x']), s16(o['y'] - u['y'])
            ox = dx < u['size'] if dx >= 0 else -dx < o['size']
            oy = dy < u['size'] if dy >= 0 else -dy < o['size']
            if ox and oy:
                hit, hit_id = 1, grid[c]
                break
    tx0, ty0 = ((u['x'] + 4) & 0xFFFF) >> 3, ((u['y'] + 4) & 0xFFFF) >> 3
    n = 3 if u['type'] < 8 else 2
    for ty in range(ty0, ty0 + n):
        for tx in range(tx0, tx0 + n):
            blk = w16(m.layout, ((ty & 0xFC) * 32 + (tx & 0xFC) // 2)) & 0x1FF
            word = w16(m.blocks, blk * 32 + (ty & 3) * 8 + (tx & 3) * 2)
            if (not word & 0x200) if u['flags'] & 1 else word & 0x8000:
                return hit | 2, hit_id
    return hit, hit_id


def attr_spr(x, y, cells, attr):
    """A list sprite from its size byte (VDP: w = b2-3 + 1, h = b0-1 + 1)
    and attr word."""
    return Spr(x, y, (cells >> 2 & 3) + 1, (cells & 3) + 1, attr & 0x7FF,
               attr >> 13 & 3, bool(attr & 0x8000), bool(attr & 0x800),
               bool(attr & 0x1000))


def unit_sprites(units, mode, cams, st, count):
    """Sprites_DrawUnits, view A's pass (P1's view shown and its menu
    closed), then view B's (P2's): each unit not carried whose position
    less the pass's camera is within -$20 up to the view's width ($B0
    split, $160 wide; the divider covers the part past the view) - $20
    across and -$20..$BF down, while the list is below $115 entries:
    front to back a marker (2 x 2, $2590) when v_blink_timer < $28 and
    its fuel is 0 or its missiles and ammo both are, its turret overlay,
    the unit. count = list entries ahead of them."""
    split = not (mode & 4)
    width = 0xB0 if split else 0x160
    mechs = st['mech']
    out = []
    for p in (0, 1):
        if mechs[p]['view'] < 0 or mechs[p]['flags'] & 0x40:
            continue
        cx, cy = cams[p]
        x0 = 176 if split and p else 0
        for u in units:
            if u is None or u['flags'] & 0x08:
                continue
            rx, ry = s16(u['x'] - cx), s16(u['y'] - cy)
            if not (0 <= rx + 32 < width and 0 <= ry + 32 < 0xE0) or \
                    count + len(out) >= 0x115:
                continue
            x, y = rx + x0, ry + 32
            if st['blink'] < 0x28 and (u['fuel'] == 0 or
                                       (u['missiles'] == 0 and u['ammo'] == 0)):
                out.append(attr_spr(x, y, 5, 0x2590))
            if u['turret'] & 0x80:
                out.append(attr_spr(x, y, u['cells'], u['overlay']))
            out.append(attr_spr(x, y, u['cells'], u['attr']))
    return out


def debris(rom, mode, cams, mechs, p):
    """Sprites of destroyed mech p's pieces, in list order."""
    mech, q = mechs[p], 1 - p
    r = 0xC0 - abs(s16(mech['hp'] - 0xC0))
    if not 0 <= r < 0x64:
        return []
    split = not (mode & 4)
    width = 144 if split else 320
    # the views of team 1 (A) and team 2 (B): split A = camera 1 at x 0,
    # B = camera 2 at x 176; wide: the shown player's camera, x 0
    if split:
        vcam, vx = [cams[0], cams[1]], [0, 176]
    else:
        c = cams[1] if mode == 6 else cams[0]
        vcam, vx = [c, c], [0, 0]
    team = 1 if mech['flags'] & 0x10 else 0
    views = []
    if mech['view'] >= 0:
        views.append(team)
    if mechs[q]['view'] >= 0 and not (mechs[q]['flags'] & 0x40):
        views.append(1 - team)
    base, pal = 0x200 + 16 * p, 2 + p
    hf, vf = bool(mech['frame'] & 0x800), bool(mech['frame'] & 0x1000)
    out = []
    for n in range(16):
        d = rom[0x4DC4 + n]
        sin = s16(w16(rom, 0x25AA + d * 4))
        cos = s16(w16(rom, 0x25AA + d * 4 + 2))
        x = mech['x'] + 0xC + ((r * sin) >> 4)
        y = mech['y'] + 0xC + ((r * cos) >> 4)
        for v in views:
            rx, ry = s16(x - vcam[v][0]), s16(y - vcam[v][1])
            if -32 <= rx < width and -32 <= ry < 192:
                out.append(Spr(rx + vx[v], ry + 32, 1, 1, base + n, pal, True, hf, vf))
    return out


def sprite_layer(rom, m, sprites, frames):
    """The sprite pixels (index, -1 none) and their priority, front sprite
    on top, lines 32 up, every sprite drawn (session 81)."""
    layer = np.full((H, W), -1, np.int16)
    prio = np.zeros((H, W), bool)
    for i, s in enumerate(sprites):
        img = sprite_image(rom, m, s, frames)
        y0, y1 = max(s.y, 32), min(s.y + s.h * 8, H)
        x0, x1 = max(s.x, 0), min(s.x + s.w * 8, W)
        if y0 >= y1 or x0 >= x1:
            continue
        part = img[y0 - s.y:y1 - s.y, x0 - s.x:x1 - s.x]
        reg = layer[y0:y1, x0:x1]
        put = (reg < 0) & (part > 0)
        reg[put] = s.pal * 16 + part[put]
        prio[y0:y1, x0:x1][put] = s.prio
    return layer, prio


_images = {}


def sprite_image(rom, m, s, frames):
    key = (m.n, s.tile, s.w, s.h, s.hf, s.vf,
           tuple(frames) if 0x200 <= s.tile < 0x220 else None)
    if key not in _images:
        img = np.zeros((s.h * 8, s.w * 8), np.int16)
        for dy in range(s.h * 8):
            for dx in range(s.w * 8):
                sx = s.w * 8 - 1 - dx if s.hf else dx
                sy = s.h * 8 - 1 - dy if s.vf else dy
                tile = s.tile + (sx // 8) * s.h + sy // 8
                img[dy, dx] = vram_tile_px(rom, m, tile, sx % 8, sy % 8,
                                           frames=frames)
        _images[key] = img
    return _images[key]


class Checker:
    def __init__(self, rom):
        self.rom = rom
        self.maps = {}
        self.static = {}
        self.units = {}
        self.hud = HudModel(rom)
        self.anims = {}
        self.cell_cache = {}
        self.begin_run()

    def begin_run(self):
        """A new file or directory of frames: no pass seen yet."""
        self.run = {'first': True, 'known': True, 'shown': [0] * 8,
                    'bars': [None] * 8, 'anim': None}

    def animation(self, m, st):
        """The map animation's model as this frame shows it (after the
        pass whose state st is), or None if its past isn't known. A frame
        with no sprite state comes before any pass: the map as loaded."""
        run = self.run
        if st is None:
            if m.n not in self.anims:
                self.anims[m.n] = Anim.from_rom(self.rom, m.n)
            run['anim'] = copy.deepcopy(self.anims[m.n])
        elif run['anim'] is not None:
            if st.get('group') is None:
                run['anim'] = None
            else:
                run['anim'].frame(st['group'])
        return run['anim']

    def gauges(self, st):
        """The bars this frame shows, as HUD_UpdateGauges and HUD_FlushBars
        left them after the pass whose state st is: per bar its cells' tile
        words, or None for HUDFrameTiles' own; None if not known."""
        run = self.run
        if run['first']:
            run['first'] = False
            if st is not None:
                run['known'] = False    # not a run's first frame
        elif st is not None:
            if st.get('gauges') is None:
                run['known'] = False
            elif run['known']:
                inputs = [(st['mech'][p]['flags'], st['mech'][p]['hp'],
                           st['gauges'][p][2], st['gauges'][p][0],
                           st['gauges'][p][1]) for p in (0, 1)]
                run['shown'], queued = self.hud.update_gauges(run['shown'],
                                                              inputs)
                for k, cells in queued.items():
                    run['bars'][k] = cells
        return run['bars'] if run['known'] else None

    def bar_cells(self, m, bars):
        """(x, y, pixels) for the bars' cells that differ from
        HUDFrameTiles' (all of them when the bars aren't known): pixels 8
        rows of 8, None not checked (transparent, or unknown)."""
        for b, (x0, y0, n) in enumerate(BARS):
            if bars is not None and bars[b] is None:
                continue
            for i in range(n):
                word = None if bars is None else bars[b][i]
                key = (m.n, word)
                if key not in self.cell_cache:
                    px = [[None] * 8 for _ in range(8)]
                    if word is not None:
                        for ty in range(8):
                            for tx in range(8):
                                c = vram_tile_px(self.rom, m, word & 0x7FF, tx,
                                                 ty, bool(word & 0x800),
                                                 bool(word & 0x1000))
                                if c:
                                    px[ty][tx] = ((word >> 13) & 3) * 16 + c
                    self.cell_cache[key] = px
                yield x0 + i * 8, y0, self.cell_cache[key]

    def map(self, n):
        if n not in self.maps:
            m = Map(self.rom, n)
            m.n = n
            if np is not None:
                m.img = map_image(m)
                m.words = map_words(m)
            self.maps[n] = m
        return self.maps[n]

    def check(self, path):
        """(pixels checked, number of pixel diffs, the first few as
        (x, y, got, want), CRAM diffs, nametable diffs (count, first few))"""
        idx, cram, n, mode, cams, vdp = read_raw(path)
        m = self.map(n)
        if cams is None:
            cams = start_cameras(m, mode)
        if (n, mode) not in self.static:
            exp, exp_cram = expected(self.rom, m, mode)
            if np is not None:
                exp = np.array([-1 if e is None else e for e in exp],
                               np.int16).reshape(H, W)
            self.static[(n, mode)] = (exp, exp_cram)
        exp, exp_cram = self.static[(n, mode)]
        st = vdp['sprites'] if vdp is not None else None
        anim = self.animation(m, st)
        exp_cram = list(exp_cram)
        if anim is not None:
            exp_cram[:16] = anim.cram[:16]
            cdiff = [i for i in range(64) if cram[i] != exp_cram[i]]
        else:
            cdiff = [i for i in range(64) if cram[i] != exp_cram[i]
                     and i not in (2, 11)]
        if not hasattr(m, "slots"):
            m.slots = {rec[0] & 0x1FF for rec in Anim.records(m)} - {0}
        slots = m.slots
        bars = self.gauges(st)

        if np is not None:
            exp = exp.copy()
            for x, y, px in self.bar_cells(m, bars):
                exp[y:y + 8, x:x + 8] = [[-1 if v is None else v for v in row]
                                         for row in px]
            tiles = tile_array(anim.tiles()) if anim is not None else None
            for x0, width, (cx, cy) in views(mode, cams):
                rows = (cy + np.arange(H - 32)) % 2048
                cols = (cx + np.arange(width)) % 2048
                if tiles is not None:
                    exp[32:, x0:x0 + width] = view_pixels(m, tiles, cx, cy,
                                                          width)
                else:
                    sub = m.img[np.ix_(rows, cols)].copy()
                    words = m.words[np.ix_(rows >> 3, cols >> 3)] & 0x1FF
                    sub[np.isin(words, list(slots))] = -1
                    exp[32:, x0:x0 + width] = sub
            if st is None:
                split = not (mode & 4)
                if split:
                    xs = (56, 232)
                elif mode == 6:
                    xs = (-127, 144)
                else:
                    xs = (144, -127)
                jets = [Spr(xs[0], 112, 4, 4, 0x200, 2, True),
                        Spr(xs[1], 112, 4, 4, 0x210, 3, True)]
                layer, _ = sprite_layer(self.rom, m, jets, [0x30, 0x30])
                exp[layer >= 0] = layer[layer >= 0]
            if st is not None:
                units = ()
                if st['difficulty'] is not None:
                    key = (n, mode, st['difficulty'])
                    if key not in self.units:
                        self.units[key] = spawn_units(self.rom, m, mode,
                                                      st['difficulty'])[0]
                    units = self.units[key]
                sprites = model_sprites(self.rom, m, mode, cams, st, units)
                frames = [st['mech'][p]['frame'] for p in (0, 1)]
                layer, sprio = sprite_layer(self.rom, m, sprites, frames)
                for x0, width, (cx, cy) in views(mode, cams):
                    ty = ((cy + np.arange(H - 32)) % 2048) >> 3
                    tx = ((cx + np.arange(width)) % 2048) >> 3
                    high = (m.words[np.ix_(ty, tx)] & 0x8000) != 0
                    sub = exp[32:, x0:x0 + width]
                    lay = layer[32:, x0:x0 + width]
                    show = (lay >= 0) & (sprio[32:, x0:x0 + width]
                                         | ~(high & (sub != BACKDROP)))
                    sub[show] = lay[show]
            got = np.frombuffer(idx, np.uint8).reshape(H, W).astype(np.int16)
            mask = exp >= 0
            ys, xs = np.nonzero(mask & (got != exp))
            diffs = [(int(x), int(y), int(got[y, x]), int(exp[y, x]))
                     for y, x in zip(ys[:5], xs[:5])]
            ntd = (0, [])
            if vdp is not None:
                ntd = nametable_diffs(m, mode, cams, vdp)
            return int(mask.sum()), len(ys), diffs, cdiff, ntd

        exp = list(exp)
        for x, y, px in self.bar_cells(m, bars):
            for ty in range(8):
                for tx in range(8):
                    exp[(y + ty) * W + x + tx] = px[ty][tx]
        tiles = anim.tiles() if anim is not None else None
        for x0, width, (cx, cy) in views(mode, cams):
            for y in range(32, H):
                for x in range(x0, x0 + width):
                    mx, my = cx + x - x0, cy + y - 32
                    if tiles is None and m.word(mx, my) & 0x1FF in slots:
                        exp[y * W + x] = None
                    else:
                        exp[y * W + x] = m.px(mx, my, tiles)
        bad = [i for i in range(W * H) if exp[i] is not None and exp[i] != idx[i]]
        diffs = [(i % W, i // W, idx[i], exp[i]) for i in bad[:5]]
        return sum(e is not None for e in exp), len(bad), diffs, cdiff, (0, [])


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    rom = open(sys.argv[1], 'rb').read()
    ck = Checker(rom)
    bad = 0
    for arg in sys.argv[2:]:
        ck.begin_run()
        if os.path.isdir(arg):
            files = sorted(os.path.join(arg, f) for f in os.listdir(arg)
                           if f.endswith('.raw'))
            if not files:
                raise SystemExit('no .raw files in ' + arg)
        else:
            files = [arg]
        # A few files: a line each. A directory of frames: one line.
        each = len(files) <= 30
        total = frames_bad = 0
        for path in files:
            checked, ndiff, diffs, cdiff, (nnt, ntd) = ck.check(path)
            total += checked
            wrong = bool(ndiff or cdiff or nnt)
            frames_bad += wrong
            if each or wrong:
                print('%-16s %s: %d pixels checked, %d differ; CRAM %d differ%s'
                      % (os.path.basename(path), 'MISMATCH' if wrong else 'ok',
                         checked, ndiff, len(cdiff),
                         '; %d cells differ' % nnt if nnt else ''))
                for x, y, g, w in diffs:
                    print('    (%d,%d) got %d want %d' % (x, y, g, w))
                for p, c, r, g, w in ntd:
                    print('    plane %s cell (%d,%d) got $%04X want $%04X'
                          % ('AB'[p], c, r, g, w))
            if wrong and frames_bad >= 5 and not each:
                print('    ... stopping at 5 bad frames')
                break
        if not each:
            print('%s: %d frames, %d pixels checked, %s'
                  % (arg, len(files), total,
                     'ok' if not frames_bad else '%d MISMATCHED' % frames_bad))
        bad += frames_bad
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
