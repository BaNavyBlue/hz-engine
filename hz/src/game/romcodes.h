#ifndef HZ_ROMCODES_H
#define HZ_ROMCODES_H

/* (Session 61) The 68k's bytes for the port's references (ref.h): how the
 * original packed a grid cell's occupant, what a shot hit, what a move ran
 * into and an object's address, for 50 units a side. Only the checks'
 * tools (to load the 68k's RAM into an HzGame and write it back) and the
 * netplay hash (statehash.c, whose images are the 68k's) use these; the
 * game itself never builds or reads such a byte.
 *
 * Every decoder takes any byte: what names an object becomes that object,
 * the rest REF_RAW, so a state written back gives the bytes it was given.
 * Units past slot 49 (only with UNITS) have no 68k byte; the encoders give
 * the slot's low bits for them, and the netplay hash adds their whole
 * references (statehash.c). */

#include "game.h"

#define ROM_SIDE_UNITS 50       /* a team's records on the 68k */
#define ROM_MECH1      0x8B94u  /* v_mech1 (and v_mech2 $100 after) */
#define ROM_UNITS      0x8D94u  /* v_units1 (v_units2 50 records after) */
#define ROM_UNIT_SIZE  0x50u

/* A grid id (v_grid, v_hit_id): 0 nobody; b6 clear a mech ($01 P1, $81
 * P2), set a unit with slot + 1 in the low 6 bits; b7 team 2. REF_ROW is
 * its byte; REF_RAW its value. */
u8    rom_grid_id(HzRef r);
HzRef rom_grid_ref(u8 id);
/* v_hit_id, a word: as the grid id, but any word comes back. */
u16   rom_hit_id(HzRef r);
HzRef rom_hit_ref(u16 id);

/* A victim code (obj_hit_victim): a unit slot + 1, + $32 for team 2; $64
 * P1's mech, $65 P2's; $66 an HQ. QUIRK: $64 is also team 2's slot 49
 * (49 + 1 + $32): read back as P1's mech, which every reader of a victim
 * treats as it treats that unit. 0 is nothing. */
u8    rom_victim(HzRef r);
HzRef rom_victim_ref(u8 v);

/* A bumped code (obj_bumped): a unit slot, + $32 for team 2; $64/$65 the
 * mechs; REF_ROW $40 (the collision's row quirk) is $FF. */
u8    rom_bumped(HzRef r);
HzRef rom_bumped_ref(u8 b);

/* An object's 68k address, low word ($8B94 / $8C94 the mechs, $8D94 + $50
 * x (team x 50 + slot) the units); 0 for none or anything else. */
u16   rom_addr(HzRef r);
HzRef rom_addr_ref(u16 a);

/* A unit's record number on the 68k (team x 50 + slot), -1 past slot 49;
 * and the record for one (0-99: each side's first 50 slots). */
int   rom_unit_index(int side, u32 slot);
HzUnit       *rom_unit(HzGame *g, int i);
const HzUnit *rom_unit_c(const HzGame *g, int i);

#endif
