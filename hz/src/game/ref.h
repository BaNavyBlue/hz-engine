#ifndef HZ_REF_H
#define HZ_REF_H

#include "../core/hz_types.h"

/* (Session 61) What one object names another by: a grid cell's occupant,
 * what a move ran into, what a shot hit, whose a shot is, what a unit or
 * an AI aims at. The 68k packed these into bytes and pointers bound to 50
 * units a side -- a grid id ($40 | slot + 1, + $80 for team 2), a victim
 * code (slot + 1, + $32 for team 2, $64/$65 a mech, $66 an HQ), a bumped
 * code (slot, + $32 for team 2), a record's address. The port keeps the
 * parts: the kind of object, its side, and for a unit its slot in that
 * side's list. Nothing in the game builds or reads the 68k's bytes; the
 * checks' tools translate (tools/rom_refs.h). */
typedef enum {
    REF_NONE = 0,
    REF_MECH,           /* side's mech */
    REF_UNIT,           /* side's unit in `slot` */
    REF_HQ,             /* side's HQ: what a shot at its ring hit */
    REF_ROW,            /* QUIRK, no object: what Unit_CheckObjCollision
                           leaves as the "id" when the cell under a unit's
                           centre is taken (`slot`: the row byte, $00,
                           $40, $80 or $C0; see units.c) */
    REF_RAW             /* no object: a byte a check's state held that
                           names nothing (`slot`: the byte; romcodes.c
                           reads them; the game treats it as no object) */
} HzRefKind;

typedef struct {
    u8  kind;           /* HzRefKind */
    u8  side;           /* 0 or 1 */
    u16 zero;           /* always 0: no padding, so records compare and
                           copy byte for byte */
    u32 slot;           /* REF_UNIT: the slot; REF_ROW: the row byte */
} HzRef;

static inline HzRef ref_none(void)
{
    HzRef r = { REF_NONE, 0, 0, 0 };
    return r;
}

static inline HzRef ref_mech(int side)
{
    HzRef r = { REF_MECH, (u8)side, 0, 0 };
    return r;
}

static inline HzRef ref_unit(int side, u32 slot)
{
    HzRef r = { REF_UNIT, (u8)side, 0, slot };
    return r;
}

static inline HzRef ref_hq(int side)
{
    HzRef r = { REF_HQ, (u8)side, 0, 0 };
    return r;
}

/* The row quirk's side is the row byte's b7, as the 68k's team test read
 * it (a grid id's team bit). */
static inline HzRef ref_row(u8 row)
{
    HzRef r = { REF_ROW, (u8)(row >> 7), 0, row };
    return r;
}

static inline HzRef ref_raw(u32 v)
{
    HzRef r = { REF_RAW, 0, 0, v };
    return r;
}

static inline bool ref_is_none(HzRef r)
{
    return r.kind == REF_NONE;
}

static inline bool ref_is_unit(HzRef r)
{
    return r.kind == REF_UNIT;
}

static inline bool ref_is_mech(HzRef r)
{
    return r.kind == REF_MECH;
}

static inline bool ref_eq(HzRef a, HzRef b)
{
    return a.kind == b.kind && a.side == b.side && a.slot == b.slot;
}

#endif
