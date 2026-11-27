#include "video.h"

#include <stdlib.h>
#include <string.h>

/* (Half B of step 1, docs/hackable.md) The layer's own store and its
 * compositor: what video.h says the game can ask for, kept as asked, and
 * the frame worked out from it. Nothing here is the VDP's: the checks
 * against the original see the cartridge's VRAM through a watcher
 * (tools/video_vdp.c), which the game never sets. */

/* What each layout puts where: the backdrop's colour, and the window
 * layer's place (shown instead of layer A). */
static int backdrop(const Video *v)
{
    return v->layout == VIDEO_MENUS ? 0 : 32;
}

static bool in_window(const Video *v, int x, int y)
{
    switch (v->layout) {
    case VIDEO_MENUS:
        return true;
    case VIDEO_GAME_SPLIT:
        return y < VIDEO_HUD_LINES || x >= VIDEO_SPLIT_COLS * TILE_W;
    default:
        return y < VIDEO_HUD_LINES;
    }
}

Cell cell_from_word(u16 w)
{
    Cell c;
    c.tile = (u16)(w & 0x07FF);
    c.flip_h = (w >> 11) & 1;
    c.flip_v = (w >> 12) & 1;
    c.pal = (w >> 13) & 3;
    c.prio = (w >> 15) & 1;
    return c;
}

u16 cell_word(Cell c)
{
    return (u16)((c.tile & 0x07FF) | (c.flip_h << 11) | (c.flip_v << 12)
                 | (c.pal << 13) | (c.prio << 15));
}

static void tell(const Video *v, const VideoChange *c)
{
    if (v->watch != NULL && v->watch->changed != NULL) {
        v->watch->changed(v->watch->user, v, c);
    }
}

void video_init(Video *v)
{
    Sprite *behind = v->behind;
    u32 cap = v->behind_cap;
    memset(v, 0, sizeof *v);
    v->behind = behind;
    v->behind_cap = cap;
    v->layout = VIDEO_GAME_SPLIT;
    v->back_at = -1;
}

void video_watch(Video *v, const VideoWatch *w)
{
    v->watch = w;
}

static void sprites_clear(Video *v)
{
    v->sprite_count = 0;
    v->game_at = 0;
    v->held = false;
    v->back_at = -1;
    v->behind_count = 0;
}

void video_start_game(Video *v, bool split)
{
    v->layout = split ? VIDEO_GAME_SPLIT : VIDEO_GAME_WIDE;
    v->display = false;
    memset(v->tiles, 0, sizeof v->tiles);
    memset(v->cells, 0, sizeof v->cells);
    memset(v->scroll_x, 0, sizeof v->scroll_x);
    memset(v->scroll_y, 0, sizeof v->scroll_y);
    memset(v->colours, 0, sizeof v->colours);
    sprites_clear(v);
    tell(v, &(VideoChange){ .what = VIDEO_DID_START_GAME, .flag = split });
}

void video_start_menus(Video *v)
{
    v->layout = VIDEO_MENUS;
    v->display = false;
    sprites_clear(v);
    tell(v, &(VideoChange){ .what = VIDEO_DID_START_MENUS });
}

void video_display(Video *v, bool on)
{
    v->display = on;
    tell(v, &(VideoChange){ .what = VIDEO_DID_DISPLAY, .flag = on });
}

void video_load_tiles(Video *v, u16 first, const u8 *packed, u32 count)
{
    for (u32 i = 0; i < count; i++) {
        memcpy(v->tiles[(first + i) & (VIDEO_TILES - 1)],
               packed + i * TILE_BYTES, TILE_BYTES);
    }
    tell(v, &(VideoChange){ .what = VIDEO_DID_TILES, .first = first,
                            .count = count, .packed = packed });
}

void video_clear_tiles(Video *v, u16 first, u32 count)
{
    for (u32 i = 0; i < count; i++) {
        memset(v->tiles[(first + i) & (VIDEO_TILES - 1)], 0, TILE_BYTES);
    }
    tell(v, &(VideoChange){ .what = VIDEO_DID_CLEAR_TILES, .first = first,
                            .count = count });
}

void video_copy_tile(Video *v, u16 to, u16 from)
{
    memmove(v->tiles[to & (VIDEO_TILES - 1)],
            v->tiles[from & (VIDEO_TILES - 1)], TILE_BYTES);
    tell(v, &(VideoChange){ .what = VIDEO_DID_COPY_TILE, .first = to,
                            .from = from });
}

void video_set_word(Video *v, LayerId layer, int col, int row, u16 word)
{
    col &= PLANE_COLS - 1;
    row &= PLANE_ROWS - 1;
    v->cells[layer][row][col] = word;
    tell(v, &(VideoChange){ .what = VIDEO_DID_CELL, .layer = layer,
                            .col = col, .row = row, .word = word });
}

void video_set_tile(Video *v, LayerId layer, int col, int row, Cell cell)
{
    video_set_word(v, layer, col, row, cell_word(cell));
}

Cell video_cell(const Video *v, LayerId layer, int col, int row)
{
    return cell_from_word(
        v->cells[layer][row & (PLANE_ROWS - 1)][col & (PLANE_COLS - 1)]);
}

void video_fill_rows(Video *v, LayerId layer, int row, int rows, Cell cell)
{
    u16 w = cell_word(cell);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < PLANE_COLS; c++) {
            video_set_word(v, layer, c, row + r, w);
        }
    }
}

void video_fill(Video *v, LayerId layer, Cell cell)
{
    video_fill_rows(v, layer, 0, PLANE_ROWS, cell);
}

void video_scroll(Video *v, LayerId layer, int sx, int sy)
{
    if (layer != LAYER_A && layer != LAYER_B) {
        return;
    }
    v->scroll_x[layer] = (s16)sx;
    v->scroll_y[layer] = (s16)sy;
    tell(v, &(VideoChange){ .what = VIDEO_DID_SCROLL, .layer = layer,
                            .x = sx, .y = sy });
}

void video_set_colour(Video *v, int index, Cram colour)
{
    index &= VIDEO_COLOURS - 1;
    colour &= 0x0EEE;
    v->colours[index] = colour;
    tell(v, &(VideoChange){ .what = VIDEO_DID_COLOUR, .index = index,
                            .colour = colour });
}

void video_set_colours(Video *v, int first, const Cram *colours, int n)
{
    for (int i = 0; i < n; i++) {
        video_set_colour(v, first + i, colours[i]);
    }
}

Cram video_colour(const Video *v, int index)
{
    return v->colours[index & (VIDEO_COLOURS - 1)];
}

/* -- sprites ------------------------------------------------------------ */

void video_sprites_begin(Video *v)
{
    v->sprite_count = 0;
    v->game_at = 0;
    v->behind_count = 0;
}

bool video_sprite_push(Video *v, const Sprite *s)
{
    if (v->sprite_count >= VIDEO_SPRITES) {
        return false;
    }
    v->sprite[v->sprite_count++] = *s;
    return true;
}

void video_sprites_game(Video *v)
{
    v->game_at = v->sprite_count;
}

void video_sprites_hold(Video *v, bool held)
{
    v->held = held;
    v->back_at = -1;
}

void video_sprites_back(Video *v, int line)
{
    v->back_at = line;
}

void video_frame_done(Video *v)
{
    if (v->back_at >= 0) {
        v->held = false;
        v->back_at = -1;
    }
}

void video_sprites_behind(Video *v, const Sprite *s, u32 n)
{
    if (n > v->behind_cap) {
        u32 cap = v->behind_cap ? v->behind_cap : 1024;
        while (cap < n) {
            cap *= 2;
        }
        Sprite *b = realloc(v->behind, cap * sizeof *b);
        if (b == NULL) {
            v->behind_count = 0;            /* out of memory: none drawn */
            return;
        }
        v->behind = b;
        v->behind_cap = cap;
    }
    if (n != 0) {
        memcpy(v->behind, s, n * sizeof *s);
    }
    v->behind_count = n;
}

void video_sprite_colours(Video *v, int n, const u8 map[16])
{
    if (n < 1 || n > 15) {
        return;
    }
    if (map == NULL) {
        v->sprite_colours_on &= (u16)~(1u << n);
        return;
    }
    memcpy(v->sprite_colours[n], map, 16);
    v->sprite_colours_on |= (u16)(1u << n);
}

/* -- the compositor ----------------------------------------------------- */

/* A pixel of a layer or of the sprites: its index (0: nothing) and
 * priority. */
typedef struct {
    u8   idx;
    bool prio;
} Px;

/* Pixel (tx, ty) of tile `tile`: 0..15. */
static u8 tile_px(const Video *v, u32 tile, int tx, int ty)
{
    u8 b = v->tiles[tile & (VIDEO_TILES - 1)][ty * 4 + (tx >> 1)];
    return (tx & 1) ? (u8)(b & 0x0F) : (u8)(b >> 4);
}

/* A layer's pixel (px, py) in its own 512 x 256. */
static Px layer_px(const Video *v, LayerId layer, int px, int py)
{
    Cell c = cell_from_word(v->cells[layer][py >> 3][px >> 3]);
    int tx = px & 7;
    int ty = py & 7;
    if (c.flip_h) {
        tx = 7 - tx;
    }
    if (c.flip_v) {
        ty = 7 - ty;
    }
    u8 n = tile_px(v, c.tile, tx, ty);
    Px out;
    out.idx = n ? (u8)(c.pal * 16 + n) : 0;
    out.prio = c.prio;
    return out;
}

static Px scrolled_px(const Video *v, LayerId layer, int x, int y)
{
    return layer_px(v, layer, (x + v->scroll_x[layer]) & (PLANE_COLS * 8 - 1),
                    (y + v->scroll_y[layer]) & (PLANE_ROWS * 8 - 1));
}

/* One sprite's pixels on line y (`sy` lines into it), the first `draw` of
 * its columns, where no sprite before it has put one. */
static void sprite_line(const Video *v, Px spr[SCREEN_W], const Sprite *s,
                        int sy, int draw)
{
    int wpx = s->w * TILE_W;
    const u8 *map = (s->colours != 0
                     && ((v->sprite_colours_on >> s->colours) & 1))
                        ? v->sprite_colours[s->colours] : NULL;
    if (s->flip_v) {
        sy = s->h * TILE_H - 1 - sy;
    }
    for (int dx = 0; dx < draw; dx++) {
        int x = s->x + dx;
        if (x < 0 || x >= SCREEN_W || spr[x].idx != 0) {
            continue;
        }
        int sx = s->flip_h ? wpx - 1 - dx : dx;
        u32 tile = (u32)s->tile + (u32)(sx >> 3) * s->h + (u32)(sy >> 3);
        u8 c = tile_px(v, tile, sx & 7, sy & 7);
        if (c && map != NULL) {
            c = map[c];
        }
        if (c) {
            spr[x].idx = (u8)(s->pal * 16 + c);
            spr[x].prio = s->prio;
        }
    }
}

static bool on_line(const Sprite *s, int y)
{
    return y >= s->y && y < s->y + s->h * TILE_H;
}

/* The sprites' pixels on line y: the list's, first on top, by the game's
 * rule (VIDEO_LINE_SPRITES of them on a line and VIDEO_LINE_DOTS of their
 * pixels; and the cartridge's mask: a sprite at x -128 ends the line once
 * a sprite elsewhere has been drawn on it); then the units behind, where
 * the game's sprites are drawn and no mask ended the line. */
static void line_sprites(const Video *v, int y, Px spr[SCREEN_W])
{
    memset(spr, 0, sizeof(Px) * SCREEN_W);

    bool game = !v->held || (v->back_at >= 0 && y >= v->back_at);
    int  last = game ? v->sprite_count
                     : (v->game_at < v->sprite_count ? v->game_at
                                                     : v->sprite_count);
    const Sprite *list[VIDEO_LINE_SPRITES];
    int count = 0;
    for (int i = 0; i < last; i++) {
        if (on_line(&v->sprite[i], y)) {
            if (count == VIDEO_LINE_SPRITES) {
                break;
            }
            list[count++] = &v->sprite[i];
        }
    }

    bool behind = game;
    int  dots = VIDEO_LINE_DOTS;
    bool seen = false;
    for (int i = 0; i < count; i++) {
        const Sprite *s = list[i];
        if (s->x == -128) {
            if (seen) {
                behind = false;
                break;
            }
        } else {
            seen = true;
        }
        int wpx = s->w * TILE_W;
        if (dots <= 0) {
            break;
        }
        int draw = wpx > dots ? dots : wpx;
        dots -= wpx;
        sprite_line(v, spr, s, y - s->y, draw);
    }

    if (!behind) {
        return;
    }
    for (u32 i = 0; i < v->behind_count; i++) {
        const Sprite *s = &v->behind[i];
        if (on_line(s, y)) {
            sprite_line(v, spr, s, y - s->y, s->w * TILE_W);
        }
    }
}

void video_compose(const Video *v, u8 *out)
{
    u8 back = (u8)backdrop(v);
    for (int y = 0; y < SCREEN_H; y++) {
        u8 *row = out + y * SCREEN_W;
        if (!v->display) {
            memset(row, back, SCREEN_W);
            continue;
        }

        Px spr[SCREEN_W];
        line_sprites(v, y, spr);

        for (int x = 0; x < SCREEN_W; x++) {
            Px a = in_window(v, x, y) ? layer_px(v, LAYER_W, x, y)
                                      : scrolled_px(v, LAYER_A, x, y);
            Px b = scrolled_px(v, LAYER_B, x, y);
            Px s = spr[x];

            /* Back to front: B low, A low, sprites low, B high, A high,
             * sprites high. */
            u8 px = back;
            if (s.idx && s.prio) {
                px = s.idx;
            } else if (a.idx && a.prio) {
                px = a.idx;
            } else if (b.idx && b.prio) {
                px = b.idx;
            } else if (s.idx) {
                px = s.idx;
            } else if (a.idx) {
                px = a.idx;
            } else if (b.idx) {
                px = b.idx;
            }
            row[x] = px;
        }
    }
}
