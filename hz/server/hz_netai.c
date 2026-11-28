/* hz_netai: the server's nightmare AI (session 58; docs/netplay.md).
 *
 *     hz_netai ASSETS [--server HOST[:PORT]] [--name NAME] [--quiet]
 *
 * A player the server runs, for when no one else is there. Online as NAME
 * (NIGHTMARE AI) at the server (127.0.0.1 and hz_server's port unless
 * --server), saying it is the AI (HELLO's NET_HELLO_AI), it HOSTs and
 * waits: hz_server lists it for anyone to pick, and gives it a quick
 * match left waiting for people (its --ai-after). The person plays on
 * their own map, difficulty and PATCHES, as P1 or P2 by a coin's toss
 * ((session 68) hz_server's --ai-side).
 *
 * Each game is main.c's network game (run_net_game) with no window and no
 * sound: a cold boot on both machines, two players on the split screen,
 * lockstep. Its side's pad is the nightmare AI's (src/game/nightmare.c),
 * as tools/hz_arena.c's bot plays a human's side: a human's pad and
 * handicaps. (Session 64) It thinks between frames, ahead: on a copy of
 * the game run on to the frame its pad will be read in -- its delay and
 * one more -- with its own pads sent for the frames between and the
 * person's as far as they have come, then held
 * (nightmare_think_ahead); only the pad it wants comes out, sent as any
 * player's pad is, and its memory (HzGame.nightmare[side]) is kept.
 * Nothing in a two-player game reads that memory, and the AI writes
 * nothing else, so this game and the person's stay the same (the state
 * hash every second says so). Nothing is drawn here: the draw layer
 * keeps what the person's machine's shows (its frame is composed only to
 * be seen), so the display's part of the hash is alike.
 *
 * (Session 64) Its delay is what the person's leaves of the round trip
 * and 4 frames (net_game_push): a person from session 64 on plays with
 * 2 frames and the AI takes the rest; one from before takes it all as
 * before, and the AI keeps 1. So that its first pads already go out with
 * its delay, it waits for the person's first INPUT before its first.
 *
 * One game at a time; for more at once, more of it (server/
 * hz-netai@.service: an instance each). After a game, the next: (session
 * 66) a game that ended as games do, it asks for a rematch at once and
 * waits up to AFTER_MAX_MINUTES in the room for the person's; LEFT, or
 * that long, and it HOSTs again. With the server gone it tries again
 * every 5 s. A game held on one frame HOLD_MAX_MINUTES, or longer than
 * GAME_MAX_HOURS, is left (session 75; before, any past two hours). A line a
 * game on stdout, unless --quiet. HZ_NETLOG, HZ_NET_LOSS and (session 66)
 * HZ_NET_END as the game's.
 *
 * (Session 67) Each game is recorded (replay.h, kind "netai": `side` the
 * AI's), in --replays DIR, else the game's own folder ($HZ_SAVES/replays,
 * or saves/replays where it runs); --no-replays none. The nightmare AI is
 * made better from how people beat it, and this way every game anyone
 * plays it can be looked at (hz_replay) without their sending a file.
 * SIGINT or SIGTERM: out (BYE) and stops. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "../src/core/hz_file.h"
#include "../src/core/hz_mods.h"
#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/nightmare.h"
#include "../src/game/replay.h"
#include "../src/game/statehash.h"
#include "../src/game/tick.h"
#include "../src/net/net_client.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define make_dir(path) mkdir(path, 0755)
#endif

#define RETRY_MS         5000
/* (Session 75) A game is left when one frame has been waited on
 * HOLD_MAX_MINUTES, or it has gone on GAME_MAX_HOURS. Until session 75 it
 * was any game past 120 minutes from its start: The owner's game of 15
 * November on STADT (10:55 UTC), played at full pace for two hours, was
 * left by the AI at frame 431,997, and his side showed it waiting for the
 * other player. A rival who sends nothing is caught by the game itself in
 * 10 s (RIVAL_SILENT_MS, NETG_SILENT). */
#ifndef HOLD_MAX_MINUTES        /* -D for a check: 0 leaves at once */
#define HOLD_MAX_MINUTES 10
#endif
#define GAME_MAX_HOURS   12
#define AFTER_MAX_MINUTES 3     /* (session 66) the person's rematch */

typedef struct {
    NetClient net;
    int       side;
    u8        want;         /* the AI's pad from this frame's think */
    NetGameStop why;        /* why the game stopped early */
    bool      too_long;
    u64       began;
    FILE     *log;          /* HZ_NETLOG */
    bool      quiet;
    int       end_at, end_who;  /* (session 66) HZ_NET_END, as main.c's */
    /* (Session 67) each game recorded (replay.h, kind "netai") */
    const char *replays;    /* the folder, NULL none */
    u64       game_id;
    HzReplay  replay;
    bool      recording;
    bool      over;         /* (session 81) this game ended with an HQ */
} Bot;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* (Session 64) Between frames, before the next pass: the pad the AI
 * wants for the frame its next push sets (c->frame + delay, read in the
 * pass after it: delay + 1 passes on), thought ahead on a copy run on to
 * it. pad_in holds frame c->frame - 1's pads, this pass's; the copy's
 * passes after it read the frames after: ours as sent, the person's as
 * far as they have come and then the last held. */
static void think(Bot *b, HzGame *g)
{
    static u8 pads[2][NET_AI_DELAY_MAX + 2];
    NetClient *c = &b->net;
    int ahead = c->delay + 1;
    if (c->frame == 0 || ahead > NET_AI_DELAY_MAX + 1) {
        return;
    }
    u8 own = 0, rival = 0;
    for (int i = 0; i <= ahead; i++) {
        u32 f = c->frame - 1 + (u32)i;
        if (f < c->local_next) {
            own = c->local[f % NET_RING];
        }
        if (f < c->remote_next) {
            rival = c->remote[f % NET_RING];
        }
        pads[b->side][i] = own;
        pads[1 - b->side][i] = rival;
    }
    const u8 *const both[2] = { pads[0], pads[1] };
    b->want = nightmare_think_ahead(g, b->side, ahead, both);
}

/* main.c's net_frame, with no window: the raster writes, the hash every
 * NET_HASH_EVERY frames, our pad (the AI's, none outside play) sent, the
 * person's for this frame waited for, both set. */
static void on_frame(HzGame *g, void *user)
{
    Bot *b = user;
    NetClient *c = &b->net;
    if (g->quit) {
        return;
    }
    if (b->end_at > 0 && c->frame == (u32)b->end_at) {
        for (int p = 0; p < 2; p++) {           /* (session 66) a check's */
            if (b->end_who & (1 << p)) {
                post_set_hq_hp(&g->posts[HZ_POST_HQ + p], 0);
            }
        }
    }
    if (c->frame == 0) {
        /* (Session 64) the person's delay read first */
        while (c->rival_delay < 0 && net_game_stopped(c) == NETG_ON
               && !g_stop) {
            net_client_poll(c);
            if (c->rival_delay < 0) {
                net_wait(c->sock, 2);
            }
        }
    }
    if (c->frame % NET_HASH_EVERY == 0) {
        u64 hash = state_hash(g);
        net_game_hash(c, c->frame, hash);
        if (b->log != NULL) {
            fprintf(b->log, "H %u %016llX\n", (unsigned)c->frame,
                    (unsigned long long)hash);
        }
    }
    net_game_push(c, b->want);
    b->want = 0;
    u64 held_from = net_now_ms();
    for (;;) {
        net_client_poll(c);
        NetGameStop stop = net_game_stopped(c);
        if (stop != NETG_ON || g_stop) {
            b->why = stop;
            g->quit = true;
            return;
        }
        if (net_game_ready(c)) {
            break;
        }
        u64 now = net_now_ms();
        if (now - held_from > (u64)HOLD_MAX_MINUTES * 60000u
            || now - b->began > (u64)GAME_MAX_HOURS * 3600000u) {
            b->too_long = true;
            g->quit = true;
            return;
        }
        if (now - c->input_sent >= 16) {
            net_game_send(c);
        }
        net_wait(c->sock, 2);
    }
    u8 pads[2];
    net_game_take(c, pads);
    for (int p = 0; p < 2; p++) {
        HzPad *to = &g->pad_in[p];
        to->up = (pads[p] & HZ_PAD_UP) != 0;
        to->down = (pads[p] & HZ_PAD_DOWN) != 0;
        to->left = (pads[p] & HZ_PAD_LEFT) != 0;
        to->right = (pads[p] & HZ_PAD_RIGHT) != 0;
        to->a = (pads[p] & HZ_PAD_A) != 0;
        to->b = (pads[p] & HZ_PAD_B) != 0;
        to->c = (pads[p] & HZ_PAD_C) != 0;
        to->start = (pads[p] & HZ_PAD_START) != 0;
    }
    if (b->recording && !replay_record(&b->replay, g)) {
        fprintf(stderr, "hz_netai: no memory; this game isn't recorded\n");
        b->recording = false;
        replay_free(&b->replay);
    }
}

/* (Session 67) The game's recording written to b->replays (made if it
 * isn't there): every game a person plays it, so that how it was beaten
 * can be read (hz_replay) without the person sending a file. */
static void save_replay(Bot *b, HzGame *g, bool over)
{
    if (!b->recording) {
        return;
    }
    b->recording = false;
    const char *why = over ? "over"
                    : b->too_long ? "held too long"
                    : b->why != NETG_ON ? net_game_stop_text(b->why)
                    : "quit";
    replay_end(&b->replay, over ? (int)g->winner : -1, why);
    make_dir(b->replays);
    char name[96];
    replay_file_name(&b->replay, name, sizeof name);
    char path[640];
    snprintf(path, sizeof path, "%s/%s", b->replays, name);
    if (!replay_save(&b->replay, path)) {
        fprintf(stderr, "hz_netai: can't write %s\n", path);
    } else if (!b->quiet) {
        printf("hz_netai: recorded %s (%u frames)\n", path,
               (unsigned)b->replay.frames);
    }
    replay_free(&b->replay);
}

/* In a network game the host's sound decides nothing (main.c's). */
static bool music_on(void *user)
{
    (void)user;
    return true;
}

/* (Session 81) What MainLoop_Title tells: the game set up (its replay
 * begun), a pass over (the AI thinks, session 64: between frames), the
 * game's end (its replay saved; who won, to the log). */
static void on_event(HzGame *g, void *user)
{
    Bot *b = user;
    NetClient *c = &b->net;
    switch ((HzEvent)g->event) {
        case HZ_EVENT_GAME: {
            b->recording = false;
            if (b->replays != NULL) {
                replay_begin(&b->replay, g, b->game_id, NET_GAME_CODE,
                             "netai", c->side, c->rival);
                b->recording = true;
            }
            break;
        }
        case HZ_EVENT_PASS: {
            think(b, g);
            break;
        }
        case HZ_EVENT_ENDED: {
            bool over = g->frame_exit == HZ_LOOP_GAME_OVER;
            save_replay(b, g, over);
            if (over) {
                b->over = true;
                if (b->log != NULL) {
                    fprintf(b->log, "R %u %s\n", (unsigned)c->frame,
                            g->winner == HZ_DRAW ? "DRAW"
                            : g->winner != 0 ? "P2" : "P1");
                }
            }
            break;
        }
        default: {
            break;
        }
    }
}

/* One game, from its START: main.c's run_net_game. It starts with a
 * frame's delay, and takes what the person's leaves (net_game_push,
 * session 64); it thinks ahead by it (think). Before session 64 it kept
 * one frame and thought on the frame as it was: at three it overshot its
 * landings and ran out of fuel carrying. */
static void play(Bot *b, HzGame *g, Video *video, const HzAssets *assets)
{
    NetClient *c = &b->net;
    video_init(video);
    game_init(g, assets, video);
    g->snd_music_on = music_on;
    g->hw_version = 0xA0;
    g->options[0] = 5;
    g->options[2] = 0xFF;
    snd_init(g);
    g->options[0] = 0;                  /* two players, the split screen */
    g->options[1] = c->settings.difficulty;
    g->options[3] = c->settings.map;
    g->fixes = c->settings.fixes | HZ_FIX_DRAW;     /* (session 66) */
    g->own_view = c->settings.view ? (u8)(c->side + 1) : 0;    /* (s. 59) */
    g->net_player = (u8)(c->side + 1);  /* (session 66) the hash's rule */
    g->rng_seed_saved = c->seed;
    b->side = c->side;
    b->want = 0;
    b->why = NETG_ON;
    b->too_long = false;
    b->began = net_now_ms();
    net_game_begin(c, 1);       /* then what the person's leaves */
    b->over = false;
    /* MainLoop_Title once round, no title: the setup, the game, its end
     * (on_event) */
    tick_main(g, HZ_MAIN_NO_TITLE | HZ_MAIN_ONCE, on_frame, on_event, b);
    bool over = b->over;
    if (b->log != NULL) {
        fprintf(b->log, "E %u %s\n", (unsigned)c->frame,
                b->why != NETG_ON ? net_game_stop_text(b->why) : "-");
        fflush(b->log);
    }
    if (!b->quiet) {
        printf("hz_netai: against %s as P%d on map %d, delay %d (the "
               "person's %d): %s at frame %u (%u waits)\n", c->rival,
               b->side + 1, c->settings.map, c->delay, c->rival_delay,
               b->too_long ? "held too long"
               : b->why != NETG_ON ? net_game_stop_text(b->why)
               : !over ? "ended"
               : (g->winner == HZ_DRAW) ? "a draw"
               : (g->winner == (u16)b->side) ? "the AI won" : "the AI lost",
               (unsigned)c->frame, (unsigned)c->stalls);
        printf("hz_netai: %s\n", nightmare_summary(g, b->side));
        fflush(stdout);
    }
    /* (Session 66) Always up for a rematch: asked at once (main's loop
     * waits in the room for the person's). (Session 75) A game held too
     * long is left (net_game_finish: back to LOBBY, and HOST again):
     * until then it stayed PLAYING, never hosted again, and on 15
     * November the server had one AI online for six hours. */
    if (b->too_long) {
        net_game_finish(c);
    } else if (net_game_after(c)) {
        net_client_rematch(c, true);
    }
}

static int usage(void)
{
    fprintf(stderr, "usage: hz_netai ASSETS [--server HOST[:PORT]] "
                    "[--name NAME] [--replays DIR | --no-replays] "
                    "[--quiet]\n"
                    "  the nightmare AI as a player at the server "
                    "(127.0.0.1:%d by default)\n", NET_PORT_DEFAULT);
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2 || strncmp(argv[1], "--", 2) == 0) {
        return usage();
    }
    const char *assets_dir = argv[1];
    char host[128] = "127.0.0.1";
    int port = NET_PORT_DEFAULT;
    const char *name = "NIGHTMARE AI";
    static Bot bot;
    Bot *b = &bot;
    /* (session 67) the game's own folder by default: $HZ_SAVES/replays,
     * else saves/replays */
    static char replays[512];
    const char *saves = getenv("HZ_SAVES");
    snprintf(replays, sizeof replays, "%s",
             (saves != NULL && saves[0] != '\0') ? saves : "saves");
    b->replays = replays;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) {
            b->quiet = true;
        } else if (strcmp(argv[i], "--replays") == 0 && i + 1 < argc) {
            b->replays = argv[++i];
        } else if (strcmp(argv[i], "--no-replays") == 0) {
            b->replays = NULL;
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            /* HOST, HOST:PORT, [V6], [V6]:PORT */
            snprintf(host, sizeof host, "%s", argv[++i]);
            char *close = strchr(host, ']');
            char *colon = strchr(host, ':');
            if (host[0] == '[' && close != NULL) {
                if (close[1] == ':') {
                    port = atoi(close + 2);
                }
                *close = '\0';
                memmove(host, host + 1, strlen(host));
            } else if (colon != NULL && strchr(colon + 1, ':') == NULL) {
                *colon = '\0';
                port = atoi(colon + 1);
            }
        } else {
            return usage();
        }
    }
    if (port <= 0 || port > 65535) {
        return usage();
    }
    if (b->replays == replays) {
        make_dir(replays);
        size_t n = strlen(replays);
        snprintf(replays + n, sizeof replays - n, "/replays");
    }

    /* The assets as the game reads them (no mods), hashed as it hashes
     * them: a person meets this only with the same. */
    static HzAssets assets;
    hz_mods_set(assets_dir, "");
    hz_file_hash_begin();
    bool loaded = hz_assets_load(&assets, assets_dir);
    u64 game_id = net_game_id(hz_file_hash_end());
    b->game_id = game_id;
    if (!loaded) {
        fprintf(stderr, "hz_netai: can't load the assets from %s/\n",
                assets_dir);
        return 1;
    }
    const char *log = getenv("HZ_NETLOG");
    if (log != NULL && log[0] != '\0') {
        b->log = fopen(log, "wb");
    }
    const char *end = getenv("HZ_NET_END");         /* (session 66) */
    if (end != NULL && sscanf(end, "%d:%d", &b->end_at, &b->end_who) != 2) {
        b->end_at = 0;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    static Video video;
    static HzGame game;
    NetClient *c = &b->net;
    net_client_init(c);
    c->ai = true;
    NetSettings any = { 0, 0, HZ_FIX_DEFAULT, 0 };  /* the person's win */
    u64 retry_at = 0;
    if (!b->quiet) {
        printf("hz_netai: %s at %s:%d\n", name, host, port);
        fflush(stdout);
    }
    while (!g_stop) {
        u64 now = net_now_ms();
        if ((c->state == NETC_OFFLINE || c->state == NETC_FAILED)
            && now >= retry_at) {
            if (c->state == NETC_FAILED && !b->quiet) {
                printf("hz_netai: %s; again in %d s\n", c->error,
                       RETRY_MS / 1000);
                fflush(stdout);
            }
            retry_at = now + RETRY_MS;
            net_client_connect(c, host, port, name, game_id);
        }
        net_client_poll(c);
        if (c->state == NETC_AFTER
            && now - c->after_began > (u64)AFTER_MAX_MINUTES * 60000u) {
            net_client_cancel(c);       /* (session 66) no rematch came */
        }
        if (c->state == NETC_LOBBY && c->rtt_known) {
            net_client_host(c, &any);   /* its round trip told, measured */
        }
        if (net_client_take_start(c)) {
            play(b, &game, &video, &assets);
            continue;
        }
        if (c->sock != NET_NO_SOCK) {
            net_wait(c->sock, 50);
        } else {
            net_sleep_ms(50);
        }
    }
    net_client_disconnect(c);
    if (b->log != NULL) {
        fclose(b->log);
    }
    return 0;
}
