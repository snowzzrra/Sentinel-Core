#include "sentinel_inspection.h"
#include "pipe_io.h"
#include "protocol.h"
#include <aclapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <algorithm>

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
void fake_response(const std::wstring& probe_path, const Snapshot& snapshot, int variant) {
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
        L" --timeout-ms 150 --json" + (variant == 4 ? L" --engine" : L""), expected[variant]);
    CHECK(!output.empty() && GetTickCount64() - start < 4000);
    server.join();
}
int wmain(int argc, wchar_t** argv) {
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
        }
        CHECK(SetEvent(ready.value));
        CHECK(WaitForSingleObject(stop.value, 30000) == WAIT_OBJECT_0);
        if (core) {
            CHECK(symbol<decltype(&sc_shutdown)>(core, "sc_shutdown")() == SC_OK);
            CHECK(FreeLibrary(core));
        }
        return 0;
    }
    CHECK(argc == 3);
    test_job.value = CreateJobObjectW(nullptr, nullptr); CHECK(test_job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(SetInformationJobObject(test_job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    const std::wstring dll = argv[1], probe_path = argv[2];
    Host first(dll, 1), second(dll, 2);
    const auto initial = query(first.child.pid, 2000);
    if (initial.result != ProbeResult::ok) std::fprintf(stderr, "initial query=%s win32=%u server_pid=%u\n",
        result_name(initial.result), initial.win32_error, initial.server_pid);
    CHECK(initial.result == ProbeResult::ok && initial.server_pid == first.child.pid);
    CHECK(initial.snapshot.pid != GetCurrentProcessId() && initial.snapshot.pid == first.child.pid);
    CHECK(initial.snapshot.core.abi_version == SC_ABI_VERSION && initial.snapshot.core.capabilities == 3);
    CHECK(initial.snapshot.core.state == SC_READY && initial.snapshot.service == ServiceState::listening);
    CHECK(initial.snapshot.core.initialization_count == 1 && std::strlen(initial.snapshot.core.build_id) == 64);
    CHECK(std::strcmp(initial.snapshot.core.version, "0.3.0-phase4.1") == 0);
    uint64_t created = 0; CHECK(process_time(first.child.process.value, created));
    CHECK(initial.snapshot.process_created == created);
    const auto other = query(second.child.pid, 2000);
    CHECK(other.result == ProbeResult::ok && initial.snapshot.instance != other.snapshot.instance);
    for (int i = 0; i < 8; ++i) {
        const auto repeated = query(first.child.pid, 2000);
        CHECK(repeated.result == ProbeResult::ok && repeated.snapshot.instance == initial.snapshot.instance);
    }
    const auto args = L"--pid " + std::to_wstring(first.child.pid) + L" --timeout-ms 2000";
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
    size = static_cast<DWORD>(encode_request(request, 1, wire_version, 9));
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
    CHECK(GetModuleHandleW(L"sentinel_core.dll") == nullptr); // Test client never hosts Core.
    std::puts("PASS separate hosts/CLI, OS PID+creation, identity, ACL, reconnect, malformed/capability/version, timeouts, shutdown; HARNESS ONLY");
    return 0;
}
