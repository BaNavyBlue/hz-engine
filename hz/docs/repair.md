# REPAIR: a repair truck and its mechanics (session 72)

The owner: "make a "repair" truck or mechanic unit for repairing
vehicles. It would be cool to give them long range and allow them to
retreat from active battle areas." Then, asked how: vehicles only
("because mechs have outposts"); "the repair truck should look like the
refuel truck but instead of the truck navigating totally to the damaged
vehicle, the truck stops in an area and two foot men get out and begin
repairing damaged units and then retreat to truck if enemy units or mech
get close enough. Would be cool because the men can scale up and down
rough terrain"; "Cost... more than refuel"; the men "green uniforms with a
head the color of their side". Then: "build it".

## What a player sees

The PATCHES page's tenth row, **REPAIR** ("THE SHOP SELLS A REPAIR TRUCK"),
off by default, `"repair"` in settings.json's `"patches"`. On, the order
menu has one more unit after the FWA on every map (left of the AMR-51D):
**REPAIR**, 2500, with one order, **RP-MC02** (a spanner; no fee). It is
built at the HQ, carried and put down as any vehicle. It looks like the
SUPPLY in olive drab, its side's colour on the trim.

Put down, it looks for damaged vehicles of its side:

- one within 160 px of it, with no enemy near: it stays where it is, and
  its two mechanics get out, a man a vehicle. Each walks to his vehicle,
  mends it (his sprite steps on the spot), takes the next, and when
  nothing within the truck's reach is left walks back and climbs in;
- none that near, but one within 400 px: it drives up to about 64 px from
  it, round units and walls as the patched SUPPLY does, and parks;
- none: it stands.

An enemy unit or mech within 120 px of the truck or of one of its men, or
a hit on either, is an alarm: the men walk back and climb in, and the
truck drives for its side's nearest outpost or HQ by the ground. Once no
enemy has been that near the truck for 4 seconds it stops and looks for
work again (a vehicle with an enemy within 120 px of it isn't work).

The mechanics can be shot (an INFANTRY's 32 HP) and have no gun. A truck
that has lost a man calls the other in and drives home for a new one, as
above; within 64 px of one of its outposts' middle (112 of its HQ's) it
has two again in under a second. Its crew is the G gauge beside a carried
truck in the order menu, and a truck with no man left blinks the
out-of-supplies marker. A truck destroyed with men out leaves them as
INFANTRY with the capture order (AT-101H), as a destroyed tank leaves its
driver.

What they mend: every vehicle of their side but the FWA -- AMR-51D,
TAX-52, SAM-42, SUPPLY, GRM-34A, the turrets (TURRETS), another repair
truck, their own truck -- and an ST-57U where a man can get within reach
of it from the shore. Not INFANTRY, and not the mech (an outpost's work).
A man mends 2 HP every 12 frames: a TAX-52 from half its 224 HP to whole
in about 11 seconds, two men on two vehicles at once.

The mech can't lift a mechanic. Lifting the truck with men out leaves
them standing; put down again, they walk to it and go on from there (a
man who finds no way back to his truck in 40 seconds turns INFANTRY, and
the truck goes home for another).

## What they are

`assets/data/repair.json` (the port's own; `assets/README.md`). Unit types
10 and 11 are empty records in `UnitStats` and stay so; with the patch
they take this file's:

| | REPAIR (type 10) | MECHANIC (type 11) | SUPPLY | INFANTRY |
|---|---|---|---|---|
| price (x 10 on screen) | 250 | not sold | 150 | 50 |
| HP | 80 | 32 | 64 | 32 |
| size, tiles under it | 24, 3 x 3 | 16, 2 x 2 | 24, 3 x 3 | 16, 2 x 2 |
| speed | 10 | 8 | 10 | 4 |
| fuel, fuel use | 1408, 0 | 65535, 0 | 1408, 0 | 65535, 0 |
| ammo | 8: its crew, 4 a man | 1 | 1 | 80 |
| build time (x 128 frames) | 4 | | 3 | 1 |
| order bits | $4000 (order $E) | none | $8000 | $062E |

(A unit moves every other frame, speed x 32 / 256 px: a mechanic 30 px a
second, the truck 37.) Ammo 1 on the mechanic, as on the SUPPLY, keeps
the out-of-supplies marker off a unit that has no gun.

The file's other values, all a mod's to change (`docs/mods.md`):

| | | |
|---|---|---|
| `search` | 400 | px: how far a truck drives for a damaged vehicle |
| `park` | 64 | px between the truck and that vehicle when it stops |
| `reach` | 160 | px from the truck its men work in |
| `danger` | 120 | px: an enemy this near is an alarm |
| `calm` | 240 | frames with no enemy near the truck before it works again |
| `rate` | 2 | HP a man mends each time he thinks (12 frames) |
| `order` | | order 14's name, fee and icon (16 x 16, a hex digit a pixel's colour of CRAM line 1, `.` none) |
| `truck_colours`, `mechanic_colours` | | 16 numbers each: colour c of the SUPPLY's (the INFANTRY's) tiles is drawn as entry c of the same palette line |

Distances are between the units' middles, the larger of the two axes, as
the SUPPLY's search; `park` is between their boxes.

**The colours.** Nothing is drawn anew. The truck is the SUPPLY's sprites
and a mechanic the INFANTRY's, each through a table that turns one colour
of the side's palette line into another: the truck's greys to the olives
both lines share (13 to 12, 14 to 6, 15 to 14) and its tank's colours to
white and red (5 to 15, 4 to 3); the mechanic's body (7 and 8, the side's
colours) to green and dark olive (2 and 12), his head and legs (10 and
11) left the side's. A sprite's colour table is the number in bits 12-15
of its size/link word, which the chip ignores and the game's own sprites
leave clear (`Sprite.colours` and `video_sprite_colours`,
`src/core/video.h`); a unit's
come from its record's sprite size ($1A, $25). The order menu's picture
is plane cells, which have no such bits, so the menu writes the SUPPLY's
picture, recoloured, into 9 of the 32 blank sprite tiles ($3CC on) when
it shows a repair truck.

## How it works

`src/game/unit_ai.c`, "the repair truck and its mechanics".

**The crew is the truck's ammo**, 4 a man. So the order menu's gauge
shows it, an own outpost or HQ under the truck (or under the mech
carrying it) tops it up as it tops up any unit's ammo, and a truck with
none blinks. A SUPPLY doesn't bring men (`needs_supply`, `supply_fill`
leave a repair truck's ammo alone). The men out are the side's mechanics
that name the truck (`truck_id`); a man's record freed by his death takes
4 off his truck (`repair_unit_freed`, `src/game/units.c`), one freed by
climbing in doesn't.

**The truck** (`unit_repair_truck`), each time it thinks (every 12
frames):

1. The alarm: set to `calm` frames by a hit, an enemy within `danger`, or
   by one of its men's; counted down while no enemy is that near the
   truck. An enemy: the other side's mech unless destroyed, and its units
   but SUPPLYs, repair trucks and mechanics.
2. With an alarm, or a man short: the men are called in (`recall`); it
   waits for them, then drives home (`truck_go_home`): the nearest of its
   side's outposts and its HQ by the ground (`paths_cost`), down
   `paths.c`'s field for it whether or not PATHS is on (Game_Start makes
   the truck's fields with REPAIR, 17 or so, as PATHS makes the others');
   with no way by the ground, straight at it. Home, it stops, and takes
   on a man in two thinks.
3. Else, on its way to a vehicle (`truck_drive_for`): the SUPPLY's route
   search (`route_search`) to within `park` of it, over ground with no
   damaging floor and no current (a truck can't hold its line across one
   as a man can), aiming at the farthest place of the route in a clear
   straight line; a turn of more than 45 degrees made standing; stopped
   facing its way, the SUPPLY's detour. No way, or 40 seconds on it: the
   vehicle is left alone for 10 (`mend_wait`), by every truck and man.
4. Else, with work within `reach` (`mend_job`: a damaged vehicle no enemy
   is within `danger` of and no other mechanic is on) or men out: it
   stands, and sends a man out for the work, one a think (`man_out`: a
   mechanic put down beside the truck, on the side the work is on if there
   is room).
5. Else it sets off for the nearest work within `search`.

**A mechanic** (`unit_mechanic`): his truck gone (destroyed, or its
record another's), he is INFANTRY (`mechanic_to_infantry`); carried, he
waits. A hit, or an enemy within `danger` of him, is his truck's alarm.
Called in, or with no work left within the truck's reach, he walks to the
truck and within 20 px of its box climbs in (his record freed). Else he
walks to his vehicle and from 20 px mends it. He walks by the same search
on his own 2 x 2 tiles, keeping off damaging floors; a current pushes him
a sixteenth of what it pushes a vehicle (`unit_move`), so ABGRUND's slopes
are his to climb. No way to a vehicle, or 30 seconds on it: it is left
alone for 10.

**The grid.** Without UNITS the original's grid holds one unit a 32 px
cell, so a man can't step into his vehicle's cell and mends from up to 20
px off, often from a corner; with UNITS units collide by their boxes and
he walks right up. The search marks the units in its way by the places
each covers (`route_mark_units`), not by a look at every unit from every
place: the same marks (built with `HZ_ROUTE_CHECK` the two are compared
at every search), sooner. The SUPPLY's own search is as it was.

**A man is a unit while he is out**: in the grid, shot at by whatever
sees him, counted in the side's units (the order menu's count, and the
original's 50 a side without UNITS: a side at 50 gets no man out) but not
in the units built (the results').

## The patch's rules

- **The shop** (`shop_types`): with REPAIR bit 10, but not in the attract
  demo. The order menu and the nightmare AI's way through it both take
  it.
- **The records** (`unit_stats_for`, `order_info_for`, `order_icon_for`):
  types 10 and 11 and order $E take repair.json's while `repair_on`: the
  patch on, or on at any time in this game (`HzGame.repair`, as TURRETS').
  Switching it off mid-game stops the shop selling trucks; those bought
  work on, and the next game is the original's.
- **Off, the original's play exactly**: no unit of type 10 or 11 exists,
  and where the code now asks whether a unit is a truck or a mechanic it
  asks only in a game with the patch (`unit_tiles`, `unit_on_foot`,
  `repair_on`): the checks against the ROM try unit types 10 to 15 too,
  and `make check` passes. 24 games (every map; the nightmare AI, the
  CPU, the bots; the PATCHES page's default, every patch, none, PATHS,
  UNITS with crowds) came out line for line the same before and after,
  their traces too.
- **Neither AI buys one.** The original's steps over it in the menu as it
  does the turrets; the nightmare AI takes an enemy truck or mechanic as
  no gun (reach 0) and the least threat.
- **Netplay and replays** carry it with the other PATCHES. `NET_GAME_CODE`
  is 10 for it: builds from before it never meet builds after it, and a
  server's `hz_netai` plays the new ones once rebuilt.
- **repair.json** is optional (absent, the patch sells nothing) and can
  be a mod.

## Checked

`make repair` (`tools/hz_repair.c`, no ROM): scenes on open ground, each
with the PATCHES page's default and REPAIR, then with UNITS too (the
grid's lists checked every frame, no two units overlapping), then with
PATHS too -- 31 scenes with the shop's without the patch, all pass:

| Scene | What must happen | Measured (STADT) |
|---|---|---|
| shop | sold with the patch, not without; price, order, crew | 2500, no fee, 2 |
| buy | P1's pad: the menu opened, the truck chosen, bought, built, picked up at the HQ, carried, put down; its men mend a vehicle | built in 8.2 s |
| mend | a truck between two damaged vehicles: both men out, both whole, the men in, the truck not moved | men out in 24 frames; half a TAX-52 (112 HP) in 12.2 s |
| self | a damaged truck alone | its own men mend it: 50 HP in 5.0 s |
| drive | a vehicle 300 px off, two units in the way | drove round them, parked 69 px off, done in 15.5 s |
| alarm | an enemy comes within 120 px of a man | the men in, the truck 61 px nearer home in 2.4 s; the enemy gone, back and done in 24 s |
| wreck | the truck destroyed, its men out | two INFANTRY, capture order |
| crew | a man killed | a man short at once, the other in, home, two again 8.9 s later |
| lift | the truck carried off, its men out | they wait; put down 250 px off, they walk to it and it drives back |
| full | (without UNITS) the side's 50 units | no man out, nothing breaks |

Whole games (`hz_arena --trucks N`: no AI buys a truck, so every N frames
each side gets one put down beside one of its vehicles): 12 games of up
to 30,000 frames on 8 maps, the nightmare AI, the CPU and the bot against
each other, 7 to 82 trucks a game, half with UNITS and PATHS: none
stopped, the grid's lists right every frame with UNITS, the route
search's two ways of marking units alike at every search. The men mended
0 to 760 HP a side: trucks put down at random spend most of a game fled
home, so this says the code holds up in a game, not what a truck is worth
to a player. The slowest frame with the trucks 2.3 ms (2.4 without), and
10.4 ms with 300 units a side as well (14.4 without, another game): none
over 16 ms.
