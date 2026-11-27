#!/usr/bin/env python3
"""The title and options screen (src/game/front.c, title.c, options.c)
against the original Menu_TitleFlow.

    hzfront_check.py ROM SYM HZ_FRONT ASSETS [--runs N] [--seed S]
                     [--frames F] [--keep DIR] [--coverage] [--stop]

Each run is a script (made up here, below): the state before
MenuModule_Title (the region, v_options, the win record) and per frame
the pads and whether the title song still plays. tools/hz_front runs the port on it;
this runs the original from MenuModule_Title on tools/m68krun.py's 68000
with the order menu check's model of the VDP's ports, and compares frame
by frame: all of VRAM (by CRC; the first differing address is found by
dumping the port's), CRAM, VSRAM and the registers as each frame shows
them, every sound command and the frame it went at, and at the end the
result, v_options, the win record and the sfx queue.

CONT differs on purpose: on the cartridge it takes a password, in the port
it loads a saved game (src/game/saves.h). A run stops on both sides as
CONT is chosen (the original entering Password_Run, the port its saved
games' page), and the stop's frame is compared. A record at the map select comes the
same way on both sides instead: a script may give one, put in place each
time MapSelect_Run starts (on the original, at its first instruction),
with v_option_map its first map not won on every difficulty, as loading a
saved game leaves them. Then the loading itself:
for random records and sides, the original's Password_Check is given a
valid password for them (v_password, with any key) and run until it
waits on CORRECT, and the port loads a saved game with that record and
side (hz_front --load); the record, v_options and v_option_map they
leave must agree.

Frames: where the menus wait for VBlank (M_WaitVBlank, and with the
display on M_WaitVBlankIfOn and M_WaitVBlankEndIfOn) the original's run
stops for the frame, as the port's hands one to its host; and at the end
of each of Title_SlideLogo's 8 steps, which the 68k runs without waiting
(each takes most of a frame) and the port shows a frame each.
M_ReadJoypads reads the script's pads for the frame just gone; the
Z80's z_music_on ($A0000B, read by Title_WaitStart) is the script's for
the frames gone by. The Z80 routines are taken as what they send: the
command and its bytes (M_Snd_FlushSfx: an effect queued is sent and the
queue emptied; the mailbox is always free, no DAC sample plays).

The port's CHANGED points need no patch: the busy wait before the SEGA
logo's cycle isn't a frame on either side, and the slide's steps are
frames on both.
"""

import os
import random
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import hzlayer                                          # noqa: E402
import m68krun                                          # noqa: E402
from hzordermenu_check import Vdp                       # noqa: E402

PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT = 0x01, 0x02, 0x04, 0x08
PAD_B, PAD_C, PAD_A, PAD_START = 0x10, 0x20, 0x40, 0x80

# RAM (offsets in the 64 KB image)
V_HW_VERSION = 0xF840
V_JOYPAD1 = 0xF844
V_OPTIONS = 0xF84A
V_WIN_RECORD = 0xF84E
V_PASSWORD = 0xF852
V_SFX_PRIORITY = 0xF90A
V_SFX_QUEUED = 0xF90C
M_VDP_REG1 = 0x9885

WIN_RECORD_KEYS = [0x4865727A, 0x6F672020, 0x5A776569, 0x20686120,
                   0x47414E20, 0x6368616E, 0x73616B75, 0x6461796F]


class Stop(Exception):
    pass


class Cpu(m68krun.M68k):
    """The interpreter with the VDP's ports, the Z80's RAM (z_music_on) and
    bus request, and the menu module's waits, pad read and sound commands
    taken over (see the module's doc)."""

    def __init__(self, rom, sym, script):
        super().__init__(rom)
        self.vdp = Vdp(bytes(0x10000), [0] * 64, [0] * 40, [0] * 24)
        self.script = script
        self.frames = []
        self.sound = []
        self.pads = 0
        self.hooks = {
            sym['M_WaitVBlank']: self.wait_vblank,
            sym['M_WaitVBlankIfOn']: self.wait_vblank_if_on,
            sym['M_WaitVBlankEndIfOn']: self.wait_vblank_if_on,
            sym['M_ReadJoypads']: self.read_joypads,
            sym['M_Snd_StartSong']: self.snd_start_song,
            sym['M_Snd_PlayMusic']: self.snd_play_music,
            sym['M_Snd_FadeMusic']: self.snd_fade_music,
            sym['M_Snd_FlushSfx']: self.snd_flush_sfx,
            sym['Password_Run']: self.cont,
        }
        self.map_select = sym['MapSelect_Run']
        self.cont_frame = None
        self.slide_step = find_insn(rom, sym['Title_SlideLogo'],
                                    sym['Title_ShowTankPhoto'],
                                    'subq.w #4,d0')
        self.seen = None

    # -- memory --------------------------------------------------------------
    def read(self, addr, size):
        a = addr & 0xFFFFFF
        if 0xC00000 <= a < 0xC00020:
            raise m68krun.M68kError('VDP read at $%06X (pc $%06X)'
                                    % (a, self.pc))
        if 0xA00000 <= a < 0xA10000:
            if a == 0xA0000B:
                k = len(self.frames)
                return self.script['music'][k] if k < len(
                    self.script['music']) else 1
            return 0
        if a == 0xA11100:
            return 0                        # the bus is granted at once
        return super().read(addr, size)

    def write(self, addr, size, value):
        a = addr & 0xFFFFFF
        if 0xC00000 <= a < 0xC00020:
            words = ([(value >> 16) & 0xFFFF, value & 0xFFFF] if size == 4
                     else [value & 0xFFFF])
            for w in words:
                if a & 0x1C == 0:
                    self.vdp.data(w)
                elif a & 0x1C == 4:
                    self.vdp.control(w)
                else:
                    raise m68krun.M68kError('VDP write at $%06X' % a)
            return
        if 0xA00000 <= a < 0xA10000 or a in (0xA11100, 0xA11101):
            return
        super().write(addr, size, value)

    # -- frames and hooks ----------------------------------------------------
    def frame(self):
        v = self.vdp
        vs = b''.join(w.to_bytes(2, 'big') for w in v.vsram)
        cut = hzlayer.link5(v.vram, v.regs[5]) == 0     # (no HBlank here)
        self.frames.append('%08X %s %08X %s %08X' % (
            v.held.crc(v.vram),
            ''.join('%03X' % (c & 0x0EEE) for c in v.cram),
            zlib.crc32(vs) & 0xFFFFFFFF,
            ''.join('%02X' % r for r in v.regs),
            hzlayer.sprites_crc(v.vram, v.regs[5], cut)))
        k = len(self.frames) - 1
        if self.script.get('dump') == k:
            self.dump = bytes(v.held.vram(v.vram))
        self.pads = self.script['pads'][k] if k < len(
            self.script['pads']) else 0
        if len(self.frames) >= len(self.script['pads']):
            raise Stop()

    def cont(self):
        self.cont_frame = len(self.frames)
        raise Stop()

    def rts(self):
        self.pc = self.pop(4)

    def wait_vblank(self):
        if not self.ram[M_VDP_REG1] & 0x40:
            raise m68krun.M68kError('M_WaitVBlank with the display off: '
                                    'the 68k would hang')
        self.frame()
        self.rts()

    def wait_vblank_if_on(self):
        if self.ram[M_VDP_REG1] & 0x40:
            self.frame()
        self.rts()

    def read_joypads(self):
        self.ram[V_JOYPAD1] = 0xFF
        self.ram[V_JOYPAD1 + 1] = self.pads
        self.rts()

    def post(self, bytes_):
        self.sound.append('%d %s' % (len(self.frames),
                                     ' '.join('%02X' % b for b in bytes_)))

    def snd_start_song(self):
        self.post([0x01, self.d[0] & 0xFF])
        self.rts()

    def snd_play_music(self):
        self.post([0x09, (self.d[0] >> 8) & 0xFF, self.d[0] & 0xFF])
        self.rts()

    def snd_fade_music(self):
        self.post([0x03, self.d[0] & 0xFF, 0x00])
        self.rts()

    def snd_flush_sfx(self):
        if self.ram[V_SFX_QUEUED] != 0xFF:
            self.post([0x02, self.ram[V_SFX_QUEUED],
                       self.ram[V_SFX_PRIORITY]])
            self.ram[V_SFX_QUEUED] = 0xFF
            self.ram[V_SFX_PRIORITY] = 0xFF
        self.rts()

    def step(self):
        h = self.hooks.get(self.pc)
        if h is not None:
            self.count += 1
            h()
            return
        if self.pc == self.slide_step:
            self.frame()
        if self.pc == self.map_select and self.script.get('inject'):
            rec = self.script['inject']
            self.ram[V_WIN_RECORD:V_WIN_RECORD + 4] = bytes(rec)
            self.ram[V_OPTIONS + 3] = next(
                m for m in range(8) if not all(b >> m & 1 for b in rec))
        if self.seen is not None:
            self.seen.add(self.pc)
        super().step()


def find_insn(rom, start, end, text):
    import m68k
    pc = start
    while pc < end:
        insn = m68k.decode(rom, pc)
        if ''.join(m68k.text(insn).split()) == ''.join(text.split()):
            return pc
        pc += insn.size
    raise SystemExit('no "%s" between $%X and $%X' % (text, start, end))


def run_original(rom, sym, script, seen=None):
    cpu = Cpu(rom, sym, script)
    cpu.seen = seen
    r = cpu.ram
    s = script['state']
    r[V_HW_VERSION] = 0
    r[V_HW_VERSION + 1] = s['hw']
    r[V_OPTIONS:V_OPTIONS + 4] = bytes(s['options'])
    r[V_WIN_RECORD:V_WIN_RECORD + 4] = bytes(s['win'])
    r[V_SFX_QUEUED] = 0xFF
    r[V_SFX_PRIORITY] = 0xFF
    end = None
    try:
        cpu.call(sym['MenuModule_Title'], limit=400_000_000)
        d0 = cpu.d[0] & 0xFFFFFFFF
        res = d0 - (1 << 32) if d0 & 0x80000000 else d0
        end = ' '.join([str(res)]
                       + ['%02X' % b for b in r[V_OPTIONS:V_OPTIONS + 4]]
                       + ['%02X' % b for b in r[V_WIN_RECORD:V_WIN_RECORD + 4]]
                       + ['%02X' % r[V_SFX_QUEUED],
                          '%02X' % r[V_SFX_PRIORITY]])
    except Stop:
        pass
    return cpu, end


# -- scripts ---------------------------------------------------------------

def valid_password(rng, record):
    key = rng.randrange(16)
    k = WIN_RECORD_KEYS[(key >> 1) & 7]
    scr = [((record >> (24 - 8 * i)) ^ (k >> (24 - 8 * i))) & 0xFF
           for i in range(4)]
    d = []
    for b in scr:
        d += [b >> 4, b & 15]
    d.append(key)
    check = (~sum(d)) & 0xFF
    d += [check >> 4, check & 15]
    return d


def make_script(rng, frames):
    state = {}
    state['hw'] = 0xA0 if rng.random() < 0.7 else rng.choice([0x20, 0x21,
                                                                0xE0, 0x00])
    opts = [rng.randrange(256) if rng.random() < 0.2 else
            rng.choice([1, 2, 5, 6, rng.randrange(8)]),
            rng.randrange(256) if rng.random() < 0.3 else rng.randrange(4),
            rng.randrange(256), rng.randrange(8)]
    state['options'] = opts
    dens = rng.choice([0.0, 0.3, 0.5, 0.85, 0.97])
    win = 0
    for b in range(32):
        if rng.random() < dens:
            win |= 1 << b
    if rng.random() < 0.1:
        win = rng.choice([0, 0xFFFFFFFF, 0xFFFFFF7F, 0x80808080])
    state['win'] = [(win >> (24 - 8 * i)) & 0xFF for i in range(4)]

    # Pads: presses of 1-3 frames between gaps; Start early or late on the
    # title. Then on the options screen, again and again: an aim (down k
    # times to a menu item, then A or C), a walk of random presses
    # weighted towards moving and choosing, and often B to go back.
    pads = []
    start_at = rng.choice([rng.randrange(1, 140), rng.randrange(140, 400),
                           rng.randrange(400, 700), 10 ** 9])
    music_end = rng.choice([10 ** 9, 10 ** 9, rng.randrange(150, 600)])

    def press(b, hold=None):
        pads.extend([0] * rng.choice([1, 2, 3, 5, 8]))
        pads.extend([b] * (hold or rng.choice([1, 1, 2, 3])))

    while len(pads) < frames:
        n = len(pads)
        if n < start_at:
            if start_at - n <= 3:
                pads.extend([PAD_START] * (start_at + 3 - n))
            elif rng.random() < 0.03:
                pads += [rng.choice([PAD_A, PAD_B, PAD_C, PAD_UP])] * \
                    rng.randrange(1, 3)
            else:
                pads.append(0)
            continue
        if n < start_at + 45:
            pads.append(0)                  # the fade and the set-up
            continue
        if n == start_at + 45 and rng.random() < 0.4:
            # straight to START (the cursor starts on it), round the map
            # select and in, and with one computer side through TYPE A-D
            press(rng.choice([PAD_A, PAD_C]))
            for _ in range(2):
                for _ in range(rng.randrange(0, 9)):
                    press(rng.choice([PAD_UP, PAD_DOWN, PAD_DOWN]))
                if rng.random() < 0.15:
                    press(PAD_B)
                press(rng.choice([PAD_A, PAD_C]))
            continue
        # an aim at a menu item (from wherever the cursor is: the walk
        # below may have moved it); now and then all the way round the
        # menu, or through PLAYER: both sides (often changed) and, with
        # one computer side, the WINDOW box, back to the menu
        aim = rng.random()
        if aim < 0.15:
            for _ in range(rng.randrange(5, 8)):
                press(PAD_DOWN)
        elif aim < 0.35:
            for _ in range(rng.randrange(8)):
                press(PAD_UP)
            press(PAD_DOWN)
            press(PAD_DOWN)
            for _ in range(3):
                press(rng.choice([PAD_A, PAD_C]))
                if rng.random() < 0.5:
                    press(rng.choice([PAD_UP, PAD_DOWN]))
        else:
            for _ in range(rng.randrange(0, 6)):
                press(rng.choice([PAD_DOWN, PAD_DOWN, PAD_UP]))
        press(rng.choice([PAD_A, PAD_C, PAD_B]))
        for _ in range(rng.randrange(2, 14)):
            r = rng.random()
            if r < 0.40:
                b = rng.choice([PAD_UP, PAD_DOWN])
            elif r < 0.50:
                b = rng.choice([PAD_LEFT, PAD_RIGHT])
            elif r < 0.75:
                b = rng.choice([PAD_A, PAD_C])
            elif r < 0.92:
                b = PAD_B
            elif r < 0.96:
                b = PAD_START
            else:
                b = rng.randrange(1, 256)
            press(b)
        for _ in range(rng.choice([0, 1, 2, 3])):
            press(PAD_B)
    pads = pads[:frames]
    music = [0 if n >= music_end else 1 for n in range(frames)]
    script = {'state': state, 'pads': pads, 'music': music}
    if rng.random() < 0.6:
        # a record at the map select, as a loaded game leaves one: maps won
        # on some difficulties, some on all four, never every map
        while True:
            dens = rng.choice([0.3, 0.6, 0.85])
            rec = [sum(1 << m for m in range(8) if rng.random() < dens)
                   for _ in range(4)]
            if any(not all(b >> m & 1 for b in rec) for m in range(8)) \
                    and any(rec):
                break
        script['inject'] = rec
    return script


def write_script(path, script):
    s = script['state']
    with open(path, 'w') as f:
        f.write('S %X %s %s\n' % (
            s['hw'], ' '.join('%X' % b for b in s['options']),
            ' '.join('%X' % b for b in s['win'])))
        if script.get('inject'):
            f.write('I %s\n' % ' '.join('%X' % b for b in script['inject']))
        for p, m in zip(script['pads'], script['music']):
            f.write('%X %X\n' % (p, m))


def read_port(path):
    frames, sound, end, cont = [], [], None, None
    for ln in open(path):
        p = ln.split(None, 2)
        if p[0] == 'C':
            cont = int(p[1])
        elif p[0] == 'F':
            frames.append(p[2].strip())
        elif p[0] == 'M':
            sound.append(p[1] + ' ' + p[2].strip())
        elif p[0] == 'E':
            end = ln[2:].strip()
    return frames, sound, end, cont


def first_vram_diff(hz_front, assets, script_path, frame, theirs):
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, 'out.txt')
        dump = os.path.join(d, 'vram.bin')
        subprocess.run([hz_front, assets, script_path, out, '--dump',
                        str(frame), dump], check=True)
        ours = open(dump, 'rb').read()
    for a in range(0x10000):
        if ours[a] != theirs[a]:
            n = sum(1 for k in range(0x10000) if ours[k] != theirs[k])
            return 'VRAM $%04X: port $%02X, original $%02X (%d bytes differ)' \
                % (a, ours[a], theirs[a], n)
    return 'VRAM: no difference at the dump'


def describe_frame(ours, theirs):
    o, t = ours.split(), theirs.split()
    names = ['VRAM CRC', 'CRAM', 'VSRAM CRC', 'registers',
             'sprites as drawn']
    out = []
    for i, n in enumerate(names):
        if o[i] != t[i]:
            if n == 'CRAM':
                for c in range(64):
                    if o[i][c * 3:c * 3 + 3] != t[i][c * 3:c * 3 + 3]:
                        out.append('CRAM %d: port $%s, original $%s'
                                   % (c, o[i][c * 3:c * 3 + 3],
                                      t[i][c * 3:c * 3 + 3]))
                        break
            elif n == 'registers':
                for r in range(24):
                    if o[i][r * 2:r * 2 + 2] != t[i][r * 2:r * 2 + 2]:
                        out.append('register %d: port $%s, original $%s'
                                   % (r, o[i][r * 2:r * 2 + 2],
                                      t[i][r * 2:r * 2 + 2]))
            else:
                out.append(n)
    return '; '.join(out)


def main():
    args = sys.argv[1:]
    opts = {'--runs': 20, '--seed': 1, '--frames': 1500}
    keep = None
    coverage = '--coverage' in args
    stop = '--stop' in args
    args = [a for a in args if a not in ('--coverage', '--stop')]
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
    hz_front, assets = pos[2], pos[3]
    rng = random.Random(opts['--seed'])
    seen = set() if coverage else None
    bad = 0
    stats = {'frames': 0, 'sound': 0, 'ended': 0, 'demo': 0, 'cont': 0}
    t0 = time.time()
    with tempfile.TemporaryDirectory() as tmp:
        work = keep or tmp
        os.makedirs(work, exist_ok=True)
        for run in range(opts['--runs']):
            frames = rng.choice([200, 600, opts['--frames'],
                                 opts['--frames']])
            script = make_script(rng, frames)
            sp = os.path.join(work, 'script%d.txt' % run)
            op = os.path.join(work, 'port%d.txt' % run)
            write_script(sp, script)
            subprocess.run([hz_front, assets, sp, op], check=True)
            ours, our_sound, our_end, our_cont = read_port(op)
            cpu, their_end = run_original(rom, sym, script, seen)
            theirs, their_sound = cpu.frames, cpu.sound
            stats['frames'] += len(theirs)
            stats['sound'] += len(their_sound)
            if their_end is not None:
                stats['ended'] += 1
                stats['demo'] += their_end.startswith('-1')
            problem = None
            for k in range(min(len(ours), len(theirs))):
                if ours[k] != theirs[k]:
                    problem = 'frame %d: %s' % (k, describe_frame(
                        ours[k], theirs[k]))
                    if ours[k].split()[0] != theirs[k].split()[0]:
                        script['dump'] = k
                        cpu2, _ = run_original(rom, sym, script)
                        problem += '; ' + first_vram_diff(
                            hz_front, assets, sp, k, cpu2.dump)
                    break
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
            if problem is None and our_cont != cpu.cont_frame:
                problem = 'CONT: port at frame %s, original at %s' % (
                    our_cont, cpu.cont_frame)
            if cpu.cont_frame is not None:
                stats['cont'] += 1
            if problem is None and our_end != their_end:
                problem = 'end: port "%s", original "%s"' % (our_end,
                                                             their_end)
            if problem:
                bad += 1
                print('run %d (%s): %s' % (run, sp, problem))
                if stop:
                    break
    print('front: %d runs, %d frames, %d sound commands, %d ended (%d to '
          'the demo), %d at CONT: %s (%.0f s)'
          % (opts['--runs'], stats['frames'], stats['sound'],
             stats['ended'], stats['demo'], stats['cont'],
             'all identical' if not bad else '%d differ' % bad,
             time.time() - t0))
    if not (stop and bad):
        bad += check_loads(rom, sym, hz_front, assets, rng,
                           max(20, opts['--runs'] * 5))
    if coverage:
        report_coverage(rom, sym, seen)
    return 1 if bad else 0


def check_loads(rom, sym, hz_front, assets, rng, count):
    """A saved game's load against Password_Check accepting a password
    for the same record and side."""
    bad = 0
    for case in range(count):
        while True:
            win = rng.getrandbits(32) if rng.random() < 0.7 else \
                rng.choice([1 << rng.randrange(32),
                            0xFFFFFFFF ^ (1 << rng.randrange(32))])
            rec = [(win >> (24 - 8 * i)) & 0xFF for i in range(4)]
            complete = all(all(b >> m & 1 for b in rec) for m in range(8))
            if win not in (0, 0xFFFFFFFF) and not complete:
                break
        human = rng.choice([1, 2])
        mode = rng.randrange(256)
        key = (rng.randrange(8) << 1) | (1 if human == 2 else 0)
        k = WIN_RECORD_KEYS[(key >> 1) & 7]
        digits = []
        for i in range(4):
            b = rec[i] ^ ((k >> (24 - 8 * i)) & 0xFF)
            digits += [b >> 4, b & 15]
        digits.append(key)
        check = (~sum(digits)) & 0xFF
        digits += [check >> 4, check & 15]

        cpu = Cpu(rom, sym, {'pads': [0] * 4, 'music': [1] * 4})
        r = cpu.ram
        r[V_OPTIONS] = mode
        r[V_OPTIONS + 1:V_OPTIONS + 4] = bytes([rng.randrange(256),
                                                rng.randrange(256),
                                                rng.randrange(256)])
        before = bytes(r[V_OPTIONS:V_OPTIONS + 4])
        r[V_WIN_RECORD:V_WIN_RECORD + 4] = bytes(
            rng.randrange(256) for _ in range(4))
        r[V_PASSWORD:V_PASSWORD + 11] = bytes(digits)
        r[M_VDP_REG1] = 0x64
        try:
            cpu.call(sym['Password_Check'], limit=20_000_000)
        except Stop:
            pass
        theirs = '%s %s' % (
            ' '.join('%02X' % b for b in r[V_OPTIONS:V_OPTIONS + 4]),
            ' '.join('%02X' % b for b in r[V_WIN_RECORD:V_WIN_RECORD + 4]))
        out = subprocess.run([hz_front, assets, '--load']
                             + ['%02X' % b for b in rec]
                             + [str(human), '%02X' % mode],
                             check=True, capture_output=True, text=True)
        ours = out.stdout.split()
        # The port's load leaves v_difficulty and v_option2 as they were;
        # the original's too: its harness byte differs, so compare the mode
        # byte, the map and the record.
        t = theirs.split()
        mine = [ours[0], before.hex()[2:4].upper(), before.hex()[4:6].upper(),
                ours[3]] + ours[4:]
        if [t[0], t[3]] + t[4:] != [mine[0], mine[3]] + mine[4:]:
            bad += 1
            print('load %d: record %s, P%d, mode $%02X: port "%s", '
                  'original "%s"' % (case, ' '.join('%02X' % b for b in rec),
                                     human, mode, ' '.join(ours), theirs))
    print('loads: %d saved games against Password_Check: %s'
          % (count, 'all agree' if not bad else '%d differ' % bad))
    return bad


def report_coverage(rom, sym, seen):
    """The instructions of the title, options and helpers that no run
    reached, by routine (instruction lines of asm/hz.asm, with their
    addresses from the in-tree assembler)."""
    import re
    import hzasm_check
    asm = os.path.join(HERE, '..', 'asm', 'hz.asm')
    table = {'__lax__': True}
    hzasm_check.assemble(asm, table, True)
    del table['__lax__']
    addrs = []
    hzasm_check.assemble(asm, table, False, addrs)
    ranges = [(sym['MenuModule_Title'], sym['GameEnd_Run']),
              (sym['Options_Run'], sym['M_VDPSetReg'] + 0x1000)]
    routine = None
    missed = {}
    order = []
    for i, ln in enumerate(open(asm).read().split('\n')):
        m = re.match(r'^([A-Za-z_]\w*):', ln)
        if m:
            routine = m.group(1)
            continue
        text = ln.split(';')[0].strip()
        if not text or text.startswith('dc.') or text.startswith('.') \
                or text.startswith('binclude') or not ln.startswith('\t'):
            continue
        a = addrs[i]
        if not any(lo <= a < hi for lo, hi in ranges):
            continue
        if a not in seen:
            if routine not in missed:
                missed[routine] = []
                order.append(routine)
            missed[routine].append(a)
    total = sum(len(v) for v in missed.values())
    print('not run: %d instructions in %d routines' % (total, len(order)))
    for r in order:
        print('    %-28s %3d  from $%X' % (r, len(missed[r]), missed[r][0]))


if __name__ == '__main__':
    sys.exit(main())
