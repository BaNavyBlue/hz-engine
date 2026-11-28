/* hz_unitmove: Units_UpdateTeam (the units' half of Combat_Update) and
 * MapFx_Update on made-up states round a point of a real map, for
 * tools/hzunitmove_check.py. The game can't reach most of this yet (the
 * unit AI isn't ported, so nothing steers a unit), so the states are
 * made up: units of both teams on every kind of event tile, at the map's
 * edges, pressed against each other and the mechs (SUPPLYs among them),
 * dying at every step of the explosion, out of fuel, pushed by every
 * kind of flow; INFANTRY at outposts with every hold; stray grid ids under
 * a unit's centre; MapFx records nearly or wholly used up.
 *
 *   hz_unitmove ASSETS OUT.txt [--map N] [--cases N] [--seed N]
 *
 * Per case a "C" line (the map's layout back as it was), then a chain of
 * 1-8 steps, each logged whole, numbers in hex. The
 * state: "U i fields" for each record logged (every record made up, or
 * with anything in it now, and once logged, kept), "M p flags carried x y size form", "G" the
 * grid's nonzero cells (cell id pairs), "P" the 20 post records (flags
 * hold1 hold2 x y), "N" the counts (units 2, types 2 x 16, built 2,
 * outposts 4), "F" the 32 MapFx records, "X" globals (group sfx_queued
 * sfx_priority hit_flags hit_id event event3). The step: "T team"
 * (Units_UpdateTeam), then perhaps "V split cam1x cam1y cam2x cam2y vcamA
 * vcamB sxA sxB syA syB view1 view2 flags1 flags2 fill" (MapFx_Update;
 * VRAM byte i = (i x $9E3779B1 + fill x $9E37) >> 24 before it). Then the
 * state in lower case, and after a V step "v crc" (CRC-32 of VRAM) and
 * "l" the layout words that differ from the map's (offset value pairs). */

#include "video_vdp.h"
#include "../src/game/game.h"
#include "../src/game/tick.h"
#include "../src/game/romcodes.h"
#include "../src/game/tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 rng_state = 1;

static int rng_below(int n)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)(((rng_state >> 8) & 0xFFFFFF) % (u32)n);
}

static s16 near(s16 c, int r)
{
    return (s16)(c + rng_below(2 * r + 1) - r);
}

static u32 crc_table[256];

static void crc_init(void)
{
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        crc_table[i] = c;
    }
}

static u32 crc32_bytes(const u8 *p, size_t n)
{
    u32 crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc = (crc >> 8) ^ crc_table[(crc ^ p[i]) & 0xFF];
    }
    return ~crc;
}

static bool logged[HZ_UNITS];

static void put_unit(FILE *f, char k, int i, const HzUnit *u)
{
    fprintf(f, "%c %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X "
            "%X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n", k, i,
            u->flags, u->type, u->order, u->dir, u->turn_to, u->tick, u->hp,
            u->size, rom_bumped(u->bumped), u->fuel_use, (u16)u->x,
            (u16)u->y, u->sub_x,
            u->sub_y, u->missiles, u->ammo, u->fuel, u->terrain,
            u->sprite_attr, u->sprite_size, u->blocked, u->speed,
            u->prev_speed, u->max_speed, u->turret, u->turret_dir,
            u->aim_dir, u->push, u->overlay_attr, u->grid_cell,
            (u16)u->vel_x, (u16)u->vel_y, (u16)u->target_vel_x,
            (u16)u->target_vel_y, u->power);
}

static void put_state(FILE *f, bool after, const HzGame *g)
{
    for (int i = 0; i < HZ_UNITS; i++) {
        const HzUnit *u = rom_unit_c(g, i);
        if (u->flags != 0 || u->x != 0 || u->y != 0 || u->size != 0
            || u->hp != 0 || u->type != 0) {
            logged[i] = true;   /* (a blocked spawn leaves flags 0) */
        }
        if (logged[i]) {
            put_unit(f, after ? 'u' : 'U', i, u);
        }
    }
    if (!after) {
        for (int p = 0; p < 2; p++) {
            const HzMech *m = &g->mech[p];
            fprintf(f, "M %X %X %X %X %X %X %X\n", p, m->flags,
                    (unsigned)m->carried_slot, (u16)m->x, (u16)m->y, m->size,
                    m->form);
        }
    }
    fputc(after ? 'g' : 'G', f);
    for (int c = 0; c < HZ_GRID * HZ_GRID; c++) {
        if (!ref_is_none(g->grid[c])) {
            fprintf(f, " %X %X", c, rom_grid_id(g->grid[c]));
        }
    }
    fputc('\n', f);
    fputc(after ? 'p' : 'P', f);
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        const HzPost *o = &g->posts[i];
        fprintf(f, " %X %X %X %X %X", o->flags, o->hold[0], o->hold[1],
                (u16)o->x, (u16)o->y);
    }
    fputc('\n', f);
    fputc(after ? 'n' : 'N', f);
    for (int t = 0; t < 2; t++) {
        fprintf(f, " %X", (u8)g->unit_count[t]);   /* the 68k's byte */
    }
    for (int t = 0; t < 2; t++) {
        for (int k = 0; k < 16; k++) {
            fprintf(f, " %X", (u8)g->unit_type_count[t][k]);
        }
    }
    fprintf(f, " %X %X", g->units_built[0], g->units_built[1]);
    for (int k = 0; k < 4; k++) {
        fprintf(f, " %X", g->outpost_count[k]);
    }
    fputc('\n', f);
    fputc(after ? 'f' : 'F', f);
    for (int i = 0; i < HZ_MAPFX; i++) {
        const HzMapFx *x = &g->mapfx[i];
        fprintf(f, " %X %X %X %X %X %X %X %X", x->flags, x->b1, x->countdown,
                x->reload, (u16)x->x, (u16)x->y, x->block, x->layout_ofs);
    }
    fputc('\n', f);
    fprintf(f, "%c %X %X %X %X %X %X %X\n", after ? 'x' : 'X',
            g->unit_ai_group, g->sfx_queued, g->sfx_priority, g->hit_flags,
            rom_hit_id(g->hit_id), g->terrain_event, g->terrain_event3);
}

/* Grid_MarkObj's cell for an object of `size` at (x, y). */
static u16 cell_of(s16 x, s16 y, u8 size)
{
    u16 h = (u16)(size >> 1);
    u16 row = (u16)(((u16)(y + h) & 0xFFE0) << 1);
    u16 col = (u16)((u16)(x + h) >> 5);
    return (u16)((row + col) & 0xFFF);
}

/* Places to go: blocks with event tiles, by terrain type; water tiles. */
#define MAX_SPOTS 4096
static HzPoint spots[16][MAX_SPOTS];
static int spot_count[16];
static HzPoint water[MAX_SPOTS];
static int water_count;

static void find_spots(const HzGame *g)
{
    for (int r = 0; r < HZ_LAYOUT_SIZE; r++) {
        for (int c = 0; c < HZ_LAYOUT_SIZE; c++) {
            u16 block = g->layout[r][c] & 0x1FF;
            bool event = false;
            for (int k = 0; k < 16; k++) {
                u16 w = g->blocks[block][k];
                if (w & 0x2000) {
                    event = true;
                }
                if ((w & 0x0200) && water_count < MAX_SPOTS
                    && rng_below(4) == 0) {
                    water[water_count++] = (HzPoint){
                        (s16)(c * 32 + (k & 3) * 8), (s16)(r * 32 + (k >> 2) * 8) };
                }
            }
            int type = g->block_terrain[block] & 0x0F;
            if (event && spot_count[type] < MAX_SPOTS) {
                spots[type][spot_count[type]++] = (HzPoint){
                    (s16)(c * 32 + 16), (s16)(r * 32 + 16) };
            }
        }
    }
}

static const HzPoint *any_spot(void)
{
    int types[16];
    int n = 0;
    for (int t = 0; t < 16; t++) {
        if (spot_count[t] > 0) {
            types[n++] = t;
        }
    }
    if (n == 0) {
        return NULL;
    }
    int t = types[rng_below(n)];
    return &spots[t][rng_below(spot_count[t])];
}

static u16 saved_layout[HZ_LAYOUT_SIZE][HZ_LAYOUT_SIZE];
static HzPost saved_posts[HZ_POST_CLEARED];
static u8 saved_outpost_count[4];

/* A unit at (x, y) in slot `slot` of `team`, its record made up near the
 * game's values (or not) and its grid mark (usually) where it stands. */
static bool rush;   /* INFANTRY crowding an outpost (the capture) */

static void make_unit(HzGame *g, int team, int slot, s16 x, s16 y)
{
    HzUnit *u = unit_at(g, team, (u32)slot);
    unit_clear(u);
    logged[team * (HZ_UNITS / 2) + slot] = true;
    static const int types[] = { 0, 1, 2, 3, 4, 5, 8, 9, 8, 4, 0, 9 };
    u8 type = (u8)(rng_below(20) ? types[rng_below(12)] : rng_below(16));
    if (rush && rng_below(4)) {
        type = 8;
    }
    const HzUnitStats *st = unit_stats_for(g, type);
    u->flags = (u8)((rng_below(16) ? 0x80 : 0) | (team ? 0x10 : 0)
                    | (rng_below(6) ? 0 : 0x20) | (rng_below(14) ? 0 : 0x08)
                    | (rng_below(5) ? 0 : 0x02) | (rng_below(6) ? 0 : 0x04)
                    | (rng_below(2) ? 0x40 : 0) | (type == 3 ? 0x01 : 0));
    if (rng_below(30) == 0) {
        u->flags = (u8)(rng_below(256) & ~0x80);   /* free, bits left */
    }
    u->type = type;
    u->order = (u8)rng_below(16);
    u->dir = (u8)rng_below(16);
    u->turn_to = (u8)rng_below(16);
    u->tick = (u8)rng_below(256);
    u->size = (u8)(rng_below(8) ? st->size : rng_below(0x30));
    u->hp = (u16)((u->flags & 0x20) ? (rng_below(4) ? 1 + rng_below(2)
                                                    : 1 + rng_below(0x10))
                  : (rng_below(3) ? 0x30 + rng_below(3) - 1
                                  : rng_below(st->hp + 9)));
    u->fuel_use = (u8)(rng_below(4) ? st->fuel_use : rng_below(256));
    u->x = x;
    u->y = y;
    u->sub_x = (u16)(rng_below(10) ? rng_below(256) : rng_below(0x10000));
    u->sub_y = (u16)(rng_below(10) ? rng_below(256) : rng_below(0x10000));
    u->missiles = (u8)rng_below(st->missiles + 4);
    u->ammo = (u8)rng_below(st->ammo + 4);
    u->fuel = (u16)(rng_below(5) == 0 ? 0 : rng_below(4) == 0
                    ? rng_below(3) + u->fuel_use - 1 : rng_below(st->fuel + 0x30));
    u->terrain = (u16)rng_below(0x10000);
    u->sprite_attr = (u16)rng_below(0x10000);
    u->sprite_size = (u16)(st->sprite_size << 8);
    u->blocked = (u8)(rng_below(3) ? 0 : rng_below(16));
    u->max_speed = st->speed;
    u->speed = (u16)(rng_below(4) == 0 ? 0 : rng_below(4) ? st->speed
                     : rng_below(20) ? rng_below(0x40) : rng_below(0x10000));
    u->prev_speed = (u8)u->speed;
    u->turret = st->overlay;
    u->turret_dir = (u8)rng_below(16);
    u->aim_dir = (u8)rng_below(16);
    int pk = rng_below(6);
    u->push = (u8)(pk == 0 ? rng_below(0x80) : pk == 1 ? 0x80 | rng_below(0x40)
                   : pk == 2 ? 0xC0 | rng_below(0x40) : 0);
    u->overlay_attr = (u16)rng_below(0x10000);
    u->power = st->power;
    s16 d = (s16)(u->speed & 0xFF);
    u8 h = (u8)((u->dir + ((u->flags & 2) ? 8 : 0)) & 0x0F);
    if (rng_below(4)) {
        /* Unit_SetSprite's target velocity (sometimes not) */
        u->target_vel_x = (s16)(u16)((s32)d * SINE_TABLE[h].x);
        u->target_vel_y = (s16)(u16)((s32)d * SINE_TABLE[h].y);
    } else {
        u->target_vel_x = (s16)(rng_below(0x1000) - 0x800);
        u->target_vel_y = (s16)(rng_below(0x1000) - 0x800);
    }
    u->vel_x = rng_below(3) ? u->target_vel_x : (s16)(rng_below(0x1000) - 0x800);
    u->vel_y = rng_below(3) ? u->target_vel_y : (s16)(rng_below(0x1000) - 0x800);
    u16 cell = cell_of(u->x, u->y, u->size);
    u->grid_cell = rng_below(12) ? cell : (u16)rng_below(0x1000);
    if ((u->flags & 0x80) && rng_below(8)) {
        g->grid[cell] = ref_unit(team, (u32)slot);
    }
}

static void make_case(HzGame *g, s16 cx, s16 cy)
{
    memcpy(g->layout, saved_layout, sizeof saved_layout);
    memcpy(g->posts, saved_posts, sizeof saved_posts);
    memcpy(g->outpost_count, saved_outpost_count, sizeof saved_outpost_count);
    units_clear_all(g);
    for (int c = 0; c < HZ_GRID * HZ_GRID; c++) {
        g->grid[c] = ref_none();
    }
    memset(g->mapfx, 0, sizeof g->mapfx);
    memset(logged, 0, sizeof logged);

    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        m->flags = (u8)(0x80 | (p ? 0x10 : 0) | (rng_below(6) ? 0 : 0x40));
        m->carried_slot = (u32)(rng_below(4) ? rng_below(8) : 4);
        m->form = (u16)(rng_below(3) ? 0 : 1);
        m->size = 0x20;
        m->x = near(cx, 0x30);
        m->y = near(cy, 0x30);
        m->view = (s8)(rng_below(5) ? p : -1);
        if (m->form == 0 && rng_below(3)) {
            g->grid[cell_of(m->x, m->y, 0x20)] = ref_mech(p);
        }
    }

    /* Units: a crowd round the centre, some pressed together at the
     * overlap edges, a few with their centre in an occupied cell. */
    for (int team = 0; team < 2; team++) {
        int n = rng_below(10) == 0 ? 50 : rng_below(16);
        for (int k = 0; k < n; k++) {
            int slot = n == 50 ? k : rng_below(50);
            s16 x;
            s16 y;
            int r = rng_below(8);
            if (r == 0 && k > 0) {
                /* next to the unit before it, at its edges */
                const HzUnit *o = unit_at(g, team, (u32)((slot + 49) % 50));
                int s = o->size ? o->size : 0x18;
                x = (s16)(o->x + (rng_below(2) ? s : -s) + rng_below(5) - 2);
                y = (s16)(o->y + rng_below(2 * s + 1) - s);
            } else if (r == 1) {
                const HzMech *m = &g->mech[rng_below(2)];
                x = (s16)(m->x + (rng_below(2) ? 0x20 : -0x18) + rng_below(5) - 2);
                y = (s16)(m->y + rng_below(0x41) - 0x20);
            } else if (rush) {
                x = near((s16)(cx - 8), 0x14);
                y = near((s16)(cy - 8), 0x14);
            } else {
                x = near(cx, 0x50);
                y = near(cy, 0x50);
            }
            make_unit(g, team, slot, x, y);
        }
    }
    /* stray ids */
    for (int k = rng_below(4); k > 0; k--) {
        u16 c = (u16)(cell_of(cx, cy, 0) + rng_below(5) - 2
                      + (rng_below(5) - 2) * 64);
        static const u8 ids[] = { 0x01, 0x81, 0x41, 0xC1, 0x45, 0xF2 };
        g->grid[c & 0xFFF] = rom_grid_ref(ids[rng_below(6)]);
    }

    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        HzPost *o = &g->posts[i];
        if (o->flags == 0 || rng_below(2)) {
            continue;
        }
        o->flags = (u8)(0x80 | rng_below(4));
        o->hold[0] = (u8)(rng_below(3) ? rng_below(5) : 3);
        o->hold[1] = (u8)(rng_below(3) ? rng_below(5) : 3);
    }
    for (int k = 0; k < 4; k++) {
        if (rng_below(4) == 0) {
            g->outpost_count[k] = (u8)rng_below(256);
        }
    }
    for (int t = 0; t < 2; t++) {
        g->unit_count[t] = (u8)(rng_below(4) ? 20 + rng_below(30) : rng_below(256));
        for (int k = 0; k < 16; k++) {
            g->unit_type_count[t][k] = (u8)(rng_below(4) ? 3 + rng_below(10)
                                                         : rng_below(256));
        }
        g->units_built[t] = (u16)(rng_below(6) ? rng_below(500) : 0xFFFF - rng_below(2));
    }
    int used = rng_below(4) == 0 ? 32 - rng_below(6) : rng_below(8);
    for (int i = 0; i < used; i++) {
        HzMapFx *x = &g->mapfx[used >= 28 ? i : rng_below(HZ_MAPFX)];
        x->flags = (u8)(0x80 | (rng_below(3) ? 0 : 0x40) | rng_below(0x40));
        x->b1 = (u8)rng_below(256);
        x->countdown = (u8)(rng_below(2) ? 0 : rng_below(256));
        x->reload = (u8)rng_below(256);
        x->x = near(cx, 0x80);
        x->y = near(cy, 0x80);
        x->block = (u16)(rng_below(4) ? rng_below(0x200) : rng_below(0x100));
        x->layout_ofs = (u16)(rng_below(4) ? rng_below(0x2000) & 0x1FFE
                                           : rng_below(0x10000));
    }
    if (rng_below(4) == 0) {
        for (int i = 0; i < HZ_MAPFX; i++) {
            g->mapfx[i].flags &= 0x7F;      /* free, other bits left */
        }
    }

    static const int groups[] = { 0, 1, 0, 1, 2, 5, 6, 0xB };
    g->unit_ai_group = (u16)(rush && rng_below(2) ? 0 : groups[rng_below(8)]);
    g->sfx_queued = (u8)(rng_below(2) ? 0xFF : rng_below(32));
    g->sfx_priority = (u8)(g->sfx_queued == 0xFF ? 0xFF : rng_below(256));
    g->hit_flags = (u16)rng_below(16);
    g->hit_id = rom_hit_ref((u16)rng_below(256));
    g->terrain_event = (u16)rng_below(0x10000);
    g->terrain_event3 = (u16)rng_below(0x10000);
}

/* (Half B of docs/hackable.md's step 1) The cartridge's VRAM, laid out
 * from the draw layer's changes by tools/video_vdp.c. */
static VideoVdp g_cart;
static Video g_video;

/* MapFx_Update with made-up views; VRAM filled first. */
static void run_mapfx(FILE *f, HzGame *g, s16 cx, s16 cy)
{
    u32 fill = (u32)rng_below(0x10000);
    for (u32 i = 0; i < VDP_VRAM_SIZE; i++) {
        g_cart.vdp->vram[i] = (u8)((i * 0x9E3779B1u + fill * 0x9E37u)
                                   >> 24);
    }
    g->split_screen = (u16)rng_below(2);
    for (int p = 0; p < 2; p++) {
        g->camera[p].x = near((s16)(cx - 0x60), 0x90);
        g->camera[p].y = near((s16)(cy - 0x60), 0x90);
        g->scroll_x[p] = (s16)rng_below(0x10000);
        g->scroll_y[p] = (s16)rng_below(0x10000);
        g->mech[p].view = (s8)(rng_below(5) ? p : -1);
        g->mech[p].flags = (u8)((g->mech[p].flags & ~0x40)
                                | (rng_below(6) ? 0 : 0x40));
    }
    int va = rng_below(4) ? 0 : 1;
    int vb = rng_below(4) ? 1 : 0;
    g->view_camera[0] = &g->camera[va];
    g->view_camera[1] = &g->camera[vb];
    fprintf(f, "V %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n",
            g->split_screen, (u16)g->camera[0].x, (u16)g->camera[0].y,
            (u16)g->camera[1].x, (u16)g->camera[1].y, va, vb,
            (u16)g->scroll_x[0], (u16)g->scroll_x[1], (u16)g->scroll_y[0],
            (u16)g->scroll_y[1], (u8)g->mech[0].view, (u8)g->mech[1].view,
            g->mech[0].flags, g->mech[1].flags, fill);
    map_fx_update(g);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hz_unitmove ASSETS OUT.txt [--map N] "
                        "[--cases N] [--seed N]\n");
        return 2;
    }
    int map = 0;
    int cases = 1000;
    for (int i = 3; i + 1 < argc; i += 2) {
        int v = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--map") == 0) {
            map = v;
        } else if (strcmp(argv[i], "--cases") == 0) {
            cases = v;
        } else if (strcmp(argv[i], "--seed") == 0) {
            rng_state = (u32)v;
        } else {
            return 2;
        }
    }
    if (map < 0 || map >= HZ_MAPS) {
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    FILE *f = fopen(argv[2], "w");
    if (f == NULL) {
        return 1;
    }
    crc_init();
    static Vdp vdp;
    static HzGame game;
    video_init(&g_video);
    vdp_reset(&vdp);
    video_vdp_watch(&g_cart, &g_video, &vdp);
    game_init(&game, &assets, &g_video);
    game.options[0] = 0;
    game.options[3] = (u8)map;
    if (!tick_new(&game, NULL, NULL)) {
        fclose(f);
        return 1;
    }
    HzGame *g = &game;
    memcpy(saved_layout, g->layout, sizeof saved_layout);
    memcpy(saved_posts, g->posts, sizeof saved_posts);
    memcpy(saved_outpost_count, g->outpost_count, sizeof saved_outpost_count);
    find_spots(g);
    fprintf(f, "HZN1 %X\n", map);

    for (int c = 0; c < cases; c++) {
        s16 cx;
        s16 cy;
        int r = rng_below(12);
        const HzPoint *sp = any_spot();
        rush = r >= 10;
        if (rush) {
            const HzPost *o = &g->posts[rng_below(HZ_POST_OUTPOSTS)];
            if (o->flags == 0) {
                o = &g->posts[0];
            }
            cx = o->x;
            cy = o->y;
        } else if (r < 2) {
            const HzPost *o = &g->posts[rng_below(HZ_POST_CLEARED)];
            cx = near(o->x, 0x30);
            cy = near(o->y, 0x30);
        } else if (r < 6 && sp != NULL) {
            cx = near(sp->x, 0x18);
            cy = near(sp->y, 0x18);
        } else if (r == 6 && water_count > 0) {
            const HzPoint *w = &water[rng_below(water_count)];
            cx = near(w->x, 0x20);
            cy = near(w->y, 0x20);
        } else if (r == 7) {
            int e = rng_below(4);
            int a = rng_below(0x30) - 0x18;
            int b = rng_below(0x800);
            cx = (s16)((e == 0) ? a : (e == 1) ? 0x7E8 + a : b);
            cy = (s16)((e == 2) ? a : (e == 3) ? 0x7E8 + a : rng_below(0x800));
        } else {
            cx = (s16)rng_below(0x800);
            cy = (s16)rng_below(0x800);
        }
        make_case(g, cx, cy);
        fprintf(f, "C\n");
        int steps = 1 + rng_below(8);
        for (int s = 0; s < steps; s++) {
            put_state(f, false, g);
            int team = rng_below(2);
            fprintf(f, "T %X\n", team);
            units_update_team(g, team);
            bool fx = rng_below(3) == 0 || s == steps - 1;
            if (fx) {
                run_mapfx(f, g, cx, cy);
            }
            put_state(f, true, g);
            if (fx) {
                fprintf(f, "v %X\n", (unsigned)crc32_bytes(vdp.vram, VDP_VRAM_SIZE));
                fputc('l', f);
                for (int i = 0; i < HZ_LAYOUT_SIZE * HZ_LAYOUT_SIZE; i++) {
                    u16 w = g->layout[i >> 6][i & 63];
                    if (w != saved_layout[i >> 6][i & 63]) {
                        fprintf(f, " %X %X", i * 2, w);
                    }
                }
                fputc('\n', f);
            }
            /* the next step: the group moves on now and then */
            if (rng_below(2)) {
                g->unit_ai_group = (u16)(g->unit_ai_group >= 0xB ? 0
                                         : g->unit_ai_group + 1);
            }
        }
    }
    fclose(f);
    printf("map %d: %d cases\n", map, cases);
    return 0;
}
