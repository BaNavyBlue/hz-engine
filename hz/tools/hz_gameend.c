/* hz_gameend: the game's end (src/game/game_end.c: WINNER, the results)
 * and the endings (src/game/ending.c) run on a script, for
 * tools/hzgameend_check.py, which runs the original MenuModule_GameEnd on
 * tools/m68krun.py with the same script and compares.
 *
 *     hz_gameend ASSETS SCRIPT OUT [--dump FRAME VRAM_OUT]
 *
 * SCRIPT (text): a line "S hw options0-3 win0-3 winner frames bases0-3
 * units0 units1 spawns0 spawns1 seed" (hex) with the state Game_Over leaves
 * (v_hw_version, v_options, v_win_record, v_winner, v_frame_counter,
 * v_results_bases, v_units_built, v_mech_spawns) and the seed of VRAM's
 * contents as the game left them (xorshift32, the check makes the same);
 * then a pad byte per frame (hex): the pads OR'd that the reads after that
 * frame see (the HZ_PAD_* bits). The run stops after the script's last
 * frame or when MenuModule_GameEnd returns.
 *
 * OUT (text): per frame handed to the host "F n vram-crc cram vsram-crc
 * regs"; per sound command "M n bytes"; at the end, if it returned, "E
 * options win" as it leaves them. --dump writes all of VRAM as it stands
 * at frame FRAME (0-based) to VRAM_OUT. The win is saved where $HZ_SAVES
 * says (the check gives it a scratch folder). */

#include "video_vdp.h"
#include "../src/game/front.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 30000

typedef struct {
    int  frames;
    u8   pad[MAX_FRAMES];
    int  done;              /* frames handed so far */
    FILE *out;
    int  dump_frame;
    const char *dump_path;
} Run;

/* (Half B of docs/hackable.md's step 1) The cartridge's video memory as
 * the check compares it: the draw layer's changes laid out by
 * tools/video_vdp.c. */
static VideoVdp g_cart;

static void on_frame(HzGame *g, void *user)
{
    Run *r = user;
    fprintf(r->out, "F %d ", r->done);
    video_vdp_frame_line(&g_cart, g->video, r->out);
    fputc('\n', r->out);
    if (r->dump_path != NULL && r->done == r->dump_frame) {
        FILE *f = fopen(r->dump_path, "wb");
        if (f != NULL) {
            static u8 vram[VDP_VRAM_SIZE];
            video_vdp_vram(&g_cart, vram);
            fwrite(vram, 1, VDP_VRAM_SIZE, f);
            fclose(f);
        }
    }

    u8 b = r->done < r->frames ? r->pad[r->done] : 0;
    HzPad *p = &g->pad_in[0];
    memset(g->pad_in, 0, sizeof g->pad_in);
    p->up = (b & HZ_PAD_UP) != 0;
    p->down = (b & HZ_PAD_DOWN) != 0;
    p->left = (b & HZ_PAD_LEFT) != 0;
    p->right = (b & HZ_PAD_RIGHT) != 0;
    p->b = (b & HZ_PAD_B) != 0;
    p->c = (b & HZ_PAD_C) != 0;
    p->a = (b & HZ_PAD_A) != 0;
    p->start = (b & HZ_PAD_START) != 0;
    r->done++;
    if (r->done >= r->frames) {
        g->quit = true;
    }
}

static Run g_run;

static void on_sound(void *user, const u8 *bytes, int count)
{
    Run *r = user;
    if (r->done >= r->frames) {
        return;                 /* past the script: the run has stopped */
    }
    fprintf(r->out, "M %d", r->done);
    for (int i = 0; i < count; i++) {
        fprintf(r->out, " %02X", bytes[i]);
    }
    fputc('\n', r->out);
}

static bool read_hex(FILE *f, unsigned *v)
{
    return fscanf(f, "%x", v) == 1;
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 7) {
        fprintf(stderr, "usage: hz_gameend ASSETS SCRIPT OUT "
                        "[--dump FRAME VRAM_OUT]\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    Run *r = &g_run;
    r->dump_frame = -1;
    if (argc == 7) {
        r->dump_frame = atoi(argv[5]);
        r->dump_path = argv[6];
    }
    FILE *in = fopen(argv[2], "r");
    r->out = fopen(argv[3], "w");
    if (in == NULL || r->out == NULL) {
        fprintf(stderr, "hz_gameend: can't open the script or OUT\n");
        return 1;
    }

    static Video video;
    static Vdp vdp;
    static HzGame game;
    HzGame *g = &game;
    video_init(&video);
    vdp_reset(&vdp);
    video_vdp_watch(&g_cart, &video, &vdp);
    game_init(g, &assets, &video);
    g->frame = on_frame;
    g->frame_user = r;
    g->snd_post = on_sound;
    g->snd_user = r;

    char tag[4];
    unsigned v[19];
    if (fscanf(in, "%3s", tag) != 1 || strcmp(tag, "S") != 0) {
        fprintf(stderr, "hz_gameend: the script's S line\n");
        return 1;
    }
    for (int i = 0; i < 19; i++) {
        if (!read_hex(in, &v[i])) {
            fprintf(stderr, "hz_gameend: the S line needs 19 numbers\n");
            return 1;
        }
    }
    g->hw_version = (u16)v[0];
    for (int i = 0; i < 4; i++) {
        g->options[i] = (u8)v[1 + i];
        g->win_record[i] = (u8)v[5 + i];
        g->results_bases[i] = (u8)v[11 + i];
    }
    g->winner = (u16)v[9];
    g->frame_counter = v[10];
    g->units_built[0] = (u16)v[15];
    g->units_built[1] = (u16)v[16];
    g->mech_spawns[0] = (u16)v[17];
    g->mech_spawns[1] = (u16)v[18];
    unsigned seed;
    if (!read_hex(in, &seed)) {
        fprintf(stderr, "hz_gameend: the S line's seed\n");
        return 1;
    }
    u32 x = seed ? seed : 1;            /* xorshift32 */
    for (u32 i = 0; i < VDP_VRAM_SIZE; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        vdp.vram[i] = (u8)x;
    }
    unsigned pad;
    while (r->frames < MAX_FRAMES && read_hex(in, &pad)) {
        r->pad[r->frames++] = (u8)pad;
    }
    fclose(in);
    if (r->frames == 0) {
        g->quit = true;
    }

    menu_game_end(g);
    if (!g->quit) {
        fprintf(r->out, "E");
        for (int i = 0; i < 4; i++) {
            fprintf(r->out, " %02X", g->options[i]);
        }
        for (int i = 0; i < 4; i++) {
            fprintf(r->out, " %02X", g->win_record[i]);
        }
        fputc('\n', r->out);
    }
    fclose(r->out);
    return 0;
}
