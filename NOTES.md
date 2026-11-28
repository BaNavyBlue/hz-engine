# hz-engine: working notes

The public version of the herzog-zwei port: the same engine, with no assets
and no ROM data in the tree. A player supplies a cartridge image they own.
The game asks for it on its first start and makes its files from it into
`hz/assets/` itself, in C (`hz/src/extract/`), through the ROM's map
(`hz/hz.sym`, as `hz/src/extract/rom_map.h`). No disassembly is needed or
shipped, and a player needs no Python. README.md is the front page; this
file holds the decisions, what is proved and what is next.

## Decisions (the owner)

* (2026-11-22) Made after ps2-engine, with the same skill: a separate
  repository, so the private tree keeps its assets and history.
* It is the owner's own: github.com/BaNavyBlue/hz-engine, committed as
  BaNavyBlue with the same address as ps2-engine (this clone's git config).
  The private port is written under a pseudonym, and nothing of that alias
  goes into this repository: not in the tools, the docs, the commit
  messages or the history. `tools/sync_private.py` reads the name from the
  private clone's git config, writes it as "the owner", and refuses to
  finish if the name, an address, a machine's IP or a home folder is left.
  The other way holds too: the private tree never names this repository.
* The working copy sits outside the folder that holds the pseudonymous
  repos, so their rules and hooks never apply to it.
* The cartridge: *Herzog Zwei* for the USA, the one image the disassembly
  is of (524,288 bytes, SHA-1 `8f7262102c2b2334f0bc88ee6fd6b08797919176`,
  header `GM MK-1400 -00`). No other is known to the owner.
* No Python extractors: "I don't want python extractors. Minimal library
  acquisition and OS dependent libraries please. ... That means mostly C."
  The private tree's session 78 wrote the extraction in C, linked into the
  game (`hz/src/extract/`, a port of `hz/tools/hz_extract.py`, which stays
  as its reference). It needs no library but the game's own code: the PNG
  reader's inflate (zipped images too), the PNG writer, the LZW and RLE
  unpackers, the song-text builder. The engine's own tools (the sync, the
  release packer, the leak check, the list of proved files) are Python with
  only its own library, for whoever builds the engine, never a player.
* The network game's id hashes a PNG by its picture, not its bytes
  (`NET_GAME_CODE` 12; the owner's choice, "Hash PNGs by picture"), so
  files made by the game's C and the private tree's Pillow play together.
* (2026-11-27) The engine follows the private tree: each of its pushes is
  synced here, proved and released, so the engine's `NET_GAME_CODE` is
  always its builds', and the two play together (and against the same
  AI). The owner: "just keep the engine in sync with stand alone please".
  It had fallen a code behind (12 against 13), and the private builds' AI
  showed as OTHER GAME.
* Licence: MIT for the engine's own work (`LICENSE`, "Copyright (c) 2026
  BaNavyBlue"), including the disassembly's names in `hz/hz.sym`, which are
  the owner's own work. The third parties keep theirs (`NOTICE.md`).
* (2026-11-24) Releases: Windows and Linux; macOS left out for now ("please
  make all releases for repository except mac version for now"). The macOS
  job still builds on pushes to main, not for a tag. The first release is
  `v0.1.0`, as ps2-engine's.
* Working builds come from the repository: GitHub Actions builds Windows,
  Linux and macOS on every push (artifacts), and a `v*` tag publishes them
  as a release. The Windows exe is `make build/dist/HerzogZwei.exe` (no
  console window, stripped, its version from the commit); never `make dist`,
  which packs a tree's assets.

## How the tree is made

```sh
python3 tools/sync_private.py ~/Desktop/GIT/Claude/herzog-zwei    # hz/, scrubbed
(cd hz && make hz_assets && ./hz_assets IMAGE)                    # hz/assets/
python3 tools/assets_list.py --check                              # as proved
(cd hz && make -j4 hz hz_headless)                                # the game
python3 tools/rom_leak_check.py --rom IMAGE --assets PRIVATE/assets \
    --known-text PRIVATE/assets/ending FILES_TO_COMMIT
```

`hz/` is emptied and rewritten by each sync (`hz/.synced-from-private`
marks it), so the engine's own files live outside it, or in `overlay/hz/`
(laid over each sync: the icon and `hz.rc`). The made assets and the builds
go with each sync too: make them again after one. Left behind: the
disassembly (`asm/`, the ROM's bytes written out), `assets/` but for the
port's own five files, every picture and sound (the icon too: the private
tree's is drawn from the game), the private working notes (`HANDOVER.md`,
`unitai_notes.md`, `mine.txt`), the disassembly's naming notes
(`tools/names/`) and its call tree (`tools/hz_tree.html`).

The ROM map is the private tree's `hz.sym`, which `make rom` writes from
the disassembly; `hz/tools/hz_rommap.py` writes `rom_map.h` from it. The
next sync brings both.

Q: the CI pins v1.5.4. A Q checkout of another version may lack
`q/fx/svf.hpp`; pass `Q_DIR=` to a v1.5.4 one.

On the Windows desktop: run MSYS2's Python by its full path from Git Bash.
The sync reads the private clone's `user.name` with the git on the PATH,
and MSYS2's own git does not follow that clone's identity include (Git for
Windows's does). Build in MSYS2's own UCRT64 shell: make run from Git Bash
loses TMP and gcc fails ("Cannot create temporary file in C:\WINDOWS\").

## What is proved

(2026-11-22, on the Windows desktop, from the private tree's commit "The
game makes its own files from the player's cartridge, in C, on its first
start".)

* **The files.** `hz_assets IMAGE` made 144 files in `hz/assets/`; with the
  port's own five, 149 of 149 are the private tree's (`hz/tools/
  hzassets_check.py`: text and JSON byte for byte, PNGs the same picture)
  and give the same network id, `D5A1A52404EC51C1` (`hz/hz_gameid`).
  `tools/assets.sha256` was written from them; the private tree's own
  files, made by Pillow, pass it too (149 of 149), so the PNG digest goes
  by the picture. The private tree's `make assets_check` proves the same
  thing on its side.
* **The sound** is checked as it is made, by the game's own song-text
  builder: the text builds back to the cartridge's 32 KB, and every tracker
  plays as its text (all 144 channels, three turns).
* **The image.** Raw, `.smd`, with a copier's header, zipped (stored and
  deflated): all read. A copy with one byte changed and a short file are
  refused, saying what they are and which image is wanted (no other game's
  image was tried).
* **The build.** `hz`, `hz_headless`, `hz_assets` and `hz_gameid` build in
  `hz/` with no warnings (MSYS2 UCRT64). The CI's Windows exe, `make
  build/dist/HerzogZwei.exe Q=1` against Q v1.5.4, built with no warnings,
  6.3 MB, and needs only Windows's own DLLs (`objdump -p`).
* **The package.** `tools/make_release.py --platform windows`, unzipped
  into an empty folder: 305 files, the only ones under `hz/assets/` the
  port's five. Its `HerzogZwei.exe`, started from another folder with a
  zipped image on its command line, made the 149 files in its own folder
  (none where it was started; no `.part` left), passed
  `tools/assets.sha256`, gave the same network id, and went on to the
  title screen. Started again, it went straight to the title.
* **Linux** (2026-11-24, Ubuntu 24.04, gcc 13.3, no SDL3 there): the
  first push did not build on Linux (`png.h` used `size_t` without
  `<stddef.h>`, which MinGW's headers happen to give); fixed in the next
  commit. Then `hz_headless`, `hz_assets` and `hz_gameid` built with no
  warnings, and `make sdl-syntax` passed, which compiles the first-start
  window's Linux branches against SDL's declarations. From the raw image
  and from the zip, the 144 made files were byte for byte the Windows
  build's, and with the port's five the network id was the same. The
  headless game with no files says how to make them.
* **The first release, `v0.1.0`** (2026-11-24, the tag on 78c4b85): GitHub
  Actions built the Windows and Linux packages and published them, the
  owner saw both files on the release. The Actions runs are read by the
  owner (no `gh` on this machine). Before it, up to 2b2f725, the workflow
  file did not parse (a step's name held `: `, copied from the skill's
  example), so Actions ran nothing; fixed with the release commit, along
  with `make_release.py` naming a dotted version's zip wrong (`v0.1.0` as
  `-v0.1.zip`). The owner's own Linux build of 2b2f725 passed too.
* **The leak check** (`tools/rom_leak_check.py` over a copy of the 293
  files to commit, with the private tree's assets, and the ending's and
  menus' texts as known text): 7 files. The port's own five under
  `hz/assets/` (equal to the private tree's, as they should be);
  `hz/src/audio/hz_snd.c`, the Z80 driver's pitch tables (FMFreqTable,
  FMFineTable, PSGPeriodTable, PSGFineTable: about 800 bytes);
  `hz/src/game/tables.c`, the game's small tables (VDP registers, sines,
  directions, the mech's frames, the CPU's launch tables and buy scripts).
  Kept, as ps2-engine keeps its tables. No text quoted. The control, the
  private disassembly's `asm/`: 37 of 37 files flagged.
  `hz/src/game/nm_atlas.c` is not flagged and is not the game's data: the
  nightmare AI's own measurements (its unit AI driven from each block of
  each map, timed to the enemy HQ).
* **Identity.** The sync's checks (no author name or address, no machine
  address, no home folder) and the owner's list of identifying words: only
  this repository's own name is found, in `LICENSE`, `NOTES.md` and
  `hz.rc`.

### The sync of 2026-11-27 (v0.1.1)

From the private tree's commit "Step 1 of the hackable plan: the game draws
through its own draw layer (src/core/video.h), not a VDP model", and the
plan before it (`hz/docs/hackable.md`). The game now draws through a layer
of its own, in place of a VDP model. `NET_GAME_CODE` is 13: no frame changed,
but the display that a split-screen game hashes did.

* **The sync:** 283 files, the author's name put as "the owner" 207 times,
  and nothing left for its checks to refuse.
* **The files:** `hz_assets IMAGE` made 144. With the port's five, 149 of
  149 are as proved (`tools/assets.sha256`, unchanged). The network id is
  `C5F18E693167BD3A`, the same as the private commit's own assets give
  under the same build.
* **The build:** `hz`, `hz_headless`, `hz_assets` and `hz_gameid` build
  with no warnings (MSYS2 UCRT64, Q v1.5.4). The private commit built on
  Linux (gcc, glibc) with no warnings.
* **The leak check:** 299 files, 8 flagged. Seven are v0.1.0's seven. The
  new one is `hz/tools/video_vdp.c`, a check tool's model of the VDP, with
  the VDP's start-up registers for the game and the menus (38 bytes). They
  are the same values `hz/src/game/tables.c` already carries. Kept, as
  those are.
* **Identity:** as before. Only this repository's own name is found.

### The sync of 2026-11-27 (v0.1.2)

From the private tree's commit "Step 2 of the hackable plan: the frame
model; step 1's rest (every sprite drawn, NET_GAME_CODE 14); the nightmare
AI keeps off a ring of launchers". The game's frame is its own (the title
loop's flow, and the VBlank and HBlank handlers as what they come to in a
frame), the layer draws every sprite and the split's divider, and the
nightmare AI no longer lands to fight under a ring of SAM-42s and GRM-34As
(`hz/docs/nightmare.md`, "The ring round its outpost"). `NET_GAME_CODE` is
14: the display a split-screen network game hashes changed again.

* **The sync:** 284 files, the author's name put as "the owner" 214
  times, and nothing left for its checks to refuse.
* **The files:** `hz_assets IMAGE` made 144. With the port's five, 149 of
  149 are as proved (`tools/assets.sha256`, unchanged). The network id is
  `923A9F53585E4FF3`, the same as the private commit's own assets and the
  server's installed ones give.
* **The build:** `hz`, `hz_headless`, `hz_assets` and `hz_gameid` build
  with no warnings (MSYS2 UCRT64, Q v1.5.4). The private commit built on
  Linux (gcc, glibc) with no warnings.
* **The leak check:** 300 files, 8 flagged, the same eight as v0.1.1's.
  Kept.
* **Identity:** as before. Only this repository's own name is found.

## Not yet run or seen

* The windowed game on Linux (a display, the drop and the file dialog)
  and anything on macOS.
* The first-start window's drop and file dialog in the packaged exe: the
  image was given on the command line (the private tree's build showed the
  window and its refusal).

## Next

1. ~~Public when the owner says~~: public (the owner, 2026-11-25).
2. macOS: its job builds on pushes to main; into the release once it has
   been seen to pass and the owner says (the release job's `needs`, its
   downloads and its notes).
3. Each push of the private tree: sync from it, prove again (this file's
   list), then the next tag. `v0.1.1` was the first (2026-11-27), `v0.1.2`
   the next (the same day).

## Open

* The engine's text is the private tree's text: anyone with both could
  tell they are the same work. The private repos' links are secret, which is
  what keeps them apart.
