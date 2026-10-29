#pragma once
#include "sentinel_recovery.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace sentinel::recovery {
struct Context {
    sc_native_scope scope{};
    uint64_t stamp = 0, fingerprint = 0;
    bool gameplay = false, readable = true, ownership_proven = false;
    uint32_t candidates = 0;
    std::string namespace_id, save, map, encounter, kind = "unknown", completion = "unqualified";
};
struct Attempt {
    Result result{};
    Context context{};
    uint64_t admitted_at = 0;
    bool console = false, reported = false, binding = false;
};
class Queue {
public:
    bool has_pending() const { return pending_.load(std::memory_order_acquire); }
    void observe(Context, uint64_t now);
    Result inspect(const Request&, uint64_t now, bool scope_current = true);
    Result submit(const Request&, uint64_t now, bool scope_current = true);
    void enqueue_console(uint64_t stamp, uint64_t now);
    Result result(const Request&, bool release, uint64_t now, bool scope_current = true);
    std::vector<Outcome> tick(uint64_t stamp, uint64_t now);
    std::deque<Attempt> history() const;
private:
    void make_room();
    void update_pending();
    bool matches(const Request&) const;
    mutable std::mutex mutex_;
    std::atomic<bool> pending_{false};
    Context current_{};
    uint64_t token_ = 0, sampled_at_ = 0, console_id_ = 0;
    uint64_t displaced_console_ = 0;
    std::deque<Attempt> attempts_;
};
Queue& queue();
}
