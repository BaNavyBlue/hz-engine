/* hz_roundtrip: every asset the port loads, read by the port's own loader
 * (hz_assets_load, src/game/assets.c) and packed back into the layout the
 * ROM has it in, compared with the ROM byte for byte; and every table in
 * src/game/tables.c against its ROM address. The map data is compared
 * with what the LZW entries unpack to (hz_lzw.c), as Game_LoadMap sees it.
 *
 *     hz_roundtrip ROM ASSETS
 *
 * One line per region (an LZW table's address with [the entry]); exit 1
 * if any differs (the first difference is shown). The song text is built
 * alone for its bytes (the trackers beside it don't build the ROM's), and
 * the songs as the game reads them, trackers and all, are played through
 * the driver against the text's: the same chip writes. `make roundtrip
 * ROM=...` runs it on assets/; the PS2 port's `make roundtrip` does the
 * same job for its assets. */

#include "../src/audio/hz_snd.h"
#include "../src/audio/songtext.h"
#include "../src/core/hz_lzw.h"
#include "../src/core/hz_rle.h"
#include "../src/core/hz_rom.h"
#include "../src/game/assets.h"
#include "../src/game/tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where each block is in the ROM, by the disassembly's labels. */
#define ROM_UNIT_DIR_TABLE       0x00244Au  /* UnitDirTable */
#define ROM_EXPLOSION_FRAMES     0x00258Au  /* ExplosionFrames */
#define ROM_HUD_FRAME            0x006C94u  /* HUDFrameTiles */
#define ROM_CPU_START_TYPE_TABLE 0x009692u  /* CPUStartTypeTable */
#define ROM_SFX_PRIORITY         0x009931u  /* SfxPriority */
#define ROM_SOUND_DATA           0x078000u  /* SoundData */
#define ROM_MECH_FRAMES          0x010000u  /* MechFrames */
#define ROM_SPRITE_TILES         0x018000u  /* SpriteTiles, RadarBlank */
#define ROM_HUD_TILES            0x01E000u  /* HUDTiles */
#define ROM_FONT_TILES           0x020000u  /* FontTiles */
#define ROM_COLOUR_CYCLES        0x0058E6u  /* ColourCycles */
#define ROM_GAME_PALETTES        0x020A00u  /* GamePalettes */
#define ROM_START_UNITS          0x052000u  /* StartUnits */
#define ROM_DEMO_TABLE           0x000598u  /* DemoTable */
#define ROM_DEMO_DATA            0x04E000u  /* DemoData */
#define ROM_UNIT_STATS           0x05DC00u  /* UnitStats */
#define ROM_UNIT_STATS_KIDOU     0x05DDC0u  /* UnitStats_KIDOU */
#define ROM_MAP_PALETTES         0x073F00u  /* MapPalettes */
#define ROM_HUD_ICONS            0x01D800u  /* HUDIcons */
#define ROM_UNIT_AVAIL_BY_MAP    0x05DE00u  /* UnitAvailByMap */
#define ROM_ORDER_INFO           0x05DE20u  /* OrderInfo */
#define ROM_RADAR_IMAGES         0x074000u  /* RadarImages */
#define ROM_TANK_PHOTO_PALETTE   0x068400u  /* TankPhotoPalette */
#define ROM_MENU_PALETTE         0x073E00u  /* MenuPalette */
#define ROM_SEGA_LOGO_PALETTE   0x05348Eu  /* SegaLogoPalette */
#define ROM_SEGA_LOGO_CYCLE     0x0534AEu  /* SegaLogoCycle */
#define ROM_COPYRIGHT_CYCLE    0x0534EEu  /* CopyrightCycle */
#define ROM_COPYRIGHT_PALETTE  0x05351Cu  /* CopyrightPalette */
#define ROM_MUSIC_TEST_REPEAT    0x055862u  /* MusicTestRepeat */
#define ROM_MUSIC_TITLES         0x0558CAu  /* MusicTitles */
#define ROM_MAP_NAMES            0x055EE0u  /* MapNames */
#define ROM_M_SFX_COUNT          0x057422u  /* M_SfxCount */
#define ROM_M_SFX_PRIORITY       0x057423u  /* M_SfxPriority */
#define ROM_WINNER_PALETTE       0x05439Au  /* WinnerPalette */
#define ROM_WINNER_PHOTO_PALETTE 0x0543FAu  /* WinnerPhotoPalette */
#define ROM_ENDING_TEXT_PALETTE  0x053688u  /* EndingTextPalette */
#define ROM_ENDING_TEXT1         0x053A16u  /* EndingText1 */
#define ROM_ENDING_TEXT2         0x053D86u  /* EndingText2 */
#define ROM_STAFF_TEXT           0x0540F6u  /* StaffText */
#define ROM_STAFF_JAPAN          0x05427Bu  /* StaffCopyrightJapan */
#define ROM_STAFF_OVERSEAS       0x05428Cu  /* StaffCopyrightOverseas */
#define ROM_FOREST_PALETTE       0x05C3E0u  /* ForestPalette */
#define ROM_ENDING_PALETTE_STILL 0x061000u  /* EndingPaletteStillLife */
#define ROM_ENDING_PALETTE_GRAVE 0x061020u  /* EndingPaletteGrave */

static int g_regions;
static int g_bad;
static long g_bytes;

static void put16(u8 *p, u16 v)
{
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

static void check(const char *what, u32 addr, const u8 *packed,
                  const u8 *expected, size_t n)
{
    g_regions++;
    g_bytes += (long)n;
    for (size_t i = 0; i < n; i++) {
        if (packed[i] != expected[i]) {
            size_t diffs = 0;
            for (size_t k = i; k < n; k++) {
                diffs += packed[k] != expected[k];
            }
            printf("%-34s $%06X %6zu bytes  DIFFERS at +$%zX (assets $%02X, "
                   "ROM $%02X), %zu bytes in all\n", what, (unsigned)addr, n,
                   i, packed[i], expected[i], diffs);
            g_bad++;
            return;
        }
    }
    printf("%-34s $%06X %6zu bytes  ok\n", what, (unsigned)addr, n);
}

static void check_words(const char *what, u32 addr, const u16 *words,
                        size_t count, const HzRom *rom)
{
    u8 *buf = malloc(count * 2);
    for (size_t i = 0; i < count; i++) {
        put16(buf + i * 2, words[i]);
    }
    check(what, addr, buf, rom->data + addr, count * 2);
    free(buf);
}

static void pack_stats(u8 *p, const HzUnitStats *st)
{
    memcpy(p, st->name, 8);
    put16(p + 0x08, st->price);
    put16(p + 0x0A, st->hp);
    p[0x0C] = st->size;
    p[0x0D] = st->ammo;
    put16(p + 0x0E, st->fuel);
    p[0x10] = st->sprite_size;
    p[0x11] = st->speed;
    p[0x12] = st->overlay;
    p[0x13] = st->fuel_use;
    p[0x14] = st->shot_speed;
    p[0x15] = st->shot_life;
    p[0x16] = st->fire_delay;
    p[0x17] = st->power;
    p[0x18] = st->sight;
    p[0x19] = st->build_time;
    p[0x1A] = st->missiles;
    p[0x1B] = st->missile_speed;
    p[0x1C] = st->missile_life;
    p[0x1D] = st->missile_delay;
    put16(p + 0x1E, st->orders);
}

/* RLE entry n of a table, unpacked to `size` bytes; NULL if it doesn't. */
static u8 *unpack_rle(const HzRom *rom, u32 table, int n, u32 size)
{
    u32 entry;
    u32 out;
    u32 used;
    u8 *buf = malloc(size);
    if (buf == NULL
        || !hz_rle_entry(rom->data, rom->size, table, (u32)n, &entry)
        || hz_rle_unpack(rom->data + entry, rom->size - entry, buf, size,
                         &out, &used) != HZ_RLE_OK
        || out != size) {
        free(buf);
        return NULL;
    }
    return buf;
}

static void check_rle(const char *what, const HzRom *rom, u32 table, int n,
                      const u8 *asset, u32 size)
{
    u8 *ref = unpack_rle(rom, table, n, size);
    if (ref == NULL) {
        printf("%-34s $%06X [%d] doesn't unpack\n", what, (unsigned)table, n);
        g_regions++;
        g_bad++;
        return;
    }
    check(what, table, asset, ref, size);
    free(ref);
}

/* The title's and the options screen's data. */
static void check_menu(const HzRom *rom, const HzAssets *a)
{
    const HzMenuAssets *m = &a->menu;
    check_rle("title/tank_photo.png", rom, HZ_RLE_TANK_PHOTOS, 0,
              m->tank_photo, sizeof m->tank_photo);
    check_words("title/tank_photo.png palette", ROM_TANK_PHOTO_PALETTE,
                m->tank_photo_palette, 16, rom);
    check_rle("title/title_logo.png (+ palette)", rom, HZ_RLE_MENU_GFX, 2,
              m->title_logo, sizeof m->title_logo);
    check_rle("title/sega_logo, copyright", rom, HZ_RLE_MENU_GFX, 1,
              m->title_gfx, sizeof m->title_gfx);
    check_rle("title/menu_font.png", rom, HZ_RLE_MENU_GFX, 0, m->menu_font,
              sizeof m->menu_font);
    check_words("palettes/menu.png", ROM_MENU_PALETTE, m->menu_palette, 64,
                rom);
    check_words("palettes/title.png line 0", ROM_SEGA_LOGO_PALETTE,
                m->sega_palette, 16, rom);
    check_words("palettes/title.png line 1", ROM_COPYRIGHT_PALETTE,
                m->copyright_palette, 16, rom);
    check_words("palettes/sega_logo_cycle.png", ROM_SEGA_LOGO_CYCLE,
                m->sega_cycle, HZ_SEGA_CYCLE, rom);
    check_words("palettes/copyright_cycle.png", ROM_COPYRIGHT_CYCLE,
                m->copyright_cycle, HZ_COPYRIGHT_CYCLE, rom);
    check_rle("title/winner_font.png", rom, HZ_RLE_WINNER_FONT, 0,
              m->winner_font, sizeof m->winner_font);
    check_rle("title/tank_photo2.png", rom, HZ_RLE_TANK_PHOTOS, 1,
              m->tank_photo2, sizeof m->tank_photo2);
    check_words("title/tank_photo2.png palette", ROM_WINNER_PHOTO_PALETTE,
                m->winner_photo_palette, 16, rom);
    check_words("palettes/winner.png", ROM_WINNER_PALETTE,
                m->winner_palette, 48, rom);

    static u8 buf[HZ_SONGS * 40];
    for (int n = 0; n < HZ_SONGS; n++) {
        for (int k = 0; k < 2; k++) {
            memcpy(buf + n * 40 + k * 20, m->song_titles[n][k],
                   HZ_SONG_TITLE);
            buf[n * 40 + k * 20 + HZ_SONG_TITLE] = 0;
        }
    }
    check("data/menu.json titles", ROM_MUSIC_TITLES, buf,
          rom->data + ROM_MUSIC_TITLES, HZ_SONGS * 40);
    for (int n = 0; n < HZ_SONGS; n++) {
        buf[n] = m->song_repeat[n] ? 0xFF : 0x00;
    }
    check("data/menu.json repeat", ROM_MUSIC_TEST_REPEAT, buf,
          rom->data + ROM_MUSIC_TEST_REPEAT, HZ_SONGS);
    for (int n = 0; n < HZ_MAPS; n++) {
        memcpy(buf + n * 8, m->map_names[n], HZ_MAP_NAME);
        buf[n * 8 + HZ_MAP_NAME] = 0;
    }
    check("data/menu.json map_names", ROM_MAP_NAMES, buf,
          rom->data + ROM_MAP_NAMES, HZ_MAPS * 8);
    check("data/menu.json sfx_count", ROM_M_SFX_COUNT, &m->sfx_count,
          rom->data + ROM_M_SFX_COUNT, 1);
    /* M_SfxPriority, the module's copy of SfxPriority, isn't an asset of
     * its own: the game's (data/sound.json) must match it. */
    check("data/sound.json (M_SfxPriority)", ROM_M_SFX_PRIORITY,
          a->sfx_priority, rom->data + ROM_M_SFX_PRIORITY, m->sfx_count);
}

/* LZW entry n of a table, unpacked; NULL if it doesn't. */
static u8 *unpack(const HzRom *rom, u32 table, int n, u32 size)
{
    u32 src;
    u32 len;
    u32 out;
    u8 *buf = malloc(size);
    if (buf == NULL
        || !hz_pack_entry(rom->data, rom->size, table, (u32)n, &src, &len)
        || hz_unpack(rom->data + src, len, buf, size, &out) != HZ_UNPACK_OK
        || out != size) {
        free(buf);
        return NULL;
    }
    return buf;
}

static void check_map(const HzRom *rom, const HzAssets *a, int n)
{
    const HzMapAssets *m = &a->map[n];
    char what[64];
    u8 *tiles = unpack(rom, HZ_PACK_MAP_TILES, n, 0x4000);
    u8 *layout = unpack(rom, HZ_PACK_MAP_LAYOUT, n, 0x2000);
    u8 *blocks = unpack(rom, HZ_PACK_MAP_BLOCKS, n, 0x4800);
    if (tiles == NULL || layout == NULL || blocks == NULL) {
        printf("map %d: the ROM's LZW entries don't unpack\n", n);
        g_bad++;
    } else {
        static u8 buf[0x4800];
        snprintf(what, sizeof what, "maps/%s tiles.png [%d]",
                 hz_map_folder(n), n);
        check(what, HZ_PACK_MAP_TILES, m->tiles, tiles, 0x4000);
        for (int r = 0; r < 64; r++) {
            for (int c = 0; c < 64; c++) {
                put16(buf + (r * 64 + c) * 2, m->layout[r][c]);
            }
        }
        snprintf(what, sizeof what, "maps/%s layout.tmx [%d]",
                 hz_map_folder(n), n);
        check(what, HZ_PACK_MAP_LAYOUT, buf, layout, 0x2000);
        for (int b = 0; b < HZ_BLOCKS; b++) {
            for (int i = 0; i < 16; i++) {
                put16(buf + b * 32 + i * 2, m->blocks[b][i]);
            }
            put16(buf + 0x4000 + b * 2, m->terrain[b]);
        }
        for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
            for (int i = 0; i < 16; i++) {
                put16(buf + 0x4400 + r * 32 + i * 2, m->anim[r][i]);
            }
        }
        snprintf(what, sizeof what, "maps/%s blocks.txt [%d]",
                 hz_map_folder(n), n);
        check(what, HZ_PACK_MAP_BLOCKS, buf, blocks, 0x4400);
        snprintf(what, sizeof what, "maps/%s anim.txt [%d]",
                 hz_map_folder(n), n);
        check(what, HZ_PACK_MAP_BLOCKS, buf + 0x4400, blocks + 0x4400,
              0x400);
    }
    free(tiles);
    free(layout);
    free(blocks);
    snprintf(what, sizeof what, "maps/%s palette.png", hz_map_folder(n));
    check_words(what, ROM_MAP_PALETTES + (u32)n * 0x20, m->palette, 16, rom);
}

/* The endings' data (ending/, palettes/ending_text.png). */
/* data/demos.json (DemoTable) and each map's demo.json (DemoData: the
 * units, then each pad's runs as 2-byte records ending in a 0 count, the
 * rest of the $380 bytes 0). */
static void check_demos(const HzRom *rom, const HzAssets *a)
{
    u8 table[HZ_DEMOS * 8];
    for (int k = 0; k < HZ_DEMOS; k++) {
        const HzDemo *d = &a->demos[k];
        u8 *p = table + k * 8;
        put16(p, d->map);
        put16(p + 2, d->split_screen);
        put16(p + 4, d->view_cfg[0]);
        put16(p + 6, d->view_cfg[1]);
    }
    check("data/demos.json", ROM_DEMO_TABLE, table,
          rom->data + ROM_DEMO_TABLE, sizeof table);
    for (int m = 0; m < HZ_MAPS; m++) {
        static u8 buf[0x800];
        const HzDemoRecording *d = &a->map[m].demo;
        memset(buf, 0, sizeof buf);
        for (int i = 0; i < d->unit_count; i++) {
            const HzStartUnit *u = &d->units[i];
            u8 *p = buf + i * 8;
            p[0] = u->flags;
            p[1] = u->type;
            p[2] = u->order;
            p[3] = u->dir;
            put16(p + 4, (u16)u->x);
            put16(p + 6, (u16)u->y);
        }
        for (int pad = 0; pad < 2; pad++) {
            u8 *p = buf + (pad == 0 ? 0x100 : 0x480);
            for (int i = 0; i < d->pad_runs[pad]; i++) {
                p[i * 2] = d->pad[pad][i].buttons;
                p[i * 2 + 1] = d->pad[pad][i].frames;
            }
        }
        char what[64];
        snprintf(what, sizeof what, "maps/%s/demo.json", hz_map_folder(m));
        check(what, ROM_DEMO_DATA + m * 0x800u, buf,
              rom->data + ROM_DEMO_DATA + m * 0x800u, sizeof buf);
    }
}

static void check_ending(const HzRom *rom, const HzAssets *a)
{
    const HzEndingAssets *e = &a->ending;
    check_words("palettes/ending_text.png", ROM_ENDING_TEXT_PALETTE,
                e->text_palette, 16, rom);
    check_rle("ending/story_font.png", rom, HZ_RLE_MENU_GFX, 3,
              e->story_font, sizeof e->story_font);
    check_rle("ending/ende.png", rom, HZ_RLE_MENU_GFX, 4, e->ende,
              sizeof e->ende);
    check_rle("ending/still_life.png", rom, HZ_RLE_ENDING_PICTURES, 0,
              e->pictures[0], sizeof e->pictures[0]);
    check_words("ending/still_life.png palette", ROM_ENDING_PALETTE_STILL,
                e->picture_palettes[0], 16, rom);
    check_rle("ending/grave.png", rom, HZ_RLE_ENDING_PICTURES, 1,
              e->pictures[1], sizeof e->pictures[1]);
    check_words("ending/grave.png palette", ROM_ENDING_PALETTE_GRAVE,
                e->picture_palettes[1], 16, rom);
    u8 *forest = unpack(rom, HZ_PACK_FOREST, 0, sizeof e->forest);
    if (forest == NULL) {
        printf("%-34s $%06X [0] doesn't unpack\n", "ending/forest.png",
               (unsigned)HZ_PACK_FOREST);
        g_regions++;
        g_bad++;
    } else {
        check("ending/forest.png", HZ_PACK_FOREST, e->forest, forest,
              sizeof e->forest);
        free(forest);
    }
    check_words("ending/forest.png palette", ROM_FOREST_PALETTE,
                e->forest_palette, 16, rom);

    /* EndingText1/2: 38 characters and two 0s a line. */
    static const u32 STORY[2] = { ROM_ENDING_TEXT1, ROM_ENDING_TEXT2 };
    static const char *const STORY_FILE[2] = { "ending/story_p1.txt",
                                               "ending/story_p2.txt" };
    static u8 buf[HZ_STORY_LINES * 40];
    for (int n = 0; n < 2; n++) {
        memset(buf, 0, sizeof buf);
        for (int l = 0; l < HZ_STORY_LINES; l++) {
            memcpy(buf + l * 40, e->story[n][l], HZ_STORY_WIDTH);
        }
        check(STORY_FILE[n], STORY[n], buf, rom->data + STORY[n],
              sizeof buf);
    }

    /* StaffText: 16 characters and a 0 a line, a 1 where names start, a 2
     * where headings start again, a 0 at the end. */
    static u8 staff[HZ_STAFF_LINES * 18 + 1];
    u32 len = 0;
    for (int i = 0; i < e->staff_count; i++) {
        if (i > 0 && e->staff[i].name != e->staff[i - 1].name) {
            staff[len++] = e->staff[i].name ? 1 : 2;
        }
        memcpy(staff + len, e->staff[i].text, HZ_STAFF_WIDTH);
        len += HZ_STAFF_WIDTH;
        staff[len++] = 0;
    }
    staff[len++] = 0;
    check("ending/staff.txt", ROM_STAFF_TEXT, staff,
          rom->data + ROM_STAFF_TEXT, len);
    u8 maker[2][HZ_STAFF_WIDTH + 1];
    for (int n = 0; n < 2; n++) {
        memcpy(maker[n], e->maker[n], HZ_STAFF_WIDTH);
        maker[n][HZ_STAFF_WIDTH] = 0;
    }
    check("ending/staff.txt japan", ROM_STAFF_JAPAN, maker[0],
          rom->data + ROM_STAFF_JAPAN, sizeof maker[0]);
    check("ending/staff.txt overseas", ROM_STAFF_OVERSEAS, maker[1],
          rom->data + ROM_STAFF_OVERSEAS, sizeof maker[1]);
}

/* -- the trackers: the songs as the game reads them against the text -- */

#define PLAY_SECONDS 200         /* a song: two turns of the longest */

typedef struct {
    u64 t;
    u8  part, reg, value;
} ChipWrite;

typedef struct {
    ChipWrite *w;
    long n, cap;
} ChipLog;

static void chip_sink(void *user, u64 t, u8 part, u8 reg, u8 value)
{
    ChipLog *l = user;
    if (l->n == l->cap) {
        long cap = l->cap ? l->cap * 2 : 1 << 16;
        ChipWrite *w = realloc(l->w, sizeof *w * (size_t)cap);
        if (w == NULL) {
            return;
        }
        l->w = w;
        l->cap = cap;
    }
    l->w[l->n++] = (ChipWrite){ t, part, reg, value };
}

/* Song n from image (and its windows), repeating (as the game repeats a
 * song; one it plays once plays the first turn of this), an effect every
 * 97 frames over it (the 29 in turn), PLAY_SECONDS long. */
static void play_song(const u8 *image, const u8 *windows, int count,
                      u16 sfx_table, int n, ChipLog *log)
{
    static HzSnd s;
    log->n = 0;
    hz_snd_init(&s, image, chip_sink, log);
    hz_snd_set_song_windows(&s, windows, count);
    u8 table[3] = { SND_CMD_SET_SFX_TABLE, (u8)(sfx_table >> 8),
                    (u8)sfx_table };
    u8 play[3] = { SND_CMD_PLAY_SONG, 0, (u8)n };
    u8 start[2] = { SND_CMD_START_SONG, 0xFF };
    hz_snd_post(&s, 0, table, 3);
    hz_snd_post(&s, 1, play, 3);
    hz_snd_post(&s, 2, start, 2);
    u64 frame = SND_MASTER / 60;
    for (u32 f = 3; f < PLAY_SECONDS * 60; f++) {
        if (f % 97 == 50) {
            u8 sfx[3] = { SND_CMD_PLAY_SFX, (u8)(f / 97 % 29), 0x7F };
            hz_snd_post(&s, f, sfx, 3);
        }
        hz_snd_run(&s, f * frame);
    }
}

/* The song text alone (songtext_use_trackers off) into the ROM's bytes;
 * then every song as the game read it (a's: its tracker where there is
 * one) played against the text's, write for write. */
static void check_sound(const HzRom *rom, const HzAssets *a,
                        const char *assets)
{
    static u8 text[HZ_SOUND_DATA];
    char dir[1024];
    u16 sfx_table = 0;
    u8 *windows = NULL;
    int count = 0;
    snprintf(dir, sizeof dir, "%s/sound", assets);
    songtext_use_trackers(false);
    bool ok = songtext_build(dir, text, &sfx_table, &windows, &count);
    songtext_use_trackers(true);
    if (!ok || windows != NULL) {
        printf("%-34s the text %s\n", "sound/ (the song text)",
               ok ? "outgrows the ROM's window" : "is refused");
        g_regions++;
        g_bad++;
        free(windows);
        return;
    }
    check("sound/ (the song text)", ROM_SOUND_DATA, text,
          rom->data + ROM_SOUND_DATA, HZ_SOUND_DATA);
    static ChipLog want, got;
    long writes = 0;
    g_regions++;
    for (int n = 0; n < HZ_SONGS; n++) {
        play_song(text, NULL, 0, sfx_table, n, &want);
        play_song(a->sound_data, a->song_windows, a->song_window_count,
                  a->sfx_table, n, &got);
        long k = 0;
        while (k < want.n && k < got.n
               && want.w[k].t == got.w[k].t && want.w[k].part == got.w[k].part
               && want.w[k].reg == got.w[k].reg
               && want.w[k].value == got.w[k].value) {
            k++;
        }
        writes += want.n;
        if (k < want.n || k < got.n) {
            printf("%-34s song %d DIFFERS at chip write %ld (%.3f s) of %ld\n",
                   "sound/songs (the trackers)", n, k,
                   (double)(k < want.n ? want.w[k].t : got.w[k].t) / SND_MASTER,
                   want.n);
            g_bad++;
            free(want.w);
            free(got.w);
            want = got = (ChipLog){ 0 };
            return;
        }
    }
    printf("%-34s %d songs, %ld chip writes the text's  ok\n",
           "sound/songs (the trackers)", HZ_SONGS, writes);
    free(want.w);
    free(got.w);
    want = got = (ChipLog){ 0 };
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: hz_roundtrip ROM ASSETS\n");
        return 2;
    }
    static HzRom rom;
    if (!hz_rom_load(&rom, argv[1])) {
        fprintf(stderr, "hz_roundtrip: %s is not a 512 KB ROM\n", argv[1]);
        return 2;
    }
    static HzAssets a;
    if (!hz_assets_load(&a, argv[2])) {
        hz_rom_free(&rom);
        return 1;
    }

    for (int n = 0; n < HZ_MAPS; n++) {
        check_map(&rom, &a, n);
    }
    check_words("palettes/game.png", ROM_GAME_PALETTES, a.game_palettes, 48,
                &rom);
    check_words("palettes/cycles.png", ROM_COLOUR_CYCLES,
                &a.colour_cycles[0][0], 32, &rom);
    check("tiles/mech.png", ROM_MECH_FRAMES, &a.mech_frames[0][0],
          rom.data + ROM_MECH_FRAMES, sizeof a.mech_frames);
    check("tiles/sprites.png", ROM_SPRITE_TILES, a.sprite_tiles,
          rom.data + ROM_SPRITE_TILES, sizeof a.sprite_tiles);
    check("tiles/hud.png", ROM_HUD_TILES, a.hud_tiles,
          rom.data + ROM_HUD_TILES, sizeof a.hud_tiles);
    check("tiles/font.png", ROM_FONT_TILES, a.font_tiles,
          rom.data + ROM_FONT_TILES, sizeof a.font_tiles);
    check_words("screens/hud_frame.txt", ROM_HUD_FRAME, &a.hud_frame[0][0],
                HZ_HUD_ROWS * HZ_HUD_COLS, &rom);

    static u8 buf[0x1000];
    for (int t = 0; t < HZ_UNIT_TYPES; t++) {
        pack_stats(buf + t * 32, &a.unit_stats[t]);
    }
    check("data/units.json units", ROM_UNIT_STATS, buf,
          rom.data + ROM_UNIT_STATS, HZ_UNIT_TYPES * 32);
    pack_stats(buf, &a.mech_stats.stats);
    memcpy(buf + 0x20, a.mech_stats.max_speed, 3);
    memcpy(buf + 0x23, a.mech_stats.fuel_use, 3);
    buf[0x26] = a.mech_stats.byte_26;
    buf[0x27] = a.mech_stats.byte_27;
    check("data/units.json mech", ROM_UNIT_STATS_KIDOU, buf,
          rom.data + ROM_UNIT_STATS_KIDOU, 0x28);
    check_words("data/units.json sprites", ROM_UNIT_DIR_TABLE,
                &a.unit_sprites[0][0], HZ_UNIT_SPRITE_TYPES * 16, &rom);
    check_words("data/units.json explosion_frames", ROM_EXPLOSION_FRAMES,
                a.explosion_frames, 16, &rom);

    memset(buf, 0, sizeof buf);
    for (int m = 0; m < HZ_MAPS; m++) {
        for (int h = 0; h < 2; h++) {
            for (int i = 0; i < a.start_unit_count[m][h]; i++) {
                const HzStartUnit *u = &a.start_units[m][h][i];
                u8 *p = buf + m * 0x200 + h * 0x100 + i * 8;
                p[0] = u->flags;
                p[1] = u->type;
                p[2] = u->order;
                p[3] = u->dir;
                put16(p + 4, (u16)u->x);
                put16(p + 6, (u16)u->y);
            }
        }
    }
    check("data/start_units.json maps", ROM_START_UNITS, buf,
          rom.data + ROM_START_UNITS, 0x1000);
    check_demos(&rom, &a);
    check("data/start_units.json cpu_start", ROM_CPU_START_TYPE_TABLE,
          a.cpu_start_types, rom.data + ROM_CPU_START_TYPE_TABLE, 4);
    check("data/sound.json", ROM_SFX_PRIORITY, a.sfx_priority,
          rom.data + ROM_SFX_PRIORITY, HZ_SFX);
    check_sound(&rom, &a, argv[2]);
    check("tiles/order_icons.png", ROM_HUD_ICONS, &a.order_icons[0][0],
          rom.data + ROM_HUD_ICONS, sizeof a.order_icons);
    memset(buf, 0, 0x100);
    for (int n = 0; n < HZ_ORDERS; n++) {
        memcpy(buf + n * 16, a.orders[n].name, HZ_ORDER_NAME);
        put16(buf + n * 16 + 10, a.orders[n].fee);
    }
    check("data/order_menu.json orders", ROM_ORDER_INFO, buf,
          rom.data + ROM_ORDER_INFO, 0x100);
    memset(buf, 0, 0x20);
    for (int m = 0; m < HZ_MAPS; m++) {
        put16(buf + m * 2, a.map[m].buyable);
    }
    check("data/order_menu.json buyable", ROM_UNIT_AVAIL_BY_MAP, buf,
          rom.data + ROM_UNIT_AVAIL_BY_MAP, 0x20);
    for (int m = 0; m < HZ_MAPS; m++) {
        char what[64];
        snprintf(what, sizeof what, "maps/%s/radar.png", hz_map_folder(m));
        check(what, ROM_RADAR_IMAGES + (u32)m * HZ_RADAR_IMAGE, a.map[m].radar,
              rom.data + ROM_RADAR_IMAGES + m * HZ_RADAR_IMAGE,
              HZ_RADAR_IMAGE);
    }
    check_menu(&rom, &a);
    check_ending(&rom, &a);

    for (int i = 0; i < HZ_TABLE_COUNT; i++) {
        const HzTableInfo *t = &HZ_TABLES[i];
        char what[64];
        snprintf(what, sizeof what, "tables.c %s", t->label);
        if (t->width == 1) {
            check(what, t->addr, t->data, rom.data + t->addr, t->bytes);
        } else if (t->width == -2) {
            const u16 *words = t->data;
            u8 *buf = malloc(t->bytes);
            for (size_t k = 0; k < t->bytes / 2; k++) {
                buf[k * 2] = (u8)words[k];
                buf[k * 2 + 1] = (u8)(words[k] >> 8);
            }
            check(what, t->addr, buf, rom.data + t->addr, t->bytes);
            free(buf);
        } else if (t->width == 4) {
            const u32 *longs = t->data;
            u8 *buf = malloc(t->bytes);
            for (size_t k = 0; k < t->bytes / 4; k++) {
                put16(buf + k * 4, (u16)(longs[k] >> 16));
                put16(buf + k * 4 + 2, (u16)longs[k]);
            }
            check(what, t->addr, buf, rom.data + t->addr, t->bytes);
            free(buf);
        } else {
            check_words(what, t->addr, t->data, t->bytes / 2, &rom);
        }
    }

    printf("roundtrip: %d regions, %ld bytes: %s\n", g_regions, g_bytes,
           g_bad == 0 ? "all identical" : "DIFFERENCES");
    hz_rom_free(&rom);
    return g_bad == 0 ? 0 : 1;
}
