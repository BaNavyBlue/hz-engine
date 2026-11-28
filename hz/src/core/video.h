#ifndef HZ_VIDEO_H
#define HZ_VIDEO_H

#include "cram.h"
#include "hz_types.h"

/* The draw layer (docs/hackable.md, step 1), PS2's video.h in shape: "a
 * semantic layer, not a VDP emulation". The game says what it wants drawn
 * -- tiles, the cells of three layers, each layer's scroll, the colours,
 * the frame's sprites in screen pixels, the screen on or off -- and
 * video_compose works out the pixels. No VRAM addresses, no register
 * writes, no DMA, no sprite table: a cell is (layer, column, row), a tile
 * its number, a sprite where it is on the screen.
 *
 * What is kept from the hardware: the indexed pipeline -- 8 x 8 tiles of
 * 4-bit colours, colour 0 clear, four palette lines of 16 -- because the
 * game's fades and colour cycles are arithmetic on palette entries, as
 * PS2's are; and tile numbers, which the art keeps until step 3 (pictures).
 *
 * The layers, back to front (with each cell's priority, as PS2's):
 *   LAYER_B  a scrolled layer: view B in a split game; the menus' picture
 *   LAYER_A  a scrolled layer: view A (the whole view in a wide game)
 *   LAYER_W  a fixed layer, never scrolled, shown instead of layer A where
 *            the screen's layout puts it: the HUD strip over a game (and in
 *            a split game the whole of view B's side, where its cells are
 *            clear and let layer B through); every menu screen in front
 * and the sprites among them by priority: B low, A or W low, sprites low,
 * B high, A or W high, sprites high.
 *
 * The screen of a split game: the HUD strip (the top 32 lines), view A on
 * the left (144 pixels), the divider (32 pixels, in front of both views'
 * edges and every sprite: video_divider) and view B on the right (144). */

#define SCREEN_W    320
#define SCREEN_H    224
#define TILE_W      8
#define TILE_H      8
#define TILE_BYTES  32      /* a tile as the game's data packs it */
#define VIDEO_TILES 2048    /* tile numbers 0-$7FF */
#define PLANE_COLS  64
#define PLANE_ROWS  32
#define VIDEO_COLOURS 64    /* four palette lines of 16 */

#define VIDEO_HUD_LINES   32    /* the HUD strip: the window's top 4 rows */
#define VIDEO_SPLIT_COLS  18    /* split: the window from column 18 (x 144) */

/* (Session 81) The frame's sprites: as many as the game pushes, each drawn
 * on every line it covers. The cartridge's limits are gone (80 a frame,
 * 20 and 320 of their pixels a line, a sprite at x -128 ending its line):
 * The owner, "We can do away with stock flicker behavior we're not
 * making an emulator" (docs/hackable.md, open question 2). */

/* The divider of a split game: x 144, 32 pixels wide, from line 32 down,
 * a 32 x 32 picture (4 x 4 tiles from its cell's tile, down each column
 * first, as a sprite's) one under another. */
#define VIDEO_DIVIDER_X     144
#define VIDEO_DIVIDER_TILES 4

typedef enum {
    LAYER_B = 0,
    LAYER_A = 1,
    LAYER_W = 2,
    LAYER_COUNT
} LayerId;

typedef struct {
    u16 tile;           /* 0-$7FF */
    u8  pal    : 2;     /* palette line */
    u8  prio   : 1;     /* in front of the sprites without it */
    u8  flip_h : 1;
    u8  flip_v : 1;
} Cell;

/* A sprite: w x h tiles from `tile`, running down each column first. */
typedef struct {
    s16 x, y;           /* screen pixels, its top left */
    u16 tile;
    u8  w, h;           /* in tiles, 1-4 */
    u8  pal    : 2;
    u8  prio   : 1;
    u8  flip_h : 1;
    u8  flip_v : 1;
    u8  colours;        /* (session 72) the colour table its pixels go
                           through (video_sprite_colours); 0: none */
} Sprite;

/* The screen's layout: which screen the layers make. */
typedef enum {
    VIDEO_GAME_SPLIT = 0,   /* the HUD strip; view A left, view B right */
    VIDEO_GAME_WIDE,        /* the HUD strip; one view */
    VIDEO_MENUS             /* layer W over layer B, the whole screen */
} VideoLayout;

/* (For the checks) What the layer was asked to change, told to a watcher
 * as it happens (video_watch): tools/video_vdp.c lays it out as the
 * cartridge's VRAM, so the checks against the original keep comparing
 * memory. The game never sets one. */
typedef enum {
    VIDEO_DID_START_GAME,       /* flag: split */
    VIDEO_DID_START_MENUS,
    VIDEO_DID_DISPLAY,          /* flag: on */
    VIDEO_DID_TILES,            /* first, count, packed */
    VIDEO_DID_CLEAR_TILES,      /* first, count */
    VIDEO_DID_COPY_TILE,        /* first (to), from */
    VIDEO_DID_CELL,             /* layer, col, row, word */
    VIDEO_DID_SCROLL,           /* layer, x, y */
    VIDEO_DID_COLOUR            /* index, colour */
} VideoDid;

typedef struct {
    VideoDid   what;
    bool       flag;
    LayerId    layer;
    int        col, row;
    u16        word;
    u16        first, from;
    u32        count;
    const u8  *packed;
    int        x, y;
    int        index;
    Cram       colour;
} VideoChange;

typedef struct Video Video;

typedef struct {
    void (*changed)(void *user, const Video *v, const VideoChange *c);
    void *user;
} VideoWatch;

struct Video {
    VideoLayout layout;
    bool        display;                    /* the screen on */
    u8   tiles[VIDEO_TILES][TILE_BYTES];    /* packed, as loaded */
    u16  cells[LAYER_COUNT][PLANE_ROWS][PLANE_COLS];   /* cell words */
    s16  scroll_x[LAYER_COUNT];             /* layers B and A */
    s16  scroll_y[LAYER_COUNT];
    Cram colours[VIDEO_COLOURS];

    /* The frame's sprites, the first on top. The screen's come first (the
     * loading screen's walk), then from `game_at` on the game's, which the
     * HUD strip can hold back: `held`, not drawn from the top of the
     * frame; `back_at`, drawn again from that line of the next frame
     * composed (and every line after it). Heap memory, grown as pushed and
     * kept by video_init (as `behind`). */
    Sprite *sprite;
    int    sprite_count;
    int    sprite_cap;
    int    game_at;
    bool   held;
    int    back_at;                         /* -1: none */

    /* (Session 61) UNITS' units: behind every other sprite, with no
     * limit, on the lines the game's sprites are drawn on. Heap memory,
     * kept by the layouts' starts. */
    Sprite *behind;
    u32     behind_count;
    u32     behind_cap;

    /* (Session 72) The repair truck's colour tables: table n (1-15) turns
     * a sprite's colour c into sprite_colours[n][c] (0: not drawn), while
     * bit n of sprite_colours_on is set. */
    u8   sprite_colours[16][16];
    u16  sprite_colours_on;

    /* (Session 81) The split game's divider (video_divider): its cell (the
     * first tile, its palette line), while `divider` is set. */
    bool divider;
    Cell divider_cell;

    const VideoWatch *watch;
};

/* The cell a packed cell word stands for, as the game's data keeps cells
 * (the maps' blocks, the HUD's frame, the menus' characters): tile in bits
 * 0-10, flips in 11 and 12, the palette line in 13-14, priority in 15. And
 * back. */
Cell cell_from_word(u16 w);
u16  cell_word(Cell c);

/* Everything clear, the game's split layout, the screen off; the sprites'
 * heap memory kept. */
void video_init(Video *v);

/* (For the checks) The watcher told of every change from now on; NULL:
 * none. */
void video_watch(Video *v, const VideoWatch *w);

/* A screen's layout, everything on it cleared: the tiles, the layers'
 * cells and scroll, the colours black, the sprites, the divider, the
 * screen off. The game's: the backdrop is colour 0 of line 2. */
void video_start_game(Video *v, bool split);

/* The menus' layout (M_VDPInit), nothing cleared but the sprites and the
 * divider, the screen off; the backdrop colour 0 of line 0. */
void video_start_menus(Video *v);

/* The screen on (the layers drawn) or off (the backdrop alone). */
void video_display(Video *v, bool on);

/* `count` tiles from tile `first` on (the numbers wrap at VIDEO_TILES),
 * from the game's packed tile data: 32 bytes a tile, two pixels a byte
 * (the left one in the high nibble), a row of four bytes after another. */
void video_load_tiles(Video *v, u16 first, const u8 *packed, u32 count);

/* Tiles `first` to `first + count - 1` clear. */
void video_clear_tiles(Video *v, u16 first, u32 count);

/* Tile `from`'s pixels into tile `to`. */
void video_copy_tile(Video *v, u16 to, u16 from);

/* One cell; column and row wrap round the layer's 64 x 32. */
void video_set_tile(Video *v, LayerId layer, int col, int row, Cell cell);
void video_set_word(Video *v, LayerId layer, int col, int row, u16 word);
Cell video_cell(const Video *v, LayerId layer, int col, int row);

/* Every cell of the layer, or of `rows` rows from `row` on. */
void video_fill(Video *v, LayerId layer, Cell cell);
void video_fill_rows(Video *v, LayerId layer, int row, int rows, Cell cell);

/* Layer A's or B's scroll: the layer's pixel (x + sx, y + sy) at screen
 * pixel (x, y), wrapping round its 512 x 256. */
void video_scroll(Video *v, LayerId layer, int sx, int sy);

/* Colour `index` (line * 16 + colour) of the palette, a 9-bit colour as
 * the game's palettes hold them (0000 BBB0 GGG0 RRR0); `n` from `first`. */
void video_set_colour(Video *v, int index, Cram colour);
void video_set_colours(Video *v, int first, const Cram *colours, int n);
Cram video_colour(const Video *v, int index);

/* The frame's sprites, from the first (on top): begun empty, the screen's
 * pushed, then video_sprites_game, then the game's. A push is refused
 * (false) only when no memory is left. The list stays until it is begun
 * again. */
void video_sprites_begin(Video *v);
bool video_sprite_push(Video *v, const Sprite *s);
void video_sprites_game(Video *v);

/* The HUD strip and the game's sprites: held back (not drawn from the top
 * of the frame) or not; let back from screen line `line` of the next
 * frame composed, and from the frame after on every line. */
void video_sprites_hold(Video *v, bool held);
void video_sprites_back(Video *v, int line);

/* The frame has been shown (or would have been): a sprites_back is in
 * full effect from here. The game's hand-over calls it after the host
 * has had the frame. */
void video_frame_done(Video *v);

/* (Session 61) UNITS' units, drawn behind every other sprite (copied;
 * none: n 0). */
void video_sprites_behind(Video *v, const Sprite *s, u32 n);

/* (Session 81) The split game's divider, in front of everything at
 * VIDEO_DIVIDER_X from the HUD strip down, drawn with `cell`'s tile on
 * (VIDEO_DIVIDER_TILES x VIDEO_DIVIDER_TILES of them) and palette line;
 * or none. Only the split layout draws it. */
void video_divider(Video *v, bool on, Cell cell);

/* (Session 72) Colour table n (1-15) for the sprites that name it; map
 * NULL: no table n again. */
void video_sprite_colours(Video *v, int n, const u8 map[16]);

/* The frame: SCREEN_W x SCREEN_H palette indices 0..63 (line * 16 +
 * colour), the colours being video_colour's. Changes nothing. */
void video_compose(const Video *v, u8 *out);

#endif /* HZ_VIDEO_H */
