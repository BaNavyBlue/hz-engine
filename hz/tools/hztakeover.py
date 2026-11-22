#!/usr/bin/env python3
"""A recorded game taken over, many times (session 73): hz_arena --replay
FILE --takeover FRAME with the nightmare AI as it is now on the side it
had (or any side: SIDE), a stand-in for the person on the other, games
side by side on every core; the AI's won-lost-drawn and what it did.

    python3 tools/hztakeover.py HZ_ARENA ASSETS FILE SIDE --at F[,F...]
        [--seeds 1-24] [--spread N] [--frames N] [--shadow]
        [--param NAME=V ...] [--foe turtle] [--foe-param NAME=V ...]
        [--put SPEC] [--rows] [--jobs J]

SIDE is the AI's (1 or 2); --foe plays the other (turtle by default: the
bot playing a player's way, hz_arena.c's TURTLE_PARAMS; bot with
--foe-param for another, idle for nobody). --at: the frames to take over
at, a row of results each. --seeds: a game a seed. A seed alone changes
little: both sides think the same thoughts until the game's own random
numbers come into it (some seeds played the very same game). --spread N
takes each seed over N frames later than the last (seed 1 at F), which
makes them different games: use it. --frames: each game's length from
its takeover (hz_arena's hour from the recording's start without it).
--shadow: hz_arena's, the AI thinking along before the takeover -- only
for the AI as it was when the game was recorded: a changed one thinks
other thoughts than the pads played, and takes over believing them
(session 73: thinking along with EDGE on it lost 94 games of 96 from 20
minutes, 9 with it off; not thinking along, 23 and 15). --param: the
AI's tuning (hz_arena --paramN for its side), so a change is tried
against its switch off. --put: hz_arena's, units put down for the other
side.

A row: at FRAME: won-lost-drawn, the HQs' HP, the outposts and the frames
at the end on average, and the AI's own counts a game (hz_arena's N
line). --rows adds a line a game.
"""
import argparse
import concurrent.futures as cf
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('assets')
    ap.add_argument('replay')
    ap.add_argument('side', type=int, choices=(1, 2))
    ap.add_argument('--at', default='43200')
    ap.add_argument('--seeds', default='1-24')
    ap.add_argument('--spread', type=int, default=0)
    ap.add_argument('--frames', type=int, default=0)
    ap.add_argument('--shadow', action='store_true')
    ap.add_argument('--param', action='append', default=[])
    ap.add_argument('--foe', default='turtle')
    ap.add_argument('--foe-param', action='append', default=[])
    ap.add_argument('--put')
    ap.add_argument('--rows', action='store_true')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    first, last = a.seeds.split('-')
    seeds = range(int(first), int(last) + 1)
    ats = [int(v) for v in a.at.split(',')]
    me, foe = a.side, 3 - a.side

    def play(job):
        at, seed = job
        real = at + a.spread * (seed - 1)
        cmd = [a.exe, a.assets, '--replay', a.replay, '--p%d' % me, 'bot',
               '--p%d' % foe, a.foe, '--takeover', str(real), '--seed',
               str(seed)]
        if a.frames:
            cmd += ['--frames', str(real + a.frames)]
        if a.shadow:
            cmd.append('--shadow')
        for p in a.param:
            cmd += ['--param%d' % me, p]
        for p in a.foe_param:
            cmd += ['--param%d' % foe, p]
        if a.put:
            cmd += ['--put', a.put]
        r = subprocess.run(cmd, capture_output=True, text=True)
        out = {'at': at, 'seed': seed, 'real': real, 'n': {}}
        for line in r.stdout.splitlines():
            f = line.split()
            if f and f[0] == 'R':
                out.update(winner=int(f[6]), frames=int(f[7]),
                           hq=[int(f[8]), int(f[9])],
                           posts=[int(f[10]), int(f[11])],
                           units=[int(f[12]), int(f[13])])
            elif f and f[0] == 'U':
                out.update(tried=int(f[1]), placed=int(f[2]))
            elif f and f[0] == 'N' and int(f[1]) == me:
                out['n'] = dict(zip(f[2::2], (int(v) for v in f[3::2])))
        if 'winner' not in out:
            sys.stderr.write('hztakeover: %s failed: %s\n' % (
                ' '.join(cmd), r.stderr.strip()))
            sys.exit(1)
        return out

    jobs = [(at, s) for at in ats for s in seeds]
    with cf.ThreadPoolExecutor(max(1, a.jobs)) as pool:
        res = list(pool.map(play, jobs))
    total = [0, 0, 0]
    keys = ['delivered', 'walled', 'guarded', 'capsent', 'raidguards', 'buys',
            'deaths', 'kills', 'holdbuys', 'freebuys', 'supplies', 'strikes',
            'strikekills', 'foughton']
    for at in ats:
        rs = [o for o in res if o['at'] == at]
        n = len(rs)
        won = sum(1 for o in rs if o['winner'] == me)
        lost = sum(1 for o in rs if o['winner'] == foe)
        total[0] += won
        total[1] += lost
        total[2] += n - won - lost
        print('at %6d: %d-%d-%d  HQ %.0f to %.0f  outposts %.1f to %.1f  '
              'frames %.0f | %s' % (
                  at, won, lost, n - won - lost,
                  sum(o['hq'][me - 1] for o in rs) / n,
                  sum(o['hq'][foe - 1] for o in rs) / n,
                  sum(o['posts'][me - 1] for o in rs) / n,
                  sum(o['posts'][foe - 1] for o in rs) / n,
                  sum(o['frames'] for o in rs) / n,
                  ' '.join('%s %.1f' % (k, sum(o['n'].get(k, 0)
                                                 for o in rs) / n)
                           for k in keys)))
        if a.put:
            print('           --put: %.1f put down of %.1f tried' % (
                sum(o.get('placed', 0) for o in rs) / n,
                sum(o.get('tried', 0) for o in rs) / n))
        if a.rows:
            for o in rs:
                print('   seed %2d from %6d: winner %d at %6d  HQ %4d %4d  '
                      'outposts %d %d  units %d %d' % (
                          o['seed'], o['real'], o['winner'], o['frames'],
                          o['hq'][0], o['hq'][1], o['posts'][0],
                          o['posts'][1], o['units'][0], o['units'][1]))
    if len(ats) > 1:
        print('all: %d-%d-%d' % tuple(total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
