#!/usr/bin/env python3
"""The Esc screen and the AUDIO page's volumes, through hz_headless.

    python3 tools/hzmenu_check.py [HZ_HEADLESS] [ASSETS]     (make menu)

Each case runs the headless game with a script of key presses (HZ_KEYS,
HZ_RAW) and its own saves folder (HZ_SAVES), then checks what the menu
wrote to settings.json, the sound (HZ_WAV) or when the run ended. Not a
model of anything in the ROM: the menu is the port's own. What it checks:

  - a run that never opens the menu writes no settings
  - MUSIC and EFFECTS step a dB a press, left down and right up
  - a key bound to a slot leaves every other slot it was in; a gamepad
    input waited for on CONTROLS binds nothing
  - Backspace while waiting empties the slot, Escape or the gamepad's
    Back keeps it
  - (session 54) the BUTTONS page: a trigger, a face button, a stick and
    a mapless joystick's button bound, each taken off the slot it was in;
    Backspace empties a slot, Back keeps one, a key binds nothing; its
    DEFAULTS puts the gamepads back and not the keys, CONTROLS' the keys
    and not the gamepads; Escape on it goes back to the GAMEPAD page, not
    the menu; the slots drawn as the first gamepad labels
    them (HZ_PAD_STYLE: none, ps3, xboxone, switch, joystick), read off
    the frame in the game's font; session 26's "pad" (a button for A to
    TURBO) read as the first slots; a new binding reaches the game (L2
    bound to B is the gun, as B on the pad is) and the input it replaced
    no longer does
  - TURBO (T in the scripts) held: B every other frame, so the gun
    fires as often as a player's can (every 6 frames)
  - QUIT ends the program at once
  - SYNTH in a build without Q stays NUKED (tools/hzq_check.py checks Q)
  - the PATCHES page's rows reach settings.json's "patches", and an old
    file's "fixes" (session 32's single switch) sets them all
  - (session 57) the menu's CPU row reaches settings.json's "cpu" and
    reads back from it
  - (PS2's) the CRT and HDR pages' values reach settings.json's "crt"
    and read back from it (PEAK's "auto" kept, WHITE clamped)
  - closing the menu with Enter on CONTINUE doesn't reach the game as
    Start (no pause: the sound goes on after it)
  - EFFECTS off sends no effects (the run is the default one until the
    first effect, frame 172 on map 1, then differs), and MUSIC off leaves
    only the effects (silence until then)
  - a settings file's odd values: below -30 dB is OFF, above 0 is 0, an
    unknown key or gamepad input, or Back, keeps the default
  - the mixer's channel table against Nuked-OPN2 (hz_ymchan)
  - (session 54) the GAMEPAD page: RUMBLE's every step in settings.json
    and buzzed on the gamepad at the new level, none at OFF; a PAD row
    buzzes its gamepad alone, an empty one nothing; the rumble a game
    sends gamepad 1 (HZ_RUMBLE's log) is, change for change, what
    hz_rumble works out for the side it drives -- P1 against the CPU, P2
    against the CPU (the CPU as P1), at MEDIUM, LOW from the file and a
    word it doesn't know (MEDIUM) -- none at OFF, none in a game of the
    CPU against the CPU (whose sides hz_rumble finds hit); the Esc screen
    stills it as it opens and keeps it still while open
  - every effect alone (hz_sound --volume): in frames 2-6, while it surely
    holds its channels, EFFECTS off leaves only the mixer's start-up settle
    (under 600) and MUSIC off keeps the effect (over 1,500). Only those
    frames: with no song started, an effect's end makes the driver restore
    its channel's music track with an instrument read from the driver's
    own tables (session 25's z_song quirk), a burst that is the music's;
    in the game a song always starts first
  - the effects that only use the PSG (no YM writes but the timers' an
    idle driver makes): EFFECTS off quiet for their whole length, the
    noise's release included (no music track has the noise channel, so
    nothing is restored when they end)
  - the map songs alone: MUSIC off is silence to the sample, EFFECTS off
    the default to the sample
  - (session 57) NETWORK's ADDRESS: a typed address (';' for ':') added to
    the player's own servers in settings.json and chosen; with the list
    full, the oldest of the player's own but the chosen forgotten for it;
    a port past 65535 not taken (NOT AN ADDRESS, nothing written); Escape
    leaves the server as it was
"""

import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
import zlib

HEADLESS = sys.argv[1] if len(sys.argv) > 1 else './hz_headless'
ASSETS = sys.argv[2] if len(sys.argv) > 2 else 'assets'
TOOLS = os.path.dirname(HEADLESS) or '.'
SOUND = os.path.join(TOOLS, 'hz_sound')
RUMBLE = os.path.join(TOOLS, 'hz_rumble')
YMCHAN = os.path.join(TOOLS, 'hz_ymchan')
EFFECTS = 29                              # the sfx table's count
SPF = 896040 * 48000 / 53693175          # WAV frames a game frame
FIRST_EFFECT = 262                        # map 1, these scripts: the gun (B at
                                         # 260); 172 before the units thought
                                         # (session 28: none drives into lava)

# (Session 54) The controls' defaults as settings.json has them.
ACTIONS = ['up', 'down', 'left', 'right', 'a', 'b', 'c', 'start', 'turbo']
KEYS_DEFAULT = {'up': ['UP', 'W'], 'down': ['DOWN', 'S'],
                'left': ['LEFT', 'A'], 'right': ['RIGHT', 'D'],
                'a': ['Z', 'L'], 'b': ['X', ';'], 'c': ['C', 'QUOTE'],
                'start': ['RETURN', '-'], 'turbo': ['V', 'SLASH']}
PAD_DEFAULT = {'up': ['DUP', 'LSUP'], 'down': ['DDOWN', 'LSDOWN'],
               'left': ['DLEFT', 'LSLEFT'], 'right': ['DRIGHT', 'LSRIGHT'],
               'a': ['WEST', '-'], 'b': ['SOUTH', '-'], 'c': ['EAST', '-'],
               'start': ['START', '-'], 'turbo': ['NORTH', '-']}

failures = []


def presses(start, step, *tokens):
    """An HZ_KEYS script: each token pressed for 2 frames, `step` apart;
    'wN' waits N frames."""
    out = []
    f = start
    for t in tokens:
        if t.startswith('w'):
            f += int(t[1:])
            continue
        out.append('%d:%s' % (f, t))
        out.append('%d:-' % (f + 2))
        f += step
    return ','.join(out)


def run(saves, frames, keys='', raw='', wav=None, settings=None, mapn=1,
        display=None, pads=(), rumble=None, mode=None, difficulty=None,
        style=None):
    os.makedirs(saves, exist_ok=True)
    if settings is not None:
        with open(os.path.join(saves, 'settings.json'), 'w') as f:
            f.write(settings)
    env = dict(os.environ)
    env.update(HZ_SAVES=saves, HZ_MODS='', HZ_FRAMES=str(frames),
               HZ_OUT=os.path.join(saves, 'last.png'))
    for k in ('HZ_KEYS', 'HZ_RAW', 'HZ_WAV', 'HZ_PAD1', 'HZ_PAD2',
              'HZ_FRAMES_DIR', 'HZ_DISPLAY', 'HZ_RUMBLE', 'HZ_PAD_STYLE',
              'HZ_OUT_INDEXED'):
        env.pop(k, None)
    if style is not None:
        env['HZ_PAD_STYLE'] = style
        env['HZ_OUT_INDEXED'] = '1'
    for i, script in enumerate(pads):
        env['HZ_PAD%d' % (i + 1)] = script
    if rumble:
        env['HZ_RUMBLE'] = rumble
    if display:
        env['HZ_DISPLAY'] = display
    if keys:
        env['HZ_KEYS'] = keys
    if raw:
        env['HZ_RAW'] = raw
    if wav:
        env['HZ_WAV'] = wav
    args = [HEADLESS, '--assets', ASSETS, '--map', str(mapn)]
    if mode is not None:
        args += ['--mode', str(mode)]
    if difficulty is not None:
        args += ['--difficulty', str(difficulty)]
    p = subprocess.run(args, env=env, capture_output=True, text=True)
    return p.stderr


def read_png(path):
    """An indexed PNG (colour type 3, 4 or 8 bits, not interlaced): its
    width, height and rows of palette indices."""
    with open(path, 'rb') as f:
        data = f.read()
    at, idat = 8, b''
    while at < len(data):
        n, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            idat += body
        at += 12 + n
    assert ctype == 3 and depth in (4, 8), (path, ctype, depth)
    raw = zlib.decompress(idat)
    stride = (w * depth + 7) // 8
    rows, prev, at = [], bytearray(stride), 0
    for _ in range(h):
        kind, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - 1] if i > 0 else 0
            b = prev[i]
            c = prev[i - 1] if i > 0 else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif kind == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        prev = line
        if depth == 8:
            rows.append(list(line))
        else:
            rows.append([(line[x // 2] >> (4 if x % 2 == 0 else 0)) & 15
                         for x in range(w)])
    return w, h, rows


_GLYPHS = None


def glyphs():
    """The Esc screen's characters as the game's font draws them: each an
    8 x 8 tuple of colours (0 none), from assets/tiles/font.png by
    sysmenu.c's font_tile."""
    global _GLYPHS
    if _GLYPHS is None:
        w, _, rows = read_png(os.path.join(ASSETS, 'tiles', 'font.png'))
        def tile(t):
            tx, ty = (t % (w // 8)) * 8, (t // (w // 8)) * 8
            return tuple(rows[ty + y][tx + x] for y in range(8)
                         for x in range(8))
        chars = {chr(c): c - 0x11 for c in range(ord('A'), ord('Z') + 1)}
        chars.update({str(d): 7 + d - 1 for d in range(1, 9)})
        chars.update({'9': 46, '0': 47, '-': ord('_') - 0x11})
        _GLYPHS = {tile(t): ch for ch, t in chars.items()}
        _GLYPHS[tuple([0] * 64)] = ' '
    return _GLYPHS


def screen_text(path):
    """An HZ_OUT_INDEXED frame read as text: a line a row of cells, each
    cell its character in the game's font, '.' for anything else (the
    window's frame, the arrow, the game)."""
    w, h, rows = read_png(path)
    table, lines = glyphs(), []
    for cy in range(h // 8):
        line = ''
        for cx in range(w // 8):
            cell = []
            for y in range(8):
                for x in range(8):
                    v = rows[cy * 8 + y][cx * 8 + x]
                    cell.append(v - 64 if 64 < v < 80 else 0)
            line += table.get(tuple(cell), '.')
        lines.append(line)
    return lines


def rumble_log(path):
    """HZ_RUMBLE's lines: (frame, pad, low, high)."""
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return [(int(a), int(b), int(c, 16), int(d, 16))
                for a, b, c, d in (line.split() for line in f)]


def rumble_expected(tmp, game, side, level, frames):
    """hz_rumble's lines for `side` at `level` in that game, and its felt
    counts side by side."""
    saves = os.path.join(tmp, 'rumble_saves')
    shutil.rmtree(saves, ignore_errors=True)
    os.makedirs(saves)
    p = subprocess.run([RUMBLE, ASSETS, '0', str(frames), '--game']
                       + [str(n) for n in game]
                       + ['--motors', str(side), str(level)],
                       env=dict(os.environ, HZ_SAVES=saves),
                       capture_output=True, text=True)
    lines, felt = [], {}
    for line in p.stdout.splitlines():
        w = line.split()
        if w[0] == 'side':
            felt[int(w[1].rstrip(':'))] = sum(int(n) for n in w[3::2])
        elif w[0] != 'seed':
            lines.append((int(w[0]), int(w[1]), int(w[2], 16),
                          int(w[3], 16)))
    return lines, felt


def settings(saves):
    path = os.path.join(saves, 'settings.json')
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return json.load(f)


def signed(path):
    w = wave.open(path)
    n = w.getnframes()
    return struct.unpack('<%dh' % (n * 2), w.readframes(n))


def samples(path):
    data = signed(path)
    return [max(abs(data[2 * i]), abs(data[2 * i + 1]))
            for i in range(len(data) // 2)]


def frame_peaks(path):
    """The peak of each 1/60 s of a WAV (either side)."""
    a = samples(path)
    blk = 800
    return [max(a[k * blk:(k + 1) * blk]) for k in range(len(a) // blk)]


def render(tmp, args, volume, seconds):
    path = os.path.join(tmp, 'sound.wav')
    subprocess.run([SOUND, ASSETS] + args + ['--seconds', str(seconds),
                                             '--wav', path, '--volume',
                                             volume],
                   capture_output=True, check=True)
    return path


def check(name, ok, detail=''):
    print('%-44s %s %s' % (name, 'ok' if ok else 'FAILED', detail))
    if not ok:
        failures.append(name)


def main():
    tmp = tempfile.mkdtemp(prefix='hzmenu')
    try:
        cases(tmp)
    finally:
        shutil.rmtree(tmp)
    if failures:
        print('%d failed: %s' % (len(failures), ', '.join(failures)))
        sys.exit(1)
    print('all agree')


def cases(tmp):
    p = subprocess.run([YMCHAN], capture_output=True, text=True)
    check('channel table against Nuked-OPN2', p.returncode == 0,
          '' if p.returncode == 0 else p.stdout)

    worst_off = 0
    least_on = 1 << 30
    which = ''
    for n in range(EFFECTS):
        off = max(frame_peaks(render(tmp, ['--sfx', '0:%d' % n], '0,0,off',
                                     0.2))[2:7])
        on = max(frame_peaks(render(tmp, ['--sfx', '0:%d' % n], '0,off,0',
                                    0.2))[2:7])
        if off >= 600 or on <= 1500:
            which += ' %d (%d/%d)' % (n, off, on)
        worst_off = max(worst_off, off)
        least_on = min(least_on, on)
    check('every effect alone: on the effects side', which == '',
          'effects off peak %d, music off at least %d%s'
          % (worst_off, least_on, which))

    def ym_writes(args):
        log = os.path.join(tmp, 'sound.log')
        subprocess.run([SOUND, ASSETS] + args + ['--seconds', '3', '--log',
                                                 log],
                       capture_output=True, check=True)
        with open(log) as f:
            return sum(1 for line in f
                       if line.split()[1] != 'P' and int(line.split()[0]) > 0)

    idle = ym_writes([])
    psg_only = [n for n in range(EFFECTS)
                if ym_writes(['--sfx', '0:%d' % n]) == idle]
    which = ''
    for n in psg_only:
        off = max(frame_peaks(render(tmp, ['--sfx', '0:%d' % n], '0,0,off',
                                     3))[2:])
        if off >= 600:
            which += ' %d (%d)' % (n, off)
    check('PSG-only effects: tails on the effects side',
          len(psg_only) > 0 and which == '',
          'effects %s%s' % (psg_only, which))

    which = ''
    for song in range(3, 11):
        args = ['--song', str(song), '--repeat']
        d = signed(render(tmp, args, '0,0,0', 6))
        e = signed(render(tmp, args, '0,0,off', 6))
        m = signed(render(tmp, args, '0,off,0', 6))
        if d != e or max(map(abs, m)) != 0:
            which += ' %d' % song
    check('map songs alone: all on the music side', which == '', which)

    d = os.path.join(tmp, 'untouched')
    run(d, 200, keys='100:R,150:RB')
    check('untouched: no settings written', settings(d) is None)

    # Main: DOWN x4 to AUDIO (past CRT), choose; MUSIC: right x2 (stops at
    # 0), left x3; EFFECTS: left x1, then C (choose) twice: up to 0, round
    # to OFF.
    d = os.path.join(tmp, 'volumes')
    run(d, 300, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'S', 'D', 'R',
                             'R', 'L', 'L', 'L', 'D', 'L', 'C', 'C'))
    s = settings(d)
    check('volumes: music -3, effects off',
          s is not None and s['audio'] == {'master': 0, 'music': -3,
                                           'effects': 'off'},
          str(s and s['audio']))

    # CONTROLS: row A, KEY 2 <- W (was UP's); then waiting on it again, a
    # gamepad input binds nothing and Escape keeps W. (Session 54: the
    # page has no PAD column; R past KEY 2 stays there.)
    d = os.path.join(tmp, 'bind')
    run(d, 300, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'S', 'D',
                             'D', 'D', 'D', 'R', 'S', 'w20', 'R', 'S', 'E',
                             'w20'),
        raw='196:W,227:PAD:NORTH')
    s = settings(d)
    check('bind: W to A, off UP; a pad input is no key',
          s is not None and s['keys']['a'] == ['Z', 'W']
          and s['keys']['up'] == ['UP', '-']
          and s['pad'] == PAD_DEFAULT,
          str(s and (s['keys']['a'], s['keys']['up'], s['pad']['a'])))

    # Backspace empties DOWN's KEY 1; Escape keeps KEY 2 (and ends the
    # wait: Q pressed after it binds nothing); on A's KEY 2 the gamepad's
    # Back keeps L (session 54: Back is Escape's there, the PAD column
    # gone). Presses from frame 120, 6 apart: the KEY 2 wait at 186,
    # Escape at 192, A's wait at 222, Back at 226.
    d = os.path.join(tmp, 'clear')
    run(d, 266, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'S', 'D',
                             'S', 'X', 'R', 'S', 'E', 'D', 'D', 'D', 'R',
                             'S', 'w10', 'E', 'w4', 'E'),
        raw='196:Q,226:PAD:BACK,229:PAD:DUP')
    s = settings(d)
    bound = s and [k for keys in s['keys'].values() for k in keys]
    check('clear: backspace empties, escape and back keep',
          s is not None and s['keys']['down'] == ['-', 'S']
          and s['keys']['a'] == ['Z', 'L'] and 'Q' not in bound
          and s['pad'] == PAD_DEFAULT,
          str(s and (s['keys']['down'], s['keys']['a'])))

    # DEFAULTS after the bindings above, from a file whose gamepads are
    # changed too: CONTROLS' puts the keys back, not the gamepads.
    odd_pad = ('{"keys": {"down": ["-", "S"], "a": ["Q", "-"]}, '
               '"pad": {"b": ["L1", "-"], "up": ["DUP", "-"]}}')
    d = os.path.join(tmp, 'defaults')
    run(d, 300, settings=odd_pad,
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'S', 'D', 'S',
                     'X', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'S'))
    s = settings(d)
    check('defaults: CONTROLS puts every key back, not the gamepads',
          s is not None and s['keys'] == KEYS_DEFAULT
          and s['pad']['b'] == ['L1', '-'] and s['pad']['up'] == ['DUP', '-']
          and s['pad']['a'] == ['WEST', '-'],
          str(s and (s['keys']['down'], s['keys']['turbo'], s['pad']['b'])))

    # (Session 54) BUTTONS: down to GAMEPAD (six rows), in, down to BUTTONS
    # (three), in; then the DEFAULTS row (up, round from UP) puts the
    # gamepads back and not the keys.
    to_buttons = ['E', 'D', 'D', 'D', 'D', 'D', 'D', 'S', 'D', 'D', 'D', 'S']
    d = os.path.join(tmp, 'buttons_defaults')
    run(d, 240, settings=odd_pad,
        keys=presses(120, 6, *(to_buttons + ['U', 'S'])))
    s = settings(d)
    check('buttons: DEFAULTS puts the gamepads back, not the keys',
          s is not None and s['pad'] == PAD_DEFAULT
          and s['keys']['down'] == ['-', 'S'] and s['keys']['a'] == ['Q', '-'],
          str(s and (s['pad']['b'], s['keys']['down'])))

    # Row A's 2ND <- L2 (a trigger); B's 1ST <- NORTH (TURBO's); C's 2ND <-
    # LSUP (UP's 2ND); START's 2ND <- JOY3 (a mapless joystick's button).
    # Presses from 120, 6 apart: BUTTONS at 186; the waits at 222, 240,
    # 258, 270, each input 3 frames after.
    d = os.path.join(tmp, 'buttons')
    run(d, 300, keys=presses(120, 6, *(to_buttons + [
        'D', 'D', 'D', 'D', 'R', 'S', 'L', 'D', 'S', 'D', 'R', 'S', 'D',
        'S'])),
        raw='225:PAD:L2,243:PAD:NORTH,261:PAD:LSUP,273:PAD:JOY3')
    s = settings(d)
    want = dict(PAD_DEFAULT, up=['DUP', '-'], a=['WEST', 'L2'],
                b=['NORTH', '-'], c=['EAST', 'LSUP'],
                start=['START', 'JOY3'], turbo=['-', '-'])
    check('buttons: a trigger, a face button, a stick, a joystick button',
          s is not None and s['pad'] == want and s['keys'] == KEYS_DEFAULT,
          str(s and s['pad']))

    # Waiting on UP's 1ST, Back keeps DUP; on DOWN's 2ND Backspace empties
    # it; waiting on it again, a key (Q) binds nothing and Escape keeps it
    # empty.
    d = os.path.join(tmp, 'buttons_clear')
    run(d, 260, keys=presses(120, 6, *(to_buttons + [
        'S', 'R', 'D', 'S', 'X', 'S', 'E'])),
        raw='195:PAD:BACK,225:Q')
    s = settings(d)
    check('buttons: backspace empties, back and escape keep, a key is no '
          'input',
          s is not None
          and s['pad'] == dict(PAD_DEFAULT, down=['DDOWN', '-'])
          and s['keys'] == KEYS_DEFAULT,
          str(s and (s['pad']['up'], s['pad']['down'])))

    # The page as the first gamepad labels it, read off the last frame:
    # each action's row, its two slots (10 cells each, 12 apart).
    def buttons_page(name, style, pads):
        d = os.path.join(tmp, name)
        run(d, 200, pads=pads, style=style,
            keys=presses(120, 6, *to_buttons))
        lines = screen_text(os.path.join(d, 'last.png'))
        col0 = (40 - 36) // 2
        page = {}
        for a, action in enumerate(ACTIONS):
            line = lines[(28 - 27) // 2 + 5 + 2 * a]
            page[action] = [line[col0 + 11 + 12 * k:col0 + 21 + 12 * k]
                            .strip() for k in range(2)]
        return lines, page

    looks = {
        ('none', ()): {'a': ['WEST', '-'], 'b': ['SOUTH', '-'],
                       'c': ['EAST', '-'], 'start': ['START', '-'],
                       'turbo': ['NORTH', '-'],
                       'up': ['DPAD UP', 'LS UP'],
                       'right': ['DPAD RIGHT', 'LS RIGHT']},
        ('ps3', ('0:-',)): {'a': ['SQUARE', '-'], 'b': ['CROSS', '-'],
                            'c': ['CIRCLE', '-'], 'start': ['START', '-'],
                            'turbo': ['TRIANGLE', '-'],
                            'up': ['DPAD UP', 'LS UP']},
        ('xboxone', ('0:-',)): {'a': ['X', '-'], 'b': ['A', '-'],
                                'c': ['B', '-'], 'start': ['MENU', '-'],
                                'turbo': ['Y', '-']},
        ('switch', ('0:-',)): {'a': ['Y', '-'], 'b': ['B', '-'],
                               'c': ['A', '-'], 'start': ['PLUS', '-'],
                               'turbo': ['X', '-']},
        ('joystick', ('0:-',)): {'up': ['HAT UP', 'AXIS UP'],
                                 'left': ['HAT LEFT', 'AXIS LEFT'],
                                 'a': ['WEST', '-']},
        # A style with no gamepad connected is no gamepad's: none's names.
        ('ps3', ()): {'b': ['SOUTH', '-'], 'turbo': ['NORTH', '-']},
    }
    bad = []
    for (style, pads), want in looks.items():
        lines, page = buttons_page('look_%s_%d' % (style, len(pads)), style,
                                   pads)
        title = any('GAMEPAD BUTTONS' in l and '1ST' not in l for l in lines)
        heads = any('1ST' in l and '2ND' in l for l in lines)
        got = {k: page[k] for k in want}
        if got != want or not title or not heads:
            bad.append('%s/%d: %s' % (style, len(pads), got))
    check('buttons: drawn as the first gamepad labels them', not bad,
          '; '.join(bad))

    # Escape on BUTTONS goes back to the GAMEPAD page, its four rows.
    d = os.path.join(tmp, 'buttons_back')
    run(d, 200, style='none', keys=presses(120, 6, *(to_buttons + ['E'])))
    lines = screen_text(os.path.join(d, 'last.png'))
    rows = [l.strip(' .') for l in lines]
    check('buttons: Escape goes back to the GAMEPAD page',
          'GAMEPAD' in rows and 'BUTTONS' in rows
          and any(r.startswith('RUMBLE') for r in rows)
          and not any('CONTINUE' in r for r in rows),
          ' | '.join(r for r in rows if r))

    # Session 26's "pad": a button for each of A to TURBO, read as the
    # first slot and an empty second (TURBO's "-" both empty); the
    # directions keep theirs. A change to MUSIC writes the file anew.
    d = os.path.join(tmp, 'pad_old')
    err = run(d, 170, settings='{"pad": {"a": "NORTH", "turbo": "-"}}',
              keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'S', 'D', 'R'))
    s = settings(d)
    check('buttons: session 26\'s "pad" read as the first slots',
          s is not None
          and s['pad'] == dict(PAD_DEFAULT, a=['NORTH', '-'],
                               turbo=['-', '-'])
          and 'keeps its own' not in err,
          str(s and s['pad']))

    # A new binding reaches the game. B's 1ST <- L2 (or R2) on BUTTONS,
    # the menu closed by 240 (the game held for its 120 frames: in play by
    # about 420); then from 430 the gamepad's L2, SOUTH or B (its
    # script's) tapped every 10 frames -- gamepad 1 is P2's with the
    # keyboard P1's: with L2 bound, L2 is P2's gun as B is; SOUTH, its
    # slot taken, is nothing, as L2 is with R2 bound.
    def tapped(name, bind, tap):
        d = os.path.join(tmp, name)
        taps = [430 + 10 * k for k in range(4)]
        raw = ['225:PAD:%s' % bind]
        pad = '0:-'
        if tap == 'B':
            pad = ','.join(['0:-'] + ['%d:B,%d:-' % (t, t + 1)
                                      for t in taps])
        else:
            raw += ['%d:PAD:%s' % (t, tap) for t in taps]
        run(d, 471, pads=[pad], raw=','.join(raw),
            keys=presses(120, 6, *(to_buttons + [
                'D', 'D', 'D', 'D', 'D', 'S', 'E', 'E', 'E'])))
        s = settings(d)
        with open(os.path.join(d, 'last.png'), 'rb') as f:
            return f.read(), s and s['pad']['b']
    l2, l2_pad = tapped('e2e_l2', 'L2', 'L2')
    b, _ = tapped('e2e_b', 'L2', 'B')
    south, _ = tapped('e2e_south', 'L2', 'SOUTH')
    r2, r2_pad = tapped('e2e_r2', 'R2', 'L2')
    check('buttons: L2 bound to B is the gun, SOUTH no longer is',
          l2 == b and south == r2 and l2 != r2
          and l2_pad == ['L2', '-'] and r2_pad == ['R2', '-'],
          'L2 = B %s, SOUTH = unbound %s, L2 != unbound %s; %s %s'
          % (l2 == b, south == r2, l2 != r2, l2_pad, r2_pad))

    # TURBO: T held from frame 300 (in play on map 1 by then) gives the
    # same frame 340 as B tapped on every even frame (the phase the host
    # starts with), with the bullets still flying, and not the same as B
    # held (one shot).
    def last(name, keys):
        d = os.path.join(tmp, name)
        run(d, 341, keys=keys)
        with open(os.path.join(d, 'last.png'), 'rb') as f:
            return f.read()
    turbo = last('turbo', '300:T,400:-')
    taps = last('taps', ','.join('%d:%s' % (n, 'B' if n % 2 == 0 else '-')
                                 for n in range(300, 401)))
    held = last('held', '300:B,400:-')
    check('turbo: T is B every other frame, more than B held',
          turbo == taps and turbo != held)

    # Into AUDIO and back out with B, down past PATCHES, (session 56)
    # NETWORK and (session 57) CPU to QUIT: the run ends at the choice
    # (frame 278), long before its 900 frames.
    d = os.path.join(tmp, 'quit')
    err = run(d, 900, keys=presses(200, 6, 'E', 'D', 'D', 'D', 'D', 'S', 'B',
                                   'D', 'D', 'D', 'D', 'D', 'D', 'S'))
    check('quit: B goes back, QUIT ends at once',
          'after 279 frames' in err,
          err.strip().splitlines()[-1] if err.strip() else '')

    # Enter on CONTINUE, held 30 frames: no pause, the sound goes on.
    d = os.path.join(tmp, 'swallow')
    wav = os.path.join(d, 'out.wav')
    os.makedirs(d, exist_ok=True)
    run(d, 330, keys='200:E,202:-,210:S,240:-', wav=wav)
    a = samples(wav)
    tail = a[int(250 * SPF):int(320 * SPF)]
    check('swallow: Enter closing the menu is no pause', max(tail) > 1000,
          'peak %d after' % max(tail))

    # The volume split, with the gun firing.
    keys = '200:R,260:RB,330:R,380:RB,420:R'
    w = {}
    for name, music, effects in (('default', 0, 0), ('nofx', 0, '"off"'),
                                 ('nomusic', '"off"', 0)):
        d = os.path.join(tmp, name)
        w[name] = os.path.join(d, 'out.wav')
        os.makedirs(d, exist_ok=True)
        run(d, 500, keys=keys, wav=w[name],
            settings='{"audio": {"music": %s, "effects": %s}}' % (music,
                                                                  effects))
    a, b, c = (samples(w[k]) for k in ('default', 'nofx', 'nomusic'))
    first = next((i for i in range(len(a)) if a[i] != b[i]), None)
    at = first / SPF if first is not None else -1
    check('effects off: the default run until the first effect',
          first is not None and FIRST_EFFECT <= at < FIRST_EFFECT + 1,
          'first difference at frame %.1f' % at)
    # Below the knee the chain is linear, so silencing the effects would
    # leave default - music-off; not sending them leaves more: the music
    # the effects took their channels from.
    sa, sb, sc = (signed(w[k]) for k in ('default', 'nofx', 'nomusic'))
    kept = max(abs(sa[i] - sb[i] - sc[i]) for i in range(len(sa)))
    check('effects off: the music keeps its channels', kept > 1000,
          'default - (effects off + music off): peak %d' % kept)
    quiet = max(c[:int(FIRST_EFFECT * SPF)])
    check('music off: silent until the first effect',
          quiet == 0 and max(c) > 1000,
          'peak %d before, %d in all' % (quiet, max(c)))

    # The CRT page (PS2's): SCANLINES on, MASK one on (the grille), SUBPIXELS
    # one on (BGR), HDR opens its page: HDR on, PEAK one on (400), WHITE
    # one on (210); B twice back to the menu. On a 1080p screen fullscreen
    # (HZ_DISPLAY) every value can be had but HDR.
    d = os.path.join(tmp, 'crt')
    run(d, 300, display='1920x1080 fullscreen',
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'S', 'S', 'D', 'R', 'D',
                     'R', 'D', 'S', 'S', 'D', 'R', 'D', 'R', 'B', 'B'))
    s = settings(d)
    check('crt: the pages\' values to settings.json',
          s is not None and s.get('crt') == {
              'scanlines': True, 'mask': 'grille', 'subpixels': 'bgr',
              'hdr': True, 'peak': 400, 'white': 210},
          str(s and s.get('crt')))
    # Read back: every value, AUTO from the file's "auto".
    run(d, 180, display='1920x1080 fullscreen',
        settings='{"crt": {"scanlines": true, "mask": "shadow", '
                 '"subpixels": "other", "hdr": false, "peak": "auto", '
                 '"white": 5000}}',
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'S', 'D', 'D', 'R'))
    s = settings(d)
    check('crt: read back; WHITE clamped, AUTO kept',
          s is not None and s.get('crt') == {
              'scanlines': True, 'mask': 'shadow', 'subpixels': 'rgb',
              'hdr': False, 'peak': 'auto', 'white': 1000},
          str(s and s.get('crt')))
    # (session 57 (2)) INTERLACED is gone (sessions 53-57): a file with
    # "scanlines": "interlaced" reads as ON -- down from SCANLINES is MASK,
    # right to the grille -- and is written back as true, with no
    # "flicker".
    d = os.path.join(tmp, 'interlaced')
    run(d, 180, display='1920x1080 fullscreen',
        settings='{"crt": {"scanlines": "interlaced", "flicker": "soft"}}',
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'S', 'D', 'R'))
    s = settings(d)
    check('crt: an "interlaced" file read as ON',
          s is not None and (s.get('crt') or {}).get('scanlines') is True
          and 'flicker' not in s['crt']
          and s['crt'].get('mask') == 'grille', str(s and s.get('crt')))

    # SYNTH (session 36) in a build without Q: right on it changes nothing
    # (Q NOT BUILT); MUSIC -1 on the way writes the file, "synth": "nuked".
    d = os.path.join(tmp, 'synth')
    run(d, 220, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'S', 'D', 'L',
                             'D', 'D', 'R'))
    s = settings(d)
    check('synth: NUKED stays without Q',
          s is not None and s.get('synth') == 'nuked'
          and s['audio']['music'] == -1,
          str(s and (s.get('synth'), s['audio'])))

    # (Session 57) CPU: down to it (nine rows), right: NIGHTMARE in the
    # file; then from such a file, Start on it: back to ORIGINAL.
    d = os.path.join(tmp, 'cpu')
    run(d, 220, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D',
                             'D', 'D', 'R'))
    s = settings(d)
    check('cpu: right on CPU writes "cpu": "nightmare"',
          s is not None and s.get('cpu') == 'nightmare',
          str(s and s.get('cpu')))
    d = os.path.join(tmp, 'cpu_back')
    run(d, 220, settings='{"cpu": "nightmare"}',
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D',
                     'D', 'S'))
    s = settings(d)
    check('cpu: read back, Start on it is ORIGINAL again',
          s is not None and s.get('cpu') == 'original',
          str(s and s.get('cpu')))

    # PATCHES: down to it (seven rows, GAMEPAD's since session 54), in, down
    # to JET GUNS, Start turns it off; the others stay as they were (FIRE
    # RATE and, since session 60, TURRETS off; UNITS, PATHS, session 72's
    # REPAIR and session 75's PIXELS off too).
    d = os.path.join(tmp, 'patches')
    run(d, 220, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D',
                             'S', 'D', 'S'))
    s = settings(d)
    check('patches: JET GUNS off, the rest as default (FIRE RATE off)',
          s is not None and s.get('patches') == {
              'supply': True, 'jet_guns': False, 'unit_aim': True,
              'fire_rate': False, 'guard_chase': True,
              'cpu_hits': True, 'turrets': False, 'units': False,
              'paths': False, 'repair': False, 'pixels': False},
          str(s and s.get('patches')))
    # Read back: session 32's "fixes": false turns every patch off; then
    # UNIT AIM turned on (left on its row) writes the file.
    d = os.path.join(tmp, 'patches_old')
    run(d, 230, settings='{"fixes": false}',
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'S',
                     'D', 'D', 'L'))
    s = settings(d)
    check('patches: an old "fixes": false is all off',
          s is not None and s.get('patches') == {
              'supply': False, 'jet_guns': False, 'unit_aim': True,
              'fire_rate': False, 'guard_chase': False,
              'cpu_hits': False, 'turrets': False, 'units': False,
              'paths': False, 'repair': False, 'pixels': False},
          str(s and s.get('patches')))
    # (Session 61) UNITS, the eighth row: up from SUPPLY round to it (past
    # PIXELS, REPAIR and PATHS, the eleventh to ninth), Start turns it on;
    # read back, kept.
    d = os.path.join(tmp, 'patches_units')
    run(d, 220, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D',
                             'S', 'U', 'U', 'U', 'U', 'S'))
    s = settings(d)
    check('patches: UNITS on, the eighth row',
          s is not None and (s.get('patches') or {}).get('units') is True
          and s['patches'].get('supply') is True, str(s and s.get('patches')))
    run(d, 180, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D',
                             'S', 'D', 'S', 'S'))
    s = settings(d)
    check('patches: UNITS read back',
          s is not None and (s.get('patches') or {}).get('units') is True,
          str(s and s.get('patches')))
    # (Session 72) REPAIR, the tenth row: up from SUPPLY past PIXELS,
    # Start turns it on and nothing else.
    d = os.path.join(tmp, 'patches_repair')
    run(d, 220, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D',
                             'S', 'U', 'U', 'S'))
    s = settings(d)
    check('patches: REPAIR on, the tenth row',
          s is not None and s.get('patches') == {
              'supply': True, 'jet_guns': True, 'unit_aim': True,
              'fire_rate': False, 'guard_chase': True,
              'cpu_hits': True, 'turrets': False, 'units': False,
              'paths': False, 'repair': True, 'pixels': False},
          str(s and s.get('patches')))
    # (Session 75) PIXELS, the eleventh row, one past the ten the page
    # shows: up from SUPPLY (the page scrolled to it), Start turns it on
    # and nothing else.
    d = os.path.join(tmp, 'patches_pixels')
    run(d, 220, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D',
                             'S', 'U', 'S'))
    s = settings(d)
    check('patches: PIXELS on, the eleventh row (scrolled)',
          s is not None and s.get('patches') == {
              'supply': True, 'jet_guns': True, 'unit_aim': True,
              'fire_rate': False, 'guard_chase': True,
              'cpu_hits': True, 'turrets': False, 'units': False,
              'paths': False, 'repair': False, 'pixels': True},
          str(s and s.get('patches')))

    # (Session 54) GAMEPAD: down to it (six rows), in, down twice to
    # RUMBLE; right to HIGH, then left to MEDIUM, LOW and OFF, 30 frames
    # apart. Each step is buzzed on the gamepad at the new level -- the
    # test's peaks, the big motor 80% and the small 60%, scaled -- and OFF
    # not at all; the file ends at "off".
    d = os.path.join(tmp, 'gamepad')
    log = os.path.join(d, 'rumble.log')
    run(d, 330, pads=['0:-'], rumble=log,
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'S', 'D',
                     'D', 'R', 'w30', 'L', 'w30', 'L', 'w30', 'L', 'w30'))
    lines = rumble_log(log)
    # The presses' pumps (frames 180, 216, 252, 288 of the script) come
    # after that many frames and one more presented: the log's count.
    starts = [181, 217, 253, 289]
    peaks = []
    for at, end in zip(starts, starts[1:] + [330]):
        got = [(lo, hi) for f, pad, lo, hi in lines if at <= f < end]
        peaks.append(max(got) if got else (0, 0))
    want = [(0xCCCC, 0x9999), (0x9999, 0x7332), (0x6666, 0x4CCC), (0, 0)]
    s = settings(d)
    check('gamepad: RUMBLE steps buzz at each level',
          peaks == want and s is not None and s.get('rumble') == 'off'
          and all(pad == 1 for _, pad, _, _ in lines)
          and lines[-1][2:] == (0, 0),
          ' '.join('%04X/%04X' % p for p in peaks) + ' '
          + str(s and s.get('rumble')))

    # PAD 2 with two gamepads: A buzzes pad 2 alone (at MEDIUM); PAD 1 with
    # none connected buzzes nothing.
    d = os.path.join(tmp, 'gamepad_pad')
    log = os.path.join(d, 'rumble.log')
    run(d, 220, pads=['0:-', '0:-'], rumble=log,
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'S', 'D',
                     'A'))
    two = rumble_log(log)
    d = os.path.join(tmp, 'gamepad_none')
    log = os.path.join(d, 'rumble.log')
    run(d, 220, rumble=log,
        keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'S', 'A'))
    none = rumble_log(log)
    check('gamepad: a PAD row buzzes its own pad',
          len(two) > 0 and all(pad == 2 for _, pad, _, _ in two)
          and max(lo for _, _, lo, _ in two) == 0x9999 and none == [],
          '%d lines, pads %s; none: %d' % (len(two),
                                           sorted({l[1] for l in two}),
                                           len(none)))

    # A game's rumble: HZ_RUMBLE's log against hz_rumble's lines for the
    # side gamepad 1 drives. Map 3, difficulty D: the CPU's units reach the
    # human's HQ by frame ~7,900.
    def game_rumble(name, mode, side, level, frames, file=None):
        d = os.path.join(tmp, name)
        log = os.path.join(d, 'rumble.log')
        run(d, frames, mapn=3, mode=mode, difficulty=3, pads=['0:-'],
            rumble=log, settings=file)
        want, felt = rumble_expected(tmp, (3, mode, 3), side, level, frames)
        return rumble_log(log), want, felt

    got, want, felt = game_rumble('game_p1', 5, 0, 2, 9000)
    check('game: P1 against the CPU feels its side',
          got == want and len(got) > 20,
          '%d lines, %d expected' % (len(got), len(want)))
    got, want, felt = game_rumble('game_p2', 2, 1, 2, 9000)
    check('game: P2 against the CPU feels its side',
          got == want and len(got) > 20,
          '%d lines, %d expected' % (len(got), len(want)))
    got, want, felt = game_rumble('game_low', 5, 0, 1, 9000,
                                  '{"rumble": "low"}')
    check('game: "low" from the file, half',
          got == want and len(got) > 20,
          '%d lines, %d expected' % (len(got), len(want)))
    got, want, felt = game_rumble('game_odd', 5, 0, 2, 9000,
                                  '{"rumble": "loud"}')
    check('game: a word not known is MEDIUM',
          got == want and len(got) > 20,
          '%d lines, %d expected' % (len(got), len(want)))
    got, want, felt = game_rumble('game_off', 5, 0, 0, 9000,
                                  '{"rumble": "off"}')
    check('game: "off" sends nothing', got == [] and want == [],
          '%d lines' % len(got))
    got, want, felt = game_rumble('game_cpus', 3, 1, 3, 12000)
    check('game: the CPU against the CPU, no one\'s',
          got == [] and felt.get(1, 0) > 0,
          '%d lines; hz_rumble felt %s' % (len(got), felt))

    # The Esc screen opened during the HQ hit's shake (from 7,863): the
    # motors still on the frame it opens and while it is open.
    d = os.path.join(tmp, 'game_menu')
    log = os.path.join(d, 'rumble.log')
    run(d, 7930, mapn=3, mode=5, difficulty=3, pads=['0:-'], rumble=log,
        keys='7869:E,7871:-,7915:E,7917:-')
    lines = rumble_log(log)
    before = [l for l in lines if l[0] <= 7870]
    during = [l for l in lines if 7870 < l[0] <= 7916]
    check('game: the Esc screen stills the rumble',
          len(before) >= 2 and before[-1][0] == 7870
          and before[-1][2:] == (0, 0) and during == [],
          '%s; %d while open' % (before[-2:], len(during)))

    d = os.path.join(tmp, 'odd')
    err = run(d, 60, mapn=0,
              settings='{"audio": {"master": -40, "music": 5, '
                       '"effects": "loud"}, "keys": {"a": ["NOSUCH", '
                       '"ESCAPE"]}, "pad": {"b": ["BACK", "NOSUCH"], '
                       '"c": "LSTICKUP"}}')
    check('odd file: messages for each bad name',
          err.count('keeps its own') == 5, err.strip().replace('\n', ' | '))
    # A change to MUSIC (5 read as 0, right stays 0) writes the file back
    # with the rest as read.
    run(d, 170, mapn=0, keys=presses(120, 6, 'E', 'D', 'D', 'D', 'D', 'S',
                                     'D', 'R'))
    s = settings(d)
    check('odd file: -40 off, +5 is 0, unknown kept',
          s is not None and s['audio'] == {'master': 'off', 'music': 0,
                                           'effects': 0}
          and s['keys']['a'] == ['Z', 'L'] and s['pad']['b'] == ['SOUTH', '-']
          and s['pad']['c'] == ['EAST', '-'],
          str(s and s['audio']))

    # (Session 57) NETWORK's ADDRESS, typed a key every 4 frames (';' is
    # ':'), then `finish` pressed.
    def address(name, text, finish, start=None):
        d = os.path.join(tmp, name)
        keys = presses(200, 6, 'E', *(['D'] * 8), 'S', 'D', 'A')
        t0 = 200 + 6 * 12 + 4
        raw = ','.join('%d:%s' % (t0 + 4 * i, 'PERIOD' if ch == '.' else ch)
                       for i, ch in enumerate(text))
        end = t0 + 4 * len(text) + 4
        if finish:
            keys += ',' + presses(end, 6, finish)
        run(d, end + 10, keys=keys, raw=raw, settings=start, style='none')
        return settings(d), screen_text(os.path.join(d, 'last.png'))

    s, _ = address('address', '203.0.113.7;4000', 'S')
    net = s['net'] if s else {}
    check('address: typed, added to the own servers and chosen',
          net.get('server') == '203.0.113.7:4000'
          and {'name': '203.0.113.7', 'host': '203.0.113.7',
               'port': 4000} in net.get('servers', []),
          str(net))
    own = ','.join('{"name": "S%d", "host": "10.0.0.%d"}' % (i, i)
                   for i in range(1, 12))
    s, _ = address('address-full', '9.9.9.9', 'S',
                   '{"net": {"server": "10.0.0.1:19890", "servers": [%s]}}'
                   % own)
    net = s['net'] if s else {}
    hosts = [e['host'] for e in net.get('servers', [])]
    check('address: a full list forgets the oldest own but the chosen',
          net.get('server') == '9.9.9.9:19890'
          and hosts == ['10.0.0.%d' % i for i in [1] + list(range(3, 12))]
          + ['9.9.9.9'], '%s %s' % (net.get('server'), hosts))
    s, lines = address('address-bad', '1.2;99999', 'S')
    check('address: a port past 65535 not taken, nothing written',
          s is None and any('NOT AN ADDRESS' in l for l in lines),
          str(s))
    s, lines = address('address-escape', '5.6', 'E')
    check('address: Escape leaves it, nothing written',
          s is None and any('ADDRESS    127.0.0.1' in l for l in lines),
          str(s))


if __name__ == '__main__':
    main()
