#include "hz_vdp.h"

#include <stdlib.h>

#include <string.h>

/* Measured 8-bit DAC output levels for a VA4 in normal intensity, from the
 * PS2 port (palette.c). The ladder is non-linear; an even ramp shifts
 * every colour. */
static const u8 DAC_LADDER[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };

void vdp_reset(Vdp *v)
{
    VdpSprite *more = v->more;
    u32 cap = v->more_cap;
    memset(v, 0, sizeof *v);
    v->target = VDP_VRAM;
    v->sprite_limits = true;
    v->more = more;
    v->more_cap = cap;
}

void vdp_copy(Vdp *dst, const Vdp *src)
{
    VdpSprite *more = dst->more;
    u32 cap = dst->more_cap;
    memcpy(dst, src, sizeof *dst);
    dst->more = more;
    dst->more_cap = cap;
    dst->more_count = 0;
    vdp_set_more(dst, src->more, src->more_count, src->more_at);
}

void vdp_set_more(Vdp *v, const VdpSprite *s, u32 n, int at)
{
    if (n > v->more_cap) {
        u32 cap = v->more_cap ? v->more_cap : 1024;
        while (cap < n) {
            cap *= 2;
        }
        VdpSprite *more = realloc(v->more, cap * sizeof *more);
        if (more == NULL) {
            v->more_count = 0;          /* out of memory: none drawn */
            return;
        }
        v->more = more;
        v->more_cap = cap;
    }
    if (n != 0) {
        memcpy(v->more, s, n * sizeof *s);
    }
    v->more_count = n;
    v->more_at = at;
}

void vdp_set_colours(Vdp *v, int n, const u8 map[16])
{
    if (n < 1 || n > 15) {
        return;
    }
    if (map == NULL) {
        v->colours_on &= (u16)~(1u << n);
        return;
    }
    memcpy(v->colours[n], map, 16);
    v->colours_on |= (u16)(1u << n);
}

void vdp_reg(Vdp *v, u16 cmd)
{
    u8 r = (u8)((cmd >> 8) & 0x1F);
    if (r < VDP_REG_COUNT) {
        v->reg[r] = (u8)cmd;
    }
}

void vdp_set_write(Vdp *v, VdpTarget target, u16 addr)
{
    v->target = target;
    v->addr = addr;
}

void vdp_command(Vdp *v, u32 cmd)
{
    /* CD1-0 in bits 31-30, CD5-2 in bits 7-4; A13-0 in bits 29-16, A15-14
     * in bits 1-0. Write codes: 1 VRAM, 3 CRAM, 5 VSRAM. */
    u8  cd = (u8)(((cmd >> 30) & 3) | ((cmd >> 2) & 0x3C));
    u16 addr = (u16)(((cmd >> 16) & 0x3FFF) | ((cmd & 3) << 14));

    switch (cd & 0x0F) {
    case 3:
        vdp_set_write(v, VDP_CRAM, addr);
        break;
    case 5:
        vdp_set_write(v, VDP_VSRAM, addr);
        break;
    default:
        vdp_set_write(v, VDP_VRAM, addr);
        break;
    }
}

static void store(Vdp *v, VdpTarget target, u16 addr, u16 value)
{
    switch (target) {
    case VDP_CRAM:
        v->cram[(addr >> 1) & (VDP_CRAM_WORDS - 1)] = (u16)(value & 0x0EEE);
        break;
    case VDP_VSRAM: {
        unsigned i = (addr >> 1) & 0x3F;
        if (i < VDP_VSRAM_WORDS) {
            v->vsram[i] = (u16)(value & 0x07FF);
        }
        break;
    }
    default:
        /* An odd address writes the word byte-swapped to the even one. */
        if (addr & 1) {
            value = (u16)((value << 8) | (value >> 8));
        }
        addr &= 0xFFFE;
        v->vram[addr] = (u8)(value >> 8);
        v->vram[addr + 1] = (u8)value;
        break;
    }
}

void vdp_write16(Vdp *v, u16 value)
{
    store(v, v->target, v->addr, value);
    v->addr = (u16)(v->addr + v->reg[15]);
}

void vdp_write32(Vdp *v, u32 value)
{
    vdp_write16(v, (u16)(value >> 16));
    vdp_write16(v, (u16)value);
}

void vdp_write_bytes(Vdp *v, const u8 *src, u32 bytes)
{
    for (u32 i = 0; i + 1 < bytes; i += 2) {
        vdp_write16(v, (u16)((src[i] << 8) | src[i + 1]));
    }
}

void vdp_put_vram(Vdp *v, u16 addr, const u8 *src, u32 bytes)
{
    for (u32 i = 0; i + 1 < bytes; i += 2) {
        store(v, VDP_VRAM, (u16)(addr + i),
              (u16)((src[i] << 8) | src[i + 1]));
    }
}

void vdp_copy_vram(Vdp *v, u16 dst, u16 src, u32 bytes)
{
    for (u32 i = 0; i < bytes; i++) {
        v->vram[(u16)(dst + i)] = v->vram[(u16)(src + i)];
    }
}

void vdp_vram_fill(Vdp *v, u16 addr, u32 count, u8 value)
{
    for (u32 i = 0; i < count; i++) {
        v->vram[addr] = value;
        addr = (u16)(addr + (v->reg[15] ? v->reg[15] : 1));
    }
}

void vdp_vram_copy(Vdp *v)
{
    u32 length = (u32)v->reg[19] | ((u32)v->reg[20] << 8);
    if (length == 0) {
        length = 0x10000;
    }
    u16 src = (u16)(v->reg[21] | (v->reg[22] << 8));
    for (u32 i = 0; i < length; i++) {
        v->vram[v->addr] = v->vram[src];
        src = (u16)(src + 1);
        v->addr = (u16)(v->addr + v->reg[15]);
    }
}

u16 vdp_vram_word(const Vdp *v, u16 addr)
{
    addr &= 0xFFFE;
    return (u16)((v->vram[addr] << 8) | v->vram[addr + 1]);
}

bool vdp_raster_write(Vdp *v, int line, VdpTarget target, u16 addr,
                      u16 value)
{
    if (v->raster_count >= VDP_RASTER_MAX) {
        return false;
    }
    VdpRasterWrite *w = &v->raster[v->raster_count++];
    w->line = line;
    w->target = target;
    w->addr = addr;
    w->value = value;
    return true;
}

void vdp_cram_rgb(u16 cram, u8 rgb[3])
{
    rgb[0] = DAC_LADDER[(cram >> 1) & 7];
    rgb[1] = DAC_LADDER[(cram >> 5) & 7];
    rgb[2] = DAC_LADDER[(cram >> 9) & 7];
}

u16 vdp_rgb_cram(const u8 rgb[3])
{
    static const int SHIFT[3] = { 1, 5, 9 };            /* R, G, B */
    u16 word = 0;
    for (int c = 0; c < 3; c++) {
        int best = 0;
        for (int level = 1; level < 8; level++) {
            int d = abs(rgb[c] - DAC_LADDER[level]);
            if (d < abs(rgb[c] - DAC_LADDER[best])) {
                best = level;
            }
        }
        word = (u16)(word | (best << SHIFT[c]));
    }
    return word;
}

/* -- rendering ------------------------------------------------------- */

/* One layer's pixel: palette index (0 = transparent) and priority. */
typedef struct {
    u8   idx;
    bool prio;
} Px;

/* Pixel (tx, ty) of a tile, flips applied: 0..15. */
static u8 tile_px(const Vdp *v, u16 tile, int tx, int ty, bool hf, bool vf)
{
    if (hf) {
        tx = 7 - tx;
    }
    if (vf) {
        ty = 7 - ty;
    }
    u8 b = v->vram[((u32)tile * 32 + (u32)ty * 4 + (u32)(tx >> 1)) & 0xFFFF];
    return (tx & 1) ? (u8)(b & 0x0F) : (u8)(b >> 4);
}

/* The pixel at (px, py) of a nametable `cols` cells wide at `base`. */
static Px nt_px(const Vdp *v, u16 base, int cols, int px, int py)
{
    u16 w = vdp_vram_word(v, (u16)(base + ((py >> 3) * cols + (px >> 3)) * 2));
    u8  c = tile_px(v, w & 0x07FF, px & 7, py & 7, (w & 0x0800) != 0,
                    (w & 0x1000) != 0);
    Px out;
    out.idx = c ? (u8)(((w >> 13) & 3) * 16 + c) : 0;
    out.prio = (w & 0x8000) != 0;
    return out;
}

static int plane_dim(u8 bits)
{
    switch (bits & 3) {
    case 1:
        return 64;
    case 3:
        return 128;
    default:
        return 32;
    }
}

/* Plane A (plane 0) or B (plane 1) at screen (x, y). */
static Px scroll_px(const Vdp *v, int plane, int x, int y)
{
    int cols = plane_dim(v->reg[16]);
    int rows = plane_dim((u8)(v->reg[16] >> 4));
    u16 base = plane ? (u16)((v->reg[4] & 0x07) << 13)
                     : (u16)((v->reg[2] & 0x38) << 10);
    u16 hs_base = (u16)((v->reg[13] & 0x3F) << 10);

    u16 hs_addr;
    switch (v->reg[11] & 3) {
    case 2:
        hs_addr = (u16)(hs_base + (y & ~7) * 4);   /* per cell */
        break;
    case 3:
        hs_addr = (u16)(hs_base + y * 4);          /* per line */
        break;
    default:
        hs_addr = hs_base;                         /* full screen */
        break;
    }
    int hs = vdp_vram_word(v, (u16)(hs_addr + plane * 2)) & 0x3FF;

    int vs;
    if (v->reg[11] & 4) {
        vs = v->vsram[((x >> 4) * 2 + plane) % VDP_VSRAM_WORDS];
    } else {
        vs = v->vsram[plane];
    }

    int px = (x - hs) & (cols * 8 - 1);
    int py = (y + vs) & (rows * 8 - 1);
    return nt_px(v, base, cols, px, py);
}

static bool in_window(const Vdp *v, int x, int y)
{
    int  vrows = v->reg[18] & 0x1F;
    bool down = (v->reg[18] & 0x80) != 0;
    if (down ? (y >> 3) >= vrows : (y >> 3) < vrows) {
        return true;
    }
    int  hcells = (v->reg[17] & 0x1F) * 2;
    bool right = (v->reg[17] & 0x80) != 0;
    return right ? (x >> 3) >= hcells : (x >> 3) < hcells;
}

/* One sprite's pixels on line y (`sy` lines into it) where no sprite
 * before it has put one: the first `draw` of its w x 8 columns from x0.
 * (Session 72) `table`: bits 12-15 of its size/link word, the colour
 * table its tiles go through (vdp_set_colours); a colour turned to 0 is
 * not drawn. */
static void sprite_line(const Vdp *v, Px spr[SCREEN_W], int x0, int sy,
                        int w, int h, u16 attr, int draw, int table)
{
    int  wpx = w * 8;
    const u8 *colours = ((v->colours_on >> table) & 1) ? v->colours[table]
                                                       : NULL;
    bool hf = (attr & 0x0800) != 0;
    bool vf = (attr & 0x1000) != 0;
    if (vf) {
        sy = h * 8 - 1 - sy;
    }
    for (int dx = 0; dx < draw; dx++) {
        int x = x0 + dx;
        if (x < 0 || x >= SCREEN_W || spr[x].idx != 0) {
            continue;
        }
        int sx = hf ? wpx - 1 - dx : dx;
        /* Sprite tiles run down each column first. */
        u16 tile = (u16)((attr & 0x07FF) + (sx >> 3) * h + (sy >> 3));
        u8  c = tile_px(v, tile, sx & 7, sy & 7, false, false);
        if (c && colours != NULL) {
            c = colours[c];
        }
        if (c) {
            spr[x].idx = (u8)(((attr >> 13) & 3) * 16 + c);
            spr[x].prio = (attr & 0x8000) != 0;
        }
    }
}

/* Sprite pixels for line y, first sprite in link order on top. */
static void line_sprites(const Vdp *v, int y, Px spr[SCREEN_W])
{
    memset(spr, 0, sizeof(Px) * SCREEN_W);

    u16 base = (u16)((v->reg[5] & 0x7E) << 9);   /* H40 ignores bit 0 */
    struct {
        int x, top, w, h;
        u16 attr;
        u16 raw_x;
        int table;                      /* (session 72) its colours */
    } list[VDP_SPRITES];
    int count = 0;

    int  idx = 0;
    bool more_on = false;               /* (session 61) */
    for (int n = 0; n < VDP_SPRITES; n++) {
        if (idx == v->more_at) {
            more_on = true;
        }
        u16 e = (u16)(base + idx * 8);
        int top = (vdp_vram_word(v, e) & 0x1FF) - 128;
        u16 sl = vdp_vram_word(v, (u16)(e + 2));
        int h = ((sl >> 8) & 3) + 1;
        int w = ((sl >> 10) & 3) + 1;

        if (y >= top && y < top + h * 8) {
            if (v->sprite_limits && count == VDP_LINE_SPRITES) {
                break;
            }
            list[count].raw_x = vdp_vram_word(v, (u16)(e + 6)) & 0x1FF;
            list[count].x = list[count].raw_x - 128;
            list[count].top = top;
            list[count].w = w;
            list[count].h = h;
            list[count].attr = vdp_vram_word(v, (u16)(e + 4));
            list[count].table = sl >> 12;
            count++;
        }

        int link = sl & 0x7F;
        if (link == 0 || link >= VDP_SPRITES) {
            break;
        }
        idx = link;
    }

    int  dots = VDP_LINE_DOTS;
    bool seen_x = false;
    for (int i = 0; i < count; i++) {
        /* A sprite at x 0 masks the rest of the line once a sprite not at
         * x 0 has been seen on it. */
        if (list[i].raw_x == 0) {
            if (seen_x) {
                more_on = false;
                break;
            }
        } else {
            seen_x = true;
        }

        int  wpx = list[i].w * 8;
        int  draw = wpx;
        if (v->sprite_limits) {
            if (dots <= 0) {
                break;
            }
            if (draw > dots) {
                draw = dots;
            }
            dots -= wpx;
        }

        sprite_line(v, spr, list[i].x, y - list[i].top, list[i].w,
                    list[i].h, list[i].attr, draw, list[i].table);
    }

    if (!more_on) {
        return;
    }
    for (u32 i = 0; i < v->more_count; i++) {
        const VdpSprite *s = &v->more[i];
        int top = (s->y & 0x1FF) - 128;
        int h = ((s->size_link >> 8) & 3) + 1;
        if (y < top || y >= top + h * 8) {
            continue;
        }
        int w = ((s->size_link >> 10) & 3) + 1;
        sprite_line(v, spr, (s->x & 0x1FF) - 128, y - top, w, h, s->attr,
                    w * 8, s->size_link >> 12);
    }
}

static void apply_raster(Vdp *v, int y)
{
    for (int i = 0; i < v->raster_count; i++) {
        if (v->raster[i].line == y) {
            store(v, v->raster[i].target, v->raster[i].addr,
                  v->raster[i].value);
        }
    }
}

void vdp_render(Vdp *v, u8 *out)
{
    u8  backdrop = (u8)(v->reg[7] & 0x3F);
    u16 win_base = (u16)((v->reg[3] & 0x3C) << 10);   /* H40 ignores bit 1 */

    /* Writes queued for line 0 or earlier are in effect from the start. */
    for (int i = 0; i < v->raster_count; i++) {
        if (v->raster[i].line < 0) {
            v->raster[i].line = 0;
        }
    }

    for (int y = 0; y < SCREEN_H; y++) {
        apply_raster(v, y);
        u8 *row = out + y * SCREEN_W;

        if (!(v->reg[1] & 0x40)) {
            memset(row, backdrop, SCREEN_W);
            continue;
        }

        Px spr[SCREEN_W];
        line_sprites(v, y, spr);

        for (int x = 0; x < SCREEN_W; x++) {
            if ((v->reg[0] & 0x20) && x < 8) {
                row[x] = backdrop;
                continue;
            }

            Px a = in_window(v, x, y) ? nt_px(v, win_base, 64, x, y)
                                      : scroll_px(v, 0, x, y);
            Px b = scroll_px(v, 1, x, y);
            Px s = spr[x];

            /* Back to front: B low, A low, sprites low, B high, A high,
             * sprites high. */
            u8 px = backdrop;
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

    /* Anything queued past the last line still lands before the next
     * frame, as it would during the bottom border. */
    for (int i = 0; i < v->raster_count; i++) {
        if (v->raster[i].line >= SCREEN_H) {
            store(v, v->raster[i].target, v->raster[i].addr,
                  v->raster[i].value);
        }
    }
    v->raster_count = 0;
}

void vdp_apply_rasters(Vdp *v)
{
    for (int i = 0; i < v->raster_count; i++) {
        if (v->raster[i].line < 0) {
            v->raster[i].line = 0;
        }
    }
    for (int y = 0; y < SCREEN_H; y++) {
        apply_raster(v, y);
    }
    for (int i = 0; i < v->raster_count; i++) {
        if (v->raster[i].line >= SCREEN_H) {
            store(v, v->raster[i].target, v->raster[i].addr,
                  v->raster[i].value);
        }
    }
    v->raster_count = 0;
}
