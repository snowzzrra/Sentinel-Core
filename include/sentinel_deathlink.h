#ifndef SENTINEL_DEATHLINK_H
#define SENTINEL_DEATHLINK_H
#include "sentinel_native.h"

/* Sentinel Core native DeathLink domain.
   Soft applies exactly one legitimate lethal event through Doom's normal
   damage/death pipeline and accepts either true death or native protection.
   Hardcore keeps one logical remote event pending across native protection
   cycles and resolves only on observed true death. */
#define SC_DEATHLINK_ABI_VERSION 1u

enum {
    SC_DEATHLINK_OBSERVE = 0,
    SC_DEATHLINK_CONFIGURE = 1,
    SC_DEATHLINK_APPLY_REMOTE = 2,
    SC_DEATHLINK_POLL_LOCAL = 3,
    SC_DEATHLINK_ACK_LOCAL = 4,
    SC_DEATHLINK_CANCEL_REMOTE = 5
};

enum {
    SC_DEATHLINK_MODE_SOFT = 0,
    SC_DEATHLINK_MODE_HARDCORE = 1
};

/* Remote logical event lifecycle. */
enum {
    SC_DEATHLINK_REMOTE_IDLE = 0,
    SC_DEATHLINK_REMOTE_RECEIVED = 1,
    SC_DEATHLINK_REMOTE_WAITING_SAFE = 2,
    SC_DEATHLINK_REMOTE_APPLYING = 3,
    SC_DEATHLINK_REMOTE_APPLIED = 4,
    SC_DEATHLINK_REMOTE_WAITING_PROTECTION_END = 5,
    SC_DEATHLINK_REMOTE_RESOLVED_DEATH = 6,
    SC_DEATHLINK_REMOTE_RESOLVED_PROTECTED = 7,
    SC_DEATHLINK_REMOTE_FAILED = 8,
    SC_DEATHLINK_REMOTE_EXPIRED = 9,
    SC_DEATHLINK_REMOTE_CANCELLED = 10
};

/* Positive protection evidence for the last application. Survival alone is
   a native failure, not proof of protection. OTHER remains reserved. */
enum {
    SC_DEATHLINK_PROTECTION_NONE = 0,
    SC_DEATHLINK_PROTECTION_EXTRA_LIFE = 1,
    SC_DEATHLINK_PROTECTION_SAVING_THROW = 2,
    SC_DEATHLINK_PROTECTION_OTHER = 3
};

/* Local outbound event origin. Remote-caused deaths are recorded but never
   published as outbound candidates. */
enum {
    SC_DEATHLINK_CAUSE_LOCAL = 0,
    SC_DEATHLINK_CAUSE_REMOTE = 1
};

enum {
    SC_DEATHLINK_LOCAL_EMPTY = 0,
    SC_DEATHLINK_LOCAL_AVAILABLE = 1
};

typedef struct sc_deathlink_request {
    sc_diagnostic_request execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t enabled;
    uint32_t mode;
    uint32_t flags;
    uint64_t event_id;
    uint8_t event_hash[16];
    uint64_t ack_sequence;
    uint32_t reserved0;
} sc_deathlink_request;

enum {
    SC_DEATHLINK_OUTCOME_OK = 0,
    SC_DEATHLINK_OUTCOME_NOOP = 1,
    SC_DEATHLINK_OUTCOME_REJECTED = 2,
    SC_DEATHLINK_OUTCOME_UNAVAILABLE = 3,
    SC_DEATHLINK_OUTCOME_NO_PLAYER = 4,
    SC_DEATHLINK_OUTCOME_UNSAFE = 5,
    SC_DEATHLINK_OUTCOME_NATIVE_FAILED = 6,
    SC_DEATHLINK_OUTCOME_DUPLICATE = 7,
    SC_DEATHLINK_OUTCOME_QUEUE_FULL = 8,
    SC_DEATHLINK_OUTCOME_EXPIRED = 9,
    SC_DEATHLINK_OUTCOME_CANCELLED = 10,
    SC_DEATHLINK_OUTCOME_DISABLED = 11
};

enum {
    SC_DEATHLINK_FLAG_BEFORE_VALID       = 1u << 0,
    SC_DEATHLINK_FLAG_AFTER_VALID        = 1u << 1,
    SC_DEATHLINK_FLAG_MUTATED            = 1u << 2,
    SC_DEATHLINK_FLAG_SHARED_STATE_BOUND = 1u << 3,
    SC_DEATHLINK_FLAG_ENABLED            = 1u << 4,
    SC_DEATHLINK_FLAG_HARDCORE           = 1u << 5,
    SC_DEATHLINK_FLAG_APPLIED            = 1u << 6,
    SC_DEATHLINK_FLAG_TRUE_DEATH         = 1u << 7,
    SC_DEATHLINK_FLAG_PROTECTED          = 1u << 8,
    SC_DEATHLINK_FLAG_SUPPRESSED         = 1u << 9,
    SC_DEATHLINK_FLAG_LOCAL_EVENT        = 1u << 10,
    SC_DEATHLINK_FLAG_ADVANCED           = 1u << 11,
    SC_DEATHLINK_FLAG_LOCAL_LOSS         = 1u << 12
};

typedef struct sc_deathlink_result {
    uint32_t size, abi_version;
    sc_diagnostic_result execution;
    char namespace_id[65];
    uint32_t kind;
    uint32_t outcome, flags, native_exception;
    uint32_t enabled, mode;
    uint32_t remote_state, remote_protection, remote_attempts, pending_count;
    uint64_t remote_event_id;
    uint32_t local_candidates, local_suppressed;
    uint32_t local_state;
    uint64_t local_death_sequence;
    uint64_t local_death_time_ms;
    uint32_t local_cause, local_protection, local_flags;
    uint32_t reserved0;
    uint64_t operations_applied;
} sc_deathlink_result;

#endif
