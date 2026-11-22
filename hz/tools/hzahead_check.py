#!/usr/bin/env python3
"""The nightmare AI through a delay (session 64, docs/nightmare.md
"Through a delay"): tools/hz_arena's bot sides with --delay and --ahead.

    hzahead_check.py HZ_ARENA ASSETS [--jobs N]

  - foresight: on every map a bot thinking 8 frames ahead against the
    original's CPU, from either side (--check-ahead): every pass the copy
    foresaw is the real game's, every part of the state hash (a CPU's
    pads are its own: nothing is guessed)
  - the same game: thinking ahead by none (--ahead at delay 0) plays the
    very games thinking on the game as it is does, on every map, bot
    against the CPU, the CPU against a bot, bot against bot and against
    the nightmare CPU
  - the delay: 4 games a map from either side against the original's CPU
    (TYPE A), the same seeds with no delay and with 8 frames: thinking
    ahead it wins two thirds as many as with none or more; thinking on
    the game as it is it loses three in four or more (so the first shows
    the thinking ahead, not an easy match)
Exit 1 when one fails."""

import argparse
import concurrent.futures
import os
import subprocess
import sys


def run(args):
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def results(text, side):
    won = lost = drawn = 0
    for line in text.splitlines():
        p = line.split()
        if p and p[0] == 'R':
            w = int(p[6])
            if w == side:
                won += 1
            elif w == 0:
                drawn += 1
            else:
                lost += 1
    return won, lost, drawn


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('arena')
    ap.add_argument('assets')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    base = [a.arena, a.assets]
    ok = True
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        # foresight
        jobs = {}
        for m in range(8):
            jobs[('P1', m)] = ex.submit(run, base + [
                '--map', str(m), '--p1', 'bot', '--p2', 'cpu', '--delay1',
                '8', '--ahead', '--check-ahead', '--seed', str(m + 1)])
            jobs[('P2', m)] = ex.submit(run, base + [
                '--map', str(m), '--p1', 'cpu', '--p2', 'bot', '--delay2',
                '8', '--ahead', '--check-ahead', '--seed', str(m + 11)])
        seen = exact = 0
        bad = []
        for key, f in jobs.items():
            code, out, err = f.result()
            for line in err.splitlines():
                if '--check-ahead:' in line:
                    w = line.split()
                    seen += int(w[2])
                    exact += int(w[5])
            if code != 0:
                bad.append('%s map %d' % key)
        good = not bad and seen > 0 and seen == exact
        ok = ok and good
        print('foresight     %s: %d passes foreseen 8 ahead, %d exactly%s'
              % ('ok' if good else 'FAIL', seen, exact,
                 ('; off: ' + ', '.join(bad)) if bad else ''))

        # the same game
        jobs = {}
        for m in range(8):
            for p1, p2 in (('bot', 'cpu'), ('cpu', 'bot'), ('bot', 'bot'),
                           ('bot', 'nm')):
                args = base + ['--map', str(m), '--p1', p1, '--p2', p2,
                               '--games', '2', '--seed', str(m + 21)]
                jobs[(m, p1, p2)] = (ex.submit(run, args),
                                     ex.submit(run, args + ['--ahead']))
        differ = []
        for key, (f0, f1) in jobs.items():
            if f0.result()[1] != f1.result()[1] or not f0.result()[1]:
                differ.append('map %d %s-%s' % key)
        good = not differ
        ok = ok and good
        print('the same game %s: %d games, thinking ahead by none and on '
              'the game as it is%s' % ('ok' if good else 'FAIL',
                                       2 * len(jobs),
                                       ('; differ: ' + ', '.join(differ))
                                       if differ else ' alike'))

        # the delay
        jobs = {}
        modes = {'none': ['0'], 'ahead': ['8', '--ahead'], 'plain': ['8']}
        for m in range(8):
            for mode, extra in modes.items():
                jobs[(1, mode, m)] = ex.submit(run, base + [
                    '--map', str(m), '--p1', 'bot', '--p2', 'cpu',
                    '--games', '4', '--seed', '31', '--delay1'] + extra)
                jobs[(2, mode, m)] = ex.submit(run, base + [
                    '--map', str(m), '--p1', 'cpu', '--p2', 'bot',
                    '--games', '4', '--seed', '41', '--delay2'] + extra)
        tally = {mode: [0, 0, 0] for mode in modes}
        for (side, mode, m), f in jobs.items():
            w, l, d = results(f.result()[1], side)
            t = tally[mode]
            t[0] += w
            t[1] += l
            t[2] += d
        w0, l0, d0 = tally['none']
        wa, la, da = tally['ahead']
        wp, lp, dp = tally['plain']
        good = 3 * wa >= 2 * w0 and lp * 4 >= 3 * (wp + lp + dp)
        ok = ok and good
        print('the delay     %s: 64 games against the CPU: with none %d won, '
              '%d lost, %d drawn; 8 frames thinking ahead %d won, %d lost, '
              '%d drawn (two thirds of the wins or more); thinking on the '
              'game as it is %d won, %d lost, %d drawn'
              % ('ok' if good else 'FAIL', w0, l0, d0, wa, la, da, wp, lp,
                 dp))
    print('ahead: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
