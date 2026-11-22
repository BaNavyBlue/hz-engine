#include "audio.h"

#include "psg.h"
#include "../audio/hz_snd.h"
#include "../audio/ym3438.h"

#ifdef HZ_HAVE_Q
#include "../audio/q/q_synth.h"
#endif

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define YM_QUEUE   4096        /* a song start is ~200 writes, 2 samples
                                  each to go out */
#define YM_CLAIM   0xF8        /* (session 38) not a write: effect track
                                   reg (the bank's 6-11) takes its FM
                                   channel (HzSndTrackWrite's claim) */
#define YM_GHOST1  0xF9        /* (session 38) a write the chip never gets
                                   (a muted music track's, part II): reg,
                                   value, for VOICES EXTRA */
#define YM_GHOST0  0xFA        /* ... part I */
#define YM_TRACK   0xFB        /* (session 38) not a write: the writes
                                   after it are the bank's track reg
                                   (0-5 music, 6-11 effects, BANK_DRIVER
                                   the driver's) */
#define YM_DRUMDB  0xFC        /* (session 38) not a write: the next
                                   strike on channel reg is value's level
                                   on its model's (hz_audio_drum_code_db);
                                   YM_DRUMHZ and YM_DRUM follow */
#define YM_DRUMHZ  0xFD        /* (session 37) not a write: the next
                                   strike on channel reg is pitched to
                                   value's tone (hz_audio_drum_code_hz;
                                   0: the model's own); the YM_DRUM entry
                                   follows */
#define YM_DRUM    0xFE        /* (session 37) a queue entry's part: not a
                                   write; the next key-on on channel reg
                                   strikes Q's drum model value - 1 (0: the
                                   PM voice) */
#define YM_MARKER  0xFF        /* a queue entry's part: not a write, the
                                  FM channels the effects have from here
                                  (the mask in `reg`) */
#define CMD_RING   256

/* (Session 38) The tracks as Q's bank numbers them (q_ps2.h's SMPS_*):
 * the six FM music tracks 0-5, the six FM effect tracks 6-11 (each on its
 * own channel, the track number's), the driver's own writes BANK_DRIVER. */
#define BANK_TRACKS 12
#define BANK_DRIVER 0xFF

/* PS2's mix (src/audio/mix.h there): the music's gain for the YM, the
 * master and the soft knee at -3 dBFS. PS2's music never uses the PSG, so
 * its PSG gain (0.9, the effects') was never set against the YM; HZ's
 * songs use both. Measured here, one YM channel at full scale (algorithm
 * 7, a carrier at TL 0) peaks at 0.102 before the gains, a PSG channel at
 * full volume at 0.25 (psg_render's square); the PSG's gain puts that
 * square's peak at half the YM channel's (about -3 dB in RMS against the
 * sine), a judgement of the hardware's balance -- the ear is the check.
 * At 0.9 the PSG stood 10 dB over a YM channel and the noise effects ran
 * into the knee. */
#define MIX_FM_GAIN     0.7f
#define MIX_PSG_GAIN    (0.102f * MIX_FM_GAIN * 0.5f / 0.25f)
#define MIX_MASTER_GAIN 4.0f    /* PS2's 3.2 + 2 dB: the songs peak at
                                   -6 to -11 dBFS as PS2's do, an effect
                                   over a song near -4 */
#define MIX_KNEE        0.70794578f

typedef struct {
    u8 part;                   /* 0, 1, or YM_MARKER */
    u8 reg;
    u8 value;
} YmWrite;

/* Which FM channel each group of four Nuked-OPN2 clocks outputs, by the
 * chip's cycle counter / 4 (OPN2_ChOutput: channel = cycles % 6, plus 1
 * before cycle 12, latched every fourth cycle). tools/hz_ymchan checks it
 * against the chip. */
const u8 HZ_AUDIO_CYCLE_CHANNEL[6] = { 1, 5, 3, 0, 4, 2 };

typedef struct {
    u8 bytes[6];
    u8 count;
} Command;

struct HzAudio {
    ym3438_t chip;
    Psg      psg;
    HzSnd    snd;
    u64      sample;           /* YM samples made so far */

    YmWrite  queue[YM_QUEUE];
    int      q_head, q_tail;
    bool     addr_sent;
    u32      dropped;

    float    dc_x1[2], dc_y1[2];
    double   step, phase;
    float    prev[2], next[2];

    Command      ring[CMD_RING];
    atomic_uint  ring_head;    /* the game thread's */
    atomic_uint  ring_tail;    /* the audio thread's */

    /* The AUDIO page's volumes (any thread) and the gains the audio
     * thread made of them at its last render. */
    atomic_int   master_db, music_db, effects_db;
    float        master_gain, music_gain, effects_gain;
    u8           fm_fx_queued; /* the effects' FM channels as last queued */
    /* The driver's z_music_on after the last render, plus $100 while
     * commands it has taken are still in its mailbox queue (the title's
     * Title_WaitStart asks whether its song is still playing). */
    atomic_uint  music_on;
    u8           fm_fx;        /* ... as the chip's output has reached */

    /* (Session 36) SYNTH. Nuked always runs; with Q the bank gets each
     * write as Nuked does (and the effects' channels), and its output
     * stands in for Nuked's FM. */
    atomic_int   synth_asked;
    int          synth;        /* the audio thread's */
    u8           shadow[2][256];   /* every register as last written */
    /* (Session 38) each FM channel's key-on as the chip has it ($28's
     * operator bits, 0: keyed off), and whether that note is a drum (the
     * drum marker before its key-on; an effect's key-on has none) */
    u8           keyed[6];
    u8           marked[6];        /* the drum marker waiting for a key-on */
    u8           keyed_drum[6];
    /* (Session 38) VOICES: Q voices the chip's six channels from its writes
     * (AUDIO_VOICES_CHIP), or each track on voices of its own from the
     * driver's track hook (AUDIO_VOICES_EXTRA: the music sounds on under
     * the effects). The tracks' writes are kept either way (the chip's
     * and the ones it never gets), so a switch can hand the bank each
     * track's registers and held note. */
    atomic_int   voices_asked;
    int          voices;           /* the audio thread's (the default:
                                      EXTRA, as the asked) */
    /* (Session 39) REPEATS: a note struck again on a sounding voice
     * starts fresh (PS2's) or from that voice's level (the chip's) */
    atomic_int   repeats_asked;
    int          repeats;          /* the audio thread's, -1 until told */
    /* (Session 48) OVERSAMPLE: the bank's operators at 1, 2 or 4 times
     * the chip's rate */
    atomic_int   oversample_asked;
    int          oversample;       /* the audio thread's, -1 until told */
    u8           writer_queued;    /* the track of the queue's last writes */
    u8           writer;           /* ... as the chip has reached them */
    u8           track_reg[BANK_TRACKS][2][256];
    bool         track_seen[BANK_TRACKS];
    u8           track_keyed[BANK_TRACKS];
    bool         track_drum[BANK_TRACKS];   /* its keyed note a drum */
    u16          music_fnum[6];    /* block | F-number each music track last
                                      wrote, the chip's or not (driver
                                      time: the drums' note lookup) */
    /* (Session 37) Q's drum models and which song's instrument, at which
     * pitches, plays each (hz_audio_set_drums) */
    HzDrumUse    drum_use[HZ_DRUM_USES];
    int          drum_uses;
    float        voice_db;         /* (session 41) Q's music voices' dB */
    int          drum_models;
#ifdef HZ_HAVE_Q
    QBank       *q;
#endif
};

/* The FM channels an effect has (z_sfx_on b0-5). */
static u8 fm_effects(const HzAudio *a)
{
    return (u8)(a->snd.sfx_on & 0x3F);
}

/* A queue entry, if there's room (false: none). */
static bool enqueue(HzAudio *a, u8 part, u8 reg, u8 value)
{
    int next = (a->q_head + 1) % YM_QUEUE;
    if (next == a->q_tail) {
        if (part == YM_MARKER) {
            a->fm_fx = reg;     /* no room: take it now */
        } else if (part >= YM_CLAIM) {
            /* no room: the strike plays as the PM voice; the track's
             * write is lost to EXTRA, its YM_TRACK sent again */
        } else {
            a->dropped++;
        }
        return false;
    }
    a->queue[a->q_head].part = part;
    a->queue[a->q_head].reg = reg;
    a->queue[a->q_head].value = value;
    a->q_head = next;
    return true;
}

/* (Session 38) HzSndTrackWrite's track as the bank's (BANK_TRACKS). */
static u8 bank_track(u8 track)
{
    if (track < 6) {
        return track;
    }
    if (track >= SND_WRITER_SFX && track < SND_WRITER_SFX + 6) {
        return (u8)(6 + track - SND_WRITER_SFX);
    }
    return BANK_DRIVER;
}

#ifdef HZ_HAVE_Q
/* A bank track's channel as $28 codes it (none for the driver's). */
static u8 bank_channel_code(u8 id)
{
    if (id >= BANK_TRACKS) {
        return 0x80;
    }
    int ch = id % 6;
    return (u8)((ch < 3) ? ch : ch + 1);
}
#endif

/* When the effects' FM channels have changed since the last marker, a
 * marker: the writes after it are made with the channels changed. */
static void mark_fm_effects(HzAudio *a)
{
    u8 mask = fm_effects(a);
    if (mask != a->fm_fx_queued) {
        a->fm_fx_queued = mask;
        enqueue(a, YM_MARKER, mask, 0);
    }
}

/* The driver's writes (hz_snd's sink): YM writes into the queue, PSG bytes
 * straight to the PSG (it takes them as they come). */
static void sink(void *user, u64 t, u8 part, u8 reg, u8 value)
{
    HzAudio *a = user;

    (void)t;
    if (part == SND_PART_PSG) {
        psg_write(&a->psg, value);
        return;
    }
    mark_fm_effects(a);
    enqueue(a, part, reg, value);
}

/* (Session 38) The driver's track hook (HzSndTrackWrite): whose the writes
 * are (a YM_TRACK marker when that changes), the writes the chip never
 * gets (YM_GHOST0/1) and the effects' claims (YM_CLAIM), in the queue in
 * the chip's order; and each music track's own pitch, for the drums. */
static void track_write(void *user, u64 t, u8 track, u8 part, u8 reg,
                        u8 value, bool to_chip)
{
    HzAudio *a = user;
    u8 id = bank_track(track);

    (void)t;
    if (part == SND_PART_CLAIM) {
        if (id >= 6 && id < BANK_TRACKS) {
            enqueue(a, YM_CLAIM, id, 0);
        }
        return;
    }
    if (id < 6 && reg >= 0xA0 && reg < 0xA8 && (reg & 3) != 3) {
        int ch = (reg & 3) + (part ? 3 : 0);
        u16 *f = &a->music_fnum[ch];
        *f = (reg >= 0xA4) ? (u16)((*f & 0x00FF) | (value << 8))
                           : (u16)((*f & 0xFF00) | value);
    }
    if (id != a->writer_queued) {
        if (!enqueue(a, YM_TRACK, id, 0)) {
            return;
        }
        a->writer_queued = id;
    }
    if (!to_chip) {
        mark_fm_effects(a);
        enqueue(a, part ? YM_GHOST1 : YM_GHOST0, reg, value);
    }
}

/* (Session 37) The driver's note hook (HzSndNote): before each music
 * key-on on an FM channel, a drum marker for it in the queue, so Q's next
 * strike there is the drum model the table gives the song's instrument,
 * or the PM voice. With no models, nothing. */
static void note(void *user, u64 t, u8 channel, u8 song, u8 instrument,
                 u16 position)
{
    HzAudio *a = user;

    (void)t;
    if (a->drum_models == 0 || channel > 5) {
        return;
    }
    u16 f = a->music_fnum[channel];
    float hz = (float)(f & 0x7FF) * (7670442.0f / 144.0f)
             / (float)(1u << (21 - ((f >> 11) & 7)));
    u8 at = 0;
    float db = 0.0f;
    int model = hz_audio_drum_lookup(a->drum_use, a->drum_uses, a->voice_db,
                                     song, instrument, hz, position, &at,
                                     &db);
    mark_fm_effects(a);
    enqueue(a, YM_DRUMDB, channel, hz_audio_drum_db_code(db));
    enqueue(a, YM_DRUMHZ, channel, at);
    enqueue(a, YM_DRUM, channel, (u8)model);
}

int hz_audio_drum_lookup(const HzDrumUse *uses, int count, float voice_db,
                         u8 song, u8 instrument, float hz, u16 position,
                         u8 *at, float *db)
{
    *at = 0;
    *db = voice_db;                             /* no entry: the PM voice */
    for (int i = 0; i < count; i++) {
        const HzDrumUse *u = &uses[i];
        if (u->song == song && u->instrument == instrument
            && hz >= u->min_hz && hz <= u->max_hz
            && (u->from < 0 || (position >= u->from && position <= u->to))) {
            if (u->model == HZ_DRUM_VOICE) {
                *db = voice_db + u->level_db;   /* (session 41) */
                return 0;
            }
            if (u->pitch > 0.0f) {
                *at = hz_audio_drum_hz_code(hz * u->pitch);
            }
            *db = u->level_db;
            return u->model + 1;
        }
    }
    return 0;
}

void hz_audio_set_voice_db(HzAudio *a, float voice_db)
{
    a->voice_db = voice_db;
}

u8 hz_audio_drum_hz_code(float hz)
{
    if (!(hz > 0.0f)) {
        return 0;
    }
    float q = 48.0f * log2f(hz / HZ_DRUM_HZ_BASE) + 1.0f;
    q = (q < 1.0f) ? 1.0f : (q > 255.0f ? 255.0f : q);
    return (u8)(q + 0.5f);
}

u8 hz_audio_drum_db_code(float db)
{
    float c = db * 2.0f + 128.0f;
    c = (c < 0.0f) ? 0.0f : (c > 255.0f ? 255.0f : c);
    return (u8)(c + 0.5f);
}

float hz_audio_drum_code_db(u8 code)
{
    return ((float)code - 128.0f) * 0.5f;
}

float hz_audio_drum_code_hz(u8 code)
{
    if (code == 0) {
        return 0.0f;
    }
    return HZ_DRUM_HZ_BASE * exp2f((float)(code - 1) / 48.0f);
}

HzAudio *hz_audio_create(const u8 *sound_data, int out_rate)
{
    HzAudio *a = calloc(1, sizeof *a);

    if (a == NULL) {
        return NULL;
    }
    int native = (int)(SND_MASTER / SND_YM_SAMPLE);

    OPN2_SetChipType(ym3438_mode_ym2612);
    OPN2_Reset(&a->chip);
    psg_init(&a->psg, native);
    a->step = (double)SND_MASTER / SND_YM_SAMPLE / (double)out_rate;
    a->phase = 1.0;                /* the first frame is a real sample */
    atomic_init(&a->ring_head, 0);
    atomic_init(&a->ring_tail, 0);
    atomic_init(&a->master_db, 0);
    atomic_init(&a->music_db, 0);
    atomic_init(&a->effects_db, 0);
    atomic_init(&a->music_on, 0);
    a->master_gain = 1.0f;
    a->music_gain = 1.0f;
    a->effects_gain = 1.0f;
    a->voices = AUDIO_VOICES_EXTRA;
    a->writer = BANK_DRIVER;           /* Init's writes: the driver's */
    a->writer_queued = BANK_DRIVER;
    hz_snd_init(&a->snd, sound_data, sink, a);
    a->snd.note = note;
    a->snd.track_write = track_write;
    atomic_init(&a->synth_asked, AUDIO_SYNTH_NUKED);
    atomic_init(&a->voices_asked, AUDIO_VOICES_EXTRA);
    atomic_init(&a->repeats_asked, -1);   /* -1: the bank's own (FRESH, or
                                             PS2_Q_PARALLEL=0's CHIP) until
                                             hz_audio_set_repeats */
    a->repeats = -1;
    atomic_init(&a->oversample_asked, -1);   /* -1: the bank's own (4, or
                                                PS2_Q_OVERSAMPLE's) until
                                                hz_audio_set_oversample */
    a->oversample = -1;
#ifdef HZ_HAVE_Q
    /* (Session 48) at the chip's rate, a frame each YM sample beside
     * Nuked's, resampled with it (native_sample): at OVERSAMPLE 1 the
     * partials heavy modulation throws past the rate fold where the
     * chip's do. Until session 48 it ran at out_rate. */
    a->q = q_bank_create(native);
#endif
    return a;
}

void hz_audio_set_song_windows(HzAudio *a, const u8 *windows, int count)
{
    hz_snd_set_song_windows(&a->snd, windows, count);
}

void hz_audio_destroy(HzAudio *a)
{
#ifdef HZ_HAVE_Q
    if (a->q != NULL) {
        q_bank_destroy(a->q);
    }
#endif
    free(a);
}

bool hz_audio_has_q(void)
{
#ifdef HZ_HAVE_Q
    return true;
#else
    return false;
#endif
}

void hz_audio_set_synth(HzAudio *a, int synth)
{
    atomic_store(&a->synth_asked, synth);
}

void hz_audio_set_voices(HzAudio *a, int voices)
{
    atomic_store(&a->voices_asked, voices);
}

void hz_audio_set_repeats(HzAudio *a, int repeats)
{
    atomic_store(&a->repeats_asked, repeats);
}

void hz_audio_set_oversample(HzAudio *a, int times)
{
    atomic_store(&a->oversample_asked, times);
}

void hz_audio_set_drums(HzAudio *a, const HzDrumModel *models, int count,
                        const HzDrumUse *uses, int use_count)
{
    a->drum_uses = 0;
    for (int i = 0; i < use_count && i < HZ_DRUM_USES; i++) {
        if (uses[i].model < count || uses[i].model == HZ_DRUM_VOICE) {
            a->drum_use[a->drum_uses++] = uses[i];
        }
    }
    a->drum_models = count;
#ifdef HZ_HAVE_Q
    if (a->q != NULL) {
        QDrumModel q[HZ_DRUM_MODELS];
        int n = count < HZ_DRUM_MODELS ? count : HZ_DRUM_MODELS;
        for (int i = 0; i < n; i++) {
            const HzDrumModel *m = &models[i];
            q[i] = (QDrumModel){
                m->tone_from_hz, m->tone_to_hz, m->glide_s, m->tone_level,
                m->tone_decay_s, m->noise_level, m->noise_decay_s,
                m->noise_hz, m->noise_q, m->noise_highpass, m->click_level,
                m->click_ms, m->click_hz, m->clip, m->fall_hz_s, m->layer,
                m->transpose, m->decay_scale, m->glide_depth, m->fall_scale,
                m->overtones_db, m->attack_db, m->ps2, m->level_db, m->tl_ref,
                m->wire_level, m->wire_decay_s, m->wire_hz, m->choke,
                m->with_model, m->with_db, m->cut, m->follow,
                m->follow_floor_hz
            };
        }
        q_bank_set_drum_models(a->q, q, n);
    }
#else
    (void)models;
#endif
}

/* (Session 38) A track's write as the chip's output reaches it (the chip's
 * or a ghost): kept per track, its key-on and whether that's a drum (the
 * music's drum marker before it), and for the chip's writes the channel's
 * key-on too; with Q on EXTRA, to the bank as the track's. */
static void track_reached(HzAudio *a, u8 part, u8 reg, u8 value, bool to_chip)
{
    u8 id = a->writer;
    bool drum = false;

    if (part == 0 && reg == 0x28 && (value & 3) != 3) {
        int ch = (value & 3) + ((value & 4) ? 3 : 0);
        u8 on = (u8)(value & 0xF0);
        if (on != 0 && id == ch) {
            drum = a->marked[ch] != 0;      /* a music key-on takes it */
            a->marked[ch] = 0;
        }
        if (id < BANK_TRACKS) {
            a->track_keyed[id] = on;
            a->track_drum[id] = drum;
        } else if (on == 0) {
            a->track_keyed[ch] = 0;          /* the driver's key-off */
            a->track_keyed[6 + ch] = 0;
        }
        if (to_chip) {
            a->keyed[ch] = on;
            if (on != 0) {
                a->keyed_drum[ch] = drum;
            }
        }
    } else if (id < BANK_TRACKS) {
        a->track_reg[id][part & 1][reg] = value;
        a->track_seen[id] = true;
    }
#ifdef HZ_HAVE_Q
    if (a->synth == AUDIO_SYNTH_Q && a->voices == AUDIO_VOICES_EXTRA) {
        q_bank_track_write(a->q, (id < BANK_TRACKS) ? id : -1,
                           bank_channel_code(id), part, reg, value, to_chip);
    }
#endif
}

#ifdef HZ_HAVE_Q
/* (Session 38) VOICES EXTRA's take: each track that has written, its
 * registers to its own voice (an effect claiming its channel first; a
 * music track whose channel an effect has, as the chip never got them),
 * then its held note keyed again (not a drum's). */
static void take_tracks(HzAudio *a, bool keys)
{
    for (int id = 0; id < BANK_TRACKS; id++) {
        int ch = id % 6;
        bool fx = (a->fm_fx >> ch) & 1u;
        bool effect = id >= 6;
        if (!a->track_seen[id] || (effect && !fx)) {
            continue;
        }
        u8 code = bank_channel_code((u8)id);
        bool to_chip = effect || !fx;
        int part = ch / 3;
        if (!keys) {
            if (effect) {
                q_bank_track_write(a->q, id, code, 0, SMPS_HOOK_CLAIM, code,
                                   false);
            }
            for (int r = 0x30 + ch % 3; r < 0xB8; r += 4) {
                if (r < 0xA8 || r >= 0xB0) {
                    q_bank_track_write(a->q, id, code, part, (u8)r,
                                       a->track_reg[id][part][r], to_chip);
                }
            }
        } else if (a->track_keyed[id] != 0 && !a->track_drum[id]) {
            q_bank_track_write(a->q, id, code, 0, 0x28,
                               (u8)(a->track_keyed[id] | code), to_chip);
        }
    }
}
#endif

/* SYNTH (and, session 38, VOICES) asked for, taken at a render: to Q, the
 * bank reset and handed every register but the key-on ($28) as the chip
 * has them, and the effects' channels; on EXTRA each track's own first. */
static void take_synth(HzAudio *a)
{
#ifdef HZ_HAVE_Q
    /* (Session 39) REPEATS: no reset, the voices sounding go on; the next
     * strike on a sounding voice does it the new way */
    int repeats = atomic_load(&a->repeats_asked);
    if (a->q != NULL && repeats >= 0 && repeats != a->repeats) {
        q_bank_set_parallel(a->q, repeats != AUDIO_REPEATS_CHIP);
        a->repeats = repeats;
    }
    /* (Session 48) OVERSAMPLE: no reset either; the bank works each
     * voice's steps out again at the new rate (PS2's 111a) */
    int os = atomic_load(&a->oversample_asked);
    if (a->q != NULL && os > 0 && os != a->oversample) {
        q_bank_set_oversample(a->q, os);
        a->oversample = os;
    }
#endif
    int want = atomic_load(&a->synth_asked);
    int voices = atomic_load(&a->voices_asked);
#ifdef HZ_HAVE_Q
    if (a->q == NULL) {
        want = AUDIO_SYNTH_NUKED;
    }
#else
    want = AUDIO_SYNTH_NUKED;
#endif
    if (want == a->synth && (want != AUDIO_SYNTH_Q || voices == a->voices)) {
        a->voices = voices;
        return;
    }
    a->synth = want;
    a->voices = voices;
#ifdef HZ_HAVE_Q
    if (want == AUDIO_SYNTH_Q) {
        bool extra = voices == AUDIO_VOICES_EXTRA;
        q_bank_reset(a->q);
        if (extra) {
            take_tracks(a, false);
        }
        for (int part = 0; part < 2; part++) {
            for (int r = 0x22; r < 0xB8; r++) {
                if (r != 0x28) {
                    q_bank_write(a->q, part, (u8)r, a->shadow[part][r]);
                }
            }
        }
        /* (Session 38) the notes held, keyed again: else a held note or
         * chord (some last 8-16 s) stays silent in Q until its channel's
         * next note. Struck from the start of its envelope, as a fresh
         * note. A drum held keyed isn't struck again (a second hit). */
        if (extra) {
            take_tracks(a, true);
            return;
        }
        q_bank_set_channel_effects(a->q, a->fm_fx);
        for (int ch = 0; ch < 6; ch++) {
            if (a->keyed[ch] != 0 && !a->keyed_drum[ch]) {
                u8 code = (u8)((ch < 3) ? ch : ch + 1);
                q_bank_write(a->q, 0, 0x28, (u8)(a->keyed[ch] | code));
            }
        }
    }
#endif
}

bool hz_audio_post(HzAudio *a, const u8 *bytes, int count)
{
    unsigned head = atomic_load_explicit(&a->ring_head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&a->ring_tail, memory_order_acquire);

    if (head - tail >= CMD_RING) {
        return false;
    }
    Command *c = &a->ring[head % CMD_RING];
    memset(c, 0, sizeof *c);
    for (int i = 0; i < count && i < 6; i++) {
        c->bytes[i] = bytes[i];
    }
    c->count = (u8)(count < 6 ? count : 6);
    atomic_store_explicit(&a->ring_head, head + 1, memory_order_release);
    return true;
}

u32 hz_audio_dropped(const HzAudio *a)
{
    return a->dropped;
}

void hz_audio_set_volume(HzAudio *a, int master_db, int music_db,
                         int effects_db)
{
    atomic_store(&a->master_db, master_db);
    atomic_store(&a->music_db, music_db);
    atomic_store(&a->effects_db, effects_db);
}

/* A volume as a gain: 0 dB exactly 1 (no pow: the default mix is the same
 * to the bit), off 0. */
static float volume_gain(int db)
{
    if (db <= AUDIO_DB_OFF) {
        return 0.0f;
    }
    if (db == 0) {
        return 1.0f;
    }
    return powf(10.0f, (float)db / 20.0f);
}

/* The ring's commands into the driver's mailbox queue, due now: the driver
 * takes them at its next step, in order (the 68k waits for the mailbox
 * between them, and the Z80 answers within microseconds). */
static void take_commands(HzAudio *a)
{
    unsigned tail = atomic_load_explicit(&a->ring_tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&a->ring_head, memory_order_acquire);

    while (tail != head && a->snd.q_count < SND_QUEUE) {
        const Command *c = &a->ring[tail % CMD_RING];
        hz_snd_post(&a->snd, a->snd.frame, c->bytes, c->count);
        tail++;
    }
    atomic_store_explicit(&a->ring_tail, tail, memory_order_release);
}

/* One YM sample, both sides, roughly -1..1 before the gains: Nuked's FM
 * and the PSG in out, Q's frame (session 48) in q_out. */
static void native_sample(HzAudio *a, float out[2], float q_out[2])
{
    hz_snd_run(&a->snd, a->sample * SND_YM_SAMPLE);
    a->sample++;
    mark_fm_effects(a);

    /* Each group of four clocks is one channel's output: summed to the
     * music's side or the effects'. */
    Bit16s buf[2];
    long music[2] = { 0, 0 };
    long effects[2] = { 0, 0 };
    for (int i = 0; i < 24; i++) {
        int ch = HZ_AUDIO_CYCLE_CHANNEL[a->chip.cycles >> 2];
        long *to = (a->fm_fx & (1u << ch)) ? effects : music;
        OPN2_Clock(&a->chip, buf);
        to[0] += buf[0];
        to[1] += buf[1];
    }
    while (!a->addr_sent && a->q_tail != a->q_head
           && a->queue[a->q_tail].part >= YM_CLAIM) {
        const YmWrite *m = &a->queue[a->q_tail];
        a->q_tail = (a->q_tail + 1) % YM_QUEUE;
        if (m->part == YM_TRACK) {
            a->writer = m->reg;
            continue;
        }
        if (m->part == YM_GHOST0 || m->part == YM_GHOST1) {
            track_reached(a, (u8)(m->part == YM_GHOST1), m->reg, m->value,
                          false);
            continue;
        }
        if (m->part == YM_CLAIM) {
#ifdef HZ_HAVE_Q
            if (a->synth == AUDIO_SYNTH_Q
                && a->voices == AUDIO_VOICES_EXTRA) {
                u8 code = bank_channel_code(m->reg);
                q_bank_track_write(a->q, m->reg, code, 0, SMPS_HOOK_CLAIM,
                                   code, false);
            }
#endif
            continue;
        }
        if (m->part == YM_DRUMDB) {
#ifdef HZ_HAVE_Q
            if (a->synth == AUDIO_SYNTH_Q) {
                q_bank_channel_drum_db(a->q, m->reg,
                                       hz_audio_drum_code_db(m->value));
            }
#endif
            continue;
        }
        if (m->part == YM_DRUMHZ) {
#ifdef HZ_HAVE_Q
            if (a->synth == AUDIO_SYNTH_Q) {
                q_bank_channel_drum_hz(a->q, m->reg,
                                       hz_audio_drum_code_hz(m->value));
            }
#endif
            continue;
        }
        if (m->part == YM_DRUM) {
            if (m->reg < 6) {
                a->marked[m->reg] = (u8)(m->value != 0);
            }
#ifdef HZ_HAVE_Q
            if (a->synth == AUDIO_SYNTH_Q) {
                q_bank_channel_drum(a->q, m->reg, (int)m->value - 1);
            }
#endif
            continue;
        }
        a->fm_fx = m->reg;
#ifdef HZ_HAVE_Q
        if (a->synth == AUDIO_SYNTH_Q && a->voices == AUDIO_VOICES_CHIP) {
            q_bank_set_channel_effects(a->q, a->fm_fx);
        }
#endif
    }
    if (a->q_tail != a->q_head) {
        const YmWrite *w = &a->queue[a->q_tail];
        Bit32u port = w->part ? 2u : 0u;

        if (!a->addr_sent) {
            OPN2_Write(&a->chip, port, w->reg);
            a->addr_sent = true;
        } else {
            OPN2_Write(&a->chip, port + 1, w->value);
            a->shadow[w->part & 1][w->reg] = w->value;
            track_reached(a, w->part, w->reg, w->value, true);
#ifdef HZ_HAVE_Q
            if (a->synth == AUDIO_SYNTH_Q) {
                q_bank_write(a->q, w->part, w->reg, w->value);
            }
#endif
            a->addr_sent = false;
            a->q_tail = (a->q_tail + 1) % YM_QUEUE;
        }
    }

    /* The PSG: tones 1-2 are music tracks 6-7's unless an effect has them
     * (z_sfx_on b6-7); tone 3 only ever sounds as the noise's pitch, so it
     * goes with the noise, the effects' while an effect's noise track is
     * on or still releasing (z_sfx8_on, its envelope state). */
    bool noise_fx = a->snd.sfx8_on != 0 || a->snd.sfx[8].env_state != 0;
    float g[PSG_CHANNELS];
    g[0] = MIX_PSG_GAIN * ((a->snd.sfx_on & 0x40) ? a->effects_gain
                                                   : a->music_gain);
    g[1] = MIX_PSG_GAIN * ((a->snd.sfx_on & 0x80) ? a->effects_gain
                                                   : a->music_gain);
    g[2] = MIX_PSG_GAIN * (noise_fx ? a->effects_gain : a->music_gain);
    g[3] = g[2];
    float p = 0.0f;
    psg_render_gains(&a->psg, &p, 1, g);
    /* Six channels' 14-bit outputs summed per side, scaled as PS2's
     * (with Q, the bank's output stands in for them). */
    float fm_on = (a->synth == AUDIO_SYNTH_NUKED) ? 1.0f : 0.0f;
    q_out[0] = q_out[1] = 0.0f;
#ifdef HZ_HAVE_Q
    if (a->synth == AUDIO_SYNTH_Q) {
        /* (Session 48) the bank's frame for this YM sample, after its
         * writes, at the chip's scale (its trim matches it to PS2's chip,
         * whose scale this mix shares) */
        q_bank_render_split(a->q, q_out, 1, MIX_FM_GAIN * a->music_gain,
                            MIX_FM_GAIN * a->effects_gain);
    }
#endif
    for (int c = 0; c < 2; c++) {
        out[c] = ((float)music[c] * a->music_gain
                  + (float)effects[c] * a->effects_gain)
               / 8192.0f * MIX_FM_GAIN * fm_on + p;
    }
}

/* One-pole high-pass (PS2's fm_dc_block). */
static float dc_block(HzAudio *a, int side, float x)
{
    const float r = 0.9995f;
    float y = x - a->dc_x1[side] + r * a->dc_y1[side];

    a->dc_x1[side] = x;
    a->dc_y1[side] = y;
    return y;
}

static float soft_limit(float x)
{
    float m = fabsf(x);

    if (m <= MIX_KNEE) {
        return x;
    }
    float room = 1.0f - MIX_KNEE;
    float y = MIX_KNEE + room * tanhf((m - MIX_KNEE) / room);
    return x < 0.0f ? -y : y;
}

void hz_audio_render(HzAudio *a, float *out, int frames)
{
    take_commands(a);
    take_synth(a);
    a->master_gain = volume_gain(atomic_load(&a->master_db));
    a->music_gain = volume_gain(atomic_load(&a->music_db));
    a->effects_gain = volume_gain(atomic_load(&a->effects_db));
    for (int i = 0; i < frames; i++) {
        while (a->phase >= 1.0) {
            float s[2], q[2];

            native_sample(a, s, q);
            /* (Session 48) Q beside the chip, resampled with it but not
             * through the DC block: the bank's output has no offset to
             * take, and as before its few Hz stay (REPEATS' check: LOCH's
             * song through it tracks the chip at r 0.82, past it 0.90) */
            for (int c = 0; c < 2; c++) {
                a->prev[c] = a->next[c];
                a->next[c] = dc_block(a, c, s[c]) + q[c];
            }
            a->phase -= 1.0;
        }
        float t = (float)a->phase;
        for (int c = 0; c < 2; c++) {
            float v = a->prev[c] + (a->next[c] - a->prev[c]) * t;
            out[2 * i + c] = soft_limit(v * MIX_MASTER_GAIN
                                        * a->master_gain);
        }
        a->phase += a->step;
    }
    atomic_store(&a->music_on,
                 a->snd.music_on | (a->snd.q_count != 0 ? 0x100u : 0u));
}

bool hz_audio_music_on(HzAudio *a)
{
    unsigned head = atomic_load_explicit(&a->ring_head, memory_order_acquire);
    unsigned tail = atomic_load_explicit(&a->ring_tail, memory_order_acquire);
    return head != tail || atomic_load(&a->music_on) != 0;
}
