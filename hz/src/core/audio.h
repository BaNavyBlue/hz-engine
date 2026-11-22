#ifndef HZ_AUDIO_H
#define HZ_AUDIO_H

#include "hz_types.h"

/* The sound: the Z80 driver's port (src/audio/hz_snd.c) playing Nuked-OPN2
 * (the YM2612) and the PSG, mixed to stereo. After PS2's (src/core/audio.c
 * and src/audio/fm.c there), cut down to what HZ needs.
 *
 * On the Mega Drive the driver runs on its own processor, clocked by the
 * YM's timers and the VBlank, while the 68k only posts commands. So here
 * the driver runs where the samples are made -- on the audio thread, in
 * hz_audio_render, advanced to each YM sample's time before the chips are
 * clocked -- and the game thread only posts commands (hz_audio_post),
 * through a single-producer, single-consumer ring. The driver's VBlanks
 * are the audio's own frames (896,040 master clocks), not the game's: the
 * Z80 doesn't wait on the 68k either.
 *
 * The chain per YM sample (master / 1008, 53,267 Hz): the driver to that
 * time; Nuked clocked 24 times, its left and right summed; one queued
 * register write -- the address on one sample, the data on the next, as
 * Nuked drops writes issued back to back (PS2's fm.c); the PSG; a DC
 * blocker per side (the chip's DC offset, AC-coupled away on the real
 * output); then a linear resample to the output rate, the mix gains and
 * PS2's soft knee (mix.h there). */

#define AUDIO_RATE     48000
#define AUDIO_CHANNELS 2           /* interleaved, left then right */

typedef struct HzAudio HzAudio;

/* sound_data: SoundData (HZ_SOUND_DATA bytes, kept by pointer). The driver
 * starts as the Z80 does once Snd_Init has released it (its Init at time
 * 0). NULL if there's no memory. */
HzAudio *hz_audio_create(const u8 *sound_data, int out_rate);
void     hz_audio_destroy(HzAudio *a);

/* The songs' own windows when they outgrow SoundData (HzAssets'
 * song_windows; hz_snd_set_song_windows), before the first render. */
void hz_audio_set_song_windows(HzAudio *a, const u8 *windows, int count);

/* Game thread: a mailbox command (the command byte, then its arguments, as
 * the 68k's Snd_* write them), taken by the driver in order at its next
 * step. False if the ring is full (the command is dropped). */
bool hz_audio_post(HzAudio *a, const u8 *bytes, int count);

/* Audio thread: `frames` stereo frames, written (not added) to out. */
void hz_audio_render(HzAudio *a, float *out, int frames);

/* Game thread: whether music is playing -- the driver's z_music_on as of
 * the audio thread's last render, or true while commands posted to it
 * haven't all been taken yet (so a song just asked for counts as on). */
bool hz_audio_music_on(HzAudio *a);

/* Chip writes the queue had no room for, since creation (0 expected). */
u32 hz_audio_dropped(const HzAudio *a);

/* (Session 26) The Esc screen's AUDIO page: each volume in whole dB on the
 * mix's own gains (0 dB: the mix as it is, the default), AUDIO_DB_OFF or
 * below for silence. Any thread; the audio thread takes them at its next
 * render. MUSIC and EFFECTS split the chips by channel: a channel is the
 * effects' while an effect's track has it (the driver's z_sfx_on and
 * z_sfx8_on), else the music's. An effect takes a music track's channel
 * outright (the music track there is muted, Cmd_PlaySfx), so the split is
 * exact; an FM channel changes sides in the chip's write order, at the
 * first write made after the effect took it or gave it back. */
#define AUDIO_DB_OFF (-100)

/* The FM channel (0-5) each group of four of a sample's 24 Nuked-OPN2
 * clocks puts out, by the chip's cycle counter / 4. */
extern const u8 HZ_AUDIO_CYCLE_CHANNEL[6];
void hz_audio_set_volume(HzAudio *a, int master_db, int music_db,
                         int effects_db);

/* (Session 36) The AUDIO page's SYNTH: what voices the YM2612's six
 * channels -- Nuked-OPN2, the chip as it was (AUDIO_SYNTH_NUKED, the
 * default), or PS2's Q backend (src/audio/q/, Cycfi's Q), which re-voices
 * the same register writes: four operators phase-modulating each other in
 * float, the chip's algorithms, ratios, detune, feedback and envelopes
 * (PS2's docs/audio.md, "The PM voicing"). The PSG is the same either way.
 * Q is there only in a build with it (`make` finds Q, see the Makefile);
 * hz_audio_has_q says. Any thread; the audio thread takes it at its next
 * render. Nuked runs on underneath either way, so the driver's timing and
 * the chip's state never change; a switch to Q gives the bank every
 * register as the chip has them (the shadow), so the next notes sound, the
 * ones already sounding don't carry over. */
#define AUDIO_SYNTH_NUKED 0
#define AUDIO_SYNTH_Q     1
bool hz_audio_has_q(void);
void hz_audio_set_synth(HzAudio *a, int synth);

/* (Session 38) The AUDIO page's VOICES, with Q (PS2's VOICES row): CHIP
 * voices the YM2612's six channels from its writes, so an effect that
 * takes a channel silences the music's part there as on the chip; EXTRA,
 * the default (PS2's), voices each track on voices of its own from the
 * driver's track hook (HzSndTrackWrite), the music's writes the chip never
 * gets included, so the music plays on under the effects. Any
 * thread; the audio thread takes it at its next render (as a switch to
 * Q: the bank handed each track's registers and held note). Nuked is the
 * chip either way. */
#define AUDIO_VOICES_CHIP  0
#define AUDIO_VOICES_EXTRA 1
void hz_audio_set_voices(HzAudio *a, int voices);

/* (Session 39) The AUDIO page's REPEATS, with Q: what a note struck again
 * on a voice still sounding does. FRESH (PS2's parallel voices): the old
 * note goes on as a tail fading over 15 ms and the new one starts on a
 * fresh voice from silence, so a fast repeat of a slow-attack instrument
 * dips (LOCH's bass, 8-10 dB). CHIP (the default since session 42): the
 * new note's attack starts from the level the old one had reached, on the
 * same voice, as the YM2612 does (its phases reset, as the chip's). Any
 * thread; the audio thread takes it at its next render, the next strike
 * on. */
#define AUDIO_REPEATS_FRESH 0
#define AUDIO_REPEATS_CHIP  1
void hz_audio_set_repeats(HzAudio *a, int repeats);

/* (Session 48) The AUDIO page's OVERSAMPLE, with Q (PS2's row): the bank
 * runs at the chip's rate, beside Nuked and resampled with it, its
 * operators at 1, 2 or 4 times that. 1 (HZ's default): the partials heavy
 * modulation throws past the rate fold back as the chip's do, the grain
 * under a feedback-heavy patch (song 6's parallel voices: the floor under
 * their notes within 0.7 dB of the chip's); 2 and 4 (PS2's default) filter
 * them out before the rate, cleaner. Any thread; taken at the next render,
 * the voices sounding go on. */
void hz_audio_set_oversample(HzAudio *a, int times);

/* (Session 37) Q's drums: HZ's driver plays its drums as FM instruments,
 * which Q's PM voicing renders thin (the kick's attack 4-8 dB down above
 * 200 Hz, the hats' noise 9 dB); with Q, the instruments the table names
 * (by song and instrument number, the driver's) strike a drum model
 * instead (PS2's: a tone gliding down, a filtered noise, a click), at the
 * track's level. assets/sound/q_drums.json (game/assets.c) holds both; the
 * game hands them over at start-up. Before any render. */
#define HZ_DRUM_MODELS 16
#define HZ_DRUM_USES   128     /* (session 39: 64 -> 128, the title's and music test's songs) */
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
    float level_db;     /* the model's level at carrier TL tl_ref */
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
} HzDrumModel;
typedef struct {
    u8 song;            /* Cmd_PlaySong's number */
    u8 instrument;      /* the song's instrument number (track +$1D) */
    u8 model;           /* an index into the models */
    float min_hz, max_hz;   /* the note's pitch (the chip's, from its
                               F-number) in this range; the first entry
                               that matches wins */
    int   from, to;     /* the track's order-list position in this range
                           (HzSndNote's; -1, -1: anywhere): a part of the
                           song, the same on each pass of its loop */
    float pitch;        /* > 0: the drum is pitched to the note x this
                           (0.5: an instrument sounding an octave below
                           its note); 0: the model's own pitch */
    float level_db;     /* (session 38) dB on the model's level: a song's
                           trim of a model it shares */
} HzDrumUse;
/* (Session 41) Q's music voices' level: every music note Q plays on its PM
 * voice (no drum) is voice_db louder (q_drums.json's "voice_level_db"),
 * and an entry with the model "voice" (HZ_DRUM_VOICE) sets that song's
 * instrument's own on top of it (its level_db), so each instrument sits
 * at the chip's level against the drums, which are fitted to the chip's.
 * Before any render. */
#define HZ_DRUM_VOICE 0xFF
void hz_audio_set_voice_db(HzAudio *a, float voice_db);
/* (Session 41) The table's entry for a music note, as core/audio.c's note
 * hook and hz_qdrums' strike both find it: the drum model + 1 (0: Q's PM
 * voice), *at its pitch code (hz_audio_drum_hz_code, 0: as defined), *db
 * its level: a drum's entry's level_db; for the PM voice voice_db plus a
 * "voice" entry's level_db. */
int  hz_audio_drum_lookup(const HzDrumUse *uses, int count, float voice_db,
                          u8 song, u8 instrument, float hz, u16 position,
                          u8 *at, float *db);
void hz_audio_set_drums(HzAudio *a, const HzDrumModel *models, int count,
                        const HzDrumUse *uses, int use_count);

/* (Session 38) A pitched drum's tone as the queue's marker carries it, in
 * a byte: quarter semitones up from 25 Hz (1: 25 Hz, 255: about 975 Hz),
 * 0 the model's own pitch. (Session 37's 4 Hz steps were a semitone at
 * the toms' 70 Hz.) */
#define HZ_DRUM_HZ_BASE  25.0f
u8    hz_audio_drum_hz_code(float hz);
float hz_audio_drum_code_hz(u8 code);
/* (Session 38) An entry's level_db in a marker's byte: half decibels,
 * 128 for 0 dB (-64 to +63.5 dB). */
u8    hz_audio_drum_db_code(float db);
float hz_audio_drum_code_db(u8 code);

#endif /* HZ_AUDIO_H */
