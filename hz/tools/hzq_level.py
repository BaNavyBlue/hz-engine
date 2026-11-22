#!/usr/bin/env python3
"""Q's drum table at the chip's level and pitch, song by song (session 39).

    tools/hzq_level.py ITERS SONG:INST [SONG:INST ...]   (needs ./hz_qdrums
                                                         built with Q, numpy,
                                                         scipy)

For each song and instrument (decimal song; the instrument's name, or its
number in hex: 3:kick_toms or 3:8) the table
(assets/sound/q_drums.json) already maps, `hz_qdrums strike` plays that
instrument alone through the chip and through Q, and each of its notes is
measured from its strike to the next strike or 150 ms at most:

  - kick, snare, hats: the level, K-weighted (ITU-R BS.1770; A-weighting
    takes 30 dB off a kick's 50 Hz), Q's against the chip's; the entry's
    `level_db` moved by the median difference
  - toms: the pitch averaged over the note by its energy (20 ms frames)
    and the level, A-weighted (session 38's measure); the entries' `pitch`
    and `level_db` (all the instrument's tom entries share them) moved by
    the medians

ITERS rounds (2-3: the level isn't linear in level_db at the top), then a
last measure. Works on a copy of assets/ in a temporary folder and writes
the table to q_drums.fitted.json in the current folder: copy it over
assets/sound/q_drums.json to keep it.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import wave

import numpy as np
from scipy.signal import lfilter

import hzsongtext

TREE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 53267
TOMS = ('low tom', 'mid tom', 'high tom')


def hz(fn):
    return (fn & 0x7FF) * (7670442.0 / 144) / (1 << (21 - ((fn >> 11) & 7)))


def load(p):
    w = wave.open(p)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
    return lfilter([1, -1], [1, -0.9995], x.astype(float))


def biquad(shelf, gain, q, fc):
    a_ = 10 ** (gain / 40)
    w0 = 2 * np.pi * fc / SR
    al = np.sin(w0) / (2 * q)
    c = np.cos(w0)
    if shelf:
        r = 2 * np.sqrt(a_) * al
        b = [a_ * ((a_ + 1) + (a_ - 1) * c + r),
             -2 * a_ * ((a_ - 1) + (a_ + 1) * c),
             a_ * ((a_ + 1) + (a_ - 1) * c - r)]
        a = [(a_ + 1) - (a_ - 1) * c + r, 2 * ((a_ - 1) - (a_ + 1) * c),
             (a_ + 1) - (a_ - 1) * c - r]
    else:
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
        a = [1 + al, -2 * c, 1 - al]
    return np.array(b) / a[0], np.array(a) / a[0]


SHELF = biquad(True, 3.999843853973347, 0.7071752369554196, 1681.974450955533)
HIGHPASS = biquad(False, 0, 0.5003270373238773, 38.13547087602444)


def kweight(x):
    return lfilter(*HIGHPASS, lfilter(*SHELF, x))


def aweight_db(g):
    sp = np.abs(np.fft.rfft(g * np.hanning(len(g)))) ** 2
    f = np.maximum(np.fft.rfftfreq(len(g), 1 / SR), 1.0)
    f2 = f * f
    ra = (12194 ** 2 * f2 * f2) / ((f2 + 20.6 ** 2) * np.sqrt(
        (f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194 ** 2))
    return 10 * np.log10((sp * ra ** 2).sum() + 1e-9)


def energy_pitch(g):
    n, hop = 1024, 256
    logs, weights = [], []
    f = np.fft.rfftfreq(32768, 1 / SR)
    band = (f > 30) & (f < 1500)
    for k in range(0, len(g) - n, hop):
        frame = g[k:k + n]
        sp = np.abs(np.fft.rfft(frame * np.hanning(n), 32768))
        logs.append(np.log2(f[band][np.argmax(sp[band])]))
        weights.append((frame ** 2).sum())
    weights = np.array(weights)
    return 2 ** (np.dot(logs, weights) / weights.sum())


def numbers_by_name(assets):
    """(Session 50) Each song's FM instruments' numbers by name, as the game
    reads the songs (a song's tracker in place of its text)."""
    folder = os.path.join(assets, 'sound')

    def read(name):
        with open(os.path.join(folder, name), encoding='utf-8-sig') as fh:
            return fh.read()
    model = hzsongtext.read_text(
        read, lambda name: os.path.isfile(os.path.join(folder, name)))
    return [{n: k for k, n in enumerate(s['fm_names']) if n}
            for s in model['songs']]


def number_of(e, names):
    """An entry's instrument number: its name's (-1: its song has none of
    that name), or as written ($hex or a number)."""
    inst = e['instrument']
    if isinstance(inst, str) and not inst.startswith('$'):
        return names[e['song']].get(inst, -1)
    return int(inst[1:], 16) if isinstance(inst, str) else inst


def entry_for(table, names, song, inst, f):
    for k, e in enumerate(table['drums']):
        if (e['song'] == song and number_of(e, names) == inst
                and e.get('min_hz', 0) <= f <= e.get('max_hz', 1e9)):
            return k
    return -1


def measure(assets, table, names, song, inst, seconds=60):
    with open(os.path.join(assets, 'sound', 'q_drums.json'), 'w') as f:
        f.write(json.dumps(table, indent=2) + '\n')
    wav = os.path.join(assets, '..', 'strike.wav')
    subprocess.run([os.path.join(TREE, 'hz_qdrums'), assets, 'strike',
                    str(song), '%X' % inst, str(seconds), wav, wav + '.txt'],
                   check=True, capture_output=True)
    chip, q = load(wav), load(wav + '.q.wav')
    kchip, kq = kweight(chip), kweight(q)
    rows = [ln.split() for ln in open(wav + '.txt')]
    starts = sorted(set(int(r[0]) for r in rows))
    levels, toms = {}, ([], [])
    seen = set()
    for r in rows:
        t = int(r[0])
        if t in seen:
            continue
        seen.add(t)
        k = entry_for(table, names, song, inst, hz(int(r[2], 16)))
        later = [u for u in starts if u > t]
        n = min(int(0.15 * SR), later[0] - t if later else 10 ** 9)
        if k < 0 or n < int(0.03 * SR):
            continue
        if table['drums'][k]['model'] in TOMS:
            if n >= int(0.05 * SR):
                toms[0].append(12 * np.log2(energy_pitch(q[t:t + n])
                                            / energy_pitch(chip[t:t + n])))
                toms[1].append(aweight_db(q[t:t + n])
                               - aweight_db(chip[t:t + n]))
        else:
            d = 10 * np.log10((kq[t:t + n] ** 2).mean()
                              / ((kchip[t:t + n] ** 2).mean() + 1e-12))
            levels.setdefault(k, []).append(d)
    return {k: float(np.median(v)) for k, v in levels.items()}, toms


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    rounds = int(sys.argv[1])
    work = tempfile.mkdtemp(prefix='hzq_level')
    assets = os.path.join(work, 'assets')
    shutil.copytree(os.path.join(TREE, 'assets'), assets)
    table = json.load(open(os.path.join(TREE, 'assets', 'sound',
                                        'q_drums.json')))
    names = numbers_by_name(assets)
    for group in sys.argv[2:]:
        song, word = int(group.split(':')[0]), group.split(':')[1]
        inst = names[song].get(word, None)
        if inst is None:
            inst = int(word, 16)
        for it in range(rounds + 1):
            levels, (st, db) = measure(assets, table, names, song, inst)
            words = []
            for k, v in sorted(levels.items()):
                e = table['drums'][k]
                words.append('%s %+.1f: %+.2f dB' % (e['model'],
                                                    e.get('level_db', 0), v))
                if it < rounds:
                    new = round(e.get('level_db', 0.0) - v, 1)
                    if abs(new) < 0.05:
                        e.pop('level_db', None)
                    else:
                        e['level_db'] = new
            if st:
                ms, md = float(np.median(st)), float(np.median(db))
                words.append('toms %d: %+.2f st, %+.2f dB' % (len(st), ms, md))
                if it < rounds:
                    for e in table['drums']:
                        if (e['song'] == song
                                and number_of(e, names) == inst
                                and e['model'] in TOMS):
                            e['pitch'] = round(e.get('pitch', 0.45)
                                               * 2 ** (-ms / 12), 3)
                            e['level_db'] = round(e.get('level_db', 0.0)
                                                  - md, 1)
            print('%d:%X round %d  %s' % (song, inst, it, '; '.join(words)),
                  flush=True)
    with open('q_drums.fitted.json', 'w') as f:
        f.write(json.dumps(table, indent=2) + '\n')
    shutil.rmtree(work)
    return 0


if __name__ == '__main__':
    sys.exit(main())
