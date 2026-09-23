#include "native_runtime.h"
#include "inventory.h"
#include "arsenal.h"
#include "runes.h"
#include "special.h"
#include "deathlink.h"
#include "native_target.h"
#include "save_native_hooks.h"
#include "save_campaign_native.h"
#include "campaign_menu.h"
#include "campaign_menu_native.h"
#include "challenge_suppression.h"
#include "fast_travel.h"
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
bool begin_event(uintptr_t root, bool change, uintptr_t descriptor, save::CampaignTransition* transition=nullptr) {
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
    if (fixture_active && fixture.checkpoint) known = change && fixture.checkpoint(descriptor, flag);
    else
#endif
    known = change && read(descriptor + 0x1961, flag);
    if (!TryAcquireSRWLockExclusive(&lock)) {
        invalidate(SC_NATIVE_EVENT_GAP);
        if (!--event_depth) event_thread.store(0, std::memory_order_release);
        return false;
    }
    if (transition) transition->generation_before=lifetime.generation;
    lifetime.begin(change, known, (flag & 2) != 0, GetTickCount64(), GetCurrentThreadId());
    if (transition) {
        transition->event_id=lifetime.sequence; transition->generation_after=lifetime.generation;
        transition->depth=lifetime.depth; transition->observed=true;
    }
    clear_context(SC_NATIVE_TRANSITION);
    ReleaseSRWLockExclusive(&lock);
    return true;
}
void end_event(bool change, bool success, bool abnormal, save::CampaignTransition* transition=nullptr) {
    uint32_t game = UINT32_MAX;
    bool readable = false;
#ifdef SC_NATIVE_TESTING
    if (fixture_active && fixture.game) { game = fixture.game(); readable = true; }
    else
#endif
    readable = read(binding.root + 0x44, game);
    if (abnormal || !readable) invalidate(abnormal ? SC_NATIVE_EXCEPTION : SC_NATIVE_READ_FAILED);
    epoch.fetch_add(1, std::memory_order_acq_rel);
    if (TryAcquireSRWLockExclusive(&lock)) {
        lifetime.end(change, success && !abnormal, game, GetTickCount64(), GetCurrentThreadId());
        if (transition) transition->generation_after=lifetime.generation;
        clear_context(SC_NATIVE_CONTEXT_UNAVAILABLE);
        ReleaseSRWLockExclusive(&lock);
    } else invalidate(SC_NATIVE_EVENT_GAP);
    if (transition) {
        transition->game=game; transition->state_read=readable; transition->abnormal=abnormal;
        transition->ended=true; transition->at_ms=GetTickCount64();
        transition->observation_reason=fault.load(std::memory_order_acquire);
    }
    if (!--event_depth) event_thread.store(0, std::memory_order_release);
}
static uintptr_t tick_player_safely(uintptr_t fn, uintptr_t active_map, uint64_t generation) {
    uintptr_t player=0;
    __try {
        player = reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(fn)(active_map, 0);
        if (player) {
            const auto& id = save::session().namespace_id();
            if (runes::admitted(id.c_str())) runes::bind_run_state_if_needed(player, generation);
            if (special::admitted(id.c_str())) {
                special::bind_run_state_if_needed(player, generation);
                special::poll_input(player, true);
            } else special::poll_input(0, false);
        }
        const auto& id = save::session().namespace_id();
        if (deathlink::admitted(id.c_str())) deathlink::tick_native();
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return player;
}
bool gameplay_admitted_locked_scope() {
    if (save::session().state() != save::SessionState::admitted ||
        !save::session().accepts_requests()) return false;
    AcquireSRWLockShared(&lock);
    const bool allowed = prerequisite(GetTickCount64()) == 0;
    ReleaseSRWLockShared(&lock);
    return allowed;
}

using FastTravelFind=uintptr_t(*)(uintptr_t,const char*);
using FastTravelActivate=void(*)(uintptr_t,uintptr_t);
FastTravelFind fast_travel_find=nullptr;
FastTravelActivate fast_travel_activate=nullptr;
std::atomic<bool> fast_travel_ready{false};
void install_fast_travel(const engine::Binding& b) {
    engine::LocalMemory memory;
    const struct { uint32_t rva; const char* hex; } sites[] = {
        {0x6ca290,"48895c2408488974241048897c241841564883ec20488bf94c8bf2488b0dee70"},
        {0xd68750,"48895c2418574883ec40488b05776244034833c44889442438488bf9488bda48"}
    };
    for (const auto& s:sites) {
        std::array<uint8_t,32> actual{},expected{};
        const auto digit=[](char c) { return static_cast<uint8_t>(c<='9'?c-'0':c-'a'+10); };
        for (size_t i=0;i<32;++i) expected[i]=static_cast<uint8_t>(digit(s.hex[i*2])*16+digit(s.hex[i*2+1]));
        if (!b.image.contains(s.rva,32,IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ,0) ||
            memory.copy(b.image.base+s.rva,actual.data(),32).reason || actual!=expected) return;
    }
    fast_travel_find=reinterpret_cast<FastTravelFind>(b.image.base+0x6ca290);
    fast_travel_activate=reinterpret_cast<FastTravelActivate>(b.image.base+0xd68750);
    fast_travel_ready.store(true,std::memory_order_release);
}
void reconcile_fast_travel(uintptr_t map,const char* map_name,uint64_t generation,uintptr_t player) {
    const auto& namespace_id=save::session().namespace_id();
    const bool authorized=fast_travel_ready.load(std::memory_order_acquire) &&
        fast_travel::entry_policy().authorized(namespace_id.c_str(),map_name,generation);
    if (!authorized) { fast_travel::dispatch_policy().observe(false,generation,0,false,false); return; }
    if (!player) return;
    __try {
        const auto image=binding.image.base;
        if (!map || *reinterpret_cast<uintptr_t*>(map)!=image+0x2ab30c8 ||
            *reinterpret_cast<uintptr_t*>(image+0x45f7370)!=map) return;
        constexpr char target_name[]="ap_fast_travel_unlock_native";
        auto target=fast_travel_find(map,target_name);
        if (target && *reinterpret_cast<uintptr_t*>(target)!=image+0x2c47aa8) target=0;
        const bool target_unlocked=target && *reinterpret_cast<uint8_t*>(target+0xb90)!=0;
        const bool world_unlocked=*reinterpret_cast<uint8_t*>(map+0x1ac380)!=0;
        const auto decision=fast_travel::dispatch_policy().observe(true,generation,target,target_unlocked,world_unlocked);
        if (decision!=fast_travel::DispatchDecision::invoke) return;
        fast_travel::dispatch_policy().invoked(target,false);
        fast_travel_activate(target,player);
        const bool post=*reinterpret_cast<uint8_t*>(target+0xb90)!=0 &&
            *reinterpret_cast<uint8_t*>(map+0x1ac380)!=0;
        fast_travel::dispatch_policy().invoked(target,post);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Native Automap collected-state ownership is scoped to exact catalog keys and maps.
// HUD idPOIManager is a different subsystem and MUST NOT be used on AP props.
SRWLOCK automap_lock = SRWLOCK_INIT;
sc_automap_request automap_snapshot{};
uint64_t automap_checked_floor[8]{};
uint64_t automap_scanned=0, automap_collected=0, automap_passes=0;
std::atomic<bool> automap_ready{false};
std::atomic<uint32_t> automap_fault{0};
uint64_t automap_generation=0;
uint32_t automap_cursor=0;
uintptr_t automap_keys=0;
using AutomapCollect=void(*)(uintptr_t,uintptr_t);
AutomapCollect automap_collect=nullptr;
#include "automap_catalog.h"

void install_automap(const engine::Binding& b) {
    engine::LocalMemory memory;
    const struct { uint32_t rva; const char* hex; } sites[] = {
        {0xa55f90,"40534883ec20448b8148030000488d057cdb01024c8bca4889442430488b9140"}
    };
    for (const auto& s:sites) {
        std::array<uint8_t,32> actual{}, expected{};
        const auto digit=[](char c) { return static_cast<uint8_t>(c<='9'?c-'0':c-'a'+10); };
        for (size_t i=0;i<32;++i) expected[i]=static_cast<uint8_t>(digit(s.hex[i*2])*16+digit(s.hex[i*2+1]));
        if (!b.image.contains(s.rva,32,IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ,0) ||
            memory.copy(b.image.base+s.rva,actual.data(),32).reason || actual!=expected) return;
    }
    automap_collect=reinterpret_cast<AutomapCollect>(b.image.base+0xa55f90);
    automap_ready.store(true,std::memory_order_release);
}
struct AutomapLists { uintptr_t keys,values; int32_t count,key_capacity,value_capacity; };
bool automap_lists(uintptr_t owner,AutomapLists& out) {
    out.keys=*reinterpret_cast<uintptr_t*>(owner);
    out.count=*reinterpret_cast<int32_t*>(owner+8);
    out.key_capacity=*reinterpret_cast<int32_t*>(owner+12);
    out.values=*reinterpret_cast<uintptr_t*>(owner+24);
    out.value_capacity=*reinterpret_cast<int32_t*>(owner+36);
    return out.count>=0 && out.count<=16384 && out.count==*reinterpret_cast<int32_t*>(owner+32) &&
        out.count<=out.key_capacity && out.count<=out.value_capacity &&
        (!out.count || (out.keys && out.values));
}
bool automap_name(uintptr_t key,char (&name)[64]) {
    const auto length=*reinterpret_cast<int32_t*>(key+16);
    const auto data=*reinterpret_cast<const char**>(key+8);
    if (!data || length<=0 || length>=64) return false;
    std::memcpy(name,data,static_cast<size_t>(length)); name[length]=0;
    return std::strlen(name)==static_cast<size_t>(length);
}
bool automap_checked(const sc_automap_request& snapshot,const char* map,const char* name) {
    if (!snapshot.known) return false;
    constexpr char helper[]="ap_automap_location_";
    if (std::strncmp(name,helper,sizeof(helper)-1)) return false;
    const char* digits=name+sizeof(helper)-1;
    if (std::strlen(digits)!=7) return false;
    uint32_t id=0;
    for (unsigned i=0;i<7;++i) {
        if (digits[i]<'0' || digits[i]>'9') return false;
        id=id*10+static_cast<uint32_t>(digits[i]-'0');
    }
    for (const auto& marker:automap_markers) {
        if (marker.location!=id || std::strcmp(marker.map,map)) continue;
        const auto bit=id-SC_AUTOMAP_LOCATION_BASE;
        return (snapshot.checked_locations[bit/64] & (uint64_t{1}<<(bit%64)))!=0;
    }
    return false;
}
bool automap_set_collected(uintptr_t system,uintptr_t key,uintptr_t object) {
    if (*reinterpret_cast<int32_t*>(object+0x134)==3) return true;
    automap_collect(system,key);
    return *reinterpret_cast<int32_t*>(object+0x134)==3;
}
// Called only by the already-admitted native frame, never by an IPC reader.
// Every pass rereads native keys/weak owners, including same-epoch reconstruction.
void reconcile_automap(uintptr_t map,const char* map_name,uint64_t generation) {
    if (!automap_ready.load(std::memory_order_acquire) || automap_fault.load()) return;
    sc_automap_request snapshot{};
    AcquireSRWLockShared(&automap_lock); snapshot=automap_snapshot; ReleaseSRWLockShared(&automap_lock);
    if (!snapshot.known || save::session().state()!=save::SessionState::admitted || !save::session().accepts_requests()) return;
    uint64_t scanned=0,collected=0,passes=0;
    __try {
        const auto image=binding.image.base;
        if (!map || *reinterpret_cast<uintptr_t*>(map)!=image+0x2ab30c8 ||
            *reinterpret_cast<uintptr_t*>(image+0x45f7370)!=map) return;
        const auto system=map+0x1a7380;
        const auto owner=system+0x340;
        AutomapLists list{};
        if (!automap_lists(owner,list)) { automap_fault.store(1); return; }
        if (automap_generation!=generation || automap_keys!=list.keys) {
            automap_generation=generation; automap_keys=list.keys; automap_cursor=0;
        }
        for (unsigned budget=0;budget<16 && collected<4;++budget) {
            if (!automap_lists(owner,list)) { automap_fault.store(1); break; }
            if (automap_cursor>=static_cast<uint32_t>(list.count)) {
                automap_cursor=0; ++passes; break;
            }
            const auto key=list.keys+static_cast<uintptr_t>(automap_cursor)*0x30;
            const auto object=list.values+static_cast<uintptr_t>(automap_cursor)*0x150;
            char name[64]{}; ++scanned;
            if (!automap_name(key,name) || !automap_checked(snapshot,map_name,name) ||
                *reinterpret_cast<int32_t*>(object+0x134)==3) {
                ++automap_cursor; continue;
            }
            if (!automap_set_collected(system,key,object)) { automap_fault.store(2); break; }
            AutomapLists after{}; char after_name[64]{};
            if (!automap_lists(owner,after) || after.count!=list.count ||
                after.keys!=list.keys || after.values!=list.values ||
                !automap_name(after.keys+static_cast<uintptr_t>(automap_cursor)*0x30,after_name) ||
                std::strcmp(name,after_name) ||
                *reinterpret_cast<int32_t*>(after.values+static_cast<uintptr_t>(automap_cursor)*0x150+0x134)!=3) {
                automap_fault.store(2); break;
            }
            ++collected; ++automap_cursor;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { automap_fault.store(GetExceptionCode()); }
    AcquireSRWLockExclusive(&automap_lock);
    automap_scanned+=scanned; automap_collected+=collected; automap_passes+=passes;
    ReleaseSRWLockExclusive(&automap_lock);
}

void post_frame() {
    if (!accepting.load(std::memory_order_acquire) || fault.load(std::memory_order_acquire)) return;
    if (owner_thread() != GetCurrentThreadId()) { invalidate(SC_NATIVE_WRONG_THREAD); return; }
    deathlink::expire_pending(GetTickCount64());
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
#ifdef SC_NATIVE_TESTING
    if (fixture_active) fixture.gate(false);
#endif
    auto result = slot ? slot->result : sc_diagnostic_result{};
    auto detail = slot ? slot->detail : sc_diagnostic_detail{};
    detail.stage = why ? SC_STAGE_CLAIM_CONTEXT : SC_STAGE_NONE;
    result.scope = scope; result.thread_id = GetCurrentThreadId();
    if (slot && slot->is_backup) result.scope = slot->request.expected;
    result.site_revision = 1; result.phase = 1; result.lifecycle = life;
    auto reject = [&](uint32_t reason, uint32_t stage) { why = reason; detail.stage = stage; };
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
    // Autonomous work uses the same fresh native admission as IPC, even with
    // zero diagnostic slots. A diagnostic query is never its actuator.
    const auto player=!why ? tick_player_safely(binding.image.base + 0x69af70, expected_map_address, scope.lifecycle_generation) : 0;
    if (!player && owner_thread() == GetCurrentThreadId()) special::poll_input(0, false);
    // Player natives may call back into a lifecycle hook before returning.
    if (!why && (epoch.load(std::memory_order_acquire) != before || fault.load(std::memory_order_acquire)))
        reject(fault.load() ? fault.load() : SC_NATIVE_EVENT_GAP, SC_STAGE_EVENT_STAMP);
    if (!why) reconcile_fast_travel(expected_map_address,facts.current_map.bytes,scope.lifecycle_generation,player);
    if (!why && (epoch.load(std::memory_order_acquire) != before || fault.load(std::memory_order_acquire)))
        reject(fault.load() ? fault.load() : SC_NATIVE_EVENT_GAP, SC_STAGE_EVENT_STAMP);
    if (!why) reconcile_automap(expected_map_address, facts.current_map.bytes, scope.lifecycle_generation);
    if (!why && player) arsenal::tick_masteries(scope.lifecycle_generation, player);
    if (!slot) return;
    if (!why && (epoch.load(std::memory_order_acquire) != before || fault.load(std::memory_order_acquire)))
        reject(fault.load() ? fault.load() : SC_NATIVE_EVENT_GAP, SC_STAGE_EVENT_STAMP);
    if (!same_scope(slot->request.expected, scope)) reject(SC_NATIVE_SCOPE_MISMATCH, SC_STAGE_SCOPE);
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
        if (slot->is_weapon_points)
            weapon_points::execute_native(slot->weapon_points_request, slot->weapon_points_result);
        if (slot->is_inventory)
            inventory::execute_native(slot->inventory_request, slot->inventory_result);
        if (slot->is_arsenal)
            arsenal::execute_native(slot->arsenal_request, slot->arsenal_result);
        if (slot->is_runes)
            runes::execute_native(slot->runes_request, slot->runes_result);
        if (slot->is_special)
            special::execute_native(slot->special_request, slot->special_result);
        if (slot->is_deathlink)
            deathlink::execute_native(slot->deathlink_request, slot->deathlink_result);
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
    save::CampaignTransition transition{};
    const bool observe = begin_event(root, true, descriptor, &transition);
    // Admission/parser/provider boundaries enforce AP ownership. A campaign
    // observation failure must not replace the native transition with return0:
    // native ExecuteMapChange owns transition progress and menu cleanup.
    // Keep its real return/cleanup even after Session refusal; all persistence
    // and campaign parser routes remain closed by that same terminal Session.
    save::campaign_change_begin(root, descriptor, transition);
    uint64_t result = 0;
    __try { result = original_change(root, descriptor, files); }
    __finally {
        transition.native_return=result; transition.abnormal=AbnormalTermination()!=FALSE;
        if (observe) end_event(true, result != 0, transition.abnormal, &transition);
        save::campaign_change_end(transition);
    }
    return result;
}
void free_detour(uintptr_t root, uintptr_t slot) {
    uintptr_t map = 0;
    bool primary = root == binding.root && slot == root + 0x50;
    bool readable = !primary || read(slot, map);
#ifdef SC_NATIVE_TESTING
    if (fixture_active && fixture.primary) { primary = root == binding.root && fixture.primary(slot, map); readable = true; }
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
    auto& installation = save::session().installation;
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
        initial.validator_reasons[i] = validate_recorded(installation, memory, binding.image, targets[i], stop_event, deadline, 1, i);
    }
    bool profile = binding.metadata.profile == SC_PROFILE_STEAM_20260818;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) { profile = true; initial.site_rva = static_cast<uint32_t>(targets[0].address - binding.image.base); }
#endif
    uintptr_t common = 0, vtable = 0, frame = 0;
    auto binding_event = installation.begin(SC_INSTALL_NATIVE_BINDING);
    if (profile) {
        initial.reason = SC_NATIVE_NONE;
        for (const auto why : initial.validator_reasons) if (why && !initial.reason) initial.reason = why;
        bool check_binding = true;
#ifdef SC_NATIVE_TESTING
        check_binding = !fixture_active;
#endif
        auto observed_read = [&](uintptr_t address, uintptr_t& value) {
            const auto result = memory.copy(address, &value, sizeof(value));
            save::attach_read(binding_event, result); return !result.reason;
        };
        if (!initial.reason && check_binding && (binding.root != binding.image.base + 0x45ea6f0 ||
            !observed_read(binding.image.base + 0x2a6b040, common) || common != binding.image.base + 0x440fc10 ||
            !observed_read(common, vtable) || vtable != binding.image.base + 0x2a6b9c0 ||
            !observed_read(vtable + 0x20, frame) || frame != targets[0].address)) initial.reason = SC_NATIVE_BINDING_FAILED;
    }
    installation.finish(binding_event, initial.reason);
    // Publish immutable binding/status before any detour can become reachable.
    AcquireSRWLockExclusive(&lock);
    lifetime = {}; status = initial; fault.store(SC_NATIVE_NONE); gaps.store(0); epoch.fetch_add(1);
    ReleaseSRWLockExclusive(&lock);
    if (!initial.reason && !stopping.load() && WaitForSingleObject(stop_event, 0) == WAIT_TIMEOUT) {
        uint32_t why = SC_NATIVE_NONE;
        const auto mh = installation.hook(SC_INSTALL_MH_INITIALIZE, 0, SC_INSTALL_UNKNOWN, 0, [] { return MH_Initialize(); });
        if (mh != MH_OK) why = SC_NATIVE_HOOK_FAILED;
        void* trampolines[3]{};
        void* detours[] = {reinterpret_cast<void*>(frame_detour), reinterpret_cast<void*>(change_detour), reinterpret_cast<void*>(free_detour)};
        unsigned created = 0;
        while (!why && created < targets.size()) {
            if (installation.hook(SC_INSTALL_NATIVE_CREATE, 1, created,
                static_cast<uint32_t>(targets[created].address - binding.image.base), [&] {
                    return MH_CreateHook(reinterpret_cast<void*>(targets[created].address), detours[created], &trampolines[created]); }) != MH_OK)
                why = SC_NATIVE_HOOK_FAILED;
            else ++created;
        }
        HMODULE module = nullptr;
        if (!why) {
            auto event = installation.begin(SC_INSTALL_PIN);
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(frame_detour), &module)) {
                const auto error = GetLastError(); why = SC_NATIVE_PIN_FAILED;
                installation.finish(event, why, SC_INSTALL_UNKNOWN, error);
            } else installation.finish(event);
        }
        if (!why) {
            pinned.store(true, std::memory_order_release);
            original_frame = reinterpret_cast<Frame>(trampolines[0]);
            original_change = reinterpret_cast<Change>(trampolines[1]);
            original_free = reinterpret_cast<Free>(trampolines[2]);
            // Per-target operations only. Never MH_ALL_HOOKS or a foreign target.
            for (unsigned i = 0; i < targets.size() && !why; ++i) {
                std::array<uint8_t, 32> current{};
                auto event = installation.begin(SC_INSTALL_NATIVE_ENABLE, 1, i,
                    static_cast<uint32_t>(targets[i].address - binding.image.base));
                const auto read_result = memory.copy(targets[i].address, current.data(), current.size());
                save::attach_read(event, read_result); event.byte_count = 32;
                std::memcpy(event.expected_bytes, targets[i].bytes.data(), 32); std::memcpy(event.actual_bytes, current.data(), 32);
                uint32_t enabled = SC_INSTALL_UNKNOWN;
                if (read_result.reason || std::memcmp(current.data(), targets[i].bytes.data(), current.size())) why = SC_NATIVE_TARGET_BYTES;
                else {
                    enabled = static_cast<uint32_t>(MH_EnableHook(reinterpret_cast<void*>(targets[i].address)));
                    if (enabled) why = SC_NATIVE_HOOK_FAILED; else initial.installed_hooks |= 1u << i;
                }
                installation.finish(event, why, enabled);
            }
        } else {
            // These trampolines have NEVER been reachable. Removing them cannot
            // race a detour entry. Once pinned/enabled, removal is unsupported.
            for (unsigned i = 0; i < created; ++i) installation.hook(SC_INSTALL_REMOVE, 1, i,
                static_cast<uint32_t>(targets[i].address - binding.image.base), [&] {
                    return MH_RemoveHook(reinterpret_cast<void*>(targets[i].address)); });
            if (mh == MH_OK) installation.hook(SC_INSTALL_UNINITIALIZE, 0, SC_INSTALL_UNKNOWN, 0, [] { return MH_Uninitialize(); });
        }
        if (!why && !stopping.load(std::memory_order_acquire)) save::install_native_hooks(binding, stop_event);
        // Special qualifies the shared HUD earnings entry before WUP owns its detour.
        if (!why && !stopping.load(std::memory_order_acquire)) special::install(binding, stop_event);
        // The WUP owner validates and patches the 13ce4c0 entry first; the
        // challenge scope then qualifies the untouched seam/continuation windows
        // and coexists with that detour on the same function.
        if (!why && !stopping.load(std::memory_order_acquire)) weapon_points::install(binding, stop_event);
        if (!why && !stopping.load(std::memory_order_acquire)) challenge::install(binding, stop_event);
        if (!why && !stopping.load(std::memory_order_acquire)) inventory::install(binding, stop_event);
        if (!why && !stopping.load(std::memory_order_acquire)) arsenal::install(binding, stop_event);
        if (!why && !stopping.load(std::memory_order_acquire)) runes::install(binding, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire)) deathlink::install(binding, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire)) install_automap(binding);
    if (!why && !stopping.load(std::memory_order_acquire)) install_fast_travel(binding);
        if (!why && !stopping.load(std::memory_order_acquire) && !campaign_menu::install(binding,stop_event)) {
            save::session().campaign_run.refuse("native_campaign_menu_installation_failed");
            why=SC_NATIVE_EXCEPTION;
        }
        AcquireSRWLockExclusive(&lock);
        status.installed_hooks = initial.installed_hooks; status.retained_module = pinned.load();
        status.reason = why; status.coverage = why ? 0 : 7;
        status.availability = why ? SC_NATIVE_DISABLED : SC_NATIVE_ENABLED;
        accepting.store(!why && !stopping.load(std::memory_order_acquire), std::memory_order_release);
        ReleaseSRWLockExclusive(&lock);
    }
    ReleaseSRWLockExclusive(&startup);
}
bool gameplay_admitted() { return gameplay_admitted_locked_scope(); }
uint64_t observation_stamp() { return epoch.load(std::memory_order_acquire); }
void publish_context(const sc_context_snapshot& observed, uint64_t before) {
    special::refresh_input_config(); // Observer thread: no game-thread file I/O.
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
sc_automap_result automap_request(const sc_automap_request& request) {
    sc_automap_result out{}; out.size=sizeof(out); out.abi_version=SC_AUTOMAP_ABI_VERSION;
    out.kind=request.kind; out.request_id=request.execution.request_id;
    std::memcpy(out.nonce,request.execution.nonce,sizeof(out.nonce));
    std::memcpy(out.namespace_id,request.namespace_id,sizeof(out.namespace_id));
    AcquireSRWLockShared(&lock);
    out.scope=status.scope; out.scope.lifecycle_generation=lifetime.generation;
    const bool admitted=same_scope(out.scope,request.execution.expected) && !request.namespace_id[64] &&
        save::session().state()==save::SessionState::admitted && save::session().accepts_requests() &&
        save::session().namespace_id()==request.namespace_id;
    AcquireSRWLockExclusive(&automap_lock);
    out.outcome=SC_AUTOMAP_REFUSED;
    if (!automap_ready.load() || automap_fault.load()) out.outcome=SC_AUTOMAP_UNAVAILABLE;
    else if (admitted && request.kind==SC_AUTOMAP_OBSERVE) out.outcome=SC_AUTOMAP_ACCEPTED;
    else if (admitted && request.kind==SC_AUTOMAP_PUBLISH && request.known<=1 && request.revision) {
        const bool duplicate=request.revision==automap_snapshot.revision && request.known==automap_snapshot.known &&
            !std::memcmp(request.checked_locations,automap_snapshot.checked_locations,sizeof(request.checked_locations));
        bool regression=false;
        if (request.known) for (size_t i=0;i<8;++i)
            regression|=(automap_checked_floor[i]&~request.checked_locations[i])!=0;
        if (regression) out.outcome=SC_AUTOMAP_REGRESSION;
        else if (duplicate || request.revision>automap_snapshot.revision) {
            automap_snapshot=request;
            if (request.known) std::memcpy(automap_checked_floor,request.checked_locations,sizeof(automap_checked_floor));
            out.outcome=SC_AUTOMAP_ACCEPTED;
        }
    }
    out.known=automap_snapshot.known; out.revision=automap_snapshot.revision;
    out.native_fault=automap_fault.load(); out.scanned=automap_scanned;
    out.collected=automap_collected; out.completed_passes=automap_passes;
    ReleaseSRWLockExclusive(&automap_lock); ReleaseSRWLockShared(&lock);
    return out;
}
sc_campaign_result campaign_request(uint16_t operation,const sc_campaign_request& request) {
    AcquireSRWLockShared(&lock);
    auto scope=status.scope; scope.lifecycle_generation=lifetime.generation;
    const bool admitted=campaign_menu::available() && same_scope(scope,request.execution.expected) &&
        save::session().state()==save::SessionState::admitted && save::session().accepts_requests() &&
        save::session().namespace_id()==request.namespace_id;
    const auto out=campaign_menu::menu().request(operation,request,admitted);
    ReleaseSRWLockShared(&lock); return out;
}
sc_weapon_points_result submit_weapon_points(const sc_weapon_points_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.execution.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    if (!why && !weapon_points::admitted(request.namespace_id)) why = SC_NATIVE_SCOPE_MISMATCH;
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, nullptr, &request);
    auto out = weapon_points::initial(request);
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) out.execution = admitted;
    else out = diagnostics.points_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_weapon_points_result weapon_points_result(const sc_weapon_points_request& request, bool cancel, bool release) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.points_result(request, cancel, GetTickCount64(), release);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_inventory_result submit_inventory(const sc_inventory_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.execution.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    if (!why && !inventory::admitted(request.namespace_id)) why = SC_NATIVE_SCOPE_MISMATCH;
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, nullptr, nullptr, &request);
    auto out = inventory::initial(request);
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) out.execution = admitted;
    else out = diagnostics.inventory_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_inventory_result inventory_result(const sc_inventory_request& request, bool cancel, bool release) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.inventory_result(request, cancel, GetTickCount64(), release);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_arsenal_result submit_arsenal(const sc_arsenal_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.execution.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    if (!why && !arsenal::admitted(request.namespace_id)) why = SC_NATIVE_SCOPE_MISMATCH;
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, nullptr, nullptr, nullptr, &request);
    sc_arsenal_result out{}; arsenal::initial(request, out);
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) out.execution = admitted;
    else out = diagnostics.arsenal_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_arsenal_result arsenal_result(const sc_arsenal_request& request, bool cancel, bool release) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.arsenal_result(request, cancel, GetTickCount64(), release);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_runes_result submit_runes(const sc_runes_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.execution.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    if (!why && !runes::admitted(request.namespace_id)) why = SC_NATIVE_SCOPE_MISMATCH;
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, nullptr, nullptr, nullptr, nullptr, &request);
    sc_runes_result out = runes::initial(request);
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) out.execution = admitted;
    else out = diagnostics.runes_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_runes_result runes_result(const sc_runes_request& request, bool cancel, bool release) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.runes_result(request, cancel, GetTickCount64(), release);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_special_result submit_special(const sc_special_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.execution.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    if (!why && !special::available()) why = SC_NATIVE_BINDING_FAILED;
    if (!why && !special::admitted(request.namespace_id)) why = SC_NATIVE_SCOPE_MISMATCH;
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &request);
    sc_special_result out = special::initial(request);
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) out.execution = admitted;
    else out = diagnostics.special_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_special_result special_result(const sc_special_request& request, bool cancel, bool release) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.special_result(request, cancel, GetTickCount64(), release);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_deathlink_result submit_deathlink(const sc_deathlink_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && !same_scope(scope, request.execution.expected)) why = SC_NATIVE_SCOPE_MISMATCH;
    if (!why && !deathlink::admitted(request.namespace_id)) why = SC_NATIVE_SCOPE_MISMATCH;
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &request);
    sc_deathlink_result out = deathlink::initial(request);
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) out.execution = admitted;
    else out = diagnostics.deathlink_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_deathlink_result deathlink_result(const sc_deathlink_request& request, bool cancel, bool release) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.deathlink_result(request, cancel, GetTickCount64(), release);
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
save::CampaignTransition checkpoint_transition() {
    save::CampaignTransition result{};
    result.at_ms=GetTickCount64();
    if (owner_thread()!=GetCurrentThreadId()) { result.observation_reason=SC_NATIVE_WRONG_THREAD; return result; }
    AcquireSRWLockShared(&lock);
    result.generation_after=lifetime.generation; result.depth=lifetime.depth;
    result.observation_reason=fault.load(); result.observed=accepting.load();
    ReleaseSRWLockShared(&lock);
    return result;
}
#ifdef SC_NATIVE_TESTING
void test_events(const engine::Binding& source, uint64_t (*change)(uintptr_t,uintptr_t,uintptr_t)) {
    binding=source; fixture={}; fixture.owner=[]() -> uint32_t { return GetCurrentThreadId(); }; fixture_active=true;
    lifetime={}; lifetime.begin(true,true,false,GetTickCount64(),GetCurrentThreadId());
    lifetime.end(true,true,SC_GAME_MAIN_MENU,GetTickCount64(),GetCurrentThreadId());
    status={}; status.game_state=SC_GAME_MAIN_MENU; event_depth=0; event_thread=0;
    original_change=change; fault=0; gaps=0; accepting=true;
}
uint64_t test_change(uintptr_t root, uintptr_t descriptor, uintptr_t files) { return change_detour(root,descriptor,files); }
void test_free(uintptr_t root,void (*free)(uintptr_t,uintptr_t)) { original_free=free; free_detour(root,root+0x50); }
void test_generation_gap() { ++lifetime.generation; }
void test_dispatch_adapter(const TestAdapter& adapter, const sc_native_scope& scope) {
    fixture=adapter; fixture_active=true; status.scope=scope;
}
void test_post_frame() { post_frame(); }
bool test_automap_checked(const sc_automap_request& snapshot,const char* map,const char* name) {
    return automap_checked(snapshot,map,name);
}
bool test_automap_collect(uintptr_t system,uintptr_t key,uintptr_t object,void (*collect)(uintptr_t,uintptr_t)) {
    automap_collect=collect;
    return automap_set_collected(system,key,object);
}
void test_start(const TestAdapter& adapter, const Snapshot& identity, HANDLE stop_event) {
    fixture = adapter; fixture_active = true;
    engine::LocalMemory memory; engine::Binding source;
    engine::read_image(memory, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), source.image);
    source.root = fixture.root;
    start(source, identity, stop_event);
}
#endif
}
