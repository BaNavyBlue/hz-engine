/* The game's end, the $53000 module's MenuModule_GameEnd ($53004:
 * Menu_GameEndFlow $5303E, GameEnd_Run $5353C), from Game_Over: WINNER
 * and the winning side scrolled up over black, the second tank photo
 * faded in under it, then the results (time, brains, bases, units,
 * bodies) until a button or a few minutes; with every map won on every
 * difficulty, the endings (ending.c). */

#include <string.h>

#include "front.h"

/* The tiles: the 2 x 2 font at tile $580 (a character c is tiles $580 + (c -
 * $30) x 4: top-left, bottom-left, top-right, bottom-right), the photo at
 * tile $100; the text on layer B (high priority, so over the window), the
 * photo on the window in palette 3. */
#define BIG_FONT_TILE   0x580
#define PHOTO_TILE      0x100
#define PHOTO_CELL      0x6100      /* palette 3, tile $100 */

static void put_cell(HzGame *g, int x, int y, u16 word)
{
    front_put(g, LAYER_B, x, y, word);
}

/* M_PutBigChar ($56C78): c at the cursor on plane B, the cursor 2 cells
 * on. '@' is the font's blank. */
static void put_big_char(HzGame *g, u8 c)
{
    HzFront *f = &g->front;
    u16 tile = (u16)(((c - 0x30) << 2) + f->text_attr + BIG_FONT_TILE);
    int x = f->cursor_x;
    int y = f->cursor_y;
    put_cell(g, x, y, tile);
    put_cell(g, x, y + 1, (u16)(tile + 1));
    put_cell(g, x + 1, y, (u16)(tile + 2));
    put_cell(g, x + 1, y + 1, (u16)(tile + 3));
    f->cursor_x = (u16)(x + 2);
}

/* M_PrintBigInline ($56C2C): text at (x, y). */
static void print_big(HzGame *g, int x, int y, const char *text)
{
    front_cursor(g, x, y);
    for (const char *s = text; *s != '\0'; s++) {
        put_big_char(g, (u8)*s);
    }
}

/* M_PrintBig2Digits ($56D12): n's low byte, capped at 99, two digits. */
static void print_big_2digits(HzGame *g, u32 n)
{
    n &= 0xFF;
    if (n > 99) {
        n = 99;
    }
    put_big_char(g, (u8)('0' + n / 10));
    put_big_char(g, (u8)('0' + n % 10));
}

/* M_PrintBig4Digits ($56D48): n's low word, capped at 9999, four digits,
 * the leading zeros blank (M_NumDigit). */
static void print_big_4digits(HzGame *g, u32 n)
{
    n &= 0xFFFF;
    if (n > 9999) {
        n = 9999;
    }
    static const u32 PLACE[3] = { 1000, 100, 10 };
    bool started = false;
    for (int i = 0; i < 3; i++) {
        u32 digit = n / PLACE[i];
        n %= PLACE[i];
        if (digit != 0) {
            started = true;
        }
        put_big_char(g, started ? (u8)('0' + digit) : MENU_BLANK);
    }
    put_big_char(g, (u8)('0' + n));
}

/* The 68000's divu.w: the 32-bit d0 by a word, the remainder in the high
 * word and the quotient in the low; a quotient past $FFFF overflows and
 * leaves d0 as it was. */
static u32 divu(u32 d0, u16 by)
{
    u32 q = d0 / by;
    if (q > 0xFFFF) {
        return d0;
    }
    return ((d0 % by) << 16) | q;
}

/* Winner_Show ($5429E): the big font in; WinnerPalette in lines 0-2 (the
 * text's colours: 0 red, 1 white, 2 yellow to white); plane B's V scroll
 * -$58, the text below the screen; display on; WINNER 1PLAYER (or
 * 2PLAYER) SIDE in palette 2 on row 1; 121 frames; then plane B scrolled
 * up a line a frame until its V scroll is 1 (89 frames); the second
 * photo drawn on the window, 40 x 28 cells, in palette 3 (still black).
 * QUIRK (not ported: nothing shows it): after unpacking the font, the 68k
 * copies (d0 + 1) x 8 more bytes from past its RLE stream to the VRAM
 * after it, d0 the unpacker's leftover (the last long's low word: 0 for
 * this font), so 8 bytes ($00, $FF x 7) into tile $680, which nothing
 * draws. */
static void winner_show(HzGame *g)
{
    const HzMenuAssets *m = &g->assets->menu;
    HzFront *f = &g->front;
    front_tiles(g, BIG_FONT_TILE, m->winner_font, sizeof m->winner_font);
    memcpy(f->palette, m->winner_palette, sizeof m->winner_palette);
    front_upload_lines(g, 0x07);
    front_set_target(g);
    f->scroll_y_b = -0x58;
    f->scroll_x_b = 0;
    front_apply_scroll(g);
    front_display_on(g);

    front_text_palette(g, 2);
    /* (Session 66) A draw (HZ_DRAW) is DRAW; a network game's own end
     * here YOU WIN or YOU LOSE ('@' a space), centred. */
    const char *own = NULL;
    if (g->winner == HZ_DRAW) {
        own = "DRAW";
    } else if (g->net_player != 0) {
        own = (g->winner == (u16)(g->net_player - 1u)) ? "YOU@WIN"
                                                        : "YOU@LOSE";
    }
    if (own != NULL) {
        print_big(g, 20 - (int)strlen(own), 1, own);
    } else {
        print_big(g, 0x02, 1, "WINNER");
        print_big(g, 0x0F, 1, (g->winner != 0) ? "2PLAYER" : "1PLAYER");
        print_big(g, 0x1E, 1, "SIDE");
    }
    front_wait_frames(g, 0x78);

    do {
        front_frame(g);
        front_apply_scroll(g);
        f->scroll_y_b = (s16)((f->scroll_y_b + 1) & 0xFF);
    } while (f->scroll_y_b != 1 && !g->quit);

    front_tiles(g, PHOTO_TILE, m->tank_photo2, sizeof m->tank_photo2);
    front_picture(g, LAYER_W, 0, 0, PHOTO_CELL, 40, 28);
}

/* Winner_FadeIn ($54428): line 3 from black to WinnerPhotoPalette (the
 * photo's blues), 8 steps of 4 frames. QUIRK: its clear of line 3 writes
 * the line's first long eight times (no step); the line is MenuPalette's,
 * black throughout, either way. */
static void winner_fade_in(HzGame *g)
{
    HzFront *f = &g->front;
    f->palette[48] = 0;
    f->palette[49] = 0;
    memcpy(&f->target[48], g->assets->menu.winner_photo_palette,
           sizeof g->assets->menu.winner_photo_palette);
    for (int step = 0; step < 8 && !g->quit; step++) {
        front_step_to_target(g);
        front_wait_frames(g, 3);
        front_upload_lines(g, 0x08);
    }
}

/* Results_RecordWin ($546FE), its record: with exactly one side the
 * computer and the human the winner, the map's bit on the difficulty set
 * in the win record. CHANGED: the 68k then makes the password and shows
 * it (unless every map is won on every difficulty); the port saves the
 * record instead (saves.h), in the slot this run's record came from or a
 * new one, and shows nothing there. */
static void results_record_win(HzGame *g)
{
    u8 sides = g->options[0] & 3;
    if (sides != 1 && sides != 2) {
        return;
    }
    if (g->winner != sides - 1) {
        return;
    }
    g->win_record[g->options[1] & 3] |= (u8)(1u << (g->options[3] & 7));
    int slot = saves_store(g->save_slot, g->win_record, sides == 1 ? 1 : 2);
    if (slot >= 0) {
        g->save_slot = slot;
    }
}

/* Results_Show ($54470), in the big font on plane B: TIME h:mm:ss from
 * v_frame_counter (99 hours at most), 1P SIDE / 2P SIDE, BRAIN (USER or
 * COM per side), BASE (the outposts held as the game ended), UNIT (units
 * built), BODY (mechs spawned); the win recorded; then until a button or
 * $2A31 frames (3 minutes), or $709 (30 s) with every map won on
 * difficulty A; the music stopped. QUIRK: the seconds come from a divu of
 * the frames by 60, which overflows past 65,535 s (18 hours 12 minutes):
 * the 68k's division is kept (divu). (With difficulty A complete the 68k
 * also unpacks the forest painting here, Ending_UnpackForest, for the
 * endings; the port has it from the assets.) */
static void results_show(HzGame *g)
{
    front_text_palette(g, 0);
    print_big(g, 0x06, 4, "TIME");
    front_text_palette(g, 1);

    u32 frames = g->frame_counter;
    u32 hours = 99;
    if (frames < 0x1464B40) {                           /* 99 hours */
        hours = divu(divu(frames, 0xE10) & 0xFFFF, 0x3C);
    }
    front_cursor(g, 0x12, 4);
    print_big_2digits(g, hours);
    print_big(g, 0x16, 4, ":");
    u32 minutes = divu(divu(frames, 0xE10) & 0xFFFF, 0x3C) >> 16;
    front_cursor(g, 0x18, 4);
    print_big_2digits(g, minutes);
    print_big(g, 0x1C, 4, ":");
    u32 seconds = divu(divu(frames, 0x3C) & 0xFFFF, 0x3C) >> 16;
    front_cursor(g, 0x1E, 4);
    print_big_2digits(g, seconds);

    front_text_palette(g, 2);
    print_big(g, 0x02, 8, "1P");
    print_big(g, 0x07, 8, "SIDE");
    print_big(g, 0x19, 8, "2P");
    print_big(g, 0x1E, 8, "SIDE");

    front_text_palette(g, 0);
    print_big(g, 0x0F, 0x0B, "BRAIN");
    front_text_palette(g, 1);
    print_big(g, 0x04, 0x0B, (g->options[0] & 2) ? "@COM" : "USER");
    print_big(g, 0x1C, 0x0B, (g->options[0] & 1) ? "@COM" : "USER");

    static const char *const LABEL[3] = { "BASE", "UNIT", "BODY" };
    u32 counts[3][2] = {
        { g->results_bases[0], g->results_bases[1] },
        { g->units_built[0], g->units_built[1] },
        { g->mech_spawns[0], g->mech_spawns[1] },
    };
    for (int i = 0; i < 3; i++) {
        int y = 0x0E + i * 3;
        front_text_palette(g, 0);
        print_big(g, 0x10, y, LABEL[i]);
        front_text_palette(g, 1);
        front_cursor(g, 0x04, y);
        print_big_4digits(g, counts[i][0]);
        front_cursor(g, 0x1C, y);
        print_big_4digits(g, counts[i][1]);
    }

    results_record_win(g);

    int wait = (g->win_record[0] == 0xFF) ? 0x708 : 0x2A30;
    for (int i = 0; i <= wait && !g->quit; i++) {
        if (front_wait_pad(g) & 0xFF) {
            break;
        }
    }
    snd_fade_music(g, 0);
}

/* MenuModule_GameEnd: M_Init (the menus' VDP set-up); the planes cleared,
 * the menu font and MenuPalette in; WINNER; the song -- $D with two
 * users, $E with two CPUs, against the computer $B if the human won and
 * $C if it lost, played once; the photo faded in, 60 frames, the
 * results; a fade to black, display off. Every map won on every
 * difficulty: the endings (ending_run). (Session 66) A network game's
 * own song here, as against the computer: $B for the winner's machine,
 * $C for the loser's; a draw $C. */
void menu_game_end(HzGame *g)
{
    HzFront *f = &g->front;
    const HzMenuAssets *m = &g->assets->menu;
    front_init(g);
    front_display_off(g);
    front_reset_scroll(g);
    front_clear_sprites(g);
    front_clear_planes(g);
    front_tiles(g, 0x000, m->menu_font, sizeof m->menu_font);
    memcpy(f->palette, m->menu_palette, sizeof f->palette);
    front_set_target(g);
    front_upload_palette(g);

    winner_show(g);

    u8 sides = g->options[0] & 3;
    u16 song = 0x0E;
    if (g->winner == HZ_DRAW) {
        song = 0x0C;
    } else if (g->net_player != 0) {
        song = (g->winner == (u16)(g->net_player - 1u)) ? 0x0B : 0x0C;
    } else if (sides == 0) {
        song = 0x0D;
    } else if (sides == 1 || sides == 2) {
        song = (g->winner == sides - 1) ? 0x0B : 0x0C;
    }
    snd_play_music(g, song);
    snd_start_song(g, 0);
    winner_fade_in(g);
    front_wait_frames(g, 0x3B);
    results_show(g);

    front_set_target(g);
    front_fade_to_black(g);
    front_display_off(g);
    if (g->win_record[0] == 0xFF && g->win_record[1] == 0xFF
        && g->win_record[2] == 0xFF && g->win_record[3] == 0xFF
        && !g->quit) {
        ending_run(g);
    }
}
