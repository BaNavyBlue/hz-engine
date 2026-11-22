/* (Session 61) romcodes.h: the 68k's bytes for the port's references. */

#include "romcodes.h"

#include "game.h"

#define TEAM2 0x80u     /* a grid id's team bit */
#define UNIT  0x40u     /* a grid id's unit bit */

int rom_unit_index(int side, u32 slot)
{
    if (slot >= ROM_SIDE_UNITS) {
        return -1;
    }
    return side * ROM_SIDE_UNITS + (int)slot;
}

HzUnit *rom_unit(HzGame *g, int i)
{
    return unit_at(g, i / ROM_SIDE_UNITS, (u32)(i % ROM_SIDE_UNITS));
}

const HzUnit *rom_unit_c(const HzGame *g, int i)
{
    return unit_at_c(g, i / ROM_SIDE_UNITS, (u32)(i % ROM_SIDE_UNITS));
}

u8 rom_grid_id(HzRef r)
{
    switch (r.kind) {
    case REF_MECH:
        return (u8)((r.side ? TEAM2 : 0) | 0x01);
    case REF_UNIT:
        return (u8)((r.side ? TEAM2 : 0) | UNIT | ((r.slot + 1) & 0x3F));
    case REF_ROW:
    case REF_RAW:
        return (u8)r.slot;
    default:
        return 0;
    }
}

HzRef rom_grid_ref(u8 id)
{
    if (id == 0) {
        return ref_none();
    }
    int side = (id & TEAM2) ? 1 : 0;
    if (!(id & UNIT)) {
        if ((id & 0x7F) != 0x01) {
            return ref_raw(id);             /* no mech's id */
        }
        return ref_mech(side);
    }
    u32 slot = (u32)((id - 1) & 0x3F);
    if (slot >= ROM_SIDE_UNITS) {
        return ref_raw(id);                 /* past a team's records */
    }
    return ref_unit(side, slot);
}

u16 rom_hit_id(HzRef r)
{
    if (r.kind == REF_RAW) {
        return (u16)r.slot;
    }
    return rom_grid_id(r);
}

HzRef rom_hit_ref(u16 id)
{
    if (id > 0xFF) {
        return ref_raw(id);
    }
    return rom_grid_ref((u8)id);
}

u8 rom_victim(HzRef r)
{
    switch (r.kind) {
    case REF_UNIT:
        return (u8)(r.slot + 1 + (r.side ? ROM_SIDE_UNITS : 0));
    case REF_MECH:
        return (u8)(0x64 + r.side);
    case REF_HQ:
        return 0x66;
    case REF_RAW:
    case REF_ROW:
        return (u8)r.slot;
    default:
        return 0;
    }
}

HzRef rom_victim_ref(u8 v)
{
    if (v == 0) {
        return ref_none();
    }
    if (v <= ROM_SIDE_UNITS) {
        return ref_unit(0, (u32)(v - 1));           /* $01-$32 */
    }
    if (v < 0x64) {
        return ref_unit(1, (u32)(v - 1 - ROM_SIDE_UNITS));  /* $33-$63 */
    }
    if (v == 0x64 || v == 0x65) {
        return ref_mech(v - 0x64);
    }
    if (v == 0x66) {
        return ref_hq(0);
    }
    return ref_raw(v);
}

u8 rom_bumped(HzRef r)
{
    switch (r.kind) {
    case REF_UNIT:
        return (u8)(r.slot + (r.side ? ROM_SIDE_UNITS : 0));
    case REF_MECH:
        return (u8)(0x64 + r.side);
    case REF_ROW:
        return 0xFF;            /* $40: slot -1 */
    case REF_RAW:
        return (u8)r.slot;
    default:
        return 0;
    }
}

HzRef rom_bumped_ref(u8 b)
{
    if (b < 2 * ROM_SIDE_UNITS) {
        return ref_unit(b >= ROM_SIDE_UNITS, b % ROM_SIDE_UNITS);
    }
    if (b == 0x64 || b == 0x65) {
        return ref_mech(b - 0x64);
    }
    if (b == 0xFF) {
        return ref_row(0x40);
    }
    return ref_raw(b);
}

u16 rom_addr(HzRef r)
{
    if (r.kind == REF_MECH) {
        return (u16)(ROM_MECH1 + r.side * 0x100u);
    }
    if (r.kind == REF_UNIT) {
        int i = rom_unit_index(r.side, r.slot);
        if (i >= 0) {
            return (u16)(ROM_UNITS + (u32)i * ROM_UNIT_SIZE);
        }
    }
    return 0;
}

HzRef rom_addr_ref(u16 a)
{
    if (a == ROM_MECH1 || a == ROM_MECH1 + 0x100) {
        return ref_mech(a == ROM_MECH1 ? 0 : 1);
    }
    if (a >= ROM_UNITS && a < ROM_UNITS + 2 * ROM_SIDE_UNITS * ROM_UNIT_SIZE
        && (a - ROM_UNITS) % ROM_UNIT_SIZE == 0) {
        u32 i = (a - ROM_UNITS) / ROM_UNIT_SIZE;
        return ref_unit(i >= ROM_SIDE_UNITS, i % ROM_SIDE_UNITS);
    }
    return ref_none();
}
