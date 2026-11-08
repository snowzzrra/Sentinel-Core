// Summons, bosses, possession and child restoration.
#include "expanded_internal.h"

namespace sentinel::expanded::detail {
extern thread_local bool actor_factory_active;
const NativeRecipe* summon_recipe(uintptr_t component, uint64_t monster_type, uint64_t logical_type);
bool collect_summon(uintptr_t component, uintptr_t entry, uintptr_t positions, uint64_t mask, uintptr_t label);
bool protected_summon_definition(uintptr_t entry);
bool summon_labels(uintptr_t parent, std::string& label, bool& shield);
bool open_component_summons(SummonCollection& collection);
void invoke_summon_creation(RecipeCreation* creation, uintptr_t component, const SummonSource* source);
RecipeNativeCheckpoint::Source freeze_summon_source(const SummonSource& source);
SummonSource thaw_summon_source(const RecipeNativeCheckpoint::Source& saved);
void invoke_checkpoint_actor(RecipeCreation* creation, const RecipeNativeCheckpoint::Child* saved);
bool restore_checkpoint_actor(uintptr_t world, const std::string& role, RecipeNativeCheckpoint::Child& saved, uintptr_t& actor);
bool adopt_summon_child(uintptr_t component, uintptr_t actor);
bool drain_component_summons(uintptr_t component);
void invoke_summon_start(uintptr_t component, SummonCollection* collection);
uint64_t invoke_suspended_recipe(RecipeCreation* scope, uintptr_t manager, uintptr_t record,
                                 uintptr_t point, uintptr_t fourth, uintptr_t fifth, unsigned kind);
uintptr_t possessed_recipe_locked(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                                   uintptr_t spirit, const std::string& host_origin, const std::string& spirit_origin);
uintptr_t invoke_possessed_recipe(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                                   uintptr_t spirit, const std::string* host_origin, const std::string* spirit_origin, std::unique_lock<std::recursive_mutex>& admission);
uintptr_t resurrection_source(uintptr_t component, int32_t index) noexcept;
uintptr_t resurrect_locked(uintptr_t component, int32_t index, const float* position, const float* axis);
#ifdef SC_NATIVE_TESTING
bool darklord_fault_call(uintptr_t component);
#endif
}

namespace sentinel::expanded::detail {

SummonCreate original_summon_create = nullptr;

SummonUpdate original_summon_update = nullptr;

NativeUnary original_summon_start = nullptr, original_summon_complete = nullptr, destroy_native_text = nullptr;

NativeUnary original_darklord_summon = nullptr;

NativeUnary original_summon_request_complete = nullptr;

NativeUnary original_summon_cleanup = nullptr;

ActorFactory original_actor_factory = nullptr;

ResurrectFactory original_resurrect_factory = nullptr;

DefinitionDatabase definition_database = nullptr;

LookupDefinition lookup_definition = nullptr;

GrowSummonRecords grow_summon_records = nullptr;

ConstructNativeString construct_native_string = nullptr;

uintptr_t animation_path_table = 0;

AssignNativeText assign_native_text = nullptr;

SamuelSetup original_samuel_setup = nullptr;

thread_local bool samuel_without_parasite = false;

ManagerContains manager_contains = nullptr;

MonsterMask monster_mask = nullptr;

uintptr_t summon_label_symbol = 0;

thread_local SummonCollection* summon_collection = nullptr;

thread_local bool actor_factory_active = false;

std::map<uintptr_t, ComponentSummons> component_summons;

std::map<uintptr_t, ComponentSummons> direct_points;

std::map<std::string, RecipeNativeCheckpoint::Child> summon_children;

std::optional<RecipeNativeCheckpoint> pending_summon_checkpoint;

NativeUnary original_show = nullptr, original_show_now = nullptr, original_hide = nullptr;

static float actor_spawn_lift(const RecipeCreation& creation) {
    const auto name = creation.body ? *reinterpret_cast<const char**>(creation.body + 8) : nullptr;
    return name && !creation.restoring ? spawn_lift(name) : 0.0f;
}
uintptr_t actor_factory_detour(uintptr_t info) {
    if (stopped.load(std::memory_order_acquire)) return original_actor_factory(info);
    const auto creation = recipe_creation;
    if (!creation || !creation->creating || actor_factory_active)
        return original_actor_factory(info);
    const float lift = actor_spawn_lift(*creation);
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

#include "../../darklord_telemetry.h"
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

static std::string summon_source_name(uintptr_t component) {
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
    return source;
}
const NativeRecipe* summon_recipe(uintptr_t component, uint64_t monster_type, uint64_t logical_type) {
    std::string source;
    source = summon_source_name(component);
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
    try { accepted = collect_summon(component, entry, positions, mask, label); } catch (...) { set_recipe_fault(__func__, __LINE__, "exception"); }
    destroy_native_text(label);
    if (!accepted) set_recipe_fault(__func__, __LINE__, "summon_collection_failed");
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
    if (!read_definition_name(target, name)) { set_recipe_fault(__func__, __LINE__, "definition_unreadable", nullptr, target); return; }
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

static bool complete_empty_summon(uintptr_t parent, const std::string& key, const std::string& label, const std::vector<SummonSource>& sources) {
        if (!recipe_state.complete_empty(key)) return false;
        std::vector<RecipeMembership> members;
        if (!summon_memberships(parent, sources.front().template_name, label, members)) return false;
        for (const auto& member : members) {
            if (!recipe_state.join_empty(key, member.owner, member.label)) return false;
        }
        return true;
}
static bool queue_component_summon(uintptr_t parent, uint64_t epoch, const RecipeLibraryEntry& definition, const std::string& key, std::vector<SummonSource>& sources, ComponentSummons& pending) {
    uint64_t offset = 0;
    if (definition.source.empty()) {
        char name[512];
        if (!read_recipe_definition(parent, name) && !read_instance_name(parent, name)) return false;
        offset = 14695981039346656037ull;
        for (auto cursor = name; *cursor; ++cursor) offset = (offset ^ static_cast<unsigned char>(*cursor)) * 1099511628211ull;
        offset = (offset ^ epoch) * 1099511628211ull;
    }
    pending.requests.push_back({key, definition.origin, std::move(sources), offset});
    return true;
}
static bool open_summon_request(uintptr_t parent, const std::pair<std::string, std::string>& identity, const std::vector<SummonSource>& sources, std::string& key, bool& empty) {
    const auto& definition = native_recipes.at(identity.first).definition;
    const auto original_count = static_cast<uint32_t>(sources.size());
    const auto numerator = definition.quantity_denominator ? definition.quantity_numerator : definition.physical_count;
    const auto denominator = definition.quantity_denominator ? definition.quantity_denominator : definition.logical_count;
    const auto quantity = (uint64_t{original_count} * numerator + denominator - 1) / denominator;
    if (quantity > INT32_MAX || next_recipe_request == UINT64_MAX) return false;
    char owner[512];
    if (!read_definition_name(parent, owner)) return false;
    key = "summon/" + definition.origin + "/" + std::to_string(++next_recipe_request);
    if (!recipe_state.request(key, {owner, definition.origin, identity.second, definition.logical_mask, original_count,
            static_cast<uint32_t>(quantity), quantity, static_cast<uint32_t>(quantity), quantity ? original_count : 0,
            "", ""})) return false;
    if (!quantity) {
        if (!complete_empty_summon(parent, key, identity.second, sources)) return false;
        empty = true; return true;
    }
    return true;
}
static bool open_summon_groups(uintptr_t parent, uint64_t epoch, std::map<std::pair<std::string, std::string>, std::vector<SummonSource>>& groups, ComponentSummons& pending) {
    for (auto& [identity, sources] : groups) {
        const auto& definition = native_recipes.at(identity.first).definition;
        std::string key; bool empty = false;
        if (!open_summon_request(parent, identity, sources, key, empty)) return false;
        if (empty) continue;
        if (!queue_component_summon(parent, epoch, definition, key, sources, pending)) return false;
    }
    return true;
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
    if (!open_summon_groups(parent, epoch, groups, pending)) return false;
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
        if (!definition) { recipe_creation_failed(*creation, __func__, __LINE__, "definition_missing"); return; }
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

static bool capture_summon_component(uintptr_t component, RecipeNativeCheckpoint::Component& saved) {
    char name[512];
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
    return true;
}
static bool capture_summon_requests(const ComponentSummons& pending, bool direct, RecipeNativeCheckpoint::Component& saved) {
    for (const auto& request : pending.requests) {
        RecipeNativeCheckpoint::Summon record;
        record.key = request.key; record.origin = request.origin; record.body_offset = request.body_offset; record.completed = request.completed;
        for (const auto& source : request.sources) record.sources.push_back(freeze_summon_source(source));
        if (direct) record.memberships = recipe_state.request_memberships(request.key);
        else if (request.sources.empty() || !summon_memberships(pending.parent, request.sources.front().template_name,
                recipe_state.requests().at(request.key).request.label, record.memberships)) return false;
        saved.requests.push_back(std::move(record));
    }
    return true;
}
static bool capture_summon_producer(uintptr_t world, uintptr_t component, const ComponentSummons& pending, bool direct, RecipeNativeCheckpoint& checkpoint, std::set<std::string>& parents) {
    if (pending.name.empty() || find_entity(world, pending.name.c_str()) != pending.parent) {
        for (const auto& request : pending.requests) recipe_state.cancel(request.key);
        return true;
    }
    char name[512], definition[512];
    if (!read_instance_name(pending.parent, name) || !*name || actor_identifier(pending.parent) != pending.identifier ||
        (!read_recipe_definition(pending.parent, definition) && !read_definition_name(pending.parent, definition))) return false;
    if (find_entity(world, name) != pending.parent || combat::confirmed_dead(world, pending.parent)) {
        for (const auto& request : pending.requests) recipe_state.cancel(request.key);
        return true;
    }
    RecipeNativeCheckpoint::Component saved;
    saved.parent = name; saved.definition = definition; saved.epoch = pending.epoch; saved.completed = pending.completed;
    if (!direct && !capture_summon_component(component, saved)) return false;
    saved.notifications = pending.notifications;
    if (!capture_summon_requests(pending, direct, saved)) return false;
    parents.insert(saved.parent); checkpoint.summons.push_back(std::move(saved));
    return true;
}
static bool capture_summon_producers(uintptr_t world, RecipeNativeCheckpoint& checkpoint, std::set<std::string>& parents) {
    for (const auto* producers : {&component_summons, &direct_points}) for (const auto& [component, pending] : *producers) {
        if (!capture_summon_producer(world, component, pending, producers == &direct_points, checkpoint, parents)) return false;
    }
    return true;
}
static bool capture_child_memberships(const std::string& role, const RecipeNativeCheckpoint::Child& saved, std::vector<RecipeMembership>& members) {
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
    return true;
}
static bool reconcile_summon_child_members(const std::string& role, const std::vector<RecipeMembership>& members) {
    const auto key = role.substr(0, role.rfind('/'));
    const auto previous = recipe_state.requests().at(key).actors.at(role).memberships;
    for (const auto& member : members) if (!recipe_state.join_actor(role, member.owner, member.label)) return false;
    for (const auto& member : previous)
        if (std::find(members.begin(), members.end(), member) == members.end()) recipe_state.detach_actor(role, member.owner, member.label);
    return true;
}
static bool capture_summon_children(uintptr_t world, RecipeNativeCheckpoint& checkpoint, const std::set<std::string>& parents) {
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
        if (!capture_child_memberships(role, saved, members)) return false;
        if (!reconcile_summon_child_members(role, members)) return false;
        checkpoint.children.emplace(role, std::move(saved));
    }
    return true;
}
bool capture_autonomous_summons(RecipeNativeCheckpoint& checkpoint) {
    if (pending_summon_checkpoint) return false;
    checkpoint.next_request = next_recipe_request;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    std::set<std::string> parents;
    if (!capture_summon_producers(world, checkpoint, parents)) return false;
    if (!capture_summon_children(world, checkpoint, parents)) return false;
    return true;
}

void invoke_checkpoint_actor(RecipeCreation* creation, const RecipeNativeCheckpoint::Child* saved) {
    std::array<uint8_t, 0x138> info{};
    unsigned strings = 0;
    __try {
        const auto name = *reinterpret_cast<const char**>(creation->body + 8);
        const auto definition = lookup_definition(definition_database(), name, 1);
        if (!definition) { recipe_creation_failed(*creation, __func__, __LINE__, "definition_missing"); return; }
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

static bool restore_unparented_children(uintptr_t world, RecipeNativeCheckpoint& checkpoint) {
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
    return true;
}
static bool restore_direct_credits(uintptr_t parent, const RecipeNativeCheckpoint::Component& saved) {
        auto& credits = point_credits[parent];
        credits.observed = *reinterpret_cast<const int32_t*>(parent + 0xd90);
        if (credits.observed < 0) return false;
        const auto origin = saved.definition.substr(std::string("ap_v070_direct_r").size());
        for (const auto& [key, progress] : recipe_state.requests()) {
            if (key.rfind("direct/", 0) != 0 || progress.request.origin != origin || !progress.request.physical_units) continue;
            const auto& request = progress.request;
            auto units = progress.emitted * request.logical_units;
            if (request.credit_limit && progress.emitted == request.credit_limit) units -= request.logical_units - request.final_credit_units;
            auto& value = credits.units[request.physical_units];
            if (value > UINT64_MAX - units) return false;
            value += units;
        }
    return true;
}
static void restore_pending_requests(uintptr_t parent, const RecipeNativeCheckpoint::Component& saved, ComponentSummons& pending) {
    pending.parent = parent; pending.identifier = actor_identifier(parent); pending.name = saved.parent;
    pending.epoch = saved.epoch; pending.completed = saved.completed;
    pending.notifications = saved.notifications;
    for (const auto& request : saved.requests) {
        SummonRequest item{request.key, request.origin, {}, request.body_offset, request.completed};
        for (const auto& source : request.sources) item.sources.push_back(thaw_summon_source(source));
        pending.requests.push_back(std::move(item));
    }
}
static bool restore_parented_children(uintptr_t world, uintptr_t component, bool direct, const RecipeNativeCheckpoint::Component& saved, RecipeNativeCheckpoint& checkpoint, bool& waiting) {
    for (auto child = checkpoint.children.begin(); child != checkpoint.children.end();) {
        if (child->second.parent != saved.parent) { ++child; continue; }
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
    return true;
}
static bool restore_summon_template(uintptr_t world, uintptr_t component, const RecipeNativeCheckpoint::Component& saved) {
    const auto target = saved.template_name.empty() ? 0 : find_entity(world, saved.template_name.c_str());
    if (!saved.template_name.empty() && !target) return false;
    assign_manager(component + 0x78, target);
    *reinterpret_cast<uint8_t*>(component + 0x48) = saved.phase;
    *reinterpret_cast<float*>(component + 0x4c) = saved.playback;
    *reinterpret_cast<uint64_t*>(component + 0x68) = saved.mask;
    *reinterpret_cast<uint8_t*>(component + 0x71) = saved.paused;
    *reinterpret_cast<uint8_t*>(component + 0x72) = saved.template_mode;
    return true;
}
static void complete_restored_summon(uintptr_t component, ComponentSummons& pending) {
        original_summon_cleanup(component);
        *reinterpret_cast<uint8_t*>(component + 0x48) = 3;
        pending.completed = true;
        for (auto& request : pending.requests) request.completed = true;
}
static bool restored_summon_component(uintptr_t parent, const RecipeNativeCheckpoint::Component& saved, bool& direct, uintptr_t& component) {
    char definition[512];
    if ((!read_recipe_definition(parent, definition) && !read_definition_name(parent, definition)) || saved.definition != definition) return false;
    direct = saved.definition.rfind("ap_v070_direct_r", 0) == 0;
    component = direct ? parent : *reinterpret_cast<const uintptr_t*>(parent + 0x165e0);
    if (!component || (!direct && *reinterpret_cast<const uintptr_t*>(component + 8) != parent)) return false;
    return true;
}
static bool initialize_restored_producer(uintptr_t world, uintptr_t parent, uintptr_t component, bool direct, const RecipeNativeCheckpoint::Component& saved, ComponentSummons& pending, bool& waiting) {
    if (!pending.parent) {
        if (!direct && !restore_summon_template(world, component, saved)) { waiting = true; return true; }
        restore_pending_requests(parent, saved, pending);
        if (direct && !restore_direct_credits(parent, saved)) return false;
    }
    return true;
}
static bool restore_summon_producer(uintptr_t world, uintptr_t parent, const RecipeNativeCheckpoint::Component& saved, RecipeNativeCheckpoint& checkpoint, bool& waiting) {
    bool direct = false; uintptr_t component = 0;
    if (!restored_summon_component(parent, saved, direct, component)) return false;
    auto& pending = direct ? direct_points[parent] : component_summons[component];
    bool template_waiting = false;
    if (!initialize_restored_producer(world, parent, component, direct, saved, pending, template_waiting)) return false;
    if (template_waiting) { waiting = true; return true; }
    if (pending.parent != parent || pending.identifier != actor_identifier(parent)) return false;
    if (!restore_parented_children(world, component, direct, saved, checkpoint, waiting)) return false;
    if (waiting) { waiting = true; return true; }
    if (!direct) complete_restored_summon(component, pending);
    return true;
}
bool restore_autonomous_summons(uintptr_t world) {
    if (!pending_summon_checkpoint) return true;
    auto& checkpoint = *pending_summon_checkpoint;
    if (!restore_unparented_children(world, checkpoint)) return false;
    for (auto saved = checkpoint.summons.begin(); saved != checkpoint.summons.end();) {
        const auto parent = find_entity(world, saved->parent.c_str());
        if (!parent) { ++saved; continue; }
        bool waiting = false;
        if (!restore_summon_producer(world, parent, *saved, checkpoint, waiting)) return false;
        if (waiting) { ++saved; continue; }
        saved = checkpoint.summons.erase(saved);
    }
    if (checkpoint.summons.empty() && checkpoint.children.empty()) pending_summon_checkpoint.reset();
    return true;
}

static bool create_pending_summon(uintptr_t component, uintptr_t parent, SummonRequest& request, const RecipeProgress& progress, bool& created, bool& retry) {
    RecipeCreation creation;
    const auto previous = worker_recipe; worker_recipe = &request.key;
    const auto previous_offset = worker_body_offset; worker_body_offset = request.body_offset;
    bool ready = false;
    try { ready = begin_recipe_creation(creation, parent, request.origin, progress.request.label); }
    catch (...) { worker_recipe = previous; worker_body_offset = previous_offset; throw; }
    worker_recipe = previous; worker_body_offset = previous_offset;
    if (!ready) { retry = true; return true; }
    const auto source_index = static_cast<size_t>(creation.emission.ordinal * request.sources.size() /
        progress.request.maximum_emissions);
    invoke_summon_creation(&creation, component, &request.sources[source_index]);
    if (recipe_fault.load(std::memory_order_acquire)) return false;
    if (!creation.actor) { retry = true; return true; }
    char parent_name[512];
    if (!read_instance_name(parent, parent_name) || !*parent_name) return false;
    auto& child = summon_children[creation.emission.actor];
    child.parent = parent_name;
    child.body = *reinterpret_cast<const char**>(creation.body + 8);
    created = true;
    return true;
}
static bool drain_summon_requests(uintptr_t component, uintptr_t parent, ComponentSummons& pending, bool& created) {
    for (auto request = pending.requests.begin(); request != pending.requests.end();) {
        const auto& progress = recipe_state.requests().at(request->key);
        while (!progress.closed && progress.emitted < progress.request.maximum_emissions) {
            bool retry = false;
            if (!create_pending_summon(component, parent, *request, progress, created, retry)) return false;
            if (retry) break;
        }
        if (!progress.closed && progress.emitted < progress.request.maximum_emissions) { ++request; continue; }
        recipe_state.cancel(request->key);
        request = pending.requests.erase(request);
    }
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
    if (!drain_summon_requests(component, parent, pending, created)) return false;
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
            !restore_autonomous_summons(*reinterpret_cast<const uintptr_t*>(world_global))) { set_recipe_fault(__func__, __LINE__, "summon_restore_failed"); return; }
    } catch (...) { set_recipe_fault(__func__, __LINE__, "exception"); return; }
    SummonCollection collection{component, {}};
    invoke_summon_start(component, &collection);
    try {
        if (!recipe_fault.load(std::memory_order_acquire) && open_component_summons(collection) && drain_component_summons(component)) return;
    } catch (...) { set_recipe_fault(__func__, __LINE__, "exception"); }
    set_recipe_fault(__func__, __LINE__, "summon_drain_failed");
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
                !restore_autonomous_summons(*reinterpret_cast<const uintptr_t*>(world_global))) { set_recipe_fault(__func__, __LINE__, "summon_restore_failed"); return; }
        } catch (...) { set_recipe_fault(__func__, __LINE__, "exception"); return; }
    }
    original_summon_update(component, frame);
    if (stopped.load(std::memory_order_acquire)) return;
    std::lock_guard<std::recursive_mutex> lock(admission_mutex);
    try { if (drain_component_summons(component)) return; } catch (...) { set_recipe_fault(__func__, __LINE__, "exception"); }
    set_recipe_fault(__func__, __LINE__, "summon_drain_failed");
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
    } catch (...) { set_recipe_fault(__func__, __LINE__, "exception", creation.key.c_str(), creation.actor, manager); }
    if (!ready) { admission.unlock(); return 0; }
    return invoke_recipe_creation(&creation, manager, record, fourth, fifth, group, admission);
}

static bool open_possessed_request(uintptr_t manager, uintptr_t label, const std::string& host_origin, const NativeRecipe& host_recipe, const NativeRecipe& spirit_recipe, std::string& child) {
    if (child.empty()) {
        auto& parent = pending_recipes[{manager, host_origin}];
        std::string label_text;
        if (!native_cstring(label, label_text, 1024) || (parent.empty() && !open_native_request(manager, host_recipe, label_text, parent)) ||
            !open_native_request(manager, spirit_recipe, "", child,
                host_recipe.definition.physical_count ? parent + "/0" : "", parent)) {
            retry_immediate(manager); return false;
        }
    }
    return true;
}
uintptr_t possessed_recipe_locked(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                                   uintptr_t spirit, const std::string& host_origin, const std::string& spirit_origin) {
    if (!load_native_recipes()) { if (!recipe_fault.load()) retry_immediate(manager); return output; }
    const auto host_recipe = native_recipes.find(host_origin), spirit_recipe = native_recipes.find(spirit_origin);
    if (host_recipe == native_recipes.end() || spirit_recipe == native_recipes.end() ||
        host_recipe->second.definition.family != 'p' || spirit_recipe->second.definition.family != 's') {
        set_recipe_fault(__func__, __LINE__, "possessed_recipe_invalid", nullptr, 0, manager); retry_immediate(manager); return output;
    }
    auto& child = pending_recipes[{manager, spirit_origin}];
    if (!open_possessed_request(manager, label, host_origin, host_recipe->second, spirit_recipe->second, child)) return output;
    const auto parent = recipe_state.requests().at(child).request.parent_request;
    if (pending_recipes.find({manager, host_origin}) != pending_recipes.end()) {
        uintptr_t result = output;
        dispatch_recipe_locked(manager, output, type, host, label, false, host_origin, result);
        if (pending_recipes.find({manager, host_origin}) != pending_recipes.end()) return output;
    }
    if (!recipe_state.requests().count(parent)) { set_recipe_fault(__func__, __LINE__, "parent_request_missing", parent.c_str(), 0, manager); retry_immediate(manager); return output; }
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
    if (!marked_host || !marked_spirit) { set_recipe_fault(__func__, __LINE__, "possessed_pair_incomplete", nullptr, 0, manager); retry_immediate(manager); return output; }
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    return invoke_possessed_recipe(manager, output, type, host, label, spirit, &host_origin, &spirit_origin, admission);
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
            set_recipe_fault(__func__, __LINE__, "body_identity_mismatch", nullptr, actor); return actor;
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

uintptr_t possessed_detour_entry(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t host, uintptr_t label,
                            uintptr_t spirit, uint64_t hosts, uintptr_t filter, uint8_t flag) {
    const auto lease = expanded_hooks.enter(27);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_possessed)>(lease.original)(manager, output, type, host, label, spirit, hosts, filter, flag);
        return possessed_detour(manager, output, type, host, label, spirit, hosts, filter, flag);
    } __finally { expanded_hooks.leave(lease); }
}

void summon_start_detour_entry(uintptr_t component) {
    const auto lease = expanded_hooks.enter(28);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_summon_start)>(lease.original)(component);
        return summon_start_detour(component);
    } __finally { expanded_hooks.leave(lease); }
}

float summon_create_detour_entry(uintptr_t component, uintptr_t entry, uintptr_t positions, uint64_t mask, uintptr_t label) {
    const auto lease = expanded_hooks.enter(29);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_summon_create)>(lease.original)(component, entry, positions, mask, label);
        return summon_create_detour(component, entry, positions, mask, label);
    } __finally { expanded_hooks.leave(lease); }
}

void summon_update_detour_entry(uintptr_t component, uintptr_t frame) {
    const auto lease = expanded_hooks.enter(30);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_summon_update)>(lease.original)(component, frame);
        return summon_update_detour(component, frame);
    } __finally { expanded_hooks.leave(lease); }
}

void summon_complete_detour_entry(uintptr_t component) {
    const auto lease = expanded_hooks.enter(31);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_summon_complete)>(lease.original)(component);
        return summon_complete_detour(component);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t actor_factory_detour_entry(uintptr_t info) {
    const auto lease = expanded_hooks.enter(32);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_actor_factory)>(lease.original)(info);
        return actor_factory_detour(info);
    } __finally { expanded_hooks.leave(lease); }
}

void summon_request_complete_detour_entry(uintptr_t target) {
    const auto lease = expanded_hooks.enter(33);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_summon_request_complete)>(lease.original)(target);
        return summon_request_complete_detour(target);
    } __finally { expanded_hooks.leave(lease); }
}

void summon_cleanup_detour_entry(uintptr_t component) {
    const auto lease = expanded_hooks.enter(34);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_summon_cleanup)>(lease.original)(component);
        return summon_cleanup_detour(component);
    } __finally { expanded_hooks.leave(lease); }
}

void role_text_detour_entry(uintptr_t destination, const char* text) {
    const auto lease = expanded_hooks.enter(38);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(assign_native_text)>(lease.original)(destination, text);
        return role_text_detour(destination, text);
    } __finally { expanded_hooks.leave(lease); }
}

void samuel_setup_detour_entry(uintptr_t component, uintptr_t actor) {
    const auto lease = expanded_hooks.enter(39);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_samuel_setup)>(lease.original)(component, actor);
        return samuel_setup_detour(component, actor);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t resurrect_detour_entry(uintptr_t component, int32_t index, const float* position, const float* axis) {
    const auto lease = expanded_hooks.enter(45);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_resurrect_factory)>(lease.original)(component, index, position, axis);
        return resurrect_detour(component, index, position, axis);
    } __finally { expanded_hooks.leave(lease); }
}

void darklord_summon_detour_entry(uintptr_t component) {
    const auto lease = expanded_hooks.enter(46);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_darklord_summon)>(lease.original)(component);
        return darklord_summon_detour(component);
    } __finally { expanded_hooks.leave(lease); }
}

}
