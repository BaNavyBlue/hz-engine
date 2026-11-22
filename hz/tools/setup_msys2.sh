#!/bin/bash
# (Session 47) The Windows build's MSYS2 packages, and Cycfi's Q, for a new
# machine (docs/windows_build.md has the whole recipe; PS2's
# tools/setup_msys2.sh is the model). Run in an MSYS2 shell (any of them:
# pacman is shared) after MSYS2's own `pacman -Syu`:
#
#     tools/setup_msys2.sh               the game, the tools and the checks
#     tools/setup_msys2.sh --shaders     + CMake, Ninja, SPIRV-Cross, to
#                                        build SDL_shadercross (make shaders)
#     tools/setup_msys2.sh --q [DIR]     + clone Q at the release the port
#                                        was last built with
#
# The options go together. numpy is for `make pans`, numpy and scipy for
# `make q_check`. The versions session 47 built and checked with: gcc
# 16.2.0, make 4.4.1, pkgconf 3.0.7, SDL3 3.4.18, Python 3.14.8, Pillow
# 12.3.0, numpy 2.5.3, scipy 1.18.1, CMake 4.4.4, Ninja 1.13.2, SPIRV-Cross
# 1.4.363. pacman installs today's.
set -eu

BASE="git make
      mingw-w64-ucrt-x86_64-gcc
      mingw-w64-ucrt-x86_64-pkgconf
      mingw-w64-ucrt-x86_64-sdl3
      mingw-w64-ucrt-x86_64-python
      mingw-w64-ucrt-x86_64-python-pillow
      mingw-w64-ucrt-x86_64-python-numpy
      mingw-w64-ucrt-x86_64-python-scipy"
SHADERS="mingw-w64-ucrt-x86_64-cmake
         mingw-w64-ucrt-x86_64-ninja
         mingw-w64-ucrt-x86_64-spirv-cross"
Q_URL=https://github.com/cycfi/Q.git
Q_COMMIT=efe26114f39e4a78bd67c1456a25487d356fb910      # v1.5.4, 2026-10-09

want="$BASE"
q_dir=""
while [ $# -gt 0 ]; do
    case "$1" in
        --shaders)
            want="$want $SHADERS"
            ;;
        --q)
            if [ $# -gt 1 ] && [ "${2#--}" = "$2" ]; then
                q_dir="$2"
                shift
            else
                # where the Makefile's Q=auto looks on Windows
                q_dir="$(cygpath -u "${USERPROFILE:-$HOME}" 2>/dev/null || echo "$HOME")/GIT/Q"
            fi
            ;;
        *)
            echo "setup_msys2: unknown option $1" >&2
            exit 2
            ;;
    esac
    shift
done

# shellcheck disable=SC2086
pacman -S --needed --noconfirm $want

if [ -n "$q_dir" ]; then
    if [ ! -d "$q_dir/.git" ]; then
        git clone --recurse-submodules "$Q_URL" "$q_dir"
    fi
    git -C "$q_dir" checkout -q "$Q_COMMIT"
    git -C "$q_dir" submodule update --init --recursive
    echo "setup_msys2: Q at $q_dir ($(git -C "$q_dir" rev-parse --short HEAD))"
fi
echo "setup_msys2: done. Build from the MSYS2 UCRT64 shell: make"
