# The Q backend (session 36)

PS2's re-voiced FM (its builds 109-139e; PS2's `docs/audio.md`, "The Q
backend" and "The PM voicing"), in HZ's mix. The Esc screen's AUDIO page
has SYNTH: **NUKED**, the YM2612 as it was (Nuked-OPN2, the default), or
**Q**, which plays the same register writes through Cycfi's Q: four
operators phase-modulating each other in float, with the chip's
algorithms, ratios, detune, feedback and envelopes. The PSG is the same
either way. `settings.json` keeps it as `"synth": "nuked"` or `"q"`.
Under it (session 38) VOICES, Q's, as PS2's row: **EXTRA** (the default)
or **CHIP** (below, "Voices"); `"voices": "extra"` or `"chip"`. Under that
(session 39) REPEATS, Q's: **CHIP** (the default since session 42) or
**FRESH** (PS2's; below, "Repeats"); `"repeats": "chip"` or `"fresh"`.
Under that (session 48) OVERSAMPLE, Q's, PS2's row: **1X** (HZ's default),
2X or 4X (PS2's default; below, "Oversample"); `"oversample": 1`, 2 or 4.

## The build

Q is header-only. The Makefile finds it as PS2's does: `$(HOME)/GIT/Q` (on
Windows the same under the user's folder), or `Q_DIR=`. `Q=auto`, the
default, builds `hz` with Q wherever Q is found and a C++ compiler answers
(`src/audio/q/q_synth.cpp` is C++20), and without it anywhere else: then
SYNTH shows NUKED with Q NOT BUILT under it. `Q=1` insists, `Q=0` leaves it
out. `hz_headless` is the checks' binary and takes Q only for `Q=1`;
`hz_headless_q` is the headless game with Q. Switching Q on or off doesn't
rebuild on its own: `make clean` between.

## What came from PS2, and what HZ added

`src/audio/q/q_synth.cpp` and `q_synth.h` are PS2's files (build 139e),
with their includes pointed at `q_ps2.h`, which holds what they took from
PS2's `synth.h`, `fm.h`, `drums.h` and `smps.h`, as PS2 has them; so a newer
`q_synth.cpp` from PS2 drops in. HZ added one call to the bank,
`q_bank_set_channel_effects`, and `q_hz.c`'s `drums_load` stub.

Where HZ differs from PS2:

- **Hooks by track since session 38.** PS2's sequencer tells the bank
  which track made each write, so Q voices each track on its own (an
  effect on voices of its own beside the music). Until session 38 HZ's
  driver (`src/audio/hz_snd.c`, the Z80 driver's port) had no such hooks,
  so the bank voiced the chip's six channels from the writes alone --
  PS2's mode before its build 111 -- with `core/audio.c` telling it which
  channels an effect holds (`z_sfx_on` b0-5) for the EFFECTS volume. That
  is VOICES CHIP now; EXTRA has the hooks (below, "Voices"). For HZ's six
  FM effect tracks `q_ps2.h`'s `SMPS_SFX_TRACKS` is 6 (PS2's 4): twelve
  virtual channels.
- **No DAC drums.** HZ's driver never plays a sample (its sample commands
  have no 68k caller), so the bank's drums are never struck.
- **No patch file.** PS2's PATCHES (the patch file by instrument number)
  needs the instrument hook; here it is off, and the bank plays every
  instrument as the chip's registers set it.
- **PS2's tuning rows**: VOICES (session 38) and OVERSAMPLE (session 48)
  are on HZ's AUDIO page (HZ's values below); TAILS and GRAIN aren't: the
  bank runs at PS2's defaults, and PS2's environment variables
  (`PS2_Q_VOICE`, `PS2_Q_TAILS`, `PS2_Q_GRAIN`, `PS2_Q_PARALLEL`, ...)
  still set them at start-up (`PS2_Q_OVERSAMPLE` too, for the tools,
  which don't set the row).

## Voices (session 38)

On the chip an effect takes its FM channel from the music: the driver
mutes the music's track there (`Track_IsMuted`: the track runs on, its
writes go nowhere) until the effect ends, then gives it back its
instrument and pitch. Most of HZ's FM effects never play an FM note:
$02, $03, $07 and a dozen more take FM1 and hold it silent for 256 frames
(4.3 s), $0F FM5 and $1A FM4 likewise, keying faint notes; their sound is
the PSG's. So on the chip each of them mutes the music's FM1 (or FM5,
FM4) part for seconds.

- **EXTRA** (the default, PS2's): each track on voices of its own. The
  driver tells the port's track hook (`HzSndTrackWrite`) every FM write a
  track makes, the ones the Z80 skips included, and when an effect takes
  a channel (`SND_PART_CLAIM`); `core/audio.c` queues them in the chip's
  order (`YM_TRACK`: whose the next writes are, `YM_GHOST0/1`: a write the
  chip never gets, `YM_CLAIM`) and hands them to the bank as PS2's
  sequencer does (`q_bank_track_write`, music tracks 0-5, effect tracks
  6-11). The music plays on under the effects. Effect $02 over the
  title's song: the music 0.0 dB from the music alone, where CHIP is 2.1
  dB under (its FM1 part gone); $0F over ABGRUND 0.0 against -1.9.
  Without effects EXTRA is CHIP exactly (+0.00 dB, envelopes r 1.0000,
  six songs, drums and all).
- **CHIP**: the chip's six channels from its writes, as until session 38:
  an effect silences the music's part on its channel, as on the chip.

A switch of VOICES, or to Q, hands the bank each track's registers (kept
from the hook's writes, the chip's and the others) and its held note:
the music muted under an effect comes back at once on EXTRA. A muted
music track's key-on (EXTRA, under an effect) is its own voice's alone:
it doesn't end the effect's note on that channel as one the chip gets
does (`QBank::key_to_chip`). The drums' marker comes with every music
key-on now, muted or not (the note hook), and the drum lookup takes the
music track's own pitch (`HzAudio.music_fnum`), so a drum under an effect
is the drum. A bank reset unhooks it (`hooked`), so VOICES can go back
to CHIP.

## Repeats (session 39)

What a note struck again on a voice still sounding does (PS2's "parallel
voices", its build 111, `q_bank_set_parallel`):

- **FRESH** (PS2's; the default until session 42): the old note goes on as a tail, released
  and faded over 15 ms, and the new one starts on a fresh voice from
  silence, its phases at zero. No click; but a fast repeat of an
  instrument with a slow attack starts each note from nothing where the
  chip's starts from where the last had got to, so it dips (LOCH's bass:
  8-10 dB, about 30% of its frames).
- **CHIP** (the default since session 42): the new note on the same voice,
  its attack from the level the voice has, its phases reset, as the YM2612
  does (Nuked's key-on).

(Session 42) the owner's example of a synth bass low in Q's mix: Sleight of
hand (song 5, LOCH's), its bass instrument 3. Alone against the chip,
bands 30-100 / 100-250 / 250-1k / 1-4k Hz: FRESH +1.1 / +0.3 / -3.1 /
-2.1 dB (the repeats' fresh attacks take its harmonics down, which is
what carries a bass in a mix), CHIP -0.4 / +0.1 / +0.4 / +0.1. With CHIP
everything but the drums matches the chip under 250 Hz (0.0 dB, no frame
6 dB under); the mix's bottom is still 1.9 dB under Nuked's there (27% of
50 ms frames 6+ dB under) for the kick: the kick/tom instrument's energy
matches (-0.1 dB) but PS2's kick is short where the chip's rings on
between hits (median -7.7 dB, 55% of frames 6+ dB under). CHIP made the
default.

The row takes effect at the next strike; nothing is reset. Before it,
`PS2_Q_PARALLEL=0` was the only way to CHIP; the variable still sets the
bank for the tools until something sets REPEATS (the game always does,
from `settings.json`). Measured with `hz_qdrums repeats SONG SECONDS`
(the song through the mixer without the drum table, Q's 20 ms envelope
against the chip's, 30 s): CHIP correlates better in every song --
LOCH's (5) 0.657 -> 0.885, Sniper (9) 0.924 -> 0.987, the title's (0)
0.972 -> 0.994, ABGRUND's (3) 0.882 -> 0.925, STRAND's (6) 0.826 -> 0.888,
the rest by 0.01-0.03; the level is the same either way (Q's -1.3 to -2.0
dB against the chip's, the trim).

## Oversample (session 48)

The chip runs at 53,267 Hz and folds every partial that phase modulation
throws past half of that straight back into the band: on a
feedback-heavy patch a floor of hash between the notes' partials, the
chip's grain. Q's operators run at 1, 2 or 4 times the bank's rate, and
at 2 and 4 their sum is low-passed (flat to 19 kHz) before the rate, so
what folds lands out of hearing (PS2's build 111a). Until session 48 the
bank ran at the output rate (48 kHz) with 4: cleaner than the chip.
The owner, on A breach of contract (6): its parallel voices (instrument
0 in thirds on FM1 and FM2) in Q "don't sit in the mix right", the chip's
"more timbrally complex", and at `PS2_Q_OVERSAMPLE=1` "I hear it much
more" (the measurements in "The drums", session 48). So:

- the bank now runs **at the chip's rate**, a frame each YM sample beside
  Nuked's (left out of the sample, as before), and comes down to the
  output's rate with the PSG through the same resampling; not through the
  DC block (the bank's output has no offset to take; through it, REPEATS'
  check fell from r 0.90 to 0.82, the bank's few Hz moved). So at 1X its
  partials fold where the chip's do: the floor under song 6's thirds
  within 1 dB of the chip's in every band over 500 Hz (`make q_check`);
- **OVERSAMPLE** on the AUDIO page (PS2's row): **1X** the default, 2X
  and 4X cleaner (the floor 3-7 dB under the chip's at 4X). The row takes
  effect at once, the voices sounding go on.

In the game against NUKED (map 1, `make q_check`) the envelopes correlate
0.993 (0.982 before). The chip's own output has some 15% of its energy
under 20 Hz after the DC block (its offsets stepping with the notes), Q's
0.1%: inaudible, but a 20 ms level's share.

## In the mix

Nuked always runs: the driver's timing, its write queue (an address, then
the data, a sample each) and the chip's state never change with SYNTH.
With Q, each data write reaches the bank as it reaches Nuked, and Nuked's
FM is left out of the native sample; the bank renders a frame at a time
beside it (since session 48 at the chip's rate, resampled with the PSG;
until then one output frame at a time), at `MIX_FM_GAIN` times the music or
effects gain (PS2's chip render is on the same scale, and the bank's trim
matches it to that chip), before the master gain and the soft knee. A
switch to Q resets the bank and hands it every register as the chip has
them (`HzAudio.shadow`, all but the key-on), then (session 38) keys on
again each channel the chip is holding a note on (`HzAudio.keyed`), so a
held note or chord sounds on from the switch, from the start of its
envelope; until session 38 it stayed silent until its channel's next note,
up to 16 s in the music test's songs (the title's song switched at 10 s:
4.8 dB under Q from the start for the next 3 s, now within 1 dB). A drum
note held keyed isn't struck again.

## The check

(Session 39) Also: the kicks at the chip's level K-weighted over each hit
(within 1 dB, every kick note of songs 3 and 8), the held-note switch at 5 s
(the title's song is about 10 s long played once, as the title plays it),
REPEATS from the AUDIO page (writes `"repeats": "chip"`, the sound goes on)
and CHIP against FRESH on LOCH's song (`hz_qdrums repeats 5 30`: r 0.885
against 0.657).

`make q_check` (needs Q and numpy; `tools/hzq_check.py`): map 1 from the
start, once with NUKED and once with Q. The 50 ms energy envelopes
correlate at 0.976 (Q plays the chip's notes); the RMS is -20.3 dBFS with
the chip, -21.4 with Q (within 3 dB); with MUSIC off Q is silent until the
first gunshot and the shots sound (the EFFECTS split); a switch to Q from
the AUDIO page at frame 300 writes `"synth": "q"` and the sound goes on.
ASan/UBSan: clean over 1,200 frames with a switch to Q and TURBO held.
Session 37 added the drums (kick notes the kick, the kick's level) and
session 38 the toms at the chip's pitch (Q's tom 30 ms in 0-4 semitones
under the chip's strike, median) and level (within 3 dB over 150 ms), a
switch keeping held notes (`hz_qdrums switch 0 10 3`: within 1.5 dB of Q
from the start) and VOICES (`hz_qdrums voices 0 02 5 4`: under effect $02
CHIP's music more than 1 dB under the music alone, EXTRA's within 0.5).
(Session 50) That test never set the effects' table, so the driver read
its "effect" at its start-up $F000, in song 0's bytes: silent, and it
held a channel only while the songs shared one window (session 49's
trackers gave each its own, and the check failed). Now it sets the table
as `snd_init` does and plays `hz_qdrums voices 17 18 5 4`: the staff
roll's song, its drums on fm6, under hq_destroyed (fm6 and the noise):
CHIP -2.3 dB, EXTRA 0.0.
With EXTRA the default the game's runs correlate 0.973 with the chip's
(0.976 on CHIP: the music under the effects is the difference).

(Session 55) Also: after an effect on FM6, the song's closed hat there
(a drum model) ends the effect's note, as any music strike on its
channel does (`QBank::key`: until session 55 the drum's strike returned
before that, and the effect's released voice took each hat's registers,
a tone at 8.8 kHz held to the next effect): ABGRUND against the CPU, the
8.6-9.0 kHz band after the CPU's ORDERED within 3 dB of the chip's
(`effect_then_hats`; +13.9 dB before, +0.2 after).

## The drums (session 37)

HZ's driver plays no samples: its drums are FM instruments, and one
instrument often plays several drums by its note (ABGRUND's plays its kick
at 147 Hz on the beat and its tom fills on eleven higher notes). Q's PM
voicing rendered them thin (the kick's attack 4-8 dB down above 200 Hz, the
FM-noise hats 9 dB down, the ringing hit's inharmonic tail a rattle).
The owner asked for none of the original percussion character: clean and
snappy, as PS2's drums. So with Q, the drums play **PS2's own drum
definitions** (`kDrums`: the kick gliding 160 to 52 Hz with its click, the
snare, the two hats, the three toms with their membrane and the small room
behind them), struck by HZ's notes.

How a note becomes a drum:

- `hz_snd.c` tells `core/audio.c` at each music key-on on an FM channel the
  song, the track's instrument and its place in the order list
  (`HzSndNote`, the port's own: the Z80 has nothing of the kind; the
  driver's writes are unchanged, `make sound` 0 of 137 differ).
- `core/audio.c` takes the note's pitch from the F-number last written to
  the channel and finds the first entry of the drum table
  (`assets/sound/q_drums.json`) for that song and instrument whose pitch
  range (and, if given, order-list range) holds it (since session 50 an
  entry names its instrument as the song does, `"instrument":
  "kick_toms"`: `assets.c` loads the table after the songs and takes the
  number from them, `songtext_fm_instrument`; an entry naming one its
  song doesn't have is left out, and said); it queues the drum and
  its pitch with the writes, in order, and Q's next key-on on that channel
  strikes it (`q_bank_channel_drum`, `q_bank_channel_drum_hz`).
- In the bank (HZ's additions to `q_synth.cpp`) a strike takes one of 12
  drum voices (a free one, else the oldest), so a tom in a fast fill rings
  on under the next as a kit's does (PS2 plays one drum at a time, as its
  DAC did; HZ's fills run 80 ms a note, which cut each tom before its
  glide settled); its level is the model's `level_db` at the carrier TL
  `tl_ref`, less 0.75 dB a TL step above it (a quieter track or a fade
  strikes it quieter); the toms feed PS2's room.

The table as shipped (each map's song; the owner's choices by ear):

- each song's kick instrument: its kick note (and lower) PS2's kick, its
  higher notes PS2's three toms by rank among them (the lowest third the
  low tom, the top third the high tom; a two-note fill low and high),
  **pitched to the chip's own tom** (session 38, below);
- the ringing hit (songs 3, 6, 8: an FM drum at 294 Hz on the backbeat) the
  snare, its fill notes toms; song 7's noise hit the snare;
- the fast FM-noise instruments (songs 3, 6, 8, 9) the closed hat, song 8's
  slower one the open hat;
- PS2's levels, the snare 1 dB up and the hats 2 dB (PS2's hats sit lower);
  since session 39 each song's kick, snare and hats at the chip's level by
  their entries' `level_db` (below, "The levels").
- the toms shaped rounder than PS2's ("doooom", not "pung": The owner):
  their ring 1.47 times as long (`decay_scale`; 2.2 in session 37, cut to
  two thirds in session 38: too loud in some songs), the membrane's
  overtones 12 dB down (`overtones_db`), the strike's noise and click 10 dB
  down (`attack_db`), 40% of the glide (`glide_depth`) and half the fall
  after it (`fall_scale`);
- each song's toms at the chip's level (session 38, below; Sniper's own
  -5 dB tom models of session 37 are gone, the entries' `level_db` does
  it).

**The toms' pitch (session 38).** Every tom instrument in the songs (and
the two ringing hits whose fill notes are toms) has all four operators at
multiplier 1/2 (DT/MUL $00): the chip sounds **half the note's frequency**
(the F-number's), and the driver's slide takes it down 1.4 semitones a
music tick (about 7 semitones over a fill's 86 ms note). Measured on
Nuked's output (a pitch tracker on each tom alone through `hz_qdrums
strike`) it is exactly that. So session 37's "pitched toms sounded high"
were an octave high, and PS2's fixed toms (173-262 Hz) stood 2-8
semitones over the chip's in ABGRUND, STRAND and EISFREI and 8-18 over it
in VULKAN, LOCH, STADT, WALDUNG and OASE, whose "toms" are 67-140 Hz
booms; Q's toms also stood 1-6 dB over the chip's there (STADT +6.3,
VULKAN +3.7, A-weighted over each tom's note). Now each tom entry has a
`pitch`: the tom struck at the note's frequency times it, fitted per song
so its pitch, averaged over the note by its energy, is the chip's (the
chip falls fast from a higher strike, Q's tom glides a little: Q strikes
1-2.5 semitones under the chip's strike; the factors 0.37-0.48 by the
songs' tempos), and a `level_db` putting its toms at the chip's level
(A-weighted, each tom's note): all within 0.5 dB and 0.1 semitone of the
chip, song by song. In the whole mix the low end Q was missing (50-126
Hz) came back in VULKAN, WALDUNG and OASE (cells 10 dB under the chip 5.7
/ 7.2 / 4.0% -> 1.8 / 2.6 / 2.3%); what Q still lacks there is the chip's
kick ringing on under 80 Hz (PS2's kick, the owner's choice, is short).

**The title's song and the music test's (session 39).** The instruments
read from the song data (each song's 36-byte records): the map songs'
kick/tom patch (algorithm 6, all four operators at multiplier 1/2, D1R
9/9/0/0) is in songs 0, 11, 13, 14, 15, 16 and 17, their ringing snare
patch (DT 1/7/0/3, D2R 0/11/21/9) in 15, 16 and 17; the rest by their
sound (`hz_qdrums strike`, the chip's first 60 ms: spectral flatness 0.35-0.55
is FM noise, 0.002-0.04 a tone):

- song 0 (the title's): instrument 4 the kick (147 and 165 Hz notes) and
  toms (a roll over two octaves), 3 (one noise hit) the open hat;
- song 1: instrument 3 (its own MUL 1/2 patch, 131 Hz) the kick,
  instrument 2 (algorithm 0, a carrier at multiplier 2: a tone at 131-163
  Hz in fast figures between the kicks, a march's snare) the snare -- **a
  guess by its part in the rhythm, to check by ear**;
- songs 11, 13, 14: the kick/tom instrument (5, 5, 3) on its lowest note the
  kick, the rest toms; 11's instrument 1 and 13's 2 (FM noise, the same
  patch) the closed hat, 14's instrument 0 (one noise hit) the open hat;
  13's instrument 4 (a two-carrier bell at 1056 Hz) left on the PM voicing:
  it is a bell, not a drum;
- songs 15, 16, 17: the kick/tom instrument (5, 6, 7) as the map songs';
  the snare instrument (4, 5, 6) the snare on its main note (294 Hz; 17's
  262 Hz), its other notes a tom;
- songs 2 and 12 have no drums.

The once-played songs (0, 11-16, as the game plays them) are surveyed once
through: played on, their order lists' transposition adds up on each pass
(`hz_qdrums` now starts them as the game does). Each song's toms fitted as
in session 38 (pitch 0.43-0.48, within 0.1 semitone; levels within 0.5
dB). The table has 83 entries now (`HZ_DRUM_USES` 64 -> 128).

**The title's song: tuned snares (session 40).** the owner, on Q's toms in
the intro: too much overtone, far too long a ring; the original sounds
like several tuned snares, a snare-wire static going on after the
fundamental has faded. Measured on the chip, each of instrument 4's notes
above its kick (24 notes, against the note's own tone at the strike): the
tone **does not decay** -- it holds its pitch (half the note's) about 20
ms, then dives, to 0.72 of it by 100 ms and 0.49 by 200 ms, out of
hearing -- until the next note on the channel cuts it; a noise burst 13
dB under the tone at the strike (2-12 kHz) falls 22 dB in 45 ms, and
then a **static** stays at about -36 dB (2-12 kHz) and -39 dB (1-2 kHz)
to the next note: op 1 at feedback 7 (noise) modulating op 2, which
sustains. Q's PS2 toms instead held their pitch, rang with the
membrane's overtones and the room, had nothing above 2 kHz after 20 ms
(-55 dB), and rang on under the next note. So the title's song plays a
model of its own, **tuned snare**: a sine struck at the chip's pitch
(`pitch` 0.5) falling 2.5 times its pitch a second (`fall_hz_s` 750 on a
300 Hz model) down to half of it, decaying over 1 s; a noise burst
(band-pass 4 kHz, Q 0.5, 0.06 s) with an 8 ms click at 3 kHz; and two
fields new in session 40: **wires** (`wire_level` 0.2, `wire_decay_s` 1,
`wire_hz` 1200: a second noise with its own envelope, the static) and
**`choke`** (a strike cuts, over 5 ms, the voices a choking model struck
from the same chip channel, as the chip's key-on does; without it the
roll's notes piled up). No membrane, no room. Note by note it now follows
the chip within about 3 dB in every band from the strike to 200 ms
(pitch within 5%); `make q_check` checks the burst, the static and the
dive. Its K-weighted level is within 0.6 dB of the chip's.

(Session 43) Its low notes too: what the table played as PS2's kick
(1B2B and 1AD3, 165 and 147 Hz notes) the owner heard as a snare, and it is:
on the chip they are the same voice an octave down -- the burst 11.5 dB
under the tone, the static at -35.7 dB by 60 ms, the tone holding then
diving (x0.92 at 60 ms, x0.85 at 100) -- and they play a run of
sixteenths with ruffs (every 0.11 s from 1.8 s, 0.055 s in the ruffs), a
snare's part; PS2's kick swept down from 1.23x with nothing over 2 kHz
after 10 ms (-68 dB). They are the model **low tuned snare** (the tuned
snare's, the dive slower: `fall_hz_s` 450, as the chip's at that pitch),
`level_db` -4.9 (the higher ones refitted to -4.3), K-weighted within
0.25 dB; q_check checks their burst and static too. The title's song has
no kick: instrument 4 is all snares.

(Session 44) the owner missed the low tone PS2's kick had given those notes
(more like the original without it, but he liked it): so PS2's kick is
struck with each, a new model field **`with`** (another model by name,
struck on its own voice as that model is defined, no pitch moved, at its
own level plus **`with_db`**, not the entry's): `"with": "kick",
"with_db": -1.3`. Measured against the chip over the low notes: PS2's
kick alone (session 42) was -0.1 dB K-weighted and +1.6 dB at 30-150 Hz
over the first 60 ms; the snare alone -0.2 / +0.4; with the kick at
with_db +1.7 (its old level) +3.2 / +4.2, at -1.3 (chosen) +1.5 / +2.6,
at -4.3 +0.7 / +1.5. So the hits are 1.5 dB over the chip's for the
thump; `with_db` sets it (+1.7: the kick as it was). q_check measures
the low snares' noise against the chip's as it stands now (the kick
moves the tone it was taken against) and the thump (1.5-5 dB over).

The same patch (all of the kick/tom instruments: algorithm 6, feedback 7,
MUL 1/2) is the map songs' and songs 11 and 13-17's toms, and their
kicks: all have the static on the chip. Left as session 38's toms there
(The owner's "doooom" by ear, and those notes sit lower: 65-260 Hz sounded
against the title's 110-390); songs 15-17's are nearest the title's.

(Session 45) **Songs 15-17's toms are tuned snares now** (the endings'
songs and the staff roll's: "Be his soul rest in peace", "A full
retreat", "Go with the stream"). Their kick/tom instrument's notes over
the kick (262-440 Hz notes, sounding 131-220) are on the chip the
title's voice: the burst -11.3 to -11.8 dB under the tone, the tone
holding (-0.8 dB at 200 ms) and diving (x0.71 at 100 ms) -- but the
static 4 dB lower, -40 dB against the title's -36. Q's toms there had no
burst (-22 dB), no static (-49), no dive (x0.92), and decayed 15 dB by
200 ms. So the model **soft-wire tuned snare** (the tuned snare with
`wire_level` 0.12 for 0.2) replaces each song's three tom entries, at
`pitch` 0.5 and `level_db` -4.5 (K-weighted +0.2 dB against the chip:
-4.3 to -4.7 all play at -4.5, the marker's half decibels): burst -12.1
to -12.4, static -39.6, x0.74-0.75 at 100 ms. The snare instruments'
fill notes (two to five a song, the ringing snare patch: noise-heavy on
the chip, static -15 dB) stay toms. The map songs' and 11, 13 and 14's
toms are the same voice on the chip and stay the owner's "doooom".

**The long kick (session 45).** The chip's kick (the same patch's lowest
notes) is a boom: its pitch dives from about 65 Hz to 25 over 250 ms
(the driver's slide), and in 30-150 Hz it stays within 3 dB of its
strike to 250 ms and rings until the next note (song 3, a kick every
0.7 s: -3.5 dB at 250-500 ms). PS2's kick (160 to 52 Hz in 35 ms, its
body 0.09 s) was 58 dB down by 120-250 ms. That was the bottom end Q
lacked between kicks (session 42: Sleight of hand's 30-100 Hz 5.5 dB
under Nuked's, 41% of 20 ms frames 6 dB or more under). The owner wanted to
try a long one: the model **long kick** is PS2's kick (its glide, click
and clip) with its body 1.5 s (`tone_decay_s`) and falling 100 Hz a
second after the glide (`fall_hz_s`, to half its 52 Hz: 26 Hz at 0.3 s),
and a new field **`cut`**: any later key-on on the chip channel (a
drum's strike or a PM note) cuts it over 5 ms, as the chip's key-on ends
the note before it -- so it rings to the next kick but not under a tom
fill (without it the kick rang into the fill's first notes at its
strike's level; with it, under 60 Hz 30-80 ms into a tom, -0.1 dB
against the chip). `choke` is the two-way case among choking models;
`cut` is one-way, the cut model's alone. Every entry that played
`"kick"` plays `"long kick"` now (the map songs, 1, 11, 13-17; the
title's low tuned snares keep PS2's kick as their `with`); its
`level_db` -5.5 keeps the entries' own levels: each song's kick
K-weighted over the hit (to 150 ms) within 0.7 dB of the chip's. Since
it is matched over the hit, its strike is about 5 dB under the short
kick's (PS2's matched only in its first 60 ms). Song 5's kicks against
the chip's (30-150 Hz, each window against the chip's first 20 ms):

| | 0-20 ms | 20-60 | 60-120 | 120-250 |
|---|---|---|---|---|
| chip | 0 | +0.3 | -0.3 | -2.6 |
| PS2's kick (as fitted) | +4.0 | +0.3 | -14.6 | -58.4 |
| long kick | +0.4 | +1.4 | +1.2 | -1.0 |

In the mix (30-100 Hz, 20 ms frames, against Nuked): Sleight of hand -5.5
dB / 41% of frames 6+ dB under -> +1.6 / 7%; song 3 -10.7 / 63% -> -3.5 /
31%; song 8 -10.3 / 64% -> -3.5 / 31%; song 9 -3.9 / 31% -> -1.0 / 10%.
To go back: the entries' `"long kick"` to `"kick"` (their levels are
the short kick's). Tried: 0.8 s and 150 Hz/s (song 3's long gaps fell
off at 250 ms: -13.5 dB against the chip's -3.5).

**The snares boom, the kick at the chip's pitch (session 46).** Session
45 left songs 3 and 8's bottom end 3.5 dB under Nuked's (31% of frames).
In narrower bands (60 s, REPEATS CHIP, Q's energy against Nuked's) it was
two things: 30-50 Hz 3 dB *over*, 50-150 Hz 4 dB under (41% of frames 6+
dB under). Each instrument alone (`hz_qdrums strike`, 60 s) showed where:

- **the ringing snare** (song 3's and 8's instrument 7, and 6's 6, 15's
  4, 16's 5, 17's 6) puts as much into 30-150 Hz on the chip as the
  kick/tom instrument does; PS2's snare was 16-32 dB under it there. The
  driver's write log (`hz_sound --log`) says why: the note 22D3 at the
  patch's MUL 1/2 is a 147 Hz tone, held through its gate (14-62 ms),
  then the release bend (`Track_NoteOff`, `.slide` toward note 0) slides
  it 1.41 semitones a music tick -- 147, 125, 90, 65, 47, 34 Hz at 14,
  43, 101, 159, 216, 274 ms in song 3 (an octave in ~120 ms) -- within
  ~3 dB of its strike until the next key-on on the channel ends it
  (usually the next kick, ~350 ms on). A boom an octave over the kick on
  every backbeat.
- **the long kick's pitch**: the chip's kick (1AD3, 73.5 Hz at MUL 1/2)
  slides the same way, 73, 62, 53, 45, 33 Hz at 14, 43, 72, 101, 159 ms;
  the long kick (160 -> 52 Hz in 35 ms, then 100 Hz/s) sat ~0.6 octave
  under it from 20 to 120 ms: its energy in 30-50 Hz (+3.4 dB against the
  chip's) instead of 50-150 (-4 to -5).

The slide is per tick, so its rate is the song's tempo: 1.41 semitones a
tick everywhere but song 9 (1.09), and ticks of 10.8 ms (song 10) to
20.7 ms (15-17), 68 to 130 semitones a second; the gate is each note's
own. So rather than a fall fitted per song, a model can **follow the
chip channel**: `follow` (QDrumModel/HzDrumModel/assets.c, any model):
its tone plays the channel's pitch (its F-number and block, as the bank's
`chip_hz`) times `follow` (0.5 for these patches), from the strike and
down through every frequency the driver writes there while it rings
(`QBank::hz_follow` from `apply`'s $A0, `DrumVoice::retune`): the release
bend from the note's own key-off, at the song's own tempo. A write up
(the next note's, set just before its key-on, whose key-on `cut`s the
voice) or more than 3 semitones down (a new note's) isn't followed;
neither is an effect's while it holds the channel. `follow_floor_hz`
stops the dive (the chip's goes on toward note 0, a few Hz). A glide
runs from `tone_from_hz` down to the followed pitch; `fall_hz_s` and an
entry's `pitch` aren't used.

The owner: "make snares boom where it makes sense ... a good middle ground
between clean and well produced sounding". So:

- **`booming snare`** (songs 3, 6, 8, 15, 16, 17: the songs whose snare
  is this booming patch; 1's and 7's snares are other instruments and
  keep `snare`): PS2's snare `with` the new **`snare boom`**, a sine at
  `follow` 0.5 (147 Hz on 22D3, 131 on song 17's 2284), decaying over 0.5
  s (`tone_decay_s`), never under 40 Hz, `cut` by the next key-on. The
  middle ground: the boom alone is ~4 dB under the chip's hit (K-weighted
  to 150 ms; its `level_db` -5.7), at the chip's level 60-120 ms in
  (60-150 Hz: -0.7 dB at -2, -3.5 now) and 5-9 dB lighter than the
  chip's from 120 ms (where the chip's drones on); PS2's snare carries
  the rest, ~2 dB down on before (the entries' `level_db`: 1.8 -> -0.2 in
  song 3, 1.9 -> -0.1 in 6, 2.0 -> 0.2 in 8, 1.6 -> -0.3 in 15 and 16, 1.8
  -> 0.0 in 17), so each hit stays at the chip's level (within 0.3 dB).
  Against the chip's snare, by window (60-120 Hz / 120-250 / 1-8 kHz):
  PS2's crack still 8-10 dB over above 1 kHz, the boom's dive within a
  semitone of the chip's.
- **the long kick** follows too: PS2's glide from 160 Hz (its click and
  clip as before) down to the chip's pitch (65-82 Hz by song), then the
  chip's slide, never under 30 Hz; `fall_hz_s` 0. Its `level_db` -5.5 ->
  -7.0 keeps the entries' levels in eleven songs (each kick K-weighted
  within 0.7 dB of the chip's); four came out more under and moved: song
  1 -3.1 -> -1.3, 4 0.2 -> 1.4, 9 -1.0 -> -0.2, 10 0.1 -> 1.3.

In the mix (60 s, REPEATS CHIP; Q's energy against Nuked's, dB, and the
share of 20 ms frames 6+ dB under):

| song | 50-150 Hz before | after | 30-100 Hz mean before | after |
|---|---|---|---|---|
| 3 | -4.1 (41%) | -0.8 (15%) | -3.3 | -0.5 |
| 8 | -4.1 (40%) | -1.0 (16%) | -2.6 | -0.5 |
| 6 | -2.9 (32%) | -0.8 (8%) | -0.7 | -1.0 |
| 15 | -2.3 (16%) | -0.4 (5%) | -1.4 | -0.2 |
| 16 | -2.7 (18%) | -0.9 (6%) | -2.2 | -0.8 |
| 17 | -2.1 (30%) | +0.4 (10%) | -3.3 | -1.0 |

30-50 Hz from +3 dB to 0 (-0.7 in 6, 15, 16); above 150 Hz unchanged. The
kick-only songs (1, 4, 5, 7, 9-11, 13, 14) as before or nearer Nuked's
under 150 Hz (song 7's 30-50 Hz +12.6 -> +5.1, 11's 50-150 -4.9 -> -1.5
mean), but song 9's 30-50 Hz -3.1 dB (+0.3 before: its old kick sat
there; its kick matches the chip's hit by hit, so something else of the
song's is under there); the envelopes against the chip's correlate 0.982
(0.976 before). To go back: the six snare entries' `"booming snare"` to
`"snare"` with their `level_db` 2 dB up (the values above); the long
kick's `follow` and `follow_floor_hz` out, `fall_hz_s` 100, `level_db`
-5.5, and the four kick entries' levels back. To tune by ear: `snare
boom`'s `level_db` (how big), `tone_decay_s` (how long), `follow_floor_hz`
(how low); `booming snare`'s `with_db` moves the boom against the crack
without touching the table's entries.

(Session 48) the owner on A breach of contract (6, STRAND's) with Q: "missing voices
... like a harmony or chord voicing is missing or not mixed correctly".
The FM parts were whole: channels 1-3 (the chords and the bell,
instruments 0, 1, 4, 5) alone, Q within 0.5 dB of the chip and no
semitone 5 dB off; each stereo side within 1-4 dB by band. What was off
was the snare instrument's (6) 38 higher notes, 243B, at 44-64 s (and
two at 1 s): the table played them as the mid tom (`pitch` 0.399), a
tone held at E3 then D#3 for 300 ms, where the chip's is its ringing hit
struck at A3 (half of 440 Hz) and diving (G#3 at 30 ms, E3 at 60, C3 at
100, G#2 at 150, C2 at 200). So in the mix Q was 6-8 dB under the chip
at G#3/A3 there, with those notes among the loudest pitches heard, and
6-10 dB over at D#3-F3: a held wrong note under the chords. Now the
booming snare takes all of instrument 6's notes (the entry's `max_hz`
300 and the mid tom entry gone): the boom follows the chip from G#3/A3
down, within 1-2 semitones; song 6's notes 6 dB under the chip (2 s
windows, every semitone within 35 dB of the loudest): none at 44-64 s,
two single bins elsewhere. `make q_check` all agree. The same mapping,
measured the same way, and changed the same way at the owner's word:
The mournful war (8, instrument 7) 243B (19 notes) held at E3/D#3
against the chip's A3 dive and 235B (11) at C3 against E3; songs 15 and
16 (2 notes each) and 17 (5) a tom 7-10 dB over the chip's, near its
pitch but held. Now the booming snare takes every note of 8's 7, 15's
4, 16's 5 and 17's 6 (their `max_hz` and five tom entries gone, the
table 87 entries): from 30 ms on each follows the chip's dive within a
semitone (at 10 ms PS2's crack). `make q_check` all agree.

Then the owner: song 6's melody line "in the chip version it has more
low mid content. The Q one is very thin." Measured, nothing thin: every
FM instrument by octave within 1.5 dB of the chip (the non-drum FM
together within 0.5 dB, note by note within 4 dB in 2 s windows); the
melody is the PSG's (tones 0 and 1 in parallel, 112 notes each, B4-C#5),
the same with either synth; the game's own 90 s of STRAND (headless,
The owner's settings) Q against NUKED within 1.5 dB an octave, 125-250
Hz 1 dB fuller. Two differences found: Q is wider (side against mid, the
drum models': without the table within 1-3 dB of the chip's, with it 4-7
dB more over 500 Hz) and its mix at the snare hits 2-3 dB brighter over
1 kHz (PS2's crack and the hats; level between the hits). Left for
The owner's ears: three 30 s files (0:40-1:10) at one gain, in
`build/listen/` (not committed): the chip, Q, and Q with no drum table
(its drum instruments FM as on the chip).

The owner's ears on them: in 1 (the chip) the two parallel voices are
"more timbrally complex or have more low mid"; 2 and 3 lack their
forwardness: "the Q timbre doesn't sit in the mix right". They are FM,
not the PSG: instrument 0 (algorithm 0, feedback 7, the chain's three
modulators at TL 21-22; song 3's instrument 0, session 43, is the same
patch) on FM1 and FM2 in parallel thirds from 21 s (B3 over G3, C#4 over
A3, ...; 209 of its 235 key-ons on two channels at once). By octave Q
plays it at the chip's level; what differs is the floor under the notes
(the 20th-percentile FFT bin against the band's mean, 60 ms into each of
the thirds, `hz_qdrums strike`, the bank at the chip's 53,267 Hz): chip
-14.2 / -15.7 / -14.2 / -11.3 dB at 100-500 Hz / 500-2k / 2-8k / 8-20k,
Q (OVERSAMPLE 4, the default) -13.8 / -18.3 / -19.4 / -18.0, at 2
-13.4 / -17.6 / -16.6 / -14.7, at 1 -12.3 / -16.1 / -14.8 / -12.0: the
chip's hash (its partials folded at its own rate, session 43), which
Q's oversampling filters out, and at OVERSAMPLE 1 at the chip's rate
Q's comes back within 0.7 dB of it. In the game the bank runs at the
output's rate (48 kHz), so OVERSAMPLE 1 folds at 24 kHz, not the chip's
26.6: the same kind of hash on other frequencies; song 6's whole mix
then 1.4-2.3 dB over the chip's above 2 kHz (+0.5-1.7 at 4). Two more
files at the same gain: `4_q_oversample_off`, `5_q_oversample_2x`; in
the game `PS2_Q_OVERSAMPLE=1` (or 2) in the environment sets it (HZ has
no OVERSAMPLE row). The owner: "I hear it much more without
oversampling". Both done: Q at the chip's rate, and OVERSAMPLE on the
AUDIO page, 1X the default (above, "Oversample").

**The toms up, the voices at the chip's (session 41).** the owner: the toms
could be a little louder everywhere, and in a couple of songs the synth
voices (a synth bass) sat lower in the mix than with Nuked. The toms: the
three tom models' `level_db` 0 -> +2 (every song's toms, and the snare
instruments' fill toms; the title's tuned snares are their own model and
stay). The voices: every music instrument struck alone (`hz_qdrums
strike`, 90 s, REPEATS CHIP so a repeat's dip doesn't count, K-weighted
over the frames the chip sounds in), Q's against the chip's: Q's PM
voices sit **2.0 dB under** the chip's (median of 72; the bank's trim,
fitted on PS2's peaks), so with the drums at the chip's level since
session 39 the voices were 2 dB low against them; and some lower still:
song 2's three voices -4.2 to -5.5 dB, song 3's instrument 0 -4.0, song
6's 5 -5.0 (alg 5, a bell), song 0's 2 only -1.0. Two were drums left out
in session 39: song 17's instrument 1 (FM noise, 471 notes, every 126 ms;
-9.1 dB) and song 8's 3 (FM noise, a crash every 2.6 s; -8.1 dB), now the
closed hat (+3.3) and the open hat (+6.0), each at the chip's level.

So the table has (session 41) **`voice_level_db`** (top level, 2.0: every
music note Q plays on its PM voice that much louder; not the drums, which
are fitted, nor the effects, whose notes bring no marker) and entries
with the model **`"voice"`** (the instrument stays Q's PM voice, its
`level_db` on top of `voice_level_db`): 16 of them, the instruments more
than 0.75 dB off after the 2 dB (song 2's 0/2/3 +3.5/+2.2/+2.4, 6's 5
+3.0, 3's 0 +2.0, 13's 4 +1.8, 10's 6 +1.4, 4's 0 +1.2, 16's 1 +1.2, 10's
4 +1.0, 6's 0, 15's 3 +0.9, 4's 1, 5's 2, 8's 0 +0.8, 0's 2 -1.0). The
path: the note hook's level marker (`YM_DRUMDB`, sent with every music
note since session 38) now carries it for a PM voice's note too, and
`pm_strike` multiplies the voice's gain by it; the lookup is
`hz_audio_drum_lookup` (core/audio.c), shared with `hz_qdrums strike`.
After: every instrument within 0.75 dB of the chip (median +0.1).
Measured with REPEATS CHIP; with FRESH LOCH's bass (song 5 instrument 3)
still dips 6 dB or more in a quarter of its frames, which CHIP doesn't.

**The levels (session 39).** Each song's kick, snare and hats at the
chip's: K-weighted (ITU-R BS.1770: a high shelf over 1.7 kHz and a high-pass
at 38 Hz) over each hit, to the next strike or 150 ms at most, Q's against
the chip's, the entry's `level_db` moved until the median is within 0.3 dB
(A-weighting, session 38's toms' measure, takes 30 dB off a kick's 50 Hz
and would have put PS2's kick 20 dB over). Before the pass Q stood: kicks
-2.4 to +3.3 dB (most -1 to -2), snares -2.0 to +4.6, the hats -6.2 to
+2.7 (most -3.5 to -4.5) against the chip; now -0.3 to +0.3. By the old
60 ms unweighted measure (session 37's check) the map songs' kicks are
now about 3 dB over the chip's: the chip's kick rings on under 80 Hz,
PS2's is short, so matched over the hit PS2's is louder at its strike.
`tools/hzq_level.py ROUNDS SONG:INST ...` does the pass (and the toms'
pitch and level) for any instruments the table maps, writing
`q_drums.fitted.json`.

Tried and left (the table can still use them): models of HZ's own drums
fitted to the chip (a replacement fit badly; a noise-and-click layer over
the PM voice gave the kick an extra resonance), an 808 kick for EISFREI's
intro (too transient; the owner preferred PS2's kick there), toms transposed
down a fourth, a fifth or an octave (the cut was the problem, not the
pitch). Model fields: PS2's `DrumPatch` (`tone_from_hz` ... `clip`,
`fall_hz_s`), (session 44) `with`/`with_db` (another model struck with it), (session
45) `cut` (any later key-on on the channel cuts it), (session
40) `wire_level`/`wire_decay_s`/`wire_hz` (a
second noise, band-passed, its own decay) and `choke` (any model: a strike
cuts what a choking model last struck from the channel), `layer` (over the
PM voice instead of in its place),
`transpose` (semitones), `ps2` (one of PS2's drums by name: then only
`level_db`, `tl_ref`, `transpose` and the shaping fields count). Entry
fields: `song`, `instrument`, `model`, optional `min_hz`/`max_hz` (the
note's pitch, the F-number's), `order_from`/`order_to` (the track's
order-list position, which `hz_qdrums strike` prints per note), `pitch`
(> 0: the drum's tone moved to the note's pitch times this; 0.5 is the
chip's own pitch for these instruments), `level_db` (session 38: dB on the
model's level, a song's trim of a shared model). In the write queue a
strike's pitch travels in quarter semitones (`hz_audio_drum_hz_code`;
session 37's 4 Hz steps were a semitone at 70 Hz) and its level in half
decibels (`hz_audio_drum_db_code`), as markers `YM_DRUMDB`, `YM_DRUMHZ`,
`YM_DRUM` before the key-on.

While an effect has a channel, a key-on there plays its own voice, never
the drum the music's last note marked (session 38; latent: the effects
that take a drum's channel, `$0F` and `$1A`, only key it off).

**A song's last note under the next song's drums (session 45).** the owner
heard a ring (about 2 kHz or higher) all through "Go with the stream"
(17) after "A full retreat" (16) on the music test, with Q. Found
(`hz_qdrums song 16,17@20`, new: songs one after another): 16's last
note on FM5 -- released at the switch, its own slow release -- stayed
the channel's PM voice, because a drum's strike (`hz_strike`) only set
its operators to release; 17's hat instrument on FM5 plays drums, whose
key-ons never strike a PM voice, so every register the driver wrote
there (the hat's patch: algorithm 5, feedback 7, its slow RR, its notes'
pitch) went on reshaping that old note: a tone at 4.4 kHz 23 dB over 17
alone, then 2.2 kHz, still 9 dB up 8 s later. On the chip the hat's
key-on ends it. Now a drum's strike lets the channel's PM voice go as a
tail (off the channel, faded over 15 ms, as a parallel tail and an
effect's claim do): -0.1 dB against 17 alone. Not Q's library (v1.5.1
and v1.5.4 have the same `q_lib`); HZ's bank, since session 37, wherever
a channel's last PM note was followed by drums (the drum table) -- in a
song too, not only across songs. Muting parts found it: the PM voices,
not the drums or the room. Across every pair of songs (306: each switched
to every other at 6 s; 2-10 s after the switch against the second song
alone, Q's excess less Nuked's) nothing is left ringing: the largest is
7 dB in one 6 Hz bin of a quiet band (10 -> 14 at 4.55 kHz: 2-3 dB on the
band, no tone). The chip has its
own: song 4 after any song starts with the old song's note releasing
under its first seconds at 662 Hz, in Nuked as in Q.
