/* hz_mechai: the CPU mech's AI (src/game/mech_ai.c) in real games, for
 * tools/hzmechai_check.py, which runs the original AI_MechThink
 * (tools/m68krun.py) on the same states and compares.
 *
 *   hz_mechai ASSETS SEED GAMES FRAMES SAMPLE IN OUT [POKES AI_POKES]
 *
 * Plays GAMES games of FRAMES frames each, the map, mode (always at least
 * one CPU), difficulty and the human's habits picked by SEED (POKES and
 * AI_POKES, if given, fix how often pokes come: % of frames, and per 1000
 * frames for each CPU). A human side
 * gets random pads; now and then (pokes, below) the game is pushed
 * towards what the AI tests: money, fuel, HP and ammo low, the human's
 * mech moved near the CPU's, its HQ hurt, missiles homing in. Every
 * AI_MechThink call in a state (plan, step, mode, substep, strategy, move
 * state, form, alert) seen fewer than 24 times so far, and one in SAMPLE
 * of the rest, is a case: the state before (IN) and after (OUT), as the
 * 68k's RAM holds it.
 *
 * IN: per game "HZM1", the map blocks ($4000 bytes, as v_map_blocks); per
 * case a byte 1, the mech (0/1), the AI's work kind and service stage
 * before, then the layout ($2000), the posts ($A0), both mechs ($200), the
 * units ($1F40), the grid ($1000), v_radar_buf1/2 ($1000), the RNG seed
 * (4), v_unit_count (2), v_map (2). A byte 0 ends the game.
 * OUT: per case the work kind and stage after, both mechs, the units and
 * the seed.
 *
 * store_mech() is where the port's readable state meets the 68k's bytes:
 * the AI's fields (HzMechAI) at +$80-$FF as the original lays them out,
 * records and units as the pointers it keeps. */

#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/romcodes.h"
#include "../src/game/tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A_POSTS  0x89B4
#define A_UNITS  0x8D94

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

static u16 rec_ptr(u8 i)
{
    return (u16)(A_POSTS + i * 8);
}

static void store_ai(const HzMechAI *ai, u8 *r)
{
    wr16(r, 0x80, ai->frames);
    r[0x82] = ai->st_map ? 0xFF : 0;
    r[0x83] = ai->misses_left;
    r[0x84] = ai->reorder;
    r[0x85] = ai->order_tries;
    wr16(r, 0x86, (u16)ai->goal.x);
    wr16(r, 0x88, (u16)ai->goal.y);
    r[0x8A] = ai->heading;
    r[0x8B] = ai->unused_flag;
    r[0x8C] = ai->hold;
    r[0x8D] = ai->buy_count;
    r[0x8E] = ai->move;
    r[0x8F] = rom_grid_id(ai->target);
    memcpy(r + 0x90, ai->marks, HZ_AI_RECORDS);
    r[0xA2] = ai->dropped;
    r[0xA3] = ai->lifted;
    r[0xA4] = ai->edge_steer;
    r[0xA5] = ai->buy_kind;
    r[0xA6] = ai->saved_step;
    r[0xA7] = ai->saved_plan;
    wr16(r, 0xA8, rec_ptr(ai->dest));
    wr16(r, 0xAA, rec_ptr(ai->from));
    wr16(r, 0xAC, rec_ptr(ai->next_dest));
    wr16(r, 0xAE, rec_ptr(ai->next_from));
    wr32(r, 0xB0, AI_BUY_SCRIPTS_ADDR + ai->buy_script);
    wr16(r, 0xB4, (u16)ai->saved_goal.x);
    wr16(r, 0xB6, (u16)ai->saved_goal.y);
    r[0xB8] = ai->countdown;
    r[0xB9] = ai->alert_post;
    r[0xBA] = ai->refuel;
    r[0xBB] = ai->route;
    r[0xBC] = ai->scan_dir;
    r[0xBD] = ai->aim_dir;
    r[0xBE] = ai->fight_form;
    r[0xBF] = ai->pad_dir8;
    for (int i = 0; i < 8; i++) {
        wr16(r, (u32)(0xC0 + i * 2), ai->work.raw[i]);
    }
    if (ai->work.kind == WORK_SERVICE) {
        wr16(r, 0xC0, rom_addr(ai->service_unit));
        if (ai->work.service_stage == SERVICE_POST) {
            wr16(r, 0xC8, rec_ptr((u8)ai->work.service.post));
        }
    }
    r[0xD0] = ai->launches;
    r[0xD1] = (u8)ai->attack_buy;
    r[0xD4] = ai->own_near;
    r[0xD5] = ai->enemy_near;
    r[0xD6] = ai->respawn;
    r[0xD7] = ai->detour;
    wr16(r, 0xD8, ai->hq_hp);
    r[0xDA] = ai->alert_taken;
    r[0xDB] = ai->alert;
    r[0xDC] = ai->hq_attack;
    r[0xDD] = ai->hq_run;
    r[0xDE] = ai->engage;
    wr16(r, 0xE0, ai->engage_range);
    r[0xE2] = ai->needs_service;
    r[0xE3] = ai->course;
    r[0xE4] = ai->hq_drop;
    r[0xE5] = ai->hq_drop_end;
    r[0xE6] = ai->post_drop;
    r[0xE7] = ai->post_drop_end;
    r[0xE8] = ai->bought;
    r[0xE9] = ai->reset_count;
    r[0xEA] = ai->deaths;
    r[0xEB] = ai->launch_deaths;
    r[0xEC] = ai->ground_shot;
    r[0xED] = ai->watch_post;
    r[0xEF] = ai->dodge;
    r[0xF0] = ai->strategy;
    r[0xF1] = ai->mode;
    r[0xF2] = ai->substep;
    r[0xF3] = ai->plan;
    r[0xF4] = ai->step;
    r[0xF5] = ai->style;
    r[0xF6] = ai->dest_class;
    r[0xF7] = ai->enemy_heading;
    wr16(r, 0xF8, rec_ptr(ai->pickup_post));
    r[0xFA] = ai->buy_variant;
    r[0xFB] = ai->shot_wait;
    r[0xFC] = ai->deliver_dir;
    r[0xFD] = ai->raid_map ? 0xFF : 0;
    r[0xFE] = ai->launch_at_from;
    r[0xFF] = ai->launch_at_dest;
}

/* Everything of a mech the AI reads or writes. */
static void store_mech(const HzMech *m, u8 *r)
{
    memset(r, 0, 0x100);
    r[0x00] = m->flags;
    r[0x01] = (u8)m->carried_slot;
    r[0x02] = m->carried_order;
    r[0x03] = m->dir;
    r[0x04] = m->turn_to;
    r[0x06] = m->transform;
    r[0x09] = rom_victim(m->hit_victim);
    wr16(r, 0x0A, m->hp);
    wr16(r, 0x10, (u16)m->x);
    wr16(r, 0x12, (u16)m->y);
    r[0x19] = m->ammo;
    wr16(r, 0x1A, m->fuel);
    wr16(r, 0x1C, m->terrain);
    r[0x22] = m->shot_flags;
    r[0x23] = m->blocked;
    r[0x28] = m->pad;
    r[0x29] = m->pad_prev;
    wr16(r, 0x2C, m->form);
    r[0x40] = m->menu.step;
    r[0x42] = (u8)m->view;
    r[0x48] = m->menu.row;
    r[0x4A] = m->menu.type;
    r[0x4C] = m->menu.order;
    r[0x4F] = m->prod_state;
    r[0x50] = m->menu.reorder_state;
    wr16(r, 0x54, m->money);
    wr16(r, 0x64, m->option2);
    wr16(r, 0x6A, m->missile_dirs);
    r[0x6D] = m->menu.closing;
    store_ai(&m->ai, r);
}

/* Everything of a unit the AI reads or writes. */
static void store_unit(const HzUnit *u, u8 *r)
{
    memset(r, 0, 0x50);
    r[0x00] = u->flags;
    r[0x01] = u->type;
    r[0x02] = u->order;
    r[0x04] = u->turn_to;
    wr16(r, 0x0A, u->hp);
    wr16(r, 0x10, (u16)u->x);
    wr16(r, 0x12, (u16)u->y);
    r[0x18] = u->missiles;
    r[0x19] = u->ammo;
    wr16(r, 0x1A, u->fuel);
    wr16(r, 0x1C, u->terrain);
    r[0x42] = u->ai.dest_dir;
}

typedef struct {
    FILE *in;
    FILE *out;
    long  cases;
    long  calls;
    long  plans[0x12];
    int   sample;       /* 1 in this many calls past the first few of a
                           state are kept */
    bool  keeping;      /* this call's case is being written */
    u16   seen[0x10000];/* calls kept per state (hashed) */
} Dump;

static int rnd(int n);

static void write_varying(const HzGame *g, FILE *f, bool before)
{
    static u8 buf[0x2000];
    if (before) {
        for (int row = 0; row < 64; row++) {
            for (int col = 0; col < 64; col++) {
                wr16(buf, (u32)((row * 64 + col) * 2), g->layout[row][col]);
            }
        }
        fwrite(buf, 1, 0x2000, f);
        for (int i = 0; i < HZ_POST_CLEARED; i++) {
            u8 *p = buf + i * 8;
            p[0] = g->posts[i].flags;
            p[1] = g->posts[i].b1;
            p[2] = g->posts[i].hold[0];
            p[3] = g->posts[i].hold[1];
            wr16(p, 4, (u16)g->posts[i].x);
            wr16(p, 6, (u16)g->posts[i].y);
        }
        fwrite(buf, 1, HZ_POST_CLEARED * 8, f);
    }
    for (int p = 0; p < 2; p++) {
        store_mech(&g->mech[p], buf);
        fwrite(buf, 1, 0x100, f);
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        store_unit(rom_unit_c(g, i), buf);
        fwrite(buf, 1, 0x50, f);
    }
    if (before) {
        for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
            fputc(rom_grid_id(g->grid[i]), f);
        }
        fwrite(g->radar_buf, 1, sizeof g->radar_buf, f);
    }
    wr32(buf, 0, g->rng_seed);
    fwrite(buf, 1, 4, f);
    if (before) {
        const u8 counts[2] = { (u8)g->unit_count[0], (u8)g->unit_count[1] };
        fwrite(counts, 1, 2, f);        /* v_unit_count: bytes */
        wr16(buf, 0, g->map);
        fwrite(buf, 1, 2, f);
    }
}

/* A state's hash: what the AI does next depends most on these. */
static u16 state_key(const HzMech *m)
{
    const HzMechAI *ai = &m->ai;
    u32 k = (u32)ai->plan * 0x9E3779B1u;
    k = (k ^ ai->step) * 0x85EBCA6Bu;
    k = (k ^ ai->mode) * 0xC2B2AE35u;
    k = (k ^ ai->substep) * 0x27D4EB2Fu;
    k = (k ^ ai->strategy) * 0x165667B1u;
    k = (k ^ ai->move) * 0x9E3779B1u;
    k = (k ^ (u32)(m->form & 3) ^ ((u32)(ai->alert >> 4) << 2)) * 0x85EBCA6Bu;
    return (u16)(k >> 16);
}

static void dump_probe(HzGame *g, int p, bool after, void *user)
{
    Dump *d = user;
    const HzMechAI *ai = &g->mech[p].ai;
    if (!after) {
        d->calls++;
        u16 key = state_key(&g->mech[p]);
        d->keeping = d->seen[key] < 24 || rnd(d->sample) == 0;
        if (!d->keeping) {
            return;
        }
        if (d->seen[key] < 0xFFFF) {
            d->seen[key]++;
        }
        fputc(1, d->in);
        fputc(p, d->in);
        fputc(ai->work.kind, d->in);
        fputc(ai->work.service_stage, d->in);
        write_varying(g, d->in, true);
    } else if (d->keeping) {
        fputc(ai->work.kind, d->out);
        fputc(ai->work.service_stage, d->out);
        write_varying(g, d->out, false);
        d->cases++;
        if (ai->plan < 0x12) {
            d->plans[ai->plan]++;
        }
    }
}

/* -- games -------------------------------------------------------------- */

static u32 rng = 1;

static int rnd(int n)
{
    rng = rng * 1103515245u + 12345u;
    return (int)(((rng >> 8) & 0xFFFFFF) % (u32)n);
}

static bool chance(int percent)
{
    return rnd(100) < percent;
}

static void frame_hook(HzGame *g, void *user)
{
    (void)g;
    (void)user;
}

/* A human's pad: held a while, then something else. */
typedef struct {
    HzPad pad;
    int   left;
    int   busy;
} Human;

static void human_pad(Human *h)
{
    if (h->left-- > 0) {
        return;
    }
    h->left = rnd(40);
    memset(&h->pad, 0, sizeof h->pad);
    if (!chance(h->busy)) {
        return;
    }
    int d = rnd(9);
    h->pad.up = d == 0 || d == 4 || d == 5;
    h->pad.down = d == 1 || d == 6 || d == 7;
    h->pad.left = d == 2 || d == 4 || d == 6;
    h->pad.right = d == 3 || d == 5 || d == 7;
    h->pad.b = chance(30);
    h->pad.a = chance(8);
    if (chance(4)) {
        h->pad.c = true;
        h->left = 0;
    }
}

/* Pokes towards what the AI tests. */
static void poke(HzGame *g)
{
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        if (!(m->flags & 0x80) || (m->flags & 0x20)) {
            continue;
        }
        const HzUnitStats *k = &g->assets->mech_stats.stats;
        switch (rnd(14)) {
        case 0:
            m->fuel = (u16)(k->fuel / 4 + rnd(5) - 2);
            break;
        case 1:
            m->hp = (u16)(chance(50) ? 0x42 + rnd(5) - 2
                                     : k->hp / 4 + rnd(5) - 2);
            break;
        case 2:
            m->ammo = (u8)(chance(50) ? 0 : k->ammo / 8 + rnd(3) - 1);
            break;
        case 3:
            m->money = (u16)(chance(50) ? rnd(200) : rnd(6000));
            break;
        case 4: {                       /* near the other mech */
            const HzMech *o = &g->mech[1 - p];
            if (!(o->flags & 0x20) && !(m->flags & 0x01)) {
                m->x = (s16)(o->x + rnd(0x180) - 0xC0);
                m->y = (s16)(o->y + rnd(0x180) - 0xC0);
                if (m->x < 0x10) {
                    m->x = 0x10;
                }
                if (m->y < 0x10) {
                    m->y = 0x10;
                }
                if (m->x > 0x7C0) {
                    m->x = 0x7C0;
                }
                if (m->y > 0x7C0) {
                    m->y = 0x7C0;
                }
            }
            break;
        }
        case 5: {                       /* its HQ hurt */
            HzPost *h = &g->posts[HZ_POST_HQ + p];
            u16 hp = post_hq_hp(h);
            if (hp > 0x80) {
                hp = (u16)(hp - rnd(0x40));
                post_set_hq_hp(h, hp);
            }
            break;
        }
        case 6:
            m->missile_dirs = (u16)(1u << rnd(16));
            break;
        default:
            break;
        }
    }
}

/* Pokes into a CPU's AI, towards the paths real games reach rarely: a
 * buy of any kind (so every delivery plan runs), a unit to service, an
 * alert, a strategy step, a route substep, a move state, a death. Only
 * values the AI itself could hold. */
static int random_own_unit(HzGame *g, int team)
{
    for (int tries = 0; tries < 30; tries++) {
        int i = team * 50 + rnd(50);
        if ((rom_unit(g, i)->flags & 0x80) && !(rom_unit(g, i)->flags & 0x2C)) {
            return i;
        }
    }
    return -1;
}

static void poke_ai(HzGame *g, int p)
{
    HzMech *m = &g->mech[p];
    HzMechAI *ai = &m->ai;
    int team = (m->flags & 0x10) ? 1 : 0;
    if (!(m->flags & 0x80) || (m->flags & 0x20) || ai->mode == 0
        || ai->plan == PLAN_BUY || (m->flags & 0x40)) {
        return;
    }
    switch (rnd(19)) {
    case 17:
    case 18: {                          /* plan $B part way through */
        HzMechAI save = *ai;
        ai->work.kind = WORK_SERVICE;
        ai->saved_plan = PLAN_TEND_UNITS;
        ai->saved_step = 0;
        ai->plan = PLAN_SERVICE_UNIT;
        ai->watch_next = (u8)(1 + rnd(9));
        ai->drop_tries = (u8)(chance(50) ? 0x10 : 1 + rnd(15));
        if (m->form == HZ_FORM_CARRYING) {
            static const u8 STEPS[] = { 4, 5, 6, 7, 8, 9, 10, 12 };
            const HzPost *post = &g->posts[16 + team];
            ai->step = STEPS[rnd(8)];
            ai->service_unit = ref_unit(team, m->carried_slot % 50);
            ai->work.service.post = (u16)(16 + team);
            ai->work.service_stage = SERVICE_POST;
            if (ai->step == 6 || ai->step == 7) {
                m->x = (s16)(post->x - 8);
                m->y = (s16)(post->y - 8);
            } else if (ai->step >= 8 && ai->step <= 10) {
                ai->work.service_stage = SERVICE_RETURN;
                ai->work.service_return.at.x = (s16)(m->x + rnd(0xC0) - 0x60);
                ai->work.service_return.at.y = (s16)(m->y + rnd(0xC0) - 0x60);
            }
            if (ai->step == 7 && chance(50)) {
                HzUnit *u = ref_unit_ptr(g, ai->service_unit);
                u->fuel = (u16)(u->fuel / 2);
                u->hp = (u16)(u->hp / 2);
            }
        } else {
            int u = random_own_unit(g, team);
            if (u < 0) {
                *ai = save;
                break;
            }
            ai->step = (u8)rnd(4);
            ai->service_unit = ref_unit(u / 50, (u32)(u % 50));
            ai->work.service_stage = SERVICE_UNIT;
            if (ai->step == 1 || chance(30)) {
                m->x = (s16)(rom_unit(g, u)->x - rnd(0x20) + 8);
                m->y = (s16)(rom_unit(g, u)->y - rnd(0x20) + 8);
            }
        }
        break;
    }
    case 15: {                          /* plan 5 part way through */
        static const u8 STEPS[] = { 0, 2, 3, 3, 5, 5, 7, 8 };
        HzMech *o = &g->mech[1 - p];
        ai->plan = PLAN_GROUND;
        ai->step = STEPS[rnd(8)];
        ai->saved_plan = 0;
        ai->attack_here = (u8)(chance(30) ? 0xFF : 0);
        ai->heading = (u8)(chance(30) ? 0xFF : 0);
        ai->frames_left = (u8)(chance(50) ? 0 : rnd(4));
        ai->misses_left = (u8)(1 + rnd(3));
        ai->ground_shot = (u8)(chance(50) ? 0 : 0xFF);
        ai->dropped = (u8)(chance(50) ? 0 : 0xFF);
        int u = random_own_unit(g, 1 - team);
        ai->target = (u >= 0 && chance(60))
                     ? ref_unit(u / 50, (u32)(u % 50))
                     : ref_mech((o->flags & 0x10) ? 1 : 0);
        ai->goal.x = (s16)(m->x + rnd(0x80) - 0x40);
        ai->goal.y = (s16)(m->y + rnd(0x80) - 0x40);
        ai->aim_dir = (u8)rnd(16);
        ai->work.kind = WORK_BOX;
        ai->work.box.left = (s16)(ai->goal.x - 0x40);
        ai->work.box.right = (s16)(ai->goal.x + 0x40);
        ai->work.box.top = (s16)(ai->goal.y - 0x40);
        ai->work.box.bottom = (s16)(ai->goal.y + 0x40);
        if (chance(50)) {
            m->pad &= 0xF0;
        }
        break;
    }
    case 16:                            /* plan 4 turning to its heading */
        ai->plan = PLAN_FLY;
        ai->step = 3;
        ai->heading = (u8)(chance(80) ? (m->dir + 8 - rnd(3) + 1) & 0x0F
                                      : 0xFF);
        ai->goal.x = m->x;
        ai->goal.y = m->y;
        ai->frames = (u16)(ai->frames & ~3u);
        break;
    case 14: {                          /* at the move states' edges */
        const HzUnitStats *k = &g->assets->mech_stats.stats;
        if (chance(50)) {               /* state 5 under an alert */
            ai->move = 5;
            ai->alert = 0x81;
            ai->alert_taken = (u8)(chance(50) ? 0xFF : 0);
            m->fuel = (u16)(k->fuel / 2 + rnd(3) - 1);
            m->ammo = (u8)(k->ammo / 2 + rnd(3) - 1);
            ai->saved_goal = ai->goal;
        } else {                        /* a fight near its HP floor */
            ai->move = 6;
            ai->alert_taken = 0;
            m->hp = (u16)(0x40 + rnd(4));
            HzMech *o = &g->mech[1 - p];
            if (!(o->flags & 0x20)) {
                o->x = (s16)(m->x + 0x30);
                o->y = m->y;
                if (o->x > 0x7C0) {
                    o->x = (s16)(m->x - 0x30);
                }
            }
        }
        break;
    }
    case 13:                            /* back from a death */
        ai->respawn = 1;
        ai->deaths = (u8)rnd(4);
        ai->launch_deaths = (u8)rnd(4);
        ai->bought = (u8)(chance(30) ? 14 + rnd(3) : rnd(14));
        ai->dest = (u8)rnd(18);
        break;
    case 10:                            /* missiles homing in */
        m->missile_dirs = (u16)(1u << ((m->dir + rnd(11) - 5) & 0x0F));
        break;
    case 11:
    case 12: {                          /* the other mech close by */
        HzMech *o = &g->mech[1 - p];
        if (!(o->flags & 0x20)) {
            int r = 0x20 + rnd(0x90);
            o->x = (s16)(m->x + (chance(50) ? r : -r));
            o->y = (s16)(m->y + rnd(2 * r) - r);
            if (o->x < 0x10) {
                o->x = 0x10;
            }
            if (o->y < 0x10) {
                o->y = 0x10;
            }
            if (o->x > 0x7C0) {
                o->x = 0x7C0;
            }
            if (o->y > 0x7C0) {
                o->y = 0x7C0;
            }
            if (chance(50)) {
                ai->move = 1;           /* look at it next */
            }
        }
        break;
    }
    case 0:
    case 1:                             /* a buy of any kind */
        ai->plan = PLAN_MODES;
        ai->step = 0;
        ai->saved_plan = 0;
        ai->mode = 2;
        ai->substep = 2;
        ai->buy_kind = (u8)rnd(8);
        ai->buy_count = (u8)(1 + rnd(2));
        ai->buy_variant = (u8)rnd(4);
        ai->attack_buy = (u8)rnd(2);
        ai->dest = (u8)rnd(18);
        ai->strategy = (u8)(chance(50) ? 0x0C : 5);
        ai->launch_at_dest = chance(20) ? 0xFF : 0;
        m->money = (u16)(m->money + 3000);
        break;
    case 2: {                           /* a unit to service */
        int u = random_own_unit(g, team);
        if (u < 0) {
            break;
        }
        if (chance(50)) {
            ai->plan = PLAN_TEND_UNITS;
            ai->step = 0;
            ai->tend_cursor = (u8)rnd(50);
            ai->goal.x = m->x;
            ai->goal.y = m->y;
        } else {
            ai->work.kind = WORK_SERVICE;
            ai->work.service_stage = SERVICE_UNIT;
            ai->service_unit = ref_unit(u / 50, (u32)(u % 50));
            ai->plan = PLAN_SERVICE_UNIT;
            ai->step = 0;
            ai->saved_plan = PLAN_TEND_UNITS;
            ai->saved_step = 0;
        }
        ai->hq_attack = chance(20) ? 0xFF : 0;
        break;
    }
    case 3: {                           /* an alert */
        static const u8 ALERTS[] = { 0x01, 0x80, 0x81, 0xEE };
        ai->alert_taken = 0;
        ai->alert = ALERTS[rnd(4)];
        ai->alert_post = (u8)rnd(16);
        break;
    }
    case 4:
    case 9:                             /* a strategy step */
    {
        static const u8 STEPS[] = { 6, 7, 8, 9, 10, 11, 12, 13, 18, 20 };
        static const u8 BOUGHT[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 14, 15 };
        ai->plan = PLAN_MODES;
        ai->step = 0;
        ai->mode = 1;
        ai->strategy = (u8)(chance(60) ? STEPS[rnd(10)] : rnd(21));
        ai->dest = (u8)(chance(60) ? rnd(16) : rnd(18));
        ai->from = (u8)(chance(50) ? 16 + team : rnd(18));
        ai->next_dest = (u8)rnd(18);
        ai->next_from = (u8)rnd(18);
        ai->style = (u8)rnd(4);
        ai->own_near = (u8)(chance(50) ? rnd(10)
                                       : (ai->style + 2 > rnd(4)
                                          ? ai->style + 2 - rnd(4) : 0));
        ai->enemy_near = (u8)(chance(40) ? 0 : chance(50) ? ai->own_near
                                                          : rnd(6));
        ai->bought = BOUGHT[rnd(12)];
        ai->launch_at_from = chance(20) ? 0xFF : 0;
        ai->launch_at_dest = chance(20) ? 0xFF : 0;
        ai->launches = (u8)(chance(50) ? 9 + rnd(3) : rnd(12));
        ai->hq_run = chance(30) ? (chance(50) ? 0xFF : 0x80) : 0;
        ai->countdown = (u8)(chance(50) ? 1 + rnd(3) : rnd(256));
        if (chance(30)) {
            ai->alert_taken = 0xFF;
        }
        ai->marks[ai->dest] = (u8)(chance(50) ? 0 : rnd(256));
        ai->hops = (u8)rnd(3);
        if (chance(30) && ai->dest < 16) {
            HzPost *r = &g->posts[ai->dest];
            r->hold[0] = (u8)(chance(50) ? 0 : rnd(5));
            r->hold[1] = (u8)(chance(50) ? 0 : rnd(5));
        }
        break;
    }
    case 5:                             /* a route substep */
        ai->plan = PLAN_MODES;
        ai->step = 0;
        ai->mode = 3;
        ai->substep = (u8)rnd(4);
        ai->route = 0xFF;
        ai->dest = (u8)rnd(18);
        ai->from = (u8)rnd(18);
        ai->style = (u8)rnd(4);
        if (chance(30)) {
            m->fuel = (u16)rnd(400);
        }
        break;
    case 6:                             /* a move state */
        ai->move = (u8)rnd(16);
        ai->hold = (u8)(chance(70) ? 0 : rnd(3));
        ai->fight_form = (u8)rnd(3);
        ai->dodge = (u8)(chance(70) ? 0 : 1 + rnd(16));
        ai->saved_goal = ai->goal;
        break;
    case 7:                             /* reset mode: plan 5 */
        ai->plan = PLAN_MODES;
        ai->mode = 4;
        ai->dest = (u8)rnd(18);
        break;
    case 8:
    default:                            /* the delivery plans' watch */
        if (m->form == HZ_FORM_CARRYING) {
            static const u8 PLANS[] = { 6, 7, 0x0A, 0x0E, 0x0F, 0x0D, 0x10 };
            ai->plan = PLANS[rnd(7)];
            ai->step = 0;
            ai->dest = (u8)rnd(18);
            ai->heading = (u8)(chance(50) ? 0xFF : rnd(8));
        }
        break;
    }
}

static int run_game(const HzAssets *assets, Dump *d, int map, int mode,
                    int difficulty, int frames, int pokes, int ai_pokes)
{
    static Video video;
    static HzGame game;
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

    fwrite("HZM1", 1, 4, d->in);
    static u8 buf[HZ_BLOCKS * 32];
    for (int b = 0; b < HZ_BLOCKS; b++) {
        for (int i = 0; i < 16; i++) {
            wr16(buf, (u32)(b * 32 + i * 2), g->blocks[b][i]);
        }
    }
    fwrite(buf, 1, sizeof buf, d->in);

    g->mech_ai_probe = dump_probe;
    g->mech_ai_probe_user = d;
    Human h[2] = { { .busy = 50 + rnd(50) }, { .busy = 50 + rnd(50) } };
    for (int f = 0; f < frames; f++) {
        for (int p = 0; p < 2; p++) {
            human_pad(&h[p]);
            g->pad_in[p] = h[p].pad;
            if (g->pad_in[p].start) {
                g->pad_in[p].start = false;
            }
        }
        if (pokes && chance(pokes)) {
            poke(g);
        }
        for (int p = 0; p < 2; p++) {
            if (ai_pokes && (g->mech[p].flags & 0x01) && rnd(1000) < ai_pokes) {
                poke_ai(g, p);
            }
        }
        HzLoopExit e = game_frame(g, true);
        if (e != HZ_LOOP_ON) {
            break;
        }
    }
    g->mech_ai_probe = NULL;
    fputc(0, d->in);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 8 && argc != 10) {
        fprintf(stderr, "usage: hz_mechai ASSETS SEED GAMES FRAMES SAMPLE "
                "IN OUT\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    int seed = atoi(argv[2]);
    int games = atoi(argv[3]);
    int frames = atoi(argv[4]);
    static Dump d;
    d.sample = atoi(argv[5]) > 0 ? atoi(argv[5]) : 1;
    d.in = fopen(argv[6], "wb");
    d.out = fopen(argv[7], "wb");
    if (d.in == NULL || d.out == NULL) {
        return 1;
    }
    rng = (u32)seed * 2654435761u + 7u;
    static const int MODES[] = { 1, 2, 3, 5, 6, 7 };
    for (int i = 0; i < games; i++) {
        int map = rnd(8);
        int mode = MODES[rnd(6)];
        int diff = rnd(4);
        int pokes = chance(50) ? 0 : 1 + rnd(4);
        int ai_pokes = chance(40) ? 0 : 1 + rnd(6);
        if (argc == 10) {
            pokes = atoi(argv[8]);
            ai_pokes = atoi(argv[9]);
        }
        if (run_game(&assets, &d, map, mode, diff, frames, pokes, ai_pokes)
            != 0) {
            return 1;
        }
        fprintf(stderr, "hz_mechai: game %d: map %d mode %d difficulty %d"
                " pokes %d%% AI pokes %d/1000\n", i, map, mode, diff, pokes,
                ai_pokes);
    }
    fclose(d.in);
    fclose(d.out);
    fprintf(stderr, "hz_mechai: %ld calls, %ld cases; plans:", d.calls,
            d.cases);
    for (int i = 0; i < 0x12; i++) {
        fprintf(stderr, " %X:%ld", i, d.plans[i]);
    }
    fprintf(stderr, "\n");
    return 0;
}
