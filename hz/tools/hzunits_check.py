#!/usr/bin/env python3
"""Check hz_shot --units (and --spawn-probes) against a direct model.

    python3 tools/hzunits_check.py hz.md FILE...

FILE is what `hz_shot ... --units FILE [--spawn-probes]` writes: the unit
records, counts and v_grid as Game_Start leaves them, v_rng_seed and
v_cpu_boost_hq_hp, and, with --spawn-probes, what Unit_Spawn made of each
of a list of new units (each from the same start state). The model is
hzview_check's spawn_units / spawn / spawn_test / new_unit, worked out
from the ROM and the 68k routines, not from the port:

  - R: Random from v_rng_seed_saved (0: a cold boot; 0 counts as
    $2A6B365A): s x 41 = H:L, new seed (H + L):L, result s's high word :
    (H + L); v_cpu_boost_hq_hp = $199 + (result >> 15 & $1FF);
  - C: v_unit_count, v_units_built and v_unit_type_count per team;
  - G: every occupied v_grid cell and its id;
  - U: every slot's record, all the fields Unit_Spawn writes;
  - P: per probe the result, v_hit_flags, v_hit_id, and when placed the
    slot and its record.

The probes are hz_shot's: round each start unit (slot order) at offsets
dx, dy from OFFSETS, then a sweep of the map at SWEEP x SWEEP, each place
as types 0, 3 (ST-57U: needs water tiles) and 8 (INFANTRY: 2 x 2 tiles).
Probe n: dir and order n & $F, team n & 1. Exit status 1 on any mismatch.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hzview_check as hv  # noqa: E402

OFFSETS = [-25, -24, -23, -17, -16, -15, 0, 15, 16, 17, 23, 24, 25]
TYPES = [0, 3, 8]
SWEEP = [-16, -4] + list(range(0, 2048, 40)) + [2044]

FIELDS = ['flags', 'type', 'order', 'dir', 'turn_to', 'tick', 'power', 'hp',
          'size', 'fuel_use', 'x', 'y', 'missiles', 'ammo', 'fuel', 'attr',
          'sprite_size', 'speed', 'prev_speed', 'max_speed', 'turret',
          'turret_dir', 'aim_dir', 'overlay', 'grid_cell', 'tvx', 'tvy']


def record(u):
    return [u[f] & 0xFFFF for f in FIELDS]


def random(seed):
    s = seed or 0x2A6B365A
    m = (s * 41) & 0xFFFFFFFF
    lo = m & 0xFFFF
    total = (lo + (m >> 16)) & 0xFFFF
    return (total << 16) | lo, (s & 0xFFFF0000) | total


def probes(start_units):
    n = 0
    for o in start_units:
        if o is None:
            continue
        for dy in OFFSETS:
            for dx in OFFSETS:
                for t in TYPES:
                    yield n, t, o['x'] + dx, o['y'] + dy
                    n += 1
    for y in SWEEP:
        for x in SWEEP:
            for t in TYPES:
                yield n, t, x, y
                n += 1


def check(rom, path, maps):
    lines = [ln.split() for ln in open(path)]
    head = lines[0]
    if head[0] != 'HZU1':
        raise SystemExit('%s: not an hz_shot --units file' % path)
    n, mode, difficulty = (int(v, 16) for v in head[1:4])
    if n not in maps:
        maps[n] = hv.Map(rom, n)
        maps[n].n = n
    m = maps[n]
    units, grid = hv.spawn_units(rom, m, mode, difficulty)
    bad = []

    def expect(what, got, want):
        if got != want:
            bad.append('%s: got %s want %s' % (
                what, ' '.join('%X' % v for v in got),
                ' '.join('%X' % v for v in want)))

    got = {'R': [], 'C': [], 'G': {}, 'U': {}, 'P': []}
    for ln in lines[1:]:
        vals = [int(v, 16) for v in ln[1:]]
        if ln[0] in ('R', 'C'):
            got[ln[0]] = vals
        elif ln[0] == 'G':
            got['G'][vals[0]] = vals[1]
        elif ln[0] == 'U':
            got['U'][vals[0]] = vals[1:]
        elif ln[0] == 'P':
            got['P'].append(vals)

    seed, r = random(0)
    expect('R', got['R'], [seed, 0x199 + (r >> 15 & 0x1FF)])
    count = [sum(u is not None for u in units[t * 50:t * 50 + 50]) for t in (0, 1)]
    types = [[sum(1 for u in units[t * 50:t * 50 + 50] if u and u['type'] == k)
              for k in range(16)] for t in (0, 1)]
    expect('C', got['C'], count + count + types[0] + types[1])
    want_grid = {c: v for c, v in enumerate(grid) if v}
    for c in sorted(set(want_grid) | set(got['G'])):
        expect('G cell %X' % c, [got['G'].get(c, 0)], [want_grid.get(c, 0)])
    for i in range(100):
        want = record(units[i]) if units[i] else None
        have = got['U'].get(i)
        if want != have:
            expect('U slot %X' % i, have or [], want or [])

    total = placed = 0
    for (k, t, x, y), p in zip(probes(units), got['P']):
        total += 1
        team = k & 1
        flags = 0x80 | team << 4
        d = k & 15
        head = [k, flags, t, d, x & 0xFFFF, y & 0xFFFF]
        u2, g2 = list(units), bytearray(grid)
        res, hit, hit_id, slot = hv.spawn(rom, m, g2, u2, flags, t, d, d, x, y)
        want = head + [res & 0xFF, hit, hit_id]
        if slot is not None:
            placed += 1
            want += [slot] + record(u2[slot])
        expect('P %X' % k, p, want)
        if len(bad) > 20:
            break
    if len(got['P']) not in (0, total):
        bad.append('%d probes, the model has %d' % (len(got['P']), total))
    return bad, total, placed


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    rom = open(sys.argv[1], 'rb').read()
    maps = {}
    failed = False
    for path in sys.argv[2:]:
        bad, total, placed = check(rom, path, maps)
        print('%-24s %s: %d probes (%d placed)%s' % (
            os.path.basename(path), 'MISMATCH' if bad else 'ok', total, placed,
            '; %d differ' % len(bad) if bad else ''))
        for b in bad[:10]:
            print('    ' + b)
        failed |= bool(bad)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
