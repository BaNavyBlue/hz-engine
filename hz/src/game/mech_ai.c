#include "game.h"
#include "tables.h"

/* The CPU's mech (AI_MechThink and the module under it, $A400-$F41E).
 *
 * Its only output is the mech's pad (mech_pad), which Mech_Update then
 * acts on as it would a player's -- flying, landing, firing, lifting,
 * dropping, and buying through the order menu by pressing C, the
 * directions and B. It also cheats a little: it sets its heading straight
 * (obj_dir) when it aims, and raises its own money when it wants to buy.
 *
 * Every frame, in layers:
 *   a move state (one of 16: mostly checks taken in turn, one a frame --
 *     supplies, the enemy mech, the own HQ, the launch outposts, missiles;
 *     a service trip; a fight). Many end the frame there ("skip the
 *     plan").
 *   a plan (one of 18: buy, fly somewhere, deliver a unit, pick one up,
 *     fight on foot, service an own unit ...), each a few steps. Plan 0
 *     runs a mode: setup, strategy (21 steps: where to go next and what
 *     to buy for it), buy (the buying and delivering), route (choosing a
 *     destination) or reset.
 *   AI_KeepOffEdges, which turns the pad along a map edge.
 * Plans are switched with AI_SetPlan (to a new plan), AI_CallPlan (to
 * one that will resume the current) and AI_ResumePlan.
 *
 * Its state is HzMech.ai (HzMechAI, game.h). Notes in the code:
 *   QUIRK    the original does this, probably not on purpose; kept, so
 *            the game plays as it did
 *   UNKNOWN  why, or whether it can happen, isn't known
 *   CHANGED  where the original depends on a value left over in a CPU
 *            register, this port does what the code evidently means; the
 *            note says what the original does
 * The 68k labels are given for cross-reference (asm/hz.asm). */

/* The context of one mech's think. */
typedef struct {
    HzGame   *g;
    HzMech   *m;            /* a0 */
    HzMech   *rival;        /* a1: the other mech */
    HzMechAI *ai;
    int       team;         /* obj_flags b4 */
    const HzUnitStats *kidou;   /* UnitStats_KIDOU */
} Cpu;

/* What a move state lets happen after it (a state that ends with
 * addq.l #4,sp returns past AI_MechThink_RunLayers). */
typedef enum {
    RUN_PLAN,               /* rts: the plan, then AI_KeepOffEdges */
    SKIP_PLAN,              /* nothing more this frame */
    EDGES_ONLY              /* bra AI_KeepOffEdges: no plan */
} MoveResult;

/* mech_ai_alert (+$DB) */
#define ALERT_LAUNCH_POST  0x01 /* the enemy at one of its launch outposts */
#define ALERT_HQ_GUARDS    0x80 /* own HQ short of guards */
#define ALERT_HQ_HIT       0x81 /* own HQ lost HP (move state 9) */
#define ALERT_HQ_HIT_FIGHT 0xEE /* the same, seen while fighting (state 6) */

/* mech_ai_marks bits, per record */
#define MARK_ROUTE           0x01 /* claimed by the route */
/* b1-b4, the buy flags: an HQ's and an outpost's on the same bits */
#define MARK_HQ_BIG_GARRISON 0x02 /* HQ: a garrison of 8, not 4 */
#define MARK_HQ_SUPPLY       0x04 /* HQ: its SUPPLY bought (cleared at once) */
#define MARK_HQ_ESCORT       0x08 /* HQ: escorts from the next visit on */
#define MARK_POST_SUPPLY     0x08 /* outpost: its SUPPLY bought */
#define MARK_POST_ESCORT     0x10 /* outpost: escorts from the next visit on */
#define MARK_ABANDONED       0x20 /* given up after a death */
#define MARK_LAUNCH          0x40 /* a launch or staging point */
#define MARK_VISITED         0x80 /* visited as ours */

/* The buy kinds (AI_BuyAt* d1, columns of AIBuyScriptTable). */
enum {
    KIND_GUARDS  = 0,       /* guards, dropped round the dest */
    KIND_CAPTURE = 1,       /* INFANTRY to capture */
    KIND_DEFEND  = 2,       /* dropped beside an enemy (plan $A) */
    KIND_HQ      = 3,       /* attack the enemy HQ */
    KIND_SUPPLY  = 4,
    KIND_ESCORT  = 5,
    KIND_ASSAULT = 6,
    KIND_LAUNCH  = 7        /* flown out and dropped at once (plan $D) */
};

#define REC_HQ(team)   (HZ_POST_HQ + (team))
/* An HQ record's flags, POST_TEAM2 aside ($C2, $C3). */
#define HQ_FLAGS       (POST_USED | POST_HQ | POST_OWNED)
#define MAP_EDGE_LO    8
#define MAP_EDGE_HI    0x7D8

/* -- small helpers ------------------------------------------------------ */

static u32 ai_random(Cpu *c)                    /* AI_Random: Random */
{
    return game_random(c->g);
}

static u8 dir8_pad(int dir8)                    /* AIPadDirs */
{
    return AI_PAD_DIRS[dir8 & 7];
}

/* The pad's buttons kept, its direction replaced. */
static void steer_pad(HzMech *m, int dir8)
{
    m->pad = (u8)((m->pad & HZ_PAD_BUTTONS) | dir8_pad(dir8));
}

static bool robot(const HzMech *m)
{
    return m->form == HZ_FORM_ROBOT;
}

static bool carrying(const HzMech *m)
{
    return m->form == HZ_FORM_CARRYING;
}

static bool destroyed(const HzMech *m)
{
    return (m->flags & MECH_DESTROYED) != 0;
}

/* mech_blocked b0/b1: its last walk ran into an object or a tile. */
static bool walk_blocked(const HzMech *m)
{
    return (m->blocked & (PROBE_OBJECT | PROBE_TILE)) != 0;
}

/* |a - b| as the 68k takes it (sub.w, then neg.w if negative). */
static u16 absdiff(s16 a, s16 b)
{
    s16 d = (s16)(a - b);
    return (u16)(d < 0 ? -d : d);
}

/* AI_ManhattanDist: |dx| + |dy|. */
static u16 manhattan(s16 x0, s16 y0, s16 x1, s16 y1)
{
    return (u16)(absdiff(x1, x0) + absdiff(y1, y0));
}

/* GetDirectionAndDist: the direction from (x0, y0) to (x1, y1), and the
 * larger of |dx| and |dy|. */
static u8 dir_and_dist(s16 x0, s16 y0, s16 x1, s16 y1, u16 *dist)
{
    u16 dx = absdiff(x1, x0);
    u16 dy = absdiff(y1, y0);
    *dist = (dx >= dy) ? dx : dy;
    return get_direction(x0, y0, x1, y1);
}

/* (x, y) within -$2C..+$C of (gx, gy) both ways: the arrival box the AI
 * uses for "there" (exclusive). */
static bool in_arrival_box(s16 x, s16 y, s16 gx, s16 gy)
{
    s16 dx = (s16)(x - gx);
    s16 dy = (s16)(y - gy);
    return dx < 0x0C && dx > -0x2C && dy < 0x0C && dy > -0x2C;
}

/* -r < a - b < r, both ways. */
static bool near_point(s16 x0, s16 y0, s16 x1, s16 y1, s16 r)
{
    s16 dx = (s16)(x0 - x1);
    s16 dy = (s16)(y0 - y1);
    return dx < r && dx > -r && dy < r && dy > -r;
}

/* asr.b #1 of a byte, as a word. */
static s16 half_s8(u8 b)
{
    s8 v = (s8)b;
    return (s16)(v >= 0 ? v / 2 : -((-v + 1) / 2));
}

/* -- records (the outposts, then the two HQs) ------------------------------ */

static HzPost *rec(Cpu *c, int i)
{
    return &c->g->posts[i];
}

/* Who owns a record: POST_OWNED, with POST_TEAM2 for P2 ($82/$83, $C2/$C3). */
static u8 own_code(const Cpu *c)
{
    return (u8)(POST_OWNED | (c->team ? POST_TEAM2 : 0));
}

static bool rec_is_own(Cpu *c, int i)
{
    return (rec(c, i)->flags & (POST_OWNED | POST_TEAM2)) == own_code(c);
}

static bool rec_is_hq(Cpu *c, int i)
{
    return (rec(c, i)->flags & (POST_USED | POST_HQ)) == (POST_USED | POST_HQ);
}

static u16 hq_hp(Cpu *c, int team)
{
    const HzPost *h = rec(c, REC_HQ(team));
    return post_hq_hp(h);
}

/* AI_CheckOwnHQHP: the own HQ has lost HP since mech_ai_hq_hp. */
static bool own_hq_hit(Cpu *c)
{
    return hq_hp(c, c->team) < c->ai->hq_hp;
}

/* Units by slot in the own team's list (AI_OwnUnitPtr). */
static HzUnit *own_unit(Cpu *c, u32 slot)
{
    return unit_at(c->g, c->team, slot);
}

/* (Session 61) A victim as the 68k's AI read its code (slot + 1, + $32
 * for team 2; $64/$65 the mechs): 1 a unit's code, 2 a mech's, 0 anything
 * else (an HQ); *team its team by the code. QUIRK: codes $32 and up count
 * as team 2, so team 1's slot 49 ($32) is team 2's, and team 2's slot 49
 * ($64) is P1's mech; no victim yet (0) is a team 1 unit's code. A slot
 * past 49 (UNITS) is its own side's unit. FIX (`fixed`, HZ_FIX_CPU_HITS):
 * a unit is its own side's unit, slot 49 too (session 62: until then
 * the fix took team 2's slot 49 for P1's mech, as the code does). */
static int victim_code_kind(HzRef v, bool fixed, bool *team2)
{
    *team2 = false;
    switch (v.kind) {
    case REF_NONE:
        return 1;
    case REF_UNIT:
        if (!fixed && v.slot == HZ_SIDE_UNITS - 1) {
            if (v.side == 1) {
                return 2;                           /* $64: P1's mech */
            }
            *team2 = true;                          /* $32 */
            return 1;
        }
        *team2 = v.side != 0;
        return 1;
    case REF_MECH:
        *team2 = v.side != 0;
        return 2;
    default:
        return 0;
    }
}

/* AI_NearestOutpost: the active record not claimed by the route (mark
 * b0) nearest (x, y) by |dx| + |dy|, the last of equals; -1 if none. */
static int nearest_unclaimed(Cpu *c, s16 x, s16 y)
{
    int best = -1;
    u16 best_d = 0xFFFF;
    for (int i = 0; i < HZ_AI_RECORDS; i++) {
        if ((c->ai->marks[i] & MARK_ROUTE)
            || !(rec(c, i)->flags & POST_USED)) {
            continue;
        }
        u16 d = manhattan(x, y, rec(c, i)->x, rec(c, i)->y);
        if (d <= best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/* AI_NearestOwnOutpost: the own record (outposts and the own HQ) nearest
 * (x, y), the last of equals; -1 if none. With `skip_goal`
 * (AI_NearestOtherOwnOutpost) not the one at mech_ai_goal. `code` is the
 * owner looked for (AI_StratHQAlarm looks for the other side's). */
static int nearest_owned(Cpu *c, s16 x, s16 y, u8 code, bool skip_goal)
{
    int best = -1;
    u16 best_d = 0xFFFF;
    for (int i = 0; i < HZ_AI_RECORDS; i++) {
        const HzPost *r = rec(c, i);
        if (!(r->flags & POST_USED)
            || (r->flags & (POST_OWNED | POST_TEAM2)) != code) {
            continue;
        }
        if (skip_goal && r->x == c->ai->goal.x && r->y == c->ai->goal.y) {
            continue;
        }
        u16 d = manhattan(x, y, r->x, r->y);
        if (d <= best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

static int nearest_own_post(Cpu *c)
{
    return nearest_owned(c, c->m->x, c->m->y, own_code(c), false);
}

/* -- seeing --------------------------------------------------------------- */

/* Grid_ScanAroundMech: Grid_ScanRings round (x + 16, y + 16), the ids in
 * scan order (nearest ring first) and per kind the counts v_scan_counts
 * holds: [0] P1 mech, [1] P1 units, [2] P2 mech, [3] P2 units. (The 68k
 * keeps them in v_scan_list/v_scan_counts; nothing else reads them.) */
#define SCAN_MAX 640              /* 11 rings hold 528 cells */

typedef struct {
    HzRef ids[SCAN_MAX];
    int   n;
    u32   counts[4];
} Scan;

/* (Session 61) UNITS (crowd.c): the scan over the cells' lists, in ring
 * order, each cell's mechs then its units (P1's side's first): the
 * counts whole, but the ids only the first of each side (all that
 * first_enemy reads), so a crowd costs no more than a cell each. */
static void crowd_scan_round(const HzGame *g, s16 x, s16 y, int rings,
                             Scan *s)
{
    u16 cells[HZ_GRID * HZ_GRID];
    int n = 0;
    int row0 = (u16)y >> 5;
    int col0 = (u16)x >> 5;
    for (int r = 1; r <= rings; r++) {
        int len = 2 * r;
        for (int k = 0; k < 4 * len; k++) {
            int i = k % len + 1;
            int row = row0 - r;
            int col = col0 - r;
            switch (k / len) {
            case 0:
                col += i;
                break;
            case 1:
                row += i;
                col += len;
                break;
            case 2:
                row += len;
                col += len - i;
                break;
            default:
                row += len - i;
                break;
            }
            if (row >= 0 && row < HZ_GRID && col >= 0 && col < HZ_GRID) {
                cells[n++] = (u16)(row * HZ_GRID + col);
            }
        }
    }
    s->n = 0;
    s->counts[0] = s->counts[1] = s->counts[2] = s->counts[3] = 0;
    bool seen[2] = { false, false };
    for (int i = 0; i < n; i++) {
        for (int p = 0; p < 2; p++) {
            if (crowd_mech_in(g, p, cells[i])) {
                s->counts[p * 2]++;
                if (!seen[p]) {
                    seen[p] = true;
                    s->ids[s->n++] = ref_mech(p);
                }
            }
        }
        for (int side = 0; side < 2; side++) {
            u32 k = g->crowd_count[side][cells[i]];
            s->counts[side * 2 + 1] += k;
            if (k != 0 && !seen[side]) {
                seen[side] = true;
                s->ids[s->n++] = unit_ref(crowd_first(g, side, cells[i]));
            }
        }
    }
}

static void scan_round(const HzGame *g, s16 x, s16 y, int rings, Scan *s)
{
    if (g->crowd) {
        crowd_scan_round(g, (s16)(x + 0x10), (s16)(y + 0x10), rings, s);
        return;
    }
    s->n = grid_scan_rings(g, (s16)(x + 0x10), (s16)(y + 0x10), rings,
                           s->ids, SCAN_MAX);
    s->counts[0] = s->counts[1] = s->counts[2] = s->counts[3] = 0;
    for (int i = 0; i < s->n; i++) {
        int kind = s->ids[i].side * 2 + (s->ids[i].kind == REF_UNIT);
        s->counts[kind]++;
    }
}

/* A count into a byte the AI keeps: 255 at most (the original's never
 * passed 102). */
static u8 count_byte(u32 n)
{
    return (u8)((n > 0xFF) ? 0xFF : n);
}

/* The rings round a record AI_CountAround and AI_CountAroundDest scan: 5,
 * an HQ 8, an HQ on an ST map 10. */
static int rings_for(Cpu *c, const HzPost *r)
{
    int n = 5;
    if ((r->flags & HQ_FLAGS) == HQ_FLAGS) {
        n += 3;
        if (c->ai->st_map) {
            n += 2;
        }
    }
    return n;
}

/* AI_CountAround: own and enemy units (not mechs) round record i. */
static void count_around(Cpu *c, int i, u8 *own, u8 *enemy)
{
    Scan s;
    const HzPost *r = rec(c, i);
    scan_round(c->g, r->x, r->y, rings_for(c, r), &s);
    *own = count_byte(s.counts[c->team ? 3 : 1]);
    *enemy = count_byte(s.counts[c->team ? 1 : 3]);
}

/* The first enemy (mech or unit) in a scan, none if none (plans 5, $A). */
static HzRef first_enemy(const Cpu *c, const Scan *s)
{
    int e = c->team ? 0 : 2;
    if (s->counts[e] + s->counts[e + 1] == 0) {
        return ref_none();
    }
    for (int i = 0; i < s->n; i++) {
        if (s->ids[i].side != c->team) {
            return s->ids[i];
        }
    }
    return ref_none();
}

/* AI_GridCellOccupied: the v_grid cell under (x + 16, y + 16) has an
 * occupant. The 68k's cell number is ((y + 16) & $FFE0) x 2 + (x + 16) / 32,
 * so for y + 16 past 2047 it reads on into v_radar_buf, as here. */
static bool cell_occupied(const HzGame *g, s16 x, s16 y)
{
    s16 i = (s16)((((u16)(y + 0x10) & 0xFFE0) * 2)
                  + ((u16)(x + 0x10) >> 5));
    if (i >= 0 && i < HZ_GRID * HZ_GRID) {
        if (g->crowd) {
            return crowd_cell_taken(g, (u16)i);     /* (session 61) */
        }
        return !ref_is_none(g->grid[i]);
    }
    i = (s16)(i - HZ_GRID * HZ_GRID);
    if (i >= 0 && i < 2 * HZ_RADAR_IMAGE) {
        return g->radar_buf[i / HZ_RADAR_IMAGE][i % HZ_RADAR_IMAGE] != 0;
    }
    return false;               /* UNKNOWN: off the map, not reached */
}

/* AI_ScanBlocksInDir: from the mech's layout cell, up to 24 cells (32 px)
 * in the 8-way direction mech_ai_scan_dir (0 up, clockwise) for a block
 * whose tile at row 1, column 1 has b9 (? water: the ST-57U needs it).
 * True with the cell's top-left in px. */
static bool scan_blocks(Cpu *c, s16 *x, s16 *y)
{
    static const s8 STEP[8][2] = {      /* AIScanDirTable: column, row */
        { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
        { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }
    };
    int row = (u16)(c->m->y + 0x10) >> 5;
    int col = (u16)(c->m->x + 0x10) >> 5;
    int d = c->ai->scan_dir & 7;
    const u16 *layout = &c->g->layout[0][0];
    for (int n = 0; n < 24; n++) {
        int cell = row * HZ_LAYOUT_SIZE + col;
        if (cell >= 0 && cell < HZ_LAYOUT_SIZE * HZ_LAYOUT_SIZE) {
            u16 block = layout[cell] & 0x1FF;
            if (c->g->blocks[block][5] & TILE_WATER) { /* AI_Scan_Found */
                *x = (s16)(col << 5);
                *y = (s16)(row << 5);
                return true;
            }
        }
        row += STEP[d][1];
        col += STEP[d][0];
        if (row < 0 || row > 0x3F || col < 0 || col > 0x3F) {
            return false;                           /* AI_Scan_None */
        }
    }
    return false;
}

/* -- fuel ------------------------------------------------------------------- */

/* A cost in fuel against the tank (AI_FuelEnoughFor and its kin): enough
 * now; or a full tank would do (mech_ai_refuel = 1, still true); or not
 * even that (false). */
static bool fuel_covers(Cpu *c, u16 need)
{
    if (need < c->m->fuel) {
        return true;
    }
    if (need >= c->kidou->fuel) {
        return false;
    }
    c->ai->refuel = 1;
    return true;
}

/* AI_FuelEnoughFor: one way, d px: (d + 50) / 3. */
static bool fuel_one_way(Cpu *c, u16 d)
{
    return fuel_covers(c, (u16)((u16)(d + 0x32) / 3));
}

/* AI_FuelEnoughForTrip: there and back: (d + 50) + (d + 50) / 3. */
static bool fuel_trip(Cpu *c, u16 d)
{
    u16 k = (u16)(d + 0x32);
    return fuel_covers(c, (u16)(k / 3 + k));
}

/* AI_FuelCovers: QUIRK: as AI_FuelEnoughFor without the / 3; its callers
 * (plan $B) pass pixels, so they ask for three times the fuel. */
static bool fuel_px(Cpu *c, u16 d)
{
    return fuel_covers(c, (u16)(d + 0x32));
}

/* One way to a record if it's ours, there and back if not. */
static bool fuel_to_rec(Cpu *c, int r, u16 d)
{
    return rec_is_own(c, r) ? fuel_one_way(c, d) : fuel_trip(c, d);
}

/* -- switching plans -------------------------------------------------------- */

static void service_check(HzMechAI *ai)
{
    if (ai->needs_service) {
        ai->refuel = 0xFF;
    }
}

/* AI_SetPlan: plan `plan` at step 0, nothing to resume, pad cleared. */
static void set_plan(Cpu *c, u8 plan)
{
    HzMechAI *ai = c->ai;
    c->m->pad = 0;
    ai->course = 0;
    service_check(ai);
    ai->plan = plan;
    ai->step = 0;
    ai->saved_plan = 0;
    ai->saved_step = 0;
    ai->detour = 0;
}

/* AI_CallPlan: plan `plan` at step 0; the current plan and step resume
 * after it. */
static void call_plan(Cpu *c, u8 plan)
{
    HzMechAI *ai = c->ai;
    ai->course = 0;
    c->m->pad = 0;
    service_check(ai);
    ai->saved_plan = ai->plan;
    ai->saved_step = ai->step;
    ai->plan = plan;
    ai->step = 0;
}

/* AI_ResumePlan */
static void resume_plan(Cpu *c)
{
    HzMechAI *ai = c->ai;
    ai->plan = ai->saved_plan;
    ai->step = ai->saved_step;
    ai->saved_plan = 0;
    ai->saved_step = 0;
    c->m->pad = 0;
    service_check(ai);
    if (ai->engage) {
        ai->engage = 0x80;
    }
}

/* -- moving the pad about ------------------------------------------------- */

/* The way a stuck mech moves off: no direction held, a random one; held
 * while walking into something, one 8-way step clockwise of the heading;
 * else keep what's held (-1). */
static int unstick_dir(Cpu *c)
{
    if ((c->m->pad & HZ_PAD_DIRS) == 0) {
        return (int)(ai_random(c) & 7);
    }
    if (walk_blocked(c->m)) {
        return ((c->m->dir + 2) & 0x0F) >> 1;
    }
    return -1;
}

/* Tap A (a robot takes off) and move off as unstick_dir says. */
static void toggle_a_and_unstick(Cpu *c)
{
    c->m->pad ^= HZ_PAD_A;
    int d = unstick_dir(c);
    if (d >= 0) {
        steer_pad(c->m, d);
    }
}

/* AI_TurnToward: heading (and mech_turn_to) set straight to `dir`.
 * QUIRK: the original works out a +/-1 step and never uses it, so the
 * mech turns at once (here and in move state 7). */
static void face(HzMech *m, u8 dir)
{
    dir &= 0x0F;
    if (dir != m->dir) {
        m->dir = dir;
        m->turn_to = dir;
    }
}

/* A course (mech_ai_course) towards (x, y), kept when the pad already
 * holds a direction, the goal is $80 px or more away and the heading is
 * within `slack` of the way there (|direction - heading|, not wrapped).
 * `dist` is the caller's distance. True if it was set. */
static bool course_to(Cpu *c, u8 dir, u16 dist, int slack, bool far_inclusive)
{
    HzMech *m = c->m;
    bool far = far_inclusive ? (dist > 0x80) : (dist >= 0x80);
    if ((m->pad & HZ_PAD_DIRS) != 0 && far) {
        u8 diff = (u8)(dir - m->dir);
        if ((s8)diff < 0) {
            diff = (u8)-diff;
        }
        if (diff < slack) {
            return false;
        }
    }
    c->ai->course = dir8_pad((dir & 0x0F) >> 1);
    return true;
}

/* -- strategy and modes: forward ------------------------------------------ */

static void run_plan(Cpu *c);
static void keep_off_edges(Cpu *c);

/* ===================================================================== */
/* The move states (AIMoveStateTable, $A646), one run every frame.        */
/* ===================================================================== */

/* 0 AI_Move_CheckSupplies: a service trip asked for (mech_ai_refuel):
 * start it (state 2), keeping the goal. Else mech_ai_needs_service = $FF
 * on fuel under 1/4, HP under 1/4 or ammo under 1/8 of KIDOU's (carrying:
 * fuel only; once an alert is taken, no HP test). Then state 1. */
static MoveResult move_check_supplies(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (m->flags & (MECH_MENU_OPEN | MECH_DESTROYED)) {
        ai->move++;
        return RUN_PLAN;
    }
    if (ai->refuel) {                                   /* .go_service */
        ai->refuel = 0;
        ai->needs_service = 0;
        m->pad = 0;
        ai->move = 2;
        ai->saved_goal = ai->goal;
        return SKIP_PLAN;
    }
    bool low = (u16)(c->kidou->fuel >> 2) > m->fuel;
    if (!low && !carrying(m)) {
        if (ai->alert_taken == 0 && (u16)(c->kidou->hp >> 2) > m->hp) {
            low = true;
        } else if ((u8)(c->kidou->ammo >> 3) > m->ammo) {   /* .check_ammo */
            low = true;
        }
    }
    ai->needs_service = low ? 0xFF : 0;
    ai->move++;
    return RUN_PLAN;
}

/* 1 AI_Move_WatchEnemyMech: the enemy mech alive, within
 * mech_ai_engage_range, in the same form (both robots, or both in the
 * air), with ammo, and no plan 5: with no plan running, or a fight
 * allowed (mech_ai_engage $80), fight (state 6, plan skipped); else note
 * it came in range (mech_ai_engage $FF). A fight is allowed with no plan
 * running (not carrying) or when a walk ran into an object. Then state 9. */
static MoveResult move_watch_enemy_mech(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    bool fight = false;
    bool in_range = false;
    if (!destroyed(c->rival) && !(m->flags & (MECH_MENU_OPEN | MECH_DESTROYED))
        && ai->plan != PLAN_GROUND) {
        if ((ai->plan == PLAN_MODES && !carrying(m))    /* .check_engage */
            || (m->blocked & PROBE_OBJECT)) {
            ai->engage = 0x80;
        }
        bool forms = robot(m) ? robot(c->rival) : !robot(c->rival);
        if (m->ammo != 0 && forms) {                    /* .check_forms */
            u16 dist;
            dir_and_dist(m->x, m->y, c->rival->x, c->rival->y, &dist);
            in_range = dist <= ai->engage_range;
        }
        if (in_range) {
            fight = ai->plan == PLAN_MODES || ai->engage == 0x80;
        }
    }
    if (fight) {                                        /* .fight */
        ai->move = 6;
        ai->engage = 0;
        ai->shot_wait = 0;
        return SKIP_PLAN;
    }
    ai->move = 9;
    ai->engage = in_range ? 0xFF : 0;                   /* .no_fight */
    return RUN_PLAN;
}

/* 9 AI_Move_CheckHQHit: the own HQ lost HP, no negative alert up: alert
 * $81. Then state 10. */
static MoveResult move_check_hq_hit(Cpu *c)
{
    HzMechAI *ai = c->ai;
    if (!(ai->alert & 0x80) && own_hq_hit(c)) {
        ai->alert_taken = 0;
        ai->alert = ALERT_HQ_HIT;
    }
    ai->move = 0x0A;
    return RUN_PLAN;
}

/* 10 AI_Move_CheckHQGuards: enemy units round the own HQ, or none of
 * ours: alert $80 (not with a negative alert up, nor when the HQ is the
 * dest); guarded again: an untaken $80 withdrawn. Then state 13. */
static MoveResult move_check_hq_guards(Cpu *c)
{
    HzMechAI *ai = c->ai;
    u8 own, enemy;
    count_around(c, REC_HQ(c->team), &own, &enemy);
    if (enemy != 0 || own == 0) {                       /* .short_of_guards */
        if (!(ai->alert & 0x80) && ai->dest != REC_HQ(c->team)) {
            ai->alert_taken = 0;
            ai->alert = ALERT_HQ_GUARDS;
        }
    } else if (ai->alert_taken == 0 && ai->alert == ALERT_HQ_GUARDS) {
        ai->alert = 0;                                  /* .guarded */
    }
    ai->move = 0x0D;
    return RUN_PLAN;
}

/* 13 AI_Move_WatchLaunchPosts: one outpost a visit (mech_ai_watch_post):
 * a launch outpost of the other side's (its AILaunchPosts) where it holds
 * some of the hold or has units near, not the dest, no HQ run under way:
 * alert 1 there. Nothing while an alert is up. Then state 14. */
static MoveResult move_watch_launch_posts(Cpu *c)
{
    HzMechAI *ai = c->ai;
    if (!(ai->alert & 0x80) && ai->alert != ALERT_LAUNCH_POST) {
        int i = ai->watch_post;
        const u8 (*posts)[16] = c->team ? AI_LAUNCH_POSTS_P1
                                        : AI_LAUNCH_POSTS_P2;
        if (posts[c->g->map & 7][i & 0x0F]) {
            const HzPost *r = rec(c, i);
            bool there = r->hold[c->team ^ 1] != 0;     /* the other's */
            if (!there) {                               /* .count_enemies */
                u8 own, enemy;
                count_around(c, i, &own, &enemy);
                there = enemy != 0;
            }
            if (there && ai->dest != i && ai->hq_run == 0) {
                ai->alert_post = ai->watch_post;        /* .enemy_there */
                ai->alert_taken = 0;
                ai->alert = ALERT_LAUNCH_POST;
            }
        }
        ai->watch_post = (u8)((ai->watch_post + 1) & 0x0F);   /* .next_post */
    }
    ai->move = 0x0E;
    return RUN_PLAN;
}

/* 14, 15 AI_Move_DodgeMissiles: a jet with enemy missiles homing in
 * (mech_missile_dirs; the lowest direction bit) from ahead turns away, 90
 * degrees (0-2 steps right of the heading, or 1-2 left) or 45 (3-4 either
 * side), and holds that 16 frames (mech_ai_dodge), the plan skipped and
 * AI_KeepOffEdges still run. Then state 0.
 * QUIRK: the new pad is set once and the round of states runs on (the
 * plan included, which can change the pad) before the hold starts. */
static MoveResult move_dodge_missiles(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (!(m->flags & (MECH_MENU_OPEN | MECH_DESTROYED)) && m->missile_dirs != 0
        && m->form == HZ_FORM_JET) {
        int from = 0;
        while (!(m->missile_dirs & (1u << from))) {     /* .find_missile */
            from++;
        }
        int rel = (from - m->dir) & 0x0F;               /* .found */
        int turn = 0;
        if (rel <= 2) {
            turn = -4;
        } else if (rel <= 4) {
            turn = -2;
        } else if (rel >= 0x0E) {
            turn = 4;
        } else if (rel >= 0x0C) {
            turn = 2;
        }
        if (turn != 0 && ai->dodge == 0) {              /* .ahead */
            steer_pad(m, ((m->dir + turn) & 0x0F) >> 1);
            ai->move = 0;
            ai->dodge = 0x10;
            return EDGES_ONLY;
        }
        if (ai->dodge != 0) {                           /* .dodging */
            ai->dodge--;
            return EDGES_ONLY;
        }
    }
    ai->move = 0;                                       /* .done */
    ai->dodge = 0;
    return RUN_PLAN;
}

/* 2 AI_Move_ToNearestPost: the goal is the nearest own outpost (or the
 * own HQ), no heading after; state 3. None: stay. Skips the plan. */
static MoveResult move_to_nearest_post(Cpu *c)
{
    int r = nearest_own_post(c);
    if (r >= 0) {
        c->ai->goal.x = rec(c, r)->x;
        c->ai->goal.y = rec(c, r)->y;
        c->ai->heading = 0xFF;
        c->ai->move++;
    }
    return SKIP_PLAN;
}

/* 12 AI_Move_ToOtherPost (nothing sets it): the nearest own outpost but
 * the one at the goal, state 3; none: state 7. Skips the plan. */
static MoveResult move_to_other_post(Cpu *c)
{
    int r = nearest_owned(c, c->m->x, c->m->y, own_code(c), true);
    if (r < 0) {
        c->ai->move = 7;
        return SKIP_PLAN;
    }
    c->ai->goal.x = rec(c, r)->x;
    c->ai->goal.y = rec(c, r)->y;
    c->ai->heading = 0xFF;
    c->ai->move = 3;
    return SKIP_PLAN;
}

/* 3 AI_Move_TakeOff: a robot taps A and walks off; a jet clears the pad
 * and calls plan 9 (fly to the goal), state 4. Skips the plan. */
static MoveResult move_take_off(Cpu *c)
{
    if (!robot(c->m)) {
        c->m->pad = 0;
        c->ai->move++;
        call_plan(c, PLAN_FLY_SERVICE);
        return SKIP_PLAN;
    }
    toggle_a_and_unstick(c);                            /* .robot */
    return SKIP_PLAN;
}

static MoveResult move_check_serviced(Cpu *c);

/* 4 AI_Move_WaitArrival: plan 9 still flying: wait; then state 5. */
static MoveResult move_wait_arrival(Cpu *c)
{
    if (c->ai->plan == PLAN_FLY_SERVICE) {
        return RUN_PLAN;
    }
    c->ai->move++;
    return move_check_serviced(c);
}

/* 5 AI_Move_CheckServiced: fuel, HP and ammo full (carrying: fuel; with
 * a negative alert up: fuel and ammo over half): state 0, the goal back,
 * plan 4 restarted. Else state 7, plan skipped. */
static MoveResult move_check_serviced(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    const HzUnitStats *k = c->kidou;
    bool done;
    if (ai->alert & 0x80) {
        done = (u16)(k->fuel >> 1) < m->fuel && (u8)(k->ammo >> 1) < m->ammo;
    } else {                                            /* .need_full */
        done = k->fuel == m->fuel
            && (carrying(m) || (k->hp == m->hp && k->ammo == m->ammo));
    }
    if (!done) {                                        /* .not_yet */
        ai->move = 7;
        return SKIP_PLAN;
    }
    ai->move = 0;                                       /* .serviced */
    ai->goal = ai->saved_goal;
    if (ai->plan == PLAN_FLY) {
        ai->step = 0;
    }
    return RUN_PLAN;
}

/* The wobble state 7 and the fight add to their aim: 0, +1, 0, -1 by
 * mech_ai_frames & $3F, $10 frames each. */
static int aim_wobble(u16 frames)
{
    int f = frames & 0x3F;
    if (f < 0x10) {
        return 0;
    }
    if (f < 0x20) {
        return 1;
    }
    if (f < 0x30) {
        return 0;
    }
    return -1;
}

/* 7 AI_Move_GuardPost (on an own outpost for service): the enemy mech a
 * jet within range: face it (with the wobble) and tap B every 4th frame;
 * else release B. State 8. Skips the plan. */
static MoveResult move_guard_post(Cpu *c)
{
    HzMech *m = c->m;
    u16 dist = 0;
    u8 dir = 0;
    if (!destroyed(c->rival)) {
        dir = dir_and_dist(m->x, m->y, c->rival->x, c->rival->y, &dist);
    }
    if (destroyed(c->rival) || dist > c->ai->engage_range
        || robot(c->rival)) {
        m->pad &= (u8)~HZ_PAD_B;                        /* .release_b */
    } else {
        face(m, (u8)(dir + aim_wobble(c->ai->frames)));     /* .aim */
        if ((c->ai->frames & 3) == 0) {                 /* .fire */
            m->pad ^= HZ_PAD_B;
        }
    }
    c->ai->move++;
    return SKIP_PLAN;
}

/* 8 AI_Move_CheckOnPost: in the arrival box of the nearest own outpost:
 * state 5. None left: the goal back, state 1. The enemy mech a jet in
 * range with ammo: the goal back, fight (6). Else back to it (2). Always
 * skips the plan. */
static MoveResult move_check_on_post(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    int r = nearest_own_post(c);
    if (r < 0) {                                        /* .no_post */
        ai->goal = ai->saved_goal;
        ai->move = 1;
        return SKIP_PLAN;
    }
    if (in_arrival_box(m->x, m->y, rec(c, r)->x, rec(c, r)->y)) {
        ai->move = 5;
        return SKIP_PLAN;
    }
    u16 dist;                                           /* .off_post */
    dir_and_dist(m->x, m->y, c->rival->x, c->rival->y, &dist);
    if (dist <= ai->engage_range && !robot(c->rival) && c->rival->ammo != 0) {
        ai->goal = ai->saved_goal;
        ai->move = 6;
        return SKIP_PLAN;
    }
    ai->move = 2;                                       /* .back_to_post */
    return SKIP_PLAN;
}

/* 6 AI_Move_Fight, the way out (.leave): first back into the form the
 * fight started in (mech_ai_fight_form), tapping A each visit; then the
 * pad cleared, the countdown cleared (no negative alert up), plan 4
 * restarted, state 0. The plan runs that frame. */
static MoveResult fight_leave(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    bool back = true;
    if (ai->fight_form != 0) {
        bool was_robot = ai->fight_form == 1;
        back = was_robot ? robot(m) : !robot(m);
    }
    if (ai->fight_form != 0 && !back) {                 /* .change_back */
        m->pad ^= HZ_PAD_A;
        int d = -1;
        if ((m->pad & HZ_PAD_DIRS) == 0) {
            d = (int)(ai_random(c) & 7);
        } else if (carrying(m)) {                       /* .change_back_dir */
            if ((ai->frames & 0x1F) == 0) {
                d = (int)(ai_random(c) & 7);
            }
        } else if (walk_blocked(m)) {                   /* .change_back_blocked */
            d = ((m->dir + 2) & 0x0F) >> 1;
        }
        if (d >= 0) {
            steer_pad(m, d);
        }
        return SKIP_PLAN;
    }
    if (ai->fight_form != 0) {                          /* .end_fight */
        ai->fight_form = 0;
        m->pad = 0;
        if (!(ai->alert & 0x80)) {
            ai->countdown = 0;
        }
        if (ai->plan == PLAN_FLY) {
            ai->step = 0;
        }
    }
    ai->hold = 0;                                       /* .to_state0 */
    ai->move = 0;
    return RUN_PLAN;
}

/* The pad a fight state sets (.store_pad): the first time, mech_ai_
 * fight_form = mech_form + 1. Skips the plan. */
static MoveResult fight_store(Cpu *c, u8 pad)
{
    c->m->pad = pad;
    if (c->ai->fight_form == 0) {
        c->ai->fight_form = (u8)(c->m->form + 1);
    }
    return SKIP_PLAN;
}

/* A fight's break-off (.break_off_move): A kept, and a direction for 16
 * frames (none held: random; walking into something: one step
 * clockwise). */
static MoveResult fight_break_off(Cpu *c)
{
    HzMech *m = c->m;
    m->pad |= HZ_PAD_A;
    u8 pad = m->pad;
    int d = unstick_dir(c);
    if (d >= 0) {
        pad = (u8)((pad & HZ_PAD_BUTTONS) | dir8_pad(d));
    }
    c->ai->hold = 0x10;
    return fight_store(c, pad);
}

/* 6 AI_Move_Fight: take on the enemy mech. The own HQ hit: alert $EE and
 * nothing else. It leaves (fight_leave) when the enemy is destroyed, or
 * on no ammo, fuel under 1/4, HP under $42 (not once an alert is taken),
 * or the enemy out of range. While transforming or holding (mech_ai_hold)
 * it keeps the pad. In different forms it taps A to match (a jet lands
 * only where the cell below is free; else a random direction for 16
 * frames); in the same form it steers at the enemy: under $40 px it faces
 * it, else presses towards it (a jet with the wobble), and on frames with
 * frames & 3 = 3 and the heading within 1 of it fires: a jet taps B; a
 * robot exactly facing fires one shot and then reads its result: an
 * enemy hit fires again, anything else breaks off for 16 frames (as does a
 * robot walking into something). Skips the plan. */
static MoveResult move_fight(Cpu *c)
{
    HzMech *m = c->m;
    HzMech *e = c->rival;
    HzMechAI *ai = c->ai;
    const HzUnitStats *k = c->kidou;

    if (own_hq_hit(c) && !(ai->alert_taken & 0x80)) {
        ai->alert_taken = 0;
        ai->alert = ALERT_HQ_HIT_FIGHT;
        return SKIP_PLAN;
    }
    if (destroyed(e) || m->ammo == 0 || (u16)(k->fuel >> 2) > m->fuel
        || (ai->alert_taken == 0 && m->hp < 0x42)) {    /* .check_leave */
        return fight_leave(c);
    }
    u16 dist;                                           /* .check_range */
    u8 dir = dir_and_dist(m->x, m->y, e->x, e->y, &dist);
    if (dist > ai->engage_range) {
        return fight_leave(c);
    }
    if (m->transform != 0) {
        return SKIP_PLAN;
    }
    if (ai->hold != 0) {                                /* .check_hold */
        ai->hold--;
        return SKIP_PLAN;
    }

    bool own_robot = robot(m);                          /* .match_forms */
    bool foe_robot = robot(e);
    if (carrying(m) && foe_robot) {                     /* .own_carrying */
        return fight_leave(c);
    }
    if (own_robot != foe_robot) {                       /* .forms_differ */
        if (m->pad & HZ_PAD_A) {                        /* .release_a */
            u8 pad = m->pad;
            if (!own_robot) {
                /* QUIRK: the original's random sign test (after a swap)
                 * sees the whole long, so the turn is always 0 to -3. */
                int d = (m->dir - (int)(ai_random(c) & 3)) & 0x0F;
                pad = (u8)((pad & HZ_PAD_BUTTONS & ~HZ_PAD_A)
                           | dir8_pad(d >> 1));
            } else {                                    /* .robot_release_a */
                int d = unstick_dir(c);
                if (d >= 0) {
                    pad = (u8)((pad & HZ_PAD_BUTTONS & ~HZ_PAD_A)
                               | dir8_pad(d));
                }
            }
            ai->hold = 0x10;
            return fight_store(c, pad);
        }
        if (!own_robot && cell_occupied(c->g, m->x, m->y)) {  /* .cant_land */
            ai->hold = 0x10;
            return fight_store(c, dir8_pad((int)(ai_random(c) & 7)));
        }
        m->pad |= HZ_PAD_A;                             /* take off / land */
    } else {                                            /* .forms_match */
        m->pad &= (u8)~HZ_PAD_A;
        ai->hold = 0;

        int f = ai->frames & 7;                         /* .fire_timing */
        /* .check_facing: QUIRK: heading - direction isn't wrapped, so 0
         * against $F counts as not facing */
        s8 off = (s8)(m->dir - dir);
        if ((f == 3 || f == 7) && m->transform == 0 && off >= -1 && off <= 1) {
            if (m->form != HZ_FORM_ROBOT) {
                ai->shot_wait = 0;                      /* .jet_fire */
                m->pad ^= HZ_PAD_B;
            } else if (off == 0) {
                if (ai->shot_wait == 0) {
                    ai->shot_wait = 0xFF;
                    m->pad ^= HZ_PAD_B;                 /* .tap_b */
                } else {                                /* .shot_result */
                    m->pad &= (u8)~(HZ_PAD_A | HZ_PAD_B);
                    if (!(m->shot_flags & OWN_SHOT_OUT)) {
                        ai->shot_wait = 0;
                        bool hit = (m->shot_flags & OWN_SHOT_HIT) != 0;
                        m->shot_flags &= (u8)~OWN_SHOT_HIT;
                        /* QUIRK: victim_code_kind's teams, unfixed */
                        bool v2;
                        int vk = victim_code_kind(m->hit_victim, false, &v2);
                        int vteam = (vk == 0) ? -1 : (v2 ? 1 : 0);
                        if (!hit || vteam < 0 || vteam == c->team) {
                            return fight_break_off(c);  /* .break_off */
                        }
                        m->pad ^= HZ_PAD_B;
                    }
                }
            }
        }
    }

    /* .steer: QUIRK: mech_ai_hold is always 0 here (both ways in set it
     * and return), so its countdown in the original is never taken */
    u8 pad = m->pad & HZ_PAD_BUTTONS;                   /* .set_pad */
    if (!robot(m)) {                                    /* .aim */
        dir = (u8)(dir + aim_wobble(ai->frames));
    }
    if (dist < 0x40) {
        face(m, dir);
        return fight_store(c, pad);
    }
    if (robot(m) && (m->pad & HZ_PAD_DIRS) != 0         /* .robot_far */
        && walk_blocked(m)) {
        return fight_break_off(c);
    }
    return fight_store(c, (u8)(pad | dir8_pad((dir & 0x0F) >> 1)));
}

static MoveResult run_move_state(Cpu *c)
{
    switch (c->ai->move) {
    case 0x0:
        return move_check_supplies(c);
    case 0x1:
        return move_watch_enemy_mech(c);
    case 0x2:
        return move_to_nearest_post(c);
    case 0x3:
        return move_take_off(c);
    case 0x4:
        return move_wait_arrival(c);
    case 0x5:
        return move_check_serviced(c);
    case 0x6:
        return move_fight(c);
    case 0x7:
        return move_guard_post(c);
    case 0x8:
        return move_check_on_post(c);
    case 0x9:
        return move_check_hq_hit(c);
    case 0xA:
        return move_check_hq_guards(c);
    case 0xB:
        return RUN_PLAN;                                /* AI_Move_Idle */
    case 0xC:
        return move_to_other_post(c);
    case 0xD:
        return move_watch_launch_posts(c);
    case 0xE:
    case 0xF:
        return move_dodge_missiles(c);
    default:
        return RUN_PLAN;    /* UNKNOWN: no state past $F is ever set */
    }
}

/* ===================================================================== */
/* The strategy (plan 0, mode 1: AIStrategyTable, $B1A0).                 */
/* ===================================================================== */

/* AI_ClearMarks: every record's route claim (mark b0) cleared. */
static void clear_route_marks(HzMechAI *ai)
{
    for (int i = 0; i < HZ_AI_RECORDS; i++) {
        ai->marks[i] &= (u8)~MARK_ROUTE;
    }
}

static void enter_reset_mode(Cpu *c)                /* AI_EnterResetMode */
{
    c->ai->substep = 0;
    c->ai->mode = 4;
    c->ai->reset_count++;
}

static void enter_buy_mode(Cpu *c)                  /* AI_EnterBuyMode */
{
    c->ai->substep = 0;
    c->ai->mode = 2;
}

static void enter_route_mode(Cpu *c)                /* AI_EnterRouteMode */
{
    c->ai->route = 0xFF;
    c->ai->substep = 0;
    c->ai->mode = 3;
}

/* AI_SendToDest / AI_SendToFrom: plan $C to the record, then plan $11
 * (look over the own units) when it gets there. The pad is left. */
static void send_to(Cpu *c, int r)
{
    HzMechAI *ai = c->ai;
    ai->attack_buy = 0;
    ai->course = 0;
    ai->goal.x = rec(c, r)->x;
    ai->goal.y = rec(c, r)->y;
    ai->step = 0;
    ai->plan = PLAN_FLY_RESUME;
    ai->saved_plan = PLAN_TEND_UNITS;
    ai->saved_step = 0;
    ai->detour = 0;
}

/* AI_BuyAtHQ / AI_BuyAtPost (and their _Attack entries): set buy mode up
 * to buy `count` units of `kind` (substep 2) -- unless the own side has
 * 50 units already. QUIRK (a cheat, kept): first the money is raised to
 * 200 (HQ) or 150 if it's below. True when buying: the 68k then returns
 * from its caller too, so the callers return at once. */
static bool buy_at(Cpu *c, bool at_hq, bool attack, u8 count, u8 kind)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    ai->attack_buy = attack ? 1 : 0;
    u16 floor = at_hq ? 200 : 150;
    if (m->money < floor) {
        m->money = floor;
    }
    u32 units = c->g->unit_count[c->team];
    if (units != 0 && units >= HZ_SIDE_UNITS            /* .at_limit */
        && !units_unlimited(c->g)) {
        return false;
    }
    ai->buy_count = count;                              /* .set_up */
    if (at_hq) {
        ai->hq_drop &= 7;
        ai->hq_drop_end = (u8)(ai->hq_drop + 8);
    } else {
        ai->post_drop &= 7;
        ai->post_drop_end = (u8)(ai->post_drop + 8);
    }
    ai->buy_kind = kind;
    ai->substep = 2;
    ai->mode = 2;
    return true;
}

/* AI_CountAroundDest: own and enemy units round the dest. */
static void count_around_dest(Cpu *c)
{
    count_around(c, c->ai->dest, &c->ai->own_near, &c->ai->enemy_near);
}

/* The alert-1 alert cleared, if it is the one taken (several steps). */
static void drop_launch_alert(HzMechAI *ai)
{
    if (ai->alert_taken != 0 && ai->alert == ALERT_LAUNCH_POST) {
        ai->alert_taken = 0;
        ai->alert = 0;
    }
}

/* 6 AI_StratAtDest: launching from the dest (mech_ai_launch_at_dest): a
 * kind-7 unit, up to 10. Then by what the dest is: 7 neutral outpost, 8/9
 * P1's/P2's outpost, 10/11 P1's/P2's HQ (else 1); a new class clears the
 * count bought. */
static void strat_at_dest(Cpu *c)
{
    HzMechAI *ai = c->ai;
    if (ai->launch_at_dest) {
        if (ai->launches < 10) {                        /* .launch_buy */
            ai->buy_variant &= 1;
            ai->launches++;
            if (!buy_at(c, false, true, 1, KIND_LAUNCH)) {
                ai->launches--;
            }
            return;
        }
        ai->launch_at_dest = 0;
        ai->bought = 0;
        ai->reset_count = 0;
    }
    u8 cls;                                             /* .classify */
    switch (rec(c, ai->dest)->flags) {
    case 0x80:
        cls = 7;
        break;
    case 0x82:
        cls = 8;
        break;
    case 0x83:
        cls = 9;
        break;
    case 0xC2:
        cls = 0x0A;
        break;
    case 0xC3:
        cls = 0x0B;
        break;
    default:
        ai->strategy = 1;
        return;
    }
    ai->strategy = cls;                                 /* .set_class */
    if (cls != ai->dest_class) {
        ai->bought = 0;
        ai->reset_count = 0;
        ai->dest_class = cls;
    }
}

static void strat_team_post(Cpu *c);

/* 7 AI_StratNeutralPost: a launch point, or enemies near: as an enemy
 * outpost. An HQ run under way: on to the next. Else capture it (kind 1):
 * 4 at once, then one every $78 visits ($F0 after the third), up to 8;
 * given up (mark b5) or held by us (hold 4): the next destination. */
static void strat_neutral_post(Cpu *c)
{
    HzMechAI *ai = c->ai;
    u8 *mark = &ai->marks[ai->dest];
    if (!(*mark & MARK_LAUNCH)) {
        if (ai->hq_run) {
            ai->strategy = 3;
            return;
        }
        if (ai->enemy_near == 0) {                      /* .capture */
            bool abandoned = (*mark & MARK_ABANDONED) != 0;
            *mark &= (u8)~MARK_ABANDONED;
            if (abandoned) {
                ai->strategy = 3;
                return;
            }
            ai->strategy = 5;
            const HzPost *r = rec(c, ai->dest);
            u8 hold = r->hold[c->team];                 /* .own_hold */
            if (hold >= 4) {
                ai->strategy = 3;
                return;
            }
            u8 n = ai->bought;
            if (n == 3) {
                ai->countdown = 0xF0;
            }
            if (n >= 4) {                               /* .pace_buys */
                if (n >= 8) {
                    ai->strategy = 3;
                    return;
                }
                if (ai->countdown != 0) {
                    ai->countdown--;
                    return;
                }
                ai->countdown = 0x78;
            }
            /* .buy_capture: (the bcs after the call is never taken) */
            if (!buy_at(c, false, false, 1, KIND_CAPTURE)) {
                ai->strategy = 3;
            }
            return;
        }
    }
    ai->strategy = c->team ? 8 : 9;                     /* .contested */
    strat_team_post(c);
}

/* QUIRK: what AI_StratOwnPost.enemies_near reads as "the record is one
 * of our launch outposts": its table pointer is worked out from the mark
 * byte's address against the wrong mech's marks, so P1 reads code bytes
 * at $C330 + map x 16 + outpost and P2 AIBuyScriptTable's own bytes at
 * $C630 + map x 16 + outpost. Non-zero: it stays to defend. */
static u8 stray_launch_byte(const Cpu *c, int i)
{
    int map = c->g->map & 7;
    if (c->team == 0) {
        return AI_STRAY_LAUNCH_P1[map][i & 0x0F];
    }
    int k = map * 16 + (i & 0x0F);
    u32 v = AI_BUY_SCRIPT_TABLE[k / 32][(k / 4) % 8];
    return (u8)(v >> (8 * (3 - (k & 3))));
}

/* AI_StratOwnPost, enemies near it: defend (kind 2) if the stray table
 * says so or under 10 bought; else give it up and go on. */
static void own_post_enemies_near(Cpu *c, u8 *mark)
{
    HzMechAI *ai = c->ai;
    bool defend = stray_launch_byte(c, ai->dest) != 0;
    if (!defend) {
        *mark &= (u8)~MARK_VISITED;
        bool abandoned = (*mark & MARK_ABANDONED) != 0;
        *mark &= (u8)~MARK_ABANDONED;
        bool give_up = false;
        if (abandoned) {
            if (*mark & MARK_LAUNCH) {
                give_up = true;
            } else {
                *mark &= (u8)~MARK_VISITED;
            }
        } else if (ai->bought < 10) {                   /* .check_bought */
            defend = true;
        } else {
            give_up = true;
        }
        if (!defend) {
            if (give_up) {                              /* .give_up_launch */
                *mark &= (u8)~(MARK_LAUNCH | MARK_VISITED);
                if (ai->hq_run != 0xFF) {
                    ai->hq_run = 0;
                }
            }
            drop_launch_alert(ai);                      /* .leave */
            ai->strategy = 3;
            return;
        }
    }
    if (buy_at(c, false, false, 1, KIND_DEFEND)) {      /* .defend */
        return;
    }
    enter_reset_mode(c);
}

/* AI_StratOwnPost: from = dest, marked visited. An enemy hold on it:
 * capture back (kind 1). Enemies near: own_post_enemies_near. Else guards
 * up to mech_ai_style (kind 0); full with style 2+: a SUPPLY once (kind
 * 4); then a raid (step 12) or, from a launch point, the enemy HQ (step
 * 19); launching from here (mech_ai_launch_at_from): kind 7 up to 10;
 * an escort (kind 5) every other visit. Then AI_SendToDest. */
static void strat_own_post(Cpu *c, u8 *mark)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    (void)m;
    ai->from = ai->dest;
    *mark |= MARK_VISITED;
    drop_launch_alert(ai);
    if (*mark & MARK_LAUNCH) {
        *mark |= MARK_HQ_SUPPLY | MARK_POST_SUPPLY | MARK_POST_ESCORT;
    } else if (ai->hq_run) {                            /* .not_launch_point */
        ai->strategy = 0x10;
        return;
    }
    const HzPost *r = rec(c, ai->dest);                 /* .check_enemy_hold */
    if (r->hold[c->team ^ 1] != 0) {
        buy_at(c, false, false, 1, KIND_CAPTURE);
        return;
    }
    if (ai->enemy_near != 0) {                          /* .no_enemy_hold */
        own_post_enemies_near(c, mark);
        return;
    }
    *mark &= (u8)~MARK_ABANDONED;
    /* `left`: the byte the original has in d0 at .maybe_escort: what the
     * guards' sum left (style - own units near) or the launch count. */
    s8 left = 0;
    bool need = false;
    if (ai->style != 0) {
        left = (s8)(ai->style - ai->own_near);
        if (left == 0) {                                /* .guards_full */
            if (ai->style >= 2) {
                ai->strategy = 0x0C;
                bool had = (*mark & MARK_POST_SUPPLY) != 0;
                *mark |= MARK_POST_SUPPLY;
                if (!had && buy_at(c, false, false, 1, KIND_SUPPLY)) {
                    return;
                }
                send_to(c, ai->dest);                   /* .go */
                return;
            }
        } else if ((u8)ai->style > ai->own_near) {
            need = true;
        }
    }
    if (need) {
        if (buy_at(c, false, false, 1, KIND_GUARDS)) {
            return;
        }
        send_to(c, ai->dest);
        return;
    }
    ai->strategy = 0x0C;                                /* .no_guards_needed */
    if (ai->launch_at_from) {
        if (ai->launches < 10) {                        /* .launch_buy */
            ai->strategy = 5;
            ai->buy_variant &= 1;
            ai->launches++;
            if (buy_at(c, false, false, 1, KIND_LAUNCH)) {
                return;
            }
            ai->launches--;
            send_to(c, ai->dest);
            return;
        }
        ai->launch_at_from = 0;
        left = (s8)ai->launches;
    }
    if (*mark & MARK_LAUNCH) {                          /* .check_launch_point */
        ai->strategy = 0x13;
    }
    if (left < -1) {                                    /* .maybe_escort */
        send_to(c, ai->dest);
        return;
    }
    bool had = (*mark & MARK_POST_ESCORT) != 0;
    *mark |= MARK_POST_ESCORT;
    if (had && buy_at(c, false, false, 1, KIND_ESCORT)) {
        return;
    }
    send_to(c, ai->dest);                               /* .go_escort */
}

/* AI_StratEnemyPost: a launch outpost of ours to be, or under 15 bought:
 * attack -- capture (kind 1), or outnumbered an assault (kind 6), and at
 * 6 bought fall back to from -- then reset mode. Else give it up. */
static void strat_enemy_post(Cpu *c, u8 *mark)
{
    HzMechAI *ai = c->ai;
    const u8 (*posts)[16] = c->team ? AI_LAUNCH_POSTS_P2 : AI_LAUNCH_POSTS_P1;
    bool attack = posts[c->g->map & 7][ai->dest & 0x0F] != 0;
    bool give_up = false;
    if (!attack) {
        bool abandoned = (*mark & MARK_ABANDONED) != 0;
        *mark &= (u8)~MARK_ABANDONED;
        bool leave = false;
        if (abandoned) {
            if (*mark & MARK_LAUNCH) {
                give_up = true;
            } else {
                *mark &= (u8)~MARK_VISITED;
                leave = true;
            }
        } else if (!(*mark & MARK_LAUNCH) && ai->hq_run) {  /* .not_abandoned */
            leave = true;
        }
        if (!leave && !give_up) {
            if (ai->bought < 15) {                      /* .check_bought */
                attack = true;
            } else {
                give_up = true;
            }
        }
        if (!attack) {
            if (give_up) {                              /* .give_up_launch */
                *mark &= (u8)~(MARK_LAUNCH | MARK_VISITED);
                if (ai->hq_run != 0xFF) {
                    ai->hq_run = 0;
                }
            }
            drop_launch_alert(ai);                      /* .leave */
            ai->strategy = 3;
            return;
        }
    }
    u8 kind = KIND_CAPTURE;                             /* .attack */
    if (ai->enemy_near == 0) {
        ai->reset_count = 0;
    } else if (ai->enemy_near > ai->own_near) {         /* .outnumbered */
        if (ai->bought == 6) {
            ai->bought = 7;
            send_to(c, ai->from);
            return;
        }
        kind = KIND_ASSAULT;
    }
    if (buy_at(c, false, true, 1, kind)) {              /* .buy */
        return;
    }
    enter_reset_mode(c);
}

/* 8/9 AI_StratTeamPost: P1's/P2's outpost: ours or theirs. */
static void strat_team_post(Cpu *c)
{
    HzMechAI *ai = c->ai;
    u8 *mark = &ai->marks[ai->dest];
    bool p1_post = ai->strategy == 8;
    ai->strategy = 5;
    bool ours = p1_post ? c->team == 0 : c->team == 1;
    if (ours) {
        strat_own_post(c, mark);
    } else {
        strat_enemy_post(c, mark);
    }
}

/* AI_HQDefenceTick: step 12; while an HQ defence runs (alert taken $FF)
 * count down, holding at the HQ; at 0 end it: the HQ's HP taken again, the
 * counter-attack's records as dest and from, step 17. True when the 68k
 * returns from its caller too. */
static bool hq_defence_tick(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    ai->strategy = 0x0C;
    if (!(ai->alert_taken & 0x80)) {
        return false;
    }
    ai->countdown--;
    if (ai->countdown == 0) {                           /* .defence_over */
        ai->alert_taken = 0;
        ai->alert = 0;
        ai->hq_hp = hq_hp(c, c->team);
        ai->dest = ai->next_dest;
        ai->from = ai->next_from;
        ai->strategy = 0x11;
        return false;
    }
    ai->strategy = 5;
    if (ai->countdown == 1) {
        send_to(c, ai->dest);
        return true;
    }
    const HzPost *r = rec(c, ai->dest);                 /* .hold_at_hq */
    if (in_arrival_box(m->x, m->y, r->x, r->y)) {
        return true;
    }
    ai->goal.x = r->x;                                  /* .fly_back */
    ai->goal.y = r->y;
    set_plan(c, PLAN_FLY_RESUME);
    return true;
}

/* AI_StratOwnHQ: enemies near: a patrol (kind 2) if 2 or more, then
 * reset mode. Else a garrison of 4 (8 once MARK_HQ_BIG_GARRISON is set)
 * by kind 0; full: (after the defence tick) MARK_HQ_BIG_GARRISON, and a
 * SUPPLY; one over: an escort (kind 5) on each such visit after the
 * first. Then AI_SendToDest. */
static void strat_own_hq(Cpu *c)
{
    HzMechAI *ai = c->ai;
    if (ai->enemy_near != 0) {                          /* .under_attack */
        ai->countdown = 0x78;
        if (ai->enemy_near >= 2 && buy_at(c, true, false, 1, KIND_DEFEND)) {
            return;
        }
        enter_reset_mode(c);
        return;
    }
    u8 *mark = &ai->marks[ai->dest];
    u8 garrison = (*mark & MARK_HQ_BIG_GARRISON) ? 8 : 4;
    s8 left = (s8)(u8)(garrison - ai->own_near);       /* d0's byte */
    if (garrison > ai->own_near) {
        if (buy_at(c, true, false, 1, KIND_GUARDS)) {
            return;
        }
        send_to(c, ai->dest);
        return;
    }
    if (garrison == ai->own_near) {                     /* .garrison_full */
        if (hq_defence_tick(c)) {
            return;
        }
        bool had = (*mark & MARK_HQ_BIG_GARRISON) != 0;
        *mark |= MARK_HQ_BIG_GARRISON;
        if (!had) {
            send_to(c, ai->dest);
            return;
        }
        /* QUIRK: MARK_HQ_SUPPLY is set and cleared again straight away,
         * so a full garrison buys a SUPPLY on every such visit */
        bool bought = (*mark & MARK_HQ_SUPPLY) != 0;
        *mark |= MARK_HQ_SUPPLY;
        if (!bought) {
            *mark &= (u8)~MARK_HQ_SUPPLY;
            if (buy_at(c, true, false, 1, KIND_SUPPLY)) {
                return;
            }
        }
        send_to(c, ai->dest);                           /* .go */
        return;
    }
    if (hq_defence_tick(c)) {                           /* .garrison_over */
        return;
    }
    if (left < -1) {
        send_to(c, ai->dest);
        return;
    }
    bool had = (*mark & MARK_HQ_ESCORT) != 0;
    *mark |= MARK_HQ_ESCORT;
    if (had && buy_at(c, true, false, 1, KIND_ESCORT)) {
        return;
    }
    send_to(c, ai->dest);                               /* .go_escort */
}

/* AI_StratEnemyHQ: an HQ attack (mech_ai_hq_attack, hq_run $FF); 10
 * bought: fall back to from. Else kind 3 (variant 1 when not
 * outnumbered), or outnumbered on an ST map with variant 2 kind 7; then
 * reset mode. (The style and reset-count tests after it change nothing:
 * every branch makes the same call.) */
static void strat_enemy_hq(Cpu *c)
{
    HzMechAI *ai = c->ai;
    ai->hq_attack = 0xFF;
    ai->hq_run = 0xFF;
    if (ai->bought >= 10) {
        ai->bought = 0;
        ai->reset_count = 0;
        send_to(c, ai->from);
        return;
    }
    u8 kind = KIND_HQ;                                  /* .pick_kind */
    if (ai->enemy_near > ai->own_near) {                /* .outnumbered */
        if (ai->st_map && (ai->buy_variant & 3) == 2) {
            kind = KIND_LAUNCH;
        }
    } else {
        ai->buy_variant = 1;
    }
    if (buy_at(c, true, true, 1, kind)) {
        return;
    }
    enter_reset_mode(c);
}

/* 10/11 AI_StratTeamHQ: P1's/P2's HQ: ours or theirs. */
static void strat_team_hq(Cpu *c)
{
    HzMechAI *ai = c->ai;
    bool p1_hq = ai->strategy == 0x0A;
    ai->strategy = 5;
    bool ours = p1_hq ? c->team == 0 : c->team == 1;
    if (ours) {
        strat_own_hq(c);
    } else {
        strat_enemy_hq(c);
    }
}

/* 15/16 AI_StratFlyToDest: from = dest, plan $C there, step 3. */
static void strat_fly_to_dest(Cpu *c)
{
    HzMechAI *ai = c->ai;
    ai->strategy = 3;
    ai->from = ai->dest;
    ai->goal.x = rec(c, ai->dest)->x;
    ai->goal.y = rec(c, ai->dest)->y;
    set_plan(c, PLAN_FLY_RESUME);
}

/* 12 AI_StratRaid: half the time a raid (kind 7): INFANTRY (variant 3)
 * on the raid maps, an ST-57U (variant 2) on the ST maps; not when
 * carrying the wrong unit (an ST map: anything but an ST; else a SUPPLY
 * or GRM). Then step 16. Else fly to the dest. */
static void strat_raid(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    u8 variant;
    if (ai->st_map) {
        if (carrying(m) && own_unit(c, m->carried_slot)->type != UNIT_ST) {
            strat_fly_to_dest(c);
            return;
        }
        variant = 2;
    } else {
        if (!ai->raid_map) {
            strat_fly_to_dest(c);
            return;
        }
        if (carrying(m)) {
            u8 t = own_unit(c, m->carried_slot)->type;
            if (t == UNIT_SUPPLY || t == UNIT_GRM) {
                strat_fly_to_dest(c);
                return;
            }
        }
        variant = 3;
    }
    if ((ai_random(c) & 8) == 0) {
        strat_fly_to_dest(c);
        return;
    }
    ai->strategy = 0x10;
    ai->buy_variant = variant;
    buy_at(c, false, false, 1, KIND_LAUNCH);
}

/* 13 AI_StratHQAlarm: the counter-attack: the enemy's record nearest the
 * own HQ if fuel allows the round trip (from the own HQ), else the dest
 * and from as they are, into mech_ai_next_dest/_from; step 0; countdown
 * $78. */
static void strat_hq_alarm(Cpu *c)
{
    HzMechAI *ai = c->ai;
    const HzPost *hq = rec(c, REC_HQ(c->team));
    ai->next_from = (u8)REC_HQ(c->team);
    int r = nearest_owned(c, hq->x, hq->y, (u8)(2 + (1 - c->team)), false);
    if (r >= 0 && fuel_trip(c, manhattan(hq->x, hq->y, rec(c, r)->x,
                                         rec(c, r)->y))) {
        ai->next_dest = (u8)r;                          /* .target_it */
    } else {
        ai->next_dest = ai->dest;
        ai->next_from = ai->from;
    }
    ai->strategy = 0;                                   /* .restore */
    ai->countdown = 0x78;
}

/* 18 AI_StratCheckReach: dest in reach from from (one way if ours):
 * step 20; else a new route (2). */
static void strat_check_reach(Cpu *c)
{
    HzMechAI *ai = c->ai;
    const HzPost *d = rec(c, ai->dest);
    const HzPost *f = rec(c, ai->from);
    u16 dist = manhattan(f->x, f->y, d->x, d->y);
    ai->strategy = fuel_to_rec(c, ai->dest, dist) ? 0x14 : 2;
}

/* 20 AI_StratReturnToFront: from the own HQ back out to from: the
 * unclaimed record nearest the HQ is claimed; from must be ours and in
 * one-way reach of it, else a new route (2); out of reach: one more hop
 * skipped (mech_ai_hops). Hops left: fly to that record (step 20 again);
 * none: fly to from (step 5), launching from it if it is a launch outpost
 * (or at the dest, under 3 launch deaths). */
static void strat_return_to_front(Cpu *c)
{
    HzMechAI *ai = c->ai;
    int r = nearest_unclaimed(c, rec(c, REC_HQ(c->team))->x,
                              rec(c, REC_HQ(c->team))->y);
    if (r < 0) {
        ai->strategy = 2;
        return;
    }
    ai->marks[r] |= MARK_ROUTE;                         /* AI_MarkNearestHome */
    if (!rec_is_own(c, ai->from)) {                     /* .check_from */
        ai->strategy = 2;
        return;
    }
    const HzPost *f = rec(c, ai->from);                 /* .check_hop */
    if (!fuel_one_way(c, manhattan(f->x, f->y, rec(c, r)->x, rec(c, r)->y))) {
        ai->hops++;
        return;
    }
    u8 step = 5;                                        /* .in_reach */
    HzPoint goal = { f->x, f->y };
    if (ai->hops != 0) {
        step = 0x14;
        goal.x = rec(c, r)->x;
        goal.y = rec(c, r)->y;
        ai->hops--;
    }
    if (step == 5) {                                    /* .check_final */
        ai->route = 0;
        const u8 (*posts)[16] = c->team ? AI_LAUNCH_POSTS_P2
                                        : AI_LAUNCH_POSTS_P1;
        const u8 *row = posts[c->g->map & 7];
        if (ai->from < 16 && row[ai->from]) {
            ai->launch_at_from = 0xFF;
        } else if (ai->dest < 16 && row[ai->dest] && ai->launch_deaths < 3) {
            ai->launch_at_dest = 0xFF;                  /* .check_launch_dest */
        } else {
            ai->launch_deaths = 0;                      /* .clear_deaths */
        }
    }
    ai->strategy = step;                                /* .fly */
    ai->goal = goal;
    set_plan(c, PLAN_FLY_RESUME);
}

static void run_strategy(Cpu *c)                    /* AI_RunStrategy */
{
    HzMechAI *ai = c->ai;
    switch (ai->strategy) {
    case 0x00:                                      /* AI_StratRestart */
        clear_route_marks(ai);
        ai->strategy = 1;
        break;
    case 0x01:                                      /* AI_StratGoHome */
        ai->strategy = 3;
        enter_buy_mode(c);
        break;
    case 0x02:                                      /* AI_StratNewRoute */
        clear_route_marks(ai);
        ai->strategy = 4;
        break;
    case 0x03:                                      /* AI_StratNextDest */
        ai->bought = 0;
        ai->reset_count = 0;
        ai->strategy = 5;
        enter_route_mode(c);
        break;
    case 0x04:                                      /* AI_StratChooseDest */
        ai->strategy = 5;
        enter_route_mode(c);
        break;
    case 0x05:                                      /* AI_StratRouteResult */
        if (ai->route == 0) {
            ai->strategy = 6;
            count_around_dest(c);
        } else if (ai->route == 0x80) {
            ai->hq_run = 0;
            ai->strategy = 0;
        } else {
            ai->strategy = 3;
        }
        break;
    case 0x06:
        strat_at_dest(c);
        break;
    case 0x07:
        strat_neutral_post(c);
        break;
    case 0x08:
    case 0x09:
        strat_team_post(c);
        break;
    case 0x0A:
    case 0x0B:
        strat_team_hq(c);
        break;
    case 0x0C:
        strat_raid(c);
        break;
    case 0x0D:
        strat_hq_alarm(c);
        break;
    case 0x0E:                                      /* AI_StratAlertOutpost */
        ai->dest = ai->alert_post;
        ai->bought = 0;
        ai->reset_count = 0;
        ai->strategy = 5;
        ai->launch_at_from = 0;
        ai->launch_at_dest = 0;
        ai->hq_attack = 0;
        break;
    case 0x0F:
    case 0x10:
        strat_fly_to_dest(c);
        break;
    case 0x11:                                      /* AI_StratResume */
        clear_route_marks(ai);
        ai->hops = 0;
        ai->strategy = 0x12;
        break;
    case 0x12:
        strat_check_reach(c);
        break;
    case 0x13:                                      /* AI_StratGoEnemyHQ */
        ai->strategy = 5;
        ai->from = ai->dest;
        ai->dest = (u8)REC_HQ(1 - c->team);
        ai->hq_run = 0xFF;
        break;
    case 0x14:
        strat_return_to_front(c);
        break;
    default:
        break;          /* UNKNOWN: no step past $14 is ever set */
    }
}

/* ===================================================================== */
/* The other modes of plan 0 (AIPlanModeTable, $B15A).                    */
/* ===================================================================== */

/* Mode 0 (AI_RunSetupMode), at the game's start, a substep a frame. */
static void run_setup_mode(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->substep) {
    case 0: {                                       /* AI_Setup_PickStyle */
        /* mech_ai_style: against a human from the options byte (its top
         * two bits); CPU against CPU, P1's at random and P2's P1's - 2. */
        u8 bits;
        if (c->rival->flags & MECH_CPU) {
            if (c->team) {
                ai->style = (u8)((c->rival->ai.style - 2) & 3);
                bits = ai->style;
            } else {
                u8 r = (u8)ai_random(c);            /* .random */
                bits = (u8)(((r << 2) | (r >> 6)) & 3);
            }
        } else {                                    /* .from_options */
            u8 o = (u8)m->option2;
            bits = (u8)(((o << 2) | (o >> 6)) & 3);
        }
        u8 style = bits & 3;                        /* .set_style */
        u16 r = (u16)((ai_random(c) & 0x3F) + 0x40 * (style & 1));
        ai->style = style;
        ai->engage_range = (u16)(0xC0 - (r >> 1));
        ai->substep++;
        break;
    }
    case 1: {                                       /* AI_Setup_OwnHQ */
        int hq = REC_HQ(c->team);
        ai->dest = (u8)hq;
        ai->hq_hp = hq_hp(c, c->team);
        ai->work.kind = WORK_HQS;
        ai->work.hqs.own.x = rec(c, hq)->x;
        ai->work.hqs.own.y = rec(c, hq)->y;
        ai->substep++;
        u16 map = c->g->map;
        ai->st_map = map == 3 || map == 5 || map == 7;
        ai->raid_map = !(map == 3 || map == 1 || map == 0);
        break;
    }
    case 2: {                                       /* AI_Setup_EnemyHQ */
        int hq = REC_HQ(1 - c->team);
        ai->work.hqs.enemy.x = rec(c, hq)->x;
        ai->work.hqs.enemy.y = rec(c, hq)->y;
        ai->substep++;
        break;
    }
    case 3:                                         /* AI_Setup_EnemyHeading */
        ai->heading = get_direction(ai->work.hqs.own.x, ai->work.hqs.own.y,
                                    ai->work.hqs.enemy.x,
                                    ai->work.hqs.enemy.y);
        ai->substep++;
        break;
    case 4:                                         /* AI_Setup_Done */
        ai->hq_drop = (u8)(((ai->heading >> 1) - 1) & 7);
        ai->enemy_heading = (u8)((ai->hq_drop + 1) & 7);
        ai->post_drop = 1;
        ai->mode = 1;
        ai->step = 0;
        ai->substep = 0;
        ai->strategy = 0;
        ai->move = 0;
        ai->unused_flag = 0xFF;
        break;
    default:
        break;          /* UNKNOWN: no substep past 4 is set */
    }
}

/* The script of buy kind `buy_kind` for a variant: its offset in
 * AI_BUY_SCRIPTS (AIBuyScriptTable holds ROM addresses). */
static u8 script_for(const HzMechAI *ai, int variant)
{
    return (u8)(AI_BUY_SCRIPT_TABLE[variant & 3][ai->buy_kind & 7]
                - AI_BUY_SCRIPTS_ADDR);
}

/* Buy mode substeps 5 and 6: the own outpost nearest the dest is where
 * the unit is collected (mech_ai_pickup_post); plan 8 there (asking for a
 * refuel, 2, if the fuel won't do). None: back to the strategy. */
static void buy_collect(Cpu *c)
{
    HzMechAI *ai = c->ai;
    const HzPost *d = rec(c, ai->dest);
    int r = nearest_owned(c, d->x, d->y, own_code(c), false);
    if (r < 0) {
        ai->mode = 1;
        return;
    }
    ai->pickup_post = (u8)r;                        /* .found_post */
    if (!fuel_one_way(c, manhattan(c->m->x, c->m->y, rec(c, r)->x,
                                   rec(c, r)->y))) {
        ai->refuel = 2;
    }
    set_plan(c, PLAN_PICK_UP);
}

/* Buy mode substep 7, for anything but a kind-0 drop point
 * (AI_BuyMode_Deliver.other_kind). `order` is what the order-$A test
 * reads (see buy_deliver). */
static void deliver_other(Cpu *c, u8 kind, u8 order)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    ai->substep = 9;
    ai->heading = 0xFF;
    if (kind == KIND_LAUNCH) {                      /* .kind7 */
        set_plan(c, ai->launch_at_dest ? PLAN_DELIVER_POST : PLAN_LAUNCH);
        return;
    }
    if (order == ORDER_ATTACK_HQ) {                 /* .launch_tables */
        const u8 (*posts)[16] = c->team ? AI_LAUNCH_POSTS_P2
                                        : AI_LAUNCH_POSTS_P1;
        const u8 (*heads)[16] = c->team ? AI_LAUNCH_HEADINGS_P2
                                        : AI_LAUNCH_HEADINGS_P1;
        int map = c->g->map & 7;
        if (ai->pickup_post < 16 && posts[map][ai->pickup_post]) {
            ai->heading = heads[map][ai->pickup_post];
            set_plan(c, PLAN_LAUNCH);               /* .launch */
            return;
        }
    }
    const HzPost *d = rec(c, ai->dest);             /* .deliver */
    const HzPost *p = rec(c, ai->pickup_post);
    if (!fuel_trip(c, manhattan(p->x, p->y, d->x, d->y))) { /* AI_FuelDestToBase */
        ai->refuel = 1;
    }
    if (kind == KIND_DEFEND) {
        ai->frames_left = 0;
        set_plan(c, PLAN_TO_ENEMY);
        return;
    }
    u8 plan = rec_is_hq(c, ai->dest) ? PLAN_DELIVER_HQ : PLAN_DELIVER_POST;
    if (!rec_is_own(c, ai->dest)) {                 /* .by_dest_type */
        set_plan(c, (u8)(plan + 8));
        return;
    }
    /* .own_dest: QUIRK: the pad is given a direction (mech_ai_deliver_dir,
     * + 1 each time) just before AI_SetPlan clears it */
    m->pad = dir8_pad(ai->deliver_dir);
    ai->deliver_dir++;
    set_plan(c, plan);
}

/* Buy mode substep 7 (AI_BuyMode_Deliver): an ST-57U: plan $10. Kind 0:
 * the next drop point round the dest (AIDropOffsetsHQ/Post by the cursor;
 * off-map points skipped; an occupied one: the next, next frame), with
 * the facing AIDropDirs gives it there, plan 4 then (substep 8) plan 3.
 * Out of drop points, or another kind: deliver_other. */
static void buy_deliver(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    const HzUnit *u = own_unit(c, m->carried_slot);
    if (u->type == UNIT_ST) {                       /* .st_unit */
        ai->substep = 9;
        set_plan(c, PLAN_DELIVER_ST);
        return;
    }
    if (ai->buy_kind != KIND_GUARDS) {
        deliver_other(c, ai->buy_kind, u->order);
        return;
    }
    /* QUIRK: the order-$A test in .other_kind reads a2: the carried unit
     * when the cursor runs out at once, but after a drop point has been
     * tried a2 is AIDropDirs, whose byte 2 ($02) is never $A. */
    u8 order = u->order;
    const HzPost *d = rec(c, ai->dest);
    for (;;) {                                      /* .next_drop_point */
        u8 n;
        const HzPoint *offsets;
        if ((d->flags & (POST_USED | POST_HQ)) == POST_USED) { /* .post_drop */
            n = ai->post_drop;
            ai->post_drop = (u8)(ai->post_drop + 2);
            if (n >= ai->post_drop_end) {
                deliver_other(c, KIND_CAPTURE, order);
                return;
            }
            offsets = AI_DROP_OFFSETS_POST;
        } else {
            n = ai->hq_drop;
            ai->hq_drop++;
            if (n >= ai->hq_drop_end) {
                deliver_other(c, KIND_CAPTURE, order);
                return;
            }
            offsets = AI_DROP_OFFSETS_HQ;
        }
        int i = n & 7;                              /* .try_point */
        ai->heading = AI_DROP_DIRS[i];
        order = AI_DROP_DIRS[2];
        u16 y = (u16)(d->y + offsets[i].y);
        if (y > MAP_EDGE_HI || y < MAP_EDGE_LO) {
            continue;
        }
        u16 x = (u16)(d->x + offsets[i].x);
        if (x > MAP_EDGE_HI || x < MAP_EDGE_LO) {
            continue;
        }
        ai->goal.x = (s16)x;
        ai->goal.y = (s16)y;
        if (cell_occupied(c->g, (s16)x, (s16)y)) {
            return;
        }
        const HzPost *p = rec(c, ai->pickup_post);  /* .cell_free */
        if (!fuel_trip(c, manhattan((s16)x, (s16)y, p->x, p->y))) {
            ai->refuel = 1;                         /* AI_FuelPointToBase */
        }
        ai->substep = 8;                            /* .fly_to_point */
        set_plan(c, PLAN_FLY);
        return;
    }
}

/* Mode 2 (AI_RunBuyMode): buy, collect and deliver, a substep at a
 * time. AI_BuyAt* enter at substep 2. */
static void run_buy_mode(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->substep) {
    case 0:
    case 1: {                                       /* AI_BuyMode_GoHome */
        int hq = REC_HQ(c->team);
        ai->from = (u8)hq;
        ai->dest = (u8)hq;
        ai->goal.x = rec(c, hq)->x;
        ai->goal.y = rec(c, hq)->y;
        fuel_trip(c, manhattan(m->x, m->y, rec(c, hq)->x, rec(c, hq)->y));
        ai->mode = 1;
        set_plan(c, PLAN_FLY_RESUME);
        break;
    }
    case 2: {                                       /* AI_BuyMode_Next */
        if (ai->buy_count == 0) {
            ai->mode = 1;
            if (ai->attack_buy) {
                if (m->carried_order != ORDER_ATTACK_HQ) {
                    enter_reset_mode(c);
                }
            } else if (ai->strategy == 0x0C) {
                send_to(c, ai->dest);
            }
            break;
        }
        u8 reorder;
        if (carrying(m)) {                          /* .buy_more */
            ai->buy_script = script_for(ai, ai->buy_variant);
            reorder = 0xFF;
        } else {                                    /* .not_carrying */
            u8 s = m->prod_state;
            if (s != 0 && !(s & 0x80) && s != 3) {
                ai->substep = 4;
                break;
            }
            ai->buy_script = script_for(ai, ai->buy_variant);   /* .new_buy */
            ai->buy_variant++;
            reorder = 0;
        }
        ai->reorder = reorder;                      /* .run_plan1 */
        ai->substep = reorder ? 7 : 3;
        set_plan(c, PLAN_BUY);
        break;
    }
    case 3:                                         /* AI_BuyMode_Bought */
        /* (Plan 1 and this treat mech_prod_state 3 as $FF; nothing writes
         * 3.) */
        if ((m->prod_state & 0x80) || m->prod_state == 3) {
            ai->mode = 1;
            break;
        }
        ai->lifted = ai->reorder;                   /* always 0 here */
        ai->bought++;
        ai->substep = 5;
        break;
    case 4:                                         /* AI_BuyMode_OnOrder */
        ai->lifted = 0;
        ai->substep = 6;
        break;
    case 5:                                         /* AI_BuyMode_Collect */
        if (ai->lifted) {
            ai->substep = 7;
        } else if (m->prod_state == 0 || (m->prod_state & 0x80)) {
            ai->substep = 2;
        } else {
            buy_collect(c);
        }
        break;
    case 6:                                         /* AI_BuyMode_CollectOld */
        if (ai->lifted || m->prod_state == 0 || (m->prod_state & 0x80)) {
            ai->substep = 2;
        } else {
            buy_collect(c);
        }
        break;
    case 7:
        buy_deliver(c);
        break;
    case 8:                                         /* AI_BuyMode_Drop */
        ai->substep = 9;
        set_plan(c, PLAN_DROP);
        break;
    case 9:                                         /* AI_BuyMode_CheckDelivered */
        if (m->form == HZ_FORM_JET || ai->dropped) {
            ai->substep = 2;
            ai->buy_count--;
        } else {
            ai->substep = 7;
        }
        break;
    default:
        break;          /* UNKNOWN: no substep past 9 is set */
    }
}

/* AI_PickNextDest: the next destination, claimed (mark b0): mech_ai_style
 * b0, the unclaimed record nearest from; else AI_NextTowardEnemyHQ. False
 * if none. */
static bool pick_next_dest(Cpu *c)
{
    HzMechAI *ai = c->ai;
    int r;
    if (ai->style & 1) {
        r = nearest_unclaimed(c, rec(c, ai->from)->x, rec(c, ai->from)->y);
    } else {                                        /* .toward_enemy_hq */
        const HzPost *e = rec(c, REC_HQ(1 - c->team));
        /* AI_NextTowardEnemyHQ: the own HQ while it is unclaimed; else
         * the unclaimed record nearest the enemy HQ that fuel from `from`
         * reaches (one way to an own record, there and back to others) */
        int hq = REC_HQ(c->team);
        if (!(ai->marks[hq] & MARK_ROUTE)) {
            r = hq;
        } else {                                    /* .search */
            const HzPost *f = rec(c, ai->from);
            r = -1;
            u16 best_d = 0xFFFF;
            for (int i = 0; i < HZ_AI_RECORDS; i++) {
                const HzPost *q = rec(c, i);
                if ((ai->marks[i] & MARK_ROUTE) || !(q->flags & POST_USED)) {
                    continue;
                }
                if (!fuel_to_rec(c, i, manhattan(q->x, q->y, f->x, f->y))) {
                    continue;
                }
                u16 d = manhattan(e->x, e->y, q->x, q->y);  /* .distance */
                if (d <= best_d) {
                    r = i;
                    best_d = d;
                }
            }
        }
    }
    if (r < 0) {
        return false;
    }
    ai->dest = (u8)r;
    ai->marks[r] |= MARK_ROUTE;
    return true;
}

/* Mode 3 (AI_RunRouteMode): choosing the next destination. Result in
 * mech_ai_route: 0 chosen, $80 none left. */
static void run_route_mode(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->substep) {
    case 0:                                         /* AI_Route_PickDest */
        ai->launch_at_from = 0;
        ai->launch_at_dest = 0;
        ai->launches = 0;
        if (!pick_next_dest(c)) {
            ai->route = 0x80;
            ai->mode = 1;
        } else {
            ai->substep = 1;
        }
        break;
    case 1: {                                       /* AI_Route_CheckFuel */
        const HzPost *f = rec(c, ai->from);
        const HzPost *d = rec(c, ai->dest);
        ai->substep = 2;
        if (!fuel_to_rec(c, ai->dest, manhattan(f->x, f->y, d->x, d->y))) {
            ai->substep = 3;                        /* .short_of_fuel */
            break;
        }
        const u8 (*posts)[16] = c->team ? AI_LAUNCH_POSTS_P2
                                        : AI_LAUNCH_POSTS_P1;
        if (ai->dest < 16 && posts[c->g->map & 7][ai->dest]) {
            /* an own launch outpost: a staging point, launch from it */
            ai->hq_run = 0x80;
            ai->marks[ai->dest] |= MARK_LAUNCH;
            ai->launch_at_dest = 0xFF;
            ai->mode = 1;                           /* .to_strategy */
            ai->route = 0;
            break;
        }
        u8 f8 = d->flags & (HQ_FLAGS | POST_TEAM2); /* .not_launch_point */
        if (f8 == HQ_FLAGS || f8 == (HQ_FLAGS | POST_TEAM2)) {
            if ((f8 == HQ_FLAGS) != (c->team == 0)) {
                ai->hq_run = 0xFF;                  /* .enemy_hq */
            }
            ai->mode = 1;
        }
        ai->route = 0;                              /* .chosen */
        break;
    }
    case 2: {                                       /* AI_Route_CheckHQReach */
        /* the enemy HQ in reach (there and back) from from: it is the
         * dest; else in reach from the dest: that is a staging point */
        int ehq = REC_HQ(1 - c->team);
        const HzPost *e = rec(c, ehq);
        const HzPost *f = rec(c, ai->from);
        if (fuel_trip(c, manhattan(f->x, f->y, e->x, e->y))) {
            ai->dest = (u8)ehq;
            ai->hq_run = 0xFF;                      /* .go_for_hq */
        } else {
            const HzPost *d = rec(c, ai->dest);     /* .from_dest */
            if (fuel_trip(c, manhattan(d->x, d->y, e->x, e->y))) {
                ai->marks[ai->dest] |= MARK_LAUNCH;
                ai->hq_run = 0x80;
            }
        }
        ai->mode = 1;                               /* .to_strategy */
        /* (mech_ai_route 1 would call plan $C here; nothing sets 1) */
        if (ai->route == 1) {
            ai->route = 0;
            set_plan(c, PLAN_FLY_RESUME);
        }
        break;
    }
    case 3: {                                       /* AI_Route_FindRefuel */
        /* short of fuel: the own outpost nearest the dest, if the mech can
         * reach it, becomes from */
        const HzPost *d = rec(c, ai->dest);
        int r = nearest_owned(c, d->x, d->y, own_code(c), false);
        if (r < 0 || !fuel_one_way(c, manhattan(m->x, m->y, rec(c, r)->x,
                                                 rec(c, r)->y))) {
            ai->mode = 1;
            break;
        }
        ai->route = 0;
        ai->substep = 2;
        ai->from = (u8)r;
        break;
    }
    default:
        break;          /* UNKNOWN: no substep past 3 is set */
    }
}

/* Mode 4 (AI_RunResetMode): the ground plan's fields cleared, mode 1,
 * plan 5 (round the dest). */
static void run_reset_mode(Cpu *c)
{
    HzMechAI *ai = c->ai;
    ai->target = ref_none();
    ai->ground_shot = 0;
    ai->misses_left = 3;
    ai->frames_left = 0;
    ai->aim_dir = 0;
    ai->attack_here = 0;
    ai->dropped = 0;
    ai->mode = 1;
    ai->heading = 0;
    set_plan(c, PLAN_GROUND);
}

static void plan_modes(Cpu *c)                      /* AI_PlanModes */
{
    switch (c->ai->mode) {
    case 0:
        run_setup_mode(c);
        break;
    case 1:
        run_strategy(c);
        break;
    case 2:
        run_buy_mode(c);
        break;
    case 3:
        run_route_mode(c);
        break;
    case 4:
        run_reset_mode(c);
        break;
    default:
        break;          /* UNKNOWN: no mode past 4 is set */
    }
}

/* ===================================================================== */
/* The plans (AIPlanTable, $B112).                                         */
/* ===================================================================== */

/* -- plan 1: buy through the order menu (AI_PlanBuy) ----------------------- */

static u8 script_type(const HzMechAI *ai)
{
    return AI_BUY_SCRIPTS[ai->buy_script];
}

static u8 script_order(const HzMechAI *ai)
{
    return AI_BUY_SCRIPTS[ai->buy_script + 1];
}

static bool script_done(const HzMechAI *ai)     /* tst.w (a5): $FF $FF */
{
    return (script_type(ai) & 0x80) != 0;
}

/* Step 0 AI_Buy_ChoosePair: the first (type, order) pair of the script it
 * can afford (unit price + order fee). QUIRK (a cheat, kept): every pair
 * it can't afford adds 100 to its money first. Carrying: re-order the
 * carried unit instead -- not an ST, SUPPLY or GRM, nor an attack-HQ unit
 * during an HQ attack -- to the first script order other than its own it
 * can afford, + 50 money per one it can't. Chosen: step 6. */
static void buy_choose_pair(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    const HzAssets *as = c->g->assets;
    /* The pair it is at; mech_ai_buy_script moves on only to a pair it
     * chose (a script run out leaves it where it was). */
    u8 at = ai->buy_script;
    if (!carrying(m)) {
        for (;;) {                                  /* .next_pair */
            u8 type = AI_BUY_SCRIPTS[at];
            u8 order = AI_BUY_SCRIPTS[at + 1];
            if (type & 0x80) {                      /* $FF $FF */
                m->prod_state = 0xFF;
                resume_plan(c);
                return;
            }
            u16 price = (u16)(unit_stats_for(c->g, type)->price
                              + as->orders[order & 0x0F].fee);
            if (price <= m->money) {
                ai->buy_script = at;                /* .chosen */
                ai->step = 6;
                return;
            }
            m->money = (u16)(m->money + 100);
            at = (u8)(at + 2);
        }
    }
    u8 type = own_unit(c, m->carried_slot)->type;   /* .reorder */
    if (type >= UNIT_ST && type <= UNIT_GRM) {
        resume_plan(c);
        return;
    }
    u8 carried = m->carried_order;                  /* .check_hq_attack */
    if (ai->hq_attack && carried == ORDER_ATTACK_HQ) {
        resume_plan(c);
        return;
    }
    for (;;) {                                      /* .next_order */
        u8 order = AI_BUY_SCRIPTS[at + 1];
        if (order == carried || (AI_BUY_SCRIPTS[at] & 0x80)) {
            resume_plan(c);
            return;
        }
        u16 fee = as->orders[order & 0x0F].fee;
        if (fee <= m->money) {
            ai->buy_script = at;                    /* .order_chosen */
            ai->step = 6;
            return;
        }
        m->money = (u16)(m->money + 50);
        at = (u8)(at + 2);
    }
}

/* Steps 1 and 2: a press on the menu, paced (mech_ai_pad_wait counts
 * down, then the press, and the count starts again from the delay). */
static u8 paced(HzMechAI *ai, u8 press)
{
    if (ai->work.presses.wait != 0) {
        ai->work.presses.wait--;
        return 0;
    }
    ai->work.presses.wait = ai->work.presses.delay;
    return press;
}

/* Step 1 AI_Buy_PickType: the cursor to row 0 (up from 1, down from 2),
 * then right or left until the menu's type is the pair's; each press also
 * writes the pair's order into the menu (mech_menu_order). Carrying:
 * nothing to do. A press is a frame, then a frame released. */
static void buy_pick_type(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (carrying(m)) {
        m->pad = 0;                                 /* .next_step */
        ai->step++;
        return;
    }
    if (m->menu.row != 0) {
        if (m->pad != 0) {
            m->pad = 0;
            return;
        }
        m->pad = paced(ai, m->menu.row == 1 ? HZ_PAD_UP : HZ_PAD_DOWN);
        return;
    }
    if (m->menu.type == script_type(ai)) {          /* .on_row0 */
        m->pad = 0;
        ai->step++;
        return;
    }
    if (m->pad != 0) {
        m->pad = 0;
        return;
    }
    bool below = m->menu.type < script_type(ai);    /* .check_type */
    m->pad = paced(ai, below ? HZ_PAD_RIGHT : HZ_PAD_LEFT);
    if (m->pad != 0) {                              /* .store_lr */
        m->menu.order = script_order(ai);
    }
}

/* Step 2 AI_Buy_PickOrder: carrying a unit with that order already:
 * step 5. The cursor to row 1 (down from 0, up from 2), then right or
 * left until the menu's order is the pair's; after 17 presses give up
 * (step 5). */
static void buy_pick_order(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (carrying(m) && m->carried_order == script_order(ai)) {
        ai->step = 5;
        return;
    }
    if (m->menu.row != 1) {                         /* .to_row1 */
        if (m->pad != 0) {
            m->pad = 0;
            return;
        }
        m->pad = paced(ai, m->menu.row == 0 ? HZ_PAD_DOWN : HZ_PAD_UP);
        return;
    }
    if (m->menu.order == script_order(ai)) {        /* .on_row1 */
        m->pad = 0;
        ai->step++;
        return;
    }
    if (m->pad != 0) {
        m->pad = 0;
        return;
    }
    bool below = m->menu.order < script_order(ai);  /* .check_order */
    m->pad = paced(ai, below ? HZ_PAD_RIGHT : HZ_PAD_LEFT);
    if (m->pad != 0) {                              /* .store_lr */
        bool none_left = ai->order_tries == 0;
        ai->order_tries--;
        if (none_left) {
            m->pad = 0;
            ai->step = 5;
        }
    }
}

/* Step 4 AI_Buy_Confirm: B, then the menu's answer (mech_prod_state;
 * carrying: mech_reorder_state): none yet, press again; short of money
 * ($FF, or 3): the next pair from step 1 (the script's end: step 5);
 * done: step 5. */
static void buy_confirm(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    u8 pad = m->pad;
    if (pad == 0) {
        m->pad = HZ_PAD_B;
        return;
    }
    m->pad = 0;                                     /* .released */
    if (pad != HZ_PAD_B) {
        return;
    }
    u8 state = carrying(m) ? m->menu.reorder_state : m->prod_state;
    if (state == 0) {
        return;
    }
    if ((state & 0x80) || state == 3) {             /* .next_pair */
        ai->buy_script = (u8)(ai->buy_script + 2);
        ai->step = script_done(ai) ? 5 : 1;
        return;
    }
    ai->step++;
}

static void plan_buy(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->step) {
    case 0:
        buy_choose_pair(c);
        break;
    case 1:
        buy_pick_type(c);
        break;
    case 2:
        buy_pick_order(c);
        break;
    case 3:                                         /* AI_Buy_Release */
        m->pad = 0;
        ai->step++;
        break;
    case 4:
        buy_confirm(c);
        break;
    case 5:                                         /* AI_Buy_CloseMenu */
        if (m->pad != 0) {
            m->pad = 0;
        } else if (!m->menu.closing) {
            if (!(m->flags & MECH_MENU_OPEN)) {
                resume_plan(c);
            } else {
                m->pad = HZ_PAD_C;
            }
        }
        break;
    case 6:                                         /* AI_Buy_OpenMenu */
        if (m->pad != 0) {
            m->pad = 0;
        } else if (!(m->flags & MECH_MENU_OPEN)) {
            m->pad = HZ_PAD_C;
        } else if (m->menu.step == 0) {             /* .wait_open */
            ai->step = 1;
            ai->order_tries = 0x10;
            u16 d = (m->view < 0) ? 0 : 6;          /* hidden: no pause */
            ai->work.kind = WORK_PRESSES;
            ai->work.presses.delay = d;
            ai->work.presses.wait = d;
        }
        break;
    default:
        break;          /* UNKNOWN: no step past 6 is set */
    }
}

/* -- plans 2 and 3: lift and drop (AI_PlanLift, AI_PlanDrop) --------------- */

/* Tap A once; if the mech landed instead, tap A until it is in the air
 * again. Plan 2 (nothing starts it): carrying afterwards, mech_ai_lifted
 * $FF. Plan 3: an empty jet afterwards, mech_ai_dropped $FF. */
static void plan_lift_or_drop(Cpu *c, bool lift)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (ai->step == 0) {
        if (lift) {
            ai->lifted = 0;
        } else {
            ai->dropped = 0;
        }
        m->pad = 0;
        ai->step++;
        return;
    }
    if (m->pad == 0) {
        m->pad = HZ_PAD_A;
        return;
    }
    m->pad = 0;                                     /* .tapped */
    if (ai->step == 1) {
        if (robot(m)) {
            ai->step++;
            return;
        }
        if (lift && carrying(m)) {
            ai->lifted = 0xFF;
        }
        if (!lift && (m->form & 0xFF) == HZ_FORM_JET) {
            ai->dropped = 0xFF;
        }
        resume_plan(c);
        return;
    }
    if (robot(m)) {                                 /* .take_off */
        return;
    }
    if (lift && carrying(m)) {
        ai->lifted = 0xFF;
    }
    resume_plan(c);
}

/* -- watching the enemy mech on the way somewhere ----------------------------- */

/* Which plan's copy of the watch (they differ in when they look, what
 * happens near the goal and the step after). */
enum {
    WATCH_FLY,          /* plans 4/9/$C step 4 (AI_Fly_WatchEnemy) */
    WATCH_DELIVER,      /* plans 6/7/$E/$F step 3 */
    WATCH_PICK_UP,      /* plan 8 step 5 */
    WATCH_TO_ENEMY,     /* plan $A step 5 */
    WATCH_SERVICE       /* plan $B step 11 */
};

/* The enemy mech in range and within 4 steps of the heading: a 16-frame
 * detour (mech_ai_detour) 90 degrees away under half the range, else 45;
 * while one runs, every other frame the pad turns a step back towards the
 * course when it is 3 to 5 8-way steps off. Else, or when it doesn't look
 * (the enemy destroyed, ...), the pad goes back on the course
 * (mech_ai_course) and a detour ends. Near the goal some plans do
 * otherwise (below). `next` is the step after. */
static void watch_enemy(Cpu *c, int kind, u8 next)
{
    HzMech *m = c->m;
    HzMech *e = c->rival;
    HzMechAI *ai = c->ai;
    bool look;
    switch (kind) {
    case WATCH_FLY:
        look = (m->blocked & PROBE_OBJECT) || (!destroyed(e)
               && (ai->plan == PLAN_FLY_SERVICE || carrying(m)));
        break;
    case WATCH_PICK_UP:
    case WATCH_SERVICE:
        look = (m->blocked & PROBE_OBJECT)
               || !(e->flags & (MECH_MENU_OPEN | MECH_DESTROYED));
        break;
    default:
        look = !destroyed(e);
        break;
    }
    bool threat = false;
    int turn = 0;
    if (look) {                                     /* .check_enemy */
        u16 dist;
        u8 dir;
        u16 range = ai->engage_range;
        if (kind == WATCH_SERVICE) {                /* AI_GetDirection */
            dir = get_direction(m->x, m->y, e->x, e->y);
            dist = manhattan(m->x, m->y, e->x, e->y);
        } else {
            dir = dir_and_dist(m->x, m->y, e->x, e->y, &dist);
        }
        int rel = (dir - m->dir) & 0x0F;
        if (dist <= range && (rel <= 4 || rel >= 0x0C)) {   /* .enemy_ahead */
            threat = true;
            switch (kind) {
            case WATCH_FLY:
                /* plan 9 with the enemy within $80 of the goal: another
                 * own outpost as the goal. UNKNOWN: the original's
                 * .allow_fight after it (mech_ai_engage $FF -> $80) is
                 * reached only when the plan isn't 9, inside a test for
                 * plan 9: never */
                if (ai->plan == PLAN_FLY_SERVICE
                    && near_point(ai->goal.x, ai->goal.y, e->x, e->y,
                                  0x80)) {
                    int r = nearest_owned(c, m->x, m->y, own_code(c), true);
                    if (r < 0) {
                        threat = false;
                        break;
                    }
                    ai->goal.x = rec(c, r)->x;
                    ai->goal.y = rec(c, r)->y;
                    ai->step = 0;
                    return;
                }
                break;
            case WATCH_DELIVER: {
                bool here = ai->plan == PLAN_DELIVER_HQ
                         || ai->plan == PLAN_DELIVER_POST;
                if (near_point(ai->goal.x, ai->goal.y, e->x, e->y,
                               here ? 0xC8 : 0x190)) {
                    m->pad &= (u8)~HZ_PAD_A;        /* drop it here */
                    ai->step = 2;
                    return;
                }
                break;
            }
            case WATCH_PICK_UP: {
                const HzPost *p = rec(c, ai->pickup_post);
                if (near_point(p->x, p->y, e->x, e->y, 0x80)) {
                    if (ai->engage) {
                        ai->engage = 0x80;
                    }
                    if (ai->hq_attack) {
                        ai->hq_attack = 0x80;
                    }
                    threat = false;
                }
                break;
            }
            case WATCH_TO_ENEMY:
                if (near_point(ai->goal.x, ai->goal.y, e->x, e->y, 0xC8)) {
                    m->pad &= (u8)~HZ_PAD_A;
                    ai->step = 4;
                    return;
                }
                break;
            default:
                break;
            }
            if (threat) {                           /* .dodge */
                int step = (dist > (u16)(range >> 1)) ? 2 : 4;
                turn = (rel >= 8) ? step : -step;
            }
        }
    }
    if (!look) {
        goto back_on_course;    /* not looking: any detour ends at once */
    }
    if (threat && ai->detour == 0) {                /* .start_detour */
        ai->detour = 0x10;
        steer_pad(m, ((m->dir + turn) & 0x0F) >> 1);
        ai->step = next;
        return;
    }
    if (ai->detour != 0) {                          /* .detouring */
        if (!(ai->detour & 1)) {
            u8 want = AI_PAD_TO_DIR8[ai->course & 0x0F];
            if (!(want & 0x80)) {
                int now = m->dir >> 1;
                int off = (want - now) & 7;
                if (off >= 3 && off < 6) {
                    steer_pad(m, now + ((off == 3) ? 1 : -1));
                }
            }
        }
        ai->detour--;                               /* .count_detour */
        ai->step = next;
        return;
    }
back_on_course:                                     /* .back_on_course */
    m->pad = (u8)((m->pad & HZ_PAD_BUTTONS) | ai->course);
    ai->detour = 0;
    ai->step = (kind == WATCH_FLY && ai->plan == PLAN_FLY_SERVICE) ? 5 : next;
}

/* -- plans 4, 9, $C: fly to the goal (AI_PlanFlyToGoal) ------------------- */

/* Steps 2, 3 AI_Fly_TurnToHeading: mech_ai_heading none or reached:
 * AI_ResumePlan; else a step towards it every 4th frame. */
static void fly_turn_to_heading(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (ai->heading & 0x80) {
        resume_plan(c);
        return;
    }
    u8 diff = (u8)(ai->heading - m->dir);
    if (diff == 0) {
        resume_plan(c);
        return;
    }
    if (ai->frames & 3) {
        return;
    }
    u8 d = ((diff & 0x0F) >= 8) ? (u8)(m->dir - 1) : (u8)(m->dir + 1);
    m->dir = d & 0x0F;
    m->turn_to = m->dir;
}

/* Arrived (the box -$2C..+$C round the goal): direction released; plans
 * 9 and $C resume. */
static bool fly_arrived(Cpu *c)
{
    if (!in_arrival_box(c->m->x, c->m->y, c->ai->goal.x, c->ai->goal.y)) {
        return false;
    }
    c->m->pad &= HZ_PAD_BUTTONS;
    return true;
}

/* Plans 4, 9 and $C. Arrived: 9 and $C resume, 4 turns to mech_ai_heading
 * first. On the way: 0 the course, 1 arrived?, 4 the enemy watch (only
 * while carrying, on plan 9 or a walk blocked), 5 (plan 9) the nearest
 * own outpost again. */
static void plan_fly(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (fly_arrived(c)) {
        if (ai->plan != PLAN_FLY) {
            resume_plan(c);
            return;
        }
        ai->frames_left = 0;
        ai->aim_dir = 0;
        fly_turn_to_heading(c);
        return;
    }
    switch (ai->step) {                             /* .run_step */
    case 0: {                                       /* AI_Fly_SetCourse */
        /* the course to the goal - $10, pressed unless a detour runs;
         * kept when already flying within 1 of it $80+ px out (QUIRK:
         * |direction - heading| isn't wrapped) */
        u16 dist;
        u8 dir = dir_and_dist(m->x, m->y, (s16)(ai->goal.x - 0x10),
                              (s16)(ai->goal.y - 0x10), &dist);
        u8 bits = dir8_pad((dir & 0x0F) >> 1);
        bool keep = false;
        if ((m->pad & HZ_PAD_DIRS) != 0 && dist >= 0x80) {
            u8 d = (u8)(dir - m->dir);
            if ((s8)d < 0) {
                d = (u8)-d;
            }
            keep = d < 2;
        }
        if (!keep) {                                /* .new_course */
            if (ai->detour == 0) {
                m->pad = (u8)((m->pad & HZ_PAD_BUTTONS) | bits);
            }
            ai->course = bits;                      /* .store_course */
        }
        ai->step = 1;
        break;
    }
    case 1:                                         /* AI_Fly_CheckArrived */
        if (fly_arrived(c)) {
            if (ai->plan != PLAN_FLY) {
                resume_plan(c);
                return;
            }
            ai->step = 3;
            ai->frames_left = 0;
            ai->aim_dir = 0;
        } else {
            ai->step = 4;
        }
        break;
    case 2:
    case 3:
        fly_turn_to_heading(c);
        break;
    case 4:
        watch_enemy(c, WATCH_FLY, 0);
        break;
    case 5: {                                       /* AI_Fly_Retarget */
        int r = nearest_own_post(c);
        if (r >= 0) {
            ai->goal.x = rec(c, r)->x;
            ai->goal.y = rec(c, r)->y;
        }
        ai->step = 0;
        break;
    }
    default:
        break;          /* UNKNOWN: no step past 5 is set */
    }
}

/* -- plan 5: fight on foot (AI_PlanGroundAttack) --------------------------- */

/* The step 2/4/8 helpers: a random nudge of the 8-way pad direction by
 * one either way (or none). */
static void nudge_course_dir(Cpu *c, u32 r, u32 sign_bit)
{
    int d = (int)(r & 1);
    if (!(r & sign_bit)) {
        d = -d;
    }
    c->ai->pad_dir8 = (u8)((c->ai->pad_dir8 + d) & 7);
}

/* Steps 1 (plan 5) and plan $A's step 1: the goal is the target's place
 * (a mech id: the other mech), and a box round it. */
static void target_pos(Cpu *c, s16 *x, s16 *y, bool *st)
{
    HzRef id = c->ai->target;
    *st = false;
    if (id.kind != REF_UNIT) {
        *x = c->rival->x;
        *y = c->rival->y;
        return;
    }
    const HzUnit *u = ref_unit_ptr(c->g, id);
    *x = u->x;
    *y = u->y;
    *st = u->type == UNIT_ST;
}

/* Step 0 AI_Ground_FindTarget: scan 6 rings (an HQ 9, on ST maps 11) round
 * the dest (mech_ai_heading set: the pickup post; mech_ai_attack_here:
 * itself) for the first enemy: mech_ai_target, step 1; none: step 6.
 * QUIRK: round itself, the original's HQ test reads a byte of its own
 * code, so it is always 6 rings. */
static void ground_find_target(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    service_check(ai);
    s16 x = m->x;
    s16 y = m->y;
    int rings = 6;
    if (!ai->attack_here) {                         /* .centre_record */
        const HzPost *r = rec(c, ai->heading ? ai->pickup_post : ai->dest);
        x = r->x;
        y = r->y;
        if ((r->flags & HQ_FLAGS) == HQ_FLAGS) {    /* .ring_count */
            rings += 3;
            if (ai->st_map) {
                rings += 2;
            }
        }
    }
    Scan s;
    scan_round(c->g, x, y, rings, &s);
    HzRef id = first_enemy(c, &s);
    if (ref_is_none(id)) {
        ai->step = 6;                               /* .no_enemy */
        return;
    }
    ai->target = id;
    ai->step++;
}

/* Step 1 AI_Ground_SetBox: the goal is the target's place; the box
 * (goal & ~7) +/- $40 ($100 round an ST-57U), clamped to 0..$7E0. */
static void ground_set_box(Cpu *c)
{
    HzMechAI *ai = c->ai;
    s16 x, y;
    bool st;
    target_pos(c, &x, &y, &st);
    u16 r = st ? 0x100 : 0x40;
    ai->goal.x = x;
    ai->goal.y = y;
    u16 bx = (u16)x & 0xFFF8;
    u16 by = (u16)y & 0xFFF8;
    ai->work.kind = WORK_BOX;
    ai->work.box.left = (s16)(bx >= r ? bx - r : 0);
    ai->work.box.right = (s16)((u16)(bx + r) > 0x7E0 ? 0x7E0 : bx + r);
    ai->work.box.top = (s16)(by >= r ? by - r : 0);
    ai->work.box.bottom = (s16)((u16)(by + r) > 0x7E0 ? 0x7E0 : by + r);
    ai->step++;
}

/* Step 3 AI_Ground_FlyIn: outside the box (exclusive): head for the goal
 * (frames left cleared), then step 0 (once it has been in the box,
 * mech_ai_dropped: step 4). Inside: mech_ai_dropped $FF; its laps to fly
 * on (mech_ai_frames_left) done: release A, step 4; else one fewer, a
 * held direction kept (step 0) or a random one (step 4). */
static void ground_fly_in(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    const s16 x = m->x;
    const s16 y = m->y;
    u8 dir;
    if (y > ai->work.box.top && y < ai->work.box.bottom
        && x > ai->work.box.left && x < ai->work.box.right) {
        ai->dropped = 0xFF;
        if (ai->frames_left == 0) {
            m->pad &= (u8)~HZ_PAD_A;
            ai->step++;
            return;
        }
        ai->frames_left--;                          /* .fly_on */
        u32 r = ai_random(c);
        if (m->pad & HZ_PAD_DIRS) {
            ai->step = 0;                           /* .rescan */
            return;
        }
        dir = (u8)(r >> 16);    /* (the swap: the seed's old high word) */
    } else {                                        /* .head_for_goal */
        ai->frames_left = 0;
        dir = get_direction(x, y, ai->goal.x, ai->goal.y);
    }
    int d = (dir & 0x0F) >> 1;                      /* .set_dir */
    ai->pad_dir8 = (u8)d;
    steer_pad(m, d);
    if (ai->dropped) {
        ai->step++;
    } else {
        ai->step = 0;
    }
}

/* Step 4 AI_Ground_Land: A not held: tap it if the grid cell below is
 * free, else step 2. Held: release; landed: step 2; still a jet: the
 * direction nudged one 8-way step either way (or not), step 2 (carrying:
 * A again). */
static void ground_land(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (!(m->pad & HZ_PAD_A)) {
        if (cell_occupied(c->g, m->x, m->y)) {
            ai->step = 2;
        } else {
            m->pad |= HZ_PAD_A;
        }
        return;
    }
    m->pad &= (u8)~HZ_PAD_A;                        /* .tapped */
    if (robot(m)) {
        ai->step = 2;
        return;
    }
    nudge_course_dir(c, ai_random(c), 0x80);        /* .still_jet */
    steer_pad(m, ai->pad_dir8);
    ai->step = 2;
    if (carrying(m)) {
        m->pad |= HZ_PAD_A;
    }
}

/* Step 5 AI_Ground_Walk: the direction from its centre to the goal's
 * (+8) is where it shoots (mech_ai_aim_dir). $30 px or more away, not
 * walking into something, and not on terrain event 6 on map 0: walk to
 * it; else stand and turn a step a frame towards it. B released, step 7. */
static void ground_walk(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    u16 dist;
    u8 dir = dir_and_dist((s16)(m->x + 0x10), (s16)(m->y + 0x10),
                          (s16)(ai->goal.x + 8), (s16)(ai->goal.y + 8),
                          &dist);
    ai->aim_dir = dir;
    bool stand = dist < 0x30 || walk_blocked(m)
              || (c->g->map == 0 && (m->terrain & 0xFF) == 6);
    if (!stand) {                                   /* .walk */
        steer_pad(m, (dir & 0x0F) >> 1);
    } else {                                        /* .stand */
        m->pad &= HZ_PAD_BUTTONS;
        u8 diff = (u8)(dir - m->dir);
        if (diff != 0) {
            u8 d = ((diff & 0x0F) <= 8) ? (u8)(m->dir + 1) : (u8)(m->dir - 1);
            m->dir = d & 0x0F;
            m->turn_to = m->dir;
        }
    }
    m->pad &= (u8)~HZ_PAD_B;                        /* .release_b */
    ai->step = 7;
}

/* Step 7 AI_Ground_Shoot: facing mech_ai_aim_dir: B (mech_ai_ground_shot
 * $FF), step 0; the next time, its result: an enemy unit hit: QUIRK: the
 * HP of unit mech_hit_victim is halved -- the victim is slot + 1, so it's
 * the next record (as UnitAI_CheckShotResult) -- unless it is inactive,
 * dying or on screen; and victims $32 and up count as team 2, though $32
 * is team 1's slot 49. FIX (HZ_FIX_CPU_HITS): the victim's own record and
 * team, slot + 1 taken back. The enemy mech hit: nothing more. Its own
 * side or an HQ hit: reposition (step 8). Three misses in a row (or a
 * walk blocked): reposition. Else step 0. */
static void ground_shoot(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (m->dir != ai->aim_dir) {                    /* .not_facing */
        if ((m->pad & HZ_PAD_DIRS) != 0 && walk_blocked(m)) {
            goto reposition;
        }
        goto reset_misses;
    }
    if (ai->ground_shot == 0) {
        m->pad ^= HZ_PAD_B;
        ai->ground_shot = 0xFF;
        ai->step = 0;
        return;
    }
    if (m->shot_flags & OWN_SHOT_OUT) {             /* .check_result */
        ai->step = 0;
        return;
    }
    ai->ground_shot = 0;
    if (!(m->shot_flags & OWN_SHOT_HIT)) {          /* .missed */
        if ((m->pad & HZ_PAD_DIRS) != 0 && walk_blocked(m)) {
            goto reposition;
        }
        ai->misses_left--;                          /* .count_miss */
        if (ai->misses_left != 0) {
            ai->step = 0;
            return;
        }
        goto reposition;
    }
    m->shot_flags &= (u8)~OWN_SHOT_HIT;
    {
        HzRef v = m->hit_victim;
        bool team2 = (m->flags & MECH_TEAM2) != 0;
        bool fixed = (c->g->fixes & HZ_FIX_CPU_HITS) != 0;
        bool vteam2;
        int kind = victim_code_kind(v, fixed, &vteam2);
        if (kind == 1) {
            if (vteam2 == team2) {
                goto reposition;
            }
            HzUnit *u = fixed ? ref_unit_ptr(c->g, v)   /* .halve_hp */
                              : victim_next_record(c->g, v);
            if (u != NULL && (u->flags & UNIT_ACTIVE)
                && !(u->flags & (UNIT_DYING | UNIT_ON_SCREEN))) {
                u->hp = (u16)(u->hp >> 1);
            }
            goto reset_misses;
        }
        if (kind == 2) {
            if (vteam2 == team2) {
                goto reposition;
            }
            goto reset_misses;
        }
        goto reposition;
    }
reset_misses:
    ai->misses_left = 3;
    ai->step = 0;
    return;
reposition:
    ai->misses_left = 3;
    ai->step = 8;
}

static void plan_ground(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (m->ammo == 0) {
        goto end;
    }
    if (ai->engage) {
        ai->engage = 0x80;
    }
    if (m->transform != 0) {                        /* .transforming */
        return;
    }
    switch (ai->step) {
    case 0:
        ground_find_target(c);
        return;
    case 1:
        ground_set_box(c);
        return;
    case 2:                                         /* AI_Ground_CheckForm */
        m->pad &= (u8)~HZ_PAD_A;
        if (robot(m)) {
            ai->step = 5;                           /* .on_foot */
            return;
        }
        if ((m->pad & HZ_PAD_DIRS) == 0) {
            int d = (int)(ai_random(c) & 7);
            ai->pad_dir8 = (u8)d;
            steer_pad(m, d);
        }
        ai->step = 3;
        return;
    case 3:
        ground_fly_in(c);
        return;
    case 4:
        ground_land(c);
        return;
    case 5:
        ground_walk(c);
        return;
    case 6:
        goto end;
    case 7:
        ground_shoot(c);
        return;
    case 8:                                         /* AI_Ground_Reposition */
        if (robot(m)) {
            toggle_a_and_unstick(c);
            return;
        }
        m->pad &= HZ_PAD_DIRS;                      /* .airborne */
        ai->step = 2;
        ai->frames_left = 0x18;
        return;
    default:
        return;         /* UNKNOWN: no step past 8 is set */
    }
end:                                                /* AI_Ground_End */
    if (!robot(m)) {
        resume_plan(c);
        return;
    }
    toggle_a_and_unstick(c);
}

/* -- plans 6/$E and 7/$F: deliver to an HQ or outpost ---------------------- */

/* The two copies differ only in their sizes (plan 7: a tighter ring). */
typedef struct {
    u16 outer;          /* the outer box: random $3F + this each side */
    u16 inner_lo;       /* inner box: goal - this .. */
    u16 inner_hi;       /*   goal + this */
    u16 margin;         /* drop early within mech_ai_engage_range + this */
    u32 nudge_bit;      /* step 2's random sign bit */
} DeliverSizes;

static const DeliverSizes DELIVER_HQ = { 0x80, 0x40, 0x60, 0xC1, 0x40 };
static const DeliverSizes DELIVER_POST = { 0x60, 0x20, 0x40, 0x60, 0x04 };

static s16 clamp_goal(s16 v)
{
    if (v < 0 || v < MAP_EDGE_LO) {
        return MAP_EDGE_LO;
    }
    if ((u16)v > MAP_EDGE_HI) {
        return MAP_EDGE_HI;
    }
    return v;
}

static s16 sub_min8(s16 v, u16 d)
{
    return (s16)((u16)v >= d && (u16)(v - d) >= MAP_EDGE_LO ? v - d
                                                            : MAP_EDGE_LO);
}

static s16 add_max(s16 v, u16 d)
{
    u16 r = (u16)(v + d);
    return (s16)(r > MAP_EDGE_HI ? MAP_EDGE_HI : r);
}

/* Step 0: the goal is the dest's place - 16 (QUIRK: both copies test for
 * plan 6, so plans 7, $E and $F take a random -64..63 each way instead),
 * clamped to 8..$7D8; the outer box goal - random .. goal + random, the
 * inner box round the goal, clamped too. */
static void deliver_set_zone(Cpu *c, const DeliverSizes *z)
{
    HzMechAI *ai = c->ai;
    s16 off_x = -0x10;
    s16 off_y = -0x10;
    if (ai->plan != PLAN_DELIVER_HQ) {
        u32 r = ai_random(c);
        off_y = half_s8((u8)r);
        off_x = half_s8((u8)(r >> 16));
    }
    u32 r = ai_random(c);                           /* .set_goal_x */
    u16 lo = (u16)((r >> 16) & 0x3F) + z->outer;    /* left and top */
    u16 hi = (u16)(r & 0x3F) + z->outer;            /* right and bottom */
    const HzPost *d = rec(c, ai->dest);
    s16 gx = clamp_goal((s16)(d->x + off_x));
    s16 gy = clamp_goal((s16)(d->y + off_y));
    ai->goal.x = gx;
    ai->goal.y = gy;
    ai->work.kind = WORK_BOX;
    ai->work.box.left = sub_min8(gx, lo);
    ai->work.box.right = add_max(gx, hi);
    ai->work.box.inner_left = sub_min8(gx, z->inner_lo);
    ai->work.box.inner_right = add_max(gx, z->inner_hi);
    ai->work.box.bottom = add_max(gy, hi);
    ai->work.box.top = sub_min8(gy, lo);
    ai->work.box.inner_top = sub_min8(gy, z->inner_lo);
    ai->work.box.inner_bottom = add_max(gy, z->inner_hi);
    ai->step++;
    ai->frames_left = 0;
}

/* The course (and its 8-way direction) for a delivery's fly-in. */
static void set_course_dir(Cpu *c, u8 dir)
{
    int d = (dir & 0x0F) >> 1;
    c->ai->pad_dir8 = (u8)d;
    c->ai->course = dir8_pad(d);
}

/* Step 1: not carrying any more: AI_ResumePlan. In the inner box: keep
 * flying (stopped: a random course, step 3). In the outer box: its frames
 * to fly on done: release A, step 2 (drop); else one fewer, stopped: a
 * random course, step 3. Outside: the course to the goal when stopped,
 * $80 px or closer, or 3+ steps off (QUIRK: not wrapped). Then within
 * mech_ai_engage_range + the margin of the goal with a missile homing in,
 * mech_ai_needs_service or mech_ai_engage: step 2 (drop now); else step
 * 3 (the enemy watch). */
static void deliver_fly_in(Cpu *c, const DeliverSizes *z)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (!carrying(m)) {
        resume_plan(c);
        return;
    }
    u16 x = (u16)m->x;
    u16 y = (u16)m->y;
    u16 dist = 0xFFFF;
    bool random_course = false;
    u32 r = 0;
    if (y >= (u16)ai->work.box.inner_top && y <= (u16)ai->work.box.inner_bottom
        && x >= (u16)ai->work.box.inner_left
        && x <= (u16)ai->work.box.inner_right) {
        if (m->pad & HZ_PAD_DIRS) {
            return;                                 /* .keep_flying */
        }
        r = ai_random(c);
        random_course = true;
    } else if (y >= (u16)ai->work.box.top && y <= (u16)ai->work.box.bottom
               && x >= (u16)ai->work.box.left
               && x <= (u16)ai->work.box.right) {
        if (ai->frames_left == 0) {                 /* .drop */
            m->pad &= (u8)~HZ_PAD_A;
            ai->step++;
            return;
        }
        ai->frames_left--;                          /* .count_down */
        r = ai_random(c);
        if (m->pad & HZ_PAD_DIRS) {
            /* CHANGED: the original goes on to the threat checks with d7
             * unset -- a value left over from whatever ran before; here
             * the distance to the goal, as head_for_goal measures it. */
            dir_and_dist(m->x, m->y, ai->goal.x, ai->goal.y, &dist);
        } else {
            r >>= 16;           /* (the swap: the seed's old high word) */
            random_course = true;
        }
    } else {                                        /* .head_for_goal */
        ai->frames_left = 0;
        u8 dir = dir_and_dist(m->x, m->y, ai->goal.x, ai->goal.y, &dist);
        bool keep = false;
        if ((m->pad & HZ_PAD_DIRS) != 0 && dist >= 0x80) {
            u8 d = (u8)(dir - m->dir);
            if ((s8)d < 0) {
                d = (u8)-d;
            }
            keep = d < 3;
        }
        if (!keep) {
            set_course_dir(c, dir);
        }
    }
    if (random_course) {
        set_course_dir(c, (u8)r);
        ai->step = 3;                               /* .to_watch */
        return;
    }
    u16 near = (u16)(ai->engage_range + z->margin);  /* .check_threats */
    if (dist < near && (m->missile_dirs != 0 || ai->needs_service
                        || ai->engage)) {
        m->pad &= (u8)~HZ_PAD_A;                    /* .drop */
        ai->step = 2;
        return;
    }
    ai->step = 3;
}

/* Step 2: tap A. Still carrying: the course nudged a step either way (or
 * not), 4 frames on, step 1. An empty jet: AI_ResumePlan. A robot: tap
 * again. */
static void deliver_drop(Cpu *c, const DeliverSizes *z)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (!(m->pad & HZ_PAD_A)) {
        m->pad |= HZ_PAD_A;
        return;
    }
    m->pad &= (u8)~HZ_PAD_A;                        /* .tapped */
    if (carrying(m)) {
        nudge_course_dir(c, ai_random(c), z->nudge_bit);
        ai->course = dir8_pad(ai->pad_dir8);
        ai->step = 1;
        ai->frames_left = 4;
        return;
    }
    if (m->form == HZ_FORM_JET) {                   /* .not_carrying */
        resume_plan(c);
    }
}

static void plan_deliver(Cpu *c, const DeliverSizes *z)
{
    switch (c->ai->step) {
    case 0:
        deliver_set_zone(c, z);
        break;
    case 1:
        deliver_fly_in(c, z);
        break;
    case 2:
        deliver_drop(c, z);
        break;
    case 3:
        watch_enemy(c, WATCH_DELIVER, 1);
        break;
    default:
        break;          /* UNKNOWN: no step past 3 is set */
    }
}

/* -- plan 8: collect a bought unit (AI_PlanPickUp) ----------------------- */

static bool pickup_post_ours(Cpu *c)
{
    const HzPost *p = rec(c, c->ai->pickup_post);
    return (p->flags & POST_USED)
        && (p->flags & (POST_OWNED | POST_TEAM2)) == own_code(c);
}

static void plan_pick_up(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->step) {
    case 0:                                         /* AI_PickUp_Start */
        if (carrying(m)) {
            ai->lifted = 0xFF;
            resume_plan(c);
            return;
        }
        ai->lifted = 0;
        ai->step++;
        return;
    case 1: {                                       /* AI_PickUp_SetCourse */
        /* UNKNOWN: carrying here resumes with mech_ai_lifted still 0 */
        if (carrying(m)) {
            resume_plan(c);
            return;
        }
        const HzPost *p = rec(c, ai->pickup_post);
        u16 dist;
        u8 dir = dir_and_dist(m->x, m->y, (s16)(p->x - 0x10),
                              (s16)(p->y - 0x10), &dist);
        course_to(c, dir, dist, 3, true);
        ai->step = 5;
        return;
    }
    case 2: {                                       /* AI_PickUp_CheckArrived */
        /* within $1C of the post - $10: ready: A (step 3); nothing on
         * order or short of money: resume; still building and the post
         * still ours: plan 5 round itself while it waits */
        const HzPost *p = rec(c, ai->pickup_post);
        if (!near_point(m->x, m->y, (s16)(p->x - 0x10), (s16)(p->y - 0x10),
                        0x1C)) {
            ai->step = 1;                           /* .not_there */
            return;
        }
        u8 s = m->prod_state;
        if (s == 0 || (s & 0x80)) {
            resume_plan(c);
            return;
        }
        if (s == 2) {                               /* .lift */
            m->pad |= HZ_PAD_A;
            ai->step++;
            return;
        }
        if (!pickup_post_ours(c)) {
            resume_plan(c);
            return;
        }
        m->pad = 0;
        ai->course = 0;
        ai->dropped = 0;
        ai->target = ref_none();
        ai->ground_shot = 0;
        ai->misses_left = 3;
        ai->frames_left = 0;
        ai->aim_dir = 0;
        ai->attack_here = 0xFF;
        ai->heading = 0xFF;
        call_plan(c, PLAN_GROUND);
        return;
    }
    case 3:                                         /* AI_PickUp_CheckLifted */
        m->pad &= HZ_PAD_DIRS;
        if (carrying(m)) {
            ai->lifted = 0xFF;
            resume_plan(c);
            return;
        }
        if (m->form != HZ_FORM_JET) {               /* .not_carrying */
            ai->step++;
            return;
        }
        if (!pickup_post_ours(c)) {                 /* .retry */
            resume_plan(c);
            return;
        }
        ai->step = 1;
        return;
    case 4:                                         /* AI_PickUp_TakeOff */
        if (carrying(m)) {
            ai->lifted = 0xFF;
            resume_plan(c);
            return;
        }
        if (m->form == HZ_FORM_JET) {               /* .airborne */
            m->pad = 0;
            ai->step = 1;
            return;
        }
        m->pad = (m->pad & HZ_PAD_A) ? 0 : HZ_PAD_A;
        return;
    case 5:
        watch_enemy(c, WATCH_PICK_UP, 2);
        return;
    default:
        return;         /* UNKNOWN: no step past 5 is set */
    }
}

/* -- plan $A: drop a defender beside an enemy (AI_PlanDeliverToEnemy) ----- */

static void to_enemy_fly_in(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (m->y >= ai->work.box.top && m->y <= ai->work.box.bottom
        && m->x >= ai->work.box.left && m->x <= ai->work.box.right) {
        if (ai->frames_left == 0) {                 /* .drop */
            m->pad &= (u8)~HZ_PAD_A;
            ai->step++;
            return;
        }
        ai->frames_left--;                          /* .count_down */
        u32 r = ai_random(c);
        if (m->pad & HZ_PAD_DIRS) {
            ai->step = 5;                           /* .to_watch */
            return;
        }
        set_course_dir(c, (u8)(r >> 16));          /* (the swap) */
        ai->step = 5;
        return;
    }
    ai->frames_left = 0;                            /* .head_for_goal */
    u8 dir = get_direction(m->x, m->y, ai->goal.x, ai->goal.y);
    set_course_dir(c, dir);
    /* QUIRK: the "distance" checked is d1 after GetDirection, which keeps
     * d1: the goal's x */
    u16 dist = (u16)ai->goal.x;
    u16 near = (u16)(ai->engage_range + 0x80);
    if (dist < near && (m->missile_dirs != 0 || ai->needs_service
                        || ai->engage)) {
        m->pad &= (u8)~HZ_PAD_A;                    /* .drop */
        ai->step++;
        return;
    }
    ai->step = 5;
}

static void plan_to_enemy(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->step) {
    case 0: {                                       /* AI_ToEnemy_FindTarget */
        /* the first enemy within 6 rings of the dest: step 1. None:
         * carrying, deliver it as usual (plan 6 at an HQ, 7 at an
         * outpost); else plan 5 at its end (take off, resume) */
        const HzPost *d = rec(c, ai->dest);
        Scan s;
        scan_round(c->g, d->x, d->y, 6, &s);
        HzRef id = first_enemy(c, &s);
        if (!ref_is_none(id)) {
            ai->target = id;
            ai->step++;
            return;
        }
        if (m->form < HZ_FORM_CARRYING) {           /* .no_enemy */
            ai->plan = PLAN_GROUND;
            ai->step = 6;
            return;
        }
        ai->plan = rec_is_hq(c, ai->dest) ? PLAN_DELIVER_HQ
                                          : PLAN_DELIVER_POST;
        ai->step = 0;
        return;
    }
    case 1: {                                       /* AI_ToEnemy_SetBox */
        /* the goal is the target's place; the box (goal & ~7) +/- $80,
         * clamped to 8..$7D8 */
        s16 x, y;
        bool st;
        target_pos(c, &x, &y, &st);
        ai->goal.x = x;
        ai->goal.y = y;
        s16 bx = (s16)((u16)x & 0xFFF8);
        s16 by = (s16)((u16)y & 0xFFF8);
        ai->work.kind = WORK_BOX;
        ai->work.box.left = sub_min8(bx, 0x80);
        ai->work.box.right = add_max(bx, 0x80);
        ai->work.box.top = sub_min8(by, 0x80);
        ai->work.box.bottom = add_max(by, 0x80);
        ai->step++;
        return;
    }
    case 2:                                         /* AI_ToEnemy_CheckForm */
        if (m->form < HZ_FORM_CARRYING) {           /* the unit is gone */
            ai->plan = PLAN_GROUND;
            ai->step = 5;
            return;
        }
        if ((m->pad & HZ_PAD_DIRS) == 0) {
            set_course_dir(c, (u8)((ai_random(c) & 7) << 1));
        }
        ai->step++;
        return;
    case 3:
        to_enemy_fly_in(c);
        return;
    case 4:                                         /* AI_ToEnemy_Drop */
        if (!(m->pad & HZ_PAD_A)) {
            m->pad |= HZ_PAD_A;
            return;
        }
        m->pad &= (u8)~HZ_PAD_A;
        if (m->form == HZ_FORM_JET) {
            resume_plan(c);                         /* .dropped */
            return;
        }
        {
            u32 r = ai_random(c);
            /* here the sign bit set means the other way */
            nudge_course_dir(c, r ^ 0x100, 0x100);
        }
        ai->course = dir8_pad(ai->pad_dir8);
        ai->step = 0;
        ai->frames_left = 6;
        return;
    case 5:
        watch_enemy(c, WATCH_TO_ENEMY, 3);
        return;
    default:
        return;         /* UNKNOWN: no step past 5 is set */
    }
}

/* -- plan $B: service an own unit (AI_PlanServiceUnit) ------------------- */

static HzUnit *service_unit(Cpu *c)
{
    HzUnit *u = ref_unit_ptr(c->g, c->ai->service_unit);
    return (u != NULL) ? u : unit_at(c->g, c->team, 0);    /* not reached */
}

/* A course to (x, y) as steps 0, 5 and 8 set it (distance: |dx| + |dy|),
 * then the enemy watch (step 11), then step `next`. False if the fuel
 * won't do (the caller resumes). */
static bool service_course(Cpu *c, s16 x, s16 y, bool round_trip, u8 next)
{
    HzMech *m = c->m;
    u8 dir = get_direction(m->x, m->y, x, y);
    u16 dist = manhattan(m->x, m->y, x, y);
    if (!(round_trip ? fuel_trip(c, dist) : fuel_px(c, dist))) {
        return false;
    }
    course_to(c, dir, dist, 2, false);
    c->ai->step = 0x0B;                             /* .to_watch */
    c->ai->watch_next = next;
    return true;
}

static void plan_service_unit(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->step) {
    case 0: {                                       /* AI_Service_GoToUnit */
        if (robot(m)) {
            ai->step = 3;
            return;
        }
        if (m->form != HZ_FORM_JET) {               /* .jet */
            resume_plan(c);
            return;
        }
        HzUnit *u = service_unit(c);
        if (!(u->flags & UNIT_ACTIVE)
            || (u->flags & (UNIT_DYING | UNIT_CARRIED | UNIT_HELD))) {
            resume_plan(c);
            return;
        }
        if (ai->hq_attack && u->order != ORDER_ATTACK_HQ) {
            /* during an HQ attack only units near the enemy HQ.
             * CHANGED: the original measures from the HQ to (the unit's
             * y, a stale d2) -- it loads the unit's y over its x; here
             * the unit's x and y */
            const HzPost *e = rec(c, REC_HQ(1 - c->team));
            u16 dist;
            dir_and_dist(e->x, e->y, u->x, u->y, &dist);
            if (dist >= 0x170) {
                resume_plan(c);
                return;
            }
        }
        if (!service_course(c, u->x, u->y, true, 1)) { /* .set_course */
            resume_plan(c);
        }
        return;
    }
    case 1: {                                       /* AI_Service_CheckAtUnit */
        HzUnit *u = service_unit(c);
        s16 dy = (s16)(m->y - u->y);
        s16 dx = (s16)(m->x - u->x);
        if (dy < 8 && dy > -0x18 && dx < 8 && dx > -0x18) {
            m->pad ^= HZ_PAD_A;
            ai->step++;
        } else {
            ai->step = 0;                           /* .not_there */
        }
        return;
    }
    case 2:                                         /* AI_Service_CheckLifted */
        m->pad &= (u8)~HZ_PAD_A;
        if (robot(m)) {
            ai->step++;
        } else if (carrying(m)) {                   /* .airborne */
            ai->step = (u8)(ai->step + 2);
            ai->unused_flag = 0xFF;
        } else {
            ai->step = 0;                           /* .retry */
        }
        return;
    case 3:                                         /* AI_Service_TakeOff */
        if (!(m->pad & HZ_PAD_A)) {
            m->pad |= HZ_PAD_A;
            return;
        }
        m->pad &= (u8)~HZ_PAD_A;                    /* .tapped */
        if (!robot(m)) {
            ai->step = 0;
            return;
        }
        if ((m->pad & HZ_PAD_DIRS) == 0                 /* .still_robot */
            || walk_blocked(m)) {
            m->pad = dir8_pad((int)(ai_random(c) & 7)); /* .random_dir */
        }
        return;
    case 4: {                                       /* AI_Service_PickPost */
        ai->work.kind = WORK_SERVICE;
        ai->work.service_stage = SERVICE_UNIT;
        ai->service_unit = ref_unit(c->team, m->carried_slot);
        int r = nearest_own_post(c);
        if (r < 0) {
            resume_plan(c);
            return;
        }
        ai->work.service.post = (u16)r;
        ai->work.service_stage = SERVICE_POST;
        ai->step++;
        return;
    }
    case 5: {                                       /* AI_Service_GoToPost */
        const HzPost *p = rec(c, ai->work.service.post % HZ_POST_CLEARED);
        if (!service_course(c, p->x, p->y, false, 6)) {
            resume_plan(c);
        }
        return;
    }
    case 6: {                                       /* AI_Service_CheckAtPost */
        const HzPost *p = rec(c, ai->work.service.post % HZ_POST_CLEARED);
        if ((p->flags & (POST_OWNED | POST_TEAM2)) == own_code(c)
            && in_arrival_box(m->x, m->y, p->x, p->y)) {
            m->pad = 0;
            ai->step++;
        } else {
            ai->step = 4;                           /* .not_there */
        }
        return;
    }
    case 7: {                                       /* AI_Service_Refill */
        /* QUIRK (a cheat, kept): the unit is filled straight up to its
         * type's maxima; only with the order menu open does it wait for
         * the outpost to have given it half of each */
        HzUnit *u = service_unit(c);
        const HzUnitStats *st = unit_stats_for(c->g, u->type);
        if ((m->flags & MECH_MENU_OPEN)
            && ((u16)(st->fuel >> 1) > u->fuel || (u16)(st->hp >> 1) > u->hp
                || (u8)(st->missiles >> 1) > u->missiles
                || (u8)(st->ammo >> 1) > u->ammo)) {
            ai->step = 6;                           /* .wait */
            return;
        }
        u->fuel = st->fuel;                         /* .refill */
        u->hp = st->hp;
        u->ammo = st->ammo;
        u->missiles = st->missiles;
        ai->step = 0x0C;
        ai->drop_tries = 0x10;
        return;
    }
    case 8: {                                       /* AI_Service_GoBack */
        if (!carrying(m)) {
            resume_plan(c);
            return;
        }
        HzPoint at = ai->work.service_return.at;
        if (!service_course(c, at.x, at.y, false, 9)) {
            resume_plan(c);
        }
        return;
    }
    case 9: {                                       /* AI_Service_CheckAtDrop */
        /* within -$18..+$8 of the return point: A (step 10). Within
         * -$58..+$48 after a failed drop: A one time in four, else stay.
         * Farther: back to step 8. mech_ai_needs_service: A at once.
         * UNKNOWN: farther away with the need, this sets step 9 again
         * every visit, so it may never reach step 10 while it lasts */
        HzPoint at = ai->work.service_return.at;
        s16 dy = (s16)(m->y - at.y);
        s16 dx = (s16)(m->x - at.x);
        if (dy < 8 && dy > -0x18 && dx < 8 && dx > -0x18) {
            m->pad |= HZ_PAD_A;
            ai->step++;
            return;
        }
        if (dy < 0x48 && dy > -0x58 && dx < 0x48 && dx > -0x58
            && ai->drop_tries != 0x10) {
            if ((ai_random(c) & 0x2080) == 0) {
                m->pad |= HZ_PAD_A;                 /* .press_a */
                ai->step++;
                return;
            }
        } else {
            ai->step = 8;                           /* .re_aim */
        }
        if (ai->needs_service) {                    /* .check_service */
            m->pad |= HZ_PAD_A;
            ai->step++;
        }
        return;
    }
    case 0x0A:                                      /* AI_Service_CheckDropped */
        m->pad &= (u8)~HZ_PAD_A;
        if (!carrying(m)) {
            resume_plan(c);
            return;
        }
        ai->drop_tries--;
        if (ai->drop_tries == 0) {
            resume_plan(c);
            return;
        }
        ai->course = dir8_pad((int)(ai_random(c) & 7));
        ai->step = 9;
        return;
    case 0x0B:
        watch_enemy(c, WATCH_SERVICE, ai->watch_next);
        return;
    case 0x0C: {                                    /* AI_Service_PickDropPoint */
        /* where to put it back: orders 0 and 2 stay carried (resume);
         * orders 3, 9, $A $80 px on towards their goal (unit +$42; none:
         * unit_turn_to); others along unit_turn_to if unit +$1D is set,
         * else where it was lifted (a carried unit keeps its x:y) */
        HzUnit *u = service_unit(c);
        u8 o = u->order;
        if (o == ORDER_ROAM || o == ORDER_PATROL) {
            resume_plan(c);
            return;
        }
        int dir = -1;
        if (o == ORDER_OUTPOST || o == ORDER_CAPTURE
            || o == ORDER_ATTACK_HQ) {              /* .toward_goal */
            dir = (u->ai.dest_dir != 0) ? u->ai.dest_dir - 1 : u->turn_to;
        } else if (u->terrain & 0xFF) {             /* .other_order */
            dir = u->turn_to;
        }
        ai->work.service_stage = SERVICE_RETURN;
        if (dir < 0) {
            ai->work.service_return.at.x = u->x;
            ai->work.service_return.at.y = u->y;
        } else {                                    /* .offset */
            const HzPoint *off = &AI_RETURN_OFFSETS[(dir & 0x0E) >> 1];
            ai->work.service_return.at.x = clamp_goal((s16)(u->x + off->x));
            ai->work.service_return.at.y = clamp_goal((s16)(u->y + off->y));
        }
        ai->step = 8;                               /* .next_step */
        m->pad &= (u8)~HZ_PAD_A;
        return;
    }
    default:
        return;         /* UNKNOWN: no step past $C is set */
    }
}

/* -- plan $D: launch a unit along a heading (AI_PlanLaunch) --------------- */

static void plan_launch(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->step) {
    case 0: {                                       /* AI_Launch_SetHeading */
        /* none: the 8-way direction to the enemy HQ */
        u8 h = ai->heading;
        if (h & 0x80) {
            const HzPost *e = rec(c, REC_HQ(1 - c->team));
            h = (u8)(get_direction(m->x, m->y, e->x, e->y) >> 1);
        }
        ai->heading = h;
        m->pad = dir8_pad(h);   /* UNKNOWN: a heading past 7 would read
                                   past AIPadDirs; none is set */
        ai->frames_left = 0;
        ai->failed_drops = 0;
        ai->step++;
        return;
    }
    case 1:                                         /* AI_Launch_Drop */
        if (ai->frames_left == 0) {
            m->pad |= HZ_PAD_A;
            ai->step++;
        } else {
            ai->frames_left--;
        }
        return;
    case 2:                                         /* AI_Launch_CheckDropped */
        /* still carrying: try again in 6 frames; after 12 tries turn the
         * pad round, after 24 fly back to the pickup post (then step 3) */
        m->pad &= (u8)~HZ_PAD_A;
        if (!carrying(m)) {
            resume_plan(c);
            return;
        }
        ai->step = 1;
        ai->frames_left = 6;
        ai->failed_drops++;
        if (ai->failed_drops == 0x0C) {
            m->pad = dir8_pad(((m->dir + 8) & 0x0F) >> 1);
            return;
        }
        if (ai->failed_drops == 0x18) {             /* .check_give_up */
            ai->step = 3;
            ai->goal.x = rec(c, ai->pickup_post)->x;
            ai->goal.y = rec(c, ai->pickup_post)->y;
            call_plan(c, PLAN_FLY_RESUME);
        }
        return;
    case 3:                                         /* AI_Launch_NextHeading */
        ai->heading = (u8)((ai->heading + 1) & 7);
        m->pad = dir8_pad(ai->heading);
        ai->frames_left = 0;
        ai->failed_drops = 0;
        ai->step = 1;
        return;
    default:
        return;         /* UNKNOWN: no step past 3 is set */
    }
}

/* -- plan $10: put an ST-57U on water (AI_PlanDeliverST) ------------------ */

static void deliver_st_look(Cpu *c)                 /* step 1 */
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    s16 x, y;
    if (scan_blocks(c, &x, &y)) {                   /* .found */
        ai->goal.x = x;
        ai->goal.y = y;
        m->pad = dir8_pad(get_direction(m->x, m->y, x, y) >> 1);
        ai->step = 2;
        return;
    }
    u8 d = (u8)((ai->scan_dir + 1) & 7);
    if (d != 0) {
        ai->scan_dir = d;
        return;
    }
    const HzPost *e = rec(c, REC_HQ(1 - c->team));  /* .none_found */
    m->pad = dir8_pad(get_direction(m->x, m->y, e->x, e->y) >> 1);
    ai->frames_left = 0x10;
    ai->step = 3;
}

static void plan_deliver_st(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    switch (ai->step) {
    case 0:                                         /* AI_DeliverST_Start */
        ai->curve = (ai_random(c) & 0x2010) ? 1 : -1;
        ai->scan_dir = 0;
        ai->aim_dir = 0;
        ai->step = 1;
        deliver_st_look(c);
        return;
    case 1:
        deliver_st_look(c);
        return;
    case 2: {                                       /* AI_DeliverST_FlyIn */
        /* $80+ px out: towards it; closer, 45 degrees off it (the side
         * mech_ai_curve says) while the count runs, then release A, step 4.
         * QUIRK: only the direction goes into the pad (the buttons kept
         * in the original's d1 are built and not used) */
        u16 dist;
        u8 dir = dir_and_dist(m->x, m->y, ai->goal.x, ai->goal.y, &dist);
        if (dist < 0x80) {
            dir = (u8)((dir + (ai->curve < 0 ? -2 : 2)) & 0x0F);
            if (ai->frames_left == 0) {
                m->pad &= (u8)~HZ_PAD_A;
                ai->step = 4;
            } else {
                ai->frames_left--;
            }
        }
        m->pad = dir8_pad(dir >> 1);                /* .set_pad */
        return;
    }
    case 3:                                         /* AI_DeliverST_TowardHQ */
        if (ai->frames_left == 0) {
            ai->step = 0;
        } else {
            ai->frames_left--;
        }
        return;
    case 4:                                         /* AI_DeliverST_Drop */
        m->pad |= HZ_PAD_A;
        ai->step = 5;
        return;
    case 5:                                         /* AI_DeliverST_CheckDropped */
        m->pad &= (u8)~HZ_PAD_A;
        if (!carrying(m)) {
            resume_plan(c);
            return;
        }
        ai->step = 2;
        ai->frames_left = 8;
        ai->failed_drops++;
        if (ai->failed_drops >= 0x10) {
            ai->step = 0;
        }
        return;
    default:
        return;         /* UNKNOWN: no step past 5 is set */
    }
}

/* -- plan $11: look over the own units (AI_PlanTendUnits) ----------------- */

/* The own HQ hurt or enemy units round it: resume. Away from the goal:
 * plan $C back to it. Else one own unit a frame (cursor mech_ai_tend_
 * cursor, slots 0-49, no distance test): HP under 1/4, no fuel, no ammo
 * (a SAM: no missiles; a GRM: no ammo or no missiles), or on terrain event
 * 6 on map 0: service it (plan $B). After slot 49: resume. */
static void plan_tend_units(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (ai->tend_cursor >= side_slots(c->g, c->team) || own_hq_hit(c)) {
        resume_plan(c);
        return;
    }
    u8 own, enemy;
    count_around(c, REC_HQ(c->team), &own, &enemy);
    if (enemy != 0) {
        resume_plan(c);
        return;
    }
    if (!in_arrival_box(m->x, m->y, ai->goal.x, ai->goal.y)) {
        call_plan(c, PLAN_FLY_RESUME);              /* .go_to_goal */
        return;
    }
    u32 slot = ai->tend_cursor;
    const HzUnit *u = own_unit(c, slot);
    ai->tend_cursor++;
    if (!(u->flags & UNIT_ACTIVE)
        || (u->flags & (UNIT_DYING | UNIT_CARRIED | UNIT_HELD))) {
        return;                                     /* .next_unit */
    }
    bool need;
    if ((u->terrain & 0xFF) == 6 && c->g->map == 0) {
        need = true;
    } else {                                        /* .check_needs */
        const HzUnitStats *st = unit_stats_for(c->g, u->type);
        need = (u16)(st->hp >> 2) > u->hp || u->fuel == 0;
        if (!need) {
            if (u->type == UNIT_SAM) {
                need = u->missiles == 0;
            } else if (u->ammo == 0) {
                need = true;
            } else if (u->type == UNIT_GRM) {
                need = u->missiles == 0;
            }
        }
    }
    if (!need) {
        return;
    }
    ai->work.kind = WORK_SERVICE;                   /* .service */
    ai->work.service_stage = SERVICE_UNIT;
    ai->service_unit = ref_unit(c->team, slot);
    call_plan(c, PLAN_SERVICE_UNIT);
}

/* ===================================================================== */
/* The top layer.                                                          */
/* ===================================================================== */

static void run_plan(Cpu *c)
{
    switch (c->ai->plan) {
    case PLAN_MODES:
        plan_modes(c);
        break;
    case PLAN_BUY:
        plan_buy(c);
        break;
    case PLAN_LIFT:
        plan_lift_or_drop(c, true);
        break;
    case PLAN_DROP:
        plan_lift_or_drop(c, false);
        break;
    case PLAN_FLY:
    case PLAN_FLY_SERVICE:
    case PLAN_FLY_RESUME:
        plan_fly(c);
        break;
    case PLAN_GROUND:
        plan_ground(c);
        break;
    case PLAN_DELIVER_HQ:
    case PLAN_DELIVER_HQ_X:
        plan_deliver(c, &DELIVER_HQ);
        break;
    case PLAN_DELIVER_POST:
    case PLAN_DELIVER_POST_X:
        plan_deliver(c, &DELIVER_POST);
        break;
    case PLAN_PICK_UP:
        plan_pick_up(c);
        break;
    case PLAN_TO_ENEMY:
        plan_to_enemy(c);
        break;
    case PLAN_SERVICE_UNIT:
        plan_service_unit(c);
        break;
    case PLAN_LAUNCH:
        plan_launch(c);
        break;
    case PLAN_DELIVER_ST:
        plan_deliver_st(c);
        break;
    case PLAN_TEND_UNITS:
        plan_tend_units(c);
        break;
    default:
        break;          /* UNKNOWN: no plan past $11 is set */
    }
}

/* AI_KeepOffEdges: a jet with a direction held, near a map edge (8 or
 * $7D8; $28 and $7B8 once it has steered here, mech_ai_edge_steer),
 * pressing outwards: turn the pad along the edge, away from the nearer
 * corner. Not for plan 5.
 * QUIRK: at the right and the top edge the corner test is inverted, so
 * it steers towards the corner there. */
static void keep_off_edges(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if ((m->flags & (MECH_MENU_OPEN | MECH_DESTROYED)) || robot(m)
        || ai->plan == PLAN_GROUND) {
        return;
    }
    u8 old = m->pad;
    u8 d = old & HZ_PAD_DIRS;
    if (d == 0) {
        return;
    }
    u16 lo = ai->edge_steer ? 0x28 : MAP_EDGE_LO;
    u16 hi = ai->edge_steer ? 0x7B8 : MAP_EDGE_HI;
    u16 x = (u16)m->x;
    u16 y = (u16)m->y;
    u8 dir = m->dir;
    enum { UP, DOWN, LEFT, RIGHT, NONE } turn = NONE;
    bool store = false;

    if (x < lo || x > hi) {                         /* .check_x */
        bool left = x < lo;
        u8 bit = left ? HZ_PAD_LEFT : HZ_PAD_RIGHT;
        if (d & bit) {
            d &= (u8)~bit;
            store = true;
            int q = (dir - (left ? 0x0C : 4)) & 0x0F;
            if (left) {                             /* .at_left */
                turn = (q >= 8) ? ((y > hi) ? UP : DOWN)
                                : ((y < lo) ? DOWN : UP);
            } else {                                /* .at_right */
                turn = (q >= 8) ? ((y > hi) ? DOWN : UP)
                                : ((y < lo) ? UP : DOWN);
            }
        }
    }
    if (!store) {                                   /* .check_y */
        if (y < lo) {                               /* .at_top */
            store = true;
            if (d & HZ_PAD_UP) {
                d &= (u8)~HZ_PAD_UP;
                turn = (dir >= 8) ? ((x > hi) ? RIGHT : LEFT)
                                  : ((x < lo) ? LEFT : RIGHT);
            }
        } else if (y > hi) {                        /* .at_bottom */
            store = true;
            if (d & HZ_PAD_DOWN) {
                d &= (u8)~HZ_PAD_DOWN;
                int q = (dir - 8) & 0x0F;
                turn = (q >= 8) ? ((x > hi) ? LEFT : RIGHT)
                                : ((x < lo) ? RIGHT : LEFT);
            }
        } else {                                    /* .off_edges */
            if (lo != MAP_EDGE_LO) {
                ai->edge_steer = 0;
            }
            return;
        }
    }
    switch (turn) {
    case UP:
        d = (u8)((d | HZ_PAD_UP) & ~HZ_PAD_DOWN);
        break;
    case DOWN:
        d = (u8)((d | HZ_PAD_DOWN) & ~HZ_PAD_UP);
        break;
    case LEFT:
        d = (u8)((d | HZ_PAD_LEFT) & ~HZ_PAD_RIGHT);
        break;
    case RIGHT:
        d = (u8)((d | HZ_PAD_RIGHT) & ~HZ_PAD_LEFT);
        break;
    default:
        break;
    }
    ai->edge_steer = old;                           /* .store_pad */
    m->pad = (u8)((d & HZ_PAD_DIRS) | (old & HZ_PAD_BUTTONS));
}

/* AI_MechThink_Alert: a negative alert not yet taken (plan 1 carries on
 * first): a robot taps A and keeps moving; a jet starts the HQ alarm
 * (strategy step 13, mech_ai_alert_taken $FF). */
static bool think_alert(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (ai->plan == PLAN_BUY) {
        return true;                                /* run the layers */
    }
    if (!robot(m)) {                                /* .start_hq_alarm */
        ai->plan = PLAN_MODES;
        ai->mode = 1;
        ai->alert_taken = 0xFF;
        ai->strategy = 0x0D;
        ai->move = 0;
        ai->fight_form = 0;
        return false;
    }
    if (!(m->pad & HZ_PAD_A)) {
        m->pad |= HZ_PAD_A;
        return false;
    }
    u8 pad = m->pad & (u8)~HZ_PAD_A;                /* .release_a */
    int d = unstick_dir(c);
    if (d >= 0) {
        pad = (u8)((pad & HZ_PAD_BUTTONS & ~HZ_PAD_A) /* .set_dir */
                   | dir8_pad(d));
    }
    m->pad = pad;
    return false;
}

/* AI_Respawned: once after a death: the counts, back to mode 1 step 17
 * (resume); 15 bought for the dest, or every third death, gives the dest
 * up (mark b5). */
static void respawned(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    ai->deaths++;
    ai->launch_deaths++;
    ai->respawn = 0;
    ai->detour = 0;
    ai->plan = PLAN_MODES;
    ai->mode = 1;
    ai->alert_taken = 0;
    ai->alert = 0;
    ai->refuel = 0;
    ai->dodge = 0;
    ai->unused_flag = 0xFF;
    ai->strategy = 0x11;
    bool give_up = ai->bought >= 15;
    if (!give_up && ai->deaths >= 3) {
        ai->deaths = 0;
        give_up = true;
    }
    if (give_up) {                                  /* .abandon_dest */
        ai->marks[ai->dest % HZ_AI_RECORDS] |= MARK_ABANDONED;
    }
    ai->bought = 0;                                 /* .clear */
    ai->reset_count = 0;
    ai->countdown = 0;
    ai->launch_at_dest = 0;
    ai->move = 0;
    m->pad = 0;
    ai->hq_attack = 0;
    ai->launch_at_from = 0;
}

static void think(Cpu *c)
{
    HzMech *m = c->m;
    HzMechAI *ai = c->ai;
    if (!(m->flags & MECH_ACTIVE) || destroyed(m)) {
        if (destroyed(m)) {                         /* AI_MechThink_Inactive */
            ai->respawn = 1;
            ai->detour = 0;
        }
        return;
    }
    if (ai->respawn) {
        respawned(c);
        return;
    }
    ai->frames++;
    if (ai->alert_taken == 0) {
        if (ai->alert & 0x80) {
            if (!think_alert(c)) {
                return;
            }
        } else if (ai->alert == ALERT_LAUNCH_POST && ai->plan == PLAN_MODES
                   && ai->mode == 1) {
            /* alert 1 while the strategy runs: step 14, the step to go
             * back to noted (UNKNOWN: and never restored) */
            ai->alert_taken = (u8)(ai->strategy + 1);
            ai->strategy = 0x0E;
            ai->mode = 1;
        }
    }
    MoveResult r = run_move_state(c);               /* AI_MechThink_RunLayers */
    if (r == SKIP_PLAN) {
        return;
    }
    if (r == RUN_PLAN) {
        run_plan(c);
    }
    keep_off_edges(c);
}

void mech_ai_think(HzGame *g, int p)
{
    Cpu c;
    c.g = g;
    c.m = &g->mech[p];
    c.rival = &g->mech[1 - p];
    c.ai = &c.m->ai;
    c.team = (c.m->flags & MECH_TEAM2) ? 1 : 0;
    c.kidou = &g->assets->mech_stats.stats;
    if (g->mech_ai_probe != NULL) {
        g->mech_ai_probe(g, p, false, g->mech_ai_probe_user);
    }
    think(&c);
    if (g->mech_ai_probe != NULL) {
        g->mech_ai_probe(g, p, true, g->mech_ai_probe_user);
    }
}
