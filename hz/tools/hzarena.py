#!/usr/bin/env python3
"""Tournaments through hz_arena (session 57): whole games with no window,
every map and difficulty asked for, games side by side on every core (or
--jobs J at a time); a table of the results, and what the nightmare AI did.

    python3 tools/hzarena.py HZ_ARENA ASSETS P1 P2 [--games N] [--seed S]
        [--maps 0-7] [--diffs 0-3] [--frames F] [--param NAME=V]
        [--param1 NAME=V] [--param2 NAME=V] [--fixes HEX] [--jobs J]
        [--quiet] [--check-side 1|2 --check-wins PCT --check-losses PCT]
                                                       (make nightmare)

P1 and P2 are hz_arena's sides: cpu (the original's AI), nm (the
nightmare AI as the CPU), bot (the nightmare AI on a human's pad, with a
human's handicaps), turtle (session 67: the bot playing as the owner
beat the nightmare AI, a wall of standing units round its HQ and then
attack units by the enemy's) or idle. --games is per map and difficulty, seeds S,
S+1 ... (the same seeds give the same games). The table: each map and
difficulty's wins for each side, draws (no HQ down in --frames, an hour
by default), the frames a game took and the HQs' HP at the end; then the
totals, and a nightmare side's averages (hz_arena's N lines: frames in
each task, deaths, kills, units sent). --fixes (session 71) is hz_arena's:
the patches (game.h's HZ_FIX_*, hex), hz_arena's own default without it.

--check-wins and --check-losses make it a check: the --check-side's wins
(P1's by default) at least that % of the games, its losses at most that
%; it exits 1 otherwise. make nightmare runs it with the nightmare AI on
each side against the original's.
"""
import argparse
import concurrent.futures as cf
import os
import subprocess
import sys


def spread(text):
    out = []
    for part in text.split(','):
        if '-' in part:
            a, b = part.split('-')
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def play(job):
    exe, assets, m, d, p1, p2, games, seed, frames, params = job
    cmd = [exe, assets, '--map', str(m), '--difficulty', str(d),
           '--p1', p1, '--p2', p2, '--games', str(games), '--seed',
           str(seed), '--frames', str(frames)]
    for flag, value in params:
        cmd += [flag, value]
    r = subprocess.run(cmd, capture_output=True, text=True)
    rows = []
    for line in r.stdout.splitlines():
        f = line.split()
        if f and f[0] == 'R':
            rows.append({'winner': int(f[6]), 'frames': int(f[7]),
                         'hq1': int(f[8]), 'hq2': int(f[9]), 'n': {}})
        elif f and f[0] == 'N' and rows:
            kv = f[2:]
            rows[-1]['n'][int(f[1])] = {kv[i]: int(kv[i + 1])
                                        for i in range(0, len(kv) - 1, 2)}
    if r.returncode != 0:
        sys.stderr.write('hzarena: %s failed: %s\n' % (' '.join(cmd),
                                                       r.stderr.strip()))
    return m, d, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('assets')
    ap.add_argument('p1')
    ap.add_argument('p2')
    ap.add_argument('--games', type=int, default=8)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--maps', default='0-7')
    ap.add_argument('--diffs', default='0-3')
    ap.add_argument('--frames', type=int, default=216000)
    ap.add_argument('--param', action='append', default=[])
    ap.add_argument('--param1', action='append', default=[])
    ap.add_argument('--param2', action='append', default=[])
    ap.add_argument('--fixes')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--check-side', type=int, default=1, choices=(1, 2))
    ap.add_argument('--check-wins', type=float)
    ap.add_argument('--check-losses', type=float)
    a = ap.parse_args()
    params = ([('--param', p) for p in a.param]
              + [('--param1', p) for p in a.param1]
              + [('--param2', p) for p in a.param2])
    if a.fixes is not None:
        params.append(('--fixes', a.fixes))
    jobs = [(a.exe, a.assets, m, d, a.p1, a.p2, a.games, a.seed, a.frames,
             params) for d in spread(a.diffs) for m in spread(a.maps)]
    results = {}
    with cf.ThreadPoolExecutor(max(1, a.jobs)) as pool:
        for m, d, rows in pool.map(play, jobs):
            results[(m, d)] = rows

    print('map diff  P1 win  P2 win  draw    frames   hq1   hq2'
          '   (P1 %s, P2 %s)' % (a.p1, a.p2))
    total = {0: 0, 1: 0, 2: 0}
    by_diff = {}
    for (m, d), rows in sorted(results.items(), key=lambda kv: kv[0][::-1]):
        w = {0: 0, 1: 0, 2: 0}
        for r in rows:
            w[r['winner']] += 1
        for k in w:
            total[k] += w[k]
            by_diff.setdefault(d, {0: 0, 1: 0, 2: 0})[k] += w[k]
        n = max(1, len(rows))
        if not a.quiet:
            print('%3d %4d  %6d  %6d  %4d  %8d  %4d  %4d' % (
                m, d, w[1], w[2], w[0],
                sum(r['frames'] for r in rows) // n,
                sum(r['hq1'] for r in rows) // n,
                sum(r['hq2'] for r in rows) // n))
    for d in sorted(by_diff):
        w = by_diff[d]
        print('difficulty %d: P1 %d, P2 %d, draws %d' % (d, w[1], w[2], w[0]))
    games = sum(total.values())
    print('all: P1 %d, P2 %d, draws %d of %d' % (total[1], total[2],
                                                 total[0], games))
    for side in (1, 2):
        stats = [r['n'][side] for rows in results.values() for r in rows
                 if side in r['n']]
        if stats:
            keys = list(stats[0])
            print('P%d nightmare, a game: %s' % (side, '  '.join(
                '%s %.1f' % (k, sum(s.get(k, 0) for s in stats) / len(stats))
                for k in keys)))

    if a.check_wins is not None or a.check_losses is not None:
        me, other = a.check_side, 3 - a.check_side
        wins = 100.0 * total[me] / max(1, games)
        losses = 100.0 * total[other] / max(1, games)
        ok = ((a.check_wins is None or wins >= a.check_wins)
              and (a.check_losses is None or losses <= a.check_losses))
        print('%s: P%d won %.1f%% (at least %s), lost %.1f%% (at most %s)'
              % ('ok' if ok else 'FAILED', me, wins, a.check_wins, losses,
                 a.check_losses))
        return 0 if ok else 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
