#!/usr/bin/env python3
"""A model of the units' move, written from the 68k code: Units_UpdateTeam
($1182) with Obj_Accelerate ($3CBE), Unit_CheckObjCollision ($1924),
Obj_CheckTerrain ($1B50), Obj_TerrainEvent ($8764) for a unit (d0 0) with
TerrainEv_Outpost's service and capture, TerrainEv_HQ, _Current, _Damage,
_Slippery, _Slow, Outpost_Service and Obj_RepairRefuel, Outpost_ShowOwner
($8B3A), MapFx_Alloc ($8E5E), Unit_SpawnSurvivor ($9696) with Unit_Spawn
($96BC), Unit_CheckSpawnBlocked ($1C72), Grid_MarkObj and Unit_SetSprite's
first call (hzview_check.new_unit); and MapFx_Update ($698A) with
MapFx_DrawBlock ($6B5A).

`Units` works on a state dict S: "mechs" (two dicts with flags, carried,
x, y, size), "units" (100 entries, None or a dict; FIELDS below), "grid"
(bytearray of 4096), "posts" (20 dicts flags h1 h2 x y), "counts" (units
[2], types [2][16], built [2], outposts [4]), "mapfx" (32 dicts, MAPFX
names); and a globals dict g (group, sfx_queued, sfx_priority, hit_flags,
hit_id, event, event3). Missing parts default to empty ones, so
tools/hzshots_check.py's Combat (and through it tools/hzmech_check.py) can
run it on states that don't carry them.

  hzunitmove_check.py ROM FILE...
"""

import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hzview_check as hv   # noqa: E402

FIELDS = ("flags type order dir turn_to tick hp size bumped fuel_use x y "
          "sub_x sub_y missiles ammo fuel terrain attr sprite_size blocked "
          "speed prev_speed max_speed turret turret_dir aim_dir push overlay "
          "grid_cell vel_x vel_y tvx tvy power").split()
SIGNED = ("x", "y")
MAPFX = "flags b1 countdown reload x y block ofs".split()

UNIT_STATS = 0x5DC00
SINE = 0x25AA
NEIGHBOURS = 0x25EA
EXPLOSION_FRAMES = 0x258A
SFX_PRIORITY = 0x9931


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def asr(v, n):
    return s16(v) >> n


def blank_state():
    return {"posts": [dict(flags=0, h1=0, h2=0, x=0, y=0) for _ in range(20)],
            "counts": {"units": [0, 0], "types": [[0] * 16, [0] * 16],
                       "built": [0, 0], "outposts": [0, 0, 0, 0]},
            "mapfx": [dict.fromkeys(MAPFX, 0) for _ in range(32)]}


def scan_posts(layout, blocks):
    """Map_ScanOutposts ($6BB0) as the units model wants it: the 20 post
    records (the first 16 outposts in layout order, the HQs at $10/$11,
    two cleared) and v_outpost_count (P1, P2, neutral, total). An owned
    outpost starts at its owner's hold 4; an HQ's h1/h2 are its HP bytes."""
    posts = [dict(flags=0, h1=0, h2=0, x=0, y=0) for _ in range(20)]
    counts = [0, 0, 0, 0]
    nxt = 0
    for i in range(64 * 64):
        blk = hv.w16(layout, i * 2) & 0x1FF
        ter = hv.w16(blocks, 0x4000 + blk * 2)
        typ, owner = ter & 0xFF, ter >> 8
        x, y = (i % 64) * 32 + 16, (i // 64) * 32 + 16
        if typ == 3:
            fl = 0xC2 | (owner & 1)
            posts[16 + (fl & 1)].update(flags=fl, h1=4, h2=0, x=x, y=y)
        elif typ == 2 and nxt < 16:
            fl = 0x80 | (owner & 3)
            p = posts[nxt]
            nxt += 1
            p.update(flags=fl, x=x, y=y)
            counts[3] += 1
            if not fl & 2:
                counts[2] += 1
            elif not fl & 1:
                counts[0] += 1
                p["h1"] = 4
            else:
                counts[1] += 1
                p["h2"] = 4
    return posts, counts


class Units:
    def __init__(self, rom, tile, block_terrain, block_word=None,
                 layout_rw=None):
        """tile(tx, ty) -> (tile word, layout word); block_terrain(layout
        word) -> the block's terrain word; block_word(block, k) -> the
        block's nametable word k (MapFx_Update); layout_rw(ofs, value) ->
        the layout word at byte offset ofs, written first if value isn't
        None."""
        self.rom = rom
        self.tile = tile
        self.block_terrain = block_terrain
        self.block_word = block_word
        self.layout_rw = layout_rw
        self.seen = {}

    def count(self, key, n=1):
        self.seen[key] = self.seen.get(key, 0) + n

    def w(self, a):
        return hv.w16(self.rom, a)

    def sfx(self, g, n):
        pri = self.rom[SFX_PRIORITY + n]
        if pri > g["sfx_priority"]:
            return
        g["sfx_priority"] = pri
        g["sfx_queued"] = n

    @staticmethod
    def defaults(S):
        b = blank_state()
        for k in b:
            S.setdefault(k, b[k])

    # -- the grid's occupants -----------------------------------------------
    def occupant(self, S, ident):
        """(x, y, size, the byte at +$01) of a grid id, as the 68k finds
        the record (a unit slot past 49 runs into the next team's)."""
        if not ident & 0x40:
            m = S["mechs"][1 if ident & 0x80 else 0]
            return s16(m["x"]), s16(m["y"]), m["size"], m["carried"]
        i = (50 if ident & 0x80 else 0) + ((ident - 1) & 0x3F)
        u = S["units"][min(i, 99)] or {}
        return s16(u.get("x", 0)), s16(u.get("y", 0)), u.get("size", 0), u.get("type", 0)

    @staticmethod
    def centre_cell(u):
        h = u["size"] >> 1
        row = (((u["y"] + h) & 0x7E0) * 2) & 0xFFFF
        col = (((u["x"] + h) & 0xFFFF) >> 5) & 0x3F
        return row, (row + col) & 0xFFF

    @staticmethod
    def mark_cell(u):
        h = u["size"] >> 1
        return ((((u["y"] + h) & 0xFFE0) * 2) + (((u["x"] + h) & 0xFFFF) >> 5)) & 0xFFF

    @staticmethod
    def overlap(u, o, mine, theirs):
        hit = 0
        for k, f in ((1, "x"), (2, "y")):
            d = s16(o[0 if f == "x" else 1] - u[f])
            if d >= 0:
                if (d & 0xFFFF) < mine:
                    hit |= k
            elif ((-d) & 0xFFFF) < theirs:
                hit |= k
        return hit == 3

    def collision(self, S, g, u):
        """Unit_CheckObjCollision."""
        g["hit_flags"] = 0
        g["hit_id"] = 0
        grid = S["grid"]
        row, cell = self.centre_cell(u)
        ident = None
        if grid[cell]:
            ident = row & 0xFF            # d0 still the row offset
            self.count("centre cell taken")
        else:
            self_supply = u["type"] == 4
            for k in range(8):
                c = (cell + s16(self.w(NEIGHBOURS + k * 2))) & 0xFFFF
                if c >= 0x1000 or not grid[c]:
                    continue
                o = self.occupant(S, grid[c])
                mine, theirs = u["size"], o[2]
                if self_supply:
                    mine, theirs = mine + 12, theirs + 12
                elif o[3] == 4:
                    mine, theirs = mine + 12, theirs + 12
                    self.count("next to a SUPPLY")
                if self.overlap(u, o, mine, theirs):
                    ident = grid[c]
                    break
        if ident is not None:
            g["hit_flags"] |= 1
            g["hit_id"] = ident
            if ((ident >> 3) ^ u["flags"]) & 0x10:
                g["hit_flags"] |= 8

    def check_terrain(self, g, u):
        """Obj_CheckTerrain."""
        g["event"] = 0
        look = 3
        last = (0xFF - (u["size"] >> 3)) & 0xFFFF
        tx = ((((u["x"] + 4) & 0xFFFF) >> 3) - 1) & 0xFFFF
        if tx >= last:
            g["hit_flags"] |= 2
            look &= ~1
            self.count("map edge")
        tx = (tx + 1) & 0xFFFF
        ty = ((((u["y"] + 4) & 0xFFFF) >> 3) - 1) & 0xFFFF
        if ty >= last:
            g["hit_flags"] |= 2
            look &= ~1
            self.count("map edge")
        ty = (ty + 1) & 0xFFFF
        n = 3 if u["type"] < 8 else 2
        for r in range(n):
            for c in range(n):
                word, lw = self.tile((tx + c) & 0xFFFF, (ty + r) & 0xFFFF)
                if look & 1 and not u["flags"] & 1:
                    blocked = word & 0x8000
                else:
                    blocked = not word & 0x200
                if blocked:
                    g["hit_flags"] |= 2
                    look &= ~1
                if look & 2 and word & 0x2000:
                    g["event3"] = lw
                    g["event"] = self.block_terrain(lw)
                    g["hit_flags"] |= 4
                    look &= ~2
                if look == 0:
                    return

    # -- terrain events (d0 0) -------------------------------------------------
    def stats(self, typ):
        return UNIT_STATS + (typ & 0xFF) * 32

    def repair(self, u):
        """Obj_RepairRefuel by the unit's own type."""
        st = self.stats(u["type"])
        for f, add, cap, word in (("hp", 8, self.w(st + 0xA), True),
                                  ("fuel", 0x28, self.w(st + 0xE), True),
                                  ("ammo", 2, self.rom[st + 0xD], False),
                                  ("missiles", 2, self.rom[st + 0x1A], False)):
            top = 0x10000 if word else 0x100
            v = u[f] + add
            if v >= top:
                v = cap
            if v >= cap:
                v = cap
            u[f] = v

    def post_own(self, S, u, index):
        fl = S["posts"][index]["flags"]
        return fl & 2 and not ((u["flags"] >> 4) ^ fl) & 1

    def service(self, S, g, u, index):
        if g["group"] != 0 or not self.post_own(S, u, index):
            return
        self.repair(u)
        self.count("services")

    def mapfx_alloc(self, S):
        for fx in S["mapfx"]:
            if not fx["flags"] & 0x80:
                return fx
        return None

    def show_owner(self, S, g, post, block):
        x = (post["x"] - 0x20) & 0xFFFF
        y = (post["y"] - 0x20) & 0xFFFF
        ofs = (((y & 0x7E0) << 2) + (x >> 5) * 2) & 0xFFFF
        for dx, dy, do, db in ((0, 0, 0, 0), (0x40, 0, 4, 1),
                               (0, 0x40, 0x100, 8), (0x40, 0x40, 0x104, 9)):
            fx = self.mapfx_alloc(S)
            if fx is None:
                self.count("MapFx full")
                break
            fx.update(flags=0x80, b1=0, countdown=0, reload=0,
                      x=(x + dx) & 0xFFFF, y=(y + dy) & 0xFFFF,
                      block=(block + db) & 0xFFFF, ofs=(ofs + do) & 0xFFFF)
        self.sfx(g, 0x16)

    def capture(self, S, g, u, index):
        if u["type"] != 8 or u["flags"] & 0x28:
            return
        post = S["posts"][index]
        C = S["counts"]
        team = (u["flags"] >> 4) & 1
        own, other = ("h2", "h1") if team else ("h1", "h2")
        owner = 0x83 if team else 0x82
        if post[own] >= 4:
            self.count("hold already 4")
            return
        post[own] += 1
        u["flags"] = 0
        C["units"][team] = (C["units"][team] - 1) & 0xFF
        C["types"][team][8] = (C["types"][team][8] - 1) & 0xFF
        post[other] = post[other] - 1 if post[other] else 0
        self.count("holds raised")
        if post[own] < 4:
            self.sfx(g, 0x15)
            return
        if post["flags"] == owner:
            self.count("own outpost topped up")
            return
        old = post["flags"]
        post["flags"] = owner
        o = C["outposts"]
        d4 = 1
        if not old & 2:
            o[2] = (o[2] - 1) & 0xFF
            d4 = 0
        if not team:
            o[0] = (o[0] + 1) & 0xFF
            o[1] = (o[1] - d4) & 0xFF
            self.show_owner(S, g, post, 0x1D0)
        else:
            o[1] = (o[1] + 1) & 0xFF
            o[0] = (o[0] - d4) & 0xFF
            self.show_owner(S, g, post, 0x1C0)
        self.count("outposts taken")

    def damage(self, g, u):
        if g["group"] > 1 or u["flags"] & 0x20:
            return
        if u["hp"] >= 0x30:
            u["hp"] -= 0x30
            self.sfx(g, 0x1C)
            self.count("ground hurts")
            return
        u["flags"] |= 0x20
        u["hp"] = 0x10
        if u["type"] == 9:
            u["x"] = s16(u["x"] - 4)
            u["y"] = s16(u["y"] - 4)
            u["sprite_size"] = 0xA00
            u["attr"] = self.w(EXPLOSION_FRAMES)
        self.sfx(g, 6 if u["type"] == 8 else 5)
        self.count("ground kills")

    def terrain_event(self, S, g, u):
        u["push"] = 0
        ev = g["event"]
        param = ev >> 8
        t = ev & 0xF
        self.count("event %X" % t)
        if t == 2:
            index = (g["event3"] >> 10) & 0xF
            self.service(S, g, u, index)
            self.capture(S, g, u, index)
        elif t == 3:
            self.service(S, g, u, 16 + ((ev >> 8) & 1))
        elif t == 6:
            u["push"] = param & 0x7F
        elif t == 8:
            self.damage(g, u)
        elif t == 9:
            u["push"] = 0x80 | (31 - (param & 0x1F))
        elif t == 0xA:
            u["push"] = 0xC0 | (param & 3)

    # -- Unit_Spawn -----------------------------------------------------------
    def spawn_test(self, S, g, u):
        """Unit_CheckSpawnBlocked -> v_hit_flags, v_hit_id."""
        hit, ident = 0, 0
        grid = S["grid"]
        _, cell = self.centre_cell(u)
        if grid[cell]:
            hit, ident = 1, grid[cell]
        else:
            for k in range(8):
                c = (cell + s16(self.w(NEIGHBOURS + k * 2))) & 0xFFFF
                if c >= 0x1000 or not grid[c]:
                    continue
                o = self.occupant(S, grid[c])
                if self.overlap(u, o, u["size"], o[2]):
                    hit, ident = 1, grid[c]
                    break
        tx0 = ((u["x"] + 4) & 0xFFFF) >> 3
        ty0 = ((u["y"] + 4) & 0xFFFF) >> 3
        n = 3 if u["type"] < 8 else 2
        for r in range(n):
            for c in range(n):
                word, _ = self.tile((tx0 + c) & 0xFFFF, (ty0 + r) & 0xFFFF)
                if (not word & 0x200) if u["flags"] & 1 else word & 0x8000:
                    return hit | 2, ident
        return hit, ident

    def spawn(self, S, g, flags, typ, order, d, x, y):
        team = (flags >> 4) & 1
        units = S["units"]
        slot = next((k for k in range(50)
                     if not (units[team * 50 + k] or {}).get("flags", 0) & 0x80),
                    None)
        if slot is None:
            return -1
        u = hv.new_unit(self.rom, flags, typ, order, d, x, y)
        full = dict.fromkeys(FIELDS, 0)
        full.update({f: u[f] for f in FIELDS if f in u})
        full["turn_to"] = d & 15
        u = full
        g["hit_flags"], g["hit_id"] = 0, 0
        g["event"] = 0
        # The record is written before Unit_CheckSpawnBlocked: a grid id
        # naming its own slot finds the new unit itself.
        units[team * 50 + slot] = u
        hit, ident = self.spawn_test(S, g, u)
        if hit:
            # Unit_Spawn's record before Unit_SetSprite: heading dir + 1,
            # no sprite or velocity yet; then its flags cleared
            g["hit_flags"], g["hit_id"] = hit, ident
            u.update(flags=0, dir=(d + 1) & 15, attr=0, overlay=0, tvx=0,
                     tvy=0, prev_speed=0)
            return -1
        C = S["counts"]
        C["units"][team] = (C["units"][team] + 1) & 0xFF
        C["types"][team][u["type"]] = (C["types"][team][u["type"]] + 1) & 0xFF
        C["built"][team] = min(C["built"][team] + 1, 0xFFFF)
        cell = self.mark_cell(u)
        u["grid_cell"] = cell
        S["grid"][cell] = (0xC0 if team else 0x40) | (slot + 1)
        return 0

    # -- Units_UpdateTeam -----------------------------------------------------
    def set_bumped(self, g, u):
        i = g["hit_id"]
        if not i & 0x40:
            b = 0x65 if i & 0x80 else 0x64
        else:
            b = ((i & 0x3F) - 1) & 0xFFFF
            if i & 0x80:
                b += 0x32
        u["bumped"] = b & 0xFF

    @staticmethod
    def accel(v, t, step):
        v, t = s16(v), s16(t)
        if t == v:
            return v
        if t > v:
            v = s16(v + step)
            return v if t > v else t
        v = s16(v - step)
        return v if t < v else t

    def move(self, S, g, u):
        old = (u["x"], u["y"])
        push = u["push"]
        if not push & 0x80:
            vx, vy = s16(u["tvx"]), s16(u["tvy"])
            if push:
                shift = 2 if u["type"] == 8 else 6
                amount = (push & 0xF) << shift
                code = (push >> 4) & 7
                if code not in (0, 4):
                    vx = s16(vx + amount if code < 4 else vx - amount)
                code = (code - 2) & 0xFFFF
                if code not in (0, 4):
                    vy = s16(vy + amount if code < 4 else vy - amount)
                self.count("currents")
            u["vel_x"], u["vel_y"] = vx & 0xFFFF, vy & 0xFFFF
        elif push & 0x40:
            vx, vy = s16(u["tvx"]), s16(u["tvy"])
            k = push & 3
            if k == 1:
                vx, vy = vx - asr(vx, 2), vy - asr(vy, 2)
            elif k == 2:
                vx, vy = asr(vx, 1), asr(vy, 1)
            elif k == 3:
                vx, vy = asr(vx, 2), asr(vy, 2)
            u["vel_x"], u["vel_y"] = vx & 0xFFFF, vy & 0xFFFF
            self.count("slowdowns")
        else:
            step = (push & 0x1F) * 2
            u["vel_x"] = self.accel(u["vel_x"], u["tvx"], step) & 0xFFFF
            u["vel_y"] = self.accel(u["vel_y"], u["tvy"], step) & 0xFFFF
            self.count("accelerations")
        for p, sub, vel in (("x", "sub_x", "vel_x"), ("y", "sub_y", "vel_y")):
            v = (u[sub] + u[vel]) & 0xFFFF
            u[p] = s16(u[p] + s8(v >> 8))
            u[sub] = v & 0xFF
        self.collision(S, g, u)
        same = all(((a + 4) & 0xFFFF) >> 3 == ((u[f] + 4) & 0xFFFF) >> 3
                   for a, f in zip(old, ("x", "y")))
        if same:
            if not u["blocked"] & 6:
                u["blocked"] = (u["blocked"] & 0xF6) | (g["hit_flags"] & 9)
                u["terrain"] = g["event"]
                if u["blocked"] & 3:
                    u["speed"] = 0
                    u["prev_speed"] = 0
                    u["x"], u["y"] = old
                    self.count("bumps (same tile)")
                if u["blocked"] & 1:
                    self.set_bumped(g, u)
                return
        elif u["speed"]:
            u["fuel"] = max(u["fuel"] - u["fuel_use"], 0)
            self.count("new tiles")
        u["push"] = 0
        self.check_terrain(g, u)
        hit = g["hit_flags"]
        u["blocked"] = hit & 0xFF
        u["terrain"] = g["event"]
        if hit & 4:
            self.terrain_event(S, g, u)
        if hit & 3:
            u["speed"] = 0
            u["prev_speed"] = 0
            u["x"], u["y"] = old
            self.count("stops")
        if hit & 1:
            self.set_bumped(g, u)

    def dying(self, S, g, u, team):
        u["hp"] = (u["hp"] - 1) & 0xFFFF
        if u["hp"] == 0:
            flags = u["flags"]
            u["flags"] = 0
            C = S["counts"]
            C["units"][team] = (C["units"][team] - 1) & 0xFF
            C["types"][team][u["type"] & 0xF] = (C["types"][team][u["type"] & 0xF] - 1) & 0xFF
            self.count("freed")
            if u["type"] < 3 and u["tick"] & 3 == 0:
                r = self.spawn(S, g, flags & 0x90, 8, u["order"], u["dir"],
                               s16(u["x"] + 4), s16(u["y"] + 4))
                self.count("survivors" if r == 0 else "survivors blocked")
            return False
        frame = (15 - u["hp"]) & 0xF
        if u["type"] == 8:
            pal = 0x6000 if u["flags"] & 0x10 else 0x4000
            u["attr"] = (0x480 + frame * 4) | pal
        else:
            a = self.w(EXPLOSION_FRAMES + frame * 2)
            u["attr"] = a
            u["overlay"] = a
        return True

    def update_team(self, S, g, team):
        self.defaults(S)
        g.setdefault("group", 0)
        g.setdefault("event3", 0)
        ident = 0xC1 if team else 0x41
        for slot in range(50):
            u = S["units"][team * 50 + slot]
            ident_now = ident + slot
            if u is None:
                continue
            for f in FIELDS:
                u.setdefault(f, 0)
            f = u["flags"] & 0xA8
            if f == 0 or f & 8:
                continue
            S["grid"][u["grid_cell"] & 0xFFF] = 0
            if f & 0x20:
                if not self.dying(S, g, u, team):
                    continue
            else:
                if u["fuel"] == 0:
                    u["tvx"] = 0
                    u["tvy"] = 0
                if u["speed"] or u["blocked"] & 4:
                    self.move(S, g, u)
                    self.count("moves")
            if u["flags"]:
                cell = self.mark_cell(u)
                S["grid"][cell] = ident_now
                u["grid_cell"] = cell

    # -- MapFx_Update ---------------------------------------------------------
    def mapfx_update(self, S, V, vram):
        """V: split cams [(x, y), (x, y)] vcam (a, b) scroll_x [a, b]
        scroll_y [a, b] views [p1, p2] mflags [p1, p2]; vram a bytearray or
        None (then only the layout and records)."""
        if vram is not None:
            width = 0x16 if V["split"] else 0x2C
            skip_a = s8(V["views"][0]) < 0 or V["mflags"][0] & 0x40
            skip_b = s8(V["views"][1]) < 0 or V["mflags"][1] & 0x40
        for fx in S["mapfx"]:
            if not fx["flags"] & 0x80:
                continue
            was = fx["countdown"]
            fx["countdown"] = (was - 1) & 0xFF
            if was:
                continue
            self.count("MapFx drawn")
            tx = ((((fx["x"] + 4) & 0xFFFF) >> 3) & 0xFC)
            ty = ((((fx["y"] + 4) & 0xFFFF) >> 3) & 0xFC)
            block = fx["block"] & 0x1FF
            ofs = fx["ofs"] & 0x1FFE
            lw = self.layout_rw(ofs, None)
            self.layout_rw(ofs, (lw & 0xFE00) | block)
            if vram is not None:
                for view, skip in ((0, skip_a), (1, skip_b)):
                    if skip:
                        continue
                    cam = V["cams"][V["vcam"][view]]
                    s = 1 if view and V["split"] else 0
                    plane = 0xE000 if view and V["split"] else 0xC000
                    col = (tx - (((cam[0] + 4) & 0xFFFF) >> 3) + 4) & 0xFFFF
                    if col >= width:
                        continue
                    col = (col - 4) & 0xFFFF
                    row = (ty - (((cam[1] + 4) & 0xFFFF) >> 3) + 4) & 0xFFFF
                    if row >= 0x1C:
                        continue
                    col = ((col + (((V["sx"][s] + 4) & 0xFFFF) >> 3)) & 0x3F) * 2
                    row = ((row + (((V["sy"][s] + 4) & 0xFFFF) >> 3)) & 0x1F) << 7
                    cell = (col + row) & 0xFFE
                    self.count("MapFx views")
                    for r in range(4):
                        rowbase = cell & 0xF80
                        c = cell
                        for k in range(4):
                            wd = self.block_word(block, r * 4 + k) & 0x19FF
                            if (wd & 0x1FF) < 0x40:
                                wd |= 0x8000
                            vram[plane + c] = wd >> 8
                            vram[plane + c + 1] = wd & 0xFF
                            c = ((c + 2) & 0x7E) | rowbase
                        cell = (cell + 0x80) & 0xFFE
            flags = fx["flags"]
            fx["flags"] = 0
            if flags & 0x40:
                t = self.block_terrain(fx["block"] & 0x1FF)
                fx["block"] = (fx["block"] & 0xFF00) | (t >> 8)
                fx["flags"] = 0x80 | (((t & 0xFF) >> 1) & 0x40)
                fx["countdown"] = fx["reload"]
                self.count("MapFx chained")


# -- tools/hz_unitmove's log ------------------------------------------------

def hexes(parts):
    return [int(v, 16) for v in parts]


def read_state(lines, i, upper):
    """The state lines from i (U/M/G/P/N/F/X or their lower case)."""
    k = "UMGPNFX" if upper else "umgpnfx"
    S = {"units": [None] * 100, "mechs": [None, None]}
    g = {}
    while i < len(lines) and lines[i][0] in k:
        c, p = lines[i][0].upper(), hexes(lines[i][1:])
        if c == "U":
            u = dict(zip(FIELDS, p[1:]))
            u["x"], u["y"] = s16(u["x"]), s16(u["y"])
            S["units"][p[0]] = u
        elif c == "M":
            S["mechs"][p[0]] = dict(zip("flags carried x y size form".split(), p[1:]))
        elif c == "G":
            grid = bytearray(4096)
            for j in range(0, len(p), 2):
                grid[p[j]] = p[j + 1]
            S["grid"] = grid
        elif c == "P":
            S["posts"] = [dict(zip("flags h1 h2 x y".split(), p[j:j + 5]))
                          for j in range(0, 100, 5)]
        elif c == "N":
            S["counts"] = {"units": p[0:2], "types": [p[2:18], p[18:34]],
                           "built": p[34:36], "outposts": p[36:40]}
        elif c == "F":
            S["mapfx"] = [dict(zip(MAPFX, p[j:j + 8])) for j in range(0, 256, 8)]
        elif c == "X":
            g = dict(zip("group sfx_queued sfx_priority hit_flags hit_id "
                         "event event3".split(), p))
        i += 1
    return S, g, i


def compare(S, T, g, h, where, errors):
    for k in range(100):
        a, b = S["units"][k], T["units"][k]
        if b is None:
            if a is not None and a.get("flags", 0):
                errors.append("%s: unit %d not logged, model flags %X" % (where, k, a["flags"]))
            continue
        for f in FIELDS:
            va = (a or {}).get(f, 0)
            vb = b[f]
            if f in SIGNED:
                va, vb = s16(va), s16(vb)
            if va != vb:
                errors.append("%s: unit %d %s %X, model %X" % (where, k, f, vb & 0xFFFF, va & 0xFFFF))
    if S["grid"] != T["grid"]:
        diff = [c for c in range(4096) if S["grid"][c] != T["grid"][c]][:4]
        errors.append("%s: grid differs at %s" % (where, ", ".join(
            "%X (%X, model %X)" % (c, T["grid"][c], S["grid"][c]) for c in diff)))
    if S["posts"] != T["posts"]:
        errors.append("%s: posts %s, model %s" % (where, T["posts"], S["posts"]))
    if S["counts"] != T["counts"]:
        errors.append("%s: counts %s, model %s" % (where, T["counts"], S["counts"]))
    for j in range(32):
        if S["mapfx"][j] != T["mapfx"][j]:
            errors.append("%s: MapFx %d %s, model %s" % (where, j, T["mapfx"][j], S["mapfx"][j]))
    for f in h:
        if g.get(f) != h[f]:
            errors.append("%s: %s %X, model %X" % (where, f, h[f], g.get(f, -1)))


def check_file(rom, checker, path):
    lines = [ln.split() for ln in open(path) if ln.strip()]
    if not lines or lines[0][0] != "HZN1":
        return ["not an hz_unitmove log"], {}
    m = checker.map(int(lines[0][1], 16))
    base = bytearray(m.layout)
    # Map_ScanOutposts: the first 16 outposts' indices in bits 10-13
    index = 0
    for j in range(4096):
        lw = hv.w16(base, j * 2)
        blk = lw & 0x1FF
        if hv.w16(m.blocks, 0x4000 + blk * 2) & 0xFF == 2 and index < 16:
            lw = blk | (index << 10)
            index += 1
            base[j * 2], base[j * 2 + 1] = lw >> 8, lw & 0xFF
    layout = bytearray(base)

    def tile(tx, ty):
        lw = hv.w16(layout, (((ty & 0xFC) >> 2) * 64 + ((tx & 0xFC) >> 2)) * 2)
        word = hv.w16(m.blocks, (lw & 0x1FF) * 32 + ((ty & 3) * 4 + (tx & 3)) * 2)
        return word, lw

    def terrain(lw):
        return hv.w16(m.blocks, 0x4000 + (lw & 0x1FF) * 2)

    def block_word(block, k):
        return hv.w16(m.blocks, block * 32 + k * 2)

    def layout_rw(ofs, value):
        if value is not None:
            layout[ofs], layout[ofs + 1] = value >> 8, value & 0xFF
        return hv.w16(layout, ofs)

    model = Units(rom, tile, terrain, block_word, layout_rw)
    errors = []
    steps = 0
    i = 1
    while i < len(lines):
        if lines[i][0] == "C":
            layout[:] = base
            i += 1
        S, g, i = read_state(lines, i, True)
        team = int(lines[i][1], 16)
        i += 1
        steps += 1
        where = "step %d (line %d)" % (steps, i)
        model.update_team(S, g, team)
        vram = None
        if lines[i][0] == "V":
            v = hexes(lines[i][1:])
            V = {"split": v[0], "cams": [(v[1], v[2]), (v[3], v[4])],
                 "vcam": (v[5], v[6]), "sx": v[7:9], "sy": v[9:11],
                 "views": v[11:13], "mflags": v[13:15]}
            fill = v[15]
            vram = bytearray(((j * 0x9E3779B1 + fill * 0x9E37) >> 24) & 0xFF
                             for j in range(0x10000))
            model.mapfx_update(S, V, vram)
            i += 1
        T, h, i = read_state(lines, i, False)
        compare(S, T, g, h, where, errors)
        if vram is not None:
            crc = int(lines[i][1], 16)
            if zlib.crc32(bytes(vram)) != crc:
                errors.append("%s: VRAM differs from the model's" % where)
            changed = hexes(lines[i + 1][1:])
            want = {changed[j]: changed[j + 1] for j in range(0, len(changed), 2)}
            mine = {j * 2: hv.w16(layout, j * 2) for j in range(4096)
                    if hv.w16(layout, j * 2) != hv.w16(base, j * 2)}
            if want != mine:
                errors.append("%s: layout %s, model %s" % (where, want, mine))
            i += 2
        if len(errors) > 20:
            break
    model.seen["steps"] = steps
    return errors, model.seen


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    rom = open(argv[1], "rb").read()
    checker = hv.Checker(rom)
    bad = 0
    total = {}
    for path in argv[2:]:
        errors, seen = check_file(rom, checker, path)
        name = os.path.basename(path)
        if errors:
            bad += 1
            print("%-14s %d errors" % (name, len(errors)))
            for e in errors[:12]:
                print("   ", e)
        else:
            print("%-14s ok: %d steps" % (name, seen.get("steps", 0)))
        for k, v in seen.items():
            total[k] = total.get(k, 0) + v
    print("seen: " + ", ".join("%d %s" % (v, k) for k, v in sorted(total.items())))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
