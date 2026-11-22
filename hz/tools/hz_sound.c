/* hz_sound ASSETS [--song N] [--repeat] [--sfx F:N[,F:N...]] [--fade F:N]
 *                 [--stop F[,F...]] [--pause F[,F...]] [--seconds S]
 *                 [--cmd F:HEX[,F:HEX...]] [--bank FILE] [--text]
 *                 [--log OUT] [--wav OUT] [--volume MASTER,MUSIC,EFFECTS]
 *
 * The C port of the Z80 sound driver (src/audio/hz_snd.c) on the script
 * tools/z80run.py takes, with the same meaning: Snd_Init's $0A/$07/$08,
 * then the song ($09 N, $01 -1 with --repeat, as the map loader), sfx
 * ($02 N with its SfxPriority value from frame F), a fade ($03 N), stops
 * ($04), pauses ($0C); a command at a frame's start at most, in frame
 * order. --wav plays the script through the game's mixer (src/core/
 * audio.c: Nuked-OPN2, the PSG) instead, a frame's commands posted before
 * its audio, into a 16-bit stereo WAV at 48 kHz, with each second's RMS
 * and peak per side printed; --volume sets the Esc screen's AUDIO volumes
 * for it (whole dB, "off"; tools/hzmenu_check.py's split check). --cmd
 * posts raw mailbox bytes (hex, the
 * command first) at frame F; --bank plays another 32 KB bank instead of
 * the assets' SoundData (tools/hzsound_check.py's made-up songs); --text
 * builds the songs from the song text alone, the ROM's SoundData, not
 * from their trackers as the game does (which play the same: `make
 * roundtrip`). The log is z80run.py's: `t part reg value`, t in master
 * clocks, part 0/1 the YM's halves or P the PSG. `make sound` compares it
 * (with --text) with `z80run.py --zero-time`'s, which runs the original
 * driver the same way on the ROM.
 */
#include "../src/audio/hz_snd.h"
#include "../src/audio/songtext.h"
#include "../src/core/audio.h"
#include "../src/game/assets.h"

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRIPT_MAX 512

typedef struct {
    u32 frame;
    u8  bytes[6];
    int count;
} Step;

typedef struct {
    FILE *log;
    long  writes;
    long  psg;
} Sink;

static void sink(void *user, u64 t, u8 part, u8 reg, u8 value)
{
    Sink *k = user;

    k->writes++;
    if (part == SND_PART_PSG) {
        k->psg++;
    }
    if (k->log == NULL) {
        return;
    }
    if (part == SND_PART_PSG) {
        fprintf(k->log, "%llu P %02X %02X\n", (unsigned long long)t, reg,
                value);
    } else {
        fprintf(k->log, "%llu %u %02X %02X\n", (unsigned long long)t,
                (unsigned)part, reg, value);
    }
}

static Step script[SCRIPT_MAX];
static int  steps;

static void add(u32 frame, const u8 *bytes, int count)
{
    if (steps >= SCRIPT_MAX) {
        fprintf(stderr, "hz_sound: script too long\n");
        exit(2);
    }
    Step *s = &script[steps++];
    s->frame = frame;
    s->count = count;
    memcpy(s->bytes, bytes, (size_t)count);
}

/* "F:N,F:N" or "F,F": each item's numbers (base as strtol's 0). */
static int parse_items(const char *text, long out[][2], int max, int want)
{
    int n = 0;
    const char *p = text;

    while (*p != '\0' && n < max) {
        char *end;
        out[n][0] = strtol(p, &end, 0);
        out[n][1] = 0;
        if (want == 2) {
            if (*end != ':') {
                return -1;
            }
            out[n][1] = strtol(end + 1, &end, 0);
        }
        n++;
        if (*end == ',') {
            end++;
        } else if (*end != '\0') {
            return -1;
        }
        p = end;
    }
    return n;
}

/* z80run.py's script.sort: by frame, keeping the order within a frame. */
static void sort_script(void)
{
    for (int i = 1; i < steps; i++) {
        Step s = script[i];
        int j = i - 1;

        while (j >= 0 && script[j].frame > s.frame) {
            script[j + 1] = script[j];
            j--;
        }
        script[j + 1] = s;
    }
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE *f, unsigned long v)
{
    put16(f, (unsigned)(v & 0xFFFF));
    put16(f, (unsigned)((v >> 16) & 0xFFFF));
}

/* A --volume value: dB, or "off". */
static int volume_db(const char *s)
{
    return (strncmp(s, "off", 3) == 0) ? AUDIO_DB_OFF : atoi(s);
}

/* The script through the mixer, frame by frame, into a WAV. */
static int render_wav(const HzAssets *assets, const char *path,
                      double seconds, const int db[3])
{
    HzAudio *a = hz_audio_create(assets->sound_data, AUDIO_RATE);
    FILE *f = fopen(path, "wb");

    if (a == NULL || f == NULL) {
        perror(path);
        return 1;
    }
    hz_audio_set_song_windows(a, assets->song_windows,
                              assets->song_window_count);
    hz_audio_set_volume(a, db[0], db[1], db[2]);
    unsigned long total = (unsigned long)(seconds * AUDIO_RATE);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + total * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, AUDIO_RATE);
    put32(f, AUDIO_RATE * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, total * 4);

    static float buf[4096 * AUDIO_CHANNELS];
    unsigned long done = 0;
    int next = 0;
    double sum[2] = { 0.0, 0.0 };
    float peak[2] = { 0.0f, 0.0f };
    unsigned long in_second = 0;
    int second = 0;

    for (u32 frame = 0; done < total; frame++) {
        while (next < steps && script[next].frame <= frame) {
            hz_audio_post(a, script[next].bytes, script[next].count);
            next++;
        }
        /* this frame's share of samples: frames are 896,040 master clocks */
        unsigned long upto = (unsigned long)((double)(frame + 1) * SND_FRAME
                                             * AUDIO_RATE / SND_MASTER);
        if (upto > total) {
            upto = total;
        }
        while (done < upto) {
            int n = (int)(upto - done > 4096 ? 4096 : upto - done);
            hz_audio_render(a, buf, n);
            for (int i = 0; i < n * AUDIO_CHANNELS; i++) {
                float v = buf[i];
                int q = (int)lrintf(v * 32767.0f);
                q = q > 32767 ? 32767 : (q < -32768 ? -32768 : q);
                put16(f, (unsigned)(q & 0xFFFF));
                sum[i & 1] += (double)v * v;
                peak[i & 1] = fabsf(v) > peak[i & 1] ? fabsf(v) : peak[i & 1];
            }
            for (int i = 0; i < n; i++) {
                if (++in_second == AUDIO_RATE) {
                    printf("%3d s  rms L %.4f R %.4f  peak L %.3f R %.3f\n",
                           second, sqrt(sum[0] / AUDIO_RATE),
                           sqrt(sum[1] / AUDIO_RATE), peak[0], peak[1]);
                    sum[0] = sum[1] = 0.0;
                    peak[0] = peak[1] = 0.0f;
                    in_second = 0;
                    second++;
                }
            }
            done += (unsigned long)n;
        }
    }
    fclose(f);
    if (hz_audio_dropped(a) != 0) {
        printf("%u chip writes dropped (queue full)\n", hz_audio_dropped(a));
    }
    hz_audio_destroy(a);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: hz_sound ASSETS [--song N] [--repeat] "
                        "[--sfx F:N,...] [--fade F:N] [--stop F,...] "
                        "[--pause F,...] [--cmd F:HEX,...] [--bank FILE] "
                        "[--text] [--seconds S] [--log OUT] [--wav OUT] "
                        "[--volume MASTER,MUSIC,EFFECTS]\n");
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--text") == 0) {
            songtext_use_trackers(false);
        }
    }
    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    int song = -1;
    bool repeat = false;
    double seconds = 10.0;
    const char *log_path = NULL;
    const char *wav_path = NULL;
    int volume[3] = { 0, 0, 0 };
    long items[SCRIPT_MAX][2];

    static const u8 set_bank[5] = { 0x0A, 0x00, 0x07, 0x80, 0x00 };
    static const u8 set_song[3] = { 0x07, 0x80, 0x00 };
    static const u8 set_sfx[3] = { 0x08, 0xFA, 0x00 };
    add(0, set_bank, 5);
    add(0, set_song, 3);
    add(0, set_sfx, 3);

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;

        if (strcmp(a, "--repeat") == 0) {
            repeat = true;
            continue;
        }
        if (strcmp(a, "--text") == 0) {
            continue;
        }
        if (v == NULL) {
            fprintf(stderr, "hz_sound: %s needs a value\n", a);
            return 2;
        }
        i++;
        if (strcmp(a, "--song") == 0) {
            song = atoi(v);
        } else if (strcmp(a, "--seconds") == 0) {
            seconds = atof(v);
        } else if (strcmp(a, "--log") == 0) {
            log_path = v;
        } else if (strcmp(a, "--wav") == 0) {
            wav_path = v;
        } else if (strcmp(a, "--volume") == 0) {
            const char *p = v;
            for (int k = 0; k < 3; k++) {
                volume[k] = volume_db(p);
                const char *comma = strchr(p, ',');
                if (comma == NULL) {
                    break;
                }
                p = comma + 1;
            }
        } else if (strcmp(a, "--bank") == 0) {
            FILE *f = fopen(v, "rb");
            if (f == NULL
                || fread(assets.sound_data, 1, HZ_SOUND_DATA, f)
                   != HZ_SOUND_DATA) {
                fprintf(stderr, "hz_sound: %s: not a %d-byte bank\n", v,
                        HZ_SOUND_DATA);
                return 1;
            }
            fclose(f);
            assets.song_windows = NULL;   /* the bank holds its songs */
        } else if (strcmp(a, "--cmd") == 0) {
            const char *p = v;
            while (*p != '\0') {
                char *end;
                u8 b[6];
                int n = 0;
                long f = strtol(p, &end, 0);
                if (*end != ':') {
                    fprintf(stderr, "hz_sound: bad --cmd %s\n", v);
                    return 2;
                }
                p = end + 1;
                while (n < 6 && p[0] != '\0' && p[0] != ',' && p[1] != '\0') {
                    char hex[3] = { p[0], p[1], '\0' };
                    b[n++] = (u8)strtol(hex, NULL, 16);
                    p += 2;
                }
                if (n == 0) {
                    fprintf(stderr, "hz_sound: bad --cmd %s\n", v);
                    return 2;
                }
                add((u32)f, b, n);
                if (*p == ',') {
                    p++;
                }
            }
        } else if (strcmp(a, "--sfx") == 0 || strcmp(a, "--fade") == 0) {
            int n = parse_items(v, items, SCRIPT_MAX, 2);
            if (n < 0) {
                fprintf(stderr, "hz_sound: bad %s %s\n", a, v);
                return 2;
            }
            for (int k = 0; k < n; k++) {
                u8 b[3];
                if (a[2] == 's') {
                    b[0] = 0x02;
                    b[1] = (u8)items[k][1];
                    b[2] = assets.sfx_priority[b[1]];
                } else {
                    b[0] = 0x03;
                    b[1] = (u8)items[k][1];
                    b[2] = 0;
                }
                add((u32)items[k][0], b, 3);
            }
        } else if (strcmp(a, "--stop") == 0 || strcmp(a, "--pause") == 0) {
            int n = parse_items(v, items, SCRIPT_MAX, 1);
            if (n < 0) {
                fprintf(stderr, "hz_sound: bad %s %s\n", a, v);
                return 2;
            }
            for (int k = 0; k < n; k++) {
                u8 b[3] = { 0x04, 0, 0 };
                if (a[2] == 'p') {
                    b[0] = 0x0C;
                    add((u32)items[k][0], b, 3);
                } else {
                    add((u32)items[k][0], b, 1);
                }
            }
        } else {
            fprintf(stderr, "hz_sound: unknown option %s\n", a);
            return 2;
        }
    }
    if (song >= 0) {
        u8 play[3] = { 0x09, 0x00, (u8)song };
        add(1, play, 3);
        if (repeat) {
            u8 start[2] = { 0x01, 0xFF };
            add(1, start, 2);
        }
    }
    sort_script();
    if (wav_path != NULL) {
        /* Snd_Init's three first, then the rest by frame */
        return render_wav(&assets, wav_path, seconds, volume);
    }

    Sink k = { NULL, 0, 0 };
    if (log_path != NULL) {
        k.log = fopen(log_path, "w");
        if (k.log == NULL) {
            perror(log_path);
            return 1;
        }
    }
    static HzSnd snd;
    hz_snd_init(&snd, assets.sound_data, sink, &k);
    hz_snd_set_song_windows(&snd, assets.song_windows,
                            assets.song_window_count);
    snd.one_command_a_frame = true;
    /* The script goes into the mailbox queue a frame ahead of its time,
     * as far as the queue takes it: a command is taken at its frame's
     * VBlank, which must find it queued. */
    u64 end = (u64)(seconds * SND_MASTER);
    int next = 0;

    for (;;) {
        while (next < steps && snd.q_count < SND_QUEUE
               && script[next].frame <= snd.frame + 1) {
            hz_snd_post(&snd, script[next].frame, script[next].bytes,
                        script[next].count);
            next++;
        }
        /* z80run.py runs while its clock is below the end: an event at
         * the end itself doesn't happen. */
        if (snd.next_frame >= end) {
            hz_snd_run(&snd, end - 1);
            break;
        }
        hz_snd_run(&snd, snd.next_frame);
    }
    printf("%ld chip writes (%ld YM, %ld PSG), music bits %02X\n", k.writes,
           k.writes - k.psg, k.psg, snd.music_on);
    if (k.log != NULL) {
        fclose(k.log);
    }
    return 0;
}
