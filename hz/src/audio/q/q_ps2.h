#ifndef HZ_Q_PS2_H
#define HZ_Q_PS2_H

/* What PS2's Q backend (q_synth.cpp, from PS2's src/audio/q/, its build
 * 139e) took from PS2's own audio headers -- synth.h, fm.h, drums.h and
 * smps.h -- kept here as PS2 has them, so q_synth.cpp stays PS2's file and
 * a newer one from PS2 drops in. Only the names it uses.
 *
 * HZ differs from PS2 where it matters to Q:
 *   - the chip's clock is the same (NTSC, the master / 7) and so its
 *     native rate, 53,267 Hz;
 *   - HZ's driver plays no DAC drums (asm/hz.asm's Z80 sample commands
 *     have no 68k caller: src/audio/hz_snd.h), so the drum entries are
 *     never struck and drums_load is the stub in q_hz.c;
 *   - HZ's driver has no hooks by track: the bank voices the chip's six
 *     channels from the writes alone (q_bank_write), and core/audio.c tells
 *     it which of them an effect holds (q_bank_set_channel_effect, HZ's own
 *     call), for the EFFECTS volume. SMPS_* below are PS2's numbers, which
 *     only size the bank's channels. */

#include "../../core/hz_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ps2_types.h */
typedef const u8 *(*Ps2Reader)(const char *path, size_t *size);

/* fm.h */
#define FM_MASTER_CLOCK 7670442          /* the YM2612's: NTSC master / 7 */
#define FM_Z80_CLOCK    3579545
#define FM_Z80_RAM      0x2000
#define FM_DAC_DRUMS    8

/* smps.h */
#define SMPS_MUSIC_TRACKS    6
#define SMPS_SFX_TRACKS      6      /* (HZ, session 38) PS2's 4; HZ's six
                                       FM effect tracks, one a channel */
#define SMPS_TRACKS          (SMPS_MUSIC_TRACKS + SMPS_SFX_TRACKS)
#define SMPS_TRACK_DRIVER    (-1)
#define SMPS_HOOK_CLAIM      0x00
#define SMPS_HOOK_INSTRUMENT 0x01

/* drums.h */
enum {
    DRUM_HIGH_TOM   = 0x80,
    DRUM_MID_TOM    = 0x81,
    DRUM_LOW_TOM    = 0x82,
    DRUM_SNARE      = 0x84,
    DRUM_KICK       = 0x88,
    DRUM_UNUSED     = 0x90,
    DRUM_OPEN_HAT   = 0xA0,
    DRUM_CLOSED_HAT = 0xC0
};
#define DRUM_Z80_RAM  0x2000
#define DRUM_MAX      8
typedef struct {
    u8  byte;
    u16 delay;
    u32 addr;
    u32 length;
} DrumEntry;
bool drums_load(const char *path, Ps2Reader reader, DrumEntry *table,
                int *count, u8 *ram);

/* synth.h */
typedef enum {
    SYNTH_OPT_VOICES = 0,
    SYNTH_OPT_TAILS,
    SYNTH_OPT_PATCHES,
    SYNTH_OPT_GRAIN,
    SYNTH_OPT_COUNT
} SynthOption;

#define SYNTH_TAIL_ROW  (-1)
#define SYNTH_TAIL_FADE 0
#define SYNTH_TAIL_RING 1

typedef struct {
    int   instrument;
    u8    rom[256];
    u8    reg[256];
    float fine_cents[4];
    float volume_db;
    int   tail;
} SynthPatch;

typedef struct {
    const char *name;
    const char *label;
    void *(*create)(int sample_rate);
    bool  (*load_drums)(void *self, const char *path);
    void  (*reset)(void *self);
    void  (*write)(void *self, int part, u8 reg, u8 value);
    void  (*drum)(void *self, u8 drum_byte);
    void  (*track_write)(void *self, int track, u8 channel, int part, u8 reg,
                         u8 value, bool to_chip);
    void  (*render)(void *self, float *out, int frames, float music_gain,
                    float effects_gain);
    void  (*shutdown)(void *self);
    bool  takes_tracks;
    void  (*set_oversample)(void *self, int factor);
    void  (*set_option)(void *self, int option, int value);
    void  (*set_patches)(void *self, const SynthPatch *patches, int count);
} SynthBackend;

#ifdef __cplusplus
}
#endif

#endif
