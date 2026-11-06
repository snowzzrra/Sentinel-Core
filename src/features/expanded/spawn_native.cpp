// Spawn admission, points, visibility and traversal.
#include "expanded_internal.h"

namespace sentinel::expanded::detail {
uint32_t native_actor_identifier(uintptr_t actor);
bool admission_observed(bool allowed, const char* reason) noexcept;
extern thread_local uintptr_t active_show_actor;
struct IndependentShow {
    RecipeCreation creation;
    bool opened = false, source = false;
};
extern std::array<uint32_t, 9> reserved_special;
extern std::array<uint32_t, 9> reserved_species;
struct ImmediateRequest {
    ImmediateRequest* previous;
    uintptr_t manager, origin;
    SpawnLimits limits;
    SpawnTracking tracking;
    bool reserved, succeeded, direct_factory;
};
extern thread_local ImmediateRequest* immediate_request;
uintptr_t invoke_direct_creation(RecipeCreation*, uintptr_t);
uint64_t invoke_point_available(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player,
                                uint8_t fov, uint8_t los, RecipeCreation* creation);
uint64_t point_available_locked(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player, uint8_t fov, uint8_t los);
bool recipe_arena_point(uintptr_t point);
bool private_supply(std::string_view name, std::string_view suffix) noexcept;
bool private_supplier(uintptr_t ai) noexcept;
unsigned special_kind(uintptr_t actor, unsigned* physical = nullptr);
bool population_member(uintptr_t world, uintptr_t actor, uint64_t scope, unsigned depth = 0);
bool special_admission(const SpawnLimits& limits);
bool supply_admission(uintptr_t world);
bool active_actor(uintptr_t actor);
void cancel_deferred_show(uintptr_t actor);
bool prepare_independent_show_locked(uintptr_t actor, IndependentShow& show);
bool prepare_independent_show(uintptr_t actor, IndependentShow& show);
void finish_independent_show(uintptr_t actor, IndependentShow& show) noexcept;
bool owns_show_admission(uintptr_t actor, const char* name);
bool prepare_show(uintptr_t actor, SpawnLimits& limits, bool& reserved);
void invoke_show(uintptr_t actor, NativeUnary original, IndependentShow* independent);
void show_admitted(uintptr_t actor, NativeUnary original);
void hide_independent_bodies(uintptr_t actor);
void try_hide_independent_bodies(uintptr_t actor) noexcept;
void hide_detour_impl(uintptr_t actor, void* hook_caller);
bool prepare_traversal(uintptr_t context, TraversalEntry& entry) noexcept;
void invoke_traversal(uintptr_t state, uintptr_t context, uintptr_t actions, uintptr_t frame, TraversalEntry* entry, std::unique_lock<std::recursive_mutex>& admission);
bool begin_immediate(ImmediateRequest* request, uintptr_t manager, uintptr_t origin, std::unique_lock<std::recursive_mutex>& admission);
void end_immediate(ImmediateRequest* request, std::unique_lock<std::recursive_mutex>& admission);
bool owns_immediate_admission(uintptr_t manager, const SpawnLimits& limits, const SpawnTracking* tracking);
bool tracked_point_type(uintptr_t manager, uintptr_t point, uint64_t type);
uintptr_t invoke_direct_creation(RecipeCreation* creation, uintptr_t point);
uintptr_t activate_direct_point(uintptr_t point, std::string_view origin);
uintptr_t point_factory_locked(uintptr_t point, uintptr_t group);
uint64_t spawn_marked(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth,
                      const SpawnLimits* limits, const SpawnTracking* tracking);
void trace_registration(uintptr_t manager, uint64_t* record, uintptr_t actor, uint64_t incoming) noexcept;
void register_spawn_locked(uintptr_t manager, uint64_t* record);
#ifdef SC_NATIVE_TESTING
bool test_supply_headroom();
#endif
}

namespace sentinel::expanded::detail {

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

#ifdef SC_NATIVE_TESTING
ActorIdentifier actor_identifier = [](uintptr_t actor) {
    return reinterpret_cast<uint32_t(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
        *reinterpret_cast<const uintptr_t*>(actor) + 0x88))(actor);
};
#else
ActorIdentifier actor_identifier = native_actor_identifier;
#endif

SpawnActor original_spawn = nullptr;

SpawnGroup original_group = nullptr;

RegisterSpawn original_register_spawn = nullptr;

NativeAdmission native_admission = nullptr;

SpawnSingle original_single = nullptr;

SpawnMultiple original_multiple = nullptr;

SpawnSpirit original_spirit = nullptr;

SpiritHelper original_spirit_helper = nullptr;

SpawnArchvile original_archvile = nullptr;

SpawnBuff original_buff = nullptr;

uintptr_t native_string_table = 0;

PointFactory original_point_factory = nullptr;

PointAvailable original_point_available = nullptr;

PointCandidate original_point_candidate = nullptr;

PointIndex original_point_index = nullptr;

RewindTimeline rewind_timeline = nullptr;

FindEntity find_entity = nullptr;

thread_local const char* admission_diagnostic_reason = "admission_unobserved";

bool admission_observed(bool allowed, const char* reason) noexcept {
    if (!allowed) admission_diagnostic_reason = reason;
    return allowed;
}

void trace_spawn_refusal(uintptr_t manager, std::string_view origin, const char* reason, const char* decision, const char* owner_override) noexcept {
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

PostEvent post_event = nullptr;

CancelEvent cancel_event = nullptr;

uintptr_t show_event = 0;

std::map<uintptr_t, DeferredShow> deferred_shows;

thread_local uintptr_t active_show_actor = 0;

std::set<std::string> independent_shows;

std::optional<RecipeNativeCheckpoint> restored_independent;

std::map<std::string, std::array<float, 12>> pending_independent_shows;

bool independent_memberships_pending = false;

TraversalEnter original_traversal_enter = nullptr;

AnimationValid original_animation_valid = nullptr;

thread_local TraversalEntry* traversal_entry = nullptr;

std::recursive_mutex admission_mutex;

std::array<uint32_t, 9> reserved_special{};

std::array<uint32_t, 9> reserved_species{};

uint32_t reserved_actors = 0;

thread_local ImmediateRequest* immediate_request = nullptr;

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
    if (creation && creation->failed) return 0;
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
    } catch (...) { set_recipe_fault(__func__, __LINE__, "exception"); return 0; }
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
    if (creation->failed) return 0;
    const auto state = read_point_state(point);
    const bool prepared = creation->override_active && creation->active_point == point;
    if (!prepared) creation->original = state;
    creation->active_point = point;
    trace_recipe_spawn(*creation, creation->body, point, 8);
    uintptr_t actor = 0;
    __try {
        auto& credits = point_credits[point];
        if (!synchronize_point_credits(point, credits)) { set_recipe_fault(__func__, __LINE__, "point_credit_sync_failed", creation->key.c_str()); return 0; }
        const auto body = functional_body(creation, group);
        if (!body) { recipe_creation_failed(*creation, __func__, __LINE__, "functional_body_missing"); return 0; }
        float destination[12];
        const auto physical_state = recipe_landing_pose(*creation, destination) && creation->original.state != 9 ?
            12u : physical_spawn_override(creation->original.state);
        if (creation->failed) return 0;
        write_point_state(point, {body, 1, 0, 0, physical_state});
        creation->creating = creation->override_active = true;
        trace_recipe_spawn(*creation, body, point, 1);
        actor = original_point_factory(point, group);
        if (actor) {
            creation->actor = actor;
            float landing[12];
            if (recipe_landing_pose(*creation, landing)) {
                const auto name=*reinterpret_cast<const char**>(creation->body+8);
                landing[2]+=name ? spawn_lift(name) : 0.0f;
                independent_pose(actor,landing);
            }
            initialize_physical_recipe(creation, actor);
        }
        trace_recipe_spawn(*creation, body, point, actor ? 4 : 2);
    } __finally {
        creation->creating = false; creation->override_active = prepared;
        write_point_state(point, prepared ? state : creation->original);
    }
    return actor;
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

unsigned special_kind(uintptr_t actor, unsigned* physical) {
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

bool population_member(uintptr_t world, uintptr_t actor, uint64_t scope, unsigned depth) {
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
    if (count < 0 || count > 1000000) { set_recipe_fault(__func__, __LINE__, "actor_count_invalid"); return false; }
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
        if (!defer_show(actor)) set_recipe_fault(__func__, __LINE__, "defer_show_failed", nullptr, actor);
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
        if (member->second.native_name != name) { set_recipe_fault(__func__, __LINE__, "body_name_mismatch", key.c_str(), actor); return false; }
        const auto identifier = actor_identifier(actor);
        recipe_actors[actor] = id;
        recipe_actor_credits[actor] = {identifier, {1, entry.physical_count}, key, name, frozen->body};
        return true;
    }
    if (frozen->body != progress.emitted || progress.reserved) {
        if (!defer_show(actor)) set_recipe_fault(__func__, __LINE__, "defer_show_failed", key.c_str(), actor);
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
    catch (...) { set_recipe_fault(__func__, __LINE__, "exception", nullptr, actor); return false; }
}

void finish_independent_show(uintptr_t actor, IndependentShow& show) noexcept {
    try {
        if (show.opened) {
            show.creation.actor = active_actor(actor) ? actor : 0;
            end_recipe_creation(show.creation); show.opened = false;
            if (!show.creation.actor && !defer_show(actor)) set_recipe_fault(__func__, __LINE__, "defer_show_failed", show.creation.key.c_str(), actor);
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
                if (extra && !defer_show(extra)) set_recipe_fault(__func__, __LINE__, "defer_show_failed", nullptr, extra);
            }
        }
    } catch (...) {
        if (show.opened) { end_recipe_creation(show.creation); show.opened = false; }
        set_recipe_fault(__func__, __LINE__, "exception", show.creation.key.c_str(), actor);
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
                if (!defer_show(actor)) set_recipe_fault(__func__, __LINE__, "defer_show_failed", nullptr, actor);
                return false;
            }
            char origin[21], source[512];
            if (!independent_origin(actor, origin) || !read_instance_name(actor, source)) return false;
            const auto found = native_recipes.find(origin);
            if (found == native_recipes.end() || found->second.definition.family != 'd' ||
                found->second.definition.source != source || found->second.definition.physical_count) {
                set_recipe_fault(__func__, __LINE__, "empty_recipe_invalid", nullptr, actor); return false;
            }
            const auto& entry = found->second.definition;
            auto key = empty_independent_key(entry.origin);
            const auto previous = recipe_state.requests().find(key);
            const bool next = previous != recipe_state.requests().end() && previous->second.empty_completed && !independent_shows.count(source);
            if (next) {
                if (next_recipe_request == UINT64_MAX) { set_recipe_fault(__func__, __LINE__, "request_counter_overflow", key.c_str(), actor); return false; }
                key = "independent/" + entry.origin + "/" + std::to_string(++next_recipe_request);
            }
            if (!recipe_state.request(key, {entry.source, entry.origin, "", entry.logical_mask, 1, 0, 0})) {
                set_recipe_fault(__func__, __LINE__, "empty_request_failed", key.c_str(), actor); return false;
            }
            if (next) {
                std::vector<RecipeMembership> members;
                if (!native_memberships(*reinterpret_cast<const uintptr_t*>(world_global), actor, members)) { set_recipe_fault(__func__, __LINE__, "memberships_unreadable", key.c_str(), actor); return false; }
                for (const auto& member : members)
                    if (!recipe_state.join_empty(key, member.owner, member.label)) { set_recipe_fault(__func__, __LINE__, "empty_membership_failed", key.c_str(), actor); return false; }
            }
            cancel_deferred_show(actor);
            independent_shows.insert(source);
            if (!recipe_state.requests().at(key).empty_completed && !recipe_state.complete_empty(key, true)) set_recipe_fault(__func__, __LINE__, "empty_completion_failed", key.c_str(), actor);
            return false;
        }
        if (stopped.load(std::memory_order_acquire) || active_show_actor == actor || !read_recipe_definition(actor, name)) return true;
        if (*reinterpret_cast<const int32_t*>(actor + 0x10) < 0) return true;
        if (active_actor(actor) || reinterpret_cast<bool(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
                *reinterpret_cast<const uintptr_t*>(actor) + 0x108))(actor)) {
            cancel_deferred_show(actor); return true;
        }
        const auto parsed = spawn_limits(name);
        if (!parsed) { set_recipe_fault(__func__, __LINE__, "spawn_limits_invalid", nullptr, actor); return false; }
        limits = *parsed;
        const bool owned = owns_show_admission(actor, name);
        if (!owned && (recipe_fault.load() || !generated_admission(limits))) {
            const auto diagnostic = actor_diagnostics.find(actor);
            trace_spawn_refusal(diagnostic == actor_diagnostics.end() ? 0 : diagnostic->second.manager_address, name,
                recipe_fault.load() ? "recipe_fault" : admission_diagnostic_reason);
            if (!defer_show(actor)) set_recipe_fault(__func__, __LINE__, "defer_show_failed", nullptr, actor);
            return false;
        }
        cancel_deferred_show(actor);
        if (!owned) { reserve_admission(limits); reserved = true; }
        return true;
    } catch (...) { set_recipe_fault(__func__, __LINE__, "exception", nullptr, actor); return false; }
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
    try { hide_independent_bodies(actor); } catch (...) { set_recipe_fault(__func__, __LINE__, "exception", nullptr, actor); }
}

void hide_detour_impl(uintptr_t actor, void* hook_caller) {
    if (stopped.load(std::memory_order_acquire)) { original_hide(actor); return; }
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
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
        if (!point) { set_recipe_fault(__func__, __LINE__, "traversal_point_missing", nullptr, actor); return false; }
        const auto origin = reinterpret_cast<const float*(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(point) + 0x1b0))(point);
        const auto axis = reinterpret_cast<const float*(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(
            *reinterpret_cast<const uintptr_t*>(point) + 0x1b8))(point);
        const auto local = reinterpret_cast<const float*>(point + 0xbd8);
        for (unsigned index = 0; index < 3; ++index)
            entry.pose[index] = axis[index] * local[0] + axis[index + 3] * local[1] + axis[index + 6] * local[2] + origin[index];
        std::memcpy(entry.pose.data() + 3, axis, 36);
        for (const auto value : entry.pose) if (!std::isfinite(value)) { set_recipe_fault(__func__, __LINE__, "traversal_pose_invalid", nullptr, actor); return false; }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { set_recipe_fault(__func__, __LINE__, "native_exception", nullptr, entry.actor); return false; }
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
    if (!load_native_recipes()) { set_recipe_fault(__func__, __LINE__, "recipe_library_unavailable"); return 0; }
    const auto recipe = native_recipes.find(std::string(origin));
    if (recipe == native_recipes.end() || recipe->second.definition.family != 'f' || next_recipe_request == UINT64_MAX) {
        set_recipe_fault(__func__, __LINE__, "direct_recipe_invalid"); return 0;
    }
    const auto& definition = recipe->second.definition;
    const auto key = "direct/" + definition.origin + "/" + std::to_string(++next_recipe_request);
    const auto name = "ap_v070_direct_r" + definition.origin;
    if (!recipe_state.request(key, {definition.owner.empty() ? name : definition.owner, definition.origin, "", definition.logical_mask,
        definition.logical_count, definition.physical_count, definition.maximum_emissions, definition.maximum_emissions, definition.final_credit_units})) {
        set_recipe_fault(__func__, __LINE__, "direct_request_failed", key.c_str()); return 0;
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
            set_recipe_fault(__func__, __LINE__, "body_recipe_mismatch", nullptr, extra, manager); return false;
        }
        bool present = false;
        for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
            const auto data = *reinterpret_cast<const uintptr_t*>(manager + offset);
            const auto count = *reinterpret_cast<const int32_t*>(manager + offset + 8);
            if (count < 0 || count > 1000000) { set_recipe_fault(__func__, __LINE__, "actor_count_invalid", nullptr, extra, manager); return false; }
            for (int32_t i = 0; i < count; ++i) {
                const auto record = data + static_cast<uintptr_t>(i) * 0x68;
                if (record_actor(record) != extra || *reinterpret_cast<const uint32_t*>(record + 12) != actor_identifier(extra)) continue;
                std::string current;
                if (!native_text(record + 0x28, current, 1024)) { set_recipe_fault(__func__, __LINE__, "label_unreadable", nullptr, extra, manager); return false; }
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
                set_recipe_fault(__func__, __LINE__, "label_unreadable", nullptr, actor, manager); return;
            }
            std::memcpy(copy.data(), record, sizeof(copy));
            if (independent->bodies.empty()) {
                const auto& entry = independent->definition;
                const auto key = empty_independent_key(entry.origin);
                char owner[512];
                if (!read_definition_name(manager, owner) ||
                    !recipe_state.request(key, {entry.source, entry.origin, "", entry.logical_mask, 1, 0, 0}) ||
                    !recipe_state.join_empty(key, owner, label)) { set_recipe_fault(__func__, __LINE__, "empty_membership_failed", key.c_str(), actor, manager); return; }
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
            set_recipe_fault(__func__, __LINE__, "membership_unreadable", nullptr, actor, manager); return;
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

void __fastcall occupied_hosts_detour_entry(HostList* out) {
    const auto lease = expanded_hooks.enter(3);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hosts)>(lease.original)(out);
        return occupied_hosts_detour(out);
    } __finally { expanded_hooks.leave(lease); }
}

uint64_t __fastcall spawn_detour_entry(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth) {
    const auto lease = expanded_hooks.enter(4);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_spawn)>(lease.original)(manager, record, point, fourth, fifth);
        return spawn_detour(manager, record, point, fourth, fifth);
    } __finally { expanded_hooks.leave(lease); }
}

uint64_t __fastcall group_detour_entry(uintptr_t manager, uint64_t* record, uintptr_t group) {
    const auto lease = expanded_hooks.enter(5);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_group)>(lease.original)(manager, record, group);
        return group_detour(manager, record, group);
    } __finally { expanded_hooks.leave(lease); }
}

void __fastcall register_spawn_detour_entry(uintptr_t manager, uint64_t* record) {
    const auto lease = expanded_hooks.enter(6);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_register_spawn)>(lease.original)(manager, record);
        return register_spawn_detour(manager, record);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t __fastcall single_detour_entry(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t point, uintptr_t label) {
    const auto lease = expanded_hooks.enter(7);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_single)>(lease.original)(manager, output, type, point, label);
        return single_detour(manager, output, type, point, label);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t __fastcall multiple_detour_entry(uintptr_t manager, uintptr_t output, uint64_t type, int32_t count,
                                    uintptr_t group, uintptr_t label) {
    const auto lease = expanded_hooks.enter(8);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_multiple)>(lease.original)(manager, output, type, count, group, label);
        return multiple_detour(manager, output, type, count, group, label);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t __fastcall spirit_detour_entry(uintptr_t manager, uintptr_t output, uintptr_t point, uint64_t allowed_hosts,
                                  uintptr_t label, uint8_t flag) {
    const auto lease = expanded_hooks.enter(9);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_spirit)>(lease.original)(manager, output, point, allowed_hosts, label, flag);
        return spirit_detour(manager, output, point, allowed_hosts, label, flag);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t __fastcall point_factory_detour_entry(uintptr_t point, uintptr_t group) {
    const auto lease = expanded_hooks.enter(10);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_point_factory)>(lease.original)(point, group);
        return point_factory_detour(point, group);
    } __finally { expanded_hooks.leave(lease); }
}

uint64_t __fastcall point_candidate_detour_entry(uintptr_t manager, uintptr_t point, uint64_t type) {
    const auto lease = expanded_hooks.enter(11);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_point_candidate)>(lease.original)(manager, point, type);
        return point_candidate_detour(manager, point, type);
    } __finally { expanded_hooks.leave(lease); }
}

int32_t __fastcall point_index_detour_entry(uintptr_t manager, uintptr_t point, uint64_t type) {
    const auto lease = expanded_hooks.enter(12);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_point_index)>(lease.original)(manager, point, type);
        return point_index_detour(manager, point, type);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t archvile_detour_entry(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t arch_template,
                           uintptr_t label, const char* group) {
    const auto lease = expanded_hooks.enter(25);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_archvile)>(lease.original)(manager, output, point, arch_template, label, group);
        return archvile_detour(manager, output, point, arch_template, label, group);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t buff_detour_entry(uintptr_t manager, uintptr_t output, uintptr_t point, uintptr_t label, const char* group) {
    const auto lease = expanded_hooks.enter(26);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_buff)>(lease.original)(manager, output, point, label, group);
        return buff_detour(manager, output, point, label, group);
    } __finally { expanded_hooks.leave(lease); }
}

uint64_t point_available_detour_entry(uintptr_t point, uintptr_t group, uint8_t time, uint8_t player, uint8_t fov, uint8_t los) {
    const auto lease = expanded_hooks.enter(35);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_point_available)>(lease.original)(point, group, time, player, fov, los);
        return point_available_detour(point, group, time, player, fov, los);
    } __finally { expanded_hooks.leave(lease); }
}

void show_detour_entry(uintptr_t actor) {
    const auto lease = expanded_hooks.enter(40);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_show)>(lease.original)(actor);
        return show_detour(actor);
    } __finally { expanded_hooks.leave(lease); }
}

void show_now_detour_entry(uintptr_t actor) {
    const auto lease = expanded_hooks.enter(41);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_show_now)>(lease.original)(actor);
        return show_now_detour(actor);
    } __finally { expanded_hooks.leave(lease); }
}

void hide_detour_entry(uintptr_t actor) {
    const auto caller = _ReturnAddress();
    const auto lease = expanded_hooks.enter(42);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hide)>(lease.original)(actor);
        return hide_detour_impl(actor, caller);
    } __finally { expanded_hooks.leave(lease); }
}

void traversal_enter_detour_entry(uintptr_t state, uintptr_t context, uintptr_t actions, uintptr_t frame) {
    const auto lease = expanded_hooks.enter(43);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_traversal_enter)>(lease.original)(state, context, actions, frame);
        return traversal_enter_detour(state, context, actions, frame);
    } __finally { expanded_hooks.leave(lease); }
}

bool animation_valid_detour_entry(uintptr_t state, const char* message) {
    const auto lease = expanded_hooks.enter(44);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_animation_valid)>(lease.original)(state, message);
        return animation_valid_detour(state, message);
    } __finally { expanded_hooks.leave(lease); }
}

}

#ifdef SC_NATIVE_TESTING

#include "../../../tests/expanded/spawn.inc"

#endif
