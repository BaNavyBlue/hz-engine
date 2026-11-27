#include "game.h"
#include "tables.h"

#include <string.h>

/* Unit_SetSprite ($2344): a unit's heading one step towards unit_turn_to,
 * its target velocity when the heading or the speed changed, and its
 * sprite attr (and the turret overlay's) for the new heading, OR'd with
 * the team palette `pal` (TEAM1_PALETTE / TEAM2_PALETTE). Nothing for a
 * dying unit.
 *
 * The step: (turn_to - heading) & $F, 1-7 -> +1, 8-$F -> -1. The 68k
 * compares that as a word (cmp.w #$8,d0) whose high byte is whatever the
 * caller left in d0, so a caller with d0 >= $100 always turns -1. The only
 * ported caller is Unit_Spawn, whose d0 is type x 32 (>= $100 for types 8
 * and 9), and it always asks for a difference of $F, which is -1 either
 * way. The turret's step has the same test on d1, whose high byte is 0
 * there for every type but INFANTRY, which has no turret. */
void unit_set_sprite(HzGame *g, HzUnit *u, u16 pal)
{
    if (u->flags & UNIT_DYING) {
        return;
    }
    const u16 (*dir_table)[16] = g->assets->unit_sprites;  /* UnitDirTable */
    u16 speed = u->speed;
    u8 prev = u->prev_speed;
    u->prev_speed = (u8)speed;

    u8 diff = (u8)((u->turn_to - u->dir) & 0x0F);
    int step = 0;
    bool set_velocity = true;
    if (diff != 0) {
        step = diff < 8 ? 1 : -1;
    } else if (prev == (u8)speed) {
        set_velocity = false;
    }

    if (set_velocity) {
        u->dir = (u8)((u->dir + step) & 0x0F);
        u8 d = u->dir;
        if (u->flags & UNIT_REVERSING) {
            d = (u8)(d + 8);
        }
        s16 sx = SINE_TABLE[d & 0x0F].x;
        s16 sy = SINE_TABLE[d & 0x0F].y;
        u->target_vel_x = (s16)(u16)((s32)(s16)speed * sx);    /* muls.w */
        u->target_vel_y = (s16)(u16)((s32)(s16)speed * sy);
    }

    bool repair = repair_on(g);
    if (!unit_on_foot(u->type, repair)) {
        /* (session 72) the repair truck: the SUPPLY's sprites (in its own
         * colours: its sprite size's high bits, video.h) */
        u8 row = (repair && u->type == UNIT_REPAIR) ? UNIT_SUPPLY : u->type;
        u->sprite_attr = (u16)(dir_table[row][u->dir] | pal);
    } else {
        /* INFANTRY: tile $400 + 4 per aim direction, every other tick
         * the walking frame ($440 on) while it moves. (Session 72) A
         * mechanic's too, and while he mends. */
        u16 attr = (u16)((0x400 + (u->aim_dir & 0x0F) * 4) | pal);
        if (u->speed != 0 || (u->type == UNIT_MECHANIC && u->ai.working)) {
            attr = (u16)(attr + ((u->tick << 6) & 0x40));
        }
        u->sprite_attr = attr;
    }

    if (!(u->turret & 0x80)) {      /* b7: a turret overlay, of type b0-b6 */
        return;
    }
    u8 tdiff = (u8)((u->aim_dir - u->turret_dir) & 0x0F);
    int tstep = 0;
    if (tdiff != 0) {
        tstep = tdiff < 8 ? 1 : -1;
    }
    u->turret_dir = (u8)((u->turret_dir + tstep) & 0x0F);
    u->overlay_attr = (u16)(dir_table[u->turret & 0x7F][u->turret_dir]
                            | pal);
}

/* Grid_MarkObj ($18AC): the unit into the v_grid cell (32 px cells, 64 a
 * row) of its centre, (x, y) + obj_size / 2; the cell into obj_grid_cell.
 * Row: (y + s/2) & $FFE0, x 2; column: (x + s/2) >> 5, unsigned; the sum &
 * $FFF (so a column past 63 runs into the next row; no object's centre is
 * ever that far right). */
void grid_mark_unit(HzGame *g, HzUnit *u)
{
    if (g->crowd) {
        crowd_link(g, u);               /* (session 61) UNITS: crowd.c */
        return;
    }
    u16 half = (u16)(u->size >> 1);
    u16 row = (u16)(((u16)(u->y + half) & 0xFFE0) * 2);
    u16 col = (u16)((u16)(u->x + half) >> 5);
    u16 cell = (u16)((row + col) & 0xFFF);
    g->grid[cell] = unit_ref(u);
    u->grid_cell = cell;
}

/* Grid_ClearObj ($1894): the object's v_grid cell (obj_grid_cell, which
 * Grid_MarkObj always leaves & $FFF) cleared: the mech's lift, and (as
 * the 68k's inline clear) Units_UpdateTeam. With UNITS the unit out of
 * its cell's list. */
void grid_clear_unit(HzGame *g, HzUnit *u)
{
    if (g->crowd) {
        crowd_unlink(g, u);
        return;
    }
    g->grid[u->grid_cell & 0xFFF] = ref_none();
}

/* The object in a v_grid cell, as Unit_CheckSpawnBlocked and
 * Unit_CheckObjCollision find it: a mech or a unit. Its x, y, obj_size and
 * the byte at +$01 the collision compares with a SUPPLY's type (a unit's
 * obj_type; QUIRK: a mech's mech_carried_slot, so a mech carrying slot 4
 * counts as a SUPPLY). */
static void grid_occupant(const HzGame *g, HzRef id, s16 *x, s16 *y,
                          u8 *size, u32 *type)
{
    if (id.kind == REF_MECH) {
        const HzMech *m = &g->mech[id.side];
        *x = m->x;
        *y = m->y;
        *size = m->size;
        *type = m->carried_slot;
        return;
    }
    const HzUnit *o = ref_unit_ptr_c(g, id);
    *x = o->x;
    *y = o->y;
    *size = o->size;
    *type = o->type;
}

/* Unit_CheckSpawnBlocked ($1C72): may the unit just set up at obj_x/y
 * stand there? v_hit_flags PROBE_OBJECT: an object overlaps it (v_hit_id's
 * low byte = its grid id), PROBE_TILE: a tile blocks it. v_terrain_event
 * is cleared.
 *
 * Objects: the v_grid cell of its centre (as Grid_MarkObj, but & $7E0 and
 * & $3F) occupied is enough; else the 8 cells round it (GridNeighbourOffsets;
 * a cell number past $FFF is skipped, but the rows aren't: a unit in
 * column 0 looks at the end of the row above, and column 63 at the start
 * of the row below). The first loop pass takes the centre cell again.
 * Overlap, per axis: whichever starts further right (down) starts within
 * the other's obj_size; both axes. The first object that overlaps ends
 * the search.
 *
 * Tiles, whether or not an object was found: from tile ((x + 4) / 8,
 * (y + 4) / 8), 3 x 3 tiles for types below 8, 2 x 2 for INFANTRY and FWA
 * (the map wrapping at 256 tiles); the ST-57U (obj_flags b0) is blocked by
 * a tile without b9, anything else by a tile with b15. The first blocked
 * tile ends the test. */
static void unit_check_spawn_blocked(HzGame *g, const HzUnit *u)
{
    g->hit_flags = 0;
    g->hit_id = ref_none();
    g->terrain_event = 0;

    u16 half = (u16)(u->size >> 1);
    u16 row = (u16)(((u16)(u->y + half) & 0x7E0) * 2);
    u16 col = (u16)(((u16)(u->x + half) >> 5) & 0x3F);
    u16 cell = (u16)((row + col) & 0xFFF);

    HzRef id = g->grid[cell];
    bool blocked = !ref_is_none(id);
    if (g->crowd) {
        id = crowd_hit(g, u, false, NULL);  /* (session 61) UNITS */
        blocked = !ref_is_none(id);
    } else if (!blocked) {
        for (int n = 0; n < 9; n++) {
            s16 off = n == 0 ? 0 : GRID_NEIGHBOUR_OFFSETS[n - 1];
            u16 c = (u16)(cell + off);
            if (c >= 0x1000) {
                continue;
            }
            id = g->grid[c];
            if (ref_is_none(id)) {
                continue;
            }
            s16 ox;
            s16 oy;
            u8 osize;
            u32 otype;
            grid_occupant(g, id, &ox, &oy, &osize, &otype);
            u8 overlap = 0;
            s16 d = (s16)(ox - u->x);
            if (d >= 0) {
                if ((u16)d < u->size) {
                    overlap |= 1;
                }
            } else if ((u16)-d < osize) {
                overlap |= 1;
            }
            d = (s16)(oy - u->y);
            if (d >= 0) {
                if ((u16)d < u->size) {
                    overlap |= 2;
                }
            } else if ((u16)-d < osize) {
                overlap |= 2;
            }
            if (overlap == 3 && hitmask_on(g)) {
                /* (session 75) PIXELS: the pixels it will have, set down */
                HzShape mine;
                HzShape theirs;
                hitmask_unit_new(g, u, u->turn_to, u->x, u->y, &mine);
                hitmask_ref(g, id, &theirs);
                if (!hitmask_touch(&mine, &theirs)) {
                    continue;
                }
            }
            if (overlap == 3) {
                blocked = true;
                break;
            }
        }
    }
    if (blocked) {
        g->hit_flags |= PROBE_OBJECT;
        g->hit_id = id;
    }

    u16 tx0 = (u16)((u16)(u->x + 4) >> 3);
    u16 ty0 = (u16)((u16)(u->y + 4) >> 3);
    int n = unit_tiles(u->type, repair_on(g));
    for (int r = 0; r < n; r++) {
        u16 ty = (u16)(ty0 + r);
        for (int c = 0; c < n; c++) {
            u16 tx = (u16)(tx0 + c);
            u16 block = g->layout[(ty & 0xFC) >> 2][(tx & 0xFC) >> 2] & 0x1FF;
            u16 word = g->blocks[block][(ty & 3) * 4 + (tx & 3)];
            bool tile_blocks = (u->flags & UNIT_BOAT)
                               ? !(word & TILE_WATER)
                               : (word & TILE_BLOCKED) != 0;
            if (tile_blocks) {
                g->hit_flags |= PROBE_TILE;
                return;
            }
        }
    }
}

/* UnitStats + type x $20: the record of a unit type, the 14 units' and
 * then KIDOU (type $E, the mech). Type $F would read the 32 bytes after
 * KIDOU's record ($5DDE0, its 8 form bytes then UnitAvailByMap); no unit
 * has it, so the port gives KIDOU's for it too. (Session 60) With
 * TURRETS, types 6 and 7 take data/turrets.json's records instead;
 * (session 72) with REPAIR, types 10 and 11 data/repair.json's. */
const HzUnitStats *unit_stats_for(const HzGame *g, u8 type)
{
    if ((type == UNIT_TURRET1 || type == UNIT_TURRET2) && turrets_on(g)) {
        return &g->assets->turret_stats[type - UNIT_TURRET1];
    }
    if ((type == UNIT_REPAIR || type == UNIT_MECHANIC) && repair_on(g)) {
        return &g->assets->repair.stats[type - UNIT_REPAIR];
    }
    if (type < HZ_UNIT_TYPES) {
        return &g->assets->unit_stats[type];
    }
    return &g->assets->mech_stats.stats;
}

/* (Session 60) Types 6 and 7 are TURRET-1 and TURRET-2 while the patch is
 * on, and for the rest of a game it has been on in (HzGame.turrets);
 * never without data/turrets.json. */
bool turrets_on(const HzGame *g)
{
    return g->assets->turrets_loaded
        && (g->turrets || (g->fixes & HZ_FIX_TURRETS));
}

/* (Session 72) Types 10 and 11 are the repair truck and its mechanic
 * while REPAIR is on, and for the rest of a game it has been on in
 * (HzGame.repair); never without data/repair.json. */
bool repair_on(const HzGame *g)
{
    return g->assets->repair_loaded
        && (g->repair || (g->fixes & HZ_FIX_REPAIR));
}

/* (Session 60) The unit types the order menu offers here: UnitAvailByMap's,
 * and with TURRETS the two turrets, (session 72) with REPAIR the repair
 * truck -- but not in the attract demo, whose recordings step through the
 * original's list. */
u16 shop_types(const HzGame *g)
{
    u16 mask = g->assets->map[g->map & 7].buyable;
    if ((g->fixes & HZ_FIX_TURRETS) && g->assets->turrets_loaded
        && g->game_type >= 0) {
        mask |= (1u << UNIT_TURRET1) | (1u << UNIT_TURRET2);
    }
    if ((g->fixes & HZ_FIX_REPAIR) && g->assets->repair_loaded
        && g->game_type >= 0) {
        mask |= 1u << UNIT_REPAIR;
    }
    return mask;
}

/* (Session 72) An order's OrderInfo record and its icon, as the order
 * menu shows them: with REPAIR, order $E is the repair truck's. */
const HzOrderInfo *order_info_for(const HzGame *g, u8 order)
{
    if ((order & 0x0F) == ORDER_REPAIR && repair_on(g)) {
        return &g->assets->repair.order;
    }
    return &g->assets->orders[order & 0x0F];
}

const u8 *order_icon_for(const HzGame *g, u8 order)
{
    if ((order & 0x0F) == ORDER_REPAIR && repair_on(g)) {
        return g->assets->repair.order_icon;
    }
    return g->assets->order_icons[order & 0x0F];
}

/* (Session 72) The draw layer's sprite colour tables 1 and 2, the
 * truck's and the mechanic's sprites' (their sprite sizes' high bits name
 * them), on while the game has REPAIR and off otherwise. */
void repair_set_colours(HzGame *g)
{
    bool on = repair_on(g);
    for (int i = 0; i < 2; i++) {
        video_sprite_colours(g->video, i + 1,
                             on ? g->assets->repair.colours[i] : NULL);
    }
}

/* (Session 72) A mechanic turned into INFANTRY where he stands (his
 * truck destroyed, or no way back to it): the type and its counts, the
 * INFANTRY's gun, fuel and sprite, the capture order; his HP kept. */
void mechanic_to_infantry(HzGame *g, HzUnit *u)
{
    int team = (u->flags & UNIT_TEAM2) ? 1 : 0;
    const HzUnitStats *st = unit_stats_for(g, UNIT_INFANTRY);
    g->unit_type_count[team][UNIT_MECHANIC]--;
    g->unit_type_count[team][UNIT_INFANTRY]++;
    u->type = UNIT_INFANTRY;
    u->order = ORDER_CAPTURE;
    if (u->hp > st->hp) {
        u->hp = st->hp;
    }
    u->ammo = st->ammo;
    u->missiles = st->missiles;
    u->fuel = st->fuel;
    u->sprite_size = (u16)(st->sprite_size << 8);
    u->speed = 0;
    u->max_speed = st->speed;
    u->prev_speed = 0;
    u->turret = st->overlay;
    u->power = st->power;
    u->fuel_use = st->fuel_use;
    u->phase = 0;
    u->fire_delay = 0;
    u->shot_flags = 0;
    memset(&u->ai, 0, sizeof u->ai);
}

/* (Session 72) Unit u's record has just been freed. A mechanic: his
 * truck, if it is still there, has a man less (REPAIR_MAN of its ammo).
 * A repair truck: its men out become INFANTRY (one already dying only
 * forgets it, so the slot's next truck doesn't lose a man for him). */
void repair_unit_freed(HzGame *g, const HzUnit *u)
{
    if (!repair_on(g)) {
        return;
    }
    if (u->type == UNIT_MECHANIC) {
        HzUnit *t = ref_unit_ptr(g, u->ai.truck_id);
        if (t != NULL && (t->flags & UNIT_ACTIVE) && t->type == UNIT_REPAIR) {
            t->ammo = (u8)((t->ammo >= REPAIR_MAN) ? t->ammo - REPAIR_MAN
                                                   : 0);
        }
        return;
    }
    if (u->type != UNIT_REPAIR) {
        return;
    }
    HzRef truck = unit_ref(u);
    for (u32 slot = 0; slot < side_slots(g, u->side); slot++) {
        HzUnit *m = unit_at(g, u->side, slot);
        if (!(m->flags & UNIT_ACTIVE) || m->type != UNIT_MECHANIC
            || !ref_eq(m->ai.truck_id, truck)) {
            continue;
        }
        if (m->flags & UNIT_DYING) {
            m->ai.truck_id = ref_none();
        } else {
            mechanic_to_infantry(g, m);
        }
    }
}

/* The first free slot of a side, or -1: with UNITS a side whose slots are
 * all taken grows one (the original's 50 records a side had no more). */
static long free_slot(HzGame *g, int side)
{
    u32 n = side_slots(g, side);
    for (u32 slot = 0; slot < n; slot++) {
        if (!(unit_at(g, side, slot)->flags & UNIT_ACTIVE)) {
            return (long)slot;
        }
    }
    if (units_unlimited(g) && side_grow(g, side)) {
        return (long)n;
    }
    return -1;
}

/* Unit_Spawn ($96BC): a new unit in the first free slot of the team in
 * `flags` (UNIT_TEAM2). Returns 0, or -1 if the team's slots are full (50
 * in the original; with UNITS never) or the place is blocked
 * (Unit_CheckSpawnBlocked); then the record keeps everything written but
 * its flags, which are cleared. (The 68k also returns slot + 1 in d7; no
 * caller uses it.)
 *
 * The record: cleared, then flags (UNIT_BOAT added for the ST-57U), type
 * & $F,
 * order; heading dir + 1 and unit_turn_to dir, so that Unit_SetSprite
 * turns it back one step and sets its velocity; from its UnitStats
 * record hp, size, ammo, missiles, fuel, sprite size (the size/link
 * word's high byte), speed (unit_speed's low byte and unit_max_speed),
 * turret, power and fuel use; unit_turret_dir and unit_aim_dir = dir;
 * position. Placed: the team's counts (v_unit_count, v_unit_type_count,
 * v_units_built saturating at $FFFF), the unit in v_grid, and
 * Unit_SetSprite with the team palette. */
int unit_spawn(HzGame *g, u8 flags, u8 type, u8 order, u8 dir, s16 x, s16 y)
{
    return (unit_spawn_unit(g, flags, type, order, dir, x, y) != NULL) ? 0
                                                                       : -1;
}

/* (Session 72) Unit_Spawn, giving the new unit's record (NULL for -1). */
HzUnit *unit_spawn_unit(HzGame *g, u8 flags, u8 type, u8 order, u8 dir,
                        s16 x, s16 y)
{
    int team = (flags & UNIT_TEAM2) ? 1 : 0;
    long slot = free_slot(g, team);
    if (slot < 0) {
        return NULL;
    }
    HzUnit *u = unit_at(g, team, (u32)slot);

    type &= 0x0F;
    if (type == UNIT_ST) {
        flags |= UNIT_BOAT;
    }
    unit_clear(u);
    const HzUnitStats *st = unit_stats_for(g, type);    /* UnitStats */
    u->flags = flags;
    u->type = type;
    u->order = order;
    u->dir = (u8)((dir + 1) & 0x0F);
    u->turn_to = (u8)(dir & 0x0F);
    u->hp = st->hp;
    u->size = st->size;
    u->ammo = st->ammo;
    u->missiles = st->missiles;
    u->fuel = st->fuel;
    u->sprite_size = (u16)(st->sprite_size << 8);
    u->speed = st->speed;           /* unit_speed's low byte (+$25) */
    u->max_speed = st->speed;
    u->prev_speed = 0;
    u->turret = st->overlay;
    u->turret_dir = u->turn_to;     /* (+$D cleared again: already 0) */
    u->aim_dir = u->turn_to;
    u->power = st->power;
    u->fuel_use = st->fuel_use;
    u->x = x;
    u->y = y;

    unit_check_spawn_blocked(g, u);
    if (g->hit_flags != 0) {
        u->flags = 0;
        return NULL;
    }

    g->unit_count[team]++;
    g->unit_type_count[team][type]++;
    if (g->units_built[team] != 0xFFFF) {
        g->units_built[team]++;
    }
    grid_mark_unit(g, u);
    unit_set_sprite(g, u, team ? TEAM2_PALETTE : TEAM1_PALETTE);
    return u;
}

/* Unit_CreateCarried ($97F8): as Unit_Spawn, for a unit a mech picks up
 * at an outpost (Outpost_PickUpProduct): the first free slot of the team
 * in `flags` (none: -1), the record cleared and filled from UnitStats
 * the same way, but with no heading, turret or aim direction, place, grid
 * mark or Unit_SetSprite (the mech carries it), and never blocked. The
 * team's counts go up as Unit_Spawn's do. Returns 0 with the slot (the
 * 68k's d7, not + 1) in *slot. */
int unit_create_carried(HzGame *g, u8 flags, u8 type, u8 order, u32 *slot)
{
    int team = (flags & UNIT_TEAM2) ? 1 : 0;
    long n = free_slot(g, team);
    if (n < 0) {
        return -1;
    }
    HzUnit *u = unit_at(g, team, (u32)n);

    type &= 0x0F;
    if (type == UNIT_ST) {
        flags |= UNIT_BOAT;
    }
    unit_clear(u);
    const HzUnitStats *st = unit_stats_for(g, type);    /* UnitStats */
    u->flags = flags;
    u->type = type;
    u->order = order;
    u->hp = st->hp;
    u->size = st->size;
    u->ammo = st->ammo;
    u->missiles = st->missiles;
    u->fuel = st->fuel;
    u->sprite_size = (u16)(st->sprite_size << 8);
    u->speed = st->speed;           /* unit_speed's low byte (+$25) */
    u->max_speed = st->speed;
    u->prev_speed = 0;
    u->turret = st->overlay;
    u->power = st->power;
    u->fuel_use = st->fuel_use;

    g->unit_count[team]++;
    g->unit_type_count[team][type]++;
    if (g->units_built[team] != 0xFFFF) {
        g->units_built[team]++;
    }
    *slot = (u32)n;
    return 0;
}

/* Units_SpawnFromRecords ($963A): the 32 StartUnits records of this map's
 * half `half` (0 or 1: the first or second $100 bytes), each (flags, type,
 * order, dir, x word, y word); a record is present when its flags have
 * UNIT_ACTIVE, and its team is its flags' own. A GRM record becomes type
 * `grm_type` guarding near its start point, unless grm_type is the GRM
 * too (then it keeps its own order). */
static void units_spawn_from_records(HzGame *g, int half, u8 grm_type)
{
    const HzStartUnit *rec = g->assets->start_units[g->map][half];
    int count = g->assets->start_unit_count[g->map][half];
    for (int i = 0; i < count; i++, rec++) {
        u8 flags = rec->flags;
        if (!(flags & UNIT_ACTIVE)) {
            continue;
        }
        u8 type = rec->type;
        u8 order = rec->order;
        if (type == UNIT_GRM && grm_type != UNIT_GRM) {
            type = grm_type;
            order = ORDER_GUARD_NEAR;
        }
        unit_spawn(g, flags, type, order, rec->dir, rec->x,
                   rec->y);
    }
}

/* Units_SpawnCPUStart ($95EE): a CPU player against a human starts with
 * units, from its half of StartUnits: P1 the first ($100 bytes), P2 the
 * second, with the GRM records turned into CPUStartTypeTable[difficulty]
 * (A AMR, B SAM, C TAX, D GRM). CPU against CPU: none. */
void units_spawn_cpu_start(HzGame *g)
{
    if (g->player_flags[0] & g->player_flags[1] & MECH_CPU) {
        return;
    }
    u8 grm_type = g->assets->cpu_start_types[g->options[1] & 3];
    if (g->player_flags[0] & MECH_CPU) {
        units_spawn_from_records(g, 0, grm_type);
    }
    if (g->player_flags[1] & MECH_CPU) {
        units_spawn_from_records(g, 1, grm_type);
    }
}

/* -- Combat_Update's other half: the units' move ---------------------- */

/* Unit_CheckObjCollision ($1924): does unit u, just moved, overlap an
 * object? v_hit_flags (and v_hit_id) cleared, then PROBE_OBJECT an object
 * overlaps (v_hit_id = its grid id) and PROBE_FOE that object is the
 * other side's.
 *
 * The cell of its centre, (x, y) + size / 2 (& $7E0, & $3F as
 * Unit_CheckSpawnBlocked; Units_UpdateTeam has just cleared its own),
 * occupied: a hit at once. QUIRK: the 68k branches to .hit without
 * loading the occupant's id, so d0 still holds the row offset ((y + size
 * / 2) & $7E0) x 2 and its low byte is stored: $00, $40, $80 or $C0 by
 * the row & 3, whatever the occupant is. Here REF_ROW with that byte: its
 * b7 decides "the other side's" as a grid id's team bit did, and
 * unit_set_bumped reads it as the 68k did.
 *
 * Else the 8 cells round it (GridNeighbourOffsets; a cell number past
 * $FFF, unsigned, is skipped: the 68k reads the byte first, harmlessly):
 * the first object that overlaps on both axes (whichever of the two starts
 * further right / down starts within the other's size). A SUPPLY (type 4)
 * adds $C to both sizes, on either side; for a mech the byte read as its
 * type is mech_carried_slot (+$01). */
static void unit_check_obj_collision(HzGame *g, const HzUnit *u,
                                     HzPoint from)
{
    g->hit_flags = 0;
    g->hit_id = ref_none();
    if (g->crowd) {
        /* (Session 61) UNITS: the boxes, no taken-centre rule; (session
         * 68) its own side's units too, and a unit on top of another (from
         * the original's grid, UNITS turned on in play) may move off it. */
        HzRef id = crowd_hit(g, u, !(g->fixes & HZ_FIX_SUPPLY), &from);
        if (!ref_is_none(id)) {
            g->hit_flags |= PROBE_OBJECT;
            g->hit_id = id;
            if (id.side != ((u->flags & UNIT_TEAM2) ? 1 : 0)) {
                g->hit_flags |= PROBE_FOE;
            }
        }
        return;
    }
    u16 half = (u16)(u->size >> 1);
    u16 row = (u16)(((u16)(u->y + half) & 0x7E0) * 2);
    u16 col = (u16)(((u16)(u->x + half) >> 5) & 0x3F);
    u16 cell = (u16)((row + col) & 0xFFF);

    HzRef id = ref_none();
    bool hit = false;
    if (!ref_is_none(g->grid[cell])) {
        id = ref_row((u8)row);                          /* d0's low byte */
        hit = true;
    } else {
        /* FIX (HZ_FIX_SUPPLY): no SUPPLY bumper. The original's $C round a
         * SUPPLY made it stop short of its customer, which was how it
         * docked; but it also needs a 48 px gap to pass between two units
         * where any other unit needs 24, so it can't get into a group.
         * With the fixes a SUPPLY docks by reach (unit_ai.c) and collides
         * with its plain box, as everything else does. */
        bool bumper = !(g->fixes & HZ_FIX_SUPPLY);
        bool self_supply = u->type == UNIT_SUPPLY && bumper;
        u16 own = (u16)(u->size + (self_supply ? 0x0C : 0));
        for (int n = 0; n < 8 && !hit; n++) {
            u16 c = (u16)(cell + GRID_NEIGHBOUR_OFFSETS[n]);
            if (c >= 0x1000 || ref_is_none(g->grid[c])) {
                continue;
            }
            id = g->grid[c];
            s16 ox;
            s16 oy;
            u8 osize;
            u32 otype;
            grid_occupant(g, id, &ox, &oy, &osize, &otype);
            u16 mine = own;
            u16 theirs = osize;
            if (self_supply) {
                theirs = (u16)(theirs + 0x0C);
            } else if (otype == UNIT_SUPPLY && bumper) {
                mine = (u16)(mine + 0x0C);
                theirs = (u16)(theirs + 0x0C);
            }
            u8 overlap = 0;
            s16 d = (s16)(ox - u->x);
            if (d >= 0) {
                if ((u16)d < mine) {
                    overlap |= 1;
                }
            } else if ((u16)-d < theirs) {
                overlap |= 1;
            }
            d = (s16)(oy - u->y);
            if (d >= 0) {
                if ((u16)d < mine) {
                    overlap |= 2;
                }
            } else if ((u16)-d < theirs) {
                overlap |= 2;
            }
            hit = overlap == 3;
            if (hit && hitmask_on(g) && !self_supply
                && !(otype == UNIT_SUPPLY && bumper)) {
                /* (session 75) PIXELS, no bumper: the pixels touch */
                HzShape us;
                HzShape them;
                hitmask_unit(g, u, &us);
                hitmask_ref(g, id, &them);
                hit = hitmask_blocks(&us, &from, &them);
            }
        }
    }
    if (hit) {
        g->hit_flags |= PROBE_OBJECT;
        g->hit_id = id;
        if (id.side != ((u->flags & UNIT_TEAM2) ? 1 : 0)) {
            g->hit_flags |= PROBE_FOE;
        }
    }
}

/* Obj_CheckTerrain ($1B50): the tiles under unit u. v_terrain_event is
 * cleared; v_hit_flags isn't (the collision's PROBE_OBJECT and PROBE_FOE
 * stay) and gets PROBE_TILE for the map edge or a tile in the way,
 * PROBE_EVENT for a terrain event.
 *
 * The edge: the tile column (x + 4) / 8 - 1, or the row, at or past $FF -
 * size / 8, unsigned (so (x + 4) / 8 = 0 too). The tiles from ((x + 4) /
 * 8, (y + 4) / 8), 3 x 3 for types below 8, 2 x 2 for INFANTRY and FWA
 * (the map wrapping at 256 tiles), row by row: the ST-57U is stopped by a
 * tile without b9 (water), anything else by one with b15; the
 * first tile with b13 is the event (v_terrain_event3 its layout word,
 * v_terrain_event its block's terrain). It ends once both are found.
 * (Once the tile test is over the 68k still tests b9 on each tile, which
 * can only set what is already set.) */
void obj_check_terrain(HzGame *g, const HzUnit *u)
{
    g->terrain_event = 0;
    u8 look = 3;            /* b0 tiles still to test, b1 no event yet */
    u16 last = (u16)(0xFF - (u->size >> 3));
    u16 tx = (u16)(((u16)(u->x + 4) >> 3) - 1);
    if (tx >= last) {
        g->hit_flags |= PROBE_TILE;
        look &= (u8)~1;
    }
    tx = (u16)(tx + 1);
    u16 ty = (u16)(((u16)(u->y + 4) >> 3) - 1);
    if (ty >= last) {
        g->hit_flags |= PROBE_TILE;
        look &= (u8)~1;
    }
    ty = (u16)(ty + 1);
    int n = unit_tiles(u->type, repair_on(g));
    for (int r = 0; r < n; r++, ty++) {
        u16 x = tx;
        for (int c = 0; c < n; c++, x++) {
            u16 lw = g->layout[(ty & 0xFC) >> 2][(x & 0xFC) >> 2];
            u16 block = (u16)(lw & 0x1FF);
            u16 word = g->blocks[block][(ty & 3) * 4 + (x & 3)];
            bool blocked;
            if ((look & 1) && !(u->flags & UNIT_BOAT)) {
                blocked = (word & TILE_BLOCKED) != 0;
            } else {
                blocked = !(word & TILE_WATER);
            }
            if (blocked) {
                g->hit_flags |= PROBE_TILE;
                look &= (u8)~1;
            }
            if ((look & 2) && (word & TILE_EVENT)) {
                g->terrain_event3 = lw;
                g->terrain_event = g->block_terrain[block];
                g->hit_flags |= PROBE_EVENT;
                look &= (u8)~2;
            }
            if (look == 0) {
                return;
            }
        }
    }
}

/* unit_bumped from v_hit_id: the mech or unit it ran into. QUIRK: the row
 * byte the 68k left in v_hit_id (Unit_CheckObjCollision) it read as a grid
 * id: $00 P1's mech, $80 P2's, $C0 team 1's slot 49 (slot -1 + $32 for
 * team 2), and $40 slot -1 of team 1 ($FF: no unit; kept as REF_ROW). */
static void unit_set_bumped(HzGame *g, HzUnit *u)
{
    HzRef id = g->hit_id;
    if (id.kind == REF_ROW) {
        switch (id.slot) {
        case 0x00:
            id = ref_mech(0);
            break;
        case 0x80:
            id = ref_mech(1);
            break;
        case 0xC0:
            id = ref_unit(0, HZ_SIDE_UNITS - 1);
            break;
        default:
            break;
        }
    }
    u->bumped = id;
}

/* FIX (HZ_FIX_SUPPLY): may SUPPLY u, just moved from (old_x, old_y), go on
 * though it hits something? Yes when the move takes it away from what it
 * hits (their centres further apart). Unit_CheckObjCollision blocks every
 * move while a SUPPLY's bumper (its box + $C) overlaps another object,
 * whichever way it goes, so in the original a SUPPLY put down inside a
 * tight group of units never moves again. (The object is the centre
 * cell's occupant when that cell is taken: v_hit_id then holds the row,
 * see Unit_CheckObjCollision.) */
static bool supply_backs_away(const HzGame *g, const HzUnit *u, s16 old_x,
                              s16 old_y)
{
    u16 half = (u16)(u->size >> 1);
    HzRef id = g->hit_id;           /* with UNITS (crowd) what it hit */
    if (!g->crowd) {
        u16 row = (u16)(((u16)(u->y + half) & 0x7E0) * 2);
        u16 col = (u16)(((u16)(u->x + half) >> 5) & 0x3F);
        HzRef centre = g->grid[(row + col) & 0xFFF];
        if (!ref_is_none(centre)) {
            id = centre;
        }
    }
    if (id.kind != REF_MECH && id.kind != REF_UNIT) {
        return false;       /* not reached: the row only with a centre */
    }
    s16 ox;
    s16 oy;
    u8 osize;
    u32 otype;
    grid_occupant(g, id, &ox, &oy, &osize, &otype);
    long cx = ox + (osize >> 1) - half;     /* its centre, less our half */
    long cy = oy + (osize >> 1) - half;
    long before = (cx - old_x) * (cx - old_x) + (cy - old_y) * (cy - old_y);
    long after = (cx - u->x) * (cx - u->x) + (cy - u->y) * (cy - u->y);
    return after > before;
}

/* Units_UpdateTeam's move (.move to .terrain_bumped): the velocity (by
 * what the ground did last move, unit_push: as mech_terrain_flow), the
 * 8.8 step, and what it runs into: an object or a tile in the way puts
 * it back and stops it. While it stays on one tile, only the collision is
 * tested again; a new tile costs fuel and gets the ground's test. */
static void unit_move(HzGame *g, HzUnit *u)
{
    s16 old_x = u->x;
    s16 old_y = u->y;
    u8 push = u->push;
    if (!(push & 0x80)) {
        /* A current (or nothing, 0); an INFANTRY is pushed less. */
        s16 vx = u->target_vel_x;
        s16 vy = u->target_vel_y;
        if (push != 0) {
            int shift = unit_on_foot(u->type, repair_on(g)) ? 2 : 6;
            s16 amount = (s16)((push & 0x0F) << shift);
            u16 code = (u16)((push >> 4) & 7);
            if (code != 0 && code != 4) {
                vx = (s16)((code < 4) ? vx + amount : vx - amount);
            }
            code = (u16)(code - 2);
            if (code != 0 && code != 4) {
                vy = (s16)((code < 4) ? vy + amount : vy - amount);
            }
        }
        u->vel_x = vx;
        u->vel_y = vy;
    } else if (push & 0x40) {
        /* Slow ground. */
        s16 vx = u->target_vel_x;
        s16 vy = u->target_vel_y;
        switch (push & 3) {
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
        u->vel_x = vx;
        u->vel_y = vy;
    } else {
        /* Slippery ground: Obj_Accelerate with a smaller step. */
        s16 step = (s16)((push & 0x1F) * 2);
        u->vel_x = obj_accelerate_axis(u->vel_x, u->target_vel_x, step);
        u->vel_y = obj_accelerate_axis(u->vel_y, u->target_vel_y, step);
    }

    u->sub_x = (u16)(u->sub_x + (u16)u->vel_x);
    u->x = (s16)(u->x + (s8)(u->sub_x >> 8));
    u->sub_x &= 0x00FF;
    u->sub_y = (u16)(u->sub_y + (u16)u->vel_y);
    u->y = (s16)(u->y + (s8)(u->sub_y >> 8));
    u->sub_y &= 0x00FF;
    unit_check_obj_collision(g, u, (HzPoint){ old_x, old_y });
    if ((g->fixes & HZ_FIX_SUPPLY) && u->type == UNIT_SUPPLY
        && (g->hit_flags & PROBE_OBJECT)
        && supply_backs_away(g, u, old_x, old_y)) {
        g->hit_flags &= (u16)~(PROBE_OBJECT | PROBE_FOE);
        g->hit_id = ref_none();
    }

    bool same_tile = ((u16)(old_x + 4) >> 3) == ((u16)(u->x + 4) >> 3)
                  && ((u16)(old_y + 4) >> 3) == ((u16)(u->y + 4) >> 3);
    if (same_tile) {
        if (!(u->blocked & (PROBE_TILE | PROBE_EVENT))) {
            u->blocked = (u8)((u->blocked & ~(PROBE_OBJECT | PROBE_FOE))
                              | (g->hit_flags & (PROBE_OBJECT | PROBE_FOE)));
            u->terrain = g->terrain_event;  /* not checked: the last probe's */
            if (u->blocked & (PROBE_OBJECT | PROBE_TILE)) {
                u->speed = 0;
                u->prev_speed = 0;
                u->x = old_x;
                u->y = old_y;
            }
            if (u->blocked & PROBE_OBJECT) {
                unit_set_bumped(g, u);
            }
            return;
        }
    } else if (u->speed != 0) {
        u->fuel = (u->fuel >= u->fuel_use) ? (u16)(u->fuel - u->fuel_use)
                                           : 0;
    }

    u->push = 0;
    obj_check_terrain(g, u);
    u16 hit = g->hit_flags;
    u->blocked = (u8)hit;
    u->terrain = g->terrain_event;
    if (hit & PROBE_EVENT) {
        unit_terrain_event(g, u);
    }
    if (hit & (PROBE_OBJECT | PROBE_TILE)) {
        u->speed = 0;
        u->prev_speed = 0;
        u->x = old_x;
        u->y = old_y;
    }
    if (hit & PROBE_OBJECT) {
        unit_set_bumped(g, u);
    }
}

/* Unit_SpawnSurvivor ($9696): the INFANTRY a destroyed AMR, TAX or SAM
 * leaves: Unit_Spawn with the dead unit's active and team bits, its
 * order and heading, at its x + 4, y + 4 (the record's flags are already
 * cleared; the rest is read before Unit_Spawn, which may reuse the slot). */
static void unit_spawn_survivor(HzGame *g, const HzUnit *u, u8 flags)
{
    unit_spawn(g, (u8)(flags & (UNIT_ACTIVE | UNIT_TEAM2)), UNIT_INFANTRY,
               u->order, u->dir,
               (s16)(u->x + 4), (s16)(u->y + 4));
}

/* Units_UpdateTeam's .dying: the explosion counts down (HP - 1). At 0 the
 * record is freed (flags 0), the team's v_unit_count and type count - 1,
 * and an AMR, TAX or SAM (types 0-2) with unit_tick & 3 = 0 leaves an
 * INFANTRY; returns false (no grid mark). Else the frame: INFANTRY tile
 * $480 + (15 - HP) x 4 with the team palette, others ExplosionFrames[15 -
 * HP] as both sprite attrs. (A dying unit's HP starts at $10, from a shot
 * or the ground, so 15 - HP is 0-14; the 68k would index before the table
 * past that.) */
static bool unit_dying(HzGame *g, HzUnit *u, int team)
{
    u->hp = (u16)(u->hp - 1);
    if (u->hp == 0) {
        u8 flags = u->flags;
        u->flags = 0;
        g->unit_count[team]--;
        g->unit_type_count[team][u->type & 0x0F]--;
        if (u->type <= UNIT_SAM && (u->tick & 3) == 0) {
            unit_spawn_survivor(g, u, flags);
        }
        if (u->type >= UNIT_REPAIR) {
            repair_unit_freed(g, u);    /* (session 72) REPAIR */
        }
        return false;
    }
    u16 frame = (u16)((0x0F - u->hp) & 0x0F);
    if (unit_on_foot(u->type, repair_on(g))) {
        u16 pal = (u->flags & UNIT_TEAM2) ? TEAM2_PALETTE : TEAM1_PALETTE;
        u->sprite_attr = (u16)((0x480 + frame * 4) | pal);
    } else {
        u16 attr = g->assets->explosion_frames[frame];
        u->sprite_attr = attr;
        u->overlay_attr = attr;
        if (u->type == UNIT_REPAIR && repair_on(g)) {
            u->sprite_size &= 0x0FFF;   /* (session 72) the explosion in
                                           its own colours, not the
                                           truck's (video.h) */
        }
    }
    return true;
}

/* Units_UpdateTeam ($1182): Combat_Update's move of one team's units (team
 * 0 or 1), slot by slot. Each record active or dying and not carried: its
 * v_grid cell cleared; dying: the explosion's frame or the end; else, out
 * of fuel, the target velocity cleared, and moving (unit_speed, or a
 * terrain event last time: a current moves a stopped unit) the move.
 * Then, unless the record was freed, its grid mark again (Grid_MarkObj's
 * arithmetic). A survivor spawned on the way may grow the side (UNITS):
 * the count is read each pass. */
void units_update_team(HzGame *g, int team)
{
    for (u32 slot = 0; slot < side_slots(g, team); slot++) {
        HzUnit *u = unit_at(g, team, slot);
        u8 f = (u8)(u->flags & (UNIT_ACTIVE | UNIT_DYING | UNIT_CARRIED));
        if (f == 0 || (f & UNIT_CARRIED)) {
            continue;
        }
        /* obj_grid_cell only ever holds Grid_MarkObj's cells, & $FFF. */
        grid_clear_unit(g, u);
        if (f & UNIT_DYING) {
            if (!unit_dying(g, u, team)) {
                continue;
            }
        } else {
            if (u->fuel == 0) {
                u->target_vel_x = 0;
                u->target_vel_y = 0;
            }
            if (u->speed != 0 || (u->blocked & PROBE_EVENT)) {
                unit_move(g, u);
            }
        }
        if (u->flags != 0) {
            grid_mark_unit(g, u);
        }
    }
}
