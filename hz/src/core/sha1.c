#include "sha1.h"

#include <stdio.h>
#include <string.h>

static u32 rol(u32 v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void block(u32 h[5], const u8 *p)
{
    u32 w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((u32)p[i * 4] << 24) | ((u32)p[i * 4 + 1] << 16)
             | ((u32)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        u32 t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void sha1_hex(const u8 *data, size_t n, char hex[41])
{
    u32 h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                 0xC3D2E1F0u };
    size_t i = 0;
    for (; i + 64 <= n; i += 64) {
        block(h, data + i);
    }
    u8 last[128];
    size_t rest = n - i;
    memset(last, 0, sizeof last);
    memcpy(last, data + i, rest);
    last[rest] = 0x80;
    size_t len = (rest + 1 + 8 <= 64) ? 64 : 128;
    u64 bits = (u64)n * 8;
    for (int k = 0; k < 8; k++) {
        last[len - 1 - k] = (u8)(bits >> (8 * k));
    }
    block(h, last);
    if (len == 128) {
        block(h, last + 64);
    }
    for (int k = 0; k < 5; k++) {
        snprintf(hex + k * 8, 9, "%08x", (unsigned)h[k]);
    }
}
