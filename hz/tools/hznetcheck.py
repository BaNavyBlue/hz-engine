#!/usr/bin/env python3
"""The determinism check for netplay: tools/hz_netcheck over many seeds,
and two machines' (or two compilers') results compared.

    hznetcheck.py run HZ_NETCHECK ASSETS OUTDIR [--seeds 1-32]
                      [--frames 200000] [--jobs N] [--seen]
    hznetcheck.py compare REF OTHER...

A network game would run the whole game on both machines and send only
the pads, so the two must come out the same from the same pads. `run`
plays each seed's random two-pad game (hz_netcheck: the title and options
screen, the load, the game, its end, the attract demo) and writes its OUT
to OUTDIR/sN.txt (a hash of the state every second; the games started and
left), each with an empty saves folder (OUTDIR/saves/sN), all cores at
once by default; then what the seeds covered.

`compare` checks each seed in OTHER against REF: the hashes and the games
as far as the shorter run goes, so a short run (a slow build: -O0, a
sanitizer) compares with the start of a long one. It names the first
second where they part; `hz_netcheck ... --verbose FROM TO` then gives
that second's frames field by field. Exit 1 on any difference."""

import argparse
import collections
import concurrent.futures
import os
import shutil
import subprocess
import sys

MODES = {0: 'two players', 1: 'P2 CPU', 2: 'P1 CPU', 3: 'CPU v CPU',
         4: 'two players wide', 5: 'P2 CPU wide', 6: 'P1 CPU wide',
         7: 'CPU v CPU wide'}


def seed_list(text):
    seeds = []
    for part in text.split(','):
        lo, _, hi = part.partition('-')
        seeds.extend(range(int(lo), int(hi or lo) + 1))
    return seeds


def run_one(binary, assets, outdir, seed, frames, mode=None):
    saves = os.path.join(outdir, 'saves', 's%d' % seed)
    shutil.rmtree(saves, ignore_errors=True)
    os.makedirs(saves)
    env = dict(os.environ, HZ_SAVES=saves)
    p = subprocess.run([binary, assets, str(seed), str(frames),
                        os.path.join(outdir, 's%d.txt' % seed)]
                       + ([mode] if mode else []),
                       env=env, capture_output=True, text=True)
    return seed, p.returncode, p.stdout.strip(), p.stderr.strip()


def load(path):
    """A run's S lines, its G and X lines (but the X of a run cut short)
    and its frame count (None if it didn't finish)."""
    with open(path, 'rb') as f:
        text = f.read().replace(b'\r', b'').decode()
    s, games, end = [], [], None
    for line in text.split('\n'):
        if line.startswith('S '):
            s.append(line)
        elif line.startswith('G ') or (line.startswith('X ')
                                       and not line.endswith(' quit')):
            games.append(line)
        elif line.startswith('E '):
            end = int(line.split()[1])
    return s, games, end


def coverage(outdir, seeds):
    modes = collections.Counter()
    maps = collections.Counter()
    exits = collections.Counter()
    demos = frames = 0
    for n in seeds:
        s, games, end = load(os.path.join(outdir, 's%d.txt' % n))
        frames += end or 0
        for line in games:
            p = line.split()
            if p[0] == 'X':
                exits[p[2]] += 1
            elif len(p) > 5:
                demos += 1
            else:
                modes[MODES.get(int(p[2]), p[2])] += 1
                maps[int(p[3])] += 1
    print('covered: %.1f hours of play; games by mode %s; by map %s; '
          '%d demos; ended %s' % (frames / 216000, dict(modes),
                                  dict(sorted(maps.items())), demos,
                                  dict(exits)))


def cmd_run(a):
    os.makedirs(a.outdir, exist_ok=True)
    seeds = seed_list(a.seeds)
    binary = os.path.abspath(a.binary)
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        mode = '--asked' if a.asked else '--seen' if a.seen else None
        jobs = [pool.submit(run_one, binary, a.assets, a.outdir, n, a.frames,
                            mode)
                for n in seeds]
        with open(os.path.join(a.outdir, 'summary.txt'), 'w') as summary:
            for job in jobs:
                seed, rc, out, err = job.result()
                summary.write(out + '\n')
                if rc != 0 or err:
                    failed += 1
                    print('seed %d: exit %d %s' % (seed, rc, err))
    print('%d seeds of %d frames in %s' % (len(seeds), a.frames, a.outdir))
    coverage(a.outdir, seeds)
    return 1 if failed else 0


def cmd_compare(a):
    bad = 0
    for other in a.other:
        seeds = sorted(int(f[1:-4]) for f in os.listdir(other)
                       if f.startswith('s') and f.endswith('.txt')
                       and f[1:-4].isdigit())
        agree = seconds = 0
        for n in seeds:
            ref_s, ref_games, ref_end = load(os.path.join(a.ref, 's%d.txt' % n))
            s, games, end = load(os.path.join(other, 's%d.txt' % n))
            if ref_end is None or end is None:
                print('%s seed %d: unfinished' % (other, n))
                bad += 1
                continue
            k = min(len(ref_s), len(s))
            upto = min(ref_end, end)
            why = None
            for i in range(k):
                if ref_s[i] != s[i]:
                    why = ('first differs at second %d (frames %d-%d):\n'
                           '    %s\n    %s' % (i + 1, i * 60, i * 60 + 59,
                                               ref_s[i], s[i]))
                    break
            if why is None:
                ref_g = [g for g in ref_games if int(g.split()[1]) < upto]
                g = [g for g in games if int(g.split()[1]) < upto]
                if ref_g != g:
                    why = 'the games differ: %s / %s' % (ref_g, g)
            if why:
                print('%s seed %d: %s' % (other, n, why))
                bad += 1
            else:
                agree += 1
                seconds += k
        print('%s: %d of %d seeds as %s (%.1f hours of play compared)'
              % (other, agree, len(seeds), a.ref, seconds / 3600))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('binary')
    r.add_argument('assets')
    r.add_argument('outdir')
    r.add_argument('--seeds', default='1-32')
    r.add_argument('--frames', type=int, default=200000)
    r.add_argument('--jobs', type=int, default=os.cpu_count())
    r.add_argument('--seen', action='store_true',
                   help='(session 79) hash the state, the screen and the '
                   'sound only (hz_netcheck --seen)')
    r.add_argument('--asked', action='store_true',
                   help='(session 81) hash the state, the layer but its '
                   'sprites, the sprites the game asked for and the sound '
                   '(hz_netcheck --asked)')
    c = sub.add_parser('compare')
    c.add_argument('ref')
    c.add_argument('other', nargs='+')
    a = ap.parse_args()
    return cmd_run(a) if a.cmd == 'run' else cmd_compare(a)


if __name__ == '__main__':
    sys.exit(main())
