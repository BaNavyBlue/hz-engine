#ifndef HZ_PNGWRITE_H
#define HZ_PNGWRITE_H

#include "hz_types.h"

#include <stddef.h>

/* Copied from the PS2 port unchanged apart from the types header.
 *
 * (PS2 build 105) PNG out: the route probe's indexed frames (moved here from
 * tools/route_probe.c unchanged, so its PNGs are byte for byte what they
 * were) and tools/crt_ref.c's RGB crops, 8-bit sRGB or 16-bit PQ. One
 * zlib stream of fixed-Huffman deflate with greedy matching, no
 * dependencies, like png.c's reader. */

/* A cICP chunk (PNG, third edition): H.273's colour primaries, transfer,
 * matrix and full-range flag. 9, 16, 0, 1 is Rec.2020 with PQ, full range
 * -- an HDR10 still a browser shows as HDR. */
typedef struct {
    u8 primaries, transfer, matrix, full_range;
} PngCicp;

/* `pixels` is h rows of `row_bytes` (16-bit samples big-endian, as PNG
 * stores them); colour type 3 needs `plte` (plte_count RGB triples).
 * `cicp` may be NULL. False when the file cannot be written. */
bool png_write(const char *path, int w, int h, int bit_depth,
               int colour_type, const u8 *pixels, size_t row_bytes,
               const u8 *plte, int plte_count, const PngCicp *cicp);

/* (Session 78) An indexed picture as Pillow writes one (tools/hz_extract.py
 * saves its PNGs so; src/extract/ makes them again): `px` one palette index
 * a byte, w x h of them; the fewest bits a pixel that hold `colours` (1, 2,
 * 4 or 8, as Pillow picks from its palette's length); PLTE of `colours` RGB
 * triples (`rgb`); with `transparent` a tRNS of one 0 (Pillow's
 * transparency=0: index 0 clear). The bytes are not Pillow's (another
 * deflate); the picture is the same, which is what the game reads and the
 * network game's id hashes (hz_file.h). */
bool png_write_indexed(const char *path, int w, int h, const u8 *px,
                       const u8 *rgb, int colours, bool transparent);

#endif /* HZ_PNGWRITE_H */
