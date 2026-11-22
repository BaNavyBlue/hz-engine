#!/usr/bin/env python3
"""Herzog Zwei's songs and sound effects as text, and the text back into the
sound driver's data.

    python3 tools/hzsongtext.py extract SOUND.bin DIR [MENU.json]
    python3 tools/hzsongtext.py build DIR OUT.bin
    python3 tools/hzsongtext.py check DIR SOUND.bin
    python3 tools/hzsongtext.py annotate DIR|FILE...

SOUND.bin is SoundData ($78000), the 32 KB the Z80 driver's bank window
shows at $8000-$FFFF (asm/hz.asm's Z80Driver). `extract` writes DIR/sounds.txt
(the layout: the song table, each song's file, where the effects sit, the
bytes after them), DIR/songs/NN_title.txt (a song: its header, its eight
tracks' order lists, its phrases and its instruments, each named:
INSTRUMENT_NAMES, docs/instrument_names.md; MENU.json, the
extractor's assets/data/menu.json, gives the titles) and DIR/effects.txt
(the effects, their phrases and instruments), and only after building the
text back into the same bytes; then each song as a tracker beside its text,
DIR/songs/NN_title.trk (tools/hztracker.py: written once it plays as the
text). `build` makes the 32 KB as the game does at start-up
(src/audio/songtext.c: a song's tracker in place of its text where there
is one), followed by each song's own 32 KB when the songs outgrow the one
(docs/sound.md, Room); `check` builds the text alone (as the ROM's: no
trackers) and compares. `annotate` writes the `# heard:` comments again
(what a note sounds as, where a transpose, a detune or a PSG track moves
it) in a folder's song and effects files, or in the files named.
docs/sound.md has the formats.
"""
import json
import os
import re
import sys

BASE = 0x8000                   # the bank window's first Z80 address
SIZE = 0x8000
SONG_FIRST = 9                  # a song's first byte (Cmd_PlaySong checks)
TRACKS = 8                      # a song's: FM1-6, PSG tones 1-2
CHANNELS = ['fm1', 'fm2', 'fm3', 'fm4', 'fm5', 'fm6', 'psg1', 'psg2', 'noise']
NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
FLAT_NAMES = {'Db': 1, 'Eb': 3, 'Gb': 6, 'Ab': 8, 'Bb': 10}
NOTE_FIRST, NOTE_END = 0x80, 0xE0
FM_SIZE, PSG_SIZE = 36, 16

# The bytes the driver reads as commands (Track_ParseCommand): $E0-$ED take
# a byte after them, $F0-$F7 none; $EE-$EF (two bytes), $F8-$FC and $FE
# (one) are skipped; $FD and $FF are the reader's own.
LOOP, END = 0xFD, 0xFF
BAR_GOES_ON, BAR_EMPTY = 0xFC, 0xFB
ARG_FLAGS = {
    0xE0: ('instrument', 'u'),
    0xE1: ('pan', 'pan'),
    0xE2: ('noise_mode', 'u'),
    0xE3: ('slide_speed', 'u'),
    0xE4: ('lfo_delay', 'u'),
    0xE5: ('wait', 'u'),
    0xE6: ('marker', 'u'),
    0xE7: ('marker', 'add'),
    0xE8: ('transpose', 's'),
    0xE9: ('volume', 's'),
    0xEA: ('tempo', 's'),
    0xEB: ('detune', 's'),
    0xEC: ('repeat', 'u'),
    0xED: ('fade_rate', 'u'),
}
WORD_FLAGS = {
    0xF0: ('lfo', 'off'), 0xF1: ('lfo', 'on'),
    0xF2: ('psg_volume', 'tone'), 0xF3: ('psg_volume', 'noise'),
    0xF4: ('slide', 'all'), 0xF5: ('slide', 'tied'),
    0xF6: ('release_bend', 'up'), 0xF7: ('release_bend', 'down'),
}
PAN_WORDS = {0x00: 'center', 0x01: 'left', 0xFF: 'right'}

# An FM instrument's 36 bytes (Track_InitInstrument, Track_SetVolume):
# operators in the chip's numbering 1-4 (the driver writes them to the
# register offsets +0, +8, +4, +C). The bits no field holds and the bytes
# the driver never reads are `unused`, kept so the data rebuilds.
FM_SCALARS = [('algorithm', 0, 0, 7), ('feedback', 0, 3, 7),
              ('fms', 1, 4, 7), ('ams', 1, 0, 3),
              ('lfo_freq', 31, 0, 7), ('lfo_sweep', 35, 0, 255)]
FM_ROWS = [('multiple', 2, 0, 15), ('detune', 2, 4, 7),
           ('level', 6, 0, 127), ('key_scale', 10, 6, 3),
           ('attack', 10, 0, 31), ('am', 14, 7, 1), ('decay1', 14, 0, 31),
           ('decay2', 18, 0, 31), ('sustain', 22, 4, 15),
           ('release', 22, 0, 15)]
# A PSG instrument's 16 bytes (Track_InitInstrument's .psg): the volume
# envelope (+$35-$3B of the track), the LFO bytes as an FM one's.
PSG_SCALARS = [('speed', 0, 0, 255), ('start_drop', 1, 0, 255),
               ('attack', 2, 0, 255), ('decay', 3, 0, 255),
               ('sustain_drop', 4, 0, 255), ('sustain', 5, 0, 255),
               ('release', 6, 0, 255), ('fms', 7, 4, 7), ('ams', 7, 0, 3),
               ('lfo_freq', 8, 0, 7), ('lfo_sweep', 12, 0, 255)]


def field_masks(scalars, rows, size):
    used = [0] * size
    for _, off, shift, top in scalars:
        used[off] |= top << shift
    for _, off, shift, top in rows:
        for k in range(4):
            used[off + k] |= top << shift
    return [~u & 0xFF for u in used]


FM_UNUSED = field_masks(FM_SCALARS, FM_ROWS, FM_SIZE)
PSG_UNUSED = field_masks(PSG_SCALARS, [], PSG_SIZE)

USES = {0: 'the title', 1: 'the options', 2: 'the map select',
        3: "ABGRUND's map", 4: "VULKAN's map", 5: "LOCH's map",
        6: "STRAND's map", 7: "STADT's map", 8: "EISFREI's map",
        9: "WALDUNG's map", 10: "OASE's map",
        11: 'the winner screen', 12: 'the winner screen',
        13: 'the winner screen', 14: 'the winner screen',
        15: 'an ending', 16: 'an ending', 17: 'the staff roll'}
EFFECT_NAMES = [
    'fire_robot', 'fire_jet', 'fire_unit', 'fire_missile', 'mech_destroyed',
    'unit_destroyed', 'infantry_destroyed', 'hit_unit', 'hit_mech',
    'hit_tile', 'take_off', 'land', 'lift', 'drop', 'jets_meet', 'menu_open',
    'menu_move', 'ordered', 'unit_ready', 'hq_alert', 'missile_alert',
    'hold_raised', 'post_taken', 'hq_hit', 'hq_destroyed', 'unused_25',
    'refused', 'debris_end', 'sizzle']

# The instruments' names (session 50, docs/instrument_names.md has the
# reasons): each song's FM instruments then its PSG ones, then the
# effects'. What `extract` writes; the text then has them (`fm_instrument
# 4 chords`, `instrument chords`), and an edit there is the name.
INSTRUMENT_NAMES = {
    0: ('low chords high open_hat snare', ''),
    1: ('high bass snare kick', 'psg_mid'),
    2: ('mid low bass high', 'psg_high'),
    3: ('mid low high closed_hat chords chords_2 high_2 snare kick_toms',
        'psg_chords'),
    4: ('mid low low_2 bass chords low_3 mid_2 kick_toms',
        'psg_high psg_mid psg_high_2'),
    5: ('chords chords_2 chords_3 bass chords_4 kick_toms', ''),
    6: ('chords chords_2 low closed_hat high mid snare kick_toms',
        'psg_chords'),
    7: ('chords mid chords_2 kick_toms snare low', ''),
    8: ('mid chords closed_hat open_hat chords_2 bass chords_3 snare '
        'open_hat_2 kick_toms', 'psg_chords'),
    9: ('chords high chords_2 bass closed_hat high_2 kick_toms', ''),
    10: ('low low_2 low_3 chords high low_4 mid low_5 kick_toms',
         'psg_chords psg_chords_2'),
    11: ('bass closed_hat bass_2 chords low kick_toms', 'psg_chords'),
    12: ('chords high mid', 'psg_high'),
    13: ('high low closed_hat mid high_2 kick_toms', 'psg_chords'),
    14: ('open_hat chords bass kick_toms', 'psg_chords'),
    15: ('bass bass_2 mid mid_2 snare kick_snare', 'psg_chords'),
    16: ('bass bass_2 bass_3 mid high snare kick_snare', 'psg_chords'),
    17: ('chords closed_hat bass mid chords_2 bass_2 snare kick_snare',
         'psg_chords psg_chords_2'),
    'effects': ('hit_unit ordered fire_missile fire_jet mech_destroyed '
                'fire_robot hold_raised hit_tile land infantry_destroyed '
                'unused_25 missile_alert',
                'psg_fire_unit psg_mech_destroyed psg_fire_unit_2 psg_hq_hit '
                'psg_drop'),
}
NAME_RE = re.compile(r'[A-Za-z][A-Za-z0-9_]{0,30}')


class TextError(ValueError):
    pass


# -- shared --------------------------------------------------------------

def note_name(v):
    return '%s%d' % (NOTE_NAMES[v % 12], v // 12)


def signed_text(v):
    if v == 0:
        return '0'
    return '+%d' % v if v < 0x80 else '-%d' % (0x100 - v)


def flag_text(byte, arg=None, names=None):
    """A command's words; names: the instruments' names an `instrument`
    picks from (None where one has none: its number)."""
    if byte in WORD_FLAGS:
        return '%s %s' % WORD_FLAGS[byte]
    if byte == 0xE0 and names and arg < len(names) and names[arg]:
        return 'instrument %s' % names[arg]
    word, kind = ARG_FLAGS[byte]
    if kind == 's':
        return '%s %s' % (word, signed_text(arg))
    if kind == 'add':
        return '%s +%d' % (word, arg)
    if kind == 'pan' and arg in PAN_WORDS:
        return 'pan %s' % PAN_WORDS[arg]
    return '%s %d' % (word, arg)


def items_bytes(items):
    out = []
    for it in items:
        kind = it[0]
        if kind == 'flag':
            out.append(it[1])
            if it[1] in ARG_FLAGS:
                out.append(it[2])
        elif kind == 'note':
            out.append(NOTE_FIRST + it[1])
            out.extend(it[2])
        elif kind in ('phrase', 'byte'):
            out.append(it[1])
        else:
            raise ValueError(kind)
    return out


# -- reading the bytes ---------------------------------------------------

class Image:
    def __init__(self, data):
        if len(data) != SIZE:
            raise ValueError('%d bytes, not %d' % (len(data), SIZE))
        self.data = data

    def b(self, a):
        return self.data[a - BASE]

    def le(self, a):
        return self.b(a) | self.b(a + 1) << 8

    def be(self, a):
        return self.b(a) << 8 | self.b(a + 1)

    def block(self, a, n):
        return [self.b(a + k) for k in range(n)]


def read_command(img, a, items):
    """One flag or skipped byte at a into items; the address after it."""
    x = img.b(a)
    if x in ARG_FLAGS:
        items.append(('flag', x, img.b(a + 1)))
        return a + 2
    if x in WORD_FLAGS:
        items.append(('flag', x))
        return a + 1
    if x in (0xEE, 0xEF):
        items.append(('byte', x))
        items.append(('byte', img.b(a + 1)))
        return a + 2
    items.append(('byte', x))
    return a + 1


def read_order(img, a):
    """An order list (Track_NextPhrase) to its $FF."""
    items = []
    while True:
        x = img.b(a)
        if x < NOTE_END:
            items.append(('phrase', x))
            a += 1
        elif x == END:
            return items, a + 1
        elif x == LOOP:
            items.append(('byte', x))
            a += 1
        else:
            a = read_command(img, a, items)


def read_phrase(img, a):
    """A phrase (Phrase_NextNote) to its $FF: notes take up to three bytes
    under $80 after them."""
    items = []
    while True:
        x = img.b(a)
        if x < NOTE_FIRST:
            raise ValueError('byte $%02X at $%04X where a note or a command '
                             'goes' % (x, a))
        if x < NOTE_END:
            a += 1
            params = []
            while len(params) < 3 and img.b(a) < 0x80:
                params.append(img.b(a))
                a += 1
            items.append(('note', x - NOTE_FIRST, params))
        elif x == END:
            return items, a + 1
        elif x == LOOP:
            items.append(('byte', x))
            a += 1
        else:
            a = read_command(img, a, items)


def read_song(img, s):
    """A song at s, its layout checked against the one build() makes."""
    def expect(what, got, want):
        if got != want:
            raise ValueError('song at $%04X: %s at $%04X, not $%04X'
                             % (s, what, got, want))
    if img.b(s) != SONG_FIRST:
        raise ValueError('song at $%04X: first byte $%02X' % (s, img.b(s)))
    song = {'volume': img.b(s + 1), 'tempo': img.b(s + 2),
            'transpose': img.b(s + 3)}
    vol, trk = s + img.le(s + 4), s + img.le(s + 6)
    phr, fm, psg = s + img.le(s + 8), s + img.le(s + 10), s + img.le(s + 12)
    size = img.le(s + 14)
    expect('the channel volumes', vol, s + 16)
    song['volumes'] = img.block(vol, 9)
    expect('the order lists\' table', trk, vol + 9)
    a = trk + 2 * TRACKS
    song['tracks'] = []
    for t in range(TRACKS):
        expect('track %d\'s order list' % t, s + img.le(trk + 2 * t), a)
        items, a = read_order(img, a)
        song['tracks'].append(items)
    expect('the phrase table', phr, a)
    first = None
    count = 0
    while first is None or phr + 2 * count < first:
        v = img.le(phr + 2 * count)
        if v and (first is None or s + v < first):
            first = s + v
        count += 1
    song['phrase_count'] = count
    a = phr + 2 * count
    song['phrases'] = {}
    for k in range(count):
        v = img.le(phr + 2 * k)
        if v:
            expect('phrase %d' % k, s + v, a)
            song['phrases'][k], a = read_phrase(img, a)
    expect('the FM instruments', fm, a)
    if (psg - fm) % FM_SIZE or (s + size + 1 - psg) % PSG_SIZE:
        raise ValueError('song at $%04X: instruments not whole' % s)
    song['fm'] = [img.block(fm + FM_SIZE * k, FM_SIZE)
                  for k in range((psg - fm) // FM_SIZE)]
    song['psg'] = [img.block(psg + PSG_SIZE * k, PSG_SIZE)
                   for k in range((s + size + 1 - psg) // PSG_SIZE)]
    return song, size


def read_effects(img, e):
    fx = {'first_byte': img.b(e)}
    count = img.b(e + 1)
    fm, psg, size = e + img.le(e + 2), e + img.le(e + 4), img.le(e + 6)
    a = e + 8 + 2 * count
    fx['effects'] = []
    used = {}
    for k in range(count):
        p = e + img.le(e + 8 + 2 * k)
        if p != a:
            raise ValueError('effect %d at $%04X, not $%04X' % (k, p, a))
        n = img.b(p)
        a = p + 1
        name = EFFECT_NAMES[k] if k < len(EFFECT_NAMES) else 'effect_%d' % k
        tracks = []
        for t in range(n):
            ch = img.b(a)
            a += 1
            flags = []
            while img.b(a) >= NOTE_END:
                a = read_command(img, a, flags)
            at = e + img.be(a)
            a += 2
            if at not in used:
                used[at] = name if n == 1 else '%s_%s' % (name,
                                                          CHANNELS[ch])
            tracks.append({'channel': ch, 'flags': flags,
                           'phrase': used[at]})
        fx['effects'].append({'name': name, 'tracks': tracks})
    fx['phrases'] = []
    for at in sorted(used):
        if at != a:
            raise ValueError('effect phrase at $%04X, not $%04X' % (at, a))
        items, a = read_phrase(img, at)
        fx['phrases'].append((used[at], items))
    if fm != a:
        raise ValueError('effect FM instruments at $%04X, not $%04X'
                         % (fm, a))
    if (psg - fm) % FM_SIZE or (e + size + 1 - psg) % PSG_SIZE:
        raise ValueError('effect instruments not whole')
    fx['fm'] = [img.block(fm + FM_SIZE * k, FM_SIZE)
                for k in range((psg - fm) // FM_SIZE)]
    fx['psg'] = [img.block(psg + PSG_SIZE * k, PSG_SIZE)
                 for k in range((e + size + 1 - psg) // PSG_SIZE)]
    return fx, e + size + 1


def read_image(data):
    """SoundData as the model build() takes."""
    img = Image(data)
    starts = []
    a = BASE
    while True:
        w = img.be(a)
        p = w | BASE
        if w == 0 or img.data[p - BASE] != SONG_FIRST:
            break
        starts.append(p)
        a += 2
    end_entry = w | BASE
    entries = (starts[0] - BASE) // 2
    for k in range(len(starts) + 1, entries):
        if img.be(BASE + 2 * k):
            raise ValueError('song table entry %d not 0' % k)
    model = {'song_table': entries, 'songs': []}
    at = BASE + 2 * entries
    for n, s in enumerate(starts):
        if s != at:
            raise ValueError('song %d at $%04X, not $%04X' % (n, s, at))
        song, size = read_song(img, s)
        song['fm_names'], song['psg_names'] = table_names(n, song)
        model['songs'].append(song)
        at = s + size
    if end_entry != at:
        raise ValueError('the song table\'s end entry $%04X, not $%04X'
                         % (end_entry, at))
    fill = at + 1
    while img.data[fill - BASE] == 0xFF and fill < 0x10000:
        fill += 1
    # The effects' table: the 68k gives its address (Snd_SetSfxTable, $FA00).
    model['effects_at'] = 0xFA00
    if fill != model['effects_at']:
        raise ValueError('$FF fill to $%04X, not up to the effects' % fill)
    model['effects'], tail = read_effects(img, model['effects_at'])
    fx = model['effects']
    fx['fm_names'], fx['psg_names'] = table_names('effects', fx)
    model['tail'] = list(img.data[tail - BASE:])
    return model


def table_names(key, part):
    """INSTRUMENT_NAMES' for a song (by number) or the effects, if they
    have as many instruments as it names (else none)."""
    fm, psg = (s.split() for s in INSTRUMENT_NAMES.get(key, ('', '')))
    if len(fm) != len(part['fm']) or len(psg) != len(part['psg']):
        return [None] * len(part['fm']), [None] * len(part['psg'])
    return fm, psg


# -- writing the text ----------------------------------------------------

def order_text(items, names=None):
    lines, cur, bars = [], '', 0
    prev_bar = False
    for it in items:
        if it == ('byte', LOOP):
            if cur:
                lines.append(cur)
            lines.append('loop')
            cur, bars = '', 0
            continue
        if it[0] == 'phrase':
            tok, bar = str(it[1]), True
        elif it == ('byte', BAR_GOES_ON):
            tok, bar = '~', True
        elif it == ('byte', BAR_EMPTY):
            tok, bar = '-', True
        elif it[0] == 'flag':
            tok, bar = flag_text(*it[1:], names=names), False
        else:
            tok, bar = 'byte $%02X' % it[1], False
        if bar and bars == 16:
            lines.append(cur)
            cur, bars = '', 0
        if cur:
            cur += ' ' if bar and prev_bar else '  '
        cur += tok
        prev_bar = bar
        bars += bar
    if cur:
        lines.append(cur)
    return lines


def phrase_text(items, names=None):
    """A line a bar (96 ticks) or eight notes, repeats indented; names: the
    instruments' its tracks pick from (one list: they are all FM, or all
    PSG), or None."""
    lines, cur = [], []
    depth = 0
    ticks = notes = 0
    length = 24

    def flush():
        nonlocal cur, ticks, notes
        if cur:
            lines.append('    ' * depth + '  '.join(cur))
        cur, ticks, notes = [], 0, 0
    for it in items:
        if it[0] == 'note':
            cur.append(' '.join([note_name(it[1])] + [str(p) for p in it[2]]))
            if it[2]:
                length = it[2][0]
            ticks += length
            notes += 1
            if ticks >= 96 or notes == 8:
                flush()
        elif it[0] == 'flag' and it[1] == 0xEC:
            flush()
            lines.append('    ' * depth + flag_text(*it[1:]))
            depth += 1
        elif it == ('byte', LOOP):
            flush()
            depth = max(depth - 1, 0)
            lines.append('    ' * depth + 'end_repeat')
        elif it[0] == 'flag':
            cur.append(flag_text(*it[1:], names=names))
        else:
            cur.append('byte $%02X' % it[1])
    flush()
    return lines


def instrument_text(kind, n, rec, name=None):
    if kind == 'fm':
        scalars, rows, unused = FM_SCALARS, FM_ROWS, FM_UNUSED
    else:
        scalars, rows, unused = PSG_SCALARS, [], PSG_UNUSED
    out = ['%s_instrument %d%s' % (kind, n, ' ' + name if name else '')]
    line = []
    for name, off, shift, top in scalars:
        line.append('%s %d' % (name, rec[off] >> shift & top))
        if kind == 'psg' and name == 'release':
            out.append('    ' + '  '.join(line))
            line = []
    if line:
        out.append('    ' + '  '.join(line))
    if rows:
        out.append('    #           op1  op2  op3  op4')
        for name, off, shift, top in rows:
            out.append('    %-10s' % name + ''.join(
                '%5d' % (rec[off + k] >> shift & top) for k in range(4)))
    extra = ['%d:$%02X' % (off, rec[off] & unused[off])
             for off in range(len(rec)) if rec[off] & unused[off]]
    if extra:
        out.append('    unused ' + ' '.join(extra))
    return out


def song_text(n, song, title):
    use = USES.get(n)
    out = ['# %s (song %d%s). docs/sound.md, "The song text", has the format.'
           % (title or 'Song %d' % n, n, ': ' + use if use else ''),
           '']
    if not song['psg'] and any(it[0] == 'phrase' for t in (6, 7)
                               for it in song['tracks'][t]):
        out[1:1] = ['# It has no psg_instrument: its PSG tracks take '
                    'instrument 0 from the', '# 16 bytes after it, the '
                    'start of the next song (docs/sound.md, Room).']
    line = 'song  volume %d  tempo %d  transpose %s' % (
        song['volume'], song['tempo'], signed_text(song['transpose']))
    defined = max(song['phrases']) + 1 if song['phrases'] else 0
    if song['phrase_count'] != defined:
        line += '  phrases %d' % song['phrase_count']
    out.append(line)
    out.append('volumes  %s' % ' '.join(str(v) for v in song['volumes']))
    names = part_names(song)
    kinds = {}
    for t, items in enumerate(song['tracks']):
        out += ['', 'track %s' % CHANNELS[t]]
        out += ['    ' + x for x in order_text(items, names[channel_kind(t)])]
        for it in items:
            if it[0] == 'phrase':
                kinds.setdefault(it[1], set()).add(channel_kind(t))
    for k in sorted(song['phrases']):
        out += ['', 'phrase %d' % k]
        out += ['    ' + x for x in phrase_text(song['phrases'][k],
                                                one_list(kinds.get(k), names))]
    out += instruments_text(song, names)
    return '\n'.join(out) + '\n'


def channel_kind(c):
    """The instruments a channel picks from: fm1-fm6 the FM list, psg1,
    psg2 and the noise the PSG list."""
    return 'fm' if c < 6 else 'psg'


def part_names(part):
    """A song's or the effects' instruments' names, a list each kind (None
    where one has none)."""
    return {kind: list(part.get(kind + '_names') or [None] * len(part[kind]))
            for kind in ('fm', 'psg')}


def one_list(kinds, names):
    """The names a phrase's instrument commands pick from: its tracks'
    list, or None (played by none, or by FM and PSG tracks both)."""
    if kinds and len(kinds) == 1:
        return names[next(iter(kinds))]
    return None


def instruments_text(part, names):
    out = []
    for kind in ('fm', 'psg'):
        for k, rec in enumerate(part[kind]):
            name = names[kind][k] if k < len(names[kind]) else None
            out += [''] + instrument_text(kind, k, rec, name)
    return out


EFFECTS_HEAD = '''\
# The sound effects (the sfx table: Snd_SetSfxTable gives the driver its
# address, sounds.txt's `effects`). docs/sound.md, "The song text", has the
# format. An effect's number is the game's (SFX_* in src/game/game.h); each
# track names its channel, the commands read before its phrase, and the
# phrase. `first_byte`: the table's first byte, which nothing reads.
'''


def effects_text(fx):
    out = [EFFECTS_HEAD.rstrip('\n'), '',
           'effects  first_byte %d' % fx['first_byte']]
    names = part_names(fx)
    kinds = {}
    for k, e in enumerate(fx['effects']):
        out += ['', 'effect %d %s' % (k, e['name'])]
        for t in e['tracks']:
            kind = channel_kind(t['channel'])
            toks = [CHANNELS[t['channel']]]
            for it in t['flags']:
                toks.append(flag_text(*it[1:], names=names[kind])
                            if it[0] == 'flag' else 'byte $%02X' % it[1])
            toks.append(t['phrase'])
            out.append('    track ' + '  '.join(toks))
            kinds.setdefault(t['phrase'], set()).add(kind)
    for name, items in fx['phrases']:
        out += ['', 'phrase %s' % name]
        out += ['    ' + x for x in phrase_text(items, one_list(
            kinds.get(name), names))]
    out += instruments_text(fx, names)
    return '\n'.join(out) + '\n'


SOUNDS_HEAD = '''\
# Herzog Zwei's songs and sound effects: the 32 KB the Z80 sound driver's
# bank window shows at $8000-$FFFF (SoundData, ROM $78000), as text. The
# game builds the bytes from these files at start-up; docs/sound.md has the
# format, tools/hzsongtext.py writes and checks them.
#
# `song_table N`: the table at $8000, N two-byte entries (each song's
# address, then where the next song would go, then zeros). The songs follow
# it in this order, each one on the last byte of the one before when that
# byte is the 9 a song begins with (the ROM's all are). `effects ADDRESS
# FILE`: the effects' table, after a $FF fill. `tail`: bytes after the
# effects that nothing reads. Songs that grow past the effects each get a
# window of their own instead (docs/sound.md, Room).
'''


def slug(title):
    return re.sub(r'[^a-z0-9]+', '_', title.lower()).strip('_')


def write_text(model, titles):
    files = {}
    lines = [SOUNDS_HEAD.rstrip('\n'), '', 'song_table %d' % model['song_table']]
    for n, song in enumerate(model['songs']):
        title = titles[n] if n < len(titles) else None
        name = 'songs/%02d_%s.txt' % (n, slug(title) if title else 'song')
        files[name] = song_text(n, song, title)
        lines.append('song %d %s' % (n, name))
    lines.append('effects $%04X effects.txt' % model['effects_at'])
    files['effects.txt'] = effects_text(model['effects'])
    if model['tail']:
        lines.append('tail')
        t = model['tail']
        for k in range(0, len(t), 16):
            lines.append('    ' + ' '.join('%02X' % v for v in t[k:k + 16]))
    files['sounds.txt'] = '\n'.join(lines) + '\n'
    return files


# -- reading the text ----------------------------------------------------

class Source:
    """A file's lines as (line number, tokens), comments dropped: a comment
    starts at a word beginning with # (the # in C#4 is a sharp)."""
    def __init__(self, name, text):
        self.name = name
        self.lines = []
        for n, line in enumerate(text.splitlines(), 1):
            toks = line.split()
            for k, t in enumerate(toks):
                if t.startswith('#'):
                    toks = toks[:k]
                    break
            if toks:
                self.lines.append((n, toks))
        self.pos = 0
        self.names = {}             # an instrument's name: (kind, number)

    def error(self, line, msg):
        if line:
            raise TextError('%s:%d: %s' % (self.name, line, msg))
        raise TextError('%s: %s' % (self.name, msg))


class Word(str):
    """A word of a block, with its line (`line`)."""


def words(lines):
    """A block's lines as one run of words, each knowing its line: a line
    break is a space (as songtext.c reads a block)."""
    out = []
    for line, toks in lines:
        for t in toks:
            w = Word(t)
            w.line = line
            out.append(w)
    return out


def line_of(tok, line):
    """The line a word is on: its own, if it knows it."""
    return getattr(tok, 'line', line)


def number(src, line, tok, lo, hi):
    m = re.fullmatch(r'([+-]?)(\$[0-9A-Fa-f]+|[0-9]+)', tok)
    if not m:
        src.error(line, '%r is not a number' % tok)
    v = int(m.group(2)[1:], 16) if m.group(2)[0] == '$' else int(m.group(2))
    if m.group(1) == '-':
        v = -v
    if not lo <= v <= hi:
        src.error(line, '%s is out of range (%d to %d)' % (tok, lo, hi))
    return v


def note_value(tok):
    m = re.fullmatch(r'([A-G])([#b]?)(-?[0-9]+)', tok)
    if not m:
        return None
    name = m.group(1) + m.group(2)
    if name in NOTE_NAMES:
        semi = NOTE_NAMES.index(name)
    elif name in FLAT_NAMES:
        semi = FLAT_NAMES[name]
    elif name in ('Cb', 'Fb', 'E#', 'B#'):
        semi = {'Cb': -1, 'Fb': 4, 'E#': 5, 'B#': 12}[name]
    else:
        return None
    return int(m.group(3)) * 12 + semi


WORDS = {}
for _b, (_w, _k) in ARG_FLAGS.items():
    WORDS.setdefault(_w, []).append((_b, _k))
for _b, (_w, _a) in WORD_FLAGS.items():
    WORDS.setdefault(_w, []).append((_b, _a))


KIND_SAID = {'fm': 'an FM', 'psg': 'a PSG'}


def scan_names(src):
    """The instruments' names (`fm_instrument N NAME`), read before the
    rest of the file, which names them before they come."""
    for line, toks in src.lines:
        if toks[0] not in ('fm_instrument', 'psg_instrument'):
            continue
        if len(toks) not in (2, 3):
            src.error(line, '%s takes its number (and a name)' % toks[0])
        if len(toks) == 3:
            n = number(src, line, toks[1], 0, 255)
            name = toks[2]
            if not NAME_RE.fullmatch(name):
                src.error(line, "'%s': an instrument's name is a letter, then "
                          "letters, digits or _ (31 at most)" % name)
            if name in src.names:
                src.error(line, 'the name %s given twice' % name)
            src.names[name] = (toks[0].split('_')[0], n)


def name_kind(src, toks, i):
    """(kind, name) when toks[i] is `instrument NAME`, else None."""
    if (toks[i] == 'instrument' and i + 1 < len(toks)
            and toks[i + 1] in src.names):
        return src.names[toks[i + 1]][0], toks[i + 1]
    return None


def check_kind(src, line, toks, i, channel):
    """Refuses `instrument NAME` at toks[i] where NAME is in the other
    list than the channel's."""
    got = name_kind(src, toks, i)
    if got and got[0] != channel_kind(channel):
        src.error(line, 'instrument %s: %s instrument, on %s'
                  % (got[1], KIND_SAID[got[0]], CHANNELS[channel]))


def check_phrase_kind(src, uses, channel, phrase):
    """Refuses a phrase played on a channel whose list its instruments'
    names aren't in (uses: kind -> its first such name and line)."""
    other = 'psg' if channel_kind(channel) == 'fm' else 'fm'
    if other in uses:
        line, name = uses[other]
        src.error(line, 'instrument %s: %s instrument, in %s, played on %s'
                  % (name, KIND_SAID[other], phrase, CHANNELS[channel]))


def names_lists(src, part):
    """The names scan_names found, a list a kind as part's instruments."""
    out = {kind: [None] * len(part[kind]) for kind in ('fm', 'psg')}
    for name, (kind, n) in src.names.items():
        if n < len(out[kind]):
            out[kind][n] = name
    return out['fm'], out['psg']


def take_flag(src, line, toks, i, word):
    """The flag `word` at toks[i]; (item, next index)."""
    if i + 1 >= len(toks):
        src.error(line, '%s needs a value' % word)
    arg = toks[i + 1]
    if word == 'instrument' and arg in src.names:
        return ('flag', 0xE0, src.names[arg][1]), i + 2
    if word == 'instrument' and re.match(r'[A-Za-z]', arg):
        src.error(line, 'instrument %s: no instrument of that name' % arg)
    for byte, kind in WORDS[word]:
        if byte in WORD_FLAGS:
            if arg == kind:
                return ('flag', byte), i + 2
            continue
        if kind == 'u':
            if word == 'marker' and arg.startswith('+'):
                continue
            return ('flag', byte, number(src, line_of(arg, line), arg, 0, 255)), i + 2
        if kind == 'add':
            if arg.startswith('+'):
                return ('flag', byte, number(src, line_of(arg, line), arg, 0, 255)), i + 2
            continue
        if kind == 's':
            return ('flag', byte,
                    number(src, line_of(arg, line), arg, -128, 127) & 0xFF), i + 2
        if kind == 'pan':
            for v, w in PAN_WORDS.items():
                if arg == w:
                    return ('flag', byte, v), i + 2
            if not re.fullmatch(r'\$[0-9A-Fa-f]+|[0-9]+', arg):
                src.error(line, 'pan %s: not center, left, right or a '
                          'number' % arg)
            return ('flag', byte, number(src, line_of(arg, line), arg, 0, 255)), i + 2
    choices = ' or '.join(k for b, k in WORDS[word])
    src.error(line, '%s %s: not %s' % (word, arg, choices))


def take_common(src, line, toks, i):
    """A flag or `byte N` at toks[i], or None."""
    t = toks[i]
    if t in WORDS:
        return take_flag(src, line, toks, i, t)
    if t == 'byte':
        if i + 1 >= len(toks):
            src.error(line, 'byte needs a value')
        return ('byte', number(src, line_of(toks[i + 1], line), toks[i + 1],
                               0, 255)), i + 2
    return None


def block_lines(src, stop):
    """The lines up to the next one starting with a word in stop."""
    out = []
    while src.pos < len(src.lines) and src.lines[src.pos][1][0] not in stop:
        out.append(src.lines[src.pos])
        src.pos += 1
    return out


def parse_order(src, lines, channel):
    items = []
    toks = words(lines)
    i = 0
    while i < len(toks):
        t = toks[i]
        line = t.line
        if t == 'loop':
            items.append(('byte', LOOP))
            i += 1
        elif t == '~':
            items.append(('byte', BAR_GOES_ON))
            i += 1
        elif t == '-':
            items.append(('byte', BAR_EMPTY))
            i += 1
        elif re.fullmatch(r'\$?[0-9A-Fa-f]+', t) and t[0] in '$0123456789':
            items.append(('phrase', number(src, line, t, 0, NOTE_END - 1)))
            i += 1
        else:
            r = take_common(src, line, toks, i)
            if r is None:
                src.error(line, '%r: not a phrase number or a command' % t)
            check_kind(src, line, toks, i, channel)
            items.append(r[0])
            i = r[1]
    return items


def parse_phrase(src, lines, uses=None):
    """A phrase's items; uses: each kind's first instrument name in it
    (kind -> (line, name)), checked against the channels that play it."""
    items = []
    toks = words(lines)
    i = 0
    while i < len(toks):
        t = toks[i]
        line = t.line
        v = note_value(t)
        if v is not None:
            if not 0 <= v < NOTE_END - NOTE_FIRST:
                src.error(line, '%s is out of range (C0 to B7)' % t)
            i += 1
            params = []
            while (i < len(toks) and len(params) < 3
                   and re.fullmatch(r'\$?[0-9A-Fa-f]+', toks[i])
                   and toks[i][0] in '$0123456789'):
                params.append(number(src, toks[i].line, toks[i], 0, 127))
                i += 1
            items.append(('note', v, params, line))
        elif t == 'end_repeat':
            items.append(('byte', LOOP))
            i += 1
        else:
            r = take_common(src, line, toks, i)
            if r is None:
                if re.fullmatch(r'[0-9$]\S*', t):
                    src.error(line, '%s: a number with no note before it (a '
                              'note takes three at most)' % t)
                src.error(line, '%r: not a note or a command' % t)
            got = name_kind(src, toks, i)
            if got and uses is not None:
                uses.setdefault(got[0], (line, got[1]))
            items.append(r[0])
            i = r[1]
    return items


def c_number(s):
    """songtext.c's parse_number: decimal or $hex, a sign before it
    allowed, or None (also past 24 bits as it reads)."""
    m = re.fullmatch(r'([+-]?)(\$[0-9A-Fa-f]+|[0-9]+)', s)
    if not m:
        return None
    digits, base = m.group(2), 10
    if digits[0] == '$':
        digits, base = digits[1:], 16
    v = 0
    for d in digits:
        if v > 0xFFFFFF:
            return None
        v = v * base + int(d, 16)
    return -v if m.group(1) == '-' else v


def parse_instrument(src, line0, kind, lines):
    if kind == 'fm':
        scalars, rows, unused, size = FM_SCALARS, FM_ROWS, FM_UNUSED, FM_SIZE
    else:
        scalars, rows, unused, size = PSG_SCALARS, [], PSG_UNUSED, PSG_SIZE
    rec = [0] * size
    seen = set()
    sc = {s[0]: s for s in scalars}
    rw = {r[0]: r for r in rows}
    toks = words(lines)
    i = 0
    while i < len(toks):
        t = toks[i]
        line = t.line
        if t in seen:
            src.error(line, '%s given twice' % t)
        if t in sc:
            _, off, shift, top = sc[t]
            if i + 1 >= len(toks):
                src.error(line, '%s needs a value' % t)
            rec[off] |= number(src, toks[i + 1].line, toks[i + 1], 0,
                               top) << shift
            i += 2
        elif t in rw:
            _, off, shift, top = rw[t]
            if len(toks) - i < 5:
                src.error(line, '%s needs four values (op1-op4)' % t)
            for k in range(4):
                tok = toks[i + 1 + k]
                rec[off + k] |= number(src, tok.line, tok, 0, top) << shift
            i += 5
        elif t == 'unused':
            # OFFSET:VALUE words to the end of its line.
            i += 1
            while i < len(toks) and toks[i].line == line:
                tok = toks[i]
                head, colon, value = tok.partition(':')
                said = "'%s': unused takes OFFSET:VALUE" % tok
                if (not colon or not head or len(head) >= 16
                        or not head[0].isdigit() or c_number(head) is None):
                    src.error(line, said)
                off = c_number(head)
                if off >= size:
                    src.error(line, 'unused: offset %d past the %d bytes'
                              % (off, size))
                v = c_number(value)
                if v is None or value[:1] in ('+', '-') or not 0 <= v <= 255:
                    src.error(line, said)
                if v & ~unused[off]:
                    src.error(line, 'unused %s: bits a field holds' % tok)
                rec[off] |= v
                i += 1
        else:
            src.error(line, '%r: not a field of %s instrument'
                      % (t, 'an FM' if kind == 'fm' else 'a PSG'))
        seen.add(t)
    missing = [n for n in list(sc) + list(rw) if n not in seen]
    if missing:
        src.error(line0, '%s_instrument: %s missing'
                  % (kind, ', '.join(missing)))
    return rec


def parse_instruments(src, line, toks, fm, psg):
    kind = toks[0].split('_')[0]
    if len(toks) not in (2, 3):
        src.error(line, '%s takes its number (and a name)' % toks[0])
    n = number(src, line, toks[1], 0, 255)
    table = fm if kind == 'fm' else psg
    if n in table:
        src.error(line, '%s %d given twice' % (toks[0], n))
    table[n] = parse_instrument(src, line, kind,
                                block_lines(src, BLOCK_WORDS))


def instrument_list(src, table, kind):
    for k in range(len(table)):
        if k not in table:
            src.error(0, '%s_instrument %d missing (they run from 0 with '
                      'none left out)' % (kind, k))
    return [table[k] for k in range(len(table))]


BLOCK_WORDS = {'song', 'volumes', 'track', 'phrase', 'fm_instrument',
               'psg_instrument', 'effects', 'effect'}


def parse_song(src):
    song = {'tracks': [None] * TRACKS, 'phrases': {}}
    fm, psg = {}, {}
    uses = {}
    count = None
    scan_names(src)
    while src.pos < len(src.lines):
        line, toks = src.lines[src.pos]
        src.pos += 1
        w = toks[0]
        if w == 'song':
            if 'volume' in song:
                src.error(line, 'song given twice')
            keys = {'volume': (0, 255), 'tempo': (0, 255),
                    'transpose': (-128, 255), 'phrases': (0, NOTE_END)}
            vals = {}
            i = 1
            while i < len(toks):
                if toks[i] not in keys or i + 1 >= len(toks):
                    src.error(line, '%r: song takes volume, tempo, '
                              'transpose (and phrases)' % toks[i])
                lo, hi = keys[toks[i]]
                vals[toks[i]] = number(src, line, toks[i + 1], lo, hi)
                i += 2
            for k in ('volume', 'tempo', 'transpose'):
                if k not in vals:
                    src.error(line, 'song: %s missing' % k)
            song['volume'] = vals['volume']
            song['tempo'] = vals['tempo']
            song['transpose'] = vals['transpose'] & 0xFF
            count = vals.get('phrases')
        elif w == 'volumes':
            if len(toks) != 10:
                src.error(line, 'volumes takes nine (fm1-fm6, psg1, psg2 '
                          'and one nothing reads)')
            song['volumes'] = [number(src, line, t, 0, 255)
                               for t in toks[1:]]
        elif w == 'track':
            if len(toks) != 2 or toks[1] not in CHANNELS[:TRACKS]:
                src.error(line, 'track takes a channel, fm1-fm6, psg1 or '
                          'psg2')
            t = CHANNELS.index(toks[1])
            if song['tracks'][t] is not None:
                src.error(line, 'track %s given twice' % toks[1])
            song['tracks'][t] = parse_order(src, block_lines(src,
                                                             BLOCK_WORDS), t)
            song.setdefault('track_lines', {})[t] = line
        elif w == 'phrase':
            if len(toks) != 2:
                src.error(line, 'phrase takes its number')
            k = number(src, line, toks[1], 0, NOTE_END - 1)
            if k in song['phrases']:
                src.error(line, 'phrase %d given twice' % k)
            song['phrases'][k] = parse_phrase(src, block_lines(src,
                                                               BLOCK_WORDS),
                                              uses.setdefault(k, {}))
        elif w in ('fm_instrument', 'psg_instrument'):
            parse_instruments(src, line, toks, fm, psg)
        else:
            src.error(line, '%r: not song, volumes, track, phrase, '
                      'fm_instrument or psg_instrument' % w)
    if 'volume' not in song:
        src.error(0, 'no song line')
    if 'volumes' not in song:
        src.error(0, 'no volumes line')
    for t in range(TRACKS):
        if song['tracks'][t] is None:
            song['tracks'][t] = []
    defined = max(song['phrases']) + 1 if song['phrases'] else 0
    if count is not None and count < defined:
        src.error(0, 'phrases %d: phrase %d is defined' % (count,
                                                           defined - 1))
    song['phrase_count'] = defined if count is None else count
    for t, items in enumerate(song['tracks']):
        for it in items:
            if it[0] == 'phrase' and it[1] not in song['phrases']:
                src.error(song.get('track_lines', {}).get(t, 0),
                          'track %s plays phrase %d, which is not defined'
                          % (CHANNELS[t], it[1]))
            if it[0] == 'phrase':
                check_phrase_kind(src, uses[it[1]], t, 'phrase %d' % it[1])
    song['fm'] = instrument_list(src, fm, 'fm')
    song['psg'] = instrument_list(src, psg, 'psg')
    song['fm_names'], song['psg_names'] = names_lists(src, song)
    song.pop('track_lines', None)
    return song


def parse_effects(src):
    fx = {'effects': [], 'phrases': []}
    fm, psg = {}, {}
    names = {}
    refs = []
    uses = {}
    scan_names(src)
    while src.pos < len(src.lines):
        line, toks = src.lines[src.pos]
        src.pos += 1
        w = toks[0]
        if w == 'effects':
            if len(toks) != 3 or toks[1] != 'first_byte':
                src.error(line, 'effects takes first_byte N')
            fx['first_byte'] = number(src, line, toks[2], 0, 255)
        elif w == 'effect':
            if len(toks) != 3:
                src.error(line, 'effect takes its number and name')
            k = number(src, line, toks[1], 0, 255)
            if k != len(fx['effects']):
                src.error(line, 'effect %d: the next is %d (they run from '
                          '0 in order)' % (k, len(fx['effects'])))
            tracks = []
            for tl, tt in block_lines(src, BLOCK_WORDS - {'track'}):
                if tt[0] != 'track' or len(tt) < 3 or tt[1] not in CHANNELS:
                    src.error(tl, 'an effect\'s line is track CHANNEL '
                              '[commands] PHRASE')
                flags = []
                c = CHANNELS.index(tt[1])
                i = 2
                while i < len(tt) - 1:
                    r = take_common(src, tl, tt, i)
                    if r is None:
                        src.error(tl, '%r: not a command' % tt[i])
                    if r[1] > len(tt) - 1:
                        src.error(tl, 'the phrase\'s name missing')
                    check_kind(src, tl, tt, i, c)
                    flags.append(r[0])
                    i = r[1]
                tracks.append({'channel': c, 'flags': flags,
                               'phrase': tt[-1]})
                refs.append((tl, tt[-1], c))
            if not tracks:
                src.error(line, 'effect %d has no track' % k)
            fx['effects'].append({'name': toks[2], 'tracks': tracks})
        elif w == 'phrase':
            if len(toks) != 2:
                src.error(line, 'phrase takes its name')
            if toks[1] in names:
                src.error(line, 'phrase %s given twice' % toks[1])
            names[toks[1]] = line
            fx['phrases'].append((toks[1], parse_phrase(
                src, block_lines(src, BLOCK_WORDS),
                uses.setdefault(toks[1], {}))))
        elif w in ('fm_instrument', 'psg_instrument'):
            parse_instruments(src, line, toks, fm, psg)
        else:
            src.error(line, '%r: not effects, effect, phrase, '
                      'fm_instrument or psg_instrument' % w)
    if 'first_byte' not in fx:
        src.error(0, 'no effects line')
    for tl, name, c in refs:
        if name not in names:
            src.error(tl, 'phrase %s is not defined' % name)
        check_phrase_kind(src, uses[name], c, 'phrase %s' % name)
    fx['fm'] = instrument_list(src, fm, 'fm')
    fx['psg'] = instrument_list(src, psg, 'psg')
    fx['fm_names'], fx['psg_names'] = names_lists(src, fx)
    return fx


def read_text(read, exists=None):
    """The model from the text; read(name) gives a file's text. With
    exists(name), a song file's tracker beside it (NN_title.trk) is read
    in its place where there is one, as the game does (hztracker.py)."""
    src = Source('sounds.txt', read('sounds.txt'))
    model = {'songs': [], 'tail': []}
    files = []
    in_tail = False
    for line, toks in src.lines:
        w = toks[0]
        if in_tail:
            for t in toks:
                if not re.fullmatch(r'[0-9A-Fa-f]{2}', t):
                    src.error(line, '%r: the tail is hex bytes' % t)
                model['tail'].append(int(t, 16))
            continue
        if w == 'song_table':
            if len(toks) != 2:
                src.error(line, 'song_table takes its entries')
            model['song_table'] = number(src, line, toks[1], 1, 0x3FFF)
        elif w == 'song':
            if len(toks) != 3:
                src.error(line, 'song takes its number and file')
            n = number(src, line, toks[1], 0, 255)
            if n != len(files):
                src.error(line, 'song %d: the next is %d (they run from 0 '
                          'in order)' % (n, len(files)))
            files.append(toks[2])
        elif w == 'effects':
            if len(toks) != 3:
                src.error(line, 'effects takes the table\'s address and '
                          'its file')
            model['effects_at'] = number(src, line, toks[1], BASE, 0xFFFF)
            model['effects_file'] = toks[2]
        elif w == 'tail':
            if len(toks) != 1:
                src.error(line, 'tail\'s bytes go on the lines after it')
            in_tail = True
        else:
            src.error(line, '%r: not song_table, song, effects or tail' % w)
    for k in ('song_table', 'effects_at'):
        if k not in model:
            src.error(0, 'no %s line' % k.split('_')[0])
    model['song_files'] = files
    for n, name in enumerate(files):
        trk = name[:-4] + '.trk' if name.endswith('.txt') else None
        if exists is not None and trk is not None and exists(trk):
            import hztracker            # (it imports this module)
            model['songs'].append(hztracker.read_tracker(trk, read(trk)))
            files[n] = trk
            continue
        model['songs'].append(parse_song(Source(name, read(name))))
    model['effects'] = parse_effects(Source(model['effects_file'],
                                            read(model['effects_file'])))
    return model


# -- the bytes from the model --------------------------------------------

def put16(out, at, v):
    out[at] = v & 0xFF
    out[at + 1] = v >> 8


def song_bytes(song):
    out = bytearray(16)
    out[0] = SONG_FIRST
    out[1], out[2], out[3] = song['volume'], song['tempo'], song['transpose']
    put16(out, 4, len(out))
    out += bytes(song['volumes'])
    trk = len(out)
    put16(out, 6, trk)
    out += bytes(2 * TRACKS)
    for t, items in enumerate(song['tracks']):
        put16(out, trk + 2 * t, len(out))
        out += bytes(items_bytes(items) + [END])
    # Track_NextPhrase hands back the phrase table offset's low byte, and
    # the caller takes $FF for the order list's end: keep it off $FF.
    if len(out) & 0xFF == 0xFF:
        out.append(0)
    phr = len(out)
    put16(out, 8, phr)
    out += bytes(2 * song['phrase_count'])
    for k in sorted(song['phrases']):
        put16(out, phr + 2 * k, len(out))
        out += bytes(items_bytes(song['phrases'][k]) + [END])
    put16(out, 10, len(out))
    for rec in song['fm']:
        out += bytes(rec)
    put16(out, 12, len(out))
    for rec in song['psg']:
        out += bytes(rec)
    put16(out, 14, len(out) - 1)
    if len(out) > 0x7FFF:
        raise TextError('a song of %d bytes: past the window' % len(out))
    return out


def effects_bytes(fx, at):
    out = bytearray(8)
    out[0] = fx['first_byte']
    out[1] = len(fx['effects'])
    out += bytes(2 * len(fx['effects']))
    fixups = []
    for k, e in enumerate(fx['effects']):
        put16(out, 8 + 2 * k, len(out))
        out.append(len(e['tracks']))
        for t in e['tracks']:
            out.append(t['channel'])
            out += bytes(items_bytes(t['flags']))
            fixups.append((len(out), t['phrase']))
            out += bytes(2)
    where = {}
    for name, items in fx['phrases']:
        where[name] = len(out)
        out += bytes(items_bytes(items) + [END])
    for pos, name in fixups:
        if where[name] >= NOTE_END << 8:
            raise TextError('effects.txt: phrase %s past $%04X'
                            % (name, at + (NOTE_END << 8)))
        out[pos], out[pos + 1] = where[name] >> 8, where[name] & 0xFF
    put16(out, 2, len(out))
    for rec in fx['fm']:
        out += bytes(rec)
    put16(out, 4, len(out))
    for rec in fx['psg']:
        out += bytes(rec)
    put16(out, 6, len(out) - 1)
    return out


def build(model):
    """The window and, when the songs outgrow it, a window per song (None
    while they fit): songtext_build's."""
    img = bytearray([0xFF]) * SIZE
    entries = model['song_table']
    nsongs = len(model['songs'])
    if nsongs + 1 > entries:
        raise TextError('sounds.txt: %d songs and the end need %d entries, '
                        'song_table has %d' % (nsongs, nsongs + 1, entries))
    songs = [song_bytes(song) for song in model['songs']]
    # The one window, as the ROM's: the songs in order after the table,
    # each from the last byte of the one before when that is a 9, and the
    # table's entry after them where a next one would go. Songs that end
    # past the effects there get a window each instead: the song right
    # after the table (and the songs after it, below), its entry the only
    # one; the one window keeps the effects and a table of zeros (no
    # song).
    e = model['effects_at']
    first = BASE + 2 * entries
    if e < first:
        raise TextError('sounds.txt: the effects at $%04X are in the song '
                        'table (it ends at $%04X)' % (e, first))
    table = [0] * entries
    at = end = first
    for n, b in enumerate(songs):
        table[n] = at - BASE
        end = at + len(b)
        at = end - 1 if b[-1] == SONG_FIRST else end
    shared = end <= e
    if shared:
        table[len(songs)] = end - 1 - BASE
        for n, b in enumerate(songs):
            img[table[n]:table[n] + len(b)] = b
        for k, v in enumerate(table):
            img[2 * k] = v >> 8
            img[2 * k + 1] = v & 0xFF
    else:
        img[:2 * entries] = bytes(2 * entries)
        for n, b in enumerate(songs):
            if first + len(b) > e:
                name = model.get('song_files', ['?'] * nsongs)[n]
                raise TextError('sounds.txt: song %d (%s) ends at $%04X, '
                                'past the effects at $%04X (%d bytes over)'
                                % (n, name, first + len(b), e,
                                   first + len(b) - e))
    fx = effects_bytes(model['effects'], e)
    tail = bytes(model['tail'])
    if e + len(fx) + len(tail) > 0x10000:
        raise TextError('sounds.txt: the effects (and the tail) run %d '
                        'bytes past $FFFF'
                        % (e + len(fx) + len(tail) - 0x10000))
    img[e - BASE:e - BASE + len(fx)] = fx
    t = e + len(fx) - BASE
    img[t:t + len(tail)] = tail
    if shared:
        return bytes(img), None
    windows = []
    for n in range(nsongs):
        w = bytearray(img)
        w[2 * n], w[2 * n + 1] = (first - BASE) >> 8, (first - BASE) & 0xFF
        # The songs from n on, chained as in the one window, up to the
        # effects: what a song reads past its end is what the one window
        # has there (the title's PSG tracks, with no PSG instrument of its
        # own, take song 1's first bytes).
        q = first
        for b in songs[n:]:
            if q >= e:
                break
            k = min(len(b), e - q)
            w[q - BASE:q - BASE + k] = b[:k]
            q += len(b) - (1 if b[-1] == SONG_FIRST else 0)
        windows.append(bytes(w))
    return bytes(img), windows


# -- the heard notes -----------------------------------------------------
#
# A note is heard as other than it is written where a transpose moves it
# (the track's, the song's), a detune of half a semitone or more takes it
# nearer the next, or it is a PSG tone (an octave up, and nothing under C3:
# Track_WriteFrequency halves the period only from octave 3). `annotate`
# walks every track as the driver plays it, repeats and the song's repeat
# from its loop point included, and puts a `# heard:` comment on each line
# with such a note: the line's notes as heard, `A|B` for one reached at
# more than one pitch. The readers skip them (they are comments);
# `annotate` writes them again after an edit.

NOTES = 0x60                    # the driver's note numbers, C0-B7
FM_TRACKS = 6                   # a song's fm1-fm6; then psg1, psg2
NOISE = 8                       # an effect's noise channel
HEARD_RE = re.compile(r'\s*# heard:.*$')


def note_add(note, more):
    """Note_Add: a note and a transpose, kept under $60 (a sum past $FF
    wraps by $60 the other way, as the driver's)."""
    s = note + more
    low = s & 0xFF
    if s > 0xFF:
        return (low + NOTES) & 0xFF
    return low if low < NOTES else low - NOTES


def walk_flag(it, st):
    """The flags that move a note's pitch: Flag_Transpose (0 clears; else
    added, mod $60) and Flag_Detune."""
    if it[1] == 0xE8:
        t = 0
        if it[2] != 0:
            t = (st['transpose'] + it[2]) & 0xFF
            if t & 0x80:
                t = (t + NOTES) & 0xFF
            elif t >= NOTES:
                t -= NOTES
        st['transpose'] = t
    elif it[1] == 0xEB:
        st['detune'] = it[2]


def heard_value(v, st, song_transpose, psg):
    """Track_PlayNote's note (the track's transpose, then the song's) with
    the detune (Track_SetNote, 1/32 semitone), named by the nearer
    semitone; a PSG tone's as it sounds."""
    n = note_add(song_transpose, note_add(v, st['transpose']))
    detune = st['detune'] - 256 if st['detune'] & 0x80 else st['detune']
    r = max(0, (n * 32 + detune + 16) >> 5)
    if psg:
        octave, semitone = divmod(r, 12)
        return (max(octave, 2) + 1) * 12 + semitone
    return r


def walk_phrase(items, st, song_transpose, psg, heard, budget):
    """A phrase as Phrase_NextNote reads it: repeats ($EC n ... $FD) of
    n passes (0: 256), three open at most."""
    stack = []
    i = 0
    while i < len(items) and budget[0] > 0:
        budget[0] -= 1
        it = items[i]
        i += 1
        if it[0] == 'note':
            v = heard_value(it[1], st, song_transpose, psg)
            got = heard.setdefault(id(it), [])
            if v not in got:
                got.append(v)
        elif it[0] == 'flag':
            walk_flag(it, st)
            if it[1] == 0xEC and len(stack) < 3:
                stack.append([it[2], i])
        elif it[0] == 'byte' and it[1] == LOOP and stack:
            top = stack[-1]
            top[0] = (top[0] - 1) & 0xFF
            if top[0] == 0:
                stack.pop()
            else:
                i = top[1]


def heard_song(song, heard):
    """Every track from its start to its end; in a song with a `loop` in
    any track, then from its loop point (its start if it has none) again,
    until it gets there with a transpose and detune it had there before.
    The game repeats exactly those (the maps', the options', the map
    select's and the staff roll's: MusicTestRepeat); the rest play once
    (the title, the winner's and the endings')."""
    repeats = any(it == ('byte', LOOP) for items in song['tracks']
                  for it in items)
    for t, items in enumerate(song['tracks']):
        psg = t >= FM_TRACKS
        st = {'transpose': 0, 'detune': 0}
        budget = [1 << 20]

        def run(start, loop):
            for k in range(start, len(items)):
                it = items[k]
                if it[0] == 'phrase':
                    walk_phrase(song['phrases'].get(it[1], []), st,
                                song['transpose'], psg, heard, budget)
                elif it[0] == 'flag':
                    walk_flag(it, st)
                elif it[0] == 'byte' and it[1] == LOOP:
                    loop = k + 1
            return loop

        loop = run(0, 0)
        seen = set()
        while (repeats and any(it[0] == 'phrase' for it in items[loop:])
               and (st['transpose'], st['detune']) not in seen
               and budget[0] > 0):
            seen.add((st['transpose'], st['detune']))
            loop = run(loop, loop)


def heard_effects(fx, heard):
    """Every effect's tracks, each from its own commands (the song's
    transpose taken as 0, as every song of the ROM's has it). The noise
    channel's notes set the third tone's pitch, heard (if at all) through
    the noise: left out."""
    phrases = dict(fx['phrases'])
    for e in fx['effects']:
        for tr in e['tracks']:
            if tr['channel'] >= NOISE:
                continue
            st = {'transpose': 0, 'detune': 0}
            for it in tr['flags']:
                if it[0] == 'flag':
                    walk_flag(it, st)
            walk_phrase(phrases.get(tr['phrase'], []), st, 0,
                        tr['channel'] >= FM_TRACKS, heard, [1 << 20])


def annotate_text(name, text):
    """A song's or the effects' text with its `# heard:` comments written
    anew (the rest as it is)."""
    lines = [HEARD_RE.sub('', line) for line in text.split('\n')]
    src = Source(name, '\n'.join(lines))
    heard = {}
    if src.lines and src.lines[0][1][0] == 'effects':
        fx = parse_effects(src)
        heard_effects(fx, heard)
        groups = [items for _, items in fx['phrases']]
    else:
        song = parse_song(src)
        heard_song(song, heard)
        groups = list(song['phrases'].values())
    by_line = {}
    for items in groups:
        for it in items:
            if it[0] == 'note':
                by_line.setdefault(it[3], []).append(it)
    for n, notes in by_line.items():
        texts = []
        differs = False
        for it in notes:
            got = heard.get(id(it), [])
            if got and got != [it[1]]:
                differs = True
            texts.append('|'.join(note_name(v) for v in got) if got
                         else note_name(it[1]))
        if differs:
            lines[n - 1] = (lines[n - 1].rstrip() + '    # heard: '
                            + '  '.join(texts))
    return '\n'.join(lines)


def annotate(paths):
    """The heard comments written again in each song or effects file named,
    or in a folder's (its sounds.txt's). Returns the files changed."""
    files = []
    for path in paths:
        if os.path.isdir(path):
            with open(os.path.join(path, 'sounds.txt'),
                      encoding='utf-8-sig') as fh:
                top = Source('sounds.txt', fh.read())
            for _, toks in top.lines:
                if toks[0] in ('song', 'effects') and len(toks) == 3:
                    files.append(os.path.join(path, toks[2]))
        else:
            files.append(path)
    changed = []
    for path in files:
        with open(path, encoding='utf-8-sig') as fh:
            text = fh.read()
        new = annotate_text(path, text)
        if new != text:
            with open(path, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write(new)
            changed.append(path)
    return changed


# -- the commands --------------------------------------------------------

def menu_titles(path):
    if not path or not os.path.exists(path):
        return []
    with open(path, encoding='utf-8') as fh:
        songs = json.load(fh)['songs']
    return [' '.join(' '.join(s['title']).split()) for s in songs]


def extract(data, folder, titles):
    model = read_image(data)
    if build(model) != (data, None):
        raise ValueError('the model doesn\'t build back into the bytes')
    files = write_text(model, titles)
    for name in files:
        if name != 'sounds.txt':
            files[name] = annotate_text(name, files[name])
    again, windows = build(read_text(lambda name: files[name]))
    if windows is not None:
        raise ValueError('the text builds back a window per song')
    if again != data:
        diff = next(k for k in range(SIZE) if again[k] != data[k])
        raise ValueError('the text builds back different from $%04X'
                         % (BASE + diff))
    os.makedirs(os.path.join(folder, 'songs'), exist_ok=True)
    for name, text in files.items():
        with open(os.path.join(folder, name), 'w', encoding='utf-8',
                  newline='\n') as fh:
            fh.write(text)
    import hztracker                    # (it imports this module)
    for name in hztracker.export(folder):
        files[name] = None
    return files


def build_folder(folder, trackers=True):
    """The window, then each song's own when they have them (as one
    string of bytes, 32 KB a window); a song's tracker read in place of its
    text where there is one, as the game does, unless trackers is False."""
    def read(name):
        with open(os.path.join(folder, name), encoding='utf-8-sig') as fh:
            return fh.read()

    def exists(name):
        return os.path.isfile(os.path.join(folder, name))
    img, windows = build(read_text(read, exists if trackers else None))
    return img + b''.join(windows or [])


def windows_said(data):
    n = len(data) // SIZE - 1
    return ', the effects, then %d songs\' own windows' % n if n else ''


def main():
    args = sys.argv[1:]
    try:
        if args[:1] == ['extract'] and len(args) in (3, 4):
            with open(args[1], 'rb') as fh:
                data = fh.read()
            files = extract(data, args[2], menu_titles(
                args[3] if len(args) > 3 else None))
            print('%s: %d files, the text built back the same (%d bytes), '
                  'the trackers playing as it' % (args[2], len(files),
                                                  len(data)))
        elif args[:1] == ['build'] and len(args) == 3:
            data = build_folder(args[1])
            with open(args[2], 'wb') as fh:
                fh.write(data)
            print('%s: %d bytes%s' % (args[2], len(data), windows_said(data)))
        elif args[:1] == ['annotate'] and len(args) >= 2:
            changed = annotate(args[1:])
            print('heard notes written again in %d files' % len(changed))
            for path in changed:
                print('  ' + path)
        elif args[:1] == ['check'] and len(args) == 3:
            data = build_folder(args[1], trackers=False)
            with open(args[2], 'rb') as fh:
                want = fh.read()
            if data != want:
                diff = next((k for k in range(min(len(data), len(want)))
                             if data[k] != want[k]), min(len(data),
                                                         len(want)))
                sys.exit('%s: differs from %s at window %d, $%04X'
                         % (args[1], args[2], diff // SIZE,
                            BASE + diff % SIZE))
            print('%s: the same as %s (%d bytes%s)'
                  % (args[1], args[2], len(data), windows_said(data)))
        else:
            sys.exit(__doc__.split('\n\n')[1])
    except TextError as e:
        sys.exit(str(e))


if __name__ == '__main__':
    sys.modules['hzsongtext'] = sys.modules[__name__]   # one module, imported or run
    main()
