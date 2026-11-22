#!/usr/bin/env python3
"""Checks tools/hz_anim's log against a model of the map animation,
written from the 68k code, not from the C:

  Map_AnimTilesUpdate ($5738)  on v_unit_ai_group 0 or 6 only. Per record
                               r: its queue entry's two flag bytes
                               cleared (clr.w); nothing more when
                               v_anim_tile_step has b7. The tile is the
                               word at the step, & $1FF; if it is
                               v_anim_tile_shown nothing is queued, else
                               shown = tile and: tile = word 0 (cmp.w:
                               the whole word) queues a CPU write
                               (flags 1 0, source v_anim_tile_gfx + r x
                               32, command (word 0 << 5 & $3FFF | $4000)
                               << 16); any other a VRAM copy (flags 1 1,
                               source register words $95 / $96 with
                               the tile x 32's low / high byte, the
                               command's first word word 0 << 5 & $3FFF
                               in the high half, the low half untouched).
                               Then the step: 15 -> 0; the next word &
                               $1FF = 0 -> 0; else + 1
  Map_AnimTilesUpload ($5812)  on the same groups. Each entry with byte
                               0 set, in order: byte 1 clear, 32 bytes of
                               the picture at the command's VRAM address;
                               set, 32 bytes copied one at a time from
                               the source registers' address to the
                               command word's (registers 1 and 15 as the
                               routine leaves them). Then registers 15 =
                               2, 1 = $24; each v_colour_cycle word + 1
                               and its ColourCycles colour (& $F) to the
                               CRAM address in ColourCycleCRAM

The queue is compared by what Map_AnimTilesUpload reads of it (job_read):
an entry not pending, nothing more; a copy's command, its first word
(the port keeps the jobs by what they do, and logs them in the 68k's
form, so the parts the 68k leaves as they were and never reads aren't
there to compare).

Anim is also the model tools/hzview_check.py and tools/hzloop_check.py
carry through their frames (Anim.from_rom: a map as Game_LoadMap and
Map_InitAnimTiles leave it).

  hzanim_check.py ROM LOG
"""

import sys
import zlib

import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hzlzw  # noqa: E402

COLOUR_CYCLES = 0x58E6      # ColourCycles: 2 x 16 CRAM words
COLOUR_CYCLE_CRAM = 0x5926  # ColourCycleCRAM: two CRAM write commands
GFX_RAM = 0xFF4800          # v_anim_tile_gfx


def w16(b, o):
    return (b[o] << 8) | b[o + 1]


def lzw(rom, base, n):
    src, ln = hzlzw.table_entry(rom, base, n)
    return bytes(hzlzw.decompress(rom[src:src + ln], ln))


class Anim:
    """The records, the state Map_InitAnimTiles and the two routines keep,
    the queue, and as much of the VDP as they touch: VRAM (all of it),
    CRAM, registers 1 and 15."""

    def __init__(self, rom, records, gfx, vram, cram):
        self.colours = [[w16(rom, COLOUR_CYCLES + (k * 16 + i) * 2)
                         for i in range(16)] for k in (0, 1)]
        self.cram_at = []
        for k in (0, 1):
            cmd = (w16(rom, COLOUR_CYCLE_CRAM + k * 4) << 16
                   | w16(rom, COLOUR_CYCLE_CRAM + k * 4 + 2))
            self.cram_at.append((cmd >> 16) & 0x7F)   # CRAM byte address
        self.rec = [list(r) for r in records]
        self.gfx = [bytes(g) for g in gfx]
        self.vram = bytearray(vram)
        self.cram = list(cram)
        self.shown = [0] * 32
        self.step = [0] * 32
        self.queue = [[0, 0, 0, 0] for _ in range(32)]
        self.cyc = [0, 0]
        self.reg1 = 0
        self.reg15 = 0
        self.counts = {"cpu": 0, "copy": 0, "wrap": 0, "end": 0,
                       "off": 0, "word0_high": 0, "frames": 0}

    @staticmethod
    def records(m):
        """A map's 32 records, from its blocks entry as unpacked
        (hzview_check's Map.blocks)."""
        return [[w16(m.blocks, 0x4400 + r * 32 + i * 2) for i in range(16)]
                for r in range(32)]

    @classmethod
    def from_rom(cls, rom, n):
        """Map n as the game leaves it before Game_Start: VRAM tiles from
        the $22400 entry, records from the $3F000 entry's $4400 part, each
        record's picture kept, shown = word 0's tile, step $80 if that is
        0; CRAM line 0 the map's palette (where the fade ends)."""
        blocks = lzw(rom, 0x3F000, n)
        tiles = lzw(rom, 0x22400, n)
        records = [[w16(blocks, 0x4400 + r * 32 + i * 2) for i in range(16)]
                   for r in range(32)]
        gfx = [tiles[(rec[0] & 0x1FF) * 32:(rec[0] & 0x1FF) * 32 + 32]
               for rec in records]
        vram = bytearray(0x10000)
        vram[:len(tiles)] = tiles
        cram = [w16(rom, 0x73F00 + n * 32 + i * 2) for i in range(16)]
        a = cls(rom, records, gfx, vram, cram + [0] * 48)
        for r, rec in enumerate(records):
            a.shown[r] = rec[0] & 0x1FF
            a.step[r] = 0x80 if rec[0] & 0x1FF == 0 else 0
        return a

    def update(self, group):
        if group not in (0, 6):
            return
        for r in range(32):
            q = self.queue[r]
            q[0] = q[1] = 0
            st = self.step[r]
            if st & 0x80:
                self.counts["off"] += 1
                continue
            rec = self.rec[r]
            tile = rec[st] & 0x1FF
            if tile != self.shown[r]:
                self.shown[r] = tile
                if tile == rec[0]:
                    q[0], q[1] = 1, 0
                    q[2] = GFX_RAM + r * 32
                    q[3] = ((rec[0] << 5) & 0x3FFF | 0x4000) << 16
                    self.counts["cpu"] += 1
                else:
                    q[0], q[1] = 1, 1
                    a = tile << 5
                    q[2] = (0x9500 | (a & 0xFF)) << 16 | 0x9600 | (a >> 8)
                    q[3] = ((rec[0] << 5) & 0x3FFF) << 16 | (q[3] & 0xFFFF)
                    self.counts["copy"] += 1
                    if tile == rec[0] & 0x1FF:
                        self.counts["word0_high"] += 1   # onto itself
            if st == 15:
                self.step[r] = 0
                self.counts["wrap"] += 1
            elif rec[st + 1] & 0x1FF == 0:
                self.step[r] = 0
                self.counts["end"] += 1
            else:
                self.step[r] = st + 1

    def upload(self, group):
        if group not in (0, 6):
            return
        for r in range(32):
            pending, dma, src, cmd = self.queue[r]
            if pending & 0xFF == 0:
                continue
            if dma & 0xFF == 0:
                self.reg15, self.reg1 = 2, 0x24
                addr = (cmd >> 16) & 0x3FFF | (cmd & 3) << 14
                pic = self.gfx[((src - GFX_RAM) >> 5) & 31]
                for i in range(0, 32, 2):
                    self.vram[(addr + i) & 0xFFFE] = pic[i]
                    self.vram[((addr + i) & 0xFFFE) + 1] = pic[i + 1]
            else:
                self.reg1, self.reg15 = 0x34, 1
                s = ((src & 0xFF) << 8) | ((src >> 16) & 0xFF)  # $96, $95
                d = (cmd >> 16) & 0x3FFF
                for i in range(32):
                    self.vram[d] = self.vram[s]
                    s = (s + 1) & 0xFFFF
                    d = (d + 1) & 0xFFFF
        self.reg15, self.reg1 = 2, 0x24
        for k in (0, 1):
            self.cyc[k] = (self.cyc[k] + 1) & 0xFFFF
            self.cram[self.cram_at[k] >> 1] = self.colours[k][self.cyc[k] & 15]
        self.counts["frames"] += 1

    def frame(self, group):
        """A frame's pair, as Game_FrameLoop runs them."""
        self.update(group)
        self.upload(group)

    def tiles(self):
        return self.vram[:0x4000]

    def cram_crc(self):
        return zlib.crc32(bytes(b for w in self.cram for b in (w >> 8, w & 0xFF)))


def fill(n, seed):
    vram = bytearray(((i * 0x9E3779B1 + seed * 0x9E37) & 0xFFFFFFFF) >> 24
                     for i in range(n))
    cram = [(i * 0x9E37 + seed) & 0x0EEE for i in range(64)]
    return vram, cram


def state_text(a):
    return ("".join("%02X" % s for s in a.step),
            "".join("%04X" % s for s in a.shown),
            "%X" % a.cyc[0], "%X" % a.cyc[1])


def job_read(q):
    """What Map_AnimTilesUpload reads of a queue entry: nothing of one not
    pending; of a copy, the source and the command's first word."""
    pending, dma, src, cmd = q
    if pending & 0xFF == 0:
        return (0,)
    if dma & 0xFF == 0:
        return (1, 0, src, cmd)
    return (1, 1, src, cmd & 0xFFFF0000)


def check(rom, path):
    with open(path) as f:
        lines = [ln.split() for ln in f if ln.strip()]
    if not lines or lines[0][0] != "HZA1":
        return ["not an hz_anim log"], {}, 0
    errors = []
    totals = {}
    passes = 0
    i = 1
    while i < len(lines):
        ln = lines[i]
        if ln[0] != "R":
            errors.append("line %d: expected R" % (i + 1))
            break
        run, seed = int(ln[1], 16), int(ln[2], 16)
        recs = [[int(w, 16) for w in lines[i + 1 + r][2:18]] for r in range(32)]
        gfx = [bytes.fromhex(lines[i + 33 + r][2]) for r in range(32)]
        vram, cram = fill(0x10000, seed)
        a = Anim(rom, recs, gfx, vram, cram)
        for r in range(32):
            a.queue[r] = [int(v, 16) for v in lines[i + 65 + r][2:6]]
        s = lines[i + 97]
        a.step = [int(s[1][k:k + 2], 16) for k in range(0, 64, 2)]
        a.shown = [int(s[2][k:k + 4], 16) for k in range(0, 128, 4)]
        a.cyc = [int(s[3], 16), int(s[4], 16)]
        i += 98
        while i < len(lines) and lines[i][0] == "P":
            group, reg1, reg15 = (int(v, 16) for v in lines[i][1:4])
            a.reg1, a.reg15 = reg1, reg15
            a.update(group)
            for r in range(32):
                got = [int(v, 16) for v in lines[i + 1 + r][2:6]]
                if job_read(got) != job_read(a.queue[r]):
                    errors.append("run %d pass %d: queue %d %s, model %s"
                                  % (run, passes, r,
                                     " ".join("%X" % v for v in got),
                                     " ".join("%X" % v for v in a.queue[r])))
                    a.queue[r] = got
            a.upload(group)
            e = lines[i + 33]
            want = state_text(a) + ("%X" % a.reg1, "%X" % a.reg15,
                                    "%X" % zlib.crc32(bytes(a.vram)),
                                    "%X" % a.cram_crc())
            names = ("steps", "shown", "cyc0", "cyc1", "reg1", "reg15",
                     "vram", "cram")
            for name, g, w in zip(names, e[1:], want):
                if g != w:
                    errors.append("run %d pass %d: %s %s, model %s"
                                  % (run, passes, name, g, w))
            if any(g != w for g, w in zip(e[1:], want)):
                # carry on from the port's state, so one fault is one error
                a.step = [int(e[1][k:k + 2], 16) for k in range(0, 64, 2)]
                a.shown = [int(e[2][k:k + 4], 16) for k in range(0, 128, 4)]
            passes += 1
            i += 34
            if len(errors) > 20:
                return errors, totals, passes
        for k, v in a.counts.items():
            totals[k] = totals.get(k, 0) + v
    return errors, totals, passes


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    rom = open(argv[1], "rb").read()
    errors, c, passes = check(rom, argv[2])
    if errors:
        print("%s: %d errors" % (argv[2], len(errors)))
        for e in errors[:12]:
            print("   ", e)
        return 1
    print("%s ok: %d passes, %d frames animated: %d CPU writes, %d VRAM "
          "copies (%d of a word 0 with high bits onto itself), %d wraps "
          "after 15, %d ends at a 0 word, %d off records"
          % (argv[2], passes, c["frames"], c["cpu"], c["copy"],
             c["word0_high"], c["wrap"], c["end"], c["off"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
