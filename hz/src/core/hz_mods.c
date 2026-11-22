#include "hz_mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MODS_PATH 1024

/* A mod the game has read: where it is, the game's own file it stands in
 * for, the last step that read it, and whether that step refused it. */
typedef struct {
    char *path;
    char *own;
    int   step;
    bool  refused;
    bool  aside;        /* set aside: the game's own is read from now on */
} Mod;

static bool g_on;
static char g_assets[MODS_PATH];
static char g_mods[MODS_PATH];
static Mod *g_list;
static int  g_count, g_cap;
static int  g_step;

/* A folder's name without the separators after it. */
static void folder_name(char *out, const char *dir)
{
    snprintf(out, MODS_PATH, "%s", dir);
    size_t n = strlen(out);
    while (n > 1 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        out[--n] = '\0';
    }
}

void hz_mods_set(const char *assets_dir, const char *mods_dir)
{
    g_on = false;
    if (mods_dir == NULL || mods_dir[0] == '\0'
        || strlen(assets_dir) >= MODS_PATH || strlen(mods_dir) >= MODS_PATH) {
        return;
    }
    folder_name(g_assets, assets_dir);
    folder_name(g_mods, mods_dir);
    g_on = true;
}

static bool is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *copy(const char *s)
{
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c != NULL) {
        memcpy(c, s, n);
    }
    return c;
}

/* The mod read for `path`, the mod's own path or the game's. */
static Mod *find(const char *path)
{
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_list[i].path, path) == 0
            || strcmp(g_list[i].own, path) == 0) {
            return &g_list[i];
        }
    }
    return NULL;
}

static Mod *add(const char *path, const char *own)
{
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 32;
        Mod *list = realloc(g_list, (size_t)cap * sizeof *list);
        if (list == NULL) {
            return NULL;
        }
        g_list = list;
        g_cap = cap;
    }
    Mod *m = &g_list[g_count];
    memset(m, 0, sizeof *m);
    m->path = copy(path);
    m->own = copy(own);
    if (m->path == NULL || m->own == NULL) {
        free(m->path);
        free(m->own);
        return NULL;
    }
    g_count++;
    return m;
}

const char *hz_mods_resolve(const char *path, char *buf, size_t size)
{
    if (!g_on) {
        return path;
    }
    size_t n = strlen(g_assets);
    if (strncmp(path, g_assets, n) != 0
        || (path[n] != '/' && path[n] != '\\')) {
        return path;
    }
    Mod *m = find(path);
    if (m == NULL) {
        int len = snprintf(buf, size, "%s/%s", g_mods, path + n + 1);
        if (len < 0 || (size_t)len >= size || !is_file(buf)) {
            return path;
        }
        m = add(buf, path);
        if (m == NULL) {
            return path;
        }
    }
    if (m->aside || strlen(m->path) >= size) {
        return path;
    }
    m->step = g_step;
    memcpy(buf, m->path, strlen(m->path) + 1);
    return buf;
}

const char *hz_mods_refused(const char *path)
{
    Mod *m = find(path);
    if (m == NULL || m->aside) {
        return path;
    }
    m->refused = true;
    return m->path;
}

void hz_mods_step(void)
{
    g_step++;
}

bool hz_mods_set_aside(void)
{
    bool named = false;
    for (int i = 0; i < g_count; i++) {
        const Mod *m = &g_list[i];
        if (!m->aside && m->step == g_step && m->refused) {
            named = true;
        }
    }
    int n = 0;
    for (int i = 0; i < g_count; i++) {
        Mod *m = &g_list[i];
        if (m->aside || m->step != g_step || (named && !m->refused)) {
            continue;
        }
        m->aside = true;
        n++;
        fprintf(stderr, "mods: %s set aside: %s (above); the game's own %s "
                        "is read in its place\n", m->path,
                named ? "it was refused" : "what it was read for was refused",
                m->own);
    }
    return n > 0;
}

const char *hz_mods_dir(void)
{
    return g_on ? g_mods : NULL;
}

int hz_mods_count(void)
{
    int n = 0;
    for (int i = 0; i < g_count; i++) {
        n += !g_list[i].aside;
    }
    return n;
}

const char *hz_mods_name(int i)
{
    for (int k = 0; k < g_count; k++) {
        if (!g_list[k].aside && i-- == 0) {
            return g_list[k].path;
        }
    }
    return NULL;
}
