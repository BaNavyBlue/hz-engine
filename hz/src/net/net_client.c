/* (Session 56) net_client.h: the lobby and the lockstep. */

#include "net_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELLO_EVERY_MS   500
#define HELLO_GIVE_UP_MS 5000
#define PING_EVERY_MS    1000
#define LIST_EVERY_MS    2000
#define ASK_EVERY_MS     500
#define ASK_GIVE_UP_MS   5000
#define SERVER_LOST_MS   10000  /* the lobby with nothing from the server */
#define RIVAL_SILENT_MS  10000  /* a game with nothing from the rival */
#define RESEND_MS        16     /* INPUT while waiting */
#define FINISH_MS        2000
#define AGAIN_EVERY_MS   500    /* (session 66) AGAIN after a game */
#define AGAIN_GIVE_UP_MS 3000   /* and no answer: the server can't */
#define SPLIT_TELL_MS    300

/* (Session 63) The balance (net_game_slow). The margins are read from
 * frame BALANCE_AFTER (the first pads are none on both sides, and the
 * first INPUTs on their way), each smoothed by BALANCE_ALPHA a sample (a
 * second or so); then a side ahead by more than BALANCE_ON frames holds
 * back BALANCE_GAIN of a frame a frame for each frame ahead (at most
 * BALANCE_MAX) until it is less than BALANCE_OFF ahead. Slow beside the
 * smoothing, so it doesn't swing past. */
#define BALANCE_AFTER    120
#define BALANCE_ALPHA    (1.0 / 32.0)
#define BALANCE_ON       0.75
#define BALANCE_OFF      0.25
#define BALANCE_GAIN     0.01
#define BALANCE_MAX      0.05

/* -- sending and receiving, with HZ_NET_LOSS -- */

static u64 next_random(u64 *s)
{
    u64 z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static bool lost(NetClient *c)
{
    if (c->loss <= 0.0) {
        return false;
    }
    double r = (double)(next_random(&c->random) >> 11) / 9007199254740992.0;
    return r < c->loss;
}

/* (Session 63) HZ_NET_LAG and HZ_NET_JITTER: what is sent, held. One
 * queue for the process (hz_netbot's players share it), made the first
 * time it's wanted. */
#define LAG_SLOTS 512

typedef struct {
    u64     due;                /* net_now_us */
    NetSock sock;
    NetAddr to;
    int     n;                  /* 0: a free slot */
    u8      data[NET_PACKET_MAX];
} Lagged;

static Lagged *g_lag;
static int     g_lag_us = -1;   /* -1: not read yet */
static int     g_jitter_us;

static void lag_flush(void)
{
    if (g_lag == NULL) {
        return;
    }
    u64 now = net_now_us();
    for (int i = 0; i < LAG_SLOTS; i++) {
        Lagged *l = &g_lag[i];
        if (l->n > 0 && l->due <= now) {
            net_send(l->sock, &l->to, l->data, l->n);
            l->n = 0;
        }
    }
}

/* A socket about to close: what it still had queued sent now (`send`),
 * or nowhere. */
static void lag_drop(NetSock sock, bool send)
{
    for (int i = 0; g_lag != NULL && i < LAG_SLOTS; i++) {
        Lagged *l = &g_lag[i];
        if (l->n > 0 && l->sock == sock) {
            if (send) {
                net_send(l->sock, &l->to, l->data, l->n);
            }
            l->n = 0;
        }
    }
}

static bool lag_hold(NetClient *c, const u8 *p, int n)
{
    if (g_lag_us < 0) {
        const char *lag = getenv("HZ_NET_LAG");
        const char *jitter = getenv("HZ_NET_JITTER");
        g_lag_us = (lag != NULL) ? (int)(atof(lag) * 1000.0) : 0;
        g_jitter_us = (jitter != NULL) ? (int)(atof(jitter) * 1000.0) : 0;
        if (g_lag_us > 0 || g_jitter_us > 0) {
            g_lag = calloc(LAG_SLOTS, sizeof *g_lag);
        }
    }
    if (g_lag == NULL) {
        return false;
    }
    u64 extra = (g_jitter_us > 0)
              ? next_random(&c->random) % (u64)(g_jitter_us + 1) : 0;
    for (int i = 0; i < LAG_SLOTS; i++) {
        Lagged *l = &g_lag[i];
        if (l->n == 0) {
            l->due = net_now_us() + (u64)g_lag_us + extra;
            l->sock = c->sock;
            l->to = c->server;
            l->n = n;
            memcpy(l->data, p, (size_t)n);
            return true;
        }
    }
    return false;               /* full: sent at once */
}

static void send_raw(NetClient *c, const u8 *p, int n)
{
    c->sent = net_now_ms();
    if (!lost(c) && !lag_hold(c, p, n)) {
        net_send(c->sock, &c->server, p, n);
    }
}

/* The header with our id and token. */
static void prefix(NetClient *c, u8 *p, NetType type)
{
    net_header(p, type);
    net_put32(p + 4, c->id);
    net_put32(p + 8, c->token);
}

static void fail(NetClient *c, const char *why)
{
    if (c->sock != NET_NO_SOCK) {
        lag_drop(c->sock, false);
        net_close(c->sock);
        c->sock = NET_NO_SOCK;
    }
    c->state = NETC_FAILED;
    snprintf(c->error, sizeof c->error, "%s", why);
    if (c->stop == NETG_ON && c->room != 0) {
        c->stop = NETG_SERVER_LOST;
    }
}

void net_client_init(NetClient *c)
{
    memset(c, 0, sizeof *c);
    c->sock = NET_NO_SOCK;
    c->rtt = 100;
    const char *loss = getenv("HZ_NET_LOSS");
    if (loss != NULL) {
        c->loss = atof(loss);
        if (c->loss > 0.9) {
            c->loss = 0.9;
        }
    }
    c->random = ((u64)net_random32() << 32) | net_random32();
}

u64 net_game_id(u64 assets_hash)
{
    u64 h = 0xCBF29CE484222325ull;
    u8 b[12];
    net_put32(b, NET_GAME_CODE);
    net_put64(b + 4, assets_hash);
    for (int i = 0; i < 12; i++) {
        h = (h ^ b[i]) * 0x00000100000001B3ull;
    }
    return h;
}

static void send_hello(NetClient *c)
{
    u8 p[NET_HELLO_LEN];
    memset(p, 0, sizeof p);
    net_header(p, NET_HELLO);
    net_put32(p + 4, c->nonce);
    net_put64(p + 8, c->game);
    net_name_put(p + 16, c->name);
    p[NET_HELLO_FLAGS] = c->ai ? NET_HELLO_AI : 0;
    send_raw(c, p, sizeof p);
}

bool net_client_connect(NetClient *c, const char *host, int port,
                        const char *name, u64 game)
{
    net_client_disconnect(c);
    double loss = c->loss;
    u64 random = c->random;
    bool ai = c->ai;
    net_client_init(c);
    c->loss = loss;
    c->random = random;
    c->ai = ai;
    if (!net_init()) {
        fail(c, "NO NETWORK");
        return false;
    }
    if (!net_resolve(host, port, &c->server)) {
        fail(c, "NO SUCH SERVER");
        return false;
    }
    c->sock = net_udp_open(0, &c->server);
    if (c->sock == NET_NO_SOCK) {
        fail(c, "NO NETWORK");
        return false;
    }
    net_name_clean(name, c->name);
    c->game = game;
    c->nonce = net_random32() | 1;
    c->began = net_now_ms();
    c->heard = c->began;
    c->state = NETC_CONNECTING;
    send_hello(c);
    return true;
}

void net_client_disconnect(NetClient *c)
{
    if (c->sock != NET_NO_SOCK && c->state != NETC_CONNECTING
        && c->state != NETC_FAILED) {
        u8 p[NET_BYE_LEN];
        prefix(c, p, NET_BYE);
        lag_drop(c->sock, true);
        net_send(c->sock, &c->server, p, sizeof p);   /* never "lost" */
    }
    if (c->sock != NET_NO_SOCK) {
        lag_drop(c->sock, false);
        net_close(c->sock);
        c->sock = NET_NO_SOCK;
    }
    c->state = NETC_OFFLINE;
    c->error[0] = '\0';
    c->room = 0;
}

static void send_ask(NetClient *c)
{
    u8 p[NET_JOIN_LEN];
    int n;
    if (c->ask == NET_HOST) {
        prefix(c, p, NET_HOST);
        memcpy(p + NET_PREFIX, c->ask_settings, NET_SETTINGS_LEN);
        net_put16(p + NET_PREFIX + NET_SETTINGS_LEN, c->ask_rtt);
        n = NET_HOST_LEN;
    } else {
        prefix(c, p, NET_JOIN);
        net_put32(p + NET_PREFIX, c->ask_room);
        memcpy(p + NET_PREFIX + 4, c->ask_settings, NET_SETTINGS_LEN);
        net_put16(p + NET_PREFIX + 4 + NET_SETTINGS_LEN, c->ask_rtt);
        p[NET_JOIN_MIN] = c->ask_flags;
        n = NET_JOIN_LEN;
    }
    send_raw(c, p, n);
    c->asked = net_now_ms();
}

static void ask(NetClient *c, NetType type, u32 room, const NetSettings *s,
                u8 flags)
{
    if (c->state == NETC_AFTER) {
        net_client_cancel(c);   /* (session 66) out of the last game's room */
    }
    if (c->state != NETC_LOBBY && c->state != NETC_WAITING
        && c->state != NETC_ASKING) {
        return;
    }
    if (c->state == NETC_WAITING && type == NET_JOIN && room == 0
        && (c->ask != NET_JOIN || c->ask_flags == flags)) {
        return;                 /* already waiting (a quick match's people
                                   only changed: told, session 58) */
    }
    if (c->state != NETC_WAITING || c->ask != NET_JOIN || room != 0) {
        c->ask_rtt = c->rtt;    /* (session 63) a quick match waiting
                                   keeps the first: the server does */
    }
    c->ask = type;
    c->ask_room = room;
    c->ask_flags = flags;
    net_settings_pack(s, c->ask_settings);
    c->state = NETC_ASKING;
    c->error[0] = '\0';
    c->began = net_now_ms();
    send_ask(c);
}

void net_client_host(NetClient *c, const NetSettings *s)
{
    ask(c, NET_HOST, 0, s, 0);
}

void net_client_quick(NetClient *c, const NetSettings *s, bool people_only)
{
    ask(c, NET_JOIN, 0, s, people_only ? NET_JOIN_PEOPLE : 0);
}

void net_client_join(NetClient *c, u32 room, const NetSettings *s)
{
    if (room != 0) {
        ask(c, NET_JOIN, room, s, 0);
    }
}

static void send_leave(NetClient *c, u32 room)
{
    u8 p[NET_LEAVE_LEN];
    prefix(c, p, NET_LEAVE);
    net_put32(p + NET_PREFIX, room);
    send_raw(c, p, sizeof p);
    send_raw(c, p, sizeof p);   /* twice: no answer comes to say it did */
}

void net_client_cancel(NetClient *c)
{
    if (c->state == NETC_AFTER) {
        send_leave(c, c->room);     /* (session 66) the rival hears LEFT */
        c->room = 0;
        c->state = NETC_LOBBY;
        return;
    }
    if (c->state != NETC_ASKING && c->state != NETC_WAITING) {
        return;
    }
    /* A HOST or quick JOIN not answered yet may still open a room: its
     * WAITING or START, coming to the LOBBY, is left then. */
    if (c->room != 0) {
        send_leave(c, c->room);
    }
    c->room = 0;
    c->state = NETC_LOBBY;
}

void net_client_want_list(NetClient *c, bool on)
{
    if (on && !c->list_wanted) {
        c->listed = 0;          /* at once */
    }
    c->list_wanted = on;
}

bool net_client_take_start(NetClient *c)
{
    bool s = c->start_new;
    c->start_new = false;
    return s;
}

const char *net_client_state_text(const NetClient *c)
{
    switch (c->state) {
    case NETC_CONNECTING:
        return "CONNECTING";
    case NETC_LOBBY:
        return "ONLINE";
    case NETC_ASKING:
        return "ASKING";
    case NETC_WAITING:
        return "WAITING FOR A RIVAL";
    case NETC_PLAYING:
        return "PLAYING";
    case NETC_AFTER:
        return "AFTER A GAME";
    case NETC_FAILED:
        return c->error;
    default:
        return "OFFLINE";
    }
}

/* -- what comes -- */

static void on_start(NetClient *c, const u8 *p, int n)
{
    if (n < NET_START_LEN) {
        return;
    }
    u32 room = net_get32(p + 4);
    u8 ready[NET_READY_LEN];
    prefix(c, ready, NET_READY);
    net_put32(ready + NET_PREFIX, room);
    if (c->state == NETC_PLAYING) {
        if (room == c->room) {
            send_raw(c, ready, sizeof ready);   /* our READY was lost */
        }
        return;
    }
    if (c->state == NETC_LOBBY) {
        /* A room from a request we gave up on: out of it, so the rival
         * hears at once. */
        send_leave(c, room);
        return;
    }
    if (c->state != NETC_WAITING && c->state != NETC_ASKING
        && c->state != NETC_AFTER) {
        return;                 /* (session 66) AFTER: the rematch */
    }
    c->room = room;
    c->side = p[8] & 1;
    c->seed = net_get32(p + 9);
    net_settings_unpack(p + 13, &c->settings);
    net_name_get(p + 13 + NET_SETTINGS_LEN, c->rival);
    c->rival_rtt = net_get16(p + 13 + NET_SETTINGS_LEN + NET_NAME_LEN);
    c->rival_ai = n > NET_START_FLAGS
                  && (p[NET_START_FLAGS] & NET_START_RIVAL_AI) != 0;
    c->state = NETC_PLAYING;
    c->start_new = true;
    c->stop = NETG_ON;
    send_raw(c, ready, sizeof ready);
}

/* (Session 63) A margin's sample into its smoothed value. */
static void smooth(double *v, bool *read, double sample)
{
    if (!*read) {
        *v = sample;
        *read = true;
    } else {
        *v += (sample - *v) * BALANCE_ALPHA;
    }
}

static void on_input(NetClient *c, const u8 *p, int n)
{
    if (c->state != NETC_PLAYING || n < NET_INPUT_S) {
        return;
    }
    const u8 *b = p + NET_HEADER;
    u32 ack = net_get32(b);
    u32 hframe = net_get32(b + 4);
    u64 hash = net_get64(b + 8);
    u32 first = net_get32(b + 16);
    int count = b[20];
    if (count > NET_INPUT_MAX || n < NET_INPUT_S + count) {
        return;
    }
    c->input_heard = net_now_ms();
    if (ack > c->acked && ack <= c->local_next) {
        c->acked = ack;
    }
    if (hframe & NET_HFRAME_SPLIT) {
        /* The rival saw the games split: so do we. */
        hframe &= ~NET_HFRAME_SPLIT;
        if (c->stop == NETG_ON) {
            c->stop = NETG_DESYNC;
            c->desync_frame = hframe;
        }
    }
    if (hframe > c->rival_hframe) {
        c->rival_hframe = hframe;
        c->rival_hash = hash;
        for (int i = 0; i < NET_HASHES; i++) {
            if (c->hash_frame[i] == hframe && hframe != 0
                && c->hash_value[i] != hash && c->stop == NETG_ON) {
                c->stop = NETG_DESYNC;
                c->desync_frame = hframe;
            }
        }
    }
    /* (Session 64) The rival's delay: an INPUT from the push in the
     * frame of its latest hash has its newest pad that frame and the
     * delay on; one from a later push, more (so a lost one reads too
     * much, never too little). The least since the hash before it. */
    u32 newest = first + (u32)count;
    if (count > 0 && count < NET_INPUT_MAX && newest > hframe
        && hframe >= c->rd_hframe) {
        int seen = (int)(newest - 1 - hframe);
        if (c->rd_now < 0 || hframe > c->rd_hframe) {
            c->rd_before = c->rd_now;
            c->rd_now = seen;
            c->rd_hframe = hframe;
        } else if (seen < c->rd_now) {
            c->rd_now = seen;
        }
        c->rival_delay = (c->rd_before >= 0 && c->rd_before < c->rd_now)
                       ? c->rd_before : c->rd_now;
    }
    /* (Session 63) The rival's margin, from the first INPUT of each push
     * (its pads up to its frame and our delay; a full INPUT may not reach
     * its newest): its frame then, and what it had of ours. */
    if (count > 0 && count < NET_INPUT_MAX && newest > c->rival_newest) {
        c->rival_newest = newest;
        if (newest > (u32)c->delay) {
            u32 at = newest - 1 - (u32)c->delay;
            if (at >= BALANCE_AFTER && ack >= at) {
                smooth(&c->rival_margin, &c->rival_margin_read,
                       (double)(ack - at));
            }
        }
    }
    for (int i = 0; i < count; i++) {
        u32 f = first + (u32)i;
        if (f < c->remote_next) {
            continue;
        }
        if (f > c->remote_next || f >= c->frame + NET_RING) {
            break;              /* a gap (can't be: see net_game_send) */
        }
        c->remote[f % NET_RING] = b[21 + i];
        c->remote_next = f + 1;
    }
}

static void on_datagram(NetClient *c, const u8 *p, int n)
{
    int type = net_type(p, n);
    if (type == 0) {
        return;
    }
    u64 now = net_now_ms();
    switch (type) {
    case NET_WELCOME:
        if (n >= NET_WELCOME_LEN && c->state == NETC_CONNECTING
            && net_get32(p + 4) == c->nonce) {
            c->id = net_get32(p + 8);
            c->token = net_get32(p + 12);
            c->waiting = net_get16(p + 16);
            c->players = net_get16(p + 18);
            c->view_rule = (n > NET_WELCOME_VIEW && p[NET_WELCOME_VIEW]
                            <= NET_VIEW_OWN) ? p[NET_WELCOME_VIEW]
                                             : NET_VIEW_HOST;
            c->state = NETC_LOBBY;
            c->heard = now;
            c->pinged = 0;      /* a PING at once: the round trip */
        }
        break;
    case NET_REFUSE:
        if (n >= NET_REFUSE_LEN) {
            u32 nonce = net_get32(p + 4);
            int why = p[8];
            if (c->state == NETC_CONNECTING && nonce == c->nonce
                && why == NET_REFUSE_BUSY) {
                /* Too many arriving at once: the HELLOs go on until
                 * HELLO_GIVE_UP_MS. */
            } else if (c->state == NETC_CONNECTING && nonce == c->nonce) {
                fail(c, why == NET_REFUSE_FULL ? "SERVER FULL"
                                               : "OTHER VERSION");
            } else if (nonce == 0 && why == NET_REFUSE_UNKNOWN
                       && c->state != NETC_CONNECTING) {
                fail(c, "SERVER DROPPED US");
            }
        }
        break;
    case NET_PONG:
        if (n >= NET_PONG_LEN) {
            u32 t = net_get32(p + 4);
            u32 rtt = (u32)(net_now_us() / 1000u) - t;  /* (s. 64) fine */
            if (rtt < 5000) {
                c->rtt = (u16)rtt;
                c->rtt_known = true;
            }
            c->waiting = net_get16(p + 8);
            c->players = net_get16(p + 10);
        }
        break;
    case NET_ROOMS:
        if (n >= NET_ROOMS_HEAD) {
            int count = p[8];
            if (count > NET_ROOM_ROWS
                || n < NET_ROOMS_HEAD + count * NET_ROOM_ENTRY) {
                break;
            }
            c->rooms_total = net_get16(p + 4);
            c->room_count = count;
            for (int i = 0; i < count; i++) {
                const u8 *e = p + NET_ROOMS_HEAD + i * NET_ROOM_ENTRY;
                NetRoomInfo *r = &c->rooms[i];
                r->id = net_get32(e);
                net_name_get(e + 4, r->name);
                r->same_game = (e[4 + NET_NAME_LEN] & NET_ROOM_SAME_GAME) != 0;
                r->ai = (e[4 + NET_NAME_LEN] & NET_ROOM_AI) != 0;
                r->secs = net_get16(e + 5 + NET_NAME_LEN);
            }
        }
        break;
    case NET_WAITING:
        if (n >= NET_WAITING_LEN && c->state == NETC_ASKING) {
            c->room = net_get32(p + 4);
            c->state = NETC_WAITING;
        } else if (n >= NET_WAITING_LEN && c->state == NETC_LOBBY) {
            /* A room opened by a request we gave up on: leave it. */
            send_leave(c, net_get32(p + 4));
        }
        break;
    case NET_NOROOM:
        if (c->state == NETC_ASKING) {
            c->state = NETC_LOBBY;
            snprintf(c->error, sizeof c->error, "TAKEN: PICK ANOTHER");
            c->listed = 0;
        }
        break;
    case NET_START:
        on_start(c, p, n);
        break;
    case NET_INPUT:
        on_input(c, p, n);
        break;
    case NET_LEFT:
        if (n >= NET_LEFT_LEN && net_get32(p + 4) == c->room) {
            if (c->state == NETC_PLAYING) {
                if (c->stop == NETG_ON) {
                    c->stop = (p[8] == NET_LEFT_LEFT) ? NETG_RIVAL_LEFT
                                                      : NETG_RIVAL_LOST;
                }
            } else if (c->state == NETC_WAITING) {
                c->state = NETC_LOBBY;
            } else if (c->state == NETC_AFTER) {
                /* (session 66) gone after the game: no rematch */
                c->state = NETC_LOBBY;
                c->room = 0;
                snprintf(c->error, sizeof c->error, "RIVAL LEFT");
            }
        }
        break;
    case NET_AGAIN:
        /* (Session 66) the server's answer: the rival's wish */
        if (n >= NET_AGAIN_S && c->state == NETC_AFTER
            && net_get32(p + 4) == c->room) {
            c->rival_again = (p[8] & 1) != 0;
            c->again_heard = true;
        }
        break;
    default:
        break;
    }
}

/* (Session 66) AGAIN: our wish, after a game. */
static void send_again(NetClient *c)
{
    u8 p[NET_AGAIN_C];
    prefix(c, p, NET_AGAIN);
    net_put32(p + NET_PREFIX, c->room);
    p[NET_PREFIX + 4] = c->again ? 1 : 0;
    send_raw(c, p, sizeof p);
    c->again_sent = net_now_ms();
}

void net_client_rematch(NetClient *c, bool want)
{
    if (c->state == NETC_AFTER) {
        c->again = want;
        send_again(c);
    }
}

void net_client_poll(NetClient *c)
{
    lag_flush();
    if (c->sock == NET_NO_SOCK) {
        return;
    }
    u8 buf[NET_PACKET_MAX + 1];
    for (int k = 0; k < 256; k++) {
        NetAddr from;
        int n = net_recv(c->sock, &from, buf, sizeof buf);
        if (n < 0) {
            break;
        }
        if (n > NET_PACKET_MAX || !net_addr_equal(&from, &c->server)
            || lost(c)) {
            continue;
        }
        c->heard = net_now_ms();
        on_datagram(c, buf, n);
        if (c->sock == NET_NO_SOCK) {
            return;             /* failed */
        }
    }

    u64 now = net_now_ms();
    switch (c->state) {
    case NETC_CONNECTING:
        if (now - c->began > HELLO_GIVE_UP_MS) {
            fail(c, "NO ANSWER");
        } else if (now - c->sent >= HELLO_EVERY_MS) {
            send_hello(c);
        }
        return;
    case NETC_ASKING:
        if (now - c->began > ASK_GIVE_UP_MS) {
            c->state = NETC_LOBBY;
            snprintf(c->error, sizeof c->error, "NO ANSWER");
        } else if (now - c->asked >= ASK_EVERY_MS) {
            send_ask(c);
        }
        break;
    case NETC_PLAYING:
        if (now - c->input_heard > RIVAL_SILENT_MS && c->stop == NETG_ON) {
            c->stop = NETG_SILENT;
        }
        return;                 /* the INPUTs keep us online */
    case NETC_AFTER:
        /* (Session 66) Our wish told every half second, the rival's in
         * the answer; none at all: a server without rematches. */
        if (!c->again_heard && now - c->after_began > AGAIN_GIVE_UP_MS) {
            net_client_cancel(c);
            snprintf(c->error, sizeof c->error, "NO REMATCH HERE");
        } else if (now - c->again_sent >= AGAIN_EVERY_MS) {
            send_again(c);
        }
        break;
    default:
        break;
    }
    if (c->state == NETC_OFFLINE || c->state == NETC_FAILED) {
        return;
    }
    if (now - c->heard > SERVER_LOST_MS) {
        fail(c, "SERVER LOST");
        return;
    }
    if (c->pinged == 0 || now - c->pinged >= PING_EVERY_MS) {
        u8 p[NET_PING_LEN];
        prefix(c, p, NET_PING);
        /* (Session 64) the time on the fine clock: Windows's ms clock
         * moves in 16 ms steps, and a round trip read 47 ms for 30 */
        net_put32(p + NET_PREFIX, (u32)(net_now_us() / 1000u));
        send_raw(c, p, sizeof p);
        c->pinged = now;
    }
    if (c->list_wanted && (c->listed == 0 || now - c->listed >= LIST_EVERY_MS)) {
        u8 p[NET_LIST_LEN];
        prefix(c, p, NET_LIST);
        net_put16(p + NET_PREFIX, 0);
        send_raw(c, p, sizeof p);
        c->listed = now;
    }
}

/* -- the lockstep -- */

void net_game_begin(NetClient *c, int delay)
{
    u32 trip = (u32)c->ask_rtt + c->rival_rtt;
    if (delay <= 0) {
        /* The rival's pad comes over its line and ours: half of each
         * round trip, in frames, and two for the bumps. (Session 64)
         * Against the server's AI two frames: it takes the rest
         * (net_game_push). */
        if (c->rival_ai) {
            delay = NET_AI_PERSON_DELAY;
        } else {
            delay = (int)((trip / 2 * 60 + 999) / 1000) + 2;
        }
    }
    if (delay > NET_DELAY_MAX) {
        delay = NET_DELAY_MAX;
    }
    c->delay = delay;
    memset(c->local, 0, sizeof c->local);
    memset(c->remote, 0, sizeof c->remote);
    c->local_next = (u32)delay;     /* frames 0..delay-1: no pad */
    c->remote_next = 0;
    c->acked = 0;
    c->frame = 0;
    memset(c->hash_frame, 0, sizeof c->hash_frame);
    c->rival_hframe = 0;
    c->sent_hframe = 0;
    c->stop = NETG_ON;
    c->stalls = 0;
    c->input_heard = net_now_ms();
    c->input_sent = 0;
    c->margin_read = false;
    c->rival_margin_read = false;
    c->rival_newest = 0;
    c->balancing = false;
    c->slowed = 0.0;
    c->lead_max = 0.0;
    const char *balance = getenv("HZ_NET_BALANCE");
    c->balance_off = balance != NULL && strcmp(balance, "0") == 0;
    c->rival_delay = -1;
    c->rd_hframe = 0;
    c->rd_now = -1;
    c->rd_before = -1;
    c->ai_sum = (int)((trip * 60 + 999) / 1000) + 4;
}

/* (Session 64) The delay wanted now (net_game_push): what the rival's
 * leaves of the sum -- the server's AI all of it, the person only past
 * its own -- and only ever more. The rival's read too high (an INPUT
 * lost) would ask for less, so less is never taken: a side that gave
 * some back would leave the other to take it, and the two to settle the
 * other way round. */
static int delay_wanted(const NetClient *c)
{
    if (c->rival_delay < 0 || !(c->ai || c->rival_ai)) {
        return c->delay;
    }
    int want = c->ai_sum - c->rival_delay;
    int most = c->ai ? NET_AI_DELAY_MAX : NET_DELAY_MAX;
    if (want > most) {
        want = most;
    }
    return want > c->delay ? want : c->delay;
}

void net_game_push(NetClient *c, u8 pad)
{
    /* Never more than the ring's worth ahead of what the rival has. */
    if (c->local_next - c->acked >= NET_RING) {
        return;
    }
    if (c->frame >= BALANCE_AFTER) {
        smooth(&c->margin, &c->margin_read,
               (double)(c->remote_next - c->frame));      /* (s. 63) */
    }
    /* (Session 64) The pads up to this frame and the delay: one, more
     * when the delay grew (this pad held). */
    c->delay = delay_wanted(c);
    while (c->local_next < c->frame + (u32)c->delay + 1
           && c->local_next - c->acked < NET_RING) {
        c->local[c->local_next % NET_RING] = pad;
        c->local_next++;
    }
    net_game_send(c);
}

bool net_game_ready(const NetClient *c)
{
    return c->remote_next > c->frame && c->local_next > c->frame;
}

void net_game_take(NetClient *c, u8 pads[2])
{
    pads[c->side] = c->local[c->frame % NET_RING];
    pads[1 - c->side] = c->remote[c->frame % NET_RING];
    c->frame++;
}

void net_game_send(NetClient *c)
{
    if (c->state != NETC_PLAYING) {
        return;
    }
    /* Every pad from the first the rival hasn't said it has: so a gap
     * never opens on its side, however many are lost. */
    u32 first = c->acked;
    u32 count = c->local_next - first;
    if (count > NET_INPUT_MAX) {
        count = NET_INPUT_MAX;
    }
    u8 p[NET_INPUT_C + NET_INPUT_MAX];
    prefix(c, p, NET_INPUT);
    net_put32(p + NET_PREFIX, c->room);
    u8 *b = p + NET_PREFIX + 4;
    net_put32(b, c->remote_next);
    net_put32(b + 4, c->sent_hframe
                     | ((c->stop == NETG_DESYNC) ? NET_HFRAME_SPLIT : 0));
    net_put64(b + 8, c->sent_hash);
    net_put32(b + 16, first);
    b[20] = (u8)count;
    for (u32 i = 0; i < count; i++) {
        b[21 + i] = c->local[(first + i) % NET_RING];
    }
    send_raw(c, p, NET_INPUT_C + (int)count);
    c->input_sent = net_now_ms();
}

void net_game_hash(NetClient *c, u32 frame, u64 hash)
{
    int i = (int)((frame / NET_HASH_EVERY) % NET_HASHES);
    c->hash_frame[i] = frame;
    c->hash_value[i] = hash;
    c->sent_hframe = frame;
    c->sent_hash = hash;
    if (frame != 0 && c->rival_hframe == frame && c->rival_hash != hash
        && c->stop == NETG_ON) {
        c->stop = NETG_DESYNC;
        c->desync_frame = frame;
    }
}

double net_game_lead(const NetClient *c)
{
    if (!c->margin_read || !c->rival_margin_read) {
        return 0.0;
    }
    return (c->rival_margin - c->margin) / 2.0;
}

double net_game_slow(NetClient *c)
{
    if (c->rival_ai) {
        return 0.0;             /* it has no clock: it runs on our pads */
    }
    double lead = net_game_lead(c);
    if (lead > c->lead_max) {
        c->lead_max = lead;
    }
    if (!c->balancing && lead > BALANCE_ON) {
        c->balancing = true;
    } else if (c->balancing && lead < BALANCE_OFF) {
        c->balancing = false;
    }
    if (!c->balancing || c->balance_off) {
        return 0.0;
    }
    double hold = BALANCE_GAIN * lead;
    if (hold > BALANCE_MAX) {
        hold = BALANCE_MAX;
    }
    c->slowed += hold;
    return hold;
}

NetGameStop net_game_stopped(const NetClient *c)
{
    if (c->state == NETC_FAILED) {
        return NETG_SERVER_LOST;
    }
    if (c->stop == NETG_ON && net_game_ready(c)) {
        return NETG_ON;
    }
    /* What's waiting can still be played out. */
    return c->stop;
}

const char *net_game_stop_text(NetGameStop why)
{
    switch (why) {
    case NETG_RIVAL_LEFT:
        return "RIVAL LEFT";
    case NETG_RIVAL_LOST:
        return "RIVAL LOST";
    case NETG_SILENT:
        return "NOTHING FROM RIVAL";
    case NETG_DESYNC:
        return "OUT OF SYNC";
    case NETG_SERVER_LOST:
        return "SERVER LOST";
    default:
        return "";
    }
}

/* Until the rival has all our pads (net_game_finish, net_game_after). */
static void finish_wait(NetClient *c)
{
    /* Until the rival has all our pads; after a split, a moment more so
     * it hears of the split from us (its LEFT, else, would say only that
     * we left). */
    u64 began = net_now_ms();
    u64 until = began + FINISH_MS;
    for (;;) {
        bool owed = c->stop == NETG_ON && c->acked < c->local_next;
        bool telling = c->stop == NETG_DESYNC
                       && net_now_ms() - began < SPLIT_TELL_MS;
        if (!(owed || telling) || c->state != NETC_PLAYING
            || net_now_ms() >= until) {
            break;
        }
        net_wait(c->sock, RESEND_MS);
        net_client_poll(c);
        if (net_now_ms() - c->input_sent >= RESEND_MS) {
            net_game_send(c);
        }
    }
}

void net_game_finish(NetClient *c)
{
    if (c->state != NETC_PLAYING) {
        return;
    }
    finish_wait(c);
    if (c->state == NETC_PLAYING) {
        send_leave(c, c->room);
        c->state = NETC_LOBBY;
        c->heard = net_now_ms();
    }
    c->room = 0;
}

bool net_game_after(NetClient *c)
{
    if (c->state == NETC_PLAYING && c->stop == NETG_ON) {
        finish_wait(c);
    }
    if (c->state != NETC_PLAYING || c->stop != NETG_ON) {
        /* cut short: why, for the page (the rival left in the results) */
        if (c->state == NETC_PLAYING && c->stop != NETG_ON) {
            snprintf(c->error, sizeof c->error, "%s",
                     net_game_stop_text(c->stop));
        }
        net_game_finish(c);
        return false;
    }
    c->error[0] = '\0';
    c->state = NETC_AFTER;
    c->heard = net_now_ms();
    c->again = false;
    c->rival_again = false;
    c->again_heard = false;
    c->after_began = c->heard;
    send_again(c);
    return true;
}
