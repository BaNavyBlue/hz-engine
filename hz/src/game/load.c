#include "game.h"

#include "tables.h"

#include <string.h>

static void upload(Vdp *v, u32 cmd, const u8 *src, u32 bytes)
{
    vdp_command(v, cmd);
    vdp_write_bytes(v, src, bytes);
}

#define MECH_FRAMES_ROM 0x010000u    /* MechFrames: 64 x $200 */

/* LoadScreen_UploadSprites ($8F04): the two loading-screen entries to the
 * sprite table at VRAM $D030, sprites 6 and 7. */
static void load_upload_sprites(HzGame *g)
{
    Vdp *v = g->vdp;
    vdp_command(v, 0x50300003);
    for (int i = 0; i < 2; i++) {
        const HzSprite *s = &g->load_sprites[i];
        vdp_write32(v, ((u32)s->y << 16) | s->size_link);
        vdp_write32(v, ((u32)s->attr << 16) | s->x);
    }
}

/* LoadScreen_UploadMech ($8F2A): MechFrames frame load_mech_frame (& $7F;
 * no frame past $3F is ever asked for, so the assets' & $3F), 16 tiles,
 * by DMA to VRAM $4000 (P1's mech tiles) and again to $4200 (P2's), with
 * register 1 $34 (DMA on, display off) meanwhile and $24 after. */
static void load_upload_mech(HzGame *g)
{
    Vdp *v = g->vdp;
    u16 f = (u16)(g->load_mech_frame & 0x7F);
    u32 s = (MECH_FRAMES_ROM + ((u32)f << 9)) >> 1;
    const u8 *src = g->assets->mech_frames[f & (HZ_MECH_FRAMES - 1)];
    vdp_reg(v, 0x8134);
    for (int i = 0; i < 2; i++) {
        vdp_reg(v, 0x9300);                             /* $100 words */
        vdp_reg(v, 0x9401);
        vdp_reg(v, (u16)(0x9500 | (s & 0xFF)));
        vdp_reg(v, (u16)(0x9600 | ((s >> 8) & 0xFF)));
        vdp_reg(v, 0x9700);
        vdp_command(v, i == 0 ? 0x40000081 : 0x42000081);
        vdp_write_bytes(v, src, HZ_MECH_FRAME_BYTES);
    }
    vdp_reg(v, 0x8124);
}

/* LoadScreen_Start ($8E7A), from Game_LoadMap with the display off and
 * the planes drawn (Screen_InitGame links the divider's sprite 5 to 6).
 * The loading screen: each player's mech as a 4 x 4 sprite just below the
 * screen (y $168), P1's sprite 6 (tile $200, line 2), P2's sprite 7 (tile
 * $210, line 3), both with priority; in split screen at the middle of its
 * view (x $B8, $168), in wide screen the shown player's at the middle (x
 * $110) and the other's off the left edge (x 1). The walk at step 0 with
 * its delay 6, 207 frames to go, frame 0 for both; VBlank_Loading as the
 * VBlank handler; interrupts on (a VBlank held while they were masked runs
 * it at once: the screen's first frame shows its first step). */
void load_screen_start(HzGame *g)
{
    g->interrupts_on = false;                           /* #$2700 */
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
    g->vblank_loading = true;
    game_unmask(g);                                     /* #$2000 */
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
void vblank_loading(HzGame *g)
{
    if (g->load_busy == 0) {
        return;                                         /* .done */
    }
    g->load_busy--;
    Vdp *v = g->vdp;
    vdp_reg(v, 0x8124);
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
    vdp_reg(v, 0x8164);                                 /* .display_on */
}

/* Game_LoadMap's wait for the loading screen (.wait_load_screen): a frame
 * at a time until its last. With quick_load (the harnesses) the frames
 * aren't handed over: VBlank_Loading runs as it would. */
static void load_screen_wait(HzGame *g)
{
    while (g->load_busy != 0 && !g->quit) {
        if (g->quick_load) {
            vblank_loading(g);
        } else {
            game_wait_vblank(g);
        }
    }
}

/* Game_LoadMap ($5538). The graphics and palettes, the screen and the
 * loading screen (load_screen_start), the map's song, the map's data, the
 * wait until the loading screen has had its 207 frames, CRAM line 0
 * black; then the VBlank handler back (IntStubs_Install). The data is the
 * assets' (assets.c), read at start-up: what the 68k unpacks from the
 * three LZW entries here is already unpacked there, so each unpack is a
 * copy into the RAM it would land in, and takes no time: the loading
 * screen's frames fall where the 68k waits (on the cartridge the
 * unpacking takes frames too, but under the 207 the screen waits for).
 * load_abort, set by Start on a demo's loading screen, cuts the load short
 * after each step (MainLoop_Title then goes back to the title). False only
 * for a map number past the eight. */
bool game_load_map(HzGame *g)
{
    const HzAssets *a = g->assets;
    Vdp *v = g->vdp;
    if (g->map >= HZ_MAPS) {
        return false;
    }
    const HzMapAssets *m = &a->map[g->map];

    game_display_on(g);
    game_wait_vblank(g);
    g->interrupts_on = false;
    /* .sprite_tiles, .font, .hud_tiles */
    upload(v, 0x48000001, a->sprite_tiles, sizeof a->sprite_tiles);
    upload(v, 0x56200003, a->font_tiles, sizeof a->font_tiles);
    upload(v, 0x60000002, a->hud_tiles, sizeof a->hud_tiles);
    game_wait_vblank(g);
    vdp_command(v, 0xC0200000);                         /* .palettes */
    for (int i = 0; i < 48; i++) {
        vdp_write16(v, a->game_palettes[i]);
    }
    game_display_off(g);
    screen_init_game(g);
    load_screen_start(g);
    game_display_on(g);
    g->load_abort = 0;
    /* The map's song (map + 3) played, then started again repeating: a
     * song $09 starts plays once (session 23). */
    snd_play_music(g, (u16)(g->map + 3));
    snd_start_song(g, 0xFF);

    if (!g->load_abort) {
        /* Blocks ($3F000 entry): $FF0000, 512 blocks of 4 x 4 nametable
         * words, a terrain word per block at $FF4000, 32 animated-tile
         * records at $FF4400. */
        memcpy(g->blocks, m->blocks, sizeof g->blocks);
        memcpy(g->block_terrain, m->terrain, sizeof g->block_terrain);
        memcpy(g->anim_tiles, m->anim, sizeof g->anim_tiles);
    }
    if (!g->load_abort) {
        /* Tiles ($22400 entry): $FF5000, then all of it to VRAM $0000. */
        memcpy(g->map_tiles, m->tiles, sizeof g->map_tiles);
        game_wait_vblank(g);
        g->interrupts_on = false;
        upload(v, 0x40000000, g->map_tiles, sizeof g->map_tiles);
        game_unmask(g);                                 /* .map_tiles */
        map_init_anim_tiles(g);
    }
    if (!g->load_abort) {
        /* Layout ($35000 entry): $FF5000, 64 x 64 words. */
        memcpy(g->layout, m->layout, sizeof g->layout);
    }
    if (!g->load_abort) {
        load_screen_wait(g);                    /* .wait_load_screen */
    }
    if (!g->load_abort) {
        g->interrupts_on = false;
        game_wait_vblank(g);
        vdp_command(v, 0xC0000000);                     /* .clear_line0 */
        for (int i = 0; i < 16; i++) {
            vdp_write16(v, 0);
        }
    }
    g->interrupts_on = false;                           /* .done */
    g->vblank_loading = false;                          /* IntStubs_Install */
    game_unmask(g);
    return true;
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
