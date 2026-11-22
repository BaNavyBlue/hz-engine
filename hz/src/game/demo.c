#include "game.h"

/* The attract demo. When the title runs out (its song ends before Start),
 * MainLoop_Title plays a demo: the next entry of the demo list
 * (assets->demos, DemoTable) picks the map and the screen, and a game runs
 * with both players human, their pads played back from the map's
 * recording (DemoData, assets->map[].demo), from the recording's units and
 * a random seed of 0, so it comes out the same each time. It ends when a
 * recording runs out, after 60 s (Game_SecondTick), or on Start (here or
 * on the loading screen), and the title comes back.
 *
 * Not ported: the development recorder that made the recordings
 * (Demo_SnapshotUnits, Demo_RecordInput, Demo_ClearRecord,
 * Dev_SaveStartUnits, v_record_mode 1-3): nothing in the shipping game
 * turns it on, and on a cartridge it writes to ROM. */

/* Demo_SetupNext ($544), from Game_SetupMode in a demo: the list's entry
 * demo_index gives the map, split or wide screen and the views (both
 * players' v_view_cfg words); free ammo off (and v_dev_heal_mechs and
 * v_record_mode, which the port doesn't keep); both players human (flags
 * $80 and $90: the recordings drive both mechs); demo_index on to the
 * next (& $F: the 16 play in turn, and the place survives games); the
 * views' cameras. v_mech_option2 is left as the last game set it (only a
 * computer player reads it). */
void demo_setup_next(HzGame *g)
{
    const HzDemo *d = &g->assets->demos[g->demo_index & 0xF];
    g->map = d->map;
    g->split_screen = d->split_screen;
    g->view_cfg[0] = d->view_cfg[0];
    g->view_cfg[1] = d->view_cfg[1];
    g->free_ammo = 0;
    g->player_flags[0] = 0x80;
    g->player_flags[1] = 0x90;
    g->demo_index = (u16)((g->demo_index + 1) & 0xF);
    game_set_view_cameras(g);
}

/* A pad's run at `pos`; past the recording's last, a run of 0 frames (the
 * ROM's stream ends with one, and its unused bytes are 0). */
static HzPadRun demo_run(const HzGame *g, int pad, u16 pos)
{
    const HzDemoRecording *d = &g->assets->map[g->map].demo;
    if (pos < d->pad_runs[pad]) {
        return d->pad[pad][pos];
    }
    return (HzPadRun){ 0, 0 };
}

/* Demo_Start ($618) with Demo_SpawnUnits ($95BA), from Game_Start in a
 * demo: the recording's units spawned as recorded (as StartUnits'
 * records, through Unit_Spawn), and each pad's frame count from its first
 * run (Game_Start has put both pads at run 0). */
void demo_start(HzGame *g)
{
    const HzDemoRecording *d = &g->assets->map[g->map].demo;
    for (int i = 0; i < d->unit_count; i++) {           /* .record */
        const HzStartUnit *u = &d->units[i];
        if (u->flags & UNIT_ACTIVE) {
            unit_spawn(g, u->flags, u->type, u->order, u->dir, u->x, u->y);
        }
    }
    for (int p = 0; p < 2; p++) {
        g->demo_frames[p] = demo_run(g, p, g->demo_pos[p]).frames;
    }
}

/* Demo_PlayInput ($44E4), each frame of a demo that Start doesn't end
 * (Game_CheckPause, after ReadJoypads): each pad's byte is its current
 * run's buttons, over what the controllers gave; the run's frames count
 * down, and at 0 the next run's count is taken and the place moves on. A
 * current run of 0 frames is the recording's end: v_frame_counter_lo =
 * $FF00, so Game_SecondTick goes back to the title this frame. P1's
 * recording is looked at first; once it has ended, P2's pad keeps what
 * its controller gave.
 *
 * QUIRK: the count is a byte, as the 68k's: it only reaches 0 from 1, so
 * a pad left at 0 (no Demo_Start: never in the game) plays its run for
 * 256 frames. */
void demo_play_input(HzGame *g)
{
    for (int p = 0; p < 2; p++) {                       /* P1, then .p2 */
        HzPadRun run = demo_run(g, p, g->demo_pos[p]);
        if (run.frames == 0) {
            g->frame_counter = (g->frame_counter & 0xFFFF0000u)
                             | 0xFF00u;                 /* .ended */
            return;
        }
        g->joypad[p] = run.buttons;
        g->demo_frames[p]--;
        if (g->demo_frames[p] == 0) {
            g->demo_frames[p] = demo_run(g, p, g->demo_pos[p] + 1).frames;
            g->demo_pos[p]++;
        }
    }
}
