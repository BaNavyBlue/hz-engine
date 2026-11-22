#ifndef HZ_RLE_H
#define HZ_RLE_H

#include "hz_types.h"

/* Port of M_RLE_UnpackToRAM ($571AC) and M_RLE_UnpackToVRAM ($57218), the
 * menu module's nibble RLE. The two differ only in where each finished
 * longword goes (move.l d0,(a1)+ against move.l d0,(a1) into VDP_DATA), so
 * one decoder serves both: the output is the byte stream either would store,
 * longwords big-endian as the 68k writes them.
 *
 * Entry: u32 output size in bytes, of which the routines use size >> 2
 * (a longword count), then run bytes: high nibble = pixel, low nibble =
 * run length - 1 for 0..$C; $D = run - 1 in the next byte, $E = in the next
 * word, $F = the next word + $10000. Eight pixels make a longword, first
 * pixel in the top nibble. Decoding stops the moment the last longword is
 * stored, mid-run if need be. */

/* RLE tables in the ROM: u32 offsets from the table's start, one per entry,
 * no count (junk follows the lists at $61108 and $70014). */
#define HZ_RLE_WINNER_FONT      0x60000u  /* 1 entry  (Winner_Show) */
#define HZ_RLE_ENDING_PICTURES  0x61100u  /* 2 entries: still life, grave (Ending_ShowPicture) */
#define HZ_RLE_TANK_PHOTOS      0x68420u  /* 2 entries (Title_LoadGfx) */
#define HZ_RLE_MENU_GFX         0x70000u  /* 5 entries: menu font, SEGA logo and copyright,
                                             title logo, ending font, "ENDE" */

typedef enum {
    HZ_RLE_OK = 0,
    HZ_RLE_BAD_SIZE,     /* size >> 2 is 0: the 68k would count down from $FFFFFFFF */
    HZ_RLE_OVERFLOW,     /* dst_cap < size & ~3; dst holds the first dst_cap bytes */
    HZ_RLE_TRUNCATED     /* the stream ran out before size & ~3 bytes came out */
} HzRleResult;

/* Decodes the entry at src (its u32 size first), at most src_len bytes of
 * it. *out_len gets the bytes written, *used the stream bytes read (size
 * word included), so src + *used is where the 68k's a0 ends up. */
HzRleResult hz_rle_unpack(const u8 *src, u32 src_len,
                          u8 *dst, u32 dst_cap, u32 *out_len, u32 *used);

/* ROM offset of entry `index` of the RLE table at rom[table] (the table's
 * start plus its u32 offset, as the routines' adda.l d0,a0). False if the
 * offset or the entry's size word falls outside the ROM; the game checks
 * neither. */
bool hz_rle_entry(const u8 *rom, u32 rom_size, u32 table, u32 index,
                  u32 *entry);

#endif /* HZ_RLE_H */
