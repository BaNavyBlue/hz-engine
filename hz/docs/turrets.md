# Turrets (session 60)

The PATCHES page's TURRETS (off by default; `"turrets"` in settings.json's
`"patches"`) puts two gun turrets in the order menu, after the GRM-34A, on
every map: TURRET-1 and TURRET-2. They are bought with one order,
BDF-1SD (stand), built at the HQ, carried and put down as the GRM-34A is,
and never move.

## Where they come from

Unit types 6 and 7 are named "NOT USE" in `UnitStats`. They are the rows
of `UnitDirTable` that hold the TAX-52's and the ST-57U's turrets, which
those vehicles draw on top of themselves (their record's +$12, $86 and
$87). Their records are all 0 but the sprite size ($0A) and type 6's size
(24); their AI handler is `UnitAI_None`; no map sells them. The order menu
has a picture for each (`UnitMenuTiles`: $34E, $37B, the turret facing
up), as it has for every type 0-9. Nothing in the ROM shows them as units
that were cut.

A third-party ROM editor (2021) sells them as "deleted gun turrets". What
it writes, read from its code: `UnitAIHandlers` types 6 and 7 to
`UnitAI_GRM`; bits 6 and 7 into every map's `UnitAvailByMap`; their
records (the GRM-34A's fire delay, sight, missile fields and order, the
player's price, HP, ammo, missiles, fuel, power and build time, and size
0 for "invincible": the hit test is inside the size, so nothing hits it);
and `ChecksumCheck`'s branch skipped. `UnitAI_GRM` takes the GRM-34A's
record by its address, so there they are GRM-34As with a turret's picture:
twin guns, missiles, the GRM-34A's gun whatever their record says.

The port's turrets are its own: bullets only, cheaper than the GRM-34A
(1500), each with the gun of the vehicle it came off.

## What they are

`assets/data/turrets.json` (the port's own; `assets/README.md`):

| | TURRET-1 (the TAX-52's) | TURRET-2 (the ST-57U's) | GRM-34A |
|---|---|---|---|
| price (x 10 on screen) | 200 | 300 | 1500 |
| HP | 160 | 96 | 144 |
| ammo | 60 | 50 | 88, 2 a volley |
| gun: speed, life, fire delay, power | 32, 25, 4, 28 | 16, 112, 5, 16 | 32, 37, 7, 20, twin |
| sight | 2 | 4 | 4 |
| build time (x 128 frames) | 3 | 4 | 6 |
| missiles | none | none | 20 |

Fuel 1408 with no use (0 fuel would show the out-of-supplies marker),
speed 0, no overlay, size 24, order bits $0002.

## How they fight

`unit_turret` (`src/game/unit_ai.c`): the GRM-34A's stand (whatever the
order, no fuel check) and its target scan, the nearest enemy in sight, a
unit before the mech (`rescan_target`), on phase 0 of the four; on phase
2 one bullet from the barrel when the gun points within a step of the
target (`fire_at_target`). A unit thinks every 12 frames. With no turret
of its own the whole sprite turns, a step a think; with the target gone
it turns back to the heading it had. Its own record's sight and gun
throughout.

- **Reach is the sight.** Measured: TURRET-1 picks a target out to 64-72
  px (its bullets would fly 100), TURRET-2 out to 128 px (its shells 224),
  as the TAX-52 and the ST-57U standing.
- **Ground bullets never hit a jet** (a jet isn't in `v_grid`), so the
  turrets hold off units and robots; a jet is the GRM-34A's and the
  SAM-42's.
- **Fire rate** (measured while hitting): TURRET-1 a bullet every 48
  frames, TURRET-2 every 96. One shot is out at a time, and it is only
  known to be over on the next phase 0: a TURRET-1 bullet that misses
  flies its 25 frames, a frame past that check, so the shot after a miss
  waits 96; a TURRET-2 shell flies up to 112 frames before the next
  (as the TAX-52 and the ST-57U).

Duels away from both HQs (an HQ repairs the units near it), each side
standing:

| | at | result |
|---|---|---|
| TURRET-1 against an AMR-51D | 56 px | the AMR-51D down in 3 shots, the turret at 96 of 160 HP |
| TURRET-1 against a TAX-52 | 56 px | the turret down; the TAX-52 at 84 of 224 |
| TURRET-2 against an AMR-51D | 100 px | the AMR-51D down in 5 shots; it never sees the turret |
| TURRET-2 against a TAX-52 | 100 px | the TAX-52 down in 15 shots, the turret untouched |

## The patch's rules

- **The shop** (`shop_types`, `src/game/units.c`): `UnitAvailByMap`'s
  types, and with TURRETS bits 6 and 7, but not in the attract demo,
  whose recordings step through the original's list. The order menu and
  the nightmare AI's way through it both take it.
- **The records** (`unit_stats_for`): types 6 and 7 take turrets.json's
  while `turrets_on`: the patch on, or on at any time in this game
  (`HzGame.turrets`, set at Game_Start and each pass of the frame loop).
  Switching it off mid-game stops the shop selling them; the ones bought
  fight on, and the next game is the original's.
- **Off, the original's play exactly**: the order menu, unit AI, attract
  demo and round-trip checks against the ROM agree as before.
- **The CPU never buys them** (its buy scripts are fixed). The original's
  menu steps over them on its way to a type; the nightmare AI rates an
  enemy TURRET-1 as an AMR-51D (threat 3, reach 104) and TURRET-2 as the
  ST-57U (threat 2, reach 200).
- **Netplay and replays** carry it with the other PATCHES (the host's;
  a change mid-game recorded at its frame). `NET_GAME_CODE` is 3 for it,
  so builds from before it never meet builds after it; a server's
  `hz_netai` (docs/netplay.md) plays the new ones once rebuilt.
- **turrets.json** is optional (absent, the patch sells nothing) and can
  be a mod (`docs/mods.md`).
