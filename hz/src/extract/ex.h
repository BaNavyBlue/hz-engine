#ifndef HZ_EX_H
#define HZ_EX_H

/* (Session 78) The extractor's own parts (extract.h is its face): the
 * cartridge as read, the files written under the assets folder, and the
 * writers that make them as tools/hz_extract.py does -- text and JSON byte
 * for byte, PNGs the same picture (pngwrite.h, png_write_indexed). Each
 * step's file (ex_maps.c, ex_tiles.c, ...) ports that file's part of
 * hz_extract.py and says which. No ROM address is written here: they are
 * rom_map.h's, by the disassembly's names. */

#include "../core/hz_types.h"
#include "rom_map.h"

#include <stdarg.h>
#include <stddef.h>

#define EX_PATH 1024

typedef struct {
    const u8 *rom;              /* the cartridge, EX_ROM_SIZE bytes */
    u32       size;
    char      root[EX_PATH];    /* the folder written into: "assets" */
    char      err[512];         /* the first thing that went wrong */
    bool      failed;
    int       files;            /* files written */
    bool      made_held;        /* EX_MADE_FILE written, as its .part */
} HzEx;

/* The maps' names, 0-7, as the options screen has them in lower case
 * (folders maps/N_NAME/). */
extern const char *const EX_MAP_NAMES[8];

/* ---- what went wrong ---- */

/* Records the first failure (printf-style) and returns false. */
bool ex_fail(HzEx *x, const char *fmt, ...);

/* ---- the cartridge ---- */

u8  ex_b(const HzEx *x, u32 a);
u16 ex_w(const HzEx *x, u32 a);        /* big-endian */
u32 ex_l(const HzEx *x, u32 a);
/* [a, a + len) of the cartridge, or NULL (and a failure) past its end. */
const u8 *ex_at(HzEx *x, u32 a, u32 len);
/* Entry n of the LZW table at `table` (hz_lzw.h), unpacked: malloc'd, the
 * caller's; *len its length. NULL and a failure if it won't unpack. */
u8 *ex_lzw(HzEx *x, u32 table, u32 n, u32 *len);
/* Entry n of the RLE table at `table` (hz_rle.h), likewise. */
u8 *ex_rle(HzEx *x, u32 table, u32 n, u32 *len);

/* ---- text ---- */

typedef struct {
    char  *p;
    size_t len, cap;
} ExBuf;

void exb_init(ExBuf *b);
void exb_free(ExBuf *b);
void exb_putc(ExBuf *b, char c);
void exb_puts(ExBuf *b, const char *s);
void exb_printf(ExBuf *b, const char *fmt, ...);
/* hz_extract.py's write_text header line: "# " + line, trailing spaces
 * dropped, then a newline. */
void exb_header(ExBuf *b, const char *line);

/* ---- files under x->root (folders made as needed; "/" between parts) ---- */

/* EX_MADE_FILE goes in as EX_MADE_FILE.part (extract.h). */

bool ex_write(HzEx *x, const char *rel, const void *data, size_t n);
bool ex_write_buf(HzEx *x, const char *rel, const ExBuf *b);

/* An indexed PNG as hz_extract.py's Pillow saves one: w x h indices, one a
 * byte; `colours` RGB triples (Pillow's palette, its length deciding the
 * bit depth); index 0 clear with `transparent` (save_indexed's). */
bool ex_write_png(HzEx *x, const char *rel, int w, int h, const u8 *px,
                  const u8 *rgb, int colours, bool transparent);

/* ---- colours ---- */

/* cram_rgb: a CRAM word through the VA4 DAC ladder (hz_extract.py's DAC). */
void ex_cram_rgb(u16 word, u8 rgb[3]);
/* rgb_list: n CRAM words as n RGB triples into rgb (3 * n bytes). */
void ex_rgb_list(const u16 *words, int n, u8 *rgb);
/* palette_words: 16 * lines big-endian words from p into words. */
void ex_palette_words(const u8 *p, int lines, u16 *words);

/* ---- JSON, as hz_extract.py's write_json lays it out ---- */

typedef struct ExJson ExJson;

ExJson *exj_object(void);
ExJson *exj_array(void);
ExJson *exj_int(long long v);
ExJson *exj_bool(bool v);
ExJson *exj_str(const char *s);
ExJson *exj_strf(const char *fmt, ...);
/* Adds KEY (kept in the order added, as a Python dict) or an item; each
 * returns its container. A NULL v (out of memory) fails the write. */
ExJson *exj_set(ExJson *obj, const char *key, ExJson *v);
ExJson *exj_push(ExJson *arr, ExJson *v);
void    exj_free(ExJson *v);
/* json_text(v) + "\n" into REL; frees v. */
bool ex_write_json(HzEx *x, const char *rel, ExJson *v);
/* json.dumps of one string (ensure_ascii's escapes), for writers that
 * lay out JSON-like text themselves. */
void exb_json_string(ExBuf *b, const char *s);

/* ---- pictures (ex_pic.c): hz_extract.py's tiles, sheets and strips ---- */

/* An indexed picture being drawn: w x h indices, one a byte, all 0 at
 * first (Image.new('P', (w, h), 0)). */
typedef struct {
    int w, h;
    u8 *px;
} ExCanvas;

bool exc_new(HzEx *x, ExCanvas *c, int w, int h);
void exc_free(ExCanvas *c);
/* put_tile: tile t of `tiles` (4 bits a pixel, 32 bytes a tile; `len`
 * bytes in all) at (px, py), flipped as asked. Fails past `len`. */
bool exc_tile(HzEx *x, ExCanvas *c, const u8 *tiles, u32 len, u32 t,
              int px, int py, bool hflip, bool vflip);
/* The canvas as a PNG whose palette is `n` CRAM words (rgb_list). */
bool exc_save(HzEx *x, const char *rel, const ExCanvas *c, const u16 *pal,
              int n, bool transparent);

/* write_palette: n CRAM words as a strip `width` wide, a pixel each;
 * fails on a word with bits a colour can't hold. */
bool ex_write_palette(HzEx *x, const char *rel, const u16 *words, int n,
                      int width);
/* write_tile_sheet: `count` tiles 16 across, row by row. */
bool ex_write_tile_sheet(HzEx *x, const char *rel, const u8 *tiles, u32 len,
                         int count, const u16 *pal, int n, bool transparent);
/* write_picture: `rows` rows of `across` consecutive tiles, no
 * transparency. */
bool ex_write_picture(HzEx *x, const char *rel, const u8 *tiles, u32 len,
                      int across, int rows, const u16 *pal, int n);

/* A sheet group (hz_extract.py's MECH_GROUPS and the like): `n` pictures
 * of w x h tiles from `tile`, each picture's tiles down its columns, or
 * along its rows with `rows`. */
typedef struct {
    const char *name;
    int  tile, w, h, n;
    bool rows;
} ExGroup;

/* write_sprite_sheet: FOLDER/NAME.png and FOLDER/NAME.json, the tiles
 * from `first` (`count` of them; tiles[0] is tile `first`) laid out as
 * the groups' pictures, with the gaps as single tiles. */
bool ex_write_sprite_sheet(HzEx *x, const char *folder, const char *name,
                           const u8 *tiles, u32 len, int first, int count,
                           const ExGroup *groups, int ngroups,
                           const u16 *pal16);

/* ---- units (ex_data.c), for StartUnits and DemoData alike ---- */

/* A unit's 8 bytes as {flags, type, order, dir, x, y}. */
ExJson *ex_unit_record(const u8 *r);
/* Up to 32 such at b into list, to the first all-0 one; false when one
 * after it doesn't start with 0 (a gap). */
bool ex_unit_list(ExJson *list, const u8 *b);

/* ---- the menu font's text (ex_title.c) ---- */

/* menu_text: len codes of the menu font as text, '@' (its blank) a space,
 * a code with no character {XX}. */
void ex_menu_text(char *out, size_t n, const u8 *b, int len);
/* music_titles: song n's (0-17) two lines from MusicTitles, as menu_text;
 * false (and a failure) if they are not 19 characters and a NUL. */
bool ex_music_title(HzEx *x, int n, char lines[2][100]);

/* ---- the steps (extract.c runs them in this order) ---- */

bool ex_map(HzEx *x, int n);           /* ex_maps.c: extract_map */
bool ex_tiles(HzEx *x);                /* ex_tiles.c: extract_tiles */
bool ex_data(HzEx *x);                 /* ex_data.c: extract_data */
bool ex_title(HzEx *x);                /* ex_title.c: extract_title */
bool ex_sound(HzEx *x);                /* ex_sound.c: extract_sound */
bool ex_ending(HzEx *x);               /* ex_ending.c: extract_ending */
bool ex_demos(HzEx *x);                /* ex_demos.c: extract_demos */
bool ex_views(HzEx *x);                /* ex_views.c: render_views */

#endif /* HZ_EX_H */
