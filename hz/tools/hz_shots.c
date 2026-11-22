/* hz_shots: Shot_FireBullet, Shot_FireMissile and Combat_Update's shots on
 * made-up states round a point of a real map, for tools/hzshots_check.py:
 * the cases the game can't reach yet (units' shots and missiles, HQ hits)
 * and the edges (owners gone or dying, dying targets, every boost, life
 * and explosion counts, hits at the overlap limits, tiles that stop
 * shots, the grid's edges).
 *
 *   hz_shots ASSETS OUT.txt [--map N] [--cases N] [--seed N]
 *
 * Per case, numbers in hex: the state before ("M p" mech fields, "U k"
 * unit fields for each unit set or named as an owner, "G" the grid's
 * nonzero cells as cell/id pairs, "Q" the HQs' HP and alerts, "X"
 * globals, "B" the shots as index:fields), the action ("F shooter flags
 * dir x y speed life missile result", or "C" for Combat_Update), then the
 * same in lower case after it. */

#include "../src/core/hz_vdp.h"
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

static int pick(const int *v, int n)
{
    return v[rng_below(n)];
}

/* (Session 61) A shot's owner as the check numbers it (and as this tool
 * draws them): a unit's 68k record, team x 50 + slot; 100 + p a mech. */
#define OWNER_MECH 100

static HzRef owner_ref(int o)
{
    return (o >= OWNER_MECH) ? ref_mech(o - OWNER_MECH)
                             : ref_unit(o / ROM_SIDE_UNITS,
                                        (u32)(o % ROM_SIDE_UNITS));
}

static int owner_num(HzRef r)
{
    if (r.kind == REF_MECH) {
        return OWNER_MECH + r.side;
    }
    if (r.kind == REF_UNIT) {
        return rom_unit_index(r.side, r.slot);
    }
    return 0;
}

static void put_mech(FILE *f, char k, int p, const HzMech *m)
{
    fprintf(f, "%c %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n", k,
            p, m->flags, (unsigned)m->carried_slot, m->hp, m->size,
            (u16)m->x, (u16)m->y, m->ammo, m->missiles, m->power,
            m->altitude, m->form, (u16)m->vel_x, (u16)m->vel_y,
            m->shot_flags, rom_victim(m->hit_victim), m->missile_dirs);
}

static void put_unit(FILE *f, char k, int i, const HzUnit *u)
{
    fprintf(f, "%c %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n", k, i,
            u->flags, u->type, u->hp, u->size, (u16)u->x, (u16)u->y,
            u->ammo, u->missiles, u->power, u->raw_0d, u->overlay_attr,
            u->shot_flags, rom_victim(u->hit_victim), u->sprite_size,
            u->sprite_attr);
}

static void put_shots(FILE *f, char k, const HzGame *g)
{
    fputc(k, f);
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
    fputc('\n', f);
}

static u16 hq_hp(const HzGame *g, int p)
{
    return post_hq_hp(&g->posts[HZ_POST_HQ + p]);
}

static void put_rest(FILE *f, bool after, const HzGame *g)
{
    fprintf(f, "%c %X %X %X %X\n", after ? 'q' : 'Q', hq_hp(g, 0),
            hq_hp(g, 1), g->mech[0].hq_alert, g->mech[1].hq_alert);
    fprintf(f, "%c %X %X %X %X %X %X %X %X %X\n", after ? 'x' : 'X',
            g->options[1], g->cpu_boost, g->sfx_queued, g->sfx_priority,
            g->unit_team_toggle, g->hit_flags, rom_hit_id(g->hit_id),
            g->terrain_event,
            g->terrain_event2);
}

static bool logged_unit[HZ_UNITS];

static void put_state(FILE *f, bool after, const HzGame *g)
{
    for (int p = 0; p < 2; p++) {
        put_mech(f, after ? 'm' : 'M', p, &g->mech[p]);
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        if (logged_unit[i]) {
            put_unit(f, after ? 'u' : 'U', i, rom_unit_c(g, i));
        }
    }
    if (!after) {
        fputc('G', f);
        for (int c = 0; c < HZ_GRID * HZ_GRID; c++) {
            if (!ref_is_none(g->grid[c])) {
                fprintf(f, " %X %X", c, rom_grid_id(g->grid[c]));
            }
        }
        fputc('\n', f);
    }
    put_rest(f, after, g);
    put_shots(f, after ? 'b' : 'B', g);
}

/* Grid_MarkObj's cell for an object of `size` at (x, y). */
static u16 cell_of(s16 x, s16 y, u8 size)
{
    u16 h = (u16)(size >> 1);
    u16 row = (u16)(((u16)(y + h) & 0xFFE0) << 1);
    u16 col = (u16)((u16)(x + h) >> 5);
    return (u16)((row + col) & 0xFFF);
}

static s16 near(s16 c, int r)
{
    return (s16)(c + rng_below(2 * r + 1) - r);
}

/* A value near `edge` (+-2) half the time, else anything below `top`. */
static int edgy(int edge, int top)
{
    return rng_below(2) ? edge + rng_below(5) - 2 : rng_below(top);
}

static void make_case(HzGame *g, s16 cx, s16 cy)
{
    memset(g->shots, 0, sizeof g->shots);
    units_clear_all(g);
    for (int c = 0; c < HZ_GRID * HZ_GRID; c++) {
        g->grid[c] = ref_none();
    }
    memset(logged_unit, 0, sizeof logged_unit);

    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        m->flags = (u8)((rng_below(8) ? 0x80 : 0) | (rng_below(5) ? 0 : 0x20)
                        | (p ? 0x10 : 0) | (rng_below(3) ? 0 : 0x01)
                        | (rng_below(6) ? 0 : 0x08));
        m->carried_slot = (u32)rng_below(60);
        m->form = (u16)rng_below(3);
        m->altitude = (u8)(m->form ? (rng_below(3) ? 0x10 : rng_below(16)) : 0);
        m->hp = (u16)edgy(0x20, 0x300);
        m->size = 0x20;
        m->x = near(cx, 0x30);
        m->y = near(cy, 0x30);
        m->ammo = (u8)(rng_below(4) ? rng_below(4) : rng_below(256));
        m->missiles = (u8)rng_below(4);
        m->power = (u8)(rng_below(4) ? 0x20 : rng_below(256));
        m->vel_x = (s16)(rng_below(0x1000) - 0x800);
        m->vel_y = (s16)(rng_below(0x1000) - 0x800);
        m->shot_flags = (u8)rng_below(256);
        m->hit_victim = rom_victim_ref((u8)rng_below(256));
        m->missile_dirs = (u16)rng_below(0x10000);
        m->hq_alert = (u8)(rng_below(2) ? 0 : rng_below(256));
        if (m->form == 0 && rng_below(4)) {
            g->grid[cell_of(m->x, m->y, 0x20)] = ref_mech(p);
        }
    }

    static const int sizes[] = { 0x08, 0x10, 0x18, 0x20, 0x0C };
    for (int team = 0; team < 2; team++) {
        int n = rng_below(14);
        for (int k = 0; k < n; k++) {
            int slot = rng_below(50);
            int i = team * 50 + slot;
            HzUnit *u = rom_unit(g, i);
            u->flags = (u8)((rng_below(10) ? 0x80 : 0) | (rng_below(5) ? 0 : 0x20)
                            | (team ? 0x10 : 0) | (rng_below(12) ? 0 : 0x08)
                            | (rng_below(2) ? 0x40 : 0));
            u->type = (u8)rng_below(14);
            u->hp = (u16)edgy(0x20, 0x100);
            u->size = (u8)pick(sizes, 5);
            u->x = near(cx, 0x40);
            u->y = near(cy, 0x40);
            u->ammo = (u8)rng_below(3);
            u->missiles = (u8)rng_below(3);
            u->power = (u8)rng_below(256);
            u->raw_0d = (u8)(rng_below(4) ? 0 : 1 + rng_below(255));
            u->overlay_attr = (u16)(rng_below(2) ? 0 : rng_below(0x10000));
            u->shot_flags = (u8)rng_below(256);
            u->hit_victim = rom_victim_ref((u8)rng_below(256));
            u->sprite_size = (u16)(rng_below(16) << 8);
            u->sprite_attr = (u16)rng_below(0x10000);
            logged_unit[i] = true;
            if (rng_below(5)) {
                g->grid[cell_of(u->x, u->y, u->size)] =
                    ref_unit(team, (u32)slot);
            }
        }
    }

    for (int team = 0; team < 2; team++) {
        int nb = rng_below(16);
        int nm = rng_below(6);
        /* Now and then every bullet or missile in use but the last one or
         * two, or all of them: the slot search's end. */
        int full = rng_below(5) == 0 ? 1 + rng_below(2) : 0;
        if (full) {
            nb = 24 + 8;
            nm = 0;
        }
        for (int k = 0; k < nb + nm; k++) {
            bool missile = full ? k >= 24 : k >= nb;
            int i = team * 32 + (missile ? 24 + rng_below(8) : rng_below(24));
            if (full) {
                i = team * 32 + k;
                int last = missile ? 31 : 23;
                if (full == 2 && k % 32 >= last - rng_below(2)) {
                    continue;
                }
            }
            HzShot *s = &g->shots[i];
            u8 fl = (u8)((team ? 0x10 : 0) | (rng_below(3) ? 0 : 0x08)
                         | (rng_below(6) ? 0 : 0x01) | (rng_below(2) ? 0x40 : 0));
            if (missile || rng_below(3) == 0) {
                fl |= 0x04;
            }
            int st = rng_below(10);
            if (st == 0) {
                fl |= 0xA0;                     /* exploding */
            } else if (st == 1 && missile) {
                fl |= 0x20;                     /* b5 alone */
            } else if (st == 2) {
                fl &= 0x7F;                     /* free, other bits left */
            } else {
                fl |= 0x80;
            }
            s->flags = fl;
            s->dir = (u8)rng_below(16);
            s->x = near(cx, 0x48);
            s->y = near(cy, 0x48);
            s->speed = (u16)(rng_below(4) ? 0x40 : rng_below(0x100));
            s->sub_x = (u16)rng_below(0x100);
            s->sub_y = (u16)rng_below(0x100);
            s->life = (u16)((fl & 0x20) ? 1 + rng_below(4)
                            : (rng_below(4) ? 1 + rng_below(3) : rng_below(80)));
            s->attr = (u16)rng_below(0x10000);
            s->vel_x = (s16)(rng_below(0x1000) - 0x800);
            s->vel_y = (s16)(rng_below(0x1000) - 0x800);
            s->aim_x = rng_below(3) ? s->vel_x : (s16)(rng_below(0x1000) - 0x800);
            s->aim_y = rng_below(3) ? s->vel_y : (s16)(rng_below(0x1000) - 0x800);
            s->power = (u8)(rng_below(4) ? rng_below(256) : rng_below(6));
            int o = rng_below(3) ? OWNER_MECH + rng_below(2) : rng_below(HZ_UNITS);
            s->owner = owner_ref(o);
            if (o < HZ_UNITS) {
                logged_unit[o] = true;
            }
        }
    }

    static const int boosts[] = { 0, 0x8000, 0x8010, 0x0010 };
    g->options[1] = (u8)rng_below(4);
    g->cpu_boost = (u16)pick(boosts, 4);
    g->sfx_queued = (u8)(rng_below(2) ? 0xFF : rng_below(32));
    g->sfx_priority = (u8)(g->sfx_queued == 0xFF ? 0xFF : rng_below(256));
    g->unit_team_toggle = (u16)rng_below(0x10000);
    for (int p = 0; p < 2; p++) {
        u16 hp = (u16)edgy(4, 0x401);
        post_set_hq_hp(&g->posts[HZ_POST_HQ + p], hp);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hz_shots ASSETS OUT.txt [--map N] "
                        "[--cases N] [--seed N]\n");
        return 2;
    }
    int map = 0;
    int cases = 2000;
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
    static Vdp vdp;
    static HzGame game;
    vdp_reset(&vdp);
    game_init(&game, &assets, &vdp);
    game.options[0] = 0;
    game.options[3] = (u8)map;
    if (!game_new(&game)) {
        fclose(f);
        return 1;
    }
    fprintf(f, "HZS1 %X\n", map);
    /* GetDirection on its own: zero and tiny differences, the slope table's
     * steps, and the far ends of the words (where divs overflows). */
    static const int deltas[] = { 0, 1, -1, 2, -2, 3, -3, 5, 10, 23, 80,
                                  -80, 0x7FF, -0x800, 0x7FFF, -0x8000 };
    for (int k = 0; k < 4000; k++) {
        s16 x0 = (s16)rng_below(0x10000);
        s16 y0 = (s16)rng_below(0x10000);
        int dx = rng_below(2) ? pick(deltas, 16) : rng_below(0x10000) - 0x8000;
        int dy = rng_below(2) ? pick(deltas, 16) : rng_below(0x10000) - 0x8000;
        if (rng_below(4) == 0) {
            dy = dx * (rng_below(81) - 40) / 16 + rng_below(3) - 1;
        }
        s16 x1 = (s16)(x0 + dx);
        s16 y1 = (s16)(y0 + dy);
        fprintf(f, "D %X %X %X %X %X\n", (u16)x0, (u16)y0, (u16)x1, (u16)y1,
                get_direction(x0, y0, x1, y1));
    }
    HzGame *g = &game;
    for (int c = 0; c < cases; c++) {
        /* Round an HQ a third of the time (its ring of type-7 blocks), at
         * the map's edges now and then, else anywhere. */
        s16 cx, cy;
        int r = rng_below(6);
        if (r < 2) {
            const HzPost *hq = &g->posts[HZ_POST_HQ + r];
            cx = near(hq->x, 0x40);
            cy = near(hq->y, 0x40);
        } else if (r == 2) {
            cx = (s16)(rng_below(2) ? rng_below(0x30) - 0x10 : 0x7F0 + rng_below(0x30));
            cy = (s16)rng_below(0x800);
        } else {
            cx = (s16)rng_below(0x800);
            cy = (s16)rng_below(0x800);
        }
        make_case(g, cx, cy);
        int shooter = 0;
        u8 flags = 0;
        bool fire = rng_below(4) == 0;
        if (fire) {
            shooter = rng_below(2) ? OWNER_MECH + rng_below(2) : rng_below(HZ_UNITS);
            if (shooter < HZ_UNITS) {
                logged_unit[shooter] = true;
            }
            flags = (u8)(0x80 | (rng_below(2) ? 0x10 : 0) | (rng_below(2) ? 0x08 : 0));
        }
        put_state(f, false, g);
        if (fire) {
            u8 dir = (u8)rng_below(16);
            s16 x = near(cx, 0x20);
            s16 y = near(cy, 0x20);
            u16 speed = (u16)rng_below(0x100);
            u16 life = (u16)rng_below(0x100);
            int missile = rng_below(3) == 0;
            int res = missile
                ? shot_fire_missile(g, owner_ref(shooter), flags, dir, x, y,
                                    speed, life)
                : shot_fire_bullet(g, owner_ref(shooter), flags, dir, x, y,
                                   speed, life);
            fprintf(f, "F %X %X %X %X %X %X %X %X %X\n", shooter, flags, dir,
                    (u16)x, (u16)y, speed, life, missile, (u8)res);
        } else {
            combat_update(g);
            fprintf(f, "C\n");
        }
        put_state(f, true, g);
    }
    fclose(f);
    printf("map %d: %d cases\n", map, cases);
    return 0;
}
