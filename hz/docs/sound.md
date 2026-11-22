# The song text (session 49)

Every song and sound effect is text under `assets/sound/`, which the game
builds into the sound driver's data at start-up: the 32 KB the Z80
driver's bank window shows at $8000-$FFFF (`SoundData`, ROM $78000;
`src/audio/songtext.c`), or a window per song once the songs outgrow it
(Room, below). Every song is also laid out as a tracker beside its text,
which the game reads in the text's place (The tracker, below). Edit a
song's `.trk` (or the effects' text), start the game, and it plays what
the file says. Or keep the game's files and put the edited copy in `mods/sound/`
(`docs/mods.md`): a copy the reader refuses is set aside, and the game
plays its own. One of the game's own files that the reader refuses stops
the game at start-up with the file, the line and why, as the other loaders
do:

    songtext: assets/sound/songs/00_jagd_flieger.txt:31: 'X5': not a note or a command

`make assets` writes the text from the ROM (`tools/hz_extract.py` calls
`tools/hzsongtext.py`), and only after building it back into the ROM's
bytes and finding them the same; `make roundtrip` proves the game's reader
against the ROM. `python3 tools/hzsongtext.py build assets/sound OUT.bin`
builds the bytes as the game does (the two readers give the same bytes
and the same refusals), and `check DIR BIN` compares. The driver itself
is the Z80 source at `Z80Driver` in `asm/hz.asm` (ported as
`src/audio/hz_snd.c`); its reading of
everything here is in HANDOVER.md, Sound.

## The files

- `sounds.txt`: the layout. `song_table 128`: the table at $8000 (two
  bytes an entry: each song's address, where the next one would go, then
  zeros). `song N FILE`: the songs, from 0 in order; the game plays them by
  number (below). `effects $FA00 effects.txt`: where the effects' table
  goes and its file; the game hands the driver this address
  (`Snd_SetSfxTable`). `tail`: hex bytes after the effects that nothing
  reads (left over from the tool that made the data).
- `songs/NN_title.txt`: a song, named by its title on the options' MUSIC
  panel.
- `songs/NN_title.trk`: the same song as a tracker, which the game reads
  in the text's place (The tracker, below).
- `effects.txt`: the 29 sound effects.
- `q_drums.json`: not the song text: the port's own drum map for the Q
  synth (docs/q.md), by song, instrument (by name since session 50, so it
  follows an instrument that moves in its song's list) and order-list
  position, so an edit that moves a song's order lists can move what it
  finds.

| Song | Title | Where |
|---|---|---|
| 0 | Jagd flieger | the title |
| 1 | The previous war | the options |
| 2 | The fighter called EMERGENCY to base | the map select |
| 3-10 | There is no time to lose ... The super fighter invigorated us | the maps' songs: ABGRUND, VULKAN, LOCH, STRAND, STADT, EISFREI, WALDUNG, OASE |
| 11-14 | The dawn of a new age, Funeral march for the METAL KNIGHTS, A Victorious bell is ringing BRASS / BELL | the winner screen (`game_end.c` picks one by the sides and who won) |
| 15, 16 | Be his soul rest in peace, A full retreat | the endings |
| 17 | Go with the stream | the staff roll |

## A song

```
song  volume 80  tempo 120  transpose 0
volumes  98 98 98 99 99 99 96 96 90

track fm1
    slide all  volume +2
    loop
    0 0 12 17 27 17 27 36 37 36 58  instrument chords_2  47 55
...
phrase 0
    instrument chords  F#4 24 22 96  slide_speed 84  G#4  C#5 12 10  B4  A4  G#4
    A4 48 46  E4
...
fm_instrument 0 mid
...
```

- `song`: the song's volume (0-99, combined with every note's), its
  tempo and a transpose added to every note. A tick, the unit of every
  length, lasts 16 x floor(8324 / (tempo + 50)) samples of the chip's
  53,267 a second: tempo 85 is about 55 ticks a second, 120 about 69. The
  shipping songs count 24 ticks a beat, 96 a bar. `phrases N` follows when
  the phrase table is longer than the last phrase (unused numbers are 0).
- `volumes`: each track's volume, fm1-fm6, psg1, psg2, and a ninth nothing
  reads.
- `track fm1` ... `track psg2`: the eight tracks, each an order list: the
  phrases it plays in turn by number, with commands between them. `loop`
  marks where the track starts again when it reaches its end, if the song
  repeats (the game's choice, not the song's: the maps' songs repeat, the
  title's doesn't). `~` and `-` are bars as the composer's tool marked them,
  `~` the phrase before going on, `-` an empty bar; the driver skips both,
  so they don't make a track wait (`wait N` does). A track with nothing in
  it is silent.
- `phrase N` (0-223): notes and commands. `repeat 4` ... `end_repeat` plays
  what is between four times (three deep at most).
- `fm_instrument N NAME`, `psg_instrument N NAME`: the song's instruments,
  numbered from 0 with none left out, each with a name (session 50,
  `docs/instrument_names.md`). `instrument NAME` picks one; its number
  does too (`instrument 4`). An FM track plays the FM list and a PSG track
  the PSG list, so a name from the other list is refused, in a phrase too:
  it is checked against every track that plays it.

A note is its name, `C0` to `B7` (sharps `C#4`, or flats `Db4`), then up
to three numbers:

    C4 24 22 96      length 24 ticks, held 22, volume 96
    C4 24 22         length and how long it is held
    C4 24            the length alone
    C4               the last numbers again

The length is the ticks to the next note; the gate how long the key stays
down (0: silent; as long as the length or longer: held into the next note,
which then glides in without being struck again); the volume 0-99, the
note's, combined with the song's, the track's and `volume`'s. A number left
out keeps its last value, from the notes before, even in an earlier
phrase. Names are pitches on an FM track (`C4` is middle C) before any
transpose; a PSG tone plays an octave higher, and nothing under C3 (notes
up to B2 all play in C3's octave). Notes on an effect's `noise` track set
the third tone's pitch, which a noise mode can follow.

Every line in the files is one bar or eight notes, and repeats are
indented, but the readers take the words in any layout: a line break is
a space, in a track's order list, a phrase and an instrument's fields
(`unused` takes the words to its line's end). (Until session 50 the
Python reader read those a line at a time: a note's numbers on the next
line, which the game plays, were refused by the tools.) The lines that
start a block (`song`, `track`, `phrase`, an instrument's) are lines of
their own. `#` starts a comment, to the line's end, when it begins a word
(in `C#4` it is a sharp).

## Heard notes

A `# heard:` comment ends each line where a note sounds other than it is
written, and gives the line's notes as they sound:

    D3 24 22 96  A3  D3  G3    # heard: D4  A4  D4  G4

What moves a note:

- **A transpose:** the song's, and the track's `transpose`, in its order
  list or a phrase. The track's adds up, and a repeating song starts each
  pass where the last one left it. The driver keeps notes within C0-B7, so
  a note moved past B7 comes round from C0: an effect's `D#7` under
  `transpose +24` sounds as D#1.
- **A detune** of half a semitone or more (`detune +16` and up): the note
  is named by the semitone it is nearer.
- **A PSG track:** a tone sounds an octave up, and nothing goes under C3
  (B2 and below sound in C3's octave).

A note reached at more than one pitch gives each, `A4|A5`: a phrase played
at two transposes, or a repeat that transposes as it goes.

A song with `loop` in any track is followed through its repeat until it
comes round as it began. The game repeats exactly those songs (the maps',
the options', the map select's and the staff roll's). The rest are
followed once.

Effects are taken at the song's transpose 0, as every song has it. Their
noise tracks are left out: those notes set the third tone's pitch, heard,
if at all, through the noise.

The readers skip the comments. After an edit, `python3 tools/hzsongtext.py
annotate assets/sound` writes them again, for a folder's songs and effects
or for the files named (`annotate mods/sound/songs/03_...txt`). `make
assets` writes them too.

Checked against the driver: every track of every song and effect was
played through `hz_snd.c`, the repeating songs for 25 minutes each, and
each track's target pitch was polled. Each track reached the pitches its
comments give and no others (C0 aside, which the check couldn't tell from
a track that hasn't played).

## The tracker

Every song is also laid out as a tracker, FastTracker II's way (as the PS2
port's `tools/tracker.py`): `songs/NN_title.trk` beside its text, every
channel side by side in rows of ticks, the notes as heard, so the parts
can be read against each other and edited by position.

    song  volume 80  tempo 120
    volumes  98 98 98 99 99 99 96 96 90
    channel fm1  slide all  volume +2  loop
    channel fm5  release_bend down  slide_speed 10  volume +2  loop
    ...

    section 00  16 rows x 6 ticks    # ticks 1-96
    row | fm1                       | fm2                       | ... | fm5                                 | ...
     00 | F#4 v96 instrument chords | D-4 v96 instrument chords | ... | D-3 gate 1 v96 instrument kick_toms | ...
     01 | ...                       | ...                       | ... | ...                                 | ...
     02 | ...                       | ...                       | ... | D-3 gate 1                          | ...
    ...
    section 05  16 rows x 6 ticks    # ticks 481-576
    row | fm1 = 01 | fm2 = 01 | fm3 = 01 | fm4 = 01 | fm5 = 00 | ...

The game reads a song's `.trk` in place of its `.txt`, which is then the
fallback. With mods (`docs/mods.md`) it reads the first there is of: a
mod's `.trk`, a mod's `.txt`, the game's `.trk`, the game's `.txt`. So an
edit of the text plays only as a mod, or once the song's `.trk` is gone.
`make assets` writes the trackers from the text, each only once it plays
as the text; `python3 tools/hztracker.py export assets/sound` writes them
again after the text has changed, and `hztracker.py text FILE.trk` prints
the song text a tracker reads as.

- `song`: the volume and the tempo, as the text's (no transpose: the
  notes are as heard). The comment at the top gives the ticks a second and
  whether the game repeats the song.
- `channel` lines: what a channel does as the song starts, before its
  first cell (the text's order list up to its first phrase), in the text's
  words; `loop` among them if that is where it comes back to.
- `section NN  ROWS rows x TICKS ticks`: the song cut into spans, each
  starting where the last ended (the `# ticks` comment is the exporter's).
  A song's rows are 6 ticks, a sixteenth note at 24 ticks a beat, or 8 (a
  triplet's) where most of its notes fall so; finer in a section that
  needs it.
- The heading row names the columns, the channels with notes, the same in
  every section. `fm1 = 03` heads a column that plays section 03's cells
  again: its cells are left blank (the two sections as long), so a part
  repeated is written once and edited once.
- A cell: the note as heard (`C-4`, `C#4`; `C4` and flats are read too),
  after the song's and the track's transposes. A PSG column names a tone
  as it sounds, an octave above the text's (`C-1` to `B-8`); names under
  `C-3` sound in C3's octave too. Then `@N` if it falls N ticks after its
  row. The note lasts to the next cell in its column. Then, in any order:
  - its gate: none written, the note is let go 2 ticks before the next
    cell (1 tick in, at least), as most of the songs' are; `hold`, held
    into the next cell, which glides in without being struck; `gate N`,
    let go N ticks in (`gate 0`: never keyed, silent at that pitch);
  - its volume, `vN` (0-99), where it changes: a cell without one has the
    last note's in its column, as the driver keeps it;
  - the commands before it, in the text's words and in the order the
    driver reads them (Commands, below), but `transpose` (in the notes),
    `wait` (a channel starts at its first cell) and `repeat` (written out).
    `loop` among them marks where the channel comes back to; the commands
    before it run only the first time.
- `...` or nothing: no new note.
- `end`, a cell of its own (with `@N` and commands): where the channel's
  order list ends, its last note's length over. In a song the game repeats
  the channel goes back from there to its `loop`, or with none to its
  start (its first cell's delay too); in one it plays once it stops. A
  `transpose` in an end cell is how far the next turn plays from this one:
  songs 11, 13, 14 and 15 have channels whose transposes add up from turn
  to turn (the game plays them once; `make sound` repeats every song).
- Then the song's instruments, as the text's, with their names. The title
  and Back to square one have no PSG instrument and read instrument 0
  from the bytes after them (Room): their trackers have those bytes as
  their own, with no name.

The reader (`src/audio/songtext.c`; `tools/hztracker.py` builds the same
bytes) makes each channel's order list (a `wait` when its first cell is
late, its channel line, the `loop` point) and a phrase of each section's
cells, the same bytes the same phrase, copies among them. A note over 127
ticks becomes pieces held on at its pitch; past 224 phrases a phrase takes
two sections, then four. Written out the songs come to 42 KB, so each has
a window of its own (Room). It refuses as the text's reader does, by file
and line:

    songtext: assets/sound/songs/03_there_is_no_time_to_lose.trk:17: F#9 is out of range (C-0 to B-7)

Checked:

- `make roundtrip`: the text alone still builds the ROM's 32,768 bytes,
  and every song as the game reads it, from its tracker, makes the text's
  chip writes, write for write: 200 s each, repeating, an effect every 97
  frames over it (2.2 million writes).
- `hztracker.py export`: each tracker against its text through the
  driver's sequencing (`MusicTrack_Tick`: the notes, lengths, gates,
  volumes and commands, tick for tick), over three turns.
- The C and the Python readers build the same bytes from the trackers and
  refuse alike: 61 broken and odd trackers, the same message, file and
  line, or the same bytes. (Session 50) Again with the names, and 59 cases
  more: names in a tracker, a song's text and the effects (a name not
  given, from the other list, twice, malformed; renamed, or numbers in
  their place), the layout (a note's numbers, a command's value or an
  instrument's fields on the next line) and an instrument's `unused`
  words. All 120 agree.
- `make mods_check`: a tracker's edit plays as the same edit in the text;
  a broken tracker mod is set aside; a tracker mod is read over a text mod.
- `make sound` plays the text (`hz_sound --text`), the ROM's bytes, which
  the original driver reads.

## Commands

In order lists, phrases, and an effect's track line:

| Text | Byte | What |
|---|---|---|
| `instrument NAME`, `instrument N` | $E0 | the track's instrument from now on, by its name or number |
| `pan left`, `right`, `center` | $E1 | the speakers (FM) |
| `noise_mode N` | $E2 | the noise channel's mode (0-15) |
| `slide_speed N` | $E3 | how fast a slide moves; 0 no slide |
| `slide all`, `slide tied` | $F4, $F5 | slide into every note, or only into a held one |
| `release_bend up`, `down` | $F6, $F7 | the pitch bends from where the key goes up |
| `lfo on`, `off` | $F1, $F0 | the chip's LFO from this FM track's instrument (music only) |
| `lfo_delay N` | $E4 | ticks before it starts |
| `wait N` | $E5 | the track waits N + 1 ticks before going on (before a note it does nothing: the note's length replaces it). `wait 0`: every music track still waiting skips to its phrase's end |
| `transpose +N`, `-N`, `0` | $E8 | added to the track's notes (in semitones); 0 resets |
| `volume +N`, `-N`, `0` | $E9 | added to the track's volume; 0 resets |
| `tempo +N`, `-N`, `0` | $EA | added to the song's tempo; 0 resets |
| `detune N` | $EB | the track's pitch, in 1/32 semitones (signed) |
| `fade_rate N` | $ED | after it, `volume` also starts a fade that steps every N ticks (`Track_VolumeFade`) |
| `psg_volume tone`, `noise` | $F2, $F3 | a PSG track's volume to its tone, or to the noise |
| `marker N`, `marker +N` | $E6, $E7 | a byte nothing reads |
| `repeat N`, `end_repeat` | $EC, $FD | phrases only |
| `loop`, `~`, `-` | $FD, $FC, $FB | order lists only |
| `byte N` | N | a byte as it is (the effects' headers have two $FE the driver skips) |

## Instruments

```
fm_instrument 0 mid
    algorithm 0  feedback 7  fms 7  ams 0  lfo_freq 3  lfo_sweep 252
    #           op1  op2  op3  op4
    multiple      2    3    1    2
    detune        5    3    2    0
    level        25   16   23    0
    key_scale     0    0    0    0
    attack       24   28   28   25
    am            0    0    0    0
    decay1        3    0    0   14
    decay2        0    4    4    2
    sustain       2    0    0    1
    release       2    4    4    8
    unused 0:$C0 28:$80 32:$80 34:$02
```

The YM2612's own fields, the operators in the chip's numbering 1-4.
`level` is the operator's attenuation (0 loudest, 127 silent): the
volumes are added to the carriers' (the algorithm's output operators).
`lfo_freq` and `lfo_sweep` reach the chip only through `lfo on`.

```
psg_instrument 0 psg_mid
    speed 91  start_drop 20  attack 99  decay 10  sustain_drop 3  sustain 1  release 99
    fms 0  ams 0  lfo_freq 0  lfo_sweep 0
    unused 15:$09
```

A PSG volume envelope, stepped 77 times a second: a step every 100 -
`speed` of them. At a note the level starts `start_drop` under the
note's volume and rises `attack` a step to it, falls `decay` a step to
`sustain_drop` under it, then `sustain` a step while the key is down; when
it goes up, `release` a step to silence. The rest is read and stored, as
an FM instrument's LFO bytes, and goes nowhere on a PSG track.

`unused OFFSET:VALUE` holds the bits of the record no field holds (the
driver never reads them, or the chip ignores them), so the ROM's bytes
build back; leave it out of a new instrument.

The name after the number (session 50) is what `instrument` calls it:
one word, a letter and then letters, digits or `_`, 31 at most, no two the
same in a file. It may be left out (then only the number picks it). Why
each one is called what it is, and how to rename one:
`docs/instrument_names.md`.

## Effects

```
effects  first_byte 3

effect 4 mech_destroyed
    track fm6  fade_rate 3  volume -30  instrument mech_destroyed  mech_destroyed_fm6
    track noise  byte $FE  byte $FE  instrument psg_mech_destroyed  mech_destroyed_noise
...
phrase mech_destroyed_fm6
    psg_volume tone
    repeat 15
        slide tied  A1 2 2 99  G6 1 1 90  slide all  slide_speed 98  G7 3 1 84
    end_repeat
```

An effect's number is the game's (`SFX_*`, `src/game/game.h`); its tracks
each name a channel (`fm1`-`fm6`, `psg1`, `psg2`, `noise`), the commands
read before its phrase, and the phrase, by name. A track takes its channel
from the music for as long as it plays, unless an effect that matters more
has it (`data/sound.json`'s priorities). Effects tick with the frame, 60 a
second, not with the song's tempo. The tracks go in channel order (the
driver matches them against channels 0-8 in turn). The effects' phrases are
stored in the file's order, and the instruments are their own, apart from
the songs'. `first_byte`: the table's first byte, which nothing reads.

## Room

The driver reads songs and effects through one 32 KB window, and the
ROM's fill it: the songs end at $F9C8, the effects start at $FA00. While
the songs fit before the effects they share that window, as in the ROM.
When they grow past it, each song gets a window of its own instead
(`songtext_build`; `hz_snd_set_song_windows`), and the driver turns to a
song's window as it starts the song (`Cmd_PlaySong`). Each window holds:

- the song table, with that song's entry the only one;
- the song, right after the table;
- the effects and `tail` where `sounds.txt` puts them.

A song can then be as long as the room from the table to the effects:
30,976 bytes with `song_table 128` and the effects at $FA00. The ROM's
songs are 494 to 3,056 bytes. `song_table 19` gives 218 bytes more (19
entries are all 18 songs need). Without `tail` the effects can also move
up to $FA89. A longer song is refused:

    sounds.txt: song 3 (songs/03_there_is_no_time_to_lose.txt) ends at $FC97, past the effects at $FA00 (663 bytes over)

A song's window also holds the songs after it, chained in order as in the
shared window, up to the effects. So a song that reads past its own end
reads the same bytes in either layout. Two of the ROM's songs do: the
title and Back to square one have no `psg_instrument`, yet their PSG
tracks play. The driver takes instrument 0 from the 16 bytes after the
song, which are the start of the next song (most of its header), and uses
them as the envelope. Their files say so at the top. Giving such
a song a `psg_instrument 0` changes how its PSG tracks sound. So does
editing the start of the next song.

Checked with song 3 grown by 4 KB (a phrase nothing plays), which gives
every song its own window:

- each of the 18 songs makes the same register writes as in the shared
  window, over 180 s each with ten effects;
- the title and a CPU game on ABGRUND (song 3's map) make the same WAV;
- an edit of the title sounds the same in either layout.

The ROM's text keeps the ROM's layout, so it builds the ROM's bytes, and
`make roundtrip`, `make sound`'s write-for-write check of the driver and
the Q drum map hold. An edited text builds wherever it fits. Nothing
checks that it makes musical sense: the driver plays whatever the bytes
say.

The trackers, written out, come to about 42 KB, so as the game ships every
song has a window of its own. The one difference that makes: before the
game's first song, an effect that ends gives its channel back to a music
track that never started, which the driver sets up from the bytes at the
song table (read as a song, as the original does). In a window of its own
those bytes differ, and so does the end of that effect's release. The game
always plays a song first (the title, the options, a game's loading
screen), so it never comes to that.
