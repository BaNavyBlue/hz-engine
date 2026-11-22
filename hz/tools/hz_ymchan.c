/* hz_ymchan: src/core/audio.c's HZ_AUDIO_CYCLE_CHANNEL against
 * Nuked-OPN2 itself. For each FM channel in turn, on a fresh chip: a loud
 * patch on that channel alone (algorithm 7, four carriers at TL 0, both
 * sides), keyed on, 4,000 samples clocked; each clock's output is summed
 * by the group of four cycles it came in. The loudest group must be the
 * one the table names for the channel, at least 20 times any other group,
 * and every other group must hold only the DAC's idle output (a few
 * counts a clock: measured +-3 a side). Prints a line per channel; exit 1 on a
 * mismatch. (tools/hzmenu_check.py runs it.) */
#include "../src/audio/ym3438.h"
#include "../src/core/audio.h"

#include <stdio.h>
#include <stdlib.h>

static void clock_sample(ym3438_t *chip, long sum[6])
{
    Bit16s buf[2];
    for (int i = 0; i < 24; i++) {
        int g = (int)(chip->cycles >> 2);
        OPN2_Clock(chip, buf);
        if (sum != NULL) {
            sum[g] += labs((long)buf[0]) + labs((long)buf[1]);
        }
    }
}

/* A register write as the mixer makes it: the address, a sample, the
 * data, a sample (Nuked drops writes issued back to back). */
static void write(ym3438_t *chip, int part, int reg, int value)
{
    OPN2_Write(chip, part ? 2u : 0u, (Bit8u)reg);
    clock_sample(chip, NULL);
    OPN2_Write(chip, part ? 3u : 1u, (Bit8u)value);
    clock_sample(chip, NULL);
}

int main(void)
{
    static ym3438_t chip;
    int bad = 0;

    OPN2_SetChipType(ym3438_mode_ym2612);
    for (int ch = 0; ch < 6; ch++) {
        OPN2_Reset(&chip);
        int part = ch / 3;
        int c = ch % 3;
        for (int op = 0; op < 4; op++) {
            int o = c + 4 * op;
            write(&chip, part, 0x30 + o, 0x01);         /* DT 0, MUL 1 */
            write(&chip, part, 0x40 + o, 0x00);         /* TL 0 */
            write(&chip, part, 0x50 + o, 0x1F);         /* AR 31 */
            write(&chip, part, 0x60 + o, 0x00);
            write(&chip, part, 0x70 + o, 0x00);
            write(&chip, part, 0x80 + o, 0x0F);
        }
        write(&chip, part, 0xB0 + c, 0x07);             /* algorithm 7 */
        write(&chip, part, 0xB4 + c, 0xC0);             /* left and right */
        write(&chip, part, 0xA4 + c, 0x22);             /* block 4 */
        write(&chip, part, 0xA0 + c, 0x69);
        write(&chip, 0, 0x28, 0xF0 | (part ? 4 + c : c));
        long sum[6] = { 0, 0, 0, 0, 0, 0 };
        for (int s = 0; s < 4000; s++) {
            clock_sample(&chip, sum);
        }
        int loudest = 0;
        long quiet = 0;
        for (int g = 1; g < 6; g++) {
            if (sum[g] > sum[loudest]) {
                loudest = g;
            }
        }
        for (int g = 0; g < 6; g++) {
            if (g != loudest && sum[g] > quiet) {
                quiet = sum[g];
            }
        }
        /* an idle group: a few counts on 4 clocks a sample, both sides */
        bool ok = HZ_AUDIO_CYCLE_CHANNEL[loudest] == ch
               && quiet <= 4000L * 4 * 2 * 4 && sum[loudest] > 20 * quiet;
        printf("FM%d: group %d (table: FM%d), %ld against %ld  %s\n", ch + 1,
               loudest, HZ_AUDIO_CYCLE_CHANNEL[loudest] + 1, sum[loudest],
               quiet, ok ? "ok" : "MISMATCH");
        bad += ok ? 0 : 1;
    }
    return bad ? 1 : 0;
}
