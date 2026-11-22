/* (Session 78) The player's cartridge image, as they have it (extract.h):
 * raw, a .smd, with a copier's header, or in a .zip -- the ways a
 * Mega Drive image is commonly kept -- told apart by its bytes. */
#include "extract.h"

#include "../core/png.h"
#include "../core/sha1.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(char *err, size_t n, const char *fmt, const char *a,
                const char *b)
{
    if (err != NULL && n > 0) {
        snprintf(err, n, fmt, a, b);
    }
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    if (b != NULL && (s == NULL || b > s)) {
        s = b;
    }
    return (s != NULL) ? s + 1 : path;
}

static bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), m = strlen(tail);
    if (n < m) {
        return false;
    }
    for (size_t i = 0; i < m; i++) {
        if (tolower((unsigned char)s[n - m + i]) != tail[i]) {
            return false;
        }
    }
    return true;
}

static bool image_name(const char *name)
{
    return ends_with(name, ".bin") || ends_with(name, ".md")
        || ends_with(name, ".gen") || ends_with(name, ".smd");
}

static u32 le16(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 le32(const u8 *p)
{
    return le16(p) | (le16(p + 2) << 16);
}

/* The one cartridge image a zip holds, by its central directory: stored
 * or deflated. Its name into name (for .smd). */
static u8 *from_zip(const u8 *z, size_t n, u32 *size, char *name,
                    size_t name_len, char *err, size_t errlen,
                    const char *path)
{
    /* the end record: the last "PK\5\6" with room for its 22 bytes */
    size_t end = (size_t)-1;
    if (n >= 22) {
        size_t lo = (n > 22 + 0xFFFF) ? n - 22 - 0xFFFF : 0;
        for (size_t i = n - 22 + 1; i-- > lo;) {
            if (memcmp(z + i, "PK\5\6", 4) == 0) {
                end = i;
                break;
            }
        }
    }
    if (end == (size_t)-1) {
        say(err, errlen, "%s: a zip whose directory can't be read%s",
            base_name(path), "");
        return NULL;
    }
    u32 entries = le16(z + end + 10);
    size_t at = le32(z + end + 16);
    int found = 0;
    u32 method = 0, packed = 0, unpacked = 0, local = 0;
    for (u32 e = 0; e < entries; e++) {
        if (at + 46 > n || memcmp(z + at, "PK\1\2", 4) != 0) {
            say(err, errlen, "%s: a zip whose directory can't be read%s",
                base_name(path), "");
            return NULL;
        }
        u32 name_n = le16(z + at + 28);
        u32 extra = le16(z + at + 30), comment = le16(z + at + 32);
        if (at + 46 + name_n > n) {
            break;
        }
        char entry[256];
        size_t k = (name_n < sizeof entry - 1) ? name_n : sizeof entry - 1;
        memcpy(entry, z + at + 46, k);
        entry[k] = '\0';
        if (image_name(entry)) {
            found++;
            method   = le16(z + at + 10);
            packed   = le32(z + at + 20);
            unpacked = le32(z + at + 24);
            local    = le32(z + at + 42);
            snprintf(name, name_len, "%s", entry);
        }
        at += 46 + name_n + extra + comment;
    }
    if (found != 1) {
        char count[16];
        snprintf(count, sizeof count, "%d", found);
        say(err, errlen, "%s: a zip with %s cartridge images in it, not one",
            base_name(path), count);
        return NULL;
    }
    if ((size_t)local + 30 > n || memcmp(z + local, "PK\3\4", 4) != 0) {
        say(err, errlen, "%s: a zip that can't be read%s", base_name(path),
            "");
        return NULL;
    }
    size_t data = local + 30 + le16(z + local + 26) + le16(z + local + 28);
    if (data > n || packed > n - data) {
        say(err, errlen, "%s: a zip that is cut short%s", base_name(path),
            "");
        return NULL;
    }
    u8 *out = NULL;
    size_t out_len = 0;
    if (method == 0) {
        out = malloc(packed > 0 ? packed : 1);
        if (out != NULL) {
            memcpy(out, z + data, packed);
            out_len = packed;
        }
    } else if (method == 8) {
        out = png_inflate_raw(z + data, packed, &out_len);
    } else {
        say(err, errlen, "%s: a zip packed in a way the game can't read "
            "(only stored or deflated)%s", base_name(path), "");
        return NULL;
    }
    if (out == NULL || out_len != unpacked) {
        free(out);
        say(err, errlen, "%s: its image won't unpack%s", base_name(path),
            "");
        return NULL;
    }
    *size = (u32)out_len;
    return out;
}

/* A .smd: a 512-byte header, then 16 KB blocks, each the odd bytes' 8 KB
 * then the even bytes'. */
static u8 *deinterleave(const u8 *d, u32 n, u32 *size)
{
    u32 body = n - 512;
    u8 *out = malloc(body > 0 ? body : 1);
    if (out == NULL) {
        return NULL;
    }
    for (u32 b = 0; b < body; b += 16384) {
        const u8 *block = d + 512 + b;
        u32 len  = (body - b < 16384) ? body - b : 16384;
        u32 half = len / 2;
        for (u32 i = 0; i < half; i++) {
            out[b + i * 2 + 1] = block[i];
            out[b + i * 2]     = block[half + i];
        }
    }
    *size = body;
    return out;
}

bool hz_ex_read_image(const char *path, u8 **data, u32 *size, char *err,
                      size_t errlen)
{
    *data = NULL;
    *size = 0;
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        say(err, errlen, "%s: it can't be opened%s", path, "");
        return false;
    }
    u8 *file = NULL;
    long len = -1;
    if (fseek(fh, 0, SEEK_END) == 0) {
        len = ftell(fh);
    }
    if (len > 0 && len <= 64L * 1024 * 1024 && fseek(fh, 0, SEEK_SET) == 0) {
        file = malloc((size_t)len);
        if (file != NULL && fread(file, 1, (size_t)len, fh) != (size_t)len) {
            free(file);
            file = NULL;
        }
    }
    fclose(fh);
    if (file == NULL) {
        say(err, errlen, "%s: it can't be read (or is empty, or over 64 MB)%s",
            base_name(path), "");
        return false;
    }

    char name[256];
    snprintf(name, sizeof name, "%s", base_name(path));
    u32 n = (u32)len;
    if (n >= 4 && memcmp(file, "PK\3\4", 4) == 0) {
        u32 inner = 0;
        u8 *unz = from_zip(file, n, &inner, name, sizeof name, err, errlen,
                           path);
        free(file);
        if (unz == NULL) {
            return false;
        }
        file = unz;
        n = inner;
    }
    if (n % 16384 == 512 && (ends_with(name, ".smd")
                             || (n > 10 && file[8] == 0xAA
                                 && file[9] == 0xBB))) {
        u32 body = 0;
        u8 *in_order = deinterleave(file, n, &body);
        free(file);
        if (in_order == NULL) {
            say(err, errlen, "out of memory%s%s", "", "");
            return false;
        }
        file = in_order;
        n = body;
    } else if (n % 16384 == 512) {
        memmove(file, file + 512, n - 512);         /* a copier's header */
        n -= 512;
    }
    *data = file;
    *size = n;
    return true;
}

bool hz_ex_is_cartridge(const u8 *data, u32 size, char *err, size_t errlen)
{
    char hex[41];
    sha1_hex(data, size, hex);
    if (size == EX_ROM_SIZE && strcmp(hex, EX_ROM_SHA1) == 0) {
        return true;
    }
    if (err != NULL && errlen > 0) {
        char head[256] = "";
        if (size > 0x190 && memcmp(data + 0x100, "SEGA", 4) == 0) {
            char title[49], serial[15];
            memcpy(title, data + 0x150, 48);
            title[48] = '\0';
            memcpy(serial, data + 0x180, 14);
            serial[14] = '\0';
            for (char *p = title; *p != '\0'; p++) {
                if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) {
                    *p = '?';
                }
            }
            for (char *p = serial; *p != '\0'; p++) {
                if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) {
                    *p = '?';
                }
            }
            /* trailing spaces off */
            for (size_t k = strlen(title); k > 0 && title[k - 1] == ' '; k--) {
                title[k - 1] = '\0';
            }
            snprintf(head, sizeof head, " Its header names \"%s\", %s",
                     title, serial);
            if (strncmp(serial, "GM MK-1400", 10) == 0) {
                /* Herzog Zwei by its header, not by its bytes */
                size_t k = strlen(head);
                snprintf(head + k, sizeof head - k, ": this game, but a "
                         "changed or damaged copy, or another release.");
            } else {
                size_t k = strlen(head);
                snprintf(head + k, sizeof head - k, ".");
            }
        }
        snprintf(err, errlen,
                 "That is not the cartridge the game's files are made from "
                 "(%u bytes, SHA-1 %s).%s They are made from %s: %u bytes, "
                 "SHA-1 %s.", (unsigned)size, hex, head, EX_ROM_NAME,
                 (unsigned)EX_ROM_SIZE, EX_ROM_SHA1);
    }
    return false;
}
