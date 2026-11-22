#ifndef HZ_NM_ATLAS_H
#define HZ_NM_ATLAS_H

/* (Session 68) The nightmare AI's terrain atlas (docs/nightmare.md, "The
 * atlas"): for each map and side, where an attack unit put down reaches
 * the enemy HQ and shoots it. nm_atlas.c is made by `make atlas`
 * (tools/hzatlas.py, hz_arena --atlas), which runs the game's own unit AI
 * from every block of plain ground on the map, alone on it, once for each
 * of `salts` random number seeds: a block passes when every one of them
 * shoots the enemy HQ within `frames`. Made again after a change to the
 * terrain or the unit AI. */

#include "../core/hz_types.h"

#define NM_ATLAS_SPAN    63     /* blocks from the enemy HQ: all */
#define NM_ATLAS_UNKNOWN 0      /* not tried: not plain ground */
#define NM_ATLAS_BAD     255    /* a drive from it never shot the HQ */
#define NM_ATLAS_TICK    32     /* frames a step of a block's byte */
/* the patches a drive's answer turns on (game.h's HZ_FIX_UNIT_AIM and,
 * session 71, HZ_FIX_PATHS) */
#define NM_ATLAS_FIXES   0x204u

typedef struct {
    u8  map;                    /* HzGame.map */
    u8  team;                   /* the attack unit's side: 0 P1, 1 P2 */
    u8  type;                   /* the unit's (UNIT_*) */
    u8  salts;                  /* drives from each block */
    u16 frames;                 /* each drive's time */
    u16 fixes;                  /* HzGame.fixes & NM_ATLAS_FIXES */
    u8  x0, y0, w, h;           /* the blocks it covers (the rest
                                   NM_ATLAS_UNKNOWN) */
    /* h rows of w blocks, 2 hex digits a block: the slowest of its drives'
     * frames to the first hit / NM_ATLAS_TICK, rounded up (1 at least,
     * 254 at most), or NM_ATLAS_UNKNOWN or NM_ATLAS_BAD */
    const char *const *rows;
} NmAtlas;

extern const NmAtlas nm_atlas[];
extern const int nm_atlas_count;

#endif /* HZ_NM_ATLAS_H */
