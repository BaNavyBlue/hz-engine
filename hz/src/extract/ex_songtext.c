/* (Session 78) tools/hzsongtext.py's extract side in C (ex_sound.h):
 * read_image (SoundData into the model), write_text's files and
 * annotate_text's `# heard:` comments. The Python writes the text, then
 * reads it back to walk its notes; here the notes are walked in the model
 * first (heard_song, heard_effects) and each line gets its comment as it
 * is written, which comes to the same text. */
#include "ex_sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const EX_SND_CHANNELS[9] = {
    "fm1", "fm2", "fm3", "fm4", "fm5", "fm6", "psg1", "psg2", "noise"
};

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* ARG_FLAGS, $E0-$ED: a byte after them. Kinds: u unsigned, s signed,
 * a "+N" (marker's add), p pan. */
static const struct {
    const char *word;
    char        kind;
} ARG_FLAGS[14] = {
    { "instrument", 'u' }, { "pan", 'p' }, { "noise_mode", 'u' },
    { "slide_speed", 'u' }, { "lfo_delay", 'u' }, { "wait", 'u' },
    { "marker", 'u' }, { "marker", 'a' }, { "transpose", 's' },
    { "volume", 's' }, { "tempo", 's' }, { "detune", 's' },
    { "repeat", 'u' }, { "fade_rate", 'u' },
};

/* WORD_FLAGS, $F0-$F7: none after them. */
static const char *const WORD_FLAGS[8][2] = {
    { "lfo", "off" }, { "lfo", "on" }, { "psg_volume", "tone" },
    { "psg_volume", "noise" }, { "slide", "all" }, { "slide", "tied" },
    { "release_bend", "up" }, { "release_bend", "down" },
};

/* An instrument's fields: FM_SCALARS, FM_ROWS, PSG_SCALARS. */
typedef struct {
    const char *name;
    int         off, shift, top;
} Field;

static const Field FM_SCALARS[] = {
    { "algorithm", 0, 0, 7 }, { "feedback", 0, 3, 7 }, { "fms", 1, 4, 7 },
    { "ams", 1, 0, 3 }, { "lfo_freq", 31, 0, 7 }, { "lfo_sweep", 35, 0, 255 },
};
static const Field FM_ROWS[] = {
    { "multiple", 2, 0, 15 }, { "detune", 2, 4, 7 }, { "level", 6, 0, 127 },
    { "key_scale", 10, 6, 3 }, { "attack", 10, 0, 31 }, { "am", 14, 7, 1 },
    { "decay1", 14, 0, 31 }, { "decay2", 18, 0, 31 },
    { "sustain", 22, 4, 15 }, { "release", 22, 0, 15 },
};
static const Field PSG_SCALARS[] = {
    { "speed", 0, 0, 255 }, { "start_drop", 1, 0, 255 },
    { "attack", 2, 0, 255 }, { "decay", 3, 0, 255 },
    { "sustain_drop", 4, 0, 255 }, { "sustain", 5, 0, 255 },
    { "release", 6, 0, 255 }, { "fms", 7, 4, 7 }, { "ams", 7, 0, 3 },
    { "lfo_freq", 8, 0, 7 }, { "lfo_sweep", 12, 0, 255 },
};

#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

/* USES: what plays each song. */
static const char *const USES[18] = {
    "the title", "the options", "the map select", "ABGRUND's map",
    "VULKAN's map", "LOCH's map", "STRAND's map", "STADT's map",
    "EISFREI's map", "WALDUNG's map", "OASE's map", "the winner screen",
    "the winner screen", "the winner screen", "the winner screen",
    "an ending", "an ending", "the staff roll",
};

static const char *const EFFECT_NAMES[] = {
    "fire_robot", "fire_jet", "fire_unit", "fire_missile", "mech_destroyed",
    "unit_destroyed", "infantry_destroyed", "hit_unit", "hit_mech",
    "hit_tile", "take_off", "land", "lift", "drop", "jets_meet", "menu_open",
    "menu_move", "ordered", "unit_ready", "hq_alert", "missile_alert",
    "hold_raised", "post_taken", "hq_hit", "hq_destroyed", "unused_25",
    "refused", "debris_end", "sizzle",
};

/* INSTRUMENT_NAMES (session 50, docs/instrument_names.md): each song's FM
 * instruments, then its PSG ones; the effects' last. */
static const char *const INSTRUMENT_NAMES[19][2] = {
    { "low chords high open_hat snare", "" },
    { "high bass snare kick", "psg_mid" },
    { "mid low bass high", "psg_high" },
    { "mid low high closed_hat chords chords_2 high_2 snare kick_toms",
      "psg_chords" },
    { "mid low low_2 bass chords low_3 mid_2 kick_toms",
      "psg_high psg_mid psg_high_2" },
    { "chords chords_2 chords_3 bass chords_4 kick_toms", "" },
    { "chords chords_2 low closed_hat high mid snare kick_toms",
      "psg_chords" },
    { "chords mid chords_2 kick_toms snare low", "" },
    { "mid chords closed_hat open_hat chords_2 bass chords_3 snare "
      "open_hat_2 kick_toms", "psg_chords" },
    { "chords high chords_2 bass closed_hat high_2 kick_toms", "" },
    { "low low_2 low_3 chords high low_4 mid low_5 kick_toms",
      "psg_chords psg_chords_2" },
    { "bass closed_hat bass_2 chords low kick_toms", "psg_chords" },
    { "chords high mid", "psg_high" },
    { "high low closed_hat mid high_2 kick_toms", "psg_chords" },
    { "open_hat chords bass kick_toms", "psg_chords" },
    { "bass bass_2 mid mid_2 snare kick_snare", "psg_chords" },
    { "bass bass_2 bass_3 mid high snare kick_snare", "psg_chords" },
    { "chords closed_hat bass mid chords_2 bass_2 snare kick_snare",
      "psg_chords psg_chords_2" },
    { "hit_unit ordered fire_missile fire_jet mech_destroyed fire_robot "
      "hold_raised hit_tile land infantry_destroyed unused_25 "
      "missile_alert",
      "psg_fire_unit psg_mech_destroyed psg_fire_unit_2 psg_hq_hit "
      "psg_drop" },
};
#define NAMES_EFFECTS 18

static const char SOUNDS_HEAD[] =
    "# Herzog Zwei's songs and sound effects: the 32 KB the Z80 sound "
    "driver's\n"
    "# bank window shows at $8000-$FFFF (SoundData, ROM $78000), as text. "
    "The\n"
    "# game builds the bytes from these files at start-up; docs/sound.md "
    "has the\n"
    "# format, tools/hzsongtext.py writes and checks them.\n"
    "#\n"
    "# `song_table N`: the table at $8000, N two-byte entries (each song's\n"
    "# address, then where the next song would go, then zeros). The songs "
    "follow\n"
    "# it in this order, each one on the last byte of the one before when "
    "that\n"
    "# byte is the 9 a song begins with (the ROM's all are). `effects "
    "ADDRESS\n"
    "# FILE`: the effects' table, after a $FF fill. `tail`: bytes after the\n"
    "# effects that nothing reads. Songs that grow past the effects each "
    "get a\n"
    "# window of their own instead (docs/sound.md, Room).\n";

static const char EFFECTS_HEAD[] =
    "# The sound effects (the sfx table: Snd_SetSfxTable gives the driver "
    "its\n"
    "# address, sounds.txt's `effects`). docs/sound.md, \"The song text\", "
    "has the\n"
    "# format. An effect's number is the game's (SFX_* in "
    "src/game/game.h); each\n"
    "# track names its channel, the commands read before its phrase, and "
    "the\n"
    "# phrase. `first_byte`: the table's first byte, which nothing reads.\n";

/* ---- shared ---- */

/* USES.get(n) */
const char *ex_snd_use(int n)
{
    return (n >= 0 && n < COUNT(USES)) ? USES[n] : NULL;
}

/* ex_sound.h */
void *ex_snd_grow(void *v, int *cap, int n, size_t size)
{
    if (n <= *cap) {
        return v;
    }
    int c = (*cap < 16) ? 16 : *cap;
    while (c < n) {
        c *= 2;
    }
    void *p = realloc(v, (size_t)c * size);
    if (p == NULL) {
        abort();                    /* a few MB at most */
    }
    *cap = c;
    return p;
}

/* items.append: a new item, zeroed, at the end. */
static SndItem *push_item(SndItems *l)
{
    l->v = ex_snd_grow(l->v, &l->cap, l->n + 1, sizeof *l->v);
    SndItem *it = &l->v[l->n++];
    memset(it, 0, sizeof *it);
    return it;
}

/* A list and its heard notes. */
static void items_free(SndItems *l)
{
    for (int i = 0; i < l->n; i++) {
        free(l->v[i].heard);
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

/* in ARG_FLAGS */
static bool is_arg_flag(int b)
{
    return b >= 0xE0 && b <= 0xED;
}

/* in WORD_FLAGS */
static bool is_word_flag(int b)
{
    return b >= 0xF0 && b <= 0xF7;
}

/* names.split()[k] (ex_sound.h) */
const char *ex_snd_name(const char *names, int k, char out[32])
{
    if (names == NULL) {
        return NULL;
    }
    const char *p = names;
    for (int i = 0;; i++) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            return NULL;
        }
        const char *e = p;
        while (*e != '\0' && *e != ' ') {
            e++;
        }
        if (i == k) {
            int n = (int)(e - p) < 31 ? (int)(e - p) : 31;
            memcpy(out, p, (size_t)n);
            out[n] = '\0';
            return out;
        }
        p = e;
    }
}

/* len(names.split()) */
static int word_count(const char *names)
{
    char w[32];
    int n = 0;
    while (ex_snd_name(names, n, w) != NULL) {
        n++;
    }
    return n;
}

/* table_names: INSTRUMENT_NAMES' lists for a song or the effects, if they
 * name as many instruments as it has (else none). */
static void table_names(int key, int nfm, int npsg, const char **fm,
                        const char **psg)
{
    *fm = *psg = NULL;
    if (key < 0 || key > NAMES_EFFECTS) {
        return;
    }
    const char *f = INSTRUMENT_NAMES[key][0];
    const char *s = INSTRUMENT_NAMES[key][1];
    if (word_count(f) == nfm && word_count(s) == npsg) {
        *fm = f;
        *psg = s;
    }
}

/* note_add */
int ex_snd_note_add(int note, int more)
{
    int s = note + more;
    int low = s & 0xFF;
    if (s > 0xFF) {
        return (low + SND_NOTES) & 0xFF;
    }
    return (low < SND_NOTES) ? low : low - SND_NOTES;
}

/* note_name: C4, C#4 */
void ex_snd_note_name(ExBuf *b, int v)
{
    exb_printf(b, "%s%d", NOTE_NAMES[v % 12], v / 12);
}

/* signed_text */
static void signed_text(ExBuf *b, int v)
{
    if (v == 0) {
        exb_puts(b, "0");
    } else if (v < 0x80) {
        exb_printf(b, "+%d", v);
    } else {
        exb_printf(b, "-%d", 0x100 - v);
    }
}

/* flag_text */
void ex_snd_flag_text(ExBuf *b, u8 byte, u8 arg, const char *names)
{
    if (is_word_flag(byte)) {
        exb_printf(b, "%s %s", WORD_FLAGS[byte - 0xF0][0],
                   WORD_FLAGS[byte - 0xF0][1]);
        return;
    }
    char name[32];
    if (byte == 0xE0 && ex_snd_name(names, arg, name) != NULL) {
        exb_printf(b, "instrument %s", name);
        return;
    }
    const char *word = ARG_FLAGS[byte - 0xE0].word;
    switch (ARG_FLAGS[byte - 0xE0].kind) {
        case 's':
            exb_printf(b, "%s ", word);
            signed_text(b, arg);
            break;
        case 'a':
            exb_printf(b, "%s +%d", word, arg);
            break;
        case 'p':
            if (arg == 0x00 || arg == 0x01 || arg == 0xFF) {
                exb_printf(b, "pan %s", arg == 0x00 ? "center"
                                        : arg == 0x01 ? "left" : "right");
                break;
            }
            exb_printf(b, "%s %d", word, arg);
            break;
        default:
            exb_printf(b, "%s %d", word, arg);
            break;
    }
}

/* ---- reading the bytes ---- */

typedef struct {
    HzEx       *x;
    const char *what;           /* the failure's start: "SoundData" */
    const u8   *d;
    bool        bad;
} Rd;

/* raise ValueError: the first failure, the reading stopped. */
static bool rd_fail(Rd *r, const char *fmt, ...)
{
    if (!r->bad) {
        char s[400];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s, sizeof s, fmt, ap);
        va_end(ap);
        ex_fail(r->x, "%s: %s", r->what, s);
    }
    r->bad = true;
    return false;
}

/* Image.b: the byte at Z80 address a. */
static int rb(Rd *r, u32 a)
{
    if (a < SND_BASE || a - SND_BASE >= SND_SIZE) {
        rd_fail(r, "$%X is past the window", (unsigned)a);
        return 0;
    }
    return r->d[a - SND_BASE];
}

/* Image.le */
static u32 rle(Rd *r, u32 a)
{
    return (u32)(rb(r, a) | rb(r, a + 1) << 8);
}

/* Image.be */
static u32 rbe(Rd *r, u32 a)
{
    return (u32)(rb(r, a) << 8 | rb(r, a + 1));
}

/* read_command: one flag or skipped byte at a; the address after it. */
static u32 read_command(Rd *r, u32 a, SndItems *items)
{
    int x = rb(r, a);
    SndItem *it = push_item(items);
    it->b = (u8)x;
    if (is_arg_flag(x)) {
        it->kind = SND_FLAG;
        it->arg = (u8)rb(r, a + 1);
        return a + 2;
    }
    if (is_word_flag(x)) {
        it->kind = SND_FLAG;
        return a + 1;
    }
    it->kind = SND_BYTE;
    if (x == 0xEE || x == 0xEF) {
        it = push_item(items);
        it->kind = SND_BYTE;
        it->b = (u8)rb(r, a + 1);
        return a + 2;
    }
    return a + 1;
}

/* read_order: an order list to its $FF. */
static u32 read_order(Rd *r, u32 a, SndItems *items)
{
    while (!r->bad) {
        int x = rb(r, a);
        if (x < SND_NOTE_END) {
            SndItem *it = push_item(items);
            it->kind = SND_PHRASE;
            it->b = (u8)x;
            a++;
        } else if (x == SND_END) {
            return a + 1;
        } else if (x == SND_LOOP) {
            SndItem *it = push_item(items);
            it->kind = SND_BYTE;
            it->b = (u8)x;
            a++;
        } else {
            a = read_command(r, a, items);
        }
    }
    return a;
}

/* read_phrase: a phrase to its $FF, a note taking up to three bytes under
 * $80 after it. */
static u32 read_phrase(Rd *r, u32 a, SndItems *items)
{
    while (!r->bad) {
        int x = rb(r, a);
        if (x < SND_NOTE_FIRST) {
            rd_fail(r, "byte $%02X at $%04X where a note or a command goes",
                    x, (unsigned)a);
            break;
        }
        if (x < SND_NOTE_END) {
            a++;
            SndItem *it = push_item(items);
            it->kind = SND_NOTE;
            it->b = (u8)(x - SND_NOTE_FIRST);
            while (it->np < 3 && rb(r, a) < 0x80 && !r->bad) {
                it->p[it->np++] = (u8)rb(r, a);
                a++;
            }
        } else if (x == SND_END) {
            return a + 1;
        } else if (x == SND_LOOP) {
            SndItem *it = push_item(items);
            it->kind = SND_BYTE;
            it->b = (u8)x;
            a++;
        } else {
            a = read_command(r, a, items);
        }
    }
    return a;
}

/* read_song's expect */
static bool expect(Rd *r, u32 s, const char *what, u32 got, u32 want)
{
    if (got != want) {
        return rd_fail(r, "song at $%04X: %s at $%04X, not $%04X",
                       (unsigned)s, what, (unsigned)got, (unsigned)want);
    }
    return !r->bad;
}

/* read_song */
static bool read_song(Rd *r, u32 s, bool built, SndSong *song, u32 *size)
{
    memset(song, 0, sizeof *song);
    song->data = r->d;
    song->at = s;
    if (rb(r, s) != SND_SONG_FIRST) {
        return rd_fail(r, "song at $%04X: first byte $%02X", (unsigned)s,
                       rb(r, s));
    }
    song->volume = (u8)rb(r, s + 1);
    song->tempo = (u8)rb(r, s + 2);
    song->transpose = (u8)rb(r, s + 3);
    u32 vol = s + rle(r, s + 4), trk = s + rle(r, s + 6);
    u32 phr = s + rle(r, s + 8), fm = s + rle(r, s + 10);
    u32 psg = s + rle(r, s + 12);
    *size = rle(r, s + 14);
    if (!expect(r, s, "the channel volumes", vol, s + 16)) {
        return false;
    }
    for (int k = 0; k < 9; k++) {
        song->volumes[k] = (u8)rb(r, vol + (u32)k);
    }
    if (!expect(r, s, "the order lists' table", trk, vol + 9)) {
        return false;
    }
    u32 a = trk + 2 * SND_TRACKS;
    for (int t = 0; t < SND_TRACKS; t++) {
        char what[40];
        snprintf(what, sizeof what, "track %d's order list", t);
        if (!expect(r, s, what, s + rle(r, trk + 2 * (u32)t), a)) {
            return false;
        }
        a = read_order(r, a, &song->tracks[t]);
    }
    if (built && phr == a + 1 && ((a - s) & 0xFF) == 0xFF) {
        a++;                        /* song_bytes' 0 off the $FF */
    }
    if (!expect(r, s, "the phrase table", phr, a)) {
        return false;
    }
    u32 first = 0;
    bool have = false;
    int count = 0;
    while ((!have || phr + 2 * (u32)count < first) && !r->bad) {
        u32 v = rle(r, phr + 2 * (u32)count);
        if (v != 0 && (!have || s + v < first)) {
            first = s + v;
            have = true;
        }
        count++;
    }
    if (r->bad) {
        return false;
    }
    song->phrase_count = count;
    song->phrases = calloc((size_t)count, sizeof *song->phrases);
    song->has = calloc((size_t)count, sizeof *song->has);
    if (song->phrases == NULL || song->has == NULL) {
        abort();
    }
    a = phr + 2 * (u32)count;
    for (int k = 0; k < count; k++) {
        u32 v = rle(r, phr + 2 * (u32)k);
        if (v != 0) {
            char what[24];
            snprintf(what, sizeof what, "phrase %d", k);
            if (!expect(r, s, what, s + v, a)) {
                return false;
            }
            song->has[k] = true;
            a = read_phrase(r, a, &song->phrases[k]);
        }
    }
    if (!expect(r, s, "the FM instruments", fm, a)) {
        return false;
    }
    if (psg < fm || s + *size + 1 < psg || (psg - fm) % SND_FM_SIZE
        || (s + *size + 1 - psg) % SND_PSG_SIZE) {
        return rd_fail(r, "song at $%04X: instruments not whole",
                       (unsigned)s);
    }
    song->fm_at = fm;
    song->psg_at = psg;
    song->nfm = (int)((psg - fm) / SND_FM_SIZE);
    song->npsg = (int)((s + *size + 1 - psg) / SND_PSG_SIZE);
    rb(r, s + *size);               /* the records are in the window */
    return !r->bad;
}

/* read_song on a window of its own (ex_sound.h). */
bool ex_snd_read_song(HzEx *x, const char *what, const u8 *data, u32 s,
                      bool built, SndSong *song, u32 *size)
{
    Rd r = { x, what, data, false };
    return read_song(&r, s, built, song, size);
}

/* sorted(used): the effects' phrases by address. */
static int cmp_phrase_at(const void *a, const void *b)
{
    u32 p = ((const SndFxPhrase *)a)->at, q = ((const SndFxPhrase *)b)->at;
    return (p > q) - (p < q);
}

/* read_effects: the effects' table at e; *tail the address after it. */
static bool read_effects(Rd *r, u32 e, SndEffects *fx, u32 *tail)
{
    fx->data = r->d;
    fx->first_byte = (u8)rb(r, e);
    fx->count = rb(r, e + 1);
    u32 fm = e + rle(r, e + 2), psg = e + rle(r, e + 4);
    u32 size = rle(r, e + 6);
    u32 a = e + 8 + 2 * (u32)fx->count;
    fx->fx = calloc((size_t)fx->count + 1, sizeof *fx->fx);
    int cap = 0;
    if (fx->fx == NULL) {
        abort();
    }
    for (int k = 0; k < fx->count && !r->bad; k++) {
        u32 p = e + rle(r, e + 8 + 2 * (u32)k);
        if (p != a) {
            return rd_fail(r, "effect %d at $%04X, not $%04X", k,
                           (unsigned)p, (unsigned)a);
        }
        int n = rb(r, p);
        a = p + 1;
        SndFx *f = &fx->fx[k];
        if (k < COUNT(EFFECT_NAMES)) {
            snprintf(f->name, sizeof f->name, "%s", EFFECT_NAMES[k]);
        } else {
            snprintf(f->name, sizeof f->name, "effect_%d", k);
        }
        f->ntracks = n;
        f->tracks = calloc((size_t)n + 1, sizeof *f->tracks);
        if (f->tracks == NULL) {
            abort();
        }
        for (int t = 0; t < n && !r->bad; t++) {
            SndFxTrack *tr = &f->tracks[t];
            tr->channel = (u8)rb(r, a);
            a++;
            while (rb(r, a) >= SND_NOTE_END && !r->bad) {
                a = read_command(r, a, &tr->flags);
            }
            u32 at = e + rbe(r, a);
            a += 2;
            int u = 0;
            while (u < fx->nphrases && fx->phrases[u].at != at) {
                u++;
            }
            if (u == fx->nphrases) {
                if (tr->channel > 8) {
                    return rd_fail(r, "effect %d: channel %d", k,
                                   tr->channel);
                }
                fx->phrases = ex_snd_grow(fx->phrases, &cap, u + 1,
                                          sizeof *fx->phrases);
                SndFxPhrase *ph = &fx->phrases[fx->nphrases++];
                memset(ph, 0, sizeof *ph);
                ph->at = at;
                if (n == 1) {
                    snprintf(ph->name, sizeof ph->name, "%s", f->name);
                } else {
                    snprintf(ph->name, sizeof ph->name, "%s_%s", f->name,
                             EX_SND_CHANNELS[tr->channel]);
                }
            }
            tr->phrase = (int)at;   /* the address, until sorted */
        }
    }
    if (r->bad) {
        return false;
    }
    qsort(fx->phrases, (size_t)fx->nphrases, sizeof *fx->phrases,
          cmp_phrase_at);
    for (int k = 0; k < fx->count; k++) {
        for (int t = 0; t < fx->fx[k].ntracks; t++) {
            SndFxTrack *tr = &fx->fx[k].tracks[t];
            int u = 0;
            while (fx->phrases[u].at != (u32)tr->phrase) {
                u++;
            }
            tr->phrase = u;
        }
    }
    for (int u = 0; u < fx->nphrases && !r->bad; u++) {
        if (fx->phrases[u].at != a) {
            return rd_fail(r, "effect phrase at $%04X, not $%04X",
                           (unsigned)fx->phrases[u].at, (unsigned)a);
        }
        a = read_phrase(r, a, &fx->phrases[u].items);
    }
    if (r->bad) {
        return false;
    }
    if (fm != a) {
        return rd_fail(r, "effect FM instruments at $%04X, not $%04X",
                       (unsigned)fm, (unsigned)a);
    }
    if (psg < fm || e + size + 1 < psg || (psg - fm) % SND_FM_SIZE
        || (e + size + 1 - psg) % SND_PSG_SIZE) {
        return rd_fail(r, "effect instruments not whole");
    }
    fx->fm_at = fm;
    fx->psg_at = psg;
    fx->nfm = (int)((psg - fm) / SND_FM_SIZE);
    fx->npsg = (int)((e + size + 1 - psg) / SND_PSG_SIZE);
    rb(r, e + size);
    *tail = e + size + 1;
    return !r->bad;
}

/* read_image */
bool ex_snd_read(HzEx *x, const u8 *data, SndModel *m)
{
    memset(m, 0, sizeof *m);
    m->data = data;
    Rd r = { x, "SoundData", data, false };
    /* the songs' entries: each a song's start (its first byte a 9) */
    int nstarts = 0;
    u32 a = SND_BASE, w;
    for (;;) {
        w = rbe(&r, a);
        u32 p = w | SND_BASE;
        if (r.bad || w == 0 || data[p - SND_BASE] != SND_SONG_FIRST) {
            break;
        }
        nstarts++;
        a += 2;
    }
    if (r.bad) {
        return false;
    }
    if (nstarts == 0) {
        return rd_fail(&r, "no song in the song table");
    }
    u32 end_entry = w | SND_BASE;
    int entries = (int)(((rbe(&r, SND_BASE) | SND_BASE) - SND_BASE) / 2);
    for (int k = nstarts + 1; k < entries; k++) {
        if (rbe(&r, SND_BASE + 2 * (u32)k) != 0) {
            return rd_fail(&r, "song table entry %d not 0", k);
        }
    }
    m->song_table = entries;
    m->songs = calloc((size_t)nstarts, sizeof *m->songs);
    if (m->songs == NULL) {
        abort();
    }
    u32 at = SND_BASE + 2 * (u32)entries;
    for (int n = 0; n < nstarts; n++) {
        u32 s = rbe(&r, SND_BASE + 2 * (u32)n) | SND_BASE, size = 0;
        if (s != at) {
            return rd_fail(&r, "song %d at $%04X, not $%04X", n, (unsigned)s,
                           (unsigned)at);
        }
        m->nsongs = n + 1;
        SndSong *song = &m->songs[n];
        if (!read_song(&r, s, false, song, &size)) {
            return false;
        }
        table_names(n, song->nfm, song->npsg, &song->fm_names,
                    &song->psg_names);
        at = s + size;
    }
    if (end_entry != at) {
        return rd_fail(&r, "the song table's end entry $%04X, not $%04X",
                       (unsigned)end_entry, (unsigned)at);
    }
    u32 fill = at + 1;
    while (fill < 0x10000 && data[fill - SND_BASE] == 0xFF) {
        fill++;
    }
    m->effects_at = SND_EFFECTS_AT;
    if (fill != m->effects_at) {
        return rd_fail(&r, "$FF fill to $%04X, not up to the effects",
                       (unsigned)fill);
    }
    u32 tail = 0;
    if (!read_effects(&r, m->effects_at, &m->fx, &tail)) {
        return false;
    }
    table_names(NAMES_EFFECTS, m->fx.nfm, m->fx.npsg, &m->fx.fm_names,
                &m->fx.psg_names);
    m->tail_at = tail;
    m->ntail = (tail - SND_BASE < SND_SIZE) ? (int)(SND_SIZE
                                                    - (tail - SND_BASE)) : 0;
    return true;
}

/* A song's lists. */
void ex_snd_song_free(SndSong *song)
{
    for (int t = 0; t < SND_TRACKS; t++) {
        items_free(&song->tracks[t]);
    }
    for (int k = 0; song->phrases != NULL && k < song->phrase_count; k++) {
        items_free(&song->phrases[k]);
    }
    free(song->phrases);
    free(song->has);
    memset(song, 0, sizeof *song);
}

/* The model's lists. */
void ex_snd_free(SndModel *m)
{
    for (int n = 0; m->songs != NULL && n < m->nsongs; n++) {
        ex_snd_song_free(&m->songs[n]);
    }
    free(m->songs);
    SndEffects *fx = &m->fx;
    for (int k = 0; fx->fx != NULL && k < fx->count; k++) {
        for (int t = 0; fx->fx[k].tracks != NULL && t < fx->fx[k].ntracks;
             t++) {
            items_free(&fx->fx[k].tracks[t].flags);
        }
        free(fx->fx[k].tracks);
    }
    free(fx->fx);
    for (int u = 0; u < fx->nphrases; u++) {
        items_free(&fx->phrases[u].items);
    }
    free(fx->phrases);
    memset(m, 0, sizeof *m);
}

/* ---- the heard notes ---- */

typedef struct {
    int transpose, detune;
} Heard;

/* walk_flag: Flag_Transpose (0 clears; else added, kept under $60) and
 * Flag_Detune. */
static void walk_flag(const SndItem *it, Heard *st)
{
    if (it->b == 0xE8) {
        int t = 0;
        if (it->arg != 0) {
            t = (st->transpose + it->arg) & 0xFF;
            if (t & 0x80) {
                t = (t + SND_NOTES) & 0xFF;
            } else if (t >= SND_NOTES) {
                t -= SND_NOTES;
            }
        }
        st->transpose = t;
    } else if (it->b == 0xEB) {
        st->detune = it->arg;
    }
}

/* heard_value: Track_PlayNote's note with the detune, named by the nearer
 * semitone; a PSG tone's as it sounds. */
static int heard_value(int v, const Heard *st, int song_transpose, bool psg)
{
    int n = ex_snd_note_add(song_transpose,
                            ex_snd_note_add(v, st->transpose));
    int detune = (st->detune & 0x80) ? st->detune - 256 : st->detune;
    int s = n * 32 + detune + 16;
    int r = (s < 0) ? 0 : s >> 5;
    if (psg) {
        int octave = r / 12, semitone = r % 12;
        return ((octave > 2 ? octave : 2) + 1) * 12 + semitone;
    }
    return r;
}

/* heard.setdefault(id(it), []), v added if new. */
static void hear(SndItem *it, int v)
{
    for (int k = 0; k < it->nheard; k++) {
        if (it->heard[k] == v) {
            return;
        }
    }
    it->heard = ex_snd_grow(it->heard, &it->capheard, it->nheard + 1,
                            sizeof *it->heard);
    it->heard[it->nheard++] = (u16)v;
}

/* walk_phrase: a phrase as Phrase_NextNote reads it, repeats of n passes
 * (0: 256), three open at most. */
static void walk_phrase(SndItems *items, Heard *st, int song_transpose,
                        bool psg, long *budget)
{
    int count[3], at[3], depth = 0;
    int i = 0;
    while (i < items->n && *budget > 0) {
        (*budget)--;
        SndItem *it = &items->v[i++];
        if (it->kind == SND_NOTE) {
            hear(it, heard_value(it->b, st, song_transpose, psg));
        } else if (it->kind == SND_FLAG) {
            walk_flag(it, st);
            if (it->b == 0xEC && depth < 3) {
                count[depth] = it->arg;
                at[depth] = i;
                depth++;
            }
        } else if (it->kind == SND_BYTE && it->b == SND_LOOP && depth > 0) {
            count[depth - 1] = (count[depth - 1] - 1) & 0xFF;
            if (count[depth - 1] == 0) {
                depth--;
            } else {
                i = at[depth - 1];
            }
        }
    }
}

/* song['phrases'].get(k, []) */
static SndItems *song_phrase(SndSong *song, int k)
{
    static SndItems none;
    return (k < song->phrase_count && song->has[k]) ? &song->phrases[k]
                                                    : &none;
}

/* heard_song's run: a track's order list from `start`; the loop point
 * after it. */
static int heard_run(SndSong *song, int t, int start, int loop, Heard *st,
                     long *budget)
{
    const SndItems *items = &song->tracks[t];
    for (int k = start; k < items->n; k++) {
        const SndItem *it = &items->v[k];
        if (it->kind == SND_PHRASE) {
            walk_phrase(song_phrase(song, it->b), st, song->transpose,
                        t >= SND_FM_TRACKS, budget);
        } else if (it->kind == SND_FLAG) {
            walk_flag(it, st);
        } else if (it->kind == SND_BYTE && it->b == SND_LOOP) {
            loop = k + 1;
        }
    }
    return loop;
}

/* heard_song's repeats: a `loop` in a track. */
static bool has_loop(const SndSong *song)
{
    for (int t = 0; t < SND_TRACKS; t++) {
        for (int k = 0; k < song->tracks[t].n; k++) {
            const SndItem *it = &song->tracks[t].v[k];
            if (it->kind == SND_BYTE && it->b == SND_LOOP) {
                return true;
            }
        }
    }
    return false;
}

/* heard_song: every track from its start to its end; in a song with a
 * `loop`, then from its loop point again until it gets there with a
 * transpose and detune it had there before. */
static void heard_song(SndSong *song)
{
    bool repeats = has_loop(song);
    bool *seen = malloc(256 * 256);     /* (transpose, detune) at the loop */
    if (seen == NULL) {
        abort();
    }
    for (int t = 0; t < SND_TRACKS; t++) {
        const SndItems *items = &song->tracks[t];
        Heard st = { 0, 0 };
        long budget = 1L << 20;
        int loop = heard_run(song, t, 0, 0, &st, &budget);
        memset(seen, 0, 256 * 256);
        for (;;) {
            bool phrase_after = false;
            for (int k = loop; k < items->n; k++) {
                phrase_after = phrase_after
                               || items->v[k].kind == SND_PHRASE;
            }
            int key = (st.transpose & 0xFF) * 256 + (st.detune & 0xFF);
            if (!repeats || !phrase_after || seen[key] || budget <= 0) {
                break;
            }
            seen[key] = true;
            loop = heard_run(song, t, loop, loop, &st, &budget);
        }
    }
    free(seen);
}

/* heard_effects: every effect's tracks from their own commands, at the
 * song's transpose 0; the noise channel's left out. */
static void heard_effects(SndEffects *fx)
{
    for (int k = 0; k < fx->count; k++) {
        for (int t = 0; t < fx->fx[k].ntracks; t++) {
            const SndFxTrack *tr = &fx->fx[k].tracks[t];
            if (tr->channel >= SND_NOISE) {
                continue;
            }
            Heard st = { 0, 0 };
            for (int i = 0; i < tr->flags.n; i++) {
                if (tr->flags.v[i].kind == SND_FLAG) {
                    walk_flag(&tr->flags.v[i], &st);
                }
            }
            long budget = 1L << 20;
            walk_phrase(&fx->phrases[tr->phrase].items, &st, 0,
                        tr->channel >= SND_FM_TRACKS, &budget);
        }
    }
}

/* ---- writing the text ---- */

/* '    ' * depth */
static void indent(ExBuf *b, int depth)
{
    for (int k = 0; k < depth; k++) {
        exb_puts(b, "    ");
    }
}

/* A buffer emptied, kept. */
static void clear(ExBuf *b)
{
    b->len = 0;
    if (b->p != NULL) {
        b->p[0] = '\0';
    }
}

/* order_text, each line under the track's head ("    "). */
static void order_text(ExBuf *out, const SndItems *items, const char *names)
{
    ExBuf cur, tok;
    exb_init(&cur);
    exb_init(&tok);
    int bars = 0;
    bool prev_bar = false;
    for (int i = 0; i < items->n; i++) {
        const SndItem *it = &items->v[i];
        if (it->kind == SND_BYTE && it->b == SND_LOOP) {
            if (cur.len > 0) {
                exb_printf(out, "    %s\n", cur.p);
            }
            exb_puts(out, "    loop\n");
            clear(&cur);
            bars = 0;
            continue;
        }
        bool bar = true;
        clear(&tok);
        if (it->kind == SND_PHRASE) {
            exb_printf(&tok, "%d", it->b);
        } else if (it->kind == SND_BYTE && it->b == 0xFC) {
            exb_puts(&tok, "~");
        } else if (it->kind == SND_BYTE && it->b == 0xFB) {
            exb_puts(&tok, "-");
        } else if (it->kind == SND_FLAG) {
            ex_snd_flag_text(&tok, it->b, it->arg, names);
            bar = false;
        } else {
            exb_printf(&tok, "byte $%02X", it->b);
            bar = false;
        }
        if (bar && bars == 16) {
            exb_printf(out, "    %s\n", cur.p);
            clear(&cur);
            bars = 0;
        }
        if (cur.len > 0) {
            exb_puts(&cur, (bar && prev_bar) ? " " : "  ");
        }
        exb_puts(&cur, tok.p);
        prev_bar = bar;
        bars += bar;
    }
    if (cur.len > 0) {
        exb_printf(out, "    %s\n", cur.p);
    }
    exb_free(&cur);
    exb_free(&tok);
}

/* A phrase's line being written, and its notes for the heard comment. */
typedef struct {
    ExBuf    cur;
    int      depth, ticks, notes;
    SndItem *note[8];
} PLine;

/* phrase_text's flush, with annotate_text's comment where a note on the
 * line sounds other than it is written. */
static void flush(ExBuf *out, PLine *l)
{
    if (l->cur.len > 0) {
        exb_puts(out, "    ");
        indent(out, l->depth);
        exb_puts(out, l->cur.p);
        bool differs = false;
        for (int k = 0; k < l->notes; k++) {
            const SndItem *it = l->note[k];
            if (it->nheard > 0
                && !(it->nheard == 1 && it->heard[0] == it->b)) {
                differs = true;
            }
        }
        if (differs) {
            exb_puts(out, "    # heard:");
            for (int k = 0; k < l->notes; k++) {
                const SndItem *it = l->note[k];
                exb_puts(out, (k == 0) ? " " : "  ");
                if (it->nheard == 0) {
                    ex_snd_note_name(out, it->b);
                }
                for (int h = 0; h < it->nheard; h++) {
                    if (h > 0) {
                        exb_putc(out, '|');
                    }
                    ex_snd_note_name(out, it->heard[h]);
                }
            }
        }
        exb_putc(out, '\n');
    }
    clear(&l->cur);
    l->ticks = l->notes = 0;
}

/* phrase_text: a line a bar (96 ticks) or eight notes, repeats indented;
 * names: the instruments' its tracks pick from, or NULL. */
static void phrase_text(ExBuf *out, SndItems *items, const char *names)
{
    PLine l;
    memset(&l, 0, sizeof l);
    exb_init(&l.cur);
    int length = 24;
    for (int i = 0; i < items->n; i++) {
        SndItem *it = &items->v[i];
        if (it->kind == SND_NOTE) {
            if (l.cur.len > 0) {
                exb_puts(&l.cur, "  ");
            }
            ex_snd_note_name(&l.cur, it->b);
            for (int k = 0; k < it->np; k++) {
                exb_printf(&l.cur, " %d", it->p[k]);
            }
            if (it->np > 0) {
                length = it->p[0];
            }
            l.ticks += length;
            l.note[l.notes++] = it;
            if (l.ticks >= 96 || l.notes == 8) {
                flush(out, &l);
            }
        } else if (it->kind == SND_FLAG && it->b == 0xEC) {
            flush(out, &l);
            exb_puts(out, "    ");
            indent(out, l.depth);
            ex_snd_flag_text(out, it->b, it->arg, NULL);
            exb_putc(out, '\n');
            l.depth++;
        } else if (it->kind == SND_BYTE && it->b == SND_LOOP) {
            flush(out, &l);
            l.depth = (l.depth > 0) ? l.depth - 1 : 0;
            exb_puts(out, "    ");
            indent(out, l.depth);
            exb_puts(out, "end_repeat\n");
        } else {
            if (l.cur.len > 0) {
                exb_puts(&l.cur, "  ");
            }
            if (it->kind == SND_FLAG) {
                ex_snd_flag_text(&l.cur, it->b, it->arg, names);
            } else {
                exb_printf(&l.cur, "byte $%02X", it->b);
            }
        }
    }
    flush(out, &l);
    exb_free(&l.cur);
}

/* instrument_text's line of scalars: '    ' + '  '.join(...). */
static void join_fields(ExBuf *b, const Field *f, int from, int to,
                        const u8 *rec)
{
    exb_puts(b, "   ");
    for (int k = from; k < to; k++) {
        exb_printf(b, "%s%s %d", (k == from) ? " " : "  ", f[k].name,
                   (rec[f[k].off] >> f[k].shift) & f[k].top);
    }
    exb_putc(b, '\n');
}

/* field_masks: the bits no field holds. */
static void unused_masks(bool fm, u8 *unused)
{
    int size = fm ? SND_FM_SIZE : SND_PSG_SIZE;
    int used[SND_FM_SIZE] = { 0 };
    const Field *sc = fm ? FM_SCALARS : PSG_SCALARS;
    int nsc = fm ? COUNT(FM_SCALARS) : COUNT(PSG_SCALARS);
    for (int k = 0; k < nsc; k++) {
        used[sc[k].off] |= sc[k].top << sc[k].shift;
    }
    for (int k = 0; fm && k < COUNT(FM_ROWS); k++) {
        for (int o = 0; o < 4; o++) {
            used[FM_ROWS[k].off + o] |= FM_ROWS[k].top << FM_ROWS[k].shift;
        }
    }
    for (int k = 0; k < size; k++) {
        unused[k] = (u8)(~used[k] & 0xFF);
    }
}

/* instrument_text */
void ex_snd_instrument_text(ExBuf *b, bool fm, int n, const u8 *rec,
                            const char *name)
{
    exb_printf(b, "%s_instrument %d", fm ? "fm" : "psg", n);
    if (name != NULL && name[0] != '\0') {
        exb_printf(b, " %s", name);
    }
    exb_putc(b, '\n');
    if (fm) {
        join_fields(b, FM_SCALARS, 0, COUNT(FM_SCALARS), rec);
        exb_puts(b, "    #           op1  op2  op3  op4\n");
        for (int k = 0; k < COUNT(FM_ROWS); k++) {
            const Field *f = &FM_ROWS[k];
            exb_printf(b, "    %-10s", f->name);
            for (int o = 0; o < 4; o++) {
                exb_printf(b, "%5d", (rec[f->off + o] >> f->shift) & f->top);
            }
            exb_putc(b, '\n');
        }
    } else {
        join_fields(b, PSG_SCALARS, 0, 7, rec);     /* to `release` */
        join_fields(b, PSG_SCALARS, 7, COUNT(PSG_SCALARS), rec);
    }
    u8 unused[SND_FM_SIZE];
    unused_masks(fm, unused);
    int size = fm ? SND_FM_SIZE : SND_PSG_SIZE;
    bool any = false;
    for (int off = 0; off < size; off++) {
        if (rec[off] & unused[off]) {
            exb_puts(b, any ? " " : "    unused ");
            exb_printf(b, "%d:$%02X", off, rec[off] & unused[off]);
            any = true;
        }
    }
    if (any) {
        exb_putc(b, '\n');
    }
}

/* instruments_text: the FM records then the PSG ones, each after a blank
 * line. */
static void instruments_text(ExBuf *b, const u8 *data, u32 fm_at, int nfm,
                             const char *fm_names, u32 psg_at, int npsg,
                             const char *psg_names)
{
    char name[32];
    for (int k = 0; k < nfm; k++) {
        exb_putc(b, '\n');
        ex_snd_instrument_text(b, true, k,
                               data + fm_at - SND_BASE + SND_FM_SIZE * k,
                               ex_snd_name(fm_names, k, name));
    }
    for (int k = 0; k < npsg; k++) {
        exb_putc(b, '\n');
        ex_snd_instrument_text(b, false, k,
                               data + psg_at - SND_BASE + SND_PSG_SIZE * k,
                               ex_snd_name(psg_names, k, name));
    }
}

/* one_list: the names a phrase's instrument commands pick from (kinds: 1
 * FM tracks play it, 2 PSG ones): its tracks' list, or none. */
static const char *one_list(int kinds, const char *fm, const char *psg)
{
    return (kinds == 1) ? fm : (kinds == 2) ? psg : NULL;
}

/* song_text, with its heard comments (annotate_text). */
void ex_snd_song_text(ExBuf *b, int n, SndSong *song, const char *title)
{
    heard_song(song);
    if (title != NULL && title[0] != '\0') {
        exb_printf(b, "# %s (song %d", title, n);
    } else {
        exb_printf(b, "# Song %d (song %d", n, n);
    }
    if (ex_snd_use(n) != NULL) {
        exb_printf(b, ": %s", ex_snd_use(n));
    }
    exb_puts(b, "). docs/sound.md, \"The song text\", has the format.\n");
    bool psg_plays = false;
    for (int t = 6; t < 8; t++) {
        for (int k = 0; k < song->tracks[t].n; k++) {
            psg_plays = psg_plays || song->tracks[t].v[k].kind == SND_PHRASE;
        }
    }
    if (song->npsg == 0 && psg_plays) {
        exb_puts(b, "# It has no psg_instrument: its PSG tracks take "
                    "instrument 0 from the\n# 16 bytes after it, the start "
                    "of the next song (docs/sound.md, Room).\n");
    }
    exb_puts(b, "\n");
    exb_printf(b, "song  volume %d  tempo %d  transpose ", song->volume,
               song->tempo);
    signed_text(b, song->transpose);
    int defined = 0;
    for (int k = 0; k < song->phrase_count; k++) {
        if (song->has[k]) {
            defined = k + 1;
        }
    }
    if (song->phrase_count != defined) {
        exb_printf(b, "  phrases %d", song->phrase_count);
    }
    exb_puts(b, "\nvolumes ");
    for (int k = 0; k < 9; k++) {
        exb_printf(b, " %d", song->volumes[k]);
    }
    exb_putc(b, '\n');
    int kinds[SND_NOTE_END] = { 0 };
    for (int t = 0; t < SND_TRACKS; t++) {
        bool fm = t < SND_FM_TRACKS;
        exb_printf(b, "\ntrack %s\n", EX_SND_CHANNELS[t]);
        order_text(b, &song->tracks[t], fm ? song->fm_names
                                           : song->psg_names);
        for (int k = 0; k < song->tracks[t].n; k++) {
            const SndItem *it = &song->tracks[t].v[k];
            if (it->kind == SND_PHRASE) {
                kinds[it->b] |= fm ? 1 : 2;
            }
        }
    }
    for (int k = 0; k < song->phrase_count; k++) {
        if (!song->has[k]) {
            continue;
        }
        exb_printf(b, "\nphrase %d\n", k);
        phrase_text(b, &song->phrases[k],
                    one_list(k < SND_NOTE_END ? kinds[k] : 0,
                             song->fm_names, song->psg_names));
    }
    instruments_text(b, song->data, song->fm_at, song->nfm,
                     song->fm_names, song->psg_at, song->npsg,
                     song->psg_names);
}

/* effects_text, with its heard comments (annotate_text). */
void ex_snd_effects_text(ExBuf *b, SndEffects *fx)
{
    heard_effects(fx);
    exb_puts(b, EFFECTS_HEAD);
    exb_printf(b, "\neffects  first_byte %d\n", fx->first_byte);
    int *kinds = calloc((size_t)fx->nphrases + 1, sizeof *kinds);
    if (kinds == NULL) {
        abort();
    }
    for (int k = 0; k < fx->count; k++) {
        exb_printf(b, "\neffect %d %s\n", k, fx->fx[k].name);
        for (int t = 0; t < fx->fx[k].ntracks; t++) {
            const SndFxTrack *tr = &fx->fx[k].tracks[t];
            bool fm = tr->channel < SND_FM_TRACKS;
            exb_printf(b, "    track %s", EX_SND_CHANNELS[tr->channel]);
            for (int i = 0; i < tr->flags.n; i++) {
                const SndItem *it = &tr->flags.v[i];
                exb_puts(b, "  ");
                if (it->kind == SND_FLAG) {
                    ex_snd_flag_text(b, it->b, it->arg,
                                     fm ? fx->fm_names : fx->psg_names);
                } else {
                    exb_printf(b, "byte $%02X", it->b);
                }
            }
            exb_printf(b, "  %s\n", fx->phrases[tr->phrase].name);
            kinds[tr->phrase] |= fm ? 1 : 2;
        }
    }
    for (int u = 0; u < fx->nphrases; u++) {
        exb_printf(b, "\nphrase %s\n", fx->phrases[u].name);
        phrase_text(b, &fx->phrases[u].items,
                    one_list(kinds[u], fx->fm_names, fx->psg_names));
    }
    free(kinds);
    instruments_text(b, fx->data, fx->fm_at, fx->nfm, fx->fm_names,
                     fx->psg_at, fx->npsg, fx->psg_names);
}

/* write_text's sounds.txt. */
void ex_snd_sounds_text(ExBuf *b, const SndModel *m, char (*file)[128])
{
    exb_puts(b, SOUNDS_HEAD);
    exb_printf(b, "\nsong_table %d\n", m->song_table);
    for (int n = 0; n < m->nsongs; n++) {
        exb_printf(b, "song %d %s\n", n, file[n]);
    }
    exb_printf(b, "effects $%04X effects.txt\n", (unsigned)m->effects_at);
    if (m->ntail > 0) {
        exb_puts(b, "tail\n");
        const u8 *t = m->data + (m->tail_at - SND_BASE);
        for (int k = 0; k < m->ntail; k += 16) {
            exb_puts(b, "   ");
            for (int i = k; i < k + 16 && i < m->ntail; i++) {
                exb_printf(b, " %02X", t[i]);
            }
            exb_putc(b, '\n');
        }
    }
}
