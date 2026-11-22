"""Herzog Zwei's decompressor, LZW_Unpack ($298A), modelled in Python.

It is ARC's method 8 ("crunched"): Unix compress 4.0 LZW (9..12-bit codes,
LSB-first, CLEAR = 256, first free = 257) whose output goes through ARC's
RLE90 filter (0x90 = repeat marker). Unlike an ARC member there is no
leading max-bits byte; the caller passes the compressed length instead.

The chunked reader is compress's getcode(): codes are read n_bits bytes at a
time, and a width change or CLEAR throws away the rest of the current chunk.
A decoder that reads a plain bitstream gets this wrong after the first
width change.
"""

CLEAR = 0x100
INIT_BITS = 9
MAX_BITS = 12
MAXMAXCODE = 1 << MAX_BITS          # $A018 = $1000
RMASK = [0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF]  # ROM $2CF6


class _Reader:
    def __init__(self, src, length):
        self.src = src
        self.pos = 0
        self.left = length          # $A000
        self.n_bits = INIT_BITS     # $A00C
        self.maxcode = (1 << INIT_BITS) - 1   # $A014
        self.clear_flg = 0          # $A00E
        self.offset = 0             # $A016
        self.size = 0               # $A012
        self.buf = bytearray(16)    # $FFA020; getcode may touch one byte past n_bits, masked to 0
        self.free_ent = 0x101       # $A010, shared with the decoder

    def getcode(self):
        if self.clear_flg > 0 or self.offset >= self.size or self.free_ent > self.maxcode:
            if self.free_ent > self.maxcode:
                self.n_bits += 1
                if self.n_bits == MAX_BITS:
                    self.maxcode = MAXMAXCODE
                else:
                    self.maxcode = (1 << self.n_bits) - 1
            if self.clear_flg > 0:
                self.n_bits = INIT_BITS
                self.maxcode = (1 << INIT_BITS) - 1
                self.clear_flg = 0
            n = 0
            while n < self.n_bits and self.left > 0:
                self.left -= 1
                self.buf[n] = self.src[self.pos]
                self.pos += 1
                n += 1
            if n <= 0:
                return -1
            self.offset = 0
            self.size = (n << 3) - (self.n_bits - 1)
        r_off = self.offset
        bits = self.n_bits
        bp = r_off >> 3
        r_off &= 7
        code = self.buf[bp] >> r_off
        bp += 1
        bits -= 8 - r_off
        r_off = 8 - r_off
        if bits >= 8:
            code |= self.buf[bp] << r_off
            bp += 1
            r_off += 8
            bits -= 8
        code |= (self.buf[bp] & RMASK[bits]) << r_off
        self.offset += self.n_bits
        return code & 0xFFF


class _Rle90:
    def __init__(self):
        self.out = bytearray()
        self.rep = False            # $A01C
        self.last = 0               # $A01E

    def put(self, c):
        if self.rep:
            if c == 0:
                self.out.append(0x90)
            else:
                # subq.b #1 / beq: count-1 more copies of the last byte
                self.out.extend(bytes([self.last]) * ((c - 1) & 0xFF))
            self.rep = False
        elif c == 0x90:
            self.rep = True
        else:
            self.last = c
            self.out.append(c)


def decompress(src, length):
    """src: bytes starting at the packed data; length: packed byte count."""
    r = _Reader(src, length)
    o = _Rle90()
    prefix = [0] * MAXMAXCODE       # $FFB430 (words)
    suffix = list(range(256)) + [0] * (MAXMAXCODE - 256)   # $FFA430
    r.free_ent = 0x101
    code = r.getcode()
    if code == -1:
        return bytes(o.out)
    finchar = oldcode = code
    o.put(finchar & 0xFF)
    while True:
        code = r.getcode()
        if code < 0:
            break
        if code == CLEAR:
            for k in range(256):
                prefix[k] = 0
            r.clear_flg = 1
            r.free_ent = 0x100
            code = r.getcode()
            if code < 0:
                break
        incode = code
        stack = []
        if code >= r.free_ent:
            stack.append(finchar)
            code = oldcode
        while code >= 0x100:
            stack.append(suffix[code])
            code = prefix[code]
        finchar = suffix[code]
        stack.append(finchar)
        while stack:
            o.put(stack.pop())
        code = r.free_ent
        if code < MAXMAXCODE:
            prefix[code] = oldcode
            suffix[code] = finchar
            r.free_ent = code + 1
        oldcode = incode
    return bytes(o.out)


def table_entry(rom, base, n):
    """The (source address, packed length) LZW_Unpack's callers read for entry n."""
    off = int.from_bytes(rom[base + 2 + 8 * n: base + 6 + 8 * n], 'big')
    ln = int.from_bytes(rom[base + 6 + 8 * n: base + 10 + 8 * n], 'big')
    return base + off, ln
