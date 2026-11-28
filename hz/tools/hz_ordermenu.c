/* hz_ordermenu: cases for the order menu's check (tools/hzordermenu_check.py
 * runs the original module on tools/m68krun.py on the same states and
 * compares).
 *
 *   hz_ordermenu ASSETS SEED CASES IN OUT
 *
 * Each case starts from a real game (a map, mode and difficulty picked by
 * the seed, played for a while with random pads, so units have moved and
 * menus have opened), then pokes the menus' state towards the edges the
 * code tests (each mech: closed, opening, open or closing; rows, types,
 * orders and what was shown; money near the cost; a unit on order or
 * ready; 49-50 units; carrying a unit with gauges to redraw; destroyed;
 * radar mode and step; positions near the rival), and writes it as images
 * of the 68k's RAM and the VDP, with a script of pads for 1-24 frames.
 * Each frame does what Mech_Update does with the menu for P1 then P2 (a
 * mech alive and not closing its menu: OrderMenu_Input while it is open,
 * else C opens it), then OrderMenu_UpdateBoth.
 *
 * IN, per case: frames (byte), then frames x 2 pad bytes, RAM $10000,
 * VRAM $10000, CRAM 64 words, VSRAM 40 words (big-endian), 24 registers.
 * OUT, per case: RAM, VRAM, CRAM, VSRAM, registers after the frames.
 *
 * store() is where the port's state meets the 68k's bytes: everything the
 * module reads or writes, at the original's addresses (the rest of RAM is
 * 0). */

#include "video_vdp.h"
#include "../src/game/game.h"
#include "../src/game/tick.h"
#include "../src/game/romcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SIZE      0x10000
#define A_BLOCKS      0x0000   /* v_map_blocks */
#define A_LAYOUT      0x5000   /* v_unpack_buf */
#define A_SCROLL      0x8004   /* v_scroll_x_a, _x_b, _y_a, _y_b */
#define A_POSTS       0x89B4   /* v_outposts, v_hq */
#define A_CAMERA      0x8A66   /* v_camera1 x, y, v_camera2 x, y */
#define A_MECH1       0x8B94
#define A_UNITS       0x8D94
#define A_GRID        0xB4D4
#define A_RADAR_BUF   0xC4D4   /* v_radar_buf1, then 2 */
#define A_GAUGE_QUEUE 0xD99C
#define A_UNIT_COUNT  0xDBFC
#define A_RADAR_SPR   0xDC52   /* v_radar_sprites1, then 2 */
#define A_SPLIT       0xF848
#define A_SFX_PRI     0xF90A
#define A_SFX_Q       0xF90C
#define A_MAP         0xF926

static void wr16(u8 *r, u32 a, u16 v)
{
    r[a] = (u8)(v >> 8);
    r[a + 1] = (u8)v;
}

static void wr32(u8 *r, u32 a, u32 v)
{
    wr16(r, a, (u16)(v >> 16));
    wr16(r, a + 2, (u16)v);
}

/* A VRAM write command for an address (what the 68k's gauge queue holds). */
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

static void store_mech(const HzMech *m, u8 *r)
{
    const HzOrderMenu *mn = &m->menu;
    r[0x00] = m->flags;
    r[0x01] = (u8)m->carried_slot;
    r[0x02] = m->carried_order;
    r[0x03] = m->dir;
    wr16(r, 0x10, (u16)m->x);
    wr16(r, 0x12, (u16)m->y);
    r[0x28] = m->pad;
    r[0x29] = m->pad_prev;
    wr16(r, 0x2C, m->form);
    r[0x40] = mn->step;
    r[0x41] = mn->radar_step;
    r[0x42] = (u8)m->view;
    r[0x43] = m->view_index;
    wr16(r, 0x44, (u16)mn->scroll_x);
    wr16(r, 0x46, (u16)mn->scroll_y);
    r[0x48] = mn->row;
    r[0x49] = mn->row_shown;
    r[0x4A] = mn->type;
    r[0x4B] = mn->type_shown;
    r[0x4C] = mn->order;
    r[0x4D] = mn->order_shown;
    r[0x4E] = mn->carried_order_shown;
    r[0x4F] = m->prod_state;
    r[0x50] = mn->reorder_state;
    r[0x51] = (u8)mn->unit_count_shown;
    r[0x52] = mn->radar_mode;
    r[0x53] = mn->radar_mode_shown;
    wr16(r, 0x54, m->money);
    wr16(r, 0x56, mn->money_shown);
    wr16(r, 0x58, mn->price);
    wr16(r, 0x5A, mn->fee);
    wr16(r, 0x5C, mn->cost);
    r[0x5E] = m->prod_type;
    r[0x5F] = m->prod_order;
    wr16(r, 0x60, m->prod_timer);
    r[0x68] = m->carried_refill;
    r[0x6D] = mn->closing;
    wr16(r, 0x78, m->radar_own_y);
    wr16(r, 0x7A, m->radar_own_x);
    wr16(r, 0x7C, m->radar_foe_y);
    wr16(r, 0x7E, m->radar_foe_x);
}

static void store_unit(const HzUnit *u, u8 *r)
{
    r[0x00] = u->flags;
    r[0x01] = u->type;
    r[0x02] = u->order;
    wr16(r, 0x0A, u->hp);
    wr16(r, 0x10, (u16)u->x);
    wr16(r, 0x12, (u16)u->y);
    r[0x18] = u->missiles;
    r[0x19] = u->ammo;
    wr16(r, 0x1A, u->fuel);
}

static void store(const HzGame *g, u8 *ram)
{
    memset(ram, 0, RAM_SIZE);
    for (int b = 0; b < HZ_BLOCKS; b++) {
        for (int i = 0; i < 16; i++) {
            wr16(ram, (u32)(A_BLOCKS + b * 32 + i * 2), g->blocks[b][i]);
        }
    }
    for (int row = 0; row < 64; row++) {
        for (int col = 0; col < 64; col++) {
            wr16(ram, (u32)(A_LAYOUT + (row * 64 + col) * 2),
                 g->layout[row][col]);
        }
    }
    wr16(ram, A_SCROLL + 0, (u16)g->scroll_x[0]);
    wr16(ram, A_SCROLL + 2, (u16)g->scroll_x[1]);
    wr16(ram, A_SCROLL + 4, (u16)g->scroll_y[0]);
    wr16(ram, A_SCROLL + 6, (u16)g->scroll_y[1]);
    for (int i = 0; i < HZ_RADAR_SPRITES; i++) {
        u8 *p = ram + A_POSTS + i * 8;
        p[0] = g->posts[i].flags;
        p[1] = g->posts[i].b1;
        p[2] = g->posts[i].hold[0];
        p[3] = g->posts[i].hold[1];
        wr16(p, 4, (u16)g->posts[i].x);
        wr16(p, 6, (u16)g->posts[i].y);
    }
    for (int c = 0; c < 2; c++) {
        wr16(ram, (u32)(A_CAMERA + c * 4), (u16)g->camera[c].x);
        wr16(ram, (u32)(A_CAMERA + c * 4 + 2), (u16)g->camera[c].y);
    }
    for (int p = 0; p < 2; p++) {
        store_mech(&g->mech[p], ram + A_MECH1 + p * 0x100);
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        store_unit(rom_unit_c(g, i), ram + A_UNITS + i * 0x50);
    }
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        ram[A_GRID + i] = rom_grid_id(g->grid[i]);
    }
    memcpy(ram + A_RADAR_BUF, g->radar_buf, sizeof g->radar_buf);
    for (int i = 0; i < HZ_GAUGE_BARS; i++) {
        const HzGaugeBar *b = &g->gauge_queue[i];
        u32 a = (u32)(A_GAUGE_QUEUE + i * 0x26);
        wr16(ram, a, b->last);
        wr32(ram, a + 2, vram_write(bar_vram(b)));
        for (int k = 0; k < HZ_GAUGE_CELLS; k++) {
            wr16(ram, (u32)(a + 6 + k * 2), b->cells[k]);
        }
    }
    ram[A_UNIT_COUNT] = (u8)g->unit_count[0];
    ram[A_UNIT_COUNT + 1] = (u8)g->unit_count[1];
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < HZ_RADAR_SPRITES; i++) {
            const HzSprite *sp = &g->radar_sprites[s][i];
            u32 a = (u32)(A_RADAR_SPR + s * 0x90 + i * 8);
            wr16(ram, a, sp->y);
            wr16(ram, a + 2, sp->size_link);
            wr16(ram, a + 4, sp->attr);
            wr16(ram, a + 6, sp->x);
        }
    }
    wr16(ram, A_SPLIT, g->split_screen);
    ram[A_SFX_PRI] = g->sfx_priority;
    ram[A_SFX_Q] = g->sfx_queued;
    wr16(ram, A_MAP, g->map);
}

static void write_vdp(const Vdp *v, FILE *f)
{
    u8 buf[128];
    fwrite(v->vram, 1, VDP_VRAM_SIZE, f);
    for (int i = 0; i < VDP_CRAM_WORDS; i++) {
        wr16(buf, (u32)(i * 2), v->cram[i]);
    }
    fwrite(buf, 1, VDP_CRAM_WORDS * 2, f);
    for (int i = 0; i < VDP_VSRAM_WORDS; i++) {
        wr16(buf, (u32)(i * 2), v->vsram[i]);
    }
    fwrite(buf, 1, VDP_VSRAM_WORDS * 2, f);
    fwrite(v->reg, 1, VDP_REG_COUNT, f);
}

/* -- making cases ------------------------------------------------------- */

static u32 rng = 1;

static int rnd(int n)
{
    rng = rng * 1103515245u + 12345u;
    return (int)(((rng >> 8) & 0xFFFFFF) % (u32)n);
}

static bool chance(int percent)
{
    return rnd(100) < percent;
}

static void frame_hook(HzGame *g, void *user)
{
    (void)g;
    (void)user;
}

static void random_pad(HzPad *p, int busy)
{
    memset(p, 0, sizeof *p);
    if (!chance(busy)) {
        return;
    }
    switch (rnd(9)) {
    case 0: p->up = true; break;
    case 1: p->down = true; break;
    case 2: p->left = true; break;
    case 3: p->right = true; break;
    case 4: p->a = true; break;
    case 5: p->b = true; break;
    case 6: p->c = true; break;
    case 7: p->a = true; p->right = true; break;
    default: p->up = true; p->left = true; break;
    }
}

static const u8 PAD_BITS[] = {
    HZ_PAD_UP, HZ_PAD_DOWN, HZ_PAD_LEFT, HZ_PAD_RIGHT, HZ_PAD_A, HZ_PAD_B,
    HZ_PAD_C, HZ_PAD_START
};

static u8 random_pad_bits(void)
{
    if (chance(35)) {
        return 0;
    }
    u8 b = PAD_BITS[rnd(7)];
    if (chance(15)) {
        b |= PAD_BITS[rnd(8)];
    }
    return b;
}

static u8 next_valid(u16 mask, u8 from)
{
    if (mask == 0) {
        return from;
    }
    u8 t = (u8)(from & 0x0F);
    while (!(mask & (1u << t))) {
        t = (u8)((t + 1) & 0x0F);
    }
    return t;
}

static u16 pick_u16_near(u16 v)
{
    switch (rnd(6)) {
    case 0: return (u16)(v + rnd(5) - 2);
    case 1: return v;
    case 2: return 0;
    case 3: return (u16)rnd(0x10000);
    case 4: return (u16)rnd(3000);
    default: return (u16)(v + rnd(400) - 200);
    }
}

static void poke_mech(HzGame *g, int p)
{
    HzMech *m = &g->mech[p];
    HzOrderMenu *mn = &m->menu;
    const HzAssets *as = g->assets;
    u16 buyable = as->map[g->map & 7].buyable;

    m->flags &= (u8)~0x68;              /* menu, destroyed, carrying */
    switch (rnd(7)) {
    case 0:
    case 1:
        mn->step = 9;                   /* closed */
        mn->closing = 0;
        break;
    case 2:
        m->flags |= 0x40;               /* opening */
        /* a hidden menu often near the end of its count, where it
         * settles */
        mn->step = (u8)((m->view < 0 && chance(70)) ? 1 + rnd(3)
                                                    : 1 + rnd(9));
        mn->closing = 0;
        break;
    case 3:
    case 4:
    case 5:
        m->flags |= 0x40;               /* open */
        mn->step = 0;
        mn->closing = 0;
        break;
    default:
        m->flags |= 0x40;               /* closing */
        mn->step = (u8)(1 + rnd(0x0F));
        mn->closing = 1;
        break;
    }
    if (chance(5)) {
        m->flags |= 0x20;               /* destroyed */
    }

    if (chance(30)) {                   /* carrying a unit */
        int team = (m->flags & 0x10) ? 1 : 0;
        int slot = rnd(50);
        HzUnit *u = rom_unit(g, team * 50 + slot);
        u->type = (u8)rnd(10);
        const HzUnitStats *st = unit_stats_for(g, u->type);
        u->hp = (u16)(chance(50) ? rnd(st->hp + 20) : st->hp);
        u->fuel = (u16)(chance(50) ? rnd(st->fuel + 100) : st->fuel);
        u->ammo = (u8)(chance(50) ? rnd(st->ammo + 10) : st->ammo);
        u->missiles = (u8)(chance(50) ? rnd(st->missiles + 10) : st->missiles);
        m->flags |= 0x08;
        m->form = 2;
        m->carried_slot = (u32)slot;
        m->carried_order = (u8)rnd(16);
        m->carried_refill = chance(50) ? 0 : (u8)rnd(16);
    } else {
        if (m->form == 2) {
            m->form = 1;
        }
        m->carried_refill = (u8)(chance(80) ? 0 : rnd(16));
    }
    mn->carried_order_shown = chance(60) ? m->carried_order : (u8)rnd(16);

    /* row $FF only with a unit carried (see order_menu_input) */
    mn->row = (u8)((m->flags & 0x08) && chance(8) ? 0xFF : rnd(3));
    int same = (m->view < 0) ? 45 : 70;    /* a hidden menu: settle shows */
    mn->row_shown = chance(same) ? mn->row : (u8)rnd(3);
    if (mn->row_shown > 2 && m->view >= 0) {
        mn->row_shown = 1;      /* a shown menu's cursor is drawn on 0-2 */
    }
    mn->type = (u8)(chance(85) ? next_valid(buyable, (u8)rnd(16)) : rnd(10));
    mn->type_shown = chance(same) ? mn->type : (u8)rnd(10);
    u16 orders = unit_stats_for(g, mn->type)->orders;
    mn->order = (u8)(chance(60) ? next_valid(orders, (u8)rnd(16)) : rnd(16));
    mn->order_shown = chance(same) ? mn->order : (u8)rnd(16);
    u16 price = (m->flags & 0x08) ? 0 : unit_stats_for(g, mn->type)->price;
    mn->price = chance(75) ? price : (u16)rnd(1000);
    mn->fee = chance(75) ? as->orders[mn->order & 0x0F].fee : (u16)rnd(400);
    mn->cost = chance(same) ? (u16)(mn->price + mn->fee) : (u16)rnd(1500);
    m->money = pick_u16_near(mn->cost);
    mn->money_shown = chance(60) ? m->money : (u16)rnd(0x10000);
    mn->reorder_state = (u8)(chance(50) ? 0 : (chance(50) ? 2 : 0xFF));
    switch (rnd(4)) {
    case 0: m->prod_state = 0; break;
    case 1: m->prod_state = 1; break;
    case 2: m->prod_state = 2; break;
    default: m->prod_state = 0xFF; break;
    }
    m->prod_timer = (u16)rnd(0x400);
    mn->radar_mode = (u8)rnd(2);
    mn->radar_mode_shown = chance(70) ? mn->radar_mode : (u8)rnd(2);
    mn->radar_step = (u8)rnd(0x0C);
    if (chance(30)) {
        mn->scroll_x = (s16)rnd(0x800);
        mn->scroll_y = (s16)rnd(0x800);
    }
    m->pad = random_pad_bits();
    m->pad_prev = random_pad_bits();
}

/* (Half B of docs/hackable.md's step 1) The cartridge's VRAM, laid out
 * from the draw layer's changes by tools/video_vdp.c. */
static VideoVdp g_cart;
static Video g_video;

/* One case: a game played a while, then the pokes. */
static bool make_case(HzGame *g, const HzAssets *assets, Vdp *vdp)
{
    static const u8 MODES[] = { 0, 1, 2, 5, 6 };
    video_init(&g_video);
    vdp_reset(vdp);
    video_vdp_watch(&g_cart, &g_video, vdp);
    game_init(g, assets, &g_video);
    g->options[0] = MODES[rnd(5)];
    g->options[1] = (u8)rnd(4);
    g->options[3] = (u8)rnd(8);
    if (!tick_new(g, frame_hook, NULL)) {
        return false;
    }
    tick_start(g, frame_hook, NULL);
    int frames = rnd(240);
    for (int f = 0; f < frames; f++) {
        for (int p = 0; p < 2; p++) {
            random_pad(&g->pad_in[p], 25);
        }
        if (tick_pass(g, true, frame_hook, NULL) != HZ_LOOP_ON) {
            return false;
        }
    }
    for (int p = 0; p < 2; p++) {
        if (chance(85)) {
            poke_mech(g, p);
        }
    }
    if (chance(40)) {                   /* the mechs near each other */
        HzMech *a = &g->mech[rnd(2)];
        HzMech *b = &g->mech[(a == &g->mech[0]) ? 1 : 0];
        b->x = (s16)(a->x + rnd(0x440) - 0x220);
        b->y = (s16)(a->y + rnd(0x440) - 0x220);
        if (chance(40)) {               /* at the radar's edge, 32 x 16 */
            int edge = (chance(50) ? 0x200 : -0x200) + (rnd(3) - 1) * 0x10;
            if (chance(50)) {
                b->x = (s16)(a->x + edge);
            } else {
                b->y = (s16)(a->y + edge);
            }
        }
        b->x = (s16)((b->x < 0) ? 0 : (b->x > 0x7E0 ? 0x7E0 : b->x));
        b->y = (s16)((b->y < 0) ? 0 : (b->y > 0x7E0 ? 0x7E0 : b->y));
    }
    for (int t = 0; t < 2; t++) {
        if (chance(30)) {
            g->unit_count[t] = (u8)(46 + rnd(6));
        }
    }
    for (int i = 0; i < 20 && chance(50); i++) {    /* stray units */
        /* (session 61) a unit's id, slot + 1 (1-50: before, 0-49 gave
         * $40/$C0 too, which no game makes) */
        int cell = rnd(HZ_GRID * HZ_GRID);
        int team = rnd(2);
        g->grid[cell] = ref_unit(team, (u32)rnd(50));
    }
    if (chance(30)) {
        for (int i = 0; i < HZ_RADAR_SPRITES; i++) {
            if (g->posts[i].flags & 0x80) {
                g->posts[i].flags = (u8)((g->posts[i].flags & 0xC0)
                                         | (chance(60) ? 2 + rnd(2) : rnd(2)));
            }
        }
    }
    g->sfx_priority = (u8)(chance(50) ? 0xFF : rnd(0x40));
    g->sfx_queued = (u8)(chance(50) ? 0xFF : rnd(0x20));
    vdp->reg[15] = 2;
    return true;
}

/* One frame of the menus, as Mech_Update and the frame loop drive them. */
static void menu_frame(HzGame *g, const u8 pads[2])
{
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        m->pad_prev = m->pad;
        m->pad = pads[p];
        if ((m->flags & 0x20) || m->menu.closing) {
            continue;
        }
        if (m->flags & 0x40) {
            order_menu_input(g, m);
        } else if ((m->pad ^ m->pad_prev) & m->pad & HZ_PAD_C) {
            m->flags |= 0x40;
        }
    }
    order_menu_update(g);
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: hz_ordermenu ASSETS SEED CASES IN OUT\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    rng = (u32)atoi(argv[2]) * 2654435761u + 7u;
    int cases = atoi(argv[3]);
    FILE *in = fopen(argv[4], "wb");
    FILE *out = fopen(argv[5], "wb");
    if (in == NULL || out == NULL) {
        return 1;
    }
    static Vdp vdp;
    static HzGame game;
    static u8 ram[RAM_SIZE];
    int made = 0;
    while (made < cases) {
        if (!make_case(&game, &assets, &vdp)) {
            continue;
        }
        int frames = chance(25) ? 1 + rnd(3) : 1 + rnd(24);
        u8 pads[24][2];
        for (int f = 0; f < frames; f++) {
            for (int p = 0; p < 2; p++) {
                pads[f][p] = random_pad_bits();
            }
        }
        fputc(frames, in);
        fwrite(pads, 1, (size_t)frames * 2, in);
        store(&game, ram);
        fwrite(ram, 1, RAM_SIZE, in);
        write_vdp(&vdp, in);
        for (int f = 0; f < frames; f++) {
            menu_frame(&game, pads[f]);
        }
        store(&game, ram);
        fwrite(ram, 1, RAM_SIZE, out);
        write_vdp(&vdp, out);
        made++;
    }
    fclose(in);
    fclose(out);
    return 0;
}
