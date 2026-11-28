#include "game.h"
#include "front.h"
#include "paths.h"

#include <stddef.h>
#include <string.h>

void game_init(HzGame *g, const HzAssets *assets, Video *video)
{
    /* (Session 61) The unit lists, the units' sprites past the VDP's
     * table, the fine grid and the nightmare AI's are kept (an HzGame
     * starts zeroed, then keeps what it allocated) and reset. */
    HzUnitList lists[2];
    memcpy(lists, g->unit_list, sizeof lists);
    HzSpriteMore more = g->sprite_more;
    HzCrowdFine *fine = g->crowd_fine;
    HzNightmare nm[2];
    memcpy(nm, g->nightmare, sizeof nm);
    memset(g, 0, sizeof *g);
    memcpy(g->unit_list, lists, sizeof lists);
    g->sprite_more = more;
    g->sprite_more.count = 0;
    g->crowd_fine = fine;
    for (int p = 0; p < 2; p++) {
        nightmare_reset(&g->nightmare[p], &nm[p]);
    }
    units_reset(g);
    g->assets = assets;
    g->video = video;
    /* Snd_Init at boot: the sfx queue empty. It lives in the $FFF000
     * block, which MainLoop_Title's clear leaves alone. */
    g->sfx_priority = 0xFF;
    g->sfx_queued = 0xFF;
    g->sfx_sent = 0xFF;
    g->save_slot = -1;
}

/* (Session 81, docs/hackable.md step 2 item 5) What the 68k's interrupt
 * handlers, its interrupt mask and the display-off bit come to in a
 * frame: the work at a frame's start (VBlank's), the HUD strip let go
 * part-way down it (HBlank's), that work held while the game is busy
 * (the mask), and a frame not shown (the display off).
 *
 * The HUD strip let go ($4760, HBlank: the HInt counter is 30, so after
 * line 30), once a frame: unless the game has kept it (v_split_done
 * set), the game's sprites, held back from the top of the frame, are
 * drawn from line 32 (the VDP reads sprite links a line ahead; the 68k
 * writes sprite 5's link word back), so the HUD strip's 32 lines never
 * show one. On the 68k it comes part-way through the frame, while the
 * game is busy; here it is decided as the frame is shown, with
 * v_split_done as the game left it. */
static void hud_release(HzGame *g)
{
    if (g->split_done) {
        return;
    }
    video_sprites_back(g->video, VIDEO_HUD_LINES);
    g->split_done++;
}

/* A game frame's start ($4788, VBlank): with the split screen running,
 * the game's sprites held back from the top of the frame (the 68k cuts
 * sprite 5's link, size kept, link 0), for hud_release to let go; the
 * frame counted, saturating. */
static void frame_start(HzGame *g)
{
    if (g->split_enable) {
        video_sprites_hold(g->video, true);
        g->split_done = 0;
    }
    if (g->frame_counter != 0xFFFFFFFFu) {              /* .count */
        g->frame_counter++;
    }
}

/* The work at a frame's start: the game's, or, while the map loads, the
 * loading screen's (load_screen_frame; on the 68k the L6 stub's target,
 * VBlank or VBlank_Loading). */
static void frame_start_work(HzGame *g)
{
    if (g->load_screen_on) {
        load_screen_frame(g);
    } else {
        frame_start(g);
    }
}

/* A frame the game has made (VDP_WaitVBlank, $ABE), in two, game_tick
 * handing it to the host between them. frame_shown: false with the
 * screen off -- the 68k doesn't wait then, and no frame passes; else the
 * HUD strip let go if the game's frame work is on. frame_next, as the
 * next tick begins: the frame done, and the next one's start work -- or,
 * while that work is held (frame_work_on false), owed until it is let go
 * (game_frame_work_on). */
static bool frame_shown(HzGame *g)
{
    if (!g->screen_on) {
        return false;
    }
    if (g->frame_work_on) {
        hud_release(g);
    }
    return true;
}

static void frame_next(HzGame *g)
{
    video_frame_done(g->video);
    if (g->frame_work_on) {
        frame_start_work(g);
    } else {
        g->frame_start_owed = true;
    }
}

void game_run_begin(HzGame *g, HzRun (*step)(HzGame *g), bool menus)
{
    g->tick_step = step;
    g->tick_menus = menus;
    g->tick_resume = false;
}

HzTick game_tick(HzGame *g)
{
    if (g->tick_resume) {
        g->tick_resume = false;
        if (!g->tick_menus) {
            frame_next(g);
        }
    }
    while (g->tick_step != NULL) {
        HzRun run = g->tick_step(g);
        if (run == HZ_RUN_DONE) {
            g->tick_step = NULL;
            break;
        }
        if (run == HZ_RUN_EVENT) {
            return HZ_TICK_EVENT;
        }
        if (g->tick_menus ? front_frame_shown(g) : frame_shown(g)) {
            g->tick_resume = true;
            return HZ_TICK_FRAME;
        }
    }
    return HZ_TICK_DONE;
}

/* The game's frame work let go again (move.w #$2000,sr: interrupts
 * unmasked). A frame start owed while it was held runs now, once however
 * many frames went by: so Game_Start's fade (held) leaves one for the
 * frame loop's start, and v_frame_counter starts at 1. (The HUD strip's
 * release is held the same way, but wherever the 68k unmasks,
 * v_split_done is set and it would do nothing.) */
void game_frame_work_on(HzGame *g)
{
    g->frame_work_on = true;
    if (g->frame_start_owed) {
        g->frame_start_owed = false;
        frame_start_work(g);
    }
}

/* The screen on or off (VDP_DisplayOn, $A7A; VDP_DisplayOff, $A8C): off,
 * the game's waits for a frame make none. */
void game_display_on(HzGame *g)
{
    video_display(g->video, true);
    g->screen_on = true;
}

void game_display_off(HzGame *g)
{
    video_display(g->video, false);
    g->screen_on = false;
}

/* Game_SetViewCameras ($514): which camera each plane's view follows.
 * Split screen: A = camera 1, B = camera 2. Wide screen: the shown
 * player's camera for both. */
void game_set_view_cameras(HzGame *g)
{
    HzPoint *a = &g->camera[0];
    HzPoint *b = &g->camera[1];
    if (!g->split_screen) {
        if ((g->view_cfg[0] >> 8) == 0) {
            b = a;
        } else {
            a = b;                                      /* .camera2 */
        }
    }
    g->view_camera[0] = a;                              /* .store */
    g->view_camera[1] = b;
}

/* Game_SetupMode ($486). In the attract demo (v_game_type negative) the
 * demo list's next entry instead (Demo_SetupNext, demo.c). Else from
 * v_options; mode bits: b0 P2 is CPU, b1 P1 is CPU, b2 wide screen. */
void game_setup_mode(HzGame *g)
{
    if (g->game_type < 0) {
        demo_setup_next(g);
        return;
    }
    g->map = g->options[3] & 7;
    u8 mode = g->options[0] & 7;
    g->split_screen = (u16)(((mode >> 2) + 1) & 1);

    u16 p1 = 0x0000;
    u16 p2 = 0x0101;
    if (mode == 6) {                                    /* wide, P1 CPU */
        p1 = 0xFF00;
        p2 = 0x0001;
    }
    if (mode == 5) {                                    /* .check_mode5 */
        p2 = 0xFF01;
    }
    /* (Session 59) The port's own view for a network game: wide, that
     * side's view alone, the other hidden, as modes 5 and 6 show the
     * human's -- but neither side made the CPU. */
    if (g->own_view == 1 || g->own_view == 2) {
        g->split_screen = 0;
        p1 = (g->own_view == 1) ? 0x0000 : 0xFF00;
        p2 = (g->own_view == 1) ? 0xFF01 : 0x0001;
    }
    g->view_cfg[0] = p1;                                /* .store_views */
    g->view_cfg[1] = p2;
    g->player_flags[0] = (u8)(0x80 | ((mode >> 1) & 1));
    g->player_flags[1] = (u8)(0x90 | (mode & 1));
    g->mech_option2 = g->options[2];

    game_set_view_cameras(g);
}

/* Random ($EA0): seed s (0 counts as $2A6B365A); s x 41 = H:L (32 bits);
 * the new seed is (H + L):L and the result s's high word : (H + L). */
u32 game_random(HzGame *g)
{
    u32 s = g->rng_seed;
    if (s == 0) {
        s = 0x2A6B365Au;
    }
    u32 m = s * 41u;                                    /* .step */
    u16 lo = (u16)m;
    u16 sum = (u16)(lo + (u16)(m >> 16));
    g->rng_seed = ((u32)sum << 16) | lo;
    return (s & 0xFFFF0000u) | sum;
}

/* MainLoop_Title ($41C) up to MenuModule_Title: the music stopped now
 * and every effect, game over cleared, the frame work held (interrupts
 * masked), the split blocked (v_split_done 1, v_hud_sprite_cut 0: the
 * word $100). */
void game_title_prepare(HzGame *g)
{
    video_sprites_behind(g->video, NULL, 0);    /* (session 61) a crowd's */
    video_sprite_colours(g->video, 1, NULL);    /* gone; (session 72) and */
    video_sprite_colours(g->video, 2, NULL);    /* a repair truck's colours */
    snd_fade_music(g, 0);
    snd_stop_sfx(g);
    g->game_over = 0;
    g->frame_work_on = false;                           /* #$2700 */
    g->split_done = 1;
    g->hud_sprite_cut = 0;
    g->split_enable = 0;
}

/* MainLoop_Title after MenuModule_Title: the frame work held, v_game_type
 * (the host's, from game_title_result), Game_SetupMode, VDP_Init,
 * Game_LoadMap; a load aborted from a demo's loading screen goes back to
 * the title (HZ_SETUP_ABORTED). Then .clear_ram: $FF7000-$FFEFFF cleared,
 * every game starting with the $FF8000 block (scroll, cameras, mechs,
 * units, queues, the menu module's RAM) zeroed -- here the fields from
 * sprite_count on; Map_ScanOutposts. Game_Start follows. */
bool game_begin_setup(HzGame *g)
{
    if (!game_setup_begin(g)) {
        g->setup_result = HZ_SETUP_NO_MAP;
        return false;
    }
    game_run_begin(g, game_setup_step, false);
    return true;
}

bool game_setup_begin(HzGame *g)
{
    g->frame_work_on = false;
    game_setup_mode(g);
    screen_start_game(g);
    return game_load_map_begin(g);
}

HzRun game_setup_step(HzGame *g)
{
    if (game_load_map_step(g) == HZ_RUN_FRAME) {
        return HZ_RUN_FRAME;
    }
    if (g->load_abort) {
        g->setup_result = HZ_SETUP_ABORTED;
        return HZ_RUN_DONE;
    }
    size_t from = offsetof(HzGame, sprite_count);
    HzNightmare nm[2];
    memcpy(nm, g->nightmare, sizeof nm);
    memset((u8 *)g + from, 0, sizeof *g - from);       /* .clear_ram */
    for (int p = 0; p < 2; p++) {
        nightmare_reset(&g->nightmare[p], &nm[p]);
    }
    units_reset(g);                 /* (session 61) the units' lists */
    map_scan_outposts(g);
    g->setup_result = HZ_SETUP_GAME;
    return HZ_RUN_DONE;
}

/* The whole of MainLoop_Title but the menus, for the harnesses, which set
 * v_options themselves. The loading screen's frames aren't handed over
 * (quick_load). False for a map past the eight; ready to start when
 * g->setup_result is HZ_SETUP_GAME. */
bool game_begin_new(HzGame *g)
{
    game_title_prepare(g);
    g->quick_load = true;
    return game_begin_setup(g);
}

/* (Session 81, docs/hackable.md step 2) MainLoop_Title ($41C), the
 * game's own sequence: its phases, from the loop's top round to it again
 * (main_step). Each _TELL phase is where the host is told (main_tell)
 * and the next goes on when it ticks again. */
enum {
    MAIN_TOP,
    MAIN_PREPARE,
    MAIN_TITLE_BEGIN,
    MAIN_TITLE,
    MAIN_SETUP_BEGIN,
    MAIN_SETUP,
    MAIN_START_BEGIN,
    MAIN_START,
    MAIN_PASS_BEGIN,
    MAIN_PASS,
    MAIN_OVER_BEGIN,
    MAIN_OVER
};

static HzRun main_tell(HzGame *g, HzEvent event, u8 next)
{
    g->event = (u8)event;
    g->main_phase = next;
    return HZ_RUN_EVENT;
}

/* Back to the loop's top, or (once round) done. */
static bool main_again(HzGame *g)
{
    g->tick_menus = false;
    g->main_phase = MAIN_TOP;
    return !(g->main_flags & HZ_MAIN_ONCE);
}

/* The loop: its top (the host told, then game_title_prepare, v_game_type
 * 0); MenuModule_Title
 * (the menus' frames; -1 in v_game_type when the title ran out, the
 * attract demo); the setup (MainLoop_Title after the menus: the loading
 * screen; a demo's load aborted with Start goes back to the top);
 * Game_Start; the passes, the first entering past ReadJoypads; Game_Over
 * to the title if an HQ fell. A quit ends it at the top, after the title
 * and after the setup; a pass the quit ends ends the game (g->frame_exit
 * HZ_LOOP_QUIT), then the top. */
static HzRun main_step(HzGame *g)
{
    for (;;) {
        switch (g->main_phase) {
            case MAIN_TOP: {
                if (g->quit) {
                    return HZ_RUN_DONE;
                }
                return main_tell(g, HZ_EVENT_TITLE, MAIN_PREPARE);
            }
            case MAIN_PREPARE: {
                game_title_prepare(g);
                g->game_type = 0;
                g->main_phase = MAIN_TITLE_BEGIN;
                break;
            }
            case MAIN_TITLE_BEGIN: {
                if (g->main_flags & HZ_MAIN_NO_TITLE) {
                    g->main_flags &= (u8)~HZ_MAIN_NO_TITLE;
                    return main_tell(g, HZ_EVENT_SETUP, MAIN_SETUP_BEGIN);
                }
                menu_title_flow_begin(g);
                g->tick_menus = true;
                g->main_phase = MAIN_TITLE;
                break;
            }
            case MAIN_TITLE: {
                if (menu_title_flow_step(g) == HZ_RUN_FRAME) {
                    return HZ_RUN_FRAME;
                }
                g->tick_menus = false;
                g->game_type = (s16)game_title_result(g);
                if (g->quit) {
                    return HZ_RUN_DONE;
                }
                return main_tell(g, HZ_EVENT_SETUP, MAIN_SETUP_BEGIN);
            }
            case MAIN_SETUP_BEGIN: {
                if (!game_setup_begin(g)) {
                    g->setup_result = HZ_SETUP_NO_MAP;
                    return HZ_RUN_DONE;
                }
                g->main_phase = MAIN_SETUP;
                break;
            }
            case MAIN_SETUP: {
                if (game_setup_step(g) == HZ_RUN_FRAME) {
                    return HZ_RUN_FRAME;
                }
                if (g->setup_result == HZ_SETUP_ABORTED || g->quit) {
                    if (!main_again(g)) {
                        return HZ_RUN_DONE;
                    }
                    break;
                }
                return main_tell(g, HZ_EVENT_GAME, MAIN_START_BEGIN);
            }
            case MAIN_START_BEGIN: {
                game_start_begin(g);
                g->main_phase = MAIN_START;
                break;
            }
            case MAIN_START: {
                if (game_start_step(g) == HZ_RUN_FRAME) {
                    return HZ_RUN_FRAME;
                }
                game_frame_begin(g, false);
                g->main_phase = MAIN_PASS;
                break;
            }
            case MAIN_PASS_BEGIN: {
                game_frame_begin(g, true);
                g->main_phase = MAIN_PASS;
                break;
            }
            case MAIN_PASS: {
                if (game_frame_step(g) == HZ_RUN_FRAME) {
                    return HZ_RUN_FRAME;
                }
                if (g->frame_exit == HZ_LOOP_ON) {
                    return main_tell(g, HZ_EVENT_PASS, MAIN_PASS_BEGIN);
                }
                return main_tell(g, HZ_EVENT_ENDED, MAIN_OVER_BEGIN);
            }
            case MAIN_OVER_BEGIN: {
                if (g->frame_exit != HZ_LOOP_GAME_OVER) {
                    if (!main_again(g)) {
                        return HZ_RUN_DONE;
                    }
                    break;
                }
                game_over_begin(g);
                g->main_phase = MAIN_OVER;
                break;
            }
            default: {                                  /* MAIN_OVER */
                if (game_over_run_step(g) == HZ_RUN_FRAME) {
                    return HZ_RUN_FRAME;
                }
                if (!main_again(g)) {
                    return HZ_RUN_DONE;
                }
                break;
            }
        }
    }
}

void game_begin_main(HzGame *g, unsigned flags)
{
    g->main_phase = MAIN_TOP;
    g->main_flags = (u8)flags;
    g->event = HZ_EVENT_NONE;
    g->setup_result = HZ_SETUP_GAME;
    game_run_begin(g, main_step, false);
}

/* Game_Start ($634). Clears the per-game counters (of those, the ported
 * ones: v_unit_team_toggle with v_unit_ai_group (one long), v_game_over,
 * v_frame_counter, v_units_built, v_mech_spawns, v_split_enable, both
 * demo streams' places (v_demo_ofs1/2, one long), v_free_ammo;
 * v_dev_heal_mechs has no ported reader). A demo (v_game_type < 0) spawns
 * its recording's units (Demo_Start) and seeds the RNG with 0, so it
 * plays the same each time; a game seeds it from v_rng_seed_saved and
 * gives a CPU player its starting units. (Record modes 1-3, a development
 * leftover, aren't ported: v_record_mode is never set in the shipping
 * game.) Then .screen: v_cpu_boost_hq_hp = $199 + (Random >> 15 & $1FF),
 * and with the screen off and the frame work held the fixed sprites,
 * mechs, views and scroll; the screen on, the fade in (the loading
 * screen's mechs still on screen: the sprite table isn't uploaded until
 * the frame loop's first pass ends), the frame work on (the frame start
 * owed through the fade counts a frame). Game_FrameLoop follows (at Game_FrameLoopEntry,
 * the pad cleared). */
void game_start_begin(HzGame *g)
{
    g->unit_team_toggle = 0;
    g->unit_ai_group = 0;
    g->game_over = 0;
    g->frame_counter = 0;
    g->units_built[0] = 0;
    g->units_built[1] = 0;
    g->mech_spawns[0] = 0;
    g->mech_spawns[1] = 0;
    g->split_enable = 0;
    g->demo_pos[0] = 0;
    g->demo_pos[1] = 0;
    g->free_ammo = 0;
    g->turrets = (g->fixes & HZ_FIX_TURRETS) != 0;      /* (session 60) */
    g->repair = (g->fixes & HZ_FIX_REPAIR) != 0;        /* (session 72) */
    repair_set_colours(g);
    if ((g->fixes & HZ_FIX_PATHS) && g->game_type >= 0) {
        paths_prepare(g);       /* (session 71) PATHS: no field made in play */
    }
    if (repair_on(g) && g->game_type >= 0) {
        paths_prepare_repair(g);    /* (session 72) the truck's ways home */
    }

    if (g->game_type < 0) {
        demo_start(g);
        g->rng_seed = 0;
    } else {                                            /* .not_demo */
        g->rng_seed = g->rng_seed_saved;                /* .new_game */
        units_spawn_cpu_start(g);
    }

    u32 r = game_random(g);                             /* .screen */
    g->cpu_boost_hq_hp = (u16)(((r >> 15) & 0x1FF) + 0x199);
    game_display_off(g);
    g->frame_work_on = false;                           /* #$2700 */
    mechs_init(g);
    map_draw_views(g);
    map_flush_scroll(g);
    game_display_on(g);
    palette_fade_in_map_begin(g);
}

/* Game_Start from Palette_FadeInMap on: its frames, then the frame work on
 * and the pad cleared. */
HzRun game_start_step(HzGame *g)
{
    if (palette_fade_in_map_step(g) == HZ_RUN_FRAME) {
        return HZ_RUN_FRAME;
    }
    game_frame_work_on(g);                              /* #$2000 */
    g->joypad[0] = 0;                                   /* clr.w v_joypad1 */
    g->joypad[1] = 0;
    return HZ_RUN_DONE;
}

void game_begin_start(HzGame *g)
{
    game_start_begin(g);
    game_run_begin(g, game_start_step, false);
}

/* The pause's phases (game_pause_step): its first frame; Start still
 * held, pressed again, let go; the last VBlank. */
enum {
    PAUSE_FIRST,
    PAUSE_RELEASE,
    PAUSE_PRESS,
    PAUSE_UNPAUSE,
    PAUSE_LAST
};

/* The frame loop's pass's phases (game_frame_step): its start (the pads,
 * Game_CheckPause), a pause, after its VDP_WaitVBlank. And for
 * game_begin_frame_end, the pass from its VDP_WaitVBlank on; for
 * game_begin_wait, a VDP_WaitVBlank alone, then done. */
enum {
    FRAME_ENTRY,
    FRAME_PAUSED,
    FRAME_END,
    FRAME_WAIT,
    FRAME_WAITED
};

/* A 3-button pad's data lines (bits 5-0, active low) with TH, the select
 * line, as the 68k last wrote it (bit 6 reads it back): TH 1 gives C B R L
 * D U; TH 0 gives Start A, two lines held low (how a 3-button pad makes
 * itself known), D U. */
static u8 pad_port_read(const HzPad *p, bool th)
{
    u8 low = 0;
    if (p->up) {
        low |= 0x01;
    }
    if (p->down) {
        low |= 0x02;
    }
    if (th) {
        if (p->left) {
            low |= 0x04;
        }
        if (p->right) {
            low |= 0x08;
        }
        if (p->b) {
            low |= 0x10;
        }
        if (p->c) {
            low |= 0x20;
        }
    } else {
        low |= 0x0C;
        if (p->a) {
            low |= 0x10;
        }
        if (p->start) {
            low |= 0x20;
        }
    }
    return (u8)((th ? 0x40 : 0x00) | (~low & 0x3F));
}

/* ReadJoypads ($B30), both ports the same way: TH 1, C B R L D U from the
 * low six bits; TH 0, Start A from bits 5 and 4, shifted up to 7 and 6;
 * inverted, so a pressed button is a 1: S A C B R L D U. (The 68k holds
 * the Z80 off the bus meanwhile, .wait_bus; the port has no Z80.) */
void game_read_joypads(HzGame *g)
{
    for (int i = 0; i < 2; i++) {
        u8 d1 = (u8)(pad_port_read(&g->pad_in[i], true) & 0x3F);
        u8 d0 = (u8)(pad_port_read(&g->pad_in[i], false) & 0x30);
        d0 = (u8)(d0 << 2);
        g->joypad[i] = (u8)~(d0 | d1);
    }
}

/* Pause_Frame ($44A2): a frame without updating the game. The VBlank,
 * then with HBlank's relink blocked and the display off the sprite
 * upload (on the cartridge the multiplexer first, so the flicker went on;
 * session 81: every sprite drawn), display on, split
 * re-enabled, the pads read. Returns them OR'd. Unlike the main loop it
 * switches the display with VDP_DisplayOff/On, which keep v_vdp_reg1.
 * Here from after the VBlank: the frame is game_pause_step's. */
static u8 pause_frame(HzGame *g)
{
    g->split_enable = 0;
    g->split_done = 1;
    game_display_off(g);
    sprites_upload(g);
    game_display_on(g);
    g->split_enable = 1;
    g->split_done = 0;
    game_read_joypads(g);
    return (u8)(g->joypad[0] | g->joypad[1]);
}

/* Game_CheckPause ($4400), each pass after ReadJoypads. Start on either
 * pad pauses: Snd_Pause with the pads, Sprites_Build once, then frames of
 * Pause_Frame until Start is let go, until it is pressed again, and until
 * it is let go again; Snd_Pause, one more VBlank, and v_frame_counter put
 * back as it was, so a pause doesn't count towards the 40 minutes of
 * Game_SecondTick or the demo's 60 s. As Start is pressed to unpause,
 * with no CPU mech and all eight buttons held across the two pads (the
 * pads OR'd: $FF), v_free_ammo toggles: a cheat for two players.
 *
 * In a demo, Start on either pad ends it (v_frame_counter_lo = $FF00,
 * past Game_SecondTick's $E10); otherwise Demo_PlayInput feeds the
 * recorded pads. v_record_mode 1-3 would run the development recorder
 * instead (not ported: never set). The pause loops also end on the
 * host's quit, the port's own.
 *
 * (Session 80) Up to the pause here; true when there is one, and then
 * game_pause_step has its frames. */
bool game_check_pause_begin(HzGame *g)
{
    u8 pads = (u8)(g->joypad[0] | g->joypad[1]);
    if (g->game_type < 0) {
        if (pads & HZ_PAD_START) {                      /* .demo */
            g->frame_counter = (g->frame_counter & 0xFFFF0000u) | 0xFF00u;
        } else {
            demo_play_input(g);
        }
        return false;
    }
    if (!(pads & HZ_PAD_START)) {
        return false;                                   /* .done */
    }

    g->pause_saved_frames = g->frame_counter;
    snd_pause(g, pads);
    sprites_build(g);
    g->pause_phase = PAUSE_FIRST;
    return true;
}

/* The pause, a frame a step: each loop's Pause_Frame is a frame, then its
 * work (pause_frame) and the loop's test; the last VBlank, then
 * v_frame_counter put back. Quit ends it after .wait_press's frame, as
 * there, with the counter left. */
HzRun game_pause_step(HzGame *g)
{
    u8 d0;
    switch (g->pause_phase) {
        case PAUSE_FIRST: {
            g->pause_phase = PAUSE_RELEASE;
            return HZ_RUN_FRAME;
        }
        case PAUSE_RELEASE: {                           /* .wait_release */
            d0 = pause_frame(g);
            if (!(d0 & HZ_PAD_START) || g->quit) {
                g->pause_phase = PAUSE_PRESS;
            }
            return HZ_RUN_FRAME;
        }
        case PAUSE_PRESS: {                             /* .wait_press */
            d0 = pause_frame(g);
            if (!(d0 & HZ_PAD_START) && !g->quit) {
                return HZ_RUN_FRAME;
            }
            if (g->quit) {
                return HZ_RUN_DONE;
            }
            if (!((g->mech[0].flags | g->mech[1].flags) & MECH_CPU)
                && d0 == 0xFF) {
                g->free_ammo = (u16)((g->free_ammo ^ 1) & 1);
            }
            g->pause_phase = PAUSE_UNPAUSE;
            return HZ_RUN_FRAME;
        }
        case PAUSE_UNPAUSE: {                           /* .wait_unpause */
            d0 = pause_frame(g);
            if ((d0 & HZ_PAD_START) && !g->quit) {
                return HZ_RUN_FRAME;
            }
            snd_pause(g, d0);
            g->pause_phase = PAUSE_LAST;
            return HZ_RUN_FRAME;
        }
        default: {                                      /* PAUSE_LAST */
            g->frame_counter = g->pause_saved_frames;
            return HZ_RUN_DONE;
        }
    }
}

/* HQ p's HP: the word at v_hq + p x 8 + 2. */
static u16 hq_hp(const HzGame *g, int p)
{
    const HzPost *hq = &g->posts[HZ_POST_HQ + p];
    return post_hq_hp(hq);
}

/* One side's income (.timer_ok for P1, .p2_income for P2): (outposts + 1)
 * x 4, doubled in a demo, added to mech_money and capped at 50000 (d1).
 * v_record_mode 1 would set 50000 (not ported: never set). */
static void second_income(HzGame *g, int p)
{
    u16 income = (u16)((g->outpost_count[p] + 1) << 2);
    if (g->game_type < 0) {
        income = (u16)(income + income);
    }
    u16 money = (u16)(income + g->mech[p].money);       /* .p1_add_income */
    if (money > 50000) {
        money = 50000;
    }
    g->mech[p].money = money;                           /* .p1_store_money */
}

/* Game_SecondTick ($808), each pass. v_second_timer counts 0..$3B; P1 is
 * paid at $3A and P2 at $3B. An HQ at 0 HP ends the game: P1's is looked
 * at first, so if both fall in one frame P2 wins; the 68k drops its
 * return address, masks interrupts and jumps to Game_Over. A demo goes
 * back to the title once v_frame_counter_lo reaches $E10 (60 s); a game
 * saves v_rng_seed into v_rng_seed_saved every frame, which seeds the
 * next game. Then, except on the second's first frame (timer 0), with
 * exactly one CPU mech: v_cpu_boost is on ($8000 when P1 is the CPU,
 * $8010 when P2 is) while the CPU's HQ is under v_cpu_boost_hq_hp, the
 * human holds 5 or more outposts or has 25 or more units, or the game is
 * 40 minutes old ($23280 frames); off otherwise. */
HzLoopExit game_second_tick(HzGame *g)
{
    u16 t = (u16)(g->second_timer + 1);
    if (t >= 0x3C) {
        t = 0;
    }
    g->second_timer = t;                                /* .timer_ok */
    if (t == 0x3B) {
        second_income(g, 1);                            /* .p2_income */
    } else if (t == 0x3A) {
        second_income(g, 0);
    }

    if (hq_hp(g, 0) == 0) {                             /* .check_hqs */
        g->frame_work_on = false;                       /* .p2_wins */
        g->winner = 1;
        /* FIX (HZ_FIX_DRAW, network games): P2's HQ down too, a draw */
        if ((g->fixes & HZ_FIX_DRAW) && hq_hp(g, 1) == 0) {
            g->winner = HZ_DRAW;
        }
        return HZ_LOOP_GAME_OVER;
    }
    if (hq_hp(g, 1) == 0) {
        g->frame_work_on = false;                       /* .p1_wins */
        g->winner = 0;
        return HZ_LOOP_GAME_OVER;
    }
    if (g->game_type < 0) {                             /* .hqs_standing */
        if ((u16)g->frame_counter >= 0xE10) {
            return HZ_LOOP_TITLE;
        }
    } else {
        g->rng_seed_saved = g->rng_seed;                /* .save_seed */
    }

    if (g->second_timer == 0) {                         /* .check_boost */
        return HZ_LOOP_ON;                              /* .done */
    }
    u8 f1 = g->mech[0].flags;
    u8 f2 = g->mech[1].flags;
    if (!((f1 ^ f2) & MECH_CPU)) {
        g->cpu_boost = 0;                               /* .boost_off */
        return HZ_LOOP_ON;
    }
    int cpu = (f1 & MECH_CPU) ? 0 : 1;                  /* else .p2_is_cpu */
    int human = 1 - cpu;
    bool ahead = hq_hp(g, cpu) < g->cpu_boost_hq_hp
              || g->outpost_count[human] >= 5
              || g->unit_count[human] >= 0x19
              || g->frame_counter >= 0x23280u;
    if (!ahead) {
        g->cpu_boost = 0;                               /* .boost_off */
    } else {
        g->cpu_boost = (cpu == 0) ? 0x8000 : 0x8010;    /* .boost_p1_cpu */
    }
    return HZ_LOOP_ON;
}

/* Game_FrameLoop ($700), one pass: ReadJoypads (Game_Start's first pass
 * enters past it, at Game_FrameLoopEntry), Game_CheckPause,
 * Game_SecondTick (which may end the loop), Mechs_Update, then
 * UnitAI_Update and Combat_Update (skipped in v_record_mode 1/3: never
 * set; with them skipped v_unit_ai_group would stay put, and at 0 the
 * map would animate every frame), then game_frame_draw (HUD_UpdateGauges
 * on), VDP_WaitVBlank and frame_uploads. Not ported yet: UnitAI_Update's
 * units (its group step is). */
static void frame_uploads(HzGame *g);

void game_frame_begin(HzGame *g, bool read_pads)
{
    g->frame_phase = FRAME_ENTRY;
    g->frame_read_pads = read_pads;
}

/* The pass after Game_CheckPause, up to its VDP_WaitVBlank (a frame), or
 * to its end where it ends early. */
static HzRun frame_logic(HzGame *g)
{
    if (g->quit) {
        g->frame_exit = HZ_LOOP_QUIT;
        return HZ_RUN_DONE;
    }
    if (g->fixes & HZ_FIX_TURRETS) {
        g->turrets = true;      /* (session 60) on once: on this game */
    }
    if ((g->fixes & HZ_FIX_REPAIR) && !g->repair) {
        g->repair = true;       /* (session 72) likewise */
        repair_set_colours(g);
    }
    HzLoopExit exit = game_second_tick(g);
    if (exit != HZ_LOOP_ON) {
        g->frame_exit = (u8)exit;
        return HZ_RUN_DONE;
    }
    mechs_update(g);
    unit_ai_update(g);
    if (g->combat_probe != NULL) {
        g->combat_probe(g, false, g->combat_probe_user);
    }
    combat_update(g);
    if (g->combat_probe != NULL) {
        g->combat_probe(g, true, g->combat_probe_user);
    }
    game_frame_draw(g);
    g->frame_phase = FRAME_END;
    return HZ_RUN_FRAME;
}

/* A pause's frames, then the rest of the pass. */
static HzRun frame_paused(HzGame *g)
{
    if (game_pause_step(g) == HZ_RUN_FRAME) {
        return HZ_RUN_FRAME;
    }
    return frame_logic(g);
}

HzRun game_frame_step(HzGame *g)
{
    switch (g->frame_phase) {
        case FRAME_WAIT: {
            g->frame_phase = FRAME_END;
            return HZ_RUN_FRAME;
        }
        case FRAME_WAITED: {
            return HZ_RUN_DONE;
        }
        case FRAME_ENTRY: {
            grid_sync(g);   /* (session 61) UNITS on or off: crowd.c */
            if (g->frame_read_pads) {
                game_read_joypads(g);
            }
            if (!game_check_pause_begin(g)) {           /* ...Entry */
                return frame_logic(g);
            }
            g->frame_phase = FRAME_PAUSED;
            return frame_paused(g);
        }
        case FRAME_PAUSED: {
            return frame_paused(g);
        }
        default: {                                      /* FRAME_END */
            frame_uploads(g);
            g->frame_exit = (u8)(g->quit ? HZ_LOOP_QUIT : HZ_LOOP_ON);
            return HZ_RUN_DONE;
        }
    }
}

void game_begin_pass(HzGame *g, bool read_pads)
{
    game_frame_begin(g, read_pads);
    game_run_begin(g, game_frame_step, false);
}

/* Game_FrameLoop ($700) from HUD_UpdateGauges to VDP_WaitVBlank. */
void game_frame_draw(HzGame *g)
{
    hud_update_gauges(g);
    map_anim_tiles_update(g);
    sprites_build(g);
    map_scroll_update(g);
}

/* Game_FrameLoop ($700) from VDP_WaitVBlank on. The finished frame goes
 * to the host (and VBlank runs); then, with HBlank's relink blocked
 * (v_split_enable 0, v_split_done 1) and the display off, the VRAM
 * uploads; display on, split re-enabled, sound effects out. The display
 * register is written straight to the VDP here, so v_vdp_reg1 keeps its
 * $8164. Not ported yet: the v_dev_heal_mechs tail (never set). */
void game_begin_frame_end(HzGame *g)
{
    g->frame_phase = FRAME_WAIT;
    game_run_begin(g, game_frame_step, false);
}

/* VDP_WaitVBlank alone: its frame, then done. */
static HzRun wait_step(HzGame *g)
{
    if (g->frame_phase == FRAME_WAITED) {
        return HZ_RUN_DONE;
    }
    g->frame_phase = FRAME_WAITED;
    return HZ_RUN_FRAME;
}

void game_begin_wait(HzGame *g)
{
    g->frame_phase = FRAME_ENTRY;
    game_run_begin(g, wait_step, false);
}

/* Game_FrameLoop after its VDP_WaitVBlank. */
static void frame_uploads(HzGame *g)
{
    g->split_enable = 0;
    g->split_done = 1;
    video_display(g->video, false);
    sprites_upload(g);
    map_flush_scroll(g);
    order_menu_update(g);                               /* OrderMenu_UpdateBoth */
    map_anim_tiles_upload(g);
    map_fx_update(g);
    hud_flush_bars(g);
    video_display(g->video, true);
    g->split_enable = 1;
    g->split_done = 0;
    snd_flush_sfx(g);
}
