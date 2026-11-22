# Building on macOS (after session 48)

How to build and run the port on a Mac. It builds as on Linux: C11 for the
game, SDL3 for the window, sound and input, C++20 only for the Q synth.
SDL3's GPU API draws through Metal there, from the checked-in
`shaders/*.msl`. Windows has docs/windows_build.md. The PS2 port's
docs/macos_build.md is the longer page (Homebrew on Apple Silicon, Intel
Macs without a package manager, HDR on a Mac); what it says holds here too.

## Tried on a Mac (after session 48)

* An Intel MacBook Pro (Core i7-6820HQ, 8 threads), macOS 15.7.7, Apple
  clang 17.0.0 (Xcode's command line tools), MacPorts: SDL3 3.4.16, gmake
  4.4.1, pkgconfig 0.29.2; Apple's Python 3.9.6 (no Pillow or numpy); Q
  v1.5.4 (efe2611) beside the folder that holds the repositories.
* `gmake -j8` builds every program and `hz` with Q, linked against libc++
  (the Makefile's choice on macOS) and MacPorts' SDL3, with no warnings.
  Before, clang gave about 500 (`-Wsometimes-uninitialized`, gcc does
  not): the start units' and the demos' records in src/game/assets.c
  stored fields a failed read had left unset. They start at 0 now; no
  output changed (below).
* `./hz` opens the window: `display: video cocoa, SDL_GPU metal, MSL
  shaders, 60.00 Hz, vsync on`, HDR on (headroom 2.000) on the laptop's
  panel. How it looks, sounds and paces is still for someone at the Mac.
* `make rom` (hz.md from the disassembly, no ROM needed), `make roundtrip`,
  `make shots` and `make units`: every file the same before and after the
  assets.c change (113 files, the ROM image's checksum among them).
  `make crt_check` 27 of 27, `make crt_sim_check` 38 of 38,
  `./crt_gpu_check` 84 of 84 on Metal, `make menu` all agree.
* Not run there: `make pans` (numpy), `make assets` (Pillow), the long
  checks against the original (`demo`, `front`, `gameend`).

## Fullscreen (session 68 (2))

FULLSCREEN switched on with the game running did nothing on a Mac.
SDL3 on macOS makes fullscreen a Space of its own by default, and once
the GPU chain has drawn into the window that switch is taken and never
made: SDL_SetWindowFullscreen and SDL_SyncWindow both say yes, the window
stays as it was and SDL_WINDOW_FULLSCREEN stays off, with nothing in the
log. Fullscreen from settings.json at the start got there, being before
the first frame. A test of SDL alone (SDL 3.4.18 on an Intel MacBook Pro,
macOS 15) showed it in three runs of three: a window a GPU device has
drawn into, never; the same window with no GPU device, always; the GPU
window with SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES set to "0", always. So
platform_sdl3.c sets that hint on macOS before SDL_Init, and fullscreen
there is the borderless window over the display, as on Windows and
Linux, with no Space of its own and no slide.

F11 on a Mac's keyboard is a media key (Show Desktop or a volume key)
unless Fn is held, or "Use F1, F2, etc. keys as standard function keys" is
on in the Keyboard settings; Option+Return is Alt+Return, and the Escape
menu's FULLSCREEN always works.

## The steps

1. Xcode's command line tools (clang, clang++, git, make):

   ```sh
   xcode-select --install
   ```

2. SDL3, pkg-config and GNU make 4 -- MacPorts (macports.org; the way it
   was first built, and the one for Intel Macs, which Homebrew no longer
   builds packages for):

   ```sh
   sudo port install SDL3 pkgconfig gmake
   sudo port install python313 py313-Pillow py313-numpy
   sudo port select --set python3 python313
   ```

   or Homebrew (brew.sh) on Apple Silicon:

   ```sh
   brew install sdl3 pkgconf make python pillow numpy
   ```

   Open a new Terminal, then check SDL3 is found: `pkg-config --modversion
   sdl3`. Python is for the checks' scripts (Pillow for `make assets`,
   numpy for `make pans`); the game does not need it.

3. (Optional) Q, for the Q synth:

   ```sh
   git clone --recurse-submodules https://github.com/cycfi/Q.git ~/GIT/Q
   ```

   The Makefile also finds Q beside the folder that holds the
   repositories: a clone at `<GIT>/Claude/herzog-zwei` finds `<GIT>/Q`, so
   `~/Documents/GIT/Q` works with the repositories in
   `~/Documents/GIT/Claude`. Anywhere else: `Q_DIR=/path/to/Q`.

4. Build and run, from the tree's top:

   ```sh
   gmake -j"$(sysctl -n hw.ncpu)"
   ./hz
   ```

   It reads `assets/` and `shaders/` from the working folder (`--assets
   DIR` for others) and keeps `saves/` there (`HZ_SAVES` for another).
   macOS's own `make` is GNU make 3.81 (2006); `gmake` is the one the build
   was tried with.

## If it does not build

- **"SDL3 not found by pkg-config."** Install SDL3 and pkg-config (step
  2), open a new Terminal, and check `pkg-config --libs sdl3` prints
  something.
- **Link errors naming `std::` or `-lstdc++`.** `gmake clean`, then `gmake
  Q_LDLIBS=-lc++` (or `Q=0` for hz without Q), and report it.
- **`*.dSYM` folders** beside the programs are clang's debug symbols
  (`-g`): harmless, ignored by git, removed by `gmake clean`.
