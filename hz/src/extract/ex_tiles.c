/* (Session 78) hz_extract.py's extract_tiles in C (ex.h): the sprite
 * sheets (mech, sprites, hud, order icons) with their JSON, the font, the
 * game's palettes and colour cycles, and the HUD strip's nametable. The
 * groups are hz_extract.py's: the port's names for the tiles. */
#include "ex.h"

#include <stdio.h>

#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static const ExGroup MECH_GROUPS[] = {
    { "mech frames: $00-$2F robot, $30-$39 jet, $3A-$3F transforming",
      0x000, 4, 4, 64, false },
};

static const ExGroup SPRITE_GROUPS[] = {   /* VRAM tiles $240-$4FF */
    { "AMR-51D: headings 0-4", 0x240, 3, 3, 5, false },
    { "TAX-52: headings 0-4", 0x26D, 3, 3, 5, false },
    { "SAM-42: headings 0-4", 0x29A, 3, 3, 5, false },
    { "ST-57U: headings 0-4", 0x2C7, 3, 3, 5, false },
    { "SUPPLY: headings 0-4", 0x2F4, 3, 3, 5, false },
    { "GRM-34A: headings 0-4", 0x321, 3, 3, 5, false },
    { "TAX-52 turret: headings 0-4", 0x34E, 3, 3, 5, false },
    { "ST-57U turret: headings 0-4", 0x37B, 3, 3, 5, false },
    { "order menu pictures (UnitMenuTiles): INFANTRY, TAX-52, ST-57U, FWA",
      0x3A8, 3, 3, 4, false },
    { "tiles $3CC-$3EB, blank: nothing draws them", 0x3CC, 1, 1, 32, false },
    { "FWA: headings 0-4", 0x3EC, 2, 2, 5, false },
    { "INFANTRY: headings 0-15", 0x400, 2, 2, 16, false },
    { "INFANTRY: headings 0-15, other step", 0x440, 2, 2, 16, false },
    { "INFANTRY dying: 15 - HP", 0x480, 2, 2, 16, false },
    { "radar, blank (RadarBlank)", 0x4C0, 8, 8, 1, true },
};

static const ExGroup HUD_GROUPS[] = {      /* VRAM tiles $500-$5FF */
    { "divider between the views", 0x500, 4, 4, 1, false },
    { "explosion (ExplosionFrames)", 0x510, 3, 3, 6, false },
    { "jet shadow", 0x570, 4, 4, 1, false },
    { "unit out of fuel or ammo", 0x590, 2, 2, 1, false },
    { "outpost hold bar, P1", 0x594, 1, 4, 1, false },
    { "outpost hold bar, P2", 0x598, 1, 4, 1, false },
    { "HQ hit warning", 0x5A3, 3, 3, 1, false },
    { "HUD bar cells: HUD_QueueBar, see the README", 0x5B0, 1, 1, 15,
      false },
    { "unit building", 0x5C0, 3, 3, 1, false },
    { "unit ready", 0x5C9, 3, 3, 1, false },
    { "missile warning arrow", 0x5D2, 2, 2, 1, false },
};

static const ExGroup ORDER_ICON_GROUPS[] = {   /* HUDIcons */
    { "order icons (HUDIcons): orders 0-15, by OrderInfo", 0x00, 2, 2, 16,
      false },
};

/* extract_tiles. */
bool ex_tiles(HzEx *x)
{
    const u8 *pal_b  = ex_at(x, EXR_GamePalettes, 0x60);
    const u8 *mech   = ex_at(x, EXR_MechFrames, 0x8000);
    const u8 *sprite = ex_at(x, EXR_SpriteTiles, 0x5800);
    const u8 *hud    = ex_at(x, EXR_HUDTiles, 0x2000);
    const u8 *icons  = ex_at(x, EXR_HUDIcons, 0x800);
    const u8 *font   = ex_at(x, EXR_FontTiles, 0x9E0);
    const u8 *cyc_b  = ex_at(x, EXR_ColourCycles, 0x40);
    const u8 *frame  = ex_at(x, EXR_HUDFrameTiles, 4 * 40 * 2);
    if (pal_b == NULL || mech == NULL || sprite == NULL || hud == NULL
        || icons == NULL || font == NULL || cyc_b == NULL || frame == NULL) {
        return false;
    }
    u16 game[48], cycles[32];
    ex_palette_words(pal_b, 3, game);
    ex_palette_words(cyc_b, 2, cycles);
    const u16 *line1 = game, *line2 = game + 16;

    bool ok = ex_write_sprite_sheet(x, "tiles", "mech", mech, 0x8000, 0,
                                    0x400, MECH_GROUPS, COUNT(MECH_GROUPS),
                                    line2)
           && ex_write_sprite_sheet(x, "tiles", "sprites", sprite, 0x5800,
                                    0x240, 0x2C0, SPRITE_GROUPS,
                                    COUNT(SPRITE_GROUPS), line2)
           && ex_write_sprite_sheet(x, "tiles", "hud", hud, 0x2000, 0x500,
                                    0x100, HUD_GROUPS, COUNT(HUD_GROUPS),
                                    line1)
           && ex_write_sprite_sheet(x, "tiles", "order_icons", icons, 0x800,
                                    0, 0x40, ORDER_ICON_GROUPS,
                                    COUNT(ORDER_ICON_GROUPS), line1)
           && ex_write_tile_sheet(x, "tiles/font.png", font, 0x9E0, 79,
                                  line1, 16, true)
           && ex_write_palette(x, "palettes/game.png", game, 48, 16)
           && ex_write_palette(x, "palettes/cycles.png", cycles, 32, 16);
    if (!ok) {
        return false;
    }

    ExBuf b;
    exb_init(&b);
    exb_header(&b, "The HUD strip (HUDFrameTiles): window rows 0-3, 40 "
                   "nametable words");
    exb_header(&b, "a row, in hex. Screen_InitGame writes them to the "
                   "window plane. The");
    exb_header(&b, "tiles are VRAM tile numbers: $500-$5FF tiles/hud.png, "
                   "$6B1-$6FF");
    exb_header(&b, "tiles/font.png. hud_frame.png is a picture of it; "
                   "editing it");
    exb_header(&b, "changes nothing.");
    for (u32 r = 0; r < 4; r++) {
        for (u32 c = 0; c < 40; c++) {
            exb_printf(&b, (c == 0) ? "%04X" : " %04X",
                       (unsigned)ex_w(x, EXR_HUDFrameTiles
                                         + (r * 40 + c) * 2));
        }
        exb_putc(&b, '\n');
    }
    ok = ex_write_buf(x, "screens/hud_frame.txt", &b);
    exb_free(&b);
    return ok;
}
