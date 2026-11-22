#ifndef HZ_ROM_H
#define HZ_ROM_H

#include "hz_types.h"

/* The cartridge image, read-only once loaded. The port reads its data
 * straight from the user's ROM for now; ROM addresses in the game code are
 * the disassembly's (named constants beside the code that uses them). */

#define HZ_ROM_SIZE      0x80000u   /* 512 KB, header $1A4: $0-$7FFFF */
#define HZ_ROM_CHECKSUM  0x18Eu     /* header word ChecksumCheck compares */
#define HZ_ROM_SUM_START 0x200u     /* EntryPoint: the sum runs from here */

typedef struct {
    u8  *data;
    u32  size;
} HzRom;

/* Reads the file. False (and *rom left empty) if it can't be read or isn't
 * HZ_ROM_SIZE bytes. */
bool hz_rom_load(HzRom *rom, const char *path);
void hz_rom_free(HzRom *rom);

/* ChecksumCheck ($380): the word sum of $200-$7FFFF, which the game
 * compares with the header word at $18E ("ROM ERROR" and a hang if they
 * differ). */
u16 hz_rom_sum(const HzRom *rom);
u16 hz_rom_header_sum(const HzRom *rom);

static inline u16 hz_rd16(const u8 *p)
{
    return (u16)((p[0] << 8) | p[1]);
}

static inline u32 hz_rd32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* A pointer into the ROM, or NULL if [addr, addr + len) isn't inside it. */
const u8 *hz_rom_at(const HzRom *rom, u32 addr, u32 len);

#endif /* HZ_ROM_H */
