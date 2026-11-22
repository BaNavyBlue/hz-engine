/* Unpacks every entry of Herzog Zwei's four LZW tables and four RLE tables.
 *
 *     hz_unpack ROM OUTDIR
 *
 * Writes OUTDIR/pack_XXXXX_NN.bin and OUTDIR/rle_XXXXX_NN.bin (table
 * address, entry) and prints one line per entry. Exits non-zero if any
 * entry fails or comes out a size other than the one its loader uses. */

#include <stdio.h>
#include <stdlib.h>

#include "../src/core/hz_lzw.h"
#include "../src/core/hz_rle.h"

#define ROM_SIZE  0x80000u
#define OUT_CAP   0x10000u

typedef struct {
    u32 base;
    u32 expect;     /* bytes the loader uses */
    const char *what;
} Table;

static const Table tables[] = {
    { HZ_PACK_MAP_TILES,  0x4000u, "map tiles" },
    { HZ_PACK_MAP_LAYOUT, 0x2000u, "map layout, to $FF5000" },
    { HZ_PACK_MAP_BLOCKS, 0x4800u, "map, to $FF0000" },
    { HZ_PACK_FOREST,     0x8C00u, "Ending_UnpackForest, to $FF0000" },
};

/* RLE tables carry no count, so every entry is listed. The sizes are each
 * entry's own size word & ~3; $70000[3] stores $A01. */
typedef struct {
    u32 table;
    u32 index;
    u32 expect;
    const char *what;
} RleEntry;

static const RleEntry rle_entries[] = {
    { HZ_RLE_WINNER_FONT,     0, 0x2000u, "WINNER font" },
    { HZ_RLE_ENDING_PICTURES, 0, 0x8C00u, "still life" },
    { HZ_RLE_ENDING_PICTURES, 1, 0x8C00u, "grave" },
    { HZ_RLE_TANK_PHOTOS,     0, 0x8C00u, "tank photo" },
    { HZ_RLE_TANK_PHOTOS,     1, 0x8C00u, "tank photo 2" },
    { HZ_RLE_MENU_GFX,        0, 0x2000u, "menu fonts" },
    { HZ_RLE_MENU_GFX,        1, 0x1000u, "Technosoft logo" },
    { HZ_RLE_MENU_GFX,        2, 0x2800u, "title logo" },
    { HZ_RLE_MENU_GFX,        3, 0x0A00u, "ending font" },
    { HZ_RLE_MENU_GFX,        4, 0x0800u, "ENDE" },
};

static u8 rom[ROM_SIZE];
static u8 out[OUT_CAP];

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s ROM OUTDIR\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }
    size_t got = fread(rom, 1, ROM_SIZE, f);
    fclose(f);
    if (got != ROM_SIZE) {
        fprintf(stderr, "%s: %zu bytes, expected %u\n", argv[1], got, ROM_SIZE);
        return 1;
    }

    int failed = 0;
    for (size_t t = 0; t < sizeof tables / sizeof tables[0]; t++) {
        const Table *tb = &tables[t];
        u32 count = hz_pack_count(rom, ROM_SIZE, tb->base);
        if (count == 0) {
            printf("%05X: not a packed table\n", (unsigned)tb->base);
            failed++;
            continue;
        }
        for (u32 n = 0; n < count; n++) {
            u32 src = 0;
            u32 len = 0;
            if (!hz_pack_entry(rom, ROM_SIZE, tb->base, n, &src, &len)) {
                printf("%05X[%u]: bad entry\n", (unsigned)tb->base, (unsigned)n);
                failed++;
                continue;
            }
            u32 out_len = 0;
            HzUnpackResult r = hz_unpack(rom + src, len, out, OUT_CAP, &out_len);
            bool ok = r == HZ_UNPACK_OK && out_len == tb->expect;
            printf("%05X[%u]  %06X  packed %5X  unpacked %5X  %s%s\n",
                   (unsigned)tb->base, (unsigned)n, (unsigned)src,
                   (unsigned)len, (unsigned)out_len, tb->what,
                   ok ? "" : "  <-- FAIL");
            if (!ok) {
                failed++;
            }

            char path[1024];
            snprintf(path, sizeof path, "%s/pack_%05X_%02u.bin", argv[2],
                     (unsigned)tb->base, (unsigned)n);
            FILE *o = fopen(path, "wb");
            if (o == NULL) {
                perror(path);
                return 1;
            }
            fwrite(out, 1, out_len, o);
            fclose(o);
        }
    }

    for (size_t e = 0; e < sizeof rle_entries / sizeof rle_entries[0]; e++) {
        const RleEntry *re = &rle_entries[e];
        u32 src = 0;
        if (!hz_rle_entry(rom, ROM_SIZE, re->table, re->index, &src)) {
            printf("%05X[%u]: bad RLE entry\n", (unsigned)re->table, (unsigned)re->index);
            failed++;
            continue;
        }
        u32 out_len = 0;
        u32 used = 0;
        HzRleResult r = hz_rle_unpack(rom + src, ROM_SIZE - src, out, OUT_CAP,
                                      &out_len, &used);
        bool ok = r == HZ_RLE_OK && out_len == re->expect;
        printf("%05X[%u]  %06X  RLE to %06X  unpacked %5X  %s%s\n",
               (unsigned)re->table, (unsigned)re->index, (unsigned)src,
               (unsigned)(src + used), (unsigned)out_len, re->what,
               ok ? "" : "  <-- FAIL");
        if (!ok) {
            failed++;
        }

        char path[1024];
        snprintf(path, sizeof path, "%s/rle_%05X_%02u.bin", argv[2],
                 (unsigned)re->table, (unsigned)re->index);
        FILE *o = fopen(path, "wb");
        if (o == NULL) {
            perror(path);
            return 1;
        }
        fwrite(out, 1, out_len, o);
        fclose(o);
    }

    if (failed != 0) {
        printf("%d failed\n", failed);
        return 1;
    }
    return 0;
}
