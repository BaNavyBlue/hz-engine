/* hz_arena: whole games against the computer with no window and no sound,
 * as fast as they run, for the nightmare AI (src/game/nightmare.c): each
 * side is played by
 *   cpu    the original's CPU (mech_ai.c), with the CPU's allowances
 *   nm     the nightmare AI as the CPU, the same allowances
 *   bot    the nightmare AI on a human's pad: no CPU allowance, the
 *          human's handicaps (fuel, the fire delay, the CPU's boosts
 *          against it), the way a player would meet the CPU
 *   turtle (session 67) the bot playing a player's way, as the owner
 *          beat the nightmare AI: a wall of standing units round its HQ,
 *          then attack units put down by the enemy HQ (nightmare.c's
 *          style 1)
 *   idle   nobody (the pad left alone)
 *   old    (make hz_arena_ab OLD=file) another nightmare.c, as the CPU:
 *          the code's own A/B
 *
 *     hz_arena ASSETS [--map M] [--difficulty D] [--p1 KIND] [--p2 KIND]
 *              [--seed S] [--games N] [--frames N] [--fixes HEX]
 *              [--param NAME=VALUE ...] [--param1|--param2 NAME=VALUE]
 *              [--trace FILE] [--every N]
 *              [--from FRAME] [--deaths FILE] [--spawn N]
 *              [--spawn-type T] [--spawn-step PX] [--spawn-order O]
 *              [--check-crowd] [--check-hits] [--toggle N] [--trucks N]
 *              [--png PREFIX --png-at F[,F...]]
 *              [--delay1 N] [--delay2 N] [--ahead] [--seen S]
 *              [--replay FILE --takeover FRAME [--shadow]]
 *              [--put SIDE,TYPE,ORDER,X,Y,EVERY[,SPAN[,FROM]]]
 *              [--drain SIDE,EVERY[,FROM[,HURT]]]
 *     hz_arena ASSETS --info
 *     hz_arena ASSETS --map M --ground
 *     hz_arena ASSETS --map M --atlas [SALTS[,FRAMES[,SPAN]]] [--fixes HEX]
 *     hz_arena ASSETS --params
 *
 * A game starts as `hz --map M --mode --difficulty D` does, with v_option2
 * as the options screen sets it for the difficulty (the CPU's style), the
 * random seed from S (game n of --games: S + n), and the fixes the
 * PATCHES page's default unless --fixes. It runs to an HQ's fall or
 * --frames (default 216000, an hour of play: a draw). One line a game on
 * stdout:
 *   R map diff p1 p2 seed winner frames hq1 hq2 posts1 posts2 units1
 *     units2 built1 built2 deaths1 deaths2 money1 money2
 * winner 1 or 2, 0 a draw; (session 68) then P avg1 avg2, each side's
 * outposts held, the game's average. --param sets the nightmare AI's tuning
 * (nightmare.c's PARAM_NAME) for both its sides, --param1/--param2 for
 * one; --trace writes a line every
 * --every frames (default 600; from --from on): the mechs, the money,
 * the outposts, (session 68) an O line of each outpost's owner (1, 2, .
 * nobody's), each side's units. --deaths writes a line a P1 unit
 * lost: where, its life, what was near. (Session 68) HZ_ARENA_MECHHITS
 * set: an MH line on stdout for each hit on a mech (frame, side, form,
 * damage, missile/gun/other, the shooter's unit type or m, its distance,
 * "dead").
 * (Session 61) --spawn N puts N units a side down at the start, round
 * each side's HQ outwards, AMRs, TAXes and INFANTRY by turns with the
 * attack-HQ order (with --fixes 80 set, UNITS: past 50 a side), and says
 * on stderr how many found room: a stress of a side past the original's
 * 50 records. --spawn-type T: only units of type T (8 INFANTRY);
 * --spawn-step: the spiral's step (24 px; less finds the gaps between
 * units already down, as 8 does for 16 px men); --spawn-order: their
 * order (ORDER_*, 2 PATROL). --check-crowd: with UNITS the grid's lists
 * checked whole every frame, and (session 68) without --toggle no two
 * units overlapping (crowd_check; exit 1 at a fault; session 75: not
 * with PIXELS, where pixels may touch with nobody moving). --toggle N:
 * UNITS turned over every N frames, as the PATCHES page can in play (the
 * grid made again each time: grid_sync). --png: the screen at the frames
 * --png-at lists, PREFIX_FRAME.png.
 * (Session 72) --trucks N: with REPAIR on (--fixes with bit 10), every N
 * frames each side gets a repair truck beside one of its vehicles (no AI
 * buys one); a T line after the R: the trucks put down, then for each
 * side its trucks at the end, the seconds its men were out (a man a
 * second) and the HP they mended.
 * (Session 62) --check-hits: with CPU HITS on, what a CPU side's hit
 * halves, at every call of the unit AI and of the original's CPU mech AI
 * (their probes): a unit loses HP there only as the unit a CPU side's
 * landed shot hit, at most halved once a shot; and the CPU mech's ground
 * shot (plan 5, step 7) halves the enemy unit it hit when it is active,
 * not dying and off screen, and goes on (step 0), its own side's
 * repositions (step 8). Exit 1 at a fault; at the end, on stderr, how
 * many of each were seen.
 * (Session 64) --delay1 N / --delay2 N: that bot side's pad is read N
 * passes after it chooses it, as in a network game (the server's AI: its
 * delay and one more); --ahead: a delayed bot side thinks on a copy of
 * the game run on to the pass its pad is read in (nightmare_think_ahead),
 * else on the game as it is; --seen S: a side thinking ahead knows a bot
 * rival's pads S passes on (its own pass's first; default all it has
 * chosen), the rest that pad held -- 1 is the server's AI, which runs
 * each frame as the person's pad comes and knows none further.
 * --check-ahead: what a side thinking ahead foresaw for each pass (the
 * copy's state at its end) against the real game's there, every part of
 * the state hash; at the end, on stderr, how many matched and which
 * parts did not; exit 1 when one did not -- exact whenever the rival's
 * pads are all known (a CPU's, or --seen as far as it goes).
 * --info prints the unit types, the orders and each map's outposts;
 * --ground the map's ground as a mech finds it (ground_chart); --params
 * the nightmare AI's tuning values and their defaults (NAME=VALUE).
 * (Session 68) --atlas the map's part of the nightmare AI's terrain
 * atlas (src/game/nm_atlas.h; tools/hzatlas.py makes nm_atlas.c from
 * it): for each side an ATLAS line (map team type salts frames fixes)
 * and 64 rows, 2 hex digits a block; SALTS drives a block (3), FRAMES
 * each (7200), the blocks within SPAN of the enemy HQ (NM_ATLAS_SPAN).
 * (Session 68) --replay FILE --takeover FRAME: the recorded game (its
 * map, TYPE and patches) played to FRAME from its pads, then both sides
 * the kinds named (bot or turtle: a recorded game has no CPU side), the
 * RNG reseeded there from --seed; --frames counts from its start. With
 * --shadow a bot side thinks before the takeover too, its pad thrown away
 * (the recorded one played): the trace then tells what it would do.
 * (Session 73) --put SIDE,TYPE,ORDER,X,Y,EVERY[,SPAN[,FROM]]: every EVERY
 * frames (from frame FROM on; after a takeover, with --replay) a unit of
 * SIDE's (1 or 2) appears at (X, Y), its x up to SPAN px either side by
 * numbers of the arena's own, if there is room: what a player's jet puts
 * down, without the jet. A U line after the R: how many were tried and
 * how many found room. With it a side's base is tried against units put
 * down where a player put his (The owner's TAX-52s in the strip north
 * of P2's HQ on ABGRUND: --put 1,1,10,170,6,900,70).
 * (Session 75) --drain SIDE,EVERY[,FROM[,HURT]]: every EVERY frames (from
 * FROM) SIDE's units standing within 400 px of its HQ lose their missiles
 * and ammo (HURT 1: and HP down to half), as a long fight empties a wall;
 * a D line after the R: the drains, then what had come back by each one
 * after the first of what they could hold (missiles, ammo, HP): its
 * SUPPLY's and repair truck's work. */

#include "../src/core/hz_vdp.h"
#include "../src/core/pngwrite.h"
#include "../src/game/game.h"
#include "../src/game/nightmare.h"
#include "../src/game/nm_atlas.h"
#include "../src/game/replay.h"
#include "../src/game/statehash.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

typedef enum { KIND_IDLE, KIND_CPU, KIND_NM, KIND_BOT, KIND_TURTLE, KIND_OLD }
    Kind;

static const char *const KIND_NAME[6] = { "idle", "cpu", "nm", "bot",
                                          "turtle", "old" };

/* A side the nightmare AI plays on a human's pad (bot, turtle). */
static bool on_pad(Kind k)
{
    return k == KIND_BOT || k == KIND_TURTLE;
}

/* (Session 67) The turtle's tuning, set when its side is named (a
 * --param1/--param2 after it changes it): a player's way, no siege, its
 * attack units put down right by the enemy HQ (The owner's were 119-152
 * px from it), and the enemy jet chased only when it comes at it (chasing
 * it as it carries lost the turtle a fifth of its wins). (Session 69)
 * Without the nightmare AI's answers to a player's ways (DOG_GUARD,
 * POST_RAID, NEST_DEATHS): a player has none of them. (Session 70) Nor
 * DRIVE_FUEL, nor REGION: kept the opponent it was; (session 73) nor
 * EDGE, nor MIX_BUYS; (session 74) nor HOLD_BACK, CAPTURE_FREE, ONE_WAY,
 * CAPTURE_HOT, FLY_STRAIGHT, FIGHT_ON, SUPPLY, REPAIR, STRIKE; (session
 * 75) nor CAPTURE_NOBODY, CAPTURE_ROBOT, CAPTURE_OPEN, HOME_LANES. */
static const char *const TURTLE_PARAMS[] = { "style=1", "siege=0",
                                             "drop_range=112",
                                             "dog_range=0", "dog_guard=0",
                                             "post_raid=0",
                                             "nest_deaths=0",
                                             "drive_fuel=0", "region=0",
                                             "edge=0", "mix_buys=0",
                                             "hold_back=0",
                                             "capture_free=0", "one_way=0",
                                             "capture_hot=0",
                                             "fly_straight=0",
                                             "fight_on=0", "supply=0",
                                             "repair=0", "strike=0",
                                             "capture_nobody=0",
                                             "capture_robot=0",
                                             "capture_open=0",
                                             "home_lanes=0", "buy_home=0",
                                             "nest_wait=0",
                                             "strike_giveup=0",
                                             "hunt_strike=0", "hunt_home=1" };

#ifdef HZ_ARENA_OLD_BRAIN
/* (make hz_arena_ab) Another nightmare.c, built with its names renamed
 * (nightmare_old_*): the side "old" is it, as the CPU. */
void nightmare_old_think(HzGame *g, int p);
const char *nightmare_old_describe(const HzGame *g, int p);
const char *nightmare_old_summary(const HzGame *g, int p);
#define KINDS 6
#else
#define KINDS 5
#endif

static bool kind_parse(const char *s, Kind *k)
{
    for (int i = 0; i < KINDS; i++) {
        if (strcmp(s, KIND_NAME[i]) == 0) {
            *k = (Kind)i;
            return true;
        }
    }
    return false;
}

/* (Session 64) Passes of chosen pads kept a side (--delay at most). */
#define ARENA_QUEUE 64

/* v_option2 by difficulty, as options.c's OPTION2_BY_TYPE */
static const u8 OPTION2_BY_TYPE[4] = { 0xFF, 0xAA, 0x55, 0x00 };

typedef struct {
    Kind  kind[2];
    int   limit;
    int   frames;
    FILE *trace;
    int   every;
    int   from;          /* --from: no trace before this frame */
    int   deaths[2];
    bool  was_dead[2];
    FILE *deaths_log;       /* --deaths: each P1 unit's end */
    u8   *unit_flags;       /* P1's slots, unit_cap of them */
    int  *unit_born;
    u32   unit_cap;
    u16   hq_was[2];        /* the HQs' HP last frame (--deaths) */
    u16   mech_was[2];      /* (session 68) the mechs' HP last frame */
    bool  check_crowd;      /* --check-crowd */
    bool  check_hits;       /* --check-hits */
    int   toggle;           /* --toggle */
    /* (Session 73) --put: side (0, 1), type, order, x, y, frames between
     * them, the span of x, the first frame; its own numbers; tried and
     * placed */
    int   put[8];
    u32   put_rng;
    long  puts, puts_placed;
    /* (Session 75) --drain: side (0, 1), frames between, the first, 1 to
     * halve their HP too; drains done, and at each after the first what
     * had come back since: missiles, ammo, HP and what they could hold */
    int   drain[4];
    long  drains;
    long  back_miss, full_miss, back_ammo, full_ammo, back_hp, full_hp;
    int   trucks;           /* (session 72) --trucks: frames between them */
    u32   truck_rng;        /* its own numbers: the game's left alone */
    long  trucks_put;
    long  men_frames[2];    /* a side's mechanics out, summed each frame, */
    long  mend_frames[2];   /* and those of them mending */
    const char *png;        /* --png PREFIX */
    const char *png_at;     /* --png-at: the frames, by commas */
    /* (Session 64) Delayed bot sides: the pads chosen, by pass. */
    int   delay[2];         /* --delay1, --delay2 */
    bool  ahead;            /* --ahead */
    int   seen;             /* --seen (-1: all chosen) */
    bool  queued[2];        /* the side's pad comes from its queue */
    u32   pass;             /* game_frame passes this game */
    u8    queue[2][ARENA_QUEUE];
    /* --check-ahead: the copy's state at the end of the pass it thought
     * in, to meet the real game's there */
    bool  check_ahead;
    bool  expect_set[2][ARENA_QUEUE];
    u64   expect[2][ARENA_QUEUE][STATE_HASH_PARTS];
    long  checked, exact, part_off[STATE_HASH_PARTS];
    /* (Session 68) --replay: the recording played to --takeover, both
     * sides then the kinds named */
    HzReplay *rep;
    int   takeover;
    bool  shadow;               /* --shadow: a bot side thinks before it */

    unsigned long long reseed;  /* the RNG's at the takeover */
} Arena;

static Arena g_arena;

/* (session 68) HZ_ARENA_MECHHITS: a line for each hit on a mech: MH frame
 * side(1/2) form damage what (missile, gun or other) the shooter (a unit
 * type, or m for the mech) and its distance, then "dead" when it killed. */
static void mech_hits(HzGame *g, Arena *a)
{
    for (int p = 0; p < 2; p++) {
        const HzMech *m = &g->mech[p];
        bool dead = (m->flags & MECH_DESTROYED) != 0;
        u16 was = a->mech_was[p];
        a->mech_was[p] = m->hp;
        if (a->was_dead[p] || (!dead && m->hp >= was)) {
            continue;
        }
        int dmg = dead ? was : was - m->hp;
        const char *what = "other";
        char who[16] = "-";
        int dist = -1;
        for (int i = 0; i < 32; i++) {
            const HzShot *sh = &g->shots[(1 - p) * 32 + i];
            if (!(sh->flags & SHOT_ACTIVE) || !(sh->flags & SHOT_EXPLODING)
                || abs(sh->x - (m->x + 12)) > 40
                || abs(sh->y - (m->y + 12)) > 40) {
                continue;
            }
            what = (i >= 24) ? "missile" : "gun";
            if (sh->owner.kind == REF_MECH) {
                snprintf(who, sizeof who, "m");
                dist = abs(g->mech[1 - p].x - m->x);
                int dy = abs(g->mech[1 - p].y - m->y);
                dist = (dy > dist) ? dy : dist;
            } else if (sh->owner.kind == REF_UNIT) {
                const HzUnit *u = ref_unit_ptr_c(g, sh->owner);
                if (u != NULL) {
                    snprintf(who, sizeof who, "%u", u->type);
                    dist = abs(u->x - m->x);
                    int dy = abs(u->y - m->y);
                    dist = (dy > dist) ? dy : dist;
                }
            }
            break;
        }
        printf("MH %d %d %u %d %s %s %d%s\n", a->frames, p + 1, m->form, dmg,
               what, who, dist, dead ? " dead" : "");
    }
}

/* Room for P1's slots in the --deaths lists (new ones zeroed). */
static void arena_track(Arena *a, u32 slots)
{
    if (slots <= a->unit_cap) {
        return;
    }
    u8 *f = realloc(a->unit_flags, slots * sizeof *f);
    int *b = realloc(a->unit_born, slots * sizeof *b);
    if (f == NULL || b == NULL) {
        fprintf(stderr, "hz_arena: out of memory\n");
        exit(1);
    }
    memset(f + a->unit_cap, 0, (slots - a->unit_cap) * sizeof *f);
    memset(b + a->unit_cap, 0, (slots - a->unit_cap) * sizeof *b);
    a->unit_flags = f;
    a->unit_born = b;
    a->unit_cap = slots;
}

/* --spawn: n units for each side, by turns, on a square spiral of 24 px
 * steps out from its HQ, wherever Unit_Spawn finds room on its own half
 * of the map (nearer its HQ than the enemy's). */
typedef struct {
    int x, y, dx, dy, run, left, turns, placed;
} Spiral;

static void spiral_step(Spiral *s)
{
    s->x += s->dx;
    s->y += s->dy;
    if (--s->left == 0) {
        int t = s->dx;
        s->dx = -s->dy;
        s->dy = t;
        if (++s->turns % 2 == 0) {
            s->run++;
        }
        s->left = s->run;
    }
}

typedef struct {
    int n;                  /* a side */
    int type;               /* -1: AMR, TAX, INFANTRY by turns */
    int step;               /* px */
    int order;
} Crowd;

static void spawn_crowd(HzGame *g, const Crowd *cr)
{
    static const u8 TYPES[3] = { UNIT_AMR, UNIT_TAX, UNIT_INFANTRY };
    int n = cr->n;
    grid_sync(g);           /* the grid UNITS asks for, before the first */
    Spiral sp[2];
    memset(sp, 0, sizeof sp);
    for (int side = 0; side < 2; side++) {
        sp[side].dx = 1;
        sp[side].run = 1;
        sp[side].left = 1;
    }
    for (long tries = 0; tries < 4000000; tries++) {
        int side = (int)(tries & 1);
        Spiral *s = &sp[side];
        if (s->placed >= n) {
            if (sp[side ^ 1].placed >= n) {
                break;
            }
            continue;
        }
        const HzPost *own = &g->posts[HZ_POST_HQ + side];
        const HzPost *foe = &g->posts[HZ_POST_HQ + 1 - side];
        long px = own->x + s->x * (long)cr->step;
        long py = own->y + s->y * (long)cr->step;
        long d_own = (px - own->x) * (px - own->x)
                     + (py - own->y) * (py - own->y);
        long d_foe = (px - foe->x) * (px - foe->x)
                     + (py - foe->y) * (py - foe->y);
        spiral_step(s);
        if (px <= 8 || py <= 8 || px >= 0x7E0 || py >= 0x7E0
            || d_own >= d_foe) {
            continue;
        }
        u8 type = cr->type >= 0 ? (u8)cr->type : TYPES[s->placed % 3];
        if (unit_spawn(g, (u8)(UNIT_ACTIVE | (side ? UNIT_TEAM2 : 0)),
                       type, (u8)cr->order,
                       (u8)((s->placed * 5) & 0x0F), (s16)px, (s16)py)
            == 0) {
            s->placed++;
        }
    }
    for (int side = 0; side < 2; side++) {
        fprintf(stderr, "hz_arena: side %d: %d of %d units placed, %lu "
                        "slots\n", side + 1, sp[side].placed, n,
                (unsigned long)side_slots(g, side));
    }
}

/* (Session 72) --trucks N: every N frames each side gets a repair truck
 * (REPAIR: --fixes with bit 10) put down beside one of its vehicles,
 * picked by numbers of the arena's own -- no AI buys one, so this is how
 * whole games get them: trucks driven, shot and carried off, their men
 * out among the fighting. */
static void spawn_trucks(HzGame *g, Arena *a)
{
    if (!repair_on(g)) {
        return;
    }
    for (int side = 0; side < 2; side++) {
        u32 slots = side_slots(g, side);
        const HzUnit *by = NULL;
        for (int tries = 0; tries < 16 && by == NULL; tries++) {
            a->truck_rng = a->truck_rng * 1664525u + 1013904223u;
            const HzUnit *u = unit_at_c(g, side, (a->truck_rng >> 8) % slots);
            if ((u->flags & UNIT_ACTIVE)
                && !(u->flags & (UNIT_DYING | UNIT_CARRIED))
                && u->type < UNIT_INFANTRY) {
                by = u;
            }
        }
        for (int d = 0; by != NULL && d < 16; d++) {
            static const s8 DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
            static const s8 DY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
            int far = 40 + (d / 8) * 32;
            HzUnit *t = unit_spawn_unit(g, (u8)(UNIT_ACTIVE
                                                | (side ? UNIT_TEAM2 : 0)),
                                        UNIT_REPAIR, ORDER_REPAIR, 0,
                                        (s16)(by->x + DX[d % 8] * far),
                                        (s16)(by->y + DY[d % 8] * far));
            if (t != NULL) {
                t->speed = 0;
                a->trucks_put++;
                break;
            }
        }
    }
}

/* (Session 73) --put: the next unit, where there is room for it (as
 * Unit_Spawn finds), standing till its first think as one put down is. */
static void put_unit(HzGame *g, Arena *a)
{
    int x = a->put[3];
    if (a->put[6] > 0) {
        a->put_rng = a->put_rng * 1664525u + 1013904223u;
        x += (int)((a->put_rng >> 8) % (u32)(2 * a->put[6] + 1)) - a->put[6];
    }
    a->puts++;
    HzUnit *u = unit_spawn_unit(g, (u8)(UNIT_ACTIVE
                                        | (a->put[0] ? UNIT_TEAM2 : 0)),
                                (u8)a->put[1], (u8)a->put[2], 0, (s16)x,
                                (s16)a->put[4]);
    if (u != NULL) {
        u->speed = 0;
        a->puts_placed++;
    }
}

/* (Session 75) --drain: a side's units standing within 400 px of its HQ
 * (but its trucks and the mechanics) emptied -- their missiles and ammo,
 * and with drain[3] half their HP gone -- as a long fight would; first
 * what came back since the last drain is counted, which is its SUPPLY's
 * and repair truck's work. */
static void drain_units(HzGame *g, Arena *a)
{
    int side = a->drain[0];
    const HzPost *hq = &g->posts[HZ_POST_HQ + side];
    bool count = a->drains > 0;
    a->drains++;
    for (u32 i = 0; i < side_slots(g, side); i++) {
        HzUnit *u = unit_at(g, side, i);
        int dx = u->x + u->size / 2 - hq->x, dy = u->y + u->size / 2 - hq->y;
        if (!(u->flags & UNIT_ACTIVE)
            || (u->flags & (UNIT_DYING | UNIT_CARRIED))
            || u->order != ORDER_STAND || u->type == UNIT_SUPPLY
            || u->type >= UNIT_REPAIR
            || (dx < 0 ? -dx : dx) > 400 || (dy < 0 ? -dy : dy) > 400) {
            continue;
        }
        const HzUnitStats *st = unit_stats_for(g, u->type);
        if (count) {
            a->back_miss += u->missiles;
            a->full_miss += st->missiles;
            a->back_ammo += u->ammo;
            a->full_ammo += st->ammo;
            a->back_hp += u->hp;
            a->full_hp += st->hp;
        }
        u->missiles = 0;
        u->ammo = 0;
        if (a->drain[3] && u->hp > st->hp / 2) {
            u->hp = (u16)(st->hp / 2);
        }
    }
}

static int own_posts(const HzGame *g, int team)
{
    int n = 0;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        u8 f = g->posts[i].flags;
        if ((f & POST_USED) && (f & POST_OWNED)
            && (f & POST_TEAM2) == (team ? POST_TEAM2 : 0)) {
            n++;
        }
    }
    return n;
}

static void trace_line(HzGame *g, Arena *a)
{
    fprintf(a->trace, "T %d", a->frames);
    for (int p = 0; p < 2; p++) {
        const HzMech *m = &g->mech[p];
        fprintf(a->trace, " | P%d %s hq %u $%u posts %d units %d hp %u "
                "fuel %u ammo %u form %u at %d,%d", p + 1,
                KIND_NAME[a->kind[p]], post_hq_hp(&g->posts[HZ_POST_HQ + p]),
                m->money, own_posts(g, p), g->unit_count[p], m->hp, m->fuel,
                m->ammo, m->form, m->x, m->y);
        if (a->kind[p] == KIND_NM || on_pad(a->kind[p])) {
            fprintf(a->trace, " %s", nightmare_describe(g, p));
        }
#ifdef HZ_ARENA_OLD_BRAIN
        if (a->kind[p] == KIND_OLD) {
            fprintf(a->trace, " %s", nightmare_old_describe(g, p));
        }
#endif
    }
    fputc('\n', a->trace);
    if (getenv("HZ_ARENA_SHOTS") != NULL) {
        /* P1's bullets in flight: flags, x, y, life */
        fprintf(a->trace, "  S1");
        for (int i = 0; i < 24; i++) {
            const HzShot *sh = &g->shots[i];
            if (sh->flags & SHOT_ACTIVE) {
                fprintf(a->trace, " %02X@%d,%d/%u", sh->flags, sh->x, sh->y,
                        sh->life);
            }
        }
        fputc('\n', a->trace);
    }
    /* (session 68) each outpost's owner: 1, 2, or . nobody's */
    fprintf(a->trace, "  O ");
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        u8 f = g->posts[i].flags;
        if (f & POST_USED) {
            fputc(!(f & POST_OWNED) ? '.' : (f & POST_TEAM2) ? '2' : '1',
                  a->trace);
        }
    }
    if (getenv("HZ_ARENA_HOLDS") != NULL) {
        /* (session 74) each outpost's holds, P1's/P2's (0-4: the fourth
         * of a side's INFANTRY in turns it) */
        fprintf(a->trace, " H");
        for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
            if (g->posts[i].flags & POST_USED) {
                fprintf(a->trace, " %u/%u", g->posts[i].hold[0],
                        g->posts[i].hold[1]);
            }
        }
    }
    fputc('\n', a->trace);
    /* each side's units: type/order(hp)@x,y and the distance to the
     * enemy HQ in blocks */
    for (int t = 0; t < 2; t++) {
        const HzPost *ehq = &g->posts[HZ_POST_HQ + 1 - t];
        fprintf(a->trace, "  U%d", t + 1);
        for (u32 i = 0; i < side_slots(g, t); i++) {
            const HzUnit *u = unit_at_c(g, t, i);
            if (!(u->flags & UNIT_ACTIVE)) {
                continue;
            }
            int dx = (u->x - ehq->x) / 32, dy = (u->y - ehq->y) / 32;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            fprintf(a->trace, " %u/%X(%u)%s%d", u->type, u->order, u->hp,
                    (u->flags & UNIT_CARRIED) ? "c" : "@",
                    dx > dy ? dx : dy);
            if (getenv("HZ_ARENA_UNITXY") != NULL) {
                /* where it is, its fuel, ammo and missiles */
                fprintf(a->trace, "[%d,%d f%u a%u m%u]", u->x, u->y, u->fuel,
                        u->ammo, u->missiles);
                if (getenv("HZ_ARENA_TARGET") != NULL) {
                    /* (session 68) its target (kind:side:slot), the
                     * direction to it + 1, a shot out, the fire delay */
                    fprintf(a->trace, "{t%u:%u:%u d%u s%u fd%u}",
                            u->ai.target_id.kind, u->ai.target_id.side,
                            u->ai.target_id.slot, u->ai.target_dir,
                            u->ai.shot_out, u->fire_delay);
                }
            }
        }
        fputc('\n', a->trace);
    }
}

/* --png-at lists frame f. */
static bool png_frame(const char *list, int f)
{
    for (const char *p = list; p != NULL && *p != '\0';) {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) {
            return false;
        }
        if (v == f) {
            return true;
        }
        p = (*end == ',') ? end + 1 : end;
    }
    return false;
}

static void write_png(HzGame *g, const char *prefix, int f)
{
    static u8 idx[SCREEN_W * SCREEN_H];
    static u8 rgb[SCREEN_W * SCREEN_H * 3];
    vdp_render(g->vdp, idx);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        vdp_cram_rgb(g->vdp->cram[idx[i] & 63], rgb + i * 3);
    }
    char path[512];
    snprintf(path, sizeof path, "%s_%d.png", prefix, f);
    if (!png_write(path, SCREEN_W, SCREEN_H, 8, 2, rgb, SCREEN_W * 3, NULL,
                   0, NULL)) {
        fprintf(stderr, "hz_arena: can't write %s\n", path);
    }
}

/* (Session 62) --check-hits: each unit's HP as an AI's call began, the
 * landed hits waiting on a CPU side's units then, the CPU mech's ground
 * shot waiting, and what was seen. */
typedef struct {
    u16   *hp[2];
    u8    *flags[2];
    u32    slots[2];
    u32    cap[2];
    HzRef *victims;         /* the CPU sides' units' hits waiting */
    u32    n_victims;
    u32    victims_cap;
    bool   ground_hit;      /* the CPU mech's ground shot hit, waiting */
    HzRef  ground_victim;
    long   unit_halvings;   /* units halved by a CPU side's unit's hit */
    long   unit_mech_hits;  /* a CPU side's unit's hits on a mech */
    long   ground_unit_hits;    /* the CPU mech's on units */
    long   ground_last_slot;    /* ... on a side's slot 49 (HZ_SIDE_UNITS) */
} HitCheck;

static HitCheck g_hits;

static void hits_fault(HzGame *g, const char *what, int side, u32 slot,
                       u16 was, u16 now)
{
    fprintf(stderr, "hz_arena: --check-hits at frame %d: %s (side %d's "
                    "slot %u, HP %u to %u)\n", g_arena.frames, what, side + 1,
            slot, was, now);
    (void)g;
    exit(1);
}

/* Every unit's HP and flags now. */
static void hits_snapshot(HzGame *g, HitCheck *h)
{
    for (int s = 0; s < 2; s++) {
        u32 n = side_slots(g, s);
        if (n > h->cap[s]) {
            u16 *hp = realloc(h->hp[s], n * sizeof *hp);
            u8 *fl = realloc(h->flags[s], n * sizeof *fl);
            if (hp == NULL || fl == NULL) {
                fprintf(stderr, "hz_arena: out of memory\n");
                exit(1);
            }
            h->hp[s] = hp;
            h->flags[s] = fl;
            h->cap[s] = n;
        }
        h->slots[s] = n;
        for (u32 i = 0; i < n; i++) {
            const HzUnit *u = unit_at_c(g, s, i);
            h->hp[s][i] = u->hp;
            h->flags[s][i] = u->flags;
        }
    }
}

/* How many of the waiting hits are on unit (side, slot). */
static u32 hits_on(const HitCheck *h, int side, u32 slot)
{
    u32 k = 0;
    for (u32 i = 0; i < h->n_victims; i++) {
        if (ref_eq(h->victims[i], ref_unit(side, slot))) {
            k++;
        }
    }
    return k;
}

static void on_unit_ai(HzGame *g, bool after, void *user)
{
    HitCheck *h = user;
    if (!(g->fixes & HZ_FIX_CPU_HITS)) {
        return;
    }
    if (!after) {
        hits_snapshot(g, h);
        h->n_victims = 0;
        for (int s = 0; s < 2; s++) {
            if (!(g->mech[s].flags & MECH_CPU)) {
                continue;
            }
            for (u32 i = 0; i < side_slots(g, s); i++) {
                const HzUnit *u = unit_at_c(g, s, i);
                if (!u->ai.shot_out || (u->shot_flags & OWN_SHOT_OUT)
                    || !(u->shot_flags & OWN_SHOT_HIT)) {
                    continue;
                }
                if (ref_is_mech(u->hit_victim)) {
                    h->unit_mech_hits++;
                }
                if (h->n_victims == h->victims_cap) {
                    u32 cap = h->victims_cap ? h->victims_cap * 2 : 64;
                    HzRef *v = realloc(h->victims, cap * sizeof *v);
                    if (v == NULL) {
                        fprintf(stderr, "hz_arena: out of memory\n");
                        exit(1);
                    }
                    h->victims = v;
                    h->victims_cap = cap;
                }
                h->victims[h->n_victims++] = u->hit_victim;
            }
        }
        return;
    }
    for (int s = 0; s < 2; s++) {
        u32 n = side_slots(g, s) < h->slots[s] ? side_slots(g, s)
                                               : h->slots[s];
        for (u32 i = 0; i < n; i++) {
            const HzUnit *u = unit_at_c(g, s, i);
            u16 was = h->hp[s][i];
            if (!(h->flags[s][i] & UNIT_ACTIVE) || !(u->flags & UNIT_ACTIVE)
                || u->hp >= was) {
                continue;
            }
            u32 k = hits_on(h, s, i);
            if (k == 0) {
                hits_fault(g, "the unit AI took HP from a unit no CPU "
                              "side's hit was on", s, i, was, u->hp);
            }
            if (u->hp < (u16)(was >> (k < 16 ? k : 15))) {
                hits_fault(g, "the unit AI halved a unit more often than it "
                              "was hit", s, i, was, u->hp);
            }
            h->unit_halvings++;
        }
    }
}

static void on_mech_ai(HzGame *g, int p, bool after, void *user)
{
    HitCheck *h = user;
    if (!(g->fixes & HZ_FIX_CPU_HITS)) {
        return;
    }
    const HzMech *m = &g->mech[p];
    if (!after) {
        hits_snapshot(g, h);
        h->ground_hit = m->ai.step == 7 && m->ai.ground_shot == 0xFF
                        && (m->shot_flags & OWN_SHOT_HIT)
                        && !(m->shot_flags & OWN_SHOT_OUT);
        h->ground_victim = m->hit_victim;
        return;
    }
    /* the ground shot's result taken: the hit seen, the shot cleared */
    bool taken = h->ground_hit && m->ai.ground_shot == 0
                 && !(m->shot_flags & OWN_SHOT_HIT);
    HzRef v = h->ground_victim;
    int team = (m->flags & MECH_TEAM2) ? 1 : 0;
    for (int s = 0; s < 2; s++) {
        u32 n = side_slots(g, s) < h->slots[s] ? side_slots(g, s)
                                               : h->slots[s];
        for (u32 i = 0; i < n; i++) {
            const HzUnit *u = unit_at_c(g, s, i);
            u16 was = h->hp[s][i];
            u8 fl = h->flags[s][i];
            bool hit = taken && ref_eq(v, ref_unit(s, i));
            if (!(fl & UNIT_ACTIVE) || !(u->flags & UNIT_ACTIVE)) {
                continue;
            }
            if (!hit) {
                if (u->hp < was) {
                    hits_fault(g, "the CPU mech's AI took HP from a unit its "
                                  "shot didn't hit", s, i, was, u->hp);
                }
                continue;
            }
            h->ground_unit_hits++;
            if (i == HZ_SIDE_UNITS - 1) {
                h->ground_last_slot++;
            }
            bool enemy = s != team;
            bool halve = enemy && !(fl & (UNIT_DYING | UNIT_ON_SCREEN));
            if (u->hp != (halve ? (u16)(was >> 1) : was)) {
                hits_fault(g, halve ? "the CPU mech's ground shot didn't "
                                      "halve the enemy unit it hit"
                                    : "the CPU mech's ground shot halved a "
                                      "unit it shouldn't",
                           s, i, was, u->hp);
            }
            if (m->ai.step != (enemy ? 0 : 8)) {
                hits_fault(g, enemy ? "the CPU mech didn't go on after "
                                      "hitting an enemy unit"
                                    : "the CPU mech went on after hitting "
                                      "its own side's unit",
                           s, i, was, u->hp);
            }
        }
    }
}

static bool replay_frame(HzGame *g, Arena *a);

static void on_frame(HzGame *g, void *user)
{
    Arena *a = user;
    a->frames++;
    if (a->check_crowd && !crowd_check(g, a->toggle == 0)) {
        fprintf(stderr, "hz_arena: the grid's lists are wrong at frame "
                        "%d\n", a->frames);
        exit(1);
    }
    if (a->png != NULL && png_frame(a->png_at, a->frames)) {
        write_png(g, a->png, a->frames);
    }
    if (a->toggle > 0 && a->frames % a->toggle == 0) {
        g->fixes ^= HZ_FIX_UNITS;
    }
    if (a->trucks > 0 && a->frames % a->trucks == 0) {
        spawn_trucks(g, a);
    }
    if (a->drain[1] > 0 && a->frames % a->drain[1] == 0
        && a->frames >= a->drain[2]
        && (a->rep == NULL || a->frames - 1 > a->takeover)) {
        drain_units(g, a);
    }
    if (a->put[5] > 0 && a->frames % a->put[5] == 0
        && a->frames >= a->put[7]
        && (a->rep == NULL || a->frames - 1 > a->takeover)) {
        put_unit(g, a);
    }
    if (a->trucks > 0) {
        for (int side = 0; side < 2; side++) {
            for (u32 i = 0; i < side_slots(g, side); i++) {
                const HzUnit *u = unit_at_c(g, side, i);
                if ((u->flags & UNIT_ACTIVE) && !(u->flags & UNIT_DYING)
                    && u->type == UNIT_MECHANIC) {
                    a->men_frames[side]++;
                    a->mend_frames[side] += u->ai.working;
                }
            }
        }
    }
    if (getenv("HZ_ARENA_MECHHITS") != NULL) {
        mech_hits(g, a);
    }
    for (int p = 0; p < 2; p++) {
        bool dead = (g->mech[p].flags & MECH_DESTROYED) != 0;
        if (dead && !a->was_dead[p]) {
            a->deaths[p]++;
        }
        a->was_dead[p] = dead;
    }
    if (a->deaths_log != NULL) {
        /* P1's units: each death, where (blocks from the enemy HQ), its
         * life, and what of the enemy's is near (types within 5 blocks,
         * the enemy mech's form and distance) */
        const HzPost *ehq = &g->posts[HZ_POST_HQ + 1];
        arena_track(a, side_slots(g, 0));
        for (u32 i = 0; i < side_slots(g, 0); i++) {
            const HzUnit *u = unit_at_c(g, 0, i);
            u8 was = a->unit_flags[i];
            bool born = (u->flags & UNIT_ACTIVE) && !(was & UNIT_ACTIVE);
            bool died = (u->flags & UNIT_DYING) && !(was & UNIT_DYING);
            if (born) {
                a->unit_born[i] = a->frames;
            }
            if (died) {
                int dx = (u->x - ehq->x) / 32, dy = (u->y - ehq->y) / 32;
                fprintf(a->deaths_log, "D %d t%u o%X life %d hq %d,%d near",
                        a->frames, u->type, u->order,
                        a->frames - a->unit_born[i], dx, dy);
                for (u32 j = 0; j < side_slots(g, 1); j++) {
                    const HzUnit *e = unit_at_c(g, 1, j);
                    if (!(e->flags & UNIT_ACTIVE)
                        || (e->flags & (UNIT_DYING | UNIT_CARRIED))) {
                        continue;
                    }
                    int ex = (e->x - u->x) / 32, ey = (e->y - u->y) / 32;
                    if (ex < 0) ex = -ex;
                    if (ey < 0) ey = -ey;
                    if (ex <= 5 && ey <= 5) {
                        fprintf(a->deaths_log, " %u/%X", e->type, e->order);
                    }
                }
                const HzMech *em = &g->mech[1];
                int mdx = (em->x - u->x) / 32, mdy = (em->y - u->y) / 32;
                if (mdx < 0) mdx = -mdx;
                if (mdy < 0) mdy = -mdy;
                fprintf(a->deaths_log, " | mech f%u d%d\n", em->form,
                        mdx > mdy ? mdx : mdy);
            }
            a->unit_flags[i] = u->flags;
        }
        /* P1's HQ hit: by what (P2's units shooting it: in range, the
         * attack state $FF), and what P1's mech was doing */
        u16 hp = post_hq_hp(&g->posts[HZ_POST_HQ]);
        if (a->frames > 1 && hp < a->hq_was[0]) {
            const HzPost *hq = &g->posts[HZ_POST_HQ];
            fprintf(a->deaths_log, "H %d -%u by", a->frames,
                    a->hq_was[0] - hp);
            for (u32 j = 0; j < side_slots(g, 1); j++) {
                const HzUnit *e = unit_at_c(g, 1, j);
                if (!(e->flags & UNIT_ACTIVE) || e->ai.attack_state != 0xFF) {
                    continue;
                }
                int dx = (e->x - hq->x) / 32, dy = (e->y - hq->y) / 32;
                fprintf(a->deaths_log, " %u/%X@%d,%d", e->type, e->order, dx,
                        dy);
            }
            const HzMech *m = &g->mech[0];
            int mdx = (m->x - hq->x) / 32, mdy = (m->y - hq->y) / 32;
            fprintf(a->deaths_log, " | mech f%u @%d,%d %s\n", m->form, mdx,
                    mdy, (a->kind[0] == KIND_NM || on_pad(a->kind[0]))
                    ? nightmare_task_name(g, 0) : "");
        }
        a->hq_was[0] = hp;
        a->hq_was[1] = post_hq_hp(&g->posts[HZ_POST_HQ + 1]);
    }
    if (a->trace != NULL && a->frames >= a->from
        && a->frames % a->every == 0) {
        trace_line(g, a);
    }
    if (!replay_frame(g, a)) {
        memset(g->pad_in, 0, sizeof g->pad_in);
        if (a->rep != NULL && (g->snd_pause_sent & 1) && (a->frames & 1)) {
            /* (session 68) taken over while paused: the bots don't
             * press Start (The owner's game of 31 October, 06:53, is
             * paused at 25 minutes), so Start every other frame */
            g->pad_in[0].start = true;
        }
    }
    if (a->frames >= a->limit) {
        g->quit = true;
    }
}

/* (Session 68) --replay: before the takeover the recording's pads; at
 * it, the RNG reseeded (each game its own from there). True while the
 * recording plays. (Played to its end, the owner's game of 30 October
 * ends as recorded, at the same frame with the same HQs and deaths; its
 * hashes aren't checked: the arena's start differs in something they
 * cover from frame 0, though not in play.) */
static bool replay_frame(HzGame *g, Arena *a)
{
    if (a->rep == NULL) {
        return false;
    }
    u32 f = (u32)(a->frames - 1);
    if ((int)f > a->takeover) {
        return false;
    }
    if ((int)f == a->takeover) {
        g->rng_seed ^= (u32)(a->reseed * 0x9E3779B9u);
        return false;
    }
    if (!replay_feed(a->rep, g, f)) {
        g->quit = true;
    }
    return true;
}

/* A bot side: the nightmare AI writes the pad, where Mechs_Update has
 * just put the joypad's (the frame's read, with no frame's delay).
 * (Session 64) A delayed one's is the pad it chose --delay passes ago
 * (none in the first). */
static void on_mech(HzGame *g, int p, bool after, void *user)
{
    Arena *a = user;
    if (after || !on_pad(a->kind[p])) {
        return;
    }
    if (a->rep != NULL && a->frames - 1 < a->takeover) {
        if (a->shadow) {
            /* (session 68) thinking alongside, its pad not used: the
             * trace tells what it would have done */
            u8 pad = g->mech[p].pad;
            nightmare_think(g, p);
            g->mech[p].pad = pad;
        }
        return;                 /* the recording's pad */
    }
    if (!a->queued[p]) {
        nightmare_think(g, p);
    } else {
        g->mech[p].pad = a->queue[p][a->pass % ARENA_QUEUE];
    }
}

/* (Session 64) Before pass a->pass: each delayed bot side chooses its pad
 * for the pass --delay on -- on a copy of the game run on to that pass
 * (--ahead), else on the game as it is (a copy at this pass). The copy's
 * pads: a side's own as chosen; a bot rival's as chosen, as far as
 * --seen allows, then held; a CPU rival plays itself. */
static void arena_choose(HzGame *g, Arena *a)
{
    static u8 pads[2][ARENA_QUEUE + 1];
    for (int p = 0; p < 2; p++) {
        if (!a->queued[p]) {
            continue;
        }
        int d = a->delay[p];
        int ahead = a->ahead ? d : 0;
        for (int s = 0; s < 2; s++) {
            int known = (s == p) ? d : (a->queued[s] ? a->delay[s] : 0);
            if (s != p && (a->queued[s] && s < p)) {
                known++;            /* it chose at this pass already */
            }
            if (s != p && a->seen >= 0 && a->seen < known) {
                known = a->seen;
            }
            u8 held = 0;
            for (int i = 0; i <= ahead; i++) {
                if (!on_pad(a->kind[s])) {
                    pads[s][i] = 0;
                    continue;
                }
                if (i < known) {
                    held = a->queue[s][(a->pass + (u32)i) % ARENA_QUEUE];
                } else if (i == 0) {
                    held = g->mech[s].pad;      /* the last it pressed */
                }
                pads[s][i] = held;
            }
        }
        const u8 *const both[2] = { pads[0], pads[1] };
        u32 at = (a->pass + (u32)d) % ARENA_QUEUE;
        a->queue[p][at] = nightmare_think_ahead(g, p, ahead, both);
        HzGame *c = nightmare_ahead_copy();
        if (a->check_ahead && c != NULL && ahead == d) {
            /* the real game's joypads: a bot's pad goes in its mech's */
            for (int s = 0; s < 2; s++) {
                if (a->queued[s]) {
                    c->joypad[s] = 0;
                }
            }
            c->quit = false;
            state_hash_parts(c, a->expect[p][at]);
            a->expect_set[p][at] = true;
        }
    }
}

/* --check-ahead: after a pass, the real game against what a side
 * thinking ahead foresaw for it. */
static void arena_check_ahead(HzGame *g, Arena *a)
{
    u64 part[STATE_HASH_PARTS];
    bool have = false;
    for (int p = 0; p < 2; p++) {
        u32 at = a->pass % ARENA_QUEUE;
        if (!a->expect_set[p][at]) {
            continue;
        }
        a->expect_set[p][at] = false;
        if (!have) {
            state_hash_parts(g, part);
            have = true;
        }
        a->checked++;
        bool same = true;
        for (int i = 0; i < STATE_HASH_PARTS; i++) {
            if (part[i] != a->expect[p][at][i]) {
                a->part_off[i]++;
                same = false;
            }
        }
        a->exact += same;
    }
}

#ifdef HZ_ARENA_OLD_BRAIN
static void old_brain(HzGame *g, int p, void *user)
{
    (void)user;
    nightmare_old_think(g, p);
}
#endif

static bool music_on(void *user)
{
    (void)user;
    return true;
}

static void info(const HzAssets *as)
{
    printf("type name     price  hp size ammo fuel spd fuse sspd slife fdel "
           "pow sight build miss msp mlife mdel orders\n");
    for (int t = 0; t < HZ_UNIT_TYPES; t++) {
        const HzUnitStats *s = &as->unit_stats[t];
        printf("%2d   %-8.8s %5u %4u %3u %4u %4u %3u %4u %4u %5u %4u %3u "
               "%5u %5u %4u %3u %5u %4u %04X\n", t, "", s->price, s->hp,
               s->size, s->ammo, s->fuel, s->speed, s->fuel_use,
               s->shot_speed, s->shot_life, s->fire_delay, s->power,
               s->sight, s->build_time, s->missiles, s->missile_speed,
               s->missile_life, s->missile_delay, s->orders);
    }
    const HzMechStats *k = &as->mech_stats;
    printf("KIDOU hp %u ammo %u fuel %u shot %u/%u power %u speed %u/%u/%u "
           "fuel use %u/%u/%u\n", k->stats.hp, k->stats.ammo,
           k->stats.fuel, k->stats.shot_speed, k->stats.shot_life,
           k->stats.power, k->max_speed[0], k->max_speed[1],
           k->max_speed[2], k->fuel_use[0], k->fuel_use[1],
           k->fuel_use[2]);
    printf("order fees:");
    for (int o = 0; o < HZ_ORDERS; o++) {
        printf(" %X:%u", o, as->orders[o].fee);
    }
    printf("\n");

    static Vdp vdp;
    static HzGame game;
    for (int map = 0; map < HZ_MAPS; map++) {
        vdp_reset(&vdp);
        game_init(&game, as, &vdp);
        game.hw_version = 0xA0;
        game.options[0] = 1;
        game.options[1] = 0;
        game.options[3] = (u8)map;
        if (!game_new(&game)) {
            printf("map %d: doesn't load\n", map);
            continue;
        }
        printf("map %d buyable %04X\n", map, as->map[map].buyable);
        for (int half = 0; half < 2; half++) {
            printf("  start units, %s as the CPU:", half ? "P2" : "P1");
            for (int i = 0; i < as->start_unit_count[map][half]; i++) {
                const HzStartUnit *su = &as->start_units[map][half][i];
                printf(" %02X:t%u/o%X@%d,%d", su->flags, su->type, su->order,
                       su->x, su->y);
            }
            printf("\n");
        }
        for (int i = 0; i < HZ_POST_CLEARED; i++) {
            const HzPost *pp = &game.posts[i];
            if (pp->flags == 0) {
                continue;
            }
            printf("  post %2d flags %02X at %4d,%4d\n", i, pp->flags,
                   pp->x, pp->y);
        }
    }
}

/* --ground: the map as a mech finds the ground, a character a block
 * (the mech over its centre): . land, s special (a human only), # no,
 * l low, ~ water; outposts 0-9 and A-F by number, H and h the HQs (P1,
 * P2). */
static void ground_chart(HzGame *g)
{
    for (int row = 0; row < 64; row++) {
        for (int col = 0; col < 64; col++) {
            int x = col * 32 + 16, y = row * 32 + 16;
            char c = 0;
            for (int i = 0; i < HZ_POST_CLEARED && c == 0; i++) {
                const HzPost *pp = &g->posts[i];
                if (pp->flags != 0 && pp->x == x && pp->y == y) {
                    c = (i >= HZ_POST_HQ) ? "Hh"[i - HZ_POST_HQ]
                                          : "0123456789ABCDEF"[i & 15];
                }
            }
            if (c == 0) {
                int gr = mech_ground_at(g, &g->mech[0], (s16)(x - 16),
                                        (s16)(y - 16));
                c = ".s#l~"[gr >= 0 && gr < 5 ? gr : 2];
            }
            putchar(c);
        }
        putchar('\n');
    }
}

/* --atlas: the map's atlas, each side's (nm_atlas.h), progress on
 * stderr. */
static void atlas_chart(HzGame *g, int salts, int frames, int span)
{
    int type = UNIT_TAX;
    for (int i = 0; ; i++) {
        int def;
        const char *what;
        const char *name = nightmare_param(i, &def, &what);
        if (name == NULL) {
            break;
        }
        if (strcmp(name, "rush_type") == 0) {
            type = def;
        }
    }
    for (int p = 0; p < 2; p++) {
        int team = (g->mech[p].flags & MECH_TEAM2) ? 1 : 0;
        const HzPost *e = &g->posts[HZ_POST_HQ + (1 - team)];
        int ex = e->x >> 5, ey = e->y >> 5;
        printf("ATLAS %d %d %d %d %d %u\n", g->map, team, type, salts, frames,
               (unsigned)(g->fixes & NM_ATLAS_FIXES));
        for (int by = 0; by < 64; by++) {
            for (int bx = 0; bx < 64; bx++) {
                int v = NM_ATLAS_UNKNOWN;
                if (abs(bx - ex) <= span && abs(by - ey) <= span) {
                    int t = nightmare_atlas_block(g, p, bx, by, salts,
                                                  frames);
                    if (t == -1) {
                        v = NM_ATLAS_BAD;
                    } else if (t >= 0) {
                        v = (t + NM_ATLAS_TICK - 1) / NM_ATLAS_TICK;
                        v = v < 1 ? 1 : v > 254 ? 254 : v;
                    }
                }
                printf("%02X", v);
            }
            printf("\n");
            fprintf(stderr, "\ratlas map %d side %d row %d ", g->map, p, by);
        }
    }
    fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: hz_arena ASSETS [--map M] [--difficulty D] "
                        "[--p1 KIND] [--p2 KIND] [--seed S] [--games N] "
                        "[--frames N] [--fixes HEX] [--param N=V] "
                        "[--trace FILE] [--every N] | --info\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    Arena *a = &g_arena;
    int map = 0, difficulty = 0, games = 1;
    unsigned long long seed = 1;
    u32 fixes = HZ_FIX_DEFAULT;
    a->kind[0] = KIND_BOT;
    a->kind[1] = KIND_CPU;
    a->limit = 216000;
    a->every = 600;
    a->seen = -1;
    const char *trace = NULL;
    bool ground = false;
    int atlas_salts = 0, atlas_frames = 7200, atlas_span = NM_ATLAS_SPAN;
    Crowd crowd = { 0, -1, 24, ORDER_ATTACK_HQ };
    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (strcmp(arg, "--info") == 0) {
            info(&assets);
            return 0;
        }
        if (strcmp(arg, "--params") == 0) {
            const char *what;
            int def;
            for (int k = 0; nightmare_param(k, &def, &what) != NULL; k++) {
                printf("%s=%d  %s\n", nightmare_param(k, NULL, NULL), def,
                       what);
            }
            return 0;
        }
        if (strcmp(arg, "--ground") == 0) {
            ground = true;
            continue;
        }
        if (strcmp(arg, "--atlas") == 0) {
            atlas_salts = 3;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *end;
                atlas_salts = (int)strtol(argv[++i], &end, 10);
                if (*end == ',') {
                    atlas_frames = (int)strtol(end + 1, &end, 10);
                }
                if (*end == ',') {
                    atlas_span = (int)strtol(end + 1, NULL, 10);
                }
            }
            continue;
        }
        if (strcmp(arg, "--check-crowd") == 0) {
            a->check_crowd = true;
            continue;
        }
        if (strcmp(arg, "--shadow") == 0) {
            a->shadow = true;
            continue;
        }
        if (strcmp(arg, "--check-hits") == 0) {
            a->check_hits = true;
            continue;
        }
        if (strcmp(arg, "--ahead") == 0) {
            a->ahead = true;
            continue;
        }
        if (strcmp(arg, "--check-ahead") == 0) {
            a->check_ahead = true;
            continue;
        }
        if (val == NULL) {
            fprintf(stderr, "hz_arena: %s needs a value\n", arg);
            return 2;
        }
        i++;
        if (strcmp(arg, "--map") == 0) {
            map = atoi(val);
        } else if (strcmp(arg, "--difficulty") == 0) {
            difficulty = atoi(val);
        } else if (strcmp(arg, "--p1") == 0 || strcmp(arg, "--p2") == 0) {
            int side = arg[3] - '1';
            if (!kind_parse(val, &a->kind[side])) {
                fprintf(stderr, "hz_arena: what side is %s?\n", val);
                return 2;
            }
            if (a->kind[side] == KIND_TURTLE) {
                for (size_t t = 0; t < sizeof TURTLE_PARAMS
                                       / sizeof *TURTLE_PARAMS; t++) {
                    nightmare_set_param_side(side, TURTLE_PARAMS[t]);
                }
            }
        } else if (strcmp(arg, "--seed") == 0) {
            seed = strtoull(val, NULL, 0);
        } else if (strcmp(arg, "--games") == 0) {
            games = atoi(val);
        } else if (strcmp(arg, "--frames") == 0) {
            a->limit = atoi(val);
        } else if (strcmp(arg, "--replay") == 0) {
            static HzReplay rep;
            char err[128];
            if (!replay_load(&rep, val, err, sizeof err)) {
                fprintf(stderr, "hz_arena: %s: %s\n", val, err);
                return 1;
            }
            a->rep = &rep;
        } else if (strcmp(arg, "--takeover") == 0) {
            a->takeover = atoi(val);
        } else if (strcmp(arg, "--fixes") == 0) {
            fixes = (u32)strtoul(val, NULL, 16);
        } else if (strcmp(arg, "--param1") == 0
                   || strcmp(arg, "--param2") == 0) {
            if (!nightmare_set_param_side(arg[7] - '1', val)) {
                fprintf(stderr, "hz_arena: no parameter %s\n", val);
                return 2;
            }
        } else if (strcmp(arg, "--param") == 0) {
            if (!nightmare_set_param(val)) {
                fprintf(stderr, "hz_arena: no parameter %s\n", val);
                return 2;
            }
        } else if (strcmp(arg, "--trace") == 0) {
            trace = val;
        } else if (strcmp(arg, "--deaths") == 0) {
            a->deaths_log = fopen(val, "w");
            if (a->deaths_log == NULL) {
                fprintf(stderr, "hz_arena: can't write %s\n", val);
                return 1;
            }
        } else if (strcmp(arg, "--from") == 0) {
            a->from = atoi(val);
        } else if (strcmp(arg, "--spawn") == 0) {
            crowd.n = atoi(val);
        } else if (strcmp(arg, "--spawn-type") == 0) {
            crowd.type = atoi(val) & 0x0F;
        } else if (strcmp(arg, "--spawn-step") == 0) {
            crowd.step = atoi(val) > 0 ? atoi(val) : 24;
        } else if (strcmp(arg, "--spawn-order") == 0) {
            crowd.order = atoi(val) & 0x0F;
        } else if (strcmp(arg, "--png") == 0) {
            a->png = val;
        } else if (strcmp(arg, "--png-at") == 0) {
            a->png_at = val;
        } else if (strcmp(arg, "--toggle") == 0) {
            a->toggle = atoi(val);
        } else if (strcmp(arg, "--trucks") == 0) {
            a->trucks = atoi(val);
        } else if (strcmp(arg, "--drain") == 0) {
            a->drain[2] = 0;
            a->drain[3] = 0;
            int got = sscanf(val, "%d,%d,%d,%d", &a->drain[0], &a->drain[1],
                             &a->drain[2], &a->drain[3]);
            if (got < 2 || a->drain[0] < 1 || a->drain[0] > 2
                || a->drain[1] < 1) {
                fprintf(stderr, "hz_arena: --drain SIDE,EVERY[,FROM[,HURT]]"
                                "\n");
                return 2;
            }
            a->drain[0]--;
        } else if (strcmp(arg, "--put") == 0) {
            a->put[6] = 0;
            a->put[7] = 0;
            int got = sscanf(val, "%d,%d,%i,%d,%d,%d,%d,%d", &a->put[0],
                             &a->put[1], &a->put[2], &a->put[3], &a->put[4],
                             &a->put[5], &a->put[6], &a->put[7]);
            if (got < 6 || a->put[0] < 1 || a->put[0] > 2 || a->put[5] < 1) {
                fprintf(stderr, "hz_arena: --put SIDE,TYPE,ORDER,X,Y,EVERY"
                                "[,SPAN[,FROM]]\n");
                return 2;
            }
            a->put[0]--;
        } else if (strcmp(arg, "--every") == 0) {
            a->every = atoi(val) > 0 ? atoi(val) : 600;
        } else if (strcmp(arg, "--delay1") == 0
                   || strcmp(arg, "--delay2") == 0) {
            int d = atoi(val);
            if (d < 0 || d >= ARENA_QUEUE) {
                fprintf(stderr, "hz_arena: a delay 0-%d\n", ARENA_QUEUE - 1);
                return 2;
            }
            a->delay[arg[7] - '1'] = d;
        } else if (strcmp(arg, "--seen") == 0) {
            a->seen = atoi(val);
        } else {
            fprintf(stderr, "hz_arena: what is %s?\n", arg);
            return 2;
        }
    }
    if (map < 0 || map >= HZ_MAPS || difficulty < 0 || difficulty > 3) {
        fprintf(stderr, "hz_arena: map 0-7, difficulty 0-3\n");
        return 2;
    }
    if (trace != NULL) {
        a->trace = fopen(trace, "w");
        if (a->trace == NULL) {
            fprintf(stderr, "hz_arena: can't write %s\n", trace);
            return 1;
        }
    }

    static Vdp vdp;
    static HzGame game;
    HzGame *g = &game;
    for (int n = 0; n < games; n++) {
        vdp_reset(&vdp);
        game_init(g, &assets, &vdp);
        g->frame = on_frame;
        g->frame_user = a;
        g->mech_probe = on_mech;
        g->mech_probe_user = a;
        if (a->check_hits) {
            g->unit_ai_probe = on_unit_ai;
            g->unit_ai_probe_user = &g_hits;
            g->mech_ai_probe = on_mech_ai;
            g->mech_ai_probe_user = &g_hits;
        }
        g->snd_music_on = music_on;
        g->fixes = fixes;
        g->hw_version = 0xA0;
        g->options[0] = 5;
        g->options[2] = 0xFF;
        snd_init(g);
        /* mode: b0 P2 is a CPU, b1 P1 is (the split screen) */
        u8 mode = 0;
        if (a->kind[1] == KIND_CPU || a->kind[1] == KIND_NM
            || a->kind[1] == KIND_OLD) {
            mode |= 1;
        }
        if (a->kind[0] == KIND_CPU || a->kind[0] == KIND_NM
            || a->kind[0] == KIND_OLD) {
            mode |= 2;
        }
        g->options[0] = mode;
        g->options[1] = (u8)difficulty;
        g->options[2] = OPTION2_BY_TYPE[difficulty];
        g->options[3] = (u8)map;
        for (int p = 0; p < 2; p++) {
            g->cpu_brain[p] = (a->kind[p] == KIND_NM) ? HZ_BRAIN_NIGHTMARE
                            : (a->kind[p] == KIND_OLD) ? HZ_BRAIN_PROBE
                            : HZ_BRAIN_ORIGINAL;
        }
#ifdef HZ_ARENA_OLD_BRAIN
        g->brain_probe = old_brain;
#endif
        if (a->rep != NULL) {
            replay_apply_start(g, a->rep);
            game_title_prepare(g);
            g->game_type = 0;
            g->quick_load = true;
            if (game_setup_after_title(g) != HZ_SETUP_GAME) {
                fprintf(stderr, "hz_arena: the replay's map doesn't load\n");
                return 1;
            }
            map = g->options[3] & 7;
            difficulty = g->options[1];
            a->reseed = seed + (unsigned long long)n;
        } else if (!game_new(g)) {
            fprintf(stderr, "hz_arena: map %d doesn't load\n", map);
            return 1;
        } else {
            g->rng_seed_saved = (u32)(seed + (unsigned long long)n);
        }
        a->frames = 0;
        memset(a->deaths, 0, sizeof a->deaths);
        memset(a->was_dead, 0, sizeof a->was_dead);
        if (a->unit_cap > 0) {
            memset(a->unit_flags, 0, a->unit_cap * sizeof *a->unit_flags);
        }
        game_start(g);
        if (ground) {
            ground_chart(g);
            return 0;
        }
        if (atlas_salts > 0) {
            atlas_chart(g, atlas_salts, atlas_frames, atlas_span);
            return 0;
        }
        if (crowd.n > 0) {
            spawn_crowd(g, &crowd);
        }
        /* (Session 64) A bot side delayed, or thinking ahead: its pads
         * from its queue, from the first pass (none chosen for it). */
        for (int p = 0; p < 2; p++) {
            a->queued[p] = on_pad(a->kind[p])
                           && (a->delay[p] > 0 || a->ahead);
            memset(a->queue[p], 0, sizeof a->queue[p]);
        }
        a->pass = 0;
        memset(a->expect_set, 0, sizeof a->expect_set);
        if (a->queued[0] || a->queued[1]) {
            arena_choose(g, a);
        }
        /* (session 68) HZ_ARENA_SLOW: the slowest frame (both sides'
         * thinks with it) and the frames over 16 ms, an S line */
        bool timed = getenv("HZ_ARENA_SLOW") != NULL;
        double slowest = 0;
        int over = 0;
        /* (session 68) each side's outposts, summed once a second */
        long post_secs[2] = { 0, 0 };
        long secs = 0;
        long tick = 0;
        HzLoopExit exit = game_frame(g, false);
        while (exit == HZ_LOOP_ON) {
            if (++tick % 60 == 0) {
                post_secs[0] += own_posts(g, 0);
                post_secs[1] += own_posts(g, 1);
                secs++;
            }
            if (a->check_ahead) {
                arena_check_ahead(g, a);
            }
            a->pass++;
            if (a->queued[0] || a->queued[1]) {
                arena_choose(g, a);
            }
            struct timespec t0, t1;
            if (timed) {
                timespec_get(&t0, TIME_UTC);
            }
            exit = game_frame(g, true);
            if (timed) {
                timespec_get(&t1, TIME_UTC);
                double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
                            + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
                if (ms > slowest) {
                    slowest = ms;
                }
                over += ms > 16.0;
            }
        }
        if (timed) {
            printf("S %.1f ms slowest, %d frames over 16 ms\n", slowest,
                   over);
        }
        int winner = (exit == HZ_LOOP_GAME_OVER) ? g->winner + 1 : 0;
        printf("R %d %d %s %s %llu %d %d %u %u %d %d %d %d %u %u %d %d %u "
               "%u\n", map, difficulty, KIND_NAME[a->kind[0]],
               KIND_NAME[a->kind[1]], seed + (unsigned long long)n, winner,
               a->frames, post_hq_hp(&g->posts[HZ_POST_HQ]),
               post_hq_hp(&g->posts[HZ_POST_HQ + 1]), own_posts(g, 0),
               own_posts(g, 1), g->unit_count[0], g->unit_count[1],
               g->units_built[0], g->units_built[1], a->deaths[0],
               a->deaths[1], g->mech[0].money, g->mech[1].money);
        if (secs > 0) {
            printf("P %.2f %.2f\n", (double)post_secs[0] / (double)secs,
                   (double)post_secs[1] / (double)secs);
        }
        if (a->trucks > 0) {
            /* (session 72) T: the trucks put down, then for each side its
             * trucks at the end, the seconds its men were out (a man a
             * second) and the HP they mended */
            printf("T %ld", a->trucks_put);
            for (int side = 0; side < 2; side++) {
                printf(" %u %ld %ld", g->unit_type_count[side][UNIT_REPAIR],
                       a->men_frames[side] / 60,
                       a->mend_frames[side] / 12 * g->assets->repair.rate);
            }
            printf("\n");
            a->trucks_put = 0;
            memset(a->men_frames, 0, sizeof a->men_frames);
            memset(a->mend_frames, 0, sizeof a->mend_frames);
        }
        if (a->put[5] > 0) {
            /* (session 73) U: --put's units tried, and those with room */
            printf("U %ld %ld\n", a->puts, a->puts_placed);
            a->puts = 0;
            a->puts_placed = 0;
        }
        if (a->drain[1] > 0) {
            /* (session 75) D: drains, then what had come back by each
             * one after the first, of what the units could hold:
             * missiles, ammo, HP */
            printf("D %ld %ld/%ld %ld/%ld %ld/%ld\n", a->drains, a->back_miss,
                   a->full_miss, a->back_ammo, a->full_ammo, a->back_hp,
                   a->full_hp);
            a->drains = 0;
            a->back_miss = a->full_miss = a->back_ammo = a->full_ammo = 0;
            a->back_hp = a->full_hp = 0;
        }
        for (int p = 0; p < 2; p++) {
            if (a->kind[p] == KIND_NM || on_pad(a->kind[p])) {
                printf("N %d %s\n", p + 1, nightmare_summary(g, p));
            }
#ifdef HZ_ARENA_OLD_BRAIN
            if (a->kind[p] == KIND_OLD) {
                printf("N %d %s\n", p + 1, nightmare_old_summary(g, p));
            }
#endif
        }
        fflush(stdout);
        g->quit = false;
        /* (Session 62) and the frames, before the next game_new: the
         * count left at --frames ended the next game in its first frames
         * (on_frame's quit, from game_new's frames on) */
        a->frames = 0;
    }
    if (a->trace != NULL) {
        fclose(a->trace);
    }
    if (a->check_ahead) {
        fprintf(stderr, "hz_arena: --check-ahead: %ld passes foreseen, %ld "
                        "exactly; parts off:", a->checked, a->exact);
        for (int i = 0; i < STATE_HASH_PARTS; i++) {
            fprintf(stderr, " %ld", a->part_off[i]);
        }
        fputc('\n', stderr);
        if (a->exact != a->checked) {
            return 1;
        }
    }
    if (a->check_hits) {
        fprintf(stderr, "hz_arena: --check-hits: %ld units halved by a CPU "
                        "side's units, %ld of their hits on a mech; the CPU "
                        "mech's ground shots on units %ld (%ld on a slot "
                        "%d)\n", g_hits.unit_halvings, g_hits.unit_mech_hits,
                g_hits.ground_unit_hits, g_hits.ground_last_slot,
                HZ_SIDE_UNITS - 1);
    }
    return 0;
}
