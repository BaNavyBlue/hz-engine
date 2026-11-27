/* hz_shot: runs the port from VDP_Init to the end of Game_Start's fade-in,
 * optionally pans the cameras for a number of frames, and writes the
 * screen as a PNG.
 *
 *   hz_shot ASSETS OUT.png [--map N] [--mode N] [--difficulty N]
 *                       [--pan SCRIPT] [--pan2 SCRIPT]
 *                       [--chase] [--land] [--debris] [--alerts]
 *                       [--frames DIR] [--raw FILE] [--raw-frames DIR]
 *                       [--units FILE] [--spawn-probes]
 *
 * --mode is the menu's mode byte (v_options +0): b0 P2 is CPU, b1 P1 is
 * CPU, b2 wide screen; default 0 (split screen, two players). A CPU
 * player against a human starts with units (Units_SpawnCPUStart), whose
 * type depends on --difficulty (v_options +1, 0-3 = TYPE A-D; default 0).
 * The RNG starts from v_rng_seed_saved = 0, as after a cold boot.
 *
 * --pan moves P1 (--pan2 P2; without it P2 does the opposite of P1).
 * SCRIPT is segments "dx,dy,frames" joined by ':' ("8,0,40:0,-3,20";
 * spaces around the ':' are allowed):
 * for that many frames the mech's camera move (mech_cam_dx/dy) is dx, dy
 * and the mech moves with it, which is what Mech_Update's move code does
 * while the camera follows (it isn't ported; this stands in for it). Keep
 * |dx|, |dy| <= 8: the game never scrolls further in a frame, and
 * Map_ScrollUpdate queues one row and one column at most. The scripts
 * start on the second frame: on the first, the mechs keep still, as in
 * the game (Game_Start clears the pad and velocities start at 0). That
 * matters: Map_ScrollUpdate's v_scroll_*_prev start at 0 (MainLoop_Title's
 * clear) while Map_DrawViews leaves the scroll at 8-15, so a first-frame
 * move of 8 up and left would queue nothing and leave a margin row and
 * column undrawn.
 *
 * --chase moves P2 towards P1 instead (at most 8 a frame on each axis),
 * aiming at P1's position plus an offset that turns a direction every 32
 * frames and changes length every 256 ($30, $60, $90), so each mech comes
 * and goes in the other's view, across every edge.
 *
 * The rest of the Mech_Update stand-in: a mech that moves faces the way
 * it moves (the SineTable direction nearest (dx, dy)), and its frame is
 * what Mech_Update's animation step ($3A62) gives a mech that isn't
 * firing or walking: robot = direction, jet = MechJetFrames[direction].
 * Mechs start as jets (Mech_Respawn); --land makes them robots from the
 * second frame, then jets and robots in turns of $80 frames (taking off,
 * the shadow moves out 2 px a frame; landed, a robot keeps the attr of
 * its last jet frame, flips and priority masked).
 *
 * Two more stand-ins exercise the rest of the mech sprites, not as the
 * game would pace them: --debris destroys both mechs from the second
 * frame, their HP running down together from $180 by 3 a frame, over
 * and over (Sprites_DrawMechDebris draws them near both ends of the run,
 * and with the mechs close their 32 pieces meet the VDP's 20 sprites a
 * line); --alerts
 * sets what Sprites_DrawAlerts shows: mech_prod_state cycling 0, 1, 2
 * every $40 frames, mech_missile_dirs a different pattern every $10 (0
 * every fourth), and mech_hq_alert $B4 every $100 frames.
 *
 * --frames writes every frame the port presents (DIR/fNNNN.png; DIR must
 * exist). --raw writes the last frame, --raw-frames DIR every panned frame
 * (DIR/fNNNN.raw), for tools/hzview_check.py: 320 x 224 palette indices,
 * 64 big-endian CRAM words, then "HZC6", map, mode (a byte each), the
 * cameras the frame was drawn from (P1 x, y, P2 x, y as big-endian words),
 * the plane A and B nametables ($1000 bytes each), the H scroll words at
 * $D400 (4 bytes) and VSRAM 0-1 (2 big-endian words); then the sprite
 * state the frame's sprites were built from: a byte (0: no upload yet,
 * the table is still Screen_InitGame's), v_blink_timer as Sprites_Build
 * left it (word), then per mech obj_flags and mech_view (a byte each), mech_form, mech_frame,
 * mech_altitude (as Sprites_Build found it), obj_hp, mech_screen_x/y and
 * obj_x/y (words),
 * mech_prod_state (byte), mech_missile_dirs (word) and mech_hq_alert as
 * Sprites_Build found it (byte); then the difficulty (a byte); then per
 * mech the HUD bars' inputs as HUD_UpdateGauges found them that pass:
 * obj_ammo (byte), obj_fuel and its HQ's HP (words); then
 * v_unit_ai_group as Map_AnimTilesUpdate found it (word): the checker
 * steps its own model of the map animation on the passes that animate.
 * The units never move yet (nothing that moves them is ported), so the
 * checker works them out from the ROM itself.
 *
 * ASSETS is the assets/ directory the game reads (tools/hz_extract.py);
 * the checkers read the ROM, so a check that passes also shows the
 * assets are the ROM's.
 *
 * --units FILE writes the unit records, counts and v_grid as Game_Start
 * left them, --spawn-probes adds what Unit_Spawn makes of a list of new
 * units (see write_units), for tools/hzunits_check.py.
 *
 * The order is MainLoop_Title's: Game_SetupMode, VDP_Init, Game_LoadMap,
 * Map_ScanOutposts, Game_Start. Each panned frame is Game_FrameLoop as far
 * as it is ported: the camera step of Mechs_Update for P1 then P2,
 * UnitAI_Update (its group step: the map animates on groups 0 and 6),
 * HUD_UpdateGauges, Map_AnimTilesUpdate, Sprites_Build, Map_ScrollUpdate,
 * Sprites_Multiplex (game_frame_draw), then VDP_WaitVBlank and the uploads
 * (game_frame_end). The menus, sound, the units' updates and the attract
 * demo aren't ported yet. */

#include "video_vdp.h"
#include "../src/core/pngwrite.h"
#include "../src/game/game.h"
#include "../src/game/romcodes.h"
#include "../src/game/tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAN_SEGMENTS 64

typedef struct {
    s16 dx, dy;
    int frames;
} PanSegment;

typedef struct {
    PanSegment seg[PAN_SEGMENTS];
    int        count;
} PanScript;

/* What a frame's sprites, HUD bars and map animation were built from
 * (HZC3-HZC6). */
typedef struct {
    u8  flags;
    s8  view;
    u16 form, frame, altitude, hp;
    s16 screen_x, screen_y, x, y;
    u8  prod_state;
    u16 missile_dirs;
    u8  hq_alert;
    u8  ammo;
    u16 fuel, hq_hp;
} MechState;

typedef struct {
    bool      valid;
    u16       blink;
    MechState mech[2];
    u16       group;       /* v_unit_ai_group */
} SpriteState;

typedef struct {
    u8          idx[SCREEN_W * SCREEN_H];
    const char *frames_dir;
    const char *raw_dir;
    int         frame_no;
    int         raw_no;
    int         map, mode, difficulty;
    HzPoint     cams[2];   /* the cameras the VDP's contents belong to */
    SpriteState shown;     /* what the VDP's sprite table was built from */
    SpriteState pending;   /* this frame's, uploaded after the wait */
} Shot;

static void put16(FILE *f, u16 v)
{
    fputc(v >> 8, f);
    fputc(v & 0xFF, f);
}

/* (Half B of docs/hackable.md's step 1) The planes and scroll a raw
 * frame carries for the pans' check are the cartridge's VRAM, laid out
 * from the draw layer's changes by tools/video_vdp.c. */
static VideoVdp g_cart;

static bool write_png(const char *path, const Video *v, const u8 *idx)
{
    static u8 rgb[SCREEN_W * SCREEN_H * 3];
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        cram_rgb(v->colours[idx[i] & 63], rgb + i * 3);
    }
    return png_write(path, SCREEN_W, SCREEN_H, 8, 2, rgb, SCREEN_W * 3,
                     NULL, 0, NULL);
}

static bool write_raw(const char *path, const Shot *s, const Vdp *v)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    fwrite(s->idx, 1, sizeof s->idx, f);
    for (int i = 0; i < VDP_CRAM_WORDS; i++) {
        fputc(v->cram[i] >> 8, f);
        fputc(v->cram[i] & 0xFF, f);
    }
    fputs("HZC6", f);
    fputc(s->map, f);
    fputc(s->mode, f);
    for (int p = 0; p < 2; p++) {
        fputc((u16)s->cams[p].x >> 8, f);
        fputc((u16)s->cams[p].x & 0xFF, f);
        fputc((u16)s->cams[p].y >> 8, f);
        fputc((u16)s->cams[p].y & 0xFF, f);
    }
    fwrite(v->vram + VRAM_PLANE_A, 1, 0x1000, f);
    fwrite(v->vram + VRAM_PLANE_B, 1, 0x1000, f);
    fwrite(v->vram + VRAM_HSCROLL, 1, 4, f);
    for (int i = 0; i < 2; i++) {
        fputc(v->vsram[i] >> 8, f);
        fputc(v->vsram[i] & 0xFF, f);
    }
    const SpriteState *st = &s->shown;
    fputc(st->valid, f);
    put16(f, st->blink);
    for (int p = 0; p < 2; p++) {
        const MechState *m = &st->mech[p];
        fputc(m->flags, f);
        fputc((u8)m->view, f);
        put16(f, m->form);
        put16(f, m->frame);
        put16(f, m->altitude);
        put16(f, m->hp);
        put16(f, (u16)m->screen_x);
        put16(f, (u16)m->screen_y);
        put16(f, (u16)m->x);
        put16(f, (u16)m->y);
        fputc(m->prod_state, f);
        put16(f, m->missile_dirs);
        fputc(m->hq_alert, f);
    }
    fputc(s->difficulty, f);
    for (int p = 0; p < 2; p++) {
        const MechState *m = &st->mech[p];
        fputc(m->ammo, f);
        put16(f, m->fuel);
        put16(f, m->hq_hp);
    }
    put16(f, st->group);
    return fclose(f) == 0;
}

static void on_frame(HzGame *g, void *user)
{
    Shot *s = user;
    video_compose(g->video, s->idx);
    if (s->raw_dir != NULL) {
        char path[1024];
        snprintf(path, sizeof path, "%s/f%04d.raw", s->raw_dir, s->raw_no++);
        if (!write_raw(path, s, g_cart.vdp)) {
            fprintf(stderr, "hz_shot: can't write %s (does %s exist?); "
                            "no more raw frames\n", path, s->raw_dir);
            s->raw_dir = NULL;
        }
    }
    if (s->frames_dir != NULL) {
        char path[1024];
        snprintf(path, sizeof path, "%s/f%04d.png", s->frames_dir,
                 s->frame_no);
        if (!write_png(path, g->video, s->idx)) {
            fprintf(stderr, "hz_shot: can't write %s (does %s exist?); "
                            "no more frames\n", path, s->frames_dir);
            s->frames_dir = NULL;
        }
    }
    s->frame_no++;
}

/* --units: the unit records as Game_Start left them, and with
 * --spawn-probes what Unit_Spawn does with each of a list of new units
 * (every probe from the same state: the game is restored after each),
 * for tools/hzunits_check.py. A text file: "HZU1 map mode difficulty",
 * "R" v_rng_seed and v_cpu_boost_hq_hp, "C" the counts, "G" each occupied
 * v_grid cell and its id, "U" each slot that isn't all zero; then "P"
 * per probe: its number, flags, type, dir, x, y, the result, v_hit_flags,
 * v_hit_id and each slot it made active (the placed unit's) with its
 * record. Numbers in hex.
 *
 * The probes, in order: round each start unit (slot order), at offsets
 * dx, dy each from PROBE_OFFSETS (either side of the overlap limits for
 * sizes $10 and $18), types 0, 3 and 8; then a sweep of the whole map,
 * x and y from PROBE_SWEEP (every 40 px, the edges, a little past them),
 * the same types. Probe n: dir n & $F, order n & $F, team n & 1, flags
 * $80 | team << 4. */
static const s16 PROBE_OFFSETS[] = {
    -25, -24, -23, -17, -16, -15, 0, 15, 16, 17, 23, 24, 25
};
static const u8 PROBE_TYPES[] = { 0, 3, 8 };

static int probe_sweep(int i)
{
    if (i == 0) {
        return -16;
    }
    if (i == 1) {
        return -4;
    }
    if (i < 2 + 52) {
        return (i - 2) * 40;                /* 0 .. 2040 */
    }
    return 2044;                            /* i == 54 */
}

#define PROBE_SWEEP_N 55

static void put_unit(FILE *f, const HzUnit *u)
{
    fprintf(f, "%X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X "
               "%X %X %X %X %X %X %X",
            u->flags, u->type, u->order, u->dir, u->turn_to, u->tick,
            u->power, u->hp, u->size, u->fuel_use, (u16)u->x, (u16)u->y,
            u->missiles, u->ammo, u->fuel, u->sprite_attr, u->sprite_size,
            u->speed, u->prev_speed, u->max_speed, u->turret, u->turret_dir,
            u->aim_dir, u->overlay_attr, u->grid_cell,
            (u16)u->target_vel_x);
    fprintf(f, " %X", (u16)u->target_vel_y);
}

static void probe(FILE *f, HzGame *g, HzGame *saved, int n, u8 type, s16 x,
                  s16 y)
{
    u8 team = (u8)(n & 1);
    u8 flags = (u8)(0x80 | (team << 4));
    u8 dir = (u8)(n & 0x0F);
    game_copy(saved, g);
    int r = unit_spawn(g, flags, type, (u8)(n & 0x0F), dir, x, y);
    fprintf(f, "P %X %X %X %X %X %X %X %X %X", n, flags, type, dir, (u16)x,
            (u16)y, r & 0xFF, g->hit_flags, rom_hit_id(g->hit_id));
    for (int i = 0; i < HZ_UNITS; i++) {    /* placed: the new record */
        if (!(rom_unit_c(saved, i)->flags & 0x80)
            && (rom_unit_c(g, i)->flags & 0x80)) {
            fprintf(f, " %X ", i);
            put_unit(f, rom_unit_c(g, i));
        }
    }
    fputc('\n', f);
    game_copy(g, saved);
}

/* A record all zero but for its place (side, slot): never used. */
static bool unit_is_zero(const HzUnit *u)
{
    static const HzUnit zero;
    HzUnit c = *u;
    c.side = 0;
    c.slot = 0;
    return memcmp(&c, &zero, sizeof zero) == 0;
}

static bool write_units(const char *path, HzGame *g, bool probes,
                        const Shot *s)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return false;
    }
    fprintf(f, "HZU1 %X %X %X\n", s->map, s->mode, s->difficulty);
    fprintf(f, "R %X %X\n", g->rng_seed, g->cpu_boost_hq_hp);
    fprintf(f, "C %X %X %X %X", (u8)g->unit_count[0], (u8)g->unit_count[1],
            g->units_built[0], g->units_built[1]);
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 16; i++) {
            fprintf(f, " %X", (u8)g->unit_type_count[t][i]);
        }
    }
    fputc('\n', f);
    for (int c = 0; c < HZ_GRID * HZ_GRID; c++) {
        if (!ref_is_none(g->grid[c])) {
            fprintf(f, "G %X %X\n", c, rom_grid_id(g->grid[c]));
        }
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        if (!unit_is_zero(rom_unit_c(g, i))) {
            fprintf(f, "U %X ", i);
            put_unit(f, rom_unit_c(g, i));
            fputc('\n', f);
        }
    }
    if (probes) {
        static HzGame saved;
        static HzGame start;
        game_copy(&start, g);
        int n = 0;
        for (int i = 0; i < HZ_UNITS; i++) {
            const HzUnit *o = rom_unit_c(&start, i);
            if (!(o->flags & 0x80)) {
                continue;
            }
            for (size_t a = 0; a < sizeof PROBE_OFFSETS / sizeof *PROBE_OFFSETS;
                 a++) {
                for (size_t b = 0;
                     b < sizeof PROBE_OFFSETS / sizeof *PROBE_OFFSETS; b++) {
                    for (size_t t = 0; t < sizeof PROBE_TYPES; t++) {
                        probe(f, g, &saved, n++, PROBE_TYPES[t],
                              (s16)(o->x + PROBE_OFFSETS[b]),
                              (s16)(o->y + PROBE_OFFSETS[a]));
                    }
                }
            }
        }
        for (int a = 0; a < PROBE_SWEEP_N; a++) {
            for (int b = 0; b < PROBE_SWEEP_N; b++) {
                for (size_t t = 0; t < sizeof PROBE_TYPES; t++) {
                    probe(f, g, &saved, n++, PROBE_TYPES[t],
                          (s16)probe_sweep(b), (s16)probe_sweep(a));
                }
            }
        }
    }
    return fclose(f) == 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: hz_shot ASSETS OUT.png [--map N] [--mode N] "
                    "[--pan SCRIPT] [--pan2 SCRIPT]\n"
                    "               [--chase] [--land] [--debris] [--alerts]\n"
                    "               [--frames DIR] [--raw FILE] "
                    "[--raw-frames DIR]\n"
                    "               [--difficulty N] [--units FILE] "
                    "[--spawn-probes]\n"
                    "  SCRIPT: dx,dy,frames[:dx,dy,frames...], "
                    "|dx|,|dy| <= 8\n");
    return 2;
}

static bool parse_pan(const char *text, PanScript *ps)
{
    ps->count = 0;
    const char *p = text;
    while (*p != '\0') {
        int dx, dy, frames, used;
        if (sscanf(p, "%d,%d,%d%n", &dx, &dy, &frames, &used) != 3
            || dx < -8 || dx > 8 || dy < -8 || dy > 8 || frames < 0
            || ps->count >= PAN_SEGMENTS) {
            return false;
        }
        ps->seg[ps->count++] = (PanSegment){ (s16)dx, (s16)dy, frames };
        p += used;
        while (*p == ' ') {
            p++;
        }
        if (*p == ':') {
            p++;
        } else if (*p != '\0') {
            return false;
        }
        while (*p == ' ') {
            p++;
        }
    }
    return true;
}

static int pan_frames(const PanScript *ps)
{
    int n = 0;
    for (int i = 0; i < ps->count; i++) {
        n += ps->seg[i].frames;
    }
    return n;
}

/* The move for frame `f` of a script; none once it has run out. */
static void pan_at(const PanScript *ps, int f, s16 *dx, s16 *dy)
{
    *dx = 0;
    *dy = 0;
    for (int i = 0; i < ps->count; i++) {
        if (f < ps->seg[i].frames) {
            *dx = ps->seg[i].dx;
            *dy = ps->seg[i].dy;
            return;
        }
        f -= ps->seg[i].frames;
    }
}

/* The SineTable direction (0 up, 4 right, clockwise) nearest (dx, dy). */
static u8 direction_of(s16 dx, s16 dy)
{
    u8   best = 0;
    long best_dot = 0;
    for (u8 d = 0; d < 16; d++) {
        long dot = (long)dx * SINE_TABLE[d].x + (long)dy * SINE_TABLE[d].y;
        if (d == 0 || dot > best_dot) {
            best = d;
            best_dot = dot;
        }
    }
    return best;
}

static s16 clamp8(int v)
{
    return (s16)(v < -8 ? -8 : (v > 8 ? 8 : v));
}

/* --chase: P2's move this frame, towards P1 plus a turning offset. */
static void chase_at(const HzGame *g, int f, s16 *dx, s16 *dy)
{
    static const int LENGTH[3] = { 0x30, 0x60, 0x90 };
    int d = (f / 32) % 16;
    int len = LENGTH[(f / 256) % 3];
    int ox = SINE_TABLE[d].x * len / 32;
    int oy = SINE_TABLE[d].y * len / 32;
    const HzMech *m1 = &g->mech[0];
    const HzMech *m2 = &g->mech[1];
    *dx = clamp8(m1->x + ox - m2->x);
    *dy = clamp8(m1->y + oy - m2->y);
}

/* After Sprites_Build; altitude and hq_alert as it found them (it moves
 * both). HUD_UpdateGauges ran just before it, and Sprites_Build changes
 * none of the bars' inputs. */
static void snapshot(const HzGame *g, SpriteState *st, const u8 altitude[2],
                     const u8 hq_alert[2])
{
    st->valid = true;
    st->blink = g->blink_timer;
    st->group = g->unit_ai_group;
    for (int p = 0; p < 2; p++) {
        const HzMech *m = &g->mech[p];
        const HzPost *hq = &g->posts[HZ_POST_HQ + p];
        st->mech[p] = (MechState){ m->flags, m->view, m->form, m->frame,
                                   altitude[p], m->hp, m->screen_x,
                                   m->screen_y, m->x, m->y, m->prod_state,
                                   m->missile_dirs, hq_alert[p], m->ammo,
                                   m->fuel,
                                   post_hq_hp(hq) };
    }
}

typedef struct {
    bool chase, land, debris, alerts;
} StandIns;

/* --debris and --alerts for frame f (from 1). */
static void stand_in_state(HzMech *m, int p, int f, const StandIns *si)
{
    if (si->debris) {
        m->flags |= 0x20;
        m->hp = (u16)(0x180 - ((f - 1) * 3) % 0x183);
    }
    if (si->alerts) {
        static const u16 DIRS[4] = { 0x0000, 0x8421, 0xFFFF, 0x1248 };
        m->prod_state = (u8)(((f + p * 0x20) / 0x40) % 3);
        int k = (f + p * 8) / 0x10;
        m->missile_dirs = (u16)(DIRS[k % 4] << (k % 5));
        if ((f + p * 0x80) % 0x100 == 1) {
            m->hq_alert = 0xB4;
        }
    }
}

/* One panned frame: Game_FrameLoop as far as it is ported. For each mech
 * the stand-in for Mech_Update (clr.l mech_cam_dx, then a move that drags
 * the camera, the heading and the frame), then its camera step;
 * UnitAI_Update (the group step); game_frame_draw (HUD_UpdateGauges,
 * Map_AnimTilesUpdate, Sprites_Build, Map_ScrollUpdate, Sprites_Multiplex);
 * the wait and the uploads. The VDP holds what the uploads wrote, for the
 * cameras and mechs as they are now, from the next frame on. */
static void pan_frame(HzGame *g, Shot *shot, const PanScript ps[2], int f,
                      const StandIns *si)
{
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        if (f == 0) {
            m->cam_dx = 0;
            m->cam_dy = 0;
        } else if (p == 1 && si->chase) {
            chase_at(g, f - 1, &m->cam_dx, &m->cam_dy);
        } else {
            pan_at(&ps[p], f - 1, &m->cam_dx, &m->cam_dy);
        }
        m->x = (s16)(m->x + m->cam_dx);
        m->y = (s16)(m->y + m->cam_dy);
        if (m->cam_dx != 0 || m->cam_dy != 0) {
            m->dir = direction_of(m->cam_dx, m->cam_dy);
        }
        if (si->land && f > 0) {
            m->form = (((f - 1) / 0x80) % 2 == 0) ? 0 : 1;
        }
        if (f > 0) {
            stand_in_state(m, p, f, si);
        }
        m->frame = (m->form == 0) ? m->dir : MECH_JET_FRAMES[m->dir & 0x0F];
        mech_follow_camera(g, p);
    }
    unit_ai_update(g);
    u8 altitude[2] = { g->mech[0].altitude, g->mech[1].altitude };
    u8 hq_alert[2] = { g->mech[0].hq_alert, g->mech[1].hq_alert };
    game_frame_draw(g);
    snapshot(g, &shot->pending, altitude, hq_alert);
    game_frame_end(g);
    shot->cams[0] = g->camera[0];
    shot->cams[1] = g->camera[1];
    shot->shown = shot->pending;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        return usage();
    }
    const char *assets_path = argv[1];
    const char *out_path = argv[2];
    int map = 0;
    int mode = 0;
    int difficulty = 0;
    const char *raw_path = NULL;
    const char *raw_dir = NULL;
    const char *units_path = NULL;
    bool spawn_probes = false;
    static PanScript pan[2];
    bool pan2_given = false;
    StandIns si = { false, false, false, false };

    static Shot shot;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--chase") == 0) {
            si.chase = true;
            continue;
        }
        if (strcmp(argv[i], "--land") == 0) {
            si.land = true;
            continue;
        }
        if (strcmp(argv[i], "--debris") == 0) {
            si.debris = true;
            continue;
        }
        if (strcmp(argv[i], "--alerts") == 0) {
            si.alerts = true;
            continue;
        }
        if (strcmp(argv[i], "--spawn-probes") == 0) {
            spawn_probes = true;
            continue;
        }
        if (i + 1 >= argc) {
            return usage();
        }
        if (strcmp(argv[i], "--map") == 0) {
            map = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0) {
            mode = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--difficulty") == 0) {
            difficulty = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0) {
            shot.frames_dir = argv[++i];
        } else if (strcmp(argv[i], "--raw") == 0) {
            raw_path = argv[++i];
        } else if (strcmp(argv[i], "--raw-frames") == 0) {
            raw_dir = argv[++i];
        } else if (strcmp(argv[i], "--units") == 0) {
            units_path = argv[++i];
        } else if (strcmp(argv[i], "--pan") == 0) {
            if (!parse_pan(argv[++i], &pan[0])) {
                return usage();
            }
        } else if (strcmp(argv[i], "--pan2") == 0) {
            if (!parse_pan(argv[++i], &pan[1])) {
                return usage();
            }
            pan2_given = true;
        } else {
            return usage();
        }
    }
    if (map < 0 || map >= HZ_MAPS || mode < 0 || mode > 7 || difficulty < 0
        || difficulty > 3) {
        return usage();
    }

    static HzAssets assets;
    if (!hz_assets_load(&assets, assets_path)) {
        return 1;
    }

    if (!pan2_given) {
        pan[1] = pan[0];
        for (int i = 0; i < pan[1].count; i++) {
            pan[1].seg[i].dx = (s16)-pan[1].seg[i].dx;
            pan[1].seg[i].dy = (s16)-pan[1].seg[i].dy;
        }
    }
    shot.map = map;
    shot.mode = mode;
    shot.difficulty = difficulty;

    static Video video;
    static Vdp vdp;
    static HzGame game;
    video_init(&video);
    vdp_reset(&vdp);
    video_vdp_watch(&g_cart, &video, &vdp);
    game_init(&game, &assets, &video);
    game.units_frozen = true;       /* the view model doesn't follow the
                                       unit AI */
    game.cpus_frozen = true;   /* nor the CPU mechs' AI (make mechai) */
    game.frame = on_frame;
    game.frame_user = &shot;
    game.options[0] = (u8)mode;
    game.options[1] = (u8)difficulty;
    game.options[3] = (u8)map;

    if (!game_new(&game)) {
        fprintf(stderr, "hz_shot: map %d doesn't load\n", map);
        return 1;
    }
    game_start(&game);
    if (units_path != NULL && !write_units(units_path, &game, spawn_probes,
                                           &shot)) {
        fprintf(stderr, "hz_shot: can't write %s\n", units_path);
        return 1;
    }
    shot.cams[0] = game.camera[0];
    shot.cams[1] = game.camera[1];

    shot.raw_dir = raw_dir;
    int frames = pan_frames(&pan[0]);
    if (pan_frames(&pan[1]) > frames) {
        frames = pan_frames(&pan[1]);
    }
    if (frames > 0) {
        frames++;                   /* the still first frame */
    }
    for (int f = 0; f < frames; f++) {
        pan_frame(&game, &shot, pan, f, &si);
    }

    /* The next frame as far as it is ported: what the draw layer holds now,
     * with HBlank's relink (VDP_WaitVBlank hands it to on_frame). */
    game_wait_vblank(&game);

    int rc = 0;
    if (!write_png(out_path, &video, shot.idx)) {
        fprintf(stderr, "hz_shot: can't write %s\n", out_path);
        rc = 1;
    }
    if (raw_path != NULL && !write_raw(raw_path, &shot, &vdp)) {
        fprintf(stderr, "hz_shot: can't write %s\n", raw_path);
        rc = 1;
    }
    printf("map %d mode %d: %d frames, cameras (%d,%d) (%d,%d)\n", map, mode,
           shot.frame_no, game.camera[0].x, game.camera[0].y,
           game.camera[1].x, game.camera[1].y);

    return rc;
}
