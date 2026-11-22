#!/usr/bin/env python3
"""The files the game's first start makes, by digest: written once from a
proved extraction, checked against any other.

  python3 tools/assets_list.py --write [ASSETS]    tools/assets.sha256
  python3 tools/assets_list.py --check [ASSETS]    exit 1 if any differs

ASSETS is the folder the game made its files in (default hz/assets). A
text file's digest is its bytes' SHA-256. A PNG's is its picture's -- the
size, bit depth and colour type, the palette, the transparency and the
pixels, decoded here with zlib -- since two PNG writers deflate the same
picture into other bytes. The game itself needs none of this (it makes its
files in C and checks the cartridge by its SHA-1); this is for checking a
build of the engine, with nothing but Python's own library.
"""
import hashlib
import struct
import sys
import zlib
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
LIST = TOP / "tools" / "assets.sha256"


def unfilter(raw, width_bytes, bpp, height):
    """PNG's five row filters undone: the rows, unpacked bytes."""
    rows = []
    prev = bytearray(width_bytes)
    at = 0
    for _ in range(height):
        f = raw[at]
        row = bytearray(raw[at + 1:at + 1 + width_bytes])
        at += 1 + width_bytes
        for i in range(width_bytes):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                row[i] = (row[i] + a) & 255
            elif f == 2:
                row[i] = (row[i] + b) & 255
            elif f == 3:
                row[i] = (row[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                row[i] = (row[i] + pred) & 255
            elif f != 0:
                raise ValueError("filter %d" % f)
        rows.append(bytes(row))
        prev = row
    return rows


def png_digest(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    at = 8
    ihdr = plte = trns = b""
    idat = bytearray()
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        at += 12 + n
        if kind == b"IHDR":
            ihdr = body
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", ihdr)
    if interlace:
        raise ValueError("%s: interlaced" % path)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits = depth * channels
    rows = unfilter(zlib.decompress(bytes(idat)), (w * bits + 7) // 8,
                    max(1, bits // 8), h)
    pixels = b"".join(rows)
    pic = hashlib.sha256()
    pic.update(struct.pack(">IIBB", w, h, depth, ctype))
    pic.update(struct.pack(">I", len(plte)) + plte)
    pic.update(struct.pack(">I", len(trns)) + trns)
    pic.update(pixels)
    return "png:" + pic.hexdigest()


def digest(path):
    if path.suffix.lower() == ".png":
        return png_digest(path)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def listing(assets):
    out = {}
    for p in sorted(assets.rglob("*")):
        if p.is_file():
            out[p.relative_to(assets).as_posix()] = digest(p)
    return out


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("--write", "--check") or len(args) > 2:
        print(__doc__)
        return 2
    assets = Path(args[1]) if len(args) > 1 else TOP / "hz" / "assets"
    have = listing(assets)
    if args[0] == "--write":
        lines = ["# The files the game's first start makes in hz/assets/ "
                 "(and the port's own),",
                 "# by digest: a PNG's of its picture. "
                 "tools/assets_list.py writes and checks it."]
        lines += ["%s  %s" % (d, rel) for rel, d in have.items()]
        LIST.write_text("\n".join(lines) + "\n", encoding="utf-8",
                        newline="\n")
        print("%s: %d files" % (LIST.relative_to(TOP), len(have)))
        return 0
    want = {}
    for line in LIST.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            d, rel = line.split("  ", 1)
            want[rel] = d
    bad = 0
    for rel, d in want.items():
        if rel not in have:
            print("MISSING  %s" % rel)
            bad += 1
        elif have[rel] != d:
            print("DIFFERS  %s" % rel)
            bad += 1
    for rel in sorted(set(have) - set(want)):
        print("EXTRA    %s" % rel)
        bad += 1
    same = sum(1 for rel, d in want.items() if have.get(rel) == d)
    print("%d of %d files as proved" % (same, len(want)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
