#!/usr/bin/env python3
"""The network game end to end (session 56, docs/netplay.md): two headless
games through a server of their own, their state the same every second.

    hznetplay_check.py HZ_SERVER HZ_HEADLESS ASSETS [--netai HZ_NETAI]
                       [--replay HZ_REPLAY] [--keep DIR]

Starts HZ_SERVER on a free UDP port on this machine, then for each case
two HZ_HEADLESS (main.c's loop, no window): one `--net host`, the other
`--net quick` a moment later, so the second finds the first waiting.
Each player's pad is a random player's (a chord of a direction and A, B,
C held a while, then the next; Start now and then, which pauses the game
for both), from its own seed. The network game runs until the host
reaches HZ_NET_FRAMES (the other's is a second more) and quits; the other
then hears its rival left. Each
writes HZ_NETLOG: the state hash every 60 frames (H FRAME HASH) and how
it ended (E FRAME WHY). The two logs' hashes must agree as far as both
go, which must be most of the run.

The cases: a clean line on map 3; 20% of the datagrams lost each way; and
a joiner whose own PATCHES are all off (settings.json), so the host's
must be the ones both play. (Session 59) own-view: the host's VIEW OWN
SIDE ONLY (--view own), each side its own screen, the hashes (the game
without its display) alike; server-rule: a second server with --view own
and a host who asked for the split screen, played on own screens.
(Session 66) esc-open: the host opens the Esc screen for ten seconds
(HZ_NET_ESC; no pad there, so nothing works the menu): the game must go
on under it, both sides in step (its HZ_NETLOG's M lines: shut 600
network frames after open, not at the same frame, as when the menu held
the game), the hashes alike.
(Session 66) Games played to their end (HZ_NET_END: an HQ at 0 HP at a
set frame on both machines): rematch, P1's HQ down and both asking for a
rematch (HZ_NET_REMATCH, HZ_NET_GAMES=2): two games in the one room, P2
the winner of each on both machines (HZ_NETLOG's R lines), the hashes
alike through both and their ends (each machine shows its own, YOU WIN
or YOU LOSE: the hash leaves the display out from the game's end), the
second on a new seed; draw, both HQs down at once: DRAW on both. With
--netai, ai-rematch: a person against the server's AI on a server of its
own, P1's HQ down, the person asking for a rematch, which the AI always
does: two games, the AI the winner of both, the person's hashes and the
AI's alike.

(Session 58) With --netai, the server's nightmare AI too (the server's
--ai-after 2): a person alone gets it (server-ai: the person's hashes and
the AI's alike, the person told it is the server's AI); two people a
moment apart play each other, never the AI (people-first); a person with
QUICK MATCH people only waits past the AI's time and then plays the next
person (people-only); --net nightmare plays the AI here until a person
hosts, and then the network game (nightmare-here). (Session 67) In
server-ai the AI's own recording of the game too (HZ_SAVES, kind
"netai"): written, and with --replay played again as the game it was
(hz_replay --check). Exit 1 on any difference."""

import argparse
import json
import os
import random
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def pad_script(seed, steps=480, start=True):
    """HZ_PAD1's "N:BUTTONS,...": chords held 1-40 frames (a tenth of
    them 60-250), Start alone 1 in 60 (`start`). (Session 68) The games
    played to their end have none: Start pauses, and a pause after the
    script's last Start is never lifted -- the HQ down at HZ_NET_END and
    the game held for good. Where a rematch's game falls in the script
    (counted in frames shown, the wait between the games too) is the
    clock's: some runs failed so, others not."""
    r = random.Random(seed)
    dirs = ['', 'U', 'D', 'L', 'R', 'UL', 'UR', 'DL', 'DR']
    out, frame = [], 0
    for _ in range(steps):
        if r.randrange(60) == 0 and start:
            chord = 'S'
        else:
            chord = r.choice(dirs) + ''.join(b for b in 'ABC'
                                             if r.randrange(4) == 0)
        out.append('%d:%s' % (frame, chord or '-'))
        frame += (60 + r.randrange(191)) if r.randrange(10) == 0 \
            else 1 + r.randrange(40)
    return ','.join(out)


def read_log(path):
    hashes, end = [], None
    if os.path.exists(path):
        for line in open(path):
            part = line.split()
            if part and part[0] == 'H':
                hashes.append((int(part[1]), part[2]))
            elif part and part[0] == 'E':
                end = (int(part[1]), ' '.join(part[2:]))
    return hashes, end


def read_marks(path):
    """(Session 66) The Esc screen's lines: [(FRAME, 'open'|'shut')]."""
    marks = []
    if os.path.exists(path):
        for line in open(path):
            part = line.split()
            if len(part) == 3 and part[0] == 'M':
                marks.append((int(part[1]), part[2]))
    return marks


def run_case(name, server, headless, assets, port, work, frames, seed,
             loss=0.0, joiner_patches_off=False, net_map=None,
             host_view=None, expect_own=False, host_esc=None):
    d = os.path.join(work, name)
    shutil.rmtree(d, ignore_errors=True)
    procs, logs = [], []
    for k, (role, who) in enumerate((('host', 'ALPHA'), ('quick', 'BRAVO'))):
        saves = os.path.join(d, who.lower())
        os.makedirs(saves)
        if k == 1 and joiner_patches_off:
            with open(os.path.join(saves, 'settings.json'), 'w') as f:
                json.dump({'patches': {key: False for key in (
                    'supply', 'jet_guns', 'unit_aim', 'fire_rate',
                    'guard_chase', 'cpu_hits')}}, f)
        log = os.path.join(d, who.lower() + '.log')
        env = dict(os.environ, HZ_SAVES=saves, HZ_FRAMES='100000000',
                   HZ_NET_FRAMES=str(frames + 60 * k),
                   HZ_OUT=os.path.join(d, who.lower() + '.png'),
                   HZ_NETLOG=log, HZ_PAD1=pad_script(seed * 2 + k),
                   HZ_NET_LOSS=str(loss), HZ_MODS='')
        if k == 0 and host_esc is not None:     # (session 66)
            env['HZ_NET_ESC'] = '%d:%d' % host_esc
            del env['HZ_PAD1']  # a random pad would work the menu
        args = [headless, '--assets', assets, '--server',
                '127.0.0.1:%d' % port, '--net', role, '--name', who]
        if net_map is not None and role == 'host':
            args += ['--map', str(net_map)]
        if host_view is not None and role == 'host':
            args += ['--view', host_view]
        out = open(os.path.join(d, who.lower() + '.out'), 'w')
        procs.append(subprocess.Popen(args, env=env, stdout=out,
                                      stderr=subprocess.STDOUT))
        logs.append(log)
        if k == 0:
            time.sleep(0.5)     # the host waiting first
    t0 = time.time()
    for p in procs:
        try:
            p.wait(timeout=max(1.0, 300 - (time.time() - t0)))
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
    secs = time.time() - t0
    (ha, ea), (hb, eb) = read_log(logs[0]), read_log(logs[1])
    n = min(len(ha), len(hb))
    differ = next((i for i in range(n) if ha[i] != hb[i]), None)
    ok = differ is None and n * 60 >= frames // 2 and ea is not None \
        and eb is not None
    # (session 59) the view both played
    own = [any('its own screen' in l for l in open(
        os.path.join(d, who + '.out')).read().splitlines()
        if 'network: playing' in l) for who in ('alpha', 'bravo')]
    if host_esc is not None:                    # (session 66)
        at, span = host_esc
        marks = read_marks(logs[0])
        want = [(at, 'open'), (at + span, 'shut')]
        if marks != want:
            print('  the Esc screen: %s, wanted %s' % (marks, want))
            ok = False
    if own != [expect_own, expect_own]:
        print('  the view: own screens %s, wanted %s' % (own, expect_own))
        ok = False
    print('%-14s %s: %d hashes alike (to frame %d), ends A %s, B %s; '
          '%.1f s' % (name, 'ok' if ok else 'FAILED', n,
                      ha[n - 1][0] if n else 0, ea, eb, secs))
    if differ is not None:
        print('  first difference at frame %d: %s against %s'
              % (ha[differ][0], ha[differ][1], hb[differ][1]))
    if not ok:
        for who in ('alpha', 'bravo'):
            text = open(os.path.join(d, who + '.out')).read()
            lines = [l for l in text.splitlines() if 'network' in l]
            print('  %s: %s' % (who, '; '.join(lines[-4:])))
    return ok


def read_results(path):
    """(Session 66) HZ_NETLOG's R lines: who won each game ended."""
    out = []
    if os.path.exists(path):
        for line in open(path):
            part = line.split()
            if len(part) == 3 and part[0] == 'R':
                out.append(part[2])
    return out


def end_case(name, headless, assets, port, work, seed, end, games,
             want, rematch=True):
    """(Session 66) Two people, each game ended by HZ_NET_END=end: `games`
    games (a rematch asked after each), `want` the R lines on both."""
    d = os.path.join(work, name)
    shutil.rmtree(d, ignore_errors=True)
    procs, logs = [], []
    for k, (role, who) in enumerate((('host', 'ALPHA'), ('quick', 'BRAVO'))):
        saves = os.path.join(d, who.lower())
        os.makedirs(saves)
        log = os.path.join(d, who.lower() + '.log')
        env = dict(os.environ, HZ_SAVES=saves, HZ_FRAMES='100000000',
                   HZ_NETLOG=log,
                   HZ_PAD1=pad_script(seed * 2 + k, start=False),
                   HZ_NET_END=end, HZ_NET_GAMES=str(games), HZ_MODS='',
                   HZ_OUT=os.path.join(d, who.lower() + '.png'))
        if rematch:
            env['HZ_NET_REMATCH'] = '1'
        args = [headless, '--assets', assets, '--server',
                '127.0.0.1:%d' % port, '--net', role, '--name', who]
        out = open(os.path.join(d, who.lower() + '.out'), 'w')
        procs.append(subprocess.Popen(args, env=env, stdout=out,
                                      stderr=subprocess.STDOUT))
        logs.append(log)
        if k == 0:
            time.sleep(0.5)
    t0 = time.time()
    finish(procs, 240)
    (ha, _), (hb, _) = read_log(logs[0]), read_log(logs[1])
    ra, rb = read_results(logs[0]), read_results(logs[1])
    played = [sum(1 for l in said(d, who) if 'network: playing' in l)
              for who in ('ALPHA', 'BRAVO')]
    ok = ha == hb and len(ha) > 0 and ra == want and rb == want \
        and played == [games, games]
    print('%-14s %s: %d games each, won %s / %s (wanted %s), %d hashes '
          '%s; %.1f s' % (name, 'ok' if ok else 'FAILED', played[0],
                          ' '.join(ra) or '-', ' '.join(rb) or '-',
                          ' '.join(want), len(ha),
                          'alike' if ha == hb else 'NOT alike',
                          time.time() - t0))
    if not ok:
        for who in ('ALPHA', 'BRAVO'):
            print('  %s: %s' % (who, '; '.join(said(d, who)[-4:])))
    return ok


AI_AFTER = 2


def headless(work, who, headless_bin, assets, port, frames, args,
             settings=None, seed=0):
    """One headless player, started; its folder work/who."""
    saves = os.path.join(work, who.lower())
    os.makedirs(saves, exist_ok=True)
    if settings is not None:
        with open(os.path.join(saves, 'settings.json'), 'w') as f:
            json.dump(settings, f)
    env = dict(os.environ, HZ_SAVES=saves, HZ_FRAMES='100000000',
               HZ_NET_FRAMES=str(frames), HZ_MODS='',
               HZ_NETLOG=os.path.join(work, who.lower() + '.log'),
               HZ_PAD1=pad_script(seed))
    out = open(os.path.join(work, who.lower() + '.out'), 'w')
    return subprocess.Popen([headless_bin, '--assets', assets, '--server',
                             '127.0.0.1:%d' % port, '--name', who] + args,
                            env=env, stdout=out, stderr=subprocess.STDOUT)


def said(work, who):
    """The lines a player wrote about the network."""
    path = os.path.join(work, who.lower() + '.out')
    return [l.strip() for l in open(path) if l.startswith('network')]


def finish(procs, secs=120):
    t0 = time.time()
    for p in procs:
        try:
            p.wait(timeout=max(1.0, secs - (time.time() - t0)))
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()


def ai_cases(a, port, work):
    """(Session 58) The server's nightmare AI: hz_netai on the server's
    port, then the four cases. A list of (name, ok, detail)."""
    results = []
    # server-ai: a person alone, against the AI, hashes alike
    d = os.path.join(work, 'server-ai')
    os.makedirs(d, exist_ok=True)
    botlog = os.path.join(d, 'ai.log')
    bot = subprocess.Popen([a.netai, a.assets, '--server',
                            '127.0.0.1:%d' % port],
                           env=dict(os.environ, HZ_NETLOG=botlog,
                                    HZ_SAVES=os.path.join(d, 'ai')),
                           stdout=open(os.path.join(d, 'ai.out'), 'w'),
                           stderr=subprocess.STDOUT)
    time.sleep(1.0)
    p = headless(d, 'SOLO', a.headless, a.assets, port, 3000,
                 ['--net', 'quick'], seed=11)
    finish([p])
    time.sleep(1.0)
    hp, _ = read_log(os.path.join(d, 'solo.log'))
    hb, _ = read_log(botlog)
    n = min(len(hp), len(hb))
    lines = said(d, 'SOLO')
    ok = n >= 40 and hp[:n] == hb[:n] \
        and any("(the server's AI)" in l for l in lines)
    # (session 67) the AI's recording of it, played again
    rec_dir = os.path.join(d, 'ai', 'replays')
    recs = sorted(f for f in os.listdir(rec_dir) if f.endswith('.hzr')) \
        if os.path.isdir(rec_dir) else []
    rec = 'no recording'
    if len(recs) == 1 and '-NETAI-' in recs[0]:
        rec = recs[0]
        if a.replay:
            r = subprocess.run([a.replay, a.assets,
                                os.path.join(rec_dir, recs[0]), '--check'],
                               capture_output=True, text=True)
            rec += ' played again: %s' % ('the game' if r.returncode == 0
                                         else 'NOT the game')
            ok = ok and r.returncode == 0
    else:
        ok = False
    results.append(('server-ai', ok, '%d hashes alike; %s; %s' % (
        n if hp[:n] == hb[:n] else -1, lines[0] if lines else 'no game',
        rec)))
    # people-first: two quick matches a moment apart, the AI waiting
    d = os.path.join(work, 'people-first')
    p1 = headless(d, 'ANNA', a.headless, a.assets, port, 300,
                  ['--net', 'quick'], seed=12)
    time.sleep(0.7)
    p2 = headless(d, 'BERT', a.headless, a.assets, port, 360,
                  ['--net', 'quick'], seed=13)
    finish([p1, p2])
    la, lb = said(d, 'ANNA'), said(d, 'BERT')
    ok = any('playing BERT' in l for l in la) \
        and any('playing ANNA' in l for l in lb)
    results.append(('people-first', ok, '%s / %s' % (
        la[0] if la else '-', lb[0] if lb else '-')))
    # people-only: past the AI's time alone, then a person
    d = os.path.join(work, 'people-only')
    p1 = headless(d, 'CARL', a.headless, a.assets, port, 300,
                  ['--net', 'quick'], settings={'net': {'quick_ai': False}},
                  seed=14)
    time.sleep(AI_AFTER + 2.5)
    alone = said(d, 'CARL')
    p2 = headless(d, 'DORA', a.headless, a.assets, port, 360,
                  ['--net', 'quick'], seed=15)
    finish([p1, p2])
    lc = said(d, 'CARL')
    ok = not any('playing' in l for l in alone) \
        and any('playing DORA' in l for l in lc)
    results.append(('people-only', ok, 'alone %.1f s: %s; then %s' % (
        AI_AFTER + 2.5, 'no game' if not alone else alone[0],
        lc[0] if lc else '-')))
    # nightmare-here: the AI here, then a person hosts
    d = os.path.join(work, 'nightmare-here')
    p1 = headless(d, 'EMMA', a.headless, a.assets, port, 300,
                  ['--net', 'nightmare'], seed=16)
    time.sleep(AI_AFTER + 2.5)
    before = said(d, 'EMMA')
    p2 = headless(d, 'FRED', a.headless, a.assets, port, 360,
                  ['--net', 'host'], seed=17)
    finish([p1, p2])
    le = said(d, 'EMMA')
    ok = any('nightmare AI here' in l for l in before) \
        and not any('playing' in l for l in before) \
        and any('playing FRED' in l for l in le)
    results.append(('nightmare-here', ok, '; '.join(le[:2])))
    bot.terminate()
    bot.wait()
    # (session 66) ai-rematch: on a server of its own, its AI ending each
    # game as the person's does (HZ_NET_END), the person asking for a
    # rematch
    d = os.path.join(work, 'ai-rematch')
    os.makedirs(d, exist_ok=True)
    port2 = free_udp_port()
    server2 = subprocess.Popen([a.server, '--port', str(port2), '--quiet',
                                '--ai-after', str(AI_AFTER)],
                               stdout=open(os.path.join(d, 'server.log'),
                                           'w'),
                               stderr=subprocess.STDOUT)
    time.sleep(0.3)
    botlog = os.path.join(d, 'ai.log')
    bot = subprocess.Popen([a.netai, a.assets, '--server',
                            '127.0.0.1:%d' % port2],
                           env=dict(os.environ, HZ_NETLOG=botlog,
                                    HZ_NET_END='1200:1',
                                    HZ_SAVES=os.path.join(d, 'ai')),
                           stdout=open(os.path.join(d, 'ai.out'), 'w'),
                           stderr=subprocess.STDOUT)
    time.sleep(1.0)
    saves = os.path.join(d, 'gina')
    os.makedirs(saves, exist_ok=True)
    p = subprocess.Popen([a.headless, '--assets', a.assets, '--server',
                          '127.0.0.1:%d' % port2, '--name', 'GINA', '--net',
                          'quick'],
                         env=dict(os.environ, HZ_SAVES=saves,
                                  HZ_FRAMES='100000000', HZ_MODS='',
                                  HZ_NETLOG=os.path.join(d, 'gina.log'),
                                  HZ_PAD1=pad_script(18, start=False),
                                  HZ_NET_END='1200:1', HZ_NET_REMATCH='1',
                                  HZ_NET_GAMES='2'),
                         stdout=open(os.path.join(d, 'gina.out'), 'w'),
                         stderr=subprocess.STDOUT)
    finish([p], 240)
    time.sleep(1.0)
    bot.terminate()
    bot.wait()
    server2.terminate()
    server2.wait()
    hp, _ = read_log(os.path.join(d, 'gina.log'))
    hb, _ = read_log(botlog)
    rp = read_results(os.path.join(d, 'gina.log'))
    rb = read_results(botlog)
    lines = said(d, 'GINA')
    games = sum(1 for l in lines if "playing NIGHTMARE AI (the server's AI)"
                in l)
    ok = games == 2 and rp == ['P2', 'P2'] and rb == rp and hp == hb \
        and len(hp) > 0
    results.append(('ai-rematch', ok, '%d games against the AI, won %s; '
                    '%d hashes %s' % (games, ' '.join(rp) or '-', len(hp),
                                      'alike' if hp == hb else 'NOT alike')))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('server')
    ap.add_argument('headless')
    ap.add_argument('assets')
    ap.add_argument('--keep', help='keep the cases\' folders here')
    ap.add_argument('--netai', help='the server\'s AI: its cases too')
    ap.add_argument('--replay', help='hz_replay: the AI\'s recording '
                    'played again (session 67)')
    a = ap.parse_args()
    work = a.keep or tempfile.mkdtemp(prefix='hznetplay_')
    os.makedirs(work, exist_ok=True)
    port = free_udp_port()
    server = subprocess.Popen([a.server, '--port', str(port), '--quiet',
                               '--ai-after', str(AI_AFTER)],
                              stdout=open(os.path.join(work, 'server.log'),
                                          'w'),
                              stderr=subprocess.STDOUT)
    time.sleep(0.3)
    try:
        results = [
            run_case('clean', a.server, a.headless, a.assets, port, work,
                     frames=6000, seed=1, net_map=3),
            run_case('loss-20', a.server, a.headless, a.assets, port, work,
                     frames=4000, seed=2, loss=0.2),
            run_case('host-patches', a.server, a.headless, a.assets, port,
                     work, frames=4000, seed=3, joiner_patches_off=True,
                     net_map=6),
            run_case('own-view', a.server, a.headless, a.assets, port,
                     work, frames=6000, seed=4, net_map=5, host_view='own',
                     expect_own=True),
            run_case('esc-open', a.server, a.headless, a.assets, port,
                     work, frames=3000, seed=6, net_map=2,
                     host_esc=(900, 600)),
            # (session 66) games to their end
            end_case('rematch', a.headless, a.assets, port, work, seed=7,
                     end='1200:1', games=2, want=['P2', 'P2']),
            end_case('draw', a.headless, a.assets, port, work, seed=8,
                     end='1200:3', games=1, want=['DRAW'], rematch=False),
        ]
        # (session 59) a server ruling own screens, the host asking for
        # the split
        port2 = free_udp_port()
        ruled = subprocess.Popen([a.server, '--port', str(port2), '--quiet',
                                  '--view', 'own'],
                                 stdout=open(os.path.join(work,
                                                          'server-own.log'),
                                             'w'),
                                 stderr=subprocess.STDOUT)
        time.sleep(0.3)
        try:
            results.append(run_case('server-rule', a.server, a.headless,
                                    a.assets, port2, work, frames=3000,
                                    seed=5, net_map=1, host_view='split',
                                    expect_own=True))
        finally:
            ruled.terminate()
            ruled.wait()
        if a.netai:
            for name, ok, detail in ai_cases(a, port, work):
                print('%-14s %s: %s' % (name, 'ok' if ok else 'FAILED',
                                         detail))
                results.append(ok)
    finally:
        server.terminate()
        server.wait()
    if not a.keep:
        shutil.rmtree(work, ignore_errors=True)
    ok = all(results)
    print('netplay_check: %s' % ('all alike' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
