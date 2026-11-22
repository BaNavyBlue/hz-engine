/* hz_audiolog LOG OUT.wav
 *
 * Plays a chip-write log (tools/z80run.py --log: lines `t part reg value`,
 * t in master clocks, part 0/1 the YM2612's halves or P the PSG) through
 * Nuked-OPN2 and the PSG, and writes a 16-bit stereo WAV at the YM's own
 * rate (master / 1008 = 53267 Hz), with each side's level printed per
 * second so a silent channel shows without listening.
 *
 * The YM side is PS2's fm.c reduced to the plain path: 24 OPN2_Clock calls
 * a sample, a write queue that puts out an address in one sample and its
 * data in the next (Nuked drops writes issued back to back). The PSG is
 * PS2's psg.c, rendered mono to both sides. The levels are rough (FM / 8192
 * per side, PSG at a fixed gain), as this is a listening and check tool,
 * not the port's mixer.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/audio/ym3438.h"
#include "../src/core/psg.h"

#define MASTER     53693175.0
#define YM_SAMPLE  1008
#define QUEUE      65536
#define PSG_GAIN   0.35f

typedef struct {
    long long t;
    int part;          /* 0, 1: YM halves; 2: PSG */
    int reg;
    int value;
} LogWrite;

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

static short clamp16(float x)
{
    float v = x * 32767.0f;

    if (v > 32767.0f) {
        return 32767;
    }
    if (v < -32768.0f) {
        return -32768;
    }
    return (short)v;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: hz_audiolog LOG OUT.wav\n");
        return 2;
    }
    FILE *in = fopen(argv[1], "r");
    if (!in) {
        perror(argv[1]);
        return 1;
    }
    size_t cap = 1024;
    size_t n = 0;
    LogWrite *w = malloc(cap * sizeof *w);
    char part[8];
    long long t;
    unsigned reg;
    unsigned value;

    while (w && fscanf(in, "%lld %7s %x %x", &t, part, &reg, &value) == 4) {
        if (n == cap) {
            cap *= 2;
            w = realloc(w, cap * sizeof *w);
            if (!w) {
                break;
            }
        }
        w[n].t = t;
        w[n].part = part[0] == 'P' ? 2 : part[0] - '0';
        w[n].reg = (int)reg;
        w[n].value = (int)value;
        n++;
    }
    fclose(in);
    if (!w || n == 0) {
        fprintf(stderr, "%s: no writes\n", argv[1]);
        return 1;
    }

    static ym3438_t chip;
    Psg psg;
    int rate = (int)(MASTER / YM_SAMPLE + 0.5);

    OPN2_SetChipType(ym3438_mode_ym2612);
    OPN2_Reset(&chip);
    psg_init(&psg, rate);

    long long samples = w[n - 1].t / YM_SAMPLE + rate;   /* + a second */
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        return 1;
    }
    unsigned long bytes = (unsigned long)samples * 4;
    fwrite("RIFF", 1, 4, out);
    put32(out, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, out);
    put32(out, 16);
    put16(out, 1);
    put16(out, 2);
    put32(out, (unsigned long)rate);
    put32(out, (unsigned long)rate * 4);
    put16(out, 4);
    put16(out, 16);
    fwrite("data", 1, 4, out);
    put32(out, bytes);

    static LogWrite queue[QUEUE];
    size_t q_head = 0;
    size_t q_tail = 0;
    int addr_sent = 0;
    size_t next = 0;
    double sum_l = 0.0;
    double sum_r = 0.0;
    int second = 0;

    for (long long s = 0; s < samples; s++) {
        long long now = s * YM_SAMPLE;

        while (next < n && w[next].t <= now) {
            if (w[next].part == 2) {
                psg_write(&psg, (u8)w[next].value);
            } else if ((q_head + 1) % QUEUE != q_tail) {
                queue[q_head] = w[next];
                q_head = (q_head + 1) % QUEUE;
            }
            next++;
        }

        Bit16s buf[2];
        long l = 0;
        long r = 0;

        for (int i = 0; i < 24; i++) {
            OPN2_Clock(&chip, buf);
            l += buf[0];
            r += buf[1];
        }
        if (q_tail != q_head) {
            const LogWrite *q = &queue[q_tail];
            Bit32u port = q->part ? 2u : 0u;

            if (!addr_sent) {
                OPN2_Write(&chip, port, (Bit8u)q->reg);
                addr_sent = 1;
            } else {
                OPN2_Write(&chip, port + 1, (Bit8u)q->value);
                addr_sent = 0;
                q_tail = (q_tail + 1) % QUEUE;
            }
        }

        float p = 0.0f;
        psg_render(&psg, &p, 1, PSG_GAIN);
        float fl = (float)l / 8192.0f + p;
        float fr = (float)r / 8192.0f + p;

        put16(out, (unsigned)(unsigned short)clamp16(fl));
        put16(out, (unsigned)(unsigned short)clamp16(fr));
        sum_l += (double)fl * fl;
        sum_r += (double)fr * fr;
        if ((s + 1) % rate == 0) {
            printf("%3d s  rms L %.4f R %.4f\n", second,
                   sqrt(sum_l / rate), sqrt(sum_r / rate));
            sum_l = 0.0;
            sum_r = 0.0;
            second++;
        }
    }
    fclose(out);
    free(w);
    return 0;
}
