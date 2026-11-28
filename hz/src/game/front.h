#ifndef HZ_FRONT_H
#define HZ_FRONT_H

/* The front end: the $53000 module's title and options screen (title.c,
 * options.c) and the helpers they share (front.c). The menus have a screen
 * of their own (M_VDPInit; the draw layer's VIDEO_MENUS, video.h): the
 * window layer over the whole screen, with layer B behind its transparent
 * cells; no interrupts. They draw through the same draw layer as the game,
 * and hand each frame to the host where the 68k waits for VBlank
 * (M_WaitVBlank and its kin poll the VDP's status; the port calls the
 * frame hook), so the host's frame hook, pads and quit work as in the
 * game.
 *
 * The state lives in HzGame.front (game.h: the menu module's RAM,
 * $FF9800-$FF9A2F, which MainLoop_Title's per-game clear zeroes); what
 * survives a game (v_options, the win record) in HzGame's
 * $FFF800 block. */

#include "game.h"
#include "saves.h"

/* The menu font's sets, as tile bases added to a character (tile =
 * character - $30 + base): */
#define FONT_PLAIN      0x00    /* white, and the frame pieces */
#define FONT_HIGHLIGHT  0x50    /* the cursor's: red on black */
#define FONT_WON        0xA0    /* grey: maps and difficulties won */
#define FONT_TITLE      0xD0    /* CONFIGURATION MODE, PASSWORD */

/* The frame pieces and the background, as characters of the font. */
#define BOX_TOP_LEFT     '|'
#define BOX_TOP          '\\'
#define BOX_TOP_RIGHT    '~'
#define BOX_LEFT         ']'
#define BOX_RIGHT        '^'
#define BOX_DIVIDE_LEFT  '='
#define BOX_DIVIDE       '>'
#define BOX_DIVIDE_RIGHT '?'
#define BOX_BOTTOM_LEFT  '}'
#define BOX_BOTTOM       '_'
#define BOX_BOTTOM_RIGHT 0x7F
#define MENU_BLANK       '@'    /* the font's space; ' ' in the port's
                                   strings stands for it */
#define MENU_BACKGROUND  '`'    /* tile $30, the options' background */

/* The pad bits as M_ReadJoypads makes them (both pads OR'd, the HZ_PAD_*
 * bits; the word's high byte is always $FF). */
#define FRONT_PADS       0x00FF

/* -- front.c: the module's helpers ($56490-$573FF) ---------------------- */

void front_init(HzGame *g);                 /* M_Init (M_VDPInit) */
void front_display_on(HzGame *g);           /* M_DisplayOn */
void front_display_off(HzGame *g);          /* M_DisplayOff */
void front_reset_scroll(HzGame *g);         /* M_ResetScroll */
void front_clear_sprites(HzGame *g);        /* M_ClearSprites */
void front_apply_scroll(HzGame *g);         /* M_ApplyScroll: plane B */
void front_blank_planes(HzGame *g);         /* M_BlankPlanes */

/* What the menus draw (src/core/video.h, the menus' layout: layer B the
 * picture behind, the window layer over the whole screen in front):
 * tiles' pictures from tile `first` on; a cell (a column past the row's
 * end runs on into the next row, as the 68k's writes run on through
 * VRAM); a picture of consecutive tiles, `rows` rows of `across` cells
 * from `first` (with its palette and priority bits); `rows` rows of a
 * layer filled with one cell. */
void front_tiles(HzGame *g, u16 first, const u8 *src, u32 bytes);
void front_put(HzGame *g, LayerId layer, int col, int row, u16 word);
void front_picture(HzGame *g, LayerId layer, int col, int row, u16 first,
                   int across, int rows);
void front_fill(HzGame *g, LayerId layer, int row, int rows, u16 word);

u16  front_read_pads(HzGame *g);            /* M_ReadJoypads: new presses */

/* (Session 80, docs/hackable.md step 2) The menus' sequences a frame at a
 * time, as the game's (HzRun): a step returns HZ_RUN_FRAME where the 68k
 * calls M_WaitVBlank or its kin (M_WaitFramePad: the frame, then
 * front_read_pads), and game_tick hands the frame to the host if the
 * display is on (front_frame_shown). M_WaitFrames (n + 1 frames) and the
 * fades so, and M_ClearPlanes in two: its frame, then
 * front_clear_planes_after. */
void  front_wait_frames_begin(HzGame *g, int n);
HzRun front_wait_frames_step(HzGame *g);
void  front_fade_to_black_begin(HzGame *g);
void  front_fade_from_black_begin(HzGame *g);
HzRun front_fade_step(HzGame *g);
void  front_clear_planes_after(HzGame *g);

/* Text in the window plane, in the menu font: ' ' stands for the font's
 * blank ('@'). */
void front_text_palette(HzGame *g, int line);   /* M_SetAttrInline */
void front_text_font(HzGame *g, u16 base);      /* M_SetTileBaseInline */
void front_cursor(HzGame *g, int x, int y);
void front_print(HzGame *g, const char *text);  /* at the cursor */
void front_print_at(HzGame *g, int x, int y, const char *text);
                                                /* M_PrintInlineAt */
void front_print_codes(HzGame *g, const u8 *codes, int n);
void front_print_2digits(HzGame *g, int n);     /* M_Print2DigitsAtCursor */
/* A line of a frame: `left`, `fill` x n, `right`, from (x, y). */
void front_box_line(HzGame *g, int x, int y, char left, char fill,
                    char right, int n);
/* A frame `w` cells wide inside, `h` rows between its top and bottom. */
void front_box(HzGame *g, int x, int y, int w, int h);

void front_upload_lines(HzGame *g, unsigned mask);  /* M_UploadPaletteLines */
void front_upload_palette(HzGame *g);       /* M_UploadPalette */
void front_clear_palette(HzGame *g);        /* M_ClearPalette */
void front_set_target(HzGame *g);           /* M_PaletteSetTarget */
void front_step_to_target(HzGame *g);       /* M_PaletteStepToTarget */
void front_apply_fade(HzGame *g);           /* M_PaletteApplyFade */
void front_play_sfx(HzGame *g, u8 sfx);     /* M_Snd_PlaySfxNow + FlushSfx */

/* -- title.c, options.c ------------------------------------------------ */

/* The options screen's pages, as HzGame.front_probe sees them. */
typedef enum {
    PAGE_SETUP,         /* Options_Run */
    PAGE_MENU,          /* Options_MainMenu */
    PAGE_P1,            /* Options_P1Side */
    PAGE_P2,            /* Options_P2Side */
    PAGE_WINDOW,        /* Options_CheckWindow */
    PAGE_SAVES,         /* CONT: the saved games (the 68k's Password_Run) */
    PAGE_MUSIC,         /* MusicTest_Run */
    PAGE_SOUND,         /* SoundTest_Run */
    PAGE_MAP_SELECT,    /* MapSelect_Run */
    PAGE_MAP,           /* MapSelect_Redraw */
    PAGE_TYPE,          /* TypeSelect_Run */
    PAGE_DONE           /* back to MainLoop_Title: the game */
} HzFrontPage;

/* Title_Run (0, or -1), Options_Run and Menu_TitleFlow, a frame at a
 * time: begun, stepped until done; the title's end in
 * g->front_run.title_result, the flow's in .flow_result
 * (game_title_result). */
void  title_run_begin(HzGame *g);
HzRun title_run_step(HzGame *g);
void  options_run_begin(HzGame *g);
HzRun options_run_step(HzGame *g);
void  menu_title_flow_begin(HzGame *g);
HzRun menu_title_flow_step(HzGame *g);

/* A saved game loaded as CONT loads it (what an accepted password did in
 * Password_Check): the record, the sides from the side the human played
 * (the window kept), v_option_map the first map not won on every
 * difficulty. */
void options_load_save(HzGame *g, const HzSaveSlot *slot, int index);

#endif /* HZ_FRONT_H */
