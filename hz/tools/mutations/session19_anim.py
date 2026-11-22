"""Session 19's mutations of the map animation (anim.c:
Map_AnimTilesUpdate, Map_AnimTilesUpload; vdp_vram_copy in hz_vdp.c;
Map_InitAnimTiles in load.c; ColourCycleCRAM in tables.c; the calls in
game.c), run with CHECK 'anim'. See HANDOVER.md for the ones expected not
to be caught."""

CHECK = 'anim'

A = 'src/game/anim.c'
MUTATIONS = [
 # Map_AnimTilesUpdate
 ('update: group 0 only', A, '    if (g->unit_ai_group != 0 && g->unit_ai_group != 6) {\n        return;\n    }\n    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {             /* .run */', '    if (g->unit_ai_group != 0) {\n        return;\n    }\n    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {             /* .run */'),
 ('update: group 6 is 5', A, '    if (g->unit_ai_group != 0 && g->unit_ai_group != 6) {\n        return;\n    }\n    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {             /* .run */', '    if (g->unit_ai_group != 0 && g->unit_ai_group != 5) {\n        return;\n    }\n    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {             /* .run */'),
 ('update: flags not cleared', A, '        job->pending = 0;                                   /* .record */\n        job->dma = 0;\n', ''),
 ('update: dma flag not cleared', A, '        job->pending = 0;                                   /* .record */\n        job->dma = 0;\n', '        job->pending = 0;                                   /* .record */\n'),
 ('update: off bit ignored', A, '        if (step & 0x80) {\n            continue;\n        }', '        if (0) {\n            continue;\n        }'),
 ('update: off from b6', A, '        if (step & 0x80) {\n            continue;\n        }', '        if (step & 0x40) {\n            continue;\n        }'),
 ('update: tile & $FF', A, 'u16 tile = (u16)(rec[step] & 0x1FF);', 'u16 tile = (u16)(rec[step] & 0xFF);'),
 ('update: tile not masked', A, 'u16 tile = (u16)(rec[step] & 0x1FF);', 'u16 tile = rec[step];'),
 ('update: always queued', A, '        if (tile != g->anim_tile_shown[r]) {', '        if (1) {'),
 ('update: shown not kept', A, '            g->anim_tile_shown[r] = tile;\n', ''),
 ('update: word 0 compared masked', A, '            if (tile == rec[0]) {', '            if (tile == (rec[0] & 0x1FF)) {'),
 ('update: word 0 never restored', A, '            if (tile == rec[0]) {', '            if (0) {'),
 ('update: CPU entry not pending', A, '                job->pending = 1;\n                job->src = HZ_ANIM_GFX_RAM', '                job->src = HZ_ANIM_GFX_RAM'),
 ('update: CPU entry as DMA', A, '                job->pending = 1;\n                job->src = HZ_ANIM_GFX_RAM', '                job->pending = 1;\n                job->dma = 1;\n                job->src = HZ_ANIM_GFX_RAM'),
 ('update: picture of the next record', A, 'job->src = HZ_ANIM_GFX_RAM + (u32)r * 32;', 'job->src = HZ_ANIM_GFX_RAM + (u32)(r + 1) * 32;'),
 ('update: CPU command without $4000', A, 'u16 addr = (u16)(((rec[0] << 5) & 0x3FFF) | 0x4000);', 'u16 addr = (u16)((rec[0] << 5) & 0x3FFF);'),
 ('update: CPU command one word on', A, 'u16 addr = (u16)(((rec[0] << 5) & 0x3FFF) | 0x4000);', 'u16 addr = (u16)((((rec[0] << 5) + 2) & 0x3FFF) | 0x4000);'),
 ('update: DMA entry not DMA', A, '                job->pending = 1;\n                job->dma = 1;\n                u16 src', '                job->pending = 1;\n                u16 src'),
 ('update: DMA entry not pending', A, '                job->pending = 1;\n                job->dma = 1;\n                u16 src', '                job->dma = 1;\n                u16 src'),
 ('update: DMA source tile x 16', A, 'u16 src = (u16)((tile & 0x1FF) << 5);', 'u16 src = (u16)((tile & 0x1FF) << 4);'),
 ('update: DMA source bytes swapped', A, 'job->src = ((u32)(0x9500 | (src & 0xFF)) << 16)\n                         | (u32)(0x9600 | (src >> 8));', 'job->src = ((u32)(0x9500 | (src >> 8)) << 16)\n                         | (u32)(0x9600 | (src & 0xFF));'),
 ('update: DMA source register 21 only', A, 'job->src = ((u32)(0x9500 | (src & 0xFF)) << 16)\n                         | (u32)(0x9600 | (src >> 8));', 'job->src = ((u32)(0x9500 | (src & 0xFF)) << 16)\n                         | (u32)(0x9500 | (src & 0xFF));'),
 ('update: DMA to the source', A, 'u16 dst = (u16)((rec[0] << 5) & 0x3FFF);', 'u16 dst = (u16)((tile << 5) & 0x3FFF);'),
 ('update: DMA command low half cleared', A, 'job->cmd = ((u32)dst << 16) | (job->cmd & 0xFFFF);', 'job->cmd = (u32)dst << 16;'),
 ('update: no wrap after 15', A, '        if (step == 15) {                                   /* .advance */\n            g->anim_tile_step[r] = 0;\n        } else if', '        if (step == 16) {                                   /* .advance */\n            g->anim_tile_step[r] = 0;\n        } else if'),
 ('update: wrap after 14', A, '        if (step == 15) {                                   /* .advance */', '        if (step == 14) {'),
 ('update: 0 word not an end', A, '        } else if ((rec[step + 1] & 0x1FF) == 0) {          /* .next_word */', '        } else if (0) {'),
 ('update: end on a 0 word, high bits too', A, '        } else if ((rec[step + 1] & 0x1FF) == 0) {          /* .next_word */', '        } else if (rec[step + 1] == 0) {'),
 ('update: end tested on this word', A, '        } else if ((rec[step + 1] & 0x1FF) == 0) {          /* .next_word */', '        } else if ((rec[step] & 0x1FF) == 0) {'),
 ('update: two steps', A, 'g->anim_tile_step[r] = (u8)((step + 1) & 0x0F);  /* .step_on */', 'g->anim_tile_step[r] = (u8)((step + 2) & 0x0F);'),
 ('update: end goes to 1', A, '        } else if ((rec[step + 1] & 0x1FF) == 0) {          /* .next_word */\n            g->anim_tile_step[r] = 0;', '        } else if ((rec[step + 1] & 0x1FF) == 0) {          /* .next_word */\n            g->anim_tile_step[r] = 1;'),
 # Map_AnimTilesUpload
 ('upload: group 0 only', A, '    if (g->unit_ai_group != 0 && g->unit_ai_group != 6) {\n        return;\n    }\n    Vdp *v', '    if (g->unit_ai_group != 0) {\n        return;\n    }\n    Vdp *v'),
 ('upload: every frame', A, '    if (g->unit_ai_group != 0 && g->unit_ai_group != 6) {\n        return;\n    }\n    Vdp *v', '    Vdp *v'),
 ('upload: pending ignored', A, '        if (job->pending == 0) {                            /* .entry */', '        if (0) {'),
 ('upload: CPU and DMA swapped', A, '        if (job->dma == 0) {\n            vdp_reg(v, 0x8F02);                             /* .cpu_wait */', '        if (job->dma != 0) {\n            vdp_reg(v, 0x8F02);                             /* .cpu_wait */'),
 ('upload: CPU auto-increment 1', A, '            vdp_reg(v, 0x8F02);                             /* .cpu_wait */', '            vdp_reg(v, 0x8F01);'),
 ('upload: CPU writes 16 bytes', A, 'vdp_write_bytes(v, anim_gfx(g, job->src), 32);  /* .cpu_copy */', 'vdp_write_bytes(v, anim_gfx(g, job->src), 16);'),
 ('upload: CPU picture of record 0', A, 'vdp_write_bytes(v, anim_gfx(g, job->src), 32);  /* .cpu_copy */', 'vdp_write_bytes(v, g->anim_tile_gfx[0], 32);'),
 ('upload: DMA auto-increment 2', A, '            vdp_reg(v, 0x8F01);\n            vdp_reg(v, 0x9320);', '            vdp_reg(v, 0x8F02);\n            vdp_reg(v, 0x9320);'),
 ('upload: DMA length $10', A, '            vdp_reg(v, 0x9320);', '            vdp_reg(v, 0x9310);'),
 ('upload: DMA source high register skipped', A, '            vdp_reg(v, (u16)job->src);\n', ''),
 ('upload: DMA no copy', A, '            vdp_vram_copy(v);\n', ''),
 ('upload: DMA command low bits', A, 'vdp_command(v, (job->cmd & 0xFFFF0000u) | 0x00C0);', 'vdp_command(v, (job->cmd & 0xFFFF0000u) | 0x00C1);'),
 ('upload: auto-increment 1 after', A, '    vdp_reg(v, 0x8F02);                                     /* .done_wait */', '    vdp_reg(v, 0x8F01);'),
 ('upload: display register not set', A, '    vdp_reg(v, 0x8F02);                                     /* .done_wait */\n    vdp_reg(v, 0x8124);\n', '    vdp_reg(v, 0x8F02);                                     /* .done_wait */\n'),
 ('cycle: counter not stepped', A, '        g->colour_cycle[k] = (u16)(g->colour_cycle[k] + 1);\n', ''),
 ('cycle: one counter for both', A, 'g->colour_cycle[k] = (u16)(g->colour_cycle[k] + 1);', 'g->colour_cycle[0] = (u16)(g->colour_cycle[0] + (k == 0));'),
 ('cycle: colour & 7', A, 'vdp_write16(v, g->assets->colour_cycles[k][g->colour_cycle[k] & 0x0F]);', 'vdp_write16(v, g->assets->colour_cycles[k][g->colour_cycle[k] & 0x07]);'),
 ('cycle: colour before the step', A, '        g->colour_cycle[k] = (u16)(g->colour_cycle[k] + 1);\n        vdp_command(v, COLOUR_CYCLE_CRAM[k]);\n        vdp_write16(v, g->assets->colour_cycles[k][g->colour_cycle[k] & 0x0F]);', '        vdp_command(v, COLOUR_CYCLE_CRAM[k]);\n        vdp_write16(v, g->assets->colour_cycles[k][g->colour_cycle[k] & 0x0F]);\n        g->colour_cycle[k] = (u16)(g->colour_cycle[k] + 1);'),
 ('cycle: tables swapped', A, 'vdp_write16(v, g->assets->colour_cycles[k][g->colour_cycle[k] & 0x0F]);', 'vdp_write16(v, g->assets->colour_cycles[1 - k][g->colour_cycle[k] & 0x0F]);'),
 ('tables: ColourCycleCRAM colour 3', 'src/game/tables.c', '{ 0xC0040000, 0xC0160000 }', '{ 0xC0060000, 0xC0160000 }'),
 # the VDP's copy
 ('vdp: copy one byte short', 'src/core/hz_vdp.c', '    for (u32 i = 0; i < length; i++) {\n        v->vram[v->addr] = v->vram[src];', '    for (u32 i = 1; i < length; i++) {\n        v->vram[v->addr] = v->vram[src];'),
 ('vdp: copy source steps by 2', 'src/core/hz_vdp.c', '        src = (u16)(src + 1);', '        src = (u16)(src + 2);'),
 ('vdp: copy source halved', 'src/core/hz_vdp.c', '    u16 src = (u16)(v->reg[21] | (v->reg[22] << 8));', '    u16 src = (u16)((v->reg[21] | (v->reg[22] << 8)) >> 1);'),
 ('vdp: copy registers swapped', 'src/core/hz_vdp.c', '    u16 src = (u16)(v->reg[21] | (v->reg[22] << 8));', '    u16 src = (u16)(v->reg[22] | (v->reg[21] << 8));'),
 # Map_InitAnimTiles
 ('init: picture of tile + 1', 'src/game/load.c', 'memcpy(g->anim_tile_gfx[r], g->map_tiles + tile * 32u, 32);', 'memcpy(g->anim_tile_gfx[r], g->map_tiles + (tile + 1) * 32u, 32);'),
 ('init: shown 0', 'src/game/load.c', '        g->anim_tile_shown[r] = tile;', '        g->anim_tile_shown[r] = 0;'),
 ('init: off never set', 'src/game/load.c', 'g->anim_tile_step[r] = (tile == 0) ? 0x80 : 0;', 'g->anim_tile_step[r] = 0;'),
 # the frame
 ('frame: no Map_AnimTilesUpdate', 'src/game/game.c', '    map_anim_tiles_update(g);\n', ''),
 ('frame: no Map_AnimTilesUpload', 'src/game/game.c', '    map_anim_tiles_upload(g);\n', ''),
 ('frame: uploaded before the scroll', 'src/game/game.c', '    map_flush_scroll(g);\n    /* OrderMenu_UpdateBoth: not ported. */\n    map_anim_tiles_upload(g);', '    map_anim_tiles_upload(g);\n    map_flush_scroll(g);\n    /* OrderMenu_UpdateBoth: not ported. */'),
 ('frame: group stepped after the anim', 'src/game/game.c', '    mechs_update(g);\n    unit_ai_update(g);\n    /* Combat_Update: not ported. */\n    game_frame_draw(g);', '    mechs_update(g);\n    /* Combat_Update: not ported. */\n    game_frame_draw(g);\n    unit_ai_update(g);'),
]
