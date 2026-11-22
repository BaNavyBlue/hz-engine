#ifndef HZ_RUMBLE_H
#define HZ_RUMBLE_H

#include "../core/hz_types.h"

/* (Session 54) The rumble: the port's own, as the Mega Drive pad had no
 * motors. A gamepad with them (a DualShock, a DualSense, an Xbox pad)
 * shakes for what happens to its player's side: the mech hit by a shot,
 * the robot on ground that hurts, the mech destroyed, the two jets
 * meeting, the HQ under fire (as the HQ alert starts, then on its beats),
 * a missile homing in (on the warning's beats), the HQ destroyed (the
 * game lost; the winner feels the blast too, less).
 *
 * Nothing here changes the game. rumble_watch looks at the game once a
 * frame, after it has run -- each mech's HP and flags, each HQ's HP and
 * alert, the missile warnings -- and at sfx_asked, the effects the game
 * asked for that frame, which say what a change was (a shot or the
 * ground; the jets meeting, which leaves no other mark; the HQ's blast).
 * What it finds starts voices: a peak on each motor, held for some
 * frames, then down to nothing. A side's motors are the loudest of its
 * voices, motor by motor, scaled by the Esc screen's RUMBLE.
 *
 * The motors: `low` the big one (a slow, heavy shake; a DualShock 3's
 * left motor, the only one it can drive at a strength), `high` the small
 * one (a quick buzz; a DualShock 3's right motor, on or off), each 0 to
 * $FFFF, as SDL3's low and high frequency rumble. Which gamepads feel a
 * side is main.c's choice, the way it routes them (a CPU's side reaches
 * none). The attract demo is felt by no one. */

typedef enum {
    RUMBLE_OFF = 0,
    RUMBLE_LOW,
    RUMBLE_MEDIUM,
    RUMBLE_HIGH,
    RUMBLE_LEVELS
} RumbleLevel;

#define RUMBLE_DEFAULT RUMBLE_MEDIUM

/* What a side felt in a frame (HzRumble.felt): RUMBLE_FELT_* bits. */
enum {
    RUMBLE_FELT_HIT       = 0x001,  /* its mech hit by a shot */
    RUMBLE_FELT_GROUND    = 0x002,  /* its robot hurt by the ground */
    RUMBLE_FELT_DESTROYED = 0x004,  /* its mech destroyed */
    RUMBLE_FELT_JETS      = 0x008,  /* the two jets met */
    RUMBLE_FELT_HQ_HIT    = 0x010,  /* its HQ hit, the HQ alert starting */
    RUMBLE_FELT_HQ_ALERT  = 0x020,  /* a beat of the HQ alert */
    RUMBLE_FELT_MISSILE   = 0x040,  /* a beat of the missile warning */
    RUMBLE_FELT_HQ_LOST   = 0x080,  /* its HQ destroyed */
    RUMBLE_FELT_HQ_WON    = 0x100   /* the other side's HQ destroyed */
};
#define RUMBLE_FELT_KINDS 9

typedef struct {
    u16 low, high;
} RumbleMotors;

/* One shake: its peaks, held `hold` frames, then down to nothing in a
 * straight line over `decay`; age counts its frames, from 0. */
typedef struct {
    u16 low, high;
    u8  hold, decay;
    u8  age;
} RumbleVoice;

#define RUMBLE_VOICES 6

typedef struct {
    bool        primed;         /* last frame's state is below */
    u16         hp[2];          /* each mech's */
    u8          flags[2];
    u16         hq_hp[2];       /* each side's HQ */
    u8          hq_alert[2];
    u8          hq_beat[2];     /* the alert's count at its last beat felt,
                                   0 none since it started */
    u16         blink;          /* v_blink_timer */
    u16         felt[2];        /* this frame's, RUMBLE_FELT_* */
    u16         damage[2];      /* this frame's HP lost by each mech */
    RumbleVoice voice[2][RUMBLE_VOICES];
} HzRumble;

typedef struct HzGame HzGame;

/* Nothing felt, nothing held: what a game's start needs, so the last
 * game's state isn't taken for a change. */
void rumble_reset(HzRumble *r);

/* Every voice stopped (the Esc screen opening), the state kept. */
void rumble_quiet(HzRumble *r);

/* Once a frame, after the game's frame: `sfx` is sfx_asked as the frame
 * left it (the host clears it after). The voices age a frame, and what
 * the frame did starts new ones. */
void rumble_watch(HzRumble *r, const HzGame *g, u32 sfx);

/* A side's motors this frame, at HIGH. */
RumbleMotors rumble_motors(const HzRumble *r, int side);

/* The motors at a level: LOW a half, MEDIUM three quarters, HIGH all. */
RumbleMotors rumble_scale(RumbleMotors m, RumbleLevel level);

/* A set of voices of the host's own (the Esc screen's test). */
void         rumble_voice_start(RumbleVoice *set, int count, u16 low,
                                u16 high, int hold, int decay);
void         rumble_voices_age(RumbleVoice *set, int count);
RumbleMotors rumble_voices_motors(const RumbleVoice *set, int count);

/* The Esc screen's test buzz, in one voice set. */
void rumble_test(RumbleVoice *set, int count);

/* RUMBLE's names: upper case for the menu, lower for settings.json. */
const char *rumble_level_name(RumbleLevel level);
const char *rumble_level_key(RumbleLevel level);
/* The level for a settings.json name, or `fallback`. */
RumbleLevel rumble_level_from_key(const char *key, RumbleLevel fallback);

#endif /* HZ_RUMBLE_H */
