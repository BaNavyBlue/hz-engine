#ifndef HZ_NIGHTMARE_H
#define HZ_NIGHTMARE_H

/* (Session 57) The nightmare AI (nightmare.c): a CPU mech's brain in
 * place of the original's (mech_ai.c), for HzGame.cpu_brain. Like the
 * original's its only output is the mech's pad; unlike it, it never
 * touches its money or its heading. tools/hz_arena.c also drives a
 * human's side with it. */

#include "../core/hz_types.h"
#include "ref.h"

#include <stdbool.h>

typedef struct HzGame HzGame;

/* HzGame.cpu_brain */
#define HZ_BRAIN_ORIGINAL  0
#define HZ_BRAIN_NIGHTMARE 1
#define HZ_BRAIN_PROBE     2    /* a test seam: HzGame.brain_probe thinks
                                   (tools/hz_arena.c's second nightmare) */

#define NM_PATH_MAX 96          /* cells in a planned flight */
#define NM_TASK_SLOTS 12        /* room for its tasks' frame counts */

/* (Session 61) An enemy unit as seen: where it was NM_TRACK_FRAMES ago and
 * the velocity since (8.8 px a frame) -- a unit's own velocity field can
 * hold a stale value while it stands; its flags then, to see a new unit
 * in the slot. (Session 68) Its HP and missiles then, and the frame they
 * last went up (repaired: by an outpost, a SUPPLY or lifted to one). */
typedef struct {
    s16 x, y;
    s16 vx, vy;
    u8  life;
    u8  missiles;
    u16 hp;
    u32 tended_at;
} NmTrack;

/* (Session 68) Its attack units looked at for a stall: the first slots
 * (HzNightmare.watch_lifted's bits) */
#define NM_WATCHED 64

/* (Session 68) Places enemy units shot its HQ from, kept */
#define NM_SPOTS 4

/* (Session 68) Enemy units it found it couldn't fight from anywhere,
 * kept a while */
#define NM_NOFIGHT 4

/* (Session 68) Places the mech was destroyed, kept a while */
#define NM_GRAVES 4

/* (Session 68) Attack drops waiting to be judged, and drops judged cold */
#define NM_DROPLOG 4
#define NM_COLD    6

/* (Session 68) Hunters (P_HUNT) it keeps track of, to lift on */
#define NM_HUNTERS 4

/* (Session 68) HZ_POST_OUTPOSTS (game.h) */
#define NM_OUTPOSTS 16

/* What it keeps for one mech, a new game's all 0 (HzGame.nightmare[p],
 * in the RAM a new game clears) but `track`, its list of what it has
 * seen of each enemy slot, which nightmare_reset keeps (emptied). */
typedef struct {
    u32 frames;             /* frames it has thought */
    u8  task;               /* NM_TASK_* */
    u8  stage;              /* the task's step */
    u16 wait;               /* frames in this stage */
    u16 age;                /* frames in this task */
    s16 goal_x, goal_y;     /* where the task goes (the mech's x, y) */
    s16 final_x, final_y;   /* a delivery's last stop */
    u8  goal_post;          /* the record it goes to */
    HzRef target;           /* a fight's: an enemy unit, the enemy mech on
                               foot, or none */
    s32 target_score;       /* how much it was worth when chosen */
    u8  buy_type, buy_order;/* what the order menu is set to buy */
    u8  cargo;              /* what the carried unit is for (NM_CARGO_*) */
    u8  cargo_post;         /* a capture's outpost */
    u8  prod_cargo;         /* the same for the unit on order */
    u8  prod_post;
    u8  capture_post;       /* the outpost being taken, $FF none */
    u8  capture_sent;       /* INFANTRY sent at it */
    bool sieging;           /* the fight is at the enemy HQ */
    u32 ready_at;           /* the frame the unit on order was ready */
    u16 attack_buys;        /* attack units bought */
    u32 jet_hit_at;         /* the last frame the enemy jet's gun hit it */
    u16 hp_was;             /* its HP last think */
    u32 rng;                /* its own random numbers (xorshift), from
                               the game's seed: not the game's Random */
    u32 front_at;           /* when front_needed last looked */
    bool front_need;
    NmTrack *track;         /* by the enemy's slot, track_cap of them */
    u32  track_cap;
    u8  deaths;
    bool was_dead;
    /* the planned flight: cells (y << 6 | x), from path_at on */
    u16 path[NM_PATH_MAX];
    u8  path_len, path_at;
    s16 path_gx, path_gy;   /* the goal it was planned for */
    u16 path_age;           /* frames since it was planned */
    u16 path_full;          /* (session 68) its cells, all of them */
    bool path_straight;     /* (P_PATH_FUEL) straight: the way round
                               longer than the fuel */
    /* the record */
    u16 bought[16];         /* units bought, by type */
    u16 buys;
    u16 kills;              /* its targets that died while it fought */
    u16 shots;
    u16 fights, dogfights;
    u16 rival_kills;        /* the rival destroyed while it chased it */
    u16 hits_taken;
    u32 task_frames[NM_TASK_SLOTS]; /* frames in each task (NM_TASK_*) */
    u8  death_fuel;         /* deaths with the tank empty */
    u8  death_carrying;     /* deaths with a unit aboard */
    u8  death_robot;        /* deaths on foot */
    u16 delivered;          /* units put down for an attack */
    u16 captures_sent;      /* INFANTRY put down to take outposts */
    /* (Session 67) style 1, a player's way: the wall round its HQ */
    bool pushing;           /* the wall is up: attack units bought */
    u16 walled;             /* units put down in the wall */
    /* (Session 67) a robot walking and not moving in a fight */
    s16 stuck_x, stuck_y;   /* where it stood */
    u8  stuck_n;            /* frames it has stood there walking */
    u32 stuck_at;           /* the frame it gave up there (0 never) */
    u16 unstuck;            /* times it did */
    u8  hover_n;            /* (session 68) frames the jet has hovered at
                               a fight's landing spot, unable to land */
    /* (Session 68) a robot on low ground walking to take off */
    s16 toff_x, toff_y;     /* the plain land it walks to */
    s16 toff_from_x, toff_from_y;   /* where it stood when it chose it */
    u32 toff_at;            /* the frame it chose it (0 none) */
    s16 toff_bad_x, toff_bad_y;     /* one it didn't get nearer */
    u32 toff_bad_at;        /* the frame it gave that up (0 none) */
    /* (Session 68) attack units gathered, then sent in together */
    s16 rally_x, rally_y;   /* where they stand (the mech's x, y) */
    bool rally_set;
    bool launching;         /* the group goes in: each one lifted, its
                               order changed to attack HQ, put down */
    bool reordered;         /* the carried one's new order tried */
    HzRef lift;             /* the gathered unit it lifts next */
    HzRef lead;             /* the first of the group sent */
    u16 gathered;           /* units put down at the rally point */
    u16 groups;             /* groups sent */
    u16 launched;           /* units sent from it */
    /* (Session 68) attack drops tried with a drive in a copy of the game */
    u16 drives;             /* places tried */
    u16 drives_bad;         /* ... where the unit never hit the HQ */
    /* (Session 68) its attack units, looked at once a second (the first
     * NM_WATCHED slots): one that stalls (its fuel going, the unit not)
     * is lifted and put down again */
    s16 watch_x[NM_WATCHED];    /* where each was a second ago */
    s16 watch_y[NM_WATCHED];
    u16 watch_fuel[NM_WATCHED]; /* ... and its fuel */
    u8  watch_still[NM_WATCHED];/* seconds in a row it stalled */
    u64 watch_lifted;       /* a bit a slot: its unit lifted (or tried
                               for) once already, not again */
    HzRef stuck;            /* the stalled one to lift; none: none */
    bool rescuing;          /* NM_TASK_LIFT lifts it */
    u16 drop_hover;         /* thinks the attack drop waited on its drive */
    u16 stalled;            /* units seen stalled */
    u16 rescued;            /* ... lifted again */
    /* (Session 68) out of missile reach (P_SAFE_DROP), and the enemy's
     * SAMs and GRMs gone for (P_NEST) */
    bool nest_goal;         /* the drop's place the nest test's */
    u16 nests;              /* attack units put down at one */
    u16 hot_goals;          /* drops planned again, missiles at the goal */
    /* (Session 68) where enemy units stood as its HQ lost HP, and the
     * guards kept there (P_DEFEND) */
    s16 spot_x[NM_SPOTS];   /* their centres */
    s16 spot_y[NM_SPOTS];
    u16 spot_hits[NM_SPOTS];/* HQ hits seen from there, 0 an empty one */
    u16 hq_hp_was;          /* its HQ's HP last think */
    u32 hq_hit_at;          /* the think it last went down (0 never) */
    u16 guarded;            /* guards put down */
    u16 converted;          /* attack units, ready, made guards */
    /* (Session 68) outposts it sent INFANTRY at and didn't take
     * (P_CAPTURE_REST) */
    u8  cap_fails[NM_OUTPOSTS];  /* times, by outpost */
    u32 cap_rest[NM_OUTPOSTS];   /* none sent before this frame */
    u16 cap_given_up;            /* times in all */
    u8  cap_row;                 /* (P_CAPTURE_ALL) failures in a row */
    u32 attack_at;               /* (P_ATTACK_GAP) the last attack unit
                                    put down (0 none yet) */
    u32 cap_rest_all;            /* ... none at all before this frame */
    /* (Session 68) a fight's target with nowhere to land by it (a boat
     * out on the water), not taken on again for a while */
    HzRef nofight[NM_NOFIGHT];
    u32 nofight_at[NM_NOFIGHT];  /* the frame it was found so */
    u8  nofight_next;            /* the slot the next goes in */
    u16 unreachable;             /* fights given up so */
    u16 guard_boats;             /* (P_DEFEND) boats put down as guards */
    u16 guard_turrets;           /* ... and turrets */
    u16 naval;                   /* (P_NAVAL) boats bought to attack */
    u16 nest_buys;               /* (P_NEST_UNITS) bought for the nest */
    u16 kept_home;               /* (P_NO_BLIND, (s. 70) P_REGION) attack
                                    units made guards */
    u8  mix_buys;                /* (P_ATTACK_MIX) its turns counted:
                                    (session 73, P_MIX_BUYS) the units
                                    bought with the garrison half up */
    bool buy_mix;                /* ... the unit choose_buy chose is one */
    u32 hurt_at;                 /* (P_BAIL_HP) the last frame it lost HP */
    u16 bailed;                  /* ... units put down before it went */
    /* (Session 68, P_DEATH_AVOID) where the mech was destroyed: its
     * centre, and the frame (0 none) */
    s16 grave_x[NM_GRAVES], grave_y[NM_GRAVES];
    u32 grave_at[NM_GRAVES];
    u8  grave_next;              /* the slot the next goes in */
    u16 bought_first;            /* (P_BUY_FIRST) buys before a fight */
    u8  bounces;                 /* (P_BOUNCES) the unit carried: turned
                                    back for fuel on its last leg */
    u16 bounced;                 /* ... units made guards so */
    /* (Session 68, P_COLD_JUDGE) attack units put down: where (the mech's
     * x, y), when (0 none) and the enemy HQ's HP then */
    s16 drop_x[NM_DROPLOG], drop_y[NM_DROPLOG];
    u32 drop_at[NM_DROPLOG];
    u16 drop_hq[NM_DROPLOG];
    u8  drop_next;
    /* ... places the enemy HQ took nothing in the time after (0 none) */
    s16 cold_x[NM_COLD], cold_y[NM_COLD];
    u32 cold_at[NM_COLD];
    u8  cold_next;
    u16 colds;                   /* ... in all */
    u16 atlas_kept;              /* (P_ATLAS_LINE) attack units made
                                    guards, no place the atlas passes */
    u8  since_hunt;              /* (P_HUNT) attack units bought since the
                                    last hunter */
    u16 hunts;                   /* ... hunters put down */
    HzRef hunter[NM_HUNTERS];    /* ... the last put down */
    u8  hunter_next;
    bool rehunting;              /* NM_TASK_LIFT lifts one on */
    u16 rehunts;                 /* ... times it did */
    u16 parks;                   /* (P_PARK) attack units carried on half
                                    a tank put down where it takes them */
    bool boat_seen;              /* (P_BOAT_SEEN) an enemy boat on the map
                                    once */
    bool fly_near;               /* (P_DROP_FLY) the drop flown over at
                                    speed: within reach of its place */
    u8  fly_misses;              /* ... and past it with the unit */
    u16 fly_drops;               /* ... units put down so */
    u16 fly_missed;              /* ... passes by with the unit */
    /* (Session 68, P_FUTILE) its attack units destroyed since the enemy
     * HQ last lost HP */
    u64 att_alive;               /* a bit a slot (NM_WATCHED): an attack
                                    unit there a second ago */
    u16 ehq_hp_was;              /* the enemy HQ's HP then */
    u16 att_lost;                /* ... destroyed since it went down */
    u32 ehq_hit_at;              /* ... the frame it last went down */
    u16 att_lost_most;           /* ... the most in a row */
    u16 futile_buys;             /* garrison units bought so */
    /* (Session 68, P_CAPTURE_ESCORT) the enemy mech seen on foot by each
     * outpost not its own: the frame (0 never) */
    u32 robot_at[NM_OUTPOSTS];
    u32 escort_at[NM_OUTPOSTS];  /* ... the last TAX-52 bought for it */
    u16 escorts;                 /* ... TAX-52s bought to go first */
    u16 path_fuel;               /* (P_PATH_FUEL) flights made straight */
    u16 home_posts;              /* (P_HOME_POST) outposts by its HQ gone for */
    u16 post_guarded;            /* (P_POST_GUARD) guards put down by its outposts */
    /* (Session 69) */
    bool dog_guard;              /* (P_DOG_GUARD) the chase is of the jet
                                    carrying by its bases: over once the
                                    unit is down */
    u16 dog_guards;              /* ... chases so */
    bool dog_idle;               /* (P_IDLE_DOG) the chase is of the jet
                                    with nothing else to do */
    u16 idle_dogs;               /* ... chases so */
    u32 raid_at[NM_OUTPOSTS];    /* (P_POST_RAID) the frame the enemy's
                                    INFANTRY was last seen by each outpost
                                    of its own (0 never) */
    u16 raid_guards;             /* ... TAX-52s bought for them */
    u16 wait_moves;              /* (P_WAIT_NEAR) waits not over its
                                    nearest base */
    /* (Session 74) */
    u16 hold_buys;               /* (P_HOLD_BACK) INFANTRY bought to put a
                                    hold back in an outpost of its own */
    u32 hold_buy_at[NM_OUTPOSTS];/* ... the last for each (0 none) */
    u8  hold_out[NM_OUTPOSTS];   /* ... bought for it within NM_HOLD_WAIT
                                    of that: on their way */
    u8  hold_seen[NM_OUTPOSTS];  /* ... the enemy's holds in it when the
                                    first of those was bought */
    u8  hold_low[NM_OUTPOSTS];   /* ... and the fewest seen since */
    u8  hold_fails[NM_OUTPOSTS]; /* ... lots in a row that put none back */
    u32 hold_rest[NM_OUTPOSTS];  /* ... no more for it before this frame */
    u16 hold_given_up;           /* ... times it gave one up so */
    u16 free_buys;               /* (P_CAPTURE_FREE) INFANTRY bought for an
                                    outpost that was nobody's, out of
                                    CAPTURE_REACH */
    u16 supplies;                /* (P_SUPPLY) SUPPLYs put down by its HQ */
    bool striking;               /* (P_STRIKE) the fight is a strike at the
                                    enemy's nest */
    HzRef strike;                /* ... the unit it goes for till it is
                                    gone */
    u16 strikes;                 /* ... fights so */
    u16 strike_kills;            /* ... that ended with the unit destroyed */
    u16 fought_on;               /* (P_FIGHT_ON) fights not left for repair */
    u16 nobody_buys;             /* (session 75) INFANTRY bought for an
                                    outpost that was nobody's */
    u16 their_buys;              /* ... for one that was the enemy's */
    u16 lane_drops;              /* (session 75, P_HOME_LANES) units put
                                    down on a block place by its HQ */
    u16 yard_drops;              /* ... trucks in the ring round it */
    u16 truck_lifts;             /* ... trucks lifted to be put there */
    u32 truck_lift_at;           /* ... the frame of the last */
    bool retrucking;             /* ... NM_TASK_LIFT lifts one */
    /* (Session 77) */
    u16 home_buys;               /* (P_BUY_HOME) units bought before a fight
                                    by its HQ with an enemy unit not yet at
                                    it */
    u16 home_pickups;            /* ... units waiting picked up so */
    u8  strike_row;              /* (P_STRIKE_GIVEUP) deaths in a row on
                                    strikes with nothing killed */
    u16 strike_giveups;          /* ... times it gave a nest up */
    u8  strike_giveup_row;       /* ... in a row with nothing killed on a
                                    strike between (the rest grows) */
    u32 strike_rest_until;       /* ... no strike near the place before
                                    this frame */
    s16 strike_rest_x, strike_rest_y;   /* ... the place (the last death) */
    /* (Session 81) */
    u16 covered;                 /* (P_FIGHT_COVER) fights left, every
                                    place by the unit under too many
                                    launchers */
    u16 fight_graves;            /* (P_FIGHT_GRAVE) units left a while,
                                    destroyed fighting them */
} HzNightmare;

/* (Session 61) Its state zeroed, keeping `own`'s track list (emptied);
 * and a copy of src's into dst, dst keeping its own track list (grown as
 * src's needs). An HzNightmare starts zeroed. */
void nightmare_reset(HzNightmare *dst, const HzNightmare *own);
void nightmare_copy(HzNightmare *dst, const HzNightmare *src);

/* One line of what it did over a game (static text; hz_arena's N line):
 * the frames in each task, the deaths by kind, units sent. */
const char *nightmare_summary(const HzGame *g, int p);

void nightmare_think(HzGame *g, int p);

/* (Session 64) The nightmare AI through a delay (docs/nightmare.md,
 * "Through a delay"). In a network game the pad it chooses is read some
 * frames after it chooses it (the server's AI: its delay and one more);
 * thinking on the game as it is, it played a state already gone -- at a
 * delay of three it overshot its landings and ran dry carrying. So it
 * thinks ahead: on a copy of the game run on to the pass its pad will be
 * read in, with the pads already chosen for the passes between, and its
 * memory taken back into g. Between passes (before the frame loop's next):
 * pads[s][i], i 0..ahead, are side s's pads (HZ_PAD_* bits, as pad_in)
 * for the i-th pass from the next (a CPU side's are 0: it plays itself
 * in the copy); p's at `ahead` isn't used. Returns the pad p wants for
 * that pass, thought just before p's mech update in it (where
 * HzGame.mech_probe thinks); 0 when the copy's game stops first (its
 * end, or a pause past NM_AHEAD_MAX more passes). The copy is the AI's
 * own, made once; the real game is untouched but g->nightmare[p]. */
#define NM_AHEAD_MAX 60
u8 nightmare_think_ahead(HzGame *g, int p, int ahead, const u8 *const pads[2]);
/* The copy as the last nightmare_think_ahead left it: at the end of the
 * pass it thought in, its pad pressed (hz_arena --check-ahead: the real
 * game should come to it). NULL before the first. */
HzGame *nightmare_ahead_copy(void);
/* One line of what it is doing (static text; hz_arena's trace). */
const char *nightmare_describe(const HzGame *g, int p);
/* The task under way's name (static text). */
const char *nightmare_task_name(const HzGame *g, int p);
/* NAME=VALUE for one of its tuning values; false if there's no NAME. */
bool nightmare_set_param(const char *name_value);
/* The same for one side (0 P1, 1 P2; -1 both). */
bool nightmare_set_param_side(int side, const char *name_value);
/* Its tuning values, i from 0 (NULL past the last): the name, the
 * default, and what it is. */
const char *nightmare_param(int i, int *def, const char **what);
/* (Session 68) For the atlas (nm_atlas.h; hz_arena --atlas): side p's
 * attack unit put down at block (bx, by) alone on g's map, `salts` times
 * with random numbers of its own: the most frames one took to shoot the
 * enemy HQ; -1 one didn't within `frames`; -2 not plain ground. */
int nightmare_atlas_block(HzGame *g, int p, int bx, int by, int salts,
                          int frames);

#endif /* HZ_NIGHTMARE_H */
