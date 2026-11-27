/* The endings: the $53000 module's Ending_Run ($536AC), from the game's
 * end (menu_game_end, game_end.c) once the win record holds every map on
 * every difficulty. Three parts, each faded in from black:
 *
 *   the story -- a picture (a still life with a letter, or a grave when
 *   the human played P2 against the computer) with the story's 22 lines
 *   scrolled up over it, a line of scroll every 7 frames;
 *   the staff roll -- over the forest painting, headings red and names
 *   yellow, a line of scroll every 4 frames, the maker's name last;
 *   ENDE -- over the forest, until Start or a minute.
 *
 * Everything shows through the menus' VDP set-up (front.h): the pictures
 * on the window plane in palette 3, the text on plane B (high priority, so
 * over the window), scrolled up through its 32 rows round and round. The
 * text starts on row 29, below the screen's 28; each row is cleared as
 * it comes round again, before it scrolls into view.
 *
 * The data is the assets' ending/ folder (HzEndingAssets). */

#include "front.h"

/* The tiles, as Ending_Run lays them out. */
#define STORY_FONT_TILE 0x000       /* a character c is tile c - $30 */
#define ENDE_TILE       0x080       /* tiles $80-$BF */
#define PICTURE_TILE    0x100       /* tiles $100-$55F */
#define PICTURE_CELL    0x6100      /* palette 3, tile $100 */
#define ENDE_CELL       0xC080      /* priority, palette 2, tile $80 */
#define BLANK_CELL      0x0010      /* the story font's blank, palette 0 */

/* Where the text goes in layer B's rows. */
#define FIRST_ROW       29          /* below the screen (rows 0-27 show) */
#define STORY_X         2
#define STAFF_X         12
#define ENDE_X          12
#define ENDE_Y          12

/* Frames a line of scroll takes. */
#define STORY_SCROLL    7
#define STAFF_SCROLL    4

/* The story's songs (the MUSIC test's names) and the staff roll's. */
#define SONG_ENDING_P1  0x0F        /* "Be his soul rest in peace" */
#define SONG_ENDING     0x10        /* "A full retreat" */
#define SONG_STAFF      0x11        /* "Go with the stream" */

#define ENDE_FRAMES     0xE10       /* + 1: 3,601 frames, a minute */

/* The game mode's sides (v_options & 3): 1 the human P1 against the
 * computer, 2 the human P2 against the computer. */
static int sides(const HzGame *g)
{
    return g->options[0] & 3;
}

/* M_PrintStrAtB ($56BB4): n characters of the story font on plane B from
 * (x, y), in m_text_attr; the cursor left after them. */
static void print_b(HzGame *g, int x, int y, const u8 *codes, int n)
{
    HzFront *f = &g->front;
    front_cursor(g, x, y);
    for (int i = 0; i < n; i++) {
        front_put(g, LAYER_B, f->cursor_x, y,
                  (u16)(codes[i] - 0x30 + f->text_attr));
        f->cursor_x++;
    }
}

/* A row of n blanks (the 68k prints a line of them: EndingBlankLine,
 * StaffBlankLine). */
static void clear_row(HzGame *g, int x, int y, int n)
{
    static const u8 BLANKS[HZ_STORY_WIDTH] = {
        '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@',
        '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@',
        '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@', '@',
    };
    print_b(g, x, y, BLANKS, n);
}

/* Ending_ScrollLine ($538D8) and Staff_ScrollLine ($539DE): plane B up
 * 8 lines, one every `frames` frames (the scroll set after the wait). */
static void scroll_row(HzGame *g, int frames)
{
    HzFront *f = &g->front;
    for (int line = 0; line < 8 && !g->quit; line++) {
        f->scroll_y_b = (s16)((f->scroll_y_b + 1) & 0xFF);
        f->scroll_x_b = 0;
        for (int i = 0; i < frames; i++) {
            front_frame(g);
        }
        front_apply_scroll(g);
    }
}

/* A picture from PICTURE_TILE on the window: 28 rows of 40 cells. */
static void draw_picture(HzGame *g)
{
    front_picture(g, LAYER_W, 0, 0, PICTURE_CELL, 40, 28);
}

/* Ending_ShowPicture ($537E4): the still life, or the grave when the
 * human played P2 against the computer; line 3's target its palette, line
 * 3 black; the window; faded in from black. The fade takes lines 0-2 up
 * to their targets too: WinnerPalette's, left from the results, which
 * nothing on screen uses. */
static void show_picture(HzGame *g)
{
    const HzEndingAssets *e = &g->assets->ending;
    HzFront *f = &g->front;
    int n = (sides(g) == 2) ? 1 : 0;
    front_tiles(g, PICTURE_TILE, e->pictures[n], sizeof e->pictures[n]);
    for (int i = 0; i < 16; i++) {
        f->target[48 + i] = e->picture_palettes[n][i];
        f->palette[48 + i] = 0;
    }
    draw_picture(g);
    front_fade_from_black(g);
}

/* Ending_SetTextPalette ($53668): line 0 the text's (EndingTextPalette:
 * colour 15 white, 1 the shadow), in CRAM and as the target. */
static void set_text_palette(HzGame *g)
{
    HzFront *f = &g->front;
    for (int i = 0; i < 16; i++) {
        f->palette[i] = g->assets->ending.text_palette[i];
    }
    front_upload_lines(g, 0x01);
    front_set_target(g);
}

/* Ending_ScrollStory ($53864): the picture, then the story, a line every
 * second row: the row cleared, a row of scroll, the line printed, another
 * row of scroll. Then 29 rows cleared one after another, the text
 * scrolled off the top. 4,088 frames.
 * CHANGED: the 68k clears a row by printing EndingText2's last line
 * (EndingBlankLine), which is blank; the port clears with blanks, so an
 * edited last line of story_p2.txt shows only in its own place. */
static void scroll_story(HzGame *g)
{
    show_picture(g);
    set_text_palette(g);
    const u8 (*story)[HZ_STORY_WIDTH] =
        g->assets->ending.story[sides(g) == 2 ? 1 : 0];
    int row = FIRST_ROW;
    for (int line = 0; line < HZ_STORY_LINES && !g->quit; line++) {
        clear_row(g, STORY_X, row, HZ_STORY_WIDTH);
        scroll_row(g, STORY_SCROLL);
        print_b(g, STORY_X, row, story[line], HZ_STORY_WIDTH);
        scroll_row(g, STORY_SCROLL);
        row = (row + 2) % PLANE_ROWS;
    }
    for (int i = 0; i < 29 && !g->quit; i++) {
        clear_row(g, STORY_X, row, HZ_STORY_WIDTH);
        scroll_row(g, STORY_SCROLL);
        row = (row + 1) % PLANE_ROWS;
    }
}

/* Ending_ShowForest ($535E6): the forest painting (its tiles in already) on
 * the window; every colour black; the targets: line 3 the forest's, line
 * 2 the text palette (ENDE's colours), colour 15 of line 0 yellow (the
 * names) and of line 1 red (the headings); faded in. The rest of lines 0
 * and 1 keep their targets from before -- line 0 the text palette, line 1
 * WinnerPalette's -- so a name's shadow (colour 1) is $022 and a
 * heading's black. */
static void show_forest(HzGame *g)
{
    const HzEndingAssets *e = &g->assets->ending;
    HzFront *f = &g->front;
    draw_picture(g);
    for (int i = 0; i < 64; i++) {
        f->palette[i] = 0;
    }
    for (int i = 0; i < 16; i++) {
        f->target[48 + i] = e->forest_palette[i];
        f->target[32 + i] = e->text_palette[i];
    }
    f->target[15] = 0x00EE;
    f->target[31] = 0x000E;
    front_fade_from_black(g);
}

/* Staff_BlankLines ($539C0): n rows cleared, each scrolled up. */
static int staff_blank_rows(HzGame *g, int row, int n)
{
    for (int i = 0; i < n && !g->quit; i++) {
        clear_row(g, STAFF_X, row, HZ_STAFF_WIDTH);
        scroll_row(g, STAFF_SCROLL);
        row = (row + 1) % PLANE_ROWS;
    }
    return row;
}

/* Staff_Scroll ($5391C): the roll, a row each line, headings in palette 1
 * (red) and names in palette 0 (yellow); a blank row before the first name
 * after a heading, 17 before the first heading after a name. Then a blank
 * row, the maker's name by the machine's region (yellow), and 32 blank
 * rows, the screen scrolled clear. 5,792 frames. */
static void staff_roll(HzGame *g)
{
    const HzEndingAssets *e = &g->assets->ending;
    front_text_palette(g, 1);
    int row = FIRST_ROW;
    for (int i = 0; i < e->staff_count && !g->quit; i++) {
        const HzStaffLine *line = &e->staff[i];
        if (i > 0 && line->name != e->staff[i - 1].name) {
            front_text_palette(g, line->name ? 0 : 1);
            row = staff_blank_rows(g, row, line->name ? 1 : 17);
        }
        print_b(g, STAFF_X, row, line->text, HZ_STAFF_WIDTH);
        scroll_row(g, STAFF_SCROLL);
        row = (row + 1) % PLANE_ROWS;
    }
    row = staff_blank_rows(g, row, 1);
    front_text_palette(g, 0);
    print_b(g, STAFF_X, row, e->maker[(g->hw_version & 0x80) ? 1 : 0],
            HZ_STAFF_WIDTH);
    scroll_row(g, STAFF_SCROLL);
    row = (row + 1) % PLANE_ROWS;
    staff_blank_rows(g, row, 32);
}

/* ENDE on layer B at (12, 12): 4 rows of 16 cells, palette 2. */
static void draw_ende(HzGame *g)
{
    front_picture(g, LAYER_B, ENDE_X, ENDE_Y, ENDE_CELL, 16, 4);
}

/* Ending_Run ($536AC). The story: the story font in, text in palette 0;
 * its song ($F if the human played P1 against the computer, else $10,
 * once); the story; a fade to black. The staff roll: plane B and the
 * window filled with blanks; song $11, repeating; the forest faded in; the
 * roll. ENDE over the forest until Start or a minute; the music faded and
 * the screen faded to black, then the title as after any game.
 * CHANGED: after the story font goes in, the 68k turns the display on
 * (the screen still black) and busy-waits about 85 ms, five frames; the
 * port doesn't wait, as with the SEGA logo's. */
void ending_run(HzGame *g)
{
    const HzEndingAssets *e = &g->assets->ending;
    front_reset_scroll(g);
    front_clear_sprites(g);
    front_display_off(g);
    front_clear_planes(g);
    front_tiles(g, STORY_FONT_TILE, e->story_font, sizeof e->story_font);
    front_text_font(g, 0);
    front_text_palette(g, 0);
    front_display_on(g);
    snd_play_music(g, sides(g) == 1 ? SONG_ENDING_P1 : SONG_ENDING);
    snd_start_song(g, 0);
    scroll_story(g);
    front_fade_to_black(g);

    front_fill(g, LAYER_B, 0, PLANE_ROWS, BLANK_CELL);
    front_fill(g, LAYER_W, 0, PLANE_ROWS, BLANK_CELL);
    snd_play_music(g, SONG_STAFF);
    snd_start_song(g, 0xFF);
    front_display_off(g);
    front_clear_palette(g);
    front_reset_scroll(g);
    front_tiles(g, PICTURE_TILE, e->forest, sizeof e->forest);
    front_display_on(g);
    show_forest(g);
    front_frame(g);
    front_reset_scroll(g);
    staff_roll(g);
    front_frame(g);
    front_reset_scroll(g);

    front_tiles(g, ENDE_TILE, e->ende, sizeof e->ende);
    draw_ende(g);
    front_set_target(g);
    for (int i = 0; i <= ENDE_FRAMES && !g->quit; i++) {
        if (front_wait_pad(g) & HZ_PAD_START) {
            break;
        }
    }
    snd_fade_music(g, 1);
    front_fade_to_black(g);
}

/* The endings on their own, for a look without the record (main.c's
 * --ending): the menus' VDP set-up, the palette's targets as the results
 * leave them (WinnerPalette's lines and the photo's blues), then Ending_Run
 * for the side `human` (1 P1, 2 P2) against the computer. v_options is
 * put back afterwards. */
void ending_preview(HzGame *g, int human)
{
    const HzMenuAssets *m = &g->assets->menu;
    HzFront *f = &g->front;
    u8 mode = g->options[0];
    g->options[0] = (u8)((mode & ~3) | (human == 2 ? 2 : 1));
    front_init(g);
    for (int i = 0; i < 48; i++) {
        f->target[i] = m->winner_palette[i];
    }
    for (int i = 0; i < 16; i++) {
        f->target[48 + i] = m->winner_photo_palette[i];
    }
    ending_run(g);
    front_display_off(g);
    g->options[0] = mode;
}
