/* (Session 78) hz_extract.py's extract_sound in C (ex.h): SoundData as
 * text under sound/, the songs named by their titles (hzsongtext.py's
 * extract: sounds.txt, songs/NN_title.txt, effects.txt, with their
 * `# heard:` comments), then each song as a tracker beside its text
 * (hztracker.py's export: songs/NN_title.trk). The Python holds the text
 * to building back into the cartridge's bytes, and each tracker to
 * playing as its text; here the files are written, then read back by the
 * game's own reader (src/audio/songtext.c) and held to the same: the
 * text alone must build the cartridge's SoundData, and each song as its
 * tracker builds must play as its text. */
#include "ex_sound.h"

#include "../audio/songtext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLES 18               /* MusicTitles' songs */

/* extract_sound's title: music_titles' two lines joined, runs of spaces
 * made one (' '.join(' '.join(t).split())). */
static bool title_of(HzEx *x, int n, char out[256])
{
    char lines[2][100];
    if (!ex_music_title(x, n, lines)) {
        return false;
    }
    char joined[256];
    snprintf(joined, sizeof joined, "%s %s", lines[0], lines[1]);
    size_t k = 0;
    for (const char *p = joined; *p != '\0' && k + 1 < 256; p++) {
        if (*p == ' ' && (k == 0 || out[k - 1] == ' ')) {
            continue;
        }
        out[k++] = *p;
    }
    while (k > 0 && out[k - 1] == ' ') {
        k--;
    }
    out[k] = '\0';
    return true;
}

/* slug: the title in lower case, each run of other than a-z and 0-9 an
 * underscore, none at the ends. */
static void slug(const char *title, char *out, size_t n)
{
    size_t k = 0;
    bool gap = false;
    for (const char *p = title; *p != '\0' && k + 2 < n; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (gap && k > 0) {
                out[k++] = '_';
            }
            out[k++] = c;
            gap = false;
        } else {
            gap = true;
        }
    }
    out[k] = '\0';
}

/* The folder x->root/sound built by the game's reader, the trackers read
 * in their texts' place or not; false (a failure) if it refuses them (it
 * says why on stderr). */
static bool build(HzEx *x, bool trackers, u8 *image, u8 **windows,
                  int *count)
{
    char dir[EX_PATH + 8];
    snprintf(dir, sizeof dir, "%s/sound", x->root);
    u16 effects_at = 0;
    songtext_use_trackers(trackers);
    bool ok = songtext_build(dir, image, &effects_at, windows, count);
    songtext_use_trackers(true);
    if (!ok) {
        return ex_fail(x, "%s: the game's reader refuses the %s (it says "
                       "why above)", dir, trackers ? "trackers" : "text");
    }
    return true;
}

/* extract's check: the text alone builds back into the same bytes, in one
 * window. */
static bool check_text(HzEx *x, const u8 *data)
{
    u8 *image = malloc(SND_SIZE);
    u8 *windows = NULL;
    int count = 0;
    if (image == NULL) {
        return ex_fail(x, "out of memory");
    }
    bool ok = build(x, false, image, &windows, &count);
    if (ok && windows != NULL) {
        ok = ex_fail(x, "SoundData: the text builds back a window per song");
    }
    for (u32 k = 0; ok && k < SND_SIZE; k++) {
        if (image[k] != data[k]) {
            ok = ex_fail(x, "SoundData: the text builds back different from "
                         "$%04X", (unsigned)(SND_BASE + k));
        }
    }
    free(windows);
    free(image);
    return ok;
}

/* export's check, on what the game builds from the trackers: each song
 * read back from its window (or the one window, while they fit) plays as
 * its text. */
static bool check_trackers(HzEx *x, SndModel *m, char (*file)[128])
{
    u8 *image = malloc(SND_SIZE);
    u8 *windows = NULL;
    int count = 0;
    if (image == NULL) {
        return ex_fail(x, "out of memory");
    }
    bool ok = build(x, true, image, &windows, &count);
    if (ok && windows != NULL && count != m->nsongs) {
        ok = ex_fail(x, "the trackers build %d windows for %d songs", count,
                     m->nsongs);
    }
    for (int n = 0; ok && n < m->nsongs; n++) {
        const u8 *w = (windows != NULL) ? windows + (size_t)n * SND_SIZE
                                        : image;
        u32 s = (windows != NULL)
              ? SND_BASE + 2 * (u32)m->song_table
              : SND_BASE + (u32)((image[2 * n] << 8) | image[2 * n + 1]);
        char what[160];
        size_t len = strlen(file[n]);
        snprintf(what, sizeof what, "sound/%.*s.trk", (int)(len - 4),
                 file[n]);
        SndSong tracked;
        u32 size = 0;
        ok = ex_snd_read_song(x, what, w, s, true, &tracked, &size)
             && ex_snd_tracker_check(x, &m->songs[n], &tracked, what);
        ex_snd_song_free(&tracked);
    }
    free(windows);
    free(image);
    return ok;
}

/* extract_sound: the text, checked, then the trackers, checked. */
bool ex_sound(HzEx *x)
{
    const u8 *data = ex_at(x, EXR_SoundData, SND_SIZE);
    if (data == NULL) {
        return false;
    }
    char titles[TITLES][256];
    for (int n = 0; n < TITLES; n++) {
        if (!title_of(x, n, titles[n])) {
            return false;
        }
    }
    SndModel m;
    if (!ex_snd_read(x, data, &m)) {
        ex_snd_free(&m);
        return false;
    }
    char (*file)[128] = calloc((size_t)m.nsongs + 1, sizeof *file);
    if (file == NULL) {
        ex_snd_free(&m);
        return ex_fail(x, "out of memory");
    }
    for (int n = 0; n < m.nsongs; n++) {
        char s[128] = "song";
        if (n < TITLES && titles[n][0] != '\0') {
            slug(titles[n], s, sizeof s);
        }
        snprintf(file[n], sizeof file[n], "songs/%02d_%s.txt", n, s);
    }

    /* write_text's files */
    ExBuf b;
    exb_init(&b);
    ex_snd_sounds_text(&b, &m, file);
    bool ok = ex_write_buf(x, "sound/sounds.txt", &b);
    char rel[160];
    for (int n = 0; ok && n < m.nsongs; n++) {
        b.len = 0;
        ex_snd_song_text(&b, n, &m.songs[n], n < TITLES ? titles[n] : NULL);
        snprintf(rel, sizeof rel, "sound/%s", file[n]);
        ok = ex_write_buf(x, rel, &b);
    }
    if (ok) {
        b.len = 0;
        ex_snd_effects_text(&b, &m.fx);
        ok = ex_write_buf(x, "sound/effects.txt", &b);
    }
    ok = ok && check_text(x, data);

    /* export's trackers */
    for (int n = 0; ok && n < m.nsongs; n++) {
        b.len = 0;
        ok = ex_snd_tracker_text(x, &b, n, &m.songs[n],
                                 n < TITLES ? titles[n] : NULL);
        size_t len = strlen(file[n]);
        snprintf(rel, sizeof rel, "sound/%.*s.trk", (int)(len - 4), file[n]);
        ok = ok && ex_write_buf(x, rel, &b);
    }
    ok = ok && check_trackers(x, &m, file);

    exb_free(&b);
    free(file);
    ex_snd_free(&m);
    return ok;
}
