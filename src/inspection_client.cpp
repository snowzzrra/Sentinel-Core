#include "sentinel_inspection.h"
#include "pipe_io.h"
#include "protocol.h"
#include <cstring>

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
static Inspection query_operation(uint32_t pid, uint32_t timeout_ms, uint64_t required, uint16_t operation,
                                   const sc_diagnostic_request* request = nullptr, uint64_t after_event = 0,
                                   uint64_t write_id = 0) {
    Inspection result;
    Handle process;
    auto fail = [&](DWORD error) {
        result.win32_error = error; result.result = classify(error);
        if (process) {
            const auto state = WaitForSingleObject(process.value, 0);
            result.target_state = state == WAIT_OBJECT_0 ? "exited" : (state == WAIT_TIMEOUT ? "live" : "unknown");
            if (state == WAIT_FAILED) result.target_wait_error = GetLastError();
        }
        // Keep the original API error/classification, even if exit is confirmed.
        // An OpenProcess denial supplies no handle and cannot prove process exit.
        return result;
    };
    if (!pid || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms) {
        result.result = ProbeResult::usage; return result;
    }
    const auto deadline = GetTickCount64() + timeout_ms;
    result.failure_stage = "open_process";
    process.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return fail(GetLastError());
    result.failure_stage = "process_alive";
    if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) return fail(ERROR_FILE_NOT_FOUND);
    uint64_t created = 0;
    result.failure_stage = "process_creation";
    if (!process_time(process.value, created)) return fail(GetLastError());
    result.verified_process_created = created;
    if (request && (request->expected.pid != pid || request->expected.process_created != created)) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    wchar_t path[32768]{}; DWORD path_size = static_cast<DWORD>(std::size(path));
    result.failure_stage = "process_image";
    if (!QueryFullProcessImageNameW(process.value, 0, path, &path_size)) return fail(GetLastError());
    result.host_path.assign(path, path_size);
    Handle pipe;
    const auto name = pipe_name(pid);
    for (;;) {
        result.failure_stage = "pipe_open";
        pipe.value = CreateFileW(name.c_str(), client_access, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe) break;
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY) return fail(error);
        const DWORD wait = remaining(deadline);
        if (!wait) return fail(ERROR_TIMEOUT);
        result.failure_stage = "pipe_wait";
        if (!WaitNamedPipeW(name.c_str(), wait)) return fail(GetLastError());
    }
    ULONG server_pid = 0;
    result.failure_stage = "pipe_server_identity";
    if (!GetNamedPipeServerProcessId(pipe.value, &server_pid)) return fail(GetLastError());
    result.server_pid = server_pid;
    if (server_pid != pid || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    Message data{};
    const DWORD size = static_cast<DWORD>(operation == save_write_operation ? encode_save_write_request(data, write_id) :
        operation >= native_operation && operation <= diagnostic_detail_cancel_operation ?
        encode_native_request(data, operation, request ? *request : sc_diagnostic_request{}, after_event) :
        encode_request(data, required, wire_version, operation));
    DWORD count = 0;
    result.failure_stage = "pipe_write";
    DWORD error = transfer(pipe.value, true, data.data(), size, count, nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return fail(error);
    result.failure_stage = "pipe_read";
    error = transfer(pipe.value, false, data.data(), static_cast<DWORD>(data.size()), count,
                     nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return fail(error);
    WireResult code{};
    result.failure_stage = "decode_response";
    // Old wire-v1 servers reject op 2 with their unchanged op-1 error envelope.
    bool decoded = operation == save_write_operation ? decode_save_write_response(data, count, code, result.snapshot, result.write) :
        operation == save_admission_operation ? decode_save_admission_response(data, count, code, result.snapshot, result.admission) :
        (operation == save_operation ? decode_save_response(data, count, code, result.snapshot, result.save) :
        (operation >= native_operation ? decode_native_response(data, count, code, operation, result.snapshot, result.native, result.diagnostic, &result.detail) :
        (operation == context_operation ? decode_context_response(data, count, code, result.snapshot, result.context) :
        (operation == engine_operation ? decode_engine_response(data, count, code, result.snapshot, result.engine) :
        decode_response(data, count, code, result.snapshot)))));
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
    result.failure_stage = "response_identity";
    if (result.snapshot.pid != pid || result.snapshot.process_created != created ||
        (request && std::memcmp(request->expected.instance_id, result.snapshot.instance.data(), result.snapshot.instance.size())) ||
        WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    if (request && (result.diagnostic.request_id != request->request_id ||
        std::memcmp(result.diagnostic.nonce, request->nonce, sizeof(request->nonce)))) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (operation == save_write_operation && write_id && result.write.operation_id != write_id) {
        result.result = ProbeResult::invalid_response; return result;
    }
    result.result = ProbeResult::ok;
    result.failure_stage = "none"; result.target_state = "live";
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
Inspection query_save(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, save_capability, save_operation);
}
Inspection query_save_admission(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, save_admission_capability, save_admission_operation);
}
Inspection query_save_write(uint32_t pid, uint32_t timeout_ms, uint64_t operation_id) {
    return query_operation(pid, timeout_ms, save_write_capability, save_write_operation, nullptr, 0, operation_id);
}
Inspection query_native(uint32_t pid, uint32_t timeout_ms, uint64_t after_event) {
    return query_operation(pid, timeout_ms, native_capability, native_operation, nullptr, after_event);
}
Inspection query_diagnostic(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_diagnostic_request& request) {
    if (operation < diagnostic_submit_operation || operation > diagnostic_detail_cancel_operation) {
        Inspection result; result.result = ProbeResult::usage; return result;
    }
    return query_operation(pid, timeout_ms, diagnostic_capability, operation, &request);
}
}
