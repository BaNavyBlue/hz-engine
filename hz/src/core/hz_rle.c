#include "hz_rle.h"

/* M_RLE_PutNibble's registers. */
typedef struct {
    u8 *dst;                /* a1 */
    u32 cap;
    u32 len;
    u32 acc;                /* d0: each pixel shifts in at the bottom */
    u32 nibbles;            /* d3 counts 8 down to the store; this counts up */
    u32 longs_left;         /* d7 */
    bool overflow;
} RleOut;

static u32 read_be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

/* M_RLE_PutNibble ($57202) / M_RLE_PutNibbleVDP ($57274). True once the
 * last longword is stored: the 68k then returns straight from the unpacker
 * (addq.l #4,sp / rts), dropping the rest of the run. */
static bool put_nibble(RleOut *o, u8 pixel)
{
    o->acc = (o->acc << 4) | pixel;
    o->nibbles++;
    if (o->nibbles < 8) {
        return false;
    }
    o->nibbles = 0;
    if (o->cap - o->len >= 4) {
        o->dst[o->len + 0] = (u8)(o->acc >> 24);
        o->dst[o->len + 1] = (u8)(o->acc >> 16);
        o->dst[o->len + 2] = (u8)(o->acc >> 8);
        o->dst[o->len + 3] = (u8)o->acc;
        o->len += 4;
    } else {
        o->overflow = true;
    }
    o->longs_left--;
    return o->longs_left == 0;
}

HzRleResult hz_rle_unpack(const u8 *src, u32 src_len,
                          u8 *dst, u32 dst_cap, u32 *out_len, u32 *used)
{
    *out_len = 0;
    *used = 0;
    if (src_len < 4) {
        return HZ_RLE_TRUNCATED;
    }

    RleOut o = { dst, dst_cap, 0, 0, 0, read_be32(src) >> 2, false };
    u32 pos = 4;
    if (o.longs_left == 0) {
        *used = pos;
        return HZ_RLE_BAD_SIZE;
    }

    bool done = false;
    bool truncated = false;
    while (!done) {
        if (pos >= src_len) {
            truncated = true;
            break;
        }
        u8 b = src[pos++];
        u8 pixel = (u8)(b >> 4);
        u32 run = b & 0x0Fu;        /* d6: the run is d6 + 1 pixels (subq.l / bcc) */
        if (run == 0x0Du) {
            if (src_len - pos < 1) {
                truncated = true;
                break;
            }
            run = src[pos];
            pos += 1;
        } else if (run >= 0x0Eu) {
            if (src_len - pos < 2) {
                truncated = true;
                break;
            }
            u32 word = ((u32)src[pos] << 8) | src[pos + 1];
            pos += 2;
            run = run == 0x0Fu ? word + 0x10000u : word;
        }
        for (u32 k = 0; k <= run && !done; k++) {
            done = put_nibble(&o, pixel);
        }
    }

    *out_len = o.len;
    *used = pos;
    if (truncated) {
        return HZ_RLE_TRUNCATED;
    }
    return o.overflow ? HZ_RLE_OVERFLOW : HZ_RLE_OK;
}

bool hz_rle_entry(const u8 *rom, u32 rom_size, u32 table, u32 index,
                  u32 *entry)
{
    if (table >= rom_size || index >= (rom_size - table) / 4) {
        return false;
    }
    u32 off = read_be32(rom + table + index * 4);
    if (off > rom_size - table || rom_size - table - off < 4) {
        return false;
    }
    *entry = table + off;
    return true;
}
