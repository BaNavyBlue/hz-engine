"""Session 26's mutations: the Esc screen, the controls and the music/
effects split. Standalone (not tools/hz_mutate.py's list format: these
touch several files), run from the tree's root, a range at a time so a run
fits the sandbox's 300 s (~35 s a mutation):

    python3 tools/mutations/session26_menu.py FIRST END

Each mutation is applied, hz_headless, hz_sound and hz_ymchan are built,
tools/hzmenu_check.py is run, and the file is put back (also on SIGTERM).
Session 26: 16 of 18 caught; 15 (PSG tone 2 judged by tone 1's bit) and 16
(the noise's release left out) are equivalent with the shipping data, which
has no effect on the PSG tones and no noise still releasing at an effect's
end. Session 54 moved two to the code that replaced theirs (Back never
bound: pad_input_bindable; CONTROLS' DEFAULTS: controls_keys_default).
"""
import subprocess, sys, signal
signal.signal(signal.SIGTERM, lambda *a: sys.exit(1))
M = [
 ('src/main.c', '    swallow(h, &in);\n    route_input', '    route_input'),
 ('src/core/keys.c', '                if (c->key[a][s] == code) {\n                    c->key[a][s] = KEY_NONE;', '                if (c->key[a][s] == code && 0) {\n                    c->key[a][s] = KEY_NONE;'),
 ('src/main.c', '    if (bytes[0] == 0x02 && h->menu.audio.effects_db <= AUDIO_DB_OFF) {', '    if (0) {'),
 ('src/core/audio.c', 'HZ_AUDIO_CYCLE_CHANNEL[6] = { 1, 5, 3, 0, 4, 2 }', 'HZ_AUDIO_CYCLE_CHANNEL[6] = { 0, 1, 2, 3, 4, 5 }'),
 ('src/core/audio.c', '    g[2] = MIX_PSG_GAIN * (noise_fx ? a->effects_gain : a->music_gain);', '    g[2] = MIX_PSG_GAIN * a->music_gain;'),
 ('src/core/audio.c', '        a->fm_fx = a->queue[a->q_tail].reg;', '        a->fm_fx = 0;'),
 ('src/game/sysmenu.c', '    if (in->back) {', '    if (0) {'),
 ('src/game/sysmenu.c', '    if (in->menu) {\n        m->listening = false;', '    if (0) {\n        m->listening = false;'),
 ('src/core/keys.c', '    return pad_input_valid(input) && input != PAD_BACK;', '    return pad_input_valid(input);'),
 ('src/game/sysmenu.c', '    return (db < SYSMENU_DB_MIN) ? AUDIO_DB_OFF : (int)db;', '    return (int)db;'),
 ('src/game/sysmenu.c', '            controls_keys_default(&m->controls);\n            m->controls_changed = true;', '            m->controls_changed = true;'),
 ('src/main.c', '        if (m->quit) {\n            return false;', '        if (0) {\n            return false;'),
 ('src/core/audio.c', '        out[c] = ((float)music[c] * a->music_gain\n                  + (float)effects[c] * a->effects_gain)', '        out[c] = ((float)music[c] * a->effects_gain\n                  + (float)effects[c] * a->music_gain)'),
 ('src/game/sysmenu.c', '    m->prev = now;\n', '\n'),
 ('src/game/sysmenu.c', '    if (v->type == JSON_STRING && strcmp(v->string, "off") == 0) {', '    if (0) {'),
 ('src/core/audio.c', '    g[1] = MIX_PSG_GAIN * ((a->snd.sfx_on & 0x80)', '    g[1] = MIX_PSG_GAIN * ((a->snd.sfx_on & 0x40)'),
 ('src/core/audio.c', '    bool noise_fx = a->snd.sfx8_on != 0 || a->snd.sfx[8].env_state != 0;', '    bool noise_fx = a->snd.sfx8_on != 0;'),
 ('src/game/sysmenu.c', '    if (in->menu || press.b) {', '    if (in->menu) {'),
]
lo, hi = int(sys.argv[1]), int(sys.argv[2])
for i in range(lo, min(hi, len(M))):
    f, old, new = M[i]
    src = open(f).read()
    assert src.count(old) == 1, (i, f)
    open(f, 'w').write(src.replace(old, new))
    try:
        b = subprocess.run(['make', 'hz_headless', 'hz_sound', 'hz_ymchan'], capture_output=True, text=True)
        if b.returncode != 0:
            print(i, 'BUILD FAILED'); continue
        r = subprocess.run(['python3', 'tools/hzmenu_check.py', './hz_headless', 'assets'], capture_output=True, text=True, timeout=150)
        bad = [l.split('  ')[0] for l in r.stdout.splitlines() if 'FAILED' in l]
        print(i, 'caught' if r.returncode else 'NOT CAUGHT', bad[:3])
    finally:
        open(f, 'w').write(src)
subprocess.run(['make', 'hz_headless', 'hz_sound', 'hz_ymchan'], capture_output=True)
