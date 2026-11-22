#!/usr/bin/env python3
"""The CPU mech's AI (src/game/mech_ai.c) against the original.

    hzmechai_check.py ROM SYM HZ_MECHAI ASSETS [--seed S] [--games N]
                      [--frames F] [--sample K] [--keep DIR] [--coverage]
                      [--max-diffs N] [--stop] [--pokes P A]

tools/hz_mechai plays real games (see that file) and writes every
AI_MechThink call's state before and after, as the 68k's RAM holds it;
this runs the original AI_MechThink from the ROM image on
tools/m68krun.py on each state (a0 the mech, a1 the other) and compares:
both mech records, the 100 unit records and the RNG seed byte for byte,
and the rest of RAM unchanged but for v_scan_counts/v_scan_list (scratch
Grid_ScanRings writes; the port keeps its scan to itself) and the stack.

The words at mech +$C0-$CF are shared by the plans (HzMechAI.work); the
port tags which use they are in. They are compared where the original
wrote them during the call, and all of them when the port's tag didn't
change.

The port's CHANGED points, as patches at the original's instructions:
  AI_DeliverHQ_FlyIn / AI_DeliverPost_FlyIn .count_down, with a direction
    held: d7 (left over) = the distance to the goal, as .head_for_goal
    measures it
  AI_Service_GoToUnit .check_hq_range: d1/d2 = the unit's x/y (the
    original loads its y over its x and leaves d2 stale)
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import m68k                                             # noqa: E402
import m68krun                                          # noqa: E402

RAM_SIZE = 0x10000
A_BLOCKS = 0x0000
A_LAYOUT = 0x5000
A_POSTS = 0x89B4
A_SCAN = 0x8A80             # v_scan_counts, then v_scan_list
A_SCAN_END = 0x8B94
A_MECH = (0x8B94, 0x8C94)
A_UNITS = 0x8D94
A_GRID = 0xB4D4
A_RADAR = 0xC4D4
A_COUNT = 0xDBFC
A_SEED = 0xF7C0
A_MAP = 0xF926
A_STACK = 0xFC00             # m68krun's stack, up to $FFFF

N_LAYOUT = 0x2000
N_POSTS = 20 * 8
N_MECHS = 0x200
N_UNITS = 100 * 0x50
N_GRID = 0x1000
N_RADAR = 0x1000

# mech_ai_buy_script (+$B0) holds a ROM address in AIBuyScripts: the
# harness writes it where the original has the table
# (src/game/tables.h's AI_BUY_SCRIPTS_ADDR); a ROM built from a changed
# source has it where its hz.sym says.
PORT_AI_BUY_SCRIPTS = 0xC6B0

WORK_NAMES = ['none', 'presses', 'box', 'hqs', 'service']


def rd16(b, a):
    return (b[a] << 8) | b[a + 1]


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


class Cpu(m68krun.M68k):
    """Records the words written in the thinking mech's +$C0-$CF."""

    def __init__(self, rom):
        super().__init__(rom)
        self.work_lo = 0
        self.written = set()

    def write(self, addr, size, value):
        a = addr & 0xFFFF
        if self.work_lo <= a < self.work_lo + 0x10 or \
                self.work_lo <= a + size - 1 < self.work_lo + 0x10:
            for k in range(size):
                w = a + k - self.work_lo
                if 0 <= w < 0x10:
                    self.written.add(w // 2)
        super().write(addr, size, value)


class Original:
    def __init__(self, rom, sym):
        self.rom = rom
        self.sym = sym
        self.entry = sym['AI_MechThink']
        self.patches = {}
        for lab in ('AI_DeliverHQ_FlyIn.count_down',
                    'AI_DeliverPost_FlyIn.count_down'):
            self.patches[self.find(lab, 'bne.s')] = self.p_count_down
        self.patches[self.find('AI_Service_GoToUnit.check_hq_range',
                               'jsr')] = self.p_hq_range
        self.hits = dict((k, 0) for k in self.patches)

    def find(self, label, mn):
        a = self.sym[label]
        for _ in range(40):
            insn = m68k.decode(self.rom, a)
            if insn.mn == mn:
                return a
            a += insn.size
        raise SystemExit('%s not found after %s' % (mn, label))

    def p_count_down(self, cpu):
        if cpu.ccr & 4:                 # Z set: no direction held
            return
        r = cpu.ram
        b = cpu.a[0] & 0xFFFF
        dx = abs(s16((rd16(r, b + 0x86) - rd16(r, b + 0x10)) & 0xFFFF))
        dy = abs(s16((rd16(r, b + 0x88) - rd16(r, b + 0x12)) & 0xFFFF))
        cpu.d[7] = (cpu.d[7] & 0xFFFF0000) | (max(dx, dy) & 0xFFFF)
        return True

    def p_hq_range(self, cpu):
        r = cpu.ram
        u = cpu.a[2] & 0xFFFF
        cpu.d[1] = (cpu.d[1] & 0xFFFF0000) | rd16(r, u + 0x10)
        cpu.d[2] = (cpu.d[2] & 0xFFFF0000) | rd16(r, u + 0x12)
        return True

    def run(self, ram, p, seen=None):
        cpu = Cpu(self.rom)
        cpu.ram = bytearray(ram)
        cpu.work_lo = A_MECH[p] + 0xC0
        patches = self.patches
        hits = self.hits

        def trace(c, insn):
            if seen is not None:
                seen.add(insn.addr)
            f = patches.get(insn.addr)
            if f and f(c):
                hits[insn.addr] += 1
        cpu.trace = trace
        cpu.a[0] = 0xFF0000 | A_MECH[p]
        cpu.a[1] = 0xFF0000 | A_MECH[1 - p]
        cpu.call(self.entry)
        return bytes(cpu.ram), cpu.written, cpu.count


def read_exact(f, n):
    b = f.read(n)
    if len(b) != n:
        raise EOFError
    return b


def relocate(ram, shift):
    """Each mech's mech_ai_buy_script moved by `shift`."""
    for base in A_MECH:
        a = base + 0xB0
        v = (int.from_bytes(ram[a:a + 4], 'big') + shift) & 0xFFFFFFFF
        ram[a:a + 4] = v.to_bytes(4, 'big')


def read_cases(f_in, f_out, shift=0):
    """Per case (p, kinds before, RAM before, kinds after, RAM after)."""
    blocks = None
    while True:
        b = f_in.read(1)
        if not b:
            return
        if b == b'H':
            read_exact(f_in, 3)
            blocks = read_exact(f_in, 0x4000)
            continue
        if b[0] == 0:
            continue
        p, kind, stage = read_exact(f_in, 3)
        ram = bytearray(RAM_SIZE)
        ram[A_BLOCKS:A_BLOCKS + 0x4000] = blocks
        ram[A_LAYOUT:A_LAYOUT + N_LAYOUT] = read_exact(f_in, N_LAYOUT)
        ram[A_POSTS:A_POSTS + N_POSTS] = read_exact(f_in, N_POSTS)
        ram[A_MECH[0]:A_MECH[0] + N_MECHS] = read_exact(f_in, N_MECHS)
        ram[A_UNITS:A_UNITS + N_UNITS] = read_exact(f_in, N_UNITS)
        ram[A_GRID:A_GRID + N_GRID] = read_exact(f_in, N_GRID)
        ram[A_RADAR:A_RADAR + N_RADAR] = read_exact(f_in, N_RADAR)
        ram[A_SEED:A_SEED + 4] = read_exact(f_in, 4)
        ram[A_COUNT:A_COUNT + 2] = read_exact(f_in, 2)
        ram[A_MAP:A_MAP + 2] = read_exact(f_in, 2)
        kind_a, stage_a = read_exact(f_out, 2)
        after = bytearray(ram)
        after[A_MECH[0]:A_MECH[0] + N_MECHS] = read_exact(f_out, N_MECHS)
        after[A_UNITS:A_UNITS + N_UNITS] = read_exact(f_out, N_UNITS)
        after[A_SEED:A_SEED + 4] = read_exact(f_out, 4)
        if shift:
            relocate(ram, shift)
            relocate(after, shift)
        yield p, (kind, stage), bytes(ram), (kind_a, stage_a), bytes(after)


MECH_NAMES = {
    0x00: 'flags', 0x03: 'dir', 0x04: 'turn_to', 0x0A: 'hp', 0x10: 'x',
    0x12: 'y', 0x28: 'pad', 0x4A: 'menu type', 0x4C: 'menu order',
    0x4F: 'prod_state', 0x54: 'money', 0x80: 'ai frames', 0x83: 'misses',
    0x85: 'tries/here', 0x86: 'goal_x', 0x88: 'goal_y', 0x8A: 'heading',
    0x8C: 'hold', 0x8D: 'buy_count', 0x8E: 'move', 0x8F: 'target',
    0xA2: 'dropped', 0xA3: 'lifted', 0xA4: 'edge_steer', 0xA5: 'buy_kind',
    0xA6: 'saved_step', 0xA7: 'saved_plan', 0xA8: 'dest', 0xAA: 'from',
    0xAC: 'next_dest', 0xAE: 'next_from', 0xB0: 'buy_script',
    0xB4: 'saved_goal', 0xB8: 'countdown', 0xB9: 'alert_post',
    0xBA: 'refuel', 0xBB: 'route', 0xBC: 'frames_left', 0xBD: 'aim_dir',
    0xBE: 'fight_form', 0xBF: 'pad_dir8', 0xD0: 'launches',
    0xD1: 'attack_buy', 0xD4: 'own_near', 0xD5: 'enemy_near',
    0xD6: 'respawn', 0xD7: 'detour', 0xD8: 'hq_hp', 0xDA: 'alert_taken',
    0xDB: 'alert', 0xDC: 'hq_attack', 0xDD: 'hq_run', 0xDE: 'engage',
    0xE0: 'engage_range', 0xE2: 'needs_service', 0xE3: 'course',
    0xE4: 'hq_drop', 0xE6: 'post_drop', 0xE8: 'bought', 0xE9: 'resets',
    0xEA: 'deaths', 0xEB: 'launch_deaths', 0xEC: 'ground_shot',
    0xED: 'watch_post', 0xEF: 'dodge', 0xF0: 'strategy', 0xF1: 'mode',
    0xF2: 'substep', 0xF3: 'plan', 0xF4: 'step', 0xF5: 'style',
    0xF6: 'dest_class', 0xF8: 'pickup_post', 0xFA: 'variant',
    0xFB: 'shot_wait', 0xFC: 'deliver_dir', 0xFE: 'launch_at_from',
    0xFF: 'launch_at_dest'}


def field_name(off):
    best = max((o for o in MECH_NAMES if o <= off), default=0)
    if off >= 0xC0 and off < 0xD0:
        return 'work +$%X' % off
    return '%s(+$%X)' % (MECH_NAMES.get(best, '?'), off)


def compare(p, before, ours, theirs, kinds_b, kinds_a, written):
    diffs = []
    for q in (0, 1):
        base = A_MECH[q]
        for off in range(0x100):
            a = base + off
            if q == p and 0xC0 <= off < 0xD0:
                w = (off - 0xC0) // 2
                if kinds_b != kinds_a and w not in written:
                    continue
            if ours[a] != theirs[a]:
                diffs.append('mech %d %s: port $%02X, original $%02X '
                             '(before $%02X)' % (q + 1, field_name(off),
                                                 ours[a], theirs[a],
                                                 before[a]))
    for i in range(100):
        a = A_UNITS + i * 0x50
        if ours[a:a + 0x50] != theirs[a:a + 0x50]:
            for k in range(0x50):
                if ours[a + k] != theirs[a + k]:
                    diffs.append('unit %d +$%X: port $%02X, original $%02X'
                                 % (i, k, ours[a + k], theirs[a + k]))
    if ours[A_SEED:A_SEED + 4] != theirs[A_SEED:A_SEED + 4]:
        diffs.append('v_rng_seed: port %s, original %s'
                     % (ours[A_SEED:A_SEED + 4].hex(),
                        theirs[A_SEED:A_SEED + 4].hex()))
    # the rest of RAM: as it was
    for lo, hi in ((0, A_SCAN), (A_SCAN_END, A_MECH[0]),
                   (A_UNITS + N_UNITS, A_SEED), (A_SEED + 4, A_STACK)):
        if before[lo:hi] != theirs[lo:hi]:
            for a in range(lo, hi):
                if before[a] != theirs[a]:
                    diffs.append('RAM $FF%04X written by the original' % a)
                    break
    return diffs


def describe(p, ram):
    b = A_MECH[p]
    return ('P%d: flags $%02X form %d plan $%X step %d mode %d substep %d '
            'strategy %d move %d alert $%02X/$%02X x %d y %d pad $%02X'
            % (p + 1, ram[b], rd16(ram, b + 0x2C), ram[b + 0xF3],
               ram[b + 0xF4], ram[b + 0xF1], ram[b + 0xF2], ram[b + 0xF0],
               ram[b + 0x8E], ram[b + 0xDA], ram[b + 0xDB],
               s16(rd16(ram, b + 0x10)), s16(rd16(ram, b + 0x12)),
               ram[b + 0x28]))


def report_coverage(rom, sym, seen):
    lo, hi = sym['AI_MechThink'], sym['AIDropOffsetsPost']
    rev = sorted((a, n) for n, a in sym.items() if lo <= a < hi)
    pc = lo
    missed = []
    while pc < hi:
        try:
            insn = m68k.decode(rom, pc)
        except Exception:
            pc += 2
            continue
        if insn.addr not in seen:
            missed.append(insn.addr)
        pc += insn.size
    # print by label, as runs
    print('%d instructions of the module never ran:' % len(missed))
    last = None
    for a in missed:
        name = max((n for n in rev if n[0] <= a), default=(lo, '?'))[1]
        if name != last:
            print('  $%06X  %s' % (a, name))
            last = name


def main():
    args = sys.argv[1:]
    if len(args) < 4:
        print(__doc__)
        return 2
    rom_path, sym_path, harness, assets = args[:4]
    opts = args[4:]
    seed, games, frames, keep, coverage, max_diffs = 1, 4, 3000, None, \
        False, 5
    sample = 1
    pokes = []
    i = 0
    while i < len(opts):
        o = opts[i]
        if o == '--seed':
            seed = int(opts[i + 1])
        elif o == '--games':
            games = int(opts[i + 1])
        elif o == '--frames':
            frames = int(opts[i + 1])
        elif o == '--pokes':
            pokes = [opts[i + 1], opts[i + 2]]
            i += 3
            continue
        elif o == '--sample':
            sample = int(opts[i + 1])
        elif o == '--keep':
            keep = opts[i + 1]
        elif o == '--max-diffs':
            max_diffs = int(opts[i + 1])
        elif o == '--coverage':
            coverage = True
            i += 1
            continue
        elif o == '--stop':
            max_diffs = 1
            i += 1
            continue
        else:
            print('unknown option %s' % o)
            return 2
        i += 2
    rom = open(rom_path, 'rb').read()
    sym = m68krun.load_symbols(sym_path)
    folder = keep or os.path.join(tempfile.gettempdir(),
                                   'hzmechai_%d' % os.getpid())
    os.makedirs(folder, exist_ok=True)
    f_in = os.path.join(folder, 'in_%d.bin' % seed)
    f_out = os.path.join(folder, 'out_%d.bin' % seed)
    subprocess.check_call([harness, assets, str(seed), str(games),
                           str(frames), str(sample), f_in, f_out] + pokes)
    orig = Original(rom, sym)
    seen = set() if coverage else None
    bad = n = insns = 0
    with open(f_in, 'rb') as fi, open(f_out, 'rb') as fo:
        shift = sym['AIBuyScripts'] - PORT_AI_BUY_SCRIPTS
        for p, kb, ram, ka, ours in read_cases(fi, fo, shift):
            try:
                theirs, written, count = orig.run(ram, p, seen)
            except m68krun.M68kError as e:
                print('case %d: the original stopped: %s' % (n, e))
                print('   ', describe(p, ram))
                bad += 1
                n += 1
                if bad >= max_diffs:
                    break
                continue
            insns += count
            diffs = compare(p, ram, ours, theirs, kb, ka, written)
            if diffs:
                bad += 1
                if bad <= max_diffs:
                    print('case %d differs (work %s/%d -> %s/%d):'
                          % (n, WORK_NAMES[kb[0] % 5], kb[1],
                             WORK_NAMES[ka[0] % 5], ka[1]))
                    print('    before', describe(p, ram))
                    print('    after ', describe(p, theirs))
                    for d in diffs[:12]:
                        print('   ', d)
                if bad >= max_diffs * (1 if max_diffs == 1 else 4):
                    break
            n += 1
    if keep is None:
        for f in (f_in, f_out):
            os.remove(f)
        os.rmdir(folder)
    print('seed %d: %d cases, %d differ; %d instructions of the original; '
          'CHANGED patches hit: %s' % (seed, n, bad, insns,
                                       ', '.join(str(v) for v in
                                                 orig.hits.values())))
    if coverage:
        report_coverage(rom, sym, seen)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
