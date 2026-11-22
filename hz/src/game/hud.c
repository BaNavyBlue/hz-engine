#include "game.h"
#include "tables.h"

/* The HUD's bars: HP, HQ, ammo and fuel for each player, in the window
 * plane's HUD strip. Each frame, after the units, hud_update_gauges
 * (HUD_UpdateGauges, $2F26) works out each bar's value and queues the
 * bars whose value changed (hud_queue_bar: HUD_QueueBar, $8684, into
 * v_gauge_queue); after the frame's VBlank wait, with the display off,
 * hud_flush_bars (HUD_FlushBars, $8736) writes the queued bars to VRAM.
 * The order menu queues the carried unit's bars the same way
 * (draw_carried_gauges in order_menu.c).
 *
 * A bar is a row of cells of four quarters each. The value fills quarters
 * from the left (cyan), the rest of the maximum shows empty (dark red),
 * and cells past the maximum are blank (black). */

#define GAUGE_EMPTY  0xA5B0     /* four quarters of the maximum, none of
                                   the value; + 1-3 quarters of it */
#define GAUGE_FULL   0xA5B4     /* four quarters of the value */

#define WINDOW_PLANE 0xF000     /* the HUD strip's plane: 64 cells a row */

/* The bars' sizes: a bar's maximum in quarters, and its cells. HP shows
 * HP / 8 and the HQ its HP / 32, ammo ammo / 4 and fuel fuel / 64. With
 * KIDOU's maxima (HP 256, ammo 96, fuel 1556) and HQs starting at 1024,
 * every bar's value is at most its maximum. */
#define BIG_BAR_QUARTERS    32  /* HP, HQ */
#define BIG_BAR_CELLS       8
#define SMALL_BAR_QUARTERS  24  /* ammo, fuel */
#define SMALL_BAR_CELLS     6

/* Where each player's bars start, in window cells: HQ and ammo on row 1,
 * HP and fuel on row 2; P1's from columns 2 and 12, P2's from 22 and 32. */
typedef struct {
    u8 col, row;
} Cell;

typedef struct {
    Cell hp, hq, ammo, fuel;
} BarCells;

static const BarCells BAR_CELLS[2] = {
    { .hp = { 2, 2 }, .hq = { 2, 1 }, .ammo = { 12, 1 }, .fuel = { 12, 2 } },
    { .hp = { 22, 2 }, .hq = { 22, 1 }, .ammo = { 32, 1 }, .fuel = { 32, 2 } }
};

static u16 window_cell(Cell at)
{
    return (u16)(WINDOW_PLANE + at.row * 0x80 + at.col * 2);
}

/* A bar's value against what it last showed: true, and the shown byte
 * updated, when they differ. QUIRK: they are compared as bytes, so a
 * value of 256 or more would match its low byte (none gets there: see the
 * sizes above), though the bar is queued with the whole value. */
static bool bar_changed(u8 *shown, u16 value)
{
    if ((u8)value == *shown) {
        return false;
    }
    *shown = (u8)value;
    return true;
}

/* HUD_UpdateGauges ($2F26): P1's bars, then P2's (the 68k has the code
 * twice). A destroyed mech's HP bar shows empty. */
void hud_update_gauges(HzGame *g)
{
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        const BarCells *at = &BAR_CELLS[p];

        u16 hp = (m->flags & MECH_DESTROYED) ? 0 : (u16)(m->hp >> 3);
        if (bar_changed(&m->hp_shown, hp)) {
            hud_queue_bar(g, hp, BIG_BAR_QUARTERS, BIG_BAR_CELLS,
                          window_cell(at->hp));
        }

        u16 hq = (u16)(post_hq_hp(&g->posts[HZ_POST_HQ + p]) >> 5);
        if (bar_changed(&m->hq_hp_shown, hq)) {
            hud_queue_bar(g, hq, BIG_BAR_QUARTERS, BIG_BAR_CELLS,
                          window_cell(at->hq));
            if (p == 1) {
                /* QUIRK: P2's HQ bar is queued with bra.w HUD_QueueBar,
                 * not bsr.w, so its rts ends HUD_UpdateGauges: P2's ammo
                 * and fuel bars wait for the next frame. */
                return;
            }
        }

        u16 ammo = (u16)(m->ammo >> 2);
        if (bar_changed(&m->ammo_shown, ammo)) {
            hud_queue_bar(g, ammo, SMALL_BAR_QUARTERS, SMALL_BAR_CELLS,
                          window_cell(at->ammo));
        }

        u16 fuel = (u16)(m->fuel >> 6);
        if (bar_changed(&m->fuel_shown, fuel)) {
            hud_queue_bar(g, fuel, SMALL_BAR_QUARTERS, SMALL_BAR_CELLS,
                          window_cell(at->fuel));
        }
    }
}

/* Of `total` quarters, how many fall in cell k (0-4). */
static int quarters_in_cell(int total, int k)
{
    int left = total - 4 * k;
    if (left < 0) {
        return 0;
    }
    return (left > 4) ? 4 : left;
}

/* A cell's tile: `filled` quarters of the value within `room` quarters
 * of the maximum (filled <= room <= 4). A cell with less than four
 * quarters of room is GAUGE_TILES[filled][room - filled]; with none it is
 * GAUGE_TILES[0][0], the blank. */
static u16 cell_tile(int filled, int room)
{
    if (room == 4) {
        return (filled == 4) ? GAUGE_FULL : (u16)(GAUGE_EMPTY + filled);
    }
    return GAUGE_TILES[filled][room - filled];
}

/* The first free queue entry, or NULL with all 16 taken. QUIRK: an entry
 * is free when its `last` (cells - 1) is 0, so a one-cell bar would look
 * free and never be written; no caller queues one. */
static HzGaugeBar *free_bar(HzGame *g)
{
    for (int i = 0; i < HZ_GAUGE_BARS; i++) {
        if (g->gauge_queue[i].last == 0) {
            return &g->gauge_queue[i];
        }
    }
    return NULL;
}

/* HUD_QueueBar ($8684): a bar of `cells` cells (at most 16) showing
 * `value` of `max` quarters (the value capped at the maximum), from VRAM
 * `addr`. QUIRK: with the queue full the bar is dropped, though the
 * caller has already set its shown value, so it waits for the next
 * change. */
void hud_queue_bar(HzGame *g, u16 value, u16 max, int cells, u16 addr)
{
    if (value > max) {
        value = max;
    }
    HzGaugeBar *bar = free_bar(g);
    if (bar == NULL) {
        return;
    }
    bar->last = (u16)(cells - 1);
    bar->addr = addr;
    for (int k = 0; k < cells && k < HZ_GAUGE_CELLS; k++) {
        bar->cells[k] = cell_tile(quarters_in_cell(value, k),
                                  quarters_in_cell(max, k));
    }
}

/* HUD_FlushBars ($8736): every queued bar to VRAM, and its entry freed.
 * (The 68k leaves the VDP's auto-increment at 2 here: whatever sets it to
 * $80 puts 2 back.) */
void hud_flush_bars(HzGame *g)
{
    for (int i = 0; i < HZ_GAUGE_BARS; i++) {
        HzGaugeBar *bar = &g->gauge_queue[i];
        if (bar->last == 0) {
            continue;
        }
        int cells = bar->last + 1;
        bar->last = 0;
        vdp_set_write(g->vdp, VDP_VRAM, bar->addr);
        for (int k = 0; k < cells && k < HZ_GAUGE_CELLS; k++) {
            vdp_write16(g->vdp, bar->cells[k]);
        }
    }
}
