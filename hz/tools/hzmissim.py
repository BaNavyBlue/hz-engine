#!/usr/bin/env python3
"""(Session 68) A SAM-42's missiles against a jet on set flights, played out
as shots.c and unit_ai.c have them: fired at most every 32 frames while
the mech is within $100 (Chebyshev, top-left to top-left), 16-way aim
from the launcher, speed 16 x SineTable (2 px a frame), aimed again every
4 frames, the velocity steered $40 a frame an axis, life 255, hit box
$18; 72 a hit against 256 HP (docs/nightmare.md, "Round the missiles,
and down on the move").

  python3 tools/hzmissim.py

Prints the hits each flight takes."""
import math

SINE = [(0, -32), (12, -29), (22, -22), (29, -12), (32, 0), (29, 12), (22, 22), (12, 29),
        (0, 32), (-12, 29), (-22, 22), (-29, 12), (-32, 0), (-29, -12), (-22, -22), (-12, -29)]
SLOPE = [-0x50, -0x17, -0x0A, -0x03, 0x03, 0x0A, 0x17, 0x50]


def get_direction(x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    if dx == 0:
        return 8 if dy >= 0 else 0
    if dy == 0:
        return 4 if dx >= 0 else 0x0C
    num, den = dy, dx
    if dx < 0:
        den, num = -dx, -num
    q = int((num << 4) / den)
    d = 0
    for s in SLOPE:
        if q <= s:
            break
        d += 1
    if dx < 0:
        d += 8
    return d & 15


def steer(cur, aim):
    if aim == cur:
        return cur
    if aim > cur:
        return min(aim, cur + 0x40)
    return max(aim, cur - 0x40)


def fly(path, life=255, delay=32, sam=(0, 0), speed=16):
    """path: list of the mech's top-left (x, y) a frame. Hits taken."""
    sx, sy = sam
    missiles = []
    wait = 0
    hits = 0
    for f, (ex, ey) in enumerate(path):
        ex, ey = int(ex), int(ey)
        if wait > 0:
            wait -= 1
        if wait == 0 and max(abs(ex - sx), abs(ey - sy)) < 0x100:
            d = get_direction(sx, sy, ex, ey)
            vx, vy = speed * SINE[d][0], speed * SINE[d][1]
            missiles.append([(sx + 8) * 256, (sy + 8) * 256, vx, vy, vx, vy, life])
            wait = delay
        keep = []
        for m in missiles:
            m[6] -= 1
            if m[6] == 0:
                continue
            if m[6] & 3 == 0:
                w = get_direction(m[0] >> 8, m[1] >> 8, ex + 12, ey + 12)
                m[4], m[5] = speed * SINE[w][0], speed * SINE[w][1]
            m[2] = steer(m[2], m[4])
            m[3] = steer(m[3], m[5])
            m[0] += m[2]
            m[1] += m[3]
            x, y = m[0] >> 8, m[1] >> 8
            if ex < x and x - ex < 0x18 and ey < y and y - ey < 0x18:
                hits += 1
                continue
            keep.append(m)
        missiles = keep
    return hits


def line(x0, y0, x1, y1, v):
    n = int(max(abs(x1 - x0), abs(y1 - y0)) / v)
    return [(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n) for i in range(n + 1)]


def run():
    for v in (4, 3):
        print('jet %d px a frame' % v)
        for b in (0, 64, 96, 128, 160, 192, 224, 255):
            p = line(-700, b, 900, b, v)
            hold = line(-700, b, 300, b, v) + [(300, b)] * 90
            away = line(-700, b, 0, b, v) + line(0, b, 0, b + 900, v)
            print('  pass %3d px: straight %d   stop 300 px past %d   turn away at it %d'
                  % (b, fly(p), fly(hold), fly(away)))
        for r in (128, 160, 192, 224):
            pts = []
            n = int(2 * math.pi * r / v)
            for i in range(n // 2):
                a = math.pi + math.pi * i / (n // 2)
                pts.append((r * math.cos(a), -r * math.sin(a)))
            pts = line(-700, 0, -r, 0, v) + pts + line(r, 0, 900, 0, v)
            print('  half circle round it at %d: %d' % (r, fly(pts)))
        # head on: towards it from 700 px, turn 90 degrees at d
        for d in (300, 256, 200, 160):
            p = line(-700, 0, -d, 0, v) + line(-d, 0, -d, 900, v)
            print('  head on, turned off at %d: %d' % (d, fly(p)))


run()
