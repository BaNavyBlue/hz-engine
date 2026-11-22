#include "game.h"
#include "tables.h"

/* VDP_Init ($9A2). VDPInitRegs sets: HInt on, counter 30 ($8A1E);
 * plane A $C000, window $F000, plane B $E000, sprites $D000, H scroll
 * $D400; backdrop line 2 colour 0; full-screen scrolling; H40; planes
 * 64 x 32; window over the top 4 rows and, for the split screen, cells
 * 18-39 of the rest ($9189). Wide screen moves the H edge off-screen. */
void vdp_init_game(HzGame *g)
{
    Vdp *v = g->vdp;

    /* v_dma_trigger: the RAM routine every DMA ends with (move.w #x,(a6)
     * then rts), because the DMA's last command word must come from RAM.
     * The port's DMA calls need no such thing. */

    for (int i = 0; i <= 0x12; i++) {                   /* .regs */
        vdp_reg(v, VDP_INIT_REGS[i]);
    }
    if (!g->split_screen) {
        vdp_reg(v, 0x9194);
    }
    g->vdp_reg1 = 0x8124;                               /* .window_h_set */

    vdp_command(v, 0x40000010);                         /* VSRAM 0 */
    vdp_write16(v, 0);
    vdp_write16(v, 0);
    vdp_command(v, 0xC0000000);                         /* CRAM 0 */
    for (int i = 0; i <= 0x3F; i++) {                   /* .cram */
        vdp_write16(v, 0);
    }
    vdp_command(v, 0x40000010);
    for (int i = 0; i <= 0x27; i++) {                   /* .vsram */
        vdp_write16(v, 0);
    }

    /* DMA fill: length $FFFF plus the first data write, auto-increment 1:
     * all of VRAM from $0000 with 0. .wait_dma polls until it is done. */
    vdp_reg(v, 0x8134);
    vdp_reg(v, 0x8F01);
    vdp_reg(v, 0x93FF);
    vdp_reg(v, 0x94FF);
    vdp_reg(v, 0x9780);
    vdp_vram_fill(v, 0x0000, 0x10000, 0);

    vdp_reg(v, 0x8124);
    vdp_reg(v, 0x8F02);
    vdp_set_more(v, NULL, 0, 0);        /* (session 61) none past it */
    g->split_done = 1;                                  /* word $101 */
    g->hud_sprite_cut = 1;
    g->split_enable = 0;
}

/* Screen_InitGame ($592E), from Game_LoadMap with the display off. */
void screen_init_game(HzGame *g)
{
    Vdp *v = g->vdp;

    vdp_command(v, 0x40000003);                         /* plane A */
    for (int i = 0; i <= 0x3FF; i++) {                  /* .plane_a */
        vdp_write32(v, 0x25FF25FF);
    }
    vdp_command(v, 0x60000003);                         /* plane B */
    for (int i = 0; i <= 0x3FF; i++) {                  /* .plane_b */
        vdp_write32(v, 0);
    }

    if (g->split_screen) {
        vdp_command(v, 0x40020010);                     /* VSRAM 2 */
        vdp_write16(v, 0xFFE0);                         /* B: -32 */
        vdp_command(v, 0x54020003);                     /* $D402 */
        vdp_write16(v, 0x00B0);                         /* B: +$B0 */

        u32 cmd = 0x60000003;
        for (int row = 0; row <= 0x17; row++) {         /* .frame_row */
            vdp_command(v, cmd);
            cmd += 0x800000;
            for (int i = 0; i <= 8; i++) {              /* .frame_cells */
                vdp_write32(v, 0x25FF25FF);
            }
        }
    }

    /* .window_fill carries on from wherever the VDP address was left:
     * $F000 in wide screen, where the plane B fill ended; but $EBA4 in
     * split screen, after the last frame row. So in split screen it
     * lands 1,490 words early: plane B rows 23-31 from cell 18 on, and
     * window rows 0-26 shifted by 18 cells (cells 18-35 get the zeros,
     * the rest $8000). Both are transparent, so nothing shows it. */
    for (int row = 0; row <= 0x1F; row++) {             /* .window_row */
        for (int i = 0; i <= 8; i++) {                  /* .window_clear */
            vdp_write32(v, 0);
        }
        for (int i = 0; i <= 0x16; i++) {               /* .window_prio */
            vdp_write32(v, 0x80008000);
        }
    }

    u32 cmd = 0x70000003;                               /* window */
    for (int row = 0; row <= 3; row++) {                /* .hud_row */
        const u16 *hud = g->assets->hud_frame[row];     /* HUDFrameTiles */
        vdp_command(v, cmd);
        for (int i = 0; i <= 0x13; i++) {               /* .hud_cells */
            vdp_write32(v, ((u32)hud[i * 2] << 16) | hud[i * 2 + 1]);
        }
        cmd += 0x800000;
    }

    /* Sprites 0-5 straight into the table: the divider between the views,
     * a column of 4 x 4 sprites of tile $500 at x 144 (y 32-223). Wide
     * screen parks them at y 0, off the top. Links 1-6. */
    vdp_command(v, 0x50000003);                         /* $D000 */
    u16 y = 0x00A0;
    u16 size_link = 0x0F01;
    u16 dy = 0x20;
    if (!g->split_screen) {
        y = 0;
        dy = 0;
    }
    for (int i = 0; i <= 5; i++) {                      /* .divider */
        vdp_write16(v, y);
        vdp_write16(v, size_link);
        vdp_write16(v, 0xA500);
        vdp_write16(v, 0x0110);
        y = (u16)(y + dy);
        size_link = (u16)((size_link & 0xFF00) | ((size_link + 1) & 0xFF));
    }
}

/* Sprites_InitFixed ($5A10): the same six divider entries into the RAM
 * copy of sprites 0-9, which VDP_UploadSprites writes every frame. */
void sprites_init_fixed(HzGame *g)
{
    u16 y = 0x00A0;
    u16 size_link = 0x0F01;
    u16 dy = 0x20;
    if (!g->split_screen) {
        y = 0;
        dy = 0;
    }
    for (int i = 0; i <= 5; i++) {                      /* .entry */
        g->sprite_fixed[i].y = y;
        g->sprite_fixed[i].size_link = size_link;
        g->sprite_fixed[i].attr = 0xA500;
        g->sprite_fixed[i].x = 0x0110;
        y = (u16)(y + dy);
        size_link = (u16)((size_link & 0xFF00) | ((size_link + 1) & 0xFF));
    }
}

/* Palette_StepChannel ($956A): a 3-bit channel one step towards the
 * target, never past 7. The target may be above 7 (the overshoot phase
 * adds 2); the channel then stops at 7. */
static u8 step_channel(u8 cur, u8 target)
{
    if (cur == target) {
        return cur;
    }
    if (cur < target) {
        return (cur < 7) ? (u8)(cur + 1) : cur;         /* .at_max */
    }
    return (cur > 0) ? (u8)(cur - 1) : cur;             /* .down */
}

/* One entry of the fade. The 68k rotates the word so each channel sits
 * in the low 3 bits in turn (blue, green, red); only those bits change. */
static u16 step_entry(u16 cur, u16 target, u8 extra)
{
    static const int SHIFT[3] = { 9, 5, 1 };            /* .channels */
    for (int c = 0; c < 3; c++) {
        u8 t = (u8)(((target >> SHIFT[c]) & 7) + extra);
        u8 n = step_channel((u8)((cur >> SHIFT[c]) & 7), t);
        cur = (u16)((cur & ~(7u << SHIFT[c])) | ((u16)n << SHIFT[c]));
    }
    return cur;
}

static void upload_line0(HzGame *g)
{
    vdp_command(g->vdp, 0xC0000000);
    for (int i = 0; i < 16; i++) {
        vdp_write16(g->vdp, g->map_palette[i]);
    }
}

/* Palette_FadeInMap ($943E): CRAM line 0 from black to the map's palette.
 * Eight steps towards each colour + 2 (colour 0: only in the first two),
 * then four towards the colour itself; one step every 4 frames. The
 * overshoot means the map comes up slightly bright and settles. */
void palette_fade_in_map(HzGame *g)
{
    for (int i = 0; i < 16; i++) {                      /* .clear */
        g->map_palette[i] = 0;
    }
    const u16 *target = g->assets->map[g->map].palette;   /* MapPalettes */

    for (int step = 7; step >= 0; step--) {             /* .overshoot */
        for (int i = 0; i < 16; i++) {                  /* .overshoot_entry */
            u8 extra = 2;
            if (step < 6 && i == 0) {
                extra = 0;
            }
            g->map_palette[i] = step_entry(g->map_palette[i], target[i],
                                           extra);
        }
        game_wait_vblank(g);
        upload_line0(g);                                /* .overshoot_upload */
        game_wait_vblank(g);
        game_wait_vblank(g);
        game_wait_vblank(g);
    }

    for (int step = 3; step >= 0; step--) {             /* .settle */
        for (int i = 0; i < 16; i++) {                  /* .settle_entry */
            g->map_palette[i] = step_entry(g->map_palette[i], target[i],
                                           0);
        }
        game_wait_vblank(g);
        upload_line0(g);                                /* .settle_upload */
        game_wait_vblank(g);
        game_wait_vblank(g);
        game_wait_vblank(g);
    }
}
