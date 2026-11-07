#include "encounter_recovery.h"
#include "player_strings_generated.h"
#include <algorithm>

namespace sentinel::recovery {
const char* message(Outcome outcome) {
    switch (outcome) {
    case pending: return player_strings::menu_recovery_pending;
    case finished: return player_strings::menu_recovery_finished;
    case no_active: return player_strings::menu_recovery_no_active;
    case unsupported: return player_strings::menu_recovery_unsupported;
    case changed: return player_strings::menu_recovery_changed;
    default: return player_strings::menu_recovery_unknown;
    }
}
bool boss_map(const std::string& map) {
    const auto separator = map.find_last_of('/');
    const auto key = map.substr(separator == std::string::npos ? 0 : separator + 1);
    for (const auto* boss : {"e1m4_boss", "e2m4_boss", "e3m4_boss", "e5m4_boss"})
        if (key == boss) return true;
    return false;
}
Queue& queue() { static Queue value; return value; }
const char* rejection(const std::string& map, const Manager& manager) {
    if (manager.complete) return "complete";
    if (!manager.active) return "inactive_flags";
    if (boss_map(map) || manager.protected_fight) return "boss";
    for (const auto& actor : manager.actors) if (actor.protected_actor) return "boss_actor";
    if (!manager.actors.empty()) return nullptr;
    if (manager.noncombat_wait) return manager.exclusion.empty() ? "unsupported_wait" : manager.exclusion.c_str();
    if (manager.event_blocked || manager.recipe_blocked) return nullptr;
    return manager.exclusion.empty() ? "no_live_ai_or_owned_combat_request" : manager.exclusion.c_str();
}

void select_targets(Attempt& attempt, const Context& context) {
    const auto empty = std::count_if(context.managers.begin(), context.managers.end(), [&](const auto& manager) {
        return manager.actors.empty() && !rejection(context.map, manager);
    });
    for (auto& manager : attempt.context.managers) {
        if (const auto reason = rejection(context.map, manager)) {
            manager.exclusion = reason;
            if (manager.active && !manager.complete && (manager.protected_fight || boss_map(context.map) ||
                manager.exclusion == "boss_actor")) attempt.outcome = unsupported;
            continue;
        }
        if (empty > 1) { manager.exclusion = "ambiguous_empty_encounters"; continue; }
        if (empty && !manager.actors.empty()) { manager.exclusion = "outside_empty_encounter"; continue; }
        manager.exclusion.clear();
        attempt.targets.push_back(manager);
    }
}
bool protected_manager(const std::string& map, const std::string& name) {
    if (map.find("/e4m2_swamp/") != std::string::npos && name == "boss_encounter_manager_1") return true;
    if (map.find("/e4m3_mcity/") != std::string::npos && name == "platformarea_samuel_encounter_manager_dlc1_1") return true;
    return map.find("/e3m3_maykr/") != std::string::npos && name == "stage2_encounter_manager";
}
bool protected_actor(const std::string& name, const std::string& kind) {
    for (const auto* actor : {"boss_intro_ai_superheavy_doomhunter_stage1_1", "confront_samuel_ai",
        "beast_of_the_arena_ai_boss_gladiator_1", "game_boss_ai_boss_gladiator_1", "khan_boss_intro_ida2",
        "intro_icon_stage_1_idai2", "platformarea_samuel_ai_boss_samuel_cin",
        "stage_1_ai_boss_darklord_reveals_ai", "stage_1_ai_boss_facing_ai"})
        if (name == actor) return true;
    return kind.find("Boss") != std::string::npos || kind.find("boss") != std::string::npos;
}
void Queue::observe(Context context, uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    current_ = std::move(context);
    sampled_at_ = now;
}
void Queue::enqueue_console(uint64_t stamp, uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (pending_.load(std::memory_order_relaxed)) { repeat_ = true; return; }
    if (attempts_.size() == 16) attempts_.pop_front();
    Attempt attempt;
    attempt.id = ++next_id_; attempt.stamp = stamp; attempt.admitted_at = now;
    attempts_.push_back(std::move(attempt));
    pending_.store(true, std::memory_order_release);
}
static bool same_context(const Context& context, const Context& previous) {
    return context.scope.lifecycle_generation == previous.scope.lifecycle_generation && context.map == previous.map &&
        context.world == previous.world && context.player == previous.player && context.namespace_id == previous.namespace_id &&
        context.save == previous.save && context.gameplay;
}
static Outcome recovery_context(const Attempt& attempt, const Context& context, uint64_t stamp, uint64_t age) {
    if (stamp != attempt.stamp || (attempt.started && !same_context(context, attempt.context))) return changed;
    if (!context.gameplay) return no_active;
    if (!context.supported) return unsupported;
    if (age > 250 || !context.readable) return unknown;
    return pending;
}
static void recovery_wait(Attempt& attempt, const Manager& manager) {
    auto& outcome = attempt.outcome;
    if (manager.protected_fight) outcome = unsupported;
    if (manager.noncombat_wait && manager.actors.empty() && attempt.elapsed >= 2000) outcome = unknown;
    for (const auto& actor : manager.actors) if (actor.protected_actor) outcome = unsupported;
    if (!manager.wait.empty()) {
        if (!attempt.wait.empty()) attempt.wait += "; ";
        attempt.wait += manager.name + ": " + manager.wait;
    }
}
static bool recovery_targets(Attempt& attempt, const Context& context) {
    auto& outcome = attempt.outcome;
    bool complete = !attempt.targets.empty();
    attempt.wait.clear();
    if (!attempt.targets.empty() && boss_map(context.map)) outcome = unsupported;
    for (const auto& target : attempt.targets) {
        const auto found = std::find_if(context.managers.begin(), context.managers.end(), [&](const Manager& manager) {
            return manager.address == target.address && manager.identifier == target.identifier && manager.name == target.name;
        });
        if (found == context.managers.end()) { outcome = changed; break; }
        if (found->complete) continue;
        complete = false;
        recovery_wait(attempt, *found);
    }
    return complete;
}
static void drain_attempt(Attempt& attempt, const Context& context, bool complete, Drain drain, std::unique_lock<std::mutex>& guard) {
    auto& outcome = attempt.outcome;
    if (outcome == pending) {
        if (!complete && attempt.elapsed >= 60000) outcome = unknown;
        else {
            auto working = attempt;
            const auto sampled = context;
            guard.unlock();
            const bool drained = drain && drain(working, sampled);
            guard.lock();
            attempt = std::move(working);
            if (!drained && outcome == pending) outcome = unknown;
            else if (complete && outcome == pending) outcome = finished;
        }
    }
}
std::vector<Outcome> Queue::tick(uint64_t stamp, uint64_t now, Drain drain) {
    std::unique_lock<std::mutex> guard(mutex_);
    if (!pending_.load(std::memory_order_relaxed)) return {};
    auto& attempt = attempts_.back();
    attempt.elapsed = now - attempt.admitted_at;
    auto& outcome = attempt.outcome;
    const auto& context = current_;
    if (!attempt.started) attempt.context = context;
    const auto context_outcome = recovery_context(attempt, context, stamp, now - sampled_at_);
    if (context_outcome != pending) outcome = context_outcome;
    else {
        if (!attempt.started) {
            attempt.context = context;
            select_targets(attempt, context);
            attempt.started = true;
            if (attempt.targets.empty() && outcome == pending) outcome = no_active;
        }
        const bool complete = recovery_targets(attempt, context);
        drain_attempt(attempt, context, complete, drain, guard);
    }
    const bool report = repeat_ || outcome != pending || !attempt.last_report || now - attempt.last_report >= 5000;
    repeat_ = false;
    if (outcome != pending) pending_.store(false, std::memory_order_release);
    if (!report) return {};
    attempt.last_report = now;
    return {outcome};
}
std::deque<Attempt> Queue::history() const { std::lock_guard<std::mutex> guard(mutex_); return attempts_; }
}
