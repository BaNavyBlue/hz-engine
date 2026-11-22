#!/usr/bin/env python3
"""A model of the shots, written from the 68k code: Shot_FireBullet ($ED2),
Shot_FireMissile ($FF4), Shot_SetVelocity ($10F4), Combat_Update
($111E) with Shots_UpdateTeam ($14A4) and (tools/hzunitmove_check.py's
model) Units_UpdateTeam, Obj_FromGridId ($18E6), Shot_CheckHit
($1E12), GetDirection ($21B8) and TerrainEv_ShotHQ ($8CD4).

`Combat` works on dicts: the two mechs (hzmech_check's MECH_FIELDS names:
flags carried hp size x y ammo missiles power altitude form vel_x vel_y
shot_flags hit_victim missile_dirs, positions as u16), the unit records
(hzview_check.new_unit's names, None for an empty slot), v_grid, the
shots (SHOT_FIELDS, owner as the port has it: a unit index, or 100 + p),
the HQ records' HP and alerts, and a globals dict (difficulty, boost,
sfx_queued, sfx_priority). It is shared by tools/hzmech_check.py (the
game's own shots) and this file's check of tools/hz_shots' log (made-up
states: missiles, units' shots, HQ hits, owners gone, every boost).

  hzshots_check.py ROM FILE...
"""

import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hzview_check as hv   # noqa: E402
import hzunitmove_check as hzu  # noqa: E402

SHOT_FIELDS = ("flags dir x y speed sub_x sub_y life attr vel_x vel_y aim_x "
               "aim_y power owner").split()

SINE = 0x25AA
NEIGHBOURS = 0x25EA
DIR_SLOPES = 0x2210
EXPLOSION_FRAMES = 0x258A
SFX_PRIORITY = 0x9931
OWNER_MECH = 100


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def get_direction(x0, y0, x1, y1, rom):
    """GetDirection: 0-$F from (x0, y0) to (x1, y1)."""
    dx = s16(x1 - x0)
    dy = s16(y1 - y0)
    if dx == 0:
        return 8 if dy >= 0 else 0
    if dy == 0:
        return 4 if dx >= 0 else 0xC
    num, den = dy, dx
    if dx < 0:
        den = s16(-dx)
        num = -num
    num = (num << 4)
    num = (num & 0xFFFFFFFF)
    num = num - (1 << 32) if num & 0x80000000 else num
    q = int(num / den)                  # divs truncates toward 0
    slope = q if -0x8000 <= q <= 0x7FFF else s16(num)
    d = 0
    for i in range(8):
        if slope <= s16(hv.w16(rom, DIR_SLOPES + i * 2)):
            break
        d += 1
    if dx < 0:
        d += 8
    return d & 0xF


class Combat:
    def __init__(self, rom, tile, block_terrain):
        """tile(tx, ty) -> (tile word, layout word);
        block_terrain(layout word) -> the block's terrain word."""
        self.rom = rom
        self.tile = tile
        self.block_terrain = block_terrain
        self.units = hzu.Units(rom, tile, block_terrain)
        self.seen = {}

    def count(self, key, n=1):
        self.seen[key] = self.seen.get(key, 0) + n

    def sfx(self, g, n):
        pri = self.rom[SFX_PRIORITY + n]
        if pri > g["sfx_priority"]:
            return
        g["sfx_priority"] = pri
        g["sfx_queued"] = n

    def sine(self, d):
        return (s16(hv.w16(self.rom, SINE + d * 4)),
                s16(hv.w16(self.rom, SINE + d * 4 + 2)))

    # -- objects ---------------------------------------------------------
    # A mech's and a unit's fields under the shot code's names.
    MECH = {"type": "carried", "air": "altitude", "w2c": "form"}
    UNIT = {"type": "type", "air": "raw_0d", "w2c": "overlay"}

    def obj(self, S, ident):
        """(record, is_mech) for an owner/target id."""
        if ident >= OWNER_MECH:
            return S["mechs"][ident - OWNER_MECH], True
        u = S["units"][ident]
        if u is None:
            u = S["units"][ident] = {"flags": 0}
        return u, False

    def get(self, rec, mech, f):
        f = (self.MECH if mech else self.UNIT).get(f, f)
        return rec.get(f, 0)

    @staticmethod
    def pos(rec, f):
        return rec.get(f, 0) & 0xFFFF

    @staticmethod
    def set_pos(rec, mech, f, v):
        rec[f] = (v & 0xFFFF) if mech else s16(v)

    @staticmethod
    def from_grid_id(ident):
        if not ident & 0x40:
            return OWNER_MECH + (1 if ident & 0x80 else 0)
        i = (50 if ident & 0x80 else 0) + ((ident - 1) & 0x3F)
        return i if i < 100 else None

    # -- firing ------------------------------------------------------------
    def power(self, g, mechs, p, team_flags, shooter_flags):
        if g["difficulty"] < 2:
            if not g["boost"] & 0x8000:
                return p
            if ((g["boost"] & 0xFF) ^ team_flags) & 0x10:
                return p
            return min(p + (p >> 1), 0xFF)
        own, other = mechs[0], mechs[1]
        if shooter_flags & 0x10:
            own, other = other, own
        if other["flags"] & 1 or not own["flags"] & 1:
            return p
        if g["difficulty"] == 2:
            return min(p + (p >> 1), 0xFF)
        return min(p * 2, 0xFF)

    def set_velocity(self, s, d):
        sx, sy = self.sine(d & 0xF)
        sp = s16(s["speed"])
        s["vel_x"] = s["aim_x"] = (sp * sx) & 0xFFFF
        s["vel_y"] = s["aim_y"] = (sp * sy) & 0xFFFF

    def alloc(self, S, flags, first, count, rec, field):
        base = 32 if flags & 0x10 else 0
        for i in range(count):
            s = S["shots"][base + first + i]
            if not s["flags"] & 0x80:
                break
        else:
            return None
        if rec.get(field, 0) == 0:
            return None
        rec[field] -= 1
        for f in SHOT_FIELDS:
            s[f] = 0
        return s

    def fire(self, S, g, shooter, flags, d, x, y, speed, life, missile):
        """Shot_FireBullet / Shot_FireMissile -> 0, or -1."""
        rec, mech = self.obj(S, shooter)
        if missile:
            s = self.alloc(S, flags, 24, 8, rec, "missiles")
        else:
            s = self.alloc(S, flags, 0, 24, rec, "ammo")
        if s is None:
            return -1
        if missile:
            flags |= 4
        elif self.get(rec, mech, "air"):
            flags |= 4
        s.update(flags=flags, dir=d, x=x & 0xFFFF, y=y & 0xFFFF,
                 speed=speed, life=life, owner=shooter)
        if missile:
            s["attr"] = 0xA550 + (0x10 if flags & 0x10 else 0)
            pw = rec.get("power", 0)
            pw = (pw + ((pw * 2) & 0xFF)) & 0xFF
            s["power"] = self.power(g, S["mechs"], pw, rec["flags"],
                                    rec["flags"])
        else:
            a = 0x254C + (1 if flags & 0x10 else 0)
            if flags & 4:
                a += 0x8002
            s["attr"] = a & 0xFFFF
            s["power"] = self.power(g, S["mechs"], rec.get("power", 0),
                                    flags, rec["flags"])
        self.set_velocity(s, d)
        if missile:
            self.sfx(g, 3)
        elif rec.get("size", 0) == 0x20:
            self.sfx(g, 1 if self.get(rec, mech, "w2c") else 0)
        elif rec["flags"] & 0x40:
            self.sfx(g, 2)
        self.count("missiles fired" if missile else "bullets fired")
        return 0

    # -- Shot_CheckHit -------------------------------------------------------
    def check_hit(self, S, g, s):
        g["hit_flags"] = 0
        g["hit_id"] = 0
        g["event"] = 0
        px = (s["x"] + 4) & 0xFFFF
        py = (s["y"] + 4) & 0xFFFF
        cell = ((((py & 0x7E0) << 1) + ((px >> 5) & 0x3F)) & 0xFFF)
        for n in range(9):
            off = 0 if n == 0 else s16(hv.w16(self.rom, NEIGHBOURS + (n - 1) * 2))
            c = (cell + off) & 0xFFFF
            if c >= 0x1000:
                continue
            ident = S["grid"][c]
            if ident == 0:
                continue
            if not ident & 0x40:
                kind = 0x10 if ident & 0x80 else 0
            else:
                kind = 0x18 if ident & 0x80 else 0x08
            o = self.from_grid_id(ident)
            if o is None:
                continue
            sides = s["flags"] & 0x18
            d6 = kind ^ sides
            if d6 == 0:
                continue
            if sides & 8 and not d6 & 0x10:
                continue
            rec, mech = self.obj(S, o)
            size = rec.get("size", 0)
            tx, ty = self.pos(rec, "x"), self.pos(rec, "y")
            if tx >= px or ((px - tx) & 0xFFFF) >= size:
                continue
            if ty >= py or ((py - ty) & 0xFFFF) >= size:
                continue
            g["hit_flags"] |= 1
            g["hit_id"] = ident
            break
        tx, ty = px >> 3, py >> 3
        g["event2"] = (((ty & 0xFC) << 5) + ((tx & 0xFC) >> 1)) & 0xFFFF
        word, lw = self.tile(tx, ty)
        if word & 0x4000:
            g["hit_flags"] |= 2
        if word & 0x2000:
            g["event"] = self.block_terrain(lw)
            g["hit_flags"] |= 4

    # -- TerrainEv_ShotHQ ---------------------------------------------------
    def terrain_event(self, S, g, s):
        ev = g["event"]
        if ev & 0xF != 7:
            if ev & 0xF in (1, 5):
                self.count("shot events 1/5")
            return
        if not s["flags"] & 8:
            return
        if not (((s["flags"] << 4) ^ ev) & 0x100):
            return
        side = 1 if ev & 0x100 else 0
        own, other = S["mechs"][side], S["mechs"][side ^ 1]
        d1 = s["power"] >> 1
        if g["difficulty"] < 2:
            if g["boost"] & 0x8000 and ((g["boost"] & 0xFF) ^ s["flags"]) & 0x10:
                d1 -= d1 >> 2
        elif not other["flags"] & 1 and own["flags"] & 1:
            if g["difficulty"] == 2:
                d1 -= d1 >> 2
            else:
                d1 >>= 1
                if d1 == 0:
                    d1 = 1
        hp = S["hq_hp"][side]
        S["hq_hp"][side] = hp - d1 if hp >= d1 else 0
        if S["hq_alert"][side] == 0:
            S["hq_alert"][side] = 0xB4
        if not s["flags"] & 1:
            rec, mech = self.obj(S, s["owner"])
            rec["hit_victim"] = 0x66
            rec["shot_flags"] = rec.get("shot_flags", 0) | 0x40
        self.sfx(g, 0x17)
        self.count("HQ hits")

    # -- Shots_UpdateTeam ------------------------------------------------------
    @staticmethod
    def move(s, pos, sub, step):
        v = (s[sub] + step) & 0xFFFF
        whole = v >> 8
        whole = whole - 0x100 if whole & 0x80 else whole
        s[sub] = v & 0xFF
        s[pos] = (s[pos] + whole) & 0xFFFF

    def hit(self, S, g, s, own, ident, target):
        t, tmech = self.obj(S, target)
        if t["flags"] & 0x20:
            self.count("hits on the dying")
            return
        t["shot_flags"] = 0x80 | (s["dir"] & 0xF)
        if t.get("hp", 0) >= s["power"]:
            t["hp"] = t.get("hp", 0) - s["power"]
            self.sfx(g, 8 if t.get("size", 0) == 0x20 else 7)
            self.count("hits")
        else:
            hp, n = 0x180, 4
            if ident & 0x40:
                hp, n = 0x10, 5
                typ = self.get(t, tmech, "type")
                if typ == 8:
                    n = 6
                elif typ == 9:
                    self.set_pos(t, tmech, "x", self.pos(t, "x") - 4)
                    self.set_pos(t, tmech, "y", self.pos(t, "y") - 4)
                    t["sprite_size"] = 0xA00
                    t["attr"] = hv.w16(self.rom, EXPLOSION_FRAMES)
            t["flags"] |= 0x20
            t["hp"] = hp
            self.sfx(g, n)
            self.count("kills")
        if not s["flags"] & 1:
            own["shot_flags"] = own.get("shot_flags", 0) | 0x40
            if ident & 0x40:
                own["hit_victim"] = ((ident & 0x3F) + (0x32 if ident & 0x80 else 0)) & 0xFF
                return
        else:
            self.count("hits with the owner gone")
        own["hit_victim"] = 0x65 if ident & 0x80 else 0x64

    def explode(self, s, own, base):
        if base == 0xA580 and not s["flags"] & 1:
            own["shot_flags"] = own.get("shot_flags", 0) & ~0x20
        s["flags"] |= 0x20
        s["life"] = 4
        s["x"] = (s["x"] - 4) & 0xFFFF
        s["y"] = (s["y"] - 4) & 0xFFFF
        s["attr"] = (base + (4 - s["life"]) * 4) & 0xFFFF

    @staticmethod
    def exploding(s, base):
        s["life"] = (s["life"] - 1) & 0xFFFF
        if s["life"] == 0:
            s["flags"] = 0
            return
        s["attr"] = (base + ((4 - s["life"]) & 0xFFFF) * 4) & 0xFFFF

    @staticmethod
    def steer(cur, aim):
        cur, aim = s16(cur), s16(aim)
        if aim == cur:
            return cur & 0xFFFF
        if aim > cur:
            n = s16(cur + 0x40)
            return (n if aim > n else aim) & 0xFFFF
        n = s16(cur - 0x40)
        return (n if aim < n else aim) & 0xFFFF

    def update_team(self, S, g, team):
        shots = S["shots"][team * 32:team * 32 + 32]
        e = S["mechs"][team ^ 1]
        ex, ey = e["x"] & 0xFFFF, e["y"] & 0xFFFF
        # a5/a6: Shot_CheckHit leaves them at v_unpack_buf and
        # v_block_terrain, so an air bullet after a ground one compares
        # with ($5000, $4000) (session 35, found by make demo)
        ax, ay = ex, ey
        for s in shots[:24]:
            if not s["flags"] & 0x80:
                continue
            if s["flags"] & 0x20:
                self.exploding(s, 0xA580)
                continue
            own, omech = self.obj(S, s["owner"])
            if own["flags"] & 0xA0 != 0x80:
                s["flags"] |= 1
            s["life"] = (s["life"] - 1) & 0xFFFF
            if s["life"] == 0:
                s["flags"] |= 0x20
                self.explode(s, own, 0xA580)
                self.count("bullets out of life")
                continue
            self.move(s, "x", "sub_x", s["aim_x"])
            self.move(s, "y", "sub_y", s["aim_y"])
            if s["flags"] & 4:
                if e["form"] == 0:
                    continue
                if ((s["x"] - ax) & 0xFFFF) >= 0x18 or ((s["y"] - ay) & 0xFFFF) >= 0x18:
                    if (ax, ay) != (ex, ey) and \
                            ((s["x"] - ex) & 0xFFFF) < 0x18 and \
                            ((s["y"] - ey) & 0xFFFF) < 0x18:
                        self.count("air misses after a ground test")
                    continue
                vy, vx = s16(s["vel_y"]) >> 2, s16(s["vel_x"]) >> 2
                e["vel_y"] = (e["vel_y"] + vy) & 0xFFFF
                e["vel_x"] = (e["vel_x"] + vx) & 0xFFFF
                ident = 0x81 if e["flags"] & 0x10 else 0x01
                self.count("air hits")
                self.hit(S, g, s, own, ident, OWNER_MECH + (team ^ 1))
                hit = 1
            else:
                self.check_hit(S, g, s)
                ax, ay = 0x5000, 0x4000
                hit = g["hit_flags"]
                if hit & 4:
                    self.terrain_event(S, g, s)
                if hit & 1:
                    ident = g["hit_id"] & 0xFF
                    o = self.from_grid_id(ident)
                    if o is not None:
                        self.hit(S, g, s, own, ident, o)
            if not hit & 3:
                if not s["flags"] & 1:
                    own["shot_flags"] = own.get("shot_flags", 0) | 0x20
                continue
            if hit & 2:
                self.sfx(g, 9)
                self.count("tile stops")
            self.explode(s, own, 0xA580)
        dirs = 0
        for s in shots[24:]:
            if s["flags"] & 0x20:
                self.exploding(s, 0x2580)
                continue
            if not s["flags"] & 0x80:
                continue
            s["life"] = (s["life"] - 1) & 0xFFFF
            if s["life"] != 0:
                if e["flags"] & 0x20:
                    want = s["dir"]
                elif s["life"] & 3:
                    want = s["dir"]
                    dirs |= 1 << ((want + 8) & 0xF)
                else:
                    want = get_direction(s["x"], s["y"], ex + 0xC, ey + 0xC,
                                         self.rom)
                    dirs |= 1 << ((want + 8) & 0xF)
                    sx, sy = self.sine(want)
                    s["aim_x"] = (s16(s["speed"]) * sx) & 0xFFFF
                    s["aim_y"] = (s16(s["speed"]) * sy) & 0xFFFF
                    self.count("missile aims")
                s["vel_x"] = self.steer(s["vel_x"], s["aim_x"])
                s["vel_y"] = self.steer(s["vel_y"], s["aim_y"])
                turn = (want - s["dir"]) & 0xF
                if turn:
                    s["dir"] = (s["dir"] + (1 if turn < 8 else -1)) & 0xF
                self.move(s, "x", "sub_x", s["vel_x"])
                self.move(s, "y", "sub_y", s["vel_y"])
                s["attr"] = (0xA550 + s["dir"] + (0x10 if s["flags"] & 0x10 else 0)) & 0xFFFF
                if ex >= s["x"] or ((s["x"] - ex) & 0xFFFF) >= 0x18:
                    continue
                if ey >= s["y"] or ((s["y"] - ey) & 0xFFFF) >= 0x18:
                    continue
                if e["flags"] & 0x20:
                    continue
                e["vel_y"] = (e["vel_y"] + s["vel_y"]) & 0xFFFF
                e["vel_x"] = (e["vel_x"] + s["vel_x"]) & 0xFFFF
                if e["hp"] >= s["power"]:
                    e["hp"] -= s["power"]
                    self.sfx(g, 8)
                else:
                    e["flags"] |= 0x20
                    e["hp"] = 0x180
                    self.sfx(g, 4)
                self.count("missile hits")
            s["flags"] |= 0x20
            s["life"] = 4
            s["x"] = (s["x"] - 4) & 0xFFFF
            s["y"] = (s["y"] - 4) & 0xFFFF
            s["attr"] = 0x2580
        e["missile_dirs"] = dirs

    def combat(self, S, g):
        """Combat_Update: both teams' shots, then one team's units
        (tools/hzunitmove_check.Units: Units_UpdateTeam) by the toggle's
        b0, which then + 1."""
        self.update_team(S, g, 0)
        self.update_team(S, g, 1)
        self.units.update_team(S, g, g["toggle"] & 1)
        g["toggle"] = (g["toggle"] + 1) & 0xFFFF


def unit_crc(units, overlay=False):
    """CRC-32 of the active, uncarried unit records as the shots touch them
    (as hz_mech and hz_shots log it): per record its index, flags without
    b6, HP, x, y, shot flags, hit victim, sprite size and attr (and with
    `overlay`, as hz_mech logs it, the turret's overlay attr)."""
    b = bytearray()
    for i, u in enumerate(units):
        if u is None or u["flags"] & 0x88 != 0x80:
            continue
        b += bytes([i, u["flags"] & ~0x40 & 0xFF])
        for v in (u.get("hp", 0), u.get("x", 0), u.get("y", 0)):
            b += (v & 0xFFFF).to_bytes(2, "big")
        b += bytes([u.get("shot_flags", 0) & 0xFF, u.get("hit_victim", 0) & 0xFF])
        for v in (u.get("sprite_size", 0), u.get("attr", 0)):
            b += (v & 0xFFFF).to_bytes(2, "big")
        if overlay:
            b += (u.get("overlay", 0) & 0xFFFF).to_bytes(2, "big")
    return zlib.crc32(bytes(b))


def parse_shots(parts):
    """`index:f1,f2,...` items (hex) -> {index: dict}."""
    out = {}
    for it in parts:
        k, v = it.split(":")
        out[int(k, 16)] = dict(zip(SHOT_FIELDS, (int(x, 16) for x in v.split(","))))
    return out


def blank_shots():
    return [dict.fromkeys(SHOT_FIELDS, 0) for _ in range(64)]


def compare_shots(mine, logged, where, errors):
    """The model's shots against a line's, flags b6 (Sprites_Build's) taken
    from the line."""
    for i in range(64):
        s = mine[i]
        t = logged.get(i)
        if t is None:
            if s["flags"] & ~0x40:
                errors.append("%s: shot %d free, model flags %X" % (where, i, s["flags"]))
            s["flags"] = 0
            continue
        if not (s["flags"] ^ t["flags"]) & ~0x40:
            s["flags"] = t["flags"]
        for f in SHOT_FIELDS:
            if s[f] != t[f]:
                errors.append("%s: shot %d %s %X, model %X" % (where, i, f, t[f], s[f]))
                s[f] = t[f]


# -- tools/hz_shots' log ----------------------------------------------------
# Line kinds: HZS1 map; then per case: M p <mech fields>, U k <unit
# fields>, G cell id ..., Q <hq hp1 hp2 alert1 alert2>, X <globals>,
# B <shots>, then an action (F p|k flags dir x y speed life missile ->
# result, or C for Combat_Update), then the state after: m/u/q/x/b lines.

M_FIELDS = ("flags carried hp size x y ammo missiles power altitude form "
            "vel_x vel_y shot_flags hit_victim missile_dirs").split()
U_FIELDS = ("flags type hp size x y ammo missiles power raw_0d overlay "
            "shot_flags hit_victim sprite_size attr").split()
X_FIELDS = "difficulty boost sfx_queued sfx_priority toggle hit_flags hit_id event event2".split()


def hexes(parts):
    return [int(v, 16) for v in parts]


def check_file(rom, checker, path):
    lines = [ln.split() for ln in open(path) if ln.strip()]
    if not lines or lines[0][0] != "HZS1":
        return ["not an hz_shots log"], {}
    m = checker.map(int(lines[0][1], 16))
    layout = m.layout

    def tile(tx, ty):
        lw = hv.w16(layout, (((ty & 0xFC) >> 2) * 64 + ((tx & 0xFC) >> 2)) * 2)
        word = hv.w16(m.blocks, (lw & 0x1FF) * 32 + ((ty & 3) * 4 + (tx & 3)) * 2)
        return word, lw

    def terrain(lw):
        return hv.w16(m.blocks, 0x4000 + (lw & 0x1FF) * 2)

    model = Combat(rom, tile, terrain)
    errors = []
    cases = 0
    i = 1
    dirs = 0
    while i < len(lines) and lines[i][0] == "D":
        x0, y0, x1, y1, r = hexes(lines[i][1:])
        want = get_direction(x0, y0, x1, y1, rom)
        if want != r:
            errors.append("line %d: GetDirection (%X, %X) to (%X, %X) %X, "
                          "model %X" % (i + 1, x0, y0, x1, y1, r, want))
        dirs += 1
        i += 1
    model.seen["directions"] = dirs
    while i < len(lines):
        # the state before
        S = {"mechs": [None, None], "units": [None] * 100,
             "grid": bytearray(4096), "shots": blank_shots(),
             "hq_hp": [0, 0], "hq_alert": [0, 0]}
        g = {}
        while lines[i][0] in "MUGQXB":
            k, p = lines[i][0], lines[i][1:]
            if k == "M":
                v = hexes(p)
                S["mechs"][v[0]] = dict(zip(M_FIELDS, v[1:]))
            elif k == "U":
                v = hexes(p)
                u = dict(zip(U_FIELDS, v[1:]))
                u["x"], u["y"] = s16(u["x"]), s16(u["y"])
                S["units"][v[0]] = u
            elif k == "G":
                v = hexes(p)
                for j in range(0, len(v), 2):
                    S["grid"][v[j]] = v[j + 1]
            elif k == "Q":
                v = hexes(p)
                S["hq_hp"], S["hq_alert"] = v[0:2], v[2:4]
            elif k == "X":
                g = dict(zip(X_FIELDS, hexes(p)))
            elif k == "B":
                for idx, s in parse_shots(p).items():
                    S["shots"][idx] = s
            i += 1
        act = lines[i]
        i += 1
        cases += 1
        where = "case %d (line %d)" % (cases, i)
        if act[0] == "F":
            shooter, flags, d, x, y, speed, life, missile, result = hexes(act[1:])
            r = model.fire(S, g, shooter, flags, d, x, y, speed, life, missile)
            if (r & 0xFF) != result:
                errors.append("%s: fired %X, model %X" % (where, result, r & 0xFF))
        else:
            model.combat(S, g)
        # the state after
        logged_units = [None] * 100
        logged_shots = {}
        while i < len(lines) and lines[i][0] in "muqxb":
            k, p = lines[i][0], lines[i][1:]
            if k == "m":
                v = hexes(p)
                for f, val in zip(M_FIELDS, v[1:]):
                    if S["mechs"][v[0]].get(f, 0) & 0xFFFF != val:
                        errors.append("%s: mech %d %s %X, model %X"
                                      % (where, v[0], f, val, S["mechs"][v[0]].get(f, 0)))
            elif k == "u":
                v = hexes(p)
                u = dict(zip(U_FIELDS, v[1:]))
                u["x"], u["y"] = s16(u["x"]), s16(u["y"])
                logged_units[v[0]] = u
            elif k == "q":
                v = hexes(p)
                if v != S["hq_hp"] + S["hq_alert"]:
                    errors.append("%s: HQ %s, model %s"
                                  % (where, v, S["hq_hp"] + S["hq_alert"]))
            elif k == "x":
                for f, val in zip(X_FIELDS, hexes(p)):
                    if f in ("hit_flags", "hit_id", "event", "event2") and act[0] == "F":
                        continue
                    if g.get(f, 0) != val:
                        errors.append("%s: %s %X, model %X" % (where, f, val, g.get(f, 0)))
            elif k == "b":
                logged_shots = parse_shots(p)
            i += 1
        for k2 in range(100):
            a, b = S["units"][k2], logged_units[k2]
            if b is None:
                continue
            for f in U_FIELDS:
                if (a or {}).get(f, 0) != b[f]:
                    errors.append("%s: unit %d %s %X, model %X"
                                  % (where, k2, f, b[f], (a or {}).get(f, 0)))
        compare_shots(S["shots"], logged_shots, where, errors)
        if len(errors) > 20:
            break
    model.seen["cases"] = cases
    for k, v in model.units.seen.items():
        model.seen["units: " + k] = v
    return errors, model.seen


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    rom = open(argv[1], "rb").read()
    checker = hv.Checker(rom)
    bad = 0
    for path in argv[2:]:
        errors, seen = check_file(rom, checker, path)
        name = os.path.basename(path)
        if errors:
            bad += 1
            print("%-22s %d errors" % (name, len(errors)))
            for e in errors[:12]:
                print("   ", e)
        else:
            print("%-22s ok: %s" % (name, ", ".join(
                "%d %s" % (v, k) for k, v in sorted(seen.items()))))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
