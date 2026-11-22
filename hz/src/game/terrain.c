#include "game.h"

#include <stddef.h>

/* Obj_TerrainEvent ($8764) and what it calls: what a terrain-event tile
 * (a nametable word with b13) does to the object over it. The 68k routine
 * takes any object in a0 and its kind in d0: 0 a unit (Units_UpdateTeam),
 * 1 a robot or 2 a jet (Mech_Update), 3 a shot (Shots_UpdateTeam). The
 * port has one entry per kind: mech_terrain_event, unit_terrain_event and
 * shot_terrain_event, each with the handlers that kind reaches.
 *
 * The handler is picked by the event's terrain type, v_terrain_event & $F
 * (TerrainEventHandlers, $8796); the terrain word's high byte is the
 * handler's parameter. On the maps: 2 outposts, 3 HQs, 6 currents (maps 0,
 * 1, 5), 7 the ring round each HQ, 8 damaging ground (map 1), 9 slippery
 * ground (map 5), $A slow ground (maps 6, 7). */

/* The unit a mech carries: its side's slot mech_carried_slot. (A mech
 * that never carried has slot 0, a record that is there.) */
HzUnit *carried_unit(HzGame *g, const HzMech *m)
{
    int side = (m->flags & MECH_TEAM2) ? 1 : 0;
    u32 slot = m->carried_slot;
    if (slot >= side_slots(g, side)) {
        slot = side_slots(g, side) - 1;     /* not reached */
    }
    return unit_at(g, side, slot);
}

/* One of Obj_RepairRefuel's four steps: `cur` + `add` up to `max` (a
 * `cur` above `max` comes down to it too). The 68k adds in the field's
 * width and takes the maximum on a carry; the sum here doesn't wrap, and
 * a sum that would have carried is past any maximum, so it comes to the
 * same. */
static u16 top_up(u16 cur, u16 add, u16 max)
{
    u32 sum = (u32)cur + add;
    return (sum < max) ? (u16)sum : max;
}

/* What Obj_RepairRefuel topped up (its d3; mech_carried_refill). */
#define REFILL_HP        0x01
#define REFILL_FUEL      0x02
#define REFILL_AMMO      0x04
#define REFILL_MISSILES  0x08

/* Obj_RepairRefuel ($8A9C): HP + 8, fuel + $28, ammo + 2, missiles + 2,
 * each capped at the type's UnitStats maximum. Returns REFILL_* for each
 * value that changed. */
static u8 obj_repair_refuel(const HzUnitStats *st, u16 *hp, u16 *fuel,
                            u8 *ammo, u8 *missiles)
{
    u8 changed = 0;
    u16 v = top_up(*hp, 8, st->hp);
    if (v != *hp) {
        changed |= REFILL_HP;
    }
    *hp = v;
    v = top_up(*fuel, 0x28, st->fuel);
    if (v != *fuel) {
        changed |= REFILL_FUEL;
    }
    *fuel = v;
    v = top_up(*ammo, 2, st->ammo);
    if (v != *ammo) {
        changed |= REFILL_AMMO;
    }
    *ammo = (u8)v;
    v = top_up(*missiles, 2, st->missiles);
    if (v != *missiles) {
        changed |= REFILL_MISSILES;
    }
    *missiles = (u8)v;
    return changed;
}

/* The outpost or HQ record `index` (v_outposts, then v_hq at $10/$11) is
 * owned by the team of an object with `obj_flags` (its team in b4: a
 * mech's, a unit's). */
static bool post_is_own(const HzGame *g, u8 obj_flags, int index)
{
    u8 flags = g->posts[index].flags;
    if (!(flags & POST_OWNED)) {
        return false;
    }
    return !(((obj_flags >> 4) ^ flags) & POST_TEAM2);
}

/* Outpost_Service ($8A14) for a mech: on frames with v_unit_ai_group 0,
 * an own outpost or HQ repairs and refuels the mech (Obj_RepairRefuel as
 * type $E, KIDOU) and the unit it carries, if any, whose result goes into
 * mech_carried_refill (the order menu redraws those gauges). */
static void outpost_service(HzGame *g, HzMech *m, int index)
{
    if (g->unit_ai_group != 0) {
        return;                                         /* Outpost_ServiceExit */
    }
    if (!post_is_own(g, m->flags, index)) {
        return;
    }
    obj_repair_refuel(unit_stats_for(g, 0x0E), &m->hp, &m->fuel,
                      &m->ammo, &m->missiles);
    if (!(m->flags & MECH_CARRYING)) {
        return;
    }
    HzUnit *u = carried_unit(g, m);
    m->carried_refill = obj_repair_refuel(unit_stats_for(g, u->type),
                                          &u->hp, &u->fuel, &u->ammo,
                                          &u->missiles);
}

/* Outpost_PickUpProduct ($891C): a jet not carrying, with its order menu
 * closed, A held (not just pressed), and a unit ready, over an own
 * outpost or HQ, loads the unit: Unit_CreateCarried in its team, carrying
 * (mech_carried_slot/_order), the order cleared, carrying speed (KIDOU
 * +$22) and fuel use (+$25, halved when either mech is a CPU on
 * difficulty A, outside the demo). With 50 units on its side it beeps
 * instead (only if its view is shown); (session 61) with UNITS never.
 * Returns true if it picked the unit up (the 68k's d2 0). */
static bool outpost_pick_up_product(HzGame *g, HzMech *m, int kind,
                                    int index)
{
    if (m->flags & MECH_MENU_OPEN) {
        return false;
    }
    if (kind != HZ_OBJ_JET || !(m->pad & HZ_PAD_A) || m->form != HZ_FORM_JET
        || m->prod_state != PROD_READY) {
        return false;
    }
    if (!post_is_own(g, m->flags, index)) {
        return false;
    }
    int team = (m->flags & MECH_TEAM2) ? 1 : 0;
    if (!side_has_room(g, team)) {
        if (m->view >= 0) {
            snd_queue_sfx(g, SFX_REFUSED);
        }
        return false;
    }

    u8 flags = (u8)((m->flags & (MECH_ACTIVE | MECH_TEAM2)) | UNIT_CARRIED);
    u32 slot;
    if (unit_create_carried(g, flags, m->prod_type, m->prod_order,
                            &slot) != 0) {
        return false;
    }
    const HzMechStats *kidou = &g->assets->mech_stats;  /* UnitStats_KIDOU */
    m->flags |= MECH_CARRYING;
    m->carried_slot = slot;
    m->carried_order = m->prod_order;
    m->form = HZ_FORM_CARRYING;
    m->prod_state = PROD_NONE;
    m->max_speed = kidou->max_speed[HZ_FORM_CARRYING];  /* +$22 */
    u8 use = kidou->fuel_use[HZ_FORM_CARRYING];         /* +$25 */
    if (g->game_type >= 0
        && ((g->mech[0].flags | g->mech[1].flags) & MECH_CPU)
        && g->options[1] == 0) {
        use = (u8)(use >> 1);
    }
    m->fuel_use = use;
    snd_queue_sfx(g, SFX_LIFT);
    return true;
}

/* TerrainEv_Outpost (type 2): the outpost whose index Map_ScanOutposts
 * left in bits 10-13 of the event tile's layout word. Service and pick-up;
 * then only an INFANTRY unit goes on, raising its side's hold (the
 * capture: unit_capture). */
static void terrain_ev_outpost(HzGame *g, HzMech *m, int kind)
{
    int index = (g->terrain_event3 >> 10) & 0x0F;
    outpost_service(g, m, index);
    outpost_pick_up_product(g, m, kind, index);
}

/* TerrainEv_HQ (type 3): the same at an HQ, record $10 + the terrain
 * word's b8 (the HQ's team). */
static void terrain_ev_hq(HzGame *g, HzMech *m, int kind)
{
    int index = HZ_POST_HQ + ((g->terrain_event & 0x100) ? 1 : 0);
    outpost_service(g, m, index);
    outpost_pick_up_product(g, m, kind, index);
}

/* TerrainEv_Damage (type 8, VULKAN's ground): a robot on frames with
 * v_unit_ai_group 0, unless already destroyed, loses $20 HP; past 0 it is
 * destroyed (HP $180 for Mech_Update's debris countdown). A jet flies
 * over it. */
static void terrain_ev_damage(HzGame *g, HzMech *m, int kind)
{
    if (kind != HZ_OBJ_ROBOT) {
        return;
    }
    if (g->unit_ai_group != 0) {
        return;
    }
    if (m->flags & MECH_DESTROYED) {
        return;
    }
    if (m->hp >= 0x20) {
        m->hp = (u16)(m->hp - 0x20);
        snd_queue_sfx(g, SFX_SIZZLE);
        return;
    }
    m->flags |= MECH_DESTROYED;
    m->hp = 0x180;
    snd_queue_sfx(g, SFX_MECH_DESTROYED);
}

/* Obj_TerrainEvent ($8764) for mech m, `kind` HZ_OBJ_ROBOT or HZ_OBJ_JET
 * (a jet for any form but the robot). mech_terrain_flow is
 * cleared first (it is already: Mech_Update cleared it before
 * Mech_CheckCollision), then the handler for the event's type. The flow
 * handlers (6, 9, $A) act on robots only, so a robot's next move feels
 * the ground it now stands on. */
void mech_terrain_event(HzGame *g, HzMech *m, int kind)
{
    m->terrain_flow = 0;
    u16 event = g->terrain_event;
    u8 param = (u8)(event >> 8);
    switch (event & 0x0F) {
    case 0x0:   /* TerrainEv_None0 */
        break;
    case 0x1:   /* TerrainEv_ShotHitFx: shots only */
        break;
    case 0x2:
        terrain_ev_outpost(g, m, kind);
        break;
    case 0x3:
        terrain_ev_hq(g, m, kind);
        break;
    case 0x4:   /* TerrainEv_None4 */
        break;
    case 0x5:   /* TerrainEv_ShotDestroy: shots only */
        break;
    case 0x6:   /* TerrainEv_Current: a current (b7 clear: direction
                   code b4-b6, strength b0-b3) */
        if (kind == HZ_OBJ_ROBOT) {
            m->terrain_flow = (u8)(param & 0x7F);
        }
        break;
    case 0x7:   /* TerrainEv_ShotHQ: shots only */
        break;
    case 0x8:
        terrain_ev_damage(g, m, kind);
        break;
    case 0x9:   /* TerrainEv_Slippery: an acceleration (b7 alone, the
                   step b0-b4) */
        if (kind == HZ_OBJ_ROBOT) {
            m->terrain_flow = (u8)(0x80 | (0x1F - (param & 0x1F)));
        }
        break;
    case 0xA:   /* TerrainEv_Slow: a slowdown (b7 and b6, how much
                   b0-b1) */
        if (kind == HZ_OBJ_ROBOT) {
            m->terrain_flow = (u8)(0xC0 | (param & 0x03));
        }
        break;
    default:    /* $B-$F: TerrainEv_NoneB ... _NoneF */
        break;
    }
}

/* Outpost_ShowOwner ($8B3A): outpost `post` was just taken; the four
 * corner blocks of the 3 x 3 round it become the owner's flags, blocks
 * `block` ($1D0 P1, $1C0 P2), + 1, + 8, + 9 (top left, top right, bottom
 * left, bottom right), each through a MapFx record (active, countdown and
 * reload 0: drawn on the next MapFx_Update) at (x, y) - $20 (+ $40 right
 * / down), layout offset ((y - $20) & $7E0) x 4 + (x - $20) / 32 x 2 (+
 * 4, + $100). The records stop at the first MapFx_Alloc that fails; the
 * sound either way. */
static void outpost_show_owner(HzGame *g, const HzPost *post, u16 block)
{
    static const struct {
        u16 dx, dy, dofs, dblock;
    } corner[4] = {
        { 0x00, 0x00, 0x000, 0 },
        { 0x40, 0x00, 0x004, 1 },
        { 0x00, 0x40, 0x100, 8 },
        { 0x40, 0x40, 0x104, 9 },
    };
    u16 x = (u16)(post->x - 0x20);
    u16 y = (u16)(post->y - 0x20);
    u16 ofs = (u16)(((y & 0x7E0) << 2) + (x >> 5) * 2);
    for (int i = 0; i < 4; i++) {
        HzMapFx *fx = mapfx_alloc(g);
        if (fx == NULL) {
            break;
        }
        fx->flags = 0x80;
        fx->b1 = 0;
        fx->countdown = 0;
        fx->reload = 0;
        fx->x = (s16)(x + corner[i].dx);
        fx->y = (s16)(y + corner[i].dy);
        fx->block = (u16)(block + corner[i].dblock);
        fx->layout_ofs = (u16)(ofs + corner[i].dofs);
    }
    snd_queue_sfx(g, SFX_POST_TAKEN);
}

/* Outpost_Service ($8A14) for a unit: on frames with v_unit_ai_group 0,
 * an own outpost or HQ repairs and refuels it as its own type
 * (Obj_RepairRefuel), and nothing more. */
static void unit_service(HzGame *g, HzUnit *u, int index)
{
    if (g->unit_ai_group != 0 || !post_is_own(g, u->flags, index)) {
        return;                                         /* Outpost_ServiceExit */
    }
    obj_repair_refuel(unit_stats_for(g, u->type), &u->hp, &u->fuel,
                      &u->ammo, &u->missiles);
}

/* TerrainEv_Outpost's capture (after Outpost_Service; Outpost_PickUpProduct
 * is a jet's only): an INFANTRY not carried or dying raises its side's
 * hold on the outpost if below 4, and is used up: its flags cleared
 * (Units_UpdateTeam then leaves its grid cell clear), the team's
 * v_unit_count and INFANTRY count - 1; the other side's hold - 1 (floor
 * 0). Still below 4: the hold's sound. At 4, unless the outpost is
 * already its side's: the owner flags, v_outpost_count moved over (from
 * neutral, or from the other side) and Outpost_ShowOwner. At a hold of 4
 * already the INFANTRY stays as it is. */
static void unit_capture(HzGame *g, HzUnit *u, int index)
{
    if (u->type != UNIT_INFANTRY || (u->flags & (UNIT_DYING | UNIT_CARRIED))) {
        return;
    }
    HzPost *post = &g->posts[index];
    int team = (u->flags & UNIT_TEAM2) ? 1 : 0;
    u8 *own = &post->hold[team];
    u8 *other = &post->hold[team ^ 1];
    u8 owner = (u8)(POST_USED | POST_OWNED | (team ? POST_TEAM2 : 0));
    if (*own >= 4) {
        return;
    }
    (*own)++;
    u->flags = 0;
    g->unit_count[team]--;
    g->unit_type_count[team][UNIT_INFANTRY]--;
    *other = (*other != 0) ? (u8)(*other - 1) : 0;
    if (*own < 4) {
        snd_queue_sfx(g, SFX_HOLD_RAISED);
        return;
    }
    if (post->flags == owner) {
        return;
    }
    u8 old = post->flags;
    post->flags = owner;
    u8 from_other = 1;
    if (!(old & POST_OWNED)) {
        g->outpost_count[2]--;                          /* neutral */
        from_other = 0;
    }
    if (!team) {
        g->outpost_count[0]++;
        g->outpost_count[1] = (u8)(g->outpost_count[1] - from_other);
        outpost_show_owner(g, post, 0x1D0);
    } else {
        g->outpost_count[1]++;
        g->outpost_count[0] = (u8)(g->outpost_count[0] - from_other);
        outpost_show_owner(g, post, 0x1C0);
    }
}

/* TerrainEv_Damage (type 8) for a unit: on v_unit_ai_group 0 and 1, unless
 * already dying, $30 HP off; past 0 (below: a unit left at 0 lives) it
 * dies (HP $10 for Units_UpdateTeam's explosion), and an FWA moves 4 px
 * up and left with the explosion's size ($A00) and first frame
 * (ExplosionFrames[0]). */
static void unit_terrain_damage(HzGame *g, HzUnit *u)
{
    if (g->unit_ai_group > 1) {
        return;
    }
    if (u->flags & UNIT_DYING) {
        return;
    }
    if (u->hp >= 0x30) {
        u->hp = (u16)(u->hp - 0x30);
        snd_queue_sfx(g, SFX_SIZZLE);
        return;
    }
    u->flags |= UNIT_DYING;
    u->hp = 0x10;
    if (u->type == UNIT_FWA) {
        u->x = (s16)(u->x - 4);
        u->y = (s16)(u->y - 4);
        u->sprite_size = 0x0A00;
        u->sprite_attr = g->assets->explosion_frames[0];
    }
    snd_queue_sfx(g, unit_on_foot(u->type, repair_on(g))
                     ? SFX_INFANTRY_DESTROYED : SFX_UNIT_DESTROYED);
}

/* Obj_TerrainEvent ($8764) for a unit (from Units_UpdateTeam):
 * unit_push cleared, then the handler for the event's type: outposts and
 * HQs service it (and an INFANTRY captures an outpost), the ground's
 * flows set unit_push for its next move, VULKAN's ground hurts it. */
void unit_terrain_event(HzGame *g, HzUnit *u)
{
    u->push = 0;
    u16 event = g->terrain_event;
    u8 param = (u8)(event >> 8);
    switch (event & 0x0F) {
    case 0x2: {                                         /* TerrainEv_Outpost */
        int index = (g->terrain_event3 >> 10) & 0x0F;
        unit_service(g, u, index);
        unit_capture(g, u, index);
        break;
    }
    case 0x3:                                           /* TerrainEv_HQ */
        unit_service(g, u, HZ_POST_HQ + ((event & 0x100) ? 1 : 0));
        break;
    case 0x6:                                           /* TerrainEv_Current */
        u->push = (u8)(param & 0x7F);
        break;
    case 0x8:
        unit_terrain_damage(g, u);
        break;
    case 0x9:                                           /* TerrainEv_Slippery */
        u->push = (u8)(0x80 | (0x1F - (param & 0x1F)));
        break;
    case 0xA:                                           /* TerrainEv_Slow */
        u->push = (u8)(0xC0 | (param & 0x03));
        break;
    default:    /* 0, 4, $B-$F nothing; 1, 5, 7 shots only */
        break;
    }
}

/* TerrainEv_ShotHQ (type 7, the ring of blocks round an HQ): a unit's
 * shot of the other side than the HQ (terrain b8) takes power / 2 off its
 * HP, floor 0. Cut to 3/4 for a shot against the CPU's HQ: on A/B while
 * v_cpu_boost is on for the other side; on C when the HQ's side is a CPU
 * and the shooter's a human; on D halved instead (min 1). The HQ's mech
 * gets the HQ alert ($B4 frames, if not already on); the shot's owner,
 * unless gone, learns its shot hit an HQ (hit victim $66). */
static void terrain_ev_shot_hq(HzGame *g, HzShot *s)
{
    if (!(s->flags & SHOT_FROM_UNIT)) {
        return;
    }
    u16 event = g->terrain_event;
    if (!((((u16)s->flags << 4) ^ event) & 0x100)) {   /* SHOT_TEAM2 << 4 */
        return;
    }
    int side = (event & 0x100) ? 1 : 0;
    HzPost *hq = &g->posts[HZ_POST_HQ + side];
    HzMech *own = &g->mech[side];
    HzMech *other = &g->mech[side ^ 1];
    u16 damage = (u16)(s->power >> 1);
    u8 difficulty = g->options[1];
    if (difficulty < 2) {
        if ((s16)g->cpu_boost < 0
            && (((g->cpu_boost & 0xFF) ^ s->flags) & SHOT_TEAM2)) {
            damage = (u16)(damage - (damage >> 2));
        }
    } else if (!(other->flags & MECH_CPU) && (own->flags & MECH_CPU)) {
        if (difficulty == 2) {
            damage = (u16)(damage - (damage >> 2));
        } else {
            damage = (u16)(damage >> 1);
            if (damage == 0) {
                damage = 1;
            }
        }
    }
    u16 hp = post_hq_hp(hq);
    hp = (hp >= damage) ? (u16)(hp - damage) : 0;
    post_set_hq_hp(hq, hp);
    if (own->hq_alert == 0) {
        own->hq_alert = 0xB4;
    }
    if (!(s->flags & SHOT_ORPHAN)) {
        if (s->owner.kind == REF_MECH) {
            HzMech *o = &g->mech[s->owner.side];
            o->hit_victim = ref_hq(side);
            o->shot_flags |= OWN_SHOT_HIT;
        } else {
            HzUnit *o = ref_unit_ptr(g, s->owner);
            o->hit_victim = ref_hq(side);
            o->shot_flags |= OWN_SHOT_HIT;
        }
    }
    snd_queue_sfx(g, SFX_HQ_HIT);
}

/* Obj_TerrainEvent ($8764) for a shot (+$2B isn't cleared). Of the
 * handlers only 1, 5 and 7 take shots: Outpost_Service and
 * _PickUpProduct want a unit or a mech, the rest units and robots. Types
 * 1 (TerrainEv_ShotHitFx) and 5 (TerrainEv_ShotDestroy) make MapFx records
 * (not ported, though MapFx_Alloc and MapFx_Update now are); no shipping
 * map has an event tile in a block of either type. */
void shot_terrain_event(HzGame *g, HzShot *s)
{
    switch (g->terrain_event & 0x0F) {
    case 0x1:   /* TerrainEv_ShotHitFx: not ported (MapFx) */
        break;
    case 0x5:   /* TerrainEv_ShotDestroy: not ported (MapFx) */
        break;
    case 0x7:
        terrain_ev_shot_hq(g, s);
        break;
    default:
        break;
    }
}
