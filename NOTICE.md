# Notices

## The game

*Herzog Zwei* (1989) is Technosoft's. This repository contains none of the
game's data: no ROM, and no art, music, maps or text taken from one. To
play, you need a cartridge image of your own (the README says which one).
The game makes its files from that image on your machine, the first time it
starts. They are never part of this repository or its releases.

This project is not affiliated with, endorsed by or connected to Technosoft,
SEGA or anyone holding the game's rights.

## What the MIT licence covers

`LICENSE` (MIT) covers this project's own work:

- the engine's source, its tools, its tests and its documentation;
- the names the port gives the game's code and data, in `hz/hz.sym` (the
  ROM's map) and throughout the code. They come from the project's own
  disassembly of the cartridge, which is not published.

It does not cover the parts below, which keep their own terms.

## Other people's work

| Part | Where | Author | Terms |
|---|---|---|---|
| Nuked-OPN2 (the YM2612 chip) | `hz/src/audio/ym3438.c`, `hz/src/audio/ym3438.h` | Alexey Khokholov (Nuke.YKT) | LGPL-2.1; `hz/src/audio/LICENSE.ym3438` |
| Cycfi Q (the re-voiced synth) | fetched at build time, not in this repository | Joel de Guzman | MIT |
| SDL3 | linked, not in this repository | Sam Lantinga and contributors | zlib |

Released builds carry these notices in their `licenses/` folder, with those
of what the build links in (on Windows also the GCC runtime, winpthreads and
libiconv). Because the game links Nuked-OPN2 (LGPL-2.1) statically, each
release package is also the complete source it was built from, so it can be
rebuilt with a changed copy of that library.
