/* Headless platform: no window, no SDL. Runs for a fixed number of frames
 * with scripted input and writes frames as PNGs, so main.c's loop builds
 * and runs anywhere and its frames can be compared with hz_shot's.
 *
 * Environment:
 *   HZ_FRAMES      frames to present before asking to quit (default 120)
 *   HZ_OUT         PNG of the last frame presented (default frame.png)
 *   HZ_OUT_INDEXED 1: that PNG indexed, as PS2's route PNGs are -- the
 *                  frame's palette indices and a PLTE of its 80 colours
 *                  (CRAM's 64 and the Esc screen's 16) through the DAC
 *                  ladder, which cram_from_rgb8 turns back into the CRAM
 *                  words exactly (tools/crt_ref.c's sources)
 *   HZ_FRAMES_DIR  also every frame, as DIR/fNNNN.png (the directory must
 *                  exist), numbered as hz_shot --frames numbers them
 *   HZ_KEYS        the keyboard's script; HZ_PAD1, HZ_PAD2 the gamepads'
 *                  (each given connects that gamepad). A script is
 *                  "N:BUTTONS,N:BUTTONS,...": from the pump after frame N
 *                  (0 = the first frame presented) the buttons held are
 *                  BUTTONS, letters from UDLRABCS and T (TURBO), or
 *                  "-" for none.
 *                  In HZ_KEYS also E (Escape: the Esc screen opens or
 *                  goes back as it goes down) and X (Backspace, likewise).
 *                  The keyboard's script is also Input.nav.
 *   HZ_RAW         keys and gamepad inputs pressed, for the CONTROLS and
 *                  BUTTONS pages: "N:NAME,N:PAD:NAME,...", a key by its
 *                  name (core/keys.c: Q, SPACE, LSHIFT, ...) or a gamepad
 *                  input by its (session 54: PAD:NORTH, PAD:LSUP, PAD:L2,
 *                  PAD:JOY3), pressed in the pump after frame N. (Session
 *                  54) PAD:BACK is also Input.menu, as Escape; each is
 *                  also a tap of what the controls make it
 *                  (platform_set_controls's): a key's on the keyboard, a
 *                  gamepad input's on gamepad 1 (when HZ_PAD1 connects
 *                  it), and its d-pad or left stick on Input.nav -- so a
 *                  check can bind one and see it reach the game
 *   HZ_PAD_STYLE   (session 54) whose labels the scripted gamepads have,
 *                  a PadStyle key (core/keys.c: ps3, xboxone, switch,
 *                  joystick ...); none by default
 *   HZ_WAV         the sound, as a 16-bit stereo WAV at 48 kHz: each frame
 *                  presented renders its share (a frame is 896,040 master
 *                  clocks), so the file runs with the game's frames
 *   HZ_RUMBLE      (session 54) a log of the rumble: a line "F PAD LOW HIGH"
 *                  (the frames presented, the gamepad from 1, the motors
 *                  in hex) each time a connected gamepad's motors change.
 *                  A scripted gamepad's name is "HEADLESS PAD N"
 *   HZ_DISPLAY     the screen the CRT page judges its values against, in
 *                  crt_display_parse's words (core/crt.h), e.g.
 *                  "3840x2400 fullscreen hdr=1.624/1.000 sdr=203"; without
 *                  it a 960x672 window on a 1080p desktop. The PNGs are
 *                  the raw frame whatever the CRT page says */

#include "platform.h"
#include "../core/pace.h"
#include "../core/pngwrite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRIPT_STEPS 512

#define EXTRA_ESCAPE    0x01
#define EXTRA_BACKSPACE 0x02

typedef struct {
    int     frame[SCRIPT_STEPS];
    Buttons held[SCRIPT_STEPS];
    u8      extra[SCRIPT_STEPS];    /* EXTRA_*: the keyboard's own keys */
    int     count;
    bool    given;
} Script;

typedef struct {
    int frame[SCRIPT_STEPS];
    int key[SCRIPT_STEPS];          /* a scancode, or KEY_NONE */
    int pad_input[SCRIPT_STEPS];    /* a gamepad input, or PAD_NONE */
    int count;
} RawScript;

struct Platform {
    int         frames_to_run;
    int         presented;
    char        out_path[512];
    const char *frames_dir;
    u8          rgb[SCREEN_W * SCREEN_H * 3];
    u8          index[SCREEN_W * SCREEN_H];         /* HZ_OUT_INDEXED's */
    u8          plte[(OVERLAY_BASE + OVERLAY_COLOURS) * 3];
    bool        indexed;
    bool        have_frame;
    Script      keys;
    Script      pad[PLATFORM_PADS];
    RawScript   raw;
    u8          extra_prev;         /* last pump's E and X */
    AspectMode  aspect;
    bool        fullscreen;
    HzAudio    *audio;
    FILE       *wav;
    u32         wav_frames;     /* stereo frames written */
    FILE       *rumble_log;     /* (session 54) HZ_RUMBLE's */
    u16         rumble[PLATFORM_PADS][2];   /* each pad's motors as logged */
    char        pad_name[PLATFORM_PADS][32];
    PadStyle    pad_style;      /* (session 54) HZ_PAD_STYLE's */
    Controls    controls;       /* (session 54) HZ_RAW's taps go through */
};

static bool parse_buttons(const char *s, size_t n, Buttons *b, u8 *extra)
{
    memset(b, 0, sizeof *b);
    *extra = 0;
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case 'E':
            *extra |= EXTRA_ESCAPE;
            break;
        case 'X':
            *extra |= EXTRA_BACKSPACE;
            break;
        case 'U':
            b->up = true;
            break;
        case 'D':
            b->down = true;
            break;
        case 'L':
            b->left = true;
            break;
        case 'R':
            b->right = true;
            break;
        case 'A':
            b->a = true;
            break;
        case 'B':
            b->b = true;
            break;
        case 'C':
            b->c = true;
            break;
        case 'S':
            b->start = true;
            break;
        case 'T':
            b->turbo = true;
            break;
        case '-':
            break;
        default:
            return false;
        }
    }
    return true;
}

/* False, with a message, if the script can't be read. */
static bool parse_script(const char *name, Script *sc)
{
    memset(sc, 0, sizeof *sc);
    const char *text = getenv(name);
    if (text == NULL) {
        return true;
    }
    sc->given = true;
    const char *s = text;
    while (*s != '\0') {
        char *end = NULL;
        long frame = strtol(s, &end, 10);
        if (end == s || *end != ':' || frame < 0
            || sc->count >= SCRIPT_STEPS) {
            fprintf(stderr, "headless: can't read %s=%s\n", name, text);
            return false;
        }
        const char *b = end + 1;
        size_t n = strcspn(b, ",");
        if (!parse_buttons(b, n, &sc->held[sc->count],
                           &sc->extra[sc->count])) {
            fprintf(stderr, "headless: can't read %s=%s\n", name, text);
            return false;
        }
        sc->frame[sc->count] = (int)frame;
        sc->count++;
        s = b + n;
        if (*s == ',') {
            s++;
        }
    }
    return true;
}

/* What a script holds after frame f: its last step at or before f. */
static Buttons script_at(const Script *sc, int f, u8 *extra)
{
    Buttons b;
    memset(&b, 0, sizeof b);
    u8 e = 0;
    for (int i = 0; i < sc->count; i++) {
        if (sc->frame[i] <= f) {
            b = sc->held[i];
            e = sc->extra[i];
        }
    }
    if (extra != NULL) {
        *extra = e;
    }
    return b;
}

/* HZ_RAW. False, with a message, if it can't be read. */
static bool parse_raw(RawScript *r)
{
    memset(r, 0, sizeof *r);
    const char *text = getenv("HZ_RAW");
    if (text == NULL) {
        return true;
    }
    const char *s = text;
    while (*s != '\0') {
        char *end = NULL;
        long frame = strtol(s, &end, 10);
        if (end == s || *end != ':' || frame < 0
            || r->count >= SCRIPT_STEPS) {
            fprintf(stderr, "headless: can't read HZ_RAW=%s\n", text);
            return false;
        }
        const char *b = end + 1;
        size_t n = strcspn(b, ",");
        char name[32];
        snprintf(name, sizeof name, "%.*s", (int)n, b);
        r->frame[r->count] = (int)frame;
        r->key[r->count] = KEY_NONE;
        r->pad_input[r->count] = PAD_NONE;
        if (strncmp(name, "PAD:", 4) == 0) {
            r->pad_input[r->count] = pad_input_from_name(name + 4);
        } else {
            r->key[r->count] = key_from_name(name);
        }
        if (r->key[r->count] == KEY_NONE
            && r->pad_input[r->count] == PAD_NONE) {
            fprintf(stderr, "headless: HZ_RAW: no key or gamepad input "
                            "%s\n", name);
            return false;
        }
        r->count++;
        s = b + n;
        if (*s == ',') {
            s++;
        }
    }
    return true;
}

Platform *platform_create(const char *title, int scale)
{
    (void)title;
    (void)scale;
    Platform *p = calloc(1, sizeof *p);
    if (p == NULL) {
        return NULL;
    }
    const char *frames = getenv("HZ_FRAMES");
    p->frames_to_run = (frames != NULL) ? atoi(frames) : 120;
    const char *out = getenv("HZ_OUT");
    snprintf(p->out_path, sizeof p->out_path, "%s",
             (out != NULL) ? out : "frame.png");
    p->frames_dir = getenv("HZ_FRAMES_DIR");
    const char *indexed = getenv("HZ_OUT_INDEXED");
    p->indexed = indexed != NULL && strcmp(indexed, "1") == 0;
    p->aspect = ASPECT_TV;
    if (!parse_script("HZ_KEYS", &p->keys)
        || !parse_script("HZ_PAD1", &p->pad[0])
        || !parse_script("HZ_PAD2", &p->pad[1]) || !parse_raw(&p->raw)) {
        free(p);
        return NULL;
    }
    const char *rumble = getenv("HZ_RUMBLE");
    if (rumble != NULL) {
        p->rumble_log = fopen(rumble, "w");
        if (p->rumble_log == NULL) {
            fprintf(stderr, "headless: can't write %s\n", rumble);
        }
    }
    for (int i = 0; i < PLATFORM_PADS; i++) {
        snprintf(p->pad_name[i], sizeof p->pad_name[i], "HEADLESS PAD %d",
                 i + 1);
    }
    const char *style = getenv("HZ_PAD_STYLE");
    p->pad_style = pad_style_from_key(style, PAD_STYLES);
    if (p->pad_style == PAD_STYLES) {
        if (style != NULL) {
            fprintf(stderr, "headless: HZ_PAD_STYLE: no style %s\n", style);
            free(p);
            return NULL;
        }
        p->pad_style = PAD_STYLE_NONE;
    }
    controls_default(&p->controls);
    return p;
}

static void put_le(FILE *f, u32 v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        fputc((int)((v >> (8 * i)) & 0xFF), f);
    }
}

static void wav_header(FILE *f, u32 frames)
{
    u32 data = frames * AUDIO_CHANNELS * 2;

    fwrite("RIFF", 1, 4, f);
    put_le(f, 36 + data, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le(f, 16, 4);
    put_le(f, 1, 2);                                /* PCM */
    put_le(f, AUDIO_CHANNELS, 2);
    put_le(f, AUDIO_RATE, 4);
    put_le(f, AUDIO_RATE * AUDIO_CHANNELS * 2, 4);
    put_le(f, AUDIO_CHANNELS * 2, 2);
    put_le(f, 16, 2);
    fwrite("data", 1, 4, f);
    put_le(f, data, 4);
}

void platform_attach_audio(Platform *p, HzAudio *audio)
{
    p->audio = audio;
    const char *path = getenv("HZ_WAV");
    if (audio == NULL || path == NULL) {
        return;
    }
    p->wav = fopen(path, "wb");
    if (p->wav == NULL) {
        fprintf(stderr, "headless: can't write %s\n", path);
        return;
    }
    wav_header(p->wav, 0);              /* sizes filled in at the end */
}

/* The sound up to the end of the frame just presented. */
static void wav_frame_audio(Platform *p)
{
    if (p->wav == NULL) {
        return;
    }
    u32 upto = (u32)((double)p->presented * 896040.0 * AUDIO_RATE
                     / 53693175.0);
    float buf[1024 * AUDIO_CHANNELS];

    while (p->wav_frames < upto) {
        int n = (int)(upto - p->wav_frames > 1024 ? 1024
                                                  : upto - p->wav_frames);
        hz_audio_render(p->audio, buf, n);
        for (int i = 0; i < n * AUDIO_CHANNELS; i++) {
            int v = (int)(buf[i] * 32767.0f);
            v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
            put_le(p->wav, (u32)(v & 0xFFFF), 2);
        }
        p->wav_frames += (u32)n;
    }
}

void platform_destroy(Platform *p)
{
    if (p == NULL) {
        return;
    }
    if (p->rumble_log != NULL) {
        fclose(p->rumble_log);
    }
    if (p->wav != NULL) {
        fseek(p->wav, 0, SEEK_SET);
        wav_header(p->wav, p->wav_frames);
        fclose(p->wav);
        fprintf(stderr, "headless: %.1f s of sound in HZ_WAV\n",
                (double)p->wav_frames / AUDIO_RATE);
    }
    if (p->have_frame) {
        bool ok = p->indexed
                ? png_write(p->out_path, SCREEN_W, SCREEN_H, 8, 3, p->index,
                            SCREEN_W, p->plte, OVERLAY_BASE + OVERLAY_COLOURS,
                            NULL)
                : png_write(p->out_path, SCREEN_W, SCREEN_H, 8, 2, p->rgb,
                            SCREEN_W * 3, NULL, 0, NULL);
        if (ok) {
            fprintf(stderr, "headless: wrote %s after %d frames\n",
                    p->out_path, p->presented);
        } else {
            fprintf(stderr, "headless: can't write %s\n", p->out_path);
        }
    }
    free(p);
}

/* (Session 54) Actions (a bit per ControlAction) into `b`. */
static void buttons_add(Buttons *b, u32 actions)
{
    b->up    = b->up    || (actions >> CONTROL_UP) & 1;
    b->down  = b->down  || (actions >> CONTROL_DOWN) & 1;
    b->left  = b->left  || (actions >> CONTROL_LEFT) & 1;
    b->right = b->right || (actions >> CONTROL_RIGHT) & 1;
    b->a     = b->a     || (actions >> CONTROL_A) & 1;
    b->b     = b->b     || (actions >> CONTROL_B) & 1;
    b->c     = b->c     || (actions >> CONTROL_C) & 1;
    b->start = b->start || (actions >> CONTROL_START) & 1;
    b->turbo = b->turbo || (actions >> CONTROL_TURBO) & 1;
}

bool platform_pump(Platform *p, Input *in)
{
    memset(in, 0, sizeof *in);
    if (p->presented >= p->frames_to_run) {
        return false;
    }
    int f = p->presented - 1;           /* the frame just presented */
    u8 extra = 0;
    in->keyboard = script_at(&p->keys, f, &extra);
    in->nav = in->keyboard;
    in->menu = (extra & EXTRA_ESCAPE) && !(p->extra_prev & EXTRA_ESCAPE);
    in->back = (extra & EXTRA_BACKSPACE)
            && !(p->extra_prev & EXTRA_BACKSPACE);
    p->extra_prev = extra;
    for (int i = 0; i < PLATFORM_PADS; i++) {
        if (!p->pad[i].given) {
            break;
        }
        in->pad[i] = script_at(&p->pad[i], f, NULL);
        in->pads = i + 1;
    }
    in->key = KEY_NONE;
    in->pad_input = PAD_NONE;
    for (int i = 0; i < p->raw.count; i++) {
        if (p->raw.frame[i] != f) {
            continue;
        }
        in->key = p->raw.key[i];
        in->pad_input = p->raw.pad_input[i];
        /* (Session 54) A gamepad's Back is the Esc screen's, as Escape;
         * anything else a tap of what the controls make it. */
        if (in->pad_input == PAD_BACK) {
            in->menu = true;
        }
        buttons_add(&in->keyboard,
                    controls_key_actions(&p->controls, in->key));
        if (in->pad_input != PAD_NONE) {
            PadHeld h;
            memset(&h, 0, sizeof h);
            h.held[in->pad_input] = true;
            if (in->pads > 0) {
                buttons_add(&in->pad[0], controls_pad_actions(&p->controls,
                                                              &h));
            }
            buttons_add(&in->nav, pad_nav_actions(&h));
        }
    }
    return true;
}

void platform_present(Platform *p, const u8 *indices, const u16 *cram,
                      const u16 *overlay)
{
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u8 v = indices[i];
        u16 word = cram[v & (VDP_CRAM_WORDS - 1)];
        if (v >= OVERLAY_BASE && v < OVERLAY_BASE + OVERLAY_COLOURS) {
            word = (overlay != NULL) ? overlay[v - OVERLAY_BASE] : 0;
        }
        vdp_cram_rgb(word, p->rgb + i * 3);
        p->index[i] = (v < OVERLAY_BASE + OVERLAY_COLOURS)
                    ? v : (u8)(v & (VDP_CRAM_WORDS - 1));
    }
    for (int i = 0; i < OVERLAY_BASE + OVERLAY_COLOURS; i++) {
        u16 word = (i < VDP_CRAM_WORDS) ? cram[i]
                 : ((overlay != NULL) ? overlay[i - OVERLAY_BASE] : 0);
        vdp_cram_rgb(word, p->plte + i * 3);
    }
    p->have_frame = true;
    if (p->frames_dir != NULL) {
        char path[1024];
        snprintf(path, sizeof path, "%s/f%04d.png", p->frames_dir,
                 p->presented);
        if (!png_write(path, SCREEN_W, SCREEN_H, 8, 2, p->rgb, SCREEN_W * 3,
                       NULL, 0, NULL)) {
            fprintf(stderr, "headless: can't write %s; no more frames\n",
                    path);
            p->frames_dir = NULL;
        }
    }
    p->presented++;
    wav_frame_audio(p);
}

/* (Session 63) No pace here: a frame shown aside is a frame presented, as
 * it was before platform_show; the clock is the frames presented. */
void platform_show(Platform *p, const u8 *indices, const u16 *cram,
                   const u16 *overlay)
{
    platform_present(p, indices, cram, overlay);
}

double platform_now(Platform *p)
{
    return (double)p->presented / PACE_HZ;
}

void platform_pace_held(Platform *p, double seconds)
{
    (void)p;
    (void)seconds;
}

void platform_pace_slow(Platform *p, double seconds)
{
    (void)p;
    (void)seconds;
}

/* Kept, so the Esc screen shows what it set; nothing is drawn by them. */
void platform_set_aspect(Platform *p, AspectMode mode)
{
    if (mode >= 0 && mode < ASPECT_COUNT) {
        p->aspect = mode;
    }
}

void platform_set_fullscreen(Platform *p, bool on)
{
    p->fullscreen = on;
}

AspectMode platform_aspect(const Platform *p)
{
    return p->aspect;
}

bool platform_fullscreen(const Platform *p)
{
    return p->fullscreen;
}

/* The scripts are the pad's buttons already: nothing to map. (Session
 * 54) HZ_RAW's taps go through them. */
void platform_set_controls(Platform *p, const Controls *c)
{
    p->controls = *c;
}

/* (Session 54) Whether gamepad `pad` is connected: its script given, and
 * every one before it (platform_pump's count). */
static bool pad_connected(const Platform *p, int pad)
{
    if (pad < 0 || pad >= PLATFORM_PADS) {
        return false;
    }
    for (int i = 0; i <= pad; i++) {
        if (!p->pad[i].given) {
            return false;
        }
    }
    return true;
}

/* (Session 54) The rumble: a change logged to HZ_RUMBLE, for a connected
 * gamepad. */
void platform_rumble(Platform *p, int pad, u16 low, u16 high)
{
    if (!pad_connected(p, pad)) {
        return;
    }
    if (p->rumble[pad][0] == low && p->rumble[pad][1] == high) {
        return;
    }
    p->rumble[pad][0] = low;
    p->rumble[pad][1] = high;
    if (p->rumble_log != NULL) {
        fprintf(p->rumble_log, "%d %d %04X %04X\n", p->presented, pad + 1,
                low, high);
    }
}

const char *platform_pad_name(Platform *p, int pad)
{
    if (!pad_connected(p, pad)) {
        return NULL;
    }
    return p->pad_name[pad];
}

/* (PS2's) HZ_DISPLAY describes the screen (crt_display_parse's words), so
 * a headless run can show every value the CRT page can; without it, the
 * default window on a 1080p desktop. Read each call: cheap, and a test can
 * set it before the menu opens. */
void platform_display_info(Platform *p, CrtDisplay *out)
{
    (void)p;
    static bool warned;
    const char *text = getenv("HZ_DISPLAY");
    if (text == NULL || !crt_display_parse(text, out)) {
        if (text != NULL && !warned) {
            fprintf(stderr, "HZ_DISPLAY: can't read \"%s\"\n", text);
            warned = true;
        }
        crt_display_default(out);
    }
}

void platform_startup(void)
{
}

void platform_find_home(const char *dir)
{
    (void)dir;
}

void platform_alert(const char *text)
{
    fprintf(stderr, "%s\n", text);
}

/* (Session 78) No window to ask in: how to make the files, and no. */
bool platform_make_assets(const char *assets, int nfiles, char **files)
{
    (void)nfiles;
    (void)files;
    fprintf(stderr, "The game's files are not made yet (%s/%s). Make "
                    "them from your own cartridge image with the game "
                    "(it asks for it), or:\n"
                    "    make hz_assets && ./hz_assets IMAGE %s\n"
                    "(docs/first_start.md)\n", assets, HZ_MADE_FILE, assets);
    return false;
}

/* The CRT is the SDL3 platform's; the PNGs here are the raw frame. */
void platform_set_crt(Platform *p, const CrtSettings *s)
{
    (void)p;
    (void)s;
}

/* (Session 54) HZ_PAD_STYLE's, for every scripted gamepad. */
PadStyle platform_pad_style(Platform *p, int pad)
{
    return pad_connected(p, pad) ? p->pad_style : PAD_STYLE_NONE;
}
