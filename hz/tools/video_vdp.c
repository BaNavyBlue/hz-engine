#include "video_vdp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* video_vdp.h says what this is for. The layout is half A's video.c. */

typedef struct {
    u16 cells[LAYER_COUNT];     /* B, A, W */
    u16 hscroll;
} Bases;

static const Bases GAME_BASES  = { { 0xE000, 0xC000, 0xF000 }, 0xD400 };
static const Bases MENUS_BASES = { { 0xE000, 0xE000, 0xF000 }, 0xDC00 };

static const Bases *bases(const VideoVdp *w)
{
    return w->layout == VIDEO_MENUS ? &MENUS_BASES : &GAME_BASES;
}

/* VDPInitRegs ($9DE), registers 0-18; and M_VDPInitRegs ($565A4). */
static const u8 GAME_REGS[19] = {
    0x14, 0x24, 0x30, 0x3C, 0x07, 0x68, 0x00, 0x20, 0x00, 0x00,
    0x1E, 0x00, 0x81, 0x35, 0x00, 0x02, 0x01, 0x89, 0x04
};
static const u8 MENUS_REGS[19] = {
    0x04, 0x24, 0x38, 0x3C, 0x07, 0x6C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x81, 0x37, 0x00, 0x02, 0x01, 0x00, 0x80
};

/* The sprite table register 5 puts in use: the game's or the menus'
 * (none for any other value: the power-on 0). */
static int table_at(const Vdp *d)
{
    switch (d->reg[5]) {
    case 0x68:
        return VRAM_SPRITES;
    case 0x6C:
        return VRAM_MENU_SPRITES;
    default:
        return -1;
    }
}

static bool in_table(const Vdp *d, u32 addr)
{
    int at = table_at(d);
    return at >= 0 && addr >= (u32)at && addr < (u32)at + VRAM_TABLE_BYTES;
}

/* Bytes written: a sprite table's while it is in use, else not. */
static void written(VideoVdp *w, u16 addr, u32 bytes)
{
    for (u32 i = 0; i < bytes; i++) {
        u16 a = (u16)(addr + i);
        w->held[a] = in_table(w->vdp, a);
    }
}

/* A register written; register 5 puts its table's bytes in use. */
static void reg(VideoVdp *w, u16 cmd)
{
    vdp_reg(w->vdp, cmd);
    if (((cmd >> 8) & 0x1F) == 5) {
        int at = table_at(w->vdp);
        if (at >= 0) {
            memset(w->held + at, 1, VRAM_TABLE_BYTES);
        }
    }
}

static void put_word(VideoVdp *w, u16 addr, u16 word)
{
    u8 b[2] = { (u8)(word >> 8), (u8)word };
    vdp_put_vram(w->vdp, addr, b, 2);
    written(w, addr, 2);
}

static void changed(void *user, const Video *v, const VideoChange *c)
{
    (void)v;
    VideoVdp *w = user;
    Vdp *d = w->vdp;
    switch (c->what) {
    case VIDEO_DID_START_GAME:
        w->layout = c->flag ? VIDEO_GAME_SPLIT : VIDEO_GAME_WIDE;
        for (int r = 0; r < 19; r++) {
            reg(w, (u16)(0x8000 | (r << 8) | GAME_REGS[r]));
        }
        if (!c->flag) {
            reg(w, 0x9194);             /* view B's side off the screen */
        }
        memset(d->cram, 0, sizeof d->cram);
        memset(d->vsram, 0, sizeof d->vsram);
        memset(d->vram, 0, sizeof d->vram);
        written(w, 0, VDP_VRAM_SIZE);
        reg(w, 0x8124);
        break;
    case VIDEO_DID_START_MENUS:
        w->layout = VIDEO_MENUS;
        for (int r = 0; r < 19; r++) {
            reg(w, (u16)(0x8000 | (r << 8) | MENUS_REGS[r]));
        }
        reg(w, 0x8124);
        break;
    case VIDEO_DID_DISPLAY:
        reg(w, c->flag ? 0x8164 : 0x8124);
        break;
    case VIDEO_DID_TILES:
        for (u32 i = 0; i < c->count; i++) {
            u16 at = (u16)((c->first + i) * 32u);
            vdp_put_vram(d, at, c->packed + i * 32u, 32);
            written(w, at, 32);
        }
        break;
    case VIDEO_DID_CLEAR_TILES: {
        static const u8 CLEAR[32];
        for (u32 i = 0; i < c->count; i++) {
            u16 at = (u16)((c->first + i) * 32u);
            vdp_put_vram(d, at, CLEAR, 32);
            written(w, at, 32);
        }
        break;
    }
    case VIDEO_DID_COPY_TILE:
        vdp_copy_vram(d, (u16)(c->first * 32u), (u16)(c->from * 32u), 32);
        written(w, (u16)(c->first * 32u), 32);
        break;
    case VIDEO_DID_CELL:
        put_word(w, (u16)(bases(w)->cells[c->layer]
                          + ((c->row & (PLANE_ROWS - 1)) << 7)
                          + ((c->col & (PLANE_COLS - 1)) << 1)), c->word);
        break;
    case VIDEO_DID_SCROLL: {
        int k = (c->layer == LAYER_A) ? 0 : 1;
        put_word(w, (u16)(bases(w)->hscroll + k * 2), (u16)-c->x);
        d->vsram[k] = (u16)(c->y & 0x07FF);
        break;
    }
    case VIDEO_DID_COLOUR:
        d->cram[c->index & (VDP_CRAM_WORDS - 1)] = (u16)(c->colour & 0x0EEE);
        break;
    }
}

void video_vdp_watch(VideoVdp *w, Video *video, Vdp *vdp)
{
    w->vdp = vdp;
    w->layout = video->layout;
    memset(w->held, 0, sizeof w->held);
    int at = table_at(vdp);
    if (at >= 0) {
        memset(w->held + at, 1, VRAM_TABLE_BYTES);
    }
    w->watch.changed = changed;
    w->watch.user = w;
    video_watch(video, &w->watch);
}

u32 video_vdp_crc32(const u8 *p, size_t n)
{
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

void video_vdp_vram(const VideoVdp *w, u8 out[VDP_VRAM_SIZE])
{
    for (u32 i = 0; i < VDP_VRAM_SIZE; i++) {
        out[i] = w->held[i] ? 0 : w->vdp->vram[i];
    }
}

u32 video_vdp_vram_crc(const VideoVdp *w)
{
    static u8 vram[VDP_VRAM_SIZE];
    video_vdp_vram(w, vram);
    return video_vdp_crc32(vram, VDP_VRAM_SIZE);
}

/* The first line sprite i of the list can be drawn on; -1: none. */
static int first_line(const Video *v, int i)
{
    if (i < v->game_at || !v->held) {
        return 0;
    }
    return v->back_at;
}

static bool shown(const Sprite *s, int from)
{
    return from >= 0 && s->y + s->h * TILE_H > from && s->y < SCREEN_H;
}

static u16 attr(const Sprite *s)
{
    return (u16)((s->tile & 0x07FF) | (s->flip_h << 11) | (s->flip_v << 12)
                 | (s->pal << 13) | (s->prio << 15));
}

/* (Session 81) The sprites drawn, as the cartridge's side lists them:
 * the split game's divider as its six 4 x 4 sprites (0-5 on the
 * cartridge, x 144, y 32 on, from line 0: before the HUD strip's cut),
 * then the layer's list. The k'th of drawn_count's; its first line in
 * *from. */
static int divider_sprites(const Video *v)
{
    return (v->divider && v->layout == VIDEO_GAME_SPLIT) ? 6 : 0;
}

static int drawn_count(const Video *v)
{
    return divider_sprites(v) + v->sprite_count;
}

static Sprite drawn_at(const Video *v, int k, int *from)
{
    int d = divider_sprites(v);
    if (k < d) {
        Cell c = v->divider_cell;
        *from = 0;
        return (Sprite){
            .x = VIDEO_DIVIDER_X,
            .y = (s16)(VIDEO_HUD_LINES
                       + k * VIDEO_DIVIDER_TILES * TILE_H),
            .tile = c.tile, .w = VIDEO_DIVIDER_TILES,
            .h = VIDEO_DIVIDER_TILES, .pal = c.pal, .prio = c.prio,
            .flip_h = c.flip_h, .flip_v = c.flip_v
        };
    }
    *from = first_line(v, k - d);
    return v->sprite[k - d];
}

u32 video_sprites_crc(const Video *v)
{
    int count = drawn_count(v);
    u8 *b = malloc((size_t)(count > 0 ? count : 1) * 11);
    if (b == NULL) {
        return 0;
    }
    size_t n = 0;
    for (int k = 0; k < count; k++) {
        int from;
        Sprite d = drawn_at(v, k, &from);
        const Sprite *s = &d;
        if (!shown(s, from)) {
            continue;
        }
        u16 a = attr(s);
        b[n++] = (u8)((u16)s->x >> 8);
        b[n++] = (u8)s->x;
        b[n++] = (u8)((u16)s->y >> 8);
        b[n++] = (u8)s->y;
        b[n++] = s->w;
        b[n++] = s->h;
        b[n++] = (u8)(a >> 8);
        b[n++] = (u8)a;
        b[n++] = s->colours;
        b[n++] = (u8)(from >> 8);
        b[n++] = (u8)from;
    }
    u32 crc = video_vdp_crc32(b, n);
    free(b);
    return crc;
}

void video_sprites_print(const Video *v, FILE *f)
{
    for (int k = 0; k < drawn_count(v); k++) {
        int from;
        Sprite d = drawn_at(v, k, &from);
        const Sprite *s = &d;
        if (shown(s, from)) {
            fprintf(f, "%d %d %dx%d %04X %d from %d\n", s->x, s->y, s->w,
                    s->h, attr(s), s->colours, from);
        }
    }
}

void video_vdp_frame_line(const VideoVdp *w, const Video *v, FILE *f)
{
    const Vdp *d = w->vdp;
    u8 vs[VDP_VSRAM_WORDS * 2];
    for (int i = 0; i < VDP_VSRAM_WORDS; i++) {
        vs[i * 2] = (u8)(d->vsram[i] >> 8);
        vs[i * 2 + 1] = (u8)d->vsram[i];
    }
    fprintf(f, "%08X ", (unsigned)video_vdp_vram_crc(w));
    for (int i = 0; i < VDP_CRAM_WORDS; i++) {
        fprintf(f, "%03X", d->cram[i] & 0x0EEE);
    }
    fprintf(f, " %08X ", (unsigned)video_vdp_crc32(vs, sizeof vs));
    for (int i = 0; i < VDP_REG_COUNT; i++) {
        fprintf(f, "%02X", d->reg[i]);
    }
    fprintf(f, " %08X", (unsigned)video_sprites_crc(v));
}
