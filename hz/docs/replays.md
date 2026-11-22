# Recorded games (session 59)

The owner: "It would probably be good for you to Analyze my game play
maybe me VS original AI and Nightmare, Right now Nightmare isn't very
scary."

Every game played -- against the CPU (either brain), two players on one
machine, or over the network -- is recorded and written when it ends to
`saves/replays/` (beside `settings.json`; `$HZ_SAVES/replays/` when set),
a file a game:

    20261021-013005-VULKAN-NIGHTMARE-A.hzr

when it started (UTC), the map, who played (NIGHTMARE or ORIGINAL for
the CPU's brain, 2PLAYERS, CPUS, NET-P1 or NET-P2), the TYPE.
`HZ_REPLAYS=0` records none. `*.hzr` is git-ignored: a player's games are
never the repo's.

## What is in one

A game is the same from the same start and the same pads -- the network
game rests on it -- so a recording is the start and the pads
(src/game/replay.h):

- The start: what `Game_Start` and the frames after it read that a game
  doesn't set for itself. `v_options` (mode, TYPE, option2, map), the
  PATCHES, the brain for each CPU side, the own view, `v_rng_seed_saved`
  (Game_Start's seed: the title moves it every frame), `v_cpu_boost`
  (the last game's, until the first second's tick), the win record, and
  the pads already set. Also the game's id (`net_game_id`: the code's
  `NET_GAME_CODE` and every byte of the assets and mods), when, and in a
  network game our side and the rival's name.
- Both pads at every frame hand-over from `Game_Start` on (the fade in
  and a pause's frames too), run-length coded.
- The PATCHES changed on the Esc screen mid-game, with the hand-over they
  came in (a game here takes them at once).
- A hash of the game every 60 hand-overs (`state_hash_game`: the game
  without its display, statehash.c), to check a replay against.
- The end: the hand-over, the winner (or none), why (`over`, `quit`,
  `closed`, `RIVAL LEFT` ...). Recording stops as the game's loop ends,
  before the finale.

The file is text: `HZREPLAY 1`, a line a start value, `pads N` and lines
`COUNT P1 P2` (hex), `patches_at FRAME HEX`, `hashes N` and a hash a
line, `end FRAME WINNER WHY`. A 4-minute game is about 7 KB.

A replay starts from a cold boot (as a network game does: `game_init`,
`snd_init`, the start), where the game recorded started from wherever
the title left the machine. What a game keeps from before its start is in
the start above; the display isn't (a title's tiles left in VRAM), which
is why the hashes leave it out.

## hz_replay

    make hz_replay
    ./hz_replay ASSETS FILE... [--events] [--minutes] [--check]

Plays each with no window and no sound (a 4-minute game in a tenth of a
second here), checks every hash, and says for each side: the mech's
deaths and how (out of fuel, shot down as a jet, carrying, on foot;
whether the enemy mech was near and firing), its gun (shots; hits and HP
taken), what it bought with which orders and what it spent, the most
money it left unspent, purchases refused, units carried and where they
were put down, units lost, outposts taken, lost and held at most, where
its time went (flying, carrying, on foot, the order menu, destroyed; by
its own HQ, by the enemy's), fuel under a quarter, presses of A, B (and
how many 2 frames after the last: TURBO) and C, its HQ's lowest HP and
the HP lost each minute; the nightmare AI's own counts for a side it
played. `--minutes` adds a line a minute (money, outposts, units, HQs,
deaths), `--events` each purchase, unit put down, outpost and death with
its time. Times are the game's clock (`v_frame_counter`: a pause doesn't
count).

A replay played on another build or with other mods is said so, and may
not play the same; a hash that differs is said with the hand-over, and
what follows is no longer the game played. `--check` only plays them and
says whether each is the game recorded (exit 1 if one isn't).

## The check

`make replay_check` (tools/hzreplay_check.py): four games by
`hz_headless` from scripted random pads -- P1 against the nightmare AI
(VULKAN), against the original's on TYPE C with Start pressed now and
then (pauses), P2 against the nightmare AI (mode 6, LOCH), two players
(EISFREI) -- each written as it ends and played again by `hz_replay
--check`: all four the games recorded (session 59: 9,890, 19,790, 8,884
and 11,790 frames, every second's hash alike). The replays the network
checks write (`make netplay_check`, each player's own `HZ_SAVES`) play
the same too: 23 of 23 in session 59, the own-view games among them.
