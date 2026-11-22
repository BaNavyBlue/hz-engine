#ifndef HZ_SYSMENU_H
#define HZ_SYSMENU_H

#include "assets.h"
#include "rumble.h"
#include "../core/keys.h"
#include "../platform/platform.h"

/* The Esc screen: the port's own menu, after PS2's (src/game/sysmenu.c
 * there). Not from the ROM: the cartridge had no way to quit, no keys to
 * remap and no volumes. Escape, or a gamepad's Back, opens it; Escape or B
 * goes back a page and closes it from the first.
 *
 * While it is open the game is frozen: main.c runs it inside the frame
 * hook, so the 68k's code stays where it waits for VBlank (in a pass of
 * the frame loop, the fade, the pause) until the menu closes. The music
 * plays on (it runs on the audio thread, as the Z80 runs apart from the
 * 68k), as PS2's does: a stopped sequencer leaves notes ringing. Not in a
 * network game (session 66): there main.c ticks it a frame at a time
 * beside the game, which goes on with our side's pad let go, so the rival
 * never waits for it.
 *
 * It is drawn in the game's own parts: the window is the HUD strip's frame
 * tiles ($5E0 corner, $5E8 edge, $5EA side) round a black inside, the text
 * the game's font (cyan letters, the font's cyan digits), the cursor the
 * font's red arrow. HZ's menus make no sound when the cursor moves (the
 * menu module plays one effect in all its code), so neither does this.
 * Its colours are the HUD's palette line (GamePalettes line 1) as the
 * assets have it, handed to the platform beside CRAM (OVERLAY_BASE): a
 * fade or a colour cycle underneath never changes them.
 *
 * Pages: the menu (CONTINUE, DISPLAY, FULLSCREEN, CRT, AUDIO, CONTROLS,
 * GAMEPAD, PATCHES, QUIT), CRT and HDR (PS2's, below), AUDIO (MASTER, MUSIC,
 * EFFECTS in whole dB, -30 to 0, and OFF; SYNTH, NUKED or Q, PS2's Q
 * backend where the build has it, else Q NOT BUILT and NUKED stays;
 * session 38's VOICES, Q's, as PS2's: CHIP, the chip's six channels, an
 * effect silencing the music's part on its channel, or EXTRA, the music
 * on voices of its own playing on under the effects; session 39's REPEATS,
 * Q's: FRESH, a note struck again on a sounding voice starting on a fresh
 * voice from silence, the old one fading, as PS2's, or CHIP, its attack
 * from the level the voice had, as the chip's, the default since session
 * 42), CONTROLS (each of the pad's
 * eight buttons and TURBO: two keys; DEFAULTS, the keys'),
 * (session 54) GAMEPAD (PAD 1 and PAD 2, the name each connected gamepad
 * gives, or NONE -- A, C or Start on one buzzes it, to tell which is
 * which; RUMBLE, OFF, LOW, MEDIUM or HIGH, game/rumble.h, every gamepad
 * buzzing at the new level as it changes; BUTTONS, which opens the
 * BUTTONS page; the menu's row shows how many are connected), BUTTONS
 * (session 54: each of the pad's eight buttons and TURBO, two gamepad
 * inputs -- any button, the d-pad, a stick pushed or a trigger pulled,
 * a mapless joystick's buttons; drawn as PAD 1 labels them, core/keys.h;
 * DEFAULTS, the gamepads') and
 * PATCHES (each of the port's gameplay fixes, HZ_FIX_*, ON or OFF, what it
 * does on the line under it; all on by default; OFF plays as the
 * original). Up and down move;
 * left and right step a value, or pick the column on CONTROLS and
 * BUTTONS; A, C or Start choose. The fixed keys (arrows, Enter), and
 * (session 54) every gamepad's d-pad and left stick, always work the
 * menu, whatever is bound.
 *
 * The CRT and HDR pages are PS2's (its builds 105-108; docs/crt.md): CRT
 * has SCANLINES (ON, OFF), MASK (OFF, APERTURE GRILLE, SLOT MASK, SHADOW
 * MASK), SUBPIXELS (RGB, BGR, OTHER: the panel's own, which no OS
 * reports) and HDR, whose C opens the HDR page: HDR (ON, OFF), PEAK (AUTO,
 * or 400 to 10000 nits: the panel's brightest) and WHITE (the game's white
 * in nits, 50 to 1000). Under each value, why the display can't have it
 * (NEEDS FULLSCREEN, NEEDS 480P / 1080P / 1440P, DESKTOP SCALED, NO HDR),
 * judged against what the platform says the display is
 * (platform_display_info, refreshed by main.c each frame the menu is
 * open). The menu's CRT row shows what will be drawn: the mask, else
 * SCANLINES, else OFF. Everything is off by default. Escape or B goes back
 * a page.
 *
 * (Session 57) CPU, on the menu itself: ORIGINAL or NIGHTMARE, the brain
 * that plays a side the CPU has, from the next game on: the original's
 * (game/mech_ai.c) or the nightmare AI (game/nightmare.c,
 * docs/nightmare.md). Left, right, A, C or Start change it.
 *
 * (Session 56) NETWORK (docs/netplay.md): SERVER, the known servers
 * (left and right pick one while offline), (session 57) ADDRESS (the
 * chosen server's; A while offline types another, as NAME, ';' for ':'
 * and the keypad's digits too; Enter adds it to the player's own servers
 * and chooses it, Escape leaves it), NAME
 * (the name the others see: A, then type, or with a pad up and down
 * change the last letter, right adds one, left takes one off; Enter, A or
 * Escape ends it), CONNECT (DISCONNECT online; under it how many are
 * online and waiting, or why it failed), QUICK MATCH (the player waiting
 * longest, or wait for one; (session 58) left and right: PEOPLE OR AI,
 * the server's nightmare AI when no one comes, or PEOPLE ONLY), HOST GAME
 * (wait to be picked; CANCEL while waiting), MAP (the map a hosted game is
 * played on), (session 59) VIEW (SPLIT SCREEN or OWN SIDE ONLY: a hosted
 * game's, and PLAY IT HERE's), (session 58) NIGHTMARE (PLAY IT HERE: a
 * game against the nightmare AI on this machine, still waiting for people
 * online; one who comes ends it), then the players waiting, each a row (A plays them; the
 * server's AI marked SERVER AI). The page is only a view: main.c
 * fills SysMenu.net from the network each frame and does what
 * net_request asks. The menu's NETWORK row shows the state.
 *
 * The settings are kept in saves/settings.json ($HZ_SAVES/settings.json
 * when set), written whenever one changes and read at start-up; (session
 * 54) RUMBLE as "rumble": "off", "low", "medium" or "high"; (session 57)
 * CPU as "cpu": "original" or "nightmare"; (session 56)
 * "net": { "name", "server" ("host:port" of the one chosen), "map", "delay"
 * (frames of input delay, 0 from the round trip), (session 58) "quick_ai"
 * (true: PEOPLE OR AI), (session 59) "view" ("split" or "own"),
 * "servers": [ { "name", "host", "port" } ] }, the
 * player's own servers, beside those the game knows (sysmenu_add_server). */

typedef enum {
    SYSMENU_CONTINUE = 0,
    SYSMENU_DISPLAY,
    SYSMENU_FULLSCREEN,
    SYSMENU_CRT,            /* opens the CRT page */
    SYSMENU_AUDIO,
    SYSMENU_CONTROLS,
    SYSMENU_GAMEPAD,        /* (session 54) opens the GAMEPAD page */
    SYSMENU_PATCHES,        /* opens the PATCHES page (HzGame.fixes) */
    SYSMENU_NETWORK,        /* (session 56) opens the NETWORK page */
    SYSMENU_CPU,            /* (session 57) ORIGINAL or NIGHTMARE */
    SYSMENU_QUIT,
    SYSMENU_ITEMS
} SysMenuItem;

typedef enum {
    SYSMENU_PAGE_MAIN = 0,
    SYSMENU_PAGE_AUDIO,
    SYSMENU_PAGE_CONTROLS,
    SYSMENU_PAGE_CRT,
    SYSMENU_PAGE_HDR,
    SYSMENU_PAGE_PATCHES,
    SYSMENU_PAGE_GAMEPAD,   /* (session 54) */
    SYSMENU_PAGE_BUTTONS,   /* (session 54) the gamepads' controls */
    SYSMENU_PAGE_NETWORK    /* (session 56) */
} SysMenuPage;

/* (Session 56) The NETWORK page's rows; after them a row per player
 * waiting (SysMenu.net.rooms of them). */
typedef enum {
    SYSMENU_NET_SERVER = 0,
    SYSMENU_NET_ADDRESS,            /* (session 57) */
    SYSMENU_NET_NAME,
    SYSMENU_NET_CONNECT,
    SYSMENU_NET_QUICK,
    SYSMENU_NET_HOST,
    SYSMENU_NET_MAP,
    SYSMENU_NET_VIEW,               /* (session 59) */
    SYSMENU_NET_NIGHTMARE,          /* (session 58) */
    SYSMENU_NET_ITEMS
} SysMenuNetItem;

#define SYSMENU_NET_LIST     4      /* players waiting shown (5 before
                                       VIEW took the room) */
#define SYSMENU_NET_SERVERS  12
#define SYSMENU_NET_NAME_LEN 12     /* NET_NAME_LEN */
#define SYSMENU_NET_HOST_LEN 63
#define SYSMENU_NET_ADDR_LEN (SYSMENU_NET_HOST_LEN + 8)  /* "[host]:port" */

/* Where a known server came from: only the settings' own are written
 * back to the settings. */
typedef enum {
    SYSMENU_SERVER_BUILTIN = 0,     /* THIS MACHINE: 127.0.0.1 */
    SYSMENU_SERVER_FILE,            /* servers.txt beside the game */
    SYSMENU_SERVER_SETTINGS,
    SYSMENU_SERVER_COMMAND          /* --server */
} SysMenuServerOrigin;

typedef struct {
    char name[SYSMENU_NET_NAME_LEN + 1];
    char host[SYSMENU_NET_HOST_LEN + 1];
    int  port;
    SysMenuServerOrigin origin;
} SysMenuServer;

typedef enum {
    SYSMENU_NETV_OFFLINE = 0,
    SYSMENU_NETV_CONNECTING,
    SYSMENU_NETV_ONLINE,
    SYSMENU_NETV_WAITING,           /* hosting, or a quick match, waiting */
    SYSMENU_NETV_PLAYING,
    SYSMENU_NETV_AFTER              /* (session 66) a game over, its room
                                       kept: HOST GAME is REMATCH */
} SysMenuNetState;

/* What the page shows of the network, main.c's each frame. */
typedef struct {
    SysMenuNetState state;
    char status[40];                /* under CONNECT */
    int  rooms;                     /* rows of players waiting, the first
                                       SYSMENU_NET_LIST */
    int  rooms_total;
    char room_name[SYSMENU_NET_LIST][SYSMENU_NET_NAME_LEN + 1];
    u32  room_id[SYSMENU_NET_LIST];
    bool room_same[SYSMENU_NET_LIST];   /* the same game: can be joined */
    bool room_ai[SYSMENU_NET_LIST];     /* (session 58) the server's AI */
    u16  room_secs[SYSMENU_NET_LIST];
    u8   view_rule;                 /* (session 59) the server's: 0 none, 1
                                       the split screen, 2 own screens */
    bool asked, rival_asked;        /* (session 66) AFTER: the rematch */
} SysMenuNetView;

typedef enum {
    SYSMENU_NETREQ_NONE = 0,
    SYSMENU_NETREQ_CONNECT,
    SYSMENU_NETREQ_DISCONNECT,
    SYSMENU_NETREQ_QUICK,
    SYSMENU_NETREQ_HOST,
    SYSMENU_NETREQ_JOIN,            /* net_request_room */
    SYSMENU_NETREQ_CANCEL,
    SYSMENU_NETREQ_NIGHTMARE,       /* (session 58) the AI, here */
    SYSMENU_NETREQ_REMATCH          /* (session 66) asked, or no longer */
} SysMenuNetRequest;

/* (Session 54) The GAMEPAD page's rows. */
typedef enum {
    SYSMENU_GAMEPAD_PAD1 = 0,
    SYSMENU_GAMEPAD_PAD2,
    SYSMENU_GAMEPAD_RUMBLE,
    SYSMENU_GAMEPAD_BUTTONS,    /* opens the BUTTONS page */
    SYSMENU_GAMEPAD_ITEMS
} SysMenuGamepadItem;

/* A gamepad's name as the page shows it: this many characters at most. */
#define SYSMENU_PAD_NAME 19

typedef enum {
    SYSMENU_CRT_SCANLINES = 0,
    SYSMENU_CRT_MASK,
    SYSMENU_CRT_SUBPIXELS,
    SYSMENU_CRT_HDR,            /* C opens the HDR page */
    SYSMENU_CRT_ITEMS
} SysMenuCrtItem;

typedef enum {
    SYSMENU_HDR_SWITCH = 0,
    SYSMENU_HDR_PEAK,
    SYSMENU_HDR_WHITE,
    SYSMENU_HDR_ITEMS
} SysMenuHdrItem;

/* What sysmenu_crt_value gives, by row: the menu's CRT summary, the CRT
 * page's four rows and the HDR page's PEAK and WHITE. */
typedef enum {
    SYSMENU_VALUE_SUMMARY = 0,
    SYSMENU_VALUE_SCANLINES,
    SYSMENU_VALUE_MASK,
    SYSMENU_VALUE_SUBPIXELS,
    SYSMENU_VALUE_HDR,
    SYSMENU_VALUE_PEAK,
    SYSMENU_VALUE_WHITE,
    SYSMENU_VALUE_COUNT
} SysMenuValue;

typedef enum {
    SYSMENU_AUDIO_MASTER = 0,
    SYSMENU_AUDIO_MUSIC,
    SYSMENU_AUDIO_EFFECTS,
    SYSMENU_AUDIO_SYNTH,        /* NUKED or Q (core/audio.h) */
    SYSMENU_AUDIO_VOICES,       /* (session 38) Q's: CHIP or EXTRA */
    SYSMENU_AUDIO_REPEATS,      /* (session 39) Q's: FRESH or CHIP */
    SYSMENU_AUDIO_OVERSAMPLE,   /* (session 48) Q's: 1X, 2X or 4X */
    SYSMENU_AUDIO_ITEMS
} SysMenuAudioItem;

/* CONTROLS: a row per pad button, then DEFAULTS; columns KEY 1, KEY 2.
 * (Session 54) BUTTONS the same, its columns 1ST and 2ND, a gamepad
 * input each. */
#define SYSMENU_CONTROL_ROWS (CONTROL_ACTIONS + 1)
#define SYSMENU_CONTROL_COLS CONTROL_KEYS
#define SYSMENU_BUTTON_ROWS  (CONTROL_ACTIONS + 1)
#define SYSMENU_BUTTON_COLS  CONTROL_PAD_SLOTS

/* The volumes' range in whole dB: 0 the default and the top, down to
 * SYSMENU_DB_MIN, and below it OFF (AUDIO_DB_OFF). */
#define SYSMENU_DB_MIN (-30)

typedef struct {
    int master_db;
    int music_db;
    int effects_db;
    int synth;          /* AUDIO_SYNTH_NUKED or AUDIO_SYNTH_Q */
    int voices;         /* AUDIO_VOICES_CHIP or AUDIO_VOICES_EXTRA */
    int repeats;        /* AUDIO_REPEATS_FRESH or AUDIO_REPEATS_CHIP */
    int oversample;     /* 1, 2 or 4 (hz_audio_set_oversample) */
} AudioSettings;

typedef struct {
    bool        open;
    SysMenuPage page;
    int         cursor;         /* the menu's row */
    int         audio_cursor;
    int         crt_cursor;
    int         hdr_cursor;
    int         patches_cursor; /* a row per HZ_FIX_* bit, in bit order */
    int         gamepad_cursor; /* (session 54) SYSMENU_GAMEPAD_* */
    int         controls_row;
    int         controls_col;
    int         buttons_row;    /* (session 54) the BUTTONS page's */
    int         buttons_col;
    bool        listening;      /* CONTROLS: waiting for a key; BUTTONS,
                                   for a gamepad input */
    s16         repeat;         /* up/down held: $1F frames to the next */
    s16         side_repeat;    /* left/right held on a value */
    u16         ticks;          /* frames open (the waiting slot blinks) */
    Buttons     prev;           /* held at the last tick: a press is a
                                   button down now and not then */

    /* The settings. main.c hands a changed one to the platform or the
     * mixer and writes settings.json, then clears its flag. */
    AspectMode    display;
    bool          fullscreen;
    AudioSettings audio;
    Controls      controls;
    u32           fixes;        /* HzGame.fixes: HZ_FIX_DEFAULT at first */
    CrtSettings   crt;          /* the CRT and HDR pages' */
    CrtDisplay    crt_display;  /* what the display is, from the platform */
    RumbleLevel   rumble;       /* (session 54) RUMBLE_DEFAULT at first */
    u8            cpu_brain;    /* (session 57) HZ_BRAIN_*: who plays a
                                   CPU side, from the next game on */
    char          pad_name[PLATFORM_PADS][SYSMENU_PAD_NAME + 1];
                                /* from the platform, "" none (main.c,
                                   sysmenu_set_pad_name) */
    PadStyle      pad_style;    /* PAD 1's, whose labels BUTTONS draws;
                                   PAD_STYLE_NONE with none (main.c, from
                                   the platform each frame) */
    int           rumble_test;  /* a gamepad to buzz (PLATFORM_PADS: every
                                   one), -1 none: main.c's to do, then -1 */
    bool          display_changed;
    bool          fullscreen_changed;
    bool          audio_changed;
    bool          controls_changed;
    bool          fixes_changed;
    bool          crt_changed;
    bool          rumble_changed;
    bool          cpu_brain_changed;
    bool          quit;         /* QUIT chosen */

    /* (Session 56) NETWORK. */
    int           net_cursor;   /* SYSMENU_NET_*, then the rooms' rows */
    bool          net_typing;   /* NAME taking letters */
    bool          addr_typing;  /* (session 57) ADDRESS taking them */
    char          addr_text[SYSMENU_NET_ADDR_LEN + 1];  /* what's typed */
    const char   *addr_error;   /* why Enter didn't take it, or NULL */
    Buttons       type_prev;    /* the pads' and fixed keys' while typing */
    SysMenuServer servers[SYSMENU_NET_SERVERS];
    int           server_count;
    int           server;       /* the chosen */
    char          server_key[SYSMENU_NET_HOST_LEN + 8];  /* "host:port"
                                   from the settings, until it's found */
    char          net_name[SYSMENU_NET_NAME_LEN + 1];
    int           net_map;      /* 0-7 */
    int           net_delay;    /* frames, 0 from the round trip */
    bool          net_quick_ai; /* (session 58) QUICK MATCH: the server's
                                   AI when no one comes */
    bool          net_own_view; /* (session 59) VIEW: a game hosted here
                                   each side on its own screen */
    SysMenuNetView net;
    SysMenuNetRequest net_request;  /* main.c's to do, then NONE */
    u32           net_request_room;
    bool          net_changed;  /* the server, name or map: to save */

    u16       colours[OVERLAY_COLOURS];   /* for OVERLAY_BASE + 0..15 */
    const u8 *font;             /* FontTiles */
    const u8 *hud;              /* HUDTiles: VRAM tiles $500-$5FF */
} SysMenu;

/* Closed, the defaults (TV aspect, windowed, 0 dB, controls_default, the
 * fixes on, the CRT off, RUMBLE MEDIUM), the art and colours from the
 * assets. */
void sysmenu_init(SysMenu *m, const HzAssets *assets);

/* saves/settings.json, or under $HZ_SAVES; `path` NULL for that again. */
void        sysmenu_set_settings_path(const char *path);
const char *sysmenu_settings_path(void);

/* Reads the settings file over the defaults. No file: false, and nothing
 * said (the first run); a file that doesn't parse: a message, the
 * defaults kept. A key missing or unknown keeps its default. */
bool sysmenu_load_settings(SysMenu *m);
void sysmenu_save_settings(const SysMenu *m);

/* Opens on the menu, with what the platform shows now (F10 and F11 change
 * them outside the menu) and the buttons held now taken as already down. */
void sysmenu_open(SysMenu *m, const Input *in, AspectMode display,
                  bool fullscreen);

/* One frame of the open menu. */
void sysmenu_tick(SysMenu *m, const Input *in);

/* (Session 54) Gamepad `pad`'s name for the GAMEPAD page (NULL: none
 * there): upper case, the characters the font hasn't as spaces, cut to
 * SYSMENU_PAD_NAME. */
void sysmenu_set_pad_name(SysMenu *m, int pad, const char *name);

/* Draws the open menu over a composed frame (palette indices), in
 * OVERLAY_BASE + 0..15. */
void sysmenu_draw(const SysMenu *m, u8 *indices);

/* What a row shows, for the menu and the harness: the dB text ("-6 DB",
 * "OFF"), a CONTROLS slot's key ("-" when none), and (session 54) a
 * BUTTONS slot's gamepad input as m->pad_style labels it. */
void sysmenu_db_text(int db, char *out, size_t n);

/* (PS2's) The text a CRT or HDR row shows, and under it why the chosen
 * value can't be had on m->crt_display ("" when it can). */
void sysmenu_crt_value(const SysMenu *m, SysMenuValue row, char *value,
                       size_t value_n, char *why, size_t why_n);
void sysmenu_slot_text(const SysMenu *m, int action, int col, char *out,
                       size_t n);
void sysmenu_button_text(const SysMenu *m, int action, int slot, char *out,
                         size_t n);

/* (Session 56) A known server for NETWORK: its index (the one already
 * there with the same host and port, its name kept), or -1 when the list
 * is full. `name` NULL or "": the host is the name. */
int sysmenu_add_server(SysMenu *m, const char *name, const char *host,
                       int port, SysMenuServerOrigin origin);

/* (Session 57) An address as a player writes it, --server's or ADDRESS's:
 * HOST, HOST:PORT, [V6] or [V6]:PORT; a bare IPv6 address (two colons or
 * more) has no port; no port is 19890. False if it isn't one: a host
 * sysmenu_add_server wouldn't take, or a port not 1-65535. */
bool sysmenu_parse_address(const char *text, char *host, size_t host_n,
                           int *port);

/* (Session 56) servers.txt: a server a line, "NAME HOST [PORT]" (the
 * name's spaces as _), # a comment. Nothing said when there's no file. */
void sysmenu_load_servers_file(SysMenu *m, const char *path);

/* (Session 56) The chosen server: the settings' "server" if it's known,
 * else the first. After every server is added. */
void sysmenu_pick_server(SysMenu *m);

/* (Session 56) A notice over the frame, the menu closed: a window with
 * one or two lines (`line2` NULL or ""), e.g. WAITING FOR RIVAL. */
void sysmenu_draw_notice(const SysMenu *m, u8 *indices, const char *line1,
                         const char *line2);

#endif /* HZ_SYSMENU_H */
