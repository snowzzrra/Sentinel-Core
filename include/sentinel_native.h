#ifndef SENTINEL_NATIVE_H
#define SENTINEL_NATIVE_H
#include "sentinel_context.h"

#define SC_NATIVE_ABI_VERSION 1u
#define SC_NATIVE_EVENT_PAGE 6u
#define SC_NATIVE_HISTORY 16u
#define SC_DIAGNOSTIC_CAPACITY 8u
#define SC_DIAGNOSTIC_MAX_DEADLINE_MS 5000u
#define SC_DIAGNOSTIC_RETENTION_MS 30000u

// Independent exact-size factual ABI. No gameplay authorization is represented.
typedef enum sc_native_availability {
    SC_NATIVE_PENDING, SC_NATIVE_ENABLED, SC_NATIVE_DISABLED, SC_NATIVE_RETAINED
} sc_native_availability;
typedef enum sc_native_reason {
    SC_NATIVE_NONE, SC_NATIVE_NOT_STARTED, SC_NATIVE_UNKNOWN_BUILD,
    SC_NATIVE_TARGET_BYTES, SC_NATIVE_TARGET_NOT_UNIQUE, SC_NATIVE_TARGET_BOUNDARY,
    SC_NATIVE_BINDING_FAILED, SC_NATIVE_HOOK_FAILED, SC_NATIVE_PIN_FAILED,
    SC_NATIVE_WRONG_THREAD, SC_NATIVE_WRONG_CALLER, SC_NATIVE_REENTRANT,
    SC_NATIVE_EVENT_GAP, SC_NATIVE_UNOBSERVED, SC_NATIVE_TRANSITION,
    SC_NATIVE_CONTEXT_UNAVAILABLE, SC_NATIVE_STALE, SC_NATIVE_SCOPE_MISMATCH,
    SC_NATIVE_QUEUE_FULL, SC_NATIVE_DUPLICATE_MISMATCH, SC_NATIVE_DEADLINE,
    SC_NATIVE_CANCELLED, SC_NATIVE_STOPPED, SC_NATIVE_NOT_FOUND,
    SC_NATIVE_READ_FAILED, SC_NATIVE_BUDGET, SC_NATIVE_EXCEPTION
} sc_native_reason;
typedef enum sc_native_lifecycle {
    SC_LIFETIME_UNOBSERVED, SC_LIFETIME_TRANSITION, SC_LIFETIME_ACTIVE,
    SC_LIFETIME_MENU, SC_LIFETIME_FAILED, SC_LIFETIME_INVALID
} sc_native_lifecycle;
typedef enum sc_native_event_kind {
    SC_EVENT_CHANGE_BEGIN = 1, SC_EVENT_CHANGE_END, SC_EVENT_PRIMARY_FREE_BEGIN,
    SC_EVENT_PRIMARY_FREE_END, SC_EVENT_BACKGROUND_FREE, SC_EVENT_GAP
} sc_native_event_kind;
typedef enum sc_diagnostic_state {
    SC_DIAGNOSTIC_UNKNOWN, SC_DIAGNOSTIC_QUEUED, SC_DIAGNOSTIC_CLAIMED,
    SC_DIAGNOSTIC_EXECUTED, SC_DIAGNOSTIC_REJECTED, SC_DIAGNOSTIC_EXPIRED,
    SC_DIAGNOSTIC_CANCELLED
} sc_diagnostic_state;

typedef struct sc_native_scope {
    uint32_t pid;
    uint64_t process_created;
    uint8_t instance_id[16];
    // Core event-derived generation, NOT an engine native_load_serial.
    uint64_t lifecycle_generation;
} sc_native_scope;
typedef struct sc_native_event {
    uint64_t sequence, generation, at_ms;
    uint32_t kind, lifecycle, thread_id, depth;
} sc_native_event;
typedef struct sc_native_snapshot {
    uint32_t size, abi_version;
    sc_native_scope scope;
    uint32_t availability, reason, site_revision, site_rva;
    uint32_t phase; // 1 = after normal original common-frame return.
    uint32_t installed_hooks, validator_reasons[3], retained_module;
    uint32_t coverage; // bit 0 ExecuteMapChange, 1 primary FreeMapInstance,
                       // bit 2 descriptor isCheckpointReload. Not exhaustive.
    uint32_t lifecycle, depth, checkpoint_flag_known, checkpoint_flag;
    uint64_t event_sequence, event_gap_count, history_oldest, history_overwritten;
    uint64_t callback_sequence, callback_at_ms;
    uint32_t callback_thread_id, native_owner_thread_id;
    uint64_t context_generation, context_sampled_at_ms;
    uint32_t context_reason, game_state;
    sc_context_map current_map;
    uint32_t queued, claimed, retained_results, history_gap, event_count;
    sc_native_event events[SC_NATIVE_EVENT_PAGE];
} sc_native_snapshot;

// The sole operation is a bounded current-context diagnostic. There is no
// address/function/command/script field. Request memory is always copied.
typedef struct sc_diagnostic_request {
    sc_native_scope expected;
    uint64_t request_id;
    uint8_t nonce[16];
    uint32_t deadline_ms; // Relative admission deadline, 1..5000 ms.
} sc_diagnostic_request;
typedef struct sc_diagnostic_result {
    uint32_t state, reason, cancel_requested, retrieved;
    sc_native_scope scope;
    uint64_t request_id;
    uint8_t nonce[16];
    uint64_t admitted_at_ms, deadline_at_ms, claimed_at_ms;
    uint64_t observed_at_ms; // Start of fresh bounded observation, distinct from execution/publication.
    uint64_t executed_at_ms, completed_at_ms, retrieved_at_ms;
    uint32_t thread_id, site_revision, phase, lifecycle, game_state;
    sc_context_map current_map;
} sc_diagnostic_result;

#ifdef __cplusplus
extern "C" {
#endif
SC_API sc_result sc_native_inspect(uint32_t abi_version, uint32_t size,
                                          sc_native_snapshot* snapshot);
#ifdef __cplusplus
}
#endif
#endif
