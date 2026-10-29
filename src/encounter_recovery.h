#pragma once
#include "sentinel_native.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace sentinel::recovery {
enum Outcome { pending, finished, no_active, unsupported, changed, unknown };
const char* message(Outcome);
bool boss_map(const std::string&);
bool protected_manager(const std::string& map, const std::string& name);
bool protected_actor(const std::string& name, const std::string& kind);
struct Actor {
    uintptr_t address = 0;
    uint32_t identifier = 0;
    bool protected_actor = false;
};
struct Manager {
    uintptr_t address = 0;
    uint32_t identifier = 0;
    bool active = false, complete = false, protected_fight = false, noncombat_wait = false;
    std::string name, kind, wait;
    std::vector<Actor> actors;
};
struct Context {
    sc_native_scope scope{};
    uint64_t stamp = 0;
    uintptr_t world = 0, player = 0;
    bool gameplay = false, readable = true, supported = false;
    std::string namespace_id, save, map;
    std::vector<Manager> managers;
};
struct Attempt {
    uint64_t id = 0, stamp = 0, admitted_at = 0, elapsed = 0, last_report = 0;
    uint64_t producers_stopped = 0, actors_killed = 0, kills_suppressed = 0;
    Outcome outcome = pending;
    bool started = false;
    Context context;
    std::vector<Manager> targets;
    std::string wait;
};
using Drain = bool(*)(Attempt&, const Context&);
class Queue {
public:
    bool has_pending() const { return pending_.load(std::memory_order_acquire); }
    void observe(Context, uint64_t now);
    void enqueue_console(uint64_t stamp, uint64_t now);
    std::vector<Outcome> tick(uint64_t stamp, uint64_t now, Drain = nullptr);
    std::deque<Attempt> history() const;
private:
    mutable std::mutex mutex_;
    std::atomic<bool> pending_{false};
    Context current_;
    uint64_t sampled_at_ = 0, next_id_ = 0;
    bool repeat_ = false;
    std::deque<Attempt> attempts_;
};
Queue& queue();
}
