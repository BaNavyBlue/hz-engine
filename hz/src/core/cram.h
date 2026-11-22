#ifndef HZ_CRAM_H
#define HZ_CRAM_H

#include "hz_types.h"
#include "hz_vdp.h"

/* A CRAM word's helpers, as the PS2 port's palette.c has them, for the CRT
 * chain brought over from it (crt.c, crt_gpu.c and their tools), so those
 * read as PS2's do. The colours are hz_vdp.c's: the same measured VA4 DAC
 * ladder (0, 52, 87, 116, 144, 172, 206, 255). */

/* 0000 BBB0 GGG0 RRR0 */
typedef u16 Cram;

typedef struct {
    u8 r, g, b;
} Rgb8;

/* A channel's level, 0..7. */
u8   cram_r(Cram c);
u8   cram_g(Cram c);
u8   cram_b(Cram c);
Cram cram_make(u8 r, u8 g, u8 b);

/* Through the ladder (vdp_cram_rgb), and back: each channel to its nearest
 * level (vdp_rgb_cram), so a colour the ladder makes comes back exactly --
 * an indexed PNG's PLTE turns back into the frame's CRAM words. */
Rgb8 cram_to_rgb8(Cram c);
Cram cram_from_rgb8(u8 r, u8 g, u8 b);

#endif /* HZ_CRAM_H */
