#ifndef HZ_LZW_H
#define HZ_LZW_H

#include "hz_types.h"

/* Direct port of LZW_Unpack ($298A), Herzog Zwei's only decompressor.
 *
 * The format is ARC's method 8 ("crunched"): Unix compress 4.0 LZW with
 * 9..12-bit codes, least significant bit first, CLEAR = $100 and the first
 * free code $101, whose output goes through ARC's RLE90 filter ($90 is the
 * repeat marker). Unlike an ARC member there is no leading max-bits byte:
 * the caller passes the packed length (d0), and the output length is
 * whatever the data decodes to. LZW_Unpack's callers pass a0 = source,
 * a1 = destination; the 68k does no bounds checking, this port does. */

/* Packed tables in the ROM. Each is 'O', a count, `count` entries of
 * {u32 offset from the table's start, u32 packed length}, then one more
 * u32 (the end of the last entry's data plus one, never read by the game),
 * then the data. Game_LoadMap ($5538) picks entry n with d1 = n * 8 + 2. */
#define HZ_PACK_MAGIC        0x4F
#define HZ_PACK_MAP_BLOCKS   0x3F000u  /* 8 maps, $4800 each -> $FF0000 */
#define HZ_PACK_MAP_TILES    0x22400u  /* 8 maps, $4000 each -> $FF5000 -> VRAM $0000 */
#define HZ_PACK_MAP_LAYOUT   0x35000u  /* 8 maps, $2000 each (64 x 64 words) -> $FF5000 */
#define HZ_PACK_FOREST       0x57800u  /* 1 entry, $8C00 -> $FF0000: the ending forest painting (Ending_UnpackForest) */

typedef enum {
    HZ_UNPACK_OK = 0,
    HZ_UNPACK_OVERFLOW,    /* dst_cap reached; dst holds the first dst_cap bytes */
    HZ_UNPACK_BAD_CODE     /* a code chain longer than the table (corrupt input) */
} HzUnpackResult;

/* Unpacks src_len packed bytes from src into dst. *out_len gets the number
 * of bytes written (capped at dst_cap). */
HzUnpackResult hz_unpack(const u8 *src, u32 src_len,
                         u8 *dst, u32 dst_cap, u32 *out_len);

/* Where entry `index` of the packed table at rom[base] lives. False if the
 * table's magic, count or offsets don't fit the ROM; the game checks none
 * of these. */
bool hz_pack_entry(const u8 *rom, u32 rom_size, u32 base, u32 index,
                   u32 *src, u32 *len);

/* The table's entry count (its second byte), or 0 if base isn't a table. */
u32 hz_pack_count(const u8 *rom, u32 rom_size, u32 base);

#endif /* HZ_LZW_H */
