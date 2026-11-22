#ifndef HZ_SND_H
#define HZ_SND_H

#include "../core/hz_types.h"

#include <stddef.h>

/* The Z80 sound driver (asm/hz.asm's Z80Driver), ported routine for routine
 * (each function names its routine and Z80 address).
 *
 * On the Mega Drive the driver is a program of its own: the 68k copies it
 * into Z80 RAM (Snd_Init), points its 32 KB bank window at SoundData
 * ($78000) and then only posts commands in a mailbox. The Z80 polls the
 * mailbox and the YM2612's timer flags; Timer B is the music's tick (the
 * song's tempo), Timer A the PSG envelopes', and the VDP's VBlank interrupt
 * the sound effects'. Everything it does reaches the chips as register
 * writes, which is all this module puts out: a sink gets each write with
 * the time it happened.
 *
 * Here the handlers run in no time at all, at the moment their event
 * happens: a timer's overflow, a frame's VBlank, a command. The timers are
 * modelled from the driver's own writes to $24-$27, as tools/z80run.py
 * models them. Within one moment the order is the Z80's: the VBlank first
 * (it interrupts the idle loop), then the mailbox, then Timer A, then
 * Timer B (Timers_Poll's order). A VBlank while the driver sits in
 * Cmd_Pause's wait is lost (interrupts are off); a timer's overflow then
 * stays flagged and is served once after the pause, as the Z80 would.
 *
 * What the zero-time model leaves out: on the Z80 a VBlank can come
 * between two music tracks' ticks (MusicTracks_Update enables interrupts
 * between them) or be lost while one track's tick holds them off for
 * longer than the VDP holds the line. Here a VBlank always comes between
 * whole handlers. tools/z80run.py --zero-time runs the original the same
 * way, which is what `make sound` compares against.
 *
 * Not ported: commands $05, $0B and $0D (DAC samples: no 68k caller; a
 * sample's speed is the Z80's instruction timing). They free the mailbox
 * and do nothing, as $06 (stop a sample) does on the Z80 when none
 * plays. */

#define SND_TRACK_COUNT   8          /* music: FM1-6, PSG tone 1-2 */
#define SND_SFX_COUNT     9          /* sfx: the same and the noise */
#define SND_BANK_SIZE     0x8000     /* the bank window, $8000-$FFFF */
#define SND_QUEUE         64         /* commands waiting for the mailbox */

#define SND_MASTER        53693175u  /* NTSC master clock */
#define SND_YM_SAMPLE     1008u      /* master clocks a YM sample */
#define SND_FRAME         896040u    /* master clocks a frame (VBlank) */

#define SND_PART_PSG      2          /* the sink's part for a PSG byte */

/* The mailbox's commands (CommandTable, $00DE), the arguments after each
 * as the 68k writes them. */
enum {
    SND_CMD_ACK           = 0x00,    /* frees the mailbox */
    SND_CMD_START_SONG    = 0x01,    /* repeat (0 once): z_song from the top */
    SND_CMD_PLAY_SFX      = 0x02,    /* sfx, SfxPriority value ($FF none) */
    SND_CMD_FADE_MUSIC    = 0x03,    /* VBlanks a step; 0 stops it now */
    SND_CMD_STOP_SFX      = 0x04,    /* every effect */
    SND_CMD_DAC_SAMPLE    = 0x05,    /* (no 68k caller) */
    SND_CMD_STOP_DAC      = 0x06,    /* Snd_StopIfBusy's: frees the mailbox */
    SND_CMD_SET_SONG      = 0x07,    /* z_song, big-endian */
    SND_CMD_SET_SFX_TABLE = 0x08,    /* z_sfx_table, big-endian */
    SND_CMD_PLAY_SONG     = 0x09,    /* song number, big-endian (plays once) */
    SND_CMD_SET_BANK      = 0x0A,    /* the bank's 68k address, big-endian */
    SND_CMD_DAC_HEADER    = 0x0B,    /* (no 68k caller) */
    SND_CMD_PAUSE         = 0x0C,    /* pause; the next one resumes */
    SND_CMD_DAC_NUMBER    = 0x0D     /* (no 68k caller) */
};

/* A track's record, $54 bytes on the Z80 (music at $1990, sfx at $1C30);
 * the fields at their offsets. Positions in the song data are Z80
 * addresses in the bank window. */
typedef struct {
    u8  priority;        /* +$00 sfx: priority value + 1; music: $FF while
                            an sfx has its channel (Track_IsMuted) */
    u16 order;           /* +$01 order list position */
    u16 loop;            /* +$03 order list loop point */
    u16 phrase;          /* +$05 phrase position */
    u8  repeat_sp;       /* +$07 repeat stack pointer, as an offset into
                            repeat_stack (the Z80 keeps its address) */
    u8  pitch_frac;      /* +$09 pitch, 1/32 semitone (Track_PitchSlide
                            holds 1/256 here for a moment) */
    u8  pitch_note;      /* +$0A pitch, the note */
    u8  target_frac;     /* +$0B a slide's target, 1/32 */
    u8  target_note;     /* +$0C */
    u16 slide_step;      /* +$0D a tick's slide, 1/256 semitone, signed */
    u8  repeats_open;    /* +$0F repeats open (3 at most) */
    u8  repeat_stack[9]; /* +$10 per repeat: passes left, phrase position */
    u8  noise;           /* +$1C $FF: a PSG track's volume to the noise */
    u8  instrument;      /* +$1D */
    u8  carriers;        /* +$1E b0-3 the operators the volume attenuates,
                            b7 keyed */
    u8  length_count;    /* +$1F ticks to the next note */
    u8  gate_count;      /* +$20 ticks to the note's key off */
    u8  algorithm;       /* +$21 $B0: feedback and algorithm */
    u8  detune;          /* +$22 1/32 semitone, signed */
    u8  slide_speed;     /* +$23 b0-6 speed, b7 only between tied notes */
    u8  wait;            /* +$24 Flag_Wait's count */
    u8  transpose;       /* +$25 0-$5F */
    u8  volume_offset;   /* +$26 */
    u8  fade_rate;       /* +$27 ticks a fade step */
    u8  fade_count;      /* +$28 */
    u8  fade_level;      /* +$29 an attenuation added to the volume */
    u8  volume;          /* +$2A 0-$63 */
    u8  flags;           /* +$2B b7 release bend, b6 bend down, b0 LFO on */
    u8  pan;             /* +$2C $B4: b7-6 left/right, b5-4 AMS, b2-0 FMS */
    u8  lfo_freq;        /* +$2D */
    u8  fms;             /* +$2E */
    u8  ams;             /* +$2F */
    u8  lfo_sweep;       /* +$30 */
    u8  lfo_delay;       /* +$31 */
    u8  note_length;     /* +$32 */
    u8  note_gate;       /* +$33 */
    u8  note_velocity;   /* +$34 */
    u8  env_speed;       /* +$35 envelope step every $64 - this ticks */
    u8  env_start_drop;  /* +$36 the attack starts this far below */
    u8  env_attack;      /* +$37 */
    u8  env_decay;       /* +$38 */
    u8  env_sustain_drop;/* +$39 the decay ends this far below */
    u8  env_sustain;     /* +$3A the sustain's fall */
    u8  env_release;     /* +$3B */
    u8  env_state;       /* +$3C $80 key on, $40 key off asked; $20 attack,
                            $10 decay, $08 sustain, $04 release */
    u8  env_count;       /* +$3D ticks to the next step */
    u8  env_peak;        /* +$3E the note's volume */
    u8  env_start;       /* +$3F */
    u8  env_floor;       /* +$40 */
    u8  env_level;       /* +$41 */
    u8  psg_volume;      /* +$42 the attenuation nibble last written */
    u8  lfo_fms_written; /* +$43 */
    u8  lfo_ams_written; /* +$44 */
    u16 pitch_written;   /* +$47 the pitch Track_SetFrequency saw */
    u8  level_asked;     /* +$4A Track_SetLevel's level */
    u16 order_start;     /* (the port's) the order list's start */
} HzSndTrack;

/* One chip write: part 0/1 the YM2612's halves, SND_PART_PSG a PSG byte
 * (reg 0). `t` is the time in master clocks. */
typedef void (*HzSndSink)(void *user, u64 t, u8 part, u8 reg, u8 value);

/* (Session 37, the port's own) Told at each key-on a music track makes on
 * an FM channel, just before its $28 write (session 38: the chip's or not,
 * a track an effect has muted too): the channel (0-5), the song
 * (Cmd_PlaySong's number), the track's instrument (+$1D) and where the
 * track is in its order list (the byte past the phrase it plays, from the
 * list's start: the same on every pass of a looping song), so the Q
 * backend can play its drum models for the drums (core/audio.c). The Z80
 * has no such thing; NULL, the default, tells nothing. */
typedef void (*HzSndNote)(void *user, u64 t, u8 channel, u8 song,
                          u8 instrument, u16 position);

/* (Session 38, the port's own) Told at each FM write a track makes --
 * those the chip gets (to_chip) and those the Z80 skips, a music track's
 * while an effect has its channel (Track_IsMuted: the track runs on, its
 * writes go nowhere) -- and, as part SND_PART_CLAIM (reg and value 0), when
 * an effect's track takes an FM channel. `track`: a music track 0-7,
 * SND_WRITER_SFX + an sfx track 0-8, or SND_WRITER_DRIVER (the driver's
 * own writes: the timers, the LFO's $22, a song's start). Just before the
 * write, if the chip gets it. For Q's VOICES EXTRA (core/audio.c): the
 * music voiced on under the effects. The Z80 has no such thing; NULL, the
 * default, tells nothing. */
#define SND_PART_CLAIM    3
#define SND_WRITER_SFX    0x10
#define SND_WRITER_DRIVER 0xFF
typedef void (*HzSndTrackWrite)(void *user, u64 t, u8 track, u8 part, u8 reg,
                                u8 value, bool to_chip);

typedef struct {
    u32 frame;           /* due from this frame (VBlank count) */
    u8  bytes[6];        /* the mailbox: command, then its arguments */
} HzSndCommand;

typedef struct {
    /* The machine around the driver. */
    const u8 *bank;      /* SoundData: the window's 32 KB */
    const u8 *song_windows;  /* (the port's) NULL, or a window per song,
                                song_window_count of them, when the songs
                                outgrow the one (docs/sound.md, Room):
                                Cmd_PlaySong turns `bank` to the song's */
    int       song_window_count;
    HzSndSink sink;
    void     *user;
    HzSndNote note;      /* (the port's) NULL, or see HzSndNote */
    HzSndTrackWrite track_write;    /* (the port's) NULL, or see above */
    const HzSndTrack *writer;       /* (the port's) the track whose writes
                                       these are, NULL the driver's */
    u8        song_id;   /* (the port's) the song Cmd_PlaySong last took */
    u64       now;       /* master clocks */
    u32       frame;     /* VBlanks so far */
    u64       next_frame;
    bool      one_command_a_frame;  /* tools/z80run.py's script: a command
                                       at a frame's start at most */
    u32       posted_frame;
    HzSndCommand queue[SND_QUEUE];
    int       q_head, q_count;

    /* The YM2612's timers as the driver's writes set them. */
    u16  ta;             /* $24/$25 */
    u8   tb;             /* $26 */
    u8   timer_ctrl;     /* $27 */
    u8   timer_flags;    /* b0 A, b1 B: the status Timers_Poll reads */
    bool timer_a_on, timer_b_on;
    u64  next_a, next_b;

    /* Driver state: Z80 RAM, named as in asm/hz.asm's Z80 driver. */
    bool paused;         /* in Cmd_Pause's WaitForUnpause */
    bool volume_unchecked;  /* the four patched write sites (Cmd_Pause):
                               Track_SetVolume's writes skip no track */
    u8   bank_addr[4];   /* z_bank_addr */
    u8   command;        /* z_command ($0003) */
    u8   arg[5];         /* z_arg0-z_arg4 */
    u8   marker;         /* z_marker */
    u8   music_on;       /* z_music_on */
    u8   sfx_on;         /* z_sfx_on */
    u8   sfx8_on;        /* z_sfx8_on */
    u8   song_repeat;    /* z_song_repeat */
    u8   channel;        /* z_channel */
    u8   lfo_channel;    /* z_lfo_channel */
    u8   lfo_channel_hi; /* z_lfo_channel_hi */
    u8   fade_speed;     /* z_fade_speed */
    u8   fade_count;     /* z_fade_count */
    u8   fade_level;     /* z_fade_level */
    u8   tempo_offset;   /* z_tempo_offset */
    u8   music_noise;    /* z_music_noise */
    u8   song_volume;    /* z_song_volume */
    u8   song_tempo;     /* z_song_tempo */
    u8   song_transpose; /* z_song_transpose */
    u8   lfo_reg;        /* z_lfo_reg */
    u8   lfo_reg_hi;     /* z_lfo_reg + 1 */
    u8   lfo_sweep;      /* z_lfo_sweep */
    u8   lfo_delay;      /* z_lfo_delay */
    u8   lfo_fms_sweep;  /* z_lfo_fms_sweep */
    u8   lfo_ams_sweep;  /* z_lfo_ams_sweep */
    u8   lfo_fms;        /* z_lfo_fms */
    u8   lfo_ams;        /* z_lfo_ams */
    u8   lfo_sweep_rate; /* z_lfo_sweep_rate */
    u8   lfo_out_fms;    /* z_lfo_out */
    u8   lfo_out_ams;    /* z_lfo_out + 1 */
    u8   lfo_spare;      /* z_lfo_spare */
    u8   lfo_sweep_count;/* z_lfo_sweep_count */
    u16  lfo_saved;      /* z_lfo_saved */
    u8   sfx_pass;       /* z_sfx_pass */
    u8   restoring;      /* z_restoring */
    u16  song;           /* z_song */
    u16  sfx_table;      /* z_sfx_table */
    HzSndTrack music[SND_TRACK_COUNT];  /* z_music_tracks */
    HzSndTrack sfx[SND_SFX_COUNT];      /* z_sfx_tracks */
    u8   psg_latch;      /* z_psg_latch */
    u8   fm_freq_reg;    /* z_fm_freq_reg */
    u8   key_ops;        /* z_key_ops */
} HzSnd;

/* The driver's tables (asm/hz.asm's Z80 driver), for the round trip too. */
extern const u16 SND_FM_FREQ_TABLE[12];        /* FMFreqTable */
extern const u8  SND_FM_FINE_TABLE[12][32];    /* FMFineTable */
extern const u16 SND_PSG_PERIOD_TABLE[12];     /* PSGPeriodTable */
extern const u8  SND_PSG_FINE_TABLE[12][32];   /* PSGFineTable */
extern const u8  SND_CARRIER_OPS[8];           /* CarrierOps (and its
                                                  copy CarrierOpsInit) */

/* Snd_Init's Z80 side: the driver's Init at time 0 (its writes go to the
 * sink). `bank` is SoundData, SND_BANK_SIZE bytes, kept by pointer. */
void hz_snd_init(HzSnd *s, const u8 *bank, HzSndSink sink, void *user);

/* (The port's) the songs' own windows (songtext_build's), `count` of
 * SND_BANK_SIZE bytes, kept by pointer; NULL: the one bank holds them all.
 * After hz_snd_init, before the first song. */
void hz_snd_set_song_windows(HzSnd *s, const u8 *windows, int count);

/* A command for the mailbox, due from VBlank `frame` (bytes: the command
 * and up to five arguments, as the 68k writes them). False: queue full. */
bool hz_snd_post(HzSnd *s, u32 frame, const u8 *bytes, int count);

/* Runs every event up to and including time `until` (master clocks). */
void hz_snd_run(HzSnd *s, u64 until);

#endif /* HZ_SND_H */
