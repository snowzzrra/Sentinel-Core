// Recipe library, creation and worker queues.
#include "expanded_internal.h"

namespace sentinel::expanded::detail {
bool record_point_credit(uintptr_t, const RecipeCreation*);
bool recipe_definition_marker(std::string_view name, bool empty);
uint64_t functional_token(std::string_view value);
bool read_recipe_metadata(uintptr_t world, uintptr_t root, std::string& metadata);
bool vanilla_bridge_group(const NativeRecipe& recipe);
bool immora_breach_group(const NativeRecipe& recipe);
bool finite_wave_ready(std::string_view owner);
bool settle_recipe_creation(RecipeCreation& creation) noexcept;
bool record_point_credit(uintptr_t point, const RecipeCreation* creation);
uintptr_t recipe_parent(const std::string& role);
bool special_recipe_call(RecipeCreation* creation, uintptr_t manager);
bool read_worker_diagnostic(uintptr_t worker, WorkerDiagnostic& value);
void remember_worker_diagnostic(uintptr_t worker) noexcept;
bool prepare_recipe_worker(uintptr_t worker, bool maintenance, bool& marked);
void invoke_recipe_worker(uintptr_t worker, bool maintenance, const std::string* previous, bool marked, std::unique_lock<std::recursive_mutex>& admission);
void recipe_worker_tick(uintptr_t worker, bool maintenance);
void prepare_queued_recipe(uintptr_t command);
bool queue_recipe_delay(uintptr_t manager);
void invoke_recipe_dispatch(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                             uintptr_t label, bool group, const std::string* origin, uintptr_t* result, std::unique_lock<std::recursive_mutex>& admission);
#ifdef SC_NATIVE_TESTING
bool recipe_fault_call(uintptr_t worker);
#endif
}

namespace sentinel::expanded::detail {

RecipeLibrary recipe_library;

std::map<std::string, NativeRecipe> native_recipes;

RecipeState recipe_state;

uint64_t recipe_stamp = 0;

uint64_t next_recipe_request = 0;

std::string session_recipe_plan, checkpoint_recipe_plan;

bool checkpoint_recipe_missing = false;

std::map<std::pair<uintptr_t, std::string>, std::string> pending_recipes;

thread_local RecipeCreation* recipe_creation = nullptr;

thread_local const std::string* worker_recipe = nullptr;

thread_local uint64_t worker_body_offset = 0;

WorkerTick original_maintain_tick = nullptr, original_stagger_tick = nullptr;

QueueCommand original_queue_command = nullptr;

std::map<uintptr_t, std::string> worker_recipes;

std::map<uintptr_t, WorkerDiagnostic> worker_diagnostics;

uint32_t worker_trace_count = 0;

std::map<uintptr_t, std::string> recipe_actors;

std::map<uintptr_t, ActorCredit> recipe_actor_credits;

std::map<uintptr_t, ActorDiagnostic> actor_diagnostics;

uint64_t actor_diagnostic_next = 0;

uint32_t registration_trace_count = 0;

static bool actor_position_jump(ActorDiagnostic& value, const std::array<float, 3>& xyz) {
        double distance = 0;
        for (size_t i = 0; i < xyz.size(); ++i) distance += std::pow(double(xyz[i]) - value.position[i], 2);
        value.position = xyz;
        if (distance < 20 * 20) return false;
        ++value.jumps;
    return true;
}
static bool readable_actor_position(uintptr_t actor, std::array<float, 3>& xyz) {
    engine::LocalMemory memory;
    return !memory.copy(actor + 0x74, xyz.data(), sizeof(xyz)).reason &&
        std::all_of(xyz.begin(), xyz.end(), [](float value) { return std::isfinite(value); });
}
void trace_actor(uintptr_t actor, const char* reason, uintptr_t source, bool position) noexcept {
    try {
        const auto found = actor_diagnostics.find(actor);
        if (found == actor_diagnostics.end()) return;
        auto& value = found->second;
        if (position && value.positioned && value.jumps >= 3) return;
        std::array<float, 3> xyz{};
        const bool readable = position && readable_actor_position(actor, xyz);
        if (position && !readable) return;
        const bool initial = position && !value.positioned;
        if (position && value.positioned && !actor_position_jump(value, xyz)) return;
        if (readable) { value.position = xyz; value.positioned = true; }
        commands::diagnostic_trace("%s actor=%llx id=%u manager=%.160s encounter=%.192s reason=%s source=%llx position=%d xyz=%g,%g,%g jumps=%u",
            initial ? "AP_ENEMY_SPAWN_POSITION" : "AP_ENEMY_ACTOR", static_cast<unsigned long long>(actor), value.identifier, value.manager.c_str(), value.encounter.c_str(), reason,
            static_cast<unsigned long long>(source), readable, xyz[0], xyz[1], xyz[2], value.jumps);
    } catch (...) {}
}

std::atomic<bool> recipe_fault{false};

SRWLOCK recipe_fault_lock = SRWLOCK_INIT;

char first_recipe_fault[1536]{};

void recipe_fault_body(uintptr_t actor, char (&body)[192]) noexcept {
    strcpy_s(body, "unknown");
    char observed[512];
    if (actor && read_recipe_definition(actor, observed)) strncpy_s(body, observed, _TRUNCATE);
    __try {
        if (recipe_creation && recipe_creation->body) {
            const auto name = *reinterpret_cast<const char**>(recipe_creation->body + 8);
            if (name) strncpy_s(body, name, _TRUNCATE);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void format_recipe_fault(const char* function, unsigned line, const char* reason, const char* key,
                         uintptr_t actor, uintptr_t manager) noexcept {
    const char* origin = "";
    const char* owner = "";
    for (const auto& request : recipe_state.requests()) if (request.first == key) {
        origin = request.second.request.origin.c_str(); owner = request.second.request.owner.c_str(); break;
    }
    char body[192]; recipe_fault_body(actor, body);
    const auto scope = native::inspect();
    std::snprintf(first_recipe_fault, sizeof(first_recipe_fault),
        "function=%.80s reason=%.96s request=%.160s actor=%llx recipe=%.80s manager=%llx owner=%.160s body=%.191s map=%.191s generation=%llu stamp=%llu at_ms=%llu line=%u",
        function, reason, key, actor, origin, manager, owner, body,
        recipe_state.map().empty() ? scope.current_map.bytes : recipe_state.map().c_str(),
        scope.scope.lifecycle_generation, native::observation_stamp(), GetTickCount64(), line);
}

static void recipe_fault_context(const char*& key, uintptr_t& actor, uintptr_t& manager) {
    if (!key) key = recipe_creation ? recipe_creation->key.c_str() : worker_recipe ? worker_recipe->c_str() : "";
    if (!actor && recipe_creation) actor = recipe_creation->actor;
    if (!manager && recipe_creation) manager = recipe_creation->selecting_manager;
    const auto diagnostic = actor_diagnostics.find(actor);
    if (diagnostic != actor_diagnostics.end()) {
        if (!*key) key = diagnostic->second.encounter.c_str();
        if (!manager) manager = diagnostic->second.manager_address;
    }
}
void set_recipe_fault(const char* function, unsigned line, const char* reason, const char* key,
                      uintptr_t actor, uintptr_t manager) noexcept {
    recipe_fault.store(true, std::memory_order_release);
    AcquireSRWLockExclusive(&recipe_fault_lock);
    if (!first_recipe_fault[0]) {
        recipe_fault_context(key, actor, manager);
        format_recipe_fault(function, line, reason, key, actor, manager);
        const auto error = startup_log::recipe_fault(first_recipe_fault);
        commands::diagnostic_trace("AP_RECIPE_FAULT %.600s log_io_error=%u", first_recipe_fault, error);
    }
    ReleaseSRWLockExclusive(&recipe_fault_lock);
}

void recipe_creation_failed(RecipeCreation& creation, const char* function, unsigned line, const char* reason) noexcept {
    creation.failed = true;
    const auto found = recipe_state.requests().find(creation.key);
    const bool retryable = !creation.actor && !creation.restoring && found != recipe_state.requests().end() &&
        found->second.reserved && found->second.emitted == creation.emission.ordinal &&
        !found->second.actors.count(creation.emission.actor);
    if (!retryable) {
        set_recipe_fault(function, line, reason, creation.key.c_str(), creation.actor, creation.selecting_manager);
        return;
    }
    commands::diagnostic_trace("AP_RECIPE_RETRY function=%s reason=%s request=%.160s ordinal=%llu stamp=%llu",
        function, reason, creation.key.c_str(), creation.emission.ordinal, native::observation_stamp());
}

std::map<std::string, uint8_t> recipe_spawn_traces;

AllocateWorker allocate_worker = nullptr;

ConstructStagger construct_stagger = nullptr;

AssignManager assign_manager = nullptr;

NativeClock native_clock = nullptr;

NativeStep native_step = nullptr;

NativeDuration native_duration = nullptr;

ConstructTime construct_time = nullptr;

SpawnPossessed original_possessed = nullptr;

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

bool read_recipe_definition(uintptr_t ai, char (&copy)[512], bool empty) noexcept {
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

static bool recipe_chunk_header(const std::string& metadata, uint64_t& count, uint64_t& size, uint64_t& checksum) {
    const auto number = [&](size_t at, size_t length, uint64_t& value) {
        const auto field = std::string_view(metadata).substr(at, length);
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value, 16);
        return parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size();
    };
    if (!number(6, 8, count) || !number(15, 8, size) || !number(24, 16, checksum) ||
        !size || size > maximum_recipe_metadata || count != (size + 1799) / 1800) return false;
    return true;
}
bool read_recipe_metadata(uintptr_t world, uintptr_t root, std::string& metadata) {
    if (!native_text(root + 0xbe0, metadata, maximum_recipe_metadata)) return false;
    if (metadata.rfind("EAPC1|", 0) != 0) return true;
    if (metadata.size() != 40 || metadata[14] != '|' || metadata[23] != '|') return false;
    uint64_t count = 0, size = 0, checksum = 0;
    if (!recipe_chunk_header(metadata, count, size, checksum)) return false;
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

bool load_recipe_metadata(uintptr_t world,uintptr_t root,RecipeLibrary& parsed) {
    std::string metadata;
    if (!read_recipe_metadata(world, root, metadata) || !parse_recipe_library(metadata, parsed)) {
        set_recipe_fault(__func__, __LINE__, "metadata_invalid");
        if (save::session().campaign_run.enabled()) save::session().campaign_run.refuse("native_enemy_recipe_metadata_invalid");
        return false;
    }
    return true;
}

static bool read_primary_recipe_bodies(uintptr_t data, int32_t count, const RecipeLibraryEntry& entry, bool partial, NativeRecipe& recipe) {
    for (int32_t i = 0; i < count; ++i) {
        const auto body = data + static_cast<uintptr_t>(i) * 0x80;
        std::string name;
        if (!native_text(body, name, 511)) return false;
        const auto tracking = spawn_recipe(name);
        if (!tracking || std::string(tracking->origin.data(), tracking->origin.size()) != entry.origin ||
            tracking->logical_units != entry.logical_count || tracking->physical_units != entry.physical_count) return false;
        const auto masks = spawn_tracking(name);
        if (masks->source != entry.logical_mask) return false;
        if (tracking->credit_units != entry.logical_count) {
            if (!partial || recipe.tail || tracking->credit_units != entry.final_credit_units ||
                tracking->body != (entry.maximum_emissions - 1) % entry.physical_count) return false;
            recipe.tail = body;
        } else {
            if (recipe.bodies[tracking->body]) return false;
            recipe.bodies[tracking->body] = body;
        }
    }
    return true;
}
static bool read_recipe_refill(uintptr_t world, const RecipeLibraryEntry& entry, NativeRecipe& recipe) {
    if (const auto refill = find_entity(world, ("ap_v070_refill_r" + entry.origin).c_str())) {
        std::string value;
        if (entry.family != 'm' || entry.maximum_emissions || !native_text(refill + 0xbe0, value, 10)) return false;
        const auto parsed_limit = std::from_chars(value.data(), value.data() + value.size(), recipe.refill_limit);
        if (parsed_limit.ec != std::errc{} || parsed_limit.ptr != value.data() + value.size() ||
            !recipe.refill_limit || recipe.refill_limit > INT32_MAX) return false;
    }
    return true;
}
static bool read_compatible_recipe_bodies(uintptr_t world, const RecipeLibraryEntry& entry, bool partial, NativeRecipe& recipe) {
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
    return true;
}
static bool add_counter_body(NativeRecipe& recipe, uintptr_t primary, uintptr_t replacement, std::string name, const std::string& original) {
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
    return true;
}
static bool recipe_body_matches(const SpawnRecipe& body, const SpawnTracking& tracking, const RecipeLibraryEntry& entry) {
    return std::string_view(body.origin.data(), body.origin.size()) == entry.origin &&
        std::tie(body.logical_units, body.physical_units, tracking.source) ==
            std::tie(entry.logical_count, entry.physical_count, entry.logical_mask);
}
static bool read_counter_bodies(uintptr_t values, int32_t length, const RecipeLibraryEntry& entry, bool partial, NativeRecipe& recipe) {
    for (int32_t index = 0; index < length; ++index) {
        const auto replacement = values + static_cast<uintptr_t>(index) * 0x80;
        std::string name, original;
        if (!native_text(replacement, name, 511)) return false;
        const auto body = spawn_recipe(name);
        const auto tracking = spawn_tracking(name);
        if (!body || !tracking || !recipe_body_matches(*body, *tracking, entry) || body->body >= recipe.bodies.size()) return false;
        const auto primary = body->credit_units == entry.logical_count ? recipe.bodies[body->body] :
            partial && body->credit_units == entry.final_credit_units ? recipe.tail : 0;
        if (!primary || !native_text(primary, original, 511)) return false;
        if (!add_counter_body(recipe, primary, replacement, std::move(name), original)) return false;
    }
    return true;
}
static bool read_recipe_counters(uintptr_t world, const RecipeLibraryEntry& entry, bool partial, NativeRecipe& recipe) {
    if (const auto alternate = find_entity(world, ("ap_v070_counter_r" + entry.origin).c_str())) {
        const auto length = *reinterpret_cast<const int32_t*>(alternate + 0xc40);
        const auto values = *reinterpret_cast<const uintptr_t*>(alternate + 0xc38);
        if (length < 0 || static_cast<uint64_t>(length) != static_cast<uint64_t>(entry.physical_count) + partial ||
            (length && !values)) return false;
        if (!read_counter_bodies(values, length, entry, partial, recipe)) return false;
    }
    return true;
}
static bool read_native_recipe(uintptr_t world, const RecipeLibraryEntry& entry, NativeRecipe& recipe) {
    const auto library = find_entity(world, ("ap_v070_bodies_r" + entry.origin).c_str());
    if (!library) return false;
    const auto count = *reinterpret_cast<const int32_t*>(library + 0xc40);
    const auto data = *reinterpret_cast<const uintptr_t*>(library + 0xc38);
    const bool partial = entry.maximum_emissions && entry.final_credit_units < entry.logical_count;
    if (count < 0 || static_cast<uint64_t>(count) != static_cast<uint64_t>(entry.physical_count) + partial) return false;
    recipe = NativeRecipe{entry, std::vector<uintptr_t>(entry.physical_count), 0};
    if (!read_primary_recipe_bodies(data, count, entry, partial, recipe)) return false;
    if (std::any_of(recipe.bodies.begin(), recipe.bodies.end(), [](auto value) { return !value; }) ||
        (partial && !recipe.tail)) return false;
    recipe.passthrough=find_entity(world,("ap_v070_passthrough_r"+entry.origin).c_str())!=0;
    if (!read_recipe_refill(world, entry, recipe)) return false;
    if (!read_compatible_recipe_bodies(world, entry, partial, recipe)) return false;
    if (!read_recipe_counters(world, entry, partial, recipe)) return false;
    return true;
}
static bool invalid_recipe_checkpoint() {
            set_recipe_fault(__func__, __LINE__, "checkpoint_invalid");
            if (save::session().campaign_run.enabled()) save::session().campaign_run.refuse("native_enemy_checkpoint_invalid");
            return false;
}

static bool validate_checkpoint_shows(RecipeNativeCheckpoint& checkpoint, const std::map<std::string, NativeRecipe>& recipes) {
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
        if (!wanted.count(name)) return invalid_recipe_checkpoint();
    }
    for (const auto& [origin,recipe] : recipes) {
        (void)origin;
        if (!recipe.passthrough) continue;
        checkpoint.shows.erase(recipe.definition.source);
        for (uint64_t index=0;index<recipe.bodies.size();++index)
            checkpoint.shows.erase(independent_body_name(recipe,index));
    }
    return true;
}
static bool recipe_sequence(std::string_view key, size_t start, uint64_t maximum) {
    uint64_t sequence = 0;
    const auto parsed = std::from_chars(key.data() + (std::min)(start, key.size()), key.data() + key.size(), sequence);
    return parsed.ec == std::errc{} && parsed.ptr == key.data() + key.size() && sequence && sequence <= maximum;
}
static bool independent_request(const RecipeProgress& progress, const RecipeLibraryEntry& entry) {
    const RecipeRequest expected{entry.source, entry.origin, "", entry.logical_mask, 1, entry.physical_count,
        entry.maximum_emissions, entry.physical_count, entry.physical_count ? 1U : 0U};
    return entry.family == 'd' && progress.request == expected && progress.emitted <= progress.request.maximum_emissions;
}
static bool autonomous_quantity(const std::string& key, const RecipeRequest& request, const RecipeLibraryEntry& definition) {
    const auto numerator = definition.quantity_denominator ? definition.quantity_numerator : definition.physical_count;
    const auto denominator = definition.quantity_denominator ? definition.quantity_denominator : definition.logical_count;
    if (key.rfind("summon/", 0) == 0) {
        const auto expected = (uint64_t{request.logical_units} * numerator + denominator - 1) / denominator;
        if (expected != request.physical_units) return invalid_recipe_checkpoint();
    } else if (std::tie(request.logical_units, request.physical_units) != std::tie(definition.logical_count, definition.physical_count) ||
        request.owner != (definition.owner.empty() ? "ap_v070_direct_r" + definition.origin : definition.owner)) return invalid_recipe_checkpoint();
    return true;
}
static bool validate_autonomous_request(const RecipeNativeCheckpoint& checkpoint, const std::string& key, const RecipeProgress& progress, const std::map<std::string, NativeRecipe>& recipes) {
    const auto recipe = recipes.find(progress.request.origin);
    const auto& request = progress.request;
    const auto slash = key.rfind('/');
    if (recipe == recipes.end()) return invalid_recipe_checkpoint();
    const auto& entry = recipe->second.definition;
    const auto final_units = request.physical_units ? request.logical_units : 0U;
    if (entry.family != (key.rfind("direct/", 0) == 0 ? 'f' : 'u') ||
        std::tie(request.logical_mask, request.physical_units, request.credit_limit, request.final_credit_units) !=
            std::tie(entry.logical_mask, request.maximum_emissions, request.maximum_emissions, final_units) ||
        key.substr(7, 20) != request.origin || !request.parent_actor.empty() || !request.parent_request.empty() ||
        !recipe_sequence(key, slash + 1, checkpoint.next_request)) return invalid_recipe_checkpoint();
    return autonomous_quantity(key, request, entry);
}
static bool validate_independent_request(const RecipeNativeCheckpoint& checkpoint, const std::string& key, const RecipeProgress& progress, const std::map<std::string, NativeRecipe>& recipes) {
    const auto recipe = recipes.find(progress.request.origin);
    const auto base = "independent/" + progress.request.origin;
    if (key != base) {
        if (progress.request.physical_units || key.rfind(base + "/", 0) != 0 ||
            !recipe_sequence(key, base.size() + 1, checkpoint.next_request)) return invalid_recipe_checkpoint();
    }
    if (recipe == recipes.end() || !independent_request(progress, recipe->second.definition)) return invalid_recipe_checkpoint();
    for (const auto& [id, actor] : progress.actors) {
        (void)id;
        if (actor.native_name != independent_body_name(recipe->second, actor.ordinal)) return invalid_recipe_checkpoint();
    }
    return true;
}
static bool validate_checkpoint_requests(const RecipeNativeCheckpoint& checkpoint, const std::map<std::string, NativeRecipe>& recipes, const RecipeState& loaded) {
    for (const auto& [key, progress] : loaded.requests()) {
        if (key.rfind("summon/", 0) == 0 || key.rfind("direct/", 0) == 0) {
            if (!validate_autonomous_request(checkpoint, key, progress, recipes)) return false;
            continue;
        }
        if (!validate_independent_request(checkpoint, key, progress, recipes)) return false;
    }
    return true;
}
static bool validate_checkpoint_actions(const RecipeNativeCheckpoint& checkpoint, const RecipeState& loaded) {
    for (const auto& [key, credit] : checkpoint.actions) {
        (void)credit;
        if (!loaded.requests().count(key.first) && std::none_of(checkpoint.identities.begin(), checkpoint.identities.end(),
            [&](const auto& item) { return item.second.request == key.first; })) return invalid_recipe_checkpoint();
    }
    return true;
}
static bool validate_checkpoint_children(const RecipeNativeCheckpoint& checkpoint, const std::map<std::string, NativeRecipe>& recipes, const RecipeState& loaded) {
    for (const auto& [role, child] : checkpoint.children) {
        const auto tracking = spawn_recipe(child.body);
        const auto& progress = loaded.requests().at(role.substr(0, role.rfind('/')));
        const auto recipe = recipes.find(progress.request.origin);
        if (!tracking || recipe == recipes.end() || (recipe->second.definition.family != 'u' && recipe->second.definition.family != 'f') ||
            std::string(tracking->origin.data(), tracking->origin.size()) != progress.request.origin ||
            std::none_of(recipe->second.bodies.begin(), recipe->second.bodies.end(), [&](uintptr_t body) {
                std::string name; return native_text(body, name, 511) && name == child.body;
            }) && std::find(recipe->second.compatible_bodies.begin(), recipe->second.compatible_bodies.end(),
                            child.body) == recipe->second.compatible_bodies.end()) return invalid_recipe_checkpoint();
        (void)role;
    }
    return true;
}
static bool checkpoint_identity_credit(const RecipeNativeCheckpoint& checkpoint, const RecipeNativeCheckpoint::Identity& identity, const NativeRecipe& recipe, const RecipeState& loaded, const SpawnRecipe& frozen, uint64_t& units, uint32_t& denominator) {
    const auto progress = loaded.requests().find(identity.request);
    if (progress != loaded.requests().end()) {
        const auto& request = progress->second.request;
        if (request.origin != recipe.definition.origin || identity.ordinal >= progress->second.emitted) return invalid_recipe_checkpoint();
        units = request.credit_limit && identity.ordinal + 1 == request.credit_limit ? request.final_credit_units : request.logical_units;
        denominator = request.physical_units;
    } else {
        const auto& definition = recipe.definition;
        const auto prefix = definition.origin + "/";
        if (std::string_view("duf").find(definition.family) != std::string_view::npos || identity.request.rfind(prefix, 0) != 0 ||
            !recipe_sequence(identity.request, prefix.size(), checkpoint.next_request) ||
            (definition.maximum_emissions && identity.ordinal >= definition.maximum_emissions) ||
            recipe.bodies.empty() || frozen.body != identity.ordinal % recipe.bodies.size()) return invalid_recipe_checkpoint();
        units = definition.maximum_emissions && identity.ordinal + 1 == definition.maximum_emissions ?
            definition.final_credit_units : definition.logical_count;
        denominator = definition.physical_count;
    }
    return true;
}
static bool validate_checkpoint_identities(const RecipeNativeCheckpoint& checkpoint, const std::map<std::string, NativeRecipe>& recipes, const RecipeState& loaded) {
    for (const auto& [name, identity] : checkpoint.identities) {
        (void)name;
        const auto frozen = spawn_recipe(identity.body);
        if (!frozen || !recipes.count(std::string(frozen->origin.data(), frozen->origin.size()))) return invalid_recipe_checkpoint();
        const auto& recipe = recipes.at(std::string(frozen->origin.data(), frozen->origin.size()));
        if (std::none_of(recipe.bodies.begin(), recipe.bodies.end(), [&](uintptr_t body) {
            std::string value; return native_text(body, value, 511) && value == identity.body;
        }) && std::find(recipe.compatible_bodies.begin(),recipe.compatible_bodies.end(),identity.body)==recipe.compatible_bodies.end() &&
            (!recipe.tail || [&] { std::string value; return !native_text(recipe.tail, value, 511) || value != identity.body; }())) return invalid_recipe_checkpoint();
        uint64_t units = 0; uint32_t denominator = 0;
        if (!checkpoint_identity_credit(checkpoint, identity, recipe, loaded, *frozen, units, denominator)) return false;
        if (identity.units != units || identity.denominator != denominator) return invalid_recipe_checkpoint();
    }
    return true;
}
static bool apply_recipe_checkpoint(uintptr_t root, const RecipeNativeCheckpoint& checkpoint) {
    if (recipe_creation || reserved_actors) return invalid_recipe_checkpoint();
    map_freed();
    native_string_table = *reinterpret_cast<const uintptr_t*>(root + 0xbe0);
    restored_actor_identities = checkpoint.identities;
    pending_independent_shows = checkpoint.shows;
    for (const auto& [key, credit] : checkpoint.actions) {
        death_action_credits[key] = {credit.units, credit.sent, credit.denominator};
    }
    next_recipe_request = checkpoint.next_request;
    if (!checkpoint.summons.empty() || !checkpoint.children.empty()) pending_summon_checkpoint = checkpoint;
    return true;
}
static bool restore_recipe_library_checkpoint(uintptr_t root, const RecipeLibrary& parsed, const std::map<std::string, NativeRecipe>& recipes, RecipeState& loaded) {
    if (!restored_independent) return true;
    auto checkpoint = *restored_independent;
    if (checkpoint.plan != parsed.plan || checkpoint.map != parsed.map) return invalid_recipe_checkpoint();
    loaded = std::move(checkpoint.decoded_state);
    if (!validate_checkpoint_shows(checkpoint, recipes)) return false;
    if (!validate_checkpoint_requests(checkpoint, recipes, loaded)) return false;
    if (!validate_checkpoint_actions(checkpoint, loaded)) return false;
    if (!validate_checkpoint_children(checkpoint, recipes, loaded)) return false;
    if (!validate_checkpoint_identities(checkpoint, recipes, loaded)) return false;
    if (!apply_recipe_checkpoint(root, checkpoint)) return false;
    restored_independent.reset();
    return true;
}
static bool matching_recipe_plan(const RecipeLibrary& parsed) {
    if (checkpoint_recipe_missing || (!checkpoint_recipe_plan.empty() && parsed.plan != checkpoint_recipe_plan) ||
        (!session_recipe_plan.empty() && parsed.plan != session_recipe_plan)) {
        set_recipe_fault(__func__, __LINE__, "checkpoint_plan_mismatch");
        if (save::session().campaign_run.enabled()) save::session().campaign_run.refuse("native_enemy_checkpoint_plan_mismatch");
        return false;
    }
    return true;
}
bool load_native_recipes() {
    if (save::session().campaign_run.snapshot().native_subtype==6) return false;
    if (!native_recipes.empty() && !restored_independent) return true;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto root = find_entity(world, "ap_v070_recipe_plan");
    RecipeLibrary parsed;
    if (!root) return false;
    if (!load_recipe_metadata(world,root,parsed)) return false;
    if (!matching_recipe_plan(parsed)) return false;
    native_string_table = *reinterpret_cast<const uintptr_t*>(root + 0xbe0);
    std::map<std::string, NativeRecipe> recipes;
    for (const auto& entry : parsed.recipes) {
        NativeRecipe recipe;
        if (!read_native_recipe(world, entry, recipe)) return false;
        recipes.emplace(entry.origin, std::move(recipe));
    }
    RecipeState loaded;
    if (!loaded.reset(parsed.plan, parsed.map)) return false;
    if (!restore_recipe_library_checkpoint(root, parsed, recipes, loaded)) return false;
    recipe_state = std::move(loaded);
    recipe_stamp = native::observation_stamp();
    session_recipe_plan = parsed.plan;
    checkpoint_recipe_plan.clear();
    recipe_library = std::move(parsed); native_recipes = std::move(recipes);
    independent_memberships_pending = true;
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
                          const std::string& parent_actor, const std::string& parent_request) {
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
                             bool armored) {
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

static const char* counter_fallback(bool spirit, bool imp, bool baron) {
    if (spirit) return "microwave_fallback";
    if (imp) return "stone_counter_fallback";
    return baron ? "armored_counter_fallback" : "blood_punch_fallback";
}
static const char* counter_species(bool spirit, bool imp, bool baron) {
    if (spirit) return "cacodemon";
    if (imp) return "imp";
    return baron ? "baron" : "prowler";
}
static void choose_recipe_counter(RecipeCreation& creation, const NativeRecipe& recipe) {
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
            commands::enemy_spawn_trace(counter_fallback(spirit, imp, baron), recipe.definition.origin.c_str(),
                *reinterpret_cast<const char**>(chosen + 8), counter_species(spirit, imp, baron), 0,
                creation.key.c_str(), recipe_state.requests().at(creation.key).request.parent_actor.c_str(), creation.emission.ordinal);
    }
}
static void resolve_recipe_source(RecipeCreation& creation, const NativeRecipe& recipe) {
    const auto source = immora_breach_group(recipe) ? "breach_wasteland_arena_target_spawn_60" : recipe.definition.source.c_str();
    creation.source = !*source ? 0 : find_entity(*reinterpret_cast<const uintptr_t*>(world_global), source);
    if (!creation.source && (recipe.definition.family == 'i' || recipe.definition.family == 'p' || immora_breach_group(recipe))) {
        // checkpoints can omit passive source entities; the compiler supplies a separate point.
        char fallback[64];
        sprintf_s(fallback, "ap_v070_source_%016llx", functional_token(source));
        const auto alias = find_entity(*reinterpret_cast<const uintptr_t*>(world_global), fallback);
        if (alias && *reinterpret_cast<const int32_t*>(alias + 0xc40) > 0) creation.source = alias;
    }
}
static bool creation_request_key(RecipeCreation& creation, uintptr_t manager, std::string_view origin, std::string_view label, const NativeRecipe& recipe) {
    if (worker_recipe) creation.key = *worker_recipe;
    else {
        auto& pending = pending_recipes[{manager, std::string(origin)}];
        if (pending.empty() && !open_native_request(manager, recipe, label, pending)) { trace_spawn_refusal(manager, origin, "request_not_opened"); return false; }
        creation.key = pending;
    }
    return true;
}
static bool admit_recipe_creation(RecipeCreation& creation, uintptr_t manager, std::string_view origin, const NativeRecipe& recipe) {
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
    return true;
}
bool begin_recipe_creation(RecipeCreation& creation, uintptr_t manager, std::string_view origin, std::string_view label) {
    if (recipe_fault.load(std::memory_order_acquire)) { trace_spawn_refusal(manager, origin, "recipe_fault"); return false; }
    if (!load_native_recipes()) { trace_spawn_refusal(manager, origin, "recipe_library_unavailable"); return false; }
    const auto found = native_recipes.find(std::string(origin));
    if (found == native_recipes.end()) { trace_spawn_refusal(manager, origin, "recipe_missing"); return false; }
    const auto& recipe = found->second;
    if (vanilla_bridge_group(recipe)) return false;
    if (!creation_request_key(creation, manager, origin, label, recipe)) return false;
    if (recipe.definition.family == 'm' && finite_wave_ready(recipe_state.requests().at(creation.key).request.owner)) { trace_spawn_refusal(manager, origin, "finite_wave_ready"); return false; }
    auto emission = recipe_state.reserve(creation.key);
    if (!emission) { trace_spawn_refusal(manager, origin, "recipe_reservation_unavailable"); return false; }
    creation.emission = std::move(*emission); creation.recipe = &recipe;
    creation.body = recipe.tail && emission->ordinal + 1 == recipe.definition.maximum_emissions ? recipe.tail :
        recipe.bodies[(emission->ordinal % recipe.bodies.size() + worker_body_offset % recipe.bodies.size()) % recipe.bodies.size()];
    choose_recipe_counter(creation, recipe);
    if (!admit_recipe_creation(creation, manager, origin, recipe)) return false;
    resolve_recipe_source(creation, recipe);
    reserve_admission(creation.limits); creation.reserved = true;
    creation.previous = recipe_creation; recipe_creation = &creation;
    return true;
}

static void remember_creation_diagnostic(const RecipeCreation& creation, uint32_t identifier, const RecipeRequest& request) {
    try {
        if (actor_diagnostics.size() < 4096 || actor_diagnostics.count(creation.actor)) {
            auto& diagnostic = actor_diagnostics[creation.actor];
            if (diagnostic.identifier != identifier) diagnostic = {};
            diagnostic.identifier = identifier; diagnostic.encounter = creation.key;
            diagnostic.manager = request.owner;
            trace_actor(creation.actor, creation.restoring ? "restored_position" : "spawn_position", 0, true);
        }
    } catch (...) {}
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
        remember_creation_diagnostic(creation, identifier, request);
        return true;
    } catch (...) { set_recipe_fault(__func__, __LINE__, "exception", creation.key.c_str(), creation.actor, creation.selecting_manager); return false; }
}

void end_recipe_creation(RecipeCreation& creation) noexcept {
    try {
        if (!creation.actor) trace_recipe_spawn(creation, creation.body, creation.source, 2);
        if (creation.actor && creation.active_point && !record_point_credit(creation.active_point, &creation))
            set_recipe_fault(__func__, __LINE__, "point_credit_failed", creation.key.c_str(), creation.actor, creation.selecting_manager);
    } catch (...) { set_recipe_fault(__func__, __LINE__, "exception", creation.key.c_str(), creation.actor, creation.selecting_manager); }
    if (!settle_recipe_creation(creation)) set_recipe_fault(__func__, __LINE__, "settlement_failed", creation.key.c_str(), creation.actor, creation.selecting_manager);
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

static void select_functional_source(RecipeCreation* creation, RecipeCreation::PointState& source, uintptr_t world, uintptr_t group, bool inherited) {
    if (source.index < 0) {
        auto& random = *reinterpret_cast<uint32_t*>(world + 0x9a038);
        random = random * 0x19660dU + 0x3c6ef35fU;
        source.index = static_cast<int32_t>((random >> 10 & 0x7fff) % source.count);
        if (inherited) *reinterpret_cast<int32_t*>(group + 0xc54) = source.index;
        else creation->original.index = source.index;
    }
}
static uintptr_t find_functional_variant(uintptr_t library, const char* variant, bool& readable) {
    const auto data = *reinterpret_cast<const uintptr_t*>(library + 0xc38);
    const auto count = *reinterpret_cast<const int32_t*>(library + 0xc40);
    if (count < 1 || count > 100000 || !data) { readable = false; return 0; }
    for (int32_t index = 0; index < count; ++index) {
        const auto entry = data + static_cast<uintptr_t>(index) * 0x80;
        std::string name;
        if (!native_text(entry, name, 511)) { readable = false; return 0; }
        if (name == variant) return entry;
    }
    return 0;
}
uintptr_t functional_body(RecipeCreation* creation, uintptr_t group) {
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    if (!find_entity(world, "ap_v070_functional_sources")) return creation->body;
    auto source = creation->original;
    const bool inherited = source.count < 1 && group;
    if (inherited) source = read_point_state(group);
    if (source.count < 1) return creation->body;
    if (source.count > 100000 || source.index >= source.count || !source.definitions) return 0;
    select_functional_source(creation, source, world, group, inherited);
    std::string source_name, body_name;
    if (!native_text(source.definitions + static_cast<uintptr_t>(source.index) * 0x80, source_name, 511)) return 0;
    char library_name[64];
    sprintf_s(library_name, "ap_v070_functional_%016llx", functional_token(source_name));
    const auto library = find_entity(world, library_name);
    if (!library) return creation->body;
    if (!native_text(creation->body, body_name, 511)) return 0;
    char variant[96];
    sprintf_s(variant, "%s_%016llx", library_name, functional_token(body_name));
    bool readable = true;
    if (const auto entry = find_functional_variant(library, variant, readable)) return entry;
    if (!readable) return 0;
    commands::enemy_spawn_trace("functional_missing", creation->recipe->definition.origin.c_str(), variant, source_name.c_str(), 0,
        creation->key.c_str(), recipe_state.requests().at(creation->key).request.parent_actor.c_str(), creation->emission.ordinal);
    return 0;
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
    if (!spirit) { set_recipe_fault(__func__, __LINE__, "spirit_missing", creation->key.c_str(), actor); return; }
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
    try { ready = begin_recipe_creation(creation, manager, origin, label); } catch (...) { set_recipe_fault(__func__, __LINE__, "exception", creation.key.c_str(), creation.actor, manager); }
    if (!ready) { admission.unlock(); return 0; }
    return invoke_recipe_creation(&creation, manager, 0, 0, 0, false, admission);
}

bool recipe_landing_pose(RecipeCreation& creation, float* pose) {
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
    if (!native_text(helper + 0xbe0, target_name, 191)) { recipe_creation_failed(creation, __func__, __LINE__, "source_name_unreadable"); return false; }
    const auto target = find_entity(world, target_name.c_str());
    if (!target) { recipe_creation_failed(creation, __func__, __LINE__, "source_missing"); return false; }
    std::memcpy(pose, reinterpret_cast<const void*>(target + 0x74), 12 * sizeof(float));
    if (!std::all_of(pose, pose + 12, [](float value) { return std::isfinite(value); })) {
        recipe_creation_failed(creation, __func__, __LINE__, "source_pose_invalid"); return false;
    }
    return true;
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

static bool finish_bridge_worker(uintptr_t worker, bool maintenance, const NativeRecipe& recipe) {
    if (!*reinterpret_cast<const uint8_t*>(worker + (maintenance ? 0xcc : 0xb9)) &&
        *reinterpret_cast<const int32_t*>(worker + (maintenance ? 0x90 : 0xa8)) > 0) {
        std::string label;
        if (!native_text(worker + 0x58, label, 1024)) return false;
        *reinterpret_cast<int32_t*>(worker + (maintenance ? 0x90 : 0xa8)) = 0;
        *reinterpret_cast<uint8_t*>(worker + (maintenance ? 0xcc : 0xb9)) = 1;
        alignas(8) uint8_t output[32]{};
        original_multiple(*reinterpret_cast<const uintptr_t*>(worker + 0x40), reinterpret_cast<uintptr_t>(output),
            recipe.definition.logical_mask, 1, 0, reinterpret_cast<uintptr_t>(label.c_str()));
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
static bool open_recipe_worker(uintptr_t worker, bool maintenance, const NativeRecipe& recipe, std::map<uintptr_t, std::string>::iterator& found) {
    const auto manager = *reinterpret_cast<const uintptr_t*>(worker + 0x40);
    std::string label, key;
    if (!manager || !native_text(worker + 0x58, label, 191) ||
        !open_native_request(manager, recipe, label, key)) return false;
    const auto& entry = recipe.definition;
    const auto quantity = static_cast<int32_t>(entry.physical_count);
    *reinterpret_cast<int32_t*>(worker + 0x90) = quantity;
    if (maintenance) {
        *reinterpret_cast<int32_t*>(worker + 0x94) = static_cast<int32_t>(
            recipe.refill_limit ? recipe.refill_limit : entry.maximum_emissions);
        if (!quantity) *reinterpret_cast<uint8_t*>(worker + 0xcc) = 1;
    } else {
        *reinterpret_cast<int32_t*>(worker + 0xa8) = quantity;
        if (!quantity) *reinterpret_cast<uint8_t*>(worker + 0xb9) = 1;
    }
    found = worker_recipes.emplace(worker, std::move(key)).first;
    remember_worker_diagnostic(worker);
    if (!quantity) recipe_state.complete_empty(found->second);
    return true;
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
        return finish_bridge_worker(worker, maintenance, recipe->second);
    }
    auto found = worker_recipes.find(worker);
    if (found == worker_recipes.end()) {
        if (!open_recipe_worker(worker, maintenance, recipe->second, found)) return false;
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
    try { ready = prepare_recipe_worker(worker, maintenance, marked); } catch (...) { set_recipe_fault(__func__, __LINE__, "worker_prepare_exception"); }
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
    } catch (...) { set_recipe_fault(__func__, __LINE__, "worker_prepare_exception"); }
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

static bool dispatch_first_recipe(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias, uintptr_t label, bool group, const std::string& origin, const std::string& key, uint64_t& result) {
    if (!recipe_state.requests().at(key).emitted) {
        // a native refusal rewinds this event; queuing it too would repeat the wave.
        if (!native_admission(*reinterpret_cast<const uintptr_t*>(world_global) + 0xafd98, 1)) {
            trace_spawn_refusal(manager, origin, "native_admission_refused");
            retry_immediate(manager, "native_admission_refused"); return false;
        }
        const auto family = native_recipes.at(origin).definition.family;
        if (family == 'a' || family == 'b' || family == 's')
            create_special_recipe(manager, origin, recipe_state.requests().at(key).request.label);
        else result = group ? original_multiple(manager, output, type, 1, alias, label) :
            original_single(manager, output, type, alias, label);
    }
    return true;
}
void dispatch_recipe_locked(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                              uintptr_t label, bool group, const std::string& origin, uintptr_t& result) {
    result = output;
    if (!load_native_recipes()) { if (!recipe_fault.load()) retry_immediate(manager, "recipe_library_unavailable"); return; }
    const auto found = native_recipes.find(origin);
    if (found == native_recipes.end()) { retry_immediate(manager, "recipe_missing"); return; }
    // preserve native group selection for the bridge's empty source.
    if (vanilla_bridge_group(found->second)) {
        result = original_multiple(manager, output, type, 1, 0, label);
        return;
    }
    auto& key = pending_recipes[{manager, origin}];
    if (key.empty()) {
        std::string label_text;
        if ((found->second.definition.family != 's' && !native_cstring(label, label_text, 1024)) ||
            !open_native_request(manager, found->second, label_text, key)) {
            retry_immediate(manager, "request_not_opened"); return;
        }
    }
    if (!found->second.definition.physical_count) {
        if ((found->second.definition.family == 'i' || found->second.definition.family == 'p') && !queue_recipe_delay(manager)) {
            retry_immediate(manager, "delay_worker_unavailable"); return;
        }
        recipe_state.complete_empty(key); pending_recipes.erase({manager, origin}); return;
    }
    if (!dispatch_first_recipe(manager, output, type, alias, label, group, origin, key, result)) return;
    const auto& birth_label = recipe_state.requests().at(key).request.label;
    if (!queue_recipe_remainder(manager, alias, birth_label.c_str(), key)) {
        retry_immediate(manager, "remainder_worker_unavailable"); return;
    }
    pending_recipes.erase({manager, origin});
    return;
}

void invoke_recipe_dispatch(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                             uintptr_t label, bool group, const std::string* origin, uintptr_t* result, std::unique_lock<std::recursive_mutex>& admission) {
    const auto previous_worker = worker_recipe;
    __try { worker_recipe = nullptr; dispatch_recipe_locked(manager, output, type, alias, label, group, *origin, *result); }
    __finally { worker_recipe = previous_worker; admission.unlock(); }
}

bool dispatch_recipe_immediate(uintptr_t manager, uintptr_t output, uint64_t type, uintptr_t alias,
                               uintptr_t label, bool group, uintptr_t& result) {
    std::string origin;
    if (stopped.load(std::memory_order_acquire) || !recipe_origin(alias, origin)) return false;
    std::unique_lock<std::recursive_mutex> admission(admission_mutex);
    invoke_recipe_dispatch(manager, output, type, alias, label, group, &origin, &result, admission);
    return true;
}

void maintain_tick_detour_entry(uintptr_t worker) {
    const auto lease = expanded_hooks.enter(16);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_maintain_tick)>(lease.original)(worker);
        return maintain_tick_detour(worker);
    } __finally { expanded_hooks.leave(lease); }
}

void stagger_tick_detour_entry(uintptr_t worker) {
    const auto lease = expanded_hooks.enter(17);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_stagger_tick)>(lease.original)(worker);
        return stagger_tick_detour(worker);
    } __finally { expanded_hooks.leave(lease); }
}

void queue_command_detour_entry(uintptr_t manager, uintptr_t command) {
    const auto lease = expanded_hooks.enter(18);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_queue_command)>(lease.original)(manager, command);
        return queue_command_detour(manager, command);
    } __finally { expanded_hooks.leave(lease); }
}

}

namespace sentinel::expanded {
using namespace detail;

std::string recipe_fault_diagnostic() {
    char snapshot[sizeof(first_recipe_fault)];
    AcquireSRWLockShared(&recipe_fault_lock);
    std::memcpy(snapshot, first_recipe_fault, sizeof(snapshot));
    ReleaseSRWLockShared(&recipe_fault_lock);
    return snapshot;
}

}
