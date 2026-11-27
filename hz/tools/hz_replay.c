/* hz_replay (session 59): recorded games (src/game/replay.h; the game
 * writes one to saves/replays/ each time a game ends) played again with no
 * window and no sound, and looked at:
 *
 *     hz_replay ASSETS FILE... [--events] [--minutes] [--check]
 *
 * Each replay starts from a cold boot with the start it recorded, is fed
 * its pads, and is checked against its hashes every second (the game
 * without its display: statehash.c's state_hash_game); the first that
 * differs is said, and what comes after it is no longer the game that was
 * played. For each side: the mech's deaths and why (out of fuel, shot
 * down as a jet, carrying, on foot), its gun (shots, hits taken), what it
 * bought and with which order, the units it carried and where it put them
 * down, the units it lost, the outposts it took and lost, where its time
 * went (flying, carrying, on foot, the order menu, destroyed; by its own
 * HQ, by the enemy's), its money (spent, the most left unspent), the HQ
 * damage it took; the nightmare AI's own counts for a side it played.
 * --minutes adds a line a minute (money, outposts, units, HQs), --events
 * every purchase, drop, capture and death with its time. --check only
 * plays them and says whether each is the game recorded (exit 1 if not).
 * Times are the game's clock (v_frame_counter: a pause doesn't count). */

#include "../src/core/hz_file.h"
#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/nightmare.h"
#include "../src/game/replay.h"
#include "../src/game/statehash.h"
#include "../src/net/net_client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const MAP_NAME[8] = {
    "ABGRUND", "VULKAN", "LOCH", "STRAND", "STADT", "EISFREI", "WALDUNG",
    "OASE"
};

/* 6 and 7 only ever built with TURRETS (session 60, docs/turrets.md) */
static const char *const UNIT_NAME[16] = {
    "AMR-51D", "TAX-52", "SAM-42", "ST-57U", "SUPPLY", "GRM-34A",
    "TURRET-1", "TURRET-2", "INFANTRY", "FWA", "?10", "?11", "?12", "?13",
    "?14", "?15"
};

static const char *const ORDER_NAME[16] = {
    "roam", "stand", "patrol", "take an outpost", "guard", "guard here",
    "ambush", "advance", "hold", "capture", "attack the HQ", "?B", "?C",
    "?D", "?E", "supply"
};

#define NEAR_HQ   400       /* px: "by an HQ" */
#define EVENTS_MAX 4000

typedef struct {
    int  deaths, deaths_fuel, deaths_air, deaths_foot, deaths_carrying;
    int  deaths_by_mech;    /* the enemy mech had fired near it just before */
    int  shots, hits_taken;
    long hp_lost;
    int  bought[16], bought_count;
    int  order[16];
    long spent;
    int  refused;           /* bought without the money */
    int  pickups, drops, drops_near_enemy_hq, drops_near_own_hq;
    int  units_lost[16], units_lost_count;
    int  posts_taken, posts_lost, posts_most;
    long t_jet, t_carry, t_foot, t_menu, t_dead;
    long t_own_hq, t_enemy_hq;
    int  presses_a, presses_b, presses_c;
    int  b_gap2;            /* B pressed again 2 frames after the last */
    int  low_fuel;          /* times it fell under a quarter */
    u16  money_most;
    int  hq_lost[64];       /* the HQ's HP lost, a minute each */
    int  hq_hp_min;
    /* the frame before */
    u8   flags, form, prod_state, ammo;
    u16  hp, money, fuel;
    u8   pad;
    u32  last_b, last_shot;
    bool low;
} Side;

typedef struct {
    HzReplay r;
    Side     side[2];
    bool     started;       /* Game_Start reached: the recording's frames */
    u32      frame;         /* hand-overs fed */
    u32      checked;       /* hashes alike */
    long     split;         /* the first hand-over whose hash differs, -1 */
    u8       post_flags[HZ_POST_OUTPOSTS];
    u8      *unit_flags[2];     /* each side's slots, unit_cap[side] */
    u32      unit_cap[2];
    u16      hq_hp[2];
    bool     events;
    bool     minutes;
    char    *event[EVENTS_MAX];
    int      event_count;
    char     minute[64][96];
    int      minute_count;
    u16      fuel_max;
} Play;

static Play g_play;

/* (Session 61) Room for a side's slots in unit_flags (new ones zeroed). */
static void track_room(Play *p, int side, u32 slots)
{
    if (slots <= p->unit_cap[side]) {
        return;
    }
    u8 *f = realloc(p->unit_flags[side], slots);
    if (f == NULL) {
        fprintf(stderr, "hz_replay: out of memory\n");
        exit(1);
    }
    memset(f + p->unit_cap[side], 0, slots - p->unit_cap[side]);
    p->unit_flags[side] = f;
    p->unit_cap[side] = slots;
}

static void event(Play *p, const HzGame *g, const char *fmt, ...)
{
    if (!p->events || p->event_count >= EVENTS_MAX) {
        return;
    }
    char text[160];
    u32 s = g->frame_counter / 60;
    int n = snprintf(text, sizeof text, "  %2u:%02u ", (unsigned)(s / 60),
                     (unsigned)(s % 60));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text + n, sizeof text - (size_t)n, fmt, ap);
    va_end(ap);
    size_t len = strlen(text) + 1;
    char *copy = malloc(len);
    if (copy != NULL) {
        memcpy(copy, text, len);
        p->event[p->event_count++] = copy;
    }
}

static int chebyshev(int dx, int dy)
{
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    return dx > dy ? dx : dy;
}

static int hq_dist(const HzGame *g, const HzMech *m, int hq)
{
    const HzPost *h = &g->posts[HZ_POST_HQ + hq];
    return chebyshev(m->x + 16 - h->x, m->y + 16 - h->y);
}

static const char *who(const Play *p, int s)
{
    static char text[2][48];
    const HzReplay *r = &p->r;
    u8 mode = r->options[0] & 3;
    bool cpu = (s == 0) ? (mode & 2) != 0 : (mode & 1) != 0;
    if (cpu) {
        snprintf(text[s], sizeof text[s], "the %s AI",
                 r->brain[s] == HZ_BRAIN_NIGHTMARE ? "nightmare"
                                                   : "original");
    } else if (strcmp(r->kind, "network") == 0) {
        snprintf(text[s], sizeof text[s], "%s",
                 s == r->side ? "you" : (r->rival[0] ? r->rival : "rival"));
    } else if (strcmp(r->kind, "netai") == 0) {
        /* (session 67) recorded by the server's AI */
        snprintf(text[s], sizeof text[s], "%s",
                 s == r->side ? "the server's AI"
                 : (r->rival[0] ? r->rival : "the person"));
    } else {
        snprintf(text[s], sizeof text[s], "%s",
                 mode == 0 ? (s == 0 ? "player 1" : "player 2") : "you");
    }
    return text[s];
}

static int owner(u8 flags)
{
    if (!(flags & POST_USED) || !(flags & POST_OWNED)) {
        return -1;
    }
    return (flags & POST_TEAM2) ? 1 : 0;
}

/* Each frame, the state as the last frame's logic left it. */
static void watch(Play *p, HzGame *g)
{
    bool first = p->frame == 0;
    for (int s = 0; s < 2; s++) {
        Side *d = &p->side[s];
        const HzMech *m = &g->mech[s];
        const HzMech *e = &g->mech[s ^ 1];
        bool dead = (m->flags & MECH_DESTROYED) != 0;
        if (first) {
            d->flags = m->flags;
            d->hp = m->hp;
            d->money = m->money;
            d->ammo = m->ammo;
            d->prod_state = m->prod_state;
            d->fuel = m->fuel;
            d->hq_hp_min = post_hq_hp(&g->posts[HZ_POST_HQ + s]);
            continue;
        }
        /* deaths */
        if (dead && !(d->flags & MECH_DESTROYED)) {
            d->deaths++;
            const char *how;
            if (m->fuel == 0) {
                d->deaths_fuel++;
                how = "out of fuel";
            } else if (d->flags & MECH_CARRYING) {
                d->deaths_carrying++;
                how = "shot down carrying a unit";
            } else if (m->form == HZ_FORM_ROBOT) {
                d->deaths_foot++;
                how = "destroyed on foot";
            } else {
                d->deaths_air++;
                how = "shot down";
            }
            bool by_mech = p->side[s ^ 1].last_shot + 40 >= p->frame
                           && chebyshev(e->x - m->x, e->y - m->y) < 260;
            if (m->fuel != 0 && by_mech) {
                d->deaths_by_mech++;
            }
            event(p, g, "P%d %s: mech %s%s, %d px from its HQ", s + 1,
                  who(p, s), how,
                  m->fuel != 0 ? (by_mech ? " (the enemy mech)"
                                          : " (by units)") : "",
                  hq_dist(g, m, s));
        }
        /* gun */
        if (m->ammo + 1 == d->ammo && !dead) {
            d->shots++;
            d->last_shot = p->frame;
        }
        if (!dead && m->hp < d->hp && !(d->flags & MECH_DESTROYED)) {
            d->hits_taken++;
            d->hp_lost += d->hp - m->hp;
        }
        /* buying */
        if (m->prod_state == PROD_BUILDING && d->prod_state != PROD_BUILDING
            && m->money < d->money) {
            u8 t = m->prod_type & 15;
            d->bought[t]++;
            d->bought_count++;
            d->order[m->prod_order & 15]++;
            d->spent += d->money - m->money;
            event(p, g, "P%d %s: bought %s, %s (%u)", s + 1, who(p, s),
                  UNIT_NAME[t], ORDER_NAME[m->prod_order & 15],
                  (unsigned)(d->money - m->money));
        }
        if (m->prod_state == 0xFF && d->prod_state != 0xFF) {
            d->refused++;
        }
        /* carrying */
        if ((m->flags & MECH_CARRYING) && !(d->flags & MECH_CARRYING)) {
            d->pickups++;
        }
        if (!(m->flags & MECH_CARRYING) && (d->flags & MECH_CARRYING)
            && !dead && !(d->flags & MECH_DESTROYED)) {   /* not a respawn */
            d->drops++;
            int far = hq_dist(g, m, s ^ 1);
            if (far < NEAR_HQ * 2) {
                d->drops_near_enemy_hq++;
            }
            if (hq_dist(g, m, s) < NEAR_HQ) {
                d->drops_near_own_hq++;
            }
            event(p, g, "P%d %s: put a unit down (%s), %d px from the enemy "
                  "HQ", s + 1, who(p, s),
                  ORDER_NAME[m->carried_order & 15], far);
        }
        /* time */
        if (dead) {
            d->t_dead++;
        } else if (m->flags & MECH_MENU_OPEN) {
            d->t_menu++;
        } else if (m->flags & MECH_CARRYING) {
            d->t_carry++;
        } else if (m->form == HZ_FORM_ROBOT) {
            d->t_foot++;
        } else {
            d->t_jet++;
        }
        if (!dead) {
            if (hq_dist(g, m, s) < NEAR_HQ) {
                d->t_own_hq++;
            } else if (hq_dist(g, m, s ^ 1) < NEAR_HQ) {
                d->t_enemy_hq++;
            }
        }
        /* fuel */
        bool low = !dead && m->fuel < p->fuel_max / 4;
        if (low && !d->low) {
            d->low_fuel++;
        }
        d->low = low;
        /* money */
        if (m->money > d->money_most) {
            d->money_most = m->money;
        }
        /* the HQ */
        u16 hq = post_hq_hp(&g->posts[HZ_POST_HQ + s]);
        if (hq < p->hq_hp[s]) {
            u32 minute = g->frame_counter / 3600;
            d->hq_lost[minute < 64 ? minute : 63] += p->hq_hp[s] - hq;
        }
        if (hq < d->hq_hp_min) {
            d->hq_hp_min = hq;
        }
        p->hq_hp[s] = hq;

        d->flags = m->flags;
        d->hp = m->hp;
        d->money = m->money;
        d->ammo = m->ammo;
        d->prod_state = m->prod_state;
        d->fuel = m->fuel;
    }
    if (first) {
        for (int s = 0; s < 2; s++) {
            p->hq_hp[s] = post_hq_hp(&g->posts[HZ_POST_HQ + s]);
        }
        for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
            p->post_flags[i] = g->posts[i].flags;
        }
        for (int s = 0; s < 2; s++) {
            track_room(p, s, side_slots(g, s));
            for (u32 i = 0; i < side_slots(g, s); i++) {
                p->unit_flags[s][i] = unit_at_c(g, s, i)->flags;
            }
        }
        return;
    }
    /* outposts */
    int held[2] = { 0, 0 };
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        int was = owner(p->post_flags[i]);
        int now = owner(g->posts[i].flags);
        if (now >= 0) {
            held[now]++;
        }
        if (now != was) {
            if (now >= 0) {
                p->side[now].posts_taken++;
            }
            if (was >= 0) {
                p->side[was].posts_lost++;
            }
            if (now >= 0) {
                event(p, g, "P%d %s: took outpost %d%s", now + 1, who(p, now),
                      i, was >= 0 ? " from the enemy" : "");
            } else {
                event(p, g, "P%d %s: lost outpost %d", was + 1, who(p, was),
                      i);
            }
        }
        p->post_flags[i] = g->posts[i].flags;
    }
    for (int s = 0; s < 2; s++) {
        if (held[s] > p->side[s].posts_most) {
            p->side[s].posts_most = held[s];
        }
    }
    /* units lost */
    for (int s = 0; s < 2; s++) {
        track_room(p, s, side_slots(g, s));
        for (u32 i = 0; i < side_slots(g, s); i++) {
            const HzUnit *u = unit_at_c(g, s, i);
            u8 f = u->flags;
            u8 was = p->unit_flags[s][i];
            if ((f & UNIT_DYING) && (f & UNIT_ACTIVE)
                && !(was & UNIT_DYING) && (was & UNIT_ACTIVE)) {
                Side *d = &p->side[s];
                d->units_lost[u->type & 15]++;
                d->units_lost_count++;
            }
            p->unit_flags[s][i] = f;
        }
    }
    /* a line a minute */
    if (p->minutes && g->frame_counter % 3600 == 0 && g->frame_counter > 0
        && p->minute_count < 64) {
        snprintf(p->minute[p->minute_count++], sizeof p->minute[0],
                 "  %3u   %5u %5u   %2d %2d   %2u %2u   %4u %4u   %2d %2d",
                 (unsigned)(g->frame_counter / 3600), g->mech[0].money,
                 g->mech[1].money, held[0], held[1], g->unit_count[0],
                 g->unit_count[1], p->hq_hp[0], p->hq_hp[1],
                 p->side[0].deaths, p->side[1].deaths);
    }
}

/* The pads fed: presses counted from them. */
static void count_pads(Play *p, const HzGame *g)
{
    for (int s = 0; s < 2; s++) {
        Side *d = &p->side[s];
        u8 now = replay_pad_byte(&g->pad_in[s]);
        u8 pressed = (u8)(now & ~d->pad);
        if (pressed & HZ_PAD_A) {
            d->presses_a++;
        }
        if (pressed & HZ_PAD_B) {
            d->presses_b++;
            if (p->frame - d->last_b == 2) {
                d->b_gap2++;
            }
            d->last_b = p->frame;
        }
        if (pressed & HZ_PAD_C) {
            d->presses_c++;
        }
        d->pad = now;
    }
}

static void on_frame(HzGame *g, void *user)
{
    Play *p = user;
    if (g->quit) {
        return;
    }
    if (!p->started) {
        return;                 /* the loading screen: not recorded */
    }
    if (p->frame % REPLAY_HASH_EVERY == 0) {
        u32 k = p->frame / REPLAY_HASH_EVERY;
        if (k < p->r.hash_count) {
            if (state_hash_game(g) == p->r.hashes[k]) {
                if (p->split < 0) {
                    p->checked++;
                }
            } else if (p->split < 0) {
                p->split = (long)p->frame;
            }
        }
    }
    watch(p, g);
    if (!replay_feed(&p->r, g, p->frame)) {
        g->quit = true;
        return;
    }
    count_pads(p, g);
    p->frame++;
}

static bool music_on(void *user)
{
    (void)user;
    return true;
}

static void clock_text(char *out, size_t len, u32 frames)
{
    u32 s = frames / 60;
    snprintf(out, len, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void pct(char *out, size_t len, long part, long whole)
{
    snprintf(out, len, "%ld%%", whole > 0 ? (part * 100 + whole / 2) / whole
                                          : 0);
}

static void row(const char *label, const char *a, const char *b)
{
    printf("  %-30s %-24s %s\n", label, a, b);
}

static void rowi(const char *label, long a, long b)
{
    char x[24], y[24];
    snprintf(x, sizeof x, "%ld", a);
    snprintf(y, sizeof y, "%ld", b);
    row(label, x, y);
}

static void bought_text(const Side *d, char *out, size_t len, bool lost)
{
    out[0] = '\0';
    size_t n = 0;
    const int *count = lost ? d->units_lost : d->bought;
    for (int t = 0; t < 16; t++) {
        if (count[t] > 0 && n < len) {
            n += (size_t)snprintf(out + n, len - n, "%s%s %d",
                                  n ? ", " : "", UNIT_NAME[t], count[t]);
        }
    }
    if (n == 0) {
        snprintf(out, len, "none");
    }
}

static void report(Play *p, HzGame *g, const char *path, HzLoopExit exit)
{
    const HzReplay *r = &p->r;
    const Side *a = &p->side[0];
    const Side *b = &p->side[1];
    const char *slash = strrchr(path, '/');
    printf("== %s\n", slash ? slash + 1 : path);
    printf("%s, TYPE %c, P1 %s against P2 %s%s; played %s\n",
           MAP_NAME[r->options[3] & 7], 'A' + (r->options[1] & 3),
           who(p, 0), who(p, 1),
           r->own_view ? " (own screens)" : "", r->when);
    char t[32];
    clock_text(t, sizeof t, g->frame_counter);
    if (p->split >= 0) {
        printf("NOT THE GAME PLAYED from hand-over %ld (%u checks alike "
               "before it): what follows is a guess\n", p->split,
               (unsigned)p->checked);
    } else {
        printf("the game recorded, checked every second (%u alike)\n",
               (unsigned)p->checked);
    }
    if (exit == HZ_LOOP_GAME_OVER && g->winner == HZ_DRAW) {
        printf("a draw (both HQs down), at %s\n", t);   /* (session 66) */
    } else if (exit == HZ_LOOP_GAME_OVER) {
        printf("P%d %s won, at %s\n", g->winner + 1, who(p, g->winner), t);
    } else {
        printf("no winner: %s at %s\n", r->why, t);
    }
    char x[200], y[200];
    printf("  %-30s %-24s %s\n", "", "P1", "P2");
    rowi("mech destroyed", a->deaths, b->deaths);
    rowi("  out of fuel", a->deaths_fuel, b->deaths_fuel);
    rowi("  shot down as a jet", a->deaths_air, b->deaths_air);
    rowi("  shot down carrying", a->deaths_carrying, b->deaths_carrying);
    rowi("  destroyed on foot", a->deaths_foot, b->deaths_foot);
    rowi("  the enemy mech near, firing", a->deaths_by_mech,
         b->deaths_by_mech);
    rowi("gun: shots", a->shots, b->shots);
    rowi("hits taken (mech)", a->hits_taken, b->hits_taken);
    rowi("HP lost (mech)", a->hp_lost, b->hp_lost);
    rowi("units bought", a->bought_count, b->bought_count);
    bought_text(a, x, sizeof x, false);
    bought_text(b, y, sizeof y, false);
    printf("    P1: %s\n    P2: %s\n", x, y);
    for (int s = 0; s < 2; s++) {
        const Side *d = &p->side[s];
        size_t n = 0;
        x[0] = '\0';
        for (int o = 0; o < 16; o++) {
            if (d->order[o] > 0) {
                n += (size_t)snprintf(x + n, sizeof x - n, "%s%s %d",
                                      n ? ", " : "", ORDER_NAME[o],
                                      d->order[o]);
            }
        }
        printf("    P%d orders: %s\n", s + 1, n ? x : "none");
    }
    rowi("money spent", a->spent, b->spent);
    rowi("most money left unspent", a->money_most, b->money_most);
    rowi("tried to buy short of money", a->refused, b->refused);
    rowi("units carried", a->pickups, b->pickups);
    rowi("  put down by the enemy HQ", a->drops_near_enemy_hq,
         b->drops_near_enemy_hq);
    rowi("  put down by its own HQ", a->drops_near_own_hq,
         b->drops_near_own_hq);
    rowi("units lost", a->units_lost_count, b->units_lost_count);
    bought_text(a, x, sizeof x, true);
    bought_text(b, y, sizeof y, true);
    printf("    P1: %s\n    P2: %s\n", x, y);
    rowi("outposts taken", a->posts_taken, b->posts_taken);
    rowi("outposts lost", a->posts_lost, b->posts_lost);
    rowi("outposts held at most", a->posts_most, b->posts_most);
    long all = (long)p->frame;
    const char *labels[5] = { "time flying", "time carrying", "time on foot",
                              "time in the order menu", "time destroyed" };
    for (int k = 0; k < 5; k++) {
        long va = k == 0 ? a->t_jet : k == 1 ? a->t_carry : k == 2 ? a->t_foot
                : k == 3 ? a->t_menu : a->t_dead;
        long vb = k == 0 ? b->t_jet : k == 1 ? b->t_carry : k == 2 ? b->t_foot
                : k == 3 ? b->t_menu : b->t_dead;
        pct(x, sizeof x, va, all);
        pct(y, sizeof y, vb, all);
        row(labels[k], x, y);
    }
    pct(x, sizeof x, a->t_own_hq, all);
    pct(y, sizeof y, b->t_own_hq, all);
    row("time by its own HQ", x, y);
    pct(x, sizeof x, a->t_enemy_hq, all);
    pct(y, sizeof y, b->t_enemy_hq, all);
    row("time by the enemy HQ", x, y);
    rowi("fuel under a quarter (times)", a->low_fuel, b->low_fuel);
    rowi("A presses", a->presses_a, b->presses_a);
    rowi("B presses (fire)", a->presses_b, b->presses_b);
    rowi("  of them 2 frames apart", a->b_gap2, b->b_gap2);
    rowi("C presses (menu)", a->presses_c, b->presses_c);
    rowi("HQ HP at the lowest", a->hq_hp_min, b->hq_hp_min);
    for (int s = 0; s < 2; s++) {
        const Side *d = &p->side[s];
        size_t n = 0;
        x[0] = '\0';
        for (int m = 0; m < 64; m++) {
            if (d->hq_lost[m] > 0) {
                n += (size_t)snprintf(x + n, sizeof x - n, "%s%d:%d",
                                      n ? " " : "", m, d->hq_lost[m]);
            }
            if (n >= sizeof x - 12) {
                break;
            }
        }
        printf("    P%d's HQ, HP lost by minute: %s\n", s + 1,
               n ? x : "none");
    }
    for (int s = 0; s < 2; s++) {
        u8 mode = r->options[0] & 3;
        bool cpu = (s == 0) ? (mode & 2) != 0 : (mode & 1) != 0;
        if (cpu && r->brain[s] == HZ_BRAIN_NIGHTMARE) {
            printf("  P%d the nightmare AI's own counts: %s\n", s + 1,
                   nightmare_summary(g, s));
        }
    }
    if (p->minutes) {
        printf("  min   money P1 P2   posts   units   HQ P1 P2   deaths\n");
        for (int i = 0; i < p->minute_count; i++) {
            printf("%s\n", p->minute[i]);
        }
    }
    if (p->events) {
        printf("  events:\n");
        for (int i = 0; i < p->event_count; i++) {
            printf("%s\n", p->event[i]);
        }
    }
    printf("\n");
}

static int usage(void)
{
    fprintf(stderr, "usage: hz_replay ASSETS FILE... [--events] [--minutes] "
                    "[--check]\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        return usage();
    }
    bool events = false, minutes = false, check = false;
    int files = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--events") == 0) {
            events = true;
        } else if (strcmp(argv[i], "--minutes") == 0) {
            minutes = true;
        } else if (strcmp(argv[i], "--check") == 0) {
            check = true;
        } else if (strncmp(argv[i], "--", 2) == 0) {
            return usage();
        } else {
            files++;
        }
    }
    if (files == 0) {
        return usage();
    }
    static HzAssets assets;
    hz_file_hash_begin();
    if (!hz_assets_load(&assets, argv[1])) {
        fprintf(stderr, "hz_replay: %s: can't read the assets\n", argv[1]);
        return 1;
    }
    char game[17];
    snprintf(game, sizeof game, "%016llX",
             (unsigned long long)net_game_id(hz_file_hash_end()));
    static Video video;
    static HzGame g;
    int bad = 0;
    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--", 2) == 0) {
            continue;
        }
        Play *p = &g_play;
        for (int k = 0; k < p->event_count; k++) {
            free(p->event[k]);
        }
        replay_free(&p->r);
        memset(p, 0, sizeof *p);
        char err[128];
        if (!replay_load(&p->r, argv[i], err, sizeof err)) {
            fprintf(stderr, "hz_replay: %s: %s\n", argv[i], err);
            bad++;
            continue;
        }
        if (strcmp(p->r.game, game) != 0) {
            fprintf(stderr, "hz_replay: %s: played on another build or "
                            "other mods (game %s, here %s): it may not "
                            "play the same\n", argv[i], p->r.game, game);
        }
        p->split = -1;
        p->events = events;
        p->minutes = minutes;
        p->fuel_max = assets.mech_stats.stats.fuel;
        video_init(&video);
        game_init(&g, &assets, &video);
        g.frame = on_frame;
        g.frame_user = p;
        g.snd_music_on = music_on;
        replay_apply_start(&g, &p->r);
        game_title_prepare(&g);
        g.game_type = 0;
        g.quick_load = true;
        if (game_setup_after_title(&g) != HZ_SETUP_GAME) {
            fprintf(stderr, "hz_replay: %s: map %d doesn't load\n", argv[i],
                    p->r.options[3] & 7);
            bad++;
            continue;
        }
        p->started = true;
        game_start(&g);
        HzLoopExit exit = game_frame(&g, false);
        while (exit == HZ_LOOP_ON) {
            exit = game_frame(&g, true);
        }
        bool same = p->split < 0
                    && (exit == HZ_LOOP_GAME_OVER) == (p->r.winner >= 0)
                    && (p->r.winner < 0 || p->r.winner == (int)g.winner);
        if (!same) {
            bad++;
        }
        if (check) {
            printf("%s: %s (%u frames, %u checks alike%s)\n", argv[i],
                   same ? "the game recorded" : "NOT the game recorded",
                   (unsigned)p->frame, (unsigned)p->checked,
                   p->split >= 0 ? ", then a difference" : "");
        } else {
            report(p, &g, argv[i], exit);
        }
    }
    return bad ? 1 : 0;
}
