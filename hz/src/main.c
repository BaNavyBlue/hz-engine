/* Herzog Zwei port: the game through a platform (SDL3, or headless).
 *
 * The game's data comes from assets/, all of it read at start-up. (Session
 * 78) The game makes it itself the first time it starts, from the player's
 * own cartridge image (src/extract/, docs/first_start.md; tools/
 * hz_extract.py is the reference it is proved against); after that the
 * cartridge is never read. Then the boot (Snd_Init's commands, v_options as a cold
 * boot leaves them) and MainLoop_Title, round and round: the title and
 * the options screen (MenuModule_Title, game/front.c), Game_SetupMode,
 * the map, Game_Start and Game_FrameLoop pass by pass. The menus and the
 * frame loop hand each finished frame to on_frame (where the 68k waits
 * for VBlank), which presents it, pumps the platform and sets the
 * controller ports for the next read.
 *
 * When the title runs out (its song ends before Start) the attract demo
 * plays (game/demo.c): the demo list's next map, both mechs driven by its
 * recording, until it runs out, 60 s or Start, then the title again.
 * The endings follow the results of a record with every map won on every
 * difficulty; --ending p1 (or p2) shows them at start-up, as the human on
 * that side against the computer, then goes on to the title.
 *
 * No password: a win is saved to saves/progress.json (game/saves.h), and
 * CONT on the options screen loads a saved game. --map, --mode or
 * --difficulty start that game at once, skipping the title the first
 * time round.
 *
 * (Session 26) Escape opens the Esc screen (game/sysmenu.c) in the frame
 * hook: the game or the menu waits where it is, the music plays on,
 * until it closes. Its settings (display, fullscreen, the volumes, the
 * controls) are read from saves/settings.json at start-up and written
 * when they change; --aspect and --fullscreen set them for one run.
 *
 * (Session 54) The rumble (game/rumble.h): each frame what the game did
 * shakes the gamepads of the side it happened to, as route_input sends
 * them (a CPU's side reaches no one), at the Esc screen's RUMBLE; the
 * menu stops it while it is open and plays its test buzz.
 *
 * (Session 49) The mods folder (docs/mods.md, PS2's): a file in mods/ at
 * the place of one in assets/ is read in its place; --mods or HZ_MODS
 * names another folder ("" none). Its README.txt is written when missing,
 * and every start says how many mods are in use and names them.
 *
 * (Session 56) The network game (docs/netplay.md): the Esc screen's
 * NETWORK page goes online through a server (server/hz_server.c), whose
 * lobby is polled every frame -- while the title runs or a game against
 * the CPU goes on, and while the menu is open -- so a rival found while
 * playing alone starts the network game at once: whatever runs is left as
 * QUIT leaves it (g->quit), and main's loop starts the network game from
 * a cold boot, the same on both machines (run_net_game): two players on
 * the split screen, the host's map and PATCHES, the server's seed. Its
 * frames go through net_frame: our pad (every controller here drives our
 * side) for a frame `delay` on, the rival's for this one waited for, a
 * hash of the state every NET_HASH_EVERY frames to catch a split. The
 * Esc screen doesn't hold the game here (session 66): it is ticked beside
 * it, our side's pad none while it is open, so the rival plays on.
 * When it ends (its results shown, or the rival gone, or the games split)
 * the NETWORK page comes up (session 66: REMATCH after a game that ended
 * as games do), and closed, the title, still online. --server, --name and --net reach
 * the network from the command line; HZ_NETLOG writes the hashes, and
 * HZ_NET_FRAMES quits when the network game reaches that frame, and
 * (session 66) HZ_NET_ESC=AT:FOR presses Escape at network frame AT and
 * again FOR frames on; HZ_NET_END=AT:WHO puts WHO's HQ (1 P1's, 2 P2's,
 * 3 both) at 0 HP at network frame AT of every game, HZ_NET_REMATCH=1
 * asks for a rematch after each, and HZ_NET_GAMES quits after that many
 * (for the checks: tools/hznetplay_check.py).
 *
 * (Session 63) Every frame is paced by the platform at the game's 59.92
 * Hz whatever the display's rate (src/core/pace.h). In the network game,
 * a wait for the rival's pad isn't owed afterwards (platform_pace_held),
 * and the side running ahead of the other holds back a little until they
 * are even (net_game_slow, platform_pace_slow), so both keep the same
 * margin against the line's bumps.
 *
 * (Session 59) Every game played here, against the CPU, two players or
 * over the network, is recorded (game/replay.h) and written when it ends
 * to saves/replays/ (beside settings.json), a small text file a game, for
 * tools/hz_replay.c to play again and look at. HZ_REPLAYS=0 records none. */

#include "core/hz_file.h"
#include "core/hz_mods.h"
#include "core/pace.h"
#include "core/video.h"
#include "core/audio.h"
#include "game/game.h"
#include "game/replay.h"
#include "game/rumble.h"
#include "game/statehash.h"
#include "game/tick.h"
#include "game/sysmenu.h"
#include "net/net_client.h"
#include "platform/platform.h"

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

typedef struct {
    Platform *platform;
    HzAudio  *audio;
    SysMenu   menu;
    bool      turbo_phase;      /* TURBO's: B on the frames it is set */
    Buttons   swallow;          /* held as the menu closed: kept from the
                                   game until let go */
    HzRumble  rumble;           /* (session 54) */
    RumbleVoice test[PLATFORM_PADS];    /* the GAMEPAD page's buzz */
    u8        idx[SCREEN_W * SCREEN_H];
    u8        composed[SCREEN_W * SCREEN_H];   /* the frame + the menu */

    /* (Session 56) The network game. */
    NetClient   net;
    u64         game_id;        /* net_game_id: what a rival must have */
    int         net_auto;       /* --net: 1 quick, 2 host, (session 58) 3
                                   nightmare, once online */
    bool        nightmare_start;    /* (session 58) the NETWORK page's
                                       NIGHTMARE: main's loop starts it */
    bool        nightmare_game;     /* the next game's CPU is the AI */
    bool        net_start;      /* a START came: main's loop starts it */
    bool        net_game;       /* the network game is on (net_frame) */
    NetGameStop net_stop;       /* why it ended early */
    bool        quit_program;   /* QUIT, or the window closed */
    FILE       *netlog;         /* HZ_NETLOG */
    int         net_frames;     /* HZ_NET_FRAMES: quit after so many */
    int         net_esc_at;     /* (session 66) HZ_NET_ESC: Escape at */
    int         net_esc_for;    /* that network frame, and again after
                                   so many */
    int         net_end_at;     /* (session 66) HZ_NET_END: the HQs in */
    int         net_end_who;    /* net_end_who (1 P1's, 2 P2's, 3 both)
                                   at 0 HP at that network frame */
    bool        net_rematch;    /* HZ_NET_REMATCH=1: asked after each */
    int         net_games_max;  /* HZ_NET_GAMES: quit after so many */
    int         net_games;      /* network games played */
    bool        name_unsaved;   /* the name was made up this run */

    /* (Session 59) The game being recorded (replay.h). */
    HzReplay    replay;
    bool        recording;
    bool        replays_off;    /* HZ_REPLAYS=0 */
} Host;

static void pad_from_buttons(HzPad *to, const Buttons *b)
{
    to->up    = to->up    || b->up;
    to->down  = to->down  || b->down;
    to->left  = to->left  || b->left;
    to->right = to->right || b->right;
    to->a     = to->a     || b->a;
    to->b     = to->b     || b->b;
    to->c     = to->c     || b->c;
    to->start = to->start || b->start;
}

/* TURBO (the port's): while it is held, B on every other frame, so each
 * frame with B is a fresh press. A human's mech still waits out
 * mech_fire_delay between shots (6 frames), so this fires as often as a
 * player's gun ever can; a computer's mech waits for no delay, only for
 * B to come again, so it can fire every other frame. */
static void pad_turbo(HzPad *to, const Buttons *b, bool phase)
{
    if (b->turbo && phase) {
        to->b = true;
    }
}

/* (Session 56) A pad as its byte (HZ_PAD_*), what the network carries,
 * and back. */
static u8 pad_bits(const HzPad *p)
{
    return (u8)((p->up ? HZ_PAD_UP : 0) | (p->down ? HZ_PAD_DOWN : 0)
                | (p->left ? HZ_PAD_LEFT : 0) | (p->right ? HZ_PAD_RIGHT : 0)
                | (p->a ? HZ_PAD_A : 0) | (p->b ? HZ_PAD_B : 0)
                | (p->c ? HZ_PAD_C : 0) | (p->start ? HZ_PAD_START : 0));
}

static void pad_from_bits(HzPad *p, u8 b)
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

/* Which controller drives which port. The players are the human ones
 * (v_player_flags b0 clear), P1 first; both when neither is (the pads then
 * only pause). The keyboard drives the first player. Gamepads: with at
 * least one per player, gamepad k drives player k (the keyboard as well
 * as gamepad 1 drives the first); with fewer, they go to the players
 * after the keyboard's, so the keyboard and one gamepad make two players.
 * With one player every controller drives it. (A CPU rival's port matters
 * little: Mechs_Update ORs both joypads into the human's mech_pad.) The
 * flags are read each frame: frames come while the map loads too, before
 * Game_SetupMode has set them (all clear then: both ports). */
typedef struct {
    int players[2];
    int count;
} Route;

static Route route_players(const HzGame *g)
{
    Route r = { { 0, 1 }, 0 };
    for (int p = 0; p < 2; p++) {
        if (!(g->player_flags[p] & 0x01)) {
            r.players[r.count++] = p;
        }
    }
    if (r.count == 0) {
        r.players[0] = 0;
        r.players[1] = 1;
        r.count = 2;
    }
    return r;
}

/* The port gamepad k of `pads` drives. */
static int route_pad(const Route *r, int pads, int k)
{
    int first = (pads >= r->count) ? 0 : 1;
    return r->players[(first + k) % r->count];
}

static void route_input(const HzGame *g, const Input *in, HzPad pad_in[2],
                        bool turbo_phase)
{
    Route r = route_players(g);
    memset(pad_in, 0, 2 * sizeof pad_in[0]);
    pad_from_buttons(&pad_in[r.players[0]], &in->keyboard);
    pad_turbo(&pad_in[r.players[0]], &in->keyboard, turbo_phase);
    for (int k = 0; k < in->pads; k++) {
        HzPad *to = &pad_in[route_pad(&r, in->pads, k)];
        pad_from_buttons(to, &in->pad[k]);
        pad_turbo(to, &in->pad[k], turbo_phase);
    }
}

/* (Session 54) The rumble after a frame of the game: what it did, from
 * sfx_asked and the state (game/rumble.h), to each gamepad as the side it
 * drives felt it -- none when that side is the CPU's -- at RUMBLE. */
static void rumble_frame(Host *h, HzGame *g, int pads)
{
    u32 sfx = g->sfx_asked;
    g->sfx_asked = 0;
    rumble_watch(&h->rumble, g, sfx);
    Route r = route_players(g);
    for (int k = 0; k < PLATFORM_PADS; k++) {
        RumbleMotors m = { 0, 0 };
        if (k < pads) {
            int side = route_pad(&r, pads, k);
            if (!(g->player_flags[side] & 0x01)) {
                m = rumble_scale(rumble_motors(&h->rumble, side),
                                 h->menu.rumble);
            }
        }
        platform_rumble(h->platform, k, m.low, m.high);
    }
}

/* (Session 54) Every gamepad still. */
static void rumble_stop(Host *h)
{
    for (int k = 0; k < PLATFORM_PADS; k++) {
        platform_rumble(h->platform, k, 0, 0);
    }
}

/* (Session 54) The Esc screen's side of the gamepads: their names for the
 * GAMEPAD page, PAD 1's labels for BUTTONS, and the test buzz, a frame of
 * it. */
static void rumble_menu(Host *h)
{
    SysMenu *m = &h->menu;
    m->pad_style = platform_pad_style(h->platform, 0);
    for (int k = 0; k < PLATFORM_PADS; k++) {
        sysmenu_set_pad_name(m, k, platform_pad_name(h->platform, k));
        rumble_voices_age(&h->test[k], 1);
        if (m->rumble_test == k || m->rumble_test == PLATFORM_PADS) {
            rumble_test(&h->test[k], 1);
        }
        RumbleMotors mo = rumble_scale(rumble_voices_motors(&h->test[k], 1),
                                       m->rumble);
        platform_rumble(h->platform, k, mo.low, mo.high);
    }
    m->rumble_test = -1;
}

static void buttons_or(Buttons *to, const Buttons *from)
{
    to->up    = to->up    || from->up;
    to->down  = to->down  || from->down;
    to->left  = to->left  || from->left;
    to->right = to->right || from->right;
    to->a     = to->a     || from->a;
    to->b     = to->b     || from->b;
    to->c     = to->c     || from->c;
    to->start = to->start || from->start;
    to->turbo = to->turbo || from->turbo;
}

/* `b` without the buttons in `m`. */
static void buttons_mask(Buttons *b, const Buttons *m)
{
    b->up    = b->up    && !m->up;
    b->down  = b->down  && !m->down;
    b->left  = b->left  && !m->left;
    b->right = b->right && !m->right;
    b->a     = b->a     && !m->a;
    b->b     = b->b     && !m->b;
    b->c     = b->c     && !m->c;
    b->start = b->start && !m->start;
    b->turbo = b->turbo && !m->turbo;
}

/* `b` with only the buttons also in `m`. */
static void buttons_and(Buttons *b, const Buttons *m)
{
    b->up    = b->up    && m->up;
    b->down  = b->down  && m->down;
    b->left  = b->left  && m->left;
    b->right = b->right && m->right;
    b->a     = b->a     && m->a;
    b->b     = b->b     && m->b;
    b->c     = b->c     && m->c;
    b->start = b->start && m->start;
    b->turbo = b->turbo && m->turbo;
}

/* Buttons held anywhere: the keyboard, the gamepads, the fixed keys. */
static Buttons all_held(const Input *in)
{
    Buttons b = in->keyboard;
    for (int i = 0; i < in->pads; i++) {
        buttons_or(&b, &in->pad[i]);
    }
    buttons_or(&b, &in->nav);
    return b;
}

/* What closed the menu (Enter on CONTINUE is Start too, B is B) must not
 * reach the game: those buttons are kept from it until they are let go
 * everywhere. */
static void swallow(Host *h, Input *in)
{
    Buttons held = all_held(in);
    buttons_and(&h->swallow, &held);
    buttons_mask(&in->keyboard, &h->swallow);
    for (int i = 0; i < in->pads; i++) {
        buttons_mask(&in->pad[i], &h->swallow);
    }
}

/* A changed setting to the platform or the mixer, and the file. */
static void apply_settings(Host *h)
{
    SysMenu *m = &h->menu;
    bool changed = false;
    if (m->display_changed) {
        platform_set_aspect(h->platform, m->display);
        m->display_changed = false;
        changed = true;
    }
    if (m->fullscreen_changed) {
        platform_set_fullscreen(h->platform, m->fullscreen);
        m->fullscreen = platform_fullscreen(h->platform);
        m->fullscreen_changed = false;
        changed = true;
    }
    if (m->audio_changed) {
        if (h->audio != NULL) {
            hz_audio_set_volume(h->audio, m->audio.master_db,
                                m->audio.music_db, m->audio.effects_db);
            hz_audio_set_synth(h->audio, m->audio.synth);
            hz_audio_set_voices(h->audio, m->audio.voices);
            hz_audio_set_repeats(h->audio, m->audio.repeats);
            hz_audio_set_oversample(h->audio, m->audio.oversample);
        }
        m->audio_changed = false;
        changed = true;
    }
    if (m->controls_changed) {
        platform_set_controls(h->platform, &m->controls);
        m->controls_changed = false;
        changed = true;
    }
    if (m->crt_changed) {
        platform_set_crt(h->platform, &m->crt);
        m->crt_changed = false;
        changed = true;
    }
    if (m->rumble_changed) {
        m->rumble_changed = false;      /* main.c reads m->rumble */
        changed = true;
    }
    if (m->net_changed) {
        m->net_changed = false;         /* (session 56) read when used */
        changed = true;
    }
    if (changed) {
        sysmenu_save_settings(m);
    }
}

/* (Session 56) What the NETWORK page shows, from the client. */
static void net_view(Host *h)
{
    const NetClient *c = &h->net;
    SysMenuNetView *v = &h->menu.net;
    switch (c->state) {
    case NETC_CONNECTING:
        v->state = SYSMENU_NETV_CONNECTING;
        break;
    case NETC_LOBBY:
        v->state = SYSMENU_NETV_ONLINE;
        break;
    case NETC_ASKING:
        v->state = SYSMENU_NETV_ONLINE;
        break;
    case NETC_WAITING:
        v->state = SYSMENU_NETV_WAITING;
        break;
    case NETC_PLAYING:
        v->state = SYSMENU_NETV_PLAYING;
        break;
    case NETC_AFTER:
        v->state = SYSMENU_NETV_AFTER;      /* (session 66) */
        break;
    default:
        v->state = SYSMENU_NETV_OFFLINE;
        break;
    }
    v->asked = c->state == NETC_AFTER && c->again;
    v->rival_asked = c->state == NETC_AFTER && c->rival_again;
    if (c->state == NETC_FAILED || (c->error[0] != '\0'
                                    && c->state != NETC_OFFLINE)) {
        snprintf(v->status, sizeof v->status, "%s", c->error);
    } else if (c->state == NETC_PLAYING) {
        snprintf(v->status, sizeof v->status, "AGAINST %s", c->rival);
    } else if (c->state == NETC_AFTER) {
        snprintf(v->status, sizeof v->status, "PLAYED %s", c->rival);
    } else if (c->state == NETC_CONNECTING || c->state == NETC_OFFLINE) {
        v->status[0] = '\0';
    } else {
        snprintf(v->status, sizeof v->status, "%d ONLINE  %d WAITING",
                 c->players, c->waiting);
    }
    int online = c->state == NETC_LOBBY || c->state == NETC_ASKING
                 || c->state == NETC_WAITING || c->state == NETC_AFTER;
    v->view_rule = (c->state == NETC_OFFLINE || c->state == NETC_FAILED
                    || c->state == NETC_CONNECTING) ? 0 : c->view_rule;
    v->rooms = 0;
    v->rooms_total = online ? c->rooms_total : 0;
    for (int k = 0; online && k < c->room_count && k < SYSMENU_NET_LIST;
         k++) {
        snprintf(v->room_name[k], sizeof v->room_name[k], "%s",
                 c->rooms[k].name);
        v->room_id[k] = c->rooms[k].id;
        v->room_same[k] = c->rooms[k].same_game;
        v->room_ai[k] = c->rooms[k].ai;
        v->room_secs[k] = c->rooms[k].secs;
        v->rooms++;
    }
}

/* (Session 56) The network game's settings, from this machine's: the
 * NETWORK page's MAP and (session 59) VIEW, the difficulty and the
 * PATCHES. The host's are the ones played. */
static NetSettings net_settings(const Host *h, const HzGame *g)
{
    NetSettings s;
    s.map = (u8)(h->menu.net_map & 7);
    s.difficulty = (u8)(g->options[1] & 3);
    s.fixes = h->menu.fixes;
    s.view = h->menu.net_own_view ? 1 : 0;      /* (session 59) */
    return s;
}

static void net_connect(Host *h)
{
    SysMenu *m = &h->menu;
    if (m->server >= m->server_count) {
        return;
    }
    const SysMenuServer *sv = &m->servers[m->server];
    if (h->name_unsaved) {
        h->name_unsaved = false;
        sysmenu_save_settings(m);
    }
    if (!net_client_connect(&h->net, sv->host, sv->port, m->net_name,
                            h->game_id)) {
        fprintf(stderr, "network: %s:%d: %s\n", sv->host, sv->port,
                h->net.error);
    }
}

/* (Session 58) The NETWORK page's NIGHTMARE: a game here against the
 * nightmare AI as the CPU, on the page's MAP and VIEW, while online the
 * player waits for people: a quick match people only (the server's AI
 * would end the game for itself), or the room already hosted. Whatever
 * runs is left as for a network game; main's loop starts it. A person
 * found ends it for the network game. */
static void net_nightmare(Host *h, HzGame *g)
{
    NetClient *c = &h->net;
    if (c->state == NETC_AFTER) {
        net_client_cancel(c);   /* (session 66) the last game's room left */
    }
    bool hosting = (c->state == NETC_WAITING || c->state == NETC_ASKING)
                   && c->ask == NET_HOST;
    if (!hosting && (c->state == NETC_LOBBY || c->state == NETC_WAITING
                     || c->state == NETC_ASKING)) {
        NetSettings s = net_settings(h, g);
        net_client_quick(c, &s, true);
    }
    h->nightmare_start = true;
    h->menu.open = false;
    g->quit = true;
}

/* (Session 56) What the NETWORK page asked for. DISCONNECT in a network
 * game ends it (the rival hears we left). */
static void net_requests(Host *h, HzGame *g)
{
    SysMenu *m = &h->menu;
    SysMenuNetRequest r = m->net_request;
    m->net_request = SYSMENU_NETREQ_NONE;
    NetSettings s = net_settings(h, g);
    switch (r) {
    case SYSMENU_NETREQ_CONNECT:
        net_connect(h);
        break;
    case SYSMENU_NETREQ_DISCONNECT:
        net_client_disconnect(&h->net);
        if (h->net_game) {
            h->net_stop = NETG_ON;      /* ours: nothing to say */
            m->open = false;
            g->quit = true;
        }
        break;
    case SYSMENU_NETREQ_QUICK:
        net_client_quick(&h->net, &s, !m->net_quick_ai);
        break;
    case SYSMENU_NETREQ_HOST:
        net_client_host(&h->net, &s);
        break;
    case SYSMENU_NETREQ_JOIN:
        net_client_join(&h->net, m->net_request_room, &s);
        break;
    case SYSMENU_NETREQ_CANCEL:
        net_client_cancel(&h->net);
        break;
    case SYSMENU_NETREQ_NIGHTMARE:
        net_nightmare(h, g);
        break;
    case SYSMENU_NETREQ_REMATCH:                /* (session 66) */
        net_client_rematch(&h->net, !h->net.again);
        break;
    default:
        break;
    }
}

/* (Session 56) The network, a frame of it (the game's frames, and the
 * menu's): the page's requests, what came, a START taken (the menu
 * closes and whatever runs is left for main's loop to start the network
 * game), and in a network game held by the menu our pads again, so the
 * rival hears from us while it waits. */
static void net_tick(Host *h, HzGame *g)
{
    NetClient *c = &h->net;
    net_requests(h, g);
    net_client_want_list(c, h->menu.open
                            && h->menu.page == SYSMENU_PAGE_NETWORK);
    net_client_poll(c);
    if (h->net_auto != 0 && c->state == NETC_LOBBY && c->rtt_known) {
        NetSettings s = net_settings(h, g);
        if (h->net_auto == 1) {
            net_client_quick(c, &s, !h->menu.net_quick_ai);
        } else if (h->net_auto == 3) {
            net_nightmare(h, g);
        } else {
            net_client_host(c, &s);
        }
        h->net_auto = 0;
    }
    if (h->net_game && c->state == NETC_PLAYING
        && net_now_ms() - c->input_sent >= 16) {
        net_game_send(c);
    }
    if (!h->net_game && net_client_take_start(c)) {
        h->net_start = true;
        h->menu.open = false;
        g->quit = true;
    }
    net_view(h);
}

/* The Esc screen opening, from the pump that asked for it. */
static void menu_begin(Host *h, const Input *in)
{
    sysmenu_open(&h->menu, in, platform_aspect(h->platform),
                 platform_fullscreen(h->platform));
    rumble_quiet(&h->rumble);           /* (session 54) */
    memset(h->test, 0, sizeof h->test);
    rumble_menu(h);
}

/* The frame under the open Esc screen, the menu drawn over it. */
static void menu_compose(Host *h)
{
    SysMenu *m = &h->menu;
    /* What the CRT page judges its values against: the display as it is
     * now (a window resized or moved, HDR switched in the desktop). */
    platform_display_info(h->platform, &m->crt_display);
    memcpy(h->composed, h->idx, sizeof h->composed);
    sysmenu_draw(m, h->composed);
}

/* The Esc screen closed: the test buzz stopped, and what closed it kept
 * from the game. */
static void menu_end(Host *h, const Input *in)
{
    memset(h->test, 0, sizeof h->test);
    rumble_stop(h);
    h->swallow = all_held(in);
}

/* A tick of the open Esc screen from a pump's input: the menu, the
 * network, a setting changed. False: QUIT. */
static bool menu_step(Host *h, HzGame *g, const Input *in)
{
    SysMenu *m = &h->menu;
    sysmenu_tick(m, in);
    net_tick(h, g);                     /* (session 56) */
    if (m->fixes_changed) {
        /* (Session 56) Not in a network game: both sides play the host's,
         * from the start; the next game here takes them. */
        if (!h->net_game) {
            g->fixes = m->fixes;
            if (h->recording) {
                replay_fixes(&h->replay, g->fixes);     /* (s. 59) */
            }
        }
        m->fixes_changed = false;
        sysmenu_save_settings(m);
    }
    if (m->cpu_brain_changed) {
        /* (Session 57) Taken by the next game (main's loop): a brain
         * changed mid-game would pick up the other's half-made plans. */
        m->cpu_brain_changed = false;
        sysmenu_save_settings(m);
    }
    apply_settings(h);
    rumble_menu(h);
    if (m->quit) {
        rumble_stop(h);
        return false;
    }
    /* F10 and F11 work with the menu open too: show what is. */
    m->display = platform_aspect(h->platform);
    m->fullscreen = platform_fullscreen(h->platform);
    if (!m->open) {
        menu_end(h, in);
    }
    return true;
}

/* The Esc screen, from the pump that asked for it until it closes: the
 * game's last frame with the menu over it, a pump, a tick. The game waits
 * meanwhile (not a network game's: net_frame ticks the menu beside it).
 * False: quit (QUIT, or the window closed). */
static bool run_menu(Host *h, HzGame *g, Input *in)
{
    menu_begin(h, in);
    while (h->menu.open) {
        menu_compose(h);
        platform_present(h->platform, h->composed, g->video->colours,
                         h->menu.colours);
        if (!platform_pump(h->platform, in) || !menu_step(h, g, in)) {
            return false;
        }
    }
    return true;
}

/* The game's Snd_* commands to the sound (game.h's snd_post). With
 * EFFECTS off an effect ($02) isn't sent at all: on the chip an effect
 * takes its channel from the music (Cmd_PlaySfx mutes the music track
 * there), so a silenced effect would still leave a hole in the music. */
static void post_sound(void *user, const u8 *bytes, int count)
{
    Host *h = user;
    if (bytes[0] == 0x02 && h->menu.audio.effects_db <= AUDIO_DB_OFF) {
        return;
    }
    if (!hz_audio_post(h->audio, bytes, count)) {
        fprintf(stderr, "hz: sound command $%02X dropped (ring full)\n",
                bytes[0]);
    }
}

/* (Session 59) The recording (replay.h): begun just before Game_Start, a
 * frame at each hand-over once the pads are set, and written when the
 * game's loop ends (before the finale: the winner is known). */
static void replay_start(Host *h, HzGame *g, const char *kind, int side,
                         const char *rival)
{
    h->recording = false;
    if (h->replays_off || g->game_type < 0) {
        return;                 /* the attract demo: not a game played */
    }
    replay_begin(&h->replay, g, h->game_id, NET_GAME_CODE, kind, side,
                 rival);
    h->recording = true;
}

static void replay_frame(Host *h, HzGame *g)
{
    if (h->recording && !replay_record(&h->replay, g)) {
        fprintf(stderr, "replay: no memory; this game isn't recorded\n");
        h->recording = false;
        replay_free(&h->replay);
    }
}

static void replay_stop(Host *h, HzGame *g, HzLoopExit exit)
{
    if (!h->recording) {
        return;
    }
    h->recording = false;
    const char *why = (exit == HZ_LOOP_GAME_OVER) ? "over"
                    : (h->net_game && h->net_stop != NETG_ON)
                    ? net_game_stop_text(h->net_stop)
                    : h->quit_program ? "closed" : "quit";
    replay_end(&h->replay, (exit == HZ_LOOP_GAME_OVER) ? (int)g->winner : -1,
               why);
    const char *saves = getenv("HZ_SAVES");
    char dir[512];
    snprintf(dir, sizeof dir, "%s",
             (saves != NULL && saves[0] != '\0') ? saves : "saves");
    make_dir(dir);
    size_t n = strlen(dir);
    snprintf(dir + n, sizeof dir - n, "/replays");
    make_dir(dir);
    char name[96];
    replay_file_name(&h->replay, name, sizeof name);
    char path[640];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (replay_save(&h->replay, path)) {
        fprintf(stderr, "replay: %s (%u frames, %s)\n", path,
                (unsigned)h->replay.frames, why);
    } else {
        fprintf(stderr, "replay: can't write %s\n", path);
    }
    replay_free(&h->replay);
}

/* VDP_WaitVBlank's hand-over: render, present, pump, route. Once the host
 * has asked to quit, frames still come until the game's loops notice
 * (a fade finishes); they are dropped. */
/* (Session 56) Our pad in a network game: every controller here drives
 * our side, TURBO and all. */
static u8 net_local_pad(Host *h, const Input *in)
{
    HzPad p;
    memset(&p, 0, sizeof p);
    pad_from_buttons(&p, &in->keyboard);
    pad_turbo(&p, &in->keyboard, h->turbo_phase);
    for (int k = 0; k < in->pads; k++) {
        pad_from_buttons(&p, &in->pad[k]);
        pad_turbo(&p, &in->pad[k], h->turbo_phase);
    }
    return pad_bits(&p);
}

/* (Session 56) The frame with a notice over it. (Session 63) Shown
 * aside: not one of the game's frames for the pace. */
static void present_notice(Host *h, HzGame *g, const char *line1,
                           const char *line2)
{
    memcpy(h->composed, h->idx, sizeof h->composed);
    sysmenu_draw_notice(&h->menu, h->composed, line1, line2);
    platform_show(h->platform, h->composed, g->video->colours, h->menu.colours);
}

/* (Session 66) The Esc screen in a network game: a tick a pump beside
 * the game, which runs on, so the rival never waits for it; our side gets
 * no buttons while it is open. A pump asking for it opens it. HZ_NETLOG
 * has a line as it opens and closes (M FRAME open|shut). False: QUIT. */
static bool net_menu(Host *h, HzGame *g, const Input *in)
{
    bool was = h->menu.open;
    if (was) {
        if (!menu_step(h, g, in)) {
            return false;
        }
    } else if (in->menu) {
        menu_begin(h, in);
    }
    if (h->netlog != NULL && h->menu.open != was) {
        fprintf(h->netlog, "M %u %s\n", (unsigned)h->net.frame,
                h->menu.open ? "open" : "shut");
    }
    return true;
}

/* (Session 56) Until the rival's pad for this frame is in: the network
 * polled, ours sent again, the window kept alive (a pump a frame's time,
 * and after a moment WAITING FOR RIVAL over the frame), the Esc screen
 * ticked (session 66: net_menu, drawn in the notice's place). False when
 * the game can't go on (h->net_stop says why, or the program is to quit).
 * (Session 63) The time waited goes to the pace as not owed. */
static bool net_wait_rival(Host *h, HzGame *g)
{
    NetClient *c = &h->net;
    u64 began = net_now_ms();
    u64 pumped = began;
    bool counted = false;
    double held_from = 0.0;
    for (;;) {
        net_client_poll(c);
        NetGameStop stop = net_game_stopped(c);
        if (stop != NETG_ON) {
            h->net_stop = stop;
            return false;
        }
        if (net_game_ready(c)) {
            if (counted) {
                platform_pace_held(h->platform,
                                   platform_now(h->platform) - held_from);
            }
            return true;
        }
        if (!counted) {
            c->stalls++;
            counted = true;
            held_from = platform_now(h->platform);
        }
        u64 now = net_now_ms();
        if (now - c->input_sent >= 16) {
            net_game_send(c);
        }
        if (now - pumped >= 16) {
            pumped = now;
            if (h->menu.open) {
                menu_compose(h);
                platform_show(h->platform, h->composed, g->video->colours,
                              h->menu.colours);
            } else if (now - began >= 250) {
                present_notice(h, g, "WAITING FOR RIVAL", c->rival);
            }
            Input in;
            if (!platform_pump(h->platform, &in)) {
                h->quit_program = true;
                return false;
            }
            if (!net_menu(h, g, &in)) {
                h->quit_program = true;
                return false;
            }
            if (g->quit) {
                return false;   /* DISCONNECT on the menu */
            }
        }
        net_wait(c->sock, 1);
    }
}

/* (Session 56) The rumble in a network game: our side's, on every
 * gamepad here. */
static void net_rumble(Host *h, HzGame *g, int pads)
{
    u32 sfx = g->sfx_asked;
    g->sfx_asked = 0;
    rumble_watch(&h->rumble, g, sfx);
    if (h->menu.open) {
        return;         /* (session 66) the Esc screen's (rumble_menu) */
    }
    RumbleMotors m = rumble_scale(rumble_motors(&h->rumble, h->net.side),
                                  h->menu.rumble);
    for (int k = 0; k < PLATFORM_PADS; k++) {
        bool on = k < pads;
        platform_rumble(h->platform, k, on ? m.low : 0, on ? m.high : 0);
    }
}

/* (Session 56) on_frame in a network game: the hash every
 * NET_HASH_EVERY frames (the state as this frame's logic left it, the
 * same moment on both machines), the frame shown, our pad for `delay`
 * frames on sent, the rival's for this one waited for, both set.
 * (Session 66) The Esc screen over the frame while it is open, ticked a
 * frame at a time (net_menu): the game goes on, our pad none. */
static void net_frame(Host *h, HzGame *g)
{
    NetClient *c = &h->net;
    if (h->net_frames > 0 && c->frame >= (u32)h->net_frames) {
        h->quit_program = true;         /* HZ_NET_FRAMES: a check's end */
        g->quit = true;
        return;
    }
    if (h->net_end_at > 0 && c->frame == (u32)h->net_end_at) {
        /* (Session 66) HZ_NET_END: a check's game over, the same frame on
         * both machines (each has it) */
        for (int p = 0; p < 2; p++) {
            if (h->net_end_who & (1 << p)) {
                post_set_hq_hp(&g->posts[HZ_POST_HQ + p], 0);
            }
        }
    }
    if (c->frame % NET_HASH_EVERY == 0) {
        u64 hash = state_hash(g);
        net_game_hash(c, c->frame, hash);
        if (h->netlog != NULL) {
            fprintf(h->netlog, "H %u %016llX\n", (unsigned)c->frame,
                    (unsigned long long)hash);
        }
    }
    if (h->menu.open) {
        menu_compose(h);
        platform_present(h->platform, h->composed, g->video->colours,
                         h->menu.colours);
    } else {
        platform_present(h->platform, h->idx, g->video->colours, NULL);
    }
    Input in;
    if (!platform_pump(h->platform, &in)) {
        h->quit_program = true;
        g->quit = true;
        return;
    }
    if (h->net_esc_for > 0 && (c->frame == (u32)h->net_esc_at
                               || c->frame == (u32)(h->net_esc_at
                                                    + h->net_esc_for))) {
        in.menu = true;         /* HZ_NET_ESC: a check's Escape */
    }
    if (!net_menu(h, g, &in)) {
        h->quit_program = true;
        g->quit = true;
        return;
    }
    if (g->quit) {
        return;                 /* DISCONNECT on the menu */
    }
    swallow(h, &in);
    h->turbo_phase = !h->turbo_phase;
    net_game_push(c, h->menu.open ? 0 : net_local_pad(h, &in));
    if (!net_wait_rival(h, g)) {
        g->quit = true;
        return;
    }
    u8 pads[2];
    net_game_take(c, pads);
    pad_from_bits(&g->pad_in[0], pads[0]);
    pad_from_bits(&g->pad_in[1], pads[1]);
    replay_frame(h, g);         /* (session 59) */
    net_rumble(h, g, in.pads);
    platform_pace_slow(h->platform, net_game_slow(c) / PACE_HZ);
}

static void on_frame(HzGame *g, void *user)
{
    Host *h = user;
    if (g->quit) {
        return;
    }
    video_compose(g->video, h->idx);
    if (h->net_game) {
        net_frame(h, g);        /* (session 56) */
        return;
    }
    platform_present(h->platform, h->idx, g->video->colours, NULL);
    Input in;
    if (!platform_pump(h->platform, &in)) {
        h->quit_program = true;
        g->quit = true;
        return;
    }
    if (in.menu && !run_menu(h, g, &in)) {
        h->quit_program = true;
        g->quit = true;
        return;
    }
    net_tick(h, g);             /* (session 56) a START quits to main */
    if (g->quit) {
        return;
    }
    swallow(h, &in);
    h->turbo_phase = !h->turbo_phase;
    route_input(g, &in, g->pad_in, h->turbo_phase);
    replay_frame(h, g);         /* (session 59) */
    rumble_frame(h, g, in.pads);
}

/* Title_WaitStart's question, the driver's z_music_on: the host's sound,
 * or (no sound) always playing, so the title waits for Start. */
static bool music_on(void *user)
{
    Host *h = user;
    return h->audio == NULL || hz_audio_music_on(h->audio);
}

/* (Session 56) In a network game the host's sound mustn't decide
 * anything: always playing, on both machines. */
static bool net_music_on(void *user)
{
    (void)user;
    return true;
}

/* The game's hooks into the host: main's, and run_net_game's again after
 * its game_init. (Session 80, docs/hackable.md step 2) Not the frame: the
 * host runs the game a frame at a time (game/tick.h), each frame it makes
 * on_frame's. */
static void game_hooks(Host *h, HzGame *g, bool net)
{
    g->snd_music_on = net ? net_music_on : music_on;
    g->snd_user = h;
    g->snd_post = (h->audio != NULL) ? post_sound : NULL;
}

/* (Session 66) After a network game: the NETWORK page, not the title,
 * over the game's last frame. A game that ended as games do leaves both
 * in its room (net_game_after): HOST GAME is REMATCH there, the rival's
 * wish under it, and when both have asked the server's START closes the
 * page for the next (net_tick: main's loop starts it). Closed, the room
 * is left (the rival hears it) and the title comes back, still online.
 * HZ_NET_REMATCH=1 asks at once and leaves the page to itself (for the
 * checks: their pads would work it). */
static void net_after_page(Host *h, HzGame *g)
{
    Input in;
    if (!platform_pump(h->platform, &in)) {
        h->quit_program = true;
        return;
    }
    menu_begin(h, &in);         /* what is held now isn't a press */
    h->menu.page = SYSMENU_PAGE_NETWORK;
    h->menu.net_cursor = SYSMENU_NET_HOST;
    if (h->net_rematch) {
        net_client_rematch(&h->net, true);
    }
    while (h->menu.open) {
        menu_compose(h);
        platform_present(h->platform, h->composed, g->video->colours,
                         h->menu.colours);
        if (!platform_pump(h->platform, &in)) {
            h->quit_program = true;
            break;
        }
        if (h->net_rematch) {
            memset(&in, 0, sizeof in);  /* the check's pads leave it be */
        }
        if (!menu_step(h, g, &in)) {
            h->quit_program = true;
            break;
        }
    }
    if (h->menu.open) {
        h->menu.open = false;
        menu_end(h, &in);
    }
    if (h->net.state == NETC_AFTER) {
        net_client_cancel(&h->net);
    }
}

/* (Session 56) The network game, from a START: a cold boot as main's,
 * the same on both machines -- two players on the split screen, the
 * host's map, difficulty and PATCHES, the server's seed for the random
 * numbers (Game_Start takes v_rng_seed_saved) -- then the load, the game
 * and its end, every frame in lockstep (net_frame). Ended early (the
 * rival gone, the games split), why is shown a moment. Then (session 66)
 * the NETWORK page (net_after_page), and from it the title, still online,
 * or the rematch. (Session 59) With the host's view OWN SIDE ONLY (or the
 * server's rule) each machine shows its own side, wide (HzGame.own_view):
 * the same game, drawn apart. (Session 66) Each machine knows its side
 * (HzGame.net_player) for its own end, YOU WIN or YOU LOSE, and both
 * HQs down in the same second is a draw (HZ_FIX_DRAW). */
/* (Session 81) What MainLoop_Title (the game's, game_begin_main) tells
 * the host. A game set up: here, a new game's rumble (session 54) and who
 * plays the CPU's side (session 57: the Esc screen's CPU as this game
 * starts; session 58: the nightmare AI for the NETWORK page's NIGHTMARE),
 * which run_net_game set before its setup; then its recording begun
 * (session 59). The frame loop over: the recording saved, and a network
 * game's winner logged (session 66: P1, P2 or DRAW). */
static void on_event(HzGame *g, void *user)
{
    Host *h = user;
    switch ((HzEvent)g->event) {
        case HZ_EVENT_GAME: {
            if (h->net_game) {
                replay_start(h, g, "network", h->net.side, h->net.rival);
                break;
            }
            rumble_reset(&h->rumble);
            g->sfx_asked = 0;
            g->cpu_brain[0] = h->menu.cpu_brain;
            g->cpu_brain[1] = h->menu.cpu_brain;
            if (h->nightmare_game) {
                g->cpu_brain[0] = HZ_BRAIN_NIGHTMARE;
                g->cpu_brain[1] = HZ_BRAIN_NIGHTMARE;
                h->nightmare_game = false;
            }
            replay_start(h, g, "local", -1, NULL);
            break;
        }
        case HZ_EVENT_ENDED: {
            HzLoopExit exit = (HzLoopExit)g->frame_exit;
            replay_stop(h, g, exit);
            if (h->net_game && exit == HZ_LOOP_GAME_OVER
                && h->netlog != NULL) {
                fprintf(h->netlog, "R %u %s\n", (unsigned)h->net.frame,
                        g->winner == HZ_DRAW ? "DRAW"
                        : g->winner != 0 ? "P2" : "P1");
            }
            break;
        }
        default: {
            break;
        }
    }
}

static void run_net_game(Host *h, HzGame *g, Video *video,
                         const HzAssets *assets)
{
    NetClient *c = &h->net;
    video_init(video);
    game_init(g, assets, video);
    game_hooks(h, g, true);
    g->hw_version = 0xA0;
    g->options[0] = 5;
    g->options[2] = 0xFF;
    snd_init(g);
    g->options[0] = 0;                  /* two players, the split screen */
    g->options[1] = c->settings.difficulty;
    g->options[3] = c->settings.map;
    g->fixes = c->settings.fixes | HZ_FIX_DRAW;     /* (session 66) */
    g->own_view = c->settings.view ? (u8)(c->side + 1) : 0;
    g->net_player = (u8)(c->side + 1);              /* (session 66) */
    g->rng_seed_saved = c->seed;
    net_game_begin(c, h->menu.net_delay);
    fprintf(stderr, "network: playing %s%s as P%d on map %d, delay %d "
                    "frames%s\n", c->rival,
            c->rival_ai ? " (the server's AI)" : "", c->side + 1,
            c->settings.map, c->delay,
            g->own_view ? ", each side its own screen" : "");
    h->net_game = true;
    h->net_stop = NETG_ON;
    rumble_reset(&h->rumble);
    g->sfx_asked = 0;
    g->cpu_brain[0] = HZ_BRAIN_ORIGINAL;    /* (session 57) no CPU here */
    g->cpu_brain[1] = HZ_BRAIN_ORIGINAL;
    /* MainLoop_Title once round, no title: the setup, the game, its end
     * (on_event) */
    tick_main(g, HZ_MAIN_NO_TITLE | HZ_MAIN_ONCE, on_frame, on_event, h);
    h->net_game = false;
    if (h->menu.open) {
        /* (Session 66) Open as the game ended: closed, for the title's
         * frames, which don't tick it (on_frame's waits in run_menu). */
        Input none;
        memset(&none, 0, sizeof none);
        h->menu.open = false;
        menu_end(h, &none);
    }
    rumble_stop(h);
    fprintf(stderr, "network: the game ended at frame %u%s%s (%u waits; "
            "ahead of the rival %.2f frames at most, %.2f at the end, held "
            "back %.2f; delay %d, the rival's %d)\n", (unsigned)c->frame,
            h->net_stop != NETG_ON ? ": " : "",
            net_game_stop_text(h->net_stop), (unsigned)c->stalls,
            c->lead_max, net_game_lead(c), c->slowed, c->delay,
            c->rival_delay);
    if (h->netlog != NULL) {
        fprintf(h->netlog, "E %u %s\n", (unsigned)c->frame,
                h->net_stop != NETG_ON ? net_game_stop_text(h->net_stop)
                                       : "-");
        fflush(h->netlog);
    }
    if (h->net_stop != NETG_ON && !h->quit_program) {
        /* Why, two seconds over the last frame. */
        u64 until = net_now_ms() + 2000;
        while (net_now_ms() < until) {
            present_notice(h, g, net_game_stop_text(h->net_stop), "");
            net_sleep_ms(16);           /* vsync paces it too, if any */
            Input in;
            if (!platform_pump(h->platform, &in)) {
                h->quit_program = true;
                break;
            }
            net_client_poll(c);
        }
    }
    net_game_after(c);          /* (session 66) a rematch may follow */
    game_hooks(h, g, false);
    g->own_view = 0;            /* (session 59) the next game here: the
                                   mode's views */
    g->net_player = 0;
    h->net_games++;
    if (h->net_frames > 0 || (h->net_games_max > 0
                              && h->net_games >= h->net_games_max)) {
        h->quit_program = true;         /* HZ_NET_FRAMES, HZ_NET_GAMES:
                                           a check's game */
    }
    if (!h->quit_program) {
        net_after_page(h, g);   /* (session 66) */
    } else if (c->state == NETC_AFTER) {
        net_client_cancel(c);
    }
    /* A START or the NIGHTMARE row on the page: main's loop starts it. */
    g->quit = h->quit_program || h->net_start || h->nightmare_start;
}

static const char MODS_README[] =
    "Herzog Zwei's mods folder.\n"
    "\n"
    "A file here is read in place of the game's own. Copy it from the\n"
    "game's assets/ folder to the same place here and edit the copy:\n"
    "\n"
    "    assets/sound/songs/03_there_is_no_time_to_lose.txt\n"
    "    -> mods/sound/songs/03_there_is_no_time_to_lose.txt\n"
    "\n"
    "The game reads the copy at its next start and says so on its\n"
    "console (\"mods: mods/sound/songs/... in place of the game's own\").\n"
    "Deleting the copy undoes it. A file the game doesn't read is\n"
    "ignored; a copy of sound/sounds.txt can name song files of its own,\n"
    "kept here.\n"
    "\n"
    "When the game refuses a mod (a word the song text doesn't know, a\n"
    "PNG it can't read, JSON that doesn't parse, a value out of range),\n"
    "it says why, by file and line where it can, sets the mod aside and\n"
    "reads its own file instead. The mod stays here to be fixed.\n"
    "\n"
    "The formats: docs/sound.md (the songs and effects) and\n"
    "assets/README.md (the rest), in the game's source.\n";

/* mods/README.txt, when it's missing (the folder made first). */
static void mods_readme(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/README.txt", dir);
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return;
    }
    make_dir(dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "mods: can't write %s\n", path);
        return;
    }
    fputs(MODS_README, f);
    fclose(f);
}

static int usage(void)
{
    fprintf(stderr,
            "usage: hz [--assets DIR] [--mods DIR] [--map 0-7] [--mode 0-7] "
            "[--difficulty 0-3]\n"
            "          [--cpu original|nightmare]\n"
            "          [--ending p1|p2]\n"
            "          [--scale N] [--aspect square|4:3|tv] [--fullscreen]\n"
            "          [--server HOST[:PORT]] [--name NAME] "
            "[--net quick|host|nightmare]\n"
            "          [--view split|own] [IMAGE]\n"
            "  assets: the game's files, which it makes itself the first "
            "time it starts,\n"
            "          from your cartridge image (default: assets)\n"
            "  IMAGE: that image (.bin, .md, .gen, .smd, or a .zip holding "
            "one), used at once\n"
            "         on the first start; without it, the game asks for "
            "it\n"
            "  mods: files read in place of assets/'s (default: mods, or "
            "HZ_MODS;\n"
            "        \"\" none; its README.txt says how)\n"
            "  with none of map, mode, difficulty: the title and the "
            "options screen first\n"
            "  map: 0 ABGRUND, 1 VULKAN, 2 LOCH, 3 STRAND, 4 STADT,\n"
            "       5 EISFREI, 6 WALDUNG, 7 OASE\n"
            "  mode: b0 P2 is the CPU, b1 P1 is the CPU, b2 wide screen\n"
            "        (0 two players, 1 split vs CPU, 5 wide vs CPU)\n"
            "  difficulty: 0-3 = TYPE A-D\n"
            "  cpu: who plays the CPU's side this run (the Esc screen's "
            "CPU)\n"
            "  ending: the endings first, as after winning every map on "
            "every\n"
            "          difficulty as P1 (the still life) or P2 (the grave)\n"
            "  keys: arrows or WASD, Z/X/C or L/;/' = A/B/C, Enter = Start\n"
            "        (CONTROLS on the Esc screen changes them);\n"
            "        Escape: the Esc screen (settings, QUIT); F11 "
            "fullscreen, F10 aspect\n"
            "  server: a network game server, chosen on the Esc screen's "
            "NETWORK\n"
            "          page (port %d unless given); name: yours there\n"
            "  net: online at start, then a quick match, or host and wait,\n"
            "       or the nightmare AI here while waiting for people\n"
            "  view: a network game hosted here on the split screen, or "
            "each side\n"
            "        on its own (the NETWORK page's VIEW; a server may rule "
            "it)\n"
            "  settings: %s\n", NET_PORT_DEFAULT, sysmenu_settings_path());
    return 2;
}

int main(int argc, char **argv)
{
    platform_startup();
    const char *assets_dir = "assets";
    bool assets_given = false;
    const char *mods_dir = getenv("HZ_MODS");
    if (mods_dir == NULL) {
        mods_dir = "mods";
    }
    int map = -1;                   /* -1: as the options leave it */
    int mode = -1;
    int difficulty = -1;
    int scale = 0;
    int aspect = -1;                /* -1: the settings file's */
    bool fullscreen = false;
    int ending = 0;                 /* --ending: 1 P1's, 2 P2's */
    int cpu_brain = -1;             /* (session 57) --cpu: -1 the settings' */
    const char *server = NULL;      /* (session 56) --server, --name, --net */
    const char *net_name = NULL;
    int net_auto = 0;
    int net_view = -1;              /* (session 59) --view: -1 the settings' */
    /* (Session 78) The arguments that are no option: a cartridge image
     * dropped onto the program, for the first start (platform.h). */
    char **files = malloc(sizeof *files * (size_t)argc);
    int nfiles = 0;
    if (files == NULL) {
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fullscreen") == 0) {
            fullscreen = true;
            continue;
        }
        if (strncmp(argv[i], "--", 2) != 0) {
            files[nfiles++] = argv[i];
            continue;
        }
        if (i + 1 >= argc) {
            return usage();
        }
        const char *arg = argv[++i];
        if (strcmp(argv[i - 1], "--assets") == 0) {
            assets_dir = arg;
            assets_given = true;
        } else if (strcmp(argv[i - 1], "--mods") == 0) {
            mods_dir = arg;
        } else if (strcmp(argv[i - 1], "--map") == 0) {
            map = atoi(arg);
        } else if (strcmp(argv[i - 1], "--mode") == 0) {
            mode = atoi(arg);
        } else if (strcmp(argv[i - 1], "--difficulty") == 0) {
            difficulty = atoi(arg);
        } else if (strcmp(argv[i - 1], "--cpu") == 0) {
            if (strcmp(arg, "original") == 0) {
                cpu_brain = HZ_BRAIN_ORIGINAL;
            } else if (strcmp(arg, "nightmare") == 0) {
                cpu_brain = HZ_BRAIN_NIGHTMARE;
            } else {
                return usage();
            }
        } else if (strcmp(argv[i - 1], "--ending") == 0) {
            if (strcmp(arg, "p1") == 0) {
                ending = 1;
            } else if (strcmp(arg, "p2") == 0) {
                ending = 2;
            } else {
                return usage();
            }
        } else if (strcmp(argv[i - 1], "--server") == 0) {
            server = arg;
        } else if (strcmp(argv[i - 1], "--name") == 0) {
            net_name = arg;
        } else if (strcmp(argv[i - 1], "--net") == 0) {
            if (strcmp(arg, "quick") == 0) {
                net_auto = 1;
            } else if (strcmp(arg, "host") == 0) {
                net_auto = 2;
            } else if (strcmp(arg, "nightmare") == 0) {
                net_auto = 3;
            } else {
                return usage();
            }
        } else if (strcmp(argv[i - 1], "--view") == 0) {
            if (strcmp(arg, "split") == 0) {
                net_view = 0;
            } else if (strcmp(arg, "own") == 0) {
                net_view = 1;
            } else {
                return usage();
            }
        } else if (strcmp(argv[i - 1], "--scale") == 0) {
            scale = atoi(arg);
        } else if (strcmp(argv[i - 1], "--aspect") == 0) {
            if (strcmp(arg, "square") == 0) {
                aspect = ASPECT_SQUARE;
            } else if (strcmp(arg, "4:3") == 0) {
                aspect = ASPECT_4_3;
            } else if (strcmp(arg, "tv") == 0) {
                aspect = ASPECT_TV;
            } else {
                return usage();
            }
        } else {
            return usage();
        }
    }
    if (map >= HZ_MAPS || mode > 7 || difficulty > 3 || scale < 0) {
        return usage();
    }
    bool skip_title = map >= 0 || mode >= 0 || difficulty >= 0;

    if (!assets_given) {
        platform_find_home(assets_dir);
    }
    /* (Session 78) The first start (docs/first_start.md): the game's files
     * not made yet, so they are made now, from the player's own cartridge
     * -- an image named on the command line (dropped onto the program), or
     * one the platform's window asks for. A folder named by --assets is
     * taken as it is. */
    char made[1024];
    snprintf(made, sizeof made, "%s/%s", assets_dir, HZ_MADE_FILE);
    FILE *made_fh = assets_given ? NULL : fopen(made, "rb");
    if (made_fh != NULL) {
        fclose(made_fh);
        if (nfiles > 0) {
            fprintf(stderr, "hz: the game's files are made already, so %s "
                            "is not used (to make them again, delete %s "
                            "first)\n", files[0], made);
        }
    } else if (!assets_given
               && !platform_make_assets(assets_dir, nfiles, files)) {
        free(files);
        return 1;
    }
    if (assets_given && nfiles > 0) {
        fprintf(stderr, "hz: %s: the game reads its files from %s/, not "
                        "from a cartridge image; not used\n", files[0],
                assets_dir);
    }
    free(files);
    hz_mods_set(assets_dir, mods_dir);
    if (hz_mods_dir() != NULL) {
        mods_readme(hz_mods_dir());
    }
    static HzAssets assets;
    hz_file_hash_begin();           /* (session 56) what a rival must have */
    bool loaded = hz_assets_load(&assets, assets_dir);
    u64 assets_hash = hz_file_hash_end();
    if (!loaded) {
        char text[2200];        /* the folder, and made: up to 1023 */
        /* (Session 78) the files are the game's own make: to make them
         * again, the mark that they are made goes (docs/first_start.md) */
        snprintf(text, sizeof text, "hz: can't load the game's files from "
                 "%s/. To make them again from your cartridge image, "
                 "delete %s and start the game again.", assets_dir, made);
        platform_alert(text);
        return 1;
    }
    if (hz_mods_dir() != NULL) {
        fprintf(stderr, "mods: %s, %d in use\n", hz_mods_dir(),
                hz_mods_count());
        for (int i = 0; i < hz_mods_count(); i++) {
            fprintf(stderr, "mods: %s in place of the game's own\n",
                    hz_mods_name(i));
        }
    }

    static Host host;
    host.platform = platform_create("Herzog Zwei", scale);
    if (host.platform == NULL) {
        return 1;
    }

    /* The Esc screen's settings: the file's, then the command line's for
     * this run (written to the file only if the menu changes something). */
    sysmenu_init(&host.menu, &assets);
    sysmenu_load_settings(&host.menu);
    if (cpu_brain >= 0) {
        host.menu.cpu_brain = (u8)cpu_brain;    /* (session 57) this run */
    }
    if (aspect >= 0) {
        host.menu.display = (AspectMode)aspect;
    }
    if (fullscreen) {
        host.menu.fullscreen = true;
    }
    /* (Session 56) The network: the servers the game knows (servers.txt
     * beside it, --server), the name (made up once if none). */
    net_client_init(&host.net);
    host.game_id = net_game_id(assets_hash);
    sysmenu_load_servers_file(&host.menu, "servers.txt");
    sysmenu_pick_server(&host.menu);
    if (server != NULL) {
        /* (Session 57) Read as the NETWORK page's ADDRESS is. */
        char name[SYSMENU_NET_HOST_LEN + 1];
        int port;
        int at = -1;
        if (sysmenu_parse_address(server, name, sizeof name, &port)) {
            at = sysmenu_add_server(&host.menu, NULL, name, port,
                                    SYSMENU_SERVER_COMMAND);
        }
        if (at < 0) {
            return usage();
        }
        host.menu.server = at;
    }
    if (net_name != NULL) {
        net_name_clean(net_name, host.menu.net_name);
    }
    if (map >= 0) {
        host.menu.net_map = map;        /* hosted too, for this run */
    }
    if (net_view >= 0) {
        host.menu.net_own_view = net_view != 0;     /* (session 59) */
    }
    if (host.menu.net_name[0] == '\0') {
        /* Made up, and kept (in the settings) once it's first used. */
        snprintf(host.menu.net_name, sizeof host.menu.net_name,
                 "PLAYER %03u", (unsigned)(net_random32() % 1000));
        host.name_unsaved = true;
    }
    const char *replays = getenv("HZ_REPLAYS");     /* (session 59) */
    host.replays_off = replays != NULL && strcmp(replays, "0") == 0;
    const char *netlog = getenv("HZ_NETLOG");
    if (netlog != NULL && netlog[0] != '\0') {
        host.netlog = fopen(netlog, "wb");
    }
    const char *net_frames = getenv("HZ_NET_FRAMES");
    if (net_frames != NULL) {
        host.net_frames = atoi(net_frames);
    }
    const char *net_esc = getenv("HZ_NET_ESC");     /* (session 66) */
    if (net_esc != NULL
        && sscanf(net_esc, "%d:%d", &host.net_esc_at,
                  &host.net_esc_for) != 2) {
        host.net_esc_for = 0;
    }
    const char *net_end = getenv("HZ_NET_END");     /* (session 66) */
    if (net_end != NULL
        && sscanf(net_end, "%d:%d", &host.net_end_at,
                  &host.net_end_who) != 2) {
        host.net_end_at = 0;
    }
    const char *net_rematch = getenv("HZ_NET_REMATCH");
    host.net_rematch = net_rematch != NULL && strcmp(net_rematch, "1") == 0;
    const char *net_games = getenv("HZ_NET_GAMES");
    if (net_games != NULL) {
        host.net_games_max = atoi(net_games);
    }
    if (net_auto != 0) {
        host.net_auto = net_auto;
        net_connect(&host);
    }

    platform_set_aspect(host.platform, host.menu.display);
    platform_set_controls(host.platform, &host.menu.controls);
    platform_set_crt(host.platform, &host.menu.crt);
    if (host.menu.fullscreen) {
        platform_set_fullscreen(host.platform, true);
    }

    /* The Z80's side: the driver loaded and released (Snd_Init's copy and
     * reset), its sound pulled by the platform. */
    HzAudio *audio = hz_audio_create(assets.sound_data, AUDIO_RATE);
    if (audio == NULL) {
        fprintf(stderr, "hz: no memory for the sound; going on silent\n");
    } else {
        hz_audio_set_song_windows(audio, assets.song_windows,
                                  assets.song_window_count);
        hz_audio_set_volume(audio, host.menu.audio.master_db,
                            host.menu.audio.music_db,
                            host.menu.audio.effects_db);
        hz_audio_set_synth(audio, host.menu.audio.synth);
        hz_audio_set_voices(audio, host.menu.audio.voices);
        hz_audio_set_repeats(audio, host.menu.audio.repeats);
        hz_audio_set_oversample(audio, host.menu.audio.oversample);
        hz_audio_set_drums(audio, assets.drum_models,
                           assets.drum_model_count, assets.drum_uses,
                           assets.drum_use_count);
        hz_audio_set_voice_db(audio, assets.drum_voice_db);
    }
    host.audio = audio;
    platform_attach_audio(host.platform, audio);

    static Video video;
    static HzGame game;
    video_init(&video);
    game_init(&game, &assets, &video);
    game_hooks(&host, &game, false);
    game.fixes = host.menu.fixes;       /* the settings' (default on) */
    /* EntryPoint and Boot_Continue: an overseas machine's version byte
     * (NTSC, no expansion unit); a cold boot's v_options (mode 5, wide
     * screen against the CPU) and v_option2 ($FF); Snd_Init. */
    game.hw_version = 0xA0;
    game.options[0] = 5;
    game.options[2] = 0xFF;
    snd_init(&game);
    if (mode >= 0) {
        game.options[0] = (u8)mode;
    }
    if (difficulty >= 0) {
        game.options[1] = (u8)difficulty;
    }
    if (map >= 0) {
        game.options[3] = (u8)map;
    }

    if (ending != 0) {
        tick_ending(&game, ending, on_frame, &host);
    }

    int rc = 0;
    for (;;) {                          /* (session 56) */
        /* MainLoop_Title (session 81: the game's) until a quit: the title
         * (with -1 in v_game_type when it ran out: the attract demo), the
         * setup (Start on a demo's loading screen: the title again), the
         * game, the finale, WINNER, the results, the win saved; then the
         * title. What main.c does between: on_event. */
        tick_main(&game, skip_title ? HZ_MAIN_NO_TITLE : 0, on_frame,
                  on_event, &host);
        skip_title = false;
        if (game.setup_result == HZ_SETUP_NO_MAP) {
            char text[64];
            snprintf(text, sizeof text, "hz: map %d doesn't load",
                     game.options[3] & 7);
            platform_alert(text);
            rc = 1;
        }
        /* (Session 56) Left for a network game: it, then the title
         * again. */
        if (host.net_start && !host.quit_program && rc == 0) {
            host.net_start = false;
            run_net_game(&host, &game, &video, &assets);
            continue;
        }
        /* (Session 58) Left for the NETWORK page's NIGHTMARE: a game
         * against it, as the CPU (P2), on the page's map; (session 59) on
         * the split screen, the AI's view beside ours (mode 1), unless
         * VIEW is OWN SIDE ONLY (wide, mode 5). The owner: "I mostly
         * want view split ... So I can observe the AI's behavior". */
        if (host.nightmare_start && !host.quit_program && rc == 0) {
            host.nightmare_start = false;
            host.nightmare_game = true;
            game.quit = false;
            game.options[0] = host.menu.net_own_view ? 5 : 1;
            game.options[3] = (u8)(host.menu.net_map & 7);
            skip_title = true;
            fprintf(stderr, "network: the nightmare AI here, on map %d%s%s\n",
                    game.options[3],
                    host.menu.net_own_view ? "" : ", the split screen",
                    host.net.state == NETC_OFFLINE
                    || host.net.state == NETC_FAILED
                    ? "" : "; still waiting for people online");
            continue;
        }
        break;
    }

    net_client_disconnect(&host.net);       /* (session 56) the rival told */
    if (host.netlog != NULL) {
        fclose(host.netlog);
    }
    platform_destroy(host.platform);        /* stops the sound first */
    hz_audio_destroy(audio);
    return rc;
}
