#include "pace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pace_init(Pace *p, double tick, double now)
{
    memset(p, 0, sizeof *p);
    p->tick = tick;
    p->last = -1.0;
    p->grid = -1.0;
    p->next = now;
}

static int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

/* PS2's pace_estimate: the mean of the ring without its PACE_TRIM
 * longest and shortest, so a load, a missed refresh or a late return
 * can't move it. */
static double estimate(const Pace *p)
{
    double sorted[PACE_WINDOW];
    memcpy(sorted, p->ring, sizeof sorted);
    qsort(sorted, PACE_WINDOW, sizeof sorted[0], compare_doubles);
    double sum = 0.0;
    for (int i = PACE_TRIM; i < PACE_WINDOW - PACE_TRIM; i++) {
        sum += sorted[i];
    }
    return sum / (PACE_WINDOW - 2 * PACE_TRIM);
}

static void unlock(Pace *p, double now)
{
    p->locked = false;
    p->next = now + p->hold;    /* the clock from here, the hold kept */
    p->hold = 0.0;
}

bool pace_frame(Pace *p, double now)
{
    p->frames++;
    if (p->locked) {
        p->locked_frames++;
        p->shown++;
        return true;            /* a frame a refresh, never caught up */
    }
    p->next += p->tick;         /* when the frame after this one is due */
    if (now - p->next > PACE_CATCHUP) {
        p->next = now - PACE_CATCHUP;   /* the rest lost: slower, not a
                                           longer burst */
    }
    if (now >= p->next) {
        return false;           /* due already: on to it */
    }
    p->shown++;
    return true;
}

/* The refresh a waited present returned at, on the grid the estimate
 * draws: each return snapped to its whole number of refreshes from the
 * last, and the grid pulled PACE_GRID_PULL of the way to it, so a return
 * a fraction of a millisecond late doesn't move which refresh the next
 * frame takes (it did at 120 and 240 Hz: frames on screen 1 and 3
 * refreshes in turn, where 2 and 2 were due). */
static void follow_grid(Pace *p, double now)
{
    if (p->estimate <= 0.0 || p->grid < 0.0) {
        p->grid = now;
        return;
    }
    double n = floor((now - p->grid) / p->estimate + 0.5);
    if (n < 1.0) {
        n = 1.0;
    }
    double on = p->grid + n * p->estimate;
    double off = now - on;
    if (fabs(off) > p->estimate * 0.25) {
        p->grid = now;          /* lost: from here */
    } else {
        p->grid = on + off * PACE_GRID_PULL;
    }
}

PaceStep pace_presented(Pace *p, double now, bool vsync, bool waited)
{
    waited = waited && vsync;
    if (waited && p->sample && p->last >= 0.0) {
        p->ring[p->at] = now - p->last;
        p->at = (p->at + 1) % PACE_WINDOW;
        if (p->count < PACE_WINDOW) {
            p->count++;
        }
        if (p->count == PACE_WINDOW) {
            p->estimate = estimate(p);
        }
    }
    p->sample = waited;
    p->last = now;
    p->waits = waited ? p->waits + 1 : 0;
    p->misses = waited ? 0 : p->misses + 1;
    if (waited) {
        follow_grid(p, now);
    }

    double error = (p->estimate > 0.0)
                 ? fabs(p->estimate - p->tick) / p->tick : 1.0;
    if (p->locked && (error > PACE_LOCK_LEAVE || !vsync
                      || p->misses >= PACE_UNLOCK_MISS)) {
        unlock(p, now);
    } else if (!p->locked && error < PACE_LOCK_ENTER
               && p->waits >= PACE_LOCK_WAITS) {
        p->locked = true;
    }

    if (p->locked) {
        if (waited && p->hold >= p->estimate) {
            p->hold -= p->estimate;
            p->again++;
            p->holds++;
            return PACE_AGAIN;  /* the balance: a refresh more */
        }
        p->next = now;
        return PACE_NEXT;
    }
    /* FREE: what goes up now is on screen from the next refresh, so the
     * next frame goes up once it is due by then. */
    double refresh = (vsync && p->estimate > 0.0) ? p->estimate : 0.0;
    double at = waited ? p->grid : now;
    if (at + refresh >= p->next) {
        return PACE_NEXT;
    }
    if (!vsync || (!waited && p->misses > PACE_QUEUE)) {
        p->until = p->next - refresh;   /* nothing waits: the clock does */
        return PACE_SLEEP;
    }
    p->again++;                 /* (a present that didn't wait fills the
                                   swapchain's queue: the next one will) */
    return PACE_AGAIN;
}

void pace_held(Pace *p, double seconds)
{
    if (seconds <= 0.0) {
        return;
    }
    p->held += seconds;
    p->sample = false;
    if (!p->locked) {
        p->next += seconds;
    }
}

void pace_slow(Pace *p, double seconds)
{
    if (seconds <= 0.0) {
        return;
    }
    p->slowed += seconds;
    if (p->locked) {
        p->hold += seconds;
    } else {
        p->next += seconds;
    }
}

void pace_aside(Pace *p, double now)
{
    p->sample = false;
    p->last = now;
}

bool pace_report(Pace *p, char *line, int size)
{
    if (p->frames < PACE_REPORT) {
        return false;
    }
    snprintf(line, (size_t)size,
             "pace: %s, display %.3f Hz; %u frames, %u shown, %u again "
             "(%u held back), %u locked; slowed %.1f ms, held %.1f ms",
             p->locked ? "locked" : "free",
             (p->estimate > 0.0) ? 1.0 / p->estimate : 0.0,
             (unsigned)p->frames, (unsigned)p->shown, (unsigned)p->again,
             (unsigned)p->holds, (unsigned)p->locked_frames,
             p->slowed * 1000.0, p->held * 1000.0);
    p->frames = 0;
    p->shown = 0;
    p->again = 0;
    p->holds = 0;
    p->locked_frames = 0;
    p->slowed = 0.0;
    p->held = 0.0;
    return true;
}
