#ifndef HZ_GAME_TICK_H
#define HZ_GAME_TICK_H

#include "game.h"

#include <stddef.h>

/* (Session 80, docs/hackable.md step 2) The game runs a frame at a time:
 * a host begins what it wants run (game_begin_title, _setup, _start,
 * _pass, _over ...) and calls game_tick, which returns at each frame the
 * game makes (HZ_TICK_FRAME: the host shows it, sets the pads, ticks
 * again) or when what was begun has ended (HZ_TICK_DONE). The game calls
 * no host back. For the hosts that run the game a stage at a time (main.c,
 * the server's AI, the checks), these run a stage to its end with the
 * host's own frame function (`frame`: what the game's frame hook was)
 * for each frame it makes. The host's code, compiled into it. */
typedef void (*HzTickFrame)(HzGame *g, void *user);
/* (Session 81) What the host does where MainLoop_Title tells it
 * something (g->event, an HzEvent). */
typedef void (*HzTickEvent)(HzGame *g, void *user);

static inline void tick_run(HzGame *g, HzTickFrame frame, void *user)
{
    HzTick t;
    while ((t = game_tick(g)) != HZ_TICK_DONE) {
        if (t == HZ_TICK_FRAME && frame != NULL) {
            frame(g, user);
        }
    }
}

/* (Session 81) MainLoop_Title, the game's (game_begin_main, flags
 * HZ_MAIN_*), to its end: a quit, or a map that doesn't load
 * (g->setup_result HZ_SETUP_NO_MAP). */
static inline void tick_main(HzGame *g, unsigned flags, HzTickFrame frame,
                             HzTickEvent event, void *user)
{
    game_begin_main(g, flags);
    HzTick t;
    while ((t = game_tick(g)) != HZ_TICK_DONE) {
        if (t == HZ_TICK_FRAME) {
            if (frame != NULL) {
                frame(g, user);
            }
        } else if (event != NULL) {
            event(g, user);
        }
    }
}

/* MenuModule_Title: the title and the options; 0, or -1 for the demo. */
static inline int tick_title(HzGame *g, HzTickFrame frame, void *user)
{
    game_begin_title(g);
    tick_run(g, frame, user);
    return game_title_result(g);
}

/* MainLoop_Title after the menus, up to Game_Start. */
static inline HzSetup tick_setup(HzGame *g, HzTickFrame frame, void *user)
{
    if (!game_begin_setup(g)) {
        return HZ_SETUP_NO_MAP;
    }
    tick_run(g, frame, user);
    return (HzSetup)g->setup_result;
}

/* The harnesses' MainLoop_Title but the menus: true when a game is ready
 * to start. */
static inline bool tick_new(HzGame *g, HzTickFrame frame, void *user)
{
    if (!game_begin_new(g)) {
        return false;
    }
    tick_run(g, frame, user);
    return g->setup_result == HZ_SETUP_GAME;
}

/* Game_Start */
static inline void tick_start(HzGame *g, HzTickFrame frame, void *user)
{
    game_begin_start(g);
    tick_run(g, frame, user);
}

/* A pass of Game_FrameLoop, and how it ended. */
static inline HzLoopExit tick_pass(HzGame *g, bool read_pads,
                                   HzTickFrame frame, void *user)
{
    game_begin_pass(g, read_pads);
    tick_run(g, frame, user);
    return (HzLoopExit)g->frame_exit;
}

/* Game_Over: the finale, the game's end, the display off. */
static inline void tick_over(HzGame *g, HzTickFrame frame, void *user)
{
    game_begin_over(g);
    tick_run(g, frame, user);
}

/* MenuModule_GameEnd */
static inline void tick_game_end(HzGame *g, HzTickFrame frame, void *user)
{
    game_begin_game_end(g);
    tick_run(g, frame, user);
}

/* The endings alone (--ending) for the side `human` (1 P1, 2 P2). */
static inline void tick_ending(HzGame *g, int human, HzTickFrame frame,
                               void *user)
{
    game_begin_ending(g, human);
    tick_run(g, frame, user);
}

#endif
