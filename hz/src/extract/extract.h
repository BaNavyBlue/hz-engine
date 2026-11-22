#ifndef HZ_EXTRACT_H
#define HZ_EXTRACT_H

/* (Session 78) The game's files from the player's own cartridge image, in
 * C, in the game itself (docs/first_start.md): what tools/hz_extract.py
 * makes from the ROM, every file -- text and JSON byte for byte, PNGs the
 * same picture -- with no Python and no library but the game's own code.
 * hz_extract.py stays the reference: `make assets_check` holds this to it.
 * The cartridge is Herzog Zwei for the USA, the image the disassembly
 * assembles to; the addresses are its (rom_map.h, from hz.sym). */

#include "../core/hz_types.h"

#include <stddef.h>

/* The file the game takes as the mark that its files are made (platform.h's
 * HZ_MADE_FILE): written as MADE.part and given its name by hz_ex_finish,
 * once every file is there, so a run cut short leaves no mark and the next
 * start makes them again. */
#define EX_MADE_FILE "data/units.json"

#define EX_ROM_SIZE 0x80000u
#define EX_ROM_SHA1 "8f7262102c2b2334f0bc88ee6fd6b08797919176"
#define EX_ROM_NAME "Herzog Zwei for the USA (\"Herzog Zwei (U) [!]\")"

/* ---- the player's image (ex_image.c) ---- */

/* PATH's cartridge bytes, in order, into *data (malloc'd, the caller's)
 * and *size: a raw image (.bin, .md, .gen), a .smd (interleaved: put in
 * order), one with a copier's 512-byte header (dropped), or a .zip holding
 * one of those (stored or deflated). False with why in err. */
bool hz_ex_read_image(const char *path, u8 **data, u32 *size, char *err,
                      size_t errlen);

/* Whether the bytes are the cartridge (EX_ROM_SIZE, EX_ROM_SHA1); if not,
 * err says what they are and what is wanted. */
bool hz_ex_is_cartridge(const u8 *data, u32 size, char *err, size_t errlen);

/* ---- the extraction (extract.c) ---- */

/* The steps, for a window to show as they go: their count, and step k's
 * name ("map 0, ABGRUND", "the music and sounds", ...). */
int         hz_ex_steps(void);
const char *hz_ex_step_name(int k);

typedef struct HzExtract HzExtract;

/* A run into the folder `root` ("assets"; made if need be) from `rom` (the
 * cartridge, EX_ROM_SIZE bytes, kept by the caller until the end). NULL if
 * out of memory. */
HzExtract *hz_ex_begin(const u8 *rom, u32 size, const char *root);
/* Runs step k (0 to hz_ex_steps() - 1, in order). False when it failed:
 * hz_ex_error says why, and the steps after it are not to be run. */
bool        hz_ex_step(HzExtract *e, int k);
const char *hz_ex_error(const HzExtract *e);
int         hz_ex_files(const HzExtract *e);   /* written so far */
/* After the last step: EX_MADE_FILE put in place. False (hz_ex_error)
 * after a failure, or if it can't be. */
bool        hz_ex_finish(HzExtract *e);
void        hz_ex_end(HzExtract *e);

/* Every step at once, then hz_ex_finish; false with why in err. */
bool hz_ex_all(const u8 *rom, u32 size, const char *root, char *err,
               size_t errlen);

#endif /* HZ_EXTRACT_H */
