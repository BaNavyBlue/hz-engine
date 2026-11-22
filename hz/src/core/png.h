#ifndef HZ_PNG_H
#define HZ_PNG_H

#include "hz_types.h"

/* Indexed PNG is the asset format for everything made of pixels.
 *
 * PNG colour type 3 stores exactly what the hardware stored: a plane of
 * palette indices plus a palette. One file therefore carries both, it
 * opens in any image editor, the palette is editable in the same window
 * as the art, and the index plane loads with no unpacking and no colour
 * conversion -- `px` below is one byte per pixel (the asset loader packs
 * two to a byte, as VRAM holds tiles).
 *
 * From the PS2 port; here the file is read with hz_file_read.
 *
 * The reader is here rather than behind libpng because the headless
 * target is supposed to build anywhere. It handles colour type 3 at bit
 * depths 1, 2, 4 and 8, non-interlaced, which is what every asset here
 * is and what editors write for indexed images.
 */

#define PNG_MAX_PALETTE 256

typedef struct {
    int width, height;
    u8 *px;                        /* width * height palette indices */
    u8  pal[PNG_MAX_PALETTE][3];   /* RGB, 8 bits per channel */
    int pal_count;
} PngImage;

/* Returns false and leaves `img` zeroed on any failure, reporting why on
 * stderr. Free with png_free. */
bool png_load_indexed(PngImage *img, const char *path);
void png_free(PngImage *img);

/* (Session 78) The reader's inflate on its own: a raw deflate stream (a
 * zip member's, method 8) into a malloc'd buffer, NULL if it is not one.
 * src/extract/ex_image.c opens a zipped cartridge image with it. */
u8 *png_inflate_raw(const u8 *in, size_t len, size_t *out_len);

#endif /* HZ_PNG_H */
