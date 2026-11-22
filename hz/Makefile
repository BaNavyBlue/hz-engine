# Herzog Zwei port. hz is the game in a window (SDL3): the title and the
# options screen, then the game against the CPU or a second player, and
# back to the title (CONT loads saved games; Escape opens the Esc screen:
# display, fullscreen, CRT, volumes, the keys with TURBO, the gamepads'
# rumble and buttons, patches, the network game, quit); when the title
# runs out, the attract demo. hz_headless is the same with no window.
# hz_shot boots to the first game frame, can pan the cameras and move the mechs, and writes PNGs. All of them read the game's data from
# assets/, never the ROM. The checkers (tools/*_check.py) read a ROM image,
# so a check that passes also shows the assets are the ROM's: by default
# hz.md, which `make rom` builds from the disassembly (asm/hz.asm with
# asm/packed/ and asm/data/) with tools/hzasm_check.py, no ROM needed.
# On Windows everything builds and runs from MSYS2's UCRT64 shell
# (docs/windows_build.md; tools/setup_msys2.sh installs what it needs).
# On macOS, with GNU make (gmake) and MacPorts' or Homebrew's SDL3
# (docs/macos_build.md).
#
#   make rom              hz.md from the disassembly (every ROM= target
#                         below makes it first when ROM is left as hz.md)
#   make tree             build/hz_tree.html: the disassembly as a clickable
#                         call tree, one page for any static web host
#                         (tools/hz_tree.py; no ROM)
#   make, make hz         the game (needs SDL3): ./hz [--assets DIR]
#                         [--map N] [--mode N] [--difficulty N] (./hz --help)
#   make tools            every tool the checks run (each check below builds
#                         its own; each tool compiles the whole game, so
#                         make -j)
#   make assets           assets/ from the ROM image (tools/hz_extract.py, Pillow);
#                         overwrites what is there. (Session 78) A ROM
#                         image outside the tree is read through the
#                         tree's hz.sym. The game makes the same files
#                         itself, in C, on its first start (src/extract/,
#                         docs/first_start.md); ./hz_assets IMAGE [DIR]
#                         runs that from a terminal
#   make assets_check ROM=...  the C extractor against hz_extract.py's
#                         assets/: every file the same (text byte for byte,
#                         PNGs by picture) and the network game's id
#   make rom_map          src/extract/rom_map.h from hz.sym again
#   make views            redraw the pictures nothing loads (the maps'
#                         blocks.png, the HUD frame) after editing assets
#   make roundtrip ROM=.. every asset read by the game's loader and packed
#                         back, against the ROM, byte for byte
#   make shots ROM=...    every map in split and wide screen -> shots/
#   make units ROM=...    every map's start units and spawn probes -> units/,
#                         checked by tools/hzunits_check.py
#   make loop ROM=...     the frame loop's pads, pause and Game_SecondTick
#                         -> loop/, checked by tools/hzloop_check.py
#   make mech ROM=...     Mech_Update with random pads and pokes -> mech/,
#                         checked by tools/hzmech_check.py
#   make hud ROM=...      the HUD gauge routines -> hud/, checked by
#                         tools/hzhud_check.py
#   make anim ROM=...     the map's animated tiles and colours on made-up
#                         records -> anim/, checked by tools/hzanim_check.py
#   make pans ROM=...     every map and view panned along PAN_TOUR with the
#                         mechs' and units' sprites, every frame checked
#                         (needs numpy; about 14 minutes; PAN_MAPS="0 1" for
#                         some maps)
#   make menu             the Esc screen and its settings, headless (no ROM)
#   make controls         the keys and gamepad inputs' bindings, names and
#                         labels (src/core/keys.c; no ROM)
#   make demo ROM=...     the whole game through the 16 attract demos against
#                         the original, frame by frame (about 30 minutes)
#   make front ROM=...    the title and options screen against the original
#   make gameend ROM=...  WINNER, the results and the endings against the
#                         original
#   make netcheck         the determinism check for netplay: 32 random
#                         two-pad games through the whole game, a hash a
#                         second -> build/netcheck/ (no ROM; about 10
#                         minutes on 32 threads); two machines' folders
#                         compared by tools/hznetcheck.py compare
#   make hz_server        (session 56) the network game's server:
#                         ./hz_server [--port 19890] (docs/netplay.md)
#   make hz_netai         (session 58) the server's nightmare AI player:
#                         ./hz_netai ASSETS [--server HOST[:PORT]]
#   make netbot_check     the server and the lockstep without the game:
#                         loss, splits, the list, 200 games at once
#   make netplay_check    two headless games through a server, their
#                         state alike every second (also 20% lost)
#   make pacecheck        (session 63) the frame pacing (src/core/pace.c)
#                         against made-up displays, 50 to 240 Hz and none
#   make netpace          (session 63) two players' clocks through a
#                         server over a made-up line: kept even when one
#                         runs fast or stops a second (about a minute)
#   make ahead            (session 64) the nightmare AI through a delay:
#                         thinking ahead on a copy of the game, exact
#                         against the CPU, as strong at 8 frames (no ROM)
#   make nightmare        (session 57) the nightmare AI (src/game/
#                         nightmare.c) as the CPU against the original's,
#                         every map, either side: it must win (no ROM;
#                         docs/nightmare.md: hz_arena, the tournaments,
#                         the training)
#   make repair           (session 72) the REPAIR patch: a repair truck
#                         and its mechanics on scenes of their own, each
#                         checked (no ROM; docs/repair.md)
#   make unitcap          (session 61) the UNITS patch: crowds of 600 a
#                         side (hz_arena --spawn) past the original's 50,
#                         and none past it without; 10,000 men a side and
#                         UNITS turned over in play, the grid checked
#                         every frame (docs/units.md)
#   make dist             (Windows) the game to hand out: build/dist/HerzogZwei/
#                         and a zip of it (no console, the icon, the assets,
#                         the shaders, a README, the licences)
#   make sdl-syntax       platform_sdl3.c, crt_gpu.c and crt_gpu_check.c
#                         against a stub of SDL3's headers
#   make crt_check        the CRT model's checks by numbers (PS2's crt_ref)
#                         on the headless game's frames -> crt_reference/
#   make crt_sim_check    the GPU passes' CPU mirror against the model
#   make crt_gpu_check    the read-back check on this machine's GPU (SDL3):
#                         ./crt_gpu_check after make crt_check
#   make shaders          src/shaders/*.hlsl -> shaders/ (SDL_shadercross)
#   make check ROM=...    roundtrip, shots, units, loop, mech, hud, anim, pans; the
#                         frames checked by tools/hzview_check.py

CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
ROM    ?= hz.md
ASSETS ?= assets
LDLIBS += -lm

CORE := $(wildcard src/core/*.c)
GAME := $(wildcard src/game/*.c)
AUDIO := $(wildcard src/audio/*.c)
# (Session 56) The network game's client: the sockets, the datagrams, the
# lobby and the lockstep (docs/netplay.md). The server is server/.
NET := $(wildcard src/net/*.c)
HDRS := $(wildcard src/core/*.h) $(wildcard src/game/*.h) \
        $(wildcard src/audio/*.h) $(wildcard src/net/*.h) \
        $(wildcard src/extract/*.h)
PLATFORM_HDRS := src/platform/platform.h
GPU_SRC := src/platform/crt_gpu.c
GPU_HDRS := src/platform/crt_gpu.h
# (Session 78) The game's files from the player's own cartridge, in C
# (src/extract/, tools/hz_extract.py's port), and the first start's window
# that runs it (docs/first_start.md): the game's and make dist's, and
# hz_assets's (the extractor from a terminal).
EXTRACT := $(wildcard src/extract/*.c)
FIRST_START := src/platform/first_start_sdl3.c $(EXTRACT)

# Unpacking an archive keeps its files' dates, which can be older than
# binaries built before it: make would call those binaries up to date (it
# did, session 16). So every binary also depends on .srcsum, a checksum of
# the sources' contents, rewritten whenever they change.
SRC_ALL := Makefile $(sort $(wildcard src/*.c src/*/*.c src/*/*.h tools/*.c))
SRC_SUM := $(shell cat $(SRC_ALL) | cksum)
$(shell test "`cat .srcsum 2>/dev/null`" = "$(SRC_SUM)" \
        || echo "$(SRC_SUM)" > .srcsum)

# (Session 36) PS2's Q backend (src/audio/q/: PS2's q_synth.cpp, C++20,
# with q_ps2.h for what it took from PS2's headers), Cycfi's Q being
# header-only: found as PS2 finds it, $(HOME)/GIT/Q (or on Windows the same
# under the user's folder), or Q_DIR=. Q=auto, the default, builds hz with
# Q wherever Q is found and a C++ compiler answers, and without it anywhere
# else (the AUDIO page's SYNTH then shows Q as NOT BUILT); Q=1 insists, Q=0
# leaves it out. hz_headless takes Q only for Q=1: it is the checks'
# binary. Switching Q on or off doesn't rebuild on its own: `make clean`.
# (macOS) Last, Q beside the folder that holds the repositories
# (<GIT>/Claude/herzog-zwei -> <GIT>/Q), wherever that folder is: a Mac
# keeps it under ~/Documents/GIT as often as ~/GIT.
Q_HAS = { test -f "$(1)/q_lib/include/q/synth/gen/envelope_gen.hpp" \
          || test -f "$(1)/q_lib/include/q/synth/envelope_gen.hpp"; }
Q_DIR ?= $(shell for d in "$$HOME/GIT/Q" \
                     "$$(cygpath -u "$$USERPROFILE" 2>/dev/null)/GIT/Q" \
                     "$$(cygpath -H 2>/dev/null)/$$USER/GIT/Q" \
                     "$$(cd "$(CURDIR)/../../Q" 2>/dev/null && pwd)"; do \
                     $(call Q_HAS,$$d) \
                     && { echo "$$d"; break; }; done)
ifeq ($(Q_DIR),)
Q_DIR := $(HOME)/GIT/Q
endif
Q ?= auto
ifeq ($(Q),auto)
GAME_Q := $(shell $(call Q_HAS,$(Q_DIR)) \
                  && test -f "$(Q_DIR)/infra/include/infra/support.hpp" \
                  && $(CXX) --version >/dev/null 2>&1 && echo 1 || echo 0)
else
GAME_Q := $(Q)
endif
ifeq ($(shell uname -s 2>/dev/null),Darwin)
Q_LDLIBS   ?= -lc++
endif
Q_LDLIBS   ?= -lstdc++
Q_CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -g
Q_INCLUDES := -isystem "$(Q_DIR)/q_lib/include" -isystem "$(Q_DIR)/infra/include"
Q_OBJ      := build/q/q_synth.o build/q/q_hz.o
ifeq ($(GAME_Q),1)
GAME_Q_OBJ  := $(Q_OBJ)
GAME_Q_DEFS := -DHZ_HAVE_Q
GAME_Q_LIBS  = $(Q_LDLIBS)
endif
ifeq ($(Q),1)
HEADLESS_Q_OBJ  := $(Q_OBJ)
HEADLESS_Q_DEFS := -DHZ_HAVE_Q
HEADLESS_Q_LIBS  = $(Q_LDLIBS)
endif

SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)

# (Session 47) Windows (MSYS2 UCRT64; docs/windows_build.md), by the
# environment's OS or MSYS2's uname, as PS2's build 121 finds it. There hz
# links SDL3 and the C and C++ runtimes statically, so hz.exe runs from
# Explorer or PowerShell with no DLL beside it; it keeps its console for
# the display:/crt: lines (pkg-config's -mwindows dropped). WIN_STATIC=0
# links as everywhere else (then run it from the UCRT64 shell).
WINDOWS := $(filter Windows_NT Msys Cygwin,$(OS) $(shell uname -o 2>/dev/null))
GAME_SDL_LIBS = $(SDL_LIBS)
ifneq ($(WINDOWS),)
WIN_STATIC ?= 1
ifeq ($(WIN_STATIC),1)
GAME_SDL_LIBS = -static $(filter-out -mwindows,\
                  $(shell pkg-config --static --libs sdl3 2>/dev/null))
endif
# (Session 56) Winsock, Windows's own DLL: the network game's sockets.
LDLIBS += -lws2_32
endif

# (Session 53) On Windows hz.exe carries the game's icon, a mech in jet
# form (src/platform/win/hz.rc and hz.ico, which tools/hz_icon.py draws
# from the assets): Explorer's, the taskbar's and the window's.
WINDRES ?= windres
ifneq ($(WINDOWS),)
WIN_RES := build/win/hz_res.o
DIST_RES := build/dist/hz_res.o
endif

# make builds the game alone, as PS1's and PS2's do. Until session 54 it
# built every tool too, each compiling the whole game again: 23 programs,
# 2.5 minutes at -j1 against hz's 8 s.
TOOLS := hz_audiolog hz_shot hz_unpack hz_headless hz_loop hz_mech hz_hud hz_anim \
         hz_shots hz_unitmove hz_roundtrip hz_sound hz_ymchan hz_unitai \
         hz_ordermenu hz_mechai hz_front hz_gameend hz_demo hz_netcheck \
         hz_rumble hz_controls hz_server hz_netbot hz_netai hz_arena \
         hz_replay hz_pacecheck hz_repair hz_assets hz_gameid

all: hz
tools: $(TOOLS)

# The game draws through SDL_GPU and the CRT chain (src/platform/crt_gpu.c,
# PS2's; docs/crt.md), reading its compiled shaders at start-up from
# shaders/ in the folder it runs from; without them, or with no SDL_GPU
# device, through SDL_Renderer (the flat picture only).
hz: $(CORE) $(AUDIO) $(GAME) $(NET) src/main.c src/platform/platform_sdl3.c \
    $(GPU_SRC) $(FIRST_START) $(HDRS) $(PLATFORM_HDRS) $(GPU_HDRS) \
    $(GAME_Q_OBJ) $(WIN_RES)
	@test -n "$(SDL_LIBS)" || { \
	    echo "SDL3 not found by pkg-config."; \
	    echo "Install SDL3 development files, or build the"; \
	    echo "display-free target instead: make hz_headless"; \
	    exit 1; }
	$(CC) $(CFLAGS) $(GAME_Q_DEFS) $(SDL_CFLAGS) -o $@ $(CORE) $(AUDIO) \
	    $(GAME) $(NET) src/main.c src/platform/platform_sdl3.c $(GPU_SRC) \
	    $(FIRST_START) $(GAME_Q_OBJ) $(WIN_RES) $(GAME_SDL_LIBS) $(GAME_Q_LIBS) $(LDLIBS)

build/win/hz_res.o: src/platform/win/hz.rc src/platform/win/hz.ico
	@mkdir -p build/win
	$(WINDRES) -I src/platform/win -O coff -o $@ src/platform/win/hz.rc

# (Session 61) make dist's exe also has its version details (hz.rc's
# HZ_DIST): the commit's date and time, UTC, as its version (2026.10.25.1412)
# and with the commit's hash as text; with no git, 0.0.0.0 and "unknown".
# hz_version.h is written only when they change, so make dist links again
# only when HEAD has moved.
build/dist/hz_res.o: src/platform/win/hz.rc src/platform/win/hz.ico \
                     build/dist/hz_version.h
	$(WINDRES) -DHZ_DIST -I src/platform/win -I build/dist -O coff -o $@ \
	    src/platform/win/hz.rc

build/dist/hz_version.h: FORCE
	@mkdir -p build/dist
	@v=`TZ=UTC git log -1 --format=%cd --date=format-local:%Y,%m,%d,%H%M \
	    2>/dev/null | sed 's/,0*\([0-9]\)/,\1/g'`; \
	 c=`git rev-parse --short HEAD 2>/dev/null`; \
	 test -n "$$v" && test -n "$$c" || { v=0,0,0,0; c=unknown; }; \
	 printf '#define HZ_VERSION_NUM %s\n#define HZ_VERSION_STR "%s, commit %s"\n' \
	     "$$v" "`echo $$v | tr , .`" "$$c" > $@.new; \
	 if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi

FORCE:

# (Session 53) The game to hand out on Windows. hz as above, but with no
# console window of its own (-mwindows: started from a terminal it writes
# there, and an error it can't go on from is also a message box; see
# platform.h), no debug records (they hold the path of the folder it was
# built in) and stripped: build/dist/HerzogZwei.exe. Then tools/hz_dist.py
# checks it holds no user name or home folder and packs it with assets/,
# the shaders, src/platform/win/README.txt and the licences into
# build/dist/HerzogZwei/ and build/dist/HerzogZwei-windows-COMMIT.zip.
# Unpacked anywhere, the game finds its files beside it.
DIST_CFLAGS := $(filter-out -g,$(CFLAGS))
DIST_Q_OBJ  := $(if $(GAME_Q_OBJ),build/dist/q/q_synth.o build/dist/q/q_hz.o)

dist: build/dist/HerzogZwei.exe
	python3 tools/hz_dist.py build/dist/HerzogZwei.exe "$(ASSETS)" \
	    build/dist --map build/dist/HerzogZwei.map \
	    --licences "$(shell pkg-config --variable=prefix sdl3)/share/licenses" \
	    $(if $(DIST_Q_OBJ),--q-licence "$(Q_DIR)/LICENSE")

build/dist/HerzogZwei.exe: $(CORE) $(AUDIO) $(GAME) $(NET) src/main.c \
                           src/platform/platform_sdl3.c $(GPU_SRC) \
                           $(FIRST_START) $(HDRS) \
                           $(PLATFORM_HDRS) $(GPU_HDRS) $(DIST_RES) \
                           $(DIST_Q_OBJ) .srcsum
	@test -n "$(WINDOWS)" || { \
	    echo "make dist: Windows only (MSYS2 UCRT64)"; exit 1; }
	@test -n "$(SDL_LIBS)" || { echo "SDL3 not found by pkg-config."; exit 1; }
	@mkdir -p build/dist
	$(CC) $(DIST_CFLAGS) $(GAME_Q_DEFS) $(SDL_CFLAGS) -mwindows -s \
	    -Wl,--no-insert-timestamp -Wl,-Map=build/dist/HerzogZwei.map \
	    -o $@ $(CORE) $(AUDIO) $(GAME) $(NET) src/main.c \
	    src/platform/platform_sdl3.c $(GPU_SRC) $(FIRST_START) $(DIST_RES) \
	    $(DIST_Q_OBJ) \
	    $(GAME_SDL_LIBS) $(GAME_Q_LIBS) $(LDLIBS)

build/dist/q/q_synth.o: src/audio/q/q_synth.cpp src/audio/q/q_synth.h \
                        src/audio/q/q_ps2.h
	@mkdir -p build/dist/q
	$(CXX) $(filter-out -g,$(Q_CXXFLAGS)) $(Q_INCLUDES) -c -o $@ \
	    src/audio/q/q_synth.cpp

build/dist/q/q_hz.o: src/audio/q/q_hz.c src/audio/q/q_ps2.h
	@mkdir -p build/dist/q
	$(CC) $(DIST_CFLAGS) -c -o $@ src/audio/q/q_hz.c

# The SDL3 platform, the CRT chain's SDL_GPU and the read-back check
# compiled against tools/sdl3_stub (no SDL3 needed): a syntax and type check
# only, for where SDL3 isn't installed.
sdl-syntax:
	$(CC) $(CFLAGS) -fsyntax-only -Itools/sdl3_stub \
	    src/platform/platform_sdl3.c $(GPU_SRC) tools/crt_gpu_check.c \
	    src/main.c

# The CRT simulation's checks (PS2's, docs/crt.md), on the headless game's
# frames written indexed into crt_reference/ first: title.png (the title
# with its logo, frame 900) and game.png (a split-screen game's frame 266,
# 60 frames into play after the loading screen: the HUD). `make
# crt_check`: the model by numbers (crt_reference/checks.txt); `make
# crt_sim_check`: the GPU passes' CPU mirror against the model at every
# SDR crop, and the HDR encodes (sim_check.txt);
# `./crt_ref crop crt_reference/title.png crt_reference NAME SX SY SW SH`
# writes crops at display resolution. No GPU needed.
CRT_CORE := src/core/crt.c src/core/cram.c src/core/hz_vdp.c \
            src/core/png.c src/core/pngwrite.c src/core/hz_file.c \
            src/core/hz_mods.c

crt_ref: tools/crt_ref.c $(CRT_CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ tools/crt_ref.c $(CRT_CORE) $(LDLIBS)

crt_frames: hz_headless
	mkdir -p crt_reference
	env -u HZ_KEYS -u HZ_PAD1 -u HZ_PAD2 -u HZ_RAW -u HZ_FRAMES_DIR \
	    HZ_OUT_INDEXED=1 HZ_FRAMES=900 HZ_OUT=crt_reference/title.png \
	    HZ_SAVES=crt_reference/saves ./hz_headless --assets "$(ASSETS)"
	env -u HZ_KEYS -u HZ_PAD1 -u HZ_PAD2 -u HZ_RAW -u HZ_FRAMES_DIR \
	    HZ_OUT_INDEXED=1 HZ_FRAMES=266 HZ_OUT=crt_reference/game.png \
	    HZ_SAVES=crt_reference/saves ./hz_headless --assets "$(ASSETS)" \
	    --map 0 --mode 1

crt_check: crt_ref crt_frames
	./crt_ref check crt_reference

crt_sim_check: crt_ref crt_frames
	./crt_ref sim crt_reference

# The read-back check: the SDL_GPU chain drawn on this machine's GPU, read
# back and held against the model (docs/crt.md, "The read-back check").
# SDL3; run from here after `make crt_check` (it reads crt_reference/'s
# frames and shaders/): ./crt_gpu_check, its report in
# crt_reference/gpu_check/.
crt_gpu_check: tools/crt_gpu_check.c $(GPU_SRC) $(CRT_CORE) $(HDRS) \
               $(GPU_HDRS) $(PLATFORM_HDRS)
	@test -n "$(SDL_LIBS)" || { echo "SDL3 not found by pkg-config."; exit 1; }
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ tools/crt_gpu_check.c $(GPU_SRC) \
	    $(CRT_CORE) $(SDL_LIBS) $(LDLIBS)

# The CRT shaders, compiled offline (PS2's targets): src/shaders/*.hlsl
# through SDL_shadercross into shaders/ as SPIR-V (Vulkan), DXIL (Direct3D
# 12) and MSL (Metal), which are kept in the tree -- the game's build needs
# no shader compiler. Rerun after changing a shader. SHADERCROSS names the
# tool; SHADER_FORMATS narrows the formats (DXIL needs a shadercross built
# with DXC's dxil library, which Windows has). As brought over from PS2 the
# DXIL files were empty (PS2's tree had none written), so a Direct3D 12
# device -- SDL_GPU's first choice on Windows -- fell back to SDL_Renderer.
# (Session 47) `make shaders` on Windows wrote them (docs/windows_build.md:
# SDL_shadercross with DXC 1.9.2609; SPIR-V byte for byte as before, MSL
# the same text). A format that fails is removed, not left behind (PS2's
# build 142b): make and the game would take an empty file for a shader.
SHADERCROSS    ?= shadercross
SHADER_FORMATS ?= spv dxil msl
SHADER_STEMS   := crt_fullscreen.vert crt_down.frag crt_across.frag \
                  crt_sharp.frag

shaders:
	@command -v "$(SHADERCROSS)" >/dev/null 2>&1 || { \
	    echo "$(SHADERCROSS) not found: build SDL_shadercross, or"; \
	    echo "make shaders SHADERCROSS=/path/to/shadercross"; exit 1; }
	@mkdir -p shaders; status=0; \
	for s in $(SHADER_STEMS); do \
	    stage=fragment; \
	    case $$s in \
	        *.vert) \
	            stage=vertex ;; \
	    esac; \
	    for f in $(SHADER_FORMATS); do \
	        case $$f in \
	            spv) \
	                d=SPIRV ;; \
	            dxil) \
	                d=DXIL ;; \
	            msl) \
	                d=MSL ;; \
	        esac; \
	        if "$(SHADERCROSS)" src/shaders/$$s.hlsl -s HLSL -d $$d -t $$stage \
	                -e main -o shaders/$$s.$$f; then \
	            echo "  shaders/$$s.$$f"; \
	        else \
	            rm -f shaders/$$s.$$f; \
	            echo "  FAILED shaders/$$s.$$f (removed)"; status=1; \
	        fi; \
	    done; \
	done; \
	exit $$status

DXC ?= dxc

shaders_dxc:
	@command -v "$(DXC)" >/dev/null 2>&1 || { \
	    echo "$(DXC) not found: make shaders_dxc DXC=/path/to/dxc"; exit 1; }
	@mkdir -p shaders; status=0; \
	for s in $(SHADER_STEMS); do \
	    profile=ps_6_0; \
	    case $$s in \
	        *.vert) \
	            profile=vs_6_0 ;; \
	    esac; \
	    if "$(DXC)" -nologo -E main -T $$profile -Fo shaders/$$s.dxil \
	            src/shaders/$$s.hlsl; then \
	        echo "  shaders/$$s.dxil"; \
	    else \
	        rm -f shaders/$$s.dxil; \
	        echo "  FAILED shaders/$$s.dxil (removed)"; status=1; \
	    fi; \
	done; \
	exit $$status

hz_headless: $(CORE) $(AUDIO) $(GAME) $(NET) src/main.c \
             src/platform/platform_headless.c $(HDRS) $(PLATFORM_HDRS) \
             $(HEADLESS_Q_OBJ)
	$(CC) $(CFLAGS) $(HEADLESS_Q_DEFS) -o $@ $(CORE) $(AUDIO) $(GAME) \
	    $(NET) src/main.c src/platform/platform_headless.c \
	    $(HEADLESS_Q_OBJ) $(HEADLESS_Q_LIBS) $(LDLIBS)

# (Session 36) The headless game with Q, for `make q_check`.
hz_headless_q: $(CORE) $(AUDIO) $(GAME) $(NET) src/main.c \
               src/platform/platform_headless.c $(HDRS) $(PLATFORM_HDRS) \
               $(Q_OBJ)
	$(CC) $(CFLAGS) -DHZ_HAVE_Q -o $@ $(CORE) $(AUDIO) $(GAME) $(NET) \
	    src/main.c src/platform/platform_headless.c $(Q_OBJ) $(Q_LDLIBS) \
	    $(LDLIBS)

# (Session 37) HZ's FM drums for Q's drum table (tools/hz_qdrums.c, with
# Q): `survey` lists the instruments each song strikes; `strike SONG
# INST[,INST]` plays those alone through the chip and through Q; `song SONG
# SECONDS nuked|q OUT.wav` a song alone through the game's mixer; `model` one
# drum model's strike.
hz_qdrums: $(CORE) $(AUDIO) $(GAME) tools/hz_qdrums.c $(HDRS) $(Q_OBJ)
	$(CC) $(CFLAGS) -DHZ_HAVE_Q -o $@ $(CORE) $(AUDIO) $(GAME) \
	    tools/hz_qdrums.c $(Q_OBJ) $(Q_LDLIBS) $(LDLIBS)

# The Q backend in HZ's mix against the chip (tools/hzq_check.py; needs Q,
# numpy and scipy): the same notes, the same level within 3 dB, the EFFECTS
# split, a switch from the AUDIO page; the drums (session 37): kick notes
# PS2's kick, fill notes PS2's toms, the kick's level.
q_check: hz_headless_q hz_qdrums
	python3 tools/hzq_check.py ./hz_headless_q "$(ASSETS)" ./hz_qdrums

build/q/q_synth.o: src/audio/q/q_synth.cpp src/audio/q/q_synth.h \
                   src/audio/q/q_ps2.h
	@mkdir -p build/q
	$(CXX) $(Q_CXXFLAGS) $(Q_INCLUDES) -c -o $@ src/audio/q/q_synth.cpp

build/q/q_hz.o: src/audio/q/q_hz.c src/audio/q/q_ps2.h
	@mkdir -p build/q
	$(CC) $(CFLAGS) -c -o $@ src/audio/q/q_hz.c

hz_shot: $(CORE) $(AUDIO) $(GAME) tools/hz_shot.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_shot.c $(LDLIBS)

hz_loop: $(CORE) $(AUDIO) $(GAME) tools/hz_loop.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_loop.c $(LDLIBS)

hz_mech: $(CORE) $(AUDIO) $(GAME) tools/hz_mech.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_mech.c $(LDLIBS)

hz_hud: $(CORE) $(AUDIO) $(GAME) tools/hz_hud.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_hud.c $(LDLIBS)

hz_anim: $(CORE) $(AUDIO) $(GAME) tools/hz_anim.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_anim.c $(LDLIBS)

hz_shots: $(CORE) $(AUDIO) $(GAME) tools/hz_shots.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_shots.c $(LDLIBS)

# A chip-write log (tools/z80run.py --log) through Nuked-OPN2 and the PSG.
hz_audiolog: tools/hz_audiolog.c src/audio/ym3438.c src/audio/ym3438.h \
             src/core/psg.c src/core/psg.h
	$(CC) $(CFLAGS) -o $@ tools/hz_audiolog.c src/audio/ym3438.c \
	    src/core/psg.c -lm

hz_unitmove: $(CORE) $(AUDIO) $(GAME) tools/hz_unitmove.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_unitmove.c $(LDLIBS)

hz_unitai: $(CORE) $(AUDIO) $(GAME) tools/hz_unitai.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_unitai.c $(LDLIBS)

hz_ordermenu: $(CORE) $(AUDIO) $(GAME) tools/hz_ordermenu.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_ordermenu.c $(LDLIBS)

hz_front: $(CORE) $(AUDIO) $(GAME) tools/hz_front.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_front.c $(LDLIBS)

hz_gameend: $(CORE) $(AUDIO) $(GAME) tools/hz_gameend.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_gameend.c $(LDLIBS)

hz_demo: $(CORE) $(AUDIO) $(GAME) tools/hz_demo.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_demo.c $(LDLIBS)

hz_netcheck: $(CORE) $(AUDIO) $(GAME) tools/hz_netcheck.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_netcheck.c $(LDLIBS)

hz_arena: $(CORE) $(AUDIO) $(GAME) tools/hz_arena.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_arena.c $(LDLIBS)

# (Session 72) The REPAIR patch's scenes (tools/hz_repair.c, docs/repair.md):
# a repair truck and its mechanics on open ground, each scene checked for
# what must happen, with the patch alone, with UNITS and with PATHS too.
# No ROM.
hz_repair: $(CORE) $(AUDIO) $(GAME) tools/hz_repair.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_repair.c $(LDLIBS)

repair: hz_repair
	./hz_repair "$(ASSETS)"

# (Session 57) hz_arena with a second nightmare AI for the code's A/B: OLD=
# another nightmare.c (git show REV:src/game/nightmare.c > old.c), its
# names renamed nightmare_old_*, plays the side "old" as the CPU:
#   make hz_arena_ab OLD=old.c
#   python3 tools/hzarena.py ./hz_arena_ab assets nm old --diffs 0
NM_OLD_NAMES := -Dnightmare_think=nightmare_old_think \
                -Dnightmare_describe=nightmare_old_describe \
                -Dnightmare_summary=nightmare_old_summary \
                -Dnightmare_task_name=nightmare_old_task_name \
                -Dnightmare_set_param=nightmare_old_set_param \
                -Dnightmare_set_param_side=nightmare_old_set_param_side \
                -Dnightmare_param=nightmare_old_param \
                -Dnightmare_reset=nightmare_old_reset \
                -Dnightmare_copy=nightmare_old_copy \
                -Dnightmare_think_ahead=nightmare_old_think_ahead \
                -Dnightmare_ahead_copy=nightmare_old_ahead_copy
hz_arena_ab: $(CORE) $(AUDIO) $(GAME) tools/hz_arena.c $(HDRS) $(OLD)
	@test -n "$(OLD)" || { echo "make hz_arena_ab OLD=path/to/nightmare.c"; exit 1; }
	@mkdir -p build
	$(CC) $(CFLAGS) $(NM_OLD_NAMES) -Isrc/game -c -o build/nightmare_old.o $(OLD)
	$(CC) $(CFLAGS) -DHZ_ARENA_OLD_BRAIN -o $@ $(CORE) $(AUDIO) $(GAME) \
	    tools/hz_arena.c build/nightmare_old.o $(LDLIBS)

hz_mechai: $(CORE) $(AUDIO) $(GAME) tools/hz_mechai.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_mechai.c $(LDLIBS)

hz_rumble: $(CORE) $(AUDIO) $(GAME) tools/hz_rumble.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_rumble.c $(LDLIBS)

# (Session 54) The controls on their own (src/core/keys.c).
hz_controls: src/core/keys.c src/core/keys.h src/core/hz_types.h tools/hz_controls.c
	$(CC) $(CFLAGS) -o $@ src/core/keys.c tools/hz_controls.c

# The C port of the Z80 driver on z80run.py's scripts (make sound).
hz_sound: $(CORE) $(AUDIO) $(GAME) tools/hz_sound.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_sound.c $(LDLIBS)

hz_ymchan: $(CORE) $(AUDIO) tools/hz_ymchan.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) tools/hz_ymchan.c $(LDLIBS)

hz_roundtrip: $(CORE) $(AUDIO) $(GAME) tools/hz_roundtrip.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(GAME) tools/hz_roundtrip.c $(LDLIBS)

hz_unpack: tools/hz_unpack.c src/core/hz_lzw.c src/core/hz_rle.c
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(TOOLS) hz: .srcsum

# The ROM image, assembled from the disassembly by the in-tree assembler
# (tools/hzasm_check.py, tools/m68kenc.py, and tools/z80enc.py for the Z80
# driver's source at Z80Driver; it stops unless the image's checksum is the
# header's). Real AS (asl + p2bin) builds the same bytes.
# A changed source (it is relocatable) builds with --fix-checksum added.
# hz.sym: its symbols (NAME $ADDR), for tools/m68krun.py's checks.
hz.md: asm/hz.asm $(wildcard asm/packed/*.lzw asm/data/*.bin)
	python3 tools/hzasm_check.py asm/hz.asm -o $@ -s hz.sym

# Every song (map + 3 for the maps) and every sound effect, played by the
# original driver (tools/z80run.py), as VGM files in songs/.
songs: $(ROM)
	mkdir -p songs
	for n in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17; do \
	    python3 tools/z80run.py "$(ROM)" --song $$n --repeat --seconds 40 \
	        --vgm songs/song$$n.vgm || exit 1; \
	done
	python3 tools/z80run.py "$(ROM)" --seconds 45 --vgm songs/sfx_all.vgm \
	    --sfx "`python3 -c "print(','.join('%d:%d' % (10 + 90 * n, n) for n in range(29)))"`"

rom: hz.md

# The disassembly as a clickable call tree: one page, its data inside it.
build/hz_tree.html: asm/hz.asm tools/hz_tree.py tools/hz_tree.html
	mkdir -p build
	python3 tools/hz_tree.py asm/hz.asm $@

tree: build/hz_tree.html

# The game's data from the ROM image, and the views drawn from the assets.
assets: $(ROM)
	python3 tools/hz_extract.py "$(ROM)" "$(ASSETS)"

views:
	python3 tools/hz_extract.py --views "$(ASSETS)"

# (Session 78) The game's own extractor in C from a terminal (src/extract/;
# docs/first_start.md): ./hz_assets IMAGE [DIR]. No SDL, no Python.
hz_assets: $(CORE) $(AUDIO) $(EXTRACT) tools/hz_assets.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(AUDIO) $(EXTRACT) tools/hz_assets.c \
	    $(LDLIBS)

# (Session 78) The network game's id of assets folders: ./hz_gameid DIR...
hz_gameid: tools/hz_gameid.c $(CORE) $(AUDIO) $(GAME) $(NET) $(HDRS)
	$(CC) $(CFLAGS) -o $@ tools/hz_gameid.c $(CORE) $(AUDIO) $(GAME) \
	    $(NET) $(LDLIBS)

# src/extract/rom_map.h again from hz.sym, after `make rom` moved a label.
rom_map:
	python3 tools/hz_rommap.py

# The C extractor against its reference: rom_map.h is hz.sym's, then
# hz_assets on ROM into build/assets_c/ (emptied first), every file the
# same as assets/'s -- text byte for byte, PNGs the same picture
# (tools/hzassets_check.py) -- and the game's id from them the same.
assets_check: hz_assets hz_gameid $(ROM)
	python3 tools/hz_rommap.py --check
	rm -rf build/assets_c
	./hz_assets "$(ROM)" build/assets_c
	python3 tools/hzassets_check.py build/assets_c "$(ASSETS)" ./hz_gameid

roundtrip: hz_roundtrip $(ROM)
	./hz_roundtrip "$(ROM)" "$(ASSETS)"

# Modes: 0 split screen, two players; 1 split, P2 CPU; 2 split, P1 CPU;
# 5 wide, P2 CPU; 6 wide, P1 CPU. A CPU player starts with units, whose
# type depends on the difficulty: each mode takes a different one per map.
SHOT_MODES := 0:split 1:split_p2cpu 2:split_p1cpu 5:wide 6:wide_p2

shots: hz_shot
	mkdir -p shots
	for m in 0 1 2 3 4 5 6 7; do \
	    for mk in $(SHOT_MODES); do \
	        mode=$${mk%%:*}; kind=$${mk#*:}; \
	        ./hz_shot "$(ASSETS)" shots/map$${m}_$$kind.png --map $$m \
	            --mode $$mode --difficulty $$(( (m + mode) % 4 )) \
	            --raw shots/map$${m}_$$kind.raw || exit 1; \
	    done; \
	done

# The frame loop (tools/hz_loop: game_frame pass by pass) against
# tools/hzloop_check.py's model of ReadJoypads, Game_CheckPause,
# Game_SecondTick, Mechs_Update's pads and production, the HUD bars
# (HUD_UpdateGauges, HUD_FlushBars) and the map animation
# (Map_AnimTilesUpdate/_Upload, tools/hzanim_check.py's model). Per map: every
# mode with random pads and pokes; a demo left to run out its 60 s, one
# with pokes, one with random pads (Start ends it); each HQ destroyed. Then fixed
# scripts: the free-ammo cheat with two players and with a CPU (no
# toggle), pads held through the fade (the first pass doesn't read them).
LOOP_CHEAT := 100:1:S,104:1:-,110:1:SULA,110:2:DRBC,113:1:-,113:2:-,$\
              200:2:S,203:2:-,210:2:SA,212:2:-
LOOP_FADE  := 0:1:URA,0:2:DLB,60:1:-,70:2:C

loop: hz_loop $(ROM)
	rm -rf loop
	mkdir -p loop
	for m in 0 1 2 3 4 5 6 7; do \
	    for mode in 0 1 2 3 4 5 6 7; do \
	        ./hz_loop "$(ASSETS)" loop/map$${m}_mode$$mode.txt --map $$m \
	            --mode $$mode --difficulty $$(( (m + mode) % 4 )) \
	            --passes 3000 --pokes --pads-random \
	            --seed $$(( m * 8 + mode + 1 )) > /dev/null || exit 1; \
	    done; \
	    ./hz_loop "$(ASSETS)" loop/map$${m}_demo.txt --map $$m --mode 5 \
	        --passes 5000 --demo > /dev/null || exit 1; \
	    ./hz_loop "$(ASSETS)" loop/map$${m}_demo_pokes.txt --map $$m --mode 1 \
	        --passes 5000 --demo --pokes --seed $$(( 100 + m )) \
	        > /dev/null || exit 1; \
	    ./hz_loop "$(ASSETS)" loop/map$${m}_demo_pads.txt --map $$m --mode 6 \
	        --passes 5000 --demo --pads-random --seed $$(( 200 + m )) \
	        > /dev/null || exit 1; \
	    ./hz_loop "$(ASSETS)" loop/map$${m}_hq1.txt --map $$m --mode 1 \
	        --passes 1000 --pads-random --hq-zero 500:1 \
	        --seed $$(( 300 + m )) > /dev/null || exit 1; \
	    ./hz_loop "$(ASSETS)" loop/map$${m}_hq2.txt --map $$m --mode 2 \
	        --passes 1000 --pokes --hq-zero 700:2 \
	        --seed $$(( 400 + m )) > /dev/null || exit 1; \
	done
	./hz_loop "$(ASSETS)" loop/cheat_two.txt --mode 0 --passes 400 \
	    --pads "$(LOOP_CHEAT)" > /dev/null
	./hz_loop "$(ASSETS)" loop/cheat_cpu.txt --mode 1 --passes 400 \
	    --pads "$(LOOP_CHEAT)" > /dev/null
	./hz_loop "$(ASSETS)" loop/fade_held.txt --map 1 --mode 0 --passes 200 \
	    --pads "$(LOOP_FADE)" > /dev/null
	python3 tools/hzloop_check.py "$(ROM)" loop

# Mech_Update with Obj_TerrainEvent's mech side (tools/hz_mech: the frame
# loop with random pads and pokes, both mechs logged round each
# Mech_Update) against tools/hzmech_check.py's model: every map in modes 0
# (two players), 1 and 5 (P2 the CPU, its start units), 6 (P1 the CPU).
mech: hz_mech $(ROM)
	rm -rf mech
	mkdir -p mech
	for m in 0 1 2 3 4 5 6 7; do \
	    for mode in 0 1 5 6; do \
	        ./hz_mech "$(ASSETS)" mech/map$${m}_mode$$mode.txt --map $$m \
	            --mode $$mode --difficulty $$(( (m + mode) % 4 )) \
	            --passes 3000 --seed $$(( m * 8 + mode + 11 )) \
	            > /dev/null || exit 1; \
	    done; \
	done
	python3 tools/hzmech_check.py "$(ROM)" mech

# The HUD gauges (tools/hz_hud: HUD_QueueBar for every value, max and
# length of bar, HUD_UpdateGauges from random states at its edges,
# HUD_FlushBars over random queues) against tools/hzhud_check.py's model.
hud: hz_hud $(ROM)
	rm -rf hud
	mkdir -p hud
	./hz_hud hud/hud.txt --seed 1
	python3 tools/hzhud_check.py "$(ROM)" hud/hud.txt

# The map animation (tools/hz_anim: Map_AnimTilesUpdate and
# Map_AnimTilesUpload on made-up records and states, the cases the maps
# never reach) against tools/hzanim_check.py's model.
anim: hz_anim $(ROM)
	rm -rf anim
	mkdir -p anim
	./hz_anim "$(ASSETS)" anim/anim.txt --seed 1
	python3 tools/hzanim_check.py "$(ROM)" anim/anim.txt

# The shots (tools/hz_shots: Shot_FireBullet, Shot_FireMissile and
# Combat_Update's shots on made-up states round a point of each map, a third
# of them round an HQ) against tools/hzshots_check.py's model.
combat: hz_shots $(ROM)
	rm -rf combat
	mkdir -p combat
	for m in 0 1 2 3 4 5 6 7; do \
	    ./hz_shots "$(ASSETS)" combat/map$$m.txt --map $$m --cases 3000 \
	        --seed $$(( m + 1 )) > /dev/null || exit 1; \
	done
	python3 tools/hzshots_check.py "$(ROM)" combat/*.txt

# The units' move (tools/hz_unitmove: Units_UpdateTeam and MapFx_Update on
# made-up states round event tiles, outposts, HQs, water and the map's
# edges of each map, chains of 1-8 updates) against
# tools/hzunitmove_check.py's model.
unitmove: hz_unitmove $(ROM)
	rm -rf unitmove
	mkdir -p unitmove
	for m in 0 1 2 3 4 5 6 7; do \
	    ./hz_unitmove "$(ASSETS)" unitmove/map$$m.txt --map $$m --cases 600 \
	        --seed $$(( m + 1 )) > /dev/null || exit 1; \
	done
	python3 tools/hzunitmove_check.py "$(ROM)" unitmove/*.txt

# The unit AI (src/game/unit_ai.c, through tools/hz_unitai) against the
# original routines from the ROM image on tools/m68krun.py, byte for byte
# over the units, shots, RNG seed and sfx queue (tools/hzunitai_check.py):
# made-up states of every type, order and state, 1-4 updates each, 4 seeds;
# then every update of a real game on each map (hz_unitai --game: wide
# screen against the CPU, P1's mech put down beside the CPU's units every
# $40 frames). About 100 s; hz.sym comes with hz.md.
unitai: hz_unitai $(ROM)
	test -f hz.sym || python3 tools/hzasm_check.py asm/hz.asm -o hz.md -s hz.sym
	for s in 1 2 3 4; do \
	    python3 tools/hzunitai_check.py "$(ROM)" hz.sym ./hz_unitai "$(ASSETS)" \
	        --cases 400 --seed $$s --quiet || exit 1; \
	done
	for m in 0 1 2 3 4 5 6 7; do \
	    python3 tools/hzunitai_check.py "$(ROM)" hz.sym ./hz_unitai "$(ASSETS)" \
	        --game $$m 5 $$(( m % 4 )) 1500 1 --keep /tmp/hzunitai_game \
	        --quiet || exit 1; \
	done
	rm -rf /tmp/hzunitai_game

# The order menu (src/game/order_menu.c, through tools/hz_ordermenu)
# against the original module from the ROM image on tools/m68krun.py, with
# a model of the VDP's ports (tools/hzordermenu_check.py): real games of
# every map and mode with each mech's menu poked closed, opening, open or
# closing (rows, types, orders, money near the cost, carrying, destroyed,
# radar), 1-24 frames of pads each; all of RAM and VRAM/CRAM/VSRAM byte
# for byte after. 4 seeds x 400 cases, about 60 s.
ordermenu: hz_ordermenu $(ROM)
	test -f hz.sym || python3 tools/hzasm_check.py asm/hz.asm -o hz.md -s hz.sym
	for s in 1 2 3 4; do \
	    python3 tools/hzordermenu_check.py "$(ROM)" hz.sym ./hz_ordermenu \
	        "$(ASSETS)" --cases 400 --seed $$s || exit 1; \
	done

# The title and options screen (src/game/front.c, title.c, options.c,
# through tools/hz_front) against the original Menu_TitleFlow on
# tools/m68krun.py (tools/hzfront_check.py): made-up scripts of pads (and
# the title song's end), compared frame by frame -- VRAM, CRAM, VSRAM,
# registers, every sound command, the options and record left; CONT stops
# both (it loads saved games in the port, takes a password on the
# cartridge), and a saved game's load is checked against Password_Check
# accepting a password for the same record. About 4 minutes.
front: hz_front $(ROM)
	test -f hz.sym || python3 tools/hzasm_check.py asm/hz.asm -o hz.md -s hz.sym
	for s in 1 2; do \
	    python3 tools/hzfront_check.py "$(ROM)" hz.sym ./hz_front "$(ASSETS)" \
	        --runs 30 --frames 1500 --seed $$s || exit 1; \
	done

# The attract demos end to end (tools/hz_demo: the whole game from the
# demo's setup, through its loading screen and Game_Start, to the title
# again) against the original on tools/m68krun.py (tools/hzdemo_check.py):
# every frame's VRAM, CRAM, VSRAM and registers, the state behind them
# (globals, mechs, units, outposts) and every sound command, all 16 demos;
# then Start on demo 5's loading screen (back to the title) and in demo 6
# (its end). The port's CHANGED points are patched into the original.
# About 30 minutes: run it detached; DEMO_DONE=DIR skips the demos that
# agreed in an earlier run (they leave a file there).
demo: hz_demo $(ROM)
	test -f hz.sym || python3 tools/hzasm_check.py asm/hz.asm -o hz.md -s hz.sym
	python3 tools/hzdemo_check.py "$(ROM)" hz.sym ./hz_demo "$(ASSETS)" \
	    $(if $(DEMO_DONE),--done $(DEMO_DONE))
	python3 tools/hzdemo_check.py "$(ROM)" hz.sym ./hz_demo "$(ASSETS)" \
	    --demos 5 --start-at 120
	python3 tools/hzdemo_check.py "$(ROM)" hz.sym ./hz_demo "$(ASSETS)" \
	    --demos 6 --start-at 1500

# (Session 52) The determinism check for netplay (tools/hz_netcheck.c,
# tools/hznetcheck.py): a network game would run the whole game on both
# machines and send only the pads, so every build must come out the same
# from the same pads. 32 seeds of a random two-pad player, 200,000
# frames each (NETCHECK_FRAMES=), through the title and options screen,
# the games (two players, either side the CPU, CPU against CPU), their
# ends and the attract demo, a hash of the state every second ->
# build/netcheck/. Then, with another machine's or compiler's folder:
#   python3 tools/hznetcheck.py compare build/netcheck OTHER
netcheck: hz_netcheck
	python3 tools/hznetcheck.py run ./hz_netcheck "$(ASSETS)" build/netcheck \
	    $(if $(NETCHECK_FRAMES),--frames $(NETCHECK_FRAMES))

# (Session 56) The network game's server (docs/netplay.md): one UDP socket,
# the lobby, the rooms, the pads passed on. Nothing of the game in it.
NET_SOCK := src/net/net_sock.c src/net/net_proto.c
hz_server: server/hz_server.c $(NET_SOCK) src/net/net_sock.h \
           src/net/net_proto.h src/core/hz_types.h
	$(CC) $(CFLAGS) -o $@ server/hz_server.c $(NET_SOCK) $(LDLIBS)

# (Session 58) The server's nightmare AI: a player the server runs, which
# plays the whole game headless in lockstep with the person's, its pad the
# nightmare AI's (docs/netplay.md). ./hz_netai ASSETS [--server HOST].
hz_netai: server/hz_netai.c $(CORE) $(AUDIO) $(GAME) $(NET) $(HDRS)
	$(CC) $(CFLAGS) -o $@ server/hz_netai.c $(CORE) $(AUDIO) $(GAME) $(NET) \
	    $(LDLIBS)

# (Session 59) Recorded games (src/game/replay.h: the game writes one to
# saves/replays/ as each ends) played again with no window, checked
# against their hashes, and looked at: what each side did.
hz_replay: tools/hz_replay.c $(CORE) $(AUDIO) $(GAME) $(NET) $(HDRS)
	$(CC) $(CFLAGS) -o $@ tools/hz_replay.c $(CORE) $(AUDIO) $(GAME) \
	    $(NET) $(LDLIBS)

# (Session 56) Players without the game, for the server and the lockstep:
# tools/hz_netbot.c.
hz_netbot: tools/hz_netbot.c $(NET) $(wildcard src/net/*.h) src/core/pace.c \
           src/core/pace.h
	$(CC) $(CFLAGS) -o $@ tools/hz_netbot.c $(NET) src/core/pace.c $(LDLIBS)

# (Session 56) The server and the lockstep through hz_netbot, against a
# server of its own on a free port: pads checked one by one, with 30% of
# the datagrams lost, a split made to be caught on both sides, rooms
# picked off the list, and 200 games at once (tools/hznetbot_check.py).
netbot_check: hz_server hz_netbot
	python3 tools/hznetbot_check.py ./hz_server ./hz_netbot

# (Session 56) Two network games through the headless game and a server
# of their own, each player a random pad: their state hashes the same
# every second, also with 20% of the datagrams lost
# (tools/hznetplay_check.py; HZ_NETLOG). (Session 58) And the server's
# nightmare AI: a person alone against it (the hashes alike), people
# meeting people first, people only, the AI here until a person comes.
# (Session 67) And the AI's own recording of the game, played again.
netplay_check: hz_server hz_headless hz_netai hz_replay
	python3 tools/hznetplay_check.py ./hz_server ./hz_headless "$(ASSETS)" \
	    --netai ./hz_netai --replay ./hz_replay

# (Session 63) The frame pacing (src/core/pace.c, docs/netplay.md "The
# pace") on its own against made-up displays: locked to one near the
# game's rate, the game's rate on the clock on any other, each frame on
# screen as evenly as the rates allow, no catch-up burst after a wait.
hz_pacecheck: src/core/pace.c src/core/pace.h src/core/hz_types.h \
              tools/hz_pacecheck.c
	$(CC) $(CFLAGS) -o $@ src/core/pace.c tools/hz_pacecheck.c $(LDLIBS)

pacecheck: hz_pacecheck
	./hz_pacecheck

# (Session 63) The balance between two machines' clocks: pairs of
# hz_netbot players each on its own clock, over a line made 20 ms long
# with 4 ms of jitter (HZ_NET_LAG, HZ_NET_JITTER): one clock 0.2% fast,
# with the balance and without; one side stopped a second; both even
# (tools/hznetpace_check.py, about a minute).
netpace: hz_server hz_netbot
	python3 tools/hznetpace_check.py ./hz_server ./hz_netbot

# (Session 59) Recorded games (src/game/replay.h, docs/replays.md): games
# played by hz_headless from scripted pads -- against the nightmare AI and
# the original's, two players, a pause -- each written to saves/replays/
# as it ends, then played again by hz_replay: each must be the game that
# was recorded (every second's hash alike, the same winner).
replay_check: hz_headless hz_replay
	python3 tools/hzreplay_check.py ./hz_headless ./hz_replay "$(ASSETS)"

# (Session 57) The nightmare AI (src/game/nightmare.c, docs/nightmare.md)
# against the original's AI, both as the CPU, through tools/hz_arena.c:
# 8 games a map (NIGHTMARE_GAMES=) on difficulty A from either side; the
# nightmare must win 85% of each side's games and lose at most 5%
# (tools/hzarena.py; on 32 threads about half a minute). No ROM.
NIGHTMARE_GAMES ?= 8
nightmare: hz_arena
	python3 tools/hzarena.py ./hz_arena "$(ASSETS)" nm cpu --diffs 0 \
	    --games $(NIGHTMARE_GAMES) --check-side 1 --check-wins 85 \
	    --check-losses 5
	python3 tools/hzarena.py ./hz_arena "$(ASSETS)" cpu nm --diffs 0 \
	    --games $(NIGHTMARE_GAMES) --check-side 2 --check-wins 85 \
	    --check-losses 5

# (Session 68) The nightmare AI's terrain atlas (src/game/nm_atlas.h,
# docs/nightmare.md "The atlas"): src/game/nm_atlas.c made again from the
# map data (hz_arena --atlas, tools/hzatlas.py; ATLAS_JOBS processes at
# once). After a change to the terrain or the unit AI; under a minute.
ATLAS_JOBS ?= 4
atlas: hz_arena
	python3 tools/hzatlas.py ./hz_arena "$(ASSETS)" --jobs $(ATLAS_JOBS)

# (Session 61) UNITS, no limit to a side's units (docs/units.md): on three
# maps a crowd of 600 a side put down round each HQ (hz_arena --spawn),
# every one placed and played with the patch, no side past 50 without;
# then 10,000 men a side, and UNITS turned over every 97 frames, the
# grid's lists checked every frame (--check-crowd). About a minute; no
# ROM.
unitcap: hz_arena
	python3 tools/hzunitcap_check.py ./hz_arena "$(ASSETS)"

# (Session 64) The nightmare AI through a delay (hz_arena --delay1/2,
# --ahead, --check-ahead; tools/hzahead_check.py): thinking ahead on a
# copy of the game, every pass it foresaw against the CPU the real game's;
# thinking ahead by none the very games of thinking on the game as it is;
# 8 frames' delay thinking ahead two thirds of the wins with none, and
# without it three in four lost. About a minute on 32 threads; no ROM.
ahead: hz_arena
	python3 tools/hzahead_check.py ./hz_arena "$(ASSETS)"

# (Session 62) CPU HITS (HZ_FIX_CPU_HITS): what a CPU side's landed hit
# halves, at every call of the unit AI and the CPU mech's (hz_arena
# --check-hits): on every map, a bot against the CPU and the CPU against
# itself, with UNITS's crowds (a side's slot 49 in use, which until
# session 62 the fix took for P1's mech and back) and with the default
# patches. About a minute; no ROM.
cpuhits: hz_arena
	for m in 0 1 2 3 4 5 6 7; do \
	    for p1 in bot cpu; do \
	        ./hz_arena "$(ASSETS)" --map $$m --p1 $$p1 --p2 cpu \
	            --fixes B7 --spawn 80 --frames 20000 --check-hits \
	            > /dev/null || exit 1; \
	        ./hz_arena "$(ASSETS)" --map $$m --p1 $$p1 --p2 cpu \
	            --frames 20000 --check-hits > /dev/null || exit 1; \
	    done; \
	done

# (Session 54) The rumble (src/game/rumble.c): 32 seeds of a random
# two-pad player, 200,000 frames each (RUMBLE_FRAMES=), through the
# title, the games (two players, either side the CPU, CPU against CPU),
# their ends and the attract demo; each frame what the rumble felt held
# against the game's own sound effects and its mechs' and HQs' fields
# (tools/hz_rumble.c's head), and every kind of shake felt somewhere.
# tools/hzrumble_check.py -> build/rumble/; no ROM.
rumble: hz_rumble
	python3 tools/hzrumble_check.py ./hz_rumble "$(ASSETS)" build/rumble \
	    $(if $(RUMBLE_FRAMES),--frames $(RUMBLE_FRAMES))

# (Session 54) The controls (src/core/keys.c): every pad input's name and
# every pad's labels for it, the defaults, random bindings and held
# inputs against a model of their own, the sticks', triggers' and hat's
# edges (tools/hz_controls.c's head). No ROM, no assets.
controls: hz_controls
	./hz_controls

# The game's end and the endings (src/game/game_end.c, ending.c, through
# tools/hz_gameend) against the original MenuModule_GameEnd on
# tools/m68krun.py (tools/hzgameend_check.py): made-up states as Game_Over
# leaves them (many with a record complete or one win short of it, so the
# endings run) and pads, compared frame by frame -- VRAM, CRAM, VSRAM,
# registers, every sound command, the options and record left; the
# password the cartridge shows is skipped (the port saves the win).
# About 7 minutes.
gameend: hz_gameend $(ROM)
	test -f hz.sym || python3 tools/hzasm_check.py asm/hz.asm -o hz.md -s hz.sym
	for s in 1 2; do \
	    python3 tools/hzgameend_check.py "$(ROM)" hz.sym ./hz_gameend \
	        "$(ASSETS)" --runs 12 --seed $$s || exit 1; \
	done

# The CPU mechs' AI (src/game/mech_ai.c, through tools/hz_mechai) against
# the original AI_MechThink on tools/m68krun.py (tools/hzmechai_check.py):
# real games (CPU against CPU or random pads, every map and mode the seed
# picks) with every call in a new AI state and a sample of the rest
# compared -- both mech records, the units and the RNG seed byte for byte,
# the rest of RAM untouched; seeds 3 and 4 as the games go, seed 23 with
# pokes into the AI's states (all eight maps across its games).
mechai: hz_mechai $(ROM)
	for s in 3 4; do \
	    python3 tools/hzmechai_check.py "$(ROM)" hz.sym ./hz_mechai \
	        "$(ASSETS)" --seed $$s --games 6 --frames 20000 --sample 40 \
	        || exit 1; \
	done
	python3 tools/hzmechai_check.py "$(ROM)" hz.sym ./hz_mechai "$(ASSETS)" \
	    --seed 23 --games 4 --frames 10000 --sample 30 --pokes 2 60

# The C port of the Z80 sound driver (src/audio/hz_snd.c, run by
# tools/hz_sound) against the original driver on tools/z80run.py's Z80, write
# for write (tools/hzsound_check.py): every song, every sfx, sfx over the map
# songs, stops, pauses, fades, and made-up banks of every flag. About 10
# minutes, the original being Python; SOUND_CACHE=DIR keeps its logs, and a
# run that finds them all takes seconds.
sound: hz_sound $(ROM)
	python3 tools/hzsound_check.py "$(ROM)" "$(ASSETS)" \
	    $(if $(SOUND_CACHE),--cache $(SOUND_CACHE))

# The Esc screen (src/game/sysmenu.c) through hz_headless: what its pages
# write to settings.json, bindings, QUIT, Enter closing it not reaching the
# game, and the AUDIO page's music/effects split: in the game (HZ_WAV),
# every effect and the map songs alone (hz_sound --volume), and the mixer's
# channel table against Nuked-OPN2 (hz_ymchan). tools/hzmenu_check.py;
# about a minute, no ROM.
menu: hz_headless hz_sound hz_ymchan hz_rumble
	python3 tools/hzmenu_check.py ./hz_headless "$(ASSETS)"

# The mods folder (docs/mods.md) through hz_headless: a mod of each kind,
# good and broken, each in a scratch mods folder, against the game with
# none: named in use or set aside, the same WAV and last frame or (a good
# one) the one it changes different. tools/hzmods_check.py; no ROM.
mods_check: hz_headless
	python3 tools/hzmods_check.py ./hz_headless "$(ASSETS)"

# Each map in modes 0-3 (none, P2's, P1's, no start units): the records
# Game_Start leaves and ~9,000-14,000 spawn probes each (hz_shot
# --spawn-probes), against tools/hzunits_check.py's model.
units: hz_shot $(ROM)
	mkdir -p units
	for m in 0 1 2 3 4 5 6 7; do \
	    for mode in 0 1 2 3; do \
	        ./hz_shot "$(ASSETS)" units/shot.png --map $$m --mode $$mode \
	            --difficulty $$(( (m + mode) % 4 )) \
	            --units units/map$${m}_mode$$mode.txt --spawn-probes \
	            > /dev/null || exit 1; \
	    done; \
	done
	rm -f units/shot.png
	python3 tools/hzunits_check.py "$(ROM)" units/*.txt

# The pan tour (hz_shot --pan; P2 does the opposite, or with --chase
# follows P1): 120 frames right
# and down, 32 frames reversing every frame, 250 left and up (into the
# map's left and top edges from anywhere), slow and diagonal moves, a
# pause, 250 frames diagonally into the far corner.
PAN_TOUR := 8,0,120 : 0,8,120 : \
    8,8,1 : -8,-8,1 : 8,8,1 : -8,-8,1 : 8,8,1 : -8,-8,1 : 8,8,1 : -8,-8,1 : \
    8,8,1 : -8,-8,1 : 8,8,1 : -8,-8,1 : 8,8,1 : -8,-8,1 : 8,8,1 : -8,-8,1 : \
    8,8,1 : -8,-8,1 : 8,8,1 : -8,-8,1 : 8,-8,1 : -8,8,1 : 8,-8,1 : -8,8,1 : \
    8,-8,1 : -8,8,1 : 8,-8,1 : -8,8,1 : 8,-8,1 : -8,8,1 : 8,-8,1 : -8,8,1 : \
    -8,0,250 : 0,-8,250 : 3,5,100 : -1,1,64 : 7,-2,90 : 0,0,5 : 8,8,250 : \
    -5,-8,60

# A loop round the start point, in steps of 1 to 7 px, twice: a view that
# follows it keeps the CPU's start units moving in and out across every
# edge of the view.
HOME_LOOP := -4,0,30 : 0,4,30 : 4,0,60 : 0,-4,60 : -4,0,60 : 0,4,30 : \
    4,0,30 : -3,-5,24 : 3,5,24 : 5,-3,24 : -5,3,24 : -1,0,40 : 0,1,40 : \
    1,0,40 : 0,-1,40 : -7,7,12 : 7,-7,12 : 7,7,12 : -7,-7,12
HOME_TOUR := $(HOME_LOOP) : $(HOME_LOOP)

# Each map in modes 0, 5 and 6 twice: P2 chasing P1 with the alerts on
# (jets, each in the other's view), and P2 mirroring P1 while both land
# and take off; then each map once (the mode by map) with P2 chasing and
# both landing, and with both destroyed (debris; the VDP's per-line
# limits); then in split screen with a CPU player and its start units:
# P2 CPU chasing, P1 CPU landing, and each with the CPU's view circling
# its base (HOME_TOUR; P2 CPU with the alerts, P1 CPU landing). The
# difficulty (so the start units'
# type) is (map + mode) mod 4. Each run's raw frames (about 110 MB) are
# checked, then removed; the last frame stays as
# pans/map<N>_mode<M>_<run>.png.
PAN_RUN = d=pans/map$(1)_mode$(2)_$(3); rm -rf $$d; mkdir -p $$d; \
	./hz_shot "$(ASSETS)" $$d.png --map $(1) --mode $(2) --pan "$(PAN_TOUR)" \
	    --difficulty $$(( ($(1) + $(2)) % 4 )) $(4) --raw-frames $$d \
	    > /dev/null || exit 1; \
	python3 tools/hzview_check.py "$(ROM)" $$d || exit 1; rm -rf $$d

PAN_MAPS ?= 0 1 2 3 4 5 6 7

pans: hz_shot $(ROM)
	mkdir -p pans
	for m in $(PAN_MAPS); do \
	    for mode in 0 5 6; do \
	        $(call PAN_RUN,$${m},$${mode},chase,--chase --alerts); \
	        $(call PAN_RUN,$${m},$${mode},land,--land); \
	    done; \
	    mode=`echo "0 5 6" | cut -d' ' -f$$((m % 3 + 1))`; \
	    $(call PAN_RUN,$${m},$${mode},chase_land,--chase --land); \
	    $(call PAN_RUN,$${m},$${mode},debris,--chase --debris); \
	    $(call PAN_RUN,$${m},1,chase,--chase --alerts); \
	    $(call PAN_RUN,$${m},2,land,--land); \
	    $(call PAN_RUN,$${m},1,home,--alerts --pan2 "$(HOME_TOUR)"); \
	    $(call PAN_RUN,$${m},2,home,--land --pan "$(HOME_TOUR)" \
	        --pan2 "$(PAN_TOUR)"); \
	done

check-shots: shots $(ROM)
	python3 tools/hzview_check.py "$(ROM)" shots

check: roundtrip check-shots units loop mech hud anim combat unitmove unitai \
       ordermenu mechai front gameend sound pans demo

# (Session 47) Each under its Windows name too (MSYS2's gcc writes hz.exe
# for -o hz).
CLEAN_BINS := $(TOOLS) hz hz_headless_q hz_qdrums crt_ref crt_gpu_check

clean:
	rm -f $(CLEAN_BINS) $(addsuffix .exe,$(CLEAN_BINS)) .srcsum
	rm -rf shots pans units loop mech hud anim combat crt_reference build \
	    *.dSYM    # (macOS) clang's -g symbols

.PHONY: rom_map assets_check repair tree nightmare atlas unitcap cpuhits pacecheck netpace ahead controls crt_frames crt_check crt_sim_check shaders shaders_dxc menu mods_check sdl-syntax sound songs all tools rom assets views roundtrip shots units loop mech hud anim combat unitmove unitai \
        ordermenu mechai gameend rumble \
        pans \
        check-shots netcheck netbot_check netplay_check rumble dist \
        replay_check \
        check clean
