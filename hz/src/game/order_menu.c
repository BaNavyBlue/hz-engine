/* The order menu (the $6DD4-$8662 module): C opens it over the mech's
 * view; up/down pick a row (unit type, order, radar mode), left/right
 * change it, A or B buys the unit (or re-orders the one being carried) or
 * toggles the radar; C closes it. While it is open the mech coasts (Mech_
 * Update gives it no direction) and its view shows the menu, with a radar
 * of the units round it or of the whole map.
 *
 * The menu draws into its view's plane (view A plane A, view B plane B; a
 * wide screen's one view plane A, with patterned panels either side) and
 * scrolls that plane to (0 or -$58, -$20) while it is open, keeping the
 * view's own scroll aside for the camera to move (mech.c). It draws itself
 * over 9 frames in split screen (one step a frame, OPEN_STEPS) and in one
 * frame on a wide screen; closing clears it and redraws the view from the
 * map (split: two rows a frame from the top and the bottom inwards).
 *
 * What the menu shows is kept beside what it stands for (row / row_shown,
 * type / type_shown ...): each frame the open menu redraws what changed.
 * Price, fee and cost are set by drawing them, so they are what the menu
 * last showed. A hidden menu (a wide screen's other mech: a CPU) draws
 * nothing; OrderMenu_Settle keeps those fields for it instead.
 *
 * (Session 59) A network game's own view (HzGame.own_view) hides the
 * rival's mech on this screen while the rival's own machine shows it, wide:
 * its menu runs here as a shown one does, unseen (muted() below), so the
 * two machines' games stay the same.
 *
 * QUIRK notes mark the original's behaviour kept as it is, CHANGED where
 * the port does what the code evidently means. The 68k labels are named
 * in comments; asm/hz.asm has the whole module with notes. */

#include "game.h"
#include "tables.h"

#include <string.h>


#define FONT_CURSOR     0xB7    /* the red arrow */
#define CLOSED_STEP     9       /* mech_menu_step with the menu closed */
#define CLOSE_STEPS     0x0F

/* -- small helpers ------------------------------------------------------ */

static bool carrying(const HzMech *m)
{
    return (m->flags & MECH_CARRYING) != 0;
}

static bool hidden(const HzMech *m)
{
    return m->view < 0;
}

/* (Session 59) The rival's menu in an own-view network game: hidden here,
 * shown wide on the rival's machine. What the menu shows decides when its
 * input is taken (row_shown, the price and cost drawn), so a hidden menu
 * (settled) and a shown one (drawn) buy on different frames. A muted menu
 * runs as the shown one, drawn whole at step 8, redrawn as it changes and
 * never settled, with nothing of it seen or heard here: what it draws goes
 * to g_mute_vdp, and its scroll, sounds and gauge bars are left out. */
static bool muted(const HzGame *g, const HzMech *m)
{
    return g->own_view != 0 && m->view < 0;
}

static Video g_mute_video;      /* what a muted menu draws: never shown */

/* A sound the menu makes, unless it is muted. */
static void menu_sfx(HzGame *g, const HzMech *m, u8 sfx)
{
    if (!muted(g, m)) {
        snd_queue_sfx(g, sfx);
    }
}

static u8 newly_pressed(const HzMech *m)
{
    return (u8)((m->pad ^ m->pad_prev) & m->pad);
}

/* The layer the menu draws on: view 0's layer A, any other layer B. */
static LayerId menu_layer(const HzMech *m)
{
    return (m->view == 0) ? LAYER_A : LAYER_B;
}

/* A cell of a layer (64 x 32 cells). */
typedef struct {
    LayerId layer;
    int col, row;
} At;

static At cell_at(LayerId layer, int col, int row)
{
    return (At){ layer, col, row };
}

/* n cells along the row from `at` (no menu row runs past the layer's
 * edge). */
static void put_cells(HzGame *g, At at, const u16 *words, int n)
{
    for (int i = 0; i < n; i++) {
        video_set_word(g->video, at.layer, at.col + i, at.row, words[i]);
    }
}

static void put_cell(HzGame *g, At at, u16 word)
{
    put_cells(g, at, &word, 1);
}

/* Tiles' pictures from tile `first` on (`bytes` / 32 of them). */
static void put_tiles(HzGame *g, u16 first, const u8 *src, u32 bytes)
{
    video_load_tiles(g->video, first, src, bytes / 32);
}

/* The tile whose 32 bytes are, on the cartridge, cells 0-15 of `layer`'s
 * row `row` (its tiles and the game screen's layers share VRAM: layer A
 * from tile $600, B from $700, the window from $780; four tiles a row). */
static u16 row_tile(LayerId layer, int row)
{
    static const u16 FIRST[LAYER_COUNT] = { 0x700, 0x600, 0x780 };
    return (u16)(FIRST[layer] + row * 4);
}

/* Half the layer (VDP_Fill2K's DMA fill of $800 bytes): the 16 rows from
 * `row`, clear. QUIRK: the radar keeps its tiles in those rows' unseen
 * cells (radar_tile_at), so the fill clears the radar's tiles there too.
 * Closing shows it: the top half's fill wipes the split view A radar's
 * top rows a frame before the bottom half's clears their cells, so the
 * radar goes blank for that frame. */
static void clear_rows(HzGame *g, LayerId layer, int row)
{
    video_fill_rows(g->video, layer, row, 16, cell_from_word(0));
    video_clear_tiles(g->video, row_tile(layer, row), 16 * 4);
}

/* The nametable word for a character of the game's font (FontTiles,
 * tiles $6B1-$6FF, palette line 1, in front): codes $B1 up are the font's
 * first tiles (frame pieces, the arrow, the cyan digits), the rest are
 * ASCII less $11. */
static u16 font_cell(u8 c)
{
    return (u16)((c >= 0xB1) ? 0xA600 + c : 0xA6A0 + c);
}

/* PrintChars / PrintInline / TextBuf_Print: n characters from (col, row). */
static void print_chars(HzGame *g, LayerId plane, int col, int row,
                        const u8 *text, int n)
{
    for (int i = 0; i < n; i++) {
        put_cell(g, cell_at(plane, col + i, row), font_cell(text[i]));
    }
}

static void print_text(HzGame *g, LayerId plane, int col, int row,
                       const char *text)
{
    print_chars(g, plane, col, row, (const u8 *)text, (int)strlen(text));
}

/* NumToDec5: five digits, leading zeros as spaces. QUIRK: the last digit
 * is a space too unless it or one before it isn't 0, so 0 prints blank. */
static void print_number5(HzGame *g, LayerId plane, int col, int row, u16 v)
{
    static const u16 POWERS[4] = { 10000, 1000, 100, 10 };
    u8 text[5];
    bool seen = false;
    for (int i = 0; i < 4; i++) {
        u8 d = (u8)(v / POWERS[i]);
        v = (u16)(v % POWERS[i]);
        if (d != 0) {
            seen = true;
        }
        text[i] = seen ? (u8)('0' + d) : ' ';
    }
    if (v != 0) {
        seen = true;
    }
    text[4] = seen ? (u8)('0' + v) : ' ';
    print_chars(g, plane, col, row, text, 5);
}

/* NumToDec2: two digits, zeros shown (100 and up run past '9'). */
static void print_number2(HzGame *g, LayerId plane, int col, int row, u8 v)
{
    u8 text[2] = { (u8)('0' + v / 10), (u8)('0' + v % 10) };
    print_chars(g, plane, col, row, text, 2);
}

/* The next type at or after `from` (or before, `step` -1), wrapping at 16,
 * whose bit is set in `mask` (not 0). */
static u8 next_in_mask(u16 mask, u8 from, int step)
{
    u8 t = (u8)(from & 0x0F);
    while (!(mask & (1u << t))) {
        t = (u8)((t + step) & 0x0F);
    }
    return t;
}

#define REPAIR_MENU_TILE 0x3CC  /* (session 72) the repair truck's
                                   picture: 9 of the 32 blank tiles */

static const HzUnitStats *type_stats(const HzGame *g, u8 type)
{
    return unit_stats_for(g, type);
}

/* -- drawing: the menu's parts ------------------------------------------ */

/* A unit's 3 x 3 picture (UnitMenuTiles; palette line 2 in P1's view, 3
 * in P2's) with its top-left cell at (col, row). */
static void draw_picture(HzGame *g, const HzMech *m, int col, int row,
                         u8 type)
{
    /* UNKNOWN: types past 9 would read past UnitMenuTiles; none is sold
     * or carried. */
    u16 tile = UNIT_MENU_TILES[type % 10];
    if (type == UNIT_REPAIR && repair_on(g)) {
        /* (session 72) REPAIR: the SUPPLY's picture in the truck's
         * colours. A layer's cells have no colour table (video.h), so
         * the 9 tiles go, recoloured, into the blank sprite tiles
         * ($3CC on: nothing draws them). */
        const u8 *colours = g->assets->repair.colours[0];
        const u8 *src = g->assets->sprite_tiles
                        + (UNIT_MENU_TILES[UNIT_SUPPLY] - 0x240) * 32;
        u8 pic[9 * 32];
        for (int i = 0; i < 9 * 32; i++) {
            pic[i] = (u8)((colours[src[i] >> 4] << 4)
                          | colours[src[i] & 0x0F]);
        }
        tile = REPAIR_MENU_TILE;
        put_tiles(g, REPAIR_MENU_TILE, pic, sizeof pic);
    }
    tile = (u16)(tile | ((m->view_index != 0) ? 0x6000 : 0x4000));
    for (int r = 0; r < 3; r++) {
        u16 cells[3] = { (u16)(tile + r), (u16)(tile + r + 3),
                         (u16)(tile + r + 6) };
        put_cells(g, cell_at(menu_layer(m), col, row + r), cells, 3);
    }
}

/* A 2 x 2 icon whose tiles are at `tile` (down each column). */
static void draw_icon_cells(HzGame *g, LayerId plane, int col, int row, u16 tile)
{
    u16 top[2] = { tile, (u16)(tile + 2) };
    u16 bottom[2] = { (u16)(tile + 1), (u16)(tile + 3) };
    put_cells(g, cell_at(plane, col, row), top, 2);
    put_cells(g, cell_at(plane, col, row + 1), bottom, 2);
}

/* OrderMenu_DrawCursor: the arrow on the current row. Carrying, row 0
 * (the type row isn't offered) becomes 1. CHANGED: so does any row past 2
 * (a hidden-then-shown stale row taken up past 0, see order_menu_input):
 * the original reads OrderMenuCursorPos past its end and draws the arrow
 * where those code bytes say. */
static void draw_cursor(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    u8 row = mn->row;
    if (carrying(m) && (row == 0 || row > 2)) {
        row = 1;
    }
    mn->row = row;
    mn->row_shown = row;
    if (row > 2) {
        return;                     /* not carrying: Input keeps 0-2 */
    }
    put_cell(g, cell_at(menu_layer(m), ORDER_MENU_CURSOR_POS[row][0],
                          ORDER_MENU_CURSOR_POS[row][1]),
             font_cell(FONT_CURSOR));
}

/* OrderMenu_EraseCursor */
static void erase_cursor(HzGame *g, const HzMech *m)
{
    u8 row = m->menu.row_shown;
    if (row > 2) {
        return;                     /* never drawn there (draw_cursor) */
    }
    put_cell(g, cell_at(menu_layer(m), ORDER_MENU_CURSOR_POS[row][0],
                          ORDER_MENU_CURSOR_POS[row][1]), font_cell(' '));
}

/* OrderMenu_DrawUnitPic: the picked type's picture at (2, 2), its name at
 * (6, 2) and price at (7, 3), and that price as mech_menu_price. Carrying:
 * the price is 0 and nothing is drawn (the carried unit is shown below). */
static void draw_unit_choice(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    if (carrying(m)) {
        mn->price = 0;
        return;
    }
    u8 type = mn->type;
    mn->type_shown = type;
    draw_picture(g, m, 2, 2, type);
    const HzUnitStats *st = type_stats(g, type);
    print_chars(g, menu_layer(m), 6, 2, st->name, 8);
    mn->price = st->price;
    print_number5(g, menu_layer(m), 7, 3, mn->price);
}

/* OrderMenu_DrawOrder: the picked order's icon (to its tiles, $6A1 or
 * view B's $6A9), its name at (6, 5) and fee at (7, 6), and that fee as
 * mech_order_fee. */
static void draw_order_choice(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    u8 order = mn->order;
    mn->order_shown = order;
    /* UNKNOWN: an order past $F would read past OrderInfo / HUDIcons; the
     * menu keeps it within 0-$F. */
    const HzOrderInfo *info = order_info_for(g, order);    /* (s. 72) */
    put_tiles(g, (m->view == 0) ? 0x6A1 : 0x6A9,
              order_icon_for(g, order), HZ_ORDER_ICON_BYTES);
    print_chars(g, menu_layer(m), 6, 5, info->name, HZ_ORDER_NAME);
    mn->fee = info->fee;
    print_number5(g, menu_layer(m), 7, 6, mn->fee);
}

/* OrderMenu_DrawOrderSel (opening): the order made one the picked type
 * takes (the next up), drawn, and its icon's cells at (2, 5). */
static void draw_order_row(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    u16 orders = type_stats(g, mn->type & 0x0F)->orders;
    if (orders != 0 && !(orders & (1u << (mn->order & 0x0F)))) {
        mn->order = next_in_mask(orders, (u8)(mn->order + 1), +1);
    }
    draw_order_choice(g, m);
    draw_icon_cells(g, menu_layer(m), 2, 5,
                    (m->view == 0) ? 0x26A1 : 0x26A9);
}

/* OrderMenu_DrawCost: price + fee at (7, 8): mech_order_cost. */
static void draw_cost(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    mn->cost = (u16)(mn->price + mn->fee);
    print_number5(g, menu_layer(m), 7, 8, mn->cost);
}

/* OrderMenu_DrawUnitCount: the units of the side the view shows at
 * ($B, $B), two digits (the count a byte on the 68k). (Session 61) With
 * UNITS a side can have 100 and more: then the number ends at column $C
 * and takes the two blank columns before it (9-$A; the money's G is at 8),
 * up to four digits, then thousands and a K ("10K" from 10,000, "999K" at
 * most); back under 100 those two are blank again. Off, or under 100 both
 * times: the original's. */
static void draw_unit_count(HzGame *g, HzMech *m)
{
    u32 count = g->unit_count[(m->view_index != 0) ? 1 : 0];
    u32 was = m->menu.unit_count_shown;
    m->menu.unit_count_shown = count;
    if (!units_unlimited(g) || (count < 100 && was < 100)) {
        print_number2(g, menu_layer(m), 0x0B, 0x0B, (u8)count);
        return;
    }
    u32 v = count;
    u8 text[4] = { ' ', ' ', ' ', ' ' };
    int i = 3;
    if (v > 9999) {
        v = (v > 999999) ? 999 : v / 1000;
        text[i--] = 'K';
    }
    do {
        text[i--] = (u8)('0' + v % 10);
        v /= 10;
    } while (v != 0 && i >= 0);
    if (i == 2) {
        text[2] = '0';                  /* two digits at least, "07" */
    }
    print_chars(g, menu_layer(m), 0x09, 0x0B, text, 4);
}

/* OrderMenu_DrawMoney: at (2, $B). */
static void draw_money(HzGame *g, HzMech *m)
{
    m->menu.money_shown = m->money;
    print_number5(g, menu_layer(m), 2, 0x0B, m->money);
}

/* OrderMenu_DrawRadarMode: the mode's sign at (8, $D); the radar's
 * refresh starts over. */
static void draw_radar_mode(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    mn->radar_step = 0;
    mn->radar_mode_shown = mn->radar_mode;
    put_cell(g, cell_at(menu_layer(m), 8, 0x0D),
             font_cell((mn->radar_mode != 0) ? 0x2E : 0x2D));
}

/* OrderMenu_DrawCarriedIcon: the carried unit's order icon into its
 * tiles ($6A5, view B's $6AD). Not carrying: nothing. */
static void draw_carried_icon(HzGame *g, HzMech *m)
{
    if (!carrying(m)) {
        return;
    }
    m->menu.carried_order_shown = m->carried_order;
    put_tiles(g, (m->view == 0) ? 0x6A5 : 0x6AD,
              order_icon_for(g, m->carried_order), HZ_ORDER_ICON_BYTES);
}

/* OrderMenu_DrawCarriedGauges: the bars (HUD_QueueBar, 5 cells each, at
 * most 20 quarters) beside the carried unit, for the bits of `which`: b0
 * HP / 16 at row $13, b1 fuel / 64 at $14, b2 ammo / 4 at $15, b3
 * missiles / 4 at $16, column $C; then mech_carried_refill 0. */
static void draw_carried_gauges(HzGame *g, HzMech *m, u8 which)
{
    const HzUnit *u = carried_unit(g, m);
    const HzUnitStats *st = type_stats(g, u->type);
    u16 value[4] = { (u16)(u->hp >> 4), (u16)(u->fuel >> 6),
                     (u16)(u->ammo >> 2), (u16)(u->missiles >> 2) };
    u16 max[4] = { (u16)(st->hp >> 4), (u16)(st->fuel >> 6),
                   (u16)(st->ammo >> 2), (u16)(st->missiles >> 2) };
    for (int i = 0; i < 4 && !muted(g, m); i++) {
        if (which & (1u << i)) {
            hud_queue_bar(g, value[i], (max[i] > 0x14) ? 0x14 : max[i], 5,
                          menu_layer(m), 0x0C, 0x13 + i);
        }
    }
    m->carried_refill = 0;
}

/* OrderMenu_DrawCarried: carrying, the carried unit (its type becomes the
 * menu's) at ($B, $10) with its order icon's cells at ($E, $10), the
 * gauges' letters D E G M down column $B from row $13, and all four bars. */
static void draw_carried(HzGame *g, HzMech *m)
{
    if (!carrying(m)) {
        return;
    }
    HzOrderMenu *mn = &m->menu;
    u8 type = carried_unit(g, m)->type;
    mn->type = type;
    mn->type_shown = type;
    draw_picture(g, m, 0x0B, 0x10, type);
    draw_carried_icon(g, m);
    LayerId plane = menu_layer(m);
    draw_icon_cells(g, plane, 0x0E, 0x10, (m->view == 0) ? 0x26A5 : 0x26AD);
    static const char LETTERS[4] = { 'D', 'E', 'G', 'M' };
    for (int i = 0; i < 4; i++) {
        put_cell(g, cell_at(plane, 0x0B, 0x13 + i),
                 font_cell((u8)LETTERS[i]));
    }
    draw_carried_gauges(g, m, 0x0F);
}

/* OrderMenu_Open3 (OrderMenu_Open3B for view B): the frame's top and
 * bottom (rows 1 and 9), the labels ("UNIT", "RADAR"), the "0G" after
 * each price (a fixed 0, prices being in tens, and a G), the carried unit. */
static void draw_labels(HzGame *g, HzMech *m)
{
    LayerId plane = menu_layer(m);
    u8 top[16];
    u8 bottom[16];
    memset(top, 0xB5, sizeof top);
    memset(bottom, 0xB5, sizeof bottom);
    top[0] = 0xB1;
    top[15] = 0xB2;
    bottom[0] = 0xB3;
    bottom[15] = 0xB4;
    print_chars(g, plane, 1, 1, top, 16);
    print_chars(g, plane, 1, 9, bottom, 16);
    print_text(g, plane, 0x0D, 0x0B, "()*+");
    print_text(g, plane, 7, 0x0B, "0/");
    print_text(g, plane, 2, 0x0D, "'$%$'");
    if (carrying(m)) {
        print_text(g, plane, 0x0B, 0x0E, "GETNOW");
    } else {
        print_text(g, plane, 0x0C, 3, "0/");
    }
    print_text(g, plane, 0x0C, 6, "0/");
    print_text(g, plane, 0x0C, 8, "0/");
    draw_carried(g, m);
}

/* OrderMenu_DrawBorder: the frame round the radar (cells 1-$A, rows
 * $E-$17); carrying, a second box on its right round the carried unit
 * ($A-$11, rows $F-$17). */
static void fill_row(HzGame *g, At at, u16 word, int n)
{
    for (int i = 0; i < n; i++) {
        put_cell(g, cell_at(at.layer, at.col + i, at.row), word);
    }
}

static void fill_column(HzGame *g, At at, u16 word, int n)
{
    for (int i = 0; i < n; i++) {
        put_cell(g, cell_at(at.layer, at.col, at.row + i), word);
    }
}

static void draw_border(HzGame *g, HzMech *m)
{
    /* cmpi.b #1: only view 1 is plane B here (the rest test for 0) */
    LayerId p = (m->view == 1) ? LAYER_B : LAYER_A;
    put_cell(g, cell_at(p, 1, 0x0E), 0x25FC);
    fill_row(g, cell_at(p, 2, 0x0E), 0x25FD, 8);
    put_cell(g, cell_at(p, 0x0A, 0x0E), 0x2DFC);
    fill_column(g, cell_at(p, 0x0A, 0x0F), 0x25AD, 8);
    put_cell(g, cell_at(p, 0x0A, 0x17), 0x25AE);
    fill_row(g, cell_at(p, 2, 0x17), 0x35FD, 8);
    put_cell(g, cell_at(p, 1, 0x17), 0x35FC);
    fill_column(g, cell_at(p, 1, 0x0F), 0x2DFE, 8);
    if (!carrying(m)) {
        fill_column(g, cell_at(p, 0x0A, 0x0F), 0x25FE, 8);
        put_cell(g, cell_at(p, 0x0A, 0x17), 0x3DFC);
        return;
    }
    put_cell(g, cell_at(p, 0x0A, 0x0F), 0x25AC);      /* .carrying */
    fill_row(g, cell_at(p, 0x0B, 0x0F), 0x25FD, 6);
    put_cell(g, cell_at(p, 0x11, 0x0F), 0x2DFC);
    fill_column(g, cell_at(p, 0x11, 0x10), 0x25FE, 7);
    put_cell(g, cell_at(p, 0x11, 0x17), 0x3DFC);
    fill_row(g, cell_at(p, 0x0B, 0x17), 0x35FD, 6);
}

/* OrderMenu_DrawSidePanels (wide screen): the patterned panels in plane A
 * columns $35-$3E and $13-$1C (left and right of the menu as the plane is
 * scrolled), SidePanelTiles' 4 rows 6 times down. */
static void draw_side_panels(HzGame *g)
{
    static const int COLS[2] = { 0x35, 0x13 };
    for (int s = 0; s < 2; s++) {
        for (int r = 0; r < 24; r++) {
            put_cells(g, cell_at(LAYER_A, COLS[s], r),
                      SIDE_PANEL_TILES[r % 4], 10);
        }
    }
}

/* -- the radar ------------------------------------------------------------ */

/* The radar image is 64 x 64 pixels, 8 x 8 tiles kept in the mech's
 * buffer (v_radar_buf1, view 1's v_radar_buf2) in four pages of two tile
 * rows. Mode 0 (units near the mech) starts each refresh from RadarBlank
 * and plots the units in the 32 x 32 grid cells round it, 2 x 2 pixels a
 * cell; mode 1 (the map) is the map's radar image, with the posts and the
 * mechs as sprites. The tiles go into VRAM nobody else uses while the menu
 * is open: split view A, plane A's unseen columns 32-63; otherwise the
 * block at $EC00 and the window plane's spare cells. */

static u8 *radar_buffer(HzGame *g, const HzMech *m)
{
    return g->radar_buf[(m->view == 0) ? 0 : 1];
}

/* Radar_LoadPage: page `page` of the buffer from RadarBlank or the map's
 * image. */
static void radar_load_page(HzGame *g, const HzMech *m, int page)
{
    const u8 *src = (m->menu.radar_mode != 0)
                  ? g->assets->map[g->map & 7].radar
                  : g->assets->sprite_tiles + (0x4C0 - 0x240) * 32;
    memcpy(radar_buffer(g, m) + page * 0x200, src + page * 0x200, 0x200);
}

/* Units in grid rows first..first+count-1 of the 32 x 32 round the mech's
 * cell, as 2 x 2 dots: colour 5 for side 1, 3 for side 2. */
static void radar_plot_rows(HzGame *g, const HzMech *m, int first, int count)
{
    u8 *buf = radar_buffer(g, m);
    int cx = (u16)m->x >> 5;
    int cy = (u16)m->y >> 5;
    for (int r = first; r < first + count; r++) {
        int gy = cy - 16 + r;
        for (int c = 0; c < 32; c++) {
            int gx = cx - 16 + c;
            if (gx < 0 || gx >= HZ_GRID || gy < 0 || gy >= HZ_GRID) {
                continue;
            }
            int side;
            if (g->crowd) {
                /* (Session 61) UNITS: the side with more units there */
                u16 cell = (u16)(gy * HZ_GRID + gx);
                u32 n0 = g->crowd_count[0][cell];
                u32 n1 = g->crowd_count[1][cell];
                if ((n0 | n1) == 0) {
                    continue;
                }
                side = n1 > n0;
            } else {
                HzRef id = g->grid[gy * HZ_GRID + gx];
                if (id.kind != REF_UNIT) {
                    continue;                           /* not a unit */
                }
                side = id.side;
            }
            u8 dot = side ? 0x33 : 0x55;
            int off = ((r & 0x1C) << 6) + ((r & 3) << 3)
                    + ((c & 0x1C) << 3) + (c & 3);
            buf[off & 0x7FF] = dot;
            buf[(off & 0x7FF) + 4] = dot;
        }
    }
}

/* Radar_PlaceMechs (mode 0): this mech's marker at the radar's centre,
 * the rival's by their distance / 16 if it is within 32 of that each way
 * (else y 0: not shown). Sprite coordinates. */
static void radar_place_mechs(HzGame *g, HzMech *m)
{
    /* the two records by m's side, as the original picks them */
    int side = (m->flags & MECH_TEAM2) ? 1 : 0;
    const HzMech *own = &g->mech[side];
    const HzMech *other = &g->mech[side ^ 1];
    u16 x0 = g->split_screen ? (side ? 0xB0 : 0) : 0x58;
    x0 = (u16)(x0 + 0xAC);
    m->radar_own_y = 0x134;
    m->radar_own_x = x0;
    s16 dx = (s16)(((u16)other->x >> 4) - ((u16)own->x >> 4));
    s16 dy = (s16)(((u16)other->y >> 4) - ((u16)own->y >> 4));
    if (dx <= -0x20 || dx >= 0x20 || dy <= -0x20 || dy >= 0x20) {
        m->radar_foe_y = 0;                             /* .foe_off */
        return;
    }
    m->radar_foe_y = (u16)(dy + 0x134);
    m->radar_foe_x = (u16)(dx + x0);
}

/* Radar_PlotMap (mode 1): the 18 posts as radar sprites at their position
 * / 32 (outposts: neutral $5A2, P1's $5A0, P2's $5A1; HQs $59E/$59F; an
 * empty record y 0), and both mechs' markers likewise. */
static void radar_plot_map(HzGame *g, HzMech *m)
{
    int side = (m->flags & MECH_TEAM2) ? 1 : 0;
    u16 x0 = g->split_screen ? (side ? 0xB0 : 0) : 0x58;
    HzSprite *spr = g->radar_sprites[side];
    for (int i = 0; i < HZ_RADAR_SPRITES; i++) {
        const HzPost *p = &g->posts[i];
        if (!(p->flags & POST_USED)) {
            spr[i].y = 0;
            continue;
        }
        u16 tile;
        if (!(p->flags & POST_OWNED)) {
            tile = 0xA5A2;                              /* no owner */
        } else {
            tile = (p->flags & POST_HQ) ? 0xA59E : 0xA5A0;
            tile = (u16)(tile + (p->flags & POST_TEAM2));
        }
        spr[i].y = (u16)(((u16)p->y >> 5) + 0x115);
        spr[i].size_link = 0;
        spr[i].attr = tile;
        spr[i].x = (u16)(((u16)p->x >> 5) + x0 + 0x8D);
    }
    const HzMech *own = &g->mech[side];                 /* .mechs */
    const HzMech *other = &g->mech[side ^ 1];
    m->radar_own_y = (u16)(((u16)own->y >> 5) + 0x114);
    m->radar_own_x = (u16)(((u16)own->x >> 5) + x0 + 0x8C);
    m->radar_foe_y = (u16)(((u16)other->y >> 5) + 0x114);
    m->radar_foe_x = (u16)(((u16)other->x >> 5) + x0 + 0x8C);
}

/* Radar_PlotUnits (opening): the whole radar for the mode. */
static void radar_plot_all(HzGame *g, HzMech *m)
{
    if (m->menu.radar_mode != 0) {
        radar_plot_map(g, m);
        return;
    }
    radar_plot_rows(g, m, 0, 32);
    radar_place_mechs(g, m);
}

/* Radar_PlotRows: eight rows (part 0-3) of mode 0, the markers after the
 * first; mode 1 the whole map plot. */
static void radar_plot_part(HzGame *g, HzMech *m, int part)
{
    if (m->menu.radar_mode != 0) {
        radar_plot_map(g, m);
        return;
    }
    radar_plot_rows(g, m, part * 8, 8);
    if (part == 0) {
        radar_place_mechs(g, m);
    }
}

/* The tile buffer tile n (0-63) goes to. The radar's tiles take tile
 * numbers whose 32 bytes are cells of the planes nothing shows while the
 * menu is open (on the cartridge tiles and the planes' cells share VRAM):
 * split view A's in layer A's columns 32-63, two tiles to a row of cells
 * ($602 on); the others in layer B's rows 24-31 ($760-$77F) and the
 * window layer's column 48 on, a tile to a row ($783 on, every fourth). */
static u16 radar_tile_at(const HzGame *g, const HzMech *m, int n)
{
    if (g->split_screen && m->view == 0) {
        return (u16)(0x602 + (n / 16) * 0x20 + ((n % 16) / 2) * 4 + (n % 2));
    }
    if (n < 32) {
        return (u16)(0x760 + n);
    }
    return (u16)(0x783 + (n - 32) * 4);
}

/* The tile number the radar's cell (col, row) shows (Radar_DrawFrame's
 * order: as radar_tile_at put them). */
static u16 radar_tile(const HzGame *g, const HzMech *m, int col, int row)
{
    return radar_tile_at(g, m, row * 8 + col);
}

/* Radar_UploadPage: page `page`'s 16 tiles into place. */
static void radar_upload_page(HzGame *g, const HzMech *m, int page)
{
    /* the split view A path always takes buffer 1; the others take view
     * 1's buffer 2 in split screen, else buffer 1 */
    const u8 *buf = (g->split_screen && m->view != 0) ? g->radar_buf[1]
                                                      : g->radar_buf[0];
    for (int n = page * 16; n < page * 16 + 16; n++) {
        put_tiles(g, radar_tile_at(g, m, n), buf + n * 32, 32);
    }
}

/* Radar_DrawFrame / Radar_DrawPage: the radar's cells (rows first..last)
 * at (2, $F) on the menu's plane; palette line 1 in mode 0, 0 in mode 1. */
static void radar_draw_rows(HzGame *g, const HzMech *m, int first, int count)
{
    LayerId plane = (g->split_screen && m->view != 0) ? LAYER_B : LAYER_A;
    u16 pal = (m->menu.radar_mode != 0) ? 0 : 0x2000;
    for (int r = first; r < first + count; r++) {
        u16 cells[8];
        for (int c = 0; c < 8; c++) {
            cells[c] = (u16)(radar_tile(g, m, c, r) | pal);
        }
        put_cells(g, cell_at(plane, 2, 0x0F + r), cells, 8);
    }
}

/* OrderMenu_RefreshRadar (from order_menu_input): steps 0-3 reload a page
 * of the buffer, 4-7 plot a quarter of the rows. */
static void radar_refresh(HzGame *g, HzMech *m)
{
    u8 step = m->menu.radar_step;
    if (step < 4) {
        radar_load_page(g, m, step);
    } else if (step < 8) {
        radar_plot_part(g, m, step - 4);
    }
}

/* OrderMenu_RadarStep (end of OrderMenu_Redraw): steps 8-$B upload and
 * redraw a page; then the next step, 0 after $B. */
static void radar_step(HzGame *g, HzMech *m)
{
    u8 step = m->menu.radar_step;
    if (step >= 8) {
        radar_upload_page(g, m, step - 8);
        radar_draw_rows(g, m, (step - 8) * 2, 2);
    }
    step++;
    if (step >= 0x0C) {
        step = 0;
    }
    m->menu.radar_step = step;
}

/* -- settling a hidden menu ----------------------------------------------- */

/* OrderMenu_Settle: a hidden menu does here what drawing does for a shown
 * one: the price (0 for a carried unit), the order made one the unit takes
 * (the next up), fee, cost, and the shown row, type and order. A shown
 * menu: nothing. */
static void settle(HzGame *g, HzMech *m)
{
    if (!hidden(m) || muted(g, m)) {
        return;
    }
    HzOrderMenu *mn = &m->menu;
    u16 price;
    u16 orders;
    if (!carrying(m)) {
        const HzUnitStats *st = type_stats(g, mn->type);
        price = st->price;
        orders = st->orders;
    } else {
        price = 0;                                      /* .carried */
        orders = type_stats(g, carried_unit(g, m)->type)->orders;
    }
    mn->price = price;
    if (orders != 0 && !(orders & (1u << (mn->order & 0x0F)))) {
        mn->order = next_in_mask(orders, (u8)(mn->order + 1), +1);
    }
    mn->fee = order_info_for(g, mn->order)->fee;        /* .fee */
    mn->cost = (u16)(price + mn->fee);
    mn->row_shown = mn->row;
    mn->type_shown = mn->type;
    mn->order_shown = mn->order;
}

/* -- input ---------------------------------------------------------------- */

static void refused(HzGame *g)
{
    snd_queue_sfx(g, SFX_REFUSED);
}

/* .buy: carrying, re-order the carried unit; else order a new one. */
static void buy(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    if (carrying(m)) {
        if (m->money < mn->cost) {
            mn->reorder_state = 0xFF;                   /* .reorder_short */
            if (!hidden(m)) {
                refused(g);
            }
            return;
        }
        mn->reorder_state = 2;
        m->money = (u16)(m->money - mn->cost);
        m->carried_order = mn->order;
        if (!hidden(m)) {
            snd_queue_sfx(g, SFX_ORDERED);
        }
        return;
    }
    if (!side_has_room(g, (m->flags & MECH_TEAM2) ? 1 : 0)) {
        refused(g);         /* QUIRK: even for a hidden menu (the others
                               are silent for it) */
        return;
    }
    /* Not ported: with v_record_mode b0 (the development recorder) the
     * unit is put down at once by the mech (Unit_Spawn). */
    if ((s8)m->prod_state > 0) {    /* building (1) or ready (2) */
        if (!hidden(m)) {                               /* .produce */
            refused(g);
        }
        return;
    }
    m->prod_type = mn->type;                            /* .place_order */
    m->prod_order = mn->order;
    if (m->money < mn->cost) {
        m->prod_state = 0xFF;
        m->prod_timer = (u16)(mn->cost - m->money);
        if (!hidden(m)) {
            refused(g);
        }
        return;
    }
    m->prod_state = 1;                                  /* .paid */
    m->money = (u16)(m->money - mn->cost);
    m->prod_timer = (u16)(type_stats(g, mn->type)->build_time << 7);
    if (!hidden(m)) {
        snd_queue_sfx(g, SFX_ORDERED);
    }
}

/* OrderMenu_Input ($733E): from mech_update while the menu is open. */
void order_menu_input(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    if (g->menus_frozen) {
        return;
    }
    if (g->split_screen || m->view == 0) {
        radar_refresh(g, m);                            /* .refresh_radar */
    }
    u8 pressed = newly_pressed(m);

    u8 row = mn->row;                                   /* .rows */
    if (!carrying(m)) {
        if (pressed & HZ_PAD_UP) {
            row = (row == 0) ? 2 : (u8)(row - 1);
        }
        if (pressed & HZ_PAD_DOWN) {
            row++;
            if (row > 2) {
                row = 0;
            }
        }
    } else {
        /* .carrying_rows: rows 1-2. QUIRK: from row 0 (left from before
         * the unit was carried, in a hidden menu or a split screen's first
         * frames) up gives $FF. */
        if (pressed & HZ_PAD_UP) {
            row--;
            if (row == 0) {
                row = 2;
            }
        }
        if (pressed & HZ_PAD_DOWN) {
            row++;
            if (row > 2) {
                row = 1;
            }
        }
    }
    mn->row = row;                                      /* .row_set */
    if (row != mn->row_shown) {
        settle(g, m);           /* a row change: nothing else this frame */
        return;
    }

    if (row == MENU_ROW_TYPE) {
        u16 buyable = shop_types(g);    /* (session 60) with TURRETS */
        if (buyable != 0 && (pressed & (HZ_PAD_LEFT | HZ_PAD_RIGHT))) {
            u8 type = (pressed & HZ_PAD_LEFT)
                    ? next_in_mask(buyable, (u8)(mn->type - 1), -1)
                    : next_in_mask(buyable, (u8)(mn->type + 1), +1);
            mn->type = type;                            /* .type_set */
            u16 orders = type_stats(g, type)->orders;
            if (orders != 0) {
                if (!(orders & (1u << (mn->order & 0x0F)))) {
                    mn->order = next_in_mask(orders, (u8)(mn->order - 1), -1);
                }
                settle(g, m);                           /* .changed */
                return;
            }
            /* a type with no orders (none is sold): on to A/B */
        }
    } else if (row == MENU_ROW_ORDER) {
        /* QUIRK for a hidden menu carrying a unit: these are the menu
         * type's orders, which needn't be the carried unit's (settle then
         * moves the order to one the carried unit takes). */
        u16 orders = type_stats(g, mn->type)->orders;
        if (orders != 0 && (pressed & (HZ_PAD_LEFT | HZ_PAD_RIGHT))) {
            mn->order = (pressed & HZ_PAD_LEFT)
                      ? next_in_mask(orders, (u8)(mn->order - 1), -1)
                      : next_in_mask(orders, (u8)(mn->order + 1), +1);
            settle(g, m);
            return;
        }
    } else if (row == MENU_ROW_RADAR) {
        if (pressed & (HZ_PAD_LEFT | HZ_PAD_RIGHT)) {
            mn->radar_mode ^= 1;
            settle(g, m);
            return;
        }
    }

    if (!(pressed & (HZ_PAD_A | HZ_PAD_B))) {           /* .buttons */
        return;
    }
    if (row == MENU_ROW_TYPE || row == MENU_ROW_ORDER) {
        buy(g, m);                                      /* .act */
    } else {
        mn->radar_mode ^= 1;    /* .toggle_radar (rows past 2 too) */
    }
    settle(g, m);
}

/* -- opening and closing -------------------------------------------------- */

/* OrderMenu_Open0 / Open0B: the view's scroll set aside and the plane put
 * at (0, -$20) (a wide screen's at -$58: the menu centred), its top half
 * cleared, radar pages 0-1 loaded. */
static void open_clear_top(HzGame *g, HzMech *m)
{
    int plane = (m->view == 0) ? 0 : 1;
    if (!muted(g, m)) {
        m->menu.scroll_x = g->scroll_x[plane];
        m->menu.scroll_y = g->scroll_y[plane];
        g->scroll_x[plane] = (s16)((plane == 0 && !g->split_screen) ? -0x58
                                                                    : 0);
        g->scroll_y[plane] = -0x20;
    }
    clear_rows(g, menu_layer(m), 0);
    radar_load_page(g, m, 0);
    radar_load_page(g, m, 1);
}

/* OrderMenu_Open1 / Open1B: the bottom half cleared, pages 2-3 loaded. */
static void open_clear_bottom(HzGame *g, HzMech *m)
{
    clear_rows(g, menu_layer(m), 16);
    radar_load_page(g, m, 2);
    radar_load_page(g, m, 3);
}

/* One of the nine opening steps (OrderMenuOpenSteps, ...StepsB). */
static void open_part(HzGame *g, HzMech *m, int part)
{
    switch (part) {
    case 0:
        open_clear_top(g, m);
        break;
    case 1:
        open_clear_bottom(g, m);
        break;
    case 2:
        radar_plot_all(g, m);
        break;
    case 3:
        draw_labels(g, m);
        break;
    case 4:                                             /* OrderMenu_Open4 */
        draw_unit_choice(g, m);
        draw_cost(g, m);
        draw_unit_count(g, m);
        break;
    case 5:                                             /* OrderMenu_Open5 */
        draw_order_row(g, m);
        draw_money(g, m);
        radar_upload_page(g, m, 0);
        break;
    case 6:
        radar_upload_page(g, m, 1);
        radar_upload_page(g, m, 2);
        break;
    case 7:                                             /* OrderMenu_Open7 */
        radar_upload_page(g, m, 3);
        radar_draw_rows(g, m, 0, 8);
        draw_cursor(g, m);
        draw_radar_mode(g, m);
        menu_sfx(g, m, SFX_MENU_OPEN);
        break;
    case 8:
        draw_border(g, m);
        break;
    default:
        break;
    }
}

/* OrderMenu_Redraw: the open menu redraws what changed since it was last
 * drawn, then the radar's step. */
static void redraw(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    if (!g->split_screen && m->view != 0 && !muted(g, m)) {
        return;                                         /* .hidden */
    }
    if (mn->row != mn->row_shown) {
        erase_cursor(g, m);
        draw_cursor(g, m);
        menu_sfx(g, m, SFX_MENU_MOVE);
    }
    if (mn->type != mn->type_shown) {                   /* .type */
        draw_unit_choice(g, m);
        menu_sfx(g, m, SFX_MENU_MOVE);
    }
    if (mn->order != mn->order_shown) {                 /* .order */
        draw_order_choice(g, m);
        menu_sfx(g, m, SFX_MENU_MOVE);
    }
    if ((u16)(mn->price + mn->fee) != mn->cost) {       /* .cost */
        draw_cost(g, m);
    }
    if (g->unit_count[(m->view_index != 0) ? 1 : 0] != mn->unit_count_shown) {
        draw_unit_count(g, m);
    }
    if (m->money != mn->money_shown) {                  /* .money */
        draw_money(g, m);
    }
    if (m->carried_order != mn->carried_order_shown) {  /* .carried_icon */
        draw_carried_icon(g, m);
    }
    if (carrying(m) && m->carried_refill != 0) {        /* .gauges */
        draw_carried_gauges(g, m, m->carried_refill);
    }
    if (mn->radar_mode != mn->radar_mode_shown) {       /* .radar_mode */
        draw_radar_mode(g, m);
        menu_sfx(g, m, SFX_MENU_MOVE);
        return;                 /* no radar step this frame */
    }
    radar_step(g, m);
}

/* OrderMenu_Step: opening, one step a frame in split screen (counting
 * mech_menu_step 9 down to 0), every step at once on a wide screen; a
 * hidden menu settles at 0. Fully open: C starts the close, else redraw. */
static void open_step(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    if (mn->step == 0) {
        if (newly_pressed(m) & HZ_PAD_C) {              /* .open */
            mn->step = CLOSE_STEPS;     /* OrderMenu_StartClose */
            mn->closing++;
        } else {
            redraw(g, m);
        }
        return;
    }
    mn->step--;
    if (g->split_screen) {
        open_part(g, m, 8 - mn->step);  /* OrderMenu_OpenSplit */
        return;
    }
    if (m->view == 0 || muted(g, m)) {
        if (mn->step == 8) {                            /* .wide_all */
            for (int part = 0; part <= 8; part++) {
                open_part(g, m, part);
            }
            draw_side_panels(g);
        }
        return;
    }
    if (mn->step == 0) {
        settle(g, m);
    }
}

/* OrderMenu_Closed */
static void closed(HzMech *m)
{
    m->flags &= (u8)~MECH_MENU_OPEN;
    m->menu.step = CLOSED_STEP;
    m->menu.closing = 0;
}

/* OrderMenu_Close0 / Close0B: the plane's top half cleared and the view's
 * scroll back (to 8-15 within its tile: the view is redrawn from its
 * corner). Close1 / Close1B: the bottom half cleared. */
static void close_clear_top(HzGame *g, HzMech *m)
{
    int plane = (m->view == 0) ? 0 : 1;
    clear_rows(g, menu_layer(m), 0);
    g->scroll_x[plane] = (s16)((m->menu.scroll_x & 7) + 8);
    g->scroll_y[plane] = (s16)((m->menu.scroll_y & 7) + 8);
}

static void close_clear_bottom(HzGame *g, HzMech *m)
{
    clear_rows(g, menu_layer(m), 16);
}

/* OrderMenu_CloseStep: mech_menu_step counts $F down. Split screen, one
 * part a frame (index $E - step): the halves cleared, then the view's 26
 * rows redrawn two a frame, from the top and the bottom inwards
 * (OrderMenu_CloseRedrawRows, OrderMenu_CloseLast); a wide screen clears
 * and redraws the whole view at once; a hidden menu just counts. Closed
 * at the end. */
static void close_step(HzGame *g, HzMech *m)
{
    HzOrderMenu *mn = &m->menu;
    mn->step--;
    if (g->split_screen) {
        int index = 0x0E - mn->step;
        if (index == 0) {
            close_clear_top(g, m);
        } else if (index == 1) {
            close_clear_bottom(g, m);
        } else if (index < 0x0E) {
            int row = (index - 2) & 0xFE;
            if (index & 1) {
                row = 0x18 - row;
            }
            map_redraw_view_rows(g, m, row, 2);
        } else {
            map_redraw_view_rows(g, m, 0x0C, 2);    /* OrderMenu_CloseLast */
            closed(m);
        }
        return;
    }
    if (m->view == 0) {                         /* OrderMenu_CloseViewA */
        if (mn->step == 0) {
            closed(m);
        } else if (mn->step == 0x0E) {
            close_clear_top(g, m);
            close_clear_bottom(g, m);
            map_redraw_view_rows(g, m, 0, 0x1A);
        }
        return;
    }
    if (mn->step == 0) {
        closed(m);
    }
}

/* OrderMenu_UpdateBoth ($6DD4), at the frame's end: each mech's open menu
 * steps its opening, redraw or close; a destroyed mech's menu closes. */
void order_menu_update(HzGame *g)
{
    if (g->menus_frozen) {
        return;
    }
    for (int p = 0; p < 2; p++) {
        HzMech *m = &g->mech[p];
        if (!(m->flags & MECH_MENU_OPEN)) {
            continue;
        }
        Video *shown = g->video;
        if (muted(g, m)) {
            g->video = &g_mute_video;       /* (session 59) unseen */
        }
        if (m->menu.closing) {
            close_step(g, m);
        } else if (m->flags & MECH_DESTROYED) {
            m->menu.step = CLOSE_STEPS;     /* OrderMenu_StartClose */
            m->menu.closing++;
        } else {
            open_step(g, m);
        }
        g->video = shown;
    }
}
