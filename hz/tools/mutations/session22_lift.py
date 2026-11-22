"""Session 22's mutations of lifting and dropping (mech_update.c: .lift,
.drop, Mech_FindUnitToLift's tests that only a lift reaches; units.c:
Grid_ClearObj) and of MapFx_Update's call at the frame's end (game.c: the
gap left in session 21), run with CHECK 'mech'. See HANDOVER.md for the
ones expected not to be caught."""

CHECK = 'mech'

M = 'src/game/mech_update.c'
U = 'src/game/units.c'
G = 'src/game/game.c'
MUTATIONS = [
 # .lift
 ('lift: never', M, "                mech_lift(g, m, rival, slot);           /* .lift */", "                (void)slot;"),
 ('lift: slot not stored', M, "    m->carried_slot = (u8)slot;\n", ""),
 ('lift: order not copied', M, "    m->carried_order = u->order;", "    m->carried_order = 0;"),
 ('lift: mech b3 not set', M, "    m->flags |= 0x08;\n    u->flags |= 0x08;\n", "    u->flags |= 0x08;\n"),
 ('lift: unit b3 not set', M, "    u->flags |= 0x08;\n    m->form = 2;", "    m->form = 2;"),
 ('lift: form 1', M, "    m->form = 2;\n    m->max_speed = kidou->max_speed[HZ_FORM_CARRYING];", "    m->form = 1;\n    m->max_speed = kidou->max_speed[HZ_FORM_CARRYING];"),
 ('lift: jet speed', M, "    m->max_speed = kidou->max_speed[HZ_FORM_CARRYING];  /* +$22 */\n    u8 use", "    m->max_speed = kidou->max_speed[HZ_FORM_JET];  /* +$22 */\n    u8 use"),
 ('lift: no +1 against a CPU', M, "    u8 use = fuel_use_for(kidou, HZ_FORM_CARRYING, m, rival);", "    u8 use = kidou->fuel_use[HZ_FORM_CARRYING];"),
 ('lift: jet fuel use', M, "    u8 use = fuel_use_for(kidou, HZ_FORM_CARRYING, m, rival);", "    u8 use = fuel_use_for(kidou, HZ_FORM_JET, m, rival);"),
 ('lift: halved in the demo', M, "    if (g->game_type >= 0 && ((m->flags | rival->flags) & 0x01)\n        && g->options[1] == 0) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */", "    if (((m->flags | rival->flags) & 0x01)\n        && g->options[1] == 0) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */"),
 ('lift: halved with no CPU', M, "    if (g->game_type >= 0 && ((m->flags | rival->flags) & 0x01)\n        && g->options[1] == 0) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */", "    if (g->game_type >= 0\n        && g->options[1] == 0) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */"),
 ('lift: halved only for this CPU', M, "    if (g->game_type >= 0 && ((m->flags | rival->flags) & 0x01)\n        && g->options[1] == 0) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */", "    if (g->game_type >= 0 && (m->flags & 0x01)\n        && g->options[1] == 0) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */"),
 ('lift: halved on any difficulty', M, "    if (g->game_type >= 0 && ((m->flags | rival->flags) & 0x01)\n        && g->options[1] == 0) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */", "    if (g->game_type >= 0 && ((m->flags | rival->flags) & 0x01)) {\n        use = (u8)(use >> 1);                           /* .lift_fuel */"),
 ('lift: never halved', M, "        use = (u8)(use >> 1);                           /* .lift_fuel */", "        use = use;                           /* .lift_fuel */"),
 ('lift: grid not cleared', M, "    grid_clear_unit(g, u);\n", ""),
 ('lift: wrong sfx', M, "    snd_queue_sfx(g, SFX_LIFT);", "    snd_queue_sfx(g, SFX_DROP);"),
 ('grid clear: next cell', U, "    g->grid[u->grid_cell & 0xFFF] = 0;\n}\n", "    g->grid[(u->grid_cell + 1) & 0xFFF] = 0;\n}\n"),
 # Mech_FindUnitToLift (reached by a lift only)
 ('find: the other team', M, "        if ((u8)((m->flags << 3) ^ id) & 0x80) {", "        if (!((u8)((m->flags << 3) ^ id) & 0x80)) {"),
 ('find: dying units too', M, "        if (u->flags & 0x20) {\n            continue;\n        }\n        u16 reach", "        if (0) {\n            continue;\n        }\n        u16 reach"),
 ('find: reach + 8', M, "        u16 reach = (u16)((u8)(0x20 - u->size) + 0x10);", "        u16 reach = (u16)((u8)(0x20 - u->size) + 0x08);"),
 ('find: x without + 8', M, "        u16 x = (u16)(u->x + 8);", "        u16 x = (u16)(u->x);"),
 ('find: only the centre cell', M, "    for (int n = 0; n < 9; n++) {                       /* .neighbour */\n        u16 c = (u16)(cell + (n == 0 ? 0 : neighbour_offset(n - 1)));\n        if (c >= 0x1000) {\n            continue;\n        }\n        u8 id = g->grid[c];                             /* .cell */\n        if (id == 0 || !(id & 0x40)) {", "    for (int n = 0; n < 1; n++) {                       /* .neighbour */\n        u16 c = (u16)(cell + (n == 0 ? 0 : neighbour_offset(n - 1)));\n        if (c >= 0x1000) {\n            continue;\n        }\n        u8 id = g->grid[c];                             /* .cell */\n        if (id == 0 || !(id & 0x40)) {"),
 # .drop
 ('drop: never', M, "            mech_drop(g, m, rival);                     /* .drop */", "            ;"),
 ('drop: blocked cell ignored', M, "    if (mech_check_land_blocked(g, m)) {\n        return;\n    }\n    int terrain", "    int terrain"),
 ('drop: boat on any ground', M, "        if (terrain != 4) {\n            return;\n        }", "        if (terrain == 3) {\n            return;\n        }"),
 ('drop: boat test b1', M, "    if (u->flags & 0x01) {                              /* .drop_boat */", "    if (u->flags & 0x02) {                              /* .drop_boat */"),
 ('drop: CPU on terrain 1', M, "        if ((m->flags & 0x01) || terrain != 1) {", "        if (terrain != 1) {"),
 ('drop: human not on terrain 1', M, "        if ((m->flags & 0x01) || terrain != 1) {", "        if (1) {"),
 ('drop: terrain 2 allowed', M, "        if ((m->flags & 0x01) || terrain != 1) {", "        if ((m->flags & 0x01) || terrain > 2) {"),
 ('drop: the other team\'s record', M, "    int i = ((m->flags & 0x10) ? HZ_UNITS / 2 : 0) + m->carried_slot;\n    HzUnit *u", "    int i = ((m->flags & 0x10) ? 0 : HZ_UNITS / 2) + m->carried_slot;\n    HzUnit *u"),
 ('drop: stays form 2', M, "    m->form = 1;\n    m->max_speed = kidou->max_speed[HZ_FORM_JET];       /* +$21 */", "    m->max_speed = kidou->max_speed[HZ_FORM_JET];       /* +$21 */"),
 ('drop: carrying speed', M, "    m->max_speed = kidou->max_speed[HZ_FORM_JET];       /* +$21 */\n    m->fuel_use = fuel_use_for(kidou, HZ_FORM_JET, m, rival);\n    m->vel_x = 0;", "    m->max_speed = kidou->max_speed[HZ_FORM_CARRYING];       /* +$21 */\n    m->fuel_use = fuel_use_for(kidou, HZ_FORM_JET, m, rival);\n    m->vel_x = 0;"),
 ('drop: no +1 against a CPU', M, "    m->fuel_use = fuel_use_for(kidou, HZ_FORM_JET, m, rival);\n    m->vel_x = 0;", "    m->fuel_use = kidou->fuel_use[HZ_FORM_JET];\n    m->vel_x = 0;"),
 ('drop: velocity x kept', M, "    m->vel_x = 0;                                       /* .drop_store_fuel */\n", ""),
 ('drop: target velocity y kept', M, "    m->target_vel_y = 0;\n\n    const HzUnitStats *st", "\n    const HzUnitStats *st"),
 ('drop: offset not halved', M, "    u16 half = (u16)((u8)(0x20 - u->size) >> 1);", "    u16 half = (u16)(u8)(0x20 - u->size);"),
 ('drop: offset from $18', M, "    u16 half = (u16)((u8)(0x20 - u->size) >> 1);", "    u16 half = (u16)((u8)(0x18 - u->size) >> 1);"),
 ('drop: order not given', M, "    u->order = m->carried_order;\n", ""),
 ('drop: y not offset', M, "    u->y = (s16)(m->y + half);", "    u->y = (s16)(m->y);"),
 ('drop: max speed not reset', M, "    u->max_speed = st->speed;\n", ""),
 ('drop: speed high byte cleared', M, "    u->speed = (u16)((u->speed & 0xFF00) | u->max_speed);", "    u->speed = u->max_speed;"),
 ('drop: speed not set', M, "    u->speed = (u16)((u->speed & 0xFF00) | u->max_speed);\n", ""),
 ('drop: unit velocity kept', M, "    u->vel_x = 0;\n    u->vel_y = 0;\n    u8 d", "    u8 d"),
 ('drop: heading not + 1', M, "    u8 d = (u8)((m->dir + 1) & 0x0F);", "    u8 d = m->dir;"),
 ('drop: turned to the new heading', M, "    d = (u8)((d - 1) & 0x0F);\n    u->turn_to = d;", "    u->turn_to = d;"),
 ('drop: turret direction kept', M, "    u->turret_dir = d;\n", ""),
 ('drop: aim direction kept', M, "    u->aim_dir = d;\n    m->flags", "    m->flags"),
 ('drop: mech b3 kept', M, "    m->flags &= (u8)~0x08;\n    u->flags &= (u8)~0x08;", "    u->flags &= (u8)~0x08;"),
 ('drop: unit b3 kept', M, "    m->flags &= (u8)~0x08;\n    u->flags &= (u8)~0x08;", "    m->flags &= (u8)~0x08;"),
 ('drop: shot flags kept', M, "    u->shot_flags = 0;\n    u->blocked = 0;\n    memset", "    u->blocked = 0;\n    memset"),
 ('drop: id team from the mech', M, "    if (u->flags & 0x10) {\n        id = 0xC0;", "    if (m->flags & 0x10) {\n        id = 0xC0;"),
 ('drop: id slot not + 1', M, "    id |= (u8)((m->carried_slot + 1) & 0x3F);", "    id |= (u8)(m->carried_slot & 0x3F);"),
 ('drop: no grid mark', M, "    grid_mark_unit(g, u, id);\n    unit_set_sprite", "    unit_set_sprite"),
 ('drop: team 1 palette for both', M, "        pal = 0x6000;", "        pal = 0x4000;"),
 ('drop: no Unit_SetSprite', M, "    unit_set_sprite(g, u, pal);\n", ""),
 ('drop: hit flags not cleared', M, "    g->hit_flags = 0;\n    g->hit_id = 0;\n    obj_check_terrain", "    g->hit_id = 0;\n    obj_check_terrain"),
 ('drop: hit id not cleared', M, "    g->hit_id = 0;\n    obj_check_terrain", "    obj_check_terrain"),
 ('drop: no terrain check', M, "    obj_check_terrain(g, u);\n    u->blocked", "    u->blocked"),
 ('drop: blocked not stored', M, "    u->blocked = (u8)g->hit_flags;\n    snd_queue_sfx(g, SFX_DROP);", "    snd_queue_sfx(g, SFX_DROP);"),
 ('drop: no sfx', M, "    snd_queue_sfx(g, SFX_DROP);\n", ""),
 # session 21's gap
 ('frame: no MapFx_Update', G, "    map_fx_update(g);\n", ""),
]
