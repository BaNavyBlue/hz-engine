/* (Session 59) replay.h: a game recorded, written, read and fed back. */

#include "replay.h"

#include "nightmare.h"
#include "statehash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *const MAP_NAMES[8] = {
    "ABGRUND", "VULKAN", "LOCH", "STRAND", "STADT", "EISFREI", "WALDUNG",
    "OASE"
};

u8 replay_pad_byte(const HzPad *p)
{
    return (u8)((p->up ? HZ_PAD_UP : 0) | (p->down ? HZ_PAD_DOWN : 0)
                | (p->left ? HZ_PAD_LEFT : 0) | (p->right ? HZ_PAD_RIGHT : 0)
                | (p->a ? HZ_PAD_A : 0) | (p->b ? HZ_PAD_B : 0)
                | (p->c ? HZ_PAD_C : 0) | (p->start ? HZ_PAD_START : 0));
}

void replay_pad_set(HzPad *p, u8 b)
{
    p->up = (b & HZ_PAD_UP) != 0;
    p->down = (b & HZ_PAD_DOWN) != 0;
    p->left = (b & HZ_PAD_LEFT) != 0;
    p->right = (b & HZ_PAD_RIGHT) != 0;
    p->a = (b & HZ_PAD_A) != 0;
    p->b = (b & HZ_PAD_B) != 0;
    p->c = (b & HZ_PAD_C) != 0;
    p->start = (b & HZ_PAD_START) != 0;
}

/* -- recording ----------------------------------------------------------- */

void replay_begin(HzReplay *r, const HzGame *g, u64 game_id, int code,
                  const char *kind, int side, const char *rival)
{
    replay_free(r);
    snprintf(r->game, sizeof r->game, "%016llX", (unsigned long long)game_id);
    r->code = code;
    snprintf(r->kind, sizeof r->kind, "%s", kind);
    r->side = side;
    snprintf(r->rival, sizeof r->rival, "%s", rival != NULL ? rival : "");
    time_t now = time(NULL);
    const struct tm *tm = gmtime(&now);     /* UTC: no time zone in it */
    if (tm != NULL) {
        strftime(r->when, sizeof r->when, "%Y-%m-%d %H:%M:%S UTC", tm);
    }
    memcpy(r->options, g->options, sizeof r->options);
    r->fixes = g->fixes;
    memcpy(r->brain, g->cpu_brain, sizeof r->brain);
    r->own_view = g->own_view;
    r->seed = g->rng_seed_saved;
    r->cpu_boost = g->cpu_boost;
    memcpy(r->win_record, g->win_record, sizeof r->win_record);
    r->pads0[0] = replay_pad_byte(&g->pad_in[0]);
    r->pads0[1] = replay_pad_byte(&g->pad_in[1]);
    r->winner = -1;
    snprintf(r->why, sizeof r->why, "on");
}

static bool grow(void **p, u32 *cap, u32 need, size_t size)
{
    if (need <= *cap) {
        return true;
    }
    u32 n = (*cap != 0) ? *cap * 2 : 4096;
    while (n < need) {
        n *= 2;
    }
    void *q = realloc(*p, (size_t)n * size);
    if (q == NULL) {
        return false;
    }
    *p = q;
    *cap = n;
    return true;
}

bool replay_record(HzReplay *r, const HzGame *g)
{
    if (r->frames % REPLAY_HASH_EVERY == 0) {
        if (!grow((void **)&r->hashes, &r->hashes_cap, r->hash_count + 1,
                  sizeof *r->hashes)) {
            return false;
        }
        r->hashes[r->hash_count++] = state_hash_game(g);
    }
    if (!grow((void **)&r->pads, &r->pads_cap, r->frames + 1,
              sizeof *r->pads)) {
        return false;
    }
    r->pads[r->frames++] = (u16)(replay_pad_byte(&g->pad_in[0])
                                 | replay_pad_byte(&g->pad_in[1]) << 8);
    return true;
}

void replay_fixes(HzReplay *r, u32 fixes)
{
    if (grow((void **)&r->fix, &r->fix_cap, r->fix_count + 1,
             sizeof *r->fix)) {
        r->fix[r->fix_count].frame = r->frames;
        r->fix[r->fix_count].fixes = fixes;
        r->fix_count++;
    }
}

void replay_end(HzReplay *r, int winner, const char *why)
{
    r->end_frame = r->frames;
    r->winner = winner;
    snprintf(r->why, sizeof r->why, "%s", why);
}

bool replay_save(const HzReplay *r, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    fprintf(f, "HZREPLAY 1\n");
    fprintf(f, "game %s\ncode %d\nkind %s\nside %d\nwhen %s\n", r->game,
            r->code, r->kind, r->side, r->when);
    if (r->rival[0] != '\0') {
        fprintf(f, "rival %s\n", r->rival);
    }
    fprintf(f, "options %02X %02X %02X %02X\npatches %X\nbrains %d %d\n"
               "own_view %d\nseed %08X\ncpu_boost %04X\n"
               "win_record %02X %02X %02X %02X\npads0 %02X %02X\n",
            r->options[0], r->options[1], r->options[2], r->options[3],
            (unsigned)r->fixes, r->brain[0], r->brain[1], r->own_view,
            (unsigned)r->seed, r->cpu_boost, r->win_record[0],
            r->win_record[1], r->win_record[2], r->win_record[3],
            r->pads0[0], r->pads0[1]);
    fprintf(f, "pads %u\n", (unsigned)r->frames);
    for (u32 i = 0; i < r->frames;) {
        u32 run = 1;
        while (i + run < r->frames && r->pads[i + run] == r->pads[i]) {
            run++;
        }
        fprintf(f, "%X %02X %02X\n", (unsigned)run, r->pads[i] & 0xFF,
                r->pads[i] >> 8);
        i += run;
    }
    for (u32 i = 0; i < r->fix_count; i++) {
        fprintf(f, "patches_at %u %X\n", (unsigned)r->fix[i].frame,
                (unsigned)r->fix[i].fixes);
    }
    fprintf(f, "hashes %u\n", (unsigned)r->hash_count);
    for (u32 i = 0; i < r->hash_count; i++) {
        fprintf(f, "%016llX\n", (unsigned long long)r->hashes[i]);
    }
    fprintf(f, "end %u %d %s\n", (unsigned)r->end_frame, r->winner, r->why);
    return fclose(f) == 0;
}

/* -- reading ------------------------------------------------------------- */

static bool fail(char *err, size_t len, int line, const char *what)
{
    snprintf(err, len, "line %d: %s", line, what);
    return false;
}

bool replay_load(HzReplay *r, const char *path, char *err, size_t err_len)
{
    memset(r, 0, sizeof *r);
    r->winner = -1;
    r->side = -1;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        snprintf(err, err_len, "can't open it");
        return false;
    }
    char line[256];
    int n = 0;
    u32 pads_left = 0, hashes_left = 0;
    bool started = false, ended = false;
    while (fgets(line, sizeof line, f) != NULL) {
        n++;
        line[strcspn(line, "\r\n")] = '\0';
        unsigned a, b, c, d;
        unsigned long long h;
        int at = 0;
        if (!started) {
            if (strcmp(line, "HZREPLAY 1") != 0) {
                fclose(f);
                return fail(err, err_len, n, "not a replay (HZREPLAY 1)");
            }
            started = true;
        } else if (pads_left > 0) {
            if (sscanf(line, "%x %x %x", &a, &b, &c) != 3 || a == 0
                || a > pads_left || b > 0xFF || c > 0xFF) {
                fclose(f);
                return fail(err, err_len, n, "a pads line: COUNT P1 P2");
            }
            for (u32 k = 0; k < a; k++) {
                r->pads[r->frames++] = (u16)(b | c << 8);
            }
            pads_left -= a;
        } else if (hashes_left > 0) {
            if (sscanf(line, "%llx", &h) != 1) {
                fclose(f);
                return fail(err, err_len, n, "a hash line");
            }
            r->hashes[r->hash_count++] = h;
            hashes_left--;
        } else if (strncmp(line, "game ", 5) == 0) {
            snprintf(r->game, sizeof r->game, "%.16s", line + 5);
        } else if (sscanf(line, "code %d", &r->code) == 1) {
        } else if (strncmp(line, "kind ", 5) == 0) {
            snprintf(r->kind, sizeof r->kind, "%.7s", line + 5);
        } else if (sscanf(line, "side %d", &r->side) == 1) {
        } else if (strncmp(line, "when ", 5) == 0) {
            snprintf(r->when, sizeof r->when, "%.23s", line + 5);
        } else if (strncmp(line, "rival ", 6) == 0) {
            snprintf(r->rival, sizeof r->rival, "%.15s", line + 6);
        } else if (sscanf(line, "options %x %x %x %x", &a, &b, &c, &d) == 4) {
            r->options[0] = (u8)a;
            r->options[1] = (u8)b;
            r->options[2] = (u8)c;
            r->options[3] = (u8)d;
        } else if (sscanf(line, "patches_at %u %x", &a, &b) == 2) {
            if (!grow((void **)&r->fix, &r->fix_cap, r->fix_count + 1,
                      sizeof *r->fix)) {
                fclose(f);
                return fail(err, err_len, n, "no memory");
            }
            r->fix[r->fix_count].frame = a;
            r->fix[r->fix_count].fixes = b;
            r->fix_count++;
        } else if (sscanf(line, "patches %x", &a) == 1) {
            r->fixes = a;
        } else if (sscanf(line, "brains %u %u", &a, &b) == 2) {
            r->brain[0] = (u8)a;
            r->brain[1] = (u8)b;
        } else if (sscanf(line, "own_view %u", &a) == 1) {
            r->own_view = (u8)a;
        } else if (sscanf(line, "seed %x", &a) == 1) {
            r->seed = a;
        } else if (sscanf(line, "cpu_boost %x", &a) == 1) {
            r->cpu_boost = (u16)a;
        } else if (sscanf(line, "win_record %x %x %x %x", &a, &b, &c, &d)
                   == 4) {
            r->win_record[0] = (u8)a;
            r->win_record[1] = (u8)b;
            r->win_record[2] = (u8)c;
            r->win_record[3] = (u8)d;
        } else if (sscanf(line, "pads0 %x %x", &a, &b) == 2) {
            r->pads0[0] = (u8)a;
            r->pads0[1] = (u8)b;
        } else if (sscanf(line, "pads %u", &a) == 1) {
            if (a == 0 || a > 0x1000000u
                || !grow((void **)&r->pads, &r->pads_cap, a,
                         sizeof *r->pads)) {
                fclose(f);
                return fail(err, err_len, n, "pads: too many, or none");
            }
            pads_left = a;
        } else if (sscanf(line, "hashes %u", &a) == 1) {
            if (a > 0x100000u
                || (a > 0 && !grow((void **)&r->hashes, &r->hashes_cap, a,
                                   sizeof *r->hashes))) {
                fclose(f);
                return fail(err, err_len, n, "hashes: too many");
            }
            hashes_left = a;
        } else if (sscanf(line, "end %u %d %n", &a, &r->winner, &at) >= 2
                   && at > 0) {
            r->end_frame = a;
            snprintf(r->why, sizeof r->why, "%.23s", line + at);
            ended = true;
        } else if (line[0] != '\0') {
            fclose(f);
            return fail(err, err_len, n, "not a line a replay has");
        }
    }
    fclose(f);
    if (!started || pads_left > 0 || hashes_left > 0 || !ended) {
        return fail(err, err_len, n, "cut short");
    }
    return true;
}

void replay_free(HzReplay *r)
{
    free(r->pads);
    free(r->hashes);
    free(r->fix);
    memset(r, 0, sizeof *r);
    r->winner = -1;
    r->side = -1;
}

void replay_file_name(const HzReplay *r, char *out, size_t len)
{
    char stamp[20] = "";
    int y, mo, d, h, mi, s;
    if (sscanf(r->when, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s)
        == 6) {
        snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", y, mo, d,
                 h, mi, s);
    }
    const char *who;
    u8 mode = r->options[0] & 3;     /* b0 P2 the CPU, b1 P1 the CPU */
    if (strcmp(r->kind, "network") == 0) {
        who = (r->side == 1) ? "NET-P2" : "NET-P1";
    } else if (strcmp(r->kind, "netai") == 0) {
        who = (r->side == 1) ? "NETAI-P2" : "NETAI-P1";     /* (s. 67) */
    } else if (mode == 0) {
        who = "2PLAYERS";
    } else if (mode == 3) {
        who = "CPUS";
    } else {
        int cpu = (mode & 1) ? 1 : 0;
        who = (r->brain[cpu] == HZ_BRAIN_NIGHTMARE) ? "NIGHTMARE"
                                                    : "ORIGINAL";
    }
    snprintf(out, len, "%s-%s-%s-%c.hzr", stamp, MAP_NAMES[r->options[3] & 7],
             who, 'A' + (r->options[1] & 3));
}

/* -- playing ------------------------------------------------------------- */

void replay_apply_start(HzGame *g, const HzReplay *r)
{
    g->hw_version = 0xA0;               /* a cold boot's, as main's */
    g->options[0] = 5;
    g->options[2] = 0xFF;
    snd_init(g);
    memcpy(g->options, r->options, sizeof g->options);
    g->fixes = r->fixes;
    memcpy(g->cpu_brain, r->brain, sizeof g->cpu_brain);
    g->own_view = r->own_view;
    g->rng_seed_saved = r->seed;
    g->cpu_boost = r->cpu_boost;
    memcpy(g->win_record, r->win_record, sizeof g->win_record);
    replay_pad_set(&g->pad_in[0], r->pads0[0]);
    replay_pad_set(&g->pad_in[1], r->pads0[1]);
}

bool replay_feed(const HzReplay *r, HzGame *g, u32 frame)
{
    if (frame >= r->frames) {
        return false;
    }
    for (u32 i = 0; i < r->fix_count; i++) {
        if (r->fix[i].frame == frame) {
            g->fixes = r->fix[i].fixes;
        }
    }
    replay_pad_set(&g->pad_in[0], (u8)r->pads[frame]);
    replay_pad_set(&g->pad_in[1], (u8)(r->pads[frame] >> 8));
    return true;
}
