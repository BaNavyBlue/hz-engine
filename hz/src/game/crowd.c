/* (Session 61) The grid with UNITS: any number of units a cell.
 *
 * The original's v_grid holds one object a 32 px cell, and a unit whose
 * centre falls in a taken cell is in a collision whatever it overlaps;
 * so once UNITS lifts the 50 records, the grid is what bounds a side
 * (3,000-4,000 units a map). With UNITS (g->crowd) v_grid stays empty
 * and each cell has a list a side of the units whose centre is in it,
 * linked through the records by slot + 1 (so a zeroed game is an empty
 * grid), and the list's length; a mech has its cell. What looks at the
 * grid -- the collisions, the shots, the scans, the radar, the AIs -- walks
 * those lists. Units block each other by the original's box test, without
 * its rule that a centre in a taken cell is a collision, so a side has no
 * limit but room: where none is left the jet can't put a unit down.
 * (Session 61 let a side's own units pass through each other, so a crowd
 * could stand anywhere; the owner, seeing tanks stacked behind a cliff,
 * session 68: "lets not allow Units to pass through each other ... the
 * game should self limit itself".) With UNITS off nothing here runs: the
 * grid and every test on it are the original's.
 *
 * A unit's collision with other units (crowd_hit) has a grid
 * of its own, 8 px cells (HzCrowdFine), each with a box round its units'
 * places: crowds of thousands stand at a front, many deep on the same few
 * pixels, and a unit beside one would look at every unit of it each move.
 * A cell whose box can't reach the unit is passed over; one looked
 * through to its end without a hit gets its box made tight again (a box
 * only grows as units come, so it is never too small). The boxes decide
 * nothing but what is looked at: the hits are what every unit's own test
 * says. */

#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clamp_grid(int v)
{
    return v < 0 ? 0 : (v >= HZ_GRID ? HZ_GRID - 1 : v);
}

u16 crowd_cell_of(s16 x, s16 y)
{
    return (u16)(clamp_grid(y >> 5) * HZ_GRID + clamp_grid(x >> 5));
}

static int clamp_fine(int v)
{
    return v < 0 ? 0 : (v >= HZ_FINE ? HZ_FINE - 1 : v);
}

/* The fine cell of a point: row x 256 + column, clamped to the map. */
static u32 fine_at(int x, int y)
{
    return (u32)(clamp_fine(y >> 3) * HZ_FINE + clamp_fine(x >> 3));
}

static const HzUnit *fine_first(const HzGame *g, int side, u32 cell)
{
    u32 head = g->crowd_fine->head[side][cell];
    return head != 0 ? unit_at_c(g, side, head - 1) : NULL;
}

static const HzUnit *fine_after(const HzGame *g, const HzUnit *u)
{
    return u->fine_next != 0 ? unit_at_c(g, u->side, u->fine_next - 1)
                             : NULL;
}

/* Box b grown to take unit u (b empty: just u). */
static void box_take(HzFineBox *b, const HzUnit *u, bool empty)
{
    if (empty) {
        b->x0 = b->x1 = u->x;
        b->y0 = b->y1 = u->y;
        b->size = u->size;
        return;
    }
    b->x0 = u->x < b->x0 ? u->x : b->x0;
    b->x1 = u->x > b->x1 ? u->x : b->x1;
    b->y0 = u->y < b->y0 ? u->y : b->y0;
    b->y1 = u->y > b->y1 ? u->y : b->y1;
    b->size = u->size > b->size ? u->size : b->size;
}

void crowd_link(HzGame *g, HzUnit *u)
{
    crowd_unlink(g, u);
    u16 half = (u16)(u->size >> 1);
    s16 cx = (s16)(u->x + half);
    s16 cy = (s16)(u->y + half);
    u16 cell = crowd_cell_of(cx, cy);
    int side = u->side;
    u32 me = u->slot + 1;
    u32 head = g->crowd_head[side][cell];
    u->crowd_prev = 0;
    u->crowd_next = head;
    if (head != 0) {
        unit_at(g, side, head - 1)->crowd_prev = me;
    }
    g->crowd_head[side][cell] = me;
    g->crowd_count[side][cell]++;
    u->crowd_cell = (u16)(cell + 1);
    u->grid_cell = cell;

    HzCrowdFine *f = g->crowd_fine;
    u32 fine = fine_at(cx, cy);
    head = f->head[side][fine];
    u->fine_prev = 0;
    u->fine_next = head;
    if (head != 0) {
        unit_at(g, side, head - 1)->fine_prev = me;
    }
    f->head[side][fine] = me;
    box_take(&f->box[side][fine], u, head == 0);
    u->fine_cell = fine + 1;
}

void crowd_unlink(HzGame *g, HzUnit *u)
{
    if (u->crowd_cell == 0) {
        return;
    }
    u16 cell = (u16)(u->crowd_cell - 1);
    int side = u->side;
    if (u->crowd_prev != 0) {
        unit_at(g, side, u->crowd_prev - 1)->crowd_next = u->crowd_next;
    } else {
        g->crowd_head[side][cell] = u->crowd_next;
    }
    if (u->crowd_next != 0) {
        unit_at(g, side, u->crowd_next - 1)->crowd_prev = u->crowd_prev;
    }
    g->crowd_count[side][cell]--;
    u->crowd_cell = 0;
    u->crowd_next = 0;
    u->crowd_prev = 0;

    u32 fine = u->fine_cell - 1;
    if (u->fine_prev != 0) {
        unit_at(g, side, u->fine_prev - 1)->fine_next = u->fine_next;
    } else {
        g->crowd_fine->head[side][fine] = u->fine_next;
    }
    if (u->fine_next != 0) {
        unit_at(g, side, u->fine_next - 1)->fine_prev = u->fine_prev;
    }
    u->fine_cell = 0;
    u->fine_next = 0;
    u->fine_prev = 0;
}

const HzUnit *crowd_first(const HzGame *g, int side, u16 cell)
{
    u32 head = g->crowd_head[side][cell];
    return head != 0 ? unit_at_c(g, side, head - 1) : NULL;
}

const HzUnit *crowd_after(const HzGame *g, const HzUnit *u)
{
    return u->crowd_next != 0 ? unit_at_c(g, u->side, u->crowd_next - 1)
                              : NULL;
}

bool crowd_mech_in(const HzGame *g, int p, u16 cell)
{
    return g->mech[p].crowd_cell == cell + 1;
}

bool crowd_cell_taken(const HzGame *g, u16 cell)
{
    return g->crowd_count[0][cell] != 0 || g->crowd_count[1][cell] != 0
        || crowd_mech_in(g, 0, cell) || crowd_mech_in(g, 1, cell);
}

int crowd_cells_round(u16 cell, u16 out[9])
{
    int row = cell / HZ_GRID;
    int col = cell % HZ_GRID;
    int n = 0;
    out[n++] = cell;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int r = row + dr;
            int c = col + dc;
            if ((dr == 0 && dc == 0) || r < 0 || r >= HZ_GRID || c < 0
                || c >= HZ_GRID) {
                continue;
            }
            out[n++] = (u16)(r * HZ_GRID + c);
        }
    }
    return n;
}

/* The overlap test of Unit_CheckSpawnBlocked and Unit_CheckObjCollision,
 * per axis: whichever starts further right (down) starts within the
 * other's size (`mine` the unit's at (x, y), `theirs` the object's). */
static bool boxes_overlap(s16 x, s16 y, u16 mine, s16 ox, s16 oy,
                          u16 theirs)
{
    s16 d = (s16)(ox - x);
    bool x_in = (d >= 0) ? (u16)d < mine : (u16)-d < theirs;
    d = (s16)(oy - y);
    bool y_in = (d >= 0) ? (u16)d < mine : (u16)-d < theirs;
    return x_in && y_in;
}

/* The widest unit type's size (24; a mod may change it). */
static u16 widest_unit(const HzGame *g)
{
    u16 w = 0;
    for (u8 t = 0; t < 16; t++) {
        u8 size = unit_stats_for(g, t)->size;
        w = size > w ? size : w;
    }
    return w;
}

/* A unit overlaps u only if its left edge is within (x - theirs, x + mine)
 * and its top within (y - theirs, y + mine); none in box b can (`mine`,
 * `pad`: the most either side may add, a SUPPLY's bumper). */
static bool box_out_of_reach(const HzFineBox *b, const HzUnit *u, u16 mine,
                             u16 pad)
{
    int theirs = b->size + pad;
    return b->x1 <= u->x - theirs || b->x0 >= u->x + mine
        || b->y1 <= u->y - theirs || b->y0 >= u->y + mine;
}

/* (Session 68) Does u's move from `from` take it off unit o, which its box
 * (`mine`, `theirs` as tested) overlapped there already: their centres
 * further apart? Two units on top of each other -- UNITS turned on in play
 * over the original's grid, which marks one unit a cell and so lets a
 * crowd overlap -- part this way, and neither goes further in. */
static bool moves_off(const HzUnit *u, const HzPoint *from, const HzUnit *o,
                      u16 mine, u16 theirs)
{
    if (from == NULL
        || !boxes_overlap(from->x, from->y, mine, o->x, o->y, theirs)) {
        return false;
    }
    long ox = 2L * o->x + o->size - u->size;    /* centres, doubled */
    long oy = 2L * o->y + o->size - u->size;
    long bx = ox - 2L * from->x;
    long by = oy - 2L * from->y;
    long ax = ox - 2L * u->x;
    long ay = oy - 2L * u->y;
    return ax * ax + ay * ay > bx * bx + by * by;
}

/* (Session 75) PIXELS (hitmask.c): u's shape, made the first time a box
 * test asks for it -- as drawn, or as it will be drawn when it is being
 * set down (`from` NULL) -- and whether u runs into o's. */
typedef struct {
    bool    made;
    HzShape shape;
} LazyShape;

static bool pixels_block(const HzGame *g, const HzUnit *u,
                         const HzPoint *from, LazyShape *mine, HzRef o)
{
    if (!mine->made) {
        if (from == NULL) {
            hitmask_unit_new(g, u, u->turn_to, u->x, u->y, &mine->shape);
        } else {
            hitmask_unit(g, u, &mine->shape);
        }
        mine->made = true;
    }
    HzShape theirs;
    hitmask_ref(g, o, &theirs);
    return hitmask_blocks(&mine->shape, from, &theirs);
}

HzRef crowd_hit(HzGame *g, const HzUnit *u, bool bumper,
                const HzPoint *from)
{
    bool self_supply = bumper && u->type == UNIT_SUPPLY;
    u16 own = (u16)(u->size + (self_supply ? 0x0C : 0));
    bool pixels = hitmask_on(g);
    LazyShape mine = { false };
    for (int p = 0; p < 2; p++) {
        const HzMech *m = &g->mech[p];
        if (m->crowd_cell != 0
            && boxes_overlap(u->x, u->y, own, m->x, m->y,
                             (u16)(m->size + (self_supply ? 0x0C : 0)))
            && (!pixels || self_supply
                || pixels_block(g, u, from, &mine, ref_mech(p)))) {
            return ref_mech(p);
        }
    }

    /* The cells: a unit that overlaps u is at most W wide (the widest
     * type), so its centre is within (x - W / 2, x + mine + W / 2), with
     * bumpers $C more each way; the same down. The other side's units
     * first, then (session 68) its own: u itself isn't in the lists while
     * it moves, nor before it is placed, but is passed over all the same. */
    int foe = (u->flags & UNIT_TEAM2) ? 0 : 1;
    u16 pad = bumper ? 0x0C : 0;
    u16 mine_most = (u16)(own + (self_supply ? 0 : pad));
    int reach = widest_unit(g) / 2 + pad;
    u32 first = fine_at(u->x - reach, u->y - reach);
    u32 last = fine_at(u->x + own + reach, u->y + own + reach);
    HzCrowdFine *f = g->crowd_fine;
    for (int k = 0; k < 2; k++) {
        int side = k == 0 ? foe : 1 - foe;
        for (u32 row = first / HZ_FINE; row <= last / HZ_FINE; row++) {
            for (u32 col = first % HZ_FINE; col <= last % HZ_FINE; col++) {
                u32 cell = row * HZ_FINE + col;
                if (f->head[side][cell] == 0
                    || box_out_of_reach(&f->box[side][cell], u, mine_most,
                                        pad)) {
                    continue;
                }
                HzFineBox tight = f->box[side][cell];
                bool empty = true;
                for (const HzUnit *o = fine_first(g, side, cell); o != NULL;
                     o = fine_after(g, o)) {
                    if (o != u) {
                        u16 mine_size = own;
                        u16 theirs = o->size;
                        bool widened = self_supply;
                        if (self_supply) {
                            theirs = (u16)(theirs + 0x0C);
                        } else if (bumper && o->type == UNIT_SUPPLY) {
                            mine_size = (u16)(mine_size + 0x0C);
                            theirs = (u16)(theirs + 0x0C);
                            widened = true;
                        }
                        if (!boxes_overlap(u->x, u->y, mine_size, o->x,
                                           o->y, theirs)) {
                            /* apart */
                        } else if (pixels && !widened) {
                            /* (session 75) PIXELS: the pixels decide,
                             * parting as moves_off has it */
                            if (pixels_block(g, u, from, &mine,
                                             unit_ref(o))) {
                                return unit_ref(o);
                            }
                        } else if (!moves_off(u, from, o, mine_size,
                                              theirs)) {
                            return unit_ref(o);
                        }
                    }
                    box_take(&tight, o, empty);
                    empty = false;
                }
                f->box[side][cell] = tight;     /* looked through: tight */
            }
        }
    }
    return ref_none();
}

/* The objects Units_UpdateTeam marks: active or dying, not carried. */
static bool in_grid(const HzUnit *u)
{
    u8 f = (u8)(u->flags & (UNIT_ACTIVE | UNIT_DYING | UNIT_CARRIED));
    return f != 0 && !(f & UNIT_CARRIED);
}

void grid_sync(HzGame *g)
{
    bool crowd = units_unlimited(g);
    if (crowd == g->crowd) {
        return;
    }
    if (crowd && g->crowd_fine == NULL) {
        g->crowd_fine = calloc(1, sizeof *g->crowd_fine);
        if (g->crowd_fine == NULL) {
            fprintf(stderr, "hz: out of memory for the units' grid\n");
            exit(1);
        }
    }
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        g->grid[i] = ref_none();
    }
    memset(g->crowd_head, 0, sizeof g->crowd_head);
    memset(g->crowd_count, 0, sizeof g->crowd_count);
    if (g->crowd_fine != NULL) {
        memset(g->crowd_fine->head, 0, sizeof g->crowd_fine->head);
    }
    for (int side = 0; side < 2; side++) {
        for (u32 slot = 0; slot < side_slots(g, side); slot++) {
            HzUnit *u = unit_at(g, side, slot);
            u->crowd_cell = 0;
            u->crowd_next = 0;
            u->crowd_prev = 0;
            u->fine_cell = 0;
            u->fine_next = 0;
            u->fine_prev = 0;
        }
    }
    g->mech[0].crowd_cell = 0;
    g->mech[1].crowd_cell = 0;
    g->crowd = crowd;
    for (int side = 0; side < 2; side++) {
        for (u32 slot = 0; slot < side_slots(g, side); slot++) {
            HzUnit *u = unit_at(g, side, slot);
            if (in_grid(u)) {
                grid_mark_unit(g, u);
            }
        }
    }
    for (int p = 0; p < 2; p++) {
        grid_mark_mech(g, &g->mech[p], ref_mech(p));
    }
}

void crowd_copy(HzGame *dst, const HzGame *src)
{
    if (!src->crowd) {
        return;             /* dst->crowd false: its fine grid unused */
    }
    if (dst->crowd_fine == NULL) {
        dst->crowd_fine = malloc(sizeof *dst->crowd_fine);
        if (dst->crowd_fine == NULL) {
            fprintf(stderr, "hz: out of memory for the units' grid\n");
            exit(1);
        }
    }
    memcpy(dst->crowd_fine, src->crowd_fine, sizeof *dst->crowd_fine);
}

static bool fault(const char *what, int side, u32 slot)
{
    fprintf(stderr, "crowd_check: %s (side %d, slot %u)\n", what, side,
            (unsigned)slot);
    return false;
}

/* (Session 68) A unit after u (a later slot of its side, or the other
 * side's when u is side 0's) whose box overlaps u's, no bumpers: the test
 * crowd_hit makes, its cells walked the same way (`widest`:
 * widest_unit's). */
static const HzUnit *overlap_after(const HzGame *g, const HzUnit *u,
                                   int widest)
{
    int reach = widest / 2;
    u32 first = fine_at(u->x - reach, u->y - reach);
    u32 last = fine_at(u->x + u->size + reach, u->y + u->size + reach);
    for (int side = u->side; side < 2; side++) {
        for (u32 row = first / HZ_FINE; row <= last / HZ_FINE; row++) {
            for (u32 col = first % HZ_FINE; col <= last % HZ_FINE; col++) {
                u32 cell = row * HZ_FINE + col;
                for (const HzUnit *o = fine_first(g, side, cell); o != NULL;
                     o = fine_after(g, o)) {
                    if ((side != u->side || o->slot > u->slot)
                        && boxes_overlap(u->x, u->y, u->size, o->x, o->y,
                                         o->size)) {
                        return o;
                    }
                }
            }
        }
    }
    return NULL;
}

bool crowd_check(const HzGame *g, bool apart)
{
    if (!g->crowd) {
        return true;
    }
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        if (!ref_is_none(g->grid[i])) {
            return fault("v_grid not empty", 0, (u32)i);
        }
    }
    int widest = widest_unit(g);
    for (int side = 0; side < 2; side++) {
        u32 slots = side_slots(g, side);
        u32 linked = 0;
        for (u16 cell = 0; cell < HZ_GRID * HZ_GRID; cell++) {
            u32 n = 0;
            u32 prev = 0;
            for (const HzUnit *u = crowd_first(g, side, cell); u != NULL;
                 u = crowd_after(g, u)) {
                if (++n > slots) {
                    return fault("a list loops", side, cell);
                }
                if (u->crowd_cell != cell + 1 || u->crowd_prev != prev) {
                    return fault("a link wrong", side, u->slot);
                }
                if (!in_grid(u)) {
                    return fault("in a list, not in play", side, u->slot);
                }
                prev = u->slot + 1;
            }
            if (n != g->crowd_count[side][cell]) {
                return fault("a count wrong", side, cell);
            }
            linked += n;
        }
        u32 fine_linked = 0;
        for (u32 cell = 0; cell < HZ_FINE * HZ_FINE; cell++) {
            const HzFineBox *b = &g->crowd_fine->box[side][cell];
            u32 n = 0;
            u32 prev = 0;
            for (const HzUnit *u = fine_first(g, side, cell); u != NULL;
                 u = fine_after(g, u)) {
                if (++n > slots) {
                    return fault("a fine list loops", side, cell);
                }
                if (u->fine_cell != cell + 1 || u->fine_prev != prev
                    || u->crowd_cell == 0) {
                    return fault("a fine link wrong", side, u->slot);
                }
                if (u->x < b->x0 || u->x > b->x1 || u->y < b->y0
                    || u->y > b->y1 || u->size > b->size) {
                    return fault("out of its cell's box", side, u->slot);
                }
                prev = u->slot + 1;
            }
            fine_linked += n;
        }
        if (fine_linked != linked) {
            return fault("the fine lists differ", side, fine_linked);
        }
        u32 alive = 0;
        for (u32 slot = 0; slot < slots; slot++) {
            const HzUnit *u = unit_at_c(g, side, slot);
            if (!in_grid(u)) {
                if (u->crowd_cell != 0) {
                    return fault("out of play, in a cell", side, slot);
                }
                continue;
            }
            if (u->crowd_cell == 0) {
                if (u->flags & UNIT_DYING) {
                    continue;       /* the game over's explosions, unmarked
                                       until their first update */
                }
                return fault("in play, in no cell", side, slot);
            }
            u16 half = (u16)(u->size >> 1);
            s16 cx = (s16)(u->x + half);
            s16 cy = (s16)(u->y + half);
            if (u->crowd_cell - 1 != crowd_cell_of(cx, cy)
                || u->fine_cell - 1 != fine_at(cx, cy)) {
                return fault("in the wrong cell", side, slot);
            }
            /* (Session 75) not with PIXELS: boxes overlap there, and
             * pixels may touch with nobody moving (a unit turns, an
             * INFANTRY steps: hitmask.c) */
            if (apart && !hitmask_on(g)
                && overlap_after(g, u, widest) != NULL) {
                return fault("on top of another unit", side, slot);
            }
            alive++;
        }
        if (alive != linked) {
            return fault("lists and records differ", side, linked);
        }
    }
    return true;
}
