/* hz_netbot: the server and net_client.c without the game (session 56,
 * docs/netplay.md). Many players in one process, each its own NetClient
 * and socket, as the game's:
 *
 *     hz_netbot HOST PORT PAIRS FRAMES [--fast] [--split SIDE FRAME]
 *               [--list] [--hz P1 P2] [--pause SIDE FRAME MS] [--pace]
 *
 * 2 x PAIRS players connect, all QUICK (so they pair up as they come),
 * and each pair plays FRAMES frames of lockstep, a frame every 1/60 s
 * (--fast: as fast as the pads come). A player's pad on a frame is a
 * function of the game's seed, its side and the frame, so the side that
 * receives it checks every pad against what it should be; the state hash
 * every NET_HASH_EVERY frames is one too, the same on both sides unless
 * --split makes SIDE's differ from FRAME on (the split must be caught).
 * --list: before pairing, half the players HOST and the other half pick
 * a room off the list (JOIN), instead of QUICK.
 *
 * (Session 63) Each player keeps its frames on its own clock through
 * src/core/pace.c, as the game does with no display to wait on: a frame
 * each 1/60 s (--hz: P1's and P2's rates, so one clock runs fast), the
 * time it waited for the rival not owed (pace_held), and the balance
 * held back (net_game_slow, pace_slow). --pause: SIDE's player stands
 * still MS at FRAME, as with the Esc screen open. --pace: a line a
 * player at the end, the waits for the rival (all, and those after the
 * first 600 frames) and the balance (how far ahead at the end and at
 * most, the frames held back). HZ_NET_LAG and HZ_NET_JITTER as the
 * game's (net_client.h).
 *
 * HZ_NET_LOSS works as in the game. Prints the games played, pads
 * checked and wrong (0), the waits for the rival, and the frames' time;
 * exit 1 when a pad was wrong, a game didn't finish, or a split went
 * unseen. */

#include "../src/core/pace.h"
#include "../src/net/net_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    NetClient c;
    bool      started;
    bool      done;
    bool      finished;
    bool      split_seen;
    bool      rival_on;     /* a pad has come from the rival: no more 0s */
    u32       frame_due;    /* frames the clock has given this player */
    u64       wrong;
    u64       checked;
    /* (Session 63) Its clock. */
    Pace      pace;
    double    due;          /* s: its next frame */
    bool      waiting;
    double    wait_from;
    bool      paused;
    u32       waits, waits_late;
    double    wait_s, wait_max;
    double    began_s, ended_s;
} Bot;

/* The fine clock, s. */
static double now_s(void)
{
    return (double)net_now_us() / 1e6;
}

static u64 mix(u64 z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Never 0: the rival's first frames, before its delay is up, are. */
static u8 pad_of(u32 seed, int side, u32 frame)
{
    return (u8)(mix(((u64)seed << 32) ^ ((u64)side << 30) ^ frame) | 1);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: hz_netbot HOST PORT PAIRS FRAMES [--fast] "
                        "[--split SIDE FRAME] [--list]\n");
        return 2;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    int pairs = atoi(argv[3]);
    u32 frames = (u32)atol(argv[4]);
    bool fast = false;
    bool list = false;
    int split_side = -1;
    u32 split_frame = 0;
    double hz[2] = { 60.0, 60.0 };
    int pause_side = -1;
    u32 pause_frame = 0;
    double pause_s = 0.0;
    bool pace_lines = false;
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--fast") == 0) {
            fast = true;
        } else if (strcmp(argv[i], "--list") == 0) {
            list = true;
        } else if (strcmp(argv[i], "--hz") == 0 && i + 2 < argc) {
            hz[0] = atof(argv[++i]);
            hz[1] = atof(argv[++i]);
        } else if (strcmp(argv[i], "--pause") == 0 && i + 3 < argc) {
            pause_side = atoi(argv[++i]);
            pause_frame = (u32)atol(argv[++i]);
            pause_s = atof(argv[++i]) / 1000.0;
        } else if (strcmp(argv[i], "--pace") == 0) {
            pace_lines = true;
        } else if (strcmp(argv[i], "--split") == 0 && i + 2 < argc) {
            split_side = atoi(argv[++i]);
            split_frame = (u32)atol(argv[++i]);
        } else {
            fprintf(stderr, "hz_netbot: what is %s?\n", argv[i]);
            return 2;
        }
    }
    int n = 2 * pairs;
    Bot *bots = calloc((size_t)n, sizeof *bots);
    if (bots == NULL || pairs < 1 || hz[0] < 1.0 || hz[1] < 1.0) {
        return 2;
    }
    NetSettings settings = { 0, 0, 0, 0 };
    u64 t0 = net_now_ms();
    for (int i = 0; i < n; i++) {
        char name[16];
        snprintf(name, sizeof name, "BOT %d", i);
        net_client_init(&bots[i].c);
        if (!net_client_connect(&bots[i].c, host, port, name,
                                net_game_id(0x1234))) {
            fprintf(stderr, "hz_netbot: %s\n", bots[i].c.error);
            return 1;
        }
    }

    /* Online, then into rooms. */
    for (;;) {
        int online = 0;
        for (int i = 0; i < n; i++) {
            net_client_poll(&bots[i].c);
            if (bots[i].c.state == NETC_FAILED) {
                fprintf(stderr, "hz_netbot: player %d: %s\n", i,
                        bots[i].c.error);
                return 1;
            }
            online += bots[i].c.state == NETC_LOBBY;
        }
        if (online == n) {
            break;
        }
        net_sleep_ms(1);
    }
    u64 t_online = net_now_ms();
    if (list) {
        for (int i = 0; i < pairs; i++) {
            net_client_host(&bots[i].c, &settings);
        }
        /* Every host waiting, then each other player picks the first room
         * on its list it hasn't seen taken (a room another took answers
         * NOROOM: the list again). */
        for (int k = pairs; k < n; k++) {
            net_client_want_list(&bots[k].c, true);
        }
    } else {
        for (int i = 0; i < n; i++) {
            net_client_quick(&bots[i].c, &settings, true);
        }
    }

    int started = 0, finished = 0;
    u64 stalls = 0;
    while (finished < n) {
        for (int i = 0; i < n; i++) {
            Bot *b = &bots[i];
            NetClient *c = &b->c;
            net_client_poll(c);
            if (b->finished) {
                continue;
            }
            if (c->state == NETC_FAILED) {
                fprintf(stderr, "hz_netbot: player %d: %s\n", i, c->error);
                return 1;
            }
            if (list && c->state == NETC_LOBBY && !b->started
                && c->room_count > 0 && i >= pairs) {
                net_client_join(c, c->rooms[(u32)i % (u32)c->room_count].id,
                                &settings);
            }
            if (!b->started) {
                if (net_client_take_start(c)) {
                    b->started = true;
                    started++;
                    net_client_want_list(c, false);
                    net_game_begin(c, 0);
                    b->began_s = now_s();
                    pace_init(&b->pace, 1.0 / hz[c->side], b->began_s);
                    b->due = b->began_s;
                } else {
                    continue;
                }
            }
            if (b->done) {
                /* As net_game_finish would: what the rival hasn't got
                 * yet, and a split told. */
                if ((c->acked < c->local_next || b->split_seen)
                    && net_now_ms() - c->input_sent >= 16) {
                    net_game_send(c);
                }
                continue;
            }
            /* The game's frame: our pad for frame + delay, then wait for
             * the rival's for this one. */
            if ((fast || now_s() >= b->due) && b->frame_due == c->frame) {
                if (c->side == pause_side && c->frame == pause_frame
                    && !b->paused && pause_s > 0.0) {
                    b->paused = true;       /* the Esc screen, held open */
                    b->due = now_s() + pause_s;
                    pace_held(&b->pace, pause_s);
                    continue;
                }
                net_game_push(c, pad_of(c->seed, c->side, c->local_next));
                b->frame_due = c->frame + 1;
            }
            if (b->frame_due > c->frame) {
                NetGameStop stop = net_game_stopped(c);
                if (stop == NETG_DESYNC) {
                    b->split_seen = true;
                    b->done = true;
                    continue;
                }
                if (stop != NETG_ON) {
                    fprintf(stderr, "hz_netbot: player %d stopped at frame "
                            "%u: %s\n", i, (unsigned)c->frame,
                            net_game_stop_text(stop));
                    return 1;
                }
                if (!net_game_ready(c)) {
                    stalls++;
                    if (!b->waiting) {
                        b->waiting = true;
                        b->wait_from = now_s();
                        b->waits++;
                        b->waits_late += c->frame >= 600;
                    }
                    if (net_now_ms() - c->input_sent >= 16) {
                        net_game_send(c);
                    }
                    continue;
                }
                if (b->waiting) {
                    /* (Session 63) Not owed: no catch-up after it. */
                    double waited = now_s() - b->wait_from;
                    b->waiting = false;
                    b->wait_s += waited;
                    if (waited > b->wait_max) {
                        b->wait_max = waited;
                    }
                    if (!fast) {
                        pace_held(&b->pace, waited);
                    }
                }
                u32 f = c->frame;
                u8 pads[2];
                net_game_take(c, pads);
                if (!fast) {
                    /* (Session 63) The balance, then the frame paced as
                     * the game paces one with no display to wait on. */
                    pace_slow(&b->pace, net_game_slow(c) * b->pace.tick);
                    double t = now_s();
                    pace_frame(&b->pace, t);
                    PaceStep step = pace_presented(&b->pace, t, false,
                                                   false);
                    b->due = (step == PACE_SLEEP) ? b->pace.until : t;
                }
                b->checked++;
                /* None until the rival's delay is up (its own: we don't
                 * know it), then each its own. */
                u8 got = pads[1 - c->side];
                if (got != 0) {
                    b->rival_on = true;
                }
                if (b->rival_on ? got != pad_of(c->seed, 1 - c->side, f)
                                : got != 0) {
                    b->wrong++;
                }
                if (f % NET_HASH_EVERY == 0) {
                    u64 h = mix(((u64)c->seed << 20) ^ f);
                    if (c->side == split_side && f >= split_frame) {
                        h ^= 1;
                    }
                    net_game_hash(c, f, h);
                }
                if (c->frame >= frames) {
                    b->done = true;
                    b->ended_s = now_s();
                }
            }
        }
        /* All done: each player's last pads out, then out of its room. */
        bool all_done = true;
        for (int i = 0; i < n; i++) {
            all_done = all_done && (bots[i].done || bots[i].finished);
        }
        if (all_done) {
            u64 until = net_now_ms() + 500;
            while (net_now_ms() < until) {
                for (int i = 0; i < n; i++) {
                    net_client_poll(&bots[i].c);
                    if (bots[i].c.acked < bots[i].c.local_next
                        && net_now_ms() - bots[i].c.input_sent >= 16) {
                        net_game_send(&bots[i].c);
                    }
                }
                net_sleep_ms(1);
            }
            for (int i = 0; i < n; i++) {
                if (!bots[i].finished) {
                    net_game_finish(&bots[i].c);
                    bots[i].finished = true;
                    finished++;
                }
            }
        }
        if (!fast) {
            net_sleep_us(250);
        }
    }
    u64 t_end = net_now_ms();

    u64 checked = 0, wrong = 0;
    int splits_seen = 0;
    u32 delay_min = 99, delay_max = 0;
    for (int i = 0; i < n; i++) {
        const Bot *b = &bots[i];
        if (pace_lines) {
            double secs = b->ended_s - b->began_s;
            printf("pace: player %d P%d at %.4f Hz: %u frames in %.2f s "
                   "(%.4f a s); %u waits (%u after frame 600), %.1f ms, "
                   "longest %.1f ms; ahead %.2f at the end, %.2f at most; "
                   "held back %.2f frames; margins %.2f, rival %.2f\n", i,
                   b->c.side + 1, hz[b->c.side], (unsigned)b->checked, secs,
                   secs > 0 ? (double)b->checked / secs : 0.0,
                   (unsigned)b->waits, (unsigned)b->waits_late,
                   b->wait_s * 1000.0, b->wait_max * 1000.0,
                   net_game_lead(&b->c), b->c.lead_max, b->c.slowed,
                   b->c.margin, b->c.rival_margin);
        }
        checked += bots[i].checked;
        wrong += bots[i].wrong;
        splits_seen += bots[i].split_seen;
        delay_min = (u32)bots[i].c.delay < delay_min ? (u32)bots[i].c.delay
                                                     : delay_min;
        delay_max = (u32)bots[i].c.delay > delay_max ? (u32)bots[i].c.delay
                                                     : delay_max;
        net_client_disconnect(&bots[i].c);
    }
    double play_s = (double)(t_end - t_online) / 1000.0;
    printf("hz_netbot: %d players online in %llu ms; %d games started; %llu "
           "pads checked, %llu wrong; %llu waits; %.1f s (%.1f frames/s a "
           "player); delay %u-%u; splits seen %d\n",
           n, (unsigned long long)(t_online - t0), started / 2,
           (unsigned long long)checked, (unsigned long long)wrong,
           (unsigned long long)stalls, play_s,
           play_s > 0 ? (double)checked / (double)n / play_s : 0.0,
           (unsigned)delay_min, (unsigned)delay_max, splits_seen);
    bool bad = wrong != 0 || started != n
               || (split_side >= 0 && splits_seen != n)
               || (split_side < 0 && splits_seen != 0);
    return bad ? 1 : 0;
}
