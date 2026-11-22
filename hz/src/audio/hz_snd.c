#include "hz_snd.h"

#include <string.h>

/* The Z80 sound driver (asm/hz.asm's Z80Driver) as C: each function names
 * the routine it stands for and its Z80 address, the track being worked on
 * (the Z80's ix) is the HzSndTrack pointer, and z_channel stays a global
 * (s->channel), as the driver relies on it in places a parameter would
 * hide (fm_update_lfo, the quirk noted there). Positions in the song data
 * are Z80 addresses in the bank window ($8000-$FFFF), as the driver keeps
 * them. What is kept of the driver's oddities is marked QUIRK. See
 * hz_snd.h for the time model. */

/* The Z80 would spin for ever on some malformed data (a $FD in a phrase
 * with no repeat open, an order list with no phrase after its loop point).
 * The port gives up after this many passes instead of hanging the audio
 * thread; no shipping song or effect comes near it. */
#define HANG_GUARD 0x10000

const u16 SND_FM_FREQ_TABLE[12] = {                /* FMFreqTable ($150F) */
    0x284, 0x2AA, 0x2D3, 0x2FE, 0x32B, 0x35B,
    0x38E, 0x3C4, 0x3FE, 0x43B, 0x47B, 0x4BF
};

const u8 SND_FM_FINE_TABLE[12][32] = {             /* FMFineTable ($1527) */
    { 0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C,
      0x0D, 0x0E, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19,
      0x1A, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25 },
    { 0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0D,
      0x0E, 0x0F, 0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1B,
      0x1C, 0x1D, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x25, 0x26, 0x27 },
    { 0x00, 0x01, 0x03, 0x04, 0x05, 0x07, 0x08, 0x09, 0x0B, 0x0C, 0x0D,
      0x0F, 0x10, 0x11, 0x13, 0x14, 0x15, 0x17, 0x18, 0x1A, 0x1B, 0x1C,
      0x1E, 0x1F, 0x20, 0x22, 0x23, 0x24, 0x26, 0x27, 0x28, 0x2A },
    { 0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E,
      0x10, 0x11, 0x12, 0x14, 0x15, 0x17, 0x18, 0x1A, 0x1B, 0x1C, 0x1E,
      0x1F, 0x21, 0x22, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2B, 0x2C },
    { 0x00, 0x02, 0x03, 0x05, 0x06, 0x08, 0x09, 0x0B, 0x0C, 0x0E, 0x0F,
      0x11, 0x12, 0x14, 0x15, 0x17, 0x18, 0x1A, 0x1B, 0x1D, 0x1E, 0x20,
      0x21, 0x23, 0x24, 0x26, 0x27, 0x29, 0x2A, 0x2C, 0x2D, 0x2F },
    { 0x00, 0x02, 0x03, 0x05, 0x06, 0x08, 0x0A, 0x0B, 0x0D, 0x0E, 0x10,
      0x12, 0x13, 0x15, 0x16, 0x18, 0x1A, 0x1B, 0x1D, 0x1E, 0x20, 0x22,
      0x23, 0x25, 0x26, 0x28, 0x2A, 0x2B, 0x2D, 0x2E, 0x30, 0x31 },
    { 0x00, 0x02, 0x03, 0x05, 0x07, 0x08, 0x0A, 0x0C, 0x0E, 0x0F, 0x11,
      0x13, 0x14, 0x16, 0x18, 0x19, 0x1B, 0x1D, 0x1E, 0x20, 0x22, 0x24,
      0x25, 0x27, 0x29, 0x2A, 0x2C, 0x2E, 0x2F, 0x30, 0x33, 0x34 },
    { 0x00, 0x02, 0x04, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0E, 0x10, 0x12,
      0x14, 0x16, 0x17, 0x19, 0x1B, 0x1D, 0x1E, 0x20, 0x22, 0x24, 0x26,
      0x27, 0x29, 0x2B, 0x2D, 0x2F, 0x30, 0x32, 0x34, 0x36, 0x38 },
    { 0x00, 0x02, 0x04, 0x06, 0x08, 0x09, 0x0B, 0x0D, 0x0F, 0x11, 0x13,
      0x15, 0x17, 0x19, 0x1B, 0x1C, 0x1E, 0x20, 0x22, 0x24, 0x26, 0x28,
      0x2A, 0x2C, 0x2E, 0x2F, 0x31, 0x33, 0x35, 0x37, 0x39, 0x3B },
    { 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14,
      0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2A,
      0x2C, 0x2E, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E },
    { 0x00, 0x02, 0x04, 0x06, 0x09, 0x0B, 0x0D, 0x0F, 0x11, 0x13, 0x15,
      0x17, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2B, 0x2D,
      0x2F, 0x31, 0x33, 0x35, 0x37, 0x3A, 0x3C, 0x3E, 0x40, 0x42 },
    { 0x00, 0x02, 0x05, 0x07, 0x09, 0x0B, 0x0E, 0x10, 0x12, 0x14, 0x17,
      0x19, 0x1B, 0x1D, 0x20, 0x22, 0x24, 0x26, 0x29, 0x2B, 0x2D, 0x2F,
      0x32, 0x34, 0x36, 0x38, 0x3B, 0x3D, 0x3F, 0x41, 0x44, 0x46 }
};

const u16 SND_PSG_PERIOD_TABLE[12] = {             /* PSGPeriodTable ($16A7) */
    0x357, 0x327, 0x2FA, 0x2CF, 0x2A7, 0x281,
    0x25D, 0x23B, 0x21B, 0x1FC, 0x1E0, 0x1C5
};

const u8 SND_PSG_FINE_TABLE[12][32] = {            /* PSGFineTable ($16BF) */
    { 0x00, 0x02, 0x03, 0x05, 0x06, 0x08, 0x0A, 0x0B, 0x0D, 0x0E, 0x10,
      0x11, 0x13, 0x14, 0x16, 0x17, 0x19, 0x1B, 0x1C, 0x1E, 0x1F, 0x21,
      0x22, 0x23, 0x25, 0x26, 0x28, 0x29, 0x2B, 0x2C, 0x2E, 0x2F },
    { 0x00, 0x02, 0x03, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x0C, 0x0D, 0x0F,
      0x10, 0x12, 0x13, 0x15, 0x16, 0x17, 0x19, 0x1A, 0x1C, 0x1D, 0x1F,
      0x20, 0x21, 0x23, 0x24, 0x26, 0x27, 0x28, 0x2A, 0x2B, 0x2C },
    { 0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D, 0x0E,
      0x0F, 0x11, 0x12, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1A, 0x1C, 0x1D,
      0x1E, 0x20, 0x21, 0x22, 0x24, 0x25, 0x26, 0x27, 0x29, 0x2A },
    { 0x00, 0x01, 0x03, 0x04, 0x05, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D,
      0x0F, 0x10, 0x11, 0x12, 0x14, 0x15, 0x16, 0x17, 0x19, 0x1A, 0x1B,
      0x1C, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x26, 0x27 },
    { 0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0D,
      0x0E, 0x0F, 0x10, 0x11, 0x13, 0x14, 0x15, 0x16, 0x17, 0x19, 0x1A,
      0x1B, 0x1C, 0x1D, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25 },
    { 0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0B, 0x0C,
      0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
      0x19, 0x1A, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23 },
    { 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
      0x0C, 0x0D, 0x0E, 0x0F, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
      0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21 },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0B,
      0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
      0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1D, 0x1E, 0x1F },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
      0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x12, 0x13, 0x14,
      0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09,
      0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x10, 0x11, 0x12, 0x13,
      0x14, 0x15, 0x16, 0x17, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
      0x0A, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x10, 0x11, 0x12,
      0x13, 0x14, 0x15, 0x15, 0x16, 0x17, 0x18, 0x19, 0x19, 0x1A },
    { 0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x08,
      0x09, 0x0A, 0x0B, 0x0C, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x10, 0x11,
      0x12, 0x13, 0x13, 0x14, 0x15, 0x16, 0x17, 0x17, 0x18, 0x19 }
};

/* CarrierOps ($0839) and its copy CarrierOpsInit ($0737), the same eight
 * bytes: per algorithm, the operators the volume attenuates (b0 op 1 ...
 * b3 op 4 in the order the TLs are written: $40, $48, $44, $4C). */
const u8 SND_CARRIER_OPS[8] = {
    0x08, 0x08, 0x08, 0x08, 0x0A, 0x0E, 0x0E, 0x0F
};

/* --- names --------------------------------------------------------------- */

/* z_channel: 0-5 the YM's FM channels, 6-7 the PSG's tone channels 1-2,
 * 8 the noise (sfx track 8 only; the PSG's tone 3 serves its pitch). */
#define FM_CHANNELS       6
#define CH_PSG1           6
#define CH_NOISE          8

/* A track's flags (+$2B). */
#define TRACK_BEND_ON_RELEASE 0x80   /* Flag_BendUp/DownOnRelease */
#define TRACK_BEND_DOWN       0x40
#define TRACK_LFO             0x01   /* Flag_LFOOn (FM tracks only) */

/* +$1E: b0-3 the operators the volume attenuates, b7 keyed. */
#define CARRIERS_KEYED    0x80
#define CARRIERS_ALL      0x0F

/* +$23: b0-6 the slide's speed, b7 slide only into a tied note. A speed
 * counts from 100: Track_SetNote's step a tick is speed - 100 (1/8
 * semitone), a release bend's 100 - speed (1/64). */
#define SLIDE_TIED_ONLY   0x80
#define SLIDE_SPEED       0x7F
#define SLIDE_ZERO        0x64

/* +$3C, a PSG track's envelope: a key on or off asked (served at the next
 * Timer A tick), then the stage it is in; 0 idle. */
#define ENV_KEY_ON        0x80
#define ENV_KEY_OFF       0x40
#define ENV_ATTACK        0x20
#define ENV_DECAY         0x10
#define ENV_SUSTAIN       0x08
#define ENV_RELEASE       0x04
#define ENV_TICKS         0x64       /* a step every $64 - +$35 ticks */

/* +$00 of a music track while an effect has its channel (Track_IsMuted);
 * +$1C of a track on the noise. */
#define TRACK_MUTED       0xFF
#define TRACK_ON_NOISE    0xFF

/* z_music_noise: the music's noise mode kept (b7), and b3: the noise
 * track's volume goes to tone 3 too. */
#define NOISE_KEPT        0x80
#define NOISE_TONE3_TOO   0x08

/* z_lfo_reg: requests for the YM's $22 (b7 write it off, b6 on), the
 * frequency in b0-2. z_lfo_sweep b7: the LFO restarts at each key-on. */
#define LFO_WRITE_OFF     0x80
#define LFO_WRITE_ON      0x40
#define LFO_FREQ          0x07
#define LFO_ENABLE        0x08       /* $22's enable bit */
#define LFO_RESTART       0x80
#define LFO_NONE          0xFF       /* z_lfo_channel: no track has it */

/* z_sfx_pass: the music's tracks are being worked on, or the effects'
 * (SFX_PASS; while VBlank_Interrupt runs them, their channels' bits):
 * their tables, no fade. */
#define MUSIC_PASS        0x00
#define SFX_PASS          0xFF

/* z_restoring: what Tracks_HaltOrResume does. */
#define HALT              0x00
#define RESUME            0xFF

/* The pan byte ($B4): b7 left, b6 right, b4-5 AMS, b0-2 FMS. */
#define PAN_SPEAKERS      0xC0
#define PAN_LEFT          0x80
#define PAN_RIGHT         0x40

/* The YM2612's registers the driver writes. A channel's are + (its code &
 * 3) in its part; an operator's, + $0/$8/$4/$C for the instrument's four
 * bytes in turn. */
#define YM_LFO            0x22
#define YM_TIMER_A_HI     0x24
#define YM_TIMER_A_LO     0x25
#define YM_TIMER_B        0x26
#define YM_TIMERS         0x27
#define YM_KEY            0x28
#define YM_DT_MUL         0x30
#define YM_TL             0x40
#define YM_RS_AR          0x50
#define YM_AM_DR          0x60
#define YM_SR             0x70
#define YM_SL_RR          0x80
#define YM_FREQ_LO        0xA0
#define YM_FREQ_HI        0xA4
#define YM_ALGORITHM      0xB0
#define YM_PAN            0xB4

/* $27: load (start) each timer, let each flag its overflows, clear each
 * flag. */
#define TIMER_LOAD_A      0x01
#define TIMER_LOAD_B      0x02
#define TIMER_ENABLE_A    0x04
#define TIMER_ENABLE_B    0x08
#define TIMER_RESET_A     0x10
#define TIMER_RESET_B     0x20
#define TIMERS_RUN        (TIMER_LOAD_A | TIMER_LOAD_B | TIMER_ENABLE_A \
                           | TIMER_ENABLE_B)
/* The status Timers_Poll reads. */
#define TIMER_A_OVER      0x01
#define TIMER_B_OVER      0x02

/* $28: the key-on's operators (all four), on or off. */
#define KEY_ALL_ON        0xF0
#define KEY_OPS_ON        0xF3       /* z_key_ops: FM_KeyOnOff's base */
#define KEY_OPS_OFF       0x03

/* The PSG's bytes: a latch for tone n's frequency ($80 + n x $20), its
 * volume (+$10), the noise's control and volume. */
#define PSG_TONE_LATCH    0x80
#define PSG_VOLUME        0x10
#define PSG_TONE3_VOLUME  0xD0
#define PSG_NOISE_CTRL    0xE0
#define PSG_NOISE_VOLUME  0xF0

/* Volumes: 0-$63 loud to the top; attenuations, $7F silent. Notes 0-$5F. */
#define VOLUME_MAX        0x63
#define SILENT            0x7F
#define NOTES             0x60

/* The song's header (Z80 addresses from z_song) and the sfx table's (from
 * z_sfx_table): words are offsets from the header's start. */
#define SONG_ID           0x00       /* 9: a song */
#define SONG_VOLUME       0x01
#define SONG_TEMPO        0x02
#define SONG_TRANSPOSE    0x03
#define SONG_CHANNEL_VOL  0x04       /* a volume per channel */
#define SONG_TRACKS       0x06       /* 8 order lists */
#define SONG_PHRASES      0x08
#define SONG_FM_INSTR     0x0A       /* FM_INSTR bytes each */
#define SONG_PSG_INSTR    0x0C       /* PSG_INSTR bytes each */
#define SFX_TABLE_COUNT   0x01
#define SFX_TABLE_FM      0x02
#define SFX_TABLE_PSG     0x04
#define SFX_TABLE_SFX     0x08       /* an offset per effect */
#define FM_INSTR          36
#define PSG_INSTR         16

/* An FM instrument: +0 feedback and algorithm, +1 AMS/FMS, then per
 * operator DT/MUL, TL, RS/AR, AM/DR, SR, SL/RR; five bytes nothing reads;
 * the LFO's frequency and four on its sweep byte. */
#define FM_INSTR_TL       6
#define FM_INSTR_LFO      31

/* The sequence bytes: below $E0 a number (in a phrase $80 up a note, below
 * $80 a length, gate or velocity after it); $E0-$F7 flags (FlagTableE0's
 * with an argument, FlagTableF0's without); in an order list $FD marks
 * the loop point, in a phrase it ends a repeat; $FF ends either. */
#define SEQ_FLAGS         0xE0
#define SEQ_NOTE          0x80
#define SEQ_LOOP          0xFD
#define SEQ_END           0xFF

typedef enum {
    PARSE_NUMBER,        /* below $E0: the Z80's carry */
    PARSE_FLAG,          /* a flag run or skipped: the Z80's z */
    PARSE_OTHER          /* $FB-$FF: nz, the byte for the caller */
} ParseKind;

#define PHRASE_ENDED      0xFF

typedef struct {
    u8 note;             /* the note, 0-$5F; PHRASE_ENDED */
    u8 length;
    u8 gate;
    u8 velocity;
} SndNote;

static void track_set_volume(HzSnd *s, HzSndTrack *t, u8 volume);
static void track_set_instrument(HzSnd *s, HzSndTrack *t, u8 instrument);
static void track_set_frequency(HzSnd *s, HzSndTrack *t);
static void track_write_frequency(HzSnd *s, HzSndTrack *t);
static void track_lfo_on(HzSnd *s, HzSndTrack *t);
static void track_lfo_off(HzSnd *s, HzSndTrack *t);
static void ym_set_tempo(HzSnd *s, u8 tempo);
static ParseKind track_parse_command(HzSnd *s, HzSndTrack *t, u16 *pos,
                                     u8 *byte);

static bool is_fm(u8 channel)
{
    return channel < FM_CHANNELS;
}

/* z_music_on's / z_sfx_on's bit for channel 0-7 (the Z80 rotates it into
 * place). */
static u8 channel_bit(u8 channel)
{
    return (u8)(1u << channel);
}

/* --- the bank window and the chips ---------------------------------- */

/* A byte of Z80 RAM ($0000-$1FFF). The driver only ever reads its own
 * RAM through a bad pointer: before the first song z_song is Snd_Init's
 * $8000 (the song table), so restoring a music track that never started
 * (an effect ending) finds its instrument at $351C, in the RAM's mirror,
 * on the driver's tables; and Flag_Wait reads a cleared record's order
 * position, 0, where Init wrote $FF. Those bytes are served; the rest of
 * the image (code, the stack, the variables) reads as 0 and is never
 * reached. */
static u8 z80_ram(u16 addr)
{
    if (addr == 0x0000) {
        return 0xFF;                    /* Init's `ld (hl),$FF`, hl 0 */
    }
    if (addr >= 0x0737 && addr < 0x073F) {
        return SND_CARRIER_OPS[addr - 0x0737];         /* CarrierOpsInit */
    }
    if (addr >= 0x0839 && addr < 0x0841) {
        return SND_CARRIER_OPS[addr - 0x0839];         /* CarrierOps */
    }
    if (addr >= 0x150F && addr < 0x1527) {             /* FMFreqTable */
        u16 w = SND_FM_FREQ_TABLE[(addr - 0x150F) / 2];
        return (u8)((addr - 0x150F) & 1 ? w >> 8 : w);
    }
    if (addr >= 0x1527 && addr < 0x16A7) {             /* FMFineTable */
        return (&SND_FM_FINE_TABLE[0][0])[addr - 0x1527];
    }
    if (addr >= 0x16A7 && addr < 0x16BF) {             /* PSGPeriodTable */
        u16 w = SND_PSG_PERIOD_TABLE[(addr - 0x16A7) / 2];
        return (u8)((addr - 0x16A7) & 1 ? w >> 8 : w);
    }
    if (addr >= 0x16BF && addr < 0x183F) {             /* PSGFineTable */
        return (&SND_PSG_FINE_TABLE[0][0])[addr - 0x16BF];
    }
    return 0;
}

/* A byte at a Z80 address: the bank window (all song data), the YM (its
 * status at $4000), $6000 up write-only, RAM below $4000 (mirrored at
 * $2000). */
static u8 z80_read(const HzSnd *s, u16 addr)
{
    if (addr >= 0x8000) {
        return s->bank[addr - 0x8000];
    }
    if (addr >= 0x6000) {
        return 0xFF;
    }
    if (addr >= 0x4000) {
        return addr == 0x4000 ? s->timer_flags : 0;
    }
    return z80_ram((u16)(addr & 0x1FFF));
}

/* A little-endian word. */
static u16 z80_read16(const HzSnd *s, u16 addr)
{
    return (u16)(z80_read(s, addr) | z80_read(s, (u16)(addr + 1)) << 8);
}

/* A word of a header: the address it points at, from the header's start. */
static u16 header_ptr(const HzSnd *s, u16 header, u8 field)
{
    return (u16)(header + z80_read16(s, (u16)(header + field)));
}

static u64 timer_a_period(const HzSnd *s)
{
    return (u64)(1024 - s->ta) * SND_YM_SAMPLE;
}

static u64 timer_b_period(const HzSnd *s)
{
    return (u64)(256 - s->tb) * 16 * SND_YM_SAMPLE;
}

/* The YM2612's timers, from the writes to $24-$27 (as tools/z80run.py's
 * Chips): loading a timer starts it counting from the write; it runs free,
 * flagging each overflow while its flag is enabled. */
static void timers_write(HzSnd *s, u8 reg, u8 value)
{
    switch (reg) {
    case YM_TIMER_A_HI:
        s->ta = (u16)((s->ta & 3) | value << 2);
        break;
    case YM_TIMER_A_LO:
        s->ta = (u16)((s->ta & ~3u) | (value & 3));
        break;
    case YM_TIMER_B:
        s->tb = value;
        break;
    case YM_TIMERS:
        if ((value & TIMER_LOAD_A) && !(s->timer_ctrl & TIMER_LOAD_A)) {
            s->timer_a_on = true;
            s->next_a = s->now + timer_a_period(s);
        }
        if (!(value & TIMER_LOAD_A)) {
            s->timer_a_on = false;
        }
        if ((value & TIMER_LOAD_B) && !(s->timer_ctrl & TIMER_LOAD_B)) {
            s->timer_b_on = true;
            s->next_b = s->now + timer_b_period(s);
        }
        if (!(value & TIMER_LOAD_B)) {
            s->timer_b_on = false;
        }
        if (value & TIMER_RESET_A) {
            s->timer_flags &= (u8)~TIMER_A_OVER;
        }
        if (value & TIMER_RESET_B) {
            s->timer_flags &= (u8)~TIMER_B_OVER;
        }
        s->timer_ctrl = value;
        break;
    default:
        break;
    }
}

static void timers_advance(HzSnd *s, u64 t)
{
    while (s->timer_a_on && s->next_a <= t) {
        if (s->timer_ctrl & TIMER_ENABLE_A) {
            s->timer_flags |= TIMER_A_OVER;
        }
        s->next_a += timer_a_period(s);
    }
    while (s->timer_b_on && s->next_b <= t) {
        if (s->timer_ctrl & TIMER_ENABLE_B) {
            s->timer_flags |= TIMER_B_OVER;
        }
        s->next_b += timer_b_period(s);
    }
}

/* (the port's) The writing track as HzSndTrackWrite numbers it. */
static u8 writer_id(const HzSnd *s)
{
    const HzSndTrack *t = s->writer;
    if (t >= s->music && t < s->music + SND_TRACK_COUNT) {
        return (u8)(t - s->music);
    }
    if (t >= s->sfx && t < s->sfx + SND_SFX_COUNT) {
        return (u8)(SND_WRITER_SFX + (t - s->sfx));
    }
    return SND_WRITER_DRIVER;
}

/* (the port's) the hook told an effect's track (the writer) takes its FM
 * channel */
static void tell_claim(HzSnd *s)
{
    if (s->track_write != NULL) {
        s->track_write(s->user, s->now, writer_id(s), SND_PART_CLAIM, 0, 0,
                       false);
    }
}

/* (the port's) the track-write hook, for an FM write */
static void tell_track(HzSnd *s, u8 part, u8 reg, u8 value, bool to_chip)
{
    if (s->track_write != NULL && part != SND_PART_PSG) {
        s->track_write(s->user, s->now, writer_id(s), part, reg, value,
                       to_chip);
    }
}

static void chip_write(HzSnd *s, u8 part, u8 reg, u8 value)
{
    tell_track(s, part, reg, value, true);
    s->sink(s->user, s->now, part, reg, value);
    if (part == 0) {
        timers_write(s, reg, value);
    }
}

/* Track_IsMuted ($04BE): the byte at ix + 0 is $FF. ix is usually a track
 * (its priority: TRACK_MUTED on a music track an sfx has taken). QUIRK:
 * where the driver writes with ix left past the records, the byte is
 * whatever lies there: z_psg_latch past the sfx records (never $FF), sfx
 * track 1 past the music records. */
static bool is_muted(u8 owner)
{
    return owner == TRACK_MUTED;
}

static void ym_write0(HzSnd *s, u8 reg, u8 value)        /* YM_Write0 */
{
    chip_write(s, 0, reg, value);
}

static void ym_write1(HzSnd *s, u8 reg, u8 value)        /* YM_Write1 */
{
    chip_write(s, 1, reg, value);
}

static void ym_write0_checked(HzSnd *s, u8 owner, u8 reg, u8 value)
{
    if (!is_muted(owner)) {
        ym_write0(s, reg, value);
    } else {
        tell_track(s, 0, reg, value, false);
    }
}

/* YM_WriteChannel ($049C): part I for z_channel 0-2, part II from 3. */
static void ym_write_channel(HzSnd *s, u8 reg, u8 value)
{
    if (s->channel >= 3) {
        ym_write1(s, reg, value);
    } else {
        ym_write0(s, reg, value);
    }
}

static void ym_write_channel_checked(HzSnd *s, u8 owner, u8 reg, u8 value)
{
    if (!is_muted(owner)) {
        ym_write_channel(s, reg, value);
    } else {
        tell_track(s, (u8)(s->channel >= 3), reg, value, false);
    }
}

static void psg_write(HzSnd *s, u8 value)                /* PSG_Write */
{
    chip_write(s, SND_PART_PSG, 0, value);
}

static void psg_write_checked(HzSnd *s, u8 owner, u8 value)
{
    if (!is_muted(owner)) {
        psg_write(s, value);
    }
}

/* PSG_Write2Checked ($04B2): a tone's latch byte, then its data byte. */
static void psg_write2_checked(HzSnd *s, u8 owner, u8 latch, u8 data)
{
    if (!is_muted(owner)) {
        psg_write(s, latch);
        psg_write(s, data);
    }
}

/* The four patched sites (FM_WriteOperator.write, Track_SetVolume's
 * .psg_write, .noise_write1, .noise_write2): checked, or not while
 * Cmd_Pause has patched them. */
static void volume_write_fm(HzSnd *s, const HzSndTrack *t, u8 reg, u8 value)
{
    if (s->volume_unchecked) {
        ym_write_channel(s, reg, value);
    } else {
        ym_write_channel_checked(s, t->priority, reg, value);
    }
}

static void volume_write_psg(HzSnd *s, const HzSndTrack *t, u8 value)
{
    if (s->volume_unchecked) {
        psg_write(s, value);
    } else {
        psg_write_checked(s, t->priority, value);
    }
}

/* `sub 3; ccf; adc a,3`: the YM's channel code for z_channel 0-5
 * (0, 1, 2, 4, 5, 6), as $28 takes it; & 3 is the channel within its
 * part, which a channel's register adds. */
static u8 ym_channel_code(u8 channel)
{
    return (u8)(channel + (channel >= 3 ? 1 : 0));
}

static u8 ym_channel_reg(u8 base, u8 channel)
{
    return (u8)(base + (ym_channel_code(channel) & 3));
}

/* The order an operator's four bytes go to the YM: + $0, $8, $4, $C. */
static const u8 OPERATOR_REGS[4] = { 0x0, 0x8, 0x4, 0xC };

/* --- volumes and notes ---------------------------------------------- */

/* Volume_Add ($042F): two attenuations added, SILENT on a signed overflow
 * or a result with b7 set. */
static u8 volume_add(u8 att, u8 more)
{
    u8 sum = (u8)(att + more);

    if ((~(att ^ more) & (att ^ sum) & 0x80) != 0) {
        return SILENT;
    }
    if (sum & 0x80) {
        return SILENT;
    }
    return sum;
}

/* Note_Add ($11A8): two notes added within 0-$5F. */
static u8 note_add(u8 note, u8 more)
{
    unsigned sum = (unsigned)note + more;
    u8 low = (u8)sum;

    if (sum > 0xFF) {
        return (u8)(low + NOTES);
    }
    if (low < NOTES) {
        return low;
    }
    return (u8)(low - NOTES);
}

/* Volume_Combine ($11B4): two volumes 0-$63 as their attenuations added. */
static u8 volume_combine(u8 volume, u8 other)
{
    unsigned att = (unsigned)(u8)(VOLUME_MAX - volume)
                 + (u8)(VOLUME_MAX - other);
    u8 sum;

    if (att > 0xFF) {
        sum = 0;
    } else {
        sum = (u8)att;
        if (sum > VOLUME_MAX) {
            sum = VOLUME_MAX;
        }
    }
    return (u8)(VOLUME_MAX - sum);
}

/* Volume_Clamp ($11D2): a volume plus an offset within 0-$63. */
static u8 volume_clamp(u8 volume, u8 offset)
{
    u8 sum = (u8)(volume + offset);

    if (sum & 0x80) {
        return 0;
    }
    if (sum > VOLUME_MAX) {
        return VOLUME_MAX;
    }
    return sum;
}

/* Instrument_Find ($04EF): FM instrument `index`, from the song's table
 * or, on the sfx pass, the sfx table's. */
static u16 fm_instrument(const HzSnd *s, u8 index)
{
    u16 table = s->sfx_pass != 0 ? header_ptr(s, s->sfx_table, SFX_TABLE_FM)
                                 : header_ptr(s, s->song, SONG_FM_INSTR);

    return (u16)(table + index * FM_INSTR);
}

/* The PSG's volume latch for this track: tone 1 or 2's, or the noise's
 * (a tone track on the noise, +$1C), or for the noise channel tone 3's
 * (TRACK_ON_NOISE clear) or the noise's. */
static u8 psg_volume_latch(const HzSnd *s, const HzSndTrack *t)
{
    if (s->channel == CH_NOISE) {
        return t->noise == 0 ? PSG_TONE3_VOLUME : PSG_NOISE_VOLUME;
    }
    if (t->noise != 0) {
        return PSG_NOISE_VOLUME;
    }
    return (u8)(PSG_TONE_LATCH | PSG_VOLUME | (s->channel - CH_PSG1) << 5);
}

/* Track_SetVolume ($0371): a volume 0-$63 (0 silent) as an attenuation
 * $63 - volume, plus the track's fade (+$29) and, for music, z_fade_level.
 * FM: the TLs of the operators +$1E picks (the instrument's TL bytes
 * attenuated, the rest written as they are); PSG: the volume nibble, when
 * it changed (+$42). The noise track while the music's noise mode has
 * NOISE_TONE3_TOO writes it to tone 3 as well. */
static void track_set_volume(HzSnd *s, HzSndTrack *t, u8 volume)
{
    u8 att = SILENT;

    if (volume != 0) {
        att = volume_add((u8)(VOLUME_MAX - volume), t->fade_level);
        if (s->sfx_pass == 0) {
            att = volume_add(s->fade_level, att);
        }
    }

    u8 ch = s->channel;
    if (is_fm(ch)) {                                     /* FM_WriteOperator */
        u16 tl = (u16)(fm_instrument(s, t->instrument) + FM_INSTR_TL);
        u8 carriers = t->carriers;

        for (int op = 0; op < 4; op++) {
            u8 level = z80_read(s, (u16)(tl + op));
            if (carriers & 1) {
                level = volume_add(level, att);
            }
            carriers >>= 1;
            volume_write_fm(s, t, ym_channel_reg((u8)(YM_TL
                                                      + OPERATOR_REGS[op]),
                                                 ch),
                            level);
        }
        return;
    }

    u8 nibble = (u8)(att >> 2);
    if (nibble > 0x0F) {
        nibble = 0x0F;
    }
    if (nibble == t->psg_volume) {
        return;
    }
    t->psg_volume = nibble;
    if (ch == CH_NOISE && t->noise != 0
        && (s->music_noise & NOISE_TONE3_TOO)) {
        volume_write_psg(s, t, (u8)(nibble | PSG_TONE3_VOLUME));
        volume_write_psg(s, t, (u8)(nibble | PSG_NOISE_VOLUME));
        return;
    }
    volume_write_psg(s, t, (u8)(nibble | psg_volume_latch(s, t)));
}

/* Track_SetVolumeNow ($0860): +$2A = the volume, Track_SetVolume. */
static void track_set_volume_now(HzSnd *s, HzSndTrack *t, u8 volume)
{
    t->volume = volume;
    track_set_volume(s, t, volume);
}

/* Track_SetLevel ($0841): the level kept in +$4A, then set. QUIRK: it is
 * skipped while a PSG track's LFO, with an AMS, has no delay left, which
 * never happens: TRACK_LFO is set on FM tracks only. */
static void track_set_level(HzSnd *s, HzSndTrack *t, u8 level)
{
    t->level_asked = level;
    if ((t->flags & TRACK_LFO) && s->lfo_ams != 0 && !is_fm(s->channel)
        && s->lfo_delay == 0) {
        return;
    }
    track_set_volume_now(s, t, t->level_asked);
}

/* Whether the last note is still held: the gate counting down from 1-$80
 * (the Z80 tests `neg`'s sign, so a gate above $80 counts as let go). */
static bool note_held(const HzSndTrack *t)
{
    return ((u8)(0 - t->gate_count) & 0x80) != 0;
}

/* Track_NoteOff ($0519): a release bend sets its slide (+$0D: a step from
 * +$23's speed, towards the top note, $5F and $FC, or 0 with
 * TRACK_BEND_DOWN); unkeyed; FM: $28 <- the channel (all operators off);
 * PSG: the envelope's key off asked unless it is already, or released. */
static void track_note_off(HzSnd *s, HzSndTrack *t)
{
    if (t->flags & TRACK_BEND_ON_RELEASE) {
        u16 step = (u16)((u8)(SLIDE_ZERO - (t->slide_speed & SLIDE_SPEED))
                         << 2);
        u8 note = 0x5F;
        u8 frac = 0xFC;

        if (t->flags & TRACK_BEND_DOWN) {
            step = (u16)(0 - step);
            note = 0;
            frac = 0;
        }
        t->slide_step = step;
        t->target_frac = frac;
        t->target_note = note;
    }
    t->carriers &= (u8)~CARRIERS_KEYED;
    if (is_fm(s->channel)) {
        ym_write0_checked(s, t->priority, YM_KEY,
                          ym_channel_code(s->channel));
        return;
    }
    if (t->env_state & (ENV_KEY_OFF | ENV_RELEASE)) {
        return;
    }
    t->env_state = ENV_KEY_OFF;
}

/* Track_KeyOn ($0574): on the LFO's channel the LFO restarts (if its sweep
 * byte has LFO_RESTART) and its delay and sweep are set up; keyed; FM:
 * $28 <- $F0 + the channel; PSG: the envelope's key on asked. */
static void track_key_on(HzSnd *s, HzSndTrack *t)
{
    u8 ch = s->channel;

    if (s->lfo_channel == ch) {
        if (s->lfo_sweep & LFO_RESTART) {
            s->lfo_spare = 0;
            s->lfo_reg |= LFO_WRITE_OFF | LFO_WRITE_ON;
            s->lfo_reg_hi |= LFO_WRITE_OFF | LFO_WRITE_ON;
        }
        /* with a delay the sweep starts from nothing, a step every
         * -sweep & $7F ticks; without, the LFO is on at once */
        u8 rate = t->lfo_delay;
        u8 fms = s->lfo_fms;
        u8 ams = s->lfo_ams;

        s->lfo_delay = t->lfo_delay;
        if (t->lfo_delay != 0) {
            rate = (u8)((u8)(0 - s->lfo_sweep) & 0x7F);
            fms = 0;
            ams = 0;
        }
        s->lfo_sweep_rate = rate;
        s->lfo_sweep_count = 1;
        s->lfo_fms_sweep = fms;
        s->lfo_ams_sweep = ams;
        s->lfo_out_fms = fms;
        s->lfo_out_ams = ams;
    }
    t->carriers |= CARRIERS_KEYED;
    if (is_fm(ch)) {
        if (s->note != NULL
            && t >= s->music && t < s->music + SND_TRACK_COUNT) {
            s->note(s->user, s->now, ch, s->song_id, t->instrument,
                    (u16)(t->order - t->order_start));
        }
        ym_write0_checked(s, t->priority, YM_KEY,
                          (u8)(KEY_ALL_ON + ym_channel_code(ch)));
        return;
    }
    t->env_state = ENV_KEY_ON;
}

/* FM_KeyOnOff ($0261): $28 <- z_key_ops (KEY_OPS_ON or _OFF) for the
 * channel. */
static void fm_key_on_off(HzSnd *s)
{
    u8 ch = s->channel;

    if (!is_fm(ch)) {
        return;
    }
    ym_write0(s, YM_KEY, (u8)(s->key_ops - 3 + ym_channel_code(ch)));
}

/* FM_WriteAlgorithm ($05D0): $B0 + channel <- +$21. */
static void fm_write_algorithm(HzSnd *s, HzSndTrack *t)
{
    ym_write_channel_checked(s, t->priority,
                             ym_channel_reg(YM_ALGORITHM, s->channel),
                             t->algorithm);
}

/* FM_WritePan ($05E1): $B4 + channel <- +$2C. QUIRK: only its speaker
 * bits while +$2B is 0: any flag, a release bend's too, lets the AMS/FMS
 * through. */
static void fm_write_pan(HzSnd *s, HzSndTrack *t)
{
    u8 ch = s->channel;

    if (!is_fm(ch)) {
        return;
    }
    u8 pan = t->pan;

    if (t->flags == 0) {
        pan &= PAN_SPEAKERS;
    }
    ym_write_channel_checked(s, t->priority, ym_channel_reg(YM_PAN, ch),
                             pan);
}

/* LFO_TakeTrack ($05FF): the YM's LFO from this track. */
static void lfo_take_track(HzSnd *s, HzSndTrack *t)
{
    s->lfo_reg = (u8)(t->lfo_freq | LFO_WRITE_ON);
    s->lfo_sweep = t->lfo_sweep;
    s->lfo_fms = t->fms;
    s->lfo_ams = t->ams;
}

/* PSG_SetNoise ($061A): on a PSG channel, the noise's control byte, mode &
 * 7 (a music track's mode also kept, with NOISE_KEPT, in z_music_noise).
 * `owner` is the byte at ix + 0. */
static void psg_set_noise(HzSnd *s, u8 owner, u8 mode)
{
    if (is_fm(s->channel)) {
        return;
    }
    if (s->sfx_pass == 0) {
        s->music_noise = (u8)(mode | NOISE_KEPT);
    }
    psg_write_checked(s, owner, (u8)((mode & 7) | PSG_NOISE_CTRL));
}

/* --- instruments ---------------------------------------------------- */

/* FM_WriteOperators ($071F, and its copy FM_WriteOperators2 at $0821):
 * one register for each operator, from `*data` on. */
static void fm_write_operators(HzSnd *s, HzSndTrack *t, u8 base, u16 *data)
{
    for (int op = 0; op < 4; op++) {
        u8 value = z80_read(s, *data);

        (*data)++;
        ym_write_channel_checked(s, t->priority,
                                 ym_channel_reg((u8)(base
                                                     + OPERATOR_REGS[op]),
                                                s->channel),
                                 value);
    }
}

/* The LFO's bytes of an instrument: +$2D its frequency, +$30 the sweep
 * byte four on; on the LFO's own channel with TRACK_LFO, LFO_TakeTrack. */
static void track_lfo_bytes(HzSnd *s, HzSndTrack *t, u16 data)
{
    t->lfo_freq = (u8)(z80_read(s, data) & LFO_FREQ);
    t->lfo_sweep = z80_read(s, (u16)(data + 4));

    u8 ch = s->channel;
    if (!is_fm(ch)) {
        return;
    }
    if (s->lfo_channel != ch) {
        return;
    }
    if (t->flags & TRACK_LFO) {
        lfo_take_track(s, t);
    }
}

/* The AMS/FMS byte: +$2E the FMS (b4-6), +$2F the AMS (b0-1). */
static void track_ams_fms(HzSndTrack *t, u8 byte)
{
    t->fms = (u8)((byte & 0x70) >> 4);
    t->ams = (u8)(byte & 0x03);
}

/* The FM instrument's part after its first byte, the same in
 * Track_InitInstrument and Track_SetInstrument: $B0, the AMS/FMS, the pan,
 * DT/MUL, the TLs skipped (Track_SetVolume writes them), RS/AR to SL/RR,
 * the LFO's bytes. */
static void fm_instrument_rest(HzSnd *s, HzSndTrack *t, u16 data)
{
    static const u8 after_tl[4] = { YM_RS_AR, YM_AM_DR, YM_SR, YM_SL_RR };

    fm_write_algorithm(s, t);
    track_ams_fms(t, z80_read(s, data));
    data++;
    t->pan &= PAN_SPEAKERS;
    fm_write_pan(s, t);

    fm_write_operators(s, t, YM_DT_MUL, &data);
    data = (u16)(data + 4);
    for (int i = 0; i < 4; i++) {
        fm_write_operators(s, t, after_tl[i], &data);
    }
    data = (u16)(data + 5);
    track_lfo_bytes(s, t, data);
}

/* The PSG instrument (PSG_INSTR bytes, from the song's table or the sfx
 * table's): the envelope to +$35-$3B, the AMS/FMS byte, the LFO's
 * bytes. */
static void psg_instrument(HzSnd *s, HzSndTrack *t, u8 index)
{
    u16 table = s->sfx_pass != 0 ? header_ptr(s, s->sfx_table, SFX_TABLE_PSG)
                                 : header_ptr(s, s->song, SONG_PSG_INSTR);
    u16 data = (u16)(table + index * PSG_INSTR);
    u8 *envelope[7] = {
        &t->env_speed, &t->env_start_drop, &t->env_attack, &t->env_decay,
        &t->env_sustain_drop, &t->env_sustain, &t->env_release
    };

    for (int i = 0; i < 7; i++) {
        *envelope[i] = z80_read(s, data);
        data++;
    }
    track_ams_fms(t, z80_read(s, data));
    data++;
    track_lfo_bytes(s, t, data);
}

/* An FM instrument's first byte: +$21 its feedback and algorithm, and the
 * carriers its algorithm attenuates. */
static u16 fm_instrument_start(HzSnd *s, HzSndTrack *t, u8 index)
{
    u16 data = fm_instrument(s, index);

    t->algorithm = (u8)(z80_read(s, data) & 0x3F);
    t->carriers = SND_CARRIER_OPS[t->algorithm & 7];
    return (u16)(data + 1);
}

/* Track_InitInstrument ($0635): Track_Init's. FM: every operator a
 * carrier, note off, volume 0, then the instrument (its carriers from
 * CarrierOpsInit); PSG: volume 0, the instrument. */
static void track_init_instrument(HzSnd *s, HzSndTrack *t, u8 index)
{
    if (!is_fm(s->channel)) {
        track_set_volume_now(s, t, 0);
        psg_instrument(s, t, index);
        return;
    }
    t->carriers = CARRIERS_ALL;
    track_note_off(s, t);
    track_set_volume_now(s, t, 0);
    fm_instrument_rest(s, t, fm_instrument_start(s, t, index));
}

/* Track_SetInstrument ($073F): note off; FM: the instrument (carriers from
 * CarrierOps), level 0 before $B0; PSG: the instrument. */
static void track_set_instrument(HzSnd *s, HzSndTrack *t, u8 index)
{
    track_note_off(s, t);
    if (!is_fm(s->channel)) {
        psg_instrument(s, t, index);
        return;
    }
    u16 data = fm_instrument_start(s, t, index);

    track_set_level(s, t, 0);
    fm_instrument_rest(s, t, data);
}

/* --- pitch ---------------------------------------------------------- */

/* Track_SetNote ($0869): the target pitch (+$0B/+$0C) = the note with the
 * detune (+$22, 1/32 semitone, signed); a slide towards it from the
 * current pitch (+$0D, speed +$23) unless the speed is 0, a release bend
 * is on (the pitch jumps there, the bend's step kept), or the slide is
 * for tied notes only and the last note isn't held; otherwise the pitch
 * jumps there. QUIRK: from a speed of 100 the step is 0 or the wrong way:
 * at 100 the pitch stays where it was until the next note, above it the
 * first tick passes the target and stops there (track_pitch_slide). */
static void track_set_note(HzSnd *s, HzSndTrack *t, u8 note)
{
    (void)s;
    u16 target = (u16)(note * 32 + (s8)t->detune);

    t->target_note = (u8)(target >> 5);
    t->target_frac = (u8)(target & 0x1F);

    u8 speed = (u8)(t->slide_speed & SLIDE_SPEED);
    bool slide;

    if (speed == 0) {
        slide = false;
    } else if (t->flags & TRACK_BEND_ON_RELEASE) {
        t->pitch_frac = t->target_frac;
        t->pitch_note = t->target_note;
        return;
    } else if (note_held(t)) {
        slide = true;
    } else {
        slide = !(t->slide_speed & SLIDE_TIED_ONLY);
    }

    if (slide) {
        /* speed - 100, up when the target is above */
        u8 step = (u8)(speed - SLIDE_ZERO);
        u16 to = (u16)(t->target_note << 8 | t->target_frac);
        u16 from = (u16)(t->pitch_note << 8 | t->pitch_frac);

        if (to >= from) {
            step = (u8)(0 - step);
        }
        t->slide_step = (u16)((s16)(s8)step * 32);
        return;
    }
    t->slide_step = 0;
    t->pitch_frac = t->target_frac;
    t->pitch_note = t->target_note;
}

/* Track_WriteFrequency ($0916): the pitch (+$0A the note, +$09 1/32).
 * FM: $A4 then $A0 + channel, FMFreqTable + FMFineTable with the octave as
 * the block; PSG: PSGPeriodTable - PSGFineTable, halved per octave above
 * 2, as a latch and a data byte. */
static void track_write_frequency(HzSnd *s, HzSndTrack *t)
{
    u8 octave = (u8)(t->pitch_note / 12);
    u8 semitone = (u8)(t->pitch_note % 12);
    u8 frac = (u8)(t->pitch_frac & 0x1F);
    u8 ch = s->channel;

    if (!is_fm(ch)) {
        s->psg_latch = (u8)(PSG_TONE_LATCH | (ch - CH_PSG1) << 5);
        u16 period = (u16)(SND_PSG_PERIOD_TABLE[semitone]
                           - SND_PSG_FINE_TABLE[semitone][frac]);
        if (octave > 2) {
            period = (u16)(period >> (octave - 2));
        }
        psg_write2_checked(s, t->priority,
                           (u8)(s->psg_latch | (period & 0x0F)),
                           (u8)((period >> 4) & 0x3F));
        return;
    }
    s->fm_freq_reg = ym_channel_reg(YM_FREQ_HI, ch);
    u16 fnum = (u16)(SND_FM_FREQ_TABLE[semitone]
                     + SND_FM_FINE_TABLE[semitone][frac]);
    ym_write_channel_checked(s, t->priority, s->fm_freq_reg,
                             (u8)((fnum >> 8) | (u8)(octave << 3)));
    ym_write_channel_checked(s, t->priority, (u8)(s->fm_freq_reg - 4),
                             (u8)(fnum & 0xFF));
}

/* Track_SetFrequency ($08EF): the pitch kept in +$47, then
 * Track_WriteFrequency. QUIRK: skipped like Track_SetLevel, which never
 * happens. */
static void track_set_frequency(HzSnd *s, HzSndTrack *t)
{
    t->pitch_written = (u16)(t->pitch_note << 8 | t->pitch_frac);
    if ((t->flags & TRACK_LFO) && s->lfo_fms != 0 && !is_fm(s->channel)
        && s->lfo_delay == 0) {
        return;
    }
    track_write_frequency(s, t);
}

/* Track_PitchSlide ($0A31): +$0D (1/256 semitone) added to the pitch until
 * it reaches or passes the target, then Track_SetFrequency. */
static void track_pitch_slide(HzSnd *s, HzSndTrack *t)
{
    u16 step = t->slide_step;

    if (step == 0) {
        return;
    }
    u16 pitch = (u16)(t->pitch_note << 8 | (u8)(t->pitch_frac << 3));
    pitch = (u16)(pitch + step);
    t->pitch_frac = (u8)(pitch & 0xFF);
    t->pitch_note = (u8)(pitch >> 8);

    u16 target = (u16)(t->target_note << 8 | (u8)(t->target_frac << 3));
    u16 past = (u16)(pitch - target);

    if ((((past >> 8) ^ (step >> 8)) & 0x80) == 0) {     /* there or past */
        t->pitch_frac = (u8)(target & 0xFF);
        t->pitch_note = (u8)(target >> 8);
        t->slide_step = 0;
    }
    t->pitch_frac = (u8)((t->pitch_frac >> 3) & 0x1F);
    track_set_frequency(s, t);
}

/* LFO_Sweep ($09E6): on the LFO's channel, each music tick: after
 * z_lfo_delay ticks, every z_lfo_sweep_rate ticks the FMS and AMS sweep
 * one up towards the track's; once both are there the sweep ends. QUIRK:
 * only z_lfo_fms_sweep/ams_sweep move and nothing writes them out
 * (z_lfo_out is Track_KeyOn's), so a delayed LFO stays off. */
static void lfo_sweep(HzSnd *s)
{
    if (s->lfo_sweep_rate == 0) {
        return;
    }
    if (s->lfo_delay != 0) {
        s->lfo_delay--;
        return;
    }
    s->lfo_sweep_count--;
    if (s->lfo_sweep_count != 0) {
        return;
    }
    int done = 0;
    u8 fms = (u8)(s->lfo_fms_sweep + 1);

    if (fms >= s->lfo_fms) {
        fms = s->lfo_fms;
        done++;
    }
    s->lfo_fms_sweep = fms;
    u8 ams = (u8)(s->lfo_ams_sweep + 1);
    if (ams >= s->lfo_ams) {
        ams = s->lfo_ams;
        done++;
    }
    s->lfo_ams_sweep = ams;
    if (done == 2) {
        s->lfo_sweep_rate = 0;
        return;
    }
    s->lfo_sweep_count = s->lfo_sweep_rate;
}

/* Track_VolumeFade ($11DE): every +$27 ticks +$29 one down. QUIRK: a
 * result with b7 set goes two up instead, so from 0 it would alternate 1
 * and 0 (the ticks call it only above 0), and from $81 up it climbs. */
static void track_volume_fade(HzSnd *s, HzSndTrack *t)
{
    (void)s;
    t->fade_count--;
    if (t->fade_count != 0) {
        return;
    }
    u8 level = (u8)(t->fade_level - 1);
    if (level & 0x80) {
        level = (u8)(level + 2);
    }
    t->fade_level = level;
    t->fade_count = t->fade_rate;
}

/* --- the sequencer -------------------------------------------------- */

/* Music_ClearChannelBit ($0EA7) / Music_SetChannelBit ($0EBA). */
static void music_clear_channel_bit(HzSnd *s)
{
    s->music_on &= (u8)~channel_bit(s->channel);
}

static void music_set_channel_bit(HzSnd *s)
{
    s->music_on |= channel_bit(s->channel);
}

/* Track_ClearLoops ($0E64): no repeat open, the stack empty. */
static void track_clear_loops(HzSndTrack *t)
{
    t->repeats_open = 0;
    t->repeat_sp = 0;
}

/* Track_SetLoopPoint ($12A5): $FD in an order list: the next entry. */
static void track_set_loop_point(HzSndTrack *t)
{
    t->loop = (u16)(t->order + 1);
}

/* Track_Init ($0E27): a PSG track on the noise first silenced; the record
 * cleared but +$00; the order list at `order`, repeats cleared, both
 * speakers, instrument 0, counters 1 (+$1F length, +$31 LFO delay). */
static void track_init(HzSnd *s, HzSndTrack *t, u16 order)
{
    if (t->noise != 0) {
        track_set_volume_now(s, t, 0);
    }
    u8 priority = t->priority;
    memset(t, 0, sizeof *t);
    t->priority = priority;
    track_clear_loops(t);
    t->order = order;
    t->order_start = order;
    t->pan = PAN_SPEAKERS;
    track_init_instrument(s, t, 0);
    t->length_count = 1;
    t->lfo_delay = 1;
}

/* Phrase_RepeatEnd ($127D): the innermost repeat (on the stack: passes
 * left, then where its $EC n is) one pass less; not done: the phrase goes
 * back past its $EC n (false); done: closed (true, the Z80's carry). No
 * repeat open: false, the phrase position as it is. */
static bool phrase_repeat_end(HzSndTrack *t)
{
    if (t->repeats_open == 0) {
        return false;
    }
    u8 *top = &t->repeat_stack[t->repeat_sp - 3];

    top[0]--;
    if (top[0] == 0) {
        t->repeat_sp = (u8)(t->repeat_sp - 3);
        t->repeats_open--;
        return true;
    }
    t->phrase = (u16)((top[2] << 8 | top[1]) + 2);
    return false;
}

/* Track_NextPhrase ($0ECD): the order list from +$01: flags run, $FD marks
 * the loop point, $FF ends the track (its bit cleared, note off: SEQ_END);
 * a number picks the song's phrase into +$05. Returns the Z80's a: SEQ_END
 * at the end, else the low byte of the song's phrase-table offset. QUIRK:
 * a song whose offset ends in $FF would read as ended. */
static u8 track_next_phrase(HzSnd *s, HzSndTrack *t)
{
    u16 pos = t->order;

    for (int guard = 0; guard < HANG_GUARD; guard++) {
        u8 byte;
        ParseKind kind = track_parse_command(s, t, &pos, &byte);

        if (kind == PARSE_NUMBER) {
            u16 table = header_ptr(s, s->song, SONG_PHRASES);
            t->phrase = (u16)(s->song + z80_read16(s, (u16)(table
                                                            + 2 * byte)));
            t->order = (u16)(pos + 1);
            return z80_read(s, (u16)(s->song + SONG_PHRASES));
        }
        if (kind == PARSE_FLAG) {
            t->order = pos;
            continue;
        }
        if (byte == SEQ_LOOP) {
            track_set_loop_point(t);
        } else if (byte == SEQ_END) {
            music_clear_channel_bit(s);
            track_note_off(s, t);
            return SEQ_END;
        }
        pos++;
        t->order = pos;
    }
    return SEQ_END;
}

/* SfxTrack_SetPhrase ($0F2A): past the flags (run), a big-endian offset
 * from z_sfx_table into +$05; *pos left past it. */
static void sfx_track_set_phrase(HzSnd *s, HzSndTrack *t, u16 *pos)
{
    u8 high = 0;

    for (int guard = 0; guard < HANG_GUARD; guard++) {
        ParseKind kind = track_parse_command(s, t, pos, &high);

        if (kind == PARSE_NUMBER) {
            break;
        }
        if (kind == PARSE_OTHER) {
            (*pos)++;
        }
    }
    (*pos)++;
    u8 low = z80_read(s, *pos);
    (*pos)++;
    t->phrase = (u16)(s->sfx_table + (u16)(high << 8 | low));
}

/* Phrase_NextNote ($0F49): the phrase from +$05: flags run, $FD closes a
 * repeat, $FF ends the phrase (PHRASE_ENDED, the repeats cleared); a note
 * $80-$DF, with its length, gate and velocity (+$32-$34) changed by up to
 * three bytes under $80 after it, in that order. QUIRK: a number under $80
 * there plays as a note $80 lower, and $7F reads as the phrase's end (its
 * repeats left open). */
static SndNote phrase_next_note(HzSnd *s, HzSndTrack *t)
{
    SndNote n = { PHRASE_ENDED, 0, 0, 0 };
    u16 pos = t->phrase;

    for (int guard = 0; guard < HANG_GUARD; guard++) {
        u8 byte;
        ParseKind kind = track_parse_command(s, t, &pos, &byte);

        if (kind == PARSE_NUMBER) {
            u8 *sticky[3] = { &t->note_length, &t->note_gate,
                              &t->note_velocity };

            n.note = (u8)(byte - SEQ_NOTE);
            pos++;
            for (int i = 0; i < 3; i++) {
                u8 v = z80_read(s, pos);
                if (v & SEQ_NOTE) {
                    break;
                }
                pos++;
                *sticky[i] = v;
            }
            n.length = t->note_length;
            n.gate = t->note_gate;
            n.velocity = t->note_velocity;
            t->phrase = pos;
            return n;
        }
        if (kind == PARSE_FLAG) {
            t->phrase = pos;
            continue;
        }
        if (byte == SEQ_LOOP) {
            if (!phrase_repeat_end(t)) {
                pos = t->phrase;
                continue;
            }
        } else if (byte == SEQ_END) {
            track_clear_loops(t);
            return n;
        }
        pos++;
        t->phrase = pos;
    }
    return n;
}

/* Track_SetNoteVolume ($1183): FM: Track_SetLevel; PSG: the envelope's
 * peak (+$3E), start (+$3F, less +$36) and floor (+$40, less +$39). */
static void track_set_note_volume(HzSnd *s, HzSndTrack *t, u8 volume)
{
    if (is_fm(s->channel)) {
        track_set_level(s, t, volume);
        return;
    }
    t->env_peak = volume;
    t->env_start = volume >= t->env_start_drop
                 ? (u8)(volume - t->env_start_drop) : 0;
    t->env_floor = volume >= t->env_sustain_drop
                 ? (u8)(volume - t->env_sustain_drop) : 0;
}

/* Track_PlayNote ($1109): the note transposed (+$25, z_song_transpose),
 * its frequency, its volume (music: the song's, the channel's and the
 * velocity; sfx: the velocity) with +$26; gate 0 rests, else a key on
 * unless the last note is still held. */
static void track_play_note(HzSnd *s, HzSndTrack *t, SndNote n)
{
    if (s->lfo_channel == s->channel) {
        s->lfo_delay = 0xFF;
    }
    u8 note = note_add(n.note, t->transpose);
    note = note_add(s->song_transpose, note);
    track_set_note(s, t, note);
    track_set_frequency(s, t);

    u8 volume;
    u8 velocity = n.velocity;
    if (s->sfx_pass != 0) {
        volume = VOLUME_MAX;
    } else {
        velocity = volume_combine(s->song_volume, velocity);
        volume = z80_read(s, (u16)(header_ptr(s, s->song, SONG_CHANNEL_VOL)
                                   + s->channel));
    }
    volume = volume_combine(volume, velocity);
    volume = volume_clamp(volume, t->volume_offset);
    track_set_note_volume(s, t, volume);

    if (n.gate == 0) {
        track_note_off(s, t);
    } else if (!note_held(t)) {
        track_key_on(s, t);
    }
    t->length_count = n.length;
    t->gate_count = n.gate;
}

/* Flag_Wait ($1394): $E5 n: wait n + 1 ticks. 0: every music track that
 * is waiting (+$24) skips to its phrase's end. QUIRK: a track whose
 * order list stands on $FF is passed over; a record never started reads
 * its order position 0, Init's $FF. */
static void flag_wait(HzSnd *s, HzSndTrack *t, u8 ticks)
{
    t->wait = ticks;
    if (ticks != 0) {
        t->length_count = (u8)(ticks + 1);
        t->gate_count = 1;
        return;
    }
    for (int k = 0; k < SND_TRACK_COUNT; k++) {
        HzSndTrack *m = &s->music[k];

        if (z80_read(s, m->order) == SEQ_END) {
            continue;
        }
        if (m->wait == 0) {
            continue;
        }
        u16 pos = m->phrase;
        for (int guard = 0; guard < HANG_GUARD; guard++) {
            if (z80_read(s, pos) >= 0xFE) {
                break;
            }
            pos++;
        }
        m->phrase = pos;
        m->wait = 0;
    }
}

/* Track_LFOOn ($135F): FM tracks: TRACK_LFO; a lower channel than the
 * LFO's owner takes the LFO. */
static void track_lfo_on(HzSnd *s, HzSndTrack *t)
{
    u8 ch = s->channel;

    if (!is_fm(ch)) {
        return;
    }
    t->flags |= TRACK_LFO;
    if (s->lfo_channel <= ch) {
        return;
    }
    s->lfo_channel = ch;
    lfo_take_track(s, t);
}

/* Track_LFOOff ($137C): TRACK_LFO cleared; the LFO's own track lets it go
 * and rewrites its pan. */
static void track_lfo_off(HzSnd *s, HzSndTrack *t)
{
    u8 ch = s->channel;

    if (!is_fm(ch)) {
        return;
    }
    t->flags &= (u8)~TRACK_LFO;
    if (s->lfo_channel != ch) {
        return;
    }
    s->lfo_channel = LFO_NONE;
    fm_write_pan(s, t);
}

/* An offset added to a byte, or 0 cleared it (Flag_Volume's, Flag_Tempo's
 * way). */
static u8 add_or_clear(u8 value, u8 more)
{
    return more == 0 ? 0 : (u8)(value + more);
}

/* The flags with an argument, FlagTableE0 ($120D: $E0-$ED). */
static void flag_with_arg(HzSnd *s, HzSndTrack *t, u8 flag, u8 arg)
{
    switch (flag) {
    case 0xE0:                                           /* Flag_Instrument */
        t->instrument = arg;
        track_set_instrument(s, t, arg);
        break;
    case 0xE1: {                                         /* Flag_Pan */
        u8 speakers = PAN_SPEAKERS;                 /* 0 both, b7 right */
        if (arg != 0) {
            speakers = (arg & 0x80) ? PAN_RIGHT : PAN_LEFT;
        }
        t->pan = (u8)((t->pan & ~PAN_SPEAKERS) | speakers);
        fm_write_pan(s, t);
        break;
    }
    case 0xE2:                                           /* Flag_Noise */
        psg_set_noise(s, t->priority, (u8)(arg & 0x0F));
        break;
    case 0xE3:                                           /* Flag_SlideSpeed */
        t->slide_speed = (u8)((t->slide_speed & SLIDE_TIED_ONLY)
                              | (arg & SLIDE_SPEED));
        break;
    case 0xE4:                                           /* Flag_LFODelay */
        t->lfo_delay = arg;
        break;
    case 0xE5:                                           /* Flag_Wait */
        flag_wait(s, t, arg);
        break;
    case 0xE6:                                           /* Flag_SetMarker */
        s->marker = arg;
        break;
    case 0xE7:                                           /* Flag_AddMarker */
        s->marker = (u8)(s->marker + arg);
        break;
    case 0xE8: {                                         /* Flag_Transpose */
        u8 transpose = 0;                   /* 0 clears; else mod $60 */
        if (arg != 0) {
            transpose = (u8)(t->transpose + arg);
            if (transpose & 0x80) {
                transpose = (u8)(transpose + NOTES);
            } else if (transpose >= NOTES) {
                transpose = (u8)(transpose - NOTES);
            }
        }
        t->transpose = transpose;
        break;
    }
    case 0xE9:                                           /* Flag_Volume */
        t->volume_offset = add_or_clear(t->volume_offset, arg);
        if (t->fade_rate == 0) {
            break;
        }
        t->fade_count = t->fade_rate;
        t->fade_level = add_or_clear(t->fade_level, arg);
        break;
    case 0xEA:                                           /* Flag_Tempo */
        s->tempo_offset = add_or_clear(s->tempo_offset, arg);
        ym_set_tempo(s, (u8)(s->song_tempo + s->tempo_offset));
        break;
    case 0xEB:                                           /* Flag_Detune */
        t->detune = arg;
        break;
    case 0xEC:                                       /* Flag_RepeatStart */
        /* passes, then where the phrase is (its $EC n), at most 3 open */
        if (t->repeats_open == 3) {
            break;
        }
        t->repeats_open++;
        t->repeat_stack[t->repeat_sp] = arg;
        t->repeat_stack[t->repeat_sp + 1] = (u8)(t->phrase & 0xFF);
        t->repeat_stack[t->repeat_sp + 2] = (u8)(t->phrase >> 8);
        t->repeat_sp = (u8)(t->repeat_sp + 3);
        break;
    case 0xED:                                           /* Flag_FadeRate */
        t->fade_rate = arg;
        break;
    default:
        break;
    }
}

/* The flags without one, FlagTableF0 ($1229: $F0-$F7). */
static void flag_alone(HzSnd *s, HzSndTrack *t, u8 flag)
{
    switch (flag) {
    case 0xF0:                                           /* Flag_LFOOff */
        if (s->sfx_pass == 0) {
            track_lfo_off(s, t);
        }
        break;
    case 0xF1:                                           /* Flag_LFOOn */
        if (s->sfx_pass == 0) {
            track_lfo_on(s, t);
        }
        break;
    case 0xF2:                                           /* Track_UseTone */
        t->noise = 0;
        break;
    case 0xF3:                                           /* Track_UseNoise */
        t->noise = TRACK_ON_NOISE;
        break;
    case 0xF4:                                       /* Flag_SlideAlways */
        t->slide_speed &= (u8)~SLIDE_TIED_ONLY;
        t->flags &= (u8)~TRACK_BEND_ON_RELEASE;
        break;
    case 0xF5:                                           /* Flag_SlideTied */
        t->slide_speed |= SLIDE_TIED_ONLY;
        t->flags &= (u8)~TRACK_BEND_ON_RELEASE;
        break;
    case 0xF6:                                     /* Flag_BendUpOnRelease */
        t->flags |= TRACK_BEND_ON_RELEASE;
        t->flags &= (u8)~TRACK_BEND_DOWN;
        break;
    case 0xF7:                                   /* Flag_BendDownOnRelease */
        t->flags |= TRACK_BEND_ON_RELEASE | TRACK_BEND_DOWN;
        break;
    default:
        break;
    }
}

/* Track_ParseCommand ($0E79): the byte at *pos. Below $E0: a number, *pos
 * as it is. $E0-$ED: that flag with its argument, run (+2); $EE-$EF: +2;
 * $F0-$F7: that flag (+1); $F8-$FA: +1; $FB-$FF: returned in *byte, *pos
 * as it is. */
static ParseKind track_parse_command(HzSnd *s, HzSndTrack *t, u16 *pos,
                                     u8 *byte)
{
    u16 at = *pos;
    u8 v = z80_read(s, at);

    *byte = v;
    if (v < SEQ_FLAGS) {
        return PARSE_NUMBER;
    }
    if (v < 0xEE) {
        flag_with_arg(s, t, v, z80_read(s, (u16)(at + 1)));
        *pos = (u16)(at + 2);
        return PARSE_FLAG;
    }
    if (v < 0xF0) {
        *pos = (u16)(at + 2);
        return PARSE_FLAG;
    }
    if (v < 0xF8) {
        flag_alone(s, t, v);
        *pos = (u16)(at + 1);
        return PARSE_FLAG;
    }
    if (v < 0xFB) {
        *pos = (u16)(at + 1);
        return PARSE_FLAG;
    }
    return PARSE_OTHER;
}

/* Between notes (MusicTrack_Tick and SfxTrack_Tick share it): the gate
 * counted down (at 0 the note off); a fading track's volume again, +$2A
 * or a PSG track's envelope level. */
static void track_tick_counters(HzSnd *s, HzSndTrack *t)
{
    t->gate_count--;
    if (t->gate_count == 0) {
        track_note_off(s, t);
    }
    if (t->fade_level == 0) {
        return;
    }
    track_set_volume_now(s, t, is_fm(s->channel) ? t->volume : t->env_level);
}

/* MusicTrack_Tick ($0FB2): the LFO sweep on its channel, the slide, the
 * fade; the length (+$1F) and gate (+$20) counted down; at the length's
 * end the next note, phrase, or the track's end (again from its loop
 * point while z_song_repeat is set). QUIRK: an ended order list keys the
 * track off and clears its bit twice, in Track_NextPhrase and here. */
static void music_track_tick(HzSnd *s, HzSndTrack *t, u8 index)
{
    s->channel = (u8)(index & 0x0F);
    if (s->lfo_channel == s->channel) {
        lfo_sweep(s);
    }
    if (t->slide_speed & SLIDE_SPEED) {
        track_pitch_slide(s, t);
    }
    if (t->fade_level != 0) {
        track_volume_fade(s, t);
    }
    t->length_count--;
    if (t->length_count != 0) {
        track_tick_counters(s, t);
        return;
    }
    for (int guard = 0; guard < HANG_GUARD; guard++) {
        /* a Flag_Wait in the order list sets the length */
        if (t->length_count != 0) {
            return;
        }
        SndNote n = phrase_next_note(s, t);
        if (n.note != PHRASE_ENDED) {
            track_play_note(s, t, n);
            return;
        }
        for (;;) {
            if (track_next_phrase(s, t) != SEQ_END) {
                break;
            }
            track_note_off(s, t);
            music_clear_channel_bit(s);
            if (s->song_repeat == 0) {
                return;
            }
            t->order = t->loop;
            music_set_channel_bit(s);
            if (++guard >= HANG_GUARD) {
                return;
            }
        }
    }
}

/* z_lfo_channel (a word) put aside while effects' tracks run: they work
 * as if no track had the LFO (z_lfo_saved). */
static void lfo_set_aside(HzSnd *s)
{
    s->lfo_saved = (u16)(s->lfo_channel | s->lfo_channel_hi << 8);
    s->lfo_channel = LFO_NONE;
    s->lfo_channel_hi = LFO_NONE;
}

static void lfo_put_back(HzSnd *s)
{
    s->lfo_channel = (u8)(s->lfo_saved & 0xFF);
    s->lfo_channel_hi = (u8)(s->lfo_saved >> 8);
}

/* The music track under an effect's channel, back when the effect lets
 * it go (SfxTrack_Tick, Cmd_StopSfx): its instrument, frequency, noise
 * (Track_UseNoise) and LFO. */
static void music_track_restore(HzSnd *s, HzSndTrack *m)
{
    track_set_instrument(s, m, m->instrument);
    track_write_frequency(s, m);
    if (m->noise != 0) {
        m->noise = TRACK_ON_NOISE;
    }
    if (m->flags & TRACK_LFO) {
        track_lfo_on(s, m);
    }
}

/* An effect's track stopped (SfxTrack_Tick's end, Cmd_StopSfx): note off,
 * the envelope idle, silent (SfxTrack_Tick at once, Cmd_StopSfx through
 * Track_SetLevel), on tone again (Track_UseTone), free. */
static void sfx_track_stop(HzSnd *s, HzSndTrack *t, bool by_level)
{
    track_note_off(s, t);
    t->env_state = 0;
    if (by_level) {
        track_set_level(s, t, 0);
    } else {
        track_set_volume_now(s, t, 0);
    }
    if (t->noise != 0) {
        t->noise = 0;
    }
    t->priority = 0;
}

/* SfxTrack_Tick ($1030): as MusicTrack_Tick for an sfx track; at its
 * phrase's end the effect's track stops and its channel goes back to the
 * music track there (its volume 0 until its next note). */
static void sfx_track_tick(HzSnd *s, HzSndTrack *t, u8 index)
{
    s->channel = (u8)(index & 0x0F);
    if (t->slide_speed & SLIDE_SPEED) {
        track_pitch_slide(s, t);
    }
    if (t->fade_level != 0) {
        track_volume_fade(s, t);
    }
    t->length_count--;
    if (t->length_count != 0) {
        track_tick_counters(s, t);
        return;
    }
    SndNote n = phrase_next_note(s, t);
    if (n.note != PHRASE_ENDED) {
        track_play_note(s, t, n);
        return;
    }
    u8 ch = s->channel;
    if (ch == CH_NOISE) {
        s->sfx8_on = 0;
    } else {
        s->sfx_on &= (u8)~channel_bit(ch);
    }
    sfx_track_stop(s, t, false);
    if (s->channel != CH_NOISE) {
        HzSndTrack *m = &s->music[ch];

        s->sfx_pass = MUSIC_PASS;
        m->priority = 0;
        s->writer = m;                  /* (the port's) the music's again */
        lfo_put_back(s);
        music_track_restore(s, m);
    }
    s->lfo_channel = LFO_NONE;          /* the effects' pass goes on */
    s->lfo_channel_hi = LFO_NONE;
    s->sfx_pass = SFX_PASS;
}

/* PSG_EnvelopeStep ($13DF): a PSG track's envelope, each Timer A tick. A
 * key on starts the attack from +$3F (or the decay if that is the peak,
 * +$3E), a key off the release; then every $64 - +$35 ticks: the attack
 * +$37 up to the peak, the decay -$38 down to the floor (+$40), the
 * sustain -$3A, the release -$3B down to 0 (then idle); the level (+$41)
 * through Track_SetLevel. */
static void psg_envelope_step(HzSnd *s, HzSndTrack *t, u8 channel)
{
    u8 saved = s->channel;

    s->channel = (u8)(channel & 0x0F);
    if (t->env_state & (ENV_KEY_ON | ENV_KEY_OFF)) {
        t->env_count = 1;
        if (t->env_state & ENV_KEY_ON) {
            u8 level = t->env_start;
            t->env_state = level == t->env_peak ? ENV_DECAY : ENV_ATTACK;
            t->env_level = level;
            track_set_level(s, t, level);
            t->env_count = (u8)(ENV_TICKS - t->env_speed);
            s->channel = saved;
            return;
        }
        t->env_state = ENV_RELEASE;
    }
    t->env_count--;
    if (t->env_count != 0) {
        s->channel = saved;
        return;
    }
    u8 state = t->env_state;
    u8 level = t->env_level;
    if (state & ENV_ATTACK) {
        level = (u8)(level + t->env_attack);
        if (level >= t->env_peak) {
            t->env_state = ENV_DECAY;
            level = t->env_peak;
        }
        t->env_level = level;
        track_set_level(s, t, level);
    } else if (state & ENV_DECAY) {
        if (level != 0) {
            level = level >= t->env_decay ? (u8)(level - t->env_decay) : 0;
            if (level <= t->env_floor) {
                t->env_state = ENV_SUSTAIN;
                level = t->env_floor;
            }
            t->env_level = level;
            track_set_level(s, t, level);
        }
    } else if (state & ENV_SUSTAIN) {
        if (level != 0) {
            level = level >= t->env_sustain ? (u8)(level - t->env_sustain)
                                            : 0;
            t->env_level = level;
            track_set_level(s, t, level);
        }
    } else if (state & ENV_RELEASE) {
        if (level == 0) {
            t->env_state = 0;
        } else {
            level = level >= t->env_release ? (u8)(level - t->env_release)
                                            : 0;
            t->env_level = level;
            track_set_level(s, t, level);
        }
    }
    t->env_count = (u8)(ENV_TICKS - t->env_speed);
    s->channel = saved;
}

/* FM_UpdateLFO ($14A8): an FM track with its LFO on: z_lfo_out into the
 * pan byte's AMS/FMS bits when it changed (+$43/+$44), FM_WritePan.
 * QUIRK: LFO_UpdateFMTracks never sets z_channel, so FM_WritePan writes
 * the register of whatever channel z_channel last named (none from 6
 * up). */
static void fm_update_lfo(HzSnd *s, HzSndTrack *t)
{
    if (!(t->flags & TRACK_LFO)) {
        return;
    }
    if (t->lfo_fms_written == s->lfo_out_fms
        && t->lfo_ams_written == s->lfo_out_ams) {
        return;
    }
    t->lfo_fms_written = s->lfo_out_fms;
    t->lfo_ams_written = s->lfo_out_ams;
    u8 ams = (u8)(s->lfo_out_ams << 4 | s->lfo_out_ams >> 4);
    t->pan = (u8)((t->pan & PAN_SPEAKERS) | ams | s->lfo_out_fms);
    fm_write_pan(s, t);
}

/* LFO_UpdateRegister ($14E0): z_lfo_reg's requests to $22 (off, then on),
 * each served once; then FM_UpdateLFO for the six FM music tracks
 * (LFO_UpdateFMTracks, z_channel kept). QUIRK: the writes are checked
 * against the byte past the sfx records (z_psg_latch, never $FF). */
static void lfo_update_register(HzSnd *s)
{
    u8 reg = s->lfo_reg;

    if (reg & LFO_WRITE_OFF) {
        reg &= (u8)~LFO_WRITE_OFF;
        ym_write0_checked(s, s->psg_latch, YM_LFO, (u8)(reg & LFO_FREQ));
    }
    if (reg & LFO_WRITE_ON) {
        reg &= (u8)~LFO_WRITE_ON;
        ym_write0_checked(s, s->psg_latch, YM_LFO,
                          (u8)((reg & LFO_FREQ) | LFO_ENABLE));
    }
    s->lfo_reg = reg;

    u8 saved = s->channel;                         /* LFO_UpdateFMTracks */
    for (int k = 0; k < FM_CHANNELS; k++) {
        s->writer = &s->music[k];
        fm_update_lfo(s, &s->music[k]);
    }
    s->writer = NULL;
    s->channel = saved;
}

/* --- the handlers --------------------------------------------------- */

/* TimerA_Tick ($0A8A): PSG_UpdateEnvelopes ($0B28: music tracks 6-7, sfx
 * tracks 6-8, then the LFO register while music plays), timer A reset. */
static void timer_a_tick(HzSnd *s)
{
    s->sfx_pass = MUSIC_PASS;
    for (u8 ch = CH_PSG1; ch < SND_TRACK_COUNT; ch++) {
        psg_envelope_step(s, &s->music[ch], ch);
    }
    s->sfx_pass = SFX_PASS;
    for (u8 ch = CH_PSG1; ch < SND_SFX_COUNT; ch++) {
        psg_envelope_step(s, &s->sfx[ch], ch);
    }
    s->sfx_pass = MUSIC_PASS;
    if (s->music_on != 0) {
        lfo_update_register(s);
    }
    ym_write0(s, YM_TIMERS, TIMERS_RUN | TIMER_RESET_A);
}

/* TimerB_Tick ($0A97): the music's tick (MusicTracks_Update: each track
 * whose z_music_on bit was set when it began), timer B reset. */
static void timer_b_tick(HzSnd *s)
{
    if (s->music_on != 0) {
        u8 playing = s->music_on;

        s->sfx_pass = MUSIC_PASS;
        for (u8 k = 0; k < SND_TRACK_COUNT; k++) {
            if (playing & channel_bit(k)) {
                s->writer = &s->music[k];
                music_track_tick(s, &s->music[k], k);
            }
        }
    }
    s->writer = NULL;
    ym_write0(s, YM_TIMERS, TIMERS_RUN | TIMER_RESET_B);
}

/* Whether effect channel `k` (0-8) plays, as `on` (z_sfx_on) and `on8`
 * (z_sfx8_on) say. */
static bool sfx_playing(u8 on, u8 on8, int k)
{
    return k < 8 ? (on >> k) & 1 : on8 & 1;
}

/* VBlank_Interrupt ($0AA8): the sfx tracks' tick (SfxTracks_Update, each
 * of the 9 that plays; the LFO set aside meanwhile), the music's noise
 * mode back once the last effect has ended, then the music's fade: a step
 * every z_fade_speed VBlanks, the music off at $7F. */
static void vblank_interrupt(HzSnd *s)
{
    u8 any = (u8)(s->sfx_on | s->sfx8_on);

    if (any != 0) {
        u8 on = s->sfx_on;
        u8 on8 = s->sfx8_on;

        s->sfx_pass = any;
        lfo_set_aside(s);
        for (int k = 0; k < SND_SFX_COUNT; k++) {
            if (sfx_playing(on, on8, k)) {
                s->writer = &s->sfx[k];
                sfx_track_tick(s, &s->sfx[k], (u8)k);
            }
        }
        s->writer = NULL;
        lfo_put_back(s);
        if ((s->sfx_on | s->sfx8_on) == 0 && (s->music_noise & NOISE_KEPT)) {
            psg_set_noise(s, s->psg_latch, s->music_noise);
        }
    }
    s->sfx_pass = MUSIC_PASS;
    if (s->music_on == 0 || s->fade_speed == 0) {
        return;
    }
    s->fade_count--;
    if (s->fade_count != 0) {
        return;
    }
    s->fade_count = s->fade_speed;
    s->fade_level++;
    if (s->fade_level == SILENT) {
        s->music_on = 0;
    }
}

/* YM_SetTempo ($04CC): Timer B ($26) = 256 - $2084 / (tempo + $32), the
 * Z80's division by subtraction. */
static void ym_set_tempo(HzSnd *s, u8 tempo)
{
    u8 count = (u8)(0x2084 / (tempo + 0x32));

    ym_write0(s, YM_TIMER_B, (u8)(0 - count));
}

/* PSG_Silence ($0437): PSGSilenceData ($0444) to the PSG, its $FF too:
 * tones 1-3 at frequency 0, the noise's control byte, every volume off. */
static void psg_silence(HzSnd *s)
{
    static const u8 data[11] = {
        0x80, 0x00, 0xA0, 0x00, 0xC0, 0x00, 0xE0, 0x9F, 0xBF, 0xDF, 0xFF
    };

    for (int i = 0; i < 11; i++) {
        psg_write(s, data[i]);
    }
}

/* Track_HaltOrResume ($02AE): halting (z_restoring 0): a keyed track keyed
 * off, its volume 0; resuming: a keyed track's volume back, keyed on. */
static void track_halt_or_resume(HzSnd *s, HzSndTrack *t, u8 pass)
{
    s->writer = t;                                       /* (the port's) */
    s->sfx_pass = pass;
    if (s->restoring == HALT) {
        if (t->carriers & CARRIERS_KEYED) {
            fm_key_on_off(s);
        }
        track_set_volume(s, t, 0);
        return;
    }
    if (!(t->carriers & CARRIERS_KEYED)) {
        return;
    }
    track_set_volume(s, t, t->volume);
    fm_key_on_off(s);
}

/* Tracks_HaltOrResume ($02E0): `restoring` 0 halt / $FF resume, `key_ops`
 * the key byte. Per channel the sfx track if its bit is set, else the
 * music's if its bit is; PSG channels also take a track still releasing
 * (the sfx's first); the noise: the sfx track 8. QUIRK: the music bits
 * only move on a channel no effect has (the Z80 shifts them only there),
 * so after an effect's channel they no longer line up with their
 * channels. */
static void tracks_halt_or_resume(HzSnd *s, u8 restoring, u8 key_ops)
{
    s->restoring = restoring;
    s->key_ops = key_ops;
    u8 music_bits = s->music_on;
    u8 sfx_bits = s->sfx_on;

    for (u8 ch = 0; ch < SND_TRACK_COUNT; ch++) {
        s->channel = ch;
        bool sfx = sfx_bits & 1;
        sfx_bits >>= 1;
        if (sfx) {
            track_halt_or_resume(s, &s->sfx[ch], SFX_PASS);
            continue;
        }
        bool music = music_bits & 1;
        music_bits >>= 1;
        if (music) {
            track_halt_or_resume(s, &s->music[ch], MUSIC_PASS);
        } else if (is_fm(ch)) {
            continue;
        } else if (s->sfx[ch].env_state != 0) {
            track_halt_or_resume(s, &s->sfx[ch], SFX_PASS);
        } else if (s->music[ch].env_state != 0) {
            track_halt_or_resume(s, &s->music[ch], MUSIC_PASS);
        }
    }
    s->channel = CH_NOISE;
    if (s->sfx8_on != 0 || s->sfx[CH_NOISE].env_state != 0) {
        track_halt_or_resume(s, &s->sfx[CH_NOISE], SFX_PASS);
    }
    s->writer = NULL;
}

static void cmd_ack(HzSnd *s)                            /* Cmd_Ack ($044F) */
{
    s->command = 0;
}

/* Song_ReadHeader ($0D92): volume, tempo (YM_SetTempo), transpose; the
 * fade, the noise, the tempo offset, the LFO's sweep and owner cleared. */
static void song_read_header(HzSnd *s)
{
    s->song_volume = z80_read(s, (u16)(s->song + SONG_VOLUME));
    s->song_tempo = z80_read(s, (u16)(s->song + SONG_TEMPO));
    s->song_transpose = z80_read(s, (u16)(s->song + SONG_TRANSPOSE));
    ym_set_tempo(s, s->song_tempo);
    s->fade_level = 0;
    s->fade_speed = 0;
    s->music_noise = 0;
    s->tempo_offset = 0;
    s->lfo_sweep = 0;
    s->lfo_channel = LFO_NONE;
    s->lfo_channel_hi = LFO_NONE;
}

/* Song_StartTracks ($0DCC): the 8 tracks from the song's track table:
 * each one's order list (+$01, its loop point +$03 there too), its first
 * phrase (none: +$05 0); every music bit set first. */
static void song_start_tracks(HzSnd *s)
{
    u16 table = header_ptr(s, s->song, SONG_TRACKS);

    s->music_on = 0xFF;
    for (u8 k = 0; k < SND_TRACK_COUNT; k++) {
        HzSndTrack *t = &s->music[k];
        u16 order = (u16)(s->song + z80_read16(s, (u16)(table + 2 * k)));

        s->channel = k;
        s->writer = t;                                   /* (the port's) */
        track_init(s, t, order);
        t->loop = order;
        if (track_next_phrase(s, t) == SEQ_END) {
            t->phrase = 0;
        }
    }
    s->writer = NULL;
}

/* Cmd_StartSong ($0C9F), $01: z_song_repeat = z_arg0; the song at z_song
 * starts. */
static void cmd_start_song(HzSnd *s)
{
    s->song_repeat = s->arg[0];
    s->sfx_pass = MUSIC_PASS;
    s->command = 0;
    song_read_header(s);
    song_start_tracks(s);
}

/* Cmd_PlaySong ($0C85), $09: song z_arg1 from the table at $8000
 * (big-endian, | $8000) if its first byte is 9; then Cmd_StartSong.
 * (The port's) a song with a window of its own is looked up and played
 * there: the window stays until the next song (effects sit at the same
 * place in all of them; docs/sound.md, Room). */
static void cmd_play_song(HzSnd *s)
{
    const u8 *bank = s->bank;
    if (s->song_windows != NULL && s->arg[1] < s->song_window_count) {
        s->bank = s->song_windows + (size_t)s->arg[1] * SND_BANK_SIZE;
    }
    u16 entry = (u16)(0x8000 + 2 * s->arg[1]);
    u16 song = (u16)((z80_read(s, entry) << 8
                      | z80_read(s, (u16)(entry + 1))) | 0x8000);

    if (z80_read(s, (u16)(song + SONG_ID)) != 0x09) {
        s->bank = bank;
        cmd_ack(s);
        return;
    }
    s->song = song;
    s->song_id = s->arg[1];
    cmd_start_song(s);
}

/* Cmd_PlaySfx ($0BA7), $02: sfx z_arg0 at priority z_arg1 ($FF: none).
 * The header: a track count, then per track its channel, flags and a
 * phrase offset. Channels are walked 0-8 against the tracks in order:
 * each track takes its channel (priority + 1 kept in +$00, the music
 * track there muted) unless an sfx with a lower value plays there.
 * QUIRK: passing over a track skips its channel byte and one more only,
 * so the tracks after it are read from the wrong place. Run out of
 * channels before tracks: z_lfo_channel stays set aside, $FFFF. */
static void cmd_play_sfx(HzSnd *s)
{
    u8 sfx = s->arg[0];
    u8 priority = (u8)(s->arg[1] + 1);

    s->command = 0;
    if (priority == 0) {
        return;
    }
    if (sfx >= z80_read(s, (u16)(s->sfx_table + SFX_TABLE_COUNT))) {
        return;
    }
    u16 pos = (u16)(s->sfx_table
                    + z80_read16(s, (u16)(s->sfx_table + SFX_TABLE_SFX
                                          + 2 * sfx)));
    lfo_set_aside(s);
    s->sfx_pass = SFX_PASS;
    s->channel = 0;
    u8 tracks = z80_read(s, pos);
    pos++;

    for (int k = 0;; k++) {
        u8 ch = s->channel;

        if (ch == z80_read(s, pos)) {
            HzSndTrack *x = &s->sfx[k];
            HzSndTrack *m = &s->music[k < SND_TRACK_COUNT ? k : 0];

            if (x->priority == 0 || x->priority >= priority) {
                x->priority = priority;
                if (ch == CH_NOISE) {
                    s->sfx8_on = 1;
                } else {
                    s->sfx_on |= channel_bit(ch);
                }
                pos++;
                if (ch != CH_NOISE) {
                    m->priority = TRACK_MUTED;
                }
                s->writer = x;                           /* (the port's) */
                if (is_fm(ch)) {
                    tell_claim(s);
                }
                track_init(s, x, pos);
                if (ch != CH_NOISE && m->noise != 0) {
                    x->noise = 0;                        /* Track_UseTone */
                }
                sfx_track_set_phrase(s, x, &pos);
                track_lfo_off(s, x);
                s->writer = NULL;
            } else {
                for (;;) {
                    u8 byte = z80_read(s, pos);
                    pos++;
                    if (byte < SEQ_FLAGS) {
                        pos++;
                        break;
                    }
                    if (byte < 0xF0) {
                        pos++;
                    }
                }
            }
            tracks--;
            if (tracks == 0) {
                lfo_put_back(s);
                return;
            }
        }
        if (s->channel + 1 == SND_SFX_COUNT) {
            return;
        }
        s->channel++;
    }
}

/* Cmd_StopSfx ($0CB5), $04: every effect's track stopped and its channel
 * given back to the music track there if that one plays; the music's
 * noise mode last. QUIRK: that write is checked against the record ix has
 * walked to, nine on from the first music track: sfx track 1's priority,
 * which is 0 by then (stopped, or free), so the write is always made. */
static void cmd_stop_sfx(HzSnd *s)
{
    cmd_ack(s);
    u8 on = s->sfx_on;
    u8 on8 = s->sfx8_on;

    if ((on | on8) == 0) {
        return;
    }
    s->sfx_on = 0;
    s->sfx8_on = 0;
    u8 playing = s->music_on;

    for (int k = 0; k < SND_SFX_COUNT; k++) {
        s->channel = (u8)k;
        if (sfx_playing(on, on8, k)) {
            HzSndTrack *x = &s->sfx[k];

            s->writer = x;                               /* (the port's) */
            s->sfx_pass = SFX_PASS;
            sfx_track_stop(s, x, true);
            if (s->channel != CH_NOISE) {
                s->music[k].priority = 0;
            }
            if (k < SND_TRACK_COUNT && (playing & channel_bit((u8)k))) {
                HzSndTrack *m = &s->music[k];

                s->writer = m;                           /* (the port's) */
                s->sfx_pass = MUSIC_PASS;
                music_track_restore(s, m);
            }
        }
    }
    s->writer = NULL;
    if (s->music_noise & NOISE_KEPT) {
        psg_set_noise(s, s->sfx[1].priority, s->music_noise);
    }
}

/* Cmd_FadeMusic ($0D4E), $03: z_arg0 VBlanks a fade step; 0 stops the
 * music now (keyed tracks off, every music volume 0, the PSG envelopes'
 * levels 0). */
static void cmd_fade_music(HzSnd *s)
{
    u8 speed = s->arg[0];

    s->command = 0;
    s->fade_speed = speed;
    s->fade_count = speed;
    if (speed != 0) {
        return;
    }
    s->music_on = 0;
    s->sfx_pass = MUSIC_PASS;
    for (u8 k = 0; k < SND_TRACK_COUNT; k++) {
        HzSndTrack *t = &s->music[k];

        s->channel = k;
        s->writer = t;                                   /* (the port's) */
        if (t->carriers & CARRIERS_KEYED) {
            track_note_off(s, t);
        }
        if (!is_fm(k)) {
            t->env_level = 0;
        }
        track_set_volume_now(s, t, 0);
    }
    s->channel = SND_TRACK_COUNT;
    s->writer = NULL;
}

/* Cmd_Pause ($00FA), $0C: every track halted (keys off, volumes 0, the
 * volume writes unchecked), then the driver waits in WaitForUnpause for
 * the next $0C (other commands dropped), interrupts off; then resumed.
 * The waiting is s->paused: the commands and VBlanks that arrive in it
 * are handled in hz_snd_run. */
static void cmd_pause_start(HzSnd *s)
{
    s->volume_unchecked = true;
    tracks_halt_or_resume(s, HALT, KEY_OPS_OFF);
    s->paused = true;
    s->command = 0;                                      /* WaitForUnpause */
}

static void cmd_pause_end(HzSnd *s)
{
    s->command = 0;
    tracks_halt_or_resume(s, RESUME, KEY_OPS_ON);
    s->volume_unchecked = false;
    s->paused = false;
}

/* Cmd_SetBank ($183F), $0A: the bank window to the 68k address z_arg0-3
 * (z_bank_addr). The port has SoundData only, the one bank the game picks
 * ($78000): the address is kept, the window stays on SoundData. */
static void cmd_set_bank(HzSnd *s)
{
    for (int i = 0; i < 4; i++) {
        s->bank_addr[i] = s->arg[i];
    }
    s->command = 0;
}

/* MainLoop's dispatch through CommandTable ($00DE); $0E up dropped. The
 * DAC's commands only free the mailbox (see hz_snd.h). */
static void dispatch(HzSnd *s)
{
    if (s->paused) {
        if (s->command == SND_CMD_PAUSE) {
            cmd_pause_end(s);
        } else {
            s->command = 0;
        }
        return;
    }
    switch (s->command) {
    case SND_CMD_ACK:
        cmd_ack(s);
        break;
    case SND_CMD_START_SONG:
        cmd_start_song(s);
        break;
    case SND_CMD_PLAY_SFX:
        cmd_play_sfx(s);
        break;
    case SND_CMD_FADE_MUSIC:
        cmd_fade_music(s);
        break;
    case SND_CMD_STOP_SFX:
        cmd_stop_sfx(s);
        break;
    case SND_CMD_DAC_SAMPLE:
        cmd_ack(s);
        break;
    case SND_CMD_STOP_DAC:
        cmd_ack(s);
        break;
    case SND_CMD_SET_SONG:
        s->song = (u16)(s->arg[0] << 8 | s->arg[1]);
        cmd_ack(s);
        break;
    case SND_CMD_SET_SFX_TABLE:
        s->sfx_table = (u16)(s->arg[0] << 8 | s->arg[1]);
        cmd_ack(s);
        break;
    case SND_CMD_PLAY_SONG:
        cmd_play_song(s);
        break;
    case SND_CMD_SET_BANK:
        cmd_set_bank(s);
        break;
    case SND_CMD_DAC_HEADER:
        cmd_ack(s);
        break;
    case SND_CMD_PAUSE:
        cmd_pause_start(s);
        break;
    case SND_CMD_DAC_NUMBER:
        cmd_ack(s);
        break;
    default:
        s->command = 0;
        break;
    }
}

/* MainLoop ($00B8) at s->now: the mailbox (the commands due), then, unless
 * paused, Timers_Poll ($0A7E): one status read, Timer A's tick, Timer
 * B's. */
static void main_loop(HzSnd *s)
{
    while (s->q_count > 0 && s->queue[s->q_head].frame <= s->frame
           && !(s->one_command_a_frame && s->posted_frame == s->frame)) {
        const HzSndCommand *c = &s->queue[s->q_head];

        s->command = c->bytes[0];
        for (int i = 0; i < 5; i++) {
            s->arg[i] = c->bytes[i + 1];
        }
        s->q_head = (s->q_head + 1) % SND_QUEUE;
        s->q_count--;
        s->posted_frame = s->frame;
        dispatch(s);
    }
    if (s->paused) {
        return;
    }
    u8 status = s->timer_flags;
    if (status & TIMER_A_OVER) {
        timer_a_tick(s);
    }
    if (status & TIMER_B_OVER) {
        timer_b_tick(s);
    }
}

void hz_snd_init(HzSnd *s, const u8 *bank, HzSndSink sink, void *user)
{
    memset(s, 0, sizeof *s);
    s->bank = bank;
    s->sink = sink;
    s->user = user;
    s->next_frame = SND_FRAME;
    s->posted_frame = 0xFFFFFFFFu;

    /* Init ($003C): bank $78000, $1980-$1F2B cleared (the memset), the
     * tempo $6E, Timer A $14B (the PSG envelopes' tick, about 1.43 kHz),
     * both timers started, PSG silent. Its `ld (hl),$FF` with hl 0 lands
     * on the driver's own first byte: nothing here. */
    s->bank_addr[1] = 0x07;
    s->bank_addr[2] = 0x80;
    ym_set_tempo(s, 0x6E);
    ym_write0(s, YM_TIMER_A_HI, 0x52);
    ym_write0(s, YM_TIMER_A_LO, 0x03);
    ym_write0(s, YM_TIMERS, 0x00);
    ym_write0(s, YM_TIMERS, TIMERS_RUN | TIMER_RESET_A | TIMER_RESET_B);
    s->music_on = 0;
    s->sfx_on = 0;
    s->sfx8_on = 0;
    s->song = 0x8000;
    s->sfx_table = 0xF000;
    psg_silence(s);
    s->command = 0;
}

void hz_snd_set_song_windows(HzSnd *s, const u8 *windows, int count)
{
    s->song_windows = windows;
    s->song_window_count = windows != NULL ? count : 0;
}

bool hz_snd_post(HzSnd *s, u32 frame, const u8 *bytes, int count)
{
    if (s->q_count >= SND_QUEUE) {
        return false;
    }
    HzSndCommand *c = &s->queue[(s->q_head + s->q_count) % SND_QUEUE];

    memset(c, 0, sizeof *c);
    c->frame = frame;
    for (int i = 0; i < count && i < 6; i++) {
        c->bytes[i] = bytes[i];
    }
    s->q_count++;
    return true;
}

void hz_snd_run(HzSnd *s, u64 until)
{
    for (;;) {
        main_loop(s);

        u64 t = s->next_frame;
        if (s->timer_a_on && s->next_a < t) {
            t = s->next_a;
        }
        if (s->timer_b_on && s->next_b < t) {
            t = s->next_b;
        }
        if (t > until) {
            break;
        }
        s->now = t;
        timers_advance(s, t);
        if (t >= s->next_frame) {
            s->frame++;
            s->next_frame += SND_FRAME;
            if (!s->paused) {
                vblank_interrupt(s);           /* VBlankVector ($0038) */
            }
        }
    }
}
