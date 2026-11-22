#include "game.h"

#include <stddef.h>

/* The mechs' set-up and the frame's mech step: Mechs_Init, Mech_Respawn,
 * Mechs_Update round Mech_Update (mech_update.c), the camera that follows
 * each mech, and the production timer of the unit on order. */

/* The 68k's asr.w: an arithmetic shift, spelled out (>> on a negative
 * value is implementation-defined in C). */
static s16 asr16(s16 v, int n)
{
    return (s16)(v < 0 ? ~(~v >> n) : v >> n);
}

/* The bounce off a map edge: the velocity reversed and halved (neg.w,
 * asr.w #1). */
static s16 bounce(s16 vel)
{
    return asr16((s16)-vel, 1);
}

/* One axis of the camera: it moves by `move`, but stays in 0..max (an
 * unsigned compare, so a negative camera is past the low end); what it
 * can't take moves the mech on screen instead. The scroll follows the
 * camera unless `scroll` is NULL (a hidden view). */
static void move_camera(s16 move, s16 *cam, s16 *scroll, s16 *screen,
                        u16 max)
{
    s16 want = (s16)(*cam + move);
    s16 excess = 0;
    if ((u16)want > max) {
        excess = (want < 0) ? want : (s16)(want - max);
    }
    s16 moved = (s16)(move - excess);
    *screen = (s16)(*screen + excess);
    *cam = (s16)(*cam + moved);
    if (scroll != NULL) {
        *scroll = (s16)(*scroll + moved);
    }
}

/* One axis of the mech: its position stays in 0..$7E0 (unsigned, as
 * above). Past the low edge the mech sits at the view's edge
 * (`screen_lo`), past the high edge at `screen_hi`, and either way its
 * velocity bounces back at half speed. */
static void clamp_mech(s16 *pos, s16 *screen, s16 *vel, s16 screen_lo,
                       s16 screen_hi)
{
    if ((u16)*pos <= 0x7E0) {
        return;
    }
    if (*pos < 0) {
        *pos = 0;
        *screen = screen_lo;
    } else {
        *pos = 0x7E0;
        *screen = screen_hi;
    }
    *vel = bounce(*vel);
}

/* Mech_UpdateCamera ($3172): the camera and the view's scroll move by the
 * mech's camera move this frame, the camera kept on the map: x 0-$770 in
 * a split screen (a $90-wide view), 0-$6C0 wide ($140), y 0-$740 ($C0
 * high). Then the mech is kept on the map. The 68k has split and wide as
 * separate copies, differing in those limits, in the mech's screen x at
 * the right edge ($70 / $120), and in wide screen leaving the scroll
 * alone for a hidden view (mech_view not 0). */
static void mech_update_camera(HzGame *g, HzMech *m, HzPoint *cam,
                               s16 *scroll_x, s16 *scroll_y)
{
    bool split = g->split_screen != 0;
    bool shown = split || m->view == 0;

    move_camera(m->cam_dx, &cam->x, shown ? scroll_x : NULL, &m->screen_x,
                split ? 0x770 : 0x6C0);
    clamp_mech(&m->x, &m->screen_x, &m->vel_x, 0, split ? 0x70 : 0x120);

    move_camera(m->cam_dy, &cam->y, shown ? scroll_y : NULL, &m->screen_y,
                0x740);
    clamp_mech(&m->y, &m->screen_y, &m->vel_y, 0x20, 0xC0);
}

/* Mech_Respawn ($2E9C): the mech back to a fresh jet with KIDOU's stats,
 * then its team's spawn count + 1 (stopping at $FFFF). A human playing a
 * CPU burns fuel one unit faster. Fuel starts at a quarter (Mechs_Init
 * tops it up); frame_loaded $FFFF makes VDP_UploadSprites send the frame
 * whatever it is. */
void mech_respawn(HzGame *g, HzMech *m, const HzMech *rival)
{
    const HzMechStats *kidou = &g->assets->mech_stats;  /* UnitStats_KIDOU */

    m->dir = 0;
    m->turn_to = 0;
    m->form = HZ_FORM_JET;
    m->altitude = 0x10;
    m->fuel_use = kidou->fuel_use[HZ_FORM_JET];
    if ((rival->flags & MECH_CPU) && !(m->flags & MECH_CPU)) {
        m->fuel_use++;
    }
    m->max_speed = kidou->max_speed[HZ_FORM_JET];
    m->size = 0x20;
    m->hp = kidou->stats.hp;
    m->fuel = (u16)(kidou->stats.fuel >> 2);
    m->power = kidou->stats.power;
    m->frame_loaded = 0xFFFF;
    m->ammo = kidou->stats.ammo;
    m->vel_x = 0;
    m->vel_y = 0;
    m->target_vel_x = 0;
    m->target_vel_y = 0;
    m->cam_dx = 0;
    m->cam_dy = 0;
    m->sub_x = 0;
    m->sub_y = 0;
    m->speed = 0;

    u16 *spawns = &g->mech_spawns[(m->flags & MECH_TEAM2) ? 1 : 0];
    if (*spawns != 0xFFFF) {
        (*spawns)++;
    }
}

/* Mechs_Init's body for P1, Mech2_Init ($2DD0) for P2. Flags, view and
 * option from the setup; its two fixed sprites (6 + p in its own view,
 * 8 + p in the rival's: P1's palette line 2 and tile $200, the frame at
 * VRAM $4000; P2's line 3, tile $210); the camera with the HQ block's
 * centre where the mech sits in its view (split screen $48 in from the
 * view's left and $60 down, the view being $90 x $C0; wide $A0 and $60);
 * then 1000 money, the radar showing the map, the AI idle, the order menu
 * closed, Mech_Respawn and a full tank. */
static void mech_init(HzGame *g, int p)
{
    HzMech       *m = &g->mech[p];
    const HzMech *rival = &g->mech[p ^ 1];
    HzSprite     *own = &g->sprite_fixed[6 + p];
    HzSprite     *other = &g->sprite_fixed[8 + p];
    u16           attr = (p == 0) ? 0x4200 : 0x6210;

    m->flags = g->player_flags[p];
    m->view = (s8)(g->view_cfg[p] >> 8);
    m->view_index = (u8)g->view_cfg[p];
    m->option2 = g->mech_option2;
    own->size_link = (u16)(0x0F07 + p);
    own->attr = attr;
    other->size_link = 0x0F09;
    other->attr = attr;

    /* The HQ's centre from the camera, and the mech within its view. */
    s16 hq_dx = g->split_screen ? 0x48 : 0xA0;
    s16 hq_dy = 0x60;
    s16 view_x = g->split_screen ? 0x38 : 0x90;
    s16 view_y = 0x50;

    const HzPost *hq = &g->posts[HZ_POST_HQ + p];
    HzPoint *cam = &g->camera[p];
    cam->x = (s16)(hq->x - hq_dx);
    cam->y = (s16)(hq->y - hq_dy);
    m->x = (s16)(cam->x + view_x);
    m->y = (s16)(cam->y + view_y);
    m->screen_x = view_x;
    m->screen_y = (s16)(view_y + 0x20);         /* under the HUD strip */

    if (m->view < 0) {
        g->scroll_y[1] = (s16)0xFFE0;
    }

    m->home_cam = *cam;
    m->home_screen.x = m->screen_x;
    m->home_screen.y = m->screen_y;
    m->menu.radar_mode = 1;
    m->menu.radar_mode_shown = 1;
    m->money = 1000;
    m->ai.move = 0x0B;                          /* AI_Move_Idle */
    m->menu.step = 9;                           /* closed */
    mech_respawn(g, m, rival);
    m->fuel = g->assets->mech_stats.stats.fuel;
}

/* Mechs_Init ($2D00): P2 first (it calls Mech2_Init), then P1. */
void mechs_init(HzGame *g)
{
    mech_init(g, 1);
    mech_init(g, 0);
}

/* The camera step of Mechs_Update ($3044), for mech p after its
 * Mech_Update: none while its order menu is closing. The camera drags the
 * scroll of the plane its view is on (view 1: plane B, anything else:
 * A), or, with the order menu open, the scroll saved when the menu
 * opened, so the plane stays put until the menu puts it back. */
void mech_follow_camera(HzGame *g, int p)
{
    HzMech *m = &g->mech[p];
    if (m->menu.closing) {
        return;
    }
    int plane = (m->view == 1) ? 1 : 0;
    s16 *sx = &g->scroll_x[plane];
    s16 *sy = &g->scroll_y[plane];
    if (m->flags & MECH_MENU_OPEN) {
        sx = &m->menu.scroll_x;
        sy = &m->menu.scroll_y;
    }
    mech_update_camera(g, m, &g->camera[p], sx, sy);
}

/* Mech_UpdateProduction ($3156): while the unit on order is being built,
 * its timer counts down; the frame it would go below 0 the unit is ready
 * (the timer left at $FFFF) and the ready sound is queued. */
static void mech_update_production(HzGame *g, HzMech *m)
{
    if (m->prod_state != PROD_BUILDING) {
        return;
    }
    if (m->prod_timer-- != 0) {
        return;
    }
    m->prod_state = PROD_READY;
    snd_queue_sfx(g, SFX_UNIT_READY);
}

/* Mechs_Update ($3044): the mech tick + 1, then P1 completely and then
 * P2: last frame's pad kept; a human's pad from its own joypad, OR'd with
 * the other joypad when the rival is a CPU, so in a one-player game
 * either controller steers (a CPU's pad comes from AI_MechThink, inside
 * Mech_Update); off the grid (a robot takes itself off), Mech_Update, the
 * camera, production, and back on the grid with its id (1 for P1, $81
 * for P2: a robot puts itself where it now is). */
void mechs_update(HzGame *g)
{
    g->mech_tick++;
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        const HzMech *rival = &g->mech[1 - p];
        m->pad_prev = m->pad;
        if (!(m->flags & MECH_CPU)) {
            m->pad = g->joypad[p];
            if (rival->flags & MECH_CPU) {
                m->pad |= g->joypad[1 - p];
            }
        }
        grid_mark_mech(g, m, ref_none());
        if (g->mech_probe != NULL) {
            g->mech_probe(g, p, false, g->mech_probe_user);
        }
        mech_update(g, p);
        if (g->mech_probe != NULL) {
            g->mech_probe(g, p, true, g->mech_probe_user);
        }
        mech_follow_camera(g, p);
        mech_update_production(g, m);
        grid_mark_mech(g, m, ref_mech(p));
    }
}
