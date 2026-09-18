#ifndef SENTINEL_ARSENAL_H
#define SENTINEL_ARSENAL_H
#include "sentinel_native.h"

/* Sentinel Core Native Arsenal State Machine & Challenge Lifecycle.
   Represents weapon mods, mod selections, purchased normal upgrades,
   AP Mastery projections, and Mission/Mastery challenge lifecycles.
   All facts remain distinct and cannot collapse into a single unlocked boolean. */
#define SC_ARSENAL_ABI_VERSION 1u

enum {
    SC_ARSENAL_OBSERVE = 0,
    SC_ARSENAL_ENSURE_MODS = 1,
    SC_ARSENAL_SELECT_MOD = 2,
    SC_ARSENAL_PURCHASE_UPGRADE = 3,
    SC_ARSENAL_PROJECT_MASTERY = 4,
    SC_ARSENAL_UPDATE_CHALLENGE = 5
};

/* Weapons (8 main arsenal weapons + special weapons) */
enum {
    SC_ARSENAL_WEAPON_SHOTGUN         = 1u << 0,  /* 7770900 / 7770000 Combat Shotgun */
    SC_ARSENAL_WEAPON_SUPER_SHOTGUN   = 1u << 1,  /* 7770003 Super Shotgun */
    SC_ARSENAL_WEAPON_HEAVY_CANNON    = 1u << 2,  /* 7770000 Heavy Cannon */
    SC_ARSENAL_WEAPON_CHAINGUN        = 1u << 3,  /* 7770005 Chaingun */
    SC_ARSENAL_WEAPON_PLASMA_RIFLE    = 1u << 4,  /* 7770001 Plasma Rifle */
    SC_ARSENAL_WEAPON_BALLISTA        = 1u << 5,  /* 7770004 Ballista */
    SC_ARSENAL_WEAPON_ROCKET_LAUNCHER = 1u << 6,  /* 7770002 Rocket Launcher */
    SC_ARSENAL_WEAPON_BFG             = 1u << 7,  /* 7770006 BFG 9000 */
    SC_ARSENAL_WEAPON_UNMAYKR         = 1u << 8,  /* 7770008 Unmaykr */
    SC_ARSENAL_WEAPON_CRUCIBLE        = 1u << 9,  /* 7770007 / 7770901 Crucible */
    SC_ARSENAL_WEAPON_HAMMER          = 1u << 10  /* 7770009 / 7770902 Sentinel Hammer */
};
#define SC_ARSENAL_ALL_WEAPONS 0x7FFu

/* 13 Weapon Mods */
enum {
    SC_ARSENAL_MOD_SHOTGUN_STICKY_BOMBS  = 1u << 0,  /* 7770058 Sticky Bombs */
    SC_ARSENAL_MOD_SHOTGUN_FULL_AUTO     = 1u << 1,  /* 7770059 Full Auto */
    SC_ARSENAL_MOD_HEAVY_PRECISION_BOLT  = 1u << 2,  /* 7770060 Precision Bolt */
    SC_ARSENAL_MOD_HEAVY_MICRO_MISSILES  = 1u << 3,  /* 7770061 Micro Missiles */
    SC_ARSENAL_MOD_PLASMA_HEAT_BLAST     = 1u << 4,  /* 7770062 Heat Blast */
    SC_ARSENAL_MOD_PLASMA_MICROWAVE      = 1u << 5,  /* 7770064 Microwave Beam */
    SC_ARSENAL_MOD_ROCKET_REMOTE_DET     = 1u << 6,  /* 7770071 Remote Detonate */
    SC_ARSENAL_MOD_ROCKET_LOCK_ON        = 1u << 7,  /* 7770073 Lock-on Burst */
    SC_ARSENAL_MOD_SSG_MEAT_HOOK         = 1u << 8,  /* 7770083 Meat Hook */
    SC_ARSENAL_MOD_BALLISTA_ARBALEST     = 1u << 9,  /* 7770075 Arbalest */
    SC_ARSENAL_MOD_BALLISTA_DESTROYER    = 1u << 10, /* 7770077 Destroyer Blade */
    SC_ARSENAL_MOD_CHAINGUN_TURRET       = 1u << 11, /* 7770081 Mobile Turret */
    SC_ARSENAL_MOD_CHAINGUN_SHIELD       = 1u << 12  /* 7770079 Energy Shield */
};
#define SC_ARSENAL_ALL_MODS 0x1FFFu
#define SC_ARSENAL_ATTACHMENT_MEAT_HOOK SC_ARSENAL_MOD_SSG_MEAT_HOOK

/* 28 Authored Normal Mod Upgrades across 13 families */
enum {
    SC_ARSENAL_UPG_SHOTGUN_STICKY_RECHARGE   = 1u << 0,  /* Quick Flinch / Faster Recharge (3 WUP) */
    SC_ARSENAL_UPG_SHOTGUN_STICKY_EXPLOSION  = 1u << 1,  /* Bigger Boom / Larger Explosion (6 WUP) */
    SC_ARSENAL_UPG_SHOTGUN_AUTO_RECOVERY     = 1u << 2,  /* Quick Recovery (1 WUP) */
    SC_ARSENAL_UPG_SHOTGUN_AUTO_CHARGE       = 1u << 3,  /* Faster Spin-up (3 WUP) */
    SC_ARSENAL_UPG_SHOTGUN_AUTO_SPEED        = 1u << 4,  /* Fast Hands / Movement Speed (5 WUP) */
    SC_ARSENAL_UPG_HEAVY_BOLT_MOVEMENT       = 1u << 5,  /* Mobility (3 WUP) */
    SC_ARSENAL_UPG_HEAVY_BOLT_RELOAD         = 1u << 6,  /* Fast Re-Chamber (6 WUP) */
    SC_ARSENAL_UPG_HEAVY_BURST_RECHARGE      = 1u << 7,  /* Quick Loader (1 WUP) */
    SC_ARSENAL_UPG_HEAVY_BURST_CHARGE        = 1u << 8,  /* Instant Loader (3 WUP) */
    SC_ARSENAL_UPG_HEAVY_BURST_PRIMARY       = 1u << 9,  /* Primary Charge (5 WUP) */
    SC_ARSENAL_UPG_PLASMA_AOE_NO_DELAY       = 1u << 10, /* Super Heated / No Primary Delay (3 WUP) */
    SC_ARSENAL_UPG_PLASMA_AOE_CHARGE         = 1u << 11, /* Compressed Heat / Faster Charge (6 WUP) */
    SC_ARSENAL_UPG_PLASMA_MICRO_CHARGE       = 1u << 12, /* Fast Beam / Faster Charge (3 WUP) */
    SC_ARSENAL_UPG_PLASMA_MICRO_RANGE        = 1u << 13, /* Increased Range (6 WUP) */
    SC_ARSENAL_UPG_ROCKET_DET_FLARE          = 1u << 14, /* Proximity Flare (3 WUP) */
    SC_ARSENAL_UPG_ROCKET_DET_CONCUSSIVE     = 1u << 15, /* Concussive Blast (6 WUP) */
    SC_ARSENAL_UPG_ROCKET_LOCK_RECOVERY      = 1u << 16, /* Fast Reset (3 WUP) */
    SC_ARSENAL_UPG_ROCKET_LOCK_TIME          = 1u << 17, /* Quick Lock (6 WUP) */
    SC_ARSENAL_UPG_SSG_HOOK_RELOAD           = 1u << 18, /* Quick Hook / Faster Reload (3 WUP) */
    SC_ARSENAL_UPG_SSG_DEFAULT_RELOAD        = 1u << 19, /* Fast Hands / Default Reload (6 WUP) */
    SC_ARSENAL_UPG_BALLISTA_ARBALEST_MOVE    = 1u << 20, /* Full Speed (3 WUP) */
    SC_ARSENAL_UPG_BALLISTA_ARBALEST_EXPLODE = 1u << 21, /* Bigger Boom (6 WUP) */
    SC_ARSENAL_UPG_BALLISTA_DESTROY_AOE      = 1u << 22, /* Buffer Blade / AoE (3 WUP) */
    SC_ARSENAL_UPG_BALLISTA_DESTROY_CHARGE   = 1u << 23, /* Rapid Charge (6 WUP) */
    SC_ARSENAL_UPG_CHAINGUN_TURRET_EQUIP     = 1u << 24, /* Rapid Deployment (3 WUP) */
    SC_ARSENAL_UPG_CHAINGUN_TURRET_MOVE      = 1u << 25, /* Fast Hands / Movement (6 WUP) */
    SC_ARSENAL_UPG_CHAINGUN_SHIELD_RECHARGE  = 1u << 26, /* Fast Charge (3 WUP) */
    SC_ARSENAL_UPG_CHAINGUN_SHIELD_SMASH     = 1u << 27  /* Shield Launch / Dash Smash (6 WUP) */
};
#define SC_ARSENAL_ALL_NORMAL_UPGRADES 0x0FFFFFFFu

/* 12 Weapon Mod Masteries + SSG Meat Hook Mastery (13 masteries total) */
enum {
    SC_ARSENAL_MASTERY_SHOTGUN_STICKY     = 1u << 0,  /* 7770070 Five-Cluster */
    SC_ARSENAL_MASTERY_SHOTGUN_AUTO       = 1u << 1,  /* 7770069 Salvo Extender */
    SC_ARSENAL_MASTERY_HEAVY_BOLT         = 1u << 2,  /* 7770067 Headshot Blast */
    SC_ARSENAL_MASTERY_HEAVY_BURST        = 1u << 3,  /* 7770068 Bottomless Missiles */
    SC_ARSENAL_MASTERY_PLASMA_AOE         = 1u << 4,  /* 7770063 Power Surge */
    SC_ARSENAL_MASTERY_PLASMA_MICRO       = 1u << 5,  /* 7770065 Concussive Blast */
    SC_ARSENAL_MASTERY_ROCKET_DET         = 1u << 6,  /* 7770072 Explosive Array */
    SC_ARSENAL_MASTERY_ROCKET_LOCK        = 1u << 7,  /* 7770074 Dual Lock */
    SC_ARSENAL_MASTERY_SSG_MEAT_HOOK      = 1u << 8,  /* 7770084 Meat Hook Mastery */
    SC_ARSENAL_MASTERY_BALLISTA_ARBALEST  = 1u << 9,  /* 7770076 Instant Charge */
    SC_ARSENAL_MASTERY_BALLISTA_DESTROYER = 1u << 10, /* 7770078 Infinitely Slicing */
    SC_ARSENAL_MASTERY_CHAINGUN_TURRET    = 1u << 11, /* 7770082 Ultimate Cooling */
    SC_ARSENAL_MASTERY_CHAINGUN_SHIELD    = 1u << 12  /* 7770080 Shield Shoot */
};
#define SC_ARSENAL_ALL_MASTERIES 0x1FFFu

/* Weapon indexes for mod selection (0..7) */
enum {
    SC_ARSENAL_WINDEX_SHOTGUN = 0,
    SC_ARSENAL_WINDEX_SSG     = 1,
    SC_ARSENAL_WINDEX_HEAVY   = 2,
    SC_ARSENAL_WINDEX_CHAINGUN = 3,
    SC_ARSENAL_WINDEX_PLASMA  = 4,
    SC_ARSENAL_WINDEX_BALLISTA = 5,
    SC_ARSENAL_WINDEX_ROCKET  = 6,
    SC_ARSENAL_WINDEX_COUNT   = 7
};

typedef struct sc_arsenal_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t mods;
    uint32_t upgrades;
    uint32_t masteries;
    uint8_t select_weapon;
    uint8_t select_mod;
    uint16_t challenge_index;
    uint32_t challenge_progress;
    uint8_t challenge_completed;
    uint8_t reserved[3];
} sc_arsenal_request;

enum {
    SC_ARSENAL_OUTCOME_OK = 0,
    SC_ARSENAL_OUTCOME_NOOP = 1,
    SC_ARSENAL_OUTCOME_DEFERRED = 2,
    SC_ARSENAL_OUTCOME_REJECTED = 3,
    SC_ARSENAL_OUTCOME_UNAVAILABLE = 4,
    SC_ARSENAL_OUTCOME_CRASH_PROTECTED = 5
};

enum {
    SC_ARSENAL_FLAG_BEFORE_VALID         = 1u << 0,
    SC_ARSENAL_FLAG_AFTER_VALID          = 1u << 1,
    SC_ARSENAL_FLAG_MUTATED              = 1u << 2,
    SC_ARSENAL_FLAG_SELECTION_PRESERVED  = 1u << 3,
    SC_ARSENAL_FLAG_SHARED_STATE_BOUND   = 1u << 4,
    SC_ARSENAL_FLAG_DEFERRED             = 1u << 5
};

typedef struct sc_arsenal_result {
    uint32_t size, abi_version;
    sc_diagnostic_result execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t outcome, flags, native_exception;
    uint32_t weapons_before, weapons_after;
    uint32_t mods_before, mods_after;
    uint8_t selected_mods_before[8], selected_mods_after[8];
    uint32_t normal_upgrades_before, normal_upgrades_after;
    uint16_t masteries_ap_before, masteries_ap_after;
    uint16_t mastery_challenges_active_before, mastery_challenges_active_after;
    uint16_t mastery_challenges_completed_before, mastery_challenges_completed_after;
    uint16_t masteries_effective_before, masteries_effective_after;
    uint32_t mission_challenges_active_before, mission_challenges_active_after;
    uint32_t mission_challenges_completed_before, mission_challenges_completed_after;
    uint64_t operations_applied;
} sc_arsenal_result;

#endif
