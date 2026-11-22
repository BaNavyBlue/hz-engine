#include "hz_lzw.h"

#include <string.h>

#define LZW_CLEAR      0x100
#define LZW_FIRST      0x101
#define LZW_INIT_BITS  9
#define LZW_MAX_BITS   12
#define LZW_TABLE_SIZE (1 << LZW_MAX_BITS)    /* maxmaxcode, ($A018) */
#define LZW_EOF        (-1)
#define RLE_MARK       0x90

/* ROM $2CF6. */
static const u8 rmask[9] = {
    0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
};

/* LZW_GetCode's ($2B34) state. The 68k keeps it at $FFA000-$FFA02F. */
typedef struct {
    const u8 *src;          /* ($A004) */
    u32 left;               /* ($A000) packed bytes not yet read */
    int n_bits;             /* ($A00C) current code width */
    int maxcode;            /* ($A014) largest code at this width */
    int clear_flg;          /* ($A00E) set by CLEAR: next read restarts at 9 bits */
    int offset;             /* ($A016) bit offset of the next code in buf */
    int size;               /* ($A012) bit offsets below this can start a code */
    int free_ent;           /* ($A010) next table slot */
    /* ($FFA020) one chunk. The +1 is for the byte getcode touches past a
     * code that ends exactly on a byte boundary; rmask[0] clears it. */
    u8 buf[LZW_MAX_BITS + 1];
} Lzw;

/* LZW_PutcNcr's ($2C90) state: ARC's putc_ncr. */
typedef struct {
    u8 *dst;
    u32 cap;
    u32 len;
    bool rep;               /* ($A01C) the last byte in was the $90 marker */
    u8 last;                /* ($A01E) the byte a repeat copies */
    bool overflow;
} Rle90;

/* getcode() from compress 4.0, which LZW_GetCode ($2B34) is. Codes are read n_bits
 * bytes (eight codes) at a time, and a width change or a CLEAR throws away
 * the rest of the current chunk, so this is not a plain bitstream reader. */
static int lzw_getcode(Lzw *z)
{
    if (z->clear_flg > 0 || z->offset >= z->size || z->free_ent > z->maxcode) {
        if (z->free_ent > z->maxcode) {
            z->n_bits++;
            if (z->n_bits == LZW_MAX_BITS) {
                z->maxcode = LZW_TABLE_SIZE;
            } else {
                z->maxcode = (1 << z->n_bits) - 1;
            }
        }
        if (z->clear_flg > 0) {
            z->n_bits = LZW_INIT_BITS;
            z->maxcode = (1 << LZW_INIT_BITS) - 1;
            z->clear_flg = 0;
        }
        int got = 0;
        while (got < z->n_bits && z->left > 0) {
            z->buf[got] = *z->src;
            z->src++;
            z->left--;
            got++;
        }
        if (got <= 0) {
            return LZW_EOF;
        }
        z->offset = 0;
        z->size = (got << 3) - (z->n_bits - 1);
    }

    int r_off = z->offset;
    int bits = z->n_bits;
    const u8 *bp = z->buf + (r_off >> 3);
    r_off &= 7;

    int code = *bp >> r_off;
    bp++;
    bits -= 8 - r_off;
    r_off = 8 - r_off;
    if (bits >= 8) {
        code |= *bp << r_off;
        bp++;
        r_off += 8;
        bits -= 8;
    }
    code |= (*bp & rmask[bits]) << r_off;

    z->offset += z->n_bits;
    return code & 0xFFF;
}

static void rle90_emit(Rle90 *o, u8 c)
{
    if (o->len < o->cap) {
        o->dst[o->len] = c;
        o->len++;
    } else {
        o->overflow = true;
    }
}

/* ARC's putc_ncr: LZW_PutcNcr ($2C90), and the copy of it inlined at $2A86. */
static void rle90_put(Rle90 *o, u8 c)
{
    if (o->rep) {
        if (c == 0) {
            rle90_emit(o, RLE_MARK);
        } else {
            /* subq.b #1 / beq: c - 1 more copies of the last byte. */
            for (u8 n = (u8)(c - 1); n > 0; n--) {
                rle90_emit(o, o->last);
            }
        }
        o->rep = false;
    } else if (c == RLE_MARK) {
        o->rep = true;
    } else {
        o->last = c;
        rle90_emit(o, c);
    }
}

HzUnpackResult hz_unpack(const u8 *src, u32 src_len,
                         u8 *dst, u32 dst_cap, u32 *out_len)
{
    u16 prefix[LZW_TABLE_SIZE];     /* ($FFB430) */
    u8 suffix[LZW_TABLE_SIZE];      /* ($FFA430) */
    /* ($FFA030) The 68k has $400 bytes here before it runs into suffix[];
     * real data never gets near that, corrupt data is caught below. */
    u8 stack[LZW_TABLE_SIZE];
    HzUnpackResult result = HZ_UNPACK_OK;

    Lzw z;
    memset(&z, 0, sizeof z);
    z.src = src;
    z.left = src_len;
    z.n_bits = LZW_INIT_BITS;
    z.maxcode = (1 << LZW_INIT_BITS) - 1;
    z.free_ent = LZW_FIRST;

    Rle90 o;
    memset(&o, 0, sizeof o);
    o.dst = dst;
    o.cap = dst_cap;

    /* LZW_ClearPrefix ($2978) clears prefix[0..511]; $29E2 sets suffix[k] = k. */
    memset(prefix, 0, sizeof prefix);
    memset(suffix, 0, sizeof suffix);
    for (int k = 0; k < 256; k++) {
        suffix[k] = (u8)k;
    }

    int code = lzw_getcode(&z);
    if (code == LZW_EOF) {
        *out_len = 0;
        return HZ_UNPACK_OK;
    }
    int finchar = code;             /* d7 */
    int oldcode = code;             /* d5 */
    rle90_put(&o, (u8)finchar);

    for (;;) {
        code = lzw_getcode(&z);
        if (code == LZW_EOF) {
            break;
        }
        if (code == LZW_CLEAR) {
            memset(prefix, 0, 512 * sizeof prefix[0]);
            z.clear_flg = 1;
            z.free_ent = LZW_FIRST - 1;
            code = lzw_getcode(&z);
            if (code == LZW_EOF) {
                break;
            }
        }

        int incode = code;          /* d4 */
        int sp = 0;
        if (code >= z.free_ent) {
            stack[sp] = (u8)finchar;
            sp++;
            code = oldcode;
        }
        while (code >= 0x100) {
            if (sp >= LZW_TABLE_SIZE - 1) {
                result = HZ_UNPACK_BAD_CODE;
                break;
            }
            stack[sp] = suffix[code];
            sp++;
            code = prefix[code];
        }
        if (result != HZ_UNPACK_OK) {
            break;
        }
        finchar = suffix[code];
        stack[sp] = (u8)finchar;
        sp++;
        while (sp > 0) {
            sp--;
            rle90_put(&o, stack[sp]);
        }

        /* After a CLEAR this fills slot $100 with the pre-CLEAR code, as
         * ARC does; nothing refers to it. */
        if (z.free_ent < LZW_TABLE_SIZE) {
            prefix[z.free_ent] = (u16)oldcode;
            suffix[z.free_ent] = (u8)finchar;
            z.free_ent++;
        }
        oldcode = incode;
    }

    *out_len = o.len;
    if (result == HZ_UNPACK_OK && o.overflow) {
        result = HZ_UNPACK_OVERFLOW;
    }
    return result;
}

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

u32 hz_pack_count(const u8 *rom, u32 rom_size, u32 base)
{
    if (base >= rom_size || rom_size - base < 2) {
        return 0;
    }
    if (rom[base] != HZ_PACK_MAGIC) {
        return 0;
    }
    return rom[base + 1];
}

bool hz_pack_entry(const u8 *rom, u32 rom_size, u32 base, u32 index,
                   u32 *src, u32 *len)
{
    if (index >= hz_pack_count(rom, rom_size, base)) {
        return false;
    }
    u32 at = base + 2 + 8 * index;
    if (rom_size - at < 8) {
        return false;
    }
    u32 off = be32(rom + at);
    u32 n = be32(rom + at + 4);
    if (off > rom_size - base || n > rom_size - base - off) {
        return false;
    }
    *src = base + off;
    *len = n;
    return true;
}
