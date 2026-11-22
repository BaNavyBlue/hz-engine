/* (Session 75) The PIXELS patch (HZ_FIX_PIXELS): objects collide, and
 * shots hit, only where their drawn pixels touch.
 *
 * The original tests boxes: a vehicle's 24 px square, an INFANTRY's and
 * an FWA's 16, the mech's 32 (in every form), a ground bullet's middle
 * point, an air bullet's and a missile's corner against 24 px of the
 * enemy jet. What is drawn in them is much less: a vehicle draws on 39%
 * (AMR-1D) to 59% (TAX-52) of its square, an INFANTRY and an FWA on 26%
 * and 31% of theirs, the robot on 30% of its 32 and the jet on 48%, a
 * shot on 3 in 8 of its tile (averages over the headings). The owner,
 * session 75: "make the collision box match drawn pixel perimeter instead
 * of a fixed box ... No reason to have oversized collision boxes",
 * "Collision box mods should apply to all objects with collision boxs",
 * "The collision box should change with object rotation".
 *
 * So with the patch on, each test the original makes is made first (a
 * hit can only be lost, never gained), and a hit stands only where the
 * two objects' pixels -- as they are drawn this frame: the unit's sprite
 * for its heading and its turret over it, the INFANTRY's walking step,
 * the mech's frame, the shot's tile, a dying unit's blast -- touch inside
 * the original's box. A SUPPLY's bumper (the $C round it without the
 * SUPPLY patch, how it docks) stays a box. The original's rule of one
 * object a 32 px grid cell (UNITS off) stays: the grid can't hold two.
 *
 * A unit turns, and an INFANTRY changes step, where it stands, without a
 * test; so two shapes can come to touch with neither moving. A move from
 * where they touch already doesn't count as a hit when it takes their
 * middles further apart, as UNITS has it for units on top of each other
 * (crowd.c, moves_off): they part, and neither goes further in.
 *
 * The pixels: assets.c keeps a byte a pixel row of each sprite tile and
 * mech frame tile (HzAssets.tile_rows, mech_rows), a bit set where the
 * colour isn't 0; a shape is up to 32 rows of 32 bits from those, as the
 * VDP lays the tiles out (down each column first, flips over the whole
 * sprite). A tile outside $240-$5FF (a mod's) counts as solid. */

#include "game.h"

#include <string.h>

bool hitmask_on(const HzGame *g)
{
    return (g->fixes & HZ_FIX_PIXELS) != 0;
}

/* The 32 bits of x the other way round. */
static u32 reverse32(u32 x)
{
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}

/* Row r of a tile: mech frame `frame`'s tile `tile` (0-15), or (frame
 * < 0) VRAM tile `tile`. */
static u8 tile_row(const HzAssets *a, int frame, u16 tile, int r)
{
    if (frame >= 0) {
        return a->mech_rows[frame][tile & 15][r];
    }
    tile &= 0x07FF;
    if (tile < HZ_MASK_FIRST || tile >= HZ_MASK_FIRST + HZ_MASK_TILES) {
        return 0xFF;
    }
    return a->tile_rows[tile - HZ_MASK_FIRST][r];
}

static void shape_start(HzShape *s, s16 x, s16 y, u8 size)
{
    memset(s, 0, sizeof *s);
    s->x = x;
    s->y = y;
    s->size = size;
}

/* A sprite into s's rows: `attr` its tile and flips (bits 0-10, $800,
 * $1000), `size` its size word (bits 8-9 the height - 1 in tiles, 10-11
 * the width - 1), `frame` a mech frame or -1. */
static void add_sprite(const HzAssets *a, HzShape *s, int frame, u16 attr,
                       u16 size)
{
    int w = ((size >> 10) & 3) + 1;
    int h = ((size >> 8) & 3) + 1;
    int wpx = w * 8;
    int hpx = h * 8;
    bool hf = (attr & 0x0800) != 0;
    bool vf = (attr & 0x1000) != 0;
    u16 base = (u16)(attr & 0x07FF);
    for (int dy = 0; dy < hpx; dy++) {
        int sy = vf ? hpx - 1 - dy : dy;
        u32 bits = 0;
        for (int cx = 0; cx < w; cx++) {
            u16 tile = (u16)(base + cx * h + (sy >> 3));
            bits = (bits << 8) | tile_row(a, frame, tile, sy & 7);
        }
        bits <<= 32 - wpx;
        if (hf) {
            bits = reverse32(bits) << (32 - wpx);
        }
        s->row[dy] |= bits;
    }
    if (hpx > s->h) {
        s->h = (u8)hpx;
    }
}

/* Only what lies in the object's box (`size` px square from its corner). */
static void clip(HzShape *s)
{
    int size = s->size;
    if (size >= 32) {
        return;
    }
    u32 cols = size == 0 ? 0 : ~0u << (32 - size);
    for (int r = 0; r < s->h; r++) {
        s->row[r] = r < size ? s->row[r] & cols : 0;
    }
    if (s->h > size) {
        s->h = (u8)size;
    }
}

void hitmask_unit(const HzGame *g, const HzUnit *u, HzShape *s)
{
    shape_start(s, u->x, u->y, u->size);
    add_sprite(g->assets, s, -1, u->sprite_attr, u->sprite_size);
    if (u->turret & 0x80) {
        add_sprite(g->assets, s, -1, u->overlay_attr, u->sprite_size);
    }
    clip(s);
}

/* As Unit_SetSprite will draw it, standing, all of it facing `dir`. */
void hitmask_unit_new(const HzGame *g, const HzUnit *u, u8 dir, s16 x,
                      s16 y, HzShape *s)
{
    const u16 (*table)[16] = g->assets->unit_sprites;
    bool repair = repair_on(g);
    dir &= 0x0F;
    u16 attr;
    if (unit_on_foot(u->type, repair)) {
        attr = (u16)(0x400 + dir * 4);
    } else {
        u8 row = (repair && u->type == UNIT_REPAIR) ? UNIT_SUPPLY : u->type;
        attr = row < HZ_UNIT_SPRITE_TYPES ? table[row][dir] : 0;
    }
    shape_start(s, x, y, u->size);
    add_sprite(g->assets, s, -1, attr, u->sprite_size);
    u8 t = (u8)(u->turret & 0x7F);
    if ((u->turret & 0x80) && t < HZ_UNIT_SPRITE_TYPES) {
        add_sprite(g->assets, s, -1, table[t][dir], u->sprite_size);
    }
    clip(s);
}

/* A robot is drawn without its frame's flips (Sprites_DrawMechRobot). */
void hitmask_mech(const HzGame *g, const HzMech *m, HzShape *s)
{
    shape_start(s, m->x, m->y, m->size);
    u16 flips = m->form == HZ_FORM_ROBOT ? 0 : (u16)(m->frame & 0x1800);
    add_sprite(g->assets, s, m->frame & (HZ_MECH_FRAMES - 1), flips, 0x0F00);
    clip(s);
}

/* The robot standing frame for its heading (mech_fuel_and_frame). */
void hitmask_robot(const HzGame *g, const HzMech *m, HzShape *s)
{
    shape_start(s, m->x, m->y, m->size);
    add_sprite(g->assets, s, m->dir & 0x0F, 0, 0x0F00);
    clip(s);
}

void hitmask_shot(const HzGame *g, const HzShot *sh, HzShape *s)
{
    shape_start(s, sh->x, sh->y, 8);
    add_sprite(g->assets, s, -1, sh->attr, 0);
}

void hitmask_ref(const HzGame *g, HzRef id, HzShape *s)
{
    if (id.kind == REF_MECH) {
        hitmask_mech(g, &g->mech[id.side], s);
    } else {
        hitmask_unit(g, ref_unit_ptr_c(g, id), s);
    }
}

bool hitmask_touch(const HzShape *a, const HzShape *b)
{
    int dx = (s16)(u16)(b->x - a->x);   /* b's column 0 in a's columns */
    int dy = (s16)(u16)(b->y - a->y);
    if (dx >= 32 || dx <= -32 || dy >= a->h || -dy >= b->h) {
        return false;
    }
    for (int r = dy > 0 ? dy : 0; r < a->h && r - dy < b->h; r++) {
        u32 rb = b->row[r - dy];
        rb = dx >= 0 ? rb >> dx : rb << -dx;
        if (a->row[r] & rb) {
            return true;
        }
    }
    return false;
}

bool hitmask_blocks(const HzShape *a, const HzPoint *from, const HzShape *b)
{
    if (!hitmask_touch(a, b)) {
        return false;
    }
    if (from == NULL) {
        return true;
    }
    HzShape was = *a;
    was.x = from->x;
    was.y = from->y;
    if (!hitmask_touch(&was, b)) {
        return true;
    }
    /* Middles, doubled. */
    long bx = 2L * b->x + b->size;
    long by = 2L * b->y + b->size;
    long nx = bx - (2L * a->x + a->size);
    long ny = by - (2L * a->y + a->size);
    long ox = bx - (2L * from->x + a->size);
    long oy = by - (2L * from->y + a->size);
    return nx * nx + ny * ny <= ox * ox + oy * oy;
}
