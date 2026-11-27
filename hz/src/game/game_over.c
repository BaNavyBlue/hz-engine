/* Game_Over ($9094) and its finale: when an HQ falls, the screen settles on
 * it, it blows up with the view shaking and the map fading to black, and
 * the game's end follows (menu_game_end, game_end.c: WINNER and the
 * results). Then MainLoop_Title: the title again. */

#include <string.h>

#include "game.h"
#include "tables.h"

/* VDP_BeginUpload ($959C): a frame, then HBlank's relink blocked and the
 * display off for the uploads. VDP_EndUpload ($95A6): the display on and
 * the relink allowed again (its wait for VBlank's end takes no frame). */
static void begin_upload(HzGame *g)
{
    game_wait_vblank(g);
    g->split_done = 2;
    video_display(g->video, false);
}

static void end_upload(HzGame *g)
{
    video_display(g->video, true);
    g->split_done = 0;
}

/* GameOver_PlaceMechs ($90F6), a wide game only (one view, the CPU's mech
 * hidden): the shown mech's view goes to the lost HQ -- if it won, its
 * camera's home becomes the loser's (the hidden mech's); if it lost, its
 * own home is the lost HQ already. The shown mech keeps its screen place
 * ($38 for mech 1, $E8 for mech 2) and the hidden one is put beside the
 * lost HQ ($68 left of it for mech 1, $48 right for mech 2, $10 up), so
 * it shows there as a jet. (Session 66) A draw (HZ_DRAW): both HQs lost,
 * the shown mech's own the one shown. */
static void place_mechs(HzGame *g)
{
    int shown = (g->mech[0].view < 0) ? 1 : 0;
    int lost = (g->winner == HZ_DRAW) ? shown : (g->winner == 0 ? 1 : 0);
    const HzPost *hq = &g->posts[HZ_POST_HQ + lost];
    if (shown == (g->winner & 0xFF)) {
        g->mech[shown].home_cam = g->mech[shown ^ 1].home_cam;
    }
    static const s16 SCREEN_X[2] = { 0x38, 0xE8 };
    static const s16 BESIDE_HQ[2] = { -0x68, 0x48 };
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        if (m->view >= 0) {
            m->home_screen.x = SCREEN_X[p];
        } else {
            m->x = (s16)(hq->x + BESIDE_HQ[p]);
            m->y = (s16)(hq->y - 0x10);
        }
    }
}

/* GameOver_MechToJet ($91EA): the mech frozen as a jet facing up (its first
 * MechJetFrames frame, to be uploaded), carrying nothing, its menu shut;
 * its camera at its home, its screen place its home one; with the split,
 * the mech itself at that place in the view. */
static void mech_to_jet(HzGame *g, HzMech *m, HzPoint *cam)
{
    m->dir = 0;
    m->turn_to = 0;
    m->form = 1;
    m->altitude = 0x10;
    m->frame = MECH_JET_FRAMES[0];
    m->frame_loaded = 0xFFFF;
    m->flags &= MECH_ACTIVE | MECH_TEAM2 | MECH_CPU;
    m->menu.step = 8;           /* +$40 8: the menu never runs again */
    m->menu.closing = 0;
    *cam = m->home_cam;
    m->screen_x = m->home_screen.x;
    m->screen_y = m->home_screen.y;
    if (g->split_screen) {
        m->x = (s16)(cam->x + m->home_screen.x);
        m->y = (s16)(cam->y + m->home_screen.y);
    }
}

/* GameOver_ResetViews ($917E): both mechs as jets; with the display off,
 * the views' scroll 0 and flushed, layers A and B cleared (on the 68k
 * four 2 KB fills) and the views drawn again. */
static void reset_views(HzGame *g)
{
    mech_to_jet(g, &g->mech[0], &g->camera[0]);
    mech_to_jet(g, &g->mech[1], &g->camera[1]);
    game_display_off(g);
    g->scroll_x[0] = 0;
    g->scroll_x[1] = 0;         /* clr.l v_scroll_x_a: x of both planes */
    g->scroll_y[0] = 0;
    g->scroll_y[1] = 0;
    map_flush_scroll(g);
    video_fill(g->video, LAYER_A, cell_from_word(0));   /* both views clear */
    video_fill(g->video, LAYER_B, cell_from_word(0));
    map_draw_views(g);
    game_display_on(g);
}

/* GameOver_ClearUnits ($9252): every unit record cleared (the shots are
 * left); v_map_palette reloaded; the sprites rebuilt and uploaded.
 * QUIRK: the palette is MapPalettes' first, map 0's (ABGRUND's) whatever
 * the map, so on any other map the colours change to ABGRUND's at the
 * fade's first step (nothing uploads it before). Kept. */
static void clear_units(HzGame *g)
{
    units_clear_all(g);
    memcpy(g->map_palette, g->assets->map[0].palette,
           sizeof g->map_palette);
    sprites_build(g);
    sprites_multiplex(g);
    begin_upload(g);
    sprites_upload(g);
    end_upload(g);
}

/* FX_SpawnExplosion ($98DA): an explosion as a dying unit (flags $A0, type
 * 3, $A frames) at x, y, in the first free record of either team's (team
 * 1's first); none free: none, or with UNITS a slot more for team 1.
 * Units_UpdateTeam plays it out and removes it. */
static HzUnit *free_record(HzGame *g)
{
    for (int side = 0; side < 2; side++) {
        u32 slots = side_slots(g, side);
        for (u32 i = 0; i < slots; i++) {
            HzUnit *u = unit_at(g, side, i);
            if (!(u->flags & UNIT_ACTIVE)) {
                return u;
            }
        }
    }
    if (units_unlimited(g) && side_grow(g, 0)) {
        return unit_at(g, 0, side_slots(g, 0) - 1);
    }
    return NULL;
}

static void spawn_explosion(HzGame *g, s16 x, s16 y)
{
    HzUnit *u = free_record(g);
    if (u != NULL) {
        unit_clear(u);
        u->flags = UNIT_ACTIVE | UNIT_DYING;
        u->type = 3;
        u->hp = 0x0A;
        u->missiles = 0xFF;
        u->ammo = 0xFF;
        u->fuel = 0xFFFF;
        u->sprite_size = 0x0A00;
        u->x = x;
        u->y = y;
    }
}

/* Random's high word (the 68k's swap after it). */
static u16 random_high(HzGame *g)
{
    return (u16)(game_random(g) >> 16);
}

/* GameOver_RandomExplosion ($9396): an explosion up to 64 px either way
 * of (x, y). */
static void random_explosion(HzGame *g, s16 x, s16 y)
{
    s16 ex = (s16)(x + (random_high(g) & 0x7F) - 0x40);
    s16 ey = (s16)(y + (random_high(g) & 0x7F) - 0x40);
    spawn_explosion(g, ex, ey);
}

/* Palette_FadeOutStep ($93C2): while v_fade_steps lasts, every colour of
 * v_map_palette a level darker on each channel (floor 0), and CRAM line 0
 * from it. */
static void palette_fade_out_step(HzGame *g)
{
    if (g->fade_steps == 0) {
        return;
    }
    g->fade_steps--;
    for (int i = 0; i < 16; i++) {
        u16 c = g->map_palette[i];
        for (int shift = 1; shift <= 9; shift += 4) {
            u16 level = (u16)((c >> shift) & 7);
            if (level > 0) {
                c = (u16)(c - (1u << shift));
            }
        }
        g->map_palette[i] = c;
    }
    video_set_colours(g->video, 0, g->map_palette, 16);
}

/* The shake's state: the scroll shaken (0 plane A, 1 plane B, (session
 * 66) 2 both), frames so far, the offset of the pair. */
typedef struct {
    int plane;
    u16 frames;
    s16 dx, dy;
} Shake;

/* -3..3 (the 68k's -3..4, a 4 made 0). */
static s16 shake_offset(HzGame *g)
{
    s16 d = (s16)((random_high(g) & 7) - 3);
    return (d == 4) ? 0 : d;
}

/* GameOver_ShakeFrame ($930E): the scroll knocked by a new random offset
 * on odd frames and back by it on even ones; the map's edges, the shots
 * and units (the explosions play out) and the sprites, uploaded over two
 * frames; a fade step every 8th. */
static void shake_frame(HzGame *g, Shake *s)
{
    s->frames++;
    if (s->frames & 1) {
        s->dx = shake_offset(g);
        s->dy = shake_offset(g);
    }
    s16 sign = (s->frames & 1) ? 1 : -1;
    for (int p = 0; p < 2; p++) {
        if (s->plane == p || s->plane == 2) {
            g->scroll_x[p] = (s16)(g->scroll_x[p] + sign * s->dx);
            g->scroll_y[p] = (s16)(g->scroll_y[p] + sign * s->dy);
        }
    }
    map_scroll_update(g);
    combat_update(g);
    sprites_build(g);
    sprites_multiplex(g);
    begin_upload(g);
    map_flush_scroll(g);
    end_upload(g);
    begin_upload(g);
    sprites_upload(g);
    if ((s->frames & 7) == 0) {
        palette_fade_out_step(g);
    }
    end_upload(g);
}

/* GameOver_DestroyHQ ($928A): the music stopped, sfx $18 (the HQ's
 * blast); round the lost HQ (12 px up and left of its centre) an
 * explosion a frame with the shake for 100 frames, then 64 more with the
 * map fading out (a step every 8th frame: black after 7), then 32 of the
 * shake alone. The scroll shaken is plane A's, or plane B's in a split
 * screen when P2 lost (P2's view). (Session 66) A draw: the explosions by
 * turns round both HQs (P1's first), and in a split screen both views
 * shaken. */
static void destroy_hq(HzGame *g)
{
    snd_fade_music(g, 0);
    game_wait_vblank(g);
    snd_queue_sfx(g, SFX_HQ_DESTROYED);
    game_wait_vblank(g);
    snd_flush_sfx(g);

    bool draw = g->winner == HZ_DRAW;
    int lost = (g->winner != 0) ? 0 : 1;
    Shake s = { (lost == 1 && g->split_screen) ? 1 : 0, 0, 0, 0 };
    if (draw && g->split_screen) {
        s.plane = 2;
    }
    s16 x[2], y[2];
    for (int p = 0; p < 2; p++) {
        const HzPost *hq = &g->posts[HZ_POST_HQ + (draw ? p : lost)];
        x[p] = (s16)(hq->x - 0x0C);
        y[p] = (s16)(hq->y - 0x0C);
    }

    g->fade_steps = 0;
    for (int i = 0; i < 100 && !g->quit; i++) {
        random_explosion(g, x[i & 1], y[i & 1]);
        shake_frame(g, &s);
    }
    g->fade_steps = 0x10;
    for (int i = 0; i < 64 && !g->quit; i++) {
        random_explosion(g, x[i & 1], y[i & 1]);
        shake_frame(g, &s);
    }
    for (int i = 0; i < 32 && !g->quit; i++) {
        shake_frame(g, &s);
    }
}

/* Game_Over ($9094), from Game_SecondTick when an HQ is at 0 HP (v_winner
 * set, interrupts masked): HBlank's relink blocked, v_game_over $FFFF (no
 * more alerts drawn), the outpost counts kept for the results, the HUD's
 * bars brought up to date; in a wide game the mechs placed round the lost
 * HQ; the views reset to the jets; the units cleared; the HQ destroyed;
 * the effects stopped; the game's end; display off. MainLoop_Title
 * follows (the caller's loop). */
void game_over(HzGame *g)
{
    g->split_done = 1;
    g->hud_sprite_cut = 0;      /* move.w #$100,v_split_done */
    g->split_enable = 0;
    g->game_over = (u16)(g->game_over - 1);
    memcpy(g->results_bases, g->outpost_count, sizeof g->results_bases);
    hud_update_gauges(g);
    begin_upload(g);
    hud_flush_bars(g);
    end_upload(g);
    if (!g->split_screen) {
        place_mechs(g);
    }
    reset_views(g);
    clear_units(g);
    destroy_hq(g);
    game_wait_vblank(g);
    snd_stop_sfx(g);
    menu_game_end(g);
    game_display_off(g);
}
