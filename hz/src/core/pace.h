#ifndef HZ_PACE_H
#define HZ_PACE_H

#include "hz_types.h"

/* (Session 63) The frame pacing: the game at the Mega Drive's own rate,
 * whatever the display runs at (docs/netplay.md, "The pace"). Until
 * session 63 a frame waited for the display's refresh (vsync), so the
 * game ran at the display's rate: 2.4 times as fast on a 144 Hz monitor,
 * and a network game at the slower machine's display.
 *
 * As PS2's main loop (src/main.c there): the intervals between presents say
 * what the display runs at (the trimmed mean of the last PACE_WINDOW). A
 * display within PACE_LOCK_ENTER of the game's rate is LOCKED: a frame a
 * refresh, as on the console (a 60 Hz monitor runs the game 0.13% fast,
 * and smoothly). Any other runs FREE on the clock, a frame each 1/PACE_HZ
 * s: each frame shown at the refreshes until the next is due (2 or 3 at
 * 144 Hz), or not at all when the clock is a frame ahead (a sixth of
 * them at 50 Hz). A present that doesn't wait for the display (no vsync,
 * a hidden window) is followed by a sleep to the clock.
 *
 * The game hands frames over from deep inside its loops, so this runs
 * inverted from PS2's: the platform asks pace_frame whether to show a
 * frame the game has made, and after each present pace_presented what
 * next -- the next frame, the same frame at the next refresh, or a sleep
 * first. No clock is read here: the platform passes the times (seconds),
 * so tools/hz_pacecheck.c runs it against made-up displays.
 *
 * The network game also tells it when the game stood still waiting for
 * the rival's pad (pace_held: not owed, so no catch-up after) and when
 * to hold back to keep the two machines even (pace_slow: net_game_slow's
 * balance). */

/* 53,693,175 / 896,040: the master clock over a frame's 3,420 x 262. */
#define PACE_HZ          59.922743
#define PACE_WINDOW      64      /* intervals the display is judged on */
#define PACE_TRIM        8       /* the longest and shortest left out */
#define PACE_LOCK_ENTER  0.01    /* locked within 1% of the game's rate */
#define PACE_LOCK_LEAVE  0.02    /* and left beyond 2% */
#define PACE_LOCK_WAITS  8       /* presents in a row that waited, to lock */
#define PACE_UNLOCK_MISS 30      /* in a row that didn't, to leave it
                                   (vsync on, but nothing waiting) */
#define PACE_QUEUE       3       /* FREE: presents in a row that didn't
                                   wait (the swapchain's queue filling)
                                   before the clock is slept to instead */
#define PACE_GRID_PULL   0.05    /* the refresh grid's pull to a return */
#define PACE_WAITED      0.001   /* s: a present this long waited */
#define PACE_CATCHUP     0.25    /* s the clock may run ahead, at most */
#define PACE_REPORT      300     /* frames between HZ_PACE's lines */

typedef enum {
    PACE_NEXT = 0,              /* run the next frame */
    PACE_AGAIN,                 /* show this one again: the next refresh */
    PACE_SLEEP                  /* sleep until Pace.until, then the next */
} PaceStep;

typedef struct {
    double tick;                /* a frame of the game, s */
    double ring[PACE_WINDOW];   /* the latest intervals between presents */
    int    count;               /* in the ring, up to PACE_WINDOW */
    int    at;
    double estimate;            /* their trimmed mean: the refresh, s; 0
                                   until the ring is full */
    bool   locked;
    int    waits;               /* presents in a row that waited */
    int    misses;              /* and that didn't */
    double last;                /* when the last present returned (-1) */
    double grid;                /* the refresh it returned at, on the
                                   estimate's grid (-1 none yet) */
    bool   sample;              /* the interval from `last` counts */
    double next;                /* FREE: when the next frame is due */
    double until;               /* PACE_SLEEP's end */
    double hold;                /* LOCKED: pace_slow's, gathered */
    /* Since the last report (pace_report). */
    u32    frames;              /* frames handed over */
    u32    shown;               /* shown at least once */
    u32    again;               /* refreshes showing a frame again */
    u32    holds;               /* of them, pace_slow's (LOCKED) */
    u32    locked_frames;
    double slowed;              /* s, pace_slow's */
    double held;                /* s, pace_held's */
} Pace;

/* `tick`: a frame's length, s (1 / PACE_HZ; the checks try others). */
void pace_init(Pace *p, double tick, double now);

/* A frame is made, at `now`: true to show it, false when the clock is a
 * frame ahead already (FREE: run the next at once). */
bool pace_frame(Pace *p, double now);

/* A present of that frame returned at `now`. `vsync`: presents wait for
 * the display's refresh (false: no vsync, or nothing to draw into);
 * `waited`: this one did (it blocked). What next. */
PaceStep pace_presented(Pace *p, double now, bool vsync, bool waited);

/* The game stood still `seconds` (the network game waiting for the
 * rival): not owed. FREE: the clock moved on by as much; LOCKED: nothing
 * (the display went on). The interval it spanned isn't the display's. */
void pace_held(Pace *p, double seconds);

/* Hold the game back `seconds` more (the network game's balance). FREE:
 * the next frame that much later; LOCKED: gathered, and a frame shown
 * again at a refresh once a refresh's worth has. */
void pace_slow(Pace *p, double seconds);

/* A present outside the frames (a notice over the last frame while the
 * network game waits): the interval it ends isn't the display's. */
void pace_aside(Pace *p, double now);

/* Every PACE_REPORT frames, a line (HZ_PACE) of what it did since the
 * last, the counts then cleared: true when one is written. */
bool pace_report(Pace *p, char *line, int size);

#endif /* HZ_PACE_H */
