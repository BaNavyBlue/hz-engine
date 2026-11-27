#include "game.h"
#include "tables.h"

/* The map's animated tiles and colours.
 *
 * A map has 32 records (v_anim_tiles, assets/maps/N/anim.txt), 16 words
 * each. Word 0 is a map tile that animates (0: the record is off); words
 * 0, 1, 2 ... are the tiles whose pictures it shows in turn, up to a 0
 * word or the 16th. The picture is put into word 0's own VRAM slot: by a
 * VRAM copy from the tile named, or, when the record comes back to word
 * 0, by a write of the slot's original picture, which Map_InitAnimTiles
 * kept in v_anim_tile_gfx. Records act in order and copy whatever the
 * source slot holds at the time, so one record can show another's
 * animation (VULKAN's records 0-8 copy from $1F0-$1F5, which records 1
 * and 9-14 animate; records 1 and 9 both animate $1F0).
 *
 * Both halves run only when v_unit_ai_group is 0 or 6. UnitAI_Update has
 * just stepped it, so these are the frames after groups $B and 5, the two
 * that run no units: one step every 6 frames. Map_AnimTilesUpload ends
 * with a colour cycle on every map: CRAM line 0 colours 2 and $B step
 * through ColourCycles (assets/palettes/cycles.png). */

#define STEP_OFF 0x80           /* v_anim_tile_step: the record is off */

static bool anim_frame(const HzGame *g)
{
    return g->unit_ai_group == 0 || g->unit_ai_group == 6;
}

/* A tile's picture in VRAM: 32 bytes a tile. The 68k masks the slot to
 * the first 16K (its command's A15-14 are 0). */
static u16 tile_vram(u16 tile)
{
    return (u16)((tile << 5) & 0x3FFF);
}

/* Map_AnimTilesUpdate ($5738), from Game_FrameLoop after
 * HUD_UpdateGauges. Each record's job is cleared; an off record does
 * nothing more. Else the word at its step (& $1FF) is the tile to show.
 * If it isn't the one shown, it is now, and a job is queued: the record's
 * own picture back when the tile equals word 0, else a VRAM copy of that
 * tile into word 0's slot. QUIRK: word 0 is compared as a whole word, so
 * a word 0 with bits above $1FF never gets its picture back: it is copied
 * onto itself instead (no record that changes tile has such a word 0).
 * Then the step goes back to 0 after 15 or when the next word & $1FF is
 * 0, else on by one. */
void map_anim_tiles_update(HzGame *g)
{
    if (!anim_frame(g)) {
        return;
    }
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
        HzAnimTileJob *job = &g->anim_tile_queue[r];
        const u16 *rec = g->anim_tiles[r];
        job->pending = false;
        job->copy = false;
        u8 step = g->anim_tile_step[r];
        if (step & STEP_OFF) {
            continue;
        }
        /* Steps only ever hold 0-15 (or STEP_OFF); the 68k would read
         * past the record for more. */
        step &= 0x0F;
        u16 tile = (u16)(rec[step] & 0x1FF);
        if (tile != g->anim_tile_shown[r]) {
            g->anim_tile_shown[r] = tile;
            job->pending = true;
            job->copy = tile != rec[0];
            job->from = tile;
            job->to = tile_vram(rec[0]);
        }
        if (step == 15 || (rec[step + 1] & 0x1FF) == 0) {
            g->anim_tile_step[r] = 0;
        } else {
            g->anim_tile_step[r] = (u8)(step + 1);
        }
    }
}

/* Map_AnimTilesUpload ($5812), from Game_FrameLoop after VDP_WaitVBlank,
 * with the display off (after Map_FlushScroll and OrderMenu_UpdateBoth),
 * on the same frames. Each queued job in order: the record's own picture
 * put back into its tile, or the tile named copied there (on the 68k a
 * VRAM copy DMA of 32 bytes); the screen stays off. Then each colour
 * cycle's counter + 1, and its ColourCycles colour (& $F) into its
 * palette entry (ColourCycleCRAM). */
void map_anim_tiles_upload(HzGame *g)
{
    if (!anim_frame(g)) {
        return;
    }
    Video *v = g->video;
    for (int r = 0; r < HZ_ANIM_RECORDS; r++) {
        const HzAnimTileJob *job = &g->anim_tile_queue[r];
        if (!job->pending) {
            continue;
        }
        /* The job keeps the 68k's slot address: the tile is it / 32. */
        u16 to = (u16)(job->to >> 5);
        if (job->copy) {
            video_copy_tile(v, to, (u16)(tile_vram(job->from) >> 5));
        } else {
            video_load_tiles(v, to, g->anim_tile_gfx[r], 1);
        }
    }
    video_display(v, false);
    for (int k = 0; k < 2; k++) {
        g->colour_cycle[k] = (u16)(g->colour_cycle[k] + 1);
        /* ColourCycleCRAM's commands name the colours by their CRAM
         * address: line 0's colours 2 and $B. */
        int entry = (int)((COLOUR_CYCLE_CRAM[k] >> 17) & 0x3F);
        video_set_colour(v, entry,
                         g->assets->colour_cycles[k][g->colour_cycle[k] & 0x0F]);
    }
}
