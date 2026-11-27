/* (Session 78) The extractor's shared parts (ex.h): the cartridge as read,
 * the files written, and hz_extract.py's writers -- write_text,
 * write_json's json_text, Pillow's indexed PNG, cram_rgb. */
#include "ex.h"
#include "extract.h"

#include "../core/hz_lzw.h"
#include "../core/hz_rle.h"
#include "../core/pngwrite.h"

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

const char *const EX_MAP_NAMES[8] = {
    "abgrund", "vulkan", "loch", "strand", "stadt", "eisfrei", "waldung",
    "oase"
};

/* Measured VA4 DAC levels (the PS2 port's, and src/core/cram.c's). */
static const u8 DAC[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };

bool ex_fail(HzEx *x, const char *fmt, ...)
{
    if (!x->failed) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(x->err, sizeof x->err, fmt, ap);
        va_end(ap);
        x->failed = true;
    }
    return false;
}

/* ---- the cartridge ---- */

u8 ex_b(const HzEx *x, u32 a)
{
    return (a < x->size) ? x->rom[a] : 0;
}

u16 ex_w(const HzEx *x, u32 a)
{
    return (u16)((ex_b(x, a) << 8) | ex_b(x, a + 1));
}

u32 ex_l(const HzEx *x, u32 a)
{
    return ((u32)ex_w(x, a) << 16) | ex_w(x, a + 2);
}

const u8 *ex_at(HzEx *x, u32 a, u32 len)
{
    if (a > x->size || len > x->size - a) {
        ex_fail(x, "$%X-$%X is past the cartridge's end", (unsigned)a,
                (unsigned)(a + len));
        return NULL;
    }
    return x->rom + a;
}

/* Room enough for any table the extractor unpacks (the forest's $8C00 is
 * the biggest). */
#define UNPACK_CAP 0x20000u

u8 *ex_lzw(HzEx *x, u32 table, u32 n, u32 *len)
{
    u32 src = 0, src_len = 0;
    if (!hz_pack_entry(x->rom, x->size, table, n, &src, &src_len)) {
        ex_fail(x, "LZW table $%X: no entry %u", (unsigned)table,
                (unsigned)n);
        return NULL;
    }
    u8 *out = malloc(UNPACK_CAP);
    if (out == NULL) {
        ex_fail(x, "out of memory");
        return NULL;
    }
    if (hz_unpack(x->rom + src, src_len, out, UNPACK_CAP, len)
        != HZ_UNPACK_OK) {
        free(out);
        ex_fail(x, "LZW table $%X entry %u: won't unpack", (unsigned)table,
                (unsigned)n);
        return NULL;
    }
    return out;
}

u8 *ex_rle(HzEx *x, u32 table, u32 n, u32 *len)
{
    u32 entry = 0, used = 0;
    if (!hz_rle_entry(x->rom, x->size, table, n, &entry)) {
        ex_fail(x, "RLE table $%X: no entry %u", (unsigned)table,
                (unsigned)n);
        return NULL;
    }
    u8 *out = malloc(UNPACK_CAP);
    if (out == NULL) {
        ex_fail(x, "out of memory");
        return NULL;
    }
    if (hz_rle_unpack(x->rom + entry, x->size - entry, out, UNPACK_CAP, len,
                      &used) != HZ_RLE_OK) {
        free(out);
        ex_fail(x, "RLE table $%X entry %u: won't unpack", (unsigned)table,
                (unsigned)n);
        return NULL;
    }
    return out;
}

/* ---- text ---- */

void exb_init(ExBuf *b)
{
    memset(b, 0, sizeof *b);
}

void exb_free(ExBuf *b)
{
    free(b->p);
    memset(b, 0, sizeof *b);
}

static void grow(ExBuf *b, size_t more)
{
    if (b->len + more + 1 <= b->cap) {
        return;
    }
    size_t cap = (b->cap < 256) ? 256 : b->cap;
    while (cap < b->len + more + 1) {
        cap *= 2;
    }
    char *p = realloc(b->p, cap);
    if (p == NULL) {
        abort();                    /* a few hundred KB at most */
    }
    b->p   = p;
    b->cap = cap;
}

void exb_putc(ExBuf *b, char c)
{
    grow(b, 1);
    b->p[b->len++] = c;
    b->p[b->len]   = '\0';
}

void exb_puts(ExBuf *b, const char *s)
{
    size_t n = strlen(s);
    grow(b, n);
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}

void exb_printf(ExBuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void exb_header(ExBuf *b, const char *line)
{
    size_t start = b->len;
    exb_puts(b, "# ");
    exb_puts(b, line);
    while (b->len > start && b->p[b->len - 1] == ' ') {
        b->len--;
    }
    b->p[b->len] = '\0';
    exb_putc(b, '\n');
}

/* ---- files ---- */

/* root/rel into out, its folders made. */
static bool full_path(HzEx *x, const char *rel, char out[EX_PATH])
{
    int n = snprintf(out, EX_PATH, "%s/%s", x->root, rel);
    if (n < 0 || n >= EX_PATH) {
        return ex_fail(x, "%s/%s: the path is too long", x->root, rel);
    }
    for (char *p = out + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
            make_dir(out);          /* there already: fine */
            *p = c;
        }
    }
    return true;
}

bool ex_write(HzEx *x, const char *rel, const void *data, size_t n)
{
    if (x->failed) {
        return false;
    }
    char path[EX_PATH];
    bool held = strcmp(rel, EX_MADE_FILE) == 0;
    char part[EX_PATH];
    if (held) {
        snprintf(part, sizeof part, "%s.part", rel);
        rel = part;
    }
    if (!full_path(x, rel, path)) {
        return false;
    }
    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        return ex_fail(x, "%s: can't be written", path);
    }
    bool ok = fwrite(data, 1, n, fh) == n;
    ok = (fclose(fh) == 0) && ok;
    if (!ok) {
        return ex_fail(x, "%s: can't be written", path);
    }
    x->files++;
    x->made_held = x->made_held || held;
    return true;
}

bool ex_write_buf(HzEx *x, const char *rel, const ExBuf *b)
{
    return ex_write(x, rel, (b->p != NULL) ? b->p : "", b->len);
}

bool ex_write_png(HzEx *x, const char *rel, int w, int h, const u8 *px,
                  const u8 *rgb, int colours, bool transparent)
{
    if (x->failed) {
        return false;
    }
    char path[EX_PATH];
    if (!full_path(x, rel, path)) {
        return false;
    }
    if (!png_write_indexed(path, w, h, px, rgb, colours, transparent)) {
        return ex_fail(x, "%s: can't be written", path);
    }
    x->files++;
    return true;
}

/* ---- colours ---- */

void ex_cram_rgb(u16 word, u8 rgb[3])
{
    rgb[0] = DAC[(word >> 1) & 7];
    rgb[1] = DAC[(word >> 5) & 7];
    rgb[2] = DAC[(word >> 9) & 7];
}

void ex_rgb_list(const u16 *words, int n, u8 *rgb)
{
    for (int i = 0; i < n; i++) {
        ex_cram_rgb(words[i], rgb + i * 3);
    }
}

void ex_palette_words(const u8 *p, int lines, u16 *words)
{
    for (int i = 0; i < 16 * lines; i++) {
        words[i] = (u16)((p[i * 2] << 8) | p[i * 2 + 1]);
    }
}

/* ---- JSON ---- */

typedef enum { J_INT, J_BOOL, J_STR, J_ARRAY, J_OBJECT } JType;

struct ExJson {
    JType      type;
    long long  num;
    char      *str;             /* J_STR's text */
    ExJson   **item;            /* J_ARRAY's items, J_OBJECT's values */
    char     **key;             /* J_OBJECT's keys */
    int        count, cap;
    bool       bad;             /* a NULL was added: out of memory */
};

static ExJson *node(JType t)
{
    ExJson *v = calloc(1, sizeof *v);
    if (v != NULL) {
        v->type = t;
    }
    return v;
}

ExJson *exj_object(void)
{
    return node(J_OBJECT);
}

ExJson *exj_array(void)
{
    return node(J_ARRAY);
}

ExJson *exj_int(long long n)
{
    ExJson *v = node(J_INT);
    if (v != NULL) {
        v->num = n;
    }
    return v;
}

ExJson *exj_bool(bool b)
{
    ExJson *v = node(J_BOOL);
    if (v != NULL) {
        v->num = b;
    }
    return v;
}

ExJson *exj_str(const char *s)
{
    ExJson *v = node(J_STR);
    if (v != NULL) {
        size_t n = strlen(s);
        v->str = malloc(n + 1);
        if (v->str == NULL) {
            free(v);
            return NULL;
        }
        memcpy(v->str, s, n + 1);
    }
    return v;
}

ExJson *exj_strf(const char *fmt, ...)
{
    char s[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    return exj_str(s);
}

static ExJson *add(ExJson *c, const char *key, ExJson *v)
{
    if (c == NULL) {
        exj_free(v);
        return NULL;
    }
    if (v == NULL) {
        c->bad = true;
        return c;
    }
    if (c->count == c->cap) {
        int cap = (c->cap == 0) ? 8 : c->cap * 2;
        ExJson **item = realloc(c->item, sizeof *item * (size_t)cap);
        char **keys = (key != NULL)
                    ? realloc(c->key, sizeof *keys * (size_t)cap) : c->key;
        if (item != NULL) {
            c->item = item;
        }
        if (key != NULL && keys != NULL) {
            c->key = keys;
        }
        if (item == NULL || (key != NULL && keys == NULL)) {
            c->bad = true;
            exj_free(v);
            return c;
        }
        c->cap = cap;
    }
    if (key != NULL) {
        size_t n = strlen(key);
        c->key[c->count] = malloc(n + 1);
        if (c->key[c->count] == NULL) {
            c->bad = true;
            exj_free(v);
            return c;
        }
        memcpy(c->key[c->count], key, n + 1);
    }
    c->item[c->count++] = v;
    return c;
}

ExJson *exj_set(ExJson *obj, const char *key, ExJson *v)
{
    return add(obj, key, v);
}

ExJson *exj_push(ExJson *arr, ExJson *v)
{
    return add(arr, NULL, v);
}

void exj_free(ExJson *v)
{
    if (v == NULL) {
        return;
    }
    for (int i = 0; i < v->count; i++) {
        exj_free(v->item[i]);
        if (v->key != NULL) {
            free(v->key[i]);
        }
    }
    free(v->item);
    free(v->key);
    free(v->str);
    free(v);
}

static bool bad_tree(const ExJson *v)
{
    if (v == NULL || v->bad) {
        return true;
    }
    for (int i = 0; i < v->count; i++) {
        if (bad_tree(v->item[i])) {
            return true;
        }
    }
    return false;
}

/* One UTF-8 code point from *s, advanced past it. */
static unsigned next_code(const unsigned char **s)
{
    const unsigned char *p = *s;
    unsigned c = *p++;
    int more = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    if (more > 0) {
        c &= 0x3Fu >> more;
        for (int i = 0; i < more && (*p & 0xC0) == 0x80; i++) {
            c = (c << 6) | (*p++ & 0x3Fu);
        }
    }
    *s = p;
    return c;
}

void exb_json_string(ExBuf *b, const char *s)
{
    exb_putc(b, '"');
    const unsigned char *p = (const unsigned char *)s;
    while (*p != '\0') {
        unsigned c = next_code(&p);
        switch (c) {
            case '"':  exb_puts(b, "\\\""); break;
            case '\\': exb_puts(b, "\\\\"); break;
            case '\n': exb_puts(b, "\\n");  break;
            case '\r': exb_puts(b, "\\r");  break;
            case '\t': exb_puts(b, "\\t");  break;
            case '\b': exb_puts(b, "\\b");  break;
            case '\f': exb_puts(b, "\\f");  break;
            default: {
                if (c < 0x20 || (c >= 0x7F && c < 0x10000)) {
                    exb_printf(b, "\\u%04x", c);
                } else if (c >= 0x10000) {
                    unsigned v = c - 0x10000;
                    exb_printf(b, "\\u%04x\\u%04x", 0xD800 + (v >> 10),
                               0xDC00 + (v & 0x3FF));
                } else {
                    exb_putc(b, (char)c);
                }
                break;
            }
        }
    }
    exb_putc(b, '"');
}

static bool plain(const ExJson *v)
{
    return v->type != J_ARRAY && v->type != J_OBJECT;
}

/* json.dumps of a plain value. */
static void dumps_plain(ExBuf *b, const ExJson *v)
{
    switch (v->type) {
        case J_INT:  exb_printf(b, "%lld", v->num); break;
        case J_BOOL: exb_puts(b, v->num ? "true" : "false"); break;
        default:     exb_json_string(b, v->str); break;
    }
}

/* json.dumps(v, separators=(', ', ': ')) of a container of plain values. */
static void dumps_one_line(ExBuf *b, const ExJson *v)
{
    bool obj = v->type == J_OBJECT;
    exb_putc(b, obj ? '{' : '[');
    for (int i = 0; i < v->count; i++) {
        if (i > 0) {
            exb_puts(b, ", ");
        }
        if (obj) {
            exb_json_string(b, v->key[i]);
            exb_puts(b, ": ");
        }
        dumps_plain(b, v->item[i]);
    }
    exb_putc(b, obj ? '}' : ']');
}

static void spaces(ExBuf *b, int n)
{
    for (int i = 0; i < n; i++) {
        exb_putc(b, ' ');
    }
}

/* json_text(v, indent, used): v starting `used` columns into a line
 * indented `indent`. */
static void json_text(ExBuf *b, const ExJson *v, int indent, int used)
{
    if (plain(v)) {
        dumps_plain(b, v);
        return;
    }
    bool all_plain = true;
    for (int i = 0; i < v->count; i++) {
        all_plain = all_plain && plain(v->item[i]);
    }
    if (all_plain) {
        ExBuf one;
        exb_init(&one);
        dumps_one_line(&one, v);
        bool fits = used + (int)one.len <= 99;
        if (fits) {
            exb_puts(b, one.p);
        }
        exb_free(&one);
        if (fits) {
            return;
        }
    }
    int pad = indent + 2;
    if (v->type == J_ARRAY && all_plain) {
        /* runs of plain values, as few lines as fit 79 columns */
        exb_puts(b, "[\n");
        ExBuf cur, piece;
        exb_init(&cur);
        exb_init(&piece);
        spaces(&cur, pad);
        bool first_line = true;
        for (int n = 0; n < v->count; n++) {
            piece.len = 0;
            if (piece.p != NULL) {
                piece.p[0] = '\0';
            }
            dumps_plain(&piece, v->item[n]);
            if (n < v->count - 1) {
                exb_putc(&piece, ',');
            }
            bool blank = cur.len == (size_t)pad;
            if ((int)(cur.len + piece.len) > 79 && !blank) {
                while (cur.len > 0 && cur.p[cur.len - 1] == ' ') {
                    cur.len--;
                }
                cur.p[cur.len] = '\0';
                if (!first_line) {
                    exb_putc(b, '\n');
                }
                exb_puts(b, cur.p);
                first_line = false;
                cur.len = 0;
                cur.p[0] = '\0';
                spaces(&cur, pad);
            }
            exb_puts(&cur, piece.p);
            exb_putc(&cur, ' ');
        }
        while (cur.len > 0 && cur.p[cur.len - 1] == ' ') {
            cur.len--;
        }
        if (cur.p != NULL) {
            cur.p[cur.len] = '\0';
        }
        if (!first_line) {
            exb_putc(b, '\n');
        }
        exb_puts(b, (cur.p != NULL) ? cur.p : "");
        exb_free(&cur);
        exb_free(&piece);
        exb_putc(b, '\n');
        spaces(b, indent);
        exb_putc(b, ']');
        return;
    }
    bool obj = v->type == J_OBJECT;
    exb_puts(b, obj ? "{\n" : "[\n");
    for (int i = 0; i < v->count; i++) {
        if (i > 0) {
            exb_puts(b, ",\n");
        }
        size_t line = b->len;
        spaces(b, pad);
        if (obj) {
            exb_json_string(b, v->key[i]);
            exb_puts(b, ": ");
        }
        json_text(b, v->item[i], indent + 2, (int)(b->len - line));
    }
    exb_putc(b, '\n');
    spaces(b, indent);
    exb_putc(b, obj ? '}' : ']');
}

bool ex_write_json(HzEx *x, const char *rel, ExJson *v)
{
    if (bad_tree(v)) {
        exj_free(v);
        return ex_fail(x, "%s: out of memory", rel);
    }
    ExBuf b;
    exb_init(&b);
    json_text(&b, v, 0, 0);
    exb_putc(&b, '\n');
    exj_free(v);
    bool ok = ex_write_buf(x, rel, &b);
    exb_free(&b);
    return ok;
}
