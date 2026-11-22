#ifndef HZ_SHA1_H
#define HZ_SHA1_H

#include "hz_types.h"

#include <stddef.h>

/* (Session 78) SHA-1 (FIPS 180-4), for telling the player's cartridge image
 * apart by its bytes (src/extract/ex_image.c): the hash every ROM set
 * lists. `hex` gets 40 lower-case digits and a NUL. */
void sha1_hex(const u8 *data, size_t n, char hex[41]);

#endif /* HZ_SHA1_H */
