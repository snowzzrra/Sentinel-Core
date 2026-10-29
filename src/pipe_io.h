#pragma once
#include <windows.h>
#include <string>

namespace sentinel {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE v = nullptr) : value(v) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
inline std::wstring pipe_name(DWORD pid) {
    return L"\\\\.\\pipe\\sentinel_core.inspect.v1." + std::to_wstring(pid);
}
inline std::wstring pipe_name_v2(DWORD pid) {
    return L"\\\\.\\pipe\\sentinel_core.inspect.v2." + std::to_wstring(pid);
}
inline DWORD remaining(ULONGLONG deadline) {
    const auto now = GetTickCount64();
    return now < deadline ? static_cast<DWORD>(deadline - now) : 0;
}
inline bool process_time(HANDLE process, uint64_t& value) {
    FILETIME created{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exit, &kernel, &user)) return false;
    value = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return true;
}
// Each operation owns its event and buffer until the kernel completes cancellation.
// The server's public shutdown bound is enforced by its lifecycle owner's join.
inline DWORD finish_io(HANDLE pipe, OVERLAPPED& ov, BOOL immediate, DWORD error,
                       HANDLE stop, DWORD timeout, DWORD& transferred) {
    if (immediate) return ERROR_SUCCESS;
    if (error != ERROR_IO_PENDING) return error;
    HANDLE events[] = {ov.hEvent, stop};
    const DWORD wait = WaitForMultipleObjects(stop ? 2u : 1u, events, FALSE, timeout);
    if (wait == WAIT_OBJECT_0) {
        return GetOverlappedResult(pipe, &ov, &transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
    }
    const DWORD result = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT :
        (wait == WAIT_OBJECT_0 + 1 ? ERROR_OPERATION_ABORTED : GetLastError());
    CancelIoEx(pipe, &ov);
    GetOverlappedResult(pipe, &ov, &transferred, TRUE); // drain before stack/event reuse
    return result;
}
inline DWORD transfer(HANDLE pipe, bool write, void* data, DWORD size, DWORD& count,
                      HANDLE stop, DWORD timeout) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) return GetLastError();
    OVERLAPPED ov{}; ov.hEvent = event.value;
    const BOOL ok = write ? WriteFile(pipe, data, size, &count, &ov) : ReadFile(pipe, data, size, &count, &ov);
    return finish_io(pipe, ov, ok, ok ? ERROR_SUCCESS : GetLastError(), stop, timeout, count);
}
inline DWORD transfer_exact(HANDLE pipe, bool write, void* data, DWORD size,
                            HANDLE stop, ULONGLONG deadline) {
    auto* bytes = static_cast<uint8_t*>(data);
    DWORD offset = 0;
    while (offset < size) {
        const DWORD timeout = remaining(deadline);
        if (!timeout) return ERROR_TIMEOUT;
        DWORD count = 0;
        const auto error = transfer(pipe, write, bytes + offset, size - offset, count, stop, timeout);
        if (error != ERROR_SUCCESS) return error;
        if (!count) return ERROR_BROKEN_PIPE;
        offset += count;
    }
    return ERROR_SUCCESS;
}
// os pipe identity queries also require read-attributes access
constexpr DWORD client_access = FILE_GENERIC_READ | FILE_WRITE_DATA;
}
