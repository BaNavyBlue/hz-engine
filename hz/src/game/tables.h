#ifndef HZ_TABLES_H
#define HZ_TABLES_H

#include "game.h"

#include <stddef.h>

/* The ROM's small code tables, as C arrays (tables.c): each named for its
 * label, with its address there. */

extern const u16     VDP_INIT_REGS[19];           /* VDPInitRegs */
extern const HzPoint SINE_TABLE[16];              /* SineTable */
extern const s16     GRID_NEIGHBOUR_OFFSETS[8];   /* GridNeighbourOffsets */
extern const s16     DIR_SLOPE_TABLE[8];          /* DirSlopeTable */
extern const HzPoint MECH_LEAD_SPLIT[8];          /* MechLeadSplit */
extern const HzPoint MECH_LEAD_WIDE[8];           /* MechLeadWide */
extern const u8      JOY_TO_DIR[16];              /* JoyToDir */
extern const u16     MECH_JET_FRAMES[16];         /* MechJetFrames */
extern const u16     MECH_TRANSFORM_FRAMES[11];   /* MechTransformFrames */
extern const u16     MECH_TRANSFORM_FRAMES2[5];   /* MechTransformFrames2 */
extern const u8      SPRITE_LINKS_P2_ROBOT[5];    /* SpriteLinksP2Robot */
extern const u8      SPRITE_LINKS_P2_JET[5];      /* SpriteLinksP2Jet */
extern const u8      MECH_DEBRIS_DIRS[16];        /* MechDebrisDirs */
extern const HzPoint MISSILE_ARROWS_SPLIT[16];    /* MissileArrowsSplit */
extern const HzPoint MISSILE_ARROWS_WIDE[16];     /* MissileArrowsWide */
extern const u8      SPRITE_MUX_TABLE[4][16];     /* SpriteMuxTable */
extern const u16     GAUGE_TILES[4][4];           /* GaugeTiles */
extern const u32     COLOUR_CYCLE_CRAM[2];        /* ColourCycleCRAM */
extern const s16     UNIT_TWIN_GUN_OFFSETS[8][4]; /* UnitTwinGunOffsets */
extern const u8      ORDER_MENU_CURSOR_POS[3][2]; /* OrderMenuCursorPos */
extern const u16     UNIT_MENU_TILES[10];         /* UnitMenuTiles */
extern const u16     SIDE_PANEL_TILES[4][10];     /* SidePanelTiles */

extern const u8      AI_PAD_DIRS[8];              /* AIPadDirs */
extern const u8      AI_PAD_TO_DIR8[16];          /* AIPadToDir8 */
extern const u8      AI_DROP_DIRS[8];             /* AIDropDirs */
extern const u8      AI_LAUNCH_POSTS_P1[8][16];   /* AILaunchPostsP1 */
extern const u8      AI_LAUNCH_HEADINGS_P1[8][16];/* AILaunchHeadingsP1 */
extern const u8      AI_LAUNCH_POSTS_P2[8][16];   /* AILaunchPostsP2 */
extern const u8      AI_LAUNCH_HEADINGS_P2[8][16];/* AILaunchHeadingsP2 */
extern const u32     AI_BUY_SCRIPT_TABLE[4][8];   /* AIBuyScriptTable */
extern const u8      AI_BUY_SCRIPTS[88];          /* AIBuyScripts */
#define AI_BUY_SCRIPTS_ADDR 0xC6B0u               /* its ROM address */
extern const HzPoint AI_RETURN_OFFSETS[8];        /* AIReturnOffsets */
extern const HzPoint AI_DROP_OFFSETS_POST[8];     /* AIDropOffsetsPost */
extern const HzPoint AI_DROP_OFFSETS_HQ[8];       /* AIDropOffsetsHQ */
extern const u8      AI_STRAY_LAUNCH_P1[8][16];   /* code bytes at $C330 */

extern const u8      MENU_VDP_INIT_REGS[19];      /* M_VDPInitRegs */

/* For the round trip (tools/hz_roundtrip.c): every table above with its
 * ROM address and word width. */
typedef struct {
    const char *label;
    u32         addr;
    const void *data;
    size_t      bytes;
    int         width;      /* 1: bytes; 2: 16-bit words (HzPoint too);
                               4: 32-bit words; -2: the Z80 driver's
                               little-endian words */
} HzTableInfo;

extern const HzTableInfo HZ_TABLES[];
extern const int         HZ_TABLE_COUNT;

#endif /* HZ_TABLES_H */
