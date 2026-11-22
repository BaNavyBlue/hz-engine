/* hz_demo: an attract demo, the whole of it, for tools/hzdemo_check.py,
 * which runs the original from the same point on tools/m68krun.py and
 * compares frame by frame.
 *
 *     hz_demo ASSETS INDEX OUT [--pads FILE] [--frames N]
 *                              [--state FRAME STATE_OUT]
 *
 * The run starts where MainLoop_Title is after MenuModule_Title has
 * returned -1 (the title ran out), with v_demo_index = INDEX (0-15) and
 * RAM as a cold boot and the title leave it: Game_SetupMode (the demo's
 * path), VDP_Init, Game_LoadMap with the loading screen, Game_Start and
 * the frame loop, until the loop leaves (the demo over: back to the
 * title), the load is aborted, or N frames (default 5000).
 *
 * --pads FILE: a pad byte per frame (hex, whitespace between): what
 * ReadJoypads gives after that frame (HZ_PAD_* bits, both pads); 0 past
 * the end. Start in it ends a demo, or aborts its load.
 *
 * OUT (text), per frame handed to the host:
 *   F n vram-crc cram vsram-crc regs      (as hz_gameend's)
 *   R n globals-crc mechs-crc units-crc posts-crc grid-crc shots-crc
 * per sound command "M n bytes", and at the end "E how frames" (how: title,
 * game_over, aborted, frames). The R line's CRCs are over 68k-layout
 * images of the state (fill_*, below): only the fields listed, the rest of
 * each image 0, so the check builds the same images from the original's
 * RAM; and of v_grid as it is, and the 64 shots' +$00-$18. --state writes those images at frame FRAME (0-based) to STATE_OUT,
 * so the check can name the fields that differ. */

#include "../src/core/hz_vdp.h"
#include "../src/game/game.h"
#include "../src/game/romcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 20000

#define GLOBALS_BYTES 0x40
#define MECH_BYTES    0x80        /* +$00-$7F of each $100 record */
#define UNIT_BYTES    0x38        /* +$00-$37 of each $50 record (the AI's
                                     +$38-$4F are left out) */
#define POST_BYTES    8

typedef struct {
    int   frames;
    u8    pad[MAX_FRAMES];
    int   limit;
    int   done;
    FILE *out;
    int   state_frame;
    const char *state_path;
} Run;

static u32 crc32_buf(const u8 *p, size_t n)
{
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

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

/* The globals compared, at offsets in a small image (the check lists the
 * same with their RAM addresses): v_rng_seed, v_frame_counter,
 * v_second_timer, v_unit_count, v_outpost_count, v_camera1/2,
 * v_scroll_x_a/b, _y_a/b, v_unit_ai_group, v_joypad1/2, v_sfx_priority,
 * v_sfx_queued, v_split_enable, v_split_done, v_game_type, v_demo_index. */
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
}

/* A mech record's +$00-$7F as the 68k lays them out (the order menu's
 * own fields but its step left out: make ordermenu checks them). */
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

/* A unit record's +$00-$37. */
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

/* A shot record's +$00-$18 (the owner, a pointer on the 68k, left out). */
#define SHOT_BYTES 0x19

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

static u8 g_globals[GLOBALS_BYTES];
static u8 g_shots[HZ_SHOTS][SHOT_BYTES];
static u8 g_mechs[2][MECH_BYTES];
static u8 g_units[HZ_UNITS][UNIT_BYTES];
static u8 g_posts[HZ_POST_CLEARED][POST_BYTES];
static u8 g_grid[HZ_GRID * HZ_GRID];

static void fill_state(const HzGame *g)
{
    fill_globals(g, g_globals);
    for (int p = 0; p < 2; p++) {
        fill_mech(&g->mech[p], g_mechs[p]);
    }
    for (int i = 0; i < HZ_UNITS; i++) {
        fill_unit(rom_unit_c(g, i), g_units[i]);
    }
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        g_grid[i] = rom_grid_id(g->grid[i]);
    }
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        fill_post(&g->posts[i], g_posts[i]);
    }
    for (int i = 0; i < HZ_SHOTS; i++) {
        fill_shot(&g->shots[i], g_shots[i]);
    }
}

static void on_frame(HzGame *g, void *user)
{
    Run *r = user;
    Vdp *v = g->vdp;
    static u8 screen[SCREEN_W * SCREEN_H];
    vdp_render(v, screen);          /* applies HBlank's write, as shown */
    u8 vs[VDP_VSRAM_WORDS * 2];
    for (int i = 0; i < VDP_VSRAM_WORDS; i++) {
        vs[i * 2] = (u8)(v->vsram[i] >> 8);
        vs[i * 2 + 1] = (u8)v->vsram[i];
    }
    fprintf(r->out, "F %d %08X ", r->done,
            (unsigned)crc32_buf(v->vram, VDP_VRAM_SIZE));
    for (int i = 0; i < VDP_CRAM_WORDS; i++) {
        fprintf(r->out, "%03X", v->cram[i] & 0x0EEE);
    }
    fprintf(r->out, " %08X ", (unsigned)crc32_buf(vs, sizeof vs));
    for (int i = 0; i < VDP_REG_COUNT; i++) {
        fprintf(r->out, "%02X", v->reg[i]);
    }
    fputc('\n', r->out);

    fill_state(g);
    fprintf(r->out, "R %d %08X %08X %08X %08X %08X %08X\n", r->done,
            (unsigned)crc32_buf(g_globals, sizeof g_globals),
            (unsigned)crc32_buf(&g_mechs[0][0], sizeof g_mechs),
            (unsigned)crc32_buf(&g_units[0][0], sizeof g_units),
            (unsigned)crc32_buf(&g_posts[0][0], sizeof g_posts),
            (unsigned)crc32_buf(g_grid, sizeof g_grid),
            (unsigned)crc32_buf(&g_shots[0][0], sizeof g_shots));
    if (r->state_path != NULL && r->done == r->state_frame) {
        FILE *f = fopen(r->state_path, "wb");
        if (f != NULL) {
            fwrite(g_globals, 1, sizeof g_globals, f);
            fwrite(g_mechs, 1, sizeof g_mechs, f);
            fwrite(g_units, 1, sizeof g_units, f);
            fwrite(g_posts, 1, sizeof g_posts, f);
            fwrite(g_grid, 1, sizeof g_grid, f);
            fwrite(g_shots, 1, sizeof g_shots, f);
            fwrite(v->vram, 1, VDP_VRAM_SIZE, f);
            fclose(f);
        }
    }

    u8 b = r->done < r->frames ? r->pad[r->done] : 0;
    memset(g->pad_in, 0, sizeof g->pad_in);
    HzPad *p = &g->pad_in[0];
    p->up = (b & HZ_PAD_UP) != 0;
    p->down = (b & HZ_PAD_DOWN) != 0;
    p->left = (b & HZ_PAD_LEFT) != 0;
    p->right = (b & HZ_PAD_RIGHT) != 0;
    p->b = (b & HZ_PAD_B) != 0;
    p->c = (b & HZ_PAD_C) != 0;
    p->a = (b & HZ_PAD_A) != 0;
    p->start = (b & HZ_PAD_START) != 0;
    r->done++;
    if (r->done >= r->limit) {
        g->quit = true;
    }
}

static Run g_run;

static void on_sound(void *user, const u8 *bytes, int count)
{
    Run *r = user;
    fprintf(r->out, "M %d", r->done);
    for (int i = 0; i < count; i++) {
        fprintf(r->out, " %02X", bytes[i]);
    }
    fputc('\n', r->out);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: hz_demo ASSETS INDEX OUT [--pads FILE] "
                        "[--frames N] [--state FRAME STATE_OUT]\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    Run *r = &g_run;
    r->limit = 5000;
    r->state_frame = -1;
    int index = atoi(argv[2]);
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--pads") == 0 && i + 1 < argc) {
            FILE *in = fopen(argv[++i], "r");
            unsigned pad;
            if (in == NULL) {
                fprintf(stderr, "hz_demo: can't read %s\n", argv[i]);
                return 1;
            }
            while (r->frames < MAX_FRAMES && fscanf(in, "%x", &pad) == 1) {
                r->pad[r->frames++] = (u8)pad;
            }
            fclose(in);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            r->limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--state") == 0 && i + 2 < argc) {
            r->state_frame = atoi(argv[++i]);
            r->state_path = argv[++i];
        } else {
            fprintf(stderr, "hz_demo: what is %s?\n", argv[i]);
            return 2;
        }
    }
    r->out = fopen(argv[3], "w");
    if (r->out == NULL) {
        fprintf(stderr, "hz_demo: can't write %s\n", argv[3]);
        return 1;
    }

    static Vdp vdp;
    static HzGame game;
    HzGame *g = &game;
    vdp_reset(&vdp);
    game_init(g, &assets, &vdp);
    /* A cold boot's v_options (mode 5, v_option2 $FF) and Snd_Init, then
     * MainLoop_Title up to MenuModule_Title; its sound isn't logged (the
     * check starts after it). */
    g->hw_version = 0xA0;
    g->options[0] = 5;
    g->options[2] = 0xFF;
    game_title_prepare(g);
    g->frame = on_frame;
    g->frame_user = r;
    g->snd_post = on_sound;
    g->snd_user = r;
    g->game_type = -1;              /* MenuModule_Title's d0: the demo */
    g->demo_index = (u16)index;

    const char *how = "frames";
    HzSetup setup = game_setup_after_title(g);
    if (setup == HZ_SETUP_ABORTED) {
        how = "aborted";
    } else if (setup == HZ_SETUP_GAME && !g->quit) {
        game_start(g);
        HzLoopExit exit = game_frame(g, false);
        while (exit == HZ_LOOP_ON) {
            exit = game_frame(g, true);
        }
        if (exit == HZ_LOOP_TITLE) {
            how = "title";
        } else if (exit == HZ_LOOP_GAME_OVER) {
            how = "game_over";
        }
    }
    fprintf(r->out, "E %s %d\n", how, r->done);
    fclose(r->out);
    return 0;
}
