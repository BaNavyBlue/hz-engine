/* hz_hud: the HUD gauge routines (src/game/hud.c) called directly, their
 * results logged for tools/hzhud_check.py.
 *
 *   hz_hud OUT.txt [--seed N] [--states N] [--flushes N]
 *
 * Three parts, numbers in hex:
 *
 * "Q value max last cmd" then the entry: HUD_QueueBar into an empty queue
 * for every last (cells - 1) 0-$F, every max from 0 to 4 x (last + 1) + 6
 * and $80, $100, $FFFF, every value from 0 to max + 6 (to the same top
 * for the three large maxima) and $FF, $100, $FFFF. The cells start as $DEAD, so the line shows which the routine
 * wrote: "Q value max last cmd | index last cmd c0 ... c15" (index -1:
 * not queued).
 *
 * "U": HUD_UpdateGauges from a random state, weighted to the edges it
 * tests: HP near a multiple of 8 (or past $7F8, where only the low byte
 * of HP / 8 is compared), the destroyed bit, HQ HP near a multiple of 32,
 * ammo near 4, fuel near 64, shown bytes equal to the value, one off or
 * anything, and 0-16 queue entries already taken (with holes). Lines:
 *   U n busy-mask
 *   S p flags hp hq ammo fuel hp_shown hq_shown ammo_shown fuel_shown
 *     (per player, before)
 *   T p ... (the same after)
 *   E index last cmd c0 ... c15 (each entry that wasn't taken before)
 *
 * "F": HUD_FlushBars over a random queue (entries free or a bar of 1-16
 * cells at a random window address, random tiles), VRAM $E000-$FFFF
 * filled with address ^ $5A5A first, auto-increment 2:
 *   F n
 *   B index last cmd c0 ... c15 (every entry, before)
 *   W addr word (every VRAM word in $E000-$FFFF that changed)
 *   L lasts after (16 values) */

#include "video_vdp.h"
#include "../src/game/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 rng_state = 1;

static u32 rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 8) & 0xFFFFFF;
}

static int rng_below(int n)
{
    return (int)(rng_next() % (u32)n);
}

/* A VRAM write command for an address (what the 68k's queue holds). */
static u32 vram_write(u16 addr)
{
    return 0x40000000u | ((u32)(addr & 0x3FFF) << 16) | (u32)(addr >> 14);
}

/* A bar's first cell as the 68k keeps it: its VRAM address in the game's
 * layout (layer B $E000, A $C000, the window $F000). */
static u16 bar_vram(const HzGaugeBar *b)
{
    static const u16 BASE[LAYER_COUNT] = { 0xE000, 0xC000, 0xF000 };
    return (u16)(BASE[b->layer] + b->row * 0x80 + b->col * 2);
}

/* A window cell n (0-$7FF) as a bar's first. */
static void bar_at(HzGaugeBar *b, int n)
{
    b->layer = LAYER_W;
    b->col = (u8)(n % PLANE_COLS);
    b->row = (u8)(n / PLANE_COLS);
}

static void put_bar(FILE *f, const HzGaugeBar *b)
{
    fprintf(f, " %X %X", b->last, vram_write(bar_vram(b)));
    for (int k = 0; k < HZ_GAUGE_CELLS; k++) {
        fprintf(f, " %X", b->cells[k]);
    }
}

static void fill_cells(HzGaugeBar *b, u16 v)
{
    for (int k = 0; k < HZ_GAUGE_CELLS; k++) {
        b->cells[k] = v;
    }
}

static void part_queue(FILE *f, HzGame *g)
{
    static const u16 EXTRA_MAX[] = { 0x80, 0x100, 0xFFFF };
    static const u16 EXTRA_VALUE[] = { 0xFF, 0x100, 0xFFFF };
    for (int last = 0; last <= 0x0F; last++) {
        int top = 4 * (last + 1) + 6;
        for (int mi = 0; mi <= top + 3; mi++) {
            u16 max = (mi <= top) ? (u16)mi : EXTRA_MAX[mi - top - 1];
            int vtop = ((max < top) ? max : top) + 6;
            for (int vi = 0; vi <= vtop + 3; vi++) {
                u16 value = (vi <= vtop) ? (u16)vi : EXTRA_VALUE[vi - vtop - 1];
                memset(g->gauge_queue, 0, sizeof g->gauge_queue);
                fill_cells(&g->gauge_queue[0], 0xDEAD);
                u16 addr = (u16)(0xF000 + 2 * (vi & 0x3F));
                hud_queue_bar(g, value, max, last + 1, LAYER_W, vi & 0x3F, 0);
                fprintf(f, "Q %X %X %X %X |", value, max, last,
                        vram_write(addr));
                int index = -1;
                for (int i = 0; i < HZ_GAUGE_BARS; i++) {
                    if (g->gauge_queue[i].last != 0) {
                        index = i;
                        break;
                    }
                }
                fprintf(f, " %d", index);
                put_bar(f, &g->gauge_queue[0]);
                fputc('\n', f);
            }
        }
    }
}

/* A value near a multiple of `step` (0 to `top` steps), or anything. */
static u16 near_step(int step, int top)
{
    if (rng_below(5) == 0) {
        return (u16)rng_next();
    }
    int v = rng_below(top + 1) * step + rng_below(5) - 2;
    return (u16)(v < 0 ? 0 : v);
}

static u8 shown_for(u16 value)
{
    switch (rng_below(4)) {
    case 0:
        return (u8)value;
    case 1:
        return (u8)(value + 1);
    case 2:
        return (u8)(value - 1);
    default:
        return (u8)rng_next();
    }
}

static void put_state(FILE *f, char tag, const HzGame *g)
{
    for (int p = 0; p < 2; p++) {
        const HzMech *m = &g->mech[p];
        const HzPost *hq = &g->posts[HZ_POST_HQ + p];
        fprintf(f, "%c %d %X %X %X %X %X %X %X %X %X\n", tag, p, m->flags,
                m->hp, post_hq_hp(hq), m->ammo, m->fuel,
                m->hp_shown, m->hq_hp_shown, m->ammo_shown, m->fuel_shown);
    }
}

static void part_update(FILE *f, HzGame *g, int states)
{
    for (int n = 0; n < states; n++) {
        memset(g->gauge_queue, 0, sizeof g->gauge_queue);
        int busy_count = 0;
        switch (rng_below(4)) {
        case 0:
            busy_count = 0;
            break;
        case 1:
            busy_count = rng_below(8);
            break;
        case 2:
            busy_count = 10 + rng_below(7);
            break;
        default:
            busy_count = rng_below(17);
            break;
        }
        unsigned busy = 0;
        for (int k = 0; k < busy_count; k++) {
            busy |= 1u << rng_below(16);
        }
        if (busy_count == 16 && rng_below(2) == 0) {
            busy = 0xFFFF;
        }
        for (int i = 0; i < HZ_GAUGE_BARS; i++) {
            HzGaugeBar *b = &g->gauge_queue[i];
            if (busy & (1u << i)) {
                b->last = (u16)(1 + rng_below(15));
                bar_at(b, 0x1A);
                fill_cells(b, 0xBEEF);
            } else {
                fill_cells(b, 0xDEAD);
            }
        }
        for (int p = 0; p < 2; p++) {
            HzMech *m = &g->mech[p];
            HzPost *hq = &g->posts[HZ_POST_HQ + p];
            m->flags = (u8)((rng_next() & 0xDF) | (rng_below(4) == 0 ? 0x20 : 0));
            m->hp = (rng_below(10) == 0) ? (u16)(0x7F8 + rng_below(0x40))
                                         : near_step(8, 0x30);
            u16 hq_hp = near_step(32, 0x24);
            post_set_hq_hp(hq, hq_hp);
            m->ammo = (u8)near_step(4, 0x3F);
            m->fuel = near_step(64, 0x1A);
            u16 v_hp = (m->flags & 0x20) ? 0 : (u16)(m->hp >> 3);
            m->hp_shown = shown_for(v_hp);
            m->hq_hp_shown = shown_for((u16)(hq_hp >> 5));
            m->ammo_shown = shown_for((u16)(m->ammo >> 2));
            m->fuel_shown = shown_for((u16)(m->fuel >> 6));
        }
        fprintf(f, "U %X %X\n", n, busy);
        put_state(f, 'S', g);
        hud_update_gauges(g);
        put_state(f, 'T', g);
        for (int i = 0; i < HZ_GAUGE_BARS; i++) {
            if (!(busy & (1u << i)) && g->gauge_queue[i].last != 0) {
                fprintf(f, "E %d", i);
                put_bar(f, &g->gauge_queue[i]);
                fputc('\n', f);
            }
        }
    }
}

/* (Half B of docs/hackable.md's step 1) The cartridge's VRAM, laid out
 * from the draw layer's changes by tools/video_vdp.c. */
static VideoVdp g_cart;

static void part_flush(FILE *f, HzGame *g, int flushes)
{
    Vdp *v = g_cart.vdp;
    static u8 before[0x2000];
    for (int n = 0; n < flushes; n++) {
        for (int a = 0xE000; a < 0x10000; a += 2) {
            u16 w = (u16)(a ^ 0x5A5A);
            v->vram[a] = (u8)(w >> 8);
            v->vram[a + 1] = (u8)w;
        }
        memcpy(before, v->vram + 0xE000, sizeof before);
        vdp_reg(v, 0x8F02);
        for (int i = 0; i < HZ_GAUGE_BARS; i++) {
            HzGaugeBar *b = &g->gauge_queue[i];
            b->last = (rng_below(3) == 0) ? 0 : (u16)rng_below(16);
            bar_at(b, rng_below(0x7E0));
            for (int k = 0; k < HZ_GAUGE_CELLS; k++) {
                b->cells[k] = (u16)rng_next();
            }
        }
        fprintf(f, "F %X\n", n);
        for (int i = 0; i < HZ_GAUGE_BARS; i++) {
            fprintf(f, "B %d", i);
            put_bar(f, &g->gauge_queue[i]);
            fputc('\n', f);
        }
        hud_flush_bars(g);
        for (int a = 0; a < 0x2000; a += 2) {
            if (v->vram[0xE000 + a] != before[a]
                || v->vram[0xE000 + a + 1] != before[a + 1]) {
                fprintf(f, "W %X %X\n", 0xE000 + a,
                        (v->vram[0xE000 + a] << 8) | v->vram[0xE000 + a + 1]);
            }
        }
        fputc('L', f);
        for (int i = 0; i < HZ_GAUGE_BARS; i++) {
            fprintf(f, " %X", g->gauge_queue[i].last);
        }
        fputc('\n', f);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: hz_hud OUT.txt [--seed N] [--states N] "
                        "[--flushes N]\n");
        return 2;
    }
    int states = 20000;
    int flushes = 2000;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--seed") == 0) {
            rng_state = (u32)strtoul(argv[i + 1], NULL, 0);
        } else if (strcmp(argv[i], "--states") == 0) {
            states = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--flushes") == 0) {
            flushes = atoi(argv[i + 1]);
        } else {
            fprintf(stderr, "hz_hud: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    FILE *f = fopen(argv[1], "w");
    if (f == NULL) {
        fprintf(stderr, "hz_hud: can't write %s\n", argv[1]);
        return 1;
    }
    static Video video;
    static Vdp vdp;
    static HzGame game;
    video_init(&video);
    vdp_reset(&vdp);
    video_vdp_watch(&g_cart, &video, &vdp);
    memset(&game, 0, sizeof game);
    game.video = &video;
    fprintf(f, "HZH1\n");
    part_queue(f, &game);
    part_update(f, &game, states);
    part_flush(f, &game, flushes);
    return fclose(f) == 0 ? 0 : 1;
}
