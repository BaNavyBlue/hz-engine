#!/usr/bin/env python3
"""The rumble's check (session 54): tools/hz_rumble over many seeds.

    hzrumble_check.py HZ_RUMBLE ASSETS OUTDIR [--seeds 1-32]
                      [--frames 200000] [--jobs N]

Each seed is a random two-pad run of the whole game (hz_rumble: the title
and options screen, the games -- two players, the CPU either side, CPU
against CPU -- their ends and the attract demo), its rumble held frame by
frame against the game's own sound effects and fields (the rules are at
the head of tools/hz_rumble.c), with an empty saves folder
(OUTDIR/saves/sN), all cores at once by default. PASS when no seed broke
a rule and every kind of shake was felt somewhere: a rule that never
fires holds nothing. Exit 1 otherwise."""

import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

KINDS = ['hit', 'ground', 'destroyed', 'jets', 'hq_hit', 'hq_alert',
         'missile', 'hq_lost', 'hq_won']


def seed_list(text):
    seeds = []
    for part in text.split(','):
        lo, _, hi = part.partition('-')
        seeds.extend(range(int(lo), int(hi or lo) + 1))
    return seeds


def run_one(binary, assets, outdir, seed, frames):
    saves = os.path.join(outdir, 'saves', 's%d' % seed)
    shutil.rmtree(saves, ignore_errors=True)
    os.makedirs(saves)
    env = dict(os.environ, HZ_SAVES=saves)
    p = subprocess.run([binary, assets, str(seed), str(frames)], env=env,
                       capture_output=True, text=True)
    return seed, p.returncode, p.stdout, p.stderr.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('assets')
    ap.add_argument('outdir')
    ap.add_argument('--seeds', default='1-32')
    ap.add_argument('--frames', type=int, default=200000)
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    binary = os.path.abspath(a.binary)
    totals = dict.fromkeys(KINDS, 0)
    failed = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        jobs = [pool.submit(run_one, binary, a.assets, a.outdir, s, a.frames)
                for s in seed_list(a.seeds)]
        for job in jobs:
            seed, rc, out, err = job.result()
            with open(os.path.join(a.outdir, 's%d.txt' % seed), 'w') as f:
                f.write(out)
            lines = out.strip().splitlines()
            summary = lines[-1] if lines else '(no output)'
            print(summary)
            for line in lines[:-1][:8]:
                print('   ', line)
            m = re.search(r'felt (.*); (\d+) broken', summary)
            if rc != 0 or m is None:
                failed.append(seed)
                if err:
                    print('   ', err.splitlines()[-1])
            if m is None:
                continue
            words = m.group(1).split()
            for name, count in zip(words[0::2], words[1::2]):
                totals[name] += int(count)
    print('felt over the seeds:',
          ', '.join('%s %d' % (k, totals[k]) for k in KINDS))
    never = [k for k in KINDS if totals[k] == 0]
    if never:
        print('never felt:', ', '.join(never))
    if failed or never:
        print('rumble: FAIL (%s)' % ', '.join(
            (['seeds %s broke a rule' % failed] if failed else [])
            + (['a kind never felt'] if never else [])))
        return 1
    print('rumble: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
