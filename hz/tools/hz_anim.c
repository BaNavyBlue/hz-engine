/* hz_anim: Map_AnimTilesUpdate and Map_AnimTilesUpload (src/game/anim.c)
 * on random records and states, logged for tools/hzanim_check.py.
 *
 *   hz_anim ASSETS OUT.txt [--runs N] [--passes N] [--seed N]
 *
 * The shipping maps reach few of the routines' cases (no record that
 * changes tile has a word 0 with bits above $1FF, every step stays in a
 * record's own words), so each run makes up 32 records, mostly from a
 * small pool of tiles so that records share tiles and copy from each
 * other's slots: records off (word 0 is tile 0, other words not), word 0
 * with high bits and its own tile later in the sequence, all 16 words in
 * use, words whose low 9 bits are 0 but not the rest, repeated tiles,
 * words anywhere in 0-$FFFF. The state is random too: each step 0-15 or
 * b7 set (with anything below it), v_anim_tile_shown the step's tile,
 * word 0's, or anything, the queue's entries left over (pending or not,
 * either kind, any tiles), v_colour_cycle anything (often near $FFFF), and
 * before each pass registers 1 and 15. VRAM and CRAM are filled from the
 * run's seed by a formula the checker repeats. Each pass is a frame's
 * pair: v_unit_ai_group set (0 or 6 half the time), Map_AnimTilesUpdate,
 * Map_AnimTilesUpload.
 *
 * The log, numbers in hex:
 *   HZA1 runs passes seed
 *   R run fill            a run; VRAM byte i = (i x $9E3779B1 + fill x
 *                         $9E37) >> 24 (32-bit), CRAM word i = (i x $9E37
 *                         + fill) & $0EEE
 *   W r w0 ... w15        record r (32 lines)
 *   G r bytes             v_anim_tile_gfx r, 64 hex digits (32 lines)
 *   Q r pending dma src cmd   queue entry r as left over, in the 68k's
 *                         form (put_job; 32 lines)
 *   S steps shown cyc0 cyc1   the state: steps 2 hex digits each, shown
 *                             4 each, run together
 *   P group reg1 reg15    a pass: the group and the VDP registers before
 *   U r pending dma src cmd   every queue entry after Map_AnimTilesUpdate
 *                         (32 lines)
 *   E steps shown cyc0 cyc1 reg1 reg15 vram cram
 *                         after Map_AnimTilesUpload; vram and cram are
 *                         CRC-32s (zlib's) of all of VRAM and of the 64
 *                         CRAM words as big-endian bytes */

#include "../src/core/hz_vdp.h"
#include "../src/game/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 rng_state = 1;

static u32 rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static u32 rng_below(u32 n)
{
    return rng_next() % n;
}

/* CRC-32 (IEEE, as zlib's crc32), by table: all of VRAM every pass. */
static u32 crc_table[256];

static void crc_init(void)
{
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        }
        crc_table[i] = c;
    }
}

static u32 crc32_bytes(const u8 *p, size_t n)
{
    u32 crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc = (crc >> 8) ^ crc_table[(crc ^ p[i]) & 0xFF];
    }
    return ~crc;
}

static u32 cram_crc(const Vdp *v)
{
    u8 b[VDP_CRAM_WORDS * 2];
    for (int i = 0; i < VDP_CRAM_WORDS; i++) {
        b[i * 2] = (u8)(v->cram[i] >> 8);
        b[i * 2 + 1] = (u8)v->cram[i];
    }
    return crc32_bytes(b, sizeof b);
}

/* A word for a record: mostly a tile from the run's pool. */
static u16 pool_word(const u16 *pool, int pool_n)
{
    u32 k = rng_below(100);
    if (k < 70) {
        return pool[rng_below((u32)pool_n)];
    }
    if (k < 85) {
        return (u16)rng_below(0x200);
    }
    if (k < 92) {
        return (u16)(rng_next() & 0xFE00);      /* low 9 bits 0 */
    }
    return (u16)rng_next();
}

static void make_record(u16 *rec, const u16 *pool, int pool_n)
{
    memset(rec, 0, 16 * sizeof rec[0]);
    u32 kind = rng_below(100);
    int len = 1 + (int)rng_below(16);
    if (kind < 6) {
        return;                                         /* all zero */
    }
    for (int i = 0; i < len; i++) {
        u16 w = pool_word(pool, pool_n);
        if ((w & 0x1FF) == 0 && rng_below(3) != 0) {
            w = (u16)(w | (pool[rng_below((u32)pool_n)] & 0x1FF));
        }
        if (i > 0 && rng_below(5) == 0) {
            w = rec[i - 1];                             /* held a step */
        }
        rec[i] = w;
    }
    if (kind < 16) {
        rec[0] = (u16)(rec[0] & 0xFE00);                /* off */
    } else if (kind < 30) {
        u16 t = (u16)(1 + rng_below(0x1FF));            /* high bits */
        rec[0] = (u16)((rng_next() & 0xFE00) | 0x0200 | t);
        for (int i = 1; i < 16; i++) {
            if (rng_below(3) == 0) {
                rec[i] = (rng_below(2) == 0) ? t : (u16)(t | 0x0400);
            }
        }
    } else if (kind < 45) {
        for (int i = 0; i < 16; i++) {                  /* all 16 used */
            while ((rec[i] & 0x1FF) == 0) {
                rec[i] = pool_word(pool, pool_n);
            }
        }
    }
    if (kind >= 16 && (rec[0] & 0x1FF) == 0) {
        rec[0] = (u16)(rec[0] | (1 + rng_below(0x1FF)));
    }
}

static void put_state(FILE *f, const HzGame *g)
{
    fputc(' ', f);
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
        fprintf(f, "%02X", g->anim_tile_step[r]);
    }
    fputc(' ', f);
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
        fprintf(f, "%04X", g->anim_tile_shown[r]);
    }
    fprintf(f, " %X %X", g->colour_cycle[0], g->colour_cycle[1]);
}

/* Queue entry r in the 68k's form: pending, dma, then for the write the
 * picture's RAM address and the VDP write command, for the copy the
 * DMA's source registers ($95ss, $96ss) and the command's first word (its
 * low half, which the 68k leaves as an earlier entry had it and never
 * reads, as 0). */
static void put_job(FILE *f, char tag, int r, const HzAnimTileJob *j)
{
    u32 src;
    u32 cmd;
    if (j->copy) {
        u16 a = (u16)(j->from << 5);
        src = ((u32)(0x9500 | (a & 0xFF)) << 16) | (u32)(0x9600 | (a >> 8));
        cmd = (u32)j->to << 16;
    } else {
        src = HZ_ANIM_GFX_RAM + (u32)r * 32;
        cmd = (u32)(j->to | 0x4000) << 16;
    }
    fprintf(f, "%c %X %X %X %X %X\n", tag, r, j->pending, j->copy,
            (unsigned)src, (unsigned)cmd);
}

static int usage(void)
{
    fprintf(stderr, "usage: hz_anim ASSETS OUT.txt [--runs N] [--passes N] "
                    "[--seed N]\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        return usage();
    }
    int runs = 300;
    int passes = 40;
    unsigned seed = 1;
    for (int i = 3; i < argc; i++) {
        if (i + 1 >= argc) {
            return usage();
        }
        if (strcmp(argv[i], "--runs") == 0) {
            runs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--passes") == 0) {
            passes = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0) {
            seed = (unsigned)strtoul(argv[++i], NULL, 0);
        } else {
            return usage();
        }
    }
    if (runs < 1 || passes < 1) {
        return usage();
    }
    rng_state = seed * 2654435761u + 1;
    crc_init();

    static HzAssets assets;
    if (!hz_assets_load(&assets, argv[1])) {
        return 1;
    }
    FILE *f = fopen(argv[2], "w");
    if (f == NULL) {
        fprintf(stderr, "hz_anim: can't write %s\n", argv[2]);
        return 1;
    }
    static Vdp vdp;
    static HzGame game;
    HzGame *g = &game;
    fprintf(f, "HZA1 %X %X %X\n", runs, passes, seed);

    for (int run = 0; run < runs; run++) {
        vdp_reset(&vdp);
        game_init(g, &assets, &vdp);
        u32 fill = rng_next() & 0xFFFF;
        for (u32 i = 0; i < VDP_VRAM_SIZE; i++) {
            vdp.vram[i] = (u8)((i * 0x9E3779B1u + fill * 0x9E37u) >> 24);
        }
        for (u32 i = 0; i < VDP_CRAM_WORDS; i++) {
            vdp.cram[i] = (u16)((i * 0x9E37u + fill) & 0x0EEE);
        }
        fprintf(f, "R %X %X\n", run, (unsigned)fill);

        u16 pool[12];
        int pool_n = 4 + (int)rng_below(9);
        for (int i = 0; i < pool_n; i++) {
            pool[i] = (u16)rng_below(0x200);
        }
        for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
            u16 *rec = g->anim_tiles[r];
            make_record(rec, pool, pool_n);
            fprintf(f, "W %X", r);
            for (int i = 0; i < 16; i++) {
                fprintf(f, " %X", rec[i]);
            }
            fputc('\n', f);
        }
        for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
            fprintf(f, "G %X ", r);
            for (int i = 0; i < 32; i++) {
                g->anim_tile_gfx[r][i] = (u8)rng_next();
                fprintf(f, "%02X", g->anim_tile_gfx[r][i]);
            }
            fputc('\n', f);
        }
        for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
            HzAnimTileJob *j = &g->anim_tile_queue[r];
            j->pending = rng_below(4) != 0;
            j->copy = rng_below(2) != 0;
            j->from = (u16)(rng_next() & 0x1FF);
            j->to = (u16)(rng_next() & 0x3FE0);
            put_job(f, 'Q', r, j);

            const u16 *rec = g->anim_tiles[r];
            u32 s = rng_below(100);
            u8 step = (u8)rng_below(16);
            if (s < 12) {
                step = (u8)(0x80 | rng_below(0x80));
            } else if ((rec[0] & 0x1FF) == 0 && s < 80) {
                step = (u8)(0x80 | rng_below(0x80));    /* as Init leaves */
            }
            g->anim_tile_step[r] = step;
            u32 w = rng_below(100);
            u16 shown;
            if (w < 40) {
                shown = (u16)(rec[step & 0x0F] & 0x1FF);
            } else if (w < 60) {
                shown = (u16)(rec[0] & 0x1FF);
            } else if (w < 90) {
                shown = (u16)rng_below(0x200);
            } else {
                shown = (u16)rng_next();
            }
            g->anim_tile_shown[r] = shown;
        }
        for (int k = 0; k < 2; k++) {
            g->colour_cycle[k] = (u16)(rng_below(3) == 0
                                       ? 0xFFF0 + rng_below(16)
                                       : rng_next());
        }
        fputc('S', f);
        put_state(f, g);
        fputc('\n', f);

        for (int p = 0; p < passes; p++) {
            u16 group = (u16)(rng_below(2) == 0 ? (rng_below(2) == 0 ? 0 : 6)
                                                : rng_below(12));
            g->unit_ai_group = group;
            vdp.reg[1] = (u8)rng_next();
            vdp.reg[15] = (u8)rng_next();
            fprintf(f, "P %X %X %X\n", group, vdp.reg[1], vdp.reg[15]);
            map_anim_tiles_update(g);
            for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
                put_job(f, 'U', r, &g->anim_tile_queue[r]);
            }
            map_anim_tiles_upload(g);
            fputc('E', f);
            put_state(f, g);
            fprintf(f, " %X %X %X %X\n", vdp.reg[1], vdp.reg[15],
                    (unsigned)crc32_bytes(vdp.vram, VDP_VRAM_SIZE),
                    (unsigned)cram_crc(&vdp));
        }
    }
    fclose(f);
    printf("hz_anim: %d runs x %d passes\n", runs, passes);
    return 0;
}
