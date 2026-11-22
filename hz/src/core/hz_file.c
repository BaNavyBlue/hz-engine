#include "hz_file.h"

#include "hz_mods.h"

#include <stdio.h>
#include <stdlib.h>

static bool g_hashing;
static u64  g_hash;

static void hash_bytes(const u8 *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        g_hash = (g_hash ^ p[i]) * 0x00000100000001B3ull;
    }
}

void hz_file_hash_begin(void)
{
    g_hashing = true;
    g_hash = 0xCBF29CE484222325ull;
}

u64 hz_file_hash_end(void)
{
    g_hashing = false;
    return g_hash;
}

/* (Session 78) Hashed when HASH (hz_file_read); png.c reads a PNG unhashed
 * and hashes the picture it decodes (hz_file.h). */
static u8 *read_file(const char *path, size_t *size, bool hash)
{
    char mod[1024];
    path = hz_mods_resolve(path, mod, sizeof mod);
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    u8 *buf = NULL;
    long length = -1;
    if (fseek(fh, 0, SEEK_END) == 0) {
        length = ftell(fh);
    }
    if (length >= 0 && fseek(fh, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)length + 1);
    }
    if (buf != NULL && fread(buf, 1, (size_t)length, fh) != (size_t)length) {
        free(buf);
        buf = NULL;
    }
    fclose(fh);
    if (buf == NULL) {
        return NULL;
    }
    buf[length] = '\0';
    if (g_hashing && hash) {
        u8 len[8];
        for (int i = 0; i < 8; i++) {
            len[i] = (u8)((u64)length >> (56 - 8 * i));
        }
        hash_bytes(len, sizeof len);
        hash_bytes(buf, (size_t)length);
    }
    if (size != NULL) {
        *size = (size_t)length;
    }
    return buf;
}

u8 *hz_file_read(const char *path, size_t *size)
{
    return read_file(path, size, true);
}

u8 *hz_file_read_unhashed(const char *path, size_t *size)
{
    return read_file(path, size, false);
}

void hz_file_hash_data(const void *data, size_t n)
{
    if (g_hashing) {
        hash_bytes(data, n);
    }
}
