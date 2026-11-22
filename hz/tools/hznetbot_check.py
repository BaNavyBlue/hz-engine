#!/usr/bin/env python3
"""The server and the lockstep without the game (session 56,
docs/netplay.md): tools/hz_netbot against a server of its own.

    hznetbot_check.py HZ_SERVER HZ_NETBOT

Starts HZ_SERVER on a free UDP port on this machine, then hz_netbot's
cases, each must pass (every pad checked as it arrives, none wrong, every
game finished):
  - 4 pairs, 600 frames at 60 a second
  - 4 pairs, 1200 frames, 30% of the datagrams lost each way
  - a split from frame 600 on one side, which both sides must catch
  - the same with 30% lost
  - 8 pairs picking rooms off the list (HOST, then JOIN from LIST), 30%
    lost
  - 200 pairs at once, 600 frames at 60 a second: the server's CPU time
    for it reported
Exit 1 when one fails."""

import argparse
import os
import socket
import subprocess
import sys
import time


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def cpu_seconds(pid):
    """The process's user and system time so far (Linux), or None."""
    try:
        fields = open('/proc/%d/stat' % pid).read().rsplit(')', 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
    except (OSError, IndexError, ValueError):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('server')
    ap.add_argument('netbot')
    a = ap.parse_args()
    port = free_udp_port()
    server = subprocess.Popen([a.server, '--port', str(port), '--players',
                               '1024', '--quiet'],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.STDOUT)
    time.sleep(0.3)
    cases = [                   # name, hz_netbot's arguments, loss
        ('plain', '4 600', 0.0),
        ('loss-30', '4 1200 --fast', 0.3),
        ('split', '4 1200 --fast --split 1 600', 0.0),
        ('split-loss', '4 1200 --fast --split 0 300', 0.3),
        ('list-loss', '8 600 --fast --list', 0.3),
        ('200-games', '200 600', 0.0),
    ]
    ok = True
    try:
        for name, args, loss in cases:
            env = dict(os.environ, HZ_NET_LOSS=str(loss))
            before = cpu_seconds(server.pid)
            t0 = time.time()
            r = subprocess.run([a.netbot, '127.0.0.1', str(port)]
                               + args.split(), env=env, capture_output=True,
                               text=True, timeout=600)
            wall = time.time() - t0
            after = cpu_seconds(server.pid)
            line = (r.stdout.strip() or r.stderr.strip()).replace(
                'hz_netbot: ', '')
            cpu = ''
            if before is not None and after is not None:
                cpu = '; server CPU %.0f%% of a core' % (
                    100 * (after - before) / wall)
            print('%-11s %s: %s%s' % (name, 'ok' if r.returncode == 0
                                      else 'FAILED', line, cpu))
            ok = ok and r.returncode == 0
            time.sleep(0.5)     # the last case's players gone
    finally:
        server.terminate()
        server.wait()
    print('netbot_check: %s' % ('all passed' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
