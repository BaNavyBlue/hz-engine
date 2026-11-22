/* (Session 78) The extraction's steps (extract.h), in hz_extract.py's
 * order: the eight maps, the tiles, the data, the title, the sound, the
 * endings, the demos, then the views drawn from what they wrote. */
#include "extract.h"

#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir((p), 0777)
#endif

struct HzExtract {
    HzEx x;
};

static const char *const STEP_NAMES[] = {
    "map 0, ABGRUND", "map 1, VULKAN", "map 2, LOCH", "map 3, STRAND",
    "map 4, STADT", "map 5, EISFREI", "map 6, WALDUNG", "map 7, OASE",
    "the sprites and tiles", "the units and tables", "the title and menus",
    "the music and sounds", "the endings", "the demos", "the views",
};

#define STEPS ((int)(sizeof STEP_NAMES / sizeof STEP_NAMES[0]))

int hz_ex_steps(void)
{
    return STEPS;
}

const char *hz_ex_step_name(int k)
{
    return (k >= 0 && k < STEPS) ? STEP_NAMES[k] : "";
}

HzExtract *hz_ex_begin(const u8 *rom, u32 size, const char *root)
{
    HzExtract *e = calloc(1, sizeof *e);
    if (e == NULL) {
        return NULL;
    }
    e->x.rom  = rom;
    e->x.size = size;
    snprintf(e->x.root, sizeof e->x.root, "%s", root);
    size_t n = strlen(e->x.root);
    while (n > 1 && (e->x.root[n - 1] == '/' || e->x.root[n - 1] == '\\')) {
        e->x.root[--n] = '\0';
    }
    make_dir(e->x.root);
    if (size != EX_ROM_SIZE) {
        ex_fail(&e->x, "the image is %u bytes, not %u", (unsigned)size,
                (unsigned)EX_ROM_SIZE);
    }
    return e;
}

bool hz_ex_step(HzExtract *e, int k)
{
    HzEx *x = &e->x;
    if (x->failed) {
        return false;
    }
    bool ok;
    if (k >= 0 && k < 8) {
        ok = ex_map(x, k);
    } else {
        switch (k) {
            case 8:  ok = ex_tiles(x);  break;
            case 9:  ok = ex_data(x);   break;
            case 10: ok = ex_title(x);  break;
            case 11: ok = ex_sound(x);  break;
            case 12: ok = ex_ending(x); break;
            case 13: ok = ex_demos(x);  break;
            case 14: ok = ex_views(x);  break;
            default: ok = ex_fail(x, "no step %d", k); break;
        }
    }
    if (!ok && !x->failed) {
        ex_fail(x, "%s: failed", hz_ex_step_name(k));
    }
    return ok && !x->failed;
}

const char *hz_ex_error(const HzExtract *e)
{
    return e->x.failed ? e->x.err : "";
}

int hz_ex_files(const HzExtract *e)
{
    return e->x.files;
}

bool hz_ex_finish(HzExtract *e)
{
    HzEx *x = &e->x;
    if (x->failed) {
        return false;
    }
    if (!x->made_held) {
        return true;                /* a run of some steps only */
    }
    char from[EX_PATH + 48], to[EX_PATH + 32];
    snprintf(to, sizeof to, "%s/%s", x->root, EX_MADE_FILE);
    snprintf(from, sizeof from, "%s.part", to);
    remove(to);                     /* Windows renames onto nothing */
    if (rename(from, to) != 0) {
        return ex_fail(x, "%s: can't be renamed %s", from, to);
    }
    x->made_held = false;
    return true;
}

void hz_ex_end(HzExtract *e)
{
    free(e);
}

bool hz_ex_all(const u8 *rom, u32 size, const char *root, char *err,
               size_t errlen)
{
    HzExtract *e = hz_ex_begin(rom, size, root);
    if (e == NULL) {
        snprintf(err, errlen, "out of memory");
        return false;
    }
    bool ok = true;
    for (int k = 0; k < STEPS && ok; k++) {
        ok = hz_ex_step(e, k);
    }
    ok = ok && hz_ex_finish(e);
    if (!ok) {
        snprintf(err, errlen, "%s", hz_ex_error(e));
    }
    hz_ex_end(e);
    return ok;
}
