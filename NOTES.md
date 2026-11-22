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
* Licence: MIT for the engine's own work (`LICENSE`, "Copyright (c) 2026
  BaNavyBlue"), including the disassembly's names in `hz/hz.sym`, which are
  the owner's own work. The third parties keep theirs (`NOTICE.md`).
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

## Not yet run or seen

* The GitHub Actions runs (no `gh` on this machine). The Linux and macOS
  jobs are untested here.
* The extractor and the first-start window on Linux and macOS.
* The first-start window's drop and file dialog in the packaged exe: the
  image was given on the command line (the private tree's build showed the
  window and its refusal).

## Next

1. The owner creates github.com/BaNavyBlue/hz-engine (private) and says to
   push; read the Actions runs; public when the owner says.

## Open

* The engine's text is the private tree's text: anyone with both could
  tell they are the same work. The private repos' links are secret, which is
  what keeps them apart.
