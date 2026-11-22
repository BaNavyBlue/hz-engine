#!/usr/bin/env python3
"""The balance between two network players' clocks (session 63,
docs/netplay.md "The pace"): tools/hz_netbot's players, each on its own
clock through src/core/pace.c, against servers of their own.

    hznetpace_check.py HZ_SERVER HZ_NETBOT

Each case a pair, 2,400 frames (40 s), every datagram held 20 ms and up
to 4 ms more (HZ_NET_LAG, HZ_NET_JITTER), all at once, a server each:
  - even: both clocks at the game's rate: no waits for the rival after
    the first 600 frames, neither ahead at the end, nothing held back
  - drift: P2's clock 0.2% fast (a frame every 8 s; real clocks are
    nearer 0.005%): no waits after 600 frames, P2 held back and never
    more than a frame and a half ahead, even at the end; P1 held nothing
  - drift-unbalanced: the same with the balance off (HZ_NET_BALANCE=0):
    P2 three frames ahead or more by the end, eating its margin (so the
    case above shows the balance doing it)
  - pause: P2 stops a second at frame 900 (the Esc screen): P1 waits it
    out, then holds back until even; neither ahead at the end
Exit 1 when one fails."""

import argparse
import os
import re
import socket
import subprocess
import sys
import time

LINE = re.compile(
    r'pace: player (\d+) P(\d) at ([\d.]+) Hz: (\d+) frames in ([\d.]+) s '
    r'\(([\d.]+) a s\); (\d+) waits \((\d+) after frame 600\), ([\d.]+) ms, '
    r'longest ([\d.]+) ms; ahead (-?[\d.]+) at the end, (-?[\d.]+) at most; '
    r'held back (-?[\d.]+) frames; margins (-?[\d.]+), rival (-?[\d.]+)')

FAST = '60.0425'                # 0.2% over the game's 59.9227


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def players(text):
    """The pace lines: {side: dict}."""
    out = {}
    for m in LINE.finditer(text):
        g = m.groups()
        out[int(g[1])] = {
            'hz': float(g[2]), 'frames': int(g[3]), 'waits': int(g[6]),
            'late': int(g[7]), 'longest': float(g[9]), 'end': float(g[10]),
            'most': float(g[11]), 'held': float(g[12]),
            'margin': float(g[13]), 'rival': float(g[14])}
    return out


def judge(name, p):
    """What broke in a case: a list of words."""
    bad = []
    if set(p) != {1, 2}:
        return ['no pace lines for both players']
    p1, p2 = p[1], p[2]
    if name == 'even':
        for side in (1, 2):
            if p[side]['late'] != 0:
                bad.append('P%d waited %d times after frame 600'
                           % (side, p[side]['late']))
            if abs(p[side]['end']) > 0.75:
                bad.append('P%d %.2f ahead at the end' % (side,
                                                          p[side]['end']))
            if p[side]['held'] > 1.0:
                bad.append('P%d held back %.2f' % (side, p[side]['held']))
    elif name == 'drift':
        for side in (1, 2):
            if p[side]['late'] != 0:
                bad.append('P%d waited %d times after frame 600'
                           % (side, p[side]['late']))
        if p2['held'] < 2.0:
            bad.append('P2 (fast) held back only %.2f' % p2['held'])
        if p2['most'] > 1.5:
            bad.append('P2 (fast) %.2f ahead at most' % p2['most'])
        if abs(p2['end']) > 1.0:
            bad.append('P2 (fast) %.2f ahead at the end' % p2['end'])
        if p1['held'] != 0.0:
            bad.append('P1 (slow) held back %.2f' % p1['held'])
    elif name == 'drift-unbalanced':
        if p2['end'] < 3.0:
            bad.append('P2 (fast) only %.2f ahead at the end unbalanced'
                       % p2['end'])
        if p2['held'] != 0.0:
            bad.append('P2 held back %.2f with the balance off' % p2['held'])
    elif name == 'pause':
        if p1['held'] < 3.0:
            bad.append('P1 held back only %.2f after the pause' % p1['held'])
        if p1['longest'] < 800.0:
            bad.append('P1 never waited out the pause (%.0f ms longest)'
                       % p1['longest'])
        for side in (1, 2):
            if abs(p[side]['end']) > 0.75:
                bad.append('P%d %.2f ahead at the end' % (side,
                                                          p[side]['end']))
        if p2['late'] != 0:
            bad.append('P2 waited %d times after frame 600' % p2['late'])
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('server')
    ap.add_argument('netbot')
    a = ap.parse_args()
    cases = [                   # name, hz_netbot's arguments, env
        ('even', '--hz 59.9227 59.9227', {}),
        ('drift', '--hz 59.9227 ' + FAST, {}),
        ('drift-unbalanced', '--hz 59.9227 ' + FAST,
         {'HZ_NET_BALANCE': '0'}),
        ('pause', '--hz 59.9227 59.9227 --pause 1 900 1000', {}),
    ]
    servers = []
    runs = []
    try:
        for name, args, extra in cases:
            port = free_udp_port()
            servers.append(subprocess.Popen(
                [a.server, '--port', str(port), '--quiet'],
                stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT))
            time.sleep(0.2)
            env = dict(os.environ, HZ_NET_LAG='20', HZ_NET_JITTER='4',
                       **extra)
            env.pop('HZ_NET_LOSS', None)
            runs.append((name, subprocess.Popen(
                [a.netbot, '127.0.0.1', str(port), '1', '2400', '--pace']
                + args.split(), env=env, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True)))
        ok = True
        for name, proc in runs:
            try:
                text, _ = proc.communicate(timeout=180)
            except subprocess.TimeoutExpired:
                proc.kill()
                text, _ = proc.communicate()
            p = players(text)
            bad = judge(name, p)
            if proc.returncode != 0:
                bad.insert(0, 'hz_netbot exit %d' % proc.returncode)
            ok = ok and not bad
            print('%-17s %s' % (name, 'PASS' if not bad else 'FAIL'))
            for side in sorted(p):
                q = p[side]
                print('    P%d at %.4f Hz: %d waits (%d after frame 600, '
                      'longest %.0f ms); ahead %.2f at the end, %.2f at '
                      'most; held back %.2f; margins %.2f / %.2f' % (
                          side, q['hz'], q['waits'], q['late'],
                          q['longest'], q['end'], q['most'], q['held'],
                          q['margin'], q['rival']))
            for b in bad:
                print('    ' + b)
            if bad and not p:
                print('    ' + text.strip().replace('\n', '\n    '))
    finally:
        for s in servers:
            s.kill()
            s.wait()
    print('netpace: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
