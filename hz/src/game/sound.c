#include "game.h"

#include "../audio/hz_snd.h"

#include <stddef.h>

/* The 68k's side of the sound: the Z80 driver is a program of its own
 * (asm/hz.asm's Z80Driver, ported as src/audio/hz_snd.c, run by the host's
 * src/core/audio.c), which the 68k only posts commands to. Each Snd_*
 * routine waits for the Z80's mailbox ($A00003) to be free, then writes a
 * command and its arguments there; here they go to the host's hook in
 * order (the ring there is the waiting). See HANDOVER.md, Sound, for the
 * commands. */

static void post(HzGame *g, const u8 *bytes, int count)
{
    if (g->snd_post != NULL) {
        g->snd_post(g->snd_user, bytes, count);
    }
}

/* Snd_Init ($25FA), after copying the driver to the Z80 and releasing it
 * (the host's hz_audio_create): Snd_SetBank SoundData ($78000),
 * Snd_SetSong $8000 (the song table itself: harmless, Cmd_PlaySong sets
 * z_song), Snd_SetSfxTable $FA00 (which also empties the sfx queue): the
 * effects' table where sound/sounds.txt puts it, the ROM's $FA00. */
void snd_init(HzGame *g)
{
    static const u8 set_bank[5] = { SND_CMD_SET_BANK, 0x00, 0x07, 0x80,
                                    0x00 };
    static const u8 set_song[3] = { SND_CMD_SET_SONG, 0x80, 0x00 };
    const u8 set_sfx[3] = { SND_CMD_SET_SFX_TABLE,
                            (u8)(g->assets->sfx_table >> 8),
                            (u8)g->assets->sfx_table };

    post(g, set_bank, 5);
    post(g, set_song, 3);
    post(g, set_sfx, 3);
    g->sfx_queued = 0xFF;
    g->sfx_priority = 0xFF;
}

/* Snd_PlayMusic ($26F8): Snd_StopIfBusy (command $06 while a DAC sample
 * plays: never, the port's driver plays none), then command $09 with the
 * song number as a big-endian word: the song plays once. */
void snd_play_music(HzGame *g, u16 song)
{
    u8 b[3] = { SND_CMD_PLAY_SONG, (u8)(song >> 8), (u8)song };

    post(g, b, 3);
}

/* Snd_StartSong ($26DC): command $01, byte: the song Snd_PlayMusic chose
 * starts again, repeating unless the byte is 0. */
void snd_start_song(HzGame *g, u8 repeat)
{
    u8 b[2] = { SND_CMD_START_SONG, repeat };

    post(g, b, 2);
}

/* Snd_FadeMusic ($271E): command $03, VBlanks a fade step, 0; a 0 stops
 * the music now. */
void snd_fade_music(HzGame *g, u8 speed)
{
    u8 b[3] = { SND_CMD_FADE_MUSIC, speed, 0x00 };

    post(g, b, 3);
}

/* Snd_StopSfx ($27D6): command $04, every sound effect stopped. */
void snd_stop_sfx(HzGame *g)
{
    static const u8 b[1] = { SND_CMD_STOP_SFX };

    post(g, b, 1);
}

/* Snd_QueueSfx ($275A): queue `sfx` unless something with a lower
 * SfxPriority value is queued already (equal values: the later wins).
 * (Session 54) Every effect asked for is also marked in sfx_asked, for
 * the rumble: the game's own word for what just happened. */
void snd_queue_sfx(HzGame *g, u8 sfx)
{
    if (sfx < 32) {
        g->sfx_asked |= 1u << sfx;      /* (session 54) the port's */
    }
    u8 priority = g->assets->sfx_priority[sfx];         /* SfxPriority */
    if (priority > g->sfx_priority) {
        return;                                         /* .done */
    }
    g->sfx_priority = priority;
    g->sfx_queued = sfx;
}

/* Snd_FlushSfx ($2796), once a frame after the uploads: with something
 * queued, take the Z80 bus; if the mailbox is free and the driver's byte
 * $A00002 (z_dac_busy) is 0, send command $02 (sfx, priority); either way
 * empty the queue, unless the mailbox was busy, which keeps it for the
 * next frame. The port's mailbox is a queue, never busy, and z_dac_busy is
 * always 0 (no DAC sample plays): it sends, and records what it sent. */
void snd_flush_sfx(HzGame *g)
{
    g->sfx_sent = 0xFF;
    if (g->sfx_queued == 0xFF) {
        return;
    }
    u8 b[3] = { SND_CMD_PLAY_SFX, g->sfx_queued, g->sfx_priority };

    post(g, b, 3);
    g->sfx_sent = g->sfx_queued;
    g->sfx_queued = 0xFF;
    g->sfx_priority = 0xFF;
}

/* Snd_Pause ($273C): wait for the mailbox (Z80_WaitMailboxSlow), then
 * command $0C with the byte `arg` and a 0. Game_CheckPause sends it, with
 * the pads, as a pause starts and as it ends: the driver's Cmd_Pause
 * silences everything on the first and waits for the second, ignoring the
 * byte. Also counted, and the byte kept, for the harnesses. */
void snd_pause(HzGame *g, u8 arg)
{
    u8 b[3] = { SND_CMD_PAUSE, arg, 0x00 };

    post(g, b, 3);
    g->snd_pause_sent++;
    g->snd_pause_arg = arg;
}
