#!/usr/bin/env python3
"""Checks tools/hz_mech's log against a model of Mech_Update ($32F2) as far
as it is ported, written from the 68k code: the dying mech's fly home and
Mech_Respawn; take-off and landing (Mechs_Overlap, Mech_GetTerrain,
Mech_FindUnitToLift, Mech_CheckLandBlocked); steering, speed, velocity
(SineTable, Obj_Accelerate, mech_terrain_flow), the 8.8 move,
Mech_CheckCollision, Mechs_JetCollide, mech_bumped; fuel by distance; the
frame (MechJetFrames, walking, firing, the transform animations); the
screen lead (MechLeadSplit/Wide); Obj_TerrainEvent for a mech (d0 1 robot,
2 jet): Outpost_Service with Obj_RepairRefuel (the carried unit too),
Outpost_PickUpProduct with Unit_CreateCarried, the damaging ground, the
current, slippery and slow ground; firing (Mech_FireGun with
tools/hzshots_check.py's Shot_FireBullet); lifting a unit (Grid_ClearObj)
and putting it down (Grid_MarkObj, Unit_SetSprite, Obj_CheckTerrain). Not
ported, so not modelled: the order menu, AI_MechThink.

Round each Combat_Update a "J" and a "K" line: the model runs its shots
(tools/hzshots_check.Combat) from J's state (its own shots and units,
checked against J's) and checks K's mechs, globals, shots and units (a
CRC). Between K and the next update only the drawing runs: the steady
fields must hold.

The unit records: the model keeps its own (the start units, then what
Unit_CreateCarried makes and Obj_RepairRefuel changes). Each line shows the
record in each mech's carried slot; the model takes a record it has no
copy of yet from an "S" line, and checks every other against its own.
v_unit_ai_group must step as UnitAI_Update steps it between passes. A
unit's obj_flags b6 (on screen in some view) belongs to Sprites_DrawUnits,
which runs between passes: the model takes it from each "S" line.

v_grid: the units Game_Start leaves (hzview_check.spawn_units), then the
mechs' Grid_MarkMech writes replayed in order: 0 at a mech's place just
before its update (the "S" line), its id after its update and camera step
(at its place in the next "S" line).

  hzmech_check.py ROM FILE...   (or directories of *.txt)
"""

import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hzview_check as hv   # noqa: E402
import hzshots_check as hzs  # noqa: E402
import hzunitmove_check as hzu  # noqa: E402

MECH_FIELDS = ("flags carried dir turn_to transform tdelay hp size bumped "
               "fuel_use x y sub_x sub_y fuel terrain frame blocked speed "
               "max_speed pad pad_prev lead flow form pad_last held vel_x "
               "vel_y tvx tvy sx sy cam_dx cam_dy home_cx home_cy home_sx "
               "home_sy fire_delay menu_closing fuel_count walk ammo power "
               "altitude frame_loaded missiles prod_state prod_type "
               "prod_order carried_order refill view shot_flags hit_victim "
               "missile_dirs cu_flags cu_type "
               "cu_order cu_hp cu_size cu_ammo cu_missiles cu_fuel "
               "cu_sprite_size cu_speed cu_max_speed cu_prev_speed cu_turret "
               "cu_power cu_fuel_use").split()
CU_FIELDS = [f[3:] for f in MECH_FIELDS if f.startswith("cu_")]
GLOBALS = ("tick cam1x cam1y cam2x cam2y split map free_ammo hit_flags "
           "hit_id event event3 sfx_queued sfx_priority spawns1 spawns2 "
           "units1 units2 group difficulty game_type built1 built2 boost "
           "toggle sfx_sent posts tc1 tc2 grid_crc layout_crc").split()

# Unchanged between one Mech_Update and the next line but by the camera
# step (x, y, screen, velocity), the pads, or the harness's pokes.
STEADY = ("flags dir turn_to transform tdelay bumped fuel_use sub_x sub_y "
          "terrain frame blocked speed max_speed form pad_last tvx tvy "
          "cam_dx cam_dy fuel_count walk carried_order refill").split()

UNIT_STATS = 0x5DC00
KIDOU = 0x5DDC0
SINE = 0x25AA
NEIGHBOURS = 0x25EA
JOY_TO_DIR = 0x4710
JET_FRAMES = 0x4720
TRANSFORM_FRAMES = 0x4740
TRANSFORM_FRAMES2 = 0x4756
LEAD_SPLIT = 0x3C7E
LEAD_WIDE = 0x3C9E
SFX_PRIORITY = 0x9931


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


class Model:
    def __init__(self, rom, m, units, grid):
        self.rom = rom
        self.m = m
        self.units = units
        self.grid = grid
        self.seen = {}
        self.shots = hzs.blank_shots()
        self.combat = hzs.Combat(rom, self.tile, self.block_terrain)
        # Map_ScanOutposts ($6BB0) leaves each outpost's index (in layout
        # order) in bits 10-13 of its layout word, the block number kept
        # and the word's other high bits dropped.
        self.layout = bytearray(m.layout)
        index = 0
        for i in range(64 * 64):
            lw = hv.w16(self.layout, i * 2)
            blk = lw & 0x1FF
            if hv.w16(m.blocks, 0x4000 + blk * 2) & 0xFF == 2:
                lw = blk | (index << 10)
                index += 1
                self.layout[i * 2] = lw >> 8
                self.layout[i * 2 + 1] = lw & 0xFF
        # The units' side of Combat_Update (hzunitmove_check.Units): the
        # posts with their holds, v_outpost_count and v_mapfx, which the
        # log doesn't carry (the posts' flags it does: taken from each J
        # line, pokes and all).
        self.posts, self.outpost_count = hzu.scan_posts(m.layout, m.blocks)
        self.mapfx = hzu.blank_state()["mapfx"]
        self.combat.units.layout_rw = self.layout_rw

    def layout_rw(self, ofs, value):
        if value is not None:
            self.layout[ofs] = value >> 8
            self.layout[ofs + 1] = value & 0xFF
        return hv.w16(self.layout, ofs)

    def units_state(self, S, g):
        """The parts of a Combat_Update state the units' move needs."""
        for k in range(18):
            self.posts[k]["flags"] = self.post_flags(g, k)
        S["posts"] = self.posts
        S["mapfx"] = self.mapfx
        S["counts"] = {
            "units": [g["units1"], g["units2"]],
            "types": [[(g[tk] >> ((15 - k) * 8)) & 0xFF for k in range(16)]
                      for tk in ("tc1", "tc2")],
            "built": [g["built1"], g["built2"]],
            "outposts": self.outpost_count}

    def units_state_back(self, S, g):
        C = S["counts"]
        g["units1"], g["units2"] = C["units"]
        g["built1"], g["built2"] = C["built"]
        for t, tk in enumerate(("tc1", "tc2")):
            g[tk] = int.from_bytes(bytes(C["types"][t]), "big")
        g["posts"] = int.from_bytes(bytes(p["flags"] for p in self.posts[:18]),
                                    "big")

    def w(self, a):
        return hv.w16(self.rom, a)

    # -- sound ------------------------------------------------------------
    def sfx(self, g, n):
        pri = self.rom[SFX_PRIORITY + n]
        if pri > g["sfx_priority"]:
            return
        g["sfx_priority"] = pri
        g["sfx_queued"] = n

    # -- the map ----------------------------------------------------------
    def tile(self, tx, ty):
        """(tile word, layout word) of tile (tx, ty)."""
        lw = hv.w16(self.layout, (((ty & 0xFC) >> 2) * 64
                                    + ((tx & 0xFC) >> 2)) * 2)
        blk = lw & 0x1FF
        word = hv.w16(self.m.blocks, blk * 32 + ((ty & 3) * 4 + (tx & 3)) * 2)
        return word, lw

    def block_terrain(self, lw):
        return hv.w16(self.m.blocks, 0x4000 + (lw & 0x1FF) * 2)

    def get_terrain(self, g, me):
        """Mech_GetTerrain."""
        water = 0xFFFF
        tx0 = ((me["x"] + 4) & 0xFFFF) >> 3
        ty0 = ((me["y"] + 4) & 0xFFFF) >> 3
        for r in range(4):
            for c in range(4):
                word, lw = self.tile((tx0 + c) & 0xFFFF, (ty0 + r) & 0xFFFF)
                typ = self.block_terrain(lw) & 0xFF
                if typ in (6, 8):
                    return 1 if g["map"] < 2 else 0
                if word & 0x200:
                    water >>= 1
                    continue
                if word & 0x8000:
                    return 2
                if (word & 0x1FF) < 0x3F:
                    return 3
        if water == 0:
            return 4
        if water == 0xFFFF:
            return 0
        return 2

    # -- v_grid -----------------------------------------------------------
    @staticmethod
    def mark_cell(me):
        """Grid_MarkMech's cell: & $FFE0, no column mask, & $FFF."""
        return ((((me["y"] + 0x10) & 0xFFE0) * 2)
                + (((me["x"] + 0x10) & 0xFFFF) >> 5)) & 0xFFF

    def grid_mark(self, me, ident):
        if me["form"] != 0:
            return
        if ident and me["flags"] & 0x20:
            return
        self.grid[self.mark_cell(me)] = ident

    @staticmethod
    def cell(me):
        """The collision code's cell: & $7E0 and & $3F."""
        return ((((me["y"] + 0x10) & 0x7E0) * 2)
                + ((((me["x"] + 0x10) & 0xFFFF) >> 5) & 0x3F)) & 0xFFF

    def occupant(self, M, ident):
        """(x, y, size, the byte at +$01) of a grid id."""
        if not ident & 0x40:
            o = M[1 if ident & 0x80 else 0]
            return s16(o["x"]), s16(o["y"]), o["size"], o["carried"]
        u = self.units[(50 if ident & 0x80 else 0) + ((ident - 1) & 0x3F)]
        if u is None:
            raise ValueError("grid id %02X names a free slot" % ident)
        return u["x"], u["y"], u["size"], u["type"]

    @staticmethod
    def overlap(me, occ, widen):
        own = me["size"] + (12 if widen else 0)
        other = occ[2] + (12 if widen else 0)
        ok = 0
        for axis, pos in ((0, "x"), (1, "y")):
            d = s16(occ[axis] - s16(me[pos]))
            if d >= 0:
                inside = (d & 0xFFFF) < own
            else:
                inside = ((-d) & 0xFFFF) < other
            ok += inside
        return ok == 2

    def neighbour(self, n):
        return s16(self.w(NEIGHBOURS + n * 2))

    def check_collision(self, M, g, me):
        """Mech_CheckCollision."""
        g["hit_flags"] = 0
        g["hit_id"] = 0
        g["event"] = 0
        if me["form"] == 0:
            base = self.cell(me)
            ident = self.grid[base]
            hit = ident != 0
            if not hit:
                for n in range(8):
                    c = (base + self.neighbour(n)) & 0xFFFF
                    if c >= 0x1000:
                        continue
                    ident = self.grid[c]
                    if not ident:
                        continue
                    occ = self.occupant(M, ident)
                    if self.overlap(me, occ, occ[3] == 4):
                        hit = True
                        break
            if hit:
                g["hit_flags"] |= 1
                g["hit_id"] = ident
                if ((ident >> 3) ^ me["flags"]) & 0x10:
                    g["hit_flags"] |= 8
        want = 3
        tx0 = ((me["x"] + 4) & 0xFFFF) >> 3
        ty0 = ((me["y"] + 4) & 0xFFFF) >> 3
        for r in range(4):
            for c in range(4):
                word, lw = self.tile((tx0 + c) & 0xFFFF, (ty0 + r) & 0xFFFF)
                if want & 1 and word & 0x8000:
                    g["hit_flags"] |= 2
                    want &= ~1
                if want & 2 and word & 0x2000:
                    g["hit_flags"] |= 4
                    want &= ~2
                    g["event3"] = lw
                    g["event"] = self.block_terrain(lw)
                if not want & 3:
                    return

    def find_unit_to_lift(self, me):
        base = ((((me["y"] + 0x10) & 0x7E0) * 2)
                + ((((me["x"] + 0x10) & 0xFFFF) >> 5) & 0x3F))
        for n in range(9):
            c = (base + (0 if n == 0 else self.neighbour(n - 1))) & 0xFFFF
            if c >= 0x1000:
                continue
            ident = self.grid[c]
            if not ident or not ident & 0x40:
                continue
            if ((me["flags"] << 3) ^ ident) & 0x80:
                continue
            slot = (ident - 1) & 0x3F
            u = self.units[(50 if ident & 0x80 else 0) + slot]
            if u is None or u["flags"] & 0x20:
                continue
            reach = ((0x20 - u["size"]) & 0xFF) + 0x10
            dx = (u["x"] + 8 - me["x"])
            dy = (u["y"] + 8 - me["y"])
            if ((u["x"] + 8) & 0xFFFF) < me["x"] or (dx & 0xFFFF) >= reach:
                continue
            if ((u["y"] + 8) & 0xFFFF) < me["y"] or (dy & 0xFFFF) >= reach:
                continue
            return slot
        return -1

    def land_blocked(self, M, me):
        base = self.cell(me)
        if self.grid[base]:
            return True
        supply = False
        if me["flags"] & 8:
            u = self.units[(50 if me["flags"] & 0x10 else 0) + me["carried"]]
            supply = u is not None and u["type"] == 4
        for n in range(9):
            c = (base + (0 if n == 0 else self.neighbour(n - 1))) & 0xFFFF
            if c >= 0x1000:
                continue
            ident = self.grid[c]
            if not ident:
                continue
            occ = self.occupant(M, ident)
            if self.overlap(me, occ, supply or occ[3] == 4):
                return True
        return False

    # -- unit records -----------------------------------------------------
    @staticmethod
    def blank_unit():
        u = dict.fromkeys(CU_FIELDS, 0)
        u["x"] = u["y"] = 0
        return u

    @staticmethod
    def carried_index(me):
        k = (50 if me["flags"] & 0x10 else 0) + me["carried"]
        return min(k, 99)

    def carried_record(self, me):
        k = self.carried_index(me)
        if self.units[k] is None:
            self.units[k] = self.blank_unit()
        return self.units[k]

    def count(self, key):
        self.seen[key] = self.seen.get(key, 0) + 1

    # -- Obj_TerrainEvent, a mech's side ----------------------------------
    @staticmethod
    def post_flags(g, index):
        return (g["posts"] >> ((17 - index) * 8)) & 0xFF

    def repair_refuel(self, a0, typ):
        """Obj_RepairRefuel: -> d3."""
        a1 = UNIT_STATS + ((typ << 5) & 0xFFFF)
        d3 = 0
        for field, add, at, width, bit in (("hp", 8, 0xA, 16, 1),
                                           ("fuel", 0x28, 0xE, 16, 2),
                                           ("ammo", 2, 0xD, 8, 4),
                                           ("missiles", 2, 0x1A, 8, 8)):
            d4 = a0[field]
            d2 = self.w(a1 + at) if width == 16 else self.rom[a1 + at]
            d1 = d4 + add
            if d1 >> width:
                d1 = d2
            if not d1 < d2:
                d1 = d2
            a0[field] = d1
            if d1 != d4:
                d3 |= bit
        return d3

    def outpost_service(self, g, me, kind, index):
        if kind >= 3 or g["group"]:
            return
        d1 = self.post_flags(g, index)
        if not d1 & 2:
            return
        if (d1 ^ (me["flags"] >> 4)) & 1:
            return
        if self.repair_refuel(me, 0xE if kind else me["type"]):
            self.count("services")
        if not kind or not me["flags"] & 8:
            return
        u = self.carried_record(me)
        me["refill"] = self.repair_refuel(u, u["type"])
        self.count("carried services")

    def create_carried(self, g, d1, d2, d3):
        """Unit_CreateCarried -> slot, or None (d0 -1)."""
        team = (d1 >> 4) & 1
        slot = None
        for k in range(50):
            u = self.units[team * 50 + k]
            if u is None or not u["flags"] & 0x80:
                slot = k
                break
        if slot is None:
            return None
        d2 &= 0xF
        if d2 == 3:
            d1 |= 1
        st = UNIT_STATS + d2 * 32
        rom = self.rom
        u = {"flags": d1, "type": d2, "order": d3, "hp": self.w(st + 0xA),
             "size": rom[st + 0xC], "ammo": rom[st + 0xD],
             "missiles": rom[st + 0x1A], "fuel": self.w(st + 0xE),
             "sprite_size": rom[st + 0x10] << 8, "speed": rom[st + 0x11],
             "max_speed": rom[st + 0x11], "prev_speed": 0,
             "turret": rom[st + 0x12], "power": rom[st + 0x17],
             "fuel_use": rom[st + 0x13], "x": 0, "y": 0}
        self.units[team * 50 + slot] = u
        key = "units%d" % (team + 1)
        g[key] = (g[key] + 1) & 0xFF
        tk = "tc%d" % (team + 1)
        shift = (15 - d2) * 8
        byte = ((g[tk] >> shift) + 1) & 0xFF
        g[tk] = (g[tk] & ~(0xFF << shift)) | (byte << shift)
        bk = "built%d" % (team + 1)
        g[bk] = (g[bk] + 1) & 0xFFFF or 0xFFFF
        return slot

    def pick_up(self, M, g, me, kind, index):
        """Outpost_PickUpProduct -> d2 (0 picked up)."""
        if me["flags"] & 0x40 or kind != 2 or not me["pad"] & 0x40:
            return 1
        if me["form"] != 1 or me["prod_state"] != 2:
            return 1
        d1 = self.post_flags(g, index)
        if not d1 & 2:
            return 1
        d2 = (me["flags"] >> 4) & 0xF
        if (d1 ^ d2) & 1:
            return 1
        team = d2 & 1
        if g["units%d" % (team + 1)] >= 0x32:
            if not me["view"] & 0x80:
                self.sfx(g, 0x1A)
                self.count("full beeps")
            else:
                self.count("full, view hidden")
            return 1
        slot = self.create_carried(g, (me["flags"] & 0x90) | 8,
                                   me["prod_type"], me["prod_order"])
        if slot is None:
            return 1
        me["flags"] |= 8
        me["carried"] = slot
        me["carried_order"] = me["prod_order"]
        me["form"] = 2
        me["prod_state"] = 0
        me["max_speed"] = self.rom[KIDOU + 0x22]
        d0 = self.rom[KIDOU + 0x25]
        if (not g["game_type"] & 0x8000
                and (M[0]["flags"] | M[1]["flags"]) & 1
                and g["difficulty"] == 0):
            d0 >>= 1
            self.count("halved pick-ups")
        me["fuel_use"] = d0
        self.sfx(g, 0x0C)
        self.count("pick-ups")
        return 0

    def terrain_event(self, M, g, me, kind):
        """Obj_TerrainEvent with a0 = me, d0 = kind."""
        if kind != 3:
            me["flow"] = 0
        ev = g["event"]
        t = ev & 0xF
        hi = (ev >> 8) & 0xFF
        if t == 2:
            lw = g["event3"]
            index = (((lw << 6) | (lw >> 10)) & 0xFFFF) & 0xF
            self.outpost_service(g, me, kind, index)
            self.pick_up(M, g, me, kind, index)
            # (then the capture, for an INFANTRY unit, d0 0)
        elif t == 3:
            index = 0x10 + (1 if ev & 0x100 else 0)
            self.outpost_service(g, me, kind, index)
            self.pick_up(M, g, me, kind, index)
        elif t == 6 and kind <= 1:
            me["flow"] = hi & 0x7F
            self.count("currents")
        elif t == 8 and kind < 2:
            group = g["group"]
            if group and (kind or group - 1):
                return
            if me["flags"] & 0x20:
                return
            d2, d3, d4 = 0x180, 0x20, 4
            hp = me["hp"] - d3
            if hp >= 0:
                me["hp"] = hp
                self.sfx(g, 0x1C)
                self.count("ground hits")
            else:
                me["flags"] |= 0x20
                me["hp"] = d2
                self.sfx(g, d4)
                self.count("ground deaths")
        elif t == 9 and kind <= 1:
            me["flow"] = (0x1F - (hi & 0x1F)) | 0x80
            self.count("slippery")
        elif t == 0xA and kind <= 1:
            me["flow"] = (hi & 3) | 0xC0
            self.count("slow")

    # -- Mech_Update ------------------------------------------------------
    @staticmethod
    def near(a, b):
        dx = (a["x"] - b["x"] + 0x1C) & 0xFFFF
        dy = (a["y"] - b["y"] + 0x1C) & 0xFFFF
        return dx < 0x38 and dy < 0x38

    def respawn(self, g, me, rival):
        k = KIDOU
        me["dir"] = 0
        me["turn_to"] = 0
        me["form"] = 1
        me["altitude"] = 0x10
        use = self.rom[k + 0x24]
        if rival["flags"] & 1 and not me["flags"] & 1:
            use += 1
        me["fuel_use"] = use & 0xFF
        me["max_speed"] = self.rom[k + 0x21]
        me["size"] = 0x20
        me["hp"] = self.w(k + 0xA)
        me["fuel"] = self.w(k + 0xE) >> 2
        me["power"] = self.rom[k + 0x17]
        me["frame_loaded"] = 0xFFFF
        me["ammo"] = self.rom[k + 0xD]
        for f in ("vel_x", "vel_y", "tvx", "tvy", "cam_dx", "cam_dy",
                  "sub_x", "sub_y", "speed"):
            me[f] = 0
        key = "spawns2" if me["flags"] & 0x10 else "spawns1"
        g[key] = 0xFFFF if g[key] == 0xFFFF else g[key] + 1

    def dying(self, g, me, rival):
        me["hp"] = (me["hp"] - 1) & 0xFFFF
        if me["hp"] == 0:
            if me["flags"] & 8:
                me["flags"] &= ~8
                team = 1 if me["flags"] & 0x10 else 0
                u = self.units[team * 50 + me["carried"]]
                if u is not None:       # a free slot's flags byte is 0
                    u["flags"] = 0
                g["units%d" % (team + 1)] = (g["units%d" % (team + 1)] - 1) & 0xFF
            me["flags"] &= ~0x20
            self.respawn(g, me, rival)
            return
        if me["hp"] == 0xA:
            self.sfx(g, 0x1B)
        me["speed"] = 0
        f = me["form"] + 1
        me["form"] = 0 if f > 2 else f
        cam = ("cam2x", "cam2y") if me["flags"] & 0x10 else ("cam1x", "cam1y")
        for home, c, pos, cd in (("home_cx", cam[0], "x", "cam_dx"),
                                 ("home_cy", cam[1], "y", "cam_dy")):
            d = s16(me[home] - g[c])
            step = -8 if d < 0 else 8
            if (abs(d) & 0xFFFF) < 8:
                step = d
            me[cd] = step & 0xFFFF
            me[pos] = (me[pos] + step) & 0xFFFF
        for home, sc, pos in (("home_sx", "sx", "x"), ("home_sy", "sy", "y")):
            d = s16(me[home] - me[sc])
            step = -1 if d < 0 else (0 if d == 0 else 1)
            me[sc] = (me[sc] + step) & 0xFFFF
            me[pos] = (me[pos] + step) & 0xFFFF
        # Mech_ClearHomePad
        dx = (me["home_cx"] + me["home_sx"] - rival["x"] + 0x26) & 0xFFFF
        dy = (me["home_cy"] + me["home_sy"] - 0x20 - rival["y"] + 0x26) & 0xFFFF
        if dx < 0x4C and dy < 0x4C:
            rival["vel_x"] = 0x200 if dx <= 0x26 else 0xFE00
            rival["vel_y"] = (-rival["vel_y"]) & 0xFFFF

    def buttons(self, M, g, me, rival):
        k = KIDOU
        pressed = (me["pad"] ^ me["pad_prev"]) & me["pad"]
        if me["flags"] & 0x40:
            return 0xFF     # the menu's input: hz_mech freezes the menus
        if pressed & 0x20:
            me["flags"] |= 0x40
            return 'menu'
        if pressed & 0x40:
            if me["form"] == 0:
                overlap = (not me["flags"] & 0x20 and not rival["flags"] & 0x20
                           and self.near(me, rival))
                if not overlap and self.get_terrain(g, me) != 3:
                    me["form"] = 1
                    me["blocked"] = 0
                    me["max_speed"] = self.rom[k + 0x21]
                    use = self.rom[k + 0x24]
                    if rival["flags"] & 1 and not me["flags"] & 1:
                        use += 1
                    me["fuel_use"] = use & 0xFF
                    for f in ("vel_x", "vel_y", "tvx", "tvy"):
                        me[f] = 0
                    me["transform"] = 0x13
                    me["tdelay"] = 6
                    me["turn_to"] = me["dir"]
                    me["dir"] = 0
                    self.sfx(g, 0x0A)
            elif me["form"] == 1:
                slot = self.find_unit_to_lift(me)
                if slot >= 0:
                    self.lift(g, me, rival, slot)
                elif (self.get_terrain(g, me) < 2
                      and not self.land_blocked(M, me)):
                    me["form"] = 0
                    me["max_speed"] = self.rom[k + 0x20]
                    me["fuel_use"] = self.rom[k + 0x23]
                    me["speed"] = 0
                    me["transform"] = 0x8D
                    me["tdelay"] = 6
                    me["turn_to"] = me["dir"]
                    me["dir"] = 0
                    self.sfx(g, 0x0B)
            else:
                self.drop(M, g, me, rival)
        elif pressed & 0x10:
            self.fire_gun(M, g, me)
        return self.rom[JOY_TO_DIR + (me["pad"] & 0xF)]

    # -- lifting and dropping (.lift $3512, .drop $3578) --------------------
    def kidou_fuel_use(self, form, me, rival):
        use = self.rom[KIDOU + 0x23 + form]
        if rival["flags"] & 1 and not me["flags"] & 1:
            use += 1
        return use & 0xFF

    def lift(self, g, me, rival, slot):
        u = self.units[min((50 if me["flags"] & 0x10 else 0) + slot, 99)]
        me["carried"] = slot
        me["carried_order"] = u["order"]
        me["flags"] |= 8
        u["flags"] |= 8
        me["form"] = 2
        me["max_speed"] = self.rom[KIDOU + 0x22]
        d0 = self.kidou_fuel_use(2, me, rival)
        if (not g["game_type"] & 0x8000 and (me["flags"] | rival["flags"]) & 1
                and g["difficulty"] == 0):
            d0 >>= 1
            self.count("halved lifts")
        me["fuel_use"] = d0
        self.grid[u["grid_cell"]] = 0           # Grid_ClearObj
        self.sfx(g, 0x0C)
        self.count("lifts")

    def set_sprite(self, u, pal):
        """Unit_SetSprite ($2344), d2 = pal; d0's high byte is 0 here."""
        if u["flags"] & 0x20:
            return
        d4 = u["speed"] & 0xFFFF
        d3 = u["prev_speed"]
        u["prev_speed"] = d4 & 0xFF
        d0 = (u["turn_to"] - u["dir"]) & 0xF
        if d0:
            step, vel = (1 if d0 < 8 else -1), True
        else:
            step, vel = 0, d3 != d4 & 0xFF
        if vel:
            u["dir"] = (u["dir"] + step) & 0xF
            h = (u["dir"] + (8 if u["flags"] & 2 else 0)) & 0xF
            u["tvx"] = (s16(d4) * s16(self.w(SINE + h * 4))) & 0xFFFF
            u["tvy"] = (s16(d4) * s16(self.w(SINE + h * 4 + 2))) & 0xFFFF
        if u["type"] != 8:
            u["attr"] = self.w(0x244A + u["type"] * 32 + u["dir"] * 2) | pal
        else:
            attr = (0x400 + (u["aim_dir"] & 0xF) * 4) | pal
            if d4:
                attr += (u["tick"] << 6) & 0x40
            u["attr"] = attr & 0xFFFF
        if u["turret"] & 0x80:
            d1 = (u["aim_dir"] - u["turret_dir"]) & 0xF
            step = 0 if d1 == 0 else (1 if d1 < 8 else -1)
            u["turret_dir"] = (u["turret_dir"] + step) & 0xF
            u["overlay"] = (self.w(0x244A + (u["turret"] & 0x7F) * 32
                                   + u["turret_dir"] * 2) | pal)

    def drop(self, M, g, me, rival):
        if self.land_blocked(M, me):
            self.count("drops blocked")
            return
        t = self.get_terrain(g, me)
        u = self.carried_record(me)
        for f in hzu.FIELDS:
            u.setdefault(f, 0)
        if u["flags"] & 1:
            if t != 4:
                self.count("boat drops refused")
                return
        elif t != 0 and (me["flags"] & 1 or t != 1):
            self.count("drops refused")
            return
        me["form"] = 1
        me["max_speed"] = self.rom[KIDOU + 0x21]
        me["fuel_use"] = self.kidou_fuel_use(1, me, rival)
        for f in ("vel_x", "vel_y", "tvx", "tvy"):
            me[f] = 0
        half = ((0x20 - u["size"]) & 0xFF) >> 1
        u["order"] = me["carried_order"]
        u["x"] = s16(me["x"] + half)
        u["y"] = s16(me["y"] + half)
        u["max_speed"] = self.rom[UNIT_STATS + u["type"] * 32 + 0x11]
        u["speed"] = (u["speed"] & 0xFF00) | u["max_speed"]
        u["vel_x"] = u["vel_y"] = 0
        d = (me["dir"] + 1) & 0xF
        u["dir"] = d
        d = (d - 1) & 0xF
        u["turn_to"] = u["turret_dir"] = u["aim_dir"] = d
        me["flags"] &= ~8
        u["flags"] &= ~8
        u["shot_flags"] = 0
        u["blocked"] = 0
        team2 = u["flags"] & 0x10
        ident = (0xC0 if team2 else 0x40) | ((me["carried"] + 1) & 0x3F)
        cell = hzu.Units.mark_cell(u)
        self.grid[cell] = ident
        u["grid_cell"] = cell
        self.set_sprite(u, 0x6000 if team2 else 0x4000)
        g["hit_flags"] = 0
        g["hit_id"] = 0
        self.combat.units.check_terrain(g, u)
        u["blocked"] = g["hit_flags"] & 0xFF
        self.sfx(g, 0x0D)
        self.count("drops" if t == 0 else "drops on terrain %d" % t)

    def fire_gun(self, M, g, me):
        """Mech_FireGun: Shot_FireBullet with the mech's flags & $90 from
        (x + $C, y + $C), KIDOU's shot speed and life."""
        if me["transform"]:
            return
        if not me["flags"] & 1 and me["fire_delay"]:
            return
        p = 0 if me is M[0] else 1
        S = {"mechs": M, "units": self.units, "grid": self.grid,
             "shots": self.shots}
        if self.combat.fire(S, g, hzs.OWNER_MECH + p, me["flags"] & 0x90,
                            me["dir"], me["x"] + 0xC, me["y"] + 0xC,
                            self.rom[KIDOU + 0x14], self.rom[KIDOU + 0x15],
                            False):
            return
        me["fire_delay"] = 6
        if g["free_ammo"]:
            me["ammo"] = (me["ammo"] + 1) & 0xFF

    def steer(self, g, me, want):
        if me["transform"]:
            return
        if want == 0xFF:
            sp = s16(me["speed"] - 4)
            me["speed"] = 0 if sp < 0 else sp
            me["lead"] = 0
            me["held"] = 0
            return
        me["turn_to"] = want
        if g["tick"] & 3 == 0:
            diff = (want - me["dir"]) & 0xF
            step = 0
            if diff == 0:
                me["flags"] &= ~2
            elif me["form"] == 0 and me["held"] < 0x10:
                if diff == 8:
                    me["flags"] |= 2
                elif diff < 7:
                    step = 1
                    me["flags"] &= ~2
                elif diff > 9:
                    step = -1
                    me["flags"] &= ~2
                elif diff == 7:
                    step = -1
                    me["flags"] |= 2
                else:
                    step = 1
                    me["flags"] |= 2
            elif diff < 8:
                step = 1
                me["flags"] &= ~2
            else:
                step = -1
            me["dir"] = (me["dir"] + step) & 0xF
        change = 6
        heading = (me["dir"] + (8 if me["flags"] & 2 else 0)) & 0xF
        if heading != me["turn_to"]:
            change = -4
            if me["lead"] == 2:
                me["lead"] = 1
        sp = s16(me["speed"] + change)
        if sp < 0:
            sp = 0
        cap = me["max_speed"]
        if me["fuel"] == 0:
            cap >>= 2
        lo = sp & 0xFF
        if lo > cap:
            lo = cap
        me["speed"] = (me["speed"] & 0xFF00) | lo
        last = me["pad_last"]
        me["pad_last"] = me["pad"]
        if not me["held"] & 0x80:
            me["held"] = (me["held"] + 1) & 0xFF
        if (me["pad"] & 0xF) != (last & 0xF):
            me["held"] = 0

    @staticmethod
    def accel(v, t, step):
        v, t = s16(v), s16(t)
        if t == v:
            return v & 0xFFFF
        if t > v:
            v = s16(v + step)
            if not t > v:
                v = t
        else:
            v = s16(v - step)
            if not t < v:
                v = t
        return v & 0xFFFF

    def move(self, M, g, me, rival):
        if me["transform"]:
            for f in ("vel_x", "vel_y", "tvx", "tvy"):
                me[f] = 0
        else:
            h = (me["dir"] + (8 if me["flags"] & 2 else 0)) & 0xF
            sp = s16(me["speed"])
            me["tvx"] = (sp * s16(self.w(SINE + h * 4))) & 0xFFFF
            me["tvy"] = (sp * s16(self.w(SINE + h * 4 + 2))) & 0xFFFF
        old = (me["x"], me["y"], me["cam_dx"], me["cam_dy"])
        if me["form"] != 0:
            me["vel_x"] = self.accel(me["vel_x"], me["tvx"], 0x18)
            me["vel_y"] = self.accel(me["vel_y"], me["tvy"], 0x18)
        else:
            fl = me["flow"]
            if not fl & 0x80:
                amount = (fl & 0xF) << 6
                vx, vy = s16(me["tvx"]), s16(me["tvy"])
                if fl:
                    # bits 4-6: 0 up, 1 up-right ... 7 up-left
                    dxs = (0, 1, 1, 1, 0, -1, -1, -1)
                    dys = (-1, -1, 0, 1, 1, 1, 0, -1)
                    code = (fl >> 4) & 7
                    vx += dxs[code] * amount
                    vy += dys[code] * amount
                me["vel_x"], me["vel_y"] = vx & 0xFFFF, vy & 0xFFFF
            elif fl & 0x40:
                vx, vy = s16(me["tvx"]), s16(me["tvy"])
                k = fl & 3
                if k == 1:
                    vx, vy = vx - (vx >> 2), vy - (vy >> 2)
                elif k == 2:
                    vx, vy = vx >> 1, vy >> 1
                elif k == 3:
                    vx, vy = vx >> 2, vy >> 2
                me["vel_x"], me["vel_y"] = vx & 0xFFFF, vy & 0xFFFF
            else:
                me["vel_x"] = self.accel(me["vel_x"], me["tvx"], fl & 0x1F)
                me["vel_y"] = self.accel(me["vel_y"], me["tvy"], fl & 0x1F)
        for sub, v, pos, cd in (("sub_x", "vel_x", "x", "cam_dx"),
                                ("sub_y", "vel_y", "y", "cam_dy")):
            acc = (me[sub] + me[v]) & 0xFFFF
            px = s8(acc >> 8)
            me[sub] = acc & 0xFF
            me[pos] = (me[pos] + px) & 0xFFFF
            me[cd] = px & 0xFFFF
        me["flow"] = 0
        self.check_collision(M, g, me)
        me["terrain"] = g["event"]
        if me["form"] == 0:
            me["blocked"] = g["hit_flags"] & 0xFF
        if g["hit_flags"] & 4:
            self.terrain_event(M, g, me, 2 if me["form"] else 1)
        if me["form"] != 0:
            a, b = M
            if (a["form"] and b["form"] and not a["flags"] & 0x20
                    and not b["flags"] & 0x20 and self.near(a, b)):
                me["x"], me["y"], me["cam_dx"], me["cam_dy"] = old
                me["blocked"] |= 9
                me["bumped"] = 0x64 if me["flags"] & 0x10 else 0x65
                rival["vel_x"], rival["vel_y"] = me["vel_x"], me["vel_y"]
                me["vel_x"] = (-me["vel_x"]) & 0xFFFF
                me["vel_y"] = (-me["vel_y"]) & 0xFFFF
                self.sfx(g, 0x0E)
            return
        if not g["hit_flags"] & 3:
            return
        me["x"], me["y"], me["cam_dx"], me["cam_dy"] = old
        if not g["hit_flags"] & 1:
            return
        ident = g["hit_id"]
        if not ident & 0x40:
            me["bumped"] = 0x65 if ident & 0x80 else 0x64
        else:
            b = (ident & 0x3F) - 1
            if ident & 0x80:
                b += 0x32
            me["bumped"] = b & 0xFF

    def fuel_frame(self, g, me):
        if not me["transform"]:
            count = (me["speed"] + me["fuel_count"]) & 0xFF
            if count >= 0x4F:
                count = (count - 0x50) & 0xFF
                step = me["walk"] + 1
                me["walk"] = 0 if step > 2 else step
                if not g["free_ammo"]:
                    if me["fuel"] < me["fuel_use"]:
                        me["fuel"] = 0
                        me["flags"] |= 0x20
                        me["hp"] = 0x180
                        self.sfx(g, 4)
                    else:
                        me["fuel"] -= me["fuel_use"]
            me["fuel_count"] = count
        else:
            left = me["transform"]
            if (left & 0x7F) >= 9:
                me["tdelay"] = (me["tdelay"] - 1) & 0xFF
                if me["tdelay"]:
                    return
                me["tdelay"] = 6
                left -= 1
                me["transform"] = 0 if (left & 0x7F) == 0 else left & 0xFF
                table, last = ((TRANSFORM_FRAMES2, 0xC) if left & 0x80
                               else (TRANSFORM_FRAMES, 0x12))
                me["frame"] = self.w(table + (last - (left & 0x7F)) * 2)
                return
            now = (left - 1) & 0xFF
            if (now & 0x7F) == 0:
                now = 0
            me["transform"] = now
            diff = (me["turn_to"] - me["dir"]) & 0xF
            if diff:
                me["dir"] = (me["dir"] + (-1 if diff >= 8 else 1)) & 0xF
            me["fuel_count"] = 0
            me["walk"] = 0
        if me["form"] != 0:
            frame = self.w(JET_FRAMES + (me["dir"] & 0xF) * 2)
            if me["form"] == 2:
                frame += 5
            me["frame"] = frame & 0xFFFF
            if me["fire_delay"]:
                me["fire_delay"] -= 1
            return
        me["frame"] = me["dir"] & 0xF
        delay = me["fire_delay"]
        if delay:
            me["fire_delay"] -= 1
        if delay >= 3:
            me["frame"] += 0x10
            me["walk"] = 0
            return
        if me["walk"]:
            me["frame"] = 0x20 + (me["dir"] & 0xE) + ((me["walk"] - 1) & 1)

    def screen(self, g, me):
        sx, sy = me["sx"], me["sy"]
        if me["lead"] == 1:
            e = (LEAD_SPLIT if g["split"] else LEAD_WIDE) + (me["dir"] & 0xE) * 2
            moved = 0
            for off, sc, cd, cur in ((0, "sx", "cam_dx", sx),
                                     (2, "sy", "cam_dy", sy)):
                t = self.w(e + off)
                if cur != t:
                    step = 1 if cur < t else -1
                    me[sc] = (me[sc] + step) & 0xFFFF
                    me[cd] = (me[cd] - step) & 0xFFFF
                    moved = 1
            if not moved:
                me["lead"] = 2
            return
        if me["lead"] == 2:
            return
        left, right = (0x21, 0x4F) if g["split"] else (0x71, 0xCF)
        out_x = sx > right or sx < left
        out_y = sy > 0x97 or sy < 0x49
        if not out_x:
            me["sx"] = (sx + me["cam_dx"]) & 0xFFFF
            me["cam_dx"] = 0
        if not out_y:
            me["sy"] = (sy + me["cam_dy"]) & 0xFFFF
            me["cam_dy"] = 0
        if out_x or out_y:
            me["lead"] = 1

    def update(self, M, g, p):
        me, rival = M[p], M[1 - p]
        me["cam_dx"] = me["cam_dy"] = 0
        if me["flags"] & 0x20:
            if me["menu_closing"]:
                return
            self.dying(g, me, rival)
        else:
            if me["menu_closing"]:
                return
            want = self.buttons(M, g, me, rival)
            if want == 'menu':
                return          # C opened the order menu: the update ends
            self.steer(g, me, want)
            self.move(M, g, me, rival)
        self.fuel_frame(g, me)
        if me["flags"] & 0x20:
            return
        self.screen(g, me)


def parse(line):
    parts = line.split()
    kind, n_pass, p = parts[0], int(parts[1], 16), int(parts[2], 16)
    vals = [int(v, 16) for v in parts[3:]]
    n = len(MECH_FIELDS)
    mechs = [dict(zip(MECH_FIELDS, vals[:n])),
             dict(zip(MECH_FIELDS, vals[n:2 * n]))]
    g = dict(zip(GLOBALS, vals[2 * n:]))
    return kind, n_pass, p, mechs, g


def check_file(rom, checker, path):
    with open(path) as f:
        lines = [ln for ln in f if ln.strip()]
    head = lines[0].split()
    if head[0] != "HZM4":
        return ["not an hz_mech log"], {}
    n_map, mode, difficulty = (int(v, 16) for v in head[1:4])
    m = checker.map(n_map)
    units, grid = hv.spawn_units(rom, m, mode, difficulty)
    model = Model(rom, m, units, grid)
    errors = []
    seen = {"updates": 0, "take-offs": 0, "landings": 0, "deaths": 0,
            "respawns": 0, "bumps": 0, "jet meets": 0, "lead 1": 0}
    pending = None          # the mech whose end-of-update mark is due
    prev = None
    i = 1
    while i < len(lines):
        if lines[i].startswith("J "):
            # Combat_Update, from the state just before it ("J": after both
            # updates, the camera steps and UnitAI_Update's group step) to
            # the one after it ("K").
            head, shot_part, _ = lines[i].split(" /")
            _, n_pass, _, MJ, gJ = parse(head)
            headK, shotK, crcK = lines[i + 1].split(" /")
            kK, _, _, MK, gK = parse(headK)
            where = "pass %d, Combat_Update" % n_pass
            if kK != "K":
                errors.append("line %d: out of step" % i)
                break
            want = prev_g["group"]
            want = 0 if want + 1 > 0xB else want + 1
            if gJ["group"] != want:
                errors.append("%s: v_unit_ai_group %X, after %X expected %X"
                              % (where, gJ["group"], prev_g["group"], want))
            # The last update's end-of-update mark, where its camera step
            # put it; the shots fired in the updates.
            if pending is not None:
                model.grid_mark(MJ[pending], 0x81 if pending else 0x01)
                pending = None
            if zlib.crc32(bytes(model.grid)) != gJ["grid_crc"]:
                errors.append("%s: v_grid differs from the model's" % where)
            if zlib.crc32(bytes(model.layout)) != gJ["layout_crc"]:
                errors.append("%s: the layout differs from the model's" % where)
            hzs.compare_shots(model.shots, hzs.parse_shots(shot_part.split()),
                              where + " (before)", errors)
            S = {"mechs": MJ, "units": model.units, "grid": model.grid,
                 "shots": model.shots, "hq_hp": [0, 0], "hq_alert": [0, 0]}
            model.units_state(S, gJ)
            model.combat.combat(S, gJ)
            model.units_state_back(S, gJ)
            gJ["grid_crc"] = zlib.crc32(bytes(model.grid))
            gJ["layout_crc"] = zlib.crc32(bytes(model.layout))
            # MapFx_Update at the frame's end: its layout writes (the
            # drawing isn't checked here)
            model.combat.units.mapfx_update(S, None, None)
            for q in (0, 1):
                # the unit in its carried slot, as the shots left it
                u = model.units[model.carried_index(MJ[q])]
                if u is not None:
                    for f in CU_FIELDS:
                        MJ[q]["cu_" + f] = u.get(f, 0)
            for q in (0, 1):
                for f in MECH_FIELDS:
                    if MJ[q][f] != MK[q][f]:
                        errors.append("%s: mech %d %s %X, model %X"
                                      % (where, q, f, MK[q][f], MJ[q][f]))
            for f in GLOBALS:
                if gJ[f] != gK[f]:
                    errors.append("%s: %s %X, model %X" % (where, f, gK[f], gJ[f]))
            hzs.compare_shots(model.shots, hzs.parse_shots(shotK.split()),
                              where, errors)
            if hzs.unit_crc(model.units, True) != int(crcK, 16):
                errors.append("%s: the units differ from the model's" % where)
            prev = MK
            prev_g = gK
            i += 2
            continue
        if i + 1 >= len(lines):
            break
        if lines[i].startswith("P "):
            # The places after the last pass's camera steps: the mark due
            # from the last update goes there.
            pos = [int(v, 16) for v in lines[i].split()[1:5]]
            for q in (0, 1):
                prev[q]["x"], prev[q]["y"] = pos[q * 2], pos[q * 2 + 1]
            if pending is not None:
                model.grid_mark(prev[pending], 0x81 if pending else 0x01)
                pending = None
            i += 1
            continue
        if lines[i].startswith("C "):
            q, fl, slot = (int(v, 16) for v in lines[i].split()[1:4])
            prev[q]["flags"] = fl
            prev[q]["carried"] = slot
            i += 1
            continue
        if lines[i].startswith("U "):
            k, hp, fuel, ammo, missiles = (int(v, 16)
                                           for v in lines[i].split()[1:6])
            if model.units[k] is None:
                model.units[k] = model.blank_unit()
            model.units[k].update(hp=hp, fuel=fuel, ammo=ammo,
                                  missiles=missiles)
            i += 1
            continue
        if lines[i].startswith("I "):
            # a unit made an INFANTRY on an outpost
            k, typ, size, x, y, blocked = (int(v, 16)
                                           for v in lines[i].split()[1:7])
            model.units[k].update(type=typ, size=size, x=s16(x), y=s16(y),
                                  blocked=blocked)
            model.count("INFANTRY pokes")
            i += 1
            continue
        if lines[i].startswith("H "):
            k, h1, h2 = (int(v, 16) for v in lines[i].split()[1:4])
            model.posts[k]["h1"], model.posts[k]["h2"] = h1, h2
            i += 1
            continue
        if lines[i].startswith("L "):
            q = int(lines[i].split()[1], 16)
            prev[q]["form"] = 0
            prev[q]["speed"] = 0
            i += 1
            continue
        if lines[i].startswith("T "):
            # A robot moved by the harness: first the mark due from the
            # last update, then its Grid_MarkMech writes at both places.
            q, fx, fy, x, y = (int(v, 16) for v in lines[i].split()[1:6])
            me = prev[q]
            # From where it is now: a jet poke earlier in the pass may have
            # moved it before an L line made it a robot.
            me["x"], me["y"] = fx, fy
            model.grid_mark(me, 0)
            me["x"], me["y"] = x, y
            model.grid_mark(me, 0x81 if q else 0x01)
            i += 1
            continue
        k1, n_pass, p, M, g = parse(lines[i])
        k2, n2, p2, ME, gE = parse(lines[i + 1])
        i += 2
        if k1 != "S" or k2 != "E" or (n2, p2) != (n_pass, p):
            errors.append("line %d: out of step" % i)
            break
        if prev is not None:
            for q in (0, 1):
                for f in STEADY:
                    if M[q][f] != prev[q][f]:
                        errors.append("pass %d p%d: mech %d %s changed "
                                      "between updates" % (n_pass, p, q, f))
            want = prev_g["group"]           # stepped by the frame (K)
            if g["group"] != want:
                errors.append("pass %d p%d: v_unit_ai_group %X, after %X "
                              "expected %X" % (n_pass, p, g["group"],
                                               prev_g["group"], want))
        for q in (0, 1):
            k = model.carried_index(M[q])
            u = model.units[k]
            if u is None:
                u = model.units[k] = model.blank_unit()
                for f in CU_FIELDS:
                    u[f] = M[q]["cu_" + f]
            # obj_flags b6 (on screen) is Sprites_DrawUnits', between
            # passes: taken from the line.
            if "flags" in u and not (u["flags"] ^ M[q]["cu_flags"]) & ~0x40:
                u["flags"] = M[q]["cu_flags"]
            for f in CU_FIELDS:
                if f not in u:
                    u[f] = M[q]["cu_" + f]
                elif u[f] != M[q]["cu_" + f]:
                    errors.append("pass %d p%d: unit %d %s %X, model %X "
                                  "(before)" % (n_pass, p, k, f,
                                               M[q]["cu_" + f], u[f]))
        if pending is not None:
            model.grid_mark(M[pending], 0x81 if pending else 0x01)
        model.grid_mark(M[p], 0)
        crc = zlib.crc32(bytes(model.grid))
        if crc != g["grid_crc"]:
            errors.append("pass %d p%d: v_grid before the update differs "
                          "from the model's" % (n_pass, p))
        if zlib.crc32(bytes(model.layout)) != g["layout_crc"]:
            errors.append("pass %d p%d: the layout before the update differs "
                          "from the model's (MapFx_Update)" % (n_pass, p))
        before = {"form": M[p]["form"], "flags": M[p]["flags"],
                  "lead": M[p]["lead"]}
        lifts = model.seen.get("lifts", 0)
        try:
            model.update(M, g, p)
        except (ValueError, TypeError) as e:
            errors.append("pass %d p%d: model: %s" % (n_pass, p, e))
            break
        if model.seen.get("lifts", 0) != lifts:
            # A unit just lifted was on the ground: its b6 is
            # Sprites_DrawUnits', not the model's.
            u = model.carried_record(M[p])
            u["flags"] = (u["flags"] & ~0x40) | (ME[p]["cu_flags"] & 0x40)
        for q in (0, 1):
            u = model.units[model.carried_index(M[q])]
            for f in CU_FIELDS:
                M[q]["cu_" + f] = 0 if u is None else u[f]
        for q in (0, 1):
            for f in MECH_FIELDS:
                if M[q][f] != ME[q][f]:
                    errors.append("pass %d p%d: mech %d %s %X, model %X"
                                  % (n_pass, p, q, f, ME[q][f], M[q][f]))
        g["grid_crc"] = zlib.crc32(bytes(model.grid))
        for f in GLOBALS:
            if g[f] != gE[f]:
                errors.append("pass %d p%d: %s %X, model %X"
                              % (n_pass, p, f, gE[f], g[f]))
        seen["updates"] += 1
        after = ME[p]
        if before["form"] == 0 and after["form"] == 1 and after["transform"]:
            seen["take-offs"] += 1
        if before["form"] == 1 and after["form"] == 0 and after["transform"]:
            seen["landings"] += 1
        if not before["flags"] & 0x20 and after["flags"] & 0x20:
            seen["deaths"] += 1
        if not before["flags"] & 0x40 and after["flags"] & 0x40:
            seen["menus opened by C"] = seen.get("menus opened by C", 0) + 1
        if before["flags"] & 0x20 and not after["flags"] & 0x20:
            seen["respawns"] += 1
        if after["blocked"] & 3 and after["form"] == 0:
            seen["bumps"] += 1
        if gE["sfx_queued"] == 0x0E:
            seen["jet meets"] += 1
        if before["lead"] != 1 and after["lead"] == 1:
            seen["lead 1"] += 1
        pending = p
        prev = ME
        prev_g = gE
        if len(errors) > 20:
            break
    seen.update(model.seen)
    seen.update(model.combat.seen)
    for k, v in model.combat.units.seen.items():
        seen["units: " + k] = v
    return errors, seen


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    rom = open(argv[1], "rb").read()
    checker = hv.Checker(rom)
    paths = []
    for a in argv[2:]:
        if os.path.isdir(a):
            paths += sorted(os.path.join(a, n) for n in os.listdir(a)
                            if n.endswith(".txt"))
        else:
            paths.append(a)
    bad = 0
    total = {}
    for path in paths:
        errors, seen = check_file(rom, checker, path)
        name = os.path.basename(path)
        if errors:
            bad += 1
            print("%-22s %d errors" % (name, len(errors)))
            for e in errors[:12]:
                print("   ", e)
        else:
            print("%-22s ok: %s" % (name, ", ".join(
                "%d %s" % (v, k) for k, v in seen.items())))
            for k, v in seen.items():
                total[k] = total.get(k, 0) + v
    if len(paths) > 1:
        print("total: " + ", ".join("%d %s" % (v, k) for k, v in total.items()))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
