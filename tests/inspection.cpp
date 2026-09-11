#include "sentinel_inspection.h"
#include "pipe_io.h"
#include "protocol.h"
#include "save_storage.h"
#include <aclapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s (win32=%lu)\n", __LINE__, #c, GetLastError()); std::exit(1); } } while (0)
using namespace sentinel;
template<class T> T symbol(HMODULE module, const char* name) {
    auto address = GetProcAddress(module, name); CHECK(address);
    T result; static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result)); return result;
}
std::wstring quote(const std::wstring& text) { return L"\"" + text + L"\""; }
std::wstring executable;
Handle test_job;

struct Child {
    Handle process, thread;
    DWORD pid = 0;
    Child(const std::wstring& command, HANDLE output = nullptr) {
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        if (output) {
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = startup.hStdError = output;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        }
        PROCESS_INFORMATION info{};
        std::vector<wchar_t> text(command.begin(), command.end()); text.push_back(0);
        CHECK(CreateProcessW(nullptr, text.data(), nullptr, nullptr, output != nullptr,
                             CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info));
        process.value = info.hProcess; thread.value = info.hThread; pid = info.dwProcessId;
        CHECK(AssignProcessToJobObject(test_job.value, process.value));
    }
    DWORD wait(DWORD timeout = 7000) {
        CHECK(WaitForSingleObject(process.value, timeout) == WAIT_OBJECT_0);
        DWORD code = 0; CHECK(GetExitCodeProcess(process.value, &code)); return code;
    }
};
struct Host {
    std::wstring prefix;
    Handle ready, stop;
    Child child;
    Host(const std::wstring& dll, unsigned sequence, bool idle = false)
        : prefix(L"Local\\SentinelInspectionTest." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(sequence)),
          ready(CreateEventW(nullptr, TRUE, FALSE, (prefix + L".ready").c_str())),
          stop(CreateEventW(nullptr, TRUE, FALSE, (prefix + L".stop").c_str())),
          child(quote(executable) + (idle ? L" --idle " : L" --host ") + quote(dll) + L" " + quote(prefix)) {
        CHECK(ready && stop);
        HANDLE events[] = {ready.value, child.process.value};
        CHECK(WaitForMultipleObjects(2, events, FALSE, 5000) == WAIT_OBJECT_0);
    }
    void shutdown() { CHECK(SetEvent(stop.value)); CHECK(child.wait() == 0); }
};
std::string probe(const std::wstring& path, const std::wstring& args, DWORD expected) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle read, write;
    CHECK(CreatePipe(&read.value, &write.value, &sa, 16384));
    CHECK(SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0));
    Child child(quote(path) + L" " + args, write.value);
    CHECK(child.wait() == expected);
    CloseHandle(write.value); write.value = nullptr;
    char buffer[16384]{}; DWORD size = 0;
    CHECK(ReadFile(read.value, buffer, sizeof(buffer), &size, nullptr));
    return std::string(buffer, size);
}
HANDLE open_pipe(DWORD pid, DWORD access = client_access) {
    const auto name = pipe_name(pid);
    const auto deadline = GetTickCount64() + 3000;
    for (;;) {
        HANDLE pipe = CreateFileW(name.c_str(), access, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY) return pipe;
        CHECK(remaining(deadline) != 0);
        CHECK(WaitNamedPipeW(name.c_str(), remaining(deadline)));
    }
}
void exchange(DWORD pid, Message request, DWORD length, WireResult expected) {
    Handle pipe(open_pipe(pid)); CHECK(pipe);
    DWORD count = 0;
    CHECK(transfer(pipe.value, true, request.data(), length, count, nullptr, 1000) == ERROR_SUCCESS);
    Message response{};
    CHECK(transfer(pipe.value, false, response.data(), static_cast<DWORD>(response.size()), count, nullptr, 1000) == ERROR_SUCCESS);
    WireResult result{}; Snapshot snapshot;
    CHECK(decode_response(response, count, result, snapshot) && result == expected);
}
void permissions(DWORD pid) {
    Handle pipe(open_pipe(pid, client_access | READ_CONTROL)); CHECK(pipe);
    PACL dacl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    CHECK(GetSecurityInfo(pipe.value, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                          &dacl, nullptr, &descriptor) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    CHECK(GetSecurityDescriptorControl(descriptor, &control, &revision));
    CHECK((control & SE_DACL_PROTECTED) != 0 && dacl && dacl->AceCount == 1);
    void* raw = nullptr; CHECK(GetAce(dacl, 0, &raw));
    const auto ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    CHECK(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && ace->Mask == client_access);
    Handle token; CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value));
    DWORD size = 0; GetTokenInformation(token.value, TokenGroups, nullptr, 0, &size);
    std::vector<uint8_t> buffer(size);
    CHECK(GetTokenInformation(token.value, TokenGroups, buffer.data(), size, &size));
    const auto groups = reinterpret_cast<TOKEN_GROUPS*>(buffer.data());
    bool matched = false;
    for (DWORD i = 0; i < groups->GroupCount; ++i)
        if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            matched = EqualSid(&ace->SidStart, groups->Groups[i].Sid) != FALSE;
    CHECK(matched);
    LocalFree(descriptor);
    CloseHandle(pipe.value); pipe.value = nullptr;
    Handle excessive(open_pipe(pid, GENERIC_READ | GENERIC_WRITE));
    CHECK(!excessive && GetLastError() == ERROR_ACCESS_DENIED);
}
HANDLE fake_pipe(DWORD pid) {
    return CreateNamedPipeW(pipe_name(pid).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
        1, 512, 512, 1000, nullptr);
}
void fake_response(const std::wstring& probe_path, const Snapshot& snapshot, int variant, bool context_query = false) {
    Handle pipe(fake_pipe(GetCurrentProcessId())); CHECK(pipe);
    std::thread server([&] {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); CHECK(event);
        OVERLAPPED ov{}; ov.hEvent = event.value;
        BOOL connected = ConnectNamedPipe(pipe.value, &ov);
        DWORD error = connected ? ERROR_SUCCESS : GetLastError(), count = 0;
        CHECK(error == ERROR_PIPE_CONNECTED || finish_io(pipe.value, ov, connected, error, nullptr, 2000, count) == ERROR_SUCCESS);
        Message request{};
        CHECK(transfer(pipe.value, false, request.data(), static_cast<DWORD>(request.size()), count, nullptr, 2000) == ERROR_SUCCESS);
        if (variant == 0) { // An available endpoint whose server never replies.
            CHECK(transfer(pipe.value, false, request.data(), 1, count, nullptr, 2000) == ERROR_BROKEN_PIPE);
        } else {
            Snapshot forged = snapshot;
            forged.pid = GetCurrentProcessId();
            CHECK(process_time(GetCurrentProcess(), forged.process_created));
            if (variant == 1) ++forged.process_created;
            auto code = variant == 2 ? WireResult::incompatible_protocol :
                (variant == 4 ? WireResult::unsupported_operation : WireResult::ok);
            DWORD size = static_cast<DWORD>(encode_response(request, code, forged));
            if (variant == 3) size = 7;
            CHECK(transfer(pipe.value, true, request.data(), size, count, nullptr, 2000) == ERROR_SUCCESS);
            transfer(pipe.value, false, request.data(), 1, count, nullptr, 2000);
        }
        DisconnectNamedPipe(pipe.value);
    });
    const DWORD expected[] = {5, 6, 7, 9, 8};
    const auto start = GetTickCount64();
    const auto output = probe(probe_path, L"--pid " + std::to_wstring(GetCurrentProcessId()) +
        L" --timeout-ms 150 --json" + (context_query ? L" --context" : (variant == 4 ? L" --engine" : L"")), expected[variant]);
    CHECK(!output.empty() && GetTickCount64() - start < 4000);
    server.join();
}
void write_codec() {
    static_assert(sizeof(sc_save_write_snapshot) == 160 && sizeof(sc_save_snapshot) == 368 && sizeof(sc_save_admission_snapshot) == 160);
    Snapshot original{}, decoded{}; original.core.abi_version = SC_ABI_VERSION;
    strcpy_s(original.core.version, "0.6.0"); strcpy_s(original.core.build_id, "synthetic-write");
    sc_save_write_snapshot value{}, received{}; value.size = sizeof(value); value.abi_version = SC_SAVE_WRITE_ABI_VERSION;
    Message data{}; WireResult code{};
    const auto check = [&](bool valid) {
        const auto size = encode_save_write_response(data, WireResult::ok, original, value);
        CHECK(decode_save_write_response(data, size, code, decoded, received) == valid);
        if (valid) CHECK(std::memcmp(&value, &received, sizeof(value)) == 0);
        return size;
    };
    const auto size = check(true);
    for (size_t n = 0; n < size; ++n) CHECK(!decode_save_write_response(data, n, code, decoded, received));
    value.state = SC_SAVE_WRITE_NOT_RETAINED; value.operation_id = UINT64_MAX; check(true);
    value.state = SC_SAVE_WRITE_PENDING; value.flags = SC_SAVE_WRITE_PROVIDER_ALIVE | SC_SAVE_WRITE_SOURCE_VALID;
    strcpy_s(value.directory, "PROFILE"); check(true);
    value.state = SC_SAVE_WRITE_INDETERMINATE; value.flags &= ~SC_SAVE_WRITE_PROVIDER_ALIVE; check(true);
    value.flags |= SC_SAVE_WRITE_PROVIDER_TERMINAL; value.native_state = 1;
    value.state = SC_SAVE_WRITE_NATIVE_FAILED; check(true);
    value.native_state = 0; value.native_outcome = 1; value.native_value = 0x40; check(true);
    value.native_outcome = 0; value.native_value = 1; value.state = SC_SAVE_WRITE_NATIVE_SUCCEEDED; check(true);
    value.sdk_sequence = 99; value.file_count = value.submitted = value.completed = 2;
    value.flags |= SC_SAVE_WRITE_PAYLOADS_PREPARED | SC_SAVE_WRITE_PAYLOADS_CAPTURED |
        SC_SAVE_WRITE_CALLBACKS_SUCCEEDED | SC_SAVE_WRITE_SDK_SUCCEEDED;
    value.state = SC_SAVE_WRITE_SDK_CONFIRMED; check(true);
    const auto confirmed = value;
    value.flags |= SC_SAVE_WRITE_READBACK_REQUIRED | SC_SAVE_WRITE_READBACK_ACTIVE;
    check(false); value.state = SC_SAVE_WRITE_READBACK_PENDING; check(true);
    value.flags |= SC_SAVE_WRITE_READBACK_HASHES; check(true);
    value.flags |= SC_SAVE_WRITE_READBACK_TERMINAL;
    check(false); value.state = SC_SAVE_WRITE_READBACK_CONFIRMED; check(true);
    value.flags &= ~SC_SAVE_WRITE_READBACK_ACTIVE; check(true);
    value.flags &= ~SC_SAVE_WRITE_READBACK_HASHES; check(false);
    value.flags |= SC_SAVE_WRITE_READBACK_ERROR; value.state = SC_SAVE_WRITE_READBACK_FAILED; check(true);
    value.flags &= ~SC_SAVE_WRITE_READBACK_REQUIRED; check(false); value = confirmed;
    value.flags |= SC_SAVE_WRITE_TRACKING_LOST; check(false);
    value.state = SC_SAVE_WRITE_NATIVE_SUCCEEDED; check(true); value = confirmed;
    value.flags |= SC_SAVE_WRITE_UNPROVEN; check(false);
    value.state = SC_SAVE_WRITE_NATIVE_SUCCEEDED; check(true); value = confirmed;
    value.native_value = 0; check(false); value = confirmed;
    value.flags &= ~SC_SAVE_WRITE_PROVIDER_TERMINAL; check(false); value = confirmed;
    value.flags &= ~SC_SAVE_WRITE_CALLBACKS_SUCCEEDED; check(false); value = confirmed;
    value.pending_handles = 1; check(false); value = confirmed;
    value.completed = 1; check(false); value = confirmed;
    value.submitted = 3; check(false); value = confirmed;
    value.sdk_sequence = 0; check(false); value = confirmed;
    value.file_count = 1025; check(false); value = confirmed;
    value.preparation_jobs = 129; check(false); value = confirmed;
    value.reserved = 1; check(false); value = confirmed;
    value.reserved_bytes[15] = 1; check(false); value = confirmed;
    value.flags |= 32768; check(false); value = confirmed;
    value.abi_version = 2; check(false); value = confirmed;
    value.directory[63] = 'x'; check(false); value = confirmed;
    value.directory[0] = '\n'; check(false); value = confirmed;
    const auto request_size = encode_save_write_request(data, UINT64_MAX);
    uint16_t operation = 0; uint64_t id = 0;
    CHECK(decode_request(data, request_size, &operation, nullptr, nullptr, &id) == WireResult::ok);
    CHECK(operation == save_write_operation && id == UINT64_MAX);
    CHECK(decode_request(data, request_size - 1) == WireResult::malformed);
    data[16] = 1; CHECK(decode_request(data, request_size) == WireResult::capability_unavailable);
    const auto error_size = encode_save_write_response(data, WireResult::capability_unavailable, original, value);
    CHECK(decode_save_write_response(data, error_size, code, decoded, received) && code == WireResult::capability_unavailable);
}
void admission_codec() {
    Snapshot original{}, decoded{};
    original.core.abi_version = SC_ABI_VERSION;
    strcpy_s(original.core.version, "0.6.0"); strcpy_s(original.core.build_id, "synthetic-admission");
    sc_save_admission_snapshot value{}, received{};
    value.size = sizeof(value); value.abi_version = SC_SAVE_ADMISSION_ABI_VERSION; value.required_routes = 63;
    Message data{}; WireResult code{};
    const auto check = [&](bool valid) {
        const auto size = encode_save_admission_response(data, WireResult::ok, original, value);
        CHECK(decode_save_admission_response(data, size, code, decoded, received) == valid);
        if (valid) CHECK(std::memcmp(&value, &received, sizeof(value)) == 0);
        return size;
    };
    const auto size = check(true);
    for (size_t n = 0; n < size; ++n) CHECK(!decode_save_admission_response(data, n, code, decoded, received));
    value.flags = SC_SAVE_SESSION_ROUTED; check(false); value.flags = 0;
    ++value.abi_version; check(false); --value.abi_version;
    value.state = SC_SAVE_SESSION_PREPARED; check(false);
    std::memset(value.namespace_id, 'a', 64); std::memcpy(value.native_root, "ap-", 3);
    std::memset(value.native_root + 3, 'a', 40); value.prepared_routes = 3; check(true);
    value.native_root[3] = 'b'; check(false); value.native_root[3] = 'a';
    value.state = SC_SAVE_SESSION_REJECTED; value.fault = 3;
    value.flags = SC_SAVE_SESSION_STARTUP_QUALIFIED; check(true);
    value.flags |= SC_SAVE_SESSION_ACCEPTING; check(false);
    value.state = SC_SAVE_SESSION_ADMITTED; value.fault = 0; value.prepared_routes = 63;
    value.flags |= SC_SAVE_SESSION_ROUTED; check(true);
    value.state = SC_SAVE_SESSION_BINDING; check(false);
    value.flags &= ~SC_SAVE_SESSION_ACCEPTING; check(true);
    value.fault = 1; check(false); value.fault = 0;
    value.state = SC_SAVE_SESSION_BINDING + 1; check(false);
    value.state = SC_SAVE_SESSION_FAULTED; value.fault = 11;
    value.flags &= ~SC_SAVE_SESSION_ACCEPTING; check(true);
    value.fault = 16; check(true); value.fault = 17; check(false); value.fault = 11;
    value.namespace_id[64] = 'a'; check(false);
    uint16_t operation = 0;
    const auto request = encode_request(data, save_admission_capability, wire_version, save_admission_operation);
    CHECK(decode_request(data, request, &operation) == WireResult::ok && operation == save_admission_operation);
}
void prelaunch_contract(const std::wstring& dll, const std::wstring& probe_path) {
    namespace fs = std::filesystem;
    wchar_t temporary[MAX_PATH]{}; CHECK(GetTempPathW(MAX_PATH, temporary));
    const auto parent = fs::canonical(temporary);
    const auto root = parent / ("sentinel-admission-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    CHECK(fs::create_directory(root));
    const auto fixture_file = root / "fixture.txt";
    storage::Descriptor descriptor;
    descriptor.identity = {"admission-fixture", 0u, 1u, std::string(64, 'a')};
    descriptor.root = root.wstring();
    std::unique_ptr<storage::Namespace> lease;
    CHECK(storage::prepare(descriptor, lease).ok());
    const auto namespace_id = lease->metadata().namespace_id;
    lease.reset();
    {
        std::ofstream out(fixture_file, std::ios::binary);
        out << "sentinel-test-session-v1\nseed_hex=61646d697373696f6e2d66697874757265\nteam=0\nslot=1\n"
            "generation_fingerprint=" << std::string(64, 'a') <<
            "\nprovenance=synthetic-fixture\nroot=" << root.u8string() << '\n';
        out.close(); CHECK(out.good());
    }
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", fixture_file.c_str()));
    Host prepared(dll, 30);
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", nullptr));
    const auto status = query_save_admission(prepared.child.pid, 2000);
    CHECK(status.result == ProbeResult::ok && status.admission.state == SC_SAVE_SESSION_PREPARED);
    CHECK(status.admission.namespace_id == namespace_id && status.admission.flags == 0);
    storage::Metadata metadata;
    CHECK(storage::inspect(descriptor, metadata).outcome == storage::Outcome::ownership_conflict);
    const auto capture = probe(probe_path, L"--pid " + std::to_wstring(prepared.child.pid) + L" --save-admission --json", 8);
    CHECK(capture.find("\"state\":\"prepared\"") != std::string::npos);
    prepared.shutdown(); // Child verifies retained module, sticky refusal, and no reinitialization.
    CHECK(storage::reopen(descriptor, lease).ok()); lease.reset();
    { std::ofstream out(fixture_file, std::ios::binary | std::ios::trunc); out << "invalid-fixture\n"; }
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", fixture_file.c_str()));
    Host invalid(dll, 31);
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", nullptr));
    const auto rejected = query_save_admission(invalid.child.pid, 2000);
    CHECK(rejected.result == ProbeResult::ok && rejected.admission.state == SC_SAVE_SESSION_REJECTED);
    CHECK(rejected.admission.fault == 1 && rejected.admission.namespace_id[0] == 0);
    invalid.shutdown();
    CHECK(fs::canonical(root).parent_path() == parent && root.filename().u8string().find("sentinel-admission-") == 0);
    fs::remove_all(root);
    std::puts("PASS production prelaunch reader/lease, DLL retention before hooks, sticky shutdown and probe refusal; synthetic hosts only");
}
int wmain(int argc, wchar_t** argv) {
    admission_codec(); write_codec();
    executable = argv[0];
    if (argc == 4 && (wcscmp(argv[1], L"--host") == 0 || wcscmp(argv[1], L"--idle") == 0)) {
        const bool idle = wcscmp(argv[1], L"--idle") == 0;
        const std::wstring prefix = argv[3];
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (prefix + L".ready").c_str()));
        Handle stop(OpenEventW(SYNCHRONIZE, FALSE, (prefix + L".stop").c_str())); CHECK(ready && stop);
        HMODULE core = nullptr;
        if (!idle) {
            core = LoadLibraryExW(argv[2], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32); CHECK(core);
            CHECK(symbol<decltype(&sc_initialize)>(core, "sc_initialize")(SC_ABI_VERSION, 0) == SC_OK);
            const auto inspect_context = symbol<decltype(&sc_context_inspect)>(core, "sc_context_inspect");
            sc_context_snapshot context{};
            CHECK(inspect_context(2, sizeof(context), &context) == SC_ABI_MISMATCH);
            CHECK(inspect_context(1, sizeof(context) - 1, &context) == SC_INVALID_ARGUMENT);
            CHECK(inspect_context(1, sizeof(context), nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_context(1, sizeof(context), &context) == SC_OK && context.pid == GetCurrentProcessId());
            const auto inspect_save = symbol<decltype(&sc_save_inspect)>(core, "sc_save_inspect");
            sc_save_snapshot save{};
            CHECK(inspect_save(2, sizeof(save), &save) == SC_ABI_MISMATCH);
            CHECK(inspect_save(1, sizeof(save) - 1, &save) == SC_INVALID_ARGUMENT);
            CHECK(inspect_save(1, sizeof(save), nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_save(1, sizeof(save), &save) == SC_OK && save.pid == GetCurrentProcessId());
            CHECK(save.mutation_available == 0 && save.mutation_reason == SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN);
            const auto inspect_admission = symbol<decltype(&sc_save_admission_inspect)>(core, "sc_save_admission_inspect");
            sc_save_admission_snapshot admission{};
            CHECK(inspect_admission(2, sizeof(admission), &admission) == SC_ABI_MISMATCH);
            CHECK(inspect_admission(1, sizeof(admission) - 1, &admission) == SC_INVALID_ARGUMENT);
            CHECK(inspect_admission(1, sizeof(admission), nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_admission(1, sizeof(admission), &admission) == SC_OK && admission.size == 160);
            const auto inspect_write = symbol<decltype(&sc_save_write_inspect)>(core, "sc_save_write_inspect");
            sc_save_write_snapshot write{};
            CHECK(inspect_write(2, sizeof(write), 0, &write) == SC_ABI_MISMATCH);
            CHECK(inspect_write(1, sizeof(write) - 1, 0, &write) == SC_INVALID_ARGUMENT);
            CHECK(inspect_write(1, sizeof(write), 0, nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_write(1, sizeof(write), 0, &write) == SC_OK && write.size == 160 && write.state == SC_SAVE_WRITE_NONE);
            CHECK(inspect_write(1, sizeof(write), UINT64_MAX, &write) == SC_OK &&
                write.state == SC_SAVE_WRITE_NOT_RETAINED && write.operation_id == UINT64_MAX);
        }
        CHECK(SetEvent(ready.value));
        CHECK(WaitForSingleObject(stop.value, 30000) == WAIT_OBJECT_0);
        if (core) {
            const auto inspect_admission = symbol<decltype(&sc_save_admission_inspect)>(core, "sc_save_admission_inspect");
            sc_save_admission_snapshot admission{};
            CHECK(inspect_admission(1, sizeof(admission), &admission) == SC_OK);
            const bool retained = admission.namespace_id[0] != 0;
            CHECK(symbol<decltype(&sc_shutdown)>(core, "sc_shutdown")() == static_cast<sc_result>(retained ? SC_UNLOAD_RETAINED : SC_OK));
            CHECK(FreeLibrary(core));
            if (retained) {
                CHECK(GetModuleHandleW(L"sentinel_core.dll") == core);
                CHECK(inspect_admission(1, sizeof(admission), &admission) == SC_OK);
                CHECK(admission.state == SC_SAVE_SESSION_REJECTED && admission.fault == 6);
                CHECK(symbol<decltype(&sc_initialize)>(core, "sc_initialize")(SC_ABI_VERSION, 0) == SC_UNLOAD_RETAINED);
            }
        }
        return 0;
    }
    CHECK(argc == 3 || (argc == 4 && wcscmp(argv[3], L"--prelaunch") == 0));
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", nullptr));
    test_job.value = CreateJobObjectW(nullptr, nullptr); CHECK(test_job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(SetInformationJobObject(test_job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    const std::wstring dll = argv[1], probe_path = argv[2];
    if (argc == 4) { prelaunch_contract(dll, probe_path); return 0; }
    Host first(dll, 1), second(dll, 2);
    const auto initial = query(first.child.pid, 2000);
    if (initial.result != ProbeResult::ok) std::fprintf(stderr, "initial query=%s win32=%u server_pid=%u\n",
        result_name(initial.result), initial.win32_error, initial.server_pid);
    CHECK(initial.result == ProbeResult::ok && initial.server_pid == first.child.pid);
    CHECK(initial.snapshot.pid != GetCurrentProcessId() && initial.snapshot.pid == first.child.pid);
    CHECK(initial.snapshot.core.abi_version == SC_ABI_VERSION && initial.snapshot.core.capabilities == 3);
    CHECK(initial.snapshot.core.state == SC_READY && initial.snapshot.service == ServiceState::listening);
    CHECK(initial.snapshot.core.initialization_count == 1 && std::strlen(initial.snapshot.core.build_id) == 64);
    CHECK(std::strcmp(initial.snapshot.core.version, "0.6.0") == 0);
    uint64_t created = 0; CHECK(process_time(first.child.process.value, created));
    CHECK(initial.snapshot.process_created == created);
    const auto other = query(second.child.pid, 2000);
    CHECK(other.result == ProbeResult::ok && initial.snapshot.instance != other.snapshot.instance);
    for (int i = 0; i < 8; ++i) {
        const auto repeated = query(first.child.pid, 2000);
        CHECK(repeated.result == ProbeResult::ok && repeated.snapshot.instance == initial.snapshot.instance);
    }
    const auto args = L"--pid " + std::to_wstring(first.child.pid) + L" --timeout-ms 2000";
    sc_save_backup_request backup{};
    backup.execution.expected.pid = first.child.pid; backup.execution.expected.process_created = created;
    std::memcpy(backup.execution.expected.instance_id, initial.snapshot.instance.data(), 16);
    backup.execution.expected.lifecycle_generation = 1; backup.execution.request_id = UINT64_MAX;
    backup.execution.nonce[0] = 42; backup.execution.deadline_ms = 1000; backup.work_deadline_ms = 10000;
    std::memset(backup.namespace_id, 'a', 64);
    const auto rejected_backup = query_save_backup(first.child.pid, 2000, save_backup_submit_operation, backup);
    CHECK(rejected_backup.result == ProbeResult::ok && rejected_backup.backup.state == SC_BACKUP_REJECTED &&
        !(rejected_backup.backup.flags & SC_BACKUP_NATIVE_ENTERED));
    CHECK(query_save_backup(first.child.pid, 2000, save_backup_result_operation, backup).backup.state == SC_BACKUP_UNKNOWN);
    CHECK(query_save_backup(first.child.pid, 2000, save_backup_cancel_operation, backup).backup.state == SC_BACKUP_UNKNOWN);
    auto wrong_backup = backup; ++wrong_backup.execution.expected.process_created;
    CHECK(query_save_backup(first.child.pid, 2000, save_backup_submit_operation, wrong_backup).result == ProbeResult::process_mismatch);
    const auto backup_args = args + L" --namespace " + std::wstring(64, L'a') + L" --campaign game --slot 0 --json";
    const auto unavailable_backup = probe(probe_path, backup_args + L" --native-backup", 8);
    CHECK(unavailable_backup.find("\"native_entered\":false") != std::string::npos);
    probe(probe_path, backup_args + L" --native-backup --slot 1", 2);
    probe(probe_path, backup_args + L" --native-backup-result", 2);
    const auto backup_instance = instance_text(initial.snapshot.instance);
    const std::wstring backup_scope = L" --request-id 18446744073709551615 --nonce " + std::wstring(L"2a") + std::wstring(30, L'0') +
        L" --expect-created " + std::to_wstring(created) + L" --expect-instance " +
        std::wstring(backup_instance.begin(), backup_instance.end()) +
        L" --expect-generation 1";
    const auto unknown_backup = probe(probe_path, backup_args + backup_scope + L" --native-backup-result", 8);
    CHECK(unknown_backup.find("\"state\":\"unknown\"") != std::string::npos);
    const auto readable = probe(probe_path, args, 0);
    CHECK(readable.find("OS server verified, non_game_host") != std::string::npos);
    const auto json = probe(probe_path, args + L" --json", 0);
    CHECK(json.find("\"result\":\"ok\"") != std::string::npos);
    CHECK(json.find("\"host_kind\":\"non_game_host\"") != std::string::npos);
    CHECK(json.find("\"engine_integration\":\"unavailable\"") != std::string::npos);
    CHECK(json.find(instance_text(initial.snapshot.instance)) != std::string::npos);
    auto engine = query_engine(first.child.pid, 2000);
    const auto observe_deadline = GetTickCount64() + 5000;
    while (engine.result == ProbeResult::ok && engine.engine.pe_reason == SC_REASON_NOT_SAMPLED && GetTickCount64() < observe_deadline) {
        CHECK(query(first.child.pid, 150).result == ProbeResult::ok);
        Sleep(100); engine = query_engine(first.child.pid, 2000);
    }
    CHECK(engine.result == ProbeResult::ok && engine.snapshot.instance == initial.snapshot.instance);
    CHECK(engine.engine.pe_reason == SC_REASON_NONE && engine.engine.profile == SC_PROFILE_NONE);
    CHECK(engine.engine.root_locator_reason == SC_REASON_SIGNATURE_MISSING);
    CHECK(engine.engine.fields[SC_ENGINE_PLAYER_PRESENT].validity == SC_OBSERVATION_UNKNOWN);
    Sleep(150);
    const auto fresh_engine = query_engine(first.child.pid, 2000);
    CHECK(fresh_engine.result == ProbeResult::ok && fresh_engine.engine.sequence > engine.engine.sequence);
    CHECK(query(first.child.pid, 2000).result == ProbeResult::ok);
    const auto engine_json = probe(probe_path, args + L" --engine --json", 0);
    CHECK(engine_json.find("\"operation\":\"engine\"") != std::string::npos);
    CHECK(engine_json.find("\"profile\":\"unrecognized\"") != std::string::npos);
    CHECK(engine_json.find("\"value\":null") != std::string::npos);
    const auto context = query_context(first.child.pid, 2000);
    CHECK(context.result == ProbeResult::ok && context.snapshot.instance == initial.snapshot.instance);
    CHECK(context.context.current_map.reason == SC_REASON_PROFILE_UNRECOGNIZED);
    CHECK(context.context.fields[SC_CONTEXT_LOAD_SERIAL].reason == SC_CONTEXT_UNSUPPORTED);
    const auto context_json = probe(probe_path, args + L" --context --json", 0);
    CHECK(context_json.find("\"operation\":\"context\"") != std::string::npos);
    CHECK(context_json.find("\"core_version\":\"0.6.0\"") != std::string::npos);
    CHECK(context_json.find("\"current_map\":{\"validity\":\"unknown\",\"reason\":\"profile_unrecognized\",\"value\":null") != std::string::npos);
    CHECK(probe(probe_path, args + L" --context", 0).find("not a load serial") != std::string::npos);
    const auto context_watch = probe(probe_path, args + L" --context --watch-count 2 --interval-ms 100 --json", 0);
    CHECK(std::count(context_watch.begin(), context_watch.end(), '\n') == 2);
    probe(probe_path, args + L" --context --engine", 2);
    probe(probe_path, args + L" --context --context", 2);
    std::printf("HARNESS_CONTEXT_JSON %s", context_json.c_str());
    std::printf("HARNESS_CONTEXT_WATCH_JSON %s", context_watch.c_str());
    const auto save = query_save(first.child.pid, 2000);
    const auto write = query_save_write(first.child.pid, 2000);
    CHECK(write.result == ProbeResult::ok && write.snapshot.instance == initial.snapshot.instance && write.write.state == SC_SAVE_WRITE_NONE);
    const auto absent_write = query_save_write(first.child.pid, 2000, UINT64_MAX);
    CHECK(absent_write.result == ProbeResult::ok && absent_write.write.state == SC_SAVE_WRITE_NOT_RETAINED && absent_write.write.operation_id == UINT64_MAX);
    const auto write_json = probe(probe_path, args + L" --save-write --json", 0);
    CHECK(write_json.find("\"operation\":\"save_write\"") != std::string::npos && write_json.find("\"state\":\"none\"") != std::string::npos);
    CHECK(write_json.find("\"native_result\":null") != std::string::npos && write_json.find("\"persistence_verified\":false") != std::string::npos);
    const auto missing_write = probe(probe_path, args + L" --save-write --write-id 18446744073709551615 --json", 0);
    CHECK(missing_write.find("\"state\":\"not_retained\"") != std::string::npos && missing_write.find("\"operation_id\":\"18446744073709551615\"") != std::string::npos);
    const auto write_watch = probe(probe_path, args + L" --save-write --watch-count 2 --interval-ms 100 --json", 0);
    CHECK(std::count(write_watch.begin(), write_watch.end(), '\n') == 2);
    probe(probe_path, args + L" --save-write --save-admission", 2);
    probe(probe_path, args + L" --save-write --save-write", 2);
    probe(probe_path, args + L" --write-id 1", 2);
    probe(probe_path, args + L" --save-write --write-id 0", 2);
    probe(probe_path, args + L" --save-write --write-id 18446744073709551616", 2);
    std::printf("HARNESS_SAVE_WRITE_JSON %s", write_json.c_str());
    const auto admission = query_save_admission(first.child.pid, 2000);
    CHECK(admission.result == ProbeResult::ok && admission.snapshot.instance == initial.snapshot.instance);
    CHECK(admission.admission.size == 160 && admission.admission.state == SC_SAVE_SESSION_DISABLED);
    CHECK(admission.admission.flags == 0 && admission.admission.required_routes == 63);
    const auto admission_json = probe(probe_path, args + L" --save-admission --json", 8);
    CHECK(admission_json.find("\"state\":\"disabled\"") != std::string::npos);
    CHECK(admission_json.find("\"route_retained\":false") != std::string::npos);
    probe(probe_path, args + L" --save-admission --save-context", 2);
    probe(probe_path, args + L" --save-admission --watch-count 2", 2);
    CHECK(save.result == ProbeResult::ok && save.snapshot.instance == initial.snapshot.instance);
    CHECK(save.save.mutation_available == 0 && save.save.mutation_reason == SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN);
    CHECK(save.save.fields[SC_SAVE_NATIVE_COMPLETION].validity == SC_OBSERVATION_UNKNOWN);
    const auto save_json = probe(probe_path, args + L" --save-context --json", 0);
    CHECK(save_json.find("\"operation\":\"save_context\"") != std::string::npos);
    CHECK(save_json.find("native_namespace_route_unproven") != std::string::npos);
    probe(probe_path, args + L" --save-context --context", 2);
    std::printf("HARNESS_SAVE_JSON %s", save_json.c_str());
    const auto watch_started = GetTickCount64();
    const auto watch = probe(probe_path, args + L" --engine --watch-count 2 --interval-ms 100 --json", 0);
    CHECK(std::count(watch.begin(), watch.end(), '\n') == 2 && GetTickCount64() - watch_started < 3000);
    CHECK(GetTickCount64() - watch_started >= 90);
    { // Target exits between capture records; no retries against a replacement process.
        Host exiting(dll, 5);
        std::thread exit_host([&] { Sleep(200); exiting.shutdown(); });
        const auto stopped_watch = probe(probe_path, L"--pid " + std::to_wstring(exiting.child.pid) +
            L" --engine --watch-count 2 --interval-ms 1000 --json", 3);
        exit_host.join();
        CHECK(stopped_watch.find("endpoint_absent") != std::string::npos);
    }
    probe(probe_path, args + L" --watch-count 2 --json", 2);
    probe(probe_path, args + L" --engine --watch-count 601 --json", 2);
    probe(probe_path, args + L" --engine --watch-count 2 --interval-ms 1 --json", 2);
    std::printf("HARNESS_JSON %s", json.c_str());
    std::printf("HARNESS_ENGINE_JSON %s", engine_json.c_str());
    std::printf("HARNESS_WATCH_JSON %s", watch.c_str());
    CHECK(probe(probe_path, L"--help", 0).find("--timeout-ms") != std::string::npos);
    probe(probe_path, L"--pid 0 --json", 2);
    probe(probe_path, L"--pid 1 --timeout-ms 10001 --json", 2);
    permissions(first.child.pid);
    CHECK(query(first.child.pid, 2000, 2).result == ProbeResult::capability_unavailable);
    Message request{};
    auto size = static_cast<DWORD>(encode_request(request, 1, 99));
    exchange(first.child.pid, request, size, WireResult::incompatible_protocol);
    size = static_cast<DWORD>(encode_request(request, 1, wire_version, 99));
    exchange(first.child.pid, request, size, WireResult::unsupported_operation);
    size = static_cast<DWORD>(encode_request(request, 1)); request[0] = 0;
    exchange(first.child.pid, request, size, WireResult::malformed);
    size = static_cast<DWORD>(encode_request(request, 1));
    exchange(first.child.pid, request, size - 1, WireResult::malformed);
    request[8] = 255;
    exchange(first.child.pid, request, size, WireResult::malformed);
    { // Oversize message must disconnect; no reply or allocation proportional to input.
        Handle pipe(open_pipe(first.child.pid)); CHECK(pipe);
        std::array<uint8_t, 513> huge{}; DWORD count = 0;
        transfer(pipe.value, true, huge.data(), static_cast<DWORD>(huge.size()), count, nullptr, 2000);
        CHECK(transfer(pipe.value, false, huge.data(), 1, count, nullptr, 2000) != ERROR_SUCCESS);
    }
    { Handle abandoned(open_pipe(first.child.pid)); CHECK(abandoned); }
    { // A connected client sends no request; admission resumes after its deadline.
        Handle stalled(open_pipe(first.child.pid)); CHECK(stalled);
        const auto start = GetTickCount64();
        CHECK(query(first.child.pid, 150).result == ProbeResult::timeout);
        CHECK(query(first.child.pid, 2500).result == ProbeResult::ok);
        CHECK(GetTickCount64() - start < 3500);
    }
    CHECK(query(first.child.pid, 2000).result == ProbeResult::ok);
    { // Explicit shutdown must cancel an admitted idle read before unloading Core.
        Handle held(open_pipe(first.child.pid)); CHECK(held);
        const auto start = GetTickCount64(); first.shutdown();
        CHECK(GetTickCount64() - start < 6000);
    }
    CHECK(query(first.child.pid, 2000).result == ProbeResult::endpoint_absent);
    Host relaunched(dll, 3);
    const auto fresh = query(relaunched.child.pid, 2000);
    CHECK(fresh.result == ProbeResult::ok && fresh.snapshot.instance != initial.snapshot.instance);
    CHECK(fresh.snapshot.process_created != initial.snapshot.process_created);
    relaunched.shutdown(); second.shutdown();
    Host idle(dll, 4, true);
    CHECK(query(idle.child.pid, 150).result == ProbeResult::endpoint_absent);
    { // A same-user impostor occupying another PID's name is rejected by OS PID.
        Handle imposter(fake_pipe(idle.child.pid)); CHECK(imposter);
        CHECK(query(idle.child.pid, 150).result == ProbeResult::process_mismatch);
    }
    { // Actual denied pipe access to a live task-owned target: never relabel as exit.
        ACL empty{}; CHECK(InitializeAcl(&empty, sizeof(empty), ACL_REVISION));
        SECURITY_DESCRIPTOR descriptor{};
        CHECK(InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION));
        CHECK(SetSecurityDescriptorDacl(&descriptor, TRUE, &empty, FALSE));
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), &descriptor, FALSE};
        Handle denied(CreateNamedPipeW(pipe_name(idle.child.pid).c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, 1000, &attributes));
        CHECK(denied);
        const auto response = query(idle.child.pid, 150);
        CHECK(response.result == ProbeResult::access_denied && response.win32_error == ERROR_ACCESS_DENIED);
        CHECK(std::strcmp(response.failure_stage, "pipe_open") == 0 && std::strcmp(response.target_state, "live") == 0);
        CHECK(response.verified_process_created != 0);
        const auto captured = probe(probe_path, L"--pid " + std::to_wstring(idle.child.pid) + L" --json", 4);
        CHECK(captured.find("\"failure_stage\":\"pipe_open\"") != std::string::npos);
        std::printf("HARNESS_DENIED_JSON %s", captured.c_str());
    }
    idle.shutdown();
    { // Empty explicit DACL distinguishes access denial from endpoint absence.
        ACL acl{}; CHECK(InitializeAcl(&acl, sizeof(acl), ACL_REVISION));
        SECURITY_DESCRIPTOR sd{}; CHECK(InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION));
        CHECK(SetSecurityDescriptorDacl(&sd, TRUE, &acl, FALSE));
        SECURITY_ATTRIBUTES sa{sizeof(sa), &sd, FALSE};
        Handle denied(CreateNamedPipeW(pipe_name(GetCurrentProcessId()).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, 1000, &sa));
        CHECK(denied);
        probe(probe_path, L"--pid " + std::to_wstring(GetCurrentProcessId()) + L" --json", 4);
    }
    for (int i = 0; i < 5; ++i) fake_response(probe_path, initial.snapshot, i);
    fake_response(probe_path, initial.snapshot, 4, true); // Old server's op-1 rejection envelope.
    CHECK(GetModuleHandleW(L"sentinel_core.dll") == nullptr); // Test client never hosts Core.
    std::puts("PASS separate hosts/CLI, OS PID+creation, identity, ACL, reconnect, malformed/capability/version, timeouts, shutdown; HARNESS ONLY");
    return 0;
}
