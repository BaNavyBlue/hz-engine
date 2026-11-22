#include "sysmenu.h"
#include "game.h"

#include "../core/json.h"

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

/* The window's frame: the HUD strip's tiles (assets/screens/hud_frame.txt
 * row 0: $5E0 its corner, $5E8 a plain stretch of its edge; rows 1-2:
 * $5EA its side, the second row v-flipped). */
#define TILE_CORNER 0x5E0
#define TILE_EDGE   0x5E8
#define TILE_SIDE   0x5EA
#define HUD_FIRST   0x500

/* FontTiles: tile n is ASCII $11 + n for '!' to '_', with the game's own
 * pictures where it needs no ASCII; the digits after '0' are yellow, so
 * the menu takes the font's second, cyan set (tiles 7-14 '1'-'8', 46 '9',
 * 47 '0'), as cyan as the letters. Tile 6 is a red arrow: the cursor. */
#define FONT_ARROW  6
#define FONT_FIRST  0x11

#define FLIP_H 1
#define FLIP_V 2

/* Overlay colour 0: black, the window's inside. */
#define FILL (OVERLAY_BASE + 0)

/* Cell layouts. The menu: no title, the rows two apart with a blank row
 * under the frame's top and over its bottom; a page: a title, a blank
 * row, then its rows the same way. Labels LABEL_COL cells into the
 * window, the arrow two in front of what it points at. */
#define SCREEN_COLS (SCREEN_W / 8)
#define SCREEN_ROWS (SCREEN_H / 8)
#define MENU_COLS   26          /* (session 57) NIGHTMARE, CONNECTING */
#define AUDIO_COLS  24
#define PAGE_COLS   36          /* CRT, HDR: NEEDS FULLSCREEN is 16 */
#define PAGE_PITCH  3           /* a row, its why under it, a fill line */
#define LABEL_COL   4
#define VALUE_COL   15
#define CTRL_COLS   30          /* (session 54) 38 with a PAD column */
#define CTRL_ROWS   27          /* the screen's height but a row */
#define CTRL_SLOT0  11          /* KEY 1's column; KEY 2 9 on */
#define CTRL_PITCH  9
#define BTN_COLS    36          /* (session 54) BUTTONS: a slot is 10 */
#define BTN_SLOT0   11
#define BTN_PITCH   12

#define SETTINGS_FILE "settings.json"

static char g_settings_path[512];

static const char *const DISPLAY_NAME[ASPECT_COUNT] = {
    "SQUARE", "4:3", "TV"
};
static const char *const DISPLAY_KEY[ASPECT_COUNT] = {
    "square", "4:3", "tv"
};
static const char *const ACTION_NAME[CONTROL_ACTIONS] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "C", "START", "TURBO"
};
/* The PATCHES page: a row per HZ_FIX_* bit, in bit order -- its bit
 * (HZ_FIX_DRAW's, 8, is none: PATHS has 9, REPAIR 10, PIXELS 11), name,
 * what it does, its key in settings.json's "patches". */
static const u32 PATCH_BIT[HZ_FIX_COUNT] = {
    HZ_FIX_SUPPLY, HZ_FIX_JET_GUN, HZ_FIX_UNIT_AIM, HZ_FIX_FIRE_RATE,
    HZ_FIX_GUARD_CHASE, HZ_FIX_CPU_HITS, HZ_FIX_TURRETS, HZ_FIX_UNITS,
    HZ_FIX_PATHS, HZ_FIX_REPAIR, HZ_FIX_PIXELS
};
static const char *const PATCH_NAME[HZ_FIX_COUNT] = {
    "SUPPLY", "JET GUNS", "UNIT AIM", "FIRE RATE", "GUARDS", "CPU HITS",
    "TURRETS", "UNITS", "PATHS", "REPAIR", "PIXELS"
};
static const char *const PATCH_WHAT[HZ_FIX_COUNT] = {
    "TRUCKS REACH UNITS IN GROUPS",
    "BULLETS ALWAYS CHECK THE JET",
    "UNITS FIRE BOTH SIDES OF UP",
    "HUMANS FIRE AS FAST AS CPUS",
    "GUARDS CHASE FROM EVERY SIDE",
    "ONLY THE UNIT HIT LOSES HP",
    "THE SHOP SELLS GUN TURRETS",
    "NO LIMIT ON UNITS PER SIDE",
    "UNITS FIND A WAY ROUND CLIFFS",
    "THE SHOP SELLS A REPAIR TRUCK",
    "ONLY DRAWN PIXELS COLLIDE"
};
static const char *const PATCH_KEY[HZ_FIX_COUNT] = {
    "supply", "jet_guns", "unit_aim", "fire_rate", "guard_chase", "cpu_hits",
    "turrets", "units", "paths", "repair", "pixels"
};
static const char *const ACTION_KEY[CONTROL_ACTIONS] = {
    "up", "down", "left", "right", "a", "b", "c", "start", "turbo"
};
/* (Session 56) NETWORK's MAP, as --map numbers them. */
static const char *const MAP_NAME[HZ_MAPS] = {
    "ABGRUND", "VULKAN", "LOCH", "STRAND", "STADT", "EISFREI", "WALDUNG",
    "OASE"
};

/* ---- settings ---- */

void sysmenu_set_settings_path(const char *path)
{
    if (path != NULL) {
        snprintf(g_settings_path, sizeof g_settings_path, "%s", path);
        return;
    }
    const char *dir = getenv("HZ_SAVES");
    snprintf(g_settings_path, sizeof g_settings_path, "%s/%s",
             (dir != NULL && dir[0] != '\0') ? dir : "saves", SETTINGS_FILE);
}

const char *sysmenu_settings_path(void)
{
    if (g_settings_path[0] == '\0') {
        sysmenu_set_settings_path(NULL);
    }
    return g_settings_path;
}

void sysmenu_init(SysMenu *m, const HzAssets *assets)
{
    memset(m, 0, sizeof *m);
    m->display = ASPECT_TV;
    m->fixes = HZ_FIX_DEFAULT;
    m->audio.voices = AUDIO_VOICES_EXTRA;   /* (session 38) PS2's default */
    m->audio.repeats = AUDIO_REPEATS_CHIP;  /* (session 42) the chip's: song
                                               5's bass loses 2-3 dB of its
                                               harmonics with FRESH */
    m->audio.oversample = 1;    /* (session 48) the chip's grain: song 6's
                                   parallel voices sit in the mix */
    controls_default(&m->controls);
    crt_settings_default(&m->crt);
    crt_display_default(&m->crt_display);
    m->rumble = RUMBLE_DEFAULT;             /* (session 54) */
    m->rumble_test = -1;
    m->cpu_brain = HZ_BRAIN_ORIGINAL;       /* (session 57) */
    /* (Session 56) NETWORK: a server on this machine (hz_server's own
     * port) always known; the name main.c's until the settings give one. */
    m->net_quick_ai = true;                 /* (session 58) */
    sysmenu_add_server(m, "THIS MACHINE", "127.0.0.1", 19890,
                       SYSMENU_SERVER_BUILTIN);
    m->font = assets->font_tiles;
    m->hud = assets->hud_tiles;
    /* GamePalettes line 1, the HUD's and the font's; colour 0 (never a
     * tile's pixel) black for the inside. */
    memcpy(m->colours, assets->game_palettes, sizeof m->colours);
    m->colours[0] = 0x000;
}

/* (Session 56) What a server's host may hold: letters, digits and . - :
 * [ ] _ (names, IPv4, IPv6). */
static bool host_ok(const char *host)
{
    if (host == NULL || host[0] == '\0'
        || strlen(host) > SYSMENU_NET_HOST_LEN) {
        return false;
    }
    for (const char *c = host; *c != '\0'; c++) {
        bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
                  || (*c >= '0' && *c <= '9') || *c == '.' || *c == '-'
                  || *c == ':' || *c == '[' || *c == ']' || *c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

int sysmenu_add_server(SysMenu *m, const char *name, const char *host,
                       int port, SysMenuServerOrigin origin)
{
    if (!host_ok(host) || port <= 0 || port > 65535) {
        fprintf(stderr, "network: server \"%s\" port %d skipped (a host "
                        "is letters, digits and . - : [ ] _)\n",
                host != NULL ? host : "", port);
        return -1;
    }
    for (int i = 0; i < m->server_count; i++) {
        if (strcmp(m->servers[i].host, host) == 0
            && m->servers[i].port == port) {
            return i;
        }
    }
    if (m->server_count == SYSMENU_NET_SERVERS) {
        return -1;
    }
    SysMenuServer *sv = &m->servers[m->server_count];
    memset(sv, 0, sizeof *sv);
    /* The name in the font's letters, as a player's. */
    const char *from = (name != NULL && name[0] != '\0') ? name : host;
    int n = 0;
    for (; *from != '\0' && n < SYSMENU_NET_NAME_LEN; from++) {
        char c = *from;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c == '_') {
            c = ' ';
        }
        bool keep = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '.' || c == ' ' || c == ':';
        sv->name[n++] = keep ? c : ' ';
    }
    snprintf(sv->host, sizeof sv->host, "%s", host);
    sv->port = port;
    sv->origin = origin;
    return m->server_count++;
}

bool sysmenu_parse_address(const char *text, char *host, size_t host_n,
                           int *port)
{
    char buf[SYSMENU_NET_ADDR_LEN + 1];
    if (strlen(text) >= sizeof buf) {
        return false;
    }
    snprintf(buf, sizeof buf, "%s", text);
    char *name = buf;
    const char *digits = NULL;
    char *close = strchr(buf, ']');
    char *colon = strchr(buf, ':');
    if (buf[0] == '[') {
        if (close == NULL || (close[1] != '\0' && close[1] != ':')) {
            return false;
        }
        if (close[1] == ':') {
            digits = close + 2;
        }
        *close = '\0';
        name = buf + 1;
    } else if (colon != NULL && strchr(colon + 1, ':') == NULL) {
        *colon = '\0';
        digits = colon + 1;
    }
    long p = 19890;
    if (digits != NULL) {
        size_t n = strlen(digits);
        if (n == 0 || n > 5 || strspn(digits, "0123456789") != n) {
            return false;
        }
        p = strtol(digits, NULL, 10);
    }
    if (p < 1 || p > 65535 || !host_ok(name) || strlen(name) >= host_n) {
        return false;
    }
    snprintf(host, host_n, "%s", name);
    *port = (int)p;
    return true;
}

/* (Session 57) ADDRESS's Enter: the typed address's server added (or the
 * one known already) and chosen. A full list makes room by forgetting the
 * oldest of the player's own servers but the chosen. NULL when it's done,
 * else what the row shows under it. */
static const char *use_address(SysMenu *m, const char *text)
{
    char host[SYSMENU_NET_HOST_LEN + 1];
    int port;
    if (!sysmenu_parse_address(text, host, sizeof host, &port)) {
        return "NOT AN ADDRESS";
    }
    int at = sysmenu_add_server(m, NULL, host, port,
                                SYSMENU_SERVER_SETTINGS);
    for (int i = 0; at < 0 && i < m->server_count; i++) {
        if (m->servers[i].origin != SYSMENU_SERVER_SETTINGS
            || i == m->server) {
            continue;
        }
        memmove(&m->servers[i], &m->servers[i + 1],
                (size_t)(m->server_count - i - 1) * sizeof m->servers[0]);
        m->server_count--;
        if (m->server > i) {
            m->server--;
        }
        at = sysmenu_add_server(m, NULL, host, port,
                                SYSMENU_SERVER_SETTINGS);
    }
    if (at < 0) {
        return "SERVER LIST FULL";
    }
    m->server = at;
    m->net_changed = true;
    return NULL;
}

void sysmenu_load_servers_file(SysMenu *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    char line[256];
    int line_no = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        line_no++;
        char name[64], host[128];
        int port = 19890;
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        int got = sscanf(line, "%63s %127s %d", name, host, &port);
        if (got <= 0) {
            continue;
        }
        if (got < 2) {
            fprintf(stderr, "network: %s line %d: NAME HOST [PORT]\n", path,
                    line_no);
            continue;
        }
        sysmenu_add_server(m, name, host, port, SYSMENU_SERVER_FILE);
    }
    fclose(f);
}

void sysmenu_pick_server(SysMenu *m)
{
    m->server = 0;
    for (int i = 0; i < m->server_count; i++) {
        char key[sizeof m->server_key];
        snprintf(key, sizeof key, "%s:%d", m->servers[i].host,
                 m->servers[i].port);
        if (strcmp(key, m->server_key) == 0) {
            m->server = i;
        }
    }
}

/* (PS2's) The CRT page's values: their names in the menu and their
 * values in settings.json. */
static const char *const MASK_NAME[CRT_MASK_COUNT] = {
    "OFF", "APERTURE GRILLE", "SLOT MASK", "SHADOW MASK",
};
static const char *const MASK_SHORT[CRT_MASK_COUNT] = {
    "OFF", "GRILLE", "SLOT MASK", "SHADOW MASK",
};
static const char *const MASK_KEY[CRT_MASK_COUNT] = {
    "off", "grille", "slot", "shadow",
};
static const char *const SUBPIXEL_NAME[CRT_SUBPIXELS_COUNT] = {
    "RGB", "BGR", "OTHER",
};
static const char *const SUBPIXEL_KEY[CRT_SUBPIXELS_COUNT] = {
    "rgb", "bgr", "other",
};

/* PEAK's stops, in nits; 0 is AUTO (crt_peak_auto). */
static const int PEAK_STOPS[] = {
    0, 400, 500, 600, 700, 800, 1000, 1200, 1400, 1600, 2000, 2500, 3000,
    4000, 10000,
};
#define PEAK_STOP_COUNT ((int)(sizeof PEAK_STOPS / sizeof PEAK_STOPS[0]))

/* WHITE's: 10 nits apart up to 400, 50 from there to 1000. */
#define WHITE_MIN 50
#define WHITE_MAX 1000

static int lookup_key(const char *value, const char *const *keys, int n,
                      int fallback)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(value, keys[i]) == 0) {
            return i;
        }
    }
    return fallback;
}

/* The file's "crt" block over the defaults: a key missing or a value not
 * known keeps what was there. */
static void load_crt(CrtSettings *c, const JsonValue *crt)
{
    c->scanlines = json_bool(crt, "scanlines", c->scanlines);
    /* (Session 57 (2)) Sessions 53 to 57 had "interlaced" too: scanlines
     * were what it drew, so ON ("flicker" is passed over). */
    if (strcmp(json_str(crt, "scanlines", ""), "interlaced") == 0) {
        c->scanlines = true;
    }
    c->mask = (CrtMask)lookup_key(json_str(crt, "mask", MASK_KEY[c->mask]),
                                  MASK_KEY, CRT_MASK_COUNT, c->mask);
    c->subpixels = (CrtSubpixels)lookup_key(
        json_str(crt, "subpixels", SUBPIXEL_KEY[c->subpixels]),
        SUBPIXEL_KEY, CRT_SUBPIXELS_COUNT, c->subpixels);
    c->hdr = json_bool(crt, "hdr", c->hdr);
    /* "peak": "auto" or a number of nits. */
    long peak = json_int(crt, "peak", -1);
    if (peak > 0) {
        c->peak_nits = (int)((peak > 10000) ? 10000 : peak);
    } else if (strcmp(json_str(crt, "peak", "auto"), "auto") == 0) {
        c->peak_nits = 0;
    }
    long white = json_int(crt, "white", c->white_nits);
    if (white < WHITE_MIN) {
        white = WHITE_MIN;
    } else if (white > WHITE_MAX) {
        white = WHITE_MAX;
    }
    c->white_nits = (int)white;
}

/* A volume from the file: a number of dB, clamped to the page's range
 * (below its bottom, OFF), or "off". Anything else keeps `fallback`. */
static int load_db(const JsonValue *audio, const char *key, int fallback)
{
    const JsonValue *v = json_get(audio, key);
    if (v == NULL) {
        return fallback;
    }
    if (v->type == JSON_STRING && strcmp(v->string, "off") == 0) {
        return AUDIO_DB_OFF;
    }
    if (v->type != JSON_NUMBER) {
        return fallback;
    }
    long db = json_int(audio, key, fallback);
    if (db > 0) {
        return 0;
    }
    return (db < SYSMENU_DB_MIN) ? AUDIO_DB_OFF : (int)db;
}

/* "keys": {"up": ["UP", "W"], ...}: a name per slot, "-" for none. A name
 * no key has keeps that slot's default. */
static void load_keys(Controls *c, const JsonValue *keys)
{
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        const JsonValue *list = json_get(keys, ACTION_KEY[a]);
        if (list == NULL || list->type != JSON_ARRAY) {
            continue;
        }
        for (int k = 0; k < CONTROL_KEYS; k++) {
            const JsonValue *name = json_at(list, k);
            if (name == NULL || name->type != JSON_STRING) {
                continue;
            }
            if (strcmp(name->string, "-") == 0) {
                c->key[a][k] = KEY_NONE;
                continue;
            }
            int code = key_from_name(name->string);
            if (code != KEY_NONE && key_bindable(code)) {
                c->key[a][k] = code;
            } else {
                fprintf(stderr, "settings: no key %s; %s keeps its own\n",
                        name->string, ACTION_KEY[a]);
            }
        }
    }
}

/* One of "pad"'s names into a slot: "-" empties it; a name no input has
 * keeps the slot's default. */
static void load_pad_slot(Controls *c, int action, int slot,
                          const JsonValue *name)
{
    if (name == NULL || name->type != JSON_STRING) {
        return;
    }
    if (strcmp(name->string, "-") == 0) {
        c->pad[action][slot] = PAD_NONE;
        return;
    }
    int input = pad_input_from_name(name->string);
    if (input != PAD_NONE && pad_input_bindable(input)) {
        c->pad[action][slot] = input;
    } else {
        fprintf(stderr, "settings: no gamepad input %s; %s keeps its own\n",
                name->string, ACTION_KEY[action]);
    }
}

/* (Session 54) "pad": {"up": ["DUP", "LSUP"], ...}: a positional name per
 * slot (core/keys.c), "-" for none. Session 26's "a": "WEST" (A to TURBO,
 * one button each) is the first slot and an empty second. */
static void load_pad(Controls *c, const JsonValue *pad)
{
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        const JsonValue *v = json_get(pad, ACTION_KEY[a]);
        if (v == NULL) {
            continue;
        }
        if (v->type == JSON_STRING) {
            load_pad_slot(c, a, 0, v);
            c->pad[a][1] = PAD_NONE;
            continue;
        }
        if (v->type != JSON_ARRAY) {
            continue;
        }
        for (int s = 0; s < CONTROL_PAD_SLOTS; s++) {
            load_pad_slot(c, a, s, json_at(v, s));
        }
    }
}

/* (Session 56) "net": the name, the map, the delay, the player's own
 * servers, and which server was chosen (resolved by sysmenu_pick_server
 * once every server is known). */
static void load_net(SysMenu *m, const JsonValue *net)
{
    const char *name = json_str(net, "name", "");
    if (name[0] != '\0') {
        snprintf(m->net_name, sizeof m->net_name, "%s", name);
    }
    long map = json_int(net, "map", m->net_map);
    m->net_map = (map >= 0 && map < HZ_MAPS) ? (int)map : 0;
    m->net_quick_ai = json_bool(net, "quick_ai", m->net_quick_ai);
    m->net_own_view = strcmp(json_str(net, "view", "split"), "own") == 0;
    long delay = json_int(net, "delay", 0);
    m->net_delay = (delay >= 0 && delay <= 15) ? (int)delay : 0;
    snprintf(m->server_key, sizeof m->server_key, "%s",
             json_str(net, "server", ""));
    const JsonValue *list = json_get(net, "servers");
    for (int i = 0; list != NULL && i < json_length(list); i++) {
        const JsonValue *e = json_at(list, i);
        long port = json_int(e, "port", 19890);
        const char *host = json_str(e, "host", "");
        if (host[0] == '\0' || port <= 0 || port > 65535) {
            fprintf(stderr, "settings: net.servers[%d] has no host or a "
                            "bad port; skipped\n", i);
            continue;
        }
        sysmenu_add_server(m, json_str(e, "name", ""), host, (int)port,
                           SYSMENU_SERVER_SETTINGS);
    }
}

bool sysmenu_load_settings(SysMenu *m)
{
    const char *path = sysmenu_settings_path();
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        fprintf(stderr, "settings: %s doesn't parse; using the defaults\n",
                path);
        return false;
    }
    const char *display = json_str(root, "display", DISPLAY_KEY[m->display]);
    for (int i = 0; i < ASPECT_COUNT; i++) {
        if (strcmp(display, DISPLAY_KEY[i]) == 0) {
            m->display = (AspectMode)i;
        }
    }
    m->fullscreen = json_bool(root, "fullscreen", m->fullscreen);
    /* Session 32's single switch, then the page's own (session 35). */
    const JsonValue *old = json_get(root, "fixes");
    if (old != NULL && old->type == JSON_BOOL) {
        m->fixes = json_bool(root, "fixes", true) ? HZ_FIX_DEFAULT : 0;
    }
    const JsonValue *patches = json_get(root, "patches");
    for (int i = 0; patches != NULL && i < HZ_FIX_COUNT; i++) {
        bool on = (m->fixes & PATCH_BIT[i]) != 0;
        if (json_bool(patches, PATCH_KEY[i], on)) {
            m->fixes |= PATCH_BIT[i];
        } else {
            m->fixes &= ~PATCH_BIT[i];
        }
    }
    const JsonValue *audio = json_get(root, "audio");
    if (audio != NULL) {
        m->audio.master_db = load_db(audio, "master", m->audio.master_db);
        m->audio.music_db = load_db(audio, "music", m->audio.music_db);
        m->audio.effects_db = load_db(audio, "effects", m->audio.effects_db);
    }
    /* (Session 36) SYNTH: "q" only where the build has Q. */
    const char *synth = json_str(root, "synth", "nuked");
    m->audio.synth = (strcmp(synth, "q") == 0 && hz_audio_has_q())
                   ? AUDIO_SYNTH_Q : AUDIO_SYNTH_NUKED;
    /* (Session 38) VOICES: "chip" or "extra" (anything else, or none: the
     * default) */
    const char *voices = json_str(root, "voices", "extra");
    m->audio.voices = (strcmp(voices, "chip") == 0) ? AUDIO_VOICES_CHIP
                                                    : AUDIO_VOICES_EXTRA;
    /* (Session 39) REPEATS: "chip" or "fresh" (anything else, or none: the
     * default, CHIP since session 42) */
    const char *repeats = json_str(root, "repeats", "chip");
    m->audio.repeats = (strcmp(repeats, "fresh") == 0) ? AUDIO_REPEATS_FRESH
                                                       : AUDIO_REPEATS_CHIP;
    /* (Session 48) OVERSAMPLE: 1, 2 or 4 (anything else, or none: 1) */
    long os = json_int(root, "oversample", 1);
    m->audio.oversample = (os == 2 || os == 4) ? (int)os : 1;
    /* (Session 54) RUMBLE: "off", "low", "medium" or "high" (anything
     * else, or none: what it was, MEDIUM by default) */
    m->rumble = rumble_level_from_key(json_str(root, "rumble", NULL),
                                      m->rumble);
    /* (Session 57) CPU: "nightmare" (anything else, or none: the
     * original's) */
    const char *cpu = json_str(root, "cpu", "original");
    m->cpu_brain = (strcmp(cpu, "nightmare") == 0) ? HZ_BRAIN_NIGHTMARE
                                                   : HZ_BRAIN_ORIGINAL;
    const JsonValue *crt = json_get(root, "crt");
    if (crt != NULL) {
        load_crt(&m->crt, crt);
    }
    const JsonValue *keys = json_get(root, "keys");
    if (keys != NULL) {
        load_keys(&m->controls, keys);
    }
    const JsonValue *pad = json_get(root, "pad");
    if (pad != NULL) {
        load_pad(&m->controls, pad);
    }
    const JsonValue *net = json_get(root, "net");
    if (net != NULL) {
        load_net(m, net);
    }
    json_free(root);
    return true;
}

static void db_json(int db, char *out, size_t n)
{
    if (db <= AUDIO_DB_OFF) {
        snprintf(out, n, "\"off\"");
    } else {
        snprintf(out, n, "%d", db);
    }
}

void sysmenu_save_settings(const SysMenu *m)
{
    const char *path = sysmenu_settings_path();
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        /* No saves/ yet: make the file's folder and try once more. */
        char dir[sizeof g_settings_path];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash != NULL) {
            *slash = '\0';
            make_dir(dir);
            f = fopen(path, "wb");
        }
    }
    if (f == NULL) {
        fprintf(stderr, "settings: can't write %s\n", path);
        return;
    }
    /* (Session 39) the patches, a line each, as many as HZ_FIX_COUNT */
    char patches[64 * HZ_FIX_COUNT];
    int at = 0;
    for (int i = 0; i < HZ_FIX_COUNT; i++) {
        at += snprintf(patches + at, sizeof patches - (size_t)at,
                       "    \"%s\": %s%s\n", PATCH_KEY[i],
                       (m->fixes & PATCH_BIT[i]) ? "true" : "false",
                       (i + 1 < HZ_FIX_COUNT) ? "," : "");
    }
    char master[16], music[16], effects[16];
    db_json(m->audio.master_db, master, sizeof master);
    db_json(m->audio.music_db, music, sizeof music);
    db_json(m->audio.effects_db, effects, sizeof effects);
    const CrtSettings *c = &m->crt;
    char peak[16];
    if (c->peak_nits > 0) {
        snprintf(peak, sizeof peak, "%d", c->peak_nits);
    } else {
        snprintf(peak, sizeof peak, "\"auto\"");
    }
    fprintf(f, "{\n  \"display\": \"%s\",\n  \"fullscreen\": %s,\n"
               "  \"patches\": {\n%s  },\n"
               "  \"synth\": \"%s\",\n"
               "  \"voices\": \"%s\",\n"
               "  \"repeats\": \"%s\",\n"
               "  \"oversample\": %d,\n"
               "  \"rumble\": \"%s\",\n"
               "  \"cpu\": \"%s\",\n"
               "  \"crt\": {\n    \"scanlines\": %s,\n    \"mask\": \"%s\",\n"
               "    \"subpixels\": \"%s\",\n    \"hdr\": %s,\n"
               "    \"peak\": %s,\n    \"white\": %d\n  },\n"
               "  \"audio\": {\n    \"master\": %s,\n    \"music\": %s,\n"
               "    \"effects\": %s\n  },\n  \"keys\": {\n",
            DISPLAY_KEY[m->display], m->fullscreen ? "true" : "false",
            patches,
            m->audio.synth == AUDIO_SYNTH_Q ? "q" : "nuked",
            m->audio.voices == AUDIO_VOICES_EXTRA ? "extra" : "chip",
            m->audio.repeats == AUDIO_REPEATS_CHIP ? "chip" : "fresh",
            m->audio.oversample, rumble_level_key(m->rumble),
            m->cpu_brain == HZ_BRAIN_NIGHTMARE ? "nightmare" : "original",
            c->scanlines ? "true" : "false",
            MASK_KEY[c->mask], SUBPIXEL_KEY[c->subpixels],
            c->hdr ? "true" : "false", peak, c->white_nits, master, music,
            effects);
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        fprintf(f, "    \"%s\": [", ACTION_KEY[a]);
        for (int k = 0; k < CONTROL_KEYS; k++) {
            char name[16];
            key_name(m->controls.key[a][k], name, sizeof name);
            fprintf(f, "%s\"%s\"", (k > 0) ? ", " : "", name);
        }
        fprintf(f, "]%s\n", (a + 1 < CONTROL_ACTIONS) ? "," : "");
    }
    fprintf(f, "  },\n  \"pad\": {\n");
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        fprintf(f, "    \"%s\": [", ACTION_KEY[a]);
        for (int s = 0; s < CONTROL_PAD_SLOTS; s++) {
            char name[16];
            pad_input_name(m->controls.pad[a][s], name, sizeof name);
            fprintf(f, "%s\"%s\"", (s > 0) ? ", " : "", name);
        }
        fprintf(f, "]%s\n", (a + 1 < CONTROL_ACTIONS) ? "," : "");
    }
    /* (Session 56) the names and hosts hold nothing JSON must escape:
     * sysmenu_add_server and the NAME row keep them to letters, digits
     * and a few marks. */
    const SysMenuServer *chosen = (m->server < m->server_count)
                                ? &m->servers[m->server] : NULL;
    fprintf(f, "  },\n  \"net\": {\n    \"name\": \"%s\",\n"
               "    \"server\": \"%s%s%d\",\n    \"map\": %d,\n"
               "    \"delay\": %d,\n    \"quick_ai\": %s,\n"
               "    \"view\": \"%s\",\n    \"servers\": [",
            m->net_name, chosen != NULL ? chosen->host : "",
            chosen != NULL ? ":" : "", chosen != NULL ? chosen->port : 0,
            m->net_map, m->net_delay, m->net_quick_ai ? "true" : "false",
            m->net_own_view ? "own" : "split");
    bool first = true;
    for (int i = 0; i < m->server_count; i++) {
        const SysMenuServer *sv = &m->servers[i];
        if (sv->origin != SYSMENU_SERVER_SETTINGS) {
            continue;
        }
        fprintf(f, "%s\n      { \"name\": \"%s\", \"host\": \"%s\", "
                   "\"port\": %d }", first ? "" : ",", sv->name, sv->host,
                sv->port);
        first = false;
    }
    fprintf(f, "%s]\n  }\n}\n", first ? "" : "\n    ");
    fclose(f);
}

/* ---- input ---- */

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
}

/* Everything that works the menu: the bound keys, every gamepad, the fixed
 * keys. */
static Buttons menu_buttons(const Input *in)
{
    Buttons b = in->keyboard;
    for (int i = 0; i < in->pads; i++) {
        buttons_or(&b, &in->pad[i]);
    }
    buttons_or(&b, &in->nav);
    return b;
}

static Buttons pressed_since(const Buttons *now, const Buttons *prev)
{
    Buttons p;
    p.up    = now->up    && !prev->up;
    p.down  = now->down  && !prev->down;
    p.left  = now->left  && !prev->left;
    p.right = now->right && !prev->right;
    p.a     = now->a     && !prev->a;
    p.b     = now->b     && !prev->b;
    p.c     = now->c     && !prev->c;
    p.start = now->start && !prev->start;
    return p;
}

void sysmenu_open(SysMenu *m, const Input *in, AspectMode display,
                  bool fullscreen)
{
    m->open = true;
    m->page = SYSMENU_PAGE_MAIN;
    m->cursor = SYSMENU_CONTINUE;
    m->listening = false;
    m->repeat = 0x1F;
    m->side_repeat = 0x1F;
    m->ticks = 0;
    m->quit = false;
    m->display = display;
    m->fullscreen = fullscreen;
    m->prev = menu_buttons(in);
}

static int *page_cursor(SysMenu *m, int *count)
{
    switch (m->page) {
    case SYSMENU_PAGE_AUDIO:
        *count = SYSMENU_AUDIO_ITEMS;
        return &m->audio_cursor;
    case SYSMENU_PAGE_CONTROLS:
        *count = SYSMENU_CONTROL_ROWS;
        return &m->controls_row;
    case SYSMENU_PAGE_BUTTONS:
        *count = SYSMENU_BUTTON_ROWS;
        return &m->buttons_row;
    case SYSMENU_PAGE_CRT:
        *count = SYSMENU_CRT_ITEMS;
        return &m->crt_cursor;
    case SYSMENU_PAGE_HDR:
        *count = SYSMENU_HDR_ITEMS;
        return &m->hdr_cursor;
    case SYSMENU_PAGE_PATCHES:
        *count = HZ_FIX_COUNT;
        return &m->patches_cursor;
    case SYSMENU_PAGE_GAMEPAD:
        *count = SYSMENU_GAMEPAD_ITEMS;
        return &m->gamepad_cursor;
    case SYSMENU_PAGE_NETWORK:
        *count = SYSMENU_NET_ITEMS + m->net.rooms;
        return &m->net_cursor;
    default:
        *count = SYSMENU_ITEMS;
        return &m->cursor;
    }
}

static void move_cursor(SysMenu *m, int dir)
{
    int count;
    int *cursor = page_cursor(m, &count);
    *cursor = (*cursor + dir + count) % count;
    m->repeat = 0x1F;
}

/* A volume a step: OFF, then SYSMENU_DB_MIN up to 0 a dB at a time. Past
 * the top, C (wrap) goes round to OFF; left and right stop at the ends. */
static int step_db(int db, int dir, bool wrap)
{
    if (db <= AUDIO_DB_OFF) {
        return (dir > 0) ? SYSMENU_DB_MIN : AUDIO_DB_OFF;
    }
    int next = db + dir;
    if (next > 0) {
        return wrap ? AUDIO_DB_OFF : 0;
    }
    return (next < SYSMENU_DB_MIN) ? AUDIO_DB_OFF : next;
}

/* (PS2's) PEAK a stop up or down; past the last, C (wrap) goes round to
 * AUTO, left and right stop at the ends. */
static int step_peak(int peak, int dir, bool wrap)
{
    int at = 0;
    for (int i = 0; i < PEAK_STOP_COUNT; i++) {
        if (PEAK_STOPS[i] <= peak) {
            at = i;
        }
    }
    at += dir;
    if (at >= PEAK_STOP_COUNT) {
        at = wrap ? 0 : PEAK_STOP_COUNT - 1;
    } else if (at < 0) {
        at = 0;
    }
    return PEAK_STOPS[at];
}

/* (PS2's) WHITE: 10 nits a step up to 400, 50 above it. */
static int step_white(int white, int dir, bool wrap)
{
    int step = (white > 400 || (white == 400 && dir > 0)) ? 50 : 10;
    int next = white + dir * step;
    if (next > WHITE_MAX) {
        next = wrap ? WHITE_MIN : WHITE_MAX;
    } else if (next < WHITE_MIN) {
        next = WHITE_MIN;
    }
    return next;
}

/* (PS2's) A CRT or HDR row's value a step. */
static void step_crt(SysMenu *m, int dir, bool wrap)
{
    CrtSettings *c = &m->crt;
    if (m->page == SYSMENU_PAGE_CRT) {
        switch (m->crt_cursor) {
        case SYSMENU_CRT_SCANLINES:
            c->scanlines = !c->scanlines;
            break;
        case SYSMENU_CRT_MASK:
            c->mask = (CrtMask)((c->mask + dir + CRT_MASK_COUNT)
                                % CRT_MASK_COUNT);
            break;
        case SYSMENU_CRT_SUBPIXELS:
            c->subpixels = (CrtSubpixels)((c->subpixels + dir
                                           + CRT_SUBPIXELS_COUNT)
                                          % CRT_SUBPIXELS_COUNT);
            break;
        case SYSMENU_CRT_HDR:
            c->hdr = !c->hdr;
            break;
        default:
            return;
        }
    } else {
        switch (m->hdr_cursor) {
        case SYSMENU_HDR_SWITCH:
            c->hdr = !c->hdr;
            break;
        case SYSMENU_HDR_PEAK:
            c->peak_nits = step_peak(c->peak_nits, dir, wrap);
            break;
        case SYSMENU_HDR_WHITE:
            c->white_nits = step_white(c->white_nits, dir, wrap);
            break;
        default:
            return;
        }
    }
    m->crt_changed = true;
}

/* Left, right, or a choice, on a row with a value. */
static void step_value(SysMenu *m, int dir, bool wrap)
{
    if (m->page == SYSMENU_PAGE_CRT || m->page == SYSMENU_PAGE_HDR) {
        step_crt(m, dir, wrap);
        return;
    }
    if (m->page == SYSMENU_PAGE_MAIN) {
        if (m->cursor == SYSMENU_DISPLAY) {
            m->display = (AspectMode)((m->display + dir + ASPECT_COUNT)
                                      % ASPECT_COUNT);
            m->display_changed = true;
        } else if (m->cursor == SYSMENU_FULLSCREEN) {
            m->fullscreen = !m->fullscreen;
            m->fullscreen_changed = true;
        } else if (m->cursor == SYSMENU_CPU) {     /* (session 57) */
            m->cpu_brain = (m->cpu_brain == HZ_BRAIN_NIGHTMARE)
                         ? HZ_BRAIN_ORIGINAL : HZ_BRAIN_NIGHTMARE;
            m->cpu_brain_changed = true;
        }
        return;
    }
    if (m->page == SYSMENU_PAGE_PATCHES) {
        m->fixes ^= PATCH_BIT[m->patches_cursor];
        m->fixes_changed = true;
        return;
    }
    if (m->page == SYSMENU_PAGE_NETWORK) {
        /* (Session 56) SERVER while offline, and MAP: round either way. */
        if (m->net_cursor == SYSMENU_NET_SERVER && m->server_count > 0
            && m->net.state == SYSMENU_NETV_OFFLINE) {
            m->server = (m->server + dir + m->server_count) % m->server_count;
            m->net_changed = true;
        } else if (m->net_cursor == SYSMENU_NET_MAP) {
            m->net_map = (m->net_map + dir + HZ_MAPS) % HZ_MAPS;
            m->net_changed = true;
        } else if (m->net_cursor == SYSMENU_NET_QUICK) {
            m->net_quick_ai = !m->net_quick_ai;     /* (session 58) */
            m->net_changed = true;
        } else if (m->net_cursor == SYSMENU_NET_VIEW) {
            m->net_own_view = !m->net_own_view;     /* (session 59) */
            m->net_changed = true;
        }
        return;
    }
    if (m->page == SYSMENU_PAGE_GAMEPAD) {
        /* (Session 54) RUMBLE a level; left and right stop at OFF and
         * HIGH, C goes round. Every gamepad buzzes at the new one. */
        if (m->gamepad_cursor != SYSMENU_GAMEPAD_RUMBLE) {
            return;
        }
        int level = (int)m->rumble + dir;
        if (level >= RUMBLE_LEVELS) {
            level = wrap ? RUMBLE_OFF : RUMBLE_HIGH;
        } else if (level < 0) {
            level = RUMBLE_OFF;
        }
        if (level != (int)m->rumble) {
            m->rumble = (RumbleLevel)level;
            m->rumble_changed = true;
            m->rumble_test = PLATFORM_PADS;
        }
        return;
    }
    if (m->page == SYSMENU_PAGE_AUDIO) {
        AudioSettings *au = &m->audio;
        switch (m->audio_cursor) {
        case SYSMENU_AUDIO_MASTER:
            au->master_db = step_db(au->master_db, dir, wrap);
            break;
        case SYSMENU_AUDIO_MUSIC:
            au->music_db = step_db(au->music_db, dir, wrap);
            break;
        case SYSMENU_AUDIO_EFFECTS:
            au->effects_db = step_db(au->effects_db, dir, wrap);
            break;
        case SYSMENU_AUDIO_SYNTH:
            if (!hz_audio_has_q()) {
                return;         /* Q NOT BUILT: NUKED stays */
            }
            au->synth = (au->synth == AUDIO_SYNTH_Q) ? AUDIO_SYNTH_NUKED
                                                    : AUDIO_SYNTH_Q;
            break;
        case SYSMENU_AUDIO_VOICES:
            if (!hz_audio_has_q()) {
                return;         /* Q's alone */
            }
            au->voices = (au->voices == AUDIO_VOICES_EXTRA)
                       ? AUDIO_VOICES_CHIP : AUDIO_VOICES_EXTRA;
            break;
        case SYSMENU_AUDIO_REPEATS:
            if (!hz_audio_has_q()) {
                return;         /* Q's alone */
            }
            au->repeats = (au->repeats == AUDIO_REPEATS_CHIP)
                        ? AUDIO_REPEATS_FRESH : AUDIO_REPEATS_CHIP;
            break;
        case SYSMENU_AUDIO_OVERSAMPLE: {
            /* (Session 48) 1, 2, 4 round, either way */
            static const int STOPS[3] = { 1, 2, 4 };
            if (!hz_audio_has_q()) {
                return;         /* Q's alone */
            }
            int at = (au->oversample == 4) ? 2 : (au->oversample == 2);
            au->oversample = STOPS[(at + (dir < 0 ? 2 : 1)) % 3];
            break;
        }
        default:
            return;
        }
        m->audio_changed = true;
        return;
    }
    /* CONTROLS and BUTTONS: left and right pick the column. */
    if (m->page == SYSMENU_PAGE_BUTTONS) {
        int col = m->buttons_col + dir;
        if (m->buttons_row < CONTROL_ACTIONS && col >= 0
            && col < SYSMENU_BUTTON_COLS) {
            m->buttons_col = col;
        }
        return;
    }
    if (m->page == SYSMENU_PAGE_CONTROLS) {
        int col = m->controls_col + dir;
        if (m->controls_row < CONTROL_ACTIONS && col >= 0
            && col < SYSMENU_CONTROL_COLS) {
            m->controls_col = col;
        }
    }
}

/* (Session 56) A on the NETWORK page: what the row does now. */
static void choose_network(SysMenu *m)
{
    SysMenuNetState st = m->net.state;
    bool online = st == SYSMENU_NETV_ONLINE || st == SYSMENU_NETV_WAITING
                  || st == SYSMENU_NETV_AFTER;
    switch (m->net_cursor) {
    case SYSMENU_NET_SERVER:
        step_value(m, +1, true);
        break;
    case SYSMENU_NET_ADDRESS:
        /* (Session 57) Typed from nothing, as the SERVER row can't change
         * online. */
        if (st == SYSMENU_NETV_OFFLINE) {
            m->addr_typing = true;
            m->addr_text[0] = '\0';
            m->addr_error = NULL;
            m->type_prev = m->prev;
        }
        break;
    case SYSMENU_NET_NAME:
        m->net_typing = true;
        m->type_prev = m->prev;
        break;
    case SYSMENU_NET_CONNECT:
        m->net_request = (st == SYSMENU_NETV_OFFLINE)
                       ? SYSMENU_NETREQ_CONNECT : SYSMENU_NETREQ_DISCONNECT;
        break;
    case SYSMENU_NET_QUICK:
        if (st == SYSMENU_NETV_ONLINE || st == SYSMENU_NETV_AFTER) {
            m->net_request = SYSMENU_NETREQ_QUICK;
        }
        break;
    case SYSMENU_NET_HOST:
        if (st == SYSMENU_NETV_AFTER) {
            m->net_request = SYSMENU_NETREQ_REMATCH;    /* (session 66) */
        } else if (st == SYSMENU_NETV_WAITING) {
            m->net_request = SYSMENU_NETREQ_CANCEL;
        } else if (online) {
            m->net_request = SYSMENU_NETREQ_HOST;
        }
        break;
    case SYSMENU_NET_MAP:
    case SYSMENU_NET_VIEW:                  /* (session 59) */
        step_value(m, +1, true);
        break;
    case SYSMENU_NET_NIGHTMARE:
        /* (Session 58) Offline too: then just a game against it. */
        if (st != SYSMENU_NETV_CONNECTING && st != SYSMENU_NETV_PLAYING) {
            m->net_request = SYSMENU_NETREQ_NIGHTMARE;
            m->open = false;
        }
        break;
    default: {
        int k = m->net_cursor - SYSMENU_NET_ITEMS;
        if (k >= 0 && k < m->net.rooms && m->net.room_same[k] && online) {
            m->net_request = SYSMENU_NETREQ_JOIN;
            m->net_request_room = m->net.room_id[k];
        }
        break;
    }
    }
}

static void step_value(SysMenu *m, int dir, bool wrap);

static void choose(SysMenu *m)
{
    switch (m->page) {
    case SYSMENU_PAGE_MAIN:
        switch (m->cursor) {
        case SYSMENU_CONTINUE:
            m->open = false;
            break;
        case SYSMENU_DISPLAY:
            step_value(m, +1, true);
            break;
        case SYSMENU_FULLSCREEN:
        case SYSMENU_CPU:
            step_value(m, +1, true);
            break;
        case SYSMENU_PATCHES:
            m->page = SYSMENU_PAGE_PATCHES;
            break;
        case SYSMENU_CRT:
            m->page = SYSMENU_PAGE_CRT;
            break;
        case SYSMENU_AUDIO:
            m->page = SYSMENU_PAGE_AUDIO;
            break;
        case SYSMENU_CONTROLS:
            m->page = SYSMENU_PAGE_CONTROLS;
            break;
        case SYSMENU_GAMEPAD:
            m->page = SYSMENU_PAGE_GAMEPAD;
            break;
        case SYSMENU_NETWORK:
            m->page = SYSMENU_PAGE_NETWORK;
            break;
        case SYSMENU_QUIT:
            m->quit = true;
            break;
        default:
            break;
        }
        break;
    case SYSMENU_PAGE_AUDIO:
        step_value(m, +1, true);
        break;
    case SYSMENU_PAGE_CRT:
        /* The HDR row opens its page; every other row steps on. */
        if (m->crt_cursor == SYSMENU_CRT_HDR) {
            m->page = SYSMENU_PAGE_HDR;
        } else {
            step_value(m, +1, true);
        }
        break;
    case SYSMENU_PAGE_HDR:
        step_value(m, +1, true);
        break;
    case SYSMENU_PAGE_PATCHES:
        step_value(m, +1, true);
        break;
    case SYSMENU_PAGE_GAMEPAD:
        /* (Session 54) A PAD row buzzes that gamepad, if there is one;
         * BUTTONS opens its page. */
        if (m->gamepad_cursor == SYSMENU_GAMEPAD_RUMBLE) {
            step_value(m, +1, true);
        } else if (m->gamepad_cursor == SYSMENU_GAMEPAD_BUTTONS) {
            m->page = SYSMENU_PAGE_BUTTONS;
        } else if (m->pad_name[m->gamepad_cursor][0] != '\0') {
            m->rumble_test = m->gamepad_cursor;
        }
        break;
    case SYSMENU_PAGE_CONTROLS:
        if (m->controls_row < CONTROL_ACTIONS) {
            m->listening = true;
        } else {
            controls_keys_default(&m->controls);
            m->controls_changed = true;
        }
        break;
    case SYSMENU_PAGE_BUTTONS:
        if (m->buttons_row < CONTROL_ACTIONS) {
            m->listening = true;
        } else {
            controls_pad_default(&m->controls);
            m->controls_changed = true;
        }
        break;
    case SYSMENU_PAGE_NETWORK:
        choose_network(m);
        break;
    default:
        break;
    }
}

/* CONTROLS or BUTTONS, waiting: Escape (or a gamepad's Back) leaves the
 * slot as it is, Backspace empties it; on CONTROLS a key binds it, on
 * BUTTONS (session 54) a gamepad input -- what goes down first on any
 * gamepad -- and anything else is ignored (a key or input the platform
 * keeps too). */
static void listen(SysMenu *m, const Input *in)
{
    bool pad = (m->page == SYSMENU_PAGE_BUTTONS);
    int action = pad ? m->buttons_row : m->controls_row;
    int col = pad ? m->buttons_col : m->controls_col;

    if (in->menu) {
        m->listening = false;
        return;
    }
    if (in->back) {
        if (pad) {
            controls_bind_pad(&m->controls, action, col, PAD_NONE);
        } else {
            controls_bind_key(&m->controls, action, col, KEY_NONE);
        }
        m->controls_changed = true;
        m->listening = false;
        return;
    }
    if (!pad && key_bindable(in->key)) {
        controls_bind_key(&m->controls, action, col, in->key);
        m->controls_changed = true;
        m->listening = false;
    } else if (pad && pad_input_bindable(in->pad_input)) {
        controls_bind_pad(&m->controls, action, col, in->pad_input);
        m->controls_changed = true;
        m->listening = false;
    }
}

/* (Session 56) A key's character for a name, or 0: SDL's scancodes, the
 * USB keyboard's (A-Z 4-29, 1-9 30-38, 0 39, space 44, minus 45). */
static char name_char(int key)
{
    if (key >= 4 && key <= 29) {
        return (char)('A' + (key - 4));
    }
    if (key >= 30 && key <= 38) {
        return (char)('1' + (key - 30));
    }
    if (key == 39) {
        return '0';
    }
    if (key == 44) {
        return ' ';
    }
    if (key == 45) {
        return '-';
    }
    return 0;
}

/* (Session 57) A key's character for an address, or 0: a name's but the
 * space, and the keypad's digits and point, '.', '[' and ']'. The keys
 * come without Shift, so ';' is ':' (never in an address). */
static char address_char(int key)
{
    if (key >= 89 && key <= 97) {
        return (char)('1' + (key - 89));
    }
    if (key == 98) {
        return '0';
    }
    if (key == 55 || key == 99) {
        return '.';
    }
    if (key == 51) {
        return ':';
    }
    if (key == 47) {
        return '[';
    }
    if (key == 48) {
        return ']';
    }
    char c = name_char(key);
    return (c == ' ') ? 0 : c;
}

/* (Session 56) A name's letters, in the order a pad steps through them;
 * (session 57) an address's, the digits first. Right adds the first. */
static const char NAME_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ";
static const char ADDR_CHARS[] = "0123456789.:ABCDEFGHIJKLMNOPQRSTUVWXYZ-[]";

typedef enum {
    TYPE_GOING = 0,
    TYPE_DONE,          /* A, C, Start or Enter */
    TYPE_ESCAPE
} TypeEnd;

/* (Session 56) A row taking characters, NAME's and (session 57)
 * ADDRESS's: `c` (the key's, 0 none) goes on the end, Backspace takes one
 * off. With a pad (or the arrows), up and down step the last through
 * `chars`, right adds chars[0], left takes the last off. The keys bound to
 * the pad type their characters here, not move. */
static TypeEnd type_text(SysMenu *m, const Input *in, char *text,
                         size_t max, const char *chars, char c)
{
    size_t n = strlen(text);
    Buttons now = in->nav;
    for (int i = 0; i < in->pads; i++) {
        buttons_or(&now, &in->pad[i]);
    }
    Buttons press = pressed_since(&now, &m->type_prev);
    m->type_prev = now;
    m->prev = menu_buttons(in);
    TypeEnd end = in->menu ? TYPE_ESCAPE : TYPE_GOING;
    if (c != 0) {
        if (n < max) {
            text[n++] = c;
            text[n] = '\0';
        }
    } else if (in->back || (press.left && n > 0)) {
        if (n > 0) {
            text[--n] = '\0';
        }
    } else if (press.right && n < max) {
        text[n++] = chars[0];
        text[n] = '\0';
    } else if ((press.up || press.down) && n > 0) {
        const char *at = strchr(chars, text[n - 1]);
        int k = (at != NULL) ? (int)(at - chars) : 0;
        int count = (int)strlen(chars);
        k = (k + (press.up ? count - 1 : 1)) % count;
        text[n - 1] = chars[k];
    } else if ((press.a || press.c || press.start) && end == TYPE_GOING) {
        end = TYPE_DONE;
    }
    return end;
}

/* (Session 57) ADDRESS taking characters: Enter (or A, C, Start) takes
 * the address, staying with why if it can't (nothing typed leaves it, as
 * Escape does). */
static void type_address(SysMenu *m, const Input *in)
{
    char before[sizeof m->addr_text];
    memcpy(before, m->addr_text, sizeof before);
    TypeEnd end = type_text(m, in, m->addr_text, SYSMENU_NET_ADDR_LEN,
                            ADDR_CHARS, address_char(in->key));
    if (strcmp(before, m->addr_text) != 0) {
        m->addr_error = NULL;
    }
    if (end == TYPE_ESCAPE || (end == TYPE_DONE && m->addr_text[0] == '\0')) {
        m->addr_typing = false;
    } else if (end == TYPE_DONE) {
        m->addr_error = use_address(m, m->addr_text);
        m->addr_typing = m->addr_error != NULL;
    }
}

/* (Session 56) NAME taking letters: a key types its letter (no space
 * first), Enter or Escape ends it. */
static void type_name(SysMenu *m, const Input *in)
{
    char *name = m->net_name;
    char c = name_char(in->key);
    if (c == ' ' && name[0] == '\0') {
        c = 0;
    }
    if (type_text(m, in, name, SYSMENU_NET_NAME_LEN, NAME_CHARS, c)
        != TYPE_GOING) {
        size_t n = strlen(name);
        while (n > 0 && name[n - 1] == ' ') {
            name[--n] = '\0';
        }
        if (n == 0) {
            snprintf(m->net_name, sizeof m->net_name, "PLAYER");
        }
        m->net_typing = false;
        m->net_changed = true;
    }
}

void sysmenu_tick(SysMenu *m, const Input *in)
{
    if (!m->open) {
        return;
    }
    m->ticks++;
    Buttons now = menu_buttons(in);
    Buttons press = pressed_since(&now, &m->prev);
    m->prev = now;

    if (m->listening) {
        listen(m, in);
        return;
    }
    if (m->net_typing) {
        type_name(m, in);
        return;
    }
    if (m->addr_typing) {
        type_address(m, in);
        return;
    }
    if (m->page == SYSMENU_PAGE_NETWORK
        && m->net_cursor >= SYSMENU_NET_ITEMS + m->net.rooms) {
        m->net_cursor = SYSMENU_NET_ITEMS + m->net.rooms - 1;  /* a row
                                                                  went */
    }
    if (in->menu || press.b) {
        if (m->page == SYSMENU_PAGE_HDR) {
            m->page = SYSMENU_PAGE_CRT;
        } else if (m->page == SYSMENU_PAGE_BUTTONS) {
            m->page = SYSMENU_PAGE_GAMEPAD;
        } else if (m->page != SYSMENU_PAGE_MAIN) {
            m->page = SYSMENU_PAGE_MAIN;
        } else {
            m->open = false;
        }
        return;
    }

    if (press.up) {
        move_cursor(m, -1);
    } else if (press.down) {
        move_cursor(m, +1);
    } else if (now.up) {
        if (--m->repeat < 0) {
            move_cursor(m, -1);
        }
    } else if (now.down) {
        if (--m->repeat < 0) {
            move_cursor(m, +1);
        }
    } else {
        m->repeat = 0x1F;
    }

    /* Left and right: a step, and held, a step every 4 frames after the
     * cursor's delay (on the AUDIO and HDR pages; elsewhere once a
     * press). */
    if (press.left || press.right) {
        step_value(m, press.right ? +1 : -1, false);
        m->side_repeat = 0x1F;
    } else if ((now.left || now.right)
               && (m->page == SYSMENU_PAGE_AUDIO
                   || m->page == SYSMENU_PAGE_HDR)) {
        if (--m->side_repeat < 0) {
            step_value(m, now.right ? +1 : -1, false);
            m->side_repeat = 3;
        }
    }

    if (press.a || press.c || press.start) {
        choose(m);
    }
}

/* (Session 54) The gamepads the GAMEPAD page shows. */
static int pads_connected(const SysMenu *m)
{
    int pads = 0;
    for (int i = 0; i < PLATFORM_PADS; i++) {
        if (m->pad_name[i][0] != '\0') {
            pads++;
        }
    }
    return pads;
}

static int font_tile(char ch);

void sysmenu_set_pad_name(SysMenu *m, int pad, const char *name)
{
    if (pad < 0 || pad >= PLATFORM_PADS) {
        return;
    }
    char *out = m->pad_name[pad];
    int n = 0;
    for (; name != NULL && name[n] != '\0' && n < SYSMENU_PAD_NAME; n++) {
        char ch = name[n];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        out[n] = (font_tile(ch) >= 0) ? ch : ' ';
    }
    /* No trailing spaces (a name cut at one), and a name of nothing but
     * what the font hasn't is still a gamepad. */
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    if (n == 0 && name != NULL) {
        snprintf(out, SYSMENU_PAD_NAME + 1, "GAMEPAD");
        return;
    }
    out[n] = '\0';
}

/* ---- what the rows show ---- */

/* (PS2's) What a CRT or HDR row shows, judged against m->crt_display. */
void sysmenu_crt_value(const SysMenu *m, SysMenuValue row, char *value,
                       size_t value_n, char *why, size_t why_n)
{
    const CrtSettings *c   = &m->crt;
    const CrtDisplay  *d   = &m->crt_display;
    double             par = crt_pixel_aspect((int)m->display);
    CrtWhy             w   = CRT_OK;

    switch (row) {
    case SYSMENU_VALUE_SUMMARY: {
        /* What will be drawn: the mask when it can be, else the scanlines
         * when they can be, else the flat picture. */
        const char *what = "OFF";
        if (c->mask != CRT_MASK_OFF
            && crt_mask_why(d, par, c->mask) == CRT_OK) {
            what = MASK_SHORT[c->mask];
        } else if (c->scanlines && crt_scanlines_why(d, par) == CRT_OK) {
            what = "SCANLINES";
        }
        snprintf(value, value_n, "%s", what);
        break;
    }
    case SYSMENU_VALUE_SCANLINES:
        snprintf(value, value_n, "%s", c->scanlines ? "ON" : "OFF");
        w = c->scanlines ? crt_scanlines_why(d, par) : CRT_OK;
        break;
    case SYSMENU_VALUE_MASK:
        snprintf(value, value_n, "%s", MASK_NAME[c->mask]);
        w = crt_mask_why(d, par, c->mask);
        break;
    case SYSMENU_VALUE_SUBPIXELS:
        snprintf(value, value_n, "%s", SUBPIXEL_NAME[c->subpixels]);
        break;
    case SYSMENU_VALUE_HDR:
        snprintf(value, value_n, "%s", c->hdr ? "ON" : "OFF");
        w = c->hdr ? crt_hdr_why(d) : CRT_OK;
        break;
    case SYSMENU_VALUE_PEAK:
        if (c->peak_nits > 0) {
            snprintf(value, value_n, "%d NITS", c->peak_nits);
        } else {
            snprintf(value, value_n, "AUTO %d NITS", crt_peak_auto(d));
        }
        break;
    case SYSMENU_VALUE_WHITE:
        snprintf(value, value_n, "%d NITS", c->white_nits);
        break;
    default:
        snprintf(value, value_n, "?");
        break;
    }
    snprintf(why, why_n, "%s", crt_why_text(w));
}

void sysmenu_db_text(int db, char *out, size_t n)
{
    if (db <= AUDIO_DB_OFF) {
        snprintf(out, n, "OFF");
    } else {
        snprintf(out, n, "%d DB", db);
    }
}

void sysmenu_slot_text(const SysMenu *m, int action, int col, char *out,
                       size_t n)
{
    key_name(m->controls.key[action][col], out, n);
}

void sysmenu_button_text(const SysMenu *m, int action, int slot, char *out,
                         size_t n)
{
    pad_input_label(m->controls.pad[action][slot], m->pad_style, out, n);
}

/* ---- drawing ---- */

/* An 8 x 8 tile (32 bytes, 4 bits a pixel, high nibble first) at cell
 * (col, row), colour 0 left as it is. */
static void draw_tile(u8 *idx, const u8 *tile, int col, int row, int flip)
{
    for (int y = 0; y < 8; y++) {
        int sy = (flip & FLIP_V) ? 7 - y : y;
        int py = row * 8 + y;
        if (py < 0 || py >= SCREEN_H) {
            continue;
        }
        for (int x = 0; x < 8; x++) {
            int sx = (flip & FLIP_H) ? 7 - x : x;
            int px = col * 8 + x;
            u8 byte = tile[sy * 4 + sx / 2];
            u8 c = (sx & 1) ? (byte & 0x0F) : (byte >> 4);
            if (c != 0 && px >= 0 && px < SCREEN_W) {
                idx[py * SCREEN_W + px] = (u8)(OVERLAY_BASE + c);
            }
        }
    }
}

static void draw_hud_tile(const SysMenu *m, u8 *idx, int tile, int col,
                          int row, int flip)
{
    draw_tile(idx, m->hud + (tile - HUD_FIRST) * 32, col, row, flip);
}

/* The font tile for a character, or -1 (a space, or one it hasn't). */
static int font_tile(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');    /* (session 57) a host's, as upper */
    }
    if (ch >= 'A' && ch <= 'Z') {
        return ch - FONT_FIRST;
    }
    if (ch >= '1' && ch <= '8') {
        return 7 + (ch - '1');
    }
    if (ch == '9') {
        return 46;
    }
    if (ch == '0') {
        return 47;
    }
    if (ch == '-') {
        return '_' - FONT_FIRST;    /* the font's '_' is a dash */
    }
    if ((ch >= ':' && ch <= '>') || ch == '!' || ch == '"' || ch == '['
        || ch == ']' || ch == '^') {
        return ch - FONT_FIRST;
    }
    return -1;
}

static void draw_text(const SysMenu *m, u8 *idx, int col, int row,
                      const char *text)
{
    for (; *text != '\0'; text++, col++) {
        if (*text == '.') {
            /* (Session 56) The font has no full stop: '!' has its dot alone
             * on row 6, the letters' baseline, so '!' with rows 0-5 blank
             * (4 bytes a row) is one. For a server's address. */
            u8 dot[32];
            memcpy(dot, m->font + ('!' - FONT_FIRST) * 32, sizeof dot);
            memset(dot, 0, 24);
            draw_tile(idx, dot, col, row, 0);
            continue;
        }
        int t = font_tile(*text);
        if (t >= 0 && t < HZ_FONT_TILES) {
            draw_tile(idx, m->font + t * 32, col, row, 0);
        }
    }
}

static void draw_arrow(const SysMenu *m, u8 *idx, int col, int row)
{
    draw_tile(idx, m->font + FONT_ARROW * 32, col, row, 0);
}

/* A window of cols x rows cells at (col0, row0): black inside, the HUD
 * strip's frame round it. */
static void draw_window(const SysMenu *m, u8 *idx, int col0, int row0,
                        int cols, int rows)
{
    for (int y = row0 * 8; y < (row0 + rows) * 8; y++) {
        if (y < 0 || y >= SCREEN_H) {
            continue;
        }
        for (int x = col0 * 8; x < (col0 + cols) * 8; x++) {
            if (x >= 0 && x < SCREEN_W) {
                idx[y * SCREEN_W + x] = FILL;
            }
        }
    }
    int right = col0 + cols - 1;
    int bottom = row0 + rows - 1;
    draw_hud_tile(m, idx, TILE_CORNER, col0, row0, 0);
    draw_hud_tile(m, idx, TILE_CORNER, right, row0, FLIP_H);
    draw_hud_tile(m, idx, TILE_CORNER, col0, bottom, FLIP_V);
    draw_hud_tile(m, idx, TILE_CORNER, right, bottom, FLIP_H | FLIP_V);
    for (int c = col0 + 1; c < right; c++) {
        draw_hud_tile(m, idx, TILE_EDGE, c, row0, 0);
        draw_hud_tile(m, idx, TILE_EDGE, c, bottom, FLIP_V);
    }
    for (int r = row0 + 1; r < bottom; r++) {
        int v = ((r - row0) & 1) ? 0 : FLIP_V;
        draw_hud_tile(m, idx, TILE_SIDE, col0, r, v);
        draw_hud_tile(m, idx, TILE_SIDE, right, r, FLIP_H | v);
    }
}

/* (Session 56) The menu's NETWORK value. */
static const char *net_state_name(const SysMenu *m)
{
    switch (m->net.state) {
    case SYSMENU_NETV_CONNECTING:
        return "CONNECTING";
    case SYSMENU_NETV_ONLINE:
        return "ONLINE";
    case SYSMENU_NETV_WAITING:
        return "WAITING";
    case SYSMENU_NETV_PLAYING:
        return "PLAYING";
    case SYSMENU_NETV_AFTER:
        return "AFTER A GAME";
    default:
        return "OFFLINE";
    }
}

static void draw_main(const SysMenu *m, u8 *idx)
{
    static const char *const LABEL[SYSMENU_ITEMS] = {
        "CONTINUE", "DISPLAY", "FULLSCREEN", "CRT", "AUDIO", "CONTROLS",
        "GAMEPAD", "PATCHES", "NETWORK", "CPU", "QUIT"
    };
    int rows = 2 * SYSMENU_ITEMS + 3;
    int col0 = (SCREEN_COLS - MENU_COLS) / 2;
    int row0 = (SCREEN_ROWS - rows) / 2;

    draw_window(m, idx, col0, row0, MENU_COLS, rows);
    for (int i = 0; i < SYSMENU_ITEMS; i++) {
        int row = row0 + 2 + 2 * i;
        draw_text(m, idx, col0 + LABEL_COL, row, LABEL[i]);
        if (i == SYSMENU_DISPLAY) {
            draw_text(m, idx, col0 + VALUE_COL, row,
                      DISPLAY_NAME[m->display]);
        } else if (i == SYSMENU_FULLSCREEN) {
            draw_text(m, idx, col0 + VALUE_COL, row,
                      m->fullscreen ? "ON" : "OFF");
        } else if (i == SYSMENU_CRT) {
            char value[24], why[24];
            sysmenu_crt_value(m, SYSMENU_VALUE_SUMMARY, value, sizeof value,
                              why, sizeof why);
            draw_text(m, idx, col0 + VALUE_COL, row, value);
        } else if (i == SYSMENU_AUDIO) {
            draw_text(m, idx, col0 + VALUE_COL, row,
                      m->audio.synth == AUDIO_SYNTH_Q ? "Q" : "NUKED");
        } else if (i == SYSMENU_GAMEPAD) {
            int pads = pads_connected(m);
            char value[16];
            if (pads == 0) {
                snprintf(value, sizeof value, "NONE");
            } else {
                snprintf(value, sizeof value, "%d PAD%s", pads,
                         (pads > 1) ? "S" : "");
            }
            draw_text(m, idx, col0 + VALUE_COL, row, value);
        } else if (i == SYSMENU_PATCHES) {
            char value[16];
            int on = 0;
            for (int k = 0; k < HZ_FIX_COUNT; k++) {
                on += (m->fixes & PATCH_BIT[k]) != 0;
            }
            if (on == HZ_FIX_COUNT) {
                snprintf(value, sizeof value, "ALL ON");
            } else if (on == 0) {
                snprintf(value, sizeof value, "OFF");
            } else {
                snprintf(value, sizeof value, "%d ON", on);
            }
            draw_text(m, idx, col0 + VALUE_COL, row, value);
        } else if (i == SYSMENU_NETWORK) {
            draw_text(m, idx, col0 + VALUE_COL, row, net_state_name(m));
        } else if (i == SYSMENU_CPU) {
            draw_text(m, idx, col0 + VALUE_COL, row,
                      m->cpu_brain == HZ_BRAIN_NIGHTMARE ? "NIGHTMARE"
                                                         : "ORIGINAL");
        }
    }
    draw_arrow(m, idx, col0 + LABEL_COL - 2, row0 + 2 + 2 * m->cursor);
}

static void draw_audio(const SysMenu *m, u8 *idx)
{
    static const char *const LABEL[SYSMENU_AUDIO_ITEMS] = {
        "MASTER", "MUSIC", "EFFECTS", "SYNTH", "VOICES", "REPEATS",
        "OVERSAMPLE"
    };
    const int db[SYSMENU_AUDIO_EFFECTS + 1] = {
        m->audio.master_db, m->audio.music_db, m->audio.effects_db
    };
    int rows = 2 * SYSMENU_AUDIO_ITEMS + 4;
    int col0 = (SCREEN_COLS - AUDIO_COLS) / 2;
    int row0 = (SCREEN_ROWS - rows) / 2;

    draw_window(m, idx, col0, row0, AUDIO_COLS, rows);
    draw_text(m, idx, col0 + LABEL_COL, row0 + 1, "AUDIO");
    for (int i = 0; i < SYSMENU_AUDIO_ITEMS; i++) {
        int row = row0 + 3 + 2 * i;
        char value[16];
        if (i == SYSMENU_AUDIO_SYNTH) {
            snprintf(value, sizeof value, "%s",
                     !hz_audio_has_q() ? "NUKED"
                     : (m->audio.synth == AUDIO_SYNTH_Q) ? "Q" : "NUKED");
        } else if (i == SYSMENU_AUDIO_VOICES) {
            /* (Session 38) Q's; with NUKED it waits for Q */
            snprintf(value, sizeof value, "%s",
                     (m->audio.voices == AUDIO_VOICES_EXTRA) ? "EXTRA"
                                                             : "CHIP");
        } else if (i == SYSMENU_AUDIO_REPEATS) {
            /* (Session 39) Q's; with NUKED it waits for Q */
            snprintf(value, sizeof value, "%s",
                     (m->audio.repeats == AUDIO_REPEATS_CHIP) ? "CHIP"
                                                              : "FRESH");
        } else if (i == SYSMENU_AUDIO_OVERSAMPLE) {
            /* (Session 48) Q's; with NUKED it waits for Q */
            snprintf(value, sizeof value, "%dX", m->audio.oversample);
        } else {
            sysmenu_db_text(db[i], value, sizeof value);
        }
        draw_text(m, idx, col0 + LABEL_COL, row, LABEL[i]);
        draw_text(m, idx, col0 + VALUE_COL, row, value);
        if (i == SYSMENU_AUDIO_SYNTH && !hz_audio_has_q()) {
            draw_text(m, idx, col0 + VALUE_COL, row + 1, "Q NOT BUILT");
        }
    }
    draw_arrow(m, idx, col0 + LABEL_COL - 2, row0 + 3 + 2 * m->audio_cursor);
}

/* (PS2's) The CRT and HDR pages: wider than the menu, a title, then the
 * rows three apart -- the value, why it can't be had on the line under it,
 * a line of fill. */
static void draw_crt_page(const SysMenu *m, u8 *idx)
{
    static const char *const CRT_LABEL[SYSMENU_CRT_ITEMS] = {
        "SCANLINES", "MASK", "SUBPIXELS", "HDR"
    };
    static const SysMenuValue CRT_ROW[SYSMENU_CRT_ITEMS] = {
        SYSMENU_VALUE_SCANLINES, SYSMENU_VALUE_MASK,
        SYSMENU_VALUE_SUBPIXELS, SYSMENU_VALUE_HDR
    };
    static const char *const HDR_LABEL[SYSMENU_HDR_ITEMS] = {
        "HDR", "PEAK", "WHITE"
    };
    static const SysMenuValue HDR_ROW[SYSMENU_HDR_ITEMS] = {
        SYSMENU_VALUE_HDR, SYSMENU_VALUE_PEAK, SYSMENU_VALUE_WHITE
    };
    bool crt = (m->page == SYSMENU_PAGE_CRT);
    int  n = crt ? SYSMENU_CRT_ITEMS : SYSMENU_HDR_ITEMS;
    const char *const *label = crt ? CRT_LABEL : HDR_LABEL;
    const SysMenuValue *rowv = crt ? CRT_ROW : HDR_ROW;
    int cursor = crt ? m->crt_cursor : m->hdr_cursor;
    int rows = PAGE_PITCH * n + 3;
    int col0 = (SCREEN_COLS - PAGE_COLS) / 2;
    int row0 = (SCREEN_ROWS - rows) / 2;

    draw_window(m, idx, col0, row0, PAGE_COLS, rows);
    draw_text(m, idx, col0 + LABEL_COL, row0 + 1, crt ? "CRT" : "HDR");
    for (int i = 0; i < n; i++) {
        int  row = row0 + 3 + PAGE_PITCH * i;
        char value[24], why[24];
        sysmenu_crt_value(m, rowv[i], value, sizeof value, why, sizeof why);
        draw_text(m, idx, col0 + LABEL_COL, row, label[i]);
        draw_text(m, idx, col0 + VALUE_COL, row, value);
        draw_text(m, idx, col0 + VALUE_COL, row + 1, why);
    }
    draw_arrow(m, idx, col0 + LABEL_COL - 2, row0 + 3 + PAGE_PITCH * cursor);
}

/* PATCHES: a row per fix two apart, and under them what the one the
 * arrow is on does. (Session 71: 9 rows three apart, as the CRT page's,
 * are 30 lines; the screen has 28. Session 72: 10 rows two apart are 25.)
 * (Session 75) At most PATCH_SHOWN rows, the window's size of session 72:
 * with more, they scroll, the arrow's row and the one under it always
 * shown, and the title says which of how many the arrow is on. */
#define PATCH_SHOWN 10

static void draw_patches(const SysMenu *m, u8 *idx)
{
    int shown = HZ_FIX_COUNT < PATCH_SHOWN ? HZ_FIX_COUNT : PATCH_SHOWN;
    int first = m->patches_cursor - shown + 2;
    if (first > HZ_FIX_COUNT - shown) {
        first = HZ_FIX_COUNT - shown;
    }
    if (first < 0) {
        first = 0;
    }
    int rows = 2 * shown + 5;
    int col0 = (SCREEN_COLS - PAGE_COLS) / 2;
    int row0 = (SCREEN_ROWS - rows) / 2;

    draw_window(m, idx, col0, row0, PAGE_COLS, rows);
    draw_text(m, idx, col0 + LABEL_COL, row0 + 1, "PATCHES");
    if (shown < HZ_FIX_COUNT) {
        char at[32];
        snprintf(at, sizeof at, "%d OF %d", m->patches_cursor + 1,
                 HZ_FIX_COUNT);
        draw_text(m, idx, col0 + VALUE_COL, row0 + 1, at);
    }
    for (int k = 0; k < shown; k++) {
        int i = first + k;
        int row = row0 + 3 + 2 * k;
        draw_text(m, idx, col0 + LABEL_COL, row, PATCH_NAME[i]);
        draw_text(m, idx, col0 + VALUE_COL, row,
                  (m->fixes & PATCH_BIT[i]) ? "ON" : "OFF");
    }
    draw_text(m, idx, col0 + LABEL_COL, row0 + rows - 2,
              PATCH_WHAT[m->patches_cursor]);
    draw_arrow(m, idx, col0 + LABEL_COL - 2,
               row0 + 3 + 2 * (m->patches_cursor - first));
}

/* (Session 54) GAMEPAD: a title, PAD 1, PAD 2 and RUMBLE two apart, and
 * under them what choosing does (or that there is nothing to feel). */
static void draw_gamepad(const SysMenu *m, u8 *idx)
{
    static const char *const LABEL[SYSMENU_GAMEPAD_ITEMS] = {
        "PAD 1", "PAD 2", "RUMBLE", "BUTTONS"
    };
    int rows = 2 * SYSMENU_GAMEPAD_ITEMS + 6;
    int col0 = (SCREEN_COLS - PAGE_COLS) / 2;
    int row0 = (SCREEN_ROWS - rows) / 2;

    draw_window(m, idx, col0, row0, PAGE_COLS, rows);
    draw_text(m, idx, col0 + LABEL_COL, row0 + 1, "GAMEPAD");
    for (int i = 0; i < SYSMENU_GAMEPAD_ITEMS; i++) {
        int row = row0 + 3 + 2 * i;
        const char *value = rumble_level_name(m->rumble);
        if (i == SYSMENU_GAMEPAD_BUTTONS) {
            value = "";
        } else if (i != SYSMENU_GAMEPAD_RUMBLE) {
            value = (m->pad_name[i][0] != '\0') ? m->pad_name[i] : "NONE";
        }
        draw_text(m, idx, col0 + LABEL_COL, row, LABEL[i]);
        draw_text(m, idx, col0 + VALUE_COL, row, value);
    }
    draw_text(m, idx, col0 + LABEL_COL, row0 + 3 + 2 * SYSMENU_GAMEPAD_ITEMS,
              (pads_connected(m) == 0) ? "NO GAMEPAD CONNECTED"
                                       : "A C OR START ON A PAD: BUZZ IT");
    draw_arrow(m, idx, col0 + LABEL_COL - 2,
               row0 + 3 + 2 * m->gamepad_cursor);
}

/* (Session 56) NETWORK: a title; SERVER, (session 57) ADDRESS, NAME,
 * CONNECT, QUICK MATCH, HOST GAME, MAP, (session 59) VIEW and (session
 * 58) NIGHTMARE two apart (the counts or what went wrong under CONNECT);
 * then WAITING (right under NIGHTMARE's line since VIEW came) and a row
 * per player waiting, with how long, or OTHER GAME when their game
 * differs. 27 rows, as CONTROLS: the screen's height but a row. */
static void draw_network(const SysMenu *m, u8 *idx)
{
    int rows = 3 + 2 * SYSMENU_NET_ITEMS + 1 + SYSMENU_NET_LIST + 1;
    int col0 = (SCREEN_COLS - PAGE_COLS) / 2;
    int row0 = (SCREEN_ROWS - rows) / 2;
    const SysMenuNetView *v = &m->net;
    bool offline = v->state == SYSMENU_NETV_OFFLINE;

    draw_window(m, idx, col0, row0, PAGE_COLS, rows);
    draw_text(m, idx, col0 + LABEL_COL, row0 + 1, "NETWORK");
    for (int i = 0; i < SYSMENU_NET_ITEMS; i++) {
        int row = row0 + 3 + 2 * i;
        const char *label = "";
        char value[48] = "";
        char under[48] = "";
        switch (i) {
        case SYSMENU_NET_SERVER:
            label = "SERVER";
            snprintf(value, sizeof value, "%s",
                     (m->server < m->server_count)
                     ? m->servers[m->server].name : "NONE");
            break;
        case SYSMENU_NET_ADDRESS: {
            /* (Session 57) The chosen server's, its port only when not
             * 19890; typing, what's typed and the cursor. Longer than the
             * window, its end, '<' where its start went. */
            int width = PAGE_COLS - VALUE_COL - 1;
            label = "ADDRESS";
            if (m->addr_typing) {
                int n = (int)strlen(m->addr_text);
                bool cut = n > width - 1;   /* the cursor's place kept */
                snprintf(value, sizeof value, "%s%.*s%s", cut ? "<" : "",
                         width - 1, m->addr_text + (cut ? n - (width - 2) : 0),
                         (m->ticks & 0x10) ? "^" : "");
                snprintf(under, sizeof under, "%s",
                         (m->addr_error != NULL) ? m->addr_error
                                                 : "TYPE IT  ENTER: DONE");
            } else if (m->server < m->server_count) {
                const SysMenuServer *sv = &m->servers[m->server];
                bool v6 = strchr(sv->host, ':') != NULL;
                char port[8] = "";
                char text[SYSMENU_NET_ADDR_LEN + 1];
                if (sv->port != 19890) {
                    snprintf(port, sizeof port, ":%d", sv->port);
                }
                snprintf(text, sizeof text, "%s%s%s%s", v6 ? "[" : "",
                         sv->host, v6 ? "]" : "", port);
                int n = (int)strlen(text);
                int from = (n > width) ? n - (width - 1) : 0;
                snprintf(value, sizeof value, "%s%.*s", (from > 0) ? "<" : "",
                         width, text + from);
            }
            break;
        }
        case SYSMENU_NET_NAME:
            label = "NAME";
            snprintf(value, sizeof value, "%s%s", m->net_name,
                     (m->net_typing && (m->ticks & 0x10)) ? "^" : "");
            if (m->net_typing) {
                snprintf(under, sizeof under, "TYPE IT  ENTER: DONE");
            }
            break;
        case SYSMENU_NET_CONNECT:
            label = offline ? "CONNECT" : "DISCONNECT";
            snprintf(under, sizeof under, "%s", v->status);
            break;
        case SYSMENU_NET_QUICK:
            label = "QUICK MATCH";
            /* (Session 58) under it: the label fills its column */
            snprintf(under, sizeof under, "%s", m->net_quick_ai
                     ? "< PEOPLE OR AI >" : "< PEOPLE ONLY >");
            break;
        case SYSMENU_NET_HOST:
            label = (v->state == SYSMENU_NETV_WAITING) ? "CANCEL"
                                                       : "HOST GAME";
            if (v->state == SYSMENU_NETV_WAITING) {
                snprintf(value, sizeof value, "WAITING");
            }
            if (v->state == SYSMENU_NETV_AFTER) {
                /* (Session 66) after a game, its room kept: our wish
                 * beside it, the rival's under it */
                label = "REMATCH";
                snprintf(value, sizeof value, "%s", v->asked ? "ASKED" : "");
                snprintf(under, sizeof under, "%s",
                         v->rival_asked ? "RIVAL ASKED"
                         : v->asked ? "WAITING FOR RIVAL" : "A: ASK FOR ONE");
            }
            break;
        case SYSMENU_NET_MAP:
            label = "MAP";
            snprintf(value, sizeof value, "%s", MAP_NAME[m->net_map & 7]);
            break;
        case SYSMENU_NET_VIEW:
            /* (Session 59) The host's: the split screen, or each side its
             * own; a server's rule under it. */
            label = "VIEW";
            snprintf(value, sizeof value, "%s", m->net_own_view
                     ? "OWN SIDE ONLY" : "SPLIT SCREEN");
            if (!offline && v->view_rule != 0) {
                snprintf(under, sizeof under, "SERVER: %s",
                         v->view_rule == 2 ? "OWN SIDE ONLY"
                                           : "SPLIT SCREEN");
            }
            break;
        default:
            label = "NIGHTMARE";
            snprintf(value, sizeof value, "PLAY IT HERE");
            if (!offline && v->state != SYSMENU_NETV_PLAYING) {
                snprintf(under, sizeof under, "STILL IN THE QUEUE");
            }
            break;
        }
        draw_text(m, idx, col0 + LABEL_COL, row, label);
        draw_text(m, idx, col0 + VALUE_COL, row, value);
        draw_text(m, idx, col0 + VALUE_COL, row + 1, under);
    }
    int list0 = row0 + 3 + 2 * SYSMENU_NET_ITEMS;
    char head[48];
    if (offline || v->state == SYSMENU_NETV_CONNECTING) {
        snprintf(head, sizeof head, "WAITING TO PLAY");
    } else if (v->rooms_total == 0) {
        snprintf(head, sizeof head, "WAITING TO PLAY: NONE");
    } else {
        snprintf(head, sizeof head, "WAITING TO PLAY: %d", v->rooms_total);
    }
    draw_text(m, idx, col0 + LABEL_COL, list0, head);
    for (int k = 0; k < v->rooms && k < SYSMENU_NET_LIST; k++) {
        char when[16];
        if (!v->room_same[k]) {
            snprintf(when, sizeof when, "OTHER GAME");
        } else if (v->room_ai[k]) {
            snprintf(when, sizeof when, "SERVER AI");
        } else {
            snprintf(when, sizeof when, "%d:%02d", v->room_secs[k] / 60,
                     v->room_secs[k] % 60);
        }
        draw_text(m, idx, col0 + LABEL_COL, list0 + 1 + k, v->room_name[k]);
        draw_text(m, idx, col0 + LABEL_COL + 18, list0 + 1 + k, when);
    }
    int at = m->net_cursor;
    int arrow = (at < SYSMENU_NET_ITEMS) ? row0 + 3 + 2 * at
                                         : list0 + 1 + (at - SYSMENU_NET_ITEMS);
    draw_arrow(m, idx, col0 + LABEL_COL - 2, arrow);
}

void sysmenu_draw_notice(const SysMenu *m, u8 *indices, const char *line1,
                         const char *line2)
{
    bool two = line2 != NULL && line2[0] != '\0';
    int len = (int)strlen(line1);
    if (two && (int)strlen(line2) > len) {
        len = (int)strlen(line2);
    }
    int cols = len + 6;
    int rows = two ? 6 : 5;
    int col0 = (SCREEN_COLS - cols) / 2;
    int row0 = (SCREEN_ROWS - rows) / 2;
    draw_window(m, indices, col0, row0, cols, rows);
    draw_text(m, indices, col0 + 3, row0 + 2, line1);
    if (two) {
        draw_text(m, indices, col0 + 3, row0 + 3, line2);
    }
}

/* CONTROLS and (session 54) BUTTONS: a row per action, then DEFAULTS,
 * the slot being set blinking (16 frames on, 16 off), what to do under
 * it while one is. */
static void draw_slots(const SysMenu *m, u8 *idx, bool pad)
{
    int cols = pad ? BTN_COLS : CTRL_COLS;
    int slot0 = pad ? BTN_SLOT0 : CTRL_SLOT0;
    int pitch = pad ? BTN_PITCH : CTRL_PITCH;
    int slots = pad ? SYSMENU_BUTTON_COLS : SYSMENU_CONTROL_COLS;
    int row_at = pad ? m->buttons_row : m->controls_row;
    int col = pad ? m->buttons_col : m->controls_col;
    int col0 = (SCREEN_COLS - cols) / 2;
    int row0 = (SCREEN_ROWS - CTRL_ROWS) / 2;

    draw_window(m, idx, col0, row0, cols, CTRL_ROWS);
    draw_text(m, idx, col0 + 3, row0 + 1, pad ? "GAMEPAD BUTTONS"
                                              : "CONTROLS");
    draw_text(m, idx, col0 + slot0, row0 + 3, pad ? "1ST" : "KEY 1");
    draw_text(m, idx, col0 + slot0 + pitch, row0 + 3, pad ? "2ND" : "KEY 2");
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        int row = row0 + 5 + 2 * a;
        draw_text(m, idx, col0 + 3, row, ACTION_NAME[a]);
        for (int c = 0; c < slots; c++) {
            if (m->listening && a == row_at && c == col
                && (m->ticks & 0x10) != 0) {
                continue;
            }
            char name[16];
            if (pad) {
                sysmenu_button_text(m, a, c, name, sizeof name);
            } else {
                sysmenu_slot_text(m, a, c, name, sizeof name);
            }
            draw_text(m, idx, col0 + slot0 + pitch * c, row, name);
        }
    }
    draw_text(m, idx, col0 + 3, row0 + 5 + 2 * CONTROL_ACTIONS, "DEFAULTS");
    if (row_at < CONTROL_ACTIONS) {
        draw_arrow(m, idx, col0 + slot0 + pitch * col - 2,
                   row0 + 5 + 2 * row_at);
    } else {
        draw_arrow(m, idx, col0 + 1, row0 + 5 + 2 * row_at);
    }
    if (m->listening) {
        draw_text(m, idx, col0 + 3, row0 + CTRL_ROWS - 3,
                  pad ? "PRESS A PAD BUTTON" : "PRESS A KEY");
        draw_text(m, idx, col0 + 3, row0 + CTRL_ROWS - 2,
                  "ESC: KEEP   BACKSPC: NONE");
    }
}

void sysmenu_draw(const SysMenu *m, u8 *indices)
{
    if (!m->open) {
        return;
    }
    switch (m->page) {
    case SYSMENU_PAGE_AUDIO:
        draw_audio(m, indices);
        break;
    case SYSMENU_PAGE_CONTROLS:
        draw_slots(m, indices, false);
        break;
    case SYSMENU_PAGE_BUTTONS:
        draw_slots(m, indices, true);
        break;
    case SYSMENU_PAGE_CRT:
        draw_crt_page(m, indices);
        break;
    case SYSMENU_PAGE_HDR:
        draw_crt_page(m, indices);
        break;
    case SYSMENU_PAGE_PATCHES:
        draw_patches(m, indices);
        break;
    case SYSMENU_PAGE_GAMEPAD:
        draw_gamepad(m, indices);
        break;
    case SYSMENU_PAGE_NETWORK:
        draw_network(m, indices);
        break;
    default:
        draw_main(m, indices);
        break;
    }
}
