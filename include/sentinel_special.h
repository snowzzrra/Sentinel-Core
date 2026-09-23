#ifndef SENTINEL_SPECIAL_H
#define SENTINEL_SPECIAL_H
#include "sentinel_native.h"

/* Sentinel Core native Special Weapon (Crucible / Sentinel Hammer) cumulative
   ownership, selection projection and the typed Ammo Refill use-request flow.
   Ownership is cumulative: later stages never remove earlier special weapons.
   Permanent ownership is independent from current combat resource state. */
#define SC_SPECIAL_ABI_VERSION 1u

enum {
    SC_SPECIAL_OBSERVE = 0,
    SC_SPECIAL_ENSURE_OWNERSHIP = 1,
    SC_SPECIAL_SELECT = 2,
    SC_SPECIAL_REFILL_STATUS = 3,
    SC_SPECIAL_REFILL_PUBLISH = 4,
    SC_SPECIAL_REFILL_TAKE = 5,
    SC_SPECIAL_REFILL_EXECUTE = 6
};

/* Selection / ownership domain values. */
enum {
    SC_SPECIAL_WEAPON_NONE = 0,
    SC_SPECIAL_WEAPON_CRUCIBLE = 1,
    SC_SPECIAL_WEAPON_HAMMER = 2
};

/* Hammer ownership tiers. Tier 2 projects the AP loot effect at the native consumer. */
enum {
    SC_SPECIAL_HAMMER_TIER_NONE = 0,
    SC_SPECIAL_HAMMER_TIER_BASE = 1,
    SC_SPECIAL_HAMMER_TIER_UPGRADED = 2
};

/* native_state_known: observed facts; SELECTION with SELECTION_POLICY identifies
   the applied Core route rather than a vanilla field. Unknown is not absent. */
enum {
    SC_SPECIAL_KNOWN_CRUCIBLE          = 1u << 0,
    SC_SPECIAL_KNOWN_HAMMER            = 1u << 1,
    SC_SPECIAL_KNOWN_HAMMER_PERKS      = 1u << 2,
    SC_SPECIAL_KNOWN_SELECTION         = 1u << 3,
    SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE = 1u << 4
};

/* Ammo Refill presentation flags projected by the authoritative AP owner. */
enum {
    SC_SPECIAL_REFILL_CONNECTED     = 1u << 0,
    SC_SPECIAL_REFILL_AUTHORITATIVE = 1u << 1,
    SC_SPECIAL_REFILL_BALANCE_KNOWN = 1u << 2
};

/* Ammo Refill use-request authorization bit (typed external owner decision). */
enum {
    SC_SPECIAL_REFILL_AUTHORIZED = 1u << 0
};

/* Ammo Refill use-request state. */
enum {
    SC_SPECIAL_REFILL_IDLE = 0,
    SC_SPECIAL_REFILL_PENDING = 1,
    SC_SPECIAL_REFILL_EXECUTED = 2,
    SC_SPECIAL_REFILL_REJECTED = 3,
    SC_SPECIAL_REFILL_EXPIRED = 4
};

typedef struct sc_special_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t own_crucible;      /* desired cumulative ownership */
    uint32_t own_hammer;        /* desired cumulative ownership */
    uint32_t hammer_tier;       /* desired permanent tier (0..2) */
    uint32_t selected;          /* selection intent / projection */
    uint32_t refill_balance;    /* publish: authoritative available charges */
    uint32_t refill_flags;      /* publish: SC_SPECIAL_REFILL_* presentation flags */
    uint64_t refill_request_id; /* take/execute: request identity */
    uint32_t refill_authorize;  /* execute: SC_SPECIAL_REFILL_AUTHORIZED or 0 */
    uint32_t reserved0;
} sc_special_request;

enum {
    SC_SPECIAL_OUTCOME_OK = 0,
    SC_SPECIAL_OUTCOME_NOOP = 1,
    SC_SPECIAL_OUTCOME_REJECTED = 2,
    SC_SPECIAL_OUTCOME_UNAVAILABLE = 3,
    SC_SPECIAL_OUTCOME_NO_PLAYER = 4,
    SC_SPECIAL_OUTCOME_READ_FAILED = 5,
    SC_SPECIAL_OUTCOME_NATIVE_FAILED = 6,
    SC_SPECIAL_OUTCOME_REFRESH_FAILED = 7,
    SC_SPECIAL_OUTCOME_CRASH_PROTECTED = 8
};

enum {
    SC_SPECIAL_FLAG_BEFORE_VALID         = 1u << 0,
    SC_SPECIAL_FLAG_AFTER_VALID          = 1u << 1,
    SC_SPECIAL_FLAG_MUTATED              = 1u << 2,
    SC_SPECIAL_FLAG_SELECTION_PRESERVED  = 1u << 3,
    SC_SPECIAL_FLAG_SHARED_STATE_BOUND   = 1u << 4,
    SC_SPECIAL_FLAG_OWNERSHIP_CUMULATIVE = 1u << 5,
    SC_SPECIAL_FLAG_RESOURCE_PRESERVED   = 1u << 6,
    SC_SPECIAL_FLAG_REFILL_AUTHORIZED    = 1u << 7,
    SC_SPECIAL_FLAG_REFILL_EXECUTED      = 1u << 8,
    SC_SPECIAL_FLAG_REFILL_EXPIRED       = 1u << 9,
    SC_SPECIAL_FLAG_HUD_PRESENTED        = 1u << 10,
    SC_SPECIAL_FLAG_REFILL_UNVERIFIED    = 1u << 11,
    SC_SPECIAL_FLAG_SELECTION_POLICY     = 1u << 12,
    SC_SPECIAL_FLAG_HELD_WEAPON_PRESERVED = 1u << 13,
    SC_SPECIAL_FLAG_HAMMER_LOOT_PROJECTED = 1u << 14,
    SC_SPECIAL_FLAGS_SUPPORTED = 0x7fffu
};

typedef struct sc_special_result {
    uint32_t size, abi_version;
    sc_diagnostic_result execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t outcome, flags, native_exception;
    uint32_t owns_crucible, owns_hammer, hammer_tier, selected;      /* shared logical run state */
    uint32_t native_crucible, native_hammer, native_hammer_perks;   /* native materialization */
    uint32_t native_selected;                                        /* selection policy with POLICY flag; not observed native use */
    uint32_t native_state_known;                                     /* SC_SPECIAL_KNOWN_* */
    uint32_t crucible_charge, crucible_charge_max;                   /* native resource observation */
    uint32_t refill_balance, refill_flags;                           /* AP-authoritative projection */
    uint64_t refill_request_id;                                      /* pending/last request identity */
    uint32_t refill_request_state;
    uint32_t refill_executed;                                        /* native execution confirmed */
    uint64_t refill_execution_id;
    uint32_t refill_sequence;                                        /* monotonic native refill counter */
    uint32_t reserved0;
    uint64_t operations_applied;
} sc_special_result;

#endif
