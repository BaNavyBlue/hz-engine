#include "cram.h"

#include <stdlib.h>

/* cram.h says what this is for. */

/* Measured 8-bit DAC output levels for a VA4 in normal intensity, from the
 * PS2 port (palette.c). The ladder is non-linear; an even ramp shifts
 * every colour. */
static const u8 DAC_LADDER[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };

u8 cram_r(Cram c)
{
    return (u8)((c >> 1) & 7);
}

u8 cram_g(Cram c)
{
    return (u8)((c >> 5) & 7);
}

u8 cram_b(Cram c)
{
    return (u8)((c >> 9) & 7);
}

Cram cram_make(u8 r, u8 g, u8 b)
{
    return (Cram)(((b & 7) << 9) | ((g & 7) << 5) | ((r & 7) << 1));
}

void cram_rgb(Cram c, u8 rgb[3])
{
    rgb[0] = DAC_LADDER[cram_r(c)];
    rgb[1] = DAC_LADDER[cram_g(c)];
    rgb[2] = DAC_LADDER[cram_b(c)];
}

Cram cram_from_rgb(const u8 rgb[3])
{
    u8 level[3];
    for (int ch = 0; ch < 3; ch++) {
        int best = 0;
        for (int l = 1; l < 8; l++) {
            if (abs(rgb[ch] - DAC_LADDER[l])
                < abs(rgb[ch] - DAC_LADDER[best])) {
                best = l;
            }
        }
        level[ch] = (u8)best;
    }
    return cram_make(level[0], level[1], level[2]);
}

Rgb8 cram_to_rgb8(Cram c)
{
    u8 rgb[3];
    cram_rgb(c, rgb);
    Rgb8 out = { rgb[0], rgb[1], rgb[2] };
    return out;
}

Cram cram_from_rgb8(u8 r, u8 g, u8 b)
{
    const u8 rgb[3] = { r, g, b };
    return cram_from_rgb(rgb);
}
