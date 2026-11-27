# The network game (session 56)

Two players on two machines, each running the whole game, through a small
server that pairs them up. Session 52 checked that the game comes out the
same from the same pads on every build (`make netcheck`); this is the rest
of the plan it wrote down: lockstep, the start agreed, and a relay.

## The design

**Lockstep.** Both machines run every frame of the game from the same
state, and only the pads travel: a byte a side a frame. Frame N runs when
both pads for frame N are in. The pad read on a frame is the one for
`delay` frames later, so a pad has that long to cross the line before it
is needed; the first `delay` frames have none on either side. Nothing else
the game reads comes from the machine it runs on (session 52 went through
it): its only floats are in the asset loader, its only clock the saves'
time stamp, and the title's question to the sound (`snd_music_on`) is
answered "playing" in a network game, which never shows the title.

**Not rollback.** The game is the 68k's loops, which hand each frame over
from deep inside (`VDP_WaitVBlank`'s place); going back a frame would mean
going back up the C stack too. Lockstep with a few frames of delay is what
the original's design allows, and it is enough for a game at Herzog Zwei's
pace.

**The start.** A network game begins from a cold boot on both machines,
whatever each was doing: `game_init`, `snd_init`, two players on the split
screen (mode 0), the host's map, difficulty and PATCHES, and the server's
seed for the random numbers (`Game_Start` takes `v_rng_seed_saved`).
(Session 59) Or, with the host's VIEW OWN SIDE ONLY, each machine its own
side alone, full width ("Each side its own screen" below). Saves
can't split it: two humans never record a win, and the fresh `game_init`
clears the win record on both sides. The two must have the same game: the
id each sends the server is a hash of every byte the game read while
loading its assets (mods included; `hz_file_hash_begin/end`) with
`NET_GAME_CODE` (src/net/net_client.h), which is bumped whenever the code
changes what a frame does. The server pairs only players with the same id.
(Session 78) A PNG is hashed by its picture (size, palette, pixels), not its
bytes, so files the game makes itself from a cartridge on its first start
(docs/first_start.md), whose PNGs deflate differently, play with this
tree's `assets/`; `NET_GAME_CODE` went to 12 with that. `./hz_gameid DIR`
prints a folder's id.

**A split.** Every 60 frames each side hashes the state (src/game/
statehash.c: the globals, mechs, units, posts, shots, the grid, VRAM, CRAM,
VSRAM and the VDP's registers; `hz_netcheck`'s parts, moved there) and
sends it with its pads. (Session 59) In an own-view game the display is
left out (`state_hash_game`): the two machines draw different things. The first that differs stops the game on both
sides with OUT OF SYNC: the side that sees it sets the top bit of the hash
frame it sends, so the other hears of it even if its own comparison never
comes.

**The server** never runs the game. It keeps the players online, the list
of those waiting, pairs them, and passes each game's pads from one side to
the other as they come. One UDP socket, one thread, fixed tables made at
start-up, an id that finds its slot at once: on this server (an 8-core
Haswell VPS) a round trip through it takes 9 microseconds, and 500 games
at once (1000 players, 44 frames a second each, the most hz_netbot could
drive) took 31% of one core and 2.8 MB.

## The datagrams (src/net/net_proto.h)

Every datagram starts `H` `Z` version type; numbers are big-endian. After
HELLO everything the game sends carries its id and token, which the server
checks; until then no answer is bigger than what came (HELLO is padded to
64 bytes), so a forged sender can't use the server against anyone.

| game -> server | server -> game |
|---|---|
| HELLO nonce game name | WELCOME nonce id token waiting players (session 59: view rule); REFUSE nonce reason (full, other version, busy, unknown id) |
| PING time (a second, in the lobby) | PONG time waiting players |
| LIST offset | ROOMS total offset count, then each room: id, the host's name, same game?, seconds waiting |
| HOST settings rtt | WAITING room |
| JOIN room settings rtt (room 0: quick) | START room side seed settings rival rival_rtt; NOROOM room |
| READY room (START came) | |
| INPUT room ack hframe hash first count pads | INPUT ack hframe hash first count pads (the rival's, passed on) |
| LEAVE room | LEFT room reason (left, timed out, never started) |
| BYE | |
| (session 66) AGAIN room want | AGAIN room rival's want; START (a rematch: a new room id) |

**The rooms.** HOST waits in a room on the list, to be picked; JOIN with
a room's id takes it. A quick match (JOIN room 0) takes the oldest room
waiting with the same game, else waits in a room of its own, which the
next quick match or HOST takes at once (a HOST then plays its own
settings). A room whose host hasn't been heard from for 3 seconds (the
lobby PINGs every second) isn't offered: its game has gone without a BYE.
START is sent again every 200 ms until each side answers (READY, or its
first INPUT), and given up after 5 s (the side that did answer hears
LEFT). A HOST or JOIN arriving after its START (the request was sent
again before the START came) gets the START again and nothing else.

**INPUT.** Each carries every pad the rival hasn't said it has (its `ack`,
the first frame it still needs), up to 64: so however many datagrams are
lost, the next one that gets through fills the gap, and a gap never opens.
Each also carries the sender's latest state hash. The game sends one every
frame, and while it waits for the rival one every 16 ms; the server
passes each on at once.

**Timeouts.** The server drops a player it hasn't heard from for 15 s (the
rival hears LEFT). The game gives up on a rival after 10 s without an
INPUT, on the server after 10 s without anything in the lobby, on HELLO
after 5 s, and a HOST or JOIN after 5 s.

**Limits.** A player may send 300 datagrams a second (the game sends at
most about 120); the server takes 500 HELLOs a second (REFUSE busy, and
the game tries again). `--players` sets how many may be online (1024 by
default, 32768 at most).

## Playing (the game's side)

The Esc screen's NETWORK page (src/game/sysmenu.c; the player's README
has it in a player's words): SERVER (the known servers, left and right),
ADDRESS (session 57: the chosen server's; A types another), NAME,
CONNECT, QUICK MATCH, HOST GAME (CANCEL while waiting), MAP, (session 59)
VIEW, (session 58) NIGHTMARE, and the players waiting, each a row to
choose (4 shown since VIEW took a row: the page is the screen's height). The page is a view: main.c fills
it from the client every frame and does what it asks.

**ADDRESS** (session 57) shows the chosen server's address, its port only
when it isn't 19890. A on it while offline types one from nothing, as
NAME is typed: letters, digits (the keypad's too), `.`, `-`, `[` `]`,
and `;` for `:` (the menu sees keys without Shift); with a pad, up and
down step the last character, digits first. Enter reads it as
`--server` is read (`sysmenu_parse_address`) and adds it to the player's
own servers, chosen (an address already known is just chosen); one that
can't be read stays on the row with NOT AN ADDRESS under it. Escape, or
Enter with nothing typed, leaves the server as it was. The list holds 12:
when it's full a new address takes the place of the oldest of the
player's own, never the one chosen. Longer than the row, the end of the
address shows, `<` where its start went.

The lobby is polled every frame wherever the game is, so a player can
play the CPU while waiting: when a rival is found, whatever runs is left
as QUIT leaves it (`g->quit`) and main's loop starts the network game.
Every controller on the machine drives our side; the rumble is our side's.
(Session 66) The Esc screen doesn't hold a network game: main.c ticks it
a frame at a time beside the game (`net_menu`), drawn over each frame,
and our side's pad is none while it is open (the buttons work the menu),
so the rival plays on and never waits for it; what closed it is kept
from the game until let go, as offline. Opened while we wait for the
rival's pad, it is drawn where WAITING FOR RIVAL would be. A game that
ends with it open closes it. (Until session 66 it held the game, and so
the rival's, which showed WAITING FOR RIVAL until it closed.) Start is
still the game's own pause, both sides', as on the console. DISCONNECT
ends a game (the rival hears we left). When the game ends -- its results shown, or the rival gone,
or a split -- the NETWORK page comes up over its last frame (session 66;
until then the title came back), and closed, the title, still online.

**Each side's own end** (session 66). The owner asked whether it plays
a victory or defeat screen for each player, or a tie, and chose each
their own: the cartridge's two-player end showed both machines WINNER
1PLAYER SIDE (or 2PLAYER) and played "A Victorious bell is ringing"
(BRASS, song $D). Now each machine knows its side (`HzGame.net_player`):
the winner's shows YOU WIN and plays "The dawn of a new age" ($B), the
loser's YOU LOSE and "Funeral march for the METAL KNIGHTS" ($C), the
cartridge's own win and lose songs against the computer
(game_end.c); the photo and the results as before. The two displays
differ from the game's end on, so the state hash leaves the display out
from then (`v_game_over` set; statehash.c), as an own-view game's does
throughout. **A draw:** the 68k checks the HQs once a second, P1's
first, so both down in the same second gave P2 the game; a network game
now calls it a draw (`HZ_FIX_DRAW`, a bit of `HzGame.fixes` past the
PATCHES page's, set only for network games: `v_winner` `HZ_DRAW`): both
HQs blow up in the finale, by turns (both views shaken on the split
screen), DRAW on both machines, the funeral march. NET_GAME_CODE 6: a
build before it can't meet one after.

**The rematch** (session 66; the owner: "add a rematch it should
probably go back to network screen instead of reset"). A game that ended
as games do leaves both players in its room on the server
(`net_game_after`: AFTER) and puts each on the NETWORK page, its cursor
on REMATCH (HOST GAME's row, which it takes the place of there): PLAYED
and the rival's name under DISCONNECT, ASKED beside REMATCH once asked
(A again takes it back), and under it RIVAL ASKED, WAITING FOR RIVAL or
A: ASK FOR ONE. Each side sends AGAIN with its wish every half second;
the server keeps both wishes and answers each AGAIN with the rival's,
and when both have asked it starts the next game in the room: the same
sides and settings, a new seed, a new room id (so nothing late from the
last game is taken for the next), START to both, the game from a cold
boot as any. Anything else from the page -- QUICK MATCH, a room off the
list, NIGHTMARE, DISCONNECT -- or the page closed leaves the room, and
the rival hears it (RIVAL LEFT on its page). A server from before
session 66 never answers AGAIN: after 3 s the room is left and the page
says NO REMATCH HERE. A game cut short (the rival gone, a split) leaves
the room as before; the page says why. The server's AI asks for a
rematch at once after every game and waits up to 3 minutes for the
person's.

**The servers the game knows:** THIS MACHINE (127.0.0.1:19890) always;
`servers.txt` beside the game, a line each, `NAME HOST [PORT]` (the
name's spaces as `_`; `make dist` packs the tree's own `servers.txt` when
there is one: it is git-ignored, so an address is never in the source);
the player's own in `saves/settings.json`, those typed on ADDRESS among
them:

    "net": {
      "name": "PLAYER 123",
      "server": "127.0.0.1:19890",
      "map": 0,
      "delay": 0,
      "view": "split",
      "servers": [ { "name": "FRIENDS", "host": "example.org", "port": 19890 } ]
    }

`delay` 0 takes it from the round trips (ours to the server and the
rival's, which START brings: half their sum in frames, and 2 more); 1-15
fixes it. A name is made up (PLAYER and three digits) the first time and
kept from the first CONNECT.

**The command line:** `--server HOST[:PORT]` (also `[V6]:PORT`) adds a
server and chooses it (session 57: a port not 1-65535 is refused, as on
ADDRESS), `--name NAME`, `--net quick` or `--net host`
(online at start, then a quick match or wait as host; `--map` is the map
hosted), (session 59) `--view split|own` (VIEW, for this run). For the checks: `HZ_NETLOG` writes each hash (`H FRAME HASH`) and
how the game ended (`E FRAME WHY`), and (session 66) the Esc screen
opening and closing (`M FRAME open|shut`); `HZ_NET_FRAMES` quits when the
network game reaches that frame; (session 66) `HZ_NET_END=AT:WHO` puts
WHO's HQ (1 P1's, 2 P2's, 3 both) at 0 HP at network frame AT of every
game (hz_netai takes it too), `HZ_NET_REMATCH=1` asks for a rematch at
once after each game and leaves the page to itself, `HZ_NET_GAMES=N`
quits after N network games, and HZ_NETLOG has who won each (`R FRAME
P1|P2|DRAW`); `HZ_NET_ESC=AT:FOR` presses
Escape at network frame AT and again FOR frames on; `HZ_NET_LOSS` (0-0.9) drops that share of what is
sent and received, at random. (Session 63) `HZ_NET_LAG` holds what is
sent that many ms and `HZ_NET_JITTER` up to that many more, at random (a
long line on one machine); `HZ_NET_BALANCE=0` reads the balance but never
holds back ("The pace"); `HZ_PACE=1` writes the pace's line every 5 s.

## Running a server

    make hz_server
    ./hz_server [--port 19890] [--players 1024] [--ai-after 15]
                [--ai-side random|p1|p2] [--view host|split|own]
                [--verbose] [--quiet]

It needs nothing but the system and one UDP port open to the players. It
logs a line per game started and ended and, unless `--quiet`, a line a
minute (players, rooms, games, datagrams); players' addresses only with
`--verbose`. It builds on Windows too (MSYS2, or MinGW: Winsock).

On a Linux server, as a service with systemd (`server/hz-server.service`
has the steps): its own user made for it, the files read-only, IP sockets
alone, 64 MB at most. The players' machines need nothing opened: they
send first, so their routers let the answers back in.

## The nightmare AI when no one is there (session 58)

The owner: "add an option to play nightmare AI on the server if no
players are available", and when asked where it should run: both.

**Here.** The NETWORK page's NIGHTMARE row, PLAY IT HERE: a game on this
machine against the nightmare AI (docs/nightmare.md) as the CPU, P2, on
the page's MAP, while online the player waits for people -- a
quick match people only, so the server's AI can't end the game for
itself, or the room already hosted. A person found ends it for the
network game, as any game here is left for one. (Session 59) On the
page's VIEW: SPLIT SCREEN (the default) plays it on the split screen
(mode 1), the AI's view beside the player's, to watch what it does; OWN
SIDE ONLY wide (mode 5), as before. Offline it is just the
game against the AI. `--net nightmare` does it from the command line.

**On the server.** `server/hz_netai.c` (`make hz_netai`): a player the
server runs, online as NIGHTMARE AI, which HOSTs and waits. The server
lists it (one entry however many wait: SERVER AI on the page), and anyone
can pick it; a quick match left waiting `--ai-after` seconds (15 by
default; 0 never) with no one is given it, unless its JOIN said people
only (QUICK MATCH's left and right on the page: PEOPLE OR AI, the
default, or PEOPLE ONLY, settings.json's `"quick_ai"`). People meet people
first: a quick match never takes the AI's room at once, nor the AI's
HOST a waiting quick match. Against it the person plays on their own
map, difficulty and PATCHES, as P1 or P2 by a coin's toss each game
((session 68) `--ai-side p1` or `p2`: always that side; a rematch keeps
the sides). The counts players see (online, waiting) leave the AI out.

It plays main.c's network game with no window and no sound: the cold
boot, the lockstep, the hash. Its side's pad is the nightmare AI's, as
tools/hz_arena.c's bot plays a human's side (a human's pad, a human's
handicaps). It thinks where the arena's does, just before its mech's
update (`HzGame.mech_probe`), but on a copy of the game: only the pad it
wants comes out, sent as any player's pad is, and its memory
(`HzGame.nightmare[side]`) is kept. The AI writes nothing else (it calls
nothing on the VDP, the sound or the game's Random), and nothing in a
two-player game reads that memory, so the person's game and the AI's stay
the same: the hashes say so. (Up to session 79 `vdp_render` applied the
split's raster write on the person's machine, in VRAM, which was hashed,
and the AI applied it without drawing, `vdp_apply_rasters`. Since session
80 the draw layer holds the HUD strip's hold on the sprites itself, the
same on both: `src/core/video.h`.)

**Its delay was one frame** (sessions 58-63). The AI was made seeing its
pad take at once. At the usual delay (3 frames on a local line) it
overshot its landings: against a player who never moved it died 12 times
in 30,000 frames, every time out of fuel carrying a unit, and delivered
none. At one frame it won, as in the arena (no deaths, 5 delivered, 23
bought; the arena's: won at frame 26,323, 5 delivered, 22 bought). The
lockstep needs only the two delays' sum to cover the round trip, so the
person took the rest: START says the rival is the server's AI
(NET_START_RIVAL_AI) and the person's delay was then the whole round
trip in frames and 3, not half and 2.

**Session 64: the AI takes the delay.** the owner: "teach the AI to
handle delay". It thinks ahead now (docs/nightmare.md, "Through a
delay"): between frames, on a copy of the game run on to the frame its
pad will be read in. So it takes the round trip, and the person plays
with 2 frames (NET_AI_PERSON_DELAY). The sum is as before, the round trip
in frames and 4 (`NetClient.ai_sum`). Nothing new is sent: each side
reads the other's delay from its INPUTs (the newest pad in one sent from
the frame of its latest hash, less that frame: an INPUT lost reads more,
never less, so the least of the last two hashes' is taken), and
`net_game_push` sets more frames' pads than one (the pad held) when its
delay is to grow:

- the server's AI waits for the person's first INPUT before its first
  push, and takes what the person's delay leaves of the sum (at most 30
  frames, NET_AI_DELAY_MAX);
- the person starts at 2 and takes more only if the AI leaves too
  little: an AI from before session 64 keeps its 1, and the person then
  takes the rest, as before;
- a person from before session 64 takes the whole round trip and 3
  itself, and the AI keeps 1 -- but thinks ahead by it now;
- either side only ever grows its delay: a delay read too high (an INPUT
  lost) asks for less, and a side giving some back would leave the other
  to take it, the two settling the other way round. (Seen: with the
  headless games at full speed the server dropped datagrams past its 300
  a second, the person's hash frame's INPUT among them; the AI read the
  person's delay as 5, gave back, and the person took 3.)

Checked: every mix of old and new person and AI through a server of their
own with 30 ms added each way (`HZ_NET_LAG`), the hashes alike to frame
3000 in each: new against new, the AI 7 or 8 frames and the person 2; a
new person against the old AI, the person 2 then 8; an old person against
the new AI, the person 9 and the AI 1; old against old as ever. A game
window (`hz`, paced at 59.92 Hz) against the new AI over 40 ms with 5 ms
of jitter: the person 2 frames, the AI 10, one wait in 2,400 frames, the
40 hashes alike. And the round trip is now timed on the fine clock
(`net_now_us`): Windows's ms clock moves in 16 ms steps, and a round trip
of 30 ms read 47.

What it costs the server: thinking 8 ahead is about 80 us a frame on a
Ryzen 9 5950X (16 before): at 60 frames a second half a percent of a
core a game.

**The datagrams' flags,** each in a place older games leave 0 and ignore,
so they meet the new server as before: HELLO's byte 28 (NET_HELLO_AI), a
byte after JOIN's rtt (NET_JOIN_PEOPLE), a byte after START's rival_rtt
(NET_START_RIVAL_AI), and ROOMS' entry flag NET_ROOM_AI.

**What it costs** (on the server, an 8-core Haswell, one core): the game
with the AI driving one side runs at 130,000-160,000 frames a second
(the game alone 330,000; the original's CPU against itself 215,000), so a
game at 60 frames a second is about 0.05% of a core; a copy of the game
(70,696 bytes) a frame is 4 MB a second. 4.6 MB of memory a game. The
network as any player's. A hundred at once: about 5% of a core and 460 MB.

**Running it** beside the server, one game at a time each:

    make hz_netai
    ./hz_netai ASSETS [--server HOST[:PORT]] [--name NAME]
               [--replays DIR | --no-replays] [--quiet]

With systemd, `server/hz-netai@.service` (an instance each: `hz-netai@1`,
`hz-netai@2` ...). ASSETS must be the players' assets byte for byte (the
same commit's `assets/`, no mods; since session 78 a PNG only the same
picture), and the build the same NET_GAME_CODE, or the players see it as
OTHER GAME. It logs a line a game (who, which
side, the map, the delay, who won) and the AI's summary of it.

(Session 67) **It records every game** (docs/nightmare.md, "How people
beat it"): to `--replays DIR`, else `$HZ_SAVES/replays`, else
`saves/replays` where it runs; `--no-replays` none. The file is the
game's own kind (replay.h) with `kind netai` and `side` the AI's, named
`...-NETAI-P2-A.hzr`; `hz_replay ASSETS FILE --minutes` reads it, calling
the sides "the server's AI" and the person's name. The service keeps
them in `/var/lib/hz-netai/N` (its `StateDirectory`), an instance each.

## Each side its own screen (session 59)

The owner: "Need the option to be able to have side by side screen or
not", and later "Maybe that's a server by server mode depending on level
of competition?" On the split screen each player sees the other's side as
well, as on one console; for close play that gives the game away.

**The choice.** The NETWORK page's VIEW, SPLIT SCREEN (the default) or OWN
SIDE ONLY, is the host's, as the map is: it rides in the settings' byte 6
(`NetSettings.view`). A server may rule it for every game it starts:
`hz_server --view split` or `--view own` writes that byte into each game's
START, `--view host` (the default) leaves the host's. WELCOME's byte 20
tells the players the rule, and the page shows it under VIEW (SERVER: OWN
SIDE ONLY). Older servers send no rule and pass the byte on: the host's
choice. `NET_GAME_CODE` went to 2 for it: older builds would ignore the
byte. (Session 60: 3, for the TURRETS patch, docs/turrets.md. Session 61:
4, for the UNITS patch, docs/units.md. Session 71: 9, for PATHS,
docs/paths.md. Session 72: 10, for REPAIR, docs/repair.md. Session 75: 11,
for PIXELS, docs/pixels.md. The whole list
is at `NET_GAME_CODE` in `src/net/net_client.h`.)

**The game.** Both machines play it as a wide game (`v_split_screen` 0),
each showing its own side as modes 5 and 6 show the human's -- P1's
machine view config $0000/$FF01, P2's $FF00/$0001 -- but with both sides
human (`HzGame.own_view`, 1 or 2; `Game_SetupMode`). The split and the
wide game differ in the logic itself (the mech's screen box and lead, the
order menu's opening in one frame, the start cameras), so it is the wide
game on both, not a split game drawn wide.

Two machines showing different sides must still run the same game. What
is drawn is not read back by the logic but in one place: **the order
menu.** A shown menu takes its price, cost and the row it shows from
drawing them, and takes no input on a frame its row changed until it is
redrawn at the frame's end; a hidden one (the original's for a CPU in a
wide game) settles those at once (`OrderMenu_Settle`). So the same pads
bought a unit on different frames on the two machines (a harness of
two-player games from random pads, own view 1 against 2: the first
difference was a purchase, frame 4,215). In an own-view game the rival's
menu, hidden here, runs as the shown one does on the rival's machine --
drawn whole at step 8, redrawn as it changes, never settled -- unseen:
`order_menu_update` points the VDP at a scratch one while it steps, and
its scroll, sounds and gauge bars are left out (order_menu.c's `muted`).

What each machine draws and plays differs, so their hash leaves out the
display (`state_hash_game`): the planes' scroll and the effect queued,
each mech's view, its frame's upload, the HUD's shown values and the HQ
alert's count (the sprites count it down only for a shown view), what is
on screen (units' and shots' b6), the VDP; and from the game's end the
mechs and cameras, which the finale places by the view (`place_mechs`).
The winner and everything before stay in.

Checked: 64 two-player games from random pads, a menu opened every few
seconds (8 seeds a map, each forced to an end between frames 12,400 and
37,600), own view 1 against 2, the display-free state alike every frame
to the last of the finale, and the same length; `make netplay_check`'s
own-view and server-rule cases (below).

## The pace (session 63)

The owner chose it from what was open: "do the 59.92 Hz network
pacing". Until session 63 every frame waited for the display's refresh
(vsync), so the game ran at the display's rate -- 2.4 times as fast on a
144 Hz monitor, half as fast over a remote desktop's 32 Hz -- and a
network game at the slower machine's, the faster waiting a little every
few frames.

**Every game, the clock.** `src/core/pace.c`, after PS2's main loop, is
the platform's (`platform_present`): the intervals between presents
measure the display (the mean of the last 64 without the 8 longest and 8
shortest). One within 1% of the game's 59.92 Hz (53,693,175 / 896,040:
a 60 Hz or 59.94 Hz monitor) is LOCKED, a frame a refresh as on the
console (a 60 Hz one runs the game 0.13% fast, and smoothly); it is left
beyond 2%. Any other display runs FREE on the clock: a frame each 1/59.92
s, shown at each refresh until the next is due (2 or 3 at 144 Hz, 2 at
120) or not at all when the clock is a frame ahead (one in six at 50
Hz, never two in a row). Which refresh a frame goes up at is judged on
the refresh grid the measurements draw, not on each return's time: a
present back a fraction of a millisecond late moved it, and at 120 Hz
frames stood 1 and 3 refreshes in turn where 2 and 2 were due. A present
that doesn't wait (no vsync, a hidden window) is followed by a sleep to
the clock. A frame that took long is caught up by at most a quarter of a
second, the rest lost. `HZ_PACE=1` writes a line every 5 s of what the
pace did (locked or free, the display's rate, frames shown and shown
again, held back).

**The network game: neither side waits.** Both machines run their own
clocks at the game's rate (or their 60 Hz displays'). Two things the
lockstep adds:

- A wait for the rival's pad isn't owed afterwards (`platform_pace_held`):
  free, the clock moves on by as much, so no frames are run back to back
  to catch up.
- **The balance.** Two clocks never agree exactly (two PCs' differ by up
  to a hundredth of a percent or so: a frame every few minutes), and a
  side held up (until session 66, by its Esc screen) leaves the side that
  waited behind. The side running
  ahead uses up its margin -- the rival's pads in hand past the frame
  about to run -- until it waits for every pad and every bump in the
  line shows. So each side reads both margins (net_client.c): its own at
  each push, the rival's from its INPUTs (the newest pad in one, less the
  delay, is the frame the rival pushed it at; its ack, what it had of
  ours then), each smoothed over a second or so. Half the difference is
  how far ahead this side runs. Ahead by more than three quarters of a
  frame, it holds back a hundredth of a frame a frame for each frame
  ahead (at most a twentieth) until less than a quarter
  (`net_game_slow`, `platform_pace_slow`: free, the next frame later;
  locked, a refresh shown again once a refresh's worth has gathered).
  Nothing new is sent: the INPUTs carry it already, so older builds and
  the server need nothing, and a game with an older build is balanced
  from the new side. Not against the server's AI, which has no clock: it
  runs as the person's pads come.

The rival's frame is read with our delay, so both sides now come to the
same delay: from the round trip each says in its HOST or JOIN, which the
server passes to the rival in START (`NetClient.ask_rtt`), not the
latest (the two could differ by a frame or two; read with the wrong
delay, the balance stood a frame off even).

What a player sees: a 60 Hz monitor as before (locked); at 144 Hz the
game at its own speed (before 2.4 times), each frame shown for 2 or 3
refreshes; two machines on different displays both at the game's rate,
the one ahead holding a frame back now and then; over a long game the
two margins the same.

## The checks

- (Session 63) `make pacecheck` (tools/hz_pacecheck.c): pace.c against
  made-up displays -- 50, 57, 59.4, 59.94, 60 (a swapchain of 1, 2 and 3
  frames), 60.5, 61, 75, 100, 120, 144, 165 and 240 Hz, and no vsync --
  120 s each, the game's frame 0.2-2 ms, presents back up to 0.3 ms late:
  locked within 1% and free beyond; the game's rate (the display's
  locked, else 59.9227); each frame on screen for whole refreshes as
  even as the rates allow; a 150 ms frame caught up in full and a 1 s one
  by a quarter of a second; a 200 ms wait (pace_held) not caught up; 100
  ms held back (pace_slow) 100 ms behind, or 6 refreshes shown again.
- (Session 63) `make netpace` (tools/hznetpace_check.py): pairs of
  hz_netbot players, each on its own clock through pace.c (`--hz`), over
  a line made 20 ms long with 4 ms of jitter (`HZ_NET_LAG`,
  `HZ_NET_JITTER`, net_client.h), 2,400 frames: both at the game's rate
  (no waits after frame 600, even); P2's clock 0.2% fast (it held back
  4.5 frames, at most 0.8 ahead, no waits, margins 6.8 and 7.9 at the
  end); the same with the balance off (`HZ_NET_BALANCE=0`: 4.8 frames
  ahead by the end, its margin down to 2.2 and going); P2 stopped a
  second at frame 900 (P1 waited it out and held back 6.3 frames: even
  again, margins 7.0 and 7.0).
- `make netbot_check` (tools/hznetbot_check.py, tools/hz_netbot.c): many
  players in one process without the game, each pad a function of the
  game's seed, side and frame, so each side checks every pad it receives.
  Against a server of its own: 4 games; 30% of the datagrams lost; a split
  made on one side, caught on both; the same with 30% lost; 8 games picked
  off the list (HOST, then JOIN from LIST) with 30% lost; 200 games at
  once. All pass, none wrong (session 56: 240,000 pads in the 200 games;
  the server 9% of a core).
- `make netplay_check` (tools/hznetplay_check.py): two headless games
  (main.c's loop, `hz_headless`) through a server of their own, each pad
  a random player's; their hashes alike every second as far as both go.
  A clean line (map 3, 6,000 frames: 100 hashes alike), 20% lost (4,000
  frames: 67), and a joiner whose own PATCHES are all off (the host's
  played: 67 alike). (Session 59) own-view: the host's `--view own`, both
  playing their own screens (100 alike); server-rule: a second server with
  `--view own` and a host asking for the split, played on own screens (50
  alike). (Session 66) esc-open: the host's Esc screen open from network
  frame 900 for 600 (`HZ_NET_ESC`; no pad there to work the menu): it
  closes at frame 1,500, so the game went on under it, both sides in step
  (50 alike). (Session 66) rematch: P1's HQ at 0 HP at frame 1,200
  (`HZ_NET_END`), both asking for a rematch: two games in the one room,
  P2 the winner of both on both machines, 66 hashes alike through both
  and their own ends; draw: both HQs at once, DRAW on both (33 alike);
  with `--netai`, ai-rematch: a person against the server's AI on a
  server of its own, two games, the AI the winner of both, the person's
  66 hashes and the AI's alike.
- (Session 58) `make netplay_check` with the server's AI (its
  `--netai`, the server's `--ai-after 2`): a person alone gets it (the
  hashes alike: 50 in 3,000 frames; told it is the server's AI, delay 4
  frames on a local line, 2 since session 64, the AI taking the rest);
  two people a moment apart play each other;
  a person with QUICK MATCH people only waits 4.5 s alone and then plays
  the next person; `--net nightmare` plays the AI here, still waiting,
  until a person hosts and the network game starts.
- `make netcheck` (session 52): the game itself the same on every build.
  Run it again, and compare with another machine's, after any change to
  the game; bump `NET_GAME_CODE` when a frame comes out differently.

## Not done yet

- (Session 63: the frame pacing is done, "The pace" above.) What it can't
  do: two 60 Hz displays a hair apart (60.00 and 59.94) still meet at
  the slower, the faster holding a frame back every 16 s or so -- now a
  refresh shown twice by the balance, not a wait.
- (Session 66: the Esc screen no longer pauses both games, "Playing (the
  game's side)" above; Start is still the game's own pause, both sides'.)
- No chat or spectators (session 66: a rematch); no accounts (a name is whatever the
  player types); a server's list holds the first 16 waiting (LIST has an
  offset for more; the page shows 5).
- CONNECT looks a server's name up while the frame waits (an address
  needs no lookup).
- The server reads a datagram a call; Linux's `recvmmsg`/`sendmmsg` would
  take batches, were it ever needed.
- (Session 58) The server's AI: a game a process (4.6 MB each). (Session
  64: the person's input delay against it is 2 frames; the AI takes the
  rest of the round trip and thinks ahead by it.)
