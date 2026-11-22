# PIXELS: only drawn pixels collide (session 75)

The owner: "Another mod we may want to implement is make the collision
box match drawn pixel perimeter instead of a fixed box, if we haven't
already? No reason to have oversized collision boxes? Might need to make
the option screen scrollable its become quite large?" Then: "Collision box
mods should apply to all objects with collision boxs." and "The collision
box should change with object rotation?"

## What a player sees

The PATCHES page's eleventh row, **PIXELS** ("ONLY DRAWN PIXELS COLLIDE"),
off by default, `"pixels"` in settings.json's `"patches"`. On, a unit
stops, the robot stops, a jet bounces off the other and a shot hits only
where what is drawn touches, as it is drawn at that moment: a tank's
picture for the way it faces with its turret over it, an INFANTRY's step,
the mech's frame (robot, jet, carrying, changing), the bullet's dot and
the missile's picture for its heading, a dying unit's blast. Units get
closer together, a shot that passes a tank's corner flies on, and a jet
can carry a unit down between two others where the box said there was no
room.

The PATCHES page shows ten rows at most now; with more (eleven since this
one) they scroll with the arrow, the title says which of how many the
arrow is on (`3 OF 11`), and the row under the arrow is always shown.

## The boxes it replaces

The original tests boxes, every one the same size whatever the picture:
a vehicle 24 px square, an INFANTRY and an FWA 16, the mech 32 in every
form, a ground bullet its middle point, an air bullet and a missile their
top-left corner against 24 px of the enemy jet. What is drawn in them,
averaged over the 16 headings (`hitmask.c`'s shapes, counted):

| object | box | drawn |
|---|---|---|
| AMR-1D | 24 | 39% (214-235 of 576 px) |
| TAX-52 | 24 | 59% |
| SAM-42 | 24 | 48% |
| ST-57U | 24 | 49% |
| SUPPLY | 24 | 56% |
| GRM-12A | 24 | 49% |
| INFANTRY | 16 | 26% |
| FWA | 16 | 31% |
| robot | 32 | 30% |
| jet | 32 | 48% |
| bullet, missile | 8 (their tile) | 38%, 30% |

## How

Each test the original makes is made first, as it was; a hit stands only
if the two shapes then touch. So a hit can be lost, never gained, and
everything the original keeps apart stays apart.

| test | where | the shapes |
|---|---|---|
| a unit moving (`Unit_CheckObjCollision`) | units.c; crowd.c with UNITS | its sprite and turret as drawn; the other's |
| a unit set down (`Unit_CheckSpawnBlocked`: the HQ's, a survivor, a mechanic) | units.c; crowd.c | the sprite it will get, facing its heading |
| the robot walking (`Mech_CheckCollision`) | mech_update.c | its frame (the last one set: the frame is set after the move) |
| the jet landing (`Mech_CheckLandBlocked`) | mech_update.c | the robot standing frame for its heading |
| the jet putting a unit down (the same test) | mech_update.c | the unit's sprite, centred under the jet, facing the jet's way |
| jets meeting (`Mechs_JetCollide`), take-off by the rival (`Mechs_Overlap`) | mech_update.c | both frames |
| a ground bullet (`Shot_CheckHit`) | shots.c | its tile; the unit's or robot's |
| an air bullet, a missile, at the enemy jet | shots.c | its tile; the jet's frame |

The nightmare AI's own copies of the drop test (`mech_land_blocked_at`)
call the game's, so they follow the patch; and the path it works out
for each of its mech's bullets (`bullet_hits_from`) counts a hit only
where the bullet's dot touches the target's shape, as the game will. (At
first it counted the box: with PIXELS on it lost one game of 256 to the
CPU, VULKAN seed 1 as P2, TYPE A, firing where its shots went past;
with the shapes it wins that game at 15:30 with its HQ whole.)

**What stays a box.** A SUPPLY's bumper (the $C round it without the
SUPPLY patch: how the original's SUPPLY docks) is a box still, and so is
a jet carrying a SUPPLY going down. With UNITS off the original's grid
holds one object a 32 px cell, and a unit whose middle is in a taken cell
is blocked whatever it touches; that stays (two objects in a cell, the
grid would lose one), so without UNITS units get closer only across
cells. What is reach rather than a collision stays as it was: the jet's
lift, a SUPPLY's and a repair truck's reach, the take-off and home pad
rules. The AIs' other plans (the SUPPLY's route search, the CPU's and
the nightmare's places to put units down, the nightmare's test for its
own units in a bullet's way) use the boxes: bigger, so they err to the
safe side. The CPU fires as the original does.

**Turning in place.** A unit turns (every 12th frame), an INFANTRY changes
step, the robot changes frame, with no test, so two shapes can come to
touch with neither moving. A move from where they touch already doesn't
count as a hit when it takes their middles further apart (as UNITS has it
for units on top of each other, crowd.c's `moves_off`): they part, and
neither goes further in. For the same reason `hz_arena --check-crowd`
doesn't look for units on top of each other with PIXELS on.

**The pixels.** `hz_assets_load` keeps a byte a pixel row for each sprite
tile $240-$5FF and each tile of the 64 mech frames (`HzAssets.tile_rows`,
`mech_rows`: bit 7 the left pixel, set where the colour isn't 0), made
from the tiles it loaded, so a mod's pictures are its shapes. A shape is
up to 32 rows of 32 bits, laid out as the VDP draws the sprite (down each
column of tiles first, the flips over the whole sprite), cut to the
object's box. A tile outside $240-$5FF (a mod's sprite table pointing
elsewhere) counts as solid. A unit's REPAIR colours, which can turn a
colour off, aren't looked at: no shipped colour table turns one off.

## Network games

The host's PATCHES are the game's, so both machines must know the bit:
`NET_GAME_CODE` is 11 (it was 10). A build with this can't meet one
without it, and the server's AI (`hz_netai`) and the Windows download
have to be updated together.

## Checks

With PIXELS off nothing changes: the five games recorded on the server
from 14 to 16 November (1,264,999 frames, 21,086 state checks) play the
same with this build.

With it on, the nightmare AI against the CPU (`hz_arena`, `--fixes 837`,
the default patches and PIXELS, and `ff7`, the server's and PIXELS; every
difficulty, both seats, seeds 1-2 and 11-14): 768 games, all won. Before
its aim knew the shapes, seeds 11-14 came out the same with PIXELS off
and on (as P1 125 won and 3 drawn, as P2 all 128 won). With UNITS on too
(`ff7`), `hz_arena --check-crowd` finds the grid's lists whole in games
on ABGRUND, STRAND and WALDUNG.
