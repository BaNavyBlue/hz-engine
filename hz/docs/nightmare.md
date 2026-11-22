# The nightmare AI (session 57)

The owner: "I want you to start grinding against the AI and writing a
nightmare mode AI, Once you've figured out how to crush the AI then train
on your own nightmare AI."

`src/game/nightmare.c` is a second brain for a side the CPU plays, in place
of the original's (`src/game/mech_ai.c`). It was written by playing the
original's AI thousands of times a minute with no window
(`tools/hz_arena.c`), reading where it lost, fixing that, and playing
again; then its tuning was trained against itself (`tools/hznmtrain.py`).

## What a player sees

The Esc screen has a CPU row: ORIGINAL (the default) or NIGHTMARE. It is
saved as `"cpu"` in `settings.json`, and `--cpu original|nightmare` sets
it for one run. A game takes it as it starts (a brain changed in the
middle of a game would pick up the other's half-made plans), so a change
counts from the next game.

It plays by the rules a player has. Its only output is the mech's pad,
as the original's is; but where the original also raises its own money
when it wants to buy (to 150 or 200, and 100 more for every unit it can't
afford) and turns its mech straight at what it aims at, the nightmare AI
does neither: it pays for what it buys from what it earns, and aims by
turning with the pad. (Session 59) Nor does it fire faster than a
player: the game lets a CPU's mech fire without waiting out the fire
delay, but the nightmare AI waits it out ("The fire rate" below). What
every CPU side gets from the game it gets too: the TYPE sets the damage
(on C and D a CPU's shots hit harder and a player's do less to its HQ; on
A and B the same while the player is ahead) and the units it starts with.

A network game has no CPU side, so the row changes nothing there.

## How it plays

Every frame it looks at the whole game, as the original's AI does, and
does one task (`NM_TASK_*`) a few steps at a time: buy through the order
menu (C, the rows, the type, the order, A, C, each press let go before the
next), collect the unit at an own base (A held over it as a jet), carry it
and put it down (A), refuel and repair over a base, fight on foot, chase
the enemy jet. When a task ends, `decide()` picks the next, in this order:

- **Fuel and HP.** Short of what gets it home, or low: the nearest base.
  An empty tank destroys a mech, and a player's jet burns its tank in
  about 16 seconds of flight, so every trip is costed first (`trip_fuel`:
  fuel_use a $50 of speed, 2.75 px a frame on each axis on a diagonal).
- **The HQ.** Enemy units near it, worth most when they have the attack-HQ
  order: it flies to a landing spot beside the nearest threat, lands, and
  shoots. A threat great enough makes it put down what it carries first.
- **A unit carried** goes where it was bought for.
- **The enemy jet** carrying a unit: chased and shot down (the original's
  AI never fires back while it carries, and the unit dies with it). And a
  jet whose gun has just hit it is turned on.
- **Buying:** INFANTRY to take outposts until it has three, or more where
  no base reaches a good drop near the enemy HQ; then TAX-52 tanks with
  attack HQ.
- **The siege:** before and while its tanks attack, it lands by the guards
  round the enemy HQ and those near its own tanks there, and kills them; a
  tank ready at the factory waits for that (900 frames at most).
- Else it waits over the nearest base, which tops it up, and looks again.

Every 15 frames the fuel, the HQ's alarm and a chance at the enemy jet can
break into the task under way.

**Flying.** A map of the danger is made each frame: where an enemy SAM or
GRM would fire missiles at it (they home in on the mech, 256 px out),
where enemy guns reach a robot, and the enemy jet's own blocks (two jets
that touch bounce apart). A flight goes straight when the way is clear,
else along A* over the 64 x 64 blocks round the missiles. The pad is
steered a stopping distance ahead (a jet slows by $18 a frame).

**Shooting.** Before each shot it simulates the bullet along each of the
16 headings against the target's motion as it has seen it move (a unit's
own velocity field can hold a stale value while it stands: aiming by it
missed a standing FWA 84 times in a row), stopping at tiles that stop
shots and at its own units. It fires when the heading it has would hit;
else it turns to the nearest heading that would (never pressing the way
behind it, which makes a robot walk backwards instead of turning), or
walks to a spot near by that hits. In the air the same against the enemy
jet's box ($18 square).

**Putting units down.** A unit with the attack-HQ order drives straight
at the HQ, or along the map's path hints, and when it is blocked turns
aside and drives on: it can be stranded behind a wall or in VULKAN's and
ABGRUND's special ground. So each game it works out, block by block, where
such a unit can drive in from (`ground_build`), and drops its tanks only
there, as near the HQ as that and its fuel allow; a capture's INFANTRY
goes down two blocks from the outpost (the outpost's own blocks can't be
landed on) with the outpost order, which walks it in.

**Its own random numbers** (an xorshift from the game's seed, apart from
the game's Random) spread its choices of place a little, so it doesn't
play every game the same.

Its tuning values are `NM_PARAMS` at the top of `nightmare.c`, each with
what it does; `hz_arena --params` lists them.

## The fire rate (session 59)

The owner: "It appears that Nightmare can fire bullets way faster than
normal AI, and way faster than my turbo button. ... I don't think it
should get an unfair advantage with fire rates."

A player's mech waits out `mech_fire_delay` (6 frames) between shots
(`Mech_FireGun`); a CPU's doesn't, and needs only B pressed again, which
is every other frame. TURBO presses B every other frame, so a player
fires at most every 6. The nightmare AI pressed B whenever its simulated
shot would hit and B was up: measured in the arena (VULKAN, TYPE A, 4
games each, the frames between one shot and the next):

| | 2 frames | 3-5 | 6 | 7-12 | more |
|---|---|---|---|---|---|
| the original's AI | 0 | 0 | 0 | 758 | 167 |
| a player's pad (the bot) | 0 | 0 | 581 | 52 | 180 |
| the nightmare AI | 975 | 19 | 4 | 7 | 90 |

So it fired three times as fast as a player can, and four times as fast
as the original's AI ever does. Now it presses B only once the fire delay
is out, as the bot always did: its shots are 6 frames apart at the
closest, a player's rate. Against the original's AI it loses little (16
games a map, TYPE A: as P1 123 won, 3 lost, 2 drawn; as P2 119 won, 7
lost, 2 drawn), and the human-handicapped bot still takes almost nothing
from it (TYPE A 6 won of 128, TYPE C 2 won, 15 drawn). The arena's `nm`
side and PLAY IT HERE are this; the server's AI (`hz_netai`) plays a
player's side and always had the delay.

## Through a delay (session 64)

The owner, told what was open ("a person playing the server's AI
carries the whole round trip as input delay"): "teach the AI to handle
delay".

In a network game a pad is read some frames after it is chosen: the
lockstep's delay, which has to cover the round trip between the two
machines. The AI was made seeing its pad take at once, and played the
frame in front of it: what it pressed came in on a game already moved on.
Until now the server's AI took a frame of delay and the person the rest
of the round trip -- and even so it thought on frame N for a pad read in
N + 2. The arena measured what that cost (`--delay`, below; the AI as a
bot against the original's CPU, TYPE A, 8 games a map, P1):

| delay | thinking on the game as it is | thinking ahead |
|---|---|---|
| 0 | 42 won, 22 lost | |
| 2 (the server's AI until now) | 13 won, 51 lost | 42 won, 22 lost |
| 5 | 0 won, 62 lost | 37 won, 27 lost |
| 8 | 0 won, 64 lost | 33 won, 30 lost |

Against an unlagged nightmare bot as P1, which nothing can foresee (the
AI P2): with no delay 29 won, 24 lost; at 2, 5 and 8 frames thinking ahead
29-25, 27-27, 36-26; thinking on the game as it is 4-48, 1-60, 0-55.

**Thinking ahead** (`nightmare_think_ahead`): between frames, a copy of
the game (`game_copy`, and the VDP's `vdp_copy`: the copy draws on its
own) is run on, whole passes of the frame loop, to the pass its pad will
be read in, with the pads already chosen for the passes between -- its
own as sent, the rival's as far as they have come and then the last held
-- and it thinks there, just before its mech's update, as it always did.
Its memory goes back into the game; nothing else does. A CPU rival plays
itself in the copy, so against the CPU the copy foresees the game
exactly: every pass of 1.16 million foreseen 8 ahead, every part of the
state hash (`hz_arena --check-ahead`). Thinking ahead by none plays the
very games thinking on the game as it is did.

That check found a slip in `game_copy`: the views' cameras
(`view_camera`) point into the game's own camera array, and a copy kept
the original's, so a copy run a pass drew, and took units on and off
screen, by the real game's cameras a pass behind. It points to the copy's
own now. (Thinking on a copy without running it, as before, read no
camera.)

What it costs: the copy and the passes. In the arena on this machine (a
Ryzen 9 5950X), a frame of the game with the AI on one side took 16 us
thinking on the game as it is, 39 us thinking 2 ahead, 79 us 8 ahead, 121
us 16 ahead: at 60 frames a second, 8 ahead is half a percent of a core.

**The arena:** `--delay1 N`, `--delay2 N` a bot side's pad read N passes
after it is chosen; `--ahead` such a side thinks ahead; `--seen S` how far
it knows a bot rival's pads (default all chosen; 1 is the server's AI,
which runs as the person's pads come); `--check-ahead` the foresight
against the real game. `make ahead` (tools/hzahead_check.py) checks the
three: the foresight exact against the CPU on every map from either side,
thinking ahead by none the same games, and at 8 frames thinking ahead at
least two thirds of the wins with none (37 won, 24 lost, 3 drawn with
none; 32, 30, 2 at 8 ahead; 0 won and 64 lost thinking on the game as
it is).

**The server's AI** (server/hz_netai.c) thinks ahead by its delay and
one more, and so can take most of the round trip itself; the person's
share is 2 frames (docs/netplay.md, "The nightmare AI when no one is
there").

## The arena

`tools/hz_arena.c` (`make hz_arena`) plays whole games with no window and
no sound, about 300,000 frames a second on one thread:

    ./hz_arena assets --map 1 --difficulty 3 --p1 bot --p2 cpu --games 8

Each side is `cpu` (the original's AI), `nm` (the nightmare AI as the CPU),
`bot` (the nightmare AI on a human's pad: the human's handicaps, its fuel
use, the fire delay, the CPU's boosts against it), `turtle` (session 67:
the bot playing a player's way, below) or `idle`. A line a game:
the winner, the frames, the HQs' HP, outposts, units, deaths, money; and
for a nightmare side what it did (frames in each task, deaths by kind,
kills, units sent). `--trace` writes the state every few frames,
`--deaths` every P1 unit lost (where, what was near) and every hit on P1's
HQ (by what, and what P1's mech was doing), `--ground` the map's ground as
a mech finds it, `--param NAME=V` (or `--param1`/`--param2`) a tuning
value.

`tools/hzarena.py` runs a tournament on every core: every map and
difficulty asked for, the table and the averages. `make nightmare` is the
check: the nightmare AI against the original's from either side, 8 games
a map, must win 85% and lose at most 5%.

## What the grinding found

A record of what decided games, in the order it came up:

- v0 lost every game: it carried tanks across the map and ran out of fuel
  (a player's jet carrying uses 1.3 fuel a px; HQ to HQ is ~1,750 px).
- The CPU's HQ is guarded, and an attack-HQ tank in range shoots only the
  HQ, never its attackers: tanks dropped into the guards dealt 37 HP each
  of 1,024. The death log showed what killed them: the CPU's mech, landed
  beside them (on foot it fires every 2 frames).
- Its own HQ fell while its mech was right there (92% of the damage): the
  fights were slow, from aiming at stale velocities and chasing moving
  landing spots. Landing as soon as the target is in reach, and aiming at
  what it has seen, halved the HQ damage it took.
- Loops cost whole games: refuelling between every leg of a trip that
  could never be made, a siege fight out of fuel range begun again and
  again, a service ended at once and chosen again. Each has its own check.
- Jets bounce: the enemy's parked jet, in the way home, bounced it for 600
  frames until its tank ran dry.
- Tanks dropped where the fuel ran out were stranded behind walls (map 2)
  and in currents (map 0): the drops now follow how a unit drives.
- Outposts: four INFANTRY each (200 money apiece with the outpost order),
  too dear to take for income alone after three; worth it only to bring a
  drop within reach.
- Tried and dropped: SAMs sent with the tanks (worse), a garrison of
  standing units by the HQ (worse: the original's AI drops its attackers
  beside the enemy's units).

## Results

The nightmare AI against the original's, both as the CPU (so neither gets
the TYPE's boosts, which are for a human against a CPU): difficulty A, 8
maps, 32 games a map from each side, an hour each at most
(`tools/hzarena.py ./hz_arena assets nm cpu --games 32 --diffs 0`):

| | nightmare wins | original wins | draws |
|---|---|---|---|
| nightmare as P1 | 255 | 1 | 0 |
| nightmare as P2 | 250 | 6 | 0 |

The original's AI against itself, the same way: 126 to 84, 46 draws.

Against a player: the `bot` side is the nightmare AI with a human's
handicaps (its fuel use, the fire delay, the TYPE's boosts against it),
the nearest thing to a player the arena has. 16 games a map, 128 a
difficulty, as P1:

| TYPE | bot against the original | bot against the nightmare |
|---|---|---|
| A | 84 won, 44 lost | 8 won, 120 lost |
| B | 35 won, 81 lost, 12 drawn | 2 won, 126 lost |
| C | 10 won, 116 lost, 2 drawn | 0 won, 118 lost, 10 drawn |
| D | 9 won, 100 lost, 19 drawn | 0 won, 117 lost, 11 drawn |

On A the handicapped bot beats the original's AI two games to one; from B
up the TYPE's boosts (the CPU's shots half as strong again or twice as
strong, the player's against its HQ cut) and the CPU's fire rate tell. The
nightmare AI under the same boosts leaves it a handful of games in 512.

## The training

`tools/hznmtrain.py` tunes the values by self-play: a challenger (the
champion with one to three values moved a few steps) plays the champion,
both as the CPU, on every map from both sides with the same seeds; one
that takes more than 56% of the points is played against the original's
AI and becomes the champion if it does no worse there. Its log is a line a
step; the champion's values go into `NM_PARAMS` by hand.

The defaults are its second run's champion (air_guard 240 to 200,
fuel_margin 150 to 210, land_reach 100 to 76, svc_hp 80 to 75), which beat
the values before it 280 games to 192 on fresh seeds and lost no ground
against the original's AI; a third run, with twice the games a step,
found nothing better in 120 steps (what won more in self-play lost against
the original's AI). The code itself is A/B'd the same way: `make
hz_arena_ab OLD=old.c` builds an arena with an older `nightmare.c`
(renamed) as the side `old`; self-play between versions is dominated by
the maps' sides (on map 1 P2 wins 7 games in 8 against itself; on map 0 P1
does), so a change is judged against the original's AI too.

    python3 tools/hznmtrain.py ./train_copy/hz_arena assets build/train

(Run it on a copy of `hz_arena`: a rebuild under it changes the game it
is training on.)

## How people beat it (session 67)

The owner: "I think I need to play more games against AI and players
to train unless you think you can create some sort of meaningful
strategy to train nightmare?" More self-play could not help: the tuning
had stopped gaining (above), and the arena's bot, its only stand-in for
a person, lost 8 games in 128 to it -- while the owner beat it both
times on record (2026-10-21, ABGRUND, TYPE A: against it as the CPU, and
online against the server's AI) and someone else beat the server's AI in
about 15 minutes. So the arena was taught to play as they did.

**What the recordings show** (`hz_replay --minutes --events`). Both wins
were the same game: INFANTRY first, three or four at a time to an
outpost, the nearest the own HQ first (5 outposts by minute 9 to its 4);
then SAM-42s and TAX-52s with the stand order put down within about
200 px of the own HQ, where an attacking tank has to stop to shoot it;
the mech at home (28% and 53% of the game by the own HQ). The nightmare
AI kept sending attack-HQ TAX-52s at that, one at a time (21 lost in the
local game), and from minute 18 sat on 3,000-6,000 money it had no use
for. Then the push: online, three attack-HQ TAX-52s put down 119-152 px
from its HQ inside 40 seconds took it from 1,024 to 100 in a minute,
while the AI was off putting its own tank down 720 px from the owner's
HQ (its drops keep out of SAM-42 range) -- the server's AI has no start
army, and nothing stood by its HQ.

**The turtle** (`hz_arena --p1 turtle`): the bot (a human's pad and
handicaps) playing that way, as the nightmare AI's style 1
(`STYLE=1`): its captures the nearest its HQ first; a wall of standing
units (`WALL`, 10: SAM-42s and TAX-52s by turns) on the ring `WALL_R`
(160 px) round its HQ, the half facing the enemy, no two on the same
spot; then attack-HQ units put down 112 px from the enemy HQ until half
the wall is gone. It neither sieges nor chases the enemy jet as it
carries (chasing lost it a fifth of its wins). Against the nightmare AI
on a human's pad -- as the server's AI plays -- it won as the owner
did: 53 games to 41 as P1, 80 to 29 as P2 (bot against bot: 47 to 55).
Against the nightmare AI as the CPU, with the start army and the TYPE's
boosts, 24 in 128.

**What it found** (each in nightmare.c, both styles):

- *A robot walking and not moving.* In a fight at home its robot walked
  for 45 seconds into ground that pushed it back, shooting nothing,
  while a tank took the HQ apart: 80-100% of the HQ damage in the
  turtle's losses came while its mech was "fighting". A robot walking
  45 frames, its heading the same and never 4 px from where it began,
  takes off and keeps 40 px off that place for 600 frames
  (`HzNightmare.unstuck` counts them).
- *A human's special ground.* What it walked into was ABGRUND's special
  ground, where only a human's mech may land and which pushes a robot
  back off it. Fights now land and walk on plain land only
  (`fight_landable`, `walkable`). The bot's games on ABGRUND against the
  CPU nightmare went from 0-1 won in 16 to 8.
- *A delivery over water.* On a human's pad (no CPU in the game: its
  carrying fuel isn't halved on TYPE A), a unit picked up on less than
  a full tank was planned as far as the fuel went; on STRAND that point
  was over the sea, where nothing can be put down, and it moved on 32 px
  at a time until the tank ran dry -- 10 times in 6 minutes, with
  nothing delivered. Now it fills up at the base it is at first; a
  short leg ends only where the unit can go down; nowhere near, it
  looks 320 px round, then goes back to a base; full at a base with
  nowhere in reach, it puts the unit down there.

**A garrison** (`GARRISON`, style 0, default 0): standing units kept
round its HQ before it buys attack units. Three of them took the CPU
nightmare from 87 to 112 wins in 128 against the turtle as P1, but cost a
little against the original's AI and nothing showed on a human's pad;
left at 0 for now.

**Results** (TYPE A, every map, 16 games a map; the nightmare AI's wins,
losses, draws; `GARRISON` 0):

| | before | after |
|---|---|---|
| as P1 against the original's AI | 126, 1, 1 | 128, 0, 0 |
| as P2 against the original's AI | 121, 6, 1 | 125, 3, 0 |
| as the CPU, P1, against the turtle | 69, 48, 11 | 87, 31, 10 |
| as the CPU, P2, against the turtle | 94, 24, 10 | 111, 9, 8 |
| on a human's pad, P1, against the turtle | 29, 80, 19 | 46, 77, 5 |
| on a human's pad, P2, against the turtle | 41, 53, 34 | 51, 61, 16 |

(The turtle runs the same code, so it gained by the fixes too.) Against
session 66's nightmare AI, both as the CPU (`make hz_arena_ab`), it won
150 games to 106 (61-67 as P1, 89-39 as P2); `make nightmare` 64-0 and
63-1, `make ahead` passes. On a
human's pad -- the server's AI -- it still loses to the turtle, the way
it lost to the owner: on VULKAN it holds 8 outposts to the turtle's 1,
puts down 100-130 attack-HQ TAX-52s a game, and the wall and the
turtle's mech kill 150-186 units; it wins two in three, after 45-53
minutes. The next step is its attack: units going in together, not one
at a time.

**The server's AI keeps its games** (server/hz_netai.c): each game a
person plays it is recorded (replay.h, kind `netai`, `side` the AI's) to
`--replays DIR`, else `$HZ_SAVES/replays` or `saves/replays` where it
runs (`--no-replays` none), named as the game names its own with
`NETAI-P2` (or P1) for the AI's side; `hz_replay` calls that side "the
server's AI" and the other by the person's name. `make netplay_check`
plays one back (`hz_replay --check`).

## Attacks in a group, and the chase (session 68)

The owner: "push it, rebuild the server and start on the grouped
attack". Session 67 left the nightmare AI on a human's pad -- the
server's AI -- losing to the turtle with its attack units going in one
at a time into the wall.

**Groups** (`GROUP`, style 0, default 0: off). Attack units are bought
with the stand order and put down by a rally point: the attack drop with
the shortest drive in that a unit can be carried to from an own base on
a full tank, at least `RALLY` px from the enemy HQ, kept while it is out
of missiles and in reach (`RALLY_R` the room they stand in). With `GROUP`
there, or the rally point gone bad, each in turn is lifted
(`NM_TASK_LIFT`: the jet over it so that its x + 8, y + 8 is in the lift
box, then A), given attack HQ in the order menu (carrying a unit, the
menu changes its order for the order's fee: 350 for attack HQ, 10 for
stand; `task_buy` without the type row), and put down by the first of
its group sent while that one is out of missiles and within 320 px,
else where it stood. That much works: a unit goes in every 2.5-6 s, close
behind the last. But it lost, every way it was tried (on a human's pad
against the turtle, both sides, 256 games):

| | won | lost |
|---|---|---|
| one at a time (as it was) | 97 | 138 |
| groups of 4, rally point 320 px out | 40 | 186 |
| groups of 3, 480 px | 48 | 180 |
| groups of 4, 704 px | 54 | 182 |

The units at a rally point stand there for as many trips as there are
of them (a unit is built in 2-9 s, a trip takes 10-20 s), and the
turtle's mech, which fights what comes within 640 px of its HQ or 320 px
of its outposts, picks them off one at a time; 704 px out it still
found them. As the CPU groups of 4 lost too: against the original's AI
107-15 and 96-20, against the turtle 42-65 and 45-43. Left in and off:
the lift and the new order are sound; it is standing units in the open
that fail.

**What did help: not chasing the jet on a human's pad.** The same 256
games for other changes (style 0 unless said):

| | won | lost |
|---|---|---|
| as it was | 97 | 138 |
| `GARRISON` 3 / 6 / 10 | 90 / 91 / 89 | 130 / 123 / 124 |
| no siege (`SIEGE` 0) | 83 | 154 |
| drops 112 px from the HQ | 85 | 151 |
| the enemy mech on foot fought (`MECH_FIGHT` 2) | 96 | 139 |
| **the carrying jet not chased** (`DOG_RANGE` 0) | **135** | **93** |
| ... chased within 350 px | 125 | 109 |
| the turtle's way (style 1 and its settings) | 93 | 93 |
| ... with a wall of 6 | 102 | 79 |

Against the nightmare AI as it was, also on a human's pad (that mirror
128-128): not chasing won 134-122; the turtle's way 138-97, and with a
wall of 6 164-82. Head to head, not chasing beat the turtle's way with a
wall of 6, 129 games to 102. As the CPU not chasing changed little
(against the original's AI 127-1 and 124-4, against the turtle 94-22
and 104-15, where it had 128-0, 125-3, 87-31 and 111-9), so it is a
human's pad's alone: `DOG_PAD` (default 0, never) stands for `DOG_RANGE`
there, and the CPU keeps 700. On a human's pad the jet's gun is slower
(a human's fire delay), and every chase left the unit it was fetching,
the fight at home or the siege.

**Results** (TYPE A, every map, 16 games a map; wins, losses, draws):

| | session 67 | now |
|---|---|---|
| as P1 against the original's AI | 128, 0, 0 | 128, 0, 0 |
| as P2 against the original's AI | 125, 3, 0 | 125, 3, 0 |
| as the CPU, P1, against the turtle | 87, 31, 10 | 87, 31, 10 |
| as the CPU, P2, against the turtle | 111, 9, 8 | 111, 9, 8 |
| on a human's pad, P1, against the turtle | 46, 77, 5 | 65, 48, 15 |
| on a human's pad, P2, against the turtle | 51, 61, 16 | 70, 45, 13 |

As the CPU they are the very same games. `make nightmare` 64-0 and 63-1;
`make ahead` passes (on a human's pad against the CPU, thinking 8 frames
ahead 39 won to 24, with no delay 37 to 27). `tools/hzarena.py --jobs J`
plays J games at a time (The owner: "Don't totally saturate my CPU with
parallel tasks please").

**The turtle's way with a smaller wall** (session 68 (3); the owner:
"try the turtle style with a wall of 6 next"). On a human's pad, style
1 with the turtle's settings, against the turtle and against the
nightmare AI as it now is (`DOG_PAD` 0), both on a human's pad (256
games each, both sides; wins, losses):

| | against the turtle | against the nightmare AI |
|---|---|---|
| a wall of 6 | 102-79 | 102-129 |
| ... `WALL_R` 120 | 87-92 | 114-114 |
| ... `WALL_R` 200 | 60-107 | 103-130 |
| a wall of 4 | 89-104 | 122-109 |
| a wall of 8 | 92-93 | 103-123 |
| a wall of 6, sieging (`SIEGE` 1) | 83-101 | 102-128 |
| a wall of 6, drops at 150 px | 86-91 | 107-133 |
| the nightmare AI itself | 135-93 | 128-128 |

None beats the nightmare AI as it is against both (a difference under
about 20 in 256 games is noise), so the server's AI keeps it.

## Attack drops tried first, and stuck units lifted (session 68 (5))

The owner, after a game on ABGRUND against the server's AI: "The tanks
ran out of fuel because they drove off a cliff and kept trying to drive
up a clif", then "yes start on 1 and 2" (1: try where a drop's unit
goes before choosing it; 2: lift a stuck one and put it down again).
The AI sent 16 TAX-52s one at a time; the line from its base to the
player's HQ crosses a band of slopes (`TerrainEv_Current`: a slope
pushes a unit back down), and each one put down above it drove at the
slope, moving a pixel back and forth with its fuel going (a new tile's
fuel each move, about 48 a second), dry in about 27 s. `route_from`, a
point a block, never saw the slope.

**The drive test** (`DRIVE` 3000 frames, 0 off; `DRIVE_NEW` 4). A unit
of the attack type is put down at the place in a copy of the game with
nothing else in it (no other unit, no shot, both mechs destroyed in the
far corner, every outpost nobody's, the HQ whole, the random numbers
from the place) and only the units run (`unit_ai_update`,
`combat_update`). It passes when its shots take HP off the enemy HQ, or
when it is still driving on (fuel left, 64 px in the last 10 s) as the
time runs out; it fails when it runs dry or dies. The answer depends on
the map, the HQ, the side, the unit and the patches alone, so it is
kept for each 8 px place, across games; the places the unit drove
through get its answer too, which saves most of the tries. A try costs
about 2 ms, and a think makes at most `DRIVE_NEW` new ones.

- `attack_drop_at` tries best first: the best block not known to fail
  is tried, then the next, so it tries no more than the failing blocks
  that score better than the best that passes.
- Before the jet sets out, the delivery's drop is tried, the jet hovering
  where it plans (3 s at most). That covers the final drop, the best
  block in reach, and "as far as the fuel goes": the places by the line
  within 96 px, 32 px apart. Where none passes it is put down as before.
  A version that tried more out in the field, flying on from a failing
  drop to the next, ran its jet short of fuel; its nearest base was an
  outpost under the player's SAMs, and it lost 17 jets in the 50 minutes
  below.
- At the drop the goal's answer is checked again (the nearest passing
  place within 96 px, 8 px apart, if it fails), never waiting in missile
  reach.

**The rescue** (`RESCUE` 1). Once a second every own attack unit in the
first 64 slots is looked at. One that, two seconds running, has moved
less than 8 px while its fuel went down 8 or more has stalled: a unit
stopped to fight burns no fuel, and within 192 px of the enemy HQ it is
where it should be. Decide lifts the stalled unit nearest the jet
(`NM_TASK_LIFT`) and delivers it as an attack unit again, once per unit.
It goes only out of missile reach, or at its edge (a block next to it
out of reach) with the jet on half its HP or more.

In `nightmare_think_ahead`'s copy a nightmare CPU rival's thinks are
thrown away, so its tries are kept apart from the ones in the game;
otherwise the copy filled the shared answers first and the side's own
counts differed (`make ahead`'s "the same game").

**The game itself.** A scratch tool (not in the repo) played
The owner's recording to ten points (4:10 to 11:40, every 50 s), then
gave P2 to the nightmare AI on a human's pad for 5 minutes, P1 its
recorded pad. Totals over the ten:

| | drops | units run dry | jets lost | the player's HQ HP taken | lifted |
|---|---|---|---|---|---|
| as it was (`DRIVE` 0, `RESCUE` 0) | 65 | 46 | 0 | 2202 | 0 |
| now | 71 | 46 | 0 | 2308 | 4 |

The drops it took to before were the slope's, and the test finds them
failing; but most of that game no passing drop was in fuel reach of the
AI's base in the north, and its one outpost on the way (by the slopes)
was under the player's SAMs, so it put them down as before, and they
stalled where lifting them meant flying into the missiles. Where a
passing drop is in reach it is taken (in the arena it found them on
every map). A unit sent where no drive passes is wasted money; not
buying one then is the next thing to try.

**The arena** (TYPE A, 16 games a map, the turtle against the nightmare
AI on a human's pad, UNITS on; P1 wins, P2 wins, draws):

| map | as it was | now |
|---|---|---|
| ABGRUND | 15, 1, 0 | 15, 1, 0 (8 lifted) |
| VULKAN | 1, 14, 1 | 1, 14, 1 |
| LOCH | 6, 4, 6 | 6, 4, 6 |
| STRAND | 8, 8, 0 | 9, 7, 0 |

As the CPU against the original's AI (ABGRUND and LOCH, 16 each) 16-0
both ways. `make nightmare` 64-0 and 63-1, `make ahead` PASS,
`make unitcap` PASS, `make netplay_check` all alike, `make replay_check`
all recorded. The slowest think in the game above took 5.4 ms (0.24 ms
without the test), 0.011 ms on average.

## Drops kept out of the missiles, and the nest tried (session 68 (6))

The owner, after another game on ABGRUND against the server's AI
(039f944, the AI P2): "bruh That was terrible", then "yes start on 1
and 2" (1: never put an attack unit down where the missiles reach; 2:
attack the SAMs and GRMs themselves). In that game the player built a
nest of SAMs and GRMs round 1575,1280, on the way from the AI's one
outpost forward (1968,560) to the player's HQ. Its tanks from there ran
dry about 220 px short of the HQ, so the drive test failed 103 of the
106 places it tried and the AI put them down as before, the nearest
passing nothing, inside the nest: the jet was shot down 6 times in 7
deliveries there. Its own HQ, undefended, went from 926 to 0 in
minutes 19 to 26.

**Safe drops** (`SAFE_DROP` 3; 0 off). `danger_build` counts, for each
32 px block, the enemy SAMs and GRMs whose reach (256 + 16 px, square)
covers it. An attack unit is never put down on a block that 3 or more
reach, nor on one any reaches with the jet under half its HP. That holds
for the old ways' places too: the best block when no drive passes, "as
far as the fuel goes", the drop at the base, and the rings round them
(`drop_ring`). At the drop the goal is looked at again; where SAMs were
put down since, the delivery is planned again (`hot`).

Played on the owner's recording (P2 handed to the AI at six points,
48600 to 75600 frames, 4 minutes each; the scratch tool of session 68
(5)), totals:

| | hits on the AI's mech | jets lost | the player's HQ HP taken | SAMs and GRMs killed |
|---|---|---|---|---|
| as it was (039f944) | 101 | 14 | 350 | 4 |
| any reach (`SAFE_DROP` 1) | 42 | 0 | 296 | 17 |
| 2 | 49 | 0 | 388 | 10 |
| 3 (now) | 77 | 1 | 554 | 8 |

Any reach kept the jet alive but left too few places to go: in the
arena on STRAND (the bot against the turtle) 178 units stalled, and 2
still stalled there; 3 plays the same arena games as before. On the
earlier game (session 68 (5)'s ten points) 71 drops to 66, units run
dry 46 to 44, the HQ HP taken 2308 to 2146, one jet lost (shot at its
own outpost while being serviced): the two points that differ reach the
western approach about a minute later.

**The nest test** (`NEST`, 0 off; 448 tried). Where no drop in reach
passes the drive test, a second test: a unit put down within `NEST` px
of an enemy SAM or GRM that guards the HQ (within 1024 px of it, 64 at
most), the nearest guard first, run in the copy with the enemy's SAMs
and GRMs kept, for half the drive test's time; it passes when it takes
HP off one of them (looked at every 8 frames). Its answers are kept
apart from the drive test's, by a hash of where those SAMs and GRMs
stand. A TAX-52 sees only 64 to 72 px and drives the map's route to
the HQ, so it passes the SAMs about 80 px off without firing; few places
pass. On the game above no drop went to one at the six points (the
differences were the game going another way), the tries went from 693
to 2465 and the slowest think to 323 ms. It is left off.

Also, a unit that runs dry still driving (64 px in the 4 s before) no
longer gives its failing answer to the places it drove through: put
down further on, with the fuel spent getting there, one may pass. A
nest test gives its answer to its start alone.

**The arena** (TYPE A, 32 games each, UNITS on): the bot against the
turtle on all eight maps, both seats on ABGRUND, STRAND and STADT, the
same wins as 039f944; against the original's AI on ABGRUND 25-7 as P2,
20-12 as P1, both versions (20 drops planned again). On ABGRUND as P2
the bot loses to the turtle 0-32 in both.

`make nightmare` 64-0 and 63-1, `make ahead` PASS (and the same game
with `NEST` 448), `make unitcap` PASS, `make netplay_check` all alike,
`make replay_check` all recorded, `make demo` all agree. The N line adds
`nests` and `hot`.

Next: defending the HQ. In that game it went from 926 HP to 0 in
seven minutes, the AI sending nothing back.

## The robot's shot one move short, and guards where its HQ was shot from (session 68 (7))

The owner, of the HQ left undefended (above): "Defenend yo ass". The
same game, minutes 19 to 27, the AI thinking along with the recording
(its pad 99% the recorded one): the player sent TAX-52s one at a time,
from two sides, each stopping 130 to 160 px off the HQ (236,256 and about
30,90) to shoot it. The AI fought every one, but its jet was ferrying
from an outpost 1,800 px east and came about 11 s late, often to two at
once; landed, it killed each in 2 or 3 s.

**The shot.** Once it stood at 116,215 and fired 60 shots at a TAX-52 on
48 HP at 236,256, its whole ammo, and none hit; it flew off for more and
the HQ lost 270 HP meanwhile. `bullet_hits_from` moved the bullet
`shot_life` times; the game moves it `shot_life - 1` (a shot's life goes
down first, and the frame it runs out it explodes where it is). Each
shot ended at 231,269, its point 235,273, a pixel short of the tank's
box. Now it moves it as the game does, as `air_hits` already did; it
answers `fight_spot` and `firing_spot` too.

**Guards** (`DEFEND` 2; 0 none). Each think its HQ's HP is looked at;
when it went down, every enemy attack-HQ unit within 224 px of it marks
its place a spot (4 kept, 48 px apart; a new one takes the least hit's
slot). Once a spot is known it keeps `DEFEND` TAX-52s standing round the
HQ, bought before attack units: each put down at the spot with the fewest
guards (of those, the most hit), the nearest landable place within 64 px.
On guard near (`DEFEND_GUARD` 1, 420 to the stand's 330) they chased off
and did worse.

On the owner's recording (P2 handed to the AI at six points, 54000 to
84000 frames, 4 minutes each), the AI thinking along with the recording
before each so it knows the game so far:

| | the AI's HQ HP lost | of it from 84000 | the player's HQ HP taken |
|---|---|---|---|
| no guards | 855 | 434 | 892 |
| 2 standing (now) | 568 | 70 | 434 |
| 2 on guard near | 643 | 172 | 852 |
| 3 standing | 498 | 126 | 124 |
| 4 standing | 520 | 126 | 84 |

The recorded pad bought no guards, so each point begins without them and
buys its own, which overstates what they cost the attack; at 72000 and
78000 the attack was under way before one could be put down. The shot
alone, the AI starting each point knowing nothing: 1144 to 1142.

**The arena** (TYPE A, 32 games each, UNITS on). With guards against
without: maps 1 to 7, both seats against the turtle, the same games (on
those it never reached the bot's HQ, or no guard met it); on ABGRUND
the bot's wins against the turtle 1 to 0 as P2 and 31 both as P1,
against the original's AI 25 to 29 as P2 and 24 to 22 as P1. The shot,
the bot's alone (the turtle runs this code too; `--param1` or
`--param2` gave it the old one): wins 277 to 296 and losses 197 to 187
in 512 games, all eight maps both seats (the most WALDUNG as P1, 4 to 8,
and OASE as P2, 19 to 27); with the turtle's shot right as well, OASE
as P2 went to 27 draws in 32. Against the original's AI on ABGRUND, as
P1, 20-12 to 24-8.

`make nightmare` 64-0 as P2 (as P1 508-2, 2 drawn, in 512), `make ahead`
PASS, `make unitcap` PASS, `make netplay_check` all alike,
`make replay_check` all recorded. The N line adds `guarded` and `spots`.

## Never to the original's AI (session 68 (8))

The owner: "fyi, nightmare should never lose to original CPU." So
every loss to it is a bug. The set: all eight maps, both seats, 32 games
each (TYPE A, `--fixes b7`), the nightmare AI as the CPU's brain (`nm`)
and on a human's pad (`bot`, the server's AI, the CPU's perks against
it). As the CPU's brain it had lost 5 of 256 (16 games each); on the
pad, with the first three fixes below, 155 of 512, 149 of them with
over 3,000 of its money unspent (most 10,000 to 24,000).

**The jet hovering.** On ABGRUND it flew to a fight's landing spot,
stopped 2 px off it (within the flight's 4) where it couldn't land, and
hovered there 3.5 minutes, the fight timing out and taken up again. Now
a second at the spot unable to land makes it a spot it was stuck at, and
`fight_spot` finds another. ABGRUND, the bot as P1: 22-10 to 31-1.

**The robot walking on the spot.** A robot walking and not moving was
seen as stuck only with the same heading; walking between two headings
it took each in turn and never was. It stood 40 s so while a TAX-52 took
its HQ from 968 to 226. Now where it stands alone counts. ABGRUND, the
bot: 32-0 as P1, 31-1 as P2.

**The robot on low ground.** A robot can't take off on low ground
(`Mech_GetTerrain`'s GROUND_LOW). On VULKAN it pressed A there for 3.7
minutes. `take_off` now walks it to the nearest plain land first (rings
8 to 192 px out), then takes off.

**Guards from the waiting unit** (`DEFEND_RICH` 1500, `DEFEND_FIRST` 1).
Its HQ being shot (in the last 10 s), it wants one more guard for each
1,500 of its money, 8 at most; and guards come before a fight, the
attack unit ready at a base, waiting while the jet fights at home, made
one and put down first (its order changed once aboard). The N line's
`converted`. Not the turtle's (style 1). With the rules below, both off:
the bot against the original's AI 437-67-8 to their 454-50-8, most of
it STRAND, STADT and EISFREI; against the turtle on STADT and EISFREI
the very games.

**INFANTRY at one outpost.** VULKAN, the CPU as P1: it sent 50 INFANTRY
at one outpost the CPU's jet kept, in 31 minutes, delivered no attack
unit, and lost with 13,358 unspent. A run of 6 not taking it ended, and
the next run went to the same one. In the bot's lost games it sent 27 to
80 a game (the mean, by map) and delivered 1 to 17 attack units; in its
won games 10 to 35. Now (`CAPTURE_REST` 10800) an outpost 6 were sent at
and not taken is left 3 minutes times the runs in a row that failed
there: below `CAPTURE_MIN` from the first, past it (outposts for the
front) from the second (`CAPTURE_ALL` 3). The N line's `gaveup`.

**A finished HQ left** (`FINISH_HP` 256). VULKAN, the AI as P1: the
CPU's HQ was down to 198 by minute 8 and stayed there 25 minutes while it
sent INFANTRY at outposts for the front, its own HQ ground down to 5,
26,000 unspent. The enemy HQ on 256 or less, attack units come before
outposts for the front: won in 12.5 minutes, its HQ on 912.

**No front while its HQ is shot** (`CAPTURE_CALM` 36000). Its HQ shot in
the last 10 minutes, the same. The original's AI sends units at the HQ
from the start; the turtle walls its HQ, and outposts taken starve it.

**The turtle and the CPU want opposite things.** Each first try was
measured against both: the bot on the sets that moved most (32 games
each; against the CPU VULKAN and STADT both seats and EISFREI as P2;
against the turtle, which kept the old rules, STADT, EISFREI and OASE,
both seats):

| capture rules | against the CPU | against the turtle |
|---|---|---|
| as before session 68 (8), the finish rule added | 91-62 | 57-33 |
| one outpost left after its first failure, past `CAPTURE_MIN` only nearer the enemy HQ than its bases (`CAPTURE_AHEAD`), no front where the enemy's missiles close every drop (`FRONT_OPEN`) | 129-24 | 22-152 |
| one outpost left after its first failure, always | 80-75 | 29-93 |
| all left after a failure, 1 or 3 minutes | 108-42 | 34-101, 16-137 |
| left below `CAPTURE_MIN` only, no front while its HQ was shot in the last 1, 5, 10 minutes or ever | 104-49, 117-37, 119-33, 121-32 | 65-38, 76-56, 77-56, 77-56 |

Against the turtle the old rules won by starving it: on EISFREI they took
every one of its outposts (3 to 0), its money fell to about 100, and the
bot won at 18 minutes; resting an outpost, or keeping to ones nearer the
front, or stopping where its SAMs close the drops (the turtle's wall),
left it its outposts and lost. Against the CPU the same captures cost the
attack. Then the second failure (`CAPTURE_ALL` 3 against 2) on STRAND,
VULKAN, STADT, EISFREI and OASE against the CPU and STRAND, STADT,
EISFREI and WALDUNG against the turtle: 84-64 to 113-40 and 77-93 to
96-75 (STRAND as P1 against the CPU 1-31 to 20-12, where it had sent 17
INFANTRY at one outpost the CPU kept and was overrun in 11 minutes;
WALDUNG as P1 against the turtle 2-28 to 19-11). `FRONT_OPEN` and
`CAPTURE_AHEAD` stay, 0.

**In all** (32 games each, all eight maps, both seats). The bot against
the original's AI, won-lost(-drawn); "before" has the hover, walking and
low-ground fixes (without them ABGRUND alone was 22-10 as P1):

| map | as P1, before | now | as P2, before | now |
|---|---|---|---|---|
| ABGRUND | 32-0 | 32-0 | 31-1 | 32-0 |
| VULKAN | 24-8 | 28-4 | 11-21 | 25-6-1 |
| LOCH | 32-0 | 32-0 | 30-2 | 32-0 |
| STRAND | 1-31 | 20-12 | 32-0 | 32-0 |
| STADT | 12-19-1 | 23-9 | 9-23 | 25-6-1 |
| EISFREI | 32-0 | 32-0 | 16-16 | 24-5-3 |
| WALDUNG | 20-12 | 32-0 | 29-3 | 32-0 |
| OASE | 31-1 | 32-0 | 11-18-3 | 21-8-3 |

353-155-4 to 454-50-8. As the CPU's brain (`nm`): 508-2-2 to 512-0.
Against the turtle (the turtle with the old capture rules, both sides
with the rest of this session's changes), with the old capture rules
against the new: 206-184-122 to 230-178-104; WALDUNG as P1 1-25-6 to
19-11-2, STADT as P1 7-15-10 to 21-8-3, as P2 18-9-5 to 19-10-3,
EISFREI as P2 0-18-14 to 11-20-1, STRAND as P2 21-11 to 28-4, VULKAN as
P1 32-0 to 24-1-7, WALDUNG as P2 16-16 to 2-30.

`make nightmare` 64-0 both seats; `make ahead` PASS; `make unitcap`
PASS; `make replay_check` all recorded; `make netplay_check` all alike.
The N line adds `converted` and `gaveup`.

## Every difficulty, boats and guards of every kind (session 68 (9))

The owner, shown 512-0 and 454-50 at TYPE A: "Not good enough,
nightmare should never lose to CPU on any level on any hardness A-D".
So the set was played at every difficulty (`--difficulty 0..3`). As
the CPU's brain the difficulty changes one thing: a CPU's jet carrying
burns half the fuel on TYPE A only (both sides are CPUs, so none of the
perks below), and B, C and D play alike. On a human's pad the original's
AI gets its perks against it: its jet's carrying fuel not halved past A
(the human's jet burns one more on every form, all difficulties), a CPU
unit's hit halves the HP of a unit off the human's screen (all
difficulties), on A and B its shots x 1.5 while its HQ is low, on C and
D its shots x 1.5 and x 2 and its HQ taking 25% and 50% less from the
human's side, and it starts with units by its HQ and outposts: TYPE A
AMR-11s, B SAM-42s, C TAX-52s, D GRM-34As (which outrange the mech: its
gun 120 px, theirs 148, their missiles 254).

Every set below is all eight maps, both seats, 8 games each (128 a
line, `--fixes b7`). On a human's pad, with the rules of session 68
(8), TYPE D went 24-71-33, its HQ lost in most of those with the
CPU's all but whole. The turtle on TYPE D (on a human's pad, its own
way) did worse still: 19-76-33.

**Guards before a fight, from any unit waiting** (`DEFEND_FIRST` 2). On
LOCH (TYPE B) an INFANTRY ready for an outpost waited 25 minutes while
the jet fought at home, and no guard could be bought: under pressure
the unit waiting is made a guard whatever it was for (1: the attack
unit only, as before).

**The carrying jet with nowhere to put down.** On WALDUNG (TYPE B) it
stood carrying over the forest 45 s, pressing nothing, while 20 units
took its HQ: with no landing place within 96 px it flies on towards the
fight.

**An attack unit at least every 3 minutes** (`ATTACK_GAP` 10,800
frames). On WALDUNG (TYPE B) it sent 48 INFANTRY at outposts and one
attack unit, and lost: no attack unit put down for that long, it takes
no more outposts for the front (as `FINISH_HP`). 18,000 was not enough.

**Fights it can't have.** On STRAND (TYPE B) a boat shot its HQ from
1,024 HP to 0 while the jet hovered over the water by it for ten
minutes: no land within 112 px to fight from, it flew to the spot above
it, hovered, timed out after 30 s and took it up again. Now a target
with no landing place 40 to 112 px off (`fight_place_any`, wherever it
last stood stuck too) is left alone for 30 s (`nofight`, four kept; N
line `unreachable`).

**Guards of every kind** (The owner: "Imagine placing boats, tanks and
turrets defending your base instead of trying to fire into the water as
a mech"). A boat with attack HQ within 288 px of the HQ (its gun reaches
224 px) makes a spot too. A spot on the water is guarded by TURRET-2
(TURRETS on) on the shore within 192 px of it, else by a boat, put down
on the water between the enemy boat and the HQ, 96 px from the enemy's;
a spot on land by TURRET-1 (TURRETS on), else a TAX-52, as before. A
spot's guards are those of its kind within 64 px (a long gun, a boat's
or TURRET-2's, within 160 px for one on the water), and one more is
bought for a spot none of its guards reaches, up to one a spot past
`DEFEND`. A TAX-52 made a guard from a waiting unit goes to a land spot,
or the wall. The N line adds `boats` and `turrets` (put down as
guards).

**Boats to attack with** (`NAVAL` 1). On STRAND every base is an island
of its own. Past TYPE A the CPU's SAM-42s (GRM-34As on D) cover its HQ's
island, no attack unit was put down there, and they went down on other
islands and drove till their fuel ran out: 0 won in 64 on B and D. Now,
where the map sells the ST-57U, no land a unit drives to the enemy HQ
from is out of missiles, and water is from which a boat sails into its
gun's reach (`g_water`: blocks of water from those within 200 px of the
HQ), the attack unit is a boat, put down on that water out of missiles
(no drive test). Its place must be flown to straight and out of
missiles, from the mech or a base whose full tank reaches it: planned
on the straight way, the jet went round the SAM-42s, turned back for
fuel each time, and carried one boat 40 minutes. The N line adds
`naval` (boats bought to attack). STRAND, 16 games each seat:

| | before | boats |
|---|---|---|
| TYPE A | 26-6 | 29-3 |
| TYPE B | 0-30-2 | 28-4 |
| TYPE D | 0-32 | 13-19 |

**A unit dry, lifted and filled.** On VULKAN (TYPE A, as the CPU's
brain) an attack unit stood empty 30 minutes 1,100 px from the enemy
HQ: a stalled unit was one burning fuel, never one with none. Now a dry
one is stalled at once and lifted, and an attack unit on half a tank or
less is filled at the nearest base first (a base tops up the carried
unit as it does the mech).

**The nest test on** (`NEST` 448; 0 before). Past TYPE A the CPU
starts with SAM-42s (GRM-34As on D) by its HQ and outposts, and they
close every drop by its HQ: with none passing, a unit is put down by
one of them as the nest test does (session 68 (6)), and goes for it.
The bot, 8 games a set: TYPE A 114-14 to 115-13, B 88-23-17 to
102-13-13, D 29-71-28 to 35-60-33. hz_arena with `HZ_ARENA_SLOW` set
times every frame, both sides' thinks with it (an S line): on TYPE A
the slowest 4.2 ms, none over 16 ms.

**The guard it never fetched.** On EISFREI (TYPE D) one TAX-52 shot
its HQ from 1,024 to 0 in 25 s while the jet hovered over it, full of
fuel, money unspent. Its HQ being shot, decide put the guard waiting
first and set off to fetch it; but an attack unit was stalled, and the
rescue's check sent the jet back to decide before the pickup ran, every
frame. The rescue now waits for a guard.

**A drive round a lake.** On EISFREI (TYPE D, the bot as P1) nine
TAX-52s stood dry at once in the pocket of a lake by an outpost
(752,656), put down where the drive test had passed: a unit still
driving when its time was up passed, and these drove round the pocket.
Now one still driving must also be `DRIVE_GAIN` (128) px nearer the
enemy HQ than where it was put down. That game went from lost at 23
minutes (57 units seen stalled) to drawn (12); the six sets that lost
most on TYPE D (8 games each), 3-40-5 to 3-34-11. All maps both seats:
TYPE A the same set for set, B 102-10-16 to 108-3-17, C 55-60-13 to
56-61-11, D 35-56-37 to 34-55-39.

**What didn't help.** Without a CPU's hits halving its units (a test
build) the bot on TYPE D went 24-71-33 to 35-65-28 (8 games a set):
keeping its units on its screen is worth little. A garrison of 6
(`GARRISON`) on the worst TYPE B sets: 22-25-9 to 17-25-14. The turtle's
way (style 1) on TYPE D: 19-76-33, the bot 24-71-33. And five things
tried on TYPE D for what was seen in its games, none kept: together
24-72-32 against 35-56-37 (8 games a set, all maps both seats), and
each left out alone on the six sets that lost most no better than all
in.

- Errands before fights. On EISFREI the mech fought the CPU's stream of
  units 22 minutes on end and killed 200 of them, with a unit ready at
  a base all that while (none is bought while one waits) and its money
  up from 4,000 to 24,000; the HQ fell. A unit ready 15 s, or 1,500
  money with nothing on order, fetched or bought before any fight short
  of the HQ's alarm: it lost the outposts it used to keep, fetching
  instead of fighting by them (VULKAN as P1: 7-0-1 to 2-2-4). At a
  minute and 4,000, no better.
- No hunting GRM-34As by its outposts (one can't move, and outreaches
  the mech: on EISFREI it flew at one a dozen times).
- A guard's trip on a quarter of the fuel margin, set off from a base
  as soon as that is in the tank. On STRAND a CPU boat 180 px off its
  HQ shot it from 1,024 to 200 by itself, and the guard boat bought for
  it was carried to the HQ to be filled to the brim first, in the
  boat's reach, and shot down with the jet.
- CPU boats left to guard boats on TYPE C and D (on STRAND the mech
  went for that boat from the shore and was shot down twice in two
  minutes): on OASE, where it wins those fights, 3-11-2 against 6-2-6
  without.
- An attack unit full at a base with nowhere in reach made a guard (on
  EISFREI ten stood out a game in a lake's pocket by the outpost they
  were carried from): never so in the games played. They were put down
  where the drive test had passed.

Four more, after the two fixes above, none kept:

- The outpost cleared first. On STADT (TYPE A, as P1) the bot sent 43
  INFANTRY at the CPU's outposts in a game, most shot by the AMR-11s
  guarding them, held 2 or 3 to its 6 or 7 and lost with 13,252
  unspent. Enemy units within 160 px of the outpost it was taking
  fought as if by its own: as P1 4-4 to 7-0-1, as P2 7-1 to 3-3-2 (the
  mech fought there on end as the CPU sent more). STADT, 16 games a
  seat: none 27-5, always 21-5-6, only while an INFANTRY waited for it
  below `CAPTURE_MIN` 26-4-2.
- GRM-34As as guards with 3,000 money or more (on TYPE D the CPU's
  outrange its units): the six sets 3-41-4; it seldom had the money.
- The CPU's carrying jet chased on a human's pad too. On EISFREI (TYPE
  D) one TAX-52 put down 80 px off its HQ took it from 1,020 to 96 in a
  minute, the mech away: the six sets 5-34-9.
- Waiting over its HQ, not the nearest base. On WALDUNG (TYPE D, as P2)
  it spent 58% of the game waiting at an outpost 1,300 px from home,
  its HQ losing HP in chunks the while: once its HQ was shot 2-37-9,
  always 3-37-8.

**In all**, 8 games a set, all maps, both seats (won-lost-drawn):

| | as the CPU's brain (`nm`) | on a human's pad (`bot`) |
|---|---|---|
| TYPE A | 128-0 | 114-14 |
| TYPE B | 127-0-1 | 108-3-17 |
| TYPE C | as B | 56-61-11 |
| TYPE D | as B | 34-55-39 (24-71-33 before) |

As the CPU's brain it never lost. On a human's pad it still does: on
TYPE A STADT as P1 (4 of 8), OASE as P2 (3), EISFREI as P2 and WALDUNG
as P1 (2), one each elsewhere; on B STRAND as P1 (2) and EISFREI as P2;
on C and D on every map, both seats, but WALDUNG as P1 (and VULKAN as
P1 on D), worst EISFREI, STRAND as P1 and WALDUNG as P2 (0 to 3 won of
8). On D the CPU's shots hit twice as hard and its HQ takes half: one
TAX-52 of the CPU's put down by its HQ takes it in a minute, and its
own attack units barely mark the CPU's (on WALDUNG as P2, 9 to 72 put
down a game, the CPU's HQ left on 912 to 1,024). Against the turtle
(TYPE A, 16 a set, all maps both seats) 117-100-39, where session 68
(8) had 230-178-104 in 32 a set.

hz_arena's trace with `HZ_ARENA_UNITXY` set adds each unit's place, fuel,
ammo and (session 68 (10)) missiles; with `HZ_ARENA_TARGET` too (session
68 (11)), its target `{t kind:side:slot d direction + 1 s shot out fd
fire delay}`.

## A blind spot in UNITS, a garrison on TYPE C and D (session 68 (10))

The owner: "keep going on C/D". The probe sets below: on TYPE D
VULKAN, LOCH, EISFREI and WALDUNG as P2 and STADT as P1, on TYPE C
EISFREI as either and STRAND as P1, 8 games each.

**Units see an enemy in their own block** (`unit_ai.c`,
`crowd_pick_enemy`). A unit looks for a target in the rings of 32 px
blocks round the block of its centre (2 rings for a TAX-52, an
AMR-51D, a SAM-42, INFANTRY, an FWA; 4 for the rest), never at that
block: in the original only the unit itself is ever there, as a unit
is stopped where the block of its centre is taken. With UNITS units
collide by their boxes, and an enemy's centre can share the block. On
EISFREI (TYPE C, as P1) an AMR-51D stood 24 px from one of the bot's
standing TAX-52s for over 800 frames, both at full HP, shooting the
HQ, and the TAX-52 never fired. With UNITS the block of its centre is
now looked at first. `NET_GAME_CODE` 8. Both sides see more; the probe
sets, D 3-23-14 to 3-26-11, C 0-24 to 0-20-4 (EISFREI as P1 0-8 to
0-4-4).

**A robot on low ground** (`take_off`). It can't take off there, and
walks to the nearest plain land first. On LOCH (TYPE B, as P2) one in a
dip by the map's edge was given one of two places by turns as it
stepped a pixel each way, and stood 75 s while two units took its HQ
from 1,024 HP to 0, 18,000 money unspent. The place is kept 3 s, and
one it got no 8 px nearer in them passed over 30 s.

**`DRIVE_GAIN` on a human's pad only.** As the CPU's brain on EISFREI
(TYPE B, as P2) 391 of its 399 drive tests failed it, 3 attack units
put down in 42 minutes, and it lost; without it, won in 12 minutes
with 16. All maps, both seats on TYPE B: the CPU's brain 127-1 with it,
128-0 without; on a human's pad 108-10-10 with it, 95-14-19 without
(EISFREI as P2 5-2 against 0-6). Measured along the ground's drive
(`g_ground`) rather than as the crow flies it failed the same 391.

**A garrison on TYPE C and D** (`GARRISON_CPU` 8). On a human's pad
against the CPU on TYPE C and D it keeps 8 standing units round its HQ
the wall's way (`WALL_R` out, the half facing the enemy, SAM-42s and
TAX-52s by turns) before attack units. The six worst D sets (as in
session 68 (9)): none 4-37-7; 4 4-30-14; 8 7-27-14; 12 5-30-13; 16
3-27-18. The SAM-42s are what helps, shooting the CPU's carrying jet:
with none of them 4-37-7, two 7-35-6. All maps (before the UNITS fix):
C 56-61-11 to 55-57-16, D 34-55-39 to 32-47-49.

**No outpost its enemy guards** (`CAPTURE_GUARDS` 2). On EISFREI
(TYPE C, as P2) it sent 9 INFANTRY one at a time at an outpost three of
the CPU's TAX-52s stood by on guard, 9 to 74 px off, each shot as it
came, while its money piled up and the CPU's HQ went untouched; it lost
in 7.6 minutes. On a human's pad against the CPU on TYPE C and D it
takes no outpost with 2 or more of the enemy's armed units within
128 px. All maps, both seats: D 33-47-48 against 26-56-46 (OASE as P2
6-0 against 1-6), C 55-54-19 against 57-54-17. With 1 the probe sets
did no better (D 7-21-12, C 0-22-2 against 5-23-12, 1-20-3).

What the CPU does on TYPE C and D: on EISFREI it put AMR-51Ds down one
at a time 150 px off the bot's HQ, each taking 100 to 150 HP before the
mech came back from a service 575 px away; a TAX-52 of its 97 px off
took the HQ from 799 to 190 while the mech, shot to 64 HP on its way
there by missiles, died, came back, and spent 6 s filling up at the HQ
with a guard for 130 px off. Carrying a unit to the bot's HQ, its jet
puts it down at once when the bot's mech is within 400 px of that HQ
(WATCH_DELIVER), wherever the jet is.

Tried and not kept (the probe sets unless said):

- SAM-42s first in the garrison: 8 of 8 6-32-10, 6 of 8 6-26-16, 8 of
  12 7-30-11 (the six D sets, against 7-27-14).
- A ring 104 px all round the HQ: 5-33-10. SAM-42s on the wall and
  TAX-52s on the ring: D 3-28-9 and (12, 4 SAMs) 0-33-7 against 3-23-14.
- A SUPPLY with the garrison (its SAM-42s ran low, 0 to 8 missiles of
  10 left): 6-33-9.
- Fights from farther (`FIGHT_RANGE` 104: D 3-31-6, C 1-21-2; 112: D
  1-33-6, C 0-24).
- GRM-34As in the garrison with 3,000 money (D 3-26-11, C 0-24), one
  more for each 3,000 unspent (2-26-12, 0-24), both (3-27-10, 0-24).
- Out of the enemy guns' sight. A unit shoots only what its sight's
  blocks hold, so 3 blocks off a TAX-52 (65 px or more) the mech's gun
  (120 px) hits it and it can't shoot back. Landing and standing so
  where it could: the mech died less (58 deaths against 71 on EISFREI
  as P1, C) but killed a quarter as much (188 against 675), and its HQ
  fell sooner: D 0-38-2 against 3-26-11, C 0-24 against 0-20-4.
- A guard's or wall unit's trip on a fuel margin of 64, not 210: D
  3-27-10, C 0-21-3.
- Attack units in groups of 4 (`GROUP` 4; on STRAND as P1, TYPE C, 28
  put down one at a time in a game took nothing off the CPU's HQ, three
  TAX-52s and a SUPPLY on guard by it): D 2-30-8, C 1-22-1 against
  5-23-12, 1-20-3.

**In all**, 8 games a set, all maps, both seats (won-lost-drawn):

| | as the CPU's brain (`nm`) | on a human's pad (`bot`) | session 68 (9) |
|---|---|---|---|
| TYPE A | 128-0 | 116-9-3 | 114-14 |
| TYPE B | 128-0 | 108-10-10 | 108-3-17 |
| TYPE C | 128-0 | 55-54-19 | 56-61-11 |
| TYPE D | 128-0 | 33-47-48 | 34-55-39 |

As the CPU's brain it never lost. On a human's pad 120 lost of 512
against 133; TYPE B went back from 3 to 10 with the UNITS fix
(108-9-11 straight after it), on STRAND, LOCH and EISFREI. On TYPE C
every set lost a game but OASE as P1 (8-0), worst STRAND as P1 (0-8),
EISFREI and WALDUNG as P2 (0-6); on TYPE D only VULKAN as P1 (8-0) and
OASE as P2 (6-0-2) lost none, worst WALDUNG as P2 (0-7), and 48 were
drawn (the arena's hour).

## TYPE D's ring of GRM-34As (session 68 (11))

The owner: "continue cores are free". The probe sets as in (10);
nothing tried here was kept, and the AI plays as session 68 (10) left
it.

**What TYPE D is.** Against a human the CPU starts with the start
units' GRM records as GRM-34As (`units_spawn_cpu_start`; A AMR-51Ds, B
SAM-42s, C TAX-52s): on WALDUNG three to five stand 99 to 108 px round
its HQ. A GRM-34A sees 4 blocks, a TAX-52 2: one coming in is shot
before it sees the GRM-34A, its twin guns doubled (80 a volley; a
TAX-52's 224 HP in three), and once in reach of the HQ it shoots only
at that (`fire_at_hq`). On WALDUNG (TYPE D, as P2) 16 to 92 attack
units were put down a game and the CPU's HQ ended on 962 to 1,024: they
got 4 to 6 blocks from it and died after 0 to 2 shots. Its units' shots
on the CPU's HQ are halved there and the CPU's on its own doubled
(`terrain.c`), so a TAX-52 of the CPU's takes 28 HP off its HQ a shot
and one of its own 7 off the CPU's. Nothing mends an HQ: a defence
alone only puts a loss off. In three losses traced (WALDUNG D, STRAND
C) 54 to 83 % of the HQ's HP went with the mech within 200 px of it.

The siege (`SIEGE`) never fights that ring: `siege_guards` passes over
a guard in another one's missiles' reach, and in the ring each is. The
mech can't outreach a GRM-34A either: its gun goes 120 px, the
GRM-34A's sight 4 blocks, and 10 hits kill one (144 HP, power 16)
while 4 of its volleys kill the mech on TYPE D. A jet's bullets hit
only the enemy jet.

Tried and not kept (the probe sets: D 5-23-12, C 1-20-3 without):

- A SAM-42 or GRM-34A by its HQ fought first. On STRAND (TYPE C, as P1)
  one of the CPU's stood 89 px off its HQ shooting it and the mech each
  time it came home: the mech filled up at the HQ under its missiles
  for over a minute, 20 to 112 HP, while it and an INFANTRY took the HQ
  from 727 to 0. `pick_threat` had passed it over for the guards
  waiting to go, and scores a SAM-42 a quarter (in its own missiles'
  reach, as every one is). One within 192 px of its HQ fought before
  guards with 96 HP or more, a service at a base out of its missiles if
  the fuel reached one: the probe sets D 5-24-11, C 2-19-3; all maps,
  both seats on a human's pad A 115-10-3, B 103-10-15, C 56-49-23, D
  34-48-46 (against 116-9-3, 108-10-10, 55-54-19, 33-47-48). As the
  CPU's brain it lost LOCH as P2 (seed 6) on B, C and D: a SAM-42 on
  patrol by its HQ held the mech, never caught, while a TAX-52 on 40 HP
  took the HQ from 439 to 5. Only those attacking its HQ, and one not
  killed in a fight's 30 s left a while: it lost that game sooner.
  (Without, it wins it in 45 minutes, its HQ on 61, 35,000 money
  unspent: a weak game.)
- Chasing the CPU's jet while it carries on a human's pad (`DOG_PAD`
  700): D 5-23-12, C 4-18-2. On WALDUNG it shot down 0 to 4 in a game.
- A ring of GRM-34As round its own HQ as the CPU has, 96 px out, all
  round and spread apart, bought before the garrison and attack units
  on TYPE C and D. In one game on WALDUNG (TYPE D, as P2) its HQ lost
  848 HP in the hour, a draw, where it fell in 16.5 minutes without.
  All maps, both seats, TYPE D: 4 of them 19-49-60 against 34-48-46 (it
  put losses off to the hour, and won less); the probe sets with 2 D
  0-29-11, C 2-18-4; with 4 C 2-20-2.
- The siege on a guard in another's missiles' reach while one of its
  own units stood within 128 px of it, to draw its gun (a unit's gun
  takes an enemy unit before the mech, `pick_enemy`): D 5-25-10, C
  4-19-1. On WALDUNG the CPU's HQ still ended on 992 to 1,024.

What's left for TYPE C and D on a human's pad is the attack: the ring
round the CPU's HQ broken some way, or another way to its HQ than
through it.

## Home first on TYPE C and D (session 68 (12))

The owner: "I think the strategy you're missing out on is secure enough
mini-bases to fund units at a reasonable rate. Keep an eye on enemy and
intercept if they threaten your base or mini-bases, defend what he tries
to take from you if it makes strategic sense, muscle him out of mini bases
by placing missle launchers where they interfere with oponents access. Go
after main base when oportunity arises. Some of it is just timing and
misdirection."

Measured first, on a human's pad against the CPU on TYPE D (all maps, both
seats, 4 a set, 13-31-20): the bot held 3.5 outposts on average to the
CPU's 3.8, behind on 10 of the 16 sets, but money was not what it lacked:
it ended games with 5,000 to 38,000 unspent (8,000 to 10,000 most of an
hour on EISFREI). What it lacked was the mech at home:

- Its attack units died at the CPU's GRM-34As round its HQ (68 (11)) and
  the jet ferrying them was 700 to 1,800 px away when one of the CPU's
  TAX-52s came down by its own HQ; one of them alone takes it in a minute
  on C and D.
- A SAM-42's missile hits for 3 x its power (72), x 2 on D: 144 of the
  mech's 256 HP, a GRM-34A's 120. Missiles hit the robot as well as the
  jet (the robot is spared only the jet's bullets). On EISFREI (D, as P2)
  26 of the mech's 33 deaths in one game were SAM-42 missiles, most of
  them at its own HQ, hovering to be serviced or taking off: a take-off
  is 19 steps, 11 of them 6 frames (74 frames), a landing 38.

Kept: `GARRISON_CPU` 40 (was 8). On a human's pad against the CPU on C and
D the garrison (standing units round the HQ, SAM-42s and TAX-52s by turns,
68 (10)) comes before any attack unit, so for most of a game there is
none; outposts are still taken and guards still bought for where the HQ
was shot from. All maps, both seats, 4 a set: D 5-14-45 (from 13-31-20),
C 18-22-24. The CPU's brain and TYPE A and B are untouched. Every unit
to the garrison and no attack unit at all but a boat (whose 224 px gun
outreaches a GRM-34A's 160): D 3-11-50, C 0-19-45, the same losses and
no wins; with 40 standing it goes after the HQ again, and on C that wins.

Still lost on D: STRAND both seats, STADT as P1, OASE as P2. On OASE the
CPU's boats shoot the HQ from the water 185 to 264 px off, out of reach
of the garrison and of the boats guarding the spots it learned (a boat
sees 4 blocks); on C the CPU comes early (EISFREI as P2, lost in 5.7
minutes: one TAX-52 with a SAM-42 behind it, the mech back from a capture
1,800 px off and driven away by the missiles).

hz_arena: the R line is followed by `P avg1 avg2`, each side's outposts
held, the game's average; the trace adds an `O` line, each outpost's
owner; `HZ_ARENA_MECHHITS` prints an `MH` line for each hit on a mech
(what hit it, from how far, and whether it killed).

Tried and not kept (the probe sets, TYPE D 8 a set, 5 sets: 5-23-12 as
it was; C 3 sets: 1-20-3):

- SAM-42s by its outposts (the "missile launchers" for its mini-bases),
  one each, the outpost nearest its HQ first: D 2-28-10, the outposts held
  the same (3.0 to 5.0 a set against 3.2 to 5.0). The nearest the enemy
  HQ first: on EISFREI 26 mech deaths carrying them there and one attack
  unit put down in the game.
- Chasing the CPU's carrying jet when it is within 400 px of one of its
  bases (the HQ, its outposts): D 5-26-9, C 3-16-5.
- Serviced and waiting at a base out of the enemy's missiles when the fuel
  reaches one: D 6-27-7.
- No fight with anything in an enemy SAM-42's or GRM-34A's reach: on
  EISFREI (D) the HQ fell in 10 minutes; the CPU's attack units come with
  SAM-42s, and the mech's fights under them are what kept it.
- The garrison of TAX-52s alone: D 5-28-7; its ring at 112 px: 7-21-12;
  both: 2-27-11; all the way round the HQ: 3-24-13 (112 px), 2-26-12
  (160).
- With the garrison of 40: 8 guards for the spots (`DEFEND`): 3-10-27
  against 1-8-31; SAM-42s and GRM-34As not sent at the HQ left alone (on
  STADT a GRM-34A standing 257 px off the HQ, out of its guns' reach,
  killed the mech 57 times: it fought it, took off hurt, was shot down):
  1-27-12; a service broken off for the HQ's alarm: 1-20-19; the spots'
  guards counted apart from the garrison's (which otherwise count as
  guards, so none is bought for a new spot): 1-10-29; with every unit to
  the garrison but boats, all maps 3-11-50 with it and without.

## A garrison against a human (session 68 (13))

The owner's game of 30 October (ABGRUND, TYPE A, the server's AI as
P2, lost at 74:26), played again (`hz_replay`): by minute 30 he had 56
standing units round his HQ, and the AI had sent 38 TAX-52s at them one
at a time (one push took 302 HP); in minutes 31 to 35 his 39 INFANTRY
took its four outposts; then his SAM-42s stood 400 to 800 px from its
HQ and 59 of his 69 GRM-34As within 400 px. Its mech died 208 times,
175 shot down as a jet, 100 of them within 200 px of its own HQ, 6 to 8
a minute from minute 47; from minute 40 to 50 it bought nothing, with
5,000 to 8,000 unspent. Its attack drops went out from 640 px of his HQ
to 930, 1,220 and 1,380 as his SAM-42s spread, and 958 of 987 drive
tests failed: with none passing, a unit is put down where the best
untested place is, and those stopped behind the river and the cliffs
(The owner: "your AI will put units down set to attack base sometimes
where they are blocked by cliffs and a river").

The turtle (hz_arena's stand-in for a player) beats it the same way:
the bot against it on all maps, both seats, 8 a set: 54-51-23, ABGRUND
as P2 0-8 in 17 to 25 minutes. One TAX-52 put down 90 to 160 px from
the HQ takes 70 to 100 HP before it is killed; the bot's two to four
guards stand 124 to 155 px out and see 64 px, its mech 500 to 1,000 px
away.

**Kept: 40 standing round its HQ against a human** (`GARRISON_HUMAN`
40, on a human's pad against a human: online), and from half of it
every third buy an attack unit (`ATTACK_MIX` 3, against a human only).
Against the turtle, all maps, both seats, 8 a set:

| | won | lost | drawn |
|---|---|---|---|
| as it was | 54 | 51 | 23 |
| garrison 40 | 12 | 15 | 101 |
| garrison 40, every third an attack unit | 17 | 12 | 99 |

ABGRUND as P2: 0-8 to 3-0-5 (a garrison of 12: 0-8; 20: 1-3-4; 40
alone: 0-0-8). Against a turtle with a wall of 50 (`--param1 wall=50`):
0-8 to 0-0-8. Still lost: STADT as P2 (4), OASE as P1 (5), EISFREI as
P2 (2), WALDUNG as P1 (1); most games are draws at the hour. Against
the CPU it plays as before (the same games).

Tried, off:

- `NO_BLIND` 1 (an attack unit only where its drive passes, else made a
  guard; none bought while none passes) with `NO_NEST_FIGHT` 1 (the
  mech never goes for a SAM-42 or GRM-34A): against the turtle 7-10-111
  with the garrison; ABGRUND as P1 8-0 to 0-0-8, a unit put down where
  the drive test failed still getting there. Alone against a turtle of
  50 on ABGRUND as P2: no change (its drives mostly pass there).
- `GARRISON_MATCH` (the garrison a share of the enemy's standing units
  by its HQ, up to `GARRISON_CAP`): 80% against the turtle on ABGRUND
  as P2 1-6-1; against a wall of 50, 0-0-8 as 40 does.
- On TYPE D against the CPU, the four sets still lost (STRAND both
  seats, STADT as P1, OASE as P2), 8 a set: 5-23-4 as it is;
  `HOME_FIGHT` 320 (only enemy units that near its HQ fought) 7-20-5
  (OASE as P2 0-8 to 1-4-3, STRAND as P2 3-5 to 1-7); `READY_FIRST` (a
  wall or guard unit waiting picked up before a fight short of the
  alarm) 5-24-3; both 5-24-3.
- `BOAT_HOME` (boats of the garrison on the water nearest its HQ, one
  for every 4 of it): OASE as P2 on D 0-8 to 3-4-1 with 3, 1-4-3 with
  6; STRAND none (the CPU's boat shoots its HQ from 192 px at a minute
  and a half, before any garrison); EISFREI as P1 0-0-8 to 0-1-7.

hz_arena: `--replay FILE --takeover FRAME` plays a recorded game to that
frame, then both sides are the kinds named (the RNG reseeded from
`--seed` there); played to its end, the 30 October game ends as
recorded. The turtle's `NEST_UNITS` (style 1): once its wall is up,
that many GRM-34As and SAM-42s (every third) put down 224 to 352 px
from the enemy HQ before its attack units; against it (40) the bot won
8 of 8 on ABGRUND as P2, its mech never building the nest he did. The N
line adds `kept` (attack units made guards, `NO_BLIND`).

## The atlas, and the unit put down before the jet goes (session 68 (14))

The owner: "It's not just about seeing the tank for n seconds it's
about creating a working knowledge of the terrain basically an internal
map of viable drop sites to reach target. There are lots of places where
vehicles just get stuck because their navigation AI isn't good." The
drive test (session 68 (5)) tried a place when the AI was about to use
it, four a think, 3,000 frames each, so most places were never tried
and, none passing, a unit went down untried.

**The atlas** (`src/game/nm_atlas.h`, made into `src/game/nm_atlas.c` by
`make atlas`: `tools/hzatlas.py` running `hz_arena --map M --atlas` on
each map): for every map and side, every block of plain ground on the
map, the game's own unit AI drives an attack-HQ TAX-52 from it alone on
the map (the drive test's copy: no other unit, the mechs gone, the
outposts nobody's), three times with random numbers of its own, up to
7,200 frames each; it fails one that dies, runs dry and doesn't hit the
HQ in 240 frames, or moves less than a block in 1,800 frames (a slope
pushing it back, a cliff in front). A block's byte is the slowest
drive's frames to its first hit / 32, or 255 failed. All 8 maps take
under a minute on two cores; 38,432 blocks tried, 22,425 reach the HQ
(within 24 blocks of it, the drop search's reach, 7,718 of 8,795). On
ABGRUND, P2 attacking P1's HQ, the flat ground north-west of it behind
the river and cliffs is all failed blocks, as are pockets in the middle;
on LOCH no unit reaches P2's HQ from most of P1's own corner, from the
west and north they do. The answer is the terrain's and the unit AI's: a change to
either wants `make atlas` again. Its key: map, side, unit type and
`HZ_FIX_UNIT_AIM` (the default patches). (Session 71) and `HZ_FIX_PATHS`
(`NM_ATLAS_FIXES` $204): `make atlas` makes each map's sides with the
default patches and again with PATHS on (their names end in p), 76,864
blocks tried in all, and a game with PATHS reads the second (docs/paths.md:
7,212 blocks more reach the HQ, none fewer).

`ATLAS` 1: in the attack drop search a failed block is never a drop
(with the drive test or without, so no blind drop goes there either), a
tried one needs no drive test, and its score is its drive's frames
(twice, as the old ground distance's 64 a block) instead of the ground
distance; the drive test answers from it for a block's corner (`ATLAS`
2: for any place, from its nearest corner's). Against the CPU on TYPE D,
all maps, both seats, 8 a set: 16-32-80 with it, 13-28-87 without: there
the drops weren't what lost.

**The unit put down before the jet goes** (`BAIL_HP` 64): The owner:
"it is probably a good idea to drop a valuable unit everywhere if the
jet is about to explode in case the unit can be rescued later."
Carrying, under 64 HP (of 256; a CPU's missile takes 20 to 48 on TYPE C
and D) and hit in the last second or in missile reach, with ground under
it that takes the unit: the unit goes down there, whatever it was for.
On TYPE D against the CPU 74 units put down so in 128 games, deaths
carrying 132 to 85, 16-28-84 (from 16-32-80). The N line adds `bailed`.

**The garrison against a human, once its HQ has been shot**
(`GARRISON_SHOT` 1). Session 68 (13)'s garrison (40, `GARRISON_HUMAN`,
from the start) measured with the games let run three hours
(`--frames 648000`) instead of one: it won fewer than it drew, and lost
on maps where attacking had won (STADT as P2 6-2 to 0-8, EISFREI as P2
6-2 to 0-7, VULKAN as P2 7-1 to 1-5): building it, its attack stopped
(no attack unit delivered in a game on STADT) while 100 to 260 INFANTRY
a game went at outposts the turtle took back. Built only once an enemy
shot has taken HP off its HQ, it keeps most of the wins and loses as
seldom. Against the turtle, all maps, both seats, 8 a set, three hours,
the atlas on:

| | won | lost | drawn |
|---|---|---|---|
| no garrison | 70 | 52 | 6 |
| garrison 40 from the start | 42 | 31 | 55 |
| garrison 40 once its HQ is shot | 65 | 32 | 31 |

Without the atlas (session 68 (13)'s build, three hours): no garrison
66-55-7, garrison from the start 41-33-54. Still lost with the garrison
once shot: EISFREI as P2 0-7-1, WALDUNG as P2 0-6-2, VULKAN as P1 3-5,
LOCH as P1 0-4-4, OASE as P1 3-3-2.

With all of it, all maps, both seats, 8 a set: as the CPU's brain
against the original's A 128-0, B, C and D 127-0-1 each; on a human's
pad against the CPU A 117-7-4, B 109-6-13, C 36-37-55, D 15-29-84
(STRAND as P1 2-6, as P2 1-7, OASE as P2 0-8).

Tried, off:

- `GUARDS_HUMAN` 1 (`CAPTURE_GUARDS`' outposts with two enemy armed
  units by them left alone against a human too): 64-32-32 with the
  garrison once shot; the INFANTRY went at outposts the turtle retook,
  unguarded, as many as before.
- `ATLAS` 2 (the atlas's answer for any place): with the atlas then
  covering 24 blocks round the HQ, LOCH as P1 against the turtle played
  the same game (its 42 drive tests, 40 failed, were all further out:
  drops short of the fuel's reach); so the atlas now covers the map.
  Not measured since.
- `SPOT_COUNT` 1 (only its guards at the spots its HQ was shot from
  counted for `DEFEND`, not a wall's standing TAX-52s): on OASE as P2
  (TYPE D) twenty of the wall stood 96 to 167 px out while CPU units
  shot the HQ from 110 to 260 px on the other side, and no guard was
  bought. TYPE D: 17-32-79.
- `HOME_EARLY` 3 (on TYPE C and D against the CPU, three TAX-52s
  standing at its HQ before any other buy): on STRAND as P1 one TAX-52
  the CPU put down 140 px from its HQ took it in 18 seconds at three
  minutes, its first nine buys INFANTRY. TYPE D 19-28-81: STRAND as P1
  2-6 to 5-3, as P2 2-6 to 0-8 (lost later, 87,000 to 112,000 frames
  from 46,000 to 60,000).

## Muscled out of an outpost (session 68 (15))

The owner's game of 30 October, 10:43 UTC (ABGRUND, TYPE A, the
server's AI as P2, session 68 (14)'s build): he won at 38:23. What
happened:

- 0:00-3:00: each side took outposts, the AI four.
- 3:44-13:59: 19 attack units put down one at a time, 16 of them at one
  place 640 px from his HQ (the atlas's quickest drive), into the nine
  TURRET-1s and two TURRET-2s he put round his HQ from 6:19: 114 HP off
  it in all.
- From 13:13 he put SAM-42s down by its outpost at the far east of the
  map, 1,500 to 1,680 px from its HQ. From 14:00 to 16:00 it set out
  with a TAX-52 from that outpost 15 times: going round the SAMs took
  the fuel the last leg was planned with, the leg's check failed on the
  way (`needs_service`), and it went back to the outpost to refuel, being
  shot there.
- 16:19-19:50: destroyed 9 times, flying from its HQ to fight his units
  by that outpost each time it came back (`pick_threat`'s outposts,
  `HUNT_RADIUS`); `decide` takes a fight before a buy, so nothing was
  bought and its money went from 3,000 to 7,400.
- 20:13 and 20:41 his INFANTRY took two of its outposts; from 21:02 his
  TAX-52s shot its HQ, which fell over 17 minutes, the jet carrying one
  unit at a time the limit (5,000 to 7,000 unspent all the while).

**`hz_arena --replay FILE --takeover FRAME --shadow`**: a bot side thinks
before the takeover too, its pad thrown away (the recorded one played),
so `--trace` tells what it would do. This game replayed so ends as it
did (frame 138,277, HQs 910 and 0, 93 buys, 9 deaths), and its trace
showed both loops. Taken over at 16:00 (frame 57,600) with the turtle
playing his side, the AI went on going out and back with its TAX-52 and
the turtle's attack units took its HQ 70 s later (3 seeds, 3 times).

The fixes:

- `ROUTE_FUEL` 1: a leg from the mech planned on the flight it will
  make. `leg_ok` took the straight line's length, `fly_to` goes round
  the missiles (`plan_path`), so a leg planned at a base failed the
  check on the way and the jet turned back, again and again. Now, when
  the straight line crosses missiles, the length is the way round them:
  `route_build`, once a think when wanted, the steps from the mech's
  block to every block with `plan_path`'s costs. Legs from a base
  (`leg_ok_from`: the outpost and drop searches) keep the straight line.
- `BOUNCES` 2, behind it: an attack unit whose jet turned back for fuel
  twice on its last leg is made a guard (stand, put down at home), as
  `NO_BLIND` does with one that has no drop.
- `DEATH_AVOID` 10,800 / `DEATH_RADIUS` 288: where the mech was
  destroyed (the last 4 places), no fight away from the HQ (out of
  `DEF_RADIUS`) with an enemy unit within 288 px of it, for three
  minutes.
- `BUY_FIRST` 1: a unit bought before a fight away from the HQ.

From 16:00 again with the turtle: it held to the hour's end all 3 times,
its HQ 468 to 912 left. Without `ROUTE_FUEL` but with `BOUNCES` and
`SAFE_SERVICE` (below) 258 to 462; with only the two fight fixes it lost
as before, at 70 s; with only `BOUNCES` and `SAFE_SERVICE`, 4 to 10
minutes after the takeover. With `ROUTE_FUEL` no unit was made a guard
by `BOUNCES`.

The fight fixes first went in for every game: against the CPU on TYPE
D, all maps, both seats, 8 a set, 10-54-64 (from 15-29-84), C 28-59-41
(36-37-55). On four of those sets (EISFREI both seats, ABGRUND as P2,
WALDUNG as P1) the fight fixes off made it 4-2-26 (6-1-25 before), on
1-20-11; on only away from the HQ, 3-4-25. Against the CPU the
mech is destroyed by its HQ often (its missiles hit twice as hard on D)
and has to fight there all the same; on EISFREI as P1 the games are a
grind with the CPU's HQ untouched, lost one or two in 8 with any of it.
So the two are against a human only (`vs_human`): against the CPU they
play as before. With them so, `BOUNCES` 2 and `SAFE_SERVICE` 1, before
`ROUTE_FUEL`: against the turtle (itself as before), all maps, both
seats, 8 a set, three hours, 73-25-30 (from 65-32-31); against the CPU
D 14-28-86, C 35-38-55, B 109-7-12, A 114-9-5; as the CPU's brain A
128-0, B, C and D 127-0-1. Of TYPE A's two new losses, STADT as P2
(seed 2) was `BOUNCES`' (a TAX-52 for a drop 180 px from the CPU's HQ
made a guard after two turns back; `ROUTE_FUEL` wins it at 86,354
frames), ABGRUND as P2 (seed 5) `SAFE_SERVICE`'s.

Tried, off: `SAFE_SERVICE` 1 (refuelled at the nearest base out of the
enemy's missiles that the fuel reaches, if there is one): on ABGRUND as
P2 against the CPU (TYPE A) its HQ was in the CPU's missiles' reach,
and refuelling at the outpost instead lost a game won without it; from
16:00 in the owner's game it changed nothing with `ROUTE_FUEL`.

`COLD_JUDGE` (0, off): an attack unit put down and the enemy HQ shot by
nothing in that many frames after, no attack drop within `COLD_R` (160)
px of the place for `COLD_FRAMES` (10,800): its answer to the TAX-52s
fed one at a time into a wall of turrets. At 4,800 against the turtle
on the four sets it loses most (LOCH as P1, EISFREI and WALDUNG as P2,
OASE as P1), three hours: 6-19-7 against 2-19-11 without (WALDUNG as P2
4-4 from 1-5-2, LOCH as P1 0-6-2 from 0-4-4), 50 to 550 drops judged
cold a set: more won, no fewer lost. Not measured on the rest or against
the CPU.

**In all** (`ROUTE_FUEL` 1, `BOUNCES` 2, the fight fixes against a human,
`SAFE_SERVICE` and `COLD_JUDGE` off), all maps, both seats, 8 a set:

| | now | session 68 (14) |
|---|---|---|
| against the turtle (as before), three hours | 74-30-24 | 65-32-31 |
| on a human's pad against the CPU, TYPE A | 120-7-1 | 117-7-4 |
| ... TYPE B | 105-7-16 | 109-6-13 |
| ... TYPE C | 33-37-58 | 36-37-55 |
| ... TYPE D | 15-27-86 | 15-29-84 |
| as the CPU's brain, TYPE A | 128-0 | 128-0 |
| ... TYPE B, C and D | 127-0-1 each | 127-0-1 each |

Against the turtle the gain is on VULKAN (as P1 6-2 from 3-5, as P2 8-0
from 7-1), STRAND (as P1 6-0-2 from 5-1-2, as P2 6-0-2 from 4-0-4) and
EISFREI as P1 (3-2-3 from 1-2-5); worse on WALDUNG as P1 (5-3 from
5-0-3) and OASE as P1 (1-3-4 from 3-3-2); still lost: EISFREI as P2
0-7-1, WALDUNG as P2 1-5-2, LOCH as P1 0-4-4.

## Shot from behind (session 68 (16))

The owner's game of 30 October, 21:56 UTC (ABGRUND, TYPE A, the
server's AI as P2, session 68 (15)'s build): he won at 24:02, his HQ
never hit. What happened:

- 8:09: his first TAX-52, put down on the side of its HQ away from his,
  took 308 HP; the jet was fighting 300 px off.
- Its HQ shot, the garrison went up (`GARRISON_HUMAN` 40,
  `GARRISON_SHOT`): 41 units put down by its HQ, all on the side facing
  his HQ (`wall_drop`), 20 of them SAM-42s.
- 16:11-24:02: a TAX-52 of his every 25 to 30 s, 96 to 156 px from its
  HQ by the map's west and north edges, 17 in all: its HQ from 716 HP to
  nothing. Its tanks stood on the far side of the HQ, its SAM-42s shoot
  only the jet, and the jet, called back from its east outpost 1,100 px
  off each time, took them one at a time. It had learnt four spots its
  HQ was shot from, one of them guarded: `defend_wanted` counted the
  garrison's TAX-52s as guards (`all >= want`), so the others got none.
- Its own attack: 10 units, 611 of 615 drive tests failed against his
  SAM-42s, his HQ untouched. Its TAX-52s went down 650 to 720 px north
  of his HQ, drove into the ravine under the cliff by it (230 px off)
  and never fired; lifted, they were flown home to fill and put down
  there again.

Then the owner: "It doesn't do a whole lot of good to build up a
Garrison at the main base if the rear is left mostly open. Sometimes only
ordering Tanks set to attack base isn't a good idea. a Nest of Sam's
doesn't shoot at ground units, only GRM do. You should be discovering
strategies for killing troublesome units. Also your drop Atlas clearly
doesn't work on Abgrund, you dropped tanks in the ravine north of the red
base which could drive close to the base but never fire. ... the AI
picked them up before they were out of fuel and flew them back to refuel
instead of putting them up on the cliff where they could park and fire at
the base." And: "I don't think the rear garrison should be conditional
on the front being completed, the Garison should be dynamic based on
attacks or distributed while building." And: "I don't need to start as
player one all the time maybe the server should do a random roll?"

**The guards and the garrison:**

- `SPOT_HUMAN` 1: against a human, a spot its HQ was shot from that no
  guard reaches gets one, however many stand elsewhere. Shadowing the
  game (`--shadow`), it sends a guard to the back corner (66,12) right
  after the 16:11 hit; the old one went on walling the east side.
- `WALL_RING` 1: against a human, each wall unit put down on the rings
  `WALL_R` and 64 px in, every side, where its own units standing within
  128 px are fewest for what the place is worth (4, up to 4 more the more
  it faces the enemy HQ, 12 more within 128 px of a spot its HQ was shot
  from): the wall goes up all round as it is built, and a side shot from
  fills first (`ring_drop`). A first try filled the front half first,
  then the rest (68-34-26 against the turtle, 8 a set).
- `SPOT_WET` 1, every game: a spot on the water its HQ was shot from that
  no boat or TURRET-2 of its reaches gets one first (`spot_wet_open`). On
  OASE (TYPE D, as P2) the CPU's boats shot its HQ from the water 230 to
  290 px west of it, the 26 to 36 of its wall counted as guards, and no
  boat was bought in 38 minutes; its mech died 54 times fighting them,
  $20,000 unspent. Now it buys them (50 in the TYPE D sets), but one
  bought after the shooting starts dies to the CPU's: OASE as P2 still
  0-8.
- `HOME_SERVICE` 1: against a human, its HQ being shot (`hq_pressed`),
  the jet refuels at the HQ if the fuel reaches it, not at the nearest
  base. On EISFREI (as P1) two of the turtle's TAX-52s put down behind
  its HQ took it in 50 s while the jet refuelled at an outpost 850 px off.
- `BASE_LOST` 1, every game: a unit carried to a base to refuel at that
  the enemy takes on the way, the leg planned again (`task_deliver`'s
  stage 1; `task_service` did so already). On EISFREI the jet hovered
  over the outpost the turtle had just taken, the tank filling no more,
  till the task's time ran out (100 s), and lost its HQ in 9 minutes.

**The ravine.** The ravine drops were not the atlas's: `attack_drop`
chose a block the atlas passes, out of the fuel's reach, and
`task_deliver`'s stage 0 then put the unit down as far along the line as
the fuel went (`drop_ring`, no atlas), on plain blocks the atlas fails
(the drives from them end under the cliff). The ravine's floor is
ABGRUND's special ground (`--ground`'s `s`: only a person's mech puts a
unit down there), which the atlas doesn't try.

- `ATLAS_LINE` 1: against a human, that drop along the line only where
  the atlas doesn't fail, on plain land (`atlas_ring`: out of missiles,
  else not `drop_hot`, as `drop_ring`); none in reach from a base on a
  full tank, the unit a guard (`atlaskept` in the summary). Only on
  blocks the atlas passed, on EISFREI against the CPU (TYPE B) all its
  attack units were kept home, 55 to 66 a game (the line crosses the
  lake, and the shore's blocks are water at the corner the atlas tries):
  0-8 drawn as P1, from 8-0; then 7-0-1. Every game (2) against the CPU,
  8 a set: TYPE B 100-10-18 against 106-7-15 without, D 17-24-87 against
  17-25-86; so against the CPU it plays as before.
- `PARK` 25: an attack unit carried on half a tank or less (a lifted
  one) goes down at the block whose drive the atlas passes in the time
  its fuel lasts (its fuel use a frame, 25% to spare), out of missiles,
  not cold, in the leg's reach, the shortest drive and flight of those
  (`park_drop`, `parks`); only if there's none, to a base to be filled
  first, as before. In the arena it never plays: every unit lifted on
  half a tank or less there was dry (the stall watch lifts the rest
  after 2 s, with most of their fuel).

**Killing what's in the way, tried, off:**

- `HUNT` (0; `HUNT_NEAR` 512, `HUNT_COVER` 5): against a human, every
  `HUNT`-th attack unit a hunter: a TAX-52 told to guard where it is put
  down (guard near: it goes for an enemy it sees within 128 px of there,
  and back), put down 64 to 112 px from the enemy SAM-42 within 512 px
  of his HQ that the fewest missiles reach, where no enemy gun does and
  fewer than 5 missile units reach; one out at a time (`hunts`). A SAM-42
  shoots only the jet, and an attack unit drives the map's way past them
  80 px off without firing (it sees 64 to 72 px). Every third, against
  the turtle (whose wall is half SAM-42s), 8 a set: 72-31-25 against
  78-25-25 without (12 to 23 hunters a game); with `FLANK` 71-29-28
  against 74-19-35. That measured hunters that never reached a SAM-42:
  done again in session 68 (17), "Hunting SAM-42s".
- `MECH_FIGHT` 3 (the default stays 1): the enemy mech on foot fought as
  the CPU and against a human. On EISFREI (as P1) the turtle's robot
  killed 38 of its INFANTRY at outposts, and by 15 minutes it held one
  outpost to the turtle's seven, $78 in hand: 8 games 2-1-5 against
  2-4-2. Against the turtle, 8 a set: 75-26-27 against 78-25-25; with
  `FLANK` 81-21-26 against 75-22-31. Against the CPU on TYPE D (2,
  always) no better: OASE as P2 0-8 both, STRAND 1-7 against 2-6.
- `WALL_GRM` (the wall by thirds SAM-42, GRM-34A, TAX-52): 71-34-23
  against 78-25-25. `ATLAS` 2 (every place the atlas's nearest corner's
  answer): 72-26-30. `WALL_SPOTS` (each wall unit's place on both rings
  scored by how far round it is from the line to the enemy HQ or a spot):
  with `FLANK`, 16 a set, 151-51-54 against 155-42-59.

**`FLANK`** (0): the turtle's way of 21:56 to try against: an attack
drop is `FLANK` worse a px it stands towards its own HQ from the enemy's
(4 with `drop_range` 96 here): its units go down behind the enemy HQ,
out of its missiles.

**Boats at home on TYPE D:** `BOAT_HOME` 3 (it was 0) with `BOAT_SEEN`
1 and `BOAT_TYPE` 3: on a human's pad against the CPU on D, boats of the
wall on the water nearest its HQ (one for every 4 of it, 3 at most;
session 68 (13)), now only once an enemy boat has been on the map. On
OASE (as P2) one CPU boat 250 px west of its HQ takes it in 2 1/2
minutes on D, its hits doubled. On D, 8 a set, 17-24-87 against
14-28-86 without (OASE as P2 2-5-1 from 0-8, STRAND as P2 2-5-1 from
1-7); on C 32-38-58 against 32-37-59 (OASE as P2 3-0-5 from 2-3-3,
STRAND as P2 0-8 from 1-7), so not on C. Both worse on EISFREI as P1 (D
0-2-6 from 0-0-8, C 2-3-3 from 3-0-5), where the CPU's boats come onto
the lake 224 to 288 px from its HQ too. Boats always, as session 68 (13)
had them: D 19-23-86, C 33-42-53.

The drive tests' answers are kept across games (`g_drives`), so in
`hz_arena` a game's play depends on the games before it in the same
process: EISFREI seed 4 alone is lost at 9 minutes, after seed 3 drawn.
Sets compared are run the same way (`ab2.sh`, 4 processes). Single games
turn on any change: STADT as P2 seed 7 on TYPE B is won with
`ATLAS_LINE` and `PARK` both off, lost with either on, though `PARK`
never plays in it.

**The server's AI by a coin's toss:** hz_server's `--ai-side
random|p1|p2` (random the default): the person against the server's AI
plays P1 or P2 by a toss (`room_match_ai`; a rematch keeps the sides).

Against the turtle (pinned to the old ways, the new ones off for it),
all maps, both seats, three hours; against the CPU on a human's pad, 8
a set:

| | now | session 68 (15) |
|---|---|---|
| against the turtle, 8 a set | 78-25-25 | 74-30-24 |
| against the turtle with `FLANK` 4 (`drop_range` 96), 8 a set | 75-22-31 | 75-21-32 |
| on a human's pad against the CPU, TYPE A | 120-7-1 | 120-7-1 |
| ... TYPE B | 106-7-15 | 105-7-16 |
| ... TYPE C | 35-37-56 | 33-37-58 |
| ... TYPE D | 17-25-86 | 15-27-86 |
| as the CPU's brain, TYPE A | 128-0 | 128-0 |
| ... TYPE B, C and D | 127-0-1 each | 127-0-1 each |

Against the CPU only `BASE_LOST`, `SPOT_WET` and on D `BOAT_HOME` play
(the rest against a human): C's two more won are `BASE_LOST`'s; on D the
gain is OASE as P2 (2-5-1 from 0-8) and STRAND as P2 (2-5-1 from 2-6),
worse on EISFREI as P1 (0-3-5 from 0-0-8). On a human's pad it still
loses to the CPU on every TYPE: A 7, B 7, C 37, D 25 of 128.

Against the turtle the gain is on STRAND as P1 (8-0 from 6-0-2), STADT
as P1 (7-1 from 6-2), VULKAN as P1 (6-1-1 from 5-2-1), EISFREI as P2
(0-5-3 from 0-7-1), WALDUNG as P2 (2-4-2 from 1-5-2) and OASE as P1
(1-2-5 from 1-3-4); worse on EISFREI as P1 (2-3-3 from 3-2-3); still
lost: EISFREI, WALDUNG as P2, LOCH as P1 0-4-4.

## Hunting SAM-42s (session 68 (17))

The owner, on session 68 (16)'s hunters (tried, off): "I think your
cost test might be misguided. To reclaim an area swarmed with Sams can
be very important." The test had measured hunters that never reached a
SAM-42. Traced (STADT, against the turtle) they went down about 190 px
from the nearest, out of guard near's 128 px: the gun-reach check pushed
the drop out, and `task_deliver` puts any unit but an attack unit down
as far along the line as the fuel goes. One stood idle two hours, and
with one out at a time no other was bought.

The case to test it on: The owner's game of 30 October, 10:43
(ABGRUND, the server's AI as P2), taken over at 18 minutes (`--replay
--takeover 64800`), the turtle (pinned to the old ways) playing his side,
30 minutes on, 8 seeds. From 16 minutes 11 of his SAM-42s and a SUPPLY
stood 150 to 300 px west of its east outpost (1968,560), no gun near
them; 5 of them reach the outpost.

- `HUNT_HOME` 1: against a human, a hunter for an enemy SAM-42 whose
  missiles reach its own HQ (worth 4) or an outpost of its (2 each),
  bought before the garrison while no hunter is at one and its HQ hasn't
  been shot in the last minute (`hq_calm`). `HUNT` (every this-many-th
  attack unit a hunter, for SAM-42s within `HUNT_NEAR` of his HQ too,
  worth 1) stays 0.
- Put down 32 to 64 px from the SAM-42, in its sight (a TAX-52 sees two
  blocks). Put down 48 to 96 px off, they chased what they saw and ran
  dry in a minute (a TAX-52's tank drives 11 s: 1,364 fuel at 2 a
  frame), then stood 77 px from the next, out of sight, 20 minutes.
- `HUNT_COVER` 12 (was 5): fewer than this many enemy missile units
  reach the drop. Every place by a nest of 11 has 11 or more. Most
  hunters still go down by the jet's emergency drop (`BAIL_HP`) on the
  way in, near enough.
- Never put down short: none in the fuel's reach, to a base on the way,
  else made a guard at home; on half a tank or less, filled first, as an
  attack unit is.
- Carried while its HQ is shot (`hq_pressed`), made a guard. In seed 1
  the jet fought the turtle's tanks at its HQ 50 s with one aboard,
  buying no guard, and lost the HQ.
- `REHUNT` (0): an idle hunter (no enemy within 160 px, or a quarter of
  a tank and none in sight) lifted on to the next SAM-42 (`rehunts`).
  130 of his SAM-42s killed against 122, but its HQ ended on 415 HP
  against 699, the jet away. Without the minute's calm 6 of 8 were lost:
  the jet flew 1,000 px off to lift one between the turtle's drops at its
  HQ.

**The fight's hover.** In seed 2 the jet, fighting at home, swung 7 px
each way over the spot it had chosen to land on, among its own units by
its HQ, for 28 s, firing nothing, while one TAX-52 took the HQ from 854
HP to 84. `task_fight` counts a second at a spot it can't land on, then
picks another, but the count was put back on each swing (the jet not
slow). Now it runs while the jet is within 12 px of the spot. That plays
in every game.

The takeover, 8 seeds:

| | no hunters | `HUNT` 1 | `HUNT` 1, `REHUNT` 1 | `HUNT_HOME` 1 (now) |
|---|---|---|---|---|
| won-lost-drawn | 0-0-8 | 0-0-8 | 0-0-8 | 0-0-8 |
| his SAM-42s killed | 96 | 122 | 130 | 118 |
| ... by its hunters | 0 | 64 | 60 | 54 |
| its jet shot down | 92 | 61 | 68 | 70 |
| its HQ's HP at the end, mean | 741 | 699 | 415 | 631 |

In the arena `HUNT_HOME` never buys one: the turtle's SAM-42s stand in
its wall by its own HQ, and with `nest_units` 6 its nests round the
bot's HQ stand by GRM-34As, whose guns reach every drop by them. The
suites with it are the same as without it to the game. Every third
attack unit a hunter (`HUNT` 3) still costs wins against the turtle:
69-23-36 against 77-24-27, with `FLANK` 77-26-25 against 79-19-30.

Against the turtle (pinned to the old ways), all maps, both seats, three
hours; against the CPU on a human's pad, 8 a set:

| | now | session 68 (16) |
|---|---|---|
| against the turtle | 77-24-27 | 78-25-25 |
| against the turtle with `FLANK` 4 (`drop_range` 96) | 79-19-30 | 75-22-31 |
| against the turtle with `nest_units` 6 | 100-6-22 | |
| on a human's pad against the CPU, TYPE A | 121-6-1 | 120-7-1 |
| ... TYPE B | 104-9-15 | 106-7-15 |
| ... TYPE C | 34-37-57 | 35-37-56 |
| ... TYPE D | 15-27-86 | 17-25-86 |
| as the CPU's brain, TYPE A | 128-0 | 128-0 |
| ... TYPE B, C and D | 127-0-1 each | 127-0-1 each |

Against the CPU the hunters don't play (against a human only); the
change there is the fight's hover, within a set's chance either way.

## Round the missiles, and down on the move (session 68 (18))

The owner, after two games on 31 October (06:20 and 06:53, ABGRUND,
the server's AI as P1, both lost at 31 and 33 minutes): "you really need
to figure out that you can fly an arc around rocket launchers instead of
avoiding triggering their fire", and then: "You can even see in the
previous game how I swing around the back of the base and drop a tank
and continue circling away from base to return to my base. Why aren't
you learning from me?"

**The missiles, played out.** `tools/hzmissim.py` plays a SAM-42's missiles as `shots.c` and `unit_ai.c` have them:
fired every 32 frames while the mech is within $100 (Chebyshev), 16-way
aim from the launcher, 2 px a frame, aimed again every 4 frames, the
velocity steered $40 a frame an axis, life 255, hit box $18. Against a
jet flying straight on past a launcher:

| closest the jet passes | jet at 4 px a frame | carrying, 3 px a frame | carrying, stopped 300 px past it |
|---|---|---|---|
| 0 px (over it) | 3 hits | 3 | 5 |
| 64 px | 0 | 0 | 4 |
| 96 px | 0 | 0 | 4 |
| 128 px | 0 | 0 | 3 |
| 192 px | 0 | 0 | 2 |
| 255 px | 0 | 0 | 0 |

Half round it 128 px off, or at it and turned away 160 px off: none.
Four hits destroy the jet (72 each, 256 HP). A missile comes up behind a
jet going by and doesn't catch it; it catches one that slows.

**The drop.** `mech_drop` has no speed rule: A puts the unit down at
any speed and zeroes the jet's velocity, which is back at a missile's
2 px a frame in 21 frames and 21 px (it gains $18 a frame). The AI flew
to the place, braked to a stop over it, and dropped: the slowed jet
missiles catch. The owner's 32:20 in the 06:53 game, traced every 2
frames: up from the GRM-34A outpost (post 6), down the west side of the
AI's HQ, round behind it 78 px south, the TAX-52 down at 3.0 px a frame
(2.7 twenty frames on), up the east side and home: ten seconds, no hit.

- `ARC_CORE` 80: a flight's missile blocks (`MISSILE_COST` 60 a block,
  as before) only those within 80 px of an enemy SAM-42 or GRM-34A, and
  those within `ARC_GOAL` 10 blocks of the flight's goal (it slows
  there); the rest of their reach `ARC_RING` 3 a block. `line_safe`,
  `plan_path` and `route_build` alike (`missile_step`, `line_clear`).
- `DROP_FLY` 24: against a human, within 160 px of the place the jet
  heads for it without braking (`fly_through`: on the axis it is
  further along held towards it and on, on the other stopped on its
  line; `head_for`'s 8 ways came in diagonally and went by 40 px off),
  and the unit goes down at the first place within 24 px it may: an
  attack unit where its drive passes (in the place's block, the place's
  answer, the atlas's mostly), INFANTRY within 8 px (on the outpost, not
  by it). Stopped
  over it after all, or past it twice, as before (`flydrops`,
  `flymisses`).
- Against the CPU not. On TYPE A, every kind within 24 px and steered
  by `head_for`, it lost 21 of 128 against 6; INFANTRY within 8 px, 9;
  16 a set, 26 of 256 against 12. On the worst maps (1, 4 and 5, 64
  games) INFANTRY alone lost 2 against 3, attack units alone 7, and with
  `fly_through` 6: won slower, the jet destroyed twice as often (29
  against 15 on map 5).
- `FLY_KINDS` 7: which (1 attack units, 2 INFANTRY, 4 hunters).

**Repaired GRM-34As.** In the 06:53 game its mech went for the GRM-34As
on the owner's outpost 400 px from its HQ one at a time, destroyed 21
times in six minutes for 5 of them. They were looked after: one went
from 48 HP to 144 in ten seconds (lifted to the outpost and put back),
one from 16 to 144, and a SUPPLY came through at 30:25 and filled three
from 0 missiles to 20. The owner: "I don't think it's bad for NM to
fight the GRM because it eventually wares down an unattentive players
GRM", "But if they're on top of repairing them its less worth while".

- `TENDED` 3600: `track_units` keeps each enemy unit's HP and missiles,
  and the frame they last went up; the mech leaves alone an enemy SAM-42
  or GRM-34A repaired in the last minute whose gun doesn't reach its HQ
  (attacking it, or within its gun reach + 64 px). The turtle repairs
  nothing, so the takeovers never meet one; against the CPU the sets are
  the same to the game.
- `HOME_GRAVE` (0): `DEATH_AVOID` at home too for such a unit, repaired
  or not. Tried, off: it changed one seed of 8, and leaves an
  unattended player's GRM-34As standing.

**Tried, off.** `CAPTURE_HUMAN`: in both games outposts 896 to 1,300 px
from its bases stood empty 12 minutes (`CAPTURE_REACH` 760), and
The owner took one 928 px from the nearest of theirs. At 1,000 against
a human the takeover of the 06:53 game at 9 minutes went from 0-3-5 to
0-1-7, 3 to 5 outposts held; against the turtle from the start it lost
42 of 128 (24 before), and only while the enemy holds more outposts
(`CAPTURE_BEHIND`) 41. `SAFE_FLY` (with `DROP_FLY`, `SAFE_DROP` this
many and never in a core): no change to the game against the turtle.

**The takeover of a paused game.** the owner's 06:53 recording is
paused from about 25 minutes (its frames run on, the game's don't); a
takeover there never went on, the bots pressing no Start. `hz_arena`
now presses Start every other frame while a taken-over game is paused
(`snd_pause_sent` odd).

The owner's three games taken over (the 30 October 10:43 game at 18
minutes, the AI as P2; 06:20 at 7:30 and 06:53 at 9:00, the AI as P1),
the turtle (pinned to the old ways) for the owner, 30 minutes on, 16
seeds each:

| | before (`ARC_CORE`, `DROP_FLY`, `TENDED` 0) | now |
|---|---|---|
| 10:43, won-lost-drawn | 0-0-16 | 0-0-16 |
| ... its jet shot down | 149 | 145 |
| 06:20 | 0-7-9 | 0-3-13 |
| ... its jet shot down | 26 | 22 |
| 06:53 | 0-6-10 | 0-2-14 |
| ... its jet shot down | 33 | 26 |

Against the turtle (pinned to the old ways), all maps, both seats, three
hours; against the CPU on a human's pad, 8 a set:

| | now | session 68 (17) |
|---|---|---|
| against the turtle | 91-15-22 | 77-24-27 |
| ... `DROP_FLY` alone (`ARC_CORE` 0) | 87-17-24 | |
| against the turtle with `FLANK` 4 (`drop_range` 96) | 88-13-27 | 79-19-30 |
| against the turtle with `nest_units` 6 | 106-5-17 | 100-6-22 |
| on a human's pad against the CPU, TYPE A | 120-7-1 | 121-6-1 |
| ... TYPE B | 110-2-16 | 104-9-15 |
| ... TYPE C | 31-35-62 | 34-37-57 |
| ... TYPE D | 13-32-83 | 15-27-86 |
| as the CPU's brain, TYPE A | 128-0 | 128-0 |
| ... TYPE B, C and D | 127-0-1 each | 127-0-1 each |

On TYPE D, 16 a set, `ARC_CORE` 80 28-62-166 and 0 29-57-170: within a
set's chance. Against the CPU only `ARC_CORE` plays (`DROP_FLY` is a
human's; `TENDED` met none).

## Outposts back, and a garrison when its attacks do nothing (session 68 (19))

The owner, after the 31 October 09:35 game (VULKAN, the server's AI as
P1, lost at 41:26): "You actually gave me a little run for my money. Good
Job but still not good enough."

**The game.** To minute 23 the AI held 5 or 6 outposts to his 3 or 4 and
shot his HQ from 1,024 to 688. At 20:29 he took its outpost 4 (1936,880)
with INFANTRY, at 22:57 again, at 23:39 outpost 3 (1520,752), and stood
his mech on foot by them: the 13 INFANTRY it sent there came one at a
time and his mech shot each within a block of it. Two SAM-42s stood by
outpost 4 from minute 24, five and a SUPPLY by outpost 3. From minute 24
it held 3, he 6 and then 7, and with `CAPTURE_MIN` 3 it took no more.
His HQ was shot no more: 25 of its attack units went in one at a time,
16 of them to one place 400 px south of his HQ, one every 40 s from
27:28 to 35:55, and each died at his wall (SAM-42s, TAX-52s, turrets and
a SUPPLY). Its money went on them ($96 to $900 from minute 28); his
piled up to $13,600. Its garrison waited on its HQ being shot
(`GARRISON_SHOT`); nothing stood by its HQ. At 35:59 one INFANTRY took
its outpost 5 (1840,1456), 450 px north of its HQ, and he stood five
SAM-42s and a SUPPLY there; from 39:34 TAX-52s and INFANTRY put down
from there 1 to 4 blocks from its HQ shot it 90 times, 1,024 to 0 in
1 minute 53. Its mech, flying at outpost 5, was destroyed five times.
Over the game he bought 40 SAM-42s and it none; it bought 49 attack
units at $670 each and lost all 49.

- `CAPTURE_EVEN` 2: against a human, outposts taken while the enemy
  holds 2 or more than it, as below `CAPTURE_MIN` (only while its HQ
  wasn't shot in the last minute and its garrison is up); past
  `CAPTURE_MIN` none at one with 2 or more enemy armed units by it
  (`CAPTURE_GUARDS`). At 1 (parity) on STADT as P2 it went for a 4 to 3
  with INFANTRY instead of the attack that wins it there in 15 minutes:
  3-5 from 7-1; OASE as P2 5-2-1 from 8-0.
- `FUTILE` 8, `FUTILE_TIME` 18,000, `FUTILE_BEHIND` 1, `FUTILE_SIZE`
  40: against a human, 8 of its attack units destroyed since the enemy
  HQ last lost HP, none for 5 minutes, and the enemy holding more
  outposts: the garrison (40) as if its HQ had been shot. In his game it
  would have gone at 28:25, his HQ last shot at 23 minutes and 8 more
  of its attack units destroyed. Against the turtle it goes only where
  the game stands still (OASE and LOCH as P1). With 4, and no time or
  outposts rule, it turned the attacks that still hurt the turtle's HQ
  into a garrison (taken over at minute 20: his HQ 680 left against
  390); with 8 and 5 minutes but no outposts rule, against the turtle
  with `nest_units` 6 EISFREI went 6-5-5 from 16-0 (its attack units
  died 8 to 15 in a row between hits there, and won); 16, 4-3-1 and
  6-0-2 there.
- Tried, off: `HUNT_TAKE` (hunters for the SAM-42s by an enemy outpost
  it would take): taken over at minute 24 it lost 2 of 16 for none.
  `CAPTURE_ESCORT` 1 (a TAX-52 on guard near put down first by an
  outpost his mech stood by on foot in the last minute) and 2 (no
  INFANTRY sent there then): the turtle stands its mech by outposts too;
  on STADT as P2 0-8 and 5-3, and 0-8 with 2, against 7-1. A first try
  that also sent one by any guarded outpost bought 30 to 97 a game.

Taken over at minutes 20 and 24 with the turtle for the owner (16
seeds, 30 minutes on) every version draws nearly every game: his side
holds 6 or 7 outposts and its HQ is not reached; with `FUTILE` 4 (no
time or outposts rule) the attacks that still hurt the turtle's HQ from
minute 20 stopped (his HQ 680 left against 390).

The owner's games taken over, the turtle (pinned to the old ways) for
The owner, 30 minutes on, 16 seeds each:

| | session 68 (18) | now |
|---|---|---|
| 30 October 10:43 at 18 minutes (the AI P2) | 0-0-16 | 0-0-16 |
| 31 October 06:20 at 7:30 (P1) | 0-3-13 | 0-2-14 |
| 31 October 06:53 at 9:00 (P1) | 0-2-14 | 0-4-12 |
| 31 October 09:35 at 20:00 (P1) | 0-0-16 | 0-0-16 |
| ... at 24:00 | 0-0-16 | 0-0-16 |

At 06:53 `CAPTURE_EVEN` never played; with `FUTILE` 0, 0-2-14: in the
three games it lost so it had bought 1 or 2 units for the garrison, late;
one it lost without them was drawn.

Against the turtle (pinned to the old ways), all maps, both seats, 8 a
set, three hours:

| | now | session 68 (18) |
|---|---|---|
| against the turtle | 94-9-25 | 91-15-22 |
| ... `CAPTURE_EVEN` 2 alone | 94-10-24 | |
| with `FLANK` 4 (`drop_range` 96) | 90-12-26 | 88-13-27 |
| with `nest_units` 6 | 108-4-16 | 106-5-17 |

Against the CPU nothing changed (all of it is against a human): on TYPE
D, all maps, both seats, 4 a set, every game played as before.

## The outpost by its HQ, and the GRM-34As he kept repaired (session 68 (20))

The owner, after the 31 October 18:31 game (VULKAN, the server's AI as
P1, lost at 69:28): "ok look at last play".

**The game.** To minute 20 the AI held 5 outposts to his 4 and shot his
HQ from 1,024 to 708; its own was untouched. At minute 21 his units shot
it for 154, and from then on it built its garrison: no units at minute
21, 34 at minute 33 (`GARRISON_SHOT`). Meanwhile he took its outpost 4
(1936,880) at 23:33 and outpost 5 (1840,1456), 448 px north of its HQ,
at 26:39, and held outpost 5 to the end; then 3 at 37:53, 6 at 41:58 and
8 at 57:24. From minute 27 it held 3 to his 6 and never sent INFANTRY
back to outpost 5: 3 is `CAPTURE_MIN`, and `CAPTURE_EVEN` waits on its HQ
being calm, which his drops from outpost 5 kept it from being (HP lost
at minutes 27, 29, 30, 31 and on). Its HQ was shot no more after
minute 20; his HQ stayed on 708 to the end.

He stood five SAM-42s, a SUPPLY and GRM-34As by outpost 5. From minute
33 to the end the AI bought one unit; its money went from $956 to
$18,212, and its mech was destroyed 73 times between minutes 33 and 62.
Thinking alongside the game (hz_arena `--shadow`), from minute 33 it
went for his GRM-34As 1,170 times in the samples, his FWAs 684 and his
SAM-42s 231. The loop: destroyed, it came back on its HQ with a quarter
of its fuel (389 of 1,556) and went for the GRM-34As by outpost 5, 400
px off (`BUY_FIRST` never played: they were a threat to its home), and
was destroyed on foot by their missiles within seconds; or it flew for
home round his SAM-42s' reach, east and north and west (`ARC_GOAL` makes
the missiles' reach within 10 blocks of the goal full cost), and ran dry
(15 times). `TENDED` didn't hold it back: he lifted his hurt GRM-34As
and put them down again repaired, and while one was carried it lost
track of it, so each came back as new (one by outpost 5 went from 64 HP
to 144 while carried, and the mech went for it 700 frames later). He
bought 139
FWAs (137 lost; they shot its HQ 593 times, 1 HP each), 30 SAM-42s and
15 GRM-34As: $120,639 spent to its $41,986.

- `TENDED_CARRY` 1: against a human, an enemy unit carried is kept track
  of, its HP and missiles as they were, so one put down repaired is seen
  repaired (`TENDED`).
- `HOME_GRAVES` 3: against a human, an enemy unit that isn't shooting its
  HQ is left alone, near its HQ too, once the mech was destroyed 3 times
  within `DEATH_RADIUS` of it in the last `DEATH_AVOID` frames.
- `PATH_FUEL` 1: against a human, a flight round the missiles longer than
  its fuel goes straight, when the straight one is within it.
- `HOME_POST` 640: against a human, an enemy outpost within 640 px of its
  HQ is taken back first, whatever `CAPTURE_MIN` and its HQ shot or not;
  one out of the enemy's missiles, not one it failed at lately, with
  fewer than 2 enemy armed units by it (`HOME_POST_GUARDS`); with half
  its garrison up (`HOME_POST_WALL`); once its attacks had hurt the enemy
  HQ and then not for 5 minutes (`HOME_POST_HIT`); and while its own HQ
  was shot in the last 5 minutes (`HOME_POST_SHOT`). Taken over at 33
  minutes with the turtle for the owner, the turtle's mech refuelled
  at outpost 5 between its drops, nothing of its by it, and it lost 13 of
  16; with this, 16 draws, its HQ untouched. Without `HOME_POST_HIT`, on
  map 5 as P1 the turtle took its outpost 576 px from its HQ in the first
  minutes and it sent INFANTRY there instead of building the attack that
  won it 7 of 8 (3-1-4; counting from 5 minutes on, 4-2-2; with the enemy
  HQ shot once, 7-0-1). Without `HOME_POST_SHOT`, against the turtle with
  `nest_units` 6 on map 6 as P1, 8 outposts to 1 and its HQ untouched, it
  went for the one by its HQ 8 to 15 times a game and lost 2 of 8 (none
  with it); on map 7 as P2 1 of 8 (none).
- Tried, off: `BASE_FUEL` (deciding at a base with less than half its
  fuel, carrying nothing, its HQ calm: refuelled there first). Asked in
  every task, against the turtle on maps 2, 5 and 6 it lost 23 of 40 for
  7 (it left its fights by its HQ and outposts, and its pickups, to
  refuel); deciding only, 12 for 8.

The turtle neither lifts its units to be repaired nor nests GRM-34As by
an outpost, so the first three are seen only thinking alongside his game
(`--shadow`, the recorded game played): from minute 33 it went for his
GRM-34As in 117 samples of 6,951 (1,250 before), fought in 1,302
(2,400), and bought or went to carry units in 1,135 (16); 93 of its
flights went straight.

The owner's games taken over, the turtle (pinned to the old ways) for
The owner, 30 minutes on, 16 seeds each:

| | session 68 (19) | now |
|---|---|---|
| 31 October 18:31 at 33 minutes (the AI P1) | 0-13-3 | 0-0-16 |
| ... at 29 minutes | 2-0-14 | 2-0-14 |
| 30 October 10:43 at 18 minutes (P2) | 0-0-16 | 0-0-16 |
| 31 October 06:20 at 7:30 (P1) | 0-2-14 | 0-2-14 |
| 31 October 06:53 at 9:00 (P1) | 0-4-12 | 0-4-12 |
| 31 October 09:35 at 20:00 (P1) | 0-0-16 | 0-0-16 |
| ... at 24:00 | 0-0-16 | 0-0-16 |

At 33 minutes its HQ kept the 731 it had, and his went from 708 to 524
on average: with outpost 5 back its attacks went in again. At 29 minutes
its attacks still hurt his HQ, and `HOME_POST` never played.

Against the turtle (pinned to the old ways), all maps, both seats, 8 a
set, three hours:

| | now | session 68 (19) |
|---|---|---|
| against the turtle | 95-13-20 | 94-9-25 |
| with `FLANK` 4 (`drop_range` 96) | 89-11-28 | 90-12-26 |
| with `nest_units` 6 | 107-6-15 | 108-4-16 |

Of the 4 more lost against the plain turtle, `HOME_POST` lost 3: on map 4
as P1 one (6-0-2 without it) and on map 5 as P2 two (2-2-4 without), a
game it held no outpost in from minute 22 to the end while the turtle
held 8.

Against the CPU nothing changed (all of it is against a human): on TYPE
D, all maps, both seats, 4 a set, every game played as before.

## Drops spread out, guards by its outposts, drops in reach: tried, off (session 68 (21))

The owner: "One thing that I've noticed to is your AI obsesses over
dropping units in the exact same place. I've never seen your AI drop
units directly near a base. It never garisons Outposts."

**His games.** In his two VULKAN games of 31 October (09:35 and 18:31,
the server's AI P1) all 80 of its attack units (49 and 31) went down 400
to 700 px from his HQ, 17 a game at one place 624 px off. He put 144 of
his 153 down 200 to 400 px from its HQ (70 at one distance, 368 px, from
all round), flying over at speed in reach of its 16 SAM-42s, and lost his
jet 4 times in the game; in the 09:35 game his 8 went down within 200 px.
`attack_drop_best` passes over every block the enemy's SAM-42s and
GRM-34As reach (`DANGER_MISSILE`): with his round his HQ the nearest left
were some 600 px out, and the score (the drive, the way from the jet,
`JITTER` up to 48) took the same one each time. Nothing of its stood by
its outposts: he took them with INFANTRY (09:35: 4 at 20:29 and 22:57, 3
at 23:39, 5 at 35:59; 18:31: 4 at 23:33, 5 at 26:39, 3 at 37:53, 6 at
41:58, 8 at 57:24).

All three tried, all off; it plays move for move as in session 68 (20):

- `DROP_SPREAD` 32 (`DROP_SPREAD_R` 64): against a human, an attack drop
  within 64 px of one of its last 4 (in 10 minutes) 32 worse each, a
  block's drive.
- `DROP_INSIDE` 3: with `DROP_FLY`, attack drops in the missiles' reach
  too where fewer than 3 reach, never within `ARC_CORE` of one, 32 worse
  each one reaching; `DROP_INSIDE_SAM`: only where SAM-42s alone reach,
  which shoot only the jet.
- `POST_GUARD` 1: a TAX-52 on guard put down 48 px from each of its
  outposts towards the enemy HQ, the first before its HQ's garrison
  (`POST_GUARD_FIRST`), only while its HQ was calm (`POST_GUARD_CALM`).
  The jet took a guard to its outpost 1,150 px off while the turtle's
  drops came in at its HQ, and the money went on guards, not INFANTRY.
- `COLD_JUDGE` 4,800 again (session 68 (15)), with `DROP_SPREAD` off: a
  drop that hurt nothing in 80 seconds, none within 160 px for 3 minutes.

The owner's games taken over, the turtle (pinned to the old ways) for
The owner, 30 minutes on, 16 seeds each (`POST_GUARD` and `DROP_INSIDE`
each alone; "after the garrison only" with `DROP_SPREAD`):

| | now | `DROP_SPREAD` | `POST_GUARD` | ... after the garrison only | `DROP_INSIDE` | `COLD_JUDGE` |
|---|---|---|---|---|---|---|
| 31 October 18:31 at 33 minutes (P1) | 0-0-16 | 0-0-16 | 0-6-10 | 0-1-15 | 0-0-16 | 0-0-16 |
| ... at 29 minutes | 2-0-14 | 3-0-13 | 1-0-15 | 1-0-15 | 0-0-16 | 2-0-14 |
| 31 October 06:20 at 7:30 (P1) | 0-2-14 | 0-3-13 | 0-1-15 | 0-3-13 | 0-5-11 | 0-6-10 |
| 31 October 09:35 at 20:00 (P1) | 0-0-16 | 0-0-16 | | 0-1-15 | | 1-0-15 |
| ... at 24:00 | 0-0-16 | 0-0-16 | | 0-1-15 | | 0-0-16 |
| 30 October 10:43 at 18 minutes (P2) | 0-0-16 | 0-0-16 | | | | 0-0-16 |
| 31 October 06:53 at 9:00 (P1) | 0-4-12 | 0-4-12 | | | | 0-4-12 |

After the garrison only it held 1.8 outposts at the end at 33 minutes to
4.4 without, 4.6 to 3.2 in his 09:35 game at 20 minutes. With
`DROP_INSIDE` at 33 minutes his HQ kept 698 HP on average (524 without).

Against the turtle (pinned to the old ways), all maps, both seats, 8 a
set, three hours:

| | now | `DROP_SPREAD` | `COLD_JUDGE` |
|---|---|---|---|
| against the turtle | 95-13-20 | 90-15-23 | 91-14-23 |
| with `FLANK` 4 (`drop_range` 96) | 89-11-28 | 89-11-28 | |
| with `nest_units` 6 | 107-6-15 | 109-6-13 | |
| map 6 as P2, map 5 both seats, 16 a set | 25-14-9 | 17-21-10 | 21-10-17 |

With `DROP_SPREAD` the map 6 P2 games were lost to the turtle taking all
eight outposts (3-11-2 against 7-6-3). The turtle doesn't answer a spot
used over and over with turrets, as the owner did on 30 October; the
best spot stays the best against it, so moving off it only costs.
`COLD_JUDGE` isn't limited to a human (it'd change its games against
the CPU too). Against the CPU nothing of the rest is played (all against
a human): on TYPE D, all maps, both seats, 4 a set, every game as before
with `DROP_SPREAD` 32.

## The carrying jet by its bases, and guards where INFANTRY came (session 69)

The owner, of his two games of 1 November against the server's AI
(e1a1ab8, installed 31 October at 21:49: today's but for session 68
(21)'s tries; P1; VULKAN 40:33 and LOCH 30:38, both won by him): "NM has
weird issues where it generally ignores my plane. It'll hover over its
outpost waiting while I am carrying a base infiltrating infantry and do
nothing until I drop it too close for it to be able to kill it before it
enters the base. If I've deprived it of money by taking all of the
bases, it'll often sit and hover until it has enough money to buy a unit
unless I'm sending a unit after its base. Occasionally NM's jet will
chase mine after I've done something close to it but its generally
blind or indifferent to my jet until I drop items. It never attempts to
Garrison its outposts."

**His games.** On LOCH his outpost 5 (112, 1232) is 512 px north of its
outpost 6 (112, 1744). From 2:28 to 3:42 he carried INFANTRY from one to
the other eight times, put down 80 px from its outpost; its jet waited
over that outpost (`NM_TASK_WAIT`, the nearest base), his carrying jet
passing 60 to 250 px from it, and it did nothing till each was down (on
a human's pad the carrying jet isn't chased: `DOG_PAD` 0, session 68).
It killed seven of the eight on foot; the eighth took the outpost. Its
outposts 8 and 7 went the same way (6:42, 10:28), then 0, 3 and 4; from
11 minutes it held none. Meanwhile it sent its own INFANTRY, a dozen in the
first 2.5 minutes, at his outpost 5, where his mech stood: none took
it. On VULKAN he held all nine outposts from minute 23;
from minute 28 it bought nothing for 12 minutes (its money 390 to 3,270)
and was destroyed 45 times in that time, 250 to 550 px from its HQ, by
the GRM-34As he put down round it: each time from its HQ to fight one
300 to 600 px out (within `DEF_RADIUS`), destroyed in 3 seconds, a
different one in turn. `HOME_GRAVES` 3 leaves one alone once the mech
was destroyed 3 times within 288 px of it, but it keeps its last 4
deaths (`NM_GRAVES`), and spread over three or four of them none came
to 3. A fight at home comes before a buy (`BUY_FIRST` is for fights
away), so nothing was bought.

**The raider** (`RAID`, for hz_arena; 0, none): a player's way to try
against, as he plays: INFANTRY sent at the outposts nearest its own
bases, the enemy's first (`pick_capture` scores them by the way from its
nearest base, 256 better for the enemy's), till it holds `RAID` (9: on
and on). The bot with `--param2 raid=9` took LOCH 8 games of 8 from the
nightmare AI as it was; nothing else in the arena plays so.

Tried (on a human's pad against a human, all maps, TYPE A, 8 games a
map; wins, losses, draws; the AI as P1 against the raider unless said):

| | against the raider |
|---|---|
| as it was | 36-14-14 (LOCH 0-8) |
| `DOG_GUARD` 300 | 51-5-8 |
| `DOG_GUARD` 400 | 51-4-9 (LOCH 8-0) |
| `DOG_GUARD` 500 | 51-2-11 |
| `DOG_GUARD` 700 | 43-1-20 |
| `POST_RAID` 1 | 51-7-6 (LOCH 8-0) |
| `POST_RAID` 2 | 41-8-15 |
| `WAIT_NEAR` 1 | 44-6-14 |
| `DOG_GUARD` 400 and `POST_RAID` 1 | **52-0-12** |
| ... and `WAIT_NEAR` 1 | 42-5-17 |
| `DOG_GUARD` 400 and `WAIT_NEAR` 1 | 49-5-10 |
| `POST_RAID` 1 and `WAIT_NEAR` 1 | 54-3-7 |

- `DOG_GUARD` 400 (kept): against a human, the enemy jet carrying a unit
  within 400 px of its HQ or an outpost of its own, and within
  `DOG_RANGE` (700) of the mech, is chased (`dog_guard_worth`), and only
  till the unit is down (`dog_guard`: the chase ends there, so it is free
  for the unit put down, which `pick_threat` finds by its outpost). Out
  of the missiles, with 128 HP, the fuel and the ammo, as any chase.
  Session 68's `DOG_PAD` chased it everywhere and lost; this only by its
  own bases.
- `POST_RAID` 1 (kept): against a human, an outpost of its own the
  enemy's INFANTRY came within 240 px of (`POST_RAID_R`; looked at twice
  a second, `raid_watch`) in the last 5 minutes (`POST_RAID_TIME`) gets
  a TAX-52 on guard (`POST_RAID` of them), bought before INFANTRY of its
  own goes out (`raid_guard_post`, `choose_buy`), put down 48 px from it
  towards the enemy's base nearest it (`raid_from`, `post_guard_drop`).
  Session 68's `POST_GUARD` put one by every outpost before any was
  raided, and lost the money it needed for INFANTRY.
- `WAIT_NEAR` 1 (off): nothing to do, over its base nearest the enemy
  mech, not its own nearest (kept unless another is 128 px nearer).
  Against the AI as it was it lost 30 of 64 as P2 with the other two.
- `IDLE_DOG` (off): nothing else to do, the enemy jet within this
  chased, carrying or not (with 176 HP, given up under 128);
  `IDLE_BROKE` 1, only short of the money for INFANTRY with nothing on
  order: his "it'll often sit and hover until it has enough money".
  With the two kept, both seats (128 games; the AI's wins and losses):

| | against the raider | against the turtle | against the AI as it was |
|---|---|---|---|
| `DOG_GUARD` 400, `POST_RAID` 1 | 87-2 | 62-5 | 39-25 |
| ... `IDLE_DOG` 400 | 83-3 | 75-4 | 34-33 |
| ... `IDLE_DOG` 700 | 81-10 | 65-11 | 32-37 |
| ... `IDLE_DOG` 400, `IDLE_BROKE` 1 | 86-7 | 61-5 | 39-25 |

  Broke it seldom is in the arena; his LOCH game taken over at 12
  minutes (the turtle for him, 8 seeds, all draws) left its HQ 446 HP on
  average with `IDLE_BROKE`, 532 without, 357 as it was.

**Results**, both seats (128 games; the AI's wins and losses):

| | as it was | now |
|---|---|---|
| against the raider | 60-20 | 87-2 |
| against the turtle | 46-14 | 62-5 |
| against the AI as it was | 33-33 | 39-25 |
| through the server's delay (3 frames, thinking ahead), the raider | 85-17 | 92-3 |
| ... the turtle | 45-18 | 47-8 |

As the CPU's brain, and on a human's pad against the CPU, nothing
changes (all of it against a human only): the same games, move for move.
His LOCH game played again with the AI of today thinking along
(`--shadow`): it goes for his carrying jet at 2:42 and 2:52, as it comes
down at its outpost 6, where the server's AI waited, and buys a guard
for that outpost.

**The GRM-34As round its HQ** (`NEST_DEATHS` 3, kept). Against a human,
once the mech was destroyed 3 times within `DEF_RADIUS` of its HQ in
`DEATH_AVOID` (of its last 4 deaths, `home_deaths`), no enemy SAM-42 or
GRM-34A whose gun doesn't reach its HQ is fought (`pick_threat`). His
VULKAN game taken over at 28 minutes, the turtle for him (8 seeds): as
it was, 7 lost and 1 drawn, destroyed 25 to 45 times; with it, 4 lost
and 4 drawn (its HQ 510 to 778), its buys 16 to 28 from 8 to 18. Then it
went for the SUPPLYs and TAX-52s among them and was still destroyed 22
to 46 times; `NEST_DEATHS_ALL` 1 (none fought in the missiles' reach
either, off) lost 7 of 8. In the arena tournaments it never comes to it:
the games are the same, move for move.

The turtle (`TURTLE_PARAMS`) is kept without `DOG_GUARD`, `POST_RAID` and
`NEST_DEATHS`, a player having none of them; the raider is run so too
(`--param2 raid=9 --param2 dog_guard=0 --param2 post_raid=0 --param2
nest_deaths=0`).

**The side against the server's AI.** the owner: "check the random
roll I always end up as player 2 now". Every game against it since the
coin's toss went in (session 68 (16)) put him on P2: 7 tosses (31
October 06:19 to 1 November 11:03, the server's log; a rematch keeps
the sides), 1 in 128. The toss is sound: the installed `hz_server` is
e1a1ab8's (the same md5), `net_random32` on the server gives 482 even
and 518 odd in 1,000, and a server here with `hz_netai` and quick
matches from `hz_headless` put the person on P1 in 10 of 28 games. Kept
as it is (The owner: keep the coin toss).

## Tanks that ran dry on the way (session 70)

The owner, of his game of 8 November against the server's AI (b646cf0,
installed at 07:06; ABGRUND, TYPE A, the AI P1; won by him at 43:57):
"It was inconsistent but pretty aggressive about certain things and
better at trying to intercept my mech."

**His game.** Its jet came at his carrying jet on 30 of the 32 flights
that ended within 400 px of one of its bases (`DOG_GUARD`), within 200 px
on 16, and hit it on 11; it shot none down, and every unit was put down.
Of his other 64 carries, far from its bases, it went for none. He held 4
outposts by minute 9, 7 by 19 and 8 by 30, it 2 and then 1; it had $300
or less most of the game, he up to $27,220, and it waited for money a
quarter of its time. From minute 12, 20 of its 29 attack drops went down
by its own outpost 6, 1,360 to 1,400 px from his HQ (the earlier ones 420
to 780): his SAM-42s kept the jet from every drop nearer, and of the
places by the line from outpost 6 to its drop at (224, 512) the farthest
whose drive passed was 150 px out (`task_deliver`, the line's loop). The
drive test (`drive_run`) passed it: at `DRIVE`'s 3,000 frames the TAX-52
was still on its way round the chasm, 128 px nearer (`DRIVE_GAIN`). A
TAX-52's tank lasts about 8,200 frames' drive. Eleven stood dry 1,000 to
1,500 px from his HQ, full of ammunition, from minute 18 to the end
(about $8,000, a third of its spending), and his HQ lost nothing after
minute 16; the rescue (`RESCUE`) lifts a stalled unit once, and it was
put down at the same place again.

He won from its outpost 6, taken at about 30 minutes, 615 px from its HQ
(outside `DOG_GUARD`): ten SAM-42s put down there from 33:00 to 36:14
destroyed its mech 7 times (`NEST_DEATHS` counted them, but it went for
the other units among them); fourteen FWAs (attack the HQ) put down at
one place 483 px from its HQ all died; then TAX-52s put down 92 to 191 px
from it took it from 866 to 26 in under two minutes, its 28 standing
units round it notwithstanding.

**The drive followed to its end** (`DRIVE_FUEL` 7,200, kept). Against a
human, a unit still on its way when `DRIVE`'s frames are up is followed
on, 7,200 frames more at most: it passes if it hurts the HQ, or still has
fuel when they are up; run dry first, it fails (`drive_going`; driving on
as it ran dry, its path keeps no answer, one put down further along
having fuel left; one that moved less than 32 px in 1,800 frames while
followed is stuck, and fails, `NM_ATLAS_STILL`, its fuel unburnt, as a
unit burns it only on a new tile). With no place passing, the attack
unit still went down
where its drive failed (blind: `NO_BLIND` is off, session 68); the first
write-up said it was made a guard, which was wrong. As the CPU's brain
nothing changes.

His game taken over at 12 minutes, the turtle for him (with
`drive_fuel=0`), seeds 1 to 8 (the first write-up's 0-0-8, 2-0-6 and
0-0-8 with `--shadow` counted wrong):

| | won-lost-drawn | its attack units dry at the end, of those alive |
|---|---|---|
| as it was | 0-1-7 | 78 of 92 (2 to 22 a game) |
| `DRIVE_FUEL` 7,200 | 2-1-5 | 8 of 9 (0 to 2) |
| as it was, thinking along before (`--shadow`) | 0-0-8 | 45 of 57 |
| `DRIVE_FUEL` 7,200, `--shadow` | 0-2-6 | 29 of 32 |

The two won (seeds 3 and 6) took his HQ at 38 minutes holding 4 outposts
to his 4; as it was, the same seeds drew holding 2 to his 6. On 24 seeds
it lost more than it won (the next table).

In the arena it seldom comes to it: no bot puts down SAM-42s enough to
push the drops out so. All maps, TYPE A, 8 a map, both seats (the AI's
wins and losses): against the raider 88-2 (87-2 as it was), against the
turtle 61-5 (62-5); against the AI as it was, P2 won 10 to 2 whichever
side had it, the other 52 drawn.

The slowest frame (both sides' thinks with it, `HZ_ARENA_SLOW`), his game
taken over at 12 minutes, seeds 3 and 5: 6.5 ms with it, 5.8 and 6.5
without; none over 16 ms. Against the CPU every game is as it was
(`make nightmare`, `make ahead`).

The turtle (`TURTLE_PARAMS`) is kept without `DRIVE_FUEL` too, and the
raider is run so (`--param2 drive_fuel=0` besides session 69's).

**No ground to his HQ** (`REGION` 1). The owner, after the above:
"Nightmare keeps putting down tanks on Abgrund that can never reach
enemy base period, the whole level is cliffs and a water barier." So it
is: of ABGRUND's ground, the part joined to P2's HQ (with outposts 1, 2
and 4) and the rest (P1's HQ, outposts 0, 3 and 5 to 8) meet nowhere; a
wall of cliffs and water runs between them from the top edge to the
left. A TAX-52 put down on the wrong side never gets there, however it
drives. `DRIVE_FUEL` alone didn't end it:
his game taken over at 12 minutes, seed 9, all 313 of its drive tests
failed, each attack unit went down blind, by the wall on its own side,
its order to attack his HQ, and died there to the turtle's mech one by
one; its money gone on them, the turtle took its outposts and its HQ.

Now, against a human, the ground joined to the enemy HQ is flooded once
a map, side and footprint (`region_of`: the places where
`obj_check_terrain`'s 3 x 3 tiles, 2 x 2 for INFANTRY and FWA, have no
b15, 8-way from those within 160 px of the HQ; slopes and currents
taken as passable, so it is never smaller than the ground a unit
drives). A place off it fails the drive test without a drive
(`drive_ok`), and an attack unit about to go down off it, blind, is not
put down so (`carried_region_ok`, at the goal and at the drop): a
TAX-52 is ordered to stand guard and put down there all the same
(`make_field`, `NM_CARGO_FIELD`), anything else made a guard at home.
Made guards at home instead (as `NO_BLIND`'s are), on seed 3 they left
its half to the turtle's drops, and it lost its outposts 5 to 7; on
guard by the wall they hold its half as the blind ones did, without
driving into the cliffs.

His game taken over at 12 and at 20 minutes, the turtle for him (with
`drive_fuel=0 region=0`), seeds 1 to 24, played on and thinking along
before (`--shadow`), the AI's won-lost-drawn:

| | 12 min | 12 min, `--shadow` | 20 min | 20 min, `--shadow` | all 96 |
|---|---|---|---|---|---|
| as it was | 0-1-23 | 0-0-24 | 0-2-22 | 0-0-24 | 0-3-93 |
| `DRIVE_FUEL` | 5-5-14 | 0-4-20 | 0-1-23 | 0-0-24 | 5-10-81 |
| ... with `NO_BLIND` 1 | 0-0-24 | 0-0-24 | 0-1-23 | 0-0-24 | 0-1-95 |
| ... with `REGION`, a guard at home | 0-1-23 | 0-0-24 | 0-2-22 | 0-0-24 | 0-3-93 |
| ... with `REGION`, on guard there (kept) | 8-0-16 | 0-0-24 | 0-2-22 | 0-0-24 | 8-2-86 |

With `REGION` none of its attack units stood dry at the end but one of
his game's (from before the takeover). Where an attack drop's drive
passes, nothing changes: ABGRUND, P1 against the turtle, seeds 1 and 2,
played alike move for move with it and without.

In the arena it changes next to nothing: no bot pushes the drops off the
HQ's ground so. All maps, TYPE A, 8 a map, both seats, the AI's wins and
losses with it (without): against the raider 87-2 (88-2), against the
turtle 60-5 (61-5). The flood is made once a game for each side and
footprint (256 x 256 places), and places off it are no longer driven:
the slowest frame (`HZ_ARENA_SLOW`), his game taken over at 12 minutes,
seeds 3 and 5, 2.7 and 3.0 ms with it, 4.4 and 4.7 without. Against the
CPU every game is as it was (`make nightmare`, `make ahead`).

The turtle is kept without `REGION` too (`TURTLE_PARAMS`), and the
raider is run so (`--param2 region=0`).

## The strip by the map's edge (session 73)

The owner's game of 10 November against the server's AI (91c781a;
ABGRUND, TYPE A, the AI P2, PATHS and UNITS on; won by him at 32:41, his
HQ never under 898) was read on 10 November: five faults (HANDOVER.md,
Next steps, the nightmare AI's (h)). This session took the first two.

**The strip.** P2's HQ stands at (176, 112). Its building's blocks take x
96 to 255 and y 32 to 159, and the row of blocks above it, the map's top
32 px, is plain ground. A mech stands on the 4 x 4 tiles from (x + 4,
y + 4), so in that row it fits only with y 0 to 3 (south of P1's HQ the
same, y $7DC to $7E0). `landable` and `walkable` took nothing within 8 px
of an edge for a place, so to the AI the strip was no place at all: none
for a wall unit or a guard, none to land and fight from. From 21:51 he
put six TAX-52s down in it (x 103 to 237, y 4 to 11: 100 px from the HQ)
and a seventh as the game ended. The first, at the strip's east end, the
mech shot from past the building's corner. The others shot the HQ till
the wall's three TAX-52s by the strip's ends had them, 70 to 112 HP a
tank; the driver of the second (one in four gets out) stood there to the
end and fired his 80 shots, 160 HP, the TAX-52 at the east end shooting
at him till its ammunition was gone, without a hit. The mech's one place
by these was east of the building, at
(259, 79), with the building in its line of fire: it stood there 34 s
without a shot, and with that place given up as stuck it had none, so the
fight's fallback took it 72 px north of the target, y -70, and it held
against the top edge for 78 s. Of the HQ's 1,024 HP about 700 went from
the strip.

**The edge's places** (`EDGE` 1, every side). A mech's place is anywhere
from 0 to $7E0 on both axes, as the game has it (`on_map`). A place
within 8 px of an edge is taken at the edge's own coordinate, 1 from the
low edge and $7DF from the high one (`edge_snap`), and flown or walked to
within 1 px (`steer_to`, `edge_goal`): a flight's 4 px left the jet at
y 4 or 5, where its tiles reach the building. The places looked for by
steps from somewhere else would step over a row 4 px tall, so the fight's
(`fight_spot`, `fight_place_any`) and those near a point
(`landable_rings`: a guard's by its spot, a put-down's) are moved to the
edge when they fall within 8 px of it, either side; a fight place moved
so is tried for its line of fire by the heading from where it now is.
The fight's fallback goes south of the target when north of it is off
the map. The wall's rings keep the places they had, none by an edge
(`WALL_EDGE` 0, below).

**More guards where it is shot from again** (`SPOT_MORE` 2, against a
human). A spot the HQ was shot from got one guard, a TAX-52 standing
within 64 px (`SPOT_HUMAN`), and with one there the next tank went down
40 px along and shot the HQ till it died. Now a spot is to have one more
guard for every 2 hits the HQ took from there, 3 at most (`spot_want`),
each put down at the nearest place to the spot: three by a spot leave a
jet little room to put anything down, and in the strip none.

**The scenes.** His game taken over just after each of his units went
down in the strip, his side idle from there, the AI playing on for a
minute (not thinking along before: see the traps below); what its HQ
lost, 8 seeds each:

| his unit (frame) | as it was | now |
|---|---|---|
| TAX-52 at (237, 11) (81,130) | 70 | 84 |
| TAX-52 at (209, 6) (105,790) | 102 | 28 |
| the driver at (190, 10) (106,390) | 160 | 12 |
| TAX-52 at (141, 4) (110,050) | 122 | 42 |
| TAX-52 at (121, 11) (113,290) | 110 | 46 |
| TAX-52 at (109, 7) (115,510) | 84 | 56 |
| TAX-52 at (128, 11) (117,670) | 120 | 38 |
| a scene | 110 | 44 |

The mech lands in the strip beside each and shoots along it. A TAX-52
has 224 HP and the mech's gun takes 16 a shot at a player's fire rate, so
a tank still gets two to four shots at the HQ (14 HP each) before it
dies. The first, put down with the jet 250 px off, got five as it was,
shot from east of the building's corner, and six now: that place had no
line of fire when the mech chose, and the one in the strip is 90 px
further.

**Tanks in the strip, again and again** (`hz_arena --put`: a unit put down
for a side every so many frames, where there is room; here a TAX-52 with
attack HQ at y 6, x 100 to 240). His game taken over at 22:00 (frame
79,200, its HQ on 758), his side idle, a tank every 15 s for 7.5 minutes,
16 seeds:

| | its HQ lost | tanks that found room | its HQ at the end |
|---|---|---|---|
| as it was | 16 of 16, after 6.7 tanks | 6.7 of 7.0 | 0 |
| `EDGE` alone | 15 of 16, after 22.6 | 22.6 of 25.6 | 2 |
| `EDGE`, `SPOT_MORE` 2 (kept) | none | 12.4 of 30 | 531 |

And with the turtle for him: taken over near 12 and near 20 minutes (48
takeovers each, 197 frames apart), a tank tried in the strip every 45 s
besides whatever the turtle does; the AI's won-lost-drawn, and the tanks
that found room of those tried (12 minutes, 20 minutes):

| | won-lost-drawn | tanks down |
|---|---|---|
| as it was | 0-96-0 | 4.9 of 5.0, 5.4 of 5.6 |
| `EDGE` alone | 0-96-0 | 13.3 of 21.4, 11.8 of 15.3 |
| ... `WALL_EDGE` 1 | 0-90-6 | 7.7 of 31.6, 10.4 of 16.6 |
| ... `WALL_EDGE` 1, `SPOT_MORE` 8 | 3-63-30 | 7.2 of 46.9, 8.1 of 31.0 |
| ... `WALL_EDGE` 1, `SPOT_MORE` 4 | 0-52-44 | 6.2 of 51.7, 6.8 of 36.8 |
| `EDGE`, `SPOT_MORE` 4 | 0-44-52 | 7.4 of 55.8, 7.0 of 39.5 |
| `EDGE`, `SPOT_MORE` 3 | 2-37-57 | 6.4 of 56.6, 6.0 of 38.9 |
| `EDGE`, `SPOT_MORE` 2 (kept) | 0-26-70 | 5.1 of 59.2, 5.6 of 43.9 |
| `EDGE`, `SPOT_MORE` 1 | 2-36-58 | 4.3 of 55.6, 5.6 of 40.1 |

As it was, five tanks took the HQ (150 HP a tank: nothing fought them).
With the guards nine tanks in ten find the strip full.

**Against the turtle alone** nothing is lost by it. His game taken over
near 12 and near 20 minutes (96 takeovers each, 97 frames apart), the
turtle for him, the AI's won-lost-drawn:

| | 12 min | 20 min | all 192 |
|---|---|---|---|
| as it was | 0-13-83 | 0-36-60 | 0-49-143 |
| `EDGE` alone | 0-11-85 | 0-28-68 | 0-39-153 |
| `EDGE`, `SPOT_MORE` 8 | 0-7-89 | 0-29-67 | 0-36-156 |
| `EDGE`, `SPOT_MORE` 4 | 1-13-82 | 0-28-68 | 1-41-150 |
| `EDGE`, `SPOT_MORE` 3 | 1-11-84 | 0-28-68 | 1-39-152 |
| `EDGE`, `SPOT_MORE` 2 (kept) | 1-9-86 | 0-28-68 | 1-37-154 |
| `EDGE`, `SPOT_MORE` 1 | 0-7-89 | 0-22-74 | 0-29-163 |
| kept, with `MIX_BUYS` 1 | 1-7-88 | 0-35-61 | 1-42-149 |

His game of 8 November (the AI P1) the same way, 48 takeovers each:
15-17-64 as it was, 19-4-73 now.

In the arena, all maps, TYPE A, 8 a map, both seats, the AI's wins and
losses now (as it was): against the turtle 68-8 (60-5), against the
raider 85-3 (87-2). On a human's pad against the CPU, TYPE A to D, 4 a
map, both seats: 142 won, 34 lost, 80 drawn (141, 37, 78). As the CPU's
brain against the original's AI, 8 a map: 64-0 from either seat, as
before (`make nightmare`); `EDGE` is every side's, so those games are no
longer move for move the ones before (with `edge=0 spot_more=0` they are:
nine games of every kind came out line for line the same as the build
before).

**Tried, off.** `WALL_EDGE` 1: the wall's ring places by an edge too.
In the first build they were the wall's from the start: his game taken
over at 12 and at 20 minutes, the turtle for him (which puts nothing down
by an edge), it lost 23 of 48 for 7 as it was, the sides the turtle came
from the thinner. Kept to places within 128 px of a spot the HQ was shot
from, it lost nothing against the turtle and gained little against the
tanks in the strip, and nothing once `SPOT_MORE`'s guards stood there
(the table above).

`MIX_BUYS` 1, the second fault: `ATTACK_MIX` 3 says every third buy with
the garrison half up is an attack unit, but `choose_buy` counted every
look at what to buy, so the attack unit's turn came and went while its
$670 wasn't there and the wall's turns between took the money as it came:
in his game 220 turns, one attack unit. Counting the units bought, and
keeping the money for the turn, it does what it says, and loses more:
against the turtle 53 of 192 for 39 before `SPOT_MORE`, 42 for 37 with
it (the last row above), 3 wall units fewer for half an attack unit more
a game at 20 minutes. So with the wall half up an attack unit still goes
only when the money for one is lying there; the parameter's text now
says so.

**The traps met.** `--shadow` (the AI thinking along before the takeover)
is for the AI as it was when the game was recorded. A changed one thinks
other thoughts than the pads played and takes over believing them: with
`EDGE` on it lost 94 of 96 taken over at 20 minutes thinking along and 23
not; with `SPOT_MORE` it had counted the wall's units as its guards. And
a seed alone changes a takeover little (both sides think the same till
the game's own random numbers come into it; some seeds played the very
same game), so a first table of 24 seeds at one frame said `EDGE` lost 12
of 24 where 96 takeovers 97 frames apart say 11 of 96 for 13.
`tools/hztakeover.py` (new) runs these tables: `--spread` for the frames
apart, `--param` to try a switch, `--put` for hz_arena's.

**His game of 13 November** (03:05 UTC; ABGRUND, TYPE A, the AI P2: still
045b8a6, this session's AI pushed and built for the server minutes
before but not installed; won by him at 14:36, his HQ untouched). The
strip again, and as his opening: a TAX-52 west of its HQ at 11:29 (56
HP), then from 12:05 five TAX-52s into the strip, picked up at his
outpost 4 and flown up the west edge, ten seconds a trip. The first stood
21 s at (213, 11) and took 308 HP: the AI's mech was 1,600 px off at his
side of the map, and nothing of its own stood by its HQ yet (one wall
unit). Its driver and the four after it took the rest: 968 HP from the
strip in two and a half minutes. Its own twelve attack TAX-52s took
nothing off his HQ (he had bought 13 units to stand, and a repair
truck).

That game taken over at 11:55, his jet left at his outpost, a TAX-52
tried in the strip every 25 s from 12:05 as he put his, 16 seeds: with
the AI as installed its HQ was on 137 at 14:36 and gone in 5 games, and
by 25 minutes gone in all 16, after 7 tanks; with this session's it was
on 825 at 14:36 and at 25 minutes, 2.1 tanks of the first 7 finding room
and 5.4 of 32.

Not done: the other three faults of that game (its outposts' guards
killed one by one by his mech on foot, the wall's idle TAX-52s, the
attack unit put down by its own outpost), and an AI that puts its own
units down in a strip like this one.

The turtle is kept without `EDGE` (`TURTLE_PARAMS`: `edge=0`,
`mix_buys=0`), and the raider is run so (`--param2 edge=0 --param2
spot_more=0`).

## Holds put back, outposts out of reach, and lives spent (session 74)

The owner's first game against the server's AI of session 73 (3aa977b:
14 November 01:10 UTC; ABGRUND, TYPE A, the AI P1, PATHS, UNITS and
REPAIR on) was won by him at 45:32, his HQ never under 898. As it was
read he asked: "Is there a reason the AI obsessively avoids getting hit by
missiles? Sometimes a suicide run is a valid strategy. There is no cost in
losing a plane other than the time to regenerate"; "Every time one of my
infantry take a slot in their outpost, Nightmare never bothers to replace
it with its own infantry";
and then: "even if the base with missile launchers aren't a target, it
navigates out of the missiles range. There is no such thing as 'Past its
reach'. Where did this concept come from? Nightmare never builds supply
or repair trucks, I just wear out the missile stock in your launchers
then there is no more problem."

**What the recording showed.**

- *Its own outposts' holds.* An outpost holds four. Each INFANTRY of the
  enemy's that goes in takes one of the owner's holds off, and his fourth
  turns it (terrain.c, `unit_capture`). `nightmare.c` never read
  `HzPost.hold`: an outpost was its own until the flag turned, and its own
  was never one to send INFANTRY to. His men went into its outpost 6 at
  13:23, 15:47, 17:21 and 17:47 (4 of the 8 he put down), 4 minutes 24 s
  in which it bought 8 units for $3,380 and no INFANTRY; into outpost 7 at
  22:32, 22:50, 24:20 and 24:38, 3 units for $1,190. When one man of its
  own got into outpost 6 at 19:52, he put one of his back 23 s later.
- *Outposts out of reach.* It took 6 and 7 in the first two minutes, sent
  12 INFANTRY at outpost 5 (5 in, against his 7) and nothing else: 8, 3
  and 0, in its own half of the map, 992, 896 and 1,376 px from its bases,
  stood empty till he took them at 11:47, 13:02 and 12:16
  (`CAPTURE_REACH` 760, the first tuning's: there and back on a tank). 2
  outposts to his 7 from minute 13, 1 from 17:47, none from 24:38;
  $20,870 spent to his $85,567. Taken over at 9:27 with his side idle for
  ten minutes it sent no INFANTRY and bought 11 guards.
- *The missiles.* By its HQ it did not shun them: the mech was destroyed
  45 times, 43 by missiles, all from 18:20 on. He made outpost 6 his nest
  (3 SAM-42s, 11 GRM-34As by 35:22, 437 to 716 px from its HQ, two
  SUPPLYs, repair trucks) and put 81 attack units down under it, one
  every 11 s; it went for each tank where it stood and was destroyed in
  about 2 s. A death cost 6 to 7 s, and a quarter tank: `Mech_Respawn`
  gives a mech a quarter of its fuel, so it flew 340 px to his tank, found
  the way home and `FUEL_MARGIN` more than it had left, and turned back as
  it landed: 30 of the 45 deaths were on that flight home, with ammunition
  left. And the bans stood: after three deaths by its HQ no SAM-42 or
  GRM-34A was fought (`NEST_DEATHS`: he lost none of his 22, nor a
  SUPPLY); an outpost in any launcher's reach was never one to take; the
  empty jet flew round every launcher's reach.
- *Its own wall ran dry.* At the end its three SAM-42s had no missile left
  and three of the TAX-52s round its HQ no ammunition: it never bought a
  SUPPLY.

**What it does now** (all against a human; the turtle and the raider are
kept without them, `TURTLE_PARAMS`):

- `HOLD_BACK` 1. An outpost of its own with a hold of the enemy's in it
  gets INFANTRY for each hold lost, before any other buy, the money kept
  for it (`hold_post`, first in `choose_buy`; not while its HQ is being
  shot; not while `HOLD_HOT` 3 of the enemy's launchers reach the
  outpost). The game's outpost order walks it in (`outpost_wanted`: an own
  outpost with an enemy hold is one to go to). Those bought are on their
  way 25 s (`hold_bought`) and not bought twice; a lot that put no hold
  back is a failure, the next goes down a quarter turn round the outpost
  (`capture_drop`), and three in a row leave it `CAPTURE_REST`.
- `CAPTURE_FREE` 1. With fewer than `CAPTURE_MIN` outposts and none to
  take in `CAPTURE_REACH`, one that is nobody's is taken wherever it is,
  the nearest its bases first (`pick_capture_of(n, true)`), while the
  enemy has no attack unit out (the trip takes the mech 20 s and more
  from its HQ). The INFANTRY goes down as far as the fuel takes it and
  walks on. For the enemy's outposts the reach is kept: with
  `CAPTURE_HUMAN` 1,400 it sent 74 INFANTRY a game at outposts the turtle
  held and lost 12 of 32 for 1.
- `CAPTURE_HOT` 3. An outpost in the enemy's missiles' reach is one to
  take while fewer than three launchers reach it (`pick_capture_of`,
  `home_post`).
- `FLY_STRAIGHT` 1. Carrying nothing, a flight goes straight through the
  launchers' reach (`fly_to`). With a unit aboard it goes round as before:
  shot down carrying, the unit is lost.
- `FIGHT_ON` 1. A fight is not left for the fuel home, nor, in the
  missiles' reach, for repair (`fights_on`, `needs_service`,
  `task_fight`'s `leave`): it shoots till it is destroyed.
- `SUPPLY` 2, `REPAIR` 2. Once two of the units standing round its HQ are
  under a quarter of their ammunition or missiles and no SUPPLY of its own
  is there, it buys one (208) and puts it down by the emptiest
  (`supply_wanted`, `supply_drop`, cargo `NM_CARGO_SUPPLY`); in a game
  with the REPAIR patch a repair truck the same way once two are under
  three quarters of their HP, when its price is there and its HQ is not
  being shot.
- `STRIKE` 1, `STRIKE_R` 720. With nothing to buy, carry or fight and its
  HQ not shot in the last minute, the mech goes for the enemy's nest by
  its HQ (launchers within 720 px of it, or with an outpost of its own in
  their reach; SUPPLYs and repair trucks within 320 px of one):
  `pick_strike`, the last thing `decide` tries before it waits. The unit
  the fewest launchers' missiles reach first (the nest taken from its
  edge), of those the repair trucks, then the SUPPLYs, then the
  launchers, and the one it went for last before another. The fight is
  worth 1, so anything `pick_threat` finds takes the mech off it.
  `NEST_DEATHS`, `HOME_GRAVES` and `TENDED` still keep `pick_threat`
  itself off the launchers: a strike is what the mech does with time it
  has, not instead of its HQ.

**Tried, off.**

- `CAPTURE_FREE` 2: nobody's outposts past `CAPTURE_MIN` too, while its
  attack is not paying (the enemy HQ not shot in `ATTACK_GAP`). In the
  takeovers below no better (71 won, 27 lost for 81 and 29). From the
  start against the raider it turned wins into draws at the hour: on
  ABGRUND, LOCH, EISFREI and WALDUNG as P1, 7 won of 16 for 14. The 800 a
  fifth outpost cost were the attack units that had kept the raider's HQ
  bare, the mech carried INFANTRY instead of fighting by his HQ beside
  its tanks, and once a wall stood there its attacks did nothing more.
  Taken whatever its attack (the first build), 0 of 16.
- `ONE_WAY` 1: INFANTRY for an outpost carried there on the fuel one way
  where there and back is too far, the jet run dry on its way home built
  again at its HQ (`leg_ok`; about 1,200 px on a tank for 760). No more
  outposts for it in the takeovers (3.4 to 3.4), two deaths more a game.
- Strikes at whatever stood within 800 px of any base of its own: against
  the raider it flew across the map from the INFANTRY at its outposts and
  lost 27 of 72 for 14. Strikes while its HQ was being shot: taken over at
  17 minutes in that game, the turtle for him, 27 lost of 32 for none (the
  mech away, or being built again, as his tanks came). Strikes without
  `FIGHT_ON`: 20 strikes, half a kill. `FIGHT_ON` for repair only: the
  mech still turned back for fuel as it landed.

**His nest, his side idle.** The game of 14 November taken over with the
nest standing and nobody playing his side (his trucks still mend and
refill), 8 starts 97 frames apart; what stood within 900 px of its HQ at
the takeover and after, and the mech's deaths:

| | SAM-42 | GRM-34A | SUPPLY | repair truck | his missiles | deaths |
|---|---|---|---|---|---|---|
| 22:13, 5 minutes, as it was | 3 to 3 | 6.8 to 5.4 | 1 to 1 | | 126 to 81 | 6.0 |
| ... now | 3 to 0.4 | 6.6 to 0.8 | 1 to 0.1 | | 124 to 10 | 12.0 |
| 36:40, 15 minutes, as it was | 3 to 3 | 11 to 10.9 | 2 to 2 | 2 to 2 | 148 to 167 | 18.2 |
| ... now | 3 to 2.6 | 11 to 7.0 | 2 to 1.6 | 2 to 1.0 | 148 to 90 | 81.6 |

The young nest is gone in five minutes, and INFANTRY goes out for the
outpost after (3.5 a game). The full one, eleven GRM-34As, shoots the jet
down before it lands more often than not: four of them in a quarter of an
hour for 82 deaths, the mech being built again more than half the time.
It costs nothing it was using (the same buys either way), and his
missiles go.

**His recordings taken over.** His six games (14, 13, 10 and 8 November on
ABGRUND, 1 November on VULKAN and LOCH) at 16 points between 5 and 25
minutes, 8 starts 197 frames apart each, played to the hour, against the
turtle, the raider and the turtle with a nest of 8 launchers by the AI's
HQ (`nest_units=8`); the AI's won-lost-drawn, 128 games a column:

| | all 384 | the turtle | the raider | the nest builder | its HQ, his | outposts |
|---|---|---|---|---|---|---|
| as it was | 55-55-274 | 19-9-100 | 2-36-90 | 34-10-84 | 662, 666 | 2.2 to 6.3 |
| now | 81-29-274 | 28-4-96 | 5-25-98 | 48-0-80 | 784, 601 | 3.3 to 5.5 |
| ... `CAPTURE_FREE` 0 | 76-27-281 | 26-5-97 | 1-22-105 | 49-0-79 | 797, 626 | 2.8 to 5.7 |
| ... `CAPTURE_FREE` 2 | 71-27-286 | 23-4-101 | 5-23-100 | 43-0-85 | 787, 621 | 3.4 to 5.5 |

A game now: 11.0 INFANTRY bought for holds, 2.5 for outposts out of reach,
2.1 SUPPLYs, 5.3 strikes; 7.1 deaths (6.3). The raider still takes most of
what it loses: from 20 minutes on in his games of 8 November and on LOCH
the position is lost either way.

**In the arena** from the start, all maps, TYPE A, 8 a map, both seats,
the AI's wins and losses of 128: against the turtle 88-1 (68-8 as it
was), against the raider 87-1 (85-3). On a human's pad against the CPU,
TYPE A to D, 4 a map, both seats, and as the CPU's brain, nothing
changes: no human plays. With every switch of the session off ten games
of every kind came out line for line as the build before.

Checks (14 November): `make nightmare` P1 100%/0%, P2 100%/0%; `make
ahead` PASS (1,075,519 passes foreseen); `make unitcap` PASS; `make
replay_check` all recorded; `make netplay_check` all alike. The game's own
code is untouched (`NET_GAME_CODE` stays 10), so `make check` wasn't run.
The N line adds `holdbuys`, `freebuys`, `supplies`, `strikes`,
`strikekills`, `foughton`, `holdgaveup`, `supplybuys` and `repairbuys`;
`HZ_ARENA_HOLDS` adds each outpost's holds (P1's/P2's) to the trace's O
line.

**Not done.** His mech on foot shoots its guards one by one (the server's
AI never fights his mech on foot: `MECH_FIGHT`); the wall's idle TAX-52s;
an attack of its own once his HQ has standing units and SAM-42s by it
(after minute 7 the atlas passed no drop in reach, and six attack tanks
went down as guards); a way into a full nest.

## Room for its trucks (session 75)

The owner, told the next step was to put the SUPPLY down by its emptiest
launcher: "You aren't leaving space for supply trucks or repair trucks to
reach the units, you can't just drop the supply truck in position if it's
over crowded with units."

**His game of 15 November (LOCH, the AI P1).** At 65:00 it had 43 units
within 360 px of its HQ, all standing: 20 SAM-42s, 12 TAX-52s, 9 turrets
and its two trucks. Most stood in one block north-west of the HQ, where
it had been shot from: the guards for those spots each went down within
64 px of a spot (`guard_drop`), up to three a spot (`SPOT_MORE`), with
nothing kept between them. From one unit's box to the next was 0 to 12 px
for 40 of them; a truck's box is 24. Its SUPPLY, put down at the block's
edge, made for the nearest empty launcher, found no way to it
(`route_find`), took the next nearest, found no way to that either, and
went back to the first: a SUPPLY passes over one customer at a time
(`skip_id`), and the two nearest were both shut in. So it went from one
to the other for the rest of the game while 15 launchers it could have
reached stood empty, and 17 of its 20 SAM-42s ended with no missiles. Put
down among them it could not have been: the mech carrying a SUPPLY goes
down only where no unit's middle is within 40 px of its own
(`Mech_CheckLandBlocked` widens both boxes by $C for a SUPPLY).

**What stands by its HQ now (`HOME_LANES`, against a human).** Its wall
and its guards for the spots its HQ was shot from go down only on block
places: blocks of 2 x 2, 32 px apart (8 px between boxes), with a lane 40
px wide between blocks every 96 px both ways, the lanes through the HQ's
middle. Every unit has a lane on two sides; a truck in one is 8 px from
it (a SUPPLY serves from 16), with its middle in a 32-px cell no unit's
is in (the SUPPLY's route search keeps a unit a cell). The HQ's ground is
no ground to 80 px either side of its middle on every map (5 x 5 cells),
so no unit's middle within 160 px: round the HQ that leaves a ring road
from which every lane leads out, and its SUPPLY and repair truck go down
on it, 112 px from the middle and 48 from the first units (past the 40 a
SUPPLY going down wants), on the side nearest the unit that wants one. A
place is taken only if the lane beside it is ground a truck reaches from
that ring (a search along the lanes from it, `lane_map`). The first ring
of blocks is 160 px out (28 places), the second 224 and 256 (80); past
that, as before. The wall unit's place is chosen as `ring_drop` chose
(the fewest standing near for what the place is worth), a guard's the
free place nearest its spot within 56 px (none: within 64 px of it, as
before). The mech puts a unit down within 6 px of its goal; the lanes
stay 28 px wide or more even then, and a guard within 56 + 6 of its spot
is one `spot_guards` counts (64). At first it took places up to 96 px
off: on EISFREI (its HQ in a corner, few block places near the spots)
those guards counted for no spot, so it bought more and more of them, 82
a game for 44, and 13 wall units for 37.

**A truck with nothing in reach is lifted.** In a game against the turtle
on LOCH its SUPPLY, carried home, was put down where the jet was when the
enemy's came at it (a dogfight), 100 px south-east of its HQ, with the
wall north-west, 230 px and more away, past the 200 a SUPPLY looks; it
stood there from 25:20 to the end with up to 34 units empty. Now a truck
of its own by its HQ that has no unit wanting it within its search (a
SUPPLY 184 px, a repair truck 384), while one farther off does, is lifted
and put down on the ring by that one; once in 20 s at most.

**The test** (`hz_arena --drain 1,7200,54000,1`, new): every 2 minutes
from 15:00 the arena empties the missiles and ammo of the AI's units
standing within 400 px of its HQ and halves their HP, as a long fight
would; the D line says what had come back by each drain (its trucks'
work). Against the turtle, `--fixes 7f7` (the server's patches), the share
of what they could hold back by the next drain, with lanes and without:

| map, seed | missiles | ammo | HP |
|---|---|---|---|
| ABGRUND 1 | 21% / 12% | 3% / 10% | 71% / 71% |
| ABGRUND 2 | 50% / 15% | 19% / 15% | 66% / 68% |
| VULKAN 1 | 51% / 23% | 55% / 35% | 82% / 74% |
| VULKAN 2 | none / 29% | 89% / 32% | 97% / 71% |
| LOCH 1 | 39% / 61% | 43% / 46% | 83% / 79% |
| LOCH 2 | 58% / 43% | 57% / 41% | 79% / 71% |
| STRAND 1 | none / none | 93% / 75% | 96% / 75% |
| STRAND 2 | none / 25% | 100% / 18% | 70% / 63% |
| EISFREI 1 | 43% / 20% | 22% / 21% | 66% / 60% |
| EISFREI 2 | 24% / 26% | 19% / 21% | 72% / 70% |

(none: no launcher at home at the drains.) STADT, WALDUNG and OASE: no
wall bought before the games ended. Not one of these lost with lanes (LOCH
2 won where it drew without); the trucks were lifted 0 to 9 times a game.
Every truck still serves one unit at a time, and one SUPPLY for a wall of
20 is not enough against a drain this hard. With no human playing nothing
changes: against the CPU every game came out line for line the same with
`home_lanes` 0 and 1. The turtle keeps the old way (`home_lanes=0`). The
N line adds `lanedrops`, `yarddrops` and `trucklifts`.

**Against his recordings** (`tools/hztakeover.py`: the five server games
of 14 to 16 November taken over at 5:00 and 12:00, 4 seeds each, against
the turtle and the raider, `--foe bot --foe-param raid=9`), the AI's
wins, losses and draws with lanes and without:

| game | turtle | raider |
|---|---|---|
| ABGRUND (P1) | 2-0-6 / 3-0-5 | 0-0-8 / 0-0-8 |
| EISFREI (P2) | 0-0-8 / 0-0-8 | 0-1-7 / 0-0-8 |
| LOCH (P1) | 4-0-4 / 5-0-3 | 0-0-8 / 0-0-8 |
| OASE (P1) | 3-0-5 / 3-0-5 | 0-1-7 / 0-1-7 |
| STADT (P1) | 6-0-2 / 5-0-3 | 0-0-8 / 0-0-8 |

15-2-63 with lanes, 16-1-63 without (with guards up to 96 px off,
18-3-59, EISFREI's walls the 13 a game above). EISFREI's raider at 12:00
taken further, 40 seeds: 0-3-37 with lanes, its HQ 719 at the end on
average, 0-2-38 without, 749. In its seed 4 with lanes the two games are
the same to frame 62,220, where a drop place drawn at random differs (the
lanes' scoring draws from the AI's numbers too), and part from there.

## Nests by its HQ: tanks for the SAM-42s, boats let come, buys not held (session 77)

The owner's games of 17 and 19 November against 8da2158 (EISFREI, TYPE
A, the AI P1; read in session 76: HANDOVER.md, Status) were lost and won
by attacks on the HQs, but the most of both went on the mech: destroyed
97 and 86 times to his 7 and 8, nearly all in fights at his nests, and on
the 19th it held $7,000 to $10,700 from minute 17 to 31 and bought
nothing. Offered three changes (a unit by its HQ that can't hurt it yet
bought past, as one away from it is; `NEST_DEATHS_ALL` 1; a strike given
up after a few deaths with nothing killed), he answered: "yes go ahead
with all thre, except that nest was being built with the specific intent
of preventing the AI from blowing up boats dropped close to the base. So
the nest of GRM weren't an unreasonable target, but perhaps the wrong
tactics were used to clear it?"

**The nest of the 19th.** From 9:48 he put ST-57Us down about (585, 490),
530 px from its HQ, ordered to attack it, about one a minute (15 in the
game), and from 11:19 GRM-34As round (450-485, 575-640), 530 to 565 px
from it; behind them seven SAM-42s, a SUPPLY and a repair truck round
outpost 3, 500 to 640 px from the HQ. The boats sail in under the
GRM-34As' missiles to about 275 px from the HQ. The mech went for each
boat where it was (one sent at the HQ is the HQ's threat anywhere,
`pick_threat`'s `at_hq`, so the rules that keep it off a place it died
at never applied) and was destroyed 34 times there from 10:27 to 24:47.

**What a nest is to the mech** (unit_ai.c, shots.c). A SAM-42 or GRM-34A
fires at the mech whenever it is within 256 px of it, flying or on foot
(`aim_at_mech` takes no notice of its form), and the missiles hit the
robot as well as the jet; they home (re-aimed every fourth frame), and a
side has 8 in the air at most. A SAM-42's takes 72 of the mech's 256 HP,
a GRM-34A's 60. A SAM-42 has no gun at all (no ammunition; `unit_sam`
fires only missiles); a GRM-34A's twin guns (20 a bullet) take what comes
within 128 px, a unit before the mech; and no missile ever hits a unit.
So against a SAM-42 a tank is the weapon and the mech is not: a TAX-52 by
one is shot at by nothing and takes it in four shots (96 HP, power 28).
The GRM-34As shoot back at both, and are the mech's.

**What it does now** (against a human; the turtle keeps the old ways,
`TURTLE_PARAMS`; with no human playing every game is line for line as
before, 64 against the CPU checked):

- `HUNT_STRIKE` 1, `HUNT_HOME` 2. Hunters (session 68 (17): a TAX-52 told
  to guard where it goes down, 32 to 64 px from an enemy SAM-42, where no
  enemy gun reaches and fewer than `HUNT_COVER` launchers do) for the
  SAM-42s within `STRIKE_R` of its HQ too, the nest it would strike at,
  not only those whose missiles reach its HQ or an outpost of its; two at
  work at a time, not one (`sam_worth`, `hunt_wanted`).
- `NEST_WAIT` 1. A unit sent at its HQ is one at the HQ for `TENDED`,
  `HOME_GRAVES` and `NEST_DEATHS` only once it is within its gun's reach
  and 192 px of it (`hq_soon`): a boat under the GRM-34As where the mech
  was destroyed three times is left till it comes out.
- `BUY_HOME` 2. A threat by its HQ that is not about to shoot it (the same
  test, `hq_soon`; not while its HQ is being shot, nor at `HQ_ALARM`) no
  longer holds the buys: the unit is bought first as for a threat away
  from the HQ (`BUY_FIRST`), and a unit waiting to go is picked up first,
  before that fight or one away from the HQ (`pickup_ready`). Not while a
  unit of its is stalled: `nightmare_think` puts a pickup back to decide
  for the rescue, and on LOCH against the turtle the first version chose
  the pickup again every frame and stood still for minutes (24,000 times
  in a game, 2 games of 64).

- `STRIKE_GIVEUP` 3, `STRIKE_REST` 10800. The mech destroyed three times
  in a row on strikes with nothing killed: no strike within 320 px of
  where it was last destroyed for three minutes, six the next time in a
  row with nothing killed between, and so on to fifteen (`pick_strike`;
  a strike's kill puts both counts back). Resting three minutes each
  time, it gave the same nest up again and again (in the game of the
  17th, 23 strikes a game from 25:00 against the raider; growing, 17).

`NEST_DEATHS_ALL` stays 0, as he asked: the GRM-34As are still fought.
The N line adds `homebuys`, `homepickups` and `strikegiveups`.

**The give-up, measured on its own.** At the nest of the 19th the
hunters do its work: without it, 15-0-1 with the boats and 31-0-1 idle
(below: 16-0-0 and 29-0-3 with it). Its worth is at the nest of the 17th
(below), where in his seven games (the table below) it halves the deaths
of that game at 12:00 (65.5 a game to 35.5 against the turtle, 65.5 to
37.5 against the raider) for the same wins and losses (24-0-40 and
1-4-59 without it, 24-0-40 and 2-4-58 with). Both EISFREI games taken
over at 15:00, 20:00, 25:00 and 30:00 against the raider, 8 seeds each:
3-3-58 without, 4-2-58 with. One stretch went the other way: the 17th
taken over at 25:00 and 28:00, 16 seeds 97 frames apart, 9-0-23 without
it, 1-1-30 with (5-0-27 before any of this), the AI delivering half the
attack units. There the wins come in runs of neighbouring seeds (seeds 3
to 7 and 9 at 25:00), the same few games played out: in seed 4, two
TAX-52s it carried out after being destroyed at the nest and starting
again at its HQ reach his HQ at 30:25 with nothing to stop them and take
it in under a minute; with the give-up it was not destroyed there then,
and they were never carried.

**His nest of the 19th with him idle** (`hz_arena --replay FILE
--takeover T`, his side standing still from there, 8 seeds; "SAMs" his
SAM-42s within 900 px of its HQ at the takeover and at the end): the
AI's wins, losses and draws, the mech's deaths a game, before and now:

| taken at | before | now |
|---|---|---|
| 12:00 | 6-0-2, 24.1 deaths, SAMs 7 to 2.9 | 7-0-1, 12.0, 7 to 0.5 |
| 18:00 | 5-0-3, 24.8, 7 to 4.6 | 7-0-1, 17.2, 7 to 0.6 |
| 25:00 | 8-0-0, 9.1, 7 to 6.9 | 8-0-0, 6.1, 7 to 0.0 |
| 32:00 | 0-0-8, 14.4, 7 to 6.8 | 7-0-1, 6.5, 7 to 0.0 |

**His boats under his GRM-34As** (the 19th taken at 17:00 and 12:00, his
side idle but for an ST-57U put down at (585, 490) every minute, ordered
at its HQ, as he did; 8 seeds): before 5-0-3 and 2-0-6, the mech
destroyed 23.1 and 24.9 times a game, striking 13.4 and 16.1 times for
2.6 and 0.2 kills, his two SAM-42s within 700 px still 1.8 and 2.0 at the
end; now 8-0-0 and 8-0-0, 12.5 and 11.5 deaths, 5.5 and 5.2 strikes for
2.6 and 2.1 kills, the SAM-42s 0.0 and 0.1.

**The 17th is not mended.** Its deaths there are not at the HQ's nest but
at his SAM-42s and GRM-34As round (995, 846), 1,080 px from its HQ and
100 to 220 px off outpost 5 (1104, 1040): carrying INFANTRY to the
outpost and striking from there (at 25:00 against the raider, 30 of 35
deaths there, 11 of them on deliveries). Six of his SAM-42s and his two
GRM-34As stand there, 1,100 to 1,350 px from its HQ, past `HUNT_STRIKE`'s
reach, and no hunter goes to them (`hunts` 0; why is not looked into
yet). Taken over with his side idle at 25:00, 33:00 and 40:00 it draws
every game as before, destroyed 19 to 29 times a game (27 to 33 before;
the give-up). Left for later: hunters for the SAM-42s by an outpost it
carries INFANTRY to, or no deliveries into a nest.

**Against his recordings** (`tools/hztakeover.py`, his seven server
games of 14 to 19 November, 4 seeds at 5:00 and 12:00, the 17th and 19th
also at 25:00, against the turtle and the raider: `--foe bot --foe-param
raid=9`, the raider with the old values), wins, losses and draws, the
sum over the takeovers of the mech's deaths a game, and of its strikes:

| | turtle | raider |
|---|---|---|
| before | 19-1-44, 296 deaths, 213 strikes | 2-4-58, 266, 153 |
| now | 24-0-40, 142, 47 | 2-4-58, 216, 64 |

The 19th at 12:00 against the turtle: 83.0 deaths a game to 16.2, 83.2
strikes for 3.8 kills to 6.2 for 2.5; the 17th at 12:00, 71.8 to 35.5.
Worse: the 17th at 25:00 against the raider, 2-0-2 to 0-0-4 (the
stretch above), and the 19th at 12:00 against the turtle, 1-0-3 to
0-0-4, the turtle's HQ left at 868 on average where it was 194 (and
800 where it was 374 at 25:00), why not looked into (the 17th's seed 4
above is one way it happens). Against the turtle ABGRUND at 5:00, OASE
at 5:00 and 12:00 and both EISFREI games at 5:00 won one or two more,
LOCH at 5:00 and STADT at 12:00 one fewer.

**From the start** (`tools/hzarena.py`, every map, TYPE A, 8 games a map,
the AI as P1 and as P2, against the turtle and the raider, `raid=9
dog_guard=0 post_raid=0 nest_deaths=0`), the AI's wins, losses and draws
in 64 games:

| | before | now |
|---|---|---|
| P1 against the turtle | 39-0-25 | 38-0-26 |
| P2 against the turtle | 35-0-29 | 32-0-32 |
| P1 against the raider | 9-2-53 | 17-0-47 |
| P2 against the raider | 4-8-52 | 7-9-48 |

No hunter was sent and no strike given up in any of them; the changes
there are the buys and pickups (`homepickups` 14 to 21 a game).
