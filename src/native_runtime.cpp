#include "native_runtime.h"
#include "native_target.h"
#include "save_native_hooks.h"
#include "save_session.h"
#include "context_observer.h"
#include "MinHook.h"
#include <intrin.h>
#include <cstring>
#include <cstdio>
#ifdef SC_NATIVE_TESTING
#include "native_test_adapter.h"
#endif

namespace sentinel::native {
namespace {
// Sole owner of native hooks, their lifecycle history and diagnostic slots.
// No core/server lock is acquired from here, including from detours.
SRWLOCK lock = SRWLOCK_INIT;
SRWLOCK startup = SRWLOCK_INIT;
Lifecycle lifetime;
Diagnostics diagnostics;
sc_native_snapshot status{};
// Pointer/name changes invalidate certainty. They never create a generation.
uint64_t bound_generation = 0;
uintptr_t bound_map_address = 0;
sc_context_map bound_map_name{};
engine::Binding binding;
std::atomic<uint64_t> epoch{0}, gaps{0};
std::atomic<uint32_t> fault{SC_NATIVE_NONE};
std::atomic<bool> accepting{false}, pinned{false};
std::atomic<bool> stopping{true};
thread_local uint32_t frame_depth = 0;
thread_local uint32_t event_depth = 0;
std::atomic<uint32_t> event_thread{0};
using Frame = void (*)(uintptr_t);
using Change = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t);
using Free = void (*)(uintptr_t, uintptr_t);
Frame original_frame = nullptr;
Change original_change = nullptr;
Free original_free = nullptr;
#ifdef SC_NATIVE_TESTING
TestAdapter fixture{};
std::atomic<bool> fixture_active{false};
#endif

void invalidate(uint32_t why) {
    uint32_t none = SC_NATIVE_NONE;
    fault.compare_exchange_strong(none, why, std::memory_order_acq_rel);
    epoch.fetch_add(1, std::memory_order_acq_rel);
    gaps.fetch_add(1, std::memory_order_relaxed);
}
template<typename T> bool read(uintptr_t address, T& value) {
    engine::LocalMemory memory;
    return memory.copy(address, &value, sizeof(value)).reason == SC_REASON_NONE;
}
uint32_t owner_thread() {
#ifdef SC_NATIVE_TESTING
    if (fixture_active) return fixture.owner();
#endif
    uintptr_t context = 0; uint64_t tid = 0;
    if (!read(binding.image.base + 0x4276160, context) || !context ||
        !read(context + 0x238, tid) || !tid || tid > UINT32_MAX) return 0;
    return static_cast<uint32_t>(tid);
}
uintptr_t map_address() {
#ifdef SC_NATIVE_TESTING
    if (fixture_active) return fixture.map();
#endif
    uintptr_t address = 0;
    return read(binding.root + 0x50, address) ? address : 0;
}
bool same_map(const sc_context_map& a, const sc_context_map& b) {
    return a.length == b.length && std::memcmp(a.bytes, b.bytes, a.length) == 0;
}
bool valid_frame(uintptr_t self, uintptr_t caller) {
    if (frame_depth != 1) { invalidate(SC_NATIVE_REENTRANT); return false; }
    uintptr_t expected_self = binding.image.base + 0x440fc10, expected_caller = binding.image.base + 0x5f5ab6;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) { expected_self = fixture.common; expected_caller = fixture.caller; }
#endif
    if (self != expected_self || caller != expected_caller) {
        invalidate(SC_NATIVE_WRONG_CALLER); return false;
    }
    if (owner_thread() != GetCurrentThreadId()) { invalidate(SC_NATIVE_WRONG_THREAD); return false; }
    return true;
}
void clear_context(uint32_t why) {
    status.context_generation = 0; status.context_sampled_at_ms = 0;
    status.context_reason = why; status.current_map = {};
}
uint32_t prerequisite(uint64_t now) {
    if (!accepting.load(std::memory_order_acquire)) return SC_NATIVE_STOPPED;
    if (const auto why = fault.load(std::memory_order_acquire)) return why;
    if (!lifetime.generation) return SC_NATIVE_UNOBSERVED;
    if (lifetime.depth) return SC_NATIVE_TRANSITION;
    if (lifetime.state != SC_LIFETIME_ACTIVE || status.context_generation != lifetime.generation)
        return SC_NATIVE_CONTEXT_UNAVAILABLE;
    if (now < status.context_sampled_at_ms || now - status.context_sampled_at_ms > 1000) return SC_NATIVE_STALE;
    return SC_NATIVE_NONE;
}
uint32_t backup_prerequisite(const sc_save_backup_request& request) {
    const auto& owner = save::session();
    if (owner.state() != save::SessionState::admitted || !owner.accepts_requests()) return SC_NATIVE_BINDING_FAILED;
    return std::memcmp(owner.namespace_id().data(), request.namespace_id, 64) ? SC_NATIVE_SCOPE_MISMATCH : SC_NATIVE_NONE;
}
bool begin_event(uintptr_t root, bool change, uintptr_t descriptor) {
    if (!accepting.load(std::memory_order_acquire)) return false;
    // Root binding and same native execution role are necessary independently
    // of frame recurrence. Other-thread nesting is never deduplicated as ours.
    if (root != binding.root || owner_thread() != GetCurrentThreadId()) {
        invalidate(SC_NATIVE_WRONG_THREAD); return false;
    }
    uint32_t none = 0;
    if (!event_depth && !event_thread.compare_exchange_strong(none, GetCurrentThreadId())) {
        invalidate(SC_NATIVE_REENTRANT); return false;
    }
    ++event_depth;
    epoch.fetch_add(1, std::memory_order_acq_rel);
    uint8_t flag = 0;
    bool known = false;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) known = change && fixture.checkpoint(descriptor, flag);
    else
#endif
    known = change && read(descriptor + 0x1961, flag);
    if (!TryAcquireSRWLockExclusive(&lock)) {
        invalidate(SC_NATIVE_EVENT_GAP);
        if (!--event_depth) event_thread.store(0, std::memory_order_release);
        return false;
    }
    lifetime.begin(change, known, (flag & 2) != 0, GetTickCount64(), GetCurrentThreadId());
    clear_context(SC_NATIVE_TRANSITION);
    ReleaseSRWLockExclusive(&lock);
    return true;
}
void end_event(bool change, bool success, bool abnormal) {
    uint32_t game = UINT32_MAX;
    bool readable = false;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) { game = fixture.game(); readable = true; }
    else
#endif
    readable = read(binding.root + 0x44, game);
    if (abnormal || !readable) invalidate(abnormal ? SC_NATIVE_EXCEPTION : SC_NATIVE_READ_FAILED);
    epoch.fetch_add(1, std::memory_order_acq_rel);
    if (TryAcquireSRWLockExclusive(&lock)) {
        lifetime.end(change, success && !abnormal, game, GetTickCount64(), GetCurrentThreadId());
        clear_context(SC_NATIVE_CONTEXT_UNAVAILABLE);
        ReleaseSRWLockExclusive(&lock);
    } else invalidate(SC_NATIVE_EVENT_GAP);
    if (!--event_depth) event_thread.store(0, std::memory_order_release);
}
void post_frame() {
    if (!accepting.load(std::memory_order_acquire) || fault.load(std::memory_order_acquire)) return;
    if (owner_thread() != GetCurrentThreadId()) { invalidate(SC_NATIVE_WRONG_THREAD); return; }
    // Missing this diagnostic opportunity is harmless; unlike a lifecycle event,
    // it need not be fabricated or become a gap when IPC briefly holds the lock.
    if (!TryAcquireSRWLockExclusive(&lock)) { diagnostics.note_claim_contention(); return; }
    ++status.callback_sequence; status.callback_at_ms = GetTickCount64();
    status.callback_thread_id = GetCurrentThreadId(); status.native_owner_thread_id = status.callback_thread_id;
    auto* slot = diagnostics.claim(status.callback_at_ms);
    uint32_t why = prerequisite(status.callback_at_ms);
    sc_native_scope scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    const auto before = epoch.load(std::memory_order_acquire);
    const auto life = lifetime.state;
    const auto expected_map_address = bound_map_address;
    const auto expected_map_name = bound_map_name;
    ReleaseSRWLockExclusive(&lock);
    if (!slot) return;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) fixture.gate(false);
#endif
    auto result = slot->result; // This callback now exclusively owns the result.
    auto detail = slot->detail;
    detail.stage = why ? SC_STAGE_CLAIM_CONTEXT : SC_STAGE_NONE;
    result.scope = scope; result.thread_id = GetCurrentThreadId();
    if (slot->is_backup) result.scope = slot->request.expected;
    result.site_revision = 1; result.phase = 1; result.lifecycle = life;
    auto reject = [&](uint32_t reason, uint32_t stage) { why = reason; detail.stage = stage; };
    if (!same_scope(slot->request.expected, scope)) reject(SC_NATIVE_SCOPE_MISMATCH, SC_STAGE_SCOPE);
    uint8_t pending = 1, pending_after = 1;
    engine::LocalMemory memory;
    sc_context_snapshot facts{};
    if (!why) {
        engine::ReadResult pending_read{};
#ifdef SC_NATIVE_TESTING
        if (fixture_active) pending = fixture.pending();
        else
#endif
        pending_read = memory.copy(binding.root + 0xb8, &pending, sizeof(pending));
        detail.pending_before = pending_read.reason ? 3u : (pending ? 2u : 1u);
        detail.pending_before_reason = pending_read.reason;
        detail.pending_before_error = pending_read.error;
        if (pending_read.reason || pending) reject(pending_read.reason ? SC_NATIVE_READ_FAILED : SC_NATIVE_TRANSITION, SC_STAGE_PENDING_BEFORE);
        else {
            engine::ReadResult after_read{};
            context::Evidence measured{};
            detail.observation_attempted = 1;
#ifdef SC_NATIVE_TESTING
            if (fixture_active) { facts = fixture.observe(measured); pending_after = fixture.pending(); }
            else
#endif
            { facts = context::sample(memory, binding, status.callback_sequence, nullptr, 2, &measured);
              after_read = memory.copy(binding.root + 0xb8, &pending_after, sizeof(pending_after)); }
            detail.observation_started_at_ms = measured.started_at_ms;
            detail.observation_elapsed_ns = measured.elapsed_ns; detail.observation_budget_ns = measured.budget_ns;
            detail.timing_valid = measured.timing_valid; detail.timing_error = measured.timing_error;
            detail.sample_reason = facts.sample_reason;
            detail.state_validity = facts.fields[SC_CONTEXT_GAME_STATE].validity;
            detail.state_reason = measured.state.reason; detail.state_error = measured.state.error;
            detail.map_validity = facts.current_map.validity;
            detail.map_reason = measured.map.reason; detail.map_error = measured.map.error;
            detail.pending_after = after_read.reason ? 3u : (pending_after ? 2u : 1u);
            detail.pending_after_reason = after_read.reason;
            detail.pending_after_error = after_read.error;
            if (after_read.reason || pending_after) reject(after_read.reason ? SC_NATIVE_READ_FAILED : SC_NATIVE_TRANSITION, SC_STAGE_PENDING_AFTER);
            else if (facts.sample_reason == SC_REASON_BUDGET) reject(SC_NATIVE_BUDGET, SC_STAGE_OBSERVATION_BUDGET);
            else if (facts.current_map.validity != SC_OBSERVATION_OBSERVED ||
                     facts.fields[SC_CONTEXT_GAME_STATE].validity != SC_OBSERVATION_OBSERVED ||
                     facts.fields[SC_CONTEXT_GAME_STATE].value != SC_GAME_IN_GAME)
                reject(SC_NATIVE_CONTEXT_UNAVAILABLE, SC_STAGE_FRESH_CONTEXT);
            // Accepted reader facts alone do not authorize the diagnostic body.
            detail.observation_accepted = facts.current_map.validity == SC_OBSERVATION_OBSERVED &&
                facts.fields[SC_CONTEXT_GAME_STATE].validity == SC_OBSERVATION_OBSERVED &&
                facts.fields[SC_CONTEXT_GAME_STATE].value == SC_GAME_IN_GAME;
        }
    }
    if (!why && (epoch.load(std::memory_order_acquire) != before || fault.load(std::memory_order_acquire)))
        reject(fault.load() ? fault.load() : SC_NATIVE_EVENT_GAP, SC_STAGE_EVENT_STAMP);
    if (!why && (map_address() != expected_map_address || !same_map(facts.current_map, expected_map_name))) {
        invalidate(SC_NATIVE_EVENT_GAP); reject(SC_NATIVE_EVENT_GAP, SC_STAGE_MAP_BINDING);
    }
    if (!why && owner_thread() != GetCurrentThreadId()) { invalidate(SC_NATIVE_WRONG_THREAD); reject(SC_NATIVE_WRONG_THREAD, SC_STAGE_NATIVE_FAULT); }
    if (!why && !accepting.load(std::memory_order_acquire)) reject(SC_NATIVE_STOPPED, SC_STAGE_NATIVE_FAULT);
    char backup_directory[64]{};
    if (!why && slot->is_backup) {
        const auto reason = backup_prerequisite(slot->backup_request);
        if (reason) reject(reason, SC_STAGE_NATIVE_FAULT);
        else {
            constexpr const char* campaigns[] = {"GAME-", "DLC1-", "DLC2-"};
            sprintf_s(backup_directory, "%s/%sAUTOSAVE%u", save::session().native_root().c_str(),
                campaigns[slot->backup_request.campaign], slot->backup_request.slot);
        }
    }
    if (!why && slot->cancel.load(std::memory_order_acquire)) reject(SC_NATIVE_CANCELLED, SC_STAGE_CANCELLATION);
    const auto execution_at = GetTickCount64();
    if (!why && execution_at >= result.deadline_at_ms) reject(SC_NATIVE_DEADLINE, SC_STAGE_DEADLINE);
    // Linearization point of the only diagnostic body. Cancellation after this
    // check may coexist with EXECUTED; a caller timeout is never proof otherwise.
    if (!why) {
        detail.stage = SC_STAGE_EXECUTED;
        result.observed_at_ms = facts.sampled_at_ms;
        result.executed_at_ms = execution_at;
        result.current_map = facts.current_map;
        result.game_state = static_cast<uint32_t>(facts.fields[SC_CONTEXT_GAME_STATE].value);
        result.state = SC_DIAGNOSTIC_EXECUTED;
        if (slot->is_backup) {
            // The observation budget ends above. Native serialization has its
            // own ordinary engine scheduling; it is not a two-millisecond job.
            const auto submitted = save::submit_native_backup(slot->backup, backup_directory);
            Diagnostics::await_backup(*slot, result, detail, submitted);
            return;
        }
    } else {
        result.state = why == SC_NATIVE_CANCELLED ? SC_DIAGNOSTIC_CANCELLED :
            (why == SC_NATIVE_DEADLINE ? SC_DIAGNOSTIC_EXPIRED : SC_DIAGNOSTIC_REJECTED);
    }
    result.reason = why;
#ifdef SC_NATIVE_TESTING
    if (fixture_active && result.state == SC_DIAGNOSTIC_EXECUTED) fixture.gate(true);
#endif
    result.completed_at_ms = GetTickCount64();
    Diagnostics::finish(*slot, result, detail);
}
void frame_detour(uintptr_t self) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ++frame_depth;
    const bool observe = accepting.load(std::memory_order_acquire) && valid_frame(self, caller);
    __try { original_frame(self); }
    __finally {
        if (observe) {
            if (AbnormalTermination()) invalidate(SC_NATIVE_EXCEPTION);
            else post_frame();
        }
        --frame_depth;
    }
}
uint64_t change_detour(uintptr_t root, uintptr_t descriptor, uintptr_t files) {
    const bool observe = begin_event(root, true, descriptor);
    uint64_t result = 0;
    __try { result = original_change(root, descriptor, files); }
    __finally { if (observe) end_event(true, result != 0, AbnormalTermination() != FALSE); }
    return result;
}
void free_detour(uintptr_t root, uintptr_t slot) {
    uintptr_t map = 0;
    bool primary = root == binding.root && slot == root + 0x50;
    bool readable = !primary || read(slot, map);
#ifdef SC_NATIVE_TESTING
    if (fixture_active) { primary = root == binding.root && fixture.primary(slot, map); readable = true; }
#endif
    if (primary && !readable && accepting.load()) invalidate(SC_NATIVE_READ_FAILED);
    const bool observe = primary && readable && map && begin_event(root, false, 0);
    __try { original_free(root, slot); }
    __finally { if (observe) end_event(false, true, AbnormalTermination() != FALSE); }
}
}

void prepare(const Snapshot& identity) {
    save::configure_prelaunch();
    AcquireSRWLockExclusive(&lock);
    status = {}; status.size = sizeof(status); status.abi_version = SC_NATIVE_ABI_VERSION;
    bound_generation = 0; bound_map_address = 0; bound_map_name = {};
    status.scope.pid = identity.pid; status.scope.process_created = identity.process_created;
    std::memcpy(status.scope.instance_id, identity.instance.data(), sizeof(status.scope.instance_id));
    status.reason = SC_NATIVE_NOT_STARTED; status.context_reason = SC_NATIVE_UNOBSERVED;
    stopping.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&lock);
}
void start(const engine::Binding& source, const Snapshot& identity, HANDLE stop_event) {
    (void)context::observation_clock(); // Cache QPC frequency before any hook is reachable.
    AcquireSRWLockExclusive(&startup);
    if (pinned.load(std::memory_order_acquire) || stopping.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&startup); return;
    }
    accepting.store(false, std::memory_order_release);
    binding = source;
    sc_native_snapshot initial{};
    initial.size = sizeof(initial); initial.abi_version = SC_NATIVE_ABI_VERSION;
    initial.scope.pid = identity.pid; initial.scope.process_created = identity.process_created;
    std::memcpy(initial.scope.instance_id, identity.instance.data(), sizeof(initial.scope.instance_id));
    initial.site_revision = 1; initial.site_rva = 0x43d1f0; initial.phase = 1;
    initial.availability = SC_NATIVE_DISABLED; initial.reason = SC_NATIVE_UNKNOWN_BUILD;
    initial.context_reason = SC_NATIVE_UNOBSERVED;
    engine::LocalMemory memory;
    std::array<Target, 3> targets{};
    const auto deadline = GetTickCount64() + 3000;
    for (unsigned i = 0; i < targets.size(); ++i) {
        targets[i] = profile_target(binding.image.base, i);
#ifdef SC_NATIVE_TESTING
        if (fixture_active) targets[i] = fixture.targets[i];
#endif
        initial.validator_reasons[i] = validate_target(memory, binding.image, targets[i], stop_event, deadline);
    }
    bool profile = binding.metadata.profile == SC_PROFILE_STEAM_20260818;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) { profile = true; initial.site_rva = static_cast<uint32_t>(targets[0].address - binding.image.base); }
#endif
    uintptr_t common = 0, vtable = 0, frame = 0;
    if (profile) {
        initial.reason = SC_NATIVE_NONE;
        for (const auto why : initial.validator_reasons) if (why && !initial.reason) initial.reason = why;
        bool check_binding = true;
#ifdef SC_NATIVE_TESTING
        check_binding = !fixture_active;
#endif
        if (!initial.reason && check_binding && (binding.root != binding.image.base + 0x45ea6f0 ||
            !read(binding.image.base + 0x2a6b040, common) || common != binding.image.base + 0x440fc10 ||
            !read(common, vtable) || vtable != binding.image.base + 0x2a6b9c0 ||
            !read(vtable + 0x20, frame) || frame != targets[0].address)) initial.reason = SC_NATIVE_BINDING_FAILED;
    }
    // Publish immutable binding/status before any detour can become reachable.
    AcquireSRWLockExclusive(&lock);
    lifetime = {}; status = initial; fault.store(SC_NATIVE_NONE); gaps.store(0); epoch.fetch_add(1);
    ReleaseSRWLockExclusive(&lock);
    if (!initial.reason && !stopping.load() && WaitForSingleObject(stop_event, 0) == WAIT_TIMEOUT) {
        uint32_t why = SC_NATIVE_NONE;
        const auto mh = MH_Initialize();
        if (mh != MH_OK) why = SC_NATIVE_HOOK_FAILED;
        void* trampolines[3]{};
        void* detours[] = {reinterpret_cast<void*>(frame_detour), reinterpret_cast<void*>(change_detour), reinterpret_cast<void*>(free_detour)};
        unsigned created = 0;
        while (!why && created < targets.size()) {
            if (MH_CreateHook(reinterpret_cast<void*>(targets[created].address), detours[created], &trampolines[created]) != MH_OK)
                why = SC_NATIVE_HOOK_FAILED;
            else ++created;
        }
        HMODULE module = nullptr;
        if (!why && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(frame_detour), &module)) why = SC_NATIVE_PIN_FAILED;
        if (!why) {
            pinned.store(true, std::memory_order_release);
            original_frame = reinterpret_cast<Frame>(trampolines[0]);
            original_change = reinterpret_cast<Change>(trampolines[1]);
            original_free = reinterpret_cast<Free>(trampolines[2]);
            // Per-target operations only. Never MH_ALL_HOOKS or a foreign target.
            for (unsigned i = 0; i < targets.size() && !why; ++i) {
                std::array<uint8_t, 32> current{};
                if (memory.copy(targets[i].address, current.data(), current.size()).reason != SC_REASON_NONE ||
                    std::memcmp(current.data(), targets[i].bytes.data(), current.size())) why = SC_NATIVE_TARGET_BYTES;
                else if (MH_EnableHook(reinterpret_cast<void*>(targets[i].address)) != MH_OK) why = SC_NATIVE_HOOK_FAILED;
                else initial.installed_hooks |= 1u << i;
            }
        } else {
            // These trampolines have NEVER been reachable. Removing them cannot
            // race a detour entry. Once pinned/enabled, removal is unsupported.
            for (unsigned i = 0; i < created; ++i) MH_RemoveHook(reinterpret_cast<void*>(targets[i].address));
            if (mh == MH_OK) MH_Uninitialize();
        }
        if (!why && !stopping.load(std::memory_order_acquire)) save::install_native_hooks(binding, stop_event);
        AcquireSRWLockExclusive(&lock);
        status.installed_hooks = initial.installed_hooks; status.retained_module = pinned.load();
        status.reason = why; status.coverage = why ? 0 : 7;
        status.availability = why ? SC_NATIVE_DISABLED : SC_NATIVE_ENABLED;
        accepting.store(!why && !stopping.load(std::memory_order_acquire), std::memory_order_release);
        ReleaseSRWLockExclusive(&lock);
    }
    ReleaseSRWLockExclusive(&startup);
}
uint64_t observation_stamp() { return epoch.load(std::memory_order_acquire); }
void publish_context(const sc_context_snapshot& observed, uint64_t before) {
#ifdef SC_NATIVE_TESTING
    const auto& value = fixture_active ? fixture.context() : observed;
#else
    const auto& value = observed;
#endif
    const auto address = map_address();
    AcquireSRWLockExclusive(&lock);
    if (accepting.load() && !fault.load() && before == epoch.load() &&
        lifetime.state == SC_LIFETIME_ACTIVE && !lifetime.depth &&
        value.fields[SC_CONTEXT_GAME_STATE].validity == SC_OBSERVATION_OBSERVED &&
        value.fields[SC_CONTEXT_GAME_STATE].value != SC_GAME_IN_GAME) invalidate(SC_NATIVE_EVENT_GAP);
    if (accepting.load(std::memory_order_acquire) && !fault.load(std::memory_order_acquire) &&
        before == epoch.load(std::memory_order_acquire) && lifetime.state == SC_LIFETIME_ACTIVE && !lifetime.depth &&
        value.current_map.validity == SC_OBSERVATION_OBSERVED && value.fields[SC_CONTEXT_GAME_STATE].validity == SC_OBSERVATION_OBSERVED &&
        value.fields[SC_CONTEXT_GAME_STATE].value == SC_GAME_IN_GAME && address) {
        if (bound_generation == lifetime.generation &&
            (address != bound_map_address || !same_map(value.current_map, bound_map_name))) {
            invalidate(SC_NATIVE_EVENT_GAP); clear_context(SC_NATIVE_EVENT_GAP);
        } else {
            bound_generation = lifetime.generation; bound_map_address = address; bound_map_name = value.current_map;
            status.context_generation = lifetime.generation; status.context_sampled_at_ms = value.sampled_at_ms;
            status.context_reason = SC_NATIVE_NONE; status.game_state = SC_GAME_IN_GAME; status.current_map = value.current_map;
        }
    } else clear_context(before != epoch.load() ? SC_NATIVE_TRANSITION : SC_NATIVE_CONTEXT_UNAVAILABLE);
    ReleaseSRWLockExclusive(&lock);
}
sc_native_snapshot inspect(uint64_t after) {
    AcquireSRWLockExclusive(&lock);
    auto out = status; out.size = sizeof(out); out.abi_version = SC_NATIVE_ABI_VERSION;
    out.scope.lifecycle_generation = lifetime.generation; out.lifecycle = lifetime.state; out.depth = lifetime.depth;
    out.checkpoint_flag_known = lifetime.checkpoint_known; out.checkpoint_flag = lifetime.checkpoint;
    out.event_gap_count = gaps.load(std::memory_order_acquire);
    if (const auto why = fault.load(std::memory_order_acquire)) {
        if (out.availability != SC_NATIVE_RETAINED) out.availability = SC_NATIVE_DISABLED;
        out.reason = why; out.context_generation = 0; out.lifecycle = SC_LIFETIME_INVALID;
        out.context_reason = why; out.current_map = {};
    }
    if (out.context_generation && GetTickCount64() - out.context_sampled_at_ms > 1000) {
        out.context_generation = 0; out.context_reason = SC_NATIVE_STALE; out.current_map = {};
    }
    lifetime.page(out, after); diagnostics.counts(out, GetTickCount64());
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_diagnostic_result submit(const sc_diagnostic_request& request, sc_diagnostic_detail* detail) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    auto out = diagnostics.submit(request, why, now, detail);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_diagnostic_result result(const sc_diagnostic_request& request, bool cancel, sc_diagnostic_detail* detail) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.retrieve(request, cancel, GetTickCount64(), detail);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_save_backup_snapshot submit_backup(const sc_save_backup_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.execution.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    if (!why) why = backup_prerequisite(request);
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, &request);
    sc_save_backup_snapshot out{};
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) {
        out.size = sizeof(out); out.abi_version = SC_SAVE_BACKUP_ABI_VERSION;
        out.state = SC_BACKUP_REJECTED; out.execution = admitted;
        std::memcpy(out.namespace_id, request.namespace_id, sizeof(out.namespace_id));
        out.campaign = request.campaign; out.slot = request.slot;
    } else out = diagnostics.backup_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_save_backup_snapshot backup_result(const sc_save_backup_request& request, bool cancel) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.backup_result(request, cancel, GetTickCount64());
    ReleaseSRWLockExclusive(&lock); return out;
}
bool stop() {
    save::session().stop_requests();
    stopping.store(true, std::memory_order_release);
    accepting.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&startup); // Never called from a native callback.
    accepting.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&lock);
    diagnostics.cancel_pending(GetTickCount64()); clear_context(SC_NATIVE_STOPPED);
    status.availability = retained() ? SC_NATIVE_RETAINED : SC_NATIVE_DISABLED;
    status.reason = SC_NATIVE_STOPPED;
    ReleaseSRWLockExclusive(&lock); ReleaseSRWLockExclusive(&startup);
    return retained();
}
bool retained() { return pinned.load(std::memory_order_acquire) || save::owner_retained(); }
#ifdef SC_NATIVE_TESTING
void test_start(const TestAdapter& adapter, const Snapshot& identity, HANDLE stop_event) {
    fixture = adapter; fixture_active = true;
    engine::LocalMemory memory; engine::Binding source;
    engine::read_image(memory, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), source.image);
    source.root = fixture.root;
    start(source, identity, stop_event);
}
#endif
}
