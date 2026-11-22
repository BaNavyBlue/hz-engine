/* (Session 63) The frame pacing's check: src/core/pace.c on its own,
 * against made-up displays (docs/netplay.md, "The pace").
 *
 *     hz_pacecheck [--verbose]
 *
 * Each display is a refresh rate (or none: no vsync) and a swapchain
 * holding Q frames, first in first out, as SDL_GPU's VSYNC: a present
 * returns once the frame Q before it is on screen, and each refresh shows
 * the next frame queued. The game makes a frame in 0.2-2 ms; a present
 * returns up to 0.3 ms late, a sleep up to 0.5 ms. This drives pace.c as
 * platform_sdl3.c's platform_present does, 120 s a display, and checks
 * over the last 100 s:
 *
 *   - LOCKED within 1% of the game's rate (59.94 and 60 Hz, and 59.4 and
 *     60.5), FREE elsewhere;
 *   - the game's rate: the display's when LOCKED, else 59.9227 Hz;
 *   - each frame on screen for whole refreshes as even as the rates
 *     allow: 1 LOCKED, 2 or 3 at 144 Hz, never 0 at 120; below the
 *     game's rate 1, and never two frames in a row not shown;
 *   - a 150 ms frame: caught up FREE in a burst of at most its length
 *     (no frame skipped after), not at all LOCKED; a 1 s one only 0.25 s;
 *   - pace_held (200 ms waited): nothing caught up after it;
 *   - pace_slow (100 ms over 600 frames): the game that much behind,
 *     LOCKED as refreshes shown again.
 *
 * Prints what broke, and PASS or FAIL; exit 1 on FAIL. */

#include "../src/core/pace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIM_SECONDS 120.0
#define SETTLE      20.0        /* the checks from here on */
#define MAX_SHOWN   (1 << 16)

static int  g_broken;
static bool g_verbose;

static void broke(const char *name, const char *what)
{
    if (g_broken < 40) {
        printf("  %s: %s\n", name, what);
    }
    g_broken++;
}

static u32 g_rng = 12345;

static double rnd(void)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return (double)((g_rng >> 8) & 0xFFFFFF) / (double)0x1000000;
}

/* The display: refreshes at phase + k * refresh; a FIFO of q frames. */
typedef struct {
    double refresh;             /* s; 0 none (no vsync) */
    double phase;
    int    q;
    double shown_at[8];         /* the last frames' refreshes, a ring */
    int    presents;
    double last_shown;          /* the refresh the last frame took */
} Display;

typedef struct {
    u32    id;                  /* the game's frame */
    double at;                  /* the refresh it went up at */
} Shown;

typedef struct {
    Display d;
    Pace    pace;
    double  now;
    u32     frame;              /* frames the game has made */
    Shown  *shown;
    int     shown_count;
    /* The scenario's events. */
    double  stall_at, stall_len;
    double  held_at, held_len;
    double  slow_at;
    int     slow_frames;
    double  slow_total;
    bool    stall_done, held_done;
    int     slowing;
    u32     frame_after_stall, frame_after_held;
    double  lag_before_held, lag_after_held;
    double  lag_before_slow, lag_after_slow;
    u32     holds_before_slow, holds_after_slow;
    u32     holds_reported;     /* pace_report clears Pace.holds */
} Sim;

/* A present at s->now: returns when it does, its frame queued. */
static double display_present(Sim *s, u32 id)
{
    Display *d = &s->d;
    if (d->refresh <= 0.0) {
        s->shown[s->shown_count].id = id;
        s->shown[s->shown_count].at = s->now;
        s->shown_count += s->shown_count + 1 < MAX_SHOWN;
        return s->now + 0.0002;
    }
    double from = s->now;
    if (d->presents > 0 && d->last_shown + d->refresh * 0.5 > from) {
        from = d->last_shown + d->refresh * 0.5;
    }
    double k = ceil((from - d->phase) / d->refresh);
    double at = d->phase + k * d->refresh;
    double ret = s->now;
    if (d->presents >= d->q) {
        double before = d->shown_at[(d->presents - d->q) % 8];
        if (before > ret) {
            ret = before;
        }
    }
    d->shown_at[d->presents % 8] = at;
    d->presents++;
    d->last_shown = at;
    s->shown[s->shown_count].id = id;
    s->shown[s->shown_count].at = at;
    s->shown_count += s->shown_count + 1 < MAX_SHOWN;
    return ret + rnd() * 0.0003;
}

/* platform_sdl3.c's platform_present, against the made-up display. */
static void present_frame(Sim *s)
{
    if (!pace_frame(&s->pace, s->now)) {
        return;
    }
    for (;;) {
        double before = s->now;
        s->now = display_present(s, s->frame);
        bool vsync = s->d.refresh > 0.0;
        bool waited = vsync && s->now - before >= 0.001;
        PaceStep step = pace_presented(&s->pace, s->now, vsync, waited);
        if (step == PACE_SLEEP) {
            if (s->pace.until > s->now) {
                s->now = s->pace.until;
            }
            s->now += rnd() * 0.0005;
            break;
        }
        if (step == PACE_NEXT) {
            break;
        }
    }
}

/* How far the game is behind the clock: real time less game time. */
static double lag(const Sim *s)
{
    return s->now - (double)s->frame * s->pace.tick;
}

typedef struct {
    const char *name;
    double      hz;             /* 0: no vsync */
    int         q;
    bool        locked;         /* expected */
    double      stall;          /* s: one frame this long at 60 s */
} Case;

static void run_case(const Case *c)
{
    Sim s;
    memset(&s, 0, sizeof s);
    s.shown = calloc(MAX_SHOWN, sizeof *s.shown);
    if (s.shown == NULL) {
        broke(c->name, "no memory");
        return;
    }
    s.d.refresh = (c->hz > 0.0) ? 1.0 / c->hz : 0.0;
    s.d.phase = rnd() * 0.01;
    s.d.q = c->q;
    s.now = 0.0;
    pace_init(&s.pace, 1.0 / PACE_HZ, s.now);
    s.stall_at = 60.0;
    s.stall_len = c->stall;
    s.held_at = 75.0;
    s.held_len = 0.2;
    s.slow_at = 90.0;
    s.slow_total = 0.1;
    s.slow_frames = 600;

    u32    frame_settle = 0, frame_rate = 0;
    double t_settle = 0.0, t_rate = 0.0;
    bool   settled = false;
    int    run = 0, worst_run = 0;
    double t_stall_end = 0.0;
    bool   lock_wrong = false;
    while (s.now < SIM_SECONDS) {
        double work = 0.0002 + rnd() * 0.0018;
        if (!s.stall_done && s.stall_len > 0.0 && s.now >= s.stall_at) {
            work = s.stall_len;
            s.stall_done = true;
            t_stall_end = s.now + work;
        }
        s.now += work;
        s.frame++;
        u32 shown_before = s.pace.shown;
        present_frame(&s);
        bool skipped = s.pace.shown == shown_before;
        if (!settled && s.now >= SETTLE) {
            settled = true;
            frame_settle = s.frame;
            t_settle = s.now;
        }
        if (frame_rate == 0 && s.now >= 59.0) {
            frame_rate = s.frame;
            t_rate = s.now;
        }
        if (settled && s.pace.locked != c->locked) {
            if (g_verbose && !lock_wrong) {
                printf("    %s: %s at %.3f s (misses %d, estimate %.3f "
                       "ms)\n", c->name, s.pace.locked ? "locked" : "free",
                       s.now, s.pace.misses, s.pace.estimate * 1000.0);
            }
            lock_wrong = true;
        }
        if (settled) {
            run = skipped ? run + 1 : 0;
            bool in_stall = s.stall_done && s.now < t_stall_end + 0.5;
            if (!in_stall && run > worst_run) {
                worst_run = run;
            }
        }
        /* The network's two: a wait for the rival, the balance. */
        if (!s.held_done && s.now >= s.held_at) {
            s.held_done = true;
            s.lag_before_held = lag(&s);
            s.now += s.held_len;
            pace_held(&s.pace, s.held_len);
            s.frame_after_held = s.frame;
        }
        if (s.held_done && s.frame == s.frame_after_held + 120) {
            s.lag_after_held = lag(&s);
        }
        if (s.slowing == 0 && s.now >= s.slow_at) {
            s.slowing = 1;
            s.lag_before_slow = lag(&s);
            s.holds_before_slow = s.holds_reported + s.pace.holds;
        }
        if (s.slowing >= 1 && s.slowing <= s.slow_frames) {
            pace_slow(&s.pace, s.slow_total / s.slow_frames);
            s.slowing++;
        } else if (s.slowing == s.slow_frames + 1) {
            s.slowing++;
        } else if (s.slowing == s.slow_frames + 60) {
            s.lag_after_slow = lag(&s);
            s.holds_after_slow = s.holds_reported + s.pace.holds;
            s.slowing++;
        } else if (s.slowing > s.slow_frames + 1) {
            s.slowing++;
        }
        char line[256];
        u32  holds = s.pace.holds;
        if (pace_report(&s.pace, line, sizeof line)) {
            s.holds_reported += holds;
            if (g_verbose && s.frame % 3000 < PACE_REPORT) {
                printf("    %s %.0f s: %s\n", c->name, s.now, line);
            }
        }
    }

    char what[256];
    if (lock_wrong) {
        snprintf(what, sizeof what, "%s after %.0f s, not always", c->locked
                 ? "LOCKED expected" : "FREE expected", SETTLE);
        broke(c->name, what);
    }
    /* The rate, from settling to the stall (no events in it). */
    double rate = (double)(frame_rate - frame_settle) / (t_rate - t_settle);
    double want = c->locked ? c->hz : PACE_HZ;
    if (fabs(rate - want) / want > 0.0015) {
        snprintf(what, sizeof what, "the game at %.4f Hz, not %.4f", rate,
                 want);
        broke(c->name, what);
    }

    /* On screen: each frame's refreshes, between settling and the stall. */
    int hist[8] = { 0 };
    int never_two = 0;
    if (c->hz > 0.0) {
        u32 prev_id = 0;
        double prev_at = -1.0;
        u32 last_id = 0;
        for (int i = 0; i < s.shown_count; i++) {
            const Shown *sh = &s.shown[i];
            if (sh->id == prev_id) {
                continue;
            }
            if (prev_at >= SETTLE && sh->at < 59.0) {
                int n = (int)floor((sh->at - prev_at) * c->hz + 0.5);
                hist[n < 7 ? n : 7]++;
                if (last_id != 0 && sh->id > last_id + 2) {
                    never_two++;
                }
            }
            last_id = sh->id;
            prev_id = sh->id;
            prev_at = sh->at;
        }
        int lo = (int)floor(c->hz / want);
        int hi = (int)ceil(c->hz / want);
        if (c->locked) {
            lo = 1;
            hi = 1;
        }
        if (lo < 1) {
            lo = 1;
        }
        for (int n = 0; n < 8; n++) {
            if (hist[n] != 0 && (n < lo || n > hi)) {
                snprintf(what, sizeof what, "%d frames on screen for %d "
                         "refreshes (only %d-%d expected)", hist[n], n, lo,
                         hi);
                broke(c->name, what);
            }
        }
        if (never_two != 0) {
            snprintf(what, sizeof what, "%d times two frames in a row not "
                     "shown", never_two);
            broke(c->name, what);
        }
    }
    /* Not shown in a row, outside the stall: at most one below the
     * game's rate, none above it. */
    int run_max = (c->hz > 0.0 && c->hz < PACE_HZ && !c->locked) ? 1 : 0;
    if (worst_run > run_max) {
        snprintf(what, sizeof what, "%d frames in a row not shown (at most "
                 "%d expected)", worst_run, run_max);
        broke(c->name, what);
    }

    /* The stall: FREE caught up to the clock, at most PACE_CATCHUP;
     * LOCKED not at all. Measured at the held event (75 s). */
    if (c->stall > 0.0) {
        double behind = s.lag_before_held;
        double settle_lag = t_settle - (double)frame_settle * s.pace.tick;
        double lost = behind - settle_lag;
        if (c->locked) {
            (void)lost;         /* LOCKED runs at the display's rate */
        } else {
            double want_lost = c->stall > PACE_CATCHUP
                             ? c->stall - PACE_CATCHUP : 0.0;
            if (fabs(lost - want_lost) > 0.035) {
                snprintf(what, sizeof what, "after a %.0f ms frame the game "
                         "%.1f ms behind, not %.1f", c->stall * 1000.0,
                         lost * 1000.0, want_lost * 1000.0);
                broke(c->name, what);
            }
        }
    }
    /* pace_held: FREE, the game behind by just the wait, nothing caught
     * up; LOCKED, the display's own rate either way. */
    if (!c->locked) {
        double grew = s.lag_after_held - s.lag_before_held;
        if (fabs(grew - s.held_len) > 0.035) {
            snprintf(what, sizeof what, "a %.0f ms wait left the game "
                     "%.1f ms behind", s.held_len * 1000.0, grew * 1000.0);
            broke(c->name, what);
        }
    }
    /* pace_slow: the game behind by the slow, LOCKED as holds. */
    {
        double grew = s.lag_after_slow - s.lag_before_slow;
        if (c->locked) {
            u32 holds = s.holds_after_slow - s.holds_before_slow;
            double want_holds = s.slow_total * c->hz;
            if (fabs((double)holds - want_holds) > 1.0) {
                snprintf(what, sizeof what, "%u refreshes held back for "
                         "%.0f ms slowed, not %.1f", (unsigned)holds,
                         s.slow_total * 1000.0, want_holds);
                broke(c->name, what);
            }
        } else if (fabs(grew - s.slow_total) > 0.035) {
            snprintf(what, sizeof what, "%.0f ms slowed left the game "
                     "%.1f ms behind", s.slow_total * 1000.0, grew * 1000.0);
            broke(c->name, what);
        }
    }
    if (g_verbose) {
        printf("  %-10s %s, %.4f Hz; on screen 1:%d 2:%d 3:%d 4:%d; worst "
               "run not shown %d\n", c->name,
               s.pace.locked ? "locked" : "free  ", rate, hist[1], hist[2],
               hist[3], hist[4], worst_run);
    }
    free(s.shown);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else {
            fprintf(stderr, "hz_pacecheck: what is %s?\n", argv[i]);
            return 2;
        }
    }
    static const Case CASES[] = {
        { "50 Hz",    50.0,    2, false, 0.15 },
        { "57 Hz",    57.0,    2, false, 0.15 },
        { "59.4 Hz",  59.4,    2, true,  0.15 },
        { "59.94 Hz", 59.94,   2, true,  0.15 },
        { "60 Hz",    60.0,    2, true,  0.15 },
        { "60 Hz q1", 60.0,    1, true,  1.0 },
        { "60 Hz q3", 60.0,    3, true,  0.15 },
        { "60.5 Hz",  60.5,    2, true,  0.15 },
        { "61 Hz",    61.0,    2, false, 0.15 },
        { "75 Hz",    75.0,    2, false, 0.15 },
        { "100 Hz",   100.0,   2, false, 0.15 },
        { "120 Hz",   120.0,   2, false, 0.15 },
        { "144 Hz",   144.0,   2, false, 1.0 },
        { "144 Hz q1", 144.0,  1, false, 0.15 },
        { "165 Hz",   165.0,   2, false, 0.15 },
        { "240 Hz",   240.0,   3, false, 0.15 },
        { "no vsync", 0.0,     2, false, 0.15 },
        { "no vsync 1s", 0.0,  2, false, 1.0 },
    };
    int n = (int)(sizeof CASES / sizeof CASES[0]);
    for (int i = 0; i < n; i++) {
        run_case(&CASES[i]);
    }
    if (g_broken != 0) {
        printf("pacecheck: FAIL (%d broken)\n", g_broken);
        return 1;
    }
    printf("pacecheck: PASS (%d displays, %.0f s each: locked or free, the "
           "rate, frames on screen, a long frame, a wait, the balance)\n",
           n, SIM_SECONDS);
    return 0;
}
