/* (Session 78) hz_extract.py's extract_demos in C (ex.h): DemoTable as
 * data/demos.json, and each map's DemoData (the recording's units and
 * both pads' runs) as its folder's demo.json. */
#include "ex.h"

#include <stdio.h>
#include <string.h>

/* v_joypad bits 0-7: Up Down Left Right B C A Start. */
static const char PAD_LETTERS[] = "UDLRBCAS";

/* DEMO_SCREENS: a DemoTable record's three words, the screen they make. */
static const struct {
    u16 a, b, c;
    const char *name;
} DEMO_SCREENS[] = {
    { 1, 0x0000, 0x0101, "split" },
    { 0, 0x0000, 0xFF01, "wide P1" },
    { 0, 0xFF00, 0x0001, "wide P2" },
};

static u16 rd16(const u8 *b, int o)
{
    return (u16)((b[o] << 8) | b[o + 1]);
}

/* pad_run: "44 UL", the frames and then the buttons held, - for none. */
static ExJson *pad_run(int buttons, int frames)
{
    char held[9];
    int k = 0;
    for (int i = 0; i < 8; i++) {
        if ((buttons >> i) & 1) {
            held[k++] = PAD_LETTERS[i];
        }
    }
    held[k] = '\0';
    return exj_strf("%d %s", frames, (k > 0) ? held : "-");
}

/* demo_stream: a stream's runs, merged where the recorder split them at
 * 255 frames (it starts a new run with the same buttons only then). */
static ExJson *demo_stream(HzEx *x, int map_n, const u8 *b, int len)
{
    static int buttons[0x400], frames[0x400], last[0x400];
    int runs = 0;
    bool ended = false;
    for (int i = 0; i < len; i += 2) {
        int bt = b[i], fr = b[i + 1];
        if (fr == 0) {
            for (int j = i; j < len; j++) {
                if (b[j] != 0) {
                    ex_fail(x, "DemoData map %d: bytes after a stream's end",
                            map_n);
                    return NULL;
                }
            }
            ended = true;
            break;
        }
        if (runs > 0 && buttons[runs - 1] == bt) {
            if (last[runs - 1] != 255) {
                ex_fail(x, "DemoData map %d: a run split short of 255",
                        map_n);
                return NULL;
            }
            frames[runs - 1] += fr;
            last[runs - 1] = fr;
        } else {
            buttons[runs] = bt;
            frames[runs] = fr;
            last[runs] = fr;
            runs++;
        }
    }
    if (!ended) {
        ex_fail(x, "DemoData map %d: a stream without an end", map_n);
        return NULL;
    }
    ExJson *list = exj_array();
    for (int r = 0; r < runs; r++) {
        exj_push(list, pad_run(buttons[r], frames[r]));
    }
    return list;
}

/* extract_demos. */
bool ex_demos(HzEx *x)
{
    const u8 *table = ex_at(x, EXR_DemoTable, 16 * 8);
    const u8 *data = ex_at(x, EXR_DemoData, 8 * 0x800);
    if (table == NULL || data == NULL) {
        return false;
    }
    ExJson *demos = exj_array();
    for (int k = 0; k < 16; k++) {
        const u8 *r = table + k * 8;
        const char *screen = NULL;
        for (size_t s = 0; s < sizeof DEMO_SCREENS / sizeof DEMO_SCREENS[0];
             s++) {
            if (rd16(r, 2) == DEMO_SCREENS[s].a
                && rd16(r, 4) == DEMO_SCREENS[s].b
                && rd16(r, 6) == DEMO_SCREENS[s].c) {
                screen = DEMO_SCREENS[s].name;
            }
        }
        if (r[0] != 0 || r[1] > 7 || screen == NULL) {
            exj_free(demos);
            return ex_fail(x, "DemoTable %d: not a map and screen the port "
                           "knows", k);
        }
        ExJson *d = exj_object();
        exj_set(d, "map", exj_int(r[1]));
        exj_set(d, "screen", exj_str(screen));
        exj_push(demos, d);
    }
    ExJson *top = exj_object();
    exj_set(top, "demos", demos);
    if (!ex_write_json(x, "data/demos.json", top)) {
        return false;
    }

    for (int n = 0; n < 8; n++) {
        const u8 *b = data + n * 0x800;
        ExJson *units = exj_array();
        if (!ex_unit_list(units, b)) {
            exj_free(units);
            return ex_fail(x, "DemoData map %d: a gap in the units", n);
        }
        ExJson *p1 = demo_stream(x, n, b + 0x100, 0x380);
        ExJson *p2 = (p1 != NULL) ? demo_stream(x, n, b + 0x480, 0x380)
                                  : NULL;
        if (p1 == NULL || p2 == NULL) {
            exj_free(units);
            exj_free(p1);
            return false;
        }
        ExJson *demo = exj_object();
        exj_set(demo, "units", units);
        exj_set(demo, "p1", p1);
        exj_set(demo, "p2", p2);
        char rel[64];
        snprintf(rel, sizeof rel, "maps/%d_%s/demo.json", n, EX_MAP_NAMES[n]);
        if (!ex_write_json(x, rel, demo)) {
            return false;
        }
    }
    return true;
}
