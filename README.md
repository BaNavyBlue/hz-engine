# Herzog Zwei: a ROM-free engine

A port of *Herzog Zwei* (Technosoft, Mega Drive, 1989) to modern computers,
in portable C with SDL3. It plays the original game's logic, units, maps
and music (on an emulated YM2612, or re-voiced with Cycfi's Q synth). It
adds a CRT look, rebindable controls, gamepads, wide and split screens,
network games, and a much harder computer opponent.

**This repository contains none of the game.** There is no ROM and no art,
music, maps or text taken from one. The game makes its files once, on your
own machine, from your own cartridge image. It asks for the image the first
time it starts and extracts the files itself, in C. There's no Python and
nothing else to install. Nothing is downloaded or sent anywhere.

## Playing

1. Download the package for your system from
   [Releases](../../releases) and unpack it: Windows or Linux (64-bit x86;
   SDL3 is built in). There is no macOS release yet; on a Mac, build from
   source (below).
2. You need your own image of *Herzog Zwei* for the USA: 524,288 bytes,
   SHA-1 `8f7262102c2b2334f0bc88ee6fd6b08797919176` (commonly named
   "Herzog Zwei (U) [!]"). A `.bin`, `.md`, `.gen` or `.smd` file works,
   and so does a `.zip` holding one.
3. Run `hz\HerzogZwei.exe` (Windows) or `./hz/hz`. The first time, it asks
   for the image: drop the file onto its window, or press Enter and choose
   it. It checks the image, makes the game's files (a second or two) and
   the game starts. Given on the command line (`./hz/hz image.bin`, or
   the image dropped onto the program), the image is used at once.

The packages are not signed. On Windows choose "More info", then "Run
anyway". On Linux, choosing the image with Enter needs
`xdg-desktop-portal` or `zenity`; dropping it onto the window, or naming it
on the command line, needs neither.

## Building from source

You need:

- a C11 compiler and make;
- SDL 3.4 or later, with pkg-config;
- optionally, Cycfi's [Q](https://github.com/cycfi/Q) v1.5 and a C++20
  compiler, for the re-voiced synth (the AUDIO page's SYNTH: Q).

```sh
cd hz
make                         # the game: ./hz (it asks for the image)
make hz_assets               # or the files from a terminal:
./hz_assets /path/to/image.bin
```

On Windows, use MSYS2's UCRT64 shell (`hz/docs/windows_build.md`). Build
with `Q_DIR=/path/to/Q` when Q is not in `~/GIT/Q`. `hz/docs/` describes the
port itself; `hz/docs/first_start.md` describes the first start and the
extractor.

## How it works

- `hz/hz.sym` says where everything is in the cartridge, by the names the
  port's disassembly gives it: every label with its address. It holds
  names and numbers only, no data and no code.
  `hz/src/extract/rom_map.h` is the part the extractor uses, written from
  it by `hz/tools/hz_rommap.py`.
- `hz/src/extract/` is the extractor, in C, linked into the game: it reads
  your image (raw, `.smd`, with a copier's header, or zipped), checks its
  SHA-1 and writes `hz/assets/`. It uses only the game's own code (the PNG
  reader's inflate, a PNG writer, the game's LZW and RLE unpackers).
- `hz/src/platform/first_start_sdl3.c` is the window that asks for the
  image when the game starts without its files, and shows the steps.
- `tools/assets.sha256` lists the files the engine was proved with, by
  digest (a PNG's by its picture). `tools/assets_list.py --check` compares
  a made `hz/assets/` with it.

Every push builds the game for Windows, Linux and macOS in GitHub Actions
(`.github/workflows/build.yml`). The packages are kept as each run's
artifacts. Pushing a tag `v*` publishes the Windows and Linux ones as a
release (macOS is not released yet). The Linux one is built on Ubuntu
24.04, so it needs a glibc as new as that one's (2.39).

## Layout

| Path | What |
|---|---|
| `hz/` | the port: source (`src/`, with the extractor in `src/extract/`), tools and checks (`tools/`), compiled shaders, docs, the network server (`server/`) |
| `tools/` | the release packager, the list of proved files, the leak check, the sync from the private tree |
| `overlay/hz/` | the engine's own versions of files laid over the port: the icon (the private tree draws its own from the game) and the Windows resource file |

## Licence

The engine's own work is MIT-licensed (`LICENSE`), including the names in
the ROM map, which are the project's own. Nuked-OPN2, Q and SDL3 keep their
own terms; `NOTICE.md` lists them. *Herzog Zwei* is Technosoft's. This
project is not affiliated with SEGA or Technosoft.

## Credits

- The YM2612: Nuked-OPN2, by Alexey Khokholov (Nuke.YKT).
- The re-voiced synth: Cycfi's Q, by Joel de Guzman.
- SDL3: Sam Lantinga and contributors.
