/* (Session 78) tools/hztracker.py's export side in C (ex_sound.h): a
 * song's tracks walked as the driver's sequencing plays them (Walk), each
 * channel's first pass as cells (channel_cells), cut into sections
 * (layout) and written as a tracker (song_tracker_text); and check, that a
 * tracker plays as its text. Where the Python raises ValueError this
 * fails; where it would never end (a loop with nothing to play) this
 * fails too. */
#include "ex_sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INSTRUMENT 0xE0
#define WAIT       0xE5
#define TRANSPOSE  0xE8
#define REPEAT     0xEC
#define PSG_OCTAVE 12           /* a PSG column names its notes an octave up */
#define WALK_MOST  (1L << 24)   /* ticks, or reads in a tick: never ends */

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const int SECTION_TICKS[3] = { 96, 192, 384 };
static const int SONG_ROWS[6] = { 6, 8, 4, 3, 2, 1 };
static const int ROW_TICKS[6] = { 8, 6, 4, 3, 2, 1 };

/* ---- the driver's sequencing ---- */

/* Walk's events: (tick, kind, ...). */
enum { EV_CMD, EV_MARK, EV_NOTE, EV_OFF, EV_END, EV_WRAP };

typedef struct {
    int  tick;
    u8   kind;
    u8   b, arg;                /* EV_CMD: the flag */
    u8   note;                  /* EV_NOTE: the note; MARK, END: transpose */
    u8   length, gate, volume;
    bool held;
} Event;

typedef struct {
    const SndSong  *song;
    const SndItems *items;
    bool            repeat;
    int             order, loop;
    const SndItems *phrase;
    int             pi;
    int             length, gate, transpose;
    u8              sticky[3];
    int             depth, count[3], at[3];
    Event          *ev;
    int             nev, cap;
    int             tick;
    bool            on;
    const char     *bad;        /* why it can't be followed */
} Walk;

static const SndItems NO_ITEMS;

/* Walk.ev: an event at this tick. */
static Event *ev(Walk *w, int kind)
{
    w->ev = ex_snd_grow(w->ev, &w->cap, w->nev + 1, sizeof *w->ev);
    Event *e = &w->ev[w->nev++];
    memset(e, 0, sizeof *e);
    e->tick = w->tick;
    e->kind = (u8)kind;
    return e;
}

/* transposed: Flag_Transpose (0 clears; else added, kept under $60). */
static int transposed(int t, int arg)
{
    if (arg == 0) {
        return 0;
    }
    int v = (t + arg) & 0xFF;
    if (v & 0x80) {
        return (v + SND_NOTES) & 0xFF;
    }
    return (v >= SND_NOTES) ? v - SND_NOTES : v;
}

/* Walk.flag */
static void walk_flag(Walk *w, const SndItem *it)
{
    if (it->b == REPEAT) {
        if (w->depth < 3) {
            w->count[w->depth] = it->arg;
            w->at[w->depth] = w->pi;
            w->depth++;
        }
        return;
    }
    if (it->b == TRANSPOSE) {
        w->transpose = transposed(w->transpose, it->arg);
    } else if (it->b == WAIT) {
        if (it->arg == 0) {
            w->bad = "wait 0 (every waiting track on to its phrase's end) "
                     "is not followed";
        }
        w->length = it->arg + 1;
        w->gate = 1;
    }
    Event *e = ev(w, EV_CMD);
    e->b = it->b;
    e->arg = it->arg;
}

/* Walk.next_phrase: Track_NextPhrase, the order list's commands run and
 * its loop point noted, up to a phrase; false at its end. */
static bool next_phrase(Walk *w)
{
    while (w->order < w->items->n) {
        const SndItem *it = &w->items->v[w->order++];
        if (it->kind == SND_PHRASE) {
            const SndSong *s = w->song;
            w->phrase = (it->b < s->phrase_count && s->has[it->b])
                        ? &s->phrases[it->b] : &NO_ITEMS;
            w->pi = 0;
            return true;
        }
        if (it->kind == SND_FLAG) {
            walk_flag(w, it);
        } else if (it->kind == SND_BYTE && it->b == SND_LOOP) {
            w->loop = w->order;
            ev(w, EV_MARK)->note = (u8)w->transpose;
        }
    }
    ev(w, EV_END)->note = (u8)w->transpose;
    return false;
}

/* Walk.next_note: Phrase_NextNote, commands run and repeats closed or gone
 * round, to a note (its numbers kept); NULL at the phrase's end. */
static const SndItem *next_note(Walk *w)
{
    while (w->pi < w->phrase->n) {
        const SndItem *it = &w->phrase->v[w->pi++];
        if (it->kind == SND_NOTE) {
            for (int k = 0; k < it->np; k++) {
                w->sticky[k] = it->p[k];
            }
            return it;
        }
        if (it->kind == SND_FLAG) {
            walk_flag(w, it);
        } else if (it->kind == SND_BYTE && it->b == SND_LOOP
                   && w->depth > 0) {
            int top = w->depth - 1;
            w->count[top] = (w->count[top] - 1) & 0xFF;
            if (w->count[top] == 0) {
                w->depth--;
            } else {
                w->pi = w->at[top];
            }
        }
    }
    w->depth = 0;
    return NULL;
}

/* Walk.step */
static void walk_step(Walk *w, int tick)
{
    w->tick = tick;
    if (!w->on) {
        return;
    }
    w->length = (w->length - 1) & 0xFF;
    if (w->length != 0) {
        w->gate = (w->gate - 1) & 0xFF;
        if (w->gate == 0) {
            ev(w, EV_OFF);
        }
        return;
    }
    for (long reads = 0; w->length == 0; reads++) {
        if (w->bad != NULL || reads > WALK_MOST) {
            if (w->bad == NULL) {
                w->bad = "a channel goes round with no note to play";
            }
            w->on = false;
            return;
        }
        const SndItem *it = next_note(w);
        if (it != NULL) {
            Event *e = ev(w, EV_NOTE);
            e->note = (u8)ex_snd_note_add(w->song->transpose,
                                          ex_snd_note_add(it->b,
                                                          w->transpose));
            e->length = w->sticky[0];
            e->gate = w->sticky[1];
            e->volume = w->sticky[2];
            e->held = ((0 - w->gate) & 0x80) != 0;
            w->length = w->sticky[0];
            w->gate = w->sticky[1];
            return;
        }
        int wraps = 0;
        while (!next_phrase(w)) {
            ev(w, EV_OFF);
            if (!w->repeat) {
                w->on = false;
                return;
            }
            if (++wraps > 1) {
                w->bad = "a channel comes back to a loop with nothing to "
                         "play";
                w->on = false;
                return;
            }
            w->order = w->loop;
            ev(w, EV_WRAP);
        }
    }
}

/* Walk(song, t, repeat) */
static void walk_init(Walk *w, const SndSong *song, int t, bool repeat)
{
    memset(w, 0, sizeof *w);
    w->song = song;
    w->items = &song->tracks[t];
    w->repeat = repeat;
    w->phrase = &NO_ITEMS;
    w->length = 1;
    w->on = next_phrase(w) && w->bad == NULL;
}

/* first_pass: channel t's events to the end of its order list. */
static void first_pass(Walk *w, const SndSong *song, int t)
{
    walk_init(w, song, t, false);
    for (int tick = 1; w->on; tick++) {
        if (tick > WALK_MOST) {
            w->bad = "a channel that never ends";
            break;
        }
        walk_step(w, tick);
    }
}

/* song_repeats: a loop point in a track. */
static bool song_repeats(const SndSong *song)
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

/* ---- a song's first pass as cells ---- */

/* A cell's command: a flag (b, arg) or SND_MARK, ('loop',). */
typedef struct {
    u8 kind, b, arg;
} Chg;

typedef struct {
    int  tick;
    bool end;                   /* the end cell: no note */
    u8   note, length, gate, volume;
    int  chg, nchg;             /* its commands: Chan.chg[chg..] */
} Cell;

typedef struct {
    Chg  *line;                 /* its commands as the song starts */
    int   nline, capline;
    Cell *cells;                /* the notes', then the end's */
    int   ncells, capcells;
    int   notes;
    Chg  *chg;
    int   nchg, capchg;
} Chan;

/* A command onto a list (a channel's line, or its cells'). */
static void chg_push(Chg **v, int *n, int *cap, int kind, int b, int arg)
{
    *v = ex_snd_grow(*v, cap, *n + 1, sizeof **v);
    (*v)[*n].kind = (u8)kind;
    (*v)[*n].b = (u8)b;
    (*v)[*n].arg = (u8)arg;
    (*n)++;
}

/* cells.append: a new cell, zeroed. */
static Cell *cell_push(Chan *c)
{
    c->cells = ex_snd_grow(c->cells, &c->capcells, c->ncells + 1,
                           sizeof *c->cells);
    Cell *x = &c->cells[c->ncells++];
    memset(x, 0, sizeof *x);
    return x;
}

/* A channel's lists. */
static void chan_free(Chan *c)
{
    free(c->line);
    free(c->cells);
    free(c->chg);
    memset(c, 0, sizeof *c);
}

/* turn_transpose: how far a repeated channel's next turn plays from its
 * first (the text's transposes adding up), the same for every note of
 * the turn, or 0. */
static bool turn_transpose(HzEx *x, const SndSong *song, int t, int *more)
{
    Walk w;
    walk_init(&w, song, t, true);
    int ends = 0, seen = 0;
    for (int tick = 1; w.on && ends < 2; tick++) {
        if (tick > WALK_MOST) {
            w.bad = "a channel that never ends";
            break;
        }
        walk_step(&w, tick);
        for (int k = seen; k < w.nev; k++) {
            ends += w.ev[k].kind == EV_END;
        }
        seen = w.nev;
    }
    if (w.bad != NULL) {
        free(w.ev);
        return ex_fail(x, "channel %s: %s", EX_SND_CHANNELS[t], w.bad);
    }
    bool looped = true;
    for (int k = 0; k < w.nev; k++) {
        looped = looped && w.ev[k].kind != EV_MARK;
    }
    int *first = malloc(sizeof *first * (size_t)(w.nev + 1));
    int *second = malloc(sizeof *second * (size_t)(w.nev + 1));
    if (first == NULL || second == NULL) {
        abort();
    }
    int nfirst = 0, nsecond = 0, turn = 0;
    for (int k = 0; k < w.nev; k++) {
        const Event *e = &w.ev[k];
        if (e->kind == EV_MARK) {
            looped = true;
        } else if (e->kind == EV_WRAP) {
            turn++;
        } else if (e->kind == EV_NOTE && turn == 0 && looped) {
            first[nfirst++] = e->note;
        } else if (e->kind == EV_NOTE && turn == 1) {
            second[nsecond++] = e->note;
        }
    }
    /* the set {(b - a) % $60}: one value, or 0 */
    int m = -1;
    bool one = nfirst == nsecond && nfirst > 0;
    for (int k = 0; one && k < nfirst; k++) {
        int d = ((second[k] - first[k]) % SND_NOTES + SND_NOTES) % SND_NOTES;
        if (m >= 0 && d != m) {
            one = false;
        }
        m = d;
    }
    *more = !one ? 0 : (m > SND_NOTES / 2) ? m - SND_NOTES : m;
    free(first);
    free(second);
    free(w.ev);
    return true;
}

/* channel_cells: channel t as the tracker writes it: its commands as the
 * song starts, its cells, its end (with how far the next turn plays, a
 * transpose, if not the same). */
static bool channel_cells(HzEx *x, const SndSong *song, int t, Chan *c)
{
    const char *name = EX_SND_CHANNELS[t];
    memset(c, 0, sizeof *c);
    Walk w;
    first_pass(&w, song, t);
    if (w.bad != NULL) {
        free(w.ev);
        return ex_fail(x, "channel %s: %s", name, w.bad);
    }
    int pending = 0;
    bool ok = true;
    for (int k = 0; ok && k < w.nev; k++) {
        const Event *e = &w.ev[k];
        if (e->kind == EV_CMD) {
            if (e->b == WAIT && e->tick != 0) {
                ok = ex_fail(x, "channel %s: a wait after the start", name);
            } else if (e->b != WAIT && e->b != TRANSPOSE) {
                if (e->tick == 0) {
                    chg_push(&c->line, &c->nline, &c->capline, SND_FLAG,
                             e->b, e->arg);
                } else {
                    chg_push(&c->chg, &c->nchg, &c->capchg, SND_FLAG, e->b,
                             e->arg);
                }
            }
        } else if (e->kind == EV_MARK) {
            if (e->tick == 0) {
                chg_push(&c->line, &c->nline, &c->capline, SND_MARK, 0, 0);
            } else {
                chg_push(&c->chg, &c->nchg, &c->capchg, SND_MARK, 0, 0);
            }
        } else if (e->kind == EV_NOTE) {
            Cell *cell = cell_push(c);
            cell->tick = e->tick;
            cell->note = e->note;
            cell->length = e->length;
            cell->gate = e->gate;
            cell->volume = e->volume;
            cell->chg = pending;
            cell->nchg = c->nchg - pending;
            pending = c->nchg;
            c->notes++;
        } else if (e->kind == EV_END && c->notes > 0) {
            int more = 0;
            ok = turn_transpose(x, song, t, &more);
            if (ok && more != 0) {
                chg_push(&c->chg, &c->nchg, &c->capchg, SND_FLAG, TRANSPOSE,
                         more & 0xFF);
            }
            Cell *cell = cell_push(c);
            cell->tick = e->tick;
            cell->end = true;
            cell->chg = pending;
            cell->nchg = c->nchg - pending;
        }
    }
    free(w.ev);
    if (!ok) {
        return false;
    }
    if (c->notes == 0) {
        for (int k = pending; k < c->nchg; k++) {
            chg_push(&c->line, &c->nline, &c->capline, c->chg[k].kind,
                     c->chg[k].b, c->chg[k].arg);
        }
    } else if (c->ncells != c->notes + 1) {
        return ex_fail(x, "channel %s: no end", name);
    }
    for (int k = 0; k + 1 < c->ncells; k++) {
        const Cell *a = &c->cells[k], *b = &c->cells[k + 1];
        if (b->tick - a->tick != (a->length ? a->length : 256)) {
            return ex_fail(x, "channel %s: a note at tick %d lasts %d, the "
                           "next is at %d", name, a->tick, a->length,
                           b->tick);
        }
    }
    return true;
}

/* ---- the text ---- */

/* is_psg */
static bool is_psg(int c)
{
    return c >= SND_FM_TRACKS;
}

/* cell_note_name: FastTracker's three letters, C-4, C#4; a PSG channel's
 * an octave up. */
static void cell_note_name(ExBuf *b, int note, int c)
{
    if (is_psg(c)) {
        note += PSG_OCTAVE;
    }
    const char *name = NOTE_NAMES[note % 12];
    exb_printf(b, "%s%s%d", name, name[1] == '\0' ? "-" : "", note / 12);
}

/* default_gate */
static int default_gate(int length)
{
    return (length - 2 > 1) ? length - 2 : 1;
}

/* gate_text's choice: -1 hold, -2 none written, else `gate N`. */
static int gate_key(const Cell *x)
{
    if (x->gate >= x->length) {
        return -1;
    }
    if (x->gate == default_gate(x->length)) {
        return -2;
    }
    return x->gate;
}

/* change_text */
static void change_text(ExBuf *b, const Chg *it, const char *names)
{
    if (it->kind == SND_MARK) {
        exb_puts(b, "loop");
    } else {
        ex_snd_flag_text(b, it->b, it->arg, names);
    }
}

/* names[channel_kind(c)]: the list a channel's instrument picks from. */
static const char *chan_names(const SndSong *song, int c)
{
    return is_psg(c) ? song->psg_names : song->fm_names;
}

/* tempo_said */
static void tempo_said(ExBuf *b, int tempo)
{
    double per_second = 53267.0 / (16 * (8324 / (tempo + 50)));
    exb_printf(b, "Tempo %d: %.1f ticks a second, a beat of 24 ticks at %d "
               "BPM", tempo, per_second, (int)rint(per_second * 60 / 24));
}

/* song_rows: 6 ticks a row where that puts nine in ten of the cells on a
 * row, else 8, else finer; a channel's cells counted from its first. */
static int song_rows(const Chan *chans, const int *cols, int ncols)
{
    for (int i = 0; i < 6; i++) {
        int r = SONG_ROWS[i];
        long on = 0, total = 0;
        for (int ci = 0; ci < ncols; ci++) {
            const Chan *c = &chans[cols[ci]];
            for (int k = 0; k < c->ncells; k++) {
                total++;
                on += (c->cells[k].tick - c->cells[0].tick) % r == 0;
            }
        }
        if (on * 10 >= total * 9) {
            return r;
        }
    }
    return 1;
}

/* A section's column: its cells (Chan.cells[first..]), the section it
 * copies (-1: written out), whether it was keyed (a copy may be of it). */
typedef struct {
    int  first, n, copy;
    bool keyed;
} Part;

typedef struct {
    int start, ticks, r;
} Sect;

/* cell_key's match: two cells, each from its section's start. */
static bool same_cell(const Chan *c, const Cell *a, int sa, const Cell *b,
                      int sb)
{
    if (a->tick - sa != b->tick - sb || a->note != b->note
        || gate_key(a) != gate_key(b) || a->volume != b->volume
        || a->nchg != b->nchg) {
        return false;
    }
    for (int k = 0; k < a->nchg; k++) {
        const Chg *p = &c->chg[a->chg + k], *q = &c->chg[b->chg + k];
        if (p->kind != q->kind || p->b != q->b || p->arg != q->arg) {
            return false;
        }
    }
    return true;
}

/* layout: the song in sections of `section` ticks, the last cut short
 * after its last row with a cell; each its start, ticks and rows' ticks,
 * and a part a column. *sects and *parts are malloc'd, the caller's. */
static int layout(HzEx *x, const Chan *chans, const int *cols, int ncols,
                  int span, int section, Sect **sects, Part **parts)
{
    int rows = song_rows(chans, cols, ncols);
    int count = (span - 1) / section + 1;
    Sect *s = calloc((size_t)count, sizeof *s);
    Part *p = calloc((size_t)count * (size_t)(ncols ? ncols : 1), sizeof *p);
    if (s == NULL || p == NULL) {
        abort();
    }
    int pos[SND_TRACKS] = { 0 };
    for (int k = 0; k < count; k++) {
        int start = 1 + k * section;
        for (int ci = 0; ci < ncols; ci++) {
            const Chan *c = &chans[cols[ci]];
            while (pos[ci] < c->ncells && c->cells[pos[ci]].tick < start) {
                pos[ci]++;
            }
            Part *pt = &p[k * ncols + ci];
            pt->first = pos[ci];
            pt->copy = -1;
            while (pt->first + pt->n < c->ncells
                   && c->cells[pt->first + pt->n].tick < start + section) {
                pt->n++;
            }
            bool keyed = pt->n > 0 && k < count - 1;
            for (int i = 0; keyed && i < pt->n; i++) {
                const Cell *cell = &c->cells[pt->first + i];
                keyed = !cell->end;
                for (int h = 0; keyed && h < cell->nchg; h++) {
                    keyed = c->chg[cell->chg + h].kind != SND_MARK;
                }
            }
            pt->keyed = keyed;
            for (int j = 0; keyed && j < k; j++) {
                const Part *q = &p[j * ncols + ci];
                bool same = q->keyed && q->n == pt->n;
                for (int i = 0; same && i < pt->n; i++) {
                    same = same_cell(c, &c->cells[q->first + i], s[j].start,
                                     &c->cells[pt->first + i], start);
                }
                if (same) {
                    pt->copy = j;
                    break;
                }
            }
        }
        int r = 1;
        for (int i = 0; i < 6; i++) {
            int rr = ROW_TICKS[i];
            if (rr > rows || section % rr) {
                continue;
            }
            r = rr;
            bool all = true;
            for (int ci = 0; all && ci < ncols; ci++) {
                const Part *pt = &p[k * ncols + ci];
                const Chan *c = &chans[cols[ci]];
                for (int h = 1; pt->copy < 0 && h < pt->n; h++) {
                    if ((c->cells[pt->first + h].tick - start) / rr
                        == (c->cells[pt->first + h - 1].tick - start) / rr) {
                        all = false;
                    }
                }
            }
            if (all) {
                break;
            }
        }
        int ticks = section;
        if (k == count - 1) {
            int most = -1;
            for (int ci = 0; ci < ncols; ci++) {
                const Part *pt = &p[k * ncols + ci];
                for (int h = 0; h < pt->n; h++) {
                    int o = chans[cols[ci]].cells[pt->first + h].tick - start;
                    most = (o > most) ? o : most;
                }
            }
            if (most < 0) {
                free(s);
                free(p);
                ex_fail(x, "the last section has no cell");
                return -1;
            }
            ticks = (most / r + 1) * r;
        }
        s[k].start = start;
        s[k].ticks = ticks;
        s[k].r = r;
    }
    *sects = s;
    *parts = p;
    return count;
}

/* A line, its trailing spaces dropped, then a newline. */
static void put_line(ExBuf *out, ExBuf *line)
{
    while (line->len > 0 && line->p[line->len - 1] == ' ') {
        line->len--;
    }
    if (line->p != NULL) {
        line->p[line->len] = '\0';
        exb_puts(out, line->p);
    }
    exb_putc(out, '\n');
    line->len = 0;
    if (line->p != NULL) {
        line->p[0] = '\0';
    }
}

/* s.ljust(width) */
static void pad(ExBuf *b, const char *s, size_t width)
{
    exb_puts(b, s);
    for (size_t k = strlen(s); k < width; k++) {
        exb_putc(b, ' ');
    }
}

/* grid_text */
static void grid_text(ExBuf *out, const SndSong *song, const Chan *chans,
                      const int *cols, int ncols, const Sect *sects,
                      const Part *parts, int count)
{
    int volume[SND_TRACKS];
    for (int c = 0; c < SND_TRACKS; c++) {
        volume[c] = -1;
    }
    ExBuf line;
    exb_init(&line);
    for (int k = 0; k < count; k++) {
        const Sect *sc = &sects[k];
        int start = sc->start, r = sc->r;
        int rows = sc->ticks / r;
        exb_printf(out, "\nsection %02d  %d row%s x %d tick%s    # ticks "
                   "%d-%d\n", k, rows, rows > 1 ? "s" : "", r,
                   r > 1 ? "s" : "", start, start + sc->ticks - 1);
        ExBuf *grid = calloc((size_t)rows * (size_t)ncols, sizeof *grid);
        if (grid == NULL) {
            abort();
        }
        char heads[SND_TRACKS][24];
        for (int ci = 0; ci < ncols; ci++) {
            int c = cols[ci];
            const Chan *ch = &chans[c];
            const Part *pt = &parts[k * ncols + ci];
            if (pt->copy < 0) {
                snprintf(heads[ci], sizeof heads[ci], "%s",
                         EX_SND_CHANNELS[c]);
            } else {
                snprintf(heads[ci], sizeof heads[ci], "%s = %02d",
                         EX_SND_CHANNELS[c], pt->copy);
            }
            for (int h = 0; h < pt->n; h++) {
                const Cell *x = &ch->cells[pt->first + h];
                int o = x->tick - start;
                if (pt->copy < 0) {
                    ExBuf *g = &grid[(o / r) * ncols + ci];
                    g->len = 0;
                    if (x->end) {
                        exb_puts(g, "end");
                    } else {
                        cell_note_name(g, x->note, c);
                    }
                    if (o % r) {
                        exb_printf(g, " @%d", o % r);
                    }
                    if (!x->end) {
                        int gk = gate_key(x);
                        if (gk == -1) {
                            exb_puts(g, " hold");
                        } else if (gk >= 0) {
                            exb_printf(g, " gate %d", gk);
                        }
                        if (x->volume != volume[c]) {
                            exb_printf(g, " v%d", x->volume);
                        }
                    }
                    for (int i = 0; i < x->nchg; i++) {
                        exb_putc(g, ' ');
                        change_text(g, &ch->chg[x->chg + i],
                                    chan_names(song, c));
                    }
                }
                if (!x->end) {
                    volume[c] = x->volume;
                }
            }
        }
        size_t widths[SND_TRACKS];
        for (int ci = 0; ci < ncols; ci++) {
            widths[ci] = strlen(heads[ci]);
            for (int n = 0; n < rows; n++) {
                ExBuf *g = &grid[n * ncols + ci];
                if (g->len == 0 && parts[k * ncols + ci].copy < 0) {
                    exb_puts(g, "...");
                }
                widths[ci] = (g->len > widths[ci]) ? g->len : widths[ci];
            }
        }
        exb_puts(&line, "row | ");
        for (int ci = 0; ci < ncols; ci++) {
            if (ci > 0) {
                exb_puts(&line, " | ");
            }
            pad(&line, heads[ci], widths[ci]);
        }
        put_line(out, &line);
        for (int n = 0; n < rows; n++) {
            exb_printf(&line, " %02d | ", n);
            for (int ci = 0; ci < ncols; ci++) {
                const ExBuf *g = &grid[n * ncols + ci];
                if (ci > 0) {
                    exb_puts(&line, " | ");
                }
                pad(&line, (g->p != NULL) ? g->p : "", widths[ci]);
            }
            put_line(out, &line);
        }
        for (int i = 0; i < rows * ncols; i++) {
            exb_free(&grid[i]);
        }
        free(grid);
    }
    exb_free(&line);
}

/* song_tracker_text, with instruments_read's instruments: those past the
 * song's own that its channels pick, as the window has them there. */
bool ex_snd_tracker_text(HzEx *x, ExBuf *b, int n, const SndSong *song,
                         const char *title)
{
    Chan chans[SND_TRACKS];
    memset(chans, 0, sizeof chans);
    bool ok = true;
    for (int t = 0; ok && t < SND_TRACKS; t++) {
        ok = channel_cells(x, song, t, &chans[t]);
    }
    int cols[SND_TRACKS], ncols = 0, span = 1;
    for (int t = 0; ok && t < SND_TRACKS; t++) {
        if (chans[t].notes > 0) {
            cols[ncols++] = t;
            int end = chans[t].cells[chans[t].ncells - 1].tick;
            span = (end > span) ? end : span;
        }
    }
    if (ok) {
        bool repeat = song_repeats(song);
        if (title != NULL && title[0] != '\0') {
            exb_printf(b, "# %s (song %d", title, n);
        } else {
            exb_printf(b, "# Song %d (song %d", n, n);
        }
        if (ex_snd_use(n) != NULL) {
            exb_printf(b, ": %s", ex_snd_use(n));
        }
        exb_puts(b, "), as a tracker. docs/sound.md, \"The tracker\", has "
                    "the format.\n# ");
        tempo_said(b, song->tempo);
        exb_printf(b, ". The game %s.\n\n",
                   repeat ? "repeats it: each channel goes back to its loop "
                            "from its end" : "plays it once");
        exb_printf(b, "song  volume %d  tempo %d\nvolumes ", song->volume,
                   song->tempo);
        for (int k = 0; k < 9; k++) {
            exb_printf(b, " %d", song->volumes[k]);
        }
        exb_putc(b, '\n');
        for (int t = 0; t < SND_TRACKS; t++) {
            if (chans[t].nline == 0 && chans[t].notes == 0) {
                continue;
            }
            exb_printf(b, "channel %s", EX_SND_CHANNELS[t]);
            for (int k = 0; k < chans[t].nline; k++) {
                exb_puts(b, "  ");
                change_text(b, &chans[t].line[k], chan_names(song, t));
            }
            exb_putc(b, '\n');
        }
    }
    ExBuf best;
    exb_init(&best);
    bool have = false;
    for (int i = 0; ok && i < 3; i++) {
        Sect *sects = NULL;
        Part *parts = NULL;
        int count = layout(x, chans, cols, ncols, span, SECTION_TICKS[i],
                           &sects, &parts);
        if (count < 0) {
            ok = false;
            break;
        }
        ExBuf g;
        exb_init(&g);
        grid_text(&g, song, chans, cols, ncols, sects, parts, count);
        if (!have || g.len < best.len) {
            exb_free(&best);
            best = g;
            have = true;
        } else {
            exb_free(&g);
        }
        free(sects);
        free(parts);
    }
    if (ok && best.p != NULL) {
        exb_puts(b, best.p);
    }
    exb_free(&best);

    /* instruments_read: the highest instrument each kind's channels pick */
    int top_fm = 0, top_psg = -1;
    for (int t = 0; ok && t < SND_TRACKS; t++) {
        const Chan *c = &chans[t];
        int *top = is_psg(t) ? &top_psg : &top_fm;
        if (c->notes > 0 && *top < 0) {
            *top = 0;
        }
        for (int k = 0; k < c->nline; k++) {
            if (c->line[k].kind == SND_FLAG && c->line[k].b == INSTRUMENT
                && c->line[k].arg > *top) {
                *top = c->line[k].arg;
            }
        }
        for (int h = 0; h < c->notes; h++) {
            const Cell *cell = &c->cells[h];
            for (int k = cell->chg; k < cell->chg + cell->nchg; k++) {
                if (c->chg[k].kind == SND_FLAG && c->chg[k].b == INSTRUMENT
                    && c->chg[k].arg > *top) {
                    *top = c->chg[k].arg;
                }
            }
        }
    }
    int nfm = (song->nfm > top_fm + 1) ? song->nfm : top_fm + 1;
    int npsg = (song->npsg > top_psg + 1) ? song->npsg : top_psg + 1;
    if (ok && (song->fm_at - SND_BASE + (u32)(SND_FM_SIZE * nfm) > SND_SIZE
               || song->psg_at - SND_BASE + (u32)(SND_PSG_SIZE * npsg)
                  > SND_SIZE)) {
        ok = ex_fail(x, "song %d: its instruments run past the window", n);
    }
    char name[32];
    for (int k = 0; ok && k < nfm; k++) {
        if (k == song->nfm) {
            exb_puts(b, "\n# Instruments its channels pick past its own: as "
                        "the song text's layout has\n# them there "
                        "(docs/sound.md, Room).\n");
        }
        exb_putc(b, '\n');
        ex_snd_instrument_text(b, true, k, song->data + song->fm_at - SND_BASE
                               + SND_FM_SIZE * k,
                               (k < song->nfm) ? ex_snd_name(song->fm_names,
                                                             k, name)
                                               : NULL);
    }
    for (int k = 0; ok && k < npsg; k++) {
        if (k == song->npsg) {
            exb_puts(b, "\n# Instruments its PSG channels pick past its own: "
                        "in the song text the bytes\n# after the song, the "
                        "start of the next (docs/sound.md, Room).\n");
        }
        exb_putc(b, '\n');
        ex_snd_instrument_text(b, false, k, song->data + song->psg_at
                               - SND_BASE + SND_PSG_SIZE * k,
                               (k < song->npsg) ? ex_snd_name(song->psg_names,
                                                              k, name)
                                                : NULL);
    }
    for (int t = 0; t < SND_TRACKS; t++) {
        chan_free(&chans[t]);
    }
    return ok;
}

/* ---- the check ---- */

/* played: what channel t plays over `ticks`, repeated, to compare: the
 * notes (a gate past the length taken as the length) and commands, but
 * the transposes, the start's wait and the loop points. *bad: why it
 * can't be followed. */
static Event *played(const SndSong *song, int t, int ticks, int *n,
                     const char **bad)
{
    Walk w;
    walk_init(&w, song, t, true);
    for (int k = 1; k <= ticks && w.on; k++) {
        walk_step(&w, k);
    }
    *bad = w.bad;
    int m = 0;
    for (int k = 0; k < w.nev; k++) {
        Event e = w.ev[k];
        if (e.kind == EV_MARK || (e.kind == EV_CMD
                                  && (e.b == TRANSPOSE || e.b == WAIT))) {
            continue;
        }
        if (e.kind == EV_NOTE && e.gate > e.length) {
            e.gate = e.length;
        } else if (e.kind == EV_END) {
            e.note = 0;
        }
        w.ev[m++] = e;
    }
    *n = m;
    return w.ev;
}

/* played's events, compared as check compares them. */
static bool same_event(const Event *a, const Event *b)
{
    return a->tick == b->tick && a->kind == b->kind && a->b == b->b
           && a->arg == b->arg && a->note == b->note
           && a->length == b->length && a->gate == b->gate
           && a->volume == b->volume && a->held == b->held;
}

/* check: every channel over the song and two more turns of it. */
bool ex_snd_tracker_check(HzEx *x, const SndSong *song,
                          const SndSong *tracked, const char *name)
{
    int span = 0;
    for (int t = 0; t < SND_TRACKS; t++) {
        Walk w;
        first_pass(&w, song, t);
        const char *bad = w.bad;
        int last = (w.nev > 0) ? w.ev[w.nev - 1].tick : 0;
        free(w.ev);
        if (bad != NULL) {
            return ex_fail(x, "%s: channel %s: %s", name, EX_SND_CHANNELS[t],
                           bad);
        }
        span = (last > span) ? last : span;
    }
    for (int t = 0; t < SND_TRACKS; t++) {
        int na = 0, nb = 0;
        const char *bad_a = NULL, *bad_b = NULL;
        Event *a = played(song, t, 3 * span, &na, &bad_a);
        Event *b = played(tracked, t, 3 * span, &nb, &bad_b);
        int k = 0;
        while (k < na && k < nb && same_event(&a[k], &b[k])) {
            k++;
        }
        bool same = k == na && k == nb && bad_a == NULL && bad_b == NULL;
        int tick = (k < na) ? a[k].tick : (k < nb) ? b[k].tick : 0;
        free(a);
        free(b);
        if (!same) {
            return ex_fail(x, "%s: channel %s plays otherwise from tick %d",
                           name, EX_SND_CHANNELS[t], tick);
        }
    }
    return true;
}
