#ifndef SENTINEL_SAVE_INSTALLATION_H
#define SENTINEL_SAVE_INSTALLATION_H
#include "sentinel_core.h"
#define SC_SAVE_INSTALLATION_ABI_VERSION 1u
#define SC_INSTALL_UNKNOWN UINT32_C(0x80000000)
enum sc_install_stage {
 SC_INSTALL_NONE, SC_INSTALL_PRELAUNCH, SC_INSTALL_PIN, SC_INSTALL_NATIVE_TARGET,
 SC_INSTALL_NATIVE_BINDING, SC_INSTALL_MH_INITIALIZE, SC_INSTALL_NATIVE_CREATE,
 SC_INSTALL_NATIVE_ENABLE, SC_INSTALL_SAVE_BINDING, SC_INSTALL_SAVE_TARGET,
 SC_INSTALL_REFERENCE_COPY, SC_INSTALL_FACTORY_CALL, SC_INSTALL_STEAM_IMPORT,
 SC_INSTALL_SAVE_CREATE, SC_INSTALL_POLICY, SC_INSTALL_SAVE_ENABLE, SC_INSTALL_READY,
 SC_INSTALL_REMOVE, SC_INSTALL_UNINITIALIZE, SC_INSTALL_STARTUP, SC_INSTALL_UPSTREAM
};
/* Fixed facts, not a ring log. Times: GetTickCount64 milliseconds.
 result: 0 pending/not attempted, 1 completed, 2 failed. SC_INSTALL_UNKNOWN marks
 unattempted statuses; zero and MH_UNKNOWN(-1) remain distinct real API results.
 Groups: 0 none, 1 native lifecycle, 2 save target, 3 helper. No process addresses. */
typedef struct sc_install_event {
 uint64_t sequence, at_ms, duration_ms;
 uint32_t stage, target_group, target_index, rva, signature_offset, result, reason;
 uint32_t read_reason, win32_error, minhook_status, byte_count, collision_rva, byte_window_offset;
 uint8_t expected_bytes[32], actual_bytes[32];
} sc_install_event;
typedef struct sc_save_installation_snapshot {
 uint32_t size, abi_version, attempt, phase, last_completed_stage, startup_observation;
 uint32_t validated, created, enabled, cleanup_failures, gaps, reserved;
 uint64_t sequence;
 sc_install_event active, primary_failure, cleanup_failure;
} sc_save_installation_snapshot;
#ifdef __cplusplus
extern "C" {
#endif
SC_API sc_result sc_save_installation_inspect(uint32_t abi, uint32_t size, sc_save_installation_snapshot* snapshot);
#ifdef __cplusplus
}
#endif
#endif
