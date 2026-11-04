// Encounter credits and completion predicates.
#include "expanded_internal.h"

namespace sentinel::expanded::detail {
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
struct DeathActionContext { std::string request; RecipeCredit credit; };
extern thread_local const DeathActionContext* death_action_context;
bool completed_independent_actor(uintptr_t actor, const SpawnRecipe& frozen, std::string* request = nullptr);
RemainingWeight remaining_weight(uintptr_t actor);
uintptr_t post_death_target(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument);
uintptr_t post_one_detour_impl(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument, void* hook_caller);
void invoke_death_actions(uintptr_t action, uintptr_t context, uintptr_t state, uintptr_t frame,
                          const DeathActionContext* projection, std::unique_lock<std::recursive_mutex>& admission);
void record_recipe_death(uintptr_t actor);
bool cache_remaining(uintptr_t command, uintptr_t record);
bool try_cache_remaining(uintptr_t command, uintptr_t record);
uintptr_t remaining_manager(uintptr_t command);
struct SynchronousRemainder {
    const NativeRecipe* recipe; const RecipeProgress* progress; uint64_t emitted, body_offset = 0;
    const RecipeNativeCheckpoint::Child* recovering = nullptr;
};
bool scope_matches(const std::vector<RecipeMembership>& members, std::string_view owner, std::string_view label);
bool pending_direct_matches(const SummonRequest& pending, std::string_view owner, uint64_t mask, std::string_view label);
bool pending_autonomous_emissions();
std::optional<bool> pending_summon_matches(const ComponentSummons& component, const SummonRequest& pending,
                                          std::string_view owner, uint64_t mask, std::string_view label);
uint64_t consumer_emitted(uintptr_t command, const std::string& key, const RecipeProgress& progress);
bool synchronous_remainders(uintptr_t command, std::vector<SynchronousRemainder>& result);
bool remaining_remainders(uintptr_t command, std::vector<RecipeCredit>& total, std::vector<RecipeCredit>& living, bool& tracked);
bool health_remainders(uintptr_t command, std::vector<RecipeHealth>& health);
bool remaining_inputs(uintptr_t command, std::vector<RecipeCredit>& total, std::vector<RecipeCredit>& living,
                      std::vector<RemainingItem>& present, bool& tracked, bool& blocked, int32_t& threshold);
bool empty_request_pending(uintptr_t manager, uint64_t mask, uintptr_t label);
bool remaining_encounter_heavy(uintptr_t command);
bool remaining_encounter_heavy_safe(uintptr_t command) noexcept;
bool optional_zombie_completion(uintptr_t command);
bool optional_zombie_completion_safe(uintptr_t command) noexcept;
void trace_remaining_actors(uintptr_t command, const char* owner, int32_t count) noexcept;
void trace_quantity_wait(uintptr_t command, char kind, uint64_t result);
bool recipe_scope(std::string_view owner, uint64_t mask, std::string_view label, bool empty_only);
bool future_matches(uintptr_t worker, uint64_t mask, uintptr_t label);
bool native_future(uintptr_t manager, uint64_t mask = uint64_t{1} << 44, uintptr_t label = 0);
uint64_t kill_result(uintptr_t command);
RemainingWeight health_weight(uintptr_t command, uintptr_t record);
float actor_health(uintptr_t actor, uintptr_t slot);
uint64_t health_result(uintptr_t command);
}

namespace sentinel::expanded::detail {

DeathActions original_death_actions = nullptr;

PostOne original_post_one = nullptr;

std::array<uintptr_t, 3> death_target_returns{};

uintptr_t activate_event = 0;

thread_local const DeathActionContext* death_action_context = nullptr;

std::map<std::pair<std::string, std::string>, DeathActionCredit> death_action_credits;

std::map<uintptr_t, PointCredits> point_credits;

std::map<std::tuple<uintptr_t, char, uint64_t, uint32_t>, uint8_t> quantity_wait_traces;

std::map<uintptr_t, RemainingWait> remaining_wait_started;

RemainingPredicate original_remaining = nullptr;

RemainingEvent original_remaining_added = nullptr, original_remaining_dead = nullptr;

RefreshEntity refresh_entity = nullptr, refresh_manager = nullptr;

IgnoredActor ignored_actor = nullptr;

std::map<uintptr_t, std::vector<RemainingItem>> remaining_weights;

KillConstruct original_kill_construct = nullptr;

RemainingPredicate original_kill = nullptr;

RemainingEvent original_kill_dead = nullptr;

WorkerCast cast_maintain = nullptr, cast_stagger = nullptr;

CompareText compare_text = nullptr;

std::map<uintptr_t, KillProgress> kill_progress;

RemainingPredicate original_health = nullptr;

RemainingEvent original_health_added = nullptr;

HealthDamage original_health_damage = nullptr;

std::map<uintptr_t, HealthProgress> health_progress;

std::recursive_mutex quantity_mutex;

uintptr_t record_actor(uintptr_t record) {
    const auto identifier = *reinterpret_cast<const uint32_t*>(record + 8);
    const auto cached = *reinterpret_cast<const uint32_t*>(record + 12);
    if (identifier != cached || (identifier != 0x1fffffe &&
        !*reinterpret_cast<const uintptr_t*>(record + 16)))
        refresh_entity(record + 8, identifier == cached ? record + 8 : record + 12);
    return *reinterpret_cast<const uintptr_t*>(record + 16);
}

bool completed_independent_actor(uintptr_t actor, const SpawnRecipe& frozen, std::string* request) {
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
            set_recipe_fault(__func__, "death_target_identity_missing", context.request.c_str(), target); return 0;
        }
        auto& credit = death_action_credits[{context.request, identity}];
        if ((credit.denominator && credit.denominator != context.credit.denominator) ||
            !context.credit.denominator || credit.units > UINT64_MAX - context.credit.numerator) {
            set_recipe_fault(__func__, "death_credit_invalid", context.request.c_str(), target); return 0;
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
    } catch (...) { set_recipe_fault(__func__, "exception"); }
    return 0;
}

uintptr_t post_one_detour_impl(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument, void* hook_caller) {
    if (stopped.load(std::memory_order_acquire)) return original_post_one(target, event, delay, argument);
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
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
                set_recipe_fault(__func__, "death_request_missing", nullptr, actor);
            }
            record_recipe_death(actor);
        }
    } catch (...) { set_recipe_fault(__func__, "exception"); }
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
    catch (...) { set_recipe_fault(__func__, "exception"); return false; }
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
            const auto started = remaining_wait_started.emplace(command,
                RemainingWait{GetTickCount64(), *reinterpret_cast<const uintptr_t*>(command)}).first->second.started;
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
    catch (...) { set_recipe_fault(__func__, "exception"); return 0; }
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

bool native_future(uintptr_t manager, uint64_t mask, uintptr_t label) {
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
    } catch (...) { set_recipe_fault(__func__, "exception"); return 0; }
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
    catch (...) { set_recipe_fault(__func__, "exception"); return 0; }
}

uint64_t __fastcall remaining_detour_entry(uintptr_t command) {
    const auto lease = expanded_hooks.enter(13);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_remaining)>(lease.original)(command);
        return remaining_detour(command);
    } __finally { expanded_hooks.leave(lease); }
}

void __fastcall remaining_added_detour_entry(uintptr_t command, uintptr_t record) {
    const auto lease = expanded_hooks.enter(14);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_remaining_added)>(lease.original)(command, record);
        return remaining_added_detour(command, record);
    } __finally { expanded_hooks.leave(lease); }
}

void __fastcall remaining_dead_detour_entry(uintptr_t command, uintptr_t record) {
    const auto lease = expanded_hooks.enter(15);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_remaining_dead)>(lease.original)(command, record);
        return remaining_dead_detour(command, record);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t kill_construct_detour_entry(uintptr_t command, int32_t threshold, uintptr_t label, uint64_t mask, uint64_t clock) {
    const auto lease = expanded_hooks.enter(19);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_kill_construct)>(lease.original)(command, threshold, label, mask, clock);
        return kill_construct_detour(command, threshold, label, mask, clock);
    } __finally { expanded_hooks.leave(lease); }
}

uint64_t kill_detour_entry(uintptr_t command) {
    const auto lease = expanded_hooks.enter(20);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_kill)>(lease.original)(command);
        return kill_detour(command);
    } __finally { expanded_hooks.leave(lease); }
}

void kill_dead_detour_entry(uintptr_t command, uintptr_t record) {
    const auto lease = expanded_hooks.enter(21);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_kill_dead)>(lease.original)(command, record);
        return kill_dead_detour(command, record);
    } __finally { expanded_hooks.leave(lease); }
}

uint64_t health_detour_entry(uintptr_t command) {
    const auto lease = expanded_hooks.enter(22);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_health)>(lease.original)(command);
        return health_detour(command);
    } __finally { expanded_hooks.leave(lease); }
}

void health_added_detour_entry(uintptr_t command, uintptr_t record) {
    const auto lease = expanded_hooks.enter(23);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_health_added)>(lease.original)(command, record);
        return health_added_detour(command, record);
    } __finally { expanded_hooks.leave(lease); }
}

void health_damage_detour_entry(uintptr_t command, uintptr_t record, float damage) {
    const auto lease = expanded_hooks.enter(24);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_health_damage)>(lease.original)(command, record, damage);
        return health_damage_detour(command, record, damage);
    } __finally { expanded_hooks.leave(lease); }
}

void death_actions_detour_entry(uintptr_t action, uintptr_t context, uintptr_t state, uintptr_t frame) {
    const auto lease = expanded_hooks.enter(36);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_death_actions)>(lease.original)(action, context, state, frame);
        return death_actions_detour(action, context, state, frame);
    } __finally { expanded_hooks.leave(lease); }
}

uintptr_t post_one_detour_entry(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument) {
    const auto caller = _ReturnAddress();
    const auto lease = expanded_hooks.enter(37);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_post_one)>(lease.original)(target, event, delay, argument);
        return post_one_detour_impl(target, event, delay, argument, caller);
    } __finally { expanded_hooks.leave(lease); }
}

}

#ifdef SC_NATIVE_TESTING

#include "../../../tests/expanded/encounters.inc"

#endif
