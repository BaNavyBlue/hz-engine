#include "game.h"
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

/* HBlank ($4760), once per frame: the HInt counter is 30, so the first
 * interrupt comes after line 30. Unless the main loop has blocked it
 * (v_split_done set), it writes sprite 5's link word back: the game's
 * sprites, held back from the top of the frame, are drawn from line 32
 * (the VDP reads sprite links a line ahead), so the HUD strip's 32 lines
 * never show one.
 *
 * The 68k's handler runs part-way through the frame, while the main loop
 * is busy; the port decides it when the frame is handed over, with
 * v_split_done as the main loop left it. */
static void hblank(HzGame *g)
{
    if (g->split_done) {
        return;
    }
    video_sprites_back(g->video, VIDEO_HUD_LINES);
    g->split_done++;
}

/* VBlank ($4788): with the split screen running, sprite 5's link cut (size
 * kept, link 0): the game's sprites held back from the top of the next
 * frame, for HBlank to let back; count the frame, saturating. */
static void vblank(HzGame *g)
{
    if (g->split_enable) {
        video_sprites_hold(g->video, true);
        g->split_done = 0;
    }
    if (g->frame_counter != 0xFFFFFFFFu) {              /* .count */
        g->frame_counter++;
    }
}

/* The L6 stub's target: VBlank, or VBlank_Loading while the map loads. */
static void vblank_handler(HzGame *g)
{
    if (g->vblank_loading) {
        vblank_loading(g);
    } else {
        vblank(g);
    }
}

/* VDP_WaitVBlank ($ABE). With the display off it returns at once: the 68k
 * doesn't wait, and the port lets no frame pass. Otherwise the frame that
 * was on screen is complete: hand it over, then run the VBlank handler,
 * which on the 68k fires as the wait ends. With interrupts masked the VDP
 * holds the interrupt until they're unmasked (game_unmask). */
void game_wait_vblank(HzGame *g)
{
    if (!(g->vdp_reg1 & 0x40)) {
        return;
    }
    if (g->interrupts_on) {
        hblank(g);
    }
    if (g->frame != NULL) {
        g->frame(g, g->frame_user);
    }
    video_frame_done(g->video);
    if (g->interrupts_on) {
        vblank_handler(g);
    } else {
        g->vblank_pending = true;
    }
}

/* move.w #$2000,sr: interrupts unmasked. A VBlank the VDP held while they
 * were masked is taken at once: so Game_Start's fade (masked) leaves one
 * for the frame loop's start, and v_frame_counter starts at 1. (HBlank's
 * is held the same way, but wherever the 68k unmasks, v_split_done is
 * set and HBlank would do nothing.) */
void game_unmask(HzGame *g)
{
    g->interrupts_on = true;
    if (g->vblank_pending) {
        g->vblank_pending = false;
        vblank_handler(g);
    }
}

/* VDP_DisplayOn ($A7A) / VDP_DisplayOff ($A8C) */
void game_display_on(HzGame *g)
{
    video_display(g->video, true);
    g->vdp_reg1 = 0x8164;
}

void game_display_off(HzGame *g)
{
    video_display(g->video, false);
    g->vdp_reg1 = 0x8124;
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
 * and every effect, game over cleared, interrupts masked, the split
 * blocked (v_split_done 1, v_hud_sprite_cut 0: the word $100). */
void game_title_prepare(HzGame *g)
{
    video_sprites_behind(g->video, NULL, 0);    /* (session 61) a crowd's */
    video_sprite_colours(g->video, 1, NULL);    /* gone; (session 72) and */
    video_sprite_colours(g->video, 2, NULL);    /* a repair truck's colours */
    snd_fade_music(g, 0);
    snd_stop_sfx(g);
    g->game_over = 0;
    g->interrupts_on = false;                           /* #$2700 */
    g->split_done = 1;
    g->hud_sprite_cut = 0;
    g->split_enable = 0;
}

/* MainLoop_Title after MenuModule_Title: interrupts masked, v_game_type
 * (the host's, from menu_title_flow), Game_SetupMode, VDP_Init,
 * Game_LoadMap; a load aborted from a demo's loading screen goes back to
 * the title (HZ_SETUP_ABORTED). Then .clear_ram: $FF7000-$FFEFFF cleared,
 * every game starting with the $FF8000 block (scroll, cameras, mechs,
 * units, queues, the menu module's RAM) zeroed -- here the fields from
 * sprite_count on; Map_ScanOutposts. Game_Start follows. */
HzSetup game_setup_after_title(HzGame *g)
{
    g->interrupts_on = false;
    game_setup_mode(g);
    screen_start_game(g);
    if (!game_load_map(g)) {
        return HZ_SETUP_NO_MAP;
    }
    if (g->load_abort) {
        return HZ_SETUP_ABORTED;
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
    return HZ_SETUP_GAME;
}

/* The whole of MainLoop_Title but the menus, for the harnesses, which set
 * v_options themselves. The loading screen's frames aren't handed over
 * (quick_load). True when a game is ready to start. */
bool game_new(HzGame *g)
{
    game_title_prepare(g);
    g->quick_load = true;
    return game_setup_after_title(g) == HZ_SETUP_GAME;
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
 * and with the display off and interrupts masked the fixed sprites,
 * mechs, views and scroll; display on, the fade in (the loading screen's
 * mechs still on screen: the sprite table isn't uploaded until the frame
 * loop's first pass ends), interrupts on (the VBlank held through the
 * fade counts a frame). Game_FrameLoop follows (at Game_FrameLoopEntry,
 * the pad cleared). */
void game_start(HzGame *g)
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
    g->interrupts_on = false;                           /* #$2700 */
    sprites_init_fixed(g);
    mechs_init(g);
    map_draw_views(g);
    map_flush_scroll(g);
    game_display_on(g);
    palette_fade_in_map(g);
    game_unmask(g);                                     /* #$2000 */
    g->joypad[0] = 0;                                   /* clr.w v_joypad1 */
    g->joypad[1] = 0;
}

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
 * then with HBlank's relink blocked and the display off the multiplexer
 * and the sprite upload (so the flicker goes on), display on, split
 * re-enabled, the pads read. Returns them OR'd. Unlike the main loop it
 * switches the display with VDP_DisplayOff/On, which keep v_vdp_reg1. */
static u8 pause_frame(HzGame *g)
{
    game_wait_vblank(g);
    g->split_enable = 0;
    g->split_done = 1;
    game_display_off(g);
    sprites_multiplex(g);
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
 * host's quit, the port's own. */
void game_check_pause(HzGame *g)
{
    u8 pads = (u8)(g->joypad[0] | g->joypad[1]);
    if (g->game_type < 0) {
        if (pads & HZ_PAD_START) {                      /* .demo */
            g->frame_counter = (g->frame_counter & 0xFFFF0000u) | 0xFF00u;
        } else {
            demo_play_input(g);
        }
        return;
    }
    if (!(pads & HZ_PAD_START)) {
        return;                                         /* .done */
    }

    u32 saved = g->frame_counter;
    snd_pause(g, pads);
    sprites_build(g);
    u8 d0;
    do {
        d0 = pause_frame(g);                            /* .wait_release */
    } while ((d0 & HZ_PAD_START) && !g->quit);
    do {
        d0 = pause_frame(g);                            /* .wait_press */
    } while (!(d0 & HZ_PAD_START) && !g->quit);
    if (g->quit) {
        return;
    }
    if (!((g->mech[0].flags | g->mech[1].flags) & MECH_CPU) && d0 == 0xFF) {
        g->free_ammo = (u16)((g->free_ammo ^ 1) & 1);
    }
    do {
        d0 = pause_frame(g);                            /* .wait_unpause */
    } while ((d0 & HZ_PAD_START) && !g->quit);
    snd_pause(g, d0);
    game_wait_vblank(g);
    g->frame_counter = saved;
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
        g->interrupts_on = false;                       /* .p2_wins */
        g->winner = 1;
        /* FIX (HZ_FIX_DRAW, network games): P2's HQ down too, a draw */
        if ((g->fixes & HZ_FIX_DRAW) && hq_hp(g, 1) == 0) {
            g->winner = HZ_DRAW;
        }
        return HZ_LOOP_GAME_OVER;
    }
    if (hq_hp(g, 1) == 0) {
        g->interrupts_on = false;                       /* .p1_wins */
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
 * on) and game_frame_end. Not ported yet: UnitAI_Update's units (its
 * group step is). */
HzLoopExit game_frame(HzGame *g, bool read_pads)
{
    grid_sync(g);           /* (session 61) UNITS on or off: crowd.c */
    if (read_pads) {
        game_read_joypads(g);
    }
    game_check_pause(g);                                /* ...Entry */
    if (g->quit) {
        return HZ_LOOP_QUIT;
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
        return exit;
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
    game_frame_end(g);
    return g->quit ? HZ_LOOP_QUIT : HZ_LOOP_ON;
}

/* Game_FrameLoop ($700) from HUD_UpdateGauges to VDP_WaitVBlank. */
void game_frame_draw(HzGame *g)
{
    hud_update_gauges(g);
    map_anim_tiles_update(g);
    sprites_build(g);
    map_scroll_update(g);
    sprites_multiplex(g);
}

/* Game_FrameLoop ($700) from VDP_WaitVBlank on. The finished frame goes
 * to the host (and VBlank runs); then, with HBlank's relink blocked
 * (v_split_enable 0, v_split_done 1) and the display off, the VRAM
 * uploads; display on, split re-enabled, sound effects out. The display
 * register is written straight to the VDP here, so v_vdp_reg1 keeps its
 * $8164. Not ported yet: the v_dev_heal_mechs tail (never set). */
void game_frame_end(HzGame *g)
{
    game_wait_vblank(g);
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
