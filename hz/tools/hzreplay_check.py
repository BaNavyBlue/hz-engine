#!/usr/bin/env python3
"""Recorded games (session 59, src/game/replay.h, docs/replays.md): games
played by the headless game from scripted pads, each written to
saves/replays/ as it ends, then played again by hz_replay, which must
find each the game recorded (every second's hash alike, the same winner).

    hzreplay_check.py HZ_HEADLESS HZ_REPLAY ASSETS [--keep DIR]

The cases: P1 against the nightmare AI on VULKAN; P1 against the
original's AI on STRAND, TYPE C, with Start now and then (a pause's
frames are recorded too); P2 against the nightmare AI on LOCH (mode 6:
P1 the CPU, the wide screen P2's); two players on the split screen on
EISFREI, both pads scripted. Each runs until its HQ falls or HZ_FRAMES.
Exit 1 if a game isn't written or doesn't play the same."""

import argparse
import glob
import os
import random
import shutil
import subprocess
import sys
import tempfile


def pad_script(seed, steps=500, starts=False):
    """HZ_PADn's "N:BUTTONS,...": chords held 1-40 frames (a tenth of
    them 60-250); with `starts`, Start alone 1 in 80 (a pause, and the
    next Start ends it)."""
    r = random.Random(seed)
    dirs = ['', 'U', 'D', 'L', 'R', 'UL', 'UR', 'DL', 'DR']
    out, frame = [], 0
    for _ in range(steps):
        if starts and r.randrange(80) == 0:
            chord = 'S'
        else:
            chord = r.choice(dirs) + ''.join(b for b in 'ABC'
                                             if r.randrange(4) == 0)
        out.append('%d:%s' % (frame, chord or '-'))
        frame += (60 + r.randrange(191)) if r.randrange(10) == 0 \
            else 1 + r.randrange(40)
    return ','.join(out)


CASES = [
    # name, args, pads (P1, P2), frames
    ('nightmare-p1', ['--map', '1', '--mode', '5', '--difficulty', '0',
                      '--cpu', 'nightmare'], (1, None), 20000),
    ('original-pause', ['--map', '3', '--mode', '5', '--difficulty', '2',
                        '--cpu', 'original'], (2, None), 20000),
    ('nightmare-p2', ['--map', '2', '--mode', '6', '--difficulty', '1',
                      '--cpu', 'nightmare'], (3, None), 20000),
    ('two-players', ['--map', '5', '--mode', '0', '--difficulty', '0'],
     (4, 5), 12000),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('headless')
    ap.add_argument('replay')
    ap.add_argument('assets')
    ap.add_argument('--keep', help='keep the cases\' folders here')
    a = ap.parse_args()
    work = a.keep or tempfile.mkdtemp(prefix='hzreplay_')
    ok = True
    for name, args, pads, frames in CASES:
        d = os.path.join(work, name)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        env = dict(os.environ, HZ_SAVES=d, HZ_MODS='', HZ_FRAMES=str(frames),
                   HZ_OUT=os.path.join(d, 'last.png'))
        env.pop('HZ_REPLAYS', None)
        env['HZ_PAD1'] = pad_script(pads[0], starts=name.endswith('pause'))
        if pads[1] is not None:
            env['HZ_PAD2'] = pad_script(pads[1])
        with open(os.path.join(d, 'game.out'), 'w') as out:
            subprocess.run([a.headless, '--assets', a.assets] + args,
                           env=env, stdout=out, stderr=subprocess.STDOUT,
                           timeout=600)
        files = sorted(glob.glob(os.path.join(d, 'replays', '*.hzr')))
        if len(files) != 1:
            print('%-15s FAILED: %d replays written' % (name, len(files)))
            ok = False
            continue
        r = subprocess.run([a.replay, a.assets, files[0], '--check'],
                           capture_output=True, text=True, timeout=600)
        said = (r.stdout + r.stderr).strip().replace(d + os.sep, '')
        good = r.returncode == 0 and 'the game recorded' in said
        print('%-15s %s: %s' % (name, 'ok' if good else 'FAILED', said))
        ok = ok and good
    if not a.keep:
        shutil.rmtree(work, ignore_errors=True)
    print('replay_check: %s' % ('all the games recorded' if ok
                                else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
