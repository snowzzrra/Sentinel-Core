#ifndef SENTINEL_INSPECTION_H
#define SENTINEL_INSPECTION_H
#include "sentinel_core.h"
#include <array>
#include <cstddef>
#include <string>

// Native client value types, NOT wire layouts or a DLL ABI.
namespace sentinel {
constexpr uint16_t wire_version = 1;
constexpr uint16_t inspect_operation = 1;
constexpr uint64_t inspect_capability = 1;
constexpr size_t max_message = 512;
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
    std::wstring host_path; // Obtained from the OS, not the reply.
    Snapshot snapshot{};
};
Inspection query(uint32_t pid, uint32_t timeout_ms, uint64_t required = inspect_capability);
const char* result_name(ProbeResult result);
const char* service_name(ServiceState state);
std::string instance_text(const std::array<uint8_t, 16>& instance);
}
#endif
