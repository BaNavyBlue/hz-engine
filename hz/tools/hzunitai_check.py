#!/usr/bin/env python3
"""The unit AI port (src/game/unit_ai.c) against the original code.

    hzunitai_check.py ROM SYM HZ_UNITAI ASSETS [--cases N] [--seed N]
                      [--keep DIR] [--quiet]

Makes up N states as images of the 68k's RAM -- units of every type and
order in every state their order gives them, targets, far enemies, SUPPLYs
approaching, docked and detouring, outposts, HQs, path hints, shots nearly
or wholly used up, the attract demo, every difficulty and CPU boost -- and
runs 1-4 UnitAI_Update calls on each twice: through the port (HZ_UNITAI,
tools/hz_unitai.c, which loads the image into the game's state and writes
it back) and through the original routines from the ROM image on
tools/m68krun.py. Then compares the units, the shots, the RNG seed, the
sound effect queue and v_unit_ai_group byte for byte.

The original is run with the port's deliberate changes (the CHANGED notes
in unit_ai.c) applied as patches at the original's own instructions, so
that what's compared is everything else:

  Unit_SetSprite        d0 & $FF on entry: the turn is judged by the
                        difference alone (the original's high byte is
                        whatever the unit's code left)
  OrderPatrol .dodge    d1 = 2 before its subq: in the demo too the step
                        is +-1 (the original's is a leftover register - 1)
  AimAtMech .store      +$4C = the enemy mech it aimed at (the original
                        leaves the far pointer unset)
  OrderSupply .approach a3 | $FF000000 before the cmpa: one form of the
                        customer pointer, so bumping the customer docks
                        and a SUPPLY carried off keeps its customer

States the port can't hold, or the original can't reach, aren't made: a
state of another order under guard, guard near or hold (only another
order's would leave one), unit types $A-$F, a SUPPLY's cursor past 49,
order F on anything but a SUPPLY, a unit's centre past 2047 (the map's
edge stops units before that; there the original's ring scan reads the
wrong cells). Not compared: v_scan_list/v_scan_counts
(scratch the port keeps local), a target pointer (+$48) once its target
id (+$46) is 0 (the original keeps a stale one there).
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m68k                                            # noqa: E402
from m68krun import M68k, M68kError, load_symbols      # noqa: E402

RAM = 0xFF0000
A_LAYOUT = 0x5000
A_POSTS = 0x89B4
A_GROUP = 0x8A70
A_MECH1 = 0x8B94
A_UNITS = 0x8D94
A_SHOTS = 0xACD4
A_GRID = 0xB4D4
A_SEED = 0xF7C0
A_OPTIONS = 0xF84A
A_SFX_PRI = 0xF90A
A_SFX_Q = 0xF90C
A_GAMETYPE = 0xF924
A_BOOST = 0xF92E

UNIT_FIELDS = [
    (0x00, 1, 'flags'), (0x01, 1, 'type'), (0x02, 1, 'order'),
    (0x03, 1, 'dir'), (0x04, 1, 'turn_to'), (0x05, 1, 'tick'),
    (0x06, 1, 'phase'), (0x07, 1, 'fire_delay'), (0x08, 1, 'power'),
    (0x09, 1, 'hit_victim'), (0x0A, 2, 'hp'), (0x0C, 1, 'size'),
    (0x0D, 1, '+0D'), (0x0E, 1, 'bumped'), (0x0F, 1, 'fuel_use'),
    (0x10, 2, 'x'), (0x12, 2, 'y'), (0x14, 2, 'sub_x'), (0x16, 2, 'sub_y'),
    (0x18, 1, 'missiles'), (0x19, 1, 'ammo'), (0x1A, 2, 'fuel'),
    (0x1C, 2, 'terrain'), (0x1E, 2, 'sprite_attr'), (0x20, 2, 'sprite_size'),
    (0x22, 1, 'shot_flags'), (0x23, 1, 'blocked'), (0x24, 2, 'speed'),
    (0x26, 1, 'prev_speed'), (0x27, 1, 'max_speed'), (0x28, 1, 'turret'),
    (0x29, 1, 'turret_dir'), (0x2A, 1, 'aim_dir'), (0x2B, 1, 'push'),
    (0x2C, 2, 'overlay_attr'), (0x2E, 2, 'grid_cell'), (0x30, 2, 'vel_x'),
    (0x32, 2, 'vel_y'), (0x34, 2, 'target_vel_x'), (0x36, 2, 'target_vel_y'),
    (0x38, 1, '+38'), (0x39, 1, '+39'), (0x3A, 2, '+3A'), (0x3C, 1, 'misses'),
    (0x3D, 1, 'state'), (0x3E, 1, 'target_dir'), (0x3F, 1, 'path_hint'),
    (0x40, 2, 'dest_dist'), (0x42, 1, 'dest_dir'), (0x43, 1, 'far_dir'),
    (0x44, 1, 'hold'), (0x45, 1, 'saved_dir'), (0x46, 1, 'target/cursor'),
    (0x47, 1, 'shot_out'), (0x48, 4, 'target/customer ptr'),
    (0x4C, 4, 'far ptr'),
]

HOME_ORDERS = (2, 4, 5, 8)
STATES = {
    0: [0, 0, 1, 0xFF],
    1: [0],
    2: [0, 0, 1, 0xFF],
    3: [0, 0, 0xFF, 1, 2, 0x40, 0x80],
    4: [0, 8, 9, 0x88, 0x89, 0x98, 0xA8, 0xB8, 0x99, 0xA9, 0xB9],
    5: [0, 8, 9, 0x88, 0x89, 0x98, 0xA8, 0xB8, 0x99, 0xA9, 0xB9],
    6: [0, 0x80, 0x90, 0xA0, 0xB0],
    7: [0],
    8: [0, 0x88, 0x89, 9, 1, 2, 3, 4],
    9: [0, 0, 1, 2, 5, 8],
    0xA: [0, 0, 1, 2, 5, 8],
}


SLOPES = (-0x50, -0x17, -0xA, -3, 3, 0xA, 0x17, 0x50)    # DirSlopeTable


def get_direction(x0, y0, x1, y1):
    """GetDirection, for making states (e.g. a unit facing home)."""
    def s16(v):
        v &= 0xFFFF
        return v - 0x10000 if v & 0x8000 else v
    dx = s16(x1 - x0)
    dy = s16(y1 - y0)
    if dx == 0:
        return 8 if dy >= 0 else 0
    if dy == 0:
        return 4 if dx >= 0 else 0xC
    num, den = dy * 16, dx
    if dx < 0:
        num, den = -num, s16(-dx)
    q = abs(num) // abs(den)
    if (num < 0) != (den < 0):
        q = -q
    v = q if -0x8000 <= q <= 0x7FFF else s16(num)
    d = 0
    for t in SLOPES:
        if v <= t:
            break
        d += 1
    return (d + (8 if dx < 0 else 0)) & 0xF


def be(v, n):
    return (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'big')


class Gen:
    """Made-up states as RAM images ($10000 bytes from $FF0000)."""

    def __init__(self, rom, sym, rng):
        self.rom = rom
        self.sym = sym
        self.r = rng
        base = sym['UnitStats']
        self.stats = [rom[base + t * 32:base + t * 32 + 32] for t in range(16)]

    def stat(self, t, off, n=1):
        s = self.stats[t]
        return int.from_bytes(s[off:off + n], 'big')

    def w(self, ram, a, n, v):
        ram[a:a + n] = be(v, n)

    def unit_id(self, i):
        return (0xC0 if i >= 50 else 0x40) | ((i % 50) + 1)

    def addr_of(self, gid):
        if not gid & 0x40:
            return 0xFFFF0000 | (A_MECH1 + (0x100 if gid & 0x80 else 0))
        i = (50 if gid & 0x80 else 0) + ((gid - 1) & 0x3F)
        return 0xFFFF0000 | (A_UNITS + i * 0x50)

    COMBOS = ([(t, o) for t in (0, 1, 3, 9) for o in range(0xB)]
              + [(2, o) for o in (1, 2, 3, 5, 0xA)] + [(4, 0xF)] * 4
              + [(5, 1)] * 2 + [(8, o) for o in range(0xB)])

    def make(self):
        r = self.r
        ram = bytearray(0x10000)
        self.calls = r.randrange(1, 5)
        group = r.randrange(12)
        self.thinking = set()
        for k in range(self.calls):
            g = (group + k) % 12
            if g not in (5, 11):
                self.thinking.add(g - 1 if g > 5 else g)
        # a case may put one type and order in every thinking slot, with
        # the states that order has and fewer targets
        self.focus = r.choice(self.COMBOS) if r.random() < 0.6 else None
        self.sparse = r.random() < 0.2     # enemy units far off: mechs seen
        self.no_goal = r.random() < 0.1    # no outpost worth going for
        self.w(ram, A_SEED, 4, r.choice([0, r.getrandbits(32)]) if r.random() < 0.05 else r.getrandbits(32))
        self.w(ram, A_GAMETYPE, 2, 0xFFFF if r.random() < 0.15 else r.choice([0, 1, 2]))
        ram[A_OPTIONS] = r.randrange(8)
        ram[A_OPTIONS + 1] = r.randrange(4)
        self.w(ram, A_BOOST, 2, r.choice([0, 0, 0x8000, 0x8010]))
        ram[A_SFX_PRI] = r.choice([0xFF, 0xFF, r.randrange(256)])
        ram[A_SFX_Q] = r.randrange(256)
        self.w(ram, A_GROUP, 2, group)
        # the layout: random blocks, path hints on some
        for i in range(64 * 64):
            v = r.randrange(0x200)
            if r.random() < 0.35:
                v |= 0x200 | (r.randrange(64) << 10)
            self.w(ram, A_LAYOUT + 2 * i, 2, v)
        cx = r.randrange(250, 1800)
        cy = r.randrange(250, 1800)
        spread = r.choice([120, 200, 320, 500])

        def near(c):
            return max(0, min(2000, c + r.randrange(-spread, spread + 1)))

        # outposts and HQs
        for i in range(16):
            p = A_POSTS + i * 8
            if r.random() < 0.9:
                ram[p] = 0x80 | r.randrange(4)
                ram[p + 2] = r.choice([0, 0, r.randrange(5)])
                ram[p + 3] = r.choice([0, 0, r.randrange(5)])
                if self.no_goal:
                    ram[p] = 0
            far = r.random() < 0.5
            self.w(ram, p + 4, 2, r.randrange(2048) if far else near(cx))
            self.w(ram, p + 6, 2, r.randrange(2048) if far else near(cy))
        for k in range(2):
            p = A_POSTS + (16 + k) * 8
            ram[p] = 0xC2 + k
            self.w(ram, p + 2, 2, r.randrange(0x400))
            self.w(ram, p + 4, 2, near(cx) if r.random() < 0.5 else r.randrange(2048))
            self.w(ram, p + 6, 2, near(cy) if r.random() < 0.5 else r.randrange(2048))
        # mechs
        mech_xy = []
        for m in range(2):
            p = A_MECH1 + m * 0x100
            flags = 0x80 | (0x10 if m else 0) | (1 if r.random() < 0.5 else 0)
            if r.random() < 0.1:
                flags |= 0x20
            ram[p] = flags
            ram[p + 0x0C] = 0x20
            x, y = near(cx), near(cy)
            self.w(ram, p + 0x10, 2, x)
            self.w(ram, p + 0x12, 2, y)
            mech_xy.append((x, y))
        # units
        present = []
        density = r.choice([0.3, 0.6, 0.9])
        for i in range(100):
            if (i % 50) % 10 in self.thinking and r.random() < 0.9:
                present.append(i)
            elif r.random() < density:
                present.append(i)
        units = {}
        sparse_team = r.randrange(2)
        for i in present:
            far_off = self.sparse and (i >= 50) == bool(sparse_team)
            units[i] = self.make_unit(ram, i, (lambda c: r.randrange(2000)) if far_off else near, cx, cy)
        # targets, far enemies, customers: ids of what's about
        ids = [self.unit_id(i) for i in present] + [0x01, 0x81]
        for i in present:
            self.make_ai(ram, i, units, ids, present)
        # the grid: every active, not carried unit at its centre, the mechs
        for i in present:
            a = A_UNITS + i * 0x50
            if ram[a] & 0x80 and not ram[a] & 0x08:
                half = ram[a + 0x0C] >> 1
                x = int.from_bytes(ram[a + 0x10:a + 0x12], 'big')
                y = int.from_bytes(ram[a + 0x12:a + 0x14], 'big')
                cell = ((((y + half) & 0xFFE0) * 2) + ((x + half) >> 5)) & 0xFFF
                ram[A_GRID + cell] = self.unit_id(i)
        for m, (x, y) in enumerate(mech_xy):
            cell = ((((y + 0x10) & 0xFFE0) * 2) + ((x + 0x10) >> 5)) & 0xFFF
            if r.random() < 0.9:
                ram[A_GRID + cell] = 0x81 if m else 0x01
        for _ in range(r.randrange(4)):
            ram[A_GRID + r.randrange(4096)] = r.choice(ids)
        # shots: some slots taken, sometimes all of a team's
        for team in range(2):
            for kind, count, first in (('b', 24, 0), ('m', 8, 24)):
                full = r.random() < 0.15
                for k in range(count):
                    if full or r.random() < 0.2:
                        s = A_SHOTS + (team * 32 + first + k) * 0x20
                        ram[s] = 0x80 | (team << 4)
                        ram[s + 1] = r.randrange(16)
                        owner = r.choice(present) if present else 0
                        self.w(ram, s + 0x1C, 4, 0xFFFF0000 | (A_UNITS + owner * 0x50))
        return self.calls, ram

    def make_unit(self, ram, i, near, cx, cy):
        r = self.r
        a = A_UNITS + i * 0x50
        team = 1 if i >= 50 else 0
        t = r.choice([0, 1, 2, 3, 4, 4, 5, 8, 8, 9])
        focused = self.focus and (i % 50) % 10 in self.thinking and r.random() < 0.85
        if focused:
            t, order = self.focus
        elif t == 4:
            order = 0xF
        elif t == 5:
            order = r.choice([1, 1, 2, 5])
        elif t == 8:
            order = r.choice([1, 2, 3, 5, 9, 9, 0xA, 0, 4, 6, 7, 8])
        else:
            order = r.choice([0, 1, 2, 3, 4, 5, 5, 6, 7, 8, 0xA, 0xA, 3])
        self.focused = getattr(self, 'focused_set', set())
        if focused:
            self.focused.add(i)
        self.focused_set = self.focused
        flags = 0x80 | (team << 4)
        if t == 3:
            flags |= 0x01
        if r.random() < 0.4:
            flags |= 0x40
        if r.random() < 0.1:
            flags |= 0x02
        roll = r.random()
        if roll < 0.04:
            flags |= 0x20
        elif roll < 0.08:
            flags |= 0x08
        elif roll < 0.11:
            flags |= 0x04
        ram[a] = flags
        ram[a + 1] = t
        ram[a + 2] = order
        ram[a + 3] = r.randrange(16)
        ram[a + 4] = ram[a + 3] if r.random() < 0.4 else r.randrange(16)
        ram[a + 5] = r.randrange(256)
        ram[a + 6] = r.randrange(256)
        ram[a + 7] = r.choice([0, 0, 1, 2, 3])
        ram[a + 8] = self.stat(t, 0x17)
        ram[a + 9] = r.choice([0, 1, r.randrange(0x64), 0x64, 0x65, 0x66, 0x66, 50, 99])
        self.w(ram, a + 0x0A, 2, r.randrange(1, 0x100))
        ram[a + 0x0C] = self.stat(t, 0x0C)
        ram[a + 0x0D] = 1 if r.random() < 0.05 else 0
        ram[a + 0x0E] = r.choice([r.randrange(100), r.randrange(0x66), 0x64, 0x65])
        ram[a + 0x0F] = self.stat(t, 0x13)
        self.w(ram, a + 0x10, 2, near(cx))
        self.w(ram, a + 0x12, 2, near(cy))
        maxm = self.stat(t, 0x1A)
        maxa = self.stat(t, 0x0D)
        maxf = self.stat(t, 0x0E, 2)
        ram[a + 0x18] = r.choice([0, maxm, maxm // 5, r.randrange(maxm + 1)]) if maxm else 0
        ram[a + 0x19] = r.choice([0, maxa, maxa // 5, r.randrange(maxa + 1)])
        self.w(ram, a + 0x1A, 2, r.choice([0, maxf, maxf // 5, r.randrange(maxf + 1)]))
        ram[a + 0x1C] = r.randrange(256)
        ram[a + 0x1D] = r.choice([6, r.randrange(16)])
        ram[a + 0x22] = r.choice([0, 0, 0x80 | r.randrange(16), 0x40, 0x20, 0x60, 0xC0 | r.randrange(16)])
        ram[a + 0x23] = r.choice([0, 0, 1, 2, 3, 9, 4, 0x0B])
        moving = r.random() < 0.5
        speed = self.stat(t, 0x11)
        ram[a + 0x27] = speed if r.random() < 0.9 else 0
        self.w(ram, a + 0x24, 2, ram[a + 0x27] if moving else 0)
        ram[a + 0x26] = r.randrange(256)
        ram[a + 0x28] = self.stat(t, 0x12)
        ram[a + 0x29] = r.choice([0, 0, r.randrange(16)])
        ram[a + 0x2A] = r.randrange(16)
        return t

    def make_ai(self, ram, i, types, ids, present):
        r = self.r
        a = A_UNITS + i * 0x50
        t = types[i]
        order = ram[a + 2]
        team_bit = 0x80 if i >= 50 else 0
        if t == 4:
            mates = [k for k in present if (k >= 50) == (i >= 50)]
            roll = r.random()
            cust = r.choice(mates) if mates else i
            if roll < 0.3 and cust != i:
                c = A_UNITS + cust * 0x50
                ram[a + 0x38] = 0xFF
                ram[a + 0x39] = r.randrange(0x20)
                ram[c] |= 0x04
                self.w(ram, c + 0x24, 2, 0)
                self.w(ram, a + 0x48, 4, self.addr_of(self.unit_id(cust)))
            elif roll < 0.6:
                self.w(ram, a + 0x48, 4, self.addr_of(self.unit_id(cust)))
                ram[a + 0x44] = r.choice([0, 0xFF, 0xFF])
                if r.random() < 0.5 and mates:
                    b = r.choice(mates)         # stopped against a mate
                    ram[a + 0x0E] = b
                    ram[a + 0x23] = 1
                    self.w(ram, a + 0x24, 2, 0)
            elif roll < 0.75:
                ram[a + 0x3A] = r.randrange(1, 5)
                ram[a + 0x3B] = r.choice([1, 0xFF])
            ram[a + 0x3D] = r.choice([0, 0, 0, 9, 8, 1, 3])
            ram[a + 0x46] = r.randrange(50)
            return
        if order in HOME_ORDERS and r.random() < 0.8:
            x = int.from_bytes(ram[a + 0x10:a + 0x12], 'big')
            y = int.from_bytes(ram[a + 0x12:a + 0x14], 'big')
            kind = r.random()
            if kind < 0.25:
                hx, hy = x & 0xFFF0 | r.randrange(16), y & 0xFFF0 | r.randrange(16)
            elif kind < 0.6:
                hx, hy = x + r.randrange(-0x50, 0x51), y + r.randrange(-0x50, 0x51)
            else:
                hx, hy = r.randrange(2048), r.randrange(2048)
            hx, hy = max(0, hx), max(0, hy)
            self.w(ram, a + 0x38, 2, hx | 0x8000)
            self.w(ram, a + 0x3A, 2, hy)
        elif order == 6:
            ram[a + 0x38] = r.choice([0, 0xFF, 0xFF])
        elif order == 7:
            ram[a + 0x38] = r.choice([0, r.randrange(1, 17)])
        elif order == 0xA:
            ram[a + 0x38] = r.choice([0, 0, 0, r.randrange(1, 13), 0x80, 0xFF, 0xFF])
        focused = i in getattr(self, 'focused_set', set())
        state = r.choice(STATES.get(order & 0xF, [0]))
        if order in (4, 5, 8) and not (ram[a + 0x38] & 0x80):
            state = 0               # a first run starts from 0
        ram[a + 0x3D] = state
        ram[a + 0x3C] = r.choice([0, 0, 1, 2, 3])
        enemies = [g for g in ids if (g & 0x80) != team_bit]
        roll = r.random()
        if focused and order in HOME_ORDERS and ram[a + 0x38] & 0x80 and r.random() < 0.5:
            hx = int.from_bytes(ram[a + 0x38:a + 0x3A], 'big') & 0x7FFF
            hy = int.from_bytes(ram[a + 0x3A:a + 0x3C], 'big')
            x = int.from_bytes(ram[a + 0x10:a + 0x12], 'big')
            y = int.from_bytes(ram[a + 0x12:a + 0x14], 'big')
            ram[a + 3] = get_direction(x, y, hx, hy)   # facing home
        if focused:
            roll = 0.45 + 0.55 * roll * roll          # fewer targets
        if roll < 0.45:
            tgt = 0
        elif roll < 0.9 and enemies:
            tgt = r.choice(enemies)
        else:
            tgt = r.choice(ids)
        ram[a + 0x46] = tgt
        if tgt:
            self.w(ram, a + 0x48, 4, self.addr_of(tgt))
            ram[a + 0x3E] = r.choice([0, r.randrange(1, 17), r.randrange(1, 17)])
        ram[a + 0x47] = r.choice([0, 0, 0xFF])
        ram[a + 0x3F] = r.choice([0, 0, 1 + 2 * r.randrange(8)])
        self.w(ram, a + 0x40, 2, r.choice([r.randrange(0x200), 0x3F, 0x40, 0x5F, 0x60, 0x9F, 0xA0, r.randrange(0x40, 0x60)]))
        ram[a + 0x42] = r.choice([0, r.randrange(1, 17), r.randrange(1, 17)])
        if t in (2, 5):
            if r.random() < 0.6:
                mech = 0x01 if team_bit else 0x81
                ram[a + 0x43] = r.randrange(1, 17)
                self.w(ram, a + 0x4C, 4, self.addr_of(mech))
        elif r.random() < (0.3 if focused and order not in (4, 5) else 0.5) and enemies:
            far = r.choice(enemies)
            ram[a + 0x43] = r.randrange(1, 17)
            self.w(ram, a + 0x4C, 4, self.addr_of(far))
        elif r.random() < 0.3 and enemies:
            self.w(ram, a + 0x4C, 4, self.addr_of(r.choice(enemies)))
        ram[a + 0x44] = r.choice([0, 0, 0xFF, 1, 0xFF])
        roll = r.random()
        if focused and order == 8 and roll < 0.2:
            # backing off: unarmed, an enemy far off, still moving
            ram[a + 0x3D] = 0x88
            ram[a + 0x46] = 0
            ram[a + 0x18] = ram[a + 0x19] = 0
            ram[a + 0x43] = r.randrange(1, 17)
            self.w(ram, a + 0x24, 2, ram[a + 0x27])
        elif focused and order == 8 and roll < 0.4 and ram[a + 0x38] & 0x80:
            # facing home, stopped against something
            hx = int.from_bytes(ram[a + 0x38:a + 0x3A], 'big') & 0x7FFF
            hy = int.from_bytes(ram[a + 0x3A:a + 0x3C], 'big')
            x = int.from_bytes(ram[a + 0x10:a + 0x12], 'big')
            y = int.from_bytes(ram[a + 0x12:a + 0x14], 'big')
            ram[a + 3] = get_direction(x, y, hx, hy)
            ram[a + 0x3D] = 0x89
            ram[a + 0x46] = 0
            ram[a + 0x23] = r.choice([1, 2, 3])
            self.w(ram, a + 0x24, 2, 0)
        elif focused and order in (4, 5, 8) and r.random() < 0.4:
            # going home, nearly there: no target, no far enemy, moving
            x = int.from_bytes(ram[a + 0x10:a + 0x12], 'big')
            y = int.from_bytes(ram[a + 0x12:a + 0x14], 'big')
            self.w(ram, a + 0x38, 2, max(0, x + r.randrange(-31, 32)) | 0x8000)
            self.w(ram, a + 0x3A, 2, max(0, y + r.randrange(-31, 32)))
            ram[a + 0x3D] = 0x89
            ram[a + 0x46] = 0
            ram[a + 0x43] = 0
            self.w(ram, a + 0x24, 2, ram[a + 0x27])
        if order == 6:
            ram[a + 0x45] = r.choice([0, 1, 0xFF])
        else:
            ram[a + 0x45] = r.choice([0, 0, r.randrange(1, 17)])


class Original:
    """The original routines on the interpreter, with the port's changes
    as patches."""

    def __init__(self, rom, sym):
        self.rom = rom
        self.sym = sym
        self.patches = {}
        self.patches[sym['Unit_SetSprite']] = self.p_setsprite
        self.patches[self.find('UnitAI_OrderPatrol.dodge', 'subq.l')] = self.p_patrol
        self.patches[sym['UnitAI_AimAtMech.store']] = self.p_aim_mech
        self.patches[self.find('UnitAI_OrderSupply.approach', 'cmpa.l')] = self.p_supply
        self.hits = dict((k, 0) for k in self.patches)

    def find(self, label, mn):
        a = self.sym[label]
        for _ in range(40):
            insn = m68k.decode(self.rom, a)
            if insn.mn == mn:
                return a
            a += insn.size
        raise SystemExit('%s not found after %s' % (mn, label))

    def p_setsprite(self, cpu):
        cpu.d[0] &= 0xFF

    def p_patrol(self, cpu):
        cpu.d[1] = 2

    def p_aim_mech(self, cpu):
        cpu.write(cpu.a[0] + 0x4C, 4, cpu.a[2])

    def p_supply(self, cpu):
        cpu.a[3] |= 0xFF000000

    def run(self, ram, calls, seen=None):
        cpu = M68k(self.rom)
        cpu.ram[:] = ram
        patches = self.patches
        hits = self.hits

        def trace(c, insn):
            if seen is not None:
                seen.add(insn.addr)
            p = patches.get(insn.addr)
            if p:
                hits[insn.addr] += 1
                p(c)
        cpu.trace = trace
        for _ in range(calls):
            cpu.d[7] = 0
            cpu.call(self.sym['UnitAI_Update'])
        return bytes(cpu.ram), cpu.count


def compare(before, ours, theirs):
    diffs = []
    for i in range(100):
        a = A_UNITS + i * 0x50
        rec_o = ours[a:a + 0x50]
        rec_t = theirs[a:a + 0x50]
        if rec_o == rec_t:
            continue
        stale_ptr = theirs[a + 1] != 4 and theirs[a + 0x46] == 0
        for off, n, name in UNIT_FIELDS:
            if off == 0x48 and stale_ptr:
                continue
            if rec_o[off:off + n] != rec_t[off:off + n]:
                diffs.append('unit %d (type %d order %X) %s: port %s, original %s, before %s'
                             % (i, theirs[a + 1], theirs[a + 2], name,
                                rec_o[off:off + n].hex(), rec_t[off:off + n].hex(),
                                before[a + off:a + off + n].hex()))
    for k in range(64):
        a = A_SHOTS + k * 0x20
        if ours[a:a + 0x20] != theirs[a:a + 0x20]:
            diffs.append('shot %d: port %s, original %s' % (k, ours[a:a + 0x20].hex(), theirs[a:a + 0x20].hex()))
    for a, n, name in ((A_SEED, 4, 'v_rng_seed'), (A_SFX_PRI, 1, 'v_sfx_priority'),
                       (A_SFX_Q, 1, 'v_sfx_queued'), (A_GROUP, 2, 'v_unit_ai_group'),
                       (A_POSTS, 160, 'posts'), (A_GRID, 4096, 'v_grid')):
        if ours[a:a + n] != theirs[a:a + n]:
            diffs.append('%s: port %s, original %s' % (name, ours[a:a + n][:16].hex(), theirs[a:a + n][:16].hex()))
    return diffs


def report_coverage(rom, sym, seen):
    """The module's instructions ($5DF20-$5FC52) never run, by label."""
    names = sorted((v, k) for k, v in sym.items() if 0x5DF20 <= v < 0x5FC54)
    missed = []
    a = 0x5DF20
    data = {sym['UnitAIHandlers']: 0x40, sym['UnitAIOrderTable']: 0x40,
            sym['UnitTwinGunOffsets']: 0x40}
    while a < 0x5FC54:
        if a in data:
            a += data[a]
            continue
        try:
            insn = m68k.decode(rom, a)
        except m68k.DecodeError:
            a += 2
            continue
        if a not in seen:
            label = max((v, k) for v, k in names if v <= a)
            missed.append('%s+%X' % (label[1], a - label[0]))
        a += insn.size
    print('not run (%d): %s' % (len(missed), ' '.join(missed)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('sym')
    ap.add_argument('harness')
    ap.add_argument('assets')
    ap.add_argument('--cases', type=int, default=300)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--keep')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--game', nargs=5, metavar=('MAP', 'MODE', 'DIFF', 'FRAMES', 'VISITS'),
                    help='check a real game (hz_unitai --game) instead of made-up states')
    ap.add_argument('--coverage', action='store_true',
                    help="list the module's instructions no case ran")
    args = ap.parse_args()
    rom = open(args.rom, 'rb').read()
    sym = load_symbols(args.sym)
    tmp = args.keep or tempfile.mkdtemp(prefix='hzunitai')
    os.makedirs(tmp, exist_ok=True)
    fin = os.path.join(tmp, 'in.bin')
    fout = os.path.join(tmp, 'out.bin')
    cases = []
    if args.game:
        subprocess.run([args.harness, args.assets, '--game'] + args.game + [fin, fout],
                       check=True)
        data = open(fin, 'rb').read()
        for k in range(len(data) // 0x10001):
            c = data[k * 0x10001:(k + 1) * 0x10001]
            cases.append((c[0], c[1:]))
    else:
        gen = Gen(rom, sym, random.Random(args.seed))
        for _ in range(args.cases):
            gen.focused_set = set()
            cases.append(gen.make())
        with open(fin, 'wb') as f:
            for calls, ram in cases:
                f.write(bytes([calls]))
                f.write(ram)
        subprocess.run([args.harness, args.assets, fin, fout], check=True)
    out = open(fout, 'rb').read()
    orig = Original(rom, sym)
    bad = 0
    insns = 0
    seen = set() if args.coverage else None
    for n, (calls, ram) in enumerate(cases):
        ours = out[n * 0x10000:(n + 1) * 0x10000]
        try:
            theirs, count = orig.run(ram, calls, seen)
        except M68kError as e:
            print('case %d: the original stopped: %s' % (n, e))
            bad += 1
            continue
        insns += count
        diffs = compare(ram, ours, theirs)
        if diffs:
            bad += 1
            if bad <= 5 or not args.quiet:
                print('case %d (%d calls, group %d): %d differences' % (n, calls, ram[A_GROUP + 1], len(diffs)))
                for d in diffs[:12]:
                    print('   ', d)
    if seen is not None:
        report_coverage(rom, sym, seen)
    hits = ', '.join('$%X %d' % (k, v) for k, v in orig.hits.items())
    what = ('game map %s mode %s difficulty %s' % tuple(args.game[:3])) if args.game else 'seed %d' % args.seed
    print('%d cases (%s), %d differ; %d instructions of the original; patches hit: %s'
          % (len(cases), what, bad, insns, hits))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
