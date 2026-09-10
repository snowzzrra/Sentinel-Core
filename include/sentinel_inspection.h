#ifndef SENTINEL_INSPECTION_H
#define SENTINEL_INSPECTION_H
#include "sentinel_core.h"
#include "sentinel_engine.h"
#include "sentinel_context.h"
#include "sentinel_native.h"
#include <array>
#include <cstddef>
#include <string>

// Native client value types, NOT wire layouts or a DLL ABI.
namespace sentinel {
constexpr uint16_t wire_version = 1;
constexpr uint16_t inspect_operation = 1;
constexpr uint64_t inspect_capability = 1;
constexpr uint16_t engine_operation = 2;
constexpr uint64_t engine_capability = 2; // Operation 2 only; basic op 1 is unchanged.
constexpr uint16_t context_operation = 3;
constexpr uint64_t context_capability = 4;
constexpr uint16_t native_operation = 4, diagnostic_submit_operation = 5,
    diagnostic_result_operation = 6, diagnostic_cancel_operation = 7;
constexpr uint64_t native_capability = 8, diagnostic_capability = 16;
// Detail revision 1, explicit new operations; old payloads remain byte-for-byte.
constexpr uint16_t diagnostic_detail_submit_operation = 8,
    diagnostic_detail_result_operation = 9, diagnostic_detail_cancel_operation = 10;
constexpr uint64_t diagnostic_detail_capability = 32;
constexpr size_t max_request = 512, max_message = 1024;
constexpr uint32_t min_timeout_ms = 50, max_timeout_ms = 10000;
enum class WireResult : uint32_t { ok, incompatible_protocol, capability_unavailable,
    unsupported_operation, malformed };
enum class ServiceState : uint32_t { stopped, listening, stopping, failed };
struct Snapshot {
    sc_status core{};
    uint32_t pid = 0;
    uint64_t process_created = 0; // Windows FILETIME ticks, never a JSON number.
    std::array<uint8_t, 16> instance{};
    ServiceState service = ServiceState::stopped;
    uint32_t service_error = 0;
};
enum class ProbeResult : int { ok = 0, usage = 2, endpoint_absent = 3,
    access_denied = 4, timeout = 5, process_mismatch = 6,
    incompatible_protocol = 7, capability_unavailable = 8,
    invalid_response = 9, io_error = 10 };
struct Inspection {
    ProbeResult result = ProbeResult::io_error;
    uint32_t win32_error = 0;
    uint32_t server_pid = 0;
    const char* failure_stage = "none"; // Client-side API stage; never from the server.
    const char* target_state = "unverified";
    uint32_t target_wait_error = 0;
    uint64_t verified_process_created = 0;
    std::wstring host_path; // Obtained from the OS, not the reply.
    Snapshot snapshot{};
    sc_engine_snapshot engine{};
    sc_context_snapshot context{};
    sc_native_snapshot native{};
    sc_diagnostic_result diagnostic{};
    sc_diagnostic_detail detail{};
};
Inspection query(uint32_t pid, uint32_t timeout_ms, uint64_t required = inspect_capability);
Inspection query_engine(uint32_t pid, uint32_t timeout_ms);
Inspection query_context(uint32_t pid, uint32_t timeout_ms);
Inspection query_native(uint32_t pid, uint32_t timeout_ms, uint64_t after_event = 0);
Inspection query_diagnostic(uint32_t pid, uint32_t timeout_ms, uint16_t operation,
                            const sc_diagnostic_request& request);
const char* native_reason_name(uint32_t reason);
const char* diagnostic_state_name(uint32_t state);
const char* diagnostic_stage_name(uint32_t stage);
const char* context_field_name(size_t field);
const char* context_reason_name(uint32_t reason);
const char* reason_name(uint32_t reason);
const char* validity_name(uint32_t validity);
const char* field_name(size_t field);
const char* result_name(ProbeResult result);
const char* service_name(ServiceState state);
std::string instance_text(const std::array<uint8_t, 16>& instance);
}
#endif
