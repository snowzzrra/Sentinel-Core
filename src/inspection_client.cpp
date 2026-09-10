#include "sentinel_inspection.h"
#include "pipe_io.h"
#include "protocol.h"

namespace sentinel {
namespace {
ProbeResult classify(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_PARAMETER:
        return ProbeResult::endpoint_absent;
    case ERROR_ACCESS_DENIED: return ProbeResult::access_denied;
    case ERROR_TIMEOUT: case ERROR_SEM_TIMEOUT: return ProbeResult::timeout;
    case ERROR_MORE_DATA: return ProbeResult::invalid_response;
    default: return ProbeResult::io_error;
    }
}
}
static Inspection query_operation(uint32_t pid, uint32_t timeout_ms, uint64_t required, uint16_t operation) {
    Inspection result;
    auto fail = [&](DWORD error) { result.win32_error = error; result.result = classify(error); return result; };
    if (!pid || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms) {
        result.result = ProbeResult::usage; return result;
    }
    const auto deadline = GetTickCount64() + timeout_ms;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!process) return fail(GetLastError());
    if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) return fail(ERROR_FILE_NOT_FOUND);
    uint64_t created = 0;
    if (!process_time(process.value, created)) return fail(GetLastError());
    wchar_t path[32768]{}; DWORD path_size = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process.value, 0, path, &path_size)) return fail(GetLastError());
    result.host_path.assign(path, path_size);
    Handle pipe;
    const auto name = pipe_name(pid);
    for (;;) {
        pipe.value = CreateFileW(name.c_str(), client_access, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe) break;
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY) return fail(error);
        const DWORD wait = remaining(deadline);
        if (!wait) return fail(ERROR_TIMEOUT);
        if (!WaitNamedPipeW(name.c_str(), wait)) return fail(GetLastError());
    }
    ULONG server_pid = 0;
    if (!GetNamedPipeServerProcessId(pipe.value, &server_pid)) return fail(GetLastError());
    result.server_pid = server_pid;
    if (server_pid != pid || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    Message data{};
    const DWORD size = static_cast<DWORD>(encode_request(data, required, wire_version, operation));
    DWORD count = 0;
    DWORD error = transfer(pipe.value, true, data.data(), size, count, nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return fail(error);
    error = transfer(pipe.value, false, data.data(), static_cast<DWORD>(data.size()), count,
                     nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return fail(error);
    WireResult code{};
    // Old wire-v1 servers reject op 2 with their unchanged op-1 error envelope.
    bool decoded = operation == context_operation ? decode_context_response(data, count, code, result.snapshot, result.context) :
        (operation == engine_operation ? decode_engine_response(data, count, code, result.snapshot, result.engine) :
        decode_response(data, count, code, result.snapshot));
    if (!decoded && operation != inspect_operation && count == header_size)
        decoded = decode_response(data, count, code, result.snapshot) && code != WireResult::ok;
    if (!decoded) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (code != WireResult::ok) {
        result.result = code == WireResult::capability_unavailable || code == WireResult::unsupported_operation ? ProbeResult::capability_unavailable :
            (code == WireResult::incompatible_protocol ? ProbeResult::incompatible_protocol : ProbeResult::invalid_response);
        return result;
    }
    if (result.snapshot.pid != pid || result.snapshot.process_created != created ||
        WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    result.result = ProbeResult::ok;
    return result;
}
Inspection query(uint32_t pid, uint32_t timeout_ms, uint64_t required) {
    return query_operation(pid, timeout_ms, required, inspect_operation);
}
Inspection query_engine(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, engine_capability, engine_operation);
}
Inspection query_context(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, context_capability, context_operation);
}
}
