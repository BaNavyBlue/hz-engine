#ifndef HZ_GAME_H
#define HZ_GAME_H

#include "../core/video.h"
#include "assets.h"
#include "nightmare.h"
#include "ref.h"

#define HZ_SPRITE_LIST     298  /* v_sprite_list: the $950 bytes Sprites_Build
                                   clears (37 x 64 + 16) */
#define HZ_SPRITE_WINDOW   70   /* entries VDP_UploadSprites DMAs to $D050 */
#define HZ_SPRITE_FIXED    10   /* sprites 0-9 (v_sprite_fixed): the window
                                   is from sprite $A */
#define HZ_UNITS           100  /* the 68k's v_units1/2, 50 a team: the
                                   checks' layout (tools/rom_refs.h) */
#define HZ_SIDE_UNITS      50   /* (session 61) a side's units in the
                                   original, and the slots each side
                                   starts a game with (HzUnitList) */
#define HZ_UNIT_CHUNK      64   /* records a HzUnitList allocates at once */
#define HZ_GRID            64   /* v_grid: 64 x 64 cells of 32 px */
#define HZ_FINE            256  /* (session 61) UNITS' fine grid: 8 px
                                   cells (HzCrowdFine) */
#define HZ_SHOTS           64   /* v_bullets1 (24), v_missiles1 (8), then the
                                   same for team 2, contiguous */
#define HZ_RADAR_SPRITES   18   /* v_radar_sprites1/2 */

/* v_outposts ($FF89B4, 16 records) then v_hq ($FF8A34, 2): 8-byte
 * records the AI indexes as one array. Map_ScanOutposts clears 20 of them;
 * what the last two ($FF8A44/$FF8A4C) hold isn't known yet. */
#define HZ_POST_OUTPOSTS   16
#define HZ_POST_HQ         16        /* HQ of player p: posts[HZ_POST_HQ + p] */
#define HZ_POST_CLEARED    20

/* HzPost.flags: $80/$81 a neutral outpost, $82/$83 P1's/P2's, $C2/$C3
 * P1's/P2's HQ. */
#define POST_USED   0x80
#define POST_HQ     0x40
#define POST_OWNED  0x02
#define POST_TEAM2  0x01        /* (with POST_OWNED) */

typedef struct {
    u8  flags;   /* +0: POST_* */
    u8  b1;      /* +1: not set by Map_ScanOutposts */
    union {      /* +2/+3: */
        u8 hold[2];     /* an outpost's: P1's and P2's hold, 0-4 */
        u8 hp_bytes[2]; /* an HQ's HP, high byte first: post_hq_hp */
    };
    s16 x, y;    /* +4, +6: centre of the block, pixels */
} HzPost;

/* An HQ record's HP (the 68k's word at +2). */
static inline u16 post_hq_hp(const HzPost *hq)
{
    return (u16)((hq->hp_bytes[0] << 8) | hq->hp_bytes[1]);
}

static inline void post_set_hq_hp(HzPost *hq, u16 hp)
{
    hq->hp_bytes[0] = (u8)(hp >> 8);
    hq->hp_bytes[1] = (u8)hp;
}

typedef struct {
    s16 x, y;
} HzPoint;

/* The pad byte ReadJoypads makes (v_joypad1/2, mech_pad), active high. */
#define HZ_PAD_UP     0x01
#define HZ_PAD_DOWN   0x02
#define HZ_PAD_LEFT   0x04
#define HZ_PAD_RIGHT  0x08
#define HZ_PAD_B      0x10
#define HZ_PAD_C      0x20
#define HZ_PAD_A      0x40
#define HZ_PAD_START  0x80

/* Masks of the pad byte. */
#define HZ_PAD_DIRS    0x0F
#define HZ_PAD_BUTTONS 0xF0     /* Start, A, C, B */

/* The controller in a port: a 3-button pad's buttons, true = pressed, as
 * the host sets them. ReadJoypads reads them through the port's data
 * lines. */
typedef struct {
    bool up, down, left, right;
    bool a, b, c, start;
} HzPad;

/* How a pass of Game_FrameLoop ends. On the 68k the loop never returns:
 * Game_SecondTick jumps out of it. */
typedef enum {
    HZ_LOOP_ON = 0,     /* on to the next pass */
    HZ_LOOP_GAME_OVER,  /* an HQ at 0 HP: jmp Game_Over, v_winner set */
    HZ_LOOP_TITLE,      /* the demo's 60 s are up: bra MainLoop_Title */
    HZ_LOOP_QUIT        /* the port's own: the host has asked to stop */
} HzLoopExit;

/* A sprite as the cartridge's RAM holds it (v_sprite_fixed, v_sprite_list,
 * the order menu's radar, the loading screen's): its words in the VDP's
 * sprite table format. sprites_upload hands the frame's to the draw layer
 * in screen pixels (Sprite, video.h). */
typedef struct {
    u16 y;          /* +0: screen y + 128 */
    u16 size_link;  /* +2: size in bits 8-11 (width - 1 in 10-11, height
                       - 1 in 8-9), the next sprite in bits 0-6; (session
                       72) the port's colour table in bits 12-15 */
    u16 attr;       /* +4: priority, palette line, flips, tile (a cell
                       word) */
    u16 x;          /* +6: screen x + 128 */
} HzSprite;

/* The mech record (v_mech1/2, $100 bytes each), only the fields the
 * ported code has touched so far, with their offsets. */
/* A mech's order menu (order_menu.c): its bytes in the mech record. The
 * menu draws itself into its view's plane; the *_shown fields are what it
 * last drew, and price, fee and cost are as last drawn (a hidden menu's as
 * OrderMenu_Settle last set them). */
typedef struct {
    u8  step;           /* +$40 mech_menu_step: 9 closed; opening counts
                           9 to 0, closing $F to 0 */
    u8  radar_step;     /* +$41 mech_radar_step: the radar's refresh, 0-$B
                           a frame */
    u8  closing;        /* +$6D mech_menu_closing */
    s16 scroll_x;       /* +$44/+$46 mech_menu_scroll_x/y: its view's */
    s16 scroll_y;       /*   scroll, saved while the menu is open */
    u8  row;            /* +$48 mech_menu_row: MENU_ROW_* */
    u8  row_shown;      /* +$49 */
    u8  type;           /* +$4A mech_menu_type: the unit type picked (the
                           carried unit's while carrying) */
    u8  type_shown;     /* +$4B */
    u8  order;          /* +$4C mech_menu_order: the order picked */
    u8  order_shown;    /* +$4D */
    u8  carried_order_shown; /* +$4E */
    u8  reorder_state;  /* +$50 mech_reorder_state: 2 re-ordered, $FF
                           short of money */
    u32 unit_count_shown; /* +$51 */
    u8  radar_mode;     /* +$52 mech_radar_mode: 0 units near, 1 map */
    u8  radar_mode_shown; /* +$53 */
    u16 money_shown;    /* +$56 */
    u16 price;          /* +$58 mech_menu_price: 0 while carrying */
    u16 fee;            /* +$5A mech_order_fee */
    u16 cost;           /* +$5C mech_order_cost: what A/B pay */
} HzOrderMenu;

/* The menu's rows (mech_menu_row). */
enum {
    MENU_ROW_TYPE  = 0,
    MENU_ROW_ORDER = 1,
    MENU_ROW_RADAR = 2
};

/* What the CPU mech's AI (mech_ai.c) keeps: the mech record's +$80-$FF.
 * Records (outposts, then the two HQs: g->posts) are named by index,
 * units by index in g->units, other objects by grid id. Where the 68k
 * gives one byte several uses, the union names each. All zero is a new
 * game's state (MainLoop_Title clears the record), which starts the AI in
 * its setup mode. */
#define HZ_AI_RECORDS  18           /* the 16 outposts and the 2 HQs */

/* Which plan is running (mech_ai_plan), as AIPlanTable has them. */
enum {
    PLAN_MODES        = 0x00,   /* AI_PlanModes: run a mode (below) */
    PLAN_BUY          = 0x01,   /* buy through the order menu */
    PLAN_LIFT         = 0x02,   /* (nothing starts it) */
    PLAN_DROP         = 0x03,
    PLAN_FLY          = 0x04,   /* fly to the goal, then turn to a heading */
    PLAN_GROUND       = 0x05,   /* land and fight on foot */
    PLAN_DELIVER_HQ   = 0x06,   /* carry a unit to an own HQ */
    PLAN_DELIVER_POST = 0x07,   /* ... to an own outpost */
    PLAN_PICK_UP      = 0x08,   /* collect a bought unit */
    PLAN_FLY_SERVICE  = 0x09,   /* fly to an own outpost for service */
    PLAN_TO_ENEMY     = 0x0A,   /* drop a unit beside an enemy */
    PLAN_SERVICE_UNIT = 0x0B,   /* take an own unit to be refilled */
    PLAN_FLY_RESUME   = 0x0C,   /* fly to the goal, then resume */
    PLAN_LAUNCH       = 0x0D,   /* drop a unit at once along a heading */
    PLAN_DELIVER_HQ_X = 0x0E,   /* plan 6 at another side's HQ */
    PLAN_DELIVER_POST_X = 0x0F, /* plan 7 at another side's outpost */
    PLAN_DELIVER_ST   = 0x10,   /* put an ST-57U on water */
    PLAN_TEND_UNITS   = 0x11    /* look over own units for one to service */
};

/* What the words at +$C0-$CF hold (the port's own tag: the plans share
 * them, see HzMechAI.work). */
enum {
    WORK_NONE = 0,
    WORK_PRESSES,               /* plan 1 */
    WORK_BOX,                   /* plans 5, 6, 7, $A, $E, $F */
    WORK_HQS,                   /* setup mode */
    WORK_SERVICE                /* plan $B */
};

/* Plan $B: how far its words are filled in. */
enum {
    SERVICE_UNIT = 0,           /* the unit */
    SERVICE_POST,               /* and the outpost that refills it */
    SERVICE_RETURN              /* and where it goes back */
};

typedef struct {
    u16  frames;            /* +$80 mech_ai_frames: + 1 a frame alive */
    bool st_map;            /* +$82 mech_ai_st_map: maps 3, 5, 7 */
    union {
        u8 misses_left;     /* +$83 plan 5: misses before it repositions */
        u8 watch_next;      /* plan $B: the step after its enemy watch */
    };
    u8   reorder;           /* +$84 mech_ai_reorder: $FF re-ordering the
                               carried unit, 0 a new buy */
    union {
        u8 order_tries;     /* +$85 plan 1: presses left on the order row */
        u8 attack_here;     /* plan 5: non-zero, look round itself */
    };
    HzPoint goal;           /* +$86/+$88 mech_ai_goal_x/y */
    union {
        u8 heading;         /* +$8A: $FF none. Setup and plan 4: 16-way;
                               plan $D: 8-way; a drop point's facing */
        s8 curve;           /* plan $10: the side it curves in on, +1/-1 */
        u8 drop_tries;      /* plan $B: drops left */
    };
    u8   unused_flag;       /* +$8B: written, never read */
    u8   hold;              /* +$8C mech_ai_hold: move state 6, frames */
    u8   buy_count;         /* +$8D: units still to buy */
    u8   move;              /* +$8E mech_ai_move: the move state */
    HzRef target;           /* +$8F plans 5, $A: the enemy (a grid id on
                               the 68k) */
    u8   marks[HZ_AI_RECORDS];  /* +$90: per record, MARK_* */
    u8   dropped;           /* +$A2 mech_ai_dropped */
    u8   lifted;            /* +$A3 mech_ai_lifted */
    u8   edge_steer;        /* +$A4: the pad before the edges turned it */
    u8   buy_kind;          /* +$A5: AIBuyScriptTable column 0-7 */
    u8   saved_step;        /* +$A6: what AI_ResumePlan goes back to */
    u8   saved_plan;        /* +$A7 */
    u8   dest;              /* +$A8 (a pointer): the record it goes for */
    u8   from;              /* +$AA: the record it plans from */
    u8   next_dest;         /* +$AC: the HQ alarm's counter-attack */
    u8   next_from;         /* +$AE */
    u8   buy_script;        /* +$B0 (a pointer): offset in AI_BUY_SCRIPTS
                               of the pair plan 1 is at */
    HzPoint saved_goal;     /* +$B4: the goal kept over a service trip */
    u8   countdown;         /* +$B8 mech_ai_countdown */
    u8   alert_post;        /* +$B9: record of an alert-1 outpost */
    u8   refuel;            /* +$BA mech_ai_refuel */
    u8   route;             /* +$BB mech_ai_route: $FF choosing, 0 chosen,
                               $80 none left */
    union {
        u8 scan_dir;        /* +$BC plan $10: AI_ScanBlocksInDir's
                               direction */
        u8 frames_left;     /* plans 5, 6, 7, $A, $D, $10: laps or frames
                               before it acts */
        u8 hops;            /* strategy 17, 20: records skipped */
    };
    union {
        u8 aim_dir;         /* +$BD plan 5: the direction it shoots in */
        u8 failed_drops;    /* plans $D, $10 */
    };
    u8   fight_form;        /* +$BE: mech_form + 1 when a fight started */
    u8   pad_dir8;          /* +$BF: the 8-way pad direction last set */
    /* +$C0-$CF: words the plans share; `kind` (WORK_*, the port's own)
     * says which member is in use. */
    struct {
        u8 kind;
        u8 service_stage;   /* SERVICE_*, while kind is WORK_SERVICE */
        union {
            struct {            /* plans 5, 6, 7, $A: where to land or
                                   drop (outer), and not (inner, 6/7) */
                s16 left, top, right, bottom;
                s16 inner_left, inner_top, inner_right, inner_bottom;
            } box;
            struct {            /* plan 1: frames between pad presses */
                u16 delay, wait;
            } presses;
            struct {            /* setup: the two HQs */
                HzPoint own, enemy;
            } hqs;
            struct {            /* plan $B */
                u16 c0;         /* +$C0 (a pointer to the unit on the 68k):
                                   the port keeps it in service_unit */
                u16 c2, c4, c6; /* (not its) */
                u16 post;       /* +$C8 (a pointer): the outpost record */
            } service;
            struct {
                u16 c0, c2, c4, c6;
                HzPoint at;     /* +$C8/+$CA: where it puts the unit back */
            } service_return;
            u16 raw[8];
        };
    } work;
    HzRef service_unit;     /* plan $B (WORK_SERVICE): the unit it services
                               (+$C0 on the 68k) */
    u8   launches;          /* +$D0: kind-7 units bought for this launch */
    /* +$D1, one variable with two uses: send_to clearing attack_buy is
     * what starts plan $11's cursor at slot 0, and the cursor counted up
     * is a non-zero attack_buy. */
    union {
        u32 attack_buy;     /* 1, bought by an AI_BuyAt*_Attack */
        u32 tend_cursor;    /* plan $11: the next own unit slot to look at */
    };
    u8   own_near;          /* +$D4 AI_CountAroundDest */
    u8   enemy_near;        /* +$D5 */
    u8   respawn;           /* +$D6: set while destroyed */
    u8   detour;            /* +$D7: frames left of a detour */
    u16  hq_hp;             /* +$D8: own HQ's HP when last taken */
    u8   alert_taken;       /* +$DA: 0 none; step + 1; $FF HQ defence */
    u8   alert;             /* +$DB: ALERT_* from the move states */
    u8   hq_attack;         /* +$DC */
    u8   hq_run;            /* +$DD: $FF for the enemy HQ, $80 staging */
    u8   engage;            /* +$DE: $80 may fight, $FF came in range */
    u16  engage_range;      /* +$E0 */
    u8   needs_service;     /* +$E2 */
    u8   course;            /* +$E3: pad direction bits towards the goal */
    u8   hq_drop;           /* +$E4: drop-point cursor round an HQ */
    u8   hq_drop_end;       /* +$E5 */
    u8   post_drop;         /* +$E6: ... round an outpost (+2 a try) */
    u8   post_drop_end;     /* +$E7 */
    u8   bought;            /* +$E8: units bought for this destination */
    u8   reset_count;       /* +$E9 */
    u8   deaths;            /* +$EA */
    u8   launch_deaths;     /* +$EB */
    u8   ground_shot;       /* +$EC plan 5: $FF until its shot's result */
    u8   watch_post;        /* +$ED move state 13's outpost, 0-15 */
    u8   dodge;             /* +$EF: frames left of a missile dodge */
    u8   strategy;          /* +$F0: the strategy step */
    u8   mode;              /* +$F1: plan 0's mode */
    u8   substep;           /* +$F2: modes 0, 2, 3 */
    u8   plan;              /* +$F3: PLAN_* */
    u8   step;              /* +$F4: the plan's step */
    u8   style;             /* +$F5: 0-3 */
    u8   dest_class;        /* +$F6: the last of steps 7-11 taken */
    u8   enemy_heading;     /* +$F7: written, never read */
    u8   pickup_post;       /* +$F8 (a pointer): the record it collects
                               bought units from */
    u8   buy_variant;       /* +$FA */
    u8   shot_wait;         /* +$FB move state 6 */
    u8   deliver_dir;       /* +$FC */
    bool raid_map;          /* +$FD: maps 2, 4-7 */
    u8   launch_at_from;    /* +$FE */
    u8   launch_at_dest;    /* +$FF */
} HzMechAI;

/* (Session 61) v_grid's cells and v_hit_id hold an HzRef (ref.h): a mech
 * or a unit; the 68k's grid id bytes are tools/rom_refs.h's. */

/* v_hit_flags: what a move's probe found (and HzMech/HzUnit.blocked). */
#define PROBE_OBJECT  0x01      /* an object (v_hit_id) */
#define PROBE_TILE    0x02      /* a tile that blocks */
#define PROBE_EVENT   0x04      /* a tile with a terrain event */
#define PROBE_FOE     0x08      /* the object is the other side's */

/* HzMech.prod_state (mech_prod_state): the unit on order. */
enum {
    PROD_NONE     = 0x00,       /* nothing on order */
    PROD_BUILDING = 0x01,
    PROD_READY    = 0x02,
    PROD_SHORT    = 0xFF        /* short of money */
};

/* HzMech.shot_flags and HzUnit.shot_flags. */
#define HIT_BY_SHOT     0x80    /* the low nibble: the shot's direction */
#define OWN_SHOT_HIT    0x40    /* its own last shot hit something */
#define OWN_SHOT_OUT    0x20    /* its own shot is in flight */

/* HzMech.flags (obj_flags). */
#define MECH_ACTIVE     0x80
#define MECH_MENU_OPEN  0x40    /* the order menu */
#define MECH_DESTROYED  0x20
#define MECH_TEAM2      0x10
#define MECH_CARRYING   0x08
#define MECH_REVERSING  0x02    /* a robot */
#define MECH_CPU        0x01

typedef struct {
    u8  flags;          /* +$00 obj_flags: MECH_* */
    u32 carried_slot;   /* +$01 mech_carried_slot: the carried unit's slot
                           in the mech's side */
    u8  carried_order;  /* +$02 mech_carried_order */
    u8  dir;            /* +$03 obj_dir: 0 up, 4 right, 8 down, $C left */
    u8  turn_to;        /* +$04 mech_turn_to */
    u8  transform;      /* +$06 mech_transform: frames left in b0-b6, b7
                           landing; 0 not transforming */
    u8  transform_delay;/* +$07 mech_transform_delay */
    u8  power;          /* +$08 obj_power */
    HzRef hit_victim;   /* +$09 mech_hit_victim: what its last shot hit,
                           as unit_hit_victim */
    u16 hp;             /* +$0A obj_hp; counts down from $180 once
                           destroyed (Sprites_DrawMechDebris) */
    u8  size;           /* +$0C obj_size */
    u8  altitude;       /* +$0D mech_altitude: jet shadow's x offset */
    HzRef bumped;       /* +$0E mech_bumped: what its last move ran into */
    u8  fuel_use;       /* +$0F mech_fuel_use */
    s16 x, y;           /* +$10/+$12 obj_x/obj_y */
    u16 sub_x, sub_y;   /* +$14/+$16 mech_sub_x/y: 8.8, the whole pixels
                           moved into obj_x/y each move */
    u8  missiles;       /* +$18 obj_missiles: only Obj_RepairRefuel writes
                           it, and KIDOU's maximum is 0, so it stays 0 */
    u8  ammo;           /* +$19 obj_ammo */
    u16 fuel;           /* +$1A obj_fuel */
    u16 terrain;        /* +$1C mech_terrain: v_terrain_event after a
                           move */
    u16 frame;          /* +$1E mech_frame: frame in the low byte, flips in
                           $1800 */
    u16 frame_loaded;   /* +$20 mech_frame_loaded: $FFFF = none */
    u8  shot_flags;     /* +$22 mech_shot_flags: HIT_BY_SHOT (the low
                           nibble its direction), OWN_SHOT_HIT,
                           OWN_SHOT_OUT */
    u8  blocked;        /* +$23 mech_blocked: robot, v_hit_flags of the
                           last move */
    u16 speed;          /* +$24 mech_speed: Mech_Update writes the low
                           byte */
    u8  max_speed;      /* +$27 mech_max_speed: KIDOU +$20/$21/$22 */
    u8  pad;            /* +$28 mech_pad: pad bits this frame (HZ_PAD_*);
                           a CPU mech's come from AI_MechThink */
    u8  pad_prev;       /* +$29 mech_pad_prev: last frame's */
    u8  lead;           /* +$2A mech_lead: 0 free in its box, 1 sliding
                           ahead, 2 the camera follows */
    u8  terrain_flow;   /* +$2B mech_terrain_flow: a terrain handler's,
                           for the next move */
    u16 form;           /* +$2C mech_form: 0 robot, 1 jet, 2 jet carrying */
    u8  pad_last;       /* +$2E mech_pad_last */
    u8  dir_held;       /* +$2F mech_dir_held: to $80 */
    s16 vel_x, vel_y;   /* +$30/+$32 obj_vel_x/y */
    s16 target_vel_x;   /* +$34/+$36 obj_target_vel_x/y */
    s16 target_vel_y;
    s16 screen_x;       /* +$38 mech_screen_x: x within its view */
    s16 screen_y;       /* +$3A mech_screen_y: y on screen, HUD strip
                           included */
    s16 cam_dx, cam_dy; /* +$3C/+$3E mech_cam_dx/dy: camera move this
                           frame (Mech_Update clears it, its move code
                           sets it) */
    s8  view;           /* +$42 mech_view: 0 plane A, 1 plane B, < 0
                           hidden */
    u8  view_index;     /* +$43 mech_view_index: 0 P1, 1 P2; a wide view
                           follows camera 2 when set */
    u8  prod_state;     /* +$4F mech_prod_state: PROD_* */
    u16 money;          /* +$54 mech_money */
    u8  prod_type;      /* +$5E mech_prod_type: the unit on order */
    u8  prod_order;     /* +$5F mech_prod_order: its order */
    u16 prod_timer;     /* +$60 mech_prod_timer: counts down while
                           building */
    u8  hp_shown;       /* +$62 mech_hp_shown: the HUD bars' values as */
    u8  hq_hp_shown;    /* +$63 mech_hq_hp_shown: HUD_UpdateGauges last */
    u8  ammo_shown;     /* +$66 mech_ammo_shown: queued them (low bytes); */
    u8  fuel_shown;     /* +$67 mech_fuel_shown: 0 at the start */
    u16 option2;        /* +$64: v_mech_option2 */
    u8  carried_refill; /* +$68 mech_carried_refill: what Outpost_Service
                           last topped up on the carried unit (b0 HP, b1
                           fuel, b2 ammo, b3 missiles); the order menu
                           redraws its gauges and clears it */
    u8  fire_delay;     /* +$69 mech_fire_delay */
    u16 missile_dirs;   /* +$6A mech_missile_dirs: bit n, a missile homing
                           in from direction n */
    u8  hq_alert;       /* +$6C mech_hq_alert: HQ-hit warning frames */
    u8  fuel_count;     /* +$6E mech_fuel_count: distance, per $50 */
    u8  walk_step;      /* +$6F mech_walk_step: 0-2 */
    HzPoint home_cam;   /* +$70/+$72 mech_home_cam_x/y */
    HzPoint home_screen;/* +$74/+$76 mech_home_screen_x/y */
    u16 radar_own_y;    /* +$78/+$7A mech_radar_own_y/x: order-menu */
    u16 radar_own_x;    /*   radar marker, sprite coordinates */
    u16 radar_foe_y;    /* +$7C/+$7E mech_radar_foe_y/x: the rival's; */
    u16 radar_foe_x;    /*   y 0 = off the radar */
    HzMechAI ai;        /* +$80-$FF: the CPU's (mech_ai.c) */
    HzOrderMenu menu;   /* +$40-$5D, +$6D: the order menu's */
    u16 crowd_cell;     /* (Session 61) with UNITS, the grid cell it
                           stands in + 1, 0 none (crowd.c) */
} HzMech;

/* Unit types (obj_type), as UnitStats has them (types 6 and 7 have
 * records but nothing builds them; $A-$D are empty records). */
enum {
    UNIT_AMR      = 0,      /* AMR-51D */
    UNIT_TAX      = 1,      /* TAX-52 */
    UNIT_SAM      = 2,      /* SAM-42 */
    UNIT_ST       = 3,      /* ST-57U, the boat */
    UNIT_SUPPLY   = 4,      /* SUPPLY */
    UNIT_GRM      = 5,      /* GRM-34A */
    UNIT_TURRET1  = 6,      /* "NOT USE": the TAX-52's turret overlay; with
                               TURRETS (session 60) TURRET-1 */
    UNIT_TURRET2  = 7,      /* "NOT USE": the ST-57U's; TURRET-2 */
    UNIT_INFANTRY = 8,      /* INFANTRY */
    UNIT_FWA      = 9,      /* FWA */
    UNIT_REPAIR   = 10,     /* (session 72) with REPAIR the repair truck:
                               an empty record in UnitStats */
    UNIT_MECHANIC = 11      /* ... and one of its two mechanics, on foot */
};

/* (Session 72) A unit type's tiles a side, as the ground is tested under
 * it: 3, or 2 for INFANTRY, the FWA and a mechanic. And whether it is a
 * man on foot: INFANTRY's sprite, its lighter push from a current.
 * `repair`: types 10 and 11 are the repair truck and its mechanic in this
 * game (repair_on); without it these are the original's own tests, `type
 * < 8` and `type == 8`, for every type (the checks against the ROM try
 * types 10-15 too). */
static inline int unit_tiles(u8 type, bool repair)
{
    return (type < UNIT_INFANTRY || (repair && type == UNIT_REPAIR)) ? 3 : 2;
}

static inline bool unit_on_foot(u8 type, bool repair)
{
    return type == UNIT_INFANTRY || (repair && type == UNIT_MECHANIC);
}

/* Unit orders (obj_order), with the names the order menu shows. */
enum {
    ORDER_ROAM       = 0x0,   /* AF-001: drive on (no one buys it) */
    ORDER_STAND      = 0x1,   /* BDF-1SD: stop and aim */
    ORDER_PATROL     = 0x2,   /* AF-001A: wander near the start point */
    ORDER_OUTPOST    = 0x3,   /* AT-101: to the nearest outpost not ours */
    ORDER_GUARD      = 0x4,   /* DF-F01 (no one buys it) */
    ORDER_GUARD_NEAR = 0x5,   /* DF-F02A: guard the start point */
    ORDER_AMBUSH     = 0x6,   /* DF-F03S (no one buys it) */
    ORDER_ADVANCE    = 0x7,   /* T-BG1 (no one buys it) */
    ORDER_HOLD       = 0x8,   /* BDF-1ST (no one buys it) */
    ORDER_CAPTURE    = 0x9,   /* AT-101H: INFANTRY into the outpost */
    ORDER_ATTACK_HQ  = 0xA,   /* BA-001C: to the enemy HQ and shoot it */
    ORDER_REPAIR     = 0xE,   /* (session 72) RP-MC02, with REPAIR: the
                                 repair truck (OrderInfo's $E is blank) */
    ORDER_SUPPLY     = 0xF    /* PW-SS10: SUPPLY */
};

/* What the unit AI (unit_ai.c) keeps for a unit. On the 68k these are
 * the record's bytes +$38-$4F, where several orders reuse the same bytes
 * for different things (noted per field); here each use has its own
 * field. Objects are named by an HzRef (REF_NONE for none; the 68k's grid
 * ids and pointers). All zero = a new unit's state: Unit_Spawn and the
 * mech's drop clear it. */
typedef struct {
    /* Patrol, guard, guard near, hold: the point they keep to (+$38:
     * x | $8000 in the high word marks it set, y in the low). */
    bool home_set;
    s16  home_x, home_y;
    bool ambush_started;    /* ambush: its first run done (+$38 $FF) */
    u8   advance_dir;       /* advance: its starting heading + 1 (+$38) */
    u8   attack_state;      /* attack HQ (+$38): 0 approaching, 1-$7F
                               wandering (updates left), $80 out of ammo,
                               $FF in range, shooting */
    bool docked;            /* SUPPLY (+$38): serving its customer */
    u8   customer_speed;    /* SUPPLY (+$39): the customer's speed (low
                               byte), given back when it lets it go */
    u8   detour_left;       /* SUPPLY (+$3A): updates left of a detour */
    s8   detour_step;       /* SUPPLY (+$3B): the detour's turn, -1 / +1 */
    u8   misses;            /* +$3C: shots in a row that missed */
    u8   state;             /* +$3D: the order's state (unit_dodge); see
                               each order in unit_ai.c */
    u8   target_dir;        /* +$3E: direction + 1 to target_id, 0 none */
    u8   path_hint;         /* +$3F: attack HQ: the map's path hint
                               towards the enemy HQ (0-$E) + 1, 0 none */
    u16  dest_dist;         /* +$40: distance to the goal (an outpost, or
                               the enemy HQ for attack HQ) */
    u8   dest_dir;          /* +$42: direction + 1 to the goal, 0 none */
    u8   far_dir;           /* +$43: direction + 1 to far_id, 0 none */
    u8   hold;              /* +$44: $FF stopped on purpose; for some
                               orders the step of a turn aside */
    u8   saved_dir;         /* +$45: the heading + 1 to go back to; for
                               ambush the step of a turn aside */
    HzRef target_id;        /* +$46: what it aims at (its pointer at +$48
                               always names the same object) */
    u32  cursor;            /* SUPPLY (+$46): the next of its team's slots
                               to look at for a customer */
    bool shot_out;          /* +$47: a bullet out, its result not seen */
    HzRef customer_id;      /* SUPPLY (+$48, a pointer): the unit it goes
                               to or serves */
    HzRef far_id;           /* +$4C (a pointer): the enemy far off */
    HzRef skip_id;          /* the port's (fixes): a customer this SUPPLY
                               found no route to, passed over until it
                               serves another */
    bool route_going;       /* the port's (fixes): it set off along its
                               route last update */
    s16  route_x, route_y;  /* the port's (fixes): the place on its route
                               it drives at; (session 71) with PATHS, any
                               unit's place on its way (path_going) */
    bool path_going;        /* (session 71) PATHS: not a SUPPLY, it drives
                               at route_x, route_y this update */
    /* (Session 72) REPAIR (unit_ai.c): a repair truck's and a mechanic's.
     * Both also use customer_id (the vehicle the truck drives for; the
     * one the man mends), the route's fields and the detour's. */
    HzRef truck_id;         /* a mechanic's truck */
    bool  recall;           /* the truck's: its men are to come in */
    u8    alarm;            /* the truck's: updates left of an alarm (an
                               enemy near it or one of its men, or a
                               hit), counted down while none is near */
    u8    stuck;            /* updates on its way: a mechanic's to a
                               vehicle or back to his truck, a truck's to
                               a vehicle */
    bool  working;          /* a mechanic's: mending this update (his
                               sprite steps on the spot) */
    u8    mend_wait;        /* any unit's: updates left in which no
                               mechanic or truck comes for it (one found
                               no way to it) */
} HzUnitAI;

/* The teams' sprite palettes (the attr word's palette line): P1's side
 * line 2, P2's line 3. */
#define TEAM1_PALETTE   0x4000
#define TEAM2_PALETTE   0x6000

/* HzUnit.flags (obj_flags). */
#define UNIT_ACTIVE     0x80
#define UNIT_ON_SCREEN  0x40    /* in some view */
#define UNIT_DYING      0x20
#define UNIT_TEAM2      0x10
#define UNIT_CARRIED    0x08
#define UNIT_HELD       0x04    /* a SUPPLY's customer, held while served */
#define UNIT_REVERSING  0x02
#define UNIT_BOAT       0x01    /* an ST-57U: needs TILE_WATER tiles */

/* A unit record (v_units1/2, $50 bytes), the fields the ported code
 * touches, with their offsets. */
typedef struct {
    u8  flags;          /* +$00 obj_flags: UNIT_* */
    u8  type;           /* +$01 obj_type */
    u8  order;          /* +$02 obj_order */
    u8  dir;            /* +$03 obj_dir: heading 0-$F */
    u8  turn_to;        /* +$04 unit_turn_to */
    u8  tick;           /* +$05 unit_tick */
    u8  power;          /* +$08 obj_power */
    HzRef hit_victim;   /* +$09 unit_hit_victim: what its last shot hit (a
                           unit, a mech, an HQ) */
    u16 hp;             /* +$0A obj_hp */
    u8  size;           /* +$0C obj_size */
    u8  raw_0d;         /* +$0D: Shot_FireBullet takes it as the shooter's
                           altitude (a mech's); no unit writer found */
    HzRef bumped;       /* +$0E unit_bumped: what its last move ran into;
                           see Unit_CheckObjCollision for the centre cell */
    u8  fuel_use;       /* +$0F unit_fuel_use */
    s16 x, y;           /* +$10/+$12 obj_x/y */
    u16 sub_x, sub_y;   /* +$14/+$16 unit_sub_x/y: 8.8, the whole pixels
                           moved out each move */
    u8  missiles;       /* +$18 obj_missiles */
    u8  ammo;           /* +$19 obj_ammo */
    u16 fuel;           /* +$1A obj_fuel */
    u16 terrain;        /* +$1C unit_terrain: v_terrain_event after its
                           last move */
    u16 sprite_attr;    /* +$1E obj_sprite_attr (Unit_SetSprite) */
    u16 sprite_size;    /* +$20 obj_sprite_size: size/link word */
    u8  blocked;        /* +$23 unit_blocked: v_hit_flags of its last move
                           (b0 object, b1 tile, b2 event, b3 the object
                           the other side's) */
    u16 speed;          /* +$24 unit_speed */
    u8  prev_speed;     /* +$26 unit_prev_speed: unit_speed's low byte as
                           Unit_SetSprite last saw it */
    u8  max_speed;      /* +$27 unit_max_speed */
    u8  turret;         /* +$28 unit_turret: b7 = overlay */
    u8  turret_dir;     /* +$29 unit_turret_dir */
    u8  aim_dir;        /* +$2A unit_aim_dir */
    u8  push;           /* +$2B unit_push: a terrain handler's, for the
                           next move (as mech_terrain_flow) */
    u16 overlay_attr;   /* +$2C unit_overlay_attr */
    u16 grid_cell;      /* +$2E obj_grid_cell */
    s16 vel_x, vel_y;   /* +$30/+$32 obj_vel_x/y */
    s16 target_vel_x;   /* +$34/+$36 obj_target_vel_x/y */
    s16 target_vel_y;
    u8  shot_flags;     /* +$22 unit_shot_flags: as mech_shot_flags */
    u8  phase;          /* +$06 unit_phase: + 1 per AI update; & 3 picks
                           what it does after its order */
    u8  fire_delay;     /* +$07 unit_fire_delay: AI updates to the next
                           bullet */
    HzUnitAI ai;        /* +$38-$4F: the unit AI's */
    /* (Session 61) The record's own place, set when its slot is made and
     * kept by unit_clear: the side's list and the slot in it. */
    u8  side;
    u32 slot;
    /* (Session 61) With UNITS, its grid cell's list of its side's units
     * (crowd.c): the cell + 1 (0: in none), the next and previous slots
     * + 1 (0: none); the same in the fine grid's. */
    u16 crowd_cell;
    u32 crowd_next;
    u32 crowd_prev;
    u32 fine_cell;
    u32 fine_next;
    u32 fine_prev;
} HzUnit;

/* Every field of a unit record zeroed but its place (side, slot). */
void unit_clear(HzUnit *u);

/* HzShot.flags. */
#define SHOT_ACTIVE     0x80
#define SHOT_ON_SCREEN  0x40
#define SHOT_EXPLODING  0x20
#define SHOT_TEAM2      0x10
#define SHOT_FROM_UNIT  0x08    /* hits the other side only, and HQs */
#define SHOT_FROM_AIR   0x04    /* a missile: always */
#define SHOT_ORPHAN     0x01    /* its owner gone */

/* A shot record (v_bullets1/2, v_missiles1/2, $20 bytes). Bullets 0-23,
 * then missiles 24-31, of each team. */
typedef struct {
    u8  flags;          /* +$00: SHOT_* */
    u8  dir;            /* +$01: 0-$F */
    s16 x, y;           /* +$02/+$04 */
    u16 speed;          /* +$06 */
    u16 sub_x, sub_y;   /* +$08/+$0A: 8.8, the whole pixels moved out */
    u16 life;           /* +$0C: frames; an explosion's 4 */
    u16 attr;           /* +$0E: sprite attr */
    s16 vel_x, vel_y;   /* +$10/+$12: a missile's velocity */
    s16 aim_x, aim_y;   /* +$14/+$16: a bullet moves by these; a missile
                           steers its velocity towards them */
    u8  power;          /* +$18 */
    HzRef owner;        /* +$1C (a pointer on the 68k): a mech or a unit */
} HzShot;

/* A row or column Map_ScrollUpdate queued for Map_FlushScroll
 * (v_row_queue_a/b, v_col_queue_a/b, $80 bytes each): the cell count - 1
 * (as the dbf counts), the plane and its first cell (the 68k keeps the
 * VDP write command for it), and the cells; plane B's queues add the
 * plane row or column the flush clears (on the 68k the command after the
 * last cell). */
#define HZ_QUEUE_CELLS 42   /* a wide row */

typedef struct {
    u16 last;
    u16 layer;              /* the layer (LayerId): A or B */
    u8  col, row;           /* the first cell */
    u16 cells[HZ_QUEUE_CELLS];
    u8  clear;              /* plane B's: the row or column to clear */
} HzScrollQueue;

/* A bar HUD_QueueBar queued for HUD_FlushBars (v_gauge_queue, 16 of $26
 * bytes): the cell count - 1 (as the dbf counts; 0 is a free entry), the
 * layer and cell of its first cell (the 68k keeps the VDP write command
 * for it), a tile word per cell; the cells run along the row, on into the
 * next. */
#define HZ_GAUGE_BARS   16
#define HZ_GAUGE_CELLS  16

typedef struct {
    u16 last;
    u8  layer;              /* LayerId */
    u8  col, row;
    u16 cells[HZ_GAUGE_CELLS];
} HzGaugeBar;

/* A tile Map_AnimTilesUpdate queued for Map_AnimTilesUpload
 * (v_anim_tile_queue, an entry of $A bytes per record): a VRAM copy of
 * tile `from`'s picture into the record's slot, or the record's own
 * picture (v_anim_tile_gfx) written back there. The 68k keeps, for the
 * copy, the DMA's source registers and the VDP command's first word; for
 * the write, the picture's RAM address and the VDP write command. */
typedef struct {
    bool pending;           /* +0: queued this time */
    bool copy;              /* +1: a VRAM copy (else the own picture) */
    u16  from;              /* the tile copied */
    u16  to;                /* the slot's VRAM address */
} HzAnimTileJob;

#define HZ_ANIM_GFX_RAM 0xFF4800u   /* v_anim_tile_gfx */

/* A map-block change MapFx_Update makes when its countdown runs out
 * (v_mapfx, 32 records of 12 bytes): the block goes into the layout word
 * at `layout_ofs` (bytes) and onto the views that show it. Outpost_ShowOwner
 * and the shots' terrain handlers 1 and 5 make them (MapFx_Alloc). */
#define HZ_MAPFX 32

typedef struct {
    u8  flags;          /* +0: b7 active, b6 chained (the next block comes
                           from the new block's terrain) */
    u8  b1;             /* +1: never read */
    u8  countdown;      /* +2: frames - 1 */
    u8  reload;         /* +3: a chained record's next countdown */
    s16 x, y;           /* +4/+6: pixels (the block's, or a shot's) */
    u16 block;          /* +8: the new block; its low byte is the chain's
                           next */
    u16 layout_ofs;     /* +$A: byte offset into the layout */
} HzMapFx;

/* The menu module's RAM ($FF9800-$FF9A2F): the title's and the options
 * screen's state (front.h). */
typedef struct {
    u16 pad_held;           /* m_pad_held: the last read (both pads OR'd,
                               the high byte $FF) */
    u16 pad_pressed;        /* m_pad_pressed: what that read found new */
    u16 cursor_x, cursor_y; /* m_cursor_x/y: the text cursor, in cells */
    u16 text_attr;          /* m_text_attr: in front, the palette line,
                               the font set's tile base */
    u8  vdp_reg1;           /* m_vdp_reg1: b6 display on */
    u16 palette[64];        /* m_palette: CRAM as the menus want it */
    u16 target[64];         /* m_palette_target */
    s16 fade_level;         /* m_fade_level: 0 full colour to $1F black */
    s16 cycle_pos;          /* m_cycle_pos: bytes into a colour cycle */
    s16 cycle_timer;        /* m_cycle_timer: a step when it runs out */
    s16 menu_item;          /* m_menu_item: 0 START .. 4 SOUND */
    s16 menu_item_shown;    /* m_menu_item_shown: drawn highlighted */
    s16 choice;             /* m_choice: 0 User / Split, 1 Computer / Wide */
    s16 choice_shown;
    s16 type;               /* m_type: the difficulty, 0-3 = A-D */
    s16 type_shown;
    s16 save_cursor;        /* the port's: CONT's slot cursor (CONT is the
                               saved games' page, not the password's) */
    s16 save_shown;
    s16 song;               /* m_song: the MUSIC test's */
    s16 sfx;                /* m_sfx: the SOUND test's */
    s16 map;                /* m_map: the map select's cursor */
    s16 map_shown;
    s16 scroll_x_b;         /* m_scroll_x_b/y_b: plane B's scroll, as */
    s16 scroll_y_b;         /*   M_ApplyScroll writes it (the game end) */
} HzFront;

/* A block's tile words (HzGame.blocks): bits of the nametable word that
 * the game also reads as the ground's. */
#define TILE_BLOCKED     0x8000 /* stops all but a boat */
#define TILE_STOPS_SHOTS 0x4000
#define TILE_EVENT       0x2000 /* a terrain event (its block's terrain) */
#define TILE_WATER       0x0200 /* a boat needs it */

/* A layout word (HzGame.layout): the block in bits 0-8. */
#define LAYOUT_PATH_HINT 0x0200 /* bits 10-15 hold the hint (path_hint) */

/* The port's gameplay fixes, HzGame.fixes's bits (the PATCHES page). */
#define HZ_FIX_SUPPLY    0x01u  /* a SUPPLY finds its way to units in a
                                   group and serves them (session 32) */
#define HZ_FIX_JET_GUN   0x02u  /* a jet's bullets can hit the enemy jet
                                   whatever was tested before them */
#define HZ_FIX_UNIT_AIM  0x04u  /* a unit fires at a target one step either
                                   side of straight up */
#define HZ_FIX_FIRE_RATE 0x08u  /* (session 39) a human's mech fires as a
                                   CPU's does, without waiting out
                                   mech_fire_delay; off by default */
#define HZ_FIX_GUARD_CHASE 0x10u  /* a guard-near unit turns to chase along
                                     its own bearing to the enemy */
#define HZ_FIX_CPU_HITS  0x20u  /* (session 55) a CPU side's hit halves the
                                   unit it hit, not the next record */
#define HZ_FIX_TURRETS   0x40u  /* (session 60) the order menu sells two
                                   gun turrets, types 6 and 7, from
                                   data/turrets.json; off by default */
#define HZ_FIX_UNITS     0x80u  /* (session 61) no limit to a side's units
                                   (the original's is 50); off by
                                   default */
#define HZ_FIX_PATHS     0x200u /* (session 71) units sent to the enemy HQ
                                   or an outpost drive round what is in
                                   the way by the ground's shortest way
                                   (paths.c); off by default. Bit 9: bit 8
                                   is HZ_FIX_DRAW's, in recordings too */
#define HZ_FIX_REPAIR    0x400u /* (session 72) the order menu sells a
                                   repair truck, type 10, whose two
                                   mechanics (type 11) mend the vehicles
                                   round it, from data/repair.json
                                   (docs/repair.md); off by default */
#define HZ_FIX_PIXELS    0x800u /* (session 75) objects collide, and shots
                                   hit, only where their drawn pixels
                                   touch (hitmask.c), not anywhere in
                                   their boxes; off by default */
#define HZ_FIX_COUNT     11
#define HZ_FIX_ALL       0xEFFu
/* (Session 66) Not a PATCHES row: a network game's own rule, set for it
 * alone (main.c's run_net_game, hz_netai), on a bit no PATCHES row has
 * (sysmenu.c's PATCH_BIT) so no page or settings file ever holds it: both
 * HQs down at the same second's check is a draw (v_winner HZ_DRAW), where
 * the 68k gives P2 the game. */
#define HZ_FIX_DRAW      0x100u
#define HZ_DRAW          2      /* v_winner for a draw */
#define HZ_FIX_DEFAULT   0x37u  /* the fixes on until the player says: all
                                   but FIRE RATE and TURRETS, which change
                                   the balance rather than mend a fault */

typedef struct HzGame HzGame;

/* Where the 68k waits for VBlank the port hands the frame to its host:
 * render it, present it, pace it. Called with the frame complete. */
typedef void (*HzFrameFn)(HzGame *g, void *user);

/* (Session 61) A side's units (v_units1/2 on the 68k, 50 records each):
 * slots in chunks of HZ_UNIT_CHUNK records, so a record never moves once
 * made. Every game starts each side with HZ_SIDE_UNITS free slots, as the
 * original's tables; with UNITS (HZ_FIX_UNITS) a side whose slots are all
 * taken grows by one. */
typedef struct {
    HzUnit **chunk;             /* chunk[i]: HZ_UNIT_CHUNK records */
    u32      chunks;            /* allocated */
    u32      slots;             /* in play: HZ_SIDE_UNITS or more */
} HzUnitList;

/* (Session 61) With UNITS, the grid the units' collisions with the
 * other side's go by (crowd.c): 8 px cells, a list a side (the first
 * slot + 1, through the units' fine_next) and a box round the places of
 * the units in it (their left and top edges, the widest; never too
 * small). Heap memory, the first time a game has UNITS. */
typedef struct {
    s16 x0, x1, y0, y1;
    u8  size;
} HzFineBox;

typedef struct {
    u32       head[2][HZ_FINE * HZ_FINE];
    HzFineBox box[2][HZ_FINE * HZ_FINE];
} HzCrowdFine;

/* (Session 61) With UNITS, the units' sprites, every frame, drawn behind
 * the rest (sprites.c, video_sprites_behind): as many as there are. */
typedef struct {
    Sprite *sprite;
    u32     count;
    u32     cap;                /* allocated */
} HzSpriteMore;

struct HzGame {
    const HzAssets *assets;     /* the game's data, read at start-up */
    Video       *video;       /* the draw layer (src/core/video.h): the
                                 host's; NULL, a copy that never draws */
    HzFrameFn    frame;
    void        *frame_user;

    /* (Session 61) The units, a list a side. Outside the block each new
     * game zeroes (from sprite_count on): units_reset clears them. An
     * HzGame starts zeroed (static, or memset) before its first
     * game_init; game_copy copies one with its units. */
    HzUnitList   unit_list[2];
    HzSpriteMore sprite_more;   /* (Session 61) kept as the lists */
    HzCrowdFine *crowd_fine;    /* (Session 61) kept as the lists */

    /* The host's side of the two controller ports, and a way to stop.
     * The frame hook sets them; ReadJoypads reads pad_in, and the frame
     * loop and the pause loops end once quit is set. */
    HzPad        pad_in[2];
    bool         quit;

    /* The Z80's mailbox: the host's sound (src/core/audio.c's
     * hz_audio_post) takes each command as the 68k's Snd_* routines
     * write it. NULL: no sound (the harnesses), the commands only
     * counted as below. */
    void       (*snd_post)(void *user, const u8 *bytes, int count);
    void        *snd_user;
    /* The driver's z_music_on, which the title polls to know when its song
     * has ended (with snd_user). NULL: always on, so the title waits for
     * Start. */
    bool       (*snd_music_on)(void *user);

    /* The saved game (saves.h) this run's record goes to: the one CONT
     * loaded, or -1 after START (a new one, given a slot when a win is
     * first recorded). The port's: on the cartridge the record went out
     * as a password. */
    int          save_slot;
    /* A test seam (tools/hz_front.c): options_run calls it as each page
     * of the options screen starts (HzFrontPage, front.h). */
    void       (*front_probe)(HzGame *g, int page, void *user);
    void        *front_probe_user;

    /* A test seam, the harness's (tools/hz_mech.c): when set, Mechs_Update
     * calls it just before (after = false) and after each Mech_Update. */
    void       (*mech_probe)(HzGame *g, int p, bool after, void *user);
    void        *mech_probe_user;
    /* Another (tools/hz_mech.c): game_frame calls it just before (after =
     * false) and after Combat_Update. */
    void       (*combat_probe)(HzGame *g, bool after, void *user);
    void        *combat_probe_user;
    /* Another, for the harnesses whose models don't follow the unit AI
     * (tools/hz_mech.c, tools/hz_shot.c; the AI has its own check, make
     * unitai): when set, unit_ai_update only steps v_unit_ai_group. */
    bool         units_frozen;
    /* Likewise for the order menus (tools/hz_mech.c; the menu has its own
     * check, make ordermenu): when set, order_menu_input and
     * order_menu_update do nothing, so a menu C opens stays as it is. */
    bool         menus_frozen;
    /* And for the CPU mechs' AI (mech_ai.c; its own check, make mechai):
     * when set, mech_update doesn't call mech_ai_think, so a CPU mech's
     * pad stays as it is (tools/hz_loop.c and the others whose models
     * don't follow it). */
    bool         cpus_frozen;
    /* For the harnesses (game_new sets it): the loading screen's frames
     * pass without being handed to the frame hook, so the harnesses'
     * frames and pads count from Game_Start as before; the screen's state
     * is the same at the end. tools/hzdemo_check.py checks the frames. */
    bool         quick_load;
    /* The port's gameplay fixes (FIX: in the code), where the original's
     * play is poor: a bit each (HZ_FIX_*), clear plays as the original.
     * game_init leaves them clear, so every check against the 68k sees
     * the original; main.c sets them from the settings (the Esc screen's
     * PATCHES page, sysmenu.c). */
    u32          fixes;
    /* (Session 57) Which AI plays a CPU side (HZ_BRAIN_*): the
     * original's (mech_ai.c) or the nightmare AI (nightmare.c). The host
     * sets it per side; game_init leaves both the original's. */
    u8           cpu_brain[2];
    /* (Session 59) A network game's own view: 0 the split screen as the
     * mode has it; 1 P1's view alone, wide, 2 P2's (Game_SetupMode). Both
     * sides stay human. The two machines show different sides of the same
     * game: the rival's order menu runs unseen as a shown one
     * (order_menu.c), and the state hash leaves the display out
     * (statehash.c). The host sets it; game_init 0. */
    u8           own_view;
    /* (Session 66) A network game's side on this machine: 0 none (not a
     * network game), 1 P1, 2 P2. The two machines differ in it, so it
     * changes only what each shows and plays: its own end (game_end.c:
     * YOU WIN or YOU LOSE, its song), and from the game's end the state
     * hash leaves the display out (statehash.c). The host sets it;
     * game_init 0. */
    u8           net_player;
    /* (Session 60) TURRETS has been on in this game (game_start, then
     * every game_frame), so types 6 and 7 stay turrets (turrets_on) until
     * the next game: switching the patch off mid-game only stops the shop
     * selling them, and the ones bought fight on. */
    bool         turrets;
    /* (Session 72) REPAIR likewise: on in this game (game_start, then
     * every game_frame), so types 10 and 11 stay the repair truck and
     * its mechanics (repair_on) until the next game. */
    bool         repair;
    /* And tools/hz_unitai.c's --game: unit_ai_update calls it on entry
     * (after = false) and on its way out. */
    void       (*unit_ai_probe)(HzGame *g, bool after, void *user);
    void        *unit_ai_probe_user;
    /* And tools/hz_mechai.c's: mech_ai_think calls it on entry (after =
     * false) and on its way out, with the mech. */
    void       (*mech_ai_probe)(HzGame *g, int p, bool after, void *user);
    void        *mech_ai_probe_user;
    /* (Session 57) And tools/hz_arena.c's A/B build: a CPU side whose
     * cpu_brain is HZ_BRAIN_PROBE thinks through it, in mech_ai_think's
     * place. */
    void       (*brain_probe)(HzGame *g, int p, void *user);
    void        *brain_probe_user;

    /* 68k status register's interrupt mask: false after move #$2700,sr.
     * VDP_WaitVBlank polls the VDP, so frames pass either way; only the
     * interrupt handlers stop. */
    bool interrupts_on;

    /* -- $FFF800 block ---------------------------------------------- */
    u16 hw_version;         /* v_hw_version: the version port's byte
                               (b7 overseas, b6 PAL); the title picks its
                               copyright line by b7 */
    u16 vdp_reg1;           /* v_vdp_reg1: shadow of VDP register 1 */
    u8  joypad[2];          /* v_joypad1/2 ($FFF844/5): HZ_PAD_* bits */
    u16 split_screen;       /* v_split_screen */
    u8  options[4];         /* v_options: mode, difficulty, option2, map */
    u8  win_record[4];      /* v_win_record: a byte per difficulty A-D, a
                               bit per map won against the computer */
    u16 view_cfg[2];        /* v_view_cfg: per player, hi byte view slot
                               (negative = hidden), lo byte view index */
    u8  player_flags[2];    /* v_player_flags */
    u16 mech_option2;       /* v_mech_option2 */
    u8  split_done;         /* v_split_done: HBlank has relinked sprite 5 */
    u8  hud_sprite_cut;     /* v_hud_sprite_cut: VDP_Init sets it with
                               split_done; VDP_UploadSprites then uploads
                               sprite 5 with link 0 */
    u8  sfx_priority;       /* v_sfx_priority: $FF = nothing queued */
    u8  sfx_queued;         /* v_sfx_queued */
    u8  sfx_sent;           /* the port's: what the last Snd_FlushSfx sent,
                               $FF none */
    u32 sfx_asked;          /* (session 54) the port's: bit n, effect n was
                               asked of Snd_QueueSfx since the host last
                               cleared it, queued or not (its priority may
                               have lost); the rumble's (game/rumble.h) */
    u8  snd_pause_sent;     /* the port's: how many Snd_Pause (the pause's
                               sound command) have gone out, and */
    u8  snd_pause_arg;      /* the last one's byte */
    u16 game_over;          /* v_game_over */
    u16 winner;             /* v_winner: 0 P1, 1 P2, (session 66)
                               HZ_DRAW both (HZ_FIX_DRAW) */
    u16 mech_spawns[2];     /* v_mech_spawns: per team */
    HzPoint *view_camera[2];/* v_view_camera_a/b */
    s16 game_type;          /* v_game_type: negative = attract demo */
    u16 map;                /* v_map */
    u16 sprite5_link;       /* v_sprite5_link: word HBlank writes */
    u16 sprite5_link_next;  /* v_sprite5_link_next: Sprites_Build's, copied
                               by VDP_UploadSprites */
    u16 split_enable;       /* v_split_enable */
    u32 frame_counter;      /* v_frame_counter; its low word is
                               v_frame_counter_lo, the demo's timeout */
    u16 free_ammo;          /* v_free_ammo: firing costs no ammo */
    u16 cpu_boost;          /* v_cpu_boost: $8000 P1 CPU, $8010 P2 CPU,
                               0 off (Game_SecondTick) */
    u16 units_built[2];     /* v_units_built: per team, saturating */
    u8  results_bases[4];   /* v_results_bases: Game_Over's copy of
                               v_outpost_count, for the results' BASE */
    u16 cpu_boost_hq_hp;    /* v_cpu_boost_hq_hp: $199-$398 (Random) */
    u32 rng_seed;           /* v_rng_seed ($FFF7C0) */
    u32 rng_seed_saved;     /* v_rng_seed_saved: Game_SecondTick keeps it
                               each frame outside demos; a new game starts
                               from it. 0 after a cold boot */
    /* The attract demo (demo.c) */
    u16 demo_index;         /* v_demo_index: the next of assets->demos */
    u16 demo_pos[2];        /* v_demo_ofs1/2: each pad's current run (the
                               68k keeps it as a byte offset, 2 a run) */
    u8  demo_frames[2];     /* v_demo_run1/2: frames left of the run */
    /* The loading screen (load.c) */
    u8  load_step;          /* v_load_anim's high byte: the walk, 0-2 */
    u8  load_delay;         /* v_load_anim_delay: frames to the next step */
    u16 load_busy;          /* v_load_busy: frames left of the screen */
    u16 load_abort;         /* v_load_abort: Start in a demo's load */
    HzSprite load_sprites[2];   /* v_load_sprites: VDP sprites 6 and 7 */
    u16 load_mech_frame;    /* v_load_mech_frame */
    bool vblank_loading;    /* the VBlank stub's target is VBlank_Loading
                               (LoadScreen_Start) until IntStubs_Install */
    bool vblank_pending;    /* a VBlank came with interrupts masked: the
                               VDP holds it until they're unmasked */

    /* -- map data ($FF0000-$FF5FFF) ------------------------------------ */
    u16 blocks[HZ_BLOCKS][16];     /* v_map_blocks: 4 x 4 nametable words */
    u16 block_terrain[HZ_BLOCKS];  /* v_block_terrain: low byte = type */
    u16 anim_tiles[HZ_ANIM_RECORDS][16];    /* v_anim_tiles records */
    u8  anim_tile_gfx[HZ_ANIM_RECORDS][32]; /* v_anim_tile_gfx */
    u16 anim_tile_shown[HZ_ANIM_RECORDS];   /* v_anim_tile_shown: the tile
                                               now in word 0's slot */
    u8  anim_tile_step[HZ_ANIM_RECORDS];    /* v_anim_tile_step: 0-15, b7
                                               off (word 0 is tile 0) */
    /* v_unpack_buf holds the map's tiles, then its layout. Kept apart
     * here; nothing reads the tiles after the layout unpack. */
    u8  map_tiles[HZ_BLOCKS * 32];
    u16 layout[HZ_LAYOUT_SIZE][HZ_LAYOUT_SIZE];  /* low 9 bits block;
                                                    bits 10-13 outpost */

    /* -- $FF8000 block ----------------------------------------------- *
     * MainLoop_Title clears $FF7000-$FFEFFF before every game (after
     * Game_LoadMap); game_init's memset stands in for it until that loop
     * is ported, so every field from here on starts a game at 0. */
    u16      sprite_count;      /* v_sprite_count */
    u16      sprite_mux_phase;  /* v_sprite_mux_phase: 0..$B */
    s16      scroll_x[2];       /* v_scroll_x_a/b */
    s16      scroll_y[2];       /* v_scroll_y_a/b */
    s16      scroll_prev_x[2];  /* v_scroll_x_a/b_prev: as Map_ScrollUpdate
                                   last saw them */
    s16      scroll_prev_y[2];
    HzSprite sprite_fixed[10];  /* v_sprite_fixed: sprites 0-9. 6/7: P1/P2
                                   in its own view, 8/9: in the rival's */
    HzSprite sprite_list[HZ_SPRITE_LIST];   /* v_sprite_list: sprites
                                               $A up, as built */
    HzPost   posts[HZ_POST_CLEARED];
    HzPoint  camera[2];         /* v_camera1/2 */
    HzMech   mech[2];
    HzNightmare nightmare[2];   /* (session 57) nightmare.c's, per mech */
    HzShot   shots[HZ_SHOTS];   /* v_bullets1 ... v_missiles2 */
    HzFront  front;             /* the menu module's ($FF9800) */
    u16      unit_ai_group;     /* v_unit_ai_group ($FF8A70): 0..$B,
                                   UnitAI_Update's; an outpost's service
                                   and damaging ground act on group 0 */
    u16      hit_flags;         /* v_hit_flags: probe result, PROBE_* */
    HzRef    hit_id;            /* v_hit_id: the object found */
    u16      terrain_event;     /* v_terrain_event */
    u16      terrain_event2;    /* v_terrain_event2: the layout offset
                                   (bytes) of a unit's or shot's tile */
    u16      terrain_event3;    /* v_terrain_event3: the layout word of
                                   the tile that had the event */
    u16      unit_team_toggle;  /* v_unit_team_toggle ($FF8A6E): b0 the
                                   team Combat_Update moves this frame */
    HzMapFx  mapfx[HZ_MAPFX];   /* v_mapfx ($FFD4D4) */
    HzRef    grid[HZ_GRID * HZ_GRID];  /* v_grid: 32 px cells, the
                                   occupant (a mech or a unit) */
    /* (Session 61) With UNITS the grid takes any number of units a cell
     * (crowd.c): v_grid stays empty, and each cell has a list a side
     * (crowd_head: the first slot + 1, 0 none; through the units'
     * crowd_next) and its length; a mech has its crowd_cell. `crowd`:
     * which of the two the grid is now. (And crowd_fine.) */
    bool     crowd;
    u32      crowd_head[2][HZ_GRID * HZ_GRID];
    u32      crowd_count[2][HZ_GRID * HZ_GRID];
    u32      unit_count[2];     /* v_unit_count: per team */
    u32      unit_type_count[2][16];   /* v_unit_type_count */
    u8       outpost_count[4];  /* v_outpost_count: P1, P2, neutral,
                                   total */
    u16      second_timer;      /* v_second_timer ($FFDC24): 0..$3B */
    u16      mech_tick;         /* v_mech_tick ($FFDC26): Mechs_Update's
                                   count */
    u16      fade_steps;        /* v_fade_steps: Palette_FadeOutStep's
                                   steps left (Game_Over's fade) */
    u16      map_palette[16];   /* v_map_palette */

    /* -- $FFD654 block ------------------------------------------------ */
    u8            scroll_queued[4]; /* v_scroll_queued: row A, column A,
                                       row B, column B */
    HzScrollQueue row_queue[2];     /* v_row_queue_a/b */
    HzScrollQueue col_queue[2];     /* v_col_queue_a/b */
    u16           sprite_window;    /* v_sprite_window: the 68k keeps a
                                       pointer; here the first entry */
    u16           blink_timer;      /* v_blink_timer: 0..$3F */
    HzSprite      radar_sprites[2][HZ_RADAR_SPRITES]; /* v_radar_sprites1/2 */
    u8            radar_buf[2][HZ_RADAR_IMAGE];       /* v_radar_buf1/2: the
                                     order menus' radar tiles */
    HzGaugeBar    gauge_queue[HZ_GAUGE_BARS];         /* v_gauge_queue */
    HzAnimTileJob anim_tile_queue[HZ_ANIM_RECORDS];   /* v_anim_tile_queue */
    u16           colour_cycle[2];  /* v_colour_cycle: ColourCycles' steps */
};

/* game.c */
void game_init(HzGame *g, const HzAssets *assets, Video *video);
void game_wait_vblank(HzGame *g);           /* VDP_WaitVBlank */
void game_unmask(HzGame *g);                /* move.w #$2000,sr */
void game_display_on(HzGame *g);            /* VDP_DisplayOn */
void game_display_off(HzGame *g);           /* VDP_DisplayOff */
void game_setup_mode(HzGame *g);            /* Game_SetupMode */
void game_set_view_cameras(HzGame *g);      /* Game_SetViewCameras */
void game_start(HzGame *g);                 /* Game_Start */
u32  game_random(HzGame *g);                /* Random */

/* MainLoop_Title from Game_SetupMode to Map_ScanOutposts: the mode, the
 * VDP, the map, the per-game RAM cleared. False for a map number past 7.
 * menu_title_flow (front.c) comes before it and fills in v_options;
 * the harnesses fill them in themselves. */
bool game_new(HzGame *g);

/* MainLoop_Title's start ($41C): the music stopped and the effects, game
 * over cleared, interrupts masked, the split blocked. Then
 * MenuModule_Title, menu_title_flow: the title, and with Start the
 * options; -1 if the title ran out (the attract demo), else 0 (a game;
 * v_options as the options left them). */
void game_title_prepare(HzGame *g);
int  menu_title_flow(HzGame *g);            /* MenuModule_Title */
/* MainLoop_Title after the menus, up to Game_Start (game_new is
 * game_title_prepare and this): a game ready to start, or a demo's load
 * aborted with Start (back to the title), or a map number past 7. */
typedef enum {
    HZ_SETUP_GAME,
    HZ_SETUP_ABORTED,
    HZ_SETUP_NO_MAP
} HzSetup;
HzSetup game_setup_after_title(HzGame *g);

/* demo.c: the attract demo */
void demo_setup_next(HzGame *g);            /* Demo_SetupNext */
void demo_start(HzGame *g);                 /* Demo_Start */
void demo_play_input(HzGame *g);            /* Demo_PlayInput */

/* game_end.c: MenuModule_GameEnd -- WINNER, the results (the win
 * recorded and saved, saves.h, where the game made the password), and
 * with every map won on every difficulty the endings (ending.c,
 * Ending_Run). game_over.c: Game_Over, the HQ's destruction, then
 * menu_game_end; the caller goes back to the title. */
void menu_game_end(HzGame *g);
void ending_run(HzGame *g);                 /* Ending_Run */
void ending_preview(HzGame *g, int human);  /* --ending: 1 P1, 2 P2 */
void game_over(HzGame *g);                  /* Game_Over */

/* Game_FrameLoop ($700): one pass, VBlank included. Game_Start's first
 * pass enters at Game_FrameLoopEntry ($704), past ReadJoypads
 * (read_pads false). */
HzLoopExit game_frame(HzGame *g, bool read_pads);

void game_read_joypads(HzGame *g);          /* ReadJoypads */
void game_check_pause(HzGame *g);           /* Game_CheckPause */
HzLoopExit game_second_tick(HzGame *g);     /* Game_SecondTick */

/* Game_FrameLoop from HUD_UpdateGauges up to VDP_WaitVBlank, as far as it
 * is ported: HUD_UpdateGauges, Sprites_Build, Map_ScrollUpdate,
 * Sprites_Multiplex. */
void game_frame_draw(HzGame *g);

/* Game_FrameLoop from VDP_WaitVBlank on: the frame goes to the host, then
 * the VBlank-time uploads. */
void game_frame_end(HzGame *g);

/* mech.c */
void mechs_init(HzGame *g);                 /* Mechs_Init (and Mech2_Init) */
void mech_respawn(HzGame *g, HzMech *m, const HzMech *rival);
                                            /* Mech_Respawn */
void mech_follow_camera(HzGame *g, int p);  /* Mechs_Update, camera part */
void mechs_update(HzGame *g);               /* Mechs_Update */
void mech_update(HzGame *g, int p);         /* Mech_Update */
/* (Session 57) For the nightmare AI: Mech_GetTerrain (GROUND_LAND 0,
 * SPECIAL 1, BLOCKED 2, LOW 3, WATER 4) and Mech_CheckLandBlocked as they
 * would be with the mech at (x, y). */
int  mech_ground_at(const HzGame *g, const HzMech *m, s16 x, s16 y);
bool mech_land_blocked_at(HzGame *g, const HzMech *m, s16 x, s16 y);
void grid_mark_mech(HzGame *g, HzMech *m, HzRef id);   /* Grid_MarkMech */
s16  obj_accelerate_axis(s16 vel, s16 target, s16 step);
                                            /* Obj_Accelerate, one axis */

/* unitlist.c (session 61): the units' storage. A side's slot `slot` (below
 * side_slots); the unit an HzRef names (REF_UNIT; NULL for anything else,
 * or a slot not in play); every slot free again, HZ_SIDE_UNITS a side
 * (game_init and every new game); every record in play cleared, the
 * slots kept (the game over's); one slot more for a side (false if no
 * memory); a whole HzGame, units and the nightmare AI's lists too. */
HzUnit       *unit_at(HzGame *g, int side, u32 slot);
const HzUnit *unit_at_c(const HzGame *g, int side, u32 slot);
u32           side_slots(const HzGame *g, int side);
HzUnit       *ref_unit_ptr(HzGame *g, HzRef r);
const HzUnit *ref_unit_ptr_c(const HzGame *g, HzRef r);
HzRef         unit_ref(const HzUnit *u);    /* REF_UNIT of its place */
void          units_reset(HzGame *g);
void          units_clear_all(HzGame *g);   /* every slot's record, kept */
bool          side_grow(HzGame *g, int side);
void          game_copy(HzGame *dst, const HzGame *src);
/* QUIRK, a CPU side's hits (UnitAI_CheckShotResult, AI_Ground_Shoot): the
 * 68k took a victim code (slot + 1, + 50 for team 2) as a record number, so
 * the record after the victim's is the one hit: the next slot of its side,
 * after team 1's last team 2's first, after team 2's last none; no victim
 * yet (code 0) team 1's slot 0. Not a unit: none. */
HzUnit       *victim_next_record(HzGame *g, HzRef v);
/* (Session 61) UNITS: no limit to a side's units in this game (not in the
 * attract demo, whose recordings are the original's). */
bool          units_unlimited(const HzGame *g);
/* A side may have another unit: under HZ_SIDE_UNITS, or no limit. */
bool          side_has_room(const HzGame *g, int side);

/* units.c */
int  unit_spawn(HzGame *g, u8 flags, u8 type, u8 order, u8 dir, s16 x,
                s16 y);                     /* Unit_Spawn: 0, or -1 */
int  unit_create_carried(HzGame *g, u8 flags, u8 type, u8 order,
                         u32 *slot);        /* Unit_CreateCarried: 0, or -1 */
const HzUnitStats *unit_stats_for(const HzGame *g, u8 type);
/* (Session 60) TURRETS: whether types 6 and 7 are turrets in this game,
 * and the unit types the order menu offers on this map. */
bool turrets_on(const HzGame *g);
u16  shop_types(const HzGame *g);
/* (Session 72) REPAIR (docs/repair.md): whether types 10 and 11 are the
 * repair truck and its mechanic in this game; an order's name and fee
 * and its icon as the order menu shows them (order $E the truck's
 * then); the sprites' colour tables on or off as the patch is (each
 * game's start, and when the patch comes on in play); a unit's record
 * freed (destroyed, or gone with the mech that carried it): a mechanic
 * off his truck's crew, a truck's men out turned into INFANTRY; a
 * mechanic turned into INFANTRY where he stands; Unit_Spawn giving the
 * new unit (NULL: none). */
bool repair_on(const HzGame *g);
const HzOrderInfo *order_info_for(const HzGame *g, u8 order);
const u8 *order_icon_for(const HzGame *g, u8 order);
void repair_set_colours(HzGame *g);
void repair_unit_freed(HzGame *g, const HzUnit *u);
void mechanic_to_infantry(HzGame *g, HzUnit *u);
HzUnit *unit_spawn_unit(HzGame *g, u8 flags, u8 type, u8 order, u8 dir,
                        s16 x, s16 y);
/* The truck's ammo a man of its crew (its record's ammo: 2 men). */
#define REPAIR_MAN 4
/* The record of the unit a mech carries: its side's mech_carried_slot
 * (terrain.c). */
HzUnit *carried_unit(HzGame *g, const HzMech *m);
                                            /* UnitStats + type x $20 */
void unit_set_sprite(HzGame *g, HzUnit *u, u16 pal);   /* Unit_SetSprite */
void units_spawn_cpu_start(HzGame *g);      /* Units_SpawnCPUStart */
void units_update_team(HzGame *g, int team);    /* Units_UpdateTeam */
void obj_check_terrain(HzGame *g, const HzUnit *u); /* Obj_CheckTerrain */
void grid_mark_unit(HzGame *g, HzUnit *u);  /* Grid_MarkObj, its own ref */
void grid_clear_unit(HzGame *g, HzUnit *u); /* Grid_ClearObj */

/* crowd.c (session 61): the grid with UNITS, any number of units a cell.
 * grid_sync puts the grid in the form units_unlimited asks for, the
 * units and mechs in it again (each frame's start; a game's settings can
 * change in play). The rest is for g->crowd. A cell: 0-4095, row x 64 +
 * column, of a point clamped to the map. */
void    grid_sync(HzGame *g);
u16     crowd_cell_of(s16 x, s16 y);
void    crowd_link(HzGame *g, HzUnit *u);   /* in its centre's cell */
void    crowd_unlink(HzGame *g, HzUnit *u); /* out of any */
const HzUnit *crowd_first(const HzGame *g, int side, u16 cell);
const HzUnit *crowd_after(const HzGame *g, const HzUnit *u);
/* Unit u's collision: the first object it overlaps -- a robot, a unit of
 * the other side, then (session 68) one of its own -- by the original's
 * test (`bumper`: a SUPPLY's $C round it); none if none. A unit moved
 * from `from` (a spawn: NULL) passes one it overlapped there too when the
 * move takes their centres further apart. */
HzRef         crowd_hit(HzGame *g, const HzUnit *u, bool bumper,
                        const HzPoint *from);
/* game_copy's: the fine grid (its own, allocated as needed). */
void          crowd_copy(HzGame *dst, const HzGame *src);
bool    crowd_mech_in(const HzGame *g, int p, u16 cell);
bool    crowd_cell_taken(const HzGame *g, u16 cell);    /* a unit or mech */
/* The 3 x 3 cells round `cell` (it first), those on the map; how many. */
int     crowd_cells_round(u16 cell, u16 out[9]);
/* Whether every unit and mech is in the grid where it should be, the
 * lists whole, and (`apart`) no two units overlapping (the checks'; a
 * message on stderr for the first fault). */
bool    crowd_check(const HzGame *g, bool apart);

/* hitmask.c (session 75): the PIXELS patch, what an object draws as a
 * shape to test collisions with. A shape: `h` rows from (x, y), bit 31
 * of a row the pixel at x, cut to the object's box (`size` px from its
 * corner). */
typedef struct {
    s16 x, y;
    u8  size;
    u8  h;
    u32 row[32];
} HzShape;
bool hitmask_on(const HzGame *g);           /* HZ_FIX_PIXELS */
void hitmask_unit(const HzGame *g, const HzUnit *u, HzShape *s);
/* Unit u as it will be drawn once set down at (x, y), facing `dir`
 * (Unit_Spawn's, mech_drop's: its sprite isn't set yet). */
void hitmask_unit_new(const HzGame *g, const HzUnit *u, u8 dir, s16 x,
                      s16 y, HzShape *s);
void hitmask_mech(const HzGame *g, const HzMech *m, HzShape *s);
/* Jet m as the robot it lands as: the standing frame for its heading. */
void hitmask_robot(const HzGame *g, const HzMech *m, HzShape *s);
void hitmask_shot(const HzGame *g, const HzShot *sh, HzShape *s);
void hitmask_ref(const HzGame *g, HzRef id, HzShape *s);  /* unit, mech */
bool hitmask_touch(const HzShape *a, const HzShape *b);
/* Whether a, moved from `from` (NULL: set down, not moved), runs into b:
 * they touch, and didn't at `from` or the move doesn't take their
 * middles further apart. */
bool hitmask_blocks(const HzShape *a, const HzPoint *from,
                    const HzShape *b);

/* shots.c */
int  shot_fire_bullet(HzGame *g, HzRef shooter, u8 flags, u8 dir, s16 x,
                      s16 y, u16 speed, u16 life);  /* Shot_FireBullet: 0, -1 */
int  shot_fire_missile(HzGame *g, HzRef shooter, u8 flags, u8 dir, s16 x,
                       s16 y, u16 speed, u16 life); /* Shot_FireMissile */
void combat_update(HzGame *g);              /* Combat_Update */
u8   get_direction(s16 x0, s16 y0, s16 x1, s16 y1); /* GetDirection */

/* unit_ai.c */
void unit_ai_update(HzGame *g);             /* UnitAI_Update */
int  grid_scan_rings(const HzGame *g, s16 x, s16 y, int rings, HzRef *ids,
                     int max);              /* Grid_ScanRings */

/* terrain.c: Obj_TerrainEvent for a mech; `kind` is the 68k's d0, 1 for
 * a robot and 2 for a jet (carrying or not). */
#define HZ_OBJ_UNIT  0
#define HZ_OBJ_ROBOT 1
#define HZ_OBJ_JET   2
#define HZ_OBJ_SHOT  3
void mech_terrain_event(HzGame *g, HzMech *m, int kind);
void unit_terrain_event(HzGame *g, HzUnit *u);  /* d0 0 */
void shot_terrain_event(HzGame *g, HzShot *s);  /* d0 3 */

/* mapfx.c */
HzMapFx *mapfx_alloc(HzGame *g);            /* MapFx_Alloc: NULL if full */
void map_fx_update(HzGame *g);              /* MapFx_Update */

/* hud.c */
void hud_update_gauges(HzGame *g);          /* HUD_UpdateGauges */
void hud_queue_bar(HzGame *g, u16 value, u16 max, int cells, LayerId layer,
                   int col, int row);
                                            /* HUD_QueueBar */
void hud_flush_bars(HzGame *g);             /* HUD_FlushBars */

/* sprites.c */
void sprites_build(HzGame *g);              /* Sprites_Build */
void sprites_multiplex(HzGame *g);          /* Sprites_Multiplex */
void sprites_upload(HzGame *g);             /* VDP_UploadSprites */
Sprite sprite_from_ram(const HzSprite *s);
bool sprites_show_one(HzGame *g, const HzSprite *s);

/* The sound effects (the Z80 driver's numbers, SfxPriority's order). */
enum {
    SFX_FIRE_ROBOT       = 0x00,
    SFX_FIRE_JET         = 0x01,
    SFX_FIRE_UNIT        = 0x02,
    SFX_FIRE_MISSILE     = 0x03,
    SFX_MECH_DESTROYED   = 0x04,    /* shot down, out of fuel, or the
                                       ground */
    SFX_UNIT_DESTROYED   = 0x05,
    SFX_INFANTRY_DESTROYED = 0x06,
    SFX_HIT_UNIT         = 0x07,
    SFX_HIT_MECH         = 0x08,    /* a target of size $20 */
    SFX_HIT_TILE         = 0x09,
    SFX_TAKE_OFF         = 0x0A,
    SFX_LAND             = 0x0B,
    SFX_LIFT             = 0x0C,    /* a unit picked up */
    SFX_DROP             = 0x0D,
    SFX_JETS_MEET        = 0x0E,
    SFX_MENU_OPEN        = 0x0F,
    SFX_MENU_MOVE        = 0x10,
    SFX_ORDERED          = 0x11,
    SFX_UNIT_READY       = 0x12,
    SFX_HQ_ALERT         = 0x13,    /* the HQ is being hit */
    SFX_MISSILE_ALERT    = 0x14,    /* a missile homing in */
    SFX_HOLD_RAISED      = 0x15,    /* an INFANTRY raised its side's hold */
    SFX_POST_TAKEN       = 0x16,    /* Outpost_ShowOwner */
    SFX_HQ_HIT           = 0x17,
    SFX_HQ_DESTROYED     = 0x18,
    SFX_REFUSED          = 0x1A,    /* can't buy, or 50 units already */
    SFX_DEBRIS_END       = 0x1B,
    SFX_SIZZLE           = 0x1C     /* the ground hurts */
};

/* sound.c: the 68k side of the sound effect queue */
void snd_init(HzGame *g);                   /* Snd_Init's commands */
void snd_play_music(HzGame *g, u16 song);   /* Snd_PlayMusic */
void snd_start_song(HzGame *g, u8 repeat);  /* Snd_StartSong */
void snd_fade_music(HzGame *g, u8 speed);   /* Snd_FadeMusic */
void snd_stop_sfx(HzGame *g);               /* Snd_StopSfx */
void snd_queue_sfx(HzGame *g, u8 sfx);      /* Snd_QueueSfx */
void snd_flush_sfx(HzGame *g);              /* Snd_FlushSfx */
void snd_pause(HzGame *g, u8 arg);          /* Snd_Pause */

/* screen.c */
void screen_start_game(HzGame *g);          /* VDP_Init */
void screen_init_game(HzGame *g);           /* Screen_InitGame */
void sprites_init_fixed(HzGame *g);         /* Sprites_InitFixed */
void screen_divider(const HzGame *g, HzSprite out[6]);
void palette_fade_in_map(HzGame *g);        /* Palette_FadeInMap */

/* load.c */
bool game_load_map(HzGame *g);              /* Game_LoadMap */
void load_screen_start(HzGame *g);          /* LoadScreen_Start */
void vblank_loading(HzGame *g);             /* VBlank_Loading */
void map_init_anim_tiles(HzGame *g);        /* Map_InitAnimTiles */

/* anim.c */
void map_anim_tiles_update(HzGame *g);      /* Map_AnimTilesUpdate */
void map_anim_tiles_upload(HzGame *g);      /* Map_AnimTilesUpload */

/* map.c */
void map_scan_outposts(HzGame *g);          /* Map_ScanOutposts */
void map_draw_views(HzGame *g);             /* Map_DrawViews */
void map_draw_view_b(HzGame *g);            /* Map_DrawViewB */
void map_draw_view_wide(HzGame *g);         /* Map_DrawViewWide */
void map_redraw_view_rows(HzGame *g, const HzMech *m, int first_row,
                          int rows);        /* Map_RedrawPlayerView */
void map_scroll_update(HzGame *g);          /* Map_ScrollUpdate */
void map_flush_scroll(HzGame *g);           /* Map_FlushScroll */

/* mech_ai.c: the CPU's mech; its only output is the mech's pad, but it
 * also steers (obj_dir) and pays itself money. */
void mech_ai_think(HzGame *g, int p);       /* AI_MechThink */

/* order_menu.c: the order menu, C over the mech's view. */
void order_menu_input(HzGame *g, HzMech *m);    /* OrderMenu_Input */
void order_menu_update(HzGame *g);              /* OrderMenu_UpdateBoth */

#endif /* HZ_GAME_H */
