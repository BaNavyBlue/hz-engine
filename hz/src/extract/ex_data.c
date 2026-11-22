/* (Session 78) hz_extract.py's extract_data in C (ex.h): the units'
 * records, the units each map starts with, the sound effects' priorities
 * and the order menu, as JSON. */
#include "ex.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* UNIT_FIELDS: a UnitStats record from +$08 (name, offset, bytes, hex?). */
static const struct {
    const char *name;
    int  off, size;
    bool hex;
} UNIT_FIELDS[] = {
    { "price", 0x08, 2, false },       { "hp", 0x0A, 2, false },
    { "size", 0x0C, 1, false },        { "ammo", 0x0D, 1, false },
    { "fuel", 0x0E, 2, false },        { "sprite_size", 0x10, 1, true },
    { "speed", 0x11, 1, false },       { "overlay", 0x12, 1, true },
    { "fuel_use", 0x13, 1, false },    { "shot_speed", 0x14, 1, false },
    { "shot_life", 0x15, 1, false },   { "fire_delay", 0x16, 1, false },
    { "power", 0x17, 1, false },       { "sight", 0x18, 1, false },
    { "build_time", 0x19, 1, false },  { "missiles", 0x1A, 1, false },
    { "missile_speed", 0x1B, 1, false }, { "missile_life", 0x1C, 1, false },
    { "missile_delay", 0x1D, 1, false }, { "orders", 0x1E, 2, true },
};

/* font_string: printable ASCII as itself, anything else (and '{') as
 * {XX}. */
static ExJson *font_string(const u8 *b, int n)
{
    char s[128];
    size_t k = 0;
    for (int i = 0; i < n && k + 5 < sizeof s; i++) {
        if (b[i] >= 0x20 && b[i] < 0x7F && b[i] != '{') {
            s[k++] = (char)b[i];
        } else {
            k += (size_t)snprintf(s + k, sizeof s - k, "{%02X}", b[i]);
        }
    }
    s[k] = '\0';
    return exj_str(s);
}

/* sprite_attr: the tile in hex, then h / v for the flips, pal N and pri
 * for what else is set. */
static ExJson *sprite_attr(u16 w)
{
    char s[40];
    int k = snprintf(s, sizeof s, "$%03X", (unsigned)(w & 0x7FF));
    if (w & 0x1800) {
        k += snprintf(s + k, sizeof s - (size_t)k, " %s%s",
                      (w & 0x800) ? "h" : "", (w & 0x1000) ? "v" : "");
    }
    if (w & 0x6000) {
        k += snprintf(s + k, sizeof s - (size_t)k, " pal%d", (w >> 13) & 3);
    }
    if (w & 0x8000) {
        snprintf(s + k, sizeof s - (size_t)k, " pri");
    }
    return exj_str(s);
}

static u16 rd16(const u8 *b, int o)
{
    return (u16)((b[o] << 8) | b[o + 1]);
}

/* stat_record: the name, then UNIT_FIELDS, into rec. */
static void stat_record(ExJson *rec, const u8 *r)
{
    exj_set(rec, "name", font_string(r, 8));
    for (size_t i = 0; i < sizeof UNIT_FIELDS / sizeof UNIT_FIELDS[0]; i++) {
        int off = UNIT_FIELDS[i].off;
        unsigned v = (UNIT_FIELDS[i].size == 2) ? rd16(r, off) : r[off];
        exj_set(rec, UNIT_FIELDS[i].name,
                UNIT_FIELDS[i].hex
                    ? exj_strf("$%0*X", UNIT_FIELDS[i].size * 2, v)
                    : exj_int(v));
    }
}

static void upper(char *out, size_t n, const char *s)
{
    size_t i = 0;
    for (; s[i] != '\0' && i + 1 < n; i++) {
        out[i] = (char)toupper((unsigned char)s[i]);
    }
    out[i] = '\0';
}

static bool zero(const u8 *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (b[i] != 0) {
            return false;
        }
    }
    return true;
}

/* A start or demo unit's 8 bytes. */
ExJson *ex_unit_record(const u8 *r)
{
    ExJson *u = exj_object();
    exj_set(u, "flags", exj_strf("$%02X", r[0]));
    exj_set(u, "type", exj_int(r[1]));
    exj_set(u, "order", exj_int(r[2]));
    exj_set(u, "dir", exj_int(r[3]));
    exj_set(u, "x", exj_int(rd16(r, 4)));
    exj_set(u, "y", exj_int(rd16(r, 6)));
    return u;
}

/* The records of up to 32 at b, to the first all-0 one, after which the
 * rest must start with 0 (no gaps). False at a gap. */
bool ex_unit_list(ExJson *list, const u8 *b)
{
    for (int i = 0; i < 32; i++) {
        const u8 *r = b + i * 8;
        if (zero(r, 8)) {
            for (int j = i; j < 32; j++) {
                if (b[j * 8] != 0) {
                    return false;
                }
            }
            return true;
        }
        exj_push(list, ex_unit_record(r));
    }
    return true;
}

static bool units_json(HzEx *x)
{
    u32 stats = EXR_UnitStats, kidou = EXR_UnitStats_KIDOU;
    const u8 *all = ex_at(x, stats, kidou - stats + 0x28);
    const u8 *dirs = ex_at(x, EXR_UnitDirTable, 10 * 32);
    const u8 *expl = ex_at(x, EXR_ExplosionFrames, 32);
    if (all == NULL || dirs == NULL || expl == NULL) {
        return false;
    }
    ExJson *units = exj_array();
    for (u32 t = 0; t < (kidou - stats) / 32; t++) {
        ExJson *rec = exj_object();
        exj_set(rec, "type", exj_int(t));
        stat_record(rec, all + t * 32);
        if (t < 10) {
            ExJson *s = exj_array();
            for (int d = 0; d < 16; d++) {
                exj_push(s, sprite_attr(rd16(dirs, (int)t * 32 + d * 2)));
            }
            exj_set(rec, "sprites", s);
        }
        exj_push(units, rec);
    }
    const u8 *m = all + (kidou - stats);
    ExJson *mech = exj_object();
    stat_record(mech, m);
    const u8 *k = m + 0x20;
    ExJson *speed = exj_object(), *use = exj_object();
    exj_set(speed, "robot", exj_int(k[0]));
    exj_set(speed, "jet", exj_int(k[1]));
    exj_set(speed, "carrying", exj_int(k[2]));
    exj_set(use, "robot", exj_int(k[3]));
    exj_set(use, "jet", exj_int(k[4]));
    exj_set(use, "carrying", exj_int(k[5]));
    exj_set(mech, "max_speed", speed);
    exj_set(mech, "fuel_use_by_form", use);
    exj_set(mech, "byte_26", exj_int(k[6]));
    exj_set(mech, "byte_27", exj_int(k[7]));
    ExJson *frames = exj_array();
    for (int i = 0; i < 16; i++) {
        exj_push(frames, sprite_attr(rd16(expl, i * 2)));
    }
    ExJson *top = exj_object();
    exj_set(top, "units", units);
    exj_set(top, "mech", mech);
    exj_set(top, "explosion_frames", frames);
    return ex_write_json(x, "data/units.json", top);
}

static bool start_units_json(HzEx *x)
{
    const u8 *start = ex_at(x, EXR_StartUnits, 8 * 0x200);
    const u8 *cpu = ex_at(x, EXR_CPUStartTypeTable, 4);
    if (start == NULL || cpu == NULL) {
        return false;
    }
    ExJson *maps = exj_array();
    for (int n = 0; n < 8; n++) {
        char big[16];
        upper(big, sizeof big, EX_MAP_NAMES[n]);
        ExJson *entry = exj_object();
        exj_set(entry, "map", exj_int(n));
        exj_set(entry, "name", exj_str(big));
        for (int half = 0; half < 2; half++) {
            ExJson *recs = exj_array();
            if (!ex_unit_list(recs, start + n * 0x200 + half * 0x100)) {
                exj_free(recs);
                exj_free(entry);
                exj_free(maps);
                return ex_fail(x, "StartUnits: a gap in map %d", n);
            }
            exj_set(entry, half ? "p2_cpu" : "p1_cpu", recs);
        }
        exj_push(maps, entry);
    }
    ExJson *types = exj_array();
    for (int i = 0; i < 4; i++) {
        exj_push(types, exj_int(cpu[i]));
    }
    ExJson *top = exj_object();
    exj_set(top, "cpu_start_types", types);
    exj_set(top, "maps", maps);
    return ex_write_json(x, "data/start_units.json", top);
}

static bool sound_json(HzEx *x)
{
    const u8 *p = ex_at(x, EXR_SfxPriority, 256);
    if (p == NULL) {
        return false;
    }
    int n = 256;
    while (n > 0 && p[n - 1] == 0xFF) {
        n--;
    }
    ExJson *prio = exj_array();
    for (int i = 0; i < n; i++) {
        exj_push(prio, exj_int(p[i]));
    }
    ExJson *top = exj_object();
    exj_set(top, "sfx_priority", prio);
    return ex_write_json(x, "data/sound.json", top);
}

static bool order_menu_json(HzEx *x)
{
    const u8 *info = ex_at(x, EXR_OrderInfo, 16 * 16);
    const u8 *avail = ex_at(x, EXR_UnitAvailByMap, 32);
    if (info == NULL || avail == NULL) {
        return false;
    }
    ExJson *orders = exj_array();
    for (int n = 0; n < 16; n++) {
        const u8 *r = info + n * 16;
        if (!zero(r + 12, 4)) {
            exj_free(orders);
            return ex_fail(x, "OrderInfo %d: bytes 12-15 not 0", n);
        }
        ExJson *o = exj_object();
        exj_set(o, "order", exj_int(n));
        exj_set(o, "name", font_string(r, 10));
        exj_set(o, "fee", exj_int(rd16(r, 10)));
        exj_push(orders, o);
    }
    if (!zero(avail + 16, 16)) {
        exj_free(orders);
        return ex_fail(x, "UnitAvailByMap: words past map 7 not 0");
    }
    ExJson *buyable = exj_array();
    for (int n = 0; n < 8; n++) {
        char big[16];
        upper(big, sizeof big, EX_MAP_NAMES[n]);
        u16 bits = rd16(avail, n * 2);
        ExJson *types = exj_array();
        for (int t = 0; t < 16; t++) {
            if ((bits >> t) & 1) {
                exj_push(types, exj_int(t));
            }
        }
        ExJson *e = exj_object();
        exj_set(e, "map", exj_int(n));
        exj_set(e, "name", exj_str(big));
        exj_set(e, "types", types);
        exj_push(buyable, e);
    }
    ExJson *top = exj_object();
    exj_set(top, "orders", orders);
    exj_set(top, "buyable", buyable);
    return ex_write_json(x, "data/order_menu.json", top);
}

/* extract_data. */
bool ex_data(HzEx *x)
{
    return units_json(x) && start_units_json(x) && sound_json(x)
        && order_menu_json(x);
}
