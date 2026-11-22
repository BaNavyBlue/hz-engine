HERZOG ZWEI for Windows
=======================

Technosoft's Herzog Zwei (1989, Sega Mega Drive), ported to Windows: the
cartridge's 68000 and Z80 code, rewritten in C from a documented
disassembly, plays the same game in a window. The pictures, maps, data and
music in assets/ were taken from the original cartridge.

Starting it
-----------
Unpack the HerzogZwei folder somewhere you can write to (your Documents
or the desktop, not Program Files: the game keeps its saves and settings
in the folder beside it), then start HerzogZwei.exe. Keep the folders
assets/ and shaders/ next to it. Windows 10 or 11, 64-bit.

If Windows stops it
-------------------
HerzogZwei.exe isn't signed (a signature costs money every year and
names a person), so Windows doesn't know it and may stop it the first
time:

  "Windows protected your PC": More info, then Run anyway. To be asked
  nothing, before unpacking: right-click the zip, Properties, tick
  Unblock at the bottom of General, OK; then unpack it.

  Windows 11's Smart App Control blocks it with no Run anyway, even
  unblocked: Windows Security, App & browser control, Smart App Control
  settings, Off. Windows 11 updated since April 2026 can turn it on
  again later; on older ones, turning it off is for good.

  An antivirus that calls it a threat is guessing about a program it
  hasn't seen before. Microsoft Defender's guess can be sent to Microsoft
  as a mistake, with the file: microsoft.com/en-us/wdsi/filesubmission.

Properties, Details shows what HerzogZwei.exe is: its version is the
date and time (UTC) of the commit it was built from, and the commit
(licenses/SOURCE.txt names it too).

Controls
--------
As they come (the Esc screen changes them, below):

Keyboard:  arrows or W/A/S/D           the pad
           Z X C   (or L ; ')          A B C
           Enter                       Start
           Escape                      the Esc screen
           F11 or Alt+Enter            fullscreen
           F10                         the picture's shape
Gamepad:   d-pad or left stick         the pad
           the left, bottom and right
           face buttons (X, A, B on
           an Xbox pad)                A B C
           Start                       Start
           Back                        the Esc screen

The Esc screen has the settings: the display, fullscreen, the CRT
picture, the volumes, the controls (keys and buttons can be changed
there, with a TURBO key), the gamepads, the game's fixes, NETWORK (two
players on two computers, below), CPU and QUIT.

CPU picks who plays the computer's side, from the next game on:
ORIGINAL, the cartridge's own, or NIGHTMARE, the port's: a much harder
opponent that flies round missiles, aims its shots, defends its HQ on
foot, shoots down a jet that carries a unit, and clears the guards round
your HQ before it sends its tanks in. It plays by the same rules (no
money it hasn't earned, and its gun waits between shots as yours does);
the TYPE you choose still sets the damage.

Playing over the network
------------------------
Two players on two computers, each running the game, through a server
that pairs them up. On the Esc screen, NETWORK:

  SERVER       left and right pick one of the servers the game knows
  ADDRESS      the server's address; choose it to type another (the
               semicolon key types the colon: 203.0.113.7:19890), Enter
               (the port can be left off when it is 19890)
  NAME         the name the other players see: choose it, type, Enter
  CONNECT      online; under it, how many are online and waiting
  QUICK MATCH  plays whoever has waited longest, or waits for someone;
               left and right: PEOPLE OR AI (if no one comes, the
               server's nightmare AI, when the server runs one) or
               PEOPLE ONLY
  HOST GAME    waits for someone to pick you, on the MAP below
  MAP          the map a game you host is played on
  VIEW         SPLIT SCREEN: both sides on each screen, as on one
               console; OWN SIDE ONLY: each player sees only their own
               side, full width. The host's is played, unless the
               server has a rule (shown under it: SERVER: ...)
  NIGHTMARE    PLAY IT HERE: a game against the nightmare AI on this
               computer while you stay in the queue; a person who comes
               ends it, and your network game starts. With VIEW SPLIT
               SCREEN the AI's side shows beside yours, to watch it
  then the players waiting: choose one to play them (NIGHTMARE AI,
  marked SERVER AI, is the server's own; against it you are P1 on your
  own map)

When a rival is found the game starts at once, whatever you were doing:
on the split screen as two players on one console, you on your side
(the host is P1, the red), or with OWN SIDE ONLY your side alone, full
width. Every controller on your computer drives
your side. The Esc screen doesn't pause a network game: the game goes on
under it while your buttons work the menu (your mech gets none), and
the other player keeps playing. Start pauses the game for both of you,
as on the console. The patches (fixes) played are the host's.

When the game ends each of you sees your own result, YOU WIN or YOU
LOSE (both HQs down in the same second is a DRAW), with its own music,
then the results. Then the NETWORK page comes back with the cursor on
REMATCH: press A to ask for another game against the same player (A
again takes it back). The row says when the other player has asked too,
and when both have, the next game starts, same sides and settings. The
server's AI always wants a rematch. Pick anything else on the page, or
close it, and the other player is told you left; closed, you are back at
the title, still online.

The servers the game knows are in servers.txt beside it (a line each:
NAME ADDRESS PORT), and your own (every address you type is kept) in
saves/settings.json, under "net" and "servers". Anyone can run a
server: hz_server, built from the source (server/), needs one UDP port
open (19890 unless told another).

Recorded games
--------------
Every game you play, against the computer or over the network, is
recorded when it ends, in saves/replays/ (a small text file a game,
named for when it was played (UTC), the map, who you played and the
TYPE). A recording holds only the game: the settings it started with
and the buttons pressed each frame. hz_replay, built from the source,
plays one again and says what each side did. Set HZ_REPLAYS=0 to record
none.

Changing the controls
---------------------
Every action (the four directions, A, B, C, Start, and TURBO, which
fires B as fast as the gun can while it is held) has two keys and two
gamepad inputs. CONTROLS on the Esc screen sets the keys; GAMEPAD, then
BUTTONS, sets the gamepad inputs. Choose a slot, then press what it
should be: a key on CONTROLS; on BUTTONS a button, a d-pad direction, a
stick pushed or a trigger pulled, on any gamepad, or a button of a
joystick or arcade stick SDL has no layout for (JOY 1, JOY 2 ...).
Escape keeps the slot as it was, Backspace empties it, and DEFAULTS
puts that page's controls back as they came. Whatever is set, an input
does one thing (set on one action, it leaves any other), Back (SELECT,
SHARE, VIEW ...) always opens the Esc screen and goes back in it, and
every gamepad's d-pad and left stick always move its cursor.

BUTTONS names each input as the first gamepad connected labels it: the
bottom face button is CROSS on a PlayStation pad, A on an Xbox pad and
B on a Switch pad. settings.json keeps the inputs by where they are on
the pad (SOUTH, EAST, WEST, NORTH, DUP, LSUP, L2, JOY1 ...), so the
same settings work on any gamepad.

A gamepad SDL doesn't know shows on the GAMEPAD page, but as a joystick:
its buttons are JOY 1, JOY 2 ..., its hat the d-pad and its first stick
the left stick. To give it a gamepad's layout, put a
gamecontrollerdb.txt (SDL's mapping format, as in the community's
SDL_GameControllerDB) next to HerzogZwei.exe. Started from a terminal,
the game says how many mappings it read.

Rumble
------
A gamepad with motors (a DualShock 3 or 4, a DualSense, an Xbox pad)
shakes when your mech is hit, when your robot stands on ground that
burns, when your mech is destroyed, when the two jets collide, when your
HQ comes under fire (and on each beat of its alarm), when a missile homes
in on you, and when an HQ is destroyed. The Esc screen's GAMEPAD page
names the gamepads connected (choose one to make it buzz) and sets
RUMBLE: OFF, LOW, MEDIUM (the first setting) or HIGH.

A DualShock 3 on Windows needs a driver that gives it to Windows as a
gamepad: DsHidMini, free from Nefarius (github.com/nefarius/DsHidMini;
its control app wants Microsoft's .NET 10 Desktop Runtime and ASP.NET
Core Runtime 10). In the control app: the pad, Configure, Settings mode
Custom, then SXS, and Apply changes. The game then names it PS3
CONTROLLER, with the PlayStation buttons and the rumble. DsHidMini's
first setting, XInput, works too, as an Xbox pad with the Xbox names;
its PS button is then the Xbox Guide button, which Steam, if it runs,
takes for itself unless Steam's Settings, Controller, "Guide Button
Focuses Steam" is off.

Saves and settings
------------------
saves/ in the game's folder: settings.json (the Esc screen's settings)
and progress.json (the saved games, the maps won: CONT on the options
screen lists them, in place of the cartridge's password).
In settings.json, "keys" and "pad" give each action its two slots by
name, "-" for none: "b": ["X", ";"] and "b": ["SOUTH", "-"] as they
come. A name the game doesn't know keeps that slot as it came.

Mods
----
A file under mods/ at the same place as one under assets/ is read in its
place: copy a file from assets/ into mods/ and change the copy (songs,
pictures, maps, data). mods/README.txt, written at the first start, says
more.

From a terminal
---------------
HerzogZwei.exe --help lists the options (start on a map, a mode or a
difficulty, the window's size, fullscreen). Started from a terminal the
game writes what it found (the display, the gamepads, the mods) there.

Licences
--------
licenses/ has the licences of the code built into HerzogZwei.exe: SDL 3
(zlib), Nuked OPN2, the Mega Drive's FM sound chip (LGPL 2.1: its
notice in Nuked-OPN2.txt, the licence's text in LGPL-2.1.txt), and the
others the build used. source.zip is the game's source, Nuked OPN2's
with it, at the commit named in licenses/SOURCE.txt.
