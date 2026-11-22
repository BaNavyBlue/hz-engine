#!/usr/bin/env python3
"""Herzog Zwei's songs laid out as a tracker (FastTracker II's way, as the
PS2 port's tools/tracker.py): every channel side by side in rows of ticks,
the notes as heard, so the parts can be read against each other and edited
by position. docs/sound.md, "The tracker", has the format.

    python3 tools/hztracker.py export DIR [SONG ...]
    python3 tools/hztracker.py text FILE.trk

`export` writes the songs of DIR's song text (sounds.txt's song files, all
of them or the numbers given) as trackers beside them, songs/NN_title.trk,
each only once it plays as its text: both are walked through the driver's
sequencing (MusicTrack_Tick) side by side, every channel's notes, lengths,
gates, volumes and commands tick for tick, over the song and two more
turns of it. The game reads a song's .trk in place of its .txt
(src/audio/songtext.c builds the bytes this does), and so does
hzsongtext.py's build; hzsongtext.py's extract writes them. `text` prints
the song text a tracker reads as.
"""
import os
import re
import sys

import hzsongtext as h

SECTION_TICKS = (96, 192, 384)  # the exporter's choice: the one that writes least
SONG_ROWS = (6, 8, 4, 3, 2, 1)  # a song's ticks a row, the first that fits
ROW_TICKS = (8, 6, 4, 3, 2, 1)  # and finer, in a section that needs it
NOTE_MAX = 127                  # a note's length or gate: a byte under $80
PSG_OCTAVE = 12                 # a PSG column names its notes an octave up
INSTRUMENT, WAIT, TRANSPOSE, REPEAT = 0xE0, 0xE5, 0xE8, 0xEC
NOT_IN_CELLS = {TRANSPOSE: 'the notes are written as heard, so there is no '
                           'transpose (but in an end cell: the next turn\'s)',
                WAIT: 'a channel starts where its first cell is, and goes '
                      'on from cell to cell',
                REPEAT: 'a tracker writes its repeats out'}
BLOCK_WORDS = {'song', 'volumes', 'channel', 'section', 'row',
               'fm_instrument', 'psg_instrument'}


def transposed(t, arg):
    """Flag_Transpose: 0 clears; else added, kept under $60."""
    if arg == 0:
        return 0
    v = (t + arg) & 0xFF
    if v & 0x80:
        return (v + h.NOTES) & 0xFF
    return v - h.NOTES if v >= h.NOTES else v


def is_psg(c):
    return c >= h.FM_TRACKS


def cell_note_name(note, c):
    """FastTracker's names, three letters: C-4, C#4; a PSG channel's an
    octave up (as it sounds from C3 up)."""
    if is_psg(c):
        note += PSG_OCTAVE
    name = h.NOTE_NAMES[note % 12]
    return '%s%s%d' % (name, '-' if len(name) == 1 else '', note // 12)


def cell_note_value(tok, c):
    """The driver's note for a cell's name (C-4, C#4, Db4 or C4), or
    None."""
    m = re.fullmatch(r'([A-G])([-#b]?)([0-9])', tok)
    if not m:
        return None
    name = m.group(1) + (m.group(2) if m.group(2) != '-' else '')
    if name in h.NOTE_NAMES:
        semi = h.NOTE_NAMES.index(name)
    elif name in h.FLAT_NAMES:
        semi = h.FLAT_NAMES[name]
    else:
        semi = {'Cb': -1, 'Fb': 4, 'E#': 5, 'B#': 12}[name]
    v = int(m.group(3)) * 12 + semi
    return v - PSG_OCTAVE if is_psg(c) else v


def default_gate(length):
    """A note with no gate written is let go 2 ticks before the next cell
    (after 1 tick at least): as most of the songs' notes are."""
    return max(1, length - 2)


# -- the driver's sequencing ---------------------------------------------

class Walk:
    """One track as MusicTrack_Tick plays it (src/audio/hz_snd.c), its
    events as (tick, kind, ...): 'cmd' a command run (its item), 'mark' the
    order list's loop point passed (the transpose there), 'note' (the note
    after the transposes, length, gate, volume, held: not struck again),
    'off' a key off, 'end' the order list's end (the transpose there),
    'wrap' back to the loop point. Tick 0 is the song's start
    (Song_StartTracks), the first tick 1."""

    def __init__(self, song, t, repeat):
        self.phrases = song['phrases']
        self.items = song['tracks'][t]
        self.song_transpose = song['transpose']
        self.repeat = repeat
        self.order = self.loop = 0
        self.phrase, self.pi = [], 0
        self.length, self.gate = 1, 0
        self.transpose = 0
        self.sticky = [0, 0, 0]
        self.stack = []
        self.events = []
        self.tick = 0
        self.on = self.next_phrase()

    def ev(self, *e):
        self.events.append((self.tick,) + e)

    def flag(self, it):
        if it[1] == REPEAT:
            if len(self.stack) < 3:
                self.stack.append([it[2], self.pi])
            return
        if it[1] == TRANSPOSE:
            self.transpose = transposed(self.transpose, it[2])
        elif it[1] == WAIT:
            if it[2] == 0:
                raise ValueError('wait 0 (every waiting track on to its '
                                 'phrase\'s end) is not followed')
            self.length, self.gate = it[2] + 1, 1
        self.ev('cmd', it)

    def next_phrase(self):
        """Track_NextPhrase: the order list's commands run, its loop point
        noted, up to a phrase; False at its end."""
        while self.order < len(self.items):
            it = self.items[self.order]
            self.order += 1
            if it[0] == 'phrase':
                self.phrase, self.pi = self.phrases.get(it[1], []), 0
                return True
            if it[0] == 'flag':
                self.flag(it)
            elif it == ('byte', h.LOOP):
                self.loop = self.order
                self.ev('mark', self.transpose)
        self.ev('end', self.transpose)
        return False

    def next_note(self):
        """Phrase_NextNote: commands run, repeats closed or gone round, to a
        note (its numbers kept: length, gate, volume); None at the phrase's
        end."""
        while self.pi < len(self.phrase):
            it = self.phrase[self.pi]
            self.pi += 1
            if it[0] == 'note':
                for k, v in enumerate(it[2]):
                    self.sticky[k] = v
                return it
            if it[0] == 'flag':
                self.flag(it)
            elif it == ('byte', h.LOOP) and self.stack:
                top = self.stack[-1]
                top[0] = (top[0] - 1) & 0xFF
                if top[0] == 0:
                    self.stack.pop()
                else:
                    self.pi = top[1]
        self.stack = []
        return None

    def step(self, tick):
        self.tick = tick
        if not self.on:
            return
        self.length = (self.length - 1) & 0xFF
        if self.length != 0:
            self.gate = (self.gate - 1) & 0xFF
            if self.gate == 0:
                self.ev('off')
            return
        while self.length == 0:
            it = self.next_note()
            if it is not None:
                length, gate, volume = self.sticky
                note = h.note_add(self.song_transpose,
                                  h.note_add(it[1], self.transpose))
                held = ((0 - self.gate) & 0x80) != 0
                self.ev('note', note, length, gate, volume, held)
                self.length, self.gate = length, gate
                return
            while not self.next_phrase():
                self.ev('off')
                if not self.repeat:
                    self.on = False
                    return
                self.order = self.loop
                self.ev('wrap')


def song_repeats(song):
    """The game repeats exactly the songs with a loop point in a track
    (MusicTestRepeat; hzsongtext.heard_song)."""
    return any(it == ('byte', h.LOOP) for items in song['tracks']
               for it in items)


def first_pass(song, t):
    """Channel t's events to the end of its order list."""
    w = Walk(song, t, False)
    tick = 1
    while w.on:
        w.step(tick)
        tick += 1
    return w.events


# -- a song's first pass as cells ----------------------------------------

def channel_cells(song, t):
    """Channel t as the tracker writes it: its commands as the song starts
    (('loop',) where the loop point is among them), its cells (tick, note,
    length, gate, volume, the commands before it, ('loop',) among them
    where the order list's loop point is) and its end (tick, the commands
    before it), the end None with no notes. A channel whose transposes add
    up from turn to turn (the song text's are relative) has its end say how
    far: a transpose there, what the next turn plays at, if the song is
    repeated."""
    line, cells, pending = [], [], []
    end = None
    for e in first_pass(song, t):
        tick, kind = e[0], e[1]
        if kind == 'cmd':
            it = e[2]
            if it[1] == WAIT and tick != 0:
                raise ValueError('channel %s: a wait after the start'
                                 % h.CHANNELS[t])
            if it[1] not in (WAIT, TRANSPOSE):
                (line if tick == 0 else pending).append(it)
        elif kind == 'mark':
            (line if tick == 0 else pending).append(('loop',))
        elif kind == 'note':
            note, length, gate, volume = e[2:6]
            cells.append({'tick': tick, 'note': note, 'length': length,
                          'gate': gate, 'volume': volume,
                          'changes': pending})
            pending = []
        elif kind == 'end' and cells:
            end = {'tick': tick, 'changes': pending}
            more = turn_transpose(song, t)
            if more:
                pending.append(('flag', TRANSPOSE, more & 0xFF))
    if not cells:
        line += pending
    for a, b in zip(cells, cells[1:] + [end]):
        if b['tick'] - a['tick'] != (a['length'] or 256):
            raise ValueError('channel %s: a note at tick %d lasts %d, the next '
                             'is at %d' % (h.CHANNELS[t], a['tick'],
                                           a['length'], b['tick']))
    return line, cells, end


def turn_transpose(song, t):
    """How far a repeated channel's next turn plays from its first, the
    text's transposes adding up (they are relative): the same for every
    note of the turn, or 0 (a channel that clears its transpose before its
    notes plays every turn the same)."""
    w = Walk(song, t, True)
    tick = 1
    ends = seen = 0
    while w.on and ends < 2:
        w.step(tick)
        tick += 1
        ends += sum(e[1] == 'end' for e in w.events[seen:])
        seen = len(w.events)
    first, second = [], []
    turn = 0
    looped = not any(e[1] == 'mark' for e in w.events)
    for e in w.events:
        if e[1] == 'mark':
            looped = True
        elif e[1] == 'wrap':
            turn += 1
        elif e[1] == 'note' and turn == 0 and looped:
            first.append(e[2])
        elif e[1] == 'note' and turn == 1:
            second.append(e[2])
    more = {(b - a) % h.NOTES for a, b in zip(first, second)}
    if len(more) != 1 or len(first) != len(second):
        return 0
    more = more.pop()
    return more - h.NOTES if more > h.NOTES // 2 else more


def instruments_read(song, image, start):
    """The song's instruments, and past its own the ones its channels pick
    as the ROM's layout has them there (the title's and Back to square
    one's PSG channels read instrument 0 from the next song's first bytes:
    docs/sound.md, Room), so the tracker's song has its own."""
    top = {'fm': 0, 'psg': -1}
    for t in range(h.TRACKS):
        kind = 'psg' if is_psg(t) else 'fm'
        line, cells, _ = channel_cells(song, t)
        if cells:
            top[kind] = max(top[kind], 0)
        for it in line + [x for c in cells for x in c['changes']]:
            if it[0] == 'flag' and it[1] == INSTRUMENT:
                top[kind] = max(top[kind], it[2])
    data = bytes(h.song_bytes(song))
    fm = list(song['fm'])
    psg = list(song['psg'])
    fm_at = start + (data[10] | data[11] << 8)
    psg_at = start + (data[12] | data[13] << 8)
    while len(fm) <= top['fm']:
        a = fm_at + h.FM_SIZE * len(fm) - h.BASE
        fm.append(list(image[a:a + h.FM_SIZE]))
    while len(psg) <= top['psg']:
        a = psg_at + h.PSG_SIZE * len(psg) - h.BASE
        psg.append(list(image[a:a + h.PSG_SIZE]))
    return fm, psg


# -- the text ------------------------------------------------------------

def change_text(it, names=None):
    if it == ('loop',):
        return 'loop'
    return h.flag_text(*it[1:], names=names)


def gate_text(gate, length):
    if gate >= length:
        return 'hold'
    if gate == default_gate(length):
        return None
    return 'gate %d' % gate


def cell_key(cell, start):
    """What a copy (`fm1 = 03`) has to match: the place in its section,
    the note, the gate, the volume, the commands."""
    if 'note' not in cell:
        return (cell['tick'] - start, 'end', tuple(cell['changes']))
    return (cell['tick'] - start, cell['note'],
            gate_text(cell['gate'], cell['length']), cell['volume'],
            tuple(cell['changes']))


def tempo_said(tempo):
    per_second = 53267.0 / (16 * (8324 // (tempo + 50)))
    return ('Tempo %d: %.1f ticks a second, a beat of 24 ticks at %d BPM'
            % (tempo, per_second, round(per_second * 60 / 24)))


def song_rows(columns):
    """The song's rows: a sixteenth note (6 ticks) where that puts nine in
    ten of its cells on a row, else a triplet's 8 ticks, else finer; a
    channel's cells counted from its first (one that starts late keeps its
    @N all through)."""
    for r in SONG_ROWS:
        on = total = 0
        for _, cells in columns:
            for x in cells:
                total += 1
                on += (x['tick'] - cells[0]['tick']) % r == 0
        if on * 10 >= total * 9:
            return r
    return 1


def layout(columns, span, section):
    """The song in sections of `section` ticks (the last cut short after
    its last row with a cell): each its start, its ticks, its rows' ticks
    and, a column each, its cells and the section it copies (None: written
    out). The rows are the song's, finer in a section where a column has
    two cells in one."""
    rows = song_rows(columns)
    sections = []
    seen = [{} for _ in columns]
    count = (span - 1) // section + 1
    for k in range(count):
        start = 1 + k * section
        parts = []
        for ci, (c, cells) in enumerate(columns):
            mine = [x for x in cells if start <= x['tick'] < start + section]
            copy = None
            if mine and k < count - 1 and not any(
                    ('loop',) in x['changes'] or 'note' not in x
                    for x in mine):
                key = tuple(cell_key(x, start) for x in mine)
                copy = seen[ci].setdefault(key, k)
                if copy == k:
                    copy = None
            parts.append((mine, copy))
        for r in ROW_TICKS:
            if r > rows or section % r:
                continue
            if all(len({(x['tick'] - start) // r for x in mine}) == len(mine)
                   for mine, copy in parts if copy is None):
                break
        ticks = section
        if k == count - 1:
            ticks = (max(x['tick'] - start for mine, _ in parts
                         for x in mine) // r + 1) * r
        sections.append((start, ticks, r, parts))
    return sections


def grid_text(columns, sections, names):
    """names: the song's instruments' names by kind (part_names)."""
    out = []
    volume = {c: None for c, _ in columns}
    for k, (start, ticks, r, parts) in enumerate(sections):
        rows = ticks // r
        out += ['', 'section %02d  %d row%s x %d tick%s    # ticks %d-%d'
                % (k, rows, 's' if rows > 1 else '', r, 's' if r > 1 else '',
                   start, start + ticks - 1)]
        grid = [[''] * len(columns) for _ in range(rows)]
        heads = []
        for ci, ((c, _), (mine, copy)) in enumerate(zip(columns, parts)):
            heads.append(h.CHANNELS[c] + ('' if copy is None
                                          else ' = %02d' % copy))
            for x in mine:
                o = x['tick'] - start
                if copy is None:
                    words = [cell_note_name(x['note'], c) if 'note' in x
                             else 'end']
                    if o % r:
                        words.append('@%d' % (o % r))
                    if 'note' in x:
                        g = gate_text(x['gate'], x['length'])
                        if g:
                            words.append(g)
                        if x['volume'] != volume[c]:
                            words.append('v%d' % x['volume'])
                    words += [change_text(it, names[h.channel_kind(c)])
                              for it in x['changes']]
                    grid[o // r][ci] = ' '.join(words)
                if 'note' in x:
                    volume[c] = x['volume']
        for row in grid:
            for ci in range(len(columns)):
                if not row[ci] and parts[ci][1] is None:
                    row[ci] = '...'
        widths = [max([len(heads[ci])] + [len(row[ci]) for row in grid])
                  for ci in range(len(columns))]
        out.append(('row | ' + ' | '.join(heads[ci].ljust(widths[ci])
                                          for ci in range(len(columns))))
                   .rstrip())
        for n, row in enumerate(grid):
            out.append((' %02d | ' % n + ' | '.join(
                row[ci].ljust(widths[ci])
                for ci in range(len(columns)))).rstrip())
    return out


def song_tracker_text(n, song, title, fm=None, psg=None):
    """A song as a tracker file; fm, psg: its instruments if not the
    song's (instruments_read)."""
    repeat = song_repeats(song)
    lines, columns = {}, []
    span = 1
    for t in range(h.TRACKS):
        line, cells, end = channel_cells(song, t)
        if line:
            lines[t] = line
        if cells:
            columns.append((t, cells + [end]))
            span = max(span, end['tick'])
    use = h.USES.get(n)
    out = ['# %s (song %d%s), as a tracker. docs/sound.md, "The tracker", '
           'has the format.' % (title or 'Song %d' % n, n,
                                ': ' + use if use else ''),
           '# %s. The game %s.' % (
               tempo_said(song['tempo']),
               'repeats it: each channel goes back to its loop from its end'
               if repeat else 'plays it once'),
           '',
           'song  volume %d  tempo %d' % (song['volume'], song['tempo']),
           'volumes  %s' % ' '.join(str(v) for v in song['volumes'])]
    names = h.part_names(song)
    for t in range(h.TRACKS):
        if t in lines or any(c == t for c, _ in columns):
            out.append('  '.join(['channel %s' % h.CHANNELS[t]]
                                 + [change_text(it, names[h.channel_kind(t)])
                                    for it in lines.get(t, [])]))
    best = None
    for section in SECTION_TICKS:
        text = grid_text(columns, layout(columns, span, section), names)
        size = sum(len(x) + 1 for x in text)
        if best is None or size < best[0]:
            best = (size, text)
    out += best[1]
    fm = song['fm'] if fm is None else fm
    psg = song['psg'] if psg is None else psg
    for k, rec in enumerate(fm):
        if k == len(song['fm']):
            out += ['', '# Instruments its channels pick past its own: as '
                    'the song text\'s layout has', '# them there '
                    '(docs/sound.md, Room).']
        out += [''] + h.instrument_text('fm', k, rec, names['fm'][k]
                                        if k < len(names['fm']) else None)
    for k, rec in enumerate(psg):
        if k == len(song['psg']):
            out += ['', '# Instruments its PSG channels pick past its own: '
                    'in the song text the bytes', '# after the song, the '
                    'start of the next (docs/sound.md, Room).']
        out += [''] + h.instrument_text('psg', k, rec, names['psg'][k]
                                        if k < len(names['psg']) else None)
    return '\n'.join(out) + '\n'


# -- reading a tracker ---------------------------------------------------

def take_change(src, line, toks, i, c, end=False):
    """A command (or `loop`) in a cell or a channel line of channel c;
    (item, next). A transpose only in an end cell (end)."""
    if toks[i] == 'loop':
        return ('loop',), i + 1
    r = h.take_common(src, line, toks, i)
    if r is None:
        src.error(line, '%r: not a note\'s gate or volume, or a command'
                  % toks[i])
    if r[0][0] == 'byte':
        src.error(line, 'byte %s: a tracker takes commands, not bytes'
                  % toks[i + 1])
    if r[0][1] in NOT_IN_CELLS and not (end and r[0][1] == TRANSPOSE):
        src.error(line, '%s: %s' % (toks[i], NOT_IN_CELLS[r[0][1]]))
    h.check_kind(src, line, toks, i, c)
    return r


def parse_cell(src, line, c, text, row_ticks, tick):
    """A cell's words: a note (or `end`), @N, its gate (`hold`, `gate N`)
    and volume (vN), the commands before it. None if empty."""
    toks = text.split()
    if not toks or toks == ['...']:
        return None
    cell = {'tick': tick, 'line': line, 'changes': []}
    if toks[0] != 'end':
        v = cell_note_value(toks[0], c)
        if v is None:
            src.error(line, '%r: a cell starts with a note, `...` or end'
                      % toks[0])
        if not 0 <= v < h.NOTES:
            src.error(line, '%s is out of range (%s)' % (
                toks[0], 'C-1 to B-8 on a PSG channel' if is_psg(c)
                else 'C-0 to B-7'))
        cell.update(note=v, gate=None, volume=None)
    i = 1
    if i < len(toks) and toks[i].startswith('@'):
        m = re.fullmatch(r'@([0-9]+)', toks[i])
        n = int(m.group(1)) if m else 0
        if not 0 < n < row_ticks:
            src.error(line, '%s: a cell is 1 to %d ticks after its row (@N)'
                      % (toks[i], row_ticks - 1))
        cell['tick'] += n
        i += 1
    while i < len(toks):
        t = toks[i]
        is_volume = re.fullmatch(r'v[0-9]+', t)
        if t in ('hold', 'gate') or is_volume:
            what = 'volume' if is_volume else 'gate'
            if 'note' not in cell:
                src.error(line, '%s: an end cell has no note' % t)
            if cell[what] is not None:
                src.error(line, '%s: the %s given twice' % (t, what))
            if t == 'hold':
                cell['gate'] = 'hold'
            elif t == 'gate':
                if i + 1 >= len(toks):
                    src.error(line, 'gate needs a value')
                i += 1
                cell['gate'] = h.number(src, line, toks[i], 0, NOTE_MAX)
            else:
                cell['volume'] = h.number(src, line, t[1:], 0, NOTE_MAX)
            i += 1
            continue
        it, i = take_change(src, line, toks, i, c, 'note' not in cell)
        if it == ('loop',) and it in cell['changes']:
            src.error(line, 'loop given twice')
        cell['changes'].append(it)
    return cell


def bars(toks):
    """A line's words with each | a word of its own."""
    out = []
    for t in toks:
        out += [x for x in re.split(r'(\|)', t) if x]
    return out


class Reader:
    """A tracker file's lines, read in order."""

    def __init__(self, name, text):
        self.src = h.Source(name, text)
        self.src.lines = [(n, bars(toks)) for n, toks in self.src.lines]
        self.song = {'transpose': 0}
        self.lines = {}             # channel: (line, its commands)
        self.columns = None         # the channels with a column
        self.sections = []          # {start, ticks, row_ticks, line}
        self.heads = None           # this section's columns: (channel, copy)
        self.row_at = -1
        self.by_section = {}        # (section, channel): its cells
        self.volume = {}            # channel: the volume the last note had
        self.fm, self.psg = {}, {}

    def error(self, line, msg):
        self.src.error(line, msg)

    def song_line(self, line, toks):
        if 'volume' in self.song:
            self.error(line, 'song given twice')
        vals = {}
        i = 1
        while i < len(toks):
            if toks[i] == 'transpose':
                self.error(line, 'transpose: the notes are written as heard, '
                           'so there is no transpose')
            if toks[i] not in ('volume', 'tempo') or i + 1 >= len(toks):
                self.error(line, '%r: song takes volume and tempo' % toks[i])
            vals[toks[i]] = h.number(self.src, line, toks[i + 1], 0, 255)
            i += 2
        for k in ('volume', 'tempo'):
            if k not in vals:
                self.error(line, 'song: %s missing' % k)
        self.song.update(vals)

    def channel_line(self, line, toks):
        if self.sections:
            self.error(line, 'channel lines go before the sections')
        if len(toks) < 2 or toks[1] not in h.CHANNELS[:h.TRACKS]:
            self.error(line, 'channel takes fm1-fm6, psg1 or psg2')
        c = h.CHANNELS.index(toks[1])
        if c in self.lines:
            self.error(line, 'channel %s given twice' % toks[1])
        items, i = [], 2
        while i < len(toks):
            it, i = take_change(self.src, line, toks, i, c)
            if it == ('loop',) and it in items:
                self.error(line, 'loop given twice')
            items.append(it)
        self.lines[c] = (line, items)

    def section_line(self, line, toks):
        self.close_section()
        if (len(toks) != 7 or toks[3] not in ('rows', 'row')
                or toks[4] != 'x' or toks[6] not in ('ticks', 'tick')):
            self.error(line, 'section takes NN  ROWS rows x TICKS ticks')
        k = h.number(self.src, line, toks[1], 0, 9999)
        if k != len(self.sections):
            self.error(line, 'section %s: the next is %02d (they run from 00 '
                       'in order)' % (toks[1], len(self.sections)))
        rows = h.number(self.src, line, toks[2], 1, 9999)
        row_ticks = h.number(self.src, line, toks[5], 1, 9999)
        last = self.sections[-1] if self.sections else None
        self.sections.append({
            'start': last['start'] + last['ticks'] if last else 1,
            'ticks': rows * row_ticks, 'row_ticks': row_ticks,
            'line': line})
        self.heads = None
        self.row_at = -1

    def heading(self, line, parts):
        k = len(self.sections) - 1
        if self.heads is not None:
            self.error(line, 'section %02d has two heading rows' % k)
        self.heads = []
        for p in parts[1:]:
            m = re.fullmatch(r'\s*(\w+)\s*(?:=\s*([0-9]+)\s*)?', p)
            if not m or m.group(1) not in h.CHANNELS[:h.TRACKS]:
                self.error(line, '%r: a column is a channel (fm1-fm6, psg1, '
                           'psg2), and = NN if it plays section NN\'s again'
                           % p.strip())
            c = h.CHANNELS.index(m.group(1))
            copy = None
            if m.group(2) is not None:
                copy = h.number(self.src, line, m.group(2), 0, 9999)
                if copy >= k:
                    self.error(line, '%s = %s: an earlier section'
                               % (m.group(1), m.group(2)))
                if self.sections[copy]['ticks'] != self.sections[k]['ticks']:
                    self.error(line, '%s = %02d: section %02d is %d ticks, '
                               'this one %d' % (m.group(1), copy, copy,
                                                self.sections[copy]['ticks'],
                                                self.sections[k]['ticks']))
            self.heads.append((c, copy))
        names = [c for c, _ in self.heads]
        if len(set(names)) != len(names):
            self.error(line, 'a channel twice in the heading')
        if self.columns is None:
            self.columns = names
        elif names != self.columns:
            self.error(line, 'the columns are %s, as in section 00'
                       % ', '.join(h.CHANNELS[c] for c in self.columns))
        shift = self.sections[k]['start']
        for c, copy in self.heads:
            cells = []
            if copy is not None:
                at = self.sections[copy]['start']
                cells = [dict(x, tick=x['tick'] - at + shift, line=line)
                         for x in self.by_section.get((copy, c), [])]
            self.by_section[(k, c)] = cells

    def row(self, line, parts):
        k = len(self.sections) - 1
        sec = self.sections[k]
        m = re.fullmatch(r'\s*([0-9]+)\s*', parts[0])
        if not m:
            self.error(line, '%r: a row starts with its number'
                       % parts[0].strip())
        n = int(m.group(1))
        if n <= self.row_at or n * sec['row_ticks'] >= sec['ticks']:
            self.error(line, 'row %s: the rows run from 00 to %02d in order'
                       % (m.group(1), sec['ticks'] // sec['row_ticks'] - 1))
        self.row_at = n
        if len(parts) - 1 != len(self.heads):
            self.error(line, '%d cells for %d columns'
                       % (len(parts) - 1, len(self.heads)))
        for (c, copy), text in zip(self.heads, parts[1:]):
            cell = parse_cell(self.src, line, c, text, sec['row_ticks'],
                              sec['start'] + n * sec['row_ticks'])
            if cell is None:
                continue
            if copy is not None:
                self.error(line, '%s = %02d: its cells are section %02d\'s; '
                           'leave them blank' % (h.CHANNELS[c], copy, copy))
            self.by_section[(k, c)].append(cell)

    def close_section(self):
        """A note's volume not written is the last note's in its column
        (the driver keeps it); a copy's are its section's."""
        k = len(self.sections) - 1
        if k < 0:
            return
        if self.heads is None:
            self.error(self.sections[k]['line'], 'section %02d has no '
                       'heading row (row | fm1 | ...)' % k)
        for c, _ in self.heads:
            for x in self.by_section[(k, c)]:
                if 'note' in x:
                    if x['volume'] is None:
                        x['volume'] = self.volume.get(c, 0)
                    self.volume[c] = x['volume']

    def read(self):
        src = self.src
        h.scan_names(src)
        while src.pos < len(src.lines):
            line, toks = src.lines[src.pos]
            src.pos += 1
            w = toks[0]
            if w == 'song':
                self.song_line(line, toks)
            elif w == 'volumes':
                if len(toks) != 10:
                    self.error(line, 'volumes takes nine (fm1-fm6, psg1, '
                               'psg2 and one nothing reads)')
                self.song['volumes'] = [h.number(src, line, t, 0, 255)
                                        for t in toks[1:]]
            elif w == 'channel':
                self.channel_line(line, toks)
            elif w == 'section':
                self.section_line(line, toks)
            elif w == 'row' or w[0] in '0123456789':
                if not self.sections:
                    self.error(line, 'a row before any section')
                parts = [[]]
                for t in toks:
                    if t == '|':
                        parts.append([])
                    else:
                        parts[-1].append(t)
                parts = [' '.join(p) for p in parts]
                if (self.heads is not None and len(parts) == len(self.heads) + 2
                        and not parts[-1]):
                    parts.pop()             # a | after the last column
                if parts[0] == 'row':
                    self.heading(line, parts)
                elif self.heads is None:
                    self.error(line, 'a row before the section\'s heading '
                               '(row | fm1 | ...)')
                else:
                    self.row(line, parts)
            elif w in ('fm_instrument', 'psg_instrument'):
                fields = []
                while (src.pos < len(src.lines)
                       and src.lines[src.pos][1][0] not in BLOCK_WORDS
                       and src.lines[src.pos][1][0][0] not in '0123456789'):
                    fields.append(src.lines[src.pos])
                    src.pos += 1
                kind = w.split('_')[0]
                table = self.fm if kind == 'fm' else self.psg
                if len(toks) not in (2, 3):
                    self.error(line, '%s takes its number (and a name)' % w)
                n = h.number(src, line, toks[1], 0, 255)
                if n in table:
                    self.error(line, '%s %d given twice' % (w, n))
                table[n] = h.parse_instrument(src, line, kind, fields)
            else:
                self.error(line, '%r: not song, volumes, channel, section, a '
                           'row, fm_instrument or psg_instrument' % w)
        self.close_section()
        if 'volume' not in self.song:
            self.error(0, 'no song line')
        if 'volumes' not in self.song:
            self.error(0, 'no volumes line')
        cells = {}
        for k in range(len(self.sections)):
            for c in self.columns or []:
                cells.setdefault(c, []).extend(self.by_section[(k, c)])
        song = self.song
        song['fm'] = h.instrument_list(src, self.fm, 'fm')
        song['psg'] = h.instrument_list(src, self.psg, 'psg')
        song['fm_names'], song['psg_names'] = h.names_lists(src, song)
        song['tracks'], song['phrases'] = build_channels(
            src, self.lines, cells, self.sections)
        song['phrase_count'] = len(song['phrases'])
        return song


def read_tracker(name, text):
    """A tracker file into the song model hzsongtext.song_bytes takes."""
    return Reader(name, text).read()


# -- the cells into order lists and phrases ------------------------------

def note_items(cell, length, prev):
    """A cell's note: past 127 ticks in pieces, the first struck and the
    rest held on at its pitch (silent once it is let go); each with the
    numbers that differ from the note before's (length, gate, volume: the
    driver keeps the last), all three at a phrase's start (prev [None])."""
    gate = cell['gate']
    if gate is None:
        gate = default_gate(length)
    elif gate == 'hold':
        gate = length
    out = []
    at = 0
    while at < length:
        n = min(NOTE_MAX, length - at)
        g = n if gate >= at + n else max(gate - at, 0)
        params = [n, g, cell['volume']]
        if prev[0] is None:
            write = params
        else:
            differ = [k for k in range(3) if params[k] != prev[0][k]]
            write = params[:differ[-1] + 1] if differ else []
        out.append(('note', cell['note'], write))
        prev[0] = params
        at += n
    return out


def section_of(sections, tick):
    for k, sec in enumerate(sections):
        if sec['start'] <= tick < sec['start'] + sec['ticks']:
            return k
    return len(sections)


def build_channels(src, lines, cells, sections):
    """Every channel's order list, and the phrases: a section's cells a
    phrase (one starting again at a `loop` cell, whose commands go in the
    order list round its loop point), the same bytes the same phrase; past
    224 phrases, two sections a phrase, then four...."""
    heads, plan = [], []
    for c in range(h.TRACKS):
        mine = cells.get(c, [])
        line = lines.get(c, (0, []))[1]
        loops = [x for x in mine if ('loop',) in x['changes']]
        name = h.CHANNELS[c]
        if loops and ('loop',) in line:
            src.error(loops[0]['line'], 'loop: channel %s has one in its '
                      'channel line' % name)
        if len(loops) > 1:
            src.error(loops[1]['line'], 'loop: channel %s has one already'
                      % name)
        ends = [k for k, x in enumerate(mine) if 'note' not in x]
        if mine and not ends:
            src.error(mine[-1]['line'], 'channel %s has no end cell' % name)
        if ends and ends[0] != len(mine) - 1:
            src.error(mine[ends[0] + 1]['line'], 'a cell after channel %s\'s '
                      'end' % name)
        if mine and ends[0] == 0:
            src.error(mine[0]['line'], 'channel %s ends before its first note'
                      % name)
        if mine and ('loop',) in mine[-1]['changes']:
            src.error(mine[-1]['line'], 'loop in the end cell: nothing after '
                      'it to come back to')
        delay = mine[0]['tick'] - 1 if mine else 0
        if delay > 255:
            src.error(mine[0]['line'], 'channel %s starts %d ticks in: 255 at '
                      'most' % (name, delay))
        # The delay: a wait first. With no loop point the channel starts
        # again from there, as the song text's does.
        head = [('flag', WAIT, delay)] if delay else []
        heads.append(head + [('byte', h.LOOP) if it == ('loop',) else it
                             for it in line])
        plan.append(mine)
    for every in (1, 2, 4, 8, 16, 32, 64, 128, 1 << 20):
        phrases, numbers, orders = {}, {}, []
        for c in range(h.TRACKS):
            order = list(heads[c])
            mine = plan[c]
            groups = []
            for k, x in enumerate(mine[:-1]):
                sec = section_of(sections, x['tick']) // every
                if (not groups or ('loop',) in x['changes']
                        or sec != groups[-1][0]):
                    groups.append((sec, []))
                groups[-1][1].append((x, mine[k + 1]['tick'] - x['tick']))
            for _, group in groups:
                items = []
                prev = [None]
                for x, length in group:
                    changes = x['changes']
                    if ('loop',) in changes:
                        k = changes.index(('loop',))
                        order += (changes[:k] + [('byte', h.LOOP)]
                                  + changes[k + 1:])
                        changes = []
                    items += changes + note_items(x, length, prev)
                key = bytes(h.items_bytes(items))
                if key not in numbers:
                    numbers[key] = len(numbers)
                    phrases[numbers[key]] = items
                order.append(('phrase', numbers[key]))
            if mine:
                order += mine[-1]['changes']
            orders.append(order)
        if len(phrases) <= h.NOTE_END:
            return orders, phrases
    src.error(0, 'more than %d phrases' % h.NOTE_END)


# -- the check -----------------------------------------------------------

def played(song, t, ticks, repeat):
    """What a channel plays, to compare: its notes (a gate past the length
    taken as the length: held on, the same to the driver) and commands, but
    the transposes (in the notes), the start's wait (in when the notes
    come; it writes nothing, so it may go anywhere among the commands
    there) and the loop points (in where it goes)."""
    w = Walk(song, t, repeat)
    for k in range(1, ticks + 1):
        w.step(k)
        if not w.on:
            break
    out = []
    for e in w.events:
        if e[1] == 'mark' or (e[1] == 'cmd'
                              and e[2][1] in (TRANSPOSE, WAIT)):
            continue
        if e[1] == 'note':
            tick, kind, note, length, gate, volume, held = e
            e = (tick, kind, note, length, min(gate, length), volume, held)
        elif e[1] == 'end':
            e = e[:2]
        out.append(e)
    return out


def check(song, tracked, name):
    """Raises if the tracker's song doesn't play as the text's: every
    channel over the song and two more turns of it, repeated (as the game
    repeats a song; one it plays once plays the first turn of that)."""
    span = max(first_pass(song, t)[-1][0] for t in range(h.TRACKS))
    for t in range(h.TRACKS):
        a = played(song, t, 3 * span, True)
        b = played(tracked, t, 3 * span, True)
        if a != b:
            k = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]),
                     min(len(a), len(b)))
            raise ValueError('%s: channel %s plays otherwise from tick %d: '
                             'the text %s, the tracker %s'
                             % (name, h.CHANNELS[t],
                                (a[k] if k < len(a) else b[k])[0],
                                a[k] if k < len(a) else 'nothing',
                                b[k] if k < len(b) else 'nothing'))


# -- the commands --------------------------------------------------------

def tracker_name(name):
    """A song file's tracker: its name with .trk for .txt (None: not a
    .txt)."""
    return name[:-4] + '.trk' if name.endswith('.txt') else None


def song_starts(model):
    """Where each song starts in the one window, as hzsongtext.build lays
    them out."""
    at = h.BASE + 2 * model['song_table']
    out = []
    for song in model['songs']:
        b = h.song_bytes(song)
        out.append(at)
        at += len(b) - (1 if b[-1] == h.SONG_FIRST else 0)
    return out


def song_title(text):
    """The title the song text's first line gives."""
    m = re.match(r'# (.*) \(song [0-9]+', text)
    return m.group(1) if m else None


def export(folder, which=None):
    """Each song of folder's text (or those numbered in which) written as
    a tracker beside its file, once checked. Returns the files written."""
    def read(name):
        with open(os.path.join(folder, name), encoding='utf-8-sig') as fh:
            return fh.read()
    model = h.read_text(read)
    image, windows = h.build(model)
    starts = song_starts(model)
    written = []
    for n, song in enumerate(model['songs']):
        if which and n not in which:
            continue
        name = model['song_files'][n]
        out = tracker_name(name)
        if out is None:
            raise ValueError('%s: not a .txt' % name)
        # What a song reads past its end: its window's bytes there.
        img = image if windows is None else windows[n]
        start = starts[n] if windows is None else h.BASE + 2 * model[
            'song_table']
        fm, psg = instruments_read(song, img, start)
        text = song_tracker_text(n, song, song_title(read(name)), fm, psg)
        tracked = read_tracker(out, text)
        check(song, tracked, out)
        with open(os.path.join(folder, out), 'w', encoding='utf-8',
                  newline='\n') as fh:
            fh.write(text)
        written.append(out)
    return written


def main():
    args = sys.argv[1:]
    try:
        if args[:1] == ['export'] and len(args) >= 2:
            files = export(args[1], {int(a) for a in args[2:]})
            print('%s: %d trackers written, each playing as its text'
                  % (args[1], len(files)))
        elif args[:1] == ['text'] and len(args) == 2:
            with open(args[1], encoding='utf-8-sig') as fh:
                song = read_tracker(args[1], fh.read())
            text = h.song_text(0, song, None).split('\n', 1)[1]
            sys.stdout.write('# %s as song text.\n%s' % (args[1], text))
        else:
            sys.exit(__doc__.split('\n\n')[1])
    except (h.TextError, ValueError) as e:
        sys.exit(str(e))


if __name__ == '__main__':
    sys.modules['hztracker'] = sys.modules[__name__]   # one module, imported or run
    main()
