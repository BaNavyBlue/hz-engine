/* hz_qdrums: HZ's FM drums, for the Q backend's drum models (session 37).
 *
 *     hz_qdrums ASSETS survey [SECONDS]
 *
 * survey: every song (music ids 0-31 the driver plays) for SECONDS (60) of
 * the driver alone, its FM key-ons by instrument: on which channels, how
 * many, the notes' lengths (key-on to key-off), their pitches (the block
 * and F-number written), and whether a pitch slide or the LFO was on --
 * the drums are the instruments struck short, on fixed pitches, often.
 *
 *     hz_qdrums ASSETS strike SONG INST[,INST...] SECONDS OUT.wav OUT.txt
 *
 * strike: the song through the driver and Nuked-OPN2 as the mixer drives
 * it, every key-on but INST's dropped, so those instruments alone sound
 * (hex numbers); with Q, OUT.wav.q.wav too, at the chip's scale;
 * OUT.wav (mono, the chip's native 53,267 Hz, 16-bit) and OUT.txt, a line
 * per strike: its sample, its channel, its block and F-number.
 *
 *     hz_qdrums ASSETS song SONG SECONDS nuked|q OUT.wav
 *     hz_qdrums ASSETS song SONG,SONG@AT,... SECONDS nuked|q OUT.wav
 *     hz_qdrums ASSETS switch SONG AT SECONDS          (with Q)
 *     hz_qdrums ASSETS voices SONG SFX AT SECONDS      (with Q)
 *
 * song: the song alone through the game's mixer (session 45: or songs one
 * after another, each from AT seconds, as the music test switches them). switch: the song with Q
 * from the start against NUKED switched to Q at AT seconds, the levels
 * after the switch (session 38). voices: VOICES CHIP and EXTRA with effect
 * SFX (hex) at AT: the music under it against the music alone, and the
 * effect (session 38).
 *
 *     hz_qdrums ASSETS repeats SONG SECONDS            (with Q)
 *
 * repeats: the song through the game's mixer with NUKED, then Q with
 * REPEATS FRESH and CHIP (session 39), without the drum table (Q plays the
 * drum instruments' FM notes, as the chip): per 20 ms the level of each, and
 * against the chip's (where it is above -45 dBFS) the mean difference, the
 * share of frames 6 dB or more under it (a repeated note's dip), and the
 * envelopes' correlation. */

#include "../src/audio/hz_snd.h"
#include "../src/core/audio.h"
#include "../src/audio/ym3438.h"
#ifdef HZ_HAVE_Q
#include "../src/audio/q/q_synth.h"
#endif
#include "../src/game/assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int  count;
    int  channels;           /* a bit per FM channel */
    long gate_sum;           /* key-on to key-off, in YM samples */
    int  gates;
    long gate_min, gate_max;
    u16  fnum_min, fnum_max; /* (block << 11) | F-number */
    int  slides, lfos;
} Use;

typedef struct {
    HzSnd *snd;
    int    song;
    u64    on_at[6];
    int    on_inst[6];
    u16    fnum[6];
    Use    use[256];
} Survey;

static Survey g_sv;

/* (session 39) Cmd_StartSong's byte as the game sends it: the title's song
 * (0) and the music test's songs 11-16 play once (data/menu.json's
 * "repeat"); the rest repeat. A song repeated past its end goes on with
 * its transposition carried over (its order lists' $E8 flags add up), so
 * the once-played songs' second passes are notes the game never plays. */
static u8 song_repeat(int song)
{
    return (song == 0 || (song >= 11 && song <= 16)) ? 0x00 : 0xFF;
}

static void sink(void *user, u64 t, u8 part, u8 reg, u8 value)
{
    Survey *sv = user;
    if (part == SND_PART_PSG) {
        return;
    }
    if (reg >= 0xA0 && reg < 0xA8) {
        int ch = (reg & 3) + (part ? 3 : 0);
        if (ch > 5 || (reg & 3) == 3) {
            return;
        }
        if (reg >= 0xA4) {
            sv->fnum[ch] = (u16)((sv->fnum[ch] & 0x00FF) | (value << 8));
        } else {
            sv->fnum[ch] = (u16)((sv->fnum[ch] & 0xFF00) | value);
        }
        return;
    }
    if (part != 0 || reg != 0x28) {
        return;
    }
    int code = value & 7;
    int ch = (code & 3) + ((code & 4) ? 3 : 0);
    if ((code & 3) == 3) {
        return;
    }
    if (value & 0xF0) {                         /* key on */
        HzSndTrack *tr = &sv->snd->music[ch];
        int inst = tr->instrument;
        Use *u = &sv->use[inst];
        u->count++;
        u->channels |= 1 << ch;
        u16 f = sv->fnum[ch] & 0x3FFF;
        if (u->count == 1 || f < u->fnum_min) {
            u->fnum_min = f;
        }
        if (f > u->fnum_max) {
            u->fnum_max = f;
        }
        if (tr->slide_step != 0) {
            u->slides++;
        }
        if (tr->flags & 1) {
            u->lfos++;
        }
        sv->on_at[ch] = t;
        sv->on_inst[ch] = inst;
    } else if (sv->on_inst[ch] >= 0) {          /* key off */
        Use *u = &sv->use[sv->on_inst[ch]];
        long g = (long)((t - sv->on_at[ch]) / SND_YM_SAMPLE);
        if (u->gates == 0 || g < u->gate_min) {
            u->gate_min = g;
        }
        if (g > u->gate_max) {
            u->gate_max = g;
        }
        u->gate_sum += g;
        u->gates++;
        sv->on_inst[ch] = -1;
    }
}

static int survey(const HzAssets *a, int seconds)
{
    static HzSnd snd;
    for (int song = 0; song < 32; song++) {
        Survey *sv = &g_sv;
        memset(sv, 0, sizeof *sv);
        for (int c = 0; c < 6; c++) {
            sv->on_inst[c] = -1;
        }
        sv->snd = &snd;
        sv->song = song;
        hz_snd_init(&snd, a->sound_data, sink, sv);
        hz_snd_set_song_windows(&snd, a->song_windows,
                                a->song_window_count);
        u8 play[3] = { 0x09, 0x00, (u8)song };
        u8 start[2] = { 0x01, song_repeat(song) };
        hz_snd_post(&snd, 0, play, 3);
        hz_snd_post(&snd, 0, start, 2);
        hz_snd_run(&snd, (u64)SND_MASTER * (u64)seconds);
        int any = 0;
        for (int i = 0; i < 256; i++) {
            any += sv->use[i].count;
        }
        if (any == 0) {
            continue;
        }
        printf("song %d\n", song);
        for (int i = 0; i < 256; i++) {
            const Use *u = &sv->use[i];
            if (u->count == 0) {
                continue;
            }
            printf("  inst %02X  %4d notes  ch", i, u->count);
            for (int c = 0; c < 6; c++) {
                if (u->channels & (1 << c)) {
                    printf(" %d", c + 1);
                }
            }
            printf("  gate %ld-%ld avg %ld smp  fnum %04X-%04X  slides %d"
                   "  lfo %d\n", u->gates ? u->gate_min : -1L,
                   u->gates ? u->gate_max : -1L,
                   u->gates ? u->gate_sum / u->gates : -1L, u->fnum_min,
                   u->fnum_max, u->slides, u->lfos);
        }
    }
    return 0;
}

/* -- strike ---------------------------------------------------------------- */

typedef struct {
    u64 t;
    u8  part, reg, value;
} Write;

static Write g_w[1 << 22];
static u16   g_fnum[6];          /* as written, at the driver's time */
static int   g_nw;
static HzSnd *g_snd;
static u64   g_insts;        /* a bit per instrument kept (0-63) */

static void strike_sink(void *user, u64 t, u8 part, u8 reg, u8 value)
{
    (void)user;
    if (part == SND_PART_PSG || g_nw >= (int)(sizeof g_w / sizeof g_w[0])) {
        return;
    }
    if (reg >= 0xA0 && reg < 0xA8 && (reg & 3) != 3) {
        int c = (reg & 3) + (part ? 3 : 0);
        g_fnum[c] = (reg >= 0xA4) ? (u16)((g_fnum[c] & 0xFF) | (value << 8))
                                  : (u16)((g_fnum[c] & 0xFF00) | value);
    }
    if (part == 0 && reg == 0x28 && (value & 0xF0)) {
        int code = value & 7;
        int ch = (code & 3) + ((code & 4) ? 3 : 0);
        u8 inst = g_snd->music[ch].instrument;
        if ((code & 3) == 3 || inst >= 64 || !((g_insts >> inst) & 1)) {
            return;                             /* not INST: never keyed */
        }
    }
    g_w[g_nw++] = (Write){ t, part, reg, value };
}

/* the driver's note hook: a marker in the write list (part 0xFE), the
 * channel and the drum model + 1 the assets' table gives (0: none) */
static const HzAssets *g_assets;

/* as core/audio.c's note(): the first entry for the song's instrument
 * whose pitch range holds the note; markers 0xFD (the pitch, as
 * hz_audio_drum_hz_code has it) and 0xFE (the model + 1) */
static u16 g_pos[6];             /* each channel's last note's position */

static void strike_note(void *user, u64 t, u8 channel, u8 song, u8 inst,
                        u16 position)
{
    (void)user;
    if (channel > 5) {
        return;
    }
    g_pos[channel] = position;
    u16 f = g_fnum[channel];
    float hz = (float)(f & 0x7FF) * (7670442.0f / 144.0f)
             / (float)(1u << (21 - ((f >> 11) & 7)));
    u8 at = 0;
    float db = 0.0f;
    int m = hz_audio_drum_lookup(g_assets->drum_uses,
                                 g_assets->drum_use_count,
                                 g_assets->drum_voice_db, song, inst, hz,
                                 position, &at, &db);
    if (g_nw + 4 <= (int)(sizeof g_w / sizeof g_w[0])) {
        /* 0xFC: the position for strike's list (its t field holds the
         * high byte: this record isn't timed); 0xFB the entry's level
         * (hz_audio_drum_db_code) */
        g_w[g_nw++] = (Write){ (u64)(position >> 8), 0xFC, channel,
                               (u8)position };
        g_w[g_nw++] = (Write){ t, 0xFB, channel, hz_audio_drum_db_code(db) };
        g_w[g_nw++] = (Write){ t, 0xFD, channel, at };
        g_w[g_nw++] = (Write){ t, 0xFE, channel, (u8)m };
    }
}

static void put16le(FILE *f, u32 v, int n)
{
    for (int i = 0; i < n; i++) {
        fputc((int)((v >> (8 * i)) & 0xFF), f);
    }
}

static int strike(const HzAssets *a, int song, u64 insts, int seconds,
                  const char *wav, const char *txt)
{
    static HzSnd snd;
    static ym3438_t chip;
    g_snd = &snd;
    g_insts = insts;
    g_nw = 0;
    hz_snd_init(&snd, a->sound_data, strike_sink, NULL);
    hz_snd_set_song_windows(&snd, a->song_windows,
                            a->song_window_count);
    g_assets = a;
    snd.note = strike_note;
    u8 play[3] = { 0x09, 0x00, (u8)song };
    u8 start[2] = { 0x01, song_repeat(song) };
    hz_snd_post(&snd, 0, play, 3);
    hz_snd_post(&snd, 0, start, 2);
    hz_snd_run(&snd, (u64)SND_MASTER * (u64)seconds);

    long n = (long)((u64)SND_MASTER * (u64)seconds / SND_YM_SAMPLE);
    FILE *f = fopen(wav, "wb");
    FILE *k = fopen(txt, "w");
    if (f == NULL || k == NULL) {
        fprintf(stderr, "hz_qdrums: can't write %s or %s\n", wav, txt);
        return 1;
    }
    fwrite("RIFF", 1, 4, f);
    put16le(f, (u32)(36 + n * 2), 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put16le(f, 16, 4);
    put16le(f, 1, 2);
    put16le(f, 1, 2);
    put16le(f, 53267, 4);
    put16le(f, 53267 * 2, 4);
    put16le(f, 2, 2);
    put16le(f, 16, 2);
    fwrite("data", 1, 4, f);
    put16le(f, (u32)(n * 2), 4);

    OPN2_Reset(&chip);
    OPN2_SetChipType(ym3438_mode_ym2612);
    u16 fnum[6] = { 0 };
    int pos[6] = { 0 };
    u8  shadow[2][256] = { { 0 } };
    int next = 0;
    bool addr_sent = false;
    for (long i = 0; i < n; i++) {
        u64 now = (u64)i * SND_YM_SAMPLE;
        while (next < g_nw && g_w[next].part >= 0xFB && !addr_sent) {
            if (g_w[next].part == 0xFC) {       /* the note's position */
                pos[g_w[next].reg] = (int)g_w[next].value
                                   | ((int)g_w[next].t << 8);
            }
            next++;                             /* Q's alone */
        }
        if (next < g_nw && g_w[next].t <= now) {    /* as core/audio.c: an
                                                       address, then data */
            const Write *w = &g_w[next];
            u32 port = w->part ? 2u : 0u;
            if (!addr_sent) {
                OPN2_Write(&chip, port, w->reg);
                addr_sent = true;
            } else {
                OPN2_Write(&chip, port + 1, w->value);
                addr_sent = false;
                next++;
                shadow[w->part & 1][w->reg] = w->value;
                if (w->reg >= 0xA0 && w->reg < 0xA8 && (w->reg & 3) != 3) {
                    int ch = (w->reg & 3) + (w->part ? 3 : 0);
                    fnum[ch] = (w->reg >= 0xA4)
                             ? (u16)((fnum[ch] & 0xFF) | (w->value << 8))
                             : (u16)((fnum[ch] & 0xFF00) | w->value);
                }
                if (w->part == 0 && w->reg == 0x28 && (w->value & 0xF0)) {
                    int code = w->value & 7;
                    int ch = (code & 3) + ((code & 4) ? 3 : 0);
                    static const u8 car[8] = { 8, 8, 8, 8, 10, 14, 14, 15 };
                    static const u8 tlr[4] = { 0x40, 0x48, 0x44, 0x4C };
                    int part = ch >= 3, c3 = ch % 3;
                    int alg = shadow[part][0xB0 + c3] & 7, tl = 127;
                    for (int op = 0; op < 4; op++) {
                        int v = shadow[part][tlr[op] + c3] & 0x7F;
                        if ((car[alg] >> op & 1) && v < tl) {
                            tl = v;
                        }
                    }
                    fprintf(k, "%ld %d %04X %d %d\n", i, ch + 1,
                            fnum[ch] & 0x3FFF, tl, pos[ch]);
                }
            }
        }
        long sum = 0;
        Bit16s buf[2];
        for (int c = 0; c < 24; c++) {
            OPN2_Clock(&chip, buf);
            sum += buf[0] + buf[1];
        }
        long v = sum * 2;
        if (v > 32767) {
            v = 32767;
        }
        if (v < -32768) {
            v = -32768;
        }
        put16le(f, (u32)(u16)(s16)v, 2);
    }
    fclose(f);
    fclose(k);
#ifdef HZ_HAVE_Q
    /* The same writes through the Q bank at the chip's rate: OUT.wav's
     * name with .q.wav, mono, the bank's output at the chip's scale
     * (MIX_FM_GAIN 0.7 as the mix gives it, then x 32768 / 2.8, so its
     * numbers sit beside the chip's: 24 clocks' sum x 2 is 1/8192 of the
     * chip's scale x 0.7... measured, not derived: see the check). */
    char qwav[512];
    snprintf(qwav, sizeof qwav, "%s.q.wav", wav);
    FILE *g = fopen(qwav, "wb");
    QBank *b = q_bank_create(53267);
    if (g == NULL || b == NULL) {
        return 1;
    }
    {
        QDrumModel qm[HZ_DRUM_MODELS];
        for (int i = 0; i < a->drum_model_count; i++) {
            const HzDrumModel *m = &a->drum_models[i];
            qm[i] = (QDrumModel){
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
        if (a->drum_model_count > 0 && getenv("HZ_QDRUMS_OFF") == NULL) {
            q_bank_set_drum_models(b, qm, a->drum_model_count);
        }
    }
    fwrite("RIFF", 1, 4, g);
    put16le(g, (u32)(36 + n * 2), 4);
    fwrite("WAVEfmt ", 1, 8, g);
    put16le(g, 16, 4);
    put16le(g, 1, 2);
    put16le(g, 1, 2);
    put16le(g, 53267, 4);
    put16le(g, 53267 * 2, 4);
    put16le(g, 2, 2);
    put16le(g, 16, 2);
    fwrite("data", 1, 4, g);
    put16le(g, (u32)(n * 2), 4);
    next = 0;
    addr_sent = false;
    for (long i = 0; i < n; i++) {
        u64 now = (u64)i * SND_YM_SAMPLE;
        while (next < g_nw && g_w[next].part >= 0xFB && !addr_sent) {
            if (g_w[next].part == 0xFC) {
                next++;
                continue;
            }
            if (g_w[next].part == 0xFB) {
                q_bank_channel_drum_db(b, g_w[next].reg,
                                       hz_audio_drum_code_db(g_w[next].value));
                next++;
                continue;
            }
            if (g_w[next].part == 0xFD) {
                q_bank_channel_drum_hz(b, g_w[next].reg,
                                       hz_audio_drum_code_hz(g_w[next].value));
            } else {
                q_bank_channel_drum(b, g_w[next].reg,
                                    (int)g_w[next].value - 1);
            }
            next++;
        }
        if (next < g_nw && g_w[next].t <= now) {
            if (!addr_sent) {
                addr_sent = true;
            } else {
                q_bank_write(b, g_w[next].part, g_w[next].reg,
                             g_w[next].value);
                addr_sent = false;
                next++;
            }
        }
        float o[2] = { 0.0f, 0.0f };
        q_bank_render_split(b, o, 1, 1.0f, 1.0f);
        /* the chip's sample above is (L + R) x 2 of 14-bit outputs; the
         * mix scales the chip by / 8192 and the bank by 1: so x 8192 / 2
         * per side, summed, x 2 */
        long v = (long)((o[0] + o[1]) * 8192.0f * 2.0f);
        if (v > 32767) {
            v = 32767;
        }
        if (v < -32768) {
            v = -32768;
        }
        put16le(g, (u32)(u16)(s16)v, 2);
    }
    q_bank_destroy(b);
    fclose(g);
#endif
    return 0;
}

/* song SONG SECONDS SYNTH OUT.wav: the song alone (no effects) through the
 * game's own mixer (core/audio.c: the driver, the chip or Q with the
 * assets' drums, the PSG, the master gain and knee), stereo 48 kHz. SYNTH:
 * nuked or q. */
static int song(const HzAssets *a, const char *list, int seconds,
                const char *synth, const char *path)
{
    /* (session 45) SONG, or songs one after another as the music test
     * plays them: "16,17@20" plays song 16 at once and song 17 at 20 s */
    int  songs[16];
    long at[16];
    int  count = 0;
    for (const char *p = list; *p != '\0' && count < 16;) {
        char *end = NULL;
        songs[count] = (int)strtol(p, &end, 10);
        at[count] = 0;
        if (*end == '@') {
            at[count] = (long)(48000.0 * strtod(end + 1, &end));
        }
        count++;
        p = (*end == ',') ? end + 1 : end;
        if (end == p && *p != '\0') {
            break;
        }
    }
    HzAudio *au = hz_audio_create(a->sound_data, 48000);
    if (au == NULL) {
        return 1;
    }
    hz_audio_set_song_windows(au, a->song_windows,
                              a->song_window_count);
    hz_audio_set_drums(au, a->drum_models, a->drum_model_count,
                       a->drum_uses, a->drum_use_count);
    hz_audio_set_voice_db(au, a->drum_voice_db);
    hz_audio_set_synth(au, strcmp(synth, "q") == 0 ? AUDIO_SYNTH_Q
                                                   : AUDIO_SYNTH_NUKED);
    int next = 0;
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return 1;
    }
    long frames = 48000L * seconds;
    fwrite("RIFF", 1, 4, f);
    put16le(f, (u32)(36 + frames * 4), 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put16le(f, 16, 4);
    put16le(f, 1, 2);
    put16le(f, 2, 2);
    put16le(f, 48000, 4);
    put16le(f, 48000 * 4, 4);
    put16le(f, 4, 2);
    put16le(f, 16, 2);
    fwrite("data", 1, 4, f);
    put16le(f, (u32)(frames * 4), 4);
    static float buf[2 * 800];
    for (long done = 0; done < frames; done += 800) {
        int k = (frames - done < 800) ? (int)(frames - done) : 800;
        while (next < count && at[next] < done + k) {
            u8 play[3] = { 0x09, 0x00, (u8)songs[next] };
            u8 start[2] = { 0x01, song_repeat(songs[next]) };
            hz_audio_post(au, play, 3);
            hz_audio_post(au, start, 2);
            next++;
        }
        hz_audio_render(au, buf, k);
        for (int i = 0; i < 2 * k; i++) {
            float v = buf[i] * 32767.0f;
            v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
            put16le(f, (u32)(u16)(s16)v, 2);
        }
    }
    fclose(f);
    hz_audio_destroy(au);
    return 0;
}

#ifdef HZ_HAVE_Q
/* (session 38) switch SONG AT SECONDS: the song through the game's mixer
 * twice, with Q from the start and with NUKED switched to Q at AT seconds
 * (as the AUDIO page does); per 100 ms after the switch the level of each
 * (dB) and the difference, then the mean difference: notes held across the
 * switch should go on sounding, not wait for their channel's next note. */
static void switch_run(const HzAssets *a, int n, float at, float seconds,
                       float *db, int count)
{
    HzAudio *au = hz_audio_create(a->sound_data, 48000);
    hz_audio_set_song_windows(au, a->song_windows,
                              a->song_window_count);
    hz_audio_set_drums(au, a->drum_models, a->drum_model_count,
                       a->drum_uses, a->drum_use_count);
    hz_audio_set_voice_db(au, a->drum_voice_db);
    hz_audio_set_synth(au, at > 0.0f ? AUDIO_SYNTH_NUKED : AUDIO_SYNTH_Q);
    u8 play[3] = { 0x09, 0x00, (u8)n };
    u8 start[2] = { 0x01, song_repeat(n) };
    hz_audio_post(au, play, 3);
    hz_audio_post(au, start, 2);
    static float buf[2 * 480];
    long from = (long)(48000.0f * (at > 0.0f ? at : -at));
    long total = from + (long)(48000.0f * seconds);
    double sum = 0.0;
    for (long done = 0; done < total; done += 480) {
        if (at > 0.0f && done >= from && done - 480 < from) {
            hz_audio_set_synth(au, AUDIO_SYNTH_Q);
        }
        hz_audio_render(au, buf, 480);
        if (done >= from) {
            for (int i = 0; i < 960; i++) {
                sum += (double)buf[i] * buf[i];
            }
            long k = (done - from) / 480;
            if ((k + 1) % 10 == 0 && k / 10 < count) {
                db[k / 10] = (float)(10.0 * log10(sum / 9600.0 + 1e-12));
                sum = 0.0;
            }
        }
    }
    hz_audio_destroy(au);
}

static int switch_test(const HzAssets *a, int n, float at, float seconds)
{
    static float q[1000], s[1000];
    int count = (int)(seconds * 10.0f);
    count = count > 1000 ? 1000 : count;
    switch_run(a, n, -at, seconds, q, count);
    switch_run(a, n, at, seconds, s, count);
    double d = 0.0;
    for (int i = 0; i < count; i++) {
        printf("%5d ms  q %6.1f  switched %6.1f  %+5.1f\n", i * 100, q[i],
               s[i], s[i] - q[i]);
        d += s[i] - q[i];
    }
    printf("mean %+.1f dB\n", count > 0 ? d / count : 0.0);
    return 0;
}

/* (session 38) voices SONG SFX AT SECONDS: VOICES CHIP and EXTRA, the song
 * with Q and effect SFX posted at AT seconds, against the song alone: the
 * music's side (EFFECTS off) over SECONDS from AT, and the effect's (MUSIC
 * off), RMS dBFS. With CHIP the music's part on the effect's FM channel
 * stops under it, as on the chip; with EXTRA it plays on. */
static double voices_run(const HzAssets *a, int n, int sfx, float at,
                         float seconds, int voices, bool music)
{
    HzAudio *au = hz_audio_create(a->sound_data, 48000);
    hz_audio_set_song_windows(au, a->song_windows,
                              a->song_window_count);
    hz_audio_set_drums(au, a->drum_models, a->drum_model_count,
                       a->drum_uses, a->drum_use_count);
    hz_audio_set_voice_db(au, a->drum_voice_db);
    hz_audio_set_synth(au, AUDIO_SYNTH_Q);
    hz_audio_set_voices(au, voices);
    hz_audio_set_volume(au, 0, music ? 0 : AUDIO_DB_OFF,
                        music ? AUDIO_DB_OFF : 0);
    /* (session 50) the effects' table, as snd_init sets it: without it the
     * driver reads "effects" at its start-up $F000, song bytes */
    u8 table[3] = { SND_CMD_SET_SFX_TABLE, (u8)(a->sfx_table >> 8),
                    (u8)a->sfx_table };
    u8 play[3] = { 0x09, 0x00, (u8)n };
    u8 start[2] = { 0x01, song_repeat(n) };
    hz_audio_post(au, table, 3);
    hz_audio_post(au, play, 3);
    hz_audio_post(au, start, 2);
    static float buf[2 * 480];
    long from = (long)(48000.0f * at);
    long total = from + (long)(48000.0f * seconds);
    double sum = 0.0;
    long count = 0;
    for (long done = 0; done < total; done += 480) {
        if (sfx >= 0 && done >= from && done - 480 < from) {
            u8 c[3] = { 0x02, (u8)sfx, 0x00 };
            hz_audio_post(au, c, 3);
        }
        hz_audio_render(au, buf, 480);
        if (done >= from) {
            for (int i = 0; i < 960; i++) {
                sum += (double)buf[i] * buf[i];
            }
            count += 960;
        }
    }
    hz_audio_destroy(au);
    return 10.0 * log10(sum / (double)(count > 0 ? count : 1) + 1e-12);
}

static int voices_test(const HzAssets *a, int n, int sfx, float at,
                       float seconds)
{
    static const char *const NAME[2] = { "chip", "extra" };
    for (int v = AUDIO_VOICES_CHIP; v <= AUDIO_VOICES_EXTRA; v++) {
        double alone = voices_run(a, n, -1, at, seconds, v, true);
        double under = voices_run(a, n, sfx, at, seconds, v, true);
        double fx = voices_run(a, n, sfx, at, seconds, v, false);
        printf("%-5s music alone %6.1f dB, under the effect %6.1f (%+.1f); "
               "the effect %6.1f\n", NAME[v], alone, under, under - alone,
               fx);
    }
    return 0;
}

/* (session 39) repeats SONG SECONDS: the 20 ms levels (dB) of the song
 * with NUKED (repeats < 0) or Q with REPEATS repeats. */
static void repeats_run(const HzAssets *a, int n, int repeats, float *db,
                        int count)
{
    HzAudio *au = hz_audio_create(a->sound_data, 48000);
    hz_audio_set_song_windows(au, a->song_windows,
                              a->song_window_count);
    /* no drum table: Q's drum models aren't the chip's notes, and would
     * hide what REPEATS does to the FM's */
    (void)a->drum_models;
    hz_audio_set_synth(au, repeats < 0 ? AUDIO_SYNTH_NUKED : AUDIO_SYNTH_Q);
    if (repeats >= 0) {
        hz_audio_set_repeats(au, repeats);
    }
    u8 play[3] = { 0x09, 0x00, (u8)n };
    u8 start[2] = { 0x01, song_repeat(n) };
    hz_audio_post(au, play, 3);
    hz_audio_post(au, start, 2);
    static float buf[2 * 960];
    for (int k = 0; k < count; k++) {
        hz_audio_render(au, buf, 960);
        double sum = 0.0;
        for (int i = 0; i < 1920; i++) {
            sum += (double)buf[i] * buf[i];
        }
        db[k] = (float)(10.0 * log10(sum / 1920.0 + 1e-12));
    }
    hz_audio_destroy(au);
}

static int repeats_test(const HzAssets *a, int n, float seconds)
{
    static const char *const NAME[2] = { "fresh", "chip" };
    static float chip[30000], q[30000];
    int count = (int)(seconds * 50.0f);
    count = count > 30000 ? 30000 : count;
    repeats_run(a, n, -1, chip, count);
    for (int r = AUDIO_REPEATS_FRESH; r <= AUDIO_REPEATS_CHIP; r++) {
        repeats_run(a, n, r, q, count);
        double diff = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0;
        double sxy = 0.0;
        int used = 0, dips = 0;
        for (int k = 0; k < count; k++) {
            if (chip[k] < -45.0f) {
                continue;
            }
            double d = q[k] - chip[k];
            diff += d;
            dips += d <= -6.0;
            double x = pow(10.0, chip[k] / 20.0), y = pow(10.0, q[k] / 20.0);
            sx += x;
            sy += y;
            sxx += x * x;
            syy += y * y;
            sxy += x * y;
            used++;
        }
        double r_xy = 0.0;
        if (used > 1) {
            double vx = sxx - sx * sx / used, vy = syy - sy * sy / used;
            r_xy = (sxy - sx * sy / used) / sqrt(vx * vy + 1e-30);
        }
        printf("song %2d %-5s %+5.2f dB against the chip, %4.1f%% of frames "
               "6 dB under, r %.3f\n", n, NAME[r],
               used > 0 ? diff / used : 0.0,
               used > 0 ? 100.0 * dips / used : 0.0, r_xy);
    }
    return 0;
}

/* model OUT.wav TL and the model's 17 numbers (q_drums.json's order:
 * tone_from_hz tone_to_hz glide_s fall_hz_s tone_level tone_decay_s
 * noise_level noise_decay_s noise_hz noise_q noise_highpass click_level
 * click_ms click_hz clip level_db; tl_ref is 0): one strike on channel 1
 * at carrier TL TL, 0.3 s, as strike's .q.wav (mono, x 8192 a side). */
static int model(int argc, char **argv)
{
    if (argc != 22) {
        fprintf(stderr, "hz_qdrums: model OUT.wav TL and 17 numbers\n");
        return 2;
    }
    float v[17];
    for (int i = 0; i < 17; i++) {
        v[i] = (float)atof(argv[5 + i]);
    }
    QDrumModel m = { v[0], v[1], v[2], v[4], v[5], v[6], v[7], v[8], v[9],
                     v[10] != 0.0f, v[11], v[12], v[13], v[14] != 0.0f,
                     v[3], v[16] != 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                     0.0f, -1, v[15], 0, 0.0f, 0.0f, 5000.0f, false, -1, 0.0f,
                     false, 0.0f, 0.0f };
    QBank *b = q_bank_create(53267);
    q_bank_set_drum_models(b, &m, 1);
    int tl = atoi(argv[4]);
    q_bank_write(b, 0, 0xB0, 0x07);
    q_bank_write(b, 0, 0xB4, 0xC0);
    for (int op = 0; op < 4; op++) {
        q_bank_write(b, 0, (u8)(0x40 + 4 * op), (u8)tl);
    }
    q_bank_channel_drum(b, 0, 0);
    if (!m.layer) {
        q_bank_write(b, 0, 0x28, 0xF0);
    } else {
        fprintf(stderr, "hz_qdrums: model renders a model alone (layer 0)\n");
        return 2;
    }
    long n = (long)(0.3 * 53267);
    FILE *g = fopen(argv[3], "wb");
    if (g == NULL) {
        return 1;
    }
    fwrite("RIFF", 1, 4, g);
    put16le(g, (u32)(36 + n * 2), 4);
    fwrite("WAVEfmt ", 1, 8, g);
    put16le(g, 16, 4);
    put16le(g, 1, 2);
    put16le(g, 1, 2);
    put16le(g, 53267, 4);
    put16le(g, 53267 * 2, 4);
    put16le(g, 2, 2);
    put16le(g, 16, 2);
    fwrite("data", 1, 4, g);
    put16le(g, (u32)(n * 2), 4);
    for (long i = 0; i < n; i++) {
        float o[2] = { 0.0f, 0.0f };
        q_bank_render_split(b, o, 1, 1.0f, 1.0f);
        long x = (long)((o[0] + o[1]) * 8192.0f);
        x = x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
        put16le(g, (u32)(u16)(s16)x, 2);
    }
    fclose(g);
    q_bank_destroy(b);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hz_qdrums ASSETS survey [SECONDS]\n");
        return 2;
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    if (strcmp(argv[2], "survey") == 0) {
        return survey(&assets, argc > 3 ? atoi(argv[3]) : 60);
    }
    if (strcmp(argv[2], "song") == 0 && argc == 7) {
        return song(&assets, argv[3], atoi(argv[4]), argv[5], argv[6]);
    }
    if (strcmp(argv[2], "strike") == 0 && argc == 8) {
        u64 insts = 0;                  /* INST: hex numbers, comma between */
        for (char *p = argv[4]; *p != '\0';) {
            char *end = NULL;
            long v = strtol(p, &end, 16);
            if (end == p || v < 0 || v > 63) {
                fprintf(stderr, "hz_qdrums: INST is hex 0-3F, comma between\n");
                return 2;
            }
            insts |= 1ull << v;
            p = (*end == ',') ? end + 1 : end;
        }
        return strike(&assets, atoi(argv[3]), insts, atoi(argv[5]), argv[6],
                      argv[7]);
    }
#ifdef HZ_HAVE_Q
    if (strcmp(argv[2], "model") == 0) {
        return model(argc, argv);
    }
    if (strcmp(argv[2], "switch") == 0 && argc == 6) {
        return switch_test(&assets, atoi(argv[3]), (float)atof(argv[4]),
                           (float)atof(argv[5]));
    }
    if (strcmp(argv[2], "repeats") == 0 && argc == 5) {
        return repeats_test(&assets, atoi(argv[3]), (float)atof(argv[4]));
    }
    if (strcmp(argv[2], "voices") == 0 && argc == 7) {
        return voices_test(&assets, atoi(argv[3]),
                           (int)strtol(argv[4], NULL, 16),
                           (float)atof(argv[5]), (float)atof(argv[6]));
    }
#endif
    fprintf(stderr, "hz_qdrums: what is %s?\n", argv[2]);
    return 2;
}
