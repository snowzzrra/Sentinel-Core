#include "engine/builds/build_descriptor.h"
#include "native_runtime.h"
#include "combat_native.h"
#include "expanded_native.h"
#include "combat_presentation.h"
#include "ap_controls_native.h"
#include "inventory.h"
#include "arsenal.h"
#include "runes.h"
#include "special.h"
#include "deathlink.h"
#include "commands_native.h"
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
#include "engine/hooks/hook_set.h"
#include <intrin.h>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <algorithm>
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
// pointer/name changes invalidate certainty. they never create a generation
uint64_t bound_generation = 0;
uintptr_t bound_map_address = 0;
sc_context_map bound_map_name{};
engine::Binding binding;
std::atomic<uint64_t> epoch{0}, gaps{0};
std::atomic<uint32_t> fault{SC_NATIVE_NONE};
std::atomic<bool> accepting{false}, pinned{false};
std::atomic<bool> stopping{true};
std::atomic<bool> hook_residue{false};
thread_local uint32_t frame_depth = 0;
thread_local uint32_t event_depth = 0;
thread_local bool loading_checkpoint = false;
std::atomic<uint32_t> event_thread{0};
using Frame = void (*)(uintptr_t);
using Change = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t);
using Free = void (*)(uintptr_t, uintptr_t);
Frame original_frame = nullptr;
Change original_change = nullptr;
Free original_free = nullptr;
std::array<Target,3> lifecycle_targets;
uint32_t lifecycle_reason = SC_NATIVE_NONE, lifecycle_history = 0;
MH_STATUS WINAPI enable_lifecycle(void* site);
hooks::HookSet lifecycle_hooks("native", {MH_CreateHook, enable_lifecycle, MH_DisableHook, MH_RemoveHook},
    &save::session().installation, 1);
struct RuntimeAttempt {
    hooks::Transaction groups;
    bool committed = false;
    ~RuntimeAttempt() {
        if (!committed) {
            accepting.store(false, std::memory_order_release);
            save::complete_native_installation(false, nullptr);
        }
    }
};

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
    if (!binding.descriptor) return 0;
    uintptr_t context = 0; uint64_t tid = 0;
    if (!read(binding.image.base + binding.descriptor->thread_context, context) || !context ||
        !read(context + binding.descriptor->thread_id_offset, tid) || !tid || tid > UINT32_MAX) return 0;
    return static_cast<uint32_t>(tid);
}
uintptr_t map_address() {
#ifdef SC_NATIVE_TESTING
    if (fixture_active) return fixture.map();
#endif
    if (!binding.descriptor) return 0;
    uintptr_t address = 0;
    return read(binding.root + binding.descriptor->map_offset, address) ? address : 0;
}
bool same_map(const sc_context_map& a, const sc_context_map& b) {
    return a.length == b.length && std::memcmp(a.bytes, b.bytes, a.length) == 0;
}
bool valid_frame(uintptr_t self, uintptr_t caller) {
    if (frame_depth != 1) { invalidate(SC_NATIVE_REENTRANT); return false; }
    uintptr_t expected_self = 0, expected_caller = 0;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) { expected_self = fixture.common; expected_caller = fixture.caller; }
    else
#endif
    {
        if (!binding.descriptor) { invalidate(SC_NATIVE_UNKNOWN_BUILD); return false; }
        expected_self = binding.image.base + binding.descriptor->common;
        expected_caller = binding.image.base + binding.descriptor->frame_caller;
    }
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
sc_evidence_scope evidence_scope(const sc_native_scope& scope, const sc_context_map& map) {
    sc_evidence_scope out{}; out.process = scope;
    const auto& session = save::session();
    const auto id = session.namespace_id(); const auto token = session.binding_token();
    if (id.size() == 64) std::memcpy(out.namespace_id, id.c_str(), 65);
    std::copy(token.begin(), token.end(), out.save_binding);
    if (map.validity == SC_OBSERVATION_OBSERVED && map.length < sizeof(out.map_name))
        std::memcpy(out.map_name, map.bytes, map.length + 1);
    if (id.size() == 64 && session.state() == save::SessionState::admitted && session.accepts_requests() &&
        (map.validity != SC_OBSERVATION_OBSERVED || map.length < sizeof(out.map_name)))
        out.readiness = SC_EVIDENCE_READY;
    return out;
}
registry::LiveState registry_live(const sc_native_scope& scope, uint32_t gameplay_reason) {
    registry::LiveState live{};
    live.scope = scope;
    live.process_reason = accepting.load(std::memory_order_acquire) ? fault.load(std::memory_order_acquire) : SC_NATIVE_STOPPED;
    live.gameplay_reason = gameplay_reason;
    live.save_admitted = save::session().state() == save::SessionState::admitted && save::session().accepts_requests();
    live.namespace_id = save::session().namespace_id();
    live.save_binding = save::session().binding_token();
    return live;
}
bool matches_entry_scope(const Diagnostics::Slot& slot, const sc_native_scope& scope, bool process_scoped) {
    auto expected = slot.request.expected;
    if (process_scoped && slot.identity) expected.lifecycle_generation = scope.lifecycle_generation;
    return same_scope(expected, scope);
}
bool matches_binding(const Diagnostics::Slot& slot, const registry::LiveState& live) {
    const auto* descriptor = registry::descriptor(static_cast<uint16_t>(slot.command_type), 1);
    const bool bound = descriptor && generated::admission_policies[static_cast<size_t>(descriptor->admission)].save_binding == generated::BindingRule::exact;
    return (!slot.identity && !slot.transient_read) || !bound || slot.save_binding == live.save_binding;
}
static bool checkpoint_flag(bool change, uintptr_t descriptor, uint8_t& flag) {
            bool known = false;
    #ifdef SC_NATIVE_TESTING
        if (fixture_active && fixture.checkpoint) known = change && fixture.checkpoint(descriptor, flag);
        else
    #endif
        known = change && read(descriptor + 0x1961, flag);
    return known;
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
    const bool known = checkpoint_flag(change, descriptor, flag);
    if (!TryAcquireSRWLockExclusive(&lock)) {
        invalidate(SC_NATIVE_EVENT_GAP);
        if (!--event_depth) event_thread.store(0, std::memory_order_release);
        return false;
    }
    if (transition) transition->generation_before=lifetime.generation;
    lifetime.begin(change, known, (flag & 2) != 0, GetTickCount64(), GetCurrentThreadId());
    combat::transition(lifetime.generation);
    if (event_depth == 1) loading_checkpoint = change && known && (flag & 2) != 0;
    if (transition) {
        transition->event_id=lifetime.sequence; transition->generation_after=lifetime.generation;
        transition->depth=lifetime.depth; transition->observed=true;
    }
    clear_context(SC_NATIVE_TRANSITION);
    ReleaseSRWLockExclusive(&lock);
    return true;
}
void end_event(bool change, bool success, bool abnormal, save::CampaignTransition* transition=nullptr) {
    if (event_depth == 1) loading_checkpoint = false;
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
            if (runes::admitted(id.c_str())) runes::bind_run_state_if_needed(player);
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
        {0xd5d660,"48895c2418574883ec70488b05671345034833c44889442458488bf9488bda48"},
        {0xda2360,"405356574881ecb0000000488b0566c640034833c448898424a8000000488b05"}
    };
    for (const auto& s:sites) {
        std::array<uint8_t,32> actual{},expected{};
        const auto digit=[](char c) { return static_cast<uint8_t>(c<='9'?c-'0':c-'a'+10); };
        for (size_t i=0;i<32;++i) expected[i]=static_cast<uint8_t>(digit(s.hex[i*2])*16+digit(s.hex[i*2+1]));
        if (!b.image.contains(s.rva,32,IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ,0) ||
            memory.copy(b.image.base+s.rva,actual.data(),32).reason || actual!=expected) return;
    }
    fast_travel_find=reinterpret_cast<FastTravelFind>(b.image.base+0x6ca290);
    fast_travel_activate=reinterpret_cast<FastTravelActivate>(b.image.base+0xd5d660);
    fast_travel_ready.store(true,std::memory_order_release);
}
bool arm_first_contact(uintptr_t target,uintptr_t activate,uintptr_t player) {
    if (!target || !activate || !player || *reinterpret_cast<const uint8_t*>(target+0xb9e)!=1) return false;
    reinterpret_cast<FastTravelActivate>(activate)(target,player);
    return *reinterpret_cast<const uint8_t*>(target+0xb9e)==0;
}
void repair_crystal_first_contact(uintptr_t map,const char* map_name,uintptr_t player) {
    if (!player || !fast_travel_ready.load(std::memory_order_acquire) ||
        !save::session().campaign_run.enabled() || std::strcmp(map_name,"game/hub/hub")) return;
    __try {
        const auto target=fast_travel_find(map,"ap_independent_progress_argent_cell_1_1072112848");
        if (!target || *reinterpret_cast<const uint8_t*>(target+0xb9e)!=1) return;
        const auto vtable=*reinterpret_cast<const uintptr_t*>(target);
        const auto activate=*reinterpret_cast<const uintptr_t*>(vtable+0x2e8);
        if (activate!=binding.image.base+0xda2360) return;
        // the first activation arms saved triggers without delivering their check.
        arm_first_contact(target,activate,player);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
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
        constexpr char target_name[]="ap_fast_travel_unlock";
        auto target=fast_travel_find(map,target_name);
        if (target && *reinterpret_cast<uintptr_t*>(target)!=image+0x2c39bf8) target=0;
        const auto native_target=fast_travel_find(map,"ap_fast_travel_unlock_native");
        if (!native_target || *reinterpret_cast<uintptr_t*>(native_target)!=image+0x2c47aa8) return;
        const bool target_unlocked=target && *reinterpret_cast<int32_t*>(target+0xb88)<0;
        const bool world_unlocked=*reinterpret_cast<uint8_t*>(map+0x1ac380)!=0;
        const auto decision=fast_travel::dispatch_policy().observe(true,generation,target,target_unlocked,world_unlocked);
        if (decision!=fast_travel::DispatchDecision::invoke) return;
        fast_travel::dispatch_policy().invoked(target,false);
        fast_travel_activate(target,player);
        const bool post=*reinterpret_cast<int32_t*>(target+0xb88)<0 &&
            *reinterpret_cast<uint8_t*>(native_target+0xb90)!=0 &&
            *reinterpret_cast<uint8_t*>(map+0x1ac380)!=0;
        fast_travel::dispatch_policy().invoked(target,post);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Native Automap collected-state ownership is scoped to exact catalog keys and maps.
// HUD idPOIManager is a different subsystem and MUST NOT be used on AP props.
SRWLOCK automap_lock = SRWLOCK_INIT;
sc_automap_request automap_snapshot{};
uint64_t automap_checked_floor[SC_AUTOMAP_LOCATION_BITS / 64]{};
uint64_t automap_scanned=0, automap_collected=0, automap_passes=0;
std::atomic<bool> automap_ready{false};
std::atomic<uint32_t> automap_fault{0};
uint64_t automap_generation=0;
uint32_t automap_cursor=0;
uintptr_t automap_keys=0;
using AutomapCollect=void(*)(uintptr_t,uintptr_t);
AutomapCollect automap_collect=nullptr;
using AutomapState=void(*)(uintptr_t,int);
AutomapState automap_state=nullptr;
#include "automap_catalog.h"

void install_automap(const engine::Binding& b) {
    engine::LocalMemory memory;
    const struct { uint32_t rva; const char* hex; } sites[] = {
        {0xa55f90,"40534883ec20448b8148030000488d057cdb01024c8bca4889442430488b9140"},
        {0xa604f0,"48895c2408574883ec208bfa488bd93b9134010000747485d27530488b491848"}
    };
    for (const auto& s:sites) {
        std::array<uint8_t,32> actual{}, expected{};
        const auto digit=[](char c) { return static_cast<uint8_t>(c<='9'?c-'0':c-'a'+10); };
        for (size_t i=0;i<32;++i) expected[i]=static_cast<uint8_t>(digit(s.hex[i*2])*16+digit(s.hex[i*2+1]));
        if (!b.image.contains(s.rva,32,IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ,0) ||
            memory.copy(b.image.base+s.rva,actual.data(),32).reason || actual!=expected) return;
    }
    automap_collect=reinterpret_cast<AutomapCollect>(b.image.base+0xa55f90);
    automap_state=reinterpret_cast<AutomapState>(b.image.base+0xa604f0);
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
int automap_status(const sc_automap_request& snapshot,const char* map,const char* name) {
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
        if (bit>=SC_AUTOMAP_LOCATION_BITS) return false;
        return (snapshot.checked_locations[bit/64] & (uint64_t{1}<<(bit%64)))!=0 ? 2 : 1;
    }
    return false;
}
bool automap_set_uncollected(uintptr_t object) {
    const auto state=*reinterpret_cast<int32_t*>(object+0x134);
    if (state==1 || state==2) return true;
    if (state!=0 || !*reinterpret_cast<uintptr_t*>(object+0x18)) return false;
    automap_state(object,1);
    return *reinterpret_cast<int32_t*>(object+0x134)==1 &&
        *reinterpret_cast<uintptr_t*>(object+0x28)==*reinterpret_cast<uintptr_t*>(object+0x18);
}
bool automap_set_collected(uintptr_t system,uintptr_t key,uintptr_t object) {
    if (*reinterpret_cast<int32_t*>(object+0x134)==3) return true;
    if (!automap_set_uncollected(object)) return false;
    automap_collect(system,key);
    return *reinterpret_cast<int32_t*>(object+0x134)==3;
}
// Called only by the already-admitted native frame, never by an IPC reader.
// Every pass rereads native keys/weak owners, including same-epoch reconstruction.
static bool collected_automap_entry(uintptr_t owner, const AutomapLists& list, const char* name) {
    AutomapLists after{}; char after_name[64]{};
    if (!automap_lists(owner,after) || after.count!=list.count ||
        after.keys!=list.keys || after.values!=list.values ||
        !automap_name(after.keys+static_cast<uintptr_t>(automap_cursor)*0x30,after_name) ||
        std::strcmp(name,after_name) ||
        *reinterpret_cast<int32_t*>(after.values+static_cast<uintptr_t>(automap_cursor)*0x150+0x134)!=3) {
        return false;
    }
    return true;
}
static void reconcile_automap_entries(uintptr_t system, uintptr_t owner, const sc_automap_request& snapshot, const char* map_name, AutomapLists& list, uint64_t& scanned, uint64_t& collected, uint64_t& passes) {
    for (unsigned budget=0;budget<16 && collected<4;++budget) {
        if (!automap_lists(owner,list)) { automap_fault.store(1); break; }
        if (automap_cursor>=static_cast<uint32_t>(list.count)) {
            automap_cursor=0; ++passes; break;
        }
        const auto key=list.keys+static_cast<uintptr_t>(automap_cursor)*0x30;
        const auto object=list.values+static_cast<uintptr_t>(automap_cursor)*0x150;
        char name[64]{}; ++scanned;
        if (!automap_name(key,name)) {
            ++automap_cursor; continue;
        }
        const auto marker_status=automap_status(snapshot,map_name,name);
        if (marker_status==1) {
            automap_set_uncollected(object);
            ++automap_cursor; continue;
        }
        if (marker_status!=2 || *reinterpret_cast<int32_t*>(object+0x134)==3) {
            ++automap_cursor; continue;
        }
        if (!automap_set_collected(system,key,object)) { ++automap_cursor; continue; }
        if (!collected_automap_entry(owner, list, name)) { automap_cursor=0; break; }
        ++collected; ++automap_cursor;
    }
}
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
        reconcile_automap_entries(system, owner, snapshot, map_name, list, scanned, collected, passes);
    } __except(EXCEPTION_EXECUTE_HANDLER) { automap_fault.store(GetExceptionCode()); }
    AcquireSRWLockExclusive(&automap_lock);
    automap_scanned+=scanned; automap_collected+=collected; automap_passes+=passes;
    ReleaseSRWLockExclusive(&automap_lock);
}

struct FrameCost {
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last=start;
    std::array<uint64_t,6> parts{};
    void mark(size_t index) {
        const auto now=std::chrono::steady_clock::now();
        parts[index]=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now-last).count());
        last=now;
    }
    ~FrameCost() {
        static uint64_t calls=0,total=0,maximum=0,slow=0,next=0;
        static std::array<uint64_t,6> sums{},maxima{};
        for (size_t i=0;i<parts.size();++i) { sums[i]+=parts[i]; maxima[i]=std::max(maxima[i],parts[i]); }
        const auto us=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-start).count());
        ++calls; total+=us; maximum=std::max(maximum,us); slow+=us>=16000;
        const auto now=GetTickCount64();
        if (now<next) return;
        next=now+5000;
        save::session().btrace.record(save::BStage::native_frame_cost,save::BStatus::succeeded,"native_frame_cost_us",0,
            {{"calls",calls},{"total_us",total},{"max_us",maximum},{"over_16ms",slow}});
        save::session().btrace.record(save::BStage::native_frame_parts,save::BStatus::succeeded,"native_frame_parts_us",0,
            {{"calls",calls},{"context_us",sums[0]},{"player_us",sums[1]},{"travel_us",sums[2]},
             {"automap_us",sums[3]},{"combat_us",sums[4]},{"arsenal_us",sums[5]},
             {"context_max_us",maxima[0]},{"player_max_us",maxima[1]},{"travel_max_us",maxima[2]},
             {"automap_max_us",maxima[3]},{"combat_max_us",maxima[4]},{"arsenal_max_us",maxima[5]}});
    }
};
void observe_mission_access(uint32_t why, uintptr_t player, bool recovery_pending, uint64_t stamp,
                           const sc_native_scope& scope, const sc_context_snapshot& facts) {
    if (!why && player && !recovery_pending && gameplay_admitted() && stamp == epoch.load(std::memory_order_acquire))
        campaign_menu::menu().destination_ready(evidence_scope(scope, facts.current_map));
}
void invalidate_changed_evidence(generated::NativeResult& result, uint64_t stamp, const std::array<uint8_t, 16>& token) {
    if (epoch.load(std::memory_order_acquire) == stamp && !fault.load(std::memory_order_acquire) &&
        save::session().accepts_requests() && token == save::session().binding_token()) return;
    std::visit([](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, sc_currency_result> || std::is_same_v<T, sc_progression_result>)
            value.after_scope.readiness = SC_EVIDENCE_UNPROVEN;
    }, result);
}
static void reject_frame(uint32_t& why, sc_diagnostic_detail& detail, uint32_t reason, uint32_t stage) {
    why = reason; detail.stage = stage;
}
static void check_frame_stamp(uint64_t before, uint32_t& why, sc_diagnostic_detail& detail) {
    if (!why && (epoch.load(std::memory_order_acquire) != before || fault.load(std::memory_order_acquire)))
        reject_frame(why, detail, fault.load() ? fault.load() : SC_NATIVE_EVENT_GAP, SC_STAGE_EVENT_STAMP);
}
static void observe_frame_context(engine::LocalMemory& memory, sc_context_snapshot& facts, sc_diagnostic_detail& detail, sc_diagnostic_result& result, uint32_t& why, uint8_t& pending_after) {
                engine::ReadResult after_read{};
                context::Evidence measured{};
                detail.observation_attempted = 1;
    #ifdef SC_NATIVE_TESTING
                if (fixture_active) { facts = fixture.observe(measured); pending_after = fixture.pending(); }
                else
    #endif
                { facts = context::sample(memory, binding, status.callback_sequence, nullptr, context::native_budget_ms, &measured);
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
                if (after_read.reason || pending_after) reject_frame(why, detail, after_read.reason ? SC_NATIVE_READ_FAILED : SC_NATIVE_TRANSITION, SC_STAGE_PENDING_AFTER);
                else if (facts.sample_reason == SC_REASON_BUDGET) {
                    reject_frame(why, detail, SC_NATIVE_BUDGET, SC_STAGE_OBSERVATION_BUDGET);
                    save::session().btrace.record(save::BStage::native_observation, save::BStatus::refused, "native_budget", 0,
                        {{"phase", result.phase}, {"stage", detail.stage}, {"elapsed_ns", detail.observation_elapsed_ns},
                         {"budget_ns", detail.observation_budget_ns}, {"timing_valid", detail.timing_valid},
                         {"state_reason", detail.state_reason}, {"map_reason", detail.map_reason}});
                }
                else if (facts.current_map.validity != SC_OBSERVATION_OBSERVED ||
                         facts.fields[SC_CONTEXT_GAME_STATE].validity != SC_OBSERVATION_OBSERVED ||
                         facts.fields[SC_CONTEXT_GAME_STATE].value != SC_GAME_IN_GAME)
                    reject_frame(why, detail, SC_NATIVE_CONTEXT_UNAVAILABLE, SC_STAGE_FRESH_CONTEXT);
                // accepted reader facts alone don't authorize the diagnostic body
                detail.observation_accepted = facts.current_map.validity == SC_OBSERVATION_OBSERVED &&
                    facts.fields[SC_CONTEXT_GAME_STATE].validity == SC_OBSERVATION_OBSERVED &&
                    facts.fields[SC_CONTEXT_GAME_STATE].value == SC_GAME_IN_GAME;
}
static void read_frame_context(engine::LocalMemory& memory, sc_context_snapshot& facts, sc_diagnostic_detail& detail, sc_diagnostic_result& result, uint32_t& why) {
    uint8_t pending = 1, pending_after = 1;
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
            if (pending_read.reason || pending) reject_frame(why, detail, pending_read.reason ? SC_NATIVE_READ_FAILED : SC_NATIVE_TRANSITION, SC_STAGE_PENDING_BEFORE);
            else {
                observe_frame_context(memory, facts, detail, result, why, pending_after);
            }
        }
}
static void validate_frame_binding(uintptr_t expected_map_address, const sc_context_map& expected_map_name, const sc_context_snapshot& facts, uint32_t& why, sc_diagnostic_detail& detail) {
    if (!why && (map_address() != expected_map_address || !same_map(facts.current_map, expected_map_name))) {
        invalidate(SC_NATIVE_EVENT_GAP); reject_frame(why, detail, SC_NATIVE_EVENT_GAP, SC_STAGE_MAP_BINDING);
    }
    if (!why && owner_thread() != GetCurrentThreadId()) { invalidate(SC_NATIVE_WRONG_THREAD); reject_frame(why, detail, SC_NATIVE_WRONG_THREAD, SC_STAGE_NATIVE_FAULT); }
    if (!why && !accepting.load(std::memory_order_acquire)) reject_frame(why, detail, SC_NATIVE_STOPPED, SC_STAGE_NATIVE_FAULT);
}
static bool frame_recovery_gameplay(bool recovery_pending, uint32_t why, uintptr_t player, const sc_context_snapshot& facts) {
    bool recovery_gameplay = false;
    if (recovery_pending) {
        const auto recovery_campaign = save::session().campaign_run.snapshot();
        recovery_gameplay = !why && player && gameplay_admitted_locked_scope() &&
        ap_controls::recovery_input_allowed() && deathlink::player_alive(player) && expanded::recovery_player_allowed(player) &&
        (std::strncmp(facts.current_map.bytes, "game/sp/", 8) == 0 || std::strncmp(facts.current_map.bytes, "game/dlc/", 9) == 0 ||
         std::strncmp(facts.current_map.bytes, "game/dlc2/", 10) == 0) &&
        recovery_campaign.map_active && !recovery_campaign.slot.empty();
    }
    return recovery_gameplay;
}
static void observe_frame_combat(uint32_t why, uintptr_t player, bool recovery_gameplay, const sc_native_scope& scope, uint64_t before, uintptr_t expected_map_address, const sc_context_snapshot& facts) {
    combat::Context combat_context{};
    if (!why && player && (gameplay_admitted() || recovery_gameplay)) {
        commands::checkpoint_player_position(player, scope.lifecycle_generation);
        combat_context.generation = scope.lifecycle_generation;
        combat_context.stamp = before;
        combat_context.sampled_at_ms = GetTickCount64();
        combat_context.world = expected_map_address;
        combat_context.player = player;
        combat_context.owner_thread = GetCurrentThreadId();
        combat_context.map = facts.current_map;
        const auto& namespace_id = save::session().namespace_id();
        if (namespace_id.size() == 64) std::memcpy(combat_context.namespace_id, namespace_id.c_str(), 65);
    }
    combat::observe(combat_context);
    if (combat_context.world) expanded::observe(combat_context.world);
}
static void observe_frame_recovery(bool recovery_pending, bool recovery_gameplay, uint64_t before, uintptr_t expected_map_address, uintptr_t player, const sc_native_scope& scope, const sc_context_snapshot& facts) {
    if (recovery_pending) {
        recovery::Context recovery_context;
        const auto campaign = save::session().campaign_run.snapshot();
        if (recovery_gameplay && before == epoch.load(std::memory_order_acquire))
            recovery_context = expanded::recovery_context(expected_map_address, facts.current_map.bytes);
        recovery_context.world = expected_map_address; recovery_context.player = player;
        recovery_context.scope = scope; recovery_context.stamp = before;
        recovery_context.namespace_id = save::session().namespace_id();
        recovery_context.save = save::session().native_root() + "/" + campaign.slot;
        recovery_context.map = facts.current_map.bytes;
        for (auto& manager : recovery_context.managers)
            manager.protected_fight = recovery::protected_manager(recovery_context.map, manager.name);
        recovery_context.gameplay = recovery_gameplay && before == epoch.load(std::memory_order_acquire);
        recovery::queue().observe(std::move(recovery_context), GetTickCount64());
    }
}
static const registry::Handler* admit_frame_slot(Diagnostics::Slot* slot, const sc_native_scope& scope, uint64_t before, uint32_t& why, sc_diagnostic_detail& detail, bool& process_scoped) {
    process_scoped = slot->is<sc_command_request>() && commands::process_scoped(slot->get<sc_command_request>());
    if (process_scoped) why = accepting.load(std::memory_order_acquire) ? fault.load(std::memory_order_acquire) : SC_NATIVE_STOPPED;
    check_frame_stamp(before, why, detail);
    if (!matches_entry_scope(*slot, scope, process_scoped)) reject_frame(why, detail, SC_NATIVE_SCOPE_MISMATCH, SC_STAGE_SCOPE);
    const auto* handler = registry::handler(slot->command_type);
    const auto live = registry_live(scope, why);
    if (!why && handler) {
        const auto reason = handler->admit(slot->payload, live);
        if (reason) reject_frame(why, detail, reason, SC_STAGE_SCOPE);
    }
    if (!handler) reject_frame(why, detail, SC_NATIVE_BINDING_FAILED, SC_STAGE_NATIVE_FAULT);
    if (!why && !matches_binding(*slot, live))
        reject_frame(why, detail, SC_NATIVE_SCOPE_MISMATCH, SC_STAGE_SCOPE);
    return handler;
}
static void prepare_frame_backup(Diagnostics::Slot* slot, uint32_t& why, sc_diagnostic_detail& detail, char (&backup_directory)[64]) {
    if (!why && slot->is<sc_save_backup_request>()) {
        const auto reason = backup_prerequisite(slot->get<sc_save_backup_request>());
        if (reason) reject_frame(why, detail, reason, SC_STAGE_NATIVE_FAULT);
        else {
            constexpr const char* campaigns[] = {"GAME-", "DLC1-", "DLC2-"};
            sprintf_s(backup_directory, "%s/%sAUTOSAVE%u", save::session().native_root().c_str(),
                campaigns[slot->get<sc_save_backup_request>().campaign], slot->get<sc_save_backup_request>().slot);
        }
    }
}
static void execute_frame_slot(Diagnostics::Slot* slot, const sc_native_scope& scope, uint64_t before, uint32_t why, sc_diagnostic_detail detail, sc_diagnostic_result result, uintptr_t player, const sc_context_snapshot& facts) {
    if (!slot) return;
    bool process_scoped = false;
    const auto* handler = admit_frame_slot(slot, scope, before, why, detail, process_scoped);
    const auto live = registry_live(scope, why);
    char backup_directory[64]{};
    prepare_frame_backup(slot, why, detail, backup_directory);
    if (!why && slot->cancel.load(std::memory_order_acquire)) reject_frame(why, detail, SC_NATIVE_CANCELLED, SC_STAGE_CANCELLATION);
    const auto execution_at = GetTickCount64();
    if (!why && execution_at >= result.deadline_at_ms) reject_frame(why, detail, SC_NATIVE_DEADLINE, SC_STAGE_DEADLINE);
    // Linearization point of the only diagnostic body. Cancellation after this
    // check may coexist with EXECUTED; a caller timeout is never proof otherwise.
    if (!why) {
        detail.stage = SC_STAGE_EXECUTED;
        result.observed_at_ms = process_scoped ? execution_at : facts.sampled_at_ms;
        result.executed_at_ms = execution_at;
        result.current_map = facts.current_map;
        result.game_state = static_cast<uint32_t>(facts.fields[SC_CONTEXT_GAME_STATE].value);
        result.state = SC_DIAGNOSTIC_EXECUTED;
        registry::ExecutionContext context{};
        context.evidence_scope = evidence_scope(scope, facts.current_map);
        context.player = player; context.backup = slot->backup; context.backup_directory = backup_directory;
        slot->entered.store(true, std::memory_order_release);
        handler->execute(slot->payload, slot->value, context);
        invalidate_changed_evidence(slot->value, before, live.save_binding);
        if (slot->is<sc_save_backup_request>()) {
            Diagnostics::await_backup(*slot, result, detail, context.submission);
            return;
        }
        slot->evidence = handler->classify(slot->payload, slot->value, result);
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

static void reconcile_frame_world(FrameCost& cost, uintptr_t expected_map_address, const sc_context_snapshot& facts, const sc_native_scope& scope, uintptr_t player, uint64_t before, uint32_t& why, sc_diagnostic_detail& detail) {
    cost.mark(1);
    if (!why) reconcile_fast_travel(expected_map_address,facts.current_map.bytes,scope.lifecycle_generation,player);
    check_frame_stamp(before, why, detail);
    cost.mark(2);
    if (!why) reconcile_automap(expected_map_address, facts.current_map.bytes, scope.lifecycle_generation);
    cost.mark(3);
}
static void reconcile_frame_inventory(FrameCost& cost, uint32_t why, uintptr_t player, uintptr_t expected_map_address, const sc_context_snapshot& facts, const sc_native_scope& scope) {
    cost.mark(4);
    if (!why && player) repair_crystal_first_contact(expected_map_address,facts.current_map.bytes,player);
    if (!why && player) arsenal::tick_masteries(scope.lifecycle_generation, player);
    cost.mark(5);
}
static void tick_native_console() {
    #ifdef SC_NATIVE_TESTING
        if (!(fixture_active && fixture.command_fixture && fixture.command_fixture()))
    #endif
        commands::console_tick(epoch.load(std::memory_order_acquire));
}
void post_frame() {
    if (!accepting.load(std::memory_order_acquire) || fault.load(std::memory_order_acquire)) return;
    if (owner_thread() != GetCurrentThreadId()) { invalidate(SC_NATIVE_WRONG_THREAD); return; }
    FrameCost cost;
    tick_native_console();
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
    if (slot && slot->is<sc_save_backup_request>()) result.scope = slot->request.expected;
    result.site_revision = 1; result.phase = 1; result.lifecycle = life;
    engine::LocalMemory memory;
    sc_context_snapshot facts{};
    read_frame_context(memory, facts, detail, result, why);
    check_frame_stamp(before, why, detail);
    validate_frame_binding(expected_map_address, expected_map_name, facts, why, detail);
    // autonomous work uses the same fresh native admission as ipc, even with zero diagnostic slots. a diagnostic query is never its actuator
    cost.mark(0);
    const auto player=!why ? tick_player_safely(binding.image.base + 0x69af70, expected_map_address, scope.lifecycle_generation) : 0;
    if (!player && owner_thread() == GetCurrentThreadId()) special::poll_input(0, false);
    // player natives may call back into a lifecycle hook before returning
    check_frame_stamp(before, why, detail);
    reconcile_frame_world(cost, expected_map_address, facts, scope, player, before, why, detail);
    const bool recovery_pending = recovery::queue().has_pending();
    bool recovery_gameplay = false;
    recovery_gameplay = frame_recovery_gameplay(recovery_pending, why, player, facts);
    observe_frame_combat(why, player, recovery_gameplay, scope, before, expected_map_address, facts);
    reconcile_frame_inventory(cost, why, player, expected_map_address, facts, scope);
    observe_frame_recovery(recovery_pending, recovery_gameplay, before, expected_map_address, player, scope, facts);
    commands::manual_tick(why ? 0 : player, before, []() { return epoch.load(std::memory_order_acquire); }, recovery_pending);
    observe_mission_access(why, player, recovery_pending, before, scope, facts);
    execute_frame_slot(slot, scope, before, why, detail, result, player, facts);
}
void frame_detour(uintptr_t self, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
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
static bool primary_freed_map(uintptr_t root, uintptr_t slot, uintptr_t& map, bool& readable) {
            bool primary = root == binding.root && slot == root + 0x50;
        readable = !primary || read(slot, map);
    #ifdef SC_NATIVE_TESTING
        if (fixture_active && fixture.primary) { primary = root == binding.root && fixture.primary(slot, map); readable = true; }
    #endif
    return primary;
}
void free_detour(uintptr_t root, uintptr_t slot) {
    uintptr_t map = 0; bool readable = false;
    const bool primary = primary_freed_map(root, slot, map, readable);
    if (primary && !readable && accepting.load()) invalidate(SC_NATIVE_READ_FAILED);
    const bool observe = primary && readable && map && begin_event(root, false, 0);
    __try { original_free(root, slot); }
    __finally {
        if (primary && readable && map) expanded::map_freed();
        if (observe) end_event(false, true, AbnormalTermination() != FALSE);
    }
}
}

void prepare(const Snapshot& identity) {
    combat::prepare(identity);
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
void frame_detour_entry(uintptr_t self) {
    const auto caller = _ReturnAddress();
    const auto lease = lifecycle_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_frame)>(lease.original)(self);
        return frame_detour(self, caller);
    } __finally { lifecycle_hooks.leave(lease); }
}

uint64_t change_detour_entry(uintptr_t root, uintptr_t descriptor, uintptr_t files) {
    const auto lease = lifecycle_hooks.enter(1);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_change)>(lease.original)(root, descriptor, files);
        return change_detour(root, descriptor, files);
    } __finally { lifecycle_hooks.leave(lease); }
}

void free_detour_entry(uintptr_t root, uintptr_t slot) {
    const auto lease = lifecycle_hooks.enter(2);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_free)>(lease.original)(root, slot);
        return free_detour(root, slot);
    } __finally { lifecycle_hooks.leave(lease); }
}

namespace {
bool retain_module() {
    auto& installation = save::session().installation;
    if (!pinned.load(std::memory_order_acquire)) {
        HMODULE module = nullptr;
        auto event = installation.begin(SC_INSTALL_PIN);
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(frame_detour_entry), &module)) {
            lifecycle_reason = SC_NATIVE_PIN_FAILED;
            installation.finish(event, lifecycle_reason, SC_INSTALL_UNKNOWN, GetLastError());
            return false;
        }
        installation.finish(event);
        pinned.store(true, std::memory_order_release);
    }
    return true;
}
MH_STATUS WINAPI enable_lifecycle(void* site) {
    auto& installation = save::session().installation;
    if (!retain_module()) return MH_ERROR_MEMORY_ALLOC;
    const auto index = static_cast<size_t>(std::find_if(lifecycle_targets.begin(), lifecycle_targets.end(),
        [&](const auto& target) { return target.address == reinterpret_cast<uintptr_t>(site); }) - lifecycle_targets.begin());
    const auto& target = lifecycle_targets[index];
    engine::LocalMemory memory;
    std::array<uint8_t,32> current{};
    auto event = installation.begin(SC_INSTALL_NATIVE_BINDING, 1, static_cast<uint32_t>(index),
        static_cast<uint32_t>(target.address - binding.image.base));
    const auto result = memory.copy(target.address, current.data(), current.size());
    save::attach_read(event, result); event.byte_count = 32;
    std::memcpy(event.expected_bytes, target.bytes.data(), 32);
    std::memcpy(event.actual_bytes, current.data(), 32);
    if (result.reason || current != target.bytes) {
        lifecycle_reason = SC_NATIVE_TARGET_BYTES;
        installation.finish(event, lifecycle_reason);
        return MH_ERROR_UNSUPPORTED_FUNCTION;
    }
    installation.finish(event);
    const auto enabled = MH_EnableHook(site);
    if (enabled == MH_OK) lifecycle_history |= 1u << index;
    return enabled;
}

}
void install_world_domains(uint32_t why, HANDLE stop_event) {
    if (!why && !stopping.load(std::memory_order_acquire)) deathlink::install(binding, stop_event);
    if (!why && save::session().campaign_run.snapshot().restart_enabled && !deathlink::available())
        save::session().campaign_run.restart_failed("restart_death_hook_unavailable");
    if (!why && !stopping.load(std::memory_order_acquire)) combat::install(binding, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire)) install_automap(binding);
    if (!why && !stopping.load(std::memory_order_acquire)) install_fast_travel(binding);
}
void install_optional_domains(uint32_t why, HANDLE stop_event) {
    // validate the currency seam after WUP patches the shared entry.
    if (!why && !stopping.load(std::memory_order_acquire)) weapon_points::install(binding, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire)) challenge::install(binding, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire)) inventory::install(binding);
    if (!why && !stopping.load(std::memory_order_acquire)) arsenal::install(binding, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire)) runes::install(binding);
    install_world_domains(why, stop_event);
}
void validate_menu_targets(uint32_t& why, engine::Memory& memory, save::Installation& installation, HANDLE stop_event) {
    // check menu callees before domain hooks patch shared swf entries
    if (!why && !stopping.load(std::memory_order_acquire) && save::session().campaign_requested() &&
        !campaign_menu::validate_native_targets(installation, memory, binding.image, stop_event,
                                                builds::steam_20260818().menu_targets(binding.image.base))) {
        save::session().campaign_run.refuse("native_campaign_menu_installation_failed");
        why = SC_NATIVE_EXCEPTION;
    }
}
void install_domains(uint32_t& why, engine::Memory& memory, save::Installation& installation, HANDLE stop_event) {
    validate_menu_targets(why, memory, installation, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire) && !ap_controls::install(binding, stop_event)) {
        save::session().campaign_run.refuse("native_ap_controls_installation_failed");
        why = SC_NATIVE_EXCEPTION;
    }
    // hud validation needs the shared string entry before enemizer hooks it.
    if (!why && !stopping.load(std::memory_order_acquire)) special::install(binding, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire) && !expanded::install(binding, stop_event)) {
        save::session().campaign_run.refuse("native_expanded_installation_failed");
        why = SC_NATIVE_EXCEPTION;
    }
    install_optional_domains(why, stop_event);
    if (!why && !stopping.load(std::memory_order_acquire) && !campaign_menu::install(binding, stop_event)) {
        save::session().campaign_run.refuse("native_campaign_menu_installation_failed");
        why = SC_NATIVE_EXCEPTION;
    }
}
static auto validate_runtime_binding(save::Installation& installation, engine::LocalMemory& memory, const std::array<Target, 3>& targets, sc_native_snapshot& initial, bool profile) {
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
            if (!initial.reason && check_binding && (binding.root != binding.image.base + binding.descriptor->root_rva ||
                !observed_read(binding.image.base + binding.descriptor->common_pointer, common) || common != binding.image.base + binding.descriptor->common ||
                !observed_read(common, vtable) || vtable != binding.image.base + binding.descriptor->common_vtable ||
                !observed_read(vtable + binding.descriptor->frame_slot, frame) || frame != targets[0].address)) initial.reason = SC_NATIVE_BINDING_FAILED;
        }
    return binding_event;
}
static void rollback_runtime(RuntimeAttempt& attempt, uint32_t why, HANDLE stop_event, const sc_native_snapshot& initial) {
        accepting.store(false, std::memory_order_release);
        save::complete_native_installation(false, stop_event);
        commands::disable();
        const auto clean = attempt.groups.rollback();
        if (!clean) hook_residue.store(true, std::memory_order_release);
        const bool kept = clean || retain_module();
        save::session().btrace.record(save::BStage::native_start, save::BStatus::refused,
            "native_installation_rollback", 0, {{"reason", why}, {"cleanup_complete", clean}, {"code_retained", kept}});
        AcquireSRWLockExclusive(&lock);
        status.installed_hooks = initial.installed_hooks; status.retained_module = pinned.load();
        status.reason = why; status.coverage = 0; status.availability = SC_NATIVE_DISABLED;
        ReleaseSRWLockExclusive(&lock);
}
static uint32_t publish_runtime_hooks(uint32_t why, HANDLE stop_event, const sc_native_snapshot& initial) {
    if (!why) {
        AcquireSRWLockExclusive(&lock);
        status.installed_hooks = initial.installed_hooks; status.retained_module = pinned.load();
        status.reason = SC_NATIVE_NONE; status.coverage = 7; status.availability = SC_NATIVE_ENABLED;
        accepting.store(true, std::memory_order_release);
        ReleaseSRWLockExclusive(&lock);
        if (!save::complete_native_installation(true, stop_event) || stopping.load(std::memory_order_acquire) ||
            (stop_event && WaitForSingleObject(stop_event, 0) != WAIT_TIMEOUT)) {
            accepting.store(false, std::memory_order_release);
            why = stopping.load(std::memory_order_acquire) ? SC_NATIVE_CANCELLED : SC_NATIVE_BINDING_FAILED;
        }
    }
    return why;
}
static uint32_t install_lifecycle_hooks(save::Installation& installation, const std::array<Target, 3>& targets, HANDLE stop_event) {
    uint32_t why = SC_NATIVE_NONE;
    const auto mh = installation.hook(SC_INSTALL_MH_INITIALIZE, 0, SC_INSTALL_UNKNOWN, 0, [] { return MH_Initialize(); });
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) why = SC_NATIVE_HOOK_FAILED;
    lifecycle_targets = targets; lifecycle_reason = SC_NATIVE_NONE; lifecycle_history = 0;
    const hooks::HookSpec specs[]{
        hooks::bind(targets[0].address, frame_detour_entry, original_frame),
        hooks::bind(targets[1].address, change_detour_entry, original_change),
        hooks::bind(targets[2].address, free_detour_entry, original_free)};
    if (!why && !lifecycle_hooks.install(specs, stop_event))
        why = lifecycle_reason ? lifecycle_reason :
            lifecycle_hooks.inspect().first_error.operation == hooks::Operation::cancel ? SC_NATIVE_CANCELLED : SC_NATIVE_HOOK_FAILED;
    return why;
}
static void install_runtime_hooks(save::Installation& installation, engine::LocalMemory& memory, const std::array<Target, 3>& targets, HANDLE stop_event, sc_native_snapshot& initial) {
            RuntimeAttempt attempt;
            uint32_t why = SC_NATIVE_NONE;
            why = install_lifecycle_hooks(installation, targets, stop_event);
            initial.installed_hooks = lifecycle_history;
    #ifdef SC_NATIVE_TESTING
            if (testing::real_hook_targets()) {
    #endif
            if (!why && !stopping.load(std::memory_order_acquire)) save::install_native_hooks(binding, stop_event);
            if (!why && save::session().campaign_requested() && save::session().state() != save::SessionState::prepared)
                why = SC_NATIVE_BINDING_FAILED;
            install_domains(why, memory, installation, stop_event);
    #ifdef SC_NATIVE_TESTING
            }
    #endif
            if (!why && (stopping.load(std::memory_order_acquire) ||
                (stop_event && WaitForSingleObject(stop_event, 0) != WAIT_TIMEOUT))) why = SC_NATIVE_CANCELLED;
            why = publish_runtime_hooks(why, stop_event, initial);
            if (why) { rollback_runtime(attempt, why, stop_event, initial);
            } else { attempt.groups.commit(); attempt.committed = true; }
}
void start(const engine::Binding& source, const Snapshot& identity, HANDLE stop_event) {
    auto& installation = save::session().installation;
    (void)context::observation_clock(); // cache qpc frequency before any hook is reachable
    AcquireSRWLockExclusive(&startup);
    if (pinned.load(std::memory_order_acquire) || hook_residue.load(std::memory_order_acquire) ||
        stopping.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&startup); return;
    }
    accepting.store(false, std::memory_order_release);
    binding = source;
    const auto command_reason = commands::prepare(binding, stop_event);
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
        targets[i] = builds::steam_20260818().profile_target(binding.image.base, i);
#ifdef SC_NATIVE_TESTING
        if (fixture_active) targets[i] = fixture.targets[i];
#endif
        initial.validator_reasons[i] = validate_recorded(installation, memory, binding.image, targets[i], stop_event, deadline, 1, i);
    }
    bool profile = binding.metadata.profile == SC_PROFILE_STEAM_20260818;
#ifdef SC_NATIVE_TESTING
    if (fixture_active) { profile = true; initial.site_rva = static_cast<uint32_t>(targets[0].address - binding.image.base); }
#endif
    const auto binding_event = validate_runtime_binding(installation, memory, targets, initial, profile);
    installation.finish(binding_event, initial.reason);
#ifdef SC_NATIVE_TESTING
    if (!fixture_active && !initial.reason) initial.reason = command_reason;
#else
    if (!initial.reason) initial.reason = command_reason;
#endif
    // publish fixed binding/status before any detour can become reachable
    AcquireSRWLockExclusive(&lock);
    lifetime = {}; status = initial; fault.store(SC_NATIVE_NONE); gaps.store(0); epoch.fetch_add(1);
    ReleaseSRWLockExclusive(&lock);
    if (!initial.reason && !stopping.load() && WaitForSingleObject(stop_event, 0) == WAIT_TIMEOUT) {
        install_runtime_hooks(installation, memory, targets, stop_event, initial);
    }
    ReleaseSRWLockExclusive(&startup);
}
bool gameplay_admitted() { return !ap_controls::input_inhibited() && gameplay_admitted_locked_scope(); }
bool checkpoint_loading() {
    return loading_checkpoint && event_depth == 1 &&
        event_thread.load(std::memory_order_acquire) == GetCurrentThreadId() &&
        accepting.load(std::memory_order_acquire) &&
        !stopping.load(std::memory_order_acquire) && !fault.load(std::memory_order_acquire);
}
uint64_t observation_stamp() { return epoch.load(std::memory_order_acquire); }
static bool active_context_epoch(uint64_t before) {
    return accepting.load() && !fault.load() && before == epoch.load() && lifetime.state == SC_LIFETIME_ACTIVE && !lifetime.depth;
}
void publish_context(const sc_context_snapshot& observed, uint64_t before) {
    special::refresh_input_config(); // observer thread: no game-thread file i/o
#ifdef SC_NATIVE_TESTING
    const auto& value = fixture_active ? fixture.context() : observed;
#else
    const auto& value = observed;
#endif
    const auto address = map_address();
    AcquireSRWLockExclusive(&lock);
    if (active_context_epoch(before) &&
        value.fields[SC_CONTEXT_GAME_STATE].validity == SC_OBSERVATION_OBSERVED &&
        value.fields[SC_CONTEXT_GAME_STATE].value != SC_GAME_IN_GAME) invalidate(SC_NATIVE_EVENT_GAP);
    if (active_context_epoch(before) &&
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
sc_evidence_scope evidence_scope() {
    const auto snapshot = inspect();
    auto out = evidence_scope(snapshot.scope, snapshot.current_map);
    if (snapshot.reason || snapshot.depth || (snapshot.lifecycle != SC_LIFETIME_ACTIVE && snapshot.lifecycle != SC_LIFETIME_MENU))
        out.readiness = SC_EVIDENCE_UNPROVEN;
    return out;
}
uint32_t namespace_reason(const generated::submission_identity& reference, const generated::AdmissionPolicy& policy,
        const registry::LiveState& live) {
    if (policy.namespace_rule == generated::NamespaceRule::process_zero &&
        !std::all_of(reference.namespace_id.begin(), reference.namespace_id.end(), [](uint8_t value) { return value == '0'; }))
        return SC_NATIVE_SCOPE_MISMATCH;
    if (policy.namespace_rule != generated::NamespaceRule::admitted_save) return SC_NATIVE_NONE;
    if (!live.save_admitted) return SC_NATIVE_BINDING_FAILED;
    const bool matches = live.namespace_id.size() == reference.namespace_id.size() &&
        std::equal(reference.namespace_id.begin(), reference.namespace_id.end(), live.namespace_id.begin());
    return matches ? SC_NATIVE_NONE : SC_NATIVE_SCOPE_MISMATCH;
}
uint32_t submission_reason(const generated::submission& submission, const generated::Descriptor* descriptor,
        const registry::LiveState& live) {
    const auto& reference = submission.reference;
    if (live.process_reason) return live.process_reason;
    if (reference.scope.pid != live.scope.pid || reference.scope.process_created != live.scope.process_created ||
        !std::equal(reference.scope.instance_id.begin(), reference.scope.instance_id.end(), live.scope.instance_id))
        return SC_NATIVE_SCOPE_MISMATCH;
    if (!descriptor) return SC_NATIVE_NONE;
    const auto& policy = generated::admission_policies[static_cast<size_t>(descriptor->admission)];
    if (const auto why = namespace_reason(reference, policy, live)) return why;
    if (policy.generation == generated::GenerationRule::exact_nonzero && !reference.scope.generation)
        return SC_NATIVE_SCOPE_MISMATCH;
    if (policy.generation != generated::GenerationRule::none && reference.scope.generation != live.scope.lifecycle_generation)
        return SC_NATIVE_SCOPE_MISMATCH;
    if (policy.save_binding == generated::BindingRule::exact && submission.save_binding != live.save_binding)
        return SC_NATIVE_SCOPE_MISMATCH;
    return SC_NATIVE_NONE;
}
Diagnostics::Reply complete_publication(Diagnostics::Slot& slot, const generated::command_reference& reference,
        const registry::Handler& handler) {
    auto result = slot.result;
    if (slot.cancel.load(std::memory_order_acquire)) {
        result.state = SC_DIAGNOSTIC_CANCELLED; result.reason = SC_NATIVE_CANCELLED;
    } else if (GetTickCount64() >= result.deadline_at_ms) {
        result.state = SC_DIAGNOSTIC_EXPIRED; result.reason = SC_NATIVE_DEADLINE;
    } else if (slot.save_binding != save::session().binding_token()) {
        result.state = SC_DIAGNOSTIC_REJECTED; result.reason = SC_NATIVE_SCOPE_MISMATCH;
    } else {
        registry::ExecutionContext context{};
        handler.execute(slot.payload, slot.value, context);
        result.state = SC_DIAGNOSTIC_EXECUTED;
        slot.evidence = handler.classify(slot.payload, slot.value, result);
    }
    result.completed_at_ms = GetTickCount64();
    Diagnostics::finish(slot, result);
    return query(reference);
}
Diagnostics::Reply submit(const generated::submission& submission, const generated::NativeRequest& payload) {
    AcquireSRWLockExclusive(&lock);
    std::unique_ptr<SRWLOCK, decltype(&ReleaseSRWLockExclusive)> guard(&lock, ReleaseSRWLockExclusive);
    const auto now = GetTickCount64();
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    const auto live = registry_live(scope, prerequisite(now));
    const auto& reference = submission.reference;
    const auto* handler = registry::handler(static_cast<registry::Id>(reference.command_type));
    const auto* descriptor = registry::descriptor(reference.command_type, reference.payload_revision);
    const auto why = submission_reason(submission, descriptor, live);
    auto out = diagnostics.submit(submission, payload, why, now, handler, &live);
    auto* publication = descriptor && descriptor->execution == generated::Execution::publication && out.error == Diagnostics::Error::none ?
        diagnostics.claim(out.reference, now) : nullptr;
    guard.reset();
    if (publication) return complete_publication(*publication, out.reference, *handler);
    return out;
}
Diagnostics::Reply query(const generated::command_reference& reference, bool cancel) {
    AcquireSRWLockExclusive(&lock);
    std::unique_ptr<SRWLOCK, decltype(&ReleaseSRWLockExclusive)> guard(&lock, ReleaseSRWLockExclusive);
    auto out = diagnostics.query(reference, cancel, GetTickCount64());
    guard.reset(); return out;
}
Diagnostics::Reply ack(const generated::command_reference& reference, const std::array<uint8_t, 16>& token) {
    AcquireSRWLockExclusive(&lock);
    std::unique_ptr<SRWLOCK, decltype(&ReleaseSRWLockExclusive)> guard(&lock, ReleaseSRWLockExclusive);
    auto out = diagnostics.ack(reference, token, GetTickCount64());
    guard.reset(); return out;
}
static uint32_t publish_automap(const sc_automap_request& request) {
    uint32_t outcome = SC_AUTOMAP_REFUSED;
    const bool duplicate=request.revision==automap_snapshot.revision && request.known==automap_snapshot.known &&
        !std::memcmp(request.checked_locations,automap_snapshot.checked_locations,sizeof(request.checked_locations));
    bool regression=false;
    if (request.known) for (size_t i=0;i<SC_AUTOMAP_LOCATION_BITS/64;++i)
        regression|=(automap_checked_floor[i]&~request.checked_locations[i])!=0;
    if (regression) outcome=SC_AUTOMAP_REGRESSION;
    else if (duplicate || request.revision>automap_snapshot.revision) {
        automap_snapshot=request;
        if (request.known) std::memcpy(automap_checked_floor,request.checked_locations,sizeof(automap_checked_floor));
        outcome=SC_AUTOMAP_ACCEPTED;
    }
    return outcome;
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
        out.outcome = publish_automap(request);
    }
    out.known=automap_snapshot.known; out.revision=automap_snapshot.revision;
    out.native_fault=automap_fault.load(); out.scanned=automap_scanned;
    out.collected=automap_collected; out.completed_passes=automap_passes;
    ReleaseSRWLockExclusive(&automap_lock); ReleaseSRWLockShared(&lock);
    return out;
}
sc_campaign_result campaign_request(uint16_t operation,const sc_campaign_request& request,const sc_campaign_summary* summary,
                                    const sc_campaign_rewards* rewards) {
    AcquireSRWLockShared(&lock);
    auto scope=status.scope; scope.lifecycle_generation=lifetime.generation;
    const bool admitted=campaign_menu::available() && same_scope(scope,request.execution.expected) &&
        save::session().state()==save::SessionState::admitted && save::session().accepts_requests() &&
        save::session().namespace_id()==request.namespace_id;
    const auto out=campaign_menu::menu().request(operation,request,admitted,summary,rewards);
    ReleaseSRWLockShared(&lock); return out;
}
sc_combat_presentation_result combat_presentation_request(const sc_combat_presentation_request& request) {
    AcquireSRWLockShared(&lock);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    const bool admitted = same_scope(scope, request.execution.expected) &&
        save::session().state() == save::SessionState::admitted && save::session().accepts_requests() &&
        save::session().namespace_id() == request.namespace_id;
    const auto result = combat::presentation().submit(request, scope, admitted);
    ReleaseSRWLockShared(&lock); return result;
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
sc_command_result submit_command(const sc_command_request& request) {
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64(); auto why = prerequisite(now);
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    if (!why && (!same_scope(scope, request.execution.expected) || !commands::admitted(request)))
        why = SC_NATIVE_SCOPE_MISMATCH;
    const auto admitted = diagnostics.submit(request.execution, why, now, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, &request);
    auto out = commands::initial(request);
    if (admitted.state == SC_DIAGNOSTIC_REJECTED) out.execution = admitted;
    else out = diagnostics.command_result(request, false, now);
    ReleaseSRWLockExclusive(&lock); return out;
}
sc_command_result command_result(const sc_command_request& request, bool cancel, bool release) {
    AcquireSRWLockExclusive(&lock);
    auto out = diagnostics.command_result(request, cancel, GetTickCount64(), release);
    ReleaseSRWLockExclusive(&lock); return out;
}
bool stop() {
    expanded::stop();
    ap_controls::stop();
    combat::stop();
    commands::disable();
    save::session().stop_requests();
    stopping.store(true, std::memory_order_release);
    accepting.store(false, std::memory_order_release);
    save::cancel_native_installation();
    AcquireSRWLockExclusive(&startup); // never called from a native callback
    accepting.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&lock);
    diagnostics.cancel_pending(GetTickCount64()); clear_context(SC_NATIVE_STOPPED);
    status.availability = retained() ? SC_NATIVE_RETAINED : SC_NATIVE_DISABLED;
    status.reason = SC_NATIVE_STOPPED;
    ReleaseSRWLockExclusive(&lock); ReleaseSRWLockExclusive(&startup);
    return retained();
}
bool retained() { return pinned.load(std::memory_order_acquire) || hook_residue.load(std::memory_order_acquire) || save::owner_retained(); }
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
bool test_arm_first_contact(uintptr_t target,uintptr_t activate,uintptr_t player) {
    return arm_first_contact(target,activate,player);
}
void test_cold_events(const engine::Binding& source, uint64_t (*change)(uintptr_t,uintptr_t,uintptr_t)) {
    binding=source; fixture={}; fixture.owner=[]() -> uint32_t { return GetCurrentThreadId(); }; fixture_active=true;
    lifetime={}; status={}; status.game_state=SC_GAME_MAIN_MENU; event_depth=0; event_thread=0;
    original_change=change; fault=0; gaps=0; accepting=true;
}
void test_observation_epoch(uint64_t value) { epoch.store(value, std::memory_order_release); }
uint64_t test_change(uintptr_t root, uintptr_t descriptor, uintptr_t files) { return change_detour(root,descriptor,files); }
void test_free(uintptr_t root,void (*free)(uintptr_t,uintptr_t)) { original_free=free; free_detour(root,root+0x50); }
void test_generation_gap() { ++lifetime.generation; }
void test_dispatch_adapter(const TestAdapter& adapter, const sc_native_scope& scope) {
    fixture=adapter; fixture_active=true; status.scope=scope;
}
void test_post_frame() { post_frame(); }
bool test_automap_checked(const sc_automap_request& snapshot,const char* map,const char* name) {
    return automap_status(snapshot,map,name)==2;
}
bool test_automap_collect(uintptr_t system,uintptr_t key,uintptr_t object,void (*collect)(uintptr_t,uintptr_t)) {
    automap_collect=collect;
    return automap_set_collected(system,key,object);
}
bool test_automap_uncollected(uintptr_t object,void (*set_state)(uintptr_t,int)) {
    automap_state=set_state;
    return automap_set_uncollected(object);
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
namespace sentinel::native {
Diagnostics::Reply read(const generated::submission& submission, const generated::NativeRequest& payload) {
    Diagnostics::Reply out{};
    AcquireSRWLockExclusive(&lock);
    const auto now = GetTickCount64();
    auto scope = status.scope; scope.lifecycle_generation = lifetime.generation;
    const auto live = registry_live(scope, prerequisite(now));
    const auto* descriptor = registry::descriptor(submission.reference.command_type, submission.reference.payload_revision);
    const auto* handler = registry::handler(static_cast<registry::Id>(submission.reference.command_type));
    auto reason = submission_reason(submission, descriptor, live);
    if (!reason && handler) reason = handler->admit(payload, live);
    ::sc_diagnostic_request request{};
    request.expected = {submission.reference.scope.pid, submission.reference.scope.process_created, {}, submission.reference.scope.generation};
    std::copy(submission.reference.scope.instance_id.begin(), submission.reference.scope.instance_id.end(), request.expected.instance_id);
    request.request_id = now; request.nonce[0] = 1; request.deadline_ms = submission.deadline_ms;
    auto* slot = diagnostics.transient(request, payload, reason, now);
    if (slot) { slot->transient_read = true; slot->save_binding = submission.save_binding; slot->command_type = static_cast<registry::Id>(submission.reference.command_type); }
    ReleaseSRWLockExclusive(&lock);
    if (!slot) { out.execution.state = SC_DIAGNOSTIC_REJECTED; out.execution.reason = reason ? reason : SC_NATIVE_QUEUE_FULL; return out; }
    for (;;) {
        AcquireSRWLockExclusive(&lock);
        const bool done = diagnostics.collect_transient(*slot, out, GetTickCount64());
        ReleaseSRWLockExclusive(&lock);
        if (done) return out;
        Sleep(1);
    }
}
}
