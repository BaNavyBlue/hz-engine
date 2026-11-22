#include "saves.h"

#include "../core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define make_dir(path) mkdir(path, 0755)
#endif

#define SAVES_FILE "progress.json"

static const char *const DIFFICULTY_KEY[4] = { "A", "B", "C", "D" };

const char *saves_path(void)
{
    static char path[512];
    const char *dir = getenv("HZ_SAVES");
    snprintf(path, sizeof path, "%s/%s",
             (dir != NULL && dir[0] != '\0') ? dir : "saves", SAVES_FILE);
    return path;
}

/* One entry of "saves"; false if it isn't one (the caller says so). */
static bool read_slot(const JsonValue *e, HzSaves *s)
{
    long n = json_int(e, "slot", 0);
    const char *human = json_str(e, "human", "");
    const JsonValue *won = json_get(e, "won");
    if (n < 1 || n > HZ_SAVE_SLOTS || won == NULL
        || (strcmp(human, "P1") != 0 && strcmp(human, "P2") != 0)) {
        return false;
    }
    HzSaveSlot *slot = &s->slot[n - 1];
    memset(slot, 0, sizeof *slot);
    for (int d = 0; d < 4; d++) {
        const JsonValue *maps = json_get(won, DIFFICULTY_KEY[d]);
        for (int i = 0; i < json_length(maps); i++) {
            const JsonValue *m = json_at(maps, i);
            if (m == NULL || m->type != JSON_NUMBER || m->number < 0
                || m->number > 7) {
                return false;
            }
            slot->record[d] |= (u8)(1u << (int)m->number);
        }
    }
    slot->human = (u8)(human[1] - '0');
    slot->time = json_int(e, "time", 0);
    slot->used = true;
    return true;
}

void saves_read(HzSaves *s)
{
    memset(s, 0, sizeof *s);
    const char *path = saves_path();
    FILE *probe = fopen(path, "rb");
    if (probe == NULL) {
        return;                         /* no saves yet */
    }
    fclose(probe);
    JsonValue *root = json_parse_file(path);
    const JsonValue *list = json_get(root, "saves");
    if (root == NULL || list == NULL) {
        fprintf(stderr, "saves: %s isn't a saves file; no saves read\n",
                path);
        json_free(root);
        return;
    }
    for (int i = 0; i < json_length(list); i++) {
        if (!read_slot(json_at(list, i), s)) {
            fprintf(stderr, "saves: %s: entry %d skipped\n", path, i);
        }
    }
    json_free(root);
}

bool saves_write(const HzSaves *s)
{
    const char *path = saves_path();
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        /* No saves/ yet: make the file's folder and try once more. */
        char dir[512];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash != NULL) {
            *slash = '\0';
            make_dir(dir);
            f = fopen(path, "wb");
        }
    }
    if (f == NULL) {
        fprintf(stderr, "saves: can't write %s\n", path);
        return false;
    }
    fprintf(f, "{\n  \"saves\": [");
    bool first = true;
    for (int n = 0; n < HZ_SAVE_SLOTS; n++) {
        const HzSaveSlot *slot = &s->slot[n];
        if (!slot->used) {
            continue;
        }
        fprintf(f, "%s\n    { \"slot\": %d, \"human\": \"P%d\", \"time\": %ld,"
                   "\n      \"won\": {", first ? "" : ",", n + 1, slot->human,
                slot->time);
        for (int d = 0; d < 4; d++) {
            fprintf(f, "%s \"%s\": [", d ? "," : "", DIFFICULTY_KEY[d]);
            bool any = false;
            for (int m = 0; m < 8; m++) {
                if (slot->record[d] & (1u << m)) {
                    fprintf(f, "%s%d", any ? ", " : "", m);
                    any = true;
                }
            }
            fputc(']', f);
        }
        fprintf(f, " } }");
        first = false;
    }
    fprintf(f, "\n  ]\n}\n");
    return fclose(f) == 0;
}

int saves_store(int slot, const u8 record[4], int human)
{
    HzSaves s;
    saves_read(&s);
    if (slot < 0 || slot >= HZ_SAVE_SLOTS) {
        slot = -1;
        for (int n = 0; n < HZ_SAVE_SLOTS && slot < 0; n++) {
            if (!s.slot[n].used) {
                slot = n;
            }
        }
        for (int n = 0; n < HZ_SAVE_SLOTS && slot < 0; n++) {
            int oldest = 0;
            for (int k = 1; k < HZ_SAVE_SLOTS; k++) {
                if (s.slot[k].time < s.slot[oldest].time) {
                    oldest = k;
                }
            }
            slot = oldest;
        }
    }
    HzSaveSlot *e = &s.slot[slot];
    e->used = true;
    memcpy(e->record, record, 4);
    e->human = (u8)(human == 2 ? 2 : 1);
    e->time = (long)time(NULL);
    return saves_write(&s) ? slot : -1;
}

static bool map_complete(const u8 record[4], int map)
{
    for (int d = 0; d < 4; d++) {
        if (!(record[d] & (1u << map))) {
            return false;
        }
    }
    return true;
}

int saves_wins(const u8 record[4])
{
    int n = 0;
    for (int d = 0; d < 4; d++) {
        for (int m = 0; m < 8; m++) {
            n += (record[d] >> m) & 1;
        }
    }
    return n;
}

int saves_next_map(const u8 record[4])
{
    for (int m = 0; m < 8; m++) {
        if (!map_complete(record, m)) {
            return m;
        }
    }
    return 8;
}
