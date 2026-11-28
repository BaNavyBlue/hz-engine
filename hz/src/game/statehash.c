/* (Session 56) statehash.h: the state's hash, from tools/hz_netcheck.c
 * (session 52) unchanged. (Session 61) Its images are the 68k's layout
 * (romcodes.h), so a game with the original's 50 units a side hashes as
 * before; a side grown past 50 (UNITS) adds the rest (hash_grown). */

#include "statehash.h"

#include "romcodes.h"

#include <string.h>

u64 state_fnv(u64 h, const void *data, size_t n)
{
    const u8 *p = data;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * STATE_FNV_PRIME;
    }
    return h;
}

/* -- the state images, hz_demo's (tools/hz_demo.c: fill_*) -- */

#define GLOBALS_BYTES 0x40
#define MECH_BYTES    0x80
#define UNIT_BYTES    0x38
#define POST_BYTES    8
#define SHOT_BYTES    0x19

static void wr8(u8 *r, u32 a, u8 v)
{
    r[a] = v;
}

static void wr16(u8 *r, u32 a, u16 v)
{
    r[a] = (u8)(v >> 8);
    r[a + 1] = (u8)v;
}

static void wr32(u8 *r, u32 a, u32 v)
{
    wr16(r, a, (u16)(v >> 16));
    wr16(r, a + 2, (u16)v);
}

static void fill_globals(const HzGame *g, u8 *r)
{
    memset(r, 0, GLOBALS_BYTES);
    wr32(r, 0x00, g->rng_seed);
    wr32(r, 0x04, g->frame_counter);
    wr16(r, 0x08, g->second_timer);
    wr8(r, 0x0A, (u8)g->unit_count[0]);
    wr8(r, 0x0B, (u8)g->unit_count[1]);
    for (int i = 0; i < 4; i++) {
        wr8(r, 0x0C + (u32)i, g->outpost_count[i]);
    }
    for (int p = 0; p < 2; p++) {
        wr16(r, 0x10 + (u32)p * 4, (u16)g->camera[p].x);
        wr16(r, 0x12 + (u32)p * 4, (u16)g->camera[p].y);
    }
    wr16(r, 0x18, (u16)g->scroll_x[0]);
    wr16(r, 0x1A, (u16)g->scroll_x[1]);
    wr16(r, 0x1C, (u16)g->scroll_y[0]);
    wr16(r, 0x1E, (u16)g->scroll_y[1]);
    wr16(r, 0x20, g->unit_ai_group);
    wr8(r, 0x22, g->joypad[0]);
    wr8(r, 0x23, g->joypad[1]);
    wr8(r, 0x24, g->sfx_priority);
    wr8(r, 0x25, g->sfx_queued);
    wr16(r, 0x26, g->split_enable);
    wr8(r, 0x28, g->split_done);
    wr16(r, 0x2A, (u16)g->game_type);
    wr16(r, 0x2C, g->demo_index);
    for (int i = 0; i < 4; i++) {
        wr8(r, 0x30 + (u32)i, g->options[i]);
    }
    wr16(r, 0x34, g->winner);
}

static void fill_mech(const HzMech *m, u8 *r)
{
    memset(r, 0, MECH_BYTES);
    wr8(r, 0x00, m->flags);
    wr8(r, 0x01, (u8)m->carried_slot);
    wr8(r, 0x02, m->carried_order);
    wr8(r, 0x03, m->dir);
    wr8(r, 0x04, m->turn_to);
    wr8(r, 0x06, m->transform);
    wr8(r, 0x07, m->transform_delay);
    wr8(r, 0x08, m->power);
    wr8(r, 0x09, rom_victim(m->hit_victim));
    wr16(r, 0x0A, m->hp);
    wr8(r, 0x0C, m->size);
    wr8(r, 0x0D, m->altitude);
    wr8(r, 0x0E, rom_bumped(m->bumped));
    wr8(r, 0x0F, m->fuel_use);
    wr16(r, 0x10, (u16)m->x);
    wr16(r, 0x12, (u16)m->y);
    wr16(r, 0x14, m->sub_x);
    wr16(r, 0x16, m->sub_y);
    wr8(r, 0x18, m->missiles);
    wr8(r, 0x19, m->ammo);
    wr16(r, 0x1A, m->fuel);
    wr16(r, 0x1C, m->terrain);
    wr16(r, 0x1E, m->frame);
    wr16(r, 0x20, m->frame_loaded);
    wr8(r, 0x22, m->shot_flags);
    wr8(r, 0x23, m->blocked);
    wr8(r, 0x25, (u8)m->speed);
    wr8(r, 0x27, m->max_speed);
    wr8(r, 0x28, m->pad);
    wr8(r, 0x29, m->pad_prev);
    wr8(r, 0x2A, m->lead);
    wr8(r, 0x2B, m->terrain_flow);
    wr16(r, 0x2C, m->form);
    wr8(r, 0x2E, m->pad_last);
    wr8(r, 0x2F, m->dir_held);
    wr16(r, 0x30, (u16)m->vel_x);
    wr16(r, 0x32, (u16)m->vel_y);
    wr16(r, 0x34, (u16)m->target_vel_x);
    wr16(r, 0x36, (u16)m->target_vel_y);
    wr16(r, 0x38, (u16)m->screen_x);
    wr16(r, 0x3A, (u16)m->screen_y);
    wr16(r, 0x3C, (u16)m->cam_dx);
    wr16(r, 0x3E, (u16)m->cam_dy);
    wr8(r, 0x40, m->menu.step);
    wr8(r, 0x42, (u8)m->view);
    wr8(r, 0x43, m->view_index);
    wr8(r, 0x4F, m->prod_state);
    wr16(r, 0x54, m->money);
    wr8(r, 0x5E, m->prod_type);
    wr8(r, 0x5F, m->prod_order);
    wr16(r, 0x60, m->prod_timer);
    wr8(r, 0x62, m->hp_shown);
    wr8(r, 0x63, m->hq_hp_shown);
    wr16(r, 0x64, m->option2);
    wr8(r, 0x66, m->ammo_shown);
    wr8(r, 0x67, m->fuel_shown);
    wr8(r, 0x68, m->carried_refill);
    wr8(r, 0x69, m->fire_delay);
    wr16(r, 0x6A, m->missile_dirs);
    wr8(r, 0x6C, m->hq_alert);
    wr8(r, 0x6E, m->fuel_count);
    wr8(r, 0x6F, m->walk_step);
    wr16(r, 0x70, (u16)m->home_cam.x);
    wr16(r, 0x72, (u16)m->home_cam.y);
    wr16(r, 0x74, (u16)m->home_screen.x);
    wr16(r, 0x76, (u16)m->home_screen.y);
}

static void fill_unit(const HzUnit *u, u8 *r)
{
    memset(r, 0, UNIT_BYTES);
    wr8(r, 0x00, u->flags);
    wr8(r, 0x01, u->type);
    wr8(r, 0x02, u->order);
    wr8(r, 0x03, u->dir);
    wr8(r, 0x04, u->turn_to);
    wr8(r, 0x05, u->tick);
    wr8(r, 0x06, u->phase);
    wr8(r, 0x07, u->fire_delay);
    wr8(r, 0x08, u->power);
    wr8(r, 0x09, rom_victim(u->hit_victim));
    wr16(r, 0x0A, u->hp);
    wr8(r, 0x0C, u->size);
    wr8(r, 0x0D, u->raw_0d);
    wr8(r, 0x0E, rom_bumped(u->bumped));
    wr8(r, 0x0F, u->fuel_use);
    wr16(r, 0x10, (u16)u->x);
    wr16(r, 0x12, (u16)u->y);
    wr16(r, 0x14, u->sub_x);
    wr16(r, 0x16, u->sub_y);
    wr8(r, 0x18, u->missiles);
    wr8(r, 0x19, u->ammo);
    wr16(r, 0x1A, u->fuel);
    wr16(r, 0x1C, u->terrain);
    wr16(r, 0x1E, u->sprite_attr);
    wr16(r, 0x20, u->sprite_size);
    wr8(r, 0x22, u->shot_flags);
    wr8(r, 0x23, u->blocked);
    wr16(r, 0x24, u->speed);
    wr8(r, 0x26, u->prev_speed);
    wr8(r, 0x27, u->max_speed);
    wr8(r, 0x28, u->turret);
    wr8(r, 0x29, u->turret_dir);
    wr8(r, 0x2A, u->aim_dir);
    wr8(r, 0x2B, u->push);
    wr16(r, 0x2C, u->overlay_attr);
    wr16(r, 0x2E, u->grid_cell);
    wr16(r, 0x30, (u16)u->vel_x);
    wr16(r, 0x32, (u16)u->vel_y);
    wr16(r, 0x34, (u16)u->target_vel_x);
    wr16(r, 0x36, (u16)u->target_vel_y);
}

static void fill_post(const HzPost *p, u8 *r)
{
    wr8(r, 0, p->flags);
    wr8(r, 1, p->b1);
    wr8(r, 2, p->hold[0]);
    wr8(r, 3, p->hold[1]);
    wr16(r, 4, (u16)p->x);
    wr16(r, 6, (u16)p->y);
}

static void fill_shot(const HzShot *t, u8 *r)
{
    memset(r, 0, SHOT_BYTES);
    wr8(r, 0x00, t->flags);
    wr8(r, 0x01, t->dir);
    wr16(r, 0x02, (u16)t->x);
    wr16(r, 0x04, (u16)t->y);
    wr16(r, 0x06, t->speed);
    wr16(r, 0x08, t->sub_x);
    wr16(r, 0x0A, t->sub_y);
    wr16(r, 0x0C, t->life);
    wr16(r, 0x0E, t->attr);
    wr16(r, 0x10, (u16)t->vel_x);
    wr16(r, 0x12, (u16)t->vel_y);
    wr16(r, 0x14, (u16)t->aim_x);
    wr16(r, 0x16, (u16)t->aim_y);
    wr8(r, 0x18, t->power);
}

/* The images are big: kept here rather than on the stack. */
static u8 g_globals[GLOBALS_BYTES];
static u8 g_mechs[2][MECH_BYTES];
static u8 g_units[HZ_UNITS][UNIT_BYTES];
static u8 g_posts[HZ_POST_CLEARED][POST_BYTES];
static u8 g_shots[HZ_SHOTS][SHOT_BYTES];
static u8 g_grid[HZ_GRID * HZ_GRID];

/* The 68k's two tables: each side's first 50 slots. */
static void fill_units(const HzGame *g)
{
    for (int side = 0; side < 2; side++) {
        for (u32 slot = 0; slot < ROM_SIDE_UNITS; slot++) {
            fill_unit(unit_at_c(g, side, slot),
                      g_units[rom_unit_index(side, slot)]);
        }
    }
}

static u64 fnv_ref(u64 h, HzRef r)
{
    u8 b[6] = { r.kind, r.side, (u8)(r.slot >> 24), (u8)(r.slot >> 16),
                (u8)(r.slot >> 8), (u8)r.slot };
    return state_fnv(h, b, sizeof b);
}

/* (Session 61) A side grown past 50 slots (UNITS): what the 68k's images
 * can't hold -- the slots past 50, and every reference whole (the images'
 * codes have no room for a slot past 49) -- on to the units' hash. A game
 * whose sides never grew hashes as it always did. */
static u64 hash_grown(const HzGame *g, u64 h, bool display)
{
    if (side_slots(g, 0) <= ROM_SIDE_UNITS
        && side_slots(g, 1) <= ROM_SIDE_UNITS) {
        return h;
    }
    u8 r[UNIT_BYTES];
    for (int side = 0; side < 2; side++) {
        u32 slots = side_slots(g, side);
        u8 n[4] = { (u8)(slots >> 24), (u8)(slots >> 16), (u8)(slots >> 8),
                    (u8)slots };
        h = state_fnv(h, n, sizeof n);
        for (u32 slot = 0; slot < slots; slot++) {
            const HzUnit *u = unit_at_c(g, side, slot);
            if (slot >= ROM_SIDE_UNITS) {
                fill_unit(u, r);
                if (!display) {
                    r[0] &= (u8)~UNIT_ON_SCREEN;
                }
                h = state_fnv(h, r, sizeof r);
            }
            h = fnv_ref(h, u->hit_victim);
            h = fnv_ref(h, u->bumped);
        }
    }
    for (int p = 0; p < 2; p++) {
        u8 c[4] = { (u8)(g->mech[p].carried_slot >> 24),
                    (u8)(g->mech[p].carried_slot >> 16),
                    (u8)(g->mech[p].carried_slot >> 8),
                    (u8)g->mech[p].carried_slot };
        h = state_fnv(h, c, sizeof c);
        h = fnv_ref(h, g->mech[p].hit_victim);
        h = fnv_ref(h, g->mech[p].bumped);
    }
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        h = fnv_ref(h, g->grid[i]);
    }
    for (int i = 0; i < 2; i++) {
        u8 c[4] = { (u8)(g->unit_count[i] >> 24), (u8)(g->unit_count[i] >> 16),
                    (u8)(g->unit_count[i] >> 8), (u8)g->unit_count[i] };
        h = state_fnv(h, c, sizeof c);
    }
    return h;
}

/* (Session 59) The game without its display: what each machine of an
 * own-view network game draws and plays differs (HzGame.own_view: each
 * shows its own side), and a replay's game starts from a cold boot while
 * the game it recorded started from the title. Left out (zeros): the
 * planes' scroll and the effect queued, each mech's view, its sprite
 * frame's upload, the HUD's shown values and the HQ alert's count (the
 * sprites count it down for a shown view), what is on screen (units' and
 * shots' b6), and the VDP; after the game's end (v_game_over set) the
 * mechs and cameras too, which the finale places by the view
 * (game_over.c). */
static void leave_out_display(const HzGame *g, u64 part[STATE_HASH_PARTS])
{
    if (g->game_over != 0) {
        memset(g_globals + 0x10, 0, 8);
    }
    memset(g_globals + 0x18, 0, 8);
    g_globals[0x24] = 0;
    g_globals[0x25] = 0;
    for (int p = 0; p < 2; p++) {
        u8 *r = g_mechs[p];
        if (g->game_over != 0) {
            memset(r, 0, MECH_BYTES);
            continue;
        }
        r[0x20] = r[0x21] = 0;
        r[0x42] = r[0x43] = 0;
        r[0x62] = r[0x63] = r[0x66] = r[0x67] = 0;
        r[0x6C] = 0;
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        g_units[i][0] &= (u8)~UNIT_ON_SCREEN;
    }
    for (int i = 0; i < HZ_SHOTS; i++) {
        g_shots[i][0] &= (u8)~SHOT_ON_SCREEN;
    }
    for (int i = 6; i < STATE_HASH_PARTS; i++) {
        part[i] = 0;
    }
}

/* (Half B of docs/hackable.md's step 1) What the draw layer holds, a part
 * each for its tiles and cells, its colours, its scroll and layout, and
 * its sprites, written out big-endian a field at a time (no struct's
 * padding or byte order in it). */
static u64 fnv16(u64 h, u16 w)
{
    const u8 b[2] = { (u8)(w >> 8), (u8)w };
    return state_fnv(h, b, 2);
}

static u64 fnv_sprite(u64 h, const Sprite *s)
{
    const u8 b[11] = {
        (u8)((u16)s->x >> 8), (u8)s->x, (u8)((u16)s->y >> 8), (u8)s->y,
        (u8)(s->tile >> 8), (u8)s->tile, s->w, s->h,
        (u8)(s->pal | (s->prio << 2) | (s->flip_h << 3) | (s->flip_v << 4)),
        s->colours, 0
    };
    return state_fnv(h, b, sizeof b);
}

static void hash_display(const Video *v, u64 part[STATE_HASH_PARTS])
{
    u64 h = state_fnv(STATE_FNV_BASIS, v->tiles, sizeof v->tiles);
    for (int l = 0; l < LAYER_COUNT; l++) {
        for (int r = 0; r < PLANE_ROWS; r++) {
            for (int c = 0; c < PLANE_COLS; c++) {
                h = fnv16(h, v->cells[l][r][c]);
            }
        }
    }
    part[6] = h;
    h = STATE_FNV_BASIS;
    for (int i = 0; i < VIDEO_COLOURS; i++) {
        h = fnv16(h, v->colours[i]);
    }
    part[7] = h;
    h = STATE_FNV_BASIS;
    for (int l = 0; l < LAYER_COUNT; l++) {
        h = fnv16(h, (u16)v->scroll_x[l]);
        h = fnv16(h, (u16)v->scroll_y[l]);
    }
    h = fnv16(h, (u16)((v->layout << 1) | v->display));
    part[8] = h;
    h = STATE_FNV_BASIS;
    for (int i = 0; i < v->sprite_count; i++) {
        h = fnv_sprite(h, &v->sprite[i]);
    }
    h = fnv16(h, (u16)v->sprite_count);
    h = fnv16(h, (u16)v->game_at);
    h = fnv16(h, (u16)((v->held << 15) | (v->back_at & 0x7FFF)));
    for (u32 i = 0; i < v->behind_count; i++) {
        h = fnv_sprite(h, &v->behind[i]);
    }
    h = fnv16(h, v->sprite_colours_on);
    for (int n = 1; n < 16; n++) {
        if ((v->sprite_colours_on >> n) & 1) {
            h = state_fnv(h, v->sprite_colours[n], 16);
        }
    }
    h = fnv16(h, v->divider ? cell_word(v->divider_cell) : 0);  /* (s. 81) */
    part[9] = h;
}

static void hash_parts(const HzGame *g, u64 part[STATE_HASH_PARTS],
                       bool display)
{
    fill_globals(g, g_globals);
    for (int p = 0; p < 2; p++) {
        fill_mech(&g->mech[p], g_mechs[p]);
    }
    fill_units(g);
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        g_grid[i] = rom_grid_id(g->grid[i]);
    }
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        fill_post(&g->posts[i], g_posts[i]);
    }
    for (int i = 0; i < HZ_SHOTS; i++) {
        fill_shot(&g->shots[i], g_shots[i]);
    }
    part[0] = state_fnv(STATE_FNV_BASIS, g_globals, sizeof g_globals);
    part[1] = state_fnv(STATE_FNV_BASIS, g_mechs, sizeof g_mechs);
    part[2] = state_fnv(STATE_FNV_BASIS, g_units, sizeof g_units);
    part[2] = hash_grown(g, part[2], true);
    part[3] = state_fnv(STATE_FNV_BASIS, g_posts, sizeof g_posts);
    part[4] = state_fnv(STATE_FNV_BASIS, g_shots, sizeof g_shots);
    part[5] = state_fnv(STATE_FNV_BASIS, g_grid, sizeof g_grid);
    hash_display(g->video, part);
    if (!display) {
        leave_out_display(g, part);
        part[0] = state_fnv(STATE_FNV_BASIS, g_globals, sizeof g_globals);
        part[1] = state_fnv(STATE_FNV_BASIS, g_mechs, sizeof g_mechs);
        part[2] = state_fnv(STATE_FNV_BASIS, g_units, sizeof g_units);
        part[2] = hash_grown(g, part[2], false);
        part[4] = state_fnv(STATE_FNV_BASIS, g_shots, sizeof g_shots);
    }
}

/* (Session 66) From a network game's end on (v_game_over set) each
 * machine shows its own end (game_end.c: YOU WIN or YOU LOSE), so the
 * display is left out then too. */
void state_hash_parts(const HzGame *g, u64 part[STATE_HASH_PARTS])
{
    hash_parts(g, part, g->own_view == 0
                        && !(g->net_player != 0 && g->game_over != 0));
}

u64 state_hash_fold(u64 h, const u64 *part, int parts)
{
    for (int i = 0; i < parts; i++) {
        u8 b[8];
        wr32(b, 0, (u32)(part[i] >> 32));
        wr32(b, 4, (u32)part[i]);
        h = state_fnv(h, b, 8);
    }
    return h;
}

u64 state_hash(const HzGame *g)
{
    u64 part[STATE_HASH_PARTS];
    state_hash_parts(g, part);
    return state_hash_fold(STATE_FNV_BASIS, part, STATE_HASH_PARTS);
}

u64 state_hash_game(const HzGame *g)
{
    u64 part[STATE_HASH_PARTS];
    hash_parts(g, part, false);
    return state_hash_fold(STATE_FNV_BASIS, part, STATE_HASH_PARTS);
}
