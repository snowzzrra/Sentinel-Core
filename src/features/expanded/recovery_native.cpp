// Encounter recovery scans and execution.
#include "expanded_internal.h"

namespace sentinel::expanded::detail {
using RecoveryNotify = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint8_t);
using RecoveryDeath = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, int32_t, int32_t, uintptr_t, uintptr_t, float);
using RecoveryClock = uintptr_t(*)(uintptr_t, int64_t*, int32_t);
using RecoveryMilliseconds = int32_t*(*)(int32_t*, int64_t);
using RecoveryStopMaintaining = uintptr_t(*)(uintptr_t, uintptr_t, uint64_t, const char*);
extern RecoveryNotify recovery_notify;
extern RecoveryDeath recovery_death;
extern RecoveryClock recovery_clock;
extern RecoveryMilliseconds recovery_milliseconds;
extern PostOne recovery_post;
extern RecoveryStopMaintaining recovery_stop_maintaining;
extern uintptr_t recovery_image;
std::string recovery_type(uintptr_t object);
bool scan_recovery_impl(uintptr_t world, recovery::Context& context);
bool scan_recovery(uintptr_t world, recovery::Context& context) noexcept;
void stop_recovery_producers(recovery::Attempt& attempt, const recovery::Manager& target);
bool kill_recovery_actor(recovery::Attempt& attempt, const recovery::Context& context, const recovery::Actor& actor);
bool drain_recovery_impl(recovery::Attempt& attempt, const recovery::Context& context);
bool drain_recovery_safe(recovery::Attempt& attempt, const recovery::Context& context) noexcept;
}

namespace sentinel::expanded::detail {

std::map<uintptr_t, std::string> recovery_rejections;

RecoveryNotify recovery_notify = nullptr;

RecoveryDeath recovery_death = nullptr;

RecoveryClock recovery_clock = nullptr;

RecoveryMilliseconds recovery_milliseconds = nullptr;

PostOne recovery_post = nullptr;

RecoveryStopMaintaining recovery_stop_maintaining = nullptr;

uintptr_t recovery_image = 0;

void prepare_recovery(const engine::Binding& binding, HANDLE stop_event) {
    recovery_death = nullptr;
    struct Site { uint32_t rva; const char* bytes; size_t leaf; };
    const Site sites[]{
        {0x11f8700, "40555356574155488d6c24c04881ec40010000488b05be62fb024833c4488945", 0},
        {0x120e730, "4883ec58f30f108424a0000000488b842498000000f30f11442448c644244000", 0},
        {0x682380, "488b05f938f7034c8bc983780800750f488b4110488b4808488bc248890ac380", 80},
        {0x17fae60, "488b05d9643f040f57c0f3480f2ac2f30f5905e1bb2501f30f594028f30f2cc0", 38},
        {0x17dd390, "44894c24205356574883ec40488b01418bd8488bfa488bf1ff504084c00f84a6", 0},
        {0xba3070, "40535556574883ec68488b0558b960034833c44889442450488bea488bf9498b", 0},
        {0x143c000, "4883ec28486381a088000085c07e5548895c2420488bd0488b999888000048c1", 0},
    };
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    for (const auto& site : sites) {
        native::Target target{}; target.address = binding.image.base + site.rva;
        for (size_t i = 0; i < target.bytes.size(); ++i) {
            unsigned byte = 0;
            if (sscanf_s(site.bytes + 2 * i, "%2x", &byte) != 1) return;
            target.bytes[i] = static_cast<uint8_t>(byte);
        }
        if (site.leaf ? native::validate_leaf_target(memory, binding.image, target, site.leaf, stop_event, deadline) :
            native::validate_target(memory, binding.image, target, stop_event, deadline)) return;
    }
    recovery_image = binding.image.base;
    recovery_notify = reinterpret_cast<RecoveryNotify>(recovery_image + sites[0].rva);
    recovery_death = reinterpret_cast<RecoveryDeath>(recovery_image + sites[1].rva);
    recovery_clock = reinterpret_cast<RecoveryClock>(recovery_image + sites[2].rva);
    recovery_milliseconds = reinterpret_cast<RecoveryMilliseconds>(recovery_image + sites[3].rva);
    recovery_post = reinterpret_cast<PostOne>(recovery_image + sites[4].rva);
    recovery_stop_maintaining = reinterpret_cast<RecoveryStopMaintaining>(recovery_image + sites[5].rva);
}

std::string recovery_type(uintptr_t object) {
    const auto table = *reinterpret_cast<const uintptr_t*>(object);
    const auto locator = *reinterpret_cast<const uintptr_t*>(table - 8);
    const auto descriptor = *reinterpret_cast<const uint32_t*>(locator + 12);
    const auto name = reinterpret_cast<const char*>(recovery_image + descriptor + 16);
    size_t length = 0;
    while (length < 192 && name[length]) ++length;
    if (length == 192) return "unknown";
    return std::string(name, length);
}

bool recovery_event_wait(uintptr_t worker, uintptr_t manager) {
    return recovery_type(worker) == ".?AVidEncounterEvent_WaitForEventFlag@@" &&
        *reinterpret_cast<const uintptr_t*>(worker + 0x40) == manager &&
        *reinterpret_cast<const int32_t*>(worker + 0xa8) != 3;
}

void scan_recovery_wait(uintptr_t worker, recovery::Manager& manager) {
    if (!worker || cast_maintain(worker) || cast_stagger(worker)) return;
    if (!manager.wait.empty()) manager.wait += ", ";
    const auto type = recovery_type(worker);
    manager.wait += type;
    const auto observed = remaining_wait_started.find(worker);
    if (observed != remaining_wait_started.end()) return;
    if (type == ".?AVidEncounterEvent_WaitForEventFlag@@") {
        const auto flag = *reinterpret_cast<const int32_t*>(worker + 0xa8);
        manager.wait += " flag=" + std::to_string(flag);
        if (recovery_event_wait(worker, manager.address)) {
            manager.event_blocked |= !*reinterpret_cast<const uint8_t*>(worker + 0xac);
            return;
        }
        manager.exclusion = flag == 3 ? "commit_wait_requires_producers" : "wait_owner_mismatch";
    } else manager.exclusion = "unsupported_wait_type:" + type;
    manager.noncombat_wait = true;
}

const char* recovery_request_rejection(const RecipeProgress& progress, uint64_t mask, const std::string& label) {
    if (progress.closed) return "request_closed";
    if (progress.request.maximum_emissions <= progress.emitted) return "request_exhausted";
    if (!progress.request.physical_units) return "request_has_no_physical_units";
    if (!(progress.request.logical_mask & mask)) return "request_mask_mismatch";
    if (!label.empty() && progress.request.label != label) return "request_label_mismatch";
    if (!native_recipes.count(progress.request.origin)) return "request_recipe_missing";
    return nullptr;
}

const char* recovery_combat_wait(uintptr_t worker, const recovery::Manager& manager) {
    const auto wait = remaining_wait_started.find(worker);
    if (wait == remaining_wait_started.end()) return "combat_wait_unobserved";
    if (wait->second.table != *reinterpret_cast<const uintptr_t*>(worker)) return "combat_wait_identity_changed";
    if (*reinterpret_cast<const uintptr_t*>(worker + 0x40) != manager.address) return "combat_wait_owner_mismatch";
    auto mask = *reinterpret_cast<const uint64_t*>(worker + 0x88);
    if (mask & (uint64_t{1} << 44)) mask = encounter_ai_mask;
    std::string label;
    if (!native_text(worker + 0x58, label, 1024)) return "combat_wait_label_unreadable";
    const char* reason = "request_owner_mismatch";
    for (const auto& [key, progress] : recipe_state.requests()) {
        (void)key;
        if (progress.request.owner != manager.name) continue;
        reason = recovery_request_rejection(progress, mask, label);
        if (!reason) return nullptr;
    }
    return reason;
}

const char* recovery_empty_request(const recovery::Context& context, recovery::Manager& manager, uintptr_t workers, int32_t count) {
    if (recipe_stamp != native::observation_stamp()) return "recipe_generation_mismatch";
    if (recipe_state.map() != context.map) return "recipe_map_mismatch";
    if (recipe_state.plan() != recipe_library.plan) return "recipe_plan_mismatch";
    if (recipe_state.map() != recipe_library.map) return "recipe_library_map_mismatch";
    const char* reason = "combat_wait_unobserved";
    for (int32_t j = 0; j < count; ++j) {
        const auto worker = *reinterpret_cast<const uintptr_t*>(workers + static_cast<uintptr_t>(j) * 8);
        if (!worker) continue;
        reason = recovery_combat_wait(worker, manager);
        if (!reason) { manager.recipe_blocked = true; break; }
    }
    return reason;
}

static bool scan_recovery_actors(uintptr_t world, uintptr_t address, recovery::Manager& manager) {
    std::set<uintptr_t> seen;
    for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
        const auto records = *reinterpret_cast<const uintptr_t*>(address + offset);
        const auto size = *reinterpret_cast<const int32_t*>(address + offset + 8);
        if (size < 0 || size > 1000000) return false;
        for (int32_t j = 0; j < size; ++j) {
            const auto actor = record_actor(records + static_cast<uintptr_t>(j) * 0x68);
            if (!actor || !seen.insert(actor).second || combat::confirmed_dead(world, actor)) continue;
            char actor_name[512];
            if (!read_instance_name(actor, actor_name)) return false;
            manager.actors.push_back({actor, actor_identifier(actor), recovery::protected_actor(actor_name, recovery_type(actor))});
        }
    }
    return true;
}
static void trace_recovery_rejection(const recovery::Context& context, const recovery::Manager& manager, uintptr_t address) {
    const auto rejection = recovery::rejection(context.map, manager);
    if (rejection && (recovery_rejections.count(address) || recovery_rejections.size() < 4096) &&
        recovery_rejections[address] != rejection) {
        recovery_rejections[address] = rejection;
        commands::diagnostic_trace("AP_ENCOUNTER_RECOVERY rejected=%.192s manager=%llx reason=%s active=%u enabled=%u cursor=%d live_ai=%zu wait=%.192s",
            manager.name.c_str(), address, rejection, *reinterpret_cast<const uint8_t*>(address + 0xdb2),
            *reinterpret_cast<const uint8_t*>(address + 0xba0), *reinterpret_cast<const int32_t*>(address + 0xda0),
            manager.actors.size(), manager.wait.c_str());
    } else if (!rejection) recovery_rejections.erase(address);
}
static bool scan_recovery_workers(uintptr_t address, recovery::Manager& manager, uintptr_t& workers, int32_t& length) {
    workers = *reinterpret_cast<const uintptr_t*>(address + 0xcc0);
    length = *reinterpret_cast<const int32_t*>(address + 0xcc8);
    if (length < 0 || length > 100000) return false;
    for (int32_t j = 0; j < length; ++j) {
        const auto worker = *reinterpret_cast<const uintptr_t*>(workers + static_cast<uintptr_t>(j) * 8);
        scan_recovery_wait(worker, manager);
    }
    return true;
}
static bool recovery_needs_empty_request(const recovery::Manager& manager) {
    return manager.actors.empty() && manager.active && !manager.complete && !manager.noncombat_wait && !manager.event_blocked;
}
bool scan_recovery_impl(uintptr_t world, recovery::Context& context) {
    const auto managers = *reinterpret_cast<const uintptr_t*>(world + 0x126658);
    const auto count = *reinterpret_cast<const int32_t*>(world + 0x126660);
    if (count < 0 || count > 100000) return false;
    for (int32_t i = 0; i < count; ++i) {
        const auto address = *reinterpret_cast<const uintptr_t*>(managers + static_cast<uintptr_t>(i) * 0x20 + 8);
        if (!address) continue;
        recovery::Manager manager;
        manager.address = address; manager.identifier = actor_identifier(address);
        char name[512];
        if (!read_definition_name(address, name)) return false;
        manager.name = name; manager.kind = recovery_type(address);
        manager.protected_fight = recovery::protected_manager(context.map, manager.name);
        manager.complete = *reinterpret_cast<const uint8_t*>(address + 0xdb3) != 0;
        manager.active = *reinterpret_cast<const uint8_t*>(address + 0xdb2) &&
            *reinterpret_cast<const uint8_t*>(address + 0xba0) && *reinterpret_cast<const int32_t*>(address + 0xda0) >= 0;
        uintptr_t workers = 0; int32_t length = 0;
        if (!scan_recovery_workers(address, manager, workers, length)) return false;
        if (!scan_recovery_actors(world, address, manager)) return false;
        if (recovery_needs_empty_request(manager)) {
            const auto reason = recovery_empty_request(context, manager, workers, length);
            if (reason) manager.exclusion = reason;
        }
        trace_recovery_rejection(context, manager, address);
        context.managers.push_back(std::move(manager));
    }
    return true;
}

bool scan_recovery(uintptr_t world, recovery::Context& context) noexcept {
    __try { return scan_recovery_impl(world, context); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void stop_recovery_workers(recovery::Attempt& attempt, const recovery::Manager& target) {
    const auto count = *reinterpret_cast<const int32_t*>(target.address + 0xcc8);
    const auto workers = *reinterpret_cast<const uintptr_t*>(target.address + 0xcc0);
    if (count < 0 || count > 100000) throw std::runtime_error("worker list unreadable");
    for (int32_t i = 0; i < count; ++i) {
        const auto command = *reinterpret_cast<const uintptr_t*>(workers + static_cast<uintptr_t>(i) * 8);
        const auto maintain = cast_maintain(command);
        const auto worker = maintain ? maintain : cast_stagger(command);
        if (!worker) continue;
        auto& complete = *reinterpret_cast<uint8_t*>(worker + (maintain ? 0xcc : 0xb9));
        if (!complete) ++attempt.producers_stopped;
        if (!maintain) { *reinterpret_cast<int32_t*>(worker + 0xa8) = 0; complete = 1; }
        const auto found = worker_recipes.find(command);
        if (found != worker_recipes.end()) { recipe_state.cancel(found->second); worker_recipes.erase(found); }
        worker_diagnostics.erase(command);
    }
}
static std::set<std::string> close_recovery_requests(recovery::Attempt& attempt, const recovery::Manager& target) {
    std::set<std::string> closed;
    for (const auto& [key, progress] : recipe_state.requests()) {
        if (target.actors.empty() && progress.request.owner != target.name) continue;
        const auto members = recipe_state.request_memberships(key);
        const bool owned = progress.request.owner == target.name ||
            std::any_of(members.begin(), members.end(), [&](const auto& member) { return member.owner == target.name; }) ||
            std::any_of(target.actors.begin(), target.actors.end(), [&](const auto& actor) {
                char name[512]; return read_definition_name(actor.address, name) && progress.request.owner == name;
            });
        if (!owned) continue;
        if (!progress.closed) { recipe_state.cancel(key); ++attempt.producers_stopped; }
        closed.insert(key);
    }
    return closed;
}
static void close_producer_requests(ComponentSummons& pending, const std::set<std::string>& closed) {
    for (auto& request : pending.requests) if (closed.count(request.key)) request.completed = true;
    if (!pending.requests.empty() && std::all_of(pending.requests.begin(), pending.requests.end(),
            [](const auto& request) { return request.completed; })) pending.completed = true;
}
static void close_recovery_summons(const std::set<std::string>& closed) {
    for (auto* producers : {&component_summons, &direct_points}) for (auto& [component, pending] : *producers) {
        (void)component;
        close_producer_requests(pending, closed);
    }
    if (pending_summon_checkpoint) for (auto& component : pending_summon_checkpoint->summons)
        for (auto& request : component.requests) if (closed.count(request.key)) request.completed = true;
}
static void cancel_recovery_shows(const std::set<std::string>& closed) {
    for (auto pending = pending_independent_shows.begin(); pending != pending_independent_shows.end();) {
        const bool cancelled = std::any_of(closed.begin(), closed.end(), [&](const auto& key) {
            const auto& request = recipe_state.requests().at(key).request;
            const auto recipe = native_recipes.find(request.origin);
            if (key.rfind("independent/", 0) != 0 || recipe == native_recipes.end()) return false;
            for (uint64_t i = 0; i < recipe->second.bodies.size(); ++i)
                if (independent_body_name(recipe->second, i) == pending->first) return true;
            return false;
        });
        if (cancelled) { independent_shows.insert(pending->first); pending = pending_independent_shows.erase(pending); }
        else ++pending;
    }
}

void stop_recovery_producers(recovery::Attempt& attempt, const recovery::Manager& target) {
    stop_recovery_workers(attempt, target);
    alignas(8) uint8_t output[32]{};
    recovery_stop_maintaining(target.address, reinterpret_cast<uintptr_t>(output), uint64_t{1} << 44, "");
    if (native::observation_stamp() != attempt.stamp) { attempt.outcome = recovery::changed; return; }
    for (auto pending = pending_recipes.begin(); pending != pending_recipes.end();) {
        if (pending->first.first != target.address) { ++pending; continue; }
        recipe_state.cancel(pending->second); ++attempt.producers_stopped;
        pending = pending_recipes.erase(pending);
    }
    const auto closed = close_recovery_requests(attempt, target);
    close_recovery_summons(closed);
    cancel_recovery_shows(closed);
}

bool kill_recovery_actor(recovery::Attempt& attempt, const recovery::Context& context, const recovery::Actor& actor) {
    if (actor_identifier(actor.address) != actor.identifier || combat::confirmed_dead(context.world, actor.address)) return true;
    if (resolve_actor(actor.address + 0x167d0)) { attempt.wait = "actor has a native owner"; return true; }
    const auto challenge = *reinterpret_cast<const uintptr_t*>(context.world + 0x65b48);
    if (!challenge || *reinterpret_cast<const uint32_t*>(context.world + 0x65b40) !=
        *reinterpret_cast<const uint32_t*>(context.world + 0x65b44)) return false;
    const auto data = *reinterpret_cast<const uintptr_t*>(challenge + 0x35f8);
    const auto declaration = data ? *reinterpret_cast<const uintptr_t*>(data + 0xc0) : 0;
    const auto dispatcher = *reinterpret_cast<const uintptr_t*>(context.world + 0x126648);
    if (!declaration || !dispatcher || !combat::suppress(context.scope.lifecycle_generation, context.world, actor.address)) return false;
    ++attempt.kills_suppressed;
    recovery_notify(dispatcher, context.player, actor.address, context.player, declaration + 0x90, declaration + 0x90, 0);
    if (native::observation_stamp() != context.stamp) { attempt.outcome = recovery::changed; return false; }
    recovery_death(context.world + 0xac660, context.player, context.player, actor.address, -1, 5,
        *reinterpret_cast<const uintptr_t*>(declaration + 0xad8), declaration + 0x9c8, 0.0f);
    if (native::observation_stamp() != context.stamp) { attempt.outcome = recovery::changed; return false; }
    int64_t step = 0; int32_t delay = 0;
    recovery_clock(context.world + 0x125a60, &step, 1);
    recovery_milliseconds(&delay, static_cast<int64_t>(static_cast<float>(step) * 2.0f));
    if (!recovery_post(actor.address, recovery_image + 0x6bebf80, delay, 0)) return false;
    if (!combat::confirmed_dead(context.world, actor.address)) return false;
    ++attempt.actors_killed;
    return true;
}

bool unblock_recovery_waits(recovery::Attempt& attempt, const recovery::Manager& target) {
    if (!target.event_blocked || !target.actors.empty()) return true;
    const auto count = *reinterpret_cast<const int32_t*>(target.address + 0xcc8);
    const auto workers = *reinterpret_cast<const uintptr_t*>(target.address + 0xcc0);
    if (count < 0 || count > 100000) return false;
    for (int32_t i = 0; i < count; ++i) {
        const auto worker = *reinterpret_cast<const uintptr_t*>(workers + static_cast<uintptr_t>(i) * 8);
        if (!worker || !recovery_event_wait(worker, target.address)) continue;
        if (*reinterpret_cast<const uint8_t*>(worker + 0xac)) continue;
        // the native predicate consumes this local latch without broadcasting the flag.
        *reinterpret_cast<uint8_t*>(worker + 0xac) = 1;
        commands::diagnostic_trace("AP_ENCOUNTER_RECOVERY unblocked=%llx manager=%llx wait=external_flag stamp=%llu",
            worker, target.address, attempt.stamp);
    }
    return true;
}

static bool kill_recovery_targets(recovery::Attempt& attempt, const recovery::Context& context, const std::vector<const recovery::Manager*>& targets) {
    std::set<uintptr_t> killed;
    for (const auto* target : targets) {
        if (target->complete) continue;
        for (const auto& actor : target->actors) {
            if (native::observation_stamp() != context.stamp) { attempt.outcome = recovery::changed; return false; }
            if (killed.insert(actor.address).second && !kill_recovery_actor(attempt, context, actor)) return false;
        }
    }
    return true;
}
bool drain_recovery_impl(recovery::Attempt& attempt, const recovery::Context& context) {
    std::vector<const recovery::Manager*> targets;
    // close every target before deaths can trigger a checkpoint.
    for (const auto& target : attempt.targets) {
        const auto found = std::find_if(context.managers.begin(), context.managers.end(), [&](const auto& manager) {
            return manager.address == target.address && manager.identifier == target.identifier;
        });
        if (found == context.managers.end()) return false;
        if (native::observation_stamp() != context.stamp) { attempt.outcome = recovery::changed; return false; }
        if (found->complete) continue;
        if (!unblock_recovery_waits(attempt, *found)) return false;
        stop_recovery_producers(attempt, *found);
        targets.push_back(&*found);
    }
    return kill_recovery_targets(attempt, context, targets);
    return true;
}

bool drain_recovery_safe(recovery::Attempt& attempt, const recovery::Context& context) noexcept {
    __try { return drain_recovery_impl(attempt, context); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}

namespace sentinel::expanded {
using namespace detail;

recovery::Context recovery_context(uintptr_t world, const char* map) {
    recovery::Context context;
    context.map = map;
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    context.supported = !stopped.load() && recovery_death && resolve_actor && definition_name && cast_maintain && cast_stagger;
    context.readable = context.supported && scan_recovery(world, context);
    return context;
}

bool drain_recovery(recovery::Attempt& attempt, const recovery::Context& context) {
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    try { return !stopped.load() && recovery_death && drain_recovery_safe(attempt, context); }
    catch (...) { return false; }
}

bool recovery_player_allowed(uintptr_t player) {
    __try { return player && *reinterpret_cast<const int32_t*>(player + 0x88a0) == 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}

#ifdef SC_NATIVE_TESTING

#include "../../../tests/expanded/recovery.inc"

#endif
