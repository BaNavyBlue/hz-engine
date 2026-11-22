#include "game.h"
#include "nm_atlas.h"
#include "tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(NM_OUTPOSTS == HZ_POST_OUTPOSTS,
               "nightmare.h keeps one an outpost");

/* (Session 57) The nightmare AI: see nightmare.h.
 *
 * Every frame it looks at the whole game (as the original's AI does) and
 * sets the mech's pad, the only thing it changes: no money given to
 * itself, no heading set straight. What it does is a task (NM_TASK_*),
 * each a few steps over many frames: buy through the order menu, collect
 * the unit at an own base, carry it (by way of own bases to refuel) and
 * put it down, refuel, fight on foot, chase the enemy jet. decide() picks
 * the next task when one ends, and some things (the HQ under attack, the
 * fuel) break into any task.
 *
 * It flies round the enemy's missiles: a map of the danger (where an
 * enemy SAM or GRM would fire at it, where enemy guns reach a robot) is
 * made each frame, and flights are planned over the 64 x 64 blocks (A*). */

/* -- tuning ------------------------------------------------------------ */

/* Its tuning values: the name hz_arena's --param gives, the default, what
 * it is. tools/hznmtrain.py tunes them in self-play. */
#define NM_PARAMS(X) \
    X(RUSH_TYPE,     rush_type,     UNIT_TAX, \
      "the unit it sends at the enemy HQ (with attack HQ)") \
    X(DROP_RANGE,    drop_range,    150, \
      "no attack unit put down nearer the enemy HQ than this, px") \
    X(DROP_GD,       drop_gd,       3, \
      "a drop this many blocks' drive from the HQ's range will do; " \
      "with none in reach of a base it takes an outpost nearer (-1 never)") \
    X(DRIVE,         drive,         3000, \
      "(session 68) an attack drop only where a unit put down alone hits " \
      "the enemy HQ (or drives on still) within this many frames " \
      "(0 untried)") \
    X(DRIVE_GAIN,    drive_gain,    128, \
      "... driving on, at least this much nearer the HQ than it was put " \
      "down, px (0 any); on a human's pad (as the CPU's brain any)") \
    X(DRIVE_NEW,     drive_new,     4, \
      "... at most this many places newly tried a think") \
    X(DRIVE_FUEL,    drive_fuel,    7200, \
      "(session 70) ... against a human, one driving on when DRIVE's " \
      "frames are up followed this many frames more at most: run dry " \
      "before it hits the HQ, it fails (0 not followed)") \
    X(REGION,        region,        1, \
      "(session 70) 1: against a human, an attack unit never put down " \
      "where no ground joins it to the enemy HQ: going down so blind, a " \
      "TAX-52 is ordered to stand guard there, anything else is a guard " \
      "at home") \
    X(BAIL_HP,       bail_hp,       64, \
      "(session 68) carrying, hit and under this much HP: the unit put " \
      "down where the jet is, if it can be (0 never)") \
    X(DEATH_AVOID,   death_avoid,   10800, \
      "(session 68) against a human, for this many frames after the mech " \
      "was destroyed, no fight away from the HQ (out of DEF_RADIUS) with " \
      "an enemy unit within DEATH_RADIUS px of the place (0 off)") \
    X(DEATH_RADIUS,  death_radius,  288, \
      "... px (0 no such places)") \
    X(BUY_FIRST,     buy_first,     1, \
      "(session 68) 1: against a human, a unit bought before a fight away " \
      "from the HQ") \
    X(BOUNCES,       bounces,       2, \
      "(session 68) an attack unit carried turned back for fuel this many " \
      "times on its last leg: made a guard, put down at home (0 never)") \
    X(SAFE_SERVICE,  safe_service,  0, \
      "(session 68) 1: refuelled at the nearest base out of the enemy's " \
      "missiles the fuel reaches, if there is one") \
    X(COLD_JUDGE,    cold_judge,    0, \
      "(session 68) an attack unit put down and the enemy HQ shot by none " \
      "in this many frames after: no attack drop within COLD_R px of the " \
      "place for COLD_FRAMES (0 never)") \
    X(COLD_R,        cold_r,        160, \
      "... px") \
    X(COLD_FRAMES,   cold_frames,   10800, \
      "... frames") \
    X(SPOT_HUMAN,    spot_human,    1, \
      "(session 68) 1: against a human, a spot its HQ was shot from that " \
      "no guard reaches gets one, however many stand elsewhere") \
    X(SPOT_WET,      spot_wet,      1, \
      "(session 68) 1: a spot on the water its HQ was shot from that no " \
      "boat or TURRET-2 of its reaches gets one, however many stand " \
      "elsewhere") \
    X(BASE_LOST,    base_lost,     1, \
      "(session 68) 1: a unit carried to a base to refuel at that the " \
      "enemy takes on the way: the next leg planned again") \
    X(HOME_SERVICE,  home_service,  1, \
      "(session 68) 1: against a human, its HQ being shot, refuelled at " \
      "the HQ if the fuel reaches it") \
    X(WALL_RING,     wall_ring,     1, \
      "(session 68) 1: against a human, its wall all round its HQ as it " \
      "is built, where fewest stand for what the place is worth (facing " \
      "the enemy, and the spots its HQ was shot from, more)") \
    X(WALL_GRM,      wall_grm,      0, \
      "(session 68) 1: against a human, its wall by thirds: SAM-42s, " \
      "GRM-34As and TAX-52s (else half SAM-42s; tried, off)") \
    X(WALL_SPOTS,    wall_spots,    0, \
      "(session 68) 1: against a human, its wall stands by the spots its " \
      "HQ was shot from as well as facing the enemy HQ (tried, off)") \
    X(ROUTE_FUEL,    route_fuel,    1, \
      "(session 68) 1: a leg from the mech planned on the flight round the " \
      "missiles (plan_path's way), not the straight line") \
    X(ATLAS,         atlas,         1, \
      "(session 68) 1: the terrain atlas (nm_atlas.h) answers the drive " \
      "for a block it tried: a drop never where it failed, the quickest " \
      "the best; 2: and for any place, its nearest block's") \
    X(HUNT,          hunt,          0, \
      "(session 68) against a human, every this-many-th attack unit a " \
      "hunter: a TAX-52 guarding by an enemy SAM-42 whose missiles cover " \
      "its HQ, its outposts or the enemy HQ's (0 none)") \
    X(HUNT_NEAR,     hunt_near,     512, \
      "... SAM-42s this near his HQ, px") \
    X(HUNT_COVER,    hunt_cover,    12, \
      "... put down where fewer than this many enemy missile units reach") \
    X(HUNT_MAX,      hunt_max,      2, \
      "... this many at a SAM-42 at a time; one covering its own HQ or " \
      "outposts sooner, before the garrison") \
    X(REHUNT,        rehunt,        0, \
      "... a hunter with no SAM-42 left by it lifted to the next") \
    X(HUNT_HOME,     hunt_home,     2, \
      "... with HUNT 0, hunters only for SAM-42s covering its own HQ or " \
      "outposts; (session 77) 2: two at a time") \
    X(ATLAS_LINE,    atlas_line,    1, \
      "(session 68) 1: against a human (2: every game), an attack unit " \
      "put down short of its drop (out of the fuel's reach, or the drop " \
      "in missiles) only where the atlas doesn't fail; none in reach " \
      "from a base on a full tank, a guard") \
    X(RESCUE,        rescue,        1, \
      "(session 68) 1: an attack unit that stalls (its fuel going, the " \
      "unit not) is lifted, once, and put down again") \
    X(PARK,          park,          25, \
      "(session 68) an attack unit carried on half a tank or less put " \
      "down where the atlas's drive takes its fuel, this % to spare, " \
      "before a base fills it (0 off)") \
    X(SAFE_DROP,    safe_drop,     3, \
      "(session 68) an attack unit never put down where this many enemy " \
      "SAMs and GRMs reach, nor one with the mech under half its HP, the " \
      "old ways' places too (0 any)") \
    X(NEST,          nest,          448, \
      "(session 68) no drop in reach whose unit hits the enemy HQ: one " \
      "within this many px of a SAM or GRM by it whose unit hurts one " \
      "(the nest test; past TYPE A the CPU's SAM-42s close every drop) " \
      "(0 none)") \
    X(DEFEND,        defend,        2, \
      "(session 68) once enemy units have shot its HQ: this many TAX-52s " \
      "kept where they stood (0 none)") \
    X(HOME_EARLY,    home_early,    0, \
      "(session 68) on a human's pad against the CPU on TYPE C and D: " \
      "this many TAX-52s standing at its HQ before any other buy") \
    X(SPOT_COUNT,    spot_count,    0, \
      "... 1: counting only its guards at the spots (a wall's TAX-52s " \
      "not), 0 all round the HQ") \
    X(DEFEND_GUARD,  defend_guard,  0, \
      "... 1 on guard near (DF-F02A, worse), 0 standing") \
    X(DEFEND_RICH,   defend_rich,   1500, \
      "... and, its HQ shot in the last 10 s, one more for each this " \
      "much money (8 in all at most; 0 none)") \
    X(DEFEND_FIRST,  defend_first,  2, \
      "... 2: and then guards before a fight, any unit waiting made " \
      "one; 1 the attack unit only; 0 bought as other units") \
    X(FUEL_MARGIN,   fuel_margin,   210, \
      "fuel kept over a trip") \
    X(DEF_RADIUS,    def_radius,    640, \
      "enemy units this near its HQ are fought, px") \
    X(HUNT_RADIUS,   hunt_radius,   320, \
      "... and, less, those this near its outposts") \
    X(HQ_ALARM,      hq_alarm,      2500, \
      "a threat to the HQ this great: the carried unit put down for it") \
    X(FIGHT_RANGE,   fight_range,   72, \
      "where it lands from a fight's target by choice, px") \
    X(LAND_REACH,    land_reach,    76, \
      "on the way, the target this near: down where it is, px") \
    X(DOG_RANGE,     dog_range,     700, \
      "the enemy jet carrying a unit this near is chased, px") \
    X(DOG_PAD,       dog_pad,       0, \
      "... on a human's pad (the server's AI), px (0 never)") \
    X(DOG_GUARD,     dog_guard,     400, \
      "(session 69) ... and against a human, one this near its HQ or an " \
      "outpost of its own chased within DOG_RANGE, till it puts the unit " \
      "down, px (0 never)") \
    X(IDLE_DOG,      idle_dog,      0, \
      "(session 69) against a human, nothing else to do: the enemy jet " \
      "this near chased, carrying or not, px (0 never)") \
    X(NEST_DEATHS,   nest_deaths,   3, \
      "(session 69) against a human, destroyed this many times within " \
      "DEF_RADIUS of its HQ in DEATH_AVOID: no enemy SAM-42 or GRM-34A " \
      "whose gun doesn't reach its HQ fought (0 never)") \
    X(NEST_DEATHS_ALL, nest_deaths_all, 0, \
      "(session 69) 1: ... nor any in their missiles' reach") \
    X(IDLE_BROKE,    idle_broke,    0, \
      "(session 69) 1: ... only short of the money for INFANTRY, nothing " \
      "on order") \
    X(AIR_GUARD,     air_guard,     200, \
      "the enemy jet shooting it this near: turned on, px") \
    X(DOG_ANY,       dog_any,       0, \
      "any enemy jet this near is chased (0 none), px") \
    X(EVADE,         evade,         120, \
      "carrying, the enemy jet shooting it this near: unit put down, px") \
    X(CAPTURE_MIN,   capture_min,   3, \
      "outposts it takes, at least") \
    X(CAPTURE_MAX,   capture_max,   8, \
      "... and at most (forward ones, for drops in reach)") \
    X(CAPTURE_REACH, capture_reach, 760, \
      "how far from its bases it goes to take one, px") \
    X(CAPTURE_NEAR,  capture_near,  320, \
      "none nearer the enemy HQ than this, px") \
    X(CAPTURE_REST,  capture_rest,  10800, \
      "(session 68) an outpost it sent 6 INFANTRY at and didn't take: " \
      "left this long times its failures in a row, frames (0 tried " \
      "again at once)") \
    X(CAPTURE_ALL,   capture_all,   3, \
      "... 3: while it has fewer than CAPTURE_MIN, past it from the " \
      "second (the turtle's outposts were taken in the end); 2: past it " \
      "never; 0 always; 1 no outpost at all, the failures those of any") \
    X(CAPTURE_GUARDS, capture_guards, 2, \
      "(session 68) on a human's pad against the CPU on TYPE C and D, " \
      "none with this many enemy armed units within 128 px (0 any)") \
    X(GUARDS_HUMAN,  guards_human,  0, \
      "(session 68) 1: ... and against a human") \
    X(CAPTURE_AHEAD, capture_ahead, 0, \
      "(session 68) 1: past CAPTURE_MIN, only outposts nearer the enemy " \
      "HQ than its bases (0 any)") \
    X(FINISH_HP,     finish_hp,     256, \
      "(session 68) the enemy HQ's HP at most this: attack units before " \
      "outposts for the front (0 never)") \
    X(CAPTURE_CALM,  capture_calm,  36000, \
      "(session 68) ... and its HQ shot in the last this many frames " \
      "(0 never)") \
    X(ATTACK_GAP,    attack_gap,    10800, \
      "(session 68) ... and no attack unit put down for this many " \
      "frames (0 never)") \
    X(FRONT_OPEN,    front_open,    0, \
      "(session 68) 1: an outpost taken for a nearer drop only when the " \
      "enemy's missiles leave one open; 0 also when they don't") \
    X(MISSILE_COST,  missile_cost,  60, \
      "a block in missile reach costs a flight this many blocks") \
    X(ARC_CORE,      arc_core,      80, \
      "(session 68) a flight's missile blocks only those this near an " \
      "enemy SAM-42 or GRM-34A, px; past them, round, ARC_RING a block " \
      "(0 all its reach)") \
    X(ARC_RING,      arc_ring,      3, \
      "... the rest of its reach costs a flight this many blocks") \
    X(ARC_GOAL,      arc_goal,      10, \
      "... but not within this many blocks of the flight's goal") \
    X(HOME_GRAVE,    home_grave,    0, \
      "(session 68) DEATH_AVOID within DEF_RADIUS too, for an enemy unit " \
      "whose gun doesn't reach its HQ") \
    X(TENDED,        tended,        3600, \
      "(session 68) the mech leaves alone an enemy SAM-42 or GRM-34A whose " \
      "gun doesn't reach its HQ and whose HP or missiles went up in the " \
      "last this many frames (repaired; 0 never)") \
    X(TENDED_CARRY,  tended_carry,  1, \
      "(session 68) against a human, 1: ... an enemy unit carried kept " \
      "track of (repaired as it is carried)") \
    X(HOME_GRAVES,   home_graves,   3, \
      "(session 68) against a human, DEATH_AVOID within DEF_RADIUS too, " \
      "for an enemy unit whose gun doesn't reach its HQ, once the mech " \
      "was destroyed this many times within DEATH_RADIUS of it (0 never)") \
    X(PATH_FUEL,     path_fuel,     1, \
      "(session 68) against a human, 1: a flight round the missiles " \
      "longer than its fuel goes straight") \
    X(BASE_FUEL,     base_fuel,     0, \
      "(session 68) against a human, deciding at a base with less than " \
      "this % of its fuel, carrying nothing, its HQ not shot in the last " \
      "10 s: refuelled there first (0 never)") \
    X(DROP_FLY,      drop_fly,      24, \
      "(session 68) against a human, attack units, INFANTRY (8 px) and " \
      "hunters put down flying over the place at speed, within this many " \
      "px of it (0 stopped there)") \
    X(FLY_KINDS,     fly_kinds,     7, \
      "... for these: 1 attack units, 2 INFANTRY, 4 hunters") \
    X(SAFE_FLY,      safe_fly,      0, \
      "(session 68) with DROP_FLY, SAFE_DROP this many, and never within " \
      "ARC_CORE of one (0 SAFE_DROP)") \
    X(CAPTURE_HUMAN, capture_human, 0, \
      "(session 68) against a human, CAPTURE_REACH this, px (0 as it)") \
    X(CAPTURE_BEHIND, capture_behind, 1, \
      "... only while the enemy holds more outposts") \
    X(SIEGE,         siege,         1, \
      "1: fight the enemy HQ's guards before and while it attacks") \
    X(SIEGE_R,       siege_r,       240, \
      "its guards: enemy units this near it, px") \
    X(SIEGE_CLEAR,   siege_clear,   0, \
      "guards left that let an attack unit go in") \
    X(SIEGE_HP,      siege_hp,      176, \
      "the HP it sets off to a siege fight with") \
    X(SIEGE_LEAVE,   siege_leave,   104, \
      "the HP it leaves one at") \
    X(SIEGE_HOLD,    siege_hold,    900, \
      "frames an attack unit, ready, waits for the guards to fall") \
    X(SVC_FUEL,      svc_fuel,      90, \
      "a service ends at this % of the fuel,") \
    X(SVC_HP,        svc_hp,        75, \
      "this % of the HP") \
    X(SVC_AMMO,      svc_ammo,      48, \
      "and this much ammo") \
    X(JITTER,        jitter,        48, \
      "random spread in its choice of places") \
    X(MECH_FIGHT,    mech_fight,    1, \
      "the enemy mech on foot fought: 0 never, 1 as the CPU, 2 always, " \
      "3 as the CPU and against a human") \
    X(MECH_HQ,       mech_hq,       20, \
      "... its weight by the HQ (an attack-HQ TAX's is 16)") \
    X(MECH_ESCORT,   mech_escort,   0,  \
      "... and by its own units going in") \
    X(STYLE,         style,         0, \
      "0 its own; 1 a player's way (hz_arena's turtle): a wall of " \
      "standing units round its HQ, then attack units by the enemy HQ") \
    X(WALL,          wall,          10, \
      "(style 1) standing units round its HQ before it attacks") \
    X(WALL_R,        wall_r,        160, \
      "(style 1) how far out from the HQ they stand, px") \
    X(NEST_UNITS,    nest_units,    0, \
      "(style 1) then GRM-34As and SAM-42s standing round the enemy HQ, " \
      "this many, before attack units (0 none)") \
    X(FLANK,         flank,         0, \
      "(session 68) attack drops behind the enemy HQ: this much worse a " \
      "px a drop stands towards its own HQ (0 none; a player's way to " \
      "try against)") \
    X(GARRISON,      garrison,      0, \
      "(style 0) standing units it keeps round its HQ (0 none)") \
    X(GARRISON_CPU,  garrison_cpu,  40, \
      "(session 68) ... on a human's pad against the CPU on TYPE C and " \
      "D, if more (40: no attack unit till then)") \
    X(GARRISON_HUMAN, garrison_human, 40, \
      "(session 68) ... on a human's pad against a human, if more") \
    X(GARRISON_SHOT, garrison_shot, 1, \
      "(session 68) 1: ... only once its HQ has been shot") \
    X(FUTILE,        futile,        8, \
      "(session 68) against a human, this many of its attack units " \
      "destroyed since the enemy HQ last lost HP: a garrison (0 never)") \
    X(FUTILE_SIZE,   futile_size,   40, \
      "... of this many, if more") \
    X(FUTILE_TIME,   futile_time,   18000, \
      "... and the enemy HQ not shot in this many frames") \
    X(FUTILE_BEHIND, futile_behind, 1, \
      "... 1: and the enemy holding more outposts") \
    X(CAPTURE_EVEN,  capture_even,  2, \
      "(session 68) against a human, outposts taken while the enemy " \
      "holds this many more than it, as below CAPTURE_MIN (0 never)") \
    X(HUNT_TAKE,     hunt_take,     0, \
      "(session 68) ... and 1: hunters for the SAM-42s by an enemy " \
      "outpost it would take so") \
    X(CAPTURE_ESCORT, capture_escort, 0, \
      "(session 68) against a human, 1: a TAX-52 on guard near put down " \
      "first by an outpost it takes that his mech stood by on foot in " \
      "the last minute, one in 2 minutes at most; 2: none taken there " \
      "then") \
    X(HOME_POST,     home_post,     640, \
      "(session 68) against a human, an enemy outpost within this many px " \
      "of its HQ taken back first, whatever CAPTURE_MIN and its HQ shot " \
      "or not (0 never)") \
    X(HOME_POST_WALL, home_post_wall, 1, \
      "... 1: only with half its garrison up") \
    X(HOME_POST_HIT, home_post_hit, 18000, \
      "... and the enemy HQ shot once, then not for this many frames " \
      "(its attacks stalled)") \
    X(HOME_POST_GUARDS, home_post_guards, 1, \
      "... 1: none with CAPTURE_GUARDS enemy armed units by it") \
    X(HOME_POST_SHOT, home_post_shot, 18000, \
      "... and its own HQ shot in the last this many frames (0 any)") \
    X(POST_GUARD,    post_guard,    0, \
      "(session 68) against a human, TAX-52s on guard put down by each of " \
      "its outposts: this many, the first before its HQ's garrison (0 " \
      "none)") \
    X(DROP_SPREAD,   drop_spread,   0, \
      "(session 68) against a human, an attack drop within DROP_SPREAD_R " \
      "px of one of its last 4 (in 10 minutes) this much worse a drop, " \
      "each (a block's drive 32; 0 none)") \
    X(DROP_SPREAD_R, drop_spread_r, 64, \
      "... px") \
    X(DROP_INSIDE,   drop_inside,   0, \
      "(session 68) against a human, with DROP_FLY: attack drops in the " \
      "enemy's missiles' reach too, where fewer than this many reach, " \
      "never within ARC_CORE of one (0 none)") \
    X(DROP_INSIDE_COST, drop_inside_cost, 32, \
      "... each one reaching a drop this much worse") \
    X(DROP_INSIDE_SAM, drop_inside_sam, 1, \
      "... 1: only where SAM-42s alone reach (they shoot only the jet)") \
    X(POST_GUARD_FIRST, post_guard_first, 1, \
      "... 1: the first before its HQ's garrison") \
    X(POST_GUARD_CALM, post_guard_calm, 1, \
      "(session 68) 1: POST_GUARD only while its HQ wasn't shot in the " \
      "last minute") \
    X(POST_RAID,     post_raid,     1, \
      "(session 69) against a human, TAX-52s on guard kept by an outpost " \
      "of its own the enemy's INFANTRY came within POST_RAID_R of in the " \
      "last POST_RAID_TIME frames: this many (0 none)") \
    X(POST_RAID_R,   post_raid_r,   240, \
      "... px") \
    X(POST_RAID_TIME, post_raid_time, 18000, \
      "... frames") \
    X(WAIT_NEAR,     wait_near,     0, \
      "(session 69) against a human, 1: nothing to do, it waits over its " \
      "base nearest the enemy mech, not the one nearest itself") \
    X(RAID,          raid,          0, \
      "(session 69) a player's way to try against (The owner's on 1 " \
      "November): INFANTRY sent at the outposts nearest its own bases, " \
      "the enemy's first, till it holds this many (0 none)") \
    X(ATTACK_MIX,    attack_mix,    3, \
      "(session 68) the garrison half up, every this-many-th look at " \
      "what to buy an attack unit's turn, bought if its price is there " \
      "then (0 none; MIX_BUYS)") \
    X(GARRISON_MATCH, garrison_match, 0, \
      "(session 68) on a human's pad, the garrison this % of the enemy's " \
      "standing units by its HQ, if more (0 none)") \
    X(GARRISON_CAP,  garrison_cap,  40, \
      "... up to this many") \
    X(HOME_FIGHT,    home_fight,    0, \
      "(session 68) ... there, enemy units fought within this of its " \
      "HQ, px (0: def_radius)") \
    X(READY_FIRST,   ready_first,   0, \
      "(session 68) ... there, 1: a wall or guard unit waiting picked " \
      "up before a fight short of hq_alarm") \
    X(NO_NEST_FIGHT, no_nest_fight, 0, \
      "(session 68) 1: the mech never goes for an enemy SAM-42 or " \
      "GRM-34A with missiles left") \
    X(NO_BLIND,      no_blind,      0, \
      "(session 68) 1: an attack unit only where its drive passes; none " \
      "such, it stands at home and no more are bought") \
    X(WALL_SPARE,    wall_spare,    12, \
      "(session 68) ... and standing units round its HQ bought instead, " \
      "up to this many") \
    X(BOAT_HOME,     boat_home,     3, \
      "(session 68) ... there, boats (ST-57U) of the wall on the water " \
      "nearest its HQ (one for every 4 of it), up to this many") \
    X(BOAT_SEEN,     boat_seen,     1, \
      "(session 68) 1: ... only once an enemy boat has been on the map") \
    X(BOAT_TYPE,     boat_type,     3, \
      "(session 68) ... from this TYPE up (2 C, 3 D)") \
    X(NAVAL,       naval,         1, \
      "(session 68) 1: boats (ST-57U) for attack units where no land by " \
      "the enemy HQ is out of missiles and water is (STRAND); 0 never") \
    X(GROUP,         group,         0, \
      "(style 0) attack units gathered standing at a rally point, then " \
      "sent in together (under 2: one at a time)") \
    X(RALLY,         rally,         320, \
      "... the rally point at least this far from the enemy HQ, px") \
    X(RALLY_R,       rally_r,       96, \
      "... and they stand this near it, px") \
    X(EDGE,          edge,          1, \
      "(session 73) 1: the map's outermost 8 px are places to land, stand " \
      "and put a unit down too, as the game has them (a mech's x and y go " \
      "from 0 to $7E0); 0: none within 8 px of an edge") \
    X(WALL_EDGE,     wall_edge,     0, \
      "(session 73) ... 1: its wall's places by an edge too, once its HQ " \
      "was shot from within 128 px of one; 0: none by an edge, as before " \
      "(tried, off: SPOT_MORE's guards do it)") \
    X(SPOT_MORE,     spot_more,     2, \
      "(session 73) against a human, a spot its HQ was shot from gets one " \
      "more guard for every this many hits from there, 3 at most (0: " \
      "one)") \
    X(MIX_BUYS,      mix_buys,      0, \
      "(session 73) 1: ATTACK_MIX counts the units bought, the money " \
      "kept for the attack unit's turn; 0: it counts every look at what " \
      "to buy, as it did, and the turn passes whenever its price isn't " \
      "there (tried, off: the wall went up slower)") \
    X(HOLD_BACK,     hold_back,     1, \
      "(session 74) against a human, 1: an outpost of its own the enemy's " \
      "INFANTRY took a hold in gets INFANTRY of its own for each hold " \
      "lost, before any other buy (0: its holds never looked at)") \
    X(HOLD_HOT,      hold_hot,      3, \
      "... not while this many enemy SAM-42s and GRM-34As reach the " \
      "outpost (0 whatever reaches)") \
    X(CAPTURE_FREE,  capture_free,  1, \
      "(session 74) against a human, 1: with fewer than CAPTURE_MIN " \
      "outposts and none to take in CAPTURE_REACH, one that is nobody's " \
      "is taken wherever it is, while the enemy has no attack unit out " \
      "(the INFANTRY put down as far as the fuel goes, walking on); 2: " \
      "and past CAPTURE_MIN too while its attack is not paying (the " \
      "enemy HQ not shot in ATTACK_GAP)") \
    X(ONE_WAY,       one_way,       0, \
      "(session 74) against a human, 1: INFANTRY for an outpost carried " \
      "there on the fuel one way where there and back is too far (the " \
      "jet run dry on its way home is built again at its HQ; tried, " \
      "off: no more outposts for it, two deaths more a game)") \
    X(CAPTURE_HOT,   capture_hot,   3, \
      "(session 74) against a human, an outpost in the enemy's missiles' " \
      "reach is one to take while fewer than this many SAM-42s and " \
      "GRM-34As reach it (0: none in any's reach, as before)") \
    X(FLY_STRAIGHT,  fly_straight,  1, \
      "(session 74) against a human, 1: carrying nothing, a flight goes " \
      "straight through the enemy's missiles' reach (the jet lost costs " \
      "the seconds it is built in, nothing more)") \
    X(FIGHT_ON,      fight_on,      1, \
      "(session 74) against a human, 1: a fight is not left for the fuel " \
      "home (built again the mech has a quarter of a tank), nor in the " \
      "enemy's missiles' reach for repair: it is fought till the mech " \
      "is destroyed (it was shot down on the way home)") \
    X(SUPPLY,        supply,        2, \
      "(session 74) against a human, a SUPPLY kept by its HQ once this " \
      "many of its standing units there are under a quarter of their " \
      "ammunition or missiles (0 never)") \
    X(REPAIR,        repair,        2, \
      "(session 74) ... and, in a game with the REPAIR patch, a repair " \
      "truck once this many of them are under three quarters of their " \
      "HP (0 never)") \
    X(STRIKE,        strike,        1, \
      "(session 74) against a human, 1: with nothing else to do and its " \
      "HQ not shot in the last minute, the mech goes for the enemy's " \
      "nest within STRIKE_R of its HQ, or in reach of an outpost of its " \
      "own, whatever it costs it: the unit the fewest launchers reach " \
      "first, of those the repair trucks, the SUPPLYs, then a SAM-42 or " \
      "GRM-34A, one till it is gone (0 never)") \
    X(STRIKE_R,      strike_r,      720, \
      "... px") \
    X(CAPTURE_NOBODY, capture_nobody, 1, \
      "(session 75) against a human, 1: of the outposts to take, nobody's " \
      "before the enemy's, and nobody's out of CAPTURE_REACH too while " \
      "the enemy has no attack unit out (0: the enemy's 100 px sooner, " \
      "as before)") \
    X(CAPTURE_ROBOT, capture_robot, 1, \
      "(session 75) against a human, 1: no INFANTRY sent at an outpost " \
      "not its own that his mech stood by on foot in the last minute, " \
      "those on their way to it sent to the next (0 as before)") \
    X(CAPTURE_OPEN,  capture_open,  43200, \
      "(session 75) against a human, for this many frames from the " \
      "game's start nobody's outposts taken past CAPTURE_MIN too, while " \
      "its attack keeps its turn (with CAPTURE_FREE; 0 never)") \
    X(HOME_LANES,    home_lanes,    1, \
      "(session 75) against a human, 1: what stands by its HQ (its wall, " \
      "its guards where its HQ was shot from) in blocks of 2 x 2 with a " \
      "lane 40 px wide between blocks, none within 160 px of the HQ's " \
      "middle, and its SUPPLY and repair truck put down in that ring, so " \
      "they reach every unit (0 as before)") \
    X(BUY_HOME,      buy_home,      2, \
      "(session 77) against a human, 1: a unit bought before a fight by " \
      "its HQ too while the enemy unit is not about to shoot it (farther " \
      "than its gun's reach and 64 px, 192 px for one sent at the HQ); " \
      "2: and a unit waiting to go picked up first, before that fight " \
      "or one away from the HQ (0 as before)") \
    X(NEST_WAIT,     nest_wait,     1, \
      "(session 77) against a human, 1: an enemy unit sent at its HQ is " \
      "one at the HQ for TENDED, HOME_GRAVES and NEST_DEATHS only once it " \
      "is within its gun's reach and 192 px of it: under a nest where the " \
      "mech was destroyed it is left till it comes out (0 anywhere)") \
    X(STRIKE_GIVEUP, strike_giveup, 3, \
      "(session 77) against a human, the mech destroyed this many times " \
      "in a row on strikes with nothing killed: no strike within 320 px " \
      "of there for STRIKE_REST frames, twice that the second time in a " \
      "row with nothing killed on a strike between, and so on to five " \
      "times (0 never)") \
    X(STRIKE_REST,   strike_rest,   10800, \
      "... frames") \
    X(HUNT_STRIKE,   hunt_strike,   1, \
      "(session 77) against a human, 1: hunters for the SAM-42s within " \
      "STRIKE_R of its HQ too (the nest it would strike at)")

enum {
#define NM_ENUM(id, name, def, what) P_##id,
    NM_PARAMS(NM_ENUM)
#undef NM_ENUM
    P_COUNT
};

static const int NM_DEFAULTS[P_COUNT] = {
#define NM_DEF(id, name, def, what) def,
    NM_PARAMS(NM_DEF)
#undef NM_DEF
};

static const char *const PARAM_NAME[P_COUNT] = {
#define NM_NAME(id, name, def, what) #name,
    NM_PARAMS(NM_NAME)
#undef NM_NAME
};

static const char *const PARAM_WHAT[P_COUNT] = {
#define NM_WHAT(id, name, def, what) what,
    NM_PARAMS(NM_WHAT)
#undef NM_WHAT
};

const char *nightmare_param(int i, int *def, const char **what)
{
    if (i < 0 || i >= P_COUNT) {
        return NULL;
    }
    if (def != NULL) {
        *def = NM_DEFAULTS[i];
    }
    if (what != NULL) {
        *what = PARAM_WHAT[i];
    }
    return PARAM_NAME[i];
}

/* Each side's tuning (a copy of NM_DEFAULTS until set), and the one the
 * think under way reads. */
static int  g_params[2][P_COUNT];
static bool g_params_set[2];
static int *g_param = g_params[0];

static void params_init(int p)
{
    if (!g_params_set[p]) {
        memcpy(g_params[p], NM_DEFAULTS, sizeof NM_DEFAULTS);
        g_params_set[p] = true;
    }
}

bool nightmare_set_param_side(int side, const char *name_value)
{
    const char *eq = strchr(name_value, '=');
    if (eq == NULL) {
        return false;
    }
    size_t len = (size_t)(eq - name_value);
    for (int i = 0; i < P_COUNT; i++) {
        if (strlen(PARAM_NAME[i]) == len
            && strncmp(PARAM_NAME[i], name_value, len) == 0) {
            for (int p = 0; p < 2; p++) {
                if (side < 0 || side == p) {
                    params_init(p);
                    g_params[p][i] = atoi(eq + 1);
                }
            }
            return true;
        }
    }
    return false;
}

bool nightmare_set_param(const char *name_value)
{
    return nightmare_set_param_side(-1, name_value);
}

/* -- the tasks ---------------------------------------------------------- */

enum {
    NM_TASK_DECIDE = 0,
    NM_TASK_BUY,            /* the order menu: buy_type with buy_order */
    NM_TASK_PICKUP,         /* to goal_post, A held over it */
    NM_TASK_DELIVER,        /* carry the unit to final, by way of bases */
    NM_TASK_SERVICE,        /* to goal_post, wait while it refuels */
    NM_TASK_WAIT,           /* hover at goal_post a moment */
    NM_TASK_FIGHT,          /* land by the target and shoot it */
    NM_TASK_DOGFIGHT,       /* chase the enemy jet and shoot it down */
    NM_TASK_LIFT,           /* (session 68) to a gathered unit, lift it */
    NM_TASK_COUNT
};

static const char *const TASK_NAME[NM_TASK_COUNT] = {
    "decide", "buy", "pickup", "deliver", "service", "wait", "fight",
    "dogfight", "lift"
};

/* What a unit is for. */
enum {
    NM_CARGO_ATTACK = 0,    /* put down short of the enemy HQ */
    NM_CARGO_CAPTURE,       /* put down by cargo_post */
    NM_CARGO_WALL,          /* (style 1) put down round its own HQ */
    NM_CARGO_GATHER,        /* (session 68) standing, at the rally point */
    NM_CARGO_LAUNCH,        /* ... lifted from there: attack HQ, put down
                               by the first of its group sent */
    NM_CARGO_GUARD,         /* (session 68) where the enemy shot its HQ
                               from */
    NM_CARGO_NEST,          /* (session 68, style 1) standing round the
                               enemy HQ */
    NM_CARGO_HUNT,          /* (session 68) a TAX-52 guarding by an enemy
                               SAM-42: it hunts them */
    NM_CARGO_POST,          /* (session 68) a TAX-52 guarding by its
                               outpost cargo_post */
    NM_CARGO_FIELD,         /* (session 70) an attack TAX-52 with no ground
                               to the enemy HQ: on guard at its drop */
    NM_CARGO_SUPPLY         /* (session 74) a SUPPLY for the units standing
                               round its own HQ */
};

/* The danger map's bits, a block each. */
#define DANGER_MISSILE  0x01    /* an enemy SAM or GRM fires at a mech */
#define DANGER_GUN      0x02    /* enemy guns reach a robot */
#define DANGER_JET      0x04    /* the enemy jet's blocks: jets bounce */
#define DANGER_FLY      (DANGER_MISSILE | DANGER_JET)
#define DANGER_CORE     0x08    /* (session 68) within P_ARC_CORE of one */

/* The context of one think. */
typedef struct {
    HzGame      *g;
    HzMech      *m;
    HzMech      *rival;
    HzNightmare *s;
    int          p;
    int          team;      /* 0 P1, 1 P2 */
    bool         cpu;       /* playing as the CPU (MECH_CPU) */
    u8           pad;       /* what it presses this frame */
    u8         (*danger)[64];   /* [y][x] */
    u8         (*missiles)[64]; /* [y][x]: (s. 68) the SAMs and GRMs
                                   whose reach DANGER_MISSILE is, 255 at
                                   most */
    s8           boat_scan; /* (s. 68) attack drops for: 1 a boat, 0 a
                               land unit, -1 the carried unit's kind */
    u32          serial;    /* (s. 68) the think's (route_build's cache;
                               0 none) */
} Nm;

/* Scratch, made again each think (the same from the same game). */
static u8 g_danger[2][64][64];
static u8 g_missiles[2][64][64];

/* The ground drive to the enemy HQ (ground_build), a side each. */
#define NM_FAR 0xFFFF

static u16 g_ground[2][64][64];
static s16 g_ground_key[2][3] = { { -1, -1, -1 }, { -1, -1, -1 } };
static u8  g_pass[2][64][64];      /* a block a ground unit drives on */
/* (Session 68) A boat's: blocks of water from one in its gun's reach of
 * the enemy HQ (NM_FAR none), a side each, made with g_ground */
static u16 g_water[2][64][64];

/* -- small helpers ------------------------------------------------------ */

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static int imax(int a, int b)
{
    return a > b ? a : b;
}

static int imin(int a, int b)
{
    return a < b ? a : b;
}

static int clamp63(int v)
{
    return v < 0 ? 0 : (v > 63 ? 63 : v);
}

static int chebyshev(int dx, int dy)
{
    return imax(iabs(dx), iabs(dy));
}

static const HzPost *post(const Nm *n, int i)
{
    return &n->g->posts[i];
}

static bool post_ours(const Nm *n, int i)
{
    u8 f = n->g->posts[i].flags;
    if (!(f & POST_USED) || !(f & POST_OWNED)) {
        return false;
    }
    return ((f & POST_TEAM2) != 0) == (n->team != 0);
}

static bool post_theirs(const Nm *n, int i)
{
    u8 f = n->g->posts[i].flags;
    if (!(f & POST_USED) || !(f & POST_OWNED)) {
        return false;
    }
    return ((f & POST_TEAM2) != 0) != (n->team != 0);
}

static int own_hq(const Nm *n)
{
    return HZ_POST_HQ + n->team;
}

static int enemy_hq(const Nm *n)
{
    return HZ_POST_HQ + 1 - n->team;
}

/* Own bases: the HQ and the outposts held. */
static bool is_base(const Nm *n, int i)
{
    if (i == own_hq(n)) {
        return true;
    }
    return i < HZ_POST_OUTPOSTS && post_ours(n, i);
}

/* (Session 74) The enemy's hold on outpost i of its own, 0 to 3: his
 * INFANTRY gone in since it was its (terrain.c, unit_capture: each one in
 * takes a hold of the owner's off, and the fourth turns the outpost). 0
 * for one not its own. */
static int hold_lost(const Nm *n, int i)
{
    if (i < 0 || i >= HZ_POST_OUTPOSTS || !post_ours(n, i)) {
        return 0;
    }
    return n->g->posts[i].hold[1 - n->team];
}

/* (Session 74, P_HOLD_BACK) INFANTRY bought for a hold lost in outpost i
 * are on their way for NM_HOLD_WAIT frames (bought, built, carried there
 * and walked in: 15 to 25 s), and not bought again meanwhile. A lot that
 * put no hold back (the enemy's never fewer than when its first was
 * bought: one stood 50 px from its outpost behind two of its own TAX-52s
 * for 17 minutes) is a failure; three in a row and the outpost is left
 * P_CAPTURE_REST frames. */
#define NM_HOLD_WAIT 1500

static void hold_bought(Nm *n, int i)
{
    HzNightmare *s = n->s;
    int lost = hold_lost(n, i);
    if (s->hold_buy_at[i] == 0
        || s->frames - s->hold_buy_at[i] >= NM_HOLD_WAIT) {
        if (s->hold_buy_at[i] != 0 && s->hold_low[i] >= s->hold_seen[i]) {
            if (++s->hold_fails[i] >= 3) {
                s->hold_fails[i] = 0;
                s->hold_rest[i] = s->frames + (u32)g_param[P_CAPTURE_REST];
                s->hold_given_up++;
            }
        } else {
            s->hold_fails[i] = 0;
        }
        s->hold_out[i] = 0;
        s->hold_seen[i] = (u8)lost;
        s->hold_low[i] = (u8)lost;
    }
    s->hold_out[i]++;
    s->hold_buy_at[i] = s->frames | 1;
}

/* ... the fewest of the enemy's holds seen in each since (every think). */
static void hold_watch(Nm *n)
{
    HzNightmare *s = n->s;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        int lost = hold_lost(n, i);
        if (lost < s->hold_low[i]) {
            s->hold_low[i] = (u8)lost;
        }
    }
}

/* (Session 74) The enemy SAM-42s and GRM-34As whose missiles reach
 * outpost i (danger_build's count for its block). */
static int post_missiles(const Nm *n, int i)
{
    const HzPost *pp = &n->g->posts[i];
    int bx = pp->x >> 5, by = pp->y >> 5;
    bx = bx < 0 ? 0 : bx > 63 ? 63 : bx;
    by = by < 0 ? 0 : by > 63 ? 63 : by;
    return n->missiles[by][bx];
}

static int mx(const Nm *n)          /* the mech's centre */
{
    return n->m->x + 16;
}

static int my(const Nm *n)
{
    return n->m->y + 16;
}

static bool unit_alive(const HzUnit *u)
{
    return (u->flags & UNIT_ACTIVE)
           && !(u->flags & (UNIT_DYING | UNIT_CARRIED));
}

static bool unit_enemy(const Nm *n, const HzUnit *u)
{
    return ((u->flags & UNIT_TEAM2) != 0) != (n->team != 0);
}

/* (Session 61) Is there an alive unit of `side`, not `except`, whose centre
 * is in the grid cells from (x0, y0) to (x1, y1) px, for which `test`
 * holds? Through the grid -- an alive unit marks its centre's cell (with
 * UNITS, the cell's list of its side: crowd.c) -- rather than over all
 * the side's slots, which with UNITS can be thousands. */
typedef bool (*UnitTest)(const HzUnit *u, const void *ctx);

static bool any_unit_near(const HzGame *g, int side, int x0, int y0,
                          int x1, int y1, const HzUnit *except,
                          UnitTest test, const void *ctx)
{
    int c0 = clamp63(x0 >> 5), c1 = clamp63(x1 >> 5);
    int r0 = clamp63(y0 >> 5), r1 = clamp63(y1 >> 5);
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            if (g->crowd) {
                for (const HzUnit *u = crowd_first(g, side,
                                                   (u16)(r * HZ_GRID + c));
                     u != NULL; u = crowd_after(g, u)) {
                    if (u != except && unit_alive(u) && test(u, ctx)) {
                        return true;
                    }
                }
                continue;
            }
            HzRef id = g->grid[r * HZ_GRID + c];
            if (id.kind != REF_UNIT || id.side != side) {
                continue;
            }
            const HzUnit *u = ref_unit_ptr_c(g, id);
            if (u == NULL || u == except || !unit_alive(u)) {
                continue;
            }
            if (test(u, ctx)) {
                return true;
            }
        }
    }
    return false;
}

static int ucx(const HzUnit *u)
{
    return u->x + u->size / 2;
}

static int ucy(const HzUnit *u)
{
    return u->y + u->size / 2;
}

static const HzMechStats *kidou(const Nm *n)
{
    return &n->g->assets->mech_stats;
}

/* Its own random number, 0..n-1 (xorshift32). */
static int nm_rand(Nm *n, int range)
{
    u32 x = n->s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    n->s->rng = x;
    return range > 0 ? (int)(x % (u32)range) : 0;
}

/* A button pressed afresh this frame: held last frame, it is let go for
 * this one (the press comes the frame after). True when pressed now. */
static bool press(Nm *n, u8 button)
{
    if (n->m->pad_prev & button) {
        return false;
    }
    n->pad |= button;
    return true;
}

/* The pad bits of an 8-way heading (0 up, 2 up-right ... 14 up-left: the
 * 16-way heading of an even direction). */
static u8 dir_pad(int dir16)
{
    return AI_PAD_DIRS[(dir16 >> 1) & 7];
}

/* The 16-way direction from (x0, y0) to (x1, y1). */
static int bearing16(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int best = 0;
    long best_dot = -0x7FFFFFFFL;
    for (int d = 0; d < 16; d++) {
        long dot = (long)dx * SINE_TABLE[d].x + (long)dy * SINE_TABLE[d].y;
        if (dot > best_dot) {
            best_dot = dot;
            best = d;
        }
    }
    return best;
}

/* -- the danger map ----------------------------------------------------- */

static bool missile_unit(const HzUnit *u)
{
    return (u->type == UNIT_SAM || u->type == UNIT_GRM) && u->missiles > 0;
}

/* Where an enemy unit's gun reaches a robot (its sight, and the GRM's
 * bullets' run), px. */
static int gun_reach(const HzUnit *u)
{
    switch (u->type) {
    case UNIT_GRM:      return 160;
    case UNIT_ST:       return 200;
    case UNIT_TURRET2:  return 200;     /* (session 60) the ST-57U's gun */
    case UNIT_SAM:
    case UNIT_SUPPLY:   return 0;
    case UNIT_REPAIR:                   /* (session 72) REPAIR: no gun */
    case UNIT_MECHANIC: return 0;
    default:            return 104;
    }
}

static void mark(u8 (*d)[64], int cx, int cy, int r, u8 bit)
{
    int x0 = clamp63((cx - r) >> 5), x1 = clamp63((cx + r) >> 5);
    int y0 = clamp63((cy - r) >> 5), y1 = clamp63((cy + r) >> 5);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            d[y][x] |= bit;
        }
    }
}

/* (Session 68) As mark, counting: n->missiles. */
static void mark_count(u8 (*d)[64], int cx, int cy, int r)
{
    int x0 = clamp63((cx - r) >> 5), x1 = clamp63((cx + r) >> 5);
    int y0 = clamp63((cy - r) >> 5), y1 = clamp63((cy + r) >> 5);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (d[y][x] < 255) {
                d[y][x]++;
            }
        }
    }
}

static void danger_build(Nm *n)
{
    u8 (*d)[64] = n->danger;
    memset(d, 0, 64 * 64);
    memset(n->missiles, 0, 64 * 64);
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (!unit_alive(u)) {
            continue;
        }
        /* AimAtMech: the mech's (x, y) within $100 of the unit's; the
         * block holding the mech's centre, so + 16 */
        if (missile_unit(u)) {
            mark(d, u->x + 16, u->y + 16, 256 + 16, DANGER_MISSILE);
            mark_count(n->missiles, u->x + 16, u->y + 16, 256 + 16);
            if (g_param[P_ARC_CORE] > 0) {
                mark(d, u->x + 16, u->y + 16, g_param[P_ARC_CORE] + 16,
                     DANGER_CORE);
            }
        }
        int g = gun_reach(u);
        if (g > 0 && u->ammo > 0) {
            mark(d, ucx(u), ucy(u), g, DANGER_GUN);
        }
    }
    const HzMech *e = n->rival;
    if (!(e->flags & MECH_DESTROYED) && e->form != HZ_FORM_ROBOT) {
        mark(d, e->x + 16, e->y + 16, 40, DANGER_JET);
    }
}

/* The danger where the mech's (x, y) would put its centre. */
static u8 danger_at(const Nm *n, int x, int y)
{
    int cx = (x + 16) >> 5, cy = (y + 16) >> 5;
    if (cx < 0 || cy < 0 || cx > 63 || cy > 63) {
        return 0;
    }
    return n->danger[cy][cx];
}

/* Has the enemy jet's gun hit the mech in the last 90 frames? (Its HP
 * fell with the enemy jet near: nothing else hurts a jet but missiles,
 * and those come from units the danger map keeps it from.) */
static bool jet_attacking(const Nm *n)
{
    return n->s->jet_hit_at != 0 && n->s->frames - n->s->jet_hit_at < 90;
}

/* Used before they are defined. */
enum { NM_REACH_ANY, NM_REACH_MECH, NM_REACH_BASE };
enum { NM_DRIVE_UNTRIED, NM_DRIVE_GOOD, NM_DRIVE_BAD };     /* (s. 68) */
static int drive_ok(Nm *n, int gx, int gy, bool nest);
static int drive_cached(Nm *n, int gx, int gy, bool nest);
static bool region_ok(const Nm *n, int type, int gx, int gy);
static void attack_drop(Nm *n, s16 *ox, s16 *oy);
static int attack_ring(Nm *n, int x, int y, int inner, int reach, int step,
                       s16 *ox, s16 *oy);
static bool atlas_line(const Nm *n);
static bool atlas_ring(Nm *n, int x, int y, int reach, s16 *ox, s16 *oy);
static bool park_drop(Nm *n, s16 *ox, s16 *oy);
static bool cold_near(const Nm *n, int x, int y);
static bool hunt_drop_at(Nm *n, bool from_mech, s16 *ox, s16 *oy,
                         int *worth_out);
static void guard_drop(Nm *n, s16 *ox, s16 *oy);
static bool attack_drop_at(Nm *n, int reach, int near, s16 *ox, s16 *oy,
                           bool *untried, bool *nest);
static bool attack_boat(const Nm *n);
static bool water_at(Nm *n, int x, int y);
static int attack_drop_best(Nm *n, int reach, int near, int how,
                            s16 *ox, s16 *oy);
static void ground_build(Nm *n);
static bool rival_jet(const Nm *n);

/* -- flying ------------------------------------------------------------- */

/* One axis of the pad towards `d` px away at velocity `v` (8.8 px a
 * frame), stopping in time at `decel` (8.8 px a frame a frame). */
static u8 axis_pad(int d, int v, int decel, u8 neg, u8 pos, int tol)
{
    long stop = (long)v * v / (2L * decel * 256) + 2;
    if (d > tol) {
        return (v > 0 && stop >= d) ? 0 : pos;
    }
    if (d < -tol) {
        return (v < 0 && stop >= -d) ? 0 : neg;
    }
    return 0;
}

/* (Session 73, P_EDGE) The map's edge. A mech's x and y go from 0 to $7E0
 * (clamp_mech), and it stands on the 4 x 4 tiles from (x + 4, y + 4). Where
 * the map's outermost block is ground between the edge and something a
 * mech can't stand on, the tiles fit only with the mech within 3 px of the
 * edge: on ABGRUND the 32 px north of P2's HQ, and south of P1's. landable
 * and walkable took nothing within 8 px of an edge for a place, so to it
 * that strip was none. In the owner's game of 10 November (ABGRUND, the
 * server's AI P2) his TAX-52s went down there, 100 px from its HQ: no wall
 * unit and no guard could be put by them, and its mech found no place to
 * fight them from (the driver of one stood there 229 s and fired his 80
 * shots, the jet held against the map's edge over him). Of its HQ's 1,024
 * HP 702 went from that strip.
 *
 * A place within 8 px of an edge is taken at NM_EDGE_LOW or NM_EDGE_HIGH,
 * and flown to within 1 px, which leaves the mech within 3 px of the
 * edge. */
#define NM_EDGE_LOW   1
#define NM_EDGE_HIGH  0x7DF

/* Is (x, y) on the map, for a mech's place? */
static bool on_map(int x, int y)
{
    int e = g_param[P_EDGE] ? 0 : 8;
    return x >= e && y >= e && x <= 0x7E0 - e && y <= 0x7E0 - e;
}

/* Is one coordinate of a place within 8 px of an edge, either side of
 * it? */
static bool by_edge(int v)
{
    return g_param[P_EDGE]
           && ((v >= -8 && v < 8) || (v > 0x7D8 && v <= 0x7E8));
}

/* One coordinate of a place: by an edge, the edge's own (NM_EDGE_LOW,
 * NM_EDGE_HIGH). */
static int edge_snap(int v)
{
    if (!by_edge(v)) {
        return v;
    }
    return (v < 8) ? NM_EDGE_LOW : NM_EDGE_HIGH;
}

/* One coordinate of where a flight or a walk stops: on the map within
 * 8 px of an edge, the edge's own, and *tol 1 px at most. */
static int edge_goal(int v, int *tol)
{
    if (g_param[P_EDGE] && v >= 0 && v <= 0x7E0 && (v < 8 || v > 0x7D8)) {
        *tol = imin(*tol, 1);
        return edge_snap(v);
    }
    return v;
}

/* Steer the mech's (x, y) towards (tx, ty) and stop there; the distance
 * left. (Session 73) A place by the map's edge: see edge_goal. */
static int steer_to(Nm *n, int tx, int ty, int tol)
{
    HzMech *m = n->m;
    int tol_x = tol, tol_y = tol;
    tx = edge_goal(tx, &tol_x);
    ty = edge_goal(ty, &tol_y);
    int dx = tx - m->x;
    int dy = ty - m->y;
    int decel = (m->form == HZ_FORM_ROBOT) ? 200 : 0x18;
    n->pad |= axis_pad(dx, m->vel_x, decel, HZ_PAD_LEFT, HZ_PAD_RIGHT, tol_x);
    n->pad |= axis_pad(dy, m->vel_y, decel, HZ_PAD_UP, HZ_PAD_DOWN, tol_y);
    return chebyshev(dx, dy);
}

/* (Session 68, P_DROP_FLY) Fly over (tx, ty) at speed: on the axis it is
 * further along, held towards it (and on past it); on the other, stopped
 * on its line. head_for, with its 8 ways and the jet's 40 px to stop an
 * axis, came in diagonally and went by 40 px off. */
static void fly_through(Nm *n, int tx, int ty)
{
    HzMech *m = n->m;
    int dx = tx - m->x;
    int dy = ty - m->y;
    if (iabs(dx) >= iabs(dy)) {
        n->pad |= (dx > 0 || (dx == 0 && m->vel_x >= 0)) ? HZ_PAD_RIGHT
                                                          : HZ_PAD_LEFT;
        n->pad |= axis_pad(dy, m->vel_y, 0x18, HZ_PAD_UP, HZ_PAD_DOWN, 2);
    } else {
        n->pad |= (dy > 0 || (dy == 0 && m->vel_y >= 0)) ? HZ_PAD_DOWN
                                                          : HZ_PAD_UP;
        n->pad |= axis_pad(dx, m->vel_x, 0x18, HZ_PAD_LEFT, HZ_PAD_RIGHT, 2);
    }
}

/* Head for (tx, ty) without stopping there. */
static void head_for(Nm *n, int tx, int ty)
{
    int dx = tx - n->m->x;
    int dy = ty - n->m->y;
    if (dx > 6) {
        n->pad |= HZ_PAD_RIGHT;
    } else if (dx < -6) {
        n->pad |= HZ_PAD_LEFT;
    }
    if (dy > 6) {
        n->pad |= HZ_PAD_DOWN;
    } else if (dy < -6) {
        n->pad |= HZ_PAD_UP;
    }
}

static bool slow(const HzMech *m)
{
    return iabs(m->vel_x) < 0x60 && iabs(m->vel_y) < 0x60;
}

/* (Session 68, P_ARC_CORE) What a flight over block (x, y) to the goal
 * block (tx, ty) costs for the enemy's missiles, tenths of a block. A
 * missile flies 2 px a frame, aimed at the mech every 4 frames; the jet
 * flies 4 (3 carrying), so one fired at it as it goes by comes up behind
 * and doesn't catch it. Played out as shots.c has it (a SAM-42 firing
 * every 32 frames), a jet at full speed is hit only flying within 64 px
 * of the launcher: going by 96 px off, half round it 128 px off, or at
 * it and turned away 160 px off, none hit it. One slowed is: carrying,
 * stopped 300 px past one it went by 128 px off, 3 hits. So the blocks
 * near a launcher, and its reach within P_ARC_GOAL blocks of the goal (a
 * flight slows there), cost as before; the rest of its reach
 * P_ARC_RING. The owner: "you can fly an arc around rocket launchers
 * instead of avoiding triggering their fire". */
static int missile_step(const Nm *n, int x, int y, int tx, int ty)
{
    u8 d = n->danger[y][x];
    if (!(d & DANGER_MISSILE)) {
        return 0;
    }
    if (g_param[P_ARC_CORE] <= 0 || (d & DANGER_CORE)
        || chebyshev(x - tx, y - ty) <= g_param[P_ARC_GOAL]) {
        return g_param[P_MISSILE_COST] * 10;
    }
    return g_param[P_ARC_RING] * 10;
}

/* Does the straight flight from block (x0, y0) to (x1, y1), on the way to
 * the goal block (tx, ty), cross a missile block (not counting the
 * start)? (Session 68) With P_ARC_CORE only those near one, or near the
 * goal. */
static bool line_clear(const Nm *n, int x0, int y0, int x1, int y1, int tx,
                       int ty)
{
    bool arc = g_param[P_ARC_CORE] > 0;
    u8 bad = arc ? (DANGER_CORE | DANGER_JET) : DANGER_FLY;
    int steps = chebyshev(x1 - x0, y1 - y0);
    for (int i = 1; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        u8 d = n->danger[y][x];
        if ((d & bad) || (arc && (d & DANGER_MISSILE)
                          && chebyshev(x - tx, y - ty)
                             <= g_param[P_ARC_GOAL])) {
            return false;
        }
    }
    return true;
}

static bool line_safe(const Nm *n, int x0, int y0, int x1, int y1)
{
    return line_clear(n, x0, y0, x1, y1, x1, y1);
}

/* A* over the blocks from the mech's to the goal's: 10 a step, 14 a
 * diagonal, + P_MISSILE_COST x 10 a missile block (none near a goal that
 * is itself in one). The path into s->path. */
static void plan_path(Nm *n, int gx, int gy)
{
    static u16 cost[64 * 64];
    static u16 from[64 * 64];
    static u32 heap[64 * 64 * 8];   /* priority << 12 | block */
    static u8  closed[64 * 64];
    static u16 rev[64 * 64];
    HzNightmare *s = n->s;
    int sx = clamp63(mx(n) >> 5), sy = clamp63(my(n) >> 5);
    int tx = clamp63((gx + 16) >> 5), ty = clamp63((gy + 16) >> 5);
    int start = sy * 64 + sx, goal = ty * 64 + tx;
    s->path_len = 0;
    s->path_at = 0;
    s->path_gx = (s16)gx;
    s->path_gy = (s16)gy;
    s->path_age = 0;
    if (start == goal) {
        return;
    }
    memset(closed, 0, sizeof closed);
    for (int i = 0; i < 64 * 64; i++) {
        cost[i] = 0xFFFF;
    }
    int hn = 0;
    cost[start] = 0;
    heap[hn++] = (u32)start;
    bool goal_hot = (n->danger[ty][tx] & DANGER_MISSILE) != 0;
    while (hn > 0) {
        int cur = (int)(heap[0] & 0xFFF);
        heap[0] = heap[--hn];
        for (int i = 0;;) {
            int l = i * 2 + 1, r = l + 1, b = i;
            if (l < hn && heap[l] < heap[b]) {
                b = l;
            }
            if (r < hn && heap[r] < heap[b]) {
                b = r;
            }
            if (b == i) {
                break;
            }
            u32 t = heap[i];
            heap[i] = heap[b];
            heap[b] = t;
            i = b;
        }
        if (closed[cur]) {
            continue;               /* an older, dearer entry */
        }
        closed[cur] = 1;
        if (cur == goal) {
            break;
        }
        int cx = cur & 63, cy = cur >> 6;
        for (int d = 0; d < 8; d++) {
            static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
            static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || ny < 0 || nx > 63 || ny > 63) {
                continue;
            }
            int nb = ny * 64 + nx;
            if (closed[nb]) {
                continue;
            }
            int c = cost[cur] + (d < 4 ? 10 : 14);
            if (!(goal_hot && chebyshev(nx - tx, ny - ty) <= 9)) {
                c += missile_step(n, nx, ny, tx, ty);
            }
            if ((n->danger[ny][nx] & DANGER_JET)
                && chebyshev(nx - tx, ny - ty) > 1) {
                c += 400;
            }
            if (c >= cost[nb] || c > 60000) {
                continue;
            }
            cost[nb] = (u16)c;
            from[nb] = (u16)cur;
            if (hn >= (int)(sizeof heap / sizeof heap[0])) {
                continue;
            }
            u32 prio = (u32)imin(0xFFFFF, c + chebyshev(tx - nx, ty - ny) * 10);
            int i = hn++;
            heap[i] = (prio << 12) | (u32)nb;
            while (i > 0) {
                int up = (i - 1) / 2;
                if (heap[up] <= heap[i]) {
                    break;
                }
                u32 t = heap[i];
                heap[i] = heap[up];
                heap[up] = t;
                i = up;
            }
        }
    }
    if (!closed[goal]) {
        return;
    }
    int len = 0;
    for (int c = goal; c != start && len < 64 * 64; c = from[c]) {
        rev[len++] = (u16)c;
    }
    int keep = imin(len, NM_PATH_MAX);
    for (int i = 0; i < keep; i++) {
        s->path[i] = rev[len - 1 - i];
    }
    s->path_len = (u8)keep;
    s->path_full = (u16)len;
}

static bool vs_human(const Nm *n);
static int trip_fuel(const Nm *n, int dist, bool loaded);

/* Fly to (gx, gy) round the missiles and stop there; the distance left.
 * Near the goal, or with the way clear, it goes straight. */
static int fly_to(Nm *n, int gx, int gy, int tol)
{
    HzNightmare *s = n->s;
    int sx = clamp63(mx(n) >> 5), sy = clamp63(my(n) >> 5);
    int tx = clamp63((gx + 16) >> 5), ty = clamp63((gy + 16) >> 5);
    int dist = chebyshev(gx - n->m->x, gy - n->m->y);
    if (dist < 96 || line_safe(n, sx, sy, tx, ty)) {
        s->path_len = 0;
        return steer_to(n, gx, gy, tol);
    }
    if (g_param[P_FLY_STRAIGHT] && vs_human(n)
        && !(n->m->flags & MECH_CARRYING)) {
        /* (session 74) the owner (14 November): "it navigates out of
         * the missiles range ... There is no cost in losing a plane other
         * than the time to regenerate." With nothing aboard the way round
         * a launcher's reach is the longer way and saves nothing (a jet
         * at speed is hit only within 64 px of one: missile_step). */
        s->path_len = 0;
        return steer_to(n, gx, gy, tol);
    }
    if (s->path_straight && s->path_gx == gx && s->path_gy == gy
        && ++s->path_age <= 90) {
        return steer_to(n, gx, gy, tol);
    }
    if (s->path_len == 0 || s->path_gx != gx || s->path_gy != gy
        || ++s->path_age > 90) {
        plan_path(n, gx, gy);
        s->path_straight = false;
        if (s->path_len > 0 && g_param[P_PATH_FUEL] && vs_human(n)) {
            /* (session 68, P_PATH_FUEL) in the owner's game of 31
             * October, 18:31 (VULKAN, the server's AI P1) the mech, by
             * his SAM-42s 400 px north of its HQ on a quarter of its tank,
             * flew for home round their reach, east and north and west,
             * and ran dry: 15 times */
            bool loaded = (n->m->flags & MECH_CARRYING) != 0;
            if (trip_fuel(n, s->path_full * 32, loaded) > n->m->fuel
                && trip_fuel(n, dist, loaded) <= n->m->fuel) {
                s->path_len = 0;
                s->path_straight = true;
                s->path_fuel++;
            }
        }
        if (s->path_len == 0) {
            return steer_to(n, gx, gy, tol);
        }
    }
    while (s->path_at < s->path_len) {
        int c = s->path[s->path_at];
        if (chebyshev((c & 63) - sx, (c >> 6) - sy) > 1) {
            break;
        }
        s->path_at++;
    }
    if (s->path_at >= s->path_len) {
        return steer_to(n, gx, gy, tol);
    }
    /* the farthest cell a few ahead the way to which is clear */
    int pick = s->path_at;
    for (int i = s->path_at; i < s->path_len && i < s->path_at + 8; i++) {
        int c = s->path[i];
        if (line_clear(n, sx, sy, c & 63, c >> 6, tx, ty)) {
            pick = i;
        }
    }
    int c = s->path[pick];
    head_for(n, (c & 63) * 32, (c >> 6) * 32);
    return dist;
}

/* The nearest own base to (x, y). */
static int nearest_base(const Nm *n, int x, int y)
{
    int best = own_hq(n);
    int best_d = 0x7FFF;
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        if (!is_base(n, i)) {
            continue;
        }
        int d = chebyshev(post(n, i)->x - x, post(n, i)->y - y);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static int base_dist(const Nm *n, int base, int x, int y)
{
    return chebyshev(post(n, base)->x - x, post(n, base)->y - y);
}

/* (Session 68) Is base i in reach of the enemy's missiles? */
static bool base_hot(const Nm *n, int i)
{
    return (n->danger[clamp63(post(n, i)->y >> 5)]
                     [clamp63(post(n, i)->x >> 5)] & DANGER_MISSILE) != 0;
}

/* Fuel a flight of `dist` px (Chebyshev) costs, carrying when `loaded`:
 * fuel_use every $50 of speed counted up, and a diagonal at full speed
 * gains 22 x speed / 256 px a frame on each axis, so dist x 256 / ($50 x
 * 22) x use, + a tenth for the turns and the way round. */
static int trip_fuel(const Nm *n, int dist, bool loaded)
{
    const HzMechStats *k = kidou(n);
    int form = loaded ? HZ_FORM_CARRYING : HZ_FORM_JET;
    int use = k->fuel_use[form];
    if (!n->cpu && (n->rival->flags & MECH_CPU)) {
        use++;
    }
    if (loaded && n->g->options[1] == 0 && n->g->game_type >= 0
        && ((n->m->flags | n->rival->flags) & MECH_CPU)) {
        use >>= 1;
    }
    return dist * use * 256 * 11 / (0x50 * 22 * 10) + 1;
}

/* -- the order menu ----------------------------------------------------- */

static bool menu_open(const HzMech *m)
{
    return (m->flags & MECH_MENU_OPEN) != 0;
}

/* Close the menu if open; true when closed. */
static bool menu_close(Nm *n)
{
    HzMech *m = n->m;
    if (!menu_open(m)) {
        return true;
    }
    if (m->menu.step == 0 && !m->menu.closing) {
        press(n, HZ_PAD_C);
    }
    return false;
}

/* Steps right from type `from` to `to` through the buyable mask. */
static int menu_steps(u16 mask, int from, int to)
{
    int steps = 0;
    int t = from;
    for (int i = 0; i < 16 && t != to; i++) {
        do {
            t = (t + 1) & 15;
        } while (!(mask & (1u << t)));
        steps++;
    }
    return (t == to) ? steps : 99;
}

static u16 cost_of(const Nm *n, int type, int order)
{
    return (u16)(unit_stats_for(n->g, (u8)type)->price
                 + n->g->assets->orders[order & 15].fee);
}

/* -- tasks -------------------------------------------------------------- */

static void set_task(Nm *n, int task)
{
    n->s->task = (u8)task;
    n->s->stage = 0;
    n->s->wait = 0;
    n->s->age = 0;
    n->s->path_len = 0;
    n->s->drop_hover = 0;
    n->s->nest_goal = false;
    n->s->hover_n = 0;
    n->s->fly_near = false;
    n->s->fly_misses = 0;
}

static void task_buy(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    HzOrderMenu *mn = &m->menu;
    if (s->stage == 0) {
        if (!menu_open(m)) {
            press(n, HZ_PAD_C);
            if (s->age > 30) {
                set_task(n, NM_TASK_DECIDE);
            }
            return;
        }
        if (mn->step != 0 || mn->closing) {
            return;                 /* opening */
        }
        s->stage = 1;
    }
    if (s->stage == 1) {
        if (!menu_open(m)) {
            set_task(n, NM_TASK_DECIDE);
            return;
        }
        if (s->age > 300) {         /* something is wrong: give up */
            s->stage = 2;
            return;
        }
        if (m->pad_prev & (HZ_PAD_DIRS | HZ_PAD_A | HZ_PAD_B)) {
            return;                 /* let go first */
        }
        u16 mask = shop_types(n->g);    /* the menu's, turrets and all */
        /* (session 68) carrying, only the order: the carried unit's new
         * one (the type row is gone) */
        bool reorder = (m->flags & MECH_CARRYING) != 0;
        if (!reorder && mn->type != s->buy_type) {
            if (mn->row != MENU_ROW_TYPE) {
                n->pad |= (mn->row == MENU_ROW_ORDER) ? HZ_PAD_UP
                                                      : HZ_PAD_DOWN;
                return;
            }
            int right = menu_steps(mask, mn->type, s->buy_type);
            int left = menu_steps(mask, s->buy_type, mn->type);
            n->pad |= (right <= left) ? HZ_PAD_RIGHT : HZ_PAD_LEFT;
            return;
        }
        if (mn->order != s->buy_order) {
            if (mn->row != MENU_ROW_ORDER) {
                n->pad |= (mn->row == MENU_ROW_TYPE) ? HZ_PAD_DOWN
                                                     : HZ_PAD_UP;
                return;
            }
            n->pad |= HZ_PAD_RIGHT;
            return;
        }
        if (mn->row == MENU_ROW_RADAR) {
            n->pad |= HZ_PAD_UP;
            return;
        }
        if (reorder && m->carried_order == s->buy_order) {
            s->stage = 2;           /* ordered */
            return;
        }
        if (!reorder && (s8)m->prod_state > 0) {
            if (s->prod_cargo == NM_CARGO_ATTACK) {
                s->attack_buys++;
            }
            if (s->buy_mix) {
                s->buy_mix = false;
                s->mix_buys++;      /* (session 73, P_MIX_BUYS) */
            }
            if (s->prod_cargo == NM_CARGO_CAPTURE
                && s->buy_type == UNIT_INFANTRY
                && s->prod_post < HZ_POST_OUTPOSTS) {
                /* (session 74) for a hold of its own, or for an outpost
                 * that is nobody's out of CAPTURE_REACH */
                const HzPost *pp = post(n, s->prod_post);
                if (hold_lost(n, s->prod_post) > 0) {
                    s->hold_buys++;
                    hold_bought(n, s->prod_post);
                } else if (!(pp->flags & POST_OWNED)
                           && base_dist(n, nearest_base(n, pp->x, pp->y),
                                        pp->x, pp->y)
                              > g_param[P_CAPTURE_REACH]) {
                    s->free_buys++;
                }
                /* (session 75) whose outpost it was for */
                if (!(pp->flags & POST_OWNED)) {
                    s->nobody_buys++;
                } else if (post_theirs(n, s->prod_post)) {
                    s->their_buys++;
                }
            }
            s->stage = 2;           /* bought */
            s->bought[s->buy_type & 15]++;
            s->buys++;
            return;
        }
        if (m->money < mn->cost || mn->cost == 0) {
            s->stage = 2;           /* can't: give up for now */
            return;
        }
        n->pad |= HZ_PAD_A;
        return;
    }
    if (menu_close(n)) {
        set_task(n, NM_TASK_DECIDE);
    }
}

#define NM_GROUND_LOW 3         /* Mech_GetTerrain's GROUND_LOW: a robot
                                   can't take off on it */
#define NM_GROUND_WATER 4       /* ... GROUND_WATER: a boat goes down there,
                                   nothing else */

static bool take_off_spot(Nm *n, s16 *ox, s16 *oy);

/* The robot up again: (session 68) off low ground first, to the plain
 * land take_off_spot finds, kept 3 s. On LOCH a robot in a dip by the
 * map's edge was given one of two places by turns as it stepped a pixel
 * each way, and stood 75 s while its HQ fell from 1,024 HP to 0. One it
 * got no 8 px nearer in the 3 s is passed over 30 s. */
static void take_off(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    if (mech_ground_at(n->g, m, m->x, m->y) != NM_GROUND_LOW) {
        s->toff_at = 0;
        press(n, HZ_PAD_A);
        return;
    }
    if (s->toff_at != 0 && s->frames - s->toff_at >= 180) {
        int was = chebyshev(s->toff_x - s->toff_from_x,
                            s->toff_y - s->toff_from_y);
        int now = chebyshev(s->toff_x - m->x, s->toff_y - m->y);
        if (s->frames - s->toff_at < 240 && now > was - 8) {
            s->toff_bad_x = s->toff_x;
            s->toff_bad_y = s->toff_y;
            s->toff_bad_at = s->frames;
        }
        s->toff_at = 0;
    }
    if (s->toff_at == 0) {
        s16 gx, gy;
        if (!take_off_spot(n, &gx, &gy)) {
            s->toff_bad_at = 0;
            if (!take_off_spot(n, &gx, &gy)) {
                press(n, HZ_PAD_A);
                return;
            }
        }
        s->toff_x = gx;
        s->toff_y = gy;
        s->toff_from_x = m->x;
        s->toff_from_y = m->y;
        s->toff_at = s->frames ? s->frames : 1;
    }
    steer_to(n, s->toff_x, s->toff_y, 2);
}

/* Take off when on foot; true when flying (not changing). */
static bool airborne(Nm *n)
{
    HzMech *m = n->m;
    if (m->transform != 0) {
        return false;
    }
    if (m->form == HZ_FORM_ROBOT) {
        take_off(n);
        return false;
    }
    return true;
}

static void task_pickup(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    if (m->flags & MECH_CARRYING) {
        s->cargo = s->prod_cargo;
        s->cargo_post = s->prod_post;
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (m->prod_state != PROD_READY || !is_base(n, s->goal_post)
        || s->age > 1500) {
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (!airborne(n)) {
        return;
    }
    const HzPost *b = post(n, s->goal_post);
    int d = fly_to(n, b->x - 16, b->y - 16, 4);
    if (d <= 10) {
        n->pad |= HZ_PAD_A;         /* held: Outpost_PickUpProduct */
    }
}

/* Is (x, y) a place this mech can land or put a unit down? */
static bool landable(Nm *n, int x, int y)
{
    if (!on_map(x, y)) {
        return false;
    }
    return mech_ground_at(n->g, n->m, (s16)x, (s16)y) <= (n->cpu ? 0 : 1)
           && !mech_land_blocked_at(n->g, n->m, (s16)x, (s16)y);
}

static bool drop_hot(const Nm *n, int x, int y);
static bool drop_inside(const Nm *n);
static bool droppable(Nm *n, int x, int y);
static bool cpu_hard(const Nm *n);
static bool water_near(Nm *n, int x, int y, int reach, s16 *ox, s16 *oy);

/* The landable place nearest (x, y) (mech coordinates) from `inner` to
 * `reach` px off, free of missiles if it can be; (s. 68) with `safe`,
 * never drop_hot. */
static bool landable_rings(Nm *n, int x, int y, int inner, int reach,
                           bool safe, s16 *ox, s16 *oy)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int r = inner; r <= reach; r += 8) {
            for (int dy = -r; dy <= r; dy += 8) {
                for (int dx = -r; dx <= r; dx += 8) {
                    if (imax(iabs(dx), iabs(dy)) != r) {
                        continue;
                    }
                    /* (session 73) by the map's edge, the edge's own */
                    int px = edge_snap(x + dx), py = edge_snap(y + dy);
                    if (pass == 0
                        && (danger_at(n, px, py) & DANGER_MISSILE)) {
                        continue;
                    }
                    if (pass == 1 && safe && drop_hot(n, px, py)) {
                        continue;
                    }
                    if (landable(n, px, py)) {
                        *ox = (s16)px;
                        *oy = (s16)py;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static bool landable_ring(Nm *n, int x, int y, int inner, int reach,
                          s16 *ox, s16 *oy)
{
    return landable_rings(n, x, y, inner, reach, false, ox, oy);
}

static bool landable_near(Nm *n, int x, int y, int reach, s16 *ox, s16 *oy)
{
    return landable_ring(n, x, y, 0, reach, ox, oy);
}

/* (P_DROP_INSIDE_SAM) An enemy GRM-34A with missiles within its reach of
 * the mech's (x, y) (as danger_build marks it)? */
static bool grm_reach(const Nm *n, int x, int y)
{
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (unit_alive(u) && u->type == UNIT_GRM && missile_unit(u)
            && chebyshev(u->x - x, u->y - y) < 256 + 24) {
            return true;
        }
    }
    return false;
}

/* (Session 68, P_DROP_INSIDE) Attack drops in the enemy's missiles' reach
 * too, put down at speed? In the owner's games of 31 October (VULKAN,
 * the server's AI P1) all 80 of its attack units went down 400 to 700 px
 * from his HQ, out of reach of his SAM-42s round it (17 a game at one
 * place 624 px off), and drove in past them; he put 144 of 153 down 200 to
 * 400 px from its HQ, in reach of its 16 SAM-42s, flying over at speed,
 * and lost his jet 4 times in the game. Tried, off: taken over with the
 * turtle for the owner, his 06:20 game at 7:30 0-5-11 (0-2-14 without),
 * and at 33 minutes in his 18:31 game his HQ shot less (698 left against
 * 524), with SAM-42s alone reaching (P_DROP_INSIDE_SAM) too. */
static bool drop_inside(const Nm *n)
{
    return g_param[P_DROP_INSIDE] > 0 && g_param[P_DROP_FLY] > 0
           && (g_param[P_FLY_KINDS] & 1) && vs_human(n);
}

/* (Session 68) Is (x, y) too hot to put its attack unit down at, where
 * P_SAFE_DROP or more of the enemy's SAMs and GRMs reach (or one, with
 * the mech under half its HP)? On ABGRUND a player's stood all round the
 * places from which a unit reached the HQ, and the old ways put it down
 * as far on as the fuel went, among them: the mech, slow as it put the
 * unit down, was shot down there six times in a game, seven units'
 * reach on it. In the game before, by one SAM's, never. */
static bool drop_hot(const Nm *n, int x, int y)
{
    int most = n->s->cargo == NM_CARGO_HUNT ? g_param[P_HUNT_COVER]
                                            : g_param[P_SAFE_DROP];
    if (most <= 0 || (n->s->cargo != NM_CARGO_ATTACK
                      && n->s->cargo != NM_CARGO_HUNT)) {
        return false;
    }
    int cx = (x + 16) >> 5, cy = (y + 16) >> 5;
    if (cx < 0 || cy < 0 || cx > 63 || cy > 63) {
        return false;
    }
    int count = n->missiles[cy][cx];
    if (drop_inside(n) && n->s->cargo == NM_CARGO_ATTACK) {
        if ((n->danger[cy][cx] & DANGER_CORE)
            || (count > 0 && g_param[P_DROP_INSIDE_SAM]
                && grm_reach(n, x, y))) {
            return true;
        }
        most = g_param[P_DROP_INSIDE];
    }
    if (g_param[P_SAFE_FLY] > 0 && g_param[P_DROP_FLY] > 0
        && n->s->cargo == NM_CARGO_ATTACK) {
        /* (session 68) put down at speed: only by a launcher, or where
         * P_SAFE_FLY reach */
        if (n->danger[cy][cx] & DANGER_CORE) {
            return true;
        }
        most = g_param[P_SAFE_FLY];
    }
    return count >= most || (count > 0 && n->m->hp < kidou(n)->stats.hp / 2);
}

/* As landable_ring, never drop_hot. */
static bool drop_ring(Nm *n, int x, int y, int inner, int reach, s16 *ox,
                      s16 *oy)
{
    return landable_rings(n, x, y, inner, reach, true, ox, oy);
}

/* Can the mech carry its unit `there` px to (gx, gy) and then fly light
 * to the own base nearest there, with `margin` to spare? */
static bool leg_fuel_ok(const Nm *n, int there, int fuel, int gx, int gy,
                        int margin)
{
    int home = nearest_base(n, gx + 16, gy + 16);
    int back = base_dist(n, home, gx + 16, gy + 16);
    return fuel >= trip_fuel(n, there, true) + trip_fuel(n, back, false)
                   + margin;
}

/* ... from (fx, fy) on `fuel`, the straight line */
static bool leg_ok_from(const Nm *n, int fx, int fy, int fuel, int gx,
                        int gy, int margin)
{
    return leg_fuel_ok(n, chebyshev(gx - fx, gy - fy), fuel, gx, gy, margin);
}

/* (Session 68) The flights from the mech's block, in blocks (a diagonal
 * one, as Chebyshev counts), the way plan_path would go round the
 * missiles (its costs, with no goal's own); NM_FAR where none. Made once
 * a think, when wanted. In the owner's game of 30 October a leg planned
 * on the straight line from its outpost ran short going round his SAM-42s
 * by it, 15 times in two minutes. */
static u16 g_route[2][64 * 64];
static u32 g_route_serial[2];

static const u16 *route_build(const Nm *n)
{
    static u16 cost[64 * 64];
    static u32 heap[64 * 64 * 8];   /* cost << 12 | block */
    static u8  closed[64 * 64];
    u16 *steps = g_route[n->p];
    if (n->serial != 0 && g_route_serial[n->p] == n->serial) {
        return steps;
    }
    g_route_serial[n->p] = n->serial;
    int start = clamp63(my(n) >> 5) * 64 + clamp63(mx(n) >> 5);
    memset(closed, 0, sizeof closed);
    for (int i = 0; i < 64 * 64; i++) {
        cost[i] = 0xFFFF;
        steps[i] = NM_FAR;
    }
    int hn = 0;
    cost[start] = 0;
    steps[start] = 0;
    heap[hn++] = (u32)start;
    while (hn > 0) {
        int cur = (int)(heap[0] & 0xFFF);
        heap[0] = heap[--hn];
        for (int i = 0;;) {
            int l = i * 2 + 1, r = l + 1, b = i;
            if (l < hn && heap[l] < heap[b]) {
                b = l;
            }
            if (r < hn && heap[r] < heap[b]) {
                b = r;
            }
            if (b == i) {
                break;
            }
            u32 t = heap[i];
            heap[i] = heap[b];
            heap[b] = t;
            i = b;
        }
        if (closed[cur]) {
            continue;               /* an older, dearer entry */
        }
        closed[cur] = 1;
        int cx = cur & 63, cy = cur >> 6;
        for (int d = 0; d < 8; d++) {
            static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
            static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || ny < 0 || nx > 63 || ny > 63) {
                continue;
            }
            int nb = ny * 64 + nx;
            if (closed[nb]) {
                continue;
            }
            int c = cost[cur] + (d < 4 ? 10 : 14)
                    + missile_step(n, nx, ny, -99, -99);
            if (n->danger[ny][nx] & DANGER_JET) {
                c += 400;
            }
            if (c >= cost[nb] || c > 60000) {
                continue;
            }
            cost[nb] = (u16)c;
            steps[nb] = (u16)(steps[cur] + 1);
            if (hn >= (int)(sizeof heap / sizeof heap[0])) {
                continue;
            }
            int i = hn++;
            heap[i] = ((u32)c << 12) | (u32)nb;
            while (i > 0) {
                int up = (i - 1) / 2;
                if (heap[up] <= heap[i]) {
                    break;
                }
                u32 t = heap[i];
                heap[i] = heap[up];
                heap[up] = t;
                i = up;
            }
        }
    }
    return steps;
}

/* (Session 68, P_ROUTE_FUEL) The flight's length from the mech to (gx,
 * gy), px: the straight line's when it is clear of missiles, else the
 * way round them. */
static int route_dist(const Nm *n, int gx, int gy)
{
    int straight = chebyshev(gx - n->m->x, gy - n->m->y);
    int sx = clamp63(mx(n) >> 5), sy = clamp63(my(n) >> 5);
    int tx = clamp63((gx + 16) >> 5), ty = clamp63((gy + 16) >> 5);
    if (!g_param[P_ROUTE_FUEL] || straight < 96
        || line_safe(n, sx, sy, tx, ty)) {
        return straight;
    }
    u16 st = route_build(n)[ty * 64 + tx];
    return st == NM_FAR ? straight : imax(straight, st * 32);
}

static bool leg_ok(const Nm *n, int gx, int gy, int margin)
{
    int there = route_dist(n, gx, gy);
    if (leg_fuel_ok(n, there, n->m->fuel, gx, gy, margin)) {
        return true;
    }
    /* (session 74, P_ONE_WAY) against a human, INFANTRY for an outpost
     * carried there on the fuel one way where there and back is too far:
     * the jet flies for home after and, run dry, is built again at its HQ
     * (The owner: "There is no cost in losing a plane other than the
     * time to regenerate"; he ran dry twice himself that game). 1,200 px
     * on a tank for 760. */
    return g_param[P_ONE_WAY] && vs_human(n)
           && (n->m->flags & MECH_CARRYING)
           && n->s->cargo == NM_CARGO_CAPTURE
           && n->m->fuel >= trip_fuel(n, there, true) + 40;
}

/* (Session 68) Can a unit be carried to (gx, gy) from one of its bases on
 * a full tank? */
static bool base_reach(const Nm *n, int gx, int gy)
{
    int full = kidou(n)->stats.fuel;
    for (int b = 0; b < HZ_POST_CLEARED; b++) {
        if (is_base(n, b)
            && leg_ok_from(n, post(n, b)->x - 16, post(n, b)->y - 16, full,
                           gx, gy, g_param[P_FUEL_MARGIN])) {
            return true;
        }
    }
    return false;
}

/* The thinks an attack drop waits for a place its drive passes (hovering
 * while they are tried, flying to one found) before it goes down as it
 * did before the drive test. */
#define NM_DROP_HOVER 180

/* In missile reach at (x, y), but a block next to it out of it: the
 * edge, where an attack unit put down out of reach sticks when it drives
 * a few px on into it. */
static bool missile_edge(Nm *n, int x, int y)
{
    for (int dy = -32; dy <= 32; dy += 32) {
        for (int dx = -32; dx <= 32; dx += 32) {
            if (!(danger_at(n, x + dx, y + dy) & DANGER_MISSILE)) {
                return true;
            }
        }
    }
    return false;
}

/* (Session 68) Its attack units, looked at once a second. On ABGRUND the
 * AI put them down by a slope, which each drove at with its fuel going
 * and never moved, till it ran dry: one that, two seconds running, hasn't
 * moved 8 px while its fuel went down is lifted and put down again
 * (decide), the nearest the mech of those it can go for: out of missile
 * reach, or at its edge with the mech on half its HP or more. Near the
 * enemy HQ (NM_WATCH_HQ px) a unit is there; one stopped to fight burns
 * no fuel. s->stuck: the one to lift, else none. */
#define NM_WATCH_HQ 192
#define NM_ESCORT_NEAR 128      /* (P_CAPTURE_ESCORT) the enemy mech on
                                   foot this near an outpost stands by
                                   it */

static void watch_units(Nm *n)
{
    HzNightmare *s = n->s;
    if (s->frames % 60 != 0) {
        return;
    }
    const HzPost *e = post(n, enemy_hq(n));
    for (int k = 0; k < NM_DROPLOG; k++) {
        /* (session 68, P_COLD_JUDGE) an attack drop judged */
        if (s->drop_at[k] == 0 || g_param[P_COLD_JUDGE] <= 0
            || s->frames - s->drop_at[k] < (u32)g_param[P_COLD_JUDGE]) {
            continue;
        }
        if (post_hq_hp(e) >= s->drop_hq[k]) {
            int c = s->cold_next;
            s->cold_x[c] = s->drop_x[k];
            s->cold_y[c] = s->drop_y[k];
            s->cold_at[c] = s->frames | 1;
            s->cold_next = (u8)((c + 1) % NM_COLD);
            s->colds++;
        }
        s->drop_at[k] = 0;
    }
    /* (session 68, P_CAPTURE_ESCORT) */
    const HzMech *r = n->rival;
    if (!(r->flags & MECH_DESTROYED) && r->form == HZ_FORM_ROBOT) {
        for (int i = 0; i < HZ_POST_OUTPOSTS && i < NM_OUTPOSTS; i++) {
            const HzPost *pp = post(n, i);
            if ((pp->flags & POST_USED) && !post_ours(n, i)
                && chebyshev(r->x + 16 - pp->x, r->y + 16 - pp->y)
                   <= NM_ESCORT_NEAR) {
                s->robot_at[i] = s->frames | 1;
            }
        }
    }
    /* (session 68, P_FUTILE) */
    if (post_hq_hp(e) < s->ehq_hp_was) {
        s->att_lost = 0;
        s->ehq_hit_at = s->frames;
    }
    s->ehq_hp_was = post_hq_hp(e);
    u32 seen = side_slots(n->g, n->team);
    for (u32 i = 0; i < seen && i < NM_WATCHED; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        u64 bit = (u64)1 << i;
        if ((s->att_alive & bit) && !(u->flags & UNIT_CARRIED)
            && (!(u->flags & UNIT_ACTIVE) || (u->flags & UNIT_DYING))) {
            s->att_lost++;
            s->att_lost_most = (u16)imax(s->att_lost_most, s->att_lost);
        }
        if (unit_alive(u) && u->order == ORDER_ATTACK_HQ) {
            s->att_alive |= bit;
        } else {
            s->att_alive &= ~bit;
        }
    }
    s->stuck = ref_none();
    int best = 0x7FFFFFFF;
    u32 slots = side_slots(n->g, n->team);
    for (u32 i = 0; i < slots && i < NM_WATCHED; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (!(u->flags & UNIT_ACTIVE) || (u->flags & UNIT_DYING)) {
            s->watch_lifted &= ~((u64)1 << i);  /* the next one here */
        }
        if (!unit_alive(u) || u->order != ORDER_ATTACK_HQ
            || chebyshev(u->x + 12 - e->x, u->y + 12 - e->y)
               < NM_WATCH_HQ) {
            s->watch_still[i] = 0;
            s->watch_fuel[i] = 0;   /* the next one here: not burning */
            continue;
        }
        bool moved = chebyshev(u->x - s->watch_x[i],
                               u->y - s->watch_y[i]) >= 8;
        bool burning = u->fuel + 8 <= s->watch_fuel[i];
        /* (session 68) or dry: on VULKAN one stood empty 30 minutes,
         * 1,100 px from the enemy HQ */
        bool dry = u->fuel == 0;
        s->watch_x[i] = u->x;
        s->watch_y[i] = u->y;
        s->watch_fuel[i] = u->fuel;
        if (dry && s->watch_still[i] < 2) {
            s->watch_still[i] = 2;
            s->stalled++;
        } else if (moved || !burning) {
            if (!dry) {
                s->watch_still[i] = 0;
                continue;
            }
        } else if (s->watch_still[i] < 0xFF && ++s->watch_still[i] == 2) {
            s->stalled++;
        }
        if (s->watch_still[i] < 2 || (s->watch_lifted & ((u64)1 << i))
            || ((danger_at(n, u->x + 12, u->y + 12) & DANGER_MISSILE)
                && (!missile_edge(n, u->x + 12, u->y + 12)
                    || n->m->hp < kidou(n)->stats.hp / 2))) {
            continue;
        }
        int d = chebyshev(u->x - n->m->x, u->y - n->m->y);
        if (d < best) {
            best = d;
            s->stuck = unit_ref(u);
        }
    }
}

/* (Session 68, P_HUNT) The carried unit noted as a hunter put down. */
static void note_hunter(Nm *n)
{
    HzNightmare *s = n->s;
    s->hunter[s->hunter_next] = unit_ref(carried_unit(n->g, n->m));
    s->hunter_next = (u8)((s->hunter_next + 1) % NM_HUNTERS);
}

/* (Session 68) The carried unit's tank full but for a base's top-up
 * (`part` 1), or more than half full (2). */
static bool unit_filled(Nm *n, int part)
{
    const HzUnit *u = carried_unit(n->g, n->m);
    int full = unit_stats_for(n->g, u->type)->fuel;
    return (part == 1) ? u->fuel + 0x28 >= full : u->fuel * 2 > full;
}

/* (Session 68) The attack unit carried made a guard (ordered to stand on
 * the way, put down at home). */
static void make_guard(Nm *n)
{
    n->s->cargo = NM_CARGO_GUARD;
    n->s->reordered = false;
    n->s->kept_home++;
    set_task(n, NM_TASK_DECIDE);
}

/* (Session 70, P_REGION) The attack unit carried, going down at (x, y)
 * where no ground joins it to the enemy HQ: a TAX-52 ordered to stand
 * guard and put down there all the same, anything else a guard at home.
 * Made guards at home, in the owner's game of 8 November taken over at
 * 12 minutes (seed 3), they left its half to the turtle's drops, and it
 * lost its outposts 5 to 7; put down there blind on attack HQ, they
 * fought the turtle's units by the wall till they ran dry, and it took
 * outposts 5 and 4 (on his side) and his HQ from there. */
static void make_field(Nm *n, int x, int y)
{
    if (carried_unit(n->g, n->m)->type != UNIT_TAX) {
        make_guard(n);
        return;
    }
    n->s->cargo = NM_CARGO_FIELD;
    n->s->final_x = (s16)x;
    n->s->final_y = (s16)y;
    n->s->reordered = false;
    n->s->kept_home++;
    set_task(n, NM_TASK_DECIDE);
}

/* (Session 70, P_REGION) May the attack unit carried go down with the
 * mech at (x, y) (its drive passing or not)? Anything else may. */
static bool carried_region_ok(Nm *n, int x, int y)
{
    if (n->s->cargo != NM_CARGO_ATTACK || !(n->m->flags & MECH_CARRYING)) {
        return true;
    }
    const HzUnit *u = carried_unit(n->g, n->m);
    return (u->flags & UNIT_BOAT) || region_ok(n, u->type, x, y);
}

/* (Session 68) No place whose drive passes for the attack unit carried
 * (P_NO_BLIND): it is made a guard (ordered to stand on the way, put
 * down at home). It was put down where the drive failed, as before: in
 * The owner's game of 30 October (ABGRUND, as P2) 958 of 987 drives
 * failed, his SAM-42s round his HQ pushing its drops out from 640 px to
 * 1,380, and its TAX-52s stopped behind the river and the cliffs. */
static bool no_drop(Nm *n)
{
    if (!g_param[P_NO_BLIND] || n->s->cargo != NM_CARGO_ATTACK) {
        return false;
    }
    make_guard(n);
    return true;
}

/* (Session 68, P_DROP_FLY) An attack unit, INFANTRY or hunter put down
 * flying over its place at speed, not stopped there. The drop zeroes the
 * jet's velocity, but it is back at a missile's 2 px a frame in 21
 * frames and 21 px, so a missile come up behind doesn't catch it as it
 * does a jet stopped. At 32:20 in the owner's game of 31 October,
 * 06:53, the owner's jet came round behind its HQ, put a TAX-52 down
 * at 3 px a frame and flew on round, untouched: "Why aren't you learning
 * from me?" Within
 * 160 px of the place the jet heads for it without braking, and the
 * unit goes down at the first place within P_DROP_FLY px of it it may
 * (an attack unit's: where its drive passes); past it twice, it stops
 * there as before. True while it does. */
static bool fly_drop(Nm *n, bool tried)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    int r = g_param[P_DROP_FLY];
    int kinds = g_param[P_FLY_KINDS];
    if (r <= 0 || !vs_human(n) || s->fly_misses >= 2
        || !((s->cargo == NM_CARGO_ATTACK && (kinds & 1))
             || (s->cargo == NM_CARGO_CAPTURE && (kinds & 2))
             || (s->cargo == NM_CARGO_HUNT && (kinds & 4)))
        || (carried_unit(n->g, m)->flags & UNIT_BOAT)) {
        return false;
    }
    int d = chebyshev(s->goal_x - m->x, s->goal_y - m->y);
    if (d > 160) {
        return false;
    }
    if (s->cargo == NM_CARGO_CAPTURE) {
        r = imin(r, 8);     /* INFANTRY on the outpost, not by it */
    }
    if (d <= r && droppable(n, m->x, m->y)) {
        int v = NM_DRIVE_GOOD;
        if (s->cargo == NM_CARGO_ATTACK && tried) {
            /* in the place's block, the place's drive (the atlas's,
             * mostly); else where it is */
            bool same = ((m->x + 16) >> 5) == ((s->goal_x + 16) >> 5)
                        && ((m->y + 16) >> 5) == ((s->goal_y + 16) >> 5);
            v = same ? drive_ok(n, s->goal_x, s->goal_y, s->nest_goal)
                     : drive_ok(n, m->x & ~7, m->y & ~7, s->nest_goal);
        }
        if (v == NM_DRIVE_GOOD) {
            if (press(n, HZ_PAD_A)) {
                s->fly_drops++;
            }
            return true;
        }
    }
    if (d <= r && slow(m)) {
        /* stopped over it after all (the places passed failed their
         * drive): as before, which tries others or keeps it home */
        return false;
    }
    if (d <= r) {
        s->fly_near = true;
    } else if (s->fly_near && d > 2 * r) {
        s->fly_near = false;
        s->fly_misses++;
        s->fly_missed++;
    }
    fly_through(n, s->goal_x, s->goal_y);
    return true;
}

static void task_deliver(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    if (!(m->flags & MECH_CARRYING) || s->age > 6000) {
        if (!(m->flags & MECH_CARRYING) && s->stage == 2) {
            if (s->cargo == NM_CARGO_CAPTURE) {
                s->captures_sent++;
            } else if (s->cargo == NM_CARGO_WALL) {
                s->walled++;
            } else if (s->cargo == NM_CARGO_GUARD
                       || s->cargo == NM_CARGO_FIELD) {
                s->guarded++;
                int type = carried_unit(n->g, m)->type;
                if (type == UNIT_ST) {
                    s->guard_boats++;
                } else if (type == UNIT_TURRET1 || type == UNIT_TURRET2) {
                    s->guard_turrets++;
                }
            } else if (s->cargo == NM_CARGO_GATHER) {
                s->gathered++;
            } else if (s->cargo == NM_CARGO_POST) {
                s->post_guarded++;
            } else if (s->cargo == NM_CARGO_SUPPLY) {
                s->supplies++;          /* (session 74) */
            } else if (s->cargo == NM_CARGO_HUNT) {
                s->hunts++;
                note_hunter(n);
            } else if (s->cargo == NM_CARGO_LAUNCH) {
                s->launched++;
                const HzUnit *lead = ref_unit_ptr_c(n->g, s->lead);
                if (lead == NULL || !unit_alive(lead)
                    || lead->order != ORDER_ATTACK_HQ) {
                    s->lead = s->lift;  /* the first of the group sent */
                }
            } else {
                s->delivered++;
                s->attack_at = s->frames;
                int k = s->drop_next;
                s->drop_x[k] = m->x;
                s->drop_y[k] = m->y;
                s->drop_at[k] = s->frames | 1;
                s->drop_hq[k] = post_hq_hp(post(n, enemy_hq(n)));
                s->drop_next = (u8)((k + 1) % NM_DROPLOG);
                if (s->nest_goal) {
                    s->nests++;
                }
            }
        }
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (!airborne(n)) {
        return;
    }
    /* the enemy jet close: the unit down here if it can be (it drives on
     * by itself), and the mech free to turn on it */
    const HzMech *e = n->rival;
    if (g_param[P_EVADE] > 0 && rival_jet(n) && e->form != HZ_FORM_CARRYING
        && chebyshev(e->x - m->x, e->y - m->y) < g_param[P_EVADE]
        && jet_attacking(n) && droppable(n, m->x, m->y)) {
        press(n, HZ_PAD_A);
        return;
    }
    int fx = s->final_x, fy = s->final_y;
    /* (session 68) an attack unit's drop tried before the mech sets out
     * (hovering where it plans, NM_DROP_HOVER thinks at most): one it
     * waited on out there, or flew on from to try more, it ran short of
     * fuel by, and its nearest base was under the enemy's missiles */
    bool tried = s->cargo == NM_CARGO_ATTACK && g_param[P_DRIVE] > 0
                 && s->drop_hover < NM_DROP_HOVER && !attack_boat(n);
    if (s->stage == 0) {                /* plan the next leg */
        if (s->cargo == NM_CARGO_ATTACK && g_param[P_BOUNCES] > 0
            && s->bounces >= g_param[P_BOUNCES]) {
            /* (session 68) turned back again and again: in the owner's
             * game of 30 October (ABGRUND, TYPE A) it set out with a
             * TAX-52 from its outpost by his SAM-42s 15 times in two
             * minutes, hit on the way round them and short of fuel each
             * time, while his TAX-52s could have shot its HQ */
            s->bounces = 0;
            s->bounced++;
            make_guard(n);
            return;
        }
        if ((s->cargo == NM_CARGO_ATTACK || s->cargo == NM_CARGO_HUNT)
            && !unit_filled(n, 2)) {
            /* (session 68) its tank half empty or less (one lifted dry):
             * where its fuel takes it to fire (P_PARK), else to the
             * nearest base first, and filled there */
            if (s->cargo == NM_CARGO_ATTACK
                && park_drop(n, &s->final_x, &s->final_y)) {
                s->parks++;
                s->goal_x = s->final_x;
                s->goal_y = s->final_y;
                s->stage = 2;
                return;
            }
            int at = nearest_base(n, mx(n), my(n));
            s->goal_post = (u8)at;
            s->goal_x = (s16)(post(n, at)->x - 16);
            s->goal_y = (s16)(post(n, at)->y - 16);
            s->stage = 1;
            return;
        }
        int to_final = chebyshev(fx - m->x, fy - m->y);
        if (leg_ok(n, fx, fy, g_param[P_FUEL_MARGIN])
            && !drop_hot(n, fx, fy)) {
            int v = tried ? drive_ok(n, fx, fy, s->nest_goal) : NM_DRIVE_GOOD;
            if (v != NM_DRIVE_GOOD) {
                s->drop_hover++;
                if (v == NM_DRIVE_BAD) {
                    attack_drop(n, &s->final_x, &s->final_y);
                    s->nest_goal = false;
                    if (drive_cached(n, s->final_x, s->final_y, false)
                        == NM_DRIVE_BAD) {
                        if (no_drop(n)) {
                            return;
                        }
                        s->drop_hover = NM_DROP_HOVER;  /* none better */
                    }
                }
                return;
            }
            s->goal_x = (s16)fx;
            s->goal_y = (s16)fy;
            if (tried) {
                s->drop_hover = 0;      /* the drop's own wait */
            }
            s->stage = 2;
            return;
        }
        int worth;
        if (s->cargo == NM_CARGO_HUNT
            && hunt_drop_at(n, true, &s->final_x, &s->final_y, &worth)) {
            /* (session 68) a hunter by a SAM-42 or nowhere: another in
             * reach now */
            s->goal_x = s->final_x;
            s->goal_y = s->final_y;
            s->stage = 2;
            return;
        }
        /* (session 67) at a base on less than a full tank (it picked the
         * unit up short): filled here first. Planning the trip on what
         * was left sent a player's side out over STRAND's water, where
         * it could put nothing down, until the tank ran dry. */
        int at = nearest_base(n, mx(n), my(n));
        if (base_dist(n, at, mx(n), my(n)) < 24
            && m->fuel < kidou(n)->stats.fuel - 40) {
            s->goal_post = (u8)at;
            s->goal_x = (s16)(post(n, at)->x - 16);
            s->goal_y = (s16)(post(n, at)->y - 16);
            s->stage = 1;
            return;
        }
        /* a base on the way to refuel at */
        int best = -1, best_d = to_final - 64;
        for (int i = 0; i < HZ_POST_CLEARED; i++) {
            if (!is_base(n, i)) {
                continue;
            }
            int there = base_dist(n, i, mx(n), my(n));
            if (there < 24) {
                continue;               /* this one: refuelled already */
            }
            if (trip_fuel(n, there, true) + g_param[P_FUEL_MARGIN]
                > m->fuel) {
                continue;
            }
            int d = base_dist(n, i, fx + 16, fy + 16);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best >= 0) {
            s->goal_post = (u8)best;
            s->goal_x = (s16)(post(n, best)->x - 16);
            s->goal_y = (s16)(post(n, best)->y - 16);
            s->stage = 1;
            return;
        }
        if (s->cargo == NM_CARGO_HUNT) {
            /* (session 68) never short of its SAM-42: a guard, home */
            s->cargo = NM_CARGO_GUARD;
            guard_drop(n, &s->final_x, &s->final_y);
            return;
        }
        /* no base nearer: an attack unit to the best drop the fuel
         * reaches; else as far towards it as the fuel goes, leaving the
         * way back (the farthest point on the line that leg_ok passes,
         * in 32 px steps) */
        bool untried = false, nest = false;
        s16 sx = 0, sy = 0;
        bool scan = s->cargo == NM_CARGO_ATTACK
                    && attack_drop_at(n, NM_REACH_MECH, g_param[P_DROP_RANGE],
                                      &sx, &sy, &untried, &nest);
        if (scan && untried && tried) {
            s->drop_hover++;
            return;
        }
        if (scan && (nest || !(tried && drive_cached(n, sx, sy, false)
                                        == NM_DRIVE_BAD))) {
            s->goal_x = sx;
            s->goal_y = sy;
            s->final_x = sx;
            s->final_y = sy;
            s->nest_goal = nest;
            if (tried) {
                s->drop_hover = 0;
            }
            s->stage = 2;
            return;
        }
        int here = nearest_base(n, mx(n), my(n));
        int reach = 0;
        for (int r = 32; r < to_final; r += 32) {
            int gx = m->x + (fx - m->x) * r / imax(to_final, 1);
            int gy = m->y + (fy - m->y) * r / imax(to_final, 1);
            if (leg_ok(n, gx, gy, g_param[P_FUEL_MARGIN])) {
                reach = r;
            }
        }
        for (int r = reach; tried && r >= 32; r -= 32) {
            /* (session 68) of the places by the line, the farthest whose
             * drive passes (the line from the base crossed ABGRUND's
             * slopes, and each unit put down there stuck on one) */
            int gx = m->x + (fx - m->x) * r / imax(to_final, 1);
            int gy = m->y + (fy - m->y) * r / imax(to_final, 1);
            int v = attack_ring(n, gx, gy, 0, 96, 32, &s->goal_x,
                                &s->goal_y);
            if (v == NM_DRIVE_UNTRIED) {
                s->drop_hover++;
                return;
            }
            if (v == NM_DRIVE_GOOD
                && leg_ok(n, s->goal_x, s->goal_y, g_param[P_FUEL_MARGIN])) {
                s->drop_hover = 0;
                s->stage = 2;
                return;
            }
        }
        if (s->cargo == NM_CARGO_ATTACK) {
            /* (session 68) none passing: put down as before, untried
             * there too */
            if (tried && no_drop(n)) {
                return;
            }
            s->drop_hover = NM_DROP_HOVER;
        }
        if (scan) {
            s->goal_x = sx;
            s->goal_y = sy;
            s->final_x = sx;
            s->final_y = sy;
            s->stage = 2;
            return;
        }
        /* (session 67) the farthest of those it can put the unit down by
         * (a place in 96 px of it, and that place in reach too; (s. 68)
         * not drop_hot; P_ATLAS_LINE, an attack unit's where the atlas
         * passes) */
        bool by_atlas = atlas_line(n);
        for (; reach >= 32; reach -= 32) {
            int gx = m->x + (fx - m->x) * reach / imax(to_final, 1);
            int gy = m->y + (fy - m->y) * reach / imax(to_final, 1);
            if ((by_atlas ? atlas_ring(n, gx, gy, 96, &s->goal_x, &s->goal_y)
                          : drop_ring(n, gx, gy, 0, 96, &s->goal_x,
                                      &s->goal_y))
                && leg_ok(n, s->goal_x, s->goal_y,
                          g_param[P_FUEL_MARGIN])) {
                break;
            }
        }
        if (reach < 32 && by_atlas && base_dist(n, here, mx(n), my(n)) < 24
            && m->fuel >= kidou(n)->stats.fuel - 40) {
            /* (session 68) full at a base and no place the atlas passes
             * in reach: a guard, home */
            s->atlas_kept++;
            make_guard(n);
            return;
        }
        if (reach < 32 && base_dist(n, here, mx(n), my(n)) < 24
            && m->fuel >= kidou(n)->stats.fuel - 40
            && drop_ring(n, m->x, m->y, 0, 160, &s->goal_x, &s->goal_y)) {
            /* (session 67) full at a base and still nowhere in reach: put
             * down here, the mech free again */
            s->stage = 2;
            return;
        }
        if (reach < 32) {
            /* stuck here: refuel where it is */
            s->goal_post = (u8)here;
            s->goal_x = (s16)(post(n, here)->x - 16);
            s->goal_y = (s16)(post(n, here)->y - 16);
            s->stage = 1;
            return;
        }
        s->stage = 2;
        return;
    }
    if (s->stage == 1) {                /* to a base, and refuel */
        if (g_param[P_BASE_LOST] && !is_base(n, s->goal_post)) {
            /* (session 68) the base taken on the way: planned again. On
             * EISFREI (as P1, against the turtle) it hovered over an
             * outpost the turtle had just taken, its tank filling no
             * more, till the task's time ran out (100 s) */
            s->stage = 0;
            return;
        }
        int d = fly_to(n, s->goal_x, s->goal_y, 4);
        if (d <= 8 && m->fuel >= kidou(n)->stats.fuel - 40
            && unit_filled(n, 1)) {
            s->stage = 0;
            s->drop_hover = 0;          /* (session 68) tried from here */
        }
        return;
    }
    if (!carried_region_ok(n, s->goal_x, s->goal_y)) {
        /* (session 70, P_REGION) an attack unit going where no ground
         * joins it to the enemy HQ (no drive passing, it was going down
         * blind): on guard there */
        make_field(n, s->goal_x, s->goal_y);
        return;
    }
    if (drop_hot(n, s->goal_x, s->goal_y)) {
        /* (session 68) too hot there now (SAMs put down, or the goal the
         * old ways' own): planned again */
        s->hot_goals++;
        s->stage = 0;
        return;
    }
    if (fly_drop(n, tried)) {
        return;
    }
    int d = fly_to(n, s->goal_x, s->goal_y, 4);
    if (d <= 6 && slow(m)) {
        if (tried && !(danger_at(n, m->x + 16, m->y + 16) & DANGER_MISSILE)) {
            /* (session 68) an attack unit goes down only where its drive
             * passes (the goal's place: the mech is within 6 px of it),
             * else at the nearest place that does; never waiting in
             * missile reach. On ABGRUND a goal put down untried was a
             * slope, where the unit sat till its fuel ran out, and the
             * next four after it. */
            s->drop_hover++;
            int v = landable(n, m->x, m->y)
                    ? drive_ok(n, s->goal_x, s->goal_y, s->nest_goal)
                    : NM_DRIVE_BAD;
            if (v == NM_DRIVE_GOOD) {
                press(n, HZ_PAD_A);
                return;
            }
            if (v == NM_DRIVE_UNTRIED
                || attack_ring(n, m->x, m->y, 8, 96, 8, &s->goal_x,
                               &s->goal_y) != NM_DRIVE_BAD) {
                return;         /* hovering, or to the place found */
            }
            if (no_drop(n)) {
                return;
            }
            s->drop_hover = NM_DROP_HOVER;  /* none: as before */
        }
        bool boat = (carried_unit(n->g, m)->flags & UNIT_BOAT) != 0;
        if (!carried_region_ok(n, m->x, m->y)) {
            make_field(n, m->x, m->y);  /* (session 70) as above */
        } else if (droppable(n, m->x, m->y)) {
            press(n, HZ_PAD_A);
        } else if (boat) {
            /* (session 68) a boat: the water nearest, else home */
            if (!water_near(n, m->x, m->y, 192, &s->goal_x, &s->goal_y)) {
                s->goal_post = (u8)nearest_base(n, mx(n), my(n));
                s->goal_x = (s16)(post(n, s->goal_post)->x - 16);
                s->goal_y = (s16)(post(n, s->goal_post)->y - 16);
                s->stage = 1;
            }
        } else if (!drop_ring(n, m->x, m->y, 8, 96, &s->goal_x, &s->goal_y)
                   && !drop_ring(n, m->x, m->y, 104, 320, &s->goal_x,
                                 &s->goal_y)) {
            /* (session 67) nowhere near to put it down: to a base and
             * plan again (it went 32 px on at a time, over water, till
             * the tank ran dry) */
            s->goal_post = (u8)nearest_base(n, mx(n), my(n));
            s->goal_x = (s16)(post(n, s->goal_post)->x - 16);
            s->goal_y = (s16)(post(n, s->goal_post)->y - 16);
            s->stage = 1;
        }
    }
}

static void task_service(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    if (!is_base(n, s->goal_post)) {
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (!airborne(n)) {
        return;
    }
    const HzPost *b = post(n, s->goal_post);
    int d = fly_to(n, b->x - 16, b->y - 16, 4);
    const HzMechStats *k = kidou(n);
    if (d <= 16 && m->fuel >= k->stats.fuel * g_param[P_SVC_FUEL] / 100
        && m->hp >= k->stats.hp * g_param[P_SVC_HP] / 100
        && m->ammo >= imin(k->stats.ammo, g_param[P_SVC_AMMO])) {
        set_task(n, NM_TASK_DECIDE);
    }
}

static void task_wait(Nm *n)
{
    HzNightmare *s = n->s;
    const HzPost *b = post(n, s->goal_post);
    if (!airborne(n)) {
        return;
    }
    fly_to(n, b->x - 16, b->y - 16, 4);
    if (s->age >= 20) {
        set_task(n, NM_TASK_DECIDE);
    }
}

/* -- fighting on foot --------------------------------------------------- */

/* A fight's target (HzNightmare.target): an enemy unit, or the enemy mech
 * on foot. As the shots see it: its (x, y) and size, and how it moves (8.8
 * px a frame); (session 75) with PIXELS, its shape (`shaped`: hitmask.c),
 * which a bullet must touch. */
typedef struct {
    int x, y, size;
    int vx, vy;
    bool shaped;
    HzShape shape;
} Tgt;

static void unit_seen_vel(const Nm *n, const HzUnit *t, int *vx, int *vy);

static int tcx(const Tgt *t)
{
    return t->x + t->size / 2;
}

static int tcy(const Tgt *t)
{
    return t->y + t->size / 2;
}

/* The target, false when it is gone: a unit dead, or the enemy mech
 * destroyed or in the air (ground shots reach only a robot). */
static bool fight_target(const Nm *n, Tgt *t)
{
    HzRef id = n->s->target;
    if (id.kind == REF_MECH) {
        const HzMech *e = n->rival;
        if ((e->flags & MECH_DESTROYED) || e->form != HZ_FORM_ROBOT) {
            return false;
        }
        t->x = e->x;
        t->y = e->y;
        t->size = e->size;
        t->vx = e->vel_x;
        t->vy = e->vel_y;
        t->shaped = hitmask_on(n->g);
        if (t->shaped) {
            hitmask_mech(n->g, e, &t->shape);
        }
        return true;
    }
    const HzUnit *u = ref_unit_ptr_c(n->g, id);
    if (u == NULL || !unit_alive(u) || !unit_enemy(n, u)) {
        return false;
    }
    t->x = u->x;
    t->y = u->y;
    t->size = u->size;
    unit_seen_vel(n, u, &t->vx, &t->vy);
    t->shaped = hitmask_on(n->g);
    if (t->shaped) {
        hitmask_unit(n->g, u, &t->shape);
    }
    return true;
}

/* An enemy unit's velocity as the mech has seen it move (8.8 px a frame;
 * 0 for an own unit). */
#define NM_TRACK_FRAMES 8

static void unit_seen_vel(const Nm *n, const HzUnit *t, int *vx, int *vy)
{
    if (t->side == n->team || t->slot >= n->s->track_cap) {
        *vx = *vy = 0;
        return;
    }
    *vx = n->s->track[t->slot].vx;
    *vy = n->s->track[t->slot].vy;
}

/* (Session 61) Room in the track list for `slots` enemy slots, the new
 * ones zeroed (nothing seen there yet); false if there is no memory (the
 * slots past the list are then not tracked: seen still). */
static bool track_room(HzNightmare *s, u32 slots)
{
    if (slots <= s->track_cap) {
        return true;
    }
    u32 cap = (s->track_cap == 0) ? 64 : s->track_cap;
    while (cap < slots) {
        cap *= 2;
    }
    NmTrack *t = realloc(s->track, cap * sizeof *t);
    if (t == NULL) {
        return false;
    }
    memset(t + s->track_cap, 0, (cap - s->track_cap) * sizeof *t);
    s->track = t;
    s->track_cap = cap;
    return true;
}

void nightmare_reset(HzNightmare *dst, const HzNightmare *own)
{
    NmTrack *t = own->track;
    u32 cap = own->track_cap;
    memset(dst, 0, sizeof *dst);
    if (t != NULL) {
        memset(t, 0, cap * sizeof *t);
    }
    dst->track = t;
    dst->track_cap = cap;
}

void nightmare_copy(HzNightmare *dst, const HzNightmare *src)
{
    if (dst == src) {
        return;
    }
    NmTrack *t = dst->track;
    u32 cap = dst->track_cap;
    *dst = *src;
    dst->track = t;
    dst->track_cap = cap;
    if (!track_room(dst, src->track_cap)) {
        fprintf(stderr, "hz: out of memory for the nightmare AI\n");
        exit(1);
    }
    if (src->track_cap > 0) {
        memcpy(dst->track, src->track, src->track_cap * sizeof *src->track);
    }
    if (dst->track_cap > src->track_cap) {
        memset(dst->track + src->track_cap, 0,
               (dst->track_cap - src->track_cap) * sizeof *dst->track);
    }
}

/* Every NM_TRACK_FRAMES frames: where each enemy unit has gone since. */
static void track_units(Nm *n)
{
    HzNightmare *s = n->s;
    if (s->frames % NM_TRACK_FRAMES != 0) {
        return;
    }
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    track_room(s, en);
    if (en > s->track_cap) {
        en = s->track_cap;
    }
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        NmTrack *t = &s->track[i];
        if (g_param[P_TENDED_CARRY] && vs_human(n) && (t->life & UNIT_ACTIVE)
            && (u->flags & UNIT_ACTIVE) && (u->flags & UNIT_CARRIED)
            && !(u->flags & UNIT_DYING)) {
            /* (session 68, P_TENDED_CARRY) carried: its HP and missiles
             * as they were, to be seen gone up where it is put down. In
             * The owner's game of 31 October, 18:31 (VULKAN, the
             * server's AI P1) he lifted his hurt GRM-34As by its outpost
             * 450 px north of its HQ, which he had taken, and put them
             * down again repaired; the mech, seeing each new, went for
             * them and was destroyed some 60 times in 36 minutes */
            t->vx = 0;
            t->vy = 0;
            t->x = u->x;
            t->y = u->y;
            continue;
        }
        if (!unit_alive(u) || !(t->life & UNIT_ACTIVE)) {
            t->vx = 0;
            t->vy = 0;
            t->tended_at = 0;
        } else {
            t->vx = (s16)((u->x - t->x) * 256 / NM_TRACK_FRAMES);
            t->vy = (s16)((u->y - t->y) * 256 / NM_TRACK_FRAMES);
            if (u->hp > t->hp || u->missiles > t->missiles) {
                t->tended_at = s->frames;
            }
        }
        t->x = u->x;
        t->y = u->y;
        t->life = unit_alive(u) ? u->flags : 0;
        t->hp = u->hp;
        t->missiles = u->missiles;
    }
}

/* Does unit o cover the point ctx (x, y), as a bullet's test has it? */
static bool covers_point(const HzUnit *o, const void *ctx)
{
    const int *pt = ctx;
    return pt[0] > o->x && pt[0] - o->x < o->size && pt[1] > o->y
           && pt[1] - o->y < o->size;
}

/* (Session 75) PIXELS: does the mech's bullet with its point at (px, py)
 * touch t's shape, t at (ox, oy)? (A miss flies on, as in the game.) */
static bool bullet_touches(const Nm *n, int px, int py, const Tgt *t,
                           int ox, int oy)
{
    HzShot b;
    memset(&b, 0, sizeof b);
    b.x = (s16)(px - 4);
    b.y = (s16)(py - 4);
    b.attr = (u16)(0x254C + (n->team ? 1 : 0));    /* shot_fire_bullet's */
    HzShape bullet;
    hitmask_shot(n->g, &b, &bullet);
    HzShape there = t->shape;
    there.x = (s16)ox;
    there.y = (s16)oy;
    return hitmask_touch(&bullet, &there);
}

/* Does a bullet the mech fires from (x, y) along `dir` hit unit t (going
 * on as it has been seen to)? The frames to the hit, 0 none: a tile that
 * stops shots, or one of its own side's units, first. The shot every
 * frame, its point the mech's (x, y) + $C + 4. (Session 68) It moves
 * shot_life - 1 times: the frame its life runs out it explodes where it
 * is. Tried one move further, a TAX-52 1 px out of reach on ABGRUND took
 * 60 shots, the mech's ammo, and the HQ lost 270 HP while it went for
 * more. */
static int bullet_hits_from(const Nm *n, int x, int y, int dir,
                            const Tgt *t)
{
    const HzGame *g = n->g;
    const HzUnitStats *k = &g->assets->mech_stats.stats;
    s32 bx = (s32)(x + 0x10) * 256;
    s32 by = (s32)(y + 0x10) * 256;
    s32 vx = (s16)(u16)(SINE_TABLE[dir & 15].x * k->shot_speed);
    s32 vy = (s16)(u16)(SINE_TABLE[dir & 15].y * k->shot_speed);
    s32 tx = (s32)t->x * 256;
    s32 ty = (s32)t->y * 256;
    for (int f = 1; f < k->shot_life; f++) {
        bx += vx;
        by += vy;
        tx += t->vx;
        ty += t->vy;
        int px = bx >> 8, py = by >> 8;
        int ox = tx >> 8, oy = ty >> 8;
        if (px > ox && px - ox < t->size && py > oy && py - oy < t->size
            && (!t->shaped || bullet_touches(n, px, py, t, ox, oy))) {
            return f;
        }
        if (px < 0 || py < 0 || px >= 0x800 || py >= 0x800) {
            return 0;
        }
        u16 blk = g->layout[(py >> 5) & 63][(px >> 5) & 63] & 0x1FF;
        u16 tw = g->blocks[blk][((py >> 3) & 3) * 4 + ((px >> 3) & 3)];
        if (tw & TILE_STOPS_SHOTS) {
            return 0;
        }
        /* an own unit in the way (its centre within a cell of the point:
         * no unit is over 32 px) */
        const int pt[2] = { px, py };
        if (any_unit_near(g, n->team, px - 32, py - 32, px + 32, py + 32,
                          NULL, covers_point, pt)) {
            return 0;
        }
    }
    return 0;
}

static int bullet_hits(const Nm *n, int dir, const Tgt *t)
{
    return bullet_hits_from(n, n->m->x, n->m->y, dir, t);
}

/* Can the robot stand at (x, y)? Ground it walks on, no one there. */
static bool walkable(Nm *n, int x, int y)
{
    if (!on_map(x, y)) {
        return false;
    }
    int gr = mech_ground_at(n->g, n->m, (s16)x, (s16)y);
    /* GROUND_BLOCKED, GROUND_WATER; (session 67) and GROUND_SPECIAL,
     * which a human's mech may land on but which pushes a robot back off
     * it (ABGRUND: it walked into it for 45 s while a tank shot its HQ) */
    if (gr == 1 || gr == 2 || gr == 4) {
        return false;
    }
    return !mech_land_blocked_at(n->g, n->m, (s16)x, (s16)y);
}

/* (Session 67) A place to land and fight from: landable, and plain land
 * (GROUND_LAND, not a human's special ground: see walkable). */
static bool fight_landable(Nm *n, int x, int y)
{
    return landable(n, x, y)
           && mech_ground_at(n->g, n->m, (s16)x, (s16)y) == 0;
}

/* The straight walk from the mech to (x, y) crosses only walkable
 * ground (checked every 8 px). */
static bool walk_clear(Nm *n, int x, int y)
{
    int dx = x - n->m->x, dy = y - n->m->y;
    int steps = chebyshev(dx, dy) / 8;
    for (int i = 1; i <= steps; i++) {
        if (!walkable(n, n->m->x + dx * i / steps,
                      n->m->y + dy * i / steps)) {
            return false;
        }
    }
    return true;
}

/* (Session 68) The robot on low ground can't take off (Mech_Update), and
 * walkable lets it walk there: on VULKAN it fought its way into a dip,
 * pressed A there for 3.7 minutes, and lost its HQ. The nearest plain
 * land within 192 px (8 px steps) it can walk to straight; not by one it
 * gave up on in the last 30 s (take_off). */
static bool take_off_spot(Nm *n, s16 *ox, s16 *oy)
{
    const HzNightmare *s = n->s;
    bool bad = s->toff_bad_at != 0 && s->frames - s->toff_bad_at < 1800;
    for (int r = 8; r <= 192; r += 8) {
        for (int dy = -r; dy <= r; dy += 8) {
            for (int dx = -r; dx <= r; dx += 8) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                int x = n->m->x + dx, y = n->m->y + dy;
                if (bad && chebyshev(x - s->toff_bad_x, y - s->toff_bad_y)
                           < 24) {
                    continue;
                }
                if (walkable(n, x, y)
                    && mech_ground_at(n->g, n->m, (s16)x, (s16)y) == 0
                    && walk_clear(n, x, y)) {
                    *ox = (s16)x;
                    *oy = (s16)y;
                    return true;
                }
            }
        }
    }
    return false;
}

/* The nearest place within 64 px (8 px steps) the robot can walk to and
 * hit t from with some heading. */
static bool firing_spot(Nm *n, const Tgt *t, s16 *ox, s16 *oy)
{
    for (int r = 8; r <= 64; r += 8) {
        for (int dy = -r; dy <= r; dy += 8) {
            for (int dx = -r; dx <= r; dx += 8) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                int x = n->m->x + dx, y = n->m->y + dy;
                bool hits = false;
                for (int d = 0; d < 16 && !hits; d++) {
                    hits = bullet_hits_from(n, x, y, d, t) != 0;
                }
                if (hits && walkable(n, x, y) && walk_clear(n, x, y)) {
                    *ox = (s16)x;
                    *oy = (s16)y;
                    return true;
                }
            }
        }
    }
    return false;
}

/* The even direction to press so a robot's heading turns towards `want`
 * (never one that would make it walk backwards instead); -1 none. */
static int turn_pad_dir(int dir, int want)
{
    int diff = (want - dir) & 15;
    if (diff == 0) {
        return -1;
    }
    int sign = (diff <= 8) ? 1 : -1;
    int need = (sign > 0) ? diff : 16 - diff;
    for (int de = imin(need, 6); de <= 6; de++) {
        int e = (dir + sign * de) & 15;
        if (!(e & 1)) {
            return e;
        }
    }
    for (int de = 5; de >= 1; de--) {
        int e = (dir + sign * de) & 15;
        if (!(e & 1)) {
            return e;
        }
    }
    return -1;
}

/* (Session 67) Is (x, y) by where the robot last stood walking and not
 * moving in a fight, less than 600 frames ago? Its own standing units
 * round the HQ (which the walk's checks don't see) can hold it there,
 * shooting nothing, while a tank takes the HQ apart. */
static bool near_stuck(const Nm *n, int x, int y)
{
    const HzNightmare *s = n->s;
    return s->stuck_at != 0 && s->frames - s->stuck_at < 600
           && chebyshev(x - s->stuck_x, y - s->stuck_y) < 40;
}

/* (Session 68) Anywhere from 40 to 112 px off the target to land and
 * fight from, wherever the mech last stood stuck. */
static bool fight_place_any(Nm *n, const Tgt *t)
{
    for (int k = 0; k < 16 * 10; k++) {
        int d = k & 15;
        int r = 40 + (k >> 4) * 8;
        if (fight_landable(n,
                           edge_snap(tcx(t) + SINE_TABLE[d].x * r / 32 - 16),
                           edge_snap(tcy(t) + SINE_TABLE[d].y * r / 32
                                     - 16))) {
            return true;
        }
    }
    return false;
}

/* (Session 68) Enemy unit r found with nowhere to fight it from in the
 * last NM_NOFIGHT_FRAMES. */
#define NM_NOFIGHT_FRAMES 1800

static bool nofight(const Nm *n, HzRef r)
{
    const HzNightmare *s = n->s;
    for (int k = 0; k < NM_NOFIGHT; k++) {
        if (s->nofight_at[k] != 0
            && s->frames - s->nofight_at[k] < NM_NOFIGHT_FRAMES
            && ref_eq(s->nofight[k], r)) {
            return true;
        }
    }
    return false;
}

/* A landing place from where the target will be: `range` px off by
 * preference, anywhere from 40 to 112 px, the side nearest the mech, out
 * of missiles if it can be, with a heading from it that would hit; not
 * where it last stood stuck. (Session 73) One by the map's edge is the
 * edge's own (edge_snap): the strip north of P2's HQ on ABGRUND has no
 * other. */
static bool fight_spot(Nm *n, const Tgt *t, int range, s16 *ox, s16 *oy)
{
    int lead = 40;
    int tx = tcx(t) + t->vx * lead / 256;
    int ty = tcy(t) + t->vy * lead / 256;
    int best_d = 0x7FFFFFFF;
    bool found = false;
    for (int k = 0; k < 16 * 10; k++) {
        int d = k & 15;
        int r = 40 + (k >> 4) * 8;
        int sx = edge_snap(tx + SINE_TABLE[d].x * r / 32 - 16);
        int sy = edge_snap(ty + SINE_TABLE[d].y * r / 32 - 16);
        if (!fight_landable(n, sx, sy) || near_stuck(n, sx, sy)) {
            continue;
        }
        int dd = chebyshev(sx - n->m->x, sy - n->m->y) + iabs(r - range) * 4;
        if (dd >= best_d) {
            continue;
        }
        if (danger_at(n, sx, sy) & DANGER_MISSILE) {
            dd += 2000;
        }
        /* a line of fire: the heading back along the ray ((session 73)
         * from a place moved to the edge, the heading from there) */
        int back = (d + 8) & 15;
        if (sx != tx + SINE_TABLE[d].x * r / 32 - 16
            || sy != ty + SINE_TABLE[d].y * r / 32 - 16) {
            back = bearing16(sx + 16, sy + 16, tx, ty);
        }
        if (!bullet_hits_from(n, sx, sy, back, t)
            && !bullet_hits_from(n, sx, sy, (back + 1) & 15, t)
            && !bullet_hits_from(n, sx, sy, (back + 15) & 15, t)) {
            dd += 500;
        }
        if (dd < best_d) {
            best_d = dd;
            *ox = (s16)sx;
            *oy = (s16)sy;
            found = true;
        }
    }
    return found;
}

static void task_fight(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    Tgt tg;
    const Tgt *t = &tg;
    if (!fight_target(n, &tg) || s->age > 1800) {
        const HzUnit *gone = ref_unit_ptr_c(n->g, s->target);
        if (gone != NULL && (gone->flags & UNIT_DYING)) {
            s->kills++;
            if (s->striking) {
                s->strike_kills++;      /* (session 74) */
                s->strike_row = 0;      /* (session 77) */
                s->strike_giveup_row = 0;
            }
        }
        s->target = ref_none();
        s->striking = false;
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (m->transform != 0) {
        return;
    }
    int dist = chebyshev(tcx(t) - mx(n), tcy(t) - my(n));
    if (m->flags & MECH_CARRYING) {
        /* the unit down first, where it is, on the way */
        if (slow(m) && landable(n, m->x, m->y)) {
            press(n, HZ_PAD_A);
            return;
        }
        s16 gx, gy;
        if (landable_ring(n, m->x, m->y, 0, 96, &gx, &gy)) {
            steer_to(n, gx, gy, 2);
        } else {
            /* (session 68) nowhere near: on towards it. On WALDUNG (TYPE
             * B) it stood carrying over the forest 45 s, pressing
             * nothing, while 20 units took its HQ */
            fly_to(n, tcx(t) - 16, tcy(t) - 16, 4);
        }
        return;
    }
    if (m->form != HZ_FORM_ROBOT) {
        /* fly to a spot beside it and land */
        if (!fight_spot(n, t, g_param[P_FIGHT_RANGE], &s->goal_x,
                        &s->goal_y)) {
            if (s->target.kind != REF_MECH && !fight_place_any(n, t)) {
                /* (session 68) nowhere by it to land: on STRAND (TYPE B)
                 * a boat shot its HQ from 1,024 HP to 0 while the jet
                 * hovered over the water by it for ten minutes, the fight
                 * timing out and taken up again. Left alone a while: the
                 * guards are for it (P_DEFEND, boats on the water) */
                s->nofight[s->nofight_next] = s->target;
                s->nofight_at[s->nofight_next] = s->frames;
                s->nofight_next = (u8)((s->nofight_next + 1) % NM_NOFIGHT);
                s->unreachable++;
                s->target = ref_none();
                set_task(n, NM_TASK_DECIDE);
                return;
            }
            s->goal_x = (s16)(tcx(t) - 16);
            s->goal_y = (s16)(tcy(t) - 16 - g_param[P_FIGHT_RANGE]);
            if (g_param[P_EDGE] && s->goal_y < 0) {
                /* (session 73) that side of it is off the map (in
                 * The owner's game of 10 November the jet held against
                 * the top edge 78 s over a man 10 px from it): the other */
                s->goal_y = (s16)(tcy(t) - 16 + g_param[P_FIGHT_RANGE]);
            }
        }
        int d = fly_to(n, s->goal_x, s->goal_y, 4);
        /* down now if here will do: landable, in reach of it and not on
         * top of it (a landing takes no speed off first) */
        bool here = fight_landable(n, m->x, m->y)
                    && !near_stuck(n, m->x, m->y);
        if (here && ((dist >= 32 && dist <= g_param[P_LAND_REACH])
                     || (d <= 10 && slow(m)))) {
            press(n, HZ_PAD_A);
            s->hover_n = 0;
        } else if (d <= 12) {
            /* (Session 68) At the spot, and it can't land: the spot was
             * landable, the place it stopped (2 px off, within the
             * flight's 4) not. On ABGRUND against the original's AI it
             * hovered there 3.5 minutes, the fight timing out and taken
             * up again, its money piling up unspent, and lost. After a
             * second the spot counts as one it was stuck at, and
             * fight_spot finds another. Counted while it swings round the
             * spot too, not only stopped: in the owner's game of 30
             * October, 10:43 (taken over at 18 minutes) it swung 7 px
             * each way over a spot by its HQ among its own units for 28
             * s, the count put back each swing, while one TAX-52 took the
             * HQ from 854 HP to 84. */
            if (++s->hover_n >= 60) {
                s->hover_n = 0;
                s->stuck_x = s->goal_x;
                s->stuck_y = s->goal_y;
                s->stuck_at = s->frames;
                s->unstuck++;
            }
        } else {
            s->hover_n = 0;
        }
        return;
    }
    /* on foot */
    int leave = s->sieging ? g_param[P_SIEGE_LEAVE] : 72;
    if (g_param[P_FIGHT_ON] && vs_human(n)
        && (danger_at(n, m->x, m->y) & DANGER_MISSILE)) {
        leave = 0;      /* (session 74) in their missiles' reach: to the end */
    }
    if (dist > 220 || m->hp < leave || m->ammo == 0) {
        take_off(n);                /* off: re-approach, or leave */
        if (m->hp < leave || m->ammo == 0) {
            s->target = ref_none();
            set_task(n, NM_TASK_DECIDE);
        }
        return;
    }
    /* (Session 59) The fire delay waited out as a player's mech must,
     * though the game lets a CPU's fire every 2 frames: The owner, "I
     * don't think it should get an unfair advantage with fire rates"
     * (the original's AI fires 7 frames apart or more). */
    bool can_fire = m->ammo > 0 && m->fire_delay == 0
                    && !(m->pad_prev & HZ_PAD_B);
    if (bullet_hits(n, m->dir, t)) {
        if (can_fire) {
            n->pad |= HZ_PAD_B;
            s->shots++;
        }
        return;                     /* stand and wait for the gun */
    }
    int best = -1, best_cost = 99;
    for (int d = 0; d < 16; d++) {
        if (!bullet_hits(n, d, t)) {
            continue;
        }
        int diff = (d - m->dir) & 15;
        int cost = (diff <= 8) ? diff : 16 - diff;
        if (cost < best_cost) {
            best_cost = cost;
            best = d;
        }
    }
    if (best >= 0) {
        int e = turn_pad_dir(m->dir, best);
        if (e >= 0) {
            n->pad |= dir_pad(e);
        }
        return;
    }
    /* nothing hits from here: a place near by that does, or walk
     * towards it (or turn to face it). (Session 67) Walking 45 frames
     * and never 4 px from where it began (what it walks into pushes it
     * back a pixel or two each frame): up again, to come down elsewhere.
     * (Session 68) Whatever its heading: walking between two headings it
     * takes each in turn, and held so it stood 40 s, shooting nothing,
     * while a TAX-52 took its HQ from 968 HP to 226 (ABGRUND, against
     * the original's AI). */
    if (chebyshev(m->x - s->stuck_x, m->y - s->stuck_y) <= 4) {
        if (++s->stuck_n >= 45) {
            s->stuck_n = 0;
            s->stuck_at = s->frames;
            s->unstuck++;
            press(n, HZ_PAD_A);
            return;
        }
    } else {
        s->stuck_x = m->x;
        s->stuck_y = m->y;
        s->stuck_n = 0;
    }
    int h = bearing16(mx(n), my(n), tcx(t), tcy(t));
    if (dist <= 110 && firing_spot(n, t, &s->goal_x, &s->goal_y)) {
        steer_to(n, s->goal_x, s->goal_y, 2);
        return;
    }
    if (dist > 90) {
        int walk = h & ~1;
        if ((h & 1) && ((s->frames / 24) & 1)) {
            walk = (h + 1) & 15;    /* between two: each in turn */
        }
        int diff = (walk - m->dir) & 15;
        if (diff >= 7 && diff <= 9) {
            int e = turn_pad_dir(m->dir, walk);
            if (e >= 0) {
                n->pad |= dir_pad(e);
            }
        } else {
            n->pad |= dir_pad(walk);
        }
    } else {
        int e = turn_pad_dir(m->dir, h);
        if (e >= 0) {
            n->pad |= dir_pad(e);
        }
    }
}

/* -- fighting in the air ------------------------------------------------ */

/* Does a bullet our jet fires now along `dir` hit the enemy jet (going on
 * at its velocity)? Shots_UpdateTeam: the shot's (x, y), from the mech's
 * + $C, within [ex, ex + $18) of the enemy's (x, y) on both axes. */
static int air_hits(const Nm *n, int dir)
{
    const HzMech *e = n->rival;
    const HzUnitStats *k = &n->g->assets->mech_stats.stats;
    s32 bx = (s32)(n->m->x + 0x0C) * 256;
    s32 by = (s32)(n->m->y + 0x0C) * 256;
    s32 vx = (s16)(u16)(SINE_TABLE[dir & 15].x * k->shot_speed);
    s32 vy = (s16)(u16)(SINE_TABLE[dir & 15].y * k->shot_speed);
    s32 ex = (s32)e->x * 256;
    s32 ey = (s32)e->y * 256;
    for (int f = 1; f < k->shot_life; f++) {
        bx += vx;
        by += vy;
        ex += e->vel_x;
        ey += e->vel_y;
        int dx = (bx >> 8) - (ex >> 8);
        int dy = (by >> 8) - (ey >> 8);
        if (dx >= 0 && dx < 0x18 && dy >= 0 && dy < 0x18) {
            return f;
        }
    }
    return 0;
}

static bool rival_jet(const Nm *n)
{
    const HzMech *e = n->rival;
    return !(e->flags & MECH_DESTROYED) && e->form != HZ_FORM_ROBOT
           && e->transform == 0;
}

/* Worth chasing the enemy jet: it carries a unit (it doesn't shoot back
 * while it does: the original's AI doesn't, a player can't for the turn
 * it takes), within P_DOG_RANGE (P_DOG_PAD on a human's pad); or, it
 * comes at the mech (within
 * P_AIR_GUARD), or any jet within P_DOG_ANY. Never into the enemy's
 * missiles, and with the HP, fuel and ammo for it. */
static bool dogfight_worth(const Nm *n)
{
    const HzMech *m = n->m;
    const HzMech *e = n->rival;
    if ((m->flags & MECH_CARRYING) || !rival_jet(n) || m->form == HZ_FORM_ROBOT
        || (danger_at(n, e->x, e->y) & DANGER_MISSILE)
        || m->fuel <= 500 || m->ammo < 16) {
        return false;
    }
    int d = chebyshev(e->x - m->x, e->y - m->y);
    if (e->form == HZ_FORM_CARRYING) {
        /* (session 68) not on a human's pad: chasing cost it its tasks
         * there for little (against the turtle 97 games won to 138 lost,
         * not chasing 135 to 93) */
        int range = n->cpu ? g_param[P_DOG_RANGE] : g_param[P_DOG_PAD];
        return d < range && m->hp >= 128;
    }
    return (d < g_param[P_AIR_GUARD] && m->hp >= 112 && jet_attacking(n))
           || (d < g_param[P_DOG_ANY] && m->hp >= 176);
}

/* (Session 69, P_DOG_GUARD) Against a human, the enemy jet carrying a
 * unit within P_DOG_GUARD of its HQ or an outpost of its own, and within
 * P_DOG_RANGE of the mech: chased till it puts the unit down. In
 * The owner's game of 1 November (LOCH, the server's AI P1) his jet
 * carried INFANTRY 500 px from his outpost to its outpost eight times
 * in 75 s, passing 60 to 250 px from its jet waiting there, which did
 * nothing till each was down; the eighth took the outpost. */
static bool dog_guard_worth(const Nm *n)
{
    const HzMech *m = n->m;
    const HzMech *e = n->rival;
    if (!vs_human(n) || g_param[P_DOG_GUARD] <= 0
        || (m->flags & MECH_CARRYING)
        || !rival_jet(n) || e->form != HZ_FORM_CARRYING
        || m->form == HZ_FORM_ROBOT
        || (danger_at(n, e->x, e->y) & DANGER_MISSILE)
        || m->fuel <= 500 || m->ammo < 16 || m->hp < 128
        || chebyshev(e->x - m->x, e->y - m->y) >= g_param[P_DOG_RANGE]) {
        return false;
    }
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        if (is_base(n, i)
            && chebyshev(e->x + 16 - post(n, i)->x, e->y + 16 - post(n, i)->y)
               <= g_param[P_DOG_GUARD]) {
            return true;
        }
    }
    return false;
}

/* (Session 69, P_IDLE_DOG) Against a human, with nothing else to do, the
 * enemy jet within P_IDLE_DOG, out of the missiles, with the HP, fuel and
 * ammo for a fight. The owner (1 November): "If I've deprived it of
 * money by taking all of the bases, it'll often sit and hover until it
 * has enough money to buy a unit". */
static bool idle_dog_worth(const Nm *n)
{
    const HzMech *m = n->m;
    const HzMech *e = n->rival;
    if (g_param[P_IDLE_BROKE]
        && ((s8)m->prod_state > 0
            || m->money >= cost_of(n, UNIT_INFANTRY, ORDER_OUTPOST))) {
        return false;
    }
    return vs_human(n) && g_param[P_IDLE_DOG] > 0
           && !(m->flags & MECH_CARRYING) && rival_jet(n)
           && m->form != HZ_FORM_ROBOT
           && !(danger_at(n, e->x, e->y) & DANGER_MISSILE)
           && m->fuel > 700 && m->ammo >= 32 && m->hp >= 176
           && chebyshev(e->x - m->x, e->y - m->y) < g_param[P_IDLE_DOG];
}

/* Into a chase, if one is worth it (dogfight_worth, dog_guard_worth;
 * idle_dog_worth too when `idle`). */
static bool start_dogfight(Nm *n, bool idle)
{
    HzNightmare *s = n->s;
    s->dog_guard = false;
    s->dog_idle = false;
    if (dogfight_worth(n)) {
    } else if (dog_guard_worth(n)) {
        s->dog_guard = true;
        s->dog_guards++;
    } else if (idle && idle_dog_worth(n)) {
        s->dog_idle = true;
        s->idle_dogs++;
    } else {
        return false;
    }
    s->dogfights++;
    set_task(n, NM_TASK_DOGFIGHT);
    return true;
}

/* The even direction to press so a jet's heading turns towards `want`
 * through it (a jet turns the short way to the pad's), -1 none. */
static int jet_turn_dir(int dir, int want)
{
    int diff = (want - dir) & 15;
    if (diff == 0) {
        return -1;
    }
    if (!(want & 1)) {
        return want;
    }
    return (diff < 8) ? ((want + 1) & 15) : ((want + 15) & 15);
}

static void task_dogfight(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    const HzMech *e = n->rival;
    if (e->flags & MECH_DESTROYED) {
        s->rival_kills++;
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (!rival_jet(n) || s->age > 900 || (m->flags & MECH_CARRYING)
        || m->hp < 80 || (danger_at(n, e->x, e->y) & DANGER_MISSILE)
        || (s->dog_guard && e->form != HZ_FORM_CARRYING)
        || (s->dog_idle && m->hp < 128)
        || (e->form != HZ_FORM_CARRYING
            && chebyshev(e->x - m->x, e->y - m->y)
               > imax(imax(g_param[P_AIR_GUARD], g_param[P_DOG_ANY]),
                      s->dog_idle ? g_param[P_IDLE_DOG] : 0) + 160)) {
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (!airborne(n)) {
        return;
    }
    bool can_fire = m->ammo > 0 && m->fire_delay == 0
                    && !(m->pad_prev & HZ_PAD_B);
    int dist = chebyshev(e->x - m->x, e->y - m->y);
    if (air_hits(n, m->dir)) {
        if (can_fire) {
            n->pad |= HZ_PAD_B;
            s->shots++;
        }
        /* hold the heading: on along it if it is one the pad gives,
         * else coast */
        if (!(m->dir & 1) && dist > 40) {
            n->pad |= dir_pad(m->dir);
        }
        return;
    }
    /* a heading that would hit, a short turn away */
    int best = -1, best_cost = 99;
    for (int d = 0; d < 16; d++) {
        if (!air_hits(n, d)) {
            continue;
        }
        int diff = (d - m->dir) & 15;
        int cost = (diff <= 8) ? diff : 16 - diff;
        if (cost < best_cost) {
            best_cost = cost;
            best = d;
        }
    }
    if (best >= 0 && best_cost <= 3) {
        int p = jet_turn_dir(m->dir, best);
        if (p >= 0) {
            n->pad |= dir_pad(p);
        }
        return;
    }
    /* pursue: where it will be when the mech gets there, from behind it
     * while still far */
    int lead = imin(40, dist / 4);
    int tx = e->x + e->vel_x * lead / 256;
    int ty = e->y + e->vel_y * lead / 256;
    int sp = imax(iabs(e->vel_x), iabs(e->vel_y));
    if (sp > 0 && dist > 120) {
        tx -= e->vel_x * 40 / sp;
        ty -= e->vel_y * 40 / sp;
    }
    if (dist < 36) {
        /* too close (the jets would bounce): off to its side */
        tx = m->x + (m->x - e->x) * 2;
        ty = m->y + (m->y - e->y) * 2;
    }
    int h = bearing16(m->x, m->y, tx, ty);
    int pd = h & ~1;
    if ((h & 1) && ((s->frames / 8) & 1)) {
        pd = (h + 1) & 15;
    }
    n->pad |= dir_pad(pd);
}

/* -- deciding ----------------------------------------------------------- */

static int type_threat(int type)
{
    switch (type) {
    case UNIT_TAX:      return 4;
    case UNIT_GRM:      return 3;
    case UNIT_AMR:      return 3;
    case UNIT_TURRET1:  return 3;       /* (session 60) the TAX-52's gun */
    case UNIT_ST:       return 2;
    case UNIT_TURRET2:  return 2;
    case UNIT_SAM:      return 2;
    case UNIT_FWA:      return 2;
    case UNIT_INFANTRY: return 1;
    default:            return 1;
    }
}

/* (Session 68) Was the mech destroyed within P_DEATH_RADIUS px of (x, y)
 * in the last P_DEATH_AVOID frames? The owner's game of 30 October
 * (ABGRUND, TYPE A): he put SAM-42s by its outpost at the far side of
 * the map, and it flew from its HQ to fight them eight times in three
 * minutes, destroyed each time by them, buying nothing ($7,000 kept)
 * till he took the outpost and two more. */
static bool grave_near(const Nm *n, int x, int y)
{
    const HzNightmare *s = n->s;
    if (g_param[P_DEATH_AVOID] <= 0 || g_param[P_DEATH_RADIUS] <= 0
        || !vs_human(n)) {
        return false;
    }
    for (int k = 0; k < NM_GRAVES; k++) {
        if (s->grave_at[k] != 0
            && s->frames - s->grave_at[k] < (u32)g_param[P_DEATH_AVOID]
            && chebyshev(x - s->grave_x[k], y - s->grave_y[k])
               <= g_param[P_DEATH_RADIUS]) {
            return true;
        }
    }
    return false;
}

/* (Session 68, P_HOME_GRAVES) How many times the mech was destroyed
 * within P_DEATH_RADIUS of (x, y) in the last P_DEATH_AVOID frames (of the
 * last NM_GRAVES). */
static int graves_near(const Nm *n, int x, int y)
{
    const HzNightmare *s = n->s;
    int count = 0;
    if (g_param[P_DEATH_AVOID] <= 0 || g_param[P_DEATH_RADIUS] <= 0
        || !vs_human(n)) {
        return 0;
    }
    for (int k = 0; k < NM_GRAVES; k++) {
        if (s->grave_at[k] != 0
            && s->frames - s->grave_at[k] < (u32)g_param[P_DEATH_AVOID]
            && chebyshev(x - s->grave_x[k], y - s->grave_y[k])
               <= g_param[P_DEATH_RADIUS]) {
            count++;
        }
    }
    return count;
}

/* (Session 69, P_NEST_DEATHS) Is the mech destroyed P_NEST_DEATHS times
 * within P_DEF_RADIUS of its HQ in the last P_DEATH_AVOID frames (of the
 * last NM_GRAVES)? */
static bool home_deaths(const Nm *n)
{
    const HzNightmare *s = n->s;
    const HzPost *hq = post(n, own_hq(n));
    int count = 0;
    if (g_param[P_NEST_DEATHS] <= 0 || g_param[P_DEATH_AVOID] <= 0
        || !vs_human(n)) {
        return false;
    }
    for (int k = 0; k < NM_GRAVES; k++) {
        if (s->grave_at[k] != 0
            && s->frames - s->grave_at[k] < (u32)g_param[P_DEATH_AVOID]
            && chebyshev(hq->x - s->grave_x[k], hq->y - s->grave_y[k])
               <= g_param[P_DEF_RADIUS]) {
            count++;
        }
    }
    return count >= g_param[P_NEST_DEATHS];
}

/* The HQ's fight radius, px: an enemy unit within it is fought for the
 * HQ. */
static int home_radius(const Nm *n)
{
    if (g_param[P_HOME_FIGHT] > 0 && cpu_hard(n)) {
        return g_param[P_HOME_FIGHT];
    }
    return g_param[P_DEF_RADIUS];
}

/* (Session 68) Is unit or mech r within home_radius of the HQ? */
static bool threat_home(const Nm *n, HzRef r)
{
    const HzPost *hq = post(n, own_hq(n));
    int x = n->rival->x + 16, y = n->rival->y + 16;
    if (r.kind != REF_MECH) {
        const HzUnit *u = ref_unit_ptr_c(n->g, r);
        if (u == NULL) {
            return false;
        }
        x = ucx(u);
        y = ucy(u);
    }
    return chebyshev(x - hq->x, y - hq->y) <= home_radius(n);
}

/* (Session 77, P_BUY_HOME, P_NEST_WAIT) Is enemy unit u about to shoot
 * the HQ: within its gun's reach and 64 px of it, 192 px for one sent at
 * the HQ (on its way in)? */
static bool hq_soon(const Nm *n, const HzUnit *u)
{
    const HzPost *hq = post(n, own_hq(n));
    int d = chebyshev(ucx(u) - hq->x, ucy(u) - hq->y);
    return d <= gun_reach(u) + 64 + (u->order == ORDER_ATTACK_HQ ? 128 : 0);
}

/* The enemy unit (or mech) most worth fighting near the HQ (or, less,
 * near an own outpost), or none; its score in *score_out. */
static HzRef pick_threat(const Nm *n, long *score_out)
{
    const HzPost *hq = post(n, own_hq(n));
    HzRef best = ref_none();
    long best_score = 0;
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    int radius = home_radius(n);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (!unit_alive(u) || nofight(n, unit_ref(u))) {
            continue;
        }
        if (g_param[P_NO_NEST_FIGHT] && missile_unit(u)) {
            /* (session 68) the owner's game of 30 October: 100 of its
             * 208 deaths within 200 px of its HQ, flying at the GRM-34As
             * and SAM-42s he put down round it */
            continue;
        }
        long score = 0;
        int d = chebyshev(ucx(u) - hq->x, ucy(u) - hq->y);
        /* (session 68) in the owner's game of 31 October, 06:53
         * (ABGRUND, the server's AI P1) its mech went for the GRM-34As on
         * The owner's outpost 400 px from its HQ one at a time,
         * destroyed 21 times in six minutes for 5 of them: the hurt ones
         * were lifted to the outpost and a SUPPLY refilled their
         * missiles. Leaving them all alone (P_HOME_GRAVE) wouldn't wear
         * down a player who doesn't, as the owner put it; leaving those
         * repaired (P_TENDED) does */
        bool at_hq = u->order == ORDER_ATTACK_HQ
                     || d <= gun_reach(u) + 64;
        if (g_param[P_NEST_WAIT] && vs_human(n)) {
            /* (session 77) the owner's game of 19 November (EISFREI,
             * the server's AI P1): from 10:27 to 24:47 its mech was
             * destroyed 34 times about 470 px from its HQ, going for his
             * boats sent at its HQ where he put them down, under the
             * GRM-34As he stood there for them ("that nest was being
             * built with the specific intent of preventing the AI from
             * blowing up boats dropped close to the base"). One sent at
             * the HQ is the HQ's only once it is about to shoot it */
            at_hq = hq_soon(n, u);
        }
        if (!at_hq && g_param[P_TENDED] > 0 && missile_unit(u)
            && i < n->s->track_cap && n->s->track[i].tended_at != 0
            && n->s->frames - n->s->track[i].tended_at
               < (u32)g_param[P_TENDED]) {
            continue;
        }
        if ((d > radius || (!at_hq && g_param[P_HOME_GRAVE]))
            && grave_near(n, ucx(u), ucy(u))) {
            continue;       /* where it was destroyed, a while */
        }
        if (!at_hq && g_param[P_HOME_GRAVES] > 0
            && graves_near(n, ucx(u), ucy(u)) >= g_param[P_HOME_GRAVES]) {
            continue;       /* (session 68) destroyed there again and again */
        }
        if (!at_hq && home_deaths(n)
            && (missile_unit(u)
                || (g_param[P_NEST_DEATHS_ALL]
                    && (n->danger[clamp63(ucy(u) >> 5)][clamp63(ucx(u) >> 5)]
                        & DANGER_MISSILE)))) {
            /* (session 69) the owner's game of 1 November, 10:22
             * (VULKAN, the server's AI P1): from 28 minutes destroyed 45
             * times in 12, going from its HQ for one GRM-34A after
             * another round it, none three times; then (taken over) for
             * the SUPPLYs and TAX-52s among them */
            continue;
        }
        if (d <= radius) {
            score = (long)type_threat(u->type)
                    * (u->order == ORDER_ATTACK_HQ ? 4 : 1) * 100000L
                    / (d + 64);
        } else if (g_param[P_HUNT_RADIUS] > 0) {
            for (int j = 0; j < HZ_POST_OUTPOSTS; j++) {
                if (!post_ours(n, j)) {
                    continue;
                }
                int dp = chebyshev(ucx(u) - post(n, j)->x,
                                   ucy(u) - post(n, j)->y);
                if (dp <= g_param[P_HUNT_RADIUS]) {
                    long sc = (long)type_threat(u->type)
                              * (u->type == UNIT_INFANTRY ? 3 : 1) * 25000L
                              / (dp + 64);
                    if (sc > score) {
                        score = sc;
                    }
                }
            }
        }
        /* not into a missile nest for it */
        if (score > 0 && (n->danger[clamp63(ucy(u) >> 5)]
                          [clamp63(ucx(u) >> 5)] & DANGER_MISSILE)) {
            score /= 4;
        }
        if (score > best_score) {
            best_score = score;
            best = unit_ref(u);
        }
    }
    /* The enemy mech on foot: by the HQ as a tank would be, or by its own
     * units on their way in (it kills them in seconds). P_MECH_FIGHT 1
     * takes it on as the CPU only (whose shots hit harder on TYPE C and
     * D), 2 always, (session 68) 3 as the CPU and against a human. */
    const HzMech *e = n->rival;
    int mf = g_param[P_MECH_FIGHT];
    if ((mf == 2 || (mf >= 1 && n->cpu) || (mf == 3 && vs_human(n)))
        && !(e->flags & MECH_DESTROYED) && e->form == HZ_FORM_ROBOT) {
        int ex = e->x + 16, ey = e->y + 16;
        long score = 0;
        int d = chebyshev(ex - hq->x, ey - hq->y);
        if (d <= g_param[P_DEF_RADIUS]) {
            score = (long)g_param[P_MECH_HQ] * 100000L / (d + 64);
        }
        /* away from the HQ, where it was destroyed: not a while */
        bool shun = d > radius && grave_near(n, ex, ey);
        u32 on = side_slots(n->g, n->team);
        for (u32 j = 0; j < on && !shun; j++) {
            const HzUnit *o = unit_at_c(n->g, n->team, j);
            if (!unit_alive(o) || o->order != ORDER_ATTACK_HQ) {
                continue;
            }
            int du = chebyshev(ex - ucx(o), ey - ucy(o));
            if (du < 192) {
                long sc = (long)g_param[P_MECH_ESCORT] * 100000L
                          / (du + 64);
                if (sc > score) {
                    score = sc;
                }
            }
        }
        if (score > 0 && (danger_at(n, e->x, e->y) & DANGER_MISSILE)) {
            score /= 4;
        }
        if (score > best_score) {
            best_score = score;
            best = ref_mech(1 - n->team);
        }
    }
    *score_out = best_score;
    return best;
}

/* Is an outpost needed: no attack drop within P_DROP_GD blocks' drive of
 * the enemy HQ's range that a unit can be carried to from an own base on
 * a full tank, (P_FRONT_OPEN) though there is one out of its missiles?
 * (Asked every 300 frames; the answer kept.) */
static bool front_needed(Nm *n)
{
    HzNightmare *s = n->s;
    if (s->front_at != 0 && s->frames - s->front_at < 300) {
        return s->front_need;
    }
    s->front_at = s->frames;
    ground_build(n);
    const HzPost *e = post(n, enemy_hq(n));
    int full = kidou(n)->stats.fuel;
    int ex = e->x >> 5, ey = e->y >> 5;
    bool ok = false;
    for (int b = 0; b < HZ_POST_CLEARED && !ok; b++) {
        if (!is_base(n, b)) {
            continue;
        }
        int fx = post(n, b)->x - 16, fy = post(n, b)->y - 16;
        for (int by = imax(0, ey - 12); by <= imin(63, ey + 12) && !ok; by++) {
            for (int bx = imax(0, ex - 12); bx <= imin(63, ex + 12); bx++) {
                if (g_ground[n->team][by][bx] > g_param[P_DROP_GD]
                    || (n->danger[by][bx] & DANGER_MISSILE)) {
                    continue;
                }
                if (leg_ok_from(n, fx, fy, full, bx * 32, by * 32,
                                g_param[P_FUEL_MARGIN])
                    && landable(n, bx * 32, by * 32)) {
                    ok = true;
                    break;
                }
            }
        }
    }
    if (!ok && g_param[P_FRONT_OPEN]) {
        /* (session 68) VULKAN: none for the enemy's missiles, an outpost
         * won't make one. Its HQ left at 198 for 25 minutes while
         * INFANTRY went out, its own ground down to 5. */
        bool open = false;
        for (int by = imax(0, ey - 12); by <= imin(63, ey + 12) && !open;
             by++) {
            for (int bx = imax(0, ex - 12); bx <= imin(63, ex + 12); bx++) {
                if (g_ground[n->team][by][bx] <= g_param[P_DROP_GD]
                    && !(n->danger[by][bx] & DANGER_MISSILE)
                    && landable(n, bx * 32, by * 32)) {
                    open = true;
                    break;
                }
            }
        }
        ok = !open;
    }
    s->front_need = !ok;
    return s->front_need;
}

static int own_outposts(const Nm *n);
static int capture_min(const Nm *n);
static bool hq_calm(const Nm *n);
static int wall_count(const Nm *n, int *sams);
static int garrison_size(const Nm *n);

/* The outpost to take next: not ours, out of the enemy's missiles, not
 * right by the enemy HQ, within P_CAPTURE_REACH of an own base, (session
 * 68) not one it failed to take lately, and past P_CAPTURE_MIN nearer
 * the enemy HQ than its bases; of those the one that brings the front
 * nearest the enemy HQ (the enemy's own a little sooner, the trip there
 * counted half). -1 none. */
/* (Session 68) On a human's pad against the CPU on TYPE C or D, where
 * the CPU's shots hit x 1.5 and x 2 and its HQ takes less. */
static bool cpu_hard(const Nm *n)
{
    return !n->cpu && (n->rival->flags & MECH_CPU) && n->g->options[1] >= 2;
}

/* (Session 68) The enemy's armed units within 128 px of outpost i. On
 * EISFREI (TYPE C, as P2) three of the CPU's TAX-52s guarding one shot
 * the 9 INFANTRY it sent there one at a time, its money piling up; on a
 * human's pad against the CPU on TYPE D, all maps, both seats, none
 * with 2 or more: 33-47-48 against 26-56-46 (C 55-54-19 against
 * 57-54-17). */
static int post_guards(const Nm *n, int i)
{
    const HzPost *pp = post(n, i);
    int es = 1 - n->team, count = 0;
    u32 en = side_slots(n->g, es);
    for (u32 k = 0; k < en; k++) {
        const HzUnit *u = unit_at_c(n->g, es, k);
        if (unit_alive(u) && gun_reach(u) > 0 && u->ammo > 0
            && chebyshev(ucx(u) - pp->x, ucy(u) - pp->y) <= 128) {
            count++;
        }
    }
    return count;
}

/* (Session 68, P_CAPTURE_ESCORT) Should a TAX-52 go first to outpost i,
 * to take it? In the owner's game of 31 October (VULKAN, the server's
 * AI P1) he stood his mech on foot by the two outposts he had taken and
 * shot the 13 INFANTRY it sent at them as they came, one at a time. Sent
 * also by an outpost an enemy unit guarded, or after 2 INFANTRY, against
 * the turtle (hz_arena) 30 to 97 went a game, one at a time, and on
 * STADT as P2 it lost 8 of 8 (1 before). */
static bool escort_wanted(const Nm *n, int i)
{
    const HzNightmare *s = n->s;
    if (g_param[P_CAPTURE_ESCORT] != 1 || !vs_human(n) || i >= NM_OUTPOSTS
        || !(shop_types(n->g) & (1u << UNIT_TAX))) {
        return false;
    }
    if (s->robot_at[i] == 0 || s->frames - s->robot_at[i] >= 3600
        || (s->escort_at[i] != 0 && s->frames - s->escort_at[i] < 7200)) {
        return false;
    }
    const HzPost *pp = post(n, i);
    u32 on = side_slots(n->g, n->team);
    for (u32 k = 0; k < on; k++) {
        const HzUnit *u = unit_at_c(n->g, n->team, k);
        if (unit_alive(u) && u->type == UNIT_TAX
            && u->order == ORDER_GUARD_NEAR
            && chebyshev(ucx(u) - pp->x, ucy(u) - pp->y) <= 192) {
            return false;
        }
    }
    return true;
}

/* (Session 68, P_CAPTURE_ESCORT 2) The enemy mech stood on foot by
 * outpost i in the last minute. */
static bool robot_held(const Nm *n, int i)
{
    const HzNightmare *s = n->s;
    return (g_param[P_CAPTURE_ESCORT] == 2 || g_param[P_CAPTURE_ROBOT])
           && vs_human(n) && i < NM_OUTPOSTS
           && s->robot_at[i] != 0 && s->frames - s->robot_at[i] < 3600;
}

/* (Session 74) Has the enemy a unit out with the attack-HQ order (carried
 * or down)? */
static bool enemy_attacking(const Nm *n)
{
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 k = 0; k < en; k++) {
        const HzUnit *u = unit_at_c(n->g, es, k);
        if ((u->flags & UNIT_ACTIVE) && !(u->flags & UNIT_DYING)
            && u->order == ORDER_ATTACK_HQ) {
            return true;
        }
    }
    return false;
}

/* The outpost to take next (the comment above cpu_hard).
 * (Session 74, P_CAPTURE_FREE) free_only: of the outposts that are
 * nobody's alone, wherever they are, the nearest its bases (choose_buy
 * asks when it has fewer than CAPTURE_MIN and none in reach). The owner
 * (14 November): "There is no such thing as 'Past its reach'. Where did
 * this concept come from?" From the first tuning (760 px: there and back
 * on a tank). In his game of 14 November outposts 8, 3 and 0, in its own
 * half of ABGRUND, 992, 896 and 1,376 px from its bases, stood empty till
 * he took them at 11:47, 13:02 and 12:16: 2 outposts to his 7. The
 * INFANTRY goes down as far as the fuel takes it (task_deliver) and walks
 * on. For the enemy's outposts the reach is kept: taken over with the
 * turtle for him, CAPTURE_HUMAN 1,400 sent 74 INFANTRY a game at outposts
 * he held and lost 12 of 32 for 1.
 * (P_CAPTURE_HOT) An outpost in the enemy's missiles' reach is one to
 * take while fewer than that many launchers reach it. */
static int pick_capture_of(const Nm *n, bool free_only)
{
    int best = -1, best_score = 0x7FFFFFFF;
    const HzPost *ehq = post(n, enemy_hq(n));
    int front = 0x7FFFFFFF;
    /* (session 68, P_CAPTURE_HUMAN) in the owner's games of 31 October
     * (ABGRUND, the server's AI P1) three outposts 896 to 1,300 px from
     * its bases stood empty 12 minutes, out of its 760; the owner
     * took one 928 px from the nearest of theirs. Tried, off: always,
     * against the turtle on maps 0-7 it lost 42 of 128 for 24; only
     * while the enemy holds more (P_CAPTURE_BEHIND), 41 */
    int reach = g_param[P_CAPTURE_REACH];
    if (g_param[P_CAPTURE_HUMAN] > 0 && vs_human(n)) {
        int theirs = 0;
        for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
            theirs += post_theirs(n, i);
        }
        if (!g_param[P_CAPTURE_BEHIND] || theirs > own_outposts(n)) {
            reach = g_param[P_CAPTURE_HUMAN];
        }
    }
    /* (session 74, P_CAPTURE_FREE) one out of CAPTURE_REACH only while
     * the enemy has no unit out with the attack-HQ order: the trip takes
     * the mech 20 s and more from its HQ. In the first build, from the
     * start against the raider on ABGRUND, his first TAX-52 shot its HQ
     * while it carried INFANTRY across the map, and the garrison of 40
     * came of that one hit (GARRISON_SHOT). */
    bool far_off = free_only && enemy_attacking(n);
    /* (session 75, P_CAPTURE_NOBODY) the owner's games of 14 November
     * (EISFREI, it P2) and 15 November (LOCH, it P1): its first twelve
     * INFANTRY ($2,400, all it had) went at an outpost of his, 608 and
     * 512 px off, his mech on foot beside it, each one he lost put back
     * by one of his; nobody's stood by (on EISFREI one 388 px from its
     * outpost 6, taken by him at 1:16), and on LOCH nobody's three, 896
     * px and more from its bases, were his by 12:09. */
    bool nobody_first = !free_only && g_param[P_CAPTURE_NOBODY]
                        && vs_human(n);
    bool nobody_far = nobody_first && !enemy_attacking(n);
    if (g_param[P_CAPTURE_AHEAD] && g_param[P_STYLE] != 1
        && own_outposts(n) >= capture_min(n)) {
        /* (session 68) VULKAN: the outpost it wanted not taken, it took
         * two further from the enemy HQ than a base it had, and the
         * attack waited on them */
        for (int b = 0; b < HZ_POST_CLEARED; b++) {
            if (is_base(n, b)) {
                const HzPost *bp = post(n, b);
                front = imin(front, chebyshev(bp->x - ehq->x, bp->y - ehq->y));
            }
        }
    }
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        const HzPost *pp = post(n, i);
        if (!(pp->flags & POST_USED) || post_ours(n, i)) {
            continue;
        }
        bool free = g_param[P_CAPTURE_FREE] && vs_human(n)
                    && !(pp->flags & POST_OWNED);
        if (free_only && !free) {
            continue;
        }
        if ((n->danger[clamp63(pp->y >> 5)][clamp63(pp->x >> 5)]
             & DANGER_MISSILE)
            && !(g_param[P_CAPTURE_HOT] > 0 && vs_human(n)
                 && post_missiles(n, i) < g_param[P_CAPTURE_HOT])) {
            continue;
        }
        if (robot_held(n, i)) {
            continue;
        }
        if (g_param[P_CAPTURE_ALL] != 1 && n->s->cap_rest[i] > n->s->frames
            && (g_param[P_CAPTURE_ALL] == 0
                || own_outposts(n) < capture_min(n)
                || (g_param[P_CAPTURE_ALL] == 3 && n->s->cap_fails[i] >= 2))) {
            continue;   /* (session 68) it failed there: left a while */
        }
        int e = chebyshev(pp->x - ehq->x, pp->y - ehq->y);
        if ((e < g_param[P_CAPTURE_NEAR] || e >= front) && !g_param[P_RAID]) {
            continue;
        }
        int b = nearest_base(n, pp->x, pp->y);
        int d = base_dist(n, b, pp->x, pp->y);
        bool nobodys = !(pp->flags & POST_OWNED);
        if (d > reach && (!free_only || far_off)
            && !(nobody_far && nobodys)) {
            continue;
        }
        bool even = g_param[P_CAPTURE_EVEN] > 0 && vs_human(n)
                    && own_outposts(n) >= g_param[P_CAPTURE_MIN];
        if (g_param[P_CAPTURE_GUARDS] > 0
            && (cpu_hard(n) || (g_param[P_GUARDS_HUMAN] && vs_human(n))
                || even)
            && post_guards(n, i) >= g_param[P_CAPTURE_GUARDS]) {
            /* (session 68, P_CAPTURE_EVEN) past CAPTURE_MIN, none into
             * a guarded one */
            continue;
        }
        int score = e + d / 2 - (post_theirs(n, i) ? 100 : 0);
        if (g_param[P_STYLE] == 1) {
            /* (session 67) a player's way: the nearest its HQ first */
            const HzPost *hq = post(n, own_hq(n));
            score = chebyshev(pp->x - hq->x, pp->y - hq->y);
        }
        if (g_param[P_RAID] > 0) {
            /* (session 69) a player's way: the nearest its bases, the
             * enemy's first */
            score = d - (post_theirs(n, i) ? 256 : 0);
        }
        if (free_only) {
            score = d;
        } else if (nobody_first && !nobodys) {
            score += 0x10000;   /* (session 75) nobody's first */
        }
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static int pick_capture(const Nm *n)
{
    return pick_capture_of(n, false);
}

static int own_outposts(const Nm *n)
{
    int c = 0;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        if (post_ours(n, i)) {
            c++;
        }
    }
    return c;
}

/* (Session 68, P_CAPTURE_EVEN) The outposts it takes at least:
 * P_CAPTURE_MIN, or against a human as many as the enemy holds, less
 * P_CAPTURE_EVEN - 1. In the owner's game of 31 October (VULKAN, the
 * server's AI P1) it held 3 from minute 24, he 6 and 7 with SAM-42s by
 * them, and with CAPTURE_MIN 3 it took none back: his money piled up to
 * $13,000, its own went on attack units. Not while its HQ was shot in the
 * last minute or its garrison is short: taken over at minute 24 with the
 * turtle for the owner, INFANTRY went out while his TAX-52s took its HQ
 * (11 of 16 lost, none before). At 1 (as many) on STADT as P2 it went for
 * the turtle's 4 to its 3 instead of the attack that wins there in 15
 * minutes: 3-5 from 7-1. */
static int capture_min(const Nm *n)
{
    int want = imax(g_param[P_CAPTURE_MIN], g_param[P_RAID]);
    int sams;
    if (g_param[P_CAPTURE_EVEN] > 0 && vs_human(n) && hq_calm(n)
        && wall_count(n, &sams) >= garrison_size(n)) {
        int theirs = 0;
        for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
            theirs += post_theirs(n, i);
        }
        want = imax(want, theirs - g_param[P_CAPTURE_EVEN] + 1);
    }
    return want;
}

/* (Session 68, P_HOME_POST) The enemy's outpost nearest its HQ within
 * P_HOME_POST px of it, out of the enemy's missiles and not one it failed
 * to take lately: -1 none. In the owner's game of 31 October, 18:31
 * (VULKAN, the server's AI P1) he took its outpost 448 px north of its
 * HQ at 26:39 and held it to the end, 43 minutes: his drops at its HQ
 * came from there, his SAM-42s and GRM-34As stood by it, and it never
 * sent INFANTRY back (3 outposts, CAPTURE_MIN; its HQ shot). Taken over
 * at 33 minutes with the turtle for the owner, the turtle's mech
 * refuelled there between its drops, nothing of its by it, and it lost
 * 13 of 16; with this, 16 draws. Only once its attacks have hurt the
 * enemy HQ and then stopped (P_HOME_POST_HIT): on map 5 as P1 the turtle
 * took its outpost 576 px from its HQ in the first minutes, and it sent
 * INFANTRY there (from the start, 3-1-4; from 5 minutes on, 4-2-2)
 * instead of building the attack that won it 7 of 8. And only while its
 * own HQ is being shot (P_HOME_POST_SHOT): against the turtle with
 * nest_units 6 on map 6 as P1, 8 outposts to 1 and its HQ untouched, it
 * went for the one by its HQ the turtle held 8 to 15 times a game, and
 * lost 2 of 8 (none without). */
static int home_post(const Nm *n)
{
    if (g_param[P_HOME_POST] <= 0 || !vs_human(n)
        || post_hq_hp(post(n, enemy_hq(n))) >= 1024    /* never shot */
        || n->s->frames - n->s->ehq_hit_at < (u32)g_param[P_HOME_POST_HIT]) {
        return -1;
    }
    if (g_param[P_HOME_POST_SHOT] > 0
        && (n->s->hq_hit_at == 0
            || n->s->frames - n->s->hq_hit_at
               >= (u32)g_param[P_HOME_POST_SHOT])) {
        return -1;
    }
    const HzPost *hq = post(n, own_hq(n));
    int best = -1, best_d = g_param[P_HOME_POST] + 1;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        const HzPost *pp = post(n, i);
        if (!(pp->flags & POST_USED) || !post_theirs(n, i)
            || ((n->danger[clamp63(pp->y >> 5)][clamp63(pp->x >> 5)]
                 & DANGER_MISSILE)
                && !(g_param[P_CAPTURE_HOT] > 0     /* (session 74) */
                     && post_missiles(n, i) < g_param[P_CAPTURE_HOT]))
            || n->s->cap_rest[i] > n->s->frames
            || (g_param[P_HOME_POST_GUARDS]
                && post_guards(n, i) >= g_param[P_CAPTURE_GUARDS])) {
            continue;
        }
        int d = chebyshev(pp->x - hq->x, pp->y - hq->y);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* -- the ground to the enemy HQ ----------------------------------------- */

/* For each block, how many blocks a ground unit drives from it to within
 * attack range of team t's enemy HQ (the blocks within 4 of it), going
 * round what blocks it (a block is passable unless a mech finds it
 * blocked or water); NM_FAR where it can't. Made once a game (the map and
 * the HQs don't move): keyed by the map and the HQ's place. */

/* The way an attack-HQ unit drives (unit_ai.c's attack_hq_drive): on a
 * block with a path hint for its side, along the hint; elsewhere
 * straight at the HQ; blocked, it turns aside and drives on. On the
 * blocks: from each, a step a block that way (8 ways), or 45 degrees
 * either side when that block won't pass; until it is within 4 blocks of
 * the HQ (in range), or it comes back to a block it was on, or can't go
 * on. The steps it took, NM_FAR if it never got there. */
static int route_from(const HzGame *g, int team, const u8 (*pass)[64],
                      int ex, int ey, int bx, int by)
{
    static u16 stamp = 0;
    static u16 seen_at[64][64];     /* == stamp: on this route already */
    stamp++;
    if (stamp == 0) {
        memset(seen_at, 0, sizeof seen_at);
        stamp = 1;
    }
    static const int DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int DY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    int side = 0;
    for (int steps = 0; steps < 120; steps++) {
        if (iabs(bx - ex) <= 4 && iabs(by - ey) <= 4) {
            return steps;
        }
        if (seen_at[by][bx] == stamp) {
            return NM_FAR;
        }
        seen_at[by][bx] = stamp;
        u16 w = g->layout[by][bx];
        int d8;
        if (w & LAYOUT_PATH_HINT) {
            d8 = team ? (w >> 10) & 7 : (w >> 13) & 7;
        } else {
            d8 = bearing16(bx * 32 + 16, by * 32 + 16, ex * 32 + 16,
                           ey * 32 + 16);
            d8 = ((d8 + 1) >> 1) & 7;
        }
        bool moved = false;
        for (int k = 0; k < 3 && !moved; k++) {
            int dd = (k == 0) ? d8 : ((k == 1) ? d8 + (side ? 1 : -1)
                                               : d8 + (side ? -1 : 1));
            dd &= 7;
            int nx = bx + DX[dd], ny = by + DY[dd];
            if (nx < 0 || ny < 0 || nx > 63 || ny > 63 || !pass[ny][nx]) {
                continue;
            }
            if ((dd & 1) && (!pass[by][nx] || !pass[ny][bx])) {
                continue;               /* no corner cutting */
            }
            if (k == 2) {
                side ^= 1;
            }
            bx = nx;
            by = ny;
            moved = true;
        }
        if (!moved) {
            return NM_FAR;
        }
    }
    return NM_FAR;
}

/* (Session 68) g_water: from each block of water whose centre is within
 * NM_BOAT_REACH of the enemy HQ, out over the water a block at a time (8
 * ways, no corner cut). */
#define NM_BOAT_REACH 200

static void water_build(Nm *n)
{
    const HzPost *e = post(n, enemy_hq(n));
    u16 (*w)[64] = g_water[n->team];
    static u8 wet[64][64];
    static u8 queue[64 * 64][2];
    int head = 0, tail = 0;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            wet[y][x] = mech_ground_at(n->g, n->m, (s16)(x * 32),
                                       (s16)(y * 32)) == NM_GROUND_WATER;
            w[y][x] = NM_FAR;
            if (wet[y][x] && chebyshev(x * 32 + 16 - e->x,
                                       y * 32 + 16 - e->y) <= NM_BOAT_REACH) {
                w[y][x] = 0;
                queue[tail][0] = (u8)x;
                queue[tail][1] = (u8)y;
                tail++;
            }
        }
    }
    static const int DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int DY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    while (head < tail) {
        int x = queue[head][0], y = queue[head][1];
        head++;
        for (int k = 0; k < 8; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx > 63 || ny > 63 || !wet[ny][nx]
                || w[ny][nx] != NM_FAR
                || ((k & 1) && (!wet[y][nx] || !wet[ny][x]))) {
                continue;
            }
            w[ny][nx] = (u16)(w[y][x] + 1);
            queue[tail][0] = (u8)nx;
            queue[tail][1] = (u8)ny;
            tail++;
        }
    }
}

static void ground_build(Nm *n)
{
    const HzPost *e = post(n, enemy_hq(n));
    s16 *key = g_ground_key[n->team];
    if (key[0] == (s16)n->g->map && key[1] == e->x && key[2] == e->y) {
        return;
    }
    key[0] = (s16)n->g->map;
    key[1] = e->x;
    key[2] = e->y;
    u16 (*f)[64] = g_ground[n->team];
    u8 (*pass)[64] = g_pass[n->team];
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int gr = mech_ground_at(n->g, n->m, (s16)(x * 32 - 4),
                                    (s16)(y * 32 - 4));
            /* not blocked, water, nor maps 0-1's special ground (currents
             * that carry a unit off, VULKAN's that burns it) */
            pass[y][x] = gr != 1 && gr != 2 && gr != 4;
        }
    }
    water_build(n);
    int ex = e->x >> 5, ey = e->y >> 5;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            f[y][x] = NM_FAR;
            if (pass[y][x] && iabs(x - ex) <= 28 && iabs(y - ey) <= 28) {
                f[y][x] = (u16)route_from(n->g, n->team,
                                          (const u8 (*)[64])pass, ex, ey,
                                          x, y);
            }
        }
    }
}

/* (Session 68) The drive test. Below a player's HQ on ABGRUND the ground
 * slopes, and a slope pushes a unit back down (TerrainEv_Current): an
 * attack unit put down under it drove at it until its fuel ran out,
 * moving a pixel back and forth, and route_from, a point a block, never
 * saw the slope. So before a drop is chosen a unit of the attack type is
 * put down there in a copy of the game with nothing else in it -- no
 * other unit, no shot, the mechs destroyed in the far corner and out of
 * the grid, every outpost nobody's, the HQ whole, the random numbers from
 * the place -- and only the units run (unit_ai_update, combat_update): it
 * passes when its shots take HP off the enemy HQ within P_DRIVE frames,
 * or it is still driving on then; it fails when it runs dry or dies.
 * The answer is the map's, the HQ's, the side's, the unit's and the
 * patches' alone, so a place's is kept (across games, as g_ground's).
 *
 * The nest test (P_NEST): the same with the enemy's SAM-42s and GRM-34As
 * left where they stand: it passes when its shots take HP off one (or
 * the HQ). On ABGRUND a player's stood all round the places from which a
 * unit reached the HQ; their missiles hit only the mech, so a unit put
 * down out of their reach goes for them. Its answers are kept while
 * those units stand where they did. */
typedef struct {
    u8   place[2][256][256];    /* a side's, an 8 px place each */
    long key[2][9];
    u32  think[2];              /* the think (its frames) ... */
    int  fresh[2];              /* ... and the places newly tried in it */
} NmDrives;
/* [1]: a rival's in nightmare_think_ahead's copy, whose thinks are
 * thrown away (its tries kept apart: its own, in the game, count) */
static NmDrives g_drives[2];
static NmDrives g_nests[2];         /* the nest test's (its fresh unused) */
static HzGame *g_drive_game;        /* the copy, with unit lists its own */
static bool thrown_away(const Nm *n);
/* The places the last drive went through (as a mech's place: y << 8 | x,
 * 8 px each): a drive from one is that drive's tail, so its answer is
 * kept for each too. */
#define NM_DRIVE_PATH 512
static u16 g_drive_path[NM_DRIVE_PATH];
static int g_drive_path_len;

/* A unit the nest test keeps: an enemy SAM-42 or GRM-34A. */
static bool nest_unit(const HzUnit *u)
{
    return unit_alive(u) && (u->type == UNIT_SAM || u->type == UNIT_GRM);
}

/* (Session 68) P_DRIVE_GAIN on a human's pad; as the CPU's brain none.
 * On EISFREI (TYPE B, as P2) the CPU's brain lost with it, 391 drops of
 * 399 failed and 3 attack units put down in 42 minutes, and won without
 * it in 12 (all maps, both seats: 127-1 with it, 128-0 without). On a
 * human's pad it helps: 108-10-10 with it, 95-14-19 without (EISFREI as
 * P2 5-2 against 0-6). */
static int drive_gain(const Nm *n)
{
    return n->cpu ? 0 : g_param[P_DRIVE_GAIN];
}

/* The side's tries (nest: the nest test's), their key checked: another
 * map, HQ, unit or patch (nest: or the enemy's SAMs and GRMs elsewhere)
 * empties them. */
static NmDrives *drives_of(Nm *n, bool nest)
{
    NmDrives *dr = &(nest ? g_nests : g_drives)[thrown_away(n) ? 1 : 0];
    const HzPost *e = post(n, enemy_hq(n));
    const HzUnitStats *st = unit_stats_for(n->g, (u8)g_param[P_RUSH_TYPE]);
    long key[9] = {
        n->g->map, e->x, e->y, g_param[P_RUSH_TYPE],
        (long)(n->g->fixes & ~HZ_FIX_UNITS), n->g->options[1],
        g_param[P_DRIVE] ^ ((long)drive_gain(n) << 16),
        ((long)st->speed << 16) | st->fuel, 0
    };
    if (nest) {
        u32 h = 2166136261u;
        int es = 1 - n->team;
        u32 en = side_slots(n->g, es);
        for (u32 i = 0; i < en; i++) {
            const HzUnit *u = unit_at_c(n->g, es, i);
            if (nest_unit(u)) {
                h = (h ^ (u32)((ucy(u) >> 5) << 12 | (ucx(u) >> 5) << 4
                               | u->type)) * 16777619u;
            }
        }
        key[8] = (long)h;
    }
    if (memcmp(key, dr->key[n->team], sizeof key) != 0) {
        memcpy(dr->key[n->team], key, sizeof key);
        memset(dr->place[n->team], 0, sizeof dr->place[n->team]);
    }
    return dr;
}

/* The enemy's SAMs' and GRMs' HP in the copy (nest_unit's). */
static int nest_hp(const HzGame *c, int es)
{
    int hp = 0;
    u32 en = side_slots(c, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(c, es, i);
        if (nest_unit(u)) {
            hp += u->hp;
        }
    }
    return hp;
}

/* The drive's copy (g_drive_game): n->g with the units (nest: but the
 * enemy's SAMs and GRMs) and shots gone, the mechs destroyed, and one
 * unit of the attack type put down at the block whose mech place is (gx,
 * gy), the random numbers the place's (salt 0: drive_run's; another, a
 * drive of its own from there). 0 done, 1 no memory for the copy, 2 no
 * room for the unit. */
static int drive_setup(Nm *n, int gx, int gy, bool nest, u32 salt)
{
    if (g_drive_game == NULL) {
        g_drive_game = calloc(1, sizeof *g_drive_game);
        if (g_drive_game == NULL) {
            return 1;
        }
    }
    HzGame *c = g_drive_game;
    game_copy(c, n->g);
    c->vdp = NULL;                  /* units never draw */
    c->frame = NULL;
    c->unit_ai_probe = NULL;
    c->combat_probe = NULL;
    c->units_frozen = false;
    int es = 1 - n->team;
    for (int side = 0; side < 2; side++) {
        u32 kept = 0;
        for (u32 slot = 0; slot < side_slots(c, side); slot++) {
            HzUnit *k = unit_at(c, side, slot);
            if (nest && side == es && nest_unit(k)) {
                kept++;
                continue;
            }
            k->flags = 0;
        }
        c->unit_count[side] = kept;
        memset(c->unit_type_count[side], 0,
               sizeof c->unit_type_count[side]);
    }
    c->fixes &= ~HZ_FIX_UNITS;      /* v_grid's way, and the grid empty */
    grid_sync(c);
    for (int i = 0; i < HZ_GRID * HZ_GRID; i++) {
        c->grid[i] = ref_none();
    }
    memset(c->shots, 0, sizeof c->shots);
    int hq_i = enemy_hq(n);
    HzPost *hq = &c->posts[hq_i];
    s16 far_x = (s16)(hq->x < 0x400 ? 0x780 : 0x40);
    s16 far_y = (s16)(hq->y < 0x400 ? 0x780 : 0x40);
    for (int p = 0; p < 2; p++) {
        HzMech *m = &c->mech[p];
        m->flags = (u8)((m->flags | MECH_DESTROYED) & ~MECH_CARRYING);
        m->x = far_x;
        m->y = far_y;
    }
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        c->posts[i].flags &= (u8)~(POST_OWNED | POST_TEAM2);
    }
    post_set_hq_hp(hq, 1024);
    c->rng_seed = (0x9E3779B9u * (u32)((gy >> 3) * 256 + (gx >> 3) + 1))
                  ^ (u32)(n->team << 7) ^ 0x2545F491u ^ (salt * 0x85EBCA6Bu);
    c->unit_ai_group = 0;
    c->unit_team_toggle = 0;
    u8 type = (u8)g_param[P_RUSH_TYPE];
    int half = (u8)(0x20 - unit_stats_for(c, type)->size) >> 1;
    u8 dir = (u8)bearing16(gx + 16, gy + 16, hq->x, hq->y);
    if (unit_spawn(c, (u8)(UNIT_ACTIVE | (n->team ? UNIT_TEAM2 : 0)), type,
                   ORDER_ATTACK_HQ, dir, (s16)(gx + half), (s16)(gy + half))
        != 0) {
        return 2;
    }
    return 0;
}

/* A drive that moved less than a block in this many frames is stuck
 * (pushed back down a slope, against a cliff): it burns no fuel there, as
 * a unit burns it only on a new tile (atlas_run; (session 70) drive_run
 * followed on). */
#define NM_ATLAS_STILL 1800

/* (Session 70) Still on its way when the drive's time is up (fuel left, 64
 * px since (was_x, was_y), (session 68) P_DRIVE_GAIN nearer the HQ than
 * `from`). */
static bool drive_going(const Nm *n, const HzUnit *u, const HzPost *hq,
                        s16 was_x, s16 was_y, int from)
{
    return u->fuel > 0 && chebyshev(u->x - was_x, u->y - was_y) >= 64
           && (drive_gain(n) <= 0
               || chebyshev(ucx(u) - hq->x, ucy(u) - hq->y)
                  <= from - drive_gain(n));
}

/* The drive from the block whose mech place is (gx, gy): true when the
 * unit's shots hurt the HQ (nest: or one of the enemy's SAMs and GRMs,
 * kept), or (not nest) it is still on its way when the time is up (true
 * too with no memory for the copy). Run dry while driving on (64 px in
 * the 240-480 frames before), its answer isn't its path's: a unit put
 * down further on has fuel left.
 * (Session 70, P_DRIVE_FUEL) Against a human, one still on its way is
 * followed on: it passes if it hits the HQ, or is still going with fuel
 * left when P_DRIVE_FUEL frames more are up; stuck (NM_ATLAS_STILL) it
 * fails, its fuel kept. In the owner's game of 8
 * November (ABGRUND, the server's AI P1) his SAM-42s kept its jet from
 * every drop by his HQ; its TAX-52s were put down by its own outpost 6,
 * 1,380 px from his HQ, and drove on past P_DRIVE's 50 s round the
 * chasm, and 11 stood dry in mid-map from minute 18 to the end. */
static bool drive_run(Nm *n, int gx, int gy, bool nest)
{
    g_drive_path_len = 0;
    int set = drive_setup(n, gx, gy, nest, 0);
    if (set != 0) {
        return set == 1;
    }
    HzGame *c = g_drive_game;
    int es = 1 - n->team;
    HzPost *hq = &c->posts[enemy_hq(n)];
    int half = (u8)(0x20 - unit_stats_for(c, (u8)g_param[P_RUSH_TYPE])->size)
               >> 1;
    const HzUnit *u = unit_at(c, n->team, 0);
    u16 whole = post_hq_hp(hq);
    int guards = nest ? nest_hp(c, es) : 0;
    int dry = -1;
    bool driving = false;           /* when it ran dry */
    s16 was_x = u->x, was_y = u->y;
    s16 mark_x = u->x, mark_y = u->y, old_x = u->x, old_y = u->y;
    int from = chebyshev(ucx(u) - hq->x, ucy(u) - hq->y);
    /* a nest test's half as long: its unit put down P_NEST px off one */
    int frames = nest ? g_param[P_DRIVE] / 2 : g_param[P_DRIVE];
    int more = !nest && vs_human(n) ? g_param[P_DRIVE_FUEL] : 0;
    bool followed = false, stuck = false;
    for (int f = 0; f < frames + more; f++) {
        if (f == frames - 600) {
            was_x = u->x;
            was_y = u->y;
        }
        if (f == frames) {
            if (!drive_going(n, u, hq, was_x, was_y, from)) {
                break;
            }
            followed = true;
            was_x = u->x;
            was_y = u->y;
        } else if (followed && (f - frames) % NM_ATLAS_STILL == 0) {
            if (chebyshev(u->x - was_x, u->y - was_y) < 32) {
                stuck = true;
                break;
            }
            was_x = u->x;
            was_y = u->y;
        }
        if (f % 240 == 0) {
            old_x = mark_x;
            old_y = mark_y;
            mark_x = u->x;
            mark_y = u->y;
        }
        unit_ai_update(c);
        combat_update(c);
        u16 at = (u16)((((u->y - half) >> 3) & 0xFF) << 8
                       | (((u->x - half) >> 3) & 0xFF));
        if ((g_drive_path_len == 0
             || g_drive_path[g_drive_path_len - 1] != at)
            && g_drive_path_len < NM_DRIVE_PATH) {
            g_drive_path[g_drive_path_len++] = at;
        }
        if (post_hq_hp(hq) < whole) {
            return true;
        }
        if (nest && (f & 7) == 7 && nest_hp(c, es) < guards) {
            return true;
        }
        if (!(u->flags & UNIT_ACTIVE) || (u->flags & UNIT_DYING)) {
            return false;
        }
        if (u->fuel == 0) {
            if (dry < 0) {
                dry = f;
                driving = chebyshev(u->x - old_x, u->y - old_y) >= 64;
            } else if (f - dry > 240) {
                break;          /* out of fuel and out of range */
            }
        }
    }
    /* the time up: still on its way, as one put down far out is (the nest
     * test's fails, its path untold too). On EISFREI (TYPE D) units put
     * down in a lake's pocket drove round it till they ran dry, nine at
     * once, and passed. (Session 70) Followed on: fuel left, not stuck */
    bool on = followed ? u->fuel > 0 && !stuck
                       : drive_going(n, u, hq, was_x, was_y, from);
    if ((u->fuel == 0 && driving) || (nest && on)) {
        g_drive_path_len = 0;
    }
    return !nest && on;
}

/* (Session 68) The atlas's drive (nm_atlas.h): drive_setup's, `salt` its
 * random numbers, run `frames` at most. The frames to the first hit on
 * the HQ; -1 none: it died, it ran dry and didn't hit it in the
 * NM_DRY_REACH frames after, or it moved less than a block in
 * NM_ATLAS_STILL frames (pushed back down a slope, against a cliff). */
#define NM_DRY_REACH   240

static int atlas_run(Nm *n, int gx, int gy, u32 salt, int frames)
{
    if (drive_setup(n, gx, gy, false, salt) != 0) {
        return -1;
    }
    HzGame *c = g_drive_game;
    HzPost *hq = &c->posts[enemy_hq(n)];
    const HzUnit *u = unit_at(c, n->team, 0);
    u16 whole = post_hq_hp(hq);
    s16 mark_x = u->x, mark_y = u->y;
    int dry = -1;
    for (int f = 0; f < frames; f++) {
        unit_ai_update(c);
        combat_update(c);
        if (post_hq_hp(hq) < whole) {
            return f + 1;
        }
        if (!(u->flags & UNIT_ACTIVE) || (u->flags & UNIT_DYING)) {
            return -1;
        }
        if (u->fuel == 0) {
            if (dry < 0) {
                dry = f;
            } else if (f - dry > NM_DRY_REACH) {
                return -1;
            }
        }
        if (f % NM_ATLAS_STILL == NM_ATLAS_STILL - 1) {
            if (chebyshev(u->x - mark_x, u->y - mark_y) < 32) {
                return -1;
            }
            mark_x = u->x;
            mark_y = u->y;
        }
    }
    return -1;
}

int nightmare_atlas_block(HzGame *g, int p, int bx, int by, int salts,
                          int frames)
{
    Nm n;
    memset(&n, 0, sizeof n);
    n.g = g;
    n.m = &g->mech[p];
    n.rival = &g->mech[1 - p];
    n.s = &g->nightmare[p];
    n.p = p;
    n.team = (n.m->flags & MECH_TEAM2) ? 1 : 0;
    n.cpu = (n.m->flags & MECH_CPU) != 0;
    n.danger = g_danger[p];
    n.boat_scan = -1;
    n.missiles = g_missiles[p];
    params_init(p);
    g_param = g_params[p];
    int gx = bx * 32, gy = by * 32;
    if (mech_ground_at(g, n.m, (s16)gx, (s16)gy) != 0) {
        return -2;
    }
    int slowest = 0;
    for (int i = 0; i < salts; i++) {
        int t = atlas_run(&n, gx, gy, (u32)i, frames);
        if (t < 0) {
            return -1;
        }
        slowest = imax(slowest, t);
    }
    return slowest;
}

/* (Session 68) The atlas's side for the game (nm_atlas.h): its map, the
 * side, the attack unit and the patches; NULL none (or P_ATLAS off). */
static const NmAtlas *atlas_of(const Nm *n)
{
    if (!g_param[P_ATLAS]) {
        return NULL;
    }
    u16 fixes = (u16)(n->g->fixes & NM_ATLAS_FIXES);
    for (int i = 0; i < nm_atlas_count; i++) {
        const NmAtlas *a = &nm_atlas[i];
        if (a->map == n->g->map && a->team == n->team
            && a->type == g_param[P_RUSH_TYPE] && a->fixes == fixes) {
            return a;
        }
    }
    return NULL;
}

static int hex_digit(char c)
{
    return c <= '9' ? c - '0' : c - 'A' + 10;
}

/* Block (bx, by)'s byte in the atlas a (NULL none): NM_ATLAS_UNKNOWN,
 * NM_ATLAS_BAD or its drive's time. */
static int atlas_at(const NmAtlas *a, int bx, int by)
{
    if (a == NULL) {
        return NM_ATLAS_UNKNOWN;
    }
    int x = bx - a->x0, y = by - a->y0;
    if (x < 0 || y < 0 || x >= a->w || y >= a->h) {
        return NM_ATLAS_UNKNOWN;
    }
    const char *c = a->rows[y] + 2 * x;
    return hex_digit(c[0]) << 4 | hex_digit(c[1]);
}

/* (Session 70, P_REGION) The ground joined to the enemy HQ for a unit of
 * one footprint: the places (a unit's (x + 4) >> 3, (y + 4) >> 3, as
 * obj_check_terrain's first tile) where its tiles have no b15 and the
 * map's edge doesn't stop it, flooded 8-way from those whose middle is
 * within NM_REGION_R px of the HQ's; slopes and currents taken as
 * passable, so it is never smaller than the ground a unit drives. Made
 * once a map, side and footprint. ABGRUND's halves are joined by none:
 * in the owner's game of 8 November (the server's AI P1) taken over at
 * 12 minutes, with no drop's drive passing, the TAX-52s put down so
 * (blind) went down against the wall on its own side and died there. */
#define NM_REGION_R 160
typedef struct {
    u32 key;                    /* the map, side and footprint (0 none) */
    u8 bits[256][32];
} NmRegion;
static NmRegion g_regions[4];

/* Can a unit of `n` x `n` tiles and `last` (obj_check_terrain's) stand
 * at place (tx, ty)? */
static bool region_stand(const HzGame *g, int tx, int ty, int n, int last)
{
    if (tx < 1 || ty < 1 || tx - 1 >= last || ty - 1 >= last) {
        return false;
    }
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int x = tx + c, y = ty + r;
            u16 lw = g->layout[(y & 0xFC) >> 2][(x & 0xFC) >> 2];
            if (g->blocks[lw & 0x1FF][(y & 3) * 4 + (x & 3)]
                & TILE_BLOCKED) {
                return false;
            }
        }
    }
    return true;
}

static const NmRegion *region_of(const Nm *n, int type)
{
    const HzGame *g = n->g;
    int size = unit_stats_for(g, (u8)type)->size;
    int small = type >= UNIT_INFANTRY;
    u32 key = (u32)(g->map + 1) | (u32)n->team << 16 | (u32)small << 17
              | (u32)size << 18;
    NmRegion *rg = &g_regions[n->team * 2 + small];
    if (rg->key == key) {
        return rg;
    }
    static u16 queue[256 * 256];
    int head = 0, tail = 0;
    int tiles = small ? 2 : 3, last = 0xFF - (size >> 3);
    const HzPost *hq = post(n, enemy_hq(n));
    memset(rg->bits, 0, sizeof rg->bits);
    rg->key = key;
    for (int ty = 1; ty <= last; ty++) {
        for (int tx = 1; tx <= last; tx++) {
            int dx = tx * 8 - 4 + size / 2 - hq->x;
            int dy = ty * 8 - 4 + size / 2 - hq->y;
            if (dx * dx + dy * dy <= NM_REGION_R * NM_REGION_R
                && region_stand(g, tx, ty, tiles, last)) {
                rg->bits[ty][tx >> 3] |= (u8)(1 << (tx & 7));
                queue[tail++] = (u16)(ty << 8 | tx);
            }
        }
    }
    while (head < tail) {
        int tx = queue[head] & 0xFF, ty = queue[head] >> 8;
        head++;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int x = tx + dx, y = ty + dy;
                if (x < 0 || y < 0 || x > 255 || y > 255
                    || (rg->bits[y][x >> 3] & (1 << (x & 7)))
                    || !region_stand(g, x, y, tiles, last)) {
                    continue;
                }
                rg->bits[y][x >> 3] |= (u8)(1 << (x & 7));
                queue[tail++] = (u16)(y << 8 | x);
            }
        }
    }
    return rg;
}

/* (Session 70, P_REGION) Could a unit of `type` put down with the mech at
 * (gx, gy) get to the enemy HQ by ground at all? It or a place next to it
 * on the region (a unit put down on a tile it can't stand on may still
 * drive off it). True when not against a human, or P_REGION off. */
static bool region_ok(const Nm *n, int type, int gx, int gy)
{
    if (!g_param[P_REGION] || !vs_human(n)) {
        return true;
    }
    const NmRegion *rg = region_of(n, type);
    int half = (0x20 - unit_stats_for(n->g, (u8)type)->size) >> 1;
    int tx = (gx + half + 4) >> 3, ty = (gy + half + 4) >> 3;
    for (int y = ty - 1; y <= ty + 1; y++) {
        for (int x = tx - 1; x <= tx + 1; x++) {
            if (x >= 0 && y >= 0 && x <= 255 && y <= 255
                && (rg->bits[y][x >> 3] & (1 << (x & 7)))) {
                return true;
            }
        }
    }
    return false;
}

/* The drive from (gx, gy)'s place as kept (nest: the nest test's):
 * NM_DRIVE_UNTRIED if not tried. */
static int drive_cached(Nm *n, int gx, int gy, bool nest)
{
    return drives_of(n, nest)->place[n->team][(gy >> 3) & 0xFF]
                                             [(gx >> 3) & 0xFF];
}

/* The drive from (gx, gy), the mech's place (8 px apart: a block's
 * corner or attack_ring's): kept, tried now (P_DRIVE_NEW a think at most,
 * the nest test's counted with the rest), or NM_DRIVE_UNTRIED when this
 * think has tried its share. */
static int drive_ok(Nm *n, int gx, int gy, bool nest)
{
    if (!nest && !region_ok(n, g_param[P_RUSH_TYPE], gx, gy)) {
        /* (session 70) no ground joins it to the HQ: kept so, as a
         * drive's answer is (drive_cached) */
        drives_of(n, false)->place[n->team][(gy >> 3) & 0xFF]
                                           [(gx >> 3) & 0xFF] = NM_DRIVE_BAD;
        return NM_DRIVE_BAD;
    }
    if (!nest && (((gx | gy) & 31) == 0 || g_param[P_ATLAS] >= 2)) {
        /* (s. 68) a block's corner: the atlas's answer, if it has one
         * (P_ATLAS 2: any place, its nearest corner's) */
        int av = atlas_at(atlas_of(n), (gx + 16) >> 5, (gy + 16) >> 5);
        if (av == NM_ATLAS_BAD) {
            return NM_DRIVE_BAD;
        }
        if (av != NM_ATLAS_UNKNOWN) {
            return NM_DRIVE_GOOD;
        }
    }
    NmDrives *dr = drives_of(n, false);
    u8 (*cache)[256] = drives_of(n, nest)->place[n->team];
    u8 *d = &cache[(gy >> 3) & 0xFF][(gx >> 3) & 0xFF];
    if (*d != NM_DRIVE_UNTRIED) {
        return *d;
    }
    if (dr->think[n->team] != n->s->frames) {
        dr->think[n->team] = n->s->frames;
        dr->fresh[n->team] = 0;
    }
    if (dr->fresh[n->team] >= g_param[P_DRIVE_NEW]) {
        return NM_DRIVE_UNTRIED;
    }
    dr->fresh[n->team]++;
    n->s->drives++;
    *d = drive_run(n, gx, gy, nest) ? NM_DRIVE_GOOD : NM_DRIVE_BAD;
    if (*d == NM_DRIVE_BAD) {
        n->s->drives_bad++;
    }
    for (int i = 0; i < g_drive_path_len; i++) {
        u8 *p = &cache[g_drive_path[i] >> 8][g_drive_path[i] & 0xFF];
        if (*p == NM_DRIVE_UNTRIED) {
            *p = *d;
        }
    }
    return *d;
}

/* An attack unit's place near (x, y): as landable_ring, on a `step` px
 * grid (8 or more), out of missiles, on plain ground (landable takes a
 * slope for a human's pad, as the game does) and with the drive passing.
 * NM_DRIVE_GOOD: (ox, oy) found; NM_DRIVE_UNTRIED: this think's tries ran
 * out first; NM_DRIVE_BAD: none. */
static int attack_ring(Nm *n, int x, int y, int inner, int reach, int step,
                       s16 *ox, s16 *oy)
{
    x &= ~7;
    y &= ~7;
    for (int r = inner; r <= reach; r += step) {
        for (int dy = -r; dy <= r; dy += step) {
            for (int dx = -r; dx <= r; dx += step) {
                int px = x + dx, py = y + dy;
                if (imax(iabs(dx), iabs(dy)) != r
                    || (danger_at(n, px, py) & DANGER_MISSILE)
                    || !landable(n, px, py)
                    || mech_ground_at(n->g, n->m, (s16)px, (s16)py) != 0) {
                    continue;
                }
                int d = drive_ok(n, px, py, false);
                if (d == NM_DRIVE_GOOD) {
                    *ox = (s16)px;
                    *oy = (s16)py;
                }
                if (d != NM_DRIVE_BAD) {
                    return d;
                }
            }
        }
    }
    return NM_DRIVE_BAD;
}

/* (Session 68, P_ATLAS_LINE) An attack unit (not a boat) carried, and an
 * atlas for the map, the side and the unit; against a human (2: every
 * game). In the owner's game of 30 October, 21:56 UTC (ABGRUND, as
 * P2), his SAM-42s round his HQ kept the jet from the drops the atlas
 * passes; task_deliver put its TAX-52s down as far along the line as the
 * fuel went, 650 to 720 px north of his HQ, on blocks the atlas fails
 * (each drive ends in the ravine under the cliff): they drove to 230 px
 * off and never fired. Against the CPU (TYPE B, 8 a set) 100-10-18 with
 * it, 106-7-15 without. */
static bool atlas_line(const Nm *n)
{
    return g_param[P_ATLAS_LINE] >= (vs_human(n) ? 1 : 2)
           && n->s->cargo == NM_CARGO_ATTACK
           && !attack_boat(n) && atlas_of(n) != NULL;
}

/* (Session 68) As drop_ring, by the atlas: the nearest place to (x, y)
 * within `reach` px (16 px steps) whose block the atlas didn't fail, out
 * of missiles, or else not drop_hot. Only on blocks it tried and passed,
 * on EISFREI against the CPU (TYPE B) its attack units were all kept
 * home, 55 to 66 a game (the middle is ice, not plain ground, which the
 * atlas doesn't try): 0-8 drawn as P1, from 8-0. */
static bool atlas_ring(Nm *n, int x, int y, int reach, s16 *ox, s16 *oy)
{
    const NmAtlas *a = atlas_of(n);
    x &= ~7;
    y &= ~7;
    for (int pass = 0; pass < 2; pass++) {
        for (int r = 0; r <= reach; r += 16) {
            for (int dy = -r; dy <= r; dy += 16) {
                for (int dx = -r; dx <= r; dx += 16) {
                    int px = x + dx, py = y + dy;
                    if (imax(iabs(dx), iabs(dy)) != r
                        || (pass == 0
                            && (danger_at(n, px, py) & DANGER_MISSILE))
                        || (pass == 1 && drop_hot(n, px, py))) {
                        continue;
                    }
                    int av = atlas_at(a, (px + 16) >> 5, (py + 16) >> 5);
                    if (av == NM_ATLAS_BAD || !landable(n, px, py)
                        || mech_ground_at(n->g, n->m, (s16)px, (s16)py)
                           != 0) {
                        continue;
                    }
                    *ox = (s16)px;
                    *oy = (s16)py;
                    return true;
                }
            }
        }
    }
    return false;
}

/* (Session 68) Where an attack unit carried on half a tank or less (one
 * lifted) goes down, rather than to a base to be filled first: the block
 * whose drive the atlas passes in the time its fuel lasts (its fuel use a
 * frame, P_PARK % to spare), out of missiles, not cold, in the leg's
 * reach, the shortest drive and flight of those (frames, the flight at 2
 * px a frame). In the owner's game of 30 October, 21:56 UTC (ABGRUND),
 * its TAX-52s lifted from the ravine north of his HQ were flown 1,000 px
 * home to fill, and back to the ravine; the cliff above it, a block or two
 * off, passes. false if none. */
static bool park_drop(Nm *n, s16 *ox, s16 *oy)
{
    const NmAtlas *a = atlas_of(n);
    if (g_param[P_PARK] <= 0 || a == NULL || attack_boat(n)) {
        return false;
    }
    const HzUnit *u = carried_unit(n->g, n->m);
    long fuel = (long)u->fuel * 100 / (100 + g_param[P_PARK]);
    long use = imax(u->fuel_use, 1) * NM_ATLAS_TICK;
    long best = 0x7FFFFFFFL;
    for (int by = 0; by < 64; by++) {
        for (int bx = 0; bx < 64; bx++) {
            int av = atlas_at(a, bx, by);
            if (av == NM_ATLAS_UNKNOWN || av == NM_ATLAS_BAD
                || av * use > fuel) {
                continue;
            }
            int gx = bx * 32, gy = by * 32;      /* the mech's x, y */
            long score = (long)av * NM_ATLAS_TICK * 2
                         + chebyshev(gx - n->m->x, gy - n->m->y) / 2;
            if (score >= best || (n->danger[by][bx] & DANGER_MISSILE)
                || cold_near(n, gx, gy) || !landable(n, gx, gy)
                || mech_ground_at(n->g, n->m, (s16)gx, (s16)gy) != 0
                || !leg_ok(n, gx, gy, g_param[P_FUEL_MARGIN])) {
                continue;
            }
            best = score;
            *ox = (s16)gx;
            *oy = (s16)gy;
        }
    }
    return best != 0x7FFFFFFFL;
}

/* attack_drop_at's searches: no drive test, the drive passing, or
 * (s. 68) the nest test passing (nest_scan) */
enum { NM_SCAN_ANY, NM_SCAN_DRIVE, NM_SCAN_NEST };

static int attack_drop_scan(Nm *n, int reach, int near, int how,
                            s16 *ox, s16 *oy);

/* Where an attack unit goes down: the landable block, out of missiles,
 * with the shortest drive in (`near` from the HQ at best -- drop_range,
 * or a rally point's -- none nearer), the nearest the mech of those;
 * with `reach` NM_REACH_MECH, only those leg_ok passes from where the mech
 * is; NM_REACH_BASE, those a unit is carried to from an own base on a
 * full tank. False if none. */
/* (Session 68) A boat's drop at block (bx, by) flown to straight, out of
 * missiles: from the mech (NM_REACH_MECH), else from a base whose full
 * tank reaches it. On STRAND (TYPE B) one 620 px off was planned on the
 * straight way, and the jet, going round the SAM-42s, turned back for
 * fuel each time, the boat carried 40 minutes. */
static bool boat_way(const Nm *n, int reach, int bx, int by)
{
    if (reach == NM_REACH_MECH) {
        return line_safe(n, clamp63(mx(n) >> 5), clamp63(my(n) >> 5), bx,
                         by);
    }
    int full = kidou(n)->stats.fuel;
    for (int b = 0; b < HZ_POST_CLEARED; b++) {
        if (is_base(n, b)
            && line_safe(n, clamp63(post(n, b)->x >> 5),
                         clamp63(post(n, b)->y >> 5), bx, by)
            && leg_ok_from(n, post(n, b)->x - 16, post(n, b)->y - 16, full,
                           bx * 32, by * 32, g_param[P_FUEL_MARGIN])) {
            return true;
        }
    }
    return false;
}

/* (Session 68) Are the attack drops a boat's: the scan's own kind, else
 * the carried unit's (a boat: an ST-57U)? */
static bool attack_boat(const Nm *n)
{
    if (n->boat_scan >= 0) {
        return n->boat_scan > 0;
    }
    return (n->m->flags & MECH_CARRYING)
           && (carried_unit(n->g, n->m)->flags & UNIT_BOAT);
}

/* (Session 68) Boats for attack units (P_NAVAL): the map sells them, no
 * land a unit drives to the enemy HQ from is out of its missiles, and
 * water a boat is carried to from a base and sails into its gun's reach
 * from is. On STRAND each base is an island: the CPU's
 * SAM-42s (TYPE B on) cover its own, and the attack units were put down
 * on others, from which they drove till their fuel ran out. */
static bool naval_wanted(Nm *n)
{
    if (g_param[P_NAVAL] <= 0 || g_param[P_STYLE] == 1
        || !(shop_types(n->g) & (1u << UNIT_ST))) {
        return false;
    }
    s16 x, y;
    n->boat_scan = 0;
    bool land = attack_drop_best(n, NM_REACH_ANY, g_param[P_DROP_RANGE],
                                 NM_SCAN_ANY, &x, &y) != NM_DRIVE_BAD;
    n->boat_scan = 1;
    bool water = !land
                 && attack_drop_best(n, NM_REACH_BASE, g_param[P_DROP_RANGE],
                                     NM_SCAN_ANY, &x, &y) != NM_DRIVE_BAD;
    n->boat_scan = -1;
    return water;
}

static bool attack_drop_at(Nm *n, int reach, int near, s16 *ox, s16 *oy,
                           bool *untried, bool *nest)
{
    /* (session 68) the drive tried too: the best block whose unit hits
     * the HQ; this think's tries used first, the best untried (*untried
     * set); none passing, from the mech, the nest test's (*nest set);
     * none, the best of all, as before */
    if (untried != NULL) {
        *untried = false;
    }
    if (nest != NULL) {
        *nest = false;
    }
    if (g_param[P_DRIVE] > 0) {
        int got = attack_drop_scan(n, reach, near, NM_SCAN_DRIVE, ox, oy);
        if (got == NM_DRIVE_BAD && g_param[P_NEST] > 0
            && reach == NM_REACH_MECH && nest != NULL) {
            got = attack_drop_scan(n, reach, near, NM_SCAN_NEST, ox, oy);
            *nest = got != NM_DRIVE_BAD;
        }
        if (got == NM_DRIVE_GOOD) {
            return true;
        }
        if (got == NM_DRIVE_UNTRIED) {
            if (untried != NULL) {
                *untried = true;
            }
            return true;
        }
    }
    if (g_param[P_NO_BLIND] && g_param[P_DRIVE] > 0) {
        return false;
    }
    return attack_drop_scan(n, reach, near, NM_SCAN_ANY, ox, oy)
           == NM_DRIVE_GOOD;
}

static int attack_drop_best(Nm *n, int reach, int near, int how,
                            s16 *ox, s16 *oy);

/* attack_drop_at's search; with the drive, best first: the best block not
 * known to fail the drive test is tried, then the next, till one passes
 * or this think's tries are used (so no more are tried than the blocks
 * failing that score better than the best that passes). NM_DRIVE_GOOD:
 * (ox, oy) found; NM_DRIVE_UNTRIED: (ox, oy) the best, untried;
 * NM_DRIVE_BAD: none. */
static int attack_drop_scan(Nm *n, int reach, int near, int how,
                            s16 *ox, s16 *oy)
{
    for (;;) {
        int got = attack_drop_best(n, reach, near, how, ox, oy);
        if (got != NM_DRIVE_UNTRIED) {
            return got;
        }
        got = drive_ok(n, *ox, *oy, how == NM_SCAN_NEST);
        if (got != NM_DRIVE_BAD) {
            return got;
        }
    }
}

/* (Session 68) The enemy's SAMs and GRMs within NM_NEST_HQ px of its HQ
 * (their centres, NM_NEST_MAX at most): the nest a nest scan's unit goes
 * for. Their count. */
#define NM_NEST_HQ  1024
#define NM_NEST_MAX 64
#define NM_SPREAD_TIME 36000    /* (P_DROP_SPREAD) a drop this recent */

static int nest_guards(Nm *n, s16 (*at)[2])
{
    const HzPost *e = post(n, enemy_hq(n));
    int es = 1 - n->team, count = 0;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en && count < NM_NEST_MAX; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (nest_unit(u)
            && chebyshev(ucx(u) - e->x, ucy(u) - e->y) <= NM_NEST_HQ) {
            at[count][0] = (s16)ucx(u);
            at[count][1] = (s16)ucy(u);
            count++;
        }
    }
    return count;
}

/* (Session 68, P_COLD_JUDGE) Is the mech's (x, y) by a drop judged cold
 * in the last P_COLD_FRAMES? In the owner's game of 30 October
 * (ABGRUND, TYPE A) 19 attack units went down one at a time, 16 at one
 * place 640 px from his HQ, into nine TURRET-1s and two TURRET-2s: 114
 * HP off it in all. */
static bool cold_near(const Nm *n, int x, int y)
{
    const HzNightmare *s = n->s;
    if (g_param[P_COLD_JUDGE] <= 0) {
        return false;
    }
    for (int k = 0; k < NM_COLD; k++) {
        if (s->cold_at[k] != 0
            && s->frames - s->cold_at[k] < (u32)g_param[P_COLD_FRAMES]
            && chebyshev(x - s->cold_x[k], y - s->cold_y[k])
               <= g_param[P_COLD_R]) {
            return true;
        }
    }
    return false;
}

/* The best block, within 24 blocks of the HQ; with the drive, of those
 * not known to fail the drive test, and its drive as kept. A nest scan's
 * (NM_SCAN_NEST): anywhere within P_NEST px of one of the nest, the
 * nearest it the best, its nest test as kept. */
static int attack_drop_best(Nm *n, int reach, int near, int how,
                            s16 *ox, s16 *oy)
{
    const HzPost *e = post(n, enemy_hq(n));
    ground_build(n);
    bool boat = attack_boat(n);
    if (boat && how != NM_SCAN_ANY) {
        return NM_DRIVE_BAD;        /* (s. 68) a boat's never tried */
    }
    long best = 0x7FFFFFFFL;
    int found = NM_DRIVE_BAD;
    int ex = e->x >> 5, ey = e->y >> 5;
    int span = 24;
    /* (s. 68) a land unit's: the atlas's drives, where it has them */
    const NmAtlas *atlas = how == NM_SCAN_NEST || boat ? NULL : atlas_of(n);
    s16 guard[NM_NEST_MAX][2];
    int guards = 0;
    /* (session 68, P_DROP_INSIDE, P_DROP_SPREAD) the owner: "your AI
     * obsesses over dropping units in the exact same place." Tried, off
     * (P_DROP_SPREAD 32): against the turtle (pinned to the old ways),
     * all maps, both seats, 90-15-23 against 95-13-20; on map 6 as P2
     * and map 5, 16 a set, 17-21-10 against 25-14-9. */
    bool inside = how != NM_SCAN_NEST && !boat && drop_inside(n);
    bool spread = how != NM_SCAN_NEST && g_param[P_DROP_SPREAD] > 0
                  && vs_human(n);
    if (how == NM_SCAN_NEST) {
        guards = nest_guards(n, guard);
        if (guards == 0) {
            return NM_DRIVE_BAD;
        }
        span = 63;
    }
    for (int by = imax(0, ey - span); by <= imin(63, ey + span); by++) {
        for (int bx = imax(0, ex - span); bx <= imin(63, ex + span); bx++) {
            u16 gd = g_ground[n->team][by][bx];
            int gx = bx * 32, gy = by * 32;      /* the mech's x, y */
            int hq = chebyshev(gx + 16 - e->x, gy + 16 - e->y);
            long score;
            int av = atlas_at(atlas, bx, by);
            if (how == NM_SCAN_NEST) {
                int g = 0x7FFF;
                for (int i = 0; i < guards; i++) {
                    g = imin(g, chebyshev(gx + 16 - guard[i][0],
                                          gy + 16 - guard[i][1]));
                }
                if (g > g_param[P_NEST]) {
                    continue;
                }
                score = (long)g * 4;
            } else {
                if (boat) {
                    gd = g_water[n->team][by][bx];
                }
                if (av == NM_ATLAS_BAD || (!boat && cold_near(n, gx, gy))) {
                    continue;
                }
                if (av != NM_ATLAS_UNKNOWN) {
                    /* its frames twice: a block's drive about 32 */
                    score = (long)av * NM_ATLAS_TICK * 2 + hq / 4;
                } else if (gd == NM_FAR) {
                    continue;
                } else {
                    score = (long)gd * 64 + hq / 4;
                }
            }
            if (hq < near) {
                continue;
            }
            if (g_param[P_FLANK] > 0 && how != NM_SCAN_NEST) {
                /* (session 68) the owner's way on 30 October, 21:56
                 * UTC: put down behind the HQ, by the map's edge */
                const HzPost *o = post(n, own_hq(n));
                int vx = o->x - e->x, vy = o->y - e->y;
                long facing = ((long)(gx + 16 - e->x) * vx
                               + (long)(gy + 16 - e->y) * vy)
                              / imax(chebyshev(vx, vy), 1);
                if (facing > 0) {
                    score += facing * g_param[P_FLANK];
                }
            }
            score += chebyshev(gx - n->m->x, gy - n->m->y) / 16
                     + nm_rand(n, g_param[P_JITTER] + 1);
            if (inside && (n->danger[by][bx] & DANGER_MISSILE)) {
                if ((n->danger[by][bx] & DANGER_CORE)
                    || n->missiles[by][bx] >= g_param[P_DROP_INSIDE]
                    || (g_param[P_DROP_INSIDE_SAM] && grm_reach(n, gx, gy))) {
                    continue;
                }
                score += (long)n->missiles[by][bx]
                         * g_param[P_DROP_INSIDE_COST];
            }
            if (spread) {
                /* (session 68) not where the last went down: in
                 * The owner's games of 31 October 17 of its attack units
                 * a game went down at one place */
                const HzNightmare *s = n->s;
                for (int k = 0; k < NM_DROPLOG; k++) {
                    if (s->drop_at[k] != 0
                        && s->frames - s->drop_at[k] < NM_SPREAD_TIME
                        && chebyshev(gx - s->drop_x[k], gy - s->drop_y[k])
                           <= g_param[P_DROP_SPREAD_R]) {
                        score += g_param[P_DROP_SPREAD];
                    }
                }
            }
            if (score >= best) {
                continue;
            }
            if (!inside && (n->danger[by][bx] & DANGER_MISSILE)) {
                continue;
            }
            if (boat ? !water_at(n, gx, gy) || !boat_way(n, reach, bx, by)
                     : (!landable(n, gx, gy)
                        || mech_ground_at(n->g, n->m, (s16)gx, (s16)gy)
                           != 0)) {
                continue;
            }
            if (reach == NM_REACH_MECH
                && !leg_ok(n, gx, gy, g_param[P_FUEL_MARGIN])) {
                continue;
            }
            if (reach == NM_REACH_BASE && !base_reach(n, gx, gy)) {
                continue;
            }
            int d = how == NM_SCAN_ANY || av != NM_ATLAS_UNKNOWN
                    ? NM_DRIVE_GOOD
                    : drive_cached(n, gx, gy, how == NM_SCAN_NEST);
            if (d == NM_DRIVE_BAD) {
                continue;
            }
            best = score;
            *ox = (s16)gx;
            *oy = (s16)gy;
            found = d;
        }
    }
    return found;
}

static void attack_drop(Nm *n, s16 *ox, s16 *oy)
{
    if (attack_drop_at(n, NM_REACH_ANY, g_param[P_DROP_RANGE], ox, oy, NULL,
                       NULL)) {
        return;
    }
    const HzPost *e = post(n, enemy_hq(n));
    int dx = mx(n) - e->x, dy = my(n) - e->y;
    int len = imax(chebyshev(dx, dy), 1);
    int gx = e->x + dx * g_param[P_DROP_RANGE] / len - 16;
    int gy = e->y + dy * g_param[P_DROP_RANGE] / len - 16;
    if (!landable_near(n, gx, gy, 96, ox, oy)) {
        *ox = (s16)gx;
        *oy = (s16)gy;
    }
}

/* Where a capture's INFANTRY goes down: the landable place nearest the
 * mech round the outpost, 48-128 px off. */
/* Can a unit drive straight from (x0, y0) to (x1, y1) (centres, px):
 * every block on the way one it drives on? (An INFANTRY with the outpost
 * order heads straight for the outpost.) */
static bool ground_line(Nm *n, int x0, int y0, int x1, int y1)
{
    ground_build(n);
    int steps = imax(1, chebyshev(x1 - x0, y1 - y0) / 16);
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
        if (!g_pass[n->team][clamp63(y >> 5)][clamp63(x >> 5)]) {
            return false;
        }
    }
    return true;
}

static void capture_drop(Nm *n, int post_i, s16 *ox, s16 *oy)
{
    const HzPost *pp = post(n, post_i);
    int best_d = 0x7FFFFFFF;
    bool found = false;
    /* (session 74, P_HOLD_BACK) INFANTRY for a hold of its own that put
     * none back from where they went down (hold_bought's failures): the
     * next go down on another side of the outpost, a quarter turn on for
     * each lot that failed (one stood 50 px west of its outpost for 17
     * minutes: no way in from there) */
    int from_x = n->m->x, from_y = n->m->y;
    int fails = (post_i < NM_OUTPOSTS && hold_lost(n, post_i) > 0)
                ? n->s->hold_fails[post_i] : 0;
    if (fails > 0) {
        int d0 = bearing16(pp->x, pp->y, n->m->x + 16, n->m->y + 16);
        int d1 = (d0 + 4 * fails) & 15;
        from_x = pp->x + SINE_TABLE[d1].x * 8 - 16;
        from_y = pp->y + SINE_TABLE[d1].y * 8 - 16;
    }
    for (int r = 48; r <= 160 && !found; r += 16) {
        for (int d = 0; d < 16; d++) {
            int gx = pp->x + SINE_TABLE[d].x * r / 32 - 16;
            int gy = pp->y + SINE_TABLE[d].y * r / 32 - 16;
            if (!landable(n, gx, gy)
                || !ground_line(n, gx + 16, gy + 16, pp->x, pp->y)) {
                continue;
            }
            int dd = chebyshev(gx - from_x, gy - from_y);
            if (dd < best_d) {
                best_d = dd;
                *ox = (s16)gx;
                *oy = (s16)gy;
                found = true;
            }
        }
    }
    if (!found) {
        *ox = (s16)(pp->x - 16);
        *oy = (s16)(pp->y - 16 + 64);
    }
}

/* The own base to collect at for a unit going to (x, y): the one nearest
 * it that the fuel reaches, out of missiles. */
static int pickup_base(const Nm *n, int x, int y)
{
    int best = own_hq(n), best_d = 0x7FFF;
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        if (!is_base(n, i)) {
            continue;
        }
        int there = base_dist(n, i, mx(n), my(n));
        if (trip_fuel(n, there, false) + g_param[P_FUEL_MARGIN]
            > n->m->fuel) {
            continue;
        }
        if (n->danger[clamp63(post(n, i)->y >> 5)][clamp63(post(n, i)->x >> 5)]
            & DANGER_MISSILE) {
            continue;
        }
        int d = base_dist(n, i, x, y);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* (Session 67) Style 1, a player's way, as the owner beat it twice
 * (2026-10-21): a wall of standing SAM-42s and TAX-52s round its own HQ,
 * on the side the enemy comes from, then attack units put down right by
 * the enemy HQ one after another. hz_arena's turtle: a stand-in for a
 * player to try the nightmare AI against. */

/* Its standing units round its HQ (within P_WALL_R + 128 px): how many,
 * and of them SAM-42s in *sams. */
static int wall_count(const Nm *n, int *sams)
{
    const HzPost *hq = post(n, own_hq(n));
    int count = 0;
    *sams = 0;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (!unit_alive(u) || u->order != ORDER_STAND
            || chebyshev(ucx(u) - hq->x, ucy(u) - hq->y)
               > g_param[P_WALL_R] + 128) {
            continue;
        }
        count++;
        if (u->type == UNIT_SAM) {
            (*sams)++;
        }
    }
    return count;
}

/* (Session 68) Its GRM-34As standing round its HQ, as wall_count. */
static int wall_grms(const Nm *n)
{
    const HzPost *hq = post(n, own_hq(n));
    int count = 0;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (unit_alive(u) && u->order == ORDER_STAND && u->type == UNIT_GRM
            && chebyshev(ucx(u) - hq->x, ucy(u) - hq->y)
               <= g_param[P_WALL_R] + 128) {
            count++;
        }
    }
    return count;
}

/* Is there one of its own units within 48 px of ctx (x, y)? */
static bool crowds(const HzUnit *u, const void *ctx)
{
    const int *pt = ctx;
    return chebyshev(ucx(u) - pt[0], ucy(u) - pt[1]) < 48;
}

/* (Session 68) How near (x, y) the nearest spot its HQ was shot from
 * is, px; 0x7FFF none known. */
static int spot_dist(const Nm *n, int x, int y)
{
    int d = 0x7FFF;
    for (int k = 0; k < NM_SPOTS; k++) {
        if (n->s->spot_hits[k] > 0) {
            d = imin(d, chebyshev(x - n->s->spot_x[k], y - n->s->spot_y[k]));
        }
    }
    return d;
}

/* (Session 68) Its units standing or guarding within `reach` px of
 * (x, y). */
static int holders_near(const Nm *n, int x, int y, int reach)
{
    int count = 0;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (unit_alive(u)
            && (u->order == ORDER_STAND || u->order == ORDER_GUARD_NEAR)
            && chebyshev(ucx(u) - x, ucy(u) - y) <= reach) {
            count++;
        }
    }
    return count;
}

/* -- (session 75, P_HOME_LANES) room for its trucks ---------------------
 *
 * The owner, of his game of 15 November (LOCH): "You aren't leaving
 * space for supply trucks or repair trucks to reach the units, you can't
 * just drop the supply truck in position if it's over crowded with
 * units." At 65:00 41 of its units stood within 210 px of its HQ, most in
 * one block on the side its HQ was shot from (its guards, each put down
 * within 64 px of a spot with nothing kept between them), 0 to 12 px from
 * one's box to the next; a truck's box is 24 px. Its SUPPLY, put down at
 * the block's edge, found no way in (route_find), and 17 of its 20
 * SAM-42s ended with no missiles. A carried SUPPLY can't go down in a gap
 * either: Mech_CheckLandBlocked widens both boxes by $C for it, so no
 * unit's middle may be within 40 px of the mech's.
 *
 * So by its HQ a unit stands only on a block place: blocks of 2 x 2, 32
 * px apart (8 px between boxes), a lane between blocks every
 * NM_LANE_PITCH px both ways, 40 px wide, the lanes through the HQ's
 * middle. Every unit has a lane on two sides; a truck in it is 8 px from
 * the unit (SUPPLY_REACH is 16), with its middle in a 32-px cell no unit's
 * is in (route_bumps). No block place within NM_LANE_FIRST px of the
 * HQ's middle: the HQ's ground is no ground to 80 px either side of its
 * middle on every map (5 x 5 cells), and round it a ring road where a
 * truck is put down (NM_YARD_R: 48 px from the first units' middles, past
 * the 40 a SUPPLY going down wants) and from which every lane leads out.
 * An HQ's middle is a cell's middle, so every place and lane is too. */

#define NM_LANE_PITCH 96        /* a block and its lane, px */
#define NM_LANE_FIRST 160       /* no unit's middle nearer the HQ's, px */
#define NM_LANE_BANDS 2         /* rings of blocks round it, at most (out
                                   to 256 px: wall_count and wall_short
                                   look 288 out) */
#define NM_YARD_R     112       /* a truck goes down this far from it */

static bool lanes_on(const Nm *n)
{
    return g_param[P_HOME_LANES] && vs_human(n);
}

/* An offset from the HQ's middle on one axis, past the lane before it. */
static int lane_mod(int d)
{
    return ((d % NM_LANE_PITCH) + NM_LANE_PITCH) % NM_LANE_PITCH;
}

/* ... a block place's: 32 or 64 px past a lane? */
static bool lane_block_axis(int d)
{
    int m = lane_mod(d);
    return m == 32 || m == 64;
}

/* Does a unit of either side, not carried, have its middle within `near`
 * px of (x, y)? */
static bool unit_middle_near(const Nm *n, int x, int y, int near)
{
    for (int side = 0; side < 2; side++) {
        u32 on = side_slots(n->g, side);
        for (u32 i = 0; i < on; i++) {
            const HzUnit *u = unit_at_c(n->g, side, i);
            if (unit_alive(u) && chebyshev(ucx(u) - x, ucy(u) - y) < near) {
                return true;
            }
        }
    }
    return false;
}

/* Can a truck drive at (x, y), its middle (the mech's ground there)? */
static bool truck_ground(Nm *n, int x, int y)
{
    return on_map(x - 16, y - 16)
           && mech_ground_at(n->g, n->m, (s16)(x - 16), (s16)(y - 16)) == 0;
}

/* Which places on the lanes a truck can reach from the ring round its HQ:
 * the 32-px points from its HQ's middle out to NM_LANE_HALF steps each
 * way, those on a lane or within NM_LANE_FIRST of the middle, ground a
 * truck drives on and no unit's middle within 28 px; reached from those
 * within NM_LANE_FIRST, one step at a time along the lanes. */
#define NM_LANE_HALF 12
#define NM_LANE_SIDE (2 * NM_LANE_HALF + 1)

typedef struct {
    u8 reached[NM_LANE_SIDE][NM_LANE_SIDE];
} LaneMap;

static void lane_map(Nm *n, LaneMap *lm)
{
    const HzPost *hq = post(n, own_hq(n));
    u8 open[NM_LANE_SIDE][NM_LANE_SIDE];
    u8 queue[NM_LANE_SIDE * NM_LANE_SIDE][2];
    int head = 0, tail = 0;
    memset(lm, 0, sizeof *lm);
    for (int j = 0; j < NM_LANE_SIDE; j++) {
        for (int i = 0; i < NM_LANE_SIDE; i++) {
            int dx = (i - NM_LANE_HALF) * 32, dy = (j - NM_LANE_HALF) * 32;
            int x = hq->x + dx, y = hq->y + dy;
            bool inside = imax(iabs(dx), iabs(dy)) <= NM_LANE_FIRST;
            open[j][i] = (inside || lane_mod(dx) == 0 || lane_mod(dy) == 0)
                         && truck_ground(n, x, y)
                         && !unit_middle_near(n, x, y, 28);
            if (open[j][i] && inside) {
                lm->reached[j][i] = 1;
                queue[tail][0] = (u8)i;
                queue[tail][1] = (u8)j;
                tail++;
            }
        }
    }
    static const int DX[4] = { 1, -1, 0, 0 };
    static const int DY[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int i = queue[head][0], j = queue[head][1];
        head++;
        for (int d = 0; d < 4; d++) {
            int ni = i + DX[d], nj = j + DY[d];
            if (ni < 0 || nj < 0 || ni >= NM_LANE_SIDE || nj >= NM_LANE_SIDE
                || !open[nj][ni] || lm->reached[nj][ni]) {
                continue;
            }
            lm->reached[nj][ni] = 1;
            queue[tail][0] = (u8)ni;
            queue[tail][1] = (u8)nj;
            tail++;
        }
    }
}

/* Is the lane place (x, y) one lm has a truck reach? */
static bool lane_reached(const Nm *n, const LaneMap *lm, int x, int y)
{
    const HzPost *hq = post(n, own_hq(n));
    int dx = x - hq->x, dy = y - hq->y;
    int i = dx / 32 + NM_LANE_HALF, j = dy / 32 + NM_LANE_HALF;
    return dx % 32 == 0 && dy % 32 == 0 && i >= 0 && j >= 0
           && i < NM_LANE_SIDE && j < NM_LANE_SIDE && lm->reached[j][i];
}

/* Is (x, y), a unit's middle, a block place by its HQ the carried unit
 * can be put down on: not by the map's edge, no post's pad within 64 px,
 * nothing there, and a lane beside it (on its x side or its y side) that
 * a truck reaches from the ring round the HQ (lm)? */
static bool lane_place_ok(Nm *n, const LaneMap *lm, int x, int y)
{
    const HzPost *hq = post(n, own_hq(n));
    int dx = x - hq->x, dy = y - hq->y;
    if (!lane_block_axis(dx) || !lane_block_axis(dy)
        || imax(iabs(dx), iabs(dy)) < NM_LANE_FIRST
        || by_edge(x - 16) || by_edge(y - 16)) {
        return false;
    }
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        const HzPost *pp = post(n, i);
        if ((pp->flags & POST_USED) && chebyshev(pp->x - x, pp->y - y) < 64) {
            return false;
        }
    }
    if (!landable(n, x - 16, y - 16) || unit_middle_near(n, x, y, 28)) {
        return false;
    }
    int lx = x + (lane_mod(dx) == 32 ? -32 : 32);
    int ly = y + (lane_mod(dy) == 32 ? -32 : 32);
    return lane_reached(n, lm, lx, y) || lane_reached(n, lm, x, ly);
}

/* The free block place nearest (x, y) (a unit's middle) within `reach` px
 * of it, as the mech's x, y. False if none. */
static bool lane_near(Nm *n, int x, int y, int reach, s16 *ox, s16 *oy)
{
    const HzPost *hq = post(n, own_hq(n));
    LaneMap lm;
    lane_map(n, &lm);
    /* the 32-px steps from the HQ's middle that cover the square */
    int k0x = (x - reach - hq->x - 31) / 32, k1x = (x + reach - hq->x + 31) / 32;
    int k0y = (y - reach - hq->y - 31) / 32, k1y = (y + reach - hq->y + 31) / 32;
    int best = 0x7FFF;
    for (int ky = k0y; ky <= k1y; ky++) {
        for (int kx = k0x; kx <= k1x; kx++) {
            int px = hq->x + kx * 32, py = hq->y + ky * 32;
            int d = chebyshev(px - x, py - y);
            if (d > reach || d >= best || !lane_place_ok(n, &lm, px, py)) {
                continue;
            }
            best = d;
            *ox = (s16)(px - 16);
            *oy = (s16)(py - 16);
        }
    }
    return best != 0x7FFF;
}

/* As ring_drop, on the block places: the first ring of blocks out from
 * the HQ with a free place (160 px off -- 128 is within NM_LANE_FIRST --
 * then 224 and 256, ...), there the place ring_drop's worth picks. False
 * if none. */
static bool lane_ring_drop(Nm *n, s16 *ox, s16 *oy)
{
    const HzPost *hq = post(n, own_hq(n));
    const HzPost *e = post(n, enemy_hq(n));
    int vx = e->x - hq->x, vy = e->y - hq->y;
    int len = imax(chebyshev(vx, vy), 1);
    LaneMap lm;
    lane_map(n, &lm);
    for (int band = 1; band <= NM_LANE_BANDS; band++) {
        int lo = band * NM_LANE_PITCH + 32, hi = band * NM_LANE_PITCH + 64;
        long best = 0x7FFFFFFFL;
        for (int dy = -hi; dy <= hi; dy += 32) {
            for (int dx = -hi; dx <= hi; dx += 32) {
                int r = imax(iabs(dx), iabs(dy));
                int cx = hq->x + dx, cy = hq->y + dy;
                if (r < lo || !lane_place_ok(n, &lm, cx, cy)) {
                    continue;
                }
                long facing = ((long)dx * vx + (long)dy * vy) / len;
                int worth = 4 + (int)(facing > 0 ? facing * 4 / r : 0);
                if (spot_dist(n, cx, cy) <= 128) {
                    worth += 12;
                }
                long score = (long)(holders_near(n, cx, cy, 128) + 1) * 1000
                             / worth + nm_rand(n, 33);
                if (score < best) {
                    best = score;
                    *ox = (s16)(cx - 16);
                    *oy = (s16)(cy - 16);
                }
            }
        }
        if (best != 0x7FFFFFFFL) {
            n->s->lane_drops++;
            return true;
        }
    }
    return false;
}

/* (P_HOME_LANES) Where a truck goes down by its HQ (the mech's x, y): on
 * the ring NM_YARD_R px round the HQ's middle, nearest (x, y). False if
 * it can't there. */
static bool yard_find(Nm *n, int x, int y, s16 *ox, s16 *oy)
{
    const HzPost *hq = post(n, own_hq(n));
    int best = 0x7FFF;
    for (int dy = -NM_YARD_R; dy <= NM_YARD_R; dy += 16) {
        for (int dx = -NM_YARD_R; dx <= NM_YARD_R; dx += 16) {
            if (imax(iabs(dx), iabs(dy)) != NM_YARD_R) {
                continue;
            }
            int cx = hq->x + dx, cy = hq->y + dy;
            int d = chebyshev(cx - x, cy - y);
            if (d >= best || by_edge(cx - 16) || by_edge(cy - 16)
                || !landable(n, cx - 16, cy - 16)) {
                continue;
            }
            best = d;
            *ox = (s16)(cx - 16);
            *oy = (s16)(cy - 16);
        }
    }
    return best != 0x7FFF;
}

static bool yard_drop(Nm *n, int x, int y, s16 *ox, s16 *oy)
{
    if (!yard_find(n, x, y, ox, oy)) {
        return false;
    }
    n->s->yard_drops++;
    return true;
}

/* (Session 68, P_WALL_RING) Against a human, where the next wall unit goes
 * down (the mech's x, y): on the rings P_WALL_R and 64 px in, every side,
 * where its own units standing within 128 px are fewest for what the
 * place is worth: 4, up to 4 more the more it faces the enemy HQ, 12 more
 * within 128 px of a spot its HQ was shot from. So the wall goes up all
 * round as it is built, and a side shot from fills first. In
 * The owner's game of 30 October, 21:56 UTC (ABGRUND, TYPE A), its 41
 * stood on the side facing his HQ and his TAX-52s, put down behind it by
 * the map's edge, shot it from 716 HP to nothing. False if no place. */
static bool ring_drop(Nm *n, s16 *ox, s16 *oy)
{
    const HzPost *hq = post(n, own_hq(n));
    const HzPost *e = post(n, enemy_hq(n));
    int vx = e->x - hq->x, vy = e->y - hq->y;
    int len = imax(chebyshev(vx, vy), 1);
    long best = 0x7FFFFFFFL;
    for (int r = g_param[P_WALL_R]; r >= imax(96, g_param[P_WALL_R] - 64);
         r -= 64) {
        for (int dy = -r; dy <= r; dy += 16) {
            for (int dx = -r; dx <= r; dx += 16) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                int cx = hq->x + dx, cy = hq->y + dy;
                if (by_edge(cx - 16) || by_edge(cy - 16)) {
                    /* (session 73, P_WALL_EDGE) by the map's edge: none,
                     * as before P_EDGE; or the edge's own place, once its
                     * HQ was shot from within 128 px of it */
                    cx = edge_snap(cx - 16) + 16;
                    cy = edge_snap(cy - 16) + 16;
                    if (!g_param[P_WALL_EDGE] || spot_dist(n, cx, cy) > 128) {
                        continue;
                    }
                }
                if (!landable(n, cx - 16, cy - 16)) {
                    continue;
                }
                const int pt[2] = { cx, cy };
                if (any_unit_near(n->g, n->team, cx - 48, cy - 48, cx + 48,
                                  cy + 48, NULL, crowds, pt)) {
                    continue;
                }
                long facing = ((long)dx * vx + (long)dy * vy) / len;
                int worth = 4 + (int)(facing > 0 ? facing * 4 / r : 0);
                if (spot_dist(n, cx, cy) <= 128) {
                    worth += 12;
                }
                long score = (long)(holders_near(n, cx, cy, 128) + 1) * 1000
                             / worth + nm_rand(n, 33);
                if (score < best) {
                    best = score;
                    *ox = (s16)(cx - 16);
                    *oy = (s16)(cy - 16);
                }
            }
        }
    }
    return best != 0x7FFFFFFFL;
}

/* Where the next wall unit goes down (the mech's x, y): on the ring
 * P_WALL_R round the HQ, the half facing the enemy HQ, the nearest the
 * line between them that no unit of its own stands on yet; (session 68,
 * P_WALL_SPOTS) against a human, or as near a spot its HQ was shot from,
 * on any side. In the owner's game of 30 October, 21:56 UTC (ABGRUND,
 * TYPE A), his first TAX-52s shot its HQ from behind, by the map's edge;
 * the garrison it bought then stood on the side facing his HQ, and his
 * next ones, put down behind again, shot it from 716 HP to 36. Against
 * the turtle putting its units down behind (P_FLANK) it lost more (off:
 * P_SPOT_HUMAN's guards instead). */
static void wall_drop(Nm *n, s16 *ox, s16 *oy)
{
    const HzPost *hq = post(n, own_hq(n));
    const HzPost *e = post(n, enemy_hq(n));
    int vx = e->x - hq->x, vy = e->y - hq->y;
    int len = imax(chebyshev(vx, vy), 1);
    long best = 0x7FFFFFFFL;
    bool found = false;
    bool spots = g_param[P_WALL_SPOTS] && vs_human(n)
                 && spot_dist(n, hq->x, hq->y) < 0x7FFF;
    if (lanes_on(n) && lane_ring_drop(n, ox, oy)) {
        return;                     /* (session 75) on a block place */
    }
    if (g_param[P_WALL_RING] && vs_human(n) && !spots
        && ring_drop(n, ox, oy)) {
        return;
    }
    for (int r = g_param[P_WALL_R]; r >= 96 && (!found || spots); r -= 64) {
        for (int dy = -r; dy <= r; dy += 16) {
            for (int dx = -r; dx <= r; dx += 16) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                long facing = ((long)dx * vx + (long)dy * vy) / len;
                int cx = hq->x + dx, cy = hq->y + dy;
                if (by_edge(cx - 16) || by_edge(cy - 16)) {
                    /* (session 73, P_WALL_EDGE) as ring_drop */
                    cx = edge_snap(cx - 16) + 16;
                    cy = edge_snap(cy - 16) + 16;
                    if (!g_param[P_WALL_EDGE] || spot_dist(n, cx, cy) > 128) {
                        continue;
                    }
                }
                long score = (facing > 0) ? (r - facing) * 4 : 0x7FFFFFFFL;
                if (spots) {
                    /* how far round the ring from the line to the enemy
                     * HQ, or from a spot, whichever is nearer */
                    int line = chebyshev(dx - vx * r / len, dy - vy * r / len);
                    score = imin((facing > 0) ? line : 0x7FFF,
                                 spot_dist(n, cx, cy)) * 4L;
                }
                if (score == 0x7FFFFFFFL) {
                    continue;
                }
                score += nm_rand(n, 33);
                if (score >= best || !landable(n, cx - 16, cy - 16)) {
                    continue;
                }
                const int pt[2] = { cx, cy };
                if (any_unit_near(n->g, n->team, cx - 48, cy - 48, cx + 48,
                                  cy + 48, NULL, crowds, pt)) {
                    continue;
                }
                best = score;
                *ox = (s16)(cx - 16);
                *oy = (s16)(cy - 16);
                found = true;
            }
        }
    }
    if (!found && !landable_near(n, hq->x + vx * 128 / len - 16,
                                 hq->y + vy * 128 / len - 16, 128, ox, oy)) {
        *ox = (s16)(hq->x - 16);
        *oy = (s16)(hq->y - 16);
    }
}

/* A wall unit of this type to buy, standing, if it can pay. */
static bool wall_unit(Nm *n, int type)
{
    HzNightmare *s = n->s;
    if (n->m->money < cost_of(n, type, ORDER_STAND)) {
        return false;
    }
    s->buy_type = (u8)type;
    s->buy_order = ORDER_STAND;
    s->prod_cargo = NM_CARGO_WALL;
    s->prod_post = 0xFF;
    return true;
}

/* The wall's next unit to buy, if it can pay: SAM-42s and TAX-52s by
 * turns (a TAX-52 where the map sells no SAM-42), standing. */
static bool wall_next(Nm *n, int wall, int sams)
{
    u16 shop = shop_types(n->g);
    if (g_param[P_WALL_GRM] && vs_human(n) && (shop & (1u << UNIT_GRM))
        && (shop & (1u << UNIT_SAM))) {
        /* (session 68) against a human, by thirds: TAX-52s, SAM-42s (the
         * jet) and GRM-34As (his tanks: a SAM-42 shoots only the jet). In
         * The owner's game of 30 October, 21:56 UTC, 20 of its 41
         * standing round its HQ were SAM-42s, and his TAX-52s shot its HQ
         * from 716 HP to nothing */
        if (sams * 3 <= wall) {
            return wall_unit(n, UNIT_SAM);
        }
        return wall_unit(n, wall_grms(n) * 2 <= wall - sams ? UNIT_GRM
                                                             : UNIT_TAX);
    }
    bool sam = (shop & (1u << UNIT_SAM)) && sams * 2 <= wall;
    return wall_unit(n, sam ? UNIT_SAM : UNIT_TAX);
}

#define NM_HOME_WATER 288       /* (session 68) a boat on water this near
                                   its HQ shoots it (its gun 224 px) */

/* (Session 68) Its boats standing within NM_HOME_WATER + 64 of its HQ. */
static int home_boats(const Nm *n)
{
    const HzPost *hq = post(n, own_hq(n));
    int count = 0;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (unit_alive(u) && u->type == UNIT_ST && u->order == ORDER_STAND
            && chebyshev(ucx(u) - hq->x, ucy(u) - hq->y)
               <= NM_HOME_WATER + 64) {
            count++;
        }
    }
    return count;
}

/* (Session 68) Where a boat of the wall goes down (the mech's x, y): the
 * water nearest its HQ within NM_HOME_WATER with no unit of its own
 * within 48 px. */
static bool home_water(Nm *n, s16 *ox, s16 *oy)
{
    const HzPost *hq = post(n, own_hq(n));
    for (int r = 0; r <= NM_HOME_WATER; r += 16) {
        for (int dy = -r; dy <= r; dy += 16) {
            for (int dx = -r; dx <= r; dx += 16) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                int cx = hq->x + dx, cy = hq->y + dy;
                if (!water_at(n, cx - 16, cy - 16)) {
                    continue;
                }
                const int pt[2] = { cx, cy };
                if (any_unit_near(n->g, n->team, cx - 48, cy - 48, cx + 48,
                                  cy + 48, NULL, crowds, pt)) {
                    continue;
                }
                *ox = (s16)(cx - 16);
                *oy = (s16)(cy - 16);
                return true;
            }
        }
    }
    return false;
}

/* (Session 68, P_BOAT_SEEN) Has an enemy boat been on the map? */
static bool boat_seen(Nm *n)
{
    HzNightmare *s = n->s;
    if (!s->boat_seen) {
        int es = 1 - n->team;
        u32 en = side_slots(n->g, es);
        for (u32 i = 0; i < en; i++) {
            const HzUnit *u = unit_at_c(n->g, es, i);
            if (unit_alive(u) && u->type == UNIT_ST) {
                s->boat_seen = true;
                break;
            }
        }
    }
    return s->boat_seen;
}

/* (Session 68) A boat for the wall (P_BOAT_HOME): on a human's pad
 * against the CPU on TYPE D (P_BOAT_TYPE), the map selling them, fewer
 * by its HQ, one for every 4 of the wall, and water by its HQ free;
 * (P_BOAT_SEEN) once an enemy boat has been on the map. On OASE (as P2)
 * the CPU's boats shot its HQ from the lake 185 to 264 px off, out of
 * the garrison's reach, and on D one took it in 2 1/2 minutes. On
 * EISFREI (as P1) the boats came from the wall, the CPU's on the lake
 * 224 to 288 px from its HQ too: worse there on C and D. */
static bool boat_home_wanted(Nm *n, int wall)
{
    if (g_param[P_BOAT_HOME] <= 0 || !cpu_hard(n)
        || n->g->options[1] < g_param[P_BOAT_TYPE]
        || !(shop_types(n->g) & (1u << UNIT_ST))
        || (g_param[P_BOAT_SEEN] && !boat_seen(n))) {
        return false;
    }
    int boats = home_boats(n);
    s16 x, y;
    return boats < g_param[P_BOAT_HOME] && wall >= 4 * (boats + 1)
           && home_water(n, &x, &y);
}

/* (Session 68) Its standing units within 400 px of the enemy HQ. */
static int nest_count(const Nm *n)
{
    const HzPost *e = post(n, enemy_hq(n));
    int count = 0;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (unit_alive(u) && u->order == ORDER_STAND
            && chebyshev(ucx(u) - e->x, ucy(u) - e->y) <= 400) {
            count++;
        }
    }
    return count;
}

/* (Session 68) Where the nest's next unit goes down (the mech's x, y):
 * 224 to 352 px from the enemy HQ, the side facing its own HQ first, no
 * unit of its own within 48 px. */
static void nest_drop(Nm *n, s16 *ox, s16 *oy)
{
    const HzPost *hq = post(n, own_hq(n));
    const HzPost *e = post(n, enemy_hq(n));
    int vx = hq->x - e->x, vy = hq->y - e->y;
    int len = imax(chebyshev(vx, vy), 1);
    long best = 0x7FFFFFFFL;
    bool found = false;
    for (int r = 224; r <= 352; r += 32) {
        for (int dy = -r; dy <= r; dy += 16) {
            for (int dx = -r; dx <= r; dx += 16) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                int cx = e->x + dx, cy = e->y + dy;
                long facing = ((long)dx * vx + (long)dy * vy) / len;
                long score = (r - facing) * 2 + nm_rand(n, 33);
                if (score >= best || !landable(n, cx - 16, cy - 16)) {
                    continue;
                }
                const int pt[2] = { cx, cy };
                if (any_unit_near(n->g, n->team, cx - 48, cy - 48, cx + 48,
                                  cy + 48, NULL, crowds, pt)) {
                    continue;
                }
                best = score;
                *ox = (s16)(cx - 16);
                *oy = (s16)(cy - 16);
                found = true;
            }
        }
    }
    if (!found) {
        attack_drop(n, ox, oy);
    }
}

/* Style 1's buy once the outposts are seen to: the wall's next unit
 * until it has P_WALL, then attack units until half the wall is gone;
 * (session 68) with P_NEST_UNITS, before them GRM-34As (every third a
 * SAM-42) standing round the enemy HQ until it has that many there
 * (The owner's game of 30 October: 69 GRM-34As, most of them put down
 * within 400 px of the server's AI's HQ once its outposts were his). */
static bool wall_buy(Nm *n)
{
    HzNightmare *s = n->s;
    int sams;
    int wall = wall_count(n, &sams);
    if (wall >= g_param[P_WALL]) {
        s->pushing = true;
    } else if (wall < g_param[P_WALL] / 2) {
        s->pushing = false;
    }
    if (!s->pushing) {
        return wall_next(n, wall, sams);
    }
    if (g_param[P_NEST_UNITS] > 0 && nest_count(n) < g_param[P_NEST_UNITS]) {
        int type = (s->nest_buys % 3 == 2) ? UNIT_SAM : UNIT_GRM;
        if (n->m->money < cost_of(n, type, ORDER_STAND)) {
            return false;
        }
        s->nest_buys++;
        s->buy_type = (u8)type;
        s->buy_order = ORDER_STAND;
        s->prod_cargo = NM_CARGO_NEST;
        s->prod_post = 0xFF;
        return true;
    }
    int type = g_param[P_RUSH_TYPE];
    if (n->m->money < cost_of(n, type, ORDER_ATTACK_HQ)) {
        return false;
    }
    s->buy_type = (u8)type;
    s->buy_order = ORDER_ATTACK_HQ;
    s->prod_cargo = NM_CARGO_ATTACK;
    s->prod_post = 0xFF;
    return true;
}

/* -- (session 68) guards where the enemy shot its HQ from --------------- */

/* On ABGRUND a player sent TAX-52s at its HQ one at a time, from two
 * sides, each stopping 130 to 160 px off to shoot it. The mech, ferrying
 * attack units 1,800 px away, came 11 s late and lost the HQ, 926 HP to
 * 0, in seven minutes. Every place an enemy unit shot it from is kept,
 * and TAX-52s are kept there. */

#define NM_SPOT_NEAR 224        /* an attack-HQ unit this near the HQ shot
                                   it */
#define NM_SPOT_BOAT 288        /* (session 68) ... a boat this near (its
                                   gun reaches 224 px) */
#define NM_SPOT_APART 48        /* nearer an old spot: that one again */
#define NM_GUARD_NEAR 64        /* a guard this near a spot is its */

/* Its HQ lost HP since the last think: each enemy attack-HQ unit within
 * NM_SPOT_NEAR a hit at its spot (a new one in the least hit's slot). */
static void learn_spots(Nm *n)
{
    HzNightmare *s = n->s;
    const HzPost *hq = post(n, own_hq(n));
    u16 hp = post_hq_hp(hq);
    bool hit = hp < s->hq_hp_was;
    s->hq_hp_was = hp;
    if (!hit) {
        return;
    }
    s->hq_hit_at = s->frames;
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        int near = (u->type == UNIT_ST) ? NM_SPOT_BOAT : NM_SPOT_NEAR;
        if (!unit_alive(u) || u->order != ORDER_ATTACK_HQ
            || chebyshev(ucx(u) - hq->x, ucy(u) - hq->y) > near) {
            continue;
        }
        int least = 0;
        int k;
        for (k = 0; k < NM_SPOTS; k++) {
            if (s->spot_hits[k] > 0
                && chebyshev(ucx(u) - s->spot_x[k], ucy(u) - s->spot_y[k])
                   < NM_SPOT_APART) {
                break;
            }
            if (s->spot_hits[k] < s->spot_hits[least]) {
                least = k;
            }
        }
        if (k == NM_SPOTS) {
            k = least;
            s->spot_x[k] = (s16)ucx(u);
            s->spot_y[k] = (s16)ucy(u);
            s->spot_hits[k] = 0;
        }
        if (s->spot_hits[k] < 0xFFFF) {
            s->spot_hits[k]++;
        }
    }
}

static int guard_order(void)
{
    return g_param[P_DEFEND_GUARD] ? ORDER_GUARD_NEAR : ORDER_STAND;
}

/* (Session 68) A guard: a TAX-52 on the guards' order, or a boat or a
 * turret (TURRETS) standing. */
static bool is_guard(const HzUnit *u)
{
    if (u->type == UNIT_TAX) {
        return u->order == guard_order();
    }
    return (u->type == UNIT_ST || u->type == UNIT_TURRET1
            || u->type == UNIT_TURRET2) && u->order == ORDER_STAND;
}

/* A gun that reaches a boat from as far as its own does (224 px): the
 * ST-57U's, and TURRET-2's. */
static bool long_gun(int type)
{
    return type == UNIT_ST || type == UNIT_TURRET2;
}

/* Is own unit u a guard within ctx[2] of the point ctx (x, y), with a
 * long gun if ctx[3]? */
static bool guard_near(const HzUnit *u, const void *ctx)
{
    const int *c = ctx;
    return is_guard(u) && (!c[3] || long_gun(u->type))
           && chebyshev(ucx(u) - c[0], ucy(u) - c[1]) <= c[2];
}

static int guards_at(const Nm *n, int x, int y, int reach, bool wet)
{
    const int c[4] = { x, y, reach, wet };
    int count = 0;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (unit_alive(u) && guard_near(u, c)) {
            count++;
        }
    }
    return count;
}

#define NM_GUARD_WET 160        /* (session 68) a long gun this near a spot
                                   on the water is its */

/* (Session 68) A spot out on the water: a boat shot from there. */
static bool spot_wet(const Nm *n, int k)
{
    return mech_ground_at(n->g, n->m, (s16)(n->s->spot_x[k] - 16),
                          (s16)(n->s->spot_y[k] - 16)) == NM_GROUND_WATER;
}

/* The guards that are spot k's. */
static int spot_guards(const Nm *n, int k)
{
    bool wet = spot_wet(n, k);
    return guards_at(n, n->s->spot_x[k], n->s->spot_y[k],
                     wet ? NM_GUARD_WET : NM_GUARD_NEAR, wet);
}

#define NM_SPOT_MOST 3          /* (session 73) guards a spot at most */

/* (Session 73, P_SPOT_MORE) The guards spot k is to have: one; against a
 * human one more for every P_SPOT_MORE times its HQ was hit from there,
 * NM_SPOT_MOST at most. A guard is its spot's within NM_GUARD_NEAR, and
 * with one there a TAX-52 put down 40 px from it still shot the HQ till
 * it died; three by a spot leave a jet little room to put one down (in
 * the strip north of P2's HQ on ABGRUND, none). */
static int spot_want(const Nm *n, int k)
{
    if (g_param[P_SPOT_MORE] <= 0 || !vs_human(n) || spot_wet(n, k)) {
        return 1;
    }
    return imin(1 + n->s->spot_hits[k] / g_param[P_SPOT_MORE], NM_SPOT_MOST);
}

/* The guards spot k has over those it is to have (under: negative). */
static int spot_over(const Nm *n, int k)
{
    return spot_guards(n, k) - spot_want(n, k);
}

/* What to buy as spot k's guard, -1 nothing the map sells: on the water
 * TURRET-2 (TURRETS) on the shore, else a boat; on land TURRET-1, else
 * a TAX-52. */
static int guard_type_for(const Nm *n, int k)
{
    u16 shop = shop_types(n->g);
    if (spot_wet(n, k)) {
        if (shop & (1u << UNIT_TURRET2)) {
            return UNIT_TURRET2;
        }
        return (shop & (1u << UNIT_ST)) ? UNIT_ST : -1;
    }
    if (shop & (1u << UNIT_TURRET1)) {
        return UNIT_TURRET1;
    }
    return (shop & (1u << UNIT_TAX)) ? UNIT_TAX : -1;
}

/* (Session 68, P_SPOT_WET) Is spot k on the water with no long gun of
 * its own reaching it? */
static bool spot_wet_open(const Nm *n, int k)
{
    return g_param[P_SPOT_WET] && n->s->spot_hits[k] > 0
           && guard_type_for(n, k) >= 0 && spot_wet(n, k)
           && spot_guards(n, k) == 0;
}

/* The spot the next guard is for: of those it can buy one for, the one
 * with the fewest ((session 73) for those it is to have, spot_want), of
 * those the most hit ((s. 68) one on the water no long gun reaches
 * first); -1 none. */
static int guard_spot(const Nm *n)
{
    const HzNightmare *s = n->s;
    int best = -1, best_guards = 0;
    bool best_open = false;
    for (int k = 0; k < NM_SPOTS; k++) {
        if (s->spot_hits[k] == 0 || guard_type_for(n, k) < 0) {
            continue;
        }
        int g = spot_over(n, k);
        bool open = spot_wet_open(n, k);
        if (best < 0 || (open && !best_open)
            || (open == best_open
                && (g < best_guards
                    || (g == best_guards
                        && s->spot_hits[k] > s->spot_hits[best])))) {
            best = k;
            best_guards = g;
            best_open = open;
        }
    }
    return best;
}

/* (Session 68) Own guards at the spots known (P_SPOT_COUNT). */
static int spots_guarded(const Nm *n)
{
    int count = 0;
    for (int k = 0; k < NM_SPOTS; k++) {
        if (n->s->spot_hits[k] > 0) {
            count += spot_guards(n, k);
        }
    }
    return count;
}

/* Its HQ lost HP in the last 10 s. */
static bool hq_pressed(const Nm *n)
{
    return n->s->hq_hit_at != 0 && n->s->frames - n->s->hq_hit_at < 600;
}

/* A guard to buy (not style 1): P_DEFEND, a spot known, fewer guards
 * round the HQ; its HQ being shot, one more for each P_DEFEND_RICH of
 * its money. On
 * every map the original's AI won against it (on a human's pad) with
 * 10,000 to 24,000 of its money unspent: its mech fought the stream of
 * units at its HQ all game, one attack unit waiting to go. */
static bool defend_wanted(const Nm *n)
{
    if (g_param[P_DEFEND] <= 0 || g_param[P_STYLE] == 1) {
        return false;
    }
    int k = guard_spot(n);
    if (k < 0) {
        return false;
    }
    int want = g_param[P_DEFEND];
    if (hq_pressed(n) && g_param[P_DEFEND_RICH] > 0) {
        want = imin(NM_SPOTS * 2, want + n->m->money / g_param[P_DEFEND_RICH]);
    }
    const HzPost *hq = post(n, own_hq(n));
    /* (session 68) P_SPOT_COUNT: only those at the spots. On OASE (TYPE
     * D, as P2) the wall's 20 TAX-52s, standing 96-167 px out, counted,
     * and none was bought while CPU units shot its HQ from 110-260 px on
     * the side the wall wasn't */
    int all = g_param[P_SPOT_COUNT]
              ? spots_guarded(n)
              : guards_at(n, hq->x, hq->y, NM_SPOT_BOAT + NM_GUARD_WET, false);
    /* (session 68) or a spot none of them reaches (the TAX-52s ashore
     * on STRAND, a boat out of their reach), up to one more a spot;
     * (P_SPOT_HUMAN) against a human, any. In the owner's game of 30
     * October, 21:56 UTC (ABGRUND, TYPE A), its garrison of 40 stood on
     * the side facing his HQ while his TAX-52s, put down 96 to 156 px
     * behind its HQ by the map's edge, shot it from 716 HP to 36: of the
     * four spots it learnt, one was guarded */
    if (spot_over(n, k) < 0 && g_param[P_SPOT_HUMAN] && vs_human(n)) {
        return true;        /* (session 73) spot_want: more for one hit
                               again and again */
    }
    if (spot_wet_open(n, k)) {
        /* (session 68) a boat's spot no long gun reaches: on OASE (TYPE
         * D, as P2) the CPU's boats shot its HQ from the water 230 to
         * 290 px west of it, the 26 to 36 of its wall counted as guards,
         * and no boat or TURRET-2 was bought in 38 minutes; its mech died
         * 54 times fighting them, $20,000 unspent */
        return true;
    }
    return all < want || (spot_guards(n, k) == 0 && all < want + NM_SPOTS);
}

/* (Session 68) Can a boat be put down with the mech at (x, y)? */
static bool water_at(Nm *n, int x, int y)
{
    return on_map(x, y)
           && mech_ground_at(n->g, n->m, (s16)x, (s16)y) == NM_GROUND_WATER
           && !mech_land_blocked_at(n->g, n->m, (s16)x, (s16)y);
}

/* The place nearest (x, y) (mech coordinates) within `reach` px (8 px
 * steps) a boat can be put down. */
static bool water_near(Nm *n, int x, int y, int reach, s16 *ox, s16 *oy)
{
    for (int r = 0; r <= reach; r += 8) {
        for (int dy = -r; dy <= r; dy += 8) {
            for (int dx = -r; dx <= r; dx += 8) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                if (water_at(n, x + dx, y + dy)) {
                    *ox = (s16)(x + dx);
                    *oy = (s16)(y + dy);
                    return true;
                }
            }
        }
    }
    return false;
}

/* Can the carried unit go down with the mech at (x, y)? A boat on the
 * water, anything else on land. */
static bool droppable(Nm *n, int x, int y)
{
    if ((n->m->flags & MECH_CARRYING)
        && (carried_unit(n->g, n->m)->flags & UNIT_BOAT)) {
        return water_at(n, x, y);
    }
    return landable(n, x, y);
}

/* Where the next guard goes down (the mech's x, y): at the spot with the
 * fewest guards of its kind (a long gun for one on the water, (s. 68)
 * none but a boat's on the water for a boat), of those the most hit;
 * the nearest place it can. A boat between the enemy's and its HQ, 96
 * px from the enemy's, if there's water there. */
static void guard_drop(Nm *n, s16 *ox, s16 *oy)
{
    const HzNightmare *s = n->s;
    int type = carried_unit(n->g, n->m)->type;
    int best = -1, best_guards = 0;
    for (int k = 0; k < NM_SPOTS; k++) {
        if (s->spot_hits[k] == 0
            || (spot_wet(n, k) ? !long_gun(type) : type == UNIT_ST)) {
            continue;
        }
        int g = spot_over(n, k);
        if (best < 0 || g < best_guards
            || (g == best_guards && s->spot_hits[k] > s->spot_hits[best])) {
            best = k;
            best_guards = g;
        }
    }
    const HzPost *hq = post(n, own_hq(n));
    if (type == UNIT_ST) {
        if (best >= 0) {
            int sx = s->spot_x[best], sy = s->spot_y[best];
            int len = imax(chebyshev(hq->x - sx, hq->y - sy), 1);
            int px = sx + (hq->x - sx) * 96 / len;
            int py = sy + (hq->y - sy) * 96 / len;
            if (water_near(n, px - 16, py - 16, 128, ox, oy)
                || water_near(n, sx - 16, sy - 16, 192, ox, oy)) {
                return;
            }
        }
        if (water_near(n, hq->x - 16, hq->y - 16, 400, ox, oy)) {
            return;
        }
    } else if (best >= 0 && lanes_on(n) && !spot_wet(n, best)
               && lane_near(n, s->spot_x[best], s->spot_y[best],
                            NM_GUARD_NEAR - 8, ox, oy)) {
        /* (session 75, P_HOME_LANES) the free block place nearest the
         * spot, near enough to be its guard (spot_guards: the mech puts
         * a unit down up to 6 px off); none, as before. With 96 px, on
         * EISFREI (its HQ in a corner) guards went down where they were
         * no spot's, and it bought 82 a game for 44, and 13 wall units
         * for 37. */
        n->s->lane_drops++;
        return;
    } else if (best >= 0
               && landable_near(n, s->spot_x[best] - 16, s->spot_y[best] - 16,
                                spot_wet(n, best) ? 192 : 64, ox, oy)) {
        return;
    }
    wall_drop(n, ox, oy);
}

/* -- (session 68) attacks in a group ------------------------------------ */

/* One at a time, its attack units met whatever guarded the enemy HQ alone
 * and died there one after another (a player's wall of standing SAM-42s
 * and TAX-52s, hz_arena's turtle). With P_GROUP they are bought standing
 * and put down at a rally point out of the HQ's missiles; with P_GROUP
 * there, each in turn is lifted, given attack HQ in the order menu
 * (carrying, the order's fee) and put down by the first sent, so they go
 * in together. Off (0): it lost, every way tried -- the enemy mech picked
 * off the units waiting (docs/nightmare.md, "Attacks in a group"). */

/* Its gathered units, standing within P_RALLY_R of the rally point: how
 * many; the one nearest the mech in *nearest (NULL none). */
static int group_count(const Nm *n, const HzUnit **nearest)
{
    const HzNightmare *s = n->s;
    *nearest = NULL;
    if (!s->rally_set) {
        return 0;
    }
    int rx = s->rally_x + 16, ry = s->rally_y + 16;
    int count = 0, best_d = 0x7FFF;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (!unit_alive(u) || u->order != ORDER_STAND
            || u->type != g_param[P_RUSH_TYPE]
            || chebyshev(ucx(u) - rx, ucy(u) - ry) > g_param[P_RALLY_R]) {
            continue;
        }
        count++;
        int d = chebyshev(ucx(u) - mx(n), ucy(u) - my(n));
        if (d < best_d) {
            best_d = d;
            *nearest = u;
        }
    }
    return count;
}

/* Is the rally point still one: out of missiles, a unit carried there
 * from a base? */
static bool rally_good(Nm *n)
{
    const HzNightmare *s = n->s;
    return s->rally_set
           && !(danger_at(n, s->rally_x + 16, s->rally_y + 16)
                & DANGER_MISSILE)
           && base_reach(n, s->rally_x, s->rally_y);
}

/* The rally point: kept while it is good; gone bad, none until those
 * there have gone in (launch_next); then the attack drop with the
 * shortest drive in that a unit is carried to from a base, P_RALLY from
 * the HQ at least. False if there is none. */
static bool rally_ok(Nm *n)
{
    HzNightmare *s = n->s;
    if (rally_good(n)) {
        return true;
    }
    const HzUnit *u;
    if (group_count(n, &u) > 0) {
        return false;
    }
    s16 x, y;
    if (!attack_drop_at(n, NM_REACH_BASE, g_param[P_RALLY], &x, &y, NULL,
                        NULL)) {
        return false;
    }
    s->rally_x = x;
    s->rally_y = y;
    s->rally_set = true;
    return true;
}

/* Is one of its own units within ctx[2] px of ctx (x, y)? */
static bool own_within(const HzUnit *u, const void *ctx)
{
    const int *c = ctx;
    return chebyshev(ucx(u) - c[0], ucy(u) - c[1]) < c[2];
}

/* Where a unit goes down by (cx, cy) (centres): the plain land nearest
 * it within `reach`, out of missiles, `gap` px from its own units; false
 * if none. */
static bool unit_spot(Nm *n, int cx, int cy, int reach, int gap, s16 *ox,
                      s16 *oy)
{
    for (int r = 0; r <= reach; r += 16) {
        for (int dy = -r; dy <= r; dy += 16) {
            for (int dx = -r; dx <= r; dx += 16) {
                if (imax(iabs(dx), iabs(dy)) != r) {
                    continue;
                }
                int x = cx + dx, y = cy + dy;
                if (!fight_landable(n, x - 16, y - 16)
                    || (danger_at(n, x, y) & DANGER_MISSILE)) {
                    continue;
                }
                const int c[3] = { x, y, gap };
                if (any_unit_near(n->g, n->team, x - gap, y - gap, x + gap,
                                  y + gap, NULL, own_within, c)) {
                    continue;
                }
                *ox = (s16)(x - 16);
                *oy = (s16)(y - 16);
                return true;
            }
        }
    }
    return false;
}

/* Where a gathered unit goes down: by the rally point. */
static void rally_drop(Nm *n, s16 *ox, s16 *oy)
{
    HzNightmare *s = n->s;
    if (!rally_ok(n)) {
        attack_drop(n, ox, oy);
        return;
    }
    if (!unit_spot(n, s->rally_x + 16, s->rally_y + 16,
                   g_param[P_RALLY_R] - 16, 32, ox, oy)) {
        *ox = s->rally_x;
        *oy = s->rally_y;
    }
}

/* Where a unit sent from the rally point goes down: by the first of its
 * group sent, while that one is out of missiles and not far; else where
 * the mech is. */
static void launch_drop(Nm *n, s16 *ox, s16 *oy)
{
    const HzUnit *lead = ref_unit_ptr_c(n->g, n->s->lead);
    if (lead != NULL && unit_alive(lead) && lead->order == ORDER_ATTACK_HQ
        && chebyshev(ucx(lead) - mx(n), ucy(lead) - my(n)) < 320
        && !(danger_at(n, ucx(lead), ucy(lead)) & DANGER_MISSILE)
        && unit_spot(n, ucx(lead), ucy(lead), 64, 24, ox, oy)) {
        return;
    }
    if (!unit_spot(n, mx(n), my(n), 96, 24, ox, oy)) {
        *ox = n->m->x;
        *oy = n->m->y;
    }
}

/* The group's next unit to send, if it is time (P_GROUP gathered, or the
 * rally point gone bad) and it can pay the order: to NM_TASK_LIFT. */
static bool launch_next(Nm *n)
{
    HzNightmare *s = n->s;
    if (g_param[P_GROUP] < 2) {
        return false;
    }
    const HzUnit *u;
    int count = group_count(n, &u);
    if (s->launching && count == 0) {
        s->launching = false;
        s->rally_set = false;       /* the next group's: chosen afresh */
    }
    if (!s->launching && count > 0
        && (count >= g_param[P_GROUP] || !rally_good(n))) {
        s->launching = true;
        s->lead = ref_none();
        s->groups++;
    }
    if (!s->launching || u == NULL
        || n->m->money < n->g->assets->orders[ORDER_ATTACK_HQ].fee) {
        return false;
    }
    s->lift = unit_ref(u);
    s->reordered = false;
    set_task(n, NM_TASK_LIFT);
    return true;
}

/* To the gathered unit `lift`, A over it (the jet lifts the own unit
 * whose x + 8, y + 8 is in [mech, mech + $30 - its size) both ways). */
static void task_lift(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    if (m->flags & MECH_CARRYING) {
        s->cargo = s->rescuing ? NM_CARGO_ATTACK
                   : s->rehunting ? NM_CARGO_HUNT
                   : s->retrucking ? NM_CARGO_SUPPLY : NM_CARGO_LAUNCH;
        s->cargo_post = 0xFF;
        s->retrucking = false;
        if (s->rescuing) {
            s->rescuing = false;
            s->rescued++;
        }
        if (s->rehunting) {
            s->rehunting = false;
            s->rehunts++;
        }
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    const HzUnit *u = ref_unit_ptr_c(n->g, s->lift);
    if (s->age > 900 && !s->rescuing && !s->rehunting && !s->retrucking) {
        /* it can't: the rest stand where they are */
        s->launching = false;
        s->rally_set = false;
    }
    int order = s->rescuing ? ORDER_ATTACK_HQ
                : s->rehunting ? ORDER_GUARD_NEAR : ORDER_STAND;
    if (s->retrucking && u != NULL) {
        /* (session 75) a truck keeps its own */
        order = (u->type == UNIT_SUPPLY) ? ORDER_SUPPLY : ORDER_REPAIR;
    }
    if (u == NULL || !unit_alive(u) || u->order != order || s->age > 900) {
        s->rescuing = false;
        s->rehunting = false;
        s->retrucking = false;
        set_task(n, NM_TASK_DECIDE);
        return;
    }
    if (!airborne(n)) {
        return;
    }
    int reach = 0x30 - u->size;
    int tx = u->x + 8 - reach / 2, ty = u->y + 8 - reach / 2;
    fly_to(n, tx, ty, 2);
    int ix = u->x + 8 - m->x, iy = u->y + 8 - m->y;
    if (ix >= 2 && ix < reach - 2 && iy >= 2 && iy < reach - 2
        && m->form == HZ_FORM_JET && slow(m)) {
        press(n, HZ_PAD_A);
    }
}

/* (Session 68) The standing units it keeps round its HQ: P_GARRISON, or
 * more (P_GARRISON_CPU) on a human's pad against the CPU on TYPE C and D,
 * where the CPU's shots hit x 1.5 and x 2 and its HQ takes less: one of
 * its TAX-52s put down by the HQ took it in a minute. (Session 68 (12))
 * 40 there, so no attack unit for a long while: on D its GRM-34As round
 * its HQ kill them in a shot or two, and the mech ferrying them is away
 * when the CPU's tanks come (The owner: "Go after main base when
 * oportunity arises"). All maps, both seats, 4 a set: D 5-14-45 from
 * 13-31-20, C 18-22-24. */
/* (Session 68) On a human's pad against a human (online). */
static bool vs_human(const Nm *n)
{
    return !n->cpu && !(n->rival->flags & MECH_CPU);
}

/* (Session 68) The enemy's standing armed units within 400 px of its
 * HQ: its wall. */
static int enemy_wall(const Nm *n)
{
    const HzPost *e = post(n, enemy_hq(n));
    int count = 0;
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (unit_alive(u) && u->order == ORDER_STAND
            && u->type != UNIT_SUPPLY && u->type != UNIT_INFANTRY
            && chebyshev(ucx(u) - e->x, ucy(u) - e->y) <= 400) {
            count++;
        }
    }
    return count;
}

/* (Session 68, P_FUTILE) Its attack units going in for nothing. In
 * The owner's game of 31 October (VULKAN, the server's AI P1) 25 went
 * in one at a time from minute 24 and died at his wall, his HQ shot no
 * more, its money gone on them, he holding 6 and 7 outposts to its 3;
 * its garrison waited on its HQ being shot, and at 39 minutes TAX-52s
 * put down from the outpost 450 px north of it took it in under 2
 * minutes, nothing standing by it. Without P_FUTILE_TIME and
 * P_FUTILE_BEHIND, against the turtle with nest_units 6 on EISFREI it
 * stopped attacks that won there through 8 to 15 losses in a row: 6-5-5
 * from 16-0. */
static bool futile(const Nm *n)
{
    const HzNightmare *s = n->s;
    if (g_param[P_FUTILE] <= 0 || s->att_lost < g_param[P_FUTILE]
        || s->frames - s->ehq_hit_at < (u32)g_param[P_FUTILE_TIME]) {
        return false;
    }
    if (g_param[P_FUTILE_BEHIND]) {
        int theirs = 0;
        for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
            theirs += post_theirs(n, i);
        }
        return theirs > own_outposts(n);
    }
    return true;
}

static int garrison_size(const Nm *n)
{
    int size = g_param[P_GARRISON];
    if (cpu_hard(n)) {
        size = imax(size, g_param[P_GARRISON_CPU]);
    }
    if (vs_human(n)
        && (!g_param[P_GARRISON_SHOT] || n->s->hq_hit_at != 0)) {
        /* (session 68) P_GARRISON_SHOT: once its HQ has been shot */
        size = imax(size, g_param[P_GARRISON_HUMAN]);
    }
    if (vs_human(n) && futile(n)) {
        size = imax(size, g_param[P_FUTILE_SIZE]);
    }
    if (g_param[P_GARRISON_MATCH] > 0 && !n->cpu) {
        /* (session 68) a wall as big as the enemy's, near enough: a
         * turtle of 50 standing (hz_arena, ABGRUND, TYPE A, as P2) took
         * its HQ 8 games of 8 while its attack units died at the wall
         * one by one; 40 at home: 8 draws */
        size = imax(size, imin(g_param[P_GARRISON_CAP],
                               enemy_wall(n) * g_param[P_GARRISON_MATCH]
                               / 100));
    }
    return size;
}

/* A guard for the spot guard_spot gives (one known). */
static bool guard_buy(Nm *n)
{
    HzNightmare *s = n->s;
    int type = guard_type_for(n, guard_spot(n));
    int order = (type == UNIT_TAX) ? guard_order() : ORDER_STAND;
    if (n->m->money < cost_of(n, type, order)) {
        return false;
    }
    s->buy_type = (u8)type;
    s->buy_order = (u8)order;
    s->prod_cargo = NM_CARGO_GUARD;
    s->prod_post = 0xFF;
    return true;
}

/* -- (session 68) hunting the enemy's SAM-42s ---------------------------- */

/* A SAM-42 shoots only the jet, so a TAX-52 by one shoots it unharmed,
 * and a nest of them shuts the jet out of what their missiles reach: its
 * outposts, its HQ, the drops by the enemy HQ. The owner: "a Nest of
 * Sam's doesn't shoot at ground units, only GRM do. You should be
 * discovering strategies for killing troublesome units", and "To reclaim
 * an area swarmed with Sams can be very important." An attack unit
 * drives the map's way past them, 80 px off, without firing (it sees 64
 * px), so against a human (P_HUNT) a hunter is bought: a TAX-52 told to
 * guard where it is put down (guard near: it goes for an enemy it sees
 * within 128 px of there, and back), 32 to 64 px from the enemy SAM-42
 * whose missiles cover the most of what it needs, where no enemy gun
 * reaches and fewer than P_HUNT_COVER missile units do. A SAM-42 is worth
 * 4 if it reaches its HQ, 2 each outpost of its, 1 if it stands within
 * P_HUNT_NEAR of the enemy HQ. A first try put them 64 to 112 px off a
 * SAM-42 near the enemy HQ only, and set out with one out of reach it put
 * down short: on STADT one stood 190 px from the nearest for two hours
 * and kept the next from being bought. Put down 48 to 96 px off, they
 * chased the SAM-42s they saw and ran dry in a minute (a TAX-52's tank
 * drives 11 s), then stood 77 px from the next, out of sight. */

#define NM_HUNT_ENGAGED 160     /* a hunter with an enemy SAM-42 this near
                                   is at work */
#define NM_HUNT_SIGHT   72      /* ... this near: in its sight */

/* Its HQ not shot in the last minute. */
static bool hq_calm(const Nm *n)
{
    return n->s->hq_hit_at == 0 || n->s->frames - n->s->hq_hit_at >= 3600;
}

/* What enemy SAM-42 u's missiles keep it from (0 nothing it needs). */
static int sam_worth(const Nm *n, const HzUnit *u)
{
    int x = u->x + 16, y = u->y + 16;   /* as danger_build marks it */
    int reach = 256 + 16;
    const HzPost *hq = post(n, own_hq(n));
    const HzPost *e = post(n, enemy_hq(n));
    int worth = 0;
    if (chebyshev(x - hq->x, y - hq->y) <= reach) {
        worth += 4;
    }
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        if (post_ours(n, i)
            && chebyshev(x - post(n, i)->x, y - post(n, i)->y) <= reach) {
            worth += 2;
        }
    }
    if (chebyshev(ucx(u) - e->x, ucy(u) - e->y) <= g_param[P_HUNT_NEAR]) {
        worth += 1;
    }
    if (g_param[P_HUNT_STRIKE] && worth < 2 && vs_human(n)
        && base_dist(n, own_hq(n), ucx(u), ucy(u)) <= g_param[P_STRIKE_R]) {
        /* (session 77) the nest it would strike at (pick_strike). In
         * The owner's game of 19 November (EISFREI, the server's AI P1)
         * seven of his SAM-42s, a SUPPLY and a repair truck stood round
         * outpost 3, 500 to 640 px from its HQ, by the GRM-34As over his
         * boats: taken over at 12:00 with his side idle, it struck 15.5
         * times a game for 2.1 kills, was destroyed 24.1 times, sent 0.5
         * hunters, and 2.9 of the seven were left at the end. A SAM-42
         * has no gun: a TAX-52 by it takes it in four shots, and missiles
         * don't hit a TAX-52 */
        worth = 2;
    }
    if (g_param[P_HUNT_TAKE] && own_outposts(n) < capture_min(n)
        && own_outposts(n) < g_param[P_CAPTURE_MAX]) {
        /* (session 68, P_HUNT_TAKE) an enemy outpost it would take: in
         * The owner's game of 31 October two SAM-42s held his by its
         * north-east 18 minutes, five and a SUPPLY the next */
        for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
            const HzPost *pp = post(n, i);
            if (post_theirs(n, i)
                && chebyshev(x - pp->x, y - pp->y) <= reach
                && chebyshev(pp->x - e->x, pp->y - e->y)
                   >= g_param[P_CAPTURE_NEAR]
                && base_dist(n, nearest_base(n, pp->x, pp->y), pp->x, pp->y)
                   <= g_param[P_CAPTURE_REACH]) {
                worth += 2;
                break;
            }
        }
    }
    return worth;
}

/* Its hunters at work: own TAX-52s guarding with an enemy SAM-42 within
 * NM_HUNT_ENGAGED px; *home: of those, ones by a SAM-42 worth 2 or more
 * (its own HQ's or outposts'). */
static int hunters_out(const Nm *n, int *home)
{
    int es = 1 - n->team;
    int count = 0;
    *home = 0;
    u32 on = side_slots(n->g, n->team);
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *h = unit_at_c(n->g, n->team, i);
        if (!unit_alive(h) || h->type != UNIT_TAX
            || h->order != ORDER_GUARD_NEAR) {
            continue;
        }
        int worth = -1;
        for (u32 j = 0; j < en; j++) {
            const HzUnit *u = unit_at_c(n->g, es, j);
            if (unit_alive(u) && u->type == UNIT_SAM
                && chebyshev(ucx(u) - ucx(h), ucy(u) - ucy(h))
                   <= NM_HUNT_ENGAGED) {
                worth = imax(worth, sam_worth(n, u));
            }
        }
        if (worth >= 0) {
            count++;
            if (worth >= 2) {
                (*home)++;
            }
        }
    }
    return count;
}

/* Where a hunter goes down (the mech's x, y), in the fuel's reach from
 * where the mech is (or, !from_mech, anywhere), *worth its SAM-42's:
 * false if none. */
static bool hunt_drop_at(Nm *n, bool from_mech, s16 *ox, s16 *oy,
                         int *worth_out)
{
    int es = 1 - n->team;
    long best = 0x7FFFFFFFL;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (!unit_alive(u) || u->type != UNIT_SAM) {
            continue;
        }
        int worth = sam_worth(n, u);
        if (worth < (g_param[P_HUNT] > 0 ? 1 : 2)) {
            continue;                   /* P_HUNT_HOME alone: its own */
        }
        for (int r = 32; r <= 64; r += 16) {
            for (int dy = -r; dy <= r; dy += 16) {
                for (int dx = -r; dx <= r; dx += 16) {
                    if (imax(iabs(dx), iabs(dy)) != r) {
                        continue;
                    }
                    int px = ucx(u) + dx - 16, py = ucy(u) + dy - 16;
                    int bx = (px + 16) >> 5, by = (py + 16) >> 5;
                    if (bx < 0 || by < 0 || bx > 63 || by > 63) {
                        continue;
                    }
                    int cover = n->missiles[by][bx];
                    long score = (long)cover * 256 + r - worth * 128
                                 + chebyshev(px - n->m->x, py - n->m->y) / 16;
                    if (score >= best || cover >= g_param[P_HUNT_COVER]
                        || (n->danger[by][bx] & DANGER_GUN)
                        || !landable(n, px, py)
                        || mech_ground_at(n->g, n->m, (s16)px, (s16)py) != 0
                        || (from_mech
                            && !leg_ok(n, px, py, g_param[P_FUEL_MARGIN]))) {
                        continue;
                    }
                    best = score;
                    *ox = (s16)px;
                    *oy = (s16)py;
                    *worth_out = worth;
                }
            }
        }
    }
    return best != 0x7FFFFFFFL;
}

/* An idle hunter of its (one it put down, guarding still, no enemy unit
 * within NM_HUNT_ENGAGED px, or none in sight and its tank a quarter
 * full or less), the nearest the mech not under P_HUNT_COVER missiles:
 * NULL none. */
static const HzUnit *hunter_idle(const Nm *n)
{
    const HzNightmare *s = n->s;
    const HzUnit *best = NULL;
    int best_d = 0x7FFFFFFF;
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (int k = 0; k < NM_HUNTERS; k++) {
        const HzUnit *h = ref_unit_ptr_c(n->g, s->hunter[k]);
        if (h == NULL || !unit_alive(h) || h->type != UNIT_TAX
            || h->order != ORDER_GUARD_NEAR || (h->flags & UNIT_CARRIED)) {
            continue;
        }
        bool low = h->fuel * 4 <= unit_stats_for(n->g, h->type)->fuel;
        int reach = low ? NM_HUNT_SIGHT : NM_HUNT_ENGAGED;
        bool busy = false;
        for (u32 j = 0; j < en && !busy; j++) {
            const HzUnit *u = unit_at_c(n->g, es, j);
            busy = unit_alive(u)
                   && chebyshev(ucx(u) - ucx(h), ucy(u) - ucy(h)) <= reach;
        }
        int bx = clamp63(ucx(h) >> 5), by = clamp63(ucy(h) >> 5);
        if (busy || n->missiles[by][bx] >= g_param[P_HUNT_COVER]) {
            continue;
        }
        int d = chebyshev(h->x - n->m->x, h->y - n->m->y);
        if (d < best_d) {
            best_d = d;
            best = h;
        }
    }
    return best;
}

/* A hunter to buy now? Every P_HUNT-th attack unit (or, home, before the
 * garrison: one for a SAM-42 covering its own HQ or outposts while none
 * is at one), P_HUNT_MAX at work at most, none while one stands idle to
 * be lifted on. */
static bool hunt_wanted(Nm *n, bool home_only)
{
    if ((g_param[P_HUNT] <= 0 && !g_param[P_HUNT_HOME]) || !vs_human(n)
        || !(shop_types(n->g) & (1u << UNIT_TAX))) {
        return false;
    }
    int home;
    if (hunters_out(n, &home) >= g_param[P_HUNT_MAX]
        || (g_param[P_REHUNT] && hunter_idle(n) != NULL)) {
        return false;
    }
    s16 x, y;
    int worth = 0;
    if (!hunt_drop_at(n, false, &x, &y, &worth)) {
        return false;
    }
    if (worth >= 2 && home < imax(1, g_param[P_HUNT_HOME]) && hq_calm(n)) {
        return true;       /* (session 77) HUNT_HOME 2: two at a time */
    }
    return !home_only && g_param[P_HUNT] > 0
           && n->s->since_hunt + 1 >= g_param[P_HUNT];
}

/* (Session 68, P_REHUNT) An idle hunter lifted, to be put down by the
 * next SAM-42: to NM_TASK_LIFT. Two hunters on the SAM-42s by its east
 * outpost in the owner's game of 30 October, 10:43 (taken over at 18
 * minutes) killed 7 of his 11 there, then stood idle the 30 minutes
 * after. Not while its HQ was shot in the last minute: there the jet
 * flew 1,000 px off to lift one between the turtle's drops at its HQ,
 * and lost it (6 of 8 seeds). */
static bool rehunt_next(Nm *n)
{
    HzNightmare *s = n->s;
    if ((g_param[P_HUNT] <= 0 && !g_param[P_HUNT_HOME]) || !g_param[P_REHUNT]
        || !vs_human(n)
        || n->m->hp < kidou(n)->stats.hp / 2 || !hq_calm(n)) {
        return false;
    }
    const HzUnit *h = hunter_idle(n);
    s16 x, y;
    int worth;
    if (h == NULL || !hunt_drop_at(n, false, &x, &y, &worth)) {
        return false;
    }
    s->lift = unit_ref(h);
    s->rehunting = true;
    set_task(n, NM_TASK_LIFT);
    return true;
}

/* As choose_buy, a hunter (P_HUNT). */
static bool buy_hunter(Nm *n)
{
    HzNightmare *s = n->s;
    if (n->m->money < cost_of(n, UNIT_TAX, ORDER_GUARD_NEAR)) {
        return false;
    }
    s->buy_type = UNIT_TAX;
    s->buy_order = ORDER_GUARD_NEAR;
    s->prod_cargo = NM_CARGO_HUNT;
    s->prod_post = 0xFF;
    s->since_hunt = 0;
    return true;
}

/* -- (session 68) guards by its outposts --------------------------------- */

/* The owner: "It never garisons Outposts." In his games of 31 October
 * (VULKAN, the server's AI P1) he took four of its outposts with INFANTRY
 * between minutes 20 and 42, nothing of its standing by them, and held
 * them to the end. Tried, off (P_POST_GUARD): a TAX-52 on guard by each,
 * taken over with the turtle for the owner, at 33 minutes in his 18:31
 * game it lost 6 of 16 (none without): the jet took a guard to its
 * outpost 1,150 px off while the turtle's drops came in at its HQ; only
 * while its HQ was calm, 6 again; after its garrison only, 1, and 0-1-15
 * in his 09:35 game at 24 minutes and 1-0-15 at 29 in his 18:31 one (0-0-16
 * and 3-0-13), holding 1.8 outposts at the end to 4.4: the money went on
 * guards, not INFANTRY. */

#define NM_POST_GUARD_NEAR 128  /* an own armed unit standing this near an
                                   outpost guards it */

/* Own armed units standing or guarding within NM_POST_GUARD_NEAR of
 * outpost i. */
static int post_guards_own(const Nm *n, int i)
{
    const HzPost *pp = post(n, i);
    int count = 0;
    u32 on = side_slots(n->g, n->team);
    for (u32 k = 0; k < on; k++) {
        const HzUnit *u = unit_at_c(n->g, n->team, k);
        if (unit_alive(u) && !(u->flags & UNIT_CARRIED) && gun_reach(u) > 0
            && (u->order == ORDER_STAND || u->order == ORDER_GUARD_NEAR)
            && chebyshev(ucx(u) - pp->x, ucy(u) - pp->y)
               <= NM_POST_GUARD_NEAR) {
            count++;
        }
    }
    return count;
}

/* (P_POST_GUARD) The own outpost the next guard is for, fewer than `want`
 * by it, out of the enemy's missiles: the fewest, of those the nearest the
 * enemy HQ; -1 none. */
static int post_guard_post(const Nm *n, int want)
{
    if (want <= 0 || !vs_human(n) || g_param[P_STYLE] == 1
        || !(shop_types(n->g) & (1u << UNIT_TAX))
        || (g_param[P_POST_GUARD_CALM] && !hq_calm(n))) {
        return -1;
    }
    const HzPost *e = post(n, enemy_hq(n));
    int best = -1, best_g = want, best_d = 0x7FFF;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        const HzPost *pp = post(n, i);
        if (!(pp->flags & POST_USED) || !post_ours(n, i)
            || (n->danger[clamp63(pp->y >> 5)][clamp63(pp->x >> 5)]
                & DANGER_MISSILE)) {
            continue;
        }
        int g = post_guards_own(n, i);
        int d = chebyshev(pp->x - e->x, pp->y - e->y);
        if (g < best_g || (g == best_g && best >= 0 && d < best_d)) {
            best = i;
            best_g = g;
            best_d = d;
        }
    }
    return best;
}

/* As choose_buy, a guard for outpost i. */
static bool buy_post_guard(Nm *n, int i)
{
    HzNightmare *s = n->s;
    if (n->m->money < cost_of(n, UNIT_TAX, ORDER_GUARD_NEAR)) {
        return false;
    }
    s->buy_type = UNIT_TAX;
    s->buy_order = ORDER_GUARD_NEAR;
    s->prod_cargo = NM_CARGO_POST;
    s->prod_post = (u8)i;
    return true;
}

/* (Session 69, P_POST_RAID) The enemy's base nearest outpost i: where
 * its raids come from. */
static int raid_from(const Nm *n, int i)
{
    const HzPost *pp = post(n, i);
    int best = enemy_hq(n);
    int best_d = chebyshev(post(n, best)->x - pp->x, post(n, best)->y - pp->y);
    for (int j = 0; j < HZ_POST_OUTPOSTS; j++) {
        if (!post_theirs(n, j)) {
            continue;
        }
        int d = chebyshev(post(n, j)->x - pp->x, post(n, j)->y - pp->y);
        if (d < best_d) {
            best_d = d;
            best = j;
        }
    }
    return best;
}

/* (Session 69, P_POST_RAID) Each outpost of its own the enemy's INFANTRY
 * comes within P_POST_RAID_R of: the frame (twice a second). */
static void raid_watch(Nm *n)
{
    HzNightmare *s = n->s;
    if (!vs_human(n) || g_param[P_POST_RAID] <= 0 || s->frames % 30 != 0) {
        return;
    }
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 k = 0; k < en; k++) {
        const HzUnit *u = unit_at_c(n->g, es, k);
        if (!unit_alive(u) || (u->flags & UNIT_CARRIED)
            || u->type != UNIT_INFANTRY) {
            continue;
        }
        for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
            if (post_ours(n, i)
                && chebyshev(ucx(u) - post(n, i)->x, ucy(u) - post(n, i)->y)
                   <= g_param[P_POST_RAID_R]) {
                s->raid_at[i] = s->frames | 1;
            }
        }
    }
}

/* (Session 69, P_POST_RAID) The own outpost raided last within
 * P_POST_RAID_TIME with fewer than P_POST_RAID guards by it, out of the
 * enemy's missiles; -1 none. The owner (1 November): "It never attempts
 * to Garrison its outposts": on LOCH he took three of them with INFANTRY
 * put down 80 px from each, nothing of its own by them. */
static int raid_guard_post(const Nm *n)
{
    const HzNightmare *s = n->s;
    if (!vs_human(n) || g_param[P_POST_RAID] <= 0
        || !(shop_types(n->g) & (1u << UNIT_TAX))) {
        return -1;
    }
    int best = -1;
    u32 best_at = 0;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        if (!post_ours(n, i) || s->raid_at[i] == 0
            || s->frames - s->raid_at[i] >= (u32)g_param[P_POST_RAID_TIME]
            || (n->danger[clamp63(post(n, i)->y >> 5)]
                         [clamp63(post(n, i)->x >> 5)] & DANGER_MISSILE)
            || post_guards_own(n, i) >= g_param[P_POST_RAID]) {
            continue;
        }
        if (s->raid_at[i] > best_at) {
            best_at = s->raid_at[i];
            best = i;
        }
    }
    return best;
}

/* Where outpost i's guard goes down (the mech's x, y): 48 px from it
 * towards the enemy HQ ((session 69) a raided one's towards the enemy's
 * base nearest it), the nearest place it can. */
static void post_guard_drop(Nm *n, int i, s16 *ox, s16 *oy)
{
    const HzPost *pp = post(n, i);
    const HzPost *e = post(n, enemy_hq(n));
    if (g_param[P_POST_RAID] > 0 && n->s->raid_at[i] != 0) {
        e = post(n, raid_from(n, i));
    }
    int len = imax(chebyshev(e->x - pp->x, e->y - pp->y), 1);
    int gx = pp->x + (e->x - pp->x) * 48 / len - 16;
    int gy = pp->y + (e->y - pp->y) * 48 / len - 16;
    if (!landable_near(n, gx, gy, 64, ox, oy)) {
        *ox = (s16)(pp->x - 16);
        *oy = (s16)(pp->y - 16 + 48);
    }
}

/* (Session 69, P_WAIT_NEAR) Nothing to do: the base to wait over.
 * Against a human its base nearest the enemy mech, where his next drop comes
 * (the one it waited over kept unless another is 128 px nearer), out of
 * the missiles and in the fuel's reach; else its nearest. In
 * The owner's games of 1 November it waited over its nearest base
 * while his drops went in at another. */
static int wait_base(Nm *n)
{
    HzNightmare *s = n->s;
    const HzMech *e = n->rival;
    int near = nearest_base(n, mx(n), my(n));
    if (!vs_human(n) || !g_param[P_WAIT_NEAR]
        || (e->flags & MECH_DESTROYED)) {
        return near;
    }
    int ex = e->x + 16, ey = e->y + 16;
    int best = near, best_d = 0x7FFFFFFF;
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        if (!is_base(n, i)
            || (n->danger[clamp63(post(n, i)->y >> 5)]
                         [clamp63(post(n, i)->x >> 5)] & DANGER_MISSILE)
            || trip_fuel(n, base_dist(n, i, mx(n), my(n)), false)
               + g_param[P_FUEL_MARGIN] > n->m->fuel) {
            continue;
        }
        int d = base_dist(n, i, ex, ey) - (i == s->goal_post ? 128 : 0);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best != near) {
        s->wait_moves++;
    }
    return best;
}

/* (Session 74, P_HOLD_BACK) the owner (14 November): "Every time one of
 * my infantry take a slot in their outpost, Nightmare never bothers to
 * replace it with its own infantry." An outpost holds four: each INFANTRY
 * of the enemy's gone in takes one of its owner's off, and the fourth
 * turns it. It never looked: in his game of 14 November its outpost 6
 * went man by man in 4 minutes 24 s and outpost 7 in 2 minutes 6 s, 11
 * other units bought meanwhile and no INFANTRY. One of its own in puts a
 * hold back (the outpost order goes to an own outpost with an enemy hold:
 * unit_ai.c, outpost_wanted).
 *
 * The outpost of its own to send INFANTRY to: the enemy's holds in it
 * more than its INFANTRY on the way (hold_bought), fewer than P_HOLD_HOT
 * of the enemy's launchers reaching it, not one given up for now; the one
 * most short first. -1 none. */
static int hold_post(const Nm *n)
{
    const HzNightmare *s = n->s;
    if (!g_param[P_HOLD_BACK] || !vs_human(n) || g_param[P_STYLE] == 1
        || !(shop_types(n->g) & (1u << UNIT_INFANTRY))) {
        return -1;
    }
    int best = -1, best_need = 0;
    for (int i = 0; i < HZ_POST_OUTPOSTS; i++) {
        int need = hold_lost(n, i);
        if (need == 0 || s->hold_rest[i] > s->frames
            || (g_param[P_HOLD_HOT] > 0
                && post_missiles(n, i) >= g_param[P_HOLD_HOT])) {
            continue;
        }
        if (s->hold_buy_at[i] != 0
            && s->frames - s->hold_buy_at[i] < NM_HOLD_WAIT) {
            need -= s->hold_out[i];
        }
        if (need > best_need) {
            best_need = need;
            best = i;
        }
    }
    return best;
}

/* (Session 74, P_SUPPLY) the owner (14 November): "Nightmare never
 * builds supply or repair trucks, I just wear out the missile stock in
 * your launchers then there is no more problem." At the end of that game
 * its three SAM-42s had no missile left and three of the TAX-52s round
 * its HQ no ammunition. A SUPPLY (208) fills what it finds within 200 px
 * under a quarter of its fuel, missiles or ammunition, and drives on to
 * the next (unit_ai.c, needs_supply).
 *
 * Its units standing or on guard within P_WALL_R + 128 px of its HQ that
 * a SUPPLY would serve for their ammunition or missiles: how many; *at
 * the one with the least left (NULL none). */
static bool home_unit(const Nm *n, const HzUnit *u)
{
    const HzPost *hq = post(n, own_hq(n));
    return unit_alive(u)
           && (u->order == ORDER_STAND || u->order == ORDER_GUARD_NEAR)
           && chebyshev(ucx(u) - hq->x, ucy(u) - hq->y)
              <= g_param[P_WALL_R] + 128;
}

/* ... short of missiles (under a quarter), or of ammunition */
static bool short_missiles(const Nm *n, const HzUnit *u)
{
    const HzUnitStats *st = unit_stats_for(n->g, u->type);
    return st->missiles != 0 && (u8)(st->missiles >> 2) > u->missiles;
}

static bool short_ammo(const Nm *n, const HzUnit *u)
{
    const HzUnitStats *st = unit_stats_for(n->g, u->type);
    return st->ammo != 0 && (u8)(st->ammo >> 2) > u->ammo;
}

/* ... a vehicle under three quarters of its HP (P_REPAIR) */
static bool hurt_vehicle(const Nm *n, const HzUnit *u)
{
    const HzUnitStats *st = unit_stats_for(n->g, u->type);
    return u->type < UNIT_INFANTRY && st->hp != 0 && u->hp * 4 < st->hp * 3;
}

static int wall_short(const Nm *n, const HzUnit **at)
{
    int count = 0, least = 0x7FFF;
    *at = NULL;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (!home_unit(n, u)) {
            continue;
        }
        const HzUnitStats *st = unit_stats_for(n->g, u->type);
        bool missiles = short_missiles(n, u);
        bool ammo = short_ammo(n, u);
        if (!missiles && !ammo) {
            continue;
        }
        count++;
        int left = missiles ? u->missiles * 100 / st->missiles
                            : u->ammo * 100 / st->ammo;
        if (left < least) {
            least = left;
            *at = u;
        }
    }
    return count;
}

/* (P_REPAIR) ... that are under three quarters of their HP (a repair
 * truck's men mend the vehicles within 160 px of it, and it drives to
 * those within 400: docs/repair.md); *at the one with the least left. */
static int wall_hurt(const Nm *n, const HzUnit **at)
{
    int count = 0, least = 0x7FFF;
    *at = NULL;
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (!home_unit(n, u) || !hurt_vehicle(n, u)) {
            continue;
        }
        const HzUnitStats *st = unit_stats_for(n->g, u->type);
        count++;
        int left = u->hp * 100 / st->hp;
        if (left < least) {
            least = left;
            *at = u;
        }
    }
    return count;
}

/* Its trucks of a type (a SUPPLY, a repair truck) within reach of the
 * units round its HQ, the one on order or carried for them too. */
static int home_trucks(const Nm *n, int type)
{
    const HzNightmare *s = n->s;
    const HzPost *hq = post(n, own_hq(n));
    int count = 0;
    if ((s8)n->m->prod_state > 0 && s->prod_cargo == NM_CARGO_SUPPLY
        && s->buy_type == type) {
        count++;
    }
    if ((n->m->flags & MECH_CARRYING) && s->cargo == NM_CARGO_SUPPLY
        && carried_unit(n->g, n->m)->type == type) {
        count++;
    }
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *u = unit_at_c(n->g, n->team, i);
        if (unit_alive(u) && u->type == type
            && chebyshev(ucx(u) - hq->x, ucy(u) - hq->y)
               <= g_param[P_WALL_R] + 128 + 200) {
            count++;
        }
    }
    return count;
}

/* A SUPPLY to buy for them: P_SUPPLY of them short, and none there. */
static bool supply_wanted(const Nm *n)
{
    const HzUnit *at;
    return g_param[P_SUPPLY] > 0 && vs_human(n) && g_param[P_STYLE] != 1
           && (shop_types(n->g) & (1u << UNIT_SUPPLY))
           && wall_short(n, &at) >= g_param[P_SUPPLY]
           && home_trucks(n, UNIT_SUPPLY) == 0;
}

/* A repair truck: P_REPAIR of them hurt, and none there (the shop sells
 * one only with the REPAIR patch). */
static bool repair_wanted(const Nm *n)
{
    const HzUnit *at;
    return g_param[P_REPAIR] > 0 && vs_human(n) && g_param[P_STYLE] != 1
           && repair_on(n->g)
           && (shop_types(n->g) & (1u << UNIT_REPAIR))
           && wall_hurt(n, &at) >= g_param[P_REPAIR]
           && home_trucks(n, UNIT_REPAIR) == 0;
}

/* Where the truck carried goes down (the mech's x, y): by the unit with
 * the least left of what it brings, the nearest place; with none in want
 * now, in the wall. */
static void supply_drop(Nm *n, s16 *ox, s16 *oy)
{
    const HzUnit *at;
    bool mends = carried_unit(n->g, n->m)->type == UNIT_REPAIR;
    bool want = (mends ? wall_hurt(n, &at) : wall_short(n, &at)) > 0;
    if (lanes_on(n)) {
        /* (session 75, P_HOME_LANES) in the ring round its HQ, nearest
         * the unit (with none in want, the side facing the enemy), to
         * drive out to it by the lanes */
        const HzPost *hq = post(n, own_hq(n));
        const HzPost *e = post(n, enemy_hq(n));
        int x = want ? ucx(at) : e->x, y = want ? ucy(at) : e->y;
        int home_r = NM_LANE_FIRST + NM_LANE_BANDS * NM_LANE_PITCH;
        if ((!want || chebyshev(x - hq->x, y - hq->y) <= home_r)
            && yard_drop(n, x, y, ox, oy)) {
            return;
        }
    }
    if (want && landable_near(n, ucx(at) - 16, ucy(at) - 16, 96, ox, oy)) {
        return;
    }
    wall_drop(n, ox, oy);
}

/* (Session 75, P_HOME_LANES) A truck of its own by its HQ with nothing to
 * do within its search while a unit there wants it farther off: lifted,
 * and put down again by that one (supply_drop). In a game against the
 * turtle on LOCH its SUPPLY, carried home, was put down where the jet was
 * when the enemy's came at it (a dogfight), 100 px south-east of its HQ;
 * its wall stood north-west, 230 px and more away, past the 200 a SUPPLY
 * looks (find_customer_fixed), and it stood there from 25:20 to the end
 * with up to 34 of its units empty by it. Not more than once in 20 s. */
#define NM_TRUCK_LIFT_GAP 1200

static bool truck_wanted_by(const Nm *n, const HzUnit *u, int type)
{
    return home_unit(n, u)
           && (type == UNIT_SUPPLY ? short_missiles(n, u) || short_ammo(n, u)
                                   : hurt_vehicle(n, u));
}

static bool truck_lift_next(Nm *n)
{
    HzNightmare *s = n->s;
    if (!lanes_on(n) || (n->m->flags & MECH_CARRYING)
        || (s->truck_lift_at != 0
            && s->frames - s->truck_lift_at < NM_TRUCK_LIFT_GAP)) {
        return false;
    }
    const HzPost *hq = post(n, own_hq(n));
    u32 on = side_slots(n->g, n->team);
    for (u32 i = 0; i < on; i++) {
        const HzUnit *t = unit_at_c(n->g, n->team, i);
        if (!unit_alive(t)
            || (t->type != UNIT_SUPPLY && t->type != UNIT_REPAIR)
            || chebyshev(ucx(t) - hq->x, ucy(t) - hq->y)
               > g_param[P_WALL_R] + 128 + 200
            || (t->type == UNIT_SUPPLY
                && (t->ai.docked || !ref_is_none(t->ai.customer_id)))) {
            continue;
        }
        /* its search: a SUPPLY's 200 px, a repair truck's 400, less a
         * margin */
        int reach = (t->type == UNIT_SUPPLY) ? 184 : 384;
        bool near = false, far = false;
        for (u32 k = 0; k < on && !near; k++) {
            const HzUnit *u = unit_at_c(n->g, n->team, k);
            if (u == t || !truck_wanted_by(n, u, t->type)) {
                continue;
            }
            if (chebyshev(ucx(u) - ucx(t), ucy(u) - ucy(t)) <= reach) {
                near = true;
                continue;
            }
            /* far off: worth the lift only if the ring has a place in
             * its reach, and the truck isn't standing there already (on
             * EISFREI, its HQ in a corner, it lifted one 30 times a game
             * from one place to the same place) */
            s16 yx, yy;
            if (yard_find(n, ucx(u), ucy(u), &yx, &yy)
                && chebyshev(yx + 16 - ucx(u), yy + 16 - ucy(u)) <= reach
                && chebyshev(yx + 16 - ucx(t), yy + 16 - ucy(t)) > 48) {
                far = true;
            }
        }
        if (near || !far) {
            continue;
        }
        s->lift = unit_ref(t);
        s->retrucking = true;
        s->truck_lift_at = s->frames;
        s->truck_lifts++;
        set_task(n, NM_TASK_LIFT);
        return true;
    }
    return false;
}

/* What to buy next: (session 68) guards where the enemy shot its HQ from,
 * then INFANTRY to take an outpost while it has few, else the attack
 * unit (style 1: the wall first). False if it can't pay yet. (Session 74)
 * Before those: INFANTRY for a hold lost in an outpost of its own, and a
 * SUPPLY for the units round its HQ. */
static bool choose_buy(Nm *n)
{
    HzNightmare *s = n->s;
    s->buy_mix = false;
    int hold = hq_pressed(n) ? -1 : hold_post(n);
    if (hold >= 0) {
        /* (session 74, P_HOLD_BACK) a hold lost in an outpost of its own:
         * INFANTRY for it before anything, the money kept for it */
        if (n->m->money < cost_of(n, UNIT_INFANTRY, ORDER_OUTPOST)) {
            return false;
        }
        s->buy_type = UNIT_INFANTRY;
        s->buy_order = ORDER_OUTPOST;
        s->prod_cargo = NM_CARGO_CAPTURE;
        s->prod_post = (u8)hold;
        return true;
    }
    if (supply_wanted(n)) {
        /* (session 74, P_SUPPLY) */
        if (n->m->money < cost_of(n, UNIT_SUPPLY, ORDER_SUPPLY)) {
            return false;
        }
        s->buy_type = UNIT_SUPPLY;
        s->buy_order = ORDER_SUPPLY;
        s->prod_cargo = NM_CARGO_SUPPLY;
        s->prod_post = 0xFF;
        return true;
    }
    if (repair_wanted(n) && !hq_pressed(n)
        && n->m->money >= cost_of(n, UNIT_REPAIR, ORDER_REPAIR)) {
        /* (session 74, P_REPAIR) when its price is there, and not while
         * its HQ is being shot: its men are called in when an enemy comes
         * within 120 px */
        s->buy_type = UNIT_REPAIR;
        s->buy_order = ORDER_REPAIR;
        s->prod_cargo = NM_CARGO_SUPPLY;
        s->prod_post = 0xFF;
        return true;
    }
    if (defend_wanted(n)) {
        return guard_buy(n);
    }
    if (g_param[P_HOME_EARLY] > 0 && cpu_hard(n) && g_param[P_STYLE] != 1) {
        /* (session 68) TAX-52s standing at its HQ before INFANTRY goes
         * out: on STRAND (TYPE D, as P1) its first nine buys were
         * INFANTRY, and one TAX-52 the CPU put down 140 px from its HQ at
         * three minutes took it in 18 seconds */
        int sams;
        if (wall_count(n, &sams) < g_param[P_HOME_EARLY]) {
            return wall_unit(n, UNIT_TAX);
        }
    }
    if (s->capture_post < HZ_POST_OUTPOSTS
        && (post_ours(n, s->capture_post) || s->capture_sent >= 6)) {
        if (!post_ours(n, s->capture_post)) {
            /* (session 68) VULKAN: 50 sent at one the CPU's jet kept, in
             * 31 minutes, and no attack unit bought while it tried */
            int i = s->capture_post;
            if (s->cap_fails[i] < 0xFF) {
                s->cap_fails[i]++;
            }
            s->cap_rest[i] = s->frames
                             + (u32)g_param[P_CAPTURE_REST] * s->cap_fails[i];
            if (s->cap_row < 0xFF) {
                s->cap_row++;
            }
            s->cap_rest_all = s->frames
                              + (u32)g_param[P_CAPTURE_REST] * s->cap_row;
            s->cap_given_up++;
        } else {
            s->cap_row = 0;
            s->cap_fails[s->capture_post] = 0;
        }
        s->capture_post = 0xFF;
    }
    /* (session 68) VULKAN: the CPU's HQ on 198 for 25 minutes while it
     * sent INFANTRY at outposts, its own ground down to 5 */
    bool finish = g_param[P_FINISH_HP] > 0 && g_param[P_STYLE] != 1
                  && post_hq_hp(post(n, enemy_hq(n))) <= g_param[P_FINISH_HP];
    if (g_param[P_CAPTURE_CALM] > 0 && g_param[P_STYLE] != 1
        && s->hq_hit_at != 0
        && s->frames - s->hq_hit_at < (u32)g_param[P_CAPTURE_CALM]) {
        finish = true;
    }
    if (g_param[P_ATTACK_GAP] > 0 && g_param[P_STYLE] != 1
        && s->frames - s->attack_at >= (u32)g_param[P_ATTACK_GAP]) {
        /* (session 68) WALDUNG (TYPE B): 48 INFANTRY sent at outposts,
         * one attack unit, and lost */
        finish = true;
    }
    /* (session 74, P_CAPTURE_FREE) an outpost that is nobody's and out of
     * CAPTURE_REACH is let go while the enemy's attack units are out
     * (pick_capture_of) */
    bool free_on = g_param[P_CAPTURE_FREE] && vs_human(n)
                   && g_param[P_STYLE] != 1;
    if ((free_on || (g_param[P_CAPTURE_NOBODY] && vs_human(n)))
        && s->capture_post < HZ_POST_OUTPOSTS
        && !(post(n, s->capture_post)->flags & POST_OWNED)
        && enemy_attacking(n)) {
        const HzPost *pp = post(n, s->capture_post);
        if (base_dist(n, nearest_base(n, pp->x, pp->y), pp->x, pp->y)
            > g_param[P_CAPTURE_REACH]) {
            s->capture_post = 0xFF;
        }
    }
    if (finish && s->capture_post != 0xFF
        && own_outposts(n) >= capture_min(n)) {
        s->capture_post = 0xFF;
    }
    if (s->capture_post == 0xFF
        && (g_param[P_CAPTURE_ALL] != 1 || s->frames >= s->cap_rest_all)
        && own_outposts(n) < g_param[P_CAPTURE_MAX]
        && (own_outposts(n) < capture_min(n)
            || (!finish && g_param[P_DROP_GD] >= 0 && front_needed(n)))) {
        int cap = pick_capture(n);
        if (cap >= 0) {
            s->capture_post = (u8)cap;
            s->capture_sent = 0;
        }
    }
    bool paying = s->ehq_hit_at != 0 && g_param[P_ATTACK_GAP] > 0
                  && s->frames - s->ehq_hit_at < (u32)g_param[P_ATTACK_GAP];
    /* (session 75, P_CAPTURE_OPEN) in the opening, nobody's past
     * CAPTURE_MIN too: on LOCH (15 November) it held its 3 and its mech
     * hovered over outpost 6 waiting for the next $670, 67% of minutes 0
     * to 6 and 89% of 6 to 12, while nobody's three were taken by him at
     * 3:18, 7:33 and 12:09 (3 outposts to his 6; each pays about $240 a
     * minute). `finish` keeps its attack's turn (CAPTURE_FREE 2's
     * lesson: the 800 a fifth outpost took were attack units). */
    bool opening = g_param[P_CAPTURE_OPEN] > 0 && !finish
                   && n->g->frame_counter < (u32)g_param[P_CAPTURE_OPEN];
    if (s->capture_post == 0xFF && free_on
        && (own_outposts(n) < g_param[P_CAPTURE_MIN]
            || (g_param[P_CAPTURE_FREE] >= 2 && !paying && !finish)
            || opening)
        && own_outposts(n) < g_param[P_CAPTURE_MAX]) {
        /* (session 74) one that is nobody's: out of CAPTURE_REACH while
         * it has fewer than CAPTURE_MIN; (P_CAPTURE_FREE 2, off) and
         * past CAPTURE_MIN while its attack is not paying (the enemy HQ
         * not shot in ATTACK_GAP), its attack keeping its turn (`finish`:
         * no attack unit put down in ATTACK_GAP). Past CAPTURE_MIN, from
         * the start against the raider on ABGRUND, LOCH, EISFREI and
         * WALDUNG as P1 it won 7 games of 16 for 14, the rest drawn at
         * the hour (taken whatever its attack, none): the 800 a fifth
         * outpost took were the attack units that had kept the raider's
         * HQ bare, the mech carried INFANTRY instead of fighting by his
         * HQ beside its tanks, and with a wall up there its attacks did
         * nothing more. */
        int cap = pick_capture_of(n, true);
        if (cap >= 0) {
            s->capture_post = (u8)cap;
            s->capture_sent = 0;
        }
    }
    int home = home_post(n);
    if (home >= 0 && s->capture_post != home) {
        int sams;
        if (!g_param[P_HOME_POST_WALL]
            || wall_count(n, &sams) * 2 >= garrison_size(n)) {
            s->capture_post = (u8)home;
            s->capture_sent = 0;
            s->home_posts++;
        }
    }
    if (s->capture_post != 0xFF && robot_held(n, s->capture_post)) {
        s->capture_post = 0xFF;
        s->escorts++;
        int cap = pick_capture(n);
        if (cap >= 0) {
            s->capture_post = (u8)cap;
            s->capture_sent = 0;
        }
    }
    int raided = raid_guard_post(n);
    if (raided >= 0) {
        /* (session 69) a guard by an outpost the enemy's INFANTRY came
         * at, before INFANTRY of its own goes out */
        if (!buy_post_guard(n, raided)) {
            return false;
        }
        s->raid_guards++;
        return true;
    }
    if (s->capture_post != 0xFF && escort_wanted(n, s->capture_post)) {
        if (n->m->money >= cost_of(n, UNIT_TAX, ORDER_GUARD_NEAR)) {
            s->buy_type = UNIT_TAX;
            s->buy_order = ORDER_GUARD_NEAR;
            s->prod_cargo = NM_CARGO_CAPTURE;
            s->prod_post = s->capture_post;
            s->escort_at[s->capture_post] = s->frames | 1;
            s->escorts++;
            return true;
        }
        return false;
    }
    if (s->capture_post != 0xFF) {
        if (n->m->money >= cost_of(n, UNIT_INFANTRY, ORDER_OUTPOST)) {
            s->buy_type = UNIT_INFANTRY;
            s->buy_order = ORDER_OUTPOST;
            s->prod_cargo = NM_CARGO_CAPTURE;
            s->prod_post = s->capture_post;
            return true;
        }
        return false;
    }
    if (g_param[P_STYLE] == 1) {
        return wall_buy(n);
    }
    if (hunt_wanted(n, true)) {
        return buy_hunter(n);
    }
    int pg = post_guard_post(n, g_param[P_POST_GUARD_FIRST]
                                ? imin(g_param[P_POST_GUARD], 1) : 0);
    if (pg >= 0) {
        return buy_post_guard(n, pg);
    }
    int garrison = garrison_size(n);
    if (garrison > 0) {
        /* (session 67) its HQ's garrison kept up first */
        int sams;
        int wall = wall_count(n, &sams);
        if (boat_home_wanted(n, wall)) {
            return wall_unit(n, UNIT_ST);
        }
        bool mix = false;
        if (wall < garrison && g_param[P_ATTACK_MIX] > 0 && vs_human(n)
            && wall * 2 >= garrison) {
            /* (session 68) half of it up: every P_ATTACK_MIX-th an
             * attack unit. Counted here, at every look at what to buy,
             * so an attack unit's turn comes and goes while its price
             * isn't there, and the wall's turns between take the money
             * as it comes: in the owner's game of 10 November 220
             * turns bought one attack unit ($508 at most of its $670 at
             * the other 219). (Session 73, P_MIX_BUYS) Counting the
             * units bought instead (task_buy), the money kept for the
             * turn: his game taken over, the turtle for him, it lost 53
             * of 192 for 39, 3 wall units fewer for half an attack unit
             * more. Off: with the wall half up an attack unit goes only
             * when it is rich. */
            if (g_param[P_MIX_BUYS]) {
                s->buy_mix = true;
                mix = (s->mix_buys + 1) % g_param[P_ATTACK_MIX] == 0;
            } else {
                mix = ++s->mix_buys % g_param[P_ATTACK_MIX] == 0;
            }
        }
        if (wall < garrison && !mix) {
            if (!wall_next(n, wall, sams)) {
                return false;
            }
            if (futile(n) && s->hq_hit_at == 0) {
                s->futile_buys++;
            }
            return true;
        }
    }
    pg = post_guard_post(n, g_param[P_POST_GUARD]);
    if (pg >= 0) {
        return buy_post_guard(n, pg);
    }
    int type = g_param[P_RUSH_TYPE];
    int order = ORDER_ATTACK_HQ;
    u8 cargo = NM_CARGO_ATTACK;
    if (hunt_wanted(n, false)) {
        return buy_hunter(n);
    } else if (naval_wanted(n)) {
        type = UNIT_ST;
    } else if (g_param[P_NO_BLIND] && g_param[P_DRIVE] > 0) {
        /* (session 68) none where no drive passes: the wall instead, up
         * to P_WALL_SPARE, else the money kept */
        s16 x, y;
        bool untried = false;
        if (!attack_drop_at(n, NM_REACH_ANY, g_param[P_DROP_RANGE], &x, &y,
                            &untried, NULL)) {
            int sams;
            int wall = wall_count(n, &sams);
            if (wall < g_param[P_WALL_SPARE]) {
                return wall_next(n, wall, sams);
            }
            return false;
        }
    }
    if (g_param[P_GROUP] >= 2 && rally_ok(n)) {
        /* (session 68) gathered standing first; none while the group
         * goes in (its orders take the money) */
        if (s->launching) {
            return false;
        }
        order = ORDER_STAND;
        cargo = NM_CARGO_GATHER;
    }
    if (n->m->money >= cost_of(n, type, order)) {
        s->buy_type = (u8)type;
        s->buy_order = (u8)order;
        s->prod_cargo = cargo;
        s->prod_post = 0xFF;
        if (type == UNIT_ST) {
            s->naval++;
        }
        if (s->since_hunt < 255) {
            s->since_hunt++;
        }
        return true;
    }
    return false;
}

/* (Session 68, P_SAFE_SERVICE) The base to refuel at: the nearest one out
 * of the enemy's missiles the fuel reaches, else the nearest. In
 * The owner's game of 30 October it sat being repaired at its outpost
 * while his SAM-42s by it shot it. (P_HOME_SERVICE) Against a human, its
 * HQ being shot, the HQ if the fuel reaches it: on EISFREI (as P1) two of
 * the turtle's TAX-52s put down behind its HQ took it in 50 s while the
 * jet refuelled at an outpost 850 px off. */
static int service_base(const Nm *n)
{
    int near = nearest_base(n, mx(n), my(n));
    if (g_param[P_HOME_SERVICE] && vs_human(n) && hq_pressed(n)) {
        int hq = own_hq(n);
        if (trip_fuel(n, base_dist(n, hq, mx(n), my(n)),
                      (n->m->flags & MECH_CARRYING) != 0)
            + g_param[P_FUEL_MARGIN] / 2 <= n->m->fuel) {
            return hq;
        }
    }
    if (!g_param[P_SAFE_SERVICE] || !base_hot(n, near)) {
        return near;
    }
    bool loaded = (n->m->flags & MECH_CARRYING) != 0;
    int best = near, best_d = 0x7FFF;
    for (int i = 0; i < HZ_POST_CLEARED; i++) {
        if (!is_base(n, i) || base_hot(n, i)) {
            continue;
        }
        int d = base_dist(n, i, mx(n), my(n));
        if (trip_fuel(n, d, loaded) + g_param[P_FUEL_MARGIN] / 2
            > n->m->fuel) {
            continue;
        }
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* (Session 74, P_FIGHT_ON) Against a human, is it in a fight it fights
 * on: not left for the fuel home, nor in the enemy's missiles' reach for
 * repair (needs_service, task_fight)? The owner (14 November):
 * "Sometimes a suicide run is a valid strategy. There is no cost in
 * losing a plane other than the time to regenerate." In his game of that
 * day it was destroyed 45 times, 30 of them flying home from a fight
 * under his GRM-34As, shot down on the way with ammunition left: 6 to 7
 * s each either way, and the shots not fired. */
static bool fights_on(const Nm *n)
{
    return g_param[P_FIGHT_ON] && vs_human(n)
           && n->s->task == NM_TASK_FIGHT
           && !(n->m->flags & MECH_CARRYING);
}

static bool needs_service(const Nm *n)
{
    HzMech *m = n->m;
    const HzNightmare *s = n->s;
    /* (session 74) a fight it fights on: not left for repair in the
     * missiles' reach, nor anywhere for the fuel home (built again it has
     * a quarter of a tank, Mech_Respawn: it flew 340 px to his tank, the
     * way home and FUEL_MARGIN were more than was left, and it turned
     * back as it landed) */
    bool on = fights_on(n);
    if (m->hp < kidou(n)->stats.hp / 3
        && !(on && (danger_at(n, m->x, m->y) & DANGER_MISSILE))) {
        return true;
    }
    bool loaded = (m->flags & MECH_CARRYING) != 0;
    if (loaded && s->task == NM_TASK_DELIVER && s->stage == 2) {
        /* on the last leg: there, then home light (planned with the
         * whole margin; half of it may go on the way round) */
        return !leg_ok(n, s->goal_x, s->goal_y, g_param[P_FUEL_MARGIN] / 2);
    }
    if (loaded && s->task == NM_TASK_DELIVER && s->stage == 1) {
        return false;               /* on its way to refuel */
    }
    int home = nearest_base(n, mx(n), my(n));
    int far = base_dist(n, home, mx(n), my(n));
    return (!on && m->fuel < trip_fuel(n, far, loaded) + g_param[P_FUEL_MARGIN])
           || (!loaded && m->ammo < 8);
}

/* (Session 68, P_BASE_FUEL) Deciding at a base, carrying nothing, its HQ
 * calm: refuel there first? In the owner's game of 31 October, 18:31
 * (VULKAN, the server's AI P1) the mech came back on its HQ on a quarter
 * of its tank and went straight out to fight on it, 400 px off. Asked in
 * every task (as needs_service), against the turtle on maps 2, 5 and 6 it
 * lost 23 of 40 for 7: it left its fights by its HQ and outposts, and its
 * pickups, to refuel; asked deciding only, 12 for 8. Off. */
static bool base_fuel_low(const Nm *n)
{
    if (g_param[P_BASE_FUEL] <= 0 || !vs_human(n)
        || (n->m->flags & MECH_CARRYING) || hq_pressed(n)) {
        return false;
    }
    int home = nearest_base(n, mx(n), my(n));
    return base_dist(n, home, mx(n), my(n)) <= 32
           && n->m->fuel * 100 < kidou(n)->stats.fuel * g_param[P_BASE_FUEL];
}

/* A missile unit within 256 + 24 px of the point ctx (x, y)? */
static bool missiles_reach(const HzUnit *u, const void *ctx)
{
    const int *pt = ctx;
    return missile_unit(u) && chebyshev(u->x - pt[0], u->y - pt[1]) < 256 + 24;
}

/* Is (x, y) under the missiles of an enemy unit other than `except`? */
static bool missiles_other(const Nm *n, int x, int y, const HzUnit *except)
{
    const int pt[2] = { x, y };
    return any_unit_near(n->g, 1 - n->team, x - 280, y - 280, x + 312,
                         y + 312, except, missiles_reach, pt);
}

/* One of ours on its way in to the enemy HQ (within 480 px of it), within
 * 160 px of the enemy unit ctx[2..3] (centres); ctx[0..1] the HQ. */
static bool escort_near(const HzUnit *o, const void *ctx)
{
    const int *c = ctx;
    return o->order == ORDER_ATTACK_HQ
           && chebyshev(ucx(o) - c[0], ucy(o) - c[1]) < 480
           && chebyshev(ucx(o) - c[2], ucy(o) - c[3]) < 160;
}

/* The enemy HQ's guards: its units within P_SIEGE_R of it (not a SUPPLY
 * or a boat), how many; the one to fight first (nearest the mech, and out
 * of any other's missiles) in *best, NULL none. */
static int siege_guards(const Nm *n, const HzUnit **best)
{
    const HzPost *e = post(n, enemy_hq(n));
    int count = 0, best_d = 0x7FFF;
    *best = NULL;
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (!unit_alive(u) || u->type == UNIT_SUPPLY || u->type == UNIT_ST) {
            continue;
        }
        bool near = chebyshev(ucx(u) - e->x, ucy(u) - e->y)
                    <= g_param[P_SIEGE_R];
        if (!near) {
            /* or by one of ours on its way in */
            const int c[4] = { e->x, e->y, ucx(u), ucy(u) };
            near = any_unit_near(n->g, n->team, c[2] - 192, c[3] - 192,
                                 c[2] + 192, c[3] + 192, NULL, escort_near,
                                 c);
        }
        if (!near) {
            continue;
        }
        count++;
        if (missiles_other(n, u->x, u->y, u)) {
            continue;
        }
        int d = chebyshev(ucx(u) - mx(n), ucy(u) - my(n));
        if (d < best_d) {
            best_d = d;
            *best = u;
        }
    }
    return count;
}

/* Can the mech fly to unit u, fight a while and fly to the own base
 * nearest it on the fuel it has? */
static bool fight_fuel_ok(const Nm *n, const HzUnit *u)
{
    int there = chebyshev(ucx(u) - mx(n), ucy(u) - my(n));
    int home = nearest_base(n, ucx(u), ucy(u));
    int back = base_dist(n, home, ucx(u), ucy(u));
    return n->m->fuel >= trip_fuel(n, there, false) + trip_fuel(n, back, false)
                         + g_param[P_FUEL_MARGIN] + 60;
}

/* (Session 74, P_STRIKE) With nothing else to do, the enemy's nest by its
 * bases gone for, whatever it costs the mech. The owner (14 November):
 * "Sometimes a suicide run is a valid strategy. There is no cost in losing
 * a plane other than the time to regenerate." In his game of that day 11
 * GRM-34As and 3 SAM-42s stood 437 to 716 px from its HQ for 26 minutes
 * with two SUPPLYs and two repair trucks, none of them ever shot at
 * (P_NEST_DEATHS, P_HOME_GRAVES and P_TENDED kept it off them), while its
 * mech waited over its HQ a third of the game and was destroyed 43 times
 * by their missiles fighting his tanks beside them. On foot among them it
 * has a second or two and 3 to 9 shots of 16: a SUPPLY has 64 HP, a
 * repair truck 80, a SAM-42 96, a GRM-34A 144.
 *
 * The unit to go for: an enemy SAM-42 or GRM-34A within P_STRIKE_R px of
 * its HQ, or with an outpost of its own in its missiles' reach, or a
 * SUPPLY or repair truck within 320 px of one of those (they refill and
 * mend the nest). The one the fewest launchers reach first; of those the
 * repair trucks, then the SUPPLYs, then the launchers nearest its HQ; and
 * the one it went for last before another. None while it found no place
 * to fight one from (nofight). */
static bool strike_launcher(const HzUnit *u)
{
    return u->type == UNIT_SAM || u->type == UNIT_GRM;
}

static bool launcher_by(const HzUnit *u, const void *ctx)
{
    const int *pt = ctx;
    return strike_launcher(u)
           && chebyshev(ucx(u) - pt[0], ucy(u) - pt[1]) <= 320;
}

static HzRef pick_strike(const Nm *n)
{
    const HzNightmare *s = n->s;
    HzRef best = ref_none();
    if (!g_param[P_STRIKE] || !vs_human(n)) {
        return best;
    }
    long best_score = 0x7FFFFFFFL;
    int es = 1 - n->team;
    u32 en = side_slots(n->g, es);
    for (u32 i = 0; i < en; i++) {
        const HzUnit *u = unit_at_c(n->g, es, i);
        if (!unit_alive(u)) {
            continue;
        }
        bool truck = u->type == UNIT_SUPPLY
                     || (repair_on(n->g) && u->type == UNIT_REPAIR);
        if (!truck && !strike_launcher(u)) {
            continue;
        }
        /* by its HQ, or with an outpost of its own in a launcher's reach
         * (the enemy's wall round his own HQ is none of it: striking at
         * whatever stood within 800 px of any base of its own, it flew
         * across the map from the raider's INFANTRY and lost 27 of 72
         * for 14) */
        int d = base_dist(n, own_hq(n), ucx(u), ucy(u));
        if (d > g_param[P_STRIKE_R]) {
            d = 0x7FFF;
            for (int b = 0; b < HZ_POST_OUTPOSTS; b++) {
                if (is_base(n, b)
                    && base_dist(n, b, ucx(u), ucy(u)) <= 256 + 16 + 32) {
                    d = imin(d, g_param[P_STRIKE_R]
                                + base_dist(n, b, ucx(u), ucy(u)));
                }
            }
        }
        if (d == 0x7FFF || nofight(n, unit_ref(u))) {
            continue;
        }
        if (s->strike_rest_until != 0 && s->frames < s->strike_rest_until
            && chebyshev(ucx(u) - s->strike_rest_x, ucy(u) - s->strike_rest_y)
               <= 320) {
            /* (session 77, P_STRIKE_GIVEUP) given up there a while: his
             * games of 17 and 19 November (EISFREI) taken over at 12:00
             * against the turtle, it struck 74 and 83 times a game for
             * 0.2 and 3.8 units killed, destroyed 72 and 83 times, the
             * nests' SUPPLYs and repair trucks keeping them up */
            continue;
        }
        if (truck) {
            const int pt[2] = { ucx(u), ucy(u) };
            if (!any_unit_near(n->g, es, pt[0] - 320, pt[1] - 320,
                               pt[0] + 320, pt[1] + 320, NULL, launcher_by,
                               pt)) {
                continue;
            }
        }
        /* the one the fewest launchers' missiles reach (the nest taken
         * from its edge: in the middle of his eleven GRM-34As it had 0.7
         * s on foot, three shots); of those the repair trucks before the
         * SUPPLYs (what it leaves of a unit their men mend before it is
         * back), those before the launchers, and the one it went for
         * last before another */
        int cover = n->missiles[clamp63(ucy(u) >> 5)][clamp63(ucx(u) >> 5)];
        long score = cover * 4000L + d
                     + (u->type == UNIT_REPAIR ? 0
                        : u->type == UNIT_SUPPLY ? 1000L : 2000L);
        if (ref_eq(unit_ref(u), s->strike)) {
            score -= 3000L;
        }
        if (score < best_score) {
            best_score = score;
            best = unit_ref(u);
        }
    }
    return best;
}

/* The unit waiting at the HQ picked up: to NM_TASK_PICKUP at the base
 * nearest where it goes. */
static void pickup_ready(Nm *n)
{
    HzNightmare *s = n->s;
    int gx, gy;
    if ((s->prod_cargo == NM_CARGO_CAPTURE || s->prod_cargo == NM_CARGO_POST)
        && s->prod_post < HZ_POST_OUTPOSTS) {
        gx = post(n, s->prod_post)->x;
        gy = post(n, s->prod_post)->y;
    } else if (s->prod_cargo == NM_CARGO_WALL
               || s->prod_cargo == NM_CARGO_GUARD
               || s->prod_cargo == NM_CARGO_SUPPLY) {
        gx = post(n, own_hq(n))->x;
        gy = post(n, own_hq(n))->y;
    } else if (s->prod_cargo == NM_CARGO_GATHER && s->rally_set) {
        gx = s->rally_x + 16;
        gy = s->rally_y + 16;
    } else {
        gx = post(n, enemy_hq(n))->x;
        gy = post(n, enemy_hq(n))->y;
    }
    s->goal_post = (u8)pickup_base(n, gx, gy);
    set_task(n, NM_TASK_PICKUP);
}

static void decide(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;

    if (menu_open(m)) {
        menu_close(n);
        return;
    }
    if (!(m->flags & MECH_CARRYING)) {
        s->bounces = 0;
    }
    if (needs_service(n) || base_fuel_low(n)) {
        s->goal_post = (u8)service_base(n);
        set_task(n, NM_TASK_SERVICE);
        return;
    }
    bool guard_carried = (m->flags & MECH_CARRYING)
                         && (s->cargo == NM_CARGO_GUARD
                             || s->cargo == NM_CARGO_FIELD);
    int carried_type = guard_carried ? carried_unit(n->g, m)->type : -1;
    int guard_ord = (carried_type == UNIT_TAX) ? guard_order() : ORDER_STAND;
    if ((carried_type == UNIT_TAX || carried_type == UNIT_ST)
        && m->carried_order != guard_ord) {
        if (!s->reordered) {
            /* (session 68) an attack unit (a TAX-52 or a boat) made a
             * guard: its order */
            s->reordered = true;
            s->buy_order = (u8)guard_ord;
            set_task(n, NM_TASK_BUY);
            return;
        }
        s->cargo = NM_CARGO_ATTACK;     /* it couldn't: as it was */
        guard_carried = false;
    }
    if (!(m->flags & MECH_CARRYING) && g_param[P_DEFEND_FIRST]
        && hq_pressed(n) && defend_wanted(n)) {
        /* (session 68) its HQ being shot: guards before the fight, the
         * unit waiting to go one of them (picked up at the HQ, ordered to
         * stand on the way). Not the attack unit alone: on LOCH (TYPE B)
         * an INFANTRY ready for an outpost waited 25 minutes while the
         * jet fought at home, and no guard could be bought. */
        if (m->prod_state == PROD_READY && s->prod_cargo != NM_CARGO_GUARD
            && (g_param[P_DEFEND_FIRST] >= 2
                || s->prod_cargo == NM_CARGO_ATTACK)) {
            s->prod_cargo = NM_CARGO_GUARD;
            s->converted++;
        }
        if (m->prod_state == PROD_READY && s->prod_cargo == NM_CARGO_GUARD) {
            const HzPost *hq = post(n, own_hq(n));
            s->reordered = false;
            s->goal_post = (u8)pickup_base(n, hq->x, hq->y);
            set_task(n, NM_TASK_PICKUP);
            return;
        }
        if ((s8)m->prod_state <= 0 && choose_buy(n)) {
            set_task(n, NM_TASK_BUY);
            return;
        }
    }
    if ((m->flags & MECH_CARRYING) && s->cargo == NM_CARGO_HUNT
        && hq_pressed(n)) {
        /* (session 68) its HQ shot: the hunter carried a guard there. In
         * The owner's game of 30 October, 10:43 (taken over at 18
         * minutes) the jet fought the turtle's tanks at its HQ 50 s with
         * one aboard, buying no guard, and lost it */
        s->cargo = NM_CARGO_GUARD;
        s->reordered = false;
        return;
    }
    long score;
    HzRef threat = pick_threat(n, &score);
    bool home = !ref_is_none(threat) && threat_home(n, threat);
    bool not_yet = false;
    if (home && g_param[P_BUY_HOME] && vs_human(n) && threat.kind != REF_MECH
        && !hq_pressed(n) && score < g_param[P_HQ_ALARM]) {
        /* (session 77) the owner's game of 19 November (EISFREI, the
         * server's AI P1): from minute 17 to 31 it had $7,000 to
         * $10,700 and bought nothing, fighting his boats and GRM-34As
         * 470 px from its HQ, a fight by its HQ coming before any buy.
         * One not about to shoot the HQ waits for the buy, as one away
         * from it does */
        const HzUnit *tu = ref_unit_ptr_c(n->g, threat);
        not_yet = tu != NULL && !hq_soon(n, tu);
    }
    if (g_param[P_BUY_FIRST] && vs_human(n) && !ref_is_none(threat)
        && (!home || not_yet) && !(m->flags & MECH_CARRYING)
        && (s8)m->prod_state <= 0 && choose_buy(n)) {
        /* (session 68) the unit on its way first: a fight from home
         * with nothing bought kept it buying nothing for six minutes */
        s->bought_first++;
        if (home) {
            s->home_buys++;
        }
        set_task(n, NM_TASK_BUY);
        return;
    }
    if (g_param[P_BUY_HOME] >= 2 && vs_human(n) && !ref_is_none(threat)
        && (!home || not_yet) && !(m->flags & MECH_CARRYING)
        && m->prod_state == PROD_READY && !hq_pressed(n)
        && score < g_param[P_HQ_ALARM]
        && (!g_param[P_RESCUE] || ref_is_none(s->stuck))) {
        /* (session 77) ... and the unit waiting picked up first: bought
         * first, it waited at the HQ while the fight went on. Not with
         * a unit stalled (nightmare_think puts a pickup back to decide
         * for the rescue, which comes later here: on LOCH against the
         * turtle it chose the pickup again every frame and stood still
         * for minutes, 24,000 times in a game) */
        s->home_pickups++;
        pickup_ready(n);
        return;
    }
    if (g_param[P_READY_FIRST] > 0 && cpu_hard(n)
        && !(m->flags & MECH_CARRYING) && m->prod_state == PROD_READY
        && (s->prod_cargo == NM_CARGO_WALL || s->prod_cargo == NM_CARGO_GUARD)
        && score < g_param[P_HQ_ALARM]) {
        const HzPost *hq = post(n, own_hq(n));
        s->reordered = false;
        s->goal_post = (u8)pickup_base(n, hq->x, hq->y);
        set_task(n, NM_TASK_PICKUP);
        return;
    }
    if (!ref_is_none(threat) && m->hp >= 96 && m->ammo >= 8
        && (!(m->flags & MECH_CARRYING)
            || (score >= g_param[P_HQ_ALARM] && !guard_carried))) {
        s->target = threat;
        s->target_score = (s32)score;
        s->sieging = false;
        s->striking = false;
        s->fights++;
        set_task(n, NM_TASK_FIGHT);
        return;
    }
    if (m->flags & MECH_CARRYING) {
        if (s->cargo == NM_CARGO_LAUNCH
            && m->carried_order != ORDER_ATTACK_HQ) {
            if (!s->reordered) {
                /* (session 68) lifted from the rally point: its new order
                 * first */
                s->reordered = true;
                s->buy_type = (u8)g_param[P_RUSH_TYPE];
                s->buy_order = ORDER_ATTACK_HQ;
                set_task(n, NM_TASK_BUY);
                return;
            }
            s->cargo = NM_CARGO_GATHER;     /* it couldn't: back */
            s->launching = false;
        }
        /* (session 74, P_HOLD_BACK) an outpost of its own with a hold of
         * the enemy's in it is one to go to */
        bool held = s->cargo == NM_CARGO_CAPTURE
                    && g_param[P_HOLD_BACK] && vs_human(n)
                    && hold_lost(n, s->cargo_post) > 0;
        if (s->cargo == NM_CARGO_CAPTURE && s->cargo_post < HZ_POST_OUTPOSTS
            && !held
            && (post_ours(n, s->cargo_post)
                || (g_param[P_CAPTURE_ROBOT] && robot_held(n, s->cargo_post)))) {
            /* taken while it was on its way, or (session 75,
             * P_CAPTURE_ROBOT) his mech on foot by it: the next one */
            int next = pick_capture(n);
            if (next < 0 && g_param[P_CAPTURE_FREE] && vs_human(n)) {
                next = pick_capture_of(n, true);    /* (session 74) */
            }
            if (next >= 0) {
                s->cargo_post = (u8)next;
            }
        }
        if (s->cargo == NM_CARGO_CAPTURE && s->cargo_post < HZ_POST_OUTPOSTS
            && (!post_ours(n, s->cargo_post) || held)) {
            capture_drop(n, s->cargo_post, &s->final_x, &s->final_y);
            if (s->capture_post == s->cargo_post) {
                s->capture_sent++;
            }
        } else if (s->cargo == NM_CARGO_SUPPLY) {
            supply_drop(n, &s->final_x, &s->final_y);   /* (session 74) */
        } else if (s->cargo == NM_CARGO_WALL
                   && carried_unit(n->g, m)->type == UNIT_ST) {
            const HzPost *hq = post(n, own_hq(n));
            if (!home_water(n, &s->final_x, &s->final_y)
                && !water_near(n, hq->x - 16, hq->y - 16, 400, &s->final_x,
                               &s->final_y)) {
                wall_drop(n, &s->final_x, &s->final_y);
            }
        } else if (s->cargo == NM_CARGO_WALL) {
            wall_drop(n, &s->final_x, &s->final_y);
        } else if (s->cargo == NM_CARGO_GUARD) {
            guard_drop(n, &s->final_x, &s->final_y);
        } else if (s->cargo == NM_CARGO_FIELD) {
            /* (session 70) where it was going, kept (make_field) */
        } else if (s->cargo == NM_CARGO_POST
                   && s->cargo_post < HZ_POST_OUTPOSTS) {
            post_guard_drop(n, s->cargo_post, &s->final_x, &s->final_y);
        } else if (s->cargo == NM_CARGO_GATHER) {
            rally_drop(n, &s->final_x, &s->final_y);
        } else if (s->cargo == NM_CARGO_NEST) {
            nest_drop(n, &s->final_x, &s->final_y);
        } else if (s->cargo == NM_CARGO_HUNT) {
            int worth;
            if (!hunt_drop_at(n, true, &s->final_x, &s->final_y, &worth)
                && !hunt_drop_at(n, false, &s->final_x, &s->final_y,
                                 &worth)) {
                s->cargo = NM_CARGO_GUARD;      /* none: home */
                guard_drop(n, &s->final_x, &s->final_y);
            }
        } else if (s->cargo == NM_CARGO_LAUNCH) {
            launch_drop(n, &s->final_x, &s->final_y);
        } else {
            attack_drop(n, &s->final_x, &s->final_y);
        }
        set_task(n, NM_TASK_DELIVER);
        return;
    }
    if (start_dogfight(n, false)) {
        return;
    }
    if (m->form == HZ_FORM_ROBOT && m->transform == 0) {
        take_off(n);                /* up again */
        return;
    }
    if (launch_next(n)) {
        return;
    }
    if (g_param[P_RESCUE] && !ref_is_none(s->stuck)) {
        /* (session 68) an attack unit stalled: lifted, and put down again
         * where its drive passes */
        const HzUnit *u = ref_unit_ptr_c(n->g, s->stuck);
        if (u != NULL && u->slot < NM_WATCHED) {
            s->watch_lifted |= (u64)1 << u->slot;   /* once */
        }
        s->lift = s->stuck;
        s->stuck = ref_none();
        s->rescuing = true;
        set_task(n, NM_TASK_LIFT);
        return;
    }
    if (rehunt_next(n)) {
        return;
    }
    if (truck_lift_next(n)) {
        return;                     /* (session 75) */
    }
    if ((s8)m->prod_state <= 0 && choose_buy(n)) {
        set_task(n, NM_TASK_BUY);
        return;
    }
    const HzUnit *guard = NULL;
    int guards = g_param[P_SIEGE] ? siege_guards(n, &guard) : 0;
    if (m->prod_state != PROD_READY) {
        s->ready_at = 0;
    } else if (s->ready_at == 0) {
        s->ready_at = s->frames;
    }
    bool attack_ready = m->prod_state == PROD_READY
                        && s->prod_cargo == NM_CARGO_ATTACK;
    bool held_long = attack_ready
                     && s->frames - s->ready_at > (u32)g_param[P_SIEGE_HOLD];
    bool other_ready = m->prod_state == PROD_READY && !attack_ready;
    if (g_param[P_SIEGE] && guard != NULL && guards > g_param[P_SIEGE_CLEAR]
        && m->ammo >= 32 && !held_long && !other_ready) {
        const HzUnit *gu = guard;
        if (!fight_fuel_ok(n, gu)) {
            /* fuel first, at the base nearest the fight (unless that is
             * where it is, full: then it is out of reach) */
            int b = pickup_base(n, ucx(gu), ucy(gu));
            if (m->fuel < kidou(n)->stats.fuel * 9 / 10
                || base_dist(n, b, mx(n), my(n)) > 24) {
                s->goal_post = (u8)b;
                set_task(n, NM_TASK_SERVICE);
                return;
            }
        } else if (m->hp >= g_param[P_SIEGE_HP]) {
            s->target = unit_ref(guard);
            s->sieging = true;
            s->striking = false;
            s->fights++;
            set_task(n, NM_TASK_FIGHT);
            return;
        } else if (attack_ready) {  /* heal first, the unit waits */
            s->goal_post = (u8)nearest_base(n, mx(n), my(n));
            set_task(n, NM_TASK_SERVICE);
            return;
        }
    }
    if (m->prod_state == PROD_READY) {
        pickup_ready(n);
        return;
    }
    if (m->hp >= 96 && m->ammo >= 8 && hq_calm(n)) {
        /* (session 74, P_STRIKE) nothing to buy, carry or fight, its HQ
         * not shot in the last minute: the enemy's nest by its bases.
         * Worth 1, so anything pick_threat finds takes the mech off it
         * (interrupts). With its HQ being shot it stays: taken over at 17
         * minutes in his game of 14 November, the turtle for him, it lost
         * 27 of 32 striking whenever it had nothing to buy (none
         * without), the mech away or being built again as his tanks came. */
        HzRef strike = pick_strike(n);
        if (!ref_is_none(strike)) {
            s->target = strike;
            s->target_score = 1;
            s->sieging = false;
            s->striking = true;
            s->strike = strike;
            s->fights++;
            s->strikes++;
            set_task(n, NM_TASK_FIGHT);
            return;
        }
    }
    if (start_dogfight(n, true)) {
        return;
    }
    /* nothing to do: over the nearest base (which tops it up), looking
     * again every few frames */
    s->goal_post = (u8)wait_base(n);
    set_task(n, NM_TASK_WAIT);
}

/* What breaks into the task under way (every 15 frames): the fuel, the
 * enemy jet carrying near by, the HQ under attack. */
static void interrupts(Nm *n)
{
    HzMech *m = n->m;
    HzNightmare *s = n->s;
    int task = s->task;
    if (task == NM_TASK_BUY || task == NM_TASK_DECIDE) {
        return;
    }
    if (needs_service(n) && task != NM_TASK_SERVICE) {
        if (task == NM_TASK_DELIVER && s->stage == 2
            && (m->flags & MECH_CARRYING) && s->bounces < 255) {
            s->bounces++;
        }
        s->goal_post = (u8)service_base(n);
        set_task(n, NM_TASK_SERVICE);
        return;
    }
    if (task == NM_TASK_SERVICE || task == NM_TASK_DOGFIGHT) {
        return;
    }
    if ((task == NM_TASK_PICKUP && s->prod_cargo == NM_CARGO_GUARD)
        || (task == NM_TASK_DELIVER && s->cargo == NM_CARGO_GUARD)) {
        return;     /* (session 68) a guard, near home: put down first */
    }
    if (task != NM_TASK_FIGHT && start_dogfight(n, task == NM_TASK_WAIT)) {
        return;
    }
    long score;
    HzRef threat = pick_threat(n, &score);
    if (task == NM_TASK_FIGHT) {
        /* a siege fight gives way to the HQ's alarm; a fight at home to
         * a threat half as great again as the one it took on */
        bool switch_to = s->sieging ? score >= g_param[P_HQ_ALARM]
                                    : (!ref_eq(threat, s->target)
                                       && score * 2 > (long)s->target_score
                                                      * 3);
        if (!switch_to) {
            return;
        }
    }
    if (ref_is_none(threat) || m->hp < 96 || m->ammo < 8) {
        return;
    }
    /* carrying: only for the HQ itself (the unit is put down first) */
    if ((m->flags & MECH_CARRYING) && score < g_param[P_HQ_ALARM]) {
        return;
    }
    s->target = threat;
    s->target_score = (s32)score;
    s->sieging = false;
    s->striking = false;
    s->fights++;
    set_task(n, NM_TASK_FIGHT);
}

/* Two jets that touch bounce apart (Mechs_JetCollide: within $1C on
 * both axes). Heading into the enemy jet from close by: sideways instead,
 * the way that keeps the most of the course (not in a dogfight, which
 * keeps its own distance). */
static void avoid_jet(Nm *n)
{
    const HzMech *e = n->rival;
    const HzMech *m = n->m;
    if (n->s->task == NM_TASK_DOGFIGHT || m->form == HZ_FORM_ROBOT
        || (e->flags & MECH_DESTROYED) || e->form == HZ_FORM_ROBOT) {
        return;
    }
    int dx = e->x - m->x, dy = e->y - m->y;
    if (iabs(dx) >= 52 || iabs(dy) >= 52) {
        return;
    }
    int px = ((n->pad & HZ_PAD_RIGHT) ? 1 : 0) - ((n->pad & HZ_PAD_LEFT) ? 1 : 0);
    int py = ((n->pad & HZ_PAD_DOWN) ? 1 : 0) - ((n->pad & HZ_PAD_UP) ? 1 : 0);
    if (px * dx + py * dy <= 0) {
        return;
    }
    /* the two sideways ways; the one away from it */
    int sx = -py, sy = px;
    if (sx * dx + sy * dy > 0 || (sx == 0 && sy == 0)) {
        sx = py;
        sy = -px;
    }
    u8 dirs = 0;
    if (sx > 0) {
        dirs |= HZ_PAD_RIGHT;
    } else if (sx < 0) {
        dirs |= HZ_PAD_LEFT;
    }
    if (sy > 0) {
        dirs |= HZ_PAD_DOWN;
    } else if (sy < 0) {
        dirs |= HZ_PAD_UP;
    }
    n->pad = (u8)((n->pad & ~HZ_PAD_DIRS) | dirs);
}

/* Whatever it is doing, a jet whose heading would hit the enemy jet
 * fires (a carrying jet can shoot too). Not with the menu open (B buys
 * there), nor with A pressed (A wins). */
static void shoot_in_passing(Nm *n)
{
    HzMech *m = n->m;
    if (m->form == HZ_FORM_ROBOT || m->transform != 0 || menu_open(m)
        || !rival_jet(n) || (n->pad & (HZ_PAD_A | HZ_PAD_B))
        || m->ammo == 0 || (m->pad_prev & HZ_PAD_B)
        || m->fire_delay != 0) {
        return;
    }
    if (chebyshev(n->rival->x - m->x, n->rival->y - m->y) < 160
        && air_hits(n, m->dir)) {
        n->pad |= HZ_PAD_B;
        n->s->shots++;
    }
}

/* (Session 68) The unit aboard put down where the jet is, whatever it was
 * for, when the jet is about to go down: its HP under P_BAIL_HP, hit in the
 * last NM_BAIL_HURT frames or in missile reach, and the ground under it
 * takes the unit. Shot down carrying, the unit goes with it; put down, it
 * fights where it stands, or is lifted again (P_RESCUE). */
#define NM_BAIL_HURT 60

static void bail_out(Nm *n)
{
    const HzMech *m = n->m;
    HzNightmare *s = n->s;
    if (g_param[P_BAIL_HP] <= 0 || !(m->flags & MECH_CARRYING)
        || m->hp >= g_param[P_BAIL_HP]) {
        return;
    }
    bool hurt = s->hurt_at != 0 && s->frames - s->hurt_at < NM_BAIL_HURT;
    if (!hurt && !(danger_at(n, m->x + 16, m->y + 16) & DANGER_MISSILE)) {
        return;
    }
    if (!droppable(n, m->x, m->y)) {
        return;
    }
    n->pad &= (u8)~(HZ_PAD_A | HZ_PAD_B | HZ_PAD_C);
    if (press(n, HZ_PAD_A)) {
        s->bailed++;
        if (s->cargo == NM_CARGO_HUNT) {
            note_hunter(n);     /* most go down so, by the SAM-42s */
        }
    }
}

/* -- the think ---------------------------------------------------------- */

void nightmare_think(HzGame *g, int p)
{
    Nm n;
    n.g = g;
    n.m = &g->mech[p];
    n.rival = &g->mech[1 - p];
    n.s = &g->nightmare[p];
    n.p = p;
    n.team = (n.m->flags & MECH_TEAM2) ? 1 : 0;
    n.cpu = (n.m->flags & MECH_CPU) != 0;
    n.pad = 0;
    n.danger = g_danger[p];
    n.boat_scan = -1;
    n.missiles = g_missiles[p];
    static u32 serial;
    n.serial = ++serial != 0 ? serial : ++serial;
    params_init(p);
    g_param = g_params[p];
    HzNightmare *s = n.s;
    if (s->frames == 0) {
        s->capture_post = 0xFF;
        s->rng = (g->rng_seed ^ (0x9E3779B9u * (u32)(p + 1))) | 1u;
    }
    s->frames++;
    s->age++;
    s->wait++;

    track_units(&n);
    watch_units(&n);
    learn_spots(&n);
    raid_watch(&n);
    hold_watch(&n);
    if (g_param[P_RESCUE] && !ref_is_none(s->stuck)
        && (s->task == NM_TASK_WAIT
            || (s->task == NM_TASK_PICKUP
                && s->prod_cargo != NM_CARGO_GUARD))) {
        /* the stalled unit first; not before a guard, which decide puts
         * first (on EISFREI, TYPE D, it chose the guard again every frame
         * and never flew while one TAX-52 shot its HQ from 1,024 to 0) */
        set_task(&n, NM_TASK_DECIDE);
    }
    if (n.m->flags & MECH_DESTROYED) {
        if (!s->was_dead) {
            s->deaths++;
            s->grave_x[s->grave_next] = (s16)(n.m->x + 16);
            s->grave_y[s->grave_next] = (s16)(n.m->y + 16);
            s->grave_at[s->grave_next] = s->frames | 1;
            s->grave_next = (u8)((s->grave_next + 1) % NM_GRAVES);
            if (n.m->fuel == 0) {
                s->death_fuel++;
            }
            if (n.m->flags & MECH_CARRYING) {
                s->death_carrying++;
            }
            if (n.m->form == HZ_FORM_ROBOT) {
                s->death_robot++;
            }
            if (s->task == NM_TASK_FIGHT && g_param[P_FIGHT_ON]
                && vs_human(&n)
                && s->hp_was < kidou(&n)->stats.hp / 3) {
                s->fought_on++;         /* (session 74) */
            }
            if (s->task == NM_TASK_FIGHT && s->striking
                && g_param[P_STRIKE_GIVEUP] > 0 && vs_human(&n)
                && ++s->strike_row >= g_param[P_STRIKE_GIVEUP]) {
                /* (session 77) destroyed on strikes again and again with
                 * nothing killed: that nest left a while */
                s->strike_row = 0;
                s->strike_giveups++;
                if (s->strike_giveup_row < 0xFF) {
                    s->strike_giveup_row++;
                }
                s->strike_rest_until = s->frames
                                       + (u32)g_param[P_STRIKE_REST]
                                         * (u32)imin(s->strike_giveup_row, 5);
                s->strike_rest_x = (s16)(n.m->x + 16);
                s->strike_rest_y = (s16)(n.m->y + 16);
            }
        }
        s->was_dead = true;
        set_task(&n, NM_TASK_DECIDE);
        n.m->pad = 0;
        return;
    }
    s->was_dead = false;
    {
        const HzMech *e = n.rival;
        if (n.m->hp < s->hp_was && n.m->form != HZ_FORM_ROBOT
            && !(e->flags & MECH_DESTROYED) && e->form != HZ_FORM_ROBOT
            && chebyshev(e->x - n.m->x, e->y - n.m->y) < 160) {
            s->jet_hit_at = s->frames;
        }
        if (n.m->hp < s->hp_was) {
            s->hurt_at = s->frames;
        }
        s->hp_was = n.m->hp;
    }
    if (s->task < NM_TASK_SLOTS) {
        s->task_frames[s->task]++;
    }
    danger_build(&n);

    if ((s->frames % 15) == 0) {
        interrupts(&n);
    }
    switch (s->task) {
    case NM_TASK_BUY:
        task_buy(&n);
        break;
    case NM_TASK_PICKUP:
        task_pickup(&n);
        break;
    case NM_TASK_DELIVER:
        task_deliver(&n);
        break;
    case NM_TASK_SERVICE:
        task_service(&n);
        break;
    case NM_TASK_WAIT:
        task_wait(&n);
        break;
    case NM_TASK_FIGHT:
        task_fight(&n);
        break;
    case NM_TASK_DOGFIGHT:
        task_dogfight(&n);
        break;
    case NM_TASK_LIFT:
        task_lift(&n);
        break;
    default:
        decide(&n);
        break;
    }
    avoid_jet(&n);
    shoot_in_passing(&n);
    bail_out(&n);
    n.m->pad = n.pad;
}

/* -- thinking ahead (session 64) --------------------------------------- */

/* The copy run ahead, and where it is. */
typedef struct {
    HzGame          game;
    Vdp             vdp;
    int             p;
    int             ahead;
    int             at;         /* passes ended in the copy */
    const u8 *const *pads;
    bool            thought;
    u8              want;
} Ahead;

static Ahead *g_ahead;

/* (Session 68) A think in the copy run ahead by a side not thinking
 * ahead in it (a nightmare CPU rival): thrown away. */
static bool thrown_away(const Nm *n)
{
    return g_ahead != NULL && n->g == &g_ahead->game && n->p != g_ahead->p;
}

static void pad_set(HzPad *to, u8 bits)
{
    to->up = (bits & HZ_PAD_UP) != 0;
    to->down = (bits & HZ_PAD_DOWN) != 0;
    to->left = (bits & HZ_PAD_LEFT) != 0;
    to->right = (bits & HZ_PAD_RIGHT) != 0;
    to->a = (bits & HZ_PAD_A) != 0;
    to->b = (bits & HZ_PAD_B) != 0;
    to->c = (bits & HZ_PAD_C) != 0;
    to->start = (bits & HZ_PAD_START) != 0;
}

/* The copy's VBlank hand-over: the next pass's pads; once it has thought
 * (or a pause runs on), the copy stops. */
static void ahead_frame(HzGame *g, void *user)
{
    Ahead *a = user;
    a->at++;
    if (a->thought || a->at > a->ahead + NM_AHEAD_MAX) {
        g->quit = true;
        return;
    }
    int i = (a->at <= a->ahead) ? a->at : a->ahead;
    for (int s = 0; s < 2; s++) {
        pad_set(&g->pad_in[s], a->pads[s][i]);
    }
}

static void ahead_mech(HzGame *g, int p, bool after, void *user)
{
    Ahead *a = user;
    if (!after && p == a->p && a->at == a->ahead && !a->thought) {
        nightmare_think(g, p);
        a->want = g->mech[p].pad;
        a->thought = true;
    }
}

u8 nightmare_think_ahead(HzGame *g, int p, int ahead, const u8 *const pads[2])
{
    if (g_ahead == NULL) {
        g_ahead = calloc(1, sizeof *g_ahead);
        if (g_ahead == NULL) {
            return 0;
        }
    }
    Ahead *a = g_ahead;
    HzGame *c = &a->game;
    game_copy(c, g);
    vdp_copy(&a->vdp, g->vdp);
    c->vdp = &a->vdp;           /* the copy draws on its own */
    c->frame = ahead_frame;
    c->frame_user = a;
    c->mech_probe = ahead_mech;
    c->mech_probe_user = a;
    c->combat_probe = NULL;
    c->unit_ai_probe = NULL;
    c->mech_ai_probe = NULL;
    c->front_probe = NULL;
    c->snd_post = NULL;         /* nothing heard */
    c->snd_music_on = NULL;
    c->quit = false;
    a->p = p;
    a->ahead = ahead;
    a->at = 0;
    a->pads = pads;
    a->thought = false;
    a->want = 0;
    for (int s = 0; s < 2; s++) {
        pad_set(&c->pad_in[s], pads[s][0]);
    }
    while (!a->thought && game_frame(c, true) == HZ_LOOP_ON) {
    }
    if (a->thought) {
        nightmare_copy(&g->nightmare[p], &c->nightmare[p]);
    }
    return a->want;
}

HzGame *nightmare_ahead_copy(void)
{
    return (g_ahead != NULL) ? &g_ahead->game : NULL;
}

const char *nightmare_task_name(const HzGame *g, int p)
{
    u8 t = g->nightmare[p].task;
    return TASK_NAME[t < NM_TASK_COUNT ? t : 0];
}

const char *nightmare_summary(const HzGame *g, int p)
{
    static char text[1400];
    const HzNightmare *s = &g->nightmare[p];
    unsigned spots = 0;
    for (int k = 0; k < NM_SPOTS; k++) {
        spots += s->spot_hits[k] > 0;
    }
    int len = 0;
    for (int t = 0; t < NM_TASK_COUNT && t < NM_TASK_SLOTS; t++) {
        len += snprintf(text + len, sizeof text - (size_t)len, "%s %u ",
                        TASK_NAME[t], s->task_frames[t]);
    }
    snprintf(text + len, sizeof text - (size_t)len,
             "deaths %u fuel %u carrying %u robot %u kills %u dogkills %u "
             "delivered %u capsent %u buys %u walled %u unstuck %u "
             "gathered %u groups %u launched %u drives %u bad %u "
             "stalled %u rescued %u nests %u hot %u guarded %u spots %u "
             "converted %u gaveup %u unreachable %u boats %u turrets %u "
             "naval %u kept %u bailed %u buyfirst %u bounced %u "
             "colds %u atlaskept %u hunts %u rehunts %u parks %u flydrops %u "
             "flymisses %u lostinrow %u futilebuys %u escorts %u "
             "pathfuel %u homeposts %u postguards %u dogguards %u "
             "raidguards %u waitmoves %u idledogs %u holdbuys %u "
             "freebuys %u supplies %u strikes %u strikekills %u "
             "foughton %u holdgaveup %u supplybuys %u repairbuys %u "
             "nobodybuys %u theirbuys %u lanedrops %u yarddrops %u "
             "trucklifts %u homebuys %u homepickups %u strikegiveups %u",
             s->deaths, s->death_fuel, s->death_carrying, s->death_robot,
             s->kills, s->rival_kills, s->delivered, s->captures_sent,
             s->buys, s->walled, s->unstuck, s->gathered, s->groups,
             s->launched, s->drives, s->drives_bad, s->stalled,
             s->rescued, s->nests, s->hot_goals, s->guarded, spots,
             s->converted, s->cap_given_up, s->unreachable,
             s->guard_boats, s->guard_turrets, s->naval, s->kept_home,
             s->bailed, s->bought_first, s->bounced, s->colds,
             s->atlas_kept, s->hunts, s->rehunts, s->parks, s->fly_drops,
             s->fly_missed, s->att_lost_most, s->futile_buys, s->escorts,
             s->path_fuel, s->home_posts, s->post_guarded, s->dog_guards,
             s->raid_guards, s->wait_moves, s->idle_dogs, s->hold_buys,
             s->free_buys, s->supplies, s->strikes, s->strike_kills,
             s->fought_on, s->hold_given_up, s->bought[UNIT_SUPPLY],
             s->bought[UNIT_REPAIR], s->nobody_buys, s->their_buys,
             s->lane_drops, s->yard_drops, s->truck_lifts, s->home_buys,
             s->home_pickups, s->strike_giveups);
    return text;
}

const char *nightmare_describe(const HzGame *g, int p)
{
    static char text[400];
    const HzNightmare *s = &g->nightmare[p];
    const HzMech *m = &g->mech[p];
    int len = snprintf(text, sizeof text, "[%s/%u goal %d,%d final %d,%d "
                       "post %u buys %u deaths %u kills %u shots %u fights "
                       "%u dog %u/%u cap %d/%u prod %02X dir %u pad %02X/%02X "
                       "tr %02X v %d,%d gr %d rally %d,%d%s]",
                       TASK_NAME[s->task < NM_TASK_COUNT ? s->task : 0],
                       s->stage, s->goal_x, s->goal_y, s->final_x,
                       s->final_y, s->goal_post, s->buys, s->deaths,
                       s->kills, s->shots, s->fights, s->dogfights,
                       s->rival_kills, s->capture_post == 0xFF ? -1
                       : s->capture_post, s->capture_sent, m->prod_state,
                       m->dir, m->pad, m->pad_prev, m->transform, m->vel_x,
                       m->vel_y, mech_ground_at(g, m, m->x, m->y),
                       s->rally_set ? s->rally_x : -1,
                       s->rally_set ? s->rally_y : -1,
                       s->launching ? " going" : "");
    const HzUnit *tu = ref_unit_ptr_c(g, s->target);
    if (s->target.kind == REF_MECH && len > 0 && len < (int)sizeof text) {
        const HzMech *e = &g->mech[1 - p];
        snprintf(text + len, sizeof text - (size_t)len,
                 " tgt mech: hp %u at %d,%d", e->hp, e->x, e->y);
    } else if (tu != NULL && len > 0 && len < (int)sizeof text) {
        snprintf(text + len, sizeof text - (size_t)len,
                 " tgt %u.%lu: t%u o%X hp %u at %d,%d", tu->side,
                 (unsigned long)tu->slot, tu->type, tu->order, tu->hp,
                 tu->x, tu->y);
    }
    return text;
}
