#!/usr/bin/env python3
"""The Q backend (src/audio/q/, PS2's) in HZ's mix, through hz_headless_q
(hz_headless built with Q: `make q_check`). Not a model of the ROM: Q
re-voices the chip, so nothing here should match Nuked sample for sample.
What it checks, on map 1 from the start (the loading screen's song, then
the map's), SYNTH set by settings.json (the first two with the drum table
left out: Q's voicing of the chip's notes):

  - Q plays the chip's notes: the 50 ms energy envelopes of the two runs
    correlate above 0.9
  - Q is as loud as the chip within 3 dB (RMS over 5-24 s; PS2's trim
    matches its bank to the chip, and HZ's mix shares PS2's scale)
  - the EFFECTS split holds with Q: music off is silent until the first
    gunshot (B at frame 400), and the shots sound
  - a switch to Q mid-song (the AUDIO page, frame 300) keeps the sound
    going: the second after it isn't silent
  - (session 37) the drums (assets/sound/q_drums.json), through hz_qdrums
    strike: ABGRUND's kick/tom instrument (song 3, instrument 8) alone,
    its 147 Hz notes PS2's kick (under 70 Hz 30 ms in), the kick within 3
    dB of the chip's over its first 60 ms (session 39: within 1 dB,
    K-weighted over the hit, as the level pass); EISFREI's (song 8, instrument
    9) the same. (Session 38) Its fill notes PS2's toms at the chip's own
    pitch: half the note's frequency (the instrument's operators all at
    multiplier 1/2), which the chip then slides down fast; Q's tom 30 ms in
    0-4 semitones under the chip's strike, as fitted (median; each note
    within -6 to +3), and the toms within 3 dB of the chip's over 150 ms
    (session 41: 2 dB over it, within 1.5 dB: the tom models turned up)
  - (session 38) a switch to Q keeps the notes held across it: the title's
    song (its long notes) switched NUKED -> Q at 5 s (session 39: 10 s
    until the song played once, as the title plays it: ~10 s long) against
    Q from the start, the next 3 s within 1.5 dB (hz_qdrums switch; before the fix
    4.8 dB under)
  - (session 38) VOICES (hz_qdrums voices): effect $02 (it holds FM1, the
    music's channel there muted, for 256 frames) over the title's song at
    5 s: on CHIP the music under it over 4 s more than 1 dB under the
    music alone (its FM1 part gone, as on the chip); on EXTRA within 0.5
    dB of it (the music plays on)
  - (session 39) REPEATS (hz_qdrums repeats, no drum table): LOCH's song
    (5) through the mixer, Q's 20 ms envelope against the chip's: with
    CHIP (a repeated note's attack from the voice's level, as the chip)
    it correlates 0.1 or more better than with FRESH (PS2's fresh voice
    and fading tail) and above 0.85; the AUDIO page's REPEATS row, stepped
    with Q on, writes "repeats": "chip" and the sound goes on (session 42:
    CHIP the default, so stepped once it writes "fresh")
  - (session 46) the booming snare (booming_snares: its dive, its boom a
    little under the chip's, the hit at the chip's level; songs 3 and 8's
    50-150 Hz at Nuked's), and the kick at the chip's own pitch 30 ms in
    (the long kick follows the channel's pitch now)
  - (session 48) OVERSAMPLE (grain: the bank at the chip's rate): under
    song 6's parallel thirds (instrument 0, algorithm 0, feedback 7, FM1
    and FM2) the floor between the notes' partials at 1X within 1.5 dB of
    the chip's over 500 Hz, at 4X 2 dB or more under it from 2 kHz; the
    AUDIO page's OVERSAMPLE row, stepped with Q on, writes "oversample": 2
    and the sound goes on
  - (session 55) after an effect on FM6, the song's closed hat there (a
    drum model) ends the effect's note: ABGRUND against the CPU, the
    8.6-9.0 kHz band after the CPU's ORDERED within 3 dB of the chip's
    (effect_then_hats; it was 14 dB over: a held tone)
"""

import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

SPF = 896040 * 48000 / 53693175          # WAV frames a game frame
failures = []


def run(binary, assets, d, frames, settings, keys='', game=('--map', '1')):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, 'settings.json'), 'w') as f:
        f.write(settings)
    env = dict(os.environ, HZ_SAVES=d, HZ_MODS='', HZ_FRAMES=str(frames),
               HZ_OUT=os.path.join(d, 'last.png'),
               HZ_WAV=os.path.join(d, 'out.wav'))
    env.pop('HZ_KEYS', None)
    if keys:
        env['HZ_KEYS'] = keys
    subprocess.run([binary, '--assets', assets, *game], env=env,
                   check=True, capture_output=True)
    w = wave.open(os.path.join(d, 'out.wav'))
    a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
    return a.astype(float).reshape(-1, 2)


def check(name, ok, detail=''):
    print('%-48s %s %s' % (name, 'ok' if ok else 'FAILED', detail))
    if not ok:
        failures.append(name)


def kweight(x, sr):
    """ITU-R BS.1770's K-weighting at any rate (pyloudnorm's design)."""
    from scipy.signal import lfilter

    def biquad(shelf, gain, q, fc):
        a_ = 10 ** (gain / 40)
        w0 = 2 * np.pi * fc / sr
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
    x = lfilter(*biquad(True, 3.999843853973347, 0.7071752369554196,
                        1681.974450955533), x)
    return lfilter(*biquad(False, 0, 0.5003270373238773, 38.13547087602444),
                   x)


def tuned_snares(qdrums, assets, tmp, low_notes=False, song=0, inst=4,
                 split=170, seconds=12):
    """(Session 40) The title's song's instrument 4 above its kick: the
    chip's MUL 1/2 patch with op 1 at feedback 7 sounds a tuned snare -- a
    tone that holds 20 ms and dives (half its pitch by 200 ms), a noise
    burst 13 dB under it, and a static (op 1's noise through op 2) about
    35 dB under it to the next note. Each note's band levels against its
    own tone at the strike, chip and Q's tuned snare: the static (2-12
    kHz, 45-100 ms) within 3 dB, the burst (2-12 kHz, the strike) within 3
    dB, the pitch at 100 ms within 8%. (Session 43) low_notes: its notes
    at 170 Hz and under (1B2B, 165 Hz: a run of sixteenths with ruffs, the same
    voice an octave down; PS2's kick until the owner heard a snare), the
    model "low tuned snare": the burst and the static (45-80 ms); since
    session 44 PS2's kick struck with them (the model's "with"), so these
    against the chip's as they stand, and the kick's thump: 30-150 Hz over
    the first 60 ms 1.5-5 dB over the chip's. (Session 45) song, inst,
    split, seconds: the endings' and staff roll's songs (15-17), whose
    kick/tom instrument's notes over the kick (split, Hz) are the same
    voice, the model "soft-wire tuned snare" (the static 4 dB under the
    title's): the burst, the static and the dive as one check a song."""
    from scipy.signal import lfilter
    sr = 53267
    wav = os.path.join(tmp, 'snares%d.wav' % song)
    subprocess.run([qdrums, assets, 'strike', str(song), '%X' % inst,
                    str(seconds), wav, wav + '.txt'], check=True,
                   capture_output=True)

    def load(p):
        w = wave.open(p)
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
        return lfilter([1, -1], [1, -0.9995], x.astype(float))
    chip, q = load(wav), load(wav + '.q.wav')
    f = np.fft.rfftfreq(8192, 1 / sr)
    low, high = (f > 30) & (f < 1500), (f > 2000) & (f < 12000)

    def frame(x, k):
        sp = np.abs(np.fft.rfft(x[k:k + 1024] * np.hanning(1024), 8192)) ** 2
        return (f[low][np.argmax(sp[low])], 10 * np.log10(sp[low].sum()),
                10 * np.log10(sp[high].sum()))
    rows = [ln.split() for ln in open(wav + '.txt')]
    starts = sorted(set(int(r[0]) for r in rows))
    got = {'chip': ([], [], []), 'q': ([], [], [])}
    for t in starts:
        fn = int([r for r in rows if int(r[0]) == t][0][2], 16)
        hz = (fn & 0x7FF) * (7670442.0 / 144) / (1 << (21 - ((fn >> 11) & 7)))
        later = [u for u in starts if u > t]
        if (hz <= split) != low_notes:
            continue
        gap = int((0.1 if low_notes else 0.12) * sr)
        if later and later[0] - t < gap:
            continue
        for name, x in (('chip', chip), ('q', q)):
            f0, l0, h0 = frame(x, t)
            static = np.mean([frame(x, t + int(ms * sr / 1000))[2]
                              for ms in ((45, 60, 80) if low_notes
                                         else (45, 60, 80, 100))])
            if low_notes:
                # (session 44) a kick struck with them ("with"): the
                # snare's noise against the chip's as it stands (the kick
                # moves the tone it was measured against), and the thump:
                # 30-150 Hz over the first 60 ms
                seg = x[t:t + int(0.06 * sr)]
                sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 8192))
                fb = np.fft.rfftfreq(8192, 1 / sr)
                got[name][0].append(h0)
                got[name][1].append(static)
                got[name][2].append(10 * np.log10(
                    (sp[(fb > 30) & (fb < 150)] ** 2).sum()))
                continue
            got[name][0].append(h0 - l0)
            got[name][1].append(static - l0)
            if not low_notes:
                got[name][2].append(frame(x, t + int(0.1 * sr))[0] / f0)
    med = {k: [float(np.median(v)) if v else 0.0 for v in got[k]]
           for k in got}
    c, g = med['chip'], med['q']
    if low_notes:
        check('drums: song 0 low tuned snares: the burst',
              abs(g[0] - c[0]) < 3, 'Q %+.1f dB against the chip, 2-12 kHz '
              '(%d notes)' % (g[0] - c[0], len(got['q'][0])))
        check('drums: song 0 low tuned snares: the static',
              abs(g[1] - c[1]) < 3, 'Q %+.1f dB against the chip at 45-80 ms'
              % (g[1] - c[1]))
        check('drums: song 0 low tuned snares: the kick under them',
              1.5 < g[2] - c[2] < 5, 'Q %+.1f dB against the chip, 30-150 Hz '
              'over the first 60 ms' % (g[2] - c[2]))
        return
    if song != 0:
        ok = (abs(g[0] - c[0]) < 3 and abs(g[1] - c[1]) < 3
              and abs(g[2] / c[2] - 1) < 0.08)
        check('drums: song %d tuned snares' % song, ok,
              'burst chip %+.1f Q %+.1f, static %+.1f / %+.1f dB, x%.2f / '
              'x%.2f at 100 ms (%d notes)' % (c[0], g[0], c[1], g[1], c[2],
                                              g[2], len(got['q'][0])))
        return
    check('drums: song 0 tuned snares: the burst',
          abs(g[0] - c[0]) < 3, 'chip %+.1f, Q %+.1f dB under the tone '
          '(%d notes)' % (c[0], g[0], len(got['q'][0])))
    check('drums: song 0 tuned snares: the static',
          abs(g[1] - c[1]) < 3, 'chip %+.1f, Q %+.1f dB at 45-100 ms'
          % (c[1], g[1]))
    check('drums: song 0 tuned snares: the dive',
          abs(g[2] / c[2] - 1) < 0.08, 'pitch at 100 ms: chip x%.2f, Q x%.2f'
          % (c[2], g[2]))


def long_kick(qdrums, assets, tmp):
    """(Session 45) The chip's kick (the kick/tom patch's lowest note) is a
    boom diving from about 65 Hz to 25 over 250 ms, ringing on at its
    strike's level to the next note, where PS2's kick was gone by 120 ms.
    The model "long kick" (PS2's kick, its body 1.5 s; session 46: after
    its glide, the chip channel's own pitch, following the driver's slide
    down to 30 Hz): song 5's kicks against the chip's, 30-150 Hz,
    each window against the chip's first 20 ms, at 60-120 and 120-250 ms
    within 3 dB; and cut by the next note on the channel (the model's
    "cut"), as the chip's key-on: under 60 Hz, 30-80 ms into a tom struck
    within 300 ms of a kick, within 3 dB of the chip's."""
    from scipy.signal import butter, lfilter, sosfilt
    sr = 53267
    wav = os.path.join(tmp, 'kicks5.wav')
    subprocess.run([qdrums, assets, 'strike', '5', '5', '60', wav,
                    wav + '.txt'], check=True, capture_output=True)

    def load(p):
        w = wave.open(p)
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
        return lfilter([1, -1], [1, -0.9995], x.astype(float))
    chip, q = load(wav), load(wav + '.q.wav')
    body = butter(4, [30, 150], 'bandpass', fs=sr, output='sos')
    sub = butter(4, [25, 60], 'bandpass', fs=sr, output='sos')
    rows = [ln.split() for ln in open(wav + '.txt')]
    starts = sorted(set(int(r[0]) for r in rows))
    hz = {}
    for r in rows:
        fn = int(r[2], 16)
        hz[int(r[0])] = ((fn & 0x7FF) * (7670442.0 / 144)
                         / (1 << (21 - ((fn >> 11) & 7))))

    def db(x, a, b):
        return 10 * np.log10((x[a:b] ** 2).mean() + 1e-9)
    ring = {'chip': [], 'q': []}
    into = {'chip': [], 'q': []}
    for name, x in (('chip', chip), ('q', q)):
        xb, xs = sosfilt(body, x), sosfilt(sub, x)
        for i, t in enumerate(starts):
            nxt = starts[i + 1] if i + 1 < len(starts) else len(x)
            if hz[t] <= 134.9 and nxt - t > int(0.25 * sr):
                ms = [int(v * sr / 1000) for v in (0, 20, 60, 120, 250)]
                s0 = db(xb, t + ms[0], t + ms[1])
                ring[name].append((db(xb, t + ms[2], t + ms[3]) - s0,
                                   db(xb, t + ms[3], t + ms[4]) - s0))
            if (i > 0 and hz[starts[i - 1]] <= 134.9 and hz[t] > 134.9
                    and t - starts[i - 1] < int(0.3 * sr)):
                into[name].append(db(xs, t + int(0.03 * sr),
                                     t + int(0.08 * sr)))
    c, g = np.median(ring['chip'], 0), np.median(ring['q'], 0)
    check('drums: song 5 long kick rings as the chip\'s',
          abs(g[0] - c[0]) < 3 and abs(g[1] - c[1]) < 3,
          '60-120 ms chip %+.1f Q %+.1f, 120-250 ms %+.1f / %+.1f dB'
          % (c[0], g[0], c[1], g[1]))
    d = float(np.median(into['q']) - np.median(into['chip']))
    check('drums: song 5 long kick cut by the next note', abs(d) < 3,
          'under 60 Hz into the tom: Q %+.1f dB against the chip (%d)'
          % (d, len(into['q'])))


def song_switch(qdrums, assets, tmp):
    """(Session 45) "A full retreat" (16) into "Go with the stream" (17),
    as the music test plays them: 16's last note on FM5 was left the
    channel's voice while 17's hat instrument there played drums, and took
    its patch and slow release: a tone at 4.4 kHz, 23 dB up, ringing on.
    17 after 16 against 17 alone, 4.3-4.5 kHz, 2-10 s after the switch:
    within 3 dB."""
    from scipy.signal import butter, sosfilt
    band = butter(4, [4300, 4500], 'bandpass', fs=48000, output='sos')

    def level(name, songs, seconds, at):
        p = os.path.join(tmp, name + '.wav')
        subprocess.run([qdrums, assets, 'song', songs, str(seconds), 'q', p],
                       check=True, capture_output=True)
        w = wave.open(p)
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
        y = sosfilt(band, x.astype(float).reshape(-1, 2).mean(1))
        y = y[int((at + 2) * 48000):int((at + 10) * 48000)]
        return 10 * np.log10((y ** 2).mean() + 1e-9)
    d = level('switch1617', '16,17@20', 30, 20) - level('alone17', '17', 10, 0)
    check('switch: no note of song 16 rings on into 17', abs(d) < 3,
          '%+.1f dB at 4.4 kHz against 17 alone' % d)


def effect_then_hats(binary, assets, tmp):
    """(Session 55) ABGRUND against the CPU, no input: the CPU's ORDERED
    (frame 395, FM6) ends at 415, and the song's closed hat on FM6, a drum
    model, strikes from 417. Its strike skipped the bookkeeping that ends
    an effect's note on its channel, so the effect's released voice took
    each hat's registers -- operators at MUL 15 on D5, RR 0 -- and held a
    tone at 8.8 kHz, 14 dB over the chip's band, until UNIT READY (523)
    took FM6. Q against the chip, 8.6-9.0 kHz, frames 430-515: within 3
    dB."""
    from scipy.signal import butter, sosfilt
    band = butter(4, [8600, 9000], 'bandpass', fs=48000, output='sos')
    game = ('--map', '0', '--mode', '1', '--difficulty', '0')

    def level(synth):
        x = run(binary, assets, os.path.join(tmp, 'hats_' + synth), 560,
                '{"synth": "%s"}' % synth, game=game)
        y = sosfilt(band, x.mean(1))[int(430 * SPF):int(515 * SPF)]
        return 10 * np.log10((y ** 2).mean() + 1e-9)
    d = level('q') - level('nuked')
    check('effects: no tone after one on a drum\'s channel', abs(d) < 3,
          '%+.1f dB at 8.8 kHz against the chip' % d)


def booming_snares(qdrums, assets, tmp):
    """(Session 46) The map songs' ringing snare (songs 3, 6, 8, 15-17):
    on the chip its tone, half the note's frequency (147 Hz on 22D3), dives
    with the driver's release bend from the note's key-off, an octave in
    about 120 ms at song 3's tempo, and rings within 3 dB of its strike
    until the next key-on on the channel; PS2's snare had none of it, so
    those songs' 50-150 Hz sat 2-4 dB under the chip's. The model "booming
    snare" is PS2's snare with "snare boom" (a sine following the chip
    channel's pitch, "follow" 0.5, decaying over 0.5 s, cut by the next
    key-on), the boom about 4 dB under the chip's hit and PS2's snare the
    rest, the hit at the chip's level. Song 3's snare (instrument 7)
    alone against the chip, each note up to the channel's next key-on (the
    kick/tom instrument 8's included): the boom's pitch (30-300 Hz) 100 ms
    in within 2 semitones of the chip's; 60-150 Hz at 60-120 ms against
    the chip's within -6 to +1 dB (a little under, by design); the hit
    K-weighted to 150 ms within 1 dB. And the mix: songs 3 and 8 through
    the game's mixer (REPEATS CHIP), Q's 50-150 Hz energy against Nuked's
    within 2 dB (4.1 under before)."""
    from scipy.signal import butter, lfilter, sosfilt
    sr = 53267
    wav = os.path.join(tmp, 'snare3.wav')
    other = os.path.join(tmp, 'snare3_kicks.wav')
    for inst, out in (('7', wav), ('8', other)):
        subprocess.run([qdrums, assets, 'strike', '3', inst, '40', out,
                        out + '.txt'], check=True, capture_output=True)

    def load(p):
        w = wave.open(p)
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
        return lfilter([1, -1], [1, -0.9995], x.astype(float))
    chip, q = load(wav), load(wav + '.q.wav')
    rows = [ln.split() for ln in open(wav + '.txt')]
    strikes = sorted(int(r[0]) for r in rows)
    keyons = sorted(set(strikes) | set(int(ln.split()[0])
                                       for ln in open(other + '.txt')))

    def boom_pitch(x, s, at):
        g = x[s + int(at * sr):s + int(at * sr) + int(0.04 * sr)]
        sp = np.abs(np.fft.rfft(g * np.hanning(len(g)), 65536))
        f = np.fft.rfftfreq(65536, 1 / sr)
        m = (f > 30) & (f < 300)
        return f[m][np.argmax(sp[m])]
    band = butter(4, [60, 150], 'bandpass', fs=sr, output='sos')
    bc, bq = sosfilt(band, chip), sosfilt(band, q)
    kc, kq = kweight(chip, sr), kweight(q, sr)
    st, lv, hit = [], [], []
    for s in strikes:
        later = [u for u in keyons if u > s]
        end = later[0] if later else len(chip)
        if end - s > int(0.15 * sr):
            st.append(12 * np.log2(boom_pitch(q, s, 0.1)
                                   / boom_pitch(chip, s, 0.1)))
            a, b = s + int(0.06 * sr), s + int(0.12 * sr)
            lv.append(10 * np.log10((bq[a:b] ** 2).mean()
                                    / (bc[a:b] ** 2).mean()))
        later = [u for u in strikes if u > s]
        n = min(int(0.15 * sr), later[0] - s if later else 10 ** 9)
        hit.append(10 * np.log10((kq[s:s + n] ** 2).mean()
                                 / (kc[s:s + n] ** 2).mean()))
    check('drums: song 3 snare booms with the chip\'s dive',
          len(st) > 4 and abs(np.median(st)) < 2,
          '%d notes, at 100 ms %+.1f st from the chip\'s'
          % (len(st), np.median(st)) if st else 'none')
    check('drums: song 3 snare boom under the chip\'s',
          len(lv) > 4 and -6 < np.median(lv) < 1,
          '60-150 Hz at 60-120 ms %+.1f dB against the chip'
          % np.median(lv) if lv else 'none')
    check('drums: song 3 booming snare at the chip\'s level',
          abs(np.median(hit)) < 1, '%+.1f dB (K-weighted)' % np.median(hit))
    low = butter(4, [50, 150], 'bandpass', fs=48000, output='sos')
    env = dict(os.environ, PS2_Q_PARALLEL='0')
    for song in (3, 8):
        level = {}
        for synth in ('nuked', 'q'):
            p = os.path.join(tmp, 'bottom%d%s.wav' % (song, synth))
            subprocess.run([qdrums, assets, 'song', str(song), '40', synth,
                            p], check=True, capture_output=True, env=env)
            w = wave.open(p)
            x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
            y = sosfilt(low, x.astype(float).reshape(-1, 2).mean(1))
            level[synth] = 10 * np.log10((y[48000:] ** 2).mean() + 1e-9)
        d = level['q'] - level['nuked']
        check('mix: song %d 50-150 Hz at Nuked\'s' % song, abs(d) < 2,
              'Q %+.1f dB against Nuked' % d)


def grain(qdrums, assets, tmp):
    """(Session 48) The chip runs at 53,267 Hz and folds the partials
    phase modulation throws past that rate straight back into the band: on
    a feedback-heavy patch, a floor of hash between the notes' partials.
    Q's operators run oversampled and low-passed (OVERSAMPLE 2, 4), so its
    notes ring cleaner; at 1 (HZ's default) the bank, at the chip's rate,
    folds them as the chip does. Song 6's instrument 0 (algorithm 0,
    feedback 7) in parallel thirds on FM1 and FM2 from 21 s, alone through
    hz_qdrums strike: 60 ms into each pair of notes, the 20th-percentile
    FFT bin against the band's mean, over 500 Hz."""
    sr = 53267
    bands = ((500, 2000), (2000, 8000), (8000, 20000))

    def floors(path, starts):
        w = wave.open(path)
        x = np.frombuffer(w.readframes(w.getnframes()),
                          dtype='<i2').astype(float)
        n = 8192
        win = np.hanning(n)
        f = np.fft.rfftfreq(n, 1 / sr)
        acc = [[] for _ in bands]
        for s0 in starts:
            seg = x[s0 + int(0.06 * sr):s0 + int(0.06 * sr) + n]
            if len(seg) < n:
                continue
            sp = np.abs(np.fft.rfft(seg * win)) ** 2
            for k, (lo, hi) in enumerate(bands):
                b = sp[(f >= lo) & (f < hi)]
                acc[k].append(10 * np.log10(np.percentile(b, 20) / b.mean()))
        return [float(np.median(a)) for a in acc]

    got = {}
    for times in (1, 4):
        wav = os.path.join(tmp, 'grain%d.wav' % times)
        env = dict(os.environ, PS2_Q_OVERSAMPLE=str(times),
                   PS2_Q_PARALLEL='0')
        subprocess.run([qdrums, assets, 'strike', '6', '0', '60', wav,
                        wav + '.txt'], env=env, check=True,
                       capture_output=True)
        starts = sorted({int(ln.split()[0]) for ln in open(wav + '.txt')
                         if ln.split()[1] in ('1', '2')
                         and int(ln.split()[0]) > 21 * sr
                         and int(ln.split()[3]) < 60})
        got['chip'] = floors(wav, starts)
        got[times] = floors(wav + '.q.wav', starts)
    near = [q - c for q, c in zip(got[1], got['chip'])]
    clean = [q - c for q, c in zip(got[4], got['chip'])]
    text = lambda d: ' / '.join('%+.1f' % v for v in d)
    check("grain: song 6 thirds at 1X as the chip's",
          max(map(abs, near)) < 1.5,
          '%s dB against the chip (0.5-2k / 2-8k / 8-20k)' % text(near))
    check('grain: song 6 thirds at 4X cleaner', max(clean[1:]) <= -2,
          '%s dB against the chip' % text(clean))


def drums(qdrums, assets, tmp):
    from scipy.signal import lfilter
    sr = 53267

    def load(p, k=1.0):
        w = wave.open(p)
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
        return lfilter([1, -1], [1, -0.9995], x.astype(float) * k)

    def pitch(x, s, at):
        g = x[s + int(at * sr):s + int(at * sr) + int(0.02 * sr)]
        sp = np.abs(np.fft.rfft(g * np.hanning(len(g)), 16384))
        f = np.fft.rfftfreq(16384, 1 / sr)
        m = (f > 30) & (f < 3000)
        return f[m][np.argmax(sp[m])]
    def nominal(fn):
        return ((fn & 0x7FF) * (7670442.0 / 144)
                / (1 << (21 - ((fn >> 11) & 7))))
    for song, inst, kick in ((3, '8', '1AD3'), (8, '9', '1AD3')):
        wav = os.path.join(tmp, 'drums%d.wav' % song)
        txt = os.path.join(tmp, 'drums%d.txt' % song)
        subprocess.run([qdrums, assets, 'strike', str(song), inst, '40', wav,
                        txt], check=True, capture_output=True)
        chip, q = load(wav), load(wav + '.q.wav')
        rows = [ln.split() for ln in open(txt)]
        ks = [int(r[0]) for r in rows if r[2] == kick][:20]
        ts = [(int(r[0]), int(r[2], 16)) for r in rows if r[2] != kick][:20]
        kp = np.median([pitch(q, s, 0.03) for s in ks])
        kc = np.median([pitch(chip, s, 0.03) for s in ks])
        st = [12 * np.log2(pitch(q, s, 0.03) / (0.5 * nominal(f)))
              for s, f in ts]
        # (session 46) the long kick follows the chip's pitch: 30 ms in,
        # past PS2's glide, within 2 semitones of the chip's kick (PS2's
        # was a fixed 52 Hz: under 70 Hz was the check)
        check('drums: song %d kick notes are the kick' % song,
              abs(12 * np.log2(kp / kc)) < 2,
              '%.0f Hz at 30 ms, the chip %.0f' % (kp, kc))
        check('drums: song %d fill toms at the chip\'s pitch' % song,
              len(st) > 0 and -4 <= np.median(st) <= 0
              and all(-6 <= x <= 3 for x in st),
              '%d notes, median %+.1f st (%+.1f..%+.1f) from the chip\'s'
              ' strike' % (len(st), np.median(st), min(st), max(st))
              if st else 'none')
        n = int(0.15 * sr)
        tl = [20 * np.log10(np.sqrt((q[s:s + n] ** 2).mean())
                            / np.sqrt((chip[s:s + n] ** 2).mean()))
              for s, f in ts]
        # (session 41) the tom models 2 dB up on the chip's (The owner: turn
        # the toms up a little)
        check('drums: song %d toms 2 dB over the chip' % song,
              len(tl) > 0 and abs(np.median(tl) - 2) < 1.5,
              '%+.1f dB' % np.median(tl) if tl else 'none')
        # (session 39) the level pass's measure: K-weighted (ITU-R BS.1770)
        # over each kick to the next strike or 150 ms at most; A-weighting
        # takes 30 dB off a kick's 50 Hz, 60 ms leaves out the chip's ring
        starts = sorted(set(int(r[0]) for r in rows))
        kc, kq = kweight(chip, sr), kweight(q, sr)
        lv = []
        for s in sorted(set(int(r[0]) for r in rows if r[2] == kick)):
            later = [u for u in starts if u > s]
            n = min(int(0.15 * sr), later[0] - s if later else 10 ** 9)
            lv.append(10 * np.log10((kq[s:s + n] ** 2).mean()
                                    / (kc[s:s + n] ** 2).mean()))
        check('drums: song %d kick at the chip\'s level' % song,
              abs(np.median(lv)) < 1, '%+.1f dB (K-weighted)'
              % np.median(lv))


def main():
    binary, assets = sys.argv[1], sys.argv[2]
    tmp = tempfile.mkdtemp(prefix='hzq')
    # The notes and the level, Q's voicing alone: the assets without the
    # drum table (the drums are PS2's, on purpose not the chip's).
    import shutil
    plain = os.path.join(tmp, 'assets_plain')
    shutil.copytree(assets, plain)
    table = os.path.join(plain, 'sound', 'q_drums.json')
    if os.path.exists(table):
        os.remove(table)
    chip = run(binary, plain, os.path.join(tmp, 'chip'), 1500,
               '{"synth": "nuked"}')
    q = run(binary, plain, os.path.join(tmp, 'q'), 1500, '{"synth": "q"}')

    def env(x):
        win = 2400
        m = x[:len(x) // win * win].reshape(-1, win, 2)
        return np.sqrt((m ** 2).mean(axis=(1, 2)))
    c = np.corrcoef(env(chip)[50:], env(q)[50:])[0, 1]
    check('notes: the envelopes correlate', c > 0.9, '%.3f' % c)

    def rms(x):
        s = x[48000 * 5:48000 * 24]
        return 20 * np.log10(np.sqrt((s ** 2).mean()) / 32768)
    a, b = rms(chip), rms(q)
    check('level: within 3 dB of the chip', abs(a - b) < 3,
          'chip %.1f dBFS, Q %.1f' % (a, b))

    m = run(binary, assets, os.path.join(tmp, 'music_off'), 700,
            '{"synth": "q", "audio": {"music": "off"}}',
            keys='400:B,402:-,420:B,422:-,440:B,442:-')
    before = np.abs(m[:int(380 * SPF)]).max()
    shots = np.abs(m[int(400 * SPF):int(500 * SPF)]).max()
    check('effects: music off silent until the shots', before == 0
          and shots > 1000, 'before %d, shots %d' % (before, shots))
    effect_then_hats(binary, assets, tmp)

    # Esc, down to AUDIO (four rows), in, down to SYNTH (three), right.
    keys = ','.join('%d:%s,%d:-' % (300 + 6 * i, k, 302 + 6 * i)
                    for i, k in enumerate('EDDDDSDDDR'))
    s = run(binary, assets, os.path.join(tmp, 'switch'), 500,
            '{"synth": "nuked"}', keys=keys)
    after = np.sqrt((s[int(370 * SPF):int(430 * SPF)] ** 2).mean())
    with open(os.path.join(tmp, 'switch', 'settings.json')) as f:
        wrote = '"synth": "q"' in f.read()
    check('switch: Q from the AUDIO page, sound goes on',
          wrote and after > 300, 'rms after %.0f, written %s' % (after, wrote))

    if len(sys.argv) > 3:
        drums(sys.argv[3], assets, tmp)
        tuned_snares(sys.argv[3], assets, tmp)
        tuned_snares(sys.argv[3], assets, tmp, low_notes=True)
        long_kick(sys.argv[3], assets, tmp)
        song_switch(sys.argv[3], assets, tmp)
        booming_snares(sys.argv[3], assets, tmp)
        grain(sys.argv[3], assets, tmp)
        for song, inst in ((15, 5), (16, 6), (17, 7)):
            tuned_snares(sys.argv[3], assets, tmp, song=song, inst=inst,
                         split=151.4, seconds=60)
        out = subprocess.run([sys.argv[3], assets, 'switch', '0', '5', '3'],
                             check=True, capture_output=True, text=True)
        mean = float(out.stdout.strip().splitlines()[-1].split()[1])
        check('switch: notes held across it go on', abs(mean) < 1.5,
              'the next 3 s %+.1f dB against Q from the start' % mean)
        # (session 50) the staff roll's song, its drums on fm6, under
        # hq_destroyed ($18, fm6 and the noise for seconds): until now
        # effect $02 of the title, read from $F000 (the effects' table
        # wasn't set, song bytes), which the songs' own windows moved.
        out = subprocess.run([sys.argv[3], assets, 'voices', '17', '18',
                              '5', '4'], check=True, capture_output=True,
                             text=True)
        under = {}
        for line in out.stdout.splitlines():
            under[line.split()[0]] = float(line.split('(')[1].split(')')[0])
        check('voices: CHIP mutes the effect\'s channel', under['chip'] < -1,
              '%+.1f dB under the effect' % under['chip'])
        check('voices: EXTRA plays the music on', abs(under['extra']) < 0.5,
              '%+.1f dB under the effect' % under['extra'])

    # (session 39) REPEATS: Esc, down to AUDIO, in, down to REPEATS (five),
    # right.
    keys = ','.join('%d:%s,%d:-' % (300 + 6 * i, k, 302 + 6 * i)
                    for i, k in enumerate('EDDDDSDDDDDR'))
    s = run(binary, assets, os.path.join(tmp, 'repeats'), 500,
            '{"synth": "q"}', keys=keys)
    after = np.sqrt((s[int(390 * SPF):int(450 * SPF)] ** 2).mean())
    with open(os.path.join(tmp, 'repeats', 'settings.json')) as f:
        wrote = '"repeats": "fresh"' in f.read()
    check('repeats: FRESH from the AUDIO page (CHIP the default), sound '
          'goes on',
          wrote and after > 300, 'rms after %.0f, written %s' % (after, wrote))
    if len(sys.argv) > 3:
        out = subprocess.run([sys.argv[3], assets, 'repeats', '5', '30'],
                             check=True, capture_output=True, text=True)
        r = {}
        for line in out.stdout.splitlines():
            r[line.split()[2]] = float(line.split()[-1])
        check('repeats: CHIP tracks the chip better than FRESH',
              r['chip'] > 0.85 and r['chip'] - r['fresh'] >= 0.1,
              'r fresh %.3f, chip %.3f' % (r['fresh'], r['chip']))

    # (session 48) OVERSAMPLE: Esc, down to AUDIO, in, down to OVERSAMPLE
    # (six), right: 1X -> 2X.
    keys = ','.join('%d:%s,%d:-' % (300 + 6 * i, k, 302 + 6 * i)
                    for i, k in enumerate('EDDDDSDDDDDDR'))
    s = run(binary, assets, os.path.join(tmp, 'oversample'), 500,
            '{"synth": "q"}', keys=keys)
    after = np.sqrt((s[int(390 * SPF):int(450 * SPF)] ** 2).mean())
    with open(os.path.join(tmp, 'oversample', 'settings.json')) as f:
        wrote = '"oversample": 2' in f.read()
    check('oversample: 2X from the AUDIO page (1X the default), sound '
          'goes on',
          wrote and after > 300, 'rms after %.0f, written %s' % (after, wrote))

    print('all agree' if not failures else '%d failed' % len(failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
