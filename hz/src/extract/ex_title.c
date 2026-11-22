/* (Session 78) hz_extract.py's extract_title in C (ex.h): the title's and
 * the options screen's pictures, fonts and palettes, the game's end's
 * (WINNER and the results), and data/menu.json (the music test's songs,
 * the maps' names). */
#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

/* The SEGA logo and the copyright line (RLE_MenuGfx entry 1, VRAM tiles
 * $480-$4FF), as the window plane draws them. */
static const ExGroup SEGA_GROUPS[] = {
    { "SEGA logo (Title_SegaLogo)", 0x480, 12, 4, 1, true },
};

static const ExGroup COPYRIGHT_GROUPS[] = {
    { "copyright, overseas: top row (window (8, 22))", 0x4B0, 16, 1, 1,
      false },
    { "copyright, overseas: bottom row (window (8, 23))", 0x4C0, 24, 1, 1,
      false },
    { "copyright, Japan: top row", 0x4D8, 16, 1, 1, false },
    { "copyright, Japan: bottom row", 0x4E8, 24, 1, 1, false },
};

/* menu_text: the menu font's codes, '@' (its blank) as a space. */
void ex_menu_text(char *out, size_t n, const u8 *b, int len)
{
    size_t k = 0;
    for (int i = 0; i < len && k + 5 < n; i++) {
        u8 c = b[i];
        if (c == 0x40) {
            out[k++] = ' ';
        } else if (c >= 0x30 && c < 0x7F && c != '{') {
            out[k++] = (char)c;
        } else {
            k += (size_t)snprintf(out + k, n - k, "{%02X}", c);
        }
    }
    out[k] = '\0';
}

/* music_titles: song n's two lines of 19 characters. */
bool ex_music_title(HzEx *x, int n, char lines[2][100])
{
    for (int k = 0; k < 2; k++) {
        const u8 *b = ex_at(x, EXR_MusicTitles + (u32)(n * 40 + k * 20), 20);
        if (b == NULL) {
            return false;
        }
        bool nul = false;
        for (int i = 0; i < 19; i++) {
            nul = nul || b[i] == 0;
        }
        if (b[19] != 0 || nul) {
            return ex_fail(x, "MusicTitles %d: not 19 characters and a NUL",
                           n);
        }
        ex_menu_text(lines[k], 100, b, 19);
    }
    return true;
}

/* palette_words of a block's lines. */
static bool words_at(HzEx *x, u32 a, int lines, u16 *words)
{
    const u8 *p = ex_at(x, a, (u32)lines * 32);
    if (p == NULL) {
        return false;
    }
    ex_palette_words(p, lines, words);
    return true;
}

/* n words at a (a colour cycle's strip). */
static bool cycle_at(HzEx *x, u32 a, int n, u16 *words)
{
    const u8 *p = ex_at(x, a, (u32)n * 2);
    if (p == NULL) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        words[i] = (u16)((p[i * 2] << 8) | p[i * 2 + 1]);
    }
    return true;
}

static bool pictures(HzEx *x)
{
    u16 photo[16], title[32], menu[64], winner[48], photo2[16];
    u16 sega_cycle[31], copy_cycle[23];
    if (!words_at(x, EXR_TankPhotoPalette, 1, photo)
        || !words_at(x, EXR_SegaLogoPalette, 1, title)
        || !words_at(x, EXR_CopyrightPalette, 1, title + 16)
        || !words_at(x, EXR_MenuPalette, 4, menu)
        || !words_at(x, EXR_WinnerPalette, 3, winner)
        || !words_at(x, EXR_WinnerPhotoPalette, 1, photo2)
        || !cycle_at(x, EXR_SegaLogoCycle, 31, sega_cycle)
        || !cycle_at(x, EXR_CopyrightCycle, 23, copy_cycle)) {
        return false;
    }
    u32 n0 = 0, n1 = 0, n2 = 0, n3 = 0, n4 = 0, n5 = 0;
    u8 *tank   = ex_rle(x, EXR_RLE_TankPhotos, 0, &n0);
    u8 *logo   = tank   ? ex_rle(x, EXR_RLE_MenuGfx, 2, &n1) : NULL;
    u8 *gfx    = logo   ? ex_rle(x, EXR_RLE_MenuGfx, 1, &n2) : NULL;
    u8 *font   = gfx    ? ex_rle(x, EXR_RLE_MenuGfx, 0, &n3) : NULL;
    u8 *wfont  = font   ? ex_rle(x, EXR_RLE_WinnerFont, 0, &n4) : NULL;
    u8 *tank2  = wfont  ? ex_rle(x, EXR_RLE_TankPhotos, 1, &n5) : NULL;
    bool ok = tank2 != NULL;

    ok = ok && ex_write_picture(x, "title/tank_photo.png", tank, n0, 40, 28,
                                photo, 16);
    if (ok) {
        if (n1 < 320 * 32) {
            ok = ex_fail(x, "the title logo is %u bytes", (unsigned)n1);
        } else {
            for (int i = 318 * 32; i < 319 * 32 && ok; i++) {
                if (logo[i] != 0) {
                    ok = ex_fail(x, "the title logo's tile 318 isn't blank");
                }
            }
        }
    }
    if (ok) {
        u16 pal[16];
        for (int i = 0; i < 16; i++) {
            pal[i] = (u16)((logo[319 * 32 + i * 2] << 8)
                           | logo[319 * 32 + i * 2 + 1]);
        }
        ok = ex_write_picture(x, "title/title_logo.png", logo, n1, 29, 11,
                              pal, 16);
    }
    if (ok && n2 < 48 * 32) {
        ok = ex_fail(x, "RLE_MenuGfx 1 is %u bytes", (unsigned)n2);
    }
    ok = ok
      && ex_write_sprite_sheet(x, "title", "sega_logo", gfx, 48 * 32, 0x480,
                               48, SEGA_GROUPS, COUNT(SEGA_GROUPS), title)
      && ex_write_sprite_sheet(x, "title", "copyright", gfx + 48 * 32,
                               n2 - 48 * 32, 0x4B0, 80, COPYRIGHT_GROUPS,
                               COUNT(COPYRIGHT_GROUPS), title + 16)
      && ex_write_tile_sheet(x, "title/menu_font.png", font, n3, 256, menu,
                             16, false)
      && ex_write_tile_sheet(x, "title/winner_font.png", wfont, n4, 256,
                             winner + 32, 16, false)
      && ex_write_picture(x, "title/tank_photo2.png", tank2, n5, 40, 28,
                          photo2, 16)
      && ex_write_palette(x, "palettes/menu.png", menu, 64, 16)
      && ex_write_palette(x, "palettes/winner.png", winner, 48, 16)
      && ex_write_palette(x, "palettes/title.png", title, 32, 16)
      && ex_write_palette(x, "palettes/sega_logo_cycle.png", sega_cycle, 31,
                          31)
      && ex_write_palette(x, "palettes/copyright_cycle.png", copy_cycle, 23,
                          23);
    free(tank);
    free(logo);
    free(gfx);
    free(font);
    free(wfont);
    free(tank2);
    return ok;
}

static bool menu_json(HzEx *x)
{
    const u8 *repeat = ex_at(x, EXR_MusicTestRepeat, 18);
    const u8 *names = ex_at(x, EXR_MapNames, 8 * 8);
    const u8 *sfx = ex_at(x, EXR_M_SfxCount, 1);
    if (repeat == NULL || names == NULL || sfx == NULL) {
        return false;
    }
    ExJson *songs = exj_array();
    for (int n = 0; n < 18; n++) {
        char lines[2][100];
        if (!ex_music_title(x, n, lines)) {
            exj_free(songs);
            return false;
        }
        if (repeat[n] != 0 && repeat[n] != 0xFF) {
            exj_free(songs);
            return ex_fail(x, "MusicTestRepeat %d: $%02X", n, repeat[n]);
        }
        ExJson *title = exj_array();
        exj_push(title, exj_str(lines[0]));
        exj_push(title, exj_str(lines[1]));
        ExJson *s = exj_object();
        exj_set(s, "song", exj_int(n));
        exj_set(s, "title", title);
        exj_set(s, "repeat", exj_bool(repeat[n] == 0xFF));
        exj_push(songs, s);
    }
    ExJson *maps = exj_array();
    for (int n = 0; n < 8; n++) {
        const u8 *b = names + n * 8;
        if (b[7] != 0) {
            exj_free(songs);
            exj_free(maps);
            return ex_fail(x, "MapNames %d: not 7 characters and a NUL", n);
        }
        char s[64];
        ex_menu_text(s, sizeof s, b, 7);
        exj_push(maps, exj_str(s));
    }
    ExJson *top = exj_object();
    exj_set(top, "songs", songs);
    exj_set(top, "map_names", maps);
    exj_set(top, "sfx_count", exj_int(sfx[0]));
    return ex_write_json(x, "data/menu.json", top);
}

/* extract_title. */
bool ex_title(HzEx *x)
{
    return pictures(x) && menu_json(x);
}
