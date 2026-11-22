#!/usr/bin/env python3
"""The mods folder (docs/mods.md), through hz_headless: a mod of each kind,
good and broken, each in a mods folder of its own beside the tree's
assets, held to the game with none.

    python3 tools/hzmods_check.py ./hz_headless assets

A case runs the title (900 frames, its song and into the attract demo; or
300, the title screen itself) or a CPU game on ABGRUND (600 frames) in a
scratch folder, with HZ_MODS naming its mods folder, and checks what
the game says (`mods: mods, N in use`, each mod in use named, each one set
aside named) and what it plays and shows: the WAV and the last frame the
same as with no mods folder, or (a good mod) the one it changes different.
A song's mod is its text (read over the game's tracker) or its tracker
(read over both); the tracker's edit plays as the same edit in the text.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import zlib
from concurrent.futures import ThreadPoolExecutor

HEADLESS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                           else './hz_headless')
ASSETS = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else 'assets')
SCENES = {'title': ([], 900), 'logo': ([], 300),
          'game': (['--map', '0', '--mode', '1'], 600)}
TITLE = 'sound/songs/00_jagd_flieger.txt'
TITLE_TRK = 'sound/songs/00_jagd_flieger.trk'
SONG1 = 'sound/songs/01_the_previous_war.txt'
SONG3 = 'sound/songs/03_there_is_no_time_to_lose.txt'


def md5(path):
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as fh:
        return hashlib.md5(fh.read()).hexdigest()


def run(root, scene, mods):
    saves = os.path.join(root, 'saves')
    os.makedirs(saves, exist_ok=True)
    args, frames = SCENES[scene]
    env = dict(os.environ, HZ_SAVES=saves, HZ_MODS=mods,
               HZ_FRAMES=str(frames),
               HZ_OUT=os.path.join(root, 'last.png'),
               HZ_WAV=os.path.join(root, 'out.wav'))
    for k in ('HZ_KEYS', 'HZ_RAW', 'HZ_PAD1', 'HZ_PAD2', 'HZ_FRAMES_DIR',
              'HZ_DISPLAY'):
        env.pop(k, None)
    p = subprocess.run([HEADLESS, '--assets', ASSETS] + args, env=env,
                       capture_output=True, text=True, cwd=root)
    return (p.returncode, p.stderr, md5(os.path.join(root, 'out.wav')),
            md5(os.path.join(root, 'last.png')))


# -- making mods ---------------------------------------------------------

def put(root, rel, data):
    path = os.path.join(root, 'mods', rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = 'wb' if isinstance(data, bytes) else 'w'
    with open(path, mode, **({} if mode == 'wb' else
                             {'encoding': 'utf-8', 'newline': '\n'})) as fh:
        fh.write(data)


def own(rel, binary=False):
    with open(os.path.join(ASSETS, rel), 'rb' if binary else 'r',
              **({} if binary else {'encoding': 'utf-8'})) as fh:
        return fh.read()


def edited(rel, old, new):
    text = own(rel)
    assert text.count(old) >= 1, (rel, old)
    return text.replace(old, new, 1)


def inverted_palette(rel):
    """The PNG with its PLTE's colours inverted (its CRC made again)."""
    data = bytearray(own(rel, binary=True))
    at = 8
    while at < len(data):
        n = int.from_bytes(data[at:at + 4], 'big')
        kind = bytes(data[at + 4:at + 8])
        if kind == b'PLTE':
            for k in range(at + 8, at + 8 + n):
                data[k] ^= 0xFF
            crc = zlib.crc32(bytes(data[at + 4:at + 8 + n]))
            data[at + 8 + n:at + 12 + n] = crc.to_bytes(4, 'big')
            return bytes(data)
        at += 12 + n
    raise ValueError('%s has no PLTE' % rel)


def big_phrase(notes):
    names = ['C4', 'D4', 'E4', 'F4', 'G4', 'A4', 'B4', 'C5']
    lines = ['', 'phrase 200']
    for _ in range(0, notes, 8):
        lines.append('    ' + '  '.join('%s 24 22 %d' % (names[j], 90 + j)
                                        for j in range(8)))
    return '\n'.join(lines) + '\n'


# the title's first note an octave down; a note that isn't one
GOOD_TITLE = ('volume +6  C5 3 1 99', 'volume +6  C4 3 1 99')
BROKEN_TITLE = ('C5 3 1 96', 'X5 3 1 96')
# the same in its tracker (phrase 0 is fm1's first, played once)
GOOD_TRK = ('C-5 v99 pan left volume +6', 'C-4 v99 pan left volume +6')
BROKEN_TRK = ('C-5 v99 pan left volume +6', 'X-5 v99 pan left volume +6')


def song_good(r):
    put(r, TITLE, edited(TITLE, *GOOD_TITLE))


def song_broken(r):
    put(r, TITLE, edited(TITLE, *BROKEN_TITLE))


def song_broken_and_good(r):
    song_broken(r)
    put(r, SONG1, own(SONG1))


def tracker_good(r):
    put(r, TITLE_TRK, edited(TITLE_TRK, *GOOD_TRK))


def tracker_broken(r):
    put(r, TITLE_TRK, edited(TITLE_TRK, *BROKEN_TRK))


def tracker_and_text(r):
    """The game's tracker as a mod, and the text edited: the tracker wins
    (a mod's tracker, a mod's text, the game's tracker, the game's
    text)."""
    put(r, TITLE_TRK, own(TITLE_TRK))
    song_good(r)


def new_song_file(r):
    put(r, 'sound/sounds.txt',
        edited('sound/sounds.txt', 'song 0 songs/00_jagd_flieger.txt',
               'song 0 songs/my_title.txt'))
    put(r, 'sound/songs/my_title.txt', edited(TITLE, *GOOD_TITLE))


def too_big(r):
    put(r, SONG3, own(SONG3) + big_phrase(7300))
    put(r, SONG1, own(SONG1))


def sounds_broken(r):
    put(r, 'sound/sounds.txt', edited('sound/sounds.txt', 'effects $FA00',
                                      'effects $8010'))


def png_good(r):
    put(r, 'palettes/title.png', inverted_palette('palettes/title.png'))


def png_broken(r):
    put(r, 'palettes/title.png', b'not a png\n')


def json_broken(r):
    put(r, 'data/units.json', own('data/units.json') + '\n}\n')


def json_refused(r):
    put(r, 'data/units.json', '{"units": []}\n')


def map_good(r):
    put(r, 'maps/0_abgrund/palette.png',
        inverted_palette('maps/0_abgrund/palette.png'))


def map_broken(r):
    put(r, 'maps/0_abgrund/layout.tmx', '<map>nothing</map>\n')


def unread(r):
    put(r, 'notes.txt', 'not a file the game reads\n')
    put(r, 'sound/songs/99_spare.txt', 'song  volume 80\n')


def nothing(r):
    pass


# name, scene, setup, in use, set aside, what differs from no mods
# ('' nothing, 'wav', 'png', or a case whose WAV it must equal: '=name')
CASES = [
    ('empty', 'title', nothing, [], [], ''),
    ('song', 'title', song_good, [TITLE], [], 'wav'),
    ('song_broken', 'title', song_broken, [], [TITLE], ''),
    ('song_broken_and_good', 'title', song_broken_and_good, [SONG1], [TITLE],
     ''),
    ('tracker', 'title', tracker_good, [TITLE_TRK], [], '=song'),
    ('tracker_broken', 'title', tracker_broken, [], [TITLE_TRK], ''),
    ('tracker_and_text', 'title', tracker_and_text, [TITLE_TRK], [], ''),
    ('new_song_file', 'title', new_song_file,
     ['sound/sounds.txt', 'sound/songs/my_title.txt'], [], '=song'),
    ('song_too_big', 'title', too_big, [], [SONG3, SONG1], ''),
    ('sounds_broken', 'title', sounds_broken, [], ['sound/sounds.txt'], ''),
    ('png', 'logo', png_good, ['palettes/title.png'], [], 'png'),
    ('png_broken', 'title', png_broken, [], ['palettes/title.png'], ''),
    ('json_broken', 'game', json_broken, [], ['data/units.json'], ''),
    ('json_refused', 'game', json_refused, [], ['data/units.json'], ''),
    ('map', 'game', map_good, ['maps/0_abgrund/palette.png'], [], 'png'),
    ('map_broken', 'game', map_broken, [], ['maps/0_abgrund/layout.tmx'],
     ''),
    ('unread', 'title', unread, [], [], ''),
]


def main():
    top = tempfile.mkdtemp(prefix='hzmods_')
    try:
        def base(scene):
            d = os.path.join(top, 'none_' + scene)
            os.makedirs(d)
            return run(d, scene, '')

        def case(c):
            name, scene, setup = c[:3]
            d = os.path.join(top, name)
            os.makedirs(d)
            setup(d)
            return run(d, scene, 'mods'), os.path.exists(
                os.path.join(d, 'mods', 'README.txt'))

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            bases = dict(zip(SCENES, pool.map(base, SCENES)))
            results = list(pool.map(case, CASES))
        failures = 0
        for scene, (code, err, wav, png) in bases.items():
            ok = code == 0 and 'mods:' not in err and wav and png
            ok = ok and not os.path.exists(os.path.join(top, 'none_' + scene,
                                                        'mods'))
            print('%-22s %s  no mods folder (HZ_MODS ""): %s'
                  % ('none (' + scene + ')', 'ok' if ok else 'FAILED',
                     'nothing said or written' if ok else err.strip()))
            failures += not ok
        by_name = {c[0]: r for c, r in zip(CASES, results)}
        for c, ((code, err, wav, png), readme) in zip(CASES, results):
            name, scene, _, in_use, aside, differs = c
            bwav, bpng = bases[scene][2], bases[scene][3]
            why = []
            if code != 0:
                why.append('exit %d' % code)
            if not readme:
                why.append('no README.txt')
            if 'mods: mods, %d in use' % len(in_use) not in err:
                why.append('not "%d in use"' % len(in_use))
            for rel in in_use:
                if 'mods: mods/%s in place of' % rel not in err:
                    why.append('%s not named in use' % rel)
            for rel in aside:
                if 'mods: mods/%s set aside' % rel not in err:
                    why.append('%s not set aside' % rel)
            if err.count(' set aside: ') != len(aside):
                why.append('%d set aside, not %d'
                           % (err.count(' set aside: '), len(aside)))
            if differs.startswith('='):
                if wav != by_name[differs[1:]][0][2]:
                    why.append('the WAV is not %s\'s' % differs[1:])
            elif (wav != bwav) != (differs == 'wav'):
                why.append('the WAV %s' % ('is the same' if wav == bwav
                                           else 'differs'))
            if (png != bpng) != (differs == 'png'):
                why.append('the last frame %s' % ('is the same' if png == bpng
                                                  else 'differs'))
            said = '; '.join(l for l in err.splitlines()
                             if l.startswith(('mods:', 'songtext:', 'assets:',
                                              'png:', 'json:'))
                             and 'in use' not in l)
            print('%-22s %s  %s' % (name, 'ok' if not why else 'FAILED',
                                    '; '.join(why) if why else
                                    (said or 'nothing set aside')[:150]))
            if why:
                failures += 1
                print('    ' + err.strip().replace('\n', '\n    '))
        total = len(CASES) + len(SCENES)
        print('%d of %d agree' % (total - failures, total))
        return 1 if failures else 0
    finally:
        shutil.rmtree(top, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
