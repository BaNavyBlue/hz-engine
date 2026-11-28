# UNITS: no limit to a side's units (session 61)

The owner: "start working on lifting the 50 unit cap as an option in the
patches section", then, asked how many: "Why have any limit just enable or
disable the limit, whould be funny to see what 10000 men walking in a
circle would be like." And on how: "You should be doing away with old M68K
constraints no reason to to bit juggling" -- "We are not emulating the
behavior of an M68K we are making a fully modifiable modern C port". Then,
told the grid held a map to 3,000-4,000 units: "yes change the grid too, I
want 10000 men"; a side's own units passing through each other and every
unit drawn were the choices put to the owner, and taken. In session 68,
having seen tanks stand on one spot where a cliff stopped them: "lets not
allow Units to pass through each other, I misunderstood the implications
The Unit cap should be unbound, the game should self limit itself since at
somepoint the Jet will no longer be able to physically place a unit on the
ground."

## What a player sees

The PATCHES page's eighth row, **UNITS** ("NO LIMIT ON UNITS PER SIDE"),
off by default, `"units"` in settings.json's `"patches"`. Off, the game is
the original's: a side has 50 units at most (the order menu beeps at the
50th, a jet can't collect a unit, a destroyed tank's crew isn't saved when
the side is full), one unit to a 32 px cell of the map, the sprites the
VDP's limits allow. On:

- A side has as many units as it builds. The CPU's AI (both brains) buys
  on while it has money -- the original's raises its own money when it
  can't afford what it wants, so it never stops.
- **Units block each other, their own side's too** (session 68; until
  then a side's passed through each other, and stacked on one spot where
  the ground stopped them). The original's box test against every unit,
  the mechs and the ground, without its rule that a unit whose centre
  falls in a taken cell has run into something: units stand as close as
  their sizes let them, never on top of one another. So room is the
  limit, not a count: when the ground round a drop is full a jet can't
  put a unit down there. Of 10,000 men a side tried round each HQ (hz_arena
  `--spawn`), 4,000 to 6,900 find room on maps 0, 4 and 6. Turned on in
  the middle of a game, it finds the units the original's grid let overlap
  (it marks one unit a cell, so a crowd past 50 a side stands on top of
  itself: 450 to 650 pairs with 600 a side); a unit on top of another may
  move off it -- a move taking their centres further apart -- and not
  further in, so they part as they move (from 454 pairs to 11 in 18 s on
  map 4; without that they stayed, every move blocked).
- **Every unit is drawn, none flickers.** The units' sprites go past the
  VDP's table of 80 (and its 20 a line); they are drawn behind the table's
  sprites, so the mechs, the shots, the outposts' bars and the order menu
  are in front of the units now. (Session 81: with or without UNITS every
  sprite is drawn, docs/hackable.md; UNITS' crowd stays behind the rest.)
- The order menu's count shows two digits as before; from 100 it widens
  left over the two blanks between the money's G and it: four digits, then
  thousands and a K (10K from 10,000).

Switched on or off in the middle of a game, it counts from the next frame
(the units already there stay; the grid is made again for them). Not in
the attract demo, whose recordings are the original's game.

## How the port keeps its units now

The original had two tables of 50 records (v_units1/2, $FF8D94) and
named a unit by bytes bound to them: a grid id ($40 | slot + 1, + $80 for
team 2), a victim code (slot + 1, + $32 for team 2, $64/$65 the mechs, $66
an HQ), a bumped code (slot, + $32), a record's address. The port had
copied all of that. Now:

- **The storage** (`unitlist.c`): each side has a list of slots in chunks
  of 64 records that never move (`HzUnitList`, `unit_at`, `side_slots`),
  growing a slot at a time when UNITS is on and every slot is taken. Each
  game starts each side with 50 free slots, as the original's tables, so
  with the patch off every slot, every scan over a side's slots (the
  SUPPLY's customer cursor, the CPU AI's tend cursor) and every free
  record's leftovers are the original's. A record knows its place
  (`side`, `slot`, kept by `unit_clear`).
- **References** (`ref.h`): whatever names an object -- a grid cell, what
  a probe found (`hit_id`), what a shot hit (`hit_victim`), what a move ran
  into (`bumped`), a shot's owner, a unit's target, customer and far enemy,
  the CPU AI's target and service unit, the nightmare AI's target -- is an
  `HzRef`: its kind (a mech, a unit, an HQ), its side and a unit's slot.
- **The original's quirks** that came from those bytes are written out in
  those terms where they are used, so the game with the patch off plays as
  before: the collision's row byte ($00/$40/$80/$C0 for a taken centre
  cell, REF_ROW, read as P1's mech, nothing, P2's mech, team 1's slot 49);
  the CPU's hits halving the record after the victim's
  (`victim_next_record`); victims from $32 counting as team 2, and team 2's
  slot 49 ($64) as P1's mech (`victim_code_kind`). (Until session 62 the
  CPU HITS fix kept a slip of its own from those codes, P1's mech and team
  2's slot 49 taken for each other; with the fix on, each is itself now.)
- **The 68k's bytes** are only at the edge: `romcodes.c` turns references
  into them and back, for the checks' tools (which load the 68k's RAM into
  an HzGame and write it back) and for the netplay hash, whose images are
  the 68k's layout -- so a game that never grew past 50 a side hashes as
  before, and one that did adds the rest (`hash_grown`).

## The grid with UNITS (`crowd.c`)

The original's v_grid holds one object a 32 px cell (the last marked; a
clear empties the cell whatever is in it), and a unit is stopped when the
cell of its centre is taken. With UNITS (`HzGame.crowd`, set by
`grid_sync` at each frame's start from the patch) v_grid stays empty and:

- each 32 px cell has a list a side of the units whose centre is in it,
  linked through the records (`crowd_next`/`crowd_prev`, slot + 1, so a
  zeroed game is an empty grid) with its length (`crowd_count`), and each
  mech its cell (`crowd_cell`, a robot not destroyed). Marking a unit
  links it, clearing unlinks it; a unit moved while in the grid (an FWA
  shot down turns into an explosion 4 px up and left) is linked again.
- What looked at v_grid walks the lists: a unit's move and its spawn
  (`crowd_hit`: a mech, a unit of the other side, then one of its own,
  session 68), a robot's move and
  landing and its lift (both sides' units, the other mech), a shot, a
  unit's look for an enemy (the same rings, each cell's enemy mech then
  its units; session 68, the cell of its own centre first, which the
  original passes over as only the unit itself is ever there: with UNITS
  units collide by their boxes, so an enemy's centre can share it, and a
  TAX-52 standing by its HQ never saw the AMR-51D beside it shooting the
  HQ), the CPU AI's scans (the counts from `crowd_count`), its
  "is the cell taken", the radar (the side with more units in a cell), the
  nightmare AI's searches.
- A unit's collision with other units has a grid of its own, 8 px
  cells (`HzCrowdFine`, heap memory, the first time a game has UNITS),
  each with a box round its units' places: a unit looks only at cells
  within reach (the widest unit type's half, 24 / 2), and passes over a
  cell whose box can't reach it. Crowds at a front stand many deep on the
  same few pixels; without the boxes every unit beside one looked through
  all of it at each move (a 10,000-a-side battle fell to 44 frames a
  second on this PC). The boxes only grow as units come and are made tight
  when a cell is looked through to its end without a hit, so they are
  never too small: what is hit is what each unit's own test says.

`crowd_check` (hz_arena `--check-crowd`) checks the lists whole: every
unit in play in the cells of its centre once, every list's links and
counts, every unit inside its cell's box, and (session 68; not with
`--toggle`, whose original's frames let units overlap) no two units
overlapping.

## The units' sprites with UNITS

`sprites.c` draws every unit in a view into `HzGame.sprite_more` (the
marker, the turret, the unit, front to back, as the original's), not into
the 70-entry window Sprites_Multiplex shared out; VDP_UploadSprites hands
them to the draw layer (`video_sprites_behind`, since session 80), which
draws them behind the frame's other sprites on every line the game's
sprites are drawn on -- so not on the HUD strip, which holds them back.
No limit a line or a frame. (Until session 81 not on a line a sprite at x
0 masked either: the layer has no such mask now, nor any cap.)
`game_title_prepare` and VDP_Init clear them.

## Tools

- `hz_arena --spawn N` puts N units a side round each HQ at the start
  (AMRs, TAXes and INFANTRY by turns, attacking the HQ); `--spawn-type T`
  only type T (8: INFANTRY), `--spawn-step PX` the spiral's step (24; 8
  finds the gaps between 16 px men), `--spawn-order O` their order;
  `--check-crowd` the grid checked every frame; `--toggle N` UNITS turned
  over every N frames; `--png PREFIX --png-at F,F...` the screen.
- `make unitcap` (`tools/hzunitcap_check.py`): maps 0, 4 and 6, the
  nightmare AI and the original's: 600 a side placed and played with
  UNITS, none past 50 without; 10,000 men a side tried, past 50 placed
  but not all (room is the limit), the grid checked every frame; a game
  turning UNITS over every 97 frames.

## Speed

Session 61, when a side's units passed through each other: 10,000 men a
side (hz_arena, map 4, the nightmare AI against the
original's, attacking the HQs, no window): the march at about 600 frames
a second, the battle at about 350, the whole game (P2's men take P1's HQ
at frame 11,703) in 29 s. Patrolling, 10,000 a side for 3,000 frames: 4.5
s. The game runs at 60.

## Checked

With the patch off, against a build of the commit before (a git
worktree):

- `make check` whole: roundtrip (151 regions identical), shots, units,
  loop, mech, hud, anim, combat (8 maps x 3000 cases), unitmove, unitai
  (4 x 400 cases and 8 games x 1500, 0 differ), ordermenu, mechai (0
  differ), front and gameend (all agree), sound (0 of 137 differ), pans
  (97 runs), demo (all 16 and the Start cases agree).
- hz_arena: every map, five pairings, two difficulties, the fixes off and
  the default, 3 games each: 480 games, byte for byte the old build's.
- `make netcheck`: the 21 of 32 seeds that play with the fixes off or the
  default (19.4 hours) as the old build's; the 11 with all fixes on have
  UNITS, so a game of their own -- and all 32 the same from gcc and clang
  (29.6 hours), so a network game agrees on both machines.
- `make menu`, `make nightmare` (P2 won 92.2%), `make replay_check`,
  `make netplay_check`: all pass.

With the patch on:

- `make unitcap` passes (above).
- `--check-crowd` on every frame: 72 games of 20,000 frames (every map,
  UNITS with the default fixes, alone and with all, 3,000 units a side,
  10,000 men, none spawned) and 216 more (the nightmare AI and the
  original's both ways, the bot), 24 turning UNITS over every 1, 97 or
  600 frames with up to 10,000 men a side: no fault. The first batch
  found one: an FWA shot down moves 4 px, and was left in its old cell.
- ASan and UBSan (clang): 16 games of 8,000 frames, crowds of 10,000 men
  and 2,000 units, UNITS turned over: clean.
- Headless PNGs: the crowds round each HQ in the split screen, and the
  order menu's count at 49, 123, 9999, 10K and 999K.

Not seen: a crowd on a window, in a played game.
