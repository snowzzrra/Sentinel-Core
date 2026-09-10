#include "sentinel_bootstrap.h"
#include <windows.h>

namespace {
SRWLOCK lock = SRWLOCK_INIT;
HANDLE worker = nullptr;
DWORD startup_error = ERROR_SUCCESS;
bool stopped = false;

DWORD WINAPI start_core(void*) {
    return sc_initialize(SC_ABI_VERSION, SC_CAP_INSPECTION | SC_CAP_LIFECYCLE);
}
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // No loads, hooks, core calls, logging or waits under the loader lock.
        // Windows serializes thread entry after DLL initialization. The importing
        // host must retain this DLL until explicit shutdown joins this finite worker.
        worker = CreateThread(nullptr, 0, start_core, nullptr, 0, nullptr);
        if (!worker) startup_error = GetLastError();
    }
    // Native hooks/state, if installed, remain pinned until process exit.
    // Never restore hooks, join or call other DLLs during DLL_PROCESS_DETACH.
    return TRUE;
}

sc_result sc_bootstrap_inspect(uint32_t abi, uint32_t size, sc_boot_status* status) {
    if (abi != SC_ABI_VERSION) return SC_ABI_MISMATCH;
    if (!status || size != sizeof(sc_boot_status)) return SC_INVALID_ARGUMENT;
    AcquireSRWLockShared(&lock);
    *status = {};
    status->size = sizeof(*status);
    status->win32_error = startup_error;
    DWORD result = STILL_ACTIVE;
    if (stopped) status->state = SC_BOOT_STOPPED;
    else if (!worker) status->state = SC_BOOT_FAILED;
    else if (!GetExitCodeThread(worker, &result)) {
        status->state = SC_BOOT_FAILED;
        status->win32_error = GetLastError();
    } else if (result == STILL_ACTIVE) status->state = SC_BOOT_STARTING;
    else status->state = result == SC_OK ? SC_BOOT_READY : SC_BOOT_FAILED;
    const sc_result inspection = sc_inspect(abi, sizeof(status->core), &status->core);
    ReleaseSRWLockShared(&lock);
    return inspection;
}

sc_result sc_bootstrap_shutdown(void) {
    AcquireSRWLockExclusive(&lock);
    if (worker) {
        const DWORD wait = WaitForSingleObject(worker, 5000);
        if (wait != WAIT_OBJECT_0) {
            startup_error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            ReleaseSRWLockExclusive(&lock);
            return SC_BOOTSTRAP_FAILURE; // Caller must retain the module and retry.
        }
        CloseHandle(worker);
        worker = nullptr;
    }
    const sc_result result = sc_shutdown();
    stopped = result == SC_OK;
    ReleaseSRWLockExclusive(&lock);
    return result;
}
