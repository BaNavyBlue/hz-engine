#include "game.h"
#include "tables.h"

/* VDP_Init ($9A2): the game's screen (VDPInitRegs: the HUD strip over the
 * top 4 rows, and in a split screen view B's side from cell 18; the
 * backdrop colour 0 of line 2; HInt after line 30), everything cleared --
 * the colours, the scroll, all of VRAM by a DMA fill -- and the screen
 * off. */
void screen_start_game(HzGame *g)
{
    video_start_game(g->video, g->split_screen != 0);
    g->screen_on = false;                               /* .window_h_set */
    g->split_done = 1;                                  /* word $101 */
    g->hud_sprite_cut = 1;
    g->split_enable = 0;
}

/* Screen_InitGame ($592E), from Game_LoadMap with the display off: layer A
 * (view A) all tile $5FF of line 1, layer B (view B) clear; in a split
 * screen view B's scroll (the layer $B0 right, 32 down) and the tile in
 * layer B's first 18 cells of rows 0-23; the window layer clear on view
 * B's side, but for the HUD strip's frame on its top 4 rows; in a split
 * screen the divider (on the cartridge six sprites, 0-5, written straight
 * into the table and kept by Sprites_InitFixed: a column of 4 x 4 sprites
 * of tile $500, line 1, in front, at x 144, y 32-223; wide screen parks
 * them off the top); no sprites. */
void screen_init_game(HzGame *g)
{
    Video *v = g->video;
    const Cell frame = cell_from_word(0x25FF);

    video_fill(v, LAYER_A, frame);                      /* .plane_a */
    video_fill(v, LAYER_B, cell_from_word(0));          /* .plane_b */

    /* The window layer's fill runs on from the last cell written: in wide
     * screen from the window's first; in a split screen from plane B's
     * row 23, cell 18 (after the last frame row, where the 68k's VDP
     * address was left), so it lands 1,490 cells early: plane B rows
     * 23-31 from cell 18 on, and the window's rows 0-26 shifted by 18
     * cells (cells 18-35 get the clear ones, the rest $8000). Both are
     * clear, so nothing shows it. */
    LayerId run = LAYER_W;
    int at = 0;
    if (g->split_screen) {
        video_scroll(v, LAYER_B, -0xB0, -0x20);         /* B: +$B0, -32 */
        for (int row = 0; row <= 0x17; row++) {         /* .frame_row */
            for (int col = 0; col < 18; col++) {        /* .frame_cells */
                video_set_tile(v, LAYER_B, col, row, frame);
            }
        }
        run = LAYER_B;
        at = 0x17 * PLANE_COLS + 18;
    }
    for (int row = 0; row <= 0x1F; row++) {             /* .window_row */
        for (int i = 0; i < PLANE_COLS; i++) {
            /* .window_clear: 18 cells clear; .window_prio: 46 with
             * priority */
            u16 w = (i < 18) ? 0 : 0x8000;
            video_set_word(v, run, at % PLANE_COLS, at / PLANE_COLS, w);
            if (++at == PLANE_COLS * PLANE_ROWS && run == LAYER_B) {
                run = LAYER_W;
                at = 0;
            }
        }
    }

    for (int row = 0; row <= 3; row++) {                /* .hud_row */
        const u16 *hud = g->assets->hud_frame[row];     /* HUDFrameTiles */
        for (int i = 0; i < 40; i++) {                  /* .hud_cells */
            video_set_word(v, LAYER_W, i, row, hud[i]);
        }
    }

    video_divider(v, g->split_screen != 0, cell_from_word(0xA500));
    video_sprites_begin(v);
    video_sprites_game(v);
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
    video_set_colours(g->video, 0, g->map_palette, 16);
}

/* Palette_FadeInMap ($943E): CRAM line 0 from black to the map's palette.
 * Eight steps towards each colour + 2 (colour 0: only in the first two),
 * then four towards the colour itself; a step every 4 frames, uploaded
 * after the first of them. The overshoot means the map comes up slightly
 * bright and settles. (Session 80) A frame a step: 12 steps of 4 frames. */
#define FADE_IN_MAP_STEPS     12
#define FADE_IN_MAP_OVERSHOOT 8

void palette_fade_in_map_begin(HzGame *g)
{
    for (int i = 0; i < 16; i++) {                      /* .clear */
        g->map_palette[i] = 0;
    }
    g->fade_in_map_frame = 0;
}

HzRun palette_fade_in_map_step(HzGame *g)
{
    int f = g->fade_in_map_frame++;
    if (f == FADE_IN_MAP_STEPS * 4) {
        return HZ_RUN_DONE;
    }
    if (f % 4 == 1) {
        upload_line0(g);                /* .overshoot_upload, .settle_upload */
    }
    if (f % 4 != 0) {
        return HZ_RUN_FRAME;
    }
    int step = f / 4;               /* 0-7 .overshoot (the 68k's 7 to 0) */
    const u16 *target = g->assets->map[g->map].palette;   /* MapPalettes */
    for (int i = 0; i < 16; i++) {      /* .overshoot_entry, .settle_entry */
        u8 extra = 0;
        if (step < FADE_IN_MAP_OVERSHOOT && !(i == 0 && step >= 2)) {
            extra = 2;
        }
        g->map_palette[i] = step_entry(g->map_palette[i], target[i], extra);
    }
    return HZ_RUN_FRAME;
}

