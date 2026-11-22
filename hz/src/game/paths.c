/* (Session 71) The PATHS patch (HZ_FIX_PATHS): a unit sent to the enemy
 * HQ or to an outpost drives the ground's shortest way there, round
 * cliffs, water and walls, where the original drives straight at it (or,
 * for attack HQ, by the map's path hints) and turns a step aside at
 * random when it runs into something: in a bay that faces its goal it
 * turns aside and back for good. A slope that pushes it back never even
 * counts as running into something.
 *
 * The ground: a field a goal, a footprint and a top speed, the cost of the
 * shortest way from each place to the goal, made as the game starts
 * (paths_prepare) or the first time a unit wants it, and kept while the
 * map is played (it depends on the map alone). A place is the tile a
 * unit's tiles start from, as
 * obj_check_terrain tests them: 3 x 3 tiles below INFANTRY, 2 x 2 for
 * INFANTRY and FWA, with no b15 (a boat: 3 x 3 with b9). A step costs 10,
 * 14 diagonally (no cutting a corner of a wall, a damaging floor or a
 * current it couldn't go that way on), times the unit's speed over the
 * speed it keeps along the step: a current (its first event tile's, as
 * unit_move pushes it) speeds it down the slope, slows it up one and
 * bends it across one; one it can't hold its line against, or that
 * leaves it under a quarter of its speed, is no way. ABGRUND's slopes
 * (a push of 960 against a TAX-52's 384) are one way: down. A damaging
 * floor adds PATHS_BURN a step, slippery ground 15; slow ground slows it
 * as unit_move does. A step off a place near a wall, a damaging floor or
 * a current too strong to cross costs more (PATHS_EDGE1, 2): the middle
 * of a gap is the cheapest way through it. The goal is the places within
 * PATHS_HQ_R px of an HQ, PATHS_POST_R of an outpost; there the
 * original's own driving takes over (it parks or shoots from there).
 * docs/paths.md. */

#include "paths.h"

#include <stdlib.h>
#include <string.h>

#define PATHS_SIDE    256       /* places a side: the map's tiles */
#define PATHS_NONE    PATHS_NO_WAY  /* no way, or no ground */
#define PATHS_FIELDS  96        /* fields kept at once: 6 kinds and
                                   speeds a goal, 16 goals at most */
#define PATHS_AHEAD   12        /* steps down the field it looks */
#define PATHS_HQ_R    112       /* px round an HQ's middle: the goal */
#define PATHS_POST_R  64        /* ... an outpost's */
#define PATHS_WALL    (-1L)     /* a place's ground: can't stand there */
#define PATHS_EDGE1   8         /* a step's extra off a place 1 from a */
#define PATHS_EDGE2   3         /* ... 2 from a: a wall, a current it
                                   can't hold its line across, a
                                   damaging floor */
#define PATHS_BURN    300       /* a step's extra on a damaging floor ($30
                                   HP a tile: a TAX-52 dies in a few) */

/* Footprints, and how hard a current pushes (unit_move): FOOT is
 * INFANTRY, SMALL the FWA. */
enum { KIND_BIG, KIND_SMALL, KIND_FOOT, KIND_BOAT, KINDS };

typedef struct {
    u32 key;                    /* map + 1, speed, post, kind; 0 none */
    u32 used;                   /* when last wanted (the slot's reuse) */
    u16 cost[PATHS_SIDE][PATHS_SIDE];
} Field;

static Field fields[PATHS_FIELDS];
static u32 field_uses;

/* The place's first x (or y): (x + 4) / 8 is the tile. */
static s16 place_px(int t)
{
    return (s16)(t * 8 - 4);
}

/* The x (or y) in the middle of the place's 8: where a unit is aimed. */
static s16 place_mid(int t)
{
    return (s16)(t * 8);
}

static int place_of(s16 px)
{
    return (u16)(px + 4) >> 3;
}

static long isqrt_l(long n)
{
    long r = 0;
    long bit = 1L << 30;
    if (n <= 0) {
        return 0;
    }
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

static int unit_kind(const HzUnit *u)
{
    /* (session 72) the repair truck 3 x 3 tiles, a mechanic on foot
     * (types 10 and 11 come here only in a game with REPAIR) */
    return (u->flags & UNIT_BOAT) ? KIND_BOAT
           : (unit_tiles(u->type, true) == 3) ? KIND_BIG
           : unit_on_foot(u->type, true) ? KIND_FOOT : KIND_SMALL;
}

/* Place (tx, ty)'s ground for a unit of `kind`: PATHS_WALL, or the
 * terrain of its first event tile (row by row, as obj_check_terrain
 * finds it; 0 none). */
static long ground(const HzGame *g, int kind, int tx, int ty)
{
    if (tx < 1 || ty < 1 || tx > 0xFC || ty > 0xFC) {
        return PATHS_WALL;
    }
    int n = (kind == KIND_SMALL || kind == KIND_FOOT) ? 2 : 3;
    long event = 0;
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int x = tx + c;
            int y = ty + r;
            u16 lw = g->layout[(y >> 2) & 0x3F][(x >> 2) & 0x3F];
            u16 block = (u16)(lw & 0x1FF);
            u16 word = g->blocks[block][(y & 3) * 4 + (x & 3)];
            if ((kind == KIND_BOAT) ? !(word & TILE_WATER)
                                    : (word & TILE_BLOCKED)) {
                return PATHS_WALL;
            }
            if (event == 0 && (word & TILE_EVENT)) {
                event = g->block_terrain[block];
            }
        }
    }
    return event;
}

void paths_push(const HzUnit *u, long *px, long *py)
{
    u8 push = u->push;
    *px = 0;
    *py = 0;
    if (push == 0 || (push & 0x80)) {
        return;                         /* none, or slow or slippery */
    }
    long amount = (long)(push & 0x0F)
                  << (unit_on_foot(u->type, true) ? 2 : 6);
    u16 code = (u16)((push >> 4) & 7);
    if (code != 0 && code != 4) {
        *px = (code < 4) ? amount : -amount;
    }
    code = (u16)(code - 2);
    if (code != 0 && code != 4) {
        *py = (code < 4) ? amount : -amount;
    }
}

/* The push of current `event` (terrain type 6) on a unit of `kind`, as
 * unit_move adds it: (*px, *py). */
static void event_push(long event, int kind, long *px, long *py)
{
    HzUnit probe;
    memset(&probe, 0, sizeof probe);
    probe.type = (kind == KIND_FOOT) ? UNIT_INFANTRY : 0;
    probe.push = (u8)((event >> 8) & 0x7F);
    paths_push(&probe, px, py);
}

/* The speed a unit of `kind` and top speed v (x 32, as Unit_SetSprite's
 * velocity) keeps going (dx, dy) on ground `event`, steering into a
 * current to hold its line (as path_steer does); 0 it can't: a current
 * against it, too strong across it, or leaving it under v / 4. */
static long progress(long event, long dx, long dy, long v, int kind)
{
    long len = isqrt_l(dx * dx + dy * dy);
    long keep = v;
    if (len == 0) {
        return 0;
    }
    switch (event & 0x0F) {
    case 0x6: {
        long px;
        long py;
        event_push(event, kind, &px, &py);
        long along = (px * dx + py * dy) / len;
        long across = px * px + py * py - along * along;
        if (across * 4 > v * v * 3) {
            return 0;                   /* across: over 0.87 v */
        }
        keep = along + isqrt_l(v * v - across);
        break;
    }
    case 0xA:
        switch ((event >> 8) & 3) {     /* unit_move's slow ground */
        case 1:
            keep = v - (v >> 2);
            break;
        case 2:
            keep = v >> 1;
            break;
        case 3:
            keep = v >> 2;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return (keep * 4 < v) ? 0 : keep;
}

/* Is ground `event` one to keep off, for a unit of `kind` and top speed
 * v: no ground, a damaging floor, or a current too strong to hold its
 * line across (it is swept along it)? */
static bool hazard(long event, long v, int kind)
{
    if (event == PATHS_WALL || (event & 0x0F) == 0x8) {
        return true;
    }
    if ((event & 0x0F) != 0x6) {
        return false;
    }
    long px;
    long py;
    event_push(event, kind, &px, &py);
    return (px * px + py * py) * 4 > v * v * 3;
}

/* A step of `len` (10, 14) going (dx, dy) on ground `event`: its cost; 0
 * no way. */
static u32 step_cost(long event, int dx, int dy, int len, long v, int kind)
{
    long keep = progress(event, dx * 256L, dy * 256L, v, kind);
    if (keep == 0) {
        return 0;
    }
    u32 cost = (u32)((len * v + keep / 2) / keep);
    switch (event & 0x0F) {
    case 0x8:
        cost += PATHS_BURN;             /* VULKAN's damaging floor */
        break;
    case 0x9:
        cost += 15;                     /* slippery */
        break;
    default:
        break;
    }
    return cost < 1 ? 1 : cost;
}

/* May a diagonal step going (dx, dy) cut past a corner on ground
 * `event`: one it could stand on, no damaging floor, and no current it
 * couldn't go that way on (it is on it a moment, as it passes)? */
static bool corner_ok(long event, int dx, int dy, long v, int kind)
{
    return event != PATHS_WALL && (event & 0x0F) != 0x8
           && progress(event, dx * 256L, dy * 256L, v, kind) != 0;
}

/* A ground's steps, for one field's kind and speed: a step's cost each
 * way off it (0 no way; [dir_of(dx, dy)]), whether a diagonal step may
 * cut past a corner on it, whether it is one to keep off. Worked out once
 * a ground a field, not once a step. */
typedef struct {
    long event;
    u32  step[9];
    bool corner[9];
    bool risk;
} Ground;

static int dir_of(int dx, int dy)
{
    return (dy + 1) * 3 + dx + 1;
}

static void ground_steps(Ground *t, long event, long v, int kind)
{
    t->event = event;
    t->risk = hazard(event, v, kind);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int i = dir_of(dx, dy);
            bool ok = event != PATHS_WALL && (dx != 0 || dy != 0);
            t->step[i] = ok ? step_cost(event, dx, dy,
                                        (dx != 0 && dy != 0) ? 14 : 10,
                                        v, kind)
                            : 0;
            t->corner[i] = ok && corner_ok(event, dx, dy, v, kind);
        }
    }
}

/* The field from the goal round post p out over the whole map, for a unit
 * of `kind` and top speed v: Dijkstra with a bucket a cost (a step costs
 * under PATHS_BUCKETS: 14 x 4 + PATHS_BURN + PATHS_EDGE1 at most), each
 * place's ground worked out once (grounds[], a few dozen a map). */
#define PATHS_BUCKETS 1024
#define PATHS_GROUNDS 1024      /* a map's blocks' terrains, and none */
/* queued at most: the goal's places, and a place 8 times */
#define PATHS_QUEUED  (PATHS_SIDE * PATHS_SIDE * 9)

static void field_make(const HzGame *g, Field *f, int post, int kind,
                       long v)
{
    static Ground grounds[PATHS_GROUNDS];
    static u16 at[PATHS_SIDE][PATHS_SIDE];      /* the place's grounds[] */
    static u8 near1[PATHS_SIDE][PATHS_SIDE];
    static u8 rows[PATHS_SIDE][PATHS_SIDE];
    static u8 edge[PATHS_SIDE][PATHS_SIDE];
    static u32 bucket[PATHS_BUCKETS];           /* its first queued + 1 */
    static u32 queued_next[PATHS_QUEUED];
    static u16 queued_place[PATHS_QUEUED];
    const HzPost *p = &g->posts[post];
    int r = (post >= HZ_POST_HQ) ? PATHS_HQ_R : PATHS_POST_R;
    int n_grounds = 0;
    int last = -1;
    u32 queued = 0;
    u32 waiting = 0;
    memset(bucket, 0, sizeof bucket);
    for (int ty = 0; ty < PATHS_SIDE; ty++) {
        for (int tx = 0; tx < PATHS_SIDE; tx++) {
            long event = ground(g, kind, tx, ty);
            if (last < 0 || grounds[last].event != event) {
                last = -1;
                for (int i = 0; i < n_grounds && last < 0; i++) {
                    last = (grounds[i].event == event) ? i : -1;
                }
                if (last < 0) {
                    /* (a block's terrain or none: never past the end) */
                    last = n_grounds++;
                    ground_steps(&grounds[last], event, v, kind);
                }
            }
            at[ty][tx] = (u16)last;
            f->cost[ty][tx] = PATHS_NONE;
            if (event == PATHS_WALL) {
                continue;
            }
            int dx = place_px(tx) + 12 - p->x;
            int dy = place_px(ty) + 12 - p->y;
            if (dx * dx + dy * dy <= r * r) {
                f->cost[ty][tx] = 0;
                queued_place[queued] = (u16)(ty << 8 | tx);
                queued_next[queued] = bucket[0];
                bucket[0] = ++queued;
                waiting++;
            }
        }
    }
    /* The edges, kept off: the extra a step from a place 1 or 2 from a
     * place to keep off (the middle of a gap the cheapest): the places to
     * keep off grown by 1 (near1) and by 1 again, rows then columns. */
    for (int pass = 0; pass < 2; pass++) {
        for (int ty = 0; ty < PATHS_SIDE; ty++) {
            for (int tx = 0; tx < PATHS_SIDE; tx++) {
                bool on = false;
                for (int x = tx - 1; x <= tx + 1; x++) {
                    if (x >= 0 && x < PATHS_SIDE) {
                        on |= pass ? near1[ty][x] != 0
                                   : grounds[at[ty][x]].risk;
                    }
                }
                rows[ty][tx] = on;
            }
        }
        for (int ty = 0; ty < PATHS_SIDE; ty++) {
            for (int tx = 0; tx < PATHS_SIDE; tx++) {
                bool on = false;
                for (int y = ty - 1; y <= ty + 1; y++) {
                    if (y >= 0 && y < PATHS_SIDE) {
                        on |= rows[y][tx] != 0;
                    }
                }
                if (pass == 0) {
                    near1[ty][tx] = on;
                } else {
                    edge[ty][tx] = near1[ty][tx] ? PATHS_EDGE1
                                   : on ? PATHS_EDGE2 : 0;
                }
            }
        }
    }
    static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    for (u32 cost = 0; waiting > 0; cost++) {
        u32 *head = &bucket[cost % PATHS_BUCKETS];
        while (*head != 0) {
            u32 q = *head - 1;
            *head = queued_next[q];
            waiting--;
            int tx = queued_place[q] & 0xFF;
            int ty = queued_place[q] >> 8;
            if (cost != f->cost[ty][tx]) {
                continue;               /* (found cheaper since) */
            }
            for (int d = 0; d < 8; d++) {
                /* The step from (nx, ny) to here, on (nx, ny)'s ground. */
                int nx = tx + DX[d];
                int ny = ty + DY[d];
                if (nx < 0 || ny < 0 || nx >= PATHS_SIDE
                    || ny >= PATHS_SIDE) {
                    continue;
                }
                int dir = dir_of(-DX[d], -DY[d]);
                u32 step = grounds[at[ny][nx]].step[dir];
                if (step == 0) {
                    continue;           /* no ground, or no way that way */
                }
                if (d >= 4 && (!grounds[at[ty][nx]].corner[dir]
                               || !grounds[at[ny][tx]].corner[dir])) {
                    continue;           /* no cutting a corner */
                }
                u32 next = cost + step + edge[ny][nx];
                if (next < PATHS_NONE && next < f->cost[ny][nx]) {
                    f->cost[ny][nx] = (u16)next;
                    queued_place[queued] = (u16)(ny << 8 | nx);
                    queued_next[queued] = bucket[next % PATHS_BUCKETS];
                    bucket[next % PATHS_BUCKETS] = ++queued;
                    waiting++;
                }
            }
        }
    }
}

/* A unit's top speed, x 32 (Unit_SetSprite's velocity). */
static long top_speed(const HzUnit *u)
{
    return (long)u->max_speed * 32;
}

/* Has map g->map a current anywhere (terrain type 6 on an event tile)?
 * Only there do a unit's speed and an INFANTRY's lighter push change a
 * field. Kept a map (it depends on the map alone). */
static bool map_currents(const HzGame *g)
{
    static u8 known[HZ_MAPS];           /* 0 not yet, 1 none, 2 some */
    int m = (g->map < HZ_MAPS) ? g->map : 0;
    if (known[m] == 0) {
        known[m] = 1;
        for (int i = 0; i < 64 * 64 && known[m] == 1; i++) {
            u16 block = (u16)(g->layout[i / 64][i % 64] & 0x1FF);
            if ((g->block_terrain[block] & 0x0F) != 0x6) {
                continue;
            }
            for (int t = 0; t < 16; t++) {
                if (g->blocks[block][t] & TILE_EVENT) {
                    known[m] = 2;
                }
            }
        }
    }
    return known[m] == 2;
}

static const Field *field_of(const HzGame *g, int post, const HzUnit *u)
{
    int kind = unit_kind(u);
    u32 speed = u->max_speed;
    if (!map_currents(g)) {
        speed = 0;                      /* the same field for each */
        kind = (kind == KIND_FOOT) ? KIND_SMALL : kind;
    }
    u32 key = (u32)(g->map + 1) | speed << 8 | (u32)post << 16
              | (u32)kind << 24;
    Field *pick = &fields[0];
    for (int i = 0; i < PATHS_FIELDS; i++) {
        if (fields[i].key == key) {
            fields[i].used = ++field_uses;
            return &fields[i];
        }
        if (fields[i].used < pick->used) {
            pick = &fields[i];
        }
    }
    field_make(g, pick, post, kind, top_speed(u));
    pick->key = key;
    pick->used = ++field_uses;
    return pick;
}

void paths_prepare(const HzGame *g)
{
    for (int post = 0; post < HZ_POST_CLEARED; post++) {
        if (!(g->posts[post].flags & POST_USED)) {
            continue;
        }
        for (int type = 0; type <= UNIT_FWA; type++) {
            HzUnit probe;
            memset(&probe, 0, sizeof probe);
            probe.type = (u8)type;
            probe.flags = (type == UNIT_ST) ? UNIT_BOAT : 0;
            probe.max_speed = unit_stats_for(g, (u8)type)->speed;
            if (type != UNIT_SUPPLY && probe.max_speed != 0) {
                field_of(g, post, &probe);
            }
        }
    }
}

/* (Session 72) REPAIR: the repair truck's field to each goal, as
 * paths_prepare makes the others' (its drives home go by them whether or
 * not PATHS is on). */
void paths_prepare_repair(const HzGame *g)
{
    for (int post = 0; post < HZ_POST_CLEARED; post++) {
        if (!(g->posts[post].flags & POST_USED)) {
            continue;
        }
        HzUnit probe;
        memset(&probe, 0, sizeof probe);
        probe.type = UNIT_REPAIR;
        probe.max_speed = unit_stats_for(g, UNIT_REPAIR)->speed;
        if (probe.max_speed != 0) {
            field_of(g, post, &probe);
        }
    }
}

/* Can unit u drive from (x0, y0) to (x1, y1) (its x, y) in a straight
 * line: every place it crosses, or would 1 px to a side of it, on the
 * field's ground, no current there in its way, and no damaging floor but
 * the way's own (the n places path_x, path_y; and where it is)? */
static bool clear_line(const HzGame *g, const Field *f, const HzUnit *u,
                       s16 x0, s16 y0, s16 x1, s16 y1, const int *path_x,
                       const int *path_y, int n)
{
    static const int SIDE_X[4] = { -1, 1, -1, 1 };
    static const int SIDE_Y[4] = { -1, -1, 1, 1 };
    long dx = x1 - x0;
    long dy = y1 - y0;
    long len = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    int kind = unit_kind(u);
    int seen[8];                        /* the last 8 places tested */
    int seen_n = 0;
    int seen_at = 0;
    for (long t = 0; t <= len; t++) {
        long x = x0 + (len ? dx * t / len : 0);
        long y = y0 + (len ? dy * t / len : 0);
        for (int c = 0; c < 4; c++) {
            int tx = place_of((s16)(x + SIDE_X[c]));
            int ty = place_of((s16)(y + SIDE_Y[c]));
            int key = ty << 8 | tx;
            bool done = false;
            for (int i = 0; i < seen_n && !done; i++) {
                done = seen[i] == key;
            }
            if (done) {
                continue;
            }
            seen[seen_at] = key;
            seen_at = (seen_at + 1) & 7;
            seen_n = (seen_n < 8) ? seen_n + 1 : 8;
            if (tx >= PATHS_SIDE || ty >= PATHS_SIDE
                || f->cost[ty][tx] == PATHS_NONE) {
                return false;
            }
            long event = ground(g, kind, tx, ty);
            if (event == 0) {
                continue;
            }
            if (progress(event, dx, dy, top_speed(u), kind) == 0) {
                return false;
            }
            bool way = tx == place_of(x0) && ty == place_of(y0);
            for (int i = 0; i < n && !way; i++) {
                way = tx == path_x[i] && ty == path_y[i];
            }
            if ((event & 0x0F) == 0x8 && !way) {
                return false;
            }
        }
    }
    return true;
}

/* Unit u's field for post `post` and its place on it, (*x, *y): NULL
 * when the post isn't on the map or u can't move. Off the field's ground (put down on a
 * place it can't stand on, or its place's rounding) *off is set and the
 * place is the cheapest next to it, if any. */
static const Field *field_at(const HzGame *g, const HzUnit *u, int post,
                             int *x, int *y, bool *off)
{
    if (post < 0 || post >= HZ_POST_CLEARED
        || !(g->posts[post].flags & POST_USED) || u->max_speed == 0) {
        return NULL;                    /* (a unit that can't move) */
    }
    const Field *f = field_of(g, post, u);
    int px = place_of(u->x);
    int py = place_of(u->y);
    *x = px;
    *y = py;
    *off = false;
    if (px >= PATHS_SIDE || py >= PATHS_SIDE
        || f->cost[py][px] != PATHS_NONE) {
        return f;
    }
    *off = true;
    u16 best = PATHS_NONE;
    for (int ny = py - 1; ny <= py + 1; ny++) {
        for (int nx = px - 1; nx <= px + 1; nx++) {
            if (nx >= 0 && ny >= 0 && nx < PATHS_SIDE && ny < PATHS_SIDE
                && f->cost[ny][nx] < best) {
                best = f->cost[ny][nx];
                *x = nx;
                *y = ny;
            }
        }
    }
    return f;
}

u16 paths_cost(const HzGame *g, const HzUnit *u, int post)
{
    int x;
    int y;
    bool off;
    const Field *f = field_at(g, u, post, &x, &y, &off);
    if (f == NULL || x >= PATHS_SIDE || y >= PATHS_SIDE) {
        return PATHS_NONE;
    }
    return f->cost[y][x];
}

bool paths_next(const HzGame *g, const HzUnit *u, int post, s16 *wx,
                s16 *wy)
{
    int x;
    int y;
    bool off;
    const Field *f = field_at(g, u, post, &x, &y, &off);
    if (f == NULL || x >= PATHS_SIDE || y >= PATHS_SIDE
        || f->cost[y][x] == PATHS_NONE) {
        return false;
    }
    if (off) {
        *wx = place_mid(x);             /* onto the ground first */
        *wy = place_mid(y);
        return true;
    }
    if (f->cost[y][x] == 0) {
        return false;                   /* there: the original's way */
    }
    int kind = unit_kind(u);
    int path_x[PATHS_AHEAD];
    int path_y[PATHS_AHEAD];
    int n = 0;
    while (n < PATHS_AHEAD && f->cost[y][x] != 0) {
        /* The step the field's cost came by: the least step + cost. */
        long event = ground(g, kind, x, y);
        u32 best = PATHS_NONE;          /* (+ the place's edge: the same
                                           for each step) */
        int bx = x;
        int by = y;
        for (int ny = y - 1; ny <= y + 1; ny++) {
            for (int nx = x - 1; nx <= x + 1; nx++) {
                if (nx < 0 || ny < 0 || nx >= PATHS_SIDE || ny >= PATHS_SIDE
                    || (nx == x && ny == y)
                    || f->cost[ny][nx] == PATHS_NONE) {
                    continue;
                }
                if (nx != x && ny != y
                    && (!corner_ok(ground(g, kind, nx, y), nx - x, ny - y,
                                   top_speed(u), kind)
                        || !corner_ok(ground(g, kind, x, ny), nx - x,
                                      ny - y, top_speed(u), kind))) {
                    continue;           /* (no cutting a corner) */
                }
                u32 step = step_cost(event, nx - x, ny - y,
                                     (nx != x && ny != y) ? 14 : 10,
                                     top_speed(u), kind);
                if (step != 0 && step + f->cost[ny][nx] < best) {
                    best = step + f->cost[ny][nx];
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
    if (n == 0) {
        return false;
    }
    int pick = 0;
    for (int k = n - 1; k > 0; k--) {
        if (clear_line(g, f, u, u->x, u->y, place_mid(path_x[k]),
                       place_mid(path_y[k]), path_x, path_y, n)) {
            pick = k;
            break;
        }
    }
    *wx = place_mid(path_x[pick]);
    *wy = place_mid(path_y[pick]);
    return true;
}
