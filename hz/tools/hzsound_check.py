#!/usr/bin/env python3
"""Check the C sound driver against the original, write for write.

    python3 tools/hzsound_check.py ROM ASSETS [FIRST [LAST]] [--cache DIR]
                                   [--stop]

Each case is a script of mailbox commands by frame (songs, sfx, fades,
stops, pauses, bad commands) on a bank. The original driver runs on
tools/z80run.py's Z80 interpreter in its zero-time mode, the C port
(src/audio/hz_snd.c) in ./hz_sound on the same script; both chip-write
logs (`t part reg value`, t in master clocks) must be identical.

Cases: every song (40 s, repeating), every sfx, sfx over the map songs,
stops, pauses, fades, songs played once -- all on the game's SoundData
(the song text alone, hz_sound --text: the ROM's bytes, which the original
reads; the trackers the game reads play the same, `make roundtrip`);
then made-up banks (make_bank): songs and effects built from every flag
and byte the parsers know, LFO, noise, repeats, waits, slides, instruments
of random bytes, played with sfx of every priority, pauses with effects
held, song changes, unknown commands. The shipping data never reaches
much of that (no song uses the LFO or a music noise mode). FIRST/LAST pick
a range of cases (the sandbox's 300 s limit). --cache DIR keeps the
original's logs (keyed by the case's script and bank) and reuses them:
the mutation tests (tools/hz_mutate.py, CHECK 'sound') run only the port.
--stop ends at the first case that differs.
"""
import hashlib
import os
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z80run  # noqa: E402

SFX = 29
SONGS = 18


RANDOM_BANKS = 80
HEAVY = 60            # from this seed on: pause-heavy banks (see make_bank)
SET_UP = [(0, [0x0A, 0x00, 0x07, 0x80, 0x00]),     # Snd_SetBank $78000
          (0, [0x07, 0x80, 0x00]),                 # Snd_SetSong $8000
          (0, [0x08, 0xFA, 0x00])]                 # Snd_SetSfxTable


def script_for(prio, song=None, repeat=False, sfx=(), fade=None, stop=(),
               pause=()):
    """z80run.py's command line as a script."""
    script = list(SET_UP)
    if song is not None:
        script.append((1, [0x09, 0x00, song]))
        if repeat:
            script.append((1, [0x01, 0xFF]))
    for f, n in sfx:
        script.append((f, [0x02, n, prio[n]]))
    if fade is not None:
        script.append((fade[0], [0x03, fade[1], 0]))
    for f in stop:
        script.append((f, [0x04]))
    for f in pause:
        script.append((f, [0x0C, 0, 0]))
    script.sort(key=lambda x: x[0])
    return script


def shipping_cases(prio):
    out = []
    for n in range(SONGS):
        out.append(('song %d' % n, 40, script_for(prio, n, True), None))
    out.append(('every sfx', 45, script_for(
        prio, sfx=[(10 + 90 * n, n) for n in range(SFX)]), None))
    rng = random.Random(25)
    for song in range(3, 11):
        sfx = []
        f = 20
        while f < 1500:
            sfx.append((f, rng.randrange(SFX)))
            f += rng.choice((1, 2, 3, 5, 8, 13, 30, 60))
        out.append(('song %d + sfx' % song, 26,
                    script_for(prio, song, True, sfx), None))
    for song in (3, 6, 9):
        sfx = [(30 + 40 * k, (song * 7 + k) % SFX) for k in range(20)]
        out.append(('song %d sfx stops' % song, 16, script_for(
            prio, song, True, sfx, stop=[45 + 40 * k
                                         for k in range(0, 20, 3)]), None))
        out.append(('song %d pauses' % song, 16, script_for(
            prio, song, True, [(100, 3), (240, 5), (250, 9), (500, 12),
                               (598, 2), (599, 14), (701, 20)],
            pause=[200, 260, 400, 402, 600, 700, 702, 720]), None))
        out.append(('song %d fade' % song, 20, script_for(
            prio, song, True, [(300, 2), (600, 4)],
            fade=(120, song % 4 + 1)), None))
        out.append(('song %d stopped' % song, 8, script_for(
            prio, song, True, [(100, 1), (200, 7)], fade=(150, 0)), None))
    for song in (0, 1, 2, 11, 17):
        out.append(('song %d once' % song, 60, script_for(prio, song),
                    None))
    # played once and paused every 37 frames: some pauses land in the
    # releases of PSG tracks whose bits have cleared at their ends
    for song in (0, 1, 2, 11, 17):
        pause = []
        for f in range(40, 3500, 37):
            pause += [f, f + 2]
        out.append(('song %d once, paused' % song, 60,
                    script_for(prio, song, pause=pause), None))
    # stopped ($03 0) and started again, over and over: stops that land
    # in the PSG tracks' envelopes
    for song in range(3, 11):
        script = script_for(prio)
        f = 1
        rng2 = random.Random(song)
        while f < 1100:
            script.append((f, [0x09, 0x00, song]))
            script.append((f, [0x01, 0xFF]))
            f += rng2.randrange(20, 90)
            script.append((f, [0x03, 0x00, 0x00]))
            f += rng2.randrange(2, 30)
        script.sort(key=lambda x: x[0])
        out.append(('song %d stop/start' % song, 20, script, None))
    return out


# --- made-up banks --------------------------------------------------------

class Bank:
    def __init__(self):
        self.data = bytearray(0x8000)
        self.pos = 0x40

    def put(self, b):
        """Place bytes, return their Z80 address."""
        if self.pos + len(b) > 0x7A00:
            raise ValueError('bank full')
        a = 0x8000 + self.pos
        self.data[self.pos:self.pos + len(b)] = bytes(x & 0xFF for x in b)
        self.pos += len(b)
        return a


def le(v):
    return [v & 0xFF, (v >> 8) & 0xFF]


def note(rng):
    b = [0x80 + rng.randrange(0x60)]
    k = rng.choice((0, 1, 1, 2, 3, 3, 3, 3))
    if k >= 1:
        b.append(rng.choice((1, 1, 2, 3, 4, 6, 8, 12, 0)))
    if k >= 2:
        b.append(rng.choice((0, 1, 2, 3, 5, 9, 14, 0x7F)))
    if k >= 3:
        b.append(rng.choice((0, 0x20, 0x40, 0x50, 0x63, 0x63, 0x70)))
    return b


def flag(rng, nins, sfx=False):
    k = rng.randrange(22)
    if k < 14:
        f = 0xE0 + k
        if f == 0xEC:
            f = 0xE6
        arg = {
            0xE0: lambda: rng.randrange(nins),
            0xE1: lambda: rng.choice((0, 1, 0x40, 0x7F, 0x80, 0xFD)),
            0xE2: lambda: rng.randrange(16),
            0xE3: lambda: rng.choice((0, 1, 20, 60, 99, 100, 101, 127, 0x85,
                                      0xC0)),
            0xE4: lambda: rng.choice((0, 0, 0, 1, 2, 5)),
            0xE5: lambda: rng.choice((0, 0, 1, 2, 4)),
            0xE6: lambda: rng.randrange(0xFE),
            0xE7: lambda: rng.randrange(0xFE),
            0xE8: lambda: rng.choice((0, 1, 12, 0x30, 0x54, 0x5F, 0x90)),
            0xE9: lambda: rng.choice((0, 3, 10, 0xFD, 0xF0, 0x40)),
            0xEA: lambda: rng.choice((0, 5, 0xFB, 20, 0x80)),
            0xEB: lambda: rng.choice((0, 3, 0x1F, 0x20, 0x41, 0xFD, 0xE0,
                                      0xDF, 0x80)),
            0xED: lambda: rng.choice((0, 1, 2, 6)),
        }[f]()
        return [f, arg]
    if k < 22:
        return [0xF0 + rng.randrange(8)]
    return []


def junk(rng, depth=0):
    """Bytes the parsers skip. Flag_Wait 0 moves a waiting track to the
    first byte from $FE on: inside a repeat that would leave its $FD with
    no repeat open, where the Z80 spins -- so no $FE there, and no flag
    argument from $FE up anywhere."""
    pick = [[0xEE, rng.randrange(0xFE)], [0xEF, 7], [0xF8], [0xF9], [0xFA],
            [0xFB], [0xFC]]
    if depth == 0:
        pick.append([0xFE])
    return rng.choice(pick)


def phrase_body(rng, nins, depth, notes_needed):
    out = []
    notes = 0
    for _ in range(rng.randint(1, 7)):
        r = rng.random()
        if r < 0.55:
            out += note(rng)
            notes += 1
        elif r < 0.85:
            out += flag(rng, nins)
        elif r < 0.93 and depth < 3:
            out += [0xEC, rng.choice((1, 2, 3, 2))]
            out += phrase_body(rng, nins, depth + 1, False)
            out += [0xFD]
        else:
            out += junk(rng, depth)
    if notes_needed and notes == 0:
        out += note(rng)
    if depth == 0 and rng.random() < 0.1:
        # a 4th repeat level is ignored: its $FD closes the 3rd, and so on
        # down, so four $EC need only three $FD
        out += [0xEC, 2, 0xEC, 2, 0xEC, 2, 0xEC, 3] + note(rng) + \
            [0xFD, 0xFD, 0xFD]
    if depth == 0 and rng.random() < 0.15:
        out += [0xE5, rng.choice((1, 2, 5))]     # a wait at the phrase's end
    if depth == 0 and rng.random() < 0.15:
        out += [0xE5, 0]                     # every waiting track released
    if depth == 0 and rng.random() < 0.2:
        # a wait, then a $FE before the end (Flag_Wait 0 stops there)
        out += [0xE5, rng.choice((3, 8))] + note(rng) + [0xFE] + note(rng)
    if depth == 0 and rng.random() < 0.25:
        # tied notes (gate past the length) under a slide: the slide's
        # tied test sees gate counters from $41 up
        out += [0xE3, rng.choice((20, 60, 99, 0x85)), rng.choice((0xF4, 0xF5))]
        for _ in range(rng.randint(2, 4)):
            out += [0x80 + rng.randrange(0x60), rng.choice((2, 4, 8)),
                    rng.choice((0x45, 0x60, 0x7F))]
    return out


def make_bank(seed):
    """Songs and sfx from every flag the driver parses. What stays out:
    a $FD in a phrase with no repeat open, a 4th repeat level, an order
    list with no phrase after its loop point, a phrase with no note in a
    song, data that lets Flag_Wait 0 strand a track inside a repeat (see
    junk) -- the Z80 itself would spin on those -- and DAC commands."""
    rng = random.Random(seed)
    # pause-heavy (seeds from HEAVY): more empty FM tracks, pauses right
    # after effects start -- Tracks_HaltOrResume's drifting music bits
    # then reach the PSG tracks' release branches. The extra choices are
    # made only for these seeds, so the banks below HEAVY stay as they were.
    heavy = seed >= HEAVY
    bank = Bank()
    songs = []
    for _ in range(3):
        nfm = rng.randint(1, 5)
        npsg = rng.randint(1, 4)
        nins = min(nfm, npsg)
        fm = bank.put([rng.randrange(256) for _ in range(36 * nfm)])
        psg_bytes = []
        for k in range(npsg):
            if k == 0 and rng.random() < 0.5:
                # the attack lands on the peak exactly (start drop a
                # multiple of the rate), then decays
                psg_bytes += [rng.choice((0x50, 0x5A, 0x60)), 0x10,
                              rng.choice((2, 4, 8)), rng.choice((1, 3)),
                              0x20, 1, 2] + [rng.randrange(256)
                                             for _ in range(9)]
                continue
            r = [rng.choice((0, 0x30, 0x50, 0x5A, 0x62, 0x63)),
                 rng.choice((0, 0x10, 0x30, 0x63)),
                 rng.choice((1, 4, 0x10, 0x40)),
                 rng.choice((0, 1, 3, 8)),
                 rng.choice((0, 0x10, 0x30, 0x70)),
                 rng.choice((0, 1, 2)),
                 rng.choice((1, 2, 6, 0x20))]
            r += [rng.randrange(256) for _ in range(9)]
            psg_bytes += r
        psg = bank.put(psg_bytes)
        phrases = [bank.put(phrase_body(rng, nins, 0, True) + [0xFF])
                   for _ in range(rng.randint(3, 9))]
        orders = []
        lfo = rng.random() < 0.5
        no_psg = rng.random() < 0.3
        for t in range(8):
            if rng.random() < 0.1 or (no_psg and t >= 6) or \
                    (heavy and t < 6 and rng.random() < 0.3):
                orders.append(bank.put([0xFF]))
                continue
            o = []
            if t == 0 and lfo:
                # FM1 owns the LFO, undelayed: Track_KeyOn sets z_lfo_out,
                # which FM_UpdateLFO writes (to z_channel's register)
                o += [0xE4, 0, 0xF1]
            if t >= 6 and rng.random() < 0.5:
                o += [0xE2, rng.randrange(16) | 8, 0xF3]   # noise, b3: tone 3
            for _ in range(rng.randint(0, 2)):
                o += flag(rng, nins)
            o += [rng.randrange(len(phrases))
                  for _ in range(rng.randint(1, 3))]
            if rng.random() < 0.5:
                o += [0xFD]
                if rng.random() < 0.3:
                    o += junk(rng)
                o += [rng.randrange(len(phrases))
                      for _ in range(rng.randint(1, 3))]
            orders.append(bank.put(o + [0xFF]))
        vols = bank.put([rng.choice((0x63, 0x63, 0x50, 0x30, 0, 0x70))
                         for _ in range(8)])
        song = bank.pos + 0x8000
        head_len = 14 + 16 + 2 * len(phrases)
        order_table = song + 14
        phrase_table = order_table + 16
        head = [9, rng.choice((0x63, 0x60, 0x40, 0x70)), rng.randrange(256),
                rng.choice((0, 0, 1, 12, 0x50))]
        for a in (vols, order_table, phrase_table, fm, psg):
            head += le((a - song) & 0xFFFF)
        for a in orders:
            head += le((a - song) & 0xFFFF)
        for a in phrases:
            head += le((a - song) & 0xFFFF)
        assert len(head) == head_len
        bank.put(head)
        songs.append(song)
    bad = bank.put([0x07, 0x09])
    table = []
    for a in songs + [bad]:
        table += [a >> 8, a & 0xFF]
    bank.data[0:len(table)] = bytes(table)

    nsfx = 12
    sfx_table = 0xFA00
    fm = bank.put([rng.randrange(256) for _ in range(36 * 3)])
    psg_bytes = []
    for _ in range(3):
        psg_bytes += [0x40, 0, 0x20, 2, 0x10, 1, 4] + \
            [rng.randrange(256) for _ in range(9)]
    psg = bank.put(psg_bytes)
    headers = []
    for n in range(nsfx):
        chans = sorted(rng.sample(range(9), rng.randint(1, 4)))
        if rng.random() < 0.15:
            chans.reverse()
        h = [len(chans) + (1 if rng.random() < 0.1 else 0)]
        for c in chans:
            ph = bank.put(phrase_body(rng, 3, 0, rng.random() < 0.9) + [0xFF])
            h.append(c)
            if c == 8 and rng.random() < 0.6:
                h += [0xF3]
            if rng.random() < 0.4:
                h += flag(rng, 3)
            off = (ph - sfx_table) & 0xFFFF
            h += [off >> 8, off & 0xFF]
        headers.append(bank.put(h))
    t = [0, nsfx] + le((fm - sfx_table) & 0xFFFF) + \
        le((psg - sfx_table) & 0xFFFF) + [0, 0]
    for a in headers:
        t += le((a - sfx_table) & 0xFFFF)
    bank.data[0x7A00:0x7A00 + len(t)] = bytes(t)

    script = list(SET_UP)
    f = 1
    playing = rng.randrange(3)
    script.append((f, [0x09, 0, playing]))
    if rng.random() < 0.7:
        script.append((f, [0x01, 0xFF]))
    for _ in range(rng.randint(15, 60)):
        f += rng.choice((1, 1, 2, 3, 7, 20))
        r = rng.random()
        if r < 0.65:
            script.append((f, [0x02, rng.randrange(nsfx + 1),
                               rng.choice((0, 1, 1, 3, 5, 5, 0x10, 0xFE,
                                           0xFF))]))
            if heavy and rng.random() < 0.3:
                f += 1
                script.append((f, [0x0C, 0, 0]))
                f += rng.choice((1, 2))
                script.append((f, [0x0C, 0, 0]))
        elif r < 0.72:
            script.append((f, [0x04]))
        elif r < 0.80:
            script.append((f, [0x0C, 0, 0]))
            f += rng.choice((1, 2, 30))
            if rng.random() < 0.3:
                script.append((f, [0x02, 1, 1]))
                f += 1
            script.append((f, [0x0C, 0, 0]))
        elif r < 0.85:
            n = rng.randrange(4)
            script.append((f, [0x09, 0, n]))
            playing = n if n < 3 else playing
            if rng.random() < 0.5:
                script.append((f, [0x01, rng.choice((0, 0xFF))]))
        elif r < 0.88:
            # $07 to the song playing: another one would leave its tracks
            # reading their order lists through the new song's phrase
            # table until the next $01 -- the Z80 does it, into its own
            # code below $8000, which the port doesn't hold (the 68k only
            # sends $07 at Snd_Init)
            a = songs[playing]
            script.append((f, [0x07, a >> 8, a & 0xFF]))
        elif r < 0.92:
            speed = rng.choice((0, 1, 2, 4))
            script.append((f, [0x03, speed]))
            if speed == 0 and rng.random() < 0.6:
                # a pause while the stopped PSG tracks still release
                f += rng.choice((1, 2, 3))
                script.append((f, [0x0C, 0, 0]))
                f += rng.choice((1, 5))
                script.append((f, [0x0C, 0, 0]))
        elif r < 0.95:
            script.append((f, [rng.choice((0x06, 0x0E, 0x10, 0xFF))]))
        else:
            script.append((f, [0x01, rng.choice((0, 0xFF))]))
    seconds = max(6, f / 60 + 4)
    return bytes(bank.data), script, seconds


def cases(prio):
    out = shipping_cases(prio)
    for n in range(RANDOM_BANKS):
        out.append(('made-up bank %d' % n, None, None, n))
    return out


def original(rom, script, seconds, bank):
    if bank is not None:
        rom = rom[:0x78000] + bank + rom[0x80000:]
    m, _ = z80run.run(rom, script, seconds, zero_time=True)
    return ['%d %s %02X %02X' % w for w in m.chips.writes]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cache = None
    if '--cache' in sys.argv:
        cache = sys.argv[sys.argv.index('--cache') + 1]
        args.remove(cache)
        os.makedirs(cache, exist_ok=True)
    stop = '--stop' in sys.argv
    if len(args) < 2:
        sys.exit(__doc__)
    rom = open(args[0], 'rb').read()
    assets = args[1]
    p = z80run.sfx_priority(rom, os.path.join(
        os.path.dirname(os.path.abspath(__file__)), 'hz_names.txt'))
    prio = rom[p:p + 256]
    all_cases = cases(prio)
    first = int(args[2]) if len(args) > 2 else 0
    last = min(int(args[3]) if len(args) > 3 else len(all_cases) - 1,
               len(all_cases) - 1)
    bad = 0
    tmp = os.path.join(tempfile.gettempdir(),
                       'hzsound_check_%d' % os.getpid())
    for i in range(first, last + 1):
        name, seconds, script, seed = all_cases[i]
        bank = None
        if seed is not None:
            bank, script, seconds = make_bank(seed)
            with open(tmp + '.bank', 'wb') as fh:
                fh.write(bank)
        key = hashlib.sha1(repr((script, seconds)).encode() +
                           (bank or b'')).hexdigest()[:16]
        cached = cache and os.path.join(cache, '%s.log' % key)
        if cached and os.path.exists(cached):
            want = open(cached).read().split('\n')[:-1]
        else:
            want = original(rom, script, seconds, bank)
            if cached:
                with open(cached, 'w') as fh:
                    fh.write('\n'.join(want) + '\n')
        cmds = ','.join('%d:%s' % (f, ''.join('%02X' % (b & 0xFF)
                                               for b in data))
                        for f, data in script[3:])
        argv = ['./hz_sound', assets, '--text', '--seconds', repr(seconds),
                '--log', tmp + '.log']
        if cmds:
            argv += ['--cmd', cmds]
        if bank is not None:
            argv += ['--bank', tmp + '.bank']
        try:
            r = subprocess.run(argv, capture_output=True, text=True,
                               timeout=30)
        except subprocess.TimeoutExpired:
            print('%2d %-22s hz_sound hung' % (i, name))
            bad += 1
            if stop:
                break
            continue
        if r.returncode != 0:
            print('%2d %-22s hz_sound failed: %s' % (i, name, r.stderr))
            bad += 1
            if stop:
                break
            continue
        got = open(tmp + '.log').read().split('\n')[:-1]
        if got == want:
            print('%2d %-22s identical, %6d writes' % (i, name, len(got)))
            continue
        bad += 1
        k = 0
        while k < min(len(got), len(want)) and got[k] == want[k]:
            k += 1
        print('%2d %-22s DIFFER at write %d of %d/%d' % (
            i, name, k, len(want), len(got)))
        for j in range(max(0, k - 3), k + 4):
            w = want[j] if j < len(want) else '-'
            g = got[j] if j < len(got) else '-'
            print('     %6d  Z80 %-22s  C %s' % (j, w, g))
        if stop:
            break
    for ext in ('.log', '.bank'):
        if os.path.exists(tmp + ext):
            os.remove(tmp + ext)
    print('%d of %d differ' % (bad, last - first + 1))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
