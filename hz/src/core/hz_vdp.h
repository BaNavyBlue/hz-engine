#ifndef HZ_VDP_H
#define HZ_VDP_H

#include "hz_types.h"

/* A model of the Mega Drive VDP at the level Herzog Zwei programs it: VRAM,
 * CRAM, VSRAM and the registers, written through a data port with the
 * VDP's auto-increment, and a renderer that reads them as the chip does.
 *
 * This is a deliberate departure from the PS2 port's video layer, which is
 * semantic (game code says what to draw; no VRAM addresses). Herzog Zwei's
 * screen is built from VDP tricks rather than drawn: the split screen is
 * the window plane over plane A plus a fixed H scroll offset on plane B,
 * the divider is six sprites, the HUD strip hides game sprites by cutting
 * sprite 5's link until an HBlank interrupt at line 30 puts it back, and
 * the scroll code streams nametable words with VDP commands it keeps in
 * RAM queues. Ported as VRAM writes, each of those stays a line-for-line
 * port, quirks included. What is kept from the PS2 renderer: the layer
 * order, the wrap arithmetic, column-major sprite tiles, the VA4 DAC
 * ladder and the PNG writer.
 *
 * Only what the game uses is modelled: mode 5, H40, no interlace, no
 * shadow/highlight. DMA is modelled by what it leaves in memory (fills and
 * copies happen at once); its timing is not. */

#define SCREEN_W 320
#define SCREEN_H 224

#define VDP_VRAM_SIZE    0x10000u
#define VDP_CRAM_WORDS   64
#define VDP_VSRAM_WORDS  40
#define VDP_REG_COUNT    24
#define VDP_SPRITES      80   /* H40 sprite table */
#define VDP_LINE_SPRITES 20   /* H40: sprites per line */
#define VDP_LINE_DOTS    320  /* H40: sprite pixels per line */
#define VDP_RASTER_MAX   16

typedef enum {
    VDP_VRAM = 0,
    VDP_CRAM,
    VDP_VSRAM
} VdpTarget;

typedef struct {
    int       line;     /* first line rendered with the new value */
    VdpTarget target;
    u16       addr;
    u16       value;
} VdpRasterWrite;

/* A sprite table entry as the VDP holds it. */
typedef struct {
    u16 y;          /* +0: screen y + 128 */
    u16 size_link;  /* +2: size in bits 8-11, link in bits 0-6 */
    u16 attr;       /* +4: priority, line, flips, tile */
    u16 x;          /* +6: screen x + 128 */
} VdpSprite;

typedef struct {
    u8  vram[VDP_VRAM_SIZE];      /* as the VDP holds it: big-endian words */
    u16 cram[VDP_CRAM_WORDS];     /* 0000 BBB0 GGG0 RRR0 */
    u16 vsram[VDP_VSRAM_WORDS];
    u8  reg[VDP_REG_COUNT];

    VdpTarget target;             /* where the data port writes */
    u16       addr;               /* and at what address */

    /* Writes an interrupt handler makes part-way down the next frame,
     * applied as vdp_render reaches their line and kept afterwards. */
    VdpRasterWrite raster[VDP_RASTER_MAX];
    int            raster_count;

    /* The VDP's per-line sprite limits (20 sprites, 320 dots). On by
     * default: the game's sprite multiplexer exists because of them. */
    bool sprite_limits;

    /* (Session 61) Sprites past the table, as many as there are (the
     * game's units with UNITS): drawn behind the table's sprites, in
     * their order (the first on top), with no limit a line or a frame, on
     * the lines where the table's links reach sprite `more_at` (so not
     * where the game's split cuts them off); not on a line a sprite at
     * x 0 masks. Heap memory, kept by vdp_reset (count 0). */
    VdpSprite *more;
    u32        more_count;
    u32        more_cap;
    int        more_at;

    /* (Session 72) The port's own: a sprite whose size/link word has
     * bits 12-15 set (the chip ignores them, and the game's own sprites
     * leave them clear) is drawn with its tiles' colours changed: colour
     * c of its palette line as colours[bits][c] (0: none). Table n counts
     * only while bit n of colours_on is set (vdp_set_colours); none is at
     * power-on. The repair truck's and its mechanics' (REPAIR). */
    u8  colours[16][16];
    u16 colours_on;
} Vdp;

/* Power-on state: everything zero, sprite limits on; no sprites past the
 * table (what it allocated for them kept). */
void vdp_reset(Vdp *v);

/* The sprites past the table, from now on (copied; none: n 0). */
void vdp_set_more(Vdp *v, const VdpSprite *s, u32 n, int at);

/* (Session 72) Colour table n (1-15) for the sprites that name it in bits
 * 12-15 of their size/link word; map NULL: no table n again. */
void vdp_set_colours(Vdp *v, int n, const u8 map[16]);

/* (Session 64) src into dst, dst keeping its own memory for the sprites
 * past the table (the nightmare AI's look ahead: nightmare_think_ahead). */
void vdp_copy(Vdp *dst, const Vdp *src);

/* A register write as the 68k sends it: $8rvv sets register r to vv. */
void vdp_reg(Vdp *v, u16 cmd);

/* Where the data port writes next. `cmd` is the 68k's 32-bit command
 * ($40000003 = VRAM $C000, $C0000000 = CRAM 0, $40000010 = VSRAM 0);
 * vdp_set_write is the same with the address spelled out. */
void vdp_command(Vdp *v, u32 cmd);
void vdp_set_write(Vdp *v, VdpTarget target, u16 addr);

/* Data port writes; each advances the address by register 15. */
void vdp_write16(Vdp *v, u16 value);
void vdp_write32(Vdp *v, u32 value);

/* Big-endian source bytes written word by word from the current address,
 * as a 68k copy loop or a 68k-to-VDP DMA does. `bytes` is even. */
void vdp_write_bytes(Vdp *v, const u8 *src, u32 bytes);

/* Big-endian source bytes into VRAM from `addr` (even), as a 68k copy
 * loop with the auto-increment at 2 writes them, whatever register 15
 * holds; the write address is left alone. */
void vdp_put_vram(Vdp *v, u16 addr, const u8 *src, u32 bytes);

/* A VRAM copy as the game's copy DMAs do it (the auto-increment at 1):
 * `bytes` bytes from `src` to `dst`, one at a time, at once. The DMA
 * registers are left alone. */
void vdp_copy_vram(Vdp *v, u16 dst, u16 src, u32 bytes);

/* DMA fill of VRAM: `count` bytes from `addr`, stepping by register 15. */
void vdp_vram_fill(Vdp *v, u16 addr, u32 count, u8 value);

/* DMA copy within VRAM, as the registers set it up: register 23 $C0
 * (mode: VRAM copy), the length in bytes in 19-20 (0 = $10000), the
 * source address in 21-22 (the byte address itself, not halved as for a
 * 68k source). Called after the command that starts it (CD5-4 set,
 * sent as the 68k's v_dma_trigger sends the last word); each byte goes
 * from the source, stepping by 1, to the write address, stepping by
 * register 15 (the game sets 1). The copy completes at once. */
void vdp_vram_copy(Vdp *v);

u16 vdp_vram_word(const Vdp *v, u16 addr);

/* Queues a write that takes effect from `line` during the next
 * vdp_render. False if the queue is full. */
bool vdp_raster_write(Vdp *v, int line, VdpTarget target, u16 addr,
                      u16 value);

/* One frame: palette indices 0..63 (line * 16 + colour), SCREEN_W x
 * SCREEN_H. Applies and then clears the raster writes. */
void vdp_render(Vdp *v, u8 *out);

/* (Session 58) What vdp_render does to the VDP, without drawing: the
 * raster writes applied in the same order, then cleared. For a game no
 * one watches (server/hz_netai.c), whose VRAM must still be the one the
 * player's machine has. */
void vdp_apply_rasters(Vdp *v);

/* CRAM word to 8-bit RGB through the VA4 DAC ladder (the PS2 port's), and
 * back: each channel to the nearest of the eight levels, so a colour the
 * ladder makes comes back exactly (the palette PNGs' round trip). */
void vdp_cram_rgb(u16 cram, u8 rgb[3]);
u16  vdp_rgb_cram(const u8 rgb[3]);

#endif /* HZ_VDP_H */
