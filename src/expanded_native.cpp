#include "expanded_native.h"
#include "expanded.h"
#include "enemy_recipe_state.h"
#include "enemy_checkpoint_native.h"
#include "combat_native.h"
#include "inventory.h"
#include "native_target.h"
#include "save_session.h"
#include "save_collector.h"
#include "commands_native.h"
#include "startup_log.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <memory>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <charconv>
#include <deque>
#include <iterator>
#include <cmath>
#include <chrono>
#include <intrin.h>
#ifdef SC_NATIVE_TESTING
#include <tuple>
#include <type_traits>
#include <thread>
#endif

namespace sentinel::expanded {
namespace {
using CostClock = std::chrono::steady_clock;
uint64_t elapsed_us(CostClock::time_point start) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(CostClock::now()-start).count());
}
struct ObserveCosts {
    uint64_t calls=0,total=0,maximum=0,slow=0,wait=0,wait_max=0,sync=0,sync_max=0,scan=0,scan_max=0,next=0;
} observe_costs;
std::atomic<uint64_t> motion_queries{0},motion_lookup_us{0},motion_controllers{0},motion_native_us{0};
struct ObserveCost {
    CostClock::time_point start=CostClock::now();
    uint64_t wait=0,sync=0,scan=0;
    uint64_t living=0;
    uintptr_t world=0;
    ~ObserveCost() {
        auto& value=observe_costs;
        const auto elapsed=elapsed_us(start);
        ++value.calls; value.total+=elapsed; value.maximum=std::max(value.maximum,elapsed);
        value.slow+=elapsed>=16000; value.wait+=wait; value.wait_max=std::max(value.wait_max,wait);
        value.sync+=sync; value.sync_max=std::max(value.sync_max,sync);
        value.scan+=scan; value.scan_max=std::max(value.scan_max,scan);
        static uint64_t peak=0;
        peak=std::max(peak,living);
        const auto now=GetTickCount64();
        if (now<value.next) return;
        value.next=now+5000;
        save::session().btrace.record(save::BStage::enemy_population,save::BStatus::succeeded,"enemy_population",0,
            {{"living",living},{"peak",peak}},world);
        save::session().btrace.record(save::BStage::enemy_motion_cost,save::BStatus::succeeded,"enemy_motion_cost_us",0,
            {{"queries",motion_queries.load(std::memory_order_relaxed)},{"lookup_us",motion_lookup_us.load(std::memory_order_relaxed)},
             {"controllers",motion_controllers.load(std::memory_order_relaxed)},{"native_us",motion_native_us.load(std::memory_order_relaxed)}},world);
        save::session().btrace.record(save::BStage::enemy_runtime,save::BStatus::succeeded,"enemy_runtime_cost_us",0,
            {{"calls",value.calls},{"total_us",value.total},{"max_us",value.maximum},{"over_16ms",value.slow},
             {"lock_us",value.wait},{"lock_max_us",value.wait_max},{"sync_us",value.sync},{"sync_max_us",value.sync_max},
             {"scan_us",value.scan},{"scan_max_us",value.scan_max}},world);
    }
};
struct WaitCost {
    CostClock::time_point start=CostClock::now();
    ~WaitCost() {
        static std::mutex mutex;
        static uint64_t calls=0,total=0,maximum=0,slow=0,next=0;
        const auto us=elapsed_us(start);
        std::lock_guard<std::mutex> lock(mutex);
        ++calls; total+=us; maximum=std::max(maximum,us); slow+=us>=16000;
        const auto now=GetTickCount64();
        if (now<next) return;
        next=now+5000;
        save::session().btrace.record(save::BStage::enemy_wait_cost,save::BStatus::succeeded,"enemy_wait_cost_us",0,
            {{"calls",calls},{"total_us",total},{"max_us",maximum},{"over_16ms",slow}});
    }
};
using Movement = float(__fastcall*)(uintptr_t);
using Controller = float(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, float);
using DefinitionName = const char*(__fastcall*)(uintptr_t);
Movement original_movement = nullptr;
Movement original_haste = nullptr;
Controller original_controller = nullptr;
DefinitionName definition_name = nullptr;
struct HostList { uintptr_t* data; int32_t count, capacity; uint8_t allocator[8]; };
using CollectHosts = void(__fastcall*)(HostList*);
using ResolveActor = uintptr_t(__fastcall*)(uintptr_t);
using GrowHosts = void(__fastcall*)(HostList*, int32_t);
CollectHosts original_hosts = nullptr;
ResolveActor resolve_actor = nullptr;
GrowHosts grow_hosts = nullptr;
uintptr_t world_global = 0;
uint32_t native_actor_identifier(uintptr_t actor) {
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    uint32_t identifier[]{0x1fffffe, 0x1fffffe};
    reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t*, uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(world) + 0x90))(world, identifier, actor);
    return identifier[0];
}
using ActorIdentifier = uint32_t(*)(uintptr_t);
#ifdef SC_NATIVE_TESTING
ActorIdentifier actor_identifier = [](uintptr_t actor) {
    return reinterpret_cast<uint32_t(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(actor) + 0x88))(actor);
};
#else
ActorIdentifier actor_identifier = native_actor_identifier;
#endif
using SpawnActor = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
SpawnActor original_spawn = nullptr;
using SpawnGroup = uint64_t(__fastcall*)(uintptr_t, uint64_t*, uintptr_t);
using RegisterSpawn = void(__fastcall*)(uintptr_t, uint64_t*);
SpawnGroup original_group = nullptr;
RegisterSpawn original_register_spawn = nullptr;
using NativeAdmission = bool(__fastcall*)(uintptr_t, int32_t);
NativeAdmission native_admission = nullptr;
using SpawnSingle = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t, uintptr_t, uintptr_t);
using SpawnMultiple = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t, int32_t, uintptr_t, uintptr_t);
using SpawnSpirit = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uint64_t, uintptr_t, uint8_t);
using PointFactory = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
using PointAvailable = uint64_t(*)(uintptr_t, uintptr_t, uint8_t, uint8_t, uint8_t, uint8_t);
using PointCandidate = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t);
using PointIndex = int32_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t);
using RewindTimeline = void(__fastcall*)(uintptr_t, int32_t);
SpawnSingle original_single = nullptr;
SpawnMultiple original_multiple = nullptr;
SpawnSpirit original_spirit = nullptr;
using SpiritHelper = void(*)(uintptr_t, uint64_t, const char*, uintptr_t, uint8_t, uint8_t, uintptr_t, uintptr_t);
using SpawnArchvile = uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, const char*);
using SpawnBuff = uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, const char*);
SpiritHelper original_spirit_helper = nullptr;
SpawnArchvile original_archvile = nullptr;
SpawnBuff original_buff = nullptr;
uintptr_t native_string_table = 0;
PointFactory original_point_factory = nullptr;
PointAvailable original_point_available = nullptr;
PointCandidate original_point_candidate = nullptr;
PointIndex original_point_index = nullptr;
RewindTimeline rewind_timeline = nullptr;
using FindEntity = uintptr_t(*)(uintptr_t, const char*);
FindEntity find_entity = nullptr;
struct NativeRecipe {
    RecipeLibraryEntry definition;
    std::vector<uintptr_t> bodies;
    uintptr_t tail = 0;
    std::vector<std::string> compatible_bodies;
    bool passthrough = false;
    std::map<uintptr_t, uintptr_t> blood_punch_alternatives;
    std::map<uintptr_t, uintptr_t> microwave_alternatives;
    std::map<uintptr_t, uintptr_t> stone_alternatives;
    std::map<uintptr_t, uintptr_t> armored_alternatives;
    uint32_t refill_limit = 0;
};
RecipeLibrary recipe_library;
std::map<std::string, NativeRecipe> native_recipes;
RecipeState recipe_state;
uint64_t next_recipe_request = 0;
std::string session_recipe_plan, checkpoint_recipe_plan;
bool checkpoint_recipe_missing = false;
std::map<std::pair<uintptr_t, std::string>, std::string> pending_recipes;
struct RecipeCreation {
    RecipeCreation* previous = nullptr;
    const NativeRecipe* recipe = nullptr;
    RecipeEmission emission;
    std::string key;
    std::vector<RecipeMembership> memberships;
    uintptr_t body = 0, source = 0, actor = 0, show_actor = 0;
    SpawnLimits limits{};
    bool reserved = false;
    struct PointState { uintptr_t definitions; int32_t count, index, animation; uint32_t state; } original{}, suspended{};
    uintptr_t active_point = 0, selecting_manager = 0;
    RecipeCreation* suspended_by = nullptr;
    bool creating = false, override_active = false, restoring = false;
};
thread_local RecipeCreation* recipe_creation = nullptr;
thread_local const std::string* worker_recipe = nullptr;
thread_local uint64_t worker_body_offset = 0;
using WorkerTick = void(*)(uintptr_t);
using QueueCommand = void(*)(uintptr_t, uintptr_t);
WorkerTick original_maintain_tick = nullptr, original_stagger_tick = nullptr;
QueueCommand original_queue_command = nullptr;
std::map<uintptr_t, std::string> worker_recipes;
struct WorkerDiagnostic { std::string manager, label; uint64_t mask = 0; };
std::map<uintptr_t, WorkerDiagnostic> worker_diagnostics;
uint32_t worker_trace_count = 0;
std::map<uintptr_t, std::string> recipe_actors;
struct ActorCredit { uint32_t identifier; RecipeCredit credit; std::string request, name; uint64_t ordinal = 0; };
std::map<uintptr_t, ActorCredit> recipe_actor_credits;
struct ActorDiagnostic {
    uint32_t identifier = 0;
    uintptr_t manager_address = 0;
    std::string manager, encounter;
    std::array<float, 3> position{};
    bool positioned = false;
    unsigned jumps = 0;
};
std::map<uintptr_t, ActorDiagnostic> actor_diagnostics;
uint64_t actor_diagnostic_next = 0;
uint32_t registration_trace_count = 0;
thread_local const char* admission_diagnostic_reason = "admission_unobserved";
bool read_definition_name(uintptr_t, char (&)[512]) noexcept;
bool admission_observed(bool allowed, const char* reason) noexcept {
    if (!allowed) admission_diagnostic_reason = reason;
    return allowed;
}
void trace_spawn_refusal(uintptr_t manager, std::string_view origin, const char* reason, const char* decision = "deferred", const char* owner_override = nullptr) noexcept {
    try {
        static std::map<std::pair<uintptr_t, std::string>, uint64_t> next;
        if (next.size() >= 4096) next.clear();
        auto& due = next[{manager, std::string(reason)}];
        const auto now = GetTickCount64();
        if (now < due) return;
        due = now + 5000;
        char owner[512]{}; int32_t cursor = -1;
        if (owner_override) strncpy_s(owner, owner_override, _TRUNCATE);
        else if (manager) read_definition_name(manager, owner);
        engine::LocalMemory memory;
        memory.copy(manager + 0xda0, &cursor, sizeof(cursor));
        commands::diagnostic_trace("AP_ENEMY_REQUEST manager=%.192s address=%llx cursor=%d origin=%.*s decision=%s reason=%s",
            owner, static_cast<unsigned long long>(manager), cursor, static_cast<int>((std::min)(origin.size(), size_t{64})), origin.data(), decision, reason);
    } catch (...) {}
}
void trace_actor(uintptr_t actor, const char* reason, uintptr_t source = 0, bool position = false) noexcept {
    try {
        const auto found = actor_diagnostics.find(actor);
        if (found == actor_diagnostics.end()) return;
        auto& value = found->second;
        if (position && value.positioned && value.jumps >= 3) return;
        std::array<float, 3> xyz{};
        engine::LocalMemory memory;
        const bool readable = position && !memory.copy(actor + 0x74, xyz.data(), sizeof(xyz)).reason &&
            std::all_of(xyz.begin(), xyz.end(), [](float v) { return std::isfinite(v); });
        if (position && !readable) return;
        const bool initial = position && !value.positioned;
        if (position && value.positioned) {
            double distance = 0;
            for (size_t i = 0; i < xyz.size(); ++i) distance += std::pow(double(xyz[i]) - value.position[i], 2);
            value.position = xyz;
            if (distance < 20 * 20) return;
            ++value.jumps;
        }
        if (readable) { value.position = xyz; value.positioned = true; }
        commands::diagnostic_trace("%s actor=%llx id=%u manager=%.160s encounter=%.192s reason=%s source=%llx position=%d xyz=%g,%g,%g jumps=%u",
            initial ? "AP_ENEMY_SPAWN_POSITION" : "AP_ENEMY_ACTOR", static_cast<unsigned long long>(actor), value.identifier, value.manager.c_str(), value.encounter.c_str(), reason,
            static_cast<unsigned long long>(source), readable, xyz[0], xyz[1], xyz[2], value.jumps);
    } catch (...) {}
}
std::map<std::string, RecipeNativeCheckpoint::Identity> restored_actor_identities;
using DeathActions = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using PostOne = uintptr_t(*)(uintptr_t, uintptr_t, int32_t, uintptr_t);
DeathActions original_death_actions = nullptr;
PostOne original_post_one = nullptr;
std::array<uintptr_t, 3> death_target_returns{};
uintptr_t activate_event = 0;
struct DeathActionContext { std::string request; RecipeCredit credit; };
thread_local const DeathActionContext* death_action_context = nullptr;
struct DeathActionCredit { uint64_t units = 0, sent = 0; uint32_t denominator = 0; };
std::map<std::pair<std::string, std::string>, DeathActionCredit> death_action_credits;
struct PointCredits {
    int32_t observed = 0;
    std::map<uint32_t, uint64_t> units;
};
std::map<uintptr_t, PointCredits> point_credits;
using SummonCreate = float(*)(uintptr_t, uintptr_t, uintptr_t, uint64_t, uintptr_t);
using SummonUpdate = void(*)(uintptr_t, uintptr_t);
using NativeUnary = void(*)(uintptr_t);
using ActorFactory = uintptr_t(*)(uintptr_t);
using DefinitionDatabase = uintptr_t(*)();
using LookupDefinition = uintptr_t(*)(uintptr_t, const char*, int32_t);
SummonCreate original_summon_create = nullptr;
SummonUpdate original_summon_update = nullptr;
NativeUnary original_summon_start = nullptr, original_summon_complete = nullptr, destroy_native_text = nullptr;
NativeUnary original_darklord_summon = nullptr;
NativeUnary original_summon_request_complete = nullptr;
NativeUnary original_summon_cleanup = nullptr;
ActorFactory original_actor_factory = nullptr;
using ResurrectFactory = uintptr_t(*)(uintptr_t, int32_t, const float*, const float*);
ResurrectFactory original_resurrect_factory = nullptr;
DefinitionDatabase definition_database = nullptr;
LookupDefinition lookup_definition = nullptr;
using GrowSummonRecords = bool(*)(uintptr_t, uint32_t);
GrowSummonRecords grow_summon_records = nullptr;
using ConstructNativeString = uintptr_t(*)(uintptr_t);
ConstructNativeString construct_native_string = nullptr;
uintptr_t animation_path_table = 0;
using AssignNativeText = void(*)(uintptr_t, const char*);
AssignNativeText assign_native_text = nullptr;
using SamuelSetup = void(*)(uintptr_t, uintptr_t);
SamuelSetup original_samuel_setup = nullptr;
thread_local bool samuel_without_parasite = false;
using ManagerContains = bool(*)(uintptr_t, uintptr_t);
ManagerContains manager_contains = nullptr;
using MonsterMask = uint64_t(*)(uint64_t);
MonsterMask monster_mask = nullptr;
uintptr_t summon_label_symbol = 0;
struct SummonSource {
    std::array<uint8_t, 48> entry{};
    std::array<float, 3> position{};
    std::string origin, label, template_name;
    uint64_t mask = 0;
};
struct SummonCollection { uintptr_t component; std::vector<SummonSource> sources; std::vector<std::string> notifications; };
thread_local SummonCollection* summon_collection = nullptr;
thread_local bool actor_factory_active = false;
struct SummonRequest {
    std::string key, origin;
    std::vector<SummonSource> sources;
    uint64_t body_offset = 0;
    bool completed = false;
};
struct ComponentSummons {
    uintptr_t parent = 0;
    uint32_t identifier = 0;
    std::string name;
    bool completed = false;
    uint64_t epoch = 0;
    std::deque<SummonRequest> requests;
    std::map<std::string, std::set<std::string>> notifications;
};
std::map<uintptr_t, ComponentSummons> component_summons;
std::map<uintptr_t, ComponentSummons> direct_points;
std::map<std::string, RecipeNativeCheckpoint::Child> summon_children;
std::optional<RecipeNativeCheckpoint> pending_summon_checkpoint;
NativeUnary original_show = nullptr, original_show_now = nullptr, original_hide = nullptr;
using PostEvent = bool(*)(uintptr_t, uintptr_t, int32_t, uint32_t, uintptr_t);
using CancelEvent = void(*)(uintptr_t, uintptr_t);
PostEvent post_event = nullptr;
CancelEvent cancel_event = nullptr;
uintptr_t show_event = 0;
struct DeferredShow { uint32_t identifier; std::string name; };
std::map<uintptr_t, DeferredShow> deferred_shows;
thread_local uintptr_t active_show_actor = 0;
struct IndependentShow {
    RecipeCreation creation;
    bool opened = false, source = false;
};
std::set<std::string> independent_shows;
std::optional<RecipeNativeCheckpoint> restored_independent;
std::map<std::string, std::array<float, 12>> pending_independent_shows;
bool independent_memberships_pending = false;
using TraversalEnter = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using AnimationValid = bool(*)(uintptr_t, const char*);
TraversalEnter original_traversal_enter = nullptr;
AnimationValid original_animation_valid = nullptr;
struct TraversalEntry {
    TraversalEntry* previous = nullptr;
    uintptr_t actor = 0;
    uint32_t identifier = 0;
    std::array<float, 12> pose{};
    bool missing = false;
};
thread_local TraversalEntry* traversal_entry = nullptr;
std::atomic<bool> recipe_fault{false};
std::map<std::string, uint8_t> recipe_spawn_traces;
std::map<std::tuple<uintptr_t, char, uint64_t, uint32_t>, uint8_t> quantity_wait_traces;
std::map<uintptr_t, uint64_t> remaining_wait_started;
using AllocateWorker = uintptr_t(*)(size_t, uint32_t);
using ConstructStagger = uintptr_t(*)(uintptr_t, int32_t, uint64_t, uint64_t, uintptr_t, uint64_t, const char*);
using AssignManager = void(*)(uintptr_t, uintptr_t);
AllocateWorker allocate_worker = nullptr;
ConstructStagger construct_stagger = nullptr;
AssignManager assign_manager = nullptr;
using NativeClock = uint64_t*(*)(uintptr_t, uint64_t*, int32_t);
using NativeStep = float(*)(uintptr_t, int32_t);
using NativeDuration = uint64_t*(*)(uint64_t*, float);
using ConstructTime = uintptr_t(*)(uintptr_t, uint64_t, uint64_t, uint64_t);
NativeClock native_clock = nullptr;
NativeStep native_step = nullptr;
NativeDuration native_duration = nullptr;
ConstructTime construct_time = nullptr;
using SpawnPossessed = uintptr_t(*)(uintptr_t, uintptr_t, uint64_t, uintptr_t, uintptr_t, uintptr_t,
                                    uint64_t, uintptr_t, uint8_t);
SpawnPossessed original_possessed = nullptr;
using RemainingPredicate = uint64_t(*)(uintptr_t);
using RemainingEvent = void(*)(uintptr_t, uintptr_t);
using RefreshEntity = void(*)(uintptr_t, uintptr_t);
using IgnoredActor = bool(*)(uintptr_t);
RemainingPredicate original_remaining = nullptr;
RemainingEvent original_remaining_added = nullptr, original_remaining_dead = nullptr;
RefreshEntity refresh_entity = nullptr, refresh_manager = nullptr;
IgnoredActor ignored_actor = nullptr;
struct RemainingWeight { RecipeCredit credit; bool recipe; };
struct RemainingItem { uint32_t identifier; RemainingWeight weight; bool known = false; };
std::map<uintptr_t, std::vector<RemainingItem>> remaining_weights;
using KillConstruct = uintptr_t(*)(uintptr_t, int32_t, uintptr_t, uint64_t, uint64_t);
using WorkerCast = uintptr_t(*)(uintptr_t);
using CompareText = uint8_t(*)(uintptr_t, uintptr_t);
KillConstruct original_kill_construct = nullptr;
RemainingPredicate original_kill = nullptr;
RemainingEvent original_kill_dead = nullptr;
WorkerCast cast_maintain = nullptr, cast_stagger = nullptr;
CompareText compare_text = nullptr;
struct KillProgress {
    std::string owner, label;
    uint64_t mask = 0;
    uint32_t seen = 0;
    uint8_t traced = 0;
    bool initialized = false, tracked = false;
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> deaths;
    RecipeBaseline baseline;
};
std::map<uintptr_t, KillProgress> kill_progress;
RemainingPredicate original_health = nullptr;
RemainingEvent original_health_added = nullptr;
using HealthDamage = void(*)(uintptr_t, uintptr_t, float);
HealthDamage original_health_damage = nullptr;
struct HealthItem { RemainingWeight weight; float initial = 0, current = 0; };
struct HealthProgress {
    std::map<uint32_t, HealthItem> actors;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint64_t> retired;
    bool tracked = false;
};
std::map<uintptr_t, HealthProgress> health_progress;
std::recursive_mutex quantity_mutex;
std::recursive_mutex admission_mutex;
std::array<uint32_t, 9> reserved_special{};
std::array<uint32_t, 9> reserved_species{};
uint32_t reserved_actors = 0;
struct ImmediateRequest {
    ImmediateRequest* previous;
    uintptr_t manager, origin;
    SpawnLimits limits;
    SpawnTracking tracking;
    bool reserved, succeeded, direct_factory;
};
thread_local ImmediateRequest* immediate_request = nullptr;

std::atomic<bool> stopped{true};
thread_local uintptr_t active_ai = 0;
thread_local float active_speed = 1.0f;
bool generated_admission(const SpawnLimits&);
void trace_recipe_spawn(const RecipeCreation&, uintptr_t body, uintptr_t point, uint8_t stage);
void reserve_admission(const SpawnLimits&);
void release_admission(const SpawnLimits&);
void retry_immediate(uintptr_t, const char* reason = "native_spawn_empty");
void initialize_kill(uintptr_t, uintptr_t);
uintptr_t point_factory_detour(uintptr_t, uintptr_t);
uint64_t spawn_detour(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uint64_t group_detour(uintptr_t, uint64_t*, uintptr_t);
bool record_point_credit(uintptr_t, const RecipeCreation*);
std::string independent_body_name(const NativeRecipe&, uint64_t);
void independent_pose(uintptr_t, const float*);
bool capture_autonomous_summons(RecipeNativeCheckpoint&);
bool restore_autonomous_summons(uintptr_t);
uintptr_t record_actor(uintptr_t);
void register_independent_copy(uintptr_t, uint64_t*, uintptr_t);
uintptr_t invoke_direct_creation(RecipeCreation*, uintptr_t);
uintptr_t drain_direct_point(uintptr_t);

bool read_definition_name(uintptr_t ai, char (&copy)[512]) noexcept {
    __try {
        const auto* name = definition_name(ai);
        if (!name) return false;
        for (size_t size = 0; size < sizeof(copy); ++size) {
            copy[size] = name[size];
            if (!copy[size]) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool recipe_definition_marker(std::string_view name, bool empty) {
    if (!empty) return spawn_recipe(name).has_value();
    constexpr std::string_view prefix = "ap_v070_empty_r";
    return name.size() == prefix.size() + 20 && name.substr(0, prefix.size()) == prefix &&
        name.substr(prefix.size()).find_first_not_of("0123456789abcdef") == name.npos;
}
bool read_recipe_definition(uintptr_t ai, char (&copy)[512], bool empty = false) noexcept {
    if (read_definition_name(ai, copy) && recipe_definition_marker(copy, empty)) return true;
    __try {
        auto definition = *reinterpret_cast<const uintptr_t*>(ai + 0xa8);
        for (unsigned depth = 0; definition && depth < 64; ++depth) {
            const auto* name = *reinterpret_cast<const char**>(definition + 8);
            if (name) {
                size_t size = 0;
                while (size < sizeof(copy) && (copy[size] = name[size])) ++size;
                if (size < sizeof(copy) && recipe_definition_marker(copy, empty)) return true;
            }
            definition = *reinterpret_cast<const uintptr_t*>(definition + 0x158);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool read_instance_name(uintptr_t actor, char (&copy)[512]) noexcept {
    __try {
        const auto name = reinterpret_cast<const char*(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(actor) + 0x60))(actor);
        if (!name) return false;
        for (size_t i = 0; i < sizeof(copy); ++i) { copy[i] = name[i]; if (!copy[i]) return true; }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool independent_origin(uintptr_t actor, char (&origin)[21]) noexcept {
    char empty[512];
    if (read_recipe_definition(actor, empty, true)) {
        std::memcpy(origin, empty + std::strlen("ap_v070_empty_r"), 20); origin[20] = 0; return true;
    }
    __try {
        auto definition = *reinterpret_cast<const uintptr_t*>(actor + 0xa8);
        for (unsigned depth = 0; definition && depth < 64; ++depth,
                definition = *reinterpret_cast<const uintptr_t*>(definition + 0x158)) {
            const auto name = *reinterpret_cast<const char**>(definition + 8);
            constexpr char prefix[] = "ap_v070_independent_r";
            if (!name || std::strncmp(name, prefix, sizeof(prefix) - 1)) continue;
            const auto value = name + sizeof(prefix) - 1;
            for (unsigned index = 0; index < 20; ++index) {
                if (!((value[index] >= '0' && value[index] <= '9') || (value[index] >= 'a' && value[index] <= 'f'))) return false;
                origin[index] = value[index];
            }
            if (value[20] != '_') return false;
            origin[20] = 0; return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
bool current_recipe_actor(uintptr_t actor) {
    auto cached = recipe_actor_credits.find(actor);
    if (cached == recipe_actor_credits.end() && !restored_actor_identities.empty()) {
        char name[512], body[512];
        if (read_instance_name(actor, name)) {
            const auto identity = restored_actor_identities.find(name);
            if (identity != restored_actor_identities.end() && read_recipe_definition(actor, body) && identity->second.body == body) {
                const auto& saved = identity->second;
                cached = recipe_actor_credits.emplace(actor, ActorCredit{actor_identifier(actor), {saved.units, saved.denominator},
                    saved.request, name, saved.ordinal}).first;
                restored_actor_identities.erase(identity);
            }
        }
    }
    if (cached == recipe_actor_credits.end()) return false;
    if (cached->second.identifier == actor_identifier(actor)) return true;
    const auto role = recipe_actors.find(actor);
    if (role != recipe_actors.end()) {
        recipe_state.actor_changed(role->second, RecipeActorState::removed);
        recipe_actors.erase(role);
    }
    recipe_actor_credits.erase(cached);
    return false;
}
bool native_text(uintptr_t string, std::string& out, size_t maximum) {
    __try {
        const auto length = *reinterpret_cast<const int32_t*>(string + 16);
        const auto data = *reinterpret_cast<const char**>(string + 8);
        if (length < 0 || static_cast<size_t>(length) > maximum || (length && !data)) return false;
        out.assign(length ? data : "", static_cast<size_t>(length));
        return out.find('\0') == out.npos;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_cstring(uintptr_t string, std::string& out, size_t maximum) {
    __try {
        const auto data = reinterpret_cast<const char*>(string);
        if (!data) { out.clear(); return true; }
        size_t size = 0;
        while (size <= maximum && data[size]) ++size;
        if (size > maximum) return false;
        out.assign(data, size); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint64_t functional_token(std::string_view value);
bool read_recipe_metadata(uintptr_t world, uintptr_t root, std::string& metadata) {
    if (!native_text(root + 0xbe0, metadata, maximum_recipe_metadata)) return false;
    if (metadata.rfind("EAPC1|", 0) != 0) return true;
    if (metadata.size() != 40 || metadata[14] != '|' || metadata[23] != '|') return false;
    uint64_t count = 0, size = 0, checksum = 0;
    const auto number = [&](size_t at, size_t length, uint64_t& value) {
        const auto field = std::string_view(metadata).substr(at, length);
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value, 16);
        return parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size();
    };
    if (!number(6, 8, count) || !number(15, 8, size) || !number(24, 16, checksum) ||
        !size || size > maximum_recipe_metadata || count != (size + 1799) / 1800) return false;
    std::string combined;
    combined.reserve(static_cast<size_t>(size));
    for (uint64_t i = 0; i < count; ++i) {
        const auto chunk = find_entity(world, ("ap_v070_recipe_plan_" + std::to_string(i)).c_str());
        std::string text;
        if (!chunk || !native_text(chunk + 0xbe0, text, 1800) ||
            text.size() != std::min<uint64_t>(1800, size - combined.size())) return false;
        combined += text;
    }
    if (functional_token(combined) != checksum) return false;
    metadata = std::move(combined);
    return true;
}
bool load_native_recipes() {
    if (!native_recipes.empty() && !restored_independent) return true;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto root = find_entity(world, "ap_v070_recipe_plan");
    std::string metadata;
    RecipeLibrary parsed;
    if (!root) return false;
    if (!read_recipe_metadata(world, root, metadata) || !parse_recipe_library(metadata, parsed)) {
        recipe_fault.store(true, std::memory_order_release);
        if (save::session().campaign_run.enabled()) save::session().campaign_run.refuse("native_enemy_recipe_metadata_invalid");
        return false;
    }
    if (checkpoint_recipe_missing || (!checkpoint_recipe_plan.empty() && parsed.plan != checkpoint_recipe_plan) ||
        (!session_recipe_plan.empty() && parsed.plan != session_recipe_plan)) {
        recipe_fault.store(true, std::memory_order_release);
        if (save::session().campaign_run.enabled()) save::session().campaign_run.refuse("native_enemy_checkpoint_plan_mismatch");
        return false;
    }
    native_string_table = *reinterpret_cast<const uintptr_t*>(root + 0xbe0);
    std::map<std::string, NativeRecipe> recipes;
    for (const auto& entry : parsed.recipes) {
        const auto library = find_entity(world, ("ap_v070_bodies_r" + entry.origin).c_str());
        if (!library) return false;
        const auto count = *reinterpret_cast<const int32_t*>(library + 0xc40);
        const auto data = *reinterpret_cast<const uintptr_t*>(library + 0xc38);
        const bool partial = entry.maximum_emissions && entry.final_credit_units < entry.logical_count;
        if (count < 0 || static_cast<uint64_t>(count) != static_cast<uint64_t>(entry.physical_count) + partial) return false;
        NativeRecipe recipe{entry, std::vector<uintptr_t>(entry.physical_count), 0};
        for (int32_t i = 0; i < count; ++i) {
            const auto body = data + static_cast<uintptr_t>(i) * 0x80;
            std::string name;
            if (!native_text(body, name, 511)) return false;
            const auto tracking = spawn_recipe(name);
            if (!tracking || std::string(tracking->origin.data(), tracking->origin.size()) != entry.origin ||
                tracking->logical_units != entry.logical_count || tracking->physical_units != entry.physical_count) return false;
            const auto masks = spawn_tracking(name);
            if (!masks || masks->source != entry.logical_mask || !spawn_limits(name)) return false;
            if (tracking->credit_units != entry.logical_count) {
                if (!partial || recipe.tail || tracking->credit_units != entry.final_credit_units ||
                    tracking->body != (entry.maximum_emissions - 1) % entry.physical_count) return false;
                recipe.tail = body;
            } else {
                if (recipe.bodies[tracking->body]) return false;
                recipe.bodies[tracking->body] = body;
            }
        }
        if (std::any_of(recipe.bodies.begin(), recipe.bodies.end(), [](auto value) { return !value; }) ||
            (partial && !recipe.tail)) return false;
        recipe.passthrough=find_entity(world,("ap_v070_passthrough_r"+entry.origin).c_str())!=0;
        if (const auto refill = find_entity(world, ("ap_v070_refill_r" + entry.origin).c_str())) {
            std::string value;
            if (entry.family != 'm' || entry.maximum_emissions || !native_text(refill + 0xbe0, value, 10)) return false;
            const auto parsed_limit = std::from_chars(value.data(), value.data() + value.size(), recipe.refill_limit);
            if (parsed_limit.ec != std::errc{} || parsed_limit.ptr != value.data() + value.size() ||
                !recipe.refill_limit || recipe.refill_limit > INT32_MAX) return false;
        }
        if (const auto compatible=find_entity(world,("ap_v070_compat_r"+entry.origin).c_str())) {
            const auto length=*reinterpret_cast<const int32_t*>(compatible+0xc40);
            const auto values=*reinterpret_cast<const uintptr_t*>(compatible+0xc38);
            if (length<0 || static_cast<uint64_t>(length)>static_cast<uint64_t>(entry.physical_count)+partial) return false;
            for (int32_t index=0;index<length;++index) {
                std::string name;
                if (!native_text(values+static_cast<uintptr_t>(index)*0x80,name,511)) return false;
                const auto body=spawn_recipe(name);
                const auto tracking=spawn_tracking(name);
                if (!body || !tracking || std::string(body->origin.data(),body->origin.size())!=entry.origin ||
                    body->logical_units!=entry.logical_count || body->physical_units!=entry.physical_count ||
                    tracking->source!=entry.logical_mask) return false;
                recipe.compatible_bodies.push_back(std::move(name));
            }
        }
        if (const auto alternate = find_entity(world, ("ap_v070_counter_r" + entry.origin).c_str())) {
            const auto length = *reinterpret_cast<const int32_t*>(alternate + 0xc40);
            const auto values = *reinterpret_cast<const uintptr_t*>(alternate + 0xc38);
            if (length < 0 || static_cast<uint64_t>(length) != static_cast<uint64_t>(entry.physical_count) + partial ||
                (length && !values)) return false;
            for (int32_t index = 0; index < length; ++index) {
                const auto replacement = values + static_cast<uintptr_t>(index) * 0x80;
                std::string name, original;
                if (!native_text(replacement, name, 511)) return false;
                const auto body = spawn_recipe(name);
                const auto tracking = spawn_tracking(name);
                if (!body || !tracking || std::string(body->origin.data(), body->origin.size()) != entry.origin ||
                    body->logical_units != entry.logical_count || body->physical_units != entry.physical_count ||
                    body->body >= recipe.bodies.size() || tracking->source != entry.logical_mask || !spawn_limits(name)) return false;
                const auto primary = body->credit_units == entry.logical_count ? recipe.bodies[body->body] :
                    partial && body->credit_units == entry.final_credit_units ? recipe.tail : 0;
                if (!primary || !native_text(primary, original, 511)) return false;
                if (name != original) {
                    const auto limits = spawn_limits(original);
                    if (!limits) return false;
                    const auto original_tracking = spawn_tracking(original);
                    auto& alternatives = original_tracking && original_tracking->physical == 0x20ull ? recipe.stone_alternatives :
                        original_tracking && original_tracking->physical == 0x20000000ull ? recipe.armored_alternatives :
                        limits->kind == 2 ? recipe.microwave_alternatives : recipe.blood_punch_alternatives;
                    if (!alternatives.emplace(primary, replacement).second) return false;
                    recipe.compatible_bodies.push_back(std::move(name));
                }
            }
        }
        recipes.emplace(entry.origin, std::move(recipe));
    }
    RecipeState loaded;
    if (!loaded.reset(parsed.plan, parsed.map)) return false;
    if (restored_independent) {
        const auto invalid = [] {
            recipe_fault.store(true, std::memory_order_release);
            if (save::session().campaign_run.enabled()) save::session().campaign_run.refuse("native_enemy_checkpoint_invalid");
            return false;
        };
        auto checkpoint = *restored_independent;
        if (checkpoint.plan != parsed.plan || checkpoint.map != parsed.map ||
            !loaded.restore(checkpoint.state, parsed.plan, parsed.map)) return invalid();
        std::set<std::string> wanted;
        for (const auto& [origin, recipe] : recipes) {
            (void)origin;
            if (recipe.definition.family == 'd') {
                if (recipe.bodies.empty()) wanted.insert(recipe.definition.source);
                for (uint64_t index = 0; index < recipe.bodies.size(); ++index) wanted.insert(independent_body_name(recipe, index));
            }
        }
        for (const auto& [name, pose] : checkpoint.shows) {
            (void)pose;
            if (!wanted.count(name)) return invalid();
        }
        for (const auto& [origin,recipe] : recipes) {
            (void)origin;
            if (!recipe.passthrough) continue;
            checkpoint.shows.erase(recipe.definition.source);
            for (uint64_t index=0;index<recipe.bodies.size();++index)
                checkpoint.shows.erase(independent_body_name(recipe,index));
        }
        for (const auto& [key, progress] : loaded.requests()) {
            const auto recipe = recipes.find(progress.request.origin);
            if (key.rfind("summon/", 0) == 0 || key.rfind("direct/", 0) == 0) {
                const auto& request = progress.request;
                const auto slash = key.rfind('/'); uint64_t sequence = 0;
                const auto parsed_sequence = std::from_chars(key.data() + slash + 1, key.data() + key.size(), sequence);
                if (recipe == recipes.end() || recipe->second.definition.family != (key.rfind("direct/", 0) == 0 ? 'f' : 'u') ||
                    request.logical_mask != recipe->second.definition.logical_mask ||
                    key.substr(7, 20) != request.origin || !request.parent_actor.empty() || !request.parent_request.empty() ||
                    request.physical_units != request.maximum_emissions || request.credit_limit != request.maximum_emissions ||
                    request.final_credit_units != (request.physical_units ? request.logical_units : 0U) ||
                    parsed_sequence.ec != std::errc{} || parsed_sequence.ptr != key.data() + key.size() ||
                    !sequence || sequence > checkpoint.next_request) return invalid();
                const auto& definition = recipe->second.definition;
                const auto numerator = definition.quantity_denominator ? definition.quantity_numerator : definition.physical_count;
                const auto denominator = definition.quantity_denominator ? definition.quantity_denominator : definition.logical_count;
                if (key.rfind("summon/", 0) == 0) {
                    const auto expected = (uint64_t{request.logical_units} * numerator + denominator - 1) / denominator;
                    if (expected != request.physical_units) return invalid();
                } else if (request.logical_units != definition.logical_count || request.physical_units != definition.physical_count ||
                    request.owner != (definition.owner.empty() ? "ap_v070_direct_r" + definition.origin : definition.owner)) return invalid();
                continue;
            }
            const auto base = "independent/" + progress.request.origin;
            if (key != base) {
                uint64_t activation = 0;
                const auto parsed_activation = std::from_chars(key.data() + (std::min)(base.size() + 1, key.size()), key.data() + key.size(), activation);
                if (progress.request.physical_units || key.rfind(base + "/", 0) != 0 || parsed_activation.ec != std::errc{} ||
                    parsed_activation.ptr != key.data() + key.size() || !activation || activation > checkpoint.next_request) return invalid();
            }
            if (recipe == recipes.end() ||
                recipe->second.definition.family != 'd' || progress.request.owner != recipe->second.definition.source ||
                progress.request.physical_units != recipe->second.definition.physical_count ||
                progress.request.logical_mask != recipe->second.definition.logical_mask || progress.request.logical_units != 1 ||
                progress.request.maximum_emissions != recipe->second.definition.maximum_emissions ||
                progress.emitted > progress.request.maximum_emissions || progress.request.credit_limit != progress.request.physical_units ||
                progress.request.final_credit_units != (progress.request.physical_units ? 1U : 0U) || !progress.request.label.empty() ||
                !progress.request.parent_actor.empty() || !progress.request.parent_request.empty()) return invalid();
            for (const auto& [id, actor] : progress.actors) {
                (void)id;
                if (actor.native_name != independent_body_name(recipe->second, actor.ordinal)) return invalid();
            }
        }
        for (const auto& [key, credit] : checkpoint.actions) {
            (void)credit;
            if (!loaded.requests().count(key.first) && std::none_of(checkpoint.identities.begin(), checkpoint.identities.end(),
                [&](const auto& item) { return item.second.request == key.first; })) return invalid();
        }
        for (const auto& [role, child] : checkpoint.children) {
            const auto tracking = spawn_recipe(child.body);
            const auto& progress = loaded.requests().at(role.substr(0, role.rfind('/')));
            const auto recipe = recipes.find(progress.request.origin);
            if (!tracking || recipe == recipes.end() || (recipe->second.definition.family != 'u' && recipe->second.definition.family != 'f') ||
                std::string(tracking->origin.data(), tracking->origin.size()) != progress.request.origin ||
                std::none_of(recipe->second.bodies.begin(), recipe->second.bodies.end(), [&](uintptr_t body) {
                    std::string name; return native_text(body, name, 511) && name == child.body;
                }) && std::find(recipe->second.compatible_bodies.begin(), recipe->second.compatible_bodies.end(),
                                child.body) == recipe->second.compatible_bodies.end()) return invalid();
            (void)role;
        }
        for (const auto& [name, identity] : checkpoint.identities) {
            (void)name;
            const auto frozen = spawn_recipe(identity.body);
            if (!frozen || !recipes.count(std::string(frozen->origin.data(), frozen->origin.size()))) return invalid();
            const auto& recipe = recipes.at(std::string(frozen->origin.data(), frozen->origin.size()));
            if (std::none_of(recipe.bodies.begin(), recipe.bodies.end(), [&](uintptr_t body) {
                std::string value; return native_text(body, value, 511) && value == identity.body;
            }) && std::find(recipe.compatible_bodies.begin(),recipe.compatible_bodies.end(),identity.body)==recipe.compatible_bodies.end() &&
                (!recipe.tail || [&] { std::string value; return !native_text(recipe.tail, value, 511) || value != identity.body; }())) return invalid();
            const auto progress = loaded.requests().find(identity.request);
            uint64_t units = 0; uint32_t denominator = 0;
            if (progress != loaded.requests().end()) {
                const auto& request = progress->second.request;
                if (request.origin != recipe.definition.origin || identity.ordinal >= progress->second.emitted) return invalid();
                units = request.credit_limit && identity.ordinal + 1 == request.credit_limit ? request.final_credit_units : request.logical_units;
                denominator = request.physical_units;
            } else {
                const auto& definition = recipe.definition;
                const auto prefix = definition.origin + "/";
                uint64_t sequence = 0;
                const auto parsed_sequence = std::from_chars(identity.request.data() + (std::min)(prefix.size(), identity.request.size()),
                    identity.request.data() + identity.request.size(), sequence);
                if (definition.family == 'd' || definition.family == 'u' || definition.family == 'f' || identity.request.rfind(prefix, 0) != 0 ||
                    parsed_sequence.ec != std::errc{} || parsed_sequence.ptr != identity.request.data() + identity.request.size() || !sequence ||
                    sequence > checkpoint.next_request || (definition.maximum_emissions && identity.ordinal >= definition.maximum_emissions) ||
                    recipe.bodies.empty() || frozen->body != identity.ordinal % recipe.bodies.size()) return invalid();
                units = definition.maximum_emissions && identity.ordinal + 1 == definition.maximum_emissions ?
                    definition.final_credit_units : definition.logical_count;
                denominator = definition.physical_count;
            }
            if (identity.units != units || identity.denominator != denominator) return invalid();
        }
        if (recipe_creation || reserved_actors) return invalid();
        map_freed();
        native_string_table = *reinterpret_cast<const uintptr_t*>(root + 0xbe0);
        restored_actor_identities = checkpoint.identities;
        pending_independent_shows = checkpoint.shows;
        for (const auto& [key, credit] : checkpoint.actions) {
            death_action_credits[key] = {credit.units, credit.sent, credit.denominator};
        }
        next_recipe_request = checkpoint.next_request;
        if (!checkpoint.summons.empty() || !checkpoint.children.empty()) pending_summon_checkpoint = checkpoint;
        restored_independent.reset();
    }
    recipe_state = std::move(loaded);
    session_recipe_plan = parsed.plan;
    checkpoint_recipe_plan.clear();
    recipe_library = std::move(parsed); native_recipes = std::move(recipes);
    independent_memberships_pending = true;
    return true;
}
bool recipe_checkpoint_root(uintptr_t& root) noexcept {
    __try {
        const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
        if (!world) return false;
        root = find_entity(world, "ap_v070_recipe_plan");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void lock_recipe_checkpoint() { admission_mutex.lock(); quantity_mutex.lock(); }
void unlock_recipe_checkpoint() { quantity_mutex.unlock(); admission_mutex.unlock(); }
bool capture_recipe_checkpoint(std::string& bytes) {
    const auto failed = [](const char* reason) {
        commands::diagnostic_trace("AP_CHECKPOINT stage=recipe_capture result=failed reason=%s", reason); return false;
    };
    uintptr_t root = 0;
    if (!recipe_checkpoint_root(root)) return failed("root_unreadable");
    if (!root) { bytes = "EAPC1|-\n"; return true; }
    if (recipe_fault.load(std::memory_order_acquire)) return failed("recipe_fault");
    if (!load_native_recipes()) return failed("recipe_library_unavailable");
    if (!session_recipe_plan.empty()) {
        const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
        for (auto actor = recipe_actors.begin(); actor != recipe_actors.end();) {
            if (actor->second.rfind("independent/", 0) != 0 && actor->second.rfind("summon/", 0) != 0 && actor->second.rfind("direct/", 0) != 0) { ++actor; continue; }
            const auto next = std::next(actor);
            if (!current_recipe_actor(actor->first)) { actor = next; continue; }
            const auto credit = recipe_actor_credits.find(actor->first);
            if (credit == recipe_actor_credits.end()) return failed("actor_credit_missing");
            const bool present = find_entity(world, credit->second.name.c_str()) == actor->first;
            if (present && !combat::confirmed_dead(world, actor->first)) { ++actor; continue; }
            if (!recipe_state.actor_changed(actor->second, present ? RecipeActorState::dead : RecipeActorState::removed)) return failed("actor_retirement_refused");
            actor = recipe_actors.erase(actor);
        }
        RecipeNativeCheckpoint checkpoint;
        checkpoint.plan = recipe_state.plan(); checkpoint.map = recipe_state.map();
        if (!capture_autonomous_summons(checkpoint)) return failed("summon_capture_refused");
        if (!recipe_state.capture(checkpoint.state, std::vector<std::string_view>{"independent/", "summon/", "direct/"})) return failed("recipe_state_capture_refused");
        auto wanted = independent_shows;
        for (const auto& [actor, pending] : deferred_shows) {
            char origin[21];
            if (independent_origin(actor, origin)) wanted.insert(pending.name);
        }
        for (const auto& name : wanted) {
            const auto actor = find_entity(world, name.c_str());
            if (!actor || combat::confirmed_dead(world, actor)) continue;
            auto& pose = checkpoint.shows[name];
            std::memcpy(pose.data(), reinterpret_cast<const void*>(actor + 0x74), sizeof(pose));
        }
        for (const auto& [name, pose] : pending_independent_shows) checkpoint.shows[name] = pose;
        std::set<std::string> identity_requests;
        checkpoint.identities = restored_actor_identities;
        for (const auto& [actor, credit] : recipe_actor_credits) {
            if (recipe_actors.count(actor) || credit.identifier != actor_identifier(actor)) continue;
            char body[512];
            if (credit.name.empty() || !read_recipe_definition(actor, body)) return failed("actor_identity_unreadable");
            checkpoint.identities[credit.name] = {credit.request, body, credit.ordinal, credit.credit.numerator, credit.credit.denominator};
        }
        for (const auto& [name, identity] : checkpoint.identities) { (void)name; identity_requests.insert(identity.request); }
        for (const auto& [key, credit] : death_action_credits) {
            if (key.first.rfind("independent/", 0) == 0 || key.first.rfind("summon/", 0) == 0 || key.first.rfind("direct/", 0) == 0 || identity_requests.count(key.first))
                checkpoint.actions[key] = {credit.units, credit.sent, credit.denominator};
        }
        const bool captured = checkpoint.capture(bytes);
        if (!captured) return failed("checkpoint_encoding_refused");
        return captured;
    }
    // native checkpoints restart the selected script; its commands recreate ordinals and credits.
    bytes = "EAPC1|" + (session_recipe_plan.empty() ? std::string("-") : session_recipe_plan) + "\n";
    return true;
}
bool restore_recipe_checkpoint(std::string_view bytes, std::string_view map) {
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    std::lock_guard<std::recursive_mutex> lock(quantity_mutex);
    if (map.empty()) return false;
    if (bytes.substr(0, 4) == "EAPN") {
        RecipeNativeCheckpoint checkpoint;
        if (!checkpoint.restore(bytes, map) || checkpoint.plan.size() != 64 ||
            checkpoint.plan.find_first_not_of("0123456789abcdef") != checkpoint.plan.npos ||
            (!session_recipe_plan.empty() && session_recipe_plan != checkpoint.plan)) return false;
        checkpoint_recipe_plan = checkpoint.plan;
        restored_independent = std::move(checkpoint); checkpoint_recipe_missing = false; return true;
    }
    if (bytes.empty()) {
        if (!session_recipe_plan.empty()) return false;
        restored_independent.reset();
        checkpoint_recipe_missing = true; checkpoint_recipe_plan.clear(); return true;
    }
    if (bytes.size() < 8 || bytes.substr(0, 6) != "EAPC1|" || bytes.back() != '\n') return false;
    const auto plan = bytes.substr(6, bytes.size() - 7);
    if (plan != "-" && (plan.size() != 64 || !std::all_of(plan.begin(), plan.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); }))) return false;
    if (plan != "-" && !session_recipe_plan.empty() && session_recipe_plan != plan) return false;
    checkpoint_recipe_plan = plan == "-" ? "" : std::string(plan);
    restored_independent.reset();
    checkpoint_recipe_missing = false;
    return true;
}
bool recipe_origin(uintptr_t alias, std::string& origin) {
    char name[512];
    constexpr std::string_view prefix = "ap_v070_request_r";
    if (!alias || !read_definition_name(alias, name)) return false;
    const std::string_view value(name);
    if (value.size() != prefix.size() + 20 || value.substr(0, prefix.size()) != prefix) return false;
    origin.assign(value.substr(prefix.size()));
    return true;
}
bool open_native_request(uintptr_t manager, const NativeRecipe& recipe, std::string_view label, std::string& key,
                          const std::string& parent_actor = {}, const std::string& parent_request = {}) {
    char owner[512];
    if (!read_definition_name(manager, owner) || next_recipe_request == UINT64_MAX) return false;
    auto candidate = recipe.definition.origin + "/" + std::to_string(++next_recipe_request);
    const auto& entry = recipe.definition;
    if (!recipe_state.request(candidate, {owner, entry.origin, std::string(label), entry.logical_mask, entry.logical_count,
        entry.physical_count, entry.maximum_emissions, entry.maximum_emissions, entry.final_credit_units, parent_actor, parent_request})) return false;
    key = std::move(candidate);
    trace_spawn_refusal(manager, recipe.definition.origin, "request_opened", "observed");
    return true;
}
bool vanilla_bridge_group(const NativeRecipe& recipe) {
    const auto& entry = recipe.definition;
    return entry.family == 'g' && entry.source.empty() && entry.owner == "game_encounter_manager_traindepot_1";
}
bool immora_breach_group(const NativeRecipe& recipe) {
    const auto& entry = recipe.definition;
    return entry.family == 'g' && entry.owner == "_example_encounter_manager_0" &&
        entry.source == "breach_wasteland_arena_encounter_spawn_group_zone_2";
}
uintptr_t countermeasure_body(const NativeRecipe& recipe, uintptr_t body, bool blood_punch, bool microwave, bool stone,
                             bool armored = true) {
    const auto blood = recipe.blood_punch_alternatives.find(body);
    if (!blood_punch && blood != recipe.blood_punch_alternatives.end()) return blood->second;
    const auto spirit = recipe.microwave_alternatives.find(body);
    if (!microwave && spirit != recipe.microwave_alternatives.end()) return spirit->second;
    const auto imp = recipe.stone_alternatives.find(body);
    if (!stone && imp != recipe.stone_alternatives.end()) return imp->second;
    const auto baron = recipe.armored_alternatives.find(body);
    return !armored && baron != recipe.armored_alternatives.end() ? baron->second : body;
}
void inherit_population(SpawnLimits& limits, uintptr_t parent) {
    if (!limits.population || !parent) return;
    char name[512];
    if (!read_recipe_definition(parent, name) && !read_definition_name(parent, name)) return;
    const auto owner = spawn_limits(name);
    if (!owner || !owner->scope) return;
    limits.population = owner->population; limits.superheavy = owner->superheavy;
    limits.pressure = owner->pressure; limits.mixed = owner->mixed; limits.scope = owner->scope; limits.heavy = owner->heavy;
    limits.strict = owner->strict;
    if (owner->strict) {
        limits.maximum = owner->maximum;
        limits.species_maximum = owner->species_maximum;
        if (limits.spirit_counter) {
            limits.heavy = (std::min)(owner->population, owner->heavy + owner->species_maximum[2]);
            limits.species_maximum[2] = 0;
        }
    }
}
bool finite_wave_ready(std::string_view owner) {
    for (const auto& [worker, key] : worker_recipes) {
        (void)worker;
        const auto found = recipe_state.requests().find(key);
        if (found == recipe_state.requests().end()) continue;
        const auto& progress = found->second;
        if (progress.closed || progress.request.owner != owner ||
            progress.emitted >= progress.request.maximum_emissions ||
            recipe_state.parent_pending(progress.request.parent_actor)) continue;
        const auto recipe = native_recipes.find(progress.request.origin);
        if (recipe == native_recipes.end() || std::string_view("igaspb").find(recipe->second.definition.family) == std::string_view::npos ||
            recipe->second.bodies.empty()) continue;
        const auto body = recipe->second.bodies[progress.emitted % recipe->second.bodies.size()];
        const auto limits = spawn_limits(*reinterpret_cast<const char**>(body + 8));
        if (limits && generated_admission(*limits)) return true;
    }
    return false;
}
uint64_t functional_token(std::string_view value);
bool begin_recipe_creation(RecipeCreation& creation, uintptr_t manager, std::string_view origin, std::string_view label) {
    if (recipe_fault.load(std::memory_order_acquire)) { trace_spawn_refusal(manager, origin, "recipe_fault"); return false; }
    if (!load_native_recipes()) { trace_spawn_refusal(manager, origin, "recipe_library_unavailable"); return false; }
    const auto found = native_recipes.find(std::string(origin));
    if (found == native_recipes.end()) { trace_spawn_refusal(manager, origin, "recipe_missing"); return false; }
    const auto& recipe = found->second;
    if (vanilla_bridge_group(recipe)) return false;
    if (worker_recipe) creation.key = *worker_recipe;
    else {
        auto& pending = pending_recipes[{manager, std::string(origin)}];
        if (pending.empty() && !open_native_request(manager, recipe, label, pending)) { trace_spawn_refusal(manager, origin, "request_not_opened"); return false; }
        creation.key = pending;
    }
    if (recipe.definition.family == 'm' && finite_wave_ready(recipe_state.requests().at(creation.key).request.owner)) { trace_spawn_refusal(manager, origin, "finite_wave_ready"); return false; }
    auto emission = recipe_state.reserve(creation.key);
    if (!emission) { trace_spawn_refusal(manager, origin, "recipe_reservation_unavailable"); return false; }
    creation.emission = std::move(*emission); creation.recipe = &recipe;
    creation.body = recipe.tail && emission->ordinal + 1 == recipe.definition.maximum_emissions ? recipe.tail :
        recipe.bodies[(emission->ordinal % recipe.bodies.size() + worker_body_offset % recipe.bodies.size()) % recipe.bodies.size()];
    const bool microwave = !recipe.microwave_alternatives.count(creation.body) || inventory::microwave_observed();
    const bool stone = !recipe.stone_alternatives.count(creation.body) || inventory::stone_counter_observed();
    const bool armored = !recipe.armored_alternatives.count(creation.body) || inventory::armored_counter_observed();
    const auto chosen = countermeasure_body(recipe, creation.body, inventory::blood_punch_observed(), microwave, stone, armored);
    if (chosen != creation.body) {
        const bool spirit = recipe.microwave_alternatives.count(creation.body) != 0;
        const bool imp = recipe.stone_alternatives.count(creation.body) != 0;
        const bool baron = recipe.armored_alternatives.count(creation.body) != 0;
        creation.body = chosen;
        if (recipe_spawn_traces.size() < 4096 && recipe_spawn_traces.emplace("counter/" + recipe.definition.origin, uint8_t{1}).second)
            commands::enemy_spawn_trace(spirit ? "microwave_fallback" : imp ? "stone_counter_fallback" :
                baron ? "armored_counter_fallback" : "blood_punch_fallback", recipe.definition.origin.c_str(),
                *reinterpret_cast<const char**>(chosen + 8), spirit ? "cacodemon" : imp ? "imp" : baron ? "baron" : "prowler", 0,
                creation.key.c_str(), recipe_state.requests().at(creation.key).request.parent_actor.c_str(), creation.emission.ordinal);
    }
    const auto name = *reinterpret_cast<const char**>(creation.body + 8);
    const auto limits = spawn_limits(name ? name : "");
    if (!limits) { trace_spawn_refusal(manager, origin, "spawn_limits_unavailable"); recipe_state.release(creation.key); return false; }
    creation.limits = *limits;
    if (recipe.definition.family == 'u') inherit_population(creation.limits, manager);
    // refills leave room for the finite waves that eventually stop them.
    if (recipe.definition.family == 'm' && !creation.limits.strict && creation.limits.population > 1) {
        creation.limits.population -= (std::min)(uint32_t{2}, creation.limits.population - 1);
        creation.limits.pressure = (std::min)(creation.limits.pressure, creation.limits.population);
    }
    if (!generated_admission(creation.limits)) { trace_spawn_refusal(manager, origin, admission_diagnostic_reason); recipe_state.release(creation.key); return false; }
    const auto source = immora_breach_group(recipe) ? "breach_wasteland_arena_target_spawn_60" : recipe.definition.source.c_str();
    creation.source = !*source ? 0 : find_entity(*reinterpret_cast<const uintptr_t*>(world_global), source);
    if (!creation.source && (recipe.definition.family == 'i' || recipe.definition.family == 'p' || immora_breach_group(recipe))) {
        // checkpoints can omit passive source entities; the compiler supplies a separate point.
        char fallback[64];
        sprintf_s(fallback, "ap_v070_source_%016llx", functional_token(source));
        const auto alias = find_entity(*reinterpret_cast<const uintptr_t*>(world_global), fallback);
        if (alias && *reinterpret_cast<const int32_t*>(alias + 0xc40) > 0) creation.source = alias;
    }
    reserve_admission(creation.limits); creation.reserved = true;
    creation.previous = recipe_creation; recipe_creation = &creation;
    return true;
}
bool settle_recipe_creation(RecipeCreation& creation) noexcept {
    try {
        if (!creation.actor) return creation.restoring || recipe_state.release(creation.key);
        const auto previous = recipe_actors.find(creation.actor);
        if (previous != recipe_actors.end()) recipe_state.actor_changed(previous->second, RecipeActorState::removed);
        char name[512];
        const std::string native_name = read_instance_name(creation.actor, name) ? name : "";
        if (creation.restoring ? !recipe_state.rebind_actor(creation.emission.actor, native_name) :
            !recipe_state.created(creation.key, creation.emission, native_name)) return false;
        for (const auto& member : creation.memberships)
            if (!recipe_state.join_actor(creation.emission.actor, member.owner, member.label)) return false;
        recipe_actors[creation.actor] = creation.emission.actor;
        recipe_actor_credits.erase(creation.actor);
        const auto& request = recipe_state.requests().at(creation.key).request;
        const auto identifier = actor_identifier(creation.actor);
        const auto units = request.credit_limit && creation.emission.ordinal + 1 == request.credit_limit ?
            request.final_credit_units : request.logical_units;
        recipe_actor_credits[creation.actor] = {identifier, {units, request.physical_units}, creation.key, native_name, creation.emission.ordinal};
        try {
            if (actor_diagnostics.size() < 4096 || actor_diagnostics.count(creation.actor)) {
                auto& diagnostic = actor_diagnostics[creation.actor];
                if (diagnostic.identifier != identifier) diagnostic = {};
                diagnostic.identifier = identifier; diagnostic.encounter = creation.key;
                diagnostic.manager = request.owner;
                trace_actor(creation.actor, creation.restoring ? "restored_position" : "spawn_position", 0, true);
            }
        } catch (...) {}
        return true;
    } catch (...) { recipe_fault.store(true, std::memory_order_release); return false; }
}
void end_recipe_creation(RecipeCreation& creation) noexcept {
    try {
        if (!creation.actor) trace_recipe_spawn(creation, creation.body, creation.source, 2);
        if (creation.actor && creation.active_point && !record_point_credit(creation.active_point, &creation))
            recipe_fault.store(true, std::memory_order_release);
    } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    if (!settle_recipe_creation(creation)) recipe_fault.store(true, std::memory_order_release);
    if (creation.reserved) release_admission(creation.limits);
    recipe_creation = creation.previous;
}
RecipeCreation::PointState read_point_state(uintptr_t point) {
    return {*reinterpret_cast<const uintptr_t*>(point + 0xc38), *reinterpret_cast<const int32_t*>(point + 0xc40),
        *reinterpret_cast<const int32_t*>(point + 0xc54), *reinterpret_cast<const int32_t*>(point + 0xcd0),
        *reinterpret_cast<const uint32_t*>(point + 0xd00)};
}
void write_point_state(uintptr_t point, const RecipeCreation::PointState& state) {
    *reinterpret_cast<uintptr_t*>(point + 0xc38) = state.definitions;
    *reinterpret_cast<int32_t*>(point + 0xc40) = state.count;
    *reinterpret_cast<int32_t*>(point + 0xc54) = state.index;
    *reinterpret_cast<int32_t*>(point + 0xcd0) = state.animation;
    *reinterpret_cast<uint32_t*>(point + 0xd00) = state.state;
}
uint32_t physical_spawn_override(uint32_t state) {
    switch (state) {
    case 6: case 7: return 12;
    case 13: return 0;
    case 16: return 17;
    case 9: return 10;
    default: return state;
    }
}
uint64_t functional_token(std::string_view value) {
    uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : value) { hash ^= static_cast<uint8_t>(byte); hash *= 1099511628211ULL; }
    return hash;
}
uintptr_t functional_body(RecipeCreation* creation, uintptr_t group) {
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    if (!find_entity(world, "ap_v070_functional_sources")) return creation->body;
    auto source = creation->original;
    const bool inherited = source.count < 1 && group;
    if (inherited) source = read_point_state(group);
    if (source.count < 1) return creation->body;
    if (source.count > 100000 || source.index >= source.count || !source.definitions) return 0;
    if (source.index < 0) {
        auto& random = *reinterpret_cast<uint32_t*>(world + 0x9a038);
        random = random * 0x19660dU + 0x3c6ef35fU;
        source.index = static_cast<int32_t>((random >> 10 & 0x7fff) % source.count);
        if (inherited) *reinterpret_cast<int32_t*>(group + 0xc54) = source.index;
        else creation->original.index = source.index;
    }
    std::string source_name, body_name;
    if (!native_text(source.definitions + static_cast<uintptr_t>(source.index) * 0x80, source_name, 511)) return 0;
    char library_name[64];
    sprintf_s(library_name, "ap_v070_functional_%016llx", functional_token(source_name));
    const auto library = find_entity(world, library_name);
    if (!library) return creation->body;
    if (!native_text(creation->body, body_name, 511)) return 0;
    char variant[96];
    sprintf_s(variant, "%s_%016llx", library_name, functional_token(body_name));
    const auto data = *reinterpret_cast<const uintptr_t*>(library + 0xc38);
    const auto count = *reinterpret_cast<const int32_t*>(library + 0xc40);
    if (count < 1 || count > 100000 || !data) return 0;
    for (int32_t index = 0; index < count; ++index) {
        const auto entry = data + static_cast<uintptr_t>(index) * 0x80;
        std::string name;
        if (!native_text(entry, name, 511)) return 0;
        if (name == variant) return entry;
    }
    commands::enemy_spawn_trace("functional_missing", creation->recipe->definition.origin.c_str(), variant, source_name.c_str(), 0,
        creation->key.c_str(), recipe_state.requests().at(creation->key).request.parent_actor.c_str(), creation->emission.ordinal);
    recipe_fault.store(true, std::memory_order_release); return 0;
}
void suspend_recipe_points(RecipeCreation* creation) {
    for (auto previous = creation->previous; previous; previous = previous->previous) if (previous->override_active) {
        previous->suspended = read_point_state(previous->active_point);
        write_point_state(previous->active_point, previous->original);
        previous->override_active = false; previous->suspended_by = creation;
    }
}
void resume_recipe_points(RecipeCreation* creation) {
    for (auto previous = creation->previous; previous; previous = previous->previous) if (previous->suspended_by == creation) {
        write_point_state(previous->active_point, previous->suspended);
        previous->suspended_by = nullptr; previous->override_active = true;
    }
}
void initialize_physical_recipe(RecipeCreation* creation, uintptr_t actor) {
    if (creation->limits.kind != 2 || creation->recipe->definition.family == 's') return;
    const auto spirit = *reinterpret_cast<const uintptr_t*>(actor + 0x165d8);
    if (!spirit) { recipe_fault.store(true, std::memory_order_release); return; }
    *reinterpret_cast<uint64_t*>(spirit + 0x80) = encounter_ai_mask;
    assign_native_text(spirit + 0x88, "");
    *reinterpret_cast<uint8_t*>(spirit + 0xb8) = 0;
}
bool synchronize_point_credits(uintptr_t point, PointCredits& credits) {
    const auto count = *reinterpret_cast<const int32_t*>(point + 0xd90);
    if (count < 0) return false;
    if (count < credits.observed) { credits = {}; credits.units[1] = count; }
    else {
        const auto delta = static_cast<uint32_t>(count - credits.observed);
        auto& units = credits.units[1];
        if (units > UINT64_MAX - delta) return false;
        units += delta;
    }
    credits.observed = count; return true;
}
bool record_point_credit(uintptr_t point, const RecipeCreation* creation) {
    auto& credits = point_credits[point];
    const auto count = *reinterpret_cast<const int32_t*>(point + 0xd90);
    if (count < credits.observed) return synchronize_point_credits(point, credits);
    if (count == credits.observed) return true;
    if (count < 0) return false;
    auto& ordinary = credits.units[1];
    const auto untracked = static_cast<uint32_t>(count - credits.observed - 1);
    if (ordinary > UINT64_MAX - untracked) return false;
    ordinary += untracked;
    const auto& request = recipe_state.requests().at(creation->key).request;
    const auto units = request.credit_limit && creation->emission.ordinal + 1 == request.credit_limit ?
        request.final_credit_units : request.logical_units;
    auto& total = credits.units[request.physical_units];
    if (total > UINT64_MAX - units) return false;
    total += units; credits.observed = count; return true;
}
bool recipe_landing_pose(const RecipeCreation&, float*);
uint64_t invoke_point_available(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player,
                                uint8_t fov, uint8_t los, RecipeCreation* creation) {
    const auto state = read_point_state(point);
    const auto maximum = *reinterpret_cast<const int32_t*>(point + 0xb88);
    const auto bounds = *reinterpret_cast<const int32_t*>(point + 0xb90);
    float destination[12], original[12];
    const auto active_point = creation ? creation->active_point : 0;
    if (creation) creation->active_point = point;
    const bool relocated = creation && recipe_landing_pose(*creation, destination);
    if (creation) creation->active_point = active_point;
    uint64_t result = 0;
    __try {
        *reinterpret_cast<int32_t*>(point + 0xb88) = 0;
        if (creation) write_point_state(point, {creation->body, 1, 0, 0, 0});
        if (relocated) {
            std::memcpy(original, reinterpret_cast<const void*>(point + 0x74), sizeof(original));
            independent_pose(point, destination);
            *reinterpret_cast<int32_t*>(point + 0xb90) = 2;
        }
        result = original_point_available(point, group, time, player, fov, los);
    } __finally {
        if (relocated) independent_pose(point, original);
        if (creation) write_point_state(point, state);
        *reinterpret_cast<int32_t*>(point + 0xb88) = maximum;
        *reinterpret_cast<int32_t*>(point + 0xb90) = bounds;
    }
    return result;
}
uint64_t point_available_locked(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player, uint8_t fov, uint8_t los) {
    if (stopped.load(std::memory_order_acquire)) return original_point_available(point, group, time, player, fov, los);
    auto creation = recipe_creation;
    if (creation && creation->creating && creation->active_point != point) creation = nullptr;
    const auto found = point_credits.find(point);
    if (!creation && found == point_credits.end()) return original_point_available(point, group, time, player, fov, los);
    try {
        auto& credits = point_credits[point];
        if (!synchronize_point_credits(point, credits)) return 0;
        const auto maximum = *reinterpret_cast<const int32_t*>(point + 0xb88);
        if (maximum > 0) {
            std::vector<RecipeCredit> values;
            for (const auto& [denominator, units] : credits.units) values.push_back({units, denominator});
            if (credit_at_least(values, maximum)) return 0;
        }
        const auto result = invoke_point_available(point, group, time, player, fov, los, creation);
        if (!result && creation) trace_recipe_spawn(*creation, creation->body, point, 64);
        return result;
    } catch (...) { recipe_fault.store(true, std::memory_order_release); return 0; }
}
uint64_t point_available_detour(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player, uint8_t fov, uint8_t los) {
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return [&]() -> uint64_t {
        __try { return point_available_locked(point, group, time, player, fov, los); }
        __finally { admission.unlock(); }
    }();
}
void trace_recipe_spawn(const RecipeCreation& creation, uintptr_t body, uintptr_t point, uint8_t stage) {
    if (!creation.recipe || creation.emission.actor.empty()) return;
    std::string planned;
    if (!native_text(creation.body, planned, 511)) return;
    const auto key = creation.key + "/" + std::to_string(creation.emission.ordinal) + "/" + planned;
    if (!recipe_spawn_traces.count(key) && recipe_spawn_traces.size() >= 4096) return;
    auto& stages = recipe_spawn_traces[key];
    if (stages & stage) return;
    stages |= stage;
    std::string definition;
    char point_name[512]{};
    if (!native_text(body, definition, 511)) return;
    if (point) read_definition_name(point, point_name);
    const auto request = recipe_state.requests().find(creation.key);
    commands::enemy_spawn_trace(stage == 1 ? "attempt" : stage == 2 ? "retry" : stage == 4 ? "created" :
        stage == 8 ? "factory" : stage == 16 ? "candidate_rejected" : stage == 32 ? "index_rejected" : "unavailable",
        creation.recipe->definition.origin.c_str(), definition.c_str(), point_name, creation.actor, creation.key.c_str(),
        request == recipe_state.requests().end() ? "" : request->second.request.parent_actor.c_str(), creation.emission.ordinal);
    const auto& limits = creation.limits;
    commands::diagnostic_trace("AP_ENEMY_SPAWN_LIMIT state=%u recipe=%.20s request=%.96s species=%llx scope=%llx population=%u pressure=%u heavy=%u superheavy=%u mixed=%u strict=%d maximum=%u,%u,%u,%u species_max=%u,%u,%u,%u,%u,%u,%u,%u,%u",
        stage, creation.recipe->definition.origin.c_str(), creation.key.c_str(),
        static_cast<unsigned long long>(limits.physical), static_cast<unsigned long long>(limits.scope),
        limits.population, limits.pressure, limits.heavy, limits.superheavy, limits.mixed, limits.strict,
        limits.maximum[0], limits.maximum[1], limits.maximum[2], limits.maximum[3],
        limits.species_maximum[0], limits.species_maximum[1], limits.species_maximum[2], limits.species_maximum[3],
        limits.species_maximum[4], limits.species_maximum[5], limits.species_maximum[6], limits.species_maximum[7], limits.species_maximum[8]);
}
bool recipe_arena_point(uintptr_t point) {
    if (!recipe_creation) return true;
    const auto& entry = recipe_creation->recipe->definition;
    if (entry.family != 'g' || entry.owner != "ai_encounter_manager_dlc1_skyscraper_upper_last" ||
        entry.source != "ai_encounter_spawn_group_zone_skyscraper_upper_last") return true;
    // this group also contains aerial and entrance points outside the final arena.
    char name[512];
    return read_definition_name(point, name) &&
        (std::string_view(name) == "ai_target_spawn_96" || std::string_view(name) == "ai_target_spawn_97" ||
         std::string_view(name) == "ai_target_spawn_98" || std::string_view(name) == "ai_target_spawn_99" ||
         std::string_view(name) == "ai_target_spawn_100" || std::string_view(name) == "ai_target_spawn_101" ||
         std::string_view(name) == "ai_target_spawn_102");
}
uintptr_t recipe_factory(uintptr_t point, uintptr_t group) {
    if (!recipe_arena_point(point)) return 0;
    const auto creation = recipe_creation;
    const auto state = read_point_state(point);
    const bool prepared = creation->override_active && creation->active_point == point;
    if (!prepared) creation->original = state;
    creation->active_point = point;
    trace_recipe_spawn(*creation, creation->body, point, 8);
    uintptr_t actor = 0;
    __try {
        auto& credits = point_credits[point];
        if (!synchronize_point_credits(point, credits)) { recipe_fault.store(true); return 0; }
        const auto body = functional_body(creation, group);
        if (!body) { recipe_fault.store(true, std::memory_order_release); return 0; }
        float destination[12];
        const auto physical_state = recipe_landing_pose(*creation, destination) && creation->original.state != 9 ?
            12u : physical_spawn_override(creation->original.state);
        write_point_state(point, {body, 1, 0, 0, physical_state});
        creation->creating = creation->override_active = true;
        trace_recipe_spawn(*creation, body, point, 1);
        actor = original_point_factory(point, group);
        if (actor) {
            float landing[12];
            if (recipe_landing_pose(*creation, landing)) {
                const auto name=*reinterpret_cast<const char**>(creation->body+8);
                landing[2]+=name ? spawn_lift(name) : 0.0f;
                independent_pose(actor,landing);
            }
            creation->actor = actor; initialize_physical_recipe(creation, actor);
        }
        trace_recipe_spawn(*creation, body, point, actor ? 4 : 2);
    } __finally {
        creation->creating = false; creation->override_active = prepared;
        write_point_state(point, prepared ? state : creation->original);
    }
    return actor;
}
save::NativeString recipe_text(const std::string& text) {
    return {native_string_table, const_cast<char*>(text.c_str()), static_cast<int32_t>(text.size()),
        static_cast<uint32_t>(text.size() + 1), {}};
}
uintptr_t recipe_parent(const std::string& role) {
    if (role.empty()) return 0;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto collection = *reinterpret_cast<const uintptr_t*>(world + 0x62088);
    const auto data = *reinterpret_cast<const uintptr_t*>(collection);
    const auto count = *reinterpret_cast<const int32_t*>(collection + 8);
    if (count < 0 || count > 1000000) return 0;
    for (int32_t i = 0; i < count; ++i) {
        const auto actor = resolve_actor(data + static_cast<uintptr_t>(i) * 24);
        const auto found = recipe_actors.find(actor);
        if (found != recipe_actors.end() && found->second == role && !combat::confirmed_dead(world, actor) &&
            !reinterpret_cast<bool(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
                *reinterpret_cast<const uintptr_t*>(actor) + 0x108))(actor)) return actor;
    }
    return 0;
}
bool special_recipe_call(RecipeCreation* creation, uintptr_t manager) {
    const auto& entry = creation->recipe->definition;
    if (entry.family != 'a' && entry.family != 'b' && entry.family != 's') return false;
    const auto& request = recipe_state.requests().at(creation->key).request;
    const auto& birth_label = request.label;
    const auto& first = entry.family == 's' ? entry.first_label : birth_label;
    alignas(8) uint8_t output[32]{};
    if (entry.family == 'a') {
        const auto arch_template = find_entity(*reinterpret_cast<const uintptr_t*>(world_global), entry.call_entity.c_str());
        original_archvile(manager, reinterpret_cast<uintptr_t>(output), creation->source, arch_template,
            reinterpret_cast<uintptr_t>(first.c_str()), entry.second_label.c_str());
    } else if (entry.family == 'b') {
        original_buff(manager, reinterpret_cast<uintptr_t>(output), creation->source,
            reinterpret_cast<uintptr_t>(first.c_str()), entry.second_label.c_str());
    } else if (creation->limits.kind != 2) {
        // a replacement without a parasite uses the ordinary spawn path.
        original_single(manager, reinterpret_cast<uintptr_t>(output), entry.logical_mask, creation->source,
            reinterpret_cast<uintptr_t>(birth_label.c_str()));
    } else {
        original_spirit_helper(manager, entry.allowed_hosts, first.c_str(), creation->source,
            0, entry.call_flag, 0, recipe_parent(request.parent_actor));
    }
    return true;
}
uint64_t recipe_point_type(uintptr_t point) {
    if (!recipe_creation || recipe_creation->creating || recipe_creation->override_active ||
        recipe_creation->source != point || !point) return 0;
    const auto name = *reinterpret_cast<const char**>(recipe_creation->body + 8);
    const auto tracking = spawn_tracking(name ? name : "");
    return tracking ? tracking->physical : 0;
}
uint64_t create_tracked(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint64_t);
uint64_t invoke_recipe_creation(RecipeCreation* creation, uintptr_t manager, uintptr_t record,
                                uintptr_t fourth, uintptr_t fifth, bool group, std::unique_lock<std::recursive_mutex>& admission) {
    uint64_t result = 0;
    __try {
        suspend_recipe_points(creation);
        trace_recipe_spawn(*creation, creation->body, creation->source, 1);
        if (special_recipe_call(creation, manager)) result = creation->actor ? 1 : 0;
        else if (!creation->source && (creation->recipe->definition.family == 'i' || creation->recipe->definition.family == 'p' ||
                                      immora_breach_group(*creation->recipe))) result = 0;
        else if (group && creation->recipe->definition.family != 'i' && creation->recipe->definition.family != 'p' &&
                 !immora_breach_group(*creation->recipe)) {
            const auto type = *reinterpret_cast<uint64_t*>(record);
            __try {
                // group points can contain a different vanilla species than the script requests.
                *reinterpret_cast<uint64_t*>(record) = (type & ~encounter_ai_mask) | encounter_ai_mask;
                result = original_group(manager, reinterpret_cast<uint64_t*>(record), creation->source);
            } __finally { *reinterpret_cast<uint64_t*>(record) = type; }
        }
        else {
            const auto physical = recipe_point_type(creation->source);
            result = physical ? create_tracked(manager, record, creation->source, fourth, fifth, physical) :
                original_spawn(manager, record, creation->source, fourth, fifth);
        }
    } __finally {
        resume_recipe_points(creation);
        end_recipe_creation(*creation);
        admission.unlock();
    }
    return result;
}
uint64_t create_special_recipe(uintptr_t manager, std::string_view origin, std::string_view label) {
    RecipeCreation creation;
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    bool ready = false;
    try { ready = begin_recipe_creation(creation, manager, origin, label); } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    if (!ready) { admission.unlock(); return 0; }
    return invoke_recipe_creation(&creation, manager, 0, 0, 0, false, admission);
}
bool recipe_landing_pose(const RecipeCreation& creation, float* pose) {
    if (!creation.active_point || creation.restoring) return false;
    char point[512], marker[64];
    if (!read_definition_name(creation.active_point, point)) return false;
    const auto name = std::string_view(point).rfind("ap_v070_source_", 0) == 0 ?
        std::string_view(creation.recipe->definition.source) : std::string_view(point);
    sprintf_s(marker, "ap_v070_landing_%016llx", functional_token(name));
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto helper = find_entity(world, marker);
    if (!helper) return false;
    std::string target_name;
    if (!native_text(helper + 0xbe0, target_name, 191)) { recipe_fault.store(true); return false; }
    const auto target = find_entity(world, target_name.c_str());
    if (!target) { recipe_fault.store(true); return false; }
    std::memcpy(pose, reinterpret_cast<const void*>(target + 0x74), 12 * sizeof(float));
    if (!std::all_of(pose, pose + 12, [](float value) { return std::isfinite(value); })) {
        recipe_fault.store(true); return false;
    }
    return true;
}
uintptr_t actor_factory_detour(uintptr_t info) {
    if (stopped.load(std::memory_order_acquire)) return original_actor_factory(info);
    const auto creation = recipe_creation;
    if (!creation || !creation->creating || actor_factory_active)
        return original_actor_factory(info);
    const auto name = creation->body ? *reinterpret_cast<const char**>(creation->body + 8) : nullptr;
    const float lift = name && !creation->restoring ? spawn_lift(name) : 0.0f;
    const bool summoned = creation->recipe->definition.family == 'u';
    float landing[12];
    const bool relocated = recipe_landing_pose(*creation, landing);
    if (recipe_fault.load()) return 0;
    if (!summoned && !lift && !relocated) return original_actor_factory(info);
    const auto override_state = *reinterpret_cast<const uint32_t*>(info + 0x70);
    float pose[12]; std::memcpy(pose, reinterpret_cast<const void*>(info + 0x40), sizeof(pose));
    uintptr_t actor = 0;
    __try {
        actor_factory_active = true;
        if (summoned) *reinterpret_cast<uint32_t*>(info + 0x70) = physical_spawn_override(override_state);
        if (relocated) std::memcpy(reinterpret_cast<void*>(info + 0x40), landing, sizeof(landing));
        *reinterpret_cast<float*>(info + 0x48) += lift;
        actor = original_actor_factory(info);
        if (actor && summoned) { creation->actor = actor; initialize_physical_recipe(creation, actor); }
    } __finally {
        *reinterpret_cast<uint32_t*>(info + 0x70) = override_state;
        std::memcpy(reinterpret_cast<void*>(info + 0x40), pose, sizeof(pose));
        actor_factory_active = false;
    }
    return actor;
}
void role_text_detour(uintptr_t destination, const char* text) {
    if (stopped.load(std::memory_order_acquire)) { assign_native_text(destination, text); return; }
    // the native Samuel helper addresses its Spirit filter through a nullable physical component.
    if (samuel_without_parasite && destination == 0x88) return;
    assign_native_text(destination, text);
}
void samuel_setup_detour(uintptr_t component, uintptr_t actor) {
    if (stopped.load(std::memory_order_acquire)) { original_samuel_setup(component, actor); return; }
    const bool previous = samuel_without_parasite;
    const auto creation = recipe_creation;
    __try {
        samuel_without_parasite = creation && creation->recipe->definition.family == 'u' &&
            creation->actor == actor && !*reinterpret_cast<const uintptr_t*>(actor + 0x165d8);
        original_samuel_setup(component, actor);
    } __finally { samuel_without_parasite = previous; }
}
#include "darklord_telemetry.h"
void darklord_summon_detour(uintptr_t component) {
    if (stopped.load(std::memory_order_acquire)) { original_darklord_summon(component); return; }
    std::lock_guard<std::recursive_mutex> lock(admission_mutex);
    auto telemetry = darklord_begin(component);
    darklord_slots(telemetry.get(), component);
    // MSVC requires SEH cleanup in a scope without C++ destructors.
    [component, call = telemetry.get()] {
        darklord_call_start(call);
        __try { original_darklord_summon(component); }
        __finally { darklord_end(call, AbnormalTermination() != FALSE, call ? elapsed_us(call->call_start) : 0); }
    }();
    darklord_watch(std::move(telemetry));
}
const NativeRecipe* summon_recipe(uintptr_t component, uint64_t monster_type, uint64_t logical_type) {
    std::string source;
    if (*reinterpret_cast<const uint8_t*>(component + 0x72)) {
        const auto identifier = *reinterpret_cast<const uint32_t*>(component + 0x78);
        const auto cached = *reinterpret_cast<const uint32_t*>(component + 0x7c);
        if (identifier != cached || (identifier != 0x1fffffe && !*reinterpret_cast<const uintptr_t*>(component + 0x80)))
            refresh_entity(component + 0x78, identifier == cached ? component + 0x78 : component + 0x7c);
        const auto target = *reinterpret_cast<const uintptr_t*>(component + 0x80);
        char name[512];
        if (target && read_definition_name(target, name)) source = name;
    }
    const NativeRecipe* fallback = nullptr;
    for (const auto& [origin, recipe] : native_recipes) {
        (void)origin;
        if (recipe.definition.family != 'u') continue;
        const bool exact = logical_type && !(logical_type & ~encounter_ai_mask) && !(logical_type & (logical_type - 1));
        if (exact ? recipe.definition.logical_mask != logical_type :
            !(monster_mask(recipe.definition.logical_mask) & monster_type)) continue;
        if (!source.empty() && recipe.definition.source == source) return &recipe;
        if (recipe.definition.source.empty()) fallback = &recipe;
    }
    return fallback;
}
bool collect_summon(uintptr_t component, uintptr_t entry, uintptr_t positions, uint64_t mask, uintptr_t label) {
    if (!summon_collection || summon_collection->component != component || !load_native_recipes()) return false;
    const auto count = *reinterpret_cast<const int32_t*>(positions + 8);
    if (!entry || !*reinterpret_cast<const uintptr_t*>(entry) || count < 1) return true;
    if (count > 100000) return false;
    const auto recipe = summon_recipe(component, *reinterpret_cast<const uint64_t*>(entry + 0x18), mask);
    if (!recipe) return false;
    SummonSource source;
    source.origin = recipe->definition.origin; source.mask = mask;
    if (*reinterpret_cast<const uint8_t*>(component + 0x72)) {
        char name[512];
        const auto target = *reinterpret_cast<const uintptr_t*>(component + 0x80);
        if (target && read_definition_name(target, name)) source.template_name = name;
    }
    if (!native_text(label, source.label, 1024)) return false;
    std::memcpy(source.entry.data(), reinterpret_cast<const void*>(entry), source.entry.size());
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    auto& random = *reinterpret_cast<uint32_t*>(world + 0x9a038);
    random = random * 0x19660d + 0x3c6ef35f;
    const auto index = (random >> 10 & 0x7fff) % static_cast<uint32_t>(count);
    const auto points = *reinterpret_cast<const uintptr_t*>(positions);
    std::memcpy(source.position.data(), reinterpret_cast<const void*>(points + index * 12), 12);
    summon_collection->sources.push_back(std::move(source));
    *reinterpret_cast<int32_t*>(positions + 8) = count - 1;
    if (index != static_cast<uint32_t>(count - 1))
        std::memcpy(reinterpret_cast<void*>(points + index * 12), reinterpret_cast<const void*>(points + (count - 1) * 12), 12);
    return true;
}
bool protected_summon_definition(uintptr_t entry) {
    if (!entry) return false;
    auto definition = *reinterpret_cast<const uintptr_t*>(entry);
    for (unsigned depth = 0; definition && depth < 64; ++depth) {
        const auto name = *reinterpret_cast<const char**>(definition + 8);
        if (name) {
            const std::string_view value(name);
            if (value.substr(0, 8) == "ai/boss/" || value.substr(0, 12) == "ai/friendly/") return true;
        }
        definition = *reinterpret_cast<const uintptr_t*>(definition + 0x158);
    }
    return false;
}
float summon_create_detour(uintptr_t component, uintptr_t entry, uintptr_t positions, uint64_t mask, uintptr_t label) {
    if (stopped.load(std::memory_order_acquire)) return original_summon_create(component, entry, positions, mask, label);
    if (!summon_collection) return original_summon_create(component, entry, positions, mask, label);
    if (protected_summon_definition(entry)) return original_summon_create(component, entry, positions, mask, label);
    bool accepted = false;
    try { accepted = collect_summon(component, entry, positions, mask, label); } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    destroy_native_text(label);
    if (!accepted) recipe_fault.store(true, std::memory_order_release);
    return 1.0f;
}
bool summon_labels(uintptr_t parent, std::string& label, bool& shield) {
    if (!summon_label_symbol) return false;
    const auto blackboard = *reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(parent + 0x5960) + 0xcd78);
    const auto symbol = *reinterpret_cast<const uintptr_t*>(summon_label_symbol);
    if (!blackboard || !symbol) return false;
    shield = false; label.clear();
    for (size_t i = 0; i < 256; ++i) {
        const auto name = *reinterpret_cast<const char**>(blackboard + 0x400 + i * 8);
        if (!name) return false;
        shield |= std::strcmp(name, "summon_shield") == 0;
        if (reinterpret_cast<uintptr_t>(name) != symbol || !*name) continue;
        if (i == 255 || *reinterpret_cast<const uintptr_t*>(blackboard + 0x408 + i * 8) != symbol) return false;
        const auto value = *reinterpret_cast<const char**>(blackboard + i * 4);
        if (!value) return false;
        size_t size = 0;
        while (size <= 1024 && value[size]) ++size;
        if (size > 1024) return false;
        label.assign(value, size);
        ++i;
    }
    return true;
}
bool summon_memberships(uintptr_t parent, std::string_view source_owner, std::string_view source_label,
                         std::vector<RecipeMembership>& result) {
    std::string label; bool shield = false;
    if (!summon_labels(parent, label, shield)) return false;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto managers = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
    const auto count = *reinterpret_cast<const int32_t*>(world + 0x126660);
    if (count < 0 || count > 100000) return false;
    for (int32_t i = 0; i < count; ++i) {
        const auto manager = *reinterpret_cast<const uintptr_t*>(managers + static_cast<uintptr_t>(i) * 0x20 + 8);
        if (!manager || !manager_contains(manager, parent)) continue;
        char name[512];
        if (!read_definition_name(manager, name)) return false;
        RecipeMembership member{name, label};
        if (std::find(result.begin(), result.end(), member) == result.end()) result.push_back(std::move(member));
    }
    if (!source_owner.empty() && !shield) {
        RecipeMembership member{std::string(source_owner), std::string(source_label)};
        if (std::find(result.begin(), result.end(), member) == result.end()) result.push_back(std::move(member));
    }
    return true;
}
void summon_request_complete_detour(uintptr_t target) {
    if (stopped.load(std::memory_order_acquire)) { original_summon_request_complete(target); return; }
    if (!summon_collection) { original_summon_request_complete(target); return; }
    char name[512];
    if (!read_definition_name(target, name)) { recipe_fault.store(true, std::memory_order_release); return; }
    summon_collection->notifications.emplace_back(name);
}
void complete_summon_requests(ComponentSummons& pending) {
    for (auto notification = pending.notifications.begin(); notification != pending.notifications.end();) {
        const auto complete = std::all_of(notification->second.begin(), notification->second.end(), [](const auto& key) {
            const auto& progress = recipe_state.requests().at(key);
            return progress.closed || progress.emitted >= progress.request.maximum_emissions;
        });
        if (!complete) { ++notification; continue; }
        const auto target = find_entity(*reinterpret_cast<const uintptr_t*>(world_global), notification->first.c_str());
        if (!target) { ++notification; continue; }
        original_summon_request_complete(target);
        notification = pending.notifications.erase(notification);
    }
}
void summon_cleanup_detour(uintptr_t component) {
    if (stopped.load(std::memory_order_acquire)) { original_summon_cleanup(component); return; }
    std::lock_guard<std::recursive_mutex> lock(admission_mutex);
    const auto found = component_summons.find(component);
    if (found != component_summons.end()) {
        auto& pending = found->second;
        for (auto request = pending.requests.begin(); request != pending.requests.end();) {
            if (request->completed) { ++request; continue; }
            recipe_state.cancel(request->key);
            request = pending.requests.erase(request);
        }
        complete_summon_requests(pending);
    }
    original_summon_cleanup(component);
}
bool open_component_summons(SummonCollection& collection) {
    if (collection.sources.empty() && collection.notifications.empty()) return true;
    const auto parent = *reinterpret_cast<const uintptr_t*>(collection.component + 8);
    const auto identifier = actor_identifier(parent);
    auto& pending = component_summons[collection.component];
    if (pending.parent != parent || pending.identifier != identifier) {
        for (const auto& request : pending.requests) recipe_state.cancel(request.key);
        complete_summon_requests(pending);
        pending = {}; pending.parent = parent; pending.identifier = identifier;
        char name[512];
        if (!read_instance_name(parent, name) || !*name) return false;
        pending.name = name;
    }
    pending.completed = false;
    if (pending.epoch == UINT64_MAX) return false;
    const auto epoch = pending.epoch++;
    std::map<std::pair<std::string, std::string>, std::vector<SummonSource>> groups;
    for (auto& source : collection.sources) groups[{source.origin, source.label}].push_back(std::move(source));
    for (auto& [identity, sources] : groups) {
        const auto& definition = native_recipes.at(identity.first).definition;
        const auto original_count = static_cast<uint32_t>(sources.size());
        const auto numerator = definition.quantity_denominator ? definition.quantity_numerator : definition.physical_count;
        const auto denominator = definition.quantity_denominator ? definition.quantity_denominator : definition.logical_count;
        const auto quantity = (uint64_t{original_count} * numerator + denominator - 1) / denominator;
        if (quantity > INT32_MAX || next_recipe_request == UINT64_MAX) return false;
        char owner[512];
        if (!read_definition_name(parent, owner)) return false;
        const auto key = "summon/" + definition.origin + "/" + std::to_string(++next_recipe_request);
        if (!recipe_state.request(key, {owner, definition.origin, identity.second, definition.logical_mask, original_count,
                static_cast<uint32_t>(quantity), quantity, static_cast<uint32_t>(quantity), quantity ? original_count : 0,
                "", ""})) return false;
        if (!quantity) {
            if (!recipe_state.complete_empty(key)) return false;
            std::vector<RecipeMembership> members;
            if (!summon_memberships(parent, sources.front().template_name, identity.second, members)) return false;
            for (const auto& member : members) {
                if (!recipe_state.join_empty(key, member.owner, member.label)) return false;
            }
            continue;
        }
        uint64_t offset = 0;
        if (definition.source.empty()) {
            char name[512];
            if (!read_recipe_definition(parent, name) && !read_instance_name(parent, name)) return false;
            offset = 14695981039346656037ull;
            for (auto cursor = name; *cursor; ++cursor) offset = (offset ^ static_cast<unsigned char>(*cursor)) * 1099511628211ull;
            offset = (offset ^ epoch) * 1099511628211ull;
        }
        pending.requests.push_back({key, definition.origin, std::move(sources), offset});
    }
    for (const auto& target : collection.notifications) {
        auto& dependencies = pending.notifications[target];
        for (const auto& request : pending.requests)
            if (!request.sources.empty() && request.sources.front().template_name == target) dependencies.insert(request.key);
    }
    complete_summon_requests(pending);
    return true;
}
void invoke_summon_creation(RecipeCreation* creation, uintptr_t component, const SummonSource* source) {
    auto entry = source->entry;
    auto position = source->position;
    struct Positions { uintptr_t data; int32_t count, capacity; uint32_t flags; } positions{
        reinterpret_cast<uintptr_t>(position.data()), 1, 1, 0x1050001};
    auto label = recipe_text(source->label);
    __try {
        const auto name = *reinterpret_cast<const char**>(creation->body + 8);
        const auto definition = lookup_definition(definition_database(), name, 1);
        if (!definition) { recipe_fault.store(true, std::memory_order_release); return; }
        *reinterpret_cast<uintptr_t*>(entry.data()) = definition;
        creation->creating = true;
        original_summon_create(component, reinterpret_cast<uintptr_t>(entry.data()), reinterpret_cast<uintptr_t>(&positions),
            source->mask, reinterpret_cast<uintptr_t>(&label));
    } __finally { creation->creating = false; end_recipe_creation(*creation); }
}
RecipeNativeCheckpoint::Source freeze_summon_source(const SummonSource& source) {
    RecipeNativeCheckpoint::Source saved;
    std::memcpy(&saved.monster, source.entry.data() + 0x18, sizeof(saved.monster));
    std::memcpy(&saved.offset, source.entry.data() + 0x28, sizeof(saved.offset));
    saved.mask = source.mask; saved.position = source.position;
    saved.origin = source.origin; saved.label = source.label; saved.template_name = source.template_name;
    return saved;
}
SummonSource thaw_summon_source(const RecipeNativeCheckpoint::Source& saved) {
    SummonSource source;
    std::memcpy(source.entry.data() + 0x18, &saved.monster, sizeof(saved.monster));
    std::memcpy(source.entry.data() + 0x28, &saved.offset, sizeof(saved.offset));
    source.mask = saved.mask; source.position = saved.position;
    source.origin = saved.origin; source.label = saved.label; source.template_name = saved.template_name;
    return source;
}
bool summon_health(uintptr_t actor, std::array<float, 4>& values, bool write) noexcept {
    __try {
        const auto health = reinterpret_cast<uintptr_t(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(actor) + 0x578))(actor);
        if (!health) return false;
        const unsigned offsets[]{0x38, 0x44, 0xe8, 0xf4};
        for (size_t i = 0; i < values.size(); ++i) {
            auto& value = *reinterpret_cast<float*>(health + offsets[i]);
            if (write) value = values[i];
            else { if (!std::isfinite(value) || value < 0) return false; values[i] = value; }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_memberships(uintptr_t world, uintptr_t actor, std::vector<RecipeMembership>& result) {
    const auto data = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
    const auto count = *reinterpret_cast<const int32_t*>(world + 0x126660);
    if (count < 0 || count > 100000) return false;
    for (int32_t i = 0; i < count; ++i) {
        const auto manager = *reinterpret_cast<const uintptr_t*>(data + static_cast<uintptr_t>(i) * 0x20 + 8);
        char owner[512];
        if (!manager || !read_definition_name(manager, owner)) return false;
        for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
            const auto records = *reinterpret_cast<const uintptr_t*>(manager + offset);
            const auto size = *reinterpret_cast<const int32_t*>(manager + offset + 8);
            if (size < 0 || size > 1000000) return false;
            for (int32_t j = 0; j < size; ++j) {
                const auto record = records + static_cast<uintptr_t>(j) * 0x68;
                if (record_actor(record) != actor) continue;
                std::string label;
                if (!native_text(record + 0x28, label, 1024)) return false;
                RecipeMembership value{owner, std::move(label)};
                if (std::find(result.begin(), result.end(), value) == result.end()) result.push_back(std::move(value));
            }
        }
    }
    return true;
}
bool restore_memberships(uintptr_t world, uintptr_t actor, uint64_t mask, const std::vector<RecipeMembership>& expected, bool& waiting) {
    std::vector<RecipeMembership> present;
    if (!native_memberships(world, actor, present)) return false;
    const auto data = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
    const auto count = *reinterpret_cast<const int32_t*>(world + 0x126660);
    for (const auto& member : expected) {
        if (std::find(present.begin(), present.end(), member) != present.end()) continue;
        uintptr_t manager = 0;
        for (int32_t i = 0; i < count; ++i) {
            const auto candidate = *reinterpret_cast<const uintptr_t*>(data + static_cast<uintptr_t>(i) * 0x20 + 8);
            char name[512];
            if (candidate && read_definition_name(candidate, name) && member.owner == name) { manager = candidate; break; }
        }
        if (!manager) { waiting = true; continue; }
        std::array<uint64_t, 13> record{};
        record[0] = mask; record[1] = uint64_t{0x1fffffe} << 32 | 0x1fffffe;
        *reinterpret_cast<save::NativeString*>(reinterpret_cast<uint8_t*>(record.data()) + 0x28) = recipe_text(member.label);
        std::array<float, 4> health;
        if (!summon_health(actor, health, false)) return false;
        const float current = health[1] + health[3];
        std::memcpy(reinterpret_cast<uint8_t*>(record.data()) + 0x64, &current, sizeof(current));
        register_independent_copy(manager, record.data(), actor);
    }
    return true;
}
bool capture_autonomous_summons(RecipeNativeCheckpoint& checkpoint) {
    if (pending_summon_checkpoint) return false;
    checkpoint.next_request = next_recipe_request;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    std::set<std::string> parents;
    for (const auto* producers : {&component_summons, &direct_points}) for (const auto& [component, pending] : *producers) {
        if (pending.name.empty() || find_entity(world, pending.name.c_str()) != pending.parent) {
            for (const auto& request : pending.requests) recipe_state.cancel(request.key);
            continue;
        }
        char name[512], definition[512];
        if (!read_instance_name(pending.parent, name) || !*name || actor_identifier(pending.parent) != pending.identifier ||
            (!read_recipe_definition(pending.parent, definition) && !read_definition_name(pending.parent, definition))) return false;
        if (find_entity(world, name) != pending.parent || combat::confirmed_dead(world, pending.parent)) {
            for (const auto& request : pending.requests) recipe_state.cancel(request.key);
            continue;
        }
        RecipeNativeCheckpoint::Component saved;
        saved.parent = name; saved.definition = definition; saved.epoch = pending.epoch; saved.completed = pending.completed;
        if (producers == &component_summons) {
        saved.phase = *reinterpret_cast<const uint8_t*>(component + 0x48);
        saved.playback = *reinterpret_cast<const float*>(component + 0x4c);
        saved.mask = *reinterpret_cast<const uint64_t*>(component + 0x68);
        saved.paused = *reinterpret_cast<const uint8_t*>(component + 0x71) != 0;
        saved.template_mode = *reinterpret_cast<const uint8_t*>(component + 0x72) != 0;
        const auto target = record_actor(component + 0x70);
        if (target) {
            if (!read_definition_name(target, name)) return false;
            saved.template_name = name;
        }
        }
        saved.notifications = pending.notifications;
        for (const auto& request : pending.requests) {
            RecipeNativeCheckpoint::Summon record;
            record.key = request.key; record.origin = request.origin; record.body_offset = request.body_offset; record.completed = request.completed;
            for (const auto& source : request.sources) record.sources.push_back(freeze_summon_source(source));
            if (producers == &direct_points) record.memberships = recipe_state.request_memberships(request.key);
            else if (request.sources.empty() || !summon_memberships(pending.parent, request.sources.front().template_name,
                    recipe_state.requests().at(request.key).request.label, record.memberships)) return false;
            saved.requests.push_back(std::move(record));
        }
        parents.insert(saved.parent); checkpoint.summons.push_back(std::move(saved));
    }
    for (const auto& [actor, role] : recipe_actors) {
        if (role.rfind("summon/", 0) != 0 && role.rfind("direct/", 0) != 0) continue;
        const auto found = summon_children.find(role);
        if (found == summon_children.end()) return false;
        auto saved = found->second;
        if (!parents.count(saved.parent)) saved.parent.clear();
        std::memcpy(saved.pose.data(), reinterpret_cast<const void*>(actor + 0x74), sizeof(saved.pose));
        if (!summon_health(actor, saved.health, false)) return false;
        std::vector<RecipeMembership> members;
        if (!native_memberships(world, actor, members)) return false;
        if (members.empty() && !saved.parent.empty() && role.rfind("summon/", 0) == 0) {
            const auto producer = std::find_if(component_summons.begin(), component_summons.end(), [&](const auto& item) {
                char name[512]; return read_instance_name(item.second.parent, name) && saved.parent == name;
            });
            if (producer == component_summons.end()) return false;
            if (*reinterpret_cast<const uint8_t*>(producer->first + 0x48) != 3) {
                char owner[512]; std::string source_owner;
                const auto target = record_actor(producer->first + 0x70);
                if (target) {
                    if (!read_definition_name(target, owner)) return false;
                    source_owner = owner;
                }
                const auto key = role.substr(0, role.rfind('/'));
                if (!summon_memberships(producer->second.parent, source_owner, recipe_state.requests().at(key).request.label, members)) return false;
            }
        }
        const auto key = role.substr(0, role.rfind('/'));
        const auto previous = recipe_state.requests().at(key).actors.at(role).memberships;
        for (const auto& member : members) if (!recipe_state.join_actor(role, member.owner, member.label)) return false;
        for (const auto& member : previous)
            if (std::find(members.begin(), members.end(), member) == members.end()) recipe_state.detach_actor(role, member.owner, member.label);
        checkpoint.children.emplace(role, std::move(saved));
    }
    return true;
}
void invoke_checkpoint_actor(RecipeCreation* creation, const RecipeNativeCheckpoint::Child* saved) {
    std::array<uint8_t, 0x138> info{};
    unsigned strings = 0;
    __try {
        const auto name = *reinterpret_cast<const char**>(creation->body + 8);
        const auto definition = lookup_definition(definition_database(), name, 1);
        if (!definition) { recipe_fault.store(true); return; }
        const auto address = reinterpret_cast<uintptr_t>(info.data());
        construct_native_string(address + 0x10); strings = 1;
        construct_native_string(address + 0x78); strings = 2;
        *reinterpret_cast<uintptr_t*>(info.data()) = definition;
        info[9] = 1;
        std::memcpy(info.data() + 0x40, saved->pose.data(), sizeof(saved->pose));
        *reinterpret_cast<uint32_t*>(info.data() + 0x70) = 12;
        *reinterpret_cast<uintptr_t*>(info.data() + 0x78) = animation_path_table;
        *reinterpret_cast<const char**>(info.data() + 0xa8) = "";
        *reinterpret_cast<const char**>(info.data() + 0xb0) = "";
        for (const auto offset : {0xb8, 0xd8, 0x118})
            *reinterpret_cast<uint64_t*>(info.data() + offset) = uint64_t{0x1fffffe} << 32 | 0x1fffffe;
        for (const auto offset : {0xf8, 0x100, 0x108}) *reinterpret_cast<int32_t*>(info.data() + offset) = -1;
        *reinterpret_cast<uint32_t*>(info.data() + 0xfc) = 0x1fffffe;
        info[0x105] = 1;
        creation->creating = true;
        creation->actor = original_actor_factory(address);
        if (creation->actor) initialize_physical_recipe(creation, creation->actor);
    } __finally {
        __try {
            if (strings > 1) destroy_native_text(reinterpret_cast<uintptr_t>(info.data()) + 0x78);
            if (strings) destroy_native_text(reinterpret_cast<uintptr_t>(info.data()) + 0x10);
        } __finally { creation->creating = false; end_recipe_creation(*creation); }
    }
}
bool restore_checkpoint_actor(uintptr_t world, const std::string& role, RecipeNativeCheckpoint::Child& saved, uintptr_t& actor) {
    const auto key = role.substr(0, role.rfind('/'));
    const auto& progress = recipe_state.requests().at(key);
    const auto& frozen = progress.actors.at(role);
    actor = find_entity(world, frozen.native_name.c_str());
    if (actor) {
        char definition[512];
        if (!read_recipe_definition(actor, definition) || saved.body != definition) return false;
        const auto units = progress.request.credit_limit && frozen.ordinal + 1 == progress.request.credit_limit ?
            progress.request.final_credit_units : progress.request.logical_units;
        recipe_actors[actor] = role;
        recipe_actor_credits[actor] = {actor_identifier(actor), {units, progress.request.physical_units}, key, frozen.native_name, frozen.ordinal};
        return true;
    }
    const auto& recipe = native_recipes.at(progress.request.origin);
    const auto body = std::find_if(recipe.bodies.begin(), recipe.bodies.end(), [&](uintptr_t entry) {
        std::string name; return native_text(entry, name, 511) && name == saved.body;
    });
    if (body == recipe.bodies.end()) return false;
    auto limits = spawn_limits(saved.body);
    if (!limits) return false;
    if (!saved.parent.empty()) inherit_population(*limits, find_entity(world, saved.parent.c_str()));
    if (!generated_admission(*limits)) {
        trace_spawn_refusal(0, key, admission_diagnostic_reason, "deferred", progress.request.owner.c_str()); return true;
    }
    RecipeCreation creation;
    creation.restoring = true; creation.recipe = &recipe; creation.key = key; creation.emission = {role, frozen.ordinal};
    creation.body = *body; creation.limits = *limits; creation.reserved = true;
    reserve_admission(*limits); creation.previous = recipe_creation; recipe_creation = &creation;
    invoke_checkpoint_actor(&creation, &saved); actor = creation.actor;
    if (recipe_fault.load()) return false;
    return !actor || summon_health(actor, saved.health, true);
}
bool adopt_summon_child(uintptr_t component, uintptr_t actor) {
    auto count = *reinterpret_cast<const int32_t*>(component + 0x20);
    const auto capacity = *reinterpret_cast<const int32_t*>(component + 0x24);
    if (count < 0 || capacity < count || count >= 1000000) return false;
    auto data = *reinterpret_cast<const uintptr_t*>(component + 0x18);
    for (int32_t i = 0; i < count; ++i)
        if (record_actor(data + static_cast<uintptr_t>(i) * 0x68) == actor) return true;
    if (count == capacity && !grow_summon_records(component + 0x18, static_cast<uint32_t>(count + 1))) return false;
    data = *reinterpret_cast<const uintptr_t*>(component + 0x18);
    const auto record = data + static_cast<uintptr_t>(count) * 0x68;
    assign_manager(record + 8, actor); assign_manager(record + 0x28, 0);
    *reinterpret_cast<uint64_t*>(record) = 0;
    std::memset(reinterpret_cast<void*>(record + 0x48), 0, 0x20);
    *reinterpret_cast<float*>(record + 0x54) = 1;
    *reinterpret_cast<int32_t*>(component + 0x20) = count + 1;
    return true;
}
bool restore_autonomous_summons(uintptr_t world) {
    if (!pending_summon_checkpoint) return true;
    auto& checkpoint = *pending_summon_checkpoint;
    for (auto child = checkpoint.children.begin(); child != checkpoint.children.end();) {
        if (!child->second.parent.empty()) { ++child; continue; }
        uintptr_t actor = 0; bool waiting = false;
        if (!restore_checkpoint_actor(world, child->first, child->second, actor)) return false;
        if (!actor) { ++child; continue; }
        const auto& progress = recipe_state.requests().at(child->first.substr(0, child->first.rfind('/')));
        if (!restore_memberships(world, actor, progress.request.logical_mask, progress.actors.at(child->first).memberships, waiting)) return false;
        if (waiting) { ++child; continue; }
        summon_children[child->first] = child->second; child = checkpoint.children.erase(child);
    }
    for (auto saved = checkpoint.summons.begin(); saved != checkpoint.summons.end();) {
        const auto parent = find_entity(world, saved->parent.c_str());
        if (!parent) { ++saved; continue; }
        char definition[512];
        if ((!read_recipe_definition(parent, definition) && !read_definition_name(parent, definition)) || saved->definition != definition) return false;
        const bool direct = saved->definition.rfind("ap_v070_direct_r", 0) == 0;
        const auto component = direct ? parent : *reinterpret_cast<const uintptr_t*>(parent + 0x165e0);
        if (!component || (!direct && *reinterpret_cast<const uintptr_t*>(component + 8) != parent)) return false;
        auto& pending = direct ? direct_points[parent] : component_summons[component];
        if (!pending.parent) {
            if (!direct) {
            const auto target = saved->template_name.empty() ? 0 : find_entity(world, saved->template_name.c_str());
            if (!saved->template_name.empty() && !target) { ++saved; continue; }
            assign_manager(component + 0x78, target);
            *reinterpret_cast<uint8_t*>(component + 0x48) = saved->phase;
            *reinterpret_cast<float*>(component + 0x4c) = saved->playback;
            *reinterpret_cast<uint64_t*>(component + 0x68) = saved->mask;
            *reinterpret_cast<uint8_t*>(component + 0x71) = saved->paused;
            *reinterpret_cast<uint8_t*>(component + 0x72) = saved->template_mode;
            }
            pending.parent = parent; pending.identifier = actor_identifier(parent); pending.name = saved->parent;
            pending.epoch = saved->epoch; pending.completed = saved->completed;
            pending.notifications = saved->notifications;
            for (const auto& request : saved->requests) {
                SummonRequest item{request.key, request.origin, {}, request.body_offset, request.completed};
                for (const auto& source : request.sources) item.sources.push_back(thaw_summon_source(source));
                pending.requests.push_back(std::move(item));
            }
            if (direct) {
                auto& credits = point_credits[parent];
                credits.observed = *reinterpret_cast<const int32_t*>(parent + 0xd90);
                if (credits.observed < 0) return false;
                const auto origin = saved->definition.substr(std::string("ap_v070_direct_r").size());
                for (const auto& [key, progress] : recipe_state.requests()) {
                    if (key.rfind("direct/", 0) != 0 || progress.request.origin != origin || !progress.request.physical_units) continue;
                    const auto& request = progress.request;
                    auto units = progress.emitted * request.logical_units;
                    if (request.credit_limit && progress.emitted == request.credit_limit) units -= request.logical_units - request.final_credit_units;
                    auto& value = credits.units[request.physical_units];
                    if (value > UINT64_MAX - units) return false;
                    value += units;
                }
            }
        }
        if (pending.parent != parent || pending.identifier != actor_identifier(parent)) return false;
        bool waiting = false;
        for (auto child = checkpoint.children.begin(); child != checkpoint.children.end();) {
            if (child->second.parent != saved->parent) { ++child; continue; }
            const auto& role = child->first; auto& record = child->second;
            const auto key = role.substr(0, role.rfind('/'));
            const auto& progress = recipe_state.requests().at(key);
            const auto& frozen = progress.actors.at(role);
            uintptr_t actor = 0;
            if (!restore_checkpoint_actor(world, role, record, actor)) return false;
            if (!actor) { waiting = true; ++child; continue; }
            if (!direct && !adopt_summon_child(component, actor)) return false;
            bool bindings_waiting = false;
            if (!restore_memberships(world, actor, progress.request.logical_mask, frozen.memberships, bindings_waiting)) return false;
            if (bindings_waiting) { waiting = true; ++child; continue; }
            summon_children[role] = record;
            child = checkpoint.children.erase(child);
        }
        if (waiting) { ++saved; continue; }
        if (!direct) {
            original_summon_cleanup(component);
            *reinterpret_cast<uint8_t*>(component + 0x48) = 3;
            pending.completed = true;
            for (auto& request : pending.requests) request.completed = true;
        }
        saved = checkpoint.summons.erase(saved);
    }
    if (checkpoint.summons.empty() && checkpoint.children.empty()) pending_summon_checkpoint.reset();
    return true;
}
bool drain_component_summons(uintptr_t component) {
    if (pending_summon_checkpoint) return true;
    const auto found = component_summons.find(component);
    if (found == component_summons.end()) return true;
    auto& pending = found->second;
    const auto parent = *reinterpret_cast<const uintptr_t*>(component + 8);
    const auto identifier = actor_identifier(parent);
    if (pending.parent != parent || pending.identifier != identifier ||
        combat::confirmed_dead(*reinterpret_cast<const uintptr_t*>(world_global), parent)) {
        for (const auto& request : pending.requests) recipe_state.cancel(request.key);
        complete_summon_requests(pending);
        component_summons.erase(found); return true;
    }
    bool created = false;
    for (auto request = pending.requests.begin(); request != pending.requests.end();) {
        const auto& progress = recipe_state.requests().at(request->key);
        while (!progress.closed && progress.emitted < progress.request.maximum_emissions) {
            RecipeCreation creation;
            const auto previous = worker_recipe; worker_recipe = &request->key;
            const auto previous_offset = worker_body_offset; worker_body_offset = request->body_offset;
            bool ready = false;
            try { ready = begin_recipe_creation(creation, parent, request->origin, progress.request.label); }
            catch (...) { worker_recipe = previous; worker_body_offset = previous_offset; throw; }
            worker_recipe = previous; worker_body_offset = previous_offset;
            if (!ready) break;
            const auto source_index = static_cast<size_t>(creation.emission.ordinal * request->sources.size() /
                progress.request.maximum_emissions);
            invoke_summon_creation(&creation, component, &request->sources[source_index]);
            if (recipe_fault.load(std::memory_order_acquire)) return false;
            if (!creation.actor) break;
            char parent_name[512];
            if (!read_instance_name(parent, parent_name) || !*parent_name) return false;
            auto& child = summon_children[creation.emission.actor];
            child.parent = parent_name;
            child.body = *reinterpret_cast<const char**>(creation.body + 8);
            created = true;
        }
        if (!progress.closed && progress.emitted < progress.request.maximum_emissions) { ++request; continue; }
        recipe_state.cancel(request->key);
        request = pending.requests.erase(request);
    }
    if (created && pending.completed && *reinterpret_cast<const uint8_t*>(component + 0x48) != 1)
        original_summon_complete(component);
    complete_summon_requests(pending);
    return true;
}
void invoke_summon_start(uintptr_t component, SummonCollection* collection) {
    const auto previous = summon_collection;
    __try { summon_collection = collection; original_summon_start(component); }
    __finally { summon_collection = previous; }
}
void summon_start_detour(uintptr_t component) {
    if (stopped.load(std::memory_order_acquire)) { original_summon_start(component); return; }
    std::lock_guard<std::recursive_mutex> lock(admission_mutex);
    uintptr_t root = 0;
    if (recipe_checkpoint_root(root) && !root) { original_summon_start(component); return; }
    try {
        if ((restored_independent && !load_native_recipes()) ||
            !restore_autonomous_summons(*reinterpret_cast<const uintptr_t*>(world_global))) { recipe_fault.store(true); return; }
    } catch (...) { recipe_fault.store(true); return; }
    SummonCollection collection{component, {}};
    invoke_summon_start(component, &collection);
    try {
        if (!recipe_fault.load(std::memory_order_acquire) && open_component_summons(collection) && drain_component_summons(component)) return;
    } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    recipe_fault.store(true, std::memory_order_release);
}
void summon_complete_detour(uintptr_t component) {
    if (stopped.load(std::memory_order_acquire)) { original_summon_complete(component); return; }
    std::lock_guard<std::recursive_mutex> lock(admission_mutex);
    original_summon_complete(component);
    const auto found = component_summons.find(component);
    if (found != component_summons.end()) {
        found->second.completed = true;
        for (auto& request : found->second.requests) request.completed = true;
    }
}
void summon_update_detour(uintptr_t component, uintptr_t frame) {
    if (!stopped.load(std::memory_order_acquire)) {
        std::lock_guard<std::recursive_mutex> lock(admission_mutex);
        try {
            if ((restored_independent && !load_native_recipes()) ||
                !restore_autonomous_summons(*reinterpret_cast<const uintptr_t*>(world_global))) { recipe_fault.store(true); return; }
        } catch (...) { recipe_fault.store(true); return; }
    }
    original_summon_update(component, frame);
    if (stopped.load(std::memory_order_acquire)) return;
    std::lock_guard<std::recursive_mutex> lock(admission_mutex);
    try { if (drain_component_summons(component)) return; } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    recipe_fault.store(true, std::memory_order_release);
}
uint64_t invoke_suspended_recipe(RecipeCreation* scope, uintptr_t manager, uintptr_t record,
                                 uintptr_t point, uintptr_t fourth, uintptr_t fifth, unsigned kind) {
    uint64_t result = 0;
    const auto previous_worker = worker_recipe;
    __try {
        suspend_recipe_points(scope); recipe_creation = nullptr; worker_recipe = nullptr;
        if (!kind) result = point_factory_detour(point, fourth);
        else if (kind == 1) result = spawn_detour(manager, record, point, fourth, fifth);
        else result = group_detour(manager, reinterpret_cast<uint64_t*>(record), point);
    } __finally { recipe_creation = scope->previous; worker_recipe = previous_worker; resume_recipe_points(scope); }
    return result;
}
uint64_t without_recipe_override(uintptr_t manager, uintptr_t record, uintptr_t point,
                                  uintptr_t fourth, uintptr_t fifth, unsigned kind) {
    RecipeCreation scope; scope.previous = recipe_creation;
    return invoke_suspended_recipe(&scope, manager, record, point, fourth, fifth, kind);
}
uint64_t create_from_recipe(uintptr_t manager, uintptr_t record, uintptr_t fourth,
                            uintptr_t fifth, bool group, std::string_view origin) {
    RecipeCreation creation;
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    bool ready = false;
    try {
        std::string label;
        ready = native_text(record + 0x28, label, 1024) && begin_recipe_creation(creation, manager, origin, label);
    } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    if (!ready) { admission.unlock(); return 0; }
    return invoke_recipe_creation(&creation, manager, record, fourth, fifth, group, admission);
}
bool try_recipe_creation(uintptr_t manager, uintptr_t record, uintptr_t alias, uintptr_t fourth,
                          uintptr_t fifth, bool group, uint64_t& result) {
    std::string origin;
    if (!recipe_origin(alias, origin)) return false;
    result = create_from_recipe(manager, record, fourth, fifth, group, origin);
    return true;
}
bool read_worker_diagnostic(uintptr_t worker, WorkerDiagnostic& value) {
    engine::LocalMemory memory;
    uintptr_t manager = 0;
    char owner[512];
    if (memory.copy(worker + 0x40, &manager, sizeof(manager)).reason ||
        memory.copy(worker + 0x88, &value.mask, sizeof(value.mask)).reason || !read_definition_name(manager, owner) ||
        !native_text(worker + 0x58, value.label, 1024)) return false;
    value.manager = owner;
    return true;
}
void remember_worker_diagnostic(uintptr_t worker) noexcept {
    try {
        if (worker_diagnostics.size() >= 4096 && !worker_diagnostics.count(worker)) return;
        WorkerDiagnostic value;
        if (read_worker_diagnostic(worker, value)) worker_diagnostics[worker] = std::move(value);
    } catch (...) {}
}
void trace_recipe_worker(const char* state, uintptr_t worker, const WorkerDiagnostic& value, const std::string& key) {
    if (worker_trace_count >= 4096) return;
    ++worker_trace_count;
    const auto progress = recipe_state.requests().find(key);
    commands::enemy_worker_trace(state, value.manager.c_str(), worker, value.mask, value.label.c_str(), key.c_str(),
        progress == recipe_state.requests().end() ? 0 : progress->second.emitted,
        progress != recipe_state.requests().end() && progress->second.closed);
}
bool prepare_recipe_worker(uintptr_t worker, bool maintenance, bool& marked) {
    const auto alias = *reinterpret_cast<const uintptr_t*>(worker + (maintenance ? 0xd8 : 0xc8));
    std::string origin;
    if (!recipe_origin(alias, origin)) return true;
    marked = true;
    if (!load_native_recipes()) { trace_spawn_refusal(*reinterpret_cast<const uintptr_t*>(worker + 0x40), origin, "worker_recipe_library_unavailable"); return false; }
    const auto recipe = native_recipes.find(origin);
    if (recipe == native_recipes.end()) { trace_spawn_refusal(*reinterpret_cast<const uintptr_t*>(worker + 0x40), origin, "worker_recipe_missing"); return false; }
    if (vanilla_bridge_group(recipe->second)) {
        if (!*reinterpret_cast<const uint8_t*>(worker + (maintenance ? 0xcc : 0xb9)) &&
            *reinterpret_cast<const int32_t*>(worker + (maintenance ? 0x90 : 0xa8)) > 0) {
            std::string label;
            if (!native_text(worker + 0x58, label, 1024)) return false;
            *reinterpret_cast<int32_t*>(worker + (maintenance ? 0x90 : 0xa8)) = 0;
            *reinterpret_cast<uint8_t*>(worker + (maintenance ? 0xcc : 0xb9)) = 1;
            alignas(8) uint8_t output[32]{};
            original_multiple(*reinterpret_cast<const uintptr_t*>(worker + 0x40), reinterpret_cast<uintptr_t>(output),
                recipe->second.definition.logical_mask, 1, 0, reinterpret_cast<uintptr_t>(label.c_str()));
        }
        const auto found = worker_recipes.find(worker);
        if (found != worker_recipes.end()) {
            recipe_state.cancel(found->second);
            worker_recipes.erase(found);
        }
        *reinterpret_cast<int32_t*>(worker + (maintenance ? 0x90 : 0xa8)) = 0;
        *reinterpret_cast<uint8_t*>(worker + (maintenance ? 0xcc : 0xb9)) = 1;
        return false;
    }
    auto found = worker_recipes.find(worker);
    if (found == worker_recipes.end()) {
        const auto manager = *reinterpret_cast<const uintptr_t*>(worker + 0x40);
        std::string label, key;
        if (!manager || !native_text(worker + 0x58, label, 191) ||
            !open_native_request(manager, recipe->second, label, key)) return false;
        const auto& entry = recipe->second.definition;
        const auto quantity = static_cast<int32_t>(entry.physical_count);
        *reinterpret_cast<int32_t*>(worker + 0x90) = quantity;
        if (maintenance) {
            *reinterpret_cast<int32_t*>(worker + 0x94) = static_cast<int32_t>(
                recipe->second.refill_limit ? recipe->second.refill_limit : entry.maximum_emissions);
            if (!quantity) *reinterpret_cast<uint8_t*>(worker + 0xcc) = 1;
        } else {
            *reinterpret_cast<int32_t*>(worker + 0xa8) = quantity;
            if (!quantity) *reinterpret_cast<uint8_t*>(worker + 0xb9) = 1;
        }
        found = worker_recipes.emplace(worker, std::move(key)).first;
        remember_worker_diagnostic(worker);
        if (!quantity) recipe_state.complete_empty(found->second);
    }
    if (recipe_state.parent_pending(recipe_state.requests().at(found->second).request.parent_actor)) {
        trace_spawn_refusal(*reinterpret_cast<const uintptr_t*>(worker + 0x40), origin, "parent_pending"); return false;
    }
    worker_recipe = &found->second;
    return true;
}
void invoke_recipe_worker(uintptr_t worker, bool maintenance, const std::string* previous, bool marked, std::unique_lock<std::recursive_mutex>& admission) {
    __try {
        (maintenance ? original_maintain_tick : original_stagger_tick)(worker);
    } __finally {
        if (marked && *reinterpret_cast<const uint8_t*>(worker + (maintenance ? 0xcc : 0xb9))) {
            const auto found = worker_recipes.find(worker);
            if (found != worker_recipes.end()) recipe_state.cancel(found->second);
        }
        worker_recipe = previous;
        admission.unlock();
    }
}
void recipe_worker_tick(uintptr_t worker, bool maintenance) {
    if (stopped.load(std::memory_order_acquire)) { (maintenance ? original_maintain_tick : original_stagger_tick)(worker); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    const auto previous = worker_recipe;
    worker_recipe = nullptr;
    bool marked = false, ready = false;
    try { ready = prepare_recipe_worker(worker, maintenance, marked); } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    if (!ready) { worker_recipe = previous; admission.unlock(); return; }
    invoke_recipe_worker(worker, maintenance, previous, marked, admission);
}
void maintain_tick_detour(uintptr_t worker) { recipe_worker_tick(worker, true); }
void stagger_tick_detour(uintptr_t worker) { recipe_worker_tick(worker, false); }
void prepare_queued_recipe(uintptr_t command) {
    const auto previous = worker_recipe;
    bool marked = false;
    try {
        if (const auto worker = cast_maintain ? cast_maintain(command) : 0) prepare_recipe_worker(worker, true, marked);
        else if (const auto staggered = cast_stagger ? cast_stagger(command) : 0) prepare_recipe_worker(staggered, false, marked);
    } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    worker_recipe = previous;
}
void queue_command_detour(uintptr_t manager, uintptr_t command) {
    if (stopped.load(std::memory_order_acquire)) { original_queue_command(manager, command); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    [&]() -> void {
        __try {
            const auto found = worker_recipes.find(command);
            if (found != worker_recipes.end()) {
                recipe_state.cancel(found->second);
                worker_recipes.erase(found);
                worker_diagnostics.erase(command);
            }
            original_queue_command(manager, command);
            trace_spawn_refusal(manager, "", "command_queued", "observed");
            prepare_queued_recipe(command);
            if (kill_progress.find(command) != kill_progress.end()) initialize_kill(command, manager);
        } __finally { admission.unlock(); }
    }();
}
bool queue_recipe_remainder(uintptr_t manager, uintptr_t alias, const char* label, const std::string& key) {
    const auto found = recipe_state.requests().find(key);
    if (found == recipe_state.requests().end()) return false;
    const auto& progress = found->second;
    if (progress.closed) return true;
    const auto count = progress.request.physical_units - progress.emitted;
    if (!count) return true;
    const auto active = *reinterpret_cast<const uintptr_t*>(manager + 0xcc0);
    const auto active_count = *reinterpret_cast<const int32_t*>(manager + 0xcc8);
    if (active_count < 0 || active_count > 100000) return false;
    for (int32_t i = 0; i < active_count; ++i) {
        const auto found_worker = worker_recipes.find(*reinterpret_cast<const uintptr_t*>(active + static_cast<uintptr_t>(i) * 8));
        if (found_worker != worker_recipes.end() && found_worker->second == key) return true;
    }
    const auto memory = allocate_worker(0x110, 0x54);
    if (!memory) return false;
    const auto command = construct_stagger(memory, static_cast<int32_t>(count), 0, 0, alias,
        progress.request.logical_mask, label);
    assign_manager(command + 0x38, manager);
    // emission work belongs to the native queue, outside a multi-condition wait's children.
    const auto wait_depth = *reinterpret_cast<const int32_t*>(manager + 0xd90);
    __try {
        *reinterpret_cast<int32_t*>(manager + 0xd90) = 0;
        original_queue_command(manager, command);
    } __finally { *reinterpret_cast<int32_t*>(manager + 0xd90) = wait_depth; }
    const auto commands = *reinterpret_cast<const uintptr_t*>(manager + 0xcc0);
    const auto length = *reinterpret_cast<const int32_t*>(manager + 0xcc8);
    for (int32_t i = 0; i < length; ++i) {
        if (*reinterpret_cast<const uintptr_t*>(commands + static_cast<uintptr_t>(i) * 8) != command) continue;
        worker_recipes[command] = key;
        remember_worker_diagnostic(command);
        return true;
    }
    reinterpret_cast<void(*)(uintptr_t, uint32_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(command)))(command, 1);
    return false;
}
bool queue_recipe_delay(uintptr_t manager) {
    const auto memory = allocate_worker(0xb8, 0x54);
    if (!memory) return false;
    const auto clock = *reinterpret_cast<const uintptr_t*>(world_global) + 0x125a60;
    uint64_t first = 0, second = 0, duration = 0;
    native_clock(clock, &first, 1);
    const auto step = native_step(clock, 1);
    native_clock(clock, &second, 1);
    native_duration(&duration, step * 2.0f);
    const auto command = construct_time(memory, second, duration, first);
    assign_manager(command + 0x38, manager); original_queue_command(manager, command);
    return true;
}
bool dispatch_recipe_locked(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                              uintptr_t label, bool group, const std::string& origin, uintptr_t& result) {
    result = output;
    if (!load_native_recipes()) { if (!recipe_fault.load()) retry_immediate(manager, "recipe_library_unavailable"); return true; }
    const auto found = native_recipes.find(origin);
    if (found == native_recipes.end()) { retry_immediate(manager, "recipe_missing"); return true; }
    // preserve native group selection for the bridge's empty source.
    if (vanilla_bridge_group(found->second)) {
        result = original_multiple(manager, output, type, 1, 0, label);
        return true;
    }
    auto& key = pending_recipes[{manager, origin}];
    if (key.empty()) {
        std::string label_text;
        if ((found->second.definition.family != 's' && !native_cstring(label, label_text, 1024)) ||
            !open_native_request(manager, found->second, label_text, key)) {
            retry_immediate(manager, "request_not_opened"); return true;
        }
    }
    if (!found->second.definition.physical_count) {
        if ((found->second.definition.family == 'i' || found->second.definition.family == 'p') && !queue_recipe_delay(manager)) {
            retry_immediate(manager, "delay_worker_unavailable"); return true;
        }
        recipe_state.complete_empty(key); pending_recipes.erase({manager, origin}); return true;
    }
    if (!recipe_state.requests().at(key).emitted) {
        // a native refusal rewinds this event; queuing it too would repeat the wave.
        if (!native_admission(*reinterpret_cast<const uintptr_t*>(world_global) + 0xafd98, 1)) {
            trace_spawn_refusal(manager, origin, "native_admission_refused");
            retry_immediate(manager, "native_admission_refused"); return true;
        }
        const auto family = found->second.definition.family;
        if (family == 'a' || family == 'b' || family == 's')
            create_special_recipe(manager, origin, recipe_state.requests().at(key).request.label);
        else result = group ? original_multiple(manager, output, type, 1, alias, label) :
            original_single(manager, output, type, alias, label);
    }
    const auto& birth_label = recipe_state.requests().at(key).request.label;
    if (!queue_recipe_remainder(manager, alias, birth_label.c_str(), key)) {
        retry_immediate(manager, "remainder_worker_unavailable"); return true;
    }
    pending_recipes.erase({manager, origin});
    return true;
}
bool invoke_recipe_dispatch(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                             uintptr_t label, bool group, const std::string* origin, uintptr_t* result, std::unique_lock<std::recursive_mutex>& admission) {
    bool handled = false;
    const auto previous_worker = worker_recipe;
    __try { worker_recipe = nullptr; handled = dispatch_recipe_locked(manager, output, type, alias, label, group, *origin, *result); }
    __finally { worker_recipe = previous_worker; admission.unlock(); }
    return handled;
}
bool dispatch_recipe_immediate(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                               uintptr_t label, bool group, uintptr_t& result) {
    std::string origin;
    if (stopped.load(std::memory_order_acquire) || !recipe_origin(alias, origin)) return false;
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return invoke_recipe_dispatch(manager, output, type, alias, label, group, &origin, &result, admission);
}
uintptr_t possessed_recipe_locked(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                                   uintptr_t spirit, const std::string& host_origin, const std::string& spirit_origin) {
    if (!load_native_recipes()) { if (!recipe_fault.load()) retry_immediate(manager); return output; }
    const auto host_recipe = native_recipes.find(host_origin), spirit_recipe = native_recipes.find(spirit_origin);
    if (host_recipe == native_recipes.end() || spirit_recipe == native_recipes.end() ||
        host_recipe->second.definition.family != 'p' || spirit_recipe->second.definition.family != 's') {
        recipe_fault.store(true); retry_immediate(manager); return output;
    }
    auto& child = pending_recipes[{manager, spirit_origin}];
    if (child.empty()) {
        auto& parent = pending_recipes[{manager, host_origin}];
        std::string label_text;
        if (!native_cstring(label, label_text, 1024) || (parent.empty() && !open_native_request(manager, host_recipe->second, label_text, parent)) ||
            !open_native_request(manager, spirit_recipe->second, "", child,
                host_recipe->second.definition.physical_count ? parent + "/0" : "", parent)) {
            retry_immediate(manager); return output;
        }
    }
    const auto parent = recipe_state.requests().at(child).request.parent_request;
    if (pending_recipes.find({manager, host_origin}) != pending_recipes.end()) {
        uintptr_t result = output;
        dispatch_recipe_locked(manager, output, type, host, label, false, host_origin, result);
        if (pending_recipes.find({manager, host_origin}) != pending_recipes.end()) return output;
    }
    if (!recipe_state.requests().count(parent)) { recipe_fault.store(true); retry_immediate(manager); return output; }
    const auto& progress = recipe_state.requests().at(child);
    if (!progress.request.physical_units) recipe_state.complete_empty(child);
    else if (!progress.emitted) create_special_recipe(manager, spirit_origin, "");
    if (!queue_recipe_remainder(manager, spirit, "", child)) {
        retry_immediate(manager); return output;
    }
    pending_recipes.erase({manager, spirit_origin});
    return output;
}
uintptr_t invoke_possessed_recipe(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                                   uintptr_t spirit, const std::string* host_origin, const std::string* spirit_origin, std::unique_lock<std::recursive_mutex>& admission) {
    uintptr_t result = output;
    const auto previous_worker = worker_recipe;
    __try {
        worker_recipe = nullptr;
        result = possessed_recipe_locked(manager, output, type, host, label, spirit, *host_origin, *spirit_origin);
    } __finally { worker_recipe = previous_worker; admission.unlock(); }
    return result;
}
uintptr_t possessed_detour(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                            uintptr_t spirit, uint64_t hosts, uintptr_t filter, uint8_t flag) {
    if (stopped.load(std::memory_order_acquire)) return original_possessed(manager, output, type, host, label, spirit, hosts, filter, flag);
    std::string host_origin, spirit_origin;
    const auto marked_host = recipe_origin(host, host_origin), marked_spirit = recipe_origin(spirit, spirit_origin);
    if (stopped.load(std::memory_order_acquire) || (!marked_host && !marked_spirit))
        return original_possessed(manager, output, type, host, label, spirit, hosts, filter, flag);
    if (!marked_host || !marked_spirit) { recipe_fault.store(true); retry_immediate(manager); return output; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return invoke_possessed_recipe(manager, output, type, host, label, spirit, &host_origin, &spirit_origin, admission);
}
uintptr_t record_actor(uintptr_t record) {
    const auto identifier = *reinterpret_cast<const uint32_t*>(record + 8);
    const auto cached = *reinterpret_cast<const uint32_t*>(record + 12);
    if (identifier != cached || (identifier != 0x1fffffe &&
        !*reinterpret_cast<const uintptr_t*>(record + 16)))
        refresh_entity(record + 8, identifier == cached ? record + 8 : record + 12);
    return *reinterpret_cast<const uintptr_t*>(record + 16);
}
uintptr_t resurrection_source(uintptr_t component, int32_t index) noexcept {
    __try {
        if (index < 0 || index >= *reinterpret_cast<const int32_t*>(component + 0xb90)) return 0;
        const auto data = *reinterpret_cast<const uintptr_t*>(component + 0xb88);
        if (!data) return 0;
        return record_actor(data + static_cast<uintptr_t>(index) * 0x38 + 8);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t resurrect_locked(uintptr_t component, int32_t index, const float* position, const float* axis) {
    const auto source = resurrection_source(component, index);
    std::optional<ActorCredit> credit;
    std::string body;
    if (source && current_recipe_actor(source)) {
        const auto found = recipe_actor_credits.find(source); char name[512];
        if (found != recipe_actor_credits.end() && read_recipe_definition(source, name)) {
            credit = found->second; body = name;
        }
    }
    const auto actor = original_resurrect_factory(component, index, position, axis);
    if (actor && credit) {
        char name[512], instance[512];
        if (!read_recipe_definition(actor, name) || name != body || !read_instance_name(actor, instance)) {
            recipe_fault.store(true); return actor;
        }
        credit->identifier = actor_identifier(actor); credit->name = instance;
        recipe_actor_credits[actor] = std::move(*credit);
    }
    return actor;
}
uintptr_t resurrect_detour(uintptr_t component, int32_t index, const float* position, const float* axis) {
    if (stopped.load()) return original_resurrect_factory(component, index, position, axis);
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return [&]() -> uintptr_t {
        __try { return resurrect_locked(component, index, position, axis); }
        __finally { admission.unlock(); }
    }();
}
bool completed_independent_actor(uintptr_t actor, const SpawnRecipe& frozen, std::string* request = nullptr) {
    const auto origin = std::string(frozen.origin.data(), frozen.origin.size());
    const auto recipe = native_recipes.find(origin);
    if (recipe == native_recipes.end() || recipe->second.definition.family != 'd') return false;
    char name[512];
    if (!read_instance_name(actor, name) || independent_body_name(recipe->second, frozen.body) != name) return false;
    const auto key = "independent/" + origin;
    const auto progress = recipe_state.requests().find(key);
    if (progress == recipe_state.requests().end() || frozen.body >= progress->second.emitted ||
        progress->second.actors.count(key + "/" + std::to_string(frozen.body))) return false;
    if (request) *request = key;
    return true;
}
RemainingWeight remaining_weight(uintptr_t actor) {
    char name[512];
    if (actor && read_recipe_definition(actor, name, true)) return {{0, 1}, true};
    if (!actor || !read_recipe_definition(actor, name)) return {{1, 1}, false};
    const auto recipe = *spawn_recipe(name);
    if (!current_recipe_actor(actor) && completed_independent_actor(actor, recipe)) return {{0, recipe.physical_units}, true};
    const RecipeProgress* progress = nullptr;
    uint64_t ordinal = 0;
    if (recipe_creation && recipe_creation->creating && recipe_creation->recipe->definition.origin ==
        std::string_view(recipe.origin.data(), recipe.origin.size())) {
        const auto found = recipe_state.requests().find(recipe_creation->key);
        if (found != recipe_state.requests().end()) { progress = &found->second; ordinal = recipe_creation->emission.ordinal; }
    }
    const auto role = recipe_actors.find(actor);
    const auto cached = recipe_actor_credits.find(actor);
    if (!progress && cached != recipe_actor_credits.end()) {
        const auto identifier = actor_identifier(actor);
        if (identifier == cached->second.identifier) return {cached->second.credit, true};
    }
    if (!progress && role != recipe_actors.end()) {
        const auto separator = role->second.rfind('/');
        const auto found = recipe_state.requests().find(role->second.substr(0, separator));
        if (found != recipe_state.requests().end()) {
            const auto member = found->second.actors.find(role->second);
            if (member != found->second.actors.end()) { progress = &found->second; ordinal = member->second.ordinal; }
        }
    }
    if (progress) {
        const auto& request = progress->request;
        const auto units = request.credit_limit && ordinal + 1 == request.credit_limit ? request.final_credit_units : request.logical_units;
        return {{units, request.physical_units}, true};
    }
    return {{recipe.credit_units, recipe.physical_units}, true};
}
uintptr_t post_death_target(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument) {
    if (!death_action_context || event != activate_event || stopped.load(std::memory_order_acquire)) {
        return original_post_one(target, event, delay, argument);
    }
    try {
        const auto& context = *death_action_context;
        const auto identifier = actor_identifier(target);
        char name[512];
        const std::string identity = read_instance_name(target, name) && *name ? name : "#" + std::to_string(identifier);
        if (context.request.rfind("independent/", 0) == 0 && identity.front() == '#') {
            recipe_fault.store(true); return 0;
        }
        auto& credit = death_action_credits[{context.request, identity}];
        if ((credit.denominator && credit.denominator != context.credit.denominator) ||
            !context.credit.denominator || credit.units > UINT64_MAX - context.credit.numerator) {
            recipe_fault.store(true, std::memory_order_release); return 0;
        }
        credit.denominator = context.credit.denominator; credit.units += context.credit.numerator;
        const auto due = credit.units / credit.denominator;
        uintptr_t result = 0;
        while (credit.sent < due) {
            commands::diagnostic_trace("AP_TRIGGER_POST target=%.192s activator_kind=AI request=%.192s source=actor_death_target delay=%d activation=unverified",
                identity.c_str(), context.request.c_str(), delay);
            result = original_post_one(target, event, delay, argument);
            ++credit.sent;
        }
        return result;
    } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    return 0;
}
uintptr_t post_one_detour(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument) {
    if (stopped.load(std::memory_order_acquire)) return original_post_one(target, event, delay, argument);
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    // only the actor's own death targets use logical credits; nested encounter events fire normally.
    if (std::find(death_target_returns.begin(), death_target_returns.end(), caller) == death_target_returns.end())
        return original_post_one(target, event, delay, argument);
    return post_death_target(target, event, delay, argument);
}
void invoke_death_actions(uintptr_t action, uintptr_t context, uintptr_t state, uintptr_t frame,
                          const DeathActionContext* projection, std::unique_lock<std::recursive_mutex>& admission) {
    const auto previous = death_action_context;
    __try { death_action_context = projection; original_death_actions(action, context, state, frame); }
    __finally { death_action_context = previous; admission.unlock(); }
}
void record_recipe_death(uintptr_t actor) {
    const auto role = recipe_actors.find(actor);
    if (role == recipe_actors.end()) return;
    recipe_state.actor_changed(role->second, RecipeActorState::dead);
    recipe_actors.erase(role);
}
void death_actions_detour(uintptr_t action, uintptr_t context, uintptr_t state, uintptr_t frame) {
    if (stopped.load(std::memory_order_acquire)) { original_death_actions(action, context, state, frame); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    DeathActionContext projection;
    bool tracked = false;
    try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(context + 0x940);
        const auto weight = remaining_weight(actor);
        if (weight.recipe) {
            const auto cached = recipe_actor_credits.find(actor);
            const auto role = recipe_actors.find(actor);
            if (cached != recipe_actor_credits.end()) projection.request = cached->second.request;
            else if (role != recipe_actors.end()) projection.request = role->second.substr(0, role->second.rfind('/'));
            else {
                char name[512];
                if (read_recipe_definition(actor, name)) completed_independent_actor(actor, *spawn_recipe(name), &projection.request);
            }
            tracked = true;
            if (!projection.request.empty()) projection.credit = weight.credit;
            else {
                projection.request = "invalid"; projection.credit = {0, 1};
                recipe_fault.store(true, std::memory_order_release);
            }
            record_recipe_death(actor);
        }
    } catch (...) { recipe_fault.store(true, std::memory_order_release); }
    invoke_death_actions(action, context, state, frame, tracked ? &projection : nullptr, admission);
}
bool cache_remaining(uintptr_t command, uintptr_t record) {
    __try {
        const auto actor = record_actor(record);
        if (!actor) return false;
        const auto count = *reinterpret_cast<const int32_t*>(command + 0xb0);
        if (count < 0 || count >= 100000) return false;
        auto& rows = remaining_weights[command];
        rows.resize(static_cast<size_t>(count) + 1);
        rows[count] = {*reinterpret_cast<const uint32_t*>(record + 12), remaining_weight(actor), true};
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool try_cache_remaining(uintptr_t command, uintptr_t record) {
    try { return cache_remaining(command, record); }
    catch (...) { recipe_fault.store(true, std::memory_order_release); return false; }
}
void __fastcall remaining_added_detour(uintptr_t command, uintptr_t record) {
    if (stopped.load(std::memory_order_acquire)) { original_remaining_added(command, record); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    std::unique_lock<std::recursive_mutex> quantity(quantity_mutex);
    [&]() -> void {
        __try {
            if (!*reinterpret_cast<const int32_t*>(command + 0xb0)) remaining_weights.erase(command);
            try_cache_remaining(command, record);
            original_remaining_added(command, record);
        } __finally { quantity.unlock(); admission.unlock(); }
    }();
}
void __fastcall remaining_dead_detour(uintptr_t command, uintptr_t record) {
    if (stopped.load(std::memory_order_acquire)) { original_remaining_dead(command, record); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    std::unique_lock<std::recursive_mutex> quantity(quantity_mutex);
    [&]() -> void {
        __try {
            const auto identifier = *reinterpret_cast<const uint32_t*>(record + 12);
            const auto before = *reinterpret_cast<const int32_t*>(command + 0xb0);
            original_remaining_dead(command, record);
            const auto after = *reinterpret_cast<const int32_t*>(command + 0xb0);
            const auto found = remaining_weights.find(command);
            if (found != remaining_weights.end() && after < before) {
                auto& rows = found->second;
                if (before >= 0 && rows.size() >= static_cast<size_t>(before)) {
                    rows.resize(before);
                    rows.erase(std::remove_if(rows.begin(), rows.end(), [identifier](const auto& item) {
                        return item.identifier == identifier;
                    }), rows.end());
                }
                if (after < 0 || rows.size() != static_cast<size_t>(after)) rows.clear();
            }
        } __finally { quantity.unlock(); admission.unlock(); }
    }();
}
uintptr_t remaining_manager(uintptr_t command) {
    const auto identifier = *reinterpret_cast<const uint32_t*>(command + 0x38);
    const auto cached = *reinterpret_cast<const uint32_t*>(command + 0x3c);
    if (identifier != cached || (identifier != 0x1fffffe && !*reinterpret_cast<const uintptr_t*>(command + 0x40)))
        refresh_manager(command + 0x38, identifier == cached ? command + 0x38 : command + 0x3c);
    return *reinterpret_cast<const uintptr_t*>(command + 0x40);
}
struct SynchronousRemainder {
    const NativeRecipe* recipe; const RecipeProgress* progress; uint64_t emitted, body_offset = 0;
    const RecipeNativeCheckpoint::Child* recovering = nullptr;
};
bool scope_matches(const std::vector<RecipeMembership>& members, std::string_view owner, std::string_view label) {
    return std::any_of(members.begin(), members.end(), [&](const auto& member) {
        return member.owner == owner && (label.empty() || member.label == label);
    });
}
bool pending_direct_matches(const SummonRequest& pending, std::string_view owner, uint64_t mask, std::string_view label) {
    const auto& progress = recipe_state.requests().at(pending.key);
    if (progress.closed || progress.emitted >= progress.request.maximum_emissions || !(progress.request.logical_mask & mask)) return false;
    auto members = recipe_state.request_memberships(pending.key);
    for (auto creation = recipe_creation; creation; creation = creation->previous)
        if (creation->key == pending.key) members.insert(members.end(), creation->memberships.begin(), creation->memberships.end());
    return std::any_of(members.begin(), members.end(), [&](const auto& member) {
        return member.owner == owner && (label.empty() || member.label == label);
    });
}
bool pending_autonomous_emissions() {
    if (pending_summon_checkpoint) return true;
    for (const auto* producers : {&component_summons, &direct_points}) for (const auto& [component, pending] : *producers) {
        (void)component;
        for (const auto& request : pending.requests) {
            const auto found = recipe_state.requests().find(request.key);
            if (found == recipe_state.requests().end() || (!found->second.closed && found->second.emitted < found->second.request.maximum_emissions)) return true;
        }
    }
    return false;
}
std::optional<bool> pending_summon_matches(const ComponentSummons& component, const SummonRequest& pending,
                                          std::string_view owner, uint64_t mask, std::string_view label) {
    const auto& progress = recipe_state.requests().at(pending.key);
    if (progress.closed || progress.emitted >= progress.request.maximum_emissions || !(progress.request.logical_mask & mask)) return false;
    std::vector<RecipeMembership> members;
    if (pending.sources.empty() || !summon_memberships(component.parent, pending.sources.front().template_name,
            progress.request.label, members)) return {};
    return std::any_of(members.begin(), members.end(), [&](const auto& member) {
        return member.owner == owner && (label.empty() || member.label == label);
    });
}
uint64_t consumer_emitted(uintptr_t command, const std::string& key, const RecipeProgress& progress) {
    for (auto creation = recipe_creation; creation; creation = creation->previous) {
        if (creation->key != key || !creation->actor || creation->emission.ordinal != progress.emitted) continue;
        const auto count = *reinterpret_cast<const int32_t*>(command + 0xb0);
        const auto records = *reinterpret_cast<const uintptr_t*>(command + 0xa8);
        if (count < 0 || count > 100000) return progress.emitted;
        for (int32_t i = 0; i < count; ++i)
            if (record_actor(records + static_cast<uintptr_t>(i) * 0x68) == creation->actor) return progress.emitted + 1;
    }
    return progress.emitted;
}
bool synchronous_remainders(uintptr_t command, std::vector<SynchronousRemainder>& result) {
    if (worker_recipes.empty() && !pending_autonomous_emissions()) return true;
    const auto manager = remaining_manager(command);
    char owner[512]; std::string label;
    if (!manager || !read_definition_name(manager, owner) || !native_text(command + 0x58, label, 1024)) return false;
    auto mask = *reinterpret_cast<const uint64_t*>(command + 0x88);
    if (mask & (uint64_t{1} << 44)) mask = encounter_ai_mask;
    std::set<std::string> counted;
    for (const auto& [worker, key] : worker_recipes) {
        (void)worker;
        const auto found = recipe_state.requests().find(key);
        if (found == recipe_state.requests().end()) continue;
        const auto& progress = found->second;
        const auto& request = progress.request;
        const auto recipe = native_recipes.find(request.origin);
        if (recipe == native_recipes.end() || std::string_view("igaspb").find(recipe->second.definition.family) == std::string_view::npos ||
            progress.closed || request.maximum_emissions <= progress.emitted || !request.physical_units ||
            request.owner != owner || !(request.logical_mask & mask) || (!label.empty() && request.label != label) ||
            !counted.insert(key).second) continue;
        const auto emitted = consumer_emitted(command, key, progress);
        if (emitted < request.maximum_emissions) result.push_back({&recipe->second, &progress, emitted});
    }
    for (const auto* producers : {&component_summons, &direct_points}) for (const auto& [component, pending] : *producers) {
        (void)component;
        for (const auto& request : pending.requests) {
            const std::optional<bool> matches = producers == &direct_points ? pending_direct_matches(request, owner, mask, label) :
                pending_summon_matches(pending, request, owner, mask, label);
            if (!matches) return false;
            if (!*matches || !counted.insert(request.key).second) continue;
            const auto& progress = recipe_state.requests().at(request.key);
            const auto emitted = consumer_emitted(command, request.key, progress);
            if (emitted < progress.request.maximum_emissions)
                result.push_back({&native_recipes.at(request.origin), &progress, emitted, request.body_offset});
        }
    }
    if (pending_summon_checkpoint) {
        for (const auto& component : pending_summon_checkpoint->summons) for (const auto& request : component.requests) {
            const auto& progress = recipe_state.requests().at(request.key);
            if (progress.closed || progress.emitted >= progress.request.maximum_emissions || !(progress.request.logical_mask & mask) ||
                !scope_matches(request.memberships, owner, label) || !counted.insert(request.key).second) continue;
            result.push_back({&native_recipes.at(request.origin), &progress, progress.emitted, request.body_offset});
        }
        const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
        for (const auto& [role, child] : pending_summon_checkpoint->children) {
            const auto key = role.substr(0, role.rfind('/'));
            const auto& progress = recipe_state.requests().at(key);
            const auto& actor = progress.actors.at(role);
            if (!(progress.request.logical_mask & mask) || !scope_matches(actor.memberships, owner, label)) continue;
            const auto live = actor.native_name.empty() ? 0 : find_entity(world, actor.native_name.c_str());
            std::vector<RecipeMembership> actual;
            if (live && !native_memberships(world, live, actual)) return false;
            if (scope_matches(actual, owner, label)) continue;
            result.push_back({&native_recipes.at(progress.request.origin), &progress, actor.ordinal, 0, &child});
        }
    }
    return true;
}
bool remaining_remainders(uintptr_t command, std::vector<RecipeCredit>& total, std::vector<RecipeCredit>& living, bool& tracked) {
    std::vector<SynchronousRemainder> remainders;
    if (!synchronous_remainders(command, remainders)) return false;
    for (const auto& remainder : remainders) {
        const auto& request = remainder.progress->request;
        if (remainder.recovering) {
            total.push_back({request.credit_limit && remainder.emitted + 1 == request.credit_limit ?
                request.final_credit_units : request.logical_units, request.physical_units});
            living.push_back(total.back()); tracked = true; continue;
        }
        auto units = (request.maximum_emissions - remainder.emitted) * request.logical_units;
        if (request.final_credit_units) units -= request.logical_units - request.final_credit_units;
        const RecipeCredit credit{units, request.physical_units};
        total.push_back(credit); living.push_back(credit); tracked = true;
    }
    return true;
}
bool health_remainders(uintptr_t command, std::vector<RecipeHealth>& health) {
    std::vector<SynchronousRemainder> remainders;
    if (!synchronous_remainders(command, remainders)) return false;
    for (const auto& remainder : remainders) {
        const auto& request = remainder.progress->request;
        if (remainder.recovering) {
            const auto& value = remainder.recovering->health;
            health.push_back({{request.credit_limit && remainder.emitted + 1 == request.credit_limit ?
                request.final_credit_units : request.logical_units, request.physical_units}, value[0] + value[2], value[1] + value[3]});
            continue;
        }
        const auto& values = remainder.recipe->definition.hitpoints;
        if (values.empty() || values.size() != remainder.recipe->bodies.size()) return false;
        for (auto ordinal = remainder.emitted; ordinal < request.maximum_emissions; ++ordinal) {
            const auto& value = values[(ordinal % values.size() + remainder.body_offset % values.size()) % values.size()];
            const auto units = ordinal + 1 == request.maximum_emissions && request.final_credit_units ?
                request.final_credit_units : request.logical_units;
            health.push_back({{units, request.physical_units}, value.maximum, value.starting});
        }
    }
    return true;
}
bool remaining_inputs(uintptr_t command, std::vector<RecipeCredit>& total, std::vector<RecipeCredit>& living,
                      std::vector<RemainingItem>& present, bool& tracked, bool& blocked, int32_t& threshold) {
    __try {
        const auto count = *reinterpret_cast<const int32_t*>(command + 0xb0);
        if (count < 0 || count > 100000) return false;
        threshold = *reinterpret_cast<const int32_t*>(command + 0xc0);
        auto& weights = remaining_weights[command];
        const auto records = *reinterpret_cast<const uintptr_t*>(command + 0xa8);
        for (int32_t i = 0; i < count; ++i) {
            const auto record = records + static_cast<uintptr_t>(i) * 0x68;
            const auto previous_id = *reinterpret_cast<const uint32_t*>(record + 12);
            const auto actor = record_actor(record);
            const auto identifier = *reinterpret_cast<const uint32_t*>(record + 12);
            const auto found = std::find_if(weights.begin(), weights.end(), [identifier, previous_id](const auto& item) {
                return item.known && (item.identifier == identifier || item.identifier == previous_id);
            });
            const auto cached = found != weights.end() ? &*found :
                identifier == 0x1fffffe && weights.size() >= static_cast<size_t>(count) && weights[i].known ? &weights[i] : nullptr;
            if (!actor && !cached) return false;
            const auto weight = cached && cached->weight.recipe ? cached->weight :
                actor ? remaining_weight(actor) : cached->weight;
            present.push_back({actor ? identifier : cached->identifier, weight, true}); tracked |= weight.recipe;
            total.push_back(weight.credit);
        }
        weights = present;
        if (!count) remaining_weights.erase(command);
        if (!remaining_remainders(command, total, living, tracked)) return false;
        if (!tracked || (threshold >= 0 && credit_at_most(total, threshold))) return true;
        const auto manager = remaining_manager(command);
        if (!manager) return false;
        blocked = *reinterpret_cast<const uint8_t*>(manager + 0xdaf) || *reinterpret_cast<const uint8_t*>(manager + 0xdb1);
        if (blocked) return true;
        for (int32_t i = 0; i < count; ++i) {
            const auto record = records + static_cast<uintptr_t>(i) * 0x68;
            const auto actor = record_actor(record);
            const auto weight = present[i].weight;
            const auto dead = actor && reinterpret_cast<bool(*)(uintptr_t)>(
                *reinterpret_cast<const uintptr_t*>(*reinterpret_cast<const uintptr_t*>(actor) + 0x108))(actor);
            if (actor && (!dead || ignored_actor(actor))) living.push_back(weight.credit);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool empty_request_pending(uintptr_t manager, uint64_t mask, uintptr_t label) {
    char owner[512]; std::string filter;
    const auto selected = mask & (uint64_t{1} << 44) ? encounter_ai_mask : mask;
    return manager && read_definition_name(manager, owner) && (!label || native_text(label, filter, 1024)) &&
        recipe_state.pending_empty(owner, selected, filter);
}
bool remaining_encounter_heavy(uintptr_t command) {
    if (stopped.load(std::memory_order_acquire)) return false;
    const auto mask = *reinterpret_cast<const uint64_t*>(command + 0x88);
    if (!mask || !(mask & (mask - 1))) return false;
    std::string label;
    char owner[512];
    if (!native_text(command + 0x58, label, 1024) || !label.empty() ||
        !read_definition_name(remaining_manager(command), owner)) return false;
    for (const auto& [key, progress] : recipe_state.requests()) {
        (void)key;
        if (progress.request.logical_mask & mask) continue;
        for (const auto& [identity, member] : progress.actors) {
            (void)identity;
            if (member.native_name.empty() || std::none_of(member.memberships.begin(), member.memberships.end(),
                    [&](const auto& value) { return value.owner == owner; })) continue;
            const auto world = world_global ? *reinterpret_cast<const uintptr_t*>(world_global) : 0;
            const auto actor = world ? find_entity(world, member.native_name.c_str()) : 0;
            char body[512];
            if (!actor || !read_recipe_definition(actor, body)) continue;
            const auto tracking = spawn_tracking(body);
            // heavy replacements of ignored fodder still have to die before the arena clears.
            if (tracking && (tracking->physical & 0xffffc000ULL)) return true;
        }
    }
    return false;
}
bool remaining_encounter_heavy_safe(uintptr_t command) noexcept {
    __try { return remaining_encounter_heavy(command); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}
bool optional_zombie_completion(uintptr_t command) {
    const auto manager = *reinterpret_cast<const uintptr_t*>(command + 0x40);
    char owner[512];
    std::string label;
    if (!manager || !world_global || !read_definition_name(manager, owner) ||
        !native_text(command + 0x58, label, 1024) ||
        *reinterpret_cast<const uint8_t*>(manager + 0xdaf) || *reinterpret_cast<const uint8_t*>(manager + 0xdb1)) return false;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    if (!world) return false;
    const auto selected = *reinterpret_cast<const uint64_t*>(command + 0x88);
    const auto mask = selected & (uint64_t{1} << 44) ? encounter_ai_mask : selected;
    const auto optional = [world](const char* name) {
        constexpr uint64_t zombies = uint64_t{1} | (uint64_t{1} << 2);
        const auto tracking = spawn_tracking(name);
        const auto recipe = spawn_recipe(name);
        return tracking && recipe && tracking->physical && !(tracking->physical & ~zombies) &&
            find_entity(world, ("ap_v070_optional_zombies_r" + std::string(recipe->origin.data(), recipe->origin.size())).c_str());
    };
    bool found = false;
    const auto count = *reinterpret_cast<const int32_t*>(manager + 0xc88);
    const auto records = *reinterpret_cast<const uintptr_t*>(manager + 0xc80);
    if (count < 0 || count > 100000 || (count && !records)) return false;
    for (int32_t i = 0; i < count; ++i) {
        const auto record = records + static_cast<uintptr_t>(i) * 0x68;
        if (!(*reinterpret_cast<const uint64_t*>(record) & mask) ||
            (!label.empty() && compare_text(record + 0x28, command + 0x58))) continue;
        const auto actor = record_actor(record);
        char name[512];
        if (!actor || reinterpret_cast<bool(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
                *reinterpret_cast<const uintptr_t*>(actor) + 0x108))(actor)) continue;
        if (!read_recipe_definition(actor, name)) return false;
        if (!optional(name)) return false;
        found = true;
    }
    std::vector<std::string> cancel;
    for (const auto& [key, progress] : recipe_state.requests()) {
        const auto& request = progress.request;
        if (progress.closed || request.owner != owner || !(request.logical_mask & mask) ||
            (!label.empty() && request.label != label) ||
            (request.maximum_emissions && progress.emitted >= request.maximum_emissions)) continue;
        const auto recipe = native_recipes.find(request.origin);
        if (recipe == native_recipes.end() || recipe->second.bodies.empty() || progress.reserved) return false;
        const auto remaining = request.maximum_emissions ? request.maximum_emissions - progress.emitted : recipe->second.bodies.size();
        for (uint64_t i = 0; i < std::min<uint64_t>(remaining, recipe->second.bodies.size()); ++i) {
            char name[512];
            const auto body = recipe->second.bodies[(progress.emitted + i) % recipe->second.bodies.size()];
            if (!body || !read_definition_name(body, name) || !optional(name)) return false;
        }
        cancel.push_back(key); found = true;
    }
    if (!found) return false;
    for (const auto& key : cancel) recipe_state.cancel(key);
    return true;
}
bool optional_zombie_completion_safe(uintptr_t command) noexcept {
    __try { return optional_zombie_completion(command); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void trace_remaining_actors(uintptr_t command, const char* owner, int32_t count) noexcept {
    __try {
        const auto records = *reinterpret_cast<const uintptr_t*>(command + 0xa8);
        for (int32_t i = 0; records && i < count && i < 32; ++i) {
            const auto actor = record_actor(records + static_cast<uintptr_t>(i) * 0x68);
            char name[512]{}, body[512]{}, position[768]{};
            if (!actor || !read_instance_name(actor, name) || !read_definition_name(actor, body)) continue;
            const auto xyz = reinterpret_cast<const float*>(actor + 0x74);
            std::snprintf(position, sizeof(position), "%s@%g,%g,%g", name, xyz[0], xyz[1], xyz[2]);
            commands::enemy_spawn_trace("waiting", owner, body, position, actor);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void trace_quantity_wait(uintptr_t command, char kind, uint64_t result) {
    const auto mask = *reinterpret_cast<const uint64_t*>(command + 0x88);
    const auto target_bits = *reinterpret_cast<const uint32_t*>(command + 0xc0);
    const auto count = *reinterpret_cast<const int32_t*>(command + 0xb0);
    const bool fault = recipe_fault.load(std::memory_order_acquire);
    const uint8_t stage = fault ? 8 : (result & 0xff) ? 4 : count ? 1 : 2;
    const auto key = std::make_tuple(command, kind, mask, target_bits);
    if (!quantity_wait_traces.count(key) && quantity_wait_traces.size() >= 4096) return;
    auto& traced = quantity_wait_traces[key];
    bool actors_due = false;
    if (kind == 'r') {
        if (result & 0xff) remaining_wait_started.erase(command);
        else if (remaining_wait_started.size() < 4096) {
            const auto started = remaining_wait_started.emplace(command, GetTickCount64()).first->second;
            actors_due = !(traced & 16) && GetTickCount64() - started >= 15000;
        }
    }
    if ((traced & stage) && !actors_due) return;
    traced |= stage;
    char owner[512]{};
    const auto manager = remaining_manager(command);
    if (manager) read_definition_name(manager, owner);
    if (actors_due) { traced |= 16; trace_remaining_actors(command, owner, count); }
    const double target = kind == 'h' ? *reinterpret_cast<const float*>(command + 0xc0) :
        *reinterpret_cast<const int32_t*>(command + 0xc0);
    commands::enemy_quantity_wait_trace(kind, owner, mask, target, count, (result & 0xff) != 0, fault);
}
uint64_t remaining_result(uintptr_t command) {
    std::vector<RecipeCredit> total, living;
    std::vector<RemainingItem> present;
    bool tracked = false, blocked = false; int32_t threshold = 0;
    if (!remaining_inputs(command, total, living, present, tracked, blocked, threshold))
        return tracked || pending_autonomous_emissions() ? 0 : original_remaining(command);
    if (threshold == 0 && remaining_encounter_heavy_safe(command)) return 0;
    if (!tracked) return original_remaining(command);
    if (empty_request_pending(remaining_manager(command), *reinterpret_cast<const uint64_t*>(command + 0x88), command + 0x58)) return 0;
    if (threshold < 0) return 0;
    if (threshold == 0 && !blocked && optional_zombie_completion_safe(command)) return 1;
    return credit_at_most(total, threshold) || (!blocked && credit_at_most(living, threshold));
}
uint64_t __fastcall remaining_detour(uintptr_t command) {
    if (stopped.load(std::memory_order_acquire)) return original_remaining(command);
    WaitCost cost;
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    std::lock_guard<std::recursive_mutex> lock(quantity_mutex);
    try { const auto result = remaining_result(command); trace_quantity_wait(command, 'r', result); return result; }
    catch (...) { recipe_fault.store(true, std::memory_order_release); return 0; }
}
void initialize_kill(uintptr_t command, uintptr_t manager) {
    std::lock_guard<std::recursive_mutex> lock(quantity_mutex);
    auto& progress = kill_progress[command];
    char owner[512];
    if (!read_definition_name(manager, owner) || !native_text(command + 0x58, progress.label, 1024)) return;
    progress.owner = owner; progress.mask = *reinterpret_cast<const uint64_t*>(command + 0x88);
    progress.baseline = recipe_state.death_baseline(progress.owner, progress.mask, progress.label);
    progress.seen = static_cast<uint32_t>(*reinterpret_cast<const int32_t*>(command + 0xa8));
    uint64_t covered = 0;
    for (const auto& [credit, count] : progress.deaths) { (void)credit; covered += count; }
    if (progress.seen > covered) progress.deaths[{1, 1}] += progress.seen - covered;
    progress.initialized = true;
}
uintptr_t kill_construct_detour(uintptr_t command, int32_t threshold, uintptr_t label, uint64_t mask, uint64_t clock) {
    if (stopped.load(std::memory_order_acquire)) return original_kill_construct(command, threshold, label, mask, clock);
    const auto result = original_kill_construct(command, threshold, label, mask, clock);
    std::lock_guard<std::recursive_mutex> lock(quantity_mutex);
    kill_progress.erase(command); kill_progress.emplace(command, KillProgress{});
    return result;
}
void kill_dead_detour(uintptr_t command, uintptr_t record) {
    if (stopped.load(std::memory_order_acquire)) { original_kill_dead(command, record); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    std::unique_lock<std::recursive_mutex> quantity(quantity_mutex);
    [&]() -> void {
        __try {
            const auto actor = record_actor(record);
            const auto weight = remaining_weight(actor);
            const auto before = *reinterpret_cast<const int32_t*>(command + 0xa8);
            const auto cached = recipe_actor_credits.find(actor);
            const bool counted = weight.recipe && cached != recipe_actor_credits.end() &&
                recipe_state.requests().count(cached->second.request);
            if (counted) record_recipe_death(actor);
            bool selected = false;
            if (weight.recipe) {
                char name[512];
                const auto tracking = read_recipe_definition(actor, name) ? spawn_tracking(name) : std::nullopt;
                const auto filter = *reinterpret_cast<const uint64_t*>(command + 0x88);
                const auto mask = filter & (uint64_t{1} << 44) ? encounter_ai_mask : filter;
                selected = tracking && (tracking->source & mask) &&
                    (!*reinterpret_cast<const int32_t*>(command + 0x68) || !compare_text(record + 0x28, command + 0x58));
            }
            original_kill_dead(command, record);
            // recipe deaths use their logical source and survive expired native actor references.
            if (weight.recipe) *reinterpret_cast<int32_t*>(command + 0xa8) = before + (!counted && selected);
            const auto after = *reinterpret_cast<const int32_t*>(command + 0xa8);
            if (after > before) {
                auto& progress = kill_progress[command];
                if (!progress.initialized && before > 0) progress.deaths[{1, 1}] = before;
                ++progress.deaths[{static_cast<uint32_t>(weight.credit.numerator), weight.credit.denominator}];
                progress.seen = static_cast<uint32_t>(after); progress.tracked |= weight.recipe;
            }
        } __finally { quantity.unlock(); admission.unlock(); }
    }();
}
bool recipe_scope(std::string_view owner, uint64_t mask, std::string_view label, bool empty_only) {
    if (mask & (uint64_t{1} << 44)) mask = encounter_ai_mask;
    for (const auto& [key, progress] : recipe_state.requests()) {
        (void)key;
        const auto& request = progress.request;
        if ((!empty_only || !request.physical_units) && request.owner == owner && (request.logical_mask & mask) &&
            (label.empty() || label == request.label)) return true;
    }
    return false;
}
bool future_matches(uintptr_t worker, uint64_t mask, uintptr_t label) {
    if (mask & (uint64_t{1} << 44)) mask = encounter_ai_mask;
    auto candidate = *reinterpret_cast<const uint64_t*>(worker + 0x88);
    if (candidate & (uint64_t{1} << 44)) candidate = encounter_ai_mask;
    return (candidate & mask) && (!label || !*reinterpret_cast<const int32_t*>(label + 16) ||
        !compare_text(worker + 0x58, label));
}
bool native_future(uintptr_t manager, uint64_t mask = uint64_t{1} << 44, uintptr_t label = 0) {
    if (empty_request_pending(manager, mask, label)) return true;
    const auto count = *reinterpret_cast<const int32_t*>(manager + 0xcc8);
    const auto commands = *reinterpret_cast<const uintptr_t*>(manager + 0xcc0);
    if (count < 0 || count > 100000) return true;
    for (int32_t i = 0; i < count; ++i) {
        const auto command = *reinterpret_cast<const uintptr_t*>(commands + static_cast<uintptr_t>(i) * 8);
        if (const auto worker = cast_maintain(command); worker && !*reinterpret_cast<const uint8_t*>(worker + 0xcc) &&
            future_matches(worker, mask, label) &&
            (*reinterpret_cast<const int32_t*>(worker + 0x94) <= 0 ||
             *reinterpret_cast<const int32_t*>(worker + 0x94) > *reinterpret_cast<const int32_t*>(worker + 0xc8))) return true;
        if (const auto worker = cast_stagger(command); worker && !*reinterpret_cast<const uint8_t*>(worker + 0xb9) &&
            future_matches(worker, mask, label) && *reinterpret_cast<const int32_t*>(worker + 0xa8) > 0) return true;
    }
    if (!component_summons.empty() || !direct_points.empty() || pending_summon_checkpoint) {
        char owner[512]; std::string filter;
        if (!read_definition_name(manager, owner) || (label && !native_text(label, filter, 1024))) return true;
        if (mask & (uint64_t{1} << 44)) mask = encounter_ai_mask;
        for (const auto* producers : {&component_summons, &direct_points}) for (const auto& [component, pending] : *producers) {
            (void)component;
            for (const auto& request : pending.requests) {
                const std::optional<bool> matches = producers == &direct_points ? pending_direct_matches(request, owner, mask, filter) :
                    pending_summon_matches(pending, request, owner, mask, filter);
                if (!matches || *matches) return true;
            }
        }
        if (pending_summon_checkpoint) {
            for (const auto& component : pending_summon_checkpoint->summons) for (const auto& request : component.requests) {
                const auto& progress = recipe_state.requests().at(request.key);
                if (!progress.closed && progress.emitted < progress.request.maximum_emissions && (progress.request.logical_mask & mask) &&
                    scope_matches(request.memberships, owner, filter)) return true;
            }
            for (const auto& [role, child] : pending_summon_checkpoint->children) {
                (void)child;
                const auto& progress = recipe_state.requests().at(role.substr(0, role.rfind('/')));
                if ((progress.request.logical_mask & mask) && scope_matches(progress.actors.at(role).memberships, owner, filter)) return true;
            }
        }
    }
    return false;
}
uint64_t kill_result(uintptr_t command) {
    const auto manager = *reinterpret_cast<const uintptr_t*>(command + 0x40);
    auto& progress = kill_progress[command];
    if (!progress.initialized) initialize_kill(command, manager);
    if (!progress.initialized) return 0;
    const auto target = *reinterpret_cast<const int32_t*>(command + 0xac);
    if (target <= 0) return 1;
    auto deaths = recipe_state.deaths(progress.owner, progress.mask, progress.label, progress.baseline);
    bool tracked = progress.tracked || recipe_scope(progress.owner, progress.mask, progress.label, false) ||
        std::any_of(deaths.begin(), deaths.end(), [](auto value) { return value.numerator != 0; });
    for (const auto& [credit, count] : progress.deaths) deaths.push_back({credit.first * count, credit.second});
    std::vector<RecipeCredit> living;
    const auto count = *reinterpret_cast<const int32_t*>(manager + 0xc88);
    const auto records = *reinterpret_cast<const uintptr_t*>(manager + 0xc80);
    if (count < 0 || count > 100000) return 0;
    const auto mask = progress.mask & (uint64_t{1} << 44) ? encounter_ai_mask : progress.mask;
    for (int32_t i = 0; i < count; ++i) {
        const auto record = records + static_cast<uintptr_t>(i) * 0x68;
        if (!(*reinterpret_cast<const uint64_t*>(record) & mask) ||
            (!progress.label.empty() && compare_text(record + 0x28, command + 0x58))) continue;
        const auto actor = record_actor(record);
        if (!actor || reinterpret_cast<bool(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(actor) + 0x108))(actor)) continue;
        const auto weight = remaining_weight(actor); tracked |= weight.recipe; living.push_back(weight.credit);
    }
    if (!tracked) {
        std::vector<SynchronousRemainder> pending;
        if (!synchronous_remainders(command, pending)) return 0;
        if (pending.empty()) return original_kill(command);
    }
    uint32_t low = 0, high = static_cast<uint32_t>(target);
    while (low < high) {
        const auto middle = low + (high - low + 1) / 2;
        if (credit_at_least(deaths, middle)) low = middle; else high = middle - 1;
    }
    progress.seen = low;
    *reinterpret_cast<int32_t*>(command + 0xa8) = static_cast<int32_t>(low);
    if (credit_at_least(deaths, static_cast<uint32_t>(target))) return 1;
    if (native_future(manager, progress.mask, command + 0x58)) return 0;
    if (optional_zombie_completion_safe(command)) return 1;
    if (!count) return 1;
    auto potential = deaths; potential.insert(potential.end(), living.begin(), living.end());
    if (!credit_at_least(potential, static_cast<uint32_t>(target))) {
        *reinterpret_cast<uint8_t*>(command + 0x90) = 1;
        return credit_at_most(living, 0);
    }
    return 0;
}
uint64_t kill_detour(uintptr_t command) {
    if (stopped.load(std::memory_order_acquire)) return original_kill(command);
    WaitCost cost;
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    std::lock_guard<std::recursive_mutex> quantity(quantity_mutex);
    try {
        const auto result = kill_result(command);
        auto& progress = kill_progress[command];
        const uint8_t stage = (result & 0xff) ? 2 : 1;
        if (progress.initialized && !(progress.traced & stage)) {
            progress.traced |= stage;
            commands::enemy_kill_wait_trace(progress.owner.c_str(), progress.label.c_str(), progress.mask,
                *reinterpret_cast<const int32_t*>(command + 0xac), progress.seen, stage == 2);
        }
        return result;
    } catch (...) { recipe_fault.store(true, std::memory_order_release); return 0; }
}
RemainingWeight health_weight(uintptr_t command, uintptr_t record) {
    const auto id = *reinterpret_cast<const uint32_t*>(record + 12);
    const auto found = health_progress.find(command);
    if (found != health_progress.end()) {
        const auto item = found->second.actors.find(id);
        if (item != found->second.actors.end()) return item->second.weight;
    }
    return remaining_weight(record_actor(record));
}
float actor_health(uintptr_t actor, uintptr_t slot) {
    return reinterpret_cast<float(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(actor) + slot))(actor);
}
void health_added_detour(uintptr_t command, uintptr_t record) {
    if (stopped.load(std::memory_order_acquire)) { original_health_added(command, record); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    std::unique_lock<std::recursive_mutex> quantity(quantity_mutex);
    [&]() -> void {
        __try {
            const auto before = *reinterpret_cast<const int32_t*>(command + 0xb0);
            if (!before && !*reinterpret_cast<const float*>(command + 0xc8)) health_progress.erase(command);
            const auto actor = record_actor(record);
            const auto identifier = *reinterpret_cast<const uint32_t*>(record + 12);
            const auto weight = health_weight(command, record);
            const auto initial = actor ? actor_health(actor, 0x6f0) : 0;
            const auto current = actor ? actor_health(actor, 0x6e8) : 0;
            original_health_added(command, record);
            if (*reinterpret_cast<const int32_t*>(command + 0xb0) > before) {
                auto& progress = health_progress[command];
                progress.actors[identifier] = {weight, initial, current}; progress.tracked |= weight.recipe;
            }
        } __finally { quantity.unlock(); admission.unlock(); }
    }();
}
void health_damage_detour(uintptr_t command, uintptr_t record, float damage) {
    if (stopped.load(std::memory_order_acquire)) { original_health_damage(command, record, damage); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    std::unique_lock<std::recursive_mutex> quantity(quantity_mutex);
    [&]() -> void {
        __try {
            const auto actor = record_actor(record);
            const auto identifier = *reinterpret_cast<const uint32_t*>(record + 12);
            const auto current = actor ? actor_health(actor, 0x6e8) : 0;
            original_health_damage(command, record, damage);
            auto& progress = health_progress[command];
            const auto found = progress.actors.find(identifier);
            if (found != progress.actors.end()) {
                found->second.current = actor ? current : found->second.current - damage;
            }
        } __finally { quantity.unlock(); admission.unlock(); }
    }();
}
uint64_t health_result(uintptr_t command) {
    if (!*reinterpret_cast<const int32_t*>(command + 0xb0) && !*reinterpret_cast<const float*>(command + 0xc8))
        health_progress.erase(command);
    const auto native = original_health(command);
    auto found = health_progress.find(command);
    const auto manager = *reinterpret_cast<const uintptr_t*>(command + 0x40);
    const auto mask = *reinterpret_cast<const uint64_t*>(command + 0x88);
    std::string label;
    char owner[512];
    const auto empty = manager && native_text(command + 0x58, label, 1024) && read_definition_name(manager, owner) &&
        recipe_scope(owner, mask, label, true);
    std::vector<RecipeHealth> health;
    if (!health_remainders(command, health)) return 0;
    const auto pending = !health.empty();
    if (!pending && (found == health_progress.end() || !found->second.tracked)) {
        if (empty && !*reinterpret_cast<const int32_t*>(command + 0xb0))
            return !native_future(manager, mask, command + 0x58);
        return native;
    }
    auto& progress = health_progress[command];
    progress.tracked |= pending;
    const auto count = *reinterpret_cast<const int32_t*>(command + 0xb0);
    const auto data = *reinterpret_cast<const uintptr_t*>(command + 0xa8);
    if (count < 0 || count > 100000) return 0;
    std::vector<uint32_t> present;
    for (int32_t i = 0; i < count; ++i) {
        const auto record = data + static_cast<uintptr_t>(i) * 0x68;
        present.push_back(*reinterpret_cast<const uint32_t*>(record + 12));
    }
    for (auto actor = progress.actors.begin(); actor != progress.actors.end();) {
        if (std::find(present.begin(), present.end(), actor->first) != present.end()) { ++actor; continue; }
        const auto& item = actor->second;
        uint32_t bits = 0; std::memcpy(&bits, &item.initial, sizeof(bits));
        ++progress.retired[{static_cast<uint32_t>(item.weight.credit.numerator), item.weight.credit.denominator, bits}];
        actor = progress.actors.erase(actor);
    }
    if (!manager) return 0;
    if (!pending && (!count || !*reinterpret_cast<const int32_t*>(manager + 0xc88)))
        return !native_future(manager, mask, command + 0x58);
    for (const auto& [id, item] : progress.actors) { (void)id; health.push_back({item.weight.credit, item.initial, item.current}); }
    for (const auto& [key, number] : progress.retired) {
        const auto [units, denominator, bits] = key;
        if (!units) continue;
        if (number > UINT64_MAX / units) return 0;
        float initial = 0; std::memcpy(&initial, &bits, sizeof(bits));
        health.push_back({{units * number, denominator}, initial, 0});
    }
    if (std::none_of(health.begin(), health.end(), [](const auto& item) { return item.credit.numerator != 0; }))
        return !native_future(manager, mask, command + 0x58);
    const auto result = health_at_most(health, *reinterpret_cast<const float*>(command + 0xc0));
    return result && *result;
}
uint64_t health_detour(uintptr_t command) {
    if (stopped.load(std::memory_order_acquire)) return original_health(command);
    WaitCost cost;
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    std::lock_guard<std::recursive_mutex> lock(quantity_mutex);
    try { const auto result = health_result(command); trace_quantity_wait(command, 'h', result); return result; }
    catch (...) { recipe_fault.store(true, std::memory_order_release); return 0; }
}
float scale_movement(uintptr_t ai, float value) noexcept {
    const auto started=CostClock::now();
    char name[512];
    const auto scaled=read_recipe_definition(ai, name) || read_definition_name(ai, name) ? movement_speed(value, name) : value;
    motion_queries.fetch_add(1,std::memory_order_relaxed);
    motion_lookup_us.fetch_add(elapsed_us(started),std::memory_order_relaxed);
    return scaled;
}
bool private_supply(std::string_view name, std::string_view suffix) noexcept {
    constexpr std::string_view prefix = "ap_v070_supply_";
    if (name.size() != prefix.size() + 20 + suffix.size() || name.substr(0, prefix.size()) != prefix ||
        name.substr(prefix.size() + 20) != suffix) return false;
    for (const auto digit : name.substr(prefix.size(), 20))
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) return false;
    return true;
}
bool private_supplier(uintptr_t ai) noexcept {
    char name[512];
    return read_definition_name(ai, name) && private_supply(name, "_actors_ai__ap_v070_s00100");
}
void __fastcall occupied_hosts_detour(HostList* out) {
    original_hosts(out);
    if (stopped.load(std::memory_order_acquire)) return;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto collection = *reinterpret_cast<const uintptr_t*>(world + 0x62088);
    const auto records = *reinterpret_cast<const uintptr_t*>(collection);
    const auto count = *reinterpret_cast<const int32_t*>(collection + 8);
    for (int32_t i = 0; i < count; ++i) {
        const auto actor = resolve_actor(records + static_cast<uintptr_t>(i) * 24);
        if (!actor || !private_supplier(actor)) continue;
        bool present = false;
        for (int32_t j = 0; j < out->count; ++j) present |= out->data[j] == actor;
        if (present) continue;
        // The engine owns this scratch list and its allocator flags.
        if (out->count == out->capacity) grow_hosts(out, out->count);
        if (out->count < out->capacity) out->data[out->count++] = actor;
    }
}

unsigned special_kind(uintptr_t actor, unsigned* physical = nullptr) {
    if (physical) *physical = 9;
    char name[512];
    if (read_recipe_definition(actor, name) || read_definition_name(actor, name)) {
        if (physical) if (const auto tracking = spawn_tracking(name)) *physical = special_species_index(tracking->physical);
        const auto frozen = spawn_limits(name);
        if (frozen) return frozen->kind;
    }
    // Native summon masks can identify Spirit templates or boss proxies.
    constexpr std::string_view kinds[]{"ai/superheavy/marauder", "ai/superheavy/archvile", "ai/ambient/spirit"};
    constexpr std::string_view species[]{"ai/superheavy/marauder", "ai/superheavy/archvile", "ai/ambient/spirit",
        "ai/ambient/buffpod", "ai/fodder/zombie_t1_screecher", "ai/heavy/bloodangel",
        "ai/fodder/prowler_cursed", "ai/fodder/imp_stone", "ai/superheavy/baron_armored"};
    bool native_other = false;
    auto definition = *reinterpret_cast<uintptr_t*>(actor + 0xa8);
    for (unsigned depth = 0; definition && depth < 64; ++depth) {
        const auto value = *reinterpret_cast<const char**>(definition + 8);
        if (value) {
            const std::string_view inherited(value);
            if (physical) {
                for (unsigned index = 0; index < std::size(species); ++index)
                    if (inherited == species[index]) *physical = index;
                native_other |= inherited.substr(0, 10) == "ai/fodder/";
                for (const auto type : {"ai/ambient/zombie_cueball", "ai/ambient/tentacle", "ai/ambient/tentacle_liquid",
                                       "ai/ambient/super_tentacle", "ai/ambient/turret", "ai/ambient/demonic_trooper"})
                    native_other |= inherited == type;
                if (inherited == "ai/fodder/prowler_cursed" || inherited == "ai/fodder/prowler" ||
                    inherited == "ai/fodder/carcass") return 3;
            }
            if (inherited=="ai/ambient/buffpod") return 4;
            if (inherited=="ai/fodder/zombie_t1_screecher") return 5;
            if (inherited.substr(0, 8) == "ai/boss/") return physical ? 9 : 3;
            for (unsigned kind = 0; kind < std::size(kinds); ++kind) if (inherited == kinds[kind]) return kind;
            if (inherited.substr(0, 14) == "ai/superheavy/") return 6;
            for (const auto fodder : {"ai/fodder/zombie_tier_1", "ai/fodder/zombie_tier_3", "ai/fodder/imp",
                                     "ai/fodder/hell_soldier", "ai/fodder/soldier", "ai/fodder/gargoyle"})
                if (inherited == fodder) return 7;
        }
        const auto parent = *reinterpret_cast<uintptr_t*>(definition + 0x158);
        if (parent == definition) break;
        definition = parent;
    }
    return physical && native_other ? 8 : 3;
}
bool population_member(uintptr_t world, uintptr_t actor, uint64_t scope, unsigned depth = 0) {
    char name[512];
    if (read_recipe_definition(actor, name) || read_definition_name(actor, name)) {
        const auto frozen = spawn_limits(name);
        if (frozen && frozen->scope) {
            if (frozen->scope == scope) return true;
            const auto role = recipe_actors.find(actor);
            if (role == recipe_actors.end() || depth >= 8) return false;
            const auto child = summon_children.find(role->second);
            if (child == summon_children.end() || child->second.parent.empty()) return false;
            const auto parent = find_entity(world, child->second.parent.c_str());
            return parent && parent != actor && population_member(world, parent, scope, depth + 1);
        }
    }
    const auto managers = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
    const auto count = *reinterpret_cast<const int32_t*>(world + 0x126660);
    if (count < 0 || count > 100000) return false;
    for (int32_t index = 0; index < count; ++index) {
        const auto manager = *reinterpret_cast<const uintptr_t*>(managers + index * 0x20 + 8);
        char owner[512];
        if (!manager || !read_definition_name(manager, owner)) continue;
        uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : std::string_view(owner)) hash = (hash ^ character) * 1099511628211ull;
        if (hash != scope) continue;
        for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
            const auto records = *reinterpret_cast<const uintptr_t*>(manager + offset);
            const auto size = *reinterpret_cast<const int32_t*>(manager + offset + 8);
            if (size < 0 || size > 1000000) return false;
            for (int32_t entry = 0; entry < size; ++entry)
                if (record_actor(records + static_cast<uintptr_t>(entry) * 0x68) == actor) return true;
        }
        return false;
    }
    return false;
}
bool special_admission(const SpawnLimits& limits) {
    __try {
        const auto world = *reinterpret_cast<uintptr_t*>(world_global);
        const auto collection = *reinterpret_cast<uintptr_t*>(world + 0x62088);
        const auto references = *reinterpret_cast<uintptr_t*>(collection);
        const auto count = *reinterpret_cast<int32_t*>(collection + 8);
        std::array<uint32_t, 9> active{};
        std::array<uint32_t, 9> species{};
        for (int32_t index = 0; index < count; ++index) {
            const auto actor = resolve_actor(references + index * 24);
            if (!actor || combat::confirmed_dead(world, actor)) continue;
            if (limits.scope && !population_member(world, actor, limits.scope)) continue;
            unsigned physical = 9;
            const auto kind = special_kind(actor, limits.strict ? &physical : nullptr);
            if (kind < active.size()) ++active[kind];
            if (limits.strict && limits.physical && physical < species.size()) ++species[physical];
        }
        for (size_t kind = 0; kind < active.size(); ++kind) {
            if (UINT32_MAX - active[kind] < reserved_special[kind]) { admission_diagnostic_reason = "reservation_counter_overflow"; return false; }
            active[kind] += reserved_special[kind];
            if (UINT32_MAX - species[kind] < reserved_species[kind]) { admission_diagnostic_reason = "species_counter_overflow"; return false; }
            species[kind] += reserved_species[kind];
        }
        return admission_observed(spawn_allowed(limits, active), "scope_population_limit") &&
            admission_observed(species_allowed(limits, species), "scope_species_limit");
    } __except(EXCEPTION_EXECUTE_HANDLER) { admission_diagnostic_reason = "population_read_fault"; return false; }
}
bool supply_admission(uintptr_t world) {
    const auto collection = *reinterpret_cast<uintptr_t*>(world + 0x62088);
    const auto references = *reinterpret_cast<uintptr_t*>(collection);
    const auto count = *reinterpret_cast<int32_t*>(collection + 8);
    const auto capacity = *reinterpret_cast<int32_t*>(collection + 12);
    const auto managers = *reinterpret_cast<uintptr_t*>(world + 0x126658);
    const auto manager_count = *reinterpret_cast<int32_t*>(world + 0x126660);
    int32_t reserved = 0;
    for (int32_t index = 0; index < manager_count; ++index) {
        const auto manager = *reinterpret_cast<uintptr_t*>(managers + index * 0x20 + 8);
        char owner[512];
        if (!manager || !read_definition_name(manager, owner) || !private_supply(owner, "_manager")) continue;
        if (*reinterpret_cast<int32_t*>(manager + 0xcc8) == 0) continue;
        bool present = false;
        for (int32_t entry = 0; entry < count && !present; ++entry) {
            const auto actor = resolve_actor(references + entry * 24);
            char name[512];
            present = actor && read_definition_name(actor, name) && private_supply(name, "_actors_ai__ap_v070_s00100") &&
                std::memcmp(owner, name, 35) == 0 && !combat::confirmed_dead(world, actor);
        }
        reserved += !present;
    }
    if (*reinterpret_cast<const uint8_t*>(collection + 0x13) && static_cast<int64_t>(count) + reserved >= capacity) return false;
    return reserved == 0 || native_admission(world + 0xafd98, 1 - reserved);
}
bool generated_admission(const SpawnLimits& limits) {
    admission_diagnostic_reason = "admission_unobserved";
    __try {
        const auto world = *reinterpret_cast<uintptr_t*>(world_global);
        const auto collection = *reinterpret_cast<uintptr_t*>(world + 0x62088);
        const auto count = *reinterpret_cast<int32_t*>(collection + 8);
        const auto capacity = *reinterpret_cast<int32_t*>(collection + 12);
        if (reserved_actors >= INT32_MAX ||
            (*reinterpret_cast<const uint8_t*>(collection + 0x13) &&
             static_cast<int64_t>(count) + reserved_actors >= capacity)) { admission_diagnostic_reason = "actor_capacity"; return false; }
        return admission_observed(supply_admission(world), "supply_reservation") &&
            admission_observed(!reserved_actors || native_admission(world + 0xafd98, 1 - static_cast<int32_t>(reserved_actors)), "native_reservation") &&
            ((!limits.population && limits.kind == 3) || special_admission(limits));
    } __except(EXCEPTION_EXECUTE_HANDLER) { admission_diagnostic_reason = "admission_read_fault"; return false; }
}
bool active_actor(uintptr_t actor) {
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto collection = *reinterpret_cast<const uintptr_t*>(world + 0x62088);
    const auto count = *reinterpret_cast<const int32_t*>(collection + 8);
    const auto records = *reinterpret_cast<const uintptr_t*>(collection);
    if (count < 0 || count > 1000000) { recipe_fault.store(true); return false; }
    for (int32_t index = 0; index < count; ++index)
        if (resolve_actor(records + static_cast<uintptr_t>(index) * 24) == actor) return true;
    return false;
}
void cancel_deferred_show(uintptr_t actor) {
    const auto pending = deferred_shows.find(actor);
    if (pending == deferred_shows.end()) return;
    const auto identifier = actor_identifier(actor);
    if (identifier == pending->second.identifier) cancel_event(actor, show_event);
    deferred_shows.erase(pending);
}
bool defer_show(uintptr_t actor) {
    char name[512];
    if (!read_instance_name(actor, name) || !*name) return false;
    const auto identifier = actor_identifier(actor);
    cancel_deferred_show(actor);
    deferred_shows[actor] = {identifier, name};
    if (post_event(actor, show_event, 50, 0, 0)) return true;
    deferred_shows.erase(actor); return false;
}
std::string independent_body_name(const NativeRecipe& recipe, uint64_t index) {
    if (!index) return recipe.definition.source;
    char suffix[16]; sprintf_s(suffix, "_%08x", static_cast<uint32_t>(index));
    return "ap_v070_independent_body_r" + recipe.definition.origin + suffix;
}
std::string empty_independent_key(std::string_view origin) {
    const auto base = "independent/" + std::string(origin);
    std::string result = base; uint64_t latest = 0;
    for (const auto& [key, progress] : recipe_state.requests()) {
        (void)progress;
        if (key.rfind(base + "/", 0) != 0) continue;
        uint64_t activation = 0;
        const auto parsed = std::from_chars(key.data() + base.size() + 1, key.data() + key.size(), activation);
        if (parsed.ec == std::errc{} && parsed.ptr == key.data() + key.size() && activation > latest) { result = key; latest = activation; }
    }
    return result;
}
void independent_pose(uintptr_t actor, const float* pose) {
    const auto physics = reinterpret_cast<uintptr_t(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(actor) + 0xe78))(actor);
    reinterpret_cast<void(*)(uintptr_t, uintptr_t, int32_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(physics) + 0x50))(physics, reinterpret_cast<uintptr_t>(pose), -1);
    reinterpret_cast<void(*)(uintptr_t, uintptr_t, int32_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(physics) + 0x58))(physics, reinterpret_cast<uintptr_t>(pose + 3), -1);
    std::memcpy(reinterpret_cast<void*>(actor + 0x74), pose, 12);
    reinterpret_cast<void(*)(uintptr_t, uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(actor) + 0xb8))(actor, reinterpret_cast<uintptr_t>(pose + 3));
}
bool prepare_independent_show_locked(uintptr_t actor, IndependentShow& show) {
    char origin[21], name[512], body[512];
    if (*reinterpret_cast<const int32_t*>(actor + 0x10) < 0) return true;
    if (!independent_origin(actor, origin)) return true;
    if (!load_native_recipes()) {
        if (!defer_show(actor)) recipe_fault.store(true);
        return false;
    }
    const auto found = native_recipes.find(origin);
    if (found == native_recipes.end() || found->second.definition.family != 'd' ||
        !read_instance_name(actor, name) || !read_recipe_definition(actor, body)) return false;
    const auto& recipe = found->second;
    const auto frozen = spawn_recipe(body);
    if (!frozen || frozen->body >= recipe.bodies.size() || independent_body_name(recipe, frozen->body) != name) return true;
    const auto key = "independent/" + recipe.definition.origin;
    const auto& entry = recipe.definition;
    if (!recipe_state.request(key, {entry.source, entry.origin, "", entry.logical_mask, 1, entry.physical_count,
            entry.maximum_emissions, entry.physical_count, 1})) return false;
    auto& creation = show.creation;
    creation.recipe = &recipe; creation.key = key; show.source = !frozen->body;
    independent_shows.insert(name);
    const auto& progress = recipe_state.requests().at(key);
    if (frozen->body < progress.emitted) {
        const auto id = key + "/" + std::to_string(frozen->body);
        const auto member = progress.actors.find(id);
        if (member == progress.actors.end()) { independent_shows.erase(name); return false; }
        if (member->second.native_name != name) { recipe_fault.store(true); return false; }
        const auto identifier = actor_identifier(actor);
        recipe_actors[actor] = id;
        recipe_actor_credits[actor] = {identifier, {1, entry.physical_count}, key, name, frozen->body};
        return true;
    }
    if (frozen->body != progress.emitted || progress.reserved) {
        if (!defer_show(actor)) recipe_fault.store(true);
        return false;
    }
    const auto emission = recipe_state.reserve(key);
    if (!emission) return false;
    creation.emission = *emission; creation.body = recipe.bodies[frozen->body];
    creation.limits = *spawn_limits(body); creation.creating = true;
    creation.previous = recipe_creation; recipe_creation = &creation; show.opened = true;
    initialize_physical_recipe(&creation, actor);
    return true;
}
bool prepare_independent_show(uintptr_t actor, IndependentShow& show) {
    try { return prepare_independent_show_locked(actor, show); }
    catch (...) { recipe_fault.store(true); return false; }
}
void finish_independent_show(uintptr_t actor, IndependentShow& show) noexcept {
    try {
        if (show.opened) {
            show.creation.actor = active_actor(actor) ? actor : 0;
            end_recipe_creation(show.creation); show.opened = false;
            if (!show.creation.actor && !defer_show(actor)) recipe_fault.store(true);
        }
        if (!show.source || !active_actor(actor)) return;
        const auto& recipe = *show.creation.recipe;
        const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
        for (uint64_t index = 1; index < recipe.bodies.size(); ++index) {
            const auto name = independent_body_name(recipe, index);
            const auto extra = find_entity(world, name.c_str());
            const auto& progress = recipe_state.requests().at(show.creation.key);
            if (index < progress.emitted && !progress.actors.count(show.creation.key + "/" + std::to_string(index))) continue;
            if (!extra || !active_actor(extra)) {
                if (index >= progress.emitted && !independent_shows.count(name)) {
                    if (!extra) {
                        auto& pose = pending_independent_shows[name];
                        std::memcpy(pose.data(), reinterpret_cast<const void*>(actor + 0x74), sizeof(pose));
                    } else {
                        independent_pose(extra, reinterpret_cast<const float*>(actor + 0x74));
                    }
                }
                independent_shows.insert(name);
                if (extra && !defer_show(extra)) recipe_fault.store(true);
            }
        }
    } catch (...) {
        if (show.opened) { end_recipe_creation(show.creation); show.opened = false; }
        recipe_fault.store(true);
    }
}
bool owns_show_admission(uintptr_t actor, const char* name) {
    if (!recipe_creation || !recipe_creation->reserved ||
        (recipe_creation->show_actor && recipe_creation->show_actor != actor)) return false;
    std::string selected;
    if (!native_text(recipe_creation->body, selected, 511) || selected != name) return false;
    recipe_creation->show_actor = actor; return true;
}
bool prepare_show(uintptr_t actor, SpawnLimits& limits, bool& reserved) {
    try {
        char name[512];
        if (stopped.load(std::memory_order_acquire) && deferred_shows.count(actor)) {
            cancel_deferred_show(actor); return false;
        }
        if (read_recipe_definition(actor, name, true)) {
            if (stopped.load()) return false;
            if (!load_native_recipes()) {
                if (!defer_show(actor)) recipe_fault.store(true);
                return false;
            }
            char origin[21], source[512];
            if (!independent_origin(actor, origin) || !read_instance_name(actor, source)) return false;
            const auto found = native_recipes.find(origin);
            if (found == native_recipes.end() || found->second.definition.family != 'd' ||
                found->second.definition.source != source || found->second.definition.physical_count) {
                recipe_fault.store(true); return false;
            }
            const auto& entry = found->second.definition;
            auto key = empty_independent_key(entry.origin);
            const auto previous = recipe_state.requests().find(key);
            const bool next = previous != recipe_state.requests().end() && previous->second.empty_completed && !independent_shows.count(source);
            if (next) {
                if (next_recipe_request == UINT64_MAX) { recipe_fault.store(true); return false; }
                key = "independent/" + entry.origin + "/" + std::to_string(++next_recipe_request);
            }
            if (!recipe_state.request(key, {entry.source, entry.origin, "", entry.logical_mask, 1, 0, 0})) {
                recipe_fault.store(true); return false;
            }
            if (next) {
                std::vector<RecipeMembership> members;
                if (!native_memberships(*reinterpret_cast<const uintptr_t*>(world_global), actor, members)) { recipe_fault.store(true); return false; }
                for (const auto& member : members)
                    if (!recipe_state.join_empty(key, member.owner, member.label)) { recipe_fault.store(true); return false; }
            }
            cancel_deferred_show(actor);
            independent_shows.insert(source);
            if (!recipe_state.requests().at(key).empty_completed && !recipe_state.complete_empty(key, true)) recipe_fault.store(true);
            return false;
        }
        if (stopped.load(std::memory_order_acquire) || active_show_actor == actor || !read_recipe_definition(actor, name)) return true;
        if (*reinterpret_cast<const int32_t*>(actor + 0x10) < 0) return true;
        if (active_actor(actor) || reinterpret_cast<bool(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
                *reinterpret_cast<const uintptr_t*>(actor) + 0x108))(actor)) {
            cancel_deferred_show(actor); return true;
        }
        const auto parsed = spawn_limits(name);
        if (!parsed) { recipe_fault.store(true); return false; }
        limits = *parsed;
        const bool owned = owns_show_admission(actor, name);
        if (!owned && (recipe_fault.load() || !generated_admission(limits))) {
            const auto diagnostic = actor_diagnostics.find(actor);
            trace_spawn_refusal(diagnostic == actor_diagnostics.end() ? 0 : diagnostic->second.manager_address, name,
                recipe_fault.load() ? "recipe_fault" : admission_diagnostic_reason);
            if (!defer_show(actor)) recipe_fault.store(true);
            return false;
        }
        cancel_deferred_show(actor);
        if (!owned) { reserve_admission(limits); reserved = true; }
        return true;
    } catch (...) { recipe_fault.store(true); return false; }
}
void invoke_show(uintptr_t actor, NativeUnary original, IndependentShow* independent) {
    const auto previous_actor = active_show_actor;
    bool reserved = false;
    SpawnLimits limits{};
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    [&]() -> void {
        __try {
            if (prepare_show(actor, limits, reserved) &&
                    (stopped.load() || active_show_actor == actor || prepare_independent_show(actor, *independent))) {
                active_show_actor = actor;
                original(actor);
            }
        } __finally {
            finish_independent_show(actor, *independent);
            active_show_actor = previous_actor;
            if (reserved) release_admission(limits);
            admission.unlock();
        }
    }();
}
void show_admitted(uintptr_t actor, NativeUnary original) {
    if (stopped.load(std::memory_order_acquire)) { original(actor); return; }
    IndependentShow independent;
    invoke_show(actor, original, &independent);
}
void show_detour(uintptr_t actor) { show_admitted(actor, original_show); }
void show_now_detour(uintptr_t actor) { show_admitted(actor, original_show_now); }
void hide_independent_bodies(uintptr_t actor) {
    char origin[21], name[512];
    if (!read_instance_name(actor, name)) return;
    independent_shows.erase(name);
    if (!independent_origin(actor, origin)) return;
    const auto recipe = native_recipes.find(origin);
    if (recipe == native_recipes.end() || recipe->second.definition.source != name) return;
    if (recipe->second.bodies.empty()) {
        const auto key = empty_independent_key(recipe->second.definition.origin);
        const auto found = recipe_state.requests().find(key);
        if (found != recipe_state.requests().end() && !found->second.empty_completed) recipe_state.cancel(key);
    }
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    for (uint64_t index = 1; index < recipe->second.bodies.size(); ++index) {
        const auto extra_name = independent_body_name(recipe->second, index);
        independent_shows.erase(extra_name);
        pending_independent_shows.erase(extra_name);
        const auto extra = find_entity(world, extra_name.c_str());
        if (!extra) continue;
        trace_actor(extra, "propagated_hide", actor);
        cancel_deferred_show(extra); original_hide(extra);
    }
}
void try_hide_independent_bodies(uintptr_t actor) noexcept {
    try { hide_independent_bodies(actor); } catch (...) { recipe_fault.store(true); }
}
void hide_detour(uintptr_t actor) {
    if (stopped.load(std::memory_order_acquire)) { original_hide(actor); return; }
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    [&]() -> void {
        __try {
            trace_actor(actor, active_show_actor == actor ? "hide_during_show" : "native_hide_cause_unobserved", caller);
            if (active_show_actor != actor) {
                cancel_deferred_show(actor);
                try_hide_independent_bodies(actor);
            }
            original_hide(actor);
        } __finally { admission.unlock(); }
    }();
}
bool prepare_traversal(uintptr_t context, TraversalEntry& entry) noexcept {
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(context + 0x940);
        char name[512];
        if (!read_recipe_definition(actor, name)) return false;
        entry.actor = actor; entry.identifier = actor_identifier(actor);
        const auto data = *reinterpret_cast<const uintptr_t*>(actor + 0x5960);
        const auto point = resolve_actor(data + 0xbc90);
        if (!point) { recipe_fault.store(true); return false; }
        const auto origin = reinterpret_cast<const float*(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(point) + 0x1b0))(point);
        const auto axis = reinterpret_cast<const float*(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(point) + 0x1b8))(point);
        const auto local = reinterpret_cast<const float*>(point + 0xbd8);
        for (unsigned index = 0; index < 3; ++index)
            entry.pose[index] = axis[index] * local[0] + axis[index + 3] * local[1] + axis[index + 6] * local[2] + origin[index];
        std::memcpy(entry.pose.data() + 3, axis, 36);
        for (const auto value : entry.pose) if (!std::isfinite(value)) { recipe_fault.store(true); return false; }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { recipe_fault.store(true); return false; }
}
bool animation_valid_detour(uintptr_t state, const char* message) {
    if (stopped.load(std::memory_order_acquire)) return original_animation_valid(state, message);
    const auto valid = original_animation_valid(state, message);
    constexpr char missing[] = "AI waiting at traversal it has no anim for";
    if (!valid && traversal_entry && !traversal_entry->missing && message &&
        !std::strncmp(message, missing, sizeof(missing) - 1) &&
        *reinterpret_cast<const uintptr_t*>(state + 0x20) == traversal_entry->actor &&
        actor_identifier(traversal_entry->actor) == traversal_entry->identifier) {
        auto& entry = *traversal_entry;
        entry.missing = true;
        reinterpret_cast<void(*)(uintptr_t, const float*)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(entry.actor) + 0x250))(entry.actor, entry.pose.data());
        reinterpret_cast<void(*)(uintptr_t, const float*)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(entry.actor) + 0x258))(entry.actor, entry.pose.data() + 3);
    }
    return valid;
}
void invoke_traversal(uintptr_t state, uintptr_t context, uintptr_t actions, uintptr_t frame, TraversalEntry* entry, std::unique_lock<std::recursive_mutex>& admission) {
    entry->previous = traversal_entry;
    __try {
        traversal_entry = entry;
        original_traversal_enter(state, context, actions, frame);
    } __finally { traversal_entry = entry->previous; admission.unlock(); }
}
void traversal_enter_detour(uintptr_t state, uintptr_t context, uintptr_t actions, uintptr_t frame) {
    if (stopped.load()) { original_traversal_enter(state, context, actions, frame); return; }
    TraversalEntry entry;
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    if (prepare_traversal(context, entry)) { invoke_traversal(state, context, actions, frame, &entry, admission); return; }
    admission.unlock();
    if (!entry.actor) original_traversal_enter(state, context, actions, frame);
}
void reserve_admission(const SpawnLimits& limits) {
    ++reserved_actors;
    if (limits.kind < reserved_special.size()) ++reserved_special[limits.kind];
    const auto species = special_species_index(limits.physical);
    if (species < reserved_species.size()) ++reserved_species[species];
}
void release_admission(const SpawnLimits& limits) {
    --reserved_actors;
    if (limits.kind < reserved_special.size()) --reserved_special[limits.kind];
    const auto species = special_species_index(limits.physical);
    if (species < reserved_species.size()) --reserved_species[species];
}
void retry_immediate(uintptr_t manager, const char* reason) {
    trace_spawn_refusal(manager, "", reason);
    rewind_timeline(manager + 0xe20, *reinterpret_cast<const int32_t*>(manager + 0xda0));
    *reinterpret_cast<uint8_t*>(manager + 0xdac) = 1;
}
bool begin_immediate(ImmediateRequest* request, uintptr_t manager, uintptr_t origin, std::unique_lock<std::recursive_mutex>& admission) {
    char name[512];
    if (stopped.load(std::memory_order_acquire) || !read_definition_name(origin, name)) return false;
    const auto limits = spawn_limits(name);
    if (!limits) return false;
    admission.lock();
    request->previous = immediate_request;
    request->manager = manager;
    request->origin = origin;
    request->limits = *limits;
    request->tracking = spawn_tracking(name).value_or(SpawnTracking{});
    request->reserved = generated_admission(*limits);
    if (!request->reserved) trace_spawn_refusal(manager, name, admission_diagnostic_reason);
    if (request->reserved) reserve_admission(*limits);
    immediate_request = request;
    return true;
}
void end_immediate(ImmediateRequest* request, std::unique_lock<std::recursive_mutex>& admission) {
    if (request->reserved) release_admission(request->limits);
    immediate_request = request->previous;
    admission.unlock();
}
uintptr_t __fastcall single_detour(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t point, uintptr_t label) {
    std::unique_lock<std::recursive_mutex> admission(admission_mutex, std::defer_lock);
    return [&]() -> uintptr_t {
        if (stopped.load(std::memory_order_acquire)) return original_single(manager, output, type, point, label);
        uintptr_t recipe_result;
        if (dispatch_recipe_immediate(manager, output, type, point, label, false, recipe_result)) return recipe_result;
        ImmediateRequest request{};
        if (!begin_immediate(&request, manager, point, admission)) return original_single(manager, output, type, point, label);
        uintptr_t result = output;
        __try {
            if (request.reserved) result = original_single(manager, output, type, point, label);
            if (!request.succeeded) retry_immediate(manager);
        } __finally { end_immediate(&request, admission); }
        return result;
    }();
}
uintptr_t __fastcall multiple_detour(uintptr_t manager, uintptr_t output, uint64_t type, int32_t count,
                                    uintptr_t group, uintptr_t label) {
    std::unique_lock<std::recursive_mutex> admission(admission_mutex, std::defer_lock);
    return [&]() -> uintptr_t {
        if (stopped.load(std::memory_order_acquire)) return original_multiple(manager, output, type, count, group, label);
        uintptr_t recipe_result;
        if (count == 1 && dispatch_recipe_immediate(manager, output, type, group, label, true, recipe_result)) return recipe_result;
        ImmediateRequest request{};
        // larger requests belong to the native worker, whose remaining count survives refusal.
        if (count != 1 || !begin_immediate(&request, manager, group, admission))
            return original_multiple(manager, output, type, count, group, label);
        uintptr_t result = output;
        __try {
            if (request.reserved) result = original_multiple(manager, output, type, count, group, label);
            if (!request.succeeded) retry_immediate(manager);
        } __finally { end_immediate(&request, admission); }
        return result;
    }();
}
bool owns_immediate_admission(uintptr_t manager, const SpawnLimits& limits, const SpawnTracking* tracking) {
    return immediate_request && immediate_request->reserved && immediate_request->manager == manager &&
        immediate_request->limits.kind == limits.kind && immediate_request->limits.maximum == limits.maximum &&
        (!immediate_request->tracking.source || (tracking && immediate_request->tracking.source == tracking->source &&
                                                immediate_request->tracking.physical == tracking->physical));
}
uintptr_t __fastcall spirit_detour(uintptr_t manager, uintptr_t output, uintptr_t point, uint64_t allowed_hosts,
                                  uintptr_t label, uint8_t flag) {
    std::unique_lock<std::recursive_mutex> admission(admission_mutex, std::defer_lock);
    return [&]() -> uintptr_t {
        if (stopped.load(std::memory_order_acquire)) return original_spirit(manager, output, point, allowed_hosts, label, flag);
        uintptr_t recipe_result;
        if (dispatch_recipe_immediate(manager, output, 0, point, label, false, recipe_result)) return recipe_result;
        char name[512];
        if (!read_definition_name(point, name) || !spawn_tracking(name))
            return original_spirit(manager, output, point, allowed_hosts, label, flag);
        ImmediateRequest request{};
        if (!begin_immediate(&request, manager, point, admission))
            return original_spirit(manager, output, point, allowed_hosts, label, flag);
        request.direct_factory = true;
        uintptr_t result = output;
        __try {
            if (request.reserved) result = original_spirit(manager, output, point, allowed_hosts, label, flag);
            if (!request.succeeded) retry_immediate(manager);
        } __finally { end_immediate(&request, admission); }
        return result;
    }();
}
uintptr_t archvile_detour(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t arch_template,
                           uintptr_t label, const char* group) {
    if (stopped.load(std::memory_order_acquire)) return original_archvile(manager, output, point, arch_template, label, group);
    uintptr_t result;
    if (dispatch_recipe_immediate(manager, output, 0, point, label, false, result)) return result;
    return original_archvile(manager, output, point, arch_template, label, group);
}
uintptr_t buff_detour(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t label, const char* group) {
    if (stopped.load(std::memory_order_acquire)) return original_buff(manager, output, point, label, group);
    uintptr_t result;
    if (dispatch_recipe_immediate(manager, output, 0, point, label, false, result)) return result;
    return original_buff(manager, output, point, label, group);
}
bool tracked_point_type(uintptr_t manager, uintptr_t point, uint64_t type) {
    if (recipe_creation && recipe_creation->override_active && !recipe_creation->creating &&
        recipe_creation->selecting_manager == manager && recipe_creation->active_point == point &&
        recipe_creation->source == point && type && !(type & ~encounter_ai_mask) &&
        *reinterpret_cast<const int32_t*>(point + 0xc40) == 1) return true;
    return immediate_request && immediate_request->direct_factory && immediate_request->reserved &&
        manager == immediate_request->manager && point == immediate_request->origin &&
        type == immediate_request->tracking.source && *reinterpret_cast<const int32_t*>(point + 0xc40) == 1;
}
bool recipe_library_point(uintptr_t point) {
    char name[512];
    if (!read_definition_name(point, name)) return false;
    const std::string_view value(name);
    if (recipe_creation && recipe_creation->source == point && recipe_creation->recipe->definition.family == 'd') return false;
    return value == "ap_v070_recipe_plan" || value.rfind("ap_v070_recipe_plan_", 0) == 0 ||
        value.rfind("ap_v070_bodies_r", 0) == 0 || value.rfind("ap_v070_compat_r", 0) == 0 ||
        value.rfind("ap_v070_counter_r", 0) == 0 || value.rfind("ap_v070_direct_r", 0) == 0 ||
        value.rfind("ap_v070_functional_", 0) == 0 || value.rfind("ap_v070_independent_", 0) == 0 ||
        value.rfind("ap_v070_landing_", 0) == 0;
}
uint64_t __fastcall point_candidate_detour(uintptr_t manager, uintptr_t point, uint64_t type) {
    if (stopped.load(std::memory_order_acquire)) return original_point_candidate(manager, point, type);
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return [&]() -> uint64_t {
        __try {
            if (recipe_library_point(point) || !recipe_arena_point(point)) return 0;
            const auto result = tracked_point_type(manager, point, type) ? 1 : original_point_candidate(manager, point, type);
            if (!result && recipe_creation && recipe_creation->source == point)
                trace_recipe_spawn(*recipe_creation, recipe_creation->body, point, 16);
            return result;
        } __finally { admission.unlock(); }
    }();
}
int32_t __fastcall point_index_detour(uintptr_t manager, uintptr_t point, uint64_t type) {
    if (stopped.load(std::memory_order_acquire)) return original_point_index(manager, point, type);
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return [&]() -> int32_t {
        __try {
            if (recipe_library_point(point) || !recipe_arena_point(point)) return -1;
            const auto result = tracked_point_type(manager, point, type) ? 0 : original_point_index(manager, point, type);
            if (result < 0 && recipe_creation && recipe_creation->source == point)
                trace_recipe_spawn(*recipe_creation, recipe_creation->body, point, 32);
            return result;
        } __finally { admission.unlock(); }
    }();
}
uintptr_t invoke_direct_creation(RecipeCreation* creation, uintptr_t point) {
    __try {
        if (!point_available_locked(point, 0, 1, 1, 1, 1)) return 0;
        return recipe_factory(point, 0);
    } __finally { end_recipe_creation(*creation); }
}
uintptr_t drain_direct_point(uintptr_t point) {
    const auto found = direct_points.find(point);
    if (found == direct_points.end() || pending_summon_checkpoint) return 0;
    auto& producer = found->second;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    uintptr_t first = 0;
    if (find_entity(world, producer.name.c_str()) != point || actor_identifier(point) != producer.identifier) {
        for (const auto& request : producer.requests) recipe_state.cancel(request.key);
        direct_points.erase(found); return 0;
    }
    for (auto request = producer.requests.begin(); request != producer.requests.end();) {
        const auto& progress = recipe_state.requests().at(request->key);
        const auto name = "ap_v070_direct_r" + request->origin;
        if (find_entity(world, name.c_str()) != point || actor_identifier(point) != producer.identifier) {
            for (const auto& pending : producer.requests) recipe_state.cancel(pending.key);
            direct_points.erase(found); return first;
        }
        while (!progress.closed && progress.emitted < progress.request.maximum_emissions) {
            if (find_entity(world, name.c_str()) != point || actor_identifier(point) != producer.identifier) {
                for (const auto& pending : producer.requests) recipe_state.cancel(pending.key);
                direct_points.erase(found); return first;
            }
            RecipeCreation creation;
            const auto previous = worker_recipe; worker_recipe = &request->key;
            bool ready = false;
            try { ready = begin_recipe_creation(creation, point, request->origin, ""); }
            catch (...) { worker_recipe = previous; throw; }
            worker_recipe = previous;
            if (!ready) break;
            const auto actor = invoke_direct_creation(&creation, point);
            if (!actor) break;
            if (!first) first = actor;
            auto& child = summon_children[creation.emission.actor];
            child.parent = name; child.body = *reinterpret_cast<const char**>(creation.body + 8);
        }
        if (!progress.closed && progress.emitted < progress.request.maximum_emissions) break;
        recipe_state.cancel(request->key); request = producer.requests.erase(request);
    }
    return first;
}
uintptr_t activate_direct_point(uintptr_t point, std::string_view origin) {
    if (!load_native_recipes()) { recipe_fault.store(true); return 0; }
    const auto recipe = native_recipes.find(std::string(origin));
    if (recipe == native_recipes.end() || recipe->second.definition.family != 'f' || next_recipe_request == UINT64_MAX) {
        recipe_fault.store(true); return 0;
    }
    const auto& definition = recipe->second.definition;
    const auto key = "direct/" + definition.origin + "/" + std::to_string(++next_recipe_request);
    const auto name = "ap_v070_direct_r" + definition.origin;
    if (!recipe_state.request(key, {definition.owner.empty() ? name : definition.owner, definition.origin, "", definition.logical_mask,
        definition.logical_count, definition.physical_count, definition.maximum_emissions, definition.maximum_emissions, definition.final_credit_units})) {
        recipe_fault.store(true); return 0;
    }
    auto& producer = direct_points[point];
    const auto identifier = actor_identifier(point);
    if (producer.parent && producer.identifier != identifier) {
        for (const auto& request : producer.requests) recipe_state.cancel(request.key);
        producer = {};
    }
    producer.parent = point; producer.identifier = identifier; producer.name = name;
    if (!definition.physical_count) { recipe_state.complete_empty(key); return 0; }
    SummonSource source;
    source.origin = definition.origin; source.mask = definition.logical_mask; source.template_name = name;
    producer.requests.push_back({key, definition.origin, {std::move(source)}, 0, false});
    return drain_direct_point(point);
}
uintptr_t point_factory_locked(uintptr_t point, uintptr_t group) {
    if (recipe_creation) return recipe_creation->creating ? without_recipe_override(0, 0, point, group, 0, 0) : recipe_factory(point, group);
    if (!stopped.load()) {
        char name[512];
        if (read_definition_name(point, name)) {
            const std::string_view value(name), prefix = "ap_v070_direct_r";
            if (value.size() == prefix.size() + 20 && value.substr(0, prefix.size()) == prefix &&
                value.substr(prefix.size()).find_first_not_of("0123456789abcdef") == value.npos)
                return activate_direct_point(point, value.substr(prefix.size()));
        }
    }
    const auto result = original_point_factory(point, group);
    if (result && immediate_request &&
        immediate_request->direct_factory && immediate_request->reserved && point == immediate_request->origin) {
        char name[512];
        if (read_definition_name(point, name)) {
            const auto tracking = spawn_tracking(name);
            if (tracking && tracking->source == immediate_request->tracking.source &&
                tracking->physical == immediate_request->tracking.physical) immediate_request->succeeded = true;
        }
    }
    return result;
}
uintptr_t __fastcall point_factory_detour(uintptr_t point, uintptr_t group) {
    if (stopped.load(std::memory_order_acquire)) return original_point_factory(point, group);
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return [&]() -> uintptr_t {
        __try { return point_factory_locked(point, group); }
        __finally { admission.unlock(); }
    }();
}
uint64_t create_tracked(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth,
                        uint64_t physical) {
    const auto original_type = *reinterpret_cast<uint64_t*>(record);
    const auto creation = recipe_point_type(point) ? recipe_creation : nullptr;
    uint64_t result = 0;
    __try {
        if (creation) {
            creation->original = read_point_state(point); creation->active_point = point;
            creation->selecting_manager = manager;
            creation->override_active = true;
            // native selection must see the body before it validates and stores the definition index.
            write_point_state(point, {creation->body, 1, 0, creation->original.animation, creation->original.state});
        }
        // frozen bodies can be absent from the manager's vanilla population.
        *reinterpret_cast<uint64_t*>(record) = (original_type & ~encounter_ai_mask) | (creation ? encounter_ai_mask : physical);
        result = original_spawn(manager, record, point, fourth, fifth);
    } __finally {
        if (creation) {
            write_point_state(point, creation->original); creation->override_active = false; creation->selecting_manager = 0;
        }
        *reinterpret_cast<uint64_t*>(record) = original_type;
    }
    return result;
}
uint64_t spawn_marked(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth,
                      const SpawnLimits* limits, const SpawnTracking* tracking) {
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return [&]() -> uint64_t {
        uint64_t result = 0;
        bool reserved = false;
        __try {
            const auto owned = owns_immediate_admission(manager, *limits, tracking);
            const auto immediate = immediate_request && immediate_request->manager == manager;
            if ((immediate && owned) || (!immediate && generated_admission(*limits))) {
                if (!owned) { reserve_admission(*limits); reserved = true; }
                result = tracking ? create_tracked(manager, record, point, fourth, fifth, tracking->physical) :
                    original_spawn(manager, record, point, fourth, fifth);
                if (owned && result) immediate_request->succeeded = true;
            } else trace_spawn_refusal(manager, "", immediate ? "immediate_admission_unowned" : admission_diagnostic_reason);
        } __finally {
            if (reserved) release_admission(*limits);
            admission.unlock();
        }
        return result;
    }();
}
uint64_t __fastcall spawn_detour(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth) {
    if (stopped.load(std::memory_order_acquire)) return original_spawn(manager, record, point, fourth, fifth);
    if (recipe_creation && !recipe_creation->creating) {
        const auto physical = recipe_point_type(point);
        return physical ? create_tracked(manager, record, point, fourth, fifth, physical) :
            original_spawn(manager, record, point, fourth, fifth);
    }
    if (!stopped.load(std::memory_order_acquire)) {
        uint64_t result;
        if (try_recipe_creation(manager, record, point, fourth, fifth, false, result)) return result;
        if (recipe_creation && recipe_creation->creating) return without_recipe_override(manager, record, point, fourth, fifth, 1);
        char name[512];
        if (read_definition_name(point, name)) {
            const auto limits = spawn_limits(name);
            if (limits) {
                const auto tracking = spawn_tracking(name);
                return spawn_marked(manager, record, point, fourth, fifth, &*limits, tracking ? &*tracking : nullptr);
            }
        }
    }
    return original_spawn(manager, record, point, fourth, fifth);
}

uint64_t __fastcall group_detour(uintptr_t manager, uint64_t* record, uintptr_t group) {
    if (!stopped.load(std::memory_order_acquire)) {
        uint64_t recipe_result;
        if ((!recipe_creation || recipe_creation->creating) &&
            try_recipe_creation(manager, reinterpret_cast<uintptr_t>(record), group, 0, 0, true, recipe_result))
            return recipe_result;
        if (recipe_creation && recipe_creation->creating)
            return without_recipe_override(manager, reinterpret_cast<uintptr_t>(record), group, 0, 0, 2);
        char name[512];
        if (read_definition_name(group, name)) {
            const auto tracking = spawn_tracking(name);
            if (tracking) {
                const auto source = *record;
                *record = (source & ~encounter_ai_mask) | tracking->physical;
                uint64_t result;
                __try { result = original_group(manager, record, group); }
                __finally { *record = source; }
                return result;
            }
        }
    }
    return original_group(manager, record, group);
}
void trace_registration(uintptr_t manager, uint64_t* record, uintptr_t actor, uint64_t incoming) noexcept {
    try {
        if (!actor || registration_trace_count >= 4096) return;
        char owner[512]{}, body[512]{}; std::string encounter;
        read_definition_name(manager, owner);
        read_recipe_definition(actor, body);
        native_text(reinterpret_cast<uintptr_t>(record) + 0x28, encounter, 191);
        const auto identifier = *reinterpret_cast<const uint32_t*>(reinterpret_cast<uintptr_t>(record) + 12);
        commands::diagnostic_trace("AP_ENEMY_REGISTER actor=%llx id=%u manager=%.128s encounter=%.128s body=%.128s input_mask=%llx source_mask=%llx",
            static_cast<unsigned long long>(actor), identifier, owner, encounter.c_str(), body,
            static_cast<unsigned long long>(incoming & encounter_ai_mask), static_cast<unsigned long long>(*record & encounter_ai_mask));
        ++registration_trace_count;
        if (actor_diagnostics.size() < 4096 || actor_diagnostics.count(actor)) {
            auto& diagnostic = actor_diagnostics[actor];
            if (diagnostic.identifier != identifier) diagnostic = {};
            diagnostic.identifier = identifier; diagnostic.manager_address = manager; diagnostic.manager = owner; diagnostic.encounter = encounter;
        }
    } catch (...) {}
}
void register_independent_copy(uintptr_t manager, uint64_t* copy, uintptr_t actor) {
    bool consumed = false;
    __try {
        assign_manager(reinterpret_cast<uintptr_t>(copy) + 8, actor);
        trace_registration(manager, copy, actor, *copy);
        original_register_spawn(manager, copy); consumed = true;
    } __finally {
        if (!consumed) assign_manager(reinterpret_cast<uintptr_t>(copy) + 8, 0);
    }
}
bool register_independent_extras(uintptr_t manager, const NativeRecipe& recipe,
                                 const std::array<uint64_t, 13>& source, const std::string& label) {
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    bool complete = true;
    for (uint64_t index = 1; index < recipe.bodies.size(); ++index) {
        const auto name = independent_body_name(recipe, index);
        const auto extra = find_entity(world, name.c_str());
        if (!extra) { complete = false; continue; }
        char definition[512];
        const auto frozen = read_recipe_definition(extra, definition) ? spawn_recipe(definition) : std::nullopt;
        if (!frozen || frozen->body != index || std::string(frozen->origin.data(), frozen->origin.size()) != recipe.definition.origin) {
            recipe_fault.store(true); return false;
        }
        bool present = false;
        for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
            const auto data = *reinterpret_cast<const uintptr_t*>(manager + offset);
            const auto count = *reinterpret_cast<const int32_t*>(manager + offset + 8);
            if (count < 0 || count > 1000000) { recipe_fault.store(true); return false; }
            for (int32_t i = 0; i < count; ++i) {
                const auto record = data + static_cast<uintptr_t>(i) * 0x68;
                if (record_actor(record) != extra || *reinterpret_cast<const uint32_t*>(record + 12) != actor_identifier(extra)) continue;
                std::string current;
                if (!native_text(record + 0x28, current, 1024)) { recipe_fault.store(true); return false; }
                present |= current == label;
            }
        }
        if (present) continue;
        auto copy = source;
        copy[0] = (copy[0] & ~encounter_ai_mask) | recipe.definition.logical_mask;
        std::fill(copy.begin() + 1, copy.begin() + 5, 0);
        copy[1] = uint64_t{0x1fffffe} << 32 | 0x1fffffe;
        copy[6] = reinterpret_cast<uintptr_t>(label.c_str());
        copy[7] = static_cast<uint32_t>(label.size()) | (uint64_t{label.size() + 1} << 32);
        register_independent_copy(manager, copy.data(), extra);
    }
    return complete;
}
void register_spawn_locked(uintptr_t manager, uint64_t* record) {
    const auto diagnostic_incoming_mask = *record;
    const auto actor = record_actor(reinterpret_cast<uintptr_t>(record));
    char origin[21], instance[512];
    const NativeRecipe* independent = nullptr;
    std::string label;
    std::array<uint64_t, 13> copy{};
    if (!stopped.load() && independent_origin(actor, origin) && read_instance_name(actor, instance) && load_native_recipes()) {
        const auto recipe = native_recipes.find(origin);
        if (recipe != native_recipes.end() && recipe->second.definition.source == instance) {
            independent = &recipe->second;
            if (!native_text(reinterpret_cast<uintptr_t>(record) + 0x28, label, 1024)) {
                recipe_fault.store(true); return;
            }
            std::memcpy(copy.data(), record, sizeof(copy));
            if (independent->bodies.empty()) {
                const auto& entry = independent->definition;
                const auto key = empty_independent_key(entry.origin);
                char owner[512];
                if (!read_definition_name(manager, owner) ||
                    !recipe_state.request(key, {entry.source, entry.origin, "", entry.logical_mask, 1, 0, 0}) ||
                    !recipe_state.join_empty(key, owner, label)) { recipe_fault.store(true); return; }
                *record = (*record & ~encounter_ai_mask) | entry.logical_mask;
            }
        }
    }
    char name[512];
    if (read_recipe_definition(actor, name) || read_definition_name(actor, name)) {
        const auto tracking = spawn_tracking(name);
        // an admitted creation can register after stop is requested.
        if (tracking) *record = (*record & ~encounter_ai_mask) | tracking->source;
    }
    const auto role = recipe_actors.find(actor);
    if (role != recipe_actors.end() || (recipe_creation && recipe_creation->creating && read_recipe_definition(actor, name))) {
        char owner[512]; std::string native_label;
        if (!read_definition_name(manager, owner) || !native_text(reinterpret_cast<uintptr_t>(record) + 0x28, native_label, 1024)) {
            recipe_fault.store(true); return;
        }
        if (role != recipe_actors.end()) recipe_state.join_actor(role->second, owner, native_label);
        else {
            const auto frozen = spawn_recipe(name);
            if (frozen && std::string(frozen->origin.data(), frozen->origin.size()) == recipe_creation->recipe->definition.origin) {
                RecipeMembership member{owner, std::move(native_label)};
                if (std::find(recipe_creation->memberships.begin(), recipe_creation->memberships.end(), member) == recipe_creation->memberships.end())
                    recipe_creation->memberships.push_back(std::move(member));
            }
        }
    }
    trace_registration(manager, record, actor, diagnostic_incoming_mask);
    original_register_spawn(manager, record);
    if (!independent) return;
    if (!register_independent_extras(manager, *independent, copy, label)) independent_memberships_pending = true;
}
void __fastcall register_spawn_detour(uintptr_t manager, uint64_t* record) {
    if (stopped.load(std::memory_order_acquire)) { original_register_spawn(manager, record); return; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    [&]() -> void {
        __try { register_spawn_locked(manager, record); }
        __finally { admission.unlock(); }
    }();
}
float __fastcall movement_detour(uintptr_t ai) {
    const auto value = original_movement(ai);
    return !stopped.load(std::memory_order_acquire) && active_ai == ai ? value * active_speed : value;
}
float __fastcall haste_detour(uintptr_t ai) {
    const auto value = original_haste(ai);
    return stopped.load(std::memory_order_acquire) ? value : scale_movement(ai, value);
}
float __fastcall controller_detour(uintptr_t controller, uintptr_t frame, uintptr_t output, float distance) {
    if (stopped.load(std::memory_order_acquire)) return original_controller(controller, frame, output, distance);
    const auto previous_ai = active_ai;
    const auto previous_speed = active_speed;
    active_ai = *reinterpret_cast<const uintptr_t*>(controller);
    active_speed = scale_movement(active_ai, 1.0f);
    // haste scales animation displacement; only the controller limits need the same factor.
    const auto started=CostClock::now();
    const auto result = original_controller(controller, frame, output, distance);
    motion_controllers.fetch_add(1,std::memory_order_relaxed);
    motion_native_us.fetch_add(elapsed_us(started),std::memory_order_relaxed);
    active_ai = previous_ai;
    active_speed = previous_speed;
    return result;
}
}
bool install(const engine::Binding& binding, HANDLE stop_event) {
    if (!save::session().campaign_run.enabled()) return true;
    if (binding.metadata.profile != SC_PROFILE_STEAM_20260818) return false;
    death_target_returns = {binding.image.base + 0xa1364a, binding.image.base + 0xa136f8, binding.image.base + 0xa137b4};
    const native::Target movement{binding.image.base + 0x7c7940,
        {0x48,0x8b,0x05,0x09,0xf6,0xe3,0x03,0x0f,0x57,0xc9,0xf3,0x0f,0x10,0x40,0x0c,0x0f,
         0x2f,0xc1,0x73,0x0f,0x48,0x8b,0x81,0x60,0x59,0x00,0x00,0xf3,0x0f,0x10,0x80,0xe8}};
    const native::Target haste{binding.image.base + 0x7c78f0,
        {0x48,0x8b,0x05,0xd9,0xf6,0xe3,0x03,0x0f,0x57,0xc9,0xf3,0x0f,0x10,0x40,0x0c,0x0f,
         0x2f,0xc1,0x73,0x1f,0x48,0x8b,0x81,0x60,0x59,0x00,0x00,0xf3,0x0f,0x10,0x80,0xf0}};
    const native::Target name{binding.image.base + 0x6e4e90,
        {0x48,0x8b,0x81,0xa8,0x00,0x00,0x00,0x48,0x85,0xc0,0x75,0x08,0x48,0x8d,0x05,0x1d,
         0x2b,0x37,0x02,0xc3,0x48,0x8b,0x40,0x08,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}};
    const native::Target controller{binding.image.base + 0x8c5cb0,
        {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x60,0x44,
         0x0f,0x29,0x44,0x24,0x30,0x48,0x8b,0xd9,0x48,0x8b,0x09,0x44,0x0f,0x28,0xc3,0xf3}};
    const native::Target hosts{binding.image.base + 0x974070,
        {0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20,0x0f,0xb6,0x41,0x13,0x48,
         0x8b,0xf9,0x84,0xc0,0x74,0x06,0x2c,0x03,0x3c,0x01,0x77,0x29,0x48,0x8b,0x11,0x48}};
    const native::Target spawn{binding.image.base + 0xbba950,
        {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,
         0x24,0x48,0xff,0xff,0xff,0x48,0x81,0xec,0xb8,0x01,0x00,0x00,0x48,0x8b,0x05,0x65}};
    const native::Target group{binding.image.base + 0xbb9c50,
        {0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55,
         0x56,0x41,0x54,0x41,0x56,0x48,0x8d,0x6c,0x24,0xb8,0x48,0x81,0xec,0x48,0x01,0x00}};
    const native::Target registered{binding.image.base + 0xb94f40,
        {0x4c,0x8b,0xdc,0x55,0x53,0x56,0x57,0x49,0x8d,0x6b,0xa1,0x48,0x81,0xec,0xd8,0x00,
         0x00,0x00,0x48,0x8b,0x05,0x7f,0x9a,0x61,0x03,0x48,0x33,0xc4,0x48,0x89,0x45,0x1f}};
    const native::Target single{binding.image.base + 0xba27f0,
        {0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x8d,0x6c,0x24,0xf8,0x48,
         0x81,0xec,0x08,0x01,0x00,0x00,0x48,0x8b,0x05,0xcb,0xc1,0x60,0x03,0x48,0x33,0xc4}};
    const native::Target multiple{binding.image.base + 0xba0780,
        {0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,
         0xf0,0x48,0x81,0xec,0x10,0x01,0x00,0x00,0x48,0x8b,0x05,0x39,0xe2,0x60,0x03,0x48}};
    const native::Target spirit{binding.image.base + 0xba2b90,
        {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
         0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x48,0x8b,0xfa,0x48,0x8b,0x0d,0xbf,0x47,0xa5}};
    const native::Target point_factory{binding.image.base + 0xd87de0,
        {0x40,0x55,0x56,0x57,0x48,0x8d,0xac,0x24,0xa0,0xfe,0xff,0xff,0x48,0x81,0xec,0x60,
         0x02,0x00,0x00,0x48,0x8b,0x05,0xde,0x6b,0x42,0x03,0x48,0x33,0xc4,0x48,0x89,0x85}};
    const native::Target point_candidate{binding.image.base + 0xb99ec0,
        {0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,
         0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x81,0xe8,0x10,0x00,0x00}};
    const native::Target point_index{binding.image.base + 0xba7130,
        {0x4c,0x8b,0xdc,0x56,0x57,0x41,0x57,0x48,0x81,0xec,0xf0,0x00,0x00,0x00,0x41,0x0f,
         0x29,0x7b,0xa8,0x48,0x8b,0x05,0x8e,0x78,0x60,0x03,0x48,0x33,0xc4,0x48,0x89,0x84}};
    const native::Target rewind{binding.image.base + 0xab7200,
        {0x3b,0x51,0x38,0x7d,0x04,0xc6,0x41,0x78,0x01,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,
         0x3b,0x51,0x38,0x7d,0x0e,0x48,0x8b,0x41,0x30,0x48,0x63,0xd2,0xc7,0x04,0x90,0x00}};
    const native::Target admission{binding.image.base + 0xb99c80,
        {0x89,0x54,0x24,0x10,0x53,0x55,0x56,0x57,0x48,0x81,0xec,0x58,0x01,0x00,0x00,0x48,
         0x8b,0xd9,0x48,0x8d,0x94,0x24,0x80,0x01,0x00,0x00,0x48,0x8b,0x0d,0xcf,0xd6,0xa5}};
    const native::Target remaining{binding.image.base + 0xba84e0,
        {0x40,0x53,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20,0x4c,0x63,0xb9,0xb0,0x00,0x00,
         0x00,0x4c,0x8b,0xf1,0x44,0x3b,0xb9,0xc0,0x00,0x00,0x00,0x0f,0x8e,0x02,0x01,0x00}};
    const native::Target remaining_added{binding.image.base + 0xbae8a0,
        {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xec,0x90,0x00,
         0x00,0x00,0x48,0x8b,0x02,0x48,0x8b,0xda,0x48,0x89,0x44,0x24,0x20,0x48,0x8b,0xf1}};
    const native::Target remaining_dead{binding.image.base + 0xbae530,
        {0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x48,0x8b,0x42,0x08,0x4c,0x8b,0xf1,0x48,0x8d,
         0x4a,0x08,0x4c,0x8b,0xea,0x48,0x83,0xc2,0x0c,0x3b,0x02,0x75,0x1b,0x3d,0xfe,0xff},
        0x40,
        {0x10,0x00,0x0f,0x84,0x0c,0x01,0x00,0x00,0x48,0x89,0x6c,0x24,0x40,0x41,0x8b,0xae,
         0xb0,0x00,0x00,0x00,0x83,0xed,0x01,0x0f,0x88,0xf2,0x00,0x00,0x00,0x48,0x89,0x5c}};
    const native::Target entity_reference{binding.image.base + 0x633550,
        {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x83,0x3d,0x12,0x3e,0xfc,0x03,0x00,0x48,0x8b,
         0xd9,0x74,0x62,0x48,0x8b,0xca,0xe8,0x25,0x50,0x68,0x00,0x48,0x8b,0xc8,0xe8,0x2d}};
    const native::Target manager_reference{binding.image.base + 0x6ac6b0,
        {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x83,0x3d,0xb2,0xac,0xf4,0x03,0x00,0x48,0x8b,
         0xd9,0x74,0x62,0x48,0x8b,0xca,0xe8,0xc5,0xbe,0x60,0x00,0x48,0x8b,0xc8,0xe8,0xed}};
    const native::Target ignored{binding.image.base + 0x6f52b0,
        {0x48,0x8b,0x81,0x60,0x59,0x00,0x00,0x83,0xb8,0x90,0x29,0x01,0x00,0x01,0x0f,0x94,
         0xc0,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}};
    const native::Target lookup{binding.image.base + 0x6ca290,
        {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41,
         0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x4c,0x8b,0xf2,0x48,0x8b,0x0d,0xee,0x70}};
    constexpr uint32_t recipe_rvas[]{0xbbe1a0, 0xbbea50, 0xb95850, 0xb92240, 0x26ec090, 0x5260a0,
        0xb925f0, 0xba8be0, 0xbae6a0, 0x20a0620, 0x210fd00, 0x3fb340, 0xba8610, 0xbae960, 0xbae000,
        0xba0bc0, 0xba19a0, 0xbbbc50, 0xba2080, 0xb92670, 0x682350, 0x6822f0, 0x17fae20,
        0x997d40, 0x998aa0, 0x999fc0, 0x996a10, 0x81dfd0, 0x17a3380, 0x17aa5d0, 0x3facb0, 0xba79b0, 0x3faff0, 0xbbc570, 0x996680, 0xd85490,
        0xa13210, 0x17dd730, 0x993090, 0x99dc60, 0x6fb2f0, 0x6fb5b0, 0x6f40b0, 0x17dd390, 0x17dce30,
        0xa20f30, 0x8f9320, 0xb21ea0, 0x9960a0, 0x3fa8e0, 0x917950};
    constexpr const char* recipe_prefixes[]{
        "4c8bdc554157498d6ba14881ecf8000000488b0520085f034833c44889450780",
        "4c8bdc554156498d6ba14881ece8000000488b0570ff5e034833c44889450780",
        "48895c2418574883ec2083b9900d000000488bfa488bd90f8ee1000000488b81",
        "48895c240848896c241848897424205741544155415641574883ec20498bf949",
        "48895c2408574883ec20488bf9eb1c488b05b212ac014885c0741d488bcfff15",
        "48895c2410574883ec20488bd9488bfa488b4908483bd1750b8b430439030f84",
        "48895c24105556574883ec20498bf9498be88bda488bf1e814feffff488d05cd",
        "41574881ec600100008b81ac0000004c8bf93981a80000007c0cb0014881c460",
        "48895c24184889742420574881ec90000000488bda488bf14883c20c8b43083b",
        "40534883ec20488bd94885c97427488b01ff50104885c0741c8b40683b05e6cf",
        "40534883ec20488bd94885c97427488b01ff50104885c0741c8b40683b05c67f",
        "4883ec08483bca750732c04883c408c34c8b41084c8b4a0848891c24450fbe10",
        "405355565741554883ec208b81b00000004c8be94c897c2460448d78ff4585ff",
        "4889742420574881eca0000000488bfa4533c08b91b0000000488bf185d27e24",
        "405556574883ec30488bea0f297424204883c20c0f28f2488bf18b45083b0275",
        "4055415441564157488d6c24884881ec78010000488b05fddd60034833c44889",
        "40555357415441554156488d6c24d94881ecf8000000488b051bd060034833c4",
        "4055535657415441554157488d6c24e04881ec20010000488b056a2d5f034833",
        "405556574154415541564157488d6c24f04881ec10010000488b0539c9600348",
        "48895c2408488974241048897c241841564883ec20498bf1498bf8488bda4c8b",
        "488b052939f7034c8b491083780800750b498b4118488902488bc2c34963c048",
        "4883ec28488b058539f7034c8bc183780800750a4c8b41104d8b4008eb2380b9",
        "488b0519653f040f57c0f3480f2a4018f30f59c1f3480f2cc0488901488bc1c3",
        "405556488dac2458ddffffb8a8230000e84b40ed01482be0488b05796c810348",
        "488bc4555356574154415541564157488da868f9ffff4881ec580700000f2970",
        "48895c2408574883ec20488bda488bf9e88b889cff83f801740b488bd3488bcf",
        "40554155488dac2498feffff4881ec68020000488b05ae7f81034833c4488985",
        "405557488dac2428ecffffb8d8140000e8bbdd0402482be0488b05e909990348",
        "488d056901f102c3cccccccccccccccc48895c241848896c2420565741564881",
        "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448",
        "40534883ec20488b5108488d05b7c76602488901488bd94885d2742d8b4114c1",
        "48895c240848896c2410488974241848897c24204154415641574883ec204c8b",
        "48895c2410488974241848897c242041564883ec304c8bf2488bd94885d20f85",
        "c681c40d000000c3cccccccccccccccc48896c2420564883ec20488b05dfada3",
        "4c8bdc5355565741564881ec90000000488b05418381034833c448894424784d",
        "488bc448895818488970205557415441564157488d68b14881ecd00000000f29",
        "40555356574155488d6c24c94881ec90000000488b05aeb779034833c4488945",
        "4883ec384c894c242041b901000000e84cfcffff4883c438c3cccccccccccccc",
        "4c8bc90fb6d183e2010fb6c1c0e8024803d24c8bc24983c80824010fb6c14c0f",
        "4055535741554156488dac2440fcffff4881ecc0040000488b055a0d81034833",
        "40574883ec20f681a202000004488bf90f855f01000048895c2438488b996059",
        "40574883ec70488b051b34ab034833c44889442450488b0554b7b703488bf94c",
        "48896c2420574883ec20488ba960590000488bf980bd312f0100007415c7852c",
        "44894c24205356574883ec40488b01418bd8488bfa488bf1ff504084c00f84a6",
        "48895c2408574883ec20488bf9488bda488b0df10e00034885c97434488b01ff",
        "405553565741564157488dac24b8baffffb848460000e855aee401482be0488b",
        "4056574881ec98000000488b05a7568b034833c448898424800000004c8b0148",
        "40555356574154415541564157488dac2428ffffff4881ecd8010000488b0515",
        "4056415641574883ec404c63fa4c8bf185d27f380fb6411333f684c074062c03",
        "488d0591cb6602c7411414000080488901488d411848894108c7411000000000",
        "4055488dac2480f0ffffb880100000e83c44f501482be0488b056a7089034833"};
    native::Target recipe_targets[std::size(recipe_rvas)]{};
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    for (size_t i = 0; i < std::size(recipe_targets); ++i) {
        auto& target = recipe_targets[i]; target.address = binding.image.base + recipe_rvas[i];
        for (size_t byte = 0; byte < target.bytes.size(); ++byte) {
            unsigned value = 0;
            if (sscanf_s(recipe_prefixes[i] + byte * 2, "%2x", &value) != 1) return false;
            target.bytes[byte] = static_cast<uint8_t>(value);
        }
        if (i == 5) {
            target.signature_offset = 0x20;
            target.signature = {0x92,0x00,0x00,0x00,0x48,0x85,0xc9,0x74,0x21,0xe8,0xd2,0xa5,0xb7,0x01,0x48,0x85,
                0xc0,0x74,0x0b,0x48,0x8b,0xd3,0x48,0x8b,0xc8,0xe8,0x02,0x74,0x6b,0x00,0x48,0x8b};
        }
        if (i == 31) {
            target.signature_offset = 0x20;
            target.signature = {0xf2,0x48,0x8b,0xe9,0xb2,0x01,0x48,0x81,0xc1,0x98,0x0c,0x00,0x00,0x33,0xff,0xe8,
                0xfc,0xd3,0x83,0xff,0x84,0xc0,0x44,0x8b,0xff,0xb9,0x01,0x00,0x00,0x00,0x44,0x0f};
        }
        if (i == 20 || i == 22 || i == 28 || i == 33 || i == 38 || i == 49) {
            if (native::validate_leaf_target(memory, binding.image, target, i == 49 ? 39 : i == 38 ? 774 : i == 20 ? 47 : i == 28 || i == 33 ? 8 : 32, stop_event, deadline)) return false;
        } else if (native::validate_target(memory, binding.image, target, stop_event, deadline)) return false;
    }
    if (native::validate_leaf_target(memory, binding.image, movement, 36, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, haste, 52, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, name, 25, stop_event, deadline) ||
        native::validate_target(memory, binding.image, controller, stop_event, deadline) ||
        native::validate_target(memory, binding.image, hosts, stop_event, deadline) ||
        native::validate_target(memory, binding.image, spawn, stop_event, deadline) ||
        native::validate_target(memory, binding.image, group, stop_event, deadline) ||
        native::validate_target(memory, binding.image, registered, stop_event, deadline) ||
        native::validate_target(memory, binding.image, single, stop_event, deadline) ||
        native::validate_target(memory, binding.image, multiple, stop_event, deadline) ||
        native::validate_target(memory, binding.image, spirit, stop_event, deadline) ||
        native::validate_target(memory, binding.image, point_factory, stop_event, deadline) ||
        native::validate_target(memory, binding.image, point_candidate, stop_event, deadline) ||
        native::validate_target(memory, binding.image, point_index, stop_event, deadline) ||
        native::validate_target(memory, binding.image, admission, stop_event, deadline) ||
        native::validate_target(memory, binding.image, remaining, stop_event, deadline) ||
        native::validate_target(memory, binding.image, remaining_added, stop_event, deadline) ||
        native::validate_target(memory, binding.image, remaining_dead, stop_event, deadline) ||
        native::validate_target(memory, binding.image, entity_reference, stop_event, deadline) ||
        native::validate_target(memory, binding.image, manager_reference, stop_event, deadline) ||
        native::validate_target(memory, binding.image, lookup, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, ignored, 18, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, rewind, 10, stop_event, deadline)) return false;
    definition_name = reinterpret_cast<DefinitionName>(name.address);
    resolve_actor = reinterpret_cast<ResolveActor>(binding.image.base + 0x6cb100);
    grow_hosts = reinterpret_cast<GrowHosts>(binding.image.base + 0x357040);
    native_admission = reinterpret_cast<NativeAdmission>(admission.address);
    rewind_timeline = reinterpret_cast<RewindTimeline>(rewind.address);
    refresh_entity = reinterpret_cast<RefreshEntity>(entity_reference.address);
    refresh_manager = reinterpret_cast<RefreshEntity>(manager_reference.address);
    ignored_actor = reinterpret_cast<IgnoredActor>(ignored.address);
    find_entity = reinterpret_cast<FindEntity>(lookup.address);
    construct_stagger = reinterpret_cast<ConstructStagger>(recipe_targets[3].address);
    allocate_worker = reinterpret_cast<AllocateWorker>(recipe_targets[4].address);
    assign_manager = reinterpret_cast<AssignManager>(recipe_targets[5].address);
    cast_maintain = reinterpret_cast<WorkerCast>(recipe_targets[9].address);
    cast_stagger = reinterpret_cast<WorkerCast>(recipe_targets[10].address);
    compare_text = reinterpret_cast<CompareText>(recipe_targets[11].address);
    original_spirit_helper = reinterpret_cast<SpiritHelper>(recipe_targets[17].address);
    construct_time = reinterpret_cast<ConstructTime>(recipe_targets[19].address);
    native_clock = reinterpret_cast<NativeClock>(recipe_targets[20].address);
    native_step = reinterpret_cast<NativeStep>(recipe_targets[21].address);
    native_duration = reinterpret_cast<NativeDuration>(recipe_targets[22].address);
    definition_database = reinterpret_cast<DefinitionDatabase>(recipe_targets[28].address);
    lookup_definition = reinterpret_cast<LookupDefinition>(recipe_targets[29].address);
    destroy_native_text = reinterpret_cast<NativeUnary>(recipe_targets[30].address);
    manager_contains = reinterpret_cast<ManagerContains>(recipe_targets[31].address);
    monster_mask = reinterpret_cast<MonsterMask>(recipe_targets[38].address);
    summon_label_symbol = binding.image.base + 0x463c8d8;
    activate_event = binding.image.base + 0x6c1cf70;
    show_event = binding.image.base + 0x6c1dc90;
    post_event = reinterpret_cast<PostEvent>(recipe_targets[43].address);
    cancel_event = reinterpret_cast<CancelEvent>(recipe_targets[44].address);
    grow_summon_records = reinterpret_cast<GrowSummonRecords>(recipe_targets[48].address);
    construct_native_string = reinterpret_cast<ConstructNativeString>(recipe_targets[49].address);
    animation_path_table = binding.image.base + 0x2a9a1f8;
    darklord_image = binding.image.base;

    world_global = binding.image.base + 0x45f7370;
    void* sites[] = {reinterpret_cast<void*>(movement.address), reinterpret_cast<void*>(haste.address),
                     reinterpret_cast<void*>(controller.address),
                     reinterpret_cast<void*>(hosts.address), reinterpret_cast<void*>(spawn.address),
                     reinterpret_cast<void*>(group.address), reinterpret_cast<void*>(registered.address),
                     reinterpret_cast<void*>(single.address), reinterpret_cast<void*>(multiple.address),
                     reinterpret_cast<void*>(spirit.address), reinterpret_cast<void*>(point_factory.address),
                     reinterpret_cast<void*>(point_candidate.address), reinterpret_cast<void*>(point_index.address),
                     reinterpret_cast<void*>(remaining.address), reinterpret_cast<void*>(remaining_added.address),
                     reinterpret_cast<void*>(remaining_dead.address), reinterpret_cast<void*>(recipe_targets[0].address),
                     reinterpret_cast<void*>(recipe_targets[1].address), reinterpret_cast<void*>(recipe_targets[2].address),
                     reinterpret_cast<void*>(recipe_targets[6].address), reinterpret_cast<void*>(recipe_targets[7].address),
                     reinterpret_cast<void*>(recipe_targets[8].address), reinterpret_cast<void*>(recipe_targets[12].address),
                     reinterpret_cast<void*>(recipe_targets[13].address), reinterpret_cast<void*>(recipe_targets[14].address),
                     reinterpret_cast<void*>(recipe_targets[15].address), reinterpret_cast<void*>(recipe_targets[16].address),
                     reinterpret_cast<void*>(recipe_targets[18].address), reinterpret_cast<void*>(recipe_targets[23].address),
                     reinterpret_cast<void*>(recipe_targets[24].address), reinterpret_cast<void*>(recipe_targets[25].address),
                     reinterpret_cast<void*>(recipe_targets[26].address), reinterpret_cast<void*>(recipe_targets[27].address),
                     reinterpret_cast<void*>(recipe_targets[33].address), reinterpret_cast<void*>(recipe_targets[34].address),
                     reinterpret_cast<void*>(recipe_targets[35].address), reinterpret_cast<void*>(recipe_targets[36].address),
                     reinterpret_cast<void*>(recipe_targets[37].address), reinterpret_cast<void*>(recipe_targets[32].address),
                     reinterpret_cast<void*>(recipe_targets[39].address), reinterpret_cast<void*>(recipe_targets[40].address),
                     reinterpret_cast<void*>(recipe_targets[41].address), reinterpret_cast<void*>(recipe_targets[42].address),
                     reinterpret_cast<void*>(recipe_targets[45].address), reinterpret_cast<void*>(recipe_targets[46].address),
                     reinterpret_cast<void*>(recipe_targets[47].address), reinterpret_cast<void*>(recipe_targets[50].address)};
    void* detours[] = {reinterpret_cast<void*>(movement_detour), reinterpret_cast<void*>(haste_detour),
                       reinterpret_cast<void*>(controller_detour),
                       reinterpret_cast<void*>(occupied_hosts_detour), reinterpret_cast<void*>(spawn_detour),
                       reinterpret_cast<void*>(group_detour), reinterpret_cast<void*>(register_spawn_detour),
                       reinterpret_cast<void*>(single_detour), reinterpret_cast<void*>(multiple_detour),
                       reinterpret_cast<void*>(spirit_detour), reinterpret_cast<void*>(point_factory_detour),
                       reinterpret_cast<void*>(point_candidate_detour), reinterpret_cast<void*>(point_index_detour),
                       reinterpret_cast<void*>(remaining_detour), reinterpret_cast<void*>(remaining_added_detour),
                       reinterpret_cast<void*>(remaining_dead_detour), reinterpret_cast<void*>(maintain_tick_detour),
                       reinterpret_cast<void*>(stagger_tick_detour), reinterpret_cast<void*>(queue_command_detour),
                       reinterpret_cast<void*>(kill_construct_detour), reinterpret_cast<void*>(kill_detour),
                       reinterpret_cast<void*>(kill_dead_detour), reinterpret_cast<void*>(health_detour),
                       reinterpret_cast<void*>(health_added_detour), reinterpret_cast<void*>(health_damage_detour),
                       reinterpret_cast<void*>(archvile_detour), reinterpret_cast<void*>(buff_detour),
                       reinterpret_cast<void*>(possessed_detour), reinterpret_cast<void*>(summon_start_detour),
                       reinterpret_cast<void*>(summon_create_detour), reinterpret_cast<void*>(summon_update_detour),
                       reinterpret_cast<void*>(summon_complete_detour), reinterpret_cast<void*>(actor_factory_detour),
                       reinterpret_cast<void*>(summon_request_complete_detour), reinterpret_cast<void*>(summon_cleanup_detour),
                       reinterpret_cast<void*>(point_available_detour), reinterpret_cast<void*>(death_actions_detour),
                       reinterpret_cast<void*>(post_one_detour), reinterpret_cast<void*>(role_text_detour),
                       reinterpret_cast<void*>(samuel_setup_detour), reinterpret_cast<void*>(show_detour),
                       reinterpret_cast<void*>(show_now_detour), reinterpret_cast<void*>(hide_detour),
                       reinterpret_cast<void*>(traversal_enter_detour), reinterpret_cast<void*>(animation_valid_detour),
                       reinterpret_cast<void*>(resurrect_detour), reinterpret_cast<void*>(darklord_summon_detour)};
    void** originals[] = {reinterpret_cast<void**>(&original_movement), reinterpret_cast<void**>(&original_haste),
                           reinterpret_cast<void**>(&original_controller),
                           reinterpret_cast<void**>(&original_hosts), reinterpret_cast<void**>(&original_spawn),
                           reinterpret_cast<void**>(&original_group), reinterpret_cast<void**>(&original_register_spawn),
                           reinterpret_cast<void**>(&original_single), reinterpret_cast<void**>(&original_multiple),
                           reinterpret_cast<void**>(&original_spirit), reinterpret_cast<void**>(&original_point_factory),
                           reinterpret_cast<void**>(&original_point_candidate), reinterpret_cast<void**>(&original_point_index),
                           reinterpret_cast<void**>(&original_remaining), reinterpret_cast<void**>(&original_remaining_added),
                           reinterpret_cast<void**>(&original_remaining_dead), reinterpret_cast<void**>(&original_maintain_tick),
                           reinterpret_cast<void**>(&original_stagger_tick), reinterpret_cast<void**>(&original_queue_command),
                           reinterpret_cast<void**>(&original_kill_construct), reinterpret_cast<void**>(&original_kill),
                           reinterpret_cast<void**>(&original_kill_dead), reinterpret_cast<void**>(&original_health),
                           reinterpret_cast<void**>(&original_health_added), reinterpret_cast<void**>(&original_health_damage),
                           reinterpret_cast<void**>(&original_archvile), reinterpret_cast<void**>(&original_buff),
                           reinterpret_cast<void**>(&original_possessed), reinterpret_cast<void**>(&original_summon_start),
                           reinterpret_cast<void**>(&original_summon_create), reinterpret_cast<void**>(&original_summon_update),
                           reinterpret_cast<void**>(&original_summon_complete), reinterpret_cast<void**>(&original_actor_factory),
                           reinterpret_cast<void**>(&original_summon_request_complete), reinterpret_cast<void**>(&original_summon_cleanup),
                           reinterpret_cast<void**>(&original_point_available), reinterpret_cast<void**>(&original_death_actions),
                           reinterpret_cast<void**>(&original_post_one), reinterpret_cast<void**>(&assign_native_text),
                           reinterpret_cast<void**>(&original_samuel_setup), reinterpret_cast<void**>(&original_show),
                           reinterpret_cast<void**>(&original_show_now), reinterpret_cast<void**>(&original_hide),
                           reinterpret_cast<void**>(&original_traversal_enter), reinterpret_cast<void**>(&original_animation_valid),
                           reinterpret_cast<void**>(&original_resurrect_factory), reinterpret_cast<void**>(&original_darklord_summon)};
    size_t created = 0;
    for (; created < std::size(sites); ++created)
        if (MH_CreateHook(sites[created], detours[created], originals[created]) != MH_OK) break;
    if (created != std::size(sites)) {
        for (size_t i = 0; i < created; ++i) MH_RemoveHook(sites[i]);
        return false;
    }
    for (size_t i = 0; i < created; ++i) {
        if (MH_EnableHook(sites[i]) == MH_OK) continue;
        // Retain reachable trampolines until process exit.
        for (auto site : sites) MH_DisableHook(site);
        return false;
    }
    if (!install_recipe_checkpoint(binding, stop_event,
            {lock_recipe_checkpoint, unlock_recipe_checkpoint, capture_recipe_checkpoint, restore_recipe_checkpoint})) {
        for (auto site : sites) MH_DisableHook(site);
        return false;
    }
    stopped.store(false, std::memory_order_release);
    return true;
}
void stop() { stopped.store(true, std::memory_order_release); stop_recipe_checkpoint(); }
bool append_recipe_membership(std::map<std::string, std::vector<RecipeMembership>>& memberships,
                               const std::string& actor, const char* owner, uintptr_t label) {
    std::string text;
    if (!native_text(label, text, 1024)) return false;
    auto& values = memberships[actor];
    RecipeMembership value{owner, std::move(text)};
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(std::move(value));
    return true;
}
bool synchronize_independent_memberships(uintptr_t world) {
    struct Registration { uintptr_t manager; const NativeRecipe* recipe; std::array<uint64_t, 13> record; std::string label; };
    std::vector<Registration> registrations;
    bool observed=false;
    const auto managers = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
    const auto count = *reinterpret_cast<const int32_t*>(world + 0x126660);
    if (count < 0 || count > 100000) return false;
    for (int32_t i = 0; i < count; ++i) {
        const auto manager = *reinterpret_cast<const uintptr_t*>(managers + static_cast<uintptr_t>(i) * 0x20 + 8);
        if (!manager) continue;
        for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
            const auto data = *reinterpret_cast<const uintptr_t*>(manager + offset);
            const auto length = *reinterpret_cast<const int32_t*>(manager + offset + 8);
            if (length < 0 || length > 1000000) return false;
            for (int32_t index = 0; index < length; ++index) {
                const auto record = data + static_cast<uintptr_t>(index) * 0x68;
                const auto actor = record_actor(record);
                char origin[21], name[512];
                if (!actor || !independent_origin(actor, origin) || !read_instance_name(actor, name)) continue;
                const auto recipe = native_recipes.find(origin);
                if (recipe == native_recipes.end() || recipe->second.definition.source != name) continue;
                observed=true;
                Registration pending{manager, &recipe->second, {}, {}};
                if (!native_text(record + 0x28, pending.label, 1024)) return false;
                if (recipe->second.bodies.empty()) {
                    char owner[512];
                    const auto& entry = recipe->second.definition;
                    const auto key = empty_independent_key(entry.origin);
                    if (!read_definition_name(manager, owner) ||
                        !recipe_state.request(key, {entry.source, entry.origin, "", entry.logical_mask, 1, 0, 0}) ||
                        !recipe_state.join_empty(key, owner, pending.label)) return false;
                    continue;
                }
                std::memcpy(pending.record.data(), reinterpret_cast<const void*>(record), sizeof(pending.record));
                registrations.push_back(std::move(pending));
            }
        }
    }
    bool complete = observed || !independent_memberships_pending;
    for (const auto& pending : registrations)
        if (!register_independent_extras(pending.manager, *pending.recipe, pending.record, pending.label)) complete = false;
    independent_memberships_pending = !complete;
    return !recipe_fault.load();
}
bool synchronize_independent_memberships_safe(uintptr_t world) noexcept {
    __try { return synchronize_independent_memberships(world); }
    __except(EXCEPTION_EXECUTE_HANDLER) { recipe_fault.store(true); return false; }
}
void reconcile_recipe_memberships(const std::string& actor, const std::vector<RecipeMembership>& memberships) {
    const auto separator = actor.rfind('/');
    const auto request = recipe_state.requests().find(actor.substr(0, separator));
    if (request == recipe_state.requests().end()) return;
    const auto found = request->second.actors.find(actor);
    if (found == request->second.actors.end()) return;
    const auto previous = found->second.memberships;
    for (const auto& member : memberships) recipe_state.join_actor(actor, member.owner, member.label);
    for (const auto& member : previous)
        if (std::find(memberships.begin(), memberships.end(), member) == memberships.end())
            recipe_state.detach_actor(actor, member.owner, member.label);
}
bool read_recipe_actors(uintptr_t world, std::map<uintptr_t, bool>& actors,
                        std::map<std::string, std::vector<RecipeMembership>>& memberships,
                        std::set<uintptr_t>& workers, std::set<uintptr_t>& owners, uint64_t* living=nullptr) {
    __try {
        if (living) *living=0;
        const auto collection = *reinterpret_cast<const uintptr_t*>(world + 0x62088);
        const auto data = *reinterpret_cast<const uintptr_t*>(collection);
        const auto count = *reinterpret_cast<const int32_t*>(collection + 8);
        if (count < 0 || count > 1000000) return false;
        for (int32_t i = 0; i < count; ++i) {
            const auto actor = resolve_actor(data + static_cast<uintptr_t>(i) * 24);
            if (actor) current_recipe_actor(actor);
            const bool tracked=actor && (!component_summons.empty() || recipe_actors.find(actor) != recipe_actors.end() ||
                recipe_actor_credits.find(actor) != recipe_actor_credits.end());
            if (actor && (tracked || living)) {
                const auto dead=combat::confirmed_dead(world, actor);
                if (living && !dead) ++*living;
                if (tracked) actors[actor]=dead;
            }
        }
        // hidden actors keep their native identity outside the active AI list.
        for (const auto& [actor, credit] : recipe_actor_credits) {
            if (actors.find(actor) != actors.end() || credit.name.empty()) continue;
            const auto present = find_entity(world, credit.name.c_str());
            if (present != actor) continue;
            const auto identifier = actor_identifier(present);
            if (identifier == credit.identifier) actors[actor] = combat::confirmed_dead(world, actor);
        }
        const auto managers = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
        const auto manager_count = *reinterpret_cast<const int32_t*>(world + 0x126660);
        if (manager_count < 0 || manager_count > 100000) return false;
        for (int32_t i = 0; i < manager_count; ++i) {
            const auto manager = *reinterpret_cast<const uintptr_t*>(managers + static_cast<uintptr_t>(i) * 0x20 + 8);
            char owner[512];
            if (!manager || !read_definition_name(manager, owner)) continue;
            owners.insert(manager);
            const auto commands = *reinterpret_cast<const uintptr_t*>(manager + 0xcc0);
            const auto command_count = *reinterpret_cast<const int32_t*>(manager + 0xcc8);
            if (command_count < 0 || command_count > 100000) return false;
            for (int32_t index = 0; index < command_count; ++index)
                workers.insert(*reinterpret_cast<const uintptr_t*>(commands + static_cast<uintptr_t>(index) * 8));
            for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
                const auto records = *reinterpret_cast<const uintptr_t*>(manager + offset);
                const auto length = *reinterpret_cast<const int32_t*>(manager + offset + 8);
                if (length < 0 || length > 1000000) return false;
                for (int32_t index = 0; index < length; ++index) {
                    const auto record = records + static_cast<uintptr_t>(index) * 0x68;
                    const auto actor = record_actor(record);
                    const auto role = recipe_actors.find(actor);
                    if (role != recipe_actors.end() && !append_recipe_membership(memberships, role->second, owner, record + 0x28)) return false;
                }
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
namespace {
struct RecoveryScan {
    uint32_t candidates = 0;
    uint64_t fingerprint = 14695981039346656037ull;
    char encounter[512]{};
    uint8_t completion = 0;
};
bool scan_recovery(uintptr_t world, RecoveryScan& scan) noexcept {
    __try {
        const auto managers = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
        const auto count = *reinterpret_cast<const int32_t*>(world + 0x126660);
        if (count < 0 || count > 100000) return false;
        for (int32_t i = 0; i < count; ++i) {
            const auto manager = *reinterpret_cast<const uintptr_t*>(managers + static_cast<uintptr_t>(i) * 0x20 + 8);
            if (!manager) continue;
            bool live = false;
            for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
                const auto records = *reinterpret_cast<const uintptr_t*>(manager + offset);
                const auto length = *reinterpret_cast<const int32_t*>(manager + offset + 8);
                if (length < 0 || length > 1000000) return false;
                for (int32_t index = 0; index < length; ++index) {
                    const auto actor = resolve_actor(records + static_cast<uintptr_t>(index) * 0x68 + 8);
                    if (actor && !combat::confirmed_dead(world, actor)) { live = true; break; }
                }
            }
            if (!live) continue;
            ++scan.candidates;
            scan.fingerprint = (scan.fingerprint ^ manager) * 1099511628211ull;
            const auto workers = *reinterpret_cast<const uintptr_t*>(manager + 0xcc0);
            const auto length = *reinterpret_cast<const int32_t*>(manager + 0xcc8);
            if (length < 0 || length > 100000) return false;
            for (int32_t index = 0; index < length; ++index)
                scan.fingerprint = (scan.fingerprint ^ *reinterpret_cast<const uintptr_t*>(workers + static_cast<uintptr_t>(index) * 8)) * 1099511628211ull;
            scan.completion = *reinterpret_cast<const uint8_t*>(manager + 0xdb3);
            scan.fingerprint = (scan.fingerprint ^ scan.completion) * 1099511628211ull;
            if (scan.candidates == 1 && !read_definition_name(manager, scan.encounter)) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
recovery::Context recovery_context(uintptr_t world) {
    recovery::Context context;
    RecoveryScan scan{};
    context.readable = !stopped.load() && resolve_actor && definition_name && scan_recovery(world, scan);
    context.candidates = scan.candidates; context.fingerprint = scan.fingerprint;
    context.encounter = scan.candidates == 1 ? scan.encounter : scan.candidates ? "ambiguous" : "none";
    context.completion = scan.candidates == 1 ? (scan.completion ? "latch_set_unqualified" : "latch_clear_unqualified") : "unqualified";
    // actor membership does not establish which encounter contains the player.
    context.ownership_proven = false;
    return context;
}
void observe(uintptr_t world) {
    if (stopped.load(std::memory_order_acquire)) return;
    ObserveCost cost; cost.world=world;
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    cost.wait=elapsed_us(cost.start);
    ++darklord_frame;
    darklord_sample(world, GetTickCount64());
    if (restored_independent && !load_native_recipes()) return;
    try {
        if (!restore_autonomous_summons(world)) { recipe_fault.store(true); return; }
        if (pending_summon_checkpoint) return;
        std::vector<uintptr_t> points;
        for (const auto& [point, producer] : direct_points) { (void)producer; points.push_back(point); }
        for (const auto point : points) drain_direct_point(point);
    } catch (...) { recipe_fault.store(true); return; }
    if (independent_memberships_pending) {
        const auto start=CostClock::now();
        try { if (!synchronize_independent_memberships_safe(world)) recipe_fault.store(true); }
        catch (...) { recipe_fault.store(true); }
        cost.sync=elapsed_us(start);
        if (recipe_fault.load()) return;
    }
    if (recipe_actors.empty() && worker_recipes.empty() && pending_recipes.empty() && component_summons.empty() &&
        recipe_actor_credits.empty() && deferred_shows.empty() && pending_independent_shows.empty()) return;
    try {
        for (auto pending = pending_independent_shows.begin(); pending != pending_independent_shows.end();) {
            const auto actor = find_entity(world, pending->first.c_str());
            char origin[21];
            if (!actor) { ++pending; continue; }
            if (!independent_origin(actor, origin)) { recipe_fault.store(true); return; }
            const auto recipe = native_recipes.find(origin);
            if (recipe == native_recipes.end()) { recipe_fault.store(true); return; }
            if (recipe->second.bodies.empty()) {
                independent_shows.insert(pending->first);
                pending = pending_independent_shows.erase(pending);
                continue;
            }
            independent_pose(actor, pending->second.data());
            independent_shows.insert(pending->first);
            if (!defer_show(actor)) { recipe_fault.store(true); return; }
            pending = pending_independent_shows.erase(pending);
        }
        for (auto pending = deferred_shows.begin(); pending != deferred_shows.end();) {
            const auto actor = find_entity(world, pending->second.name.c_str());
            const auto identifier = actor ? actor_identifier(actor) : 0;
            if (actor == pending->first && identifier == pending->second.identifier) ++pending;
            else pending = deferred_shows.erase(pending);
        }
        std::map<uintptr_t, bool> actors;
        std::map<std::string, std::vector<RecipeMembership>> memberships;
        std::set<uintptr_t> workers, owners;
        const std::vector<RecipeMembership> none;
        const auto scan_start=CostClock::now();
        const auto scanned=read_recipe_actors(world, actors, memberships, workers, owners,&cost.living);
        cost.scan=elapsed_us(scan_start);
        if (!scanned) { recipe_fault.store(true); return; }
        const auto now = GetTickCount64();
        if (now >= actor_diagnostic_next) {
            actor_diagnostic_next = now + 250;
            for (const auto& [actor, dead] : actors) if (!dead) trace_actor(actor, "large_jump", 0, true);
        }
        for (auto credit = recipe_actor_credits.begin(); credit != recipe_actor_credits.end();) {
            if (actors.find(credit->first) != actors.end()) { ++credit; continue; }
            trace_actor(credit->first, "absent_from_observation_cause_unobserved");
            actor_diagnostics.erase(credit->first);
            credit = recipe_actor_credits.erase(credit);
        }
        for (auto worker = worker_recipes.begin(); worker != worker_recipes.end();) {
            if (workers.find(worker->first) != workers.end()) { ++worker; continue; }
            const auto diagnostic = worker_diagnostics.find(worker->first);
            if (diagnostic != worker_diagnostics.end()) trace_recipe_worker("worker_removed", worker->first, diagnostic->second, worker->second);
            recipe_state.cancel(worker->second);
            if (diagnostic != worker_diagnostics.end()) {
                trace_recipe_worker("request_cancelled", worker->first, diagnostic->second, worker->second);
                worker_diagnostics.erase(diagnostic);
            }
            worker = worker_recipes.erase(worker);
        }
        for (auto pending = pending_recipes.begin(); pending != pending_recipes.end();) {
            if (owners.find(pending->first.first) != owners.end()) { ++pending; continue; }
            recipe_state.cancel(pending->second); pending = pending_recipes.erase(pending);
        }
        for (auto pending = component_summons.begin(); pending != component_summons.end();) {
            const auto parent = actors.find(pending->second.parent);
            if (parent != actors.end() && !parent->second && actor_identifier(pending->second.parent) == pending->second.identifier) {
                ++pending; continue;
            }
            for (const auto& request : pending->second.requests) recipe_state.cancel(request.key);
            complete_summon_requests(pending->second);
            pending = component_summons.erase(pending);
        }
        for (auto actor = recipe_actors.begin(); actor != recipe_actors.end();) {
            const auto present = actors.find(actor->first);
            const auto found = memberships.find(actor->second);
            if (found != memberships.end()) reconcile_recipe_memberships(actor->second, found->second);
            if (present == actors.end() || present->second) {
                trace_actor(actor->first, present == actors.end() ? "absent_from_observation_cause_unobserved" : "dead_observed");
                actor_diagnostics.erase(actor->first);
                recipe_state.actor_changed(actor->second, present == actors.end() ? RecipeActorState::removed : RecipeActorState::dead);
                if (present == actors.end()) recipe_actor_credits.erase(actor->first);
                actor = recipe_actors.erase(actor); continue;
            }
            if (found == memberships.end()) reconcile_recipe_memberships(actor->second, none);
            ++actor;
        }
    } catch (...) { recipe_fault.store(true, std::memory_order_release); }
}
void map_freed() {
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    std::lock_guard<std::recursive_mutex> lock(quantity_mutex);
    darklord_clear();
    remaining_weights.clear();
    kill_progress.clear();
    health_progress.clear();
    recipe_library = {}; native_recipes.clear(); pending_recipes.clear(); worker_recipes.clear(); recipe_actors.clear();
    component_summons.clear(); direct_points.clear(); summon_children.clear(); pending_summon_checkpoint.reset();
    deferred_shows.clear();
    independent_shows.clear();
    pending_independent_shows.clear();
    independent_memberships_pending = false;
    recipe_actor_credits.clear();
    restored_actor_identities.clear();
    death_action_credits.clear();
    point_credits.clear();
    recipe_spawn_traces.clear();
    worker_diagnostics.clear(); worker_trace_count = 0;
    actor_diagnostics.clear(); actor_diagnostic_next = 0; registration_trace_count = 0;
    quantity_wait_traces.clear();
    remaining_wait_started.clear();
    recipe_state = {}; next_recipe_request = 0; native_string_table = 0; recipe_fault.store(false);
}
#ifdef SC_NATIVE_TESTING
#include <memory>
#include "../tests/expanded_stop.inc"
bool darklord_fault_call(uintptr_t component) {
    __try { darklord_summon_detour(component); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode() == 0xe0700003; }
    return false;
}
bool test_darklord_summon_scope() {
    std::array<uintptr_t, 4> slots{0, 11, 0, 22};
    std::array<uint8_t, 0x1f0> declaration{};
    std::array<uintptr_t, 3> component{0, 0, reinterpret_cast<uintptr_t>(declaration.data())};
    *reinterpret_cast<uintptr_t*>(declaration.data() + 0x1e0) = reinterpret_cast<uintptr_t>(slots.data());
    *reinterpret_cast<int32_t*>(declaration.data() + 0x1e8) = 2;
    const auto previous = original_darklord_summon;
    const auto previous_stopped = stopped.load();
    const auto previous_fault = recipe_fault.exchange(false);
    static bool observed = false, fault = false;
    static unsigned calls = 0;
    calls = 0;
    original_darklord_summon = [](uintptr_t pointer) {
        const auto decl = *reinterpret_cast<const uintptr_t*>(pointer + 0x10);
        const auto values = *reinterpret_cast<const uintptr_t**>(decl + 0x1e0);
        observed = values[1] == 11 && values[3] == 22;
        ++calls;
        if (fault) RaiseException(0xe0700003, 0, 0, nullptr);
    };
    stopped.store(false);
    darklord_summon_detour(reinterpret_cast<uintptr_t>(component.data()));
    bool ok = observed && calls == 1 && !recipe_fault.load() && slots == std::array<uintptr_t, 4>{0, 11, 0, 22};
    stopped.store(true); observed = false; fault = true;
    ok &= darklord_fault_call(reinterpret_cast<uintptr_t>(component.data())) &&
        observed && calls == 2 && slots == std::array<uintptr_t, 4>{0, 11, 0, 22};
    stopped.store(true); fault = false; observed = false;
    darklord_summon_detour(reinterpret_cast<uintptr_t>(component.data()));
    ok &= observed && calls == 3 && slots == std::array<uintptr_t, 4>{0, 11, 0, 22};
    stopped.store(previous_stopped);
    recipe_fault.store(previous_fault);
    fault = false; original_darklord_summon = previous;
    return ok;
}
bool test_darklord_telemetry() {
    static std::vector<std::string> lines;
    static std::array<uintptr_t, 2> actors{};
    static std::array<uint32_t, 3> handles{9, 111, 222};
    std::vector<uint8_t> world(0x9a100), component(0x400);
    std::array<std::vector<uint8_t>, 2> actor_bytes{std::vector<uint8_t>(0x16830), std::vector<uint8_t>(0x16830)};
    std::array<std::vector<uint8_t>, 2> animator_bytes{std::vector<uint8_t>(0xa300), std::vector<uint8_t>(0xa300)};
    std::array<uintptr_t, 44> knight{}, pain{};
    knight[1] = reinterpret_cast<uintptr_t>("ai/heavy/hellknight");
    pain[1] = reinterpret_cast<uintptr_t>("ai/boss/darklord/summoned/painelemental");
    std::array<uintptr_t, 4> slots{0, reinterpret_cast<uintptr_t>(knight.data()), 0, reinterpret_cast<uintptr_t>(pain.data())};
    std::vector<uint8_t> declaration(0x1f0);
    *reinterpret_cast<uintptr_t*>(declaration.data() + 0x1e0) = reinterpret_cast<uintptr_t>(slots.data());
    *reinterpret_cast<int32_t*>(declaration.data() + 0x1e8) = 2;
    std::array<uintptr_t, 0x100 / 8> world_table{}, actor_table{}, animator_table{};
    world_table[0x88 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t, uint32_t* handle) -> uintptr_t {
        return *handle == 111 ? actors[0] : *handle == 222 ? actors[1] : uintptr_t{1};
    });
    actor_table[0x60 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t) { return "fixture_actor"; });
    const auto previous_image = darklord_image, previous_world = world_global;
    const auto previous_sink = darklord_sink;
    const auto previous_clock = native_clock;
    const auto previous_original = original_darklord_summon;
    auto previous_calls = std::move(darklord_calls);
    darklord_calls.clear(); lines.clear();
    darklord_sink = +[](const char* line, bool) { lines.emplace_back(line); };
    darklord_image = world_table[0x88 / 8] - 0x6998e0;
    animator_table[0x98 / 8] = darklord_image + 0x4947b0;
    uintptr_t world_address = reinterpret_cast<uintptr_t>(world.data());
    world_global = reinterpret_cast<uintptr_t>(&world_address);
    *reinterpret_cast<uintptr_t*>(world.data()) = reinterpret_cast<uintptr_t>(world_table.data());
    *reinterpret_cast<uintptr_t*>(world.data() + 0x9a068) = reinterpret_cast<uintptr_t>("game/dlc2/e5m4_boss/e5m4_boss");
    *reinterpret_cast<uint32_t*>(world.data() + 0x9a038) = 0x1234;
    const auto address = reinterpret_cast<uintptr_t>(component.data());
    *reinterpret_cast<uintptr_t*>(address + 8) = 77;
    *reinterpret_cast<uintptr_t*>(address + 0x10) = reinterpret_cast<uintptr_t>(declaration.data());
    *reinterpret_cast<uintptr_t*>(address + 0x3d8) = reinterpret_cast<uintptr_t>(handles.data());
    *reinterpret_cast<int32_t*>(address + 0x3e0) = 1;
    for (size_t i = 0; i < actors.size(); ++i) {
        actors[i] = reinterpret_cast<uintptr_t>(actor_bytes[i].data());
        const auto actor = actors[i], animator = reinterpret_cast<uintptr_t>(animator_bytes[i].data());
        *reinterpret_cast<uintptr_t*>(actor) = reinterpret_cast<uintptr_t>(actor_table.data());
        *reinterpret_cast<int32_t*>(actor + 0x10) = static_cast<int32_t>(i + 7);
        *reinterpret_cast<uintptr_t*>(actor + 0xa8) = reinterpret_cast<uintptr_t>(i ? pain.data() : knight.data());
        *reinterpret_cast<uintptr_t*>(actor + 0x5960) = animator;
        *reinterpret_cast<float*>(actor + 0x65a4) = 100;
        *reinterpret_cast<uintptr_t*>(animator + 0x9890) = reinterpret_cast<uintptr_t>(animator_table.data());
        *reinterpret_cast<uint8_t*>(animator + 0x9890 + 0x370) = 1;
        *reinterpret_cast<uint16_t*>(animator + 0x9890 + 0x5ac) = 7;
        *reinterpret_cast<int32_t*>(animator + 0x9890 + 0x5ec) = 1;
        *reinterpret_cast<uintptr_t*>(world.data() + 0x1af8 + (i + 7) * 8) = actor;
        *reinterpret_cast<uint32_t*>(world.data() + 0x21af8 + (i + 7) * 4) = handles[i + 1];
    }
    native_clock = +[](uintptr_t, uint64_t* value, int32_t) -> uint64_t* { *value = 123456; return value; };
    original_darklord_summon = +[](uintptr_t pointer) { *reinterpret_cast<int32_t*>(pointer + 0x3e0) = 3; };
    const auto previous_stopped = stopped.exchange(false);
    darklord_summon_detour(address);
    stopped.store(previous_stopped);
    bool ok = darklord_calls.size() == 1;
    if (ok) {
        auto& call = *darklord_calls.begin()->second;
        ok &= slots[1] == reinterpret_cast<uintptr_t>(knight.data()) && call.actors.size() == 2;
        const auto now = call.actors.empty() ? GetTickCount64() : call.actors[0].created;
        for (auto& actor : call.actors) actor.created = actor.sampled = now;
        const auto before = lines.size();
        darklord_sample(world_address, now + 250); darklord_sample(world_address, now + 500);
        ok &= lines.size() == before;
        *reinterpret_cast<float*>(actors[0] + 0x74) = 1;
        *reinterpret_cast<uint16_t*>(animator_bytes[0].data() + 0x9890 + 0x5ac) = 8;
        for (const auto time : {1000, 2000, 3000, 5000, 8000, 12000}) darklord_sample(world_address, now + time);
        ok &= darklord_calls.empty();
    }
    auto broken = darklord_begin(1);
    if (broken) {
        darklord_slots(broken.get(), 1);
        darklord_call_start(broken.get()); darklord_end(broken.get(), false, 1);
        darklord_track(*broken, 333);
        broken->lines = 1023;
        darklord_emit(*broken, "sample", "unused=1"); darklord_emit(*broken, "sample", "unused=2");
    } else ok = false;
    const auto contains = [&](const char* a, const char* b) {
        return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
            return line.find(a) != line.npos && line.find(b) != line.npos;
        });
    };
    ok &= contains("event=begin", "rng_before=0x1234 count_before=1") &&
        contains("event=end", "count_before=1 count_after=3 original_us=") &&
        contains("event=slot", "slot=0 decl=ai/heavy/hellknight inherit=null") &&
        contains("event=slot", "slot=1 decl=ai/boss/darklord/summoned/painelemental") &&
        contains("event=identity", "slot=0 entity_name=fixture_actor decl=ai/heavy/hellknight inherit=null") &&
        contains("event=actor", "anim_read=ok anim_enabled=1") &&
        contains("event=sample", "recovered_at_ms=1000") &&
        contains("event=summary", "frozen_ms=12000 recovered_at_ms=-1 samples=8") &&
        contains("event=actor", "read_fault=1") && contains("event=read_fault", "field=handle_diff") &&
        contains("event=limit", "reason=invocation_line_cap");
    for (const auto& line : lines) ok &= line.rfind("AP_DARKLORD ", 0) == 0 && line.find(" seq=") != line.npos &&
        line.find(" at_ms=") != line.npos && line.find(" game_tick=") != line.npos && line.find(" map=e5m4_boss ") != line.npos;
    Snapshot identity; identity.pid = GetCurrentProcessId(); identity.process_created = 123456;
    startup_log::record(identity, 0);
    for (const auto& line : lines) ok &= startup_log::darklord(line.c_str()) == ERROR_SUCCESS;
    darklord_clear(); darklord_calls = std::move(previous_calls);
    original_darklord_summon = previous_original; native_clock = previous_clock;
    darklord_image = previous_image; world_global = previous_world; darklord_sink = previous_sink;
    return ok;
}
bool test_recipe_transport() {
    static uintptr_t root, library;
    static std::vector<std::array<uint8_t, 0xbe0 + sizeof(save::NativeString)>> chunks;
    static bool missing = false;
    const auto previous_world = world_global;
    const auto previous_find = find_entity;
    const auto previous_name = definition_name;
    auto world = std::make_unique<std::array<uint8_t, 0x126680>>();
    uintptr_t address = reinterpret_cast<uintptr_t>(world->data());
    world_global = reinterpret_cast<uintptr_t>(&address);
    std::array<uint8_t, 0xc50> root_storage{}, library_storage{};
    root = reinterpret_cast<uintptr_t>(root_storage.data());
    library = reinterpret_cast<uintptr_t>(library_storage.data());
    std::string metadata = "EAP1|" + std::string(64, 'a') + "|game/test\n";
    for (unsigned i = 0; i < 414; ++i) {
        char origin[21]; sprintf_s(origin, "%020x", i);
        metadata += std::string(origin) + "|i|" + std::string(128, 's') + std::to_string(i) +
            "|arena|0000000000000010|00000001|00000000|00000000|00000000\n";
    }
    std::vector<std::string> texts;
    for (size_t at = 0; at < metadata.size(); at += 1800) texts.push_back(metadata.substr(at, 1800));
    chunks.resize(texts.size());
    for (size_t i = 0; i < texts.size(); ++i)
        *reinterpret_cast<save::NativeString*>(chunks[i].data() + 0xbe0) = recipe_text(texts[i]);
    char header[41];
    sprintf_s(header, "EAPC1|%08x|%08x|%016llx", static_cast<unsigned>(texts.size()),
        static_cast<unsigned>(metadata.size()), functional_token(metadata));
    std::string transport = header;
    const auto publish = [&] { *reinterpret_cast<save::NativeString*>(root + 0xbe0) = recipe_text(transport); };
    publish();
    find_entity = [](uintptr_t, const char* name) -> uintptr_t {
        const std::string_view value(name);
        if (value == "ap_v070_recipe_plan") return root;
        if (value.rfind("ap_v070_bodies_r", 0) == 0) return library;
        if (value.rfind("ap_v070_recipe_plan_", 0) != 0) return 0;
        size_t index = 0; const auto at = value.find_last_of('_') + 1;
        const auto parsed = std::from_chars(value.data() + at, value.data() + value.size(), index);
        return !missing && parsed.ec == std::errc{} && index < chunks.size() ?
            reinterpret_cast<uintptr_t>(chunks[index].data()) : 0;
    };
    definition_name = [](uintptr_t) { return "ap_v070_recipe_plan_0"; };
    map_freed(); session_recipe_plan.clear();
    std::string decoded, checkpoint;
    RecipeLibrary parsed;
    bool ok = metadata.size() > 65536 && read_recipe_metadata(address, root, decoded) && decoded == metadata &&
        parse_recipe_library(decoded, parsed) && parsed.recipes.size() == 414 && recipe_library_point(root) &&
        load_native_recipes() && native_recipes.size() == 414 && capture_recipe_checkpoint(checkpoint);
    map_freed();
    ok &= restore_recipe_checkpoint(checkpoint, "game/test") && load_native_recipes() && native_recipes.size() == 414;
    missing = true; ok &= !read_recipe_metadata(address, root, decoded); missing = false;
    std::swap(texts[0][0], texts[1][0]); ok &= !read_recipe_metadata(address, root, decoded);
    std::swap(texts[0][0], texts[1][0]);
    --reinterpret_cast<save::NativeString*>(chunks[0].data() + 0xbe0)->length;
    ok &= !read_recipe_metadata(address, root, decoded);
    ++reinterpret_cast<save::NativeString*>(chunks[0].data() + 0xbe0)->length;
    for (const size_t offset : {6u, 15u, 24u}) {
        transport = header; transport[offset] = 'f'; publish();
        ok &= !read_recipe_metadata(address, root, decoded);
    }
    transport = metadata.substr(0, 2046); publish(); map_freed(); session_recipe_plan.clear();
    ok &= !load_native_recipes() && recipe_fault.load() && !capture_recipe_checkpoint(checkpoint);
    uintptr_t result = 0;
    ok &= dispatch_recipe_locked(0, 0x1234, 0, 0, 0, false, "unused", result) && result == 0x1234;
    transport = metadata; publish(); map_freed(); session_recipe_plan.clear();
    ok &= load_native_recipes() && native_recipes.size() == 414;
    map_freed(); session_recipe_plan.clear();
    world_global = previous_world; find_entity = previous_find; definition_name = previous_name;
    chunks.clear();
    return ok && test_darklord_summon_scope() && test_darklord_telemetry();
}

bool recipe_fault_call(uintptr_t worker) {
    __try { maintain_tick_detour(worker); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode() == 0xe0700002; }
    return false;
}

bool test_recipe_inputs() {
    struct Text { uintptr_t table; const char* data; int32_t length; uint32_t flags; };
    struct Collection { uintptr_t records; int32_t count, capacity; uint32_t flags; };
    static uintptr_t root, library, alias, source_group, source_point, owner, destination, spirit_alias, spirit_library, spirit_point, counter_library;
    static std::array<uintptr_t, 2> actor_addresses;
    static uintptr_t original_definitions;
    static std::string metadata, origin, selected_name;
    static const char* arena_point_name;
    static unsigned calls, attempts, registrations, nested_calls;
    static bool fail, valid, nested, fault, unique_bodies, missing_source, breach_fallback_available;
    static uint64_t expected_logical, expected_physical;
    static uint32_t expected_override, expected_maintenance_population;
    auto world = std::make_unique<std::array<uint8_t, 0x126680>>();
    std::array<uint8_t, 0xe30> root_storage{}, library_storage{}, alias_storage{}, group_storage{}, point_storage{}, manager{};
    std::array<uint8_t, 0x140> worker{};
    std::array<uint8_t, 0x80 * 4> bodies{};
    std::array<uintptr_t, 0x90 / 8> actor_table{};
    std::array<uintptr_t, 2> actor_storage{};
    actor_table[0x88 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t actor) -> uint32_t {
        return actor == actor_addresses[0] ? 1 : 2;
    });
    for (size_t i = 0; i < actor_storage.size(); ++i) {
        actor_storage[i] = reinterpret_cast<uintptr_t>(actor_table.data());
        actor_addresses[i] = reinterpret_cast<uintptr_t>(&actor_storage[i]);
    }
    std::array<std::string, 4> names;
    root = reinterpret_cast<uintptr_t>(root_storage.data()); library = reinterpret_cast<uintptr_t>(library_storage.data());
    alias = reinterpret_cast<uintptr_t>(alias_storage.data()); source_group = reinterpret_cast<uintptr_t>(group_storage.data());
    source_point = reinterpret_cast<uintptr_t>(point_storage.data()); owner = reinterpret_cast<uintptr_t>(manager.data());
    const auto world_address = reinterpret_cast<uintptr_t>(world->data());
    Collection collection{0, 0, 32, 0x1050001};
    *reinterpret_cast<uintptr_t*>(world->data() + 0x62088) = reinterpret_cast<uintptr_t>(&collection);
    const auto previous_world = world_global;
    const auto previous_name = definition_name;
    const auto previous_find = find_entity;
    const auto previous_factory = original_point_factory;
    const auto previous_group = original_group;
    const auto previous_spawn = original_spawn;
    const auto previous_register = original_register_spawn;
    const auto previous_candidate = original_point_candidate;
    const auto previous_index = original_point_index;
    const auto previous_tick = original_maintain_tick;
    const auto previous_resolve = resolve_actor;
    const auto previous_refresh = refresh_entity;
    const auto previous_single = original_single;
    const auto previous_archvile = original_archvile;
    const auto previous_buff = original_buff;
    const auto previous_helper = original_spirit_helper;
    const auto previous_allocate = allocate_worker;
    const auto previous_construct = construct_stagger;
    const auto previous_assign = assign_manager;
    const auto previous_queue = original_queue_command;
    const auto previous_stagger = original_stagger_tick;
    const auto previous_rewind = rewind_timeline;
    const auto previous_clock = native_clock;
    const auto previous_step = native_step;
    const auto previous_duration = native_duration;
    const auto previous_time = construct_time;
    world_global = reinterpret_cast<uintptr_t>(&world_address);
    refresh_entity = [](uintptr_t reference, uintptr_t) {
        const auto id = *reinterpret_cast<const uint32_t*>(reference);
        if (id == 1 || id == 2) *reinterpret_cast<uintptr_t*>(reference + 8) = actor_addresses[id - 1];
    };
    calls = attempts = registrations = nested_calls = 0; fail = nested = fault = unique_bodies = missing_source = false; valid = true;
    arena_point_name = nullptr;
    breach_fallback_available = false;
    expected_logical = 16;
    expected_override = 12;
    expected_maintenance_population = 3;
    spirit_alias = spirit_library = spirit_point = counter_library = 0;
    origin.assign(20, 'b');
    metadata = "EAP1|" + std::string(64, 'a') + "|game/test\n" + origin +
        "|m|original_group|arena|0000000000000010|00000002|00000003|00000005|00000001\n";
    *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
    for (size_t i = 0; i < names.size(); ++i) {
        char index[9]; sprintf_s(index, "%08x", static_cast<unsigned>(i == 3 ? 1 : i));
        names[i] = "ap_v070_ai_r" + origin + "_" + index + "_q0000000200000003" + (i == 3 ? "_w00000001" : "") +
            "_t00000000000000100000000000000020_p00000005000000010000000300000100_l00000001000000010000000100000003_kn__ap_v070_s00100";
        *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(), static_cast<int32_t>(names[i].size()), 0};
    }
    *reinterpret_cast<uintptr_t*>(library + 0xc38) = reinterpret_cast<uintptr_t>(bodies.data());
    *reinterpret_cast<int32_t*>(library + 0xc40) = 4;
    original_definitions = 0x9911;
    *reinterpret_cast<uintptr_t*>(source_point + 0xc38) = original_definitions;
    *reinterpret_cast<int32_t*>(source_point + 0xc40) = 2;
    *reinterpret_cast<int32_t*>(source_point + 0xc54) = 1;
    *reinterpret_cast<int32_t*>(source_point + 0xd00) = 7;
    *reinterpret_cast<int32_t*>(source_point + 0xcd0) = 9;
    const std::string alias_name = "ap_v070_request_r" + origin;
    static const char* alias_text; alias_text = alias_name.c_str();
    static const char* spirit_alias_text;
    definition_name = [](uintptr_t entity) -> const char* {
        if (entity == alias) return alias_text;
        if (spirit_alias && entity == spirit_alias) return spirit_alias_text;
        if (entity == owner) return "arena";
        if (entity == destination) return "destination";
        if (entity == source_point && arena_point_name) return arena_point_name;
        if (entity == actor_addresses[0] || entity == actor_addresses[1]) return selected_name.c_str();
        return "native_point";
    };
    find_entity = [](uintptr_t, const char* name) -> uintptr_t {
        if (!std::strcmp(name, "ap_v070_recipe_plan")) return root;
        if (std::string_view(name) == "ap_v070_bodies_r" + origin) return library;
        if (counter_library && std::string_view(name) == "ap_v070_counter_r" + origin) return counter_library;
        if (spirit_library && std::string_view(name) == "ap_v070_bodies_r" + std::string(20, 'c')) return spirit_library;
        if (!std::strcmp(name, "original_group")) return source_group;
        if (!std::strcmp(name, "ai_encounter_spawn_group_zone_skyscraper_upper_last")) return source_group;
        if (!std::strcmp(name, "breach_wasteland_arena_target_spawn_60")) return missing_source ? 0 : source_point;
        if (missing_source && breach_fallback_available && !std::strcmp(name, "ap_v070_source_413b7abe2818a6b7")) return alias;
        if (!std::strcmp(name, "original_point")) return missing_source ? 0 : source_point;
        if (missing_source && !std::strcmp(name, "ap_v070_source_a9f183750900d6cf")) return alias;
        if (!std::strcmp(name, "arch_template")) return source_group;
        if (!std::strcmp(name, "original_spirit_point")) return spirit_point;
        return 0;
    };
    original_point_factory = [](uintptr_t point, uintptr_t group) -> uintptr_t {
        if (!recipe_creation) {
            ++nested_calls;
            valid = valid && *reinterpret_cast<uintptr_t*>(point + 0xc38) == original_definitions &&
                *reinterpret_cast<int32_t*>(point + 0xc40) == 2 && *reinterpret_cast<int32_t*>(point + 0xd00) == 7;
            return 0x7777;
        }
        ++attempts;
        if (recipe_creation->recipe->definition.family == 'm') valid &= recipe_creation->limits.population == expected_maintenance_population;
        const auto expected_point = immora_breach_group(*recipe_creation->recipe) && missing_source ? alias :
            recipe_creation->recipe->definition.family == 's' && spirit_point ? spirit_point : source_point;
        valid = valid && point == expected_point && group == 0 && *reinterpret_cast<uint32_t*>(point + 0xd00) == expected_override &&
            !*reinterpret_cast<int32_t*>(point + 0xcd0) && *reinterpret_cast<int32_t*>(point + 0xc40) == 1 &&
            !*reinterpret_cast<int32_t*>(point + 0xc54);
        native_text(*reinterpret_cast<uintptr_t*>(point + 0xc38), selected_name, 511);
        if (fault) RaiseException(0xe0700002, 0, 0, nullptr);
        if (fail) return 0;
        if (nested) {
            nested = false; valid = valid && point_factory_detour(point, group) == 0x7777 &&
                *reinterpret_cast<int32_t*>(point + 0xc40) == 1 && *reinterpret_cast<int32_t*>(point + 0xd00) == 12;
        }
        ++calls; return actor_addresses[unique_bodies ? recipe_creation->emission.ordinal % actor_addresses.size() : 0];
    };
    original_register_spawn = [](uintptr_t manager_address, uint64_t* record) {
        const auto logical = recipe_creation ? recipe_creation->recipe->definition.logical_mask : expected_logical;
        valid = valid && manager_address == owner && record[0] == logical; ++registrations;
    };
    original_spawn = [](uintptr_t manager_address, uintptr_t record, uintptr_t point, uintptr_t, uintptr_t) -> uint64_t {
        const auto type = *reinterpret_cast<uint64_t*>(record);
        const bool supplied = recipe_creation && *reinterpret_cast<uintptr_t*>(point + 0xc38) == recipe_creation->body;
        const bool immediate = recipe_creation && recipe_creation->recipe->definition.family == 'i';
        const auto expected_point = recipe_creation && immora_breach_group(*recipe_creation->recipe) && missing_source ? alias :
            recipe_creation && recipe_creation->recipe->definition.family == 's' && spirit_point ? spirit_point : source_point;
        const auto logical = recipe_creation ? recipe_creation->recipe->definition.logical_mask : expected_logical;
        const auto physical = recipe_creation ? spawn_tracking(*reinterpret_cast<const char**>(recipe_creation->body + 8))->physical : expected_physical;
        valid = valid && manager_address == owner && point == expected_point &&
            (type == (supplied ? physical : logical) || type == encounter_ai_mask);
        if (immediate && !supplied) return 0;
        if (supplied) {
            uint64_t selected = type;
            if (immediate || type == encounter_ai_mask) {
                selected = 0;
                bool population_entry = false;
                for (uint64_t candidate = 1; candidate <= (uint64_t{1} << 43); candidate <<= 1) {
                    if (!(type & candidate) || !point_candidate_detour(manager_address, point, candidate)) continue;
                    if (!selected) selected = candidate;
                    population_entry |= candidate == 16;
                }
                if (!selected || !population_entry) return 0;
                const auto unrelated = expected_physical == 1 ? 2 : 1;
                valid = valid && !point_candidate_detour(manager_address + 1, point, unrelated) &&
                    point_index_detour(manager_address + 1, point, unrelated) == -1 &&
                    !point_candidate_detour(manager_address, source_group, selected) &&
                    point_index_detour(manager_address, source_group, selected) == -1;
            } else if (!point_candidate_detour(manager_address, point, type)) return 0;
            const auto index = point_index_detour(manager_address, point, selected);
            *reinterpret_cast<int32_t*>(point + 0xc54) = index >= 0 && index < *reinterpret_cast<int32_t*>(point + 0xc40) ? index : -1;
            valid = valid && *reinterpret_cast<int32_t*>(point + 0xc54) >= 0;
        }
        const auto actor = point_factory_detour(point, 0);
        if (!actor) return 0;
        const auto id = actor_identifier(actor);
        reinterpret_cast<uint64_t*>(record)[1] = uint64_t{id} << 32 | id;
        reinterpret_cast<uint64_t*>(record)[2] = 0;
        register_spawn_detour(manager_address, reinterpret_cast<uint64_t*>(record)); return 1;
    };
    original_group = [](uintptr_t manager_address, uint64_t* record, uintptr_t group) -> uint64_t {
        valid = valid && group == source_group;
        if ((*record & encounter_ai_mask) != encounter_ai_mask) return 0;
        return spawn_detour(manager_address, reinterpret_cast<uintptr_t>(record), source_point, 0, 0);
    };
    const std::string label = "public_label";
    *reinterpret_cast<uintptr_t*>(worker.data() + 0x40) = owner;
    *reinterpret_cast<uintptr_t*>(worker.data() + 0xd8) = alias;
    *reinterpret_cast<Text*>(worker.data() + 0x58) = {0, label.c_str(), static_cast<int32_t>(label.size()), 0};
    *reinterpret_cast<int32_t*>(worker.data() + 0x90) = 2;
    *reinterpret_cast<int32_t*>(worker.data() + 0x94) = 3;
    original_maintain_tick = [](uintptr_t command) {
        valid = valid && *reinterpret_cast<int32_t*>(command + 0x90) == 3 && *reinterpret_cast<int32_t*>(command + 0x94) == 5;
        alignas(8) uint8_t record[0x68]{};
        *reinterpret_cast<uint64_t*>(record) = 16;
        *reinterpret_cast<Text*>(record + 0x28) = *reinterpret_cast<Text*>(command + 0x58);
        if (group_detour(owner, reinterpret_cast<uint64_t*>(record), alias))
            ++*reinterpret_cast<int32_t*>(command + 0xc8);
        if (*reinterpret_cast<int32_t*>(command + 0xc8) == 5) *reinterpret_cast<uint8_t*>(command + 0xcc) = 1;
    };
    stopped.store(false);
    fail = true;
    maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    bool ok = attempts == 1 && !calls && recipe_spawn_traces.size() == 1 &&
        recipe_state.requests().size() == 1 &&
        !recipe_state.requests().begin()->second.emitted && !recipe_state.requests().begin()->second.reserved && !reserved_actors;
    const auto first = selected_name;
    const std::string finite_origin(20, 'e');
    auto finite_recipe = native_recipes.at(origin); finite_recipe.definition.family = 'i'; finite_recipe.definition.origin = finite_origin;
    native_recipes.emplace(finite_origin, std::move(finite_recipe));
    ok &= recipe_state.request("finite_priority", {"arena", finite_origin, "", 16, 2, 3, 3});
    worker_recipes[1] = "finite_priority"; fail = false;
    maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    ok &= attempts == 1 && !calls;
    recipe_state.cancel("finite_priority"); worker_recipes.erase(1); native_recipes.erase(finite_origin); fail = true;
    collection.count = collection.capacity;
    maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    ok = ok && attempts == 1 && !recipe_state.requests().begin()->second.emitted;
    collection.count = 0; fail = false; nested = true;
    maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    ok = ok && calls == 1 && selected_name == first && nested_calls == 1;
    std::string checkpoint;
    ok = ok && capture_recipe_checkpoint(checkpoint);
    for (int i = 0; i < 4; ++i) maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    std::string snapshot;
    RecipeState restored;
    ok = ok && valid && calls == 5 && registrations == 5 && selected_name == names[3] && !reserved_actors &&
        recipe_state.requests().begin()->second.emitted == 5 && recipe_state.requests().begin()->second.closed &&
        recipe_state.capture(snapshot) && restored.restore(snapshot, recipe_library.plan, recipe_library.map) &&
        *reinterpret_cast<uintptr_t*>(source_point + 0xc38) == original_definitions &&
        *reinterpret_cast<int32_t*>(source_point + 0xc40) == 2 && *reinterpret_cast<int32_t*>(source_point + 0xc54) == 1 &&
        *reinterpret_cast<int32_t*>(source_point + 0xd00) == 7 && *reinterpret_cast<int32_t*>(source_point + 0xcd0) == 9;
    ok = ok && recipe_state.actor_changed(recipe_actors.begin()->second, RecipeActorState::dead) &&
        !credit_at_most(recipe_state.deaths("arena", 16, label), 0) &&
        !restore_recipe_checkpoint("EAPC1|" + std::string(64, 'c') + "\n", "game/test") &&
        !restore_recipe_checkpoint("EAPC1|invalid\n", "game/test") &&
        restore_recipe_checkpoint(checkpoint, "game/test");
    map_freed();
    for (size_t i = 0; i < names.size(); ++i) {
        names[i].replace(names[i].find("00000100_l"), 8, "00000102");
        *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(), static_cast<int32_t>(names[i].size()), 0};
    }
    expected_maintenance_population = 5;
    *reinterpret_cast<int32_t*>(worker.data() + 0x90) = 2;
    *reinterpret_cast<int32_t*>(worker.data() + 0x94) = 3;
    *reinterpret_cast<int32_t*>(worker.data() + 0xc8) = 0;
    *reinterpret_cast<uint8_t*>(worker.data() + 0xcc) = 0;
    calls = attempts = registrations = 0;
    maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    ok &= valid && calls == 1 && registrations == 1;
    for (size_t i = 0; i < names.size(); ++i) {
        names[i].replace(names[i].find("00000102_l"), 8, "00000100");
        *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(), static_cast<int32_t>(names[i].size()), 0};
    }
    expected_maintenance_population = 3;
    map_freed();
    *reinterpret_cast<int32_t*>(worker.data() + 0x90) = 2;
    *reinterpret_cast<int32_t*>(worker.data() + 0x94) = 3;
    *reinterpret_cast<int32_t*>(worker.data() + 0xc8) = 0;
    *reinterpret_cast<uint8_t*>(worker.data() + 0xcc) = 0;
    calls = attempts = registrations = 0;
    maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    ok = ok && calls == 1 && selected_name == first && recipe_state.requests().begin()->second.emitted == 1 &&
        credit_at_most(recipe_state.deaths("arena", 16, label), 0);
    for (int i = 0; i < 4; ++i) maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
    ok = ok && valid && calls == 5 && registrations == 5 && selected_name == names[3] &&
        recipe_state.requests().size() == 1 && recipe_state.requests().begin()->second.closed;
    std::array<uint8_t, 0x140> failed_worker{};
    *reinterpret_cast<uintptr_t*>(failed_worker.data() + 0x40) = owner;
    *reinterpret_cast<uintptr_t*>(failed_worker.data() + 0xd8) = alias;
    *reinterpret_cast<Text*>(failed_worker.data() + 0x58) = {0, label.c_str(), static_cast<int32_t>(label.size()), 0};
    fault = true;
    ok = ok && recipe_fault_call(reinterpret_cast<uintptr_t>(failed_worker.data())) && !reserved_actors && !recipe_creation &&
        !recipe_state.requests().rbegin()->second.reserved && !recipe_state.requests().rbegin()->second.emitted &&
        *reinterpret_cast<uintptr_t*>(source_point + 0xc38) == original_definitions;
    fault = false;
    bool unlocked = false;
    const auto probe = CreateThread(nullptr, 0, [](void* output) -> DWORD {
        const auto acquired = admission_mutex.try_lock();
        if (acquired) admission_mutex.unlock();
        *static_cast<bool*>(output) = acquired; return 0;
    }, &unlocked, 0, nullptr);
    ok = ok && probe && WaitForSingleObject(probe, 1000) == WAIT_OBJECT_0 && unlocked;
    if (probe) CloseHandle(probe);
    std::array<uint8_t, 0xe30> destination_storage{};
    std::array<uint8_t, 0x40> manager_references{};
    std::array<uint8_t, 24> actor_reference{};
    std::array<uint8_t, 0x68> migrated_record{};
    destination = reinterpret_cast<uintptr_t>(destination_storage.data());
    *reinterpret_cast<uintptr_t*>(manager_references.data() + 8) = owner;
    *reinterpret_cast<uintptr_t*>(manager_references.data() + 0x20 + 8) = destination;
    *reinterpret_cast<uintptr_t*>(world->data() + 0x126658) = reinterpret_cast<uintptr_t>(manager_references.data());
    *reinterpret_cast<int32_t*>(world->data() + 0x126660) = 2;
    collection.records = reinterpret_cast<uintptr_t>(actor_reference.data()); collection.count = 1;
    *reinterpret_cast<uintptr_t*>(destination + 0xc80) = reinterpret_cast<uintptr_t>(migrated_record.data());
    *reinterpret_cast<int32_t*>(destination + 0xc88) = 1;
    *reinterpret_cast<uint32_t*>(migrated_record.data() + 8) = 1;
    *reinterpret_cast<uint32_t*>(migrated_record.data() + 12) = 1;
    *reinterpret_cast<uintptr_t*>(migrated_record.data() + 16) = actor_addresses[0];
    const std::string migrated_label = "migrated";
    *reinterpret_cast<Text*>(migrated_record.data() + 0x28) = {0, migrated_label.c_str(), static_cast<int32_t>(migrated_label.size()), 0};
    resolve_actor = [](uintptr_t) -> uintptr_t { return actor_addresses[0]; };
    const auto cancelled_key = worker_recipes.at(reinterpret_cast<uintptr_t>(failed_worker.data()));
    const uintptr_t active_commands[]{reinterpret_cast<uintptr_t>(failed_worker.data())};
    *reinterpret_cast<uintptr_t*>(owner + 0xcc0) = reinterpret_cast<uintptr_t>(active_commands);
    *reinterpret_cast<int32_t*>(owner + 0xcc8) = 1;
    observe(world_address);
    ok = ok && credit_at_most(recipe_state.alive("arena", 16, "public_label"), 0) &&
        !credit_at_most(recipe_state.alive("destination", 16, "migrated"), 0) &&
        recipe_state.requests().begin()->second.request.owner == "arena" && !recipe_fault.load() &&
        !recipe_state.requests().at(cancelled_key).closed;
    *reinterpret_cast<int32_t*>(owner + 0xcc8) = 0;
    observe(world_address);
    ok = ok && recipe_state.requests().at(cancelled_key).closed &&
        !recipe_state.requests().at(cancelled_key).emitted && worker_recipes.empty() &&
        credit_at_most(recipe_state.deaths("arena", 16, "public_label"), 0);
    collection.count = 0;
    observe(world_address);
    ok = ok && recipe_state.requests().begin()->second.actors.empty() &&
        credit_at_most(recipe_state.deaths("destination", 16, "migrated"), 0) && recipe_state.capture(snapshot);
    static std::array<uint8_t, 0x110> queued_worker;
    static uintptr_t queued_commands[1];
    static bool allocation_allowed, native_allowed;
    static unsigned retries, helper_records;
    const auto previous_admission = native_admission;
    native_allowed = true;
    native_admission = [](uintptr_t, int32_t) { return native_allowed; };
    allocate_worker = [](size_t size, uint32_t tag) -> uintptr_t {
        valid = valid && size == 0x110 && tag == 0x54;
        return allocation_allowed ? reinterpret_cast<uintptr_t>(queued_worker.data()) : 0;
    };
    construct_stagger = [](uintptr_t memory, int32_t count, uint64_t minimum, uint64_t maximum,
                            uintptr_t target, uint64_t mask, const char* text) -> uintptr_t {
        valid = valid && (count == 1 || count == 2) && !minimum && !maximum && (target == alias || target == spirit_alias) &&
            (mask == expected_logical || mask == uint64_t{1} << 40);
        queued_worker.fill(0);
        *reinterpret_cast<int32_t*>(memory + 0x90) = count;
        *reinterpret_cast<int32_t*>(memory + 0xa8) = count;
        *reinterpret_cast<uintptr_t*>(memory + 0xc8) = target;
        *reinterpret_cast<uint64_t*>(memory + 0x88) = mask;
        *reinterpret_cast<Text*>(memory + 0x58) = {0, text, static_cast<int32_t>(std::strlen(text)), 0};
        return memory;
    };
    assign_manager = [](uintptr_t reference, uintptr_t manager_address) { *reinterpret_cast<uintptr_t*>(reference + 8) = manager_address; };
    original_queue_command = [](uintptr_t manager_address, uintptr_t command) {
        if (command == reinterpret_cast<uintptr_t>(queued_worker.data()))
            valid = valid && !*reinterpret_cast<const int32_t*>(manager_address + 0xd90);
        queued_commands[0] = command;
        *reinterpret_cast<uintptr_t*>(manager_address + 0xcc0) = reinterpret_cast<uintptr_t>(queued_commands);
        *reinterpret_cast<int32_t*>(manager_address + 0xcc8) = 1;
    };
    rewind_timeline = [](uintptr_t timeline, int32_t) { valid = valid && timeline == owner + 0xe20; ++retries; };
    original_single = [](uintptr_t manager_address, uintptr_t output, uint64_t type, uintptr_t point, uintptr_t text) -> uintptr_t {
        if (!native_allowed) { retry_immediate(manager_address); return output; }
        alignas(8) uint8_t record[0x68]{};
        *reinterpret_cast<uint64_t*>(record) = type;
        const auto label = reinterpret_cast<const char*>(text);
        *reinterpret_cast<Text*>(record + 0x28) = {0, label, static_cast<int32_t>(std::strlen(label)), 0};
        spawn_detour(manager_address, reinterpret_cast<uintptr_t>(record), point, 0, 0);
        return output;
    };
    original_archvile = [](uintptr_t manager_address, uintptr_t output, uintptr_t point, uintptr_t arch_template,
                            uintptr_t text, const char* group) -> uintptr_t {
        valid = valid && arch_template == source_group && !std::strcmp(group, "raw_group");
        return original_single(manager_address, output, expected_logical, point, text);
    };
    original_buff = [](uintptr_t manager_address, uintptr_t output, uintptr_t point, uintptr_t text, const char* group) -> uintptr_t {
        valid = valid && !std::strcmp(group, "raw_group");
        return original_single(manager_address, output, expected_logical, point, text);
    };
    original_spirit_helper = [](uintptr_t manager_address, uint64_t hosts, const char* text, uintptr_t point,
                                uint8_t first, uint8_t flag, uintptr_t group, uintptr_t parent) {
        valid = valid && manager_address == owner && hosts == 16 && !first && flag && !group && !parent &&
            text && std::strcmp(text, "host | filter") == 0;
        if (point_factory_detour(point, 0)) ++helper_records;
    };
    original_stagger_tick = [](uintptr_t command) {
        alignas(8) uint8_t record[0x68]{};
        *reinterpret_cast<uint64_t*>(record) = *reinterpret_cast<uint64_t*>(command + 0x88);
        *reinterpret_cast<save::NativeString*>(record + 0x28) = *reinterpret_cast<save::NativeString*>(command + 0x58);
        if (group_detour(owner, reinterpret_cast<uint64_t*>(record), *reinterpret_cast<uintptr_t*>(command + 0xc8)))
            --*reinterpret_cast<int32_t*>(command + 0xa8);
        if (!*reinterpret_cast<int32_t*>(command + 0xa8)) *reinterpret_cast<uint8_t*>(command + 0xb9) = 1;
    };
    for (const auto [family, definition_count] : {std::pair{'i', 0}, {'i', 1}, {'i', 2}, {'a', 2}, {'b', 2}, {'s', 2}}) {
        map_freed(); unique_bodies = true;
        *reinterpret_cast<int32_t*>(source_point + 0xc40) = definition_count;
        *reinterpret_cast<int32_t*>(source_point + 0xc54) = definition_count ? definition_count - 1 : -1;
        original_point_candidate = [](uintptr_t, uintptr_t point, uint64_t type) -> uint64_t {
            if (*reinterpret_cast<int32_t*>(point + 0xc40) != 1) return 0;
            const auto name = *reinterpret_cast<const char**>(*reinterpret_cast<uintptr_t*>(point + 0xc38) + 8);
            const auto tracking = spawn_tracking(name);
            return tracking && tracking->physical == type;
        };
        original_point_index = [](uintptr_t manager_address, uintptr_t point, uint64_t type) -> int32_t {
            return original_point_candidate(manager_address, point, type) ? 0 : -1;
        };
        const uint32_t source_override = family == 'i' ? 1 : family == 'a' ? 3 : family == 'b' ? 8 : 9;
        expected_override = family == 's' ? 10 : source_override;
        *reinterpret_cast<uint32_t*>(source_point + 0xd00) = source_override;
        const auto source_state = read_point_state(source_point);
        const auto source_unchanged = [&] {
            const auto current = read_point_state(source_point);
            return current.definitions == source_state.definitions && current.count == source_state.count &&
                current.index == source_state.index && current.animation == source_state.animation && current.state == source_state.state;
        };
        expected_logical = family == 's' ? uint64_t{1} << 40 : family == 'a' ? uint64_t{1} << 30 :
            family == 'b' ? uint64_t{1} << 37 : uint64_t{1} << (definition_count == 2 ? 21 : definition_count ? 24 : 26);
        expected_physical = family == 'i' ? (definition_count == 2 ? 2 : definition_count ? 16 : 1) : 32;
        char logical[17]; sprintf_s(logical, "%016llx", expected_logical);
        char physical[17]; sprintf_s(physical, "%016llx", expected_physical);
        metadata = "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin + "|" + family +
            "|original_point|arena|" + logical + "|00000001|00000002|00000002|00000001|" +
            (family == 'a' ? "arch_template" : "") + "|" +
            (family == 's' ? "686f7374207c2066696c746572" : "7075626c69635f6c6162656c") +
            (family == 's' ? "||0000000000000010|01\n" : "|7261775f67726f7570|0000000000000000|00\n");
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        *reinterpret_cast<int32_t*>(library + 0xc40) = 2;
        for (unsigned i = 0; i < 2; ++i) {
            names[i] = "ap_v070_ai_r" + origin + "_0000000" + std::to_string(i) + "_q0000000100000002_t" + logical +
                physical + std::string("_l00000001000000010000000100000003_kn__ap_v070_s00100");
            *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(), static_cast<int32_t>(names[i].size()), 0};
        }
        calls = attempts = registrations = retries = helper_records = 0; fail = false; allocation_allowed = false;
        if (family == 'i' && !definition_count) {
            native_allowed = false; allocation_allowed = true;
            collection.count = 0;
            uintptr_t refused_result = 0;
            for (unsigned retry = 0; retry < 3; ++retry)
                dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false, refused_result);
            const bool deferred_once = recipe_state.requests().size() == 1 && pending_recipes.size() == 1 &&
                worker_recipes.empty() && !attempts && retries == 3;
            if (!deferred_once) std::fprintf(stderr, "refused spawn: requests=%zu pending=%zu workers=%zu attempts=%u retries=%u\n",
                recipe_state.requests().size(), pending_recipes.size(), worker_recipes.size(), attempts, retries);
            ok &= deferred_once;
            native_allowed = true;
            dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false, refused_result);
            ok &= recipe_state.requests().size() == 1 && pending_recipes.empty() && worker_recipes.size() == 1 && calls == 1;
            stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
            ok &= calls == 2 && recipe_state.requests().size() == 1 && recipe_state.requests().begin()->second.emitted == 2;
            map_freed(); native_allowed = true; allocation_allowed = false;
            calls = attempts = registrations = retries = helper_records = 0;
            const auto previous_cast = cast_stagger;
            const auto previous_remaining = original_remaining;
            cast_stagger = [](uintptr_t command) -> uintptr_t {
                return command == reinterpret_cast<uintptr_t>(queued_worker.data()) ? command : 0;
            };
            original_remaining = [](uintptr_t) -> uint64_t { return 1; };
            const auto queued = construct_stagger(reinterpret_cast<uintptr_t>(queued_worker.data()),
                2, 0, 0, alias, expected_logical, label.c_str());
            assign_manager(queued + 0x38, owner);
            queue_command_detour(owner, queued);
            alignas(8) std::array<uint8_t, 0xd0> wait{};
            const auto waiting = reinterpret_cast<uintptr_t>(wait.data());
            *reinterpret_cast<uintptr_t*>(waiting + 0x40) = owner;
            *reinterpret_cast<Text*>(waiting + 0x58) = {0, "", 0, 0};
            *reinterpret_cast<uint64_t*>(waiting + 0x88) = encounter_ai_mask;
            const bool waits_before_first_tick = worker_recipes.size() == 1 &&
                recipe_state.requests().size() == 1 && !calls && !remaining_result(waiting);
            if (!waits_before_first_tick) std::fprintf(stderr, "queued wave completed before its first tick\n");
            ok &= waits_before_first_tick;
            for (unsigned tick = 0; tick < 2; ++tick) stagger_tick_detour(queued);
            ok &= calls == 2 && recipe_state.requests().size() == 1 &&
                recipe_state.requests().begin()->second.emitted == 2;
            cast_stagger = previous_cast; original_remaining = previous_remaining;
            map_freed(); calls = attempts = registrations = retries = helper_records = 0;
        }
        collection.count = collection.capacity;
        uintptr_t result = 0;
        ok = ok && dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false, result) &&
            result == 88 && !attempts && retries == 1 && !recipe_state.requests().begin()->second.emitted;
        collection.count = 0; fail = true;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false, result);
        const auto chosen = selected_name;
        ok = ok && attempts == 1 && !calls && retries == 2 && source_unchanged();
        fail = false;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false, result);
        ok = ok && calls == 1 && selected_name == chosen && retries == 3 && recipe_state.requests().begin()->second.emitted == 1;
        allocation_allowed = true;
        *reinterpret_cast<int32_t*>(owner + 0xd90) = 2;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false, result);
        ok = ok && calls == 1 && pending_recipes.empty() && worker_recipes.size() == 1 &&
            *reinterpret_cast<const int32_t*>(owner + 0xd90) == 2;
        *reinterpret_cast<int32_t*>(owner + 0xd90) = 0;
        stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        ok = ok && valid && calls == 2 && attempts == 3 && recipe_state.requests().begin()->second.emitted == 2 &&
            recipe_state.requests().begin()->second.closed && registrations == 2 && !helper_records &&
            !reserved_actors && !recipe_creation && !recipe_fault.load() && source_unchanged();
        map_freed(); calls = attempts = registrations = retries = helper_records = 0;
        collection.count = collection.capacity;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false, result);
        ok &= !attempts && !retries && pending_recipes.empty() && worker_recipes.size() == 1 &&
            !recipe_state.requests().begin()->second.emitted;
        collection.count = 0;
        for (int i = 0; i < 2; ++i) stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        ok &= valid && calls == 2 && recipe_state.requests().begin()->second.closed && source_unchanged();
    }
    map_freed();
    ok &= load_native_recipes();
    auto& empty_group = native_recipes.at(origin);
    empty_group.definition.family = 'g'; empty_group.definition.source.clear();
    empty_group.definition.owner = "game_encounter_manager_traindepot_1";
    const auto empty_multiple = original_multiple;
    static unsigned bridge_calls;
    bridge_calls = 0;
    original_multiple = [](uintptr_t manager, uintptr_t output, uint64_t mask, int32_t count, uintptr_t group, uintptr_t label) -> uintptr_t {
        valid &= manager == owner && mask == expected_logical && count == 1 && !group &&
            !std::strcmp(reinterpret_cast<const char*>(label), "public_label");
        ++bridge_calls; return output;
    };
    uintptr_t empty_result = 0;
    const auto empty_attempts = attempts;
    ok &= dispatch_recipe_immediate(owner, 88, expected_logical, alias,
        reinterpret_cast<uintptr_t>(label.c_str()), true, empty_result) && empty_result == 88 &&
        recipe_state.requests().empty() && pending_recipes.empty() && worker_recipes.empty() && attempts == empty_attempts && bridge_calls == 1;
    const auto empty_worker = construct_stagger(reinterpret_cast<uintptr_t>(queued_worker.data()),
        1, 0, 0, alias, expected_logical, label.c_str());
    assign_manager(empty_worker + 0x38, owner);
    stagger_tick_detour(empty_worker);
    ok &= *reinterpret_cast<const uint8_t*>(empty_worker + 0xb9) &&
        !*reinterpret_cast<const int32_t*>(empty_worker + 0xa8) && recipe_state.requests().empty() && attempts == empty_attempts && bridge_calls == 2;
    stagger_tick_detour(empty_worker);
    ok &= bridge_calls == 2;
    bool bridge_marked = false;
    ok &= !prepare_recipe_worker(empty_worker, false, bridge_marked) && bridge_marked && bridge_calls == 2;
    std::string empty_key;
    ok &= open_native_request(owner, empty_group, label, empty_key);
    worker_recipes[empty_worker] = empty_key;
    *reinterpret_cast<int32_t*>(empty_worker + 0xa8) = 1;
    *reinterpret_cast<uint8_t*>(empty_worker + 0xb9) = 0;
    stagger_tick_detour(empty_worker);
    ok &= recipe_state.requests().at(empty_key).closed && !recipe_state.requests().at(empty_key).emitted &&
        worker_recipes.empty() && attempts == empty_attempts && bridge_calls == 3 && valid &&
        credit_at_most(recipe_state.deaths("arena", expected_logical, label), 0);
    original_multiple = empty_multiple;
    map_freed();
    calls = attempts = registrations = nested_calls = 0;
    fail = nested = fault = unique_bodies = false;
    collection.count = 0;
    ok &= load_native_recipes();
    auto& arena_group = native_recipes.at(origin);
    arena_group.definition.family = 'g';
    arena_group.definition.owner = "ai_encounter_manager_dlc1_skyscraper_upper_last";
    arena_group.definition.source = "ai_encounter_spawn_group_zone_skyscraper_upper_last";
    const auto arena_candidate = original_point_candidate;
    const auto arena_index = original_point_index;
    original_point_candidate = [](uintptr_t, uintptr_t, uint64_t) -> uint64_t { return 1; };
    original_point_index = [](uintptr_t, uintptr_t, uint64_t) -> int32_t { return 3; };
    RecipeCreation arena_creation;
    ok &= begin_recipe_creation(arena_creation, owner, origin, label);
    const auto arena_key = arena_creation.key;
    const auto arena_attempts = attempts;
    for (const auto point : {"ai_target_spawn_35", "ai_target_spawn_85", "ai_target_spawn_86", "ai_target_spawn_146", "ai_target_spawn_147"}) {
        arena_point_name = point;
        ok &= !point_candidate_detour(owner, source_point, expected_logical) &&
            point_index_detour(owner, source_point, expected_logical) == -1 && !recipe_factory(source_point, 0);
    }
    for (const auto point : {"ai_target_spawn_96", "ai_target_spawn_97", "ai_target_spawn_98", "ai_target_spawn_99",
                             "ai_target_spawn_100", "ai_target_spawn_101", "ai_target_spawn_102"}) {
        arena_point_name = point;
        ok &= point_candidate_detour(owner, source_point, expected_logical) == 1 &&
            point_index_detour(owner, source_point, expected_logical) == 3;
    }
    arena_point_name = "ai_target_spawn_147";
    arena_group.definition.family = 'm';
    ok &= point_candidate_detour(owner, source_point, expected_logical) == 1;
    arena_group.definition.family = 'g'; arena_group.definition.owner = "another_arena";
    ok &= point_candidate_detour(owner, source_point, expected_logical) == 1;
    arena_group.definition.owner = "ai_encounter_manager_dlc1_skyscraper_upper_last";
    end_recipe_creation(arena_creation);
    ok &= attempts == arena_attempts && !recipe_state.requests().at(arena_key).emitted &&
        !recipe_state.requests().at(arena_key).closed && !recipe_state.requests().at(arena_key).reserved;
    arena_point_name = "ai_target_spawn_97";
    ok &= begin_recipe_creation(arena_creation, owner, origin, label) && recipe_factory(source_point, 0);
    end_recipe_creation(arena_creation);
    ok &= recipe_state.requests().at(arena_key).emitted == 1 && !reserved_actors && !recipe_fault.load();
    arena_point_name = nullptr;
    original_point_candidate = arena_candidate; original_point_index = arena_index;
    map_freed();
    expected_logical = 0x100; expected_physical = 0x800000;
    metadata = "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin +
        "|g|breach_wasteland_arena_encounter_spawn_group_zone_2|_example_encounter_manager_0|0000000000000100|00000001|00000001|00000001|00000001||||0000000000000000|00\n";
    *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
    names[0] = "ap_v070_ai_r" + origin + "_00000000_q0000000100000001_t00000000000001000000000000800000_l00000001000000010000000100000003_kn__ap_v070_s00100";
    *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
    *reinterpret_cast<int32_t*>(library + 0xc40) = 1;
    calls = attempts = registrations = 0; unique_bodies = false;
    alias_storage = point_storage;
    alignas(8) uint8_t breach_record[0x68]{};
    *reinterpret_cast<uint64_t*>(breach_record) = expected_logical;
    *reinterpret_cast<Text*>(breach_record + 0x28) = {0, label.c_str(), static_cast<int32_t>(label.size()), 0};
    const auto breach_state = read_point_state(source_point);
    const auto breach_unchanged = [&] {
        for (const auto point : {source_point, alias}) {
            const auto state = read_point_state(point);
            if (state.definitions != breach_state.definitions || state.count != breach_state.count ||
                state.index != breach_state.index || state.animation != breach_state.animation || state.state != breach_state.state) return false;
        }
        return *reinterpret_cast<const uint64_t*>(breach_record) == expected_logical;
    };
    const auto breach_group = original_group;
    static unsigned breach_group_calls;
    breach_group_calls = 0;
    original_group = [](uintptr_t, uint64_t*, uintptr_t) -> uint64_t { ++breach_group_calls; return 0; };
    ok &= load_native_recipes();
    auto& breach_recipe = native_recipes.at(origin);
    breach_recipe.definition.owner = "another_arena";
    missing_source = breach_fallback_available = true;
    RecipeCreation unrelated_group;
    ok &= begin_recipe_creation(unrelated_group, owner, origin, label) && !unrelated_group.source;
    end_recipe_creation(unrelated_group);
    breach_recipe.definition.owner = "_example_encounter_manager_0";
    breach_fallback_available = false;
    missing_source = true;
    ok &= !create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin);
    const auto breach_key = pending_recipes.at({owner, origin});
    ok &= !attempts && !breach_group_calls && !recipe_state.requests().at(breach_key).emitted &&
        !recipe_state.requests().at(breach_key).reserved && breach_unchanged();
    breach_fallback_available = true; fail = true;
    ok &= !create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin) &&
        attempts == 1 && !calls && !recipe_state.requests().at(breach_key).emitted &&
        !recipe_state.requests().at(breach_key).reserved && breach_unchanged();
    fail = false;
    ok &= create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin) == 1 &&
        calls == 1 && !breach_group_calls && recipe_state.requests().at(breach_key).emitted == 1 &&
        !reserved_actors && !recipe_fault.load() && breach_unchanged() && valid;
    ok &= !create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin) && calls == 1;
    missing_source = breach_fallback_available = false;
    original_group = breach_group;
    map_freed(); expected_logical = 16;
    std::array<uint8_t, 0xe30> spirit_alias_storage{}, spirit_library_storage{}, spirit_point_storage = point_storage;
    std::array<uint8_t, 0x80> spirit_body{};
    const std::string spirit_origin(20, 'c'), spirit_alias_name = "ap_v070_request_r" + spirit_origin;
    spirit_alias = reinterpret_cast<uintptr_t>(spirit_alias_storage.data()); spirit_alias_text = spirit_alias_name.c_str();
    spirit_library = reinterpret_cast<uintptr_t>(spirit_library_storage.data()); spirit_point = reinterpret_cast<uintptr_t>(spirit_point_storage.data());
    metadata = "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin +
        "|p|original_point|arena|0000000000000010|00000001|00000001|00000001|00000001||7075626c69635f6c6162656c||0000000000000000|00\n" +
        spirit_origin + "|s|original_spirit_point|arena|0000010000000000|00000001|00000001|00000001|00000001||686f7374207c2066696c746572||0000000000000010|01\n";
    *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
    names[0] = "ap_v070_ai_r" + origin + "_00000000_q0000000100000001_t00000000000000100000000000000020_l00000001000000010000000100000003_kn__ap_v070_s00100";
    names[1] = "ap_v070_ai_r" + spirit_origin + "_00000000_q0000000100000001_t00000100000000000000000000000020_l00000001000000010000000100000003_kn__ap_v070_s00100";
    *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
    *reinterpret_cast<int32_t*>(library + 0xc40) = 1;
    *reinterpret_cast<Text*>(spirit_body.data()) = {0, names[1].data(), static_cast<int32_t>(names[1].size()), 0};
    *reinterpret_cast<uintptr_t*>(spirit_library + 0xc38) = reinterpret_cast<uintptr_t>(spirit_body.data());
    *reinterpret_cast<int32_t*>(spirit_library + 0xc40) = 1;
    static Collection* host_collection;
    static SpawnSingle host_single;
    host_collection = &collection; host_single = original_single;
    original_single = [](uintptr_t manager_address, uintptr_t output, uint64_t type, uintptr_t point, uintptr_t text) -> uintptr_t {
        const auto result = host_single(manager_address, output, type, point, text);
        if (calls) host_collection->count = host_collection->capacity;
        return result;
    };
    calls = attempts = registrations = retries = helper_records = 0; allocation_allowed = true;
    collection.count = collection.capacity - 1;
    const auto compound = possessed_detour(owner, 99, 16, alias, reinterpret_cast<uintptr_t>(label.c_str()), spirit_alias,
        16, reinterpret_cast<uintptr_t>(label.c_str()), 1);
    ok = ok && compound == 99 && calls == 1 && registrations == 1 && !helper_records && pending_recipes.empty() &&
        worker_recipes.size() == 1 && !retries && !reserved_actors;
    const auto child_key = worker_recipes.begin()->second;
    const auto host_key = recipe_state.requests().at(child_key).request.parent_request;
    ok = ok && !host_key.empty() && recipe_state.requests().at(child_key).request.parent_actor == host_key + "/0";
    stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
    ok = ok && calls == 1 && !recipe_state.requests().at(child_key).emitted;
    collection.count = 0;
    ok = ok && recipe_state.actor_changed(host_key + "/0", RecipeActorState::dead);
    stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
    ok = ok && valid && calls == 2 && registrations == 2 && !helper_records &&
        recipe_state.requests().at(child_key).emitted == 1 && recipe_state.requests().at(child_key).closed &&
        recipe_state.capture(snapshot) && restored.restore(snapshot, recipe_library.plan, recipe_library.map) && !recipe_fault.load();
    const auto pair_metadata = metadata;
    const auto pair_names = names;
    for (unsigned body_count : {1u, 2u}) {
        map_freed();
        metadata = "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin +
            "|p|original_point|arena|0000000000000010|00000001|0000000" + std::to_string(body_count) +
            "|0000000" + std::to_string(body_count) + "|00000001||7075626c69635f6c6162656c||0000000000000000|00\n" +
            spirit_origin + "|s|original_spirit_point|arena|0000010000000000|00000001|0000000" + std::to_string(body_count) +
            "|0000000" + std::to_string(body_count) + "|00000001||686f7374207c2066696c746572||0000000000000010|01\n";
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        std::array<uint8_t, 0x100> child_bodies{};
        for (unsigned i = 0; i < body_count; ++i) {
            names[i] = "ap_v070_ai_r" + origin + "_0000000" + std::to_string(i) + "_q000000010000000" + std::to_string(body_count) +
                "_t00000000000000100000000000000020_l00000001000000010000000100000003_kn__ap_v070_s00100";
            names[i + 2] = "ap_v070_ai_r" + spirit_origin + "_0000000" + std::to_string(i) + "_q000000010000000" + std::to_string(body_count) +
                "_t00000100000000000000000000000020_l00000001000000010000000100000003_kn__ap_v070_s00100";
            *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(), static_cast<int32_t>(names[i].size()), 0};
            *reinterpret_cast<Text*>(child_bodies.data() + i * 0x80) = {0, names[i + 2].data(), static_cast<int32_t>(names[i + 2].size()), 0};
        }
        *reinterpret_cast<int32_t*>(library + 0xc40) = static_cast<int32_t>(body_count);
        *reinterpret_cast<uintptr_t*>(spirit_library + 0xc38) = reinterpret_cast<uintptr_t>(child_bodies.data());
        *reinterpret_cast<int32_t*>(spirit_library + 0xc40) = static_cast<int32_t>(body_count);
        *reinterpret_cast<int32_t*>(owner + 0xcc8) = 0;
        static QueueCommand pair_queue;
        pair_queue = original_queue_command;
        original_queue_command = [](uintptr_t manager_address, uintptr_t command) {
            pair_queue(manager_address, command); allocation_allowed = false;
        };
        calls = attempts = registrations = retries = helper_records = 0;
        allocation_allowed = body_count == 2; native_allowed = true; unique_bodies = true; collection.count = 0;
        const auto invoke_pair = [&] {
            return possessed_detour(owner, 99, 16, alias, reinterpret_cast<uintptr_t>(label.c_str()), spirit_alias,
                16, reinterpret_cast<uintptr_t>(label.c_str()), 1);
        };
        native_allowed = false;
        for (unsigned retry = 0; retry < 3; ++retry)
            ok &= invoke_pair() == 99 && !calls && !attempts && pending_recipes.size() == 2 && recipe_state.requests().size() == 2;
        const auto refused_parent = pending_recipes.at({owner, origin});
        const auto refused_child = pending_recipes.at({owner, spirit_origin});
        native_allowed = true;
        const auto initial_pair = invoke_pair();
        ok &= initial_pair == 99 && calls == 1 && pending_recipes.count({owner, spirit_origin}) == 1;
        if (!pending_recipes.count({owner, spirit_origin})) { original_queue_command = pair_queue; break; }
        const auto retry_child = pending_recipes.at({owner, spirit_origin});
        const auto retry_parent = recipe_state.requests().at(retry_child).request.parent_request;
        ok &= retry_parent == refused_parent && retry_child == refused_child;
        ok &= recipe_state.actor_changed(retry_parent + "/0", RecipeActorState::dead);
        if (body_count == 2) {
            collection.count = 0;
            stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
            ok &= calls == 2 && recipe_state.actor_changed(retry_parent + "/1", RecipeActorState::dead);
            *reinterpret_cast<int32_t*>(owner + 0xcc8) = 0;
        }
        allocation_allowed = false; collection.count = 0; fail = true;
        const auto completed_calls = calls;
        const auto retry_count = retries;
        for (unsigned retry = 0; retry < 3; ++retry) {
            collection.count = 0;
            ok &= invoke_pair() == 99 && calls == completed_calls && recipe_state.requests().size() == 2 &&
                pending_recipes.at({owner, spirit_origin}) == retry_child &&
                recipe_state.requests().at(retry_child).request.parent_request == retry_parent &&
                recipe_state.requests().at(retry_child).request.parent_actor == retry_parent + "/0" &&
                !recipe_state.requests().at(retry_child).emitted &&
                recipe_state.requests().at(retry_parent).emitted == body_count &&
                recipe_state.requests().at(retry_parent).dead == body_count &&
                recipe_state.requests().at(retry_parent).closed == (body_count == 2) &&
                credit_at_least(recipe_state.deaths("arena", 16, label), 1);
        }
        ok &= retries == retry_count + 3 && !reserved_actors;
        if (body_count == 2) {
            fail = false; collection.count = 0;
            ok &= invoke_pair() == 99 && calls == completed_calls + 1 && pending_recipes.at({owner, spirit_origin}) == retry_child &&
                recipe_state.requests().at(retry_child).actors.at(retry_child + "/0").ordinal == 0 &&
                recipe_state.actor_changed(retry_child + "/0", RecipeActorState::dead);
            for (unsigned retry = 0; retry < 3; ++retry) {
                collection.count = 0;
                ok &= invoke_pair() == 99 && calls == completed_calls + 1 &&
                    pending_recipes.at({owner, spirit_origin}) == retry_child &&
                    recipe_state.requests().at(retry_child).request.parent_request == retry_parent &&
                    recipe_state.requests().at(retry_child).emitted == 1 && recipe_state.requests().at(retry_child).dead == 1 &&
                    !credit_at_most(recipe_state.deaths("arena", uint64_t{1} << 40, ""), 0);
            }
        }
        fail = false; allocation_allowed = true; collection.count = 0;
        const auto recovery_calls = calls;
        ok &= invoke_pair() == 99 && calls == recovery_calls + (body_count == 1 ? 1u : 0u) && pending_recipes.empty();
        collection.count = 0;
        if (body_count == 2) stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        const auto& recovered = recipe_state.requests().at(retry_child);
        ok &= calls == body_count * 2 && recovered.emitted == body_count && recovered.closed == (body_count == 2) &&
            recovered.dead == (body_count == 2 ? 1u : 0u) && recovered.actors.size() == 1 &&
            recovered.actors.at(retry_child + (body_count == 1 ? "/0" : "/1")).ordinal == body_count - 1 &&
            recipe_state.requests().at(retry_parent).dead == body_count && valid && !recipe_fault.load();
        original_queue_command = pair_queue;
    }
    map_freed();
    *reinterpret_cast<uintptr_t*>(spirit_library + 0xc38) = reinterpret_cast<uintptr_t>(spirit_body.data());
    *reinterpret_cast<int32_t*>(spirit_library + 0xc40) = 1;
    metadata = pair_metadata; names = pair_names;
    *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
    *reinterpret_cast<Text*>(spirit_body.data()) = {0, names[1].data(), static_cast<int32_t>(names[1].size()), 0};
    metadata.replace(metadata.find("00000001|00000001|00000001|00000001"), 35, "00000001|00000000|00000000|00000000");
    *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
    *reinterpret_cast<int32_t*>(library + 0xc40) = 0;
    static std::array<uint8_t, 0xb8> native_delay;
    static unsigned delay_count;
    delay_count = 0;
    allocate_worker = [](size_t size, uint32_t tag) -> uintptr_t {
        valid = valid && size == 0xb8 && tag == 0x54;
        return reinterpret_cast<uintptr_t>(native_delay.data());
    };
    native_clock = [](uintptr_t, uint64_t* value, int32_t channel) -> uint64_t* {
        valid = valid && channel == 1; *value = 43; return value;
    };
    native_step = [](uintptr_t, int32_t channel) { valid = valid && channel == 1; return 0.125f; };
    native_duration = [](uint64_t* value, float seconds) -> uint64_t* {
        valid = valid && seconds == 0.25f; *value = 250; return value;
    };
    construct_time = [](uintptr_t memory, uint64_t current, uint64_t duration, uint64_t initial) -> uintptr_t {
        valid = valid && current == 43 && duration == 250 && initial == 43; ++delay_count; return memory;
    };
    calls = attempts = registrations = retries = helper_records = 0; collection.count = 0;
    ok = ok && possessed_detour(owner, 99, 16, alias, reinterpret_cast<uintptr_t>(label.c_str()), spirit_alias,
        16, reinterpret_cast<uintptr_t>(label.c_str()), 1) == 99 && valid && calls == 1 && delay_count == 1 &&
        registrations == 1 && !helper_records && pending_recipes.empty() &&
        recipe_state.requests().begin()->second.empty_completed && !recipe_state.requests().rbegin()->second.request.parent_actor.size();
    original_single = host_single; collection.count = 0;
    map_freed();
    metadata = "EAP1|" + std::string(64, 'a') + "|game/test\n" + origin +
        "|p|original_point|arena|0000000000000010|00000001|00000001|00000001|00000001\n";
    *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
    *reinterpret_cast<int32_t*>(library + 0xc40) = 1;
    std::array<uint8_t, 0xe30> counter_storage{};
    Text counter_body{0, nullptr, 0, 0};
    auto counter_name = names[0];
    counter_name.replace(counter_name.find("_t00000000000000100000000000000020"), 34,
                         "_t00000000000000100000000000000040");
    counter_body = {0, counter_name.c_str(), static_cast<int32_t>(counter_name.size()), 0};
    counter_library = reinterpret_cast<uintptr_t>(counter_storage.data());
    *reinterpret_cast<uintptr_t*>(counter_library + 0xc38) = reinterpret_cast<uintptr_t>(&counter_body);
    *reinterpret_cast<int32_t*>(counter_library + 0xc40) = 1;
    ok &= load_native_recipes() && native_recipes.at(origin).stone_alternatives.size() == 1 &&
        countermeasure_body(native_recipes.at(origin), reinterpret_cast<uintptr_t>(bodies.data()), false, false, false) ==
            reinterpret_cast<uintptr_t>(&counter_body);
    map_freed();
    const auto stone_primary = names[0], stone_alternate = counter_name;
    names[0].replace(names[0].find("_t00000000000000100000000000000020"), 34,
                     "_t00000000000000100000000020000000");
    counter_name.replace(counter_name.find("_t00000000000000100000000000000040"), 34,
                          "_t00000000000000100000000010000000");
    *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
    counter_body = {0, counter_name.data(), static_cast<int32_t>(counter_name.size()), 0};
    ok &= load_native_recipes() && native_recipes.at(origin).armored_alternatives.size() == 1 &&
        native_recipes.at(origin).stone_alternatives.empty() &&
        countermeasure_body(native_recipes.at(origin), reinterpret_cast<uintptr_t>(bodies.data()), false, false, false, false) ==
            reinterpret_cast<uintptr_t>(&counter_body) &&
        countermeasure_body(native_recipes.at(origin), reinterpret_cast<uintptr_t>(bodies.data()), false, false, false, true) ==
            reinterpret_cast<uintptr_t>(bodies.data());
    map_freed();
    names[0] = stone_primary; counter_name = stone_alternate;
    *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
    counter_body = {0, counter_name.data(), static_cast<int32_t>(counter_name.size()), 0};
    ok &= load_native_recipes();
    alias_storage = point_storage; missing_source = true;
    RecipeCreation fallback;
    ok &= begin_recipe_creation(fallback, owner, origin, label) && fallback.source == alias;
    end_recipe_creation(fallback);
    *reinterpret_cast<int32_t*>(alias + 0xc40) = 0;
    RecipeCreation empty_alias;
    ok &= begin_recipe_creation(empty_alias, owner, origin, label) && !empty_alias.source;
    end_recipe_creation(empty_alias); missing_source = false;
    auto& bounded = native_recipes.at(origin);
    bounded.definition.family = 'm'; bounded.definition.maximum_emissions = bounded.definition.final_credit_units = 0;
    bounded.refill_limit = 7;
    bool marked = false;
    const auto previous_worker = worker_recipe;
    ok &= prepare_recipe_worker(reinterpret_cast<uintptr_t>(failed_worker.data()), true, marked) && marked &&
        *reinterpret_cast<int32_t*>(failed_worker.data() + 0x94) == 7 &&
        !recipe_state.requests().at(*worker_recipe).request.maximum_emissions;
    worker_recipe = previous_worker;
    map_freed();
    *reinterpret_cast<int32_t*>(counter_library + 0xc40) = 0;
    ok &= !load_native_recipes();
    map_freed(); stopped.store(true);
    world_global = previous_world; definition_name = previous_name; find_entity = previous_find;
    original_point_factory = previous_factory; original_group = previous_group; original_spawn = previous_spawn;
    original_register_spawn = previous_register; original_maintain_tick = previous_tick; resolve_actor = previous_resolve;
    refresh_entity = previous_refresh;
    original_point_candidate = previous_candidate; original_point_index = previous_index;
    original_single = previous_single; original_archvile = previous_archvile; original_buff = previous_buff;
    original_spirit_helper = previous_helper; allocate_worker = previous_allocate; construct_stagger = previous_construct;
    assign_manager = previous_assign; original_queue_command = previous_queue; original_stagger_tick = previous_stagger;
    rewind_timeline = previous_rewind;
    native_admission = previous_admission;
    native_clock = previous_clock; native_step = previous_step; native_duration = previous_duration; construct_time = previous_time;
    return ok;
}

bool test_supply_headroom() {
    const auto previous_resolve = resolve_actor;
    const auto previous_name = definition_name;
    const auto previous_admission = native_admission;
    static int32_t managed, maximum;
    static uintptr_t budget;
    managed = 0; maximum = 90;
    native_admission = [](uintptr_t candidate, int32_t adjustment) { return candidate == budget && managed < adjustment - 1 + maximum; };
    static uintptr_t owners[2];
    alignas(uintptr_t) uint8_t managers[2][0xcd0]{};
    int32_t command_count = 1;
    for (unsigned index = 0; index < 2; ++index) {
        owners[index] = reinterpret_cast<uintptr_t>(managers[index]);
        std::memcpy(managers[index] + 0xcc8, &command_count, sizeof(command_count));
    }
    resolve_actor = [](uintptr_t reference) { return *reinterpret_cast<uintptr_t*>(reference); };
    definition_name = [](uintptr_t entity) -> const char* {
        if (entity == owners[0]) return "ap_v070_supply_0123456789abcdefabcd_manager";
        if (entity == owners[1]) return "ap_v070_supply_0123456789abcdefabce_manager";
        if (entity == 1) return "ap_v070_supply_0123456789abcdefabcd_actors_ai__ap_v070_s00100";
        return "native_ai";
    };
    uintptr_t references[4][3]{{11}, {22}, {33}, {44}};
    struct References { uintptr_t data; int32_t count, capacity; uint32_t flags = 0x1050001; }
        collection{reinterpret_cast<uintptr_t>(references), 2, 4};
    uintptr_t manager_references[2][4]{{0, owners[0]}, {0, owners[1]}};
    auto world = std::make_unique<uint8_t[]>(0x126668);
    const auto collection_address = reinterpret_cast<uintptr_t>(&collection);
    const auto manager_address = reinterpret_cast<uintptr_t>(manager_references);
    const int32_t manager_count = 2;
    std::memcpy(world.get() + 0x62088, &collection_address, sizeof(collection_address));
    std::memcpy(world.get() + 0x126658, &manager_address, sizeof(manager_address));
    std::memcpy(world.get() + 0x126660, &manager_count, sizeof(manager_count));
    const auto address = reinterpret_cast<uintptr_t>(world.get());
    budget = address + 0xafd98;
    bool ok = !supply_admission(address);
    collection.count = 1;
    ok = ok && supply_admission(address);
    maximum = 3; managed = 1;
    ok = ok && !supply_admission(address);
    managed = 0;
    ok = ok && supply_admission(address);
    maximum = 90;
    collection.count = 2; references[0][0] = 1;
    ok = ok && supply_admission(address);
    collection.count = 3;
    ok = ok && !supply_admission(address);
    command_count = 0;
    std::memcpy(managers[1] + 0xcc8, &command_count, sizeof(command_count));
    ok = ok && supply_admission(address);
    collection.count = 4;
    ok = ok && !supply_admission(address);
    collection.flags = 0; references[0][0] = 11;
    ok = ok && supply_admission(address);
    resolve_actor = previous_resolve; definition_name = previous_name; native_admission = previous_admission;
    return ok;
}

bool test_limit_inputs() {
    const auto previous_spawn = original_spawn;
    const auto previous_resolve = resolve_actor;
    const auto previous_name = definition_name;
    const auto previous_world = world_global;
    const auto was_stopped = stopped.exchange(false);
    static unsigned calls;
    static bool abi;
    calls = 0; abi = true;
    original_spawn = [](uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth) -> uint64_t {
        ++calls; abi = abi && manager == 7 && record == 8 && point == 9 && fourth == 10 && fifth == 11;
        return 1;
    };
    resolve_actor = [](uintptr_t record) { return *reinterpret_cast<const uintptr_t*>(record); };
    struct Definition { uintptr_t unused; const char* name; uint8_t padding[0x158 - 16]; Definition* parent; };
    static_assert(offsetof(Definition, parent) == 0x158);
    Definition marauder_type{0, "ai/superheavy/marauder", {}, nullptr};
    Definition archvile_type{0, "ai/superheavy/archvile", {}, nullptr};
    Definition spirit_type{0, "ai/ambient/spirit", {}, nullptr};
    Definition boss_type{0, "ai/boss/darklord", {}, &marauder_type};
    Definition marauder_definition{0, "native_marauder", {}, &marauder_type};
    Definition archvile_definition{0, "native_archvile", {}, &archvile_type};
    Definition spirit_definition{0, "native_spirit", {}, &spirit_type};
    definition_name = [](uintptr_t actor) -> const char* {
        if (actor == 9) return "ap_v070_spawn_a_l00000001000000010000000100000002_km__ap_v070_s00100";
        const auto definition = *reinterpret_cast<Definition**>(actor + 0xa8);
        return definition ? definition->name : "native_ai";
    };
    uintptr_t table[34]{};
    struct Actor { uintptr_t* table; bool dead; uint8_t padding[0xa8 - 9]; Definition* definition; };
    static_assert(offsetof(Actor, definition) == 0xa8);
    Actor marauder{table, false, {}, &marauder_definition}, archvile{table, false, {}, &archvile_definition},
          spirit{table, false, {}, &spirit_definition}, boss{table, false, {}, &boss_type};
    Definition default_type{0, "ai/default", {}, nullptr};
    default_type.parent = &default_type;
    Actor ordinary{table, false, {}, &default_type};
    bool hierarchy_ok = special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    Definition strict_parent{0,
        "ap_v070_ai_a_t00000000000000010000000040000000_v"
        "000000010000000200000001000000000000000000000000000000000000000000000000"
        "_a1234567812345678_p00000006000000020000000600000203_l00000001000000020000000100000003_ka__ap_v070_s00100", {}, nullptr};
    ordinary.definition = &strict_parent;
    auto child_limits = *spawn_limits("ap_v070_ai_a_p00000008000000030000000800000303_l00000004000000040000000400000008_ks__ap_v070_s00100");
    inherit_population(child_limits, reinterpret_cast<uintptr_t>(&ordinary));
    hierarchy_ok &= child_limits.strict && child_limits.maximum == std::array<uint32_t, 4>{1,2,1,3} &&
        child_limits.population == 6 && child_limits.heavy == 2 && child_limits.superheavy == 2 &&
        !spawn_allowed(child_limits, {0,0,1});
    strict_parent.name =
        "ap_v070_ai_a_t00000000000000010000000040000000_v"
        "000000010000000200000001000000000000000000000000000000000000000000000000"
        "_a1234567812345678_p00000006000000020000000600000003_l00000001000000020000000100000003_ka__ap_v070_s00100";
    child_limits.kind = 3; child_limits.spirit_counter = true;
    inherit_population(child_limits, reinterpret_cast<uintptr_t>(&ordinary));
    hierarchy_ok &= child_limits.heavy == 1 && child_limits.species_maximum[2] == 0 &&
        spawn_allowed(child_limits, {}) && !spawn_allowed(child_limits, {0,0,0,1});
    strict_parent.name = "ap_v070_ai_a_a1234567812345678_p00000006000000020000000600000201_l00000001000000020000000100000003_ka__ap_v070_s00100";
    child_limits.maximum = {4,4,4,8};
    inherit_population(child_limits, reinterpret_cast<uintptr_t>(&ordinary));
    hierarchy_ok &= !child_limits.strict && child_limits.maximum == std::array<uint32_t, 4>{4,4,4,8};
    constexpr const char* special_types[]{"ai/superheavy/marauder", "ai/superheavy/archvile", "ai/ambient/spirit",
        "ai/ambient/buffpod", "ai/fodder/zombie_t1_screecher", "ai/heavy/bloodangel",
        "ai/fodder/prowler_cursed", "ai/fodder/imp_stone", "ai/superheavy/baron_armored"};
    constexpr uint64_t special_masks[]{0x8000000ull, 0x40000000ull, 0x10000000000ull, 0x2000000000ull,
        0x2ull, 0x1000000ull, 0x100ull, 0x20ull, 0x20000000ull};
    for (unsigned index = 0; index < std::size(special_types); ++index) {
        strict_parent.name = special_types[index];
        unsigned physical = 9;
        special_kind(reinterpret_cast<uintptr_t>(&ordinary), &physical);
        hierarchy_ok &= physical == index;
        child_limits.physical = special_masks[index];
        reserve_admission(child_limits);
        hierarchy_ok &= reserved_species[index] == 1;
        release_admission(child_limits);
        hierarchy_ok &= reserved_species[index] == 0;
    }
    for (const auto type : {"ai/fodder/lostsoul", "ai/fodder/imp_stone", "ai/ambient/zombie_cueball",
            "ai/ambient/tentacle", "ai/ambient/tentacle_liquid", "ai/ambient/super_tentacle",
            "ai/ambient/turret", "ai/ambient/demonic_trooper"}) {
        strict_parent.name = type;
        unsigned physical = 9;
        hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary), &physical) == 8 &&
            special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    }
    strict_parent.name = "ai/boss/gladiator";
    unsigned boss_species = 9;
    hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary), &boss_species) == 9 &&
        special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    for (const auto type : {"ai/heavy/mancubus_fire", "ai/fodder/prowler", "ai/fodder/carcass"}) {
        strict_parent.name = type;
        hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary), &boss_species) == 3;
    }
    ordinary.definition = &default_type;
    NativeRecipe counter;
    counter.blood_punch_alternatives.emplace(11, 22);
    counter.microwave_alternatives.emplace(33, 44);
    counter.stone_alternatives.emplace(55, 66);
    counter.armored_alternatives.emplace(77, 88);
    hierarchy_ok &= countermeasure_body(counter, 11, false, false, false) == 22 && countermeasure_body(counter, 11, true, false, false) == 11 &&
                    countermeasure_body(counter, 33, false, false, false) == 44 && countermeasure_body(counter, 33, false, true, false) == 33 &&
                    countermeasure_body(counter, 55, false, false, false) == 66 && countermeasure_body(counter, 55, false, false, true) == 55 &&
                    countermeasure_body(counter, 77, false, false, false, false) == 88 &&
                    countermeasure_body(counter, 77, false, false, false, true) == 77 &&
                    countermeasure_body(counter, 99, false, false, false, false) == 99;
    Definition first{0, "ai/fodder/first", {}, &default_type};
    default_type.parent = &first;
    hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    table[0x108 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(+[](uintptr_t actor) { return reinterpret_cast<Actor*>(actor)->dead; });
    uintptr_t references[4][3]{{reinterpret_cast<uintptr_t>(&marauder)}, {reinterpret_cast<uintptr_t>(&archvile)},
                             {reinterpret_cast<uintptr_t>(&spirit)}, {reinterpret_cast<uintptr_t>(&boss)}};
    struct References { uintptr_t data; int32_t count, capacity; uint32_t flags = 0x1050001; }
        collection{reinterpret_cast<uintptr_t>(references), 4, 5};
    uint64_t records[4][0x68 / sizeof(uint64_t)]{};
    for (unsigned index = 0; index < 4; ++index) {
        records[index][0] = uint64_t{1} << 27;
        records[index][1] = (uint64_t{1} << 32) | 1;
        records[index][2] = references[index][0];
    }
    alignas(uintptr_t) uint8_t manager[0xd20]{};
    const auto records_address = reinterpret_cast<uintptr_t>(records);
    const int32_t actor_count = 4;
    std::memcpy(manager + 0xc80, &records_address, sizeof(records_address));
    std::memcpy(manager + 0xc88, &actor_count, sizeof(actor_count));
    uintptr_t managers[2][4]{{0, reinterpret_cast<uintptr_t>(manager)}, {0, reinterpret_cast<uintptr_t>(manager)}};
    auto world = std::make_unique<uint8_t[]>(0x126668);
    const auto collection_address = reinterpret_cast<uintptr_t>(&collection);
    const auto managers_address = reinterpret_cast<uintptr_t>(managers);
    const int32_t manager_count = 2;
    std::memcpy(world.get() + 0x62088, &collection_address, sizeof(collection_address));
    std::memcpy(world.get() + 0x126658, &managers_address, sizeof(managers_address));
    std::memcpy(world.get() + 0x126660, &manager_count, sizeof(manager_count));
    const auto world_address = reinterpret_cast<uintptr_t>(world.get());
    world_global = reinterpret_cast<uintptr_t>(&world_address);
    bool ok = spawn_detour(7, 8, 9, 10, 11) == 0 && calls == 0;
    marauder.dead = true; archvile.dead = true;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 0 && calls == 0;
    references[0][0] = 0;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 0 && calls == 0;
    references[1][0] = 0;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 1 && calls == 1;
    marauder.dead = false; references[0][0] = reinterpret_cast<uintptr_t>(&marauder);
    collection.data = reinterpret_cast<uintptr_t>(references + 1); collection.count = 3;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 1 && calls == 2;
    collection.data = reinterpret_cast<uintptr_t>(references); collection.count = 4;
    stopped.store(true);
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 1 && calls == 3 && abi;
    uint64_t native_scope = 14695981039346656037ull;
    for (const unsigned char character : std::string_view("native_ai")) native_scope = (native_scope ^ character) * 1099511628211ull;
    SpawnLimits strict_scope{};
    strict_scope.kind = 3; strict_scope.population = strict_scope.pressure = 3;
    strict_scope.heavy = 1; strict_scope.strict = true; strict_scope.scope = native_scope;
    ordinary.definition = &strict_parent; strict_parent.name = "ai/heavy/mancubus_fire";
    references[0][0] = records[0][2] = reinterpret_cast<uintptr_t>(&ordinary);
    collection.count = 1;
    hierarchy_ok &= population_member(world_address, reinterpret_cast<uintptr_t>(&ordinary), native_scope) &&
        !special_admission(strict_scope);
    strict_scope.scope = native_scope + 1;
    hierarchy_ok &= !population_member(world_address, reinterpret_cast<uintptr_t>(&ordinary), strict_scope.scope) &&
        special_admission(strict_scope);
    strict_scope.scope = native_scope;
    strict_parent.name = "ai/ambient/turret";
    hierarchy_ok &= special_admission(strict_scope);
    original_spawn = previous_spawn; resolve_actor = previous_resolve;
    definition_name = previous_name; world_global = previous_world; stopped.store(was_stopped);
    return ok && hierarchy_ok && test_supply_headroom();
}
bool test_quantity_inputs() {
    struct Actor {
        std::array<uintptr_t, 224> table{};
        std::array<uint8_t, 0xb0> bytes{};
        bool dead = false, ignored = false;
        std::string name;
        float health = 100;
    };
    static std::array<Actor, 6> actors;
    static unsigned native_calls = 0;
    const auto previous_name = definition_name;
    const auto previous_remaining = original_remaining;
    const auto previous_dead = original_remaining_dead;
    const auto previous_entity = refresh_entity, previous_manager = refresh_manager;
    const auto previous_ignored = ignored_actor;
    auto previous_weights = std::move(remaining_weights);
    const auto was_stopped = stopped.exchange(false);
    definition_name = [](uintptr_t actor) -> const char* {
        for (const auto& value : actors) if (actor == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.name.c_str();
        return "ai/fodder/imp";
    };
    refresh_entity = refresh_manager = [](uintptr_t, uintptr_t) {};
    ignored_actor = [](uintptr_t actor) {
        for (const auto& value : actors) if (actor == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.ignored;
        return false;
    };
    original_remaining = [](uintptr_t) -> uint64_t { ++native_calls; return 42; };
    original_remaining_dead = [](uintptr_t command, uintptr_t record) {
        auto& count = *reinterpret_cast<int32_t*>(command + 0xb0);
        auto* data = *reinterpret_cast<uint8_t**>(command + 0xa8);
        const auto identifier = *reinterpret_cast<const uint32_t*>(record + 12);
        for (int32_t i = count; i-- > 0;) {
            if (*reinterpret_cast<const uint32_t*>(data + i * 0x68 + 12) != identifier) continue;
            --count;
            std::memmove(data + i * 0x68, data + (i + 1) * 0x68, static_cast<size_t>(count - i) * 0x68);
        }
    };
    std::array<uint8_t, 0xdb8> manager{};
    std::array<uint8_t, 0xc8> command{};
    std::array<uint8_t, 0x68 * 6> records{};
    const auto cmd = reinterpret_cast<uintptr_t>(command.data());
    *reinterpret_cast<uintptr_t*>(command.data() + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
    *reinterpret_cast<uintptr_t*>(command.data() + 0xa8) = reinterpret_cast<uintptr_t>(records.data());
    *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 6;
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
    for (size_t i = 0; i < actors.size(); ++i) {
        auto& actor = actors[i]; actor.dead = actor.ignored = false; actor.bytes.fill(0);
        actor.table[0x88 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) -> uint32_t {
            for (uint32_t index = 0; index < actors.size(); ++index)
                if (address == reinterpret_cast<uintptr_t>(actors[index].bytes.data())) return index + 1;
            return 0;
        });
        actor.table[0x108 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) {
            for (const auto& value : actors) if (address == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.dead;
            return false;
        });
        *reinterpret_cast<uintptr_t*>(actor.bytes.data()) = reinterpret_cast<uintptr_t>(actor.table.data());
        actor.name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i) +
            "_q0000000200000006_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100";
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = static_cast<uint32_t>(i + 1);
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = static_cast<uint32_t>(i + 1);
        *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) = reinterpret_cast<uintptr_t>(actor.bytes.data());
    }
    bool ok = remaining_detour(cmd) == 0;
    {
        const auto old_world = world_global;
        const auto old_find = find_entity;
        auto old_recipes = std::move(native_recipes);
        auto old_state = std::move(recipe_state);
        auto old_kills = std::move(kill_progress);
        const auto first_name = actors[0].name, second_name = actors[1].name;
        static uintptr_t fixture_world = 1;
        static bool marker = true;
        world_global = reinterpret_cast<uintptr_t>(&fixture_world);
        find_entity = [](uintptr_t, const char* name) -> uintptr_t {
            return marker && std::string_view(name).rfind("ap_v070_optional_zombies_r", 0) == 0 ? 1 : 0;
        };
        actors[0].name = "ap_v070_ai_r0123456789abcdef0123_00000000_q0000000100000001_t00000000000000100000000000000001_l00000001000000010000000100000002_kn__ap_v070_s00100";
        actors[1].name = "ap_v070_ai_r0123456789abcdef0123_00000000_q0000000100000001_t00000000000000100000000000004000_l00000001000000010000000100000002_kn__ap_v070_s00100";
        *reinterpret_cast<uintptr_t*>(manager.data() + 0xc80) = reinterpret_cast<uintptr_t>(records.data());
        *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 2;
        *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16;
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 2;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 0;
        for (size_t i = 0; i < 2; ++i) *reinterpret_cast<uint64_t*>(records.data() + i * 0x68) = 16;
        remaining_weights.erase(cmd);
        ok = ok && remaining_detour(cmd) == 0;
        actors[1].dead = true;
        ok = ok && remaining_detour(cmd) == 1 && !actors[0].dead;
        const auto previous_handle = *reinterpret_cast<uint64_t*>(records.data() + 0x68 + 8);
        *reinterpret_cast<uint64_t*>(records.data() + 0x68 + 8) = uint64_t{0x1fffffe} << 32 | 0x1fffffe;
        *reinterpret_cast<uintptr_t*>(records.data() + 0x68 + 16) = 0;
        ok = ok && optional_zombie_completion_safe(cmd);
        *reinterpret_cast<uint64_t*>(records.data() + 0x68 + 8) = previous_handle;
        *reinterpret_cast<uintptr_t*>(records.data() + 0x68 + 16) = reinterpret_cast<uintptr_t>(actors[1].bytes.data());
        marker = false;
        ok = ok && remaining_detour(cmd) == 0;
        marker = true;
        actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000004");
        ok = ok && remaining_detour(cmd) == 1;
        actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000002");
        ok = ok && remaining_detour(cmd) == 0;
        actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000008");
        ok = ok && remaining_detour(cmd) == 0;
        actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000001");
        manager[0xdaf] = 1;
        ok = ok && remaining_detour(cmd) == 0;
        manager[0xdaf] = 0;
        std::array<uint8_t, 0xb0> zombie_kill{};
        const auto kill = reinterpret_cast<uintptr_t>(zombie_kill.data());
        *reinterpret_cast<uintptr_t*>(kill + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
        *reinterpret_cast<uint64_t*>(kill + 0x88) = 16;
        *reinterpret_cast<int32_t*>(kill + 0xac) = 1;
        ok = ok && kill_result(kill) == 1 && *reinterpret_cast<int32_t*>(kill + 0xa8) == 0;
        recipe_state.reset(std::string(64, 'a'), "fixture");
        RecipeRequest pending{"ai/fodder/imp", "pending", "", 16, 1, 2, 2};
        ok = ok && recipe_state.request("pending", pending);
        native_recipes["pending"].bodies = {reinterpret_cast<uintptr_t>(actors[0].bytes.data()),
                                            reinterpret_cast<uintptr_t>(actors[1].bytes.data())};
        ok = ok && remaining_detour(cmd) == 0 && !recipe_state.requests().at("pending").closed;
        actors[1].name = actors[0].name;
        ok = ok && remaining_detour(cmd) == 1 && recipe_state.requests().at("pending").closed;
        ok = ok && recipe_state.deaths("ai/fodder/imp", 16, "").empty();
        native_recipes = std::move(old_recipes); recipe_state = std::move(old_state); kill_progress = std::move(old_kills);
        world_global = old_world; find_entity = old_find;
        actors[0].name = first_name; actors[1].name = second_name; actors[1].dead = false;
        *reinterpret_cast<uintptr_t*>(manager.data() + 0xc80) = 0;
        *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 0;
        *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 0;
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 6;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
        for (size_t i = 0; i < 2; ++i) *reinterpret_cast<uint64_t*>(records.data() + i * 0x68) = 0;
        remaining_weights.erase(cmd);
    }
    actors[5].dead = actors[1].dead = actors[3].dead = true;
    ok = ok && remaining_detour(cmd) == 1;
    actors[3].ignored = true;
    ok = ok && remaining_detour(cmd) == 0;
    actors[3].ignored = false; manager[0xdaf] = 1;
    ok = ok && remaining_detour(cmd) == 0;
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
    ok = ok && remaining_detour(cmd) == 1;
    *reinterpret_cast<uintptr_t*>(records.data() + 5 * 0x68 + 16) = 0;
    *reinterpret_cast<uint32_t*>(records.data() + 5 * 0x68 + 8) = 0x1fffffe;
    *reinterpret_cast<uint32_t*>(records.data() + 5 * 0x68 + 12) = 0x1fffffe;
    actors[0].name = "ai/phase_transition";
    ok = ok && remaining_detour(cmd) == 1;
    std::array<uint8_t, 0x68> dead_record{};
    std::memcpy(dead_record.data(), records.data() + 0x68, dead_record.size());
    remaining_dead_detour(cmd, reinterpret_cast<uintptr_t>(dead_record.data()));
    ok = ok && remaining_detour(cmd) == 1 && *reinterpret_cast<int32_t*>(command.data() + 0xb0) == 5;
    manager[0xdaf] = 0; *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
    ok = ok && remaining_detour(cmd) == 1;
    remaining_weights.erase(cmd);
    *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 2;
    for (unsigned i = 0; i < 2; ++i) {
        actors[i].dead = false;
        actors[i].name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i) +
            "_q0000000600000002_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100";
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 1;
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 1;
        *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) = reinterpret_cast<uintptr_t>(actors[i].bytes.data());
    }
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
    ok = ok && remaining_detour(cmd) == 0;
    actors[0].dead = true; ok = ok && remaining_detour(cmd) == 0;
    actors[1].dead = true; ok = ok && remaining_detour(cmd) == 1;
    remaining_weights.erase(cmd);
    *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 5;
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 3;
    for (unsigned i = 0; i < 5; ++i) {
        actors[i].dead = false;
        actors[i].name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i % 3) +
            "_q0000000200000003" + (i == 4 ? "_w00000001" : "") +
            "_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100";
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 1;
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 1;
        *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) = reinterpret_cast<uintptr_t>(actors[i].bytes.data());
    }
    ok = ok && remaining_detour(cmd) == 1;
    actors[4].dead = true; *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
    ok = ok && remaining_detour(cmd) == 0;
    actors[1].dead = true; ok = ok && remaining_detour(cmd) == 1;
    stopped.store(true); ok = ok && remaining_detour(cmd) == 42; stopped.store(false);
    *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 2;
    actors[0].name = actors[1].name = "ai/fodder/imp";
    for (unsigned i = 0; i < 2; ++i) {
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 101;
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 101;
    }
    ok = ok && remaining_detour(cmd) == 42 && native_calls == 2;
    const auto previous_kill = original_kill;
    const auto previous_kill_dead = original_kill_dead;
    const auto previous_maintain_cast = cast_maintain, previous_stagger_cast = cast_stagger;
    const auto previous_health = original_health;
    const auto previous_health_added = original_health_added;
    const auto previous_health_damage = original_health_damage;
    auto previous_health_progress = std::move(health_progress);
    auto previous_kills = std::move(kill_progress);
    auto previous_recipe_state = std::move(recipe_state);
    std::array<uint8_t, 0x140> future_worker{};
    uintptr_t future_commands[]{reinterpret_cast<uintptr_t>(future_worker.data())};
    *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = 16;
    cast_maintain = [](uintptr_t worker) { return worker; };
    cast_stagger = [](uintptr_t) -> uintptr_t { return 0; };
    original_kill = [](uintptr_t address) -> uint64_t {
        return native_future(*reinterpret_cast<uintptr_t*>(address + 0x40)) ? 0 : 42;
    };
    original_kill_dead = [](uintptr_t command_address, uintptr_t record) {
        if (*reinterpret_cast<uint64_t*>(record) & *reinterpret_cast<uint64_t*>(command_address + 0x88))
            ++*reinterpret_cast<int32_t*>(command_address + 0xa8);
    };
    std::array<uint8_t, 0xb0> kill_command{};
    const auto kill = reinterpret_cast<uintptr_t>(kill_command.data());
    *reinterpret_cast<uintptr_t*>(kill + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
    *reinterpret_cast<uint64_t*>(kill + 0x88) = 16;
    *reinterpret_cast<int32_t*>(kill + 0xac) = 1;
    *reinterpret_cast<uintptr_t*>(manager.data() + 0xc80) = reinterpret_cast<uintptr_t>(records.data());
    *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 6;
    *reinterpret_cast<uintptr_t*>(manager.data() + 0xcc0) = reinterpret_cast<uintptr_t>(future_commands);
    *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 1;
    for (unsigned i = 0; i < 6; ++i) {
        actors[i].dead = false; actors[i].health = 100;
        actors[i].name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i) +
            "_q0000000200000006_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100";
        *reinterpret_cast<uint64_t*>(records.data() + i * 0x68) = 16;
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 1;
        *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 1;
        *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) = reinterpret_cast<uintptr_t>(actors[i].bytes.data());
    }
    initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
    ok = ok && kill_detour(kill) == 0;
    for (unsigned i : {5u, 1u, 3u}) {
        auto* record = records.data() + i * 0x68;
        *reinterpret_cast<uint64_t*>(record) = 32;
        kill_dead_detour(kill, reinterpret_cast<uintptr_t>(record)); actors[i].dead = true;
        *reinterpret_cast<uint64_t*>(record) = 16;
        ok = ok && kill_detour(kill) == (i == 3 ? 1u : 0u);
    }
    *reinterpret_cast<uint64_t*>(kill + 0x88) = 32;
    const auto seen = *reinterpret_cast<int32_t*>(kill + 0xa8);
    *reinterpret_cast<uint64_t*>(records.data()) = 32;
    kill_dead_detour(kill, reinterpret_cast<uintptr_t>(records.data()));
    ok = ok && *reinterpret_cast<int32_t*>(kill + 0xa8) == seen;
    *reinterpret_cast<uint64_t*>(records.data()) = 16;
    *reinterpret_cast<uint64_t*>(kill + 0x88) = 16;
    *reinterpret_cast<int32_t*>(kill + 0xac) = 2;
    ok = ok && kill_detour(kill) == 0;
    *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 0;
    ok = ok && kill_detour(kill) == 0;
    *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 0;
    ok = ok && kill_detour(kill) == 1;
    recipe_state.reset("plan", "map");
    kill_progress.erase(kill); *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
    *reinterpret_cast<int32_t*>(kill + 0xac) = 5;
    *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 1;
    initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
    ok &= recipe_state.request("torch", {"ai/fodder/imp", "origin", "", 16, 3, 3, 0});
    for (unsigned i = 0; i < 5; ++i) {
        const auto emitted = recipe_state.reserve("torch");
        ok &= emitted && recipe_state.created("torch", *emitted) &&
            recipe_state.actor_changed(emitted->actor, RecipeActorState::dead) &&
            !recipe_state.actor_changed(emitted->actor, RecipeActorState::dead);
        if (emitted && i % 2 == 0) {
            const auto actor = reinterpret_cast<uintptr_t>(actors[0].bytes.data());
            recipe_actors[actor] = emitted->actor;
            recipe_actor_credits[actor] = {1, {1, 1}, "torch"};
            *reinterpret_cast<uint64_t*>(records.data()) = 32;
            kill_dead_detour(kill, reinterpret_cast<uintptr_t>(records.data()));
            recipe_actors.erase(actor); recipe_actor_credits.erase(actor);
            *reinterpret_cast<uint64_t*>(records.data()) = 16;
        }
        ok &= kill_detour(kill) == (i == 4 ? 1u : 0u) &&
            *reinterpret_cast<int32_t*>(kill + 0xa8) == static_cast<int32_t>(i + 1);
    }
    kill_progress.erase(kill);
    initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
    ok &= kill_detour(kill) == 1;
    *reinterpret_cast<int32_t*>(kill + 0xac) = 2;
    *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
    recipe_state.reset("plan", "map");
    kill_progress.erase(kill); *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
    initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
    ok = ok && recipe_state.request("empty", {"ai/fodder/imp", "origin", "", 16, 2, 0, 0}) && recipe_state.complete_empty("empty");
    *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 1;
    ok = ok && kill_detour(kill) == 1;
    kill_progress.erase(kill); *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
    initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
    ok = ok && kill_detour(kill) == 0;
    recipe_state.reset("plan", "map");
    bool scope_ok = true;
    const uint64_t hunter_mask = uint64_t{1} << 26;
    scope_ok &= recipe_state.request("hunter", {"ai/fodder/imp", "origin", "", hunter_mask, 1, 1, 1});
    const auto hunter = recipe_state.reserve("hunter");
    scope_ok &= hunter && recipe_state.created("hunter", *hunter) && recipe_state.actor_changed(hunter->actor, RecipeActorState::dead);
    *reinterpret_cast<uint64_t*>(kill + 0x88) = hunter_mask;
    *reinterpret_cast<int32_t*>(kill + 0xac) = 2;
    *reinterpret_cast<int32_t*>(kill + 0xa8) = 1;
    *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = 16;
    *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 1;
    actors[0].dead = false;
    *reinterpret_cast<uint64_t*>(records.data()) = 16;
    kill_progress.erase(kill); initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
    scope_ok &= kill_detour(kill) == 1;
    *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = hunter_mask;
    scope_ok &= kill_detour(kill) == 0;
    *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = 16;
    *reinterpret_cast<uint64_t*>(records.data()) = hunter_mask;
    scope_ok &= kill_detour(kill) == 0;
    actors[0].dead = true;
    scope_ok &= kill_detour(kill) == 1;
    *reinterpret_cast<uint64_t*>(records.data()) = 16;
    *reinterpret_cast<save::NativeString*>(kill + 0x58) = {0, const_cast<char*>("unrelated"), 9, 0x40000000U, {}};
    kill_progress.erase(kill); initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
    scope_ok &= kill_detour(kill) == 0;
    *reinterpret_cast<save::NativeString*>(kill + 0x58) = {};
    *reinterpret_cast<uint64_t*>(kill + 0x88) = 16;
    *reinterpret_cast<uint64_t*>(records.data()) = 16;
    *reinterpret_cast<int32_t*>(kill + 0xac) = 1;
    recipe_state.reset("plan", "map");
    std::array<uint8_t, 0xd0> health_command{};
    std::array<uint8_t, 0x68 * 6> health_records{};
    const auto health = reinterpret_cast<uintptr_t>(health_command.data());
    *reinterpret_cast<uintptr_t*>(health + 0xa8) = reinterpret_cast<uintptr_t>(health_records.data());
    *reinterpret_cast<float*>(health + 0xc0) = 0.5f;
    original_health_added = [](uintptr_t command_address, uintptr_t record) {
        auto& count = *reinterpret_cast<int32_t*>(command_address + 0xb0);
        auto* data = *reinterpret_cast<uint8_t**>(command_address + 0xa8);
        std::memcpy(data + count * 0x68, reinterpret_cast<void*>(record), 0x68);
        *reinterpret_cast<float*>(data + count * 0x68 + 100) = 100;
        *reinterpret_cast<float*>(command_address + 0xc8) += 100; ++count;
    };
    for (auto& actor : actors) {
        actor.table[0x6f0 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t) { return 100.0f; });
        actor.table[0x6e8 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) {
            for (const auto& value : actors) if (address == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.health;
            return 0.0f;
        });
    }
    original_health_damage = [](uintptr_t command_address, uintptr_t record, float amount) {
        auto* data = *reinterpret_cast<uint8_t**>(command_address + 0xa8);
        const auto count = *reinterpret_cast<int32_t*>(command_address + 0xb0);
        for (int32_t i = 0; i < count; ++i) if (*reinterpret_cast<uint32_t*>(data + i * 0x68 + 12) ==
            *reinterpret_cast<uint32_t*>(record + 12)) {
            *reinterpret_cast<float*>(data + i * 0x68 + 100) -= amount;
            *reinterpret_cast<float*>(command_address + 0xc4) += amount; return;
        }
    };
    original_health = [](uintptr_t command_address) -> uint64_t {
        auto* data = *reinterpret_cast<uint8_t**>(command_address + 0xa8);
        auto& count = *reinterpret_cast<int32_t*>(command_address + 0xb0);
        for (int32_t i = count; i-- > 0;) if (!*reinterpret_cast<uintptr_t*>(data + i * 0x68 + 16)) {
            *reinterpret_cast<float*>(command_address + 0xc4) += *reinterpret_cast<float*>(data + i * 0x68 + 100);
            --count; std::memmove(data + i * 0x68, data + (i + 1) * 0x68, static_cast<size_t>(count - i) * 0x68);
        }
        const auto initial = *reinterpret_cast<float*>(command_address + 0xc8);
        return (initial - *reinterpret_cast<float*>(command_address + 0xc4)) / initial <=
            *reinterpret_cast<float*>(command_address + 0xc0);
    };
    *reinterpret_cast<uintptr_t*>(health + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
    *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 6;
    for (unsigned i = 0; i < 6; ++i) health_added_detour(health, reinterpret_cast<uintptr_t>(records.data() + i * 0x68));
    ok = ok && *reinterpret_cast<float*>(health + 0xc8) == 600;
    actors[0].health = 0;
    health_damage_detour(health, reinterpret_cast<uintptr_t>(records.data()), 100);
    actors[1].health = 0;
    health_damage_detour(health, reinterpret_cast<uintptr_t>(records.data() + 0x68), 100);
    ok = ok && health_detour(health) == 0 && *reinterpret_cast<float*>(health_records.data() + 2 * 0x68 + 100) == 100;
    *reinterpret_cast<uintptr_t*>(health_records.data() + 5 * 0x68 + 16) = 0;
    *reinterpret_cast<uint32_t*>(health_records.data() + 5 * 0x68 + 8) = 0x1fffffe;
    *reinterpret_cast<uint32_t*>(health_records.data() + 5 * 0x68 + 12) = 0x1fffffe;
    ok = ok && health_detour(health) == 1 && *reinterpret_cast<int32_t*>(health + 0xb0) == 5 &&
        *reinterpret_cast<float*>(health_records.data() + 2 * 0x68 + 100) == 100;
    for (auto& [identifier, item] : health_progress[health].actors) { (void)identifier; item.weight.credit = {0, 1}; }
    health_progress[health].retired.clear(); health_progress[health].retired[{0, 1, 0}] = 1;
    ok = ok && health_detour(health) == 1;
    {
        const auto saved_register = original_register_spawn;
        const auto saved_added = original_remaining_added;
        const auto saved_dead = original_remaining_dead;
        const auto saved_refresh = refresh_entity;
        const auto saved_health_added = original_health_added;
        const auto saved_remaining = original_remaining;
        original_remaining = [](uintptr_t wait) -> uint64_t { return !*reinterpret_cast<const int32_t*>(wait + 0xb0); };
        static RemainingEvent append_health;
        append_health = saved_health_added;
        refresh_entity = [](uintptr_t reference, uintptr_t) {
            const auto id = *reinterpret_cast<const uint32_t*>(reference);
            if (id && id <= actors.size())
                *reinterpret_cast<uintptr_t*>(reference + 8) = reinterpret_cast<uintptr_t>(actors[id - 1].bytes.data());
        };
        original_register_spawn = [](uintptr_t owner, uint64_t* record) {
            const auto actor = record_actor(reinterpret_cast<uintptr_t>(record));
            if (!actor) return;
            auto& count = *reinterpret_cast<int32_t*>(owner + 0xc88);
            const auto data = *reinterpret_cast<uintptr_t*>(owner + 0xc80);
            std::memcpy(reinterpret_cast<void*>(data + count++ * 0x68), record, 0x68);
        };
        const auto selected = +[](uintptr_t wait, uintptr_t record) {
            const auto mask = *reinterpret_cast<const uint64_t*>(wait + 0x88);
            const auto filter = reinterpret_cast<const save::NativeString*>(wait + 0x58);
            std::string label;
            return (*reinterpret_cast<const uint64_t*>(record) & (mask & (uint64_t{1} << 44) ? encounter_ai_mask : mask)) &&
                (!filter->length || (native_text(record + 0x28, label, 1024) && label == filter->data));
        };
        static bool (*select_record)(uintptr_t, uintptr_t);
        static RemainingEvent remove_record;
        select_record = selected; remove_record = saved_dead;
        original_remaining_added = [](uintptr_t wait, uintptr_t record) {
            if (!select_record(wait, record)) return;
            auto& count = *reinterpret_cast<int32_t*>(wait + 0xb0);
            const auto data = *reinterpret_cast<uintptr_t*>(wait + 0xa8);
            std::memcpy(reinterpret_cast<void*>(data + count++ * 0x68), reinterpret_cast<const void*>(record), 0x68);
        };
        original_remaining_dead = [](uintptr_t wait, uintptr_t record) {
            if (select_record(wait, record)) remove_record(wait, record);
        };
        original_health_added = [](uintptr_t wait, uintptr_t record) {
            if (select_record(wait, record)) append_health(wait, record);
        };
        const uint64_t arachnotron = uint64_t{1} << 22, spectre = uint64_t{1} << 17,
            cacodemon = uint64_t{1} << 18, hell_knight = uint64_t{1} << 14, spirit = uint64_t{1} << 40,
            flag = uint64_t{1} << 60;
        const std::string encounter_label = "class_selection", other_label = "other_label";
        const auto text = [](const std::string& value) -> save::NativeString {
            return {0, const_cast<char*>(value.c_str()), static_cast<int32_t>(value.size()), 0x40000000U, {}};
        };
        const auto body_name = [](unsigned origin_index, unsigned body, unsigned count, uint64_t source, uint64_t physical) {
            char suffix[160];
            sprintf_s(suffix, "_%08x_q00000001%08x_t%016llx%016llx_l00000001000000010000000100000002_kn__ap_v070_s00100",
                body, count, source, physical);
            return "ap_v070_ai_r" + std::string(20, static_cast<char>('a' + origin_index)) + suffix;
        };
        std::array<uint8_t, 0xdb8> other_manager{};
        std::array<std::array<uint64_t, 13>, 7> registered{};
        std::array<std::array<uint64_t, 13>, 6> admitted{};
        std::array<uint64_t, 13> other_admitted{};
        const auto owner = reinterpret_cast<uintptr_t>(manager.data());
        const auto other_owner = reinterpret_cast<uintptr_t>(other_manager.data());
        const auto old_records = *reinterpret_cast<uintptr_t*>(owner + 0xc80);
        const auto old_count = *reinterpret_cast<int32_t*>(owner + 0xc88);
        *reinterpret_cast<uintptr_t*>(owner + 0xc80) = reinterpret_cast<uintptr_t>(admitted.data());
        *reinterpret_cast<int32_t*>(owner + 0xc88) = 0;
        *reinterpret_cast<uintptr_t*>(other_owner + 0xc80) = reinterpret_cast<uintptr_t>(other_admitted.data());
        const uint64_t physical[]{spectre, cacodemon, spectre, cacodemon, arachnotron, arachnotron};
        for (unsigned i = 0; i < actors.size(); ++i) {
            actors[i].dead = false; actors[i].health = 100;
            actors[i].name = body_name(i < 2 ? i : i < 4 ? 2 : i, i == 3 ? 1 : 0, i == 2 || i == 3 ? 2 : 1,
                i == 4 ? spirit : arachnotron, physical[i]);
        }
        for (unsigned i = 0; i < registered.size(); ++i) {
            auto& record = registered[i];
            const auto actor_index = std::min(i, 5u);
            record[0] = physical[actor_index] | flag;
            record[1] = uint64_t{actor_index + 1} << 32 | (actor_index + 1);
            *reinterpret_cast<save::NativeString*>(reinterpret_cast<uintptr_t>(record.data()) + 0x28) =
                text(i == 5 ? other_label : encounter_label);
            ok &= !record[2];
            register_spawn_detour(i == 6 ? other_owner : owner, record.data());
            ok &= record[2] == reinterpret_cast<uintptr_t>(actors[actor_index].bytes.data()) &&
                record[0] == (flag | (i == 4 ? spirit : arachnotron));
        }
        std::array<uint8_t, 0xc8> class_wait{}, any_wait{};
        std::array<std::array<uint64_t, 13>, 6> class_records{}, any_records{};
        const auto class_command = reinterpret_cast<uintptr_t>(class_wait.data());
        const auto any_command = reinterpret_cast<uintptr_t>(any_wait.data());
        const auto build_wait = [&](uintptr_t wait, uint64_t mask, uintptr_t data) {
            *reinterpret_cast<uintptr_t*>(wait + 0x40) = owner;
            *reinterpret_cast<uint64_t*>(wait + 0x88) = mask;
            *reinterpret_cast<save::NativeString*>(wait + 0x58) = text(encounter_label);
            *reinterpret_cast<uintptr_t*>(wait + 0xa8) = data;
            for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(owner + 0xc88); ++i)
                remaining_added_detour(wait, reinterpret_cast<uintptr_t>(admitted[i].data()));
        };
        build_wait(class_command, arachnotron, reinterpret_cast<uintptr_t>(class_records.data()));
        build_wait(any_command, uint64_t{1} << 44, reinterpret_cast<uintptr_t>(any_records.data()));
        ok &= *reinterpret_cast<int32_t*>(class_command + 0xb0) == 4 &&
            *reinterpret_cast<int32_t*>(any_command + 0xb0) == 5 &&
            remaining_detour(class_command) == 0 && remaining_detour(any_command) == 0;
        std::vector<RecipeCredit> selected_total, selected_living;
        std::vector<RemainingItem> selected_items;
        bool tracked = false, blocked = false;
        int32_t threshold = 0;
        ok &= remaining_inputs(class_command, selected_total, selected_living, selected_items, tracked, blocked, threshold) &&
            tracked && credit_at_least(selected_total, 3) && credit_at_most(selected_total, 3);
        for (unsigned i : {0u, 2u, 1u, 3u}) {
            actors[i].dead = true;
            const auto record = reinterpret_cast<uintptr_t>(admitted[i].data());
            remaining_dead_detour(class_command, record); remaining_dead_detour(any_command, record);
            ok &= remaining_detour(class_command) == (i == 3 ? 1u : 0u) && remaining_detour(any_command) == 0;
        }
        ok &= !actors[4].dead && !actors[5].dead && !*reinterpret_cast<int32_t*>(class_command + 0xb0);
        actors[4].dead = true;
        remaining_dead_detour(any_command, reinterpret_cast<uintptr_t>(admitted[4].data()));
        ok &= remaining_detour(any_command) == 1;
        health_progress.erase(health); health_records.fill(0);
        *reinterpret_cast<int32_t*>(health + 0xb0) = 0;
        *reinterpret_cast<float*>(health + 0xc8) = *reinterpret_cast<float*>(health + 0xc4) = 0;
        *reinterpret_cast<float*>(health + 0xc0) = 0.25f;
        *reinterpret_cast<uint64_t*>(health + 0x88) = hell_knight;
        *reinterpret_cast<save::NativeString*>(health + 0x58) = text(encounter_label);
        *reinterpret_cast<int32_t*>(owner + 0xc88) = 0;
        for (unsigned i = 0; i < 2; ++i) {
            actors[i].dead = false; actors[i].health = 100;
            actors[i].name = body_name(i, 0, 1, i ? spirit : hell_knight, i ? hell_knight : spectre);
            auto& record = registered[i]; record[0] = i ? hell_knight : spectre; record[2] = 0;
            register_spawn_detour(owner, record.data());
            health_added_detour(health, reinterpret_cast<uintptr_t>(admitted[i].data()));
        }
        ok &= *reinterpret_cast<int32_t*>(health + 0xb0) == 1 && health_progress[health].actors.count(1) == 1 &&
            health_progress[health].actors.count(2) == 0 && health_detour(health) == 0;
        actors[0].health = 25;
        health_damage_detour(health, reinterpret_cast<uintptr_t>(admitted[0].data()), 75);
        ok &= health_detour(health) == 1 && actors[1].health == 100;
        {
            static std::array<Actor, 9> gate_actors;
            static std::array<std::array<uint64_t, 13>, 9> gate_registered;
            static uintptr_t gate_owner, gate_alias;
            static std::string gate_alias_name;
            static unsigned gate_emissions;
            static bool gate_valid;
            const auto gate_name = definition_name;
            const auto gate_refresh = refresh_entity;
            const auto gate_world_global = world_global;
            const auto gate_find = find_entity;
            const auto gate_resolve = resolve_actor;
            const auto gate_tick = original_maintain_tick;
            auto gate_state = std::move(recipe_state);
            auto gate_recipes = std::move(native_recipes);
            auto gate_workers = std::move(worker_recipes);
            auto gate_roles = std::move(recipe_actors);
            auto gate_credits = std::move(recipe_actor_credits);
            auto gate_diagnostics = std::move(worker_diagnostics);
            const auto gate_next = next_recipe_request;
            const auto gate_trace_count = worker_trace_count;
            const auto gate_fault = recipe_fault.exchange(false);
            const uint64_t soldier = uint64_t{1} << 9, gargoyle = uint64_t{1} << 6, imp = uint64_t{1} << 4,
                shotgun_soldier = uint64_t{1} << 10, cyber_mancubus = uint64_t{1} << 21,
                pinky = uint64_t{1} << 16, baron = uint64_t{1} << 28;
            const uint64_t gate_physical[]{soldier, gargoyle, imp, gargoyle, cyber_mancubus, soldier, shotgun_soldier, spectre, baron};
            const std::string pool_origin(20, 'd'), knight_origin(20, 'e'), pinky_origin(20, 'f');
            std::array<uint8_t, 0xe30> gate_manager{};
            std::array<uint8_t, 0x140> gate_worker{};
            uintptr_t alias_storage = 0;
            gate_owner = reinterpret_cast<uintptr_t>(gate_manager.data());
            gate_alias = reinterpret_cast<uintptr_t>(&alias_storage);
            gate_alias_name = "ap_v070_request_r" + pool_origin;
            gate_emissions = 0; gate_valid = true;
            definition_name = [](uintptr_t entity) -> const char* {
                if (entity == gate_owner) return "slayer_gate";
                if (entity == gate_alias) return gate_alias_name.c_str();
                for (const auto& actor : gate_actors)
                    if (entity == reinterpret_cast<uintptr_t>(actor.bytes.data())) return actor.name.c_str();
                return "native_ai";
            };
            refresh_entity = [](uintptr_t reference, uintptr_t) {
                const auto id = *reinterpret_cast<const uint32_t*>(reference);
                if (id && id <= gate_actors.size())
                    *reinterpret_cast<uintptr_t*>(reference + 8) = reinterpret_cast<uintptr_t>(gate_actors[id - 1].bytes.data());
            };
            resolve_actor = [](uintptr_t reference) -> uintptr_t {
                const auto id = *reinterpret_cast<const uint32_t*>(reference);
                return id && id <= gate_actors.size() ? reinterpret_cast<uintptr_t>(gate_actors[id - 1].bytes.data()) : 0;
            };
            find_entity = [](uintptr_t, const char*) -> uintptr_t { return 0; };
            original_remaining = [](uintptr_t wait) -> uint64_t {
                return !*reinterpret_cast<const int32_t*>(wait + 0xb0) &&
                    !native_future(*reinterpret_cast<const uintptr_t*>(wait + 0x40),
                        *reinterpret_cast<const uint64_t*>(wait + 0x88), wait + 0x58);
            };
            auto gate_world = std::make_unique<std::array<uint8_t, 0x126680>>();
            const auto gate_world_address = reinterpret_cast<uintptr_t>(gate_world->data());
            world_global = reinterpret_cast<uintptr_t>(&gate_world_address);
            std::array<std::array<uint64_t, 3>, 9> gate_references{};
            struct { uintptr_t data; int32_t count, capacity; uint32_t flags; } gate_collection{
                reinterpret_cast<uintptr_t>(gate_references.data()), 9, 9, 0x1050001};
            std::array<uintptr_t, 4> gate_managers{0, gate_owner};
            std::array<std::array<uint64_t, 13>, 9> gate_admitted{};
            uintptr_t gate_commands[]{reinterpret_cast<uintptr_t>(gate_worker.data())};
            *reinterpret_cast<uintptr_t*>(gate_world_address + 0x62088) = reinterpret_cast<uintptr_t>(&gate_collection);
            *reinterpret_cast<uintptr_t*>(gate_world_address + 0x126658) = reinterpret_cast<uintptr_t>(gate_managers.data());
            *reinterpret_cast<int32_t*>(gate_world_address + 0x126660) = 1;
            *reinterpret_cast<uintptr_t*>(gate_owner + 0xc80) = reinterpret_cast<uintptr_t>(gate_admitted.data());
            *reinterpret_cast<uintptr_t*>(gate_owner + 0xcc0) = reinterpret_cast<uintptr_t>(gate_commands);
            *reinterpret_cast<int32_t*>(gate_owner + 0xcc8) = 1;
            const auto worker = reinterpret_cast<uintptr_t>(gate_worker.data());
            *reinterpret_cast<uintptr_t*>(worker + 0x40) = gate_owner;
            *reinterpret_cast<uintptr_t*>(worker + 0xd8) = gate_alias;
            *reinterpret_cast<uint64_t*>(worker + 0x88) = soldier;
            *reinterpret_cast<save::NativeString*>(worker + 0x58) = text(encounter_label);
            auto& pool = native_recipes[pool_origin];
            pool.definition.origin = pool_origin; pool.definition.owner = "slayer_gate";
            pool.definition.family = 'm'; pool.definition.logical_mask = soldier;
            pool.definition.logical_count = pool.definition.physical_count = 4;
            pool.bodies.resize(4);
            ok &= recipe_state.reset("plan", "slayer_gate") &&
                recipe_state.request("gate_knights", {"slayer_gate", knight_origin, encounter_label, hell_knight, 3, 3, 3}) &&
                recipe_state.request("gate_pinkies", {"slayer_gate", pinky_origin, encounter_label, pinky, 2, 2, 2});
            for (unsigned i = 0; i < gate_actors.size(); ++i) {
                auto& actor = gate_actors[i]; actor.dead = actor.ignored = false; actor.bytes.fill(0);
                actor.table[0x88 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) -> uint32_t {
                    for (uint32_t index = 0; index < gate_actors.size(); ++index)
                        if (address == reinterpret_cast<uintptr_t>(gate_actors[index].bytes.data())) return index + 1;
                    return 0;
                });
                actor.table[0x108 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) {
                    for (const auto& value : gate_actors)
                        if (address == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.dead;
                    return false;
                });
                *reinterpret_cast<uintptr_t*>(actor.bytes.data()) = reinterpret_cast<uintptr_t>(actor.table.data());
                const auto base = i < 4 ? 0u : i < 7 ? 4u : 7u;
                const auto count = i < 4 ? 4u : i < 7 ? 3u : 2u;
                const auto source = i < 4 ? soldier : i < 7 ? hell_knight : pinky;
                actor.name = body_name(i < 4 ? 3 : i < 7 ? 4 : 5, i - base, count, source, gate_physical[i]);
                char units[9]; sprintf_s(units, "%08x", count);
                actor.name.replace(actor.name.find("_q") + 2, 8, units);
                auto& record = gate_registered[i]; record.fill(0);
                record[0] = gate_physical[i] | flag;
                record[1] = uint64_t{i + 1} << 32 | (i + 1);
                *reinterpret_cast<save::NativeString*>(reinterpret_cast<uintptr_t>(record.data()) + 0x28) = text(encounter_label);
                gate_references[i][0] = i + 1;
            }
            static bool (*emit_gate_body)(const std::string&, size_t);
            emit_gate_body = +[](const std::string& key, size_t index) {
                const auto emission = recipe_state.reserve(key);
                if (!emission) return false;
                const auto actor = reinterpret_cast<uintptr_t>(gate_actors[index].bytes.data());
                gate_valid &= recipe_state.created(key, *emission) && !gate_registered[index][2];
                recipe_actors[actor] = emission->actor;
                recipe_actor_credits[actor] = {static_cast<uint32_t>(index + 1), {1, 1}, key, "", emission->ordinal};
                register_spawn_detour(gate_owner, gate_registered[index].data());
                ++gate_emissions;
                return true;
            };
            original_maintain_tick = [](uintptr_t) {
                const auto& progress = recipe_state.requests().at(*worker_recipe);
                if (progress.actors.size() >= 4) return;
                if (progress.emitted >= 4) { gate_valid = false; return; }
                gate_valid &= emit_gate_body(*worker_recipe, static_cast<size_t>(progress.emitted));
            };
            const auto tick_gate_workers = [&] {
                for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(gate_owner + 0xcc8); ++i)
                    maintain_tick_detour(gate_commands[i]);
            };
            for (unsigned i = 0; i < 4; ++i) tick_gate_workers();
            const auto pool_key = worker_recipes.at(worker);
            for (unsigned i = 4; i < 9; ++i) ok &= emit_gate_body(i < 7 ? "gate_knights" : "gate_pinkies", i);
            ok &= gate_valid && gate_emissions == 9 && recipe_state.requests().at(pool_key).emitted == 4 &&
                !recipe_state.requests().at(pool_key).request.maximum_emissions && !recipe_state.requests().at(pool_key).closed &&
                *reinterpret_cast<int32_t*>(worker + 0x90) == 4 && !*reinterpret_cast<int32_t*>(worker + 0x94) &&
                *reinterpret_cast<uint64_t*>(worker + 0x88) == soldier;
            for (unsigned i = 0; i < 9; ++i)
                ok &= gate_registered[i][2] == reinterpret_cast<uintptr_t>(gate_actors[i].bytes.data()) &&
                    gate_registered[i][0] == (flag | (i < 4 ? soldier : i < 7 ? hell_knight : pinky));
            class_wait.fill(0); any_wait.fill(0);
            remaining_weights.erase(class_command); remaining_weights.erase(any_command);
            const auto build_gate_wait = [&](uintptr_t wait, uint64_t mask, uintptr_t data) {
                *reinterpret_cast<uintptr_t*>(wait + 0x40) = gate_owner;
                *reinterpret_cast<uint64_t*>(wait + 0x88) = mask;
                *reinterpret_cast<uintptr_t*>(wait + 0xa8) = data;
                for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(gate_owner + 0xc88); ++i)
                    remaining_added_detour(wait, reinterpret_cast<uintptr_t>(gate_admitted[i].data()));
            };
            build_gate_wait(class_command, hell_knight | pinky, reinterpret_cast<uintptr_t>(class_records.data()));
            ok &= *reinterpret_cast<int32_t*>(class_command + 0xb0) == 5 && remaining_detour(class_command) == 0 &&
                !native_future(gate_owner, hell_knight | pinky) && native_future(gate_owner);
            const auto kill_gate_body = [&](unsigned index, uintptr_t wait) {
                const auto actor = reinterpret_cast<uintptr_t>(gate_actors[index].bytes.data());
                gate_actors[index].dead = true; gate_references[index][0] = 0;
                record_recipe_death(actor);
                remaining_dead_detour(wait, reinterpret_cast<uintptr_t>(gate_registered[index].data()));
                auto& count = *reinterpret_cast<int32_t*>(gate_owner + 0xc88);
                for (int32_t i = 0; i < count; ++i) if (gate_admitted[i][1] == gate_registered[index][1]) {
                    --count;
                    std::memmove(gate_admitted.data() + i, gate_admitted.data() + i + 1, static_cast<size_t>(count - i) * 0x68);
                    break;
                }
            };
            for (unsigned i : {4u, 7u, 5u, 8u, 6u}) {
                kill_gate_body(i, class_command);
                tick_gate_workers();
                ok &= remaining_detour(class_command) == (i == 6 ? 1u : 0u) && gate_emissions == 9;
            }
            ok &= *reinterpret_cast<int32_t*>(gate_owner + 0xc88) == 4 &&
                std::none_of(gate_actors.begin(), gate_actors.begin() + 4, [](const auto& actor) { return actor.dead; }) &&
                !recipe_state.requests().at(pool_key).closed && native_future(gate_owner) && worker_recipes.at(worker) == pool_key;
            *reinterpret_cast<int32_t*>(gate_owner + 0xcc8) = 0;
            for (unsigned retry = 0; retry < 3; ++retry) {
                observe(gate_world_address); tick_gate_workers();
                ok &= worker_recipes.empty() && recipe_state.requests().at(pool_key).closed &&
                    recipe_state.requests().at(pool_key).emitted == 4 && !emit_gate_body(pool_key, 0) && gate_emissions == 9 &&
                    recipe_state.requests().size() == 3 && !native_future(gate_owner) &&
                    credit_at_least(recipe_state.deaths("slayer_gate", hell_knight | pinky, encounter_label), 5);
            }
            build_gate_wait(any_command, uint64_t{1} << 44, reinterpret_cast<uintptr_t>(any_records.data()));
            ok &= *reinterpret_cast<int32_t*>(any_command + 0xb0) == 4 && remaining_detour(any_command) == 0;
            for (unsigned i = 0; i < 4; ++i) {
                kill_gate_body(i, any_command); observe(gate_world_address); tick_gate_workers();
                ok &= remaining_detour(any_command) == (i == 3 ? 1u : 0u) && gate_emissions == 9;
            }
            ok &= !recipe_fault.load() && gate_valid && recipe_state.requests().at(pool_key).dead == 4 &&
                credit_at_least(recipe_state.deaths("slayer_gate", soldier | hell_knight | pinky, encounter_label), 9);
            recipe_state = std::move(gate_state); native_recipes = std::move(gate_recipes); worker_recipes = std::move(gate_workers);
            recipe_actors = std::move(gate_roles); recipe_actor_credits = std::move(gate_credits); worker_diagnostics = std::move(gate_diagnostics);
            next_recipe_request = gate_next; worker_trace_count = gate_trace_count; recipe_fault.store(gate_fault);
            definition_name = gate_name; refresh_entity = gate_refresh; world_global = gate_world_global;
            find_entity = gate_find; resolve_actor = gate_resolve; original_maintain_tick = gate_tick;
        }
        remaining_weights.erase(class_command); remaining_weights.erase(any_command); health_progress.erase(health);
        *reinterpret_cast<uintptr_t*>(owner + 0xc80) = old_records;
        *reinterpret_cast<int32_t*>(owner + 0xc88) = old_count;
        original_register_spawn = saved_register; original_remaining_added = saved_added;
        original_remaining_dead = saved_dead; refresh_entity = saved_refresh; original_health_added = saved_health_added;
        original_remaining = saved_remaining;
    }
    auto previous_recipes = std::move(native_recipes);
    auto previous_workers = std::move(worker_recipes);
    const std::string remainder_origin = "0123456789abcdef0123", remainder_label = "remainder";
    native_recipes[remainder_origin].definition.family = 'g';
    native_recipes[remainder_origin].bodies.resize(6);
    recipe_state.reset("plan", "map");
    ok = ok && recipe_state.request("remainder", {"ai/fodder/imp", remainder_origin, remainder_label, 16, 2, 6, 6, 6, 2});
    const auto emitted = recipe_state.reserve("remainder");
    ok = ok && emitted && recipe_state.created("remainder", *emitted);
    worker_recipes[reinterpret_cast<uintptr_t>(future_worker.data())] = "remainder";
    remaining_weights.erase(cmd); actors[0].dead = false;
    *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 1;
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
    *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16;
    *reinterpret_cast<save::NativeString*>(command.data() + 0x58) =
        {0, const_cast<char*>(remainder_label.c_str()), static_cast<int32_t>(remainder_label.size()), 0x40000000U, {}};
    ok = ok && remaining_detour(cmd) == 0;
    actors[0].dead = true;
    ok = ok && remaining_detour(cmd) == 0;
    remaining_weights.erase(cmd); *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 0;
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 0;
    ok = ok && remaining_detour(cmd) == 0;
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
    ok = ok && remaining_detour(cmd) == 1;
    *reinterpret_cast<save::NativeString*>(command.data() + 0x58) =
        {0, const_cast<char*>("unrelated"), 9, 0x40000000U, {}};
    ok = ok && remaining_detour(cmd) == 42;
    *reinterpret_cast<save::NativeString*>(command.data() + 0x58) =
        {0, const_cast<char*>(remainder_label.c_str()), static_cast<int32_t>(remainder_label.size()), 0x40000000U, {}};
    health_records.fill(0); *reinterpret_cast<int32_t*>(health + 0xb0) = 0;
    *reinterpret_cast<float*>(health + 0xc8) = *reinterpret_cast<float*>(health + 0xc4) = 0;
    *reinterpret_cast<float*>(health + 0xc0) = 0.9f;
    *reinterpret_cast<uint64_t*>(health + 0x88) = 16;
    *reinterpret_cast<save::NativeString*>(health + 0x58) =
        {0, const_cast<char*>(remainder_label.c_str()), static_cast<int32_t>(remainder_label.size()), 0x40000000U, {}};
    *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 1;
    actors[0].dead = false; actors[0].health = 100;
    native_recipes[remainder_origin].definition.hitpoints.assign(6, {1000, 1000});
    native_recipes[remainder_origin].definition.hitpoints[0] = {100, 100};
    ok = ok && health_detour(health) == 0;
    health_added_detour(health, reinterpret_cast<uintptr_t>(records.data()));
    actors[0].health = 0;
    health_damage_detour(health, reinterpret_cast<uintptr_t>(records.data()), 100);
    ok = ok && health_detour(health) == 0;
    *reinterpret_cast<float*>(health + 0xc0) = 1;
    ok = ok && health_detour(health) == 1;
    *reinterpret_cast<float*>(health + 0xc0) = 0.9f;
    ok = ok && recipe_state.cancel("remainder") && remaining_detour(cmd) == 42 && health_detour(health) == 1;
    const auto previous_world = world_global;
    const auto previous_find = find_entity;
    uintptr_t arena_world = 77;
    world_global = reinterpret_cast<uintptr_t>(&arena_world);
    find_entity = [](uintptr_t, const char* name) -> uintptr_t {
        return std::strcmp(name, "extra_heavy") == 0 ? reinterpret_cast<uintptr_t>(actors[0].bytes.data()) : 0;
    };
    recipe_state.reset(std::string(64, 'a'), "game/test");
    ok &= recipe_state.request("outside", {"ai/fodder/imp", "0123456789abcdef0123", "maintenance", 1, 1, 1, 1});
    const auto outside = recipe_state.reserve("outside");
    ok &= outside && recipe_state.created("outside", *outside, "extra_heavy");
    actors[0].name = "ap_v070_ai_r0123456789abcdef0123_00000000_q0000000100000001_t00000000000000010000000000080000_l00000001000000010000000100000002_kn__ap_v070_s00100";
    *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 0;
    *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 0;
    *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16 | 32;
    *reinterpret_cast<save::NativeString*>(command.data() + 0x58) = {};
    ok &= remaining_detour(cmd) == 0;
    *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16;
    ok &= remaining_detour(cmd) == 42;
    *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16 | 32;
    actors[0].name.replace(actors[0].name.find("0000000000080000"), 16, "0000000000000020");
    ok &= remaining_detour(cmd) == 42;
    actors[0].name.replace(actors[0].name.find("0000000000000020", actors[0].name.find("_t")), 16, "0000000000080000");
    ok &= recipe_state.actor_changed(outside->actor, RecipeActorState::dead) && remaining_detour(cmd) == 42;
    world_global = previous_world; find_entity = previous_find;
    native_recipes = std::move(previous_recipes); worker_recipes = std::move(previous_workers);
    kill_progress = std::move(previous_kills); recipe_state = std::move(previous_recipe_state);
    original_kill = previous_kill; original_kill_dead = previous_kill_dead;
    cast_maintain = previous_maintain_cast; cast_stagger = previous_stagger_cast;
    original_health = previous_health; original_health_added = previous_health_added; original_health_damage = previous_health_damage;
    health_progress = std::move(previous_health_progress);
    remaining_weights = std::move(previous_weights);
    definition_name = previous_name; original_remaining = previous_remaining;
    original_remaining_dead = previous_dead;
    refresh_entity = previous_entity; refresh_manager = previous_manager; ignored_actor = previous_ignored;
    stopped.store(was_stopped); return ok && scope_ok;
}
#endif
}
