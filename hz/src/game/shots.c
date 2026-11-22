#include <string.h>

#include "game.h"
#include "tables.h"

/* Shots: Shot_FireBullet ($ED2), Shot_FireMissile ($FF4), Shot_SetVelocity
 * ($10F4), Combat_Update ($111E) with Shots_UpdateTeam ($14A4),
 * Obj_FromGridId ($18E6), Shot_CheckHit ($1E12) and GetDirection ($21B8).
 *
 * Each team has 24 bullets and 8 missiles ($20-byte records). A bullet
 * fired on the ground (Shot_CheckHit) hits units and mechs through
 * v_grid, and stops at tiles with b14; one fired from the air (a jet's,
 * SHOT_FROM_AIR) hits only the enemy mech, and only as a jet. A missile
 * homes in on the enemy mech and hits only it. A unit's shot
 * (SHOT_FROM_UNIT) hits only the other side, and only it damages an HQ
 * (TerrainEv_ShotHQ). A mech's bullets hit any unit, its own side's
 * included, and the other mech. */

#define MECH_SIZE       0x20    /* KIDOU's obj_size; every unit is smaller */

/* The explosion's first sprite attr: a bullet's (palette line 1), a
 * missile's (line 1, high priority), each 4 frames of 4 tiles. */
#define BULLET_EXPLOSION  0xA580
#define MISSILE_EXPLOSION 0x2580

#define BULLETS         24
#define MISSILES        8

/* The fields Shots_UpdateTeam and the firing routines reach in an object
 * record, a mech's or a unit's (they share the offsets). */
typedef struct {
    u8  *flags;         /* +$00 */
    HzRef *hit_victim;  /* +$09 */
    u16 *hp;            /* +$0A */
    s16 *x, *y;         /* +$10/+$12 */
    u8  *missiles;      /* +$18 */
    u8  *ammo;          /* +$19 */
    u16 *sprite_attr;   /* +$1E: a unit's (a mech's frame is never set) */
    u16 *sprite_size;   /* +$20: a unit's */
    u8  *shot_flags;    /* +$22 */
    u32 type;           /* +$01: a unit's (a mech's mech_carried_slot) */
    u8  power;          /* +$08 */
    u8  size;           /* +$0C */
    u8  altitude;       /* +$0D: mech_altitude; a unit's byte, which no
                           unit code writes */
    u16 form;           /* +$2C: mech_form; read only for an object of
                           MECH_SIZE (a unit's would be its overlay attr) */
} HzObj;

static HzObj obj_at(HzGame *g, HzRef id)
{
    HzObj o;
    if (id.kind == REF_MECH) {
        HzMech *m = &g->mech[id.side];
        o.flags = &m->flags;
        o.hit_victim = &m->hit_victim;
        o.hp = &m->hp;
        o.x = &m->x;
        o.y = &m->y;
        o.missiles = &m->missiles;
        o.ammo = &m->ammo;
        o.sprite_attr = NULL;
        o.sprite_size = NULL;
        o.shot_flags = &m->shot_flags;
        o.type = m->carried_slot;
        o.power = m->power;
        o.size = m->size;
        o.altitude = m->altitude;
        o.form = m->form;
    } else {
        HzUnit *u = ref_unit_ptr(g, id);
        o.flags = &u->flags;
        o.hit_victim = &u->hit_victim;
        o.hp = &u->hp;
        o.x = &u->x;
        o.y = &u->y;
        o.missiles = &u->missiles;
        o.ammo = &u->ammo;
        o.sprite_attr = &u->sprite_attr;
        o.sprite_size = &u->sprite_size;
        o.shot_flags = &u->shot_flags;
        o.type = u->type;
        o.power = u->power;
        o.size = u->size;
        o.altitude = u->raw_0d;
        o.form = u->overlay_attr;
    }
    return o;
}

/* x 1.5 or x 2 of a byte, saturating at $FF. */
static u8 boost_half(u8 p)
{
    u16 v = (u16)(p + (p >> 1));
    return (v > 0xFF) ? 0xFF : (u8)v;
}

static u8 boost_double(u8 p)
{
    u16 v = (u16)(p + p);
    return (v > 0xFF) ? 0xFF : (u8)v;
}

/* The shot's power from the firing routines' boost code. Difficulty A/B:
 * x 1.5 while v_cpu_boost is on for this side (its team bit equal to
 * `team_flags`' team bit). C/D: when the shooter's side (shooter_flags'
 * team bit) is a CPU and the other a human, x 1.5 on C, x 2 on D. Every
 * flags byte here has its team in b4 (MECH_TEAM2, UNIT_TEAM2,
 * SHOT_TEAM2). */
static u8 shot_power(HzGame *g, u8 p, u8 team_flags, u8 shooter_flags)
{
    u8 difficulty = g->options[1];                      /* v_difficulty */
    if (difficulty < 2) {
        if ((s16)g->cpu_boost >= 0) {
            return p;                                   /* boost off */
        }
        if (((g->cpu_boost & 0xFF) ^ team_flags) & SHOT_TEAM2) {
            return p;                                   /* the other side's */
        }
        return boost_half(p);
    }
    const HzMech *own = &g->mech[0];
    const HzMech *other = &g->mech[1];
    if (shooter_flags & SHOT_TEAM2) {
        own = &g->mech[1];
        other = &g->mech[0];
    }
    if ((other->flags & MECH_CPU) || !(own->flags & MECH_CPU)) {
        return p;
    }
    return (difficulty == 2) ? boost_half(p) : boost_double(p);
}

/* Shot_SetVelocity ($10F4): both velocity pairs = SineTable[dir & $F] x
 * speed, the low words of the products. */
static void shot_set_velocity(HzShot *s, u8 dir)
{
    const HzPoint *sn = &SINE_TABLE[dir & 0x0F];
    s16 vx = (s16)(u16)((s32)(s16)s->speed * sn->x);
    s16 vy = (s16)(u16)((s32)(s16)s->speed * sn->y);
    s->vel_x = vx;
    s->vel_y = vy;
    s->aim_x = vx;
    s->aim_y = vy;
}

/* The common part of the two firing routines: a free record of the
 * team's `first` .. `first + count - 1`, a round from `*rounds` (none
 * left: none fired, the count kept at 0), the record cleared. */
static HzShot *shot_alloc(HzGame *g, u8 flags, int first, int count,
                          u8 *rounds)
{
    int base = (flags & SHOT_TEAM2) ? 32 : 0;
    HzShot *s = NULL;
    for (int i = 0; i < count; i++) {
        if (!(g->shots[base + first + i].flags & SHOT_ACTIVE)) {
            s = &g->shots[base + first + i];
            break;
        }
    }
    if (s == NULL) {
        return NULL;
    }
    if (*rounds == 0) {
        return NULL;
    }
    (*rounds)--;
    memset(s, 0, sizeof *s);
    return s;
}

/* Shot_FireBullet ($ED2): a bullet from `shooter` (a mech or a unit) with
 * `flags` (SHOT_ACTIVE, its team, SHOT_FROM_UNIT for a unit's), heading
 * `dir`, from (x, y), at `speed` for `life` frames: from the air when the
 * shooter flies. 0 fired, -1 not (no free bullet, or no ammo). The sound:
 * a mech's gun (robot or jet), or a unit's on screen. */
int shot_fire_bullet(HzGame *g, HzRef shooter, u8 flags, u8 dir, s16 x,
                     s16 y, u16 speed, u16 life)
{
    HzObj o = obj_at(g, shooter);
    HzShot *s = shot_alloc(g, flags, 0, BULLETS, o.ammo);
    if (s == NULL) {
        return -1;
    }
    if (o.altitude != 0) {
        flags |= SHOT_FROM_AIR;
    }
    s->flags = flags;
    s->dir = dir;
    s->x = x;
    s->y = y;
    s->speed = speed;
    s->life = life;
    u16 attr = 0x254C;              /* palette line 1, tile $54C */
    if (flags & SHOT_TEAM2) {
        attr = (u16)(attr + 1);
    }
    if (flags & SHOT_FROM_AIR) {
        attr = (u16)(attr + 0x8002);    /* high priority, tile $54E/$54F */
    }
    s->attr = attr;
    s->owner = shooter;
    s->power = shot_power(g, o.power, flags, *o.flags);
    shot_set_velocity(s, dir);
    if (o.size == MECH_SIZE) {
        snd_queue_sfx(g, (o.form != HZ_FORM_ROBOT) ? SFX_FIRE_JET
                                                   : SFX_FIRE_ROBOT);
    } else if (*o.flags & UNIT_ON_SCREEN) {
        snd_queue_sfx(g, SFX_FIRE_UNIT);
    }
    return 0;
}

/* Shot_FireMissile ($FF4): as Shot_FireBullet with the team's missiles and
 * obj_missiles; always from the air; power obj_power x 3 as a byte (it
 * wraps), the boosts by the shooter's own team bit. */
int shot_fire_missile(HzGame *g, HzRef shooter, u8 flags, u8 dir, s16 x,
                      s16 y, u16 speed, u16 life)
{
    HzObj o = obj_at(g, shooter);
    HzShot *s = shot_alloc(g, flags, BULLETS, MISSILES, o.missiles);
    if (s == NULL) {
        return -1;
    }
    flags |= SHOT_FROM_AIR;
    s->flags = flags;
    s->dir = dir;
    s->x = x;
    s->y = y;
    s->speed = speed;
    s->life = life;
    u16 attr = 0xA550;              /* high priority, line 1, tile $550 */
    if (flags & SHOT_TEAM2) {
        attr = (u16)(attr + 0x10);
    }
    s->attr = attr;
    s->owner = shooter;
    u8 p = (u8)(o.power + (u8)(o.power + o.power));
    s->power = shot_power(g, p, *o.flags, *o.flags);
    shot_set_velocity(s, dir);
    snd_queue_sfx(g, SFX_FIRE_MISSILE);
    return 0;
}

/* GetDirection ($21B8): direction 0-$F from (x0, y0) to (x1, y1), 0 up,
 * 4 right. */
u8 get_direction(s16 x0, s16 y0, s16 x1, s16 y1)
{
    s16 dx = (s16)(x1 - x0);
    s16 dy = (s16)(y1 - y0);
    if (dx == 0) {
        return (dy >= 0) ? 8 : 0;
    }
    if (dy == 0) {
        return (dx >= 0) ? 4 : 0x0C;
    }
    s32 num = dy;
    s16 den = dx;
    if (dx < 0) {
        den = (s16)-dx;
        num = -num;
    }
    num = (s32)((u32)num << 4);
    s32 q = num / den;
    s16 slope;
    if (q < -0x8000 || q > 0x7FFF) {
        slope = (s16)(u16)num;          /* divs overflows: d4 unchanged */
    } else {
        slope = (s16)q;
    }
    u8 d = 0;
    for (int i = 0; i < 8; i++) {
        if (slope <= DIR_SLOPE_TABLE[i]) {
            break;
        }
        d++;
    }
    if (dx < 0) {
        d = (u8)(d + 8);
    }
    return d & 0x0F;
}

/* Whether shot s may hit an object of side `side`, a unit or not (the
 * occupant in the shot's terms: its team, and whether it is a unit): a
 * shot doesn't hit its own kind of its own side (a mech's, its own mech;
 * a unit's, its side's units), and a unit's shot nothing of its own
 * side. */
static bool shot_may_hit(const HzShot *s, int side, bool unit)
{
    u8 kind = side ? SHOT_TEAM2 : 0;
    if (unit) {
        kind |= SHOT_FROM_UNIT;
    }
    u8 sides = (u8)(s->flags & (SHOT_TEAM2 | SHOT_FROM_UNIT));
    u8 differ = (u8)(kind ^ sides);
    if (differ == 0) {
        return false;
    }
    return !((sides & SHOT_FROM_UNIT) && !(differ & SHOT_TEAM2));
}

/* Whether the point (px, py) is on the object at (x, y) of `size`. */
static bool point_on(u16 px, u16 py, s16 x, s16 y, u16 size)
{
    return (u16)x < px && (u16)(px - (u16)x) < size
        && (u16)y < py && (u16)(py - (u16)y) < size;
}

/* (Session 75) PIXELS (hitmask.c): whether shot s's pixels touch object
 * id's; the patch off, true. */
static bool shot_touches(const HzGame *g, const HzShot *s, HzRef id)
{
    if (!hitmask_on(g)) {
        return true;
    }
    HzShape shot;
    HzShape obj;
    hitmask_shot(g, s, &shot);
    hitmask_ref(g, id, &obj);
    return hitmask_touch(&shot, &obj);
}

/* (Session 61) UNITS (crowd.c): the first object the point is on in the
 * 3 x 3 cells round its own, each cell's mechs first, then its units (P1's
 * side's first), of those the shot may hit; none if none. */
static HzRef crowd_shot_hit(const HzGame *g, const HzShot *s, u16 px,
                            u16 py)
{
    u16 cells[9];
    int n = crowd_cells_round(crowd_cell_of((s16)px, (s16)py), cells);
    for (int i = 0; i < n; i++) {
        for (int p = 0; p < 2; p++) {
            const HzMech *m = &g->mech[p];
            if (crowd_mech_in(g, p, cells[i]) && shot_may_hit(s, p, false)
                && point_on(px, py, m->x, m->y, m->size)
                && shot_touches(g, s, ref_mech(p))) {
                return ref_mech(p);
            }
        }
        for (int side = 0; side < 2; side++) {
            if (!shot_may_hit(s, side, true)) {
                continue;
            }
            for (const HzUnit *u = crowd_first(g, side, cells[i]); u != NULL;
                 u = crowd_after(g, u)) {
                if (point_on(px, py, u->x, u->y, u->size)
                    && shot_touches(g, s, unit_ref(u))) {
                    return unit_ref(u);
                }
            }
        }
    }
    return ref_none();
}

/* Shot_CheckHit ($1E12): what the point (x + 4, y + 4) of shot s is on. */
static void shot_check_hit(HzGame *g, HzShot *s)
{
    g->hit_flags = 0;
    g->hit_id = ref_none();
    g->terrain_event = 0;
    u16 px = (u16)(s->x + 4);
    u16 py = (u16)(s->y + 4);
    u16 cell = (u16)((((py & 0x7E0) << 1) + ((px >> 5) & 0x3F)) & 0xFFF);
    if (g->crowd) {
        HzRef obj = crowd_shot_hit(g, s, px, py);   /* (session 61) */
        if (!ref_is_none(obj)) {
            g->hit_flags |= PROBE_OBJECT;
            g->hit_id = obj;
        }
    }
    for (int n = 0; n <= 8 && !g->crowd; n++) {
        u16 c = (u16)(cell + ((n == 0) ? 0 : GRID_NEIGHBOUR_OFFSETS[n - 1]));
        if (c >= 0x1000) {
            continue;
        }
        HzRef obj = g->grid[c];
        if (ref_is_none(obj)) {
            continue;
        }
        if (!shot_may_hit(s, obj.side, obj.kind == REF_UNIT)) {
            continue;
        }
        HzObj o = obj_at(g, obj);
        if (!point_on(px, py, *o.x, *o.y, o.size)
            || !shot_touches(g, s, obj)) {
            continue;
        }
        g->hit_flags |= PROBE_OBJECT;
        g->hit_id = obj;
        break;
    }

    u16 tx = (u16)(px >> 3);
    u16 ty = (u16)(py >> 3);
    g->terrain_event2 = (u16)(((ty & 0xFC) << 5) + ((tx & 0xFC) >> 1));
    u16 blk = g->layout[(ty & 0xFC) >> 2][(tx & 0xFC) >> 2] & 0x1FF;
    u16 tw = g->blocks[blk][(ty & 3) * 4 + (tx & 3)];
    if (tw & TILE_STOPS_SHOTS) {
        g->hit_flags |= PROBE_TILE;
    }
    if (tw & TILE_EVENT) {
        g->terrain_event = g->block_terrain[blk];
        g->hit_flags |= PROBE_EVENT;
    }
}

/* The 8.8 move: the sub-pixel word takes the step, its high byte (signed
 * whole pixels) moves into the position. */
static void move_88(s16 *pos, u16 *sub, s16 step)
{
    *sub = (u16)(*sub + (u16)step);
    s8 whole = (s8)(u8)(*sub >> 8);
    *sub &= 0x00FF;
    *pos = (s16)(*pos + whole);
}

/* A hit (Shots_UpdateTeam's .hit to .store_victim): shot s, whose owner
 * is `own` (gone when SHOT_ORPHAN), hits object `obj`, unless it is
 * already dying. It loses the shot's power in HP, or is destroyed (a
 * mech's HP then counts down from $180, a unit's from $10, an FWA turning
 * into an explosion 4 px up and left); the owner learns its shot hit and
 * what it hit, `victim` (obj, but for a jet's hit on the enemy jet the
 * mech its team bit names, as the 68k built that grid id). */
static void shot_hit(HzGame *g, HzShot *s, HzObj *own, HzRef obj,
                     HzRef victim)
{
    HzObj t = obj_at(g, obj);
    if (*t.flags & UNIT_DYING) {                        /* MECH_DESTROYED */
        return;
    }
    *t.shot_flags = (u8)(HIT_BY_SHOT | (s->dir & 0x0F));
    if (*t.hp >= s->power) {
        *t.hp = (u16)(*t.hp - s->power);
        snd_queue_sfx(g, (t.size == MECH_SIZE) ? SFX_HIT_MECH
                                               : SFX_HIT_UNIT);
    } else {
        u16 hp = 0x180;
        u8 sfx = SFX_MECH_DESTROYED;
        if (obj.kind == REF_UNIT) {
            hp = 0x10;
            sfx = SFX_UNIT_DESTROYED;
            if (unit_on_foot(t.type, repair_on(g))) {
                sfx = SFX_INFANTRY_DESTROYED;
            } else if (t.type == UNIT_FWA) {
                *t.x = (s16)(*t.x - 4);
                *t.y = (s16)(*t.y - 4);
                HzUnit *u = ref_unit_ptr(g, obj);
                if (g->crowd && u->crowd_cell != 0) {
                    crowd_link(g, u);   /* (session 61) UNITS: its place */
                }
                *t.sprite_size = 0x0A00;
                *t.sprite_attr = g->assets->explosion_frames[0];
            }
        }
        *t.flags |= UNIT_DYING;                         /* MECH_DESTROYED */
        *t.hp = hp;
        snd_queue_sfx(g, sfx);
    }
    /* What it hit (obj_hit_victim). QUIRK: an orphan shot writes the
     * victim into the record it still points at, and always as the mech
     * of the side it hit, whatever it hit. */
    if (!(s->flags & SHOT_ORPHAN)) {
        *own->shot_flags |= OWN_SHOT_HIT;
        *own->hit_victim = victim;
        return;
    }
    *own->hit_victim = ref_mech(victim.side);
}

/* A bullet ends (Shots_UpdateTeam's .explode): the owner's shot no
 * longer in flight (unless the owner is gone), the explosion's 4 frames
 * from 4 px up and left. */
static void bullet_explode(HzShot *s, HzObj *own)
{
    if (!(s->flags & SHOT_ORPHAN)) {
        *own->shot_flags &= (u8)~OWN_SHOT_OUT;
    }
    s->flags |= SHOT_EXPLODING;
    s->life = 4;
    s->x = (s16)(s->x - 4);
    s->y = (s16)(s->y - 4);
    s->attr = (u16)(BULLET_EXPLOSION + (u16)(4 - s->life) * 4);
}

/* An exploding shot (.exploding / .missile_exploding): a frame of the
 * explosion; freed (the flags byte cleared) after the last. */
static void shot_exploding(HzShot *s, u16 base)
{
    s->life = (u16)(s->life - 1);
    if (s->life == 0) {
        s->flags = 0;
        return;
    }
    s->attr = (u16)(base + (u16)(4 - s->life) * 4);
}

/* A missile's steering (.steer): a velocity a step of $40 towards the
 * aim, or to it when closer. */
static s16 steer(s16 cur, s16 aim)
{
    if (aim == cur) {
        return cur;
    }
    if (aim > cur) {
        s16 n = (s16)(cur + 0x40);
        return (aim > n) ? n : aim;
    }
    s16 n = (s16)(cur - 0x40);
    return (aim < n) ? n : aim;
}

/* Shots_UpdateTeam ($14A4): `team`'s bullets, then its missiles, against
 * the other team's mech. */
static void shots_update_team(HzGame *g, int team)
{
    HzShot *shots = &g->shots[team * 32];
    HzMech *e = &g->mech[team ^ 1];
    s16 ex = e->x;
    s16 ey = e->y;
    /* Where an air bullet looks for the enemy mech (the 68k's a5/a6).
     * QUIRK: Shot_CheckHit, a ground bullet's test, leaves a5/a6 pointing
     * at v_unpack_buf and v_block_terrain without putting them back, so
     * once a ground bullet earlier in the list has been tested this
     * frame, every air bullet after it is compared with ($5000, $4000)
     * and can't hit: a jet's gun misses the enemy jet while its side's
     * ground shots are in flight ahead of it in the list. The missiles
     * load the position again. FIX (HZ_FIX_JET_GUN): the enemy's position
     * throughout. */
    s16 air_x = ex;
    s16 air_y = ey;

    for (int i = 0; i < BULLETS; i++) {
        HzShot *s = &shots[i];
        if (!(s->flags & SHOT_ACTIVE)) {
            continue;
        }
        if (s->flags & SHOT_EXPLODING) {
            shot_exploding(s, BULLET_EXPLOSION);
            continue;
        }
        /* The owner gone (not active, or dying): an orphan for good. The
         * active and dying bits are the same for mechs and units. */
        HzObj own = obj_at(g, s->owner);
        if ((*own.flags & (UNIT_ACTIVE | UNIT_DYING)) != UNIT_ACTIVE) {
            s->flags |= SHOT_ORPHAN;
        }
        s->life = (u16)(s->life - 1);
        if (s->life == 0) {
            s->flags |= SHOT_EXPLODING;
            bullet_explode(s, &own);
            continue;
        }
        move_88(&s->x, &s->sub_x, s->aim_x);
        move_88(&s->y, &s->sub_y, s->aim_y);
        u16 hit;
        if (s->flags & SHOT_FROM_AIR) {
            if (e->form == HZ_FORM_ROBOT) {
                continue;
            }
            if ((u16)(s->x - air_x) >= 0x18
                || (u16)(s->y - air_y) >= 0x18
                || !shot_touches(g, s, ref_mech(team ^ 1))) {
                continue;
            }
            e->vel_y = (s16)(e->vel_y + (s->vel_y >> 2));
            e->vel_x = (s16)(e->vel_x + (s->vel_x >> 2));
            shot_hit(g, s, &own, ref_mech(team ^ 1),
                     ref_mech((e->flags & MECH_TEAM2) ? 1 : 0));
            hit = PROBE_OBJECT;
        } else {
            shot_check_hit(g, s);
            if (!(g->fixes & HZ_FIX_JET_GUN)) {
                air_x = 0x5000;         /* QUIRK above: v_unpack_buf */
                air_y = 0x4000;         /* and v_block_terrain's words */
            }
            hit = g->hit_flags;
            if (hit & PROBE_EVENT) {
                shot_terrain_event(g, s);
            }
            if (hit & PROBE_OBJECT) {
                shot_hit(g, s, &own, g->hit_id, g->hit_id);
            }
        }
        if (!(hit & (PROBE_OBJECT | PROBE_TILE))) {
            if (!(s->flags & SHOT_ORPHAN)) {
                *own.shot_flags |= OWN_SHOT_OUT;
            }
            continue;
        }
        if (hit & PROBE_TILE) {
            snd_queue_sfx(g, SFX_HIT_TILE);
        }
        bullet_explode(s, &own);
    }

    u16 dirs = 0;
    for (int i = 0; i < MISSILES; i++) {
        HzShot *s = &shots[BULLETS + i];
        if (s->flags & SHOT_EXPLODING) {
            shot_exploding(s, MISSILE_EXPLOSION);
            continue;
        }
        if (!(s->flags & SHOT_ACTIVE)) {
            continue;
        }
        s->life = (u16)(s->life - 1);
        if (s->life != 0) {
            u8 want;
            if (e->flags & MECH_DESTROYED) {
                want = s->dir;
            } else if (s->life & 3) {
                want = s->dir;
                dirs |= (u16)(1u << ((want + 8) & 0x0F));
            } else {
                want = get_direction(s->x, s->y, (s16)(ex + 0x0C),
                                     (s16)(ey + 0x0C));
                dirs |= (u16)(1u << ((want + 8) & 0x0F));
                const HzPoint *sn = &SINE_TABLE[want];
                s->aim_x = (s16)(u16)((s32)(s16)s->speed * sn->x);
                s->aim_y = (s16)(u16)((s32)(s16)s->speed * sn->y);
            }
            s->vel_x = steer(s->vel_x, s->aim_x);
            s->vel_y = steer(s->vel_y, s->aim_y);
            u8 turn = (u8)((want - s->dir) & 0x0F);
            if (turn != 0) {
                s->dir = (u8)((s->dir + ((turn < 8) ? 1 : -1)) & 0x0F);
            }
            move_88(&s->x, &s->sub_x, s->vel_x);
            move_88(&s->y, &s->sub_y, s->vel_y);
            u16 attr = (u16)(0xA550 + s->dir);
            if (s->flags & SHOT_TEAM2) {
                attr = (u16)(attr + 0x10);
            }
            s->attr = attr;
            if ((u16)ex >= (u16)s->x || (u16)(s->x - ex) >= 0x18) {
                continue;
            }
            if ((u16)ey >= (u16)s->y || (u16)(s->y - ey) >= 0x18) {
                continue;
            }
            if (e->flags & MECH_DESTROYED) {
                continue;
            }
            if (!shot_touches(g, s, ref_mech(team ^ 1))) {
                continue;           /* (session 75) PIXELS */
            }
            e->vel_y = (s16)(e->vel_y + s->vel_y);
            e->vel_x = (s16)(e->vel_x + s->vel_x);
            if (e->hp >= s->power) {
                e->hp = (u16)(e->hp - s->power);
                snd_queue_sfx(g, SFX_HIT_MECH);
            } else {
                e->flags |= MECH_DESTROYED;
                e->hp = 0x180;
                snd_queue_sfx(g, SFX_MECH_DESTROYED);
            }
        }
        s->flags |= SHOT_EXPLODING;
        s->life = 4;
        s->x = (s16)(s->x - 4);
        s->y = (s16)(s->y - 4);
        s->attr = (u16)(MISSILE_EXPLOSION + (u16)(4 - s->life) * 4);
    }
    e->missile_dirs = dirs;
}

/* Combat_Update ($111E): team 1's shots against v_mech2, team 2's against
 * v_mech1, then one team's units by v_unit_team_toggle's b0
 * (Units_UpdateTeam: team 1 on even frames, team 2 on odd ones), the
 * toggle + 1. */
void combat_update(HzGame *g)
{
    shots_update_team(g, 0);
    shots_update_team(g, 1);
    units_update_team(g, g->unit_team_toggle & 1);
    g->unit_team_toggle = (u16)(g->unit_team_toggle + 1);
}
