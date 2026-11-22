/* (Session 78) The sound step's own parts (ex_sound.c, ex_songtext.c,
 * ex_tracker.c): SoundData read into tools/hzsongtext.py's model, which
 * the song text, its `# heard:` comments and the trackers
 * (tools/hztracker.py) are written from. Only format facts are here: the
 * data comes from the cartridge. */
#ifndef HZ_EX_SOUND_H
#define HZ_EX_SOUND_H

#include "ex.h"

#define SND_BASE        0x8000u   /* the bank window's first Z80 address */
#define SND_SIZE        0x8000u
#define SND_SONG_FIRST  9         /* a song's first byte (Cmd_PlaySong) */
#define SND_TRACKS      8         /* a song's: fm1-fm6, psg1, psg2 */
#define SND_FM_TRACKS   6
#define SND_NOISE       8         /* an effect's noise channel */
#define SND_NOTE_FIRST  0x80
#define SND_NOTE_END    0xE0
#define SND_NOTES       0x60      /* the driver's notes, C0-B7 */
#define SND_FM_SIZE     36
#define SND_PSG_SIZE    16
#define SND_LOOP        0xFD
#define SND_END         0xFF
#define SND_EFFECTS_AT  0xFA00u   /* the table Snd_SetSfxTable gives */

/* An item of an order list or a phrase, as hzsongtext's tuples: ('flag',
 * b, arg), ('note', b, params), ('phrase', b), ('byte', b); SND_MARK is
 * hztracker's ('loop',) among a cell's commands. */
enum { SND_FLAG, SND_NOTE, SND_PHRASE, SND_BYTE, SND_MARK };

typedef struct {
    u8   kind;
    u8   b;         /* the flag's or byte's value, the note, the phrase */
    u8   arg;       /* an ARG_FLAGS flag's value (0 for the others) */
    u8   np;        /* a note's numbers: length, gate, volume */
    u8   p[3];
    u16 *heard;     /* a note's pitches as heard, in the order reached */
    int  nheard, capheard;
} SndItem;

typedef struct {
    SndItem *v;
    int      n, cap;
} SndItems;

typedef struct {
    const u8   *data;               /* the window it was read from */
    u32         at;                 /* its Z80 address */
    u8          volume, tempo, transpose;
    u8          volumes[9];
    SndItems    tracks[SND_TRACKS];
    int         phrase_count;       /* the phrase table's entries */
    SndItems   *phrases;            /* phrase_count, those with has[k] */
    bool       *has;
    u32         fm_at, psg_at;      /* the instruments' Z80 addresses */
    int         nfm, npsg;
    const char *fm_names;           /* INSTRUMENT_NAMES' words, or NULL */
    const char *psg_names;
} SndSong;

typedef struct {
    u8       channel;
    SndItems flags;                 /* the commands before its phrase */
    int      phrase;                /* SndEffects.phrases' */
} SndFxTrack;

typedef struct {
    char        name[40];
    int         ntracks;
    SndFxTrack *tracks;
} SndFx;

typedef struct {
    u32      at;
    char     name[48];
    SndItems items;
} SndFxPhrase;

typedef struct {
    const u8    *data;              /* the window it was read from */
    u8           first_byte;
    int          count;
    SndFx       *fx;
    int          nphrases;          /* in their addresses' order */
    SndFxPhrase *phrases;
    u32          fm_at, psg_at;
    int          nfm, npsg;
    const char  *fm_names, *psg_names;
} SndEffects;

typedef struct {
    const u8   *data;               /* the window, SND_SIZE bytes */
    int         song_table;         /* its entries */
    int         nsongs;
    SndSong    *songs;
    u32         effects_at;
    SndEffects  fx;
    u32         tail_at;            /* the bytes after the effects */
    int         ntail;
} SndModel;

extern const char *const EX_SND_CHANNELS[9];

/* Room for n items in an array of `size`-byte ones (*cap grown); aborts
 * when out of memory, as ex_out.c's text buffers do. */
void *ex_snd_grow(void *v, int *cap, int n, size_t size);

/* ---- ex_songtext.c: hzsongtext.py ---- */

/* read_image: the window `data` into m (its pointers into data); false
 * (and a failure, "SoundData: ...") where the Python raises. */
bool ex_snd_read(HzEx *x, const u8 *data, SndModel *m);
/* read_song: the song at s of the window `data`; *size its size byte
 * (le(s + 14)). built: as songtext_build lays one out, which may put a 0
 * before the phrase table (song_bytes' keeping it off $FF). `what` starts
 * the failure's message. */
bool ex_snd_read_song(HzEx *x, const char *what, const u8 *data, u32 s,
                      bool built, SndSong *song, u32 *size);
void ex_snd_song_free(SndSong *song);
void ex_snd_free(SndModel *m);

/* The files write_text makes: sounds.txt (file[n]: song n's file,
 * "songs/NN_title.txt"), a song's (title NULL: none) and the effects',
 * each with annotate_text's `# heard:` comments. */
void ex_snd_sounds_text(ExBuf *b, const SndModel *m, char (*file)[128]);
void ex_snd_song_text(ExBuf *b, int n, SndSong *song, const char *title);
void ex_snd_effects_text(ExBuf *b, SndEffects *fx);

/* The writers the tracker shares: flag_text (names: the instruments'
 * words an `instrument` picks from, or NULL), note_name, instrument_text
 * (fm or psg; name NULL: none), and the k-th word of a names list (NULL
 * past its end or with no list). */
void ex_snd_flag_text(ExBuf *b, u8 byte, u8 arg, const char *names);
void ex_snd_note_name(ExBuf *b, int v);
void ex_snd_instrument_text(ExBuf *b, bool fm, int n, const u8 *rec,
                            const char *name);
const char *ex_snd_name(const char *names, int k, char out[32]);
/* USES: what plays song n ("the title"), or NULL. */
const char *ex_snd_use(int n);
/* Note_Add: a note and a transpose, kept under $60 as the driver's. */
int ex_snd_note_add(int note, int more);

/* ---- ex_tracker.c: hztracker.py ---- */

/* song_tracker_text, with instruments_read's instruments from the window
 * the song was read from: song n as a tracker (title NULL: none). False
 * (and a failure) where export raises. */
bool ex_snd_tracker_text(HzEx *x, ExBuf *b, int n, const SndSong *song,
                         const char *title);
/* check: whether `tracked` (the tracker's song, as built) plays as
 * `song` over three turns of it; a failure naming `name` if not. */
bool ex_snd_tracker_check(HzEx *x, const SndSong *song,
                          const SndSong *tracked, const char *name);

#endif /* HZ_EX_SOUND_H */
