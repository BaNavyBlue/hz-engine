/* hz_repair: the REPAIR patch (docs/repair.md; src/game/unit_ai.c's repair
 * truck and mechanics) on scenes set up on open ground, with no window and
 * no sound, each checked for what must happen:
 *
 *     hz_repair ASSETS [--map M] [--scene NAME] [--fixes HEX] [--trace]
 *               [--png PREFIX --png-every N]
 *
 *   shop    the truck is sold with the patch and not without, at
 *           repair.json's price, with its order
 *   buy     P1's pad: the order menu opened, the truck chosen and bought,
 *           built, picked up at the HQ, carried to the scene and put
 *           down, where its men mend a vehicle
 *   mend    a truck between two damaged vehicles: both its men out, both
 *           vehicles whole, the men in again, the truck where it was
 *   self    a damaged truck alone: its own men mend it
 *   drive   a damaged vehicle beyond its men's reach: the truck drives up
 *           and parks, the vehicle is mended
 *   alarm   an enemy comes near a man at work: the men in, the truck off
 *           towards home; the enemy gone, it comes back and finishes
 *   wreck   the truck destroyed with its men out: two INFANTRY
 *   crew    a man killed: the truck a man short, the other in, the truck
 *           home, and its crew whole again there
 *   lift    the truck carried off with its men out (as a mech's lift
 *           leaves the records): they wait; put down, they come to it
 *   full    (without UNITS) the side's 50 units: no man comes out, and
 *           nothing breaks
 *
 * Every scene (default: all) is played on the map (default 4, STADT) with
 * the PATCHES page's default and REPAIR, then with UNITS too (the grid's
 * lists checked every frame), then with PATHS too; --fixes plays one set.
 * A line a scene on stdout, "ok" or what failed; exit 1 if any failed.
 * --trace: the truck, its men and the vehicles every 12 frames on stderr.
 * --png: the screen every N frames, PREFIX_SCENE_FRAME.png (P1's jet is
 * flown over the scene first). */

#include "../src/core/hz_vdp.h"
#include "../src/core/pngwrite.h"
#include "../src/game/game.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    HzGame *g;
    int     frames;
    bool    trace;
    const char *png;
    int     png_every;
    const char *scene;
    int     ox, oy;             /* the scene's ground: its top-left */
    bool    fly;                /* P1's jet to the scene's middle */
    bool    fly_always;         /* (--png: every scene's, to see it) */
    HzPad   pad;                /* P1's buttons, as a scene holds them */
    const char *shot;           /* --png: a picture of the next frame, by
                                   this name */
} Run;

static Run run_state;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static const char *type_name(u8 type)
{
    static const char *const NAME[12] = {
        "AMR", "TAX", "SAM", "ST", "SUPPLY", "GRM", "T1", "T2", "INF", "FWA",
        "TRUCK", "MAN"
    };
    return (type < 12) ? NAME[type] : "?";
}

static void trace(const Run *r)
{
    const HzGame *g = r->g;
    fprintf(stderr, "%5d", r->frames);
    for (int side = 0; side < 2; side++) {
        for (u32 i = 0; i < side_slots(g, side); i++) {
            const HzUnit *u = unit_at_c(g, side, i);
            if (!(u->flags & UNIT_ACTIVE)) {
                continue;
            }
            fprintf(stderr, " | %d.%u %s%s %d,%d hp %u", side + 1, i,
                    type_name(u->type), (u->flags & UNIT_DYING) ? "+"
                    : (u->flags & UNIT_CARRIED) ? "^" : "", u->x - r->ox,
                    u->y - r->oy, u->hp);
            if (u->type == UNIT_REPAIR) {
                fprintf(stderr, " crew %u alarm %u%s job %d", u->ammo,
                        u->ai.alarm, u->ai.recall ? " recall" : "",
                        ref_is_none(u->ai.customer_id)
                        ? -1 : (int)u->ai.customer_id.slot);
            } else if (u->type == UNIT_MECHANIC) {
                fprintf(stderr, " job %d stuck %u%s",
                        ref_is_none(u->ai.customer_id)
                        ? -1 : (int)u->ai.customer_id.slot, u->ai.stuck,
                        u->ai.working ? " mending" : "");
            }
        }
    }
    fprintf(stderr, "\n");
}

static void write_png(const Run *r, const char *name)
{
    static u8 idx[SCREEN_W * SCREEN_H];
    static u8 rgb[SCREEN_W * SCREEN_H * 3];
    HzGame *g = r->g;
    vdp_render(g->vdp, idx);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        vdp_cram_rgb(g->vdp->cram[idx[i] & 63], rgb + i * 3);
    }
    char path[512];
    if (name != NULL) {
        snprintf(path, sizeof path, "%s_%s_%s.png", r->png, r->scene, name);
    } else {
        snprintf(path, sizeof path, "%s_%s_%d.png", r->png, r->scene,
                 r->frames);
    }
    if (!png_write(path, SCREEN_W, SCREEN_H, 8, 2, rgb, SCREEN_W * 3, NULL,
                   0, NULL)) {
        fprintf(stderr, "hz_repair: can't write %s\n", path);
    }
}

static void on_frame(HzGame *g, void *user)
{
    Run *r = user;
    r->frames++;
    if (g->crowd && !crowd_check(g, true)) {
        fprintf(stderr, "hz_repair: %s: the grid's lists are wrong at frame "
                "%d\n", r->scene, r->frames);
        exit(1);
    }
    /* P1's pad: the scene's buttons, and its jet flown to the scene (for
     * the pictures, or by the scene) */
    g->pad_in[0] = r->pad;
    if (r->fly) {
        const HzMech *m = &g->mech[0];
        int dx = r->ox + 250 - (m->x + 16);
        int dy = r->oy + 150 - (m->y + 16);
        g->pad_in[0].left = dx < -6;
        g->pad_in[0].right = dx > 6;
        g->pad_in[0].up = dy < -6;
        g->pad_in[0].down = dy > 6;
    }
    if (r->trace && r->frames % 12 == 0) {
        trace(r);
    }
    if (r->png != NULL && r->png_every > 0 && r->frames % r->png_every == 0) {
        write_png(r, NULL);
    }
    if (r->png != NULL && r->shot != NULL) {
        write_png(r, r->shot);
    }
    r->shot = NULL;
}

static bool music_on(void *user)
{
    (void)user;
    return true;
}

/* No tile of the w x h px at (x, y) blocked or with an event (an outpost,
 * a current, damaging ground). */
static bool flat(const HzGame *g, int x, int y, int w, int h)
{
    if (x < 64 || y < 64 || x + w > 2048 - 64 || y + h > 2048 - 64) {
        return false;
    }
    for (int ty = y >> 3; ty <= (y + h - 1) >> 3; ty++) {
        for (int tx = x >> 3; tx <= (x + w - 1) >> 3; tx++) {
            u16 block = (u16)(g->layout[(ty >> 2) & 0x3F][(tx >> 2) & 0x3F]
                              & 0x1FF);
            u16 word = g->blocks[block][(ty & 3) * 4 + (tx & 3)];
            if (word & (TILE_BLOCKED | TILE_EVENT | TILE_WATER)) {
                return false;
            }
        }
    }
    return true;
}

/* The scene's ground: the w x h px of flat ground nearest P1's HQ, at
 * least 200 px from it (clear of its ring) and of every outpost. */
static bool find_ground(Run *r, int w, int h)
{
    const HzGame *g = r->g;
    const HzPost *hq = &g->posts[HZ_POST_HQ];
    long best = -1;
    for (int y = 64; y + h < 2048 - 64; y += 16) {
        for (int x = 64; x + w < 2048 - 64; x += 16) {
            long dx = x + w / 2 - hq->x;
            long dy = y + h / 2 - hq->y;
            long d = dx * dx + dy * dy;
            if ((best >= 0 && d >= best) || !flat(g, x, y, w, h)) {
                continue;
            }
            bool clear = true;
            for (int i = 0; i <= HZ_POST_HQ + 1 && clear; i++) {
                const HzPost *p = &g->posts[i];
                if (!(p->flags & POST_USED)) {
                    continue;
                }
                clear = p->x < x - 160 || p->x > x + w + 160
                     || p->y < y - 160 || p->y > y + h + 160;
            }
            if (clear) {
                best = d;
                r->ox = x;
                r->oy = y;
            }
        }
    }
    return best >= 0;
}

static bool start(Run *r, HzGame *g, Vdp *vdp, const HzAssets *as, int map,
                  u32 fixes)
{
    vdp_reset(vdp);
    game_init(g, as, vdp);
    r->g = g;
    r->frames = 0;
    r->fly = r->fly_always;
    memset(&r->pad, 0, sizeof r->pad);
    r->shot = NULL;
    g->frame = on_frame;
    g->frame_user = r;
    g->snd_music_on = music_on;
    g->fixes = fixes;
    g->hw_version = 0xA0;
    snd_init(g);
    g->options[0] = 0;          /* two players: no CPU, no start units */
    g->options[1] = 0;
    g->options[2] = 0xFF;
    g->options[3] = (u8)map;
    if (!game_new(g)) {
        fprintf(stderr, "hz_repair: map %d doesn't load\n", map);
        return false;
    }
    g->rng_seed_saved = 1;
    game_start(g);
    if (!find_ground(r, 420, 240)) {
        fprintf(stderr, "hz_repair: map %d has no 420 x 240 px of flat "
                "ground\n", map);
        return false;
    }
    if (r->trace) {
        fprintf(stderr, "%s: the ground at %d,%d; from it, P1's HQ at "
                "%d,%d\n", r->scene, r->ox, r->oy,
                g->posts[HZ_POST_HQ].x - r->ox,
                g->posts[HZ_POST_HQ].y - r->oy);
    }
    return game_frame(g, false) == HZ_LOOP_ON;
}

static bool frames(Run *r, int n)
{
    for (int i = 0; i < n; i++) {
        if (game_frame(r->g, true) != HZ_LOOP_ON) {
            return false;
        }
    }
    return true;
}

/* A unit put down at the scene's (x, y), standing; its HP if hp >= 0. */
static HzUnit *put(Run *r, int side, u8 type, u8 order, int x, int y, int hp)
{
    HzUnit *u = unit_spawn_unit(r->g, (u8)(UNIT_ACTIVE
                                           | (side ? UNIT_TEAM2 : 0)), type,
                                order, 0, (s16)(r->ox + x), (s16)(r->oy + y));
    if (u == NULL) {
        fprintf(stderr, "hz_repair: %s: no room for a %s at %d,%d\n",
                r->scene, type_name(type), x, y);
        exit(1);
    }
    u->speed = 0;
    if (hp >= 0) {
        u->hp = (u16)hp;
    }
    return u;
}

static int count(const HzGame *g, int side, u8 type)
{
    int n = 0;
    for (u32 i = 0; i < side_slots(g, side); i++) {
        const HzUnit *u = unit_at_c(g, side, i);
        n += (u->flags & UNIT_ACTIVE) && !(u->flags & UNIT_DYING)
             && u->type == type;
    }
    return n;
}

static u16 full_hp(const HzGame *g, const HzUnit *u)
{
    return unit_stats_for(g, u->type)->hp;
}

/* `took`: the frames until `cond` holds, at most `limit`; -1 if it never
 * did. */
#define WAIT(r, limit, cond, took) do { \
    (took) = -1; \
    for (int w_ = 0; w_ <= (limit); w_++) { \
        if (cond) { (took) = w_; break; } \
        if (!frames((r), 1)) { break; } \
    } \
} while (0)

#define FAIL(...) do { say("FAIL %s: ", r->scene); say(__VA_ARGS__); \
                       say("\n"); return false; } while (0)

static bool counts_right(Run *r)
{
    const HzGame *g = r->g;
    for (int side = 0; side < 2; side++) {
        u32 n = 0;
        u32 by_type[16] = { 0 };
        for (u32 i = 0; i < side_slots(g, side); i++) {
            const HzUnit *u = unit_at_c(g, side, i);
            if (u->flags & UNIT_ACTIVE) {
                n++;
                by_type[u->type & 15]++;
            }
        }
        if (n != g->unit_count[side]) {
            say("FAIL %s: side %d has %u units, its count says %u\n",
                r->scene, side + 1, n, g->unit_count[side]);
            return false;
        }
        for (int t = 0; t < 16; t++) {
            if (by_type[t] != g->unit_type_count[side][t]) {
                say("FAIL %s: side %d has %u of type %d, its count says "
                    "%u\n", r->scene, side + 1, by_type[t], t,
                    g->unit_type_count[side][t]);
                return false;
            }
        }
    }
    return true;
}

/* -- the scenes ----------------------------------------------------------- */

static bool scene_shop(Run *r)
{
    HzGame *g = r->g;
    bool on = (g->fixes & HZ_FIX_REPAIR) != 0;
    bool sold = (shop_types(g) & (1u << UNIT_REPAIR)) != 0;
    if (sold != on) {
        FAIL("the patch is %s and the shop %s the truck", on ? "on" : "off",
             sold ? "sells" : "doesn't sell");
    }
    if (!on) {
        say("ok   %s: not sold without the patch\n", r->scene);
        return true;
    }
    const HzUnitStats *st = unit_stats_for(g, UNIT_REPAIR);
    const HzOrderInfo *o = order_info_for(g, ORDER_REPAIR);
    if (st->price != g->assets->repair.stats[0].price
        || !(st->orders & (1u << ORDER_REPAIR))
        || st->ammo != 2 * REPAIR_MAN) {
        FAIL("the truck's record: price %u, orders %04X, ammo %u", st->price,
             st->orders, st->ammo);
    }
    say("ok   %s: sold at %u0, order fee %u0, a crew of %d\n", r->scene,
        st->price, o->fee, st->ammo / REPAIR_MAN);
    return true;
}

/* A button of P1's pad pressed for two frames and let go for two. */
static bool tap(Run *r, bool *button)
{
    *button = true;
    bool on = frames(r, 2);
    *button = false;
    return on && frames(r, 2);
}

static bool menu_open(const HzMech *m)
{
    return (m->flags & MECH_MENU_OPEN) && m->menu.step == 0
        && !m->menu.closing;
}

static bool scene_buy(Run *r)
{
    HzGame *g = r->g;
    HzMech *m = &g->mech[0];
    const HzUnitStats *st = unit_stats_for(g, UNIT_REPAIR);
    r->fly = false;
    int t;
    frames(r, 20);
    tap(r, &r->pad.c);
    WAIT(r, 120, menu_open(m), t);
    if (t < 0) {
        FAIL("the order menu didn't open");
    }
    tap(r, &r->pad.left);       /* left of the AMR-51D: the list's end */
    frames(r, 4);
    u16 cost = (u16)(st->price + order_info_for(g, ORDER_REPAIR)->fee);
    if (m->menu.type != UNIT_REPAIR || m->menu.order != ORDER_REPAIR
        || m->menu.price != st->price || m->menu.cost != cost) {
        FAIL("the menu: type %u, order %X, price %u, cost %u", m->menu.type,
             m->menu.order, m->menu.price, m->menu.cost);
    }
    r->shot = "menu";
    frames(r, 2);
    u16 money = m->money;
    tap(r, &r->pad.a);
    if (m->prod_state != PROD_BUILDING || m->prod_type != UNIT_REPAIR
        || m->prod_order != ORDER_REPAIR || m->money != money - cost) {
        FAIL("bought: state %u, type %u, order %X, money %u of %u",
             m->prod_state, m->prod_type, m->prod_order, m->money, money);
    }
    tap(r, &r->pad.c);
    WAIT(r, 120, !(m->flags & MECH_MENU_OPEN), t);
    if (t < 0) {
        FAIL("the order menu didn't close");
    }
    int built;
    WAIT(r, 1800, m->prod_state == PROD_READY, built);
    if (built < 0) {
        FAIL("not built after 30 s");
    }
    /* a jet over its HQ with A held picks the truck up */
    if (m->form == HZ_FORM_ROBOT) {
        tap(r, &r->pad.a);
        WAIT(r, 300, m->form == HZ_FORM_JET && m->transform == 0, t);
        if (t < 0) {
            FAIL("the mech didn't take off");
        }
    }
    r->pad.a = true;
    WAIT(r, 120, (m->flags & MECH_CARRYING) != 0, t);
    r->pad.a = false;
    if (t < 0) {
        FAIL("the truck not picked up at the HQ (form %u, state %u)",
             m->form, m->prod_state);
    }
    HzUnit *truck = carried_unit(g, m);
    if (truck->type != UNIT_REPAIR || m->carried_order != ORDER_REPAIR
        || truck->ammo != 2 * REPAIR_MAN || truck->hp != st->hp) {
        FAIL("carried: type %u, order %X, crew %u, %u HP", truck->type,
             m->carried_order, truck->ammo, truck->hp);
    }
    frames(r, 4);
    tap(r, &r->pad.c);          /* the menu with the truck carried */
    WAIT(r, 120, menu_open(m), t);
    r->shot = "carried";
    frames(r, 2);
    tap(r, &r->pad.c);
    WAIT(r, 120, !(m->flags & MECH_MENU_OPEN), t);
    if (t < 0) {
        FAIL("the order menu didn't close with the truck carried");
    }
    /* to the scene's ground, and down */
    r->fly = true;
    WAIT(r, 3600, abs(m->x + 16 - (r->ox + 250)) <= 6
                  && abs(m->y + 16 - (r->oy + 150)) <= 6, t);
    r->fly = false;
    if (t < 0) {
        FAIL("the mech didn't reach the scene: at %d,%d of it",
             m->x - r->ox, m->y - r->oy);
    }
    frames(r, 30);
    tap(r, &r->pad.a);
    if ((m->flags & MECH_CARRYING) || (truck->flags & UNIT_CARRIED)
        || !(truck->flags & UNIT_ACTIVE) || truck->order != ORDER_REPAIR) {
        FAIL("the truck not put down: flags %02X, order %X", truck->flags,
             truck->order);
    }
    HzUnit *tax = put(r, 0, UNIT_TAX, ORDER_STAND, truck->x - r->ox + 90,
                      truck->y - r->oy, 100);
    int t_full;
    frames(r, 180);
    r->shot = "work";
    WAIT(r, 2400, tax->hp >= full_hp(g, tax), t_full);
    if (t_full < 0 || !counts_right(r)) {
        FAIL("the TAX-52 beside it at %u HP after 40 s", tax->hp);
    }
    say("ok   %s: bought through the order menu for %u0 (of %u0), built in "
        "%.1f s, picked up, carried, put down, and its men mended a "
        "TAX-52\n", r->scene, cost, money, built / 60.0);
    return true;
}

static bool scene_mend(Run *r)
{
    HzGame *g = r->g;
    HzUnit *truck = put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 200, 100, -1);
    HzUnit *tax = put(r, 0, UNIT_TAX, ORDER_STAND, 280, 100, -1);
    HzUnit *amr = put(r, 0, UNIT_AMR, ORDER_STAND, 200, 180, 20);
    tax->hp = (u16)(full_hp(g, tax) / 2);
    s16 tx = truck->x;
    s16 ty = truck->y;
    int t_out, t_tax, t_all, t_in;
    WAIT(r, 120, count(g, 0, UNIT_MECHANIC) == 2, t_out);
    if (t_out < 0) {
        FAIL("%d men out after 120 frames", count(g, 0, UNIT_MECHANIC));
    }
    WAIT(r, 1800, tax->hp >= full_hp(g, tax), t_tax);
    if (t_tax < 0) {
        FAIL("the TAX-52 at %u of %u HP after 30 s", tax->hp,
             full_hp(g, tax));
    }
    WAIT(r, 1800, amr->hp >= full_hp(g, amr) && tax->hp >= full_hp(g, tax),
         t_all);
    if (t_all < 0) {
        FAIL("the AMR-51D at %u of %u HP", amr->hp, full_hp(g, amr));
    }
    WAIT(r, 900, count(g, 0, UNIT_MECHANIC) == 0, t_in);
    if (t_in < 0) {
        FAIL("%d men still out 15 s after the work", count(g, 0,
                                                            UNIT_MECHANIC));
    }
    if (g->unit_count[0] != 3 || truck->ammo != 2 * REPAIR_MAN
        || truck->x != tx || truck->y != ty || g->units_built[0] != 3) {
        FAIL("after: %u units, crew %u, the truck moved %d,%d, %u built",
             g->unit_count[0], truck->ammo, truck->x - tx, truck->y - ty,
             g->units_built[0]);
    }
    if (!counts_right(r)) {
        return false;
    }
    say("ok   %s: men out in %d frames, a TAX-52's half (%u HP) mended in "
        "%.1f s, the men in %.1f s after the work\n", r->scene, t_out,
        full_hp(g, tax) / 2, (t_out + t_tax) / 60.0, t_in / 60.0);
    return true;
}

static bool scene_self(Run *r)
{
    HzGame *g = r->g;
    HzUnit *truck = put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 200, 100, 30);
    int t_full, t_in;
    WAIT(r, 1800, truck->hp >= full_hp(g, truck), t_full);
    if (t_full < 0) {
        FAIL("the truck at %u of %u HP after 30 s", truck->hp,
             full_hp(g, truck));
    }
    WAIT(r, 600, count(g, 0, UNIT_MECHANIC) == 0, t_in);
    if (t_in < 0 || g->unit_count[0] != 1 || !counts_right(r)) {
        FAIL("%d men out after, %u units", count(g, 0, UNIT_MECHANIC),
             g->unit_count[0]);
    }
    say("ok   %s: mended by its own men in %.1f s\n", r->scene,
        t_full / 60.0);
    return true;
}

static bool scene_drive(Run *r)
{
    HzGame *g = r->g;
    const HzRepair *rp = &g->assets->repair;
    HzUnit *truck = put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 40, 100, -1);
    HzUnit *tax = put(r, 0, UNIT_TAX, ORDER_STAND, 340, 100, 150);
    /* something in the straight way */
    put(r, 0, UNIT_GRM, ORDER_STAND, 190, 100, -1);
    put(r, 0, UNIT_GRM, ORDER_STAND, 190, 126, -1);
    s16 x0 = truck->x;
    int t_full, t_in;
    WAIT(r, 3000, tax->hp >= full_hp(g, tax), t_full);
    if (t_full < 0) {
        FAIL("the TAX-52 at %u HP after 50 s; the truck drove %d px",
             tax->hp, truck->x - x0);
    }
    int gap = tax->x - (truck->x + truck->size);
    if (truck->x - x0 < 100 || gap > rp->park + 16) {
        FAIL("the truck drove %d px and stands %d px from the TAX-52",
             truck->x - x0, gap);
    }
    WAIT(r, 900, count(g, 0, UNIT_MECHANIC) == 0, t_in);
    if (t_in < 0 || !counts_right(r)) {
        FAIL("%d men still out", count(g, 0, UNIT_MECHANIC));
    }
    say("ok   %s: drove %d px round two units, parked %d px off, the "
        "TAX-52 whole after %.1f s\n", r->scene, truck->x - x0, gap,
        t_full / 60.0);
    return true;
}

/* How far P1's unit u is from home: the nearest of P1's outposts and its
 * HQ (the larger axis). */
static int home_dist(const HzGame *g, const HzUnit *u)
{
    int best = 1 << 20;
    for (int i = 0; i <= HZ_POST_HQ; i++) {
        const HzPost *p = &g->posts[i];
        if (i < HZ_POST_HQ && p->flags != (POST_USED | POST_OWNED)) {
            continue;
        }
        int dx = u->x + 12 - p->x;
        int dy = u->y + 12 - p->y;
        dx = (dx < 0) ? -dx : dx;
        dy = (dy < 0) ? -dy : dy;
        int d = (dx > dy) ? dx : dy;
        best = (d < best) ? d : best;
    }
    return best;
}

static bool scene_alarm(Run *r)
{
    HzGame *g = r->g;
    HzUnit *truck = put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 200, 100, -1);
    HzUnit *tax = put(r, 0, UNIT_TAX, ORDER_STAND, 280, 100, 20);
    int t;
    WAIT(r, 240, count(g, 0, UNIT_MECHANIC) >= 1 && tax->hp > 24, t);
    if (t < 0) {
        FAIL("no man mending after 4 s");
    }
    /* an enemy 110 px from the TAX-52 and the man on it: out of its own
     * sight (a SAM-42 shoots at mechs alone) */
    HzUnit *foe = put(r, 1, UNIT_SAM, ORDER_STAND, 280, 210, -1);
    u16 hp_then = tax->hp;
    int home0 = home_dist(g, truck);
    s16 x0 = truck->x;
    s16 y0 = truck->y;
    WAIT(r, 900, count(g, 0, UNIT_MECHANIC) == 0, t);
    if (t < 0) {
        FAIL("the men still out 15 s after the enemy came");
    }
    if (tax->hp > hp_then + 8) {
        FAIL("mended on with the enemy there: %u to %u HP", hp_then,
             tax->hp);
    }
    int t_off;
    WAIT(r, 900, home_dist(g, truck) < home0 - 60, t_off);
    if (t_off < 0) {
        FAIL("the truck didn't make for home: %d,%d from where it stood, "
             "%d px from home (%d before)", truck->x - x0, truck->y - y0,
             home_dist(g, truck), home0);
    }
    int nearer = home0 - home_dist(g, truck);
    /* the enemy gone: back to work once it is calm */
    foe->flags |= UNIT_DYING;
    foe->hp = 0x10;
    int t_back;
    WAIT(r, 5400, tax->hp >= full_hp(g, tax), t_back);
    if (t_back < 0) {
        FAIL("the TAX-52 at %u HP 90 s after the enemy went; the truck is "
             "%d,%d from where it stood", tax->hp, truck->x - x0,
             truck->y - y0);
    }
    if (!counts_right(r)) {
        return false;
    }
    say("ok   %s: the men in %.1f s after the enemy came, the truck %d px "
        "nearer home %.1f s later, back and done %.1f s after it went\n",
        r->scene, t / 60.0, nearer, t_off / 60.0, t_back / 60.0);
    return true;
}

static bool scene_wreck(Run *r)
{
    HzGame *g = r->g;
    HzUnit *truck = put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 200, 100, -1);
    put(r, 0, UNIT_TAX, ORDER_STAND, 280, 100, 10);
    put(r, 0, UNIT_TAX, ORDER_STAND, 200, 180, 10);
    int t;
    WAIT(r, 240, count(g, 0, UNIT_MECHANIC) == 2, t);
    if (t < 0) {
        FAIL("%d men out after 4 s", count(g, 0, UNIT_MECHANIC));
    }
    frames(r, 120);
    truck->flags |= UNIT_DYING;
    truck->hp = 0x10;
    frames(r, 90);
    int men = count(g, 0, UNIT_MECHANIC);
    int inf = count(g, 0, UNIT_INFANTRY);
    if (men != 0 || inf != 2 || g->unit_count[0] != 4 || !counts_right(r)) {
        FAIL("after the truck: %d mechanics, %d INFANTRY, %u units", men,
             inf, g->unit_count[0]);
    }
    for (u32 i = 0; i < side_slots(g, 0); i++) {
        const HzUnit *u = unit_at_c(g, 0, i);
        if ((u->flags & UNIT_ACTIVE) && u->type == UNIT_INFANTRY
            && (u->order != ORDER_CAPTURE || (u->sprite_size >> 12) != 0)) {
            FAIL("an INFANTRY with order %X, sprite size %04X", u->order,
                 u->sprite_size);
        }
    }
    frames(r, 600);             /* and they go about their order */
    if (!counts_right(r)) {
        return false;
    }
    say("ok   %s: its two men INFANTRY with the capture order\n", r->scene);
    return true;
}

static bool scene_crew(Run *r)
{
    HzGame *g = r->g;
    HzUnit *truck = put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 200, 100, -1);
    put(r, 0, UNIT_TAX, ORDER_STAND, 280, 100, 10);
    put(r, 0, UNIT_TAX, ORDER_STAND, 200, 180, 10);
    int t;
    WAIT(r, 240, count(g, 0, UNIT_MECHANIC) == 2, t);
    if (t < 0) {
        FAIL("%d men out after 4 s", count(g, 0, UNIT_MECHANIC));
    }
    frames(r, 120);
    for (u32 i = 0; i < side_slots(g, 0); i++) {
        HzUnit *u = unit_at(g, 0, i);
        if ((u->flags & UNIT_ACTIVE) && u->type == UNIT_MECHANIC) {
            u->flags |= UNIT_DYING;
            u->hp = 0x10;
            break;
        }
    }
    int t_short, t_in, t_home;
    WAIT(r, 120, truck->ammo == REPAIR_MAN, t_short);
    if (t_short < 0) {
        FAIL("the truck's crew is %u two seconds after a man died",
             truck->ammo);
    }
    WAIT(r, 900, count(g, 0, UNIT_MECHANIC) == 0, t_in);
    if (t_in < 0) {
        FAIL("the other man still out after 15 s");
    }
    int home0 = home_dist(g, truck);
    WAIT(r, 7200, truck->ammo == 2 * REPAIR_MAN, t_home);
    if (t_home < 0) {
        FAIL("no new man after 2 minutes: the truck is %d px from home "
             "(%d before), crew %u", home_dist(g, truck), home0, truck->ammo);
    }
    if (!counts_right(r)) {
        return false;
    }
    if (home_dist(g, truck) > 112) {
        FAIL("a crew of two again %d px from home", home_dist(g, truck));
    }
    say("ok   %s: a man short at once, the other in after %.1f s, a crew "
        "of two again %.1f s later, at home (%d px off, %d before)\n",
        r->scene, t_in / 60.0, t_home / 60.0, home_dist(g, truck), home0);
    return true;
}

static bool scene_lift(Run *r)
{
    HzGame *g = r->g;
    HzUnit *truck = put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 200, 100, -1);
    HzUnit *tax = put(r, 0, UNIT_TAX, ORDER_STAND, 280, 100, 10);
    int t;
    WAIT(r, 240, count(g, 0, UNIT_MECHANIC) >= 1 && tax->hp > 14, t);
    if (t < 0) {
        FAIL("no man mending after 4 s");
    }
    /* as mech_lift leaves a carried unit: the flag, out of the grid */
    truck->flags |= UNIT_CARRIED;
    grid_clear_unit(g, truck);
    frames(r, 24);
    u16 hp_then = tax->hp;
    HzPoint at = { 0, 0 };
    for (u32 i = 0; i < side_slots(g, 0); i++) {
        const HzUnit *u = unit_at_c(g, 0, i);
        if ((u->flags & UNIT_ACTIVE) && u->type == UNIT_MECHANIC) {
            at.x = u->x;
            at.y = u->y;
        }
    }
    frames(r, 240);
    for (u32 i = 0; i < side_slots(g, 0); i++) {
        const HzUnit *u = unit_at_c(g, 0, i);
        if ((u->flags & UNIT_ACTIVE) && u->type == UNIT_MECHANIC
            && (u->x != at.x || u->y != at.y) && count(g, 0, UNIT_MECHANIC)
               == 1) {
            FAIL("a man walked %d,%d while his truck was carried",
                 u->x - at.x, u->y - at.y);
        }
    }
    if (tax->hp != hp_then || count(g, 0, UNIT_MECHANIC) < 1) {
        FAIL("with the truck carried: the TAX-52 %u to %u HP, %d men",
             hp_then, tax->hp, count(g, 0, UNIT_MECHANIC));
    }
    /* as mech_drop puts it down, 250 px west: out of its men's reach of
     * the TAX-52 */
    truck->flags &= (u8)~UNIT_CARRIED;
    truck->x = (s16)(r->ox + 30);
    truck->y = (s16)(r->oy + 100);
    truck->speed = 0;
    memset(&truck->ai, 0, sizeof truck->ai);
    grid_mark_unit(g, truck);
    int t_in, t_full;
    WAIT(r, 1800, count(g, 0, UNIT_MECHANIC) == 0
                  || tax->hp >= full_hp(g, tax), t_in);
    if (t_in < 0) {
        FAIL("the men not back 30 s after the truck came down");
    }
    WAIT(r, 5400, tax->hp >= full_hp(g, tax), t_full);
    if (t_full < 0 || !counts_right(r)) {
        FAIL("the TAX-52 at %u HP 90 s on", tax->hp);
    }
    say("ok   %s: the men waited, walked 250 px back in %.1f s, and the "
        "truck drove back for the TAX-52\n", r->scene, t_in / 60.0);
    return true;
}

static bool scene_full(Run *r)
{
    HzGame *g = r->g;
    if (units_unlimited(g)) {
        say("ok   %s: (with UNITS a side has no limit)\n", r->scene);
        return true;
    }
    put(r, 0, UNIT_REPAIR, ORDER_REPAIR, 200, 100, -1);
    HzUnit *tax = put(r, 0, UNIT_TAX, ORDER_STAND, 280, 100, 10);
    /* (32 px apart: without UNITS the grid holds one unit a cell) */
    static const int ROW_Y[4] = { 8, 150, 182, 214 };
    for (int i = 0; i < 48; i++) {
        put(r, 0, UNIT_INFANTRY, ORDER_STAND, 12 + (i % 12) * 32,
            ROW_Y[i / 12], -1);
    }
    frames(r, 300);
    if (count(g, 0, UNIT_MECHANIC) != 0 || tax->hp != 10
        || g->unit_count[0] != 50 || !counts_right(r)) {
        FAIL("%d men out with 50 units, the TAX-52 at %u HP, %u units",
             count(g, 0, UNIT_MECHANIC), tax->hp, g->unit_count[0]);
    }
    say("ok   %s: no man out while the side has its 50 units\n", r->scene);
    return true;
}

typedef struct {
    const char *name;
    bool (*play)(Run *r);
} Scene;

static const Scene SCENES[] = {
    { "shop", scene_shop },   { "buy", scene_buy },
    { "mend", scene_mend },
    { "self", scene_self },   { "drive", scene_drive },
    { "alarm", scene_alarm }, { "wreck", scene_wreck },
    { "crew", scene_crew },   { "lift", scene_lift },
    { "full", scene_full }
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "hz_repair ASSETS [--map M] [--scene NAME] "
                "[--fixes HEX] [--trace] [--png PREFIX --png-every N]\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    if (!assets.repair_loaded) {
        fprintf(stderr, "hz_repair: %s has no data/repair.json\n", argv[1]);
        return 1;
    }
    Run *r = &run_state;
    int map = 4;
    const char *only = NULL;
    long one_fix = -1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--map") && i + 1 < argc) {
            map = atoi(argv[++i]) & 7;
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc) {
            only = argv[++i];
        } else if (!strcmp(argv[i], "--fixes") && i + 1 < argc) {
            one_fix = strtol(argv[++i], NULL, 16);
        } else if (!strcmp(argv[i], "--trace")) {
            r->trace = true;
        } else if (!strcmp(argv[i], "--png") && i + 1 < argc) {
            r->png = argv[++i];
            r->fly_always = true;
        } else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) {
            r->png_every = atoi(argv[++i]);
        } else {
            fprintf(stderr, "hz_repair: %s?\n", argv[i]);
            return 2;
        }
    }
    static const u32 SETS[4] = {
        HZ_FIX_DEFAULT | HZ_FIX_REPAIR,
        HZ_FIX_DEFAULT | HZ_FIX_REPAIR | HZ_FIX_UNITS,
        HZ_FIX_DEFAULT | HZ_FIX_REPAIR | HZ_FIX_UNITS | HZ_FIX_PATHS,
        HZ_FIX_DEFAULT
    };
    static Vdp vdp;
    static HzGame game;
    int failed = 0;
    int played = 0;
    for (int s = 0; s < 4; s++) {
        u32 fixes = (one_fix >= 0) ? (u32)one_fix : SETS[s];
        if (one_fix >= 0 && s > 0) {
            break;
        }
        say("patches %03X\n", (unsigned)fixes);
        for (size_t k = 0; k < sizeof SCENES / sizeof SCENES[0]; k++) {
            if (only != NULL && strcmp(only, SCENES[k].name) != 0) {
                continue;
            }
            if (!(fixes & HZ_FIX_REPAIR) && k != 0) {
                continue;       /* without the patch: only what is sold */
            }
            r->scene = SCENES[k].name;
            if (!start(r, &game, &vdp, &assets, map, fixes)) {
                return 1;
            }
            played++;
            if (!SCENES[k].play(r)) {
                failed++;
            }
        }
    }
    if (played == 0) {
        fprintf(stderr, "hz_repair: no scene %s\n", only);
        return 2;
    }
    say("%d of %d scenes failed\n", failed, played);
    return failed != 0;
}
