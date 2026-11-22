/* (Session 78) hz_extract.py's extract_map in C (ex.h): a map's palette,
 * tiles, blocks, animated tiles, layout (Tiled's .tmx) and radar picture,
 * from the LZW tables Game_LoadMap unpacks. */
#include "ex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u16 rd16(const u8 *b, u32 o)
{
    return (u16)((b[o] << 8) | b[o + 1]);
}

static void upper(char *out, size_t n, const char *s)
{
    size_t i = 0;
    for (; s[i] != '\0' && i + 1 < n; i++) {
        out[i] = (char)toupper((unsigned char)s[i]);
    }
    out[i] = '\0';
}

/* write_tmx: the block layer (gid = block + 1) and the two path-hint
 * layers (red arrows 513-520 for P1, blue 521-528 for P2; 0 where the word
 * has no hint). */
static bool write_tmx(HzEx *x, const char *rel, const u8 *layout_b,
                      const char *name)
{
    static int cells[3][4096];
    for (int i = 0; i < 4096; i++) {
        u16 w = rd16(layout_b, (u32)i * 2);
        if (!(w & 0x200) && (w & 0xFC00)) {
            return ex_fail(x, "%s: layout word $%04X has hint bits without "
                           "bit 9", name, (unsigned)w);
        }
        cells[0][i] = (w & 0x1FF) + 1;
        cells[1][i] = (w & 0x200) ? 513 + ((w >> 13) & 7) : 0;
        cells[2][i] = (w & 0x200) ? 521 + ((w >> 10) & 7) : 0;
    }
    static const char *const LAYERS[3] = { "blocks", "path_p1", "path_p2" };
    ExBuf b;
    exb_init(&b);
    exb_puts(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    exb_puts(&b, "<map version=\"1.10\" tiledversion=\"1.10.2\" "
                 "orientation=\"orthogonal\" renderorder=\"right-down\" "
                 "width=\"64\" height=\"64\" tilewidth=\"32\" "
                 "tileheight=\"32\" infinite=\"0\" nextlayerid=\"4\" "
                 "nextobjectid=\"1\">\n");
    exb_puts(&b, " <tileset firstgid=\"1\" name=\"blocks\" tilewidth=\"32\" "
                 "tileheight=\"32\" tilecount=\"512\" columns=\"16\">\n");
    exb_puts(&b, "  <image source=\"blocks.png\" width=\"512\" "
                 "height=\"1024\"/>\n");
    exb_puts(&b, " </tileset>\n");
    exb_puts(&b, " <tileset firstgid=\"513\" name=\"path_hints\" "
                 "tilewidth=\"32\" tileheight=\"32\" tilecount=\"16\" "
                 "columns=\"8\">\n");
    exb_puts(&b, "  <image source=\"../path_hints.png\" width=\"256\" "
                 "height=\"64\"/>\n");
    exb_puts(&b, " </tileset>\n");
    for (int l = 0; l < 3; l++) {
        exb_printf(&b, " <layer id=\"%d\" name=\"%s\" width=\"64\" "
                       "height=\"64\">\n", l + 1, LAYERS[l]);
        exb_puts(&b, "  <data encoding=\"csv\">\n");
        for (int r = 0; r < 64; r++) {
            for (int c = 0; c < 64; c++) {
                exb_printf(&b, (c == 0) ? "%d" : ",%d", cells[l][r * 64 + c]);
            }
            exb_puts(&b, (r < 63) ? ",\n" : "\n");
        }
        exb_puts(&b, "</data>\n");
        exb_puts(&b, " </layer>\n");
    }
    exb_puts(&b, "</map>\n");
    bool ok = ex_write_buf(x, rel, &b);
    exb_free(&b);
    return ok;
}

/* extract_map. */
bool ex_map(HzEx *x, int n)
{
    const char *name = EX_MAP_NAMES[n];
    char big[16], folder[64], rel[EX_PATH];
    upper(big, sizeof big, name);
    snprintf(folder, sizeof folder, "maps/%d_%s", n, name);

    u32 tiles_n = 0, layout_n = 0, blocks_n = 0;
    u8 *tiles  = ex_lzw(x, EXR_pack_map_tiles, (u32)n, &tiles_n);
    u8 *layout = ex_lzw(x, EXR_pack_map_layout, (u32)n, &layout_n);
    u8 *blocks = ex_lzw(x, EXR_pack_map_blocks, (u32)n, &blocks_n);
    const u8 *pal_b = ex_at(x, EXR_MapPalettes + (u32)n * 32, 32);
    const u8 *radar = ex_at(x, EXR_RadarImages + (u32)n * 0x800, 0x800);
    bool ok = tiles != NULL && layout != NULL && blocks != NULL
           && pal_b != NULL && radar != NULL;
    if (ok && (tiles_n != 0x4000 || layout_n != 0x2000
               || blocks_n != 0x4800)) {
        ok = ex_fail(x, "map %d: unexpected unpacked sizes", n);
    }
    u16 pal[16];
    if (ok) {
        ex_palette_words(pal_b, 1, pal);
        snprintf(rel, sizeof rel, "%s/palette.png", folder);
        ok = ex_write_palette(x, rel, pal, 16, 16);
    }
    if (ok) {
        snprintf(rel, sizeof rel, "%s/tiles.png", folder);
        ok = ex_write_tile_sheet(x, rel, tiles, tiles_n, 512, pal, 16, false);
    }

    if (ok) {
        ExBuf b;
        exb_init(&b);
        char line[160];
        snprintf(line, sizeof line, "Map %d, %s: the 512 blocks "
                 "(v_map_blocks, $FF0000), one a line:", n, big);
        exb_header(&b, line);
        exb_header(&b, "16 nametable words, 4 x 4 tiles row by row, then "
                       "the block's");
        exb_header(&b, "terrain word (v_block_terrain, $FF4000). All in "
                       "hex. What the");
        exb_header(&b, "bits mean is in assets/README.md. blocks.png is a "
                       "picture of these");
        exb_header(&b, "for Tiled, drawn from them; editing it changes "
                       "nothing.");
        for (u32 k = 0; k < 512; k++) {
            for (u32 i = 0; i < 16; i++) {
                exb_printf(&b, (i == 0) ? "%04X" : " %04X",
                           (unsigned)rd16(blocks, k * 32 + i * 2));
            }
            exb_printf(&b, "  %04X  # %03X\n",
                       (unsigned)rd16(blocks, 0x4000 + k * 2), (unsigned)k);
        }
        snprintf(rel, sizeof rel, "%s/blocks.txt", folder);
        ok = ex_write_buf(x, rel, &b);
        exb_free(&b);
    }

    if (ok) {
        ExBuf b;
        exb_init(&b);
        char line[160];
        snprintf(line, sizeof line, "Map %d, %s: the 32 animated-tile "
                 "records (v_anim_tiles, $FF4400),", n, big);
        exb_header(&b, line);
        exb_header(&b, "one a line, 16 words in hex. Word 0 is the map tile "
                       "animated (0: the");
        exb_header(&b, "record is off); the words from 0 on are the tiles "
                       "it shows in turn");
        exb_header(&b, "(low 9 bits), up to a 0 word or the 16th. "
                       "Map_AnimTilesUpdate");
        snprintf(line, sizeof line, "($%X) steps them; see "
                 "assets/README.md.", (unsigned)EXR_Map_AnimTilesUpdate);
        exb_header(&b, line);
        for (u32 r = 0; r < 32; r++) {
            for (u32 i = 0; i < 16; i++) {
                exb_printf(&b, (i == 0) ? "%04X" : " %04X",
                           (unsigned)rd16(blocks, 0x4400 + r * 32 + i * 2));
            }
            exb_printf(&b, "  # %02u\n", (unsigned)r);
        }
        snprintf(rel, sizeof rel, "%s/anim.txt", folder);
        ok = ex_write_buf(x, rel, &b);
        exb_free(&b);
    }

    if (ok) {
        snprintf(rel, sizeof rel, "%s/layout.tmx", folder);
        ok = write_tmx(x, rel, layout, name);
    }

    if (ok) {
        ExCanvas c;
        ok = exc_new(x, &c, 64, 64);
        for (u32 t = 0; t < 64 && ok; t++) {
            ok = exc_tile(x, &c, radar, 0x800, t, (int)(t % 8) * 8,
                          (int)(t / 8) * 8, false, false);
        }
        snprintf(rel, sizeof rel, "%s/radar.png", folder);
        ok = ok && exc_save(x, rel, &c, pal, 16, false);
        exc_free(&c);
    }
    free(tiles);
    free(layout);
    free(blocks);
    return ok;
}
