#ifndef HZ_FILE_H
#define HZ_FILE_H

#include "hz_types.h"

#include <stddef.h>

/* A whole file, read in one go, with a NUL after its last byte so text can
 * be parsed in place. NULL if it can't be read; the caller frees it. The
 * PS2 port reads its assets through a RAM store (assetfs); here every
 * asset is read once at start-up and decoded into HzAssets, so a plain
 * read is all the loaders need. A file under assets/ with a mod in the
 * mods folder reads the mod (hz_mods.h). */
u8 *hz_file_read(const char *path, size_t *size);

/* (Session 56) A running hash (FNV-1a) of every file hz_file_read reads
 * from hz_file_hash_begin to hz_file_hash_end, each file's length then its
 * bytes: main.c hashes the assets' load, mods and all, which a network
 * game's two sides must have alike (net_client.h's net_game_id). */
void hz_file_hash_begin(void);
u64  hz_file_hash_end(void);

/* (Session 78) A PNG is hashed by its picture, not its bytes: png.c reads
 * it with hz_file_read_unhashed, then hashes what it decoded with
 * hz_file_hash_data (its width, height, palette and pixels). A PNG's bytes
 * depend on the zlib that wrote it -- Pillow from PyPI and MSYS2's Pillow
 * write 43 of the 59 asset PNGs differently, the same pictures -- and the
 * game reads only the picture, so two players whose extractions differ
 * only so play together (net_client.h, NET_GAME_CODE 12). */
u8  *hz_file_read_unhashed(const char *path, size_t *size);
void hz_file_hash_data(const void *data, size_t n);

#endif /* HZ_FILE_H */
