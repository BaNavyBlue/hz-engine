/* (Session 61) The units' storage: a list a side (HzUnitList), slots in
 * chunks that never move, so an HzUnit pointer stays good for the game.
 * The original had two tables of 50 records (v_units1/2); every game here
 * starts each side with those 50 slots, free, and only UNITS lets a side
 * grow past them -- so with the patch off every slot, every scan over a
 * side's slots and every free record's leftovers are the original's. */

#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void unit_clear(HzUnit *u)
{
    u8 side = u->side;
    u32 slot = u->slot;
    memset(u, 0, sizeof *u);
    u->side = side;
    u->slot = slot;
}

/* A new chunk, zeroed, each record knowing its place. */
static bool list_add_chunk(HzUnitList *l, int side)
{
    HzUnit **chunk = realloc(l->chunk, (l->chunks + 1) * sizeof *chunk);
    if (chunk == NULL) {
        return false;
    }
    l->chunk = chunk;
    HzUnit *c = calloc(HZ_UNIT_CHUNK, sizeof *c);
    if (c == NULL) {
        return false;
    }
    for (u32 i = 0; i < HZ_UNIT_CHUNK; i++) {
        c[i].side = (u8)side;
        c[i].slot = l->chunks * HZ_UNIT_CHUNK + i;
    }
    l->chunk[l->chunks++] = c;
    return true;
}

static void out_of_memory(void)
{
    fprintf(stderr, "hz: out of memory for the units\n");
    exit(1);
}

HzUnit *unit_at(HzGame *g, int side, u32 slot)
{
    return &g->unit_list[side].chunk[slot / HZ_UNIT_CHUNK]
                                    [slot % HZ_UNIT_CHUNK];
}

const HzUnit *unit_at_c(const HzGame *g, int side, u32 slot)
{
    return &g->unit_list[side].chunk[slot / HZ_UNIT_CHUNK]
                                    [slot % HZ_UNIT_CHUNK];
}

u32 side_slots(const HzGame *g, int side)
{
    return g->unit_list[side].slots;
}

HzUnit *ref_unit_ptr(HzGame *g, HzRef r)
{
    if (r.kind != REF_UNIT || r.side > 1
        || r.slot >= g->unit_list[r.side].slots) {
        return NULL;
    }
    return unit_at(g, r.side, r.slot);
}

const HzUnit *ref_unit_ptr_c(const HzGame *g, HzRef r)
{
    if (r.kind != REF_UNIT || r.side > 1
        || r.slot >= g->unit_list[r.side].slots) {
        return NULL;
    }
    return unit_at_c(g, r.side, r.slot);
}

HzRef unit_ref(const HzUnit *u)
{
    return ref_unit(u->side, u->slot);
}

void units_reset(HzGame *g)
{
    for (int side = 0; side < 2; side++) {
        HzUnitList *l = &g->unit_list[side];
        while (l->chunks * HZ_UNIT_CHUNK < HZ_SIDE_UNITS) {
            if (!list_add_chunk(l, side)) {
                out_of_memory();
            }
        }
        for (u32 c = 0; c < l->chunks; c++) {
            for (u32 i = 0; i < HZ_UNIT_CHUNK; i++) {
                unit_clear(&l->chunk[c][i]);
            }
        }
        l->slots = HZ_SIDE_UNITS;
    }
}

void units_clear_all(HzGame *g)
{
    for (int side = 0; side < 2; side++) {
        u32 slots = side_slots(g, side);
        for (u32 i = 0; i < slots; i++) {
            unit_clear(unit_at(g, side, i));
        }
    }
    /* With UNITS the grid's lists go with them (crowd.c); off, they are
     * empty, and v_grid keeps what it had, as the original's. */
    memset(g->crowd_head, 0, sizeof g->crowd_head);
    memset(g->crowd_count, 0, sizeof g->crowd_count);
    if (g->crowd_fine != NULL) {
        memset(g->crowd_fine->head, 0, sizeof g->crowd_fine->head);
    }
}

bool side_grow(HzGame *g, int side)
{
    HzUnitList *l = &g->unit_list[side];
    if (l->slots == l->chunks * HZ_UNIT_CHUNK && !list_add_chunk(l, side)) {
        return false;
    }
    unit_clear(unit_at(g, side, l->slots));
    l->slots++;
    return true;
}

HzUnit *victim_next_record(HzGame *g, HzRef v)
{
    if (v.kind == REF_NONE) {
        return unit_at(g, 0, 0);                /* code 0: record 0 */
    }
    if (v.kind != REF_UNIT || v.slot >= side_slots(g, v.side)) {
        return NULL;
    }
    if (v.slot + 1 < side_slots(g, v.side)) {
        return unit_at(g, v.side, v.slot + 1);
    }
    return (v.side == 0) ? unit_at(g, 1, 0) : NULL;
}

bool units_unlimited(const HzGame *g)
{
    return (g->fixes & HZ_FIX_UNITS) && g->game_type >= 0;
}

bool side_has_room(const HzGame *g, int side)
{
    return units_unlimited(g) || g->unit_count[side] < HZ_SIDE_UNITS;
}

void game_copy(HzGame *dst, const HzGame *src)
{
    if (dst == src) {
        return;
    }
    HzUnitList keep[2];
    memcpy(keep, dst->unit_list, sizeof keep);
    HzSpriteMore more = dst->sprite_more;
    HzCrowdFine *fine = dst->crowd_fine;
    HzNightmare nm_keep[2];
    memcpy(nm_keep, dst->nightmare, sizeof nm_keep);
    memcpy(dst, src, sizeof *dst);
    /* (Session 64) The views' cameras point into the game's own camera
     * array: the copy's into its own (they read src's, a pass behind,
     * once the copy ran a pass: nightmare_think_ahead's). */
    for (int v = 0; v < 2; v++) {
        if (src->view_camera[v] != NULL) {
            dst->view_camera[v] = &dst->camera[src->view_camera[v]
                                               - src->camera];
        }
    }
    memcpy(dst->unit_list, keep, sizeof keep);
    dst->sprite_more = more;        /* its own; rebuilt each frame */
    dst->sprite_more.count = 0;
    dst->crowd_fine = fine;
    crowd_copy(dst, src);
    for (int side = 0; side < 2; side++) {
        HzUnitList *d = &dst->unit_list[side];
        const HzUnitList *s = &src->unit_list[side];
        while (d->chunks * HZ_UNIT_CHUNK < s->slots) {
            if (!list_add_chunk(d, side)) {
                out_of_memory();
            }
        }
        u32 whole = (s->slots + HZ_UNIT_CHUNK - 1) / HZ_UNIT_CHUNK;
        for (u32 c = 0; c < whole; c++) {
            memcpy(d->chunk[c], s->chunk[c],
                   HZ_UNIT_CHUNK * sizeof *d->chunk[c]);
        }
        d->slots = s->slots;
        dst->nightmare[side] = nm_keep[side];
        nightmare_copy(&dst->nightmare[side], &src->nightmare[side]);
    }
}
