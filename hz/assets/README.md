# assets/

The game's data, as the C port reads it. `tools/hz_extract.py` wrote all
of it from the ROM (`make assets ROM=hz.md`); the port loads every file
once at start-up (`src/game/assets.c`) and never reads the ROM. Edit
anything here, run `hz` again, and the change is in the game. Or leave
these as they are and put the edited copy in `mods/`, at the same place:
the game reads it in place of this one, and sets it aside if it refuses
it (`docs/mods.md`).

`make roundtrip ROM=hz.md` loads these files with the game's own loader,
packs them back into the ROM's layout and compares, region by region: on
an untouched tree every region is identical; after an edit it shows which
region changed and where. `make assets` overwrites everything here with
the ROM's data again.

Files marked *view* are pictures for looking at; nothing loads them, and
editing them changes nothing. `make views` redraws them from the other
files after an edit.

Not here yet, because nothing ported uses them: the attract demo's
records. The ROM's small code tables (`SineTable`, `JoyToDir`, ...) aren't assets: they
are C arrays in `src/game/tables.c` (the Z80 driver's in
`src/audio/hz_snd.c`). The Z80 driver itself is code, ported to C
(`src/audio/hz_snd.c`); its data, the songs and effects, is `sound/`, as
text.

## Formats

Everything made of pixels is an indexed PNG, as in the PS2 port: the
pixels are palette indices 0-15, which is what the hardware stores, and
any editor that keeps an image indexed edits them. A PNG may be saved at
any bit depth and with any palette size; only indices over 15 are refused.

Colours are 3 bits a channel on the Mega Drive. A palette PNG's colours
go through the measured DAC ladder (0, 52, 87, 116, 144, 172, 206, 255),
and the loader snaps each channel back to the nearest of those eight, so
a colour picked in an editor becomes the nearest one the hardware can
show.

Words in text files are hex, `#` starts a comment, blank lines are
ignored. In JSON, numbers are decimal, and values written `"$..."` are hex
(bit fields and tile numbers); either form is accepted anywhere.

## maps/

One folder a map: `0_abgrund`, `1_vulkan`, `2_loch`, `3_strand`,
`4_stadt`, `5_eisfrei`, `6_waldung`, `7_oase`. A map is 64 x 64 blocks; a
block is 4 x 4 tiles of 8 x 8 pixels (2048 pixels square).

| File | ROM | What |
|---|---|---|
| `palette.png` | `MapPalettes` + map x $20 | CRAM line 0, 16 x 1. `Palette_FadeInMap` fades it in |
| `tiles.png` | LZW table $22400, entry *map* | the 512 map tiles, 16 across, as VRAM $0000 gets them. Its PNG palette is a copy of `palette.png` for viewing |
| `blocks.txt` | LZW table $3F000, $0000-$43FF | 512 blocks, a line each: 16 nametable words (4 x 4 tiles, row by row), then the block's terrain word |
| `anim.txt` | the same entry, $4400-$47FF | the 32 animated-tile records, a line each, 16 words |
| `layout.tmx` | LZW table $35000, entry *map* | the 64 x 64 layout, for Tiled |
| `blocks.png` | — | *view*: the 512 blocks drawn 32 x 32, 16 across: the Tiled tileset |
| `radar.png` | `RadarImages` + map x $800 | the order menu's map radar (its mode 1), 64 x 64: 8 x 8 tiles in rows, in the map's palette. The menu draws it with CRAM line 0 |
| `demo.json` | `DemoData` + map x $800 | the attract demo on this map: the units it starts with and both pads' recordings (below) |

`maps/path_hints.png` is a *view* too: the arrows the path-hint layers
are painted with.

**Block words** (`blocks.txt`, `v_map_blocks`): bits 0-8 the tile, b11
h-flip, b12 v-flip. The other bits are the game's, not the VDP's: the
drawer (`Map_DrawBlockRect`) keeps only `& $19FF` and sets priority itself
on tiles below $40. b15 blocks ground units; the ST-57U instead needs b9
on every tile under it, and `Mech_GetTerrain` reads b9 too (very likely
water); b13 is a game flag. Blocks the map doesn't use are $FExx / $FFFF.

**Terrain word** (`v_block_terrain`, last on each line): the low byte is
the type the code tests: 2 outpost, 3 HQ (`Map_ScanOutposts`), 6 and 8
(`Mech_GetTerrain`, maps 0-1). The rest isn't read yet (`Obj_TerrainEvent`
isn't ported). $FFFF on unused blocks.

**Animated tiles** (`anim.txt`, read in `Map_AnimTilesUpdate`, $5738):
word 0 is the map tile that animates (0: the record is off); words 0, 1,
2, ... are the tiles it shows in turn (low 9 bits), until a word whose
low 9 bits are 0, or the 16th. One step every 6 frames (when
`v_unit_ai_group` is 0 or 6). A step copies the named tile's picture, as
VRAM holds it at that moment, into word 0's slot; showing word 0 again
puts the slot's own picture back (kept when the map loads). So a tile
named in a record can itself be animated by another, and records act in
order, 0 first: VULKAN's lava records copy from $1F0-$1F5 while records 1
and 9-14 animate those. A word naming the tile already shown does
nothing, so a word repeated holds the picture for another 6 frames
(EISFREI's records). Keep word 0's bits above the low 9 clear: the code
compares the whole word when a record comes back to it, so with any of
them set the slot never gets its own picture back (ABGRUND's records
8-31 are $FFFF throughout: they never change tile, so it never matters).

**Layout** (`layout.tmx`): open it in Tiled. Three layers:

- `blocks`: the block at each place (gid = block + 1, from the `blocks`
  tileset). Every cell needs one. Tiled's flips are ignored.
- `path_p1`, `path_p2`: the path hints order $A (attack the HQ) follows,
  for each team: an arrow in one of 8 directions (up first, clockwise),
  or nothing. The red arrows (gids 513-520) and the blue (521-528) mean
  the same; colour only tells the layers apart. A place with an arrow in
  either layer has a hint; the other layer's direction is then up unless
  painted.

The layers must stay CSV (Map Properties, Tile Layer Format) and 64 x 64.
The tilesets may stay in the map or be exported to `.tsx` files named
`blocks.tsx` and `path_hints.tsx`. The layout word the game gets is the
block in bits 0-8, b9 if there is a hint, P1's direction in bits 13-15 and
P2's in bits 10-12; `Map_ScanOutposts` then writes each outpost's index
into bits 10-13 of its own word in RAM.

To change what a block looks like, edit `tiles.png` and `blocks.txt`, then
`make views` so Tiled shows it.

**Demo** (`demo.json`): what the attract mode plays on this map
(`data/demos.json` says when). A demo is a game with both players human,
both pads played back from the recording; the random numbers start from
the same seed each time, so the game comes out the same.

| Field | What |
|---|---|
| `units` | the units on the map when the demo starts, up to 32, each as in `start_units.json` (`flags` b7 set, b4 team 2; `type`, `order`, `dir`, `x`, `y`). Both teams' are here: a demo gives no computer player its starting units |
| `p1`, `p2` | each pad's recording, a run a string: the number of frames, a space, then the buttons held as letters (`U` `D` `L` `R` up, down, left, right; `A` `B` `C`; `S` Start), or `-` for none: `"44 UL"` is up and left for 44 frames |

The demo ends when either recording runs out (P1's is looked at first,
and once it has P2's isn't played any more) or after 3,600 frames (60
s), whichever comes first; the shipping recordings are 3,588-3,598 frames.
Start ends it at once (in the game it would pause, so a recording never
holds it). The ROM keeps a run as 2 bytes, buttons then frames, and a
pad's recording in $380 bytes ending with a run of 0 frames, so a run
longer than 255 frames is kept as several (the loader splits it), and a
recording can hold 447 of those at most. The recording only replays
right while the game it was made with comes out the same: change the
map, the units or the game's rules and the mechs act on presses meant for
another situation.

## tiles/

Sprite art laid out as the pictures it makes. Each sheet's JSON says
where every group of tiles is in the PNG; the loader reads the tiles back
from those places into the order VRAM holds them. A VDP sprite's tiles run
down its first column, then the next: the loader does that, so a picture
in the PNG looks the way it does on screen.

| File | ROM | VRAM | PNG palette (a copy) |
|---|---|---|---|
| `mech.png` / `.json` | `MechFrames` | $4000 / $4200, one frame at a time | line 2 (red) |
| `sprites.png` / `.json` | `SpriteTiles`, `RadarBlank` | tiles $240-$4FF | line 2 (red) |
| `hud.png` / `.json` | `HUDTiles` | tiles $500-$5FF | line 1 |
| `font.png` | `FontTiles` | tiles $6B1-$6FF | line 1 |
| `order_icons.png` / `.json` | `HUDIcons` | copied one at a time to tiles $6A1 / $6A5 (view B $6A9 / $6AD) by the order menu | line 1 |

`font.png` is a plain tile sheet: 79 tiles, 16 across, in order. Colour
0 is transparent in all of these (the PNGs mark it so for viewing).

A sheet's JSON:

```
{
  "image": "sprites.png",
  "first_tile": "$240",       VRAM tile number of the sheet's first tile
  "tiles": 704,               how many tiles the sheet has
  "groups": [
    {"name": "AMR-51D: headings 0-4", "tile": "$240", "w": 3, "h": 3,
     "count": 5, "across": 5, "x": 0, "y": 0}, ...
  ]
}
```

A group is `count` pictures of `w` x `h` tiles, starting at tile `tile`,
drawn `across` to a row from (`x`, `y`) in the PNG. Its tiles run down
each column of a picture unless `"order": "rows"` (the blank radar).
Groups of 1 x 1 are single tiles, 16 to a row; those named "not
identified" are tiles nothing is known to draw yet. Every
tile of the sheet must be in exactly one group. Moving a group in the PNG
and changing its `x`, `y` to match changes nothing in the game.

What the groups are, from the code that draws them:

- `sprites`: each unit type's five headings 0-4 (the other eleven are
  these flipped: see `sprites` in `data/units.json`); the TAX-52 and
  ST-57U turrets; FWA; INFANTRY's 16 headings in two steps; INFANTRY dying
  (frame 15 - HP); the blank radar, 8 x 8 tiles (the order menu's radar
  starts from it in mode 0, units round the mech); the order menu's
  pictures of INFANTRY, TAX-52, ST-57U and FWA, $3A8-$3CB (the other
  types' pictures are their own sprite facing up, or their turret's:
  `UnitMenuTiles`, a code table). $3CC-$3EB: 32 blank tiles (all colour
  0) that nothing draws. INFANTRY's dying frame 15 ($4BC-$4BF) is never
  drawn either: HP 0 frees the unit first.
- `order_icons`: the 16 orders' icons as the order menu shows them, by
  order number (`OrderInfo`'s order; orders 0, 4, 6, 7, 8 and 12-14 have
  no picture; 11, a nameless order nothing gives, has 15's).
- `hud`: the divider between the split screen's views, the explosion's
  six frames, the jet's shadow, a unit's no-fuel/no-ammo marker, the
  outpost hold bars (P1, P2), the HQ-hit warning, unit building / ready,
  the missile warning arrow; the HUD bar cells (below). The single tiles
  include the HUD strip's pieces (see `screens/hud_frame.txt`).

The HUD bar cells, $5B0-$5BE, are what `HUD_QueueBar` builds a bar from
(the HP, HQ, ammo and fuel bars in the HUD strip, the carried unit's in
the order menu). A cell is four quarters, filled from the left; on line 1
the value's quarters are cyan, the rest of the bar's maximum dark red,
and past the maximum black. $5B0 + n: n quarters filled of a whole cell
($5B0 empty, $5B4 full). $5BE: past the maximum. The cell where the
maximum ends inside it (bars whose maximum isn't a multiple of 4) comes
from `GaugeTiles` (a code table, `src/game/tables.c`), by the value's
quarters and the maximum's: $5B7/$5B6/$5B5 none filled of 1/2/3,
$5BA/$5B9/$5B8 one filled of 1/2/3, $5BC/$5BB two of 2/3, $5BD three of
3.
- `mech`: the 64 frames: $00-$0F robot standing (a heading each),
  $10-$1F firing, $20-$2F walking, $30-$34 jet (flips give the rest),
  $35-$39 jet carrying, $3A-$3F transforming.

## palettes/

`game.png`: `GamePalettes`, CRAM lines 1-3 (16 x 3): line 1 the HUD and
font, line 2 the red team (P1), line 3 the blue team (P2).

`cycles.png`: `ColourCycles` ($58E6), 16 x 2: the colours CRAM line 0's
colour 2 (row 0) and colour $B (row 1) step through on every map, one
step every 6 frames (`Map_AnimTilesUpload`), from the second colour on.
The shipping cycles run red to yellow and back, and blue to cyan and
back; each map's `palette.png` has such colours in those two places,
which show only until the first step. Which two CRAM colours are cycled
is code (`ColourCycleCRAM` in `src/game/tables.c`).

`menu.png`: `MenuPalette` ($73E00), CRAM lines 0-3 of the options screen.

`title.png`: the title's two lines (16 x 2): row 0 `SegaLogoPalette`
($5348E), CRAM line 0 while the SEGA logo shows; row 1 `CopyrightPalette`
($5351C), what CRAM line 3 fades in to under the copyright line.

`sega_logo_cycle.png`: `SegaLogoCycle` ($534AE), 31 colours in a row: the
SEGA logo's sweep. Every 4th frame line 0's colours 2-12 take 11 of them,
from colour 20 back to colour 0 a step at a time, then stay.

`copyright_cycle.png`: `CopyrightCycle` ($534EE), 23 colours in a row: the
copyright line's. Every 4th frame line 3's colours 6-14 take 9 of them,
from colour 14 back to 0, and round.

`winner.png`: `WinnerPalette` ($5439A), CRAM lines 0-2 (16 x 3) of the
game's end (WINNER and the results): the big font's colours, line 0 red
(the labels), line 1 white (the figures), line 2 yellow to white (WINNER,
the sides).

`ending_text.png`: `EndingTextPalette` ($53688), one line: CRAM line 0
while the endings' story scrolls (colour 15 the text, 1 its shadow), and
line 2, ENDE's colours, at the end.

## title/

The title's and options screen's graphics, from the menu module's RLE
entries.

`tank_photo.png`: `RLE_TankPhotos` entry 0, 320 x 224: the photo behind the
title, 1,120 tiles in reading order (plane B from tile 0, palette line 1).
Its PNG palette is its CRAM line (`TankPhotoPalette`, $68400): the loader
reads the colours from it.

`title_logo.png`: `RLE_MenuGfx` entry 2, "Herzog zwei", 29 x 11 tiles in
reading order (tiles $580 up). Its PNG palette is the logo's CRAM line 0;
the ROM entry keeps it as a 320th tile after the picture, which is where
the loader puts it back.

`sega_logo.png` + `.json`: `RLE_MenuGfx` entry 1's first 48 tiles (VRAM
$480-$4AF), the SEGA logo, 12 x 4 tiles in rows. A sprite-sheet layout, as
`tiles/`; its palette is a copy of `palettes/title.png` row 0.

`copyright.png` + `.json`: the rest of entry 1 ($4B0-$4FF): the copyright
line, two rows of tiles each, as the title draws them at (8, 22) and
(8, 23): an overseas machine's ("Techno Soft") on top, a Japanese one's
("Tecno Soft") below. Palette: a copy of `palettes/title.png` row 1.

`menu_font.png`: `RLE_MenuGfx` entry 0, 256 tiles, 16 across: the options
screen's font (VRAM tiles 0-$FF). A character's tile is its code - $30
plus a set's base: $00 white (with the frame pieces, `|\~]^=>?}_` and $7F,
and `` ` `` the background), $50 the cursor's red, $A0 the grey of maps and
difficulties won, $D0 the headings' (CONFIGURATION MODE). The PNG palette
is a copy of `palettes/menu.png` line 0.

`winner_font.png`: `RLE_WinnerFont`, 256 tiles, 16 across: the game end's
2 x 2 font (VRAM tiles $580-$67F). A character c is the four tiles from
$580 + (c - $30) x 4: top-left, bottom-left, top-right, bottom-right. The
PNG palette is a copy of `palettes/winner.png` line 2.

`tank_photo2.png`: `RLE_TankPhotos` entry 1, 320 x 224: the blue tank photo
under the results, as `tank_photo.png`; its PNG palette is the CRAM line
it fades in to (`WinnerPhotoPalette`, $543FA), which the loader reads.

## ending/

The endings (`Ending_Run`), shown after the results once every map is won
on every difficulty: the story over a picture, the staff roll over the
forest painting, then ENDE.

`still_life.png`, `grave.png`: `RLE_EndingPictures` entries 0 and 1, 320 x
224 (1,120 tiles in reading order, window cells from tile $100, palette
line 3): the story's picture, the still life with a letter unless the
human played P2 against the computer, the grave if so. Each PNG palette is
its CRAM line (`EndingPaletteStillLife`, `EndingPaletteGrave`), which the
loader reads.

`forest.png`: the LZW table at $57800, entry 0, 320 x 224 as the pictures:
the forest painting behind the staff roll and ENDE. Its PNG palette is
its CRAM line (`ForestPalette`, $5C3E0).

`story_font.png`: `RLE_MenuGfx` entry 3, 80 tiles, 16 across: the endings'
font (VRAM tiles 0-$4F). A character code c is tile c - $30; the
punctuation sits on other codes (`:` draws a full stop, `;` a comma, `<`
an apostrophe, `=` a quotation mark, `>` an exclamation mark) and `@` is
the blank. Colour 15 is the text, 1 its shadow. The PNG palette is a copy
of `palettes/ending_text.png`.

`ende.png`: `RLE_MenuGfx` entry 4, 128 x 32: ENDE, 16 x 4 tiles in
reading order (VRAM tiles $80-$BF), drawn on plane B at (12, 12) in
palette line 2. The PNG palette is a copy of `palettes/ending_text.png`.

`story_p1.txt`, `story_p2.txt`: `EndingText1` and `EndingText2`, the story
(22 lines of up to 38 characters) unless and when the human played P2
against the computer. Written as the font draws them: `.` `,` `'` `"` `!`
and spaces; the loader turns them back into the font's codes and pads each
line with blanks. `{XX}` is any other code. Each line takes two rows of
the scroll; an empty line is a gap.

`staff.txt`: `StaffText` and the last line: a line each, its kind and 16
characters between bars (fewer are padded with blanks). `heading` lines
show red, `name` lines yellow; the roll starts with a heading; a blank row
comes before the first name after a heading, and 17 before the first
heading after a name (in the ROM, a byte 1 or 2 between the lines). Then
the maker's name, last of all: `overseas` on an overseas machine, `japan`
on a Japanese one (`StaffCopyrightOverseas`, `StaffCopyrightJapan`).

## screens/

`hud_frame.txt`: `HUDFrameTiles`, the HUD strip: window rows 0-3, 40
nametable words a row (tile in bits 0-10, $800 h-flip, $1000 v-flip,
$6000 palette line, $8000 priority). `Screen_InitGame` writes it.
`hud_frame.png` is a *view* of it.

## data/

**`units.json`**: `UnitStats` ($20 bytes a type, types 0-13), with the
sprites from `UnitDirTable`; `mech`, the player mech's record
(`UnitStats_KIDOU`); `explosion_frames` (`ExplosionFrames`).

| Field | Offset | What |
|---|---|---|
| `name` | +$00 | 8 characters in the game's font codes: `_` shows as `-`, `@` as `0`, `{XX}` is a byte that isn't a printable character (`{B8}` 1 ... `{BE}` 7) |
| `price` | +$08 | |
| `hp` | +$0A | |
| `size` | +$0C | pixels square, for collisions |
| `ammo` | +$0D | |
| `fuel` | +$0E | |
| `sprite_size` | +$10 | the VDP's size bits: (width - 1) x 4 + (height - 1), in tiles |
| `speed` | +$11 | |
| `overlay` | +$12 | b7: a turret drawn over it, of type `overlay & $7F` |
| `fuel_use` | +$13 | taken from its fuel each move |
| `shot_speed`, `shot_life`, `fire_delay` | +$14-$16 | its gun |
| `power` | +$17 | |
| `sight` | +$18 | rings of 32-pixel grid cells it looks at |
| `build_time` | +$19 | x 128 frames |
| `missiles`, `missile_speed`, `missile_life`, `missile_delay` | +$1A-$1D | |
| `orders` | +$1E | a bit per order it can be given |

`sprites` (types 0-9): the sprite for each of the 16 headings (0 up, 4
right, clockwise): `"$tile"`, then `h` / `v` / `hv` for the flips, and
`palN`, `pri` when those bits are set. The team's palette line is added
when the game draws it. INFANTRY (type 8) has a row here, but the game
draws it from tile $400 + heading x 4 instead.

`mech` adds `max_speed` and `fuel_use_by_form` for each form (robot, jet,
jet carrying a unit; +$20-$25), and `byte_26`, `byte_27`, which nothing
read so far uses.

Types: 0 AMR-51D, 1 TAX-52, 2 SAM-42, 3 ST-57U, 4 SUPPLY, 5 GRM-34A,
6 and 7 "NOT USE" (the TAX-52 and ST-57U turrets; with the TURRETS
patch, `turrets.json`'s records), 8 INFANTRY, 9 FWA, 10-13 empty (with
the REPAIR patch 10 and 11 are `repair.json`'s truck and mechanic).

**`turrets.json`** (the port's own, no ROM behind it; session 60): the
records types 6 and 7 take with the PATCHES page's TURRETS, which sells
them as TURRET-1 and TURRET-2 (`docs/turrets.md`). `turrets`: two records
as `units.json`'s, `type` 6 then 7, without `sprites` (UnitDirTable's
turret rows draw them, the whole sprite turning: keep `overlay` $00).
Keep `fuel` above 0 (at 0 the out-of-supplies marker shows); `orders` is
what the menu offers, $0002 (stand), as the turret's AI stands whatever
its order. Optional: absent, the patch sells nothing. `make assets`
leaves it alone.

**`repair.json`** (the port's own, no ROM behind it; session 72): the
PATCHES page's REPAIR, which sells a repair truck whose two mechanics
mend the vehicles round it (`docs/repair.md`, where every value is
explained). `truck` and `mechanic`: a record each as `units.json`'s,
`type` 10 and 11, without `sprites` (the SUPPLY's and the INFANTRY's
draw them). The truck's `ammo` is its crew, 4 a man; its `orders` $4000
is order 14, whose name, fee and icon are `order`'s (`icon`: 16 rows of
16 characters, a hex digit a pixel's colour of CRAM line 1, `.` none).
`sprite_size`'s high digit names the colour table the sprite is drawn
through: 1 `truck_colours`, 2 `mechanic_colours` (16 numbers each: colour
c of the tiles becomes entry c, in the same palette line; 0 isn't drawn).
`search`, `park`, `reach`, `danger` (px), `calm` (frames) and `rate` (HP)
are how they work. Keep the mechanic's `ammo` and both `fuel`s above 0
(the out-of-supplies marker). Optional: absent, the patch sells nothing.
`make assets` leaves it alone.

**`start_units.json`**: `StartUnits`, the units a computer player starts
with against a human, per map: `p1_cpu` when P1 is the computer, `p2_cpu`
when P2 is (`Units_SpawnCPUStart`; none when both are computers).

| Field | What |
|---|---|
| `flags` | b7 set (the record is there), b4 team 2. b6 and b0 are left over from the development tool that saved the records ($C0/$D0; $81/$91 on the ST-57U, which gets b0 anyway); `Unit_Spawn` copies the flags into the unit as they are |
| `type`, `order`, `dir` | unit type, order (as the order menu numbers them), heading 0-15 |
| `x`, `y` | pixels |

A GRM-34A record (type 5) becomes `cpu_start_types[difficulty]` with
order 5, unless that type is GRM-34A too (`CPUStartTypeTable`: TYPE A
AMR-51D, B SAM-42, C TAX-52, D GRM-34A). Up to 32 records a half.

**`order_menu.json`**: the order menu's data.

| Field | What |
|---|---|
| `orders` | `OrderInfo` ($5DE20, 16 bytes an order): `name`, 10 font codes as `units.json`'s names (`_` is the font's dash, `{B8}`-`{BF}` the cyan digits 1-8), and `fee`, what the order adds to a unit's price. Prices, fees and money are in tens on screen (the menu puts a fixed 0 after them) |
| `buyable` | `UnitAvailByMap` ($5DE00, a word per map): per map the unit types the menu offers, as a list of type numbers |

A unit type can only take the orders its `orders` bits in `units.json`
allow; the menu skips the rest.

**`menu.json`**: the options screen's text and numbers.

| Field | What |
|---|---|
| `songs` | per song (0-17): `title`, the MUSIC test's two lines (`MusicTitles`, $558CA), 19 characters each in the menu font, a space for its blank; and `repeat`, whether the test plays it repeating (`MusicTestRepeat`, $55862) |
| `map_names` | the map select's names (`MapNames`, $55EE0), 7 characters each |
| `sfx_count` | how many effects the SOUND test offers (`M_SfxCount`, $57422) |

**`demos.json`**: `DemoTable`, the attract mode's 16 demos. When the
title runs out (its song ends without Start), the next one plays, then
the title again; the place in the list is kept while the game runs (on
the console a reset keeps it too, a power-on starts from the first), so
they come in turn. Start during a demo, or during its map's loading
screen, goes back to the title. Each entry: `map` (whose `demo.json` plays) and
`screen`: `"split"` (both views), `"wide P1"` or `"wide P2"` (one view,
that player's). The shipping list plays every map twice, split and wide.

**`sound.json`**: `SfxPriority`, a byte per sound effect: when two are
asked for in one frame the lower value wins. Effects past the list are
$FF, unused.

## sound/

**`sounds.txt`, `songs/`, `effects.txt`** (session 49): `SoundData`
($78000-$7FFFF), the 32 KB the Z80 driver's bank window shows at
$8000-$FFFF, as text: `sounds.txt` lays it out (the song table, the song
files in order, where the effects go, the bytes after them nothing reads);
`songs/NN_title.txt` is a song (its header, its eight tracks' order lists,
its phrases of named notes, its FM and PSG instruments as named fields,
each instrument with a name its tracks pick it by, session 50:
`docs/instrument_names.md`);
`effects.txt` the 29 sound effects. The game builds the bytes from them at
start-up (`src/audio/songtext.c`), so nothing points anywhere by hand: a
phrase can grow, and what comes after it moves. Songs that outgrow the
32 KB each get a 32 KB window of their own. `songs/NN_title.trk` is each
song again as a tracker (every channel side by side in rows of ticks, the
notes as heard), which the game reads in the text's place. The formats,
what each command does and the room: `docs/sound.md`.

**`q_drums.json`** (the port's own, no ROM behind it; session 37): with
the AUDIO page's SYNTH on Q, which of each song's FM instruments are drums
and which drum each plays -- PS2's own (kick, snare, the three toms, the
hats) or a model of its own -- by song, instrument (by its name since
session 50, `"instrument": "kick_toms"`, as the song's `fm_instrument 8
kick_toms` calls it; a number still does), the note's pitch range and, if
needed, a part of the song (the track's order-list position). An entry
naming an instrument its song doesn't have (a song mod renamed it) is
left out, and the game says so; the rest of the table holds.
The kick instruments play the kick on their kick note (since session 45 a
long one, ringing to the next note as the chip's) and toms above it,
the toms pitched to the chip's own (each entry's `pitch`: the chip sounds
these instruments at half the note's frequency) and trimmed to its level
(the entry's `level_db`, on its model's); the title's song (0) and the
endings' and staff roll's (15-17) play tuned snares there instead, as the
chip sounds them (sessions 40 and 45). (Session 46) A model's `follow`
(0.5 for these patches) plays its tone at the chip channel's own pitch
and follows the driver's slide down after the note's key-off, at the
song's tempo, never under `follow_floor_hz`: the long kick, and the
snare's boom in the songs whose snare booms on the chip (3, 6, 8, 15-17:
the model `booming snare`, PS2's snare `with` the `snare boom`). `level_db` per drum sets its
level; each song's kick, snare and hats are at the chip's level by their
entries' `level_db` (session 39: K-weighted over each hit,
`tools/hzq_level.py`). (Session 41) `voice_level_db` (top level) puts Q's PM voices at the
chip's level against the drums (2 dB: the bank's trim leaves them under),
and entries with the model `"voice"` set one instrument's own on top of
it (`level_db`). Mapped: the title's song (0), song 1, the map songs
(3-10) and songs 11, 13-17 (2 and 12 have no drums). Absent, Q plays every instrument as its
own voicing. The fields and how a note finds its drum: `docs/q.md`, "The
drums". `hz_qdrums survey` and `strike` (the Makefile's `hz_qdrums`, with
Q) find a song's instruments and play them alone.
