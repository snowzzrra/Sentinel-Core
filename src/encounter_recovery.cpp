#include "encounter_recovery.h"
#include <algorithm>

namespace sentinel::recovery {
const char* message(Outcome outcome) {
    switch (outcome) {
    case pending: return "Encounter recovery in progress";
    case finished: return "Encounter finished";
    case no_active: return "No active encounter";
    case unsupported: return "Unsupported encounter";
    case changed: return "Encounter changed; request cancelled";
    default: return "Recovery outcome unknown";
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
std::vector<Outcome> Queue::tick(uint64_t stamp, uint64_t now, Drain drain) {
    std::unique_lock<std::mutex> guard(mutex_);
    if (!pending_.load(std::memory_order_relaxed)) return {};
    auto& attempt = attempts_.back();
    attempt.elapsed = now - attempt.admitted_at;
    auto& outcome = attempt.outcome;
    const auto& context = current_;
    if (!attempt.started) attempt.context = context;
    if (stamp != attempt.stamp || (attempt.started &&
        (context.scope.lifecycle_generation != attempt.context.scope.lifecycle_generation ||
         context.map != attempt.context.map || context.world != attempt.context.world ||
         context.player != attempt.context.player || context.namespace_id != attempt.context.namespace_id ||
         context.save != attempt.context.save || !context.gameplay))) outcome = changed;
    else if (!context.gameplay) outcome = no_active;
    else if (!context.supported) outcome = unsupported;
    else if (now - sampled_at_ > 250 || !context.readable) outcome = unknown;
    else {
        if (!attempt.started) {
            attempt.context = context;
            for (const auto& manager : context.managers)
                if (manager.active && !manager.complete && !manager.actors.empty()) attempt.targets.push_back(manager);
            attempt.started = true;
            if (attempt.targets.empty()) outcome = no_active;
        }
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
            if (found->protected_fight) outcome = unsupported;
            if (found->noncombat_wait && found->actors.empty() && attempt.elapsed >= 2000) outcome = unknown;
            for (const auto& actor : found->actors) if (actor.protected_actor) outcome = unsupported;
            if (!found->wait.empty()) {
                if (!attempt.wait.empty()) attempt.wait += "; ";
                attempt.wait += found->name + ": " + found->wait;
            }
        }
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
    const bool report = repeat_ || outcome != pending || !attempt.last_report || now - attempt.last_report >= 5000;
    repeat_ = false;
    if (outcome != pending) pending_.store(false, std::memory_order_release);
    if (!report) return {};
    attempt.last_report = now;
    return {outcome};
}
std::deque<Attempt> Queue::history() const { std::lock_guard<std::mutex> guard(mutex_); return attempts_; }
}
