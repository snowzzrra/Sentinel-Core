#ifndef SENTINEL_INVENTORY_H
#define SENTINEL_INVENTORY_H
#include "sentinel_native.h"

/* Independent typed capability for Sentinel Core native shared inventory.
   Retains persistent inventory / run state across Base, TAG1, and TAG2. */
#define SC_INVENTORY_ABI_VERSION 1u
#define SC_INVENTORY_MAX_CAPACITY_TIER 4u

enum {
    SC_INV_OBSERVE = 0,
    SC_INV_ENSURE = 1,
    SC_INV_SET_CAPACITY = 2
};

/* Domain-local bitmask; never cast to the differently ordered Arsenal mask. */
enum {
    SC_INV_WEAPON_SHOTGUN         = 1u << 0,  /* 7770900 Combat Shotgun */
    SC_INV_WEAPON_SUPER_SHOTGUN   = 1u << 1,  /* 7770003 Super Shotgun */
    SC_INV_WEAPON_HEAVY_CANNON    = 1u << 2,  /* 7770000 Heavy Cannon */
    SC_INV_WEAPON_CHAINGUN        = 1u << 3,  /* 7770005 Chaingun */
    SC_INV_WEAPON_PLASMA_RIFLE    = 1u << 4,  /* 7770001 Plasma Rifle */
    SC_INV_WEAPON_BALLISTA        = 1u << 5,  /* 7770004 Ballista */
    SC_INV_WEAPON_ROCKET_LAUNCHER = 1u << 6,  /* 7770002 Rocket Launcher */
    SC_INV_WEAPON_BFG             = 1u << 7,  /* 7770006 BFG 9000 */
    SC_INV_WEAPON_CHAINSAW        = 1u << 8,  /* 7770010 Chainsaw */
    SC_INV_WEAPON_UNMAYKR         = 1u << 9   /* 7770008 Unmaykr */
};
#define SC_INV_ALL_WEAPONS 0x3FFu

/* Equipment bitmask */
enum {
    SC_INV_EQUIP_LAUNCHER         = 1u << 0,  /* 7770011 Frag Grenade */
    SC_INV_EQUIP_FLAME_BELCH      = 1u << 1,  /* 7770012 Flame Belch */
    SC_INV_EQUIP_BLOOD_PUNCH      = 1u << 2,  /* 7770014 Blood Punch */
    SC_INV_EQUIP_DASH             = 1u << 3,  /* 7770015 Dash */
    SC_INV_EQUIP_ICE_BOMB         = 1u << 4   /* 7770013 Ice Bomb */
};
#define SC_INV_ALL_EQUIPMENT 0x1Fu

/* Special weapons bitmask */
enum {
    SC_INV_SPECIAL_CRUCIBLE       = 1u << 0,  /* 7770007 / 7770901 Crucible */
    SC_INV_SPECIAL_HAMMER         = 1u << 1   /* 7770009 / 7770902 Sentinel Hammer */
};
#define SC_INV_ALL_SPECIAL 0x3u

/* Persistent upgrades bitmask (Support Runes and Slayer Gate Keys) */
enum {
    SC_INV_UPGRADE_RUNE_PUNCH     = 1u << 0,  /* 7770146 Desperate Punch */
    SC_INV_UPGRADE_RUNE_TAKEBACK  = 1u << 1,  /* 7770147 Take Back */
    SC_INV_UPGRADE_RUNE_BLAST     = 1u << 2,  /* 7770145 Break Blast */
    SC_INV_UPGRADE_KEY_EXULTIA    = 1u << 3,  /* 7770150 Slayer Key Exultia */
    SC_INV_UPGRADE_KEY_CULTIST    = 1u << 4,  /* 7770151 Slayer Key Cultist Base */
    SC_INV_UPGRADE_KEY_SGN        = 1u << 5,  /* 7770152 Slayer Key Super Gore Nest */
    SC_INV_UPGRADE_KEY_ARC        = 1u << 6,  /* 7770153 Slayer Key ARC Complex */
    SC_INV_UPGRADE_KEY_PHOBOS     = 1u << 7,  /* 7770154 Slayer Key Phobos / Mars Core */
    SC_INV_UPGRADE_KEY_TARAS      = 1u << 8,  /* 7770155 Slayer Key Taras Nabad */
    SC_INV_UPGRADE_KEY_ATLANTICA  = 1u << 9,  /* 7770148 Slayer Key UAC Atlantica */
    SC_INV_UPGRADE_KEY_HOLT       = 1u << 10  /* 7770149 Slayer Key The Holt */
};
#define SC_INV_ALL_UPGRADES 0x7FFu

typedef struct sc_inventory_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t weapons;
    uint32_t equipment;
    uint32_t special_weapons;
    uint32_t persistent_upgrades;
    uint8_t health_tier;
    uint8_t armor_tier;
    uint8_t ammo_tier;
    uint8_t reserved;
} sc_inventory_request;

enum {
    SC_INV_NOT_EXECUTED = 0,
    SC_INV_OBSERVED = 1,
    SC_INV_MUTATED = 2,
    SC_INV_PRECONDITION = 3,
    SC_INV_NO_PLAYER = 4,
    SC_INV_READ_FAILED = 5,
    SC_INV_NATIVE_FAILED = 6,
    SC_INV_REFRESH_FAILED = 7,
    SC_INV_UNAVAILABLE = 8
};

enum {
    SC_INV_BEFORE_VALID = 1,
    SC_INV_AFTER_VALID = 2,
    SC_INV_NATIVE_ENTERED = 4,
    SC_INV_REFRESHED = 8,
    SC_INV_SHARED_STATE_BOUND = 16
};

typedef struct sc_inventory_result {
    uint32_t size, abi_version;
    sc_diagnostic_result execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t outcome, flags, native_exception;
    uint32_t weapons_before, weapons_after;
    uint32_t equipment_before, equipment_after;
    uint32_t special_before, special_after;
    uint32_t upgrades_before, upgrades_after;
    uint8_t health_tier_before, health_tier_after;
    uint8_t armor_tier_before, armor_tier_after;
    uint8_t ammo_tier_before, ammo_tier_after;
    uint8_t reserved_before, reserved_after;
    uint64_t operations_applied;
} sc_inventory_result;

#endif
