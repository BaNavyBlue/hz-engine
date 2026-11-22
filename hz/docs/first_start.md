# The first start: the game's files from the player's cartridge

(Session 78.) The game reads everything it uses from `assets/` and never the
ROM (src/game/assets.c). `tools/hz_extract.py` makes `assets/` from the ROM,
and this tree has those files checked in. A copy of the game without them
makes them itself, the first time it starts, from the player's own cartridge
image. It does this in C, inside the game, with no Python and no library but
the game's own code (`src/extract/`).

## What the player sees

`hz` (or `HerzogZwei.exe`) starts, finds no `assets/data/units.json`
(`HZ_MADE_FILE`), and opens a small window of its own before the game's:

- it asks for the cartridge image: drop it onto the window, or press Enter
  (or click) for the system's file dialog. An image named on the command
  line, or dropped onto the program's icon, is used at once;
- the extraction runs in 15 steps, one a frame, with a bar and the step's
  name (the eight maps, the sprites and tiles, the units and tables, the
  title and menus, the music and sounds, the endings, the demos, the views);
- if it can't, the window says why (not the cartridge, a zip with two
  images, a file that can't be written) and asks again. Escape quits;
- when every file is made, the window closes and the game starts as
  usual.

The window writes in SDL's built-in 8x8 face (`SDL_RenderDebugText`), since
the game's own font is one of the files to be made.

The headless game (`hz_headless`) has no window: it says on stderr how to
make the files and stops. A folder named by `--assets` is taken as it is and
never made.

## The cartridge

The files are made from Herzog Zwei for the USA, the image the disassembly
assembles to:

| | |
|---|---|
| Name in most sets | `Herzog Zwei (U) [!].bin` |
| Size | 524,288 bytes |
| SHA-1 | `8f7262102c2b2334f0bc88ee6fd6b08797919176` |
| Header | `GM MK-1400 -00`, region U |

`src/extract/ex_image.c` takes it as players keep it:

- a raw image (`.bin`, `.md`, `.gen`);
- a `.smd` (512-byte header, then 16 KB blocks with odd bytes first),
  put back in order;
- a raw image with a copier's 512-byte header, which is dropped;
- a `.zip` holding exactly one of those, stored or deflated (the inflate is
  the PNG reader's, `png_inflate_raw`).

Then the bytes must be the cartridge's by size and SHA-1 (`src/core/sha1.c`).
Anything else is refused. The message names what was given (its header's
title and serial when it has one) and what is wanted.

## Where the files go

They go into `assets/` beside the game: the folder `platform_find_home`
looks in first. Started from elsewhere with no `assets/` there, the game
moves to its own folder first. `assets/data/units.json` is the mark that
the files are made. The extractor writes it as `units.json.part` and renames
it only after every other file (`hz_ex_finish`), so a run cut short leaves no
mark and the next start makes the files again.

To make them again: delete `assets/data/units.json` and start the game, or
run `./hz_assets IMAGE`.

Five files under `assets/` are the port's own and no extraction makes
them: `README.md`, `data/repair.json`, `data/turrets.json`,
`sound/q_drums.json`, and `maps/path_hints.png` (hz_extract.py's drawing of
16 arrows for Tiled, nothing of the cartridge's). A copy of the game without
the game's files still carries these five.

## The extractor (`src/extract/`)

It is `tools/hz_extract.py` in C, step for step. The Python stays the
reference: `make assets_check` holds the C to it.

| File | What |
|---|---|
| `extract.h` | its face: the image read and known, the steps one at a time, `hz_ex_all` |
| `extract.c` | the steps, in hz_extract.py's order |
| `ex.h`, `ex_out.c` | the cartridge as read, files written (folders made), write_text's headers, write_json's `json_text` exactly, Pillow's indexed PNG, `cram_rgb` |
| `ex_pic.c` | `put_tile`, the tile sheets, `write_sprite_sheet` with `sheet_layout`, the palette strips |
| `ex_image.c` | the player's image: raw, .smd, copier header, zip; the SHA-1 check |
| `ex_maps.c` | `extract_map`: palette, tiles, blocks, anim, layout.tmx, radar |
| `ex_tiles.c` | `extract_tiles`: the sprite sheets and their JSON, the font, the game's palettes, hud_frame.txt |
| `ex_data.c` | `extract_data`: units, start units, sound priorities, the order menu |
| `ex_title.c` | `extract_title`: the title's and options screen's pictures and palettes, the game's end, menu.json |
| `ex_sound.c` and the files beside it | `extract_sound`: hzsongtext.py's text and hztracker.py's trackers, checked by building them back with the game's own builder (`src/audio/songtext.c`) |
| `ex_ending.c` | `extract_ending`: the endings' pictures, the stories, the staff roll |
| `ex_demos.c` | `extract_demos`: data/demos.json, each map's demo.json |
| `ex_views.c` | `render_views`: each map's blocks.png and hud_frame.png, drawn from the cartridge's bytes (the files render_views reads back hold exactly those) |
| `rom_map.h` | where everything is in the ROM, by the disassembly's names |

No ROM address is written in the C. They are `rom_map.h`'s `EXR_` names,
which `tools/hz_rommap.py` writes from the tree's `hz.sym` (`make rom_map`
after `make rom` moved a label; `--check` says whether it is current). No
data of the cartridge's is copied into the C either. It holds only format
facts: record layouts, the codes of the text, the tables hz_extract.py
itself holds (the sheet groups' names, the unit fields).

`hz.sym` is checked in since this session: the original ROM's map, which
hz_extract.py also reads an image outside the tree through.

### Text the same byte for byte; PNGs the same picture

Every text and JSON file is the Python's byte for byte. A PNG is the same
picture (mode, size, palette, transparency, pixels) but not the same bytes,
since it is another deflate (`src/core/pngwrite.c`'s fixed-Huffman one).
`png_write_indexed` follows Pillow's rules for an indexed image:

- the fewest bits a pixel that hold the palette: 1 for up to 2 colours, 2 for
  up to 4, 4 for up to 16, else 8;
- a PLTE of exactly the palette's colours;
- with `transparency=0`, a tRNS of one 0 byte.

### The network game's id

Two copies play together online only when their files agree
(`net_game_id`, `src/core/hz_file.h`). Since PNG bytes differ between
writers (and between one Pillow and another: 43 of 59 differed with PyPI's,
whose zlib is another), the loader hashes a PNG by its decoded picture: its
size, palette and pixels (`png.c`'s `hash_picture`), and every other file by
its bytes. `NET_GAME_CODE` went to 12 with that change. Files made by the
game's first start and this tree's `assets/` have the same id.
`tools/hz_gameid` prints a folder's id.

## The tools

- `./hz_assets IMAGE [DIR] [--only K,...]` runs the extractor from a
  terminal (`make hz_assets`; no SDL). `--only` runs some steps alone, for
  working on one.
- `make assets_check ROM=IMAGE` runs `hz_assets` into `build/assets_c/` and
  then `tools/hzassets_check.py`. That copies in the port's five files, then
  wants every file of `assets/` there and the same, nothing more, and the
  two folders' network ids equal (`./hz_gameid`). The check needs Pillow, the
  game doesn't.
- `./hz_gameid DIR...`: each folder's network game id.
