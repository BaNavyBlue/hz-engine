# Hackable, as PS2 is (the plan, 2026-11-25)

The owner, 2026-11-25, on finding the port still drawing through a model
of the VDP: "did you miss the memo that this should follow in the footsteps
of phantasy star 2?", then: "PS2 is almost fully hackable, I think it's
time for you to start focusing on that and not make assumptions about what
counts as recreating sega architecture. Assets are converted into something
hackable like the Mod folder for PS2."

So the rule from here on: **where PS2 has left the hardware's way, this
port leaves it too, without asking whether the hardware detail "changes
what is seen".** Session 25's note ("model hardware only where it changes
what is seen or heard; the VDP model stays as the renderer") is replaced by
this page. Before designing anything here, look at how PS2 does it
(`../phantasy-star-2/ps2`, its `docs/`).

## Where PS2 is

- **Video:** `src/core/video.h`, "a semantic layer, not a VDP emulation.
  There are no VRAM addresses, no control-port write pairs and no DMA
  queue: ported game code says what it wants drawn and the compositor
  works out the pixels." Planes of cells, a sprite list, a scroll per
  layer, palette lines; fades and flashes as calls. Since its build 140
  every frame is composed as colour beside the index plane.
- **Art:** `docs/asset_format.md`, "a file is what you see": a picture is
  a full-colour PNG (RGBA, transparent where nothing is drawn) of exactly
  what the screen shows, with a JSON description a thing (size, anchor,
  frames keyed by the numbers the game uses). Layout A:
  `assets/art/<kind>/<thing>/<thing>.json` and `<frame>.png`; HD pictures
  under the same names in `assets/art_hd/`. No palette, no tile sheet to
  assemble, no VRAM tile numbers. Colour cycles are glow masks
  (`<frame>_slots.png`). Every kind proved equal to the old path on every
  route before its old loader is deleted.
- **Mods:** `docs/mods.md`: any file the game reads, copied to the same
  place under `mods/` and edited; refused mods set aside a file at a time.
  The packaged game writes `mods/originals/`.
- The owner's answers there that carry over here unasked: HD is a bonus
  set laid over the native one, a file at a time, 4x by default; layout A;
  glow masks for colour cycles; no HD stand-ins (the HD check waits for
  painted art); HD drawn without the CRT model, in HDR where there is one.

## Where this port is

Game logic is readable C since session 48 (named fields, no 68k RAM
addresses), and the mods folder is PS2's (session 49). What is still the
hardware's way:

1. **Video is a VDP model** (`src/core/hz_vdp.c`, decided in session 10):
   64 KB of VRAM, CRAM, VSRAM, registers, 68k-style 32-bit VDP commands,
   DMA "by its result", the sprite table walked by link with the 20 a line
   and 320 dots a line limits, raster writes for HBlank. Eight game files
   talk to it as the 68k talked to the chip: `screen.c`, `load.c`,
   `front.c`, `sprites.c`, `game.c`, `game_over.c`, `mapfx.c`, `anim.c`.
2. **The split screen is built from chip tricks**: the window plane over
   plane A plus a fixed H-scroll offset on plane B, the divider as six
   sprites, the HUD strip kept clear of game sprites by cutting sprite 5's
   link until a line-30 HBlank puts it back.
3. **The sprite pipeline is the cartridge's**: a 298-entry list in VDP
   coordinates (screen + $80), a 70-entry window chosen by the multiplexer
   each frame, link bytes, a DMA from the list's 68k address (`0xFF8064`),
   the mech's 16 tiles DMA'd from its ROM address when its frame changes.
4. **The scroll code streams VDP commands** kept in RAM queues
   (`HzScrollQueue`).
5. **The frame model is the 68k's**: code blocks in `game_wait_vblank`,
   which hands the frame over and then runs the VBlank handler;
   `interrupts_on` follows the ported `move #$2700/#$2000,sr`; the display
   off (`$8134`) means no frame passes.
6. **Art is tiles and palettes**: indexed PNGs with palette strips;
   `tiles/*.json` place groups by VRAM tile number (`"first_tile":
   "$240"`); sprite attributes in the code are VRAM words (tile, palette
   line, priority: `$A570`, `$4200`); the HUD frame is nametable words as
   text (`screens/hud_frame.txt`); a team's colour is which palette line a
   sprite is drawn with.
7. **Units' and shots' pictures are found by tile number arithmetic** in
   `sprites.c` and the tables, not by name.

**Step 1 so far (sessions 79-80)** has taken away 1 and 4, and most of 2
and 3: the game draws through the draw layer (`src/core/video.h`), which
keeps its own tiles, cells, colours and sprites; `hz_vdp.c` is a test
tool; no VDP command, DMA, sprite table or raster write is left in the
game. Still the cartridge's: the sprite list in RAM (VDP coordinates, the
70-entry window, links), read into the layer each frame, and the divider
as six sprites. Both wait on open question 2.

## The order

Each step is its own piece of work, proved before the old path goes:
every check's frames (`make check`'s shots, the demos, the title, game
over and the endings, the arena's and replays' games) drawn the new way
equal to the old way pixel for pixel. No step changes what the game does,
so no frame count, replay, network game or AI result moves (and
`NET_GAME_CODE` changes where an asset's format does, since the network
id hashes the assets, and where the display's state a split-screen
network game compares does: step 1, below).

1. **The draw layer.** PS2's `video.c` brought over (planes of cells, the
   sprite list, a scroll a layer, palette lines, the colour plane), with
   what this game needs that PS2's has not: **two views**, each a
   rectangle of the screen with its own camera onto the map and its own
   sprites, and the HUD strip and divider drawn as what they are. The
   eight files say what to draw. The sprite drawers push each sprite into
   the view it is in, in screen pixels; no list in VDP coordinates, no
   window, no links, no DMA. The cartridge's sprite cap and flicker (the
   multiplexer, 20 a line) stay only as a game rule if the owner wants
   them (PS2 keeps its software cap of 80 so), not as chip behaviour.
   The scroll code sets cells; no command queues. Proved against the VDP
   model, then `hz_vdp.c` goes (or stays a test tool, as the 68k
   interpreter does, and nothing in the game links it).
2. **The frame model.** The game steps a frame at a time as PS2's does; no
   blocking wait, no interrupt mask, no VBlank or HBlank handlers, no
   display-off register. The same frames in the same order, so the demos,
   replays and network games are unchanged.
3. **Art as pictures** (PS2's `docs/asset_format.md`, layout A, its
   principles as they stand): `assets/art/<kind>/<thing>/`, RGBA PNGs and
   a JSON description a thing, frames keyed by the numbers the game uses
   (a unit's heading, the mech's frame). Kinds: `units` (each type's
   headings, turrets, its marker), `mech` (jet, robot, shadow, debris),
   `shots`, `hud` (the frame, the bars, the alerts, the font),
   `order_menu` (icons, radar), `title`, `ending`, `screens`. Glow masks for
   the maps' colour cycle and anything else a palette effect touches. The
   maps: their block art as RGBA tile pictures, still edited in Tiled. The
   extractor (`src/extract/`) writes the new files; `make assets_check`
   proves them; the old loader goes kind by kind.
4. **HD**: `assets/art_hd/`, as PS2's, once there is painted art.

## Step 1 as it goes

Step 1 is done in two halves, so the checks against the cartridge keep
proving each move:

- **Half A (session 79): the game calls the draw layer.**
  `src/core/video.h` is the layer's interface, PS2's in shape: tiles by
  number (`video_load_tiles`, `video_clear_tiles`, `video_copy_tile`),
  the cells of three layers by (layer, column, row) (`video_set_tile`,
  `video_set_word`, `video_fill`, `video_fill_rows`), a scroll a layer in
  pixels (`video_scroll`), the colours by index (`video_set_colour(s)`),
  the screen on or off (`video_display`), and the two layouts
  (`video_start_game`, split or wide; `video_start_menus`). The layers:
  B (view B; the menus' picture), A (view A), W (the window: the HUD
  strip, view B's side of a split game, every menu screen in front). The
  game's drawing goes through it: screen.c, load.c, map.c, mapfx.c,
  anim.c, hud.c, order_menu.c, game_over.c, and the menus' front.c,
  title.c, options.c, game_end.c, ending.c. The HUD's bar queue and the
  scroll queues hold a layer and a cell, not a VDP command. Underneath,
  `src/core/video.c` still writes the VDP model, so VRAM is what it was
  and every check against the original still holds. Still on the VDP:
  the sprites (sprites.c's table and DMA, the divider's six in
  screen.c, the loading screen's two in load.c, `front_clear_sprites`),
  the split's raster writes in the HBlank and VBlank handlers (game.c),
  and the state hash's display parts.
- **Half B (session 80): the layer's own store and compositor.**
  `src/core/video.c` keeps the tiles (2,048, by number), the three
  layers' cells, a scroll a layer, the 64 colours and the frame's sprites
  itself, and `video_compose` draws the frame from them, back to front:
  B low, A or W low, sprites low, B high, A or W high, sprites high.
  Nothing in the game holds VRAM, CRAM, VSRAM or a register.
  - **Sprites in screen pixels** (`Sprite`: x, y, tile, size, palette
    line, priority, flips, colour table). `sprites_upload`
    (VDP_UploadSprites) walks the frame's 80 from the RAM list by their
    links, as the chip read them, and pushes each through
    `sprite_from_ram`. The divider's six (screen.c) and the loading
    screen's two (load.c) are pushed as the screen's own, ahead of the
    game's (`video_sprites_game`).
  - **The HUD strip as what it is:** VBlank holds the game's sprites back
    (`video_sprites_hold`), HBlank lets them back from line 32
    (`video_sprites_back`), and the layer draws none of them on the
    strip's lines. No link is cut in a table, no raster write is kept.
  - **The rest:** the mechs' frames are tiles loaded by number
    (`video_load_tiles` at $200 and $210), not a DMA from the ROM. UNITS'
    crowd and the repair truck's colour tables (sessions 61 and 72) are
    the layer's (`video_sprites_behind`, `video_sprite_colours`).
  - **The cartridge's limits, as the layer's rule** until open question 2
    is answered: 80 sprites a frame, 20 and 320 of their pixels a line,
    and the mask (a sprite at x -128 ends its line once another has been
    drawn on it).
  - **`src/core/hz_vdp.c` is `tools/hz_vdp.c`:** only the checks link it.
- **What is left of step 1** (open question 2 first): the sprite list
  kept in screen pixels and pushed into the view it is in, with no RAM
  list in VDP coordinates, no 70-entry window and no links; the divider
  drawn as a divider, not six sprites in the line counts.

**What the cartridge's VRAM let share** (in half A, tile numbers whose
VRAM the planes or tables also used) and where half B put it:
- the order menu's radar: its 64 tiles (`radar_tile_at`) are cells of
  the layers nothing shows while the menu is open (split view A's in
  layer A's columns 32-63; the others in layer B's rows 24-31 and the
  window's column 48 on). Kept as a QUIRK (`order_menu.c`, `clear_rows`):
  on closing, the cartridge's fill of half a layer's cells also clears
  the radar's tiles there, so for one frame the split view A radar's top
  rows go blank before the second fill clears their cells. The layer
  does the same by asking for it (`video_clear_tiles` on the rows'
  tiles); the first proof run found the frame (seed 1, frame 305);
- the order menu's icons at tiles $6A1-$6B0 (VRAM $D420-$D5FF, beside
  the H scroll table, which uses 4 bytes of it): tiles of their own now;
- Title_Run's clear, $3C0 tiles from $460 (VRAM $8C00 to the end and round
  to $03FF): through the menus' sprite table, scroll table and both layers
  on its way to tiles 0-$1F. Now it clears tiles only; the screen is off,
  and the planes and scroll are set before it comes back on;
- Screen_InitGame's window fill, which in a split game starts in layer
  B's row 23 (where the 68k's VDP address was left) and runs on 1,490
  cells into the window: clear cells either way, so nothing shows it.

**Checks changed:** `tools/hzanim_check.py` leaves register 15 out (the
layer writes no data port, so there is no auto-increment to leave at 2;
register 1, the screen off, is still compared); `tools/hz_hud.c` and
`tools/hz_ordermenu.c` turn a bar's layer and cell back into the 68k's
VDP command for the comparison. **The proof across the change:**
`hz_netcheck --seen` (the game, the screen's pixels and the sound each
frame, not how the screen is held) over 32 seeds of 200,000 frames, the
build before half A and the build after compared.

Half B's checks against the cartridge go through a watcher
(`tools/video_vdp.c`): the layer tells it each change (`video_watch`;
the game never sets one) and it lays them out in a `Vdp` as the
cartridge's VRAM, CRAM, VSRAM and registers, and the tools that compare
memory with the 68k interpreter's read that (hz_demo, hz_front,
hz_gameend, hz_ordermenu, hz_hud, hz_anim, hz_loop, hz_unitmove,
hz_shot). What the layer does not keep, the sprite table, is left out of
the VRAM both sides compare (`tools/hzlayer.py`: a byte the 68k wrote
into the table in use counts as 0), and the demo's, the front's and the
game end's frames compare the sprites as drawn instead: each
shown sprite's place, size, cell word, colour table and first line
(`video_sprites_crc` and `hzlayer.sprites_crc`). `hz_netcheck --frame N
FILE` writes a frame's pixels and colours, to find where two builds'
frames part. **The proof:** the same 32 seeds of 200,000 frames (29.6
hours of play), the half A build and half B's, `--seen`: all 32 the
same.

**The network id:** a split-screen network game exchanges the display's
state (`statehash.c` parts 6-9: VRAM, CRAM, VSRAM, registers). Half A
already changes it: VRAM, CRAM and VSRAM hash as before, and registers
0-18 hold what they did every frame, but the DMA registers (19-23) no
longer hold what the old uploads left (the DMA fill of VDP_Init, the
loading screen's mech frames), until a game's first sprite DMA sets them
alike (seed 1: 258 of its first 3,001 frames differ, all in those five).
Nothing shows them. So `NET_GAME_CODE` is 13 from half A on (a build of
12 refuses one of 13 instead of drifting from it), and the server's AI
and the Windows download change together when step 1 goes out. Half B
changes the display's hash again, under the same 13 (neither half went
out alone): parts 6-9 are now the layer's tiles and cells, its colours,
its scroll, layout and screen on or off, and its sprites (the frame's,
the HUD strip's hold, UNITS' crowd, the colour tables).

## Open for the owner

1. **Team colours.** On the cartridge one picture serves both sides,
   drawn with the red line (P1) or the blue (P2). As full-colour pictures:
   (a) one picture a side, both written by the extractor (simplest to
   paint: a file is what you see); or (b) one picture and a team mask
   beside it, a glow mask whose pixels take the side's colours (one file
   to paint, the sides kept alike). Needed by step 3.
2. **The cartridge's sprite cap and flicker** (the 70-sprite window and 20
   sprites a line): kept as a rule, off by default, or dropped. Needed by
   step 1; until answered, step 1 keeps them as a rule so nothing seen
   changes.
3. **The maps**: block art as tile pictures edited in Tiled (as now, in
   colour), or each map as one big picture with its collision kept in the
   Tiled map (PS2's open question 1 for its world maps).
