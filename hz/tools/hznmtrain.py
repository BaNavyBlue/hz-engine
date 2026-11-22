#!/usr/bin/env python3
"""Self-play training of the nightmare AI's tuning values (session 57).

    python3 tools/hznmtrain.py HZ_ARENA ASSETS OUTDIR [--iters N]
        [--games N] [--seed S] [--start CHAMPION.json]

A hill climb. The champion starts as the defaults (hz_arena --params) or
a champion.json from an earlier run. Each step changes one to three of
its values (by a few steps of SPACE's, within its range) and plays the
challenger against the champion, both as the CPU (hz_arena's nm against
nm), on every map from both sides, --games each with the same seeds for
both. A win counts 1, a draw a half and the HQs' HP difference over
4096. A challenger that takes more than 56% of the points is then played
against the original's AI (on every map, both sides); it becomes the
champion if it does no worse there than the champion did (within half a
point in a hundred). OUTDIR/train_log.txt has a line a step; the champion
is kept in OUTDIR/champion.json, whose values go into nightmare.c's
NM_PARAMS by hand.

Run it on a copy of hz_arena: a rebuild under it changes the game it
trains on halfway.
"""
import argparse
import concurrent.futures as cf
import json
import os
import random
import subprocess
import sys
import time

# The values it tries: name: (lowest, highest, step). The others
# (rush_type, siege_clear, dog_any, mech_fight) stay as they are.
SPACE = {
    'drop_range': (100, 320, 16),
    'drop_gd': (-1, 10, 1),
    'fuel_margin': (60, 300, 20),
    'def_radius': (300, 1000, 40),
    'hunt_radius': (0, 640, 40),
    'hq_alarm': (500, 8000, 250),
    'fight_range': (40, 112, 8),
    'land_reach': (64, 140, 8),
    'dog_range': (0, 1200, 60),
    'air_guard': (0, 400, 40),
    'evade': (0, 240, 20),
    'capture_min': (0, 6, 1),
    'capture_max': (3, 9, 1),
    'capture_reach': (400, 1200, 40),
    'capture_near': (160, 640, 40),
    'missile_cost': (10, 200, 10),
    'siege': (0, 1, 1),
    'siege_r': (120, 400, 20),
    'siege_hp': (96, 256, 16),
    'siege_leave': (48, 176, 16),
    'siege_hold': (0, 3000, 150),
    'svc_fuel': (50, 100, 5),
    'svc_hp': (40, 100, 5),
    'svc_ammo': (8, 96, 8),
    'jitter': (0, 200, 8),
    'mech_hq': (0, 40, 4),
    'mech_escort': (0, 30, 3),
}


def defaults(exe, assets):
    r = subprocess.run([exe, assets, '--params'], capture_output=True,
                       text=True, check=True)
    out = {}
    for line in r.stdout.splitlines():
        name, rest = line.split('=', 1)
        out[name] = int(rest.split()[0])
    return out


def arena(exe, assets, m, p1, p2, games, seed, params1, params2):
    cmd = [exe, assets, '--map', str(m), '--difficulty', '0', '--p1', p1,
           '--p2', p2, '--games', str(games), '--seed', str(seed),
           '--frames', '108000']
    for k, v in params1.items():
        cmd += ['--param1', '%s=%d' % (k, v)]
    for k, v in params2.items():
        cmd += ['--param2', '%s=%d' % (k, v)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = []
    for line in r.stdout.splitlines():
        f = line.split()
        if f and f[0] == 'R':
            out.append((int(f[6]), int(f[8]), int(f[9])))
    return out


def points(rows, side):
    """The side's (1 or 2) points: a win 1, a draw half and the HP lead."""
    pts = 0.0
    for winner, hq1, hq2 in rows:
        if winner == side:
            pts += 1
        elif winner == 0:
            own, other = (hq1, hq2) if side == 1 else (hq2, hq1)
            pts += 0.5 + (own - other) / 4096.0
    return pts


def both_sides(pool, exe, assets, a, b, a_kind, b_kind, games, seed):
    """a's share of the points, a playing each map as P1 and as P2."""
    jobs = []
    for m in range(8):
        jobs.append(pool.submit(arena, exe, assets, m, a_kind, b_kind, games,
                                seed + m * 1000, a, b))
        jobs.append(pool.submit(arena, exe, assets, m, b_kind, a_kind, games,
                                seed + m * 1000, b, a))
    pts = n = 0
    for i, j in enumerate(jobs):
        rows = j.result()
        pts += points(rows, 1 if i % 2 == 0 else 2)
        n += len(rows)
    return pts / max(1, n)


def mutate(champ, rng):
    chal = dict(champ)
    for name in rng.sample(sorted(SPACE), rng.choice([1, 1, 2, 3])):
        lo, hi, step = SPACE[name]
        delta = step * rng.choice([-3, -2, -1, 1, 2, 3])
        chal[name] = max(lo, min(hi, chal[name] + delta))
    return chal


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('assets')
    ap.add_argument('outdir')
    ap.add_argument('--iters', type=int, default=150)
    ap.add_argument('--games', type=int, default=12)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--start', default='')
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rng = random.Random(a.seed)
    champ = defaults(a.exe, a.assets)
    if a.start:
        champ.update(json.load(open(a.start)))
    log = open(os.path.join(a.outdir, 'train_log.txt'), 'a')
    with cf.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        champ_cpu = both_sides(pool, a.exe, a.assets, champ, {}, 'nm', 'cpu',
                               a.games, 7777)
        log.write('start %s against the original %.3f\n'
                  % (json.dumps(champ, sort_keys=True), champ_cpu))
        log.flush()
        for it in range(a.iters):
            t0 = time.time()
            chal = mutate(champ, rng)
            changed = {k: chal[k] for k in chal if chal[k] != champ[k]}
            self_share = both_sides(pool, a.exe, a.assets, chal, champ, 'nm',
                                    'nm', a.games, 100000 + it * 37)
            cpu = None
            verdict = 'reject'
            if self_share > 0.56:
                cpu = both_sides(pool, a.exe, a.assets, chal, {}, 'nm', 'cpu',
                                 a.games, 7777)
                if cpu >= champ_cpu - 0.005:
                    verdict = 'ACCEPT'
                    champ, champ_cpu = chal, cpu
                    with open(os.path.join(a.outdir, 'champion.json'),
                              'w') as f:
                        json.dump(champ, f, indent=1, sort_keys=True)
            log.write('%4d %6.1fs self %.3f original %s %s %s\n' % (
                it, time.time() - t0, self_share,
                '%.3f' % cpu if cpu is not None else '-', verdict,
                json.dumps(changed, sort_keys=True)))
            log.flush()
        log.write('final %s against the original %.3f\n'
                  % (json.dumps(champ, sort_keys=True), champ_cpu))
    return 0


if __name__ == '__main__':
    sys.exit(main())
