#include "game.h"
#include "tables.h"

#include <stdlib.h>
#include <string.h>

/* The sprite pipeline: Sprites_Build fills the frame's list every frame,
 * and VDP_UploadSprites (after the frame) hands the draw layer the mechs'
 * four fixed sprites and the whole list, and the mechs' frames.
 * (Session 81, docs/hackable.md step 1) Every sprite is drawn:
 * Sprites_Multiplex, which on the cartridge picked the 70 entries shown
 * each frame (the flicker), the window and the links are gone.
 *
 * The drawers work in the cartridge's coordinates (screen + $80), as the
 * 68k does; an entry is put in screen pixels as it is added
 * (sprite_from_ram: 9 bits of each, less the 128). All arithmetic is on
 * 68k words, so it is done in u16 and wraps as the 68k's does; the
 * unsigned compares against a width stand for the 68k's cmp.w / bcc,
 * which also reject what lies left of or above the view (negative).
 *
 * The 68k keeps a0 pointing at the next free list entry and
 * v_sprite_count counting them; every drawer advances both together, so
 * here the count is the index. */

/* One list entry. */
static void list_set(HzGame *g, int at, u16 y, u16 size_link, u16 attr,
                     u16 x)
{
    const HzSprite r = { y, size_link, attr, x };
    g->sprite_list[at] = sprite_from_ram(&r);
}

static void list_add(HzGame *g, u16 y, u16 size_link, u16 attr, u16 x)
{
    list_set(g, g->sprite_count++, y, size_link, attr, x);
}

/* tst.b (v_sprite_count): the count's high byte, i.e. 256 or more. */
static bool list_past_256(const HzGame *g)
{
    return (g->sprite_count >> 8) != 0;
}

static bool view_shown(const HzMech *m)
{
    return m->view >= 0;
}

/* Shown and its order menu closed: what most drawers ask of a view's
 * mech before drawing into it. */
static bool view_open(const HzMech *m)
{
    return m->view >= 0 && !(m->flags & MECH_MENU_OPEN);
}

/* The 68k's asr.l #4 on a muls.w product, spelled out (>> on a negative
 * value is implementation-defined in C). */
static s32 asr32_4(s32 v)
{
    return v < 0 ? ~(~v >> 4) : v >> 4;
}

/* Sprites_DrawMechDebris ($4C80), from Sprites_DrawMechJet for a
 * destroyed mech. Mech_Update sets obj_hp to $180 and counts it
 * down; the radius is $C0 - |HP - $C0|, so the pieces fly out from 0 while
 * HP is above $11C and come back to 0 below $64 (none in between). Each of
 * the frame's 16 tiles becomes a 1 x 1 sprite at centre + radius x the
 * SineTable entry of its MechDebrisDirs direction / 16, in the view of its
 * team (camera A for team 1, B for team 2: team, not player) unless that
 * view is hidden, and in the other view unless the rival's is hidden or
 * its order menu open. `attr` is the mech's palette and tile ($4200 or
 * $6210); flips and priority are added, and each piece takes the next
 * tile. */
static void draw_mech_debris(HzGame *g, const HzMech *m, const HzMech *rival,
                             u16 attr)
{
    s16 d = (s16)(m->hp - 0xC0);
    if (d >= 0) {
        d = (s16)-d;
    }
    u16 radius = (u16)(d + 0xC0);
    if (radius >= 0x64) {
        return;
    }

    const HzPoint *own_cam = g->view_camera[0];
    const HzPoint *other_cam = g->view_camera[1];
    u16 own_dx = 0, other_dx = 0;
    u16 width = 0x160;
    if (g->split_screen) {
        other_dx = 0xB0;
        width = 0xB0;
    }
    if (m->flags & MECH_TEAM2) {
        const HzPoint *c = own_cam;
        own_cam = other_cam;
        other_cam = c;
        u16 t = own_dx;
        own_dx = other_dx;
        other_dx = t;
    }

    u16 cy = (u16)(m->y + 0x0C);
    u16 cx = (u16)(m->x + 0x0C);
    attr = (u16)(attr | (m->frame & 0x1800) | 0x8000);
    bool own_hidden = !view_shown(m);
    bool other_hidden = !view_shown(rival)
                        || (rival->flags & MECH_MENU_OPEN);

    for (int i = 0; i <= 0x0F; i++) {
        if (g->sprite_count < 0x116) {
            const HzPoint *step = &SINE_TABLE[MECH_DEBRIS_DIRS[i]];
            s16 radius_s = (s16)radius;
            s32 sin = step->x;
            s32 cos = step->y;
            u16 x = (u16)(asr32_4(radius_s * sin) + cx);
            u16 y = (u16)(asr32_4(radius_s * cos) + cy);
            if (!own_hidden) {
                u16 sx = (u16)(x - own_cam->x + 0x20);
                u16 sy = (u16)(y - own_cam->y + 0x20);
                if (sx < width && sy < 0xE0) {
                    list_add(g, (u16)(sy + 0x80), 0, attr,
                             (u16)(sx + 0x60 + own_dx));
                }
            }
            if (!other_hidden) {
                u16 sx = (u16)(x - other_cam->x + 0x20);
                u16 sy = (u16)(y - other_cam->y + 0x20);
                if (sx < width && sy < 0xE0) {
                    list_add(g, (u16)(sy + 0x80), 0, attr,
                             (u16)(sx + 0x60 + other_dx));
                }
            }
        }
        attr++;
    }
}

/* Sprites_DrawMechJet ($4AA2). m = a5, rival = a6, fixed = a4 (its entry
 * in its own view; fixed[2], +$10, is its entry in the rival's view),
 * cam = a2 (the rival's camera), attr = d3, own_dx/other_dx = d4/d5 (the
 * x of its own and the rival's view). Both fixed entries start hidden
 * (y 0); a destroyed mech goes to the debris, a robot stops there. A jet:
 * attr with its flips and priority; altitude + 2, up to $10; in its own
 * view (shown, menu closed) the fixed entry at its screen position and a
 * shadow ($A570, 4 x 4, priority) altitude pixels to the right; in the
 * rival's view (shown, menu closed) the fixed entry at obj_x/y - the
 * rival's camera if within -$20..width-$20 across and -$20..$BF down,
 * and, when airborne, a shadow likewise. Shadows go in the list only below
 * $117 entries. */
static void draw_mech_jet(HzGame *g, HzMech *m, const HzMech *rival,
                          HzSprite *fixed, const HzPoint *cam, u16 attr,
                          u16 own_dx, u16 other_dx)
{
    fixed[0].y = 0;
    fixed[2].y = 0;
    if (m->flags & MECH_DESTROYED) {
        draw_mech_debris(g, m, rival, attr);
        return;
    }
    if (m->form == HZ_FORM_ROBOT) {
        return;
    }
    attr = (u16)(attr | (m->frame & 0x1800) | 0x8000);
    fixed[0].attr = attr;
    u16 alt = (u16)(m->altitude + 2);
    if (alt > 0x10) {
        alt = 0x10;
    }
    m->altitude = (u8)alt;

    if (view_open(m)) {
        u16 x = (u16)(m->screen_x + own_dx + 0x80);
        fixed[0].x = x;
        u16 shadow_x = (u16)(x + alt);
        u16 y = (u16)(m->screen_y + 0x80);
        fixed[0].y = y;
        if (g->sprite_count < 0x117) {
            list_add(g, y, 0x0F00, 0xA570, shadow_x);
        }
    }

    if (!view_open(rival)) {
        return;
    }
    fixed[2].attr = fixed[0].attr;
    u16 width = g->split_screen ? 0xB0 : 0x160;
    u16 x = (u16)(m->x - cam->x + 0x20);
    if (x < width) {
        u16 y = (u16)(m->y - cam->y + 0x20);
        if (y < 0xE0) {
            fixed[2].y = (u16)(y + 0x80);
            fixed[2].x = (u16)(x + other_dx + 0x60);
        }
    }
    if (g->sprite_count >= 0x117) {
        return;
    }
    if (m->altitude == 0) {
        return;
    }
    x = (u16)(m->x + m->altitude - cam->x + 0x20);
    if (x >= width) {
        return;
    }
    u16 y = (u16)(m->y - cam->y + 0x20);
    if (y >= 0xE0) {
        return;
    }
    list_add(g, (u16)(y + 0x80), 0x0F00, 0xA570, (u16)(x + other_dx + 0x60));
}

/* Sprites_DrawMechRobot ($4BC6), arguments as Sprites_DrawMechJet's (attr
 * isn't used). A robot, not destroyed: altitude 0, and one list sprite (4
 * x 4) per view it shows in, with the attr of its own fixed entry less
 * priority and flips, so map tiles below $40 (priority) cover it. The
 * sprites are only added below $117 entries; in its own view that test
 * returns from the routine. */
static void draw_mech_robot(HzGame *g, HzMech *m, const HzMech *rival,
                            const HzSprite *fixed, const HzPoint *cam,
                            u16 own_dx, u16 other_dx)
{
    if ((m->flags & MECH_DESTROYED) || m->form != HZ_FORM_ROBOT) {
        return;
    }
    m->altitude = 0;
    u16 attr = (u16)(fixed[0].attr & 0x67FF);

    if (view_open(m)) {
        if (g->sprite_count >= 0x117) {
            return;
        }
        list_add(g, (u16)(m->screen_y + 0x80), 0x0F00, attr,
                 (u16)(m->screen_x + own_dx + 0x80));
    }

    if (!view_open(rival) || g->sprite_count >= 0x117) {
        return;
    }
    u16 width = g->split_screen ? 0xB0 : 0x160;
    u16 x = (u16)(m->x - cam->x + 0x20);
    if (x >= width) {
        return;
    }
    u16 y = (u16)(m->y - cam->y + 0x20);
    if (y >= 0xE0) {
        return;
    }
    list_add(g, (u16)(y + 0x80), 0x0F00, attr, (u16)(x + other_dx + 0x60));
}

/* Sprites_DrawUnits ($4DD4): every active unit of both teams (team 1's
 * slots, then team 2's) within the view (-$20..width-$20, -$20..$BF), not
 * carried, while the list is below $115, gets up to three entries in this
 * order (front to back): a blinking marker ($2590, 2 x 2) when its fuel is
 * 0 or its missiles and ammo both are, for the first $28 frames of every
 * $40; its turret overlay (unit_overlay_attr) when unit_turret b7 is set;
 * then the unit itself (obj_sprite_attr, obj_sprite_size). A drawn unit
 * is UNIT_ON_SCREEN; one outside the view stops being so unless
 * `keep_shown` (the second view's pass: on screen in either view). */
typedef struct {
    u16  width;
    bool markers;
    u16  left, top, dx;
    bool keep_shown;
} UnitView;

static void draw_unit(HzGame *g, HzUnit *u, const UnitView *v)
{
    if (!(u->flags & UNIT_ACTIVE)) {
        return;
    }
    u16 x = (u16)(u->x - v->left);
    u16 y = (u16)(u->y - v->top);
    if (x >= v->width || y >= 0xE0 || (u->flags & UNIT_CARRIED)
        || g->sprite_count >= 0x115) {
        if (!v->keep_shown) {
            u->flags &= (u8)~UNIT_ON_SCREEN;
        }
        return;
    }

    int marker = -1;
    if (v->markers) {
        if (u->fuel == 0) {
            marker = g->sprite_count++;
        } else if (u->missiles == 0 && u->ammo == 0) {
            marker = g->sprite_count++;
        }
    }
    int overlay = -1;
    if (u->turret & 0x80) {
        overlay = g->sprite_count++;
    }

    u->flags |= UNIT_ON_SCREEN;
    u16 sx = (u16)(x + v->dx);
    u16 sy = (u16)(y + 0x80);
    list_add(g, sy, u->sprite_size, u->sprite_attr, sx);
    if (overlay >= 0) {
        list_set(g, overlay, sy, u->sprite_size, u->overlay_attr, sx);
    }
    if (marker >= 0) {
        list_set(g, marker, sy, 0x0500, 0x2590, sx);
    }
}

/* (Session 61) UNITS (g->crowd): every unit in the view is drawn and
 * none flickers. Its sprites (the same three, front to back) go to
 * g->sprite_more, which the draw layer draws behind every other sprite
 * with no limit (video_sprites_behind), so the 80 were left to the rest
 * -- the mechs, the shots, the bars, the order menu, all now in front of
 * the units -- and the multiplexer had no units to share out. (Session
 * 81: every sprite is drawn now; the crowd stays behind the rest.) */
static void more_add(HzGame *g, u16 y, u16 size_link, u16 attr, u16 x)
{
    HzSpriteMore *l = &g->sprite_more;
    if (l->count == l->cap) {
        u32 cap = l->cap ? l->cap * 2 : 1024;
        Sprite *s = realloc(l->sprite, cap * sizeof *s);
        if (s == NULL) {
            return;                     /* out of memory: not drawn */
        }
        l->sprite = s;
        l->cap = cap;
    }
    const HzSprite r = { y, size_link, attr, x };
    l->sprite[l->count++] = sprite_from_ram(&r);
}

static void draw_unit_more(HzGame *g, HzUnit *u, const UnitView *v)
{
    if (!(u->flags & UNIT_ACTIVE)) {
        return;
    }
    u16 x = (u16)(u->x - v->left);
    u16 y = (u16)(u->y - v->top);
    if (x >= v->width || y >= 0xE0 || (u->flags & UNIT_CARRIED)) {
        if (!v->keep_shown) {
            u->flags &= (u8)~UNIT_ON_SCREEN;
        }
        return;
    }
    u->flags |= UNIT_ON_SCREEN;
    u16 sx = (u16)(x + v->dx);
    u16 sy = (u16)(y + 0x80);
    if (v->markers
        && (u->fuel == 0 || (u->missiles == 0 && u->ammo == 0))) {
        more_add(g, sy, 0x0500, 0x2590, sx);
    }
    if (u->turret & 0x80) {
        more_add(g, sy, u->sprite_size, u->overlay_attr, sx);
    }
    more_add(g, sy, u->sprite_size, u->sprite_attr, sx);
}

static void draw_units(HzGame *g, const HzPoint *cam, u16 dx,
                       bool keep_shown)
{
    UnitView v;
    v.width = g->split_screen ? 0xB0 : 0x160;
    v.markers = g->blink_timer < 0x28;
    v.left = (u16)(cam->x - 0x20);
    v.top = (u16)(cam->y - 0x20);
    v.dx = dx;
    v.keep_shown = keep_shown;
    for (int side = 0; side < 2; side++) {
        u32 slots = side_slots(g, side);
        for (u32 slot = 0; slot < slots; slot++) {
            if (g->crowd) {
                draw_unit_more(g, unit_at(g, side, slot), &v);
            } else {
                draw_unit(g, unit_at(g, side, slot), &v);
            }
        }
    }
}

/* One shot for Sprites_DrawAirShots / _DrawGroundShots: within the view
 * (-$18..width-$18, -$18..$BF), the list below 256 entries: SHOT_ON_SCREEN
 * and a 1 x 1 sprite (2 x 2 while exploding) with the shot's attr; else
 * not on screen, whichever view's pass this is (so the bit ends up as the
 * second view's). */
static void draw_shot(HzGame *g, HzShot *s, const HzPoint *cam, u16 dx,
                      u16 width)
{
    u16 x = (u16)(s->x - (u16)(cam->x - 0x18));
    u16 y = (u16)(s->y - (u16)(cam->y - 0x18));
    if (x >= width || y >= 0xD8 || list_past_256(g)) {
        s->flags &= (u8)~SHOT_ON_SCREEN;
        return;
    }
    s->flags |= SHOT_ON_SCREEN;
    u16 size = (s->flags & SHOT_EXPLODING) ? 0x0500 : 0;
    list_add(g, (u16)(y + 0x88), size, s->attr, (u16)(x + dx));
}

/* Sprites_DrawAirShots ($4ED2): all 64 shot records, those fired from the
 * air. */
static void draw_air_shots(HzGame *g, const HzPoint *cam, u16 dx)
{
    u16 width = g->split_screen ? 0xA8 : 0x158;
    for (int i = 0; i < HZ_SHOTS; i++) {
        HzShot *s = &g->shots[i];
        if ((s->flags & SHOT_ACTIVE) && (s->flags & SHOT_FROM_AIR)) {
            draw_shot(g, s, cam, dx, width);
        }
    }
}

/* Sprites_DrawGroundShots ($4F58): each team's 24 bullets (not its
 * missiles) not fired from the air. */
static void draw_ground_shots(HzGame *g, const HzPoint *cam, u16 dx)
{
    u16 width = g->split_screen ? 0xA8 : 0x158;
    for (int team = 0; team < 2; team++) {
        for (int i = 0; i < 24; i++) {
            HzShot *s = &g->shots[team * 32 + i];
            if ((s->flags & SHOT_ACTIVE) && !(s->flags & SHOT_FROM_AIR)) {
                draw_shot(g, s, cam, dx, width);
            }
        }
    }
}

/* Sprites_DrawOutpostBars ($5034): for each outpost record up to the first
 * inactive one, each side's hold (1-4) as a bar that many cells tall
 * beside the outpost's block: P1's ($A594) 8 px left of it, P2's ($A598)
 * on its right edge, from 8 px above its top (screen y = centre + 8 -
 * camera; the block's top is at centre - $10 + $20, the view starting at
 * line $20). A bar is drawn if its screen x is within -8 up to the view's
 * width; the outpost is skipped unless centre + 8 is within the view's
 * height; drawing stops at 256 entries. */
static void draw_outpost_bars(HzGame *g, const HzPoint *cam, u16 dx)
{
    u16 width = g->split_screen ? 0x98 : 0x148;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        const HzPost *p = &g->posts[i];
        if (!(p->flags & POST_USED)) {
            return;
        }
        u16 y = (u16)(p->y + 8 - cam->y);
        if (y >= 0xE0) {
            continue;
        }
        y = (u16)(y + 0x80);
        if (list_past_256(g)) {
            return;
        }
        if (p->hold[0] != 0) {
            u16 x = (u16)(p->x - cam->x - 0x10);
            if (x < width) {
                list_add(g, y, (u16)((u8)(p->hold[0] - 1) << 8),
                         0xA594,
                         (u16)(x + dx + 0x78));
            }
        }
        if (p->hold[1] != 0) {
            u16 x = (u16)(p->x - cam->x + 0x18);
            if (x < width) {
                list_add(g, y, (u16)((u8)(p->hold[1] - 1) << 8),
                         0xA598,
                         (u16)(x + dx + 0x78));
            }
        }
    }
}

/* Sprites_DrawAlerts ($50D6) for mech m, nothing once the game is over:
 *  - production (view shown, blink >= $14): 3 x 3 icon at a fixed place in
 *    the view, $A5C0 while a unit is being built, $A5C9 when it's ready;
 *  - missiles homing in (view shown): sfx $14 when blink & $1F is 0; while
 *    it is 8 or more, a 2 x 2 arrow ($A5D2) at the MissileArrows* place of
 *    each direction bit (x + $B0 for view slot 1 in split screen);
 *  - HQ hit (mech_hq_alert, view shown): sfx $13 when it reads $A0, $64 or
 *    $28; while blink >= $10, a 3 x 3 warning ($A5A3), and only then is
 *    the count taken down.
 * The places are x $B8 / $DC (+$B0 for team 2 in split screen; wide $110 /
 * $134), y $A2. Each part draws only below 256 entries. */
static void draw_alerts(HzGame *g, HzMech *m)
{
    if (g->game_over) {
        return;
    }

    if (view_shown(m) && g->blink_timer >= 0x14) {
        u16 x = 0xB8;
        u16 y = 0xA2;
        if (m->flags & MECH_TEAM2) {
            x = (u16)(x + 0xB0);
        }
        if (!g->split_screen) {
            x = 0x110;
        }
        if (!list_past_256(g)) {
            if (m->prod_state == PROD_BUILDING) {
                list_add(g, y, 0x0A00, 0xA5C0, x);
            } else if (m->prod_state == PROD_READY) {
                list_add(g, y, 0x0A00, 0xA5C9, x);
            }
        }
    }

    if (view_shown(m) && m->missile_dirs != 0) {
        u16 blink = (u16)(g->blink_timer & 0x1F);
        if (blink == 0) {
            snd_queue_sfx(g, SFX_MISSILE_ALERT);
        }
        if (blink >= 8) {
            const HzPoint *pos = MISSILE_ARROWS_WIDE;
            u16 dx = 0;
            if (g->split_screen) {
                pos = MISSILE_ARROWS_SPLIT;
                if (m->view != 0) {
                    dx = 0xB0;
                }
            }
            if (!list_past_256(g)) {
                u16 dirs = m->missile_dirs;
                for (int i = 0; i <= 0x0F; i++) {
                    if (dirs & 1) {
                        list_add(g, (u16)pos->y, 0x0500, 0xA5D2,
                                 (u16)(pos->x + dx));
                    }
                    dirs >>= 1;
                    pos++;
                }
            }
        }
    }

    if (m->hq_alert == 0 || !view_shown(m)) {
        return;
    }
    if (m->hq_alert == 0xA0 || m->hq_alert == 0x64 || m->hq_alert == 0x28) {
        snd_queue_sfx(g, SFX_HQ_ALERT);
    }
    if (g->blink_timer < 0x10) {
        return;
    }
    u16 x = 0xDC;
    if (m->flags & MECH_TEAM2) {
        x = (u16)(x + 0xB0);
    }
    if (!g->split_screen) {
        x = 0x134;
    }
    if (list_past_256(g)) {
        return;
    }
    list_add(g, 0xA2, 0x0A00, 0xA5A3, x);
    m->hq_alert--;
}

/* Sprites_DrawOrderMenu ($52A6), for a mech whose order menu is open and
 * settled: the radar's own marker (attr_own, 1 x 1) and the rival's (if
 * its y isn't 0), below $116 entries; in map mode, below $106 entries,
 * the radar sprites whose y isn't 0, copied whole. */
static void draw_order_menu(HzGame *g, const HzMech *m, const HzSprite *radar,
                            u16 attr_own, u16 attr_foe)
{
    if (g->sprite_count >= 0x116) {
        return;
    }
    list_add(g, m->radar_own_y, 0, attr_own, m->radar_own_x);
    if (m->radar_foe_y != 0) {
        list_add(g, m->radar_foe_y, 0, attr_foe, m->radar_foe_x);
    }
    if (m->menu.radar_mode == 0 || g->sprite_count >= 0x106) {
        return;
    }
    for (int i = 0; i < HZ_RADAR_SPRITES; i++) {
        if (radar[i].y != 0) {
            g->sprite_list[g->sprite_count++] = sprite_from_ram(&radar[i]);
        }
    }
}

/* Sprites_Build ($47CA). The list, front to back: alerts (P1, P2), outpost
 * bars (view A, B), jet shadows, air shots, robots, units, ground shots,
 * order-menu sprites. The jets themselves are fixed entries 6-9, ahead of
 * the whole list. A view's part is drawn if its mech's view is shown and
 * its order menu closed (the order menu's part: open and settled). View A
 * belongs to P1 and view B to P2 (x + $B0 when P2's view slot is 1); in
 * wide screen the hidden player's part is skipped and the other view pass
 * draws into the one view. */
void sprites_build(HzGame *g)
{
    g->sprite_count = 0;
    memset(g->sprite_list, 0, sizeof g->sprite_list);
    g->sprite_more.count = 0;           /* (session 61) UNITS' */
    g->blink_timer = (u16)((g->blink_timer + 1) & 0x3F);

    HzMech *m1 = &g->mech[0];
    HzMech *m2 = &g->mech[1];
    u16 b_dx = (m2->view == 1) ? 0xB0 : 0;
    u16 split_dx = g->split_screen ? 0xB0 : 0;

    draw_alerts(g, m1);
    draw_alerts(g, m2);

    if (view_open(m1)) {
        draw_outpost_bars(g, g->view_camera[0], 0);
    }
    if (view_open(m2)) {
        draw_outpost_bars(g, g->view_camera[1], b_dx);
    }

    draw_mech_jet(g, m1, m2, &g->sprite_fixed[6], &g->camera[1], 0x4200,
                  0, split_dx);
    draw_mech_jet(g, m2, m1, &g->sprite_fixed[7], &g->camera[0], 0x6210,
                  split_dx, 0);

    if (view_open(m1)) {
        draw_air_shots(g, g->view_camera[0], 0x68);
    }
    if (view_open(m2)) {
        draw_air_shots(g, g->view_camera[1], (u16)(0x68 + b_dx));
    }

    draw_mech_robot(g, m1, m2, &g->sprite_fixed[6], &g->camera[1],
                    0, split_dx);
    draw_mech_robot(g, m2, m1, &g->sprite_fixed[7], &g->camera[0],
                    split_dx, 0);

    /* The order of fixed sprites 6-9, by P2's form (.link_order: the
     * links of sprites 5-9 on the cartridge, 5's leading to the first and
     * the last's to the list): robot 6, 7, 8, 9; jet 7, 6, 9, 8. */
    const u8 *links = m2->form == HZ_FORM_ROBOT ? SPRITE_LINKS_P2_ROBOT
                                                : SPRITE_LINKS_P2_JET;
    u8 at = links[0];
    for (int i = 0; i < 4; i++) {
        g->sprite_order[i] = at;
        at = links[at - 5];
    }

    /* The 68k's d4 b31: set, then cleared by view A's moveq if it draws,
     * then flipped for view B: the first view drawn takes UNIT_ON_SCREEN
     * off the units outside it, the second doesn't. */
    bool keep_shown = true;
    if (view_open(m1)) {
        keep_shown = false;
        draw_units(g, g->view_camera[0], 0x60, keep_shown);
    }
    keep_shown = !keep_shown;
    if (view_open(m2)) {
        draw_units(g, g->view_camera[1], (u16)(0x60 + b_dx), keep_shown);
    }

    if (view_open(m1)) {
        draw_ground_shots(g, g->view_camera[0], 0x68);
    }
    if (view_open(m2)) {
        draw_ground_shots(g, g->view_camera[1], (u16)(0x68 + b_dx));
    }

    if (view_shown(m1) && (m1->flags & MECH_MENU_OPEN)
        && m1->menu.step == 0) {
        draw_order_menu(g, m1, g->radar_sprites[0], 0x859C, 0x859D);
    }
    if (view_shown(m2) && (m2->flags & MECH_MENU_OPEN)
        && m2->menu.step == 0) {
        draw_order_menu(g, m2, g->radar_sprites[1], 0x859D, 0x859C);
    }
}

/* A sprite in the cartridge's RAM format as the draw layer's: its words'
 * 9 bits of position less the 128 of the border, its size, its cell word
 * and (session 72) its colour table. */
Sprite sprite_from_ram(const HzSprite *s)
{
    Sprite o;
    Cell c = cell_from_word(s->attr);
    o.x = (s16)((s->x & 0x1FF) - 128);
    o.y = (s16)((s->y & 0x1FF) - 128);
    o.tile = c.tile;
    o.w = (u8)(((s->size_link >> 10) & 3) + 1);
    o.h = (u8)(((s->size_link >> 8) & 3) + 1);
    o.pal = c.pal;
    o.prio = c.prio;
    o.flip_h = c.flip_h;
    o.flip_v = c.flip_v;
    o.colours = (u8)(s->size_link >> 12);
    return o;
}

/* One more sprite of the frame's, under those before it. */
bool sprites_show_one(HzGame *g, const HzSprite *s)
{
    Sprite o = sprite_from_ram(s);
    return video_sprite_push(g->video, &o);
}

/* VDP_UploadSprites ($53D2), after the frame with the display off: the
 * frame's sprites to the draw layer, front to back -- the mechs' fixed
 * sprites in Sprites_Build's order, then every list entry; the HUD strip
 * holds them back when v_hud_sprite_cut is set (every game: VDP_Init
 * sets it), until the strip lets them go (hud_release, game.c). (Session
 * 61) UNITS' units behind them. And each mech's frame (16 tiles,
 * MechFrames + frame x $200) to its tiles ($200, $210) when mech_frame
 * isn't what was sent last. (Session 81) On the cartridge: sprites 0-9
 * from v_sprite_fixed (0-5 the divider, the layer's now), then the
 * multiplexer's 70-entry window of the list, linked. */
void sprites_upload(HzGame *g)
{
    Video *v = g->video;
    video_sprites_begin(v);
    video_sprites_game(v);
    for (int i = 0; i < 4; i++) {
        sprites_show_one(g, &g->sprite_fixed[g->sprite_order[i]]);
    }
    for (int i = 0; i < g->sprite_count && i < HZ_SPRITE_LIST; i++) {
        video_sprite_push(v, &g->sprite_list[i]);
    }
    video_sprites_hold(v, g->hud_sprite_cut != 0);
    video_sprites_behind(v, g->sprite_more.sprite, g->sprite_more.count);

    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        if (m->frame == m->frame_loaded) {
            continue;
        }
        m->frame_loaded = m->frame;
        /* No mech frame past $3F is ever set (robot $00-$2F, MechJetFrames
         * + 5 at most $39, the transform tables $3A-$3F); the 68k would
         * send whatever follows MechFrames in the ROM, the port frame &
         * $3F. */
        u16 f = (u16)(m->frame & 0xFF);
        video_load_tiles(v, p == 0 ? 0x200 : 0x210,
                         g->assets->mech_frames[f & (HZ_MECH_FRAMES - 1)],
                         HZ_MECH_FRAME_BYTES / TILE_BYTES);
    }
}
