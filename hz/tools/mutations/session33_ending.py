"""Session 33's mutations of the endings (src/game/ending.c), the menu
helpers they added to front.c, and session 32's game end
(src/game/game_end.c), run with CHECK 'gameend' (tools/hzgameend_check.py
--endings: the original MenuModule_GameEnd on tools/m68krun.py, frame by
frame). Results: see HANDOVER.md, session 33."""

CHECK = 'gameend'

E = 'src/game/ending.c'
F = 'src/game/front.c'
G = 'src/game/game_end.c'
MUTATIONS = [
 # -- the story
 ('ending: picture choice', E, 'int n = (sides(g) == 2) ? 1 : 0;', 'int n = (sides(g) == 1) ? 1 : 0;'),
 ('ending: picture line 3 black', E, '        f->palette[48 + i] = 0;\n    }\n    draw_picture(g);', '        f->palette[48 + i] = e->picture_palettes[n][i];\n    }\n    draw_picture(g);'),
 ('ending: picture cell', E, '#define PICTURE_CELL    0x6100', '#define PICTURE_CELL    0x4100'),
 ('ending: picture row stride', E, 'front_vram_at(g, (u16)(FRONT_WINDOW + row * 0x80));', 'front_vram_at(g, (u16)(FRONT_WINDOW + row * 0x50));'),
 ('ending: picture vram', E, '#define PICTURE_VRAM    0x2000', '#define PICTURE_VRAM    0x2020'),
 ('ending: text palette mask', E, 'front_upload_lines(g, 0x01);', 'front_upload_lines(g, 0x03);'),
 ('ending: text palette target', E, '    front_upload_lines(g, 0x01);\n    front_set_target(g);', '    front_upload_lines(g, 0x01);'),
 ('ending: story choice', E, 'g->assets->ending.story[sides(g) == 2 ? 1 : 0];', 'g->assets->ending.story[0];'),
 ('ending: story scroll frames', E, '#define STORY_SCROLL    7', '#define STORY_SCROLL    6'),
 ('ending: story row step', E, '        row = (row + 2) % PLANE_ROWS;', '        row = (row + 1) % PLANE_ROWS;'),
 ('ending: story trailing rows', E, 'for (int i = 0; i < 29 && !g->quit; i++) {', 'for (int i = 0; i < 28 && !g->quit; i++) {'),
 ('ending: story no clear', E, '        clear_row(g, STORY_X, row, HZ_STORY_WIDTH);\n        scroll_row(g, STORY_SCROLL);\n        print_b', '        scroll_row(g, STORY_SCROLL);\n        print_b'),
 ('ending: story x', E, '#define STORY_X         2', '#define STORY_X         3'),
 ('ending: first row', E, '#define FIRST_ROW       29', '#define FIRST_ROW       28'),
 ('ending: scroll wrap', E, 'f->scroll_y_b = (s16)((f->scroll_y_b + 1) & 0xFF);', 'f->scroll_y_b = (s16)((f->scroll_y_b + 1) & 0x1FF);'),
 ('ending: scroll before wait', E, '        for (int i = 0; i < frames; i++) {\n            front_frame(g);\n        }\n        front_apply_scroll(g);', '        front_apply_scroll(g);\n        for (int i = 0; i < frames; i++) {\n            front_frame(g);\n        }'),
 ('ending: scroll x', E, '        f->scroll_x_b = 0;\n        for', '        f->scroll_x_b = 1;\n        for'),
 ('ending: print char offset', E, 'vdp_write16(g->vdp, (u16)(codes[i] - 0x30 + f->text_attr));', 'vdp_write16(g->vdp, (u16)(codes[i] - 0x2F + f->text_attr));'),
 ('ending: print row width', E, 'front_vram_at(g, (u16)(FRONT_PLANE_B + ((y << 6) + x) * 2));', 'front_vram_at(g, (u16)(FRONT_PLANE_B + ((y << 5) + x) * 2));'),
 ('ending: story song', E, 'snd_play_music(g, sides(g) == 1 ? SONG_ENDING_P1 : SONG_ENDING);', 'snd_play_music(g, sides(g) == 2 ? SONG_ENDING_P1 : SONG_ENDING);'),
 ('ending: story song once', E, '    snd_start_song(g, 0);\n    scroll_story(g);', '    snd_start_song(g, 0xFF);\n    scroll_story(g);'),
 ('ending: text palette 0', E, '    front_text_font(g, 0);\n    front_text_palette(g, 0);', '    front_text_font(g, 0);\n    front_text_palette(g, 1);'),
 ('ending: story font vram', E, '#define STORY_FONT_VRAM 0x0000', '#define STORY_FONT_VRAM 0x0020'),
 # -- between the parts, the forest
 ('ending: blank fill', E, '#define BLANK_CELL      0x0010', '#define BLANK_CELL      0x0030'),
 ('ending: staff song repeats', E, '    snd_start_song(g, 0xFF);\n    front_display_off(g);', '    snd_start_song(g, 0);\n    front_display_off(g);'),
 ('ending: no clear palette', E, '    front_display_off(g);\n    front_clear_palette(g);', '    front_display_off(g);'),
 ('ending: forest picture', E, 'front_vram_bytes(g, PICTURE_VRAM, e->forest, sizeof e->forest);', 'front_vram_bytes(g, PICTURE_VRAM, e->pictures[0], sizeof e->forest);'),
 ('ending: forest names colour', E, 'f->target[15] = 0x00EE;', 'f->target[15] = 0x00E0;'),
 ('ending: forest headings colour', E, 'f->target[31] = 0x000E;', 'f->target[31] = 0x000C;'),
 ('ending: forest line 2', E, '        f->target[32 + i] = e->text_palette[i];', '        f->target[32 + i] = e->forest_palette[i];'),
 ('ending: forest palette clear', E, '    draw_picture(g);\n    for (int i = 0; i < 64; i++) {\n        f->palette[i] = 0;\n    }', '    draw_picture(g);'),
 ('ending: frame after forest', E, '    show_forest(g);\n    front_frame(g);', '    show_forest(g);'),
 # -- the staff roll
 ('staff: x', E, '#define STAFF_X         12', '#define STAFF_X         11'),
 ('staff: scroll frames', E, '#define STAFF_SCROLL    4', '#define STAFF_SCROLL    5'),
 ('staff: first palette', E, '    front_text_palette(g, 1);\n    int row = FIRST_ROW;', '    front_text_palette(g, 0);\n    int row = FIRST_ROW;'),
 ('staff: palette by kind', E, 'front_text_palette(g, line->name ? 0 : 1);', 'front_text_palette(g, line->name ? 1 : 0);'),
 ('staff: blanks before names', E, 'row = staff_blank_rows(g, row, line->name ? 1 : 17);', 'row = staff_blank_rows(g, row, line->name ? 2 : 17);'),
 ('staff: blanks before headings', E, 'row = staff_blank_rows(g, row, line->name ? 1 : 17);', 'row = staff_blank_rows(g, row, line->name ? 1 : 16);'),
 ('staff: change test', E, 'if (i > 0 && line->name != e->staff[i - 1].name) {', 'if (i > 0 && line->name && !e->staff[i - 1].name) {'),
 ('staff: row step', E, '        scroll_row(g, STAFF_SCROLL);\n        row = (row + 1) % PLANE_ROWS;\n    }\n    row = staff_blank_rows(g, row, 1);', '        scroll_row(g, STAFF_SCROLL);\n        row = (row + 2) % PLANE_ROWS;\n    }\n    row = staff_blank_rows(g, row, 1);'),
 ('staff: palette before last blank', E, '    row = staff_blank_rows(g, row, 1);\n    front_text_palette(g, 0);', '    front_text_palette(g, 0);\n    row = staff_blank_rows(g, row, 1);'),
 ('staff: maker region', E, 'e->maker[(g->hw_version & 0x80) ? 1 : 0]', 'e->maker[(g->hw_version & 0x40) ? 1 : 0]'),
 ('staff: end blanks', E, 'staff_blank_rows(g, row, 32);', 'staff_blank_rows(g, row, 31);'),
 ('staff: blank width', E, 'clear_row(g, STAFF_X, row, HZ_STAFF_WIDTH);', 'clear_row(g, STAFF_X, row, HZ_STAFF_WIDTH - 1);'),
 # -- ENDE
 ('ende: cell', E, '#define ENDE_CELL       0xC080', '#define ENDE_CELL       0x8080'),
 ('ende: y', E, '#define ENDE_Y          12', '#define ENDE_Y          13'),
 ('ende: vram', E, '#define ENDE_VRAM       0x1000', '#define ENDE_VRAM       0x1020'),
 ('ende: frames', E, '#define ENDE_FRAMES     0xE10', '#define ENDE_FRAMES     0xE0F'),
 ('ende: start', E, 'if (front_wait_pad(g) & HZ_PAD_START) {\n            break;\n        }\n    }\n    snd_fade_music(g, 1);', 'if (front_wait_pad(g) & HZ_PAD_A) {\n            break;\n        }\n    }\n    snd_fade_music(g, 1);'),
 ('ende: fade speed', E, 'snd_fade_music(g, 1);', 'snd_fade_music(g, 0);'),
 ('ende: no target', E, '    draw_ende(g);\n    front_set_target(g);', '    draw_ende(g);'),
 # -- front.c's new helpers
 ('front: fade in start', F, 'f->fade_level = 0x20;', 'f->fade_level = 0x1F;'),
 ('front: fade in end', F, '} while (f->fade_level >= 0);', '} while (f->fade_level > 0);'),
 ('front: fade in order', F, '        front_apply_fade(g);\n        front_frame(g);\n        front_upload_lines(g, 0x0F);\n        f->fade_level--;', '        front_apply_fade(g);\n        front_upload_lines(g, 0x0F);\n        front_frame(g);\n        f->fade_level--;'),
 ('front: clear palette upload', F, '        g->front.palette[i] = 0;\n    }\n    front_upload_palette(g);\n}', '        g->front.palette[i] = 0;\n    }\n}'),
 ('front: apply scroll vsram', F, 'vdp_set_write(g->vdp, VDP_VSRAM, 2);\n    vdp_write16(g->vdp, (u16)g->front.scroll_y_b);', 'vdp_set_write(g->vdp, VDP_VSRAM, 0);\n    vdp_write16(g->vdp, (u16)g->front.scroll_y_b);'),
 # -- the game end (session 32)
 ('end: endings test', G, '        && g->win_record[2] == 0xFF && g->win_record[3] == 0xFF\n', '        && g->win_record[2] == 0xFF\n'),
 ('end: winner side', G, '(g->winner != 0) ? "2PLAYER" : "1PLAYER"', '(g->winner == 0) ? "2PLAYER" : "1PLAYER"'),
 ('end: winner scroll start', G, 'f->scroll_y_b = -0x58;', 'f->scroll_y_b = -0x50;'),
 ('end: winner wait', G, 'front_wait_frames(g, 0x78);', 'front_wait_frames(g, 0x77);'),
 ('end: photo cell', G, '#define PHOTO_CELL      0x6100', '#define PHOTO_CELL      0x6000'),
 ('end: photo fade steps', G, 'for (int step = 0; step < 8 && !g->quit; step++) {', 'for (int step = 0; step < 7 && !g->quit; step++) {'),
 ('end: song lost', G, 'song = (g->winner == sides - 1) ? 0x0B : 0x0C;', 'song = (g->winner == sides - 1) ? 0x0B : 0x0B;'),
 ('end: wait after photo', G, '    front_wait_frames(g, 0x3B);\n    results_show(g);', '    front_wait_frames(g, 0x3A);\n    results_show(g);'),
 ('end: record side', G, '    if (g->winner != sides - 1) {\n        return;\n    }', '    if (g->winner != (sides & 1)) {\n        return;\n    }'),
 ('end: record bit', G, '|= (u8)(1u << (g->options[3] & 7));', '|= (u8)(1u << (g->options[3] & 3));'),
 ('end: short wait', G, 'int wait = (g->win_record[0] == 0xFF) ? 0x708 : 0x2A30;', 'int wait = (g->win_record[1] == 0xFF) ? 0x708 : 0x2A30;'),
 ('end: brain p1', G, 'print_big(g, 0x04, 0x0B, (g->options[0] & 2) ? "@COM" : "USER");', 'print_big(g, 0x04, 0x0B, (g->options[0] & 1) ? "@COM" : "USER");'),
 ('end: hours cap', G, 'if (frames < 0x1464B40) {', 'if (frames <= 0x1464B40) {'),
 ('end: 4 digits blank', G, 'put_big_char(g, started ? (u8)(\'0\' + digit) : MENU_BLANK);', 'put_big_char(g, (u8)(\'0\' + digit));'),
]
