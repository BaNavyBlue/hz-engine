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
game. Session 81 took the rest of 2 and 3 (open question 2 answered, the
cap and flicker dropped): the sprite list is in screen pixels, every
sprite is drawn, the multiplexer, its window and the links are gone, and
the divider is the layer's. So step 1 is done.

**Step 2 (sessions 80-81)** has taken away 5: the game runs a frame at a
time (`game_tick`), the title-to-title loop is the game's and tells the
host where its own work goes (`game_begin_main`), and no interrupt
handler, mask or display register is left, only what they come to in a
frame.

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
- **What is left of step 1** (open question 2 answered in session 81: the
  cap and flicker dropped, no setting): the sprite list
  kept in screen pixels and pushed into the view it is in, with no RAM
  list in VDP coordinates, no 70-entry window and no links; the divider
  drawn as a divider, not six sprites in the line counts.
- **The plan for it (session 81)**, the cap and flicker dropped:
  1. *The layer draws every sprite pushed*: no 80 a frame, no 20 or 320
     dots a line, no x -128 mask. Its list grows as it needs.
  2. *The game keeps its frame's sprites in screen pixels* (`Sprite`),
     built by the drawers in the cartridge's order (the mechs' by P2's
     form, as the links ordered them; then every list entry), handed to
     the layer after the frame as now. Sprites_Multiplex, its table, the
     70-entry window, the links and v_sprite5_link go. What is the
     game's own stays: the list's count (past 256 a shot isn't on the
     screen, `SHOT_ON_SCREEN`), UNITS' crowd behind, the HUD strip's hold
     (the layer's), the loading screen's and the radar's sprites where
     the checks compare them with the 68k's RAM.
  3. *The divider as a divider*: the split layout's column at x 144 from
     line 32, in front of the sprites, drawn by the layer from its tiles;
     no sprites 0-5.
  4. *The checks*: the cartridge's side becomes every sprite its game
     asked for (the 68k's RAM: the fixed sprites by their links, then
     the whole list), drawn without the cartridge's limits, against the
     port's. Across builds the netcheck compares the game, the sound,
     the planes and the sprites asked for (a mode both builds get), since
     the screen now differs wherever the cartridge flickered.
  5. `NET_GAME_CODE` 14 (the layer's sprites hash otherwise), so the
     server's AI and the download change together when it goes out.
- **Done (session 81)**, as planned:
  - `video.c`: the frame's sprites on the heap, as many as pushed, each
    drawn on every line it covers (`line_sprites`); `VIDEO_SPRITES`,
    `VIDEO_LINE_SPRITES`, `VIDEO_LINE_DOTS` and the x -128 mask gone. The
    divider is `video_divider` (x 144 from line 32, a 32 x 32 picture of
    tile $500's 4 x 4 on, line 1, in front of every sprite), set by
    Screen_InitGame in a split game; the loading screen and the frames
    push no divider.
  - `sprites.c`: `v_sprite_list` holds `Sprite`s in screen pixels, each
    made by `list_set` / `list_add` from the drawers' cartridge words
    (`sprite_from_ram`: 9 bits less 128); `sprites_multiplex`,
    `sprite_window`, `sprite_mux_phase`, `sprite5_link(_next)` and the
    link bytes gone; the order of the mechs' fixed sprites (6-9) is
    `sprite_order`, walked from the ROM's `SpriteLinksP2Robot/Jet` as the
    links were; `sprites_upload` pushes those four, then every entry, the
    crowd behind. Sprites_InitFixed and `screen_divider` gone (sprites
    0-5 were the divider). `v_sprite_count` and its rules stay (past 256
    a shot is off the screen, past $115 no unit is drawn, and so on).
  - The checks: `hzlayer.sprites(..., listed)` walks the game's table to
    sprite $A, then the whole list as VDP_UploadSprites' DMA found it
    (`hzdemo_check.py`'s VDP reads `v_sprite_list` up to
    `v_sprite_count` as that DMA runs, and forgets it when anything else
    writes the window's place); the port's side counts the divider as
    the cartridge's six (`tools/video_vdp.c`, `drawn_at`).
    `hzview_check.py`'s model draws every sprite (its 20 a line, 320
    dots and its 70-entry error gone). `hz_netcheck --asked` (and
    `hznetcheck.py run --asked`): the state, the layer's tiles, cells,
    colours, scroll and layout, the sprites asked for (the mechs' four,
    the list, the crowd) and the sound, for builds that draw the
    sprites another way.
- **Its proof (session 81):** every check against the cartridge alike
  (the demos, the front, the game's end, the order menu, the HUD, the
  map's animation, the frame loop, the mechs, the combat, the units' moves
  and the repairs, the shots and pans, the units and their AI, the mechs'
  AI, the sound), `rumble`, `replay_check`, `netplay_check` and
  `roundtrip`. The demos' lists are short (31 entries at most; the
  check's side read the 68k's whole list on 14,384 of each 15,416 game
  frames), so `make pans` has a new run a map, the crowd (`CROWD_TOUR`:
  P1 into P2 CPU's base, P2 chasing, both mechs destroyed, the alerts on),
  whose list runs past the 70 entries the cartridge's multiplexer sent a
  frame (71-73, maps 0-2) and past the VDP's 20 sprites a line: every
  sprite is drawn, and every pixel is the view model's, which draws them
  all. `hz_netcheck --asked`, item 5's build (with `--asked`) against
  this one: 32 of 32 seeds the same (29.6 hours of play), so the game,
  the sound, the planes and every sprite the game asked for are what
  they were. The nightmare AI's 256 arena games the same as aa2698c's,
  game for game.

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

Halves A and B went out as aa2698c (session 80); the rest of step 1 waited
on open question 2, answered in session 81, and went in then (above),
under `NET_GAME_CODE` 14.

## Step 2 as it goes

**The shape, PS2's:** `game_tick` runs one frame and returns; the host
shows it, reads the pads and calls it again. A screen is a state with
phases (PS2's `ScreenId`, `TitlePhase`, `FadeState`), not a routine that
loops and waits for the VBlank. Until session 80 the game called the host
instead: every wait was `game_wait_vblank` (or the menus' `front_frame`),
which handed the frame to the host's hook (`g->frame`) and ran the VBlank
handler, so the title flow, the loading screen, a game and its end were
each one long call. Item 4 below turned it round.

**How it goes, so the checks keep proving each move.** A sequence that
runs over several frames becomes a phase machine: a `_begin`, and a
`_step` that goes on from where the last one stopped until it has made a
frame (`HZ_RUN_FRAME`, where the 68k calls VDP_WaitVBlank) or is done
(`HZ_RUN_DONE`). Until the top was done, the old function ran its steps
to the end the 68k's way (`game_run_frames`: a VDP_WaitVBlank for each
frame), so the hosts, the frames and every check against the cartridge
stayed as they were. From the leaves up:

1. the game's own: the map's fade in, the pause, the frame loop's pass
   and Game_Start (session 80);
2. the loading screen and the game over's finale (session 80);
3. the menus: the front's fades, the title, the options, the game's end,
   the endings (session 80);
4. the top: `game_tick`, and main.c, the tools and the server's AI
   turned round to call it, the frame hook gone (session 80); then
   MainLoop_Title's flow in the game, not in each host (session 81);
5. the VBlank and HBlank handlers, the interrupt mask and the display-off
   register made what they come to in a frame: work at its start, the
   HUD strip's hold, a frame not shown (session 81).

**Piece 1 (session 80):** `HzRun` and `game_run_frames` (game.h; gone
with item 4);
Palette_FadeInMap as 12 steps of 4 frames (`palette_fade_in_map_begin`,
`_step`); Game_CheckPause as `game_check_pause_begin` (the demo's Start,
or a pause beginning) and `game_pause_step` (its first frame; Start
held, pressed again, let go; the last VBlank, the frame counter put
back). Their state is in `HzGame` (`fade_in_map_frame`, `pause_phase`,
`pause_saved_frames`), outside the state the hashes cover.

**Pieces 2 and 3 (session 80):** the frame loop's pass
(`game_frame_begin`, `game_frame_step`: its start with the pads and
Game_CheckPause, a pause's frames, the rest up to its VDP_WaitVBlank,
the uploads after it; how it ended in `frame_exit`) and Game_Start
(`game_start_begin` up to the fade, `game_start_step` the fade's frames,
then interrupts on). Game_LoadMap (`game_load_map_begin`,
`game_load_map_step`): its first frame, the tiles, the screen and the
data, the map's tiles, the loading screen's frames, the last frame;
`load_abort` is set only on a frame and stays set, so from the first
test that finds it the rest are skipped. Game_Over up to the game's end
(`game_over_begin`, `game_over_step`, state in `HzGame.over`): the bars'
upload, the units', the blast's two frames, then GameOver_DestroyHQ's
shake, three loops (100, 64, 32 frames) of two frames each (the scroll's
upload, then the sprites'), the last frame; `menu_game_end` and the
display off follow. `nightmare.c`'s look ahead steps its own copy of
the game, so each copy has its own phases.

**Piece 4 (session 80), the menus:** `front_run_frames` runs a menu's
steps as `game_run_frames` does the game's, with M_WaitVBlank's frame
(`front_frame`) for each. M_WaitFrames and the fades
(`front_wait_frames_begin`, `front_fade_to_black_begin`,
`front_fade_from_black_begin`, their `_step`s); M_ClearPlanes as its
frame (none where the display is off: everywhere but the map select)
and `front_clear_planes_after`. Title_Run (`title_run_begin`,
`title_run_step`, PS2's TitlePhase): the SEGA logo's frames and its
fade, the photo's 16 frames and its fade in, 41 frames, the logo's
slide, the copyright line's fade in and its cycle, the song's end, the
fade out. Options_Run (`options_run_begin`, `options_run_step`): a page
in the parts its frames make -- what comes before a frame it starts with,
its drawing up to the first wait for the pads, then a press after each
frame (`page_X`, `X_press`: the next page, or `PAGE_WAIT`); the test
seam `front_probe` before each page, as before. Menu_TitleFlow the
title's steps, then the options'. MenuModule_GameEnd
(`menu_game_end_begin`, `_step`: WINNER and its scroll, the photo's
fade in, the results until a button, the fade out, the endings) and
Ending_Run (`ending_run_begin`, `_step`; a row of scroll
`scroll_row_begin`, `scroll_row_step`). Their state is
`HzGame.front_run` (HzFrontRun), apart from the menus' RAM (HzFront). A
loop a quit ends ends where the 68k's does, and what follows it runs as
before: a wait a quit cuts short makes no frame, and its step goes on in
the same call. (`front_run_frames` and the blocking calls went with item
4.)

**Item 4 (session 80), `game_tick`:** it runs the sequence a host began
(`game_run_begin`: the step, and whether its frames are the menus'
M_WaitVBlank or the game's VDP_WaitVBlank) until it has made a frame to
show, and returns there (`HZ_TICK_FRAME`): at the point VDP_WaitVBlank
called the host's hook, after the HBlank's work and before the frame's
end (`video_frame_done`) and the VBlank handler, which the next tick
runs first. A frame with the display off, or a menu frame after a quit,
is no frame to show (no host saw them before either), and the tick goes
on. What a host runs: `game_begin_title`, `_setup`, `_new` (the
harnesses'), `_start`, `_pass`, `_over` (the finale, MenuModule_GameEnd
and the display off as one sequence, `g->tick_menus` turning over
between them), `_game_end`, `_ending`, and hz_shot's `_frame_end` and
`_wait`. The setup after the title is a sequence too
(`game_setup_begin`, `game_setup_step`: the load's frames, then
.clear_ram). Every host is turned round: main.c (and the headless
build), the server's AI, the checks and tools, and nightmare's look
ahead on its copy. `src/game/tick.h` has their runs of a stage to its
end (`tick_title`, `tick_setup`, `tick_new`, `tick_start`, `tick_pass`,
`tick_over`, `tick_game_end`, `tick_ending`), each with the host's own
frame function; the host's code, compiled into it. The frame hook
(`g->frame`, `g->frame_user`, `HzFrameFn`) is gone, and with it the
blocking calls (menu_title_flow, game_setup_after_title, game_new,
game_start, game_frame, game_over, menu_game_end, ending_preview and
the menus' and the game's inner ones), `game_run_frames`,
`front_run_frames`, `front_frame` and `game_wait_vblank`.

**Item 4's second half (session 81), MainLoop_Title in the game:**
until now each host that runs the whole game (main.c, the netcheck, the
rumble check, the server's AI) chained the stages itself -- the title,
the setup, Game_Start, the passes, Game_Over, the title again -- with
its own work between them. Now the loop is the game's:
`game_begin_main` (game.c, `main_step`) runs it round until a quit (at
the top, after the title or after the setup) or a map past the eight
(`g->setup_result` `HZ_SETUP_NO_MAP`), and the host is told where its
work goes: `game_tick` returns `HZ_TICK_EVENT` (no frame made; a step
returns `HZ_RUN_EVENT`) with `g->event` (an `HzEvent`):

| event | where | what the hosts do there |
|---|---|---|
| `TITLE` | the loop's top, before game_title_prepare | the checks: the title's picks (left alone or played) |
| `SETUP` | the menus over (v_game_type -1: the demo) | the checks: the loading screen's frames from here |
| `GAME` | a game set up, before Game_Start | main.c: a new game's rumble, who plays the CPU, the replay begun; the server's AI: its replay; the checks: their counts, the netcheck's G line |
| `PASS` | a pass of the frame loop over, the next to come | the server's AI thinks (session 64: between frames) |
| `ENDED` | the frame loop over (`g->frame_exit`) | the replay saved, a network game's winner logged; the netcheck's X line |

Flags: `HZ_MAIN_NO_TITLE` (no title the first time round: main.c's
skipped title and the nightmare's game, the checks' first game) and
`HZ_MAIN_ONCE` (round once, then done: a network game, main.c's and the
server's AI's, with no title). tick.h's `tick_main` runs it with the
host's frame and event functions. A host now holds only what is its
own: main.c's outer loop (a network game, or the NIGHTMARE row's, after
the title quits for it) and the network's own set-up and end. Still the
hosts' to chain (they are harnesses, each holding the game at its own
points): hz_arena, hz_demo, hz_replay and the cartridge checks, through
tick.h's runs of a stage.

**Item 5 (session 81), the handlers, the mask and the display off as
what they come to in a frame** (game.c's head, load.c, front.c):

| the 68k's | now |
|---|---|
| VBlank ($4788) | `frame_start`: a game frame's start, the game's sprites held back from the HUD strip if the split runs, the frame counted |
| VBlank_Loading, the L6 stub's target | `load_screen_frame`, the loading screen's step as a frame's start work while `load_screen_on` (`frame_start_work` picks) |
| HBlank ($4760) | `hud_release`: the HUD strip let go from line 32, decided as the frame is shown |
| the interrupt mask (`move #$2700/#$2000,sr`) | `frame_work_on`: false, a frame's start work is held, owed (`frame_start_owed`) and run once when `game_frame_work_on` lets it go (so Game_Start's fade leaves the frame loop's counter at 1) |
| `v_vdp_reg1`'s b6, `m_vdp_reg1`'s b6 | `screen_on`, the game's and the menus': off, a wait for a frame makes none |

`frame_shown` (the frame to the host: none with the screen off; else the
HUD let go if the work is on) and `frame_next` (the next tick's first:
the frame done, then the next one's start work, or owed) are
VDP_WaitVBlank's two halves. What the cartridge's RAM holds and the
checks compare with the 68k's (`v_split_done`, `v_split_enable`,
`v_hud_sprite_cut`, the frame counter) stays as it is, being the game's
own state. Nothing is renamed in the 68k references: a routine's
comment still names VDP_WaitVBlank, VBlank_Loading, IntStubs_Install.

**The proof, step 2 (session 80):** after each piece, the checks against
the cartridge (they compare every frame the game makes, the menus' and
the game's, with the 68k interpreter's) and `hz_netcheck --seen` over
the same 32 seeds of 200,000 frames, step 1's build (aa2698c) and the new
one compared. On the last build, the frame hook gone: every check
against the cartridge alike (the demos, the front, the game's end and
the endings, the order menu, the HUD, the map's animation, the frame
loop, the shots and pans, the units, the mechs and their AI, the sound),
the replays and network play alike, the netcheck 32 of 32 the same,
and the nightmare AI's arena games (against the CPU and the bot, from
either seat, every map and difficulty: 256 games) the same on both
builds, game for game. Nothing a peer exchanges changes, so
`NET_GAME_CODE` stays 13.

**Session 81, item 4's second half:** the netcheck 32 of 32 the same as
aa2698c's (29.6 hours of play: the title, the demos, the loading
screens, the games and their ends, now run by `game_begin_main`); the
full netcheck (the layer's state too) the same as the build before, 6
seeds of 200,000 frames; the rumble check's 16 seeds the same output on
both builds (its games, overs, demos and every shake felt), and `make
rumble`; `replay_check` and `netplay_check` (main.c's local and network
games, the server's AI) alike; `front`, `gameend`, `loop` against the
cartridge.

**Session 81, item 5:** every check against the cartridge alike (the
demos, the front, the game's end, the order menu, the HUD, the map's
animation, the frame loop, the mechs, the combat, the units' moves and
the repairs, the shots and pans, the units and their AI, the mechs' AI,
the sound), `rumble`, `replay_check` and `netplay_check`; the netcheck
32 of 32 the same as aa2698c's (29.6 hours of play); the full netcheck
the same as item 4's build, 6 seeds of 200,000 frames; and the nightmare
AI's arena games (256) the same as aa2698c's, game for game. So step 2
is done: the game runs a frame at a time, and every frame is the one it
made before, in the same order. `NET_GAME_CODE` stays 13 for it.

## Answered by the owner (2026-11-26, session 81)

1. **Team colours: one picture and a team mask.** A unit's picture is
   painted once; beside it a mask (as the glow masks for colour cycles)
   says which pixels take the side's colours, red for P1, blue for P2, so
   the sides stay alike. (Not one picture a side.) For step 3.
2. **The cartridge's sprite cap and flicker: dropped.** First "a rule,
   off by default", then, the same day: "We can do away with stock
   flicker behavior we're not making an emulator." So no 70-sprite
   window, no 20 sprites or 320 dots a line, no flicker, and no setting
   for them: every sprite is drawn every frame. Where the checks compare
   with the cartridge, the cartridge's limits are the checks' own (the
   test tools' VDP model), not the game's. For the rest of step 1.
3. **The maps: tile pictures edited in Tiled**, as now but in full
   colour: the block art as tile pictures, so the animated tiles and each
   block's terrain stay as the game uses them. (Not one big picture a
   map.) For step 3.
