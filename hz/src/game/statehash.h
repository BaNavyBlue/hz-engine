#ifndef HZ_STATEHASH_H
#define HZ_STATEHASH_H

/* (Session 56) A hash of the game's state, for the network game's check
 * that both machines' games are still the same (net_client.h) and for
 * tools/hz_netcheck.c, the determinism check, whose parts these are
 * (session 52, moved here unchanged).
 *
 * The state as hz_demo's images lay it out (its globals with v_options
 * and v_winner, the mechs, units, posts and shots, big-endian as the 68k
 * keeps them), v_grid, and the VDP's VRAM, CRAM, VSRAM and registers --
 * each part a 64-bit FNV-1a. What the screen shows and the sound commands
 * follow from these; hz_netcheck adds them as parts of its own. */

#include "game.h"

#include <stddef.h>

#define STATE_HASH_PARTS 10

#define STATE_FNV_BASIS 0xCBF29CE484222325ull
#define STATE_FNV_PRIME 0x00000100000001B3ull

u64 state_fnv(u64 h, const void *data, size_t n);

/* Each part's hash: globals, mechs, units, posts, shots, grid, VRAM,
 * CRAM, VSRAM, registers. (Session 59) In an own-view game
 * (HzGame.own_view) the display is left out, as state_hash_game does. */
void state_hash_parts(const HzGame *g, u64 part[STATE_HASH_PARTS]);

/* The parts folded into one, as hz_netcheck folds its: each part's eight
 * bytes, big-endian, into an FNV-1a from the basis. */
u64 state_hash_fold(u64 h, const u64 *part, int parts);

/* The whole: state_hash_fold of state_hash_parts. */
u64 state_hash(const HzGame *g);

/* (Session 59) The game alone, its display left out (the scroll, the
 * effect queued, what is on screen, the HUD's shown values, the VDP; the
 * mechs after the game's end): the same on two machines showing
 * different views of it, and for a replay against the game recorded. */
u64 state_hash_game(const HzGame *g);

#endif /* HZ_STATEHASH_H */
