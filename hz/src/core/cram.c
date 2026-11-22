#include "cram.h"

/* cram.h says what this is for. */

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

Rgb8 cram_to_rgb8(Cram c)
{
    u8 rgb[3];
    vdp_cram_rgb(c, rgb);
    Rgb8 out = { rgb[0], rgb[1], rgb[2] };
    return out;
}

Cram cram_from_rgb8(u8 r, u8 g, u8 b)
{
    const u8 rgb[3] = { r, g, b };
    return vdp_rgb_cram(rgb);
}
