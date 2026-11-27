/* The title ($53048-$5346C): the SEGA logo, then the "Herzog zwei"
 * logo over the tank photo with the copyright line, until Start or the
 * end of the title song.
 *
 * The tiles while the title runs: 0-$45F the photo (layer B,
 * palette 1), $480-$4AF the SEGA logo and $4B0-$4FF the copyright
 * lines (window, palettes 0 and 3), $580-$6BF the logo (window, palette
 * 0; the 320th tile is its palette, never drawn). The window layer and
 * layer B are filled with $26BE, a blank tile, everywhere else. */

#include "front.h"

#include <string.h>

/* Title_LoadGfx ($5309A): the photo's tiles from tile 0, the SEGA
 * logo's and the copyright lines' from tile $480 (VRAM $9000). */
static void load_gfx(HzGame *g)
{
    const HzMenuAssets *m = &g->assets->menu;
    front_tiles(g, 0x000, m->tank_photo, sizeof m->tank_photo);
    front_tiles(g, 0x480, m->title_gfx, sizeof m->title_gfx);
}

/* Title_CycleSegaColours ($53324): every 4th call, line 0's colours 2-12
 * from SegaLogoCycle, the window one colour further back each time (from
 * colour 20 down to 0, then it stays). */
static void cycle_sega_colours(HzGame *g)
{
    HzFront *f = &g->front;
    if (--f->cycle_timer >= 0) {
        return;
    }
    f->cycle_timer = 3;
    if (f->cycle_pos < 0) {
        return;
    }
    int from = f->cycle_pos / 2;
    f->cycle_pos -= 2;
    for (int i = 0; i < 11; i++) {
        f->palette[2 + i] = g->assets->menu.sega_cycle[from + i];
    }
}

/* Title_SegaLogo ($53296): the logo in SegaLogoPalette, 130
 * frames of its colours cycling (Start ends them), then a fade to black
 * and the display off. */
static void sega_logo(HzGame *g)
{
    HzFront *f = &g->front;
    for (int i = 0; i < 16; i++) {
        f->palette[i] = g->assets->menu.sega_palette[i];
    }
    front_upload_lines(g, 0x1);
    front_picture(g, LAYER_W, 14, 12, 0x8480, 12, 4);
    front_display_on(g);
    /* CHANGED: here the 68k busy-waits about 1.25 frames ($3E81 passes of
     * dbf) with the logo up, before the cycle's first frame; the port
     * goes straight on. */
    snd_fade_music(g, 0);
    f->cycle_pos = 0x28;
    f->cycle_timer = 0;
    for (int n = 0; n < 0x82 && !g->quit; n++) {
        cycle_sega_colours(g);
        if (front_wait_pad(g) & HZ_PAD_START) {
            break;
        }
        front_upload_lines(g, 0x1);
    }
    front_set_target(g);
    front_fade_to_black(g);
    front_display_off(g);
}

/* Title_LoadLogo ($5315E): the logo's palette, the entry's last tile,
 * into line 0 and the target. (The 68k unpacks the logo into RAM here;
 * the port has it in the assets.) */
static void load_logo(HzGame *g)
{
    HzFront *f = &g->front;
    const u8 *pal = g->assets->menu.title_logo + 319 * 32;
    for (int i = 0; i < 16; i++) {
        f->palette[i] = (u16)((pal[i * 2] << 8) | pal[i * 2 + 1]);
    }
    front_upload_lines(g, 0x1);
    front_set_target(g);
}

/* Title_DrawLogo ($5310A): the logo's tiles cleared, its nametable (11 x
 * 29 at window (6, 4), in front), the display on. */
static void draw_logo(HzGame *g)
{
    video_clear_tiles(g->video, 0x580, 0x140);
    front_picture(g, LAYER_W, 6, 4, 0x8580, 29, 11);
    front_display_on(g);
}

/* Title_ShowTankPhoto ($53206): the photo on plane B, then 16 frames.
 * QUIRK: over those frames the 68k writes a grey ramp ($000 up to $EEE,
 * every 2 frames) to line 1 of m_palette but never uploads it, so CRAM
 * stays black: in effect a 16-frame wait that leaves line 1 white for
 * the fade-in, which therefore starts from white. */
static void show_tank_photo(HzGame *g)
{
    HzFront *f = &g->front;
    front_picture(g, LAYER_B, 0, 0, 0x2000, 40, 28);
    for (u16 grey = 0; grey <= 0xEEE && !g->quit; grey += 0x222) {
        for (int i = 0; i < 16; i++) {
            f->palette[16 + i] = grey;
        }
        front_frame(g);
        front_frame(g);
    }
}

/* Title_FadeInPhoto ($5325C): line 1's target the photo's palette; 8
 * steps of a level each, 4 frames apart, line 1 uploaded after each. */
static void fade_in_photo(HzGame *g)
{
    HzFront *f = &g->front;
    for (int i = 0; i < 16; i++) {
        f->target[16 + i] = g->assets->menu.tank_photo_palette[i];
    }
    for (int step = 0; step < 8 && !g->quit; step++) {
        front_step_to_target(g);
        for (int k = 0; k < 4; k++) {
            front_frame(g);
        }
        front_upload_lines(g, 0x2);
    }
}

/* Title_SlideLogo ($53190): 8 steps; in each, every tile of the logo is
 * shown pushed down 7, 6 ... 0 rows -- its top rows only, the rest cut
 * off by the next tile's -- and all 320 uploaded. CHANGED: the 68k runs
 * the steps back to back with the display on and no wait (each takes
 * most of a frame, so the slide lasts about 6 frames, uploads landing
 * mid-frame); the port shows each step for a frame. */
static void slide_logo(HzGame *g)
{
    static u8 work[HZ_LOGO_TILES * 32 + 32];
    const u8 *logo = g->assets->menu.title_logo;
    for (int down = 28; down >= 0; down -= 4) {
        for (int t = 0; t < HZ_LOGO_TILES; t++) {
            memset(work + t * 32, 0, 32);
            memcpy(work + t * 32 + down, logo + t * 32, 32);
        }
        front_tiles(g, 0x580, work, HZ_LOGO_TILES * 32);
        front_frame(g);
    }
}

/* Title_Show ($530EE) */
static void title_show(HzGame *g)
{
    load_logo(g);
    draw_logo(g);
    show_tank_photo(g);
    fade_in_photo(g);
    front_wait_frames(g, 0x28);
    slide_logo(g);
}

/* Title_CycleCopyright ($5335A): every 4th call, line 3's colours 6-14
 * from CopyrightCycle, the window a colour further back each time, and
 * round from colour 14. */
static void cycle_copyright(HzGame *g)
{
    HzFront *f = &g->front;
    if (--f->cycle_timer >= 0) {
        return;
    }
    f->cycle_timer = 3;
    if (f->cycle_pos < 0) {
        f->cycle_pos = 0x1C;
    }
    int from = f->cycle_pos / 2;
    f->cycle_pos -= 2;
    for (int i = 0; i < 9; i++) {
        f->palette[48 + 6 + i] = g->assets->menu.copyright_cycle[from + i];
    }
}

/* Is the title song still playing? Title_WaitStart asks the Z80 (its
 * z_music_on); the port asks the host's sound. */
static bool music_on(HzGame *g)
{
    return g->snd_music_on == NULL || g->snd_music_on(g->snd_user);
}

/* Title_WaitStart ($53394): the copyright line (its overseas spelling,
 * "Techno Soft", on an overseas machine; "Tecno Soft" on a Japanese one),
 * faded in from black over 33 frames, then its colours cycling until
 * Start (the music faded, 0) or the title song's end (the music stopped,
 * -1: the attract demo). Then a fade to black. (Nothing on screen asks
 * for Start.) */
static int wait_start(HzGame *g)
{
    HzFront *f = &g->front;
    u16 text = (g->hw_version & 0x80) ? 0xE4B0 : 0xE4D8;
    front_picture(g, LAYER_W, 8, 22, text, 16, 1);
    front_picture(g, LAYER_W, 8, 23, (u16)(text + 16), 24, 1);
    f->cycle_pos = 0x1C;
    f->cycle_timer = 0;
    for (int i = 0; i < 16; i++) {
        f->target[48 + i] = g->assets->menu.copyright_palette[i];
    }

    /* The fade-in: every colour computed from its target at the level,
     * line 3 alone uploaded. QUIRK: Start during it leaves lines 0-2 of
     * m_palette at that level's darkness, so the fade to black that
     * follows starts from there: the photo and logo drop a few levels at
     * once. */
    bool start = false;
    for (f->fade_level = 0x20; f->fade_level >= 0 && !g->quit;
         f->fade_level--) {
        front_apply_fade(g);
        if (front_wait_pad(g) & HZ_PAD_START) {
            start = true;
            break;
        }
        front_upload_lines(g, 0x8);
    }

    int result = 0;
    while (!start && !g->quit) {
        if (!music_on(g)) {
            front_frame(g);
            snd_fade_music(g, 0);
            result = -1;
            break;
        }
        cycle_copyright(g);
        if (front_wait_pad(g) & HZ_PAD_START) {
            start = true;
            break;
        }
        front_upload_lines(g, 0x8);
    }
    if (start) {
        snd_fade_music(g, 2);
    }
    front_set_target(g);
    front_fade_to_black(g);
    return result;
}

/* Title_Run ($53048): tile $460 on cleared, $3C0 tiles (VRAM $8C00 to
 * $FFFF and, as the address wraps, $0000-$03FF: tiles 0-$1F; on the way
 * it clears the sprite table, the scroll and both layers, whose VRAM
 * those tile numbers are in the menus' layout), the graphics, the SEGA
 * logo, the title song (once), the title; 0 after Start, -1 if the song
 * ran out. */
int title_run(HzGame *g)
{
    front_display_off(g);
    front_reset_scroll(g);
    video_clear_tiles(g->video, 0x460, 0x3C0);
    load_gfx(g);
    front_blank_planes(g);
    sega_logo(g);
    front_blank_planes(g);
    snd_play_music(g, 0);
    snd_start_song(g, 0);
    title_show(g);
    return wait_start(g);
}
