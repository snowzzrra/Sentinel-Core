// Recipe checkpoint capture, restoration and memberships.
#include "expanded_internal.h"

namespace sentinel::expanded::detail {
bool append_recipe_membership(std::map<std::string, std::vector<RecipeMembership>>& memberships,
                               const std::string& actor, const char* owner, uintptr_t label);
bool synchronize_independent_memberships(uintptr_t world);
}

namespace sentinel::expanded::detail {

std::map<std::string, RecipeNativeCheckpoint::Identity> restored_actor_identities;

bool recipe_checkpoint_root(uintptr_t& root) noexcept {
    if ([] { return save::session().campaign_run.snapshot().native_subtype==6; }()) { root=0; return true; }
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
    __except(EXCEPTION_EXECUTE_HANDLER) { set_recipe_fault(__func__, __LINE__, "native_exception"); return false; }
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
                        std::set<uintptr_t>& workers, std::set<uintptr_t>& owners, uint64_t* living) {
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

}
