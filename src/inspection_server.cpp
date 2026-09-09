#include "inspection_server.h"
#include "pipe_io.h"
#include "protocol.h"
#include <sddl.h>
#include <vector>

namespace sentinel {
namespace {
HANDLE pipe = INVALID_HANDLE_VALUE, stop = nullptr, worker = nullptr;

DWORD serve(void*) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) { inspection_failed(GetLastError()); return 1; }
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
        if (transfer(pipe, false, data.data(), static_cast<DWORD>(data.size()), count, stop,
                     remaining(deadline)) == ERROR_SUCCESS) {
            const auto result = decode_request(data, count);
            const DWORD size = static_cast<DWORD>(encode_response(data, result, current_snapshot()));
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
    const DWORD wait = WaitForSingleObject(worker, 5000);
    if (wait != WAIT_OBJECT_0) return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    CloseHandle(worker); CloseHandle(stop); CloseHandle(pipe);
    worker = nullptr; stop = nullptr; pipe = INVALID_HANDLE_VALUE;
    return ERROR_SUCCESS;
}
}
