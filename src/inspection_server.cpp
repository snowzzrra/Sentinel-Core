#include "startup_log.h"
#include "inspection_server.h"
#include "pipe_io.h"
#include "protocol_v2.h"
#include "engine_observer.h"
#include "context_observer.h"
#include "save_observer.h"
#include "native_runtime.h"
#include "commands_native.h"
#include <sddl.h>
#include <vector>
#include <cstdio>
#include <exception>
#include <new>

namespace sentinel {
namespace {
HANDLE stop = nullptr, observer = nullptr;
struct ConnectionWorker { HANDLE pipe = INVALID_HANDLE_VALUE, thread = nullptr; };
std::array<ConnectionWorker, 4> connections;

DWORD accept_v2(HANDLE endpoint) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) return GetLastError();
    OVERLAPPED ov{}; ov.hEvent = event.value;
    const BOOL ok = ConnectNamedPipe(endpoint, &ov);
    const auto error = ok ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_PIPE_CONNECTED) return ERROR_SUCCESS;
    DWORD count = 0;
    return finish_io(endpoint, ov, ok, error, stop, INFINITE, count);
}
DWORD read_v2(HANDLE endpoint, generated::wire_header_2& header, generated::Bytes& body) {
    generated::Bytes bytes(generated::header_size_2);
    DWORD count = 0;
    auto error = transfer(endpoint, false, bytes.data(), 1, count, stop, INFINITE);
    if (error != ERROR_SUCCESS) return error;
    if (count != 1) return ERROR_BROKEN_PIPE;
    const auto deadline = GetTickCount64() + 1000;
    error = transfer_exact(endpoint, false, bytes.data() + 1, 31, stop, deadline);
    if (error != ERROR_SUCCESS) return error;
    if (!v2::header(bytes, header, false)) return ERROR_INVALID_DATA;
    body.resize(header.payload_length);
    return transfer_exact(endpoint, false, body.data(), header.payload_length, stop, deadline);
}
void serve_v2_client(HANDLE endpoint) {
    for (;;) {
        generated::wire_header_2 header{}; generated::Bytes body, response;
        if (read_v2(endpoint, header, body) != ERROR_SUCCESS) return;
        if (!process_v2(header, body, response)) return;
#ifdef SC_FAULT_REDIRECT
        if (testing::drop_pipe_reply(header.operation)) return;
#endif
        if (transfer_exact(endpoint, true, response.data(), static_cast<DWORD>(response.size()), stop,
                           GetTickCount64() + 1000) != ERROR_SUCCESS) return;
    }
}
DWORD serve_v2(void* context) {
    const auto endpoint = static_cast<ConnectionWorker*>(context)->pipe;
    try {
        for (;;) {
            const auto wait = WaitForSingleObject(stop, 0);
            if (wait == WAIT_FAILED) { inspection_failed(GetLastError()); return 1; }
            if (wait != WAIT_TIMEOUT) return 0;
            const auto error = accept_v2(endpoint);
            if (error == ERROR_OPERATION_ABORTED) return 0;
            if (error == ERROR_SUCCESS) serve_v2_client(endpoint);
            else if (error != ERROR_NO_DATA) { inspection_failed(error); return 1; }
            if (!DisconnectNamedPipe(endpoint) && GetLastError() != ERROR_PIPE_NOT_CONNECTED) {
                inspection_failed(GetLastError()); return 1;
            }
        }
    } catch (...) { inspection_failed(ERROR_UNHANDLED_EXCEPTION); return 1; }
}

DWORD observer_failed(const char* stage, const char* error) noexcept {
    char line[768]{};
    std::snprintf(line, sizeof(line), "[Sentinel Core] observer stage=%s error=%.512s\n", stage, error);
    OutputDebugStringA(line);
    commands::diagnostic_trace("%s", line);
    publish_engine(engine::unavailable(SC_REASON_INTERNAL_ERROR));
    publish_context(context::unavailable(SC_REASON_INTERNAL_ERROR));
    publish_save(save::unavailable(SC_REASON_INTERNAL_ERROR));
    startup_log::record(current_snapshot(), SC_REASON_INTERNAL_ERROR);
    return 1;
}
DWORD observe(void*) {
    engine::LocalMemory memory;
    engine::Binding binding;
    uint64_t sequence = 0;
    uint64_t diagnostic_at = 0;
    const char* stage = "bind_host";
    try {
        if (WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) return 0;
        binding = engine::bind_host(memory, stop);
        stage = "native_start";
        native::start(binding, current_snapshot(), stop);
        stage = "startup_log";
        startup_log::record(current_snapshot(), binding.metadata.sample_reason);
        do {
            stage = "engine_sample";
            publish_engine(engine::sample(memory, binding, ++sequence));
            stage = "context_sample";
            const auto before = native::observation_stamp();
            const auto observed = context::sample(memory, binding, sequence, stop);
            stage = "context_publish";
            native::publish_context(observed, before);
            publish_context(observed);
            stage = "save_sample";
            publish_save(save::sample(memory, binding, sequence, stop));
            if (GetTickCount64() >= diagnostic_at) {
                stage = "startup_log";
                startup_log::record(current_snapshot(), binding.metadata.sample_reason);
                diagnostic_at = GetTickCount64() + 1000;
            }
        } while (WaitForSingleObject(stop, 100) == WAIT_TIMEOUT);
    } catch (const std::bad_alloc&) { return observer_failed(stage, "bad_alloc"); }
    catch (const std::exception& error) { return observer_failed(stage, error.what()); }
    catch (...) { return observer_failed(stage, "unknown_exception"); }
    return 0;
}
void start_observer() {
    observer = CreateThread(nullptr, 0, observe, nullptr, 0, nullptr);
    if (!observer) {
        publish_engine(engine::unavailable(SC_REASON_INTERNAL_ERROR));
        publish_context(context::unavailable(SC_REASON_INTERNAL_ERROR));
        publish_save(save::unavailable(SC_REASON_INTERNAL_ERROR));
    }
}
DWORD create_v2_endpoints(SECURITY_ATTRIBUTES& attributes) {
    for (size_t i = 0; i < connections.size(); ++i) {
        connections[i].pipe = CreateNamedPipeW(pipe_name_v2(GetCurrentProcessId()).c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (i == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE | WRITE_DAC : 0),
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            static_cast<DWORD>(connections.size()), 16384, 16384, 1000, &attributes);
        if (connections[i].pipe == INVALID_HANDLE_VALUE) return GetLastError();
    }
    return ERROR_SUCCESS;
}
DWORD create_endpoints(SECURITY_ATTRIBUTES& attributes) {
    BOOL present = FALSE, defaulted = FALSE; PACL dacl = nullptr; void* raw = nullptr;
    if (!GetSecurityDescriptorDacl(attributes.lpSecurityDescriptor, &present, &dacl, &defaulted)) return GetLastError();
    if (!present || !dacl) return ERROR_INVALID_SECURITY_DESCR;
    if (!GetAce(dacl, 0, &raw)) return GetLastError();
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    ace->Mask |= FILE_GENERIC_WRITE;
    const auto error = create_v2_endpoints(attributes);
    ace->Mask = client_access;
    if (error != ERROR_SUCCESS) return error;
    // own every instance before restricting creation rights
    if (!SetKernelObjectSecurity(connections[0].pipe, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                 attributes.lpSecurityDescriptor)) return GetLastError();
    return ERROR_SUCCESS;
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
    // limit clients to this logon without pipe creation rights
    const std::wstring sddl = L"D:P(A;;0x0012008b;;;" + std::wstring(sid) + L")";
    if (LocalFree(sid)) return GetLastError();
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr)) return GetLastError();
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    DWORD error = create_endpoints(attributes);
    if (LocalFree(descriptor) && error == ERROR_SUCCESS) error = GetLastError();
    return error;
}
}
DWORD close_service_handles() {
    DWORD error = ERROR_SUCCESS;
    auto close = [&](HANDLE& handle) {
        if (handle && handle != INVALID_HANDLE_VALUE && !CloseHandle(handle)) { error = GetLastError(); return; }
        handle = nullptr;
    };
    close(observer);
    for (auto& connection : connections) { close(connection.thread); close(connection.pipe); }
    close(stop);
    return error;
}
DWORD start_inspection() {
    if (stop || observer) return ERROR_BUSY;
    DWORD error = secure_pipe();
    if (error != ERROR_SUCCESS) { const auto cleanup = close_service_handles(); return cleanup ? cleanup : error; }
    stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop) { error = GetLastError(); const auto cleanup = close_service_handles(); return cleanup ? cleanup : error; }
    for (auto& connection : connections) {
        connection.thread = CreateThread(nullptr, 0, serve_v2, &connection, 0, nullptr);
        if (!connection.thread) { error = GetLastError(); break; }
    }
    if (error == ERROR_SUCCESS) start_observer();
    if (error != ERROR_SUCCESS) { const auto cleanup = stop_inspection(); return cleanup ? cleanup : error; }
    return ERROR_SUCCESS;
}
DWORD join_service(HANDLE thread, ULONGLONG deadline) {
    if (!thread) return ERROR_SUCCESS;
    const auto wait = WaitForSingleObject(thread, remaining(deadline));
    if (wait == WAIT_OBJECT_0) return ERROR_SUCCESS;
    return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
}
DWORD stop_inspection() {
    if (!stop) return ERROR_SUCCESS;
    if (!SetEvent(stop)) return GetLastError();
    const auto deadline = GetTickCount64() + 5000;
    if (const auto error = join_service(observer, deadline)) return error;
    for (const auto& connection : connections)
        if (const auto error = join_service(connection.thread, deadline)) return error;
    return close_service_handles();
}
}
