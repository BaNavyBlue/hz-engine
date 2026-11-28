# PATHS: units find a way round cliffs (session 71)

The owner, of the nightmare AI's tanks on ABGRUND: "Nightmare keeps
putting down tanks on Abgrund that can never reach enemy base period, the
whole level is cliffs and a water barier. That might be a good "patch" to
add, fix unit pathing so vehicles at the very least never get stuck in
pathing traps." Then: "go ahead with the pathing patch, default off".

## What a player sees

The PATCHES page's ninth row, **PATHS** ("UNITS FIND A WAY ROUND CLIFFS"),
off by default, `"paths"` in settings.json's `"patches"`. Nine rows three
lines apart are 30 lines and the screen has 28, so the page now lists the
patches two lines apart and shows what the one the arrow is on does under
the list.

On, a unit on its way to the enemy HQ (BA-001C, attack HQ) or to an
outpost (AT-101, outpost; AT-101H, an INFANTRY's capture) drives the
ground's shortest way there: round cliffs, water and walls, down the
slopes it can't climb, across currents it can, keeping off damaging floors
and to the middle of a gap. Near its goal (112 px from an HQ's middle, 64
from an outpost's) the original's own driving takes over: it parks,
shoots or drives in as it always did. Every other order, and every unit
when the patch is off, drives as the original's.

The original's units drive straight at their goal (attack HQ by the map's
path hints where it has them) and, when something stops them, turn a step
aside at random. In a bay that faces the goal they turn aside and back for
good; a slope that pushes them back doesn't even count as being stopped.
On ABGRUND a tank driving north into one of its slopes sits there until
its fuel runs out.

## How

`src/game/paths.c`. A **field** is the cost of the shortest way from
every place on the map to one goal, for one footprint and one top speed.
It depends on the map alone, so the AI's drive tests and both machines of
a network game have the same ones, whenever each was made. With PATHS on,
Game_Start (`game_start_step`) makes every field the game's units could want
(`paths_prepare`): each outpost and HQ, for each unit type that moves but
the SUPPLY; a unit's speed, and INFANTRY's lighter push, matter only on a
map with currents (ABGRUND, VULKAN, EISFREI: 6 fields a goal, else 3).
That is 33 to 66 fields, 2.5 to 5.5 ms each (Dijkstra over the map's
256 x 256 places with a bucket a cost, each place's ground worked out
once): 108 ms on STADT, 163 ms on ABGRUND, while the map loads. A field
wanted that isn't there (PATHS turned on in play) is made then; 96 are
kept, the least lately wanted made again when one more is needed. A unit
that can't move has none.

- **A place** is the tile a unit's tiles start from, as
  `obj_check_terrain` tests them: 3 x 3 tiles below INFANTRY, 2 x 2 for
  INFANTRY and the FWA; no b15 (a boat: every tile b9).
- **A step** to one of 8 neighbours costs 10, 14 diagonally, times the
  unit's top speed over the speed it keeps along the step. A diagonal
  step doesn't cut past a wall, a damaging floor or a current it couldn't
  go that way on (it is on that corner for a frame or two as it passes).
- **A current** (terrain type 6: ABGRUND's slopes, VULKAN's and EISFREI's
  currents) adds its push to a unit's velocity each move (`unit_move`):
  `(param & $F) << 6` along one of 8 ways (`<< 2` for INFANTRY). The step
  keeps `along + sqrt(v^2 - across^2)`: down a slope faster, up one slower,
  across one bent. A current too strong across the step to hold the line
  (`across > 0.87 v`), or one that leaves the unit under a quarter of its
  speed, is no way. ABGRUND's slopes push at 960 against a TAX-52's 384:
  one way, down. VULKAN's push at 256 and EISFREI's at 128 to 192: slow
  going against them.
- **A damaging floor** (VULKAN's, type 8: $30 HP each tile entered; the
  TAX-52s traced had 128 and 224) adds 300 a step: a way 30 tiles longer
  rather than one tile on it.
- **Slow ground** (types $A) slows the step as `unit_move` does; slippery
  ground (9) adds 15.
- **The edges:** a step off a place 1 tile from a wall, a damaging floor
  or a current too strong to cross adds 8, 2 tiles from one 3, so the way
  goes through the middle of a gap.

**Driving** (`unit_ai.c`): where the original's order code turns the unit
at its goal (`attack_hq_drive`, `order_outpost`, `order_capture`),
`path_heading` asks `paths_next` for the place to drive at: down the
field's own steps (each the least step + cost of its neighbours) up to 12
steps, the farthest of them it can reach in a straight line. A straight
line is checked every pixel and 1 px either side of it (the unit is never
quite on it): each place on the field's ground, no current in its way, and
no damaging floor but the way's own. After `Unit_SetSprite`, `path_steer`
sends the unit straight at that place (as the SUPPLY fix's `supply_steer`)
rather than along the nearest of the 16 headings; more than 45 degrees off
it turns where it stands (but not on a damaging floor: it burns standing
there). On a current it steers into it to hold its line: what the current
adds across the line is taken off, and what is left of its speed goes
along the line. An outpost order's goal outpost is the nearest by the
ground of those it can reach (`goal_outpost_index`), the original's
nearest in a straight line when it can reach none.

## Measured

**The atlas** (hz_arena `--atlas 3,7200`, the nightmare AI's map of where a
TAX-52 put down reaches the enemy HQ: from each 32 px block of plain
ground, 3 drives of 2 minutes, a block counted only if all 3 hurt the HQ).
Blocks a drive reaches the HQ from, P1's side and P2's, without the patch
and with it; the drive's time from the blocks reached both ways:

| Map | P1 | P2 | Time |
|---|---|---|---|
| 0 ABGRUND | 550 → 694 | 508 → 656 | 20 s → 15 s, 18 → 13 |
| 1 VULKAN | 1,458 → 1,499 | 774 → 947 | 17 → 16, 15 → 13 |
| 2 LOCH | 1,369 → 2,217 | 1,002 → 2,236 | 50 → 34, 57 → 42 |
| 3 STRAND | 58 → 58 | 60 → 60 | islands: a TAX-52 can't get off |
| 4 STADT | 2,010 → 3,393 | 2,467 → 3,393 | 29 → 23, 34 → 27 |
| 5 EISFREI | 1,628 → 1,961 | 1,731 → 1,961 | 42 → 33, 46 → 36 |
| 6 WALDUNG | 1,962 → 2,104 | 1,519 → 2,104 | 46 → 32, 40 → 33 |
| 7 OASE | 2,704 → 3,177 | 2,625 → 3,177 | 50 → 39, 45 → 38 |

No block reached without the patch is lost with it; 7,212 are gained. On
STADT, WALDUNG and OASE every block of ground reaches the HQ. On ABGRUND
the patch can't join the two halves (no ground does), but on each half
the tanks no longer drive up slopes.

The first try lost ground on ABGRUND (80 and 157 blocks fewer) and VULKAN.
What the traces showed, each put right in turn:

1. The field took a slope as dearer ground (+30 a step) either way: tanks
   drove north up a slope that pushes at 960 and sat there. Now a step's
   cost depends on its direction against the push.
2. Places were aimed at by their first pixel, so a unit 1 px short of a
   gap 4 places wide rounded onto the slope beside it and was swept off;
   and the shortest way hugged the gap's edge. Now the place's middle, and
   the edge costs.
3. Straight lines were checked every 4 px: one clipped a slope's corner
   between two checks. Now every pixel and 1 px to each side.
4. On VULKAN a damaging floor at +60 a step was crossed rather than gone
   round, and straight lines cut corners across it: tanks died. Now 300 a
   step, an edge to keep off, and no straight line across one the way
   doesn't take.
5. A diagonal step cut past the corner of a slope; for the frame or two
   it was on that corner the slope swept it away.
6. A tank in the middle of VULKAN's damaging floor overshot each one-tile
   place (it drives 18 px between thinks), stopped to turn round and burnt
   standing. Now a straight line may follow the way's own damaging floor,
   and it never stops to turn on one.

With the patch off, the atlas made again is byte for byte the one before.

**Time.** The first build made each field the first time a unit wanted
it, 5 to 22 ms each with a binary heap and every step's cost worked out
afresh, several at once as units set off: the slowest frame 19 to 136 ms
(0.3 to 1.0 without the patch), up to 8 frames a game over 16 ms. Each
ground's steps worked out once a field, a bucket a cost for the heap, the
fields kept for each kind and speed only where currents make them differ,
and all of them made as the map loads: the slowest frame on each map
0.2 to 2.4 ms with the patch, none over 16 ms (hz_arena `HZ_ARENA_SLOW`,
the nightmare AI against the bot, a game a map). The atlas made again
after that is byte for byte the same. A unit's think with the patch,
`paths_next`, takes about 11 us.

**Games** (hz_arena, `--fixes 237`: the PATCHES page's default and PATHS;
every map, both seats). The patch drives every side's units, so the
original's AI and the bots playing as people gain from it as much as the
nightmare AI does.

- The nightmare AI as the CPU's brain against the original's, TYPE A to D,
  4 games a map and difficulty: P1 128-0, P2 124-0 and 4 draws. The draws
  are one seed, VULKAN seed 2, at each difficulty; TYPE A's is a win at
  frame 55,394 without the patch. With it the original's capture INFANTRY
  and guarding TAX-52s reach VULKAN's outposts: it holds 5 or 6 to the
  nightmare AI's 2 to 4 and keeps 20 units out; the nightmare AI's units
  are lost about as fast as it buys them (102 bought, 4 left at the end)
  and its money piles up to $26,000. At the hour its HQ has 186 HP, the
  CPU's 4 (a next step: HANDOVER.md). VULKAN's other 3 seeds are won, in
  51,000 to 56,000 frames (48,000 to 58,000 without).
- On a human's pad (`bot`), 8 games a map, TYPE A, against the raider
  74-11 with 43 draws (session 70 without the patch: 87-2) and the turtle
  56-8 with 64 draws (60-5).

`make nightmare` and the other checks play the PATCHES page's default, so
without PATHS; `make netplay_check` plays every patch and stays alike.
