# Instrument names (session 50)

Every instrument has a name now, as PS2's do (its
`docs/instrument_names.md`). The name goes on the instrument's head line,
and a channel picks the instrument by it:

    fm_instrument 4 chords
    ...
    instrument chords  F#4 24 22 96  ...          (song text)
     00 | F#4 v96 instrument chords | ...          (tracker)

`make assets` writes them, from `tools/hzsongtext.py`'s `INSTRUMENT_NAMES`.
The files then have them, so to rename one, change its head line and
every `instrument` that names it (in a song's `.txt` or `.trk`, or in
`effects.txt`). A number still picks an instrument too (`instrument 4`),
and a head line may leave its name out.

The rules, the same in both readers (`src/audio/songtext.c`, the game's;
`tools/hzsongtext.py`):

- A name is one word: a letter, then letters, digits or `_`, 31 at most.
- No two instruments in one file have the same name, FM and PSG together.
- A name picks from its own list. An FM channel (fm1-fm6) plays the FM
  instruments, and psg1, psg2 and the noise play the PSG ones. So a PSG
  instrument's name on an FM channel is refused, and so is the reverse.
  In the song text that holds for a phrase too: it is checked against
  every track that plays it.

Each name goes with its file, as each song's instruments do (`chords` in
one song and in another are two instruments).

The Q synth's drum map (`assets/sound/q_drums.json`, docs/q.md) names
them too: `{"song": 3, "instrument": "kick_toms", ...}`. So a rename in a
song is a rename there too. An entry naming an instrument its song
doesn't have is left out, and the game says so; the rest of the map
holds:

    assets: assets/sound/q_drums.json: drums[0]: song 3 has no FM instrument "kick_toms": left out

    songtext: assets/sound/songs/03_there_is_no_time_to_lose.txt:59: instrument lo: no instrument of that name
    songtext: assets/sound/songs/03_there_is_no_time_to_lose.trk:17: instrument psg_chords: a PSG instrument, on fm1
    songtext: assets/sound/songs/03_there_is_no_time_to_lose.txt:59: instrument psg_chords: a PSG instrument, in phrase 2, played on fm3

## How the names were chosen

They follow PS2's scheme, which the owner agreed for PS2. They are
proposals: any of them can be renamed.

Every song was walked once through, as the tracker's exporter walks it
(`hztracker.py`'s `channel_cells`: the driver's sequencing). Each note was
taken with the instrument its channel had then, as heard: after the
transposes, a PSG tone an octave up, nothing under C3. Then:

- **A drum** is named as `assets/sound/q_drums.json` (the Q synth's drum
  map) has it: `kick_toms` (FM noise played low as the long kick and
  higher as toms), `kick_snare` (the long kick and a tuned snare), `kick`,
  `snare`, `closed_hat`, `open_hat`.
- **`chords`**: half its notes or more are struck at the same tick as the
  same instrument on another channel (parts in thirds, chords).
- **Otherwise, where its middle note sits:** `bass` below C3, `low` C3-B3,
  `mid` C4-B4, `high` from C5.
- **A PSG instrument** (a volume envelope) has `psg_` before its name.
- **A second instrument of the same name** gets `_2`, and so on, in the
  instruments' order.
- **An effects' instrument** takes the name of the first effect that plays
  it (by the game's effect number, `SFX_*`). The PSG ones play on the noise
  channel, and have `psg_` before their name.
- **What isn't a song's own instrument** gets no name: the PSG instrument
  0 that the title and Back to square one read from the next song's bytes
  (docs/sound.md, Room). Their trackers write that instrument out without
  a name.

The table below gives, for each instrument: the channels that play it, the
range of its notes as heard and how many there are (one pass, a song's
repeats not counted), and the reason for its name.

## The effects

| | Name | Played by (effect/channel) |
|---|---|---|
| FM 0 | `hit_unit` | hit_unit, menu_move (fm6) |
| FM 1 | `ordered` | ordered, unit_ready, hq_alert, refused |
| FM 2 | `fire_missile` | fire_missile, jets_meet, post_taken, sizzle |
| FM 3 | `fire_jet` | fire_jet |
| FM 4 | `mech_destroyed` | mech_destroyed, hit_mech, hq_destroyed |
| FM 5 | `fire_robot` | fire_robot, take_off |
| FM 6 | `hold_raised` | hold_raised, post_taken (its phrase turns to it) |
| FM 7 | `hit_tile` | hit_tile |
| FM 8 | `land` | land, menu_open |
| FM 9 | `infantry_destroyed` | infantry_destroyed |
| FM 10 | `unused_25` | unused_25 (no caller plays it) |
| FM 11 | `missile_alert` | missile_alert, unused_25 |
| PSG 0 | `psg_fire_unit` | fire_unit (its phrase turns to it; noise) |
| PSG 1 | `psg_mech_destroyed` | mech_destroyed, lift, hq_destroyed |
| PSG 2 | `psg_fire_unit_2` | fire_unit (first), unit_destroyed, hq_destroyed |
| PSG 3 | `psg_hq_hit` | hq_hit |
| PSG 4 | `psg_drop` | drop, debris_end |

## The songs

### 0 Jagd flieger

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `low` | fm2 | A2-A4, 27 | middle note G3 |
| FM 1 | `chords` | fm3, fm4, fm5 | A0-A7, 210 | 100% struck with itself on another channel |
| FM 2 | `high` | fm6 | A4-B7, 39 | middle note A6 |
| FM 3 | `open_hat` | fm1 | E3-E3, 1 | q_drums.json: open hat |
| FM 4 | `snare` | fm1, fm2 | D3-G5, 104 | q_drums.json: low tuned snare, tuned snare |
| PSG 0 | (none) | psg1, psg2 | F4-E6, 136 | not the song's own: the next song's first bytes (sound.md, Room) |

### 1 The previous war

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `high` | fm1, fm5, fm6 | B4-D#5, 24 | middle note C5 |
| FM 1 | `bass` | fm2 | E2-G#2, 40 | middle note F2 |
| FM 2 | `snare` | fm3, fm4 | C2-C2, 40 | q_drums.json: snare |
| FM 3 | `kick` | fm3, fm4 | C3-C3, 24 | q_drums.json: long kick |
| PSG 0 | `psg_mid` | psg1, psg2 | G4-C5, 16 | middle note A4 |

### 2 The fighter called EMERGENCY to base

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `mid` | fm1, fm2 | G4-A#4, 6 | middle note A4 |
| FM 1 | `low` | fm3 | G3-G#4, 32 | middle note G3 |
| FM 2 | `bass` | fm4 | E1-B1, 103 | middle note E1 |
| FM 3 | `high` | fm5, fm6 | D4-A#5, 96 | middle note D5 |
| PSG 0 | `psg_high` | psg1, psg2 | D#4-A#5, 64 | middle note D#5 |

### 3 There is no time to lose

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `mid` | fm4 | F#3-F#5, 86 | middle note F#4 |
| FM 1 | `low` | fm3 | C#2-E5, 402 | middle note D3 |
| FM 2 | `high` | fm4 | C#1-C#7, 2 | middle note C#7 |
| FM 3 | `closed_hat` | fm6 | D5-D5, 544 | q_drums.json: closed hat |
| FM 4 | `chords` | fm1, fm2 | F#3-F#5, 184 | 100% struck with itself on another channel |
| FM 5 | `chords_2` | fm1, fm2 | A3-F#5, 114 | 89% struck with itself on another channel |
| FM 6 | `high_2` | fm4 | F#1-C#7, 134 | middle note F#6 |
| FM 7 | `snare` | fm5 | D4-D4, 89 | q_drums.json: booming snare |
| FM 8 | `kick_toms` | fm5 | D3-E5, 168 | q_drums.json: long kick, low tom, mid tom, high tom |
| PSG 0 | `psg_chords` | psg1, psg2 | A3-F#5, 112 | 100% struck with itself on another channel |

### 4 Take it easy

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `mid` | fm1, fm4, fm5, fm6 | A2-A5, 692 | middle note B4 |
| FM 1 | `low` | fm1, fm4 | D3-A4, 160 | middle note A3 |
| FM 2 | `low_2` | fm1, fm4, fm5, fm6 | G2-B4, 180 | middle note A3 |
| FM 3 | `bass` | fm2 | E2-A4, 368 | middle note G#2 |
| FM 4 | `chords` | fm1, fm4, fm5, fm6 | G4-G#5, 384 | 100% struck with itself on another channel |
| FM 5 | `low_3` | fm1, fm4 | D3-A4, 160 | middle note A3 |
| FM 6 | `mid_2` | fm1, fm4 | G2-G#5, 332 | middle note A4 |
| FM 7 | `kick_toms` | fm3 | C3-B3, 310 | q_drums.json: long kick, low tom, high tom |
| PSG 0 | `psg_high` | psg1, psg2 | A3-A6, 688 | middle note B5 |
| PSG 1 | `psg_mid` | psg1, psg2 | B3-E5, 40 | middle note A4 |
| PSG 2 | `psg_high_2` | psg1, psg2 | G5-G#6, 192 | middle note D6 |

### 5 Sleight of hand

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `chords` | fm1, fm2, fm3, fm5 | E3-B5, 274 | 52% struck with itself on another channel |
| FM 1 | `chords_2` | fm1, fm3, fm5 | B3-A5, 276 | 91% struck with itself on another channel |
| FM 2 | `chords_3` | fm1, fm5 | D4-B5, 132 | 59% struck with itself on another channel |
| FM 3 | `bass` | fm2 | E1-C3, 380 | middle note G2 |
| FM 4 | `chords_4` | fm1, fm5 | D4-B5, 132 | 59% struck with itself on another channel |
| FM 5 | `kick_toms` | fm4 | C3-G5, 320 | q_drums.json: long kick, low tom, mid tom, high tom |

### 6 A breach of contract

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `chords` | fm1, fm2, fm3 | C#3-E5, 471 | 98% struck with itself on another channel |
| FM 1 | `chords_2` | fm1, fm2, fm3 | F#3-F#5, 118 | 81% struck with itself on another channel |
| FM 2 | `low` | fm4 | B1-C#5, 538 | middle note C#3 |
| FM 3 | `closed_hat` | fm6 | D3-D5, 586 | q_drums.json: closed hat |
| FM 4 | `high` | fm3 | F#1-B5, 119 | middle note C#5 |
| FM 5 | `mid` | fm3 | A3-D5, 28 | middle note F#4 |
| FM 6 | `snare` | fm5 | D4-A4, 129 | q_drums.json: booming snare |
| FM 7 | `kick_toms` | fm5 | D3-C#5, 360 | q_drums.json: long kick, low tom, mid tom, high tom |
| PSG 0 | `psg_chords` | psg1, psg2 | C#4-B5, 220 | 100% struck with itself on another channel |

### 7 Back to square one

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `chords` | fm1, fm3, fm5, fm6 | F#2-D5, 436 | 99% struck with itself on another channel |
| FM 1 | `mid` | fm3, fm6 | A3-C#4, 16 | middle note C#4 |
| FM 2 | `chords_2` | fm1, fm3, fm5, fm6 | E2-C#5, 398 | 96% struck with itself on another channel |
| FM 3 | `kick_toms` | fm4 | C3-D#4, 313 | q_drums.json: long kick, low tom, mid tom, high tom |
| FM 4 | `snare` | fm4 | C1-B2, 109 | q_drums.json: snare |
| FM 5 | `low` | fm2 | C#2-C#4, 332 | middle note F#3 |
| PSG 0 | (none) | psg1, psg2 | A4-F#6, 172 | not the song's own: the next song's first bytes (sound.md, Room) |

### 8 The mournful war

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `mid` | fm6 | C2-D5, 216 | middle note E4 |
| FM 1 | `chords` | fm1, fm2 | A3-A4, 16 | 100% struck with itself on another channel |
| FM 2 | `closed_hat` | fm5 | D2-D3, 328 | q_drums.json: closed hat |
| FM 3 | `open_hat` | fm5 | D3-D3, 32 | q_drums.json: open hat |
| FM 4 | `chords_2` | fm1, fm2 | E2-C4, 222 | 100% struck with itself on another channel |
| FM 5 | `bass` | fm3 | C1-D4, 548 | middle note C2 |
| FM 6 | `chords_3` | fm1, fm2 | E3-A4, 80 | 100% struck with itself on another channel |
| FM 7 | `snare` | fm4 | D4-A4, 162 | q_drums.json: booming snare |
| FM 8 | `open_hat_2` | fm5 | D3-D4, 212 | q_drums.json: open hat |
| FM 9 | `kick_toms` | fm4 | D3-C5, 165 | q_drums.json: long kick, low tom, mid tom, high tom |
| PSG 0 | `psg_chords` | psg1, psg2 | A#3-D5, 388 | 100% struck with itself on another channel |

### 9 Sniper

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `chords` | fm1, fm6 | F4-E5, 127 | 61% struck with itself on another channel |
| FM 1 | `high` | fm1 | G4-G5, 32 | middle note C5 |
| FM 2 | `chords_2` | fm1, fm3, fm6 | E2-D6, 1013 | 69% struck with itself on another channel |
| FM 3 | `bass` | fm2 | E2-B3, 466 | middle note A2 |
| FM 4 | `closed_hat` | fm5 | C5-G5, 412 | q_drums.json: closed hat |
| FM 5 | `high_2` | fm5 | C5-C5, 1 | middle note C5 |
| FM 6 | `kick_toms` | fm4 | C2-G5, 401 | q_drums.json: long kick, low tom, mid tom, high tom |

### 10 The super fighter invigorated us

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `low` | fm1, fm5 | A2-A4, 448 | middle note F3 |
| FM 1 | `low_2` | fm2, fm6 | F2-F4, 392 | middle note D3 |
| FM 2 | `low_3` | fm3 | C2-A3, 520 | middle note D3 |
| FM 3 | `chords` | fm1, fm2, fm5, fm6 | A3-A#4, 116 | 50% struck with itself on another channel |
| FM 4 | `high` | fm2, fm6 | F5-A6, 62 | middle note D6 |
| FM 5 | `low_4` | fm4 | C3-C3, 8 | middle note C3 |
| FM 6 | `mid` | fm4 | C3-C4, 44 | middle note C4 |
| FM 7 | `low_5` | fm4 | C3-G3, 9 | middle note C3 |
| FM 8 | `kick_toms` | fm4 | C3-G5, 359 | q_drums.json: long kick, low tom, mid tom, high tom |
| PSG 0 | `psg_chords` | psg1, psg2 | E5-E7, 156 | 88% struck with itself on another channel |
| PSG 1 | `psg_chords_2` | psg1, psg2 | C5-F6, 56 | 79% struck with itself on another channel |

### 11 The dawn of a new age

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `bass` | fm2 | G2-C4, 56 | middle note B2 |
| FM 1 | `closed_hat` | fm3, fm4 | C2-C2, 171 | q_drums.json: closed hat |
| FM 2 | `bass_2` | fm3 | C2-C2, 1 | middle note C2 |
| FM 3 | `chords` | fm2, fm3 | A3-F4, 128 | 100% struck with itself on another channel |
| FM 4 | `low` | fm1, fm5, fm6 | B2-A#4, 127 | middle note B3 |
| FM 5 | `kick_toms` | fm4 | E3-A4, 50 | q_drums.json: long kick, low tom, mid tom, high tom |
| PSG 0 | `psg_chords` | psg1, psg2 | D5-E6, 130 | 55% struck with itself on another channel |

### 12 Funeral march for the METAL KNIGHTS

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `chords` | fm2, fm3, fm4 | E2-A4, 131 | 100% struck with itself on another channel |
| FM 1 | `high` | fm1, fm5, fm6 | E4-G5, 75 | middle note C5 |
| FM 2 | `mid` | fm4, fm6 | E4-D5, 74 | middle note A4 |
| PSG 0 | `psg_high` | psg1, psg2 | F5-A6, 186 | middle note C6 |

### 13 A Victorious bell is ringing BRASS

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `high` | fm1, fm5, fm6 | B4-F5, 100 | middle note D5 |
| FM 1 | `low` | fm2 | G2-C3, 37 | middle note C3 |
| FM 2 | `closed_hat` | fm4 | C4-C4, 58 | q_drums.json: closed hat |
| FM 3 | `mid` | fm4 | C4-C4, 2 | middle note C4 |
| FM 4 | `high_2` | fm6 | C6-C6, 47 | middle note C6 |
| FM 5 | `kick_toms` | fm3 | C3-G4, 84 | q_drums.json: long kick, low tom, mid tom, high tom |
| PSG 0 | `psg_chords` | psg1, psg2 | B4-D6, 54 | 100% struck with itself on another channel |

### 14 A Victorious bell is ringing BELL

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `open_hat` | fm3 | C3-C3, 1 | q_drums.json: open hat |
| FM 1 | `chords` | fm1, fm4, fm5, fm6 | B4-C7, 143 | 59% struck with itself on another channel |
| FM 2 | `bass` | fm2 | G1-C2, 37 | middle note C2 |
| FM 3 | `kick_toms` | fm3 | C3-G4, 83 | q_drums.json: long kick, low tom, mid tom, high tom |
| PSG 0 | `psg_chords` | psg1, psg2 | B4-D6, 54 | 100% struck with itself on another channel |

### 15 Be his soul rest in peace

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `bass` | fm6 | C2-D2, 10 | middle note D2 |
| FM 1 | `bass_2` | fm5 | C2-D2, 34 | middle note D2 |
| FM 2 | `mid` | fm1, fm2 | G3-C5, 110 | middle note F4 |
| FM 3 | `mid_2` | fm1, fm2, fm3, fm4 | G2-A4, 330 | middle note D4 |
| FM 4 | `snare` | fm6 | D4-A4, 23 | q_drums.json: booming snare |
| FM 5 | `kick_snare` | fm6 | D3-C5, 106 | q_drums.json: long kick, soft-wire tuned snare |
| PSG 0 | `psg_chords` | psg1, psg2 | E5-A5, 512 | 100% struck with itself on another channel |

### 16 A full retreat

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `bass` | fm6 | C2-D4, 62 | middle note D2 |
| FM 1 | `bass_2` | fm1, fm2 | G1-A2, 64 | middle note D2 |
| FM 2 | `bass_3` | fm5 | C2-D2, 34 | middle note D2 |
| FM 3 | `mid` | fm1, fm2 | A3-D5, 96 | middle note G4 |
| FM 4 | `high` | fm3, fm4 | C6-A6, 266 | middle note D6 |
| FM 5 | `snare` | fm6 | D4-A4, 23 | q_drums.json: booming snare |
| FM 6 | `kick_snare` | fm6 | D3-C5, 106 | q_drums.json: long kick, soft-wire tuned snare |
| PSG 0 | `psg_chords` | psg1, psg2 | E5-A5, 512 | 100% struck with itself on another channel |

### 17 Go with the stream

| | Name | Channels | Notes heard, count | Why |
|---|---|---|---|---|
| FM 0 | `chords` | fm1, fm2, fm3 | D1-C6, 356 | 98% struck with itself on another channel |
| FM 1 | `closed_hat` | fm5 | D3-D4, 662 | q_drums.json: closed hat |
| FM 2 | `bass` | fm4 | C1-F4, 420 | middle note G2 |
| FM 3 | `mid` | fm1 | F0-F5, 58 | middle note G4 |
| FM 4 | `chords_2` | fm1, fm2, fm3 | A0-A#4, 251 | 97% struck with itself on another channel |
| FM 5 | `bass_2` | fm4 | C2-A#2, 65 | middle note F2 |
| FM 6 | `snare` | fm6 | C4-D4, 90 | q_drums.json: booming snare |
| FM 7 | `kick_snare` | fm6 | D3-A4, 251 | q_drums.json: long kick, soft-wire tuned snare |
| PSG 0 | `psg_chords` | psg1, psg2 | F3-F5, 732 | 100% struck with itself on another channel |
| PSG 1 | `psg_chords_2` | psg1, psg2 | F3-C6, 316 | 100% struck with itself on another channel |
