#!/usr/bin/env python3
"""(Session 61) make unitcap: the UNITS patch (no limit to a side's units)
through hz_arena's --spawn, which puts a crowd of units down round each
HQ at the start (docs/units.md).

  python3 tools/hzunitcap_check.py ./hz_arena ASSETS [--units N]
                                   [--men N]

On each of maps 0, 4 and 6 (ABGRUND, STADT, WALDUNG), the nightmare AI
against the original's and the original's against itself, a game of
6000 frames:
  - with UNITS (--fixes B7): both sides past the original's 50 units, all
    N placed where the map has room (N a side), the game played to its end
    or its frames;
  - without (--fixes 37): neither side past 50 slots, so no more than 50
    units (Unit_Spawn refuses past them, as on the cartridge).
Then, with UNITS, the nightmare AI against the original's, the grid's
lists checked whole every frame (--check-crowd), no two units on top of
each other: on each map 10,000 men a side (--men) tried 8 px
apart round the HQs and sent at the enemy's, past 50 placed but not all
(session 68: a side's units block each other, so room, not a count, is
the limit), 3000 frames; and a game of 600 a side turning UNITS over every
97 frames (--toggle), the grid made again each time (its lists checked,
not the overlaps: the original's grid lets a crowd overlap).
Exit 1 on any failure."""

import argparse
import re
import subprocess
import sys

PLACED = re.compile(r'side (\d): (\d+) of (\d+) units placed, (\d+) slots')


def run(exe, assets, m, p1, p2, fixes, units, frames=6000, extra=()):
    p = subprocess.run([exe, assets, '--map', str(m), '--p1', p1,
                        '--p2', p2, '--games', '1', '--seed', '5',
                        '--frames', str(frames), '--fixes', fixes,
                        '--spawn', str(units)] + list(extra),
                       capture_output=True, text=True, timeout=900)
    sides = {}
    for line in p.stderr.splitlines():
        hit = PLACED.search(line)
        if hit:
            sides[int(hit.group(1))] = (int(hit.group(2)), int(hit.group(4)))
    result = [l for l in p.stdout.splitlines() if l.startswith('R ')]
    faults = [l for l in p.stderr.splitlines() if 'crowd_check' in l]
    return p.returncode, sides, result, faults


def report(name, ok, why, result):
    print('%-44s %s %s' % (name, 'ok' if ok else 'FAILED', why))
    if ok and result:
        print('    ' + result[0])
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('assets')
    ap.add_argument('--units', type=int, default=600)
    ap.add_argument('--men', type=int, default=10000)
    a = ap.parse_args()
    failures = 0
    for m in (0, 4, 6):
        for p1, p2 in (('nm', 'cpu'), ('cpu', 'cpu')):
            for fixes, unlimited in (('B7', True), ('37', False)):
                code, sides, result, _ = run(a.exe, a.assets, m, p1, p2,
                                             fixes, a.units)
                ok = code == 0 and len(result) == 1 and len(sides) == 2
                why = 'exit %d, %d results' % (code, len(result))
                if ok and unlimited:
                    ok = all(placed == a.units and slots > 50
                             for placed, slots in sides.values())
                    why = 'placed/slots %s' % sides
                elif ok:
                    ok = all(placed <= 50 and slots == 50
                             for placed, slots in sides.values())
                    why = 'placed/slots %s' % sides
                failures += report('map %d %s %s %s' % (
                    m, p1, p2, 'UNITS' if unlimited else 'off'), ok, why,
                    result)
    for m in (0, 4, 6):
        code, sides, result, faults = run(
            a.exe, a.assets, m, 'nm', 'cpu', 'B7', a.men, 3000,
            ['--spawn-type', '8', '--spawn-step', '8', '--spawn-order', '10',
             '--check-crowd'])
        ok = (code == 0 and len(result) == 1 and len(sides) == 2
              and not faults
              and all(50 < placed < a.men for placed, _ in sides.values()))
        why = 'exit %d, placed/slots %s%s' % (
            code, sides, (', ' + faults[0]) if faults else '')
        failures += report('map %d nm cpu UNITS %d men a side' % (m, a.men),
                           ok, why, result)
        code, sides, result, faults = run(
            a.exe, a.assets, m, 'nm', 'cpu', 'B7', a.units, 6000,
            ['--toggle', '97', '--check-crowd'])
        ok = code == 0 and len(result) == 1 and not faults
        why = 'exit %d%s' % (code, (', ' + faults[0]) if faults else '')
        failures += report('map %d nm cpu UNITS turned over' % m, ok, why,
                           result)
    print('unitcap: %s' % ('PASS' if failures == 0 else
                           'FAIL (%d)' % failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
