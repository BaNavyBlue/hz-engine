/* The options screen ($548BC-$563F5): CONFIGURATION MODE.
 *
 * The menu: START (the map select; with one side the computer, then the
 * difficulty), CONT (the saved games: see below), PLAYER (each side User
 * or Computer, and with one computer the window, split or wide), MUSIC and
 * SOUND (the tests). Up and down move; A, B or C choose on the menu
 * itself, elsewhere A or C choose and B goes back. What it sets is
 * v_options (the mode byte, the difficulty, the option byte the CPU's
 * style comes from, the map) and the win record; the panel at the bottom
 * always shows the sides and the window.
 *
 * CHANGED: on the cartridge CONT asks for an 11-letter password (the win
 * record, which has no battery behind it, as the game's end showed it).
 * The port has no password: CONT lists the saved games (saves.h) and
 * loads one, which sets what an accepted password set; the record is
 * saved where the game's end made the password (game_end.c).
 *
 * On the 68k each page is a stretch of code that branches to the next,
 * back and forth; here a page is a function returning the page that
 * follows. Each page's boxes are drawn over what is there (a page going
 * back to an earlier one redraws only that page's box, so parts of a
 * later, bigger box can stay on screen: QUIRK, kept). */

#include "front.h"

#include <stdio.h>

typedef HzFrontPage Page;

#define CHOOSE (HZ_PAD_A | HZ_PAD_C)

/* OptionsMenuItems ($54D74) */
static const char *const MENU_ITEMS[5] = {
    "START ", "CONT  ", "PLAYER", "MUSIC ", "SOUND ",
};

/* TypeNames ($563D6) */
static const char *const TYPE_NAMES[4] = {
    "TYPE A", "TYPE B", "TYPE C", "TYPE D",
};

/* v_option2 by difficulty (TypeSelect_Run .choose): the CPU mech's style
 * comes from its top two bits (mech_ai.c). */
static const u8 OPTION2_BY_TYPE[4] = { 0xFF, 0xAA, 0x55, 0x00 };

static bool one_cpu(const HzGame *g)
{
    u8 sides = g->options[0] & 3;
    return sides == 1 || sides == 2;
}

/* Map `map` won on difficulty `type` (v_win_record's byte per difficulty,
 * bit per map). */
static bool won(const HzGame *g, int type, int map)
{
    return (g->win_record[type] >> (map & 7)) & 1;
}

/* Won on all four: the map select skips it and greys its name. */
static bool map_complete(const HzGame *g, int map)
{
    for (int t = 0; t < 4; t++) {
        if (!won(g, t, map)) {
            return false;
        }
    }
    return true;
}

/* (Session 80, docs/hackable.md step 2) A page in the parts its frames
 * make: what comes before a frame it starts with (page_before: the
 * menu's, and M_ClearPlanes' on the setup and the map select); the page
 * up to its first wait for the pads (page_X: the next page, or PAGE_WAIT,
 * the presses); a press after each frame (X_press: the next page, or
 * PAGE_WAIT for another). Each wait is M_WaitFramePad's: the frame, then
 * the pads. */
#define PAGE_WAIT ((Page)(PAGE_DONE + 1))

/* -- the panel and the menu ---------------------------------------------- */

/* Options_DrawPanel ($5497A): the panel at the bottom, rows 20-26. */
static void draw_panel(HzGame *g)
{
    u8 mode = g->options[0];
    front_text_palette(g, 1);
    front_text_font(g, FONT_PLAIN);
    front_box_line(g, 1, 20, BOX_TOP_LEFT, BOX_TOP, BOX_TOP_RIGHT, 36);
    front_box_line(g, 1, 21, BOX_LEFT, MENU_BLANK, BOX_RIGHT, 36);
    front_print_at(g, 4, 21, "1 PLAYER SIDE");
    front_box_line(g, 1, 22, BOX_DIVIDE_LEFT, BOX_DIVIDE, BOX_DIVIDE_RIGHT,
                   36);
    front_box_line(g, 1, 23, BOX_LEFT, MENU_BLANK, BOX_RIGHT, 36);
    front_print_at(g, 4, 23, "2 PLAYER SIDE");
    front_box_line(g, 1, 24, BOX_DIVIDE_LEFT, BOX_DIVIDE, BOX_DIVIDE_RIGHT,
                   36);
    front_box_line(g, 1, 25, BOX_LEFT, MENU_BLANK, BOX_RIGHT, 36);
    front_print_at(g, 4, 25, "GAME WINDOW");
    front_box_line(g, 1, 26, BOX_BOTTOM_LEFT, BOX_BOTTOM,
                   (char)BOX_BOTTOM_RIGHT, 36);
    front_text_palette(g, 0);
    front_print_at(g, 20, 21, (mode & 2) ? "Computer Brain" : "User Brain    ");
    front_print_at(g, 20, 23, (mode & 1) ? "Computer Brain" : "User Brain    ");
    front_print_at(g, 20, 25, (mode & 4) ? "Wide Display  " : "Split Display ");
}

/* Options_Run ($548BC): the options song, the menu font and palette, the
 * planes cleared; the cursors and the tests' numbers cleared, the mode
 * byte and the difficulty to their ranges; the heading and the panel.
 * (The 68k also masks the password's letters to 0-15: no password
 * here.) */
static void setup_before(HzGame *g)
{
    HzFront *f = &g->front;
    const HzMenuAssets *m = &g->assets->menu;
    snd_play_music(g, 1);
    snd_start_song(g, 0xFF);
    front_display_off(g);
    front_reset_scroll(g);
    front_clear_sprites(g);
    front_tiles(g, 0x000, m->menu_font, sizeof m->menu_font);
    for (int i = 0; i < 64; i++) {
        f->palette[i] = m->menu_palette[i];
    }
    front_upload_palette(g);
}

/* The rest, after M_ClearPlanes' frame (the display off: none). */
static Page page_setup(HzGame *g)
{
    HzFront *f = &g->front;
    front_clear_planes_after(g);
    f->menu_item = 0;
    f->menu_item_shown = 0;
    f->song = 0;
    f->sfx = 0;
    g->options[0] &= 7;
    g->options[1] &= 3;
    front_text_palette(g, 1);
    front_text_font(g, FONT_TITLE);
    front_print_at(g, 11, 1, "CONFIGURATION MODE");
    draw_panel(g);
    return PAGE_MENU;
}

/* The shape of every highlight on these pages: the entry last
 * highlighted redrawn plain, the new one in the cursor's font. */
static void highlight(HzGame *g, s16 *shown, s16 now,
                      void (*print)(HzGame *g, int n))
{
    front_text_font(g, FONT_PLAIN);
    print(g, *shown);
    front_text_font(g, FONT_HIGHLIGHT);
    *shown = now;
    print(g, now);
}

/* OptionsMenu_PrintItem ($54D48) */
static void print_menu_item(HzGame *g, int n)
{
    front_print_at(g, 3, 5 + 2 * n, MENU_ITEMS[n]);
}

/* Options_MainMenu ($54B78): after a frame, rows 3-17 cleared to the
 * background, the menu's box, its cursor. */
static Page page_menu(HzGame *g)
{
    HzFront *f = &g->front;
    front_fill(g, LAYER_W, 3, 15, 0x0030);
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 1, 3, 8, 11);
    for (int n = 0; n < 5; n++) {
        print_menu_item(g, n);
    }
    highlight(g, &f->menu_item_shown, f->menu_item, print_menu_item);
    return PAGE_WAIT;
}

static Page menu_press(HzGame *g, u16 p)
{
    HzFront *f = &g->front;
    if (p & HZ_PAD_DOWN) {
        if (++f->menu_item > 4) {
            f->menu_item = 0;
        }
        highlight(g, &f->menu_item_shown, f->menu_item, print_menu_item);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_UP) {
        if (--f->menu_item < 0) {
            f->menu_item = 4;
        }
        highlight(g, &f->menu_item_shown, f->menu_item, print_menu_item);
        return PAGE_WAIT;
    }
    if (!(p & (HZ_PAD_B | CHOOSE))) {
        return PAGE_WAIT;
    }
    switch (f->menu_item) {
    case 0:
        /* START: a fresh record, from the first map (and a new saved
         * game, once there is a win to save) */
        g->options[3] = 0;
        for (int t = 0; t < 4; t++) {
            g->win_record[t] = 0;
        }
        g->save_slot = -1;
        return PAGE_MAP_SELECT;
    case 1:
        return PAGE_SAVES;
    case 2:
        return PAGE_P1;
    case 3:
        return PAGE_MUSIC;
    case 4:
        return PAGE_SOUND;
    default:
        return PAGE_WAIT;
    }
}

/* -- PLAYER ------------------------------------------------------------------ */

/* OptionsP1_PrintChoice ($54F20), OptionsP2_PrintChoice ($550C8),
 * OptionsWindow_PrintChoice ($55290) */
static void print_p1_choice(HzGame *g, int n)
{
    front_print_at(g, 14, n ? 9 : 7, n ? "Computer" : "User    ");
}

static void print_p2_choice(HzGame *g, int n)
{
    front_print_at(g, 17, n ? 12 : 10, n ? "Computer" : "User    ");
}

static void print_window_choice(HzGame *g, int n)
{
    front_print_at(g, 16, n ? 15 : 13, n ? "Wide  " : "Split ");
}

/* The two-way choice the PLAYER pages share: up and down toggle, B goes
 * back, A or C stores the choice in v_options' `bit`, redraws the panel
 * and goes on. Each page's print, bit, page back and page next. */
typedef struct {
    void (*print)(HzGame *g, int n);
    u8   bit;
    Page back;
    Page next;
} Side;

static const Side SIDES[3] = {
    { print_p1_choice, 0x02, PAGE_MENU, PAGE_P2 },          /* PAGE_P1 */
    { print_p2_choice, 0x01, PAGE_P1, PAGE_WINDOW },        /* PAGE_P2 */
    { print_window_choice, 0x04, PAGE_P2, PAGE_MENU },      /* PAGE_WINDOW */
};

static const Side *side_of(Page page)
{
    return &SIDES[page - PAGE_P1];
}

static Page choose_side(HzGame *g, Page page)
{
    HzFront *f = &g->front;
    const Side *s = side_of(page);
    f->choice = (g->options[0] & s->bit) ? 1 : 0;
    f->choice_shown = 0;
    highlight(g, &f->choice_shown, f->choice, s->print);
    return PAGE_WAIT;
}

static Page choose_side_press(HzGame *g, Page page, u16 p)
{
    HzFront *f = &g->front;
    const Side *s = side_of(page);
    if (p & (HZ_PAD_UP | HZ_PAD_DOWN)) {
        f->choice = (s16)((f->choice + 1) & 1);
        highlight(g, &f->choice_shown, f->choice, s->print);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_B) {
        return s->back;
    }
    if (p & CHOOSE) {
        g->options[0] = (u8)((g->options[0] & ~s->bit)
                             | (f->choice ? s->bit : 0));
        draw_panel(g);
        return s->next;
    }
    return PAGE_WAIT;
}

/* Options_P1Side ($54D9C): the 1 PLAYER box, v_options b1. */
static Page page_p1(HzGame *g)
{
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 12, 3, 10, 7);
    front_print_at(g, 14, 7, "User");
    front_print_at(g, 14, 9, "Computer");
    front_text_palette(g, 1);
    front_print_at(g, 14, 5, "1 PLAYER");
    front_text_palette(g, 0);
    return choose_side(g, PAGE_P1);
}

/* Options_P2Side ($54F48): the 2 PLAYER box, v_options b0. */
static Page page_p2(HzGame *g)
{
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 15, 6, 10, 7);
    front_print_at(g, 17, 10, "User");
    front_print_at(g, 17, 12, "Computer");
    front_text_palette(g, 1);
    front_print_at(g, 17, 8, "2 PLAYER");
    front_text_palette(g, 0);
    return choose_side(g, PAGE_P2);
}

/* Options_CheckWindow ($550F0): two players or two computers play split
 * screen (b2 cleared; with two users the whole byte is cleared); one
 * computer: the WINDOW box, v_options b2. */
static Page page_window(HzGame *g)
{
    u8 sides = g->options[0] & 3;
    if (sides == 0 || sides == 3) {
        g->options[0] = (u8)(sides == 0 ? 0 : g->options[0] & 3);
        draw_panel(g);
        return PAGE_MENU;
    }
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 14, 9, 8, 7);
    front_print_at(g, 16, 13, "Split");
    front_print_at(g, 16, 15, "Wide");
    front_text_palette(g, 1);
    front_print_at(g, 16, 11, "WINDOW");
    front_text_palette(g, 0);
    return choose_side(g, PAGE_WINDOW);
}

/* -- CONT: the saved games (the port's; see the top) -------------------- */

/* The CONT box: at (12, 3), 22 cells inside, a heading, then a row per
 * slot (rows 6-13). */
#define SAVES_X     12
#define SAVES_ROW0  6

/* The slots as CONT last read them. */
static HzSaves g_saves;

/* A slot's row: "1 VULKAN    5 WON  P1" -- the next map to play, the wins
 * (map and difficulty pairs), the side the human played; EMPTY, or ALL
 * WON for a record with nothing left to play. 21 cells, in the font set
 * the caller chose. */
static void print_save(HzGame *g, int n)
{
    const HzSaveSlot *slot = &g_saves.slot[n];
    char row[64];
    if (!slot->used) {
        snprintf(row, sizeof row, "%d EMPTY              ", n + 1);
    } else if (saves_next_map(slot->record) > 7) {
        snprintf(row, sizeof row, "%d ALL WON          P%d", n + 1,
                 slot->human);
    } else {
        char name[HZ_MAP_NAME + 1];
        const u8 *codes = g->assets->menu.map_names[saves_next_map(
            slot->record)];
        for (int i = 0; i < HZ_MAP_NAME; i++) {
            name[i] = (char)(codes[i] == MENU_BLANK ? ' ' : codes[i]);
        }
        name[HZ_MAP_NAME] = '\0';
        snprintf(row, sizeof row, "%d %s  %2d WON  P%d", n + 1, name,
                 saves_wins(slot->record), slot->human);
    }
    front_print_at(g, SAVES_X + 2, SAVES_ROW0 + n, row);
}

static void draw_saves(HzGame *g)
{
    saves_read(&g_saves);
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box_line(g, SAVES_X, 3, BOX_TOP_LEFT, BOX_TOP, BOX_TOP_RIGHT, 22);
    front_box_line(g, SAVES_X, 4, BOX_LEFT, MENU_BLANK, BOX_RIGHT, 22);
    front_box_line(g, SAVES_X, 5, BOX_DIVIDE_LEFT, BOX_DIVIDE,
                   BOX_DIVIDE_RIGHT, 22);
    for (int n = 0; n < HZ_SAVE_SLOTS; n++) {
        front_box_line(g, SAVES_X, SAVES_ROW0 + n, BOX_LEFT, MENU_BLANK,
                       BOX_RIGHT, 22);
        print_save(g, n);
    }
    front_box_line(g, SAVES_X, SAVES_ROW0 + HZ_SAVE_SLOTS, BOX_BOTTOM_LEFT,
                   BOX_BOTTOM, (char)BOX_BOTTOM_RIGHT, 22);
    front_text_font(g, FONT_TITLE);
    front_print_at(g, SAVES_X + 2, 4, "SAVED GAMES");
    front_text_font(g, FONT_PLAIN);
}

void options_load_save(HzGame *g, const HzSaveSlot *slot, int index)
{
    for (int t = 0; t < 4; t++) {
        g->win_record[t] = slot->record[t];
    }
    g->options[3] = (u8)saves_next_map(slot->record);
    g->options[0] = (u8)((g->options[0] & 4) | (slot->human == 2 ? 2 : 1));
    g->save_slot = index;
}

/* CONT: the slots; up and down pick one, A or C load it (not an empty
 * one, nor one with every map won) and go to the map select, B back to
 * the menu. */
static Page page_saves(HzGame *g)
{
    HzFront *f = &g->front;
    f->save_shown = f->save_cursor;
    draw_saves(g);
    highlight(g, &f->save_shown, f->save_cursor, print_save);
    return PAGE_WAIT;
}

static Page saves_press(HzGame *g, u16 p)
{
    HzFront *f = &g->front;
    if (p & (HZ_PAD_DOWN | HZ_PAD_UP)) {
        int dir = (p & HZ_PAD_DOWN) ? 1 : HZ_SAVE_SLOTS - 1;
        f->save_cursor = (s16)((f->save_cursor + dir) % HZ_SAVE_SLOTS);
        highlight(g, &f->save_shown, f->save_cursor, print_save);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_B) {
        return PAGE_MENU;
    }
    if (p & CHOOSE) {
        const HzSaveSlot *slot = &g_saves.slot[f->save_cursor];
        if (slot->used && saves_next_map(slot->record) < 8) {
            options_load_save(g, slot, f->save_cursor);
            draw_panel(g);
            return PAGE_MAP_SELECT;
        }
    }
    return PAGE_WAIT;
}

/* -- MUSIC and SOUND ----------------------------------------------------------- */

/* MusicTest_PrintTitle ($55876) */
static void print_song_title(HzGame *g)
{
    const HzMenuAssets *m = &g->assets->menu;
    front_text_palette(g, 1);
    front_text_font(g, FONT_PLAIN);
    for (int k = 0; k < 2; k++) {
        front_cursor(g, 14, 5 + k);
        front_print_codes(g, m->song_titles[g->front.song][k], HZ_SONG_TITLE);
    }
}

/* The options song again, as B on the tests leaves them. */
static void options_song(HzGame *g)
{
    snd_play_music(g, 1);
    snd_start_song(g, 0xFF);
}

/* MusicTest_Run ($5571C): the 18 songs by title; A or C plays one,
 * repeating as MusicTestRepeat says. */
static Page page_music(HzGame *g)
{
    snd_fade_music(g, 2);
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 12, 3, 21, 4);
    print_song_title(g);
    return PAGE_WAIT;
}

static Page music_press(HzGame *g, u16 p)
{
    HzFront *f = &g->front;
    if (p & HZ_PAD_DOWN) {
        if (++f->song > HZ_SONGS - 1) {
            f->song = 0;
        }
        print_song_title(g);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_UP) {
        if (--f->song < 0) {
            f->song = HZ_SONGS - 1;
        }
        print_song_title(g);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_B) {
        options_song(g);
        return PAGE_MENU;
    }
    if (p & CHOOSE) {
        snd_play_music(g, (u16)f->song);
        snd_start_song(g, g->assets->menu.song_repeat[f->song] ? 0xFF
                                                               : 0x00);
    }
    return PAGE_WAIT;
}

/* SoundTest_PrintNumber ($55C8E) */
static void print_sfx_number(HzGame *g)
{
    front_text_palette(g, 1);
    front_cursor(g, 20, 5);
    front_print_2digits(g, g->front.sfx & 0xFF);
}

/* SoundTest_Run ($55B9A): the effects by number; A or C plays one. */
static Page page_sound(HzGame *g)
{
    snd_fade_music(g, 2);
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 12, 3, 10, 3);
    front_print_at(g, 14, 5, "SOUND");
    print_sfx_number(g);
    return PAGE_WAIT;
}

static Page sound_press(HzGame *g, u16 p)
{
    HzFront *f = &g->front;
    int count = g->assets->menu.sfx_count;
    if (p & HZ_PAD_DOWN) {
        if (++f->sfx > count - 1) {
            f->sfx = 0;
        }
        print_sfx_number(g);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_UP) {
        if (--f->sfx < 0) {
            f->sfx = (s16)(count - 1);
        }
        print_sfx_number(g);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_B) {
        options_song(g);
        return PAGE_MENU;
    }
    if (p & CHOOSE) {
        front_play_sfx(g, (u8)f->sfx);
    }
    return PAGE_WAIT;
}

/* -- the map select and the difficulty ------------------------------------- */

/* MapSelect_PrintName ($55EB2) */
static void print_map_name(HzGame *g, int n)
{
    front_cursor(g, 3, 4 + 2 * n);
    front_print_codes(g, g->assets->menu.map_names[n], HZ_MAP_NAME);
}

/* MapSelect_DrawNames ($5606E): the names, greyed if won on all four
 * difficulties; with one computer side each followed by its A-D, greyed
 * where won (MapSelect_PrintWins: its two blanks in the name's font). */
static void draw_map_names(HzGame *g)
{
    front_text_palette(g, 0);
    for (int n = 0; n < HZ_MAPS; n++) {
        front_text_font(g, map_complete(g, n) ? FONT_WON : FONT_PLAIN);
        print_map_name(g, n);
        if (!one_cpu(g)) {
            continue;
        }
        front_print(g, "  ");
        for (int t = 0; t < 4; t++) {
            static const char *const letter[4] = { "A", "B", "C", "D" };
            front_text_font(g, won(g, t, n) ? FONT_WON : FONT_PLAIN);
            front_print(g, letter[t]);
        }
    }
}

/* MapSelect_Preview ($55F20): the map's radar picture in a palette-1 box
 * at (25, 2): the map's palette into CRAM line 3, its radar into
 * tiles $100-$13F, drawn 8 x 8 at (26, 3). */
static void map_preview(HzGame *g)
{
    HzFront *f = &g->front;
    const HzMapAssets *map = &g->assets->map[f->map & 7];
    front_text_palette(g, 1);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 25, 2, 8, 8);
    for (int i = 0; i < 16; i++) {
        f->palette[48 + i] = map->palette[i];
    }
    front_upload_lines(g, 0x8);
    front_tiles(g, 0x100, map->radar, HZ_RADAR_IMAGE);
    front_picture(g, LAYER_W, 26, 3, 0xE100, 8, 8);
}

/* MapSelect_Highlight ($55E80): the old name plain (QUIRK: plain even if
 * that map is won on all four; the cursor never rests on such a map, so
 * it never shows), the new one highlighted, then the preview. */
static void map_highlight(HzGame *g)
{
    HzFront *f = &g->front;
    front_text_palette(g, 0);
    highlight(g, &f->map_shown, f->map, print_map_name);
    map_preview(g);
}

/* One step of the map cursor, skipping maps won on all four
 * difficulties. UNKNOWN: with every map won the 68k would loop for ever;
 * no way into the map select has such a record (START clears it; the 68k's
 * password refuses it, and CONT here won't load it), so the port gives up
 * after a lap. */
static void step_map(HzGame *g, int dir)
{
    HzFront *f = &g->front;
    for (int tries = 0; tries < HZ_MAPS; tries++) {
        f->map = (s16)((f->map + dir) & 7);
        if (!map_complete(g, f->map)) {
            return;
        }
    }
}

/* MapSelect_Run ($55CB2): the map select's song (page_before, before
 * M_ClearPlanes' frame), the planes cleared, the cursor on
 * v_option_map. */
static void map_select_before(HzGame *g)
{
    snd_play_music(g, 2);
    snd_start_song(g, 0xFF);
}

static Page page_map_select(HzGame *g)
{
    HzFront *f = &g->front;
    front_clear_planes_after(g);
    f->map = g->options[3];
    f->map_shown = g->options[3];
    return PAGE_MAP;
}

/* MapSelect_Redraw ($55CD6): the list (wider, with the A-D columns, when
 * exactly one side is the computer), up and down to pick; B back to the
 * whole options screen afresh; A or C: the map, then the difficulty with
 * one computer side, else the game. */
static Page page_map(HzGame *g)
{
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 1, 2, one_cpu(g) ? 15 : 9, 17);
    draw_map_names(g);
    map_highlight(g);
    return PAGE_WAIT;
}

static Page map_press(HzGame *g, u16 p)
{
    HzFront *f = &g->front;
    if (p & HZ_PAD_DOWN) {
        step_map(g, 1);
        map_highlight(g);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_UP) {
        step_map(g, -1);
        map_highlight(g);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_B) {
        return PAGE_SETUP;
    }
    if (p & CHOOSE) {
        g->options[3] = (u8)f->map;
        return one_cpu(g) ? PAGE_TYPE : PAGE_DONE;
    }
    return PAGE_WAIT;
}

/* TypeSelect_PrintEntry ($563AA) */
static void print_type(HzGame *g, int n)
{
    front_print_at(g, 14, 5 + 2 * n, TYPE_NAMES[n]);
}

/* TypeSelect_DrawList ($563F6): D up to A, each greyed if won on this map;
 * the easiest not won. UNKNOWN: with all four won the 68k's d3 would be
 * left over; unreachable (the map select skips such maps), 0 here. */
static int draw_type_list(HzGame *g)
{
    int first = 0;
    for (int t = 3; t >= 0; t--) {
        bool w = won(g, t, g->options[3]);
        front_text_font(g, w ? FONT_WON : FONT_PLAIN);
        if (!w) {
            first = t;
        }
        print_type(g, t);
    }
    return first;
}

/* One step of the difficulty cursor, skipping those won on this map
 * (a lap at most, as step_map). */
static void step_type(HzGame *g, int dir)
{
    HzFront *f = &g->front;
    for (int tries = 0; tries < 4; tries++) {
        f->type = (s16)((f->type + dir) & 3);
        if (!won(g, f->type, g->options[3])) {
            return;
        }
    }
}

/* TypeSelect_Run ($561E4): TYPE A-D, the cursor on the easiest not yet
 * won; B back to the map select; A or C: v_difficulty and v_option2, the
 * panel, the game. */
static Page page_type(HzGame *g)
{
    HzFront *f = &g->front;
    front_text_palette(g, 0);
    front_text_font(g, FONT_PLAIN);
    front_box(g, 12, 3, 8, 9);
    for (int t = 0; t < 4; t++) {
        print_type(g, t);
    }
    int first = draw_type_list(g);
    f->type = (s16)first;
    f->type_shown = (s16)first;
    highlight(g, &f->type_shown, f->type, print_type);
    return PAGE_WAIT;
}

static Page type_press(HzGame *g, u16 p)
{
    HzFront *f = &g->front;
    if (p & HZ_PAD_DOWN) {
        step_type(g, 1);
        highlight(g, &f->type_shown, f->type, print_type);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_UP) {
        step_type(g, -1);
        highlight(g, &f->type_shown, f->type, print_type);
        return PAGE_WAIT;
    }
    if (p & HZ_PAD_B) {
        return PAGE_MAP;
    }
    if (p & CHOOSE) {
        g->options[1] = (u8)f->type;
        draw_panel(g);
        g->options[2] = OPTION2_BY_TYPE[f->type];
        return PAGE_DONE;
    }
    return PAGE_WAIT;
}

/* -- the pages' run -------------------------------------------------------- */

/* Whether the page starts with a frame, and what comes before it. */
static bool page_before(HzGame *g, Page page)
{
    switch (page) {
    case PAGE_SETUP:
        setup_before(g);
        return true;
    case PAGE_MENU:
        return true;
    case PAGE_MAP_SELECT:
        map_select_before(g);
        return true;
    default:
        return false;
    }
}

static Page page_enter(HzGame *g, Page page)
{
    switch (page) {
    case PAGE_SETUP:
        return page_setup(g);
    case PAGE_MENU:
        return page_menu(g);
    case PAGE_P1:
        return page_p1(g);
    case PAGE_P2:
        return page_p2(g);
    case PAGE_WINDOW:
        return page_window(g);
    case PAGE_SAVES:
        return page_saves(g);
    case PAGE_MUSIC:
        return page_music(g);
    case PAGE_SOUND:
        return page_sound(g);
    case PAGE_MAP_SELECT:
        return page_map_select(g);
    case PAGE_MAP:
        return page_map(g);
    case PAGE_TYPE:
        return page_type(g);
    default:
        return PAGE_DONE;
    }
}

static Page page_press(HzGame *g, Page page, u16 p)
{
    switch (page) {
    case PAGE_MENU:
        return menu_press(g, p);
    case PAGE_P1:
    case PAGE_P2:
    case PAGE_WINDOW:
        return choose_side_press(g, page, p);
    case PAGE_SAVES:
        return saves_press(g, p);
    case PAGE_MUSIC:
        return music_press(g, p);
    case PAGE_SOUND:
        return sound_press(g, p);
    case PAGE_MAP:
        return map_press(g, p);
    case PAGE_TYPE:
        return type_press(g, p);
    default:
        return PAGE_DONE;
    }
}

/* Options_Run's whole flow, page by page, until the game: before each
 * page the test seam (front_probe). (Session 80) Its phases: a page's
 * start, its part after the frame it starts with, a press. A quit ends
 * it at a page's start or at a wait for the pads. */
enum {
    OPT_PAGE,
    OPT_ENTER,
    OPT_PRESS
};

void options_run_begin(HzGame *g)
{
    g->front_run.page = PAGE_SETUP;
    g->front_run.page_phase = OPT_PAGE;
}

HzRun options_run_step(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    for (;;) {
        Page page = (Page)r->page;
        Page next;
        if (r->page_phase == OPT_PAGE) {
            if (page == PAGE_DONE || g->quit) {
                return HZ_RUN_DONE;
            }
            if (g->front_probe != NULL) {
                g->front_probe(g, page, g->front_probe_user);
                if (g->quit) {
                    return HZ_RUN_DONE;
                }
            }
            if (page_before(g, page)) {
                r->page_phase = OPT_ENTER;
                return HZ_RUN_FRAME;
            }
            next = page_enter(g, page);
        } else if (r->page_phase == OPT_ENTER) {
            next = page_enter(g, page);
        } else {
            next = page_press(g, page, front_read_pads(g));
        }
        if (next != PAGE_WAIT) {
            r->page = (u8)next;
            r->page_phase = OPT_PAGE;
            continue;
        }
        if (g->quit) {
            r->page = PAGE_DONE;
            return HZ_RUN_DONE;
        }
        r->page_phase = OPT_PRESS;
        return HZ_RUN_FRAME;
    }
}
