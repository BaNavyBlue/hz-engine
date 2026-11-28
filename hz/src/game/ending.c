/* The endings: the $53000 module's Ending_Run ($536AC), from the game's
 * end (MenuModule_GameEnd, game_end.c) once the win record holds every map on
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
 * 8 lines, one every `frames` frames (the scroll set after the wait), none
 * after a quit. (Session 80) Begun, then stepped a frame at a time. */
static void scroll_row_begin(HzGame *g, int frames)
{
    HzFrontRun *r = &g->front_run;
    r->scroll_frames = (u8)frames;
    r->scroll_line = 0;
    r->scroll_frame = 0;
}

static HzRun scroll_row_step(HzGame *g)
{
    HzFront *f = &g->front;
    HzFrontRun *r = &g->front_run;
    for (;;) {
        if (r->scroll_frame == 0) {
            if (r->scroll_line >= 8 || g->quit) {
                return HZ_RUN_DONE;
            }
            f->scroll_y_b = (s16)((f->scroll_y_b + 1) & 0xFF);
            f->scroll_x_b = 0;
        }
        if (r->scroll_frame < r->scroll_frames) {
            r->scroll_frame++;
            return HZ_RUN_FRAME;
        }
        front_apply_scroll(g);
        r->scroll_frame = 0;
        r->scroll_line++;
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
 * nothing on screen uses. (Session 80) Up to the fade, ending_run_step's
 * (E_PICTURE). */
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

/* Ending_ShowForest ($535E6): the forest painting (its tiles in already) on
 * the window; every colour black; the targets: line 3 the forest's, line
 * 2 the text palette (ENDE's colours), colour 15 of line 0 yellow (the
 * names) and of line 1 red (the headings); faded in. The rest of lines 0
 * and 1 keep their targets from before -- line 0 the text palette, line 1
 * WinnerPalette's -- so a name's shadow (colour 1) is $022 and a
 * heading's black. (Session 80) Up to the fade, ending_run_step's
 * (E_FOREST). */
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
 * port doesn't wait, as with the SEGA logo's.
 *
 * Ending_ScrollStory ($53864): the picture, then the story, a line every
 * second row: the row cleared, a row of scroll, the line printed, another
 * row of scroll. Then 29 rows cleared one after another, the text
 * scrolled off the top. 4,088 frames.
 * CHANGED: the 68k clears a row by printing EndingText2's last line
 * (EndingBlankLine), which is blank; the port clears with blanks, so an
 * edited last line of story_p2.txt shows only in its own place.
 *
 * Staff_Scroll ($5391C): the roll, a row each line, headings in palette 1
 * (red) and names in palette 0 (yellow); a blank row before the first name
 * after a heading, 17 before the first heading after a name
 * (Staff_BlankLines, $539C0: n rows cleared, each scrolled up). Then a
 * blank row, the maker's name by the machine's region (yellow), and 32
 * blank rows, the screen scrolled clear. 5,792 frames. */
/* (Session 80, docs/hackable.md step 2) Its phases, a frame at a time. */
enum {
    E_START,            /* up to M_ClearPlanes' frame (the display off) */
    E_PLANES,           /* the font, the song, the picture */
    E_PICTURE,          /*   faded in */
    E_STORY_LINE,       /* a line of the story: its row cleared, scrolled */
    E_STORY_PRINT,      /*   the line printed, scrolled */
    E_STORY_NEXT,       /*   the next line */
    E_STORY_CLEAR,      /* the 29 rows after the story: one cleared, */
    E_STORY_CLEAR_NEXT, /*   scrolled */
    E_STORY_OUT,        /* the fade to black; the forest */
    E_FOREST,           /*   faded in, */
    E_FOREST_FRAME,     /*   a frame */
    E_STAFF_LINE,       /* a line of the roll: the blank rows before it, */
    E_STAFF_PRINT,      /*   the line printed, scrolled */
    E_STAFF_NEXT,       /*   the next line */
    E_BLANK,            /* r->blank blank rows: one cleared, */
    E_BLANK_NEXT,       /*   scrolled; then r->blank_after */
    E_MAKER,            /* the maker's name, scrolled */
    E_MAKER_NEXT,       /*   then 32 blank rows */
    E_STAFF_END,        /* a frame */
    E_ENDE,             /* ENDE, then until Start */
    E_ENDE_WAIT,        /*   or a minute: the frame, */
    E_ENDE_PAD,         /*   the pads */
    E_FADE_OUT          /* the music faded, the fade to black */
};

void ending_run_begin(HzGame *g)
{
    g->front_run.ending_phase = E_START;
}

/* ENDE's end: the music faded, the fade to black begun. */
static void ende_end(HzGame *g)
{
    snd_fade_music(g, 1);
    front_fade_to_black_begin(g);
    g->front_run.ending_phase = E_FADE_OUT;
}

/* Blank rows, then `after`. */
static void blank_rows(HzGame *g, int n, u8 after)
{
    HzFrontRun *r = &g->front_run;
    r->blank = (s16)n;
    r->blank_after = after;
    r->ending_phase = E_BLANK;
}

HzRun ending_run_step(HzGame *g)
{
    const HzEndingAssets *e = &g->assets->ending;
    HzFrontRun *r = &g->front_run;
    for (;;) {
        switch (r->ending_phase) {
        case E_START:
            front_reset_scroll(g);
            front_clear_sprites(g);
            front_display_off(g);
            r->ending_phase = E_PLANES;
            return HZ_RUN_FRAME;
        case E_PLANES:
            front_clear_planes_after(g);
            front_tiles(g, STORY_FONT_TILE, e->story_font,
                        sizeof e->story_font);
            front_text_font(g, 0);
            front_text_palette(g, 0);
            front_display_on(g);
            snd_play_music(g, sides(g) == 1 ? SONG_ENDING_P1 : SONG_ENDING);
            snd_start_song(g, 0);
            show_picture(g);
            front_fade_from_black_begin(g);
            r->ending_phase = E_PICTURE;
            break;
        case E_PICTURE:
            if (front_fade_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            set_text_palette(g);
            r->row = FIRST_ROW;
            r->line = 0;
            r->ending_phase = E_STORY_LINE;
            break;
        case E_STORY_LINE:
            if (r->line < HZ_STORY_LINES && !g->quit) {
                clear_row(g, STORY_X, r->row, HZ_STORY_WIDTH);
                scroll_row_begin(g, STORY_SCROLL);
                r->ending_phase = E_STORY_PRINT;
            } else {
                r->blank = 29;
                r->ending_phase = E_STORY_CLEAR;
            }
            break;
        case E_STORY_PRINT:
            if (scroll_row_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            print_b(g, STORY_X, r->row,
                    e->story[sides(g) == 2 ? 1 : 0][r->line], HZ_STORY_WIDTH);
            scroll_row_begin(g, STORY_SCROLL);
            r->ending_phase = E_STORY_NEXT;
            break;
        case E_STORY_NEXT:
            if (scroll_row_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            r->row = (s16)((r->row + 2) % PLANE_ROWS);
            r->line++;
            r->ending_phase = E_STORY_LINE;
            break;
        case E_STORY_CLEAR:
            if (r->blank > 0 && !g->quit) {
                clear_row(g, STORY_X, r->row, HZ_STORY_WIDTH);
                scroll_row_begin(g, STORY_SCROLL);
                r->ending_phase = E_STORY_CLEAR_NEXT;
            } else {
                front_fade_to_black_begin(g);
                r->ending_phase = E_STORY_OUT;
            }
            break;
        case E_STORY_CLEAR_NEXT:
            if (scroll_row_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            r->row = (s16)((r->row + 1) % PLANE_ROWS);
            r->blank--;
            r->ending_phase = E_STORY_CLEAR;
            break;
        case E_STORY_OUT:
            if (front_fade_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
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
            front_fade_from_black_begin(g);
            r->ending_phase = E_FOREST;
            break;
        case E_FOREST:
            if (front_fade_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            r->ending_phase = E_FOREST_FRAME;
            return HZ_RUN_FRAME;
        case E_FOREST_FRAME:
            front_reset_scroll(g);
            front_text_palette(g, 1);
            r->row = FIRST_ROW;
            r->line = 0;
            r->ending_phase = E_STAFF_LINE;
            break;
        case E_STAFF_LINE:
            if (r->line < e->staff_count && !g->quit) {
                const HzStaffLine *line = &e->staff[r->line];
                if (r->line > 0 && line->name != e->staff[r->line - 1].name) {
                    front_text_palette(g, line->name ? 0 : 1);
                    blank_rows(g, line->name ? 1 : 17, E_STAFF_PRINT);
                } else {
                    r->ending_phase = E_STAFF_PRINT;
                }
            } else {
                blank_rows(g, 1, E_MAKER);
            }
            break;
        case E_STAFF_PRINT:
            print_b(g, STAFF_X, r->row, e->staff[r->line].text,
                    HZ_STAFF_WIDTH);
            scroll_row_begin(g, STAFF_SCROLL);
            r->ending_phase = E_STAFF_NEXT;
            break;
        case E_STAFF_NEXT:
            if (scroll_row_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            r->row = (s16)((r->row + 1) % PLANE_ROWS);
            r->line++;
            r->ending_phase = E_STAFF_LINE;
            break;
        case E_BLANK:
            if (r->blank > 0 && !g->quit) {
                clear_row(g, STAFF_X, r->row, HZ_STAFF_WIDTH);
                scroll_row_begin(g, STAFF_SCROLL);
                r->ending_phase = E_BLANK_NEXT;
            } else {
                r->ending_phase = r->blank_after;
            }
            break;
        case E_BLANK_NEXT:
            if (scroll_row_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            r->row = (s16)((r->row + 1) % PLANE_ROWS);
            r->blank--;
            r->ending_phase = E_BLANK;
            break;
        case E_MAKER:
            front_text_palette(g, 0);
            print_b(g, STAFF_X, r->row,
                    e->maker[(g->hw_version & 0x80) ? 1 : 0], HZ_STAFF_WIDTH);
            scroll_row_begin(g, STAFF_SCROLL);
            r->ending_phase = E_MAKER_NEXT;
            break;
        case E_MAKER_NEXT:
            if (scroll_row_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            r->row = (s16)((r->row + 1) % PLANE_ROWS);
            blank_rows(g, 32, E_STAFF_END);
            break;
        case E_STAFF_END:
            r->ending_phase = E_ENDE;
            return HZ_RUN_FRAME;
        case E_ENDE:
            front_reset_scroll(g);
            front_tiles(g, ENDE_TILE, e->ende, sizeof e->ende);
            draw_ende(g);
            front_set_target(g);
            r->n = 0;
            r->ending_phase = E_ENDE_WAIT;
            break;
        case E_ENDE_WAIT:
            if (r->n <= ENDE_FRAMES && !g->quit) {
                r->ending_phase = E_ENDE_PAD;
                return HZ_RUN_FRAME;
            }
            ende_end(g);
            break;
        case E_ENDE_PAD:
            if (front_read_pads(g) & HZ_PAD_START) {
                ende_end(g);
            } else {
                r->n++;
                r->ending_phase = E_ENDE_WAIT;
            }
            break;
        default:                                /* E_FADE_OUT */
            return front_fade_step(g);
        }
    }
}

/* The endings on their own, for a look without the record (main.c's
 * --ending): the menus' VDP set-up, the palette's targets as the results
 * leave them (WinnerPalette's lines and the photo's blues), then Ending_Run
 * for the side `human` (1 P1, 2 P2) against the computer. v_options is
 * put back afterwards. */
/* Ending_Run's steps, then the display off and v_options put back. */
static HzRun preview_step(HzGame *g)
{
    if (ending_run_step(g) == HZ_RUN_FRAME) {
        return HZ_RUN_FRAME;
    }
    front_display_off(g);
    g->options[0] = g->front_run.preview_mode;
    return HZ_RUN_DONE;
}

void game_begin_ending(HzGame *g, int human)
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
    g->front_run.preview_mode = mode;
    ending_run_begin(g);
    game_run_begin(g, preview_step, true);
}
