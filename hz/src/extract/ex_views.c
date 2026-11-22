/* (Session 78) hz_extract.py's render_views in C (ex.h): the pictures
 * nothing loads -- each map's blocks.png for Tiled and the HUD strip's
 * hud_frame.png. render_views draws them from the files the steps before
 * wrote; those files hold the cartridge's bytes as they are (`make
 * roundtrip`), so these draw from the cartridge, the same pictures.
 * maps/path_hints.png, hz_extract.py's own drawing of 16 arrows, is not
 * the cartridge's: it is shipped as it is. */
#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* render_blocks: map n's 512 blocks as 32 x 32 pictures, 16 across. */
static bool blocks_png(HzEx *x, int n)
{
    u32 tiles_n = 0, blocks_n = 0;
    u8 *tiles = ex_lzw(x, EXR_pack_map_tiles, (u32)n, &tiles_n);
    u8 *blocks = tiles ? ex_lzw(x, EXR_pack_map_blocks, (u32)n, &blocks_n)
                       : NULL;
    const u8 *pal_b = ex_at(x, EXR_MapPalettes + (u32)n * 32, 32);
    bool ok = blocks != NULL && pal_b != NULL;
    if (ok && blocks_n < 512 * 32) {
        ok = ex_fail(x, "map %d: its blocks are %u bytes", n,
                     (unsigned)blocks_n);
    }
    ExCanvas c = { 0, 0, NULL };
    ok = ok && exc_new(x, &c, 512, 1024);
    for (int b = 0; b < 512 && ok; b++) {
        int bx = (b % 16) * 32, by = (b / 16) * 32;
        for (int i = 0; i < 16 && ok; i++) {
            const u8 *p = blocks + b * 32 + i * 2;
            u16 e = (u16)((p[0] << 8) | p[1]);
            ok = exc_tile(x, &c, tiles, tiles_n, e & 0x1FFu,
                          bx + (i % 4) * 8, by + (i / 4) * 8,
                          (e & 0x800) != 0, (e & 0x1000) != 0);
        }
    }
    if (ok) {
        u16 pal[16];
        char rel[64];
        ex_palette_words(pal_b, 1, pal);
        snprintf(rel, sizeof rel, "maps/%d_%s/blocks.png", n,
                 EX_MAP_NAMES[n]);
        ok = exc_save(x, rel, &c, pal, 16, false);
    }
    exc_free(&c);
    free(tiles);
    free(blocks);
    return ok;
}

/* The HUD strip as the window plane draws it: the sprites' and the HUD's
 * tiles and the font where the game puts them in VRAM, CRAM lines 1-3 the
 * game's palettes (line 0 black). */
static bool hud_frame_png(HzEx *x)
{
    const u8 *sprite = ex_at(x, EXR_SpriteTiles, 0x5800);
    const u8 *hud = ex_at(x, EXR_HUDTiles, 0x2000);
    const u8 *font = ex_at(x, EXR_FontTiles, 79 * 32);
    const u8 *pal_b = ex_at(x, EXR_GamePalettes, 0x60);
    if (sprite == NULL || hud == NULL || font == NULL || pal_b == NULL) {
        return false;
    }
    u8 *vram = calloc(0x10000, 1);
    if (vram == NULL) {
        return ex_fail(x, "out of memory");
    }
    memcpy(vram + 0x4800, sprite, 0x5800);
    memcpy(vram + 0xA000, hud, 0x2000);
    memcpy(vram + 0xD620, font, 79 * 32);
    u16 pal[64] = { 0 };
    ex_palette_words(pal_b, 3, pal + 16);

    ExCanvas c;
    bool ok = exc_new(x, &c, 320, 32);
    for (u32 r = 0; r < 4 && ok; r++) {
        for (u32 col = 0; col < 40; col++) {
            u16 e = ex_w(x, EXR_HUDFrameTiles + (r * 40 + col) * 2);
            const u8 *t = vram + (size_t)(e & 0x7FF) * 32;
            int line = (e >> 13) & 3;
            for (int y = 0; y < 8; y++) {
                for (int xx = 0; xx < 8; xx++) {
                    int sx = (e & 0x800) ? 7 - xx : xx;
                    int sy = (e & 0x1000) ? 7 - y : y;
                    u8 b = t[sy * 4 + sx / 2];
                    int v = (sx & 1) ? (b & 15) : (b >> 4);
                    c.px[(r * 8 + (u32)y) * 320 + col * 8 + (u32)xx] =
                        (u8)(line * 16 + v);
                }
            }
        }
    }
    ok = ok && exc_save(x, "screens/hud_frame.png", &c, pal, 64, false);
    exc_free(&c);
    free(vram);
    return ok;
}

/* render_views. */
bool ex_views(HzEx *x)
{
    for (int n = 0; n < 8; n++) {
        if (!blocks_png(x, n)) {
            return false;
        }
    }
    return hud_frame_png(x);
}
