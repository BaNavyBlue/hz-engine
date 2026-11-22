#include "game.h"

#include <stddef.h>

/* The map-block changes (v_mapfx, 32 records of 12 bytes): a captured
 * outpost's flags (Outpost_ShowOwner) and, on no shipping map, what the
 * shots' terrain handlers 1 and 5 would do. MapFx_Update runs once a frame
 * after the uploads (display off); a record whose countdown runs out puts
 * its block into the layout and onto whichever view shows it. */

/* MapFx_Alloc ($8E5E): the first record with b7 clear (the 68k's a3, d0
 * 0), or NULL (d0 -1). */
HzMapFx *mapfx_alloc(HzGame *g)
{
    for (int i = 0; i < HZ_MAPFX; i++) {                /* .find */
        if (!(g->mapfx[i].flags & 0x80)) {
            return &g->mapfx[i];                        /* .found */
        }
    }
    return NULL;
}

/* MapFx_DrawBlock ($6B5A): the block's 4 x 4 nametable words from VDP
 * command `cmd`, each cell's command written before it; along a row the
 * column wraps in the plane's 64 cells (& $7E, the row's base kept), and
 * each row is $80 bytes on, wrapping in the 32 rows (& $6FFE). As the view
 * drawers: & $19FF, priority on tiles below $40. */
static void mapfx_draw_block(HzGame *g, const u16 *tiles, u32 cmd)
{
    for (int r = 0; r < 4; r++) {                       /* .row */
        u32 c = cmd;
        u16 base = (u16)((c >> 16) & 0xFF80);
        for (int k = 0; k < 4; k++) {                   /* .cell */
            vdp_command(g->vdp, c);
            u16 w = (u16)(*tiles++ & 0x19FF);
            if ((w & 0x1FF) < 0x40) {
                w |= 0x8000;
            }
            vdp_write16(g->vdp, w);                     /* .store */
            u16 hi = (u16)((((c >> 16) + 2) & 0x7E) | base);
            c = ((u32)hi << 16) | (c & 0xFFFF);
        }
        u16 hi = (u16)((((cmd >> 16) + 0x80)) & 0x6FFE);
        cmd = ((u32)hi << 16) | (cmd & 0xFFFF);
    }
}

/* Where a block at map tile (tx, ty) (block-aligned) falls in a view with
 * camera `cam` and plane scroll (sx, sy): columns from the camera's tile
 * (camera + 4) / 8, shown when -4 <= dx < width - 4 (unsigned, as the
 * 68k's `+ 4`, `cmp`); rows when -4 <= dy < $18. The plane cell: column dx
 * + (sx + 4) / 8 (& $3F), row dy + 4 + (sy + 4) / 8 (& $1F): the row
 * keeps the + 4 of its test (the column's is taken off again). Returns
 * false when the block is off the view; else the VDP command `plane` with
 * the cell's offset (& $FFE). */
static bool mapfx_view_cmd(u16 tx, u16 ty, const HzPoint *cam, s16 sx,
                           s16 sy, u16 width, u32 plane, u32 *cmd)
{
    u16 col = (u16)(tx - ((u16)(cam->x + 4) >> 3) + 4);
    if (col >= width) {
        return false;
    }
    col = (u16)(col - 4);
    u16 row = (u16)(ty - ((u16)(cam->y + 4) >> 3) + 4);
    if (row >= 0x1C) {
        return false;
    }
    col = (u16)(((col + ((u16)(sx + 4) >> 3)) & 0x3F) * 2);
    row = (u16)(((row + ((u16)(sy + 4) >> 3)) & 0x1F) << 7);
    u16 ofs = (u16)((col + row) & 0xFFE);
    *cmd = plane | ((u32)ofs << 16);
    return true;
}

/* MapFx_Update ($698A): each active record's countdown (+2) - 1; when it
 * runs out (it was 0): its block (& $1FF) into the layout word at its
 * offset (& $1FFE; the word's top 7 bits kept), and its tiles drawn where
 * view A (camera A, plane A, scroll A) and view B (camera B; split: plane
 * B and scroll B, wide: plane A and scroll A) show the block; a view is
 * skipped while its mech's view is hidden (mech_view < 0) or its order
 * menu open (obj_flags b6), P1 for view A, P2 for view B. Width: the
 * view's tiles + 4 ($16 split, $2C wide). Then the record is cleared,
 * unless chained (b6): the next block is the new block's terrain high
 * byte (into +9), flags $80 with the terrain low byte's b7 as b6 (chained
 * on), countdown = +3. (A record's x/y give the block it draws over; the
 * layout offset is kept apart, as the 68k has them.) */
void map_fx_update(HzGame *g)
{
    u16 width = g->split_screen ? 0x16 : 0x2C;          /* .width_set */
    const HzMech *m1 = &g->mech[0];
    const HzMech *m2 = &g->mech[1];
    bool skip_a = m1->view < 0 || (m1->flags & MECH_MENU_OPEN); /* .p1_menu */
    bool skip_b = m2->view < 0 || (m2->flags & MECH_MENU_OPEN); /* .p2_menu */

    for (int i = 0; i < HZ_MAPFX; i++) {                /* .record */
        HzMapFx *fx = &g->mapfx[i];
        if (!(fx->flags & 0x80)) {
            continue;
        }
        u8 was = fx->countdown;
        fx->countdown = (u8)(was - 1);
        if (was != 0) {
            continue;
        }
        u16 tx = (u16)(((u16)(fx->x + 4) >> 3) & 0xFC);
        u16 ty = (u16)(((u16)(fx->y + 4) >> 3) & 0xFC);
        u16 block = (u16)(fx->block & 0x1FF);
        u16 ofs = (u16)((fx->layout_ofs & 0x1FFE) >> 1);
        u16 *lw = &g->layout[ofs >> 6][ofs & 0x3F];
        *lw = (u16)((*lw & 0xFE00) | block);
        const u16 *tiles = g->blocks[block];

        u32 cmd;
        if (!skip_a && mapfx_view_cmd(tx, ty, g->view_camera[0],
                                      g->scroll_x[0], g->scroll_y[0],
                                      width, 0x40000003u, &cmd)) {
            mapfx_draw_block(g, tiles, cmd);
        }
        if (!skip_b) {                                  /* .view_b */
            int s = g->split_screen ? 1 : 0;            /* .b_plane_set */
            u32 plane = g->split_screen ? 0x60000003u : 0x40000003u;
            if (mapfx_view_cmd(tx, ty, g->view_camera[1], g->scroll_x[s],
                               g->scroll_y[s], width, plane, &cmd)) {
                mapfx_draw_block(g, tiles, cmd);
            }
        }

        u8 flags = fx->flags;                           /* .chain */
        fx->flags = 0;
        if (flags & 0x40) {
            /* The 68k indexes by the whole +8 word; a chained record's
             * blocks come from terrain high bytes, so they are below $100. */
            u16 t = g->block_terrain[fx->block & 0x1FF];
            fx->block = (u16)((fx->block & 0xFF00) | (t >> 8));
            fx->flags = (u8)(0x80 | (((t & 0xFF) >> 1) & 0x40));
            fx->countdown = fx->reload;
        }
    }
}
