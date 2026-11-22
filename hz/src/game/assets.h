#ifndef HZ_ASSETS_H
#define HZ_ASSETS_H

#include "../core/hz_types.h"
#include "../core/audio.h"

/* Everything the port takes from the game's data, loaded from assets/ at
 * start-up (hz_assets_load) and decoded into the forms the ported code
 * uses: tiles packed two pixels to a byte as VRAM holds them, nametable
 * words, CRAM words, records. After that nothing is read from disk, and
 * the ROM is never read at all.
 *
 * tools/hz_extract.py writes assets/ from the ROM; assets/README.md says
 * what each file is and which ROM data it came from; `make roundtrip`
 * (tools/hz_roundtrip.c) loads them with this code and checks the result
 * against the ROM byte for byte. The small code tables (SineTable and the
 * like) are C arrays in tables.c, not assets. */

#define HZ_MAPS            8
#define HZ_BLOCKS          512
#define HZ_LAYOUT_SIZE     64        /* blocks per side; 4 x 4 tiles each */
#define HZ_ANIM_RECORDS    32

#define HZ_MECH_FRAMES     64        /* MechFrames: 64 x $200, 4 x 4 tiles */
#define HZ_MECH_FRAME_BYTES 0x200
#define HZ_SPRITE_TILE_BYTES 0x5800  /* SpriteTiles + RadarBlank: VRAM tiles
                                        $240-$4FF */
#define HZ_HUD_TILE_BYTES  0x2000    /* HUDTiles: VRAM tiles $500-$5FF */
#define HZ_MASK_FIRST      0x240     /* (session 75) the first sprite tile */
#define HZ_MASK_TILES      0x3C0     /* ... to $5FF: units, shots, blasts */
#define HZ_FONT_TILES      79        /* FontTiles: $9E0 bytes, VRAM $D620 */
#define HZ_HUD_ROWS        4         /* HUDFrameTiles: window rows 0-3 */
#define HZ_HUD_COLS        40
#define HZ_UNIT_TYPES      14        /* UnitStats records before KIDOU */
#define HZ_UNIT_SPRITE_TYPES 10      /* UnitDirTable: types 0-9 */
#define HZ_START_UNITS     32        /* StartUnits records a half */
#define HZ_SFX             256       /* SfxPriority: a byte per sfx */
#define HZ_SOUND_DATA      0x8000    /* SoundData, $78000-$7FFFF */
#define HZ_ORDERS          16        /* OrderInfo records, HUDIcons */
#define HZ_ORDER_NAME      10        /* OrderInfo: name characters */
#define HZ_ORDER_ICON_BYTES 0x80     /* HUDIcons: 2 x 2 tiles an order */
#define HZ_RADAR_IMAGE     0x800     /* RadarImages: 64 x 64 pixels, 8 x 8
                                        tiles in rows */
#define HZ_DEMOS           16        /* DemoTable: the attract mode's list */
#define HZ_DEMO_UNITS      32        /* DemoData: unit records a map */
#define HZ_DEMO_RUNS       447       /* DemoData: a pad's runs of 1-255
                                        frames; its $380 bytes end with a
                                        run of 0 */

/* A UnitStats record ($20 bytes), each field at its offset. */
typedef struct {
    u8  name[8];        /* +$00, in the game's font codes */
    u16 price;          /* +$08 stat_price */
    u16 hp;             /* +$0A stat_hp */
    u8  size;           /* +$0C stat_size */
    u8  ammo;           /* +$0D stat_ammo */
    u16 fuel;           /* +$0E stat_fuel */
    u8  sprite_size;    /* +$10 stat_sprite_size: VDP size bits */
    u8  speed;          /* +$11 stat_speed */
    u8  overlay;        /* +$12 stat_overlay: b7 turret of type & $7F */
    u8  fuel_use;       /* +$13 stat_fuel_use */
    u8  shot_speed;     /* +$14 stat_shot_speed */
    u8  shot_life;      /* +$15 stat_shot_life */
    u8  fire_delay;     /* +$16 stat_fire_delay */
    u8  power;          /* +$17 stat_power */
    u8  sight;          /* +$18 stat_sight */
    u8  build_time;     /* +$19 stat_build_time: x 128 frames */
    u8  missiles;       /* +$1A stat_missiles */
    u8  missile_speed;  /* +$1B stat_missile_speed */
    u8  missile_life;   /* +$1C stat_missile_life */
    u8  missile_delay;  /* +$1D stat_missile_delay */
    u16 orders;         /* +$1E stat_orders: a bit per order */
} HzUnitStats;

/* The mech's forms, as KIDOU's per-form bytes are ordered. */
#define HZ_FORM_ROBOT    0
#define HZ_FORM_JET      1
#define HZ_FORM_CARRYING 2

/* UnitStats_KIDOU: the player mech's record, a unit's and 8 more bytes. */
typedef struct {
    HzUnitStats stats;
    u8  max_speed[3];   /* +$20-$22: robot, jet, carrying */
    u8  fuel_use[3];    /* +$23-$25: robot, jet, carrying */
    u8  byte_26;        /* +$26, +$27: no reader found yet */
    u8  byte_27;
} HzMechStats;

/* A StartUnits record (8 bytes). */
typedef struct {
    u8  flags;          /* b7 present, b4 team 2 (b6, b0: see README) */
    u8  type;
    u8  order;
    u8  dir;
    s16 x, y;
} HzStartUnit;

/* A DemoTable entry: the map, and split screen or wide screen showing
 * P1's view or P2's, as v_view_cfg's two words (a view slot, $FF hidden,
 * and a view index each). */
typedef struct {
    u8  map;
    u16 split_screen;
    u16 view_cfg[2];
} HzDemo;

/* A run of a recorded pad: the buttons (HZ_PAD_* bits, as v_joypad) held
 * for 1-255 frames. */
typedef struct {
    u8  buttons;
    u8  frames;
} HzPadRun;

/* DemoData for one map: the units the demo starts with (StartUnits'
 * records) and each pad's runs. A file's longer runs are split into runs
 * of 255 at load time, as the recorder stored them. */
typedef struct {
    HzStartUnit units[HZ_DEMO_UNITS];
    u8  unit_count;
    HzPadRun pad[2][HZ_DEMO_RUNS];
    u16 pad_runs[2];
} HzDemoRecording;

/* An OrderInfo record (16 bytes): the order's name in font codes and
 * the fee the order menu adds to a unit's price. */
typedef struct {
    u8  name[HZ_ORDER_NAME];
    u16 fee;            /* +$0A */
} HzOrderInfo;

/* (Session 72) data/repair.json, the port's own: the REPAIR patch's truck
 * and its mechanics (docs/repair.md). */
typedef struct {
    HzUnitStats stats[2];       /* types 10 and 11: the truck, a mechanic.
                                   A truck's ammo is its crew, 4 a man */
    HzOrderInfo order;          /* order $E, the truck's: name and fee */
    u8  order_icon[HZ_ORDER_ICON_BYTES];    /* and its icon */
    u8  colours[2][16];         /* the truck's and a mechanic's sprites:
                                   colour c is drawn as colours[c] of the
                                   same palette line (the SUPPLY's and the
                                   INFANTRY's tiles) */
    u16 search;                 /* px: how far a truck drives for a
                                   damaged vehicle */
    u16 park;                   /* px between it and the vehicle when it
                                   stops */
    u16 reach;                  /* px from the truck its men work in */
    u16 danger;                 /* px: an enemy this near sends the men
                                   in and the truck home */
    u16 calm;                   /* frames with no enemy near before it
                                   works again */
    u8  rate;                   /* HP a man mends each time he thinks (12
                                   frames) */
} HzRepair;

/* The title and the options screen's data (the $53000 module's). */
#define HZ_PHOTO_TILES      1120     /* RLE_TankPhotos 0: 40 x 28 */
#define HZ_LOGO_TILES       320      /* RLE_MenuGfx 2: 29 x 11, then the
                                        palette as the 320th tile */
#define HZ_TITLE_GFX_TILES  128      /* RLE_MenuGfx 1: VRAM tiles $480-$4FF */
#define HZ_MENU_FONT_TILES  256      /* RLE_MenuGfx 0: VRAM tiles 0-$FF */
#define HZ_SEGA_CYCLE       31       /* SegaLogoCycle */
#define HZ_COPYRIGHT_CYCLE      23       /* CopyrightCycle */
#define HZ_SONGS            18       /* MusicTitles, MusicTestRepeat */
#define HZ_SONG_TITLE       19       /* characters a line, two lines */
#define HZ_MAP_NAME         7        /* MapNames: characters */
#define HZ_WINNER_FONT_TILES 256     /* RLE_WinnerFont: VRAM tiles $580-$67F */

typedef struct {
    u8  tank_photo[HZ_PHOTO_TILES * 32];
    u16 tank_photo_palette[16];          /* TankPhotoPalette */
    u8  title_logo[HZ_LOGO_TILES * 32];  /* tile 319 = its CRAM line, as
                                            Title_LoadLogo takes it */
    u8  title_gfx[HZ_TITLE_GFX_TILES * 32];  /* the SEGA logo and the
                                            copyright line */
    u8  menu_font[HZ_MENU_FONT_TILES * 32];
    u16 menu_palette[64];                /* MenuPalette: CRAM lines 0-3 */
    u16 sega_palette[16];          /* SegaLogoPalette */
    u16 copyright_palette[16];         /* CopyrightPalette */
    u16 sega_cycle[HZ_SEGA_CYCLE];
    u16 copyright_cycle[HZ_COPYRIGHT_CYCLE];
    u8  song_titles[HZ_SONGS][2][HZ_SONG_TITLE];  /* in the menu font's
                                            codes ('@' the blank) */
    bool song_repeat[HZ_SONGS];          /* MusicTestRepeat $FF */
    u8  map_names[HZ_MAPS][HZ_MAP_NAME]; /* MapNames */
    u8  sfx_count;                       /* M_SfxCount: the SOUND test's */
    /* The game's end (game_end.c): */
    u8  winner_font[HZ_WINNER_FONT_TILES * 32];  /* the 2 x 2 font */
    u8  tank_photo2[HZ_PHOTO_TILES * 32];        /* RLE_TankPhotos 1 */
    u16 winner_photo_palette[16];        /* WinnerPhotoPalette: line 3 */
    u16 winner_palette[48];              /* WinnerPalette: lines 0-2 */
} HzMenuAssets;

/* The endings (ending.c), after a record of every map won on every
 * difficulty. Text is in the story font's codes (a character c is tile
 * c - $30; '@' the blank). */
#define HZ_STORY_FONT_TILES 80       /* RLE_MenuGfx 3: VRAM tiles 0-$4F */
#define HZ_ENDE_TILES       64       /* RLE_MenuGfx 4: 16 x 4, tiles $80 up */
#define HZ_STORY_LINES      22       /* EndingText1/2 */
#define HZ_STORY_WIDTH      38
#define HZ_STAFF_WIDTH      16
#define HZ_STAFF_LINES      64       /* the port's room; StaffText has 21 */

typedef struct {
    u8   text[HZ_STAFF_WIDTH];
    bool name;                       /* a name (yellow), else a heading
                                        (red) */
} HzStaffLine;

typedef struct {
    u8  story_font[HZ_STORY_FONT_TILES * 32];
    u8  ende[HZ_ENDE_TILES * 32];
    u16 text_palette[16];                /* EndingTextPalette */
    u8  pictures[2][HZ_PHOTO_TILES * 32];  /* RLE_EndingPictures: the still
                                            life, the grave */
    u16 picture_palettes[2][16];         /* EndingPaletteStillLife, Grave */
    u8  forest[HZ_PHOTO_TILES * 32];     /* pack_forest (LZW) */
    u16 forest_palette[16];              /* ForestPalette */
    u8  story[2][HZ_STORY_LINES][HZ_STORY_WIDTH];  /* EndingText1, 2 */
    HzStaffLine staff[HZ_STAFF_LINES];   /* StaffText */
    int staff_count;
    u8  maker[2][HZ_STAFF_WIDTH];        /* the last line: [0] a Japanese
                                            machine's, [1] an overseas one's */
} HzEndingAssets;

/* One map: what Game_LoadMap's three LZW entries and MapPalettes hold. */
typedef struct {
    u8  tiles[HZ_BLOCKS * 32];      /* 512 tiles, as VRAM $0000 gets them */
    u16 layout[HZ_LAYOUT_SIZE][HZ_LAYOUT_SIZE];  /* low 9 bits block,
                                       b9 path hint, b10-15 hint dirs */
    u16 blocks[HZ_BLOCKS][16];      /* v_map_blocks: 4 x 4 nametable words */
    u16 terrain[HZ_BLOCKS];         /* v_block_terrain */
    u16 anim[HZ_ANIM_RECORDS][16];  /* v_anim_tiles records */
    u16 palette[16];                /* MapPalettes: CRAM line 0 */
    u8  radar[HZ_RADAR_IMAGE];      /* RadarImages: the order menu's map
                                       radar, tiles as VRAM holds them */
    u16 buyable;                    /* UnitAvailByMap: a bit per unit type
                                       the order menu offers */
    HzDemoRecording demo;           /* DemoData: the attract demo here */
} HzMapAssets;

typedef struct {
    HzMapAssets map[HZ_MAPS];

    u16 game_palettes[48];          /* GamePalettes: CRAM lines 1-3 */
    u16 colour_cycles[2][16];       /* ColourCycles: CRAM line 0 colours
                                       2 and $B, a step every 6 frames */
    u8  mech_frames[HZ_MECH_FRAMES][HZ_MECH_FRAME_BYTES];
    u8  sprite_tiles[HZ_SPRITE_TILE_BYTES];
    u8  hud_tiles[HZ_HUD_TILE_BYTES];
    /* (Session 75, HZ_FIX_PIXELS) where the tiles above are drawn, made
     * from them once they are loaded (hitmask_build): per pixel row a
     * byte, bit 7 the left pixel, set where its colour isn't 0. Sprite
     * tiles $240-$5FF (units, shots, blasts) and each mech frame's 16. */
    u8  tile_rows[HZ_MASK_TILES][8];
    u8  mech_rows[HZ_MECH_FRAMES][16][8];
    u8  font_tiles[HZ_FONT_TILES * 32];
    u16 hud_frame[HZ_HUD_ROWS][HZ_HUD_COLS];

    HzUnitStats unit_stats[HZ_UNIT_TYPES];
    u16 unit_sprites[HZ_UNIT_SPRITE_TYPES][16];  /* UnitDirTable: sprite
                                                    attr per heading */
    u16 explosion_frames[16];       /* ExplosionFrames */
    HzMechStats mech_stats;         /* UnitStats_KIDOU */

    HzStartUnit start_units[HZ_MAPS][2][HZ_START_UNITS];  /* [map][half]:
                                       half 0 used when P1 is the CPU */
    u8  start_unit_count[HZ_MAPS][2];
    u8  cpu_start_types[4];         /* CPUStartTypeTable */
    u8  sfx_priority[HZ_SFX];       /* SfxPriority */
    u8  sound_data[HZ_SOUND_DATA];  /* SoundData: the Z80's bank window
                                       (songs, sfx, instruments), built
                                       from sound/'s text */
    u16 sfx_table;                  /* the effects' table in it, a Z80
                                       address (the ROM's $FA00) */
    u8 *song_windows;               /* NULL while the songs all fit
                                       sound_data; else a window each,
                                       song_window_count of HZ_SOUND_DATA
                                       (malloc'd, kept), and sound_data
                                       the effects alone (docs/sound.md,
                                       Room) */
    int song_window_count;
    HzDemo demos[HZ_DEMOS];         /* DemoTable */
    /* (Session 37) sound/q_drums.json, the port's own (no ROM behind it):
     * Q's drum models and the songs' instruments that strike them. An
     * absent file: none. */
    HzDrumModel drum_models[HZ_DRUM_MODELS];
    int         drum_model_count;
    HzDrumUse   drum_uses[HZ_DRUM_USES];
    int         drum_use_count;
    float       drum_voice_db;  /* (session 41) "voice_level_db" */
    /* (Session 60) data/turrets.json, the port's own: the records types 6
     * and 7 take with the TURRETS patch (UnitStats' are "NOT USE", all 0,
     * and stay as the ROM has them). An absent file: no turrets. */
    HzUnitStats turret_stats[2];
    bool        turrets_loaded;
    /* (Session 72) data/repair.json, the port's own: the REPAIR patch's
     * repair truck and mechanic, types 10 and 11 (UnitStats' records
     * there are empty and stay so). An absent file: no repair truck. */
    HzRepair    repair;
    bool        repair_loaded;
    HzOrderInfo orders[HZ_ORDERS];  /* OrderInfo */
    u8  order_icons[HZ_ORDERS][HZ_ORDER_ICON_BYTES];  /* HUDIcons */
    HzMenuAssets menu;              /* the title and the options screen */
    HzEndingAssets ending;          /* the endings */
} HzAssets;

/* Reads every file under `dir` into `a`. False, with what went wrong on
 * stderr (file and line or field), if any is missing or malformed. */
bool hz_assets_load(HzAssets *a, const char *dir);

/* The folder name of map n under assets/maps ("0_abgrund" ...). */
const char *hz_map_folder(int n);

#endif /* HZ_ASSETS_H */
