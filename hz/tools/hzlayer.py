"""(Half B of docs/hackable.md's step 1) The original's side of the rule
tools/video_vdp.c keeps for the port: what the checks compare of the
cartridge's VDP, now that the port's draw layer (src/core/video.h) keeps
no sprite table.

VRAM is compared with the sprite tables' bytes as 0. A byte is a table's
(held) from when register 5 puts its table in use ($68: $D000-$D27F, the
game's; $6C: $D800-$DA7F, the menus') until it is written while it lies
outside the table in use; a byte written inside the table in use is held.

The sprites are compared as they are drawn (sprites_crc): the table in
use, walked by its links from sprite 0 (80 at most, to a link of 0 or past
79); for each sprite with a line on the screen at or below the first its
group is drawn from: its x and y (screen pixels, big-endian), its width
and height in tiles, its cell word, its colour table (the size word's top
four bits: 0 on the cartridge) and that first line, CRC-32 of them all.
The first line is 0, but for the sprites after sprite 5 when sprite 5
ended the list until HBlank linked the rest on (the HUD strip holding the
game's sprites back): line 32.
"""

import struct
import zlib

TABLES = {0x68: 0xD000, 0x6C: 0xD800}
TABLE_BYTES = 0x280
SPRITES = 80
HUD_LINES = 32
SCREEN_H = 224


class Held:
    """Which bytes of VRAM are a sprite table's. The VDP model calls
    wrote(addr) for each byte it writes and table(value) for each write of
    register 5."""

    def __init__(self, reg5=0):
        self.mask = bytearray(0x10000)
        self.lo = self.hi = -1
        self.table(reg5)

    def table(self, reg5):
        at = TABLES.get(reg5)
        if at is None:
            self.lo = self.hi = -1
            return
        self.lo, self.hi = at, at + TABLE_BYTES
        self.mask[self.lo:self.hi] = b'\x01' * TABLE_BYTES

    def wrote(self, a):
        self.mask[a] = self.lo <= a < self.hi

    def vram(self, vram):
        """VRAM as the checks compare it: the held bytes as 0."""
        out = bytearray(vram)
        for at in TABLES.values():
            for a in range(at, at + TABLE_BYTES):
                if self.mask[a]:
                    out[a] = 0
        return out

    def crc(self, vram):
        return zlib.crc32(bytes(self.vram(vram))) & 0xFFFFFFFF


def link5(vram, reg5):
    """Sprite 5's link in the table in use; None without one."""
    at = TABLES.get(reg5)
    if at is None:
        return None
    return vram[at + 5 * 8 + 3] & 0x7F


def sprites(vram, reg5, cut):
    """The sprites as drawn: (x, y, w, h, cell word, colours, first line)
    each. `cut`: sprite 5 ended the list before HBlank (link5 was 0)."""
    at = TABLES.get(reg5)
    out = []
    if at is None:
        return out
    i = 0
    first = 0
    for _ in range(SPRITES):
        o = at + i * 8
        w0, w1, w2, w3 = struct.unpack('>4H', bytes(vram[o:o + 8]))
        x = (w3 & 0x1FF) - 128
        y = (w0 & 0x1FF) - 128
        w = ((w1 >> 10) & 3) + 1
        h = ((w1 >> 8) & 3) + 1
        if y + h * 8 > first and y < SCREEN_H:
            out.append((x, y, w, h, w2, w1 >> 12, first))
        if i == 5 and cut:
            first = HUD_LINES
        link = w1 & 0x7F
        if link == 0 or link >= SPRITES:
            break
        i = link
    return out


def sprites_crc(vram, reg5, cut):
    b = b''.join(struct.pack('>hhBBHBH', *s)
                 for s in sprites(vram, reg5, cut))
    return zlib.crc32(b) & 0xFFFFFFFF
