#ifndef SENTINEL_ENGINE_H
#define SENTINEL_ENGINE_H
#include "sentinel_core.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Independent additive contract. Never enlarge sc_status (C ABI 1). */
#define SC_ENGINE_ABI_VERSION 1u
#define SC_ENGINE_FIELD_COUNT 6u
enum { SC_ENGINE_ROOT, SC_ENGINE_LOADING, SC_ENGINE_IN_GAME,
       SC_ENGINE_MAP_PRESENT, SC_ENGINE_PLAYER_PRESENT, SC_ENGINE_CUTSCENE_ID };
enum { SC_OBSERVATION_UNKNOWN, SC_OBSERVATION_OBSERVED, SC_OBSERVATION_PROVISIONAL };
enum { SC_REASON_NONE, SC_REASON_NOT_SAMPLED, SC_REASON_STOPPED,
       SC_REASON_INVALID_PE, SC_REASON_READ_FAILED, SC_REASON_PARTIAL_READ,
       SC_REASON_OUT_OF_RANGE, SC_REASON_SIGNATURE_MISSING, SC_REASON_SIGNATURE_AMBIGUOUS,
       SC_REASON_PROFILE_UNRECOGNIZED, SC_REASON_PARENT_UNAVAILABLE,
       SC_REASON_PARENT_NULL, SC_REASON_INVALID_VALUE, SC_REASON_TRANSITION,
       SC_REASON_CANCELLED, SC_REASON_BUDGET, SC_REASON_INTERNAL_ERROR, SC_REASON_STALE };
enum { SC_PROFILE_NONE, SC_PROFILE_STEAM_20260818 };
/* value is meaningful ONLY when validity != UNKNOWN; unknown value is canonical 0.
   Presence means the checked pointer slot was non-null, not object lifetime/control.
   cutscene_id is the raw signed integer; no action authorization is derived. */
typedef struct sc_engine_field {
    uint32_t validity;
    uint32_t reason;
    int32_t value;
    uint32_t win32_error;
} sc_engine_field;
typedef struct sc_engine_snapshot {
    uint32_t size;
    uint32_t abi_version;
    uint64_t sequence;
    uint64_t sampled_at_ms; /* GetTickCount64 at completion, not wall time. */
    uint32_t duration_ms;
    uint32_t sample_reason; /* NONE means bounded double-read agreement, not atomicity. */
    uint32_t pe_reason;
    uint32_t machine;
    uint32_t timestamp;
    uint32_t image_size;
    uint32_t entry_rva;
    uint32_t disk_hash_reason;
    char disk_sha256[65]; /* Disk-file evidence, NEVER a live-image hash. Empty on failure. */
    uint32_t profile;
    uint32_t locator_revision;
    uint32_t root_locator_reason;
    uint32_t root_signature_rva;
    uint32_t root_target_rva;
    uint32_t profile_runtime_validated; /* 0: this Core reader still needs game smoke. */
    sc_engine_field fields[SC_ENGINE_FIELD_COUNT];
} sc_engine_snapshot;
/* Outside DllMain. Exact size/version. Copies Core's last completed sample;
   caller checks sampled_at_ms/sequence. No new engine reads on the caller thread. */
SC_API sc_result sc_engine_inspect(uint32_t abi, uint32_t size, sc_engine_snapshot* snapshot);
#ifdef __cplusplus
}
#endif
#endif
