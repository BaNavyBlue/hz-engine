/* SDL3 platform.
 *
 * Taken from the PS2 port's SDL3 platform. Two ways to the screen, as
 * PS2's (its builds 107-108). SDL_GPU first: the frame's indices and its
 * palette go up as textures and crt_gpu.c draws them -- the CRT chain
 * (docs/crt.md) when the CRT page asks for scanlines or a mask this display
 * can have, else the flat picture: the largest whole multiple of 224 lines
 * that fits, the pixel aspect across, black bars round it. With HDR ON
 * where SDL says the window has HDR, the swapchain is HDR10 (Rec.2020, PQ)
 * or scRGB (HDR_EXTENDED_LINEAR), whichever the window offers (HDR10
 * first; HZ_HDR_SWAPCHAIN=scrgb or =pq picks), the chain in nits by PEAK
 * and WHITE; SDR otherwise, following the window's HDR state as it
 * changes. SDL_Renderer is the fallback (PS2's SDR placeholder, the flat
 * picture only: the indices resolved through CRAM on the CPU, a nearest
 * step to a whole multiple of 320, then a linear one down to the aspect's
 * width) when there is no SDL_GPU device, when the compiled shaders are
 * not in shaders/, or with HZ_GPU=0; HZ_GPU_DEBUG=1 asks for SDL_GPU's
 * debug layer. The line `display: ...` at start-up says which. Vsync, else
 * a sleep to the frame rate; borderless fullscreen. New here: gamepads
 * (PS2 reads the keyboard only).
 *
 * (Session 63) The pace is src/core/pace.c's (as PS2's main loop): a
 * display within 1% of the game's 59.92 Hz shows a frame a refresh, any
 * other is shown the game's frames on the clock (platform_present). Until
 * session 63 every present waited for the refresh: the game ran at the
 * display's rate, 2.4 times as fast at 144 Hz. HZ_PACE=1: a line every
 * 5 s of what the pace did.
 *
 * The pad's keys and gamepad inputs are the controls (core/keys.h, set on
 * the Esc screen's CONTROLS and BUTTONS pages); by default arrows or
 * W/A/S/D, Z/X/C or L/;/' for A/B/C, Enter for Start, and on a gamepad
 * the d-pad or the left stick, West/South/East (X/A/B on an Xbox pad) for
 * A/B/C, as the Mega Drive pad's A/B/C run left to right, and Start. The
 * platform's own keys: Escape (or a gamepad's Back) opens the Esc screen,
 * F11 or Alt+Enter toggles fullscreen, F10 steps the aspect.
 *
 * (Session 54) Any gamepad, any input: each pump reads what every
 * gamepad holds -- its buttons, its sticks and triggers past half their
 * travel -- as core/keys.h's pad inputs, and the controls make them the
 * pad's buttons. A joystick SDL has no gamepad mapping for (an arcade
 * stick, an old USB pad) is opened as a joystick: its buttons are JOY 1
 * to JOY 32, its first hat the d-pad, its first two axes the left stick.
 * A gamecontrollerdb.txt beside the program (SDL's mapping format) is
 * read at start-up, for pads SDL doesn't know. The Esc screen's BUTTONS
 * page draws the inputs as the first gamepad labels them
 * (platform_pad_style, from SDL_GetGamepadType).
 *
 * (Session 54) A gamepad's motors are the rumble's (platform_rumble,
 * game/rumble.h): SDL_RumbleGamepad (SDL_RumbleJoystick for a mapless
 * one), on any pad SDL can shake (a DualShock 3 or 4, a DualSense, an
 * Xbox pad), stopped when the pad goes or the program ends. */

#include "platform.h"
#include "crt_gpu.h"
#include "../core/pace.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#define change_dir _chdir
#else
#include <unistd.h>
#define change_dir chdir
#endif

#define WINDOW_SCALE 3

/* Pixel width / height for each AspectMode (platform.h). */
static const double ASPECT_PAR[ASPECT_COUNT] = {
    1.0,                /* ASPECT_SQUARE */
    14.0 / 15.0,        /* ASPECT_4_3: (4/3) / (320/224) */
    32.0 / 35.0,        /* ASPECT_TV: 6.14 MHz / 6.71 MHz */
};

static const char *const ASPECT_NAME[ASPECT_COUNT] = {
    "square", "4:3", "TV (32:35)"
};

/* (Session 54) The rumble (platform_rumble): what a gamepad's motors were
 * last sent, and the frames since. */
#define RUMBLE_STEP_SHIFT 11    /* a change of a 32nd is sent at once */
#define RUMBLE_RESEND     6     /* a level held is sent again so often, */
#define RUMBLE_MS         250   /* each send good for this long */

typedef struct {
    u16 low, high;
    int frames;
} Rumble;

/* (Session 54) A gamepad: an SDL_Gamepad, or the SDL_Joystick of one SDL
 * has no gamepad mapping for (`pad` NULL); neither, no gamepad there. */
typedef struct {
    SDL_Gamepad   *pad;
    SDL_Joystick  *joy;
    SDL_JoystickID id;
    PadHeld        latched;     /* buttons gone down since the last pump */
    PadHeld        prev;        /* held at the last pump: an axis or hat
                                   going past half is a press */
} PadSlot;

struct Platform {
    SDL_Window   *window;
    /* The SDL_GPU chain; NULL when SDL_Renderer draws. */
    SDL_GPUDevice *gpu;
    CrtGpu        *crt_gpu;
    char           crt_said[200];   /* the last `crt:` line, said on a change */
    /* The swapchain's composition, and which the window offers (asked
     * again when its HDR state or display changes). */
    SDL_GPUSwapchainComposition composition;
    bool           comp_known;
    bool           comp_ok[4];
    int            hdr_said;        /* the HDR state last said: -1 none yet */
    CrtSettings    crt;             /* the CRT page's */
    u8             frame[SCREEN_W * SCREEN_H];  /* the indices sent up */
    /* The SDL_Renderer fallback. */
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    SDL_Texture  *scaled;       /* the frame at whole multiples */
    int           scaled_w, scaled_h;
    bool          scaled_failed;
    AspectMode    aspect;
    bool          fullscreen;   /* as the window is, not as last asked */
    u32           pixels[SCREEN_W * SCREEN_H];
    Pace          pace;         /* (session 63) */
    bool          pace_log;     /* HZ_PACE */
    bool          vsync;
    PadSlot       slot[PLATFORM_PADS];  /* in the order they came */
    Buttons       latched_keys;         /* presses since the last pump */
    Buttons       latched_keys_nav;     /* the same, the fixed keys */
    SDL_AudioStream *audio_stream;      /* PS2's: F32 stereo, 48 kHz */
    HzAudio         *audio;
    Controls      controls;
    bool          latched_menu;         /* Escape or Back since the pump */
    bool          latched_back;         /* Backspace */
    int           latched_key;          /* the first key pressed */
    int           latched_pad_input;    /* the first gamepad input */
    Rumble        rumble[PLATFORM_PADS];    /* (session 54) as last sent */
};

static void set_action(Buttons *b, int action)
{
    switch (action) {
    case CONTROL_UP:
        b->up = true;
        break;
    case CONTROL_DOWN:
        b->down = true;
        break;
    case CONTROL_LEFT:
        b->left = true;
        break;
    case CONTROL_RIGHT:
        b->right = true;
        break;
    case CONTROL_A:
        b->a = true;
        break;
    case CONTROL_B:
        b->b = true;
        break;
    case CONTROL_C:
        b->c = true;
        break;
    case CONTROL_START:
        b->start = true;
        break;
    case CONTROL_TURBO:
        b->turbo = true;
        break;
    default:
        break;
    }
}

/* A key's pad buttons, by the controls. */
static void key_button(const Platform *p, int sc, Buttons *b)
{
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        for (int k = 0; k < CONTROL_KEYS; k++) {
            if (p->controls.key[a][k] == sc) {
                set_action(b, a);
            }
        }
    }
}

/* The Esc screen's fixed keys: arrows, and Enter as Start. */
static void nav_key(int sc, Buttons *b)
{
    switch (sc) {
    case KEY_UP:
        b->up = true;
        break;
    case KEY_DOWN:
        b->down = true;
        break;
    case KEY_LEFT:
        b->left = true;
        break;
    case KEY_RIGHT:
        b->right = true;
        break;
    case KEY_RETURN:
        b->start = true;
        break;
    default:
        break;
    }
}

/* (Session 54) Actions, a bit per ControlAction, as pad buttons. */
static void actions_to_buttons(u32 actions, Buttons *b)
{
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        if ((actions >> a) & 1) {
            set_action(b, a);
        }
    }
}

static void slot_close(PadSlot *s);
static void pad_removed_at(Platform *p, int i);
static void load_mappings(void);

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

void platform_set_aspect(Platform *p, AspectMode mode)
{
    if (mode >= 0 && mode < ASPECT_COUNT) {
        p->aspect = mode;
    }
}

AspectMode platform_aspect(const Platform *p)
{
    return p->aspect;
}

bool platform_fullscreen(const Platform *p)
{
    return p->fullscreen;
}

void platform_set_controls(Platform *p, const Controls *c)
{
    p->controls = *c;
}

/* The mouse cursor: hidden while the window is fullscreen, shown in a
 * window (PS2 build 112a). */
static void follow_cursor(const Platform *p)
{
    if (p->fullscreen) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
}

/* Borderless fullscreen on the display the window is on: SDL3's
 * fullscreen with no mode set, so nothing is switched (PS2 build 100).
 * platform_present lays the frame out from the output's size every frame. */
void platform_set_fullscreen(Platform *p, bool on)
{
    if (p->fullscreen == on) {
        return;
    }
    if (on && !SDL_SetWindowFullscreenMode(p->window, NULL)) {
        SDL_Log("borderless fullscreen unavailable (%s)", SDL_GetError());
        return;
    }
    if (!SDL_SetWindowFullscreen(p->window, on)) {
        SDL_Log("fullscreen %s failed (%s)", on ? "on" : "off",
                SDL_GetError());
        return;
    }
    SDL_SyncWindow(p->window);
    p->fullscreen = on;
    follow_cursor(p);
}

/* ---- the SDL_GPU chain (PS2's builds 107-108) ---- */

static void display_state(Platform *p, int out_w, int out_h,
                          CrtDisplay *out);

/* The output in pixels: the GPU's swapchain is the window's pixels. */
static void output_size(Platform *p, int *w, int *h)
{
    *w = 0;
    *h = 0;
    if (p->gpu != NULL) {
        SDL_GetWindowSizeInPixels(p->window, w, h);
    } else {
        SDL_GetRenderOutputSize(p->renderer, w, h);
    }
}

/* The SDL_GPU chain on the window: a device, the compiled shaders, the
 * window claimed with an SDR swapchain and vsync, the pipelines for its
 * format. False, with everything undone, when any of it can't be had --
 * SDL_Renderer draws instead. */
static bool gpu_start(Platform *p)
{
    const char *env = SDL_getenv("HZ_GPU");
    if (env != NULL && strcmp(env, "0") == 0) {
        fprintf(stderr, "display: HZ_GPU=0, so SDL_Renderer\n");
        return false;
    }
    bool debug = SDL_getenv("HZ_GPU_DEBUG") != NULL;
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV
                                             | SDL_GPU_SHADERFORMAT_DXIL
                                             | SDL_GPU_SHADERFORMAT_MSL,
                                             debug, NULL);
    if (dev == NULL) {
        fprintf(stderr, "display: no SDL_GPU device (%s), so SDL_Renderer\n",
                SDL_GetError());
        return false;
    }
    CrtGpu *g = crt_gpu_create(dev, "shaders");
    if (g == NULL) {
        SDL_DestroyGPUDevice(dev);
        fprintf(stderr, "display: no CRT shaders, so SDL_Renderer\n");
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(dev, p->window)) {
        fprintf(stderr, "display: SDL_GPU can't have the window (%s), so "
                        "SDL_Renderer\n", SDL_GetError());
        crt_gpu_destroy(g);
        SDL_DestroyGPUDevice(dev);
        return false;
    }
    /* SDR, as sRGB-encoded 8-bit: the chain encodes (the HDR swapchains
     * when the CRT page asks), and vsync paces the game. */
    SDL_SetGPUSwapchainParameters(dev, p->window,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_VSYNC);
    SDL_GPUTextureFormat format =
        SDL_GetGPUSwapchainTextureFormat(dev, p->window);
    if (!crt_gpu_set_target(g, format)) {
        crt_gpu_destroy(g);
        SDL_ReleaseWindowFromGPUDevice(dev, p->window);
        SDL_DestroyGPUDevice(dev);
        fprintf(stderr, "display: no CRT pipelines, so SDL_Renderer\n");
        return false;
    }
    p->gpu         = dev;
    p->crt_gpu     = g;
    p->vsync       = true;
    p->composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    p->comp_known  = false;
    return true;
}

static const char *composition_name(SDL_GPUSwapchainComposition c)
{
    switch (c) {
        case SDL_GPU_SWAPCHAINCOMPOSITION_SDR: {
            return "SDR";
        }
        case SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR: {
            return "SDR_LINEAR";
        }
        case SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR: {
            return "scRGB (HDR_EXTENDED_LINEAR)";
        }
        case SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084: {
            return "HDR10 (Rec.2020, PQ)";
        }
        default: {
            return "?";
        }
    }
}

/* Which compositions the window offers, asked once and again after SDL
 * says its HDR state or its display changed. */
static void gpu_offered(Platform *p)
{
    if (p->comp_known) {
        return;
    }
    static const SDL_GPUSwapchainComposition ALL[4] = {
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
        SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR,
        SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084,
    };
    for (int i = 0; i < 4; i++) {
        p->comp_ok[i] = SDL_WindowSupportsGPUSwapchainComposition(
            p->gpu, p->window, ALL[i]);
    }
    p->comp_known = true;
}

static bool offered(const Platform *p, SDL_GPUSwapchainComposition c)
{
    int i = (int)c;
    return i >= 0 && i < 4 && p->comp_ok[i];
}

/* HDR ON where SDL says the window has HDR (the page's NO HDR rule): HDR10
 * if the window offers it, else scRGB; HZ_HDR_SWAPCHAIN=scrgb asks for
 * scRGB first, =pq for HDR10 only. SDR otherwise. */
static SDL_GPUSwapchainComposition gpu_wanted(Platform *p,
                                              const CrtDisplay *d)
{
    if (!p->crt.hdr || crt_hdr_why(d) != CRT_OK) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    }
    gpu_offered(p);
    const char *env   = SDL_getenv("HZ_HDR_SWAPCHAIN");
    bool        scrgb = env != NULL && strcmp(env, "scrgb") == 0;
    bool        pq    = env != NULL && strcmp(env, "pq") == 0;
    if (scrgb && offered(p, SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR)) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
    }
    if (offered(p, SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084)) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
    }
    if (!pq && offered(p, SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR)) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
    }
    return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
}

/* The swapchain switched to `c` and the pipelines remade for its format;
 * idle first, as nothing may be in flight on the old swapchain. False
 * (the old swapchain kept, or SDR) when SDL refuses it. */
static bool gpu_switch(Platform *p, SDL_GPUSwapchainComposition c)
{
    SDL_WaitForGPUIdle(p->gpu);
    if (!SDL_SetGPUSwapchainParameters(p->gpu, p->window, c,
                                       SDL_GPU_PRESENTMODE_VSYNC)) {
        fprintf(stderr, "display: the swapchain refused %s (%s)\n",
                composition_name(c), SDL_GetError());
        if ((int)c >= 0 && (int)c < 4) {
            p->comp_ok[(int)c] = false;
        }
        return false;
    }
    SDL_GPUTextureFormat format =
        SDL_GetGPUSwapchainTextureFormat(p->gpu, p->window);
    if (!crt_gpu_set_target(p->crt_gpu, format)) {
        fprintf(stderr, "display: no CRT pipelines for %s\n",
                composition_name(c));
        if ((int)c >= 0 && (int)c < 4) {
            p->comp_ok[(int)c] = false;
        }
        return false;
    }
    p->composition = c;
    fprintf(stderr, "display: swapchain %s, texture format %d\n",
            composition_name(c), (int)format);
    return true;
}

/* Each frame, before the swapchain texture is acquired: the swapchain the
 * page and the window's HDR state ask for. A composition SDL refuses is
 * not asked again until the window's HDR state changes; SDR is the floor. */
static void gpu_follow_hdr(Platform *p)
{
    CrtDisplay d;
    display_state(p, 0, 0, &d);
    int hdr = d.hdr ? 1 : 0;
    if (hdr != p->hdr_said) {
        fprintf(stderr, "display: HDR %s, headroom %.3f, SDR white %.3f "
                        "(%.0f nits), AUTO %d nits\n",
                d.hdr ? "on" : "off", (double)d.headroom,
                (double)d.sdr_white, (double)d.sdr_nits,
                crt_peak_auto(&d));
        p->hdr_said = hdr;
    }
    SDL_GPUSwapchainComposition want = gpu_wanted(p, &d);
    if (want == p->composition) {
        return;
    }
    /* A refused HDR swapchain may have left the old pipelines released or
     * the swapchain changed: SDR again, whole. */
    if (!gpu_switch(p, want) && want != SDL_GPU_SWAPCHAINCOMPOSITION_SDR) {
        gpu_switch(p, SDL_GPU_SWAPCHAINCOMPOSITION_SDR);
    }
}

/* The encode the swapchain wants, for the flat picture and the plan. */
static CrtEncode composition_encode(SDL_GPUSwapchainComposition c)
{
    if (c == SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084) {
        return CRT_ENCODE_PQ;
    }
    if (c == SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR) {
        return CRT_ENCODE_SCRGB;
    }
    return CRT_ENCODE_SRGB;
}

/* scRGB's 1.0 in nits: SDL's SDR white is that many of its linear units,
 * so 1.0 is sdr_nits / sdr_white -- 80 on Windows (scRGB), SDR white
 * (taken as 203 nits) on Wayland and macOS, where SDL's SDR white is 1.0. */
static double scrgb_unit_nits(const CrtDisplay *d)
{
    return (d->sdr_white > 0.0f) ? (double)(d->sdr_nits / d->sdr_white)
                                 : 80.0;
}

/* The SDL_Renderer fallback: a streaming texture, nearest. */
static bool renderer_start(Platform *p)
{
    p->renderer = SDL_CreateRenderer(p->window, NULL);
    if (p->renderer == NULL) {
        SDL_Log("renderer creation failed: %s", SDL_GetError());
        return false;
    }
    p->texture = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_XRGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   SCREEN_W, SCREEN_H);
    if (p->texture == NULL) {
        SDL_Log("texture creation failed: %s", SDL_GetError());
        SDL_DestroyRenderer(p->renderer);
        p->renderer = NULL;
        return false;
    }
    SDL_SetTextureScaleMode(p->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(p->texture, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);

    /* Without pacing the loop would draw as fast as it can. vsync is the
     * good version; the sleep in platform_present covers the rest. */
    p->vsync = SDL_SetRenderVSync(p->renderer, 1);
    if (!p->vsync) {
        SDL_Log("vsync unavailable (%s); pacing with sleep", SDL_GetError());
    }
    return true;
}

static char *g_start_dir;

const char *platform_start_dir(void)
{
    return g_start_dir;
}

void platform_startup(void)
{
    g_start_dir = SDL_GetCurrentDirectory();    /* (session 78) */
#ifdef _WIN32
    /* A stream with no handle is -2: the packaged game's, started with no
     * console. One redirected to a file keeps its file. */
    if (_fileno(stderr) < 0 && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stderr);
        if (_fileno(stdout) < 0) {
            freopen("CONOUT$", "w", stdout);
        }
    }
#endif
}

static bool has_dir(const char *base, const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s%s", base, dir);
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info)
           && info.type == SDL_PATHTYPE_DIRECTORY;
}

void platform_find_home(const char *dir)
{
    if (has_dir("", dir)) {
        return;
    }
    const char *base = SDL_GetBasePath();   /* ends in a separator */
    if (base != NULL && has_dir(base, dir) && change_dir(base) != 0) {
        fprintf(stderr, "hz: can't move to %s\n", base);
    }
}

void platform_alert(const char *text)
{
    fprintf(stderr, "%s\n", text);
#ifdef _WIN32
    if (_fileno(stderr) < 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Herzog Zwei", text,
                                 NULL);
    }
#endif
}

/* platform_alert with SDL's reason. */
static void alert_sdl(const char *what)
{
    char text[512];
    snprintf(text, sizeof text, "hz: %s: %s", what, SDL_GetError());
    platform_alert(text);
}

Platform *platform_create(const char *title, int scale)
{
#ifdef _WIN32
    /* The window's icon: the exe's (src/platform/win/hz.rc, icon 1). */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    /* (Session 55) A DualShock 3 under DsHidMini's SXS mode (Sony's
     * sixaxis.sys's protocol) through SDL's own PS3 driver: PS3 CONTROLLER,
     * its PlayStation names and its rumble. Without it SDL takes the pad
     * through Windows's generic path, whose force feedback refuses the
     * rumble. DsHidMini's XInput mode (an Xbox pad) needs nothing. */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER, "1");
#endif
#ifdef __APPLE__
    /* (Session 68 (2)) FULLSCREEN on a Mac in place, not in a Space of its own.
     * Once the GPU chain has drawn into the window, SDL's Space
     * fullscreen takes the switch and never makes it: every call says
     * yes, the window stays as it was and SDL_WINDOW_FULLSCREEN stays
     * off. Only fullscreen at the start, before the first frame, got
     * there. In place it switches every time (docs/macos_build.md). */
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        alert_sdl("SDL_Init failed");
        return NULL;
    }
    load_mappings();

    Platform *p = SDL_calloc(1, sizeof *p);
    if (p == NULL) {
        SDL_Quit();
        return NULL;
    }
    if (scale <= 0) {
        scale = WINDOW_SCALE;
    }

    /* High pixel density: the window's buffer is the panel's own pixels
     * on a scaled desktop, and the frame is placed by the output's size in
     * pixels (PS2 build 105). */
    p->window = SDL_CreateWindow(title, SCREEN_W * scale, SCREEN_H * scale,
                                 SDL_WINDOW_RESIZABLE
                                 | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (p->window == NULL) {
        alert_sdl("window creation failed");
        SDL_free(p);
        SDL_Quit();
        return NULL;
    }
    if (!gpu_start(p) && !renderer_start(p)) {
        alert_sdl("nothing to draw with");
        SDL_DestroyWindow(p->window);
        SDL_free(p);
        SDL_Quit();
        return NULL;
    }
    p->aspect = ASPECT_TV;
    p->hdr_said = -1;
    crt_settings_default(&p->crt);
    controls_default(&p->controls);
    p->latched_key = KEY_NONE;
    p->latched_pad_input = PAD_NONE;

    /* One line saying what drives the screen: which video backend, which
     * renderer or GPU driver, and what rate the display really runs at. */
    const SDL_DisplayMode *mode =
        SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(p->window));
    const char *video_driver = SDL_GetCurrentVideoDriver();
    char        drawing[96];
    if (p->gpu != NULL) {
        snprintf(drawing, sizeof drawing, "SDL_GPU %s, %s shaders",
                 SDL_GetGPUDeviceDriver(p->gpu),
                 crt_gpu_shader_format(p->crt_gpu));
    } else {
        const char *render_driver = SDL_GetRendererName(p->renderer);
        snprintf(drawing, sizeof drawing, "renderer %s (the flat picture "
                 "only)", (render_driver != NULL) ? render_driver : "?");
    }
    fprintf(stderr, "display: video %s, %s, %.2f Hz, vsync %s\n",
            (video_driver != NULL) ? video_driver : "?", drawing,
            (mode != NULL) ? (double)mode->refresh_rate : 0.0,
            p->vsync ? "on" : "off (pacing with sleep)");

    pace_init(&p->pace, 1.0 / PACE_HZ, platform_now(p));
    p->pace_log = SDL_getenv("HZ_PACE") != NULL;
    return p;
}

/* PS2's: SDL pulls on its own thread; the mix is made here in chunks of
 * up to 512 stereo frames. `additional` is in bytes, a part-frame rounded
 * up to a whole one. */
static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream,
                                   int additional, int total)
{
    (void)total;
    Platform *p = (Platform *)userdata;

    if (p->audio == NULL) {
        return;
    }
    float buf[512 * AUDIO_CHANNELS];
    const int frame_bytes = AUDIO_CHANNELS * (int)sizeof(float);
    int frames_needed = (additional + frame_bytes - 1) / frame_bytes;

    while (frames_needed > 0) {
        int chunk = frames_needed > 512 ? 512 : frames_needed;
        hz_audio_render(p->audio, buf, chunk);
        SDL_PutAudioStreamData(stream, buf, chunk * frame_bytes);
        frames_needed -= chunk;
    }
}

void platform_attach_audio(Platform *p, HzAudio *audio)
{
    p->audio = audio;
    if (audio == NULL) {
        return;
    }
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = AUDIO_CHANNELS;
    spec.freq = AUDIO_RATE;
    p->audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, p);
    if (p->audio_stream == NULL) {
        SDL_Log("audio unavailable (%s); continuing silently",
                SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(p->audio_stream);
}

void platform_destroy(Platform *p)
{
    if (p == NULL) {
        return;
    }
    if (p->audio_stream != NULL) {
        SDL_DestroyAudioStream(p->audio_stream);   /* stops the callback */
    }
    for (int i = 0; i < PLATFORM_PADS; i++) {
        slot_close(&p->slot[i]);
    }
    if (p->gpu != NULL) {
        SDL_WaitForGPUIdle(p->gpu);
        crt_gpu_destroy(p->crt_gpu);
        SDL_ReleaseWindowFromGPUDevice(p->gpu, p->window);
        SDL_DestroyGPUDevice(p->gpu);
    }
    if (p->scaled != NULL) {
        SDL_DestroyTexture(p->scaled);
    }
    if (p->texture != NULL) {
        SDL_DestroyTexture(p->texture);
    }
    if (p->renderer != NULL) {
        SDL_DestroyRenderer(p->renderer);
    }
    SDL_DestroyWindow(p->window);
    SDL_free(p);
    SDL_Quit();
}

/* (Session 54) A slot's gamepad or joystick closed, its motors stopped. */
static void slot_close(PadSlot *s)
{
    if (s->pad != NULL) {
        SDL_RumbleGamepad(s->pad, 0, 0, 0);
        SDL_CloseGamepad(s->pad);
    }
    if (s->joy != NULL) {
        SDL_RumbleJoystick(s->joy, 0, 0, 0);
        SDL_CloseJoystick(s->joy);
    }
    memset(s, 0, sizeof *s);
}

static int pad_slot(const Platform *p, SDL_JoystickID id)
{
    for (int i = 0; i < PLATFORM_PADS; i++) {
        if ((p->slot[i].pad != NULL || p->slot[i].joy != NULL)
            && p->slot[i].id == id) {
            return i;
        }
    }
    return -1;
}

/* A slot newly filled: said on stderr, nothing held or latched yet. */
static void slot_filled(Platform *p, int i, const char *name, bool mapless)
{
    fprintf(stderr, "gamepad %d: %s%s\n", i + 1,
            (name != NULL) ? name : "?",
            mapless ? " (no gamepad mapping: a joystick)" : "");
    memset(&p->slot[i].latched, 0, sizeof p->slot[i].latched);
    memset(&p->slot[i].prev, 0, sizeof p->slot[i].prev);
    memset(&p->rumble[i], 0, sizeof p->rumble[i]);
}

/* A gamepad has come: into the first free slot (a third is left alone).
 * (Session 54) One already there as a mapless joystick (a mapping come
 * since) becomes the gamepad in its slot. */
static void pad_added(Platform *p, SDL_JoystickID id)
{
    int i = pad_slot(p, id);
    if (i >= 0 && p->slot[i].pad != NULL) {
        return;
    }
    if (i >= 0) {
        SDL_CloseJoystick(p->slot[i].joy);
        p->slot[i].joy = NULL;
    }
    for (int k = 0; k < PLATFORM_PADS && i < 0; k++) {
        if (p->slot[k].pad == NULL && p->slot[k].joy == NULL) {
            i = k;
        }
    }
    if (i < 0) {
        return;
    }
    p->slot[i].pad = SDL_OpenGamepad(id);
    if (p->slot[i].pad == NULL) {
        SDL_Log("gamepad %u: %s", (unsigned)id, SDL_GetError());
        pad_removed_at(p, i);
        return;
    }
    p->slot[i].id = id;
    slot_filled(p, i, SDL_GetGamepadName(p->slot[i].pad), false);
}

/* (Session 54) A joystick has come: if SDL has no gamepad mapping for it,
 * into the first free slot as a joystick (a gamepad comes as a gamepad
 * too, to pad_added). */
static void joy_added(Platform *p, SDL_JoystickID id)
{
    if (SDL_IsGamepad(id) || pad_slot(p, id) >= 0) {
        return;
    }
    for (int i = 0; i < PLATFORM_PADS; i++) {
        if (p->slot[i].pad == NULL && p->slot[i].joy == NULL) {
            p->slot[i].joy = SDL_OpenJoystick(id);
            if (p->slot[i].joy == NULL) {
                SDL_Log("joystick %u: %s", (unsigned)id, SDL_GetError());
                return;
            }
            p->slot[i].id = id;
            slot_filled(p, i, SDL_GetJoystickName(p->slot[i].joy), true);
            return;
        }
    }
}

/* Slot `i` emptied: the ones after it move up, so the slots stay in the
 * order the pads came. */
static void pad_removed_at(Platform *p, int i)
{
    slot_close(&p->slot[i]);
    for (int j = i; j + 1 < PLATFORM_PADS; j++) {
        p->slot[j] = p->slot[j + 1];
        p->rumble[j] = p->rumble[j + 1];
    }
    memset(&p->slot[PLATFORM_PADS - 1], 0, sizeof p->slot[PLATFORM_PADS - 1]);
    memset(&p->rumble[PLATFORM_PADS - 1], 0,
           sizeof p->rumble[PLATFORM_PADS - 1]);
}

/* A gamepad or joystick has gone (SDL says each for a gamepad: the second
 * finds nothing). */
static void pad_removed(Platform *p, SDL_JoystickID id)
{
    int i = pad_slot(p, id);
    if (i >= 0) {
        pad_removed_at(p, i);
        fprintf(stderr, "gamepad %d gone\n", i + 1);
    }
}

/* (Session 54) An input gone down on slot `i` (an event's): latched, so a
 * tap between two pumps still counts, and the first since the last pump
 * is the one the BUTTONS page binds. */
static void pad_pressed(Platform *p, int i, int input)
{
    p->slot[i].latched.held[input] = true;
    if (p->latched_pad_input == PAD_NONE && input != PAD_BACK) {
        p->latched_pad_input = input;
    }
}

/* (Session 54) What a slot holds now, as pad inputs: a gamepad's buttons
 * and its sticks and triggers past half; a mapless joystick's buttons,
 * its first hat as the d-pad and its first two axes as the left stick. */
static void slot_held(const PadSlot *s, PadHeld *h)
{
    memset(h, 0, sizeof *h);
    if (s->pad != NULL) {
        for (int b = 0; b < PAD_BUTTONS; b++) {
            h->held[b] = SDL_GetGamepadButton(s->pad, (SDL_GamepadButton)b);
        }
        s16 axes[PAD_AXES];
        for (int a = 0; a < PAD_AXES; a++) {
            axes[a] = SDL_GetGamepadAxis(s->pad, (SDL_GamepadAxis)a);
        }
        pad_held_axes(h, axes);
        return;
    }
    int buttons = SDL_GetNumJoystickButtons(s->joy);
    for (int b = 0; b < buttons && b < PAD_JOYS; b++) {
        h->held[PAD_JOY1 + b] = SDL_GetJoystickButton(s->joy, b);
    }
    if (SDL_GetNumJoystickHats(s->joy) > 0) {
        pad_held_hat(h, SDL_GetJoystickHat(s->joy, 0));
    }
    if (SDL_GetNumJoystickAxes(s->joy) >= 2) {
        pad_held_joy_axes(h, SDL_GetJoystickAxis(s->joy, 0),
                          SDL_GetJoystickAxis(s->joy, 1));
    }
}

/* (Session 54) gamecontrollerdb.txt beside the program: SDL's mapping
 * format, for gamepads SDL doesn't know (a pad of them then comes as a
 * gamepad, not a mapless joystick). */
static void load_mappings(void)
{
    const char *base = SDL_GetBasePath();
    char path[1024];
    SDL_PathInfo info;
    if (base == NULL) {
        return;
    }
    snprintf(path, sizeof path, "%sgamecontrollerdb.txt", base);
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        return;
    }
    int n = SDL_AddGamepadMappingsFromFile(path);
    if (n < 0) {
        fprintf(stderr, "gamecontrollerdb.txt: %s\n", SDL_GetError());
    } else {
        fprintf(stderr, "gamecontrollerdb.txt: %d mappings\n", n);
    }
}

void platform_rumble(Platform *p, int pad, u16 low, u16 high)
{
    if (pad < 0 || pad >= PLATFORM_PADS
        || (p->slot[pad].pad == NULL && p->slot[pad].joy == NULL)) {
        return;
    }
    Rumble *r = &p->rumble[pad];
    bool on = low != 0 || high != 0;
    bool was = r->low != 0 || r->high != 0;
    bool moved = (low >> RUMBLE_STEP_SHIFT) != (r->low >> RUMBLE_STEP_SHIFT)
              || (high >> RUMBLE_STEP_SHIFT) != (r->high >> RUMBLE_STEP_SHIFT);
    r->frames++;
    if (on != was || moved || (on && r->frames >= RUMBLE_RESEND)) {
        Uint32 ms = on ? RUMBLE_MS : 0;
        if (p->slot[pad].pad != NULL) {
            SDL_RumbleGamepad(p->slot[pad].pad, low, high, ms);
        } else {
            SDL_RumbleJoystick(p->slot[pad].joy, low, high, ms);
        }
        r->low = low;
        r->high = high;
        r->frames = 0;
    }
}

const char *platform_pad_name(Platform *p, int pad)
{
    if (pad < 0 || pad >= PLATFORM_PADS) {
        return NULL;
    }
    const char *name = NULL;
    if (p->slot[pad].pad != NULL) {
        name = SDL_GetGamepadName(p->slot[pad].pad);
    } else if (p->slot[pad].joy != NULL) {
        name = SDL_GetJoystickName(p->slot[pad].joy);
    } else {
        return NULL;
    }
    return (name != NULL) ? name : "GAMEPAD";
}

PadStyle platform_pad_style(Platform *p, int pad)
{
    if (pad < 0 || pad >= PLATFORM_PADS) {
        return PAD_STYLE_NONE;
    }
    if (p->slot[pad].joy != NULL) {
        return PAD_STYLE_JOYSTICK;
    }
    if (p->slot[pad].pad == NULL) {
        return PAD_STYLE_NONE;
    }
    switch (SDL_GetGamepadType(p->slot[pad].pad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
        return PAD_STYLE_XBOX360;
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return PAD_STYLE_XBOXONE;
    case SDL_GAMEPAD_TYPE_PS3:
        return PAD_STYLE_PS3;
    case SDL_GAMEPAD_TYPE_PS4:
        return PAD_STYLE_PS4;
    case SDL_GAMEPAD_TYPE_PS5:
        return PAD_STYLE_PS5;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return PAD_STYLE_SWITCH;
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return PAD_STYLE_GAMECUBE;
    default:
        return PAD_STYLE_STANDARD;
    }
}

/* The platform's own keys: Escape for the Esc screen, Backspace (the
 * CONTROLS page's), fullscreen, aspect. */
static void platform_key(Platform *p, const SDL_KeyboardEvent *key)
{
    if (key->scancode == SDL_SCANCODE_ESCAPE) {
        p->latched_menu = true;
        return;
    }
    if (key->scancode == SDL_SCANCODE_BACKSPACE) {
        p->latched_back = true;
    }
    bool alt_enter = key->scancode == SDL_SCANCODE_RETURN
                  && (key->mod & SDL_KMOD_ALT) != 0;
    if (key->scancode == SDL_SCANCODE_F11 || alt_enter) {
        platform_set_fullscreen(p, !p->fullscreen);
    } else if (key->scancode == SDL_SCANCODE_F10) {
        p->aspect = (AspectMode)((p->aspect + 1) % ASPECT_COUNT);
        fprintf(stderr, "aspect: %s\n", ASPECT_NAME[p->aspect]);
    }
}

bool platform_pump(Platform *p, Input *in)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) {
            return false;
        }
        /* The window system can take the window in or out of fullscreen on
         * its own (a desktop shortcut): follow it. */
        if (ev.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
            p->fullscreen = true;
            follow_cursor(p);
        } else if (ev.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
            p->fullscreen = false;
            follow_cursor(p);
        }
        /* HDR switched on or off in the desktop, its headroom changed, or
         * the window moved to another display: what the window offers is
         * asked again before the next frame (gpu_follow_hdr). */
        if (ev.type == SDL_EVENT_WINDOW_HDR_STATE_CHANGED
            || ev.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
            p->comp_known = false;
        }
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
            pad_added(p, ev.gdevice.which);
        } else if (ev.type == SDL_EVENT_JOYSTICK_ADDED) {
            joy_added(p, ev.jdevice.which);
        } else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
            pad_removed(p, ev.gdevice.which);
        } else if (ev.type == SDL_EVENT_JOYSTICK_REMOVED) {
            pad_removed(p, ev.jdevice.which);
        } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
            platform_key(p, &ev.key);
            /* Latched, not only sampled: a tap between two frames still
             * reaches the game. Alt+Enter is the platform's, not Start. */
            if ((ev.key.mod & SDL_KMOD_ALT) == 0) {
                key_button(p, (int)ev.key.scancode, &p->latched_keys);
                nav_key((int)ev.key.scancode, &p->latched_keys_nav);
                if (p->latched_key == KEY_NONE) {
                    p->latched_key = (int)ev.key.scancode;
                }
            }
        } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            int slot = pad_slot(p, ev.gbutton.which);
            if (slot < 0 || p->slot[slot].pad == NULL
                || ev.gbutton.button >= PAD_BUTTONS) {
                continue;
            }
            if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                p->latched_menu = true;
            }
            pad_pressed(p, slot, ev.gbutton.button);
        } else if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
            /* (Session 54) A mapless joystick's: a gamepad's own come as
             * gamepad events. */
            int slot = pad_slot(p, ev.jbutton.which);
            if (slot >= 0 && p->slot[slot].joy != NULL
                && ev.jbutton.button < PAD_JOYS) {
                pad_pressed(p, slot, PAD_JOY1 + ev.jbutton.button);
            }
        }
    }

    memset(in, 0, sizeof *in);
    int count = 0;
    const bool *keys = SDL_GetKeyboardState(&count);
    bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    for (int sc = 0; sc < count; sc++) {
        if (!keys[sc]) {
            continue;
        }
        if (alt && sc == SDL_SCANCODE_RETURN) {
            continue;
        }
        key_button(p, sc, &in->keyboard);
        nav_key(sc, &in->nav);
    }
    buttons_or(&in->keyboard, &p->latched_keys);
    buttons_or(&in->nav, &p->latched_keys_nav);
    memset(&p->latched_keys, 0, sizeof p->latched_keys);
    memset(&p->latched_keys_nav, 0, sizeof p->latched_keys_nav);
    in->menu = p->latched_menu;
    in->back = p->latched_back;
    in->key = p->latched_key;
    p->latched_menu = false;
    p->latched_back = false;
    p->latched_key = KEY_NONE;

    /* (Session 54) Each gamepad's inputs held, and what went down since
     * the last pump, through the controls; the d-pad and left stick also
     * work the Esc screen whatever they are bound to. */
    for (int i = 0; i < PLATFORM_PADS; i++) {
        PadSlot *s = &p->slot[i];
        if (s->pad == NULL && s->joy == NULL) {
            break;
        }
        PadHeld now;
        slot_held(s, &now);
        for (int k = 0; k < PAD_INPUTS && p->latched_pad_input == PAD_NONE;
             k++) {
            if (now.held[k] && !s->prev.held[k] && k != PAD_BACK) {
                p->latched_pad_input = k;
            }
        }
        s->prev = now;
        for (int k = 0; k < PAD_INPUTS; k++) {
            now.held[k] = now.held[k] || s->latched.held[k];
        }
        memset(&s->latched, 0, sizeof s->latched);
        actions_to_buttons(controls_pad_actions(&p->controls, &now),
                           &in->pad[i]);
        actions_to_buttons(pad_nav_actions(&now), &in->nav);
        in->pads = i + 1;
    }
    in->pad_input = p->latched_pad_input;
    p->latched_pad_input = PAD_NONE;
    return true;
}

/* The render-target copy of the frame at w x h, made (or remade when the
 * window changes size) on demand; linear, so the one non-integer step --
 * across, down to the aspect's width -- blends at most one column at each
 * pixel's edge. NULL if the renderer can't make targets: the caller draws
 * nearest. */
static SDL_Texture *scaled_target(Platform *p, int w, int h)
{
    if (p->scaled != NULL && p->scaled_w == w && p->scaled_h == h) {
        return p->scaled;
    }
    if (p->scaled != NULL) {
        SDL_DestroyTexture(p->scaled);
        p->scaled = NULL;
    }
    if (p->scaled_failed) {
        return NULL;
    }
    p->scaled = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_TARGET, w, h);
    if (p->scaled == NULL) {
        SDL_Log("no render target (%s); the aspect is drawn nearest",
                SDL_GetError());
        p->scaled_failed = true;
        return NULL;
    }
    SDL_SetTextureScaleMode(p->scaled, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(p->scaled, SDL_BLENDMODE_NONE);
    p->scaled_w = w;
    p->scaled_h = h;
    return p->scaled;
}

/* Where the frame goes in an output out_w x out_h (crt.c's
 * crt_sharp_layout, which the GPU's flat picture shares): up and down the
 * largest whole multiple of 224 that fits; across that many lines' worth
 * at the pixel aspect; the first step, nearest, to the next whole multiple
 * of 320 at or above that width. An output too small for one whole
 * multiple gets the frame fitted. */
static void frame_layout(const Platform *p, int out_w, int out_h,
                         SDL_FRect *dst, int *step_w, int *step_h)
{
    CrtSharp l;
    crt_sharp_layout(out_w, out_h, ASPECT_PAR[p->aspect], &l);
    dst->w  = (float)l.w;
    dst->h  = (float)l.h;
    dst->x  = (float)l.x;
    dst->y  = (float)l.y;
    *step_w = l.step_w;
    *step_h = l.step_h;
}

/* What the chain draws on an output of w x h: the CRT page's settings,
 * each switched off where this display can't have it (the rules the page
 * shows, crt.c); in nits by PEAK and WHITE on an HDR swapchain, else SDR.
 * False when neither the scanlines nor a mask is left: the flat picture. */
static bool crt_plan_now(Platform *p, int w, int h, CrtPlan *plan)
{
    CrtDisplay d;
    display_state(p, w, h, &d);
    double  par  = crt_pixel_aspect((int)p->aspect);
    bool    scan = p->crt.scanlines && crt_scanlines_why(&d, par) == CRT_OK;
    CrtMask mask = (p->crt.mask != CRT_MASK_OFF
                    && crt_mask_why(&d, par, p->crt.mask) == CRT_OK)
                 ? p->crt.mask : CRT_MASK_OFF;
    if (!scan && mask == CRT_MASK_OFF) {
        return false;
    }
    memset(plan, 0, sizeof *plan);
    plan->out_w         = w;
    plan->out_h         = h;
    plan->par           = par;
    plan->set           = p->crt;
    plan->set.scanlines = scan;
    plan->set.mask      = mask;
    plan->encode        = composition_encode(p->composition);
    plan->hdr_out       = plan->encode != CRT_ENCODE_SRGB;
    plan->scrgb_nits    = scrgb_unit_nits(&d);
    plan->peak_nits     = (p->crt.peak_nits > 0) ? p->crt.peak_nits
                                                 : crt_peak_auto(&d);
    crt_plan_resolve(plan);
    return true;
}

/* The flat picture's encode: the swapchain's, its white the HDR page's
 * WHITE. */
static void flat_encode(Platform *p, CrtEncodeParams *e)
{
    CrtDisplay d;
    display_state(p, 0, 0, &d);
    crt_encode_params(composition_encode(p->composition),
                      (p->crt.white_nits > 0) ? p->crt.white_nits
                                              : CRT_WHITE_DEFAULT,
                      scrgb_unit_nits(&d), e);
}

/* One stderr line when what is drawn changes. */
static void say_chain(Platform *p, const CrtPlan *plan, int w, int h)
{
    static const char *const MASK[CRT_MASK_COUNT] = {
        "no mask", "aperture grille", "slot mask", "shadow mask"
    };
    static const char *const SUB[CRT_SUBPIXELS_COUNT] = {
        "RGB", "BGR", "OTHER"
    };
    char line[sizeof p->crt_said];
    char mode[96];
    int  white = (p->crt.white_nits > 0) ? p->crt.white_nits
                                         : CRT_WHITE_DEFAULT;
    if (p->composition == SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084) {
        snprintf(mode, sizeof mode, "HDR10 PQ, WHITE %d", white);
    } else if (p->composition
               == SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR) {
        CrtDisplay d;
        display_state(p, 0, 0, &d);
        snprintf(mode, sizeof mode, "scRGB (1.0 = %.0f nits), WHITE %d",
                 scrgb_unit_nits(&d), white);
    } else if (p->crt.hdr) {
        CrtDisplay d;
        display_state(p, 0, 0, &d);
        snprintf(mode, sizeof mode, "SDR (HDR ON, but %s)",
                 d.hdr ? "no HDR swapchain the window offers"
                       : "the window has no HDR");
    } else {
        snprintf(mode, sizeof mode, "SDR");
    }
    if (plan == NULL) {
        snprintf(line, sizeof line, "crt: off, the flat picture, %dx%d, %s",
                 w, h, mode);
    } else if (plan->hdr_out) {
        snprintf(line, sizeof line,
                 "crt: %s, %s (%s), %dx%d, k %d, triad %d, %s, PEAK %.0f, "
                 "scan depth %.2f, mask depth %.2f",
                 plan->set.scanlines ? "scanlines" : "no scanlines",
                 MASK[plan->set.mask], SUB[plan->set.subpixels], w, h,
                 plan->k, plan->triad, mode, plan->peak_nits,
                 plan->scan_depth, plan->mask_depth);
    } else {
        snprintf(line, sizeof line,
                 "crt: %s, %s (%s), %dx%d, k %d, triad %d, %s",
                 plan->set.scanlines ? "scanlines" : "no scanlines",
                 MASK[plan->set.mask], SUB[plan->set.subpixels], w, h,
                 plan->k, plan->triad, mode);
    }
    if (strcmp(line, p->crt_said) != 0) {
        snprintf(p->crt_said, sizeof p->crt_said, "%s", line);
        fprintf(stderr, "%s\n", line);
    }
}

/* The SDL_GPU chain. True when it waited on the display (the swapchain's
 * vsync), false when there was nothing to draw into (a minimised window)
 * and the caller must pace. The palette is CRAM's 64 words and the Esc
 * screen's 16; an index past them is taken as the renderer takes it. */
static bool present_gpu(Platform *p, const u8 *indices, const u16 *cram,
                        const u16 *overlay)
{
    Cram palette[FB_INDICES];
    for (int i = 0; i < FB_INDICES; i++) {
        if (i < VIDEO_COLOURS) {
            palette[i] = cram[i];
        } else {
            palette[i] = (overlay != NULL) ? overlay[i - OVERLAY_BASE] : 0;
        }
    }
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u8 v = indices[i];
        p->frame[i] = (v < FB_INDICES) ? v : (u8)(v & (VIDEO_COLOURS - 1));
    }
    gpu_follow_hdr(p);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(p->gpu);
    if (cmd == NULL) {
        return false;
    }
    SDL_GPUTexture *swap = NULL;
    Uint32          w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, p->window, &swap, &w,
                                               &h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    if (swap == NULL) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }
    CrtPlan plan;
    bool    crt = crt_plan_now(p, (int)w, (int)h, &plan);
    if (!crt_gpu_upload(p->crt_gpu, cmd, p->frame, palette,
                        crt ? &plan : NULL)) {
        crt = false;
    }
    say_chain(p, crt ? &plan : NULL, (int)w, (int)h);
    if (crt) {
        crt_gpu_draw_crt(p->crt_gpu, cmd, swap);
    } else {
        CrtSharp        l;
        CrtEncodeParams e;
        crt_sharp_layout((int)w, (int)h, ASPECT_PAR[p->aspect], &l);
        flat_encode(p, &e);
        crt_gpu_draw_sharp(p->crt_gpu, cmd, swap, &l, &e);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

/* The SDL_Renderer fallback: the flat picture. */
static void present_renderer(Platform *p, const u8 *indices,
                             const u16 *cram, const u16 *overlay)
{
    /* The 64 CRAM words and the menu's 16 through the DAC ladder once a
     * frame, not once a pixel. */
    u32 lut[OVERLAY_BASE + OVERLAY_COLOURS];
    for (int i = 0; i < OVERLAY_BASE + OVERLAY_COLOURS; i++) {
        u16 word = 0;
        if (i < VIDEO_COLOURS) {
            word = cram[i];
        } else if (overlay != NULL) {
            word = overlay[i - OVERLAY_BASE];
        }
        u8 rgb[3];
        cram_rgb(word, rgb);
        lut[i] = ((u32)rgb[0] << 16) | ((u32)rgb[1] << 8) | (u32)rgb[2];
    }
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u8 v = indices[i];
        p->pixels[i] = lut[(v < OVERLAY_BASE + OVERLAY_COLOURS)
                           ? v : (v & (VIDEO_COLOURS - 1))];
    }
    SDL_UpdateTexture(p->texture, NULL, p->pixels,
                      SCREEN_W * (int)sizeof(u32));

    /* The whole-multiple step goes to its own target first, so the window
     * is cleared and drawn in one go after it. At 1:1 (or with no render
     * targets) the frame goes straight to the window, nearest. */
    int out_w = 0, out_h = 0;
    SDL_GetRenderOutputSize(p->renderer, &out_w, &out_h);
    SDL_FRect    dst    = { 0.0f, 0.0f, 0.0f, 0.0f };
    SDL_Texture *source = p->texture;
    if (out_w > 0 && out_h > 0) {
        int step_w, step_h;
        frame_layout(p, out_w, out_h, &dst, &step_w, &step_h);
        if ((float)step_w != dst.w || (float)step_h != dst.h) {
            SDL_Texture *scaled = scaled_target(p, step_w, step_h);
            if (scaled != NULL) {
                SDL_SetRenderTarget(p->renderer, scaled);
                SDL_RenderTexture(p->renderer, p->texture, NULL, NULL);
                SDL_SetRenderTarget(p->renderer, NULL);
                source = scaled;
            }
        }
    }
    SDL_RenderClear(p->renderer);
    if (dst.w > 0.0f && dst.h > 0.0f) {
        SDL_RenderTexture(p->renderer, source, NULL, &dst);
    }
    SDL_RenderPresent(p->renderer);
}

/* The frame to the display: true when presents wait for its refresh
 * (vsync), false when nothing does (no vsync, or a minimised window with
 * nothing to draw into): then the pace's clock sleeps. */
static bool draw(Platform *p, const u8 *indices, const u16 *cram,
                 const u16 *overlay)
{
    if (p->gpu != NULL) {
        return present_gpu(p, indices, cram, overlay);
    }
    present_renderer(p, indices, cram, overlay);
    return p->vsync;
}

double platform_now(Platform *p)
{
    (void)p;
    return (double)SDL_GetTicksNS() / 1e9;
}

/* (Session 63) The frame, as often as the pace says: not at all when the
 * clock is a frame ahead, again at each refresh before the next is due,
 * then a sleep to it when nothing waited (pace.h). */
void platform_present(Platform *p, const u8 *indices, const u16 *cram,
                      const u16 *overlay)
{
    if (pace_frame(&p->pace, platform_now(p))) {
        for (;;) {
            double before = platform_now(p);
            bool vsync = draw(p, indices, cram, overlay);
            double now = platform_now(p);
            PaceStep step = pace_presented(&p->pace, now, vsync,
                                           now - before >= PACE_WAITED);
            if (step == PACE_SLEEP) {
                double left = p->pace.until - platform_now(p);
                if (left > 0.0) {
                    SDL_DelayPrecise((Uint64)(left * 1e9));
                }
                break;
            }
            if (step != PACE_AGAIN) {
                break;
            }
        }
    }
    char line[256];
    if (p->pace_log && pace_report(&p->pace, line, sizeof line)) {
        fprintf(stderr, "%s\n", line);
    }
}

void platform_show(Platform *p, const u8 *indices, const u16 *cram,
                   const u16 *overlay)
{
    draw(p, indices, cram, overlay);
    pace_aside(&p->pace, platform_now(p));
}

void platform_pace_held(Platform *p, double seconds)
{
    pace_held(&p->pace, seconds);
}

void platform_pace_slow(Platform *p, double seconds)
{
    pace_slow(&p->pace, seconds);
}

/* The CRT page's settings: the SDL_GPU chain draws them from the next
 * frame (present_gpu); the SDL_Renderer fallback ignores them. */
void platform_set_crt(Platform *p, const CrtSettings *s)
{
    p->crt = *s;
}

/* What the CRT page judges its values against (crt.h, CrtDisplay). The
 * output is the window's in pixels; fullscreen's size is the desktop
 * mode's in pixels. The window is taken as scaled by the desktop when its
 * pixel density falls short of the desktop mode's: its buffer is smaller
 * than the panel area it covers, so the compositor scales it up. Whether
 * the desktop's own mode is the panel's isn't asked here: SDL has no call
 * for the panel's native size, and the largest fullscreen mode is not it
 * everywhere (macOS lists scaled modes above the native one), so a false
 * DESKTOP SCALED would lock the masks out. HDR is the window's own
 * properties: enabled, the headroom over SDR white, and SDR white in the
 * linear colourspace's units. */
void platform_display_info(Platform *p, CrtDisplay *out)
{
    int w = 0, h = 0;
    output_size(p, &w, &h);
    display_state(p, w, h, out);
}

/* platform_display_info for an output of out_w x out_h: the menu asks
 * with the window's, present_gpu with the swapchain texture's. */
static void display_state(Platform *p, int out_w, int out_h,
                          CrtDisplay *out)
{
    crt_display_default(out);
    if (out_w > 0 && out_h > 0) {
        out->out_w = out_w;
        out->out_h = out_h;
    }
    out->fullscreen = p->fullscreen;

    SDL_DisplayID          id   = SDL_GetDisplayForWindow(p->window);
    const SDL_DisplayMode *desk = SDL_GetDesktopDisplayMode(id);
    if (desk != NULL) {
        float density = (desk->pixel_density > 0.0f) ? desk->pixel_density
                                                     : 1.0f;
        out->full_w = (int)(desk->w * density + 0.5f);
        out->full_h = (int)(desk->h * density + 0.5f);
        float win_density = SDL_GetWindowPixelDensity(p->window);
        if (!p->fullscreen && win_density > 0.0f
            && fabsf(win_density - density) > 0.01f) {
            out->window_scaled = true;
        }
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(p->window);
    out->hdr       = SDL_GetBooleanProperty(
        props, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
    out->headroom  = SDL_GetFloatProperty(
        props, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f);
    out->sdr_white = SDL_GetFloatProperty(
        props, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f);
    /* SDR white in nits. Windows's linear space is scRGB, whose 1.0 is 80
     * nits. On Wayland and macOS SDL's SDR white is 1.0 and its nits are
     * not said (PS2's ProArt P16 under Wayland: headroom 1.624, SDR white
     * 1.000), so it is taken as BT.2408's reference white, 203 nits. */
#ifdef _WIN32
    out->sdr_nits = out->sdr_white * 80.0f;
#else
    out->sdr_nits = out->sdr_white * 203.0f;
#endif
}
