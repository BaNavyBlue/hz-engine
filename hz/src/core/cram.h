#ifndef HZ_CRAM_H
#define HZ_CRAM_H

#include "hz_types.h"

/* A colour as the game's palettes hold them (a CRAM word), and its helpers,
 * as the PS2 port's palette.c has them, for the draw layer, the platforms
 * and the CRT chain brought over from PS2 (crt.c, crt_gpu.c and their
 * tools). A colour's 8-bit RGB is the measured VA4 DAC ladder's (0, 52,
 * 87, 116, 144, 172, 206, 255): an even ramp shifts every colour. */

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

/* Through the ladder, and back: each channel to its nearest level, so a
 * colour the ladder makes comes back exactly -- an indexed PNG's PLTE
 * turns back into the frame's colours. cram_rgb and cram_from_rgb are the
 * same with the channels in an array (R, G, B). */
Rgb8 cram_to_rgb8(Cram c);
Cram cram_from_rgb8(u8 r, u8 g, u8 b);
void cram_rgb(Cram c, u8 rgb[3]);
Cram cram_from_rgb(const u8 rgb[3]);

#endif /* HZ_CRAM_H */
