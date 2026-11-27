/* hz_rumble: the rumble's check (session 54, src/game/rumble.c).
 *
 *     hz_rumble ASSETS SEED FRAMES [--verbose] [--game MAP MODE DIFFICULTY]
 *               [--motors SIDE LEVEL]
 *
 * Runs main.c's loop as hz_netcheck does (the title and the options
 * screen, the load, the game, its end, the attract demo), FRAMES frames,
 * both pads driven by hz_netcheck's pseudo-random player from SEED, the
 * first game's mode (two players, the CPU either side, CPU against CPU),
 * map and difficulty picked from SEED. After each frame the rumble
 * watches it as main.c's does (rumble_watch with sfx_asked, then
 * cleared), and what it felt is held against what the game itself said
 * and did that frame -- its sound effects (sfx_asked), and the mechs' and
 * HQs' fields, read here apart from the rumble:
 *
 *   felt -> said    a hit only with SFX_HIT_MECH and the mech's HP down;
 *                   the ground only with SFX_SIZZLE, its robot $20 HP
 *                   down; destroyed only with SFX_MECH_DESTROYED; the
 *                   jets only with SFX_JETS_MEET and both mechs jets; the
 *                   HQ hit only with SFX_HQ_HIT, its HQ's HP down and its
 *                   alert started; the HQ alert's beat only with
 *                   SFX_HQ_ALERT and its count at a beat ($A0, $64,
 *                   $28), once a beat; the missile's beat only with
 *                   SFX_MISSILE_ALERT (or its view hidden, where the game
 *                   sounds nothing) and a missile homing in; the HQ lost
 *                   and won only with SFX_HQ_DESTROYED, the loser's HQ at
 *                   0 HP
 *   said -> felt    SFX_HIT_MECH: a side hit or destroyed; SFX_SIZZLE with
 *                   a robot $20 HP down: its ground (or a hit, with a
 *                   shot too); SFX_MECH_DESTROYED: a side destroyed (or a
 *                   wreck destroyed again, out of fuel: its debris count
 *                   back at $180);
 *                   SFX_JETS_MEET: both sides the jets; SFX_HQ_HIT: an
 *                   HQ's HP down, and the HQ hit felt where its alert
 *                   started; SFX_HQ_ALERT: a side's count at a beat, and
 *                   each beat felt before the count moves on;
 *                   SFX_MISSILE_ALERT: a
 *                   missile's beat; SFX_HQ_DESTROYED: lost and won
 *   motors          a side that felt something has its motors going that
 *                   frame; motors going only within 120 frames (the
 *                   longest shake) of something felt; nothing felt or
 *                   going in the attract demo
 *
 * Output on stdout: a line per broken rule (F frame side what), up to 40
 * (all with --verbose), then a summary: "seed N: F frames, G games (O
 * over), D demos; felt hit H ground ... ; K broken". Exit 1 if anything
 * broke. tools/hzrumble_check.py runs the seeds and checks that every
 * kind was felt somewhere. Set HZ_SAVES to an empty folder: a won game is
 * saved there.
 *
 * --game: the first game as `hz --map MAP --mode MODE --difficulty
 * DIFFICULTY` starts it (the default fixes), the pads left alone
 * throughout, as hz_headless runs it with an idle HZ_PAD1. --motors: also
 * a line "F 1 LOW HIGH" each frame SIDE's motors at LEVEL (0 OFF to 3
 * HIGH) change, F counted as hz_headless counts frames presented: the
 * HZ_RUMBLE log hz_headless writes when gamepad 1 feels that side
 * (tools/hzmenu_check.py compares the two), and before the summary the
 * felt counts side by side ("side S: kind N ..."). */

#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/rumble.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- hz_netcheck's player -- */

static u64 next_random(u64 *s)
{
    u64 z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

typedef struct {
    u64 random;
    int left;
    u8  held;
} Player;

static void player_next(Player *pl, bool title)
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
    if (title ? r % 6 == 0 : r % 400 == 0) {
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

#define TITLE_SONG_FRAMES 1800
#define LONGEST_SHAKE     120       /* HQ lost: held 60, down over 60 */
#define SHOWN_BROKEN      40

static const char *const KIND_NAME[RUMBLE_FELT_KINDS] = {
    "hit", "ground", "destroyed", "jets", "hq_hit", "hq_alert", "missile",
    "hq_lost", "hq_won"
};

/* What this check keeps of the last frame, apart from the rumble's own. */
typedef struct {
    bool primed;
    u16  hp[2];
    u8   flags[2];
    u16  hq_hp[2];
    u8   hq_alert[2];
    u8   beat_felt[2];  /* the alert's count when a beat was last felt */
} Seen;

typedef struct {
    int      limit;
    int      done;
    bool     title;         /* the title and options are on */
    int      title_frames;
    bool     title_idle;
    Player   player[2];
    u64      choices;
    bool     verbose;
    bool     idle;          /* --game: the pads left alone */
    int      motors_side;   /* --motors, -1 none */
    RumbleLevel motors_level;
    RumbleMotors motors_last;
    int      side_count[2][RUMBLE_FELT_KINDS];
    HzRumble rumble;
    Seen     seen;
    int      since_felt[2]; /* frames since a side last felt anything */
    int      felt_count[RUMBLE_FELT_KINDS];
    int      broken;
    int      games, demos, overs;
} Run;

static Run g_run;

static bool asked(u32 sfx, int n)
{
    return (sfx & (1u << n)) != 0;
}

static void broke(Run *r, int side, const char *what)
{
    r->broken++;
    if (r->verbose || r->broken <= SHOWN_BROKEN) {
        printf("F %d %d %s\n", r->done, side, what);
    }
}

/* The frame's rules (the file's head). */
static void check_frame(Run *r, const HzGame *g, u32 sfx)
{
    const HzRumble *ru = &r->rumble;
    Seen *was = &r->seen;
    bool demo = g->game_type < 0;
    Seen now;
    for (int s = 0; s < 2; s++) {
        now.hp[s] = g->mech[s].hp;
        now.flags[s] = g->mech[s].flags;
        now.hq_hp[s] = post_hq_hp(&g->posts[HZ_POST_HQ + s]);
        now.hq_alert[s] = g->mech[s].hq_alert;
    }
    now.primed = !demo;
    for (int s = 0; s < 2; s++) {
        now.beat_felt[s] = was->primed ? was->beat_felt[s] : 0;
    }

    for (int s = 0; s < 2; s++) {
        u16 felt = ru->felt[s];
        RumbleMotors m = rumble_motors(ru, s);
        bool going = m.low != 0 || m.high != 0;
        for (int k = 0; k < RUMBLE_FELT_KINDS; k++) {
            if (felt & (1u << k)) {
                r->felt_count[k]++;
                r->side_count[s][k]++;
            }
        }
        r->since_felt[s] = (felt != 0) ? 0 : r->since_felt[s] + 1;
        if (demo) {
            if (felt != 0) {
                broke(r, s, "felt in the attract demo");
            }
            if (going && r->since_felt[s] > LONGEST_SHAKE) {
                broke(r, s, "motors going in the attract demo");
            }
            continue;
        }
        if (felt != 0 && !going) {
            broke(r, s, "felt, motors still");
        }
        if (going && r->since_felt[s] > LONGEST_SHAKE) {
            broke(r, s, "motors going, nothing felt for 120 frames");
        }
        if (!was->primed) {
            if (felt != 0) {
                broke(r, s, "felt on the first frame watched");
            }
            continue;
        }
        const HzMech *mech = &g->mech[s];
        bool alive = !(was->flags[s] & MECH_DESTROYED)
                  && !(now.flags[s] & MECH_DESTROYED);
        bool hp_down = alive && now.hp[s] < was->hp[s];
        bool robot = mech->form == HZ_FORM_ROBOT;
        bool view_hidden = mech->view < 0;

        if ((felt & RUMBLE_FELT_HIT)
            && !(asked(sfx, SFX_HIT_MECH) && hp_down)) {
            broke(r, s, "hit felt, no SFX_HIT_MECH or HP down");
        }
        if ((felt & RUMBLE_FELT_GROUND)
            && !(asked(sfx, SFX_SIZZLE) && hp_down && robot
                 && was->hp[s] - now.hp[s] == 0x20)) {
            broke(r, s, "ground felt, no SFX_SIZZLE or robot $20 down");
        }
        if ((felt & RUMBLE_FELT_DESTROYED)
            && !asked(sfx, SFX_MECH_DESTROYED)) {
            broke(r, s, "destroyed felt, no SFX_MECH_DESTROYED");
        }
        if ((felt & RUMBLE_FELT_JETS)
            && !(asked(sfx, SFX_JETS_MEET)
                 && g->mech[0].form != HZ_FORM_ROBOT
                 && g->mech[1].form != HZ_FORM_ROBOT)) {
            broke(r, s, "jets felt, no SFX_JETS_MEET or not two jets");
        }
        if ((felt & RUMBLE_FELT_HQ_HIT)
            && !(asked(sfx, SFX_HQ_HIT) && now.hq_hp[s] < was->hq_hp[s]
                 && was->hq_alert[s] == 0 && now.hq_alert[s] != 0)) {
            broke(r, s, "HQ hit felt, no SFX_HQ_HIT, HP down or alert");
        }
        bool beat_count = was->hq_alert[s] == 0xA0
                       || was->hq_alert[s] == 0x64
                       || was->hq_alert[s] == 0x28;
        if ((felt & RUMBLE_FELT_HQ_ALERT)
            && !(asked(sfx, SFX_HQ_ALERT) && beat_count)) {
            broke(r, s, "HQ alert felt, no SFX_HQ_ALERT or count at a beat");
        }
        if (felt & RUMBLE_FELT_HQ_ALERT) {
            if (now.beat_felt[s] == was->hq_alert[s]) {
                broke(r, s, "HQ alert's beat felt twice");
            }
            now.beat_felt[s] = was->hq_alert[s];
        }
        /* The count leaving a beat's value: that beat felt. */
        if (beat_count && now.hq_alert[s] != was->hq_alert[s]
            && now.beat_felt[s] != was->hq_alert[s]) {
            broke(r, s, "HQ alert's count past a beat, the beat not felt");
        }
        if ((felt & RUMBLE_FELT_MISSILE)
            && !((asked(sfx, SFX_MISSILE_ALERT) || view_hidden)
                 && mech->missile_dirs != 0)) {
            broke(r, s, "missile felt, no SFX_MISSILE_ALERT or missile");
        }
        if ((felt & (RUMBLE_FELT_HQ_LOST | RUMBLE_FELT_HQ_WON))
            && !asked(sfx, SFX_HQ_DESTROYED)) {
            broke(r, s, "HQ lost or won felt, no SFX_HQ_DESTROYED");
        }
        if ((felt & RUMBLE_FELT_HQ_LOST) && now.hq_hp[s] != 0) {
            broke(r, s, "HQ lost felt, its HQ not at 0 HP");
        }
        /* SFX_SIZZLE with this robot $20 down: felt as the ground, or as a
         * hit when a shot hit a mech too. */
        if (asked(sfx, SFX_SIZZLE) && hp_down && robot
            && was->hp[s] - now.hp[s] == 0x20
            && !(felt & (RUMBLE_FELT_GROUND | RUMBLE_FELT_HIT))) {
            broke(r, s, "SFX_SIZZLE, robot $20 down, not felt");
        }
        /* SFX_HQ_HIT with this HQ down and its alert starting. */
        if (asked(sfx, SFX_HQ_HIT) && now.hq_hp[s] < was->hq_hp[s]
            && was->hq_alert[s] == 0 && now.hq_alert[s] != 0
            && !(felt & RUMBLE_FELT_HQ_HIT)) {
            broke(r, s, "SFX_HQ_HIT, alert started, not felt");
        }
        if (asked(sfx, SFX_JETS_MEET) && !(felt & RUMBLE_FELT_JETS)) {
            broke(r, s, "SFX_JETS_MEET, not felt");
        }
    }
    if (demo || !was->primed) {
        *was = now;
        return;
    }

    /* The effects that some side must have felt. */
    u16 either = ru->felt[0] | ru->felt[1];
    if (asked(sfx, SFX_HIT_MECH)
        && !(either & (RUMBLE_FELT_HIT | RUMBLE_FELT_DESTROYED))) {
        broke(r, -1, "SFX_HIT_MECH, no side hit or destroyed");
    }
    /* (a wreck out of fuel is destroyed again while it flies home, its
     * debris count back at $180: the sound again, nothing new to feel) */
    bool again = false;
    for (int s = 0; s < 2; s++) {
        again = again || ((was->flags[s] & MECH_DESTROYED)
                          && (now.flags[s] & MECH_DESTROYED)
                          && now.hp[s] == 0x180);
    }
    if (asked(sfx, SFX_MECH_DESTROYED) && !(either & RUMBLE_FELT_DESTROYED)
        && !again) {
        char what[96];
        snprintf(what, sizeof what, "SFX_MECH_DESTROYED, no side destroyed "
                 "(flags %02X %02X, HP %u %u)", now.flags[0], now.flags[1],
                 now.hp[0], now.hp[1]);
        broke(r, -1, what);
    }
    /* (an HQ at 0, its floor, is hit on until the game sees it over) */
    if (asked(sfx, SFX_HQ_HIT) && !(now.hq_hp[0] < was->hq_hp[0])
        && !(now.hq_hp[1] < was->hq_hp[1]) && now.hq_hp[0] != 0
        && now.hq_hp[1] != 0) {
        char what[96];
        snprintf(what, sizeof what, "SFX_HQ_HIT, no HQ down (HQ HP %u %u, "
                 "were %u %u)", now.hq_hp[0], now.hq_hp[1], was->hq_hp[0],
                 was->hq_hp[1]);
        broke(r, -1, what);
    }
    bool at_beat = false;
    for (int s = 0; s < 2; s++) {
        at_beat = at_beat || was->hq_alert[s] == 0xA0
               || was->hq_alert[s] == 0x64 || was->hq_alert[s] == 0x28;
    }
    if (asked(sfx, SFX_HQ_ALERT) && !at_beat) {
        broke(r, -1, "SFX_HQ_ALERT, no side's count at a beat");
    }
    if (asked(sfx, SFX_MISSILE_ALERT) && !(either & RUMBLE_FELT_MISSILE)) {
        broke(r, -1, "SFX_MISSILE_ALERT, no beat felt");
    }
    if (asked(sfx, SFX_HQ_DESTROYED)
        && !((either & RUMBLE_FELT_HQ_LOST) && (either & RUMBLE_FELT_HQ_WON))) {
        broke(r, -1, "SFX_HQ_DESTROYED, no lost and won");
    }
    *was = now;
}

/* main.c's on_frame, the platform's part the rumble and its check. */
static void on_frame(HzGame *g, void *user)
{
    Run *r = user;
    if (g->quit) {
        return;
    }
    u32 sfx = g->sfx_asked;
    g->sfx_asked = 0;
    rumble_watch(&r->rumble, g, sfx);
    check_frame(r, g, sfx);
    r->title_frames++;
    if (r->motors_side >= 0) {
        RumbleMotors m = rumble_scale(rumble_motors(&r->rumble,
                                                    r->motors_side),
                                      r->motors_level);
        if (m.low != r->motors_last.low || m.high != r->motors_last.high) {
            printf("%d 1 %04X %04X\n", r->done + 1, m.low, m.high);
            r->motors_last = m;
        }
    }

    for (int p = 0; p < 2; p++) {
        Player *pl = &r->player[p];
        if (--pl->left <= 0) {
            player_next(pl, r->title);
        }
    }
    memset(g->pad_in, 0, sizeof g->pad_in);
    if (!r->idle && !(r->title && r->title_idle)) {
        pad_set(&g->pad_in[0], r->player[0].held);
        pad_set(&g->pad_in[1], r->player[1].held);
    }
    r->done++;
    if (r->done >= r->limit) {
        g->quit = true;
    }
}

static bool music_on(void *user)
{
    Run *r = user;
    return r->title_frames < TITLE_SONG_FRAMES;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: hz_rumble ASSETS SEED FRAMES [--verbose] "
                        "[--game MAP MODE DIFFICULTY] [--motors SIDE LEVEL]"
                        "\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    Run *r = &g_run;
    u64 seed = strtoull(argv[2], NULL, 0);
    r->limit = atoi(argv[3]);
    r->motors_side = -1;
    int game_map = -1, game_mode = -1, game_difficulty = -1;
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            r->verbose = true;
        } else if (strcmp(argv[i], "--game") == 0 && i + 3 < argc) {
            game_map = atoi(argv[++i]);
            game_mode = atoi(argv[++i]);
            game_difficulty = atoi(argv[++i]);
            r->idle = true;
        } else if (strcmp(argv[i], "--motors") == 0 && i + 2 < argc) {
            r->motors_side = atoi(argv[++i]) & 1;
            r->motors_level = (RumbleLevel)(atoi(argv[++i]) & 3);
        } else {
            fprintf(stderr, "hz_rumble: what is %s?\n", argv[i]);
            return 2;
        }
    }
    r->choices = seed * 5 + 0x7A11;
    for (int p = 0; p < 2; p++) {
        r->player[p].random = seed * 5 + (u64)p + 2;
    }
    rumble_reset(&r->rumble);

    static Video video;
    static HzGame game;
    HzGame *g = &game;
    video_init(&video);
    game_init(g, &assets, &video);
    g->frame = on_frame;
    g->frame_user = r;
    g->snd_music_on = music_on;
    g->snd_user = r;                /* music_on's; no snd_post: no sound */
    static const u32 FIXES[3] = { HZ_FIX_ALL, 0, HZ_FIX_DEFAULT };
    g->fixes = FIXES[next_random(&r->choices) % 3];
    static const u8 MODES[6] = { 0, 0, 1, 2, 3, 5 };
    g->hw_version = 0xA0;
    g->options[0] = 5;
    g->options[2] = 0xFF;
    snd_init(g);
    g->options[0] = MODES[next_random(&r->choices) % 6];
    g->options[1] = (u8)(next_random(&r->choices) % 4);
    g->options[3] = (u8)(next_random(&r->choices) % HZ_MAPS);
    if (r->idle) {
        /* main.c's game.fixes (the settings' default) and its picks */
        g->fixes = HZ_FIX_DEFAULT;
        g->options[0] = (u8)game_mode;
        g->options[1] = (u8)game_difficulty;
        g->options[3] = (u8)game_map;
    }
    bool skip_title = true;

    while (!g->quit) {                      /* main.c's MainLoop_Title */
        r->title = true;
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
        r->title = false;
        HzSetup setup = game_setup_after_title(g);
        if (setup == HZ_SETUP_NO_MAP) {
            fprintf(stderr, "hz_rumble: map %d doesn't load\n",
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
        /* main.c's: a new game's rumble, and its own state here. */
        rumble_reset(&r->rumble);
        r->seen.primed = false;
        g->sfx_asked = 0;
        game_start(g);
        HzLoopExit exit = game_frame(g, false);
        while (exit == HZ_LOOP_ON) {
            exit = game_frame(g, true);
        }
        if (exit == HZ_LOOP_GAME_OVER) {
            r->overs++;
            game_over(g);
        }
    }
    if (r->motors_side >= 0) {
        for (int s = 0; s < 2; s++) {
            printf("side %d:", s);
            for (int k = 0; k < RUMBLE_FELT_KINDS; k++) {
                printf(" %s %d", KIND_NAME[k], r->side_count[s][k]);
            }
            printf("\n");
        }
    }
    printf("seed %llu: %d frames, %d games (%d over), %d demos; felt",
           (unsigned long long)seed, r->done, r->games, r->overs, r->demos);
    for (int k = 0; k < RUMBLE_FELT_KINDS; k++) {
        printf(" %s %d", KIND_NAME[k], r->felt_count[k]);
    }
    printf("; %d broken\n", r->broken);
    return (r->broken != 0) ? 1 : 0;
}
