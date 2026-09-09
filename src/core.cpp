#include "inspection_server.h"
#include "pipe_io.h"
#include <bcrypt.h>

namespace {
SRWLOCK lock = SRWLOCK_INIT;
SRWLOCK lifecycle = SRWLOCK_INIT;
constexpr uint64_t capabilities = SC_CAP_INSPECTION | SC_CAP_LIFECYCLE;
sentinel::Snapshot current{{sizeof(sc_status), SC_ABI_VERSION, capabilities, SC_COLD,
                           SC_OK, 0, 0, SC_VERSION, SC_BUILD_ID}};

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
Snapshot current_snapshot() {
    AcquireSRWLockShared(&lock);
    const auto result = current;
    ReleaseSRWLockShared(&lock);
    return result;
}
void inspection_failed(DWORD error) {
    AcquireSRWLockExclusive(&lock);
    current.service = ServiceState::failed;
    current.service_error = error;
    record(SC_INSPECTION_FAILURE, "[Sentinel Core] inspection service failed; engine unavailable\n");
    ReleaseSRWLockExclusive(&lock);
}
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
    } else if (current.service == sentinel::ServiceState::stopping) {
        result = record(SC_INSPECTION_FAILURE, "[Sentinel Core] initialize: retry shutdown first\n");
    } else {
        if (current.core.state != SC_READY) {
            current.core.state = SC_READY;
            ++current.core.initialization_count;
        }
        if (current.service != sentinel::ServiceState::listening) {
            current.pid = GetCurrentProcessId();
            DWORD error = ERROR_SUCCESS;
            if (!sentinel::process_time(GetCurrentProcess(), current.process_created)) error = GetLastError();
            else if (BCryptGenRandom(nullptr, current.instance.data(), static_cast<ULONG>(current.instance.size()),
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) error = ERROR_GEN_FAILURE;
            if (error == ERROR_SUCCESS) error = sentinel::start_inspection();
            current.service_error = error;
            current.service = error == ERROR_SUCCESS ? sentinel::ServiceState::listening : sentinel::ServiceState::failed;
            result = record(error == ERROR_SUCCESS ? SC_OK : SC_INSPECTION_FAILURE,
                error == ERROR_SUCCESS ? "[Sentinel Core " SC_VERSION " build=" SC_BUILD_ID "] ready: local inspection; engine unavailable, gameplay unprobed\n" :
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
    ReleaseSRWLockExclusive(&lock);
    // Never hold the snapshot lock while joining an admitted reader.
    const DWORD error = sentinel::stop_inspection();
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
        result = current.core.last_result = SC_OK;
    }
    ReleaseSRWLockExclusive(&lock);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
