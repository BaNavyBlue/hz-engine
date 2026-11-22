#!/usr/bin/env python3
"""The game's end and the endings (src/game/game_end.c, ending.c) against
the original MenuModule_GameEnd.

    hzgameend_check.py ROM SYM HZ_GAMEEND ASSETS [--runs N] [--seed S]
                       [--keep DIR] [--coverage] [--stop] [--endings]

Each run is a script (made up here, below): the state Game_Over leaves
(the region, v_options, the win record, the winner, the frame counter,
the bases, units and bodies counted) with VRAM full of pseudo-random bytes
as the game might leave it, and a pad byte per frame. tools/hz_gameend runs
the port on it; this runs the original from Menu_GameEndFlow (M_Init, then
GameEnd_Run) on tools/m68krun.py with the title check's model of the VDP
and of the menu module's waits, pads and sound commands (hzfront_check.Cpu),
and compares frame by frame: all of VRAM (by CRC; the first differing
address is found by dumping the port's), CRAM, VSRAM and the registers as
each frame shows them, every sound command and its frame, and at the end
v_options and the win record.

The records are made so that many runs reach the endings: every map won
on every difficulty already, or all but the map and difficulty just won;
others have A-C complete but not D (no endings), or A but not B (the
results' 30 s wait), and the frame counter at 99 hours exactly or near.
--endings (the mutation tests') makes nearly every run reach them, and
the results short.

Two patches on the original's side, for what the port does differently on
purpose:
  - CHANGED: Results_RecordWin makes and shows the password (the port saves
    the win instead): the original skips from its first password step to
    its rts (.complete), the record already updated.
  - not ported (nothing shows it): Winner_Show copies 8 bytes from past the
    WINNER font's RLE entry to VRAM $D000; the copy's writes are undone.
The port's other CHANGED points need none: the busy wait in Ending_Run is
no frame on either side, and the story's row clear (EndingBlankLine) is
blank in the shipping data.
"""

import os
import random
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import m68k                                             # noqa: E402
import m68krun                                          # noqa: E402
import hzfront_check                                    # noqa: E402
from hzfront_check import (Stop, first_vram_diff,       # noqa: E402
                           describe_frame, PAD_A, PAD_B, PAD_C, PAD_START,
                           PAD_UP)

V_HW_VERSION = 0xF840
V_OPTIONS = 0xF84A
V_WIN_RECORD = 0xF84E
V_WINNER = 0xF912
V_UNITS_BUILT = 0xF914
V_RESULTS_BASES = 0xF918
V_MECH_SPAWNS = 0xF91C
V_FRAME_COUNTER = 0xF920


def xorshift_vram(seed):
    x = seed or 1
    out = bytearray(0x10000)
    for i in range(0x10000):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] = x & 0xFF
    return out


class Cpu(hzfront_check.Cpu):
    """The title check's interpreter, with the two patches above."""

    def __init__(self, rom, sym, script):
        super().__init__(rom, sym, script)
        self.copy_start = sym['Winner_Show.copy']
        self.copy_end = self.copy_start + 8
        if m68k.text(m68k.decode(rom, self.copy_end)).split()[0] \
                != 'movea.l':
            raise SystemExit('Winner_Show.copy: not the loop expected')
        self.snapshot = None
        pc = sym['Results_RecordWin']
        while True:
            insn = m68k.decode(rom, pc)
            if insn.mn == 'addq.b':
                break
            pc += insn.size
        self.hooks[pc] = self.skip_password
        self.complete = sym['Results_RecordWin.complete']
        self.expect = None
        self.mismatch = None

    def skip_password(self):
        self.pc = self.complete

    def frame(self):
        """The title check's frame, and with `expect` (the port's frames)
        the run stops at the first that differs, VRAM kept for the report."""
        k = len(self.frames)
        try:
            super().frame()
        finally:
            exp = self.expect
            if exp is not None and self.mismatch is None and (
                    k >= len(exp) or exp[k] != self.frames[k]):
                self.mismatch = k
                self.dump = bytes(self.vdp.vram)
                raise Stop()

    def step(self):
        if self.pc == self.copy_start and self.snapshot is None:
            self.snapshot = bytes(self.vdp.vram)
        elif self.pc == self.copy_end and self.snapshot is not None:
            v = self.vdp.vram
            for a in list(range(0, 0xB000)) + list(range(0xD000, 0x10000)):
                v[a] = self.snapshot[a]
            self.snapshot = None
        super().step()


def run_original(rom, sym, script, seen=None, expect=None):
    cpu = Cpu(rom, sym, script)
    cpu.seen = seen
    cpu.expect = expect
    r = cpu.ram
    s = script['state']
    r[V_HW_VERSION] = 0
    r[V_HW_VERSION + 1] = s['hw']
    r[V_OPTIONS:V_OPTIONS + 4] = bytes(s['options'])
    r[V_WIN_RECORD:V_WIN_RECORD + 4] = bytes(s['win'])
    r[V_WINNER:V_WINNER + 2] = s['winner'].to_bytes(2, 'big')
    r[V_FRAME_COUNTER:V_FRAME_COUNTER + 4] = s['frames'].to_bytes(4, 'big')
    r[V_RESULTS_BASES:V_RESULTS_BASES + 4] = bytes(s['bases'])
    for i in range(2):
        r[V_UNITS_BUILT + 2 * i:V_UNITS_BUILT + 2 * i + 2] = \
            s['units'][i].to_bytes(2, 'big')
        r[V_MECH_SPAWNS + 2 * i:V_MECH_SPAWNS + 2 * i + 2] = \
            s['spawns'][i].to_bytes(2, 'big')
    r[hzfront_check.V_SFX_QUEUED] = 0xFF
    r[hzfront_check.V_SFX_PRIORITY] = 0xFF
    cpu.vdp.vram = xorshift_vram(s['seed'])
    end = None
    try:
        cpu.call(sym['Menu_GameEndFlow'], limit=2_000_000_000)
        end = ' '.join(['%02X' % b for b in r[V_OPTIONS:V_OPTIONS + 4]]
                       + ['%02X' % b for b in r[V_WIN_RECORD:V_WIN_RECORD + 4]])
    except Stop:
        pass
    return cpu, end


# -- scripts ---------------------------------------------------------------

def make_script(rng, endings=False):
    s = {}
    s['hw'] = rng.choice([0xA0, 0xA0, 0x20, 0x80, 0x00, 0xE0, 0xA1])
    sides = rng.choice([1, 2, 1, 2, 1, 2, 0, 3])
    s['options'] = [sides | rng.choice([0, 4]), rng.randrange(4),
                    rng.randrange(256), rng.randrange(8)]
    diff, mapn = s['options'][1], s['options'][3]
    human_won = sides in (1, 2) and rng.random() < 0.65
    if human_won:
        s['winner'] = sides - 1
    elif sides in (1, 2):
        s['winner'] = 2 - sides             # the computer won
    else:
        s['winner'] = rng.randrange(2)
    kind = rng.random() * (0.6 if endings else 1)
    if endings:                     # (as the mutation tests were run)
        if kind < 0.4:
            win = 0xFFFFFFFF & ~(1 << (8 * (3 - diff) + mapn))
        else:
            win = 0xFFFFFFFF
    elif kind < 0.35:
        win = 0xFFFFFFFF & ~(1 << (8 * (3 - diff) + mapn))
    elif kind < 0.5:
        win = 0xFFFFFFFF
    elif kind < 0.6:
        win = 0xFF000000 | rng.randrange(1 << 24)
    elif kind < 0.7:
        win = 0xFFFFFF00 | rng.randrange(0xFF)  # D alone short
    elif kind < 0.78:
        win = 0xFF00FFFF | rng.randrange(0xFF) << 16  # A, not B
    else:
        dens = rng.choice([0.1, 0.5, 0.9])
        win = sum(1 << b for b in range(32) if rng.random() < dens)
    s['win'] = [(win >> (24 - 8 * i)) & 0xFF for i in range(4)]
    times = [rng.randrange(0, 216000), rng.randrange(3_900_000, 4_000_000),
             rng.randrange(0x1464B40 - 2000, 0x1464B40 + 2000),
             rng.randrange(1 << 32)]
    if not endings:                 # 99 hours exactly, and either side
        times.append(0x1464B40 + rng.randrange(-1, 2))
    s['frames'] = rng.choice(times)
    s['bases'] = [rng.randrange(256) if rng.random() < 0.2
                  else rng.randrange(24) for _ in range(4)]
    s['units'] = [rng.choice([rng.randrange(200), rng.randrange(1 << 16)])
                  for _ in range(2)]
    s['spawns'] = [rng.choice([rng.randrange(100), rng.randrange(1 << 16)])
                   for _ in range(2)]
    s['seed'] = rng.randrange(1, 1 << 32)

    # Pads: none at all (every wait runs out), a press now and then (a
    # button, often Start), or Start held for long stretches.
    total = rng.choice([20000, 20000, rng.randrange(100, 20000)])
    style = rng.choice(['none', 'sparse', 'sparse', 'held'])
    pads = []
    if endings:
        # the results ended by a press soon after they start
        total = 20000
        style = rng.choice(['none', 'sparse', 'sparse'])
        for _ in range(10):
            pads += [0] * rng.randrange(30, 60) + [PAD_A]
    while len(pads) < total:
        if style == 'none':
            pads.append(0)
        elif style == 'sparse':
            pads.extend([0] * rng.choice([rng.randrange(20, 300),
                                          rng.randrange(300, 4000)]))
            b = PAD_START if rng.random() < 0.4 else rng.choice(
                [PAD_A, PAD_B, PAD_C, PAD_UP, rng.randrange(1, 256)])
            pads.extend([b] * rng.randrange(1, 4))
        else:
            pads.extend([0] * rng.randrange(1, 2000))
            pads.extend([PAD_START] * rng.randrange(1, 6000))
    return {'state': s, 'pads': pads[:total], 'music': []}


def write_script(path, script):
    s = script['state']
    nums = ([s['hw']] + s['options'] + s['win'] + [s['winner'], s['frames']]
            + s['bases'] + s['units'] + s['spawns'] + [s['seed']])
    with open(path, 'w') as f:
        f.write('S %s\n' % ' '.join('%X' % n for n in nums))
        for p in script['pads']:
            f.write('%X\n' % p)


def read_port(path):
    frames, sound, end = [], [], None
    for ln in open(path):
        p = ln.split(None, 2)
        if p[0] == 'F':
            frames.append(p[2].strip())
        elif p[0] == 'M':
            sound.append(p[1] + ' ' + p[2].strip())
        elif p[0] == 'E':
            end = ln[2:].strip()
    return frames, sound, end


def report_coverage(rom, sym, seen):
    """The game end's and the endings' instructions no run reached."""
    import re
    import hzasm_check
    asm = os.path.join(HERE, '..', 'asm', 'hz.asm')
    table = {'__lax__': True}
    hzasm_check.assemble(asm, table, True)
    del table['__lax__']
    addrs = []
    hzasm_check.assemble(asm, table, False, addrs)
    lo, hi = sym['GameEnd_Run'], sym['WinRecord_Scramble']
    routine = None
    missed = {}
    for i, ln in enumerate(open(asm).read().split('\n')):
        m = re.match(r'^([A-Za-z_]\w*):', ln)
        if m:
            routine = m.group(1)
            continue
        text = ln.split(';')[0].strip()
        if not text or text.startswith('dc.') or not ln.startswith('\t') \
                or text.startswith('binclude'):
            continue
        a = addrs[i]
        if lo <= a < hi and a not in seen:
            missed.setdefault(routine, []).append('$%06X %s' % (a, text))
    for r, lines in missed.items():
        print('never run in %s:' % r)
        for x in lines:
            print('   ', x)


def main():
    args = sys.argv[1:]
    opts = {'--runs': 12, '--seed': 1}
    keep = None
    coverage = '--coverage' in args
    stop = '--stop' in args
    endings = '--endings' in args
    args = [a for a in args if a not in ('--coverage', '--stop',
                                         '--endings')]
    pos = []
    i = 0
    while i < len(args):
        if args[i] in opts:
            opts[args[i]] = int(args[i + 1])
            i += 2
        elif args[i] == '--keep':
            keep = args[i + 1]
            i += 2
        else:
            pos.append(args[i])
            i += 1
    if len(pos) != 4:
        raise SystemExit(__doc__)
    rom = open(pos[0], 'rb').read()
    sym = m68krun.load_symbols(pos[1])
    hz_gameend, assets = pos[2], pos[3]
    rng = random.Random(opts['--seed'])
    seen = set() if coverage else None
    bad = 0
    stats = {'frames': 0, 'sound': 0, 'ended': 0, 'endings': 0}
    t0 = time.time()
    with tempfile.TemporaryDirectory() as tmp:
        work = keep or tmp
        os.makedirs(work, exist_ok=True)
        env = dict(os.environ, HZ_SAVES=os.path.join(tmp, 'saves'))
        for run in range(opts['--runs']):
            script = make_script(rng, endings)
            sp = os.path.join(work, 'script%d.txt' % run)
            op = os.path.join(work, 'port%d.txt' % run)
            write_script(sp, script)
            subprocess.run([hz_gameend, assets, sp, op], check=True, env=env)
            ours, our_sound, our_end = read_port(op)
            cpu, their_end = run_original(rom, sym, script, seen,
                                          ours if stop else None)
            theirs, their_sound = cpu.frames, cpu.sound
            stats['frames'] += len(theirs)
            stats['sound'] += len(their_sound)
            stats['ended'] += their_end is not None
            stats['endings'] += any(x.split()[1:3] == ['09', '00']
                                    and x.split()[3] in ('0F', '10')
                                    for x in their_sound)
            problem = None
            for k in range(min(len(ours), len(theirs))):
                if ours[k] != theirs[k]:
                    problem = 'frame %d: %s' % (k, describe_frame(
                        ours[k], theirs[k]))
                    if ours[k].split()[0] != theirs[k].split()[0]:
                        if cpu.mismatch != k:
                            script['dump'] = k
                            cpu, _ = run_original(rom, sym, script)
                        problem += '; ' + first_vram_diff(
                            hz_gameend, assets, sp, k, cpu.dump)
                    break
            if problem is None and cpu.mismatch is not None:
                problem = 'frames: port %d, original more' % len(ours)
            if problem is None and len(ours) != len(theirs):
                problem = 'frames: port %d, original %d' % (len(ours),
                                                            len(theirs))
            if problem is None and our_sound != their_sound:
                for k in range(max(len(our_sound), len(their_sound))):
                    a = our_sound[k] if k < len(our_sound) else '-'
                    b = their_sound[k] if k < len(their_sound) else '-'
                    if a != b:
                        problem = 'sound %d: port "%s", original "%s"' % (
                            k, a, b)
                        break
            if problem is None and our_end != their_end:
                problem = 'end: port "%s", original "%s"' % (our_end,
                                                             their_end)
            if problem:
                bad += 1
                print('run %d (%s): %s' % (run, sp, problem))
                if stop:
                    break
    print('gameend: %d runs, %d frames, %d sound commands, %d ended, %d '
          'reached the endings: %s (%.0f s)'
          % (opts['--runs'], stats['frames'], stats['sound'],
             stats['ended'], stats['endings'],
             'all identical' if not bad else '%d differ' % bad,
             time.time() - t0))
    if coverage:
        report_coverage(rom, sym, seen)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
