#ifndef SENTINEL_SAVE_H
#define SENTINEL_SAVE_H
#include "sentinel_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive, exact-size C ABI. Observation only; no save/mount/restore call exists. */
#define SC_SAVE_ABI_VERSION 1u
#define SC_SAVE_FIELD_COUNT 12u
enum { SC_SAVE_ROOT, SC_SAVE_MANAGER, SC_SAVE_PROVIDER, SC_SAVE_QUEUED_REQUESTS,
       SC_SAVE_PENDING_MAP_LOAD, SC_SAVE_JOB_WITNESS, SC_SAVE_REQUEST58_WITNESS,
       SC_SAVE_REQUEST50_WITNESS, SC_SAVE_SELECTED_SLOT, SC_SAVE_NAMESPACE_ROUTE,
       SC_SAVE_NATIVE_OPERATION_ID, SC_SAVE_NATIVE_COMPLETION };
enum { SC_SAVE_PROVIDER_UNKNOWN, SC_SAVE_PROVIDER_STEAM, SC_SAVE_PROVIDER_LOCAL_ENCRYPTED,
       SC_SAVE_PROVIDER_LOCAL, SC_SAVE_PROVIDER_FOREIGN };
/* Engine reason values retain their meanings. These extension reasons are disjoint. */
enum { SC_SAVE_UNSUPPORTED = 64, SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN,
       SC_SAVE_SELECTED_SLOT_UNPROVEN, SC_SAVE_COMPLETION_UNPROVEN };
typedef struct sc_save_field {
    uint32_t validity;
    uint32_t reason;
    uint64_t value; /* Meaningful only when OBSERVED; UNKNOWN is canonical zero. */
    uint32_t win32_error;
    uint32_t reserved; /* Zero in ABI 1. */
} sc_save_field;
typedef struct sc_save_snapshot {
    uint32_t size;
    uint32_t abi_version;
    uint32_t pid;
    uint32_t profile;
    uint64_t process_created;
    uint8_t instance[16];
    uint64_t sequence;
    uint64_t sampled_at_ms; /* GetTickCount64 completion epoch, not wall time. */
    uint32_t duration_ms;
    uint32_t sample_reason;
    uint32_t layout_revision;
    uint32_t root_locator_reason;
    uint32_t mutation_available; /* Always zero: native namespace admission is unproven. */
    uint32_t mutation_reason;
    sc_save_field fields[SC_SAVE_FIELD_COUNT];
} sc_save_snapshot;
/* Bounded double-read agreement is not atomicity or ownership. Root/manager/job
   values are pointer-presence witnesses, never addresses. Provider is a vtable
   classification, not proof of selected slot or destination. Queued requests may
   decrease on timeout. Zero requests/jobs NEVER proves idle, completion or disk
   persistence. Selected slot, namespace route, operation ID and completion remain
   explicitly unknown. Native mutation requires separately established routing,
   lifetime/owner-thread admission, completion and persistence boundaries.
   Copies the last sampler result; performs no reads on the caller thread. */
SC_API sc_result sc_save_inspect(uint32_t abi, uint32_t size, sc_save_snapshot* snapshot);
/* Independent admission ABI. A read performs no filesystem/provider work and
   cannot authorize a session. Process identity remains in the IPC envelope. */
#define SC_SAVE_ADMISSION_ABI_VERSION 1u
enum { SC_SAVE_SESSION_DISABLED, SC_SAVE_SESSION_PREPARED, SC_SAVE_SESSION_STARTING,
       SC_SAVE_SESSION_ADMITTED, SC_SAVE_SESSION_REJECTED, SC_SAVE_SESSION_FAULTED,
       SC_SAVE_SESSION_BINDING }; /* Routing retained; PROFILE/startup admission still pending. */
enum { SC_SAVE_SESSION_ROUTED = 1, SC_SAVE_SESSION_ACCEPTING = 2,
       SC_SAVE_SESSION_STARTUP_QUALIFIED = 4 };
typedef struct sc_save_admission_snapshot {
    uint32_t size, abi_version, state, fault;
    uint32_t prepared_routes, required_routes, flags, reserved;
    char namespace_id[65];
    char native_root[44];
    uint8_t reserved_bytes[19];
} sc_save_admission_snapshot;
SC_API sc_result sc_save_admission_inspect(uint32_t abi, uint32_t size, sc_save_admission_snapshot* snapshot);
/* Correlated write evidence, independent of the older observation/admission ABIs.
   Reads only Core-owned records; never polls a native task or SDK handle. ID 0
   selects the newest retained operation. IDs are scoped to the process instance.
   SDK_CONFIRMED requires the provider result, SDK result, all successful callbacks
   and every submitted payload matching its prepared hash. It does NOT establish
   storage readback, cloud synchronization, playable reopen or global quiescence.
   READBACK_CONFIRMED additionally requires a private native Load of every exact
   transport payload and matching hashes. It does not establish playable reopen.
   Completed records are bounded; NOT_RETAINED does not mean never executed. */
#define SC_SAVE_WRITE_ABI_VERSION 1u
enum { SC_SAVE_WRITE_NONE, SC_SAVE_WRITE_PENDING, SC_SAVE_WRITE_NATIVE_FAILED,
       SC_SAVE_WRITE_NATIVE_SUCCEEDED, SC_SAVE_WRITE_SDK_CONFIRMED,
       SC_SAVE_WRITE_INDETERMINATE, SC_SAVE_WRITE_NOT_RETAINED,
       SC_SAVE_WRITE_READBACK_PENDING, SC_SAVE_WRITE_READBACK_CONFIRMED, SC_SAVE_WRITE_READBACK_FAILED };
enum { SC_SAVE_WRITE_PROVIDER_ALIVE = 1, SC_SAVE_WRITE_PROVIDER_TERMINAL = 2,
       SC_SAVE_WRITE_SOURCE_VALID = 4, SC_SAVE_WRITE_VECTOR_RELEASED = 8,
       SC_SAVE_WRITE_PAYLOADS_PREPARED = 16, SC_SAVE_WRITE_SDK_SUCCEEDED = 32,
       SC_SAVE_WRITE_PAYLOADS_CAPTURED = 64, SC_SAVE_WRITE_CALLBACKS_SUCCEEDED = 128,
       SC_SAVE_WRITE_UNPROVEN = 256, SC_SAVE_WRITE_TRACKING_LOST = 512,
       SC_SAVE_WRITE_READBACK_REQUIRED = 1024, SC_SAVE_WRITE_READBACK_ACTIVE = 2048,
       SC_SAVE_WRITE_READBACK_HASHES = 4096, SC_SAVE_WRITE_READBACK_TERMINAL = 8192,
       SC_SAVE_WRITE_READBACK_ERROR = 16384 };
typedef struct sc_save_write_snapshot {
    uint32_t size, abi_version, state, flags;
    uint64_t operation_id, sdk_sequence;
    uint32_t file_count, submitted, completed, pending_handles;
    uint32_t preparation_jobs, preflight_jobs, native_value, reserved;
    int64_t native_state, native_outcome; /* Canonical zero without PROVIDER_TERMINAL. */
    char directory[64];
    uint8_t reserved_bytes[16];
} sc_save_write_snapshot;
SC_API sc_result sc_save_write_inspect(uint32_t abi, uint32_t size, uint64_t operation_id,
    sc_save_write_snapshot* snapshot);
#ifdef __cplusplus
}
#endif
#endif
