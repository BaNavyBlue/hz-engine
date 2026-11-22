#include "game.h"

/* Map_ScanOutposts ($6BB0): walk the 64 x 64 layout; a block whose
 * terrain type is 3 is an HQ, 2 an outpost. The terrain word's high byte
 * says whose, as the record's flags have it: POST_TEAM2, and for
 * outposts POST_OWNED at all (an HQ is always owned). Each outpost's
 * index goes into its layout word (bits 10-13) so a mech or unit over it
 * can find its record. The 68k has no limit on the count; the maps hold
 * 16 at most, and the port stops at the records it has. */
void map_scan_outposts(HzGame *g)
{
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        g->posts[i] = (HzPost){ 0 };
    }
    for (int i = 0; i < 4; i++) {
        g->outpost_count[i] = 0;
    }

    int next = 0;                     /* the next outpost index */
    s16 y = 0x10;
    for (int row = 0; row < HZ_LAYOUT_SIZE; row++) {
        s16 x = 0x10;
        for (int col = 0; col < HZ_LAYOUT_SIZE; col++) {
            u16 block = g->layout[row][col] & 0x1FF;
            u16 terrain = g->block_terrain[block];
            u8  type = (u8)terrain;
            u8  owner = (u8)(terrain >> 8);

            if (type == 3) {
                u8 flags = (u8)(POST_USED | POST_HQ | POST_OWNED
                                | (owner & POST_TEAM2));
                HzPost *hq = &g->posts[HZ_POST_HQ + (flags & POST_TEAM2)];
                hq->flags = flags;
                post_set_hq_hp(hq, 1024);
                hq->x = x;
                hq->y = y;
            } else if (type == 2 && next < HZ_POST_OUTPOSTS) {
                /* The block number and the index: the word's other high
                 * bits are dropped. */
                g->layout[row][col] = (u16)(block | (next << 10));
                u8 flags = (u8)(POST_USED | (owner & (POST_OWNED
                                                      | POST_TEAM2)));
                HzPost *op = &g->posts[next++];
                op->flags = flags;
                op->x = x;
                op->y = y;
                g->outpost_count[3]++;
                if (!(flags & POST_OWNED)) {
                    g->outpost_count[2]++;                   /* neutral */
                } else if (!(flags & POST_TEAM2)) {
                    g->outpost_count[0]++;
                    op->hold[0] = 4;                    /* P1 holds it */
                } else {
                    g->outpost_count[1]++;
                    op->hold[1] = 4;
                }
            }
            x = (s16)(x + 0x20);
        }
        y = (s16)(y + 0x20);
    }
}

/* The planes' VRAM: 64 x 32 cells each, a row $80 bytes. */
#define PLANE_A  0xC000u
#define PLANE_B  0xE000u
#define HSCROLL  0xD400u    /* the H scroll table */

#define PLANE_COLS 64
#define PLANE_ROWS 32
#define HUD_ROWS   4        /* the HUD strip over every view */

/* A plane cell's VRAM address; the column and the row wrap round the
 * plane. */
static u16 cell_addr(u16 plane, int col, int row)
{
    return (u16)(plane + ((row & (PLANE_ROWS - 1)) << 7)
                 + ((col & (PLANE_COLS - 1)) << 1));
}

/* The plane cell under view position (x, y): its column and row there,
 * the row four down (the HUD strip's). */
static int view_col(s16 x)
{
    return ((u16)x >> 3) & (PLANE_COLS - 1);
}

static int view_row(s16 y)
{
    return ((((u16)y >> 3) & (PLANE_ROWS - 1)) + HUD_ROWS)
           & (PLANE_ROWS - 1);
}

/* The map tile (0-255, the map wrapping) under map pixel c. */
static u16 map_tile(s16 c)
{
    return (u16)(((u16)c >> 3) & 0xFF);
}

/* The map tile under (camera - 8): one tile left of and above the view. */
static u16 camera_tile(s16 c)
{
    return map_tile((s16)(c - 8));
}

/* Each full redraw first brings the view's scroll back to 8-15 (the same
 * position within a tile): the plane is redrawn from its corner, so the
 * scroll can start over. */
static void reset_scroll(HzGame *g, int plane)
{
    g->scroll_y[plane] = (s16)((g->scroll_y[plane] & 7) + 8);
    g->scroll_x[plane] = (s16)((g->scroll_x[plane] & 7) + 8);
}

/* One map cell as the plane holds it: map tile (col, row), each 0-255
 * (the map wraps), through its block. The block word keeps its tile and
 * flips only (& $19FF: line 0, no priority, and not bit 13, which the
 * game uses as a flag); tiles below $40 get priority. */
static u16 map_cell(const HzGame *g, u16 col, u16 row)
{
    u16 layout = g->layout[(row >> 2) & 63][(col >> 2) & 63];
    u16 w = (u16)(g->blocks[layout & 0x1FF][(row & 3) * 4 + (col & 3)]
                  & 0x19FF);
    if ((w & 0x1FF) < 0x40) {
        w |= 0x8000;
    }
    return w;
}

/* Map_DrawBlockRect ($5E02): `cols` x `rows` map tiles from tile (col,
 * row) into a plane from cell (pcol, prow), wrapping round the plane's
 * 64 x 32 cells and the map's 256 x 256 tiles. (The 68k steps through the
 * layout and the block a tile at a time; the cells are the same.) */
static void draw_block_rect(HzGame *g, u16 plane, int pcol, int prow,
                            u16 col, u16 row, int cols, int rows)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            vdp_set_write(g->vdp, VDP_VRAM,
                          cell_addr(plane, pcol + c, prow + r));
            vdp_write16(g->vdp, map_cell(g, (u16)((col + c) & 0xFF),
                                         (u16)((row + r) & 0xFF)));
        }
    }
}

/* A whole view from its camera, the plane from the view's corner (one
 * cell left of and above the scroll), the map from the tile under
 * (camera - 8): the views are 18 x 24 cells split, 40 x 24 wide, drawn
 * with a cell of margin all round. */
static void draw_view(HzGame *g, int plane, const HzPoint *cam, int cols)
{
    reset_scroll(g, plane);
    draw_block_rect(g, plane ? PLANE_B : PLANE_A,
                    view_col((s16)(g->scroll_x[plane] - 8)),
                    view_row((s16)(g->scroll_y[plane] - 8)),
                    camera_tile(cam->x), camera_tile(cam->y), cols, 26);
}

/* Map_DrawViews ($5A44): split screen draws B, then A from camera 1;
 * wide screen goes to Map_DrawViewWide. */
void map_draw_views(HzGame *g)
{
    if (!g->split_screen) {
        map_draw_view_wide(g);
        return;
    }
    map_draw_view_b(g);
    draw_view(g, 0, &g->camera[0], 20);
}

/* Map_DrawViewB ($5ACC): plane B from camera 2, split screen only. */
void map_draw_view_b(HzGame *g)
{
    if (!g->split_screen) {
        return;
    }
    draw_view(g, 1, &g->camera[1], 20);
}

/* Map_DrawViewWide ($5B50): plane A, 42 cells wide, from camera 1 unless
 * P1's mech isn't shown (then camera 2). */
void map_draw_view_wide(HzGame *g)
{
    int cam = (g->mech[0].view != 0) ? 1 : 0;
    draw_view(g, 0, &g->camera[cam], 42);
}

/* The asr.w the 68k uses to compare scroll cells. */
static s16 asr16(s16 v, int n)
{
    return (s16)(v < 0 ? ~(~v >> n) : v >> n);
}

/* A plane's scroll has crossed a cell since Map_ScrollUpdate last saw
 * it (asr.w #3 on both, so a negative scroll rounds down). */
static bool cell_changed(s16 now, s16 prev)
{
    return asr16(now, 3) != asr16(prev, 3);
}

/* Queue flags and queues: row A, column A, row B, column B. */
enum { Q_ROW_A, Q_COL_A, Q_ROW_B, Q_COL_B };

/* The queue routines differ only in plane, edge and camera, so they are
 * one function each for rows and columns here; each call below names the
 * 68k routines it stands for. Map_BuildRow ($63A2) and Map_BuildColumn
 * ($6788) fill the cells, each looked up afresh.
 *
 *   plane   0 A (camera 1 in split screen), 1 B (camera 2)
 *   edge    the view position the new row/column is at, relative to the
 *           scroll and to the camera alike: rows -8 (top margin) or $C0
 *           (bottom margin: the view is $C0 high); columns -8 (left) or
 *           $90 / $140 (right: the view's width)
 *   cam     the camera the cells come from
 *   cells   20 / 42 for a split / wide row, 26 for a column
 *
 * Plane B's queues also name a whole plane row or column for the flush
 * to clear, 28 rows or 22 columns on from the new one: the column clears
 * keep plane B blank where it lies under view A (without them the
 * columns view B leaves behind wrap round to there); the row clears land
 * under the HUD strip or below the screen. No map tile, HUD or divider
 * pixel is transparent, so on the shipping maps neither can be seen; they
 * are kept as the 68k has them. */
static void queue_row(HzGame *g, int plane, s16 edge, const HzPoint *cam,
                      int cells)
{
    HzScrollQueue *q = &g->row_queue[plane];
    g->scroll_queued[plane ? Q_ROW_B : Q_ROW_A] = 1;
    q->plane = plane ? PLANE_B : PLANE_A;
    q->col = (u8)view_col((s16)(g->scroll_x[plane] - 8));
    q->row = (u8)view_row((s16)(g->scroll_y[plane] + edge));
    q->last = (u16)(cells - 1);
    u16 col = map_tile((s16)(cam->x - 8));
    u16 row = map_tile((s16)(cam->y + edge));
    for (int i = 0; i < cells; i++) {
        q->cells[i] = map_cell(g, (u16)((col + i) & 0xFF), row);
    }
    if (plane) {
        /* Top: the row below the screen. Bottom: the row under the HUD
         * strip's top. */
        q->clear = (u8)((q->row + (edge < 0 ? 28 : -28)) & (PLANE_ROWS - 1));
    }
}

static void queue_column(HzGame *g, int plane, s16 edge, const HzPoint *cam)
{
    HzScrollQueue *q = &g->col_queue[plane];
    g->scroll_queued[plane ? Q_COL_B : Q_COL_A] = 1;
    q->plane = plane ? PLANE_B : PLANE_A;
    q->col = (u8)view_col((s16)(g->scroll_x[plane] + edge));
    q->row = (u8)view_row((s16)(g->scroll_y[plane] - 8));
    q->last = 26 - 1;
    u16 col = map_tile((s16)(cam->x + edge));
    u16 row = map_tile((s16)(cam->y - 8));
    for (int i = 0; i < 26; i++) {
        q->cells[i] = map_cell(g, col, (u16)((row + i) & 0xFF));
    }
    if (plane) {
        /* Left: off the right of the screen. Right: under the divider. */
        q->clear = (u8)((q->col + (edge < 0 ? 22 : -22)) & (PLANE_COLS - 1));
    }
}

#define EDGE_TOP     (-8)
#define EDGE_BOTTOM  0xC0
#define EDGE_LEFT    (-8)
#define EDGE_RIGHT   0x90   /* split screen */
#define EDGE_RIGHT_W 0x140  /* wide screen */

/* A split view: the column on the side the camera moved to
 * (mech_cam_dx < 0: left), then the row (mech_cam_dy < 0: top). Both are
 * queued whichever axis crossed the cell, so a move along one axis with
 * the other's cam_d nonzero redraws a margin that is already right. */
static void queue_split_view(HzGame *g, int plane)
{
    const HzMech *m = &g->mech[plane];
    const HzPoint *cam = &g->camera[plane];
    if (m->cam_dx < 0) {
        queue_column(g, plane, EDGE_LEFT, cam);         /* Map_QueueColLeftA/B */
    } else if (m->cam_dx > 0) {
        queue_column(g, plane, EDGE_RIGHT, cam);        /* ...ColRightA/B */
    }
    if (m->cam_dy < 0) {
        queue_row(g, plane, EDGE_TOP, cam, 20);         /* ...RowTopA/B */
    } else if (m->cam_dy > 0) {
        queue_row(g, plane, EDGE_BOTTOM, cam, 20);      /* ...RowBottomA/B */
    }
}

/* Map_ScrollUpdate ($5EE0), once a frame before VDP_WaitVBlank: for each
 * view whose scroll crossed a cell since last frame, queue the new margin
 * row and column (on plane A or B, from its camera). Split screen: view
 * A follows mech 1, view B mech 2. Wide screen: only view A, following
 * the shown mech (mech 1 if its mech_view is 0, else mech 2), from camera
 * 2 if that mech's mech_view_index is set (the ...Cam2 entries). One row
 * and one column at most: a scroll that moved more than a cell in a frame
 * would leave a gap (the camera moves 8 px a frame at most). */
void map_scroll_update(HzGame *g)
{
    for (int i = 0; i < 4; i++) {
        g->scroll_queued[i] = 0;
    }

    if (g->split_screen) {
        for (int plane = 0; plane < 2; plane++) {
            if (cell_changed(g->scroll_x[plane], g->scroll_prev_x[plane])
                || cell_changed(g->scroll_y[plane], g->scroll_prev_y[plane])) {
                queue_split_view(g, plane);
            }
        }
    } else if (cell_changed(g->scroll_x[0], g->scroll_prev_x[0])
               || cell_changed(g->scroll_y[0], g->scroll_prev_y[0])) {
        const HzMech *m = &g->mech[0];
        if (m->view != 0) {
            m = &g->mech[1];
        }
        const HzPoint *cam = &g->camera[m->view_index ? 1 : 0];
        /* Map_QueueColLeftFull, ...ColRightFull, ...RowTopFull,
         * ...RowBottomFull, or their Cam2 copies. */
        if (m->cam_dx < 0) {
            queue_column(g, 0, EDGE_LEFT, cam);
        } else if (m->cam_dx > 0) {
            queue_column(g, 0, EDGE_RIGHT_W, cam);
        }
        if (m->cam_dy < 0) {
            queue_row(g, 0, EDGE_TOP, cam, 42);
        } else if (m->cam_dy > 0) {
            queue_row(g, 0, EDGE_BOTTOM, cam, 42);
        }
    }

    for (int plane = 0; plane < 2; plane++) {
        g->scroll_prev_x[plane] = g->scroll_x[plane];
        g->scroll_prev_y[plane] = g->scroll_y[plane];
    }
}

/* Map_FlushRowA/B's cells ($6856): rightwards, the column wrapping round
 * the plane row. */
static void flush_row(Vdp *v, const HzScrollQueue *q)
{
    for (int i = 0; i <= q->last; i++) {
        vdp_set_write(v, VDP_VRAM, cell_addr(q->plane, q->col + i, q->row));
        vdp_write16(v, q->cells[i]);
    }
}

/* Map_FlushColA/B's cells ($6884): downwards, the row wrapping round the
 * plane. */
static void flush_column(Vdp *v, const HzScrollQueue *q)
{
    for (int i = 0; i <= q->last; i++) {
        vdp_set_write(v, VDP_VRAM, cell_addr(q->plane, q->col, q->row + i));
        vdp_write16(v, q->cells[i]);
    }
}

/* Map_FlushScroll ($67F2): the scroll registers, then whatever
 * Map_ScrollUpdate queued. H scroll is the negated view scroll, plane
 * B's offset $B0 to the right (it shows through the window's right-hand
 * side). Plane B's row is followed by a whole plane row of blank cells
 * (Map_FlushRowB), its column by a whole plane column (Map_FlushColB:
 * the 68k writes it with the auto-increment at $80, then puts it back to
 * 2). */
void map_flush_scroll(HzGame *g)
{
    Vdp *v = g->vdp;
    vdp_set_write(v, VDP_VRAM, HSCROLL);
    vdp_write16(v, (u16)-g->scroll_x[0]);
    vdp_write16(v, (u16)(-g->scroll_x[1] + 0xB0));
    vdp_set_write(v, VDP_VSRAM, 0);
    vdp_write16(v, (u16)g->scroll_y[0]);
    vdp_write16(v, (u16)g->scroll_y[1]);

    if (g->scroll_queued[Q_ROW_A]) {
        flush_row(v, &g->row_queue[0]);                 /* Map_FlushRowA */
    }
    if (g->scroll_queued[Q_COL_A]) {
        flush_column(v, &g->col_queue[0]);              /* Map_FlushColA */
    }
    if (g->scroll_queued[Q_ROW_B]) {
        const HzScrollQueue *q = &g->row_queue[1];
        flush_row(v, q);                                /* Map_FlushRowB */
        for (int col = 0; col < PLANE_COLS; col++) {
            vdp_set_write(v, VDP_VRAM, cell_addr(PLANE_B, col, q->clear));
            vdp_write16(v, 0);
        }
    }
    if (g->scroll_queued[Q_COL_B]) {
        const HzScrollQueue *q = &g->col_queue[1];
        flush_column(v, q);                             /* Map_FlushColB */
        for (int row = 0; row < PLANE_ROWS; row++) {
            vdp_set_write(v, VDP_VRAM, cell_addr(PLANE_B, q->clear, row));
            vdp_write16(v, 0);
        }
    }
}

/* Map_RedrawPlayerView ($5C4E): `rows` of mech m's view (first_row from
 * the view's top, a plane row each) drawn again from the map, as the order
 * menu's close does: split screen 20 cells wide (view A plane A from
 * camera 1, any other view plane B from camera 2), wide screen 42 (plane
 * A, from camera 2 when the view follows it, mech_view_index); a hidden
 * wide view draws nothing. The plane cell is the view's corner (its
 * scroll - 8) first_row rows down; the map tile the camera's (camera -
 * 8) / 8, first_row rows down. */
void map_redraw_view_rows(HzGame *g, const HzMech *m, int first_row, int rows)
{
    int plane, cam, cols;
    if (g->split_screen) {
        plane = (m->view == 0) ? 0 : 1;
        cam = plane;
        cols = 20;
    } else {
        if (m->view < 0) {
            return;
        }
        plane = 0;
        cam = (m->view_index != 0) ? 1 : 0;
        cols = 42;
    }
    u16 row = (u16)((((u16)(g->camera[cam].y - 8) >> 3) + first_row) & 0xFF);
    draw_block_rect(g, plane ? PLANE_B : PLANE_A,
                    view_col((s16)(g->scroll_x[plane] - 8)),
                    view_row((s16)(g->scroll_y[plane] - 8)) + first_row,
                    camera_tile(g->camera[cam].x), row, cols, rows);
}
