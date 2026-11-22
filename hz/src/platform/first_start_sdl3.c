/* (Session 78) The first start, SDL3's (platform.h, docs/first_start.md),
 * after the PS2 port's build 186.
 *
 * The game's files are made from the player's own cartridge, in the game
 * itself (src/extract/: no Python, no library but the game's own code).
 * This is the window that asks for it: an image dropped on it or chosen in
 * the system's file dialog, the extraction's steps run one a frame and
 * shown as they go, and what went wrong when it could not. The game's own
 * font is one of the files to be made, so the window writes in SDL's
 * built-in 8x8 face (SDL_RenderDebugText), on an SDL_Renderer of its own;
 * it closes before the game's window opens. */
#include "platform.h"

#include "../extract/extract.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define change_dir _chdir
#else
#include <unistd.h>
#define change_dir chdir
#endif

/* The window: 80 columns and 32 rows of the 8x8 face, drawn twice the size
 * (whole multiples when it is made bigger). */
#define FS_W        640
#define FS_H        360
#define FS_SCALE    2
#define FS_CELL     8
#define FS_ROW_H    11                      /* a line, with its leading */
#define FS_ROWS     (FS_H / FS_ROW_H)
#define FS_COLS     (FS_W / FS_CELL)
#define FS_MARGIN   4                       /* columns */
#define FS_TEXT     (FS_COLS - 2 * FS_MARGIN)

#define FS_PATH     1024
#define FS_ARGS     32

typedef enum {
    STATE_ASK,          /* waiting for the image */
    STATE_RUN,          /* the files being made */
    STATE_FAILED,       /* it could not: why, and ask again */
    STATE_DONE,
    STATE_LEFT          /* the player closed it */
} State;

typedef struct {
    State         state;
    SDL_Window   *window;
    SDL_Renderer *renderer;

    char assets[FS_PATH];               /* the folder the files go in */
    char cartridge[FS_PATH];            /* the image they are made from */
    char why[1024];                     /* what went wrong */

    /* The run. */
    u8         *rom;
    u32         rom_size;
    HzExtract  *ex;
    int         step;                   /* the steps done */
    int         steps;

    /* The file dialog, whose answer may come on another thread. */
    SDL_AtomicInt dialog_open;
    SDL_AtomicInt dialog_done;
    char          dialog_path[FS_PATH];
} FirstStart;

static bool path_is(const char *path, SDL_PathType type)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == type;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *back  = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    return (slash != NULL) ? slash + 1 : path;
}

/* ---- the run ---- */

static void end_run(FirstStart *fs)
{
    if (fs->ex != NULL) {
        hz_ex_end(fs->ex);
        fs->ex = NULL;
    }
    free(fs->rom);
    fs->rom = NULL;
}

static void failed(FirstStart *fs, const char *why)
{
    fprintf(stderr, "first start: %s\n", why);
    if (why != fs->why) {
        SDL_snprintf(fs->why, sizeof fs->why, "%s", why);
    }
    end_run(fs);
    fs->state = STATE_FAILED;
    fs->cartridge[0] = '\0';
}

/* The image read and known, and the extraction begun: its steps run from
 * the window's loop (run_step). */
static void start_run(FirstStart *fs)
{
    fs->why[0] = '\0';
    fs->step   = 0;
    fs->steps  = hz_ex_steps();
    fprintf(stderr, "first start: %s\n", fs->cartridge);
    if (!hz_ex_read_image(fs->cartridge, &fs->rom, &fs->rom_size, fs->why,
                          sizeof fs->why)
        || !hz_ex_is_cartridge(fs->rom, fs->rom_size, fs->why,
                               sizeof fs->why)) {
        failed(fs, fs->why);
        return;
    }
    fs->ex = hz_ex_begin(fs->rom, fs->rom_size, fs->assets);
    if (fs->ex == NULL) {
        failed(fs, "out of memory");
        return;
    }
    fs->state = STATE_RUN;
}

/* The next step; after the last, the files' mark put in place. */
static void run_step(FirstStart *fs)
{
    if (!hz_ex_step(fs->ex, fs->step)) {
        char why[sizeof fs->why];
        SDL_snprintf(why, sizeof why, "Making %s: %s",
                     hz_ex_step_name(fs->step), hz_ex_error(fs->ex));
        failed(fs, why);
        return;
    }
    fs->step++;
    if (fs->step < fs->steps) {
        return;
    }
    if (!hz_ex_finish(fs->ex)) {
        failed(fs, hz_ex_error(fs->ex));
        return;
    }
    fprintf(stderr, "first start: %d files made in %s/\n",
            hz_ex_files(fs->ex), fs->assets);
    end_run(fs);
    fs->state = STATE_DONE;
}

/* ---- the image given ---- */

/* A file dropped or chosen: the files are made from it at once. */
static void offer(FirstStart *fs, const char *path)
{
    if (fs->state == STATE_RUN || path == NULL || path[0] == '\0') {
        return;
    }
    SDL_snprintf(fs->cartridge, sizeof fs->cartridge, "%s", path);
    start_run(fs);
}

static void SDLCALL dialog_answer(void *userdata, const char *const *files,
                                  int filter)
{
    (void)filter;
    FirstStart *fs = userdata;
    fs->dialog_path[0] = '\0';
    if (files != NULL && files[0] != NULL) {
        SDL_snprintf(fs->dialog_path, sizeof fs->dialog_path, "%s", files[0]);
    } else if (files == NULL) {
        fprintf(stderr, "first start: the file dialog failed: %s\n",
                SDL_GetError());
    }
    SDL_SetAtomicInt(&fs->dialog_done, 1);
}

static const SDL_DialogFileFilter CARTRIDGE_FILTERS[] = {
    { "Cartridge images (.bin .md .gen .smd .zip)", "bin;md;gen;smd;zip" },
    { "All files", "*" },
};

static void choose(FirstStart *fs)
{
    if (SDL_GetAtomicInt(&fs->dialog_open) != 0 || fs->state == STATE_RUN) {
        return;
    }
    SDL_SetAtomicInt(&fs->dialog_open, 1);
    SDL_SetAtomicInt(&fs->dialog_done, 0);
    SDL_ShowOpenFileDialog(dialog_answer, fs, fs->window, CARTRIDGE_FILTERS,
                           (int)SDL_arraysize(CARTRIDGE_FILTERS), NULL,
                           false);
}

/* ---- the command line, as UTF-8 ---- */

#ifdef _WIN32
/* Windows hands main() its arguments in the ANSI code page, which cannot
 * hold every file name; they are read again from the wide command line
 * by CommandLineToArgvW's rules, the program's own name skipped. */
static int wide_arguments(char out[][FS_PATH], int max)
{
    const wchar_t *p = GetCommandLineW();
    if (p == NULL) {
        return 0;
    }
    if (*p == L'"') {                       /* the program: up to a quote */
        p++;
        while (*p != L'\0' && *p != L'"') {
            p++;
        }
        if (*p == L'"') {
            p++;
        }
    } else {
        while (*p != L'\0' && *p != L' ' && *p != L'\t') {
            p++;
        }
    }
    int count = 0;
    static wchar_t arg[FS_PATH];
    for (;;) {
        while (*p == L' ' || *p == L'\t') {
            p++;
        }
        if (*p == L'\0' || count == max) {
            break;
        }
        size_t n      = 0;
        bool   quoted = false;
        while (*p != L'\0' && (quoted || (*p != L' ' && *p != L'\t'))) {
            size_t slashes = 0;
            while (*p == L'\\') {
                slashes++;
                p++;
            }
            if (*p == L'"') {
                for (size_t s = 0; s < slashes / 2 && n + 1 < FS_PATH; s++) {
                    arg[n++] = L'\\';
                }
                if (slashes % 2 == 1) {
                    if (n + 1 < FS_PATH) {
                        arg[n++] = L'"';
                    }
                } else if (quoted && p[1] == L'"') {
                    if (n + 1 < FS_PATH) {
                        arg[n++] = L'"';
                    }
                    p++;
                } else {
                    quoted = !quoted;
                }
                p++;
            } else {
                for (size_t s = 0; s < slashes && n + 1 < FS_PATH; s++) {
                    arg[n++] = L'\\';
                }
                if (*p != L'\0' && (quoted || (*p != L' ' && *p != L'\t'))) {
                    if (n + 1 < FS_PATH) {
                        arg[n++] = *p;
                    }
                    p++;
                }
            }
        }
        arg[n] = L'\0';
        if (WideCharToMultiByte(CP_UTF8, 0, arg, -1, out[count], FS_PATH,
                                NULL, NULL) > 0) {
            count++;
        }
    }
    return count;
}
#endif

static bool is_absolute(const char *path)
{
#ifdef _WIN32
    return path[0] == '\\' || path[0] == '/'
        || (path[0] != '\0' && path[1] == ':');
#else
    return path[0] == '/';
#endif
}

/* PATH into OUT, from the folder the game was started in when it is
 * relative: platform_find_home may have moved since. */
static void from_start_dir(const char *path, char *out)
{
    const char *dir = platform_start_dir();         /* ends in a separator */
    if (is_absolute(path) || dir == NULL) {
        SDL_snprintf(out, FS_PATH, "%s", path);
    } else {
        SDL_snprintf(out, FS_PATH, "%s%s", dir, path);
    }
}

/* The first file named on the command line (main.c's FILES: its arguments
 * that are no option), into OUT; false when none was. On Windows the wide
 * command line is read again and its options passed over as main() passes
 * them: --fullscreen alone, any other --NAME with the value after it. */
static bool given_file(int nfiles, char **files, char *out)
{
    if (nfiles <= 0) {
        return false;
    }
#ifdef _WIN32
    (void)files;
    static char args[FS_ARGS][FS_PATH];
    int n = wide_arguments(args, FS_ARGS);
    for (int i = 0; i < n; i++) {
        if (strncmp(args[i], "--", 2) != 0) {
            from_start_dir(args[i], out);
            return true;
        }
        if (strcmp(args[i], "--fullscreen") != 0) {
            i++;
        }
    }
    return false;
#else
    from_start_dir(files[0], out);
    return true;
#endif
}

/* ---- the screen ---- */

static void colour(SDL_Renderer *r, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
}

/* TEXT from column FS_MARGIN of row ROW, wrapped at spaces to FS_TEXT
 * columns, (indent) INDENT more for every line but the first; the next
 * free row. Only ASCII is in the face: other letters show as it draws
 * them. */
static int text(SDL_Renderer *r, int row, int indent, const char *s)
{
    char line[FS_COLS + 1];
    int  first = 1;
    while (*s != '\0') {
        int width = FS_TEXT - (first ? 0 : indent);
        int len   = (int)strlen(s);
        int cut   = len;
        const char *nl = strchr(s, '\n');
        if (nl != NULL && nl - s <= width) {
            cut = (int)(nl - s);
        } else if (len > width) {
            cut = width;
            while (cut > 0 && s[cut] != ' ') {
                cut--;
            }
            if (cut == 0) {
                cut = width;
            }
        }
        memcpy(line, s, (size_t)cut);
        line[cut] = '\0';
        SDL_RenderDebugText(r,
                            (float)((FS_MARGIN + (first ? 0 : indent))
                                    * FS_CELL),
                            (float)(row * FS_ROW_H), line);
        row++;
        s += cut;
        while (*s == ' ' || *s == '\n') {
            s++;
        }
        first = 0;
    }
    return row;
}

static void bar(SDL_Renderer *r, int row, int step, int steps)
{
    SDL_FRect box  = { (float)(FS_MARGIN * FS_CELL), (float)(row * FS_ROW_H),
                       (float)(FS_TEXT * FS_CELL), (float)FS_CELL };
    SDL_FRect fill = box;
    fill.w = (steps > 0) ? box.w * (float)step / (float)steps : 0.0f;
    colour(r, 0x30, 0x60, 0xC0);
    SDL_RenderFillRect(r, &fill);
    colour(r, 0xA0, 0xB0, 0xD0);
    SDL_RenderRect(r, &box);
}

static void draw(FirstStart *fs)
{
    SDL_Renderer *r = fs->renderer;
    colour(r, 0x08, 0x10, 0x30);
    SDL_RenderClear(r);
    int row = 3;
    colour(r, 0xFF, 0xE0, 0x80);
    row = text(r, row, 0, "HERZOG ZWEI") + 2;
    colour(r, 0xE8, 0xE8, 0xE8);
    if (fs->state == STATE_RUN) {
        row = text(r, row, 0, "Making the game's files from");
        row = text(r, row, 2, base_name(fs->cartridge)) + 2;
        bar(r, row, fs->step, fs->steps);
        row += 2;
        char line[200];
        SDL_snprintf(line, sizeof line, "step %d of %d: %s", fs->step + 1,
                     fs->steps, hz_ex_step_name(fs->step));
        text(r, row, 0, line);
    } else {
        row = text(r, row, 0,
                   "The game's files are not here yet. They are made once, "
                   "from your own cartridge image of Herzog Zwei for the USA "
                   "-- the one often named \"Herzog Zwei (U) [!]\": a .bin, "
                   ".md, .gen or .smd file, or a .zip holding one. Nothing is "
                   "downloaded or sent anywhere.") + 2;
        if (fs->state == STATE_FAILED) {
            colour(r, 0xFF, 0x90, 0x80);
            row = text(r, row, 0, "That did not work:");
            colour(r, 0xB0, 0xB0, 0xB0);
            row = text(r, row, 2, fs->why) + 1;
        }
        colour(r, 0xFF, 0xFF, 0xFF);
        row = text(r, row, 2, "Drop the image onto this window,");
        text(r, row, 2, "or press Enter (or click) to choose it.");
        colour(r, 0x90, 0x90, 0x90);
        text(r, FS_ROWS - 4, 0, "Escape quits.");
    }
    SDL_RenderPresent(r);
}

/* ---- the window ---- */

static bool open_window(FirstStart *fs, const char *title)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "first start: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, FS_W * FS_SCALE, FS_H * FS_SCALE,
                                     SDL_WINDOW_RESIZABLE, &fs->window,
                                     &fs->renderer)) {
        fprintf(stderr, "first start: no window: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetRenderLogicalPresentation(fs->renderer, FS_W, FS_H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(fs->renderer, 1);
    SDL_RaiseWindow(fs->window);
    return true;
}

static void close_window(FirstStart *fs)
{
    SDL_DestroyRenderer(fs->renderer);
    SDL_DestroyWindow(fs->window);
    fs->renderer = NULL;
    fs->window   = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

/* The window's loop until the files are made or the player leaves. */
static bool run(FirstStart *fs)
{
    SDL_WindowID mine = SDL_GetWindowID(fs->window);
    while (fs->state != STATE_DONE && fs->state != STATE_LEFT) {
        SDL_Event ev;
        /* a step a frame while making them; else wait for the player */
        bool have = (fs->state == STATE_RUN) ? SDL_PollEvent(&ev)
                                             : SDL_WaitEventTimeout(&ev, 30);
        while (have) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: {
                    fs->state = STATE_LEFT;
                    break;
                }
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                    if (ev.window.windowID == mine) {
                        fs->state = STATE_LEFT;
                    }
                    break;
                }
                case SDL_EVENT_DROP_FILE: {
                    if (ev.drop.windowID == mine) {
                        offer(fs, ev.drop.data);
                    }
                    break;
                }
                case SDL_EVENT_KEY_DOWN: {
                    if (ev.key.windowID != mine || ev.key.repeat
                        || fs->state == STATE_RUN) {
                        break;
                    }
                    if (ev.key.key == SDLK_ESCAPE) {
                        fs->state = STATE_LEFT;
                    } else if (ev.key.key == SDLK_RETURN
                               || ev.key.key == SDLK_KP_ENTER
                               || ev.key.key == SDLK_SPACE) {
                        choose(fs);
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (ev.button.windowID == mine) {
                        choose(fs);
                    }
                    break;
                }
                default: {
                    break;
                }
            }
            have = SDL_PollEvent(&ev);
        }
        if (SDL_GetAtomicInt(&fs->dialog_done) != 0) {
            SDL_SetAtomicInt(&fs->dialog_done, 0);
            SDL_SetAtomicInt(&fs->dialog_open, 0);
            if (fs->state != STATE_LEFT) {
                offer(fs, fs->dialog_path);
            }
        }
        if (fs->state == STATE_RUN) {
            draw(fs);                       /* the step about to run */
            run_step(fs);
        } else if (fs->state != STATE_DONE && fs->state != STATE_LEFT) {
            draw(fs);
        }
    }
    if (fs->ex != NULL) {                   /* left while making them */
        fprintf(stderr, "first start: stopped; the files are not made\n");
        end_run(fs);
    }
    /* A dialog still open answers into nothing: wait for it to go. */
    while (SDL_GetAtomicInt(&fs->dialog_open) != 0
           && SDL_GetAtomicInt(&fs->dialog_done) == 0) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    return fs->state == STATE_DONE;
}

/* The files go beside the game, where it looks for them first
 * (platform_find_home): started from elsewhere (a shortcut, a terminal)
 * with no ASSETS there, the game's own folder is moved to. */
static void to_game_folder(const char *assets)
{
    if (is_absolute(assets) || path_is(assets, SDL_PATHTYPE_DIRECTORY)) {
        return;
    }
    const char *base = SDL_GetBasePath();   /* ends in a separator */
    if (base != NULL && change_dir(base) != 0) {
        fprintf(stderr, "first start: can't move to %s\n", base);
    }
}

bool platform_make_assets(const char *assets, int nfiles, char **files)
{
    fprintf(stderr, "first start: the game's files are not made yet "
                    "(%s/%s); asking for the cartridge\n", assets,
            HZ_MADE_FILE);
    to_game_folder(assets);
    FirstStart *fs = SDL_calloc(1, sizeof *fs);
    if (fs == NULL) {
        return false;
    }
    fs->state = STATE_ASK;
    SDL_snprintf(fs->assets, sizeof fs->assets, "%s", assets);
    if (!open_window(fs, "Herzog Zwei")) {
        SDL_free(fs);
        return false;
    }
    /* Named on the command line (dropped onto the program): used at once. */
    if (given_file(nfiles, files, fs->cartridge)) {
        start_run(fs);
    }
    bool made = run(fs);
    close_window(fs);
    SDL_free(fs);
    return made;
}
