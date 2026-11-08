// Shared declarations for expanded native domains.
#pragma once
#include "engine/builds/build_descriptor.h"
#include "expanded_native.h"
#include "expanded.h"
#include "enemy_recipe_state.h"
#include "enemy_checkpoint_native.h"
#include "combat_native.h"
#include "inventory.h"
#include "native_target.h"
#include "native_runtime.h"
#include <stdexcept>
#include "save_session.h"
#include "save_collector.h"
#include "commands_native.h"
#include "startup_log.h"
#include <MinHook.h>
#include "engine/hooks/hook_set.h"
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

namespace sentinel::expanded::detail {
extern hooks::HookSet expanded_hooks;
void prepare_recovery(const engine::Binding&, HANDLE);
using CostClock = std::chrono::steady_clock;
uint64_t elapsed_us(CostClock::time_point start);
extern std::atomic<uint64_t> motion_queries,motion_lookup_us,motion_controllers,motion_native_us;
using Movement = float(__fastcall*)(uintptr_t);
using Controller = float(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, float);
using DefinitionName = const char*(__fastcall*)(uintptr_t);
extern Movement original_movement;
extern Movement original_haste;
extern Controller original_controller;
extern DefinitionName definition_name;
struct HostList { uintptr_t* data; int32_t count, capacity; uint8_t allocator[8]; };
using CollectHosts = void(__fastcall*)(HostList*);
using ResolveActor = uintptr_t(__fastcall*)(uintptr_t);
using GrowHosts = void(__fastcall*)(HostList*, int32_t);
extern CollectHosts original_hosts;
extern ResolveActor resolve_actor;
extern GrowHosts grow_hosts;
extern uintptr_t world_global;
using ActorIdentifier = uint32_t(*)(uintptr_t);
extern ActorIdentifier actor_identifier;
using SpawnActor = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
extern SpawnActor original_spawn;
using SpawnGroup = uint64_t(__fastcall*)(uintptr_t, uint64_t*, uintptr_t);
using RegisterSpawn = void(__fastcall*)(uintptr_t, uint64_t*);
extern SpawnGroup original_group;
extern RegisterSpawn original_register_spawn;
using NativeAdmission = bool(__fastcall*)(uintptr_t, int32_t);
extern NativeAdmission native_admission;
using SpawnSingle = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t, uintptr_t, uintptr_t);
using SpawnMultiple = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t, int32_t, uintptr_t, uintptr_t);
using SpawnSpirit = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uint64_t, uintptr_t, uint8_t);
using PointFactory = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
using PointAvailable = uint64_t(*)(uintptr_t, uintptr_t, uint8_t, uint8_t, uint8_t, uint8_t);
using PointCandidate = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t);
using PointIndex = int32_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t);
using RewindTimeline = void(__fastcall*)(uintptr_t, int32_t);
extern SpawnSingle original_single;
extern SpawnMultiple original_multiple;
extern SpawnSpirit original_spirit;
using SpiritHelper = void(*)(uintptr_t, uint64_t, const char*, uintptr_t, uint8_t, uint8_t, uintptr_t, uintptr_t);
using SpawnArchvile = uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, const char*);
using SpawnBuff = uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, const char*);
extern SpiritHelper original_spirit_helper;
extern SpawnArchvile original_archvile;
extern SpawnBuff original_buff;
extern uintptr_t native_string_table;
extern PointFactory original_point_factory;
extern PointAvailable original_point_available;
extern PointCandidate original_point_candidate;
extern PointIndex original_point_index;
extern RewindTimeline rewind_timeline;
using FindEntity = uintptr_t(*)(uintptr_t, const char*);
extern FindEntity find_entity;
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
extern RecipeLibrary recipe_library;
extern std::map<std::string, NativeRecipe> native_recipes;
extern RecipeState recipe_state;
extern uint64_t recipe_stamp;
extern uint64_t next_recipe_request;
extern std::string session_recipe_plan, checkpoint_recipe_plan;
extern bool checkpoint_recipe_missing;
extern std::map<std::pair<uintptr_t, std::string>, std::string> pending_recipes;
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
    bool creating = false, override_active = false, restoring = false, failed = false;
};
extern thread_local RecipeCreation* recipe_creation;
extern thread_local const std::string* worker_recipe;
extern thread_local uint64_t worker_body_offset;
using WorkerTick = void(*)(uintptr_t);
using QueueCommand = void(*)(uintptr_t, uintptr_t);
extern WorkerTick original_maintain_tick, original_stagger_tick;
extern QueueCommand original_queue_command;
extern std::map<uintptr_t, std::string> worker_recipes;
struct WorkerDiagnostic { std::string manager, label; uint64_t mask = 0; };
extern std::map<uintptr_t, WorkerDiagnostic> worker_diagnostics;
extern uint32_t worker_trace_count;
extern std::map<uintptr_t, std::string> recipe_actors;
struct ActorCredit { uint32_t identifier; RecipeCredit credit; std::string request, name; uint64_t ordinal = 0; };
extern std::map<uintptr_t, ActorCredit> recipe_actor_credits;
struct ActorDiagnostic {
    uint32_t identifier = 0;
    uintptr_t manager_address = 0;
    std::string manager, encounter;
    std::array<float, 3> position{};
    bool positioned = false;
    unsigned jumps = 0;
};
extern std::map<uintptr_t, ActorDiagnostic> actor_diagnostics;
extern uint64_t actor_diagnostic_next;
extern uint32_t registration_trace_count;
extern thread_local const char* admission_diagnostic_reason;
bool read_definition_name(uintptr_t, char (&)[512]) noexcept;
void trace_spawn_refusal(uintptr_t manager, std::string_view origin, const char* reason, const char* decision = "deferred", const char* owner_override = nullptr) noexcept;
void trace_actor(uintptr_t actor, const char* reason, uintptr_t source = 0, bool position = false) noexcept;
extern std::map<std::string, RecipeNativeCheckpoint::Identity> restored_actor_identities;
using DeathActions = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using PostOne = uintptr_t(*)(uintptr_t, uintptr_t, int32_t, uintptr_t);
extern DeathActions original_death_actions;
extern PostOne original_post_one;
extern std::array<uintptr_t, 3> death_target_returns;
extern uintptr_t activate_event;
struct DeathActionCredit { uint64_t units = 0, sent = 0; uint32_t denominator = 0; };
extern std::map<std::pair<std::string, std::string>, DeathActionCredit> death_action_credits;
struct PointCredits {
    int32_t observed = 0;
    std::map<uint32_t, uint64_t> units;
};
extern std::map<uintptr_t, PointCredits> point_credits;
using SummonCreate = float(*)(uintptr_t, uintptr_t, uintptr_t, uint64_t, uintptr_t);
using SummonUpdate = void(*)(uintptr_t, uintptr_t);
using NativeUnary = void(*)(uintptr_t);
using ActorFactory = uintptr_t(*)(uintptr_t);
using DefinitionDatabase = uintptr_t(*)();
using LookupDefinition = uintptr_t(*)(uintptr_t, const char*, int32_t);
extern SummonCreate original_summon_create;
extern SummonUpdate original_summon_update;
extern NativeUnary original_summon_start, original_summon_complete, destroy_native_text;
extern NativeUnary original_darklord_summon;
extern NativeUnary original_summon_request_complete;
extern NativeUnary original_summon_cleanup;
extern ActorFactory original_actor_factory;
using ResurrectFactory = uintptr_t(*)(uintptr_t, int32_t, const float*, const float*);
extern ResurrectFactory original_resurrect_factory;
extern DefinitionDatabase definition_database;
extern LookupDefinition lookup_definition;
using GrowSummonRecords = bool(*)(uintptr_t, uint32_t);
extern GrowSummonRecords grow_summon_records;
using ConstructNativeString = uintptr_t(*)(uintptr_t);
extern ConstructNativeString construct_native_string;
extern uintptr_t animation_path_table;
using AssignNativeText = void(*)(uintptr_t, const char*);
extern AssignNativeText assign_native_text;
using SamuelSetup = void(*)(uintptr_t, uintptr_t);
extern SamuelSetup original_samuel_setup;
extern thread_local bool samuel_without_parasite;
using ManagerContains = bool(*)(uintptr_t, uintptr_t);
extern ManagerContains manager_contains;
using MonsterMask = uint64_t(*)(uint64_t);
extern MonsterMask monster_mask;
extern uintptr_t summon_label_symbol;
struct SummonSource {
    std::array<uint8_t, 48> entry{};
    std::array<float, 3> position{};
    std::string origin, label, template_name;
    uint64_t mask = 0;
};
struct SummonCollection { uintptr_t component; std::vector<SummonSource> sources; std::vector<std::string> notifications; };
extern thread_local SummonCollection* summon_collection;
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
extern std::map<uintptr_t, ComponentSummons> component_summons;
extern std::map<uintptr_t, ComponentSummons> direct_points;
extern std::map<std::string, RecipeNativeCheckpoint::Child> summon_children;
extern std::optional<RecipeNativeCheckpoint> pending_summon_checkpoint;
extern NativeUnary original_show, original_show_now, original_hide;
using PostEvent = bool(*)(uintptr_t, uintptr_t, int32_t, uint32_t, uintptr_t);
using CancelEvent = void(*)(uintptr_t, uintptr_t);
extern PostEvent post_event;
extern CancelEvent cancel_event;
extern uintptr_t show_event;
struct DeferredShow { uint32_t identifier; std::string name; };
extern std::map<uintptr_t, DeferredShow> deferred_shows;
extern std::set<std::string> independent_shows;
extern std::optional<RecipeNativeCheckpoint> restored_independent;
extern std::map<std::string, std::array<float, 12>> pending_independent_shows;
extern bool independent_memberships_pending;
using TraversalEnter = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using AnimationValid = bool(*)(uintptr_t, const char*);
extern TraversalEnter original_traversal_enter;
extern AnimationValid original_animation_valid;
struct TraversalEntry {
    TraversalEntry* previous = nullptr;
    uintptr_t actor = 0;
    uint32_t identifier = 0;
    std::array<float, 12> pose{};
    bool missing = false;
};
extern thread_local TraversalEntry* traversal_entry;
extern std::atomic<bool> recipe_fault;
extern SRWLOCK recipe_fault_lock;
extern char first_recipe_fault[1536];
void set_recipe_fault(const char* function, unsigned line, const char* reason, const char* key = nullptr,
                      uintptr_t actor = 0, uintptr_t manager = 0) noexcept;
void recipe_creation_failed(RecipeCreation& creation, const char* function, unsigned line, const char* reason) noexcept;
extern std::map<std::string, uint8_t> recipe_spawn_traces;
extern std::map<std::tuple<uintptr_t, char, uint64_t, uint32_t>, uint8_t> quantity_wait_traces;
struct RemainingWait { uint64_t started; uintptr_t table; };
extern std::map<uintptr_t, RemainingWait> remaining_wait_started;
extern std::map<uintptr_t, std::string> recovery_rejections;
using AllocateWorker = uintptr_t(*)(size_t, uint32_t);
using ConstructStagger = uintptr_t(*)(uintptr_t, int32_t, uint64_t, uint64_t, uintptr_t, uint64_t, const char*);
using AssignManager = void(*)(uintptr_t, uintptr_t);
extern AllocateWorker allocate_worker;
extern ConstructStagger construct_stagger;
extern AssignManager assign_manager;
using NativeClock = uint64_t*(*)(uintptr_t, uint64_t*, int32_t);
using NativeStep = float(*)(uintptr_t, int32_t);
using NativeDuration = uint64_t*(*)(uint64_t*, float);
using ConstructTime = uintptr_t(*)(uintptr_t, uint64_t, uint64_t, uint64_t);
extern NativeClock native_clock;
extern NativeStep native_step;
extern NativeDuration native_duration;
extern ConstructTime construct_time;
using SpawnPossessed = uintptr_t(*)(uintptr_t, uintptr_t, uint64_t, uintptr_t, uintptr_t, uintptr_t,
                                    uint64_t, uintptr_t, uint8_t);
extern SpawnPossessed original_possessed;
using RemainingPredicate = uint64_t(*)(uintptr_t);
using RemainingEvent = void(*)(uintptr_t, uintptr_t);
using RefreshEntity = void(*)(uintptr_t, uintptr_t);
using IgnoredActor = bool(*)(uintptr_t);
extern RemainingPredicate original_remaining;
extern RemainingEvent original_remaining_added, original_remaining_dead;
extern RefreshEntity refresh_entity, refresh_manager;
extern IgnoredActor ignored_actor;
struct RemainingWeight { RecipeCredit credit; bool recipe; };
struct RemainingItem { uint32_t identifier; RemainingWeight weight; bool known = false; };
extern std::map<uintptr_t, std::vector<RemainingItem>> remaining_weights;
using KillConstruct = uintptr_t(*)(uintptr_t, int32_t, uintptr_t, uint64_t, uint64_t);
using WorkerCast = uintptr_t(*)(uintptr_t);
using CompareText = uint8_t(*)(uintptr_t, uintptr_t);
extern KillConstruct original_kill_construct;
extern RemainingPredicate original_kill;
extern RemainingEvent original_kill_dead;
extern WorkerCast cast_maintain, cast_stagger;
extern CompareText compare_text;
struct KillProgress {
    std::string owner, label;
    uint64_t mask = 0;
    uint32_t seen = 0;
    uint8_t traced = 0;
    bool initialized = false, tracked = false;
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> deaths;
    RecipeBaseline baseline;
};
extern std::map<uintptr_t, KillProgress> kill_progress;
extern RemainingPredicate original_health;
extern RemainingEvent original_health_added;
using HealthDamage = void(*)(uintptr_t, uintptr_t, float);
extern HealthDamage original_health_damage;
struct HealthItem { RemainingWeight weight; float initial = 0, current = 0; };
struct HealthProgress {
    std::map<uint32_t, HealthItem> actors;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint64_t> retired;
    bool tracked = false;
};
extern std::map<uintptr_t, HealthProgress> health_progress;
extern std::recursive_mutex quantity_mutex;
extern std::recursive_mutex admission_mutex;
extern uint32_t reserved_actors;
extern std::atomic<bool> stopped;
extern thread_local uintptr_t active_ai;
extern thread_local float active_speed;
bool generated_admission(const SpawnLimits&);
void trace_recipe_spawn(const RecipeCreation&, uintptr_t body, uintptr_t point, uint8_t stage);
void reserve_admission(const SpawnLimits&);
void release_admission(const SpawnLimits&);
void retry_immediate(uintptr_t, const char* reason = "native_spawn_empty");
void initialize_kill(uintptr_t, uintptr_t);
uintptr_t point_factory_detour(uintptr_t, uintptr_t);
uint64_t spawn_detour(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uint64_t group_detour(uintptr_t, uint64_t*, uintptr_t);
std::string independent_body_name(const NativeRecipe&, uint64_t);
void independent_pose(uintptr_t, const float*);
bool capture_autonomous_summons(RecipeNativeCheckpoint&);
bool restore_autonomous_summons(uintptr_t);
uintptr_t record_actor(uintptr_t);
void register_independent_copy(uintptr_t, uint64_t*, uintptr_t);
uintptr_t drain_direct_point(uintptr_t);
bool read_definition_name(uintptr_t ai, char (&copy)[512]) noexcept;
bool read_recipe_definition(uintptr_t ai, char (&copy)[512], bool empty = false) noexcept;
bool read_instance_name(uintptr_t actor, char (&copy)[512]) noexcept;
bool independent_origin(uintptr_t actor, char (&origin)[21]) noexcept;
bool current_recipe_actor(uintptr_t actor);
bool native_text(uintptr_t string, std::string& out, size_t maximum);
bool native_cstring(uintptr_t string, std::string& out, size_t maximum);
bool load_native_recipes();
bool recipe_checkpoint_root(uintptr_t& root) noexcept;
void lock_recipe_checkpoint();
void unlock_recipe_checkpoint();
bool capture_recipe_checkpoint(std::string& bytes);
bool restore_recipe_checkpoint(std::string_view bytes, std::string_view map);
bool recipe_origin(uintptr_t alias, std::string& origin);
bool open_native_request(uintptr_t manager, const NativeRecipe& recipe, std::string_view label, std::string& key,
                          const std::string& parent_actor = {}, const std::string& parent_request = {});
uintptr_t countermeasure_body(const NativeRecipe& recipe, uintptr_t body, bool blood_punch, bool microwave, bool stone,
                             bool armored = true);
void inherit_population(SpawnLimits& limits, uintptr_t parent);
bool begin_recipe_creation(RecipeCreation& creation, uintptr_t manager, std::string_view origin, std::string_view label);
void end_recipe_creation(RecipeCreation& creation) noexcept;
RecipeCreation::PointState read_point_state(uintptr_t point);
void write_point_state(uintptr_t point, const RecipeCreation::PointState& state);
uint32_t physical_spawn_override(uint32_t state);
uintptr_t functional_body(RecipeCreation* creation, uintptr_t group);
void suspend_recipe_points(RecipeCreation* creation);
void resume_recipe_points(RecipeCreation* creation);
void initialize_physical_recipe(RecipeCreation* creation, uintptr_t actor);
bool synchronize_point_credits(uintptr_t point, PointCredits& credits);
bool recipe_landing_pose(RecipeCreation&, float*);
uint64_t point_available_detour(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player, uint8_t fov, uint8_t los);
void trace_recipe_spawn(const RecipeCreation& creation, uintptr_t body, uintptr_t point, uint8_t stage);
uintptr_t recipe_factory(uintptr_t point, uintptr_t group);
save::NativeString recipe_text(const std::string& text);
uint64_t recipe_point_type(uintptr_t point);
uint64_t create_tracked(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint64_t);
uint64_t invoke_recipe_creation(RecipeCreation* creation, uintptr_t manager, uintptr_t record,
                                uintptr_t fourth, uintptr_t fifth, bool group, std::unique_lock<std::recursive_mutex>& admission);
uint64_t create_special_recipe(uintptr_t manager, std::string_view origin, std::string_view label);
uintptr_t actor_factory_detour(uintptr_t info);
void role_text_detour(uintptr_t destination, const char* text);
void samuel_setup_detour(uintptr_t component, uintptr_t actor);
void darklord_summon_detour(uintptr_t component);
float summon_create_detour(uintptr_t component, uintptr_t entry, uintptr_t positions, uint64_t mask, uintptr_t label);
bool summon_memberships(uintptr_t parent, std::string_view source_owner, std::string_view source_label,
                         std::vector<RecipeMembership>& result);
void summon_request_complete_detour(uintptr_t target);
void complete_summon_requests(ComponentSummons& pending);
void summon_cleanup_detour(uintptr_t component);
bool summon_health(uintptr_t actor, std::array<float, 4>& values, bool write) noexcept;
bool native_memberships(uintptr_t world, uintptr_t actor, std::vector<RecipeMembership>& result);
bool restore_memberships(uintptr_t world, uintptr_t actor, uint64_t mask, const std::vector<RecipeMembership>& expected, bool& waiting);
bool capture_autonomous_summons(RecipeNativeCheckpoint& checkpoint);
bool restore_autonomous_summons(uintptr_t world);
void summon_start_detour(uintptr_t component);
void summon_complete_detour(uintptr_t component);
void summon_update_detour(uintptr_t component, uintptr_t frame);
uint64_t without_recipe_override(uintptr_t manager, uintptr_t record, uintptr_t point,
                                  uintptr_t fourth, uintptr_t fifth, unsigned kind);
uint64_t create_from_recipe(uintptr_t manager, uintptr_t record, uintptr_t fourth,
                            uintptr_t fifth, bool group, std::string_view origin);
bool try_recipe_creation(uintptr_t manager, uintptr_t record, uintptr_t alias, uintptr_t fourth,
                          uintptr_t fifth, bool group, uint64_t& result);
void trace_recipe_worker(const char* state, uintptr_t worker, const WorkerDiagnostic& value, const std::string& key);
void maintain_tick_detour(uintptr_t worker);
void stagger_tick_detour(uintptr_t worker);
void queue_command_detour(uintptr_t manager, uintptr_t command);
bool queue_recipe_remainder(uintptr_t manager, uintptr_t alias, const char* label, const std::string& key);
void dispatch_recipe_locked(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                              uintptr_t label, bool group, const std::string& origin, uintptr_t& result);
bool dispatch_recipe_immediate(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                               uintptr_t label, bool group, uintptr_t& result);
uintptr_t possessed_detour(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                            uintptr_t spirit, uint64_t hosts, uintptr_t filter, uint8_t flag);
uintptr_t record_actor(uintptr_t record);
uintptr_t resurrect_detour(uintptr_t component, int32_t index, const float* position, const float* axis);
void death_actions_detour(uintptr_t action, uintptr_t context, uintptr_t state, uintptr_t frame);
void __fastcall remaining_added_detour(uintptr_t command, uintptr_t record);
void __fastcall remaining_dead_detour(uintptr_t command, uintptr_t record);
uint64_t remaining_result(uintptr_t command);
uint64_t __fastcall remaining_detour(uintptr_t command);
void initialize_kill(uintptr_t command, uintptr_t manager);
uintptr_t kill_construct_detour(uintptr_t command, int32_t threshold, uintptr_t label, uint64_t mask, uint64_t clock);
void kill_dead_detour(uintptr_t command, uintptr_t record);
uint64_t kill_detour(uintptr_t command);
void health_added_detour(uintptr_t command, uintptr_t record);
void health_damage_detour(uintptr_t command, uintptr_t record, float damage);
uint64_t health_detour(uintptr_t command);
void __fastcall occupied_hosts_detour(HostList* out);
bool generated_admission(const SpawnLimits& limits);
bool defer_show(uintptr_t actor);
std::string independent_body_name(const NativeRecipe& recipe, uint64_t index);
std::string empty_independent_key(std::string_view origin);
void independent_pose(uintptr_t actor, const float* pose);
void show_detour(uintptr_t actor);
void show_now_detour(uintptr_t actor);
bool animation_valid_detour(uintptr_t state, const char* message);
void traversal_enter_detour(uintptr_t state, uintptr_t context, uintptr_t actions, uintptr_t frame);
void reserve_admission(const SpawnLimits& limits);
void release_admission(const SpawnLimits& limits);
void retry_immediate(uintptr_t manager, const char* reason);
uintptr_t __fastcall single_detour(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t point, uintptr_t label);
uintptr_t __fastcall multiple_detour(uintptr_t manager, uintptr_t output, uint64_t type, int32_t count,
                                    uintptr_t group, uintptr_t label);
uintptr_t __fastcall spirit_detour(uintptr_t manager, uintptr_t output, uintptr_t point, uint64_t allowed_hosts,
                                  uintptr_t label, uint8_t flag);
uintptr_t archvile_detour(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t arch_template,
                           uintptr_t label, const char* group);
uintptr_t buff_detour(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t label, const char* group);
bool recipe_library_point(uintptr_t point);
uint64_t __fastcall point_candidate_detour(uintptr_t manager, uintptr_t point, uint64_t type);
int32_t __fastcall point_index_detour(uintptr_t manager, uintptr_t point, uint64_t type);
uintptr_t drain_direct_point(uintptr_t point);
uintptr_t __fastcall point_factory_detour(uintptr_t point, uintptr_t group);
uint64_t create_tracked(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth,
                        uint64_t physical);
uint64_t __fastcall spawn_detour(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth);
uint64_t __fastcall group_detour(uintptr_t manager, uint64_t* record, uintptr_t group);
void register_independent_copy(uintptr_t manager, uint64_t* copy, uintptr_t actor);
bool register_independent_extras(uintptr_t manager, const NativeRecipe& recipe,
                                 const std::array<uint64_t, 13>& source, const std::string& label);
void __fastcall register_spawn_detour(uintptr_t manager, uint64_t* record);
float __fastcall movement_detour(uintptr_t ai);
float __fastcall haste_detour(uintptr_t ai);
float __fastcall controller_detour(uintptr_t controller, uintptr_t frame, uintptr_t output, float distance);
float __fastcall movement_detour_entry(uintptr_t ai);
float __fastcall haste_detour_entry(uintptr_t ai);
float __fastcall controller_detour_entry(uintptr_t controller, uintptr_t frame, uintptr_t output, float distance);
void __fastcall occupied_hosts_detour_entry(HostList* out);
uint64_t __fastcall spawn_detour_entry(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth);
uint64_t __fastcall group_detour_entry(uintptr_t manager, uint64_t* record, uintptr_t group);
void __fastcall register_spawn_detour_entry(uintptr_t manager, uint64_t* record);
uintptr_t __fastcall single_detour_entry(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t point, uintptr_t label);
uintptr_t __fastcall multiple_detour_entry(uintptr_t manager, uintptr_t output, uint64_t type, int32_t count,
                                    uintptr_t group, uintptr_t label);
uintptr_t __fastcall spirit_detour_entry(uintptr_t manager, uintptr_t output, uintptr_t point, uint64_t allowed_hosts,
                                  uintptr_t label, uint8_t flag);
uintptr_t __fastcall point_factory_detour_entry(uintptr_t point, uintptr_t group);
uint64_t __fastcall point_candidate_detour_entry(uintptr_t manager, uintptr_t point, uint64_t type);
int32_t __fastcall point_index_detour_entry(uintptr_t manager, uintptr_t point, uint64_t type);
uint64_t __fastcall remaining_detour_entry(uintptr_t command);
void __fastcall remaining_added_detour_entry(uintptr_t command, uintptr_t record);
void __fastcall remaining_dead_detour_entry(uintptr_t command, uintptr_t record);
void maintain_tick_detour_entry(uintptr_t worker);
void stagger_tick_detour_entry(uintptr_t worker);
void queue_command_detour_entry(uintptr_t manager, uintptr_t command);
uintptr_t kill_construct_detour_entry(uintptr_t command, int32_t threshold, uintptr_t label, uint64_t mask, uint64_t clock);
uint64_t kill_detour_entry(uintptr_t command);
void kill_dead_detour_entry(uintptr_t command, uintptr_t record);
uint64_t health_detour_entry(uintptr_t command);
void health_added_detour_entry(uintptr_t command, uintptr_t record);
void health_damage_detour_entry(uintptr_t command, uintptr_t record, float damage);
uintptr_t archvile_detour_entry(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t arch_template,
                           uintptr_t label, const char* group);
uintptr_t buff_detour_entry(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t label, const char* group);
uintptr_t possessed_detour_entry(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                            uintptr_t spirit, uint64_t hosts, uintptr_t filter, uint8_t flag);
void summon_start_detour_entry(uintptr_t component);
float summon_create_detour_entry(uintptr_t component, uintptr_t entry, uintptr_t positions, uint64_t mask, uintptr_t label);
void summon_update_detour_entry(uintptr_t component, uintptr_t frame);
void summon_complete_detour_entry(uintptr_t component);
uintptr_t actor_factory_detour_entry(uintptr_t info);
void summon_request_complete_detour_entry(uintptr_t target);
void summon_cleanup_detour_entry(uintptr_t component);
uint64_t point_available_detour_entry(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player, uint8_t fov, uint8_t los);
void death_actions_detour_entry(uintptr_t action, uintptr_t context, uintptr_t state, uintptr_t frame);
uintptr_t post_one_detour_entry(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument);
void role_text_detour_entry(uintptr_t destination, const char* text);
void samuel_setup_detour_entry(uintptr_t component, uintptr_t actor);
void show_detour_entry(uintptr_t actor);
void show_now_detour_entry(uintptr_t actor);
void hide_detour_entry(uintptr_t actor);
void traversal_enter_detour_entry(uintptr_t state, uintptr_t context, uintptr_t actions, uintptr_t frame);
bool animation_valid_detour_entry(uintptr_t state, const char* message);
uintptr_t resurrect_detour_entry(uintptr_t component, int32_t index, const float* position, const float* axis);
void darklord_summon_detour_entry(uintptr_t component);
bool synchronize_independent_memberships_safe(uintptr_t world) noexcept;
void reconcile_recipe_memberships(const std::string& actor, const std::vector<RecipeMembership>& memberships);
bool read_recipe_actors(uintptr_t world, std::map<uintptr_t, bool>& actors,
                        std::map<std::string, std::vector<RecipeMembership>>& memberships,
                        std::set<uintptr_t>& workers, std::set<uintptr_t>& owners, uint64_t* living=nullptr);
void prepare_recovery(const engine::Binding& binding, HANDLE stop_event);
extern uintptr_t darklord_image;
extern uint64_t darklord_frame;
void darklord_sample(uintptr_t world, uint64_t now) noexcept;
void darklord_clear() noexcept;
}
