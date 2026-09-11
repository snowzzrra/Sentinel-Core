#include "native_test_adapter.h"
#include "native_runtime.h"
#include "inspection_server.h"
#include "pipe_io.h"
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <algorithm>
#include <cwchar>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL native host line %d: %s (win32=%lu)\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
using namespace sentinel;
namespace {
std::atomic<uint32_t> owner{0}, game{SC_GAME_IN_GAME}, command{0};
std::atomic<uintptr_t> caller{0}, map{42};
std::atomic<uint64_t> originals{0}, changes{0}, frees{0}, last_return{0};
std::atomic<bool> pause_frames{false}, finish_thread{false}, pending{false}, recurse{false};
std::atomic<unsigned> gate_stage{0};
std::atomic<unsigned> observation_case{0};
HANDLE parked = nullptr, wake = nullptr, gate_entered = nullptr, gate_release = nullptr, changed = nullptr;
int root_object = 0, common_object = 0, slot_object = 0;
struct Descriptor { uint8_t checkpoint; bool success; uint32_t destination; };
constexpr uint64_t change_return = UINT64_C(0x1234567800000001);

__declspec(noinline) void fixture_frame(uintptr_t self);
__declspec(noinline) void call_frame() { fixture_frame(reinterpret_cast<uintptr_t>(&common_object)); }
__declspec(noinline) void fixture_frame(uintptr_t self) {
    caller.store(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    originals.fetch_add(GetCurrentThreadId() != 0 && self != 0 ? 1 : 0);
    if (recurse.exchange(false)) call_frame();
}
__declspec(noinline) void fixture_free(uintptr_t root, uintptr_t slot) {
    frees.fetch_add(GetCurrentThreadId() != 0 && root != 0 && slot != 0 ? 1 : 0);
    map.store(0);
}
__declspec(noinline) uint64_t fixture_change(uintptr_t root, uintptr_t descriptor, uintptr_t files) {
    const auto* d = reinterpret_cast<const Descriptor*>(descriptor);
    changes.fetch_add(GetCurrentThreadId() != 0 && files == 0 ? 1 : 0);
    game.store(SC_GAME_LOADING);
    fixture_free(root, reinterpret_cast<uintptr_t>(&slot_object));
    map.store(d->success ? 42 : 0); // Intentional exact same address/name fixture.
    game.store(d->success ? d->destination : SC_GAME_MAIN_MENU);
    return d->success ? change_return : 0;
}
sc_context_snapshot facts() {
    sc_context_snapshot c{}; c.size = sizeof(c); c.abi_version = SC_CONTEXT_ABI_VERSION;
    c.sampled_at_ms = GetTickCount64();
    c.fields[SC_CONTEXT_GAME_STATE] = {SC_OBSERVATION_OBSERVED, SC_REASON_NONE, game.load(), 0};
    if (game.load() == SC_GAME_IN_GAME && map.load()) {
        c.current_map.validity = SC_OBSERVATION_OBSERVED;
        strcpy_s(c.current_map.bytes, "fixture/same-name-and-address");
        c.current_map.length = static_cast<uint32_t>(std::strlen(c.current_map.bytes));
    }
    return c;
}
// Real production double-read/budget function on explicit fake memory. These
// fixtures are not native semantic proof and no controls enter production IPC.
sc_context_snapshot observe(context::Evidence& measured) {
    const auto mode = observation_case.load();
    struct Memory final : engine::Memory {
        unsigned mode, state_reads = 0;
        explicit Memory(unsigned value) : mode(value) {}
        engine::ReadResult copy(uintptr_t address, void* out, size_t size) override {
            constexpr uintptr_t root = 0x1445ea6f0ULL, object = 0x200000000ULL, data = 0x300000000ULL;
            const char name[] = "fixture/same-name-and-address";
            const uintptr_t root_table = 0x142aaa730ULL, map_table = 0x142ab30c8ULL;
            uint32_t state[2]{SC_GAME_IN_GAME, 12};
            struct String { uintptr_t table, data; int32_t length; uint32_t allocation; };
            const String text{0x142a67478ULL, data, static_cast<int32_t>(sizeof(name) - 1), 4096};
            const void* source = nullptr; size_t width = 0;
            if (address == root) { source = &root_table; width = sizeof(root_table); }
            if (address == root + 0x44) {
                if (mode == 6 && ++state_reads == 2) state[0] = SC_GAME_LOADING;
                source = state; width = sizeof(state);
            }
            if (address == root + 0x50) { source = &object; width = sizeof(object); }
            if (address == object) { source = &map_table; width = sizeof(map_table); }
            if (address == object + 0x9a060) { source = &text; width = sizeof(text); }
            if (address == data) {
                if (mode == 5) return {SC_REASON_PARTIAL_READ, ERROR_PARTIAL_COPY};
                source = name; width = sizeof(name);
            }
            if (!source || size != width) return {SC_REASON_READ_FAILED, ERROR_NOACCESS};
            std::memcpy(out, source, size); return {};
        }
    } memory(mode);
    engine::Binding b{}; b.image.base = 0x140000000ULL; b.root = 0x1445ea6f0ULL;
    b.metadata.profile = SC_PROFILE_STEAM_20260818;
    static thread_local int64_t ticks = 0, delta = 0;
    ticks = 0; delta = mode == 3 ? 2000001 : (mode == 4 ? 15000000 : 100000);
    const context::Clock clock{
        [](int64_t& out) { out = ticks; ticks += delta; return true; },
        []() -> uint64_t { return GetTickCount64(); }, 1000000000};
    auto c = context::sample(memory, b, 1, nullptr, 2, &measured, mode ? &clock : nullptr);
    if (mode == 8) pending.store(true);
    return c;
}
void gate(bool executed) {
    const unsigned expected = executed ? 2u : 1u;
    if (gate_stage.load() != expected) return;
    SetEvent(gate_entered);
    CHECK(WaitForSingleObject(gate_release, 5000) == WAIT_OBJECT_0);
    gate_stage.store(0);
}
template<class Predicate> void until_at(int line, const char* stage, Predicate predicate, uint32_t ms = 4000) {
    const auto deadline = GetTickCount64() + ms;
    while (!predicate()) {
        if (GetTickCount64() >= deadline) {
            const auto n = native::inspect();
            std::fprintf(stderr, "TIMEOUT native host line %d: %s; availability=%u reason=%u hooks=%u "
                "lifecycle=%u generation=%llu context=%llu context_reason=%u gaps=%llu callbacks=%llu "
                "originals=%llu command=%u paused=%u owner=%u callback_thread=%u\n",
                line, stage, n.availability, n.reason, n.installed_hooks, n.lifecycle,
                n.scope.lifecycle_generation, n.context_generation, n.context_reason,
                n.event_gap_count, n.callback_sequence, originals.load(), command.load(),
                pause_frames.load() ? 1u : 0u, owner.load(), n.callback_thread_id);
            std::exit(1);
        }
        Sleep(1);
    }
}
#define until(...) until_at(__LINE__, #__VA_ARGS__, __VA_ARGS__)
void park() { pause_frames.store(true); CHECK(WaitForSingleObject(parked, 4000) == WAIT_OBJECT_0); }
void resume() { pause_frames.store(false); SetEvent(wake); }
void transition(uint32_t action) {
    ResetEvent(changed); command.store(action); resume();
    // inspect() owns the native history lock. Polling it while this fixture
    // drives a lifecycle callback can intentionally produce EVENT_GAP, before
    // the readiness assertion gets a chance to observe the completed event.
    CHECK(WaitForSingleObject(changed, 4000) == WAIT_OBJECT_0);
}
void ready_at(int line) { until_at(line, "ready: published lifecycle context", [] {
    const auto n = native::inspect(); return n.context_generation && !n.context_reason;
}); }
#define ready() ready_at(__LINE__)
struct Child {
    Handle process, output;
    Child(const wchar_t* probe, const std::wstring& args) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; Handle writer;
        CHECK(CreatePipe(&output.value, &writer.value, &sa, 32768));
        CHECK(SetHandleInformation(output.value, HANDLE_FLAG_INHERIT, 0));
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = writer.value; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        std::wstring line = L"\"" + std::wstring(probe) + L"\" --pid " + std::to_wstring(GetCurrentProcessId()) + L" --json " + args;
        PROCESS_INFORMATION pi{};
        CHECK(CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
        process.value = pi.hProcess; CloseHandle(pi.hThread);
    }
    std::string complete(DWORD expected) {
        CHECK(WaitForSingleObject(process.value, 8000) == WAIT_OBJECT_0);
        DWORD code = 0; CHECK(GetExitCodeProcess(process.value, &code) && code == expected);
        std::string text; char bytes[4096]; DWORD count = 0;
        while (ReadFile(output.value, bytes, sizeof(bytes), &count, nullptr) && count) text.append(bytes, count);
        CHECK(text.size() < 32768);
        std::fwrite(text.data(), 1, text.size(), stdout); // Retained JSONL in test log.
        return text;
    }
};
sc_diagnostic_request request(uint64_t id) {
    sc_diagnostic_request r{}; r.expected = native::inspect().scope;
    r.request_id = id; r.nonce[0] = 17; r.deadline_ms = 5000; return r;
}
std::wstring retrieve_args(const sc_diagnostic_request& r, bool cancel) {
    std::array<uint8_t, 16> nonce{}, instance{};
    std::copy(std::begin(r.nonce), std::end(r.nonce), nonce.begin());
    std::copy(std::begin(r.expected.instance_id), std::end(r.expected.instance_id), instance.begin());
    const auto n = instance_text(nonce), i = instance_text(instance);
    return std::wstring(cancel ? L"--diagnostic-cancel" : L"--diagnostic-result") + L" --request-id " +
        std::to_wstring(r.request_id) + L" --nonce " + std::wstring(n.begin(), n.end()) + L" --expect-created " +
        std::to_wstring(r.expected.process_created) + L" --expect-instance " + std::wstring(i.begin(), i.end()) +
        L" --expect-generation " + std::to_wstring(r.expected.lifecycle_generation);
}
}
int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 3); const wchar_t* probe = argv[1];
    parked = CreateEventW(nullptr, TRUE, FALSE, nullptr); wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gate_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); gate_release = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    changed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(parked && wake && gate_entered && gate_release && changed);
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)); CHECK(stop);
    CHECK(sc_initialize(SC_ABI_VERSION, SC_CAP_INSPECTION | SC_CAP_LIFECYCLE) == SC_OK);
    until([] { return native::inspect().availability == SC_NATIVE_DISABLED; });
    CHECK(native::inspect().reason == SC_NATIVE_UNKNOWN_BUILD && !native::inspect().installed_hooks);
    auto old = query_context(GetCurrentProcessId(), 2000); CHECK(old.result == ProbeResult::ok && old.context.profile == SC_PROFILE_NONE);
    Child unknown(probe, L"--native"); CHECK(unknown.complete(0).find("unknown_build") != std::string::npos);
    std::thread callback([] {
        owner.store(GetCurrentThreadId());
        while (!finish_thread.load()) {
            if (pause_frames.load()) {
                SetEvent(parked); WaitForSingleObject(wake, 5000); ResetEvent(parked); continue;
            }
            const auto action = command.exchange(0);
            if (action) {
                Descriptor descriptor{static_cast<uint8_t>(action == 1 ? 2 : 0), action != 2,
                                      static_cast<uint32_t>(action == 3 ? SC_GAME_MAIN_MENU : SC_GAME_IN_GAME)};
                last_return.store(fixture_change(reinterpret_cast<uintptr_t>(&root_object), reinterpret_cast<uintptr_t>(&descriptor), 0));
                SetEvent(changed);
            }
            call_frame(); Sleep(2); // Only this task-owned host synthesizes frames.
        }
    });
    until([] { return caller.load() && originals.load(); }); park();
    native::TestAdapter adapter{};
    adapter.root = reinterpret_cast<uintptr_t>(&root_object); adapter.common = reinterpret_cast<uintptr_t>(&common_object);
    adapter.caller = caller.load();
    adapter.owner = [] { return owner.load(); }; adapter.game = [] { return game.load(); };
    adapter.pending = [] { return static_cast<uint8_t>(pending.load()); }; adapter.context = facts; adapter.observe = observe;
    adapter.map = [] { return map.load(); };
    adapter.checkpoint = [](uintptr_t p, uint8_t& value) { value = reinterpret_cast<const Descriptor*>(p)->checkpoint; return true; };
    adapter.primary = [](uintptr_t p, uintptr_t& value) { value = map.load(); return p == reinterpret_cast<uintptr_t>(&slot_object); };
    adapter.gate = gate;
    const uintptr_t addresses[] = {reinterpret_cast<uintptr_t>(fixture_frame), reinterpret_cast<uintptr_t>(fixture_change), reinterpret_cast<uintptr_t>(fixture_free)};
    engine::LocalMemory memory; engine::Image image;
    CHECK(engine::read_image(memory, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), image).reason == SC_REASON_NONE);
    for (unsigned i = 0; i < 3; ++i) {
        adapter.targets[i].address = addresses[i];
        CHECK(memory.copy(addresses[i], adapter.targets[i].bytes.data(), 32).reason == SC_REASON_NONE);
        CHECK(native::validate_target(memory, image, adapter.targets[i], nullptr, GetTickCount64() + 3000) == SC_NATIVE_NONE);
    }
    auto distinct = adapter.targets[0]; distinct.signature_offset = 1;
    CHECK(memory.copy(distinct.address + 1, distinct.signature.data(), 32).reason == SC_REASON_NONE);
    CHECK(native::validate_target(memory, image, distinct, nullptr, GetTickCount64() + 3000) == SC_NATIVE_NONE);
    distinct.signature[0] ^= 1;
    CHECK(native::validate_target(memory, image, distinct, nullptr, GetTickCount64() + 3000) == SC_NATIVE_TARGET_BYTES);
    distinct.signature_offset = UINT32_MAX;
    CHECK(native::validate_target(memory, image, distinct, nullptr, GetTickCount64() + 3000) == SC_NATIVE_TARGET_BOUNDARY);
    // Foreign entry redirection in task-owned code only; parked callback cannot
    // enter it. Validator rejects it without following/overwriting the jump.
    DWORD protect = 0, ignored = 0;
    CHECK(VirtualProtect(reinterpret_cast<void*>(addresses[0]), 32, PAGE_EXECUTE_READWRITE, &protect));
    *reinterpret_cast<uint8_t*>(addresses[0]) = 0xe9;
    CHECK(native::validate_target(memory, image, adapter.targets[0], nullptr, GetTickCount64() + 3000) == SC_NATIVE_TARGET_BYTES);
    std::memcpy(reinterpret_cast<void*>(addresses[0]), adapter.targets[0].bytes.data(), 32);
    CHECK(VirtualProtect(reinterpret_cast<void*>(addresses[0]), 32, protect, &ignored));
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addresses[0]), 32);
    auto interior = adapter.targets[0]; ++interior.address;
    CHECK(memory.copy(interior.address, interior.bytes.data(), 32).reason == SC_REASON_NONE);
    CHECK(native::validate_target(memory, image, interior, nullptr, GetTickCount64() + 3000) == SC_NATIVE_TARGET_BOUNDARY);
    native::test_start(adapter, current_snapshot(), stop.value);
    CHECK(native::inspect().availability == SC_NATIVE_ENABLED && native::inspect().installed_hooks == 7);
    CHECK(native::inspect().lifecycle == SC_LIFETIME_UNOBSERVED && !native::inspect().context_generation);
    transition(1); ready();
    CHECK(last_return.load() == change_return && changes.load() == 1 && frees.load() == 1);
    CHECK(native::inspect().scope.lifecycle_generation == 1 && native::inspect().checkpoint_flag);
    Child normal(probe, L"--diagnostic --deadline-ms 1500");
    const auto successful = normal.complete(0);
    CHECK(successful.find("callback_executed") != std::string::npos);
    CHECK(native::inspect().callback_thread_id == owner.load() && owner.load() != GetCurrentThreadId());
    if (std::wcscmp(argv[2], L"basic") == 0) {
        park(); Child stale(probe, L"--diagnostic --deadline-ms 1500");
        until([] { return native::inspect().queued == 1; });
        transition(1);
        CHECK(stale.complete(8).find("scope_mismatch") != std::string::npos); ready();
        CHECK(native::inspect().scope.lifecycle_generation == 2);
        Child fresh(probe, L"--diagnostic --deadline-ms 1500");
        CHECK(fresh.complete(0).find("callback_executed") != std::string::npos);
        park(); Child expired(probe, L"--diagnostic --deadline-ms 150");
        CHECK(expired.complete(8).find("\"state\":\"expired\"") != std::string::npos);
        resume(); ready();
        for (unsigned stage = 1; stage <= 2; ++stage) {
            park(); auto r = request(100 + stage);
            auto admitted = query_diagnostic(GetCurrentProcessId(), 2000, diagnostic_submit_operation, r);
            CHECK(admitted.result == ProbeResult::ok && admitted.diagnostic.state == SC_DIAGNOSTIC_QUEUED);
            auto duplicate = query_diagnostic(GetCurrentProcessId(), 2000, diagnostic_submit_operation, r);
            CHECK(duplicate.diagnostic.admitted_at_ms == admitted.diagnostic.admitted_at_ms);
            ResetEvent(gate_entered); gate_stage.store(stage); resume();
            CHECK(WaitForSingleObject(gate_entered, 4000) == WAIT_OBJECT_0);
            const auto start = GetTickCount64(); CHECK(query(GetCurrentProcessId(), 500).result == ProbeResult::ok);
            CHECK(GetTickCount64() - start < 500); // Sole IPC worker remains responsive.
            Child cancel(probe, retrieve_args(r, true));
            const auto cancellation = cancel.complete(8);
            CHECK(cancellation.find("\"state\":\"claimed\"") != std::string::npos && cancellation.find("\"cancel_requested\":true") != std::string::npos);
            SetEvent(gate_release);
            until([&] { return native::result(r, false).state != SC_DIAGNOSTIC_CLAIMED; });
            auto completed = query_diagnostic(GetCurrentProcessId(), 2000, diagnostic_result_operation, r);
            CHECK(completed.result == ProbeResult::ok && completed.diagnostic.cancel_requested);
            CHECK(completed.diagnostic.state == static_cast<uint32_t>(stage == 1 ? SC_DIAGNOSTIC_CANCELLED : SC_DIAGNOSTIC_EXECUTED));
        }
        ready(); park(); pending.store(true);
        auto r = request(200); CHECK(query_diagnostic(GetCurrentProcessId(), 2000, diagnostic_submit_operation, r).diagnostic.state == SC_DIAGNOSTIC_QUEUED);
        resume(); until([&] { return native::result(r, false).state >= SC_DIAGNOSTIC_EXECUTED; });
        CHECK(native::result(r, false).reason == SC_NATIVE_TRANSITION); pending.store(false);
        park(); transition(2); until([] { return native::inspect().lifecycle == SC_LIFETIME_FAILED; });
        CHECK(!native::inspect().context_generation && last_return.load() == 0);
        park(); transition(3); until([] { return native::inspect().lifecycle == SC_LIFETIME_MENU; });
        CHECK(!native::inspect().context_generation);
        park(); transition(1); ready();
        recurse.store(true); until([] { return native::inspect().reason == SC_NATIVE_REENTRANT; });
    } else if (std::wcscmp(argv[2], L"wrong-thread") == 0) {
        for (const unsigned mode : {1u, 3u, 4u, 5u, 6u, 8u}) {
            park(); observation_case.store(mode);
            const auto req = request(300 + mode);
            const auto admitted = query_diagnostic(GetCurrentProcessId(), 2000, diagnostic_detail_submit_operation, req);
            CHECK(admitted.diagnostic.state == SC_DIAGNOSTIC_QUEUED);
            resume(); until([&] { return native::result(req, false).state >= SC_DIAGNOSTIC_EXECUTED; });
            const auto out = query_diagnostic(GetCurrentProcessId(), 2000, diagnostic_detail_result_operation, req);
            CHECK(out.result == ProbeResult::ok && out.detail.revision == 1 && out.detail.observation_attempted);
            CHECK(out.detail.timing_valid && out.detail.observation_budget_ns == 2000000);
            CHECK(out.detail.observation_elapsed_ns == (mode == 3 ? 2000001u : (mode == 4 ? 15000000u : 100000u)));
            if (mode == 1) CHECK(out.diagnostic.state == SC_DIAGNOSTIC_EXECUTED && out.detail.observation_accepted);
            else {
                CHECK(out.diagnostic.state == SC_DIAGNOSTIC_REJECTED && !out.diagnostic.current_map.length && !out.diagnostic.observed_at_ms);
                if (mode == 3 || mode == 4) CHECK(out.diagnostic.reason == SC_NATIVE_BUDGET && out.detail.stage == SC_STAGE_OBSERVATION_BUDGET);
                if (mode == 5) CHECK(out.detail.map_reason == SC_REASON_PARTIAL_READ && out.detail.map_error == ERROR_PARTIAL_COPY);
                if (mode == 6) CHECK(out.detail.state_reason == SC_REASON_TRANSITION && out.detail.stage == SC_STAGE_FRESH_CONTEXT);
                if (mode == 8) CHECK(out.detail.stage == SC_STAGE_PENDING_AFTER && out.detail.pending_before == 1 && out.detail.pending_after == 2);
            }
            Child captured(probe, retrieve_args(req, false));
            CHECK(captured.complete(mode == 1 ? 0 : 8).find("\"observation_attempted\":true") != std::string::npos);
            const auto again = query_diagnostic(GetCurrentProcessId(), 2000, diagnostic_detail_result_operation, req);
            CHECK(std::memcmp(&out.detail, &again.detail, sizeof(out.detail)) == 0);
            observation_case.store(0); pending.store(false); ready();
        }
        park(); owner.store(0); resume(); until([] { return native::inspect().reason == SC_NATIVE_WRONG_THREAD; });
    } else {
        park();
        const auto generation = native::inspect().scope.lifecycle_generation;
        if (std::wcscmp(argv[2], L"unobserved-map") == 0) map.store(43);
        else game.store(SC_GAME_LOADING);
        resume(); until([] { return native::inspect().reason == SC_NATIVE_EVENT_GAP; });
        CHECK(native::inspect().scope.lifecycle_generation == generation); // Observation cannot mint a generation.
    }
    CHECK(!native::inspect().context_generation && native::inspect().event_gap_count);
    const auto original_count = originals.load();
    sc_native_snapshot exact{};
    sc_save_admission_snapshot admission_status{};
    CHECK(sc_save_admission_inspect(SC_SAVE_ADMISSION_ABI_VERSION, sizeof(admission_status), &admission_status) == SC_OK);
    CHECK(admission_status.size == 160 && admission_status.state == SC_SAVE_SESSION_DISABLED);
    const auto installation_query = query_save_installation(GetCurrentProcessId(), 1000);
    CHECK(installation_query.result == ProbeResult::ok && installation_query.installation.attempt == 1);
    Child installation_probe(probe, L"--save-installation");
    const auto installation_json = installation_probe.complete(0);
    CHECK(installation_json.find("\"operation\":\"save_installation\"") != std::string::npos &&
          installation_json.find("\"primary_failure\":{") != std::string::npos && installation_json.find("GetTickCount64") != std::string::npos);
    CHECK(installation_query.installation.primary_failure.sequence); // Initial unknown-build refusal remains queryable.
    sc_save_installation_snapshot installation_status{};
    CHECK(sc_save_installation_inspect(1, sizeof(installation_status), &installation_status) == SC_OK);
    CHECK(installation_status.primary_failure.sequence == installation_query.installation.primary_failure.sequence);
    CHECK(sc_save_installation_inspect(2, sizeof(installation_status), &installation_status) == SC_ABI_MISMATCH);
    CHECK(sc_save_installation_inspect(1, sizeof(installation_status)-1, &installation_status) == SC_INVALID_ARGUMENT);
    CHECK(sc_save_admission_inspect(SC_SAVE_ADMISSION_ABI_VERSION + 1, sizeof(admission_status), &admission_status) == SC_ABI_MISMATCH);
    CHECK(sc_save_admission_inspect(SC_SAVE_ADMISSION_ABI_VERSION, sizeof(admission_status) - 1, &admission_status) == SC_INVALID_ARGUMENT);
    CHECK(sc_save_admission_inspect(SC_SAVE_ADMISSION_ABI_VERSION, sizeof(admission_status), nullptr) == SC_INVALID_ARGUMENT);
    CHECK(sc_native_inspect(SC_NATIVE_ABI_VERSION, sizeof(exact), &exact) == SC_OK);
    CHECK(sc_native_inspect(SC_NATIVE_ABI_VERSION + 1, sizeof(exact), &exact) == SC_ABI_MISMATCH);
    CHECK(sc_native_inspect(SC_NATIVE_ABI_VERSION, sizeof(exact) - 1, &exact) == SC_INVALID_ARGUMENT);
    const auto identity = current_snapshot();
    inspection_failed(ERROR_GEN_FAILURE);
    CHECK(sc_initialize(SC_ABI_VERSION, 0) == SC_UNLOAD_RETAINED);
    CHECK(current_snapshot().instance == identity.instance); // A retained service failure cannot rebind live hooks.
    CHECK(sc_shutdown() == SC_UNLOAD_RETAINED && sc_shutdown() == SC_UNLOAD_RETAINED);
    CHECK(native::retained() && sc_initialize(SC_ABI_VERSION, 0) == SC_UNLOAD_RETAINED);
    CHECK(native::inspect().availability == SC_NATIVE_RETAINED);
    call_frame(); CHECK(originals.load() > original_count); // Still valid pass-through trampolines after shutdown.
    CHECK(query(GetCurrentProcessId(), 500).result == ProbeResult::endpoint_absent);
    finish_thread.store(true); resume(); callback.join();
    CloseHandle(parked); CloseHandle(wake); CloseHandle(gate_entered); CloseHandle(gate_release); CloseHandle(changed);
    std::puts("PASS actual MinHook attach/original/trampoline, task-owned callback thread, separate probe/IPC, stale scope and cancellation states, retained shutdown; HARNESS ONLY, NOT DOOM SEMANTICS");
}
