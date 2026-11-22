#ifndef HZ_PLATFORM_H
#define HZ_PLATFORM_H

#include "../core/hz_types.h"
#include "../core/audio.h"
#include "../core/crt.h"
#include "../core/hz_vdp.h"
#include "../core/keys.h"
#include "../extract/extract.h"

/* Everything OS- and GPU-specific lives behind this interface (as in the
 * PS2 port, whose platform layer this follows). The core and game layers
 * include no platform headers, so the game builds and runs headless.
 *
 * What differs from PS2's: the frame is the VDP model's output (palette
 * indices 0-63 and the 64 CRAM words), and the input is two controllers'
 * worth -- the keyboard and up to two gamepads, handed over as they are;
 * which player each drives is main.c's choice, since it depends on the
 * game mode. The sound is src/core/audio.c's, which the platform only
 * pulls samples from (platform_attach_audio).
 *
 * The CRT chain is PS2's (src/core/crt.c, src/platform/crt_gpu.c, the
 * shaders; docs/crt.md): the SDL3 platform draws through SDL_GPU -- the
 * CRT page's scanlines and masks, SDR or HDR, else the flat picture --
 * and falls back to SDL_Renderer (the flat picture only) when SDL_GPU or
 * the compiled shaders can't be had.
 *
 * (Session 26) The Esc screen (src/game/sysmenu.c) is the port's own menu:
 * Escape, or a gamepad's Back, opens it (Input.menu) instead of quitting.
 * The keys and gamepad inputs that make the pad are the menu's CONTROLS
 * and BUTTONS (platform_set_controls); the menu also reads the fixed keys
 * and (session 54) every gamepad's d-pad and left stick in Input.nav, so
 * it can always be worked whatever is bound. It draws in
 * frame indices 64-79, with their colours handed to platform_present. */

#define PLATFORM_PADS 2

typedef struct {
    bool up, down, left, right;
    bool a, b, c, start;
    bool turbo;                 /* TURBO: the host presses B every other
                                   frame while it is held */
} Buttons;

/* How wide a pixel is drawn, as the pixel aspect ratio (width / height),
 * PS2's three: square; the 320 x 224 frame stretched to 4:3 (14:15); and
 * what an NTSC set drew in H40, 32:35 (the VDP's 6.71 MHz dot clock
 * against the 6.14 MHz a square pixel would need). */
typedef enum {
    ASPECT_SQUARE = 0,
    ASPECT_4_3    = 1,
    ASPECT_TV     = 2,
    ASPECT_COUNT
} AspectMode;

/* One pump's input. Each Buttons is what is held now, OR'd with anything
 * pressed and let go since the last pump, so a tap shorter than a frame
 * still shows for one frame (the pad itself is read once a frame, by
 * ReadJoypads). `pads` is how many gamepads are connected, in the order
 * they were. `keyboard` and `pad` go through the controls.
 *
 * The Esc screen's: `menu`, Escape or a gamepad's Back pressed since the
 * last pump; `nav`, the fixed keys held (arrows, Enter as Start) and
 * (session 54) every gamepad's d-pad and left stick (pad_nav_actions),
 * the same whatever the controls; `back`, Backspace pressed; `key` and
 * `pad_input`, the first key and gamepad input to go down since the last
 * pump (a scancode; a PAD_* input, core/keys.h: a button, a stick or
 * trigger passing half its travel, a mapless joystick's button or hat;
 * KEY_NONE / PAD_NONE), for binding. */
typedef struct {
    Buttons keyboard;
    Buttons pad[PLATFORM_PADS];
    int     pads;
    bool    menu;
    bool    back;
    Buttons nav;
    int     key;
    int     pad_input;
} Input;

/* The colours of the menu's frame indices: OVERLAY_BASE + 0..15. */
#define OVERLAY_BASE   64
#define OVERLAY_COLOURS 16

typedef struct Platform Platform;

/* (Session 53) main.c's first call. On Windows the packaged game (make
 * dist) has no console of its own: started from a terminal it writes
 * stdout and stderr there (none from Explorer). The headless platform
 * does nothing. */
void platform_startup(void);

/* The folder the game reads and writes in (assets/, shaders/, saves/,
 * mods/): the current one when it holds `dir`, else the program's own
 * when that one does, so the packaged game finds its files from a
 * shortcut or another folder. The headless platform stays put (the
 * checks run it from the tree). */
void platform_find_home(const char *dir);

/* A message the player must see (the game can't go on): stderr, and on
 * Windows a message box too when there is no console to show it. */
void platform_alert(const char *text);

/* (Session 78) The first start (docs/first_start.md). A copy of the game
 * whose files are not made yet -- no ASSETS/HZ_MADE_FILE, which the
 * extractor puts in place last (src/extract/extract.h) -- asks for the
 * cartridge image they are made from, in a window of its own (an image
 * dropped on it, or chosen), and makes them into ASSETS from it in the
 * game itself, its steps shown as they go. `files` (`nfiles` of them: the
 * command line's arguments that are no option) were named on the command
 * line -- an image dropped onto the program -- and the first is used at
 * once. True when the files are there now; false when the player left it
 * (the headless platform: at once, saying on stderr how to make them).
 * Before platform_create. src/platform/first_start_sdl3.c. */
#define HZ_MADE_FILE EX_MADE_FILE
bool platform_make_assets(const char *assets, int nfiles, char **files);

/* (Session 78) The folder the game was started in, before
 * platform_find_home moved it (UTF-8; NULL if unknown): a file named on
 * the command line by a relative path is found from there. The SDL3
 * platform's only (platform_startup notes it). */
const char *platform_start_dir(void);

/* `scale` is the window's starting size in whole multiples of 320 x 224
 * (0: the platform's default). */
Platform *platform_create(const char *title, int scale);
void      platform_destroy(Platform *p);

/* Services the window and fills in the input. Returns false when the user
 * has asked to quit (the window closed). The SDL3 platform takes its own
 * keys first: F11 (or Alt+Enter) toggles fullscreen, F10 steps the
 * aspect; Escape is Input.menu. */
bool platform_pump(Platform *p, Input *in);

/* Hands over a finished frame: SCREEN_W x SCREEN_H palette indices and the
 * CRAM they index (0-63), and for indices 64-79 the colours in `overlay`
 * (16 CRAM words; NULL when nothing uses them). (Session 63) Paces the
 * game (src/core/pace.h): returns when the next frame is due -- at the
 * display's next refresh when it runs within 1% of the game's 59.92 Hz,
 * else on the clock, the frame shown at every refresh before the next is
 * due, or not at all when the clock is a frame ahead (until session 63 it
 * waited for the display's refresh, whatever its rate). The SDL3
 * platform writes a line of what it did every 5 s with HZ_PACE=1. */
void platform_present(Platform *p, const u8 *indices, const u16 *cram,
                      const u16 *overlay);

/* (Session 63) A frame shown outside the game's (a notice over the last
 * one while the network game waits): once, unpaced. The headless
 * platform takes it as any presented frame. */
void platform_show(Platform *p, const u8 *indices, const u16 *cram,
                   const u16 *overlay);

/* (Session 63) Seconds from some fixed moment on the platform's fine
 * clock (the headless platform's: its frames at the game's rate). */
double platform_now(Platform *p);

/* (Session 63) The network game's (pace.h's pace_held and pace_slow):
 * the game stood still `seconds` waiting for the rival, not owed; hold
 * the game back `seconds` more, the balance. The headless platform has
 * no clock to keep and ignores them. */
void platform_pace_held(Platform *p, double seconds);
void platform_pace_slow(Platform *p, double seconds);

void platform_set_aspect(Platform *p, AspectMode mode);
AspectMode platform_aspect(const Platform *p);
bool platform_fullscreen(const Platform *p);

/* Which keys and gamepad buttons are the pad's (core/keys.h). */
void platform_set_controls(Platform *p, const Controls *c);

/* (Session 54) The rumble (game/rumble.h): gamepad `pad`'s motors from
 * now on, 0 to $FFFF each -- `low` the big, slow one, `high` the small,
 * quick one (SDL3's low and high frequency rumble); 0 and 0 stop them.
 * main.c sets every connected pad's every frame. The SDL3 platform sends
 * a change at once (to a 32nd of the range) and a level held again every
 * 6 frames, each good for 250 ms, so the motors stop by themselves when
 * the frames do; a gamepad without motors ignores it. The headless
 * platform logs each change to HZ_RUMBLE. */
void platform_rumble(Platform *p, int pad, u16 low, u16 high);

/* The name gamepad `pad` gives itself, as the system has it ("PS3
 * Controller"); NULL when there is no gamepad there. */
const char *platform_pad_name(Platform *p, int pad);

/* (Session 54) Whose labels gamepad `pad` has (core/keys.h's PadStyle:
 * SDL3's gamepad type, JOYSTICK for one SDL has no mapping for);
 * PAD_STYLE_NONE when there is no gamepad there. The headless platform's
 * scripted gamepads are HZ_PAD_STYLE's, none by default. */
PadStyle platform_pad_style(Platform *p, int pad);

/* Hands the sound to the platform: the SDL3 platform opens a stream that
 * pulls hz_audio_render on SDL's audio thread (none to be had: a message,
 * and the game goes on silent); the headless one renders each presented
 * frame's share into HZ_WAV, if set. NULL: silent. Once, before the
 * first frame; platform_destroy stops it before the audio may go. */
void platform_attach_audio(Platform *p, HzAudio *audio);
void platform_set_fullscreen(Platform *p, bool on);

/* (PS2's) What the CRT page needs to know about where the frame goes
 * (crt.h, CrtDisplay): the output's pixels, fullscreen's, whether the
 * desktop scales the window, SDL's HDR state. The headless platform
 * answers from HZ_DISPLAY (crt_display_parse's words), else the default
 * window. */
void platform_display_info(Platform *p, CrtDisplay *out);

/* (PS2's) The CRT page's settings: the SDL3 platform's SDL_GPU chain
 * draws them from the next frame, each switched off where the display
 * can't have it; its SDL_Renderer fallback and the headless platform
 * ignore them. */
void platform_set_crt(Platform *p, const CrtSettings *s);

#endif /* HZ_PLATFORM_H */
