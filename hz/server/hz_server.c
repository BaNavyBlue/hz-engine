/* hz_server: the network game's server (session 56; docs/netplay.md).
 *
 *     hz_server [--port N] [--players N] [--ai-after S]
 *               [--ai-side random|p1|p2] [--view host|split|own]
 *               [--verbose] [--quiet]
 *
 * One UDP socket (NET_PORT_DEFAULT, 19890, unless --port), one thread, no
 * library but the system's. It never runs the game: both machines do, in
 * lockstep, and the server only
 *   - keeps the players online (HELLO, then PING every second or so; one
 *     heard from for TIMEOUT_MS is dropped),
 *   - keeps the rooms: a player who HOSTs waits in one, on the list LIST
 *     pages through; JOIN takes a room off the list (room 0, a quick
 *     match: the oldest waiting with the same game, else the asker waits
 *     in a room of its own, open to anyone); a HOST arriving while a quick
 *     match waits takes it at once, with the host's settings,
 *   - starts a game (START to both: the side, the seed, the host's
 *     settings, the rival's name), sent again until each side answers,
 *   - passes each INPUT on to the other side, as it came, at once,
 *   - tells a side its rival left (LEFT) when the rival leaves, times out
 *     or never took the START,
 *   - (session 66) after a game, which leaves both in its room, starts the
 *     next there when each has asked for a rematch (AGAIN): the same sides
 *     and settings, a new seed, a new room id (so nothing late from the
 *     last game is taken for the next); until then each AGAIN is answered
 *     with the rival's wish.
 * Every player and room is a slot in a table made at start-up (--players,
 * 1024 by default): nothing is allocated after, and an id finds its slot
 * at once. A player sending more than RATE_LIMIT datagrams in a second
 * has the rest of that second's dropped.
 *
 * (Session 58) The nightmare AI (server/hz_netai.c, players the server
 * runs: HELLO says so) only HOSTs. Its room is listed -- one, however many
 * wait -- for anyone to pick, who plays it on their own settings, as P1
 * or P2 by a coin's toss each game ((session 68) --ai-side p1 or p2: always
 * that side);
 * a quick match left waiting --ai-after seconds (15; 0 never) takes one,
 * unless it asked for people only. A quick match never takes an AI's room
 * at once, nor an AI's HOST a quick match: people meet people first. The
 * counts the players see leave the AI out.
 *
 * (Session 59) The view: the host's settings say whether the game is
 * played on the split screen or each side on its own screen; --view split
 * or own rules it for every game here (a server for close play, or one
 * where nobody sees the other's side), --view host (the default) leaves
 * it to the host. WELCOME tells the players the rule.
 *
 * It logs a line per game started and ended, and every minute (unless
 * --quiet) the players, waiting rooms, games and datagrams in and out.
 * Players' addresses are logged only with --verbose. SIGINT or SIGTERM
 * stops it. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L     /* gmtime_r */
#endif

#include "../src/net/net_proto.h"
#include "../src/net/net_sock.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PLAYERS_DEFAULT 1024
#define PLAYERS_MAX     32768   /* an id's slot is its low 16 bits */
#define TIMEOUT_MS      15000   /* silence that drops a player */
#define START_RESEND_MS 200
#define START_GIVE_UP_MS 5000
#define STALE_MS        3000    /* a waiting host this quiet (the lobby
                                   PINGs every second) isn't offered */
#define HOUSE_MS        100     /* the timers' tick */
#define STATS_MS        60000
#define RATE_LIMIT      300     /* datagrams a player may send a second */
#define HELLO_LIMIT     500     /* HELLOs taken a second, from everyone */

typedef struct {
    bool    used;
    u16     gen;            /* bumped as the slot is freed: old ids fail */
    NetAddr addr;
    u32     token;
    u32     nonce;          /* its HELLO's: the same again gets the same
                               WELCOME (the first was lost) */
    u64     game;
    char    name[NET_NAME_LEN + 1];
    u64     heard;
    int     room;           /* a slot, -1 none */
    int     side;
    bool    ready;          /* took its START */
    u16     rtt;            /* as it says (HOST, JOIN) */
    bool    ai;             /* (session 58) the server's AI (HELLO) */
    u64     window;         /* RATE_LIMIT's second, and its count */
    int     window_count;
} Player;

typedef struct {
    bool used;
    bool playing;
    u16  gen;
    int  player[2];         /* slots, -1 none; [0] the host */
    u8   settings[NET_SETTINGS_LEN];
    u64  game;
    u64  opened;
    u64  started;
    u64  start_sent;
    u32  seed;
    bool quick;             /* opened by a quick match: any HOST takes it */
    bool people_only;       /* (session 58) the quick match's JOIN said so */
    bool ai;                /* (session 58) hosted by the AI */
    bool again[2];          /* (session 66) each side's rematch wish */
    int  prev, next;        /* the waiting list, oldest first */
} Room;

typedef struct {
    NetSock sock;
    int     max;
    Player *player;
    Room   *room;
    int    *free_player;    /* stacks of free slots */
    int     free_players;
    int    *free_room;
    int     free_rooms;
    int     wait_head, wait_tail;
    int     online, waiting, playing;
    int     ai_online, ai_waiting;  /* (session 58) of those, the AI's */
    u64     ai_after;               /* ms a quick match waits for people */
    u8      view_rule;              /* (session 59) NET_VIEW_* */
    int     ai_side;                /* (session 68) the person's side
                                       against the AI: -1 a coin's toss */
    u64     now;
    u64     hello_window;
    int     hello_count;
    u64     in, out, in_bytes, out_bytes;   /* since the last stats line */
    u64     games_started, games_ended;
    bool    verbose, quiet;
} Server;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void say(const char *fmt, ...)
{
    char when[32];
    time_t t = time(NULL);
    struct tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    printf("%s ", when);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

/* -- ids -- */

static u32 make_id(u16 gen, int slot)
{
    return ((u32)gen << 16) | (u32)slot;
}

/* The player slot an id and token name, or -1. */
static int find_player(Server *s, u32 id, u32 token)
{
    int slot = (int)(id & 0xFFFF);
    if (slot >= s->max) {
        return -1;
    }
    Player *p = &s->player[slot];
    if (!p->used || p->gen != (u16)(id >> 16) || p->token != token) {
        return -1;
    }
    return slot;
}

/* The room slot an id names, or -1. */
static int find_room(Server *s, u32 id)
{
    int slot = (int)(id & 0xFFFF);
    if (slot >= s->max) {
        return -1;
    }
    Room *r = &s->room[slot];
    if (!r->used || r->gen != (u16)(id >> 16)) {
        return -1;
    }
    return slot;
}

/* -- sending -- */

static void send_to(Server *s, const NetAddr *to, const u8 *p, int n)
{
    if (net_send(s->sock, to, p, n)) {
        s->out++;
        s->out_bytes += (u64)n;
    }
}

static void send_refuse(Server *s, const NetAddr *to, u32 nonce,
                        NetRefuse why)
{
    u8 p[NET_REFUSE_LEN];
    net_header(p, NET_REFUSE);
    net_put32(p + 4, nonce);
    p[8] = (u8)why;
    send_to(s, to, p, sizeof p);
}

static void send_welcome(Server *s, int slot)
{
    Player *pl = &s->player[slot];
    u8 p[NET_WELCOME_LEN + 1];
    net_header(p, NET_WELCOME);
    net_put32(p + 4, pl->nonce);
    net_put32(p + 8, make_id(pl->gen, slot));
    net_put32(p + 12, pl->token);
    net_put16(p + 16, (u16)(s->waiting - s->ai_waiting));
    net_put16(p + 18, (u16)(s->online - s->ai_online));
    p[NET_WELCOME_VIEW] = s->view_rule;
    send_to(s, &pl->addr, p, sizeof p);
}

static void send_room_note(Server *s, int slot, NetType type, int room)
{
    u8 p[8];
    net_header(p, type);
    u32 id = 0;
    if (room >= 0) {
        id = make_id(s->room[room].gen, room);
    }
    net_put32(p + 4, id);
    send_to(s, &s->player[slot].addr, p, sizeof p);
}

static void send_start(Server *s, int room, int side)
{
    Room *r = &s->room[room];
    Player *me = &s->player[r->player[side]];
    Player *rival = &s->player[r->player[1 - side]];
    u8 p[NET_START_LEN + 1];
    net_header(p, NET_START);
    net_put32(p + 4, make_id(r->gen, room));
    p[8] = (u8)side;
    net_put32(p + 9, r->seed);
    memcpy(p + 13, r->settings, NET_SETTINGS_LEN);
    net_name_put(p + 13 + NET_SETTINGS_LEN, rival->name);
    net_put16(p + 13 + NET_SETTINGS_LEN + NET_NAME_LEN, rival->rtt);
    p[NET_START_FLAGS] = rival->ai ? NET_START_RIVAL_AI : 0;
    send_to(s, &me->addr, p, sizeof p);
}

static void send_left(Server *s, int slot, int room, NetLeft why)
{
    u8 p[NET_LEFT_LEN];
    net_header(p, NET_LEFT);
    net_put32(p + 4, make_id(s->room[room].gen, room));
    p[8] = (u8)why;
    send_to(s, &s->player[slot].addr, p, sizeof p);
}

/* -- rooms -- */

static void wait_unlink(Server *s, int room)
{
    Room *r = &s->room[room];
    if (r->prev >= 0) {
        s->room[r->prev].next = r->next;
    } else {
        s->wait_head = r->next;
    }
    if (r->next >= 0) {
        s->room[r->next].prev = r->prev;
    } else {
        s->wait_tail = r->prev;
    }
    r->prev = r->next = -1;
    s->waiting--;
    if (r->ai) {
        s->ai_waiting--;
    }
}

static void room_close(Server *s, int room)
{
    Room *r = &s->room[room];
    if (!r->playing) {
        wait_unlink(s, room);
    } else {
        s->playing--;
    }
    r->used = false;
    r->gen++;
    s->free_room[s->free_rooms++] = room;
}

/* A new waiting room hosted by player `slot`, at the end of the list. */
static int room_open(Server *s, int slot, const u8 *settings)
{
    if (s->free_rooms == 0) {
        return -1;              /* can't be: a room per player at most */
    }
    int room = s->free_room[--s->free_rooms];
    Room *r = &s->room[room];
    u16 gen = r->gen;
    memset(r, 0, sizeof *r);
    r->used = true;
    r->gen = gen;
    r->player[0] = slot;
    r->player[1] = -1;
    memcpy(r->settings, settings, NET_SETTINGS_LEN);
    r->game = s->player[slot].game;
    r->opened = s->now;
    r->prev = s->wait_tail;
    r->next = -1;
    if (s->wait_tail >= 0) {
        s->room[s->wait_tail].next = room;
    } else {
        s->wait_head = room;
    }
    s->wait_tail = room;
    s->waiting++;
    r->ai = s->player[slot].ai;
    if (r->ai) {
        s->ai_waiting++;
    }
    s->player[slot].room = room;
    s->player[slot].side = 0;
    return room;
}

/* Whether a waiting room's host has been heard from lately: one whose
 * game has gone (no BYE) stays until TIMEOUT_MS, but isn't offered. */
static bool room_live(const Server *s, int room)
{
    return s->now - s->player[s->room[room].player[0]].heard <= STALE_MS;
}

/* (Session 58) The oldest live AI room waiting with `game`, or -1. */
static int ai_room(Server *s, u64 game)
{
    for (int room = s->wait_head; room >= 0; room = s->room[room].next) {
        if (s->room[room].ai && s->room[room].game == game
            && room_live(s, room)) {
            return room;
        }
    }
    return -1;
}

/* Player `slot` takes the waiting room: the game starts. */
static void room_match(Server *s, int room, int slot)
{
    Room *r = &s->room[room];
    wait_unlink(s, room);
    r->playing = true;
    r->player[1] = slot;
    r->seed = net_random32();
    if (s->view_rule != NET_VIEW_HOST) {        /* (session 59) */
        r->settings[NET_SETTINGS_VIEW] = (s->view_rule == NET_VIEW_OWN);
    }
    r->started = s->now;
    r->start_sent = s->now;
    s->playing++;
    s->games_started++;
    for (int side = 0; side < 2; side++) {
        Player *p = &s->player[r->player[side]];
        p->room = room;
        p->side = side;
        p->ready = false;
        send_start(s, room, side);
    }
    say("game %u: %s%s against %s%s%s (games %d, waiting %d, online %d)",
        (unsigned)make_id(r->gen, room), s->player[r->player[0]].name,
        s->player[r->player[0]].ai ? " (the server's AI)" : "",
        s->player[r->player[1]].name,
        s->player[r->player[1]].ai ? " (the server's AI)" : "",
        (r->settings[NET_SETTINGS_VIEW] & 1) ? ", own screens" : "",
        s->playing, s->waiting - s->ai_waiting, s->online - s->ai_online);
}

/* (Session 58) A person, `slot`, against the AI waiting in `room`, on
 * their own settings: (session 68) P1 or P2 by a coin's toss (ai_side
 * -1), else the side ai_side. */
static void room_match_ai(Server *s, int room, int slot, const u8 *settings)
{
    Room *r = &s->room[room];
    int ai = r->player[0];
    memcpy(r->settings, settings, NET_SETTINGS_LEN);
    s->player[slot].room = room;
    int side = s->ai_side >= 0 ? s->ai_side : (int)(net_random32() & 1);
    if (side == 1) {
        room_match(s, room, slot);      /* the AI P1 */
    } else {
        r->player[0] = slot;
        room_match(s, room, ai);
    }
}

/* (Session 66) Both asked for a rematch: the room's next game, the same
 * players on the same sides with the same settings, under a new id (the
 * last game's INPUTs, LEAVEs and AGAINs no longer find it) with a new
 * seed. START to both, sent again until each takes it, as any game's. */
static void room_rematch(Server *s, int room)
{
    Room *r = &s->room[room];
    u32 last = make_id(r->gen, room);
    r->gen++;
    if (make_id(r->gen, room) == 0) {
        r->gen++;               /* an id is never 0 (JOIN 0 is quick) */
    }
    r->again[0] = r->again[1] = false;
    r->seed = net_random32();
    r->started = s->now;
    r->start_sent = s->now;
    s->games_ended++;
    s->games_started++;
    for (int side = 0; side < 2; side++) {
        s->player[r->player[side]].ready = false;
        send_start(s, room, side);
    }
    say("game %u: %s against %s again, as game %u", (unsigned)last,
        s->player[r->player[0]].name, s->player[r->player[1]].name,
        (unsigned)make_id(r->gen, room));
}

/* Player `slot` out of its room, if it has one; its rival, if any, told
 * why and back in the lobby. */
static void room_leave(Server *s, int slot, NetLeft why)
{
    Player *p = &s->player[slot];
    int room = p->room;
    if (room < 0) {
        return;
    }
    Room *r = &s->room[room];
    p->room = -1;
    if (r->playing) {
        int other = r->player[1 - p->side];
        if (other >= 0) {
            send_left(s, other, room, why);
            s->player[other].room = -1;
        }
        s->games_ended++;
        say("game %u ended: %s %s (%llu s)", (unsigned)make_id(r->gen, room),
            p->name, why == NET_LEFT_TIMEOUT ? "timed out"
                     : why == NET_LEFT_NOSTART ? "never started" : "left",
            (unsigned long long)((s->now - r->started) / 1000));
    }
    room_close(s, room);
}

/* -- players -- */

static void player_drop(Server *s, int slot, NetLeft why)
{
    room_leave(s, slot, why);
    Player *p = &s->player[slot];
    p->used = false;
    p->gen++;
    if (p->gen == 0) {
        p->gen = 1;             /* an id is never 0 */
    }
    s->free_player[s->free_players++] = slot;
    s->online--;
    if (p->ai) {
        s->ai_online--;
    }
}

static int player_by_addr(Server *s, const NetAddr *a)
{
    for (int i = 0; i < s->max; i++) {
        if (s->player[i].used && net_addr_equal(&s->player[i].addr, a)) {
            return i;
        }
    }
    return -1;
}

static void on_hello(Server *s, const NetAddr *from, const u8 *p, int n)
{
    if (n < NET_HELLO_LEN) {
        return;
    }
    u32 nonce = net_get32(p + 4);
    if (s->now - s->hello_window >= 1000) {
        s->hello_window = s->now;
        s->hello_count = 0;
    }
    if (++s->hello_count > HELLO_LIMIT) {
        send_refuse(s, from, nonce, NET_REFUSE_BUSY);
        return;
    }
    /* From an address already online: the same HELLO again (its WELCOME
     * was lost) gets it again; a new one means that game started over. */
    int slot = player_by_addr(s, from);
    if (slot >= 0) {
        if (s->player[slot].nonce == nonce) {
            send_welcome(s, slot);
            return;
        }
        player_drop(s, slot, NET_LEFT_LEFT);
    }
    if (s->free_players == 0) {
        send_refuse(s, from, nonce, NET_REFUSE_FULL);
        return;
    }
    slot = s->free_player[--s->free_players];
    Player *pl = &s->player[slot];
    u16 gen = pl->gen;
    memset(pl, 0, sizeof *pl);
    pl->used = true;
    pl->gen = gen;
    pl->addr = *from;
    pl->token = net_random32();
    pl->nonce = nonce;
    pl->game = net_get64(p + 8);
    net_name_get(p + 16, pl->name);
    pl->ai = (p[NET_HELLO_FLAGS] & NET_HELLO_AI) != 0;
    if (pl->ai) {
        s->ai_online++;
    }
    pl->heard = s->now;
    pl->room = -1;
    pl->window = s->now;
    s->online++;
    send_welcome(s, slot);
    if (s->verbose) {
        char where[64];
        net_addr_text(from, where, sizeof where);
        say("%s online from %s (online %d)", pl->name, where, s->online);
    }
}

static void on_list(Server *s, int slot, const u8 *p, int n)
{
    if (n < NET_LIST_LEN) {
        return;
    }
    int offset = net_get16(p + NET_PREFIX);
    Player *me = &s->player[slot];
    u8 out[NET_ROOMS_HEAD + NET_ROOMS_PAGE * NET_ROOM_ENTRY];
    int count = 0;
    int total = 0;
    bool ai_listed = false;
    for (int room = s->wait_head; room >= 0; room = s->room[room].next) {
        const Room *r = &s->room[room];
        if (r->player[0] == slot || !room_live(s, room)) {
            continue;           /* not one's own, nor a gone one's */
        }
        if (r->ai && (ai_listed || r->game != me->game)) {
            continue;           /* (session 58) one AI, which can play */
        }
        ai_listed = ai_listed || r->ai;
        if (total++ < offset || count == NET_ROOMS_PAGE) {
            continue;
        }
        u8 *e = out + NET_ROOMS_HEAD + count * NET_ROOM_ENTRY;
        net_put32(e, make_id(r->gen, room));
        net_name_put(e + 4, s->player[r->player[0]].name);
        e[4 + NET_NAME_LEN] = (u8)(((r->game == me->game)
                                    ? NET_ROOM_SAME_GAME : 0)
                                   | (r->ai ? NET_ROOM_AI : 0));
        u64 secs = (s->now - r->opened) / 1000;
        net_put16(e + 5 + NET_NAME_LEN, (u16)(secs > 0xFFFF ? 0xFFFF : secs));
        count++;
    }
    net_header(out, NET_ROOMS);
    net_put16(out + 4, (u16)total);
    net_put16(out + 6, (u16)offset);
    out[8] = (u8)count;
    send_to(s, &me->addr, out, NET_ROOMS_HEAD + count * NET_ROOM_ENTRY);
}

/* The oldest live room waiting for a quick match with `slot`'s game, or
 * -1. */
static int quick_room(Server *s, int slot)
{
    for (int room = s->wait_head; room >= 0; room = s->room[room].next) {
        const Room *r = &s->room[room];
        if (r->quick && r->game == s->player[slot].game
            && r->player[0] != slot && room_live(s, room)) {
            return room;
        }
    }
    return -1;
}

static void on_host(Server *s, int slot, const u8 *settings, u16 rtt)
{
    room_leave(s, slot, NET_LEFT_LEFT);
    s->player[slot].rtt = rtt;
    /* A quick match waiting plays anyone: this host, on its settings,
     * the host P1 as ever. */
    int quick = s->player[slot].ai ? -1 : quick_room(s, slot);
    if (quick >= 0) {
        Room *r = &s->room[quick];
        int waiter = r->player[0];
        memcpy(r->settings, settings, NET_SETTINGS_LEN);
        r->player[0] = slot;
        s->player[slot].room = quick;
        room_match(s, quick, waiter);
        return;
    }
    int room = room_open(s, slot, settings);
    if (room >= 0) {
        send_room_note(s, slot, NET_WAITING, room);
    }
}

/* A HOST or JOIN from a player whose game has started but who hasn't
 * taken its START: a request sent again before the START came, late.
 * The START again, and nothing else (leaving would end the game). */
static bool late_ask(Server *s, int slot)
{
    Player *me = &s->player[slot];
    if (me->room < 0 || !s->room[me->room].playing || me->ready) {
        return false;
    }
    send_start(s, me->room, me->side);
    return true;
}

static void on_join(Server *s, int slot, const u8 *p, int n)
{
    if (n < NET_JOIN_MIN || late_ask(s, slot)) {
        return;
    }
    u32 want = net_get32(p + NET_PREFIX);
    const u8 *settings = p + NET_PREFIX + 4;
    u16 rtt = net_get16(p + NET_PREFIX + 4 + NET_SETTINGS_LEN);
    u8 flags = (n >= NET_JOIN_LEN) ? p[NET_JOIN_MIN] : 0;
    Player *me = &s->player[slot];
    /* Already in the room asked for (the JOIN again: its answer lost). */
    if (me->room >= 0 && want != 0
        && make_id(s->room[me->room].gen, me->room) == want) {
        return;
    }
    if (want == 0) {
        /* Quick: the oldest room waiting with the same game, else wait in
         * a new one. A JOIN 0 again while waiting keeps the room. */
        if (me->room >= 0 && !s->room[me->room].playing) {
            /* (Session 58) its people-only as this JOIN says */
            if (s->room[me->room].quick) {
                s->room[me->room].people_only = (flags & NET_JOIN_PEOPLE) != 0;
            }
            send_room_note(s, slot, NET_WAITING, me->room);
            return;
        }
        room_leave(s, slot, NET_LEFT_LEFT);
        me->rtt = rtt;
        for (int room = s->wait_head; room >= 0;
             room = s->room[room].next) {
            if (s->room[room].game == me->game && !s->room[room].ai
                && s->room[room].player[0] != slot && room_live(s, room)) {
                room_match(s, room, slot);
                return;
            }
        }
        int room = room_open(s, slot, settings);
        if (room >= 0) {
            s->room[room].quick = true;
            s->room[room].people_only = (flags & NET_JOIN_PEOPLE) != 0;
            send_room_note(s, slot, NET_WAITING, room);
        }
        return;
    }
    int room = find_room(s, want);
    if (room < 0 || s->room[room].playing || s->room[room].game != me->game
        || s->room[room].player[0] == slot) {
        send_room_note(s, slot, NET_NOROOM, -1);
        return;
    }
    room_leave(s, slot, NET_LEFT_LEFT);
    me->rtt = rtt;
    if (s->room[room].ai) {
        room_match_ai(s, room, slot, settings);
    } else {
        room_match(s, room, slot);
    }
}

/* The room `slot` is playing in, if the datagram names it; else -1. */
static int playing_room(Server *s, int slot, u32 room_id)
{
    int room = s->player[slot].room;
    if (room < 0 || !s->room[room].playing
        || make_id(s->room[room].gen, room) != room_id) {
        return -1;
    }
    return room;
}

static void on_input(Server *s, int slot, const u8 *p, int n)
{
    if (n < NET_INPUT_C) {
        return;
    }
    int count = p[NET_INPUT_C - 1];
    if (count > NET_INPUT_MAX || n < NET_INPUT_C + count) {
        return;
    }
    int room = playing_room(s, slot, net_get32(p + NET_PREFIX));
    if (room < 0) {
        return;
    }
    Player *me = &s->player[slot];
    me->ready = true;
    int other = s->room[room].player[1 - me->side];
    u8 out[NET_INPUT_S + NET_INPUT_MAX];
    net_header(out, NET_INPUT);
    memcpy(out + NET_HEADER, p + NET_PREFIX + 4,
           (size_t)(NET_INPUT_BODY + count));
    send_to(s, &s->player[other].addr, out, NET_INPUT_S + count);
}

/* (Session 66) AGAIN from a player in a game's room: its wish kept;
 * both wishing, the rematch; else the rival's wish back, no bigger than
 * what came. */
static void on_again(Server *s, int slot, const u8 *p, int n)
{
    if (n < NET_AGAIN_C) {
        return;
    }
    u32 id = net_get32(p + NET_PREFIX);
    int room = playing_room(s, slot, id);
    if (room < 0) {
        return;
    }
    Room *r = &s->room[room];
    Player *me = &s->player[slot];
    r->again[me->side] = (p[NET_PREFIX + 4] & 1) != 0;
    if (r->again[0] && r->again[1]) {
        room_rematch(s, room);
        return;
    }
    u8 out[NET_AGAIN_S];
    net_header(out, NET_AGAIN);
    net_put32(out + 4, id);
    out[8] = r->again[1 - me->side] ? 1 : 0;
    send_to(s, &me->addr, out, sizeof out);
}

static void on_datagram(Server *s, const NetAddr *from, const u8 *p, int n)
{
    s->in++;
    s->in_bytes += (u64)n;
    /* A HELLO of another version is told so; anything else not ours is
     * dropped unanswered. */
    if (n >= NET_HELLO_LEN && p[0] == 'H' && p[1] == 'Z'
        && p[3] == NET_HELLO && p[2] != NET_PROTO_VERSION) {
        send_refuse(s, from, net_get32(p + 4), NET_REFUSE_VERSION);
        return;
    }
    int type = net_type(p, n);
    if (type == 0) {
        return;
    }
    if (type == NET_HELLO) {
        on_hello(s, from, p, n);
        return;
    }
    if (n < NET_PREFIX) {
        return;
    }
    int slot = find_player(s, net_get32(p + 4), net_get32(p + 8));
    if (slot < 0) {
        /* No bigger than what came (NET_REFUSE_LEN < NET_PREFIX). */
        send_refuse(s, from, 0, NET_REFUSE_UNKNOWN);
        return;
    }
    Player *me = &s->player[slot];
    if (s->now - me->window >= 1000) {
        me->window = s->now;
        me->window_count = 0;
    }
    if (++me->window_count > RATE_LIMIT) {
        return;
    }
    me->heard = s->now;
    if (!net_addr_equal(&me->addr, from)) {
        me->addr = *from;       /* its NAT gave it a new port: id and
                                   token say it's the same player */
    }
    switch (type) {
    case NET_PING:
        if (n >= NET_PING_LEN) {
            u8 out[NET_PONG_LEN];
            net_header(out, NET_PONG);
            memcpy(out + 4, p + NET_PREFIX, 4);
            net_put16(out + 8, (u16)(s->waiting - s->ai_waiting));
            net_put16(out + 10, (u16)(s->online - s->ai_online));
            send_to(s, &me->addr, out, sizeof out);
        }
        break;
    case NET_LIST:
        on_list(s, slot, p, n);
        break;
    case NET_HOST:
        if (n >= NET_HOST_LEN && !late_ask(s, slot)) {
            if (me->room >= 0 && !s->room[me->room].playing) {
                send_room_note(s, slot, NET_WAITING, me->room);
            } else {
                on_host(s, slot, p + NET_PREFIX,
                        net_get16(p + NET_PREFIX + NET_SETTINGS_LEN));
            }
        }
        break;
    case NET_JOIN:
        on_join(s, slot, p, n);
        break;
    case NET_READY:
        if (n >= NET_READY_LEN
            && playing_room(s, slot, net_get32(p + NET_PREFIX)) >= 0) {
            me->ready = true;
        }
        break;
    case NET_INPUT:
        on_input(s, slot, p, n);
        break;
    case NET_LEAVE:
        if (n >= NET_LEAVE_LEN && me->room >= 0
            && make_id(s->room[me->room].gen, me->room)
               == net_get32(p + NET_PREFIX)) {
            room_leave(s, slot, NET_LEFT_LEFT);
        }
        break;
    case NET_BYE:
        player_drop(s, slot, NET_LEFT_LEFT);
        break;
    case NET_AGAIN:
        on_again(s, slot, p, n);
        break;
    default:
        break;
    }
}

/* (Session 58) Quick matches left waiting ai_after for people, and not
 * people only, each against an AI that waits with the same game. */
static void ai_fallback(Server *s)
{
    if (s->ai_after == 0 || s->ai_waiting == 0) {
        return;
    }
    int room = s->wait_head;
    while (room >= 0) {
        Room *r = &s->room[room];
        int next = r->next;
        if (r->quick && !r->people_only && !r->ai
            && s->now - r->opened >= s->ai_after && room_live(s, room)) {
            int ai = ai_room(s, r->game);
            if (ai >= 0) {
                int person = r->player[0];
                u8 settings[NET_SETTINGS_LEN];
                memcpy(settings, r->settings, sizeof settings);
                room_leave(s, person, NET_LEFT_LEFT);   /* its own, closed */
                room_match_ai(s, ai, person, settings);
                next = s->wait_head;                     /* the list moved */
            }
        }
        room = next;
    }
}

/* The timers: the silent dropped, STARTs sent again or given up, quick
 * matches given the AI. */
static void housekeeping(Server *s)
{
    ai_fallback(s);
    for (int slot = 0; slot < s->max; slot++) {
        Player *p = &s->player[slot];
        if (p->used && s->now - p->heard > TIMEOUT_MS) {
            if (s->verbose) {
                say("%s timed out", p->name);
            }
            player_drop(s, slot, NET_LEFT_TIMEOUT);
        }
    }
    for (int room = 0; room < s->max; room++) {
        Room *r = &s->room[room];
        if (!r->used || !r->playing) {
            continue;
        }
        bool both = s->player[r->player[0]].ready
                    && s->player[r->player[1]].ready;
        if (both) {
            continue;
        }
        if (s->now - r->started > START_GIVE_UP_MS) {
            int late = s->player[r->player[0]].ready ? r->player[1]
                                                     : r->player[0];
            room_leave(s, late, NET_LEFT_NOSTART);
            continue;
        }
        if (s->now - r->start_sent >= START_RESEND_MS) {
            r->start_sent = s->now;
            for (int side = 0; side < 2; side++) {
                if (!s->player[r->player[side]].ready) {
                    send_start(s, room, side);
                }
            }
        }
    }
}

static void stats(Server *s, u64 span_ms)
{
    double secs = (double)span_ms / 1000.0;
    say("online %d (AI %d), waiting %d (AI %d), games %d; in %.0f/s "
        "(%.1f kB/s), out %.0f/s (%.1f kB/s); games started %llu, ended %llu",
        s->online, s->ai_online, s->waiting, s->ai_waiting, s->playing, (double)s->in / secs,
        (double)s->in_bytes / secs / 1000.0, (double)s->out / secs,
        (double)s->out_bytes / secs / 1000.0,
        (unsigned long long)s->games_started,
        (unsigned long long)s->games_ended);
    s->in = s->out = s->in_bytes = s->out_bytes = 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: hz_server [--port N] [--players N] [--ai-after S] "
                    "[--ai-side random|p1|p2] [--view host|split|own] "
                    "[--verbose] [--quiet]\n"
                    "  port: UDP, %d by default\n"
                    "  players: online at once, %d by default (at most %d)\n"
                    "  ai-after: seconds a quick match waits for people "
                    "before the AI\n"
                    "            (server/hz_netai.c, when one runs): 15; 0 "
                    "never\n"
                    "  ai-side: the person's side against the AI: a coin's "
                    "toss each game\n"
                    "           (random, the default), or always p1 or p2\n"
                    "  view: every game on the split screen, or each side "
                    "on its own;\n"
                    "        host (the default): the host's choice\n"
                    "  verbose: log players' addresses and comings and goings\n"
                    "  quiet: no line a minute\n",
            NET_PORT_DEFAULT, PLAYERS_DEFAULT, PLAYERS_MAX);
    return 2;
}

int main(int argc, char **argv)
{
    static Server server;
    Server *s = &server;
    int port = NET_PORT_DEFAULT;
    s->max = PLAYERS_DEFAULT;
    int ai_after = 15;
    s->ai_side = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            s->verbose = true;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            s->quiet = true;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
            s->max = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ai-after") == 0 && i + 1 < argc) {
            ai_after = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ai-side") == 0 && i + 1 < argc) {
            const char *v = argv[++i];          /* (session 68) */
            if (strcmp(v, "random") == 0) {
                s->ai_side = -1;
            } else if (strcmp(v, "p1") == 0) {
                s->ai_side = 0;
            } else if (strcmp(v, "p2") == 0) {
                s->ai_side = 1;
            } else {
                return usage();
            }
        } else if (strcmp(argv[i], "--view") == 0 && i + 1 < argc) {
            const char *v = argv[++i];          /* (session 59) */
            if (strcmp(v, "host") == 0) {
                s->view_rule = NET_VIEW_HOST;
            } else if (strcmp(v, "split") == 0) {
                s->view_rule = NET_VIEW_SPLIT;
            } else if (strcmp(v, "own") == 0) {
                s->view_rule = NET_VIEW_OWN;
            } else {
                return usage();
            }
        } else {
            return usage();
        }
    }
    if (port <= 0 || port > 65535 || s->max < 2 || s->max > PLAYERS_MAX
        || ai_after < 0) {
        return usage();
    }
    s->ai_after = (u64)ai_after * 1000;
    s->player = calloc((size_t)s->max, sizeof *s->player);
    s->room = calloc((size_t)s->max, sizeof *s->room);
    s->free_player = calloc((size_t)s->max, sizeof *s->free_player);
    s->free_room = calloc((size_t)s->max, sizeof *s->free_room);
    if (s->player == NULL || s->room == NULL || s->free_player == NULL
        || s->free_room == NULL) {
        fprintf(stderr, "hz_server: no memory for %d players\n", s->max);
        return 1;
    }
    for (int i = 0; i < s->max; i++) {
        s->player[i].gen = 1;
        s->room[i].gen = 1;
        s->free_player[i] = s->max - 1 - i;     /* slot 0 first */
        s->free_room[i] = s->max - 1 - i;
    }
    s->free_players = s->free_rooms = s->max;
    s->wait_head = s->wait_tail = -1;

    s->sock = net_udp_open(port, NULL);
    if (s->sock == NET_NO_SOCK) {
        fprintf(stderr, "hz_server: can't open UDP port %d\n", port);
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    static const char *const VIEW_RULE[3] = {
        "the host's", "the split screen", "own screens"
    };
    say("hz_server: UDP port %d, up to %d players, protocol %d, view %s",
        port, s->max, NET_PROTO_VERSION, VIEW_RULE[s->view_rule]);

    u8 buf[NET_PACKET_MAX + 1];
    s->now = net_now_ms();
    u64 next_house = s->now + HOUSE_MS;
    u64 stats_from = s->now;
    while (!g_stop) {
        net_wait(s->sock, HOUSE_MS);
        s->now = net_now_ms();
        /* A batch, then the timers get their turn. */
        for (int k = 0; k < 4096; k++) {
            NetAddr from;
            int n = net_recv(s->sock, &from, buf, sizeof buf);
            if (n < 0) {
                break;
            }
            if (n <= NET_PACKET_MAX) {
                on_datagram(s, &from, buf, n);
            }
        }
        s->now = net_now_ms();
        if (s->now >= next_house) {
            housekeeping(s);
            next_house = s->now + HOUSE_MS;
        }
        if (!s->quiet && s->now - stats_from >= STATS_MS) {
            stats(s, s->now - stats_from);
            stats_from = s->now;
        }
    }
    say("hz_server: stopped (online %d, games %d)", s->online, s->playing);
    net_close(s->sock);
    return 0;
}
