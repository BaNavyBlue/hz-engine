"""Session 18's mutations of the HUD gauges (hud.c: HUD_UpdateGauges,
HUD_QueueBar, HUD_FlushBars; GaugeTiles in tables.c; the calls in
game.c), run with CHECK 'hud'. Expected NOT CAUGHT (equivalent): "update:
ammo max $20" (6 cells show 24 quarters: any maximum from 24 up draws the
same), "queue: full from 5" ($A5B0 + 4 is the full tile, and the cells
after come out the same), "queue: max 0 takes a tile" (with the max used
up the value is too: GaugeTiles[0][0] is the blank tile, and the count
steps the same)."""

CHECK = 'hud'

H = 'src/game/hud.c'
MUTATIONS = [
 # HUD_UpdateGauges
 ('update: HP / 4', H, 'u16 d1 = (u16)(m->hp >> 3);', 'u16 d1 = (u16)(m->hp >> 2);'),
 ('update: destroyed ignored', H, '        if (m->flags & 0x20) {\n            d1 = 0;', '        if (0) {\n            d1 = 0;'),
 ('update: destroyed from b4', H, '        if (m->flags & 0x20) {', '        if (m->flags & 0x10) {'),
 ('update: HP compared as a word', H, 'if ((u8)d1 != m->hp_shown) {', 'if (d1 != m->hp_shown) {'),
 ('update: HP max $1F', H, 'hud_queue_bar(g, d1, 0x20, 7, cmd->hp);', 'hud_queue_bar(g, d1, 0x1F, 7, cmd->hp);'),
 ('update: HP 7 cells', H, 'hud_queue_bar(g, d1, 0x20, 7, cmd->hp);', 'hud_queue_bar(g, d1, 0x20, 6, cmd->hp);'),
 ('update: HP at the HQ bar', H, 'hud_queue_bar(g, d1, 0x20, 7, cmd->hp);', 'hud_queue_bar(g, d1, 0x20, 7, cmd->hq);'),
 ('update: HP shown not kept', H, '            m->hp_shown = (u8)d1;\n', ''),
 ('update: HQ / 16', H, 'd1 = (u16)(((hq->b2 << 8) | hq->b3) >> 5);', 'd1 = (u16)(((hq->b2 << 8) | hq->b3) >> 4);'),
 ('update: P1\'s HQ for both', H, 'const HzPost *hq = &g->posts[HZ_POST_HQ + p];', 'const HzPost *hq = &g->posts[HZ_POST_HQ];'),
 ('update: HQ low byte dropped', H, 'd1 = (u16)(((hq->b2 << 8) | hq->b3) >> 5);', 'd1 = (u16)((hq->b2 << 8) >> 5);'),
 ('update: cut after P1\'s HQ', H, '            if (p == 1) {\n                /* P2', '            if (p == 0) {\n                /* P2'),
 ('update: no cut', H, '            if (p == 1) {\n                /* P2', '            if (p == 7) {\n                /* P2'),
 ('update: ammo / 8', H, 'd1 = (u16)(m->ammo >> 2);', 'd1 = (u16)(m->ammo >> 3);'),
 ('update: ammo max $20', H, 'hud_queue_bar(g, d1, 0x18, 5, cmd->ammo);', 'hud_queue_bar(g, d1, 0x20, 5, cmd->ammo);'),
 ('update: fuel / 32', H, 'd1 = (u16)(m->fuel >> 6);', 'd1 = (u16)(m->fuel >> 5);'),
 ('update: fuel 7 cells', H, 'hud_queue_bar(g, d1, 0x18, 5, cmd->fuel);', 'hud_queue_bar(g, d1, 0x18, 6, cmd->fuel);'),
 ('update: fuel kept as ammo', H, '            m->fuel_shown = (u8)d1;', '            m->ammo_shown = (u8)d1;'),
 ('update: P2 fuel one cell right', H, '0x70C00003, 0x71400003 }', '0x70C00003, 0x71420003 }'),
 # HUD_QueueBar
 ('queue: no clamp', H, '    if (value > max) {\n        value = max;', '    if (0) {\n        value = max;'),
 ('queue: clamped to max - 1', H, '        value = max;                                    /* .clamped */', '        value = (u16)(max - 1);'),
 ('queue: last entry first', H, '    for (int i = 0; i < HZ_GAUGE_BARS; i++) {           /* .find_slot */', '    for (int i = HZ_GAUGE_BARS - 1; i >= 0; i--) {'),
 ('queue: full queue -> entry 0', H, '    if (bar == NULL) {\n        return;                                         /* .done */', '    if (bar == NULL) {\n        bar = &g->gauge_queue[0];'),
 ('queue: count + 1', H, '    bar->last = last;                                   /* .slot_found */', '    bar->last = (u16)(last + 1);'),
 ('queue: full tile $A5B3', H, '#define GAUGE_FULL   0xA5B4', '#define GAUGE_FULL   0xA5B3'),
 ('queue: full from 5', H, '    while (d1 >= 4) {                                   /* .full_cells */', '    while (d1 >= 5) {'),
 ('queue: max not stepped', H, '        d1 = (u16)(d1 - 4);\n        d2 = (u16)(d2 - 4);', '        d1 = (u16)(d1 - 4);'),
 ('queue: full cells one more', H, '        if (--d3 < 0) {                                 /* dbf */\n            return;\n        }\n    }\n    if (d2 < 4)', '        if (d3-- < 0) {\n            return;\n        }\n    }\n    if (d2 < 4)'),
 ('queue: both end: max column', H, 'put_cell(bar, &n, GAUGE_TILES[d1][d2 - d1]);', 'put_cell(bar, &n, GAUGE_TILES[d1][d2]);'),
 ('queue: both end: indices swapped', H, 'put_cell(bar, &n, GAUGE_TILES[d1][d2 - d1]);', 'put_cell(bar, &n, GAUGE_TILES[d2 - d1][d1]);'),
 ('queue: max 0 takes a tile', H, '        if (d2 != 0) {\n            put_cell(bar, &n, GAUGE_TILES[d1][d2 - d1]);', '        if (1) {\n            put_cell(bar, &n, GAUGE_TILES[d1][d2 - d1]);'),
 ('queue: partial cell one more', H, 'put_cell(bar, &n, (u16)(GAUGE_EMPTY + d1));', 'put_cell(bar, &n, (u16)(GAUGE_EMPTY + d1 + 1));'),
 ('queue: partial cell no stop', H, '        if (--d3 < 0) {                                 /* subq, bcs */\n            return;\n        }', '        --d3;'),
 ('queue: empty cells blank', H, '            put_cell(bar, &n, GAUGE_EMPTY);', '            put_cell(bar, &n, GAUGE_NONE);'),
 ('queue: max ends from row 1', H, 'put_cell(bar, &n, GAUGE_TILES[0][d2]);', 'put_cell(bar, &n, GAUGE_TILES[1][d2]);'),
 ('queue: max ends, count kept', H, 'put_cell(bar, &n, GAUGE_TILES[0][d2]);  /* .max_ends */\n                d3--;', 'put_cell(bar, &n, GAUGE_TILES[0][d2]);'),
 ('queue: blank tile $A5BD', H, '#define GAUGE_NONE   0xA5BE', '#define GAUGE_NONE   0xA5BD'),
 ('queue: one blank short', H, '    while (d3 >= 0) {                                   /* .blank_rest */', '    while (d3 > 0) {'),
 ('tables: GaugeTiles[1][1]', 'src/game/tables.c', '{ 0xA5BA, 0xA5B9, 0xA5B8, 0xA5B1 }', '{ 0xA5BA, 0xA5B8, 0xA5B8, 0xA5B1 }'),
 # HUD_FlushBars
 ('flush: count not cleared', H, '        bar->last = 0;\n', ''),
 ('flush: one word short', H, 'for (int k = 0; k <= last && k < HZ_GAUGE_CELLS; k++) {', 'for (int k = 0; k < last && k < HZ_GAUGE_CELLS; k++) {'),
 ('flush: free entries written', H, '        if (last == 0) {\n            continue;', '        if (0) {\n            continue;'),
 ('flush: stops at a free entry', H, '            continue;                                   /* .next_slot */', '            break;'),
 # the frame
 ('frame: no HUD_UpdateGauges', 'src/game/game.c', '    hud_update_gauges(g);\n', ''),
 ('frame: no HUD_FlushBars', 'src/game/game.c', '    hud_flush_bars(g);\n', ''),
 ('frame: flushed before the wait', 'src/game/game.c', '    game_wait_vblank(g);\n    g->split_enable = 0;\n    g->split_done = 1;\n    vdp_reg(g->vdp, 0x8124);', '    hud_flush_bars(g);\n    game_wait_vblank(g);\n    g->split_enable = 0;\n    g->split_done = 1;\n    vdp_reg(g->vdp, 0x8124);'),
]
