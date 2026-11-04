#include "engine/hooks/hook_set.h"
#include "save_startup_gate.h"
#include "save_hook_call.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL hook_set:%d: %s\n",__LINE__,#x); std::abort(); } } while (0)
using namespace sentinel::hooks;
namespace {
struct Fake {
    unsigned creates = 0, enables = 0, disables = 0, removes = 0;
    unsigned fail_create = 0, fail_enable = 0, fail_disable = 0, fail_remove = 0;
    unsigned cancel_create = 0, cancel_enable = 0;
    bool already_enabled = false;
    bool already_disabled = false;
    std::array<bool, 3> owned{}, enabled{}, foreign{};
    std::vector<size_t> removed;
    HANDLE stop = nullptr, enable_entered = nullptr, enable_release = nullptr;
} fake;
int original() { return 7; }
int detour() { return 9; }
size_t index(void* target) { return reinterpret_cast<uintptr_t>(target) - 1; }
MH_STATUS WINAPI create(void* target, void*, void** out) {
    const auto i = index(target);
    if (++fake.creates == fake.fail_create) { fake.foreign[i] = true; return MH_ERROR_ALREADY_CREATED; }
    CHECK(!fake.owned[i] && !fake.foreign[i]);
    fake.owned[i] = true; *out = reinterpret_cast<void*>(original);
    if (fake.creates == fake.cancel_create) CHECK(SetEvent(fake.stop));
    return MH_OK;
}
MH_STATUS WINAPI enable(void* target) {
    if (fake.enable_entered) {
        CHECK(SetEvent(fake.enable_entered));
        CHECK(WaitForSingleObject(fake.enable_release, 5000) == WAIT_OBJECT_0);
    }
    if (++fake.enables == fake.fail_enable) {
        if (fake.already_enabled) { fake.enabled[index(target)] = true; return MH_ERROR_ENABLED; }
        return MH_ERROR_MEMORY_PROTECT;
    }
    fake.enabled[index(target)] = true;
    if (fake.enables == fake.cancel_enable) CHECK(SetEvent(fake.stop));
    return MH_OK;
}
MH_STATUS WINAPI disable(void* target) {
    if (++fake.disables == fake.fail_disable) return MH_ERROR_MEMORY_PROTECT;
    fake.enabled[index(target)] = false; return fake.already_disabled ? MH_ERROR_DISABLED : MH_OK;
}
MH_STATUS WINAPI remove(void* target) {
    const auto i = index(target);
    CHECK(fake.owned[i] && !fake.enabled[i] && !fake.foreign[i]);
    if (++fake.removes == fake.fail_remove) return MH_ERROR_MEMORY_PROTECT;
    fake.owned[i] = false; fake.removed.push_back(i); return MH_OK;
}
const Api api{create, enable, disable, remove};
void faulting_entry(HookSet& hooks) {
    const auto lease = hooks.enter(0);
    __try { RaiseException(0xe0421234, 0, 0, nullptr); }
    __finally { hooks.leave(lease); }
}
bool catches_original_fault(HookSet& hooks) {
    __try { faulting_entry(hooks); }
    __except(GetExceptionCode() == 0xe0421234 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
void runtime_transaction() {
    sentinel::save::StartupGate cancelled;
    cancelled.cancel();
    CHECK(cancelled.arm() && cancelled.wait().state == sentinel::save::StartupGate::State::cancelled);
    fake = {};
    HookSet existing("existing", api), first("first", api), second("second", api);
    int (*a)() = nullptr, (*b)() = nullptr, (*c)() = nullptr;
    const HookSpec before[]{bind(1, detour, a)}, one[]{bind(2, detour, b)}, two[]{bind(3, detour, c)};
    CHECK(existing.install(before));
    {
        Transaction attempt;
        CHECK(existing.install(before) && first.install(one));
        fake.fail_create = 3;
        CHECK(!second.install(two));
        CHECK(attempt.rollback());
        CHECK(existing.inspect().state == State::installed && first.inspect().state == State::empty);
        CHECK(fake.owned[0] && !fake.owned[1] && fake.foreign[2]);
    }
    CHECK(existing.uninstall());
    fake = {};
    {
        Transaction attempt;
        CHECK(first.install(one) && second.install(two));
        fake.fail_remove = 1;
        CHECK(!attempt.rollback());
        CHECK(second.inspect().state == State::cleanup_failed && first.inspect().state == State::empty);
        CHECK(fake.removed == std::vector<size_t>{1});
    }
    CHECK(second.uninstall());
    fake = {};
    {
        Transaction attempt;
        CHECK(first.install(one));
        attempt.commit();
    }
    CHECK(first.inspect().state == State::installed && first.uninstall());
    {
        Transaction attempt;
        CHECK(first.install(one));
    }
    CHECK(first.inspect().state == State::empty);
    for (bool cancel : {false, true}) {
        fake = {};
        sentinel::save::StartupGate gate;
        CHECK(gate.arm());
        Transaction attempt;
        CHECK(first.install(one));
        std::atomic<sentinel::save::StartupGate::State> result{sentinel::save::StartupGate::State::pending};
        std::thread waiter([&] {
            const auto lease = first.enter(0);
            result = gate.wait().state;
            first.leave(lease);
        });
        const auto deadline = GetTickCount64() + 5000;
        while (!gate.waiting() && GetTickCount64() < deadline) SwitchToThread();
        CHECK(gate.waiting() == 1);
        if (cancel) gate.cancel();
        else { sentinel::save::StartupGate::Completion completion(gate); }
        CHECK(attempt.rollback());
        waiter.join();
        const auto expected = cancel ? sentinel::save::StartupGate::State::cancelled : sentinel::save::StartupGate::State::failed;
        CHECK(result == expected && first.inspect().state == State::empty);
        { sentinel::save::StartupGate::Completion late(gate); late.ready(); }
        CHECK(gate.wait().state == expected);
    }
}
void nested_transaction() {
    fake = {};
    HookSet first("first", api), second("second", api);
    int (*a)() = nullptr, (*b)() = nullptr;
    const HookSpec one[]{bind(1, detour, a)}, two[]{bind(2, detour, b)};
    Transaction outer;
    CHECK(first.install(one));
    {
        Transaction inner;
        CHECK(second.install(two));
        inner.commit();
    }
    CHECK(outer.rollback());
    CHECK(first.inspect().state == State::empty && second.inspect().state == State::empty);
    CHECK(fake.removed == std::vector<size_t>({1, 0}));
}
void failures() {
    for (unsigned nth = 1; nth <= 3; ++nth) for (bool at_enable : {false, true}) {
        fake = {};
        fake.fail_create = at_enable ? 0 : nth; fake.fail_enable = at_enable ? nth : 0;
        HookSet hooks("failure", api);
        int (*a)() = nullptr, (*b)() = nullptr, (*c)() = nullptr;
        const HookSpec specs[]{bind(1, detour, a), bind(2, detour, b), bind(3, detour, c)};
        CHECK(!hooks.install(specs));
        const auto state = hooks.inspect();
        CHECK(state.state == State::empty && state.first_error.site == nth - 1);
        CHECK(state.first_error.operation == (at_enable ? Operation::enable : Operation::create));
        CHECK(!a && !b && !c);
        CHECK(fake.foreign[nth - 1] == !at_enable);
        CHECK(!fake.owned[0] && !fake.owned[1] && !fake.owned[2]);
        for (size_t i = 1; i < fake.removed.size(); ++i) CHECK(fake.removed[i - 1] > fake.removed[i]);
        CHECK(hooks.uninstall());
    }
    fake = {}; fake.fail_enable = 1; fake.already_enabled = true;
    HookSet hooks("enabled_conflict", api);
    int (*fn)() = nullptr;
    const HookSpec specs[]{bind(1, detour, fn)};
    CHECK(!hooks.install(specs) && hooks.inspect().state == State::empty);
    CHECK(fake.disables == 1 && !fake.owned[0] && !fn);
}
void cleanup_errors() {
    for (bool at_remove : {false, true}) {
        fake = {};
        HookSet hooks("cleanup", api);
        int (*a)() = nullptr, (*b)() = nullptr, (*c)() = nullptr;
        const HookSpec specs[]{bind(1, detour, a), bind(2, detour, b), bind(3, detour, c)};
        CHECK(hooks.install(specs)); CHECK(hooks.install(specs)); CHECK(fake.creates == 3);
        CHECK(catches_original_fault(hooks) && hooks.inspect().sites[0].entries == 0);
        if (at_remove) fake.fail_remove = 1; else fake.fail_disable = 1;
        CHECK(!hooks.uninstall());
        const auto state = hooks.inspect();
        CHECK(state.state == State::cleanup_failed && state.cleanup_errors.size() == 1);
        CHECK(state.cleanup_errors[0].operation == (at_remove ? Operation::remove : Operation::disable));
        CHECK(state.sites[2].created && c && !a && !b);
        CHECK(!hooks.install(specs));
        const auto lease = hooks.enter(2);
        CHECK(!lease.committed && lease.original == (at_remove ? reinterpret_cast<void*>(3) : reinterpret_cast<void*>(original)));
        hooks.leave(lease);
        CHECK(hooks.uninstall()); CHECK(hooks.uninstall()); CHECK(!c);
    }
}
void cancellation() {
    for (unsigned phase = 0; phase < 3; ++phase) for (unsigned nth = 1; nth <= 3; ++nth) {
        fake = {}; fake.stop = CreateEventW(nullptr, TRUE, phase == 0, nullptr); CHECK(fake.stop);
        if (phase == 1) fake.cancel_create = nth;
        if (phase == 2) fake.cancel_enable = nth;
        HookSet hooks("cancel", api);
        int (*a)() = nullptr, (*b)() = nullptr, (*c)() = nullptr;
        const HookSpec specs[]{bind(1, detour, a), bind(2, detour, b), bind(3, detour, c)};
        CHECK(!hooks.install(specs, fake.stop));
        CHECK(hooks.inspect().state == State::empty && !a && !b && !c);
        CHECK(hooks.inspect().first_error.operation == Operation::cancel);
        CHECK(CloseHandle(fake.stop));
    }
}
void optional_sites_and_history() {
    fake = {};
    sentinel::save::Installation record;
    HookSet hooks("optional", api, &record, 6);
    int (*a)() = nullptr, (*b)() = nullptr;
    HookSpec specs[]{bind(1, detour, a), bind(2, detour, b)};
    specs[0].id = 11; specs[0].rva = 0x1234;
    specs[1].selected = false;
    CHECK(hooks.install(specs));
    const auto skipped = hooks.enter(1);
    CHECK(!skipped.counted && !skipped.committed && skipped.original == reinterpret_cast<void*>(2));
    hooks.leave(skipped);
    fake.already_disabled = true;
    CHECK(hooks.uninstall());
    CHECK(record.inspect().cleanup_failures == 0);
    fake.already_disabled = false;
    CHECK(record.inspect().created == 1 && record.inspect().enabled == 1);
    CHECK(record.inspect().active.target_group == 6 && record.inspect().active.target_index == 11);
    CHECK(record.inspect().active.rva == 0x1234);
    specs[1].selected = true;
    CHECK(hooks.install(specs));
    CHECK(record.inspect().created == 3 && record.inspect().enabled == 3);
    CHECK(hooks.uninstall());
    specs[0].target = 3;
    CHECK(!hooks.install(specs));
    const auto delayed = hooks.enter(0);
    CHECK(delayed.original == reinterpret_cast<void*>(1) && !delayed.committed);
    hooks.leave(delayed);
}
void entries() {
    for (bool rollback : {false, true}) {
        fake = {};
        fake.enable_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        fake.enable_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        CHECK(fake.enable_entered && fake.enable_release);
        if (rollback) fake.fail_enable = 1;
        HookSet hooks("entries", api);
        int (*fn)() = nullptr, (*second)() = nullptr;
        const HookSpec specs[]{bind(1, detour, fn), bind(2, detour, second)};
        std::thread installer([&] { CHECK(hooks.install(specs) != rollback); });
        CHECK(WaitForSingleObject(fake.enable_entered, 5000) == WAIT_OBJECT_0);
        std::atomic<bool> entered{false};
        std::thread early([&] {
            const auto lease = hooks.enter(0);
            CHECK(lease.committed != rollback);
            CHECK(lease.original == (rollback ? reinterpret_cast<void*>(1) : reinterpret_cast<void*>(original)));
            hooks.leave(lease); entered = true;
        });
        CHECK(!entered.load()); CHECK(SetEvent(fake.enable_release));
        installer.join(); early.join();
        if (!rollback) {
            const auto held = hooks.enter(0);
            CHECK(held.counted && held.committed);
            std::atomic<bool> removed{false};
            std::thread teardown([&] { CHECK(hooks.uninstall()); removed = true; });
            while (hooks.inspect().sites[0].enabled) SwitchToThread();
            CHECK(!removed.load());
            CHECK(hooks.inspect().sites[1].created);
            const auto late = hooks.enter(0);
            CHECK(!late.counted && !late.committed && late.original == reinterpret_cast<void*>(1));
            hooks.leave(late);
            CHECK(reinterpret_cast<int(*)()>(held.original)() == 7);
            hooks.leave(held); teardown.join(); CHECK(removed.load());
        }
        CHECK(hooks.uninstall());
        const auto delayed = hooks.enter(0);
        CHECK(!delayed.counted && delayed.original == reinterpret_cast<void*>(1)); hooks.leave(delayed);
        CHECK(CloseHandle(fake.enable_entered) && CloseHandle(fake.enable_release));
    }
}
HookSet real("real");
int (*real_original)() = nullptr;
bool fail_original = false;
unsigned original_calls = 0;
int real_function() {
    ++original_calls;
    if (fail_original) RaiseException(0xe0421234, 0, 0, nullptr);
    return 7;
}
int real_detour() {
    const auto lease = real.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<int(*)()>(lease.original)();
        return reinterpret_cast<int(*)()>(lease.original)() + 2;
    }
    __finally { real.leave(lease); }
}
bool catches_real_fault(int (*call)()) {
    __try { call(); }
    __except(GetExceptionCode() == 0xe0421234 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
void real_hooks() {
    CHECK(MH_Initialize() == MH_OK);
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)); CHECK(code);
    uint8_t bytes[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    const auto function = reinterpret_cast<uintptr_t>(real_function);
    std::memcpy(bytes + 2, &function, sizeof(function));
    std::memcpy(code, bytes, sizeof(bytes));
    DWORD prior = 0; CHECK(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &prior));
    CHECK(FlushInstructionCache(GetCurrentProcess(), code, 4096));
    const auto call = reinterpret_cast<int(*)()>(code);
    const HookSpec specs[]{bind(reinterpret_cast<uintptr_t>(code), real_detour, real_original)};
    CHECK(call() == 7 && real.install(specs) && call() == 9);
    const auto deferred = sentinel::save::HookOriginal<real, 0, decltype(real_original)>::call;
    CHECK(deferred() == 7);
    fail_original = true;
    CHECK(catches_real_fault(call) && real.inspect().sites[0].entries == 0);
    CHECK(catches_real_fault(deferred) && real.inspect().sites[0].entries == 0);
    fail_original = false;
    CHECK(real.uninstall() && call() == 7 && !real_original);
    CHECK(deferred() == 7);
    const auto before = original_calls;
    CHECK(real_detour() == 7 && original_calls == before + 1);
    CHECK(!std::memcmp(code, bytes, sizeof(bytes)));
    CHECK(real.install(specs) && call() == 9 && real.uninstall());
    CHECK(MH_Uninitialize() == MH_OK); CHECK(VirtualFree(code, 0, MEM_RELEASE));
}
}
int main() {
    runtime_transaction();
    nested_transaction();
    failures();
    cleanup_errors();
    cancellation();
    optional_sites_and_history();
    entries();
    real_hooks();
    std::puts("PASS hook ownership, rollback, retention, cancellation, concurrent entries and symmetric removal");
}
