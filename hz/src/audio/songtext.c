#include "songtext.h"

#include "../core/hz_file.h"
#include "../core/hz_mods.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The song text into the driver's bytes: tools/hzsongtext.py's read_text
 * and build, function for function, so that both make the same image
 * from the same text (the extractor proves its text with the Python;
 * `make roundtrip` proves this against the ROM). docs/sound.md has the
 * format; the Z80 driver (asm/hz.asm's Z80Driver) reads what this
 * writes. */

#define BASE          0x8000      /* the window's first Z80 address */
#define SONG_FIRST    9           /* a song's first byte (Cmd_PlaySong) */
#define TRACKS        8           /* a song's: FM1-6, PSG tones 1-2 */
#define CHANNEL_COUNT 9           /* and the noise, for effects */
#define NOTE_FIRST    0x80        /* note bytes $80-$DF: C0-B7 */
#define NOTE_END      0xE0
#define FM_SIZE       36
#define PSG_SIZE      16
#define INSTRUMENTS   256         /* an instrument command's byte */
#define PHRASES       NOTE_END    /* an order list's phrase byte */
#define CMD_LOOP      0xFD        /* order list: the loop point; phrase:
                                     a repeat's end */
#define CMD_END       0xFF
#define BAR_GOES_ON   0xFC        /* skipped: the composer's bar marks */
#define BAR_EMPTY     0xFB
#define PATH_LEN      1024

static const char *const CHANNELS[CHANNEL_COUNT] = {
    "fm1", "fm2", "fm3", "fm4", "fm5", "fm6", "psg1", "psg2", "noise"
};

/* -- a growing byte buffer ------------------------------------------------ */

typedef struct {
    u8  *b;
    int  n, cap;
} Buf;

static bool put(Buf *b, u8 v)
{
    if (b->n == b->cap) {
        int cap = b->cap ? b->cap * 2 : 256;
        u8 *p = realloc(b->b, (size_t)cap);
        if (p == NULL) {
            fprintf(stderr, "songtext: out of memory\n");
            return false;
        }
        b->b = p;
        b->cap = cap;
    }
    b->b[b->n++] = v;
    return true;
}

static bool put_zeros(Buf *b, int n)
{
    for (int k = 0; k < n; k++) {
        if (!put(b, 0)) {
            return false;
        }
    }
    return true;
}

static bool put_buf(Buf *b, const u8 *v, int n)
{
    for (int k = 0; k < n; k++) {
        if (!put(b, v[k])) {
            return false;
        }
    }
    return true;
}

/* A little-endian word at b[at], as the driver's offsets are. */
static void set16(Buf *b, int at, int v)
{
    b->b[at] = (u8)v;
    b->b[at + 1] = (u8)(v >> 8);
}

static void buf_free(Buf *b)
{
    free(b->b);
    b->b = NULL;
    b->n = b->cap = 0;
}

/* -- a file's words -------------------------------------------------------- */

/* Each word with its line, and whether it starts the line. A comment runs
 * from a word starting with # to the line's end (the # in C#4 is a
 * sharp). */
typedef struct {
    const char *s;
    int  line;
    bool first;
} Tok;

/* An instrument's name (`fm_instrument 4 chords`): what `instrument
 * chords` picks. */
typedef struct {
    const char *name;           /* a word of the file's */
    bool fm;
    int  number;
} InstName;

typedef struct {
    char      path[PATH_LEN];
    char     *text;
    Tok      *tok;
    int       n, cap;
    InstName *names;            /* scan_names' */
    int       nnames, capnames;
} Src;

/* A refusal, by file and line (a mod's own path: it is then set aside,
 * docs/mods.md). */
static bool err(const Src *src, int line, const char *fmt, ...)
{
    va_list ap;
    hz_mods_refused(src->path);
    if (line > 0) {
        fprintf(stderr, "songtext: %s:%d: ", src->path, line);
    } else {
        fprintf(stderr, "songtext: %s: ", src->path);
    }
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return false;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

static bool add_tok(Src *src, const char *s, int line, bool first)
{
    if (src->n == src->cap) {
        int cap = src->cap ? src->cap * 2 : 1024;
        Tok *t = realloc(src->tok, sizeof *t * (size_t)cap);
        if (t == NULL) {
            fprintf(stderr, "songtext: out of memory\n");
            return false;
        }
        src->tok = t;
        src->cap = cap;
    }
    src->tok[src->n++] = (Tok){ s, line, first };
    return true;
}

/* A file's words; with bars (a tracker's), each | a word of its own. */
static bool src_open(Src *src, const char *dir, const char *name, bool bars)
{
    size_t d = strlen(dir);
    size_t n = strlen(name);
    memset(src, 0, sizeof *src);
    if (d + 1 + n + 1 > PATH_LEN) {
        fprintf(stderr, "songtext: %s/%s: path too long\n", dir, name);
        return false;
    }
    char own[PATH_LEN];
    memcpy(own, dir, d);
    own[d] = '/';
    memcpy(own + d + 1, name, n + 1);
    const char *path = hz_mods_resolve(own, src->path, PATH_LEN);
    if (path != src->path) {
        memcpy(src->path, own, d + 1 + n + 1);
    }
    src->text = (char *)hz_file_read(src->path, NULL);
    if (src->text == NULL) {
        fprintf(stderr, "songtext: %s: can't be read\n", src->path);
        return false;
    }
    int line = 1;
    bool first = true;
    bool comment = false;
    char *p = src->text;
    if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) {
        p += 3;                         /* an editor's UTF-8 mark */
    }
    while (*p != '\0') {
        if (*p == '\n') {
            *p++ = '\0';
            line++;
            first = true;
            comment = false;
            continue;
        }
        if (is_space(*p)) {
            *p++ = '\0';
            continue;
        }
        char *start = p;
        while (*p != '\0' && *p != '\n' && !is_space(*p)) {
            p++;
        }
        if (*start == '#') {
            comment = true;
        }
        if (comment) {
            continue;
        }
        char *piece = start;
        for (char *q = start; bars && q < p; q++) {
            if (*q != '|') {
                continue;
            }
            *q = '\0';
            if ((q > piece && !add_tok(src, piece, line, first))
                || !add_tok(src, "|", line, first && q == start)) {
                return false;
            }
            first = false;
            piece = q + 1;
        }
        if (piece < p && !add_tok(src, piece, line, first)) {
            return false;
        }
        first = false;
        if (*p != '\0' && *p != '\n') {
            *p++ = '\0';
        }
    }
    return true;
}

static void src_free(Src *src)
{
    free(src->text);
    free(src->tok);
    free(src->names);
    src->text = NULL;
    src->tok = NULL;
    src->names = NULL;
}

/* The index of the first word after i that starts a line. */
static int line_end(const Src *src, int i)
{
    i++;
    while (i < src->n && !src->tok[i].first) {
        i++;
    }
    return i;
}

/* The index of the first word from i on that starts a line and is one of
 * words (NULL-ended): where a block's lines stop. */
static int block_end(const Src *src, int i, const char *const *words)
{
    for (; i < src->n; i++) {
        if (!src->tok[i].first) {
            continue;
        }
        for (const char *const *w = words; *w != NULL; w++) {
            if (strcmp(src->tok[i].s, *w) == 0) {
                return i;
            }
        }
    }
    return i;
}

static const char *const SONG_BLOCKS[] = {
    "song", "volumes", "track", "phrase", "fm_instrument", "psg_instrument",
    "effects", "effect", NULL
};
static const char *const EFFECT_BLOCKS[] = {
    "song", "volumes", "phrase", "fm_instrument", "psg_instrument",
    "effects", "effect", NULL
};

/* -- numbers and notes ----------------------------------------------------- */

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int hex_digit(char c)
{
    if (is_digit(c)) {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Decimal or $hex, a + or - before it allowed. */
static bool parse_number(const char *s, long *out)
{
    bool neg = false;
    if (*s == '+' || *s == '-') {
        neg = *s == '-';
        s++;
    }
    long v = 0;
    if (*s == '$') {
        s++;
        if (*s == '\0') {
            return false;
        }
        for (; *s != '\0'; s++) {
            int d = hex_digit(*s);
            if (d < 0 || v > 0xFFFFFF) {
                return false;
            }
            v = v * 16 + d;
        }
    } else {
        if (*s == '\0') {
            return false;
        }
        for (; *s != '\0'; s++) {
            if (!is_digit(*s) || v > 0xFFFFFF) {
                return false;
            }
            v = v * 10 + (*s - '0');
        }
    }
    *out = neg ? -v : v;
    return true;
}

/* A word that looks like a number with no sign: $hex or a digit first. */
static bool looks_numeric(const char *s)
{
    return *s == '$' || is_digit(*s);
}

static bool number(const Src *src, const Tok *t, long lo, long hi, long *out)
{
    if (!parse_number(t->s, out)) {
        return err(src, t->line, "'%s' is not a number", t->s);
    }
    if (*out < lo || *out > hi) {
        return err(src, t->line, "%s is out of range (%ld to %ld)", t->s, lo,
                   hi);
    }
    return true;
}

/* A note's value (C0 = 0, a semitone a step), or false if the word isn't
 * a note's name: A-G, then # or b, then the octave. */
static bool note_value(const char *s, long *out)
{
    static const int SEMI[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A-G */
    if (*s < 'A' || *s > 'G') {
        return false;
    }
    long v = SEMI[*s - 'A'];
    s++;
    if (*s == '#') {
        v++;
        s++;
    } else if (*s == 'b') {
        v--;
        s++;
    }
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    if (!is_digit(*s)) {
        return false;
    }
    long oct = 0;
    for (; *s != '\0'; s++) {
        if (!is_digit(*s) || oct > 1000) {
            return false;
        }
        oct = oct * 10 + (*s - '0');
    }
    *out = (neg ? -oct : oct) * 12 + v;
    return true;
}

/* -- the instruments' names ----------------------------------------------- */

static bool is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* A letter, then letters, digits or _, 31 at most. */
static bool name_ok(const char *s)
{
    if (!is_letter(*s)) {
        return false;
    }
    int n = 0;
    for (; *s != '\0'; s++, n++) {
        if (!is_letter(*s) && !is_digit(*s) && *s != '_') {
            return false;
        }
    }
    return n <= 31;
}

static const InstName *find_name(const Src *src, const char *s)
{
    for (int k = 0; k < src->nnames; k++) {
        if (strcmp(src->names[k].name, s) == 0) {
            return &src->names[k];
        }
    }
    return NULL;
}

/* The instruments' names (`fm_instrument N NAME`), read before the rest of
 * the file, which names them before they come. */
static bool scan_names(Src *src)
{
    for (int i = 0; i < src->n; i++) {
        const Tok *t = &src->tok[i];
        if (!t->first || (strcmp(t->s, "fm_instrument") != 0
                          && strcmp(t->s, "psg_instrument") != 0)) {
            continue;
        }
        int stop = line_end(src, i);
        if (stop != i + 2 && stop != i + 3) {
            return err(src, t->line, "%s takes its number (and a name)",
                       t->s);
        }
        if (stop == i + 2) {
            continue;
        }
        long n;
        const char *name = src->tok[i + 2].s;
        if (!number(src, &src->tok[i + 1], 0, INSTRUMENTS - 1, &n)) {
            return false;
        }
        if (!name_ok(name)) {
            return err(src, t->line, "'%s': an instrument's name is a letter, "
                       "then letters, digits or _ (31 at most)", name);
        }
        if (find_name(src, name) != NULL) {
            return err(src, t->line, "the name %s given twice", name);
        }
        if (src->nnames == src->capnames) {
            int cap = src->capnames ? src->capnames * 2 : 64;
            InstName *p = realloc(src->names, sizeof *p * (size_t)cap);
            if (p == NULL) {
                fprintf(stderr, "songtext: out of memory\n");
                return false;
            }
            src->names = p;
            src->capnames = cap;
        }
        src->names[src->nnames++] = (InstName){ name, t->s[0] == 'f',
                                                (int)n };
    }
    return true;
}

static bool is_fm_channel(int c)
{
    return c < 6;
}

/* `instrument NAME` at word i (its value before end): the instrument it
 * names, else NULL. */
static const InstName *named(const Src *src, int i, int end)
{
    if (strcmp(src->tok[i].s, "instrument") != 0 || i + 1 >= end) {
        return NULL;
    }
    return find_name(src, src->tok[i + 1].s);
}

/* Refuses `instrument NAME` at word i where NAME is in the other list
 * than channel c's. */
static bool check_kind(const Src *src, int i, int end, int c)
{
    const InstName *nm = named(src, i, end);
    if (nm != NULL && nm->fm != is_fm_channel(c)) {
        return err(src, src->tok[i].line, "instrument %s: %s instrument, on %s",
                   nm->name, nm->fm ? "an FM" : "a PSG", CHANNELS[c]);
    }
    return true;
}

/* A phrase's first instrument name of each list (FM, PSG), checked against
 * the channels that play it. */
typedef struct {
    const char *name[2];
    int         line[2];
} NameUse;

static void note_use(const Src *src, int i, int end, NameUse *use)
{
    const InstName *nm = named(src, i, end);
    if (nm != NULL && use != NULL && use->name[!nm->fm] == NULL) {
        use->name[!nm->fm] = nm->name;
        use->line[!nm->fm] = src->tok[i].line;
    }
}

/* Refuses a phrase (an effect's by its name, else a song's by its number)
 * played on channel c whose list its instruments' names aren't in. */
static bool check_use(const Src *src, const NameUse *use, int c,
                      const char *name, int number)
{
    int other = is_fm_channel(c) ? 1 : 0;
    char num[16];
    if (use->name[other] == NULL) {
        return true;
    }
    snprintf(num, sizeof num, "%d", number);
    return err(src, use->line[other], "instrument %s: %s instrument, in "
               "phrase %s, played on %s", use->name[other],
               other ? "a PSG" : "an FM", name != NULL ? name : num,
               CHANNELS[c]);
}

/* -- commands ------------------------------------------------------------- */

typedef enum { ARG_NONE, ARG_U, ARG_S, ARG_PAN, ARG_ADD } ArgKind;

typedef struct {
    const char *word;
    u8 byte;
    ArgKind kind;
    const char *value;          /* ARG_NONE: the word after */
} Command;

/* The flags (FlagTableE0, FlagTableF0); a word may stand for two. */
static const Command COMMANDS[] = {
    { "instrument",   0xE0, ARG_U,    NULL },
    { "pan",          0xE1, ARG_PAN,  NULL },
    { "noise_mode",   0xE2, ARG_U,    NULL },
    { "slide_speed",  0xE3, ARG_U,    NULL },
    { "lfo_delay",    0xE4, ARG_U,    NULL },
    { "wait",         0xE5, ARG_U,    NULL },
    { "marker",       0xE6, ARG_U,    NULL },
    { "marker",       0xE7, ARG_ADD,  NULL },
    { "transpose",    0xE8, ARG_S,    NULL },
    { "volume",       0xE9, ARG_S,    NULL },
    { "tempo",        0xEA, ARG_S,    NULL },
    { "detune",       0xEB, ARG_S,    NULL },
    { "repeat",       0xEC, ARG_U,    NULL },
    { "fade_rate",    0xED, ARG_U,    NULL },
    { "lfo",          0xF0, ARG_NONE, "off" },
    { "lfo",          0xF1, ARG_NONE, "on" },
    { "psg_volume",   0xF2, ARG_NONE, "tone" },
    { "psg_volume",   0xF3, ARG_NONE, "noise" },
    { "slide",        0xF4, ARG_NONE, "all" },
    { "slide",        0xF5, ARG_NONE, "tied" },
    { "release_bend", 0xF6, ARG_NONE, "up" },
    { "release_bend", 0xF7, ARG_NONE, "down" },
};
#define COMMAND_COUNT ((int)(sizeof COMMANDS / sizeof COMMANDS[0]))

static const char *const PAN_WORDS[3] = { "center", "left", "right" };
static const u8 PAN_BYTES[3] = { 0x00, 0x01, 0xFF };

static bool is_command_word(const char *s)
{
    if (strcmp(s, "byte") == 0) {
        return true;
    }
    for (int k = 0; k < COMMAND_COUNT; k++) {
        if (strcmp(s, COMMANDS[k].word) == 0) {
            return true;
        }
    }
    return false;
}

/* The command or `byte N` at word *i (before end) into out; *i moves past
 * it. The caller has seen is_command_word. */
static bool take_command(const Src *src, int *i, int end, Buf *out)
{
    const Tok *t = &src->tok[*i];
    if (*i + 1 >= end) {
        return err(src, t->line, "%s needs a value", t->s);
    }
    const Tok *a = &src->tok[*i + 1];
    long v;
    *i += 2;
    if (strcmp(t->s, "byte") == 0) {
        return number(src, a, 0, 255, &v) && put(out, (u8)v);
    }
    const char *choices[4];
    int nchoices = 0;
    for (int k = 0; k < COMMAND_COUNT; k++) {
        const Command *c = &COMMANDS[k];
        if (strcmp(t->s, c->word) != 0) {
            continue;
        }
        switch (c->kind) {
        case ARG_NONE:
            if (strcmp(a->s, c->value) == 0) {
                return put(out, c->byte);
            }
            choices[nchoices++] = c->value;
            continue;
        case ARG_U:
            if (c->byte == 0xE6 && a->s[0] == '+') {
                continue;
            }
            if (c->byte == 0xE0 && find_name(src, a->s) != NULL) {
                return put(out, c->byte)
                       && put(out, (u8)find_name(src, a->s)->number);
            }
            if (c->byte == 0xE0 && is_letter(a->s[0])) {
                return err(src, t->line, "instrument %s: no instrument of "
                           "that name", a->s);
            }
            return number(src, a, 0, 255, &v) && put(out, c->byte)
                   && put(out, (u8)v);
        case ARG_ADD:
            if (a->s[0] != '+') {
                continue;
            }
            return number(src, a, 0, 255, &v) && put(out, c->byte)
                   && put(out, (u8)v);
        case ARG_S:
            return number(src, a, -128, 127, &v) && put(out, c->byte)
                   && put(out, (u8)(v & 0xFF));
        case ARG_PAN:
            for (int p = 0; p < 3; p++) {
                if (strcmp(a->s, PAN_WORDS[p]) == 0) {
                    return put(out, c->byte) && put(out, PAN_BYTES[p]);
                }
            }
            if (!looks_numeric(a->s)) {
                return err(src, t->line, "pan %s: not center, left, right "
                           "or a number", a->s);
            }
            return number(src, a, 0, 255, &v) && put(out, c->byte)
                   && put(out, (u8)v);
        }
    }
    if (nchoices == 2) {
        return err(src, t->line, "%s %s: not %s or %s", t->s, a->s,
                   choices[0], choices[1]);
    }
    return err(src, t->line, "%s %s: not a value it takes", t->s, a->s);
}

/* Track c's order list: phrase numbers, ~ and - (bars), loop, commands. */
static bool parse_order(const Src *src, int i, int end, int c, Buf *out)
{
    while (i < end) {
        const Tok *t = &src->tok[i];
        long v;
        if (strcmp(t->s, "loop") == 0) {
            if (!put(out, CMD_LOOP)) {
                return false;
            }
            i++;
        } else if (strcmp(t->s, "~") == 0) {
            if (!put(out, BAR_GOES_ON)) {
                return false;
            }
            i++;
        } else if (strcmp(t->s, "-") == 0) {
            if (!put(out, BAR_EMPTY)) {
                return false;
            }
            i++;
        } else if (looks_numeric(t->s)) {
            if (!number(src, t, 0, PHRASES - 1, &v) || !put(out, (u8)v)) {
                return false;
            }
            i++;
        } else if (is_command_word(t->s)) {
            int at = i;
            if (!take_command(src, &i, end, out)
                || !check_kind(src, at, end, c)) {
                return false;
            }
        } else {
            return err(src, t->line, "'%s': not a phrase number or a command",
                       t->s);
        }
    }
    return true;
}

/* A phrase: notes (a name, then up to three numbers under 128: length,
 * gate, volume), end_repeat, commands; use: its instruments' names. */
static bool parse_phrase(const Src *src, int i, int end, Buf *out,
                         NameUse *use)
{
    while (i < end) {
        const Tok *t = &src->tok[i];
        long v;
        if (note_value(t->s, &v)) {
            if (v < 0 || v >= NOTE_END - NOTE_FIRST) {
                return err(src, t->line, "%s is out of range (C0 to B7)",
                           t->s);
            }
            if (!put(out, (u8)(NOTE_FIRST + v))) {
                return false;
            }
            i++;
            for (int k = 0; k < 3 && i < end && looks_numeric(src->tok[i].s);
                 k++, i++) {
                if (!number(src, &src->tok[i], 0, 127, &v)
                    || !put(out, (u8)v)) {
                    return false;
                }
            }
        } else if (strcmp(t->s, "end_repeat") == 0) {
            if (!put(out, CMD_LOOP)) {
                return false;
            }
            i++;
        } else if (is_command_word(t->s)) {
            int at = i;
            if (!take_command(src, &i, end, out)) {
                return false;
            }
            note_use(src, at, end, use);
        } else if (looks_numeric(t->s)) {
            return err(src, t->line, "%s: a number with no note before it "
                       "(a note takes three at most)", t->s);
        } else {
            return err(src, t->line, "'%s': not a note or a command", t->s);
        }
    }
    return true;
}

/* -- instruments ---------------------------------------------------------- */

typedef struct {
    const char *name;
    u8 off, shift, top;
} Field;

/* An FM instrument's 36 bytes: operators in the chip's numbering 1-4
 * (Track_InitInstrument writes them to the register offsets +0, +8, +4,
 * +C); a PSG instrument's 16: the volume envelope, then the LFO bytes as
 * an FM one's. The bits no field holds are `unused`. */
static const Field FM_SCALARS[] = {
    { "algorithm", 0, 0, 7 }, { "feedback", 0, 3, 7 },
    { "fms", 1, 4, 7 }, { "ams", 1, 0, 3 },
    { "lfo_freq", 31, 0, 7 }, { "lfo_sweep", 35, 0, 255 }
};
static const Field FM_ROWS[] = {
    { "multiple", 2, 0, 15 }, { "detune", 2, 4, 7 }, { "level", 6, 0, 127 },
    { "key_scale", 10, 6, 3 }, { "attack", 10, 0, 31 }, { "am", 14, 7, 1 },
    { "decay1", 14, 0, 31 }, { "decay2", 18, 0, 31 },
    { "sustain", 22, 4, 15 }, { "release", 22, 0, 15 }
};
static const Field PSG_SCALARS[] = {
    { "speed", 0, 0, 255 }, { "start_drop", 1, 0, 255 },
    { "attack", 2, 0, 255 }, { "decay", 3, 0, 255 },
    { "sustain_drop", 4, 0, 255 }, { "sustain", 5, 0, 255 },
    { "release", 6, 0, 255 }, { "fms", 7, 4, 7 }, { "ams", 7, 0, 3 },
    { "lfo_freq", 8, 0, 7 }, { "lfo_sweep", 12, 0, 255 }
};
#define FIELDS(a) ((int)(sizeof a / sizeof a[0]))

/* The instrument block from word i to end into rec (size bytes). */
static bool parse_instrument(const Src *src, int line0, bool fm, int i,
                             int end, u8 *rec)
{
    const Field *sc = fm ? FM_SCALARS : PSG_SCALARS;
    int nsc = fm ? FIELDS(FM_SCALARS) : FIELDS(PSG_SCALARS);
    const Field *rw = fm ? FM_ROWS : NULL;
    int nrw = fm ? FIELDS(FM_ROWS) : 0;
    int size = fm ? FM_SIZE : PSG_SIZE;
    u8 used[FM_SIZE] = { 0 };
    bool seen[16] = { false };          /* scalars, then rows */
    bool seen_unused = false;

    for (int k = 0; k < nsc; k++) {
        used[sc[k].off] |= (u8)(sc[k].top << sc[k].shift);
    }
    for (int k = 0; k < nrw; k++) {
        for (int op = 0; op < 4; op++) {
            used[rw[k].off + op] |= (u8)(rw[k].top << rw[k].shift);
        }
    }
    memset(rec, 0, (size_t)size);
    while (i < end) {
        const Tok *t = &src->tok[i];
        long v;
        int k;
        for (k = 0; k < nsc && strcmp(t->s, sc[k].name) != 0; k++) {
        }
        if (k < nsc) {
            if (seen[k]) {
                return err(src, t->line, "%s given twice", t->s);
            }
            seen[k] = true;
            if (i + 1 >= end) {
                return err(src, t->line, "%s needs a value", t->s);
            }
            if (!number(src, &src->tok[i + 1], 0, sc[k].top, &v)) {
                return false;
            }
            rec[sc[k].off] |= (u8)(v << sc[k].shift);
            i += 2;
            continue;
        }
        for (k = 0; k < nrw && strcmp(t->s, rw[k].name) != 0; k++) {
        }
        if (k < nrw) {
            if (seen[nsc + k]) {
                return err(src, t->line, "%s given twice", t->s);
            }
            seen[nsc + k] = true;
            if (end - i < 5) {
                return err(src, t->line, "%s needs four values (op1-op4)",
                           t->s);
            }
            for (int op = 0; op < 4; op++) {
                if (!number(src, &src->tok[i + 1 + op], 0, rw[k].top, &v)) {
                    return false;
                }
                rec[rw[k].off + op] |= (u8)(v << rw[k].shift);
            }
            i += 5;
            continue;
        }
        if (strcmp(t->s, "unused") == 0) {
            if (seen_unused) {
                return err(src, t->line, "unused given twice");
            }
            seen_unused = true;
            int stop = line_end(src, i);
            for (i++; i < stop && i < end; i++) {
                const Tok *u = &src->tok[i];
                const char *colon = strchr(u->s, ':');
                long off;
                char head[16];
                size_t n = colon ? (size_t)(colon - u->s) : 0;
                if (colon == NULL || n == 0 || n >= sizeof head) {
                    return err(src, u->line, "'%s': unused takes "
                               "OFFSET:VALUE", u->s);
                }
                memcpy(head, u->s, n);
                head[n] = '\0';
                if (!is_digit(head[0]) || !parse_number(head, &off)) {
                    return err(src, u->line, "'%s': unused takes "
                               "OFFSET:VALUE", u->s);
                }
                if (off >= size) {
                    return err(src, u->line, "unused: offset %ld past the "
                               "%d bytes", off, size);
                }
                if (!parse_number(colon + 1, &v) || colon[1] == '+'
                    || colon[1] == '-' || v < 0 || v > 255) {
                    return err(src, u->line, "'%s': unused takes "
                               "OFFSET:VALUE", u->s);
                }
                if (v & used[off]) {
                    return err(src, u->line, "unused %s: bits a field holds",
                               u->s);
                }
                rec[off] |= (u8)v;
            }
            continue;
        }
        return err(src, t->line, "'%s': not a field of a%s instrument",
                   t->s, fm ? "n FM" : " PSG");
    }
    char missing[256];
    size_t n = 0;
    missing[0] = '\0';
    for (int k = 0; k < nsc + nrw; k++) {
        if (!seen[k]) {
            int w = snprintf(missing + n, sizeof missing - n, "%s%s",
                             n ? ", " : "",
                             k < nsc ? sc[k].name : rw[k - nsc].name);
            if (w > 0 && (size_t)w < sizeof missing - n) {
                n += (size_t)w;
            }
        }
    }
    if (n > 0) {
        return err(src, line0, "%s_instrument: %s missing", fm ? "fm" : "psg",
                   missing);
    }
    return true;
}

typedef struct {
    u8   fm[INSTRUMENTS][FM_SIZE];
    u8   psg[INSTRUMENTS][PSG_SIZE];
    bool fm_given[INSTRUMENTS];
    bool psg_given[INSTRUMENTS];
} Instruments;

/* `fm_instrument N [NAME]` / `psg_instrument N [NAME]` at word i (the name
 * scan_names'); *next: the block's end. */
static bool take_instrument(const Src *src, int i, Instruments *in,
                            const char *const *blocks, int *next)
{
    const Tok *t = &src->tok[i];
    bool fm = strcmp(t->s, "fm_instrument") == 0;
    int head = line_end(src, i);
    long n;
    if (head != i + 2 && head != i + 3) {
        return err(src, t->line, "%s takes its number (and a name)", t->s);
    }
    if (!number(src, &src->tok[i + 1], 0, INSTRUMENTS - 1, &n)) {
        return false;
    }
    bool *given = fm ? &in->fm_given[n] : &in->psg_given[n];
    if (*given) {
        return err(src, t->line, "%s %ld given twice", t->s, n);
    }
    *given = true;
    *next = block_end(src, head, blocks);
    return parse_instrument(src, t->line, fm, head, *next,
                            fm ? in->fm[n] : in->psg[n]);
}

/* The instruments, numbered from 0 with none left out, onto out; *offset:
 * where the FM ones start, *psg_offset the PSG ones. */
static bool put_instruments(const Src *src, const Instruments *in, Buf *out,
                            int *fm_offset, int *psg_offset)
{
    int nfm = 0, npsg = 0;
    for (int k = 0; k < INSTRUMENTS; k++) {
        if (in->fm_given[k]) {
            nfm = k + 1;
        }
        if (in->psg_given[k]) {
            npsg = k + 1;
        }
    }
    for (int k = 0; k < nfm; k++) {
        if (!in->fm_given[k]) {
            return err(src, 0, "fm_instrument %d missing (they run from 0 "
                       "with none left out)", k);
        }
    }
    for (int k = 0; k < npsg; k++) {
        if (!in->psg_given[k]) {
            return err(src, 0, "psg_instrument %d missing (they run from 0 "
                       "with none left out)", k);
        }
    }
    *fm_offset = out->n;
    for (int k = 0; k < nfm; k++) {
        if (!put_buf(out, in->fm[k], FM_SIZE)) {
            return false;
        }
    }
    *psg_offset = out->n;
    for (int k = 0; k < npsg; k++) {
        if (!put_buf(out, in->psg[k], PSG_SIZE)) {
            return false;
        }
    }
    return true;
}

/* -- a song --------------------------------------------------------------- */

typedef struct {
    Buf     track[TRACKS];
    bool    track_given[TRACKS];
    Buf     phrase[PHRASES];
    bool    phrase_given[PHRASES];
    NameUse use[PHRASES];
    Instruments in;
} SongParts;

static void song_parts_free(SongParts *p)
{
    for (int k = 0; k < TRACKS; k++) {
        buf_free(&p->track[k]);
    }
    for (int k = 0; k < PHRASES; k++) {
        buf_free(&p->phrase[k]);
    }
}

/* `song volume V tempo T transpose X [phrases N]`. */
static bool take_song_line(const Src *src, int i, int stop, u8 header[3],
                           long *phrases)
{
    static const char *const KEYS[4] = { "volume", "tempo", "transpose",
                                         "phrases" };
    static const long LO[4] = { 0, 0, -128, 0 };
    static const long HI[4] = { 255, 255, 255, PHRASES };
    long vals[4];
    bool have[4] = { false, false, false, false };
    for (i++; i < stop; i += 2) {
        int k;
        for (k = 0; k < 4 && strcmp(src->tok[i].s, KEYS[k]) != 0; k++) {
        }
        if (k == 4 || i + 1 >= stop) {
            return err(src, src->tok[i].line, "'%s': song takes volume, "
                       "tempo, transpose (and phrases)", src->tok[i].s);
        }
        if (!number(src, &src->tok[i + 1], LO[k], HI[k], &vals[k])) {
            return false;
        }
        have[k] = true;
    }
    for (int k = 0; k < 3; k++) {
        if (!have[k]) {
            return err(src, src->tok[i - 1].line, "song: %s missing",
                       KEYS[k]);
        }
        header[k] = (u8)(vals[k] & 0xFF);
    }
    *phrases = have[3] ? vals[3] : -1;
    return true;
}

/* A song's parts laid out as hzsongtext.py's song_bytes: the header, the
 * channel volumes, the order lists' table and lists, the phrase table
 * (count entries) and phrases, the FM and PSG instruments. */
static bool song_layout(const Src *src, const SongParts *p,
                        const u8 header[3], const u8 volumes[9], int count,
                        Buf *out)
{
    bool ok = put_zeros(out, 16);
    if (ok) {
        out->b[0] = SONG_FIRST;
        memcpy(&out->b[1], header, 3);
        set16(out, 4, out->n);
        ok = put_buf(out, volumes, 9);
    }
    int table = out->n;
    if (ok) {
        set16(out, 6, table);
        ok = put_zeros(out, 2 * TRACKS);
    }
    for (int c = 0; ok && c < TRACKS; c++) {
        set16(out, table + 2 * c, out->n);
        ok = put_buf(out, p->track[c].b, p->track[c].n) && put(out, CMD_END);
    }
    /* Track_NextPhrase hands back the phrase table offset's low byte and
     * the caller takes $FF for the order list's end: keep it off $FF. */
    if (ok && (out->n & 0xFF) == 0xFF) {
        ok = put(out, 0);
    }
    table = out->n;
    if (ok) {
        set16(out, 8, table);
        ok = put_zeros(out, 2 * count);
    }
    for (int k = 0; ok && k < PHRASES; k++) {
        if (p->phrase_given[k]) {
            set16(out, table + 2 * k, out->n);
            ok = put_buf(out, p->phrase[k].b, p->phrase[k].n)
                 && put(out, CMD_END);
        }
    }
    int fm_offset = 0, psg_offset = 0;
    if (ok) {
        ok = put_instruments(src, &p->in, out, &fm_offset, &psg_offset);
    }
    if (ok) {
        set16(out, 10, fm_offset);
        set16(out, 12, psg_offset);
        set16(out, 14, out->n - 1);
        if (out->n > 0x7FFF) {
            ok = err(src, 0, "a song of %d bytes: past the window", out->n);
        }
    }
    return ok;
}

/* A song file into its bytes (song_layout). */
static bool song_bytes(const Src *src, Buf *out)
{
    SongParts *p = calloc(1, sizeof *p);
    if (p == NULL) {
        fprintf(stderr, "songtext: out of memory\n");
        return false;
    }
    u8 header[3];
    u8 volumes[9];
    bool have_song = false, have_volumes = false;
    long phrases = -1;
    int track_line[TRACKS] = { 0 };
    bool ok = true;
    int i = 0;

    while (ok && i < src->n) {
        const Tok *t = &src->tok[i];
        int stop = line_end(src, i);
        long v;
        if (strcmp(t->s, "song") == 0) {
            if (have_song) {
                ok = err(src, t->line, "song given twice");
                break;
            }
            have_song = true;
            ok = take_song_line(src, i, stop, header, &phrases);
            i = stop;
        } else if (strcmp(t->s, "volumes") == 0) {
            if (stop - i != 10) {
                ok = err(src, t->line, "volumes takes nine (fm1-fm6, psg1, "
                         "psg2 and one nothing reads)");
                break;
            }
            for (int k = 0; ok && k < 9; k++) {
                ok = number(src, &src->tok[i + 1 + k], 0, 255, &v);
                volumes[k] = (u8)v;
            }
            have_volumes = true;
            i = stop;
        } else if (strcmp(t->s, "track") == 0) {
            int c;
            for (c = 0; stop == i + 2 && c < TRACKS
                        && strcmp(src->tok[i + 1].s, CHANNELS[c]) != 0; c++) {
            }
            if (stop != i + 2 || c == TRACKS) {
                ok = err(src, t->line, "track takes a channel, fm1-fm6, psg1 "
                         "or psg2");
                break;
            }
            if (p->track_given[c]) {
                ok = err(src, t->line, "track %s given twice", CHANNELS[c]);
                break;
            }
            p->track_given[c] = true;
            track_line[c] = t->line;
            int next = block_end(src, stop, SONG_BLOCKS);
            ok = parse_order(src, stop, next, c, &p->track[c]);
            i = next;
        } else if (strcmp(t->s, "phrase") == 0) {
            if (stop != i + 2) {
                ok = err(src, t->line, "phrase takes its number");
                break;
            }
            if (!number(src, &src->tok[i + 1], 0, PHRASES - 1, &v)) {
                ok = false;
                break;
            }
            if (p->phrase_given[v]) {
                ok = err(src, t->line, "phrase %ld given twice", v);
                break;
            }
            p->phrase_given[v] = true;
            int next = block_end(src, stop, SONG_BLOCKS);
            ok = parse_phrase(src, stop, next, &p->phrase[v], &p->use[v]);
            i = next;
        } else if (strcmp(t->s, "fm_instrument") == 0
                   || strcmp(t->s, "psg_instrument") == 0) {
            ok = take_instrument(src, i, &p->in, SONG_BLOCKS, &i);
        } else {
            ok = err(src, t->line, "'%s': not song, volumes, track, phrase, "
                     "fm_instrument or psg_instrument", t->s);
        }
    }
    if (ok && !have_song) {
        ok = err(src, 0, "no song line");
    }
    if (ok && !have_volumes) {
        ok = err(src, 0, "no volumes line");
    }
    int defined = 0;
    for (int k = 0; k < PHRASES; k++) {
        if (p->phrase_given[k]) {
            defined = k + 1;
        }
    }
    if (ok && phrases >= 0 && phrases < defined) {
        ok = err(src, 0, "phrases %ld: phrase %d is defined", phrases,
                 defined - 1);
    }
    int count = phrases >= 0 ? (int)phrases : defined;
    for (int c = 0; ok && c < TRACKS; c++) {
        const Buf *b = &p->track[c];
        for (int k = 0; ok && k < b->n; k++) {
            /* A phrase number: a byte under $E0 not a command's value. */
            u8 x = b->b[k];
            if (x >= 0xE0 && x <= 0xEF) {
                k++;
            } else if (x < NOTE_END && !p->phrase_given[x]) {
                ok = err(src, track_line[c], "track %s plays phrase %d, which "
                         "is not defined", CHANNELS[c], x);
            } else if (x < NOTE_END) {
                ok = check_use(src, &p->use[x], c, NULL, x);
            }
        }
    }
    if (ok) {
        ok = song_layout(src, p, header, volumes, count, out);
    }
    song_parts_free(p);
    free(p);
    return ok;
}

/* -- a tracker ------------------------------------------------------------ */

/* A song laid out as a tracker (docs/sound.md, "The tracker"):
 * tools/hztracker.py's reader and build_channels, function for function,
 * the same bytes and the same refusals. Its cells become order lists and
 * phrases (a section's cells a phrase, the same bytes the same phrase),
 * laid out as a song file's (song_layout). */

#define TRK_INSTRUMENT 0xE0
#define TRK_WAIT       0xE5
#define TRK_TRANSPOSE  0xE8
#define TRK_REPEAT     0xEC
#define NOTE_MAX       127        /* a note's length or gate: under $80 */
#define NOTES          0x60       /* the driver's notes, C0-B7 */
#define PSG_OCTAVE     12         /* a PSG column names a note an octave up */
#define GATE_DEFAULT   (-1)       /* let go 2 ticks before the next cell */
#define GATE_HOLD      (-2)       /* held into the next cell */
#define TRK_LIMIT      9999       /* a section's number, rows, ticks */

static const char *const TRK_BLOCKS[] = {
    "song", "volumes", "channel", "section", "row", "fm_instrument",
    "psg_instrument", NULL
};

typedef struct {
    long tick;
    int  line;
    int  section;
    u8   channel;
    bool end;                   /* an end cell: no note */
    u8   note;                  /* the driver's */
    int  gate;                  /* GATE_DEFAULT, GATE_HOLD or ticks */
    int  volume;                /* -1: the column's last note's */
    int  cmd, ncmd;             /* its commands' bytes in Trk.cmds */
    int  loop;                  /* `loop` among them (a byte offset), -1 */
} TrkCell;

typedef struct {
    long start, ticks, row_ticks;
    int  line;
} TrkSection;

typedef struct {
    const Src  *src;
    Buf         cmds;
    TrkCell    *cell;
    int         ncell, capcell;
    TrkSection *sec;
    int         nsec, capsec;
    bool        line_given[TRACKS];
    int         line_cmd[TRACKS], line_ncmd[TRACKS], line_loop[TRACKS];
    int         ncolumns;       /* -1: section 00's heading not read */
    u8          columns[TRACKS];
    int         nheads;         /* -1: this section's heading not read */
    u8          head[TRACKS];
    int         copy[TRACKS];   /* -1: written out */
    long        row_at;
    int         sec_first;      /* this section's first cell */
    int         volume[TRACKS];
    bool        have_song, have_volumes;
    u8          header[3], volumes[9];
} Trk;

static bool trk_is_psg(int c)
{
    return c >= 6;
}

/* Words a to b joined by single spaces, as the Python reader sees a part
 * of a row. */
static const char *trk_join(const Src *src, int a, int b, char *out,
                            size_t size)
{
    size_t n = 0;
    out[0] = '\0';
    for (int i = a; i < b; i++) {
        int w = snprintf(out + n, size - n, "%s%s", i > a ? " " : "",
                         src->tok[i].s);
        if (w < 0 || (size_t)w >= size - n) {
            break;
        }
        n += (size_t)w;
    }
    return out;
}

/* A cell's note name (C-4, C#4, Db4 or C4: one digit of octave) as the
 * driver's note: on a PSG channel an octave down. */
static bool trk_note(const char *s, int c, long *out)
{
    static const int SEMI[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A-G */
    if (*s < 'A' || *s > 'G') {
        return false;
    }
    long v = SEMI[*s - 'A'];
    s++;
    if (*s == '#') {
        v++;
        s++;
    } else if (*s == 'b') {
        v--;
        s++;
    } else if (*s == '-') {
        s++;
    }
    if (!is_digit(s[0]) || s[1] != '\0') {
        return false;
    }
    v += (s[0] - '0') * 12;
    *out = trk_is_psg(c) ? v - PSG_OCTAVE : v;
    return true;
}

/* All digits, at least one. */
static bool all_digits(const char *s)
{
    if (*s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (!is_digit(*s)) {
            return false;
        }
    }
    return true;
}

/* A number from a string not a word of the file's (vN's N, = NN's NN). */
static bool trk_number(const Src *src, int line, const char *s, long lo,
                       long hi, long *out)
{
    Tok t = { s, line, false };
    return number(src, &t, lo, hi, out);
}

/* A command (or `loop`) at word *i, before end, of channel c into
 * t->cmds; *loop gets its place among the bytes from `from` when it is
 * `loop`. A transpose only in an end cell (end_cell): the next turn's. */
static bool trk_change(Trk *t, int *i, int end, int c, int from, int *loop,
                       bool end_cell)
{
    const Src *src = t->src;
    const Tok *w = &src->tok[*i];
    if (strcmp(w->s, "loop") == 0) {
        if (*loop >= 0) {
            return err(src, w->line, "loop given twice");
        }
        *loop = t->cmds.n - from;
        (*i)++;
        return true;
    }
    if (!is_command_word(w->s)) {
        return err(src, w->line, "'%s': not a note's gate or volume, or a "
                   "command", w->s);
    }
    int at = t->cmds.n;
    int word = *i;
    if (!take_command(src, i, end, &t->cmds)) {
        return false;
    }
    if (strcmp(w->s, "byte") == 0) {
        return err(src, w->line, "byte %s: a tracker takes commands, not "
                   "bytes", src->tok[*i - 1].s);
    }
    switch (t->cmds.b[at]) {
    case TRK_TRANSPOSE:
        return end_cell || err(src, w->line, "%s: the notes are written as "
                               "heard, so there is no transpose (but in an "
                               "end cell: the next turn's)", w->s);
    case TRK_WAIT:
        return err(src, w->line, "%s: a channel starts where its first cell "
                   "is, and goes on from cell to cell", w->s);
    case TRK_REPEAT:
        return err(src, w->line, "%s: a tracker writes its repeats out",
                   w->s);
    default:
        return check_kind(src, word, end, c);
    }
}

static TrkCell *trk_new_cell(Trk *t)
{
    if (t->ncell == t->capcell) {
        int cap = t->capcell ? t->capcell * 2 : 1024;
        TrkCell *c = realloc(t->cell, sizeof *c * (size_t)cap);
        if (c == NULL) {
            fprintf(stderr, "songtext: out of memory\n");
            return NULL;
        }
        t->cell = c;
        t->capcell = cap;
    }
    return &t->cell[t->ncell++];
}

/* A cell's words a to b: a note (or `end`), @N, its gate (`hold`, `gate
 * N`) and volume (vN), the commands before it. Nothing if empty or `...`;
 * *cell NULL then. */
static bool trk_cell(Trk *t, int a, int b, int c, int line, long tick,
                     TrkCell **cell)
{
    const Src *src = t->src;
    const TrkSection *sec = &t->sec[t->nsec - 1];
    *cell = NULL;
    if (a == b || (b == a + 1 && strcmp(src->tok[a].s, "...") == 0)) {
        return true;
    }
    TrkCell x = { 0 };
    x.tick = tick;
    x.line = line;
    x.section = t->nsec - 1;
    x.channel = (u8)c;
    x.gate = GATE_DEFAULT;
    x.volume = -1;
    x.cmd = t->cmds.n;
    x.loop = -1;
    const char *head = src->tok[a].s;
    bool gate_given = false;
    if (strcmp(head, "end") == 0) {
        x.end = true;
    } else {
        long v;
        if (!trk_note(head, c, &v)) {
            return err(src, line, "'%s': a cell starts with a note, `...` or "
                       "end", head);
        }
        if (v < 0 || v >= NOTES) {
            return err(src, line, "%s is out of range (%s)", head,
                       trk_is_psg(c) ? "C-1 to B-8 on a PSG channel"
                                     : "C-0 to B-7");
        }
        x.note = (u8)v;
    }
    int i = a + 1;
    if (i < b && src->tok[i].s[0] == '@') {
        const char *s = src->tok[i].s;
        long n = 0;
        if (all_digits(s + 1) && strlen(s + 1) < 9) {
            n = strtol(s + 1, NULL, 10);
        } else if (all_digits(s + 1)) {
            n = TRK_LIMIT * TRK_LIMIT;
        }
        if (!(n > 0 && n < sec->row_ticks)) {
            return err(src, line, "%s: a cell is 1 to %ld ticks after its row "
                       "(@N)", s, sec->row_ticks - 1);
        }
        x.tick += n;
        i++;
    }
    while (i < b) {
        const char *w = src->tok[i].s;
        bool volume = w[0] == 'v' && all_digits(w + 1);
        if (strcmp(w, "hold") == 0 || strcmp(w, "gate") == 0 || volume) {
            if (x.end) {
                return err(src, line, "%s: an end cell has no note", w);
            }
            if (volume ? x.volume >= 0 : gate_given) {
                return err(src, line, "%s: the %s given twice", w,
                           volume ? "volume" : "gate");
            }
            long v;
            if (strcmp(w, "hold") == 0) {
                x.gate = GATE_HOLD;
                gate_given = true;
            } else if (strcmp(w, "gate") == 0) {
                if (i + 1 >= b) {
                    return err(src, line, "gate needs a value");
                }
                i++;
                if (!number(src, &src->tok[i], 0, NOTE_MAX, &v)) {
                    return false;
                }
                x.gate = (int)v;
                gate_given = true;
            } else {
                if (!trk_number(src, line, w + 1, 0, NOTE_MAX, &v)) {
                    return false;
                }
                x.volume = (int)v;
            }
            i++;
            continue;
        }
        if (!trk_change(t, &i, b, c, x.cmd, &x.loop, x.end)) {
            return false;
        }
    }
    x.ncmd = t->cmds.n - x.cmd;
    TrkCell *p = trk_new_cell(t);
    if (p == NULL) {
        return false;
    }
    *p = x;
    *cell = p;
    return true;
}

/* `song volume V tempo T`. */
static bool trk_song_line(Trk *t, int i, int stop)
{
    const Src *src = t->src;
    int line = src->tok[i].line;
    long vals[2];
    bool have[2] = { false, false };
    static const char *const KEYS[2] = { "volume", "tempo" };
    if (t->have_song) {
        return err(src, line, "song given twice");
    }
    for (i++; i < stop; i += 2) {
        const char *w = src->tok[i].s;
        if (strcmp(w, "transpose") == 0) {
            return err(src, line, "transpose: the notes are written as heard, "
                       "so there is no transpose");
        }
        int k = strcmp(w, "volume") == 0 ? 0 : strcmp(w, "tempo") == 0 ? 1
                                                                      : -1;
        if (k < 0 || i + 1 >= stop) {
            return err(src, line, "'%s': song takes volume and tempo", w);
        }
        if (!number(src, &src->tok[i + 1], 0, 255, &vals[k])) {
            return false;
        }
        have[k] = true;
    }
    for (int k = 0; k < 2; k++) {
        if (!have[k]) {
            return err(src, line, "song: %s missing", KEYS[k]);
        }
    }
    t->header[0] = (u8)vals[0];
    t->header[1] = (u8)vals[1];
    t->header[2] = 0;
    t->have_song = true;
    return true;
}

/* `channel NAME [commands] [loop]`: what the channel does as the song
 * starts. */
static bool trk_channel_line(Trk *t, int i, int stop)
{
    const Src *src = t->src;
    int line = src->tok[i].line;
    int c;
    if (t->nsec > 0) {
        return err(src, line, "channel lines go before the sections");
    }
    for (c = 0; i + 1 < stop && c < TRACKS
                && strcmp(src->tok[i + 1].s, CHANNELS[c]) != 0; c++) {
    }
    if (i + 1 >= stop || c == TRACKS) {
        return err(src, line, "channel takes fm1-fm6, psg1 or psg2");
    }
    if (t->line_given[c]) {
        return err(src, line, "channel %s given twice", CHANNELS[c]);
    }
    t->line_given[c] = true;
    t->line_cmd[c] = t->cmds.n;
    t->line_loop[c] = -1;
    for (i += 2; i < stop;) {
        if (!trk_change(t, &i, stop, c, t->line_cmd[c], &t->line_loop[c],
                        false)) {
            return false;
        }
    }
    t->line_ncmd[c] = t->cmds.n - t->line_cmd[c];
    return true;
}

/* A note's volume not written is the last note's in its column (the
 * driver keeps it); a copy's are its section's, already so. */
static bool trk_close_section(Trk *t)
{
    if (t->nsec == 0) {
        return true;
    }
    if (t->nheads < 0) {
        return err(t->src, t->sec[t->nsec - 1].line, "section %02d has no "
                   "heading row (row | fm1 | ...)", t->nsec - 1);
    }
    for (int k = t->sec_first; k < t->ncell; k++) {
        TrkCell *x = &t->cell[k];
        if (!x->end) {
            if (x->volume < 0) {
                x->volume = t->volume[x->channel];
            }
            t->volume[x->channel] = x->volume;
        }
    }
    return true;
}

/* `section NN  ROWS rows x TICKS ticks`. */
static bool trk_section_line(Trk *t, int i, int stop)
{
    const Src *src = t->src;
    int line = src->tok[i].line;
    if (!trk_close_section(t)) {
        return false;
    }
    if (stop - i != 7
        || (strcmp(src->tok[i + 3].s, "rows") != 0
            && strcmp(src->tok[i + 3].s, "row") != 0)
        || strcmp(src->tok[i + 4].s, "x") != 0
        || (strcmp(src->tok[i + 6].s, "ticks") != 0
            && strcmp(src->tok[i + 6].s, "tick") != 0)) {
        return err(src, line, "section takes NN  ROWS rows x TICKS ticks");
    }
    long k, rows, row_ticks;
    if (!number(src, &src->tok[i + 1], 0, TRK_LIMIT, &k)) {
        return false;
    }
    if (k != t->nsec) {
        return err(src, line, "section %s: the next is %02d (they run from 00 "
                   "in order)", src->tok[i + 1].s, t->nsec);
    }
    if (!number(src, &src->tok[i + 2], 1, TRK_LIMIT, &rows)
        || !number(src, &src->tok[i + 5], 1, TRK_LIMIT, &row_ticks)) {
        return false;
    }
    if (t->nsec == t->capsec) {
        int cap = t->capsec ? t->capsec * 2 : 64;
        TrkSection *s = realloc(t->sec, sizeof *s * (size_t)cap);
        if (s == NULL) {
            fprintf(stderr, "songtext: out of memory\n");
            return false;
        }
        t->sec = s;
        t->capsec = cap;
    }
    TrkSection *last = t->nsec ? &t->sec[t->nsec - 1] : NULL;
    t->sec[t->nsec++] = (TrkSection){
        last ? last->start + last->ticks : 1, rows * row_ticks, row_ticks,
        line };
    t->nheads = -1;
    t->row_at = -1;
    t->sec_first = t->ncell;
    return true;
}

/* A heading part: `fm1`, or `fm1 = NN` (a section it plays again), as
 * the Python's \s*(\w+)\s*(?:=\s*([0-9]+)\s*)? on the joined words. */
static bool trk_column(const char *s, char *name, size_t nsize,
                       char *digits, size_t dsize)
{
    size_t n = 0;
    while (*s == ' ') {
        s++;
    }
    while (is_digit(*s) || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')
           || *s == '_') {
        if (n + 1 < nsize) {
            name[n++] = *s;
        }
        s++;
    }
    name[n] = '\0';
    digits[0] = '\0';
    if (n == 0) {
        return false;
    }
    while (*s == ' ') {
        s++;
    }
    if (*s == '\0') {
        return true;
    }
    if (*s++ != '=') {
        return false;
    }
    while (*s == ' ') {
        s++;
    }
    n = 0;
    while (is_digit(*s)) {
        if (n + 1 < dsize) {
            digits[n++] = *s;
        }
        s++;
    }
    digits[n] = '\0';
    while (*s == ' ') {
        s++;
    }
    return n > 0 && *s == '\0';
}

/* `row | fm1 | fm2 = NN | ...`: the section's columns (section 00's in
 * every section), each written out or played again from section NN. The
 * parts: words bar[p] + 1 to bar[p + 1] (the last to stop). */
static bool trk_heading(Trk *t, int line, const int *bar, int nbar, int stop)
{
    const Src *src = t->src;
    int k = t->nsec - 1;
    char text[256];
    if (t->nheads >= 0) {
        return err(src, line, "section %02d has two heading rows", k);
    }
    int *head = malloc(sizeof *head * (size_t)(2 * nbar + 1));
    if (head == NULL) {
        fprintf(stderr, "songtext: out of memory\n");
        return false;
    }
    int *copy = head + nbar;
    bool ok = true;
    for (int p = 0; ok && p < nbar; p++) {
        int b = p + 1 < nbar ? bar[p + 1] : stop;
        char name[16], digits[16];
        int c;
        trk_join(src, bar[p] + 1, b, text, sizeof text);
        ok = trk_column(text, name, sizeof name, digits, sizeof digits);
        for (c = 0; ok && c < TRACKS && strcmp(name, CHANNELS[c]) != 0; c++) {
        }
        if (!ok || c == TRACKS) {
            ok = err(src, line, "'%s': a column is a channel (fm1-fm6, psg1, "
                     "psg2), and = NN if it plays section NN's again", text);
            break;
        }
        head[p] = c;
        copy[p] = -1;
        if (digits[0] != '\0') {
            long v = 0;
            ok = trk_number(src, line, digits, 0, TRK_LIMIT, &v);
            if (ok && v >= k) {
                ok = err(src, line, "%s = %s: an earlier section", name,
                         digits);
            } else if (ok && t->sec[v].ticks != t->sec[k].ticks) {
                ok = err(src, line, "%s = %02ld: section %02ld is %ld ticks, "
                         "this one %ld", name, v, v, t->sec[v].ticks,
                         t->sec[k].ticks);
            }
            copy[p] = (int)v;
        }
    }
    for (int x = 0; ok && x < nbar; x++) {
        for (int y = 0; ok && y < x; y++) {
            if (head[x] == head[y]) {
                ok = err(src, line, "a channel twice in the heading");
            }
        }
    }
    if (ok) {
        t->nheads = nbar;                   /* 8 at most: none twice */
        for (int x = 0; x < nbar; x++) {
            t->head[x] = (u8)head[x];
            t->copy[x] = copy[x];
        }
    }
    free(head);
    if (!ok) {
        return false;
    }
    bool same = t->ncolumns == t->nheads;
    for (int x = 0; same && x < t->nheads; x++) {
        same = t->columns[x] == t->head[x];
    }
    if (t->ncolumns < 0) {
        t->ncolumns = t->nheads;
        memcpy(t->columns, t->head, (size_t)t->nheads);
    } else if (!same) {
        size_t n = 0;
        text[0] = '\0';
        for (int x = 0; x < t->ncolumns; x++) {
            int w = snprintf(text + n, sizeof text - n, "%s%s",
                             x ? ", " : "", CHANNELS[t->columns[x]]);
            n += w > 0 ? (size_t)w : 0;
        }
        return err(src, line, "the columns are %s, as in section 00", text);
    }
    /* A copy's cells: its section's, moved here. */
    for (int x = 0; x < t->nheads; x++) {
        if (t->copy[x] < 0) {
            continue;
        }
        const TrkSection *from = &t->sec[t->copy[x]];
        int count = t->ncell;
        for (int n = 0; n < count; n++) {
            if (t->cell[n].section != t->copy[x]
                || t->cell[n].channel != t->head[x]) {
                continue;
            }
            TrkCell *p = trk_new_cell(t);
            if (p == NULL) {
                return false;
            }
            *p = t->cell[n];
            p->tick = p->tick - from->start + t->sec[k].start;
            p->line = line;
            p->section = k;
        }
    }
    return true;
}

/* ` NN | cell | cell ...`: row NN's cells, a column each; the number in
 * words first to bar[0]. */
static bool trk_row(Trk *t, int line, const int *bar, int nbar, int first,
                    int stop)
{
    const Src *src = t->src;
    const TrkSection *sec = &t->sec[t->nsec - 1];
    int end = nbar > 0 ? bar[0] : stop;
    char text[256];
    if (end != first + 1 || !all_digits(src->tok[first].s)) {
        return err(src, line, "'%s': a row starts with its number",
                   trk_join(src, first, end, text, sizeof text));
    }
    const char *digits = src->tok[first].s;
    long n = strlen(digits) < 9 ? strtol(digits, NULL, 10)
                                : TRK_LIMIT * TRK_LIMIT;
    if (n <= t->row_at || n * sec->row_ticks >= sec->ticks) {
        return err(src, line, "row %s: the rows run from 00 to %02ld in order",
                   digits, sec->ticks / sec->row_ticks - 1);
    }
    t->row_at = n;
    if (nbar != t->nheads) {
        return err(src, line, "%d cells for %d columns", nbar, t->nheads);
    }
    for (int p = 0; p < nbar; p++) {
        int b = p + 1 < nbar ? bar[p + 1] : stop;
        int c = t->head[p];
        TrkCell *cell;
        if (!trk_cell(t, bar[p] + 1, b, c, line,
                      sec->start + n * sec->row_ticks, &cell)) {
            return false;
        }
        if (cell != NULL && t->copy[p] >= 0) {
            return err(src, line, "%s = %02d: its cells are section %02d's; "
                       "leave them blank", CHANNELS[c], t->copy[p],
                       t->copy[p]);
        }
    }
    return true;
}

/* A grid line (a heading or a row) from word i to stop: its parts
 * between the bars, a | after the last column dropped. */
static bool trk_grid_line(Trk *t, int i, int stop)
{
    const Src *src = t->src;
    int line = src->tok[i].line;
    if (t->nsec == 0) {
        return err(src, line, "a row before any section");
    }
    int *bar = malloc(sizeof *bar * (size_t)(stop - i + 1));
    if (bar == NULL) {
        fprintf(stderr, "songtext: out of memory\n");
        return false;
    }
    int nbar = 0;
    for (int k = i; k < stop; k++) {
        if (strcmp(src->tok[k].s, "|") == 0) {
            bar[nbar++] = k;
        }
    }
    if (t->nheads >= 0 && nbar == t->nheads + 1 && bar[nbar - 1] == stop - 1) {
        nbar--;
        stop--;
    }
    bool ok;
    int end = nbar > 0 ? bar[0] : stop;
    if (end == i + 1 && strcmp(src->tok[i].s, "row") == 0) {
        ok = trk_heading(t, line, bar, nbar, stop);
    } else if (t->nheads < 0) {
        ok = err(src, line, "a row before the section's heading (row | fm1 "
                 "| ...)");
    } else {
        ok = trk_row(t, line, bar, nbar, i, stop);
    }
    free(bar);
    return ok;
}

/* Where a tracker's instrument block stops: a block word or a row. */
static int trk_block_end(const Src *src, int i)
{
    int end = block_end(src, i, TRK_BLOCKS);
    for (int k = i; k < end; k++) {
        if (src->tok[k].first && is_digit(src->tok[k].s[0])) {
            return k;
        }
    }
    return end;
}

/* A cell's note: past 127 ticks in pieces, the first struck and the rest
 * held on at its pitch (silent once it is let go); each with the numbers
 * that differ from the note before's (length, gate, volume: the driver
 * keeps the last), all three at a phrase's start (prev[0] < 0). */
static bool trk_note_bytes(const TrkCell *x, long length, int prev[3],
                           Buf *out)
{
    long gate = x->gate == GATE_DEFAULT ? (length - 2 > 1 ? length - 2 : 1)
              : x->gate == GATE_HOLD ? length : x->gate;
    for (long at = 0; at < length;) {
        long n = length - at < NOTE_MAX ? length - at : NOTE_MAX;
        long g = gate >= at + n ? n : gate - at > 0 ? gate - at : 0;
        int params[3] = { (int)n, (int)g, x->volume };
        int write = 3;
        if (prev[0] >= 0) {
            write = 0;
            for (int k = 0; k < 3; k++) {
                if (params[k] != prev[k]) {
                    write = k + 1;
                }
            }
        }
        if (!put(out, (u8)(NOTE_FIRST + x->note))) {
            return false;
        }
        for (int k = 0; k < write; k++) {
            if (!put(out, (u8)params[k])) {
                return false;
            }
        }
        memcpy(prev, params, sizeof params);
        at += n;
    }
    return true;
}

/* Every channel's order list and the phrases into p (build_channels): a
 * section's cells a phrase (one starting again at a `loop` cell, whose
 * commands go in the order list round its loop point), the same bytes the
 * same phrase; past 224 phrases, two sections a phrase, then four.... */
static bool trk_channels(Trk *t, SongParts *p, int *count)
{
    const Src *src = t->src;
    const u8 *cmds = t->cmds.b;
    int *mine = malloc(sizeof *mine * (size_t)(t->ncell + 1));
    int *first = malloc(sizeof *first * (TRACKS + 1));
    bool ok = mine != NULL && first != NULL;
    if (!ok) {
        fprintf(stderr, "songtext: out of memory\n");
    }
    /* Each channel's cells, in order: the sections', copies among them. */
    int n = 0;
    for (int c = 0; ok && c < TRACKS; c++) {
        first[c] = n;
        for (int s = 0; s < t->nsec; s++) {
            for (int k = 0; k < t->ncell; k++) {
                if (t->cell[k].channel == c && t->cell[k].section == s) {
                    mine[n++] = k;
                }
            }
        }
    }
    first[TRACKS] = n;
    for (int c = 0; ok && c < TRACKS; c++) {
        const int *m = &mine[first[c]];
        int count_c = first[c + 1] - first[c];
        const char *name = CHANNELS[c];
        int loops[2] = { -1, -1 };
        int nloops = 0;
        for (int k = 0; k < count_c; k++) {
            if (t->cell[m[k]].loop >= 0 && nloops < 2) {
                loops[nloops++] = m[k];
            }
        }
        int end = -1;
        for (int k = 0; k < count_c && end < 0; k++) {
            if (t->cell[m[k]].end) {
                end = k;
            }
        }
        if (nloops > 0 && t->line_given[c] && t->line_loop[c] >= 0) {
            ok = err(src, t->cell[loops[0]].line, "loop: channel %s has one "
                     "in its channel line", name);
        } else if (nloops > 1) {
            ok = err(src, t->cell[loops[1]].line, "loop: channel %s has one "
                     "already", name);
        } else if (count_c > 0 && end < 0) {
            ok = err(src, t->cell[m[count_c - 1]].line, "channel %s has no "
                     "end cell", name);
        } else if (end >= 0 && end != count_c - 1) {
            ok = err(src, t->cell[m[end + 1]].line, "a cell after channel "
                     "%s's end", name);
        } else if (count_c > 0 && end == 0) {
            ok = err(src, t->cell[m[0]].line, "channel %s ends before its "
                     "first note", name);
        } else if (count_c > 0 && t->cell[m[count_c - 1]].loop >= 0) {
            ok = err(src, t->cell[m[count_c - 1]].line, "loop in the end "
                     "cell: nothing after it to come back to");
        } else if (count_c > 0 && t->cell[m[0]].tick - 1 > 255) {
            ok = err(src, t->cell[m[0]].line, "channel %s starts %ld ticks "
                     "in: 255 at most", name, t->cell[m[0]].tick - 1);
        }
    }
    static const int EVERY[9] = { 1, 2, 4, 8, 16, 32, 64, 128, 1 << 20 };
    for (int e = 0; ok && e < 9; e++) {
        int every = EVERY[e];
        int nphrases = 0;
        for (int k = 0; k < PHRASES; k++) {
            buf_free(&p->phrase[k]);
            p->phrase_given[k] = false;
        }
        for (int c = 0; ok && c < TRACKS; c++) {
            Buf *order = &p->track[c];
            const int *m = &mine[first[c]];
            int count_c = first[c + 1] - first[c];
            buf_free(order);
            /* The delay: a wait first. With no loop point the channel
             * starts again from there, as the song text's does. */
            long delay = count_c ? t->cell[m[0]].tick - 1 : 0;
            if (delay) {
                ok = put(order, TRK_WAIT) && put(order, (u8)delay);
            }
            if (ok && t->line_given[c]) {
                int at = t->line_cmd[c];
                int len = t->line_ncmd[c];
                int loop = t->line_loop[c];
                ok = loop < 0 ? put_buf(order, cmds + at, len)
                     : put_buf(order, cmds + at, loop) && put(order, CMD_LOOP)
                       && put_buf(order, cmds + at + loop, len - loop);
            }
            for (int k = 0; ok && k < count_c - 1;) {
                /* A phrase: cells to the next group of sections, or a
                 * loop cell. */
                Buf phrase = { 0 };
                int group = t->cell[m[k]].section / every;
                int prev[3] = { -1, -1, -1 };
                int j = k;
                for (; ok && j < count_c - 1; j++) {
                    const TrkCell *x = &t->cell[m[j]];
                    if (j > k && (x->loop >= 0 || x->section / every != group)) {
                        break;
                    }
                    long length = t->cell[m[j + 1]].tick - x->tick;
                    if (x->loop >= 0) {
                        ok = put_buf(order, cmds + x->cmd, x->loop)
                             && put(order, CMD_LOOP)
                             && put_buf(order, cmds + x->cmd + x->loop,
                                        x->ncmd - x->loop);
                    } else {
                        ok = put_buf(&phrase, cmds + x->cmd, x->ncmd);
                    }
                    ok = ok && trk_note_bytes(x, length, prev, &phrase);
                }
                k = j;
                int num;
                for (num = 0; ok && num < nphrases; num++) {
                    if (p->phrase[num].n == phrase.n
                        && memcmp(p->phrase[num].b, phrase.b,
                                  (size_t)phrase.n) == 0) {
                        break;
                    }
                }
                if (ok && num == nphrases) {
                    if (nphrases < PHRASES) {
                        p->phrase[nphrases] = phrase;
                        p->phrase_given[nphrases] = true;
                        phrase = (Buf){ 0 };
                    }
                    nphrases++;
                }
                buf_free(&phrase);
                ok = ok && put(order, (u8)(num < PHRASES ? num : 0));
            }
            if (ok && count_c > 0) {
                const TrkCell *x = &t->cell[m[count_c - 1]];
                ok = put_buf(order, cmds + x->cmd, x->ncmd);
            }
        }
        if (ok && nphrases <= PHRASES) {
            *count = nphrases;
            break;
        }
        if (ok && e == 8) {
            ok = err(src, 0, "more than %d phrases", PHRASES);
        }
    }
    free(mine);
    free(first);
    return ok;
}

/* A tracker file into its bytes (song_layout). */
static bool tracker_bytes(const Src *src, Buf *out)
{
    SongParts *p = calloc(1, sizeof *p);
    Trk *t = calloc(1, sizeof *t);
    if (p == NULL || t == NULL) {
        free(p);
        free(t);
        fprintf(stderr, "songtext: out of memory\n");
        return false;
    }
    t->src = src;
    t->ncolumns = -1;
    t->nheads = -1;
    bool ok = true;
    int i = 0;
    while (ok && i < src->n) {
        const Tok *w = &src->tok[i];
        int stop = line_end(src, i);
        if (strcmp(w->s, "song") == 0) {
            ok = trk_song_line(t, i, stop);
        } else if (strcmp(w->s, "volumes") == 0) {
            if (stop - i != 10) {
                ok = err(src, w->line, "volumes takes nine (fm1-fm6, psg1, "
                         "psg2 and one nothing reads)");
            }
            for (int k = 0; ok && k < 9; k++) {
                long v;
                ok = number(src, &src->tok[i + 1 + k], 0, 255, &v);
                t->volumes[k] = (u8)v;
            }
            t->have_volumes = true;
        } else if (strcmp(w->s, "channel") == 0) {
            ok = trk_channel_line(t, i, stop);
        } else if (strcmp(w->s, "section") == 0) {
            ok = trk_section_line(t, i, stop);
        } else if (strcmp(w->s, "row") == 0 || is_digit(w->s[0])) {
            ok = trk_grid_line(t, i, stop);
        } else if (strcmp(w->s, "fm_instrument") == 0
                   || strcmp(w->s, "psg_instrument") == 0) {
            bool fm = w->s[0] == 'f';
            int next = trk_block_end(src, stop);
            long v;
            if (stop != i + 2 && stop != i + 3) {
                ok = err(src, w->line, "%s takes its number (and a name)",
                         w->s);
            } else if (number(src, &src->tok[i + 1], 0, INSTRUMENTS - 1, &v)) {
                bool *given = fm ? &p->in.fm_given[v] : &p->in.psg_given[v];
                if (*given) {
                    ok = err(src, w->line, "%s %ld given twice", w->s, v);
                } else {
                    *given = true;
                    ok = parse_instrument(src, w->line, fm, stop, next,
                                          fm ? p->in.fm[v] : p->in.psg[v]);
                }
            } else {
                ok = false;
            }
            stop = next;
        } else {
            ok = err(src, w->line, "'%s': not song, volumes, channel, "
                     "section, a row, fm_instrument or psg_instrument", w->s);
        }
        i = stop;
    }
    ok = ok && trk_close_section(t);
    if (ok && !t->have_song) {
        ok = err(src, 0, "no song line");
    }
    if (ok && !t->have_volumes) {
        ok = err(src, 0, "no volumes line");
    }
    if (ok) {
        int fm_offset, psg_offset;
        Buf scratch = { 0 };
        ok = put_instruments(src, &p->in, &scratch, &fm_offset, &psg_offset);
        buf_free(&scratch);
    }
    int count = 0;
    ok = ok && trk_channels(t, p, &count);
    ok = ok && song_layout(src, p, t->header, t->volumes, count, out);
    song_parts_free(p);
    free(p);
    buf_free(&t->cmds);
    free(t->cell);
    free(t->sec);
    free(t);
    return ok;
}

/* -- the effects ---------------------------------------------------------- */

typedef struct {
    const char *name;
    int line;
    int at;
    int channel;                /* a track's: the channel it plays on */
    NameUse use;                /* a phrase's: its instruments' names */
} PhraseRef;

/* The effects' file into its bytes, laid out as hzsongtext.py's
 * effects_bytes: the table (first byte, count, the FM and PSG
 * instruments' offsets, the size, an offset an effect), each effect's
 * tracks, the phrases in the file's order, the instruments. A track's
 * phrase is a big-endian offset from the table. */
static bool effects_bytes(const Src *src, Buf *out)
{
    Instruments *in = calloc(1, sizeof *in);
    PhraseRef *refs = calloc((size_t)src->n + 1, sizeof *refs);
    PhraseRef *defs = calloc((size_t)src->n + 1, sizeof *defs);
    Buf heads = { 0 };          /* the effects' headers */
    Buf phrases = { 0 };
    int nrefs = 0, ndefs = 0, count = 0;
    int starts[256];
    long first = -1;
    bool ok = in != NULL && refs != NULL && defs != NULL;
    int i = 0;

    if (!ok) {
        fprintf(stderr, "songtext: out of memory\n");
    }
    while (ok && i < src->n) {
        const Tok *t = &src->tok[i];
        int stop = line_end(src, i);
        long v;
        if (strcmp(t->s, "effects") == 0) {
            if (stop != i + 3 || strcmp(src->tok[i + 1].s, "first_byte") != 0) {
                ok = err(src, t->line, "effects takes first_byte N");
                break;
            }
            ok = number(src, &src->tok[i + 2], 0, 255, &first);
            i = stop;
        } else if (strcmp(t->s, "effect") == 0) {
            if (stop != i + 3) {
                ok = err(src, t->line, "effect takes its number and name");
                break;
            }
            if (!number(src, &src->tok[i + 1], 0, 255, &v)) {
                ok = false;
                break;
            }
            if (v != count || count == 255) {
                ok = err(src, t->line, "effect %ld: the next is %d (they run "
                         "from 0 in order)", v, count);
                break;
            }
            starts[count++] = heads.n;
            int next = block_end(src, stop, EFFECT_BLOCKS);
            int ntracks = 0;
            int count_at = heads.n;
            ok = put(&heads, 0);
            for (int j = stop; ok && j < next; j = line_end(src, j)) {
                const Tok *tt = &src->tok[j];
                int end = line_end(src, j);
                int c;
                if (strcmp(tt->s, "track") != 0 || end - j < 3) {
                    ok = err(src, tt->line, "an effect's line is track "
                             "CHANNEL [commands] PHRASE");
                    break;
                }
                for (c = 0; c < CHANNEL_COUNT
                            && strcmp(src->tok[j + 1].s, CHANNELS[c]) != 0;
                     c++) {
                }
                if (c == CHANNEL_COUNT) {
                    ok = err(src, tt->line, "an effect's line is track "
                             "CHANNEL [commands] PHRASE");
                    break;
                }
                ok = put(&heads, (u8)c);
                int k = j + 2;
                while (ok && k < end - 1) {
                    if (!is_command_word(src->tok[k].s)) {
                        ok = err(src, tt->line, "'%s': not a command",
                                 src->tok[k].s);
                        break;
                    }
                    int at = k;
                    ok = take_command(src, &k, end, &heads);
                    if (ok && k > end - 1) {
                        ok = err(src, tt->line, "the phrase's name missing");
                    }
                    ok = ok && check_kind(src, at, end, c);
                }
                if (!ok) {
                    break;
                }
                refs[nrefs++] = (PhraseRef){ .name = src->tok[end - 1].s,
                                             .line = tt->line, .at = heads.n,
                                             .channel = c };
                ok = put_zeros(&heads, 2);
                ntracks++;
            }
            if (ok && ntracks == 0) {
                ok = err(src, t->line, "effect %ld has no track", v);
            }
            if (ok) {
                heads.b[count_at] = (u8)ntracks;
            }
            i = next;
        } else if (strcmp(t->s, "phrase") == 0) {
            if (stop != i + 2) {
                ok = err(src, t->line, "phrase takes its name");
                break;
            }
            for (int k = 0; k < ndefs; k++) {
                if (strcmp(defs[k].name, src->tok[i + 1].s) == 0) {
                    ok = err(src, t->line, "phrase %s given twice",
                             src->tok[i + 1].s);
                }
            }
            if (!ok) {
                break;
            }
            PhraseRef *def = &defs[ndefs++];
            *def = (PhraseRef){ .name = src->tok[i + 1].s, .line = t->line,
                                .at = phrases.n, .channel = -1 };
            int next = block_end(src, stop, EFFECT_BLOCKS);
            ok = parse_phrase(src, stop, next, &phrases, &def->use)
                 && put(&phrases, CMD_END);
            i = next;
        } else if (strcmp(t->s, "fm_instrument") == 0
                   || strcmp(t->s, "psg_instrument") == 0) {
            ok = take_instrument(src, i, in, EFFECT_BLOCKS, &i);
        } else {
            ok = err(src, t->line, "'%s': not effects, effect, phrase, "
                     "fm_instrument or psg_instrument", t->s);
        }
    }
    if (ok && first < 0) {
        ok = err(src, 0, "no effects line");
    }
    /* Each track's phrase defined, and its instruments' names in its
     * channel's list. */
    for (int k = 0; ok && k < nrefs; k++) {
        int d;
        for (d = 0; d < ndefs && strcmp(defs[d].name, refs[k].name) != 0;
             d++) {
        }
        if (d == ndefs) {
            ok = err(src, refs[k].line, "phrase %s is not defined",
                     refs[k].name);
            break;
        }
        ok = check_use(src, &defs[d].use, refs[k].channel, refs[k].name, 0);
    }

    /* The layout. */
    int heads_at = 8 + 2 * count;
    int phrases_at = heads_at + heads.n;
    if (ok) {
        ok = put_zeros(out, 8);
    }
    if (ok) {
        out->b[0] = (u8)first;
        out->b[1] = (u8)count;
        ok = put_zeros(out, 2 * count) && put_buf(out, heads.b, heads.n)
             && put_buf(out, phrases.b, phrases.n);
    }
    for (int k = 0; ok && k < count; k++) {
        set16(out, 8 + 2 * k, heads_at + starts[k]);
    }
    for (int k = 0; ok && k < nrefs; k++) {
        int d;
        for (d = 0; d < ndefs && strcmp(defs[d].name, refs[k].name) != 0;
             d++) {
        }
        int at = phrases_at + defs[d].at;
        if (at >= NOTE_END << 8) {
            ok = err(src, refs[k].line, "phrase %s past $%04X", refs[k].name,
                     NOTE_END << 8);
            break;
        }
        out->b[heads_at + refs[k].at] = (u8)(at >> 8);
        out->b[heads_at + refs[k].at + 1] = (u8)at;
    }
    int fm_offset = 0, psg_offset = 0;
    if (ok) {
        ok = put_instruments(src, in, out, &fm_offset, &psg_offset);
    }
    if (ok) {
        set16(out, 2, fm_offset);
        set16(out, 4, psg_offset);
        set16(out, 6, out->n - 1);
    }
    buf_free(&heads);
    buf_free(&phrases);
    free(refs);
    free(defs);
    free(in);
    return ok;
}

/* -- the image ------------------------------------------------------------ */

static bool g_trackers = true;

void songtext_use_trackers(bool on)
{
    g_trackers = on;
}

/* Each song's FM instruments' names, as the last songtext_build read them
 * (songtext_fm_instrument). */
typedef struct {
    int  song, number;
    char name[32];
} SongName;

static SongName *g_names;
static int g_nnames, g_capnames;

static bool keep_names(const Src *src, int song)
{
    for (int k = 0; k < src->nnames; k++) {
        if (!src->names[k].fm) {
            continue;
        }
        if (g_nnames == g_capnames) {
            int cap = g_capnames ? g_capnames * 2 : 256;
            SongName *p = realloc(g_names, sizeof *p * (size_t)cap);
            if (p == NULL) {
                fprintf(stderr, "songtext: out of memory\n");
                return false;
            }
            g_names = p;
            g_capnames = cap;
        }
        SongName *n = &g_names[g_nnames++];
        n->song = song;
        n->number = src->names[k].number;
        snprintf(n->name, sizeof n->name, "%s", src->names[k].name);
    }
    return true;
}

int songtext_fm_instrument(int song, const char *name)
{
    for (int k = 0; k < g_nnames; k++) {
        if (g_names[k].song == song && strcmp(g_names[k].name, name) == 0) {
            return g_names[k].number;
        }
    }
    return -1;
}

static bool is_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

/* A song file into its bytes, or its tracker beside it (NN_title.trk for
 * NN_title.txt) in its place: a mod's tracker, a mod's text, the game's
 * tracker, the game's text, the first there is (so a text mod isn't
 * hidden behind the game's tracker; docs/mods.md). *tracker: whether it
 * was the tracker. Its FM instruments' names are kept as song n_song's. */
static bool song_file_bytes(const char *dir, const char *name, int n_song,
                            Buf *out, bool *tracker)
{
    size_t d = strlen(dir);
    size_t n = strlen(name);
    char trk[PATH_LEN];
    *tracker = false;
    if (g_trackers && n > 4 && strcmp(name + n - 4, ".txt") == 0
        && d + 1 + n + 1 <= PATH_LEN) {
        char own[PATH_LEN], txt[PATH_LEN], buf[PATH_LEN];
        memcpy(trk, name, n - 4);
        memcpy(trk + n - 4, ".trk", 5);
        memcpy(txt, dir, d);
        txt[d] = '/';
        memcpy(txt + d + 1, name, n + 1);
        memcpy(own, txt, d + 1 + n + 1);
        memcpy(own + d + 1 + n - 4, ".trk", 5);
        *tracker = hz_mods_resolve(own, buf, sizeof buf) != own
                   || (hz_mods_resolve(txt, buf, sizeof buf) == txt
                       && is_file(own));
    }
    Src src;
    bool ok = src_open(&src, dir, *tracker ? trk : name, *tracker)
              && scan_names(&src)
              && (*tracker ? tracker_bytes(&src, out)
                           : song_bytes(&src, out))
              && keep_names(&src, n_song);
    src_free(&src);
    return ok;
}

bool songtext_build(const char *dir, u8 image[SONGTEXT_SIZE],
                    u16 *effects_at, u8 **song_windows, int *song_count)
{
    *song_windows = NULL;
    *song_count = 0;
    g_nnames = 0;
    Src top;
    if (!src_open(&top, dir, "sounds.txt", false)) {
        src_free(&top);
        return false;
    }
    long entries = -1, effects = -1;
    const char *files[256];
    const char *effects_file = NULL;
    int nsongs = 0;
    int tail_from = -1;
    bool ok = true;
    long v;

    for (int i = 0; ok && i < top.n; i = line_end(&top, i)) {
        const Tok *t = &top.tok[i];
        int stop = line_end(&top, i);
        if (strcmp(t->s, "song_table") == 0) {
            ok = stop == i + 2
                 ? number(&top, &top.tok[i + 1], 1, 0x3FFF, &entries)
                 : err(&top, t->line, "song_table takes its entries");
        } else if (strcmp(t->s, "song") == 0) {
            if (stop != i + 3) {
                ok = err(&top, t->line, "song takes its number and file");
            } else if (number(&top, &top.tok[i + 1], 0, 255, &v)) {
                if (v != nsongs) {
                    ok = err(&top, t->line, "song %ld: the next is %d (they "
                             "run from 0 in order)", v, nsongs);
                } else {
                    files[nsongs++] = top.tok[i + 2].s;
                }
            } else {
                ok = false;
            }
        } else if (strcmp(t->s, "effects") == 0) {
            if (stop != i + 3) {
                ok = err(&top, t->line, "effects takes the table's address "
                         "and its file");
            } else {
                ok = number(&top, &top.tok[i + 1], BASE, 0xFFFF, &effects);
                effects_file = top.tok[i + 2].s;
            }
        } else if (strcmp(t->s, "tail") == 0) {
            if (stop != i + 1) {
                ok = err(&top, t->line, "tail's bytes go on the lines after "
                         "it");
            }
            tail_from = stop;
            break;
        } else {
            ok = err(&top, t->line, "'%s': not song_table, song, effects or "
                     "tail", t->s);
        }
    }
    if (ok && entries < 0) {
        ok = err(&top, 0, "no song_table line");
    }
    if (ok && effects < 0) {
        ok = err(&top, 0, "no effects line");
    }
    if (ok && nsongs + 1 > entries) {
        ok = err(&top, 0, "%d songs and the end need %d entries, song_table "
                 "has %ld", nsongs, nsongs + 1, entries);
    }

    /* Every song's bytes, in order. */
    Buf songs[256];
    bool tracked[256] = { false };
    memset(songs, 0, sizeof songs);
    for (int n = 0; ok && n < nsongs; n++) {
        ok = song_file_bytes(dir, files[n], n, &songs[n], &tracked[n]);
    }

    /* The one window, as the ROM's: the songs in order after the table,
     * each from the last byte of the one before when that is a 9, and the
     * table's entry after them where a next one would go. Songs that end
     * past the effects there get a window each instead: the song right
     * after the table (and the songs after it, below), its entry the only
     * one; the one window keeps the effects and a table of zeros (no
     * song). */
    long first = BASE + 2 * entries;
    long at = first;
    long end = first;
    u16 table[0x4000];
    memset(table, 0, sizeof table);
    if (ok && effects < first) {
        ok = err(&top, 0, "the effects at $%04lX are in the song table (it "
                 "ends at $%04lX)", effects, first);
    }
    for (int n = 0; ok && n < nsongs; n++) {
        table[n] = (u16)(at - BASE);
        end = at + songs[n].n;
        at = songs[n].b[songs[n].n - 1] == SONG_FIRST ? end - 1 : end;
    }
    bool shared = end <= effects;
    memset(image, 0xFF, SONGTEXT_SIZE);
    if (ok && shared) {
        table[nsongs] = (u16)(end - 1 - BASE);
        for (int n = 0; n < nsongs; n++) {
            memcpy(&image[table[n]], songs[n].b, (size_t)songs[n].n);
        }
        for (long k = 0; k < entries; k++) {
            image[2 * k] = (u8)(table[k] >> 8);
            image[2 * k + 1] = (u8)table[k];
        }
    } else if (ok) {
        memset(image, 0, (size_t)(2 * entries));
        for (int n = 0; ok && n < nsongs; n++) {
            if (first + songs[n].n > effects) {
                int len = (int)strlen(files[n]) - (tracked[n] ? 4 : 0);
                ok = err(&top, 0, "song %d (%.*s%s) ends at $%04lX, past the "
                         "effects at $%04lX (%ld bytes over)", n, len,
                         files[n], tracked[n] ? ".trk" : "",
                         first + songs[n].n, effects,
                         first + songs[n].n - effects);
            }
        }
    }
    if (ok) {
        Src src;
        Buf b = { 0 };
        ok = src_open(&src, dir, effects_file, false) && scan_names(&src)
             && effects_bytes(&src, &b);
        long tail = 0;
        for (int i = tail_from; ok && i >= 0 && i < top.n; i++) {
            const char *s = top.tok[i].s;
            if (strlen(s) != 2 || hex_digit(s[0]) < 0 || hex_digit(s[1]) < 0) {
                ok = err(&top, top.tok[i].line, "'%s': the tail is hex bytes",
                         s);
            }
            tail++;
        }
        if (ok && effects + b.n + tail > 0x10000) {
            ok = err(&top, 0, "the effects (and the tail) run %ld bytes past "
                     "$FFFF", effects + b.n + tail - 0x10000);
        }
        if (ok) {
            memcpy(&image[effects - BASE], b.b, (size_t)b.n);
            long t = effects - BASE + b.n;
            for (int i = tail_from; i >= 0 && i < top.n; i++, t++) {
                const char *s = top.tok[i].s;
                image[t] = (u8)(hex_digit(s[0]) << 4 | hex_digit(s[1]));
            }
            *effects_at = (u16)effects;
        }
        buf_free(&b);
        src_free(&src);
    }
    if (ok && !shared) {
        u8 *w = malloc((size_t)nsongs * SONGTEXT_SIZE);
        if (w == NULL) {
            fprintf(stderr, "songtext: out of memory\n");
            ok = false;
        }
        for (int n = 0; ok && n < nsongs; n++) {
            u8 *p = w + (size_t)n * SONGTEXT_SIZE;
            memcpy(p, image, SONGTEXT_SIZE);
            p[2 * n] = (u8)((first - BASE) >> 8);
            p[2 * n + 1] = (u8)(first - BASE);
            /* The songs from n on, chained as in the one window, up to the
             * effects: what a song reads past its end is what the one
             * window has there (the title's PSG tracks, with no PSG
             * instrument of its own, take song 1's first bytes). */
            long q = first;
            for (int m = n; m < nsongs && q < effects; m++) {
                long len = songs[m].n;
                if (q + len > effects) {
                    len = effects - q;
                }
                memcpy(&p[q - BASE], songs[m].b, (size_t)len);
                q += songs[m].n;
                if (songs[m].b[songs[m].n - 1] == SONG_FIRST) {
                    q--;
                }
            }
        }
        if (ok) {
            *song_windows = w;
            *song_count = nsongs;
        }
    }
    for (int n = 0; n < nsongs; n++) {
        buf_free(&songs[n]);
    }
    src_free(&top);
    return ok;
}
