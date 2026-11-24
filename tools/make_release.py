#!/usr/bin/env python3
"""Package a release: the engine's source and a built game.

  python3 tools/make_release.py --platform windows --exe hz/build/dist/HerzogZwei.exe \\
      --version v0.1.0 [--license NAME PATH ...] [--out dist]

Writes OUT/hz-engine-VERSION-PLATFORM/ (emptied first) and beside it a
.zip (Windows) or .tar.gz (Linux, macOS, which keeps the program's
permissions):

  * every file of this repository that git tracks or would track (so the
    release is its own complete source -- which the LGPL asks for, the game
    linking Nuked-OPN2);
  * the game, built from that source, as hz/HerzogZwei.exe (Windows) or
    hz/hz;
  * licenses/: each --license NAME PATH (a file, or a folder's files) --
    what the game links in -- and Nuked-OPN2's;
  * README.txt for players.

Nothing else is needed to play: the game makes its own files the first time
it starts, in C, from the player's cartridge image. No game data goes in:
hz/assets/ holds only the files the repository tracks (the made ones are
ignored by git, so a tree where the game ran packs the same), and it
refuses a package with a Mega Drive cartridge image in it.

This script is for whoever builds a release (GitHub Actions does); a player
needs no Python.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
NAME = "hz-engine"

README = """Herzog Zwei -- a ROM-free engine, {version} for {platform_name}

This is a port of Herzog Zwei (Technosoft, 1989) to modern computers, in
C. It contains none of the game: the game makes its files once, the first
time it starts, from your own cartridge image. Nothing is downloaded or
sent.

1. You need your own image of Herzog Zwei for the USA -- the one commonly
   named "Herzog Zwei (U) [!]": 524,288 bytes, SHA-1
   8f7262102c2b2334f0bc88ee6fd6b08797919176. A .bin, .md, .gen or .smd
   file, or a .zip holding one, all work.

2. Start the game: {play_step}
   The first time, it asks for the image: drop the file onto its window,
   or press Enter and choose it. It checks the image, makes about 150
   files under hz/assets/ from it (a second or two) and the game starts.
   From a terminal, give the image after the program's name and it is
   used at once: {terminal_step}

{platform_notes}Keys (Escape, then CONTROLS, changes them): the arrow keys or W A S D for
the pad, Z X C (or L ; ') for A B C, Enter for Start, Escape for the menu
(controls, display, the CRT look, audio, the network game). F11 toggles
fullscreen, F10 the aspect. Gamepads work too.

This project is not affiliated with SEGA or Technosoft. The engine is
MIT-licensed (LICENSE); NOTICE.md and licenses/ list the parts that are
other people's. This folder is the complete source the game was built
from: hz/ is the game, README.md says how to build it.
"""

PLATFORMS = {
    "windows": dict(
        platform_name="Windows (64-bit)",
        exe="HerzogZwei.exe",
        play_step="run hz\\HerzogZwei.exe.",
        terminal_step="hz\\HerzogZwei.exe\n   path\\to\\image.bin "
                      "(or drag the image onto HerzogZwei.exe).",
        platform_notes="Windows may warn that the program is from an unknown "
                       "publisher (it is not\nsigned): \"More info\", then "
                       "\"Run anyway\".\n\n"),
    "linux": dict(
        platform_name="Linux (x86-64)",
        exe="hz",
        play_step="./hz/hz",
        terminal_step="./hz/hz /path/to/image.bin",
        platform_notes=""),
    "macos": dict(
        platform_name="macOS (Apple silicon and Intel)",
        exe="hz",
        play_step="./hz/hz",
        terminal_step="./hz/hz /path/to/image.bin",
        platform_notes="macOS blocks programs from the internet that are "
                       "not signed. Once, in this\nfolder: xattr -dr "
                       "com.apple.quarantine .\n\n"),
}


def git_files():
    out = subprocess.run(["git", "-C", str(TOP), "ls-files", "-z",
                          "--cached", "--others", "--exclude-standard"],
                         check=True, capture_output=True).stdout
    return sorted({p for p in out.decode("utf-8").split("\0") if p})


def archive(folder, platform):
    if platform == "windows":
        # Not with_suffix: a version's dots (v0.1.0) are not a suffix.
        path = folder.parent / (folder.name + ".zip")
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED,
                             compresslevel=9) as z:
            for p in sorted(folder.rglob("*")):
                if p.is_file():
                    z.write(p, p.relative_to(folder.parent).as_posix())
    else:
        path = folder.parent / (folder.name + ".tar.gz")
        with tarfile.open(path, "w:gz") as t:
            t.add(folder, arcname=folder.name)
    return path


def cartridge_in(path):
    """A Mega Drive image's header ("SEGA" at $100) or a Master System's
    ("TMR SEGA" at $7FF0)."""
    size = path.stat().st_size
    if size < 0x8000:
        return False
    with open(path, "rb") as fh:
        fh.seek(0x100)
        if fh.read(4) == b"SEGA":
            return True
        fh.seek(0x7FF0)
        return fh.read(8) == b"TMR SEGA"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", choices=sorted(PLATFORMS), required=True)
    ap.add_argument("--exe", required=True, help="the built game")
    ap.add_argument("--version", required=True)
    ap.add_argument("--license", nargs=2, action="append", default=[],
                    metavar=("NAME", "PATH"))
    ap.add_argument("--out", default="dist")
    a = ap.parse_args()
    plat = PLATFORMS[a.platform]

    files = git_files()
    folder = Path(a.out).resolve() / ("%s-%s-%s" % (NAME, a.version,
                                                    a.platform))
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for rel in files:
        src = TOP / rel
        if not src.is_file():
            continue                        # deleted, not yet committed
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)

    dest = folder / "hz" / plat["exe"]
    shutil.copy2(a.exe, dest)
    os.chmod(dest, 0o755)

    licenses = folder / "licenses"
    licenses.mkdir()
    for name, path in a.license:
        path = Path(path)
        if path.is_dir():
            for p in sorted(path.rglob("*")):
                if p.is_file():
                    out = licenses / name / p.relative_to(path)
                    out.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(p, out)
        elif path.is_file():
            shutil.copy2(path, licenses / ("%s-%s" % (name, path.name)))
        else:
            raise SystemExit("--license %s: %s not found" % (name, path))
    shutil.copy2(TOP / "hz" / "src" / "audio" / "LICENSE.ym3438",
                 licenses / "Nuked-OPN2-LGPL-2.1.txt")

    text = README.format(version=a.version, **plat)
    if a.platform == "windows":
        text = text.replace("\n", "\r\n")
    (folder / "README.txt").write_text(text, newline="")

    # Nothing of the game: no cartridge image in any file the package holds.
    for p in folder.rglob("*"):
        if p.is_file() and cartridge_in(p):
            raise SystemExit("a cartridge image in the package: %s" % p)
    path = archive(folder, a.platform)
    print("%s (%.1f MB)" % (path, path.stat().st_size / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
