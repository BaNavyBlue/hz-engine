#ifndef HZ_REPLAY_H
#define HZ_REPLAY_H

/* (Session 59) A game recorded as it is played -- how it started and both
 * pads every frame -- so that it can be played again, the same, with no
 * window, and looked at: what each side did, where it won and lost
 * (tools/hz_replay.c). The owner: "It would probably be good for you to
 * Analyze my game play maybe me VS original AI and Nightmare".
 *
 * A game is the same from the same start and the same pads (the network
 * game rests on it: docs/netplay.md). The start is what Game_Start and
 * the frames after it read that a game doesn't set for itself: v_options,
 * the PATCHES, who plays a CPU's side, the own view, v_rng_seed_saved
 * (Game_Start's seed), v_cpu_boost (the last game's, until the first
 * second's tick), the win record and the pads already set. Then both pads
 * at every frame hand-over from Game_Start on (a pause's frames too, and
 * the fade in), and the PATCHES changed on the Esc screen mid-game (a game
 * here takes them at once). A hash of the game without its display
 * (state_hash_game) every REPLAY_HASH_EVERY frames lets the player check
 * that it is still the game that was played: a replay starts from a cold
 * boot, the game recorded from wherever the title left the machine.
 *
 * The file (saves/replays/, a game each) is text: "HZREPLAY 1", a line a
 * start value ("patches HEX" the PATCHES), "pads N" and lines "COUNT P1
 * P2" (hex: COUNT frames with those pads, HZ_PAD_* bytes), "patches_at
 * FRAME HEX" for a change, "hashes N" and a hash a line, then "end FRAME
 * WINNER WHY". */

#include "game.h"

#include <stddef.h>

#define REPLAY_HASH_EVERY 60

typedef struct {
    u32 frame;                  /* the hand-over it came in */
    u32 fixes;
} HzReplayFixes;

typedef struct {
    /* the start */
    char game[17];              /* the game's id (net_game_id), hex */
    int  code;                  /* NET_GAME_CODE when it was played */
    char kind[8];               /* "local" or "network"; (session 67)
                                   "netai" the server's AI's own, `side`
                                   the AI's */
    int  side;                  /* network: ours, 0 P1 1 P2; local -1 */
    char when[24];              /* "2026-10-21 01:30 UTC" */
    char rival[16];             /* network: the rival's name */
    u8   options[4];            /* v_options: mode, difficulty, option2,
                                   map */
    u32  fixes;
    u8   brain[2];              /* HZ_BRAIN_* for a CPU side */
    u8   own_view;
    u32  seed;                  /* v_rng_seed_saved */
    u16  cpu_boost;
    u8   win_record[4];
    u8   pads0[2];              /* the pads set before Game_Start */

    /* the frames */
    u16 *pads;                  /* P1 | P2 << 8 */
    u32  frames, pads_cap;
    u64 *hashes;                /* at frames 0, 60, 120 ... (before the
                                   frame's pads are set) */
    u32  hash_count, hashes_cap;
    HzReplayFixes *fix;
    u32  fix_count, fix_cap;

    /* the end */
    u32  end_frame;
    int  winner;                /* 0 P1, 1 P2, -1 none */
    char why[24];               /* "over", "quit", "rival left" ... */
} HzReplay;

/* A pad as its byte (HZ_PAD_*) and back: the network's byte. */
u8   replay_pad_byte(const HzPad *p);
void replay_pad_set(HzPad *p, u8 b);

/* Just before Game_Start: the start from the game, the frames emptied. */
void replay_begin(HzReplay *r, const HzGame *g, u64 game_id, int code,
                  const char *kind, int side, const char *rival);

/* At a frame hand-over, once the pads are set: the hash (every
 * REPLAY_HASH_EVERY frames) and the pads. False when out of memory (the
 * recording stops). */
bool replay_record(HzReplay *r, const HzGame *g);

/* The PATCHES changed mid-game (at the hand-over replay_record counts
 * next). */
void replay_fixes(HzReplay *r, u32 fixes);

/* The end: the frame, the winner (-1 none) and why. */
void replay_end(HzReplay *r, int winner, const char *why);

/* Written to `path`; false (errno says) when it can't be. */
bool replay_save(const HzReplay *r, const char *path);

/* Read from `path`; false with why in `err` (a line) when it can't be. */
bool replay_load(HzReplay *r, const char *path, char *err, size_t err_len);

void replay_free(HzReplay *r);

/* The file's name for a game: when (UTC), the map, who played, the
 * TYPE: "20261021-013005-VULKAN-NIGHTMARE-A.hzr". */
void replay_file_name(const HzReplay *r, char *out, size_t len);

/* Playing it: after game_init, the hooks and Snd_Init as at a cold boot,
 * the start as recorded (game_title_prepare, v_game_type 0 and the game
 * set up after the title follow, then Game_Start). */
void replay_apply_start(HzGame *g, const HzReplay *r);

/* At hand-over `frame` of the replay: the PATCHES changed there, if any,
 * and the pads; false past the recording. */
bool replay_feed(const HzReplay *r, HzGame *g, u32 frame);

#endif /* HZ_REPLAY_H */
