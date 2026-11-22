#ifndef PS2_Q_SYNTH_H
#define PS2_Q_SYNTH_H

/* (build 110) The Q backend's C interface: the sequencer's chip writes
 * re-voiced with Cycfi's Q (q_synth.cpp, C++20; docs/audio.md, "The Q
 * backend"). Built only with `make Q=1`, `make q_demo` or `make
 * audio_check_q`; C reaches it through these calls and the synth table's
 * entry (q_synth_backend, synth.h), all extern "C".
 *
 * The bank decodes register writes as the chip would take them: it keeps a
 * shadow of both ports, a key-on strikes a voice with the patch the shadow
 * holds, $A4/$A0 retune it, the carriers' total levels set its loudness, $B4
 * pans it, a drum byte strikes the drum voice with FM6's pan. */

#include "../../core/hz_types.h"
#include "q_ps2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QBank QBank;

/* How the drums sound (docs/audio.md, "The Q backend", for the owner's choice):
 * synthesised by what the ROM's six samples measure as, or the samples
 * themselves at the Z80's rates through Q's Hermite interpolation and a
 * low-pass. PS2_Q_DRUMS=synth|samples picks one at create; synthesised
 * unless it says otherwise. */
typedef enum {
    Q_DRUMS_SYNTH = 0,
    Q_DRUMS_SAMPLES
} QDrumMode;

/* (build 111) How the FM voices are made (docs/audio.md, "The PM voicing"):
 * four operators phase-modulating each other in float, the chip's
 * algorithms, ratios, detune, feedback and envelopes (the default); or build
 * 110's subtractive approximation, a band-limited waveform per carrier
 * through a filter. PS2_Q_VOICE=pm|subtractive picks one at create. */
typedef enum {
    Q_VOICE_PM = 0,
    Q_VOICE_SUBTRACTIVE
} QVoicing;

QBank *q_bank_create(int sample_rate);
void   q_bank_destroy(QBank *b);

/* (build 136b) The drums, music/drums.json and its WAVs (drums.h's
 * drums_load), for Q_DRUMS_SAMPLES (the synthesised drums need nothing
 * loaded). */
bool   q_bank_load_drums(QBank *b, const char *path);

/* (build 125) The game's pack for q_bank_load_drums (ps2_types.h's
 * Ps2Reader); NULL, the default, reads the disk. */
void   q_synth_set_reader(Ps2Reader reader);

/* Every voice silent, the shadow at the chip's reset values. */
void   q_bank_reset(QBank *b);

/* fm_write's shape: a register write to port 0 or 1. */
void   q_bank_write(QBank *b, int part, u8 reg, u8 value);

/* (111) The sequencer's hooks (smps.h's track_write): a write by the track
 * that made it. From the first one the bank voices tracks, not chip
 * channels, and `write` only keeps the chip's registers. */
void   q_bank_track_write(QBank *b, int track, u8 channel, int part, u8 reg,
                          u8 value, bool to_chip);

/* fm_dac_play's shape: a drum byte. */
void   q_bank_drum(QBank *b, u8 drum_byte);

/* fm_render's shape: ADDS `frames` stereo frames, left then right, scaled
 * by gain and by the bank's trim. */
void   q_bank_render(QBank *b, float *out, int frames, float gain);

/* (build 111) The voicing, and the trim that goes with it. */
void   q_bank_set_voicing(QBank *b, QVoicing voicing);

/* (111) The music at music_gain, the effects' voices at effects_gain. */
void   q_bank_render_split(QBank *b, float *out, int frames, float music,
                           float effects);

/* (111) The PM voicing's running rate: 1, 2 or 4 x the output rate
 * (PS2_Q_OVERSAMPLE; 4 unless it says otherwise). */
void   q_bank_set_oversample(QBank *b, int n);

/* (111) Parallel voices on or off (PS2_Q_PARALLEL=0: off). */
void   q_bank_set_parallel(QBank *b, bool on);

/* (build 112) The AUDIO page's VOICES, TAILS and PATCHES (synth.h's
 * SynthOption; PS2_Q_EFFECTS=take, PS2_Q_TAILS=ring, PS2_Q_PATCHES=0 at
 * create for the tools), and the patch file's instruments (a copy is kept). */
void   q_bank_set_option(QBank *b, int option, int value);
void   q_bank_set_patches(QBank *b, const SynthPatch *patches, int count);

/* The drum mode, and the trim that goes with it. */
void   q_bank_set_drum_mode(QBank *b, QDrumMode mode);

/* The bank's level against the chip's (kQTrim in q_synth.cpp, one a drum
 * mode, set by `q_demo --trim-pass`); the pass sets 1 to measure. */
void   q_bank_set_trim(QBank *b, float trim);
float  q_bank_trim(const QBank *b);

/* Diagnostics for q_demo --bench: retune the filters every n samples; FM
 * voices x samples rendered so far. */
void      q_bank_set_cutoff_every(QBank *b, int n);
long long q_bank_voice_samples(const QBank *b);

/* (HZ, session 36) HZ's driver has no hooks by track: the bank voices the
 * chip's six channels from the writes alone, and this says which of them
 * an effect holds (bit n: channel n, z_sfx_on's b0-5), for the EFFECTS
 * volume; a note keyed on from here is the effects' or the music's by it. */
void      q_bank_set_channel_effects(QBank *b, unsigned mask);

/* (HZ, session 37) HZ's FM drums. HZ's driver plays its drums as FM
 * instruments (a kick is a note whose pitch the driver slides down), which
 * the PM voicing renders thin; these play them as PS2's drums are played,
 * a model each (PS2's DrumPatch: a tone gliding down, a filtered noise, a
 * click), a drum voice a chip channel. The models come from the assets
 * (q_drums.json), each with the carrier TL it was measured at, so a quieter
 * track (or a fade) strikes it quieter by the chip's 0.75 dB a TL step. */
typedef struct {
    float tone_from_hz, tone_to_hz, glide_s, tone_level, tone_decay_s;
    float noise_level, noise_decay_s, noise_hz, noise_q;
    bool  noise_highpass;
    float click_level, click_ms, click_hz;
    bool  clip;
    float fall_hz_s;    /* after the glide the pitch keeps falling (Hz/s) */
    bool  layer;        /* true: struck over the PM voice (its noise and
                           click the attack the PM voicing loses), false:
                           in its place */
    float transpose;    /* semitones (- down): the tone, its glide and fall,
                           and a tom's membrane moved together */
    /* (session 37) shaping a drum, PS2's or its own: the body's decay x
     * decay_scale; the glide's depth x glide_depth (0: no glide, the drum
     * at its settled pitch from the strike); the fall after it x
     * fall_scale; a tom's membrane overtones overtones_db up (- down); the
     * strike's noise and click attack_db. 1, 1, 1, 0, 0: as defined. */
    float decay_scale, glide_depth, fall_scale, overtones_db, attack_db;
    int   ps2;          /* (session 37) PS2's own drum of this byte ($80
                           high tom ... $C0 closed hat, drums.h), as PS2
                           plays it: the fields above unused; -1: them */
    float level_db;     /* the whole model's level, at carrier TL tl_ref */
    int   tl_ref;
    /* (session 40) the wires: a second noise, band-passed at wire_hz,
     * from the strike at wire_level, falling to nothing over wire_decay_s
     * (wire_level 0: none) -- an FM snare's static, ringing on after its
     * tone has gone; and choke: a strike cuts (over 5 ms) the voices a
     * choking model struck from the same chip channel, as the chip's
     * key-on cuts the note before it */
    float wire_level, wire_decay_s, wire_hz;
    bool  choke;
    /* (session 44) with: another model struck with this one, on its own
     * voice, as that model defines it (no pitch moved), at its own level
     * plus with_db (not the entry's): a kick's thump under a low tuned
     * snare; -1: none */
    int   with_model;
    float with_db;
    /* (session 45) cut: any later key-on on the same chip channel (a
     * drum's strike or a PM note) cuts this model's voice over 5 ms, as
     * the chip's key-on ends the note before it -- for a drum that rings
     * on until the next note (a long kick), not into a tom fill; choke
     * above is the two-way case among choking models */
    bool  cut;
    /* (session 46) follow: the tone sounds the chip channel's own pitch
     * times follow (0.5: a patch at MUL 1/2), from the strike and down
     * through each frequency the driver writes there while it rings -- its
     * release bend, which starts at the note's own key-off and steps at
     * the song's tempo -- so it dives as the chip's note does. A write up
     * (the next note's, set before its key-on) or more than 3 semitones
     * down isn't followed. Never under follow_floor_hz. A glide runs from
     * tone_from_hz down to it; fall_hz_s and an entry's pitch are unused.
     * 0: off */
    float follow, follow_floor_hz;
} QDrumModel;
void      q_bank_set_drum_models(QBank *b, const QDrumModel *models,
                                 int count);
/* The next key-on on chip channel ch (0-5) strikes model (an index into the
 * models), or the PM voice (-1): core/audio.c says so before each key-on,
 * from the driver's note hook (HzSndNote) and the assets' table. */
void      q_bank_channel_drum(QBank *b, int ch, int model);
/* The next strike's pitch, Hz (0: the model's own): the model's tone (its
 * glide, its fall, a tom's membrane) moved so it starts there. Before
 * q_bank_channel_drum. */
void      q_bank_channel_drum_hz(QBank *b, int ch, float hz);
/* (HZ, session 38) The next strike's level, dB on its model's (an entry's
 * of the table: a song's own trim). Before q_bank_channel_drum. */
void      q_bank_channel_drum_db(QBank *b, int ch, float db);

/* (111b) Writes still waiting for their slot (the chip's pace: one every
 * two native samples), as fm_pending_writes. */
int       q_bank_pending_writes(const QBank *b);

#ifdef __cplusplus
}
#endif

#endif /* PS2_Q_SYNTH_H */
