#include "game.h"
#include "tables.h"

#include <stddef.h>
#include <string.h>

/* Mech_Update ($32F2) and the helpers it calls for moving: the player's
 * mech (and a CPU's) for one frame. Ported so far: dying (the fly home and
 * the respawn), taking off and landing, steering, speed, velocity, the
 * move and its collisions, the terrain event under the mech
 * (Obj_TerrainEvent, terrain.c), firing (Mech_FireGun; the shots are
 * shots.c's), fuel, the frame, and the screen lead.
 * lifting and dropping a unit (A over an own unit, or carrying).
 * C opens the order menu (order_menu.c), whose OrderMenu_Input runs here
 * while it is open. A CPU mech's pad comes from AI_MechThink (mech_ai.c),
 * which runs first. */

/* What Mech_GetTerrain finds under the mech. */
enum {
    GROUND_LAND    = 0,     /* anyone lands or drops a unit here */
    GROUND_SPECIAL = 1,     /* a type 6 or 8 block on maps 0-1: only a
                               human lands or drops here */
    GROUND_BLOCKED = 2,     /* a tile that blocks, or water and land */
    GROUND_LOW     = 3,     /* a tile under $3F: no taking off */
    GROUND_WATER   = 4      /* water only: an ST-57U may be dropped */
};

#define TRANSFORM_LANDING 0x80  /* mech_transform: landing, not taking off */


/* The layout word and the tile word at tile (tx, ty), as Mech_GetTerrain
 * and Mech_CheckCollision find them: layout (ty & $FC, tx & $FC) / 4,
 * then the block's 4 x 4 nametable words. */
static u16 map_tile(const HzGame *g, u16 tx, u16 ty, u16 *layout_word)
{
    u16 lw = g->layout[(ty & 0xFC) >> 2][(tx & 0xFC) >> 2];
    if (layout_word != NULL) {
        *layout_word = lw;
    }
    return g->blocks[lw & 0x1FF][(ty & 3) * 4 + (tx & 3)];
}

/* Obj_Accelerate ($3CBE), one axis: `step` towards the target, not past
 * it (word arithmetic, signed compares). */
s16 obj_accelerate_axis(s16 vel, s16 target, s16 step)
{
    if (target == vel) {
        return vel;
    }
    if (target > vel) {
        vel = (s16)(vel + step);
        if (!(target > vel)) {
            vel = target;
        }
        return vel;
    }
    vel = (s16)(vel - step);
    if (!(target < vel)) {
        vel = target;
    }
    return vel;
}

/* The heading the mech moves along: its own, or the opposite while a
 * robot reverses. */
static u8 move_heading(const HzMech *m)
{
    return (u8)((m->dir + ((m->flags & MECH_REVERSING) ? 8 : 0)) & 0x0F);
}

static void obj_accelerate(HzMech *m, s16 step)
{
    m->vel_x = obj_accelerate_axis(m->vel_x, m->target_vel_x, step);
    m->vel_y = obj_accelerate_axis(m->vel_y, m->target_vel_y, step);
}

/* Grid_MarkMech ($3D86): a robot writes `id` (itself, or nothing) into its
 * v_grid cell ((x + $10)/32, (y + $10)/32; no column mask, & $FFF); a jet
 * leaves the grid alone, and a destroyed mech writes nothing but nothing.
 * (Session 61) With UNITS (crowd.c) the mech's own cell: a robot's not
 * destroyed, or none (nothing, in any form). */
void grid_mark_mech(HzGame *g, HzMech *m, HzRef id)
{
    if (g->crowd) {
        m->crowd_cell = 0;
        if (!ref_is_none(id) && m->form == HZ_FORM_ROBOT
            && !(m->flags & MECH_DESTROYED)) {
            m->crowd_cell = (u16)(crowd_cell_of((s16)(m->x + 0x10),
                                                (s16)(m->y + 0x10)) + 1);
        }
        return;
    }
    if (m->form != HZ_FORM_ROBOT) {
        return;
    }
    if (!ref_is_none(id) && (m->flags & MECH_DESTROYED)) {
        return;
    }
    u16 row = (u16)(((u16)(m->y + 0x10) & 0xFFE0) * 2);
    u16 col = (u16)((u16)(m->x + 0x10) >> 5);
    g->grid[(row + col) & 0xFFF] = id;
}

/* A v_grid occupant: position, size, and the byte at +$01 the collision
 * code compares with 4, a SUPPLY's type. QUIRK: for a mech that byte is
 * mech_carried_slot, so a mech whose carried slot is 4 is widened as a
 * SUPPLY is. */
typedef struct {
    s16 x, y;
    u8  size;
    u32 byte1;
} Occupant;

static Occupant grid_occupant(HzGame *g, HzRef id)
{
    Occupant o;
    if (id.kind == REF_MECH) {
        const HzMech *m = &g->mech[id.side];
        o.x = m->x;
        o.y = m->y;
        o.size = m->size;
        o.byte1 = m->carried_slot;
        return o;
    }
    const HzUnit *u = ref_unit_ptr(g, id);
    o.x = u->x;
    o.y = u->y;
    o.size = u->size;
    o.byte1 = u->type;
    return o;
}

/* The overlap test of Mech_CheckCollision and Mech_CheckLandBlocked: per
 * axis, whichever starts further right (down) starts within the other's
 * size; `widen` adds $C to both sizes (a SUPPLY). */
static bool overlaps(const HzMech *m, const Occupant *o, bool widen)
{
    u16 own = (u16)(m->size + (widen ? 0x0C : 0));
    u16 other = (u16)(o->size + (widen ? 0x0C : 0));
    bool x_in;
    s16 d = (s16)(o->x - m->x);
    if (d >= 0) {
        x_in = (u16)d < own;
    } else {
        x_in = (u16)-d < other;
    }
    bool y_in;
    d = (s16)(o->y - m->y);
    if (d >= 0) {
        y_in = (u16)d < own;
    } else {
        y_in = (u16)-d < other;
    }
    return x_in && y_in;
}

/* The mech's own v_grid cell as the collision code computes it: & $7E0
 * and & $3F. */
static u16 mech_cell(const HzMech *m)
{
    u16 row = (u16)(((u16)(m->y + 0x10) & 0x7E0) * 2);
    u16 col = (u16)(((u16)(m->x + 0x10) >> 5) & 0x3F);
    return (u16)((row + col) & 0xFFF);
}

static s16 neighbour_offset(int n)
{
    return GRID_NEIGHBOUR_OFFSETS[n];
}

/* (Session 75) PIXELS (hitmask.c): whether what stands at the mech's
 * place (`mine`: NULL, the patch off) runs into object id, its boxes
 * overlapping; a widened box (a SUPPLY) is a box still. */
static bool pixels_block(const HzGame *g, const HzShape *mine,
                         const HzPoint *from, bool widen, HzRef id)
{
    if (mine == NULL || widen) {
        return true;
    }
    HzShape theirs;
    hitmask_ref(g, id, &theirs);
    return hitmask_blocks(mine, from, &theirs);
}

/* (Session 61) UNITS (crowd.c): the first object robot m overlaps in the
 * 3 x 3 cells round its own -- the other mech, then the cell's units, P1's
 * side's first -- as `overlaps` tests them (widened: `supply`, or the
 * occupant a SUPPLY; a mech is never one). No own-cell rule: the boxes
 * decide. None if none. */
static HzRef crowd_mech_hit(HzGame *g, const HzMech *m, bool supply,
                            const HzShape *mine, const HzPoint *from)
{
    u16 cells[9];
    int n = crowd_cells_round(crowd_cell_of((s16)(m->x + 0x10),
                                            (s16)(m->y + 0x10)), cells);
    for (int i = 0; i < n; i++) {
        for (int p = 0; p < 2; p++) {
            const HzMech *r = &g->mech[p];
            if (r == m || !crowd_mech_in(g, p, cells[i])) {
                continue;
            }
            Occupant o = { r->x, r->y, r->size, 0xFF };
            if (overlaps(m, &o, supply)
                && pixels_block(g, mine, from, supply, ref_mech(p))) {
                return ref_mech(p);
            }
        }
        for (int side = 0; side < 2; side++) {
            for (const HzUnit *u = crowd_first(g, side, cells[i]); u != NULL;
                 u = crowd_after(g, u)) {
                Occupant o = { u->x, u->y, u->size, u->type };
                bool widen = supply || u->type == UNIT_SUPPLY;
                if (overlaps(m, &o, widen)
                    && pixels_block(g, mine, from, widen, unit_ref(u))) {
                    return unit_ref(u);
                }
            }
        }
    }
    return ref_none();
}

/* Mech_CheckCollision ($3DC0) -> v_hit_flags (PROBE_*), v_hit_id,
 * v_terrain_event (and v_terrain_event3). A robot: its own cell's occupant,
 * else the first of the 8 cells round it whose occupant overlaps (a cell
 * number past $FFF skipped; the 68k reads its byte first). Both forms: the
 * 4 x 4 tiles from ((x + 4)/8, (y + 4)/8), the first with b15 (b1) and
 * the first with b13 (b2: that tile's layout word and its block's
 * terrain), stopping once both are found. */
static void mech_check_collision(HzGame *g, const HzMech *m,
                                 const HzPoint *from)
{
    g->hit_flags = 0;
    g->hit_id = ref_none();
    g->terrain_event = 0;

    if (m->form == HZ_FORM_ROBOT) {
        /* (session 75) PIXELS: its frame (the last one set) */
        HzShape shape;
        const HzShape *mine = NULL;
        if (hitmask_on(g)) {
            hitmask_mech(g, m, &shape);
            mine = &shape;
        }
        u16 cell = mech_cell(m);
        HzRef id = g->grid[cell];
        bool hit = !ref_is_none(id);
        if (g->crowd) {
            /* (session 61) UNITS */
            id = crowd_mech_hit(g, m, false, mine, from);
            hit = !ref_is_none(id);
        }
        for (int n = 0; !hit && !g->crowd && n < 8; n++) {
            u16 c = (u16)(cell + neighbour_offset(n));
            if (c >= 0x1000) {
                continue;
            }
            id = g->grid[c];
            if (ref_is_none(id)) {
                continue;
            }
            Occupant o = grid_occupant(g, id);
            bool widen = o.byte1 == UNIT_SUPPLY;
            hit = overlaps(m, &o, widen)
                  && pixels_block(g, mine, from, widen, id);
        }
        if (hit) {
            g->hit_flags |= PROBE_OBJECT;
            g->hit_id = id;
            if (id.side != ((m->flags & MECH_TEAM2) ? 1 : 0)) {
                g->hit_flags |= PROBE_FOE;
            }
        }
    }

    u16 tx0 = (u16)((u16)(m->x + 4) >> 3);
    u16 ty = (u16)((u16)(m->y + 4) >> 3);
    u8 looking = 3;
    for (int r = 0; r < 4; r++, ty++) {
        u16 tx = tx0;
        for (int c = 0; c < 4; c++, tx++) {
            u16 lw;
            u16 w = map_tile(g, tx, ty, &lw);
            if ((looking & 1) && (w & TILE_BLOCKED)) {
                g->hit_flags |= PROBE_TILE;
                looking &= (u8)~1;
            }
            if ((looking & 2) && (w & TILE_EVENT)) {
                g->hit_flags |= PROBE_EVENT;
                looking &= (u8)~2;
                g->terrain_event3 = lw;
                g->terrain_event = g->block_terrain[lw & 0x1FF];
            }
            if (!(looking & 3)) {
                return;
            }
        }
    }
}

/* Within $1C px of each other on both axes (Mechs_JetCollide's and
 * Mechs_Overlap's test). */
static bool mechs_near(const HzMech *a, const HzMech *b)
{
    u16 dx = (u16)(a->x - b->x + 0x1C);
    u16 dy = (u16)(a->y - b->y + 0x1C);
    return dx < 0x38 && dy < 0x38;
}

/* (Session 75) PIXELS (hitmask.c): near, and their frames touch. */
static bool mechs_touch(const HzGame *g, const HzMech *a, const HzMech *b)
{
    if (!mechs_near(a, b)) {
        return false;
    }
    if (!hitmask_on(g)) {
        return true;
    }
    HzShape sa;
    HzShape sb;
    hitmask_mech(g, a, &sa);
    hitmask_mech(g, b, &sb);
    return hitmask_touch(&sa, &sb);
}

/* Mechs_JetCollide ($3FEA): both jets, neither destroyed, and near. */
static bool mechs_jet_collide(const HzGame *g)
{
    const HzMech *a = &g->mech[0];
    const HzMech *b = &g->mech[1];
    if (a->form == HZ_FORM_ROBOT || b->form == HZ_FORM_ROBOT
        || (a->flags & MECH_DESTROYED) || (b->flags & MECH_DESTROYED)) {
        return false;
    }
    return mechs_touch(g, a, b);
}

/* Mechs_Overlap ($40AA): neither destroyed, and near. */
static bool mechs_overlap(const HzGame *g, const HzMech *a, const HzMech *b)
{
    if ((a->flags & MECH_DESTROYED) || (b->flags & MECH_DESTROYED)) {
        return false;
    }
    return mechs_touch(g, a, b);
}

/* Mech_ClearHomePad ($404A): the rival within $26 px of m's landing spot
 * (home camera + home screen position, y - $20) on both axes is pushed:
 * vel_x $200 away from the spot, vel_y reversed. */
static void mech_clear_home_pad(const HzMech *m, HzMech *rival)
{
    u16 dx = (u16)(m->home_cam.x + m->home_screen.x - rival->x + 0x26);
    u16 dy = (u16)(m->home_cam.y + m->home_screen.y - 0x20 - rival->y
                   + 0x26);
    if (dx >= 0x4C || dy >= 0x4C) {
        return;
    }
    rival->vel_x = (dx <= 0x26) ? 0x200 : -0x200;
    rival->vel_y = (s16)-rival->vel_y;
}

/* Whether own unit u is one the mech can lift: not dying, its (x + 8,
 * y + 8) within [mech, mech + $30 - its size) on both axes (unsigned). */
static bool liftable(const HzGame *g, const HzMech *m, const HzUnit *u)
{
    if (u->flags & UNIT_DYING) {
        return false;
    }
    if (u->type == UNIT_MECHANIC && repair_on(g)) {
        return false;       /* (session 72) REPAIR: his truck's to carry */
    }
    u16 reach = (u16)((u8)(0x20 - u->size) + 0x10);
    u16 x = (u16)(u->x + 8);
    u16 y = (u16)(u->y + 8);
    if (x < (u16)m->x || (u16)(x - (u16)m->x) >= reach) {
        return false;
    }
    return y >= (u16)m->y && (u16)(y - (u16)m->y) < reach;
}

/* (Session 61) UNITS (crowd.c): the first own unit liftable in the 3 x 3
 * cells round the mech's (each cell's list in order). Its slot, or -1. */
static long crowd_find_unit_to_lift(HzGame *g, const HzMech *m, int side)
{
    u16 cells[9];
    int n = crowd_cells_round(crowd_cell_of((s16)(m->x + 0x10),
                                            (s16)(m->y + 0x10)), cells);
    for (int i = 0; i < n; i++) {
        for (const HzUnit *u = crowd_first(g, side, cells[i]); u != NULL;
             u = crowd_after(g, u)) {
            if (liftable(g, m, u)) {
                return (long)u->slot;
            }
        }
    }
    return -1;
}

/* Mech_FindUnitToLift ($40F2): an own unit, not dying, in the mech's cell
 * or the 8 round it, whose (x + 8, y + 8) lies within [mech, mech + $30 -
 * its size) on both axes (unsigned). Its slot, or -1. */
static long mech_find_unit_to_lift(HzGame *g, const HzMech *m)
{
    u16 row = (u16)(((u16)(m->y + 0x10) & 0x7E0) * 2);
    u16 col = (u16)(((u16)(m->x + 0x10) >> 5) & 0x3F);
    u16 cell = (u16)(row + col);
    int side = (m->flags & MECH_TEAM2) ? 1 : 0;
    if (g->crowd) {
        return crowd_find_unit_to_lift(g, m, side);
    }
    for (int n = 0; n < 9; n++) {
        u16 c = (u16)(cell + (n == 0 ? 0 : neighbour_offset(n - 1)));
        if (c >= 0x1000) {
            continue;
        }
        HzRef id = g->grid[c];
        if (id.kind != REF_UNIT || id.side != side) {
            continue;               /* nobody, a mech, the other side */
        }
        if (!liftable(g, m, ref_unit_ptr(g, id))) {
            continue;
        }
        return (long)id.slot;
    }
    return -1;
}

/* Mech_CheckLandBlocked ($41B6): its cell taken, or an occupant of the 9
 * cells (its own again first) overlapping it; both sizes + $C when the
 * carried unit or the occupant is a SUPPLY (see Occupant). */
static bool mech_check_land_blocked(HzGame *g, const HzMech *m)
{
    u16 cell = mech_cell(m);
    if (!ref_is_none(g->grid[cell])) {
        return true;
    }
    bool supply = false;
    if (m->flags & MECH_CARRYING) {
        supply = carried_unit(g, m)->type == UNIT_SUPPLY;
    }
    /* (Session 75) PIXELS: what will stand here -- the unit put down,
     * centred and facing the mech's way (mech_drop), or the robot it lands
     * as */
    HzShape shape;
    const HzShape *mine = NULL;
    if (hitmask_on(g)) {
        if (m->flags & MECH_CARRYING) {
            const HzUnit *u = carried_unit(g, m);
            s16 half = (s16)((u8)(0x20 - u->size) >> 1);
            hitmask_unit_new(g, u, m->dir, (s16)(m->x + half),
                             (s16)(m->y + half), &shape);
        } else {
            hitmask_robot(g, m, &shape);
        }
        mine = &shape;
    }
    if (g->crowd) {
        /* (Session 61) UNITS: the boxes, no own-cell rule. */
        return !ref_is_none(crowd_mech_hit(g, m, supply, mine, NULL));
    }
    for (int n = 0; n < 9; n++) {
        u16 c = (u16)(cell + (n == 0 ? 0 : neighbour_offset(n - 1)));
        if (c >= 0x1000) {
            continue;
        }
        HzRef id = g->grid[c];
        if (ref_is_none(id)) {
            continue;
        }
        Occupant o = grid_occupant(g, id);
        bool widen = supply || o.byte1 == UNIT_SUPPLY;
        if (overlaps(m, &o, widen)
            && pixels_block(g, mine, NULL, widen, id)) {
            return true;
        }
    }
    return false;
}

/* Mech_GetTerrain ($4336): what the 4 x 4 tiles below say (GROUND_*). A
 * block of terrain type 6 or 8 decides at once: special on maps 0-1, else
 * land. Otherwise per tile: b9 (water) counted; b15 blocked; a tile index
 * under $3F low. After all 16: all water, none land, some blocked. */
static int mech_get_terrain(const HzGame *g, const HzMech *m)
{
    u16 water = 0xFFFF;
    u16 tx0 = (u16)((u16)(m->x + 4) >> 3);
    u16 ty = (u16)((u16)(m->y + 4) >> 3);
    for (int r = 0; r < 4; r++, ty++) {
        u16 tx = tx0;
        for (int c = 0; c < 4; c++, tx++) {
            u16 lw;
            u16 w = map_tile(g, tx, ty, &lw);
            u8 type = (u8)g->block_terrain[lw & 0x1FF];
            if (type == 8 || type == 6) {
                return (g->map < 2) ? GROUND_SPECIAL : GROUND_LAND;
            }
            if (w & TILE_WATER) {
                water = (u16)(water >> 1);
                continue;
            }
            if (w & TILE_BLOCKED) {
                return GROUND_BLOCKED;
            }
            if ((w & 0x1FF) < 0x3F) {
                return GROUND_LOW;
            }
        }
    }
    if (water == 0) {
        return GROUND_WATER;
    }
    if (water == 0xFFFF) {
        return GROUND_LAND;
    }
    return GROUND_BLOCKED;
}

/* (Session 57) The two above for the nightmare AI, at another place. */
int mech_ground_at(const HzGame *g, const HzMech *m, s16 x, s16 y)
{
    HzMech at = *m;
    at.x = x;
    at.y = y;
    return mech_get_terrain(g, &at);
}

bool mech_land_blocked_at(HzGame *g, const HzMech *m, s16 x, s16 y)
{
    HzMech at = *m;
    at.x = x;
    at.y = y;
    return mech_check_land_blocked(g, &at);
}

/* A destroyed mech (Mech_Update's .dying): HP counts down (the debris
 * sound's end at $A); at 0, the respawn, the carried unit (if any) gone
 * with it. Meanwhile speed 0, mech_form cycling 0-2 (the debris's
 * frames), and the fly home: the team's camera
 * up to 8 px a frame towards mech_home_cam (obj_x/y with it), the screen
 * position 1 px a frame towards mech_home_screen (obj_x/y with it), and
 * Mech_ClearHomePad. */
static void mech_dying(HzGame *g, HzMech *m, HzMech *rival)
{
    m->hp = (u16)(m->hp - 1);
    if (m->hp == 0) {
        if (m->flags & MECH_CARRYING) {
            m->flags &= (u8)~MECH_CARRYING;
            carried_unit(g, m)->flags = 0;
            g->unit_count[(m->flags & MECH_TEAM2) ? 1 : 0]--;
            if (carried_unit(g, m)->type >= UNIT_REPAIR) {
                repair_unit_freed(g, carried_unit(g, m));   /* (session 72) */
            }
        }
        m->flags &= (u8)~MECH_DESTROYED;
        mech_respawn(g, m, rival);
        return;
    }
    if (m->hp == 0x0A) {
        snd_queue_sfx(g, SFX_DEBRIS_END);
    }
    m->speed = 0;
    u16 form = (u16)(m->form + 1);
    if (form > 2) {
        form = 0;
    }
    m->form = form;

    const HzPoint *cam = &g->camera[(m->flags & MECH_TEAM2) ? 1 : 0];
    s16 d = (s16)(m->home_cam.x - cam->x);
    s16 step = (d < 0) ? -8 : 8;
    if ((u16)(d < 0 ? -d : d) < 8) {
        step = d;
    }
    m->cam_dx = step;
    m->x = (s16)(m->x + step);
    d = (s16)(m->home_cam.y - cam->y);
    step = (d < 0) ? -8 : 8;
    if ((u16)(d < 0 ? -d : d) < 8) {
        step = d;
    }
    m->cam_dy = step;
    m->y = (s16)(m->y + step);

    d = (s16)(m->home_screen.x - m->screen_x);
    step = (d < 0) ? -1 : ((d == 0) ? 0 : 1);
    m->screen_x = (s16)(m->screen_x + step);
    m->x = (s16)(m->x + step);
    d = (s16)(m->home_screen.y - m->screen_y);
    step = (d < 0) ? -1 : ((d == 0) ? 0 : 1);
    m->screen_y = (s16)(m->screen_y + step);
    m->y = (s16)(m->y + step);
    mech_clear_home_pad(m, rival);
}

/* KIDOU's fuel use for a form, +1 for a human against a CPU. */
static u8 fuel_use_for(const HzMechStats *kidou, int form, const HzMech *m,
                       const HzMech *rival)
{
    u8 use = kidou->fuel_use[form];
    if ((rival->flags & MECH_CPU) && !(m->flags & MECH_CPU)) {
        use++;
    }
    return use;
}

/* Lifting (Mech_Update's .lift, $3512): the own unit in `slot`
 * (Mech_FindUnitToLift's) picked up: mech_carried_slot, mech_carried_order
 * = its order, both carrying / carried, the carrying form with KIDOU's
 * carrying speed (+$22) and fuel use (+$25, + 1 for
 * a human against a CPU; then halved, as a byte, outside the demo with a
 * CPU in the game on difficulty A); the unit leaves the grid
 * (Grid_ClearObj); sfx $C. The unit keeps everything else, its place
 * included, until it is put down. */
static void mech_lift(HzGame *g, HzMech *m, const HzMech *rival, u32 slot)
{
    const HzMechStats *kidou = &g->assets->mech_stats;  /* UnitStats_KIDOU */
    m->carried_slot = slot;
    HzUnit *u = carried_unit(g, m);
    m->carried_order = u->order;
    m->flags |= MECH_CARRYING;
    u->flags |= UNIT_CARRIED;
    m->form = HZ_FORM_CARRYING;
    m->max_speed = kidou->max_speed[HZ_FORM_CARRYING];  /* +$22 */
    u8 use = fuel_use_for(kidou, HZ_FORM_CARRYING, m, rival);
    if (g->game_type >= 0 && ((m->flags | rival->flags) & MECH_CPU)
        && g->options[1] == 0) {
        use = (u8)(use >> 1);
    }
    m->fuel_use = use;
    grid_clear_unit(g, u);
    snd_queue_sfx(g, SFX_LIFT);
}

/* Dropping (Mech_Update's .drop, $3578): with its cell free
 * (Mech_CheckLandBlocked) and the ground right (Mech_GetTerrain: the
 * ST-57U water only; any other land, or special for a human), the carried
 * unit (the mech's team, slot mech_carried_slot) is put down. The mech: a
 * jet again, KIDOU's jet speed
 * (+$21) and fuel use (+$24, + 1 for a human against a CPU), velocities 0.
 * The unit: centred under the mech ((x, y) + ($20 - size) / 2, a byte
 * halved), mech_carried_order as its order, unit_max_speed from UnitStats
 * (+$11) and unit_speed's low byte the same (its high byte kept), velocity
 * 0, heading the mech's + 1 with turn-to, turret and aim directions the
 * mech's (Unit_SetSprite turns it back the one step, setting its target
 * velocity and sprite), both b3 cleared, +$06/$07, +$22/$23 and $38-$4F
 * cleared; the unit into v_grid, Unit_SetSprite with the team's
 * palette, v_hit_flags/v_hit_id cleared, Obj_CheckTerrain's v_hit_flags
 * into unit_blocked; sfx $D. Not touched: the unit's 8.8 remainders,
 * unit_push, unit_terrain, unit_tick (which an INFANTRY's sprite reads). */
static void mech_drop(HzGame *g, HzMech *m, const HzMech *rival)
{
    if (mech_check_land_blocked(g, m)) {
        return;
    }
    int terrain = mech_get_terrain(g, m);
    HzUnit *u = carried_unit(g, m);
    if (u->flags & UNIT_BOAT) {
        if (terrain != GROUND_WATER) {
            return;
        }
    } else if (terrain != GROUND_LAND) {
        if ((m->flags & MECH_CPU) || terrain != GROUND_SPECIAL) {
            return;
        }
    }

    const HzMechStats *kidou = &g->assets->mech_stats;
    m->form = HZ_FORM_JET;
    m->max_speed = kidou->max_speed[HZ_FORM_JET];       /* +$21 */
    m->fuel_use = fuel_use_for(kidou, HZ_FORM_JET, m, rival);
    m->vel_x = 0;
    m->vel_y = 0;
    m->target_vel_x = 0;
    m->target_vel_y = 0;

    const HzUnitStats *st = unit_stats_for(g, u->type); /* UnitStats */
    u16 half = (u16)((u8)(0x20 - u->size) >> 1);
    u->order = m->carried_order;
    u->x = (s16)(m->x + half);
    u->y = (s16)(m->y + half);
    u->max_speed = st->speed;
    u->speed = (u16)((u->speed & 0xFF00) | u->max_speed);
    u->vel_x = 0;
    u->vel_y = 0;
    u8 d = (u8)((m->dir + 1) & 0x0F);
    u->dir = d;
    d = (u8)((d - 1) & 0x0F);
    u->turn_to = d;
    u->turret_dir = d;
    u->aim_dir = d;
    m->flags &= (u8)~MECH_CARRYING;
    u->flags &= (u8)~UNIT_CARRIED;
    u->phase = 0;
    u->fire_delay = 0;
    u->shot_flags = 0;
    u->blocked = 0;
    memset(&u->ai, 0, sizeof u->ai);
    u16 pal = (u->flags & UNIT_TEAM2) ? TEAM2_PALETTE : TEAM1_PALETTE;
    grid_mark_unit(g, u);
    unit_set_sprite(g, u, pal);
    g->hit_flags = 0;
    g->hit_id = ref_none();
    obj_check_terrain(g, u);
    u->blocked = (u8)g->hit_flags;
    snd_queue_sfx(g, SFX_DROP);
}

/* Mech_FireGun ($3D04): not while transforming; a human waits out
 * mech_fire_delay (a CPU doesn't). Shot_FireBullet with the mech's flags
 * & $90 (b3 clear: its bullets hit any unit and the other mech, never an
 * HQ) and heading, from (x + $C, y + $C), at KIDOU's shot speed and life
 * (+$14/+$15); fired: mech_fire_delay 6, and with v_free_ammo the round
 * given back. */
static void mech_fire_gun(HzGame *g, HzMech *m)
{
    if (m->transform != 0) {
        return;
    }
    /* FIX (HZ_FIX_FIRE_RATE): a human's mech fires as a CPU's always does,
     * whenever B is pressed and a shot slot is free */
    bool human_waits = !(m->flags & MECH_CPU)
                       && !(g->fixes & HZ_FIX_FIRE_RATE);
    if (human_waits && m->fire_delay != 0) {
        return;
    }
    const HzUnitStats *kidou = &g->assets->mech_stats.stats;
    if (shot_fire_bullet(g, ref_mech((int)(m - g->mech)),
                         (u8)(m->flags & (MECH_ACTIVE | MECH_TEAM2)),
                         m->dir, (s16)(m->x + 0x0C),
                         (s16)(m->y + 0x0C), kidou->shot_speed,
                         kidou->shot_life) != 0) {
        return;
    }
    m->fire_delay = 6;
    if (g->free_ammo != 0) {
        m->ammo++;
    }
}

/* The buttons (Mech_Update's .buttons to .pad_dir): those pressed this
 * frame (not held from the last). A takes off, lifts a unit, lands or
 * drops the carried unit; B fires; C opens the order menu. Returns
 * JoyToDir's direction for the pad, $FF none, or
 * MENU_OPENED (C opened the order menu: the update ends). */
#define MENU_OPENED 0x100     /* mech_buttons: C opened the order menu */

static int mech_buttons(HzGame *g, HzMech *m, HzMech *rival)
{
    const HzMechStats *kidou = &g->assets->mech_stats;  /* UnitStats_KIDOU */
    u8 pressed = (u8)((m->pad ^ m->pad_prev) & m->pad);

    if (m->flags & MECH_MENU_OPEN) {
        order_menu_input(g, m);     /* no direction while it is open */
        return 0xFF;
    }
    if (pressed & HZ_PAD_C) {
        m->flags |= MECH_MENU_OPEN; /* the update ends */
        return MENU_OPENED;
    }

    if (pressed & HZ_PAD_A) {
        if (m->form == HZ_FORM_ROBOT) {
            if (!mechs_overlap(g, m, rival)
                && mech_get_terrain(g, m) != GROUND_LOW) {
                m->form = HZ_FORM_JET;                  /* take off */
                m->blocked = 0;
                m->max_speed = kidou->max_speed[HZ_FORM_JET];   /* +$21 */
                m->fuel_use = fuel_use_for(kidou, HZ_FORM_JET, m, rival);
                m->vel_x = 0;
                m->vel_y = 0;
                m->target_vel_x = 0;
                m->target_vel_y = 0;
                m->transform = 0x13;                    /* 19 steps */
                m->transform_delay = 6;
                m->turn_to = m->dir;
                m->dir = 0;
                snd_queue_sfx(g, SFX_TAKE_OFF);
            }
        } else if (m->form == HZ_FORM_JET) {
            long slot = mech_find_unit_to_lift(g, m);
            if (slot >= 0) {
                mech_lift(g, m, rival, (u32)slot);
            } else if (mech_get_terrain(g, m) <= GROUND_SPECIAL
                       && !mech_check_land_blocked(g, m)) {
                m->form = HZ_FORM_ROBOT;                /* land */
                m->max_speed = kidou->max_speed[HZ_FORM_ROBOT]; /* +$20 */
                m->fuel_use = kidou->fuel_use[HZ_FORM_ROBOT];   /* +$23 */
                m->speed = 0;
                m->transform = TRANSFORM_LANDING | 0x0D;
                m->transform_delay = 6;
                m->turn_to = m->dir;
                m->dir = 0;
                snd_queue_sfx(g, SFX_LAND);
            }
        } else {
            mech_drop(g, m, rival);
        }
    } else if (pressed & HZ_PAD_B) {
        mech_fire_gun(g, m);
    }
    return JOY_TO_DIR[m->pad & HZ_PAD_DIRS];
}

/* Steering (Mech_Update's .steer to .store_speed): every 4th frame the
 * heading turns a step towards the pad's direction (a robot under $10
 * frames into a turn reverses instead when the pad points behind it), and
 * the speed rises by 6 on course or falls by 4 off it, capped at the
 * maximum (a quarter of it with the tank empty); no direction slows it by
 * 4. */
static void mech_steer(HzGame *g, HzMech *m, u8 want)
{
    if (m->transform != 0) {
        return;
    }
    if (want == 0xFF) {
        s16 speed = (s16)(m->speed - 4);
        m->speed = (u16)((speed < 0) ? 0 : speed);
        m->lead = 0;
        m->dir_held = 0;
        return;
    }
    m->turn_to = want;
    if ((g->mech_tick & 3) == 0) {
        u8 diff = (u8)((want - m->dir) & 0x0F);
        s8 step;
        if (diff == 0) {
            step = 0;
            m->flags &= (u8)~MECH_REVERSING;
        } else if (m->form == HZ_FORM_ROBOT && m->dir_held < 0x10) {
            if (diff == 8) {
                step = 0;
                m->flags |= MECH_REVERSING;
            } else if (diff < 7) {
                step = 1;
                m->flags &= (u8)~MECH_REVERSING;
            } else if (diff > 9) {
                step = -1;
                m->flags &= (u8)~MECH_REVERSING;
            } else if (diff < 8) {
                step = -1;
                m->flags |= MECH_REVERSING;
            } else {
                step = 1;
                m->flags |= MECH_REVERSING;
            }
        } else if (diff < 8) {
            step = 1;
            m->flags &= (u8)~MECH_REVERSING;
        } else {
            step = -1;
        }
        m->dir = (u8)((m->dir + step) & 0x0F);
    }

    s16 change = 6;
    if (move_heading(m) != m->turn_to) {
        change = -4;
        if (m->lead == 2) {
            m->lead = 1;
        }
    }
    s16 speed = (s16)(m->speed + change);
    if (speed < 0) {
        speed = 0;
    }
    u8 cap = m->max_speed;
    if (m->fuel == 0) {
        cap = (u8)(cap >> 2);
    }
    u8 lo = (u8)speed;
    if (lo > cap) {
        lo = cap;
    }
    m->speed = (u16)((m->speed & 0xFF00) | lo);

    u8 last = m->pad_last;
    m->pad_last = m->pad;
    if (!(m->dir_held & 0x80)) {
        m->dir_held++;
    }
    if ((m->pad & HZ_PAD_DIRS) != (last & HZ_PAD_DIRS)) {
        m->dir_held = 0;
    }
}

/* The move (Mech_Update's .velocity to .store_bumped): the target
 * velocity from the heading and speed, the velocity towards it (a robot's
 * by what the ground under it did last move: mech_terrain_flow), the move
 * in 8.8, and what it ran into: a robot stops at an object or a blocking
 * tile, a jet bounces off the other jet. */
static void mech_move(HzGame *g, HzMech *m, HzMech *rival)
{
    if (m->transform != 0) {
        m->vel_x = 0;
        m->vel_y = 0;
        m->target_vel_x = 0;
        m->target_vel_y = 0;
    } else {
        const HzPoint *sine = &SINE_TABLE[move_heading(m)];
        s32 speed = (s16)m->speed;
        m->target_vel_x = (s16)(u16)(speed * sine->x);
        m->target_vel_y = (s16)(u16)(speed * sine->y);
    }

    s16 old_x = m->x;
    s16 old_y = m->y;
    s16 old_cam_dx = m->cam_dx;
    s16 old_cam_dy = m->cam_dy;
    if (m->form != HZ_FORM_ROBOT) {
        obj_accelerate(m, 0x18);
    } else {
        /* What the ground did last move (terrain.c's handlers). */
        u8 flow = m->terrain_flow;
        if (!(flow & 0x80)) {
            /* A current (or nothing, 0): the target velocity pushed by
             * b0-b3 x $40 along b4-b6's direction code. */
            s16 amount = (s16)((flow & 0x0F) << 6);
            s16 vx = m->target_vel_x;
            s16 vy = m->target_vel_y;
            if (flow != 0) {
                u16 code = (u16)((flow >> 4) & 7);
                if (code != 0 && code != 4) {
                    vx = (s16)((code < 4) ? vx + amount : vx - amount);
                }
                code = (u16)(code - 2);
                if (code != 0 && code != 4) {
                    vy = (s16)((code < 4) ? vy + amount : vy - amount);
                }
            }
            m->vel_x = vx;
            m->vel_y = vy;
        } else if (flow & 0x40) {
            /* Slow ground: the target velocity cut by a quarter, a half
             * or three quarters (b0-b1). */
            s16 vx = m->target_vel_x;
            s16 vy = m->target_vel_y;
            switch (flow & 3) {
            case 1:
                vx = (s16)(vx - (vx >> 2));
                vy = (s16)(vy - (vy >> 2));
                break;
            case 2:
                vx = (s16)(vx >> 1);
                vy = (s16)(vy >> 1);
                break;
            case 3:
                vx = (s16)(vx >> 2);
                vy = (s16)(vy >> 2);
                break;
            default:
                break;
            }
            m->vel_x = vx;
            m->vel_y = vy;
        } else {
            /* Slippery ground: a slower acceleration (b0-b4). */
            obj_accelerate(m, (s16)(flow & 0x1F));
        }
    }

    m->sub_x = (u16)(m->sub_x + (u16)m->vel_x);
    s16 px = (s8)(m->sub_x >> 8);
    m->sub_x &= 0x00FF;
    m->x = (s16)(m->x + px);
    m->cam_dx = px;
    m->sub_y = (u16)(m->sub_y + (u16)m->vel_y);
    s16 py = (s8)(m->sub_y >> 8);
    m->sub_y &= 0x00FF;
    m->y = (s16)(m->y + py);
    m->cam_dy = py;
    m->terrain_flow = 0;

    HzPoint from = { old_x, old_y };
    mech_check_collision(g, m, &from);
    m->terrain = g->terrain_event;
    if (m->form == HZ_FORM_ROBOT) {
        m->blocked = (u8)g->hit_flags;
    }
    if (g->hit_flags & PROBE_EVENT) {
        mech_terrain_event(g, m, (m->form != HZ_FORM_ROBOT) ? HZ_OBJ_JET
                                                           : HZ_OBJ_ROBOT);
    }

    if (m->form != HZ_FORM_ROBOT) {
        if (mechs_jet_collide(g)) {
            m->x = old_x;
            m->y = old_y;
            m->cam_dx = old_cam_dx;
            m->cam_dy = old_cam_dy;
            m->blocked |= PROBE_OBJECT | PROBE_FOE;
            m->bumped = ref_mech((m->flags & MECH_TEAM2) ? 0 : 1);
            rival->vel_x = m->vel_x;
            rival->vel_y = m->vel_y;
            m->vel_x = (s16)-m->vel_x;
            m->vel_y = (s16)-m->vel_y;
            snd_queue_sfx(g, SFX_JETS_MEET);
        }
        return;
    }
    if (!(g->hit_flags & (PROBE_OBJECT | PROBE_TILE))) {
        return;
    }
    m->x = old_x;
    m->y = old_y;
    m->cam_dx = old_cam_dx;
    m->cam_dy = old_cam_dy;
    if (!(g->hit_flags & PROBE_OBJECT)) {
        return;
    }
    m->bumped = g->hit_id;
}

/* Fuel and the frame (Mech_Update's .fuel to .transform_done): fuel by
 * distance (each $50 of speed added up costs mech_fuel_use; an empty tank
 * destroys the mech), the walk's step, the transform animation, and the
 * sprite frame for the form and heading (a robot's firing frame while
 * mech_fire_delay is 3 or more). */
static void mech_fuel_and_frame(HzGame *g, HzMech *m)
{
    if (m->transform == 0) {
        u8 count = (u8)(m->speed + m->fuel_count);
        if (count >= 0x4F) {
            count = (u8)(count - 0x50);
            u8 step = (u8)(m->walk_step + 1);
            if (step > 2) {
                step = 0;
            }
            m->walk_step = step;
            if (g->free_ammo == 0) {
                if (m->fuel < m->fuel_use) {
                    m->fuel = 0;
                    m->flags |= MECH_DESTROYED;
                    m->hp = 0x180;
                    snd_queue_sfx(g, SFX_MECH_DESTROYED);
                } else {
                    m->fuel = (u16)(m->fuel - m->fuel_use);
                }
            }
        }
        m->fuel_count = count;
    } else {
        u16 left = m->transform;
        if ((left & 0x7F) >= 9) {
            m->transform_delay--;
            if (m->transform_delay != 0) {
                return;
            }
            m->transform_delay = 6;
            left = (u16)(left - 1);
            m->transform = (u8)(((left & 0x7F) == 0) ? 0 : left);
            const u16 *table = MECH_TRANSFORM_FRAMES;
            u16 last = 0x12;
            if (left & TRANSFORM_LANDING) {
                table = MECH_TRANSFORM_FRAMES2;
                last = 0x0C;
            }
            u16 index = (u16)(last - (left & 0x7F));
            m->frame = table[index];
            return;
        }
        u8 now = (u8)(left - 1);
        if ((now & 0x7F) == 0) {
            now = 0;
        }
        m->transform = now;
        u8 diff = (u8)((m->turn_to - m->dir) & 0x0F);
        if (diff != 0) {
            m->dir = (u8)((m->dir + ((diff >= 8) ? -1 : 1)) & 0x0F);
        }
        m->fuel_count = 0;
        m->walk_step = 0;
    }

    if (m->form != HZ_FORM_ROBOT) {
        u16 frame = MECH_JET_FRAMES[m->dir & 0x0F];
        if (m->form == HZ_FORM_CARRYING) {
            frame = (u16)(frame + 5);
        }
        m->frame = frame;
        if (m->fire_delay != 0) {
            m->fire_delay--;
        }
        return;
    }
    m->frame = m->dir & 0x0F;
    u8 delay = m->fire_delay;
    if (delay != 0) {
        m->fire_delay--;
    }
    if (delay >= 3) {
        m->frame = (u16)(m->frame + 0x10);
        m->walk_step = 0;
        return;
    }
    if (m->walk_step != 0) {
        m->frame = (u16)(0x20 + (m->dir & 0x0E) + ((m->walk_step - 1) & 1));
    }
}

/* The screen lead (Mech_Update's .screen): where the mech sits on
 * screen, and so how much the camera
 * moves. mech_lead 0: the mech moves on screen within a box, on each axis
 * where it is inside; outside either, lead 1. Lead 1: it slides 1 px a
 * frame towards MechLeadSplit/Wide for its 8-way heading while the camera
 * takes the difference; there, lead 2: the camera follows fully. */
static void mech_screen_lead(HzGame *g, HzMech *m)
{
    u16 sx = (u16)m->screen_x;
    u16 sy = (u16)m->screen_y;
    if (m->lead == 1) {
        const HzPoint *lead = g->split_screen ? MECH_LEAD_SPLIT
                                              : MECH_LEAD_WIDE;
        const HzPoint *e = &lead[(m->dir & 0x0E) >> 1]; /* 8-way heading */
        u8 moved = 0;
        u16 tx = (u16)e->x;
        if (sx != tx) {
            s16 step = (sx < tx) ? 1 : -1;
            m->screen_x = (s16)(m->screen_x + step);
            m->cam_dx = (s16)(m->cam_dx - step);
            moved |= 1;
        }
        u16 ty = (u16)e->y;
        if (sy != ty) {
            s16 step = (sy < ty) ? 1 : -1;
            m->screen_y = (s16)(m->screen_y + step);
            m->cam_dy = (s16)(m->cam_dy - step);
            moved |= 2;
        }
        if (moved == 0) {
            m->lead = 2;
        }
        return;
    }
    if (m->lead == 2) {
        return;
    }
    u8 out = 0;
    u16 left = g->split_screen ? 0x21 : 0x71;
    u16 right = g->split_screen ? 0x4F : 0xCF;
    if (sx > right || sx < left) {
        out |= 1;
    }
    if (sy > 0x97 || sy < 0x49) {
        out |= 2;
    }
    if (!(out & 1)) {
        m->screen_x = (s16)(sx + m->cam_dx);
        m->cam_dx = 0;
    }
    if (!(out & 2)) {
        m->screen_y = (s16)(sy + m->cam_dy);
        m->cam_dy = 0;
    }
    if (out != 0) {
        m->lead = 1;
    }
}

/* Mech_Update ($32F2) for mech p (a0), the other being the rival (a1). */
void mech_update(HzGame *g, int p)
{
    HzMech *m = &g->mech[p];
    HzMech *rival = &g->mech[1 - p];
    m->cam_dx = 0;
    m->cam_dy = 0;
    if ((m->flags & MECH_CPU) && !g->cpus_frozen) {
        if (g->cpu_brain[p] == HZ_BRAIN_NIGHTMARE) {
            nightmare_think(g, p);      /* (session 57) the port's own */
        } else if (g->cpu_brain[p] == HZ_BRAIN_PROBE
                   && g->brain_probe != NULL) {
            g->brain_probe(g, p, g->brain_probe_user);
        } else {
            mech_ai_think(g, p);                        /* AI_MechThink */
        }
    }

    if (m->flags & MECH_DESTROYED) {
        if (m->menu.closing) {
            return;
        }
        mech_dying(g, m, rival);
    } else {
        if (m->menu.closing) {
            return;
        }
        int want = mech_buttons(g, m, rival);
        if (want == MENU_OPENED) {
            return;
        }
        mech_steer(g, m, (u8)want);
        mech_move(g, m, rival);
    }
    mech_fuel_and_frame(g, m);
    if (m->flags & MECH_DESTROYED) {
        return;
    }
    mech_screen_lead(g, m);
}
