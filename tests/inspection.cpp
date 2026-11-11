#include "sentinel_inspection.h"
#include "sentinel_version.h"
#include "pipe_io.h"
#include "protocol_v2.h"
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
#include "wire_fixtures.h"
#include <numeric>
#include <utility>

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
    DWORD wait(DWORD timeout = 60000) {
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
        CHECK(ready);
        CHECK(stop);
        HANDLE events[] = {ready.value, child.process.value};
        CHECK(WaitForMultipleObjects(2, events, FALSE, 60000) == WAIT_OBJECT_0);
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
    const auto name = pipe_name_v2(pid);
    const auto deadline = GetTickCount64() + 30000;
    for (;;) {
        HANDLE pipe = CreateFileW(name.c_str(), access, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY) return pipe;
        CHECK(remaining(deadline) != 0);
        CHECK(WaitNamedPipeW(name.c_str(), remaining(deadline)));
    }
}

void permissions(DWORD pid) {
    Handle pipe(open_pipe(pid, client_access | READ_CONTROL)); CHECK(pipe);
    PACL dacl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    CHECK(GetSecurityInfo(pipe.value, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                          &dacl, nullptr, &descriptor) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    CHECK(GetSecurityDescriptorControl(descriptor, &control, &revision));
    CHECK((control & SE_DACL_PROTECTED) != 0);
    CHECK(dacl);
    CHECK(dacl->AceCount == 1);
    void* raw = nullptr; CHECK(GetAce(dacl, 0, &raw));
    const auto ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
    CHECK(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
    CHECK(ace->Mask == client_access);
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
    CHECK(!excessive);
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
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
        out << "sentinel-test-session-v3\nseed_hex=61646d697373696f6e2d66697874757265\nteam=0\nslot=1\n"
            "generation_fingerprint=" << std::string(64, 'a') <<
            "\nslot_data_revision=0.7.0\nsession_mod_contract_revision=2\nprovenance=synthetic-fixture\nroot=" << root.u8string() << '\n';
        out.close(); CHECK(out.good());
    }
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", fixture_file.c_str()));
    Host prepared(dll, 30);
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", nullptr));
    const auto status = query_save_admission(prepared.child.pid, 2000);
    CHECK(status.result == ProbeResult::ok);
    CHECK(status.admission.state == SC_SAVE_SESSION_PREPARED);
    CHECK(status.admission.namespace_id == namespace_id);
    CHECK(status.admission.flags == 0);
    storage::Metadata metadata;
    CHECK(storage::inspect(descriptor, metadata).outcome == storage::Outcome::ownership_conflict);
    const auto capture = probe(probe_path, L"--pid " + std::to_wstring(prepared.child.pid) + L" --save-admission --json", 8);
    CHECK(capture.find("\"state\":\"prepared\"") != std::string::npos);
    prepared.shutdown(); // child checks kept module, sticky refusal, and no reinitialization
    CHECK(storage::reopen(descriptor, lease).ok()); lease.reset();
    { std::ofstream out(fixture_file, std::ios::binary | std::ios::trunc); out << "invalid-fixture\n"; }
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", fixture_file.c_str()));
    Host invalid(dll, 31);
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", nullptr));
    const auto rejected = query_save_admission(invalid.child.pid, 2000);
    CHECK(rejected.result == ProbeResult::ok);
    CHECK(rejected.admission.state == SC_SAVE_SESSION_REJECTED);
    CHECK(rejected.admission.fault == 1);
    CHECK(rejected.admission.namespace_id[0] == 0);
    invalid.shutdown();
    CHECK(fs::canonical(root).parent_path() == parent);
    CHECK(root.filename().u8string().find("sentinel-admission-") == 0);
    fs::remove_all(root);
    std::puts("PASS production prelaunch reader/lease, DLL retention before hooks, sticky shutdown and probe refusal; synthetic hosts only");
}
generated::wire_header_2 v2_fake_read(HANDLE pipe, generated::Bytes& body) {
    generated::Bytes bytes(32); generated::wire_header_2 header;
    CHECK(transfer_exact(pipe, false, bytes.data(), 32, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    CHECK(v2::header(bytes, header, false)); body.resize(header.payload_length);
    CHECK(transfer_exact(pipe, false, body.data(), header.payload_length, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS); return header;
}
void v2_fake_accept(HANDLE pipe) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr)); CHECK(event);
    OVERLAPPED ov{}; ov.hEvent = event.value;
    const auto ok = ConnectNamedPipe(pipe, &ov); const auto error = ok ? ERROR_SUCCESS : GetLastError(); DWORD count = 0;
    CHECK(error == ERROR_PIPE_CONNECTED || finish_io(pipe, ov, ok, error, nullptr, 2000, count) == ERROR_SUCCESS);
}
void v2_fake_server(HANDLE pipe, unsigned mode, generated::hello_result hello) {
    v2_fake_accept(pipe); generated::Bytes body, bytes;
    auto header = v2_fake_read(pipe, body); header.flags = 1;
    if (mode == 5) ++hello.scope.process_created;
    CHECK(generated::encode(hello, body)); CHECK(v2::frame(header, body, bytes));
    const auto deadline = GetTickCount64() + 30000;
    for (auto& byte : bytes) CHECK(transfer_exact(pipe, true, &byte, 1, nullptr, deadline) == ERROR_SUCCESS);
    if (mode != 5) {
        header = v2_fake_read(pipe, body); header.flags = 1;
        generated::command_reference reference; CHECK(generated::decode(body, reference));
        generated::command_reply reply{hello.scope.instance_id, reference, {3,2,4,2,{}}};
        if (mode == 1) ++reply.actual_instance_id[0];
        if (mode == 2) reply.reference.command_id = "other";
        if (mode == 3) ++header.correlation_id;
        if (mode == 4) ++reply.reference.scope.process_created;
        CHECK(generated::encode(generated::command_response{reply, 4, {}}, body)); CHECK(v2::frame(header, body, bytes));
        for (auto& byte : bytes) CHECK(transfer_exact(pipe, true, &byte, 1, nullptr, deadline) == ERROR_SUCCESS);
    }
    DWORD count = 0; uint8_t byte = 0;
    const auto error = transfer(pipe, false, &byte, 1, count, nullptr, 2000);
    CHECK(error == ERROR_BROKEN_PIPE || (error == ERROR_SUCCESS && !count));
}
void v2_client_identity() {
    for (unsigned mode = 0; mode < 6; ++mode) {
        Handle server(CreateNamedPipeW(pipe_name_v2(GetCurrentProcessId()).c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 4, 16384, 16384, 1000, nullptr)); CHECK(server);
        generated::hello_result hello{}; hello.scope.pid = GetCurrentProcessId();
        CHECK(process_time(GetCurrentProcess(), hello.scope.process_created)); hello.scope.instance_id[0] = 1;
        hello.max_frame = 16384; hello.execution_slots = 8; hello.full_result_ms = 30000; hello.identity_capacity = 65536;
        std::thread worker([&] { v2_fake_server(server.value, mode, hello); });
        V2Connection connection; const auto connected = connection.connect(GetCurrentProcessId(), 2000);
        if (mode == 5) CHECK(connected == ProbeResult::process_mismatch);
        else {
            CHECK(connected == ProbeResult::ok);
            generated::command_reference reference{hello.scope, {}, "identity", 5, 1, {}}; reference.namespace_id.fill('0');
            generated::Bytes body; CHECK(generated::encode(reference, body));
            const auto result = connection.exchange(3, body, 2000);
            CHECK(result.transport == (mode ? ProbeResult::invalid_response : ProbeResult::ok));
        }
        connection.close(); worker.join();
    }
}
void v2_fairness(DWORD pid) {
    Handle first(open_pipe(pid, client_access)), second(open_pipe(pid, client_access)), third(open_pipe(pid, client_access));
    CHECK(first);
    CHECK(second);
    CHECK(third);
    V2Connection active;
    CHECK(active.connect(pid, 1000) == ProbeResult::ok);
    CHECK(active.exchange(6, {}, 1000).transport == ProbeResult::ok);
    V2Connection excess;
    CHECK(excess.connect(pid, 100) == ProbeResult::timeout);
    CHECK(active.exchange(6, {}, 1000).transport == ProbeResult::ok);
}
void v2_partial_deadline(DWORD pid, bool header_only = false) {
    Handle pipe(open_pipe(pid, client_access)); CHECK(pipe);
    generated::wire_header_2 header{generated::wire_magic, 2, 2, 100, 0, 1, 0, 0};
    generated::Bytes bytes; CHECK(generated::encode(header, bytes));
    const auto begin = GetTickCount64();
    CHECK(transfer_exact(pipe.value, true, bytes.data(), header_only ? 1u : 32u, nullptr, begin + 1000) == ERROR_SUCCESS);
    for (unsigned fragments = 0; fragments < 30; ++fragments) {
        Sleep(100);
        DWORD count = 0;
        const auto error = transfer(pipe.value, true, bytes.data(), 1, count, nullptr, 30000);
        if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA) return;
        CHECK(error == ERROR_SUCCESS);
    }
    CHECK(false);
}
void v2_shutdown_io(DWORD pid, Host& host) {
    Handle idle(open_pipe(pid, client_access)), partial(open_pipe(pid, client_access)), blocked(open_pipe(pid, client_access));
    CHECK(idle);
    CHECK(partial);
    CHECK(blocked);
    uint8_t byte = 0x53;
    CHECK(transfer_exact(partial.value, true, &byte, 1, nullptr, GetTickCount64() + 1000) == ERROR_SUCCESS);
    generated::Bytes frame, bytes;
    CHECK(v2::frame({generated::wire_magic, 2, 6, 0, 0, 1, 0, 0}, {}, frame));
    for (unsigned i = 0; i < 200; ++i) bytes.insert(bytes.end(), frame.begin(), frame.end());
    CHECK(transfer_exact(blocked.value, true, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    const auto start = GetTickCount64(); host.shutdown();
    CHECK(GetTickCount64() - start < 60000);
}
void watch_target_exit(const std::wstring& dll, const std::wstring& probe_path) {
    Host exiting(dll, 5);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle read, writer;
    CHECK(CreatePipe(&read.value, &writer.value, &sa, 16384));
    CHECK(SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0));
    Child watcher(quote(probe_path) + L" --pid " + std::to_wstring(exiting.child.pid) +
        L" --engine --watch-count 2 --interval-ms 1000 --json", writer.value);
    CloseHandle(writer.value); writer.value = nullptr;
    std::string stopped_watch;
    char byte = 0; DWORD count = 0;
    do {
        CHECK(ReadFile(read.value, &byte, 1, &count, nullptr));
        CHECK(count == 1);
        stopped_watch += byte;
    } while (byte != '\n');
    CHECK(stopped_watch.find("\"result\":\"ok\"") != std::string::npos);
    exiting.shutdown();
    CHECK(watcher.wait() == 3);
    char buffer[16384]{};
    CHECK(ReadFile(read.value, buffer, sizeof(buffer), &count, nullptr));
    stopped_watch.append(buffer, count);
    CHECK(stopped_watch.find("endpoint_absent") != std::string::npos);
}
auto next_engine_sample(DWORD pid, uint64_t sequence) {
    auto result = query_engine(pid, 2000);
    const auto deadline = GetTickCount64() + 30000;
    while (result.result == ProbeResult::ok && result.engine.sequence <= sequence) {
        CHECK(GetTickCount64() < deadline);
        result = query_engine(pid, 2000);
    }
    return result;
}
int inspection_host(int, wchar_t** argv) {
        const bool idle = wcscmp(argv[1], L"--idle") == 0;
        const std::wstring prefix = argv[3];
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (prefix + L".ready").c_str()));
        Handle stop(OpenEventW(SYNCHRONIZE, FALSE, (prefix + L".stop").c_str())); CHECK(ready);
        CHECK(stop);
        HMODULE core = nullptr;
        if (!idle) {
            core = LoadLibraryExW(argv[2], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32); CHECK(core);
            CHECK(symbol<decltype(&sc_initialize)>(core, "sc_initialize")(SC_ABI_VERSION, 0) == SC_OK);
            const auto inspect_context = symbol<decltype(&sc_context_inspect)>(core, "sc_context_inspect");
            sc_context_snapshot context{};
            CHECK(inspect_context(2, sizeof(context), &context) == SC_ABI_MISMATCH);
            CHECK(inspect_context(1, sizeof(context) - 1, &context) == SC_INVALID_ARGUMENT);
            CHECK(inspect_context(1, sizeof(context), nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_context(1, sizeof(context), &context) == SC_OK);
            CHECK(context.pid == GetCurrentProcessId());
            const auto inspect_save = symbol<decltype(&sc_save_inspect)>(core, "sc_save_inspect");
            sc_save_snapshot save{};
            CHECK(inspect_save(2, sizeof(save), &save) == SC_ABI_MISMATCH);
            CHECK(inspect_save(1, sizeof(save) - 1, &save) == SC_INVALID_ARGUMENT);
            CHECK(inspect_save(1, sizeof(save), nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_save(1, sizeof(save), &save) == SC_OK);
            CHECK(save.pid == GetCurrentProcessId());
            CHECK(save.mutation_available == 0);
            CHECK(save.mutation_reason == SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN);
            const auto inspect_admission = symbol<decltype(&sc_save_admission_inspect)>(core, "sc_save_admission_inspect");
            sc_save_admission_snapshot admission{};
            CHECK(inspect_admission(2, sizeof(admission), &admission) == SC_ABI_MISMATCH);
            CHECK(inspect_admission(1, sizeof(admission) - 1, &admission) == SC_INVALID_ARGUMENT);
            CHECK(inspect_admission(1, sizeof(admission), nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_admission(1, sizeof(admission), &admission) == SC_OK);
            CHECK(admission.size == 160);
            const auto inspect_write = symbol<decltype(&sc_save_write_inspect)>(core, "sc_save_write_inspect");
            sc_save_write_snapshot write{};
            CHECK(inspect_write(2, sizeof(write), 0, &write) == SC_ABI_MISMATCH);
            CHECK(inspect_write(1, sizeof(write) - 1, 0, &write) == SC_INVALID_ARGUMENT);
            CHECK(inspect_write(1, sizeof(write), 0, nullptr) == SC_INVALID_ARGUMENT);
            CHECK(inspect_write(1, sizeof(write), 0, &write) == SC_OK);
            CHECK(write.size == 160);
            CHECK(write.state == SC_SAVE_WRITE_NONE);
            CHECK(inspect_write(1, sizeof(write), UINT64_MAX, &write) == SC_OK);
            CHECK(write.state == SC_SAVE_WRITE_NOT_RETAINED);
            CHECK(write.operation_id == UINT64_MAX);
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
                CHECK(admission.state == SC_SAVE_SESSION_REJECTED);
                CHECK(admission.fault == 6);
                CHECK(symbol<decltype(&sc_initialize)>(core, "sc_initialize")(SC_ABI_VERSION, 0) == SC_UNLOAD_RETAINED);
            }
        }
        return 0;
}
int wmain(int argc, wchar_t** argv) {
    executable = argv[0];
    if (argc == 4 && (wcscmp(argv[1], L"--host") == 0 || wcscmp(argv[1], L"--idle") == 0)) {
        return inspection_host(argc, argv);
    }
    CHECK(argc == 3 || (argc == 4 && wcscmp(argv[3], L"--prelaunch") == 0));
    CHECK(SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", nullptr));
    test_job.value = CreateJobObjectW(nullptr, nullptr); CHECK(test_job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(SetInformationJobObject(test_job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    const std::wstring dll = argv[1], probe_path = argv[2];
    CHECK(query(0, 2000).result == ProbeResult::usage);
    CHECK(query(GetCurrentProcessId(), min_timeout_ms - 1).result == ProbeResult::usage);
    if (argc == 4) { prelaunch_contract(dll, probe_path); return 0; }
    Host first(dll, 1), second(dll, 2);
    const auto initial = query(first.child.pid, 2000);
    if (initial.result != ProbeResult::ok) std::fprintf(stderr, "initial query=%s win32=%u server_pid=%u\n",
        result_name(initial.result), initial.win32_error, initial.server_pid);
    CHECK(initial.result == ProbeResult::ok);
    CHECK(initial.server_pid == first.child.pid);
    CHECK(initial.snapshot.pid != GetCurrentProcessId());
    CHECK(initial.snapshot.pid == first.child.pid);
    CHECK(initial.snapshot.core.abi_version == SC_ABI_VERSION);
    CHECK(initial.snapshot.core.capabilities == 3);
    CHECK(initial.snapshot.core.state == SC_READY);
    CHECK(initial.snapshot.core.state == SC_READY);
    CHECK(initial.snapshot.core.initialization_count == 1);
    CHECK(std::strlen(initial.snapshot.core.build_id) == 64);
    CHECK(std::strcmp(initial.snapshot.core.version, SC_PRODUCT_VERSION) == 0);
    uint64_t created = 0; CHECK(process_time(first.child.process.value, created));
    CHECK(initial.snapshot.process_created == created);
    const auto other = query(second.child.pid, 2000);
    CHECK(other.result == ProbeResult::ok);
    CHECK(initial.snapshot.instance != other.snapshot.instance);
    for (int i = 0; i < 8; ++i) {
        const auto repeated = query(first.child.pid, 2000);
        CHECK(repeated.result == ProbeResult::ok);
        CHECK(repeated.snapshot.instance == initial.snapshot.instance);
    }
    const auto args = L"--pid " + std::to_wstring(first.child.pid) + L" --timeout-ms 2000";
    sc_save_backup_request backup{};
    backup.execution.expected.pid = first.child.pid; backup.execution.expected.process_created = created;
    std::memcpy(backup.execution.expected.instance_id, initial.snapshot.instance.data(), 16);
    backup.execution.expected.lifecycle_generation = 1; backup.execution.request_id = UINT64_MAX;
    backup.execution.nonce[0] = 42; backup.execution.deadline_ms = 1000; backup.work_deadline_ms = 10000;
    std::memset(backup.namespace_id, 'a', 64);
    const auto rejected_backup = query_save_backup(first.child.pid, 2000, save_backup_submit_operation, backup);
    CHECK(rejected_backup.result == ProbeResult::ok);
    CHECK(rejected_backup.backup.state == SC_BACKUP_REJECTED);
    CHECK(!(rejected_backup.backup.flags & SC_BACKUP_NATIVE_ENTERED));
    CHECK(query_save_backup(first.child.pid, 2000, save_backup_result_operation, backup).backup.state == SC_BACKUP_REJECTED);
    CHECK(query_save_backup(first.child.pid, 2000, save_backup_cancel_operation, backup).backup.state == SC_BACKUP_REJECTED);
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
    CHECK(unknown_backup.find("\"state\":\"rejected\"") != std::string::npos);
    const auto readable = probe(probe_path, args, 0);
    CHECK(readable.find("OS server verified, non_game_host") != std::string::npos);
    const auto json = probe(probe_path, args + L" --json", 0);
    CHECK(json.find("\"result\":\"ok\"") != std::string::npos);
    CHECK(json.find("\"host_kind\":\"non_game_host\"") != std::string::npos);
    CHECK(json.find("\"engine_integration\":\"unavailable\"") != std::string::npos);
    CHECK(json.find(instance_text(initial.snapshot.instance)) != std::string::npos);
    auto engine = query_engine(first.child.pid, 2000);
    const auto observe_deadline = GetTickCount64() + 30000;
    while (engine.result == ProbeResult::ok && engine.engine.pe_reason == SC_REASON_NOT_SAMPLED && GetTickCount64() < observe_deadline) {
        CHECK(query(first.child.pid, 150).result == ProbeResult::ok);
        Sleep(100); engine = query_engine(first.child.pid, 2000);
    }
    CHECK(engine.result == ProbeResult::ok);
    CHECK(engine.snapshot.instance == initial.snapshot.instance);
    CHECK(engine.engine.pe_reason == SC_REASON_NONE);
    CHECK(engine.engine.profile == SC_PROFILE_NONE);
    CHECK(engine.engine.root_locator_reason == SC_REASON_SIGNATURE_MISSING);
    CHECK(engine.engine.fields[SC_ENGINE_PLAYER_PRESENT].validity == SC_OBSERVATION_UNKNOWN);
    const auto fresh_engine = next_engine_sample(first.child.pid, engine.engine.sequence);
    CHECK(fresh_engine.result == ProbeResult::ok);
    CHECK(fresh_engine.engine.sequence > engine.engine.sequence);
    CHECK(query(first.child.pid, 2000).result == ProbeResult::ok);
    const auto engine_json = probe(probe_path, args + L" --engine --json", 0);
    CHECK(engine_json.find("\"operation\":\"engine\"") != std::string::npos);
    CHECK(engine_json.find("\"profile\":\"unrecognized\"") != std::string::npos);
    CHECK(engine_json.find("\"value\":null") != std::string::npos);
    const auto context = query_context(first.child.pid, 2000);
    CHECK(context.result == ProbeResult::ok);
    CHECK(context.snapshot.instance == initial.snapshot.instance);
    CHECK(context.context.current_map.reason == SC_REASON_PROFILE_UNRECOGNIZED);
    CHECK(context.context.fields[SC_CONTEXT_LOAD_SERIAL].reason == SC_CONTEXT_UNSUPPORTED);
    sc_combat_query combat_query{}; std::memset(combat_query.namespace_id, 'a', 64);
    combat_query.map_count = 1; strcpy_s(combat_query.maps[0], "game/sp/hellonearth/hellonearth");
    const auto combat = query_combat(first.child.pid, 2000, combat_query);
    CHECK(combat.result == ProbeResult::ok);
    CHECK(combat.snapshot.instance == initial.snapshot.instance);
    CHECK(combat.combat.abi_version == SC_COMBAT_ABI_VERSION);
    CHECK(combat.combat.scope.pid == first.child.pid);
    CHECK(combat.combat.scope.process_created == initial.snapshot.process_created);
    CHECK(std::memcmp(combat.combat.scope.instance_id, initial.snapshot.instance.data(), 16) == 0);
    CHECK(combat.combat.qualified_kills == 0);
    CHECK(!std::strcmp(combat.combat.namespace_id, combat_query.namespace_id));
    const auto context_json = probe(probe_path, args + L" --context --json", 0);
    CHECK(context_json.find("\"operation\":\"context\"") != std::string::npos);
    CHECK(context_json.find("\"core_version\":\"" SC_PRODUCT_VERSION "\"") != std::string::npos);
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
    CHECK(write.result == ProbeResult::ok);
    CHECK(write.snapshot.instance == initial.snapshot.instance);
    CHECK(write.write.state == SC_SAVE_WRITE_NONE);
    const auto absent_write = query_save_write(first.child.pid, 2000, UINT64_MAX);
    CHECK(absent_write.result == ProbeResult::ok);
    CHECK(absent_write.write.state == SC_SAVE_WRITE_NOT_RETAINED);
    CHECK(absent_write.write.operation_id == UINT64_MAX);
    const auto write_json = probe(probe_path, args + L" --save-write --json", 0);
    CHECK(write_json.find("\"operation\":\"save_write\"") != std::string::npos);
    CHECK(write_json.find("\"state\":\"none\"") != std::string::npos);
    CHECK(write_json.find("\"native_result\":null") != std::string::npos);
    CHECK(write_json.find("\"persistence_verified\":false") != std::string::npos);
    const auto missing_write = probe(probe_path, args + L" --save-write --write-id 18446744073709551615 --json", 0);
    CHECK(missing_write.find("\"state\":\"not_retained\"") != std::string::npos);
    CHECK(missing_write.find("\"operation_id\":\"18446744073709551615\"") != std::string::npos);
    const auto write_watch = probe(probe_path, args + L" --save-write --watch-count 2 --interval-ms 100 --json", 0);
    CHECK(std::count(write_watch.begin(), write_watch.end(), '\n') == 2);
    probe(probe_path, args + L" --save-write --save-admission", 2);
    probe(probe_path, args + L" --save-write --save-write", 2);
    probe(probe_path, args + L" --write-id 1", 2);
    probe(probe_path, args + L" --save-write --write-id 0", 2);
    probe(probe_path, args + L" --save-write --write-id 18446744073709551616", 2);
    std::printf("HARNESS_SAVE_WRITE_JSON %s", write_json.c_str());
    const auto admission = query_save_admission(first.child.pid, 2000);
    CHECK(admission.result == ProbeResult::ok);
    CHECK(admission.snapshot.instance == initial.snapshot.instance);
    CHECK(admission.admission.size == 160);
    CHECK(admission.admission.state == SC_SAVE_SESSION_DISABLED);
    CHECK(admission.admission.flags == 0);
    CHECK(admission.admission.required_routes == 63);
    const auto admission_json = probe(probe_path, args + L" --save-admission --json", 8);
    CHECK(admission_json.find("\"state\":\"disabled\"") != std::string::npos);
    CHECK(admission_json.find("\"route_retained\":false") != std::string::npos);
    probe(probe_path, args + L" --save-admission --save-context", 2);
    probe(probe_path, args + L" --save-admission --watch-count 2", 2);
    CHECK(save.result == ProbeResult::ok);
    CHECK(save.snapshot.instance == initial.snapshot.instance);
    CHECK(save.save.mutation_available == 0);
    CHECK(save.save.mutation_reason == SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN);
    CHECK(save.save.fields[SC_SAVE_NATIVE_COMPLETION].validity == SC_OBSERVATION_UNKNOWN);
    const auto save_json = probe(probe_path, args + L" --save-context --json", 0);
    CHECK(save_json.find("\"operation\":\"save_context\"") != std::string::npos);
    CHECK(save_json.find("native_namespace_route_unproven") != std::string::npos);
    probe(probe_path, args + L" --save-context --context", 2);
    std::printf("HARNESS_SAVE_JSON %s", save_json.c_str());
    const auto watch_started = GetTickCount64();
    const auto watch = probe(probe_path, args + L" --engine --watch-count 2 --interval-ms 100 --json", 0);
    CHECK(std::count(watch.begin(), watch.end(), '\n') == 2);
    CHECK(GetTickCount64() - watch_started < 30000);
    CHECK(GetTickCount64() - watch_started >= 90);
    watch_target_exit(dll, probe_path);
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
    v2_fairness(first.child.pid);
    v2_partial_deadline(first.child.pid);
    v2_partial_deadline(first.child.pid, true);
    CHECK(query(first.child.pid, 2000, 2).result == ProbeResult::capability_unavailable);
    CHECK(query(first.child.pid, 2000).result == ProbeResult::ok);
    { // explicit shutdown must cancel an admitted idle read before unloading core
        const auto start = GetTickCount64(); v2_shutdown_io(first.child.pid, first);
        CHECK(GetTickCount64() - start < 60000);
    }
    CHECK(query(first.child.pid, 2000).result == ProbeResult::endpoint_absent);
    Host relaunched(dll, 3);
    const auto fresh = query(relaunched.child.pid, 2000);
    CHECK(fresh.result == ProbeResult::ok);
    CHECK(fresh.snapshot.instance != initial.snapshot.instance);
    CHECK(fresh.snapshot.process_created != initial.snapshot.process_created);
    relaunched.shutdown(); second.shutdown();
    Host idle(dll, 4, true);
    CHECK(query(idle.child.pid, 150).result == ProbeResult::endpoint_absent);
    {
        Handle imposter(CreateNamedPipeW(pipe_name_v2(idle.child.pid).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 4, 16384, 16384, 1000, nullptr)); CHECK(imposter);
        V2Connection connection; CHECK(connection.connect(idle.child.pid, 150) == ProbeResult::process_mismatch);
    }
    { // real denied pipe access to a live task-owned target: never relabel as exit
        ACL empty{}; CHECK(InitializeAcl(&empty, sizeof(empty), ACL_REVISION));
        SECURITY_DESCRIPTOR descriptor{};
        CHECK(InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION));
        CHECK(SetSecurityDescriptorDacl(&descriptor, TRUE, &empty, FALSE));
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), &descriptor, FALSE};
        Handle denied(CreateNamedPipeW(pipe_name_v2(idle.child.pid).c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, 1000, &attributes));
        CHECK(denied);
        const auto response = query(idle.child.pid, 150);
        CHECK(response.result == ProbeResult::access_denied);
        CHECK(response.win32_error == ERROR_ACCESS_DENIED);
        CHECK(std::strcmp(response.failure_stage, "pipe_open") == 0);
        CHECK(std::strcmp(response.target_state, "live") == 0);
        CHECK(response.verified_process_created != 0);
        const auto captured = probe(probe_path, L"--pid " + std::to_wstring(idle.child.pid) + L" --json", 4);
        CHECK(captured.find("\"failure_stage\":\"pipe_open\"") != std::string::npos);
        std::printf("HARNESS_DENIED_JSON %s", captured.c_str());
    }
    idle.shutdown();
    { // empty explicit dacl distinguishes access denial from endpoint absence
        ACL acl{}; CHECK(InitializeAcl(&acl, sizeof(acl), ACL_REVISION));
        SECURITY_DESCRIPTOR sd{}; CHECK(InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION));
        CHECK(SetSecurityDescriptorDacl(&sd, TRUE, &acl, FALSE));
        SECURITY_ATTRIBUTES sa{sizeof(sa), &sd, FALSE};
        Handle denied(CreateNamedPipeW(pipe_name_v2(GetCurrentProcessId()).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, 1000, &sa));
        CHECK(denied);
        probe(probe_path, L"--pid " + std::to_wstring(GetCurrentProcessId()) + L" --json", 4);
    }
    v2_client_identity();
    CHECK(GetModuleHandleW(L"sentinel_core.dll") == nullptr); // test client never hosts core
    std::puts("PASS separate hosts/CLI, OS PID+creation, identity, ACL, reconnect, malformed/capability/version, timeouts, shutdown; HARNESS ONLY");
    return 0;
}
