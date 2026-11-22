#!/usr/bin/env python3
"""Mutation tests (run from the hz/ directory, with the ROM as hz.md):

  python3 tools/hz_mutate.py LIST [FIRST [LAST]]

LIST is a Python file defining MUTATIONS = [(label, path, old, new), ...]
(tools/mutations/*.py) and optionally CHECK, which check runs (default
'mech'). For each mutation in [FIRST, LAST): replace `old` (which must
occur exactly once) with `new` in `path`, rebuild the check's harnesses,
run its steps until one fails ("caught") or all pass ("NOT CAUGHT":
equivalent, or not reached); then put the file back.

  mech  hz_mech on the RUNS below, each log through tools/hzmech_check.py
        (~15 s a mutation since session 22: ~16 a command in the sandbox's
        300 s)
  hud   hz_hud (5,000 states, 500 flushes) through tools/hzhud_check.py,
        two hz_loop runs with pokes through tools/hzloop_check.py, then a
        180-frame hz_shot pan with --chase --debris through
        tools/hzview_check.py (~12 s: ~15 a command)
  anim  hz_anim (100 runs) through tools/hzanim_check.py, an hz_loop run
        with pokes through tools/hzloop_check.py, 180-frame hz_shot pans
        on STRAND (split) and VULKAN (wide) through tools/hzview_check.py
        (~12 s)
  anim-ship  the same without hz_anim: what the shipping maps alone catch
        (CHECK=anim-ship in the environment overrides the list's CHECK)
  combat  hz_shots (maps 1 and 7, 3,000 cases each) through
        tools/hzshots_check.py, then two hz_mech runs (two players; P1 the
        CPU) through tools/hzmech_check.py
  unitmove  hz_unitmove (maps 1, 5, 6: 150, 120, 120 cases) through
        tools/hzunitmove_check.py, then an hz_mech run (VULKAN, wide, P2
        the CPU, its units moving) through tools/hzmech_check.py
  ordermenu  tools/hzordermenu_check.py, seeds 1 and 2 (400 cases each,
        the original on tools/m68krun.py; ~15-35 s a mutation)
  mechai  tools/hzmechai_check.py, seeds 11, 8, 7, 12 (2 games of 8,000
        frames each; seed 11 with AI pokes 6 per 1000 frames, the rest 60;
        the original on tools/m68krun.py; stops at the first difference;
        ~15-75 s a mutation)
  sound  hz_sound on every case of tools/hzsound_check.py (shipping songs
        and sfx, made-up banks), against the original driver's logs kept
        in /tmp/hzsound_cache (made on the first run, ~6 minutes in
        chunks: python3 tools/hzsound_check.py hz.md assets A B --cache
        /tmp/hzsound_cache); stops at the first difference (~8 s)
  gameend  tools/hzgameend_check.py: seed 1 (make gameend's first 12
        runs), then --endings seeds 101 and 102 (3 runs each, nearly all
        reaching the endings); the original on tools/m68krun.py; stops at
        the first difference; ~1 minute a caught mutation, ~8 not caught:
        run it detached, on a copy of the tree)"""
import os, signal, subprocess, sys


def _stopped(signum, frame):
    # `timeout` sends SIGTERM: raise, so the finally below puts the file
    # back instead of leaving the mutation in the source (session 25)
    raise KeyboardInterrupt


signal.signal(signal.SIGTERM, _stopped)
# (map, mode, seed): lava, currents, ice, slow ground, split and wide,
# with and without a CPU.
RUNS = [(1, 0, 1), (1, 5, 2), (0, 1, 3), (5, 6, 4), (6, 0, 5), (7, 5, 6),
        (3, 1, 7), (4, 6, 8), (2, 0, 9)]


def mech_steps():
    for k, (m, mode, seed) in enumerate(RUNS):
        out = '/tmp/mut_%d.txt' % k
        yield ('map %d mode %d' % (m, mode),
               ['./hz_mech', 'assets', out, '--map', str(m), '--mode', str(mode),
                '--difficulty', str((m + mode) % 4), '--passes', '3000',
                '--seed', str(seed + 100)],
               ['python3', 'tools/hzmech_check.py', 'hz.md', out])


def hud_steps():
    yield ('hz_hud', ['./hz_hud', '/tmp/mut_hud.txt', '--seed', '7',
                      '--states', '5000', '--flushes', '500'],
           ['python3', 'tools/hzhud_check.py', 'hz.md', '/tmp/mut_hud.txt'])
    for k, (m, mode, seed) in enumerate(((1, 1, 31), (4, 2, 32))):
        out = '/tmp/mut_loop_%d.txt' % k
        yield ('loop map %d mode %d' % (m, mode),
               ['./hz_loop', 'assets', out, '--map', str(m), '--mode', str(mode),
                '--difficulty', str((m + mode) % 4), '--passes', '2000',
                '--pokes', '--pads-random', '--seed', str(seed)],
               ['python3', 'tools/hzloop_check.py', 'hz.md', out])
    d = '/tmp/mut_pan'
    yield ('pan', ['sh', '-c', 'rm -rf %s && mkdir -p %s && ./hz_shot assets '
                   '%s.png --map 5 --mode 0 --pan "8,0,60:0,8,60:-3,-5,60" '
                   '--chase --debris --raw-frames %s' % (d, d, d, d)],
           ['python3', 'tools/hzview_check.py', 'hz.md', d])


def anim_steps():
    yield ('hz_anim', ['./hz_anim', 'assets', '/tmp/mut_anim.txt', '--seed',
                       '7', '--runs', '100'],
           ['python3', 'tools/hzanim_check.py', 'hz.md', '/tmp/mut_anim.txt'])
    yield ('loop map 1 mode 1',
           ['./hz_loop', 'assets', '/tmp/mut_loop_a.txt', '--map', '1',
            '--mode', '1', '--difficulty', '2', '--passes', '1000', '--pokes',
            '--pads-random', '--seed', '41'],
           ['python3', 'tools/hzloop_check.py', 'hz.md', '/tmp/mut_loop_a.txt'])
    for m, mode in ((3, 0), (1, 5)):
        d = '/tmp/mut_pan_%d' % m
        yield ('pan map %d' % m,
               ['sh', '-c', 'rm -rf %s && mkdir -p %s && ./hz_shot assets '
                '%s.png --map %d --mode %d --pan "8,0,60:0,8,60:-3,-5,60" '
                '--chase --raw-frames %s' % (d, d, d, m, mode, d)],
               ['python3', 'tools/hzview_check.py', 'hz.md', d])


def combat_steps():
    for m, seed in ((1, 51), (7, 52)):
        out = '/tmp/mut_combat_%d.txt' % m
        yield ('hz_shots map %d' % m,
               ['./hz_shots', 'assets', out, '--map', str(m), '--cases',
                '3000', '--seed', str(seed)],
               ['python3', 'tools/hzshots_check.py', 'hz.md', out])
    for k, (m, mode, seed) in enumerate(((2, 0, 53), (4, 6, 54))):
        out = '/tmp/mut_cmech_%d.txt' % k
        yield ('mech map %d mode %d' % (m, mode),
               ['./hz_mech', 'assets', out, '--map', str(m), '--mode',
                str(mode), '--difficulty', str((m + mode) % 4), '--passes',
                '3000', '--seed', str(seed)],
               ['python3', 'tools/hzmech_check.py', 'hz.md', out])


def unitmove_steps():
    for m, seed, n in ((1, 61, 150), (5, 62, 120), (6, 63, 120)):
        out = '/tmp/mut_unitmove_%d.txt' % m
        yield ('hz_unitmove map %d' % m,
               ['./hz_unitmove', 'assets', out, '--map', str(m), '--cases',
                str(n), '--seed', str(seed)],
               ['python3', 'tools/hzunitmove_check.py', 'hz.md', out])
    out = '/tmp/mut_umech.txt'
    yield ('mech map 1 mode 5',
           ['./hz_mech', 'assets', out, '--map', '1', '--mode', '5',
            '--difficulty', '2', '--passes', '3000', '--seed', '64'],
           ['python3', 'tools/hzmech_check.py', 'hz.md', out])


def unitai_steps():
    for seed, n in ((71, 250), (72, 250)):
        yield ('unitai seed %d' % seed, ['true'],
               ['python3', 'tools/hzunitai_check.py', 'hz.md', 'hz.sym',
                './hz_unitai', 'assets', '--cases', str(n), '--seed',
                str(seed), '--quiet'])


def ordermenu_steps():
    for seed in (1, 2):
        yield ('ordermenu seed %d' % seed, ['true'],
               ['python3', 'tools/hzordermenu_check.py', 'hz.md', 'hz.sym',
                './hz_ordermenu', 'assets', '--cases', '400', '--seed',
                str(seed), '--quiet'])


def mechai_steps():
    for seed, ai_pokes in ((11, '6'), (8, '60'), (7, '60'), (12, '60')):
        yield ('mechai seed %d' % seed, ['true'],
               ['python3', 'tools/hzmechai_check.py', 'hz.md', 'hz.sym',
                './hz_mechai', 'assets', '--seed', str(seed), '--games', '2',
                '--frames', '8000', '--sample', '30', '--stop', '--pokes',
                '2', ai_pokes])


def front_steps():
    for seed in (1, 2):
        yield ('front seed %d' % seed, ['true'],
               ['python3', 'tools/hzfront_check.py', 'hz.md', 'hz.sym',
                './hz_front', 'assets', '--runs', '8', '--frames', '1200',
                '--seed', str(seed), '--stop'])


def gameend_steps():
    yield ('gameend seed 1', ['true'],
           ['python3', 'tools/hzgameend_check.py', 'hz.md', 'hz.sym',
            './hz_gameend', 'assets', '--runs', '12', '--seed', '1',
            '--stop'])
    for seed in (101, 102):
        yield ('gameend seed %d' % seed, ['true'],
               ['python3', 'tools/hzgameend_check.py', 'hz.md', 'hz.sym',
                './hz_gameend', 'assets', '--runs', '3', '--endings',
                '--seed', str(seed), '--stop'])


def anim_ship_steps():
    for step in list(anim_steps())[1:]:
        yield step


def sound_steps():
    yield ('sound', ['true'],
           ['python3', 'tools/hzsound_check.py', 'hz.md', 'assets',
            '--cache', '/tmp/hzsound_cache', '--stop'])


CHECKS = {'mech': (['hz_mech'], mech_steps),
          'sound': (['hz_sound'], sound_steps),
          'hud': (['hz_hud', 'hz_loop', 'hz_shot'], hud_steps),
          'anim': (['hz_anim', 'hz_loop', 'hz_shot'], anim_steps),
          'anim-ship': (['hz_loop', 'hz_shot'], anim_ship_steps),
          'combat': (['hz_shots', 'hz_mech'], combat_steps),
          'unitmove': (['hz_unitmove', 'hz_mech'], unitmove_steps),
          'unitai': (['hz_unitai'], unitai_steps),
          'ordermenu': (['hz_ordermenu'], ordermenu_steps),
          'mechai': (['hz_mechai'], mechai_steps),
          'front': (['hz_front'], front_steps),
          'gameend': (['hz_gameend'], gameend_steps)}

ns = {}
exec(open(sys.argv[1]).read(), ns)
# A copy of the file being mutated is kept as PATH.unmutated until it is
# put back, so a run killed at a bad moment (session 28: one was, and left
# a mutation in) is mended by the next run before it starts.
for _, path, _, _ in ns['MUTATIONS']:
    if os.path.exists(path + '.unmutated'):
        os.replace(path + '.unmutated', path)
        print('%s: put back from %s.unmutated' % (path, path))
targets, steps = CHECKS[os.environ.get('CHECK', ns.get('CHECK', 'mech'))]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
last = int(sys.argv[3]) if len(sys.argv) > 3 else len(ns['MUTATIONS'])
muts = ns['MUTATIONS'][first:last]
for label, path, old, new in muts:
    src = open(path).read()
    if src.count(old) != 1:
        print('%-40s BAD PATTERN (%d)' % (label, src.count(old)))
        continue
    backup = src
    open(path + '.unmutated', 'w').write(backup)
    open(path, 'w').write(src.replace(old, new))
    try:
        r = subprocess.run(['make', '-s', '-j3'] + targets, capture_output=True,
                           text=True)
        if r.returncode:
            print('%-40s BUILD FAILED' % label); continue
        caught = None
        for name, harness, check in steps():
            r = subprocess.run(harness, capture_output=True, text=True)
            if r.returncode:
                caught = '%s: harness rc %d' % (name, r.returncode); break
            r = subprocess.run(check, capture_output=True, text=True)
            if r.returncode:
                lines = [l.strip() for l in r.stdout.split('\n')
                         if l.strip().startswith(('pass', 'U ', 'Q ', 'F ', 'f0'))]
                lines += [l.strip() for l in r.stdout.split('\n')
                          if 'DIFFER' in l or 'failed' in l or 'hung' in l]
                lines += [l.strip() for l in r.stdout.split('\n')
                          if l.strip() and 'ok' not in l.split()[:3]]
                caught = '%s: %s' % (name, lines[0] if lines else r.stdout[:80])
                break
        print('%-40s %s' % (label, ('caught: ' + caught[:110]) if caught else 'NOT CAUGHT'), flush=True)
    finally:
        open(path, 'w').write(backup)
        os.remove(path + '.unmutated')
subprocess.run(['make', '-s', '-j3'] + targets, capture_output=True)
