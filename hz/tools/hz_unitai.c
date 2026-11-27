/* hz_unitai: the port's unit AI (src/game/unit_ai.c) on states given as
 * images of the 68k's RAM, for tools/hzunitai_check.py, which runs the
 * original code (tools/m68krun.py) on the same images and compares.
 *
 *   hz_unitai ASSETS IN OUT
 *   hz_unitai ASSETS --game MAP MODE DIFFICULTY FRAMES VISITS IN OUT
 *
 * --game plays a real game instead (nobody at the pads; VISITS 1 puts P1's
 * mech down beside the other side's units every $40 frames) and writes
 * each UnitAI_Update's state before and after as the cases above.
 *
 * IN: cases, each a byte (how many UnitAI_Update calls to make in a row)
 * then $10000 bytes, the RAM $FF0000-$FFFFFF. OUT: per case the RAM after,
 * $10000 bytes. Only what the unit AI reads or writes is carried over (see
 * load/store): the RNG seed, the game type, difficulty and CPU boost, the
 * sound effect queue, v_unit_ai_group, the posts, both mechs' flags,
 * position and size, the 100 unit records, the 64 shots, the grid and the
 * layout; the rest of OUT is IN's.
 *
 * This file is where the port's readable state meets the 68k's bytes: the
 * unit AI's fields (HzUnitAI) go to and from +$38-$4F as the original lays
 * them out, which several orders share (see game.h), and (session 61) the
 * port's references to and from the 68k's codes and pointers
 * (src/game/romcodes.h). */

#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/romcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SIZE   0x10000
#define A_LAYOUT   0x5000   /* v_unpack_buf */
#define A_POSTS    0x89B4   /* v_outposts, then v_hq */
#define A_GROUP    0x8A70   /* v_unit_ai_group */
#define A_MECH1    0x8B94
#define A_UNITS    0x8D94
#define A_SHOTS    0xACD4   /* v_bullets1 */
#define A_GRID     0xB4D4
#define A_SEED     0xF7C0
#define A_OPTIONS  0xF84A   /* v_options: +1 v_difficulty */
#define A_SFX_PRI  0xF90A
#define A_SFX_Q    0xF90C
#define A_GAMETYPE 0xF924
#define A_BOOST    0xF92E

static u8 rd8(const u8 *r, u32 a) { return r[a]; }
static u16 rd16(const u8 *r, u32 a) { return (u16)((r[a] << 8) | r[a + 1]); }
static u32 rd32(const u8 *r, u32 a)
{
    return ((u32)rd16(r, a) << 16) | rd16(r, a + 2);
}
static void wr8(u8 *r, u32 a, u8 v) { r[a] = v; }
static void wr16(u8 *r, u32 a, u16 v)
{
    r[a] = (u8)(v >> 8);
    r[a + 1] = (u8)v;
}
static void wr32(u8 *r, u32 a, u32 v)
{
    wr16(r, a, (u16)(v >> 16));
    wr16(r, a + 2, (u16)v);
}

/* An object's address as UnitAI_ObjFromId leaves it (movea.w: $FFFFxxxx);
 * 0 for none. */
static u32 obj_addr(HzRef r)
{
    u16 a = rom_addr(r);
    return a ? (0xFFFF0000u | a) : 0;
}

/* The object at a 68k address, none for none. */
static HzRef addr_id(u32 addr)
{
    if ((addr & 0xFFFFFF) < 0xFF0000) {
        return ref_none();
    }
    return rom_addr_ref((u16)addr);
}

static bool home_order(const HzUnit *u)
{
    u8 o = u->order & 0x0F;
    return o == ORDER_PATROL || o == ORDER_GUARD || o == ORDER_GUARD_NEAR
        || o == ORDER_HOLD;
}

static void load_unit(HzUnit *u, const u8 *r)
{
    u->flags = r[0x00];
    u->type = r[0x01];
    u->order = r[0x02];
    u->dir = r[0x03];
    u->turn_to = r[0x04];
    u->tick = r[0x05];
    u->phase = r[0x06];
    u->fire_delay = r[0x07];
    u->power = r[0x08];
    u->hit_victim = rom_victim_ref(r[0x09]);
    u->hp = rd16(r, 0x0A);
    u->size = r[0x0C];
    u->raw_0d = r[0x0D];
    u->bumped = rom_bumped_ref(r[0x0E]);
    u->fuel_use = r[0x0F];
    u->x = (s16)rd16(r, 0x10);
    u->y = (s16)rd16(r, 0x12);
    u->sub_x = rd16(r, 0x14);
    u->sub_y = rd16(r, 0x16);
    u->missiles = r[0x18];
    u->ammo = r[0x19];
    u->fuel = rd16(r, 0x1A);
    u->terrain = rd16(r, 0x1C);
    u->sprite_attr = rd16(r, 0x1E);
    u->sprite_size = rd16(r, 0x20);
    u->shot_flags = r[0x22];
    u->blocked = r[0x23];
    u->speed = rd16(r, 0x24);
    u->prev_speed = r[0x26];
    u->max_speed = r[0x27];
    u->turret = r[0x28];
    u->turret_dir = r[0x29];
    u->aim_dir = r[0x2A];
    u->push = r[0x2B];
    u->overlay_attr = rd16(r, 0x2C);
    u->grid_cell = rd16(r, 0x2E);
    u->vel_x = (s16)rd16(r, 0x30);
    u->vel_y = (s16)rd16(r, 0x32);
    u->target_vel_x = (s16)rd16(r, 0x34);
    u->target_vel_y = (s16)rd16(r, 0x36);

    HzUnitAI *ai = &u->ai;
    memset(ai, 0, sizeof *ai);
    bool supply = u->type == UNIT_SUPPLY;
    if (supply) {
        ai->docked = r[0x38] != 0;
        ai->customer_speed = r[0x39];
        ai->detour_left = r[0x3A];
        ai->detour_step = (s8)r[0x3B];
        ai->cursor = r[0x46];
        ai->customer_id = addr_id(rd32(r, 0x48));
    } else {
        u8 o = u->order & 0x0F;
        if (home_order(u)) {
            u16 hi = rd16(r, 0x38);
            ai->home_set = (hi & 0x8000) != 0;
            ai->home_x = (s16)(hi & 0x7FFF);
        } else if (o == ORDER_AMBUSH) {
            ai->ambush_started = r[0x38] != 0;
        } else if (o == ORDER_ADVANCE) {
            ai->advance_dir = r[0x38];
        } else if (o == ORDER_ATTACK_HQ) {
            ai->attack_state = r[0x38];
        }
        ai->home_y = (s16)rd16(r, 0x3A);
        ai->target_id = rom_grid_ref(r[0x46]);
    }
    ai->misses = r[0x3C];
    ai->state = r[0x3D];
    ai->target_dir = r[0x3E];
    ai->path_hint = r[0x3F];
    ai->dest_dist = rd16(r, 0x40);
    ai->dest_dir = r[0x42];
    ai->far_dir = r[0x43];
    ai->hold = r[0x44];
    ai->saved_dir = r[0x45];
    ai->shot_out = r[0x47] != 0;
    ai->far_id = addr_id(rd32(r, 0x4C));
}

static void store_unit(const HzUnit *u, u8 *r)
{
    r[0x00] = u->flags;
    r[0x01] = u->type;
    r[0x02] = u->order;
    r[0x03] = u->dir;
    r[0x04] = u->turn_to;
    r[0x05] = u->tick;
    r[0x06] = u->phase;
    r[0x07] = u->fire_delay;
    r[0x08] = u->power;
    r[0x09] = rom_victim(u->hit_victim);
    wr16(r, 0x0A, u->hp);
    r[0x0C] = u->size;
    r[0x0D] = u->raw_0d;
    r[0x0E] = rom_bumped(u->bumped);
    r[0x0F] = u->fuel_use;
    wr16(r, 0x10, (u16)u->x);
    wr16(r, 0x12, (u16)u->y);
    wr16(r, 0x14, u->sub_x);
    wr16(r, 0x16, u->sub_y);
    r[0x18] = u->missiles;
    r[0x19] = u->ammo;
    wr16(r, 0x1A, u->fuel);
    wr16(r, 0x1C, u->terrain);
    wr16(r, 0x1E, u->sprite_attr);
    wr16(r, 0x20, u->sprite_size);
    r[0x22] = u->shot_flags;
    r[0x23] = u->blocked;
    wr16(r, 0x24, u->speed);
    r[0x26] = u->prev_speed;
    r[0x27] = u->max_speed;
    r[0x28] = u->turret;
    r[0x29] = u->turret_dir;
    r[0x2A] = u->aim_dir;
    r[0x2B] = u->push;
    wr16(r, 0x2C, u->overlay_attr);
    wr16(r, 0x2E, u->grid_cell);
    wr16(r, 0x30, (u16)u->vel_x);
    wr16(r, 0x32, (u16)u->vel_y);
    wr16(r, 0x34, (u16)u->target_vel_x);
    wr16(r, 0x36, (u16)u->target_vel_y);

    const HzUnitAI *ai = &u->ai;
    memset(r + 0x38, 0, 0x18);
    if (u->type == UNIT_SUPPLY) {
        r[0x38] = ai->docked ? 0xFF : 0;
        r[0x39] = ai->customer_speed;
        r[0x3A] = ai->detour_left;
        r[0x3B] = (u8)ai->detour_step;
        r[0x46] = (u8)ai->cursor;
        wr32(r, 0x48, obj_addr(ai->customer_id));
    } else {
        u8 o = u->order & 0x0F;
        if (home_order(u)) {
            wr16(r, 0x38, ai->home_set ? (u16)(ai->home_x | 0x8000) : 0);
        } else if (o == ORDER_AMBUSH) {
            r[0x38] = ai->ambush_started ? 0xFF : 0;
        } else if (o == ORDER_ADVANCE) {
            r[0x38] = ai->advance_dir;
        } else if (o == ORDER_ATTACK_HQ) {
            r[0x38] = ai->attack_state;
        }
        wr16(r, 0x3A, (u16)ai->home_y);
        r[0x46] = rom_grid_id(ai->target_id);
        /* the target pointer: what target_id names (the 68k keeps a stale
         * one once the target is lost; the checker doesn't compare it then) */
        wr32(r, 0x48, obj_addr(ai->target_id));
    }
    r[0x3C] = ai->misses;
    r[0x3D] = ai->state;
    r[0x3E] = ai->target_dir;
    r[0x3F] = ai->path_hint;
    wr16(r, 0x40, ai->dest_dist);
    r[0x42] = ai->dest_dir;
    r[0x43] = ai->far_dir;
    r[0x44] = ai->hold;
    r[0x45] = ai->saved_dir;
    r[0x47] = ai->shot_out ? 0xFF : 0;
    wr32(r, 0x4C, obj_addr(ai->far_id));
}

/* A shot's owner (a mech or a unit) <-> its address. */
static u32 owner_addr(HzRef owner)
{
    return 0xFFFF0000u | rom_addr(owner);
}

static HzRef addr_owner(u32 addr)
{
    return rom_addr_ref((u16)addr);
}

/* Each shot's owner pointer as loaded: written back as it was unless the
 * shot has another owner now (a free slot's is whatever was left there). */
static u32 shot_owner_raw[HZ_SHOTS];

static void load(HzGame *g, const u8 *ram)
{
    g->rng_seed = rd32(ram, A_SEED);
    g->game_type = (s16)rd16(ram, A_GAMETYPE);
    g->options[0] = rd8(ram, A_OPTIONS);
    g->options[1] = rd8(ram, A_OPTIONS + 1);
    g->cpu_boost = rd16(ram, A_BOOST);
    g->sfx_priority = rd8(ram, A_SFX_PRI);
    g->sfx_queued = rd8(ram, A_SFX_Q);
    g->unit_ai_group = rd16(ram, A_GROUP);
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        const u8 *p = ram + A_POSTS + i * 8;
        g->posts[i].flags = p[0];
        g->posts[i].b1 = p[1];
        g->posts[i].hold[0] = p[2];
        g->posts[i].hold[1] = p[3];
        g->posts[i].x = (s16)rd16(p, 4);
        g->posts[i].y = (s16)rd16(p, 6);
    }
    for (int m = 0; m < 2; m++) {
        const u8 *p = ram + A_MECH1 + m * 0x100;
        g->mech[m].flags = p[0];
        g->mech[m].size = p[0x0C];
        g->mech[m].x = (s16)rd16(p, 0x10);
        g->mech[m].y = (s16)rd16(p, 0x12);
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        load_unit(rom_unit(g, i), ram + A_UNITS + i * 0x50);
    }
    for (int i = 0; i < HZ_SHOTS; i++) {
        const u8 *p = ram + A_SHOTS + i * 0x20;
        HzShot *s = &g->shots[i];
        s->flags = p[0];
        s->dir = p[1];
        s->x = (s16)rd16(p, 2);
        s->y = (s16)rd16(p, 4);
        s->speed = rd16(p, 6);
        s->sub_x = rd16(p, 8);
        s->sub_y = rd16(p, 0xA);
        s->life = rd16(p, 0xC);
        s->attr = rd16(p, 0xE);
        s->vel_x = (s16)rd16(p, 0x10);
        s->vel_y = (s16)rd16(p, 0x12);
        s->aim_x = (s16)rd16(p, 0x14);
        s->aim_y = (s16)rd16(p, 0x16);
        s->power = p[0x18];
        shot_owner_raw[i] = rd32(p, 0x1C);
        s->owner = addr_owner(shot_owner_raw[i]);
    }
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        g->grid[i] = rom_grid_ref(ram[A_GRID + i]);
    }
    for (int row = 0; row < 64; row++) {
        for (int col = 0; col < 64; col++) {
            g->layout[row][col] = rd16(ram, (u32)(A_LAYOUT + (row * 64 + col) * 2));
        }
    }
}

static void store(const HzGame *g, u8 *ram)
{
    wr32(ram, A_SEED, g->rng_seed);
    wr16(ram, A_GAMETYPE, (u16)g->game_type);
    wr8(ram, A_OPTIONS, g->options[0]);
    wr8(ram, A_OPTIONS + 1, g->options[1]);
    wr16(ram, A_BOOST, g->cpu_boost);
    wr8(ram, A_SFX_PRI, g->sfx_priority);
    wr8(ram, A_SFX_Q, g->sfx_queued);
    wr16(ram, A_GROUP, g->unit_ai_group);
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        u8 *p = ram + A_POSTS + i * 8;
        p[0] = g->posts[i].flags;
        p[1] = g->posts[i].b1;
        p[2] = g->posts[i].hold[0];
        p[3] = g->posts[i].hold[1];
        wr16(p, 4, (u16)g->posts[i].x);
        wr16(p, 6, (u16)g->posts[i].y);
    }
    for (int m = 0; m < 2; m++) {
        u8 *p = ram + A_MECH1 + m * 0x100;
        p[0] = g->mech[m].flags;
        p[0x0C] = g->mech[m].size;
        wr16(p, 0x10, (u16)g->mech[m].x);
        wr16(p, 0x12, (u16)g->mech[m].y);
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        store_unit(rom_unit_c(g, i), ram + A_UNITS + i * 0x50);
    }
    for (int i = 0; i < HZ_SHOTS; i++) {
        u8 *p = ram + A_SHOTS + i * 0x20;
        const HzShot *s = &g->shots[i];
        memset(p, 0, 0x20);
        p[0] = s->flags;
        p[1] = s->dir;
        wr16(p, 2, (u16)s->x);
        wr16(p, 4, (u16)s->y);
        wr16(p, 6, s->speed);
        wr16(p, 8, s->sub_x);
        wr16(p, 0xA, s->sub_y);
        wr16(p, 0xC, s->life);
        wr16(p, 0xE, s->attr);
        wr16(p, 0x10, (u16)s->vel_x);
        wr16(p, 0x12, (u16)s->vel_y);
        wr16(p, 0x14, (u16)s->aim_x);
        wr16(p, 0x16, (u16)s->aim_y);
        p[0x18] = s->power;
        bool same = ref_eq(addr_owner(shot_owner_raw[i]), s->owner);
        wr32(p, 0x1C, same ? shot_owner_raw[i] : owner_addr(s->owner));
    }
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        ram[A_GRID + i] = rom_grid_id(g->grid[i]);
    }
    for (int row = 0; row < 64; row++) {
        for (int col = 0; col < 64; col++) {
            wr16(ram, (u32)(A_LAYOUT + (row * 64 + col) * 2), g->layout[row][col]);
        }
    }
}

/* --game: a real game; each UnitAI_Update's state before (into IN, as a
 * case of one call) and after (into OUT). */
typedef struct {
    FILE *in;
    FILE *out;
    u8    ram[RAM_SIZE];
    int   cases;
} GameDump;

static void dump_probe(HzGame *g, bool after, void *user)
{
    GameDump *d = user;
    store(g, d->ram);
    if (!after) {
        fputc(1, d->in);
        fwrite(d->ram, 1, RAM_SIZE, d->in);
    } else {
        fwrite(d->ram, 1, RAM_SIZE, d->out);
        d->cases++;
    }
}

static u32 rng = 1;

static int rng_below(int n)
{
    rng = rng * 1103515245u + 12345u;
    return (int)(((rng >> 8) & 0xFFFFFF) % (u32)n);
}

static void frame_hook(HzGame *g, void *user)
{
    (void)g;
    (void)user;
}

/* A game on `map`, `mode`, `difficulty` for `frames` frames, nobody at the
 * pads. With `visits`, every $40 frames P1's mech is put down as a robot
 * beside one of the other side's units (or its base), so they have
 * something to see, chase and shoot. */
static int run_game(const HzAssets *assets, int map, int mode, int difficulty,
                    int frames, bool visits, const char *in, const char *out)
{
    static Video video;
    static HzGame game;
    static GameDump d;
    d.in = fopen(in, "wb");
    d.out = fopen(out, "wb");
    if (d.in == NULL || d.out == NULL) {
        return 1;
    }
    video_init(&video);
    game_init(&game, assets, &video);
    HzGame *g = &game;
    g->frame = frame_hook;
    g->options[0] = (u8)mode;
    g->options[1] = (u8)difficulty;
    g->options[3] = (u8)map;
    if (!game_new(g)) {
        return 1;
    }
    game_start(g);
    g->unit_ai_probe = dump_probe;
    g->unit_ai_probe_user = &d;
    for (int f = 0; f < frames; f++) {
        if (visits && f % 0x40 == 0x20) {
            HzMech *m = &g->mech[0];
            int pick = -1;
            for (int tries = 0; tries < 20 && pick < 0; tries++) {
                int i = HZ_UNITS / 2 + rng_below(HZ_UNITS / 2);
                if (rom_unit(g, i)->flags & 0x80) {
                    pick = i;
                }
            }
            if (pick >= 0 && !(m->flags & 0x20)) {
                m->form = 0;
                m->altitude = 0;
                m->x = (s16)(rom_unit(g, pick)->x + rng_below(0x70) - 0x38);
                m->y = (s16)(rom_unit(g, pick)->y + rng_below(0x70) - 0x38);
            }
        }
        if (game_frame(g, true) != HZ_LOOP_ON) {
            break;
        }
    }
    fclose(d.in);
    fclose(d.out);
    fprintf(stderr, "hz_unitai: map %d mode %d difficulty %d: %d updates\n",
            map, mode, difficulty, d.cases);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 10 && strcmp(argv[2], "--game") == 0) {
        static HzAssets assets;
        if (!hz_assets_load(&assets, argv[1])) {
            return 1;
        }
        rng = (u32)atoi(argv[6]) * 7919u + 1u;
        return run_game(&assets, atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                        atoi(argv[6]), argv[7][0] == '1', argv[8], argv[9]);
    }
    if (argc != 4) {
        fprintf(stderr, "usage: hz_unitai ASSETS IN OUT\n"
                "       hz_unitai ASSETS --game MAP MODE DIFFICULTY FRAMES "
                "VISITS(0/1) IN OUT\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    FILE *in = fopen(argv[2], "rb");
    FILE *out = fopen(argv[3], "wb");
    if (in == NULL || out == NULL) {
        return 1;
    }
    static Video video;
    static HzGame game;
    static u8 ram[RAM_SIZE];
    video_init(&video);
    int cases = 0;
    for (;;) {
        int calls = fgetc(in);
        if (calls == EOF) {
            break;
        }
        if (fread(ram, 1, RAM_SIZE, in) != RAM_SIZE) {
            fprintf(stderr, "hz_unitai: short case %d\n", cases);
            return 1;
        }
        game_init(&game, &assets, &video);
        load(&game, ram);
        for (int k = 0; k < calls; k++) {
            unit_ai_update(&game);
        }
        store(&game, ram);
        fwrite(ram, 1, RAM_SIZE, out);
        cases++;
    }
    fclose(in);
    fclose(out);
    return 0;
}
