"""Session 29's mutations of the order menu (src/game/order_menu.c), run
with CHECK 'ordermenu' (tools/hzordermenu_check.py: the original module
on tools/m68krun.py, RAM and VRAM). All caught by seeds 1-2; 'radar: rival
x edge' (the rival at exactly +32 x 16 px) and 'open: hidden settle' (a
case ending between steps 1 and 0) by only a few cases, after
tools/hz_ordermenu.c was made to aim at them."""

CHECK = 'ordermenu'

A = 'src/game/order_menu.c'
MUTATIONS = [
 ('input: up from 0', A, 'row = (row == 0) ? 2 : (u8)(row - 1);', 'row = (row == 0) ? 1 : (u8)(row - 1);'),
 ('input: carrying down wrap', A, '            if (row > 2) {\n                row = 1;', '            if (row > 2) {\n                row = 2;'),
 ('input: type left search', A, 'next_in_mask(buyable, (u8)(mn->type - 1), -1)', 'next_in_mask(buyable, (u8)(mn->type - 2), -1)'),
 ('input: type change keeps order up', A, 'mn->order = next_in_mask(orders, (u8)(mn->order - 1), -1);\n                }', 'mn->order = next_in_mask(orders, (u8)(mn->order + 1), +1);\n                }'),
 ('buy: re-order at equal money', A, 'if (m->money < mn->cost) {\n            mn->reorder_state = 0xFF;', 'if (m->money <= mn->cost) {\n            mn->reorder_state = 0xFF;'),
 ('buy: shortfall sign', A, 'm->prod_timer = (u16)(mn->cost - m->money);', 'm->prod_timer = (u16)(m->money - mn->cost);'),
 ('buy: build time', A, '(u16)(type_stats(g, mn->type)->build_time << 7)', '(u16)(type_stats(g, mn->type)->build_time << 6)'),
 ('buy: unit limit', A, '    if (g->unit_count[(m->flags & 0x10) ? 1 : 0] >= MAX_UNITS) {', '    if (g->unit_count[(m->flags & 0x10) ? 1 : 0] > MAX_UNITS) {'),
 ('buy: limit beep hidden', A, '        refused(g);         /* QUIRK: even for a hidden menu', '        if (!hidden(m)) refused(g);         /* QUIRK: even for a hidden menu'),
 ('settle: price', A, '    mn->price = price;\n    if (orders != 0', '    mn->price = price + 1;\n    if (orders != 0'),
 ('settle: row shown', A, 'mn->row_shown = mn->row;\n    mn->type_shown', 'mn->row_shown = 0;\n    mn->type_shown'),
 ('radar: side colours', A, 'u8 dot = (id & 0x80) ? 0x33 : 0x55;', 'u8 dot = (id & 0x80) ? 0x55 : 0x33;'),
 ('radar: dot rows', A, 'buf[(off & 0x7FF) + 4] = dot;', 'buf[(off & 0x7FF) + 8] = dot;'),
 ('radar: rival x edge', A, 'if (dx <= -0x20 || dx >= 0x20', 'if (dx <= -0x20 || dx > 0x20'),
 ('radar: post y', A, 'spr[i].y = (u16)(((u16)p->y >> 5) + 0x115);', 'spr[i].y = (u16)(((u16)p->y >> 5) + 0x114);'),
 ('radar: HQ tile', A, 'tile = (p->flags & 0x40) ? 0xA59E : 0xA5A0;', 'tile = (p->flags & 0x40) ? 0xA5A0 : 0xA5A0;'),
 ('radar: bottom tiles', A, 'return (u16)(0xF060 + (n - 32) * 0x80);', 'return (u16)(0xF060 + (n - 32) * 0x20);'),
 ('radar: palette', A, 'u16 pal = (m->menu.radar_mode != 0) ? 0 : 0x2000;', 'u16 pal = 0x2000;'),
 ('radar: step wrap', A, '    if (step >= 0x0C) {\n        step = 0;', '    if (step >= 0x0B) {\n        step = 0;'),
 ('redraw: radar step after mode', A, '        return;                 /* no radar step this frame */', '        '),
 ('print: zero digit', A, '    if (v != 0) {\n        seen = true;\n    }\n    text[4]', '    text[4]'),
 ('print: two digits swapped', A, "u8 text[2] = { (u8)('0' + v / 10), (u8)('0' + v % 10) };", "u8 text[2] = { (u8)('0' + v % 10), (u8)('0' + v / 10) };"),
 ('cursor: carrying row 0', A, '    if (carrying(m) && (row == 0 || row > 2)) {', '    if (carrying(m) && (row > 2)) {'),
 ('labels: GETNOW row', A, 'print_text(g, plane, 0x0B, 0x0E, "GETNOW");', 'print_text(g, plane, 0x0B, 0x0F, "GETNOW");'),
 ('border: carried column', A, 'fill_column(g, cell_addr(p, 0x11, 0x10), 0x25FE, 7);', 'fill_column(g, cell_addr(p, 0x11, 0x10), 0x25FE, 6);'),
 ('side panels: rows', A, 'SIDE_PANEL_TILES[r % 4], 10);', 'SIDE_PANEL_TILES[(r + 1) % 4], 10);'),
 ('gauges: cap', A, 'hud_queue_bar(g, value[i], (max[i] > 0x14) ? 0x14 : max[i], 4,', 'hud_queue_bar(g, value[i], (max[i] > 0x13) ? 0x13 : max[i], 4,'),
 ('gauges: refill kept', A, '    m->carried_refill = 0;\n}', '}'),
 ('open: wide scroll', A, 'g->scroll_x[plane] = (s16)((plane == 0 && !g->split_screen) ? -0x58 : 0);', 'g->scroll_x[plane] = (s16)((plane == 0 && !g->split_screen) ? -0x50 : 0);'),
 ('close: scroll y', A, '    g->scroll_y[plane] = (s16)((m->menu.scroll_y & 7) + 8);', '    g->scroll_y[plane] = (s16)((m->menu.scroll_y & 7) + 7);'),
 ('close: odd rows', A, '                row = 0x18 - row;', '                row = 0x16 - row;'),
 ('close: wide rows', A, '            map_redraw_view_rows(g, m, 0, 0x1A);', '            map_redraw_view_rows(g, m, 0, 0x19);'),
 ('open: split step', A, '        open_part(g, m, 8 - mn->step);', '        open_part(g, m, 9 - mn->step);'),
 ('open: hidden settle', A, '    if (mn->step == 0) {\n        settle(g, m);\n    }', '    if (mn->step == 1) {\n        settle(g, m);\n    }'),
 ('update: destroyed', A, '        } else if (m->flags & 0x20) {', '        } else if (m->flags & 0x80 && 0) {'),
 ('input: toggle radar', A, '        mn->radar_mode ^= 1;    /* .toggle_radar', '        mn->radar_mode ^= 0;    /* .toggle_radar'),
 ('radar mode: step kept', A, '    mn->radar_step = 0;\n    mn->radar_mode_shown', '    mn->radar_mode_shown'),
 ('money: shown', A, '    m->menu.money_shown = m->money;', '    m->menu.money_shown = (u16)(m->money + 1);'),
]
