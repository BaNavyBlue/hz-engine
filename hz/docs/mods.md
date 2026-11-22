# The mods folder (session 49)

Like PS2's (its `docs/mods.md`): a file in `mods/` at the same place as one
in `assets/` is read in place of the game's own, so a player can change the
songs, the pictures or the data and keep the game's files as they are.

## What a player sees

Every start makes the folder if it is missing, writes `mods/README.txt`
(how to use it) if that is missing, and says how many mods are in use and
which they are:

    mods: mods, 1 in use
    mods: mods/sound/songs/03_there_is_no_time_to_lose.txt in place of the game's own

To change a file, copy it from `assets/` to the same place under `mods/`
and edit the copy:

    assets/sound/songs/03_there_is_no_time_to_lose.txt
    -> mods/sound/songs/03_there_is_no_time_to_lose.txt

The game reads the copy at its next start. Deleting the copy undoes it. A
file the game doesn't read is ignored. Any file the game reads can be a
mod: the songs and effects (`sound/`, docs/sound.md), the maps, pictures
and palettes, the JSON data (`assets/README.md` has the formats). A copy of
`sound/sounds.txt` can also name song files of its own, kept in `mods/sound/`:

    song 0 songs/my_title.txt          (mods/sound/sounds.txt)

A song is read from the first there is of: a mod's tracker
(`songs/NN_title.trk`, docs/sound.md, The tracker), a mod's text
(`NN_title.txt`), the game's tracker, the game's text. So a text mod isn't
hidden behind the game's tracker, and a tracker mod is read over both.

`--mods DIR` or `HZ_MODS` names another folder; `HZ_MODS=""` turns it off
(the checks do: a tree's own `mods/` never reaches them). The tools
(`hz_sound`, `hz_roundtrip`, the check harnesses) never read it. `mods/` is
in `.gitignore`.

## When a mod is refused

A mod the game refuses is set aside, and the game reads its own file in its
place. It says why, by the mod's own path (and line, for the song text),
and names what it set aside:

    songtext: mods/sound/songs/00_jagd_flieger.txt:33: 'X5': not a note or a command
    mods: mods/sound/songs/00_jagd_flieger.txt set aside: it was refused (above); the game's own assets/sound/songs/00_jagd_flieger.txt is read in its place
    mods: mods, 1 in use
    mods: mods/sound/songs/01_the_previous_war.txt in place of the game's own

The game refuses what its loaders refuse:

- a PNG it can't read;
- JSON that doesn't parse;
- each loader's own checks: a tile sheet that isn't whole tiles, a palette
  short of colours, a map without its layers, a units file without its 14
  records, and so on;
- the song text's refusals: a word it doesn't know, a note out of range,
  an instrument's name it doesn't have or from the other list (an FM
  one on a PSG channel: `docs/instrument_names.md`), a song too long for
  its window, the effects in the song table.

Only the mod the refusal names is set aside; other mods stay. A refusal
that names no mod (the songs together too long for the effects, said
against the game's own `sounds.txt`) sets aside every mod that loader read,
and it runs again. A mod that reads but means something odd (a slower
tempo, a stronger unit) isn't refused: the game plays what it says. So
with a song mod that renames an instrument the Q drum map names
(`q_drums.json`): its entries for the old name are left out, and said
(`docs/instrument_names.md`), until a mod of the map renames them too. Until
now a broken file in `assets/` stopped the game at its start; it still
does, since there is no file of the game's own behind it.

## How it is built

- `src/core/hz_mods.c`: `hz_mods_set(assets, mods)` turns the folder on
  (main.c only). `hz_mods_resolve` gives the path to read for an asset
  path: its mod's, if one is under the folder and not set aside. It notes
  each mod it hands out with the loader step that read it.
  `hz_mods_refused` marks a mod a reader turned down and gives its path for
  the message. `hz_mods_set_aside` sets aside the step's refused mods,
  otherwise every mod the step read.
- Every asset read goes through `hz_file_read`, which resolves.
  `png_load_indexed`, `json_parse_file` and the song text's `src_open`
  resolve first, so that their messages name the mod, and mark it refused
  when they fail. So does `assets.c`'s `fail`.
- `hz_assets_load` runs its loaders as `STEP`s. A step that fails with mods
  set aside runs again; one that fails with none to set aside fails the
  load, as before.
- `main.c`: `--mods`/`HZ_MODS` (default `mods`), then `README.txt`, the
  load, and the report.

## Checked

`make mods_check` (`tools/hzmods_check.py`) runs `hz_headless` with a mod
of each kind, good and broken, each in a scratch mods folder of its own
(all at once, about 15 s). It holds each one to the game with no mods
folder (`HZ_MODS=""`, which also says and writes nothing):

| Case | What the mod is | Expected |
|---|---|---|
| `empty` | an empty folder | 0 in use, README written, the same |
| `song` | the title's first note an octave down | in use; the WAV differs, the frame doesn't |
| `song_broken` | a note `X5` | set aside; the same WAV |
| `song_broken_and_good` | that, and song 1 copied as it is | the broken one set aside, the good one in use |
| `tracker` | the title's tracker with the same note an octave down | in use; the `song` case's WAV |
| `tracker_broken` | a note `X-5` in the title's tracker | set aside; the same WAV |
| `tracker_and_text` | the title's tracker as it is, and the `song` edit | the tracker in use; the same WAV |
| `new_song_file` | `sounds.txt` naming `songs/my_title.txt`, the `song` edit | both in use; the `song` case's WAV |
| `song_too_big` | song 3 grown by 29 KB, and song 1 | both set aside (the refusal names neither) |
| `sounds_broken` | `effects $8010` | set aside |
| `png` | `palettes/title.png` with its colours inverted | in use; the title screen differs |
| `png_broken` | `palettes/title.png` not a PNG | set aside; the same |
| `json_broken` | `data/units.json` with a stray `}` | set aside; a CPU game the same |
| `json_refused` | `data/units.json` with no units | set aside; the same |
| `map` | ABGRUND's palette inverted | in use; the game's frame differs |
| `map_broken` | ABGRUND's `layout.tmx` without its layers | set aside; the same |
| `unread` | files the game doesn't read | 0 in use, the same |

20 of 20 agree, the three no-mods runs included (title, title screen,
game). `make roundtrip`, `make sound` and `make menu` hold.
