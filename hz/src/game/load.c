#include "game.h"

#include "tables.h"

#include <string.h>

/* The tiles each part of the game's art goes to. */
#define TILE_MECH1   0x200          /* P1's mech frame: 16 tiles */
#define TILE_MECH2   0x210          /* P2's */
#define TILE_SPRITES 0x240          /* SpriteTiles: $240-$4FF */
#define TILE_HUD     0x500          /* HUDTiles: $500-$5FF */
#define TILE_FONT    0x6B1          /* FontTiles: $6B1-$6FF */

static void load_tiles(HzGame *g, u16 first, const u8 *src, u32 bytes)
{
    video_load_tiles(g->video, first, src, bytes / 32);
}

/* LoadScreen_UploadSprites ($8F04): the two loading-screen entries, the
 * frame's sprites (on the cartridge 6 and 7 of the table, after the
 * divider's six). */
static void load_upload_sprites(HzGame *g)
{
    video_sprites_begin(g->video);
    video_sprites_game(g->video);
    for (int i = 0; i < 2; i++) {
        sprites_show_one(g, &g->load_sprites[i]);
    }
}

/* LoadScreen_UploadMech ($8F2A): MechFrames frame load_mech_frame (& $7F;
 * no frame past $3F is ever asked for, so the assets' & $3F), 16 tiles,
 * into P1's mech tiles and P2's, with the screen off. */
static void load_upload_mech(HzGame *g)
{
    u16 f = (u16)(g->load_mech_frame & 0x7F);
    const u8 *src = g->assets->mech_frames[f & (HZ_MECH_FRAMES - 1)];
    video_display(g->video, false);
    load_tiles(g, TILE_MECH1, src, HZ_MECH_FRAME_BYTES);
    load_tiles(g, TILE_MECH2, src, HZ_MECH_FRAME_BYTES);
}

/* LoadScreen_Start ($8E7A), from Game_LoadMap with the display off and
 * the planes drawn (Screen_InitGame links the divider's sprite 5 to 6).
 * The loading screen: each player's mech as a 4 x 4 sprite just below the
 * screen (y $168), P1's sprite 6 (tile $200, line 2), P2's sprite 7 (tile
 * $210, line 3), both with priority; in split screen at the middle of its
 * view (x $B8, $168), in wide screen the shown player's at the middle (x
 * $110) and the other's off the left edge (x 1). The walk at step 0 with
 * its delay 6, 207 frames to go, frame 0 for both; the loading screen's
 * step as each frame's start work (VBlank_Loading as the VBlank handler);
 * the frame work on (a frame start owed while it was held runs at once:
 * the screen's first frame shows its first step). */
void load_screen_start(HzGame *g)
{
    g->frame_work_on = false;                           /* #$2700 */
    u16 x1 = 0x00B8;
    u16 x2 = 0x0168;
    if (!g->split_screen) {                             /* .wide */
        x1 = 0x0110;
        x2 = 0x0001;
        if ((s16)g->view_cfg[0] < 0) {                  /* P1's hidden */
            x1 = 0x0001;
            x2 = 0x0110;
        }
    }
    g->load_sprites[0] = (HzSprite){ 0x0168, 0x0F07, 0xC200, x1 };
    g->load_sprites[1] = (HzSprite){ 0x0168, 0x0F00, 0xE210, x2 };
    g->load_mech_frame = 0;                             /* .start */
    g->load_step = 0;
    g->load_delay = 6;
    g->load_busy = 0xCF;
    load_upload_sprites(g);
    load_upload_mech(g);
    g->load_screen_on = true;
    game_frame_work_on(g);                              /* #$2000 */
}

/* VBlank_Loading ($8FBA): the loading screen's VBlank, while Game_LoadMap
 * runs. Nothing once its frames are done; else a frame less and, with the
 * display off: for the first 120 (load_busy $CE down to $57) both mechs
 * rise a line and walk (frames 0, $20, $21, a step every 6 frames: a
 * robot facing up), up to the middle of the view (y $F0); then for 86
 * (load_busy $56 to 1) they transform, MechTransformFrames entry ($57 -
 * load_busy) / 8, sent when it changes; at 0 the jet facing up
 * (MechJetFrames' first), where Mechs_Init puts each player's jet. In a
 * demo the pads are read, and Start on either sets load_abort. Display
 * on. (The frame counter isn't counted, and the split isn't touched.) */
void load_screen_frame(HzGame *g)
{
    if (g->load_busy == 0) {
        return;                                         /* .done */
    }
    g->load_busy--;
    video_display(g->video, false);
    u16 busy = g->load_busy;
    if (busy >= 0x57) {
        g->load_sprites[0].y--;
        g->load_sprites[1].y--;
        load_upload_sprites(g);
        g->load_delay--;
        if (g->load_delay == 0) {
            g->load_delay = 6;
            u8 step = (u8)(g->load_step + 1);
            if (step > 2) {
                step = 0;
            }
            g->load_step = step;                        /* .walk_step */
            g->load_mech_frame = (step == 0) ? 0 : (u16)(0x1F + step);
            load_upload_mech(g);                        /* .walk_frame */
        }
    } else if (busy == 0) {                             /* .transform */
        g->load_mech_frame = MECH_JET_FRAMES[0];
        load_upload_mech(g);                            /* .upload */
    } else {
        u16 f = MECH_TRANSFORM_FRAMES[(0x57 - busy) >> 3];
        if (f != g->load_mech_frame) {                  /* .transform_frame */
            g->load_mech_frame = f;
            load_upload_mech(g);
        }
    }
    if (g->game_type < 0) {                             /* .check_abort */
        game_read_joypads(g);
        if ((g->joypad[0] | g->joypad[1]) & HZ_PAD_START) {
            g->load_abort = 0xFFFF;
        }
    }
    video_display(g->video, true);                     /* .display_on */
}

/* Game_LoadMap's phases (game_load_map_step), each from one of its
 * VDP_WaitVBlanks on: its first frame; the tiles; the screen and the data;
 * the map's tiles; the wait for the loading screen; the last frame. */
enum {
    LOAD_START,
    LOAD_TILES,
    LOAD_SCREEN,
    LOAD_MAP_TILES,
    LOAD_WAIT,
    LOAD_CLEAR
};

/* Game_LoadMap's end (.done): the game's frame start back (IntStubs_Install:
 * VBlank the handler again). */
static HzRun load_done(HzGame *g)
{
    g->frame_work_on = false;
    g->load_screen_on = false;
    game_frame_work_on(g);
    return HZ_RUN_DONE;
}

/* Game_LoadMap's wait for the loading screen (.wait_load_screen): a frame
 * at a time until its last. With quick_load (the harnesses) the frames
 * aren't handed over: VBlank_Loading runs as it would. Then, unless the
 * load was cut short, the frame work held and one more frame. */
static HzRun load_wait(HzGame *g)
{
    while (g->load_busy != 0 && !g->quit) {
        if (!g->quick_load) {
            return HZ_RUN_FRAME;
        }
        load_screen_frame(g);
    }
    if (g->load_abort) {
        return load_done(g);
    }
    g->frame_work_on = false;
    g->load_phase = LOAD_CLEAR;
    return HZ_RUN_FRAME;
}

/* Game_LoadMap ($5538). The graphics and palettes, the screen and the
 * loading screen (load_screen_start), the map's song, the map's data, the
 * wait until the loading screen has had its 207 frames, CRAM line 0
 * black; then the game's frame start back (IntStubs_Install). The data is the
 * assets' (assets.c), read at start-up: what the 68k unpacks from the
 * three LZW entries here is already unpacked there, so each unpack is a
 * copy into the RAM it would land in, and takes no time: the loading
 * screen's frames fall where the 68k waits (on the cartridge the
 * unpacking takes frames too, but under the 207 the screen waits for).
 * load_abort, set by Start on a demo's loading screen, cuts the load short
 * after each step (MainLoop_Title then goes back to the title). False only
 * for a map number past the eight. */
bool game_load_map_begin(HzGame *g)
{
    if (g->map >= HZ_MAPS) {
        return false;
    }
    g->load_phase = LOAD_START;
    return true;
}

/* (Session 80) A frame at a time. load_abort can only be set on a frame
 * (VBlank_Loading's, in a demo) and nothing clears it after
 * load_screen_start, so where the 68k tests it before each step, from the
 * first that finds it set the rest are skipped: on to .done. */
HzRun game_load_map_step(HzGame *g)
{
    const HzAssets *a = g->assets;
    const HzMapAssets *m = &a->map[g->map];
    switch (g->load_phase) {
        case LOAD_START: {
            game_display_on(g);
            g->load_phase = LOAD_TILES;
            return HZ_RUN_FRAME;
        }
        case LOAD_TILES: {
            g->frame_work_on = false;
            /* .sprite_tiles, .font, .hud_tiles */
            load_tiles(g, TILE_SPRITES, a->sprite_tiles,
                       sizeof a->sprite_tiles);
            load_tiles(g, TILE_FONT, a->font_tiles, sizeof a->font_tiles);
            load_tiles(g, TILE_HUD, a->hud_tiles, sizeof a->hud_tiles);
            g->load_phase = LOAD_SCREEN;
            return HZ_RUN_FRAME;
        }
        case LOAD_SCREEN: {
            video_set_colours(g->video, 16, a->game_palettes,
                              48);                      /* .palettes */
            game_display_off(g);
            screen_init_game(g);
            load_screen_start(g);
            game_display_on(g);
            g->load_abort = 0;
            /* The map's song (map + 3) played, then started again
             * repeating: a song $09 starts plays once (session 23). */
            snd_play_music(g, (u16)(g->map + 3));
            snd_start_song(g, 0xFF);
            if (g->load_abort) {
                return load_done(g);
            }
            /* Blocks ($3F000 entry): $FF0000, 512 blocks of 4 x 4
             * nametable words, a terrain word per block at $FF4000, 32
             * animated-tile records at $FF4400. */
            memcpy(g->blocks, m->blocks, sizeof g->blocks);
            memcpy(g->block_terrain, m->terrain, sizeof g->block_terrain);
            memcpy(g->anim_tiles, m->anim, sizeof g->anim_tiles);
            if (g->load_abort) {
                return load_done(g);
            }
            /* Tiles ($22400 entry): $FF5000, then all of it to VRAM
             * $0000. */
            memcpy(g->map_tiles, m->tiles, sizeof g->map_tiles);
            g->load_phase = LOAD_MAP_TILES;
            return HZ_RUN_FRAME;
        }
        case LOAD_MAP_TILES: {
            g->frame_work_on = false;
            load_tiles(g, 0, g->map_tiles, sizeof g->map_tiles);
            game_frame_work_on(g);                      /* .map_tiles */
            map_init_anim_tiles(g);
            if (g->load_abort) {
                return load_done(g);
            }
            /* Layout ($35000 entry): $FF5000, 64 x 64 words. */
            memcpy(g->layout, m->layout, sizeof g->layout);
            if (g->load_abort) {
                return load_done(g);
            }
            g->load_phase = LOAD_WAIT;                  /* .wait_load_screen */
            return load_wait(g);
        }
        case LOAD_WAIT: {
            return load_wait(g);
        }
        default: {                                      /* LOAD_CLEAR */
            for (int i = 0; i < 16; i++) {              /* .clear_line0 */
                video_set_colour(g->video, i, 0);
            }
            return load_done(g);
        }
    }
}

/* Map_InitAnimTiles ($56D4): each record's first word (low 9 bits) names
 * a map tile; its 32 bytes, still in v_unpack_buf, are kept in
 * v_anim_tile_gfx, so Map_AnimTilesUpdate can put the picture back after
 * copying others over it. Then v_anim_tile_shown = that tile, and
 * v_anim_tile_step 0, or $80 (the record is off) when the tile is 0. */
void map_init_anim_tiles(HzGame *g)
{
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {             /* .gfx_record */
        u16 tile = g->anim_tiles[r][0] & 0x1FF;
        memcpy(g->anim_tile_gfx[r], g->map_tiles + tile * 32u, 32);
    }
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {             /* .index_record */
        u16 tile = g->anim_tiles[r][0] & 0x1FF;
        g->anim_tile_shown[r] = tile;
        g->anim_tile_step[r] = (tile == 0) ? 0x80 : 0;
    }
}
