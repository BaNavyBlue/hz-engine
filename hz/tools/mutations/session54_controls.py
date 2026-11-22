"""Session 54's mutations: the controls made any gamepad's (src/core/keys.c
on its own is tools/hz_controls.c's, `make controls`) -- the BUTTONS page,
"pad" in settings.json, PAD 1's labels, HZ_RAW's taps through the
controls. Standalone, as session 26's, run from the tree's root, a range
at a time:

    python3 tools/mutations/session54_controls.py FIRST END

Each mutation is applied, hz_headless, hz_sound, hz_ymchan and hz_rumble
are built, tools/hzmenu_check.py is run, and the file is put back (also
on SIGTERM). Run it on a copy of the tree: it edits and rebuilds in place.

Session 54: 14 of 14 caught (1, BUTTONS going back to the menu, by the
case "buttons: Escape goes back to the GAMEPAD page", added for it; the
first round, without that case, caught 13).
"""
import subprocess, sys, signal
signal.signal(signal.SIGTERM, lambda *a: sys.exit(1))
M = [
 # a key binds on BUTTONS
 ('src/game/sysmenu.c', '    if (!pad && key_bindable(in->key)) {', '    if (key_bindable(in->key)) {'),
 # BUTTONS goes back to the menu, not GAMEPAD
 ('src/game/sysmenu.c', '        } else if (m->page == SYSMENU_PAGE_BUTTONS) {\n            m->page = SYSMENU_PAGE_GAMEPAD;', '        } else if (m->page == SYSMENU_PAGE_BUTTONS) {\n            m->page = SYSMENU_PAGE_MAIN;'),
 # BUTTONS' DEFAULTS puts the keys back too
 ('src/game/sysmenu.c', '            controls_pad_default(&m->controls);\n            m->controls_changed = true;', '            controls_default(&m->controls);\n            m->controls_changed = true;'),
 # session 26's "pad" read into the second slot
 ('src/game/sysmenu.c', '            load_pad_slot(c, a, 0, v);', '            load_pad_slot(c, a, 1, v);'),
 # both slots written as the first
 ('src/game/sysmenu.c', '            pad_input_name(m->controls.pad[a][s], name, sizeof name);', '            pad_input_name(m->controls.pad[a][0], name, sizeof name);'),
 # BUTTONS drawn without PAD 1's labels
 ('src/game/sysmenu.c', '    pad_input_label(m->controls.pad[action][slot], m->pad_style, out, n);', '    pad_input_label(m->controls.pad[action][slot], PAD_STYLE_NONE, out, n);'),
 # PAD 1's style never handed over
 ('src/main.c', '    m->pad_style = platform_pad_style(h->platform, 0);\n', ''),
 # HZ_RAW's gamepad taps not through the controls
 ('src/platform/platform_headless.c', '                buttons_add(&in->pad[0], controls_pad_actions(&p->controls,\n                                                              &h));', '                (void)h;'),
 # the headless platform keeps no controls
 ('src/platform/platform_headless.c', '    p->controls = *c;', '    (void)c;'),
 # BUTTONS' slot is CONTROLS' column
 ('src/game/sysmenu.c', '    int action = pad ? m->buttons_row : m->controls_row;\n    int col = pad ? m->buttons_col : m->controls_col;', '    int action = pad ? m->buttons_row : m->controls_row;\n    int col = m->controls_col;'),
 # the GAMEPAD page's BUTTONS row opens nothing
 ('src/game/sysmenu.c', '        } else if (m->gamepad_cursor == SYSMENU_GAMEPAD_BUTTONS) {\n            m->page = SYSMENU_PAGE_BUTTONS;', '        } else if (m->gamepad_cursor == SYSMENU_GAMEPAD_BUTTONS) {\n            m->page = SYSMENU_PAGE_GAMEPAD;'),
 # a pad input bound on BUTTONS left in the slot it was in
 ('src/core/keys.c', '                if (c->pad[a][s] == input) {\n                    c->pad[a][s] = PAD_NONE;', '                if (c->pad[a][s] == input && 0) {\n                    c->pad[a][s] = PAD_NONE;'),
 # Back as a raw gamepad press is no Escape
 ('src/platform/platform_headless.c', '        if (in->pad_input == PAD_BACK) {\n            in->menu = true;', '        if (0) {\n            in->menu = true;'),
 # Backspace on BUTTONS empties the key slot instead
 ('src/game/sysmenu.c', '        if (pad) {\n            controls_bind_pad(&m->controls, action, col, PAD_NONE);', '        if (0) {\n            controls_bind_pad(&m->controls, action, col, PAD_NONE);'),
]
lo, hi = int(sys.argv[1]), int(sys.argv[2])
for i in range(lo, min(hi, len(M))):
    f, old, new = M[i]
    src = open(f).read()
    assert src.count(old) == 1, (i, f)
    open(f, 'w').write(src.replace(old, new))
    try:
        b = subprocess.run(['make', 'hz_headless', 'hz_sound', 'hz_ymchan', 'hz_rumble'], capture_output=True, text=True)
        if b.returncode != 0:
            print(i, 'BUILD FAILED'); continue
        r = subprocess.run(['python3', 'tools/hzmenu_check.py', './hz_headless', 'assets'], capture_output=True, text=True, timeout=600)
        bad = [l.split('  ')[0] for l in r.stdout.splitlines() if 'FAILED' in l]
        print(i, 'caught' if r.returncode else 'NOT CAUGHT', bad[:3])
    finally:
        open(f, 'w').write(src)
subprocess.run(['make', 'hz_headless', 'hz_sound', 'hz_ymchan', 'hz_rumble'], capture_output=True)
