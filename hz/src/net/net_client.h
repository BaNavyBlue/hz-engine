#ifndef HZ_NET_CLIENT_H
#define HZ_NET_CLIENT_H

/* (Session 56) The game's side of a network game (docs/netplay.md): the
 * server's lobby and the lockstep. Nothing here blocks but
 * net_client_connect's name lookup and net_game_finish's short linger;
 * the host calls net_client_poll every frame (and while it waits).
 *
 * The lobby: net_client_connect sends HELLO until WELCOME (LOBBY), then a
 * PING a second keeps it online and measures the round trip. HOST waits
 * in a room of one's own (WAITING) until someone picks it from the list;
 * QUICK takes the oldest room waiting with the same game, or waits in a
 * new one (and after a while the server's nightmare AI, if it runs one,
 * unless people only, session 58); JOIN takes a room from the list (net_client_want_list keeps
 * the list fresh while it's shown). START (PLAYING): the game begins.
 * (Session 66) A game that ends (net_game_after) leaves both players in
 * its room (AFTER): each may ask for a rematch (net_client_rematch, AGAIN
 * every half second, which the server answers with the rival's wish),
 * and when both have asked the server STARTs the next game there, on the
 * same sides and settings and a new seed. Anything else asked for, or
 * the room left (net_client_cancel), and the rival hears LEFT.
 *
 * The lockstep: both machines run every frame of the game from the same
 * state, with the same two pads. The pad read on a frame here is the one
 * for `delay` frames on (the first `delay` frames have none), sent at once
 * and again with each datagram until the rival says it has it; a frame is
 * run only when the rival's pad for it is in. A hash of the game's state
 * every NET_HASH_EVERY frames goes along too: the first that differs is a
 * split (desync), and the game stops there.
 *
 * HZ_NET_LOSS (a fraction, 0-0.9) drops that much of what is sent and
 * received, at random: to see the lockstep ride out a bad line.
 * (Session 63) HZ_NET_LAG holds each datagram sent that many ms, and
 * HZ_NET_JITTER up to that many more at random: a long line on one
 * machine, for the pace's checks. */

#include "net_proto.h"
#include "net_sock.h"

/* Bumped whenever the game's code changes what a frame does: two builds
 * with different ones never meet (it is folded into `game`). 2 (session
 * 59): the view in the settings, the own view's game. 3 (session 60): the
 * TURRETS patch, the fixes' bit 6. 4 (session 61): the UNITS patch, the
 * fixes' bit 7. 5 (session 62): CPU HITS without its slip (a CPU side's
 * unit hitting P1's mech halved team 2's slot 49). 6 (session 66): both
 * HQs down in the same second a draw (HZ_FIX_DRAW), and each side's own
 * end (the state hash without the display from the game's end on). 7
 * (session 68): with UNITS a side's units block each other. 8 (session
 * 68): with UNITS a unit sees an enemy in the cell of its own centre. 9
 * (session 71): the PATHS patch, the fixes' bit 9. 10 (session 72): the
 * REPAIR patch, the fixes' bit 10. 11 (session 75): the PIXELS patch, the
 * fixes' bit 11. 12 (session 78): no frame changed, but the assets' hash
 * did -- a PNG hashed by its picture, not its bytes (hz_file.h) -- so a
 * build of 11 and one of 12 would never agree on `game` anyway. 13
 * (session 79): no frame changed, but the display a split-screen game
 * hashes did: the draw layer (docs/hackable.md, step 1) loads tiles and
 * clears the screen without DMA, so the VDP's DMA registers (19-23) no
 * longer hold what those uploads left, and the layer's own store (half B)
 * changes it again. 14 (session 81): every sprite drawn (step 1's rest: no
 * cap, no flicker, the divider the layer's), so the frames and the
 * sprites a split-screen game hashes differ. */
#define NET_GAME_CODE 14

#define NET_RING       256      /* frames of pads kept a side */
#define NET_HASH_EVERY 60       /* a state hash a second */
#define NET_HASHES     8        /* own hashes kept to compare */
#define NET_DELAY_MAX  15
/* (Session 64) Against the server's AI (net_game_push): the person's own
 * delay, and the most the AI takes (it thinks ahead: nightmare.h's
 * nightmare_think_ahead). */
#define NET_AI_PERSON_DELAY 2
#define NET_AI_DELAY_MAX    30
#define NET_ROOM_ROWS  NET_ROOMS_PAGE

typedef enum {
    NETC_OFFLINE = 0,
    NETC_CONNECTING,            /* HELLO sent, no WELCOME yet */
    NETC_LOBBY,                 /* online */
    NETC_ASKING,                /* HOST or JOIN sent, no answer yet */
    NETC_WAITING,               /* in a room, for a rival */
    NETC_PLAYING,               /* START: the game is on */
    NETC_FAILED,                /* offline, `error` says why */
    NETC_AFTER                  /* (session 66) a game over, still in its
                                   room: a rematch may be asked for */
} NetClientState;

/* Why a network game stopped (net_game_stopped). */
typedef enum {
    NETG_ON = 0,
    NETG_RIVAL_LEFT,
    NETG_RIVAL_LOST,            /* the server or the line lost the rival */
    NETG_SILENT,                /* nothing from the rival for too long */
    NETG_DESYNC,                /* the two games split */
    NETG_SERVER_LOST
} NetGameStop;

typedef struct {
    u32  id;
    char name[NET_NAME_LEN + 1];
    bool same_game;
    bool ai;                    /* (session 58) the server's nightmare AI */
    u16  secs;                  /* waiting this long */
} NetRoomInfo;

typedef struct {
    NetClientState state;
    char    error[32];          /* why FAILED or a request came to nothing,
                                   in the font's characters */
    NetSock sock;
    NetAddr server;
    char    name[NET_NAME_LEN + 1];
    u64     game;
    u32     nonce, id, token;
    u64     began, sent, heard, pinged, listed;
    u16     rtt;                /* to the server, ms (100 until measured) */
    bool    rtt_known;
    u16     players, waiting;   /* the server's counts */
    u8      view_rule;          /* (session 59) the server's NET_VIEW_* */
    bool    list_wanted;
    NetRoomInfo rooms[NET_ROOM_ROWS];
    int     room_count;
    int     rooms_total;

    /* A HOST or JOIN going until it's answered. */
    NetType ask;
    u32     ask_room;
    u8      ask_settings[NET_SETTINGS_LEN];
    u8      ask_flags;          /* JOIN's: NET_JOIN_PEOPLE */
    u16     ask_rtt;            /* (session 63) the rtt it says, the same
                                   each time: the rival's START has it */
    u64     asked;
    bool    ai;                 /* (session 58) we are the server's AI
                                   (HELLO says so); kept by connect */

    /* The game: START's. */
    u32     room;
    bool    start_new;          /* START came; net_client_take_start */
    int     side;               /* 0 P1 (the host), 1 P2 */
    u32     seed;
    NetSettings settings;
    char    rival[NET_NAME_LEN + 1];
    u16     rival_rtt;
    bool    rival_ai;           /* (session 58) the server's AI */
    /* (Session 66) After a game (AFTER): whether we want a rematch, the
     * rival's wish as the server last said, whether it has said at all
     * (a server from before session 66 never does), since when. */
    bool    again;
    bool    rival_again;
    bool    again_heard;
    u64     again_sent, after_began;

    /* The lockstep. */
    int     delay;
    u8      local[NET_RING];
    u32     local_next;         /* our pads are set for frames below */
    u8      remote[NET_RING];
    u32     remote_next;        /* the rival's are in for frames below */
    u32     acked;              /* the rival has ours below this */
    u32     frame;              /* the next frame the game runs */
    u32     hash_frame[NET_HASHES];
    u64     hash_value[NET_HASHES];
    u32     rival_hframe;       /* the rival's latest hash, 0 none (frame
                                   0's is never sent) */
    u64     rival_hash;
    u32     sent_hframe;        /* ours that goes out */
    u64     sent_hash;
    NetGameStop stop;
    u32     desync_frame;
    u64     input_sent, input_heard;
    u32     stalls;             /* frames that waited for the rival */
    /* (Session 63) The balance (docs/netplay.md, "The pace"): each side's
     * margin, the rival's pads in hand past the frame about to run --
     * ours at each push, the rival's from its INPUTs (its newest pad less
     * the delay is the frame it pushed at, its ack what it had of ours) --
     * smoothed. The side with less is ahead of the other and holds back
     * (net_game_slow), so a faster clock or a pause doesn't leave one
     * side running on what just came. */
    double  margin, rival_margin;           /* frames */
    bool    margin_read, rival_margin_read;
    u32     rival_newest;       /* the rival's pads seen below this */
    bool    balancing;
    bool    balance_off;        /* HZ_NET_BALANCE=0: read, never held */
    double  slowed;             /* frames held back, this game */
    double  lead_max;           /* the most ahead, this game */
    /* (Session 64) The split of the delay against the server's AI
     * (net_game_push): the rival's delay as its INPUTs show it -- the
     * newest pad in one sent from the frame of its latest hash, less that
     * frame, the least seen since the hash before -- and the sum the two
     * delays must come to, the round trip in frames and 4 (what the
     * person's whole share and the AI's one frame came to before). */
    int     rival_delay;        /* -1 until read */
    u32     rd_hframe;          /* the latest hash it was read from */
    int     rd_now, rd_before;  /* the least since it, and the one before */
    int     ai_sum;

    double  loss;               /* HZ_NET_LOSS */
    u64     random;
} NetClient;

void net_client_init(NetClient *c);

/* `game`: what the rival must have alike (net_game_id). False (FAILED,
 * the error set) when the host can't be found or there's no network. */
bool net_client_connect(NetClient *c, const char *host, int port,
                        const char *name, u64 game);
void net_client_disconnect(NetClient *c);

/* Everything that came, and the timers: call every frame. */
void net_client_poll(NetClient *c);

void net_client_host(NetClient *c, const NetSettings *s);
/* people_only (session 58): never the server's AI, however long. */
void net_client_quick(NetClient *c, const NetSettings *s, bool people_only);
void net_client_join(NetClient *c, u32 room, const NetSettings *s);
/* Out of the waiting room (or the request; session 66: or the room
 * after a game): back to LOBBY. */
void net_client_cancel(NetClient *c);
void net_client_want_list(NetClient *c, bool on);

/* True once when a START has come: the game's to start. */
bool net_client_take_start(NetClient *c);

/* The text for the state, for the menu: "OFFLINE", "CONNECTING",
 * "ONLINE", "WAITING FOR A RIVAL", ... */
const char *net_client_state_text(const NetClient *c);

/* The id of what the game read (assets and mods, hz_file_read_hash) with
 * NET_GAME_CODE. */
u64 net_game_id(u64 assets_hash);

/* -- the lockstep -- */

/* The game begins: frame 0, our first `delay` pads none (delay 0: from the
 * round trips, START's and ours: half the two players' round trip each,
 * and against the server's AI, which takes one frame, all of it).
 * (Session 63) Ours as our HOST or JOIN said it, as the rival has it from
 * its START, so both sides come to the same delay (the balance reads the
 * rival's frame from its pads by ours). */
void net_game_begin(NetClient *c, int delay);

/* Our pad (HZ_PAD_* bits) for the next frame we haven't set, sent.
 * (Session 64) Against the server's AI the delay is moved here, by
 * setting more frames' pads than one (this pad) or none: the server's AI
 * (NetClient.ai) takes what the person's delay leaves of the sum; the
 * person (rival_ai) starts at NET_AI_PERSON_DELAY and takes more only
 * if the AI leaves too little (an AI from before session 64 keeps one
 * frame). Both read the other's delay from its INPUTs; no datagram
 * changed. */
void net_game_push(NetClient *c, u8 pad);

/* Whether the rival's pad for c->frame is in. */
bool net_game_ready(const NetClient *c);

/* Both pads for c->frame (P1's, P2's), and on to the next. */
void net_game_take(NetClient *c, u8 pads[2]);

/* What we haven't heard the rival has, sent again (each frame, and while
 * waiting a few times a frame's time). */
void net_game_send(NetClient *c);

/* Our hash of the state at `frame` (a multiple of NET_HASH_EVERY), kept to
 * compare and sent. */
void net_game_hash(NetClient *c, u32 frame, u64 hash);
/* (Session 63) How far ahead of the rival we run, in frames: half the
 * rival's margin less ours (0 until both are read). */
double net_game_lead(const NetClient *c);
/* (Session 63) What to hold back after this frame, in frames (a part of
 * one, for the platform's pace_slow): once ahead by more than three
 * quarters of a frame, until less than a quarter, a hundredth of a frame
 * for each frame ahead, at most a twentieth. None against the server's
 * AI, which runs as the person's pads come. */
double net_game_slow(NetClient *c);

/* NETG_ON while the game can go on. */
NetGameStop net_game_stopped(const NetClient *c);
const char *net_game_stop_text(NetGameStop why);

/* The game is over here: once the rival has our last pads (or a second
 * or two), out of the room. Back to LOBBY. */
void net_game_finish(NetClient *c);
/* (Session 66) As net_game_finish, but a game that ended as games do (no
 * stop: the rival still there) stays in its room: AFTER, nothing asked
 * yet. True then. */
bool net_game_after(NetClient *c);
/* (Session 66) After a game: a rematch wanted, or no longer. */
void net_client_rematch(NetClient *c, bool want);

#endif /* HZ_NET_CLIENT_H */
