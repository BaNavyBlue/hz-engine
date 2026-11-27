#ifndef HZ_VIDEO_VDP_H
#define HZ_VIDEO_VDP_H

#include "../src/core/video.h"
#include "hz_vdp.h"

#include <stdio.h>

/* (Half B of docs/hackable.md's step 1) The checks against the original
 * compare the cartridge's video memory, frame by frame; the game keeps
 * none since half B (src/core/video.h). This is the bridge, for the
 * checks alone: a watcher on the draw layer that lays each change out in
 * a Vdp as the 68k's writes did (half A's video.c, moved here) -- the
 * tiles at tile x 32, the layers' cells and the scroll where VDP_Init's
 * or M_VDPInit's registers put them, the colours in CRAM -- so a check
 * still sees the original's VRAM, CRAM, VSRAM and registers 0-18.
 *
 * Not the sprites: the cartridge's sprite table (its links, the window the
 * multiplexer chose, the DMA) has no place in the layer, which keeps the
 * frame's sprites in screen pixels. A check compares them as they are
 * drawn (video_sprites_crc) and leaves the table's bytes out of VRAM
 * (video_vdp_vram_crc): the bytes a sprite table has held since they were
 * last written elsewhere, by the same rule on the original's side
 * (tools/hzlayer.py). */

/* The cartridge's VRAM, as VDP_Init's registers and Game_LoadMap's uploads
 * lay it out (the menus': M_VDPInit's planes at $E000 and $F000, the
 * scroll at $DC00, the sprites at $D800). */
#define VRAM_MAP_TILES     0x0000u  /* 512 tiles, the map's $22400 entry */
#define VRAM_MECH1_FRAME   0x4000u  /* 16 tiles: P1's mech frame */
#define VRAM_MECH2_FRAME   0x4200u
#define VRAM_SPRITE_TILES  0x4800u  /* SpriteTiles: tiles $240-$4FF */
#define VRAM_HUD_TILES     0xA000u  /* HUDTiles: tiles $500-$5FF */
#define VRAM_PLANE_A       0xC000u  /* view A */
#define VRAM_SPRITES       0xD000u  /* the sprite table */
#define VRAM_HSCROLL       0xD400u  /* full-screen: A at +0, B at +2 */
#define VRAM_FONT          0xD620u  /* FontTiles: tiles $6B1-$6FF */
#define VRAM_PLANE_B       0xE000u  /* view B */
#define VRAM_WINDOW        0xF000u  /* the HUD strip (and the split's mask) */
#define VRAM_MENU_SPRITES  0xD800u
#define VRAM_TABLE_BYTES   0x280u   /* 80 sprites of 8 bytes */

typedef struct {
    Vdp        *vdp;
    VideoLayout layout;
    VideoWatch  watch;
    u8          held[VDP_VRAM_SIZE];    /* 1: a sprite table's byte */
} VideoVdp;

/* From now on every change to `video` is written to `vdp` too, as it is
 * (a check may have filled it with noise first). */
void video_vdp_watch(VideoVdp *w, Video *video, Vdp *vdp);

/* CRC-32 of the VRAM a check compares: the sprite tables' bytes as 0. */
u32 video_vdp_vram_crc(const VideoVdp *w);

/* The same VRAM, into `out` (a dump for naming a difference). */
void video_vdp_vram(const VideoVdp *w, u8 out[VDP_VRAM_SIZE]);

/* CRC-32 of the frame's sprites as they are drawn: each sprite of the
 * list, in order, that has a line on the screen at or below the first
 * its group is drawn from (the game's: 0, or VIDEO_HUD_LINES while the HUD
 * strip holds them back; while held and not let back, none) -- its x and
 * y (big-endian s16), w, h, its cell word (tile, flips, line, priority,
 * big-endian), its colour table and that first line (big-endian u16). */
u32 video_sprites_crc(const Video *v);

/* The same, as text, a line a sprite (for naming a difference). */
void video_sprites_print(const Video *v, FILE *f);

u32 video_vdp_crc32(const u8 *p, size_t n);

/* A frame as the checks against the original print it (hz_demo's,
 * hz_front's, hz_gameend's F lines after the frame's number): the VRAM's
 * CRC (video_vdp_vram_crc), CRAM, VSRAM's CRC, the 24 registers and the
 * sprites' CRC (video_sprites_crc). */
void video_vdp_frame_line(const VideoVdp *w, const Video *v, FILE *f);

#endif /* HZ_VIDEO_VDP_H */
