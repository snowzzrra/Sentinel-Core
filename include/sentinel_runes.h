#ifndef SENTINEL_RUNES_H
#define SENTINEL_RUNES_H
#include "sentinel_native.h"

/* Sentinel Core Native Runes, Support Runes, and Sentinel Crystal Pair Effects.
   Guarantees canonical native registration distinct from perk grants.
   Guarantees independent 3-slot player selection: receiving a Rune never auto-equips.
   Guarantees deterministic crystal pair effects derived from progressive capacity tiers. */
#define SC_RUNES_ABI_VERSION 1u

enum {
    SC_RUNES_OBSERVE = 0,
    SC_RUNES_ENSURE_NORMAL = 1,
    SC_RUNES_ENSURE_SUPPORT = 2,
    SC_RUNES_SELECT_NORMAL = 3,
    SC_RUNES_SELECT_SUPPORT = 4
};

/* 9 Normal Runes (indices 0..8) */
enum {
    SC_RUNE_SAVAGERY         = 1u << 0,  /* 7770085 perk/player/runes/glory_kill_speed */
    SC_RUNE_SEEK_AND_DESTROY = 1u << 1,  /* 7770086 perk/player/runes/glory_kill_dash */
    SC_RUNE_BLOOD_FUELED     = 1u << 2,  /* 7770087 perk/player/runes/speed_boost_on_glory_kill */
    SC_RUNE_AIR_CONTROL      = 1u << 3,  /* 7770089 perk/player/runes/double_jump_air_control */
    SC_RUNE_DAZED_CONFUSED   = 1u << 4,  /* 7770090 perk/player/runes/modify_enemy_stagger_duration */
    SC_RUNE_SAVING_THROW     = 1u << 5,  /* 7770091 perk/player/runes/activate_focus_on_death_blow */
    SC_RUNE_CHRONO_STRIKE    = 1u << 6,  /* 7770093 perk/player/runes/target_strike */
    SC_RUNE_EQUIPMENT_FIEND  = 1u << 7,  /* 7770094 perk/player/runes/decrease_equipment_recharge */
    SC_RUNE_PUNCH_AND_REAVE  = 1u << 8   /* 7770095 perk/player/runes/blood_punch_loot_on_damage */
};
#define SC_RUNES_ALL_NORMAL 0x1FFu

/* 3 Support Runes (indices 0..2) */
enum {
    SC_SUPPORT_RUNE_BREAK_BLAST     = 1u << 0,  /* 7770145 perk/player/runes/dlc/weakpoint_concussive_blast */
    SC_SUPPORT_RUNE_DESPERATE_PUNCH = 1u << 1,  /* 7770146 perk/player/runes/dlc/blood_punch_low_health_bonus_damage */
    SC_SUPPORT_RUNE_TAKE_BACK       = 1u << 2   /* 7770147 perk/player/runes/dlc/extra_life_refund */
};
#define SC_RUNES_ALL_SUPPORT 0x7u

/* 6 Sentinel Crystal Pair Effects (derived from Health, Armor, Ammo capacity tiers) */
enum {
    SC_CRYSTAL_PAIR_QUICKDRAW_BELCH   = 1u << 0,  /* Health >= 1 && Armor >= 1 -> perk/player/equipment/flame_reduce_cooldown */
    SC_CRYSTAL_PAIR_LOOT_MAGNET       = 1u << 1,  /* Armor >= 2 && Ammo >= 3  -> perk/player/suit/fundamentals/increase_pickup_radius */
    SC_CRYSTAL_PAIR_NAPALM_BELCH      = 1u << 2,  /* Health >= 2 && Ammo >= 1 -> perk/player/equipment/flame_extend_duration */
    SC_CRYSTAL_PAIR_HEALTH_FOR_BLOOD  = 1u << 3,  /* Health >= 4 && Armor >= 3 -> perk/player/suit/self_preservation/overhealth */
    SC_CRYSTAL_PAIR_BELCH_ARMOR_BOOST = 1u << 4,  /* Health >= 3 && Ammo >= 2 -> perk/player/equipment/flame_more_loot */
    SC_CRYSTAL_PAIR_ARMOR_FOR_BLOOD   = 1u << 5   /* Armor >= 4 && Ammo >= 4  -> perk/player/suit/self_preservation/overarmor */
};
#define SC_CRYSTAL_ALL_PAIRS 0x3Fu

typedef struct sc_runes_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t normal_runes;
    uint32_t support_runes;
    uint8_t select_slot;      /* 0..2 for normal slots, 3 for support slot */
    int8_t select_rune;       /* -1 for empty/unequip, or rune index 0..8 / support index 0..2 */
    uint8_t reserved[2];
} sc_runes_request;

enum {
    SC_RUNES_OUTCOME_OK = 0,
    SC_RUNES_OUTCOME_NOOP = 1,
    SC_RUNES_OUTCOME_REJECTED = 2,
    SC_RUNES_OUTCOME_NO_PLAYER = 3,
    SC_RUNES_OUTCOME_UNAVAILABLE = 4,
    SC_RUNES_OUTCOME_READ_FAILED = 5,
    SC_RUNES_OUTCOME_NATIVE_FAILED = 6,
    SC_RUNES_OUTCOME_REFRESH_FAILED = 7
};

enum {
    SC_RUNES_FLAG_BEFORE_VALID        = 1u << 0,
    SC_RUNES_FLAG_AFTER_VALID         = 1u << 1,
    SC_RUNES_FLAG_MUTATED             = 1u << 2,
    SC_RUNES_FLAG_SELECTION_PRESERVED = 1u << 3,
    SC_RUNES_FLAG_SHARED_STATE_BOUND  = 1u << 4
};

typedef struct sc_runes_result {
    uint32_t size, abi_version;
    sc_diagnostic_result execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t outcome, flags, native_exception;
    uint32_t owned_normal_before, owned_normal_after;
    uint32_t owned_support_before, owned_support_after;
    int8_t selected_slots_before[3], selected_slots_after[3];
    int8_t selected_support_before, selected_support_after;
    uint8_t unlocked_slots_before, unlocked_slots_after;
    uint8_t derived_pairs_before, derived_pairs_after;
    uint8_t reserved_before, reserved_after;
    uint64_t operations_applied;
} sc_runes_result;

#endif
