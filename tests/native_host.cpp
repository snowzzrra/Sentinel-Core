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
HANDLE parked = nullptr, wake = nullptr, gate_entered = nullptr, gate_release = nullptr;
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
void gate(bool executed) {
    const unsigned expected = executed ? 2u : 1u;
    if (gate_stage.load() != expected) return;
    SetEvent(gate_entered);
    CHECK(WaitForSingleObject(gate_release, 5000) == WAIT_OBJECT_0);
    gate_stage.store(0);
}
template<class Predicate> void until(Predicate predicate, uint32_t ms = 4000) {
    const auto deadline = GetTickCount64() + ms;
    while (!predicate()) { CHECK(GetTickCount64() < deadline); Sleep(1); }
}
void park() { pause_frames.store(true); CHECK(WaitForSingleObject(parked, 4000) == WAIT_OBJECT_0); }
void resume() { pause_frames.store(false); SetEvent(wake); }
void ready() { until([] { const auto n = native::inspect(); return n.context_generation && !n.context_reason; }); }
struct Child {
    Handle process, output;
    Child(const wchar_t* probe, const std::wstring& args) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; Handle writer;
        CHECK(CreatePipe(&output.value, &writer.value, &sa, 0));
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
    CHECK(argc == 3); const wchar_t* probe = argv[1]; const bool wrong_thread = std::wcscmp(argv[2], L"wrong-thread") == 0;
    parked = CreateEventW(nullptr, TRUE, FALSE, nullptr); wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gate_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); gate_release = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK(parked && wake && gate_entered && gate_release);
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
            }
            call_frame(); Sleep(2); // Only this task-owned host synthesizes frames.
        }
    });
    until([] { return caller.load() && originals.load(); }); park();
    native::TestAdapter adapter{};
    adapter.root = reinterpret_cast<uintptr_t>(&root_object); adapter.common = reinterpret_cast<uintptr_t>(&common_object);
    adapter.caller = caller.load();
    adapter.owner = [] { return owner.load(); }; adapter.game = [] { return game.load(); };
    adapter.pending = [] { return static_cast<uint8_t>(pending.load()); }; adapter.context = facts;
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
    command.store(1); resume(); ready();
    CHECK(last_return.load() == change_return && changes.load() == 1 && frees.load() == 1);
    CHECK(native::inspect().scope.lifecycle_generation == 1 && native::inspect().checkpoint_flag);
    Child normal(probe, L"--diagnostic --deadline-ms 1500");
    const auto successful = normal.complete(0);
    CHECK(successful.find("callback_executed") != std::string::npos);
    CHECK(native::inspect().callback_thread_id == owner.load() && owner.load() != GetCurrentThreadId());
    if (!wrong_thread) {
        park(); Child stale(probe, L"--diagnostic --deadline-ms 1500");
        until([] { return native::inspect().queued == 1; });
        command.store(1); resume();
        CHECK(stale.complete(8).find("scope_mismatch") != std::string::npos); ready();
        CHECK(native::inspect().scope.lifecycle_generation == 2);
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
        park(); command.store(2); resume(); until([] { return native::inspect().lifecycle == SC_LIFETIME_FAILED; });
        CHECK(!native::inspect().context_generation && last_return.load() == 0);
        park(); command.store(3); resume(); until([] { return native::inspect().lifecycle == SC_LIFETIME_MENU; });
        CHECK(!native::inspect().context_generation);
        park(); command.store(1); resume(); ready();
        recurse.store(true); until([] { return native::inspect().reason == SC_NATIVE_REENTRANT; });
    } else if (std::wcscmp(argv[2], L"wrong-thread") == 0) {
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
    CloseHandle(parked); CloseHandle(wake); CloseHandle(gate_entered); CloseHandle(gate_release);
    std::puts("PASS actual MinHook attach/original/trampoline, task-owned callback thread, separate probe/IPC, stale scope and cancellation states, retained shutdown; HARNESS ONLY, NOT DOOM SEMANTICS");
}
