#include "rumble.h"
#include "game.h"

#include <string.h>

/* (Session 54) The rumble (rumble.h). The shakes, at HIGH, in frames:
 *
 *   hit        (session 55, the owner: "a short burst if you get hit by
 *              a bullet or missile") the small motor full for 6, a burst
 *              every hit; under it the big motor 30% + 70% by the HP lost
 *              (full at 64, a missile's or a TAX's; a jet's own gun takes
 *              16), held 4, down over 4-12 by the same. Until 55 the big
 *              motor alone (40% +) for 3 frames below 24 HP, too short
 *              for a DualShock 3's to spin up
 *   ground     a buzz: the small motor 60%, the big 25%, 4 + 4 (VULKAN's
 *              ground takes 32 HP every 8 frames, so it buzzes on while
 *              the robot stays)
 *   destroyed  both motors full, the small 12, the big held 20 and down
 *              over 40
 *   jets       (session 55, the owner: "big shake when the other plane
 *              hits you") both motors full, the small 12, the big held
 *              18 and down over 24. Until 55 the big 90% and the small
 *              80%, 6 + 8: a thud
 *   HQ hit     the big 75%, 10 + 14, as the HQ alert starts; then on each
 *              of its beats (the alert's sound, as its count of $B4 comes
 *              to $A0, $64 and $28) the big 55%, 6 + 12. The shots between
 *              aren't felt one by one: an HQ under fire is hit many times
 *              a second
 *   missile    a tick: the small motor 50% and the big 15% for 3 frames,
 *              each time the warning sounds (every 32 frames)
 *   HQ lost    both full, the small 20, the big held 60 and down over 60:
 *              the HQ's blast and the shake of the screen
 *   HQ won     the big 50%, 20 + 40 */

#define FULL 0xFFFF

/* A share of a motor's full, in thousandths. */
static u16 part(int permille)
{
    return (u16)((u32)FULL * (u32)permille / 1000u);
}

static int voice_left(const RumbleVoice *v)
{
    int left = v->hold + v->decay - v->age;
    return (left > 0) ? left : 0;
}

/* A peak's value at the voice's age. */
static u16 voice_value(const RumbleVoice *v, u16 peak)
{
    if (v->age < v->hold) {
        return peak;
    }
    int left = voice_left(v);
    if (left == 0) {
        return 0;
    }
    return (u16)((u32)peak * (u32)left / (u32)(v->decay + 1));
}

void rumble_voice_start(RumbleVoice *set, int count, u16 low, u16 high,
                        int hold, int decay)
{
    /* A free voice, else the one closest to its end. */
    int pick = 0;
    for (int i = 0; i < count; i++) {
        if (voice_left(&set[i]) < voice_left(&set[pick])) {
            pick = i;
        }
    }
    RumbleVoice *v = &set[pick];
    v->low = low;
    v->high = high;
    v->hold = (u8)hold;
    v->decay = (u8)decay;
    v->age = 0;
}

void rumble_voices_age(RumbleVoice *set, int count)
{
    for (int i = 0; i < count; i++) {
        if (voice_left(&set[i]) > 0) {
            set[i].age++;
        }
    }
}

RumbleMotors rumble_voices_motors(const RumbleVoice *set, int count)
{
    RumbleMotors m = { 0, 0 };
    for (int i = 0; i < count; i++) {
        u16 low = voice_value(&set[i], set[i].low);
        u16 high = voice_value(&set[i], set[i].high);
        if (low > m.low) {
            m.low = low;
        }
        if (high > m.high) {
            m.high = high;
        }
    }
    return m;
}

void rumble_test(RumbleVoice *set, int count)
{
    rumble_voice_start(set, count, part(800), part(600), 10, 14);
}

void rumble_reset(HzRumble *r)
{
    memset(r, 0, sizeof *r);
}

void rumble_quiet(HzRumble *r)
{
    memset(r->voice, 0, sizeof r->voice);
}

static void start(HzRumble *r, int side, u16 low, u16 high, int hold,
                  int decay)
{
    rumble_voice_start(r->voice[side], RUMBLE_VOICES, low, high, hold,
                       decay);
}

/* What a frame's felt bits start for a side. */
static void start_felt(HzRumble *r, int side)
{
    u16 felt = r->felt[side];
    if (felt & RUMBLE_FELT_HIT) {
        int d = (r->damage[side] > 64) ? 64 : r->damage[side];
        start(r, side, 0, FULL, 6, 0);
        start(r, side, part(300 + 700 * d / 64), 0, 4, 4 + d / 8);
    }
    if (felt & RUMBLE_FELT_GROUND) {
        start(r, side, part(250), part(600), 4, 4);
    }
    if (felt & RUMBLE_FELT_DESTROYED) {
        start(r, side, FULL, 0, 20, 40);
        start(r, side, 0, FULL, 12, 0);
    }
    if (felt & RUMBLE_FELT_JETS) {
        start(r, side, FULL, 0, 18, 24);
        start(r, side, 0, FULL, 12, 0);
    }
    if (felt & RUMBLE_FELT_HQ_HIT) {
        start(r, side, part(750), 0, 10, 14);
    }
    if (felt & RUMBLE_FELT_HQ_ALERT) {
        start(r, side, part(550), 0, 6, 12);
    }
    if (felt & RUMBLE_FELT_MISSILE) {
        start(r, side, part(150), part(500), 3, 0);
    }
    if (felt & RUMBLE_FELT_HQ_LOST) {
        start(r, side, FULL, 0, 60, 60);
        start(r, side, 0, FULL, 20, 0);
    }
    if (felt & RUMBLE_FELT_HQ_WON) {
        start(r, side, part(500), 0, 20, 40);
    }
}

/* The HQ alert's beats: sprites.c asks for its sound on each frame that
 * finds the count at $A0, $64 or $28, and counts it down only on the
 * frames it draws the warning (v_blink_timer $10 or more), so the sound
 * is asked for up to 16 frames running at each. A beat is felt on the
 * first of them. Its start ($B4) is the HQ hit. */
static bool hq_alert_beat(u8 count)
{
    return count == 0xA0 || count == 0x64 || count == 0x28;
}

void rumble_watch(HzRumble *r, const HzGame *g, u32 sfx)
{
    for (int s = 0; s < 2; s++) {
        rumble_voices_age(r->voice[s], RUMBLE_VOICES);
        r->felt[s] = 0;
        r->damage[s] = 0;
    }
    if (g->game_type < 0) {
        r->primed = false;              /* the attract demo: no one's */
        return;
    }

    u16 blink = g->blink_timer;
    bool blinked = r->primed && blink != r->blink;
    for (int s = 0; s < 2; s++) {
        const HzMech *m = &g->mech[s];
        u16 hq = post_hq_hp(&g->posts[HZ_POST_HQ + s]);
        if (r->primed) {
            bool was_down = (r->flags[s] & MECH_DESTROYED) != 0;
            bool down = (m->flags & MECH_DESTROYED) != 0;
            if (!was_down && down) {
                r->felt[s] |= RUMBLE_FELT_DESTROYED;
            } else if (!was_down && m->hp < r->hp[s]) {
                /* A shot's, or (the ground's sizzle asked for, no shot
                 * on a mech) the ground's. */
                r->damage[s] = (u16)(r->hp[s] - m->hp);
                bool shot = (sfx & (1u << SFX_HIT_MECH)) != 0;
                bool ground = (sfx & (1u << SFX_SIZZLE)) != 0;
                r->felt[s] |= (ground && !shot) ? RUMBLE_FELT_GROUND
                                                : RUMBLE_FELT_HIT;
            }
            if (hq < r->hq_hp[s] && r->hq_alert[s] == 0
                && m->hq_alert != 0) {
                r->felt[s] |= RUMBLE_FELT_HQ_HIT;
            }
            /* The count the frame's sprites saw: last frame's. */
            u8 seen = r->hq_alert[s];
            if ((sfx & (1u << SFX_HQ_ALERT)) && hq_alert_beat(seen)
                && r->hq_beat[s] != seen) {
                r->felt[s] |= RUMBLE_FELT_HQ_ALERT;
                r->hq_beat[s] = seen;
            }
            /* The warning sounds as sprites.c's draw_alerts asks, which it
             * doesn't once the game is over (session 55: a missile still
             * homing in through the HQ's blast buzzed with no warning) */
            if (blinked && m->missile_dirs != 0 && (blink & 0x1F) == 0
                && !g->game_over) {
                r->felt[s] |= RUMBLE_FELT_MISSILE;
            }
        }
        r->hp[s] = m->hp;
        r->flags[s] = m->flags;
        r->hq_hp[s] = hq;
        r->hq_alert[s] = m->hq_alert;
        if (m->hq_alert == 0 || m->hq_alert == 0xB4) {
            r->hq_beat[s] = 0;          /* over, or started again */
        }
    }
    r->blink = blink;
    if (r->primed && (sfx & (1u << SFX_JETS_MEET))) {
        r->felt[0] |= RUMBLE_FELT_JETS;
        r->felt[1] |= RUMBLE_FELT_JETS;
    }
    if (r->primed && (sfx & (1u << SFX_HQ_DESTROYED))) {
        /* GameOver_DestroyHQ's: the loser's HQ, by v_winner; (session
         * 66) a draw's both */
        int lost = (g->winner != 0) ? 0 : 1;
        r->felt[lost] |= RUMBLE_FELT_HQ_LOST;
        r->felt[lost ^ 1] |= (g->winner == HZ_DRAW) ? RUMBLE_FELT_HQ_LOST
                                                    : RUMBLE_FELT_HQ_WON;
    }
    r->primed = true;
    for (int s = 0; s < 2; s++) {
        start_felt(r, s);
    }
}

RumbleMotors rumble_motors(const HzRumble *r, int side)
{
    return rumble_voices_motors(r->voice[side], RUMBLE_VOICES);
}

RumbleMotors rumble_scale(RumbleMotors m, RumbleLevel level)
{
    static const u32 SHARE[RUMBLE_LEVELS] = { 0, 2, 3, 4 };    /* quarters */
    u32 q = ((unsigned)level < RUMBLE_LEVELS) ? SHARE[level] : 0;
    m.low = (u16)(m.low * q / 4u);
    m.high = (u16)(m.high * q / 4u);
    return m;
}

static const char *const LEVEL_NAME[RUMBLE_LEVELS] = {
    "OFF", "LOW", "MEDIUM", "HIGH"
};
static const char *const LEVEL_KEY[RUMBLE_LEVELS] = {
    "off", "low", "medium", "high"
};

const char *rumble_level_name(RumbleLevel level)
{
    return ((unsigned)level < RUMBLE_LEVELS) ? LEVEL_NAME[level] : "?";
}

const char *rumble_level_key(RumbleLevel level)
{
    return ((unsigned)level < RUMBLE_LEVELS) ? LEVEL_KEY[level] : "?";
}

RumbleLevel rumble_level_from_key(const char *key, RumbleLevel fallback)
{
    for (int i = 0; key != NULL && i < RUMBLE_LEVELS; i++) {
        if (strcmp(key, LEVEL_KEY[i]) == 0) {
            return (RumbleLevel)i;
        }
    }
    return fallback;
}
