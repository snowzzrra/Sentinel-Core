#include "startup_log.h"
#include "inspection_server.h"
#include "pipe_io.h"
#include "protocol.h"
#include "engine_observer.h"
#include "context_observer.h"
#include "save_observer.h"
#include "save_session.h"
#include "native_runtime.h"
#include <sddl.h>
#include <vector>

namespace sentinel {
namespace {
HANDLE pipe = INVALID_HANDLE_VALUE, stop = nullptr, worker = nullptr, observer = nullptr;

DWORD observe(void*) {
    engine::LocalMemory memory;
    engine::Binding binding;
    uint64_t sequence = 0;
    try {
        if (WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) return 0;
        binding = engine::bind_host(memory, stop);
        native::start(binding, current_snapshot(), stop);
        startup_log::record(current_snapshot(), binding.metadata.sample_reason);
        do {
            publish_engine(engine::sample(memory, binding, ++sequence));
            const auto before = native::observation_stamp();
            const auto observed = context::sample(memory, binding, sequence, stop);
            native::publish_context(observed, before);
            publish_context(observed);
            publish_save(save::sample(memory, binding, sequence, stop));
            startup_log::record(current_snapshot(), binding.metadata.sample_reason);
        } while (WaitForSingleObject(stop, 100) == WAIT_TIMEOUT);
    } catch (...) {
        binding.metadata = engine::unavailable(SC_REASON_INTERNAL_ERROR);
        publish_engine(binding.metadata);
        publish_context(context::unavailable(SC_REASON_INTERNAL_ERROR));
        publish_save(save::unavailable(SC_REASON_INTERNAL_ERROR));
    }
    return 0;
}
DWORD serve(void*) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) { inspection_failed(GetLastError()); return 1; }
    // One sampler owns all engine reads. Resolver startup never blocks basic IPC.
    observer = CreateThread(nullptr, 0, observe, nullptr, 0, nullptr);
    if (!observer) {
        publish_engine(engine::unavailable(SC_REASON_INTERNAL_ERROR));
        publish_context(context::unavailable(SC_REASON_INTERNAL_ERROR));
        publish_save(save::unavailable(SC_REASON_INTERNAL_ERROR));
    }
    while (WaitForSingleObject(stop, 0) == WAIT_TIMEOUT) {
        ResetEvent(event.value);
        OVERLAPPED ov{}; ov.hEvent = event.value;
        const BOOL connected = ConnectNamedPipe(pipe, &ov);
        const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        DWORD count = 0;
        const DWORD connection = error == ERROR_PIPE_CONNECTED ? ERROR_SUCCESS :
            finish_io(pipe, ov, connected, error, stop, 1000, count);
        if (connection == ERROR_OPERATION_ABORTED) break;
        if (connection == ERROR_TIMEOUT) { DisconnectNamedPipe(pipe); continue; }
        if (connection == ERROR_NO_DATA || connection == ERROR_BROKEN_PIPE || connection == ERROR_PIPE_NOT_CONNECTED) {
            DisconnectNamedPipe(pipe); continue;
        }
        if (connection != ERROR_SUCCESS) { inspection_failed(connection); return 1; }
        const auto deadline = GetTickCount64() + 1000;
        Message data{};
        if (transfer(pipe, false, data.data(), static_cast<DWORD>(max_request), count, stop,
                     remaining(deadline)) == ERROR_SUCCESS) {
            uint16_t operation = inspect_operation;
            sc_diagnostic_request diagnostic{}; uint64_t after_event = 0, write_id = 0;
            sc_save_backup_request backup{};
            sc_weapon_points_request points{};
            const auto result = decode_request(data, count, &operation, &diagnostic, &after_event, &write_id, &backup, &points);
            sc_weapon_points_result points_result{};
            if (result == WireResult::ok && operation >= weapon_points_submit_operation) {
                points_result = operation == weapon_points_submit_operation ? native::submit_weapon_points(points) :
                    native::weapon_points_result(points, operation == weapon_points_cancel_operation,
                        operation == weapon_points_release_operation);
            }
            sc_save_backup_snapshot backup_result{};
            if (result == WireResult::ok && operation >= save_backup_submit_operation && operation <= save_backup_cancel_operation) {
                backup_result = operation == save_backup_submit_operation ? native::submit_backup(backup) :
                    native::backup_result(backup, operation == save_backup_cancel_operation);
            }
            sc_diagnostic_result diagnostic_result{};
            sc_diagnostic_detail detail{};
            if (result == WireResult::ok && operation >= diagnostic_submit_operation && operation <= diagnostic_detail_cancel_operation) {
                diagnostic_result = operation == diagnostic_submit_operation || operation == diagnostic_detail_submit_operation ?
                    native::submit(diagnostic, &detail) : native::result(diagnostic,
                        operation == diagnostic_cancel_operation || operation == diagnostic_detail_cancel_operation, &detail);
            }
            const DWORD size = static_cast<DWORD>(operation >= weapon_points_submit_operation ?
                encode_weapon_points_response(data, result, operation, current_snapshot(), points_result) :
                operation == save_installation_operation ?
                encode_installation_response(data, result, current_snapshot(), save::session().installation.inspect()) : operation >= save_backup_submit_operation ?
                encode_backup_response(data, result, operation, current_snapshot(), backup_result) : operation == save_write_operation ?
                encode_save_write_response(data, result, current_snapshot(), save::session().native_writes.snapshot(write_id)) :
                operation == save_admission_operation ?
                encode_save_admission_response(data, result, current_snapshot(), save::session().inspect()) : (operation == save_operation ?
                encode_save_response(data, result, current_snapshot(), current_save_snapshot()) : (operation >= native_operation ?
                encode_native_response(data, result, operation, current_snapshot(), native::inspect(after_event), diagnostic_result, detail) :
                (operation == context_operation ?
                encode_context_response(data, result, current_snapshot(), current_context_snapshot()) : (operation == engine_operation ?
                encode_engine_response(data, result, current_snapshot(), current_engine_snapshot()) :
                encode_response(data, result, current_snapshot()))))));
            if (transfer(pipe, true, data.data(), size, count, stop, remaining(deadline)) == ERROR_SUCCESS) {
                // Wait for client close (or reject extra input), so DisconnectNamedPipe
                // cannot discard the reply before it is read. Never FlushFileBuffers.
                transfer(pipe, false, data.data(), 1, count, stop, remaining(deadline));
            }
        }
        DisconnectNamedPipe(pipe);
    }
    DisconnectNamedPipe(pipe);
    return 0;
}
DWORD secure_pipe() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return GetLastError();
    DWORD size = 0;
    GetTokenInformation(token.value, TokenGroups, nullptr, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return GetLastError();
    std::vector<uint8_t> buffer(size);
    if (!GetTokenInformation(token.value, TokenGroups, buffer.data(), size, &size)) return GetLastError();
    const auto groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
    PSID logon = nullptr;
    for (DWORD i = 0; i < groups->GroupCount; ++i)
        if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            logon = groups->Groups[i].Sid;
    if (!logon) return ERROR_NO_SUCH_LOGON_SESSION;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(logon, &sid)) return GetLastError();
    // Protected DACL: only this logon, generic READ + write DATA. Deliberately
    // exclude FILE_CREATE_PIPE_INSTANCE (also implied by GENERIC_WRITE).
    const std::wstring sddl = L"D:P(A;;0x0012008b;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr)) return GetLastError();
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    pipe = CreateNamedPipeW(pipe_name(GetCurrentProcessId()).c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, static_cast<DWORD>(max_message), static_cast<DWORD>(max_message), 1000, &attributes);
    const DWORD error = pipe == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    LocalFree(descriptor);
    return error;
}
}
DWORD start_inspection() {
    if (worker) return ERROR_BUSY; // A failed stop must be retried, never replaced.
    DWORD error = secure_pipe();
    if (error != ERROR_SUCCESS) return error;
    stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop) error = GetLastError();
    else {
        worker = CreateThread(nullptr, 0, serve, nullptr, 0, nullptr);
        if (!worker) error = GetLastError();
    }
    if (error != ERROR_SUCCESS) {
        if (stop) CloseHandle(stop);
        stop = nullptr; CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE;
    }
    return error;
}
DWORD stop_inspection() {
    if (!worker) return ERROR_SUCCESS;
    SetEvent(stop); // Worker cancels/drains its own overlapped I/O.
    const auto deadline = GetTickCount64() + 5000;
    DWORD wait = WaitForSingleObject(worker, remaining(deadline));
    if (wait != WAIT_OBJECT_0) return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    // Joining the service first also synchronizes its observer-handle publication.
    if (observer) {
        wait = WaitForSingleObject(observer, remaining(deadline));
        if (wait != WAIT_OBJECT_0) return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        CloseHandle(observer); observer = nullptr;
    }
    CloseHandle(worker); CloseHandle(stop); CloseHandle(pipe);
    worker = nullptr; stop = nullptr; pipe = INVALID_HANDLE_VALUE;
    return ERROR_SUCCESS;
}
}
