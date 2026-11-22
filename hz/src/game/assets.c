#include "assets.h"

#include "../audio/songtext.h"
#include "../core/hz_file.h"
#include "../core/hz_mods.h"
#include "../core/hz_vdp.h"
#include "../core/json.h"
#include "../core/png.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The formats are described in assets/README.md; each loader below says
 * which file it reads and checks what it can, so that a hand edit that
 * breaks a file stops the program with the file's name instead of
 * loading something the game would never have. A mod (docs/mods.md) that
 * a loader refuses is set aside instead, and the game's own file read. */

static const char *const MAP_FOLDERS[HZ_MAPS] = {
    "0_abgrund", "1_vulkan", "2_loch", "3_strand", "4_stadt", "5_eisfrei",
    "6_waldung", "7_oase"
};

const char *hz_map_folder(int n)
{
    return (n >= 0 && n < HZ_MAPS) ? MAP_FOLDERS[n] : "";
}

#define PATH_MAX_LEN 1024

static bool fail(const char *path, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "assets: %s: ", hz_mods_refused(path));
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return false;
}

/* dir/name into out (PATH_MAX_LEN bytes); a path too long for it comes
 * out empty, and the loader then reports the file can't be read. */
static void join(char *out, const char *dir, const char *name)
{
    size_t d = strlen(dir);
    size_t n = strlen(name);
    if (d + 1 + n + 1 > PATH_MAX_LEN) {
        out[0] = '\0';
        return;
    }
    memcpy(out, dir, d);
    out[d] = '/';
    memcpy(out + d + 1, name, n + 1);
}

/* -- pixels --------------------------------------------------------------- */

/* A palette strip: 16 wide, a row per CRAM line; the PNG's palette holds
 * the colours (pixel (x, y) is colour y * 16 + x). Each colour is snapped
 * to the nearest level the DAC can make. */
static bool load_colours(const char *path, u16 *words, int count)
{
    PngImage img;
    if (!png_load_indexed(&img, path)) {
        return fail(path, "can't be read as an indexed PNG");
    }
    bool ok = img.pal_count >= count;
    if (ok) {
        for (int i = 0; i < count; i++) {
            words[i] = vdp_rgb_cram(img.pal[i]);
        }
    }
    png_free(&img);
    return ok ? true : fail(path, "needs %d colours in its palette", count);
}

static bool load_palette(const char *path, u16 *words, int lines)
{
    return load_colours(path, words, lines * 16);
}

/* Pixel (x, y) of an image, checked against its size and the 16 colours
 * a tile can use. */
static bool pixel(const PngImage *img, int x, int y, u8 *out)
{
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) {
        return false;
    }
    u8 v = img->px[(size_t)y * (size_t)img->width + (size_t)x];
    if (v > 15) {
        return false;
    }
    *out = v;
    return true;
}

/* The 8 x 8 pixels at (x, y) as a tile: 32 bytes, rows top to bottom,
 * two pixels a byte, the left one in the high nibble. */
static bool pack_tile(const PngImage *img, int x, int y, u8 *tile)
{
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c += 2) {
            u8 hi;
            u8 lo;
            if (!pixel(img, x + c, y + r, &hi)
                || !pixel(img, x + c + 1, y + r, &lo)) {
                return false;
            }
            *tile++ = (u8)((hi << 4) | lo);
        }
    }
    return true;
}

/* A plain tile sheet: `count` tiles, row-major, as many across as the
 * image is wide. */
static bool load_tile_sheet(const char *path, u8 *out, int count)
{
    PngImage img;
    if (!png_load_indexed(&img, path)) {
        return fail(path, "can't be read as an indexed PNG");
    }
    int across = img.width / 8;
    bool ok = across > 0;
    for (int t = 0; ok && t < count; t++) {
        ok = pack_tile(&img, (t % across) * 8, (t / across) * 8,
                       out + t * 32);
        if (!ok) {
            fail(path, "tile %d is outside the image or uses a colour "
                       "past 15", t);
        }
    }
    png_free(&img);
    if (across == 0) {
        return fail(path, "is narrower than a tile");
    }
    return ok;
}

/* A picture a nametable draws from consecutive tiles: exactly `across` x
 * `rows` tiles, in reading order; its PNG palette's first 16 colours are
 * its CRAM line. */
static bool load_picture(const char *path, u8 *out, int across, int rows,
                         u16 palette[16])
{
    PngImage img;
    if (!png_load_indexed(&img, path)) {
        return fail(path, "can't be read as an indexed PNG");
    }
    bool ok = img.width == across * 8 && img.height == rows * 8;
    if (!ok) {
        fail(path, "should be %d x %d pixels", across * 8, rows * 8);
    } else if (img.pal_count < 16) {
        ok = fail(path, "needs 16 colours in its palette");
    }
    for (int i = 0; ok && i < 16; i++) {
        palette[i] = vdp_rgb_cram(img.pal[i]);
    }
    for (int t = 0; ok && t < across * rows; t++) {
        ok = pack_tile(&img, (t % across) * 8, (t / across) * 8, out + t * 32);
        if (!ok) {
            fail(path, "tile %d uses a colour past 15", t);
        }
    }
    png_free(&img);
    return ok;
}

/* A number, or a string of hex digits after '$'. */
static bool json_number(const JsonValue *v, long *out)
{
    if (v == NULL) {
        return false;
    }
    if (v->type == JSON_NUMBER) {
        *out = (long)v->number;
        return (double)*out == v->number;
    }
    if (v->type == JSON_STRING && v->string[0] == '$' && v->string[1]) {
        char *end = NULL;
        *out = strtol(v->string + 1, &end, 16);
        return *end == '\0';
    }
    return false;
}

/* Member `key` of `obj` as a number in [lo, hi]; says where it failed. */
static bool field(const char *path, const char *where, const JsonValue *obj,
                  const char *key, long lo, long hi, long *out)
{
    const JsonValue *v = json_get(obj, key);
    if (json_number(v, out) && *out >= lo && *out <= hi) {
        return true;
    }
    if (v != NULL && v->type == JSON_STRING) {
        return fail(path, "%s: \"%s\" should be from $%lX to $%lX", where,
                    key, lo, hi);
    }
    return fail(path, "%s: \"%s\" should be a number from %ld to %ld", where,
                key, lo, hi);
}

/* A sprite sheet: the PNG named by the JSON beside it, and the JSON's
 * groups, each `count` pictures of w x h tiles, `across` a row from
 * (x, y). A picture's tiles run down each column in turn, as the VDP
 * reads a sprite's, unless the group's order is "rows". Every tile of the
 * sheet's range must come from exactly one group. */
static bool load_sprite_sheet(const char *dir, const char *name, u8 *out,
                              long first, long count)
{
    char path[PATH_MAX_LEN];
    char json_name[64];
    snprintf(json_name, sizeof json_name, "%s.json", name);
    join(path, dir, json_name);
    JsonValue *sheet = json_parse_file(path);
    if (sheet == NULL) {
        return fail(path, "can't be read as JSON");
    }

    long v = 0;
    bool ok = field(path, "sheet", sheet, "first_tile", first, first, &v)
           && field(path, "sheet", sheet, "tiles", count, count, &v);
    const char *image = json_str(sheet, "image", NULL);
    const JsonValue *groups = json_get(sheet, "groups");
    if (ok && (image == NULL || groups == NULL
               || groups->type != JSON_ARRAY)) {
        ok = fail(path, "needs \"image\" and \"groups\"");
    }

    PngImage img;
    memset(&img, 0, sizeof img);
    char png_path[PATH_MAX_LEN];
    if (ok) {
        join(png_path, dir, image);
        ok = png_load_indexed(&img, png_path);
        if (!ok) {
            fail(png_path, "can't be read as an indexed PNG");
        }
    }

    u8 *seen = calloc((size_t)count, 1);
    ok = ok && seen != NULL;
    for (int g = 0; ok && g < json_length(groups); g++) {
        const JsonValue *grp = json_at(groups, g);
        char where[32];
        snprintf(where, sizeof where, "groups[%d]", g);
        long tile, w, h, n, across, x, y;
        ok = field(path, where, grp, "tile", first, first + count - 1, &tile)
          && field(path, where, grp, "w", 1, 64, &w)
          && field(path, where, grp, "h", 1, 64, &h)
          && field(path, where, grp, "count", 1, count, &n)
          && field(path, where, grp, "across", 1, n, &across)
          && field(path, where, grp, "x", 0, 1 << 16, &x)
          && field(path, where, grp, "y", 0, 1 << 16, &y);
        bool rows = strcmp(json_str(grp, "order", "columns"), "rows") == 0;
        if (ok && tile + n * w * h > first + count) {
            ok = fail(path, "%s runs past tile $%lX", where,
                      first + count - 1);
        }
        for (long i = 0; ok && i < n; i++) {
            long fx = x + (i % across) * w * 8;
            long fy = y + (i / across) * h * 8;
            for (long k = 0; ok && k < w * h; k++) {
                long t = tile + i * w * h + k - first;
                long tx = rows ? k % w : k / h;
                long ty = rows ? k / w : k % h;
                if (seen[t]) {
                    ok = fail(path, "%s: tile $%lX is in two groups", where,
                              t + first);
                    break;
                }
                seen[t] = 1;
                ok = pack_tile(&img, (int)(fx + tx * 8), (int)(fy + ty * 8),
                               out + t * 32);
                if (!ok) {
                    fail(png_path, "%s, tile $%lX: outside the image or a "
                                   "colour past 15", where, t + first);
                }
            }
        }
    }
    for (long t = 0; ok && t < count; t++) {
        if (!seen[t]) {
            ok = fail(path, "no group has tile $%lX", t + first);
        }
    }
    free(seen);
    png_free(&img);
    json_free(sheet);
    return ok;
}

/* -- text --------------------------------------------------------------- */

/* Lines of hex words: `rows` data lines of `cols` words each; blank lines
 * and anything after '#' ignored. */
static bool load_hex_rows(const char *path, u16 *out, int rows, int cols)
{
    char *text = (char *)hz_file_read(path, NULL);
    if (text == NULL) {
        return fail(path, "can't be read");
    }
    int row = 0;
    int line_no = 0;
    bool ok = true;
    for (char *line = text; ok && line != NULL; ) {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        line_no++;
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (*p != '\0') {
            if (row == rows) {
                ok = fail(path, "line %d: more than %d rows", line_no, rows);
                break;
            }
            for (int c = 0; ok && c < cols; c++) {
                char *end = NULL;
                unsigned long v = strtoul(p, &end, 16);
                if (end == p || v > 0xFFFF) {
                    ok = fail(path, "line %d: needs %d hex words", line_no,
                              cols);
                }
                out[row * cols + c] = (u16)v;
                p = end;
            }
            while (ok && (*p == ' ' || *p == '\t' || *p == '\r')) {
                p++;
            }
            if (ok && *p != '\0') {
                ok = fail(path, "line %d: more than %d words", line_no,
                          cols);
            }
            row++;
        }
        line = next;
    }
    free(text);
    if (ok && row != rows) {
        ok = fail(path, "has %d rows, needs %d", row, rows);
    }
    return ok;
}

/* -- Tiled ------------------------------------------------------------ */

/* The value of attribute `name` in the tag starting at `tag` (up to its
 * '>'), copied into out; false if the tag has none. */
static bool tag_attr(const char *tag, const char *name, char *out,
                     size_t cap)
{
    const char *end = strchr(tag, '>');
    char pattern[64];
    snprintf(pattern, sizeof pattern, " %s=\"", name);
    const char *hit = strstr(tag, pattern);
    if (hit == NULL || (end != NULL && hit > end)) {
        return false;
    }
    hit += strlen(pattern);
    size_t n = 0;
    while (hit[n] != '"' && hit[n] != '\0' && n + 1 < cap) {
        out[n] = hit[n];
        n++;
    }
    out[n] = '\0';
    return hit[n] == '"';
}

/* The first gid of the tileset called `name`, 0 if there isn't one. A
 * tileset Tiled has exported to a file is known by the file's name
 * (source="blocks.tsx" is "blocks"). */
static long tmx_firstgid(const char *text, const char *name)
{
    for (const char *t = strstr(text, "<tileset"); t != NULL;
         t = strstr(t + 1, "<tileset")) {
        char value[256];
        bool named = tag_attr(t, "name", value, sizeof value)
                  && strcmp(value, name) == 0;
        if (!named && tag_attr(t, "source", value, sizeof value)) {
            char *base = strrchr(value, '/');
            base = (base != NULL) ? base + 1 : value;
            char *dot = strrchr(base, '.');
            if (dot != NULL) {
                *dot = '\0';
            }
            named = strcmp(base, name) == 0;
        }
        if (named && tag_attr(t, "firstgid", value, sizeof value)) {
            return atol(value);
        }
    }
    return 0;
}

/* The CSV cells of the layer called `name`: false if there is no such
 * layer (*found false) or it isn't 64 x 64 CSV (an error). Tiled's flip
 * bits (the top four) are dropped: nothing here flips a block. */
static bool tmx_layer(const char *path, const char *text, const char *name,
                      u32 *cells, bool *found)
{
    *found = false;
    for (const char *l = strstr(text, "<layer"); l != NULL;
         l = strstr(l + 1, "<layer")) {
        char value[64];
        if (!tag_attr(l, "name", value, sizeof value)
            || strcmp(value, name) != 0) {
            continue;
        }
        *found = true;
        char w[16];
        char h[16];
        if (!tag_attr(l, "width", w, sizeof w)
            || !tag_attr(l, "height", h, sizeof h) || atoi(w) != 64
            || atoi(h) != 64) {
            return fail(path, "layer %s isn't 64 x 64", name);
        }
        const char *data = strstr(l, "<data");
        if (data == NULL || !tag_attr(data, "encoding", value, sizeof value)
            || strcmp(value, "csv") != 0) {
            return fail(path, "layer %s isn't CSV (Tiled: Map Properties, "
                              "Tile Layer Format)", name);
        }
        const char *p = strchr(data, '>');
        const char *end = strstr(data, "</data>");
        if (p == NULL || end == NULL) {
            return fail(path, "layer %s has no data", name);
        }
        p++;
        for (int i = 0; i < 64 * 64; i++) {
            while (p < end && (*p == ',' || *p == ' ' || *p == '\n'
                               || *p == '\r' || *p == '\t')) {
                p++;
            }
            char *stop = NULL;
            unsigned long gid = strtoul(p, &stop, 10);
            if (stop == p || stop > end) {
                return fail(path, "layer %s has fewer than 4096 cells",
                            name);
            }
            cells[i] = (u32)(gid & 0x0FFFFFFFu);
            p = stop;
        }
        return true;
    }
    return false;
}

/* layout.tmx: the "blocks" layer (gid = block + the blocks tileset's
 * first gid) and the path-hint layers "path_p1" / "path_p2" (gid = an
 * arrow of the path_hints tileset: 0-7 red, 8-15 blue, either colour
 * taken as its 8-way direction; 0 no hint). A cell with a hint in either
 * layer gets layout bit 9; the other layer's missing direction is 0. */
static bool load_layout(const char *path, u16 layout[64][64])
{
    char *text = (char *)hz_file_read(path, NULL);
    if (text == NULL) {
        return fail(path, "can't be read");
    }
    static u32 blocks[64 * 64];
    static u32 p1[64 * 64];
    static u32 p2[64 * 64];
    long fg_blocks = tmx_firstgid(text, "blocks");
    long fg_hints = tmx_firstgid(text, "path_hints");
    bool found = false;
    bool ok = fg_blocks > 0;
    if (!ok) {
        fail(path, "has no tileset called blocks");
    } else {
        ok = tmx_layer(path, text, "blocks", blocks, &found);
        if (!ok && !found) {
            fail(path, "has no layer called blocks");
        }
    }
    bool have_p1 = false;
    bool have_p2 = false;
    memset(p1, 0, sizeof p1);
    memset(p2, 0, sizeof p2);
    if (ok) {
        ok = tmx_layer(path, text, "path_p1", p1, &have_p1) || !have_p1;
    }
    if (ok) {
        ok = tmx_layer(path, text, "path_p2", p2, &have_p2) || !have_p2;
    }
    if (ok && (have_p1 || have_p2) && fg_hints <= 0) {
        ok = fail(path, "has path layers but no tileset called path_hints");
    }
    for (int i = 0; ok && i < 64 * 64; i++) {
        long b = (long)blocks[i] - fg_blocks;
        if (blocks[i] == 0 || b < 0 || b >= HZ_BLOCKS) {
            ok = fail(path, "blocks layer, row %d column %d: no block",
                      i / 64, i % 64);
            break;
        }
        u16 w = (u16)b;
        long d1 = p1[i] ? (long)p1[i] - fg_hints : 0;
        long d2 = p2[i] ? (long)p2[i] - fg_hints : 0;
        if (d1 < 0 || d1 > 15 || d2 < 0 || d2 > 15) {
            ok = fail(path, "path layers, row %d column %d: not an arrow",
                      i / 64, i % 64);
            break;
        }
        if (p1[i] || p2[i]) {
            w = (u16)(w | 0x200 | ((d1 & 7) << 13) | ((d2 & 7) << 10));
        }
        layout[i / 64][i % 64] = w;
    }
    free(text);
    return ok;
}

/* -- records ------------------------------------------------------------ */

/* A name in font codes: printable characters as themselves, {XX} for any
 * other byte; `len` bytes exactly. */
static bool font_codes(const char *s, u8 *name, int len)
{
    int n = 0;
    while (*s != '\0') {
        if (n == len) {
            return false;
        }
        if (*s == '{') {
            char *end = NULL;
            unsigned long v = strtoul(s + 1, &end, 16);
            if (end != s + 3 || *end != '}' || v > 0xFF) {
                return false;
            }
            name[n++] = (u8)v;
            s = end + 1;
        } else {
            name[n++] = (u8)*s++;
        }
    }
    return n == len;
}

static bool font_string(const char *s, u8 name[8])
{
    return font_codes(s, name, 8);
}

/* A sprite attr: "$tile" then any of h, v, hv, palN, pri. */
static bool sprite_attr(const char *s, u16 *out)
{
    if (s == NULL || *s != '$') {
        return false;
    }
    char *p = NULL;
    unsigned long tile = strtoul(s + 1, &p, 16);
    if (p == s + 1 || tile > 0x7FF) {
        return false;
    }
    u16 w = (u16)tile;
    while (*p == ' ') {
        while (*p == ' ') {
            p++;
        }
        if (strncmp(p, "hv", 2) == 0 && (p[2] == ' ' || p[2] == '\0')) {
            w |= 0x1800;
            p += 2;
        } else if (*p == 'h' && (p[1] == ' ' || p[1] == '\0')) {
            w |= 0x0800;
            p += 1;
        } else if (*p == 'v' && (p[1] == ' ' || p[1] == '\0')) {
            w |= 0x1000;
            p += 1;
        } else if (strncmp(p, "pal", 3) == 0 && p[3] >= '0' && p[3] <= '3'
                   && (p[4] == ' ' || p[4] == '\0')) {
            w |= (u16)((p[3] - '0') << 13);
            p += 4;
        } else if (strncmp(p, "pri", 3) == 0
                   && (p[3] == ' ' || p[3] == '\0')) {
            w |= 0x8000;
            p += 3;
        } else {
            return false;
        }
    }
    *out = w;
    return *p == '\0';
}

static bool sprite_list(const char *path, const char *where,
                        const JsonValue *arr, u16 out[16])
{
    if (arr == NULL || json_length(arr) != 16) {
        return fail(path, "%s: needs 16 sprites", where);
    }
    for (int d = 0; d < 16; d++) {
        const JsonValue *v = json_at(arr, d);
        if (v->type != JSON_STRING || !sprite_attr(v->string, &out[d])) {
            return fail(path, "%s[%d]: \"$tile\" and any of h v hv palN pri",
                        where, d);
        }
    }
    return true;
}

static bool unit_record(const char *path, const char *where,
                        const JsonValue *r, HzUnitStats *st)
{
    const char *name = json_str(r, "name", NULL);
    if (name == NULL || !font_string(name, st->name)) {
        return fail(path, "%s: \"name\" needs 8 characters ({XX} is one)",
                    where);
    }
    long v[20];
    static const struct {
        const char *key;
        long        max;
    } F[20] = {
        { "price", 0xFFFF }, { "hp", 0xFFFF }, { "size", 0xFF },
        { "ammo", 0xFF }, { "fuel", 0xFFFF }, { "sprite_size", 0xFF },
        { "speed", 0xFF }, { "overlay", 0xFF }, { "fuel_use", 0xFF },
        { "shot_speed", 0xFF }, { "shot_life", 0xFF },
        { "fire_delay", 0xFF }, { "power", 0xFF }, { "sight", 0xFF },
        { "build_time", 0xFF }, { "missiles", 0xFF },
        { "missile_speed", 0xFF }, { "missile_life", 0xFF },
        { "missile_delay", 0xFF }, { "orders", 0xFFFF }
    };
    for (int i = 0; i < 20; i++) {
        if (!field(path, where, r, F[i].key, 0, F[i].max, &v[i])) {
            return false;
        }
    }
    st->price = (u16)v[0];
    st->hp = (u16)v[1];
    st->size = (u8)v[2];
    st->ammo = (u8)v[3];
    st->fuel = (u16)v[4];
    st->sprite_size = (u8)v[5];
    st->speed = (u8)v[6];
    st->overlay = (u8)v[7];
    st->fuel_use = (u8)v[8];
    st->shot_speed = (u8)v[9];
    st->shot_life = (u8)v[10];
    st->fire_delay = (u8)v[11];
    st->power = (u8)v[12];
    st->sight = (u8)v[13];
    st->build_time = (u8)v[14];
    st->missiles = (u8)v[15];
    st->missile_speed = (u8)v[16];
    st->missile_life = (u8)v[17];
    st->missile_delay = (u8)v[18];
    st->orders = (u16)v[19];
    return true;
}

static bool form_bytes(const char *path, const JsonValue *mech,
                       const char *key, u8 out[3])
{
    static const char *const FORMS[3] = { "robot", "jet", "carrying" };
    const JsonValue *o = json_get(mech, key);
    char where[48];
    snprintf(where, sizeof where, "mech.%s", key);
    for (int f = 0; f < 3; f++) {
        long v;
        if (!field(path, where, o, FORMS[f], 0, 0xFF, &v)) {
            return false;
        }
        out[f] = (u8)v;
    }
    return true;
}

/* data/units.json: UnitStats (with UnitDirTable's sprites for types 0-9),
 * UnitStats_KIDOU and ExplosionFrames. */
static bool load_units(const char *path, HzAssets *a)
{
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *units = json_get(root, "units");
    bool ok = json_length(units) == HZ_UNIT_TYPES;
    if (!ok) {
        fail(path, "\"units\" needs %d records", HZ_UNIT_TYPES);
    }
    for (int t = 0; ok && t < HZ_UNIT_TYPES; t++) {
        const JsonValue *r = json_at(units, t);
        char where[32];
        long type;
        snprintf(where, sizeof where, "units[%d]", t);
        ok = field(path, where, r, "type", t, t, &type)
          && unit_record(path, where, r, &a->unit_stats[t]);
        if (ok && t < HZ_UNIT_SPRITE_TYPES) {
            snprintf(where, sizeof where, "units[%d].sprites", t);
            ok = sprite_list(path, where, json_get(r, "sprites"),
                             a->unit_sprites[t]);
        }
    }
    const JsonValue *mech = json_get(root, "mech");
    long b26 = 0;
    long b27 = 0;
    ok = ok && unit_record(path, "mech", mech, &a->mech_stats.stats)
         && form_bytes(path, mech, "max_speed", a->mech_stats.max_speed)
         && form_bytes(path, mech, "fuel_use_by_form", a->mech_stats.fuel_use)
         && field(path, "mech", mech, "byte_26", 0, 0xFF, &b26)
         && field(path, "mech", mech, "byte_27", 0, 0xFF, &b27)
         && sprite_list(path, "explosion_frames",
                        json_get(root, "explosion_frames"),
                        a->explosion_frames);
    a->mech_stats.byte_26 = (u8)b26;
    a->mech_stats.byte_27 = (u8)b27;
    json_free(root);
    return ok;
}

/* (Session 60) data/turrets.json, the port's own: "turrets", the records
 * of types 6 and 7 with the TURRETS patch, each as a units.json record
 * (its "type" 6 or 7, no sprites: UnitDirTable's turret rows draw them).
 * Optional: absent, the patch sells nothing. */
static bool load_turrets(const char *path, HzAssets *a)
{
    a->turrets_loaded = false;
    char mod[PATH_MAX_LEN];
    FILE *probe = fopen(hz_mods_resolve(path, mod, sizeof mod), "rb");
    if (probe == NULL) {
        return true;
    }
    fclose(probe);
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *turrets = json_get(root, "turrets");
    bool ok = json_length(turrets) == 2;
    if (!ok) {
        fail(path, "\"turrets\" needs 2 records, types 6 and 7");
    }
    for (int i = 0; ok && i < 2; i++) {
        const JsonValue *r = json_at(turrets, i);
        char where[32];
        long type;
        snprintf(where, sizeof where, "turrets[%d]", i);
        ok = field(path, where, r, "type", 6 + i, 6 + i, &type)
          && unit_record(path, where, r, &a->turret_stats[i]);
    }
    json_free(root);
    a->turrets_loaded = ok;
    return ok;
}

/* repair.json's "icon": 16 rows of 16 characters, a hex digit a pixel's
 * colour and '.' none, into the icon's 2 x 2 tiles as HUDIcons has an
 * order's (down each column). */
static bool repair_icon(const char *path, const JsonValue *rows, u8 *out)
{
    if (json_length(rows) != 16) {
        return fail(path, "order.icon: needs 16 rows");
    }
    memset(out, 0, HZ_ORDER_ICON_BYTES);
    for (int y = 0; y < 16; y++) {
        const JsonValue *r = json_at(rows, y);
        if (r->type != JSON_STRING || strlen(r->string) != 16) {
            return fail(path, "order.icon[%d]: needs 16 characters", y);
        }
        for (int x = 0; x < 16; x++) {
            char c = r->string[x];
            int v = (c == '.') ? 0
                  : (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (v < 0) {
                return fail(path, "order.icon[%d]: '%c' is not a colour "
                            "(0-9, A-F, or . for none)", y, c);
            }
            u8 *b = out + ((x >> 3) * 2 + (y >> 3)) * 32 + (y & 7) * 4
                    + ((x & 7) >> 1);
            *b |= (u8)((x & 1) ? v : v << 4);
        }
    }
    return true;
}

/* (Session 72) data/repair.json, the port's own: the REPAIR patch's
 * "truck" and "mechanic" (each as a units.json record, its "type" 10 or
 * 11, no sprites: the SUPPLY's and the INFANTRY's draw them in
 * "truck_colours" and "mechanic_colours"), the truck's "order" (14: its
 * name, fee and icon) and how they work ("search", "park", "reach",
 * "danger" in px, "calm" in frames, "rate" in HP). Optional: absent, the
 * patch sells nothing. */
static bool load_repair(const char *path, HzAssets *a)
{
    a->repair_loaded = false;
    char mod[PATH_MAX_LEN];
    FILE *probe = fopen(hz_mods_resolve(path, mod, sizeof mod), "rb");
    if (probe == NULL) {
        return true;
    }
    fclose(probe);
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    HzRepair *rp = &a->repair;
    static const char *const UNIT[2] = { "truck", "mechanic" };
    static const char *const COLOURS[2] = { "truck_colours",
                                            "mechanic_colours" };
    bool ok = true;
    long v;
    for (int i = 0; ok && i < 2; i++) {
        const JsonValue *r = json_get(root, UNIT[i]);
        ok = field(path, UNIT[i], r, "type", 10 + i, 10 + i, &v)
          && unit_record(path, UNIT[i], r, &rp->stats[i]);
        const JsonValue *c = json_get(root, COLOURS[i]);
        if (ok && json_length(c) != 16) {
            ok = fail(path, "\"%s\" needs 16 colours", COLOURS[i]);
        }
        for (int k = 0; ok && k < 16; k++) {
            ok = json_number(json_at(c, k), &v) && v >= 0 && v <= 15;
            if (!ok) {
                fail(path, "%s[%d]: not a colour 0-15", COLOURS[i], k);
            }
            rp->colours[i][k] = (u8)v;
        }
    }
    const JsonValue *order = json_get(root, "order");
    ok = ok && field(path, "order", order, "order", 14, 14, &v)
         && field(path, "order", order, "fee", 0, 0xFFFF, &v);
    rp->order.fee = (u16)v;
    const char *name = json_str(order, "name", NULL);
    if (ok && (name == NULL
               || !font_codes(name, rp->order.name, HZ_ORDER_NAME))) {
        ok = fail(path, "order: \"name\" needs %d characters", HZ_ORDER_NAME);
    }
    ok = ok && repair_icon(path, json_get(order, "icon"), rp->order_icon);
    static const struct {
        const char *key;
        long        lo, hi;
    } F[6] = {
        { "search", 0, 760 }, { "park", 0, 255 }, { "reach", 16, 400 },
        { "danger", 0, 2047 }, { "calm", 0, 0xFFFF }, { "rate", 1, 255 }
    };
    long n[6] = { 0 };
    for (int i = 0; ok && i < 6; i++) {
        ok = field(path, "repair", root, F[i].key, F[i].lo, F[i].hi, &n[i]);
    }
    rp->search = (u16)n[0];
    rp->park = (u16)n[1];
    rp->reach = (u16)n[2];
    rp->danger = (u16)n[3];
    rp->calm = (u16)n[4];
    rp->rate = (u8)n[5];
    json_free(root);
    a->repair_loaded = ok;
    return ok;
}

/* data/start_units.json: CPUStartTypeTable and StartUnits, per map the
 * records used when P1 is the CPU (p1_cpu) and when P2 is (p2_cpu). */
static bool load_start_units(const char *path, HzAssets *a)
{
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *types = json_get(root, "cpu_start_types");
    bool ok = json_length(types) == 4;
    if (!ok) {
        fail(path, "\"cpu_start_types\" needs 4 unit types");
    }
    for (int d = 0; ok && d < 4; d++) {
        long v;
        ok = json_number(json_at(types, d), &v) && v >= 0 && v <= 0xFF;
        a->cpu_start_types[d] = (u8)v;
        if (!ok) {
            fail(path, "cpu_start_types[%d]: not a byte", d);
        }
    }
    const JsonValue *maps = json_get(root, "maps");
    if (ok && json_length(maps) != HZ_MAPS) {
        ok = fail(path, "\"maps\" needs %d entries", HZ_MAPS);
    }
    static const char *const HALF[2] = { "p1_cpu", "p2_cpu" };
    for (int m = 0; ok && m < HZ_MAPS; m++) {
        const JsonValue *entry = json_at(maps, m);
        long n;
        char where[48];
        snprintf(where, sizeof where, "maps[%d]", m);
        ok = field(path, where, entry, "map", m, m, &n);
        for (int h = 0; ok && h < 2; h++) {
            const JsonValue *recs = json_get(entry, HALF[h]);
            int count = json_length(recs);
            if (recs == NULL || count > HZ_START_UNITS) {
                ok = fail(path, "%s.%s: up to %d records", where, HALF[h],
                          HZ_START_UNITS);
                break;
            }
            a->start_unit_count[m][h] = (u8)count;
            for (int i = 0; ok && i < count; i++) {
                const JsonValue *r = json_at(recs, i);
                HzStartUnit *u = &a->start_units[m][h][i];
                long f = 0, t = 0, o = 0, d = 0, x = 0, y = 0;
                snprintf(where, sizeof where, "maps[%d].%s[%d]", m, HALF[h],
                         i);
                ok = field(path, where, r, "flags", 0x80, 0xFF, &f)
                  && field(path, where, r, "type", 0, 0xFF, &t)
                  && field(path, where, r, "order", 0, 0xFF, &o)
                  && field(path, where, r, "dir", 0, 0xFF, &d)
                  && field(path, where, r, "x", -0x8000, 0x7FFF, &x)
                  && field(path, where, r, "y", -0x8000, 0x7FFF, &y);
                u->flags = (u8)f;
                u->type = (u8)t;
                u->order = (u8)o;
                u->dir = (u8)d;
                u->x = (s16)x;
                u->y = (s16)y;
            }
        }
    }
    json_free(root);
    return ok;
}

/* data/sound.json: SfxPriority; sfx past the list are $FF (unused). */
static bool load_sound(const char *path, HzAssets *a)
{
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *prio = json_get(root, "sfx_priority");
    int n = json_length(prio);
    bool ok = prio != NULL && n <= HZ_SFX;
    if (!ok) {
        fail(path, "\"sfx_priority\" needs up to %d bytes", HZ_SFX);
    }
    memset(a->sfx_priority, 0xFF, sizeof a->sfx_priority);
    for (int i = 0; ok && i < n; i++) {
        long v;
        ok = json_number(json_at(prio, i), &v) && v >= 0 && v <= 0xFF;
        a->sfx_priority[i] = (u8)v;
        if (!ok) {
            fail(path, "sfx_priority[%d]: not a byte", i);
        }
    }
    json_free(root);
    return ok;
}

/* -- the attract demo ---------------------------------------------------- */

static const char PAD_LETTERS[8] = { 'U', 'D', 'L', 'R', 'B', 'C', 'A', 'S' };

/* A pad run's text: frames, a space, then the buttons held as letters in
 * any order (PAD_LETTERS: U D L R B C A S), or "-" for none. Split into runs
 * of at most 255 frames onto `runs` from `*count`. */
static bool pad_run(const char *path, const char *where, const char *s,
                    HzPadRun *runs, u16 *count)
{
    char *end = NULL;
    long frames = strtol(s, &end, 10);
    if (end == s || frames < 1 || *end != ' ' || end[1] == '\0') {
        return fail(path, "%s: \"%s\" should be frames (1 or more), a space "
                    "and the buttons (U D L R A B C S, or -)", where, s);
    }
    u8 buttons = 0;
    const char *b = end + 1;
    if (strcmp(b, "-") != 0) {
        for (; *b; b++) {
            const char *hit = memchr(PAD_LETTERS, *b, sizeof PAD_LETTERS);
            if (hit == NULL || (buttons >> (hit - PAD_LETTERS) & 1)) {
                return fail(path, "%s: \"%s\": '%c' isn't a button, or comes "
                            "twice", where, s, *b);
            }
            buttons |= (u8)(1u << (hit - PAD_LETTERS));
        }
    }
    while (frames > 0) {
        if (*count >= HZ_DEMO_RUNS) {
            return fail(path, "%s: more than %d runs (a run is 255 frames "
                        "at most)", where, HZ_DEMO_RUNS);
        }
        u8 n = (u8)(frames > 255 ? 255 : frames);
        runs[(*count)++] = (HzPadRun){ buttons, n };
        frames -= n;
    }
    return true;
}

/* maps/N_name/demo.json: DemoData for the map, the units the demo starts
 * with and each pad's runs. */
static bool load_demo(const char *path, HzDemoRecording *d)
{
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *units = json_get(root, "units");
    int count = json_length(units);
    bool ok = units != NULL && count <= HZ_DEMO_UNITS;
    if (!ok) {
        fail(path, "\"units\": up to %d records", HZ_DEMO_UNITS);
    }
    d->unit_count = (u8)count;
    for (int i = 0; ok && i < count; i++) {
        const JsonValue *r = json_at(units, i);
        HzStartUnit *u = &d->units[i];
        long f = 0, t = 0, o = 0, dir = 0, x = 0, y = 0;
        char where[32];
        snprintf(where, sizeof where, "units[%d]", i);
        ok = field(path, where, r, "flags", 0x80, 0xFF, &f)
          && field(path, where, r, "type", 0, 0xFF, &t)
          && field(path, where, r, "order", 0, 0xFF, &o)
          && field(path, where, r, "dir", 0, 0xFF, &dir)
          && field(path, where, r, "x", -0x8000, 0x7FFF, &x)
          && field(path, where, r, "y", -0x8000, 0x7FFF, &y);
        *u = (HzStartUnit){ (u8)f, (u8)t, (u8)o, (u8)dir, (s16)x, (s16)y };
    }
    static const char *const PADS[2] = { "p1", "p2" };
    for (int p = 0; ok && p < 2; p++) {
        const JsonValue *runs = json_get(root, PADS[p]);
        int n = json_length(runs);
        if (runs == NULL || runs->type != JSON_ARRAY) {
            ok = fail(path, "\"%s\": a list of runs", PADS[p]);
            break;
        }
        d->pad_runs[p] = 0;
        for (int i = 0; ok && i < n; i++) {
            const JsonValue *r = json_at(runs, i);
            char where[32];
            snprintf(where, sizeof where, "%s[%d]", PADS[p], i);
            if (r->type != JSON_STRING) {
                ok = fail(path, "%s: a string, \"frames buttons\"", where);
                break;
            }
            ok = pad_run(path, where, r->string, d->pad[p], &d->pad_runs[p]);
        }
    }
    json_free(root);
    return ok;
}

/* data/demos.json: DemoTable, the 16 demos the title's timeout plays in
 * turn: a map, and "split", "wide P1" or "wide P2". */
static bool load_demos(const char *path, HzAssets *a)
{
    static const struct {
        const char *name;
        u16 split, cfg1, cfg2;
    } SCREENS[3] = {
        { "split",   1, 0x0000, 0x0101 },
        { "wide P1", 0, 0x0000, 0xFF01 },
        { "wide P2", 0, 0xFF00, 0x0001 },
    };
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *demos = json_get(root, "demos");
    bool ok = json_length(demos) == HZ_DEMOS;
    if (!ok) {
        fail(path, "\"demos\" needs %d entries", HZ_DEMOS);
    }
    for (int k = 0; ok && k < HZ_DEMOS; k++) {
        const JsonValue *e = json_at(demos, k);
        char where[32];
        long map;
        snprintf(where, sizeof where, "demos[%d]", k);
        ok = field(path, where, e, "map", 0, HZ_MAPS - 1, &map);
        const char *screen = json_str(e, "screen", "");
        int s = 0;
        while (ok && s < 3 && strcmp(screen, SCREENS[s].name) != 0) {
            s++;
        }
        if (ok && s == 3) {
            ok = fail(path, "%s: \"screen\" should be \"split\", \"wide P1\" "
                      "or \"wide P2\"", where);
        }
        if (ok) {
            a->demos[k] = (HzDemo){ (u8)map, SCREENS[s].split,
                                    { SCREENS[s].cfg1, SCREENS[s].cfg2 } };
        }
    }
    json_free(root);
    return ok;
}

/* -- Q's drums (session 37) ----------------------------------------------- */

static bool drum_float(const char *path, const char *where, const JsonValue *o,
                       const char *key, float lo, float hi, float *out)
{
    const JsonValue *v = json_get(o, key);
    if (v == NULL || v->type != JSON_NUMBER || v->number < lo
        || v->number > hi) {
        return fail(path, "%s: \"%s\" should be a number from %g to %g",
                    where, key, (double)lo, (double)hi);
    }
    *out = (float)v->number;
    return true;
}

/* sound/q_drums.json: "models", each a name and PS2's drum fields with
 * the model's level_db at carrier TL tl_ref; "drums", each a song, an FM
 * instrument of it (by its name, session 50: songtext_fm_instrument; or
 * its number) and a model's name. Optional: absent, no drums. An entry
 * naming an instrument its song doesn't have (a song mod renamed it) is
 * left out, and said: the file isn't refused for it. */
static bool load_q_drums(const char *path, HzAssets *a)
{
    a->drum_model_count = 0;
    a->drum_use_count = 0;
    char mod[PATH_MAX_LEN];
    FILE *probe = fopen(hz_mods_resolve(path, mod, sizeof mod), "rb");
    if (probe == NULL) {
        return true;
    }
    fclose(probe);
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    static char names[HZ_DRUM_MODELS][32];
    const JsonValue *models = json_get(root, "models");
    int n = json_length(models);
    bool ok = models != NULL && n <= HZ_DRUM_MODELS;
    if (!ok) {
        fail(path, "\"models\": a list of up to %d", HZ_DRUM_MODELS);
    }
    for (int i = 0; ok && i < n; i++) {
        const JsonValue *m = json_at(models, i);
        HzDrumModel *d = &a->drum_models[i];
        char where[48];
        snprintf(where, sizeof where, "models[%d]", i);
        snprintf(names[i], sizeof names[i], "%s", json_str(m, "name", ""));
        float hp = 0.0f, clip = 0.0f, tl = 0.0f;
        /* (session 40) the wires (own models) and the choke (any) */
        d->wire_level = 0.0f;
        d->wire_decay_s = 0.0f;
        d->wire_hz = 5000.0f;
        d->choke = json_bool(m, "choke", false);
        d->cut = json_bool(m, "cut", false);      /* (session 45) */
        d->with_model = -1;             /* (session 44) "with": below */
        d->with_db = 0.0f;
        /* (session 46) follow the chip channel's pitch (any model) */
        d->follow = 0.0f;
        d->follow_floor_hz = 0.0f;
        if (ok && json_get(m, "follow") != NULL) {
            ok = drum_float(path, where, m, "follow", 0, 16, &d->follow);
        }
        if (ok && json_get(m, "follow_floor_hz") != NULL) {
            ok = drum_float(path, where, m, "follow_floor_hz", 0, 20000,
                            &d->follow_floor_hz);
        }
        /* "ps2": one of PS2's own drums, played as PS2 plays it */
        static const struct {
            const char *name;
            int byte;
        } PS2[7] = {
            { "high tom", 0x80 }, { "mid tom", 0x81 }, { "low tom", 0x82 },
            { "snare", 0x84 }, { "kick", 0x88 }, { "open hat", 0xA0 },
            { "closed hat", 0xC0 }
        };
        const char *ps2 = json_str(m, "ps2", NULL);
        d->ps2 = -1;
        d->decay_scale = 1.0f;
        d->glide_depth = 1.0f;
        d->fall_scale = 1.0f;
        static const struct {
            const char *key;
            float lo, hi;
        } SHAPE[5] = {
            { "decay_scale", 0.05f, 20 }, { "glide_depth", 0, 4 },
            { "fall_scale", 0, 20 }, { "overtones_db", -60, 20 },
            { "attack_db", -60, 20 }
        };
        float *shape[5] = { &d->decay_scale, &d->glide_depth, &d->fall_scale,
                            &d->overtones_db, &d->attack_db };
        for (int k = 0; ok && k < 5; k++) {
            if (json_get(m, SHAPE[k].key) != NULL) {
                ok = drum_float(path, where, m, SHAPE[k].key, SHAPE[k].lo,
                                SHAPE[k].hi, shape[k]);
            }
        }
        if (ps2 != NULL) {
            for (int k = 0; k < 7; k++) {
                if (strcmp(ps2, PS2[k].name) == 0) {
                    d->ps2 = PS2[k].byte;
                }
            }
            if (d->ps2 < 0) {
                ok = fail(path, "%s: \"ps2\" should be high tom, mid tom, "
                          "low tom, snare, kick, open hat or closed hat",
                          where);
                break;
            }
            d->level_db = 0.0f;
            ok = drum_float(path, where, m, "level_db", -60, 30, &d->level_db)
              && drum_float(path, where, m, "tl_ref", 0, 127, &tl);
            if (ok && json_get(m, "transpose") != NULL) {
                ok = drum_float(path, where, m, "transpose", -48, 48,
                                &d->transpose);
            }
            d->tl_ref = (int)tl;
            if (ok && names[i][0] == '\0') {
                ok = fail(path, "%s: a \"name\"", where);
            }
            continue;
        }
        ok = drum_float(path, where, m, "tone_from_hz", 0, 20000, &d->tone_from_hz)
          && drum_float(path, where, m, "tone_to_hz", 0, 20000, &d->tone_to_hz)
          && drum_float(path, where, m, "glide_s", 0, 10, &d->glide_s)
          && drum_float(path, where, m, "tone_level", 0, 100, &d->tone_level)
          && drum_float(path, where, m, "tone_decay_s", 0, 10, &d->tone_decay_s)
          && drum_float(path, where, m, "noise_level", 0, 100, &d->noise_level)
          && drum_float(path, where, m, "noise_decay_s", 0, 10, &d->noise_decay_s)
          && drum_float(path, where, m, "noise_hz", 1, 20000, &d->noise_hz)
          && drum_float(path, where, m, "noise_q", 0.1f, 20, &d->noise_q)
          && drum_float(path, where, m, "click_level", 0, 100, &d->click_level)
          && drum_float(path, where, m, "click_ms", 0, 1000, &d->click_ms)
          && drum_float(path, where, m, "click_hz", 1, 20000, &d->click_hz)
          && drum_float(path, where, m, "fall_hz_s", 0, 20000, &d->fall_hz_s)
          && drum_float(path, where, m, "level_db", -60, 30, &d->level_db)
          && drum_float(path, where, m, "tl_ref", 0, 127, &tl);
        if (ok && json_get(m, "transpose") != NULL) {
            ok = drum_float(path, where, m, "transpose", -48, 48,
                            &d->transpose);
        }
        if (ok && json_get(m, "wire_level") != NULL) {
            ok = drum_float(path, where, m, "wire_level", 0, 100,
                            &d->wire_level)
              && drum_float(path, where, m, "wire_decay_s", 0, 10,
                            &d->wire_decay_s)
              && drum_float(path, where, m, "wire_hz", 1, 20000,
                            &d->wire_hz);
        }
        d->noise_highpass = json_bool(m, "noise_highpass", false);
        d->clip = json_bool(m, "clip", false);
        d->layer = json_bool(m, "layer", false);
        d->tl_ref = (int)tl;
        (void)hp;
        (void)clip;
        if (ok && names[i][0] == '\0') {
            ok = fail(path, "%s: a \"name\"", where);
        }
    }
    /* (session 44) "with": another model by name, struck with this one at
     * its own level plus "with_db"; one that has a "with" of its own isn't
     * followed */
    for (int i = 0; ok && i < n; i++) {
        const JsonValue *m = json_at(models, i);
        const char *with = json_str(m, "with", "");
        if (with[0] == '\0') {
            continue;
        }
        int k = 0;
        while (k < n && strcmp(names[k], with) != 0) {
            k++;
        }
        char where[48];
        snprintf(where, sizeof where, "models[%d]", i);
        if (k == n || k == i) {
            ok = fail(path, "%s: \"with\": no other model \"%s\"", where,
                      with);
            break;
        }
        a->drum_models[i].with_model = k;
        if (json_get(m, "with_db") != NULL) {
            ok = drum_float(path, where, m, "with_db", -60, 30,
                            &a->drum_models[i].with_db);
        }
    }
    if (ok) {
        a->drum_model_count = n;
    }
    /* (session 41) Q's music voices' level over the chip's (dB) */
    a->drum_voice_db = 0.0f;
    float voice_db = 0.0f;
    if (ok && json_get(root, "voice_level_db") != NULL) {
        ok = drum_float(path, "the table", root, "voice_level_db", -30, 30,
                        &voice_db);
        a->drum_voice_db = voice_db;
    }
    const JsonValue *drums = json_get(root, "drums");
    int u = json_length(drums);
    if (ok && (drums == NULL || u > HZ_DRUM_USES)) {
        ok = fail(path, "\"drums\": a list of up to %d", HZ_DRUM_USES);
    }
    int kept = 0;
    for (int i = 0; ok && i < u; i++) {
        const JsonValue *e = json_at(drums, i);
        char where[48];
        long song, inst = -1;
        snprintf(where, sizeof where, "drums[%d]", i);
        const JsonValue *iv = json_get(e, "instrument");
        const char *named = iv != NULL && iv->type == JSON_STRING
                            && iv->string[0] != '$' ? iv->string : NULL;
        ok = field(path, where, e, "song", 0, 31, &song)
          && (named != NULL
              || field(path, where, e, "instrument", 0, 63, &inst));
        if (ok && named != NULL) {
            inst = songtext_fm_instrument((int)song, named);
        }
        const char *name = json_str(e, "model", "");
        int k = 0;
        while (ok && k < n && strcmp(names[k], name) != 0) {
            k++;
        }
        /* (session 41) "voice": the instrument stays Q's PM voice, at its
         * level_db over voice_level_db */
        if (ok && k == n && strcmp(name, "voice") == 0) {
            k = HZ_DRUM_VOICE;
        }
        if (ok && k == n) {
            ok = fail(path, "%s: no model named \"%s\"", where, name);
        }
        float lo = 0.0f, hi = 100000.0f, pitch = 0.0f, from = -1.0f,
              to = -1.0f, level = 0.0f;
        if (ok && json_get(e, "order_from") != NULL) {
            ok = drum_float(path, where, e, "order_from", 0, 65535, &from)
              && drum_float(path, where, e, "order_to", from, 65535, &to);
        }
        if (ok && json_get(e, "min_hz") != NULL) {
            ok = drum_float(path, where, e, "min_hz", 0, 100000, &lo);
        }
        if (ok && json_get(e, "max_hz") != NULL) {
            ok = drum_float(path, where, e, "max_hz", 0, 100000, &hi);
        }
        if (ok && json_get(e, "pitch") != NULL) {
            ok = drum_float(path, where, e, "pitch", 0, 16, &pitch);
        }
        if (ok && json_get(e, "level_db") != NULL) {
            ok = drum_float(path, where, e, "level_db", -30, 30, &level);
        }
        if (ok && inst < 0) {
            char buf[PATH_MAX_LEN];
            fprintf(stderr, "assets: %s: %s: song %ld has no FM instrument "
                    "\"%s\": left out\n", hz_mods_resolve(path, buf, sizeof buf),
                    where, song, named);
        } else if (ok) {
            a->drum_uses[kept++] = (HzDrumUse){ (u8)song, (u8)inst, (u8)k, lo,
                                                hi, (int)from, (int)to, pitch,
                                                level };
        }
    }
    if (ok) {
        a->drum_use_count = kept;
    }
    json_free(root);
    return ok;
}

/* -- all of it ----------------------------------------------------------- */

static bool load_map(const char *dir, int n, HzMapAssets *m)
{
    char folder[PATH_MAX_LEN];
    char path[PATH_MAX_LEN];
    char name[64];
    snprintf(name, sizeof name, "maps/%s", MAP_FOLDERS[n]);
    join(folder, dir, name);

    join(path, folder, "palette.png");
    if (!load_palette(path, m->palette, 1)) {
        return false;
    }
    join(path, folder, "tiles.png");
    if (!load_tile_sheet(path, m->tiles, HZ_BLOCKS)) {
        return false;
    }
    static u16 rows[HZ_BLOCKS * 17];
    join(path, folder, "blocks.txt");
    if (!load_hex_rows(path, rows, HZ_BLOCKS, 17)) {
        return false;
    }
    for (int b = 0; b < HZ_BLOCKS; b++) {
        memcpy(m->blocks[b], &rows[b * 17], 16 * sizeof(u16));
        m->terrain[b] = rows[b * 17 + 16];
    }
    join(path, folder, "anim.txt");
    if (!load_hex_rows(path, &m->anim[0][0], HZ_ANIM_RECORDS, 16)) {
        return false;
    }
    join(path, folder, "radar.png");
    if (!load_tile_sheet(path, m->radar, HZ_RADAR_IMAGE / 32)) {
        return false;
    }
    join(path, folder, "demo.json");
    if (!load_demo(path, &m->demo)) {
        return false;
    }
    join(path, folder, "layout.tmx");
    return load_layout(path, m->layout);
}

/* data/order_menu.json: OrderInfo (name and fee per order) and
 * UnitAvailByMap (the unit types the menu offers on each map). */
static bool load_order_menu(const char *path, HzAssets *a)
{
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *orders = json_get(root, "orders");
    bool ok = json_length(orders) == HZ_ORDERS;
    if (!ok) {
        fail(path, "\"orders\" needs %d records", HZ_ORDERS);
    }
    for (int n = 0; ok && n < HZ_ORDERS; n++) {
        const JsonValue *r = json_at(orders, n);
        char where[32];
        long v;
        snprintf(where, sizeof where, "orders[%d]", n);
        ok = field(path, where, r, "order", n, n, &v)
          && field(path, where, r, "fee", 0, 0xFFFF, &v);
        a->orders[n].fee = (u16)v;
        const char *name = json_str(r, "name", NULL);
        if (ok && (name == NULL
                   || !font_codes(name, a->orders[n].name, HZ_ORDER_NAME))) {
            ok = fail(path, "%s: \"name\" needs %d characters", where,
                      HZ_ORDER_NAME);
        }
    }
    const JsonValue *buyable = json_get(root, "buyable");
    if (ok && json_length(buyable) != HZ_MAPS) {
        ok = fail(path, "\"buyable\" needs %d maps", HZ_MAPS);
    }
    for (int m = 0; ok && m < HZ_MAPS; m++) {
        const JsonValue *r = json_at(buyable, m);
        const JsonValue *types = json_get(r, "types");
        char where[32];
        long v;
        snprintf(where, sizeof where, "buyable[%d]", m);
        ok = field(path, where, r, "map", m, m, &v);
        if (ok && (types == NULL || types->type != JSON_ARRAY)) {
            ok = fail(path, "%s: \"types\" should be a list", where);
        }
        a->map[m].buyable = 0;
        for (int i = 0; ok && i < json_length(types); i++) {
            ok = json_number(json_at(types, i), &v) && v >= 0 && v <= 15;
            if (!ok) {
                fail(path, "%s.types[%d]: not a unit type 0-15", where, i);
            }
            a->map[m].buyable |= (u16)(1u << v);
        }
    }
    json_free(root);
    return ok;
}

/* Text in the menu font: a space is the font's blank ('@', as the ROM
 * has it), {XX} any other byte; `len` characters exactly. */
static bool menu_text(const char *s, u8 *out, int len)
{
    int n = 0;
    while (*s != '\0') {
        if (n == len) {
            return false;
        }
        if (*s == '{') {
            char *end = NULL;
            unsigned long v = strtoul(s + 1, &end, 16);
            if (end != s + 3 || *end != '}' || v > 0xFF) {
                return false;
            }
            out[n++] = (u8)v;
            s = end + 1;
        } else {
            out[n++] = (*s == ' ') ? (u8)'@' : (u8)*s;
            s++;
        }
    }
    return n == len;
}

static bool load_menu(const char *path, HzMenuAssets *m)
{
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return fail(path, "can't be read as JSON");
    }
    const JsonValue *songs = json_get(root, "songs");
    bool ok = json_length(songs) == HZ_SONGS;
    if (!ok) {
        fail(path, "\"songs\" needs %d records", HZ_SONGS);
    }
    for (int n = 0; ok && n < HZ_SONGS; n++) {
        const JsonValue *r = json_at(songs, n);
        const JsonValue *title = json_get(r, "title");
        const JsonValue *rep = json_get(r, "repeat");
        char where[32];
        long v;
        snprintf(where, sizeof where, "songs[%d]", n);
        ok = field(path, where, r, "song", n, n, &v);
        if (ok && json_length(title) != 2) {
            ok = fail(path, "%s: \"title\" needs two lines", where);
        }
        for (int k = 0; ok && k < 2; k++) {
            const JsonValue *line = json_at(title, k);
            ok = line != NULL && line->type == JSON_STRING
              && menu_text(line->string, m->song_titles[n][k], HZ_SONG_TITLE);
            if (!ok) {
                fail(path, "%s: title line %d needs %d characters", where, k,
                     HZ_SONG_TITLE);
            }
        }
        if (ok && (rep == NULL || rep->type != JSON_BOOL)) {
            ok = fail(path, "%s: \"repeat\" should be true or false", where);
        }
        if (ok) {
            m->song_repeat[n] = rep->boolean;
        }
    }
    const JsonValue *names = json_get(root, "map_names");
    if (ok && json_length(names) != HZ_MAPS) {
        ok = fail(path, "\"map_names\" needs %d names", HZ_MAPS);
    }
    for (int n = 0; ok && n < HZ_MAPS; n++) {
        const JsonValue *name = json_at(names, n);
        ok = name != NULL && name->type == JSON_STRING
          && menu_text(name->string, m->map_names[n], HZ_MAP_NAME);
        if (!ok) {
            fail(path, "map_names[%d] needs %d characters", n, HZ_MAP_NAME);
        }
    }
    long v = 0;
    if (ok) {
        ok = field(path, "menu", root, "sfx_count", 1, 0xFF, &v);
        m->sfx_count = (u8)v;
    }
    json_free(root);
    return ok;
}

/* The title's pictures, sheets and palettes, and the options' data. */
static bool load_title(const char *dir, HzMenuAssets *m)
{
    char path[PATH_MAX_LEN];
    char title[PATH_MAX_LEN];
    join(title, dir, "title");
    join(path, title, "tank_photo.png");
    if (!load_picture(path, m->tank_photo, 40, 28, m->tank_photo_palette)) {
        return false;
    }
    u16 logo_palette[16];
    join(path, title, "title_logo.png");
    if (!load_picture(path, m->title_logo, 29, 11, logo_palette)) {
        return false;
    }
    /* Tile 318 is the logo's last (blank) one; the 320th is its palette,
     * as the RLE entry ends with it (Title_LoadLogo reads it from there). */
    for (int i = 0; i < 16; i++) {
        m->title_logo[319 * 32 + i * 2] = (u8)(logo_palette[i] >> 8);
        m->title_logo[319 * 32 + i * 2 + 1] = (u8)logo_palette[i];
    }
    if (!load_sprite_sheet(title, "sega_logo", m->title_gfx, 0x480, 48)
        || !load_sprite_sheet(title, "copyright", m->title_gfx + 48 * 32,
                              0x4B0, 80)) {
        return false;
    }
    join(path, title, "menu_font.png");
    if (!load_tile_sheet(path, m->menu_font, HZ_MENU_FONT_TILES)) {
        return false;
    }
    u16 two[32];
    join(path, dir, "palettes/menu.png");
    if (!load_palette(path, m->menu_palette, 4)) {
        return false;
    }
    join(path, dir, "palettes/title.png");
    if (!load_palette(path, two, 2)) {
        return false;
    }
    memcpy(m->sega_palette, two, sizeof m->sega_palette);
    memcpy(m->copyright_palette, two + 16, sizeof m->copyright_palette);
    join(path, dir, "palettes/sega_logo_cycle.png");
    if (!load_colours(path, m->sega_cycle, HZ_SEGA_CYCLE)) {
        return false;
    }
    join(path, dir, "palettes/copyright_cycle.png");
    if (!load_colours(path, m->copyright_cycle, HZ_COPYRIGHT_CYCLE)) {
        return false;
    }
    join(path, title, "winner_font.png");
    if (!load_tile_sheet(path, m->winner_font, HZ_WINNER_FONT_TILES)) {
        return false;
    }
    join(path, title, "tank_photo2.png");
    if (!load_picture(path, m->tank_photo2, 40, 28,
                      m->winner_photo_palette)) {
        return false;
    }
    join(path, dir, "palettes/winner.png");
    if (!load_palette(path, m->winner_palette, 3)) {
        return false;
    }
    join(path, dir, "data/menu.json");
    return load_menu(path, m);
}

/* -- the endings ----------------------------------------------------------- */

/* Text for the story font, as tools/hz_extract.py writes it: letters and
 * digits as themselves, the punctuation the font draws on other codes
 * (. , ' " ! on : ; < = >), a space its blank '@', {XX} any other code.
 * At most `len` characters, the rest blank. False if longer or if a
 * character isn't one of these. */
static bool story_codes(const char *s, size_t n, u8 *out, int len)
{
    static const char PUNCT[] = ".,'\"!";
    int k = 0;
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        u8 code;
        if (k == len) {
            return false;
        }
        if (c == '{') {
            if (i + 3 >= n || s[i + 3] != '}') {
                return false;
            }
            char hex[3] = { s[i + 1], s[i + 2], '\0' };
            char *end = NULL;
            unsigned long v = strtoul(hex, &end, 16);
            if (end != hex + 2) {
                return false;
            }
            code = (u8)v;
            i += 4;
        } else {
            const char *p = (c != '\0') ? strchr(PUNCT, c) : NULL;
            if (c == ' ') {
                code = '@';
            } else if (p != NULL) {
                code = (u8)(0x3A + (p - PUNCT));
            } else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
                       || (c >= 'a' && c <= 'z')) {
                code = (u8)c;
            } else {
                return false;
            }
            i++;
        }
        out[k++] = code;
    }
    while (k < len) {
        out[k++] = '@';
    }
    return true;
}

/* The next line of a text file (its '\r' dropped), or NULL at the end;
 * *cursor moves past it, *line_no counts it. */
static char *next_line(char **cursor, int *line_no)
{
    char *line = *cursor;
    if (line == NULL || *line == '\0') {
        return NULL;
    }
    char *nl = strchr(line, '\n');
    if (nl != NULL) {
        *nl = '\0';
        *cursor = nl + 1;
    } else {
        *cursor = line + strlen(line);
    }
    size_t n = strlen(line);
    if (n > 0 && line[n - 1] == '\r') {
        line[n - 1] = '\0';
    }
    (*line_no)++;
    return line;
}

/* story_p1.txt / story_p2.txt: '#' lines are comments, then exactly
 * HZ_STORY_LINES lines (empty ones blank). */
static bool load_story(const char *path, u8 out[HZ_STORY_LINES][HZ_STORY_WIDTH])
{
    char *text = (char *)hz_file_read(path, NULL);
    if (text == NULL) {
        return fail(path, "can't be read");
    }
    char *cursor = text;
    int line_no = 0;
    int n = 0;
    bool ok = true;
    for (char *line; ok && (line = next_line(&cursor, &line_no)) != NULL; ) {
        if (line[0] == '#') {
            continue;
        }
        if (n == HZ_STORY_LINES) {
            ok = fail(path, "line %d: more than %d lines", line_no,
                      HZ_STORY_LINES);
        } else if (!story_codes(line, strlen(line), out[n], HZ_STORY_WIDTH)) {
            ok = fail(path, "line %d: more than %d characters, or one the "
                      "story font hasn't", line_no, HZ_STORY_WIDTH);
        }
        n++;
    }
    if (ok && n != HZ_STORY_LINES) {
        ok = fail(path, "needs %d lines, has %d", HZ_STORY_LINES, n);
    }
    free(text);
    return ok;
}

/* staff.txt: '#' lines are comments; each other line a kind (heading,
 * name, overseas, japan), then the text between the first '|' and the
 * last. The roll starts with a heading; overseas and japan once each. */
static bool load_staff(const char *path, HzEndingAssets *e)
{
    e->staff_count = 0;
    char *text = (char *)hz_file_read(path, NULL);
    if (text == NULL) {
        return fail(path, "can't be read");
    }
    char *cursor = text;
    int line_no = 0;
    bool have[2] = { false, false };
    bool ok = true;
    for (char *line; ok && (line = next_line(&cursor, &line_no)) != NULL; ) {
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        char *bar = strchr(line, '|');
        char *last = strrchr(line, '|');
        if (bar == NULL || last == bar) {
            ok = fail(path, "line %d: the text goes between two bars", line_no);
            break;
        }
        char *end = bar;
        while (end > line && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        size_t kind_len = (size_t)(end - line);
        u8 codes[HZ_STAFF_WIDTH];
        if (!story_codes(bar + 1, (size_t)(last - bar - 1), codes,
                         HZ_STAFF_WIDTH)) {
            ok = fail(path, "line %d: more than %d characters, or one the "
                      "story font hasn't", line_no, HZ_STAFF_WIDTH);
            break;
        }
        bool name = false;
        bool *seen = NULL;          /* a maker line: which */
        u8 *maker = NULL;
        if (kind_len == 7 && strncmp(line, "heading", 7) == 0) {
            name = false;
        } else if (kind_len == 4 && strncmp(line, "name", 4) == 0) {
            name = true;
        } else if (kind_len == 8 && strncmp(line, "overseas", 8) == 0) {
            seen = &have[1];
            maker = e->maker[1];
        } else if (kind_len == 5 && strncmp(line, "japan", 5) == 0) {
            seen = &have[0];
            maker = e->maker[0];
        } else {
            ok = fail(path, "line %d: the kind is heading, name, overseas or "
                      "japan", line_no);
            break;
        }
        if (maker != NULL) {
            if (*seen) {
                ok = fail(path, "line %d: a second %.*s line", line_no,
                          (int)kind_len, line);
            }
            *seen = true;
            memcpy(maker, codes, HZ_STAFF_WIDTH);
            continue;
        }
        if (e->staff_count == 0 && name) {
            ok = fail(path, "line %d: the roll starts with a heading", line_no);
        } else if (e->staff_count == HZ_STAFF_LINES) {
            ok = fail(path, "line %d: more than %d lines", line_no,
                      HZ_STAFF_LINES);
        } else {
            HzStaffLine *l = &e->staff[e->staff_count++];
            memcpy(l->text, codes, HZ_STAFF_WIDTH);
            l->name = name;
        }
    }
    if (ok && e->staff_count == 0) {
        ok = fail(path, "has no heading or name lines");
    }
    if (ok && (!have[0] || !have[1])) {
        ok = fail(path, "needs an overseas and a japan line");
    }
    free(text);
    return ok;
}

/* The endings' pictures, fonts, palettes and text (ending/). */
static bool load_ending(const char *dir, HzEndingAssets *e)
{
    char path[PATH_MAX_LEN];
    char folder[PATH_MAX_LEN];
    u16 unused[16];
    join(folder, dir, "ending");
    join(path, dir, "palettes/ending_text.png");
    if (!load_palette(path, e->text_palette, 1)) {
        return false;
    }
    join(path, folder, "story_font.png");
    if (!load_tile_sheet(path, e->story_font, HZ_STORY_FONT_TILES)) {
        return false;
    }
    join(path, folder, "ende.png");
    if (!load_picture(path, e->ende, 16, 4, unused)) {
        return false;
    }
    static const char *const PICTURES[2] = { "still_life.png", "grave.png" };
    for (int n = 0; n < 2; n++) {
        join(path, folder, PICTURES[n]);
        if (!load_picture(path, e->pictures[n], 40, 28,
                          e->picture_palettes[n])) {
            return false;
        }
    }
    join(path, folder, "forest.png");
    if (!load_picture(path, e->forest, 40, 28, e->forest_palette)) {
        return false;
    }
    static const char *const STORIES[2] = { "story_p1.txt", "story_p2.txt" };
    for (int n = 0; n < 2; n++) {
        join(path, folder, STORIES[n]);
        if (!load_story(path, e->story[n])) {
            return false;
        }
    }
    join(path, folder, "staff.txt");
    return load_staff(path, e);
}

/* A step of the load: `call` again while it refuses mods, each time with
 * the ones it refused set aside (hz_mods_set_aside, which names them);
 * the load fails when the game's own files are refused. */
#define STEP(call)                                                       \
    do {                                                                 \
        hz_mods_step();                                                  \
        while (!(call)) {                                                \
            if (!hz_mods_set_aside()) {                                  \
                return false;                                            \
            }                                                            \
            hz_mods_step();                                              \
        }                                                                \
    } while (0)

/* (Session 75, HZ_FIX_PIXELS, hitmask.c) A packed 4 bpp tile's pixel
 * rows as bits: bit 7 the left pixel, set where the colour isn't 0. */
static void tile_bits(const u8 *tile, u8 out[8])
{
    for (int r = 0; r < 8; r++) {
        u8 bits = 0;
        for (int x = 0; x < 8; x++) {
            u8 b = tile[r * 4 + x / 2];
            u8 c = (x & 1) ? (u8)(b & 0x0F) : (u8)(b >> 4);
            if (c != 0) {
                bits |= (u8)(0x80 >> x);
            }
        }
        out[r] = bits;
    }
}

static void tile_rows_build(HzAssets *a)
{
    for (int t = 0; t < HZ_MASK_TILES; t++) {
        int vram = HZ_MASK_FIRST + t;
        const u8 *tile = vram < 0x500
                         ? a->sprite_tiles + (size_t)(vram - 0x240) * 32
                         : a->hud_tiles + (size_t)(vram - 0x500) * 32;
        tile_bits(tile, a->tile_rows[t]);
    }
    for (int f = 0; f < HZ_MECH_FRAMES; f++) {
        for (int t = 0; t < 16; t++) {
            tile_bits(&a->mech_frames[f][t * 32], a->mech_rows[f][t]);
        }
    }
}

bool hz_assets_load(HzAssets *a, const char *dir)
{
    memset(a, 0, sizeof *a);
    char path[PATH_MAX_LEN];
    char tiles[PATH_MAX_LEN];
    join(tiles, dir, "tiles");

    for (int n = 0; n < HZ_MAPS; n++) {
        STEP(load_map(dir, n, &a->map[n]));
    }
    join(path, dir, "palettes/game.png");
    STEP(load_palette(path, a->game_palettes, 3));
    join(path, dir, "palettes/cycles.png");
    STEP(load_palette(path, &a->colour_cycles[0][0], 2));
    STEP(load_sprite_sheet(tiles, "mech", &a->mech_frames[0][0], 0,
                           HZ_MECH_FRAMES * 16));
    STEP(load_sprite_sheet(tiles, "sprites", a->sprite_tiles, 0x240,
                           HZ_SPRITE_TILE_BYTES / 32));
    STEP(load_sprite_sheet(tiles, "hud", a->hud_tiles, 0x500,
                           HZ_HUD_TILE_BYTES / 32));
    tile_rows_build(a);             /* (session 75) HZ_FIX_PIXELS */
    STEP(load_sprite_sheet(tiles, "order_icons",&a->order_icons[0][0], 0,
                           HZ_ORDERS * HZ_ORDER_ICON_BYTES / 32));
    join(path, tiles, "font.png");
    STEP(load_tile_sheet(path, a->font_tiles, HZ_FONT_TILES));
    join(path, dir, "screens/hud_frame.txt");
    STEP(load_hex_rows(path, &a->hud_frame[0][0], HZ_HUD_ROWS,
                       HZ_HUD_COLS));
    join(path, dir, "data/units.json");
    STEP(load_units(path, a));
    join(path, dir, "data/turrets.json");
    STEP(load_turrets(path, a));
    join(path, dir, "data/repair.json");
    STEP(load_repair(path, a));
    join(path, dir, "data/start_units.json");
    STEP(load_start_units(path, a));
    join(path, dir, "data/sound.json");
    STEP(load_sound(path, a));
    join(path, dir, "data/order_menu.json");
    STEP(load_order_menu(path, a));
    join(path, dir, "data/demos.json");
    STEP(load_demos(path, a));
    STEP(load_title(dir, &a->menu));
    STEP(load_ending(dir, &a->ending));
    /* sound/: the songs and effects as text (docs/sound.md), built into
     * SoundData, the 32 KB the Z80 driver's bank window shows, or a window
     * per song once they outgrow it; songtext names the file and line of
     * anything it refuses. */
    join(path, dir, "sound");
    STEP(songtext_build(path, a->sound_data, &a->sfx_table,
                        &a->song_windows, &a->song_window_count));
    /* (Session 50) after the songs: the drum map names their instruments */
    join(path, dir, "sound/q_drums.json");
    STEP(load_q_drums(path, a));
    return true;
}
