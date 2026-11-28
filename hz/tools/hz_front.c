/* hz_front: the title and options screen (src/game/front.c, title.c,
 * options.c) run on a script, for tools/hzfront_check.py, which runs the
 * original Menu_TitleFlow on tools/m68krun.py with the same script and
 * compares.
 *
 *     hz_front ASSETS SCRIPT OUT [--dump FRAME VRAM_OUT]
 *
 * SCRIPT (text): a line "S hw options0-3 win0-3" (hex bytes) with the
 * state before MenuModule_Title; optionally "I win0-3": a record put in
 * place each time the map select starts (as loading a saved game leaves
 * one, with v_option_map its first map not won on every difficulty; the
 * check does the same to the original); then a line per frame "pad
 * music" (hex): the pads OR'd that the reads after that frame see (the
 * HZ_PAD_* bits), and whether the title song still plays (z_music_on
 * when Title_WaitStart asks, after that many frames). The run stops after
 * the script's last frame, when the flow returns, or when CONT is chosen
 * (the saved games' page, the port's own: the check stops the original as
 * it enters the password's).
 *
 * OUT (text): per frame handed to the host "F n vram-crc cram vsram-crc
 * regs" (the state as the frame shows it); per sound command "M n bytes"
 * (n frames gone by); "C n" if CONT was chosen; at the end, if the flow
 * returned, "E result options win sfx-queue". --dump writes all of VRAM
 * as it stands at frame FRAME (0-based) to VRAM_OUT.
 *
 *     hz_front ASSETS --load W0 W1 W2 W3 HUMAN OPTIONS0
 *
 * loads a saved game with that record and side (options_load_save) over
 * that mode byte and prints "options win" as it leaves them. */

#include "video_vdp.h"
#include "../src/game/front.h"
#include "../src/game/tick.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 20000

typedef struct {
    int  frames;
    u8   pad[MAX_FRAMES];
    u8   music[MAX_FRAMES];
    bool inject;
    u8   inject_record[4];
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

static void on_page(HzGame *g, int page, void *user)
{
    Run *r = user;
    if (page == PAGE_MAP_SELECT && r->inject) {
        memcpy(g->win_record, r->inject_record, 4);
        g->options[3] = (u8)saves_next_map(r->inject_record);
    }
    if (page == PAGE_SAVES) {
        fprintf(r->out, "C %d\n", r->done);
        g->quit = true;
        r->done = r->frames;            /* nothing more is logged */
    }
}

static bool on_music(void *user)
{
    Run *r = user;
    return r->done < r->frames ? r->music[r->done] != 0 : true;
}

static bool read_hex(FILE *f, unsigned *v)
{
    return fscanf(f, "%x", v) == 1;
}

int main(int argc, char **argv)
{
    if (argc == 9 && strcmp(argv[2], "--load") == 0) {
        static HzAssets la;
        static Video lv;
        static HzGame lg;
        if (!hz_assets_load(&la, argv[1])) {
            return 1;
        }
        video_init(&lv);
        game_init(&lg, &la, &lv);
        HzSaveSlot slot;
        memset(&slot, 0, sizeof slot);
        slot.used = true;
        for (int i = 0; i < 4; i++) {
            slot.record[i] = (u8)strtoul(argv[3 + i], NULL, 16);
        }
        slot.human = (u8)atoi(argv[7]);
        lg.options[0] = (u8)strtoul(argv[8], NULL, 16);
        options_load_save(&lg, &slot, 0);
        printf("%02X %02X %02X %02X %02X %02X %02X %02X\n", lg.options[0],
               lg.options[1], lg.options[2], lg.options[3], lg.win_record[0],
               lg.win_record[1], lg.win_record[2], lg.win_record[3]);
        return 0;
    }
    if (argc != 4 && argc != 7) {
        fprintf(stderr, "usage: hz_front ASSETS SCRIPT OUT "
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
        fprintf(stderr, "hz_front: can't open the script or OUT\n");
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
    g->snd_post = on_sound;
    g->snd_music_on = on_music;
    g->snd_user = r;
    g->front_probe = on_page;
    g->front_probe_user = r;

    char tag[4];
    unsigned v;
    if (fscanf(in, "%3s", tag) != 1 || strcmp(tag, "S") != 0
        || !read_hex(in, &v)) {
        fprintf(stderr, "hz_front: the script's S line\n");
        return 1;
    }
    g->hw_version = (u16)v;
    for (int i = 0; i < 4 && read_hex(in, &v); i++) {
        g->options[i] = (u8)v;
    }
    for (int i = 0; i < 4 && read_hex(in, &v); i++) {
        g->win_record[i] = (u8)v;
    }
    long at = ftell(in);
    if (fscanf(in, "%3s", tag) == 1 && strcmp(tag, "I") == 0) {
        r->inject = true;
        for (int i = 0; i < 4 && read_hex(in, &v); i++) {
            r->inject_record[i] = (u8)v;
        }
    } else {
        fseek(in, at, SEEK_SET);
    }
    unsigned pad;
    unsigned music;
    while (r->frames < MAX_FRAMES && read_hex(in, &pad) && read_hex(in, &music)) {
        r->pad[r->frames] = (u8)pad;
        r->music[r->frames] = (u8)music;
        r->frames++;
    }
    fclose(in);
    if (r->frames == 0) {
        g->quit = true;
    }

    int result = tick_title(g, on_frame, r);
    if (!g->quit) {
        fprintf(r->out, "E %d", result);
        for (int i = 0; i < 4; i++) {
            fprintf(r->out, " %02X", g->options[i]);
        }
        for (int i = 0; i < 4; i++) {
            fprintf(r->out, " %02X", g->win_record[i]);
        }
        fprintf(r->out, " %02X %02X\n", g->sfx_queued, g->sfx_priority);
    }
    fclose(r->out);
    return 0;
}
