#include "startup_log.h"
#include "inspection_server.h"
#include "pipe_io.h"
#include "engine_observer.h"
#include "context_observer.h"
#include "save_observer.h"
#include "save_session.h"
#include "sentinel_save_installation.h"
#include "native_runtime.h"
#include <cstring>
#include <bcrypt.h>

namespace {
SRWLOCK lock = SRWLOCK_INIT;
SRWLOCK lifecycle = SRWLOCK_INIT;
constexpr uint64_t capabilities = SC_CAP_INSPECTION | SC_CAP_LIFECYCLE;
sentinel::Snapshot current{{sizeof(sc_status), SC_ABI_VERSION, capabilities, SC_COLD,
                           SC_OK, 0, 0, SC_VERSION, SC_BUILD_ID}};
sc_engine_snapshot current_engine = sentinel::engine::unavailable(SC_REASON_NOT_SAMPLED);
sc_context_snapshot current_context = sentinel::context::unavailable(SC_REASON_NOT_SAMPLED);
sc_save_snapshot current_save = sentinel::save::unavailable(SC_REASON_NOT_SAMPLED);

sc_result record(sc_result result, const char* message) {
    current.core.last_result = result;
    if (current.core.diagnostic_count < SC_DIAGNOSTIC_LIMIT) {
        ++current.core.diagnostic_count;
        OutputDebugStringA(message);
    }
    return result;
}
}

namespace sentinel {
sc_save_snapshot current_save_snapshot() {
    AcquireSRWLockShared(&lock);
    auto result = current_save;
    result.pid = current.pid; result.process_created = current.process_created;
    std::memcpy(result.instance, current.instance.data(), sizeof(result.instance));
    ReleaseSRWLockShared(&lock);
    return save::freshness(result, GetTickCount64());
}
void publish_save(const sc_save_snapshot& snapshot) {
    AcquireSRWLockExclusive(&lock);
    if (current.core.state == SC_READY && current.service == ServiceState::listening) current_save = snapshot;
    ReleaseSRWLockExclusive(&lock);
}
sc_context_snapshot current_context_snapshot() {
    AcquireSRWLockShared(&lock);
    auto result = current_context;
    result.pid = current.pid; result.process_created = current.process_created;
    std::memcpy(result.instance, current.instance.data(), sizeof(result.instance));
    ReleaseSRWLockShared(&lock);
    return context::freshness(result, GetTickCount64());
}
void publish_context(const sc_context_snapshot& snapshot) {
    AcquireSRWLockExclusive(&lock);
    if (current.core.state == SC_READY && current.service == ServiceState::listening) current_context = snapshot;
    ReleaseSRWLockExclusive(&lock);
}
sc_engine_snapshot current_engine_snapshot() {
    AcquireSRWLockShared(&lock);
    const auto result = current_engine;
    ReleaseSRWLockShared(&lock);
    return engine::freshness(result, GetTickCount64());
}
void publish_engine(const sc_engine_snapshot& snapshot) {
    AcquireSRWLockExclusive(&lock);
    if (current.core.state == SC_READY && current.service == ServiceState::listening)
        current_engine = snapshot;
    ReleaseSRWLockExclusive(&lock);
}
Snapshot current_snapshot() {
    AcquireSRWLockShared(&lock);
    const auto result = current;
    ReleaseSRWLockShared(&lock);
    return result;
}
void inspection_failed(DWORD error) {
    AcquireSRWLockExclusive(&lock);
    current_engine = engine::unavailable(SC_REASON_INTERNAL_ERROR);
    current_context = context::unavailable(SC_REASON_INTERNAL_ERROR);
    current_save = save::unavailable(SC_REASON_INTERNAL_ERROR);
    current.service = ServiceState::failed;
    current.service_error = error;
    record(SC_INSPECTION_FAILURE, "[Sentinel Core] inspection service failed; engine unavailable\n");
    ReleaseSRWLockExclusive(&lock);
}
}

sc_result sc_save_inspect(uint32_t abi, uint32_t size, sc_save_snapshot* snapshot) {
    if (abi != SC_SAVE_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!snapshot || size != sizeof(sc_save_snapshot)) return SC_INVALID_ARGUMENT;
    *snapshot = sentinel::current_save_snapshot();
    return SC_OK;
}

sc_result sc_save_installation_inspect(uint32_t abi, uint32_t size, sc_save_installation_snapshot* snapshot) {
    if (abi != SC_SAVE_INSTALLATION_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!snapshot || size != sizeof(*snapshot)) return SC_INVALID_ARGUMENT;
    *snapshot = sentinel::save::session().installation.inspect(); return SC_OK;
}
sc_result sc_save_admission_inspect(uint32_t abi, uint32_t size, sc_save_admission_snapshot* snapshot) {
    if (abi != SC_SAVE_ADMISSION_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!snapshot || size != sizeof(sc_save_admission_snapshot)) return SC_INVALID_ARGUMENT;
    *snapshot = sentinel::save::session().inspect();
    return SC_OK;
}
sc_result sc_save_write_inspect(uint32_t abi, uint32_t size, uint64_t operation_id, sc_save_write_snapshot* snapshot) {
    if (abi != SC_SAVE_WRITE_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!snapshot || size != sizeof(sc_save_write_snapshot)) return SC_INVALID_ARGUMENT;
    *snapshot = sentinel::save::session().native_writes.snapshot(operation_id);
    return SC_OK;
}
sc_result sc_native_inspect(uint32_t abi, uint32_t size, sc_native_snapshot* snapshot) {
    if (abi != SC_NATIVE_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!snapshot || size != sizeof(sc_native_snapshot)) return SC_INVALID_ARGUMENT;
    *snapshot = sentinel::native::inspect();
    return SC_OK;
}

sc_result sc_context_inspect(uint32_t abi, uint32_t size, sc_context_snapshot* snapshot) {
    if (abi != SC_CONTEXT_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!snapshot || size != sizeof(sc_context_snapshot)) return SC_INVALID_ARGUMENT;
    *snapshot = sentinel::current_context_snapshot();
    return SC_OK;
}

sc_result sc_engine_inspect(uint32_t abi, uint32_t size, sc_engine_snapshot* snapshot) {
    if (abi != SC_ENGINE_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!snapshot || size != sizeof(sc_engine_snapshot)) return SC_INVALID_ARGUMENT;
    *snapshot = sentinel::current_engine_snapshot();
    return SC_OK;
}

sc_result sc_inspect(uint32_t abi, uint32_t size, sc_status* status) {
    if (abi != SC_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!status || size != sizeof(sc_status)) return SC_INVALID_ARGUMENT;
    *status = sentinel::current_snapshot().core;
    return SC_OK;
}

sc_result sc_initialize(uint32_t abi, uint64_t required) {
    AcquireSRWLockExclusive(&lifecycle);
    AcquireSRWLockExclusive(&lock);
    sc_result result = SC_OK;
    if (abi != SC_ABI_VERSION) {
        result = record(SC_ABI_MISMATCH, "[Sentinel Core] initialize: ABI mismatch\n");
    } else if ((required & ~capabilities) != 0) {
        result = record(SC_CAPABILITY_UNAVAILABLE,
            "[Sentinel Core] initialize: required capability unavailable; no game capabilities implemented\n");
    } else if (sentinel::native::retained() && current.service != sentinel::ServiceState::listening) {
        result = record(SC_UNLOAD_RETAINED, "[Sentinel Core] native hooks retained until process exit\n");
    } else if (current.service == sentinel::ServiceState::stopping) {
        result = record(SC_INSPECTION_FAILURE, "[Sentinel Core] initialize: retry shutdown first\n");
    } else {
        if (current.core.state != SC_READY) {
            current.core.state = SC_READY;
            ++current.core.initialization_count;
        }
        if (current.service != sentinel::ServiceState::listening) {
            current_engine = sentinel::engine::unavailable(SC_REASON_NOT_SAMPLED);
            current_context = sentinel::context::unavailable(SC_REASON_NOT_SAMPLED);
            current_save = sentinel::save::unavailable(SC_REASON_NOT_SAMPLED);
            current.pid = GetCurrentProcessId();
            DWORD error = ERROR_SUCCESS;
            if (!sentinel::process_time(GetCurrentProcess(), current.process_created)) error = GetLastError();
            else if (BCryptGenRandom(nullptr, current.instance.data(), static_cast<ULONG>(current.instance.size()),
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) error = ERROR_GEN_FAILURE;
            if (error == ERROR_SUCCESS) {
                sentinel::startup_log::record(current);
                sentinel::native::prepare(current);
                sentinel::startup_log::record(current);
                error = sentinel::start_inspection();
            }
            current.service_error = error;
            current.service = error == ERROR_SUCCESS ? sentinel::ServiceState::listening : sentinel::ServiceState::failed;
            result = record(error == ERROR_SUCCESS ? SC_OK : SC_INSPECTION_FAILURE,
                error == ERROR_SUCCESS ? "[Sentinel Core " SC_VERSION " build=" SC_BUILD_ID "] ready: local inspection; read-only observer pending, gameplay unprobed\n" :
                    "[Sentinel Core] initialized; inspection start failed\n");
        } else current.core.last_result = SC_OK;
    }
    ReleaseSRWLockExclusive(&lock);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}

sc_result sc_shutdown(void) {
    AcquireSRWLockExclusive(&lifecycle);
    AcquireSRWLockExclusive(&lock);
    current.service = sentinel::ServiceState::stopping;
    current_engine = sentinel::engine::unavailable(SC_REASON_STOPPED);
    current_context = sentinel::context::unavailable(SC_REASON_STOPPED);
    current_save = sentinel::save::unavailable(SC_REASON_STOPPED);
    ReleaseSRWLockExclusive(&lock);
    // Never hold the snapshot lock while joining an admitted reader.
    sentinel::native::stop();
    const DWORD error = sentinel::stop_inspection();
    const bool retained = sentinel::native::stop(); // Observer startup is now joined.
    AcquireSRWLockExclusive(&lock);
    current.service_error = error;
    sc_result result;
    if (error != ERROR_SUCCESS) {
        result = record(SC_INSPECTION_FAILURE, "[Sentinel Core] shutdown incomplete; retain module and retry\n");
    } else {
        current.service = sentinel::ServiceState::stopped;
        if (current.core.state != SC_STOPPED) {
            current.core.state = SC_STOPPED;
            record(SC_OK, "[Sentinel Core] stopped\n");
        }
        result = current.core.last_result = retained ? SC_UNLOAD_RETAINED : SC_OK;
    }
    ReleaseSRWLockExclusive(&lock);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
