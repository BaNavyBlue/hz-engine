#include "tables.h"

#include "../audio/hz_snd.h"

/* The small tables the ported code reads, as the ROM has them. The PS2
 * port keeps tables like these beside the code as C arrays; here they are
 * in one file, with their labels and addresses, so that `make roundtrip`
 * (tools/hz_roundtrip.c) can check every one against the ROM through
 * HZ_TABLES below. Game data -- unit stats, start units, graphics, maps --
 * is in assets/ instead (assets.c). */

/* VDPInitRegs ($A54): register words for VDP_Init, $80-$92. */
const u16 VDP_INIT_REGS[19] = {
    0x8014, 0x8124, 0x8230, 0x833C, 0x8407, 0x8568, 0x8600, 0x8720, 0x8800,
    0x8900, 0x8A1E, 0x8B00, 0x8C81, 0x8D35, 0x8E00, 0x8F02, 0x9001, 0x9189,
    0x9204
};

/* SineTable ($25AA): per heading 0-$F (0 up, 4 right), the x and y of a
 * step of $20: x = sin, y = -cos. */
const HzPoint SINE_TABLE[16] = {
    { 0, -32 }, { 12, -29 }, { 22, -22 }, { 29, -12 },
    { 32, 0 }, { 29, 12 }, { 22, 22 }, { 12, 29 },
    { 0, 32 }, { -12, 29 }, { -22, 22 }, { -29, 12 },
    { -32, 0 }, { -29, -12 }, { -22, -22 }, { -12, -29 }
};

/* GridNeighbourOffsets ($25EA): the 8 v_grid cells round a cell (64 a
 * row), clockwise from above. */
/* DirSlopeTable ($2210): GetDirection's dy x 16 / dx thresholds. */
const s16 DIR_SLOPE_TABLE[8] = {
    -0x50, -0x17, -0x0A, -0x03, 0x03, 0x0A, 0x17, 0x50
};

const s16 GRID_NEIGHBOUR_OFFSETS[8] = {
    -64, -63, 1, 65, 64, 63, -1, -65
};

/* MechLeadSplit ($3C7E): per 8-way heading (up first, clockwise), where
 * the mech sits on screen while moving that way (split screen). */
const HzPoint MECH_LEAD_SPLIT[8] = {
    { 0x38, 0xA8 }, { 0x18, 0x98 }, { 0x10, 0x70 }, { 0x18, 0x48 },
    { 0x38, 0x38 }, { 0x58, 0x48 }, { 0x60, 0x70 }, { 0x58, 0x98 }
};

/* MechLeadWide ($3C9E): the same, wide screen. */
const HzPoint MECH_LEAD_WIDE[8] = {
    { 0x90, 0xA8 }, { 0x50, 0x98 }, { 0x30, 0x70 }, { 0x50, 0x48 },
    { 0x90, 0x38 }, { 0xD0, 0x48 }, { 0xF0, 0x70 }, { 0xD0, 0x98 }
};

/* JoyToDir ($4710): pad bits (U D L R) to a heading 0-$E, $FF none. */
const u8 JOY_TO_DIR[16] = {
    0xFF, 0x00, 0x08, 0xFF, 0x0C, 0x0E, 0x0A, 0xFF, 0x04, 0x02, 0x06, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};

/* MechJetFrames ($4720): per heading, the jet's frame | flips ($800 h,
 * $1000 v). */
const u16 MECH_JET_FRAMES[16] = {
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x1033, 0x1032, 0x1031, 0x1030,
    0x1831, 0x1832, 0x1833, 0x0834, 0x0833, 0x0832, 0x0831
};

/* MechTransformFrames ($4740): taking off, frame by frame. */
const u16 MECH_TRANSFORM_FRAMES[11] = {
    0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F, 0x003E, 0x003F, 0x003E,
    0x003F, 0x003E
};

/* MechTransformFrames2 ($4756): landing. */
const u16 MECH_TRANSFORM_FRAMES2[5] = {
    0x003E, 0x003D, 0x003C, 0x003B, 0x003A
};

/* SpriteLinksP2Robot ($4A98): link bytes of fixed sprites 5-9. */
const u8 SPRITE_LINKS_P2_ROBOT[5] = {
    0x06, 0x07, 0x08, 0x09, 0x0A
};

/* SpriteLinksP2Jet ($4A9D): the same with P2 a jet. */
const u8 SPRITE_LINKS_P2_JET[5] = {
    0x07, 0x09, 0x06, 0x0A, 0x08
};

/* MechDebrisDirs ($4DC4): SineTable heading of each debris piece. */
const u8 MECH_DEBRIS_DIRS[16] = {
    0x0E, 0x0F, 0x00, 0x02, 0x0C, 0x0D, 0x01, 0x03, 0x0B, 0x09, 0x05, 0x04,
    0x0A, 0x08, 0x07, 0x06
};

/* MissileArrowsSplit ($5226): per missile direction bit, the warning
 * arrow's sprite x and y (split screen). */
const HzPoint MISSILE_ARROWS_SPLIT[16] = {
    { 0x0C0, 0x0A4 }, { 0x0DC, 0x0A4 }, { 0x0FC, 0x0A4 }, { 0x0FC, 0x0D3 },
    { 0x0FC, 0x0F8 }, { 0x0FC, 0x11D }, { 0x0FC, 0x14C }, { 0x0DC, 0x14C },
    { 0x0C0, 0x14C }, { 0x0A4, 0x14C }, { 0x084, 0x14C }, { 0x084, 0x11D },
    { 0x084, 0x0F8 }, { 0x084, 0x0D3 }, { 0x084, 0x0A4 }, { 0x0A4, 0x0A4 }
};

/* MissileArrowsWide ($5266): the same, wide screen. */
const HzPoint MISSILE_ARROWS_WIDE[16] = {
    { 0x118, 0x0A4 }, { 0x156, 0x0A4 }, { 0x1AC, 0x0A4 }, { 0x1AC, 0x0D3 },
    { 0x1AC, 0x0F8 }, { 0x1AC, 0x11D }, { 0x1AC, 0x14C }, { 0x156, 0x14C },
    { 0x118, 0x14C }, { 0x0DA, 0x14C }, { 0x084, 0x14C }, { 0x084, 0x11D },
    { 0x084, 0x0F8 }, { 0x084, 0x0D3 }, { 0x084, 0x0A4 }, { 0x0DA, 0x0A4 }
};

/* SpriteMuxTable ($54F8): per page count (row) and phase 0..$B, which
 * 70-entry window Sprites_Multiplex sends. (Session 81) Kept as the
 * cartridge's data; the game draws every sprite and reads it no more. */
const u8 SPRITE_MUX_TABLE[4][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0 },
    { 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 0, 0, 0 },
    { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 0, 0, 0 }
};
/* GaugeTiles ($8716): HUD_QueueBar's tile for the cell where both a
 * bar's value and its maximum end, by value left (row, 0-3) and maximum
 * left minus value (column). On line 1, cyan quarters are the value,
 * dark red the rest of the maximum, black past it. Only bars whose maximum isn't a multiple
 * of 4 reach a maximum of 1-3 here; the unused entries hold $A5BE or the
 * tile a maximum of 4 would have. */
/* ColourCycleCRAM ($5926): the CRAM write commands Map_AnimTilesUpload
 * sends ColourCycles' colours with: line 0 colours 2 and $B. The colours
 * themselves are assets/palettes/cycles.png. */
const u32 COLOUR_CYCLE_CRAM[2] = { 0xC0040000, 0xC0160000 };

/* UnitTwinGunOffsets ($5F9A8): the GRM's two guns, by heading & 7
 * (opposite headings share a row): the first bullet's x, y from the unit's
 * x, y; the second's from the first's. */
const s16 UNIT_TWIN_GUN_OFFSETS[8][4] = {
    { 4, 8,  8, 0 }, { 4, 6,  8, 4 }, { 5, 5,  6, 6 }, { 6, 4,  4, 8 },
    { 8, 4,  0, 8 }, { 5, 4, -2, 8 }, { 7, 5, -6, 4 }, { 8, 6, -8, 4 },
};

const u16 GAUGE_TILES[4][4] = {
    { 0xA5BE, 0xA5B7, 0xA5B6, 0xA5B5 },
    { 0xA5BA, 0xA5B9, 0xA5B8, 0xA5B1 },
    { 0xA5BC, 0xA5BB, 0xA5B2, 0xA5BE },
    { 0xA5BD, 0xA5B3, 0xA5BE, 0xA5BE }
};

/* Each table above, where it is in the ROM, and the width of its words
 * (1 or 2; an HzPoint is two words): what the round trip packs back. */
/* OrderMenuCursorPos ($77D0): the order menu's cursor (column, row) on
 * rows 0-2: unit type, order, radar mode. */
const u8 ORDER_MENU_CURSOR_POS[3][2] = {
    { 1, 3 }, { 1, 6 }, { 1, 0x0D }
};

/* UnitMenuTiles ($7AD0): each unit type's picture in the order menu, its
 * first tile (3 x 3, down each column as a sprite's): types 0, 2, 4, 5
 * use their own sprite facing up, 6 and 7 their turret's; 1, 3, 8 and 9
 * have pictures of their own at $3A8-$3CB. */
const u16 UNIT_MENU_TILES[10] = {
    0x0240, 0x03B1, 0x029A, 0x03BA, 0x02F4, 0x0321, 0x034E, 0x037B,
    0x03A8, 0x03C3
};

/* SidePanelTiles ($8536): 4 rows of 10 cells, the patterned panels either
 * side of the wide screen's order menu (repeated 6 times down). */
const u16 SIDE_PANEL_TILES[4][10] = {
    { 0x25D6, 0x25D9, 0x25DD, 0x25DD, 0x25DD, 0x25DD, 0x25DD, 0x25DD,
      0x2DD9, 0x2DD6 },
    { 0x25D7, 0x25DA, 0x25DE, 0x25DE, 0x25DE, 0x25DE, 0x25DE, 0x25DE,
      0x2DDA, 0x2DD7 },
    { 0x25D8, 0x25DB, 0x25DF, 0x25DF, 0x25DF, 0x25DF, 0x25DF, 0x25DF,
      0x2DDB, 0x2DD8 },
    { 0x25D7, 0x25DC, 0x25FB, 0x25FB, 0x25FB, 0x25FB, 0x25FB, 0x25FB,
      0x2DDC, 0x2DD7 }
};

/* -- the CPU mech AI's (mech_ai.c) -------------------------------------- */

/* AIPadDirs ($B10A): the pad's direction bits for 8 headings, up
 * first, clockwise. */
const u8 AI_PAD_DIRS[8] = {
    0x01, 0x09, 0x08, 0x0A, 0x02, 0x06, 0x04, 0x05
};

/* AIPadToDir8 ($D082): pad direction bits -> 8-way direction, $FF for
 * none or two opposite ones. (16 more bytes follow in the ROM: 16-way
 * heading -> pad bits; no reader found.) */
const u8 AI_PAD_TO_DIR8[16] = {
    0xFF, 0x00, 0x04, 0xFF, 0x06, 0x07, 0x05, 0xFF,
    0x02, 0x01, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* AIDropDirs ($C428): the 16-way facing a delivered guard gets at each
 * drop point, clockwise from up-left. */
const u8 AI_DROP_DIRS[8] = {
    0x0E, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C
};

/* AILaunchPostsP1 ($C430): [map][outpost], 1 = a launch outpost of P1:
 * kind-7 units are flown out from it (the route, the strategy steps; the
 * other side's move state 13 watches it). */
const u8 AI_LAUNCH_POSTS_P1[8][16] = {
    { 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* AILaunchHeadingsP1 ($C4B0): [map][outpost], the 8-way heading plan $D
 * flies from that launch outpost. */
const u8 AI_LAUNCH_HEADINGS_P1[8][16] = {
    { 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 6, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 5, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 4, 0, 3, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 5, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* AILaunchPostsP2 ($C530): [map][outpost], 1 = a launch outpost of P2:
 * kind-7 units are flown out from it (the route, the strategy steps; the
 * other side's move state 13 watches it). */
const u8 AI_LAUNCH_POSTS_P2[8][16] = {
    { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* AILaunchHeadingsP2 ($C5B0): [map][outpost], the 8-way heading plan $D
 * flies from that launch outpost. */
const u8 AI_LAUNCH_HEADINGS_P2[8][16] = {
    { 0, 0, 0, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 4, 3, 0, 2, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 6, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 6, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 6, 0, 1, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 4, 6, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* AIBuyScriptTable ($C630): [variant 0-3][kind 0-7], the ROM address of
 * the AIBuyScripts entry the CPU buys from. */
const u32 AI_BUY_SCRIPT_TABLE[4][8] = {
    { 0xC6B0, 0xC6BC, 0xC6C2, 0xC6D2, 0xC6E8, 0xC6CC, 0xC6EC, 0xC6F8 },
    { 0xC6B2, 0xC6BC, 0xC6C4, 0xC6DA, 0xC6E8, 0xC6C8, 0xC6F4, 0xC6FE },
    { 0xC6B6, 0xC6BC, 0xC6C8, 0xC6E2, 0xC6E8, 0xC6C4, 0xC6EC, 0xC704 },
    { 0xC6B0, 0xC6BC, 0xC6CC, 0xC6DA, 0xC6E8, 0xC6C2, 0xC6F4, 0xC700 }
};

/* AIBuyScripts ($C6B0): (type, order) byte pairs, $FF $FF ends a script;
 * the table's entries start at different pairs, so scripts overlap. */
const u8 AI_BUY_SCRIPTS[88] = {
    0x05, 0x01, 0x01, 0x05, 0x01, 0x01, 0x00, 0x05, 0x00, 0x01, 0xFF, 0xFF,
    0x08, 0x09, 0x08, 0x03, 0xFF, 0xFF, 0x05, 0x01, 0x01, 0x05, 0x01, 0x02,
    0x00, 0x05, 0x00, 0x02, 0x09, 0x05, 0x09, 0x02, 0xFF, 0xFF, 0x05, 0x01,
    0x02, 0x0A, 0x02, 0x02, 0xFF, 0xFF, 0x01, 0x0A, 0x00, 0x0A, 0x09, 0x0A,
    0xFF, 0xFF, 0x01, 0x02, 0x00, 0x02, 0xFF, 0xFF, 0x04, 0x0F, 0xFF, 0xFF,
    0x01, 0x03, 0x00, 0x03, 0x09, 0x03, 0x08, 0x03, 0x08, 0x09, 0xFF, 0xFF,
    0x00, 0x0A, 0x09, 0x0A, 0xFF, 0xFF, 0x01, 0x0A, 0x08, 0x0A, 0xFF, 0xFF,
    0x03, 0x0A, 0xFF, 0xFF
};

/* AIReturnOffsets ($EA9E): plan $B puts a unit back $80 px on in its
 * 8-way direction: (dx, dy), up first, clockwise. */
const HzPoint AI_RETURN_OFFSETS[8] = {
    { 0, -128 },
    { 127, -128 },
    { 127, 0 },
    { 127, 127 },
    { 0, 127 },
    { -128, 127 },
    { -128, 0 },
    { -128, -128 }
};

/* AIDropOffsetsPost ($F41E): the 8 drop points round an outpost, (dx, dy) from its
 * record, clockwise from up-left. */
const HzPoint AI_DROP_OFFSETS_POST[8] = {
    { -72, -72 },
    { -16, -80 },
    { 56, -72 },
    { 80, -16 },
    { 56, 56 },
    { -16, 80 },
    { -72, 56 },
    { -80, -16 }
};

/* AIDropOffsetsHQ ($F43E): the 8 drop points round an HQ, (dx, dy) from its
 * record, clockwise from up-left. */
const HzPoint AI_DROP_OFFSETS_HQ[8] = {
    { -112, -112 },
    { -16, -112 },
    { 112, -112 },
    { 112, -16 },
    { 112, 112 },
    { -16, 112 },
    { -112, 112 },
    { -112, -16 }
};

/* The bytes AI_StratOwnPost.enemies_near reads for P1 as its "launch
 * post" table ($C330: code in AI_BuyMode_Deliver), [map][outpost]; P2's
 * read lands on AIBuyScriptTable's own bytes (see mech_ai.c). */
const u8 AI_STRAY_LAUNCH_P1[8][16] = {
    { 0, 16, 103, 12, 36, 124, 0, 0, 197, 48, 38, 124, 0, 0, 197, 176 },
    { 62, 57, 0, 255, 249, 38, 233, 79, 212, 199, 214, 199, 62, 40, 0, 248 },
    { 4, 71, 137, 180, 230, 79, 190, 124, 0, 16, 100, 16, 212, 199, 214, 199 },
    { 74, 18, 103, 8, 17, 83, 0, 138, 96, 0, 0, 136, 63, 0, 97, 0 },
    { 250, 190, 100, 6, 17, 124, 0, 1, 0, 186, 48, 31, 176, 60, 0, 2 },
    { 102, 10, 66, 40, 0, 188, 112, 10, 96, 0, 44, 60, 52, 104, 0, 168 },
    { 112, 6, 18, 18, 52, 1, 194, 124, 0, 192, 178, 124, 0, 192, 103, 2 },
    { 82, 0, 196, 124, 0, 3, 22, 16, 232, 11, 134, 60, 0, 2, 198, 124 }
};

/* M_VDPInitRegs ($565A4): the menus' VDP registers 0-$12: planes A and B
 * at $E000, the window at $F000 over the whole screen ($12 = $80: from row
 * 0 down), sprites at $D800, H scroll at $DC00, auto-increment 2, 64 x 32
 * planes, H40. */
const u8 MENU_VDP_INIT_REGS[19] = {
    0x04, 0x24, 0x38, 0x3C, 0x07, 0x6C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x81, 0x37, 0x00, 0x02, 0x01, 0x00, 0x80,
};

const HzTableInfo HZ_TABLES[] = {
    { "VDPInitRegs", 0x000A54, VDP_INIT_REGS, sizeof VDP_INIT_REGS, 2 },
    { "SineTable", 0x0025AA, SINE_TABLE, sizeof SINE_TABLE, 2 },
    { "DirSlopeTable", 0x002210, DIR_SLOPE_TABLE, sizeof DIR_SLOPE_TABLE, 2 },
    { "GridNeighbourOffsets", 0x0025EA, GRID_NEIGHBOUR_OFFSETS,
      sizeof GRID_NEIGHBOUR_OFFSETS, 2 },
    { "MechLeadSplit", 0x003C7E, MECH_LEAD_SPLIT, sizeof MECH_LEAD_SPLIT, 2 },
    { "MechLeadWide", 0x003C9E, MECH_LEAD_WIDE, sizeof MECH_LEAD_WIDE, 2 },
    { "JoyToDir", 0x004710, JOY_TO_DIR, sizeof JOY_TO_DIR, 1 },
    { "MechJetFrames", 0x004720, MECH_JET_FRAMES, sizeof MECH_JET_FRAMES, 2 },
    { "MechTransformFrames", 0x004740, MECH_TRANSFORM_FRAMES,
      sizeof MECH_TRANSFORM_FRAMES, 2 },
    { "MechTransformFrames2", 0x004756, MECH_TRANSFORM_FRAMES2,
      sizeof MECH_TRANSFORM_FRAMES2, 2 },
    { "SpriteLinksP2Robot", 0x004A98, SPRITE_LINKS_P2_ROBOT,
      sizeof SPRITE_LINKS_P2_ROBOT, 1 },
    { "SpriteLinksP2Jet", 0x004A9D, SPRITE_LINKS_P2_JET,
      sizeof SPRITE_LINKS_P2_JET, 1 },
    { "MechDebrisDirs", 0x004DC4, MECH_DEBRIS_DIRS, sizeof MECH_DEBRIS_DIRS,
      1 },
    { "MissileArrowsSplit", 0x005226, MISSILE_ARROWS_SPLIT,
      sizeof MISSILE_ARROWS_SPLIT, 2 },
    { "MissileArrowsWide", 0x005266, MISSILE_ARROWS_WIDE,
      sizeof MISSILE_ARROWS_WIDE, 2 },
    { "SpriteMuxTable", 0x0054F8, SPRITE_MUX_TABLE, sizeof SPRITE_MUX_TABLE,
      1 },
    { "GaugeTiles", 0x008716, GAUGE_TILES, sizeof GAUGE_TILES, 2 },
    { "ColourCycleCRAM", 0x005926, COLOUR_CYCLE_CRAM,
      sizeof COLOUR_CYCLE_CRAM, 4 },
    { "UnitTwinGunOffsets", 0x05F9A8, UNIT_TWIN_GUN_OFFSETS,
      sizeof UNIT_TWIN_GUN_OFFSETS, 2 },
    { "OrderMenuCursorPos", 0x0077D0, ORDER_MENU_CURSOR_POS,
      sizeof ORDER_MENU_CURSOR_POS, 1 },
    { "UnitMenuTiles", 0x007AD0, UNIT_MENU_TILES, sizeof UNIT_MENU_TILES, 2 },
    { "SidePanelTiles", 0x008536, SIDE_PANEL_TILES, sizeof SIDE_PANEL_TILES,
      2 },
    { "AIPadDirs", 0x00B10A, AI_PAD_DIRS, sizeof AI_PAD_DIRS, 1 },
    { "AIPadToDir8", 0x00D082, AI_PAD_TO_DIR8, sizeof AI_PAD_TO_DIR8, 1 },
    { "AIDropDirs", 0x00C428, AI_DROP_DIRS, sizeof AI_DROP_DIRS, 1 },
    { "AILaunchPostsP1", 0x00C430, AI_LAUNCH_POSTS_P1,
      sizeof AI_LAUNCH_POSTS_P1, 1 },
    { "AILaunchHeadingsP1", 0x00C4B0, AI_LAUNCH_HEADINGS_P1,
      sizeof AI_LAUNCH_HEADINGS_P1, 1 },
    { "AILaunchPostsP2", 0x00C530, AI_LAUNCH_POSTS_P2,
      sizeof AI_LAUNCH_POSTS_P2, 1 },
    { "AILaunchHeadingsP2", 0x00C5B0, AI_LAUNCH_HEADINGS_P2,
      sizeof AI_LAUNCH_HEADINGS_P2, 1 },
    { "AIBuyScriptTable", 0x00C630, AI_BUY_SCRIPT_TABLE,
      sizeof AI_BUY_SCRIPT_TABLE, 4 },
    { "AIBuyScripts", 0x00C6B0, AI_BUY_SCRIPTS, sizeof AI_BUY_SCRIPTS, 1 },
    { "AIReturnOffsets", 0x00EA9E, AI_RETURN_OFFSETS,
      sizeof AI_RETURN_OFFSETS, 2 },
    { "AIDropOffsetsPost", 0x00F41E, AI_DROP_OFFSETS_POST,
      sizeof AI_DROP_OFFSETS_POST, 2 },
    { "AIDropOffsetsHQ", 0x00F43E, AI_DROP_OFFSETS_HQ,
      sizeof AI_DROP_OFFSETS_HQ, 2 },
    { "AIStrayLaunchP1", 0x00C330, AI_STRAY_LAUNCH_P1,
      sizeof AI_STRAY_LAUNCH_P1, 1 },
    { "M_VDPInitRegs", 0x0565A4, MENU_VDP_INIT_REGS,
      sizeof MENU_VDP_INIT_REGS, 1 },
    /* The Z80 driver's (src/audio/hz_snd.c), at Z80Driver ($20B00) + their
     * Z80 addresses. */
    { "CarrierOpsInit", (0x020B00u + 0x0737), SND_CARRIER_OPS,
      sizeof SND_CARRIER_OPS, 1 },
    { "CarrierOps", (0x020B00u + 0x0839), SND_CARRIER_OPS,
      sizeof SND_CARRIER_OPS, 1 },
    { "FMFreqTable", (0x020B00u + 0x150F), SND_FM_FREQ_TABLE,
      sizeof SND_FM_FREQ_TABLE, -2 },
    { "FMFineTable", (0x020B00u + 0x1527), SND_FM_FINE_TABLE,
      sizeof SND_FM_FINE_TABLE, 1 },
    { "PSGPeriodTable", (0x020B00u + 0x16A7), SND_PSG_PERIOD_TABLE,
      sizeof SND_PSG_PERIOD_TABLE, -2 },
    { "PSGFineTable", (0x020B00u + 0x16BF), SND_PSG_FINE_TABLE,
      sizeof SND_PSG_FINE_TABLE, 1 },
};
const int HZ_TABLE_COUNT = (int)(sizeof HZ_TABLES / sizeof HZ_TABLES[0]);
