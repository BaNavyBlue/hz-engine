"""Nibble-RLE used by the $53000 module: M_RLE_UnpackToVRAM ($57218) and M_RLE_UnpackToRAM ($571AC).

Table: u32 offsets from the table start, one per entry, no count.
Entry: u32 output size (low 2 bits ignored: lsr.l #2), then bytes:
high nibble = pixel, low nibble = run-1 for 0-$C; $D = next byte,
$E = next word, $F = next word + $10000. Pixels pack 8 per longword.
Tables: $60000 (1), $61100 (2), $68420 (2), $70000 (5).
"""
def rle_unpack(rom, table, index, limit=None):
    if limit is None:
        limit = len(rom)
    off = int.from_bytes(rom[table+index*4:table+index*4+4], 'big')
    a = table + off
    size = int.from_bytes(rom[a:a+4], 'big'); a += 4
    size &= ~3                     # M_RLE_UnpackToVRAM: lsr.l #2 drops the low bits
    if size == 0 or size > 0x20000:
        raise ValueError('bad size %X' % size)
    out = bytearray(); cur = 0; nib = 0
    while len(out) < size:
        if a >= limit:
            raise ValueError('ran off end')
        b = rom[a]; a += 1
        pix = b >> 4; r = b & 15
        if r == 0xD:
            r = rom[a]; a += 1
        elif r == 0xE:
            r = (rom[a] << 8) | rom[a+1]; a += 2
        elif r == 0xF:
            r = ((rom[a] << 8) | rom[a+1]) + 0x10000; a += 2
        for _ in range(r + 1):
            cur = ((cur << 4) | pix) & 0xFFFFFFFF; nib += 1
            if nib == 8:
                out += cur.to_bytes(4, 'big'); nib = 0
                if len(out) == size:
                    break
    return bytes(out), table + off, a
