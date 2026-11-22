# Building on Windows (session 47)

How to set up a Windows machine to build, run and check the port, from
nothing. Session 47 did it this way on Windows 10 Enterprise LTSC 2021
(19044), a Ryzen 9 5950X (32 threads) and a GeForce RTX 3080 Ti (driver
32.0.15.9174). It follows PS2's `docs/windows_build.md` (its builds
113-128): the same MSYS2 UCRT64 toolchain, SDL3 and shader tools.

## What the tree has, and what it does not

**In the tree:** all the source, the Makefile, `asm/` (the disassembly with
its data, from which `make rom` assembles `hz.md`), `assets/`, and the CRT
shaders compiled for every backend (`shaders/*.spv`, `.dxil`, `.msl`; the
DXIL was first written in session 47 -- before, those files were empty and
Direct3D 12 drew without the CRT).

**Not in it**, each fetched from its own source by the steps below:

| What | Needed for | Where it comes from |
|---|---|---|
| MSYS2 (UCRT64): gcc, make, pkgconf, SDL3, Python, Pillow, numpy, scipy, git | everything (numpy: `make pans`; numpy and scipy: `make q_check`) | msys2.org, then `tools/setup_msys2.sh` |
| Cycfi's Q (header-only) | the Q synth (optional: without it hz builds with the chip alone and the AUDIO page shows Q as NOT BUILT) | github.com/cycfi/Q, `tools/setup_msys2.sh --q` (v1.5.4) |
| SDL_shadercross and Microsoft's DXC | only to recompile the shaders (`make shaders`, `make shaders_dxc`); the compiled ones are in the tree | below, "The shaders" |

**Drivers:** none beyond the graphics card's own. SDL_GPU takes Direct3D 12
first on Windows (DXIL shaders); `SDL_GPU_DRIVER=vulkan` takes Vulkan
(SPIR-V). With no SDL_GPU device, or `HZ_GPU=0`, the game draws through
SDL_Renderer: the flat picture only. Sound is SDL3's audio on the default
device.

## The steps

1. **MSYS2.** Install from https://www.msys2.org (the installer's default is
   `C:\msys64`), or unpack the base archive anywhere with no installer:
   `msys2-base-x86_64-<date>.sfx.exe -y -oC:\tools` gives `C:\tools\msys64`
   (session 47's machine: its `C:\msys64` was a 2015 install, pacman 4.2,
   too old to update, so the new one went beside it). Open "MSYS2 UCRT64"
   (`ucrt64.exe` in that folder) and update: `pacman -Syu`, which may close
   the shell; open it again and run it once more.
2. **The tree.** Clone it anywhere (a path with spaces works: the Makefile
   quotes). `.gitattributes` keeps text LF in the checkout, as in the
   repository: Git for Windows' default `core.autocrlf=true` would otherwise
   write CRLF, MSYS2's git would then see every file changed, and bash
   refuses a CRLF script. A tree checked out before `.gitattributes` came
   (session 47) still has CRLF; with no changes of your own in it, check it
   out again once: `git rm --cached -r -q . && git reset --hard`.
3. **The packages:** in the UCRT64 shell, from the tree's top:

       tools/setup_msys2.sh --q

   (`--q` clones Q to `%USERPROFILE%\GIT\Q`, where the Makefile's `Q=auto`
   looks; `--q DIR` puts it elsewhere, then build with `Q_DIR=DIR`.)
4. **Speed (optional):** Windows Security > Virus & threat protection >
   Exclusions: the tree's folder and the MSYS2 folder. Real-time scanning of
   every compiler run and harness file slows the checks.
5. **Build and run**, in the UCRT64 shell at the tree's top:

       make
       ./hz

   On Windows `hz.exe` links SDL3 and the C and C++ runtimes statically: it
   needs only Windows's own DLLs, so it also runs from Explorer or
   PowerShell -- from the tree's top, as it reads `assets/` and `shaders/`
   from the folder it runs in -- and keeps a console window for its
   `display:` and `crt:` lines. `make WIN_STATIC=0 hz` links SDL3.dll and
   the runtimes from `/ucrt64/bin` instead (then run it from the UCRT64
   shell). The other programs -- `hz_headless`, the checks' harnesses,
   `crt_gpu_check` -- link as everywhere else and run from the UCRT64
   shell.
6. **Check**, as on Linux (the Makefile's header lists every target):

       make rom crt_check crt_sim_check
       make crt_gpu_check && ./crt_gpu_check
       make -j16 roundtrip hud anim loop mech combat unitmove units shots \
           menu sound unitai ordermenu front gameend mechai

   `./crt_gpu_check` opens a window and draws the CRT chain on the GPU;
   `SDL_GPU_DRIVER=vulkan ./crt_gpu_check crt_reference/gpu_check_vulkan`
   checks the Vulkan side. `make demo` and `make pans` run their pieces one
   after another; with the cores to spare, split them (each demo check
   takes its own temporary folder, each map its own `pans/` files):

       make hz_shot hz_demo rom && rm -rf pans
       for m in 0 1 2 3 4 5 6 7; do make pans PAN_MAPS=$m & done
       for d in $(seq 0 15); do
           python3 tools/hzdemo_check.py hz.md hz.sym ./hz_demo assets \
               --demos $d &
       done
       python3 tools/hzdemo_check.py hz.md hz.sym ./hz_demo assets \
           --demos 5 --start-at 120 &
       python3 tools/hzdemo_check.py hz.md hz.sym ./hz_demo assets \
           --demos 6 --start-at 1500 &
       wait

   (`nice -n 10` before each keeps the game smooth if it's played
   meanwhile.) Session 47's results are under "Session 47's checks" below.

## The package to hand out (session 53)

    make dist

builds `build/dist/HerzogZwei.exe` and packs it into
`build/dist/HerzogZwei/` and `build/dist/HerzogZwei-windows-COMMIT.zip`
(about 4 MB): the exe, `assets/`, the shaders for Direct3D 12 and Vulkan
(DXIL and SPIR-V), `README.txt` (from `src/platform/win/README.txt`: how to
start it, the controls, where the saves go, mods), `licenses/` (SDL3,
Nuked OPN2's notice and the LGPL 2.1's text, Q when built with it, and the
MinGW-w64 runtime, winpthreads and libiconv that the static link takes in,
with `SOURCE.txt` naming the commit and the MSYS2 packages) and
(session 55) `source.zip`, `git archive` of HEAD without `HANDOVER.md` and
the other working notes: the exe links Nuked OPN2 (LGPL 2.1) in
statically, and the LGPL asks for the source to go with it. Every text
file in it, and in the package, has the maintainer's name put as "the
author", as the Phantasy Star ports' packages do; every entry of both
zips is dated the commit's time in UTC.

(Session 61) The packed `assets/` must be the commit's byte for byte, or
nothing is packed: a network game's id hashes every byte of the assets
the game reads, so assets that differ make it another game to every
build of the commit and to the server's AI (OTHER GAME). Every package
until then had shipped 48 asset files with CRLF line ends, from a checkout
Git for Windows made before `.gitattributes` said LF, and could meet no
Linux build. `git ls-files --eol | grep w/crlf` lists such files; made LF
(`sed -i 's/\r$//'`, then `git add` them: git sees no change), they are
the commit's again.

The exe differs from `make hz`'s in four ways:

- **No console window** (`-mwindows`). Started from a terminal it writes
  its lines (`display:`, `mods:`, the gamepads) there, and `--help` works;
  started from Explorer, an error it can't go on from (no assets, no
  window) is a message box (`platform_startup`, `platform_alert`).
- **No debug records, stripped** (`-s`). `make hz`'s `-g` records hold the
  path of the folder it was built in, which takes in the user's name;
  `tools/hz_dist.py` looks through the exe for the builder's user name and
  home folder and packs nothing if either is there. The zip's dates are
  the commit's, in UTC.
- **Its own folder.** When the folder it starts in has no `assets/`, the
  game moves to the exe's folder (`platform_find_home`), so a shortcut or
  a start from another folder works. `make hz` does the same.
- **Its version details** (session 61; `hz.rc`'s `HZ_DIST`, the numbers
  from `build/dist/hz_version.h`, which the Makefile writes when HEAD
  moves): Properties, Details shows Herzog Zwei, the owner, the version
  (the commit's date and time, UTC: 2026.10.25.628 for 06:28) and the
  commit. They sign nothing.

Both exes carry the game's icon, P1's mech as a jet nose up
(`src/platform/win/hz.ico` through `hz.rc` and `windres`): Explorer shows
it, and SDL's window takes it by `SDL_HINT_WINDOWS_INTRESOURCE_ICON`.
`tools/hz_icon.py ASSETS OUT.ico [--frame 0x30] [--line 2] [--preview
PNG]` draws it from `assets/tiles/mech.png` and `assets/palettes/game.png`
(another frame or the blue team: `--frame`, `--line 3`); the `.ico` is in
the tree so the build needs no Pillow.

**Unsigned.** SmartScreen ("Windows protected your PC") warns about a
downloaded exe that neither its file nor its signer has a history for,
and every build is a new file; Windows 11's Smart App Control blocks
one outright. Session 61 looked at what would stop that: a code-signing
certificate (Microsoft's Artifact Signing, $9.99 a month, US and Canada
only; or a CA's, from about $100 a year) shows its holder's verified
legal name in every copy and still warns until the signer has a
history, EV certificates included since 2024; SignPath's free
open-source signing wants every part under an OSI licence (the assets
are the cartridge's); the Microsoft Store signs for you but takes
infringing apps down on the rights holder's report. So the package stays
unsigned and its README says how to get past the warnings: More info,
Run anyway; Unblock on the zip before unpacking; Smart App Control off.

Session 53 checked the package unpacked in a scratch folder: from another
folder it found its files beside it (Direct3D 12 with the DXIL shaders,
`mods/` made beside the exe), the window's class icon set; the exe alone
said it can't load the assets, to the terminal from a terminal and in a
message box from Explorer; `--help` printed. It imports only Windows's own
DLLs and the UCRT.

## Working from Claude Code on Windows

Claude Code's Bash tool is Git Bash, whose own runtime cannot run MSYS2's
make (PS2 found every recipe failing with 127, "Cannot create temporary
file"). Run builds through MSYS2's bash:

    MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc \
        "export USERPROFILE='$USERPROFILE'; cd '<tree>' && make -j32 ..."

(`/c/tools/msys64/...` for the base archive unpacked in `C:\tools`.) Git Bash hands MSYSTEM to
MSYS2's bash but drops most other variables -- USERPROFILE, which the
Makefile's Q search and `tools/setup_msys2.sh --q` use, and
CHERE_INVOKING, so the shell starts in its home -- hence the `export` and
the `cd` inside the command. Started from PowerShell, Explorer or the
Start menu, MSYS2 gets the whole environment. MSYS2's Python is native
Windows Python: it doesn't know MSYS2's paths, so `/tmp` written in a
script means `C:\tmp` (the checkers use `tempfile.gettempdir()`; paths on
make's command lines are converted by MSYS2 on the way). `tools/hz_mutate.py`
still writes under `/tmp` and was not tried on Windows.

## The shaders (optional)

`shaders/` holds the CRT chain compiled for every backend, in the tree;
only a change under `src/shaders/*.hlsl` needs `make shaders`, which wants
SDL_shadercross (`shadercross` on the PATH). Session 47 built it from
source as PS2's builds did:

1. `tools/setup_msys2.sh --shaders` (CMake, Ninja, SPIRV-Cross).
2. `git clone https://github.com/libsdl-org/SDL_shadercross.git` (commit
   1ff05bec573988a98ef9e0260b4da44f512b8367, PS2's; no submodules needed,
   as it builds against MSYS2's SPIRV-Cross and SDL3). It has no binary
   releases.
3. Microsoft's DXC release zip from
   https://github.com/microsoft/DirectXShaderCompiler/releases (session 47:
   v1.9.2609, `dxc_2026_09_29.zip`, dxcompiler 1.9.2609.5), unpacked into
   the clone's `external/DirectXShaderCompiler-binaries/` (`bin/`, `inc/`,
   `lib/`), where its CMake looks.
4. In the clone, from the UCRT64 shell:

       cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release \
           -DSDLSHADERCROSS_VENDORED=OFF -DSDLSHADERCROSS_DXC=ON \
           -DSDLSHADERCROSS_SHARED=ON -DSDLSHADERCROSS_CLI=ON \
           -DSDLSHADERCROSS_INSTALL=ON -DCMAKE_INSTALL_PREFIX=/usr/local
       cmake --build build && cmake --install build
       cp external/DirectXShaderCompiler-binaries/bin/x64/dxc.exe \
          external/DirectXShaderCompiler-binaries/bin/x64/dxcompiler.dll \
          external/DirectXShaderCompiler-binaries/bin/x64/dxil.dll \
          /usr/local/bin/

   `/usr/local/bin` is on the UCRT64 shell's PATH but outside pacman's
   files. `dxil.dll` beside the compiler is what signs the DXIL, which
   Direct3D 12 requires. Then `make shaders` writes all of `shaders/`;
   `make shaders_dxc` makes the DXIL with DXC alone.

What session 47's `make shaders` wrote: the SPIR-V byte for byte as the
tree had it, the MSL the same text, and the four DXIL files new -- shader
model 6.0, signed. (`make shaders_dxc` writes DXIL of other bytes, also
signed; the tree's is shadercross's.)

## Session 47's checks

Built with gcc 16.2.0: no warnings, the game with Q (v1.5.4).

* `./hz`: `display: video windows, SDL_GPU direct3d12, DXIL shaders, 60.00
  Hz, vsync on`, HDR seen on the display.
* `make rom`: the image's checksum the header's ($E2BE).
* `make crt_check`: 27 checks passed (as on Linux).
* `./crt_gpu_check`: 84 checks passed on Direct3D 12 (DXIL) and 84 on
  Vulkan (SPIR-V).
* Every check target passed, as on Linux, the whole set in about 10
  minutes on the 32 threads (the one-CPU sandbox took over an hour):
  * `make -j16` rom, crt_check, crt_sim_check (38/38), roundtrip (150
    regions identical), hud, anim, loop, mech, combat, unitmove, units,
    shots, menu: 107 s;
  * `make -j16` sound (137 cases, 0 differ), unitai, ordermenu, front,
    gameend, mechai (all identical, 0 differ): 230 s;
  * q_check (Q v1.5.4): all agree;
  * demo and pans split as in step 6: the 16 demos and both Start runs
    all agree frame by frame (174-214 s each), the 96 pan runs ok: 272 s.
