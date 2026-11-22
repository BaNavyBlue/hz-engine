#include "hz_rom.h"

#include <stdio.h>
#include <stdlib.h>

bool hz_rom_load(HzRom *rom, const char *path)
{
    rom->data = NULL;
    rom->size = 0;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }

    u8 *buf = malloc(HZ_ROM_SIZE + 1);
    if (buf == NULL) {
        fclose(f);
        return false;
    }

    /* One byte more than the ROM holds, so an oversized file is caught. */
    size_t got = fread(buf, 1, HZ_ROM_SIZE + 1, f);
    fclose(f);
    if (got != HZ_ROM_SIZE) {
        free(buf);
        return false;
    }

    rom->data = buf;
    rom->size = HZ_ROM_SIZE;
    return true;
}

void hz_rom_free(HzRom *rom)
{
    free(rom->data);
    rom->data = NULL;
    rom->size = 0;
}

u16 hz_rom_sum(const HzRom *rom)
{
    /* $FFC0 passes of 4 words from EntryPoint: every word to the end. */
    u16 sum = 0;
    for (u32 a = HZ_ROM_SUM_START; a < rom->size; a += 2) {
        sum = (u16)(sum + hz_rd16(rom->data + a));
    }
    return sum;
}

u16 hz_rom_header_sum(const HzRom *rom)
{
    return hz_rd16(rom->data + HZ_ROM_CHECKSUM);
}

const u8 *hz_rom_at(const HzRom *rom, u32 addr, u32 len)
{
    if (addr > rom->size || len > rom->size - addr) {
        return NULL;
    }
    return rom->data + addr;
}
