/* hz_mech: Game_FrameLoop as ported, with random pads and pokes, logging
 * both mechs round each Mech_Update (the game's mech_probe seam) for
 * tools/hzmech_check.py, Obj_TerrainEvent's mech side included.
 *
 *   hz_mech ASSETS OUT.txt [--map N] [--mode N] [--difficulty N]
 *           [--passes N] [--seed N]
 *
 * Pads: at each VBlank, now and then a port changes to random buttons
 * (never Start) or toggles A alone. Pokes before a pass, now and then: a mech's fuel low, its
 * mech_terrain_flow, mech_fire_delay, mech_dir_held, mech_lead, a dying
 * mech's HP low, mech_screen_x/y at the lead box's edges, v_free_ammo, mech_carried_slot (4 makes a robot count as
 * a SUPPLY to the other's collisions), obj_flags b3 with a carried slot
 * (logged "C p flags slot"), a jet moved next to the other mech, over a
 * unit (where A lifts it; an empty one at Mech_FindUnitToLift's reach
 * round one of its side's), or onto a dying rival's landing spot, a
 * carrying jet moved over open ground, terrain 1 (maps 0-1) or open water
 * (where only an ST-57U can be put down),
 * and a robot moved next to a unit or the other mech (half the time at the
 * overlap test's edges, where the SUPPLY rule's $C tells): its Grid_MarkMech
 * writes done as the game would (0 where it was, its id where it lands)
 * and logged as "T p from_x from_y x y" before the pass, for the model to
 * replay; and a jet turned into a robot on the spot (form 0, speed 0),
 * logged as "L p", so that robots meet units often.
 *
 * For the terrain events: a mech moved onto a block with event tiles (a
 * terrain type picked first, so outposts and HQs come up as often as the
 * big areas; a robot through the "T" line; a jet with a unit ready onto
 * one of its own outposts or its HQ), its HP, fuel, ammo and
 * missiles low, past the maximum or near the top of the field (where the
 * adds overflow), the same for the unit it carries (logged as "U k hp fuel
 * ammo missiles"), HP at the damaging ground's $20 step,
 * a unit ready (mech_prod_state 2, a random type 0-9 and order) or another
 * production state, obj_flags b6 (the order menu open; logged as a "C"
 * line), its side's v_unit_count at 47-50, an outpost's owner,
 * v_difficulty, and v_game_type negative (a demo) for a while early on.
 * For the capture (MapFx_Update's layout writes at the frame's end): an
 * active unit made an INFANTRY on an outpost not its side's, unit_blocked
 * b2 set so that its next move looks at the ground (logged "I k type size
 * x y blocked"), with its side's hold there at 3 (logged "H index hold1
 * hold2"), so that it takes the outpost.
 *
 * The log, numbers in hex: "HZM4 map mode difficulty"; then per
 * Mech_Update "S pass p" (before) and "E pass p" (after), each followed by
 * MECH_FIELDS for mech 1, the same for mech 2, then GLOBALS (see
 * put_state), the last two being CRC-32s of v_grid and of the layout
 * (big-endian words). Each pass from the
 * second starts with "P x1 y1 x2 y2", the mechs' places after the last
 * camera step (where their end-of-update marks went). A CPU's mech_pad is
 * set in the probe before its update, where AI_MechThink (not ported)
 * would set it: random now and then, B pressed again often. Round each
 * Combat_Update a "J" line (before) and a "K" line (after): the state, the
 * shots and a CRC of the units (combat_probe). */

#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/romcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 rng_state = 1;

static int rng_below(int n)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)(((rng_state >> 8) & 0xFFFFFF) % (u32)n);
}

typedef struct {
    FILE *f;
    int   pass;
} Log;

/* A shot's owner as the check numbers it: a unit's 68k record (team x 50
 * + slot), 100 + p a mech, 0 none. */
static int owner_num(HzRef r)
{
    if (r.kind == REF_MECH) {
        return 100 + r.side;
    }
    if (r.kind == REF_UNIT) {
        return rom_unit_index(r.side, r.slot);
    }
    return 0;
}

static void put_mech(FILE *f, const HzGame *g, const HzMech *m)
{
    int k = ((m->flags & 0x10) ? HZ_UNITS / 2 : 0) + (int)m->carried_slot;
    const HzUnit *u = rom_unit_c(g, (k < HZ_UNITS) ? k : HZ_UNITS - 1);
    const int v[] = {
        m->flags, (int)m->carried_slot, m->dir, m->turn_to, m->transform,
        m->transform_delay, m->hp, m->size, rom_bumped(m->bumped),
        m->fuel_use,
        (u16)m->x, (u16)m->y, m->sub_x, m->sub_y, m->fuel, m->terrain,
        m->frame, m->blocked, m->speed, m->max_speed, m->pad, m->pad_prev,
        m->lead, m->terrain_flow, m->form, m->pad_last, m->dir_held,
        (u16)m->vel_x, (u16)m->vel_y, (u16)m->target_vel_x,
        (u16)m->target_vel_y, (u16)m->screen_x, (u16)m->screen_y,
        (u16)m->cam_dx, (u16)m->cam_dy, (u16)m->home_cam.x,
        (u16)m->home_cam.y, (u16)m->home_screen.x, (u16)m->home_screen.y,
        m->fire_delay, m->menu.closing, m->fuel_count, m->walk_step,
        m->ammo, m->power, m->altitude, m->frame_loaded, m->missiles,
        m->prod_state, m->prod_type, m->prod_order, m->carried_order,
        m->carried_refill, (u8)m->view, m->shot_flags,
        rom_victim(m->hit_victim),
        m->missile_dirs,
        /* the unit in its team's slot mech_carried_slot */
        u->flags, u->type, u->order, u->hp, u->size, u->ammo, u->missiles,
        u->fuel, u->sprite_size, u->speed, u->max_speed, u->prev_speed,
        u->turret, u->power, u->fuel_use
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        fprintf(f, " %X", (unsigned)v[i]);
    }
}

/* n bytes as one hex number, the first the most significant. */
static void put_bytes(FILE *f, const u8 *b, int n)
{
    fputc(' ', f);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%02X", b[i]);
    }
}

/* CRC-32 (IEEE, as zlib's crc32) of v_grid. */
static u32 grid_crc(const HzGame *g)
{
    u32 crc = 0xFFFFFFFFu;
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        crc ^= rom_grid_id(g->grid[i]);
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
    }
    return ~crc;
}

/* CRC-32 (IEEE) of `n` more bytes onto `crc` (start 0xFFFFFFFF, end ~). */
static u32 crc_bytes(u32 crc, const u8 *b, int n)
{
    for (int i = 0; i < n; i++) {
        crc ^= b[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
    }
    return crc;
}

static void put_state(FILE *f, const HzGame *g)
{
    put_mech(f, g, &g->mech[0]);
    put_mech(f, g, &g->mech[1]);
    const int v[] = {
        g->mech_tick, (u16)g->camera[0].x, (u16)g->camera[0].y,
        (u16)g->camera[1].x, (u16)g->camera[1].y, g->split_screen, g->map,
        g->free_ammo, g->hit_flags, rom_hit_id(g->hit_id), g->terrain_event,
        g->terrain_event3, g->sfx_queued, g->sfx_priority, g->mech_spawns[0],
        g->mech_spawns[1], (u8)g->unit_count[0], (u8)g->unit_count[1],
        g->unit_ai_group, g->options[1], (u16)g->game_type,
        g->units_built[0], g->units_built[1], g->cpu_boost,
        g->unit_team_toggle, g->sfx_sent
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        fprintf(f, " %X", (unsigned)v[i]);
    }
    u8 posts[18];
    for (int i = 0; i < 18; i++) {
        posts[i] = g->posts[i].flags;
    }
    put_bytes(f, posts, 18);
    for (int t = 0; t < 2; t++) {
        u8 types[16];
        for (int i = 0; i < 16; i++) {
            types[i] = (u8)g->unit_type_count[t][i];
        }
        put_bytes(f, types, 16);
    }
    fprintf(f, " %X", (unsigned)grid_crc(g));
    u32 crc = 0xFFFFFFFFu;
    for (int r = 0; r < HZ_LAYOUT_SIZE; r++) {
        for (int c = 0; c < HZ_LAYOUT_SIZE; c++) {
            const u8 b[2] = { (u8)(g->layout[r][c] >> 8), (u8)g->layout[r][c] };
            crc = crc_bytes(crc, b, 2);
        }
    }
    fprintf(f, " %X", (unsigned)~crc);
}

/* Round Combat_Update (the game's combat_probe seam): "J pass 0" before
 * and "K pass 0" after it, each the state as an S line has it, "/", the
 * shots in use as index:fields, "/", a CRC-32 of the active uncarried
 * units as the shots touch them (index, flags without b6, HP, x, y, shot
 * flags, hit victim, sprite size and attr, overlay attr;
 * tools/hzshots_check.unit_crc with overlay). */
static void combat_probe(HzGame *g, bool after, void *user)
{
    Log *l = user;
    FILE *f = l->f;
    fprintf(f, "%c %X 0", after ? 'K' : 'J', l->pass);
    put_state(f, g);
    fprintf(f, " /");
    for (int i = 0; i < HZ_SHOTS; i++) {
        const HzShot *s = &g->shots[i];
        if (s->flags == 0) {
            continue;
        }
        fprintf(f, " %X:%X,%X,%X,%X,%X,%X,%X,%X,%X,%X,%X,%X,%X,%X,%X", i,
                s->flags, s->dir, (u16)s->x, (u16)s->y, s->speed, s->sub_x,
                s->sub_y, s->life, s->attr, (u16)s->vel_x, (u16)s->vel_y,
                (u16)s->aim_x, (u16)s->aim_y, s->power,
                owner_num(s->owner));
    }
    u32 crc = 0xFFFFFFFFu;
    for (int i = 0; i < HZ_UNITS; i++) {
        const HzUnit *u = rom_unit_c(g, i);
        if ((u->flags & 0x88) != 0x80) {
            continue;
        }
        const u8 b[] = {
            (u8)i, (u8)(u->flags & ~0x40), (u8)(u->hp >> 8), (u8)u->hp,
            (u8)((u16)u->x >> 8), (u8)u->x, (u8)((u16)u->y >> 8), (u8)u->y,
            u->shot_flags, rom_victim(u->hit_victim),
            (u8)(u->sprite_size >> 8),
            (u8)u->sprite_size, (u8)(u->sprite_attr >> 8), (u8)u->sprite_attr,
            (u8)(u->overlay_attr >> 8), (u8)u->overlay_attr
        };
        crc = crc_bytes(crc, b, (int)sizeof b);
    }
    fprintf(f, " / %X\n", (unsigned)~crc);
}

static void probe(HzGame *g, int p, bool after, void *user)
{
    Log *l = user;
    HzMech *m = &g->mech[p];
    if (!after && (m->flags & 0x01)) {
        /* A CPU's mech_pad, as AI_MechThink would set it at the start of
         * Mech_Update (after Mechs_Update has kept the last one in
         * mech_pad_prev): now and then a random one, else often B
         * pressed again (a CPU fires at each press, its
         * mech_fire_delay notwithstanding). */
        if (rng_below(8) == 0) {
            m->pad = (u8)rng_below(128);
        } else if (rng_below(3) == 0) {
            m->pad ^= 0x10;
        } else if (rng_below(6) == 0) {
            m->pad ^= 0x40;
        }
    }
    fprintf(l->f, "%c %X %X", after ? 'E' : 'S', l->pass, p);
    put_state(l->f, g);
    fputc('\n', l->f);
}

/* Random pads, never Start (no pauses here). */
static void on_frame(HzGame *g, void *user)
{
    (void)user;
    for (int port = 0; port < 2; port++) {
        if (rng_below(10) == 0) {
            g->pad_in[port].a = !g->pad_in[port].a;     /* A alone, often */
        }
        if (rng_below(8) != 0) {
            continue;
        }
        unsigned bits = (unsigned)rng_below(128);
        HzPad *pad = &g->pad_in[port];
        pad->up = (bits & 0x01) != 0;
        pad->down = (bits & 0x02) != 0;
        pad->left = (bits & 0x04) != 0;
        pad->right = (bits & 0x08) != 0;
        pad->b = (bits & 0x10) != 0;
        pad->c = (bits & 0x20) != 0;
        pad->a = (bits & 0x40) != 0;
        pad->start = false;
    }
}

static s16 clamp_map(int v)
{
    return (s16)((v < 0) ? 0 : ((v > 0x7E0) ? 0x7E0 : v));
}

/* The layout cells whose block has event tiles (b13), by terrain type. */
#define MAX_EVENT_CELLS (HZ_LAYOUT_SIZE * HZ_LAYOUT_SIZE)
static u16 event_cells[16][MAX_EVENT_CELLS];
static int event_cell_count[16];

static void find_event_cells(const HzGame *g)
{
    for (int i = 0; i < HZ_LAYOUT_SIZE * HZ_LAYOUT_SIZE; i++) {
        u16 block = g->layout[i / HZ_LAYOUT_SIZE][i % HZ_LAYOUT_SIZE] & 0x1FF;
        bool event = false;
        for (int t = 0; t < 16; t++) {
            if (g->blocks[block][t] & 0x2000) {
                event = true;
            }
        }
        if (event) {
            int type = g->block_terrain[block] & 0x0F;
            event_cells[type][event_cell_count[type]++] = (u16)i;
        }
    }
}

/* The layout cells whose block is all water (b9 on its 16 tiles) and not
 * terrain 6 or 8 (Mech_GetTerrain's early answers): a jet whose (x + 4) / 8
 * is the block's first tile column, and the same for y, reads 4 there. */
static u16 water_cells[MAX_EVENT_CELLS];
static int water_cell_count;
/* ... and the cells where it reads 0 (open ground: no water, no b15, no
 * tile under $3F) or, on maps 0-1, 1 (a block of terrain 6 or 8). */
static u16 ground_cells[2][MAX_EVENT_CELLS];
static int ground_cell_count[2];

static void find_water_cells(const HzGame *g)
{
    for (int i = 0; i < HZ_LAYOUT_SIZE * HZ_LAYOUT_SIZE; i++) {
        u16 block = g->layout[i / HZ_LAYOUT_SIZE][i % HZ_LAYOUT_SIZE] & 0x1FF;
        int type = g->block_terrain[block] & 0xFF;
        bool water = type != 6 && type != 8;
        for (int t = 0; t < 16; t++) {
            if (!(g->blocks[block][t] & 0x0200)) {
                water = false;
            }
        }
        if (water) {
            water_cells[water_cell_count++] = (u16)i;
        }
        bool open = type != 6 && type != 8;
        for (int t = 0; t < 16; t++) {
            u16 w = g->blocks[block][t];
            if ((w & 0x8200) || (w & 0x1FF) < 0x3F) {
                open = false;
            }
        }
        if (open) {
            ground_cells[0][ground_cell_count[0]++] = (u16)i;
        } else if (g->map < 2 && (type == 6 || type == 8)) {
            ground_cells[1][ground_cell_count[1]++] = (u16)i;
        }
    }
}

/* A robot moved to (x, y): its Grid_MarkMech writes as the game would (0
 * where it was, its id where it lands), logged for the model. */
static void move_robot(HzGame *g, FILE *f, int p, s16 x, s16 y)
{
    HzMech *m = &g->mech[p];
    s16 from_x = m->x;
    s16 from_y = m->y;
    grid_mark_mech(g, m, ref_none());
    m->x = x;
    m->y = y;
    grid_mark_mech(g, m, ref_mech(p));
    fprintf(f, "T %X %X %X %X %X\n", p, (unsigned)(u16)from_x,
            (unsigned)(u16)from_y, (unsigned)(u16)m->x, (unsigned)(u16)m->y);
}

static void poke(HzGame *g, FILE *f)
{
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        const HzMech *o = &g->mech[1 - p];
        if (!(m->flags & 0x20) && rng_below(200) == 0) {
            m->fuel = (u16)rng_below(6);
        }
        if (rng_below(50) == 0) {
            m->terrain_flow = (u8)rng_below(256);
        }
        if (rng_below(60) == 0) {
            m->fire_delay = (u8)rng_below(7);
        }
        if (rng_below(40) == 0) {
            m->dir_held = (u8)((rng_below(2) == 0) ? 0x0F + rng_below(3)
                                                   : rng_below(256));
        }
        if (rng_below(80) == 0) {
            m->lead = (u8)rng_below(4);
        }
        if (rng_below(60) == 0) {
            /* at an edge of the lead-0 box, give or take 2 */
            static const int xs[2][2] = { { 0x71, 0xCF }, { 0x21, 0x4F } };
            static const int ys[2] = { 0x49, 0x97 };
            int split = g->split_screen ? 1 : 0;
            m->screen_x = (s16)(xs[split][rng_below(2)] - 2 + rng_below(5));
            m->screen_y = (s16)(ys[rng_below(2)] - 2 + rng_below(5));
        }
        if ((m->flags & 0x20) && rng_below(20) == 0) {
            m->hp = (u16)(1 + rng_below(12));
        }
        if (m->form != 2 && rng_below(50) == 0) {
            /* (never while carrying: the game doesn't change a carrying
             * jet's slot, and the drop would put down another record) */
            m->carried_slot = (u8)((rng_below(2) == 0) ? 4 : rng_below(50));
        }
        if (m->form != 0 && rng_below(100) == 0) {
            m->x = clamp_map(o->x - 40 + rng_below(81));
            m->y = clamp_map(o->y - 40 + rng_below(81));
        }
        if (m->form != 0 && rng_below(40) == 0) {
            const HzUnit *u = rom_unit_c(g, rng_below(HZ_UNITS));
            if (u->flags & 0x80) {
                m->x = clamp_map(u->x - 24 + rng_below(33));
                m->y = clamp_map(u->y - 24 + rng_below(33));
            }
        }
        if (m->form != 0 && (o->flags & 0x20) && rng_below(10) == 0) {
            /* next to the dying rival's landing spot */
            m->x = clamp_map(o->home_cam.x + o->home_screen.x - 0x28
                             + rng_below(0x51));
            m->y = clamp_map(o->home_cam.y + o->home_screen.y - 0x20 - 0x28
                             + rng_below(0x51));
        }
        if (m->form != 2 && rng_below(150) == 0) {
            m->flags ^= 0x08;
            m->carried_slot = (u8)rng_below(50);
            fprintf(f, "C %X %X %X\n", p, m->flags,
                    (unsigned)m->carried_slot);
        }
        if (m->form != 0 && !(m->flags & 0x20) && m->transform == 0
            && rng_below(60) == 0) {
            m->form = 0;
            m->speed = 0;
            fprintf(f, "L %X\n", p);
        }
        if (m->form == 0 && rng_below(30) == 0) {
            s16 tx = o->x;
            s16 ty = o->y;
            int tsize = o->size;
            const HzUnit *uk = rom_unit_c(g, rng_below(HZ_UNITS));
            if (uk->flags & 0x80) {
                tx = uk->x;
                ty = uk->y;
                tsize = uk->size;
            }
            s16 x;
            s16 y;
            if (rng_below(2) == 0) {
                x = clamp_map(tx - 40 + rng_below(81));
                y = clamp_map(ty - 40 + rng_below(81));
            } else {
                /* At the overlap test's edges: right of or below the
                 * target by its size + 10..13, or left of or above it by
                 * the mech's; level with it on the other axis. */
                int right = rng_below(2);
                int dist = (right ? tsize : m->size) + 10 + rng_below(4);
                int along = right ? dist : -dist;
                int level = rng_below(9) - 4;
                if (rng_below(2) == 0) {
                    x = clamp_map(tx + along);
                    y = clamp_map(ty + level);
                } else {
                    x = clamp_map(tx + level);
                    y = clamp_map(ty + along);
                }
            }
            move_robot(g, f, p, x, y);
        }

        /* The terrain events. */
        if (!(m->flags & 0x20) && m->transform == 0 && rng_below(25) == 0) {
            int types[16];
            int n = 0;
            for (int t = 0; t < 16; t++) {
                if (event_cell_count[t] > 0) {
                    types[n++] = t;
                }
            }
            int t = types[rng_below(n)];
            int cell = event_cells[t][rng_below(event_cell_count[t])];
            s16 x = clamp_map((cell % HZ_LAYOUT_SIZE) * 32 - 24
                              + rng_below(49));
            s16 y = clamp_map((cell / HZ_LAYOUT_SIZE) * 32 - 24
                              + rng_below(49));
            if (m->form == 0) {
                move_robot(g, f, p, x, y);
            } else {
                m->x = x;
                m->y = y;
            }
        }
        if (m->form == 1 && m->prod_state == 2 && !(m->flags & 0x20)
            && m->transform == 0 && rng_below(15) == 0) {
            /* A jet with a unit ready: onto one of its side's outposts or
             * its HQ, where it can pick the unit up. */
            int own[HZ_POST_HQ + 2];
            int n = 0;
            for (int i = 0; i < HZ_POST_HQ + 2; i++) {
                u8 fl = g->posts[i].flags;
                if ((fl & 0x02) && !(((m->flags >> 4) ^ fl) & 0x01)) {
                    own[n++] = i;
                }
            }
            if (n > 0) {
                const HzPost *post = &g->posts[own[rng_below(n)]];
                m->x = clamp_map(post->x - 0x10 - 12 + rng_below(25));
                m->y = clamp_map(post->y - 0x10 - 12 + rng_below(25));
            }
        }
        if (!(m->flags & 0x20) && rng_below(40) == 0) {
            if (rng_below(8) == 0) {
                /* at the top, where Obj_RepairRefuel's adds overflow */
                m->hp = (u16)(0xFFF6 + rng_below(10));
                m->fuel = (u16)(0xFFD6 + rng_below(42));
                m->ammo = (u8)(0xFC + rng_below(4));
                m->missiles = (u8)(0xFC + rng_below(4));
            } else {
                m->hp = (u16)rng_below(0x110);
                m->fuel = (u16)rng_below(0x700);
                m->ammo = (u8)rng_below(0x70);
                m->missiles = (u8)rng_below(4);
            }
        }
        if ((m->flags & 0x08) && rng_below(30) == 0) {
            /* the carried unit low (nothing ported uses up a unit's
             * supplies), or at the top; logged as "U k hp fuel ammo
             * missiles" */
            int k = ((m->flags & 0x10) ? HZ_UNITS / 2 : 0)
                    + (int)m->carried_slot;
            HzUnit *u = rom_unit(g, (k < HZ_UNITS) ? k : HZ_UNITS - 1);
            if (rng_below(8) == 0) {
                u->hp = (u16)(0xFFF6 + rng_below(10));
                u->fuel = (u16)(0xFFD6 + rng_below(42));
                u->ammo = (u8)(0xFC + rng_below(4));
                u->missiles = (u8)(0xFC + rng_below(4));
            } else {
                u->hp = (u16)rng_below(0x120);
                u->fuel = (u16)rng_below(0x600);
                u->ammo = (u8)rng_below(0x50);
                u->missiles = (u8)rng_below(0x10);
            }
            fprintf(f, "U %X %X %X %X %X\n", (k < HZ_UNITS) ? k : HZ_UNITS - 1,
                    u->hp, u->fuel, u->ammo, u->missiles);
        }
        if (!(m->flags & 0x20) && rng_below(60) == 0) {
            m->hp = (u16)(0x1C + rng_below(9));
        }
        if (m->form == 2 && m->transform == 0 && rng_below(12) == 0) {
            /* A carrying jet over open ground (or terrain 1), where most
             * units can be put down. */
            int k = (ground_cell_count[1] > 0 && rng_below(3) == 0) ? 1 : 0;
            if (ground_cell_count[k] > 0) {
                int cell = ground_cells[k][rng_below(ground_cell_count[k])];
                m->x = clamp_map((cell % HZ_LAYOUT_SIZE) * 32 - 4
                                 + rng_below(8));
                m->y = clamp_map((cell / HZ_LAYOUT_SIZE) * 32 - 4
                                 + rng_below(8));
            }
        }
        if (m->form == 1 && !(m->flags & 0x28) && m->transform == 0
            && rng_below(12) == 0) {
            /* An empty jet over one of its side's units, at
             * Mech_FindUnitToLift's reach or just past it. */
            int k = ((m->flags & 0x10) ? HZ_UNITS / 2 : 0)
                    + rng_below(HZ_UNITS / 2);
            const HzUnit *u = rom_unit_c(g, k);
            if ((u->flags & 0xA8) == 0x80) {
                int reach = (u8)(0x20 - u->size) + 0x10;
                m->x = clamp_map(u->x + 8 - rng_below(reach + 2));
                m->y = clamp_map(u->y + 8 - rng_below(reach + 2));
            }
        }
        if (m->form == 2 && m->transform == 0 && water_cell_count > 0
            && rng_below(15) == 0) {
            /* A carrying jet over open water: an ST-57U can be put down
             * there, anything else can't. */
            int cell = water_cells[rng_below(water_cell_count)];
            m->x = clamp_map((cell % HZ_LAYOUT_SIZE) * 32 - 4 + rng_below(8));
            m->y = clamp_map((cell / HZ_LAYOUT_SIZE) * 32 - 4 + rng_below(8));
        }
        /* The order menu's flag, for a few passes now and then (C opens
         * it too; the menus are frozen here, so this also closes it). */
        if (rng_below((m->flags & 0x40) ? 10 : 300) == 0) {
            m->flags ^= 0x40;
            fprintf(f, "C %X %X %X\n", p, m->flags,
                    (unsigned)m->carried_slot);
        }
        if (rng_below(30) == 0) {
            static const u8 states[3] = { 0x00, 0x01, 0xFF };
            m->prod_state = (rng_below(3) == 0) ? states[rng_below(3)] : 2;
            m->prod_type = (u8)rng_below(10);
            m->prod_order = (u8)rng_below(16);
        }
    }
    if (g->outpost_count[3] > 0 && rng_below(40) == 0) {
        /* An INFANTRY onto an outpost not its side's, its side's hold
         * there at 3: its next move (unit_blocked b2) takes it. */
        int k = rng_below(HZ_UNITS);
        HzUnit *u = rom_unit(g, k);
        int index = rng_below(g->outpost_count[3]);
        HzPost *post = &g->posts[index];
        int team = (u->flags & 0x10) ? 1 : 0;
        if ((u->flags & 0xA8) == 0x80 && post->flags != (team ? 0x83 : 0x82)) {
            u->type = 8;
            u->size = unit_stats_for(g, 8)->size;
            u->x = clamp_map(post->x - u->size / 2 - 8 + rng_below(17));
            u->y = clamp_map(post->y - u->size / 2 - 8 + rng_below(17));
            u->blocked |= 0x04;
            fprintf(f, "I %X %X %X %X %X %X\n", k, u->type, u->size,
                    (unsigned)(u16)u->x, (unsigned)(u16)u->y, u->blocked);
            if (rng_below(4) != 0) {
                if (team) {
                    post->hold[1] = 3;
                } else {
                    post->hold[0] = 3;
                }
            }
            fprintf(f, "H %X %X %X\n", index, post->hold[0],
                    post->hold[1]);
        }
    }
    if (rng_below(100) == 0) {
        g->unit_count[rng_below(2)] = (u8)(47 + rng_below(4));
    }
    if (rng_below(150) == 0 && g->outpost_count[3] > 0) {
        g->posts[rng_below(g->outpost_count[3])].flags =
            (u8)(0x80 | rng_below(4));
    }
    if (rng_below(200) == 0) {
        g->options[1] = (u8)rng_below(4);
    }
    /* A demo for a while, but not near its 60 s timeout ($E10 frames),
     * which would end the loop. */
    if (g->frame_counter >= 0xD00) {
        g->game_type = 0;
    } else if (rng_below(200) == 0) {
        g->game_type = (g->game_type < 0) ? 0 : -1;
    }
    if (rng_below(300) == 0) {
        g->free_ammo ^= 1;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hz_mech ASSETS OUT.txt [--map N] [--mode N] "
                        "[--difficulty N] [--passes N] [--seed N]\n");
        return 2;
    }
    int map = 0;
    int mode = 0;
    int difficulty = 0;
    int passes = 2000;
    for (int i = 3; i + 1 < argc; i += 2) {
        int v = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--map") == 0) {
            map = v;
        } else if (strcmp(argv[i], "--mode") == 0) {
            mode = v;
        } else if (strcmp(argv[i], "--difficulty") == 0) {
            difficulty = v;
        } else if (strcmp(argv[i], "--passes") == 0) {
            passes = v;
        } else if (strcmp(argv[i], "--seed") == 0) {
            rng_state = (u32)v;
        } else {
            return 2;
        }
    }
    if (map < 0 || map >= HZ_MAPS || mode < 0 || mode > 7) {
        return 2;
    }

    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    Log log = { fopen(argv[2], "w"), 0 };
    if (log.f == NULL) {
        return 1;
    }
    static Video video;
    static HzGame game;
    video_init(&video);
    game_init(&game, &assets, &video);
    game.frame = on_frame;
    game.options[0] = (u8)mode;
    game.options[1] = (u8)difficulty;
    game.options[3] = (u8)map;
    if (!game_new(&game)) {
        fclose(log.f);
        return 1;
    }
    game_start(&game);
    game.mech_probe = probe;
    game.mech_probe_user = &log;
    find_event_cells(&game);
    find_water_cells(&game);
    fprintf(log.f, "HZM4 %X %X %X\n", map, mode, difficulty);

    game.combat_probe = combat_probe;
    game.units_frozen = true;       /* the model doesn't follow the unit AI */
    game.menus_frozen = true;       /* nor the order menu (make ordermenu) */
    game.cpus_frozen = true;        /* nor the CPU's AI (make mechai) */
    game.combat_probe_user = &log;
    HzLoopExit exit = game_frame(&game, false);
    for (log.pass = 1; log.pass < passes && exit == HZ_LOOP_ON; log.pass++) {
        fprintf(log.f, "P %X %X %X %X\n", (unsigned)(u16)game.mech[0].x,
                (unsigned)(u16)game.mech[0].y, (unsigned)(u16)game.mech[1].x,
                (unsigned)(u16)game.mech[1].y);
        poke(&game, log.f);
        exit = game_frame(&game, true);
    }
    fclose(log.f);
    printf("map %d mode %d: %d passes\n", map, mode, log.pass);
    return 0;
}
