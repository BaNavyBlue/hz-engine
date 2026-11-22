#include "game.h"
#include "paths.h"
#include "tables.h"

#ifdef HZ_ROUTE_CHECK           /* (session 72) route_mark_units' check */
#include <stdio.h>
#include <stdlib.h>
#endif

/* The units' AI (UnitAI_Update and the module under it, $5DF20-$5FC52).
 *
 * Every frame one group of units thinks: 5 slots of each team, so each
 * unit every 12th frame. A unit's update: its tick and phase + 1, its fire
 * delay - 1; then what its type does -- for most types: check its fuel,
 * follow its order (one function per order below), then by phase & 3 one
 * of: look for a target, look for the goal outpost, shoot, look further
 * for enemies. Then Unit_SetSprite turns it a step and sets its velocity.
 * Units_UpdateTeam (units.c) moves it; the shots it fires are shots.c's.
 *
 * Its state is HzUnit.ai (game.h). Objects are named by HzRef (ref.h).
 * Notes in the code:
 *   QUIRK    the original does this, probably not on purpose; kept, so
 *            the game plays as it did
 *   UNKNOWN  why, or whether it can happen, isn't known
 *   CHANGED  where the original depends on a value left over in a CPU
 *            register or on how it happens to store a pointer, this port
 *            does what the code evidently means instead; the note says
 *            what the original does
 * The 68k labels are given for cross-reference (asm/hz.asm). */

/* The context of one unit's update. */
typedef struct {
    HzGame            *g;
    HzUnit            *u;       /* its record: u->side, u->slot */
    int                team;    /* 0 or 1 */
    const HzUnitStats *st;      /* its type's UnitStats */
} Ai;

/* -- small helpers ------------------------------------------------------ */

static bool armed(const HzUnit *u)          /* tst.w obj_missiles */
{
    return u->missiles != 0 || u->ammo != 0;
}

static bool moving(const HzUnit *u)
{
    return u->speed != 0;
}

static void stop(HzUnit *u)
{
    u->speed = 0;
}

/* Full speed: unit_speed's low byte = unit_max_speed (the high byte is
 * kept; it's always 0). */
static void go(HzUnit *u)
{
    u->speed = (u16)((u->speed & 0xFF00) | u->max_speed);
}

/* unit_turret b7: it aims a turret (unit_aim_dir) apart from its heading.
 * An INFANTRY counts as one while its AI runs (see unit_infantry). */
static bool has_turret(const HzUnit *u)
{
    return (u->turret & 0x80) != 0;
}

/* unit_blocked b0/b1: its last move ran into an object or a tile. */
static bool blocked(const HzUnit *u)
{
    return (u->blocked & (PROBE_OBJECT | PROBE_TILE)) != 0;
}

static u8 dir_step(u8 dir, int step)
{
    return (u8)((dir + step) & 0x0F);
}

/* Where the object `id` names is (UnitAI_ObjFromId): a mech or a unit. */
static void obj_pos(const HzGame *g, HzRef id, s16 *x, s16 *y)
{
    if (id.kind == REF_MECH) {
        const HzMech *m = &g->mech[id.side];
        *x = m->x;
        *y = m->y;
        return;
    }
    const HzUnit *o = ref_unit_ptr_c(g, id);
    *x = o->x;
    *y = o->y;
}

/* |b - a| as GetDirectionAndDist takes it: a word, unsigned after. */
static u16 dist16(s16 a, s16 b)
{
    u16 d = (u16)(b - a);
    if (d & 0x8000) {
        d = (u16)-d;
    }
    return d;
}

/* GetDirectionAndDist: the direction from (x0, y0) to (x1, y1) and the
 * larger of |dx| and |dy|. */
static u8 dir_and_dist(s16 x0, s16 y0, s16 x1, s16 y1, u16 *dist)
{
    u16 dx = dist16(x0, x1);
    u16 dy = dist16(y0, y1);
    *dist = (dx >= dy) ? dx : dy;
    return get_direction(x0, y0, x1, y1);
}

/* Its UnitStats record for the maxima of what a SUPPLY tops up. */
static const HzUnitStats *stats_of(const HzGame *g, const HzUnit *u)
{
    return unit_stats_for(g, u->type);
}

/* The heading + 1 kept to go back to after an engagement, once. */
static void save_heading(HzUnit *u)
{
    if (u->ai.saved_dir == 0) {
        u->ai.saved_dir = (u8)(u->dir + 1);
    }
}

/* Aim at the target if its direction is known; a unit with no turret
 * turns its body to it too. */
static void aim_at_target(HzUnit *u)
{
    if (u->ai.target_dir == 0) {
        return;
    }
    u8 t = (u8)(u->ai.target_dir - 1);
    u->aim_dir = t;
    if (!has_turret(u)) {
        u->turn_to = t;
    }
}

/* A step aside by the heading's quarter: 0-4 left, 5-8 right, 9-$C left,
 * $D-$F right (capture and attack HQ, when blocked). */
static s8 quarter_step(u8 dir)
{
    if (dir <= 4) {
        return -1;
    }
    if (dir <= 8) {
        return 1;
    }
    if (dir <= 0x0C) {
        return -1;
    }
    return 1;
}

/* The enemy HQ's record: P1's for team 2, P2's for team 1. */
static const HzPost *enemy_hq(const Ai *a)
{
    return &a->g->posts[HZ_POST_HQ + (a->team ? 0 : 1)];
}

/* (x, y) & $FFF0 equal to the home's: back at the start point. */
static bool at_home(const HzUnit *u)
{
    return (((u16)u->x & 0xFFF0) == ((u16)u->ai.home_x & 0x7FF0))
        && (((u16)u->y & 0xFFF0) == ((u16)u->ai.home_y & 0xFFF0));
}

static void set_home(HzUnit *u)
{
    u->ai.home_set = true;
    u->ai.home_x = u->x;
    u->ai.home_y = u->y;
}

static u8 dir_home(const HzUnit *u, u16 *dist)
{
    return dir_and_dist(u->x, u->y, (s16)(u->ai.home_x & 0x7FFF),
                        u->ai.home_y, dist);
}

/* -- seeing (Grid_ScanRings and the target routines) ---------------------- */

/* Grid_ScanRings: the grid ids in `rings` rings of 32 px cells round the
 * cell of (x, y), nearest ring first, each ring clockwise from one cell
 * right of its top-left corner, that corner last. The centre cell isn't
 * looked at; cells off the 64 x 64 grid are skipped. Writes at most `max`
 * ids, returns how many. (The 68k also writes them, 0 between rings and $FF
 * after, to v_scan_list, with a count per kind in v_scan_counts; nothing
 * the units do reads either.) UNKNOWN: a centre right of x 2047 or below
 * y 2047 on the 68k reads the wrong row (its cell number is & $FFF); no
 * object gets there. */
int grid_scan_rings(const HzGame *g, s16 x, s16 y, int rings, HzRef *ids,
                    int max)
{
    u16 row0 = (u16)((u16)y >> 5);
    u16 col0 = (u16)((u16)x >> 5);
    int n = 0;
    for (int r = 1; r <= rings; r++) {
        int len = 2 * r;
        u16 top = (u16)(row0 - r);
        u16 left = (u16)(col0 - r);
        for (int side = 0; side < 4; side++) {
            for (int i = 1; i <= len; i++) {
                u16 row = top;
                u16 col = left;
                switch (side) {
                case 0:                         /* top, left to right */
                    col = (u16)(left + i);
                    break;
                case 1:                         /* right, downwards */
                    row = (u16)(top + i);
                    col = (u16)(left + len);
                    break;
                case 2:                         /* bottom, right to left */
                    row = (u16)(top + len);
                    col = (u16)(left + len - i);
                    break;
                default:                        /* left, upwards */
                    row = (u16)(top + len - i);
                    break;
                }
                if (row >= HZ_GRID || col >= HZ_GRID) {
                    continue;
                }
                HzRef id = g->grid[row * HZ_GRID + col];
                if (!ref_is_none(id) && n < max) {
                    ids[n++] = id;
                }
            }
        }
    }
    return n;
}

/* The enemy the unit picks among what it sees within `rings` rings of its
 * centre: the first enemy unit in scan order, else the last enemy mech
 * seen (in the attract demo the first enemy of either kind). `skip`: one
 * to pass over; `only`: if not none, the only one to look for. */
static HzRef crowd_pick_enemy(const Ai *a, int rings, HzRef skip,
                              HzRef only);

static HzRef pick_enemy(const Ai *a, int rings, HzRef skip, HzRef only)
{
    static HzRef ids[HZ_GRID * HZ_GRID];
    const HzUnit *u = a->u;
    if (a->g->crowd) {
        return crowd_pick_enemy(a, rings, skip, only);
    }
    u16 half = (u16)(u->size >> 1);
    int n = grid_scan_rings(a->g, (s16)(u->x + half), (s16)(u->y + half),
                            rings, ids, HZ_GRID * HZ_GRID);
    int own = (u->flags & UNIT_TEAM2) ? 1 : 0;
    HzRef mech = ref_none();
    for (int i = 0; i < n; i++) {
        HzRef id = ids[i];
        if (id.side == own) {
            continue;
        }
        if (!ref_is_none(skip) && ref_eq(id, skip)) {
            continue;
        }
        if (!ref_is_none(only) && !ref_eq(id, only)) {
            continue;
        }
        if (a->g->game_type < 0 || id.kind == REF_UNIT) {
            return id;
        }
        mech = id;
    }
    return mech;
}

/* crowd_pick_enemy's look at the cell (row, col): its first enemy unit,
 * else none, an enemy mech in it put in *mech (skip, only as pick_enemy's).
 * A cell off the grid: none. */
static HzRef crowd_cell_enemy(const HzGame *g, int foe, int row, int col,
                              HzRef skip, HzRef only, HzRef *mech)
{
    if (row < 0 || row >= HZ_GRID || col < 0 || col >= HZ_GRID) {
        return ref_none();
    }
    u16 cell = (u16)(row * HZ_GRID + col);
    if (crowd_mech_in(g, foe, cell)) {
        HzRef id = ref_mech(foe);
        if ((ref_is_none(skip) || !ref_eq(id, skip))
            && (ref_is_none(only) || ref_eq(id, only))) {
            *mech = id;
        }
    }
    for (const HzUnit *o = crowd_first(g, foe, cell); o != NULL;
         o = crowd_after(g, o)) {
        HzRef id = unit_ref(o);
        if ((ref_is_none(skip) || !ref_eq(id, skip))
            && (ref_is_none(only) || ref_eq(id, only))) {
            return id;
        }
    }
    return ref_none();
}

/* (Session 61) UNITS (crowd.c): pick_enemy over the cells' lists, in the
 * same ring order: in each cell the other side's mech, then its units; the
 * first enemy unit, else the last enemy mech seen. (Not in the attract
 * demo: no UNITS there.) (Session 68) The cell of its own centre first,
 * which the original never looks at: its grid held one a cell, and there
 * the unit itself. With UNITS units collide by their boxes, so an enemy's
 * centre can share the cell: a TAX-52 standing by its HQ never saw the
 * AMR-51D beside it shooting the HQ. */
static HzRef crowd_pick_enemy(const Ai *a, int rings, HzRef skip,
                              HzRef only)
{
    const HzGame *g = a->g;
    const HzUnit *u = a->u;
    u16 half = (u16)(u->size >> 1);
    int foe = (u->flags & UNIT_TEAM2) ? 0 : 1;
    int row0 = (u16)(u->y + half) >> 5;
    int col0 = (u16)(u->x + half) >> 5;
    HzRef mech = ref_none();
    HzRef id = crowd_cell_enemy(g, foe, row0, col0, skip, only, &mech);
    if (!ref_is_none(id)) {
        return id;
    }
    for (int r = 1; r <= rings; r++) {
        int len = 2 * r;
        for (int k = 0; k < 4 * len; k++) {
            int side = k / len;
            int i = k % len + 1;
            int row = row0 - r;
            int col = col0 - r;
            switch (side) {
            case 0:                             /* top, left to right */
                col += i;
                break;
            case 1:                             /* right, downwards */
                row += i;
                col += len;
                break;
            case 2:                             /* bottom, right to left */
                row += len;
                col += len - i;
                break;
            default:                            /* left, upwards */
                row += len - i;
                break;
            }
            id = crowd_cell_enemy(g, foe, row, col, skip, only, &mech);
            if (!ref_is_none(id)) {
                return id;
            }
        }
    }
    return mech;
}

/* UnitAI_SetTarget, or no target with none. */
static void set_target(Ai *a, HzRef id)
{
    HzUnit *u = a->u;
    u->ai.target_id = id;
    if (ref_is_none(id)) {
        u->ai.target_dir = 0;
        return;
    }
    s16 x;
    s16 y;
    obj_pos(a->g, id, &x, &y);
    u->ai.target_dir = (u8)(get_direction(u->x, u->y, x, y) + 1);
}

/* UnitAI_CheckShotResult: what became of its last bullet. A hit: misses
 * back to 0, and a CPU's side halves the HP of the unit it hit. A miss:
 * one more miss; true at three in a row (time for another target). */
static bool check_shot_result(Ai *a)
{
    HzUnit *u = a->u;
    if (!u->ai.shot_out || (u->shot_flags & OWN_SHOT_OUT)) {
        return false;                       /* none out, or still flying */
    }
    if (!(u->shot_flags & OWN_SHOT_HIT)) {
        u->ai.shot_out = false;
        u->ai.misses++;
        return u->ai.misses >= 3;
    }
    u->shot_flags &= (u8)~OWN_SHOT_HIT;
    if (a->g->mech[a->team].flags & MECH_CPU) {
        /* The own side is a CPU: the victim, if a unit, loses half its
         * HP, unless it's dying or on screen (obj_flags b6). QUIRK: the
         * record after the victim's (victim_next_record) -- a unit carried
         * among them too, as it is never drawn, so never on screen. FIX
         * (HZ_FIX_CPU_HITS): the victim's own record, and a mech hit
         * halves no unit. (Session 62: until then the fix took P1's mech,
         * the 68k's $64, for team 2's slot 49, whose code it shares.) */
        HzRef v = u->hit_victim;
        HzUnit *o = NULL;
        if (a->g->fixes & HZ_FIX_CPU_HITS) {
            o = ref_unit_ptr(a->g, v);      /* NULL but for a unit */
        } else {
            o = victim_next_record(a->g, v);
        }
        if (o != NULL && (o->flags & UNIT_ACTIVE)
            && !(o->flags & (UNIT_ON_SCREEN | UNIT_DYING))) {
            o->hp = (u16)(o->hp >> 1);
        }
    }
    u->ai.shot_out = false;
    u->ai.misses = 0;
    return false;
}

/* UnitAI_FindTarget: with no target, the nearest enemy in sight
 * (stat_sight rings). With one: three misses in a row -> another one in
 * sight; else it keeps the target while it still sees it, and loses it
 * otherwise (its misses kept). */
static void find_target(Ai *a)
{
    HzUnitAI *ai = &a->u->ai;
    int sight = a->st->sight;
    if (ref_is_none(ai->target_id)) {
        set_target(a, sight ? pick_enemy(a, sight, ref_none(), ref_none())
                            : ref_none());
        ai->misses = 0;
        return;
    }
    if (check_shot_result(a)) {
        set_target(a, sight ? pick_enemy(a, sight, ai->target_id, ref_none())
                            : ref_none());
        ai->misses = 0;
        return;
    }
    set_target(a, sight ? pick_enemy(a, sight, ref_none(), ai->target_id)
                        : ref_none());
}

/* UnitAI_RescanTarget (GRM): the nearest enemy in sight every time. */
static void rescan_target(Ai *a)
{
    int sight = a->st->sight;
    check_shot_result(a);                   /* its answer isn't used */
    set_target(a, sight ? pick_enemy(a, sight, ref_none(), ref_none())
                        : ref_none());
    a->u->ai.misses = 0;
}

/* UnitAI_FindTargetHQ: attack HQ looks only while approaching. */
static void find_target_hq(Ai *a)
{
    if (a->u->ai.attack_state == 0) {
        find_target(a);
    }
}

/* UnitAI_ScanFar: the nearest enemy within twice its sight -> far_dir,
 * far_id (far_id is kept when none is found). */
static void scan_far(Ai *a)
{
    HzUnitAI *ai = &a->u->ai;
    int sight = a->st->sight;
    HzRef id = sight ? pick_enemy(a, sight * 2, ref_none(), ref_none())
                     : ref_none();
    if (ref_is_none(id)) {
        ai->far_dir = 0;
        return;
    }
    s16 x;
    s16 y;
    obj_pos(a->g, id, &x, &y);
    ai->far_id = id;
    ai->far_dir = (u8)(get_direction(a->u->x, a->u->y, x, y) + 1);
}

/* UnitAI_AimAtMech (SAM, GRM): far_dir = direction + 1 to the enemy mech
 * when it's alive and under $100 px away, else 0. CHANGED: also far_id =
 * that mech. The original leaves its far pointer (+$4C) unset, so a SAM on
 * guard near (the CPU's start units on difficulty B) reads the "far
 * enemy's" x, y from ROM $10; here it's the mech it aims at. */
static void aim_at_mech(Ai *a)
{
    HzUnit *u = a->u;
    const HzMech *m = &a->g->mech[a->team ? 0 : 1];
    u->ai.far_id = ref_mech(a->team ? 0 : 1);
    if (m->flags & MECH_DESTROYED) {
        u->ai.far_dir = 0;
        return;
    }
    u16 dist;
    u8 d = dir_and_dist(u->x, u->y, m->x, m->y, &dist);
    u->ai.far_dir = (dist < 0x100) ? (u8)(d + 1) : 0;
}

/* Is outpost i one to go to: nobody's or the enemy's, or its side's with
 * an enemy hold on it (being taken)? */
static bool outpost_wanted(const Ai *a, int i)
{
    const HzPost *p = &a->g->posts[i];
    if (!(p->flags & POST_USED)) {
        return false;
    }
    /* 0 nobody, 2 P1, 3 P2 */
    u8 owner = p->flags & (POST_OWNED | POST_TEAM2);
    if (a->team == 0) {
        return owner == 0 || owner == 3 || p->hold[1] != 0;
    }
    return owner == 0 || owner == 2 || p->hold[0] != 0;
}

/* UnitAI_GoalOutpost's choice: the nearest outpost (|dx| + |dy|, ties to
 * the later) that is nobody's or the enemy's, or its side's with an enemy
 * hold on it (being taken); -1 none. */
static int goal_outpost_index(const Ai *a)
{
    const HzUnit *u = a->u;
    int best = -1;
    u16 best_d = 0xFFFF;
    for (int i = 0; (a->g->fixes & HZ_FIX_PATHS) && i < HZ_POST_OUTPOSTS;
         i++) {
        /* (session 71) PATHS: the nearest by the ground of those it can
         * get to, if any */
        if (outpost_wanted(a, i)) {
            u16 d = paths_cost(a->g, u, i);
            if (d != PATHS_NO_WAY && d <= best_d) {
                best = i;
                best_d = d;
            }
        }
    }
    if (best >= 0) {
        return best;
    }
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        const HzPost *p = &a->g->posts[i];
        if (!(p->flags & POST_USED)) {
            continue;
        }
        if (!outpost_wanted(a, i)) {
            continue;
        }
        u16 d = (u16)(dist16(u->x, p->x) + dist16(u->y, p->y));
        if (d <= best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/* UnitAI_GoalOutpost: goal_outpost_index's -> dest_dir, dest_dist. None:
 * dest_dir 0. */
static void goal_outpost(Ai *a)
{
    HzUnit *u = a->u;
    int best = goal_outpost_index(a);
    if (best < 0) {
        u->ai.dest_dir = 0;
        return;
    }
    const HzPost *p = &a->g->posts[best];
    u8 d = dir_and_dist(u->x, u->y, p->x, p->y, &u->ai.dest_dist);
    u->ai.dest_dir = (u8)(d + 1);
}

static long isqrt(long n);

/* (Session 71) PATHS: the heading towards post `post` down the ground's
 * shortest way (paths_next), the place it drives at kept for path_steer;
 * else (the patch off, no way from here, or there) false and *dir as it
 * was. */
static bool path_heading(Ai *a, int post, u8 *dir)
{
    HzUnit *u = a->u;
    s16 wx;
    s16 wy;
    if (!(a->g->fixes & HZ_FIX_PATHS) || post < 0
        || !paths_next(a->g, u, post, &wx, &wy)) {
        return false;
    }
    u->ai.route_x = wx;
    u->ai.route_y = wy;
    u->ai.path_going = true;
    *dir = get_direction(u->x, u->y, wx, wy);
    return true;
}

/* (Session 71) After Unit_SetSprite, with PATHS: a unit on its way
 * (path_heading) goes straight at its place, not along the nearest of the
 * 16 headings, as supply_steer; facing more than 45 degrees off it, it
 * turns where it stands, but not on a damaging floor (VULKAN's: it burns
 * standing there). On a current it steers into it to hold its line (what
 * the current adds across the line taken off), turning or not. */
static void path_steer(Ai *a)
{
    HzUnit *u = a->u;
    const HzUnitAI *ai = &u->ai;
    if (!moving(u) || (u->flags & UNIT_REVERSING)) {
        return;
    }
    long dx = ai->route_x - u->x;
    long dy = ai->route_y - u->y;
    long len = isqrt(dx * dx + dy * dy);
    if (len == 0) {
        return;
    }
    long speed = (s16)u->speed * 32;
    long px;
    long py;
    paths_push(u, &px, &py);
    if (px != 0 || py != 0) {
        /* across = push - (push . line) line; what it keeps along the
         * line, sqrt(speed^2 - across^2), less the across */
        long along = (px * dx + py * dy) / len;
        long ax = px - along * dx / len;
        long ay = py - along * dy / len;
        long across = isqrt(ax * ax + ay * ay);
        if (across >= speed) {
            /* (more than it has: all of it against the across) */
            u->target_vel_x = (s16)(-ax * speed / across);
            u->target_vel_y = (s16)(-ay * speed / across);
            return;
        }
        long keep = isqrt(speed * speed - across * across);
        u->target_vel_x = (s16)(keep * dx / len - ax);
        u->target_vel_y = (s16)(keep * dy / len - ay);
        return;
    }
    int turn = (get_direction(u->x, u->y, ai->route_x, ai->route_y)
                - u->dir) & 0x0F;
    if (turn > 8) {
        turn = 16 - turn;
    }
    if (turn > 2 && (u->terrain & 0x0F) != 0x8) {
        u->target_vel_x = 0;
        u->target_vel_y = 0;
        return;
    }
    u->target_vel_x = (s16)(speed * dx / len);
    u->target_vel_y = (s16)(speed * dy / len);
}

/* UnitAI_DirToEnemyHQ: dest_dir, dest_dist towards the enemy HQ. */
static void dir_to_enemy_hq(Ai *a)
{
    HzUnit *u = a->u;
    const HzPost *hq = enemy_hq(a);
    u8 d = dir_and_dist(u->x, u->y, hq->x, hq->y, &u->ai.dest_dist);
    u->ai.dest_dir = (u8)(d + 1);
}

/* Map_GetPathHint: the hint (0-$E, 8 directions x 2) towards the enemy HQ
 * on the map block under its centre, -1 for none. Layout bit 9 marks a
 * hint; bits 13-15 are team 1's direction, 10-12 team 2's. */
static int path_hint(const HzGame *g, const HzUnit *u)
{
    u16 half = (u16)(u->size >> 1);
    int row = (((u16)(u->y + half)) & 0x7E0) >> 5;
    int col = (((u16)(u->x + half)) & 0x7E0) >> 5;
    u16 w = g->layout[row][col];
    if (!(w & LAYOUT_PATH_HINT)) {
        return -1;
    }
    int d = (u->flags & UNIT_TEAM2) ? (w >> 10) & 7 : (w >> 13) & 7;
    return d * 2;
}

/* -- shooting ------------------------------------------------------------ */

/* Which way its gun points: an INFANTRY's aim, a turret's direction, else
 * the heading. */
static u8 gun_dir(const HzUnit *u)
{
    if (u->type == UNIT_INFANTRY) {
        return u->aim_dir;
    }
    if (has_turret(u)) {
        return u->turret_dir;
    }
    return u->dir;
}

static u8 shot_flags_for(const HzUnit *u)
{
    /* UNIT_TEAM2 and SHOT_TEAM2 are the same bit */
    return (u8)((u->flags & UNIT_TEAM2) | SHOT_ACTIVE | SHOT_FROM_UNIT);
}

/* UnitAI_FireBullet: a bullet from its centre - 4. True when fired. */
static bool fire_bullet(Ai *a, u8 dir)
{
    const HzUnit *u = a->u;
    s16 off = (s16)((u->size >> 1) - 4);
    return shot_fire_bullet(a->g, unit_ref(a->u), shot_flags_for(u), dir,
                            (s16)(u->x + off), (s16)(u->y + off),
                            a->st->shot_speed, a->st->shot_life) == 0;
}

/* UnitAI_FireBulletOffset (GRM): two bullets, from its guns
 * (UNIT_TWIN_GUN_OFFSETS); the second only if the first went, and the
 * second's own result isn't looked at. */
static bool fire_twin(Ai *a, u8 dir)
{
    const HzUnit *u = a->u;
    const s16 *o = UNIT_TWIN_GUN_OFFSETS[dir & 7];
    s16 x = (s16)(u->x + o[0]);
    s16 y = (s16)(u->y + o[1]);
    u8 flags = shot_flags_for(u);
    if (shot_fire_bullet(a->g, unit_ref(a->u), flags, dir, x, y,
                         a->st->shot_speed, a->st->shot_life) != 0) {
        return false;
    }
    shot_fire_bullet(a->g, unit_ref(a->u), flags, dir, (s16)(x + o[2]),
                     (s16)(y + o[3]), a->st->shot_speed, a->st->shot_life);
    return true;
}

/* UnitAI_FireMissile: a missile from (x, y) + 8. */
static bool fire_missile(Ai *a, u8 dir)
{
    const HzUnit *u = a->u;
    return shot_fire_missile(a->g, unit_ref(a->u), shot_flags_for(u), dir,
                             (s16)(u->x + 8), (s16)(u->y + 8),
                             a->st->missile_speed,
                             a->st->missile_life) == 0;
}

/* UnitAI_FireAtTarget / _FireTwinAtTarget: with ammo, no bullet out, a
 * target and the fire delay run out: target_dir = its direction + 1, and
 * if the gun points within one step of it, fire. QUIRK: the step is
 * compared as a plain difference, not round the 16 directions, so a gun
 * at $F never fires at a target at 0 (or the other way): a unit facing a
 * step either side of straight up holds its fire at an enemy on the other
 * side. FIX (HZ_FIX_UNIT_AIM): the difference taken round the 16. */
static void fire_at_target(Ai *a, bool twin)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (u->ammo == 0 || ai->shot_out || ref_is_none(ai->target_id)
        || u->fire_delay != 0) {
        return;
    }
    s16 x;
    s16 y;
    obj_pos(a->g, ai->target_id, &x, &y);
    u8 to = (u8)(get_direction(u->x, u->y, x, y) + 1);
    ai->target_dir = to;
    u8 gun = gun_dir(u);
    s8 diff = (s8)(u8)(gun + 1 - to);
    u8 off = (u8)((diff < 0) ? -diff : diff);
    if (a->g->fixes & HZ_FIX_UNIT_AIM) {
        u8 d = (u8)(diff & 0x0F);       /* FIX: round the 16 directions */
        off = (d > 8) ? (u8)(16 - d) : d;
    }
    if (off > 1) {
        return;
    }
    bool fired = twin ? fire_twin(a, gun) : fire_bullet(a, gun);
    if (fired) {
        u->fire_delay = a->st->fire_delay;
        ai->shot_out = true;
    }
}

/* UnitAI_FireAtHQ: attack HQ in range (attack_state not 0) fires straight
 * ahead whenever it can; otherwise as fire_at_target. */
static void fire_at_hq(Ai *a)
{
    HzUnit *u = a->u;
    if (u->ai.attack_state == 0) {
        fire_at_target(a, false);
        return;
    }
    if (u->ammo == 0 || u->ai.shot_out || u->fire_delay != 0) {
        return;
    }
    if (fire_bullet(a, gun_dir(u))) {
        u->fire_delay = a->st->fire_delay;
        u->ai.shot_out = true;
    }
}

/* UnitAI_FireMissileAtMech (SAM, GRM): a missile towards far_dir when the
 * missile delay (unit_turret_dir, which these types use as one) is 0. */
static void fire_missile_at_mech(Ai *a)
{
    HzUnit *u = a->u;
    if (u->missiles == 0 || u->ai.far_dir == 0 || u->turret_dir != 0) {
        return;
    }
    if (fire_missile(a, (u8)(u->ai.far_dir - 1))) {
        u->turret_dir = a->st->missile_delay;
    }
}

/* -- the orders ---------------------------------------------------------- */

/* UnitAI_TurnFromTarget: out of ammo, with the target within 4 steps of
 * ahead, turn away from it. QUIRK: the new heading is the target's
 * bearing relative to the heading, - 8 -- a relative bearing used as a
 * heading -- not the target's direction + 8. */
static void turn_from_target(HzUnit *u)
{
    if (u->ai.target_dir == 0) {
        return;
    }
    u8 rel = (u8)((u->dir - (u->ai.target_dir - 1)) & 0x0F);
    if (rel > 4 && rel < 0x0C) {
        return;
    }
    u8 h = (u8)((rel - 8) & 0x0F);
    u->turn_to = h;
    u->aim_dir = h;
}

/* With a target in its direction and ammo, a turret unit steers 3 steps
 * off the target, on the side it's already on, and while moving swings its
 * turret a step: roam away from that side, ambush towards it. (Roam's
 * original works the swing out from where the target falls from the new
 * heading, by quarters; that is only ever 3 or $D steps, so it comes to
 * this.) */
static void turret_engage(HzUnit *u, u8 t, bool roam)
{
    bool left = ((t - u->dir) & 0x0F) < 8;
    u->turn_to = dir_step(t, left ? -3 : 3);
    if (!moving(u)) {
        return;
    }
    int swing = left ? -1 : 1;
    u->aim_dir = dir_step(u->aim_dir, roam ? -swing : swing);
}

/* Roam's and patrol's "resume" (UnitAI_RoamResume, _PatrolResume): aim
 * ahead with no target, forget the saved heading and the turn aside. */
static void resume(HzUnit *u)
{
    if (ref_is_none(u->ai.target_id)) {
        u->aim_dir = u->turn_to;
    }
    u->ai.saved_dir = 0;
    u->ai.state = 0;
}

/* 0 AF-001, roam (UnitAI_OrderRoam): drive on; engage what it sees; a
 * blocked unit turns aside (state: the step, -1 or +1, picked once). */
static void order_roam(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (ref_is_none(ai->target_id)) {
        ai->hold = 0;
    } else if (!armed(u)) {
        turn_from_target(u);
        ai->hold = 0;
    } else if (ai->target_dir != 0) {
        u8 t = (u8)(ai->target_dir - 1);
        u->aim_dir = t;
        save_heading(u);
        if (!has_turret(u)) {
            u->turn_to = t;
            stop(u);
            ai->hold = 0xFF;
        } else {
            turret_engage(u, t, true);
        }
    }

    if (moving(u)) {                                    /* .moving */
        if (!armed(u) && ref_is_none(ai->target_id)) {
            stop(u);
        }
        resume(u);
        return;
    }
    if (ai->hold & 0x80) {
        return;                                         /* stopped to aim */
    }
    if (blocked(u)) {                                   /* .dodge */
        s8 step = (game_random(a->g) & 0x200) ? -1 : 1;
        if (ai->state == 0) {
            ai->state = (u8)step;
        }
        u8 h = dir_step(u->dir, (s8)ai->state);
        u->turn_to = h;
        if (ref_is_none(ai->target_id)) {
            u->aim_dir = h;
        }
        go(u);
        return;
    }
    if (ai->saved_dir != 0) {
        u->turn_to = (u8)(ai->saved_dir - 1);
        resume(u);
    } else if (!armed(u) && ai->misses != 0) {
        resume(u);
    }
    go(u);
}

/* 1 BDF-1SD, stand (UnitAI_OrderStand; the GRM runs it whatever its
 * order): stopped, aim at the target; the heading saved the first time,
 * turned back to once the target is gone. QUIRK: a turret unit saves its
 * turret's direction + 1 (it reads unit_turret's word), not its heading;
 * it only ever turns the turret back with it, so no harm. */
static void order_stand(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    stop(u);
    if (!ref_is_none(ai->target_id)) {
        if (ai->target_dir == 0) {
            return;
        }
        u8 t = (u8)(ai->target_dir - 1);
        u->aim_dir = t;
        u8 kept = u->turret_dir;
        if (!has_turret(u)) {
            u->turn_to = t;
            kept = u->dir;
        }
        if (ai->saved_dir == 0) {
            ai->saved_dir = (u8)(kept + 1);
        }
        return;
    }
    if (ai->saved_dir == 0) {
        return;
    }
    u8 h = (u8)(ai->saved_dir - 1);
    if (has_turret(u)) {
        u->aim_dir = h;
    } else {
        u->turn_to = h;
    }
    ai->saved_dir = 0;
}

/* 2 AF-001A, patrol (UnitAI_OrderPatrol): wander within $46 px of the
 * start point: past it, head back; nearer, a random heading every 16th
 * update. Engages as roam does (without the turret's offset). */
static void order_patrol(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (!ai->home_set) {
        set_home(u);
    }
    if (ref_is_none(ai->target_id)) {
        ai->hold = 0;
    } else if (!armed(u)) {
        turn_from_target(u);
        ai->hold = 0;
    } else if (ai->target_dir != 0) {
        u8 t = (u8)(ai->target_dir - 1);
        u->aim_dir = t;
        save_heading(u);
        if (!has_turret(u)) {
            u->turn_to = t;
        }
        stop(u);
        ai->hold = 0xFF;
    }

    if (moving(u)) {                                    /* .moving */
        u16 dist;
        u8 d = dir_home(u, &dist);
        if (dist >= 0x46) {
            u->turn_to = d;
        } else if ((u->phase & 0x0F) == 0) {
            u->turn_to = (u8)(game_random(a->g) & 0x0F);
        }
        resume(u);
        return;
    }
    if (ai->hold & 0x80) {
        return;
    }
    if (blocked(u)) {                                   /* .dodge */
        if (ai->state == 0) {
            /* CHANGED: in the attract demo the original's step is a
             * register left over from the unit before (Unit_SetSprite's
             * d1) - 1; here it's -1 or +1 as outside the demo (and as in
             * the game the demos were recorded in). So from a demo's first
             * dodge on, its units move otherwise than on the cartridge;
             * the mechs still follow the recordings. */
            u16 r = (u16)game_random(a->g);
            ai->state = (r & 1) ? 1 : 0xFF;
        }
        u8 h = dir_step(u->dir, (s8)ai->state);
        u->turn_to = h;
        if (ref_is_none(ai->target_id)) {
            u->aim_dir = h;
        }
        go(u);
        return;
    }
    if (ai->saved_dir != 0) {
        u->turn_to = (u8)(ai->saved_dir - 1);
    }
    resume(u);
    go(u);
}

/* Outpost's resume (UnitAI_OutpostResume): as roam's, but the turn aside
 * is kept. */
static void outpost_resume(HzUnit *u)
{
    if (ref_is_none(u->ai.target_id)) {
        u->aim_dir = u->turn_to;
    }
    u->ai.saved_dir = 0;
}

/* Outpost's turn aside: state $40 a step left, $80 (or anything else not
 * 0 or 2) right; 0 and 2 pick one by Random. */
static void outpost_dodge(Ai *a)
{
    HzUnit *u = a->u;
    u8 s = u->ai.state;
    if (s == 0 || s == 2) {
        s = (u8)((game_random(a->g) & 0x40) + 0x40);
        u->ai.state = s;
    }
    u8 h = dir_step(u->dir, (s == 0x40) ? -1 : 1);
    u->turn_to = h;
    if (ref_is_none(u->ai.target_id)) {
        u->aim_dir = h;
    }
    go(u);
}

/* 3 AT-101, outpost (UnitAI_OrderOutpost): to the goal outpost
 * (goal_outpost); a vehicle stops $40-$5F px out, facing away (state $FF,
 * parked) until the goal is $A0 or more away; an INFANTRY drives in.
 * state: 0 driving; $FF parked; 1 waits to face turn_to, then drives
 * (UNKNOWN: nothing in this order sets 1); 2, $40, $80 turning aside. */
static void order_outpost(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    u->aim_dir = u->dir;
    if (ref_is_none(ai->target_id)) {
        if (!armed(u) && ai->far_dir != 0) {
            ai->hold = 0xFF;            /* unarmed, an enemy about: wait */
            stop(u);
        } else {
            ai->hold = 0;
        }
    } else if (!armed(u)) {
        turn_from_target(u);
        ai->hold = 0;
    } else if (ai->target_dir != 0) {
        u8 t = (u8)(ai->target_dir - 1);
        u->aim_dir = t;
        save_heading(u);
        if (!has_turret(u)) {
            u->turn_to = t;
            stop(u);
            ai->hold = 0xFF;
        } else if (moving(u)) {
            /* the turret a step towards the target (from the heading) */
            int step = (((t - u->dir) & 0x0F) < 8) ? 1 : -1;
            u->aim_dir = dir_step(u->aim_dir, step);
        }
    }

    if (moving(u)) {                                    /* .moving */
        ai->state = 0;
        u8 d = ai->dest_dir;
        if (d == 0) {
            outpost_resume(u);
            return;
        }
        if (u->type != UNIT_INFANTRY && ai->dest_dist >= 0x40
            && ai->dest_dist < 0x60) {
            stop(u);
            ai->state = 0xFF;
            u8 h = (u8)((d - 9) & 0x0F);  /* the goal's direction + 8 */
            u->turn_to = h;
            if (ref_is_none(ai->target_id)) {
                u->aim_dir = h;
            }
            return;
        }
        u8 h = (u8)(d - 1);
        path_heading(a, goal_outpost_index(a), &h);    /* (s. 71) PATHS */
        u->turn_to = h;
        outpost_resume(u);
        return;
    }
    if (ai->hold & 0x80) {
        return;
    }
    switch (ai->state) {
    case 0x00:
        if (blocked(u)) {
            outpost_dodge(a);
            return;
        }
        if (ai->saved_dir != 0) {
            u->turn_to = (u8)(ai->saved_dir - 1);
        }
        outpost_resume(u);
        go(u);
        return;
    case 0xFF:                          /* parked (UnitAI_OutpostParked) */
        /* Under $40 from the goal the original also clears unit_home's
         * high byte, which nothing reads under this order. */
        if (ai->dest_dist >= 0xA0) {
            ai->state = 0;
        }
        return;
    case 0x01:
        if (u->dir == u->turn_to) {
            ai->state = 0;
            go(u);
        }
        return;
    default:
        outpost_dodge(a);
        return;
    }
}

/* Guard's states (+$3D): 0 idle at home; low nibble 8 chasing, 9 going
 * home; b7 clear: waiting to face turn_to, then b7 set and full speed;
 * b4-b5: a countdown of updates after a turn aside. */
#define GUARD_MOVING 0x80
#define GUARD_COUNT  0x30
#define GUARD_CHASE  0x08
#define GUARD_HOME   0x09

/* Face h: if not facing it, turn to it, stop and wait for the turn. */
static void guard_face(HzUnit *u, u8 h)
{
    if (h == u->dir) {
        return;
    }
    u->turn_to = h;
    u->aim_dir = h;
    stop(u);
    u->ai.state &= (u8)~GUARD_MOVING;
}

/* Guard near: chase the far enemy only if it's within $80 px of the start
 * point. QUIRK: the heading taken is the direction from the start point to
 * the enemy (not from the unit), - 1 as if it were + 1-encoded, and not
 * masked: usually the unit sets off a step to the left of the enemy and
 * turns at its next update; an enemy straight north of the start point
 * gives $FF, which the heading never equals, so the unit turns to $F and
 * waits there for good (seen in a game: it still fires at what comes into
 * sight, but never moves again). FIX (HZ_FIX_GUARD_CHASE): its own bearing
 * to the enemy, far_dir - 1, as guard's chase takes. */
static void guard_near_check_far(Ai *a)
{
    HzUnit *u = a->u;
    s16 x;
    s16 y;
    obj_pos(a->g, u->ai.far_id, &x, &y);
    u16 dist;
    u8 d = dir_and_dist((s16)(u->ai.home_x & 0x7FFF), u->ai.home_y, x, y,
                        &dist);
    if (dist >= 0x80) {
        return;
    }
    u->ai.state = GUARD_CHASE;
    u8 h = (u8)(d - 1);
    if (a->g->fixes & HZ_FIX_GUARD_CHASE) {
        h = (u8)(u->ai.far_dir - 1);    /* FIX: from the unit itself */
    }
    guard_face(u, h);
}

/* 4 DF-F01, guard, and 5 DF-F02A, guard near (UnitAI_OrderGuard,
 * _OrderGuardNear): keep the start point; engage a target in sight; chase
 * an enemy seen far off (guard near: only one near the start point), give
 * up when it's gone, go home. */
static void order_guard(Ai *a, bool near)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (!ai->home_set) {
        set_home(u);
        stop(u);
        ai->saved_dir = u->dir;         /* the heading itself, not + 1 */
    }
    if (armed(u) && !ref_is_none(ai->target_id)) {  /* .engage */
        stop(u);
        aim_at_target(u);
        return;
    }
    u8 s = ai->state;
    if (s == 0) {                                       /* .idle */
        if (armed(u) && ai->far_dir != 0) {
            if (near) {
                guard_near_check_far(a);
            } else {
                ai->state = GUARD_CHASE;
                guard_face(u, (u8)(ai->far_dir - 1));
            }
        }
        return;
    }
    if (!(s & GUARD_MOVING)) {                          /* .turning */
        if (u->dir == u->turn_to) {
            go(u);
            ai->state |= GUARD_MOVING;
        }
        return;
    }
    if (!moving(u)) {                                   /* .bump_turn */
        u8 h = dir_step(u->dir, ((s & 0x0F) == GUARD_CHASE) ? -1 : 1);
        u->turn_to = h;
        u->aim_dir = h;
        go(u);
        ai->state |= GUARD_COUNT;
        return;
    }
    if (s & GUARD_COUNT) {
        ai->state = (u8)((s & 0x8F) | ((s & GUARD_COUNT) - 0x10));
        return;
    }
    switch (s & 0x0F) {
    case GUARD_CHASE:
        if (armed(u) && ai->far_dir != 0) {
            guard_face(u, (u8)(ai->far_dir - 1));
        } else {
            ai->state = GUARD_HOME;                     /* .give_up */
            stop(u);
            guard_face(u, dir_step(u->dir, 8));
        }
        return;
    case GUARD_HOME:
        if (near) {
            /* QUIRK: guard near chases again without looking at its
             * ammo */
            if (ai->far_dir != 0) {
                guard_near_check_far(a);
                return;
            }
        } else if (armed(u) && ai->far_dir != 0) {
            ai->state = GUARD_CHASE;
            guard_face(u, (u8)(ai->far_dir - 1));
            return;
        }
        if (at_home(u)) {
            stop(u);
            u->turn_to = ai->saved_dir;
            u->aim_dir = ai->saved_dir;
            ai->state = 0;
            return;
        }
        {
            u16 dist;
            guard_face(u, dir_home(u, &dist));
        }
        return;
    default: {
        /* UNKNOWN: only another order's state gets here, and a unit's
         * order never changes with its AI state kept (only Unit_Spawn,
         * Unit_CreateCarried and the drop set it, and they clear it), so
         * this isn't reached. The original slows down by a CPU register
         * left over from an earlier unit (CHANGED: here by the distance
         * home, which it evidently means): full speed, halved under $80
         * px and again under $40, at least 1. */
        u16 dist;
        dir_home(u, &dist);
        u8 sp = u->max_speed;
        if (dist < 0x80) {
            sp = (u8)(sp >> 1);
            if (sp == 0) {
                sp = 1;
            }
        }
        if (dist < 0x40) {
            sp = (u8)(sp >> 1);
            if (sp == 0) {
                sp = 1;
            }
        }
        u->speed = (u16)((u->speed & 0xFF00) | sp);
        return;
    }
    }
}

/* Ambush's turn aside, kept in saved_dir (here the step, not a heading):
 * Random $20 -> +1, else -1, picked once. */
static u8 ambush_step(Ai *a)
{
    HzUnitAI *ai = &a->u->ai;
    if (ai->saved_dir == 0) {
        ai->saved_dir = (game_random(a->g) & 0x20) ? 1 : 0xFF;
    }
    return ai->saved_dir;
}

/* 6 DF-F03S, ambush (UnitAI_OrderAmbush): wait, stopped, until an enemy
 * is seen far off; then hunt it (state $80) until it's gone. state b7:
 * hunting; b4-b5 a countdown after a turn aside. QUIRK: the countdown is
 * tested as state & $1E (not $30), so of the 3 updates it's set to it
 * counts one; and with low bits set it would go wrong (no state here has
 * them). */
static void order_ambush(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (!ai->ambush_started) {
        stop(u);
        ai->ambush_started = true;
    }
    if (armed(u) && !ref_is_none(ai->target_id)) {  /* .engage */
        if (ai->target_dir != 0) {
            u8 t = (u8)(ai->target_dir - 1);
            u->aim_dir = t;
            if (!has_turret(u)) {
                u->turn_to = t;
                stop(u);
                ai->hold = 0xFF;
            } else {
                turret_engage(u, t, false);
            }
        }
        if (moving(u)) {                                /* .moving */
            if (ref_is_none(ai->target_id)) {
                u->aim_dir = u->turn_to;
            }
            ai->saved_dir = 0;
            return;
        }
        if (ai->hold & 0x80) {
            return;
        }
        u8 h = dir_step(u->dir, (s8)ambush_step(a));
        u->turn_to = h;
        if (ref_is_none(ai->target_id)) {
            u->aim_dir = h;
        }
        go(u);
        return;
    }
    ai->hold = 0;
    u8 s = ai->state;
    if (s == 0) {                                       /* .idle */
        if (ai->far_dir == 0) {
            ai->saved_dir = 0;
            return;
        }
        u8 h = (u8)(ai->far_dir - 1);
        u->turn_to = h;
        u->aim_dir = h;
        ai->state = 0x80;
        go(u);
        return;
    }
    if (!(s & 0x80)) {
        return;             /* UNKNOWN: no state of this order gets here */
    }
    if (!moving(u)) {                                   /* .bump_turn */
        u8 h = dir_step(u->dir, (s8)ambush_step(a));
        u->turn_to = h;
        if (ref_is_none(ai->target_id)) {
            u->aim_dir = h;
        }
        ai->state |= 0x30;
        go(u);
        return;
    }
    if (ai->far_dir == 0) {
        stop(u);
        ai->state = 0;
        return;
    }
    u8 count = s & 0x1E;
    if (count != 0) {
        ai->state = (u8)((s & 0x8F) | (u8)(count - 0x10));
        return;
    }
    u8 h = (u8)(ai->far_dir - 1);                       /* .head_for_far */
    u->turn_to = h;
    u->aim_dir = h;
}

/* 7 T-BG1, advance (UnitAI_OrderAdvance): on in its first heading at full
 * speed; stop to aim at a target (out of ammo: turn from it). */
static void order_advance(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (ai->advance_dir == 0) {
        ai->advance_dir = (u8)(u->dir + 1);
    }
    if (!ref_is_none(ai->target_id)) {
        if (armed(u)) {
            if (ai->target_dir != 0) {
                aim_at_target(u);
                stop(u);
            }
            return;
        }
        turn_from_target(u);
    }
    ai->hold = 0;                                       /* .drive_on */
    if (ai->advance_dir != 0) {
        u8 h = (u8)(ai->advance_dir - 1);
        u->turn_to = h;
        u->aim_dir = h;
    }
    go(u);
}

/* Hold's states (+$3D): 0 at home; $88 out of ammo, backing off from the
 * enemy; $89 going home (checked each update); 9 waiting to face home; 1-4
 * driving home, counting down to $89. */
#define HOLD_BACK_OFF 0x88
#define HOLD_RETURN   0x89
#define HOLD_TURN     0x09
#define HOLD_DRIVE    0x04

/* A turn aside kept in `hold` (Random $400 -> -1, else +1, picked once).
 * QUIRK: not masked to 16 directions, so turn_to may be $10 or $FF; turning
 * works out the same, as Unit_SetSprite masks the difference. */
static void hold_dodge(Ai *a)
{
    HzUnit *u = a->u;
    if (u->ai.hold == 0) {
        u->ai.hold = (game_random(a->g) & 0x400) ? 0xFF : 1;
    }
    u8 h = (u8)(u->ai.hold + u->dir);
    u->turn_to = h;
    u->aim_dir = h;
}

/* 8 BDF-1ST, hold (UnitAI_OrderHold): keep the start point; out of
 * ammo, back off from the enemy, then go home. Nothing gives a unit this
 * order: no type's order list has it, nor the start units, the demos' or
 * the CPU's buy scripts. */
static void order_hold(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (!ai->home_set) {
        set_home(u);
        ai->saved_dir = u->dir;
        stop(u);
        /* UNKNOWN: the original tests the states below as words whose
         * high byte, on this first run only, is y's; only a state left by
         * another order could tell, and none is (see guard). */
    }
    if (!ref_is_none(ai->target_id)) {      /* .target */
        if (ai->state == HOLD_BACK_OFF) {
            if (!armed(u)) {                            /* .backing_off */
                if (moving(u)) {
                    return;
                }
                if (blocked(u)) {
                    hold_dodge(a);
                } else {
                    turn_from_target(u);
                }
                go(u);
                return;
            }
            ai->state = HOLD_RETURN;
        }
        aim_at_target(u);                               /* .aim */
        if (!armed(u)) {
            ai->state = HOLD_BACK_OFF;
        }
        return;
    }
    u8 s = ai->state;
    if (s == 0) {
        if (!armed(u)) {
            ai->state = HOLD_BACK_OFF;
            return;
        }
        u->turn_to = ai->saved_dir;
        u->aim_dir = ai->saved_dir;
        return;
    }
    if (s == HOLD_BACK_OFF) {                           /* .retreat */
        if (armed(u) || ai->far_dir == 0) {
            ai->state = HOLD_RETURN;
            return;
        }
        if (moving(u)) {
            stop(u);
            ai->hold = 0;
            u8 h = dir_step(u->dir, 8);
            u->turn_to = h;
            u->aim_dir = h;
        }
        return;
    }
    if (s == HOLD_TURN) {
        if (u->dir == u->turn_to) {
            ai->state = HOLD_RETURN;
        }
        return;
    }
    if (s < 5) {                                        /* .countdown */
        ai->state--;
        if (ai->state == 0) {
            ai->state = HOLD_RETURN;
        }
        return;
    }
    if (at_home(u)) {                                   /* .check_home */
        stop(u);
        u->turn_to = ai->saved_dir;
        u->aim_dir = ai->saved_dir;
        ai->state = 0;
        return;
    }
    u16 dist;
    u8 h = dir_home(u, &dist);
    if (h != u->dir) {
        u->turn_to = h;
        u->aim_dir = h;
        stop(u);
        ai->state = HOLD_TURN;
        return;
    }
    if (!moving(u) && blocked(u)) {
        hold_dodge(a);
    }
    ai->state = HOLD_DRIVE;
    go(u);
}

/* 9 AT-101H, capture (UnitAI_OrderCapture; INFANTRY): straight for the
 * goal outpost. Blocked: a step aside (hold, picked once by the heading's
 * quarter) and 8 updates (state) before steering again. */
static void order_capture(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    u->aim_dir = u->dir;
    if (!ref_is_none(ai->target_id) && ai->target_dir != 0) {
        u->aim_dir = (u8)(ai->target_dir - 1);
    }
    if (!moving(u)) {
        if (blocked(u)) {
            if (ai->hold == 0) {
                ai->hold = (u8)quarter_step(u->dir);
            }
            u->turn_to = dir_step(u->dir, (s8)ai->hold);
            ai->state = 8;
        }
        go(u);
        return;
    }
    ai->hold = 0;
    if (ai->state != 0) {
        ai->state--;
        return;
    }
    u8 d = 0;               /* no goal: north, for the aim below */
    if (ai->dest_dir != 0) {
        d = (u8)(ai->dest_dir - 1);
        path_heading(a, goal_outpost_index(a), &d);    /* (s. 71) PATHS */
        u->turn_to = d;
    }
    if (has_turret(u)) {
        u->aim_dir = d;
    }
}

/* Attack HQ's driving, after its state's part (.move in the original). */
static void attack_hq_drive(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (!moving(u)) {
        if (blocked(u)) {
            if (ai->hold == 0) {
                s8 step;
                if (ai->path_hint != 0) {
                    /* QUIRK: half the heading (8 directions) against the
                     * hint - 1 (16-way, even): a mix of the two scales */
                    u8 v = (u8)(((u->dir >> 1) - (ai->path_hint - 1)) & 7);
                    step = (v < 4) ? 1 : -1;
                } else {
                    step = quarter_step(u->dir);
                }
                ai->hold = (u8)step;
            }
            u->turn_to = dir_step(u->dir, (s8)ai->hold);
            ai->state = 8;
        }
        go(u);
        return;
    }
    ai->hold = 0;
    if (ai->attack_state != 0) {                        /* wandering */
        if (ai->dest_dir != 0) {
            u->aim_dir = (u8)(ai->dest_dir - 1);
        }
        return;
    }
    if (ai->state != 0) {
        ai->state--;
        return;
    }
    u8 d = 0;
    if (path_heading(a, HZ_POST_HQ + (a->team ? 0 : 1), &d)) {
        u->turn_to = d;                 /* (session 71) PATHS */
    } else if (ai->path_hint != 0) {
        d = (u8)(ai->path_hint - 1);
        u->turn_to = d;
    } else if (ai->dest_dir != 0) {
        d = (u8)(ai->dest_dir - 1);
        u->turn_to = d;
    }
    if (has_turret(u)) {
        u->aim_dir = d;
    }
}

/* Attack HQ in range or out of ammo (attack_state b7). */
static void attack_hq_shooting(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    bool rearmed = ai->attack_state == 0x80;    /* out of ammo before */
    if (ai->shot_out && !(u->shot_flags & OWN_SHOT_OUT)) {
        ai->shot_out = false;
        if (!(u->shot_flags & OWN_SHOT_HIT)) {
            ai->attack_state = 0x0C;    /* a miss: wander 12 updates */
            return;
        }
        u->shot_flags &= (u8)~OWN_SHOT_HIT;
        if (u->hit_victim.kind != REF_HQ) {
            ai->attack_state = 0x0C;    /* it hit something else */
            return;
        }
        rearmed = false;                /* (the 68k's d0: the HQ's $66) */
    }
    if (u->ammo != 0) {
        if (rearmed) {                  /* re-armed: approach again */
            ai->attack_state = 0;
            ai->shot_out = false;
        }
        return;
    }
    ai->attack_state = 0x80;            /* out of ammo */
    if (!(u->shot_flags & HIT_BY_SHOT)) {
        u->flags &= (u8)~UNIT_REVERSING; /* not hit: sit */
        stop(u);
        return;
    }
    u->shot_flags &= (u8)~HIT_BY_SHOT;  /* hit: back off */
    if (moving(u)) {
        return;
    }
    if (!blocked(u)) {
        u->flags |= UNIT_REVERSING;     /* reverse */
    } else {
        u->flags &= (u8)~UNIT_REVERSING;
        u->turn_to = dir_step(u->dir, 1);
    }
    go(u);
}

/* A BA-001C, attack HQ (UnitAI_OrderAttackHQ): follow the map's path
 * hints to the enemy HQ; at shot range ((shot speed / 8) x shot life + $38)
 * stop and shoot straight ahead (fire_at_hq). Any shot that doesn't hit
 * the HQ: 12 updates of wandering (a random heading on 8 of them), then
 * approach again. Out of ammo: sit, back off when hit. attack_state: see
 * HzUnitAI. */
static void order_attack_hq(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    u8 s = ai->attack_state;
    if (s & 0x80) {
        attack_hq_shooting(a);
        return;
    }
    if (s != 0) {                                       /* wandering */
        ai->attack_state = --s;
        if (s & 0x0C) {
            u->turn_to = (u8)(game_random(a->g) & 0x0F);
        }
        attack_hq_drive(a);
        return;
    }
    if (!ref_is_none(ai->target_id) && ai->target_dir != 0) {
        /* .approach */
        u->aim_dir = (u8)(ai->target_dir - 1);
    }
    if (ai->dest_dir != 0) {
        u16 range = (u16)((a->st->shot_speed >> 3) * a->st->shot_life
                          + 0x38);
        if (ai->dest_dist < range) {
            ai->attack_state = 0xFF;
            u8 h = (u8)(ai->dest_dir - 1);
            u->aim_dir = h;
            u->turn_to = h;
            stop(u);
            return;
        }
    }
    attack_hq_drive(a);
}

/* Does `c` need a SUPPLY: below a quarter of `max`'s fuel, missiles or
 * ammo (a maximum of 0 doesn't count)? */
static bool needs_supply(const HzGame *g, const HzUnitStats *max,
                         const HzUnit *c)
{
    if (max->fuel != 0 && (u16)(max->fuel >> 2) > c->fuel) {
        return true;
    }
    if (max->missiles != 0 && (u8)(max->missiles >> 2) > c->missiles) {
        return true;
    }
    if (c->type == UNIT_REPAIR && repair_on(g)) {
        return false;       /* (session 72) REPAIR: a repair truck's ammo
                               is its crew, and no SUPPLY brings men */
    }
    return max->ammo != 0 && (u8)(max->ammo >> 2) > c->ammo;
}

/* The SUPPLY lets its customer go: the customer no longer held (b2), its
 * speed and maximum speed back. */
static void supply_release(Ai *a, HzUnit *c)
{
    HzUnitAI *ai = &a->u->ai;
    c->flags &= (u8)~UNIT_HELD;
    c->speed = (u16)((c->speed & 0xFF00) | ai->customer_speed);
    ai->docked = false;
    c->max_speed = stats_of(a->g, c)->speed;
}

/* Turn away from where the last shot came from (+ 7 or + 9). */
static void supply_evade(Ai *a)
{
    HzUnit *u = a->u;
    u8 from = (u8)((u->shot_flags + 8) & 0x0F);
    int turn = (game_random(a->g) & 0x1000) ? 9 : 7;
    u->turn_to = (u8)((from + turn) & 0x0F);
    u->ai.state = 9;
}

static void supply_detour(Ai *a)
{
    HzUnit *u = a->u;
    u->ai.detour_step = (game_random(a->g) & 0x100) ? -1 : 1;
    u->ai.detour_left = 4;
    go(u);
}

static void supply_dock(Ai *a, HzUnit *c)
{
    HzUnit *u = a->u;
    u->ai.docked = true;
    u->ai.customer_speed = (u8)c->speed;
    c->speed = 0;
    c->flags |= UNIT_HELD;
    /* QUIRK: the original clears the customer's +$3A word, meaning a
     * SUPPLY customer's detour; for a customer on patrol, guard, guard
     * near or hold that word is its home's y, so its home moves to the
     * top of the map. */
    c->ai.detour_left = 0;
    c->ai.detour_step = 0;
    c->ai.home_y = 0;
    stop(u);
}

/* One update's filling: + 100 fuel, + 20 missiles, + 20 ammo, each up to
 * its type's maximum; true once all three are full. */
static bool supply_fill(Ai *a, HzUnit *c)
{
    const HzUnitStats *max = stats_of(a->g, c);
    int full = 0;
    u32 fuel = (u32)c->fuel + 100;
    if (fuel > 0xFFFF || fuel >= max->fuel) {
        fuel = max->fuel;
        full++;
    }
    c->fuel = (u16)fuel;
    u16 missiles = (u16)(c->missiles + 20);
    if (missiles > 0xFF || missiles >= max->missiles) {
        missiles = max->missiles;
        full++;
    }
    c->missiles = (u8)missiles;
    if (c->type == UNIT_REPAIR && repair_on(a->g)) {
        return full == 2;   /* (session 72) REPAIR: its crew left as it is */
    }
    u16 ammo = (u16)(c->ammo + 20);
    if (ammo > 0xFF || ammo >= max->ammo) {
        ammo = max->ammo;
        full++;
    }
    c->ammo = (u8)ammo;
    return full == 3;
}

/* Hit while serving: let the customer go and evade. */
static void supply_hit(Ai *a, HzUnit *c)
{
    HzUnit *u = a->u;
    u->shot_flags &= (u8)~HIT_BY_SHOT;
    supply_release(a, c);
    u->ai.hold = 0;
    u->ai.customer_id = ref_none();
    supply_evade(a);
}

/* Serving (docked): supply_fill an update; when all three are full, or
 * the customer is carried off or dying, let it go and turn about. Hit:
 * let it go and evade. */
static void supply_serve(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    HzUnit *c = ref_unit_ptr(a->g, ai->customer_id);
    if (u->shot_flags & HIT_BY_SHOT) {
        supply_hit(a, c);
        return;
    }
    if (!(c->flags & (UNIT_DYING | UNIT_CARRIED)) && !supply_fill(a, c)) {
        return;
    }
    supply_release(a, c);                               /* .release_turn */
    u->turn_to = dir_step(u->dir, 8);
    ai->state = 9;
    ai->hold = 0;
    ai->customer_id = ref_none();
}

/* Driving to the customer. The first time (hold 0) it gives up on one $C8
 * px or more away. Stopped against a unit of its side: dock if that unit
 * needs serving, else a detour. QUIRK: whether a bumped unit needs serving
 * is judged by its type's maxima against the customer's current values,
 * and a SUPPLY or ST-57U bumped never counts. CHANGED: the original never
 * recognises the customer itself when it bumps into it (its pointer is
 * kept in two different forms, $FFFFxxxx and $00FFxxxx), and once a
 * SUPPLY is carried off mid-service it forgets its customer for the same
 * reason; here bumping the customer docks, and the customer is kept. */
static void supply_approach(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    HzUnit *c = ref_unit_ptr(a->g, ai->customer_id);
    u16 dist;
    u8 d = dir_and_dist(u->x, u->y, c->x, c->y, &dist);
    if (ai->hold == 0) {
        if (dist >= 0xC8) {
            ai->customer_id = ref_none();
            ai->hold = 0;
            return;
        }
        u->turn_to = d;                                 /* .set_off */
        u->aim_dir = d;
        go(u);
        ai->hold = 0xFF;
        return;
    }
    u->turn_to = d;
    u->aim_dir = d;
    if (moving(u)) {
        return;
    }
    if (!(u->blocked & PROBE_OBJECT) || (u->blocked & PROBE_FOE)
        || u->bumped.kind != REF_UNIT) {
        supply_detour(a);               /* a mech, or no unit */
        return;
    }
    HzUnit *b = ref_unit_ptr(a->g, u->bumped);
    if (b == c) {
        supply_dock(a, c);
        return;
    }
    if (b->type == UNIT_ST || b->type == UNIT_SUPPLY
        || !needs_supply(a->g, stats_of(a->g, b), c)) {
        supply_detour(a);
        return;
    }
    ai->customer_id = u->bumped;                        /* .adopt */
    supply_dock(a, b);
}

/* F PW-SS10, supply (UnitAI_OrderSupply; every SUPPLY runs it whatever
 * its order): drive to the customer find_customer picked, dock, serve,
 * turn away. state: 9 waiting to face turn_to, then 8; 1-8 driving, the
 * updates left (stop at 0). */
static void order_supply(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (ai->docked) {
        supply_serve(a);
        return;
    }
    u8 s = ai->state;
    if (s == 9) {
        if (u->dir == u->turn_to) {
            ai->state = 8;
        }
        return;
    }
    if (s != 0) {
        go(u);
        ai->state = --s;
        if (s == 0) {
            stop(u);
        }
        return;
    }
    if (ai->detour_left != 0) {
        if (moving(u)) {
            ai->detour_left--;
            if (ai->detour_left == 0) {
                ai->detour_step = 0;
            }
            return;
        }
        u8 h = dir_step(u->dir, ai->detour_step);
        u->turn_to = h;
        u->aim_dir = h;
        go(u);
        return;
    }
    if (!ref_is_none(ai->customer_id)) {
        supply_approach(a);
        return;
    }
    if (u->shot_flags & HIT_BY_SHOT) {                  /* .no_customer */
        u->shot_flags &= (u8)~HIT_BY_SHOT;
        supply_evade(a);
        return;
    }
    stop(u);
    ai->hold = 0;
}

/* UnitAI_FindCustomer (SUPPLY), while it isn't on its way (hold 0): the
 * next 10 of its team's slots from the cursor, round at the last (50 in
 * the original), for a unit that needs serving -- active, not dying,
 * carried, held or an ST-57U. QUIRK: its own record isn't passed over, so
 * a SUPPLY low on fuel can pick itself. */
static void find_customer(Ai *a)
{
    HzUnitAI *ai = &a->u->ai;
    if (ai->hold != 0) {
        return;
    }
    u32 slots = side_slots(a->g, a->team);
    u32 k = (ai->cursor < slots) ? ai->cursor : 0;
    for (int n = 0; n < 10; n++) {
        const HzUnit *c = unit_at(a->g, a->team, k);
        k = (k + 1 < slots) ? k + 1 : 0;
        if ((c->flags & UNIT_ACTIVE)
            && !(c->flags & (UNIT_DYING | UNIT_CARRIED | UNIT_HELD))
            && !(c->flags & UNIT_BOAT)
            && needs_supply(a->g, stats_of(a->g, c), c)) {
            ai->cursor = k;
            ai->customer_id = unit_ref(c);
            return;
        }
    }
    ai->cursor = k;
}

/* -- the SUPPLY with the port's fix (HZ_FIX_SUPPLY) ----------------------
 *
 * FIX: the original SUPPLY serves a unit only after driving into it until
 * it stops, facing it; inside a group of units each rotation takes an AI
 * update a step, a bump into a full neighbour costs a one-step detour, and
 * after every customer it turns about (8 updates) and tries to drive off.
 * It judges a bumped unit by its customer's fuel and ammo, so it docks
 * again with the unit it has just filled. And its dock clears the
 * customer's +$3A word -- a guard's or patrol's home y -- so a unit it
 * serves then drives for the top of the map. Its collision bumper ($C
 * round it, units.c) keeps it out of any gap under 48 px, and blocks
 * every move once it is inside one (put down among units, it never moves
 * again). With the fixes:
 *  - it docks with any needy unit of its side within reach (SUPPLY_REACH
 *    px between the boxes), facing it or not, moving or not;
 *  - when one is full it serves the next needy unit in reach in place,
 *    and only drives on when none is left;
 *  - it picks the nearest needy unit within the original's $C8 px, of all
 *    its side's units, never itself and never one another SUPPLY of its
 *    side is already on its way to or serving;
 *  - it drives there by a route round the ground and the units in the way
 *    (route_find), aiming at the farthest point of it in a clear straight
 *    line, at that exact angle (supply_steer: not only the 16 headings);
 *    a turn of more than 45 degrees it makes standing; a customer it has
 *    no route to is passed over;
 *  - it collides with its plain box, and may back out of an overlap
 *    (units.c);
 *  - the customer's home is left alone.
 * The original's evade and the detour (when stuck facing its way) are
 * kept. */

#define SUPPLY_REACH  0x10      /* px between the two boxes: the
                                   original's bumper ($C) and a few px */
#define SUPPLY_SEARCH 0xC8      /* px (the larger axis): the original's
                                   give-up distance */

/* The gap between two units' boxes on one axis (negative: overlapping). */
static int box_gap(s16 a, u8 a_size, s16 b, u8 b_size)
{
    int d = b - a;
    return (d >= 0) ? d - a_size : -d - b_size;
}

static bool in_reach(const HzUnit *s, const HzUnit *c)
{
    return box_gap(s->x, s->size, c->x, c->size) <= SUPPLY_REACH
        && box_gap(s->y, s->size, c->y, c->size) <= SUPPLY_REACH;
}

/* Is another SUPPLY of a's side on its way to unit c, or serving it? */
static bool claimed_elsewhere(const Ai *a, const HzUnit *c)
{
    HzRef id = unit_ref(c);
    u32 slots = side_slots(a->g, a->team);
    for (u32 i = 0; i < slots; i++) {
        const HzUnit *s = unit_at(a->g, a->team, i);
        if (s == a->u || !(s->flags & UNIT_ACTIVE)
            || s->type != UNIT_SUPPLY
            || (s->flags & (UNIT_DYING | UNIT_CARRIED | UNIT_HELD))) {
            continue;
        }
        if (ref_eq(s->ai.customer_id, id)
            && (s->ai.hold != 0 || s->ai.docked)) {
            return true;
        }
    }
    return false;
}

/* Could unit c of a's side be a's customer: active, not dying, carried,
 * held or an ST-57U, below a quarter of something, not a itself and not
 * another SUPPLY's? */
static bool wants_supply(const Ai *a, const HzUnit *c)
{
    return c != NULL && c != a->u && (c->flags & UNIT_ACTIVE)
        && !(c->flags & (UNIT_DYING | UNIT_CARRIED | UNIT_HELD))
        && !(c->flags & UNIT_BOAT)
        && needs_supply(a->g, stats_of(a->g, c), c)
        && !claimed_elsewhere(a, c);
}

/* The needy unit in reach nearest a (its customer first), or NULL. */
static HzUnit *needy_in_reach(const Ai *a)
{
    const HzUnit *u = a->u;
    if (!ref_is_none(u->ai.customer_id)) {
        HzUnit *c = ref_unit_ptr(a->g, u->ai.customer_id);
        if (wants_supply(a, c) && in_reach(u, c)) {
            return c;
        }
    }
    HzUnit *best = NULL;
    int best_gap = 0;
    u32 slots = side_slots(a->g, a->team);
    for (u32 i = 0; i < slots; i++) {
        HzUnit *c = unit_at(a->g, a->team, i);
        if (!wants_supply(a, c) || !in_reach(u, c)) {
            continue;
        }
        int gx = box_gap(u->x, u->size, c->x, c->size);
        int gy = box_gap(u->y, u->size, c->y, c->size);
        int gap = (gx > gy) ? gx : gy;
        if (best == NULL || gap < best_gap) {
            best = c;
            best_gap = gap;
        }
    }
    return best;
}

static long isqrt(long n)
{
    long r = 0;
    while ((r + 1) * (r + 1) <= n) {
        r++;
    }
    return r;
}

/* The way to a customer (HZ_FIX_SUPPLY): a cheapest-route search over the
 * ground a SUPPLY can stand on, in a window round it and its customer,
 * from every place within reach of the customer out to the SUPPLY. A
 * place is the tile its 3 x 3 tiles start from, as obj_check_terrain
 * tests them (no tile with b15, inside the map's edge); a step costs 10
 * (14 diagonally). Places inside another unit's bumper are as the moves
 * have them (Unit_CheckObjCollision, supply_backs_away in units.c): a
 * SUPPLY never drives into one from open ground, but one already inside
 * may drive on through them and out (60 more a step). */
#define ROUTE_SIDE    128       /* tiles a side of the window, at most */
#define ROUTE_MARGIN  16        /* tiles round the SUPPLY and its customer */
#define ROUTE_AHEAD   8         /* steps along the route it looks */
#define ROUTE_NONE    0xFFFFFFFFu

typedef struct {
    int x0, y0, w, h;                       /* the window, in tiles */
    u32 cost[ROUTE_SIDE * ROUTE_SIDE];
    u8  extra[ROUTE_SIDE * ROUTE_SIDE];     /* 0 ground, 1 bumper, 2 none */
    u32 heap[ROUTE_SIDE * ROUTE_SIDE * 8];
    int heap_n;
} Route;

static Route route;

/* The place's top-left pixel: (x + 4) / 8 is the tile. */
static s16 route_px(int t)
{
    return (s16)(t * 8 - 4);
}

/* Would SUPPLY u at place (tx, ty) overlap unit o (Unit_CheckObjCollision:
 * plain boxes with the fixes), or have its centre in o's grid cell (32 px:
 * one object a cell)? */
static bool route_bumps(const HzUnit *u, const HzUnit *o, int tx, int ty)
{
    s16 x = route_px(tx);
    s16 y = route_px(ty);
    if (box_gap(x, u->size, o->x, o->size) < 0
        && box_gap(y, u->size, o->y, o->size) < 0) {
        return true;
    }
    int half = u->size >> 1;
    int ohalf = o->size >> 1;
    return ((x + half) >> 5) == ((o->x + ohalf) >> 5)
        && ((y + half) >> 5) == ((o->y + ohalf) >> 5);
}

/* (Session 72) The same for a repair truck or a mechanic, by the grid the
 * game has: with UNITS (crowd.c) units collide by their boxes alone, so a
 * man may walk right up to a vehicle; without, route_bumps' one a cell. */
static bool route_bumps_in(const HzGame *g, const HzUnit *u, const HzUnit *o,
                           int tx, int ty)
{
    if (!g->crowd) {
        return route_bumps(u, o, tx, ty);
    }
    return box_gap(route_px(tx), u->size, o->x, o->size) < 0
        && box_gap(route_px(ty), u->size, o->y, o->size) < 0;
}

static void route_push(u32 cost, int cell)
{
    Route *r = &route;
    u32 key = (cost << 14) | (u32)cell;
    int i = r->heap_n++;
    while (i > 0 && r->heap[(i - 1) / 2] > key) {
        r->heap[i] = r->heap[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    r->heap[i] = key;
}

static u32 route_pop(void)
{
    Route *r = &route;
    u32 top = r->heap[0];
    u32 last = r->heap[--r->heap_n];
    int i = 0;
    for (;;) {
        int c = i * 2 + 1;
        if (c >= r->heap_n) {
            break;
        }
        if (c + 1 < r->heap_n && r->heap[c + 1] < r->heap[c]) {
            c++;
        }
        if (r->heap[c] >= last) {
            break;
        }
        r->heap[i] = r->heap[c];
        i = c;
    }
    r->heap[i] = last;
    return top;
}

static int route_tile(s16 px)
{
    return (u16)(px + 4) >> 3;
}

/* (Session 72) What a search is for (route_search's flags): the SUPPLY's
 * has none. */
#define ROUTE_CAREFUL 1         /* not over damaging ground (VULKAN's) */
#define ROUTE_DRY     2         /* nor into a current (a truck's: it can't
                                   hold its line across one as a man can) */
#define ROUTE_FAST    4         /* the units marked by the places each
                                   covers: the same marks, made sooner */

/* Can unit u stand at place (tx, ty): inside the map's edge and no tile
 * with b15 under it, as obj_check_terrain tests them (a SUPPLY's 3 x 3
 * tiles, the edge at tile $FC). (Session 72) Any unit's own tiles (2 x 2
 * on foot) and edge, and with `flags` no damaging ground or current under
 * it either. */
static bool route_ground_for(const HzGame *g, const HzUnit *u, int tx, int ty,
                             int flags)
{
    int n = unit_tiles(u->type, repair_on(g));
    int last = 0xFF - (u->size >> 3);
    if (tx < 1 || ty < 1 || tx > last || ty > last) {
        return false;
    }
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int x = tx + c;
            int y = ty + r;
            u16 block = (u16)(g->layout[(y >> 2) & 0x3F][(x >> 2) & 0x3F]
                              & 0x1FF);
            u16 word = g->blocks[block][(y & 3) * 4 + (x & 3)];
            if (word & TILE_BLOCKED) {
                return false;
            }
            if (flags != 0 && (word & TILE_EVENT)) {
                int type = g->block_terrain[block] & 0x0F;
                if (((flags & ROUTE_CAREFUL) && type == 0x8)
                    || ((flags & ROUTE_DRY) && type == 0x6)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* (Session 72) ROUTE_FAST: every place of the window where unit u would
 * bump into another unit (route_bumps_in) marked, unit by unit: the
 * places its box overlaps the other's from, and those that put its centre
 * in the other's grid cell. The marks a loop over every place and unit
 * makes, as route_search's own; with HZ_ROUTE_CHECK defined each is
 * checked against such a loop. */
static void route_mark_units(const Ai *a, const HzUnit *c)
{
    Route *r = &route;
    const HzUnit *u = a->u;
    int half = u->size >> 1;
    for (int side = 0; side < 2; side++) {
        u32 slots = side_slots(a->g, side);
        for (u32 k = 0; k < slots; k++) {
            const HzUnit *o = unit_at_c(a->g, side, k);
            if (o == u || o == c || !(o->flags & UNIT_ACTIVE)
                || (o->flags & (UNIT_DYING | UNIT_CARRIED))) {
                continue;
            }
            int cell_x = ((o->x + (o->size >> 1)) >> 5) * 32 - half;
            int cell_y = ((o->y + (o->size >> 1)) >> 5) * 32 - half;
            int x_lo = o->x - u->size;
            int x_hi = o->x + o->size;
            int y_lo = o->y - u->size;
            int y_hi = o->y + o->size;
            x_lo = (cell_x < x_lo) ? cell_x : x_lo;
            x_hi = (cell_x + 31 > x_hi) ? cell_x + 31 : x_hi;
            y_lo = (cell_y < y_lo) ? cell_y : y_lo;
            y_hi = (cell_y + 31 > y_hi) ? cell_y + 31 : y_hi;
            /* a place's pixel is tile x 8 - 4; a tile more each way */
            int tx0 = (x_lo + 4 + 0x1000) / 8 - 0x200 - 1 - r->x0;
            int tx1 = (x_hi + 4 + 0x1000) / 8 - 0x200 + 1 - r->x0;
            int ty0 = (y_lo + 4 + 0x1000) / 8 - 0x200 - 1 - r->y0;
            int ty1 = (y_hi + 4 + 0x1000) / 8 - 0x200 + 1 - r->y0;
            tx0 = (tx0 < 0) ? 0 : tx0;
            ty0 = (ty0 < 0) ? 0 : ty0;
            tx1 = (tx1 >= r->w) ? r->w - 1 : tx1;
            ty1 = (ty1 >= r->h) ? r->h - 1 : ty1;
            for (int y = ty0; y <= ty1; y++) {
                for (int x = tx0; x <= tx1; x++) {
                    int i = y * r->w + x;
                    if (r->extra[i] == 0
                        && route_bumps_in(a->g, u, o, r->x0 + x,
                                          r->y0 + y)) {
                        r->extra[i] = 1;
                    }
                }
            }
        }
    }
#ifdef HZ_ROUTE_CHECK
    for (int i = 0; i < r->w * r->h; i++) {
        if (r->extra[i] == 2) {
            continue;
        }
        u8 slow = 0;
        for (int side = 0; side < 2 && slow == 0; side++) {
            u32 slots = side_slots(a->g, side);
            for (u32 k = 0; k < slots; k++) {
                const HzUnit *o = unit_at_c(a->g, side, k);
                if (o == u || o == c || !(o->flags & UNIT_ACTIVE)
                    || (o->flags & (UNIT_DYING | UNIT_CARRIED))) {
                    continue;
                }
                if (route_bumps_in(a->g, u, o, r->x0 + i % r->w,
                                   r->y0 + i / r->w)) {
                    slow = 1;
                    break;
                }
            }
        }
        if (slow != r->extra[i]) {
            fprintf(stderr, "route_mark_units: place %d,%d is %d, the loop's "
                    "%d\n", r->x0 + i % r->w, r->y0 + i / r->w, r->extra[i],
                    slow);
            abort();
        }
    }
#endif
}

/* The search, from the places within `reach` px of unit c (between the
 * boxes) out until unit u's place is settled. False: no route. (Session
 * 72: route_find, the SUPPLY's, with its reach and what the search is for
 * -- `flags`, ROUTE_* -- made its to choose.) */
static bool route_search(const Ai *a, const HzUnit *c, int reach, int flags)
{
    Route *r = &route;
    const HzUnit *u = a->u;
    int ux = route_tile(u->x);
    int uy = route_tile(u->y);
    int cx = route_tile(c->x);
    int cy = route_tile(c->y);
    r->x0 = ((ux < cx) ? ux : cx) - ROUTE_MARGIN;
    r->y0 = ((uy < cy) ? uy : cy) - ROUTE_MARGIN;
    r->w = ((ux > cx) ? ux : cx) + ROUTE_MARGIN - r->x0 + 1;
    r->h = ((uy > cy) ? uy : cy) + ROUTE_MARGIN - r->y0 + 1;
    if (r->w > ROUTE_SIDE || r->h > ROUTE_SIDE) {
        return false;
    }
    r->heap_n = 0;
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            int i = y * r->w + x;
            int tx = r->x0 + x;
            int ty = r->y0 + y;
            r->cost[i] = ROUTE_NONE;
            r->extra[i] = 0;
            if (!route_ground_for(a->g, u, tx, ty,
                                  flags & (ROUTE_CAREFUL | ROUTE_DRY))) {
                r->extra[i] = 2;
                continue;
            }
            for (int side = 0;
                 side < 2 && r->extra[i] == 0 && !(flags & ROUTE_FAST);
                 side++) {
                u32 slots = side_slots(a->g, side);
                for (u32 k = 0; k < slots; k++) {
                    const HzUnit *o = unit_at_c(a->g, side, k);
                    if (o == a->u || o == c || !(o->flags & UNIT_ACTIVE)
                        || (o->flags & (UNIT_DYING | UNIT_CARRIED))) {
                        continue;
                    }
                    if (route_bumps(u, o, tx, ty)) {
                        r->extra[i] = 1;
                        break;
                    }
                }
            }
            if (box_gap(route_px(tx), u->size, c->x, c->size) <= reach
                && box_gap(route_px(ty), u->size, c->y, c->size) <= reach) {
                r->cost[i] = 0;
                route_push(0, i);
            }
        }
    }
    if (flags & ROUTE_FAST) {
        route_mark_units(a, c);
    }
    int goal = (uy - r->y0) * r->w + (ux - r->x0);
    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    while (r->heap_n > 0) {
        u32 key = route_pop();
        int i = (int)(key & 0x3FFF);
        u32 cost = key >> 14;
        if (cost != r->cost[i]) {
            continue;
        }
        if (i == goal) {
            return true;
        }
        int x = i % r->w;
        int y = i / r->w;
        for (int d = 0; d < 8; d++) {
            int nx = x + DX[d];
            int ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= r->w || ny >= r->h) {
                continue;
            }
            int n = ny * r->w + nx;
            if (r->extra[n] == 2 && n != goal) {
                continue;
            }
            if (d >= 4 && (r->extra[y * r->w + nx] == 2
                           || r->extra[ny * r->w + x] == 2)) {
                continue;               /* no cutting a corner */
            }
            if (r->extra[i] == 1 && r->extra[n] == 0) {
                continue;               /* the SUPPLY would drive n -> i:
                                           into a bumper from open ground */
            }
            u32 step = (d < 4) ? 10 : 14;
            if (r->extra[n] == 1) {
                step += 60;
            }
            if (cost + step < r->cost[n]) {
                r->cost[n] = cost + step;
                route_push(cost + step, n);
            }
        }
    }
    return false;
}

/* The SUPPLY's search: to within its reach of customer c. */
static bool route_find(const Ai *a, const HzUnit *c)
{
    return route_search(a, c, SUPPLY_REACH, 0);
}

/* Is the straight line from SUPPLY u to place (x, y) of the window clear:
 * every place it crosses ground, and no bumper unless u is in one now? */
static bool route_clear(const HzUnit *u, int x, int y)
{
    Route *r = &route;
    int ux = route_tile(u->x) - r->x0;
    int uy = route_tile(u->y) - r->y0;
    bool inside = r->extra[uy * r->w + ux] == 1;
    long px = u->x;
    long py = u->y;
    long dx = route_px(r->x0 + x) - px;
    long dy = route_px(r->y0 + y) - py;
    long len = isqrt(dx * dx + dy * dy);
    for (long t = 0; t <= len; t += 4) {
        int tx = route_tile((s16)(px + dx * t / (len ? len : 1))) - r->x0;
        int ty = route_tile((s16)(py + dy * t / (len ? len : 1))) - r->y0;
        if (tx < 0 || ty < 0 || tx >= r->w || ty >= r->h) {
            return false;
        }
        u8 e = r->extra[ty * r->w + tx];
        if (e == 2 || (e == 1 && !inside)) {
            return false;
        }
    }
    return true;
}

/* After route_find: where SUPPLY u drives at -- the farthest place up to
 * ROUTE_AHEAD steps down the route that it can drive at in a straight
 * line (the next step at least), as a pixel position. */
static void route_ahead(const HzUnit *u, s16 *px, s16 *py)
{
    Route *r = &route;
    int x = route_tile(u->x) - r->x0;
    int y = route_tile(u->y) - r->y0;
    int path_x[ROUTE_AHEAD];
    int path_y[ROUTE_AHEAD];
    int n = 0;
    while (n < ROUTE_AHEAD) {
        u32 best = r->cost[y * r->w + x];
        int bx = x;
        int by = y;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx;
                int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= r->w || ny >= r->h) {
                    continue;
                }
                if (r->cost[ny * r->w + nx] < best) {
                    best = r->cost[ny * r->w + nx];
                    bx = nx;
                    by = ny;
                }
            }
        }
        if (bx == x && by == y) {
            break;
        }
        x = bx;
        y = by;
        path_x[n] = x;
        path_y[n] = y;
        n++;
    }
    int pick = 0;
    for (int k = n - 1; k > 0; k--) {
        if (route_clear(u, path_x[k], path_y[k])) {
            pick = k;
            break;
        }
    }
    if (n == 0) {
        *px = u->x;
        *py = u->y;
        return;
    }
    *px = route_px(r->x0 + path_x[pick]);
    *py = route_px(r->y0 + path_y[pick]);
}

/* Dock with unit c: it is held and stopped while served; its home kept
 * (only a SUPPLY customer's detour is cleared). */
static void supply_dock_fixed(Ai *a, HzUnit *c)
{
    HzUnit *u = a->u;
    u->ai.customer_id = unit_ref(c);
    u->ai.hold = 0xFF;
    u->ai.docked = true;
    u->ai.skip_id = ref_none();
    u->ai.route_going = false;
    u->ai.customer_speed = (u8)c->speed;
    c->speed = 0;
    c->flags |= UNIT_HELD;
    if (c->type == UNIT_SUPPLY) {
        c->ai.detour_left = 0;
        c->ai.detour_step = 0;
    }
    stop(u);
}

/* Serving: as supply_serve, but a full customer is followed by the next
 * needy unit in reach, in place; with none it stops and find_customer_fixed
 * picks the next (no turning about). */
static void supply_serve_fixed(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    HzUnit *c = ref_unit_ptr(a->g, ai->customer_id);
    if (u->shot_flags & HIT_BY_SHOT) {
        supply_hit(a, c);
        return;
    }
    if (!(c->flags & (UNIT_DYING | UNIT_CARRIED)) && !supply_fill(a, c)) {
        return;
    }
    supply_release(a, c);
    ai->hold = 0;
    ai->customer_id = ref_none();
    HzUnit *next = needy_in_reach(a);
    if (next != NULL) {
        supply_dock_fixed(a, next);
        return;
    }
    stop(u);
}

/* On the way to the customer: dock with it, or with any needy unit, as
 * soon as one is in reach; drop it once it no longer wants serving (full,
 * gone, carried, another SUPPLY's). Else head for it; stopped against
 * something, a detour. */
static void supply_approach_fixed(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    const HzUnit *cu = ref_unit_ptr(a->g, ai->customer_id);
    if (!wants_supply(a, cu)) {
        ai->customer_id = ref_none();
        ai->hold = 0;
        stop(u);
        return;
    }
    HzUnit *here = needy_in_reach(a);
    if (here != NULL) {
        supply_dock_fixed(a, here);
        return;
    }
    if (!route_find(a, cu)) {
        ai->skip_id = ai->customer_id;  /* no way there: another */
        ai->customer_id = ref_none();
        ai->hold = 0;
        stop(u);
        return;
    }
    s16 wx;
    s16 wy;
    route_ahead(u, &wx, &wy);
    u8 d = get_direction(u->x, u->y, wx, wy);
    u->turn_to = d;
    u->aim_dir = d;
    ai->route_x = wx;
    ai->route_y = wy;
    ai->hold = 0xFF;
    int turn = (d - u->dir) & 0x0F;
    if (turn > 8) {
        turn = 16 - turn;
    }
    if (turn > 2) {                     /* over 45 degrees: turn standing */
        stop(u);
        ai->route_going = false;
        return;
    }
    if (moving(u)) {
        return;
    }
    if (ai->route_going && (u->blocked & (PROBE_OBJECT | PROBE_TILE))) {
        ai->route_going = false;        /* facing its way and still stuck */
        supply_detour(a);
        return;
    }
    ai->route_going = true;
    go(u);
}

/* After Unit_SetSprite: a SUPPLY driving along its route goes straight at
 * the place it aims at, not along the nearest of the 16 headings (its
 * sprite still shows that heading); the speed as Unit_SetSprite's,
 * speed x 32. */
static void supply_steer(Ai *a)
{
    HzUnit *u = a->u;
    const HzUnitAI *ai = &u->ai;
    if (!ai->route_going || ai->docked || ref_is_none(ai->customer_id)
        || !moving(u) || (u->flags & UNIT_REVERSING)) {
        return;
    }
    long dx = ai->route_x - u->x;
    long dy = ai->route_y - u->y;
    long len = isqrt(dx * dx + dy * dy);
    if (len == 0) {
        return;
    }
    long speed = (s16)u->speed;
    u->target_vel_x = (s16)(speed * 32 * dx / len);
    u->target_vel_y = (s16)(speed * 32 * dy / len);
}

/* F PW-SS10 with the fixes: as order_supply, the evade and detours kept. */
static void order_supply_fixed(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (ai->docked) {
        supply_serve_fixed(a);
        return;
    }
    u8 s = ai->state;
    if (s == 9) {
        if (u->dir == u->turn_to) {
            ai->state = 8;
        }
        return;
    }
    if (s != 0) {
        go(u);
        ai->state = --s;
        if (s == 0) {
            stop(u);
        }
        return;
    }
    if (ai->detour_left != 0) {
        if (moving(u)) {
            ai->detour_left--;
            if (ai->detour_left == 0) {
                ai->detour_step = 0;
            }
            return;
        }
        u8 h = dir_step(u->dir, ai->detour_step);
        u->turn_to = h;
        u->aim_dir = h;
        go(u);
        return;
    }
    if (!ref_is_none(ai->customer_id)) {
        supply_approach_fixed(a);
        return;
    }
    if (u->shot_flags & HIT_BY_SHOT) {
        u->shot_flags &= (u8)~HIT_BY_SHOT;
        supply_evade(a);
        return;
    }
    HzUnit *here = needy_in_reach(a);
    if (here != NULL) {
        supply_dock_fixed(a, here);
        return;
    }
    stop(u);
    ai->hold = 0;
}

/* The customer, while it isn't on its way or serving: the nearest unit of
 * its side that wants serving within SUPPLY_SEARCH px, or none. */
static void find_customer_fixed(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (ai->hold != 0 || ai->docked) {
        return;
    }
    const HzUnit *best = NULL;
    u16 best_d = 0;
    u32 slots = side_slots(a->g, a->team);
    for (u32 i = 0; i < slots; i++) {
        const HzUnit *c = unit_at(a->g, a->team, i);
        if (!wants_supply(a, c) || ref_eq(unit_ref(c), ai->skip_id)) {
            continue;
        }
        u16 d;
        dir_and_dist(u->x, u->y, c->x, c->y, &d);
        if (d < SUPPLY_SEARCH && (best == NULL || d < best_d)) {
            best = c;
            best_d = d;
        }
    }
    ai->customer_id = (best != NULL) ? unit_ref(best) : ref_none();
}

/* -- the repair truck and its mechanics (HZ_FIX_REPAIR, session 72) -------
 *
 * The port's own units (docs/repair.md; data/repair.json has their records
 * and the numbers below named there). The owner: "make a "repair" truck
 * or mechanic unit for repairing vehicles ... the truck stops in an area
 * and two foot men get out and begin repairing damaged units and then
 * retreat to truck if enemy units or mech get close enough."
 *
 * The truck (type 10, the SUPPLY's sprites in its own colours) carries two
 * mechanics: its ammo is its crew, REPAIR_MAN a man, so the order menu's
 * gauge shows them and an own outpost or HQ brings it back to two as it
 * tops up any unit's ammo. Each update it:
 *  - takes alarm at a hit, or an enemy unit or mech within `danger` px of
 *    it or of one of its men; the alarm lasts until none has been that
 *    near the truck for `calm` frames;
 *  - with an alarm, or a man short, calls its men in, waits for them, and
 *    drives to its side's nearest outpost or HQ by the ground (paths.c's
 *    fields, whether or not PATHS is on), where a man short is made up;
 *  - else, with a damaged vehicle of its side within `reach` px that no
 *    enemy is near, stays where it is and sends a man out for it, one an
 *    update;
 *  - else drives for the nearest such vehicle within `search` px, by the
 *    SUPPLY's route search, to within `park` px of it.
 * A mechanic (type 11, INFANTRY's sprites, the uniform green) is a unit
 * while he is out: shot at, shot, in the way. He walks to his vehicle by
 * the same search on his own two tiles a side, mends it `rate` HP an
 * update from MAN_TOUCH px, takes the next one within the truck's reach,
 * and with none, or called, walks back and climbs in (his record freed).
 * His truck lifted, he waits where he is; destroyed, he is INFANTRY from
 * then on, as he is if he finds no way back in MAN_LOST updates. No mech
 * lifts him (mech_update.c).
 * Mended: every vehicle of its side but the FWA -- the truck itself too --
 * and a boat only where a man can get within reach from the shore. A
 * vehicle no way is found to is left alone for MEND_WAIT updates. */

#define MAN_REACH     0x10      /* px between a man's box and what he walks
                                   up to (as SUPPLY_REACH), */
#define MAN_TOUCH     0x14      /* and from where he mends it or boards: a
                                   little more, so a man a pixel short of
                                   his search's goal is there */
#define PARK_SLACK    8         /* likewise a truck's, past `park` */
#define MAN_GIVE_UP   150       /* updates a man walks for one vehicle */
#define MAN_LOST      200       /* updates on his way back before he is on
                                   his own */
#define TRUCK_GIVE_UP 200       /* updates a truck drives for one vehicle */
#define MEND_WAIT     50        /* updates a vehicle no way was found to is
                                   left alone */
#define HOME_POST_PX  64        /* how near an own outpost's middle is home, */
#define HOME_HQ_PX    112       /* and an HQ's (as paths.c ends its ways) */
#define CREW_UP       2         /* ammo a truck takes on an update at home
                                   (Obj_RepairRefuel's) */

/* The distance between two units' middles, the larger axis. */
static int centre_dist(const HzUnit *p, const HzUnit *q)
{
    int dx = (p->x + (p->size >> 1)) - (q->x + (q->size >> 1));
    int dy = (p->y + (p->size >> 1)) - (q->y + (q->size >> 1));
    dx = (dx < 0) ? -dx : dx;
    dy = (dy < 0) ? -dy : dy;
    return (dx > dy) ? dx : dy;
}

/* An enemy of `team` within r px of unit u's middle (the larger axis):
 * its mech, unless destroyed, or a unit of its that isn't a SUPPLY, a
 * repair truck or a mechanic. */
static bool enemy_near(const HzGame *g, int team, const HzUnit *u, int r)
{
    int cx = u->x + (u->size >> 1);
    int cy = u->y + (u->size >> 1);
    int foe = team ^ 1;
    const HzMech *m = &g->mech[foe];
    if (!(m->flags & MECH_DESTROYED)) {
        int dx = m->x + (m->size >> 1) - cx;
        int dy = m->y + (m->size >> 1) - cy;
        if (dx >= -r && dx <= r && dy >= -r && dy <= r) {
            return true;
        }
    }
    u32 slots = side_slots(g, foe);
    for (u32 i = 0; i < slots; i++) {
        const HzUnit *o = unit_at_c(g, foe, i);
        if (!(o->flags & UNIT_ACTIVE)
            || (o->flags & (UNIT_DYING | UNIT_CARRIED))
            || o->type == UNIT_SUPPLY || o->type == UNIT_REPAIR
            || o->type == UNIT_MECHANIC) {
            continue;
        }
        int dx = o->x + (o->size >> 1) - cx;
        int dy = o->y + (o->size >> 1) - cy;
        if (dx >= -r && dx <= r && dy >= -r && dy <= r) {
            return true;
        }
    }
    return false;
}

/* A vehicle a mechanic mends, and damaged: active, not dying or carried,
 * not a man or an FWA, under its type's HP, and not being left alone. */
static bool mends(const HzGame *g, const HzUnit *c)
{
    if (c == NULL || !(c->flags & UNIT_ACTIVE)
        || (c->flags & (UNIT_DYING | UNIT_CARRIED))
        || unit_on_foot(c->type, true) || c->type == UNIT_FWA) {
        return false;
    }
    return c->ai.mend_wait == 0 && c->hp < unit_stats_for(g, c->type)->hp;
}

/* Is a mechanic of a's side other than a's own unit on vehicle c? */
static bool mend_claimed(const Ai *a, const HzUnit *c)
{
    HzRef id = unit_ref(c);
    u32 slots = side_slots(a->g, a->team);
    for (u32 i = 0; i < slots; i++) {
        const HzUnit *m = unit_at_c(a->g, a->team, i);
        if (m != a->u && (m->flags & UNIT_ACTIVE)
            && !(m->flags & UNIT_DYING) && m->type == UNIT_MECHANIC
            && ref_eq(m->ai.customer_id, id)) {
            return true;
        }
    }
    return false;
}

/* The work there is: of a's side's damaged vehicles within `range` px of
 * unit `round`, with no enemy near and no other mechanic on it, the one
 * nearest unit `from`; NULL none. */
static HzUnit *mend_job(const Ai *a, const HzUnit *round, int range,
                        const HzUnit *from)
{
    const HzRepair *rp = &a->g->assets->repair;
    HzUnit *best = NULL;
    int best_d = 0;
    u32 slots = side_slots(a->g, a->team);
    for (u32 i = 0; i < slots; i++) {
        HzUnit *c = unit_at(a->g, a->team, i);
        if (!mends(a->g, c) || centre_dist(round, c) > range) {
            continue;
        }
        int d = centre_dist(from, c);
        if ((best != NULL && d >= best_d) || mend_claimed(a, c)
            || enemy_near(a->g, a->team, c, rp->danger)) {
            continue;
        }
        best = c;
        best_d = d;
    }
    return best;
}

/* Are units s and c within `gap` px of each other, box to box? */
static bool in_gap(const HzUnit *s, const HzUnit *c, int gap)
{
    return box_gap(s->x, s->size, c->x, c->size) <= gap
        && box_gap(s->y, s->size, c->y, c->size) <= gap;
}

/* The SUPPLY's detour (supply_detour), a truck's and a mechanic's too:
 * true while one is on. */
static bool repair_detour(Ai *a)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    if (ai->detour_left == 0) {
        return false;
    }
    if (moving(u)) {
        ai->detour_left--;
        if (ai->detour_left == 0) {
            ai->detour_step = 0;
        }
        return true;
    }
    u8 h = dir_step(u->dir, ai->detour_step);
    u->turn_to = h;
    u->aim_dir = h;
    go(u);
    return true;
}

static void repair_halt(HzUnit *u)
{
    stop(u);
    u->ai.route_going = false;
}

/* After a route_search to unit c that found a way: off along it, as
 * supply_approach_fixed -- at the farthest place of the route in a clear
 * straight line (repair_steer goes straight at it), a detour when it is
 * stopped facing its way. `standing`: a turn of more than 45 degrees made
 * where it is (a truck's; a man's sprite turns at once). */
static void repair_drive(Ai *a, const HzUnit *c, bool standing)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    s16 wx;
    s16 wy;
    route_ahead(u, &wx, &wy);
    if (wx == u->x && wy == u->y) {
        /* its place is within reach as the search counts it (by the
         * tile), it isn't quite: straight at c */
        wx = (s16)(c->x + ((c->size - u->size) >> 1));
        wy = (s16)(c->y + ((c->size - u->size) >> 1));
    }
    u8 d = get_direction(u->x, u->y, wx, wy);
    u->turn_to = d;
    u->aim_dir = d;
    ai->route_x = wx;
    ai->route_y = wy;
    int turn = (d - u->dir) & 0x0F;
    if (turn > 8) {
        turn = 16 - turn;
    }
    if (standing && turn > 2) {
        repair_halt(u);
        return;
    }
    if (moving(u)) {
        ai->route_going = true;
        return;
    }
    if (ai->route_going && (u->blocked & (PROBE_OBJECT | PROBE_TILE))) {
        ai->route_going = false;        /* facing its way and still stuck */
        supply_detour(a);
        return;
    }
    ai->route_going = true;
    go(u);
}

/* After Unit_SetSprite: a truck or a mechanic on its way goes straight at
 * the place it aims at, as supply_steer (a truck on a paths.c way home is
 * path_steer's). */
static void repair_steer(Ai *a)
{
    HzUnit *u = a->u;
    const HzUnitAI *ai = &u->ai;
    if (!ai->route_going || ai->path_going || !moving(u)
        || (u->flags & UNIT_REVERSING)) {
        return;
    }
    long dx = ai->route_x - u->x;
    long dy = ai->route_y - u->y;
    long len = isqrt(dx * dx + dy * dy);
    if (len == 0) {
        return;
    }
    long speed = (s16)u->speed;
    u->target_vel_x = (s16)(speed * 32 * dx / len);
    u->target_vel_y = (s16)(speed * 32 * dy / len);
}

/* How long an alarm lasts, in updates: `calm` frames. */
static u8 alarm_updates(const HzRepair *rp)
{
    int n = rp->calm / 12;
    return (u8)((n < 1) ? 1 : (n > 255) ? 255 : n);
}

/* Truck a's men out: its side's mechanics who name it, dying ones too
 * (still its crew until their records are freed: repair_unit_freed). */
static int men_out(const Ai *a)
{
    HzRef id = unit_ref(a->u);
    int n = 0;
    u32 slots = side_slots(a->g, a->team);
    for (u32 i = 0; i < slots; i++) {
        const HzUnit *m = unit_at_c(a->g, a->team, i);
        if ((m->flags & UNIT_ACTIVE) && m->type == UNIT_MECHANIC
            && ref_eq(m->ai.truck_id, id)) {
            n++;
        }
    }
    return n;
}

/* A man out of truck a for vehicle c: a mechanic put down beside the
 * truck, on the side c is on if there is room there, else the next way
 * round, then a little further out. False: no room, or no slot (the side's
 * 50 without UNITS). He isn't one of the units built (the results'). */
static bool man_out(Ai *a, const HzUnit *c)
{
    HzGame *g = a->g;
    const HzUnit *u = a->u;
    const HzUnitStats *ms = unit_stats_for(g, UNIT_MECHANIC);
    int cx = u->x + (u->size >> 1);
    int cy = u->y + (u->size >> 1);
    u8 towards = (c == u) ? u->dir : get_direction(u->x, u->y, c->x, c->y);
    u16 built = g->units_built[a->team];
    for (int ring = 0; ring < 2; ring++) {
        int dist = (u->size >> 1) + (ms->size >> 1) + 6 + ring * 10;
        for (int k = 0; k < 16; k++) {
            int turn = (k & 1) ? -((k + 1) / 2) : k / 2;
            u8 d = (u8)((towards + turn) & 0x0F);
            s16 x = (s16)(cx + SINE_TABLE[d].x * dist / 32 - (ms->size >> 1));
            s16 y = (s16)(cy + SINE_TABLE[d].y * dist / 32 - (ms->size >> 1));
            HzUnit *m = unit_spawn_unit(g, (u8)(UNIT_ACTIVE
                                                | (a->team ? UNIT_TEAM2 : 0)),
                                        UNIT_MECHANIC, ORDER_REPAIR, d, x, y);
            if (m != NULL) {
                g->units_built[a->team] = built;
                m->speed = 0;           /* (Unit_Spawn sets it off) */
                m->ai.truck_id = unit_ref(u);
                m->ai.customer_id = unit_ref(c);
                return true;
            }
        }
    }
    return false;
}

/* The outpost or HQ truck a goes home to: of its side's (its HQ always
 * one), the nearest by the ground (paths_cost); none it can drive to, the
 * nearest. */
static int home_post(const Ai *a)
{
    const HzGame *g = a->g;
    const HzUnit *u = a->u;
    int best = -1;
    u16 best_cost = PATHS_NO_WAY;
    int near = HZ_POST_HQ + a->team;
    u16 near_d = 0xFFFF;
    u8 owner = (u8)(POST_USED | POST_OWNED | (a->team ? POST_TEAM2 : 0));
    for (int i = 0; i <= HZ_POST_HQ + 1; i++) {
        const HzPost *p = &g->posts[i];
        bool own = (i < HZ_POST_HQ) ? p->flags == owner
                                    : i == HZ_POST_HQ + a->team;
        if (!own) {
            continue;
        }
        u16 cost = paths_cost(g, u, i);
        if (cost != PATHS_NO_WAY && (best < 0 || cost < best_cost)) {
            best = i;
            best_cost = cost;
        }
        u16 d = (u16)(dist16(u->x, p->x) + dist16(u->y, p->y));
        if (d < near_d) {
            near = i;
            near_d = d;
        }
    }
    return (best >= 0) ? best : near;
}

/* Home: to home_post's outpost or HQ, down paths.c's way (path_steer's)
 * or, with none, straight at it. There it stops, and a truck short of a
 * man takes one on (CREW_UP of its ammo an update, as the outpost's own
 * service gives). */
static void truck_go_home(Ai *a)
{
    HzGame *g = a->g;
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    int post = home_post(a);
    const HzPost *p = &g->posts[post];
    int half = u->size >> 1;
    u16 dist;
    dir_and_dist((s16)(u->x + half), (s16)(u->y + half), p->x, p->y, &dist);
    if (dist <= ((post >= HZ_POST_HQ) ? HOME_HQ_PX : HOME_POST_PX)) {
        repair_halt(u);
        if (u->ammo < a->st->ammo) {
            int ammo = u->ammo + CREW_UP;
            u->ammo = (u8)((ammo < a->st->ammo) ? ammo : a->st->ammo);
        }
        return;
    }
    s16 wx;
    s16 wy;
    if (paths_next(g, u, post, &wx, &wy)) {
        ai->path_going = true;
    } else {
        wx = (s16)(p->x - half);
        wy = (s16)(p->y - half);
    }
    ai->route_x = wx;
    ai->route_y = wy;
    u8 d = get_direction(u->x, u->y, wx, wy);
    u->turn_to = d;
    u->aim_dir = d;
    if (!moving(u) && ai->route_going
        && (u->blocked & (PROBE_OBJECT | PROBE_TILE))) {
        ai->route_going = false;
        supply_detour(a);
        return;
    }
    ai->route_going = true;
    go(u);
}

/* Truck a on its way to vehicle c, further off than its men's reach: the
 * SUPPLY's search to within `park` px of it, over ground a truck keeps to
 * (no damaging floor, no current). No way, or TRUCK_GIVE_UP updates on
 * it: c left alone for a while. */
static void truck_drive_for(Ai *a, HzUnit *c)
{
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    const HzRepair *rp = &a->g->assets->repair;
    if (++ai->stuck >= TRUCK_GIVE_UP
        || !route_search(a, c, rp->park,
                         ROUTE_CAREFUL | ROUTE_DRY | ROUTE_FAST)) {
        c->ai.mend_wait = MEND_WAIT;
        ai->customer_id = ref_none();
        ai->stuck = 0;
        repair_halt(u);
        return;
    }
    repair_drive(a, c, true);
}

/* The repair truck (type 10; see above). */
static void unit_repair_truck(Ai *a)
{
    HzGame *g = a->g;
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    const HzRepair *rp = &g->assets->repair;
    if (u->shot_flags & HIT_BY_SHOT) {
        u->shot_flags &= (u8)~HIT_BY_SHOT;
        ai->alarm = alarm_updates(rp);
    } else if (enemy_near(g, a->team, u, rp->danger)) {
        ai->alarm = alarm_updates(rp);
    } else if (ai->alarm != 0) {
        ai->alarm--;
    }
    int out = men_out(a);
    int crew = u->ammo / REPAIR_MAN;
    ai->recall = ai->alarm != 0 || crew < a->st->ammo / REPAIR_MAN;
    if (repair_detour(a)) {
        return;
    }
    if (ai->recall) {
        ai->customer_id = ref_none();
        ai->stuck = 0;
        if (out > 0) {
            repair_halt(u);             /* it waits for its men */
            return;
        }
        truck_go_home(a);
        return;
    }
    if (out == 0 && !ref_is_none(ai->customer_id)) {
        /* on its way to a vehicle: on, while it still wants mending */
        HzUnit *c = ref_unit_ptr(g, ai->customer_id);
        if (mends(g, c) && !in_gap(u, c, rp->park + PARK_SLACK)
            && centre_dist(u, c) <= rp->search + 64
            && !mend_claimed(a, c)
            && !enemy_near(g, a->team, c, rp->danger)) {
            truck_drive_for(a, c);
            return;
        }
        ai->customer_id = ref_none();
        ai->stuck = 0;
    }
    HzUnit *job = mend_job(a, u, rp->reach, u);
    if (out > 0 || job != NULL) {
        repair_halt(u);                 /* work within reach: here */
        ai->customer_id = ref_none();
        if (job != NULL && crew - out > 0) {
            man_out(a, job);
        }
        return;
    }
    job = mend_job(a, u, rp->search, u);
    if (job == NULL) {
        repair_halt(u);
        return;
    }
    ai->customer_id = unit_ref(job);
    ai->stuck = 0;
    truck_drive_for(a, job);
}

/* A mechanic back in his truck: his record freed (he is in the grid, as
 * every unit between its moves). */
static void man_board(Ai *a)
{
    HzGame *g = a->g;
    HzUnit *u = a->u;
    grid_clear_unit(g, u);
    u->flags = 0;
    g->unit_count[a->team]--;
    g->unit_type_count[a->team][UNIT_MECHANIC]--;
}

/* A mechanic on his way to unit c (a vehicle, or his truck): the search
 * on his own tiles, keeping off damaging ground, to within MAN_REACH.
 * False: no way. */
static bool man_walk(Ai *a, const HzUnit *c)
{
    if (!route_search(a, c, MAN_REACH, ROUTE_CAREFUL | ROUTE_FAST)) {
        repair_halt(a->u);
        return false;
    }
    repair_drive(a, c, false);
    return true;
}

/* A mechanic (type 11; see above). */
static void unit_mechanic(Ai *a)
{
    HzGame *g = a->g;
    HzUnit *u = a->u;
    HzUnitAI *ai = &u->ai;
    const HzRepair *rp = &g->assets->repair;
    ai->working = false;
    HzUnit *t = ref_unit_ptr(g, ai->truck_id);
    if (t == NULL || !(t->flags & UNIT_ACTIVE) || (t->flags & UNIT_DYING)
        || t->type != UNIT_REPAIR) {
        mechanic_to_infantry(g, u);     /* no truck: on his own */
        return;
    }
    if (t->flags & UNIT_CARRIED) {
        repair_halt(u);                 /* his truck lifted: he waits */
        return;
    }
    if ((u->shot_flags & HIT_BY_SHOT)
        || enemy_near(g, a->team, u, rp->danger)) {
        u->shot_flags &= (u8)~HIT_BY_SHOT;
        t->ai.alarm = alarm_updates(rp);
        t->ai.recall = true;
    }
    if (repair_detour(a)) {
        return;
    }
    HzUnit *c = NULL;
    if (!t->ai.recall) {
        c = ref_unit_ptr(g, ai->customer_id);
        if (!mends(g, c) || centre_dist(t, c) > rp->reach
            || enemy_near(g, a->team, c, rp->danger)) {
            c = mend_job(a, t, rp->reach, u);   /* the next, if any */
            if (c != NULL) {
                ai->stuck = 0;
            }
        }
    }
    if (c == NULL) {
        /* back to the truck, and in */
        if (!ref_is_none(ai->customer_id)) {
            ai->stuck = 0;              /* (from a vehicle: a new walk) */
        }
        ai->customer_id = ref_none();
        if (in_gap(u, t, MAN_TOUCH)) {
            man_board(a);
            return;
        }
        bool way = man_walk(a, t);
        int stuck = ai->stuck + (way ? 1 : 4);
        if (stuck >= MAN_LOST) {
            t->ammo = (u8)((t->ammo >= REPAIR_MAN) ? t->ammo - REPAIR_MAN
                                                   : 0);
            mechanic_to_infantry(g, u); /* no way back: on his own */
            return;
        }
        ai->stuck = (u8)stuck;
        return;
    }
    ai->customer_id = unit_ref(c);
    if (in_gap(u, c, MAN_TOUCH)) {
        /* mending: he faces it and works */
        const HzUnitStats *max = stats_of(g, c);
        repair_halt(u);
        u8 d = get_direction(u->x, u->y,
                             (s16)(c->x + ((c->size - u->size) >> 1)),
                             (s16)(c->y + ((c->size - u->size) >> 1)));
        u->turn_to = d;
        u->aim_dir = d;
        u32 hp = (u32)c->hp + rp->rate;
        c->hp = (u16)((hp < max->hp) ? hp : max->hp);
        ai->working = true;
        ai->stuck = 0;
        if (c->hp >= max->hp) {
            ai->customer_id = ref_none();       /* done: the next */
        }
        return;
    }
    if (++ai->stuck >= MAN_GIVE_UP || !man_walk(a, c)) {
        c->ai.mend_wait = MEND_WAIT;    /* no way to it: left for now */
        ai->customer_id = ref_none();
        ai->stuck = 0;
        repair_halt(u);
    }
}

/* -- types ---------------------------------------------------------------- */

/* UnitAI_CheckFuelAim: out of fuel, stop for good and only aim; false.
 * Attack HQ past its approach doesn't check. */
static bool check_fuel_aim(Ai *a)
{
    HzUnit *u = a->u;
    if (u->order == ORDER_ATTACK_HQ && u->ai.attack_state != 0) {
        return true;
    }
    if (u->fuel != 0) {
        return true;
    }
    u->max_speed = 0;
    stop(u);
    aim_at_target(u);
    return false;
}

/* UnitAI_RunOrder: attack HQ first reads the map's path hint; then the
 * order (& $F) runs. */
static void run_order(Ai *a)
{
    HzUnit *u = a->u;
    if (u->order == ORDER_ATTACK_HQ) {
        int h = path_hint(a->g, u);
        u->ai.path_hint = (h < 0) ? 0 : (u8)(h + 1);
    }
    switch (u->order & 0x0F) {
    case ORDER_ROAM:
        order_roam(a);
        break;
    case ORDER_STAND:
        order_stand(a);
        break;
    case ORDER_PATROL:
        order_patrol(a);
        break;
    case ORDER_OUTPOST:
        order_outpost(a);
        break;
    case ORDER_GUARD:
        order_guard(a, false);
        break;
    case ORDER_GUARD_NEAR:
        order_guard(a, true);
        break;
    case ORDER_AMBUSH:
        order_ambush(a);
        break;
    case ORDER_ADVANCE:
        order_advance(a);
        break;
    case ORDER_HOLD:
        order_hold(a);
        break;
    case ORDER_CAPTURE:
        order_capture(a);
        break;
    case ORDER_ATTACK_HQ:
        order_attack_hq(a);
        break;
    case ORDER_SUPPLY:
        /* UNKNOWN: a unit other than a SUPPLY given this order would mix
         * the SUPPLY's fields with its target's on the 68k (they share
         * +$46/+$48); nothing gives one this order */
        order_supply(a);
        break;
    default:
        break;                          /* $B-$E: nothing */
    }
}

/* Attack HQ's phases: towards the enemy HQ; even phases look for a target
 * (while approaching), odd ones shoot. */
static void attack_hq_phase(Ai *a)
{
    dir_to_enemy_hq(a);
    if (a->u->phase & 1) {
        fire_at_hq(a);
    } else {
        find_target_hq(a);
    }
}

/* A SAM's attack HQ: even phases aim at the enemy mech, odd ones fire. */
static void sam_attack_hq_phase(Ai *a)
{
    dir_to_enemy_hq(a);
    if (a->u->phase & 1) {
        fire_missile_at_mech(a);
    } else {
        aim_at_mech(a);
    }
}

/* What most types do after their order, by phase & 3. */
static void common_phases(Ai *a)
{
    if (a->u->order == ORDER_ATTACK_HQ) {
        attack_hq_phase(a);
        return;
    }
    switch (a->u->phase & 3) {
    case 0:
        find_target(a);
        break;
    case 1:
        goal_outpost(a);
        break;
    case 2:
        fire_at_target(a, false);
        break;
    default:
        scan_far(a);
        break;
    }
}

/* AMR, TAX, ST-57U, FWA (UnitAI_AMR, _TAX, _ST, _FWA). */
static void unit_vehicle(Ai *a)
{
    if (check_fuel_aim(a)) {
        run_order(a);
    }
    common_phases(a);
}

/* SAM (UnitAI_SAM): unit_turret_dir is its missile delay. */
static void unit_sam(Ai *a)
{
    HzUnit *u = a->u;
    if (u->turret_dir != 0) {
        u->turret_dir--;
    }
    if (check_fuel_aim(a)) {
        run_order(a);
    }
    if (u->order == ORDER_ATTACK_HQ) {
        sam_attack_hq_phase(a);
        return;
    }
    switch (u->phase & 3) {
    case 0:
    case 2:
        aim_at_mech(a);
        break;
    case 1:
        goal_outpost(a);
        break;
    default:
        fire_missile_at_mech(a);
        break;
    }
}

/* SUPPLY (UnitAI_Supply). */
static void unit_supply(Ai *a)
{
    HzUnit *u = a->u;
    if (u->fuel == 0) {                     /* UnitAI_CheckFuel */
        u->max_speed = 0;
        stop(u);
    } else if (a->g->fixes & HZ_FIX_SUPPLY) {
        order_supply_fixed(a);
    } else {
        order_supply(a);
    }
    if (a->g->fixes & HZ_FIX_SUPPLY) {
        find_customer_fixed(a);
    } else {
        find_customer(a);
    }
}

/* GRM (UnitAI_GRM): stands whatever its order, no fuel check;
 * unit_turret_dir is its missile delay. */
static void unit_grm(Ai *a)
{
    HzUnit *u = a->u;
    if (u->turret_dir != 0) {
        u->turret_dir--;
    }
    order_stand(a);
    switch (u->phase & 3) {
    case 0:
        rescan_target(a);
        break;
    case 1:
        fire_missile_at_mech(a);
        break;
    case 2:
        fire_at_target(a, true);
        break;
    default:
        aim_at_mech(a);
        break;
    }
}

/* (Session 60) TURRET-1, TURRET-2 (types 6 and 7, with TURRETS): the
 * GRM's stand (whatever its order, no fuel check) and target scan (the
 * nearest enemy in sight, a unit before the mech), then one bullet from
 * its barrel as the vehicle it came off fires one; no missiles. Its own
 * record's sight and gun (the GRM's routine takes the GRM's), and no
 * turret of its own, so the whole sprite turns to aim. */
static void unit_turret(Ai *a)
{
    order_stand(a);
    switch (a->u->phase & 3) {
    case 0:
        rescan_target(a);
        break;
    case 2:
        fire_at_target(a, false);
        break;
    default:
        break;
    }
}

/* INFANTRY (UnitAI_Infantry): aims by unit_aim_dir as a turret does while
 * it thinks (unit_turret b7 set, cleared again after: UnitAI_InfantryDone),
 * no fuel check. UNKNOWN: why it forgets a current under it (unit_terrain
 * 6 -> 0); the CPU mech's AI reads unit_terrain. */
static void unit_infantry(Ai *a)
{
    HzUnit *u = a->u;
    if ((u->terrain & 0xFF) == 6) {
        u->terrain = 0;
    }
    u->turret |= 0x80;
    run_order(a);
    common_phases(a);
    u->turret &= 0x7F;
}

static void unit_think(Ai *a)
{
    switch (a->u->type & 0x0F) {
    case UNIT_AMR:
    case UNIT_TAX:
    case UNIT_ST:
    case UNIT_FWA:
        unit_vehicle(a);
        break;
    case UNIT_SAM:
        unit_sam(a);
        break;
    case UNIT_SUPPLY:
        unit_supply(a);
        break;
    case UNIT_GRM:
        unit_grm(a);
        break;
    case UNIT_INFANTRY:
        unit_infantry(a);
        break;
    case UNIT_TURRET1:
    case UNIT_TURRET2:
        if (turrets_on(a->g)) {
            unit_turret(a);             /* (session 60) TURRETS */
        }
        break;                          /* else nothing */
    case 0x0F:
        /* UNKNOWN: types $A-$F never exist; on the 68k they'd run with
         * the UnitStats of whichever unit came before */
        attack_hq_phase(a);
        break;
    case UNIT_REPAIR:
    case UNIT_MECHANIC:
        if (repair_on(a->g)) {          /* (session 72) REPAIR */
            if (a->u->type == UNIT_REPAIR) {
                unit_repair_truck(a);
            } else {
                unit_mechanic(a);
            }
            break;
        }
        sam_attack_hq_phase(a);         /* else as $C-$E */
        break;
    default:
        sam_attack_hq_phase(a);         /* $A-$E */
        break;
    }
}

/* -- the group ------------------------------------------------------------ */

/* UnitAI_UpdateGroup: slots group, group + 10, group + 20 ... of each
 * team (to 40 in the original's 50). A unit carried, held by a SUPPLY or
 * dying doesn't think; if it's a SUPPLY serving someone, it lets its
 * customer go. */
static void update_group(HzGame *g, int group)
{
    for (int team = 0; team < 2; team++) {
        for (u32 slot = (u32)group; slot < side_slots(g, team); slot += 10) {
            Ai a;
            a.g = g;
            a.team = team;
            a.u = unit_at(g, team, slot);
            a.st = stats_of(g, a.u);
            HzUnit *u = a.u;
            if (!(u->flags & UNIT_ACTIVE)) {
                continue;
            }
            if (u->flags & (UNIT_DYING | UNIT_CARRIED | UNIT_HELD)) {
                /* .release_customer */
                if (u->type == UNIT_SUPPLY && u->ai.docked) {
                    supply_release(&a, ref_unit_ptr(g, u->ai.customer_id));
                }
                continue;
            }
            u->tick++;
            if (u->fire_delay != 0) {
                u->fire_delay--;
            }
            u->phase++;
            u->ai.path_going = false;   /* (session 71) path_heading */
            if (u->ai.mend_wait != 0) {
                u->ai.mend_wait--;      /* (session 72) REPAIR */
            }
            unit_think(&a);
            if (!(u->flags & UNIT_ACTIVE)) {
                continue;               /* (session 72) a mechanic back in
                                           his truck: his record freed */
            }
            /* CHANGED: the original's Unit_SetSprite compares the turn
             * as a word whose high byte is whatever the type's code left
             * in d0 (SUPPLYs, from find_customer, often a nonzero one),
             * and then always turns -1; here every unit turns the short
             * way, as Unit_SetSprite means to. */
            unit_set_sprite(g, u, team ? 0x6000 : 0x4000);
            if ((g->fixes & HZ_FIX_SUPPLY) && u->type == UNIT_SUPPLY) {
                supply_steer(&a);
            }
            bool repair = repair_on(g)      /* (session 72) REPAIR */
                          && (u->type == UNIT_REPAIR
                              || u->type == UNIT_MECHANIC);
            if (repair) {
                repair_steer(&a);
            }
            if (u->ai.path_going && ((g->fixes & HZ_FIX_PATHS) || repair)) {
                path_steer(&a);
            }
        }
    }
}

/* UnitAI_Update ($5DF20), from Game_FrameLoop after Mechs_Update:
 * v_unit_ai_group runs 0..$B; groups 5 and $B think nothing (Outpost_
 * Service and the damaging ground act on group 0, Map_AnimTilesUpdate on 0
 * and 6), the others run units group (or group - 1 past 5). */
void unit_ai_update(HzGame *g)
{
    if (g->unit_ai_probe != NULL) {
        g->unit_ai_probe(g, false, g->unit_ai_probe_user);
    }
    u16 group = g->unit_ai_group;
    if (group != 5 && group != 0x0B && !g->units_frozen) {
        update_group(g, (group > 5) ? group - 1 : group);
    }
    group = (u16)(group + 1);
    if (group > 0x0B) {
        group = 0;
    }
    g->unit_ai_group = group;
    if (g->unit_ai_probe != NULL) {
        g->unit_ai_probe(g, true, g->unit_ai_probe_user);
    }
}
