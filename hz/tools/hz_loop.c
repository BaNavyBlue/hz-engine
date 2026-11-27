/* hz_loop: Game_FrameLoop as ported (game_frame), pass by pass, with the
 * controller ports scripted by VBlank and the state Game_SecondTick reads
 * poked at random between passes, logged for tools/hzloop_check.py.
 *
 *   hz_loop ASSETS OUT.txt [--map N] [--mode N] [--difficulty N]
 *           [--passes N] [--seed N] [--pads SCRIPT] [--demo]
 *           [--pokes] [--hq-zero PASS:P]
 *
 * --pads: "V:PORT:BUTTONS,..." -- from the hook of VBlank V (0 = the first
 * frame handed over, the fade's included) port PORT (1 or 2) holds
 * BUTTONS (UDLRABCS, "-" none) until a later step for that port.
 * --pads-random: a random script instead, a step on about a third of the
 * VBlanks from the first pass on for twice as many VBlanks as passes,
 * Start in about one in twelve; both ports let go at its end.
 * --demo: v_game_type -1 after Game_Start (a game's start, a demo's loop:
 * Demo_Start and Demo_PlayInput aren't ported). --pokes: before each pass,
 * now and then, mostly at the edges Game_SecondTick tests: an HQ's HP
 * (near v_cpu_boost_hq_hp, or anything), outpost counts (3-6, or 0-8),
 * unit counts (23-27, or 0-30), money (a payday's income short of the cap,
 * give or take 2), the frame counter near 40 minutes or back under it (in
 * a demo near its 60 s), a mech's CPU bit, a unit being built with 0-3
 * frames to go, the HUD bars' inputs (HP, ammo, fuel near the steps
 * HUD_UpdateGauges divides by; the HQ pokes near multiples of 32 too).
 * --hq-zero PASS:P: HQ P
 * (1 or 2) at 0 HP before that pass.
 *
 * The log, numbers in hex: "HZL1 map mode difficulty"; "B" after
 * Game_Start: VBlanks so far, v_cpu_boost_hq_hp, v_rng_seed; then per
 * pass "S" the state before it (after any pokes) and "E" after it:
 *   S pass vblanks poked-mask fc timer money1 money2 boost seed saved
 *     free_ammo cmd0c_sent cmd0c_arg mech_tick flags1 flags2 hq1 hq2
 *     posts1 posts2 units1 units2 game_type pad1 pad2 joy1 joy2
 *     prod_state1 prod_state2 prod_timer1 prod_timer2 sfx_sent
 *     pad_prev1 pad_prev2 hp1 ammo1 fuel1 hp2 ammo2 fuel2 shown1 shown2
 *     bars group cyc0 cyc1 steps tiles_shown tiles colours dpos1 dpos2
 *     drun1 drun2
 * (shown: mech_hp_shown, _hq_hp_shown, _ammo_shown, _fuel_shown as one
 * long; bars: a CRC-32 of the window cells under the eight HUD bars;
 * group: v_unit_ai_group; cyc0/1: v_colour_cycle; steps and tiles_shown:
 * v_anim_tile_step and v_anim_tile_shown, 2 and 4 hex digits each, run
 * together; tiles: a CRC-32 of VRAM $0000-$3FFF, the map's tiles; dpos1/2
 * and drun1/2: each demo pad's run (v_demo_ofs1/2 / 2) and frames left
 * (v_demo_run1/2);
 * colours: CRAM line 0 colours 2 and $B as one long)
 *   E pass exit vblanks (the same fields from fc on)
 * "P" before them repeats the pad script ("-" for none). The mask says
 * which fields a poke changed: 1 hq1, 2 hq2, 4 posts1, 8 posts2, $10
 * units1, $20 units2, $40 money1, $80 money2, $100 fc, $200 flags1, $400
 * flags2, $800 production 1, $1000 production 2, $2000 P1's HP, ammo and
 * fuel, $4000 P2's. A script that never lets
 * Start go would pause for ever: past 200,000 VBlanks the harness quits. */

#include "video_vdp.h"
#include "../src/game/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAD_STEPS 4096

typedef struct {
    int    vblank[PAD_STEPS];
    int    port[PAD_STEPS];
    HzPad  pad[PAD_STEPS];
    int    count;
} PadScript;

typedef struct {
    const PadScript *pads;
    int              vblanks;
} Loop;

static u32 rng_state = 1;

static u32 rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 8) & 0xFFFFFF;
}

static int rng_below(int n)
{
    return (int)(rng_next() % (u32)n);
}

static bool parse_pad(const char *s, size_t n, HzPad *p)
{
    memset(p, 0, sizeof *p);
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case 'U':
            p->up = true;
            break;
        case 'D':
            p->down = true;
            break;
        case 'L':
            p->left = true;
            break;
        case 'R':
            p->right = true;
            break;
        case 'A':
            p->a = true;
            break;
        case 'B':
            p->b = true;
            break;
        case 'C':
            p->c = true;
            break;
        case 'S':
            p->start = true;
            break;
        case '-':
            break;
        default:
            return false;
        }
    }
    return true;
}

/* A random script over VBlanks first..last - 1, written out as text in
 * the --pads form. */
static void random_pads(PadScript *ps, int first, int last, char *text,
                        size_t size)
{
    static const char LETTERS[8] = { 'U', 'D', 'L', 'R', 'B', 'C', 'A', 'S' };
    size_t used = 0;
    text[0] = '\0';
    for (int v = first; v < last && ps->count < PAD_STEPS; v++) {
        if (rng_below(3) != 0) {
            continue;
        }
        int port = rng_below(2);
        unsigned bits = (unsigned)rng_below(256) & 0x7F;
        if (rng_below(12) == 0) {
            bits |= 0x80;
        }
        char buttons[9];
        int n = 0;
        for (int b = 0; b < 8; b++) {
            if (bits & (1u << b)) {
                buttons[n++] = LETTERS[b];
            }
        }
        if (n == 0) {
            buttons[n++] = '-';
        }
        buttons[n] = '\0';
        int w = snprintf(text + used, size - used, "%s%d:%d:%s",
                         (used > 0) ? "," : "", v, port + 1, buttons);
        if (w < 0 || (size_t)w >= size - used) {
            break;
        }
        used += (size_t)w;
        ps->vblank[ps->count] = v;
        ps->port[ps->count] = port;
        parse_pad(buttons, (size_t)n, &ps->pad[ps->count]);
        ps->count++;
    }
    /* Both ports let go at the end, so no pause outlasts the script. */
    for (int port = 0; port < 2 && ps->count < PAD_STEPS; port++) {
        int w = snprintf(text + used, size - used, "%s%d:%d:-",
                         (used > 0) ? "," : "", last, port + 1);
        if (w < 0 || (size_t)w >= size - used) {
            break;
        }
        used += (size_t)w;
        ps->vblank[ps->count] = last;
        ps->port[ps->count] = port;
        memset(&ps->pad[ps->count], 0, sizeof ps->pad[ps->count]);
        ps->count++;
    }
}

static bool parse_pads(const char *text, PadScript *ps)
{
    const char *s = text;
    while (*s != '\0') {
        char *end = NULL;
        long v = strtol(s, &end, 10);
        if (end == s || *end != ':' || v < 0 || ps->count >= PAD_STEPS) {
            return false;
        }
        long port = strtol(end + 1, &end, 10);
        if ((port != 1 && port != 2) || *end != ':') {
            return false;
        }
        const char *b = end + 1;
        size_t n = strcspn(b, ",");
        if (!parse_pad(b, n, &ps->pad[ps->count])) {
            return false;
        }
        ps->vblank[ps->count] = (int)v;
        ps->port[ps->count] = (int)port - 1;
        ps->count++;
        s = b + n;
        if (*s == ',') {
            s++;
        }
    }
    return true;
}

/* Mech_Update sees no pad (the probe seam): the mechs stay put, so its
 * sound effects and flags stay out of what this check models (make mech
 * checks Mech_Update). mech_pad is put back straight after, as Mechs_Update
 * stored it. */
static u8 hidden_pad[2];

static void hide_pad(HzGame *g, int p, bool after, void *user)
{
    (void)user;
    if (!after) {
        hidden_pad[p] = g->mech[p].pad;
        g->mech[p].pad = 0;
    } else {
        g->mech[p].pad = hidden_pad[p];
    }
}

/* The hook: count the VBlank, and set each port as the script says after
 * it. */
static void on_frame(HzGame *g, void *user)
{
    Loop *l = user;
    int v = l->vblanks++;
    if (l->vblanks > 200000) {
        g->quit = true;
    }
    for (int i = 0; i < l->pads->count; i++) {
        if (l->pads->vblank[i] == v) {
            g->pad_in[l->pads->port[i]] = l->pads->pad[i];
        }
    }
}

static u16 hq_hp(const HzGame *g, int p)
{
    const HzPost *hq = &g->posts[HZ_POST_HQ + p];
    return post_hq_hp(hq);
}

static void set_hq_hp(HzGame *g, int p, u16 hp)
{
    HzPost *hq = &g->posts[HZ_POST_HQ + p];
    post_set_hq_hp(hq, hp);
}

/* (Half B of docs/hackable.md's step 1) The cartridge's VRAM, laid out
 * from the draw layer's changes by tools/video_vdp.c. */
static VideoVdp g_cart;
static Video g_video;

/* The window cells under the eight HUD bars (HUD_UpdateGauges' commands:
 * P1 HP, HQ, ammo, fuel, then P2's), as VRAM holds them. */
static const struct {
    u16 addr;
    int cells;
} BARS[8] = {
    { 0xF104, 8 }, { 0xF084, 8 }, { 0xF098, 6 }, { 0xF118, 6 },
    { 0xF12C, 8 }, { 0xF0AC, 8 }, { 0xF0C0, 6 }, { 0xF140, 6 }
};

/* CRC-32 (IEEE, as zlib's crc32), by table. */
static u32 crc_table[256];

static void crc_init(void)
{
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        }
        crc_table[i] = c;
    }
}

static u32 crc_bytes(u32 crc, const u8 *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc = (crc >> 8) ^ crc_table[(crc ^ p[i]) & 0xFF];
    }
    return crc;
}

/* CRC-32 of those cells' bytes, bar by bar. */
static u32 bars_crc(void)
{
    u32 crc = 0xFFFFFFFFu;
    for (int b = 0; b < 8; b++) {
        crc = crc_bytes(crc, g_cart.vdp->vram + BARS[b].addr,
                        (size_t)BARS[b].cells * 2);
    }
    return ~crc;
}

static u32 shown(const HzMech *m)
{
    return ((u32)m->hp_shown << 24) | ((u32)m->hq_hp_shown << 16)
         | ((u32)m->ammo_shown << 8) | m->fuel_shown;
}

static void put_state(FILE *f, const HzGame *g)
{
    fprintf(f, " %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X"
               " %X %X %X %X %X %X %X %X %X %X %X %X",
            g->frame_counter, g->second_timer, g->mech[0].money,
            g->mech[1].money, g->cpu_boost, g->rng_seed, g->rng_seed_saved,
            g->free_ammo, g->snd_pause_sent, g->snd_pause_arg, g->mech_tick,
            g->mech[0].flags, g->mech[1].flags, hq_hp(g, 0), hq_hp(g, 1),
            g->outpost_count[0], g->outpost_count[1], (u8)g->unit_count[0],
            (u8)g->unit_count[1], (u16)g->game_type, g->mech[0].pad,
            g->mech[1].pad, g->joypad[0], g->joypad[1],
            g->mech[0].prod_state, g->mech[1].prod_state,
            g->mech[0].prod_timer, g->mech[1].prod_timer, g->sfx_sent,
            g->mech[0].pad_prev, g->mech[1].pad_prev);
    fprintf(f, " %X %X %X %X %X %X %X %X %X", g->mech[0].hp,
            g->mech[0].ammo, g->mech[0].fuel, g->mech[1].hp, g->mech[1].ammo,
            g->mech[1].fuel, shown(&g->mech[0]), shown(&g->mech[1]),
            bars_crc());
    fprintf(f, " %X %X %X ", g->unit_ai_group, g->colour_cycle[0],
            g->colour_cycle[1]);
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
        fprintf(f, "%02X", g->anim_tile_step[r]);
    }
    fputc(' ', f);
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
        fprintf(f, "%04X", g->anim_tile_shown[r]);
    }
    fprintf(f, " %X %X",
            ~crc_bytes(0xFFFFFFFFu, g_cart.vdp->vram, 0x4000),
            ((u32)g_cart.vdp->cram[2] << 16) | g_cart.vdp->cram[11]);
    fprintf(f, " %X %X %X %X\n", g->demo_pos[0], g->demo_pos[1],
            g->demo_frames[0], g->demo_frames[1]);
}

/* A value near a multiple of `step` (`lo` to `hi` steps), never below
 * `min`. */
static u16 near_step(int step, int lo, int hi, int min)
{
    int v = (lo + rng_below(hi - lo + 1)) * step + rng_below(5) - 2;
    return (u16)(v < min ? min : v);
}

/* Random pokes before a pass; returns the mask of what changed. */
static unsigned poke(HzGame *g)
{
    unsigned mask = 0;
    if (rng_below(8) == 0) {
        int p = rng_below(2);
        int kind = rng_below(3);
        u16 hp = (kind == 0) ? (u16)(g->cpu_boost_hq_hp - 2 + rng_below(5))
               : (kind == 1) ? (u16)(1 + rng_below(0x400))
                             : near_step(32, 0, 0x20, 1);
        set_hq_hp(g, p, hp);
        mask |= 1u << p;
    }
    for (int p = 0; p < 2; p++) {
        if (rng_below(10) == 0) {
            g->outpost_count[p] = (u8)((rng_below(3) != 0)
                                       ? 3 + rng_below(4) : rng_below(9));
            mask |= 4u << p;
        }
        if (rng_below(10) == 0) {
            g->unit_count[p] = (u8)((rng_below(3) != 0)
                                    ? 23 + rng_below(5) : rng_below(31));
            mask |= 0x10u << p;
        }
        if (rng_below(16) == 0) {
            int income = (g->outpost_count[p] + 1) * 4
                       * ((g->game_type < 0) ? 2 : 1);
            g->mech[p].money = (u16)(50000 - income - 2 + rng_below(5));
            mask |= 0x40u << p;
        }
        if (rng_below(64) == 0) {
            g->mech[p].flags ^= 0x01;
            mask |= 0x200u << p;
        }
        if (rng_below(12) == 0) {
            /* The HUD bars' inputs, at the steps HUD_UpdateGauges divides
             * by. HP past $7F8 now and then: HP / 8 then has a low byte
             * the shown byte can match. Fuel stays at 1 or more: a jet
             * with none would come down, which Mech_Update's own check
             * covers. */
            HzMech *m = &g->mech[p];
            switch (rng_below(4)) {
            case 0:
                m->hp = (rng_below(8) == 0) ? (u16)(0x7F8 + rng_below(0x20))
                                            : near_step(8, 0, 0x22, 0);
                break;
            case 1:
                m->ammo = (u8)near_step(4, 0, 0x19, 0);
                break;
            case 2:
                m->fuel = near_step(64, 0, 0x19, 1);
                break;
            default:
                m->hp = near_step(8, 0, 0x22, 0);
                m->ammo = (u8)near_step(4, 0, 0x19, 0);
                m->fuel = near_step(64, 0, 0x19, 1);
                break;
            }
            mask |= 0x2000u << p;
        }
        if (rng_below(24) == 0) {
            g->mech[p].prod_state = (u8)rng_below(3);
            g->mech[p].prod_timer = (u16)rng_below(4);
            mask |= 0x800u << p;
        }
    }
    if (rng_below(32) == 0) {
        if (g->game_type < 0) {
            g->frame_counter = (u32)(0xE10 - 40 + rng_below(30));
        } else if (rng_below(2) == 0) {
            g->frame_counter = (u32)(0x23280 - 3 + rng_below(7));
        } else {
            g->frame_counter = (u32)rng_below(0x20000);
        }
        mask |= 0x100;
    }
    return mask;
}

static int usage(void)
{
    fprintf(stderr, "usage: hz_loop ASSETS OUT.txt [--map N] [--mode N] "
                    "[--difficulty N] [--passes N] [--seed N]\n"
                    "       [--pads SCRIPT | --pads-random] [--demo] [--pokes] "
                    "[--hq-zero PASS:P]\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        return usage();
    }
    int map = 0;
    int mode = 0;
    int difficulty = 0;
    int passes = 600;
    bool demo = false;
    bool pokes = false;
    bool pads_random = false;
    int hq_zero_pass = -1;
    int hq_zero_p = 0;
    static PadScript pads;
    const char *pads_text = "-";
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0) {
            demo = true;
            continue;
        }
        if (strcmp(argv[i], "--pokes") == 0) {
            pokes = true;
            continue;
        }
        if (strcmp(argv[i], "--pads-random") == 0) {
            pads_random = true;
            continue;
        }
        if (i + 1 >= argc) {
            return usage();
        }
        const char *arg = argv[++i];
        if (strcmp(argv[i - 1], "--map") == 0) {
            map = atoi(arg);
        } else if (strcmp(argv[i - 1], "--mode") == 0) {
            mode = atoi(arg);
        } else if (strcmp(argv[i - 1], "--difficulty") == 0) {
            difficulty = atoi(arg);
        } else if (strcmp(argv[i - 1], "--passes") == 0) {
            passes = atoi(arg);
        } else if (strcmp(argv[i - 1], "--seed") == 0) {
            rng_state = (u32)strtoul(arg, NULL, 10);
        } else if (strcmp(argv[i - 1], "--pads") == 0) {
            pads_text = arg;
            if (!parse_pads(arg, &pads)) {
                return usage();
            }
        } else if (strcmp(argv[i - 1], "--hq-zero") == 0) {
            if (sscanf(arg, "%d:%d", &hq_zero_pass, &hq_zero_p) != 2
                || hq_zero_p < 1 || hq_zero_p > 2) {
                return usage();
            }
            hq_zero_p--;
        } else {
            return usage();
        }
    }
    if (map < 0 || map >= HZ_MAPS || mode < 0 || mode > 7 || difficulty < 0
        || difficulty > 3 || passes < 1) {
        return usage();
    }

    crc_init();
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    FILE *f = fopen(argv[2], "w");
    if (f == NULL) {
        fprintf(stderr, "hz_loop: can't write %s\n", argv[2]);
        return 1;
    }

    static Vdp vdp;
    static HzGame game;
    Loop loop = { &pads, 0 };
    video_init(&g_video);
    vdp_reset(&vdp);
    video_vdp_watch(&g_cart, &g_video, &vdp);
    game_init(&game, &assets, &g_video);
    game.frame = on_frame;
    game.frame_user = &loop;
    game.options[0] = (u8)mode;
    game.options[1] = (u8)difficulty;
    game.options[3] = (u8)map;
    if (!game_new(&game)) {
        fprintf(stderr, "hz_loop: map %d doesn't load\n", map);
        fclose(f);
        return 1;
    }
    game_start(&game);
    game.mech_probe = hide_pad;
    game.cpus_frozen = true;        /* the model doesn't follow the CPU's AI
                                       (make mechai) */
    /* The CPU's start units parked (unit_speed 0): Units_UpdateTeam then
     * only marks them where they stand, so the ground's events, deaths and
     * their sound effects stay out of what this check models (make
     * unitmove, mech and combat check the units' move). */
    for (int side = 0; side < 2; side++) {
        for (u32 slot = 0; slot < side_slots(&game, side); slot++) {
            unit_at(&game, side, slot)->speed = 0;
        }
    }
    if (demo) {
        game.game_type = -1;
    }
    static char random_text[PAD_STEPS * 16];
    if (pads_random) {
        pads.count = 0;
        random_pads(&pads, loop.vblanks, loop.vblanks + passes * 2,
                    random_text, sizeof random_text);
        pads_text = (random_text[0] != '\0') ? random_text : "-";
    }
    fprintf(f, "HZL1 %X %X %X\n", map, mode, difficulty);
    fprintf(f, "B %X %X %X\n", loop.vblanks, game.cpu_boost_hq_hp,
            game.rng_seed);
    fprintf(f, "P %s\n", pads_text);

    HzLoopExit exit = HZ_LOOP_ON;
    int pass = 0;
    for (; pass < passes && exit == HZ_LOOP_ON; pass++) {
        unsigned mask = pokes ? poke(&game) : 0;
        if (pass == hq_zero_pass) {
            set_hq_hp(&game, hq_zero_p, 0);
            mask |= 1u << hq_zero_p;
        }
        fprintf(f, "S %X %X %X", pass, loop.vblanks, mask);
        put_state(f, &game);
        exit = game_frame(&game, pass > 0);
        fprintf(f, "E %X %X %X", pass, (unsigned)exit, loop.vblanks);
        put_state(f, &game);
    }
    fclose(f);
    printf("map %d mode %d: %d passes, %d VBlanks, exit %d\n", map, mode,
           pass, loop.vblanks, (int)exit);
    return 0;
}
