/* (Session 71) paths.h: the PATHS patch's ground (HZ_FIX_PATHS). */

#ifndef HZ_PATHS_H
#define HZ_PATHS_H

#include "game.h"

/* Unit u on its way to post `post` (g->posts' index): where it drives at
 * next, a place down the ground's shortest way there (a unit's x, y), the
 * farthest of the next steps it can drive at in a straight line. False:
 * no way from where it is, or already there. */
bool paths_next(const HzGame *g, const HzUnit *u, int post, s16 *wx,
                s16 *wy);

/* The ground's cost from unit u's place to post `post` (10 a tile's
 * step), PATHS_NO_WAY when it can't get there. */
#define PATHS_NO_WAY 0xFFFFu
u16 paths_cost(const HzGame *g, const HzUnit *u, int post);

/* Every field the game's units could want made now (Game_Start, with
 * PATHS): each goal (an outpost, an HQ) for each unit type that moves
 * (not the SUPPLY), so none is made in play. 3 a goal (6 where a current
 * makes speed matter). */
void paths_prepare(const HzGame *g);

/* (Session 72) REPAIR: the repair truck's field to each goal made now
 * (Game_Start, with REPAIR). */
void paths_prepare_repair(const HzGame *g);

/* What the current under unit u (unit_push) adds to its velocity in its
 * next move, as unit_move adds it: (*px, *py); 0, 0 none. */
void paths_push(const HzUnit *u, long *px, long *py);

#endif
