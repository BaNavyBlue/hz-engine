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
 * and the display off. (Session 80) Up to the frames here; the frames
 * are title_run_step's (T_SEGA, T_SEGA_FADE). */
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
 * the fade-in, which therefore starts from white. (Session 80) The ramp's
 * steps, a pair of frames each, are title_run_step's (T_PHOTO). */
static void show_tank_photo(HzGame *g)
{
    front_picture(g, LAYER_B, 0, 0, 0x2000, 40, 28);
}

static void photo_grey(HzGame *g, u16 grey)
{
    for (int i = 0; i < 16; i++) {
        g->front.palette[16 + i] = grey;
    }
}

/* Title_FadeInPhoto ($5325C): line 1's target the photo's palette; 8
 * steps of a level each, 4 frames apart, line 1 uploaded after each.
 * (Session 80) The steps are title_run_step's (T_PHOTO_FADE). */
static void fade_in_photo(HzGame *g)
{
    HzFront *f = &g->front;
    for (int i = 0; i < 16; i++) {
        f->target[16 + i] = g->assets->menu.tank_photo_palette[i];
    }
}

/* Title_SlideLogo ($53190): 8 steps; in each, every tile of the logo is
 * shown pushed down 7, 6 ... 0 rows -- its top rows only, the rest cut
 * off by the next tile's -- and all 320 uploaded. CHANGED: the 68k runs
 * the steps back to back with the display on and no wait (each takes
 * most of a frame, so the slide lasts about 6 frames, uploads landing
 * mid-frame); the port shows each step for a frame. (Session 80) A step,
 * `down` bytes (4 a row); the frames are title_run_step's (T_SLIDE). */
static void slide_logo(HzGame *g, int down)
{
    static u8 work[HZ_LOGO_TILES * 32 + 32];
    const u8 *logo = g->assets->menu.title_logo;
    for (int t = 0; t < HZ_LOGO_TILES; t++) {
        memset(work + t * 32, 0, 32);
        memcpy(work + t * 32 + down, logo + t * 32, 32);
    }
    front_tiles(g, 0x580, work, HZ_LOGO_TILES * 32);
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
 * for Start.)
 *
 * The fade-in: every colour computed from its target at the level, line
 * 3 alone uploaded. QUIRK: Start during it leaves lines 0-2 of m_palette
 * at that level's darkness, so the fade to black that follows starts from
 * there: the photo and logo drop a few levels at once. (Session 80) Up to
 * the frames here; they are title_run_step's (T_COPYRIGHT_IN on). */
static void wait_start(HzGame *g)
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
    g->front_run.start = false;
    f->fade_level = 0x20;
}

/* Title_Run ($53048): tile $460 on cleared, $3C0 tiles (VRAM $8C00 to
 * $FFFF and, as the address wraps, $0000-$03FF: tiles 0-$1F; on the way
 * it clears the sprite table, the scroll and both layers, whose VRAM
 * those tile numbers are in the menus' layout), the graphics, the SEGA
 * logo, the title song (once), the title (Title_Show: the logo, the
 * photo, its fade in, 41 frames, the slide); 0 after Start, -1 if the
 * song ran out. */
/* (Session 80) Title_Run's phases, as PS2's TitlePhase: the start; the
 * SEGA logo's frames and its fade to black; the photo's 16 frames, its
 * fade in, the 41 frames after it, the logo's slide; the copyright line
 * faded in, its colours cycling, the song's end; the fade to black. */
enum {
    T_START,
    T_SEGA,
    T_SEGA_FADE,
    T_PHOTO,
    T_PHOTO_FADE,
    T_PAUSE,
    T_SLIDE,
    T_COPYRIGHT_IN,
    T_COPYRIGHT,
    T_SONG_END,
    T_FADE_OUT
};

void title_run_begin(HzGame *g)
{
    g->front_run.title_phase = T_START;
}

/* The end: the music faded after Start, the fade to black. */
static HzRun title_end(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    if (r->start) {
        snd_fade_music(g, 2);
    }
    front_set_target(g);
    front_fade_to_black_begin(g);
    r->title_phase = T_FADE_OUT;
    return front_fade_step(g);
}

/* Title_WaitStart's loop: the song's end (a frame, then the music
 * stopped, -1) or a frame of the cycle. */
static HzRun copyright_next(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    if (r->start || g->quit) {
        return title_end(g);
    }
    if (!music_on(g)) {
        r->title_phase = T_SONG_END;
        return HZ_RUN_FRAME;
    }
    cycle_copyright(g);
    r->title_phase = T_COPYRIGHT;
    return HZ_RUN_FRAME;
}

/* Title_WaitStart's fade in, a level a frame. */
static HzRun copyright_in_next(HzGame *g)
{
    if (g->front.fade_level >= 0 && !g->quit) {
        front_apply_fade(g);
        g->front_run.title_phase = T_COPYRIGHT_IN;
        return HZ_RUN_FRAME;
    }
    return copyright_next(g);
}

/* The slide's next step (28 bytes down to 0, 4 at a time), a frame each. */
static HzRun slide_next(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    if (r->n >= 0) {
        slide_logo(g, r->n);
        r->title_phase = T_SLIDE;
        return HZ_RUN_FRAME;
    }
    wait_start(g);
    return copyright_in_next(g);
}

/* The 41 frames after the photo's fade in (none on quit), then the
 * slide. */
static HzRun pause_next(HzGame *g)
{
    if (front_wait_frames_step(g) == HZ_RUN_FRAME) {
        return HZ_RUN_FRAME;
    }
    g->front_run.n = 28;
    return slide_next(g);
}

/* Title_FadeInPhoto's next step: a level towards the target, 4 frames. */
static HzRun photo_fade_next(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    if (r->n < 8 && !g->quit) {
        front_step_to_target(g);
        r->part = 0;
        r->title_phase = T_PHOTO_FADE;
        return HZ_RUN_FRAME;
    }
    front_wait_frames_begin(g, 0x28);
    r->title_phase = T_PAUSE;
    return pause_next(g);
}

/* Title_ShowTankPhoto's next grey ($000 to $EEE), two frames each. */
static HzRun photo_next(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    if (r->n <= 0xEEE && !g->quit) {
        photo_grey(g, (u16)r->n);
        r->part = 0;
        r->title_phase = T_PHOTO;
        return HZ_RUN_FRAME;
    }
    fade_in_photo(g);
    r->n = 0;
    return photo_fade_next(g);
}

/* Title_SegaLogo's next frame of the cycle (130 at most), or its fade. */
static HzRun sega_next(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    if (r->n < 0x82 && !g->quit) {
        cycle_sega_colours(g);
        r->title_phase = T_SEGA;
        return HZ_RUN_FRAME;
    }
    front_set_target(g);
    front_fade_to_black_begin(g);
    r->title_phase = T_SEGA_FADE;
    return front_fade_step(g);
}

HzRun title_run_step(HzGame *g)
{
    HzFrontRun *r = &g->front_run;
    switch (r->title_phase) {
        case T_START: {
            front_display_off(g);
            front_reset_scroll(g);
            video_clear_tiles(g->video, 0x460, 0x3C0);
            load_gfx(g);
            front_blank_planes(g);
            sega_logo(g);
            r->title_result = 0;
            r->n = 0;
            return sega_next(g);
        }
        case T_SEGA: {
            if (front_read_pads(g) & HZ_PAD_START) {
                r->n = 0x82;
            } else {
                front_upload_lines(g, 0x1);
                r->n++;
            }
            return sega_next(g);
        }
        case T_SEGA_FADE: {
            if (front_fade_step(g) == HZ_RUN_FRAME) {
                return HZ_RUN_FRAME;
            }
            front_display_off(g);
            front_blank_planes(g);
            snd_play_music(g, 0);
            snd_start_song(g, 0);
            load_logo(g);                       /* Title_Show */
            draw_logo(g);
            show_tank_photo(g);
            r->n = 0;
            return photo_next(g);
        }
        case T_PHOTO: {
            if (r->part == 0) {
                r->part = 1;
                return HZ_RUN_FRAME;
            }
            r->n += 0x222;
            return photo_next(g);
        }
        case T_PHOTO_FADE: {
            if (++r->part < 4) {
                return HZ_RUN_FRAME;
            }
            front_upload_lines(g, 0x2);
            r->n++;
            return photo_fade_next(g);
        }
        case T_PAUSE: {
            return pause_next(g);
        }
        case T_SLIDE: {
            r->n -= 4;
            return slide_next(g);
        }
        case T_COPYRIGHT_IN: {
            if (front_read_pads(g) & HZ_PAD_START) {
                r->start = true;
                return title_end(g);
            }
            front_upload_lines(g, 0x8);
            g->front.fade_level--;
            return copyright_in_next(g);
        }
        case T_COPYRIGHT: {
            if (front_read_pads(g) & HZ_PAD_START) {
                r->start = true;
                return title_end(g);
            }
            front_upload_lines(g, 0x8);
            return copyright_next(g);
        }
        case T_SONG_END: {
            snd_fade_music(g, 0);
            r->title_result = -1;
            return title_end(g);
        }
        default: {                              /* T_FADE_OUT */
            return front_fade_step(g);
        }
    }
}
