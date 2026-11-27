/* hz_netcheck: the determinism check for netplay. A network game would run
 * the whole game on both machines and send only the two pad bytes a frame,
 * so it needs the game to come out the same from the same pads on every
 * build: Windows and Linux, any compiler, any optimization.
 *
 *     hz_netcheck ASSETS SEED FRAMES OUT [--verbose FROM TO]
 *
 * Runs main.c's loop (MainLoop_Title: the title and the options screen,
 * the load, the game, its end) with no window and no sound, for FRAMES
 * frames, both pads driven by a pseudo-random player made from SEED: a
 * chord of buttons held for a while, then the next. The first game skips
 * the title, as `hz --mode --map --difficulty` does, with those picked
 * from SEED (two players, vs the CPU either side, CPU against CPU); later
 * ones go through the menus with whatever the player presses. A title the
 * player leaves alone (a quarter of them) runs out into the attract demo:
 * the title's z_music_on is answered from the frame count, as a network
 * game would answer it, not from the sound. The fixes (HzGame.fixes) are
 * picked from SEED too.
 *
 * Each frame folds into a running 64-bit hash (FNV-1a): the state as
 * hz_demo's images lay it out (its globals, mechs, units, posts and shots),
 * v_grid, what the draw layer holds (src/core/video.h: its tiles and
 * cells, colours, scroll and layout, sprites), the frame video_compose
 * draws, and the sound commands posted. OUT (text):
 *   S second hash where     every 60 frames (where: title, load, game,
 *                           over), the hash so far
 *   G frame mode map difficulty   a game started
 *   X frame how             a game left (over, title, quit)
 *   E frames hash games demos     at the end
 * Two builds agree when their OUTs are the same. --verbose FROM TO also
 * writes each frame's own hashes in FROM..TO (V frame globals mechs units
 * posts shots grid tiles colours scroll sprites screen sound; up to
 * session 79 the VDP's vram cram vsram regs in their places), to find the
 * field where two builds part. --seen (session 79) folds only the state,
 * the screen and the sound, not how the screen is held: two builds that
 * draw the same frames another way compare. --frame N FILE (session 80)
 * writes frame N as drawn: its 320 x 224 colour indices, then the 64
 * colours (big-endian words). Summary on stdout. Set HZ_SAVES to an empty
 * folder: a won game is saved there and CONT reads it back. */

#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/statehash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- the hash: the state's parts (game/statehash.c, session 56), then the
 * screen and the sound -- */

#define FNV_BASIS STATE_FNV_BASIS
#define fnv       state_fnv

/* -- the player -- */

/* splitmix64: the same numbers from the same seed on any build. */
static u64 next_random(u64 *s)
{
    u64 z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

typedef enum {
    WHERE_TITLE = 0,    /* the title and the options screen */
    WHERE_LOAD,         /* game_setup_after_title: the loading screen */
    WHERE_GAME,         /* the frame loop */
    WHERE_OVER,         /* game_over */
    WHERE_COUNT
} Where;

static const char *const WHERE_NAME[WHERE_COUNT] = {
    "title", "load", "game", "over"
};

typedef struct {
    u64 random;
    int left;           /* frames the chord is held for yet */
    u8  held;           /* HZ_PAD_* */
} Player;

/* The next chord: a direction (or none) and each of A, B and C a quarter
 * of the time, held 1-40 frames (a tenth of them 60-250: flying across
 * the map). Start is common on the title and the options screen (it moves
 * them on) and rare elsewhere: in a game it pauses, and on a demo's
 * loading screen it calls the demo off. */
static void player_next(Player *pl, Where where)
{
    static const u8 DIRS[9] = {
        0, HZ_PAD_UP, HZ_PAD_DOWN, HZ_PAD_LEFT, HZ_PAD_RIGHT,
        HZ_PAD_UP | HZ_PAD_LEFT, HZ_PAD_UP | HZ_PAD_RIGHT,
        HZ_PAD_DOWN | HZ_PAD_LEFT, HZ_PAD_DOWN | HZ_PAD_RIGHT
    };
    u64 r = next_random(&pl->random);
    u8 b = DIRS[r % 9];
    r /= 9;
    if (r % 4 == 0) {
        b |= HZ_PAD_A;
    }
    r /= 4;
    if (r % 4 == 0) {
        b |= HZ_PAD_B;
    }
    r /= 4;
    if (r % 4 == 0) {
        b |= HZ_PAD_C;
    }
    r /= 4;
    if (where == WHERE_TITLE ? r % 6 == 0 : r % 400 == 0) {
        b |= HZ_PAD_START;
    }
    r /= 400;
    pl->left = (r % 10 == 0) ? 60 + (int)((r / 10) % 191)
                             : 1 + (int)((r / 10) % 40);
    pl->held = b;
}

static void pad_set(HzPad *p, u8 b)
{
    p->up = (b & HZ_PAD_UP) != 0;
    p->down = (b & HZ_PAD_DOWN) != 0;
    p->left = (b & HZ_PAD_LEFT) != 0;
    p->right = (b & HZ_PAD_RIGHT) != 0;
    p->a = (b & HZ_PAD_A) != 0;
    p->b = (b & HZ_PAD_B) != 0;
    p->c = (b & HZ_PAD_C) != 0;
    p->start = (b & HZ_PAD_START) != 0;
}

/* -- the run -- */

/* A title left alone runs out after this many frames: z_music_on goes
 * false, as the title's song ending would make it. */
#define TITLE_SONG_FRAMES 1800

typedef struct {
    FILE    *out;
    int      limit;
    int      done;          /* frames handed over so far */
    Where    where;
    int      title_frames;  /* frames since the title was prepared */
    bool     title_idle;    /* this title is left alone */
    u64      hash;
    u64      sound;         /* this frame's sound commands */
    Player   player[2];
    u64      choices;       /* the run's other picks */
    int      verbose_from, verbose_to;
    int      frame_at;      /* --frame N FILE: frame N's picture */
    const char *frame_path;
    bool     seen;          /* --seen: what is seen and heard only */
    int      frames_in[WHERE_COUNT];
    int      games, demos, overs;
} Run;

static Run g_run;

static u8 g_screen[SCREEN_W * SCREEN_H];

static void on_sound(void *user, const u8 *bytes, int count)
{
    Run *r = user;
    u8 n = (u8)count;
    r->sound = fnv(r->sound, &n, 1);
    r->sound = fnv(r->sound, bytes, (size_t)count);
}

/* main.c's on_frame, the platform's part a hash. */
static void on_frame(HzGame *g, void *user)
{
    Run *r = user;
    if (g->quit) {
        return;
    }
    video_compose(g->video, g_screen);

    u64 part[STATE_HASH_PARTS + 2];
    state_hash_parts(g, part);
    part[10] = fnv(FNV_BASIS, g_screen, sizeof g_screen);
    part[11] = r->sound;
    if (r->seen) {
        /* (Session 79) The game, the screen and the sound, not how the
         * screen is held: so a build that draws another way (the VDP
         * model then, the draw layer now: docs/hackable.md) compares. */
        u64 seen[8];
        memcpy(seen, part, 6 * sizeof seen[0]);
        seen[6] = part[10];
        seen[7] = part[11];
        r->hash = state_hash_fold(r->hash, seen, 8);
    } else {
        r->hash = state_hash_fold(r->hash, part, 12);
    }
    if (r->done == r->frame_at) {
        FILE *pf = fopen(r->frame_path, "wb");
        if (pf != NULL) {
            fwrite(g_screen, 1, sizeof g_screen, pf);
            for (int i = 0; i < 64; i++) {
                fputc(g->video->colours[i] >> 8, pf);
                fputc(g->video->colours[i] & 0xFF, pf);
            }
            fclose(pf);
        }
    }
    if (r->done >= r->verbose_from && r->done <= r->verbose_to) {
        fprintf(r->out, "V %d", r->done);
        for (int i = 0; i < 12; i++) {
            fprintf(r->out, " %016llX", (unsigned long long)part[i]);
        }
        fputc('\n', r->out);
    }
    r->sound = FNV_BASIS;
    r->frames_in[r->where]++;
    r->title_frames++;

    /* The pads for the next frame. */
    for (int p = 0; p < 2; p++) {
        Player *pl = &r->player[p];
        if (--pl->left <= 0) {
            player_next(pl, r->where);
        }
    }
    memset(g->pad_in, 0, sizeof g->pad_in);
    if (!(r->where == WHERE_TITLE && r->title_idle)) {
        pad_set(&g->pad_in[0], r->player[0].held);
        pad_set(&g->pad_in[1], r->player[1].held);
    }

    r->done++;
    if (r->done % 60 == 0) {
        fprintf(r->out, "S %d %016llX %s\n", r->done / 60,
                (unsigned long long)r->hash, WHERE_NAME[r->where]);
    }
    if (r->done >= r->limit) {
        g->quit = true;
    }
}

/* Title_WaitStart's z_music_on: the title's song plays for
 * TITLE_SONG_FRAMES. */
static bool music_on(void *user)
{
    Run *r = user;
    return r->title_frames < TITLE_SONG_FRAMES;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: hz_netcheck ASSETS SEED FRAMES OUT "
                        "[--verbose FROM TO] [--seen] [--frame N FILE]\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    Run *r = &g_run;
    u64 seed = strtoull(argv[2], NULL, 0);
    r->limit = atoi(argv[3]);
    r->verbose_from = -1;
    r->verbose_to = -2;
    r->frame_at = -1;
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--frame") == 0 && i + 2 < argc) {
            r->frame_at = atoi(argv[++i]);
            r->frame_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0 && i + 2 < argc) {
            r->verbose_from = atoi(argv[++i]);
            r->verbose_to = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seen") == 0) {
            r->seen = true;
        } else {
            fprintf(stderr, "hz_netcheck: what is %s?\n", argv[i]);
            return 2;
        }
    }
    r->out = fopen(argv[4], "wb");  /* "\n" on Windows too: OUTs compare
                                       as they are */
    if (r->out == NULL) {
        fprintf(stderr, "hz_netcheck: can't write %s\n", argv[4]);
        return 1;
    }
    r->hash = FNV_BASIS;
    r->sound = FNV_BASIS;
    r->choices = seed * 3 + 0x5EED;
    for (int p = 0; p < 2; p++) {
        r->player[p].random = seed * 3 + (u64)p + 1;
    }

    static Video video;
    static HzGame game;
    HzGame *g = &game;
    video_init(&video);
    game_init(g, &assets, &video);
    g->frame = on_frame;
    g->frame_user = r;
    g->snd_music_on = music_on;
    g->snd_post = on_sound;
    g->snd_user = r;
    /* The fixes: all on, none, or the default. */
    static const u32 FIXES[3] = { HZ_FIX_ALL, 0, HZ_FIX_DEFAULT };
    g->fixes = FIXES[next_random(&r->choices) % 3];
    /* main.c's cold boot, then the first game's picks: two players, the
     * CPU as P2 or P1, or CPU against CPU (the split screen, or wide). */
    static const u8 MODES[6] = { 0, 0, 1, 2, 3, 5 };
    g->hw_version = 0xA0;
    g->options[0] = 5;
    g->options[2] = 0xFF;
    snd_init(g);
    g->options[0] = MODES[next_random(&r->choices) % 6];
    g->options[1] = (u8)(next_random(&r->choices) % 4);
    g->options[3] = (u8)(next_random(&r->choices) % HZ_MAPS);
    bool skip_title = true;

    while (!g->quit) {                      /* MainLoop_Title */
        r->where = WHERE_TITLE;
        r->title_frames = 0;
        r->title_idle = next_random(&r->choices) % 4 == 0;
        game_title_prepare(g);
        g->game_type = 0;
        if (!skip_title) {
            g->game_type = (s16)menu_title_flow(g);
            if (g->quit) {
                break;
            }
        }
        skip_title = false;
        r->where = WHERE_LOAD;
        HzSetup setup = game_setup_after_title(g);
        if (setup == HZ_SETUP_NO_MAP) {
            fprintf(stderr, "hz_netcheck: map %d doesn't load\n",
                    g->options[3] & 7);
            return 1;
        }
        if (setup == HZ_SETUP_ABORTED || g->quit) {
            continue;
        }
        if (g->game_type < 0) {
            r->demos++;
        } else {
            r->games++;
        }
        fprintf(r->out, "G %d %d %d %d%s\n", r->done, g->options[0],
                g->options[3] & 7, g->options[1],
                g->game_type < 0 ? " demo" : "");
        r->where = WHERE_GAME;
        game_start(g);
        HzLoopExit exit = game_frame(g, false);
        while (exit == HZ_LOOP_ON) {
            exit = game_frame(g, true);
        }
        fprintf(r->out, "X %d %s\n", r->done,
                exit == HZ_LOOP_GAME_OVER ? "over"
                : exit == HZ_LOOP_TITLE ? "title" : "quit");
        if (exit == HZ_LOOP_GAME_OVER) {
            r->overs++;
            r->where = WHERE_OVER;
            game_over(g);
        }
    }
    fprintf(r->out, "E %d %016llX %d %d\n", r->done,
            (unsigned long long)r->hash, r->games, r->demos);
    fclose(r->out);
    printf("seed %llu: %d frames, %016llX; games %d (over %d), demos %d; "
           "frames title %d, load %d, game %d, over %d; fixes %X\n",
           (unsigned long long)seed, r->done, (unsigned long long)r->hash,
           r->games, r->overs, r->demos, r->frames_in[WHERE_TITLE],
           r->frames_in[WHERE_LOAD], r->frames_in[WHERE_GAME],
           r->frames_in[WHERE_OVER], (unsigned)g->fixes);
    return 0;
}
