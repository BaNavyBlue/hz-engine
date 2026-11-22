#ifndef HZ_NET_PROTO_H
#define HZ_NET_PROTO_H

/* (Session 56) The network game's datagrams, between the game
 * (net_client.h) and the server (server/hz_server.c). docs/netplay.md has
 * the whole design; in short:
 *
 * Both machines run the whole game; only the pads travel, a byte a side a
 * frame (lockstep). The server never runs the game: it keeps the players
 * online, the list of those waiting for a rival, pairs them, and passes
 * each game's pads from one side to the other.
 *
 * Every datagram starts 'H' 'Z' NET_PROTO_VERSION type; numbers are big-
 * endian, as the 68k's. After HELLO, everything the game sends carries its
 * id and token (NET_PREFIX bytes in all), which the server checks: no
 * reply bigger than what came is sent before that, so a forged sender
 * can't use the server to flood someone.
 *
 *   game -> server                       server -> game
 *   HELLO  nonce game name               WELCOME nonce id token waiting players
 *                                        REFUSE  nonce reason
 *   PING   time                          PONG    time waiting players
 *   LIST   offset                        ROOMS   total offset count, entries
 *   HOST   settings rtt                  WAITING room
 *   JOIN   room settings rtt (room 0:    START   room side seed settings rival
 *          the oldest waiting, or host)          rival_rtt
 *                                        NOROOM  room
 *   READY  room (START came)
 *   INPUT  room ack hframe hash first    INPUT   ack hframe hash first count
 *          count pads                            pads (the rival's, passed on)
 *   LEAVE  room                          LEFT    room reason
 *   BYE    (going offline)
 *
 * `game` is what both sides must have alike to play together: the code's
 * NET_GAME_CODE and every byte of the assets and mods the game read
 * (net_client.h). A room is offered only to players with the same.
 *
 * (Session 58) The server's own players: server/hz_netai.c, the nightmare
 * AI, says so in HELLO (NET_HELLO_AI). It only ever HOSTs; its room is
 * listed (one, NET_ROOM_AI) for anyone to pick, and a quick match that
 * finds no one takes it after the server's --ai-after seconds, unless the
 * JOIN said people only (NET_JOIN_PEOPLE). Against it the player is P1 on
 * the player's own settings. Older games send none of the flags (zeros:
 * a person, the AI allowed) and ignore NET_ROOM_AI.
 *
 * (Session 59) The view: the host's settings say whether the game is
 * played on the split screen or each side on its own (NetSettings.view);
 * a server may rule it for every game it starts (hz_server --view), and
 * says so in WELCOME (NET_WELCOME_VIEW; older servers send none: the
 * host's choice). Older games ignore the byte. */

#include "../core/hz_types.h"

#include <stddef.h>

#define NET_PROTO_VERSION 1
#define NET_PORT_DEFAULT  19890

#define NET_PACKET_MAX    512   /* nothing bigger is sent or taken */
#define NET_NAME_LEN      12    /* a player's name: A-Z, 0-9, space, - */
#define NET_SETTINGS_LEN  8     /* the host's game: see NetSettings */
#define NET_ROOMS_PAGE    16    /* entries in one ROOMS */
#define NET_INPUT_MAX     64    /* pads in one INPUT */

typedef enum {
    NET_HELLO = 1,
    NET_WELCOME,
    NET_REFUSE,
    NET_PING,
    NET_PONG,
    NET_LIST,
    NET_ROOMS,
    NET_HOST,
    NET_JOIN,
    NET_WAITING,
    NET_START,
    NET_NOROOM,
    NET_READY,
    NET_INPUT,
    NET_LEAVE,
    NET_LEFT,
    NET_BYE,
    NET_AGAIN,                  /* (session 66) the rematch */
    NET_TYPES
} NetType;

/* REFUSE's reasons. */
typedef enum {
    NET_REFUSE_FULL = 1,        /* the server has all the players it takes */
    NET_REFUSE_VERSION,         /* another NET_PROTO_VERSION */
    NET_REFUSE_UNKNOWN,         /* an id the server doesn't know (it was
                                   restarted, or dropped this player) */
    NET_REFUSE_BUSY             /* too many arriving at once: try again */
} NetRefuse;

/* LEFT's reasons. */
typedef enum {
    NET_LEFT_LEFT = 1,          /* the rival left the game */
    NET_LEFT_TIMEOUT,           /* the server heard nothing from the rival */
    NET_LEFT_NOSTART            /* the rival never took the START */
} NetLeft;

/* INPUT's hframe: its top bit says the sender saw the games split (the
 * hashes differ); the rest is the frame of the hash sent. */
#define NET_HFRAME_SPLIT 0x80000000u

/* ROOMS entry's flags. */
#define NET_ROOM_SAME_GAME 0x01 /* the same `game` as the asker's */
#define NET_ROOM_AI        0x02 /* (session 58) the server's nightmare AI */

/* (Session 58) HELLO's flags byte, after the name. */
#define NET_HELLO_FLAGS    28
#define NET_HELLO_AI       0x01 /* a player the server runs: the AI */

/* (Session 58) JOIN's flags byte, after the rtt (a JOIN without it is
 * NET_JOIN_MIN long: flags 0). */
#define NET_JOIN_PEOPLE    0x01 /* a quick match with people only */

/* (Session 59) WELCOME's view byte, after the players: the server's rule
 * for the view (a WELCOME without it is NET_WELCOME_LEN long: the host's
 * choice). START's settings carry the view as played. */
#define NET_WELCOME_VIEW   NET_WELCOME_LEN
#define NET_VIEW_HOST      0    /* the host's choice */
#define NET_VIEW_SPLIT     1    /* every game on the split screen */
#define NET_VIEW_OWN       2    /* every game each side on its own */

/* (Session 58) START's flags byte, after its rival_rtt (a START without
 * it is NET_START_LEN long: flags 0). Against the server's AI, which plays
 * with a frame's delay, the person takes the rest of the round trip as
 * theirs: the lockstep needs only the two delays' sum to cover it. */
#define NET_START_FLAGS    NET_START_LEN
#define NET_START_RIVAL_AI 0x01 /* the rival is the server's AI */

/* The sizes: the header, the header with id and token, and each message
 * (INPUT's without its pads). */
#define NET_HEADER     4
#define NET_PREFIX     12
#define NET_HELLO_LEN  64       /* padded: no reply to it is longer */
#define NET_WELCOME_LEN 20
#define NET_REFUSE_LEN 9
#define NET_PING_LEN   16
#define NET_PONG_LEN   12
#define NET_LIST_LEN   14
#define NET_ROOMS_HEAD 9
#define NET_ROOM_ENTRY (4 + NET_NAME_LEN + 1 + 2)
#define NET_HOST_LEN   (NET_PREFIX + NET_SETTINGS_LEN + 2)
#define NET_JOIN_MIN   (NET_PREFIX + 4 + NET_SETTINGS_LEN + 2)
#define NET_JOIN_LEN   (NET_JOIN_MIN + 1)
#define NET_WAITING_LEN 8
#define NET_START_LEN  (NET_HEADER + 4 + 1 + 4 + NET_SETTINGS_LEN \
                        + NET_NAME_LEN + 2)
#define NET_NOROOM_LEN 8
#define NET_READY_LEN  16
#define NET_LEAVE_LEN  16
#define NET_LEFT_LEN   9
#define NET_BYE_LEN    NET_PREFIX
/* (Session 66) AGAIN, the game's: prefix, room, want (1 a rematch, 0 not
 * or no longer); the server's answer: header, room, the rival's want. A
 * server from before it drops the type unanswered. */
#define NET_AGAIN_C    (NET_PREFIX + 4 + 1)
#define NET_AGAIN_S    (NET_HEADER + 4 + 1)
/* INPUT, the game's: prefix, room, then the body the server passes on. */
#define NET_INPUT_BODY (4 + 4 + 8 + 4 + 1)    /* ack hframe hash first count */
#define NET_INPUT_C    (NET_PREFIX + 4 + NET_INPUT_BODY)
#define NET_INPUT_S    (NET_HEADER + NET_INPUT_BODY)

_Static_assert(NET_ROOMS_HEAD + NET_ROOMS_PAGE * NET_ROOM_ENTRY
               <= NET_PACKET_MAX, "a ROOMS fits");
_Static_assert(NET_INPUT_C + NET_INPUT_MAX <= NET_PACKET_MAX, "an INPUT fits");
_Static_assert(NET_WELCOME_LEN <= NET_HELLO_LEN
               && NET_REFUSE_LEN <= NET_HELLO_LEN, "no reply outgrows HELLO");

/* The host's choices, all a network game takes from either machine's
 * settings: the map, the difficulty (shown, not played: two humans), the
 * PATCHES (HzGame.fixes) and (session 59) the view, its byte 6 (a server's
 * --view rules it). */
typedef struct {
    u8  map;                    /* 0-7 */
    u8  difficulty;             /* 0-3 */
    u32 fixes;                  /* HZ_FIX_* */
    u8  view;                   /* 0 the split screen, 1 each its own */
} NetSettings;

#define NET_SETTINGS_VIEW 6     /* the view's byte in the packed settings */

void net_settings_pack(const NetSettings *s, u8 out[NET_SETTINGS_LEN]);
void net_settings_unpack(const u8 in[NET_SETTINGS_LEN], NetSettings *s);

/* The bytes, big-endian. */
void net_put16(u8 *p, u16 v);
void net_put32(u8 *p, u32 v);
void net_put64(u8 *p, u64 v);
u16  net_get16(const u8 *p);
u32  net_get32(const u8 *p);
u64  net_get64(const u8 *p);

/* The header; and whether p (n bytes) starts with one of this version,
 * giving its type (0 when not). */
void net_header(u8 *p, NetType type);
int  net_type(const u8 *p, int n);

/* A name as the game's font can draw it: upper case, A-Z 0-9 space and -
 * kept, anything else a space, the ends trimmed, NET_NAME_LEN at most;
 * "PLAYER" when nothing is left. `out` holds NET_NAME_LEN + 1. */
void net_name_clean(const char *in, char *out);

/* A name into its NET_NAME_LEN bytes (space-padded) and back. */
void net_name_put(u8 *p, const char *name);
void net_name_get(const u8 *p, char *out);

#endif /* HZ_NET_PROTO_H */
