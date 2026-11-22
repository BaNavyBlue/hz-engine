/* (Session 78) hz_extract.py's pictures in C (ex.h): put_tile, the tile
 * sheets, write_sprite_sheet with its sheet_layout and picture_tile, the
 * palette strips. The PNGs are the pictures Pillow saves (pngwrite.h). */
#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOOSE_ACROSS 16         /* single tiles, as a plain sheet has them */
#define SHEET_WIDTH  256        /* pixels: the widest a band of pictures runs */

bool exc_new(HzEx *x, ExCanvas *c, int w, int h)
{
    c->w  = w;
    c->h  = h;
    c->px = calloc((size_t)w * (size_t)h, 1);
    if (c->px == NULL) {
        return ex_fail(x, "out of memory");
    }
    return true;
}

void exc_free(ExCanvas *c)
{
    free(c->px);
    c->px = NULL;
}

/* put_tile (tile_pixels: rows top to bottom, high nibble first). */
bool exc_tile(HzEx *x, ExCanvas *c, const u8 *tiles, u32 len, u32 t,
              int px, int py, bool hflip, bool vflip)
{
    if ((u64)t * 32 + 32 > len) {
        return ex_fail(x, "tile %u is past its tiles' end", (unsigned)t);
    }
    if (px < 0 || py < 0 || px + 8 > c->w || py + 8 > c->h) {
        return ex_fail(x, "a tile drawn at (%d, %d) is off its picture", px,
                       py);
    }
    const u8 *p = tiles + (size_t)t * 32;
    for (int ty = 0; ty < 8; ty++) {
        for (int tx = 0; tx < 8; tx++) {
            int sx = hflip ? 7 - tx : tx;
            int sy = vflip ? 7 - ty : ty;
            u8 b = p[sy * 4 + sx / 2];
            c->px[(size_t)(py + ty) * (size_t)c->w + (size_t)(px + tx)] =
                (sx & 1) ? (b & 15) : (b >> 4);
        }
    }
    return true;
}

bool exc_save(HzEx *x, const char *rel, const ExCanvas *c, const u16 *pal,
              int n, bool transparent)
{
    u8 rgb[256 * 3];
    if (n < 1 || n > 256) {
        return ex_fail(x, "%s: a palette of %d colours", rel, n);
    }
    ex_rgb_list(pal, n, rgb);
    return ex_write_png(x, rel, c->w, c->h, c->px, rgb, n, transparent);
}

/* write_palette. */
bool ex_write_palette(HzEx *x, const char *rel, const u16 *words, int n,
                      int width)
{
    for (int i = 0; i < n; i++) {
        if (words[i] & ~0x0EEE) {
            return ex_fail(x, "%s: CRAM word $%04X has bits a colour can't "
                           "hold", rel, (unsigned)words[i]);
        }
    }
    ExCanvas c;
    if (!exc_new(x, &c, width, n / width)) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        c.px[i] = (u8)i;
    }
    bool ok = exc_save(x, rel, &c, words, n, false);
    exc_free(&c);
    return ok;
}

/* write_tile_sheet. */
bool ex_write_tile_sheet(HzEx *x, const char *rel, const u8 *tiles, u32 len,
                         int count, const u16 *pal, int n, bool transparent)
{
    ExCanvas c;
    if (!exc_new(x, &c, 128, (count + 15) / 16 * 8)) {
        return false;
    }
    bool ok = true;
    for (int t = 0; t < count && ok; t++) {
        ok = exc_tile(x, &c, tiles, len, (u32)t, (t % 16) * 8, (t / 16) * 8,
                      false, false);
    }
    ok = ok && exc_save(x, rel, &c, pal, n, transparent);
    exc_free(&c);
    return ok;
}

/* write_picture. */
bool ex_write_picture(HzEx *x, const char *rel, const u8 *tiles, u32 len,
                      int across, int rows, const u16 *pal, int n)
{
    ExCanvas c;
    if (!exc_new(x, &c, across * 8, rows * 8)) {
        return false;
    }
    bool ok = true;
    for (int t = 0; t < across * rows && ok; t++) {
        ok = exc_tile(x, &c, tiles, len, (u32)t, (t % across) * 8,
                      (t / across) * 8, false, false);
    }
    ok = ok && exc_save(x, rel, &c, pal, n, false);
    exc_free(&c);
    return ok;
}

/* ---- sprite sheets ---- */

/* sheet_layout's item: a group, or a gap's single tiles. */
typedef struct {
    char name[160];
    int  tile, w, h, n;
    bool rows;
    int  across, y;
} Item;

/* loose_name. */
static void loose_name(char *out, size_t n, int first, int last)
{
    if (first == last) {
        snprintf(out, n, "tile $%X, not identified", first);
    } else {
        snprintf(out, n, "tiles $%X-$%X, not identified", first, last);
    }
}

static int by_tile(const void *a, const void *b)
{
    const ExGroup *ga = *(const ExGroup *const *)a;
    const ExGroup *gb = *(const ExGroup *const *)b;
    return (ga->tile > gb->tile) - (ga->tile < gb->tile);
}

/* picture_tile: tile k of picture i, and where it sits in the picture. */
static int picture_tile(const Item *g, int i, int k, int *tx, int *ty)
{
    if (g->rows) {
        *tx = k % g->w;
        *ty = k / g->w;
    } else {
        *tx = k / g->h;
        *ty = k % g->h;
    }
    return g->tile + i * g->w * g->h + k;
}

bool ex_write_sprite_sheet(HzEx *x, const char *folder, const char *name,
                           const u8 *tiles, u32 len, int first, int count,
                           const ExGroup *groups, int ngroups,
                           const u16 *pal16)
{
    const ExGroup *sorted[64];
    if (ngroups > 64) {
        return ex_fail(x, "%s: too many groups", name);
    }
    for (int i = 0; i < ngroups; i++) {
        sorted[i] = &groups[i];
    }
    qsort(sorted, (size_t)ngroups, sizeof sorted[0], by_tile);

    Item items[130];
    int ni = 0;
    int t = first;
    for (int i = 0; i < ngroups; i++) {
        const ExGroup *g = sorted[i];
        if (g->tile < t) {
            return ex_fail(x, "sheet groups overlap at $%X", g->tile);
        }
        if (g->tile > t) {
            Item *it = &items[ni++];
            loose_name(it->name, sizeof it->name, t, g->tile - 1);
            it->tile = t;
            it->w = it->h = 1;
            it->n = g->tile - t;
            it->rows = true;
        }
        Item *it = &items[ni++];
        snprintf(it->name, sizeof it->name, "%s", g->name);
        it->tile = g->tile;
        it->w = g->w;
        it->h = g->h;
        it->n = g->n;
        it->rows = g->rows;
        t = g->tile + g->w * g->h * g->n;
    }
    if (t > first + count) {
        return ex_fail(x, "sheet groups run past the region");
    }
    if (t < first + count) {
        Item *it = &items[ni++];
        loose_name(it->name, sizeof it->name, t, first + count - 1);
        it->tile = t;
        it->w = it->h = 1;
        it->n = first + count - t;
        it->rows = true;
    }
    int y = 0, width = 0;
    ExJson *list = exj_array();
    for (int i = 0; i < ni; i++) {
        Item *it = &items[i];
        int across = (it->w == 1 && it->h == 1) ? LOOSE_ACROSS
                   : SHEET_WIDTH / (it->w * 8);
        if (across > it->n) {
            across = it->n;
        }
        it->across = across;
        it->y = y;
        int rows = (it->n + across - 1) / across;
        ExJson *g = exj_object();
        exj_set(g, "name", exj_str(it->name));
        exj_set(g, "tile", exj_strf("$%X", it->tile));
        exj_set(g, "w", exj_int(it->w));
        exj_set(g, "h", exj_int(it->h));
        exj_set(g, "count", exj_int(it->n));
        exj_set(g, "across", exj_int(across));
        exj_set(g, "x", exj_int(0));
        exj_set(g, "y", exj_int(y));
        if (it->rows && !(it->w == 1 || it->h == 1)) {
            exj_set(g, "order", exj_str("rows"));
        }
        exj_push(list, g);
        if (across * it->w * 8 > width) {
            width = across * it->w * 8;
        }
        y += rows * it->h * 8;
    }

    ExCanvas c;
    if (!exc_new(x, &c, width, y)) {
        exj_free(list);
        return false;
    }
    bool ok = true;
    for (int i = 0; i < ni && ok; i++) {
        const Item *g = &items[i];
        for (int p = 0; p < g->n && ok; p++) {
            int fx = (p % g->across) * g->w * 8;
            int fy = g->y + (p / g->across) * g->h * 8;
            for (int k = 0; k < g->w * g->h && ok; k++) {
                int tx, ty;
                int tile = picture_tile(g, p, k, &tx, &ty);
                ok = exc_tile(x, &c, tiles, len, (u32)(tile - first),
                              fx + tx * 8, fy + ty * 8, false, false);
            }
        }
    }
    char rel[EX_PATH];
    snprintf(rel, sizeof rel, "%s/%s.png", folder, name);
    ok = ok && exc_save(x, rel, &c, pal16, 16, true);
    exc_free(&c);
    if (!ok) {
        exj_free(list);
        return false;
    }
    ExJson *sheet = exj_object();
    exj_set(sheet, "image", exj_strf("%s.png", name));
    exj_set(sheet, "first_tile", exj_strf("$%X", first));
    exj_set(sheet, "tiles", exj_int(count));
    exj_set(sheet, "groups", list);
    snprintf(rel, sizeof rel, "%s/%s.json", folder, name);
    return ex_write_json(x, rel, sheet);
}
