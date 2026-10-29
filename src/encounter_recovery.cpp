#include "encounter_recovery.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace sentinel::recovery {
namespace {
bool scope_equal(const sc_native_scope& a, const sc_native_scope& b) {
    return a.pid == b.pid && a.process_created == b.process_created &&
        a.lifecycle_generation == b.lifecycle_generation && !std::memcmp(a.instance_id, b.instance_id, 16);
}
bool same(const Request& a, const Request& b) {
    return scope_equal(a.execution.expected, b.execution.expected) &&
        a.execution.request_id == b.execution.request_id && !std::memcmp(a.execution.nonce, b.execution.nonce, 16) &&
        a.execution.deadline_ms == b.execution.deadline_ms && a.token == b.token && a.stamp == b.stamp &&
        !std::memcmp(a.namespace_id, b.namespace_id, 65) && !std::memcmp(a.save, b.save, 96);
}
struct Finisher {
    std::string_view kind;
    Outcome (*execute)(const Context&);
};
constexpr std::array<Finisher, 0> finishers{};
Outcome execute(const Context& context) {
    if (context.ownership_proven)
        for (const auto& finisher : finishers)
            if (finisher.kind == context.kind) return finisher.execute(context);
    return unsupported;
}
}
Queue& queue() { static Queue value; return value; }
void Queue::update_pending() {
    pending_.store(displaced_console_ || std::any_of(attempts_.begin(), attempts_.end(), [](const Attempt& attempt) {
        return attempt.binding || attempt.result.state == SC_DIAGNOSTIC_QUEUED || (attempt.console && !attempt.reported);
    }), std::memory_order_release);
}
void Queue::observe(Context context, uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!scope_equal(context.scope, current_.scope) || context.stamp != current_.stamp ||
        context.namespace_id != current_.namespace_id || context.save != current_.save ||
        context.map != current_.map || context.fingerprint != current_.fingerprint ||
        context.candidates != current_.candidates) ++token_;
    current_ = std::move(context); sampled_at_ = now;
    for (auto& attempt : attempts_) {
        if (!attempt.binding || attempt.admitted_at > now) continue;
        attempt.binding = false;
        auto& out = attempt.result;
        auto& r = out.request;
        out.state = SC_DIAGNOSTIC_REJECTED;
        if (attempt.console ? r.stamp != current_.stamp :
            !scope_equal(r.execution.expected, current_.scope) || current_.namespace_id != r.namespace_id) {
            out.outcome = changed; continue;
        }
        if (now - attempt.admitted_at >= r.execution.deadline_ms) { out.outcome = no_active; continue; }
        if (attempt.console) {
            r.execution.expected = current_.scope;
            std::memcpy(r.namespace_id, current_.namespace_id.c_str(), (std::min)(current_.namespace_id.size(), size_t(64)));
        }
        r.token = token_; r.stamp = current_.stamp;
        std::memcpy(r.save, current_.save.c_str(), (std::min)(current_.save.size(), size_t(95)));
        attempt.context = current_;
        out.state = attempt.console ? SC_DIAGNOSTIC_QUEUED : SC_DIAGNOSTIC_UNKNOWN;
        out.outcome = !current_.gameplay ? no_active : !current_.readable ? unknown : current_.candidates ? unsupported : no_active;
    }
    update_pending();
}
bool Queue::matches(const Request& r) const {
    return scope_equal(r.execution.expected, current_.scope) && r.stamp == current_.stamp &&
        r.token == token_ && current_.namespace_id == r.namespace_id && current_.save == r.save;
}
void Queue::make_room() {
    if (attempts_.size() < 16) return;
    if (attempts_.front().console && !attempts_.front().reported) ++displaced_console_;
    attempts_.pop_front();
}
Result Queue::inspect(const Request& request, uint64_t now, bool scope_current) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& attempt : attempts_) if (attempt.result.request.execution.request_id == request.execution.request_id) {
        auto expected = attempt.result.request;
        expected.token = request.token; expected.stamp = request.stamp;
        std::memcpy(expected.save, request.save, sizeof(expected.save));
        if (!same(expected, request)) return {request, SC_DIAGNOSTIC_REJECTED, changed};
        if (attempt.binding && (!scope_current || now - attempt.admitted_at >= request.execution.deadline_ms)) {
            attempt.binding = false;
            attempt.result.state = SC_DIAGNOSTIC_REJECTED;
            attempt.result.outcome = scope_current ? no_active : changed;
            update_pending();
        }
        auto out = attempt.result;
        if (out.state == SC_DIAGNOSTIC_UNKNOWN) out.state = SC_DIAGNOSTIC_EXECUTED;
        return out;
    }
    Result out{request, SC_DIAGNOSTIC_QUEUED, pending};
    if (!scope_current) out = {request, SC_DIAGNOSTIC_REJECTED, changed};
    make_room();
    attempts_.push_back({out, {}, now, false, false, scope_current});
    update_pending();
    return out;
}
Result Queue::submit(const Request& r, uint64_t now, bool scope_current) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& attempt : attempts_) if (attempt.result.request.execution.request_id == r.execution.request_id) {
        if (!same(attempt.result.request, r)) return {r, SC_DIAGNOSTIC_REJECTED, changed};
        if (attempt.result.state != SC_DIAGNOSTIC_UNKNOWN) return attempt.result;
        attempt.result = {r, SC_DIAGNOSTIC_QUEUED, pending};
        if (!scope_current || !matches(r)) attempt.result = {r, SC_DIAGNOSTIC_REJECTED, changed};
        else if (!current_.gameplay || now - attempt.admitted_at >= r.execution.deadline_ms)
            attempt.result = {r, SC_DIAGNOSTIC_REJECTED, no_active};
        update_pending();
        return attempt.result;
    }
    make_room();
    Attempt attempt{{r, SC_DIAGNOSTIC_REJECTED, no_active}, current_, now};
    if (!valid(r) || !r.execution.request_id || !r.execution.deadline_ms || r.execution.deadline_ms > 5000)
        attempt.result = {r, SC_DIAGNOSTIC_REJECTED, no_active};
    else if (!scope_current || !matches(r)) attempt.result = {r, SC_DIAGNOSTIC_REJECTED, changed};
    attempts_.push_back(std::move(attempt));
    update_pending();
    return attempts_.back().result;
}
void Queue::enqueue_console(uint64_t stamp, uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    Request request{};
    request.execution.request_id = (uint64_t(1) << 63) | ++console_id_;
    request.execution.deadline_ms = 2000; request.stamp = stamp;
    make_room();
    attempts_.push_back({{request, SC_DIAGNOSTIC_QUEUED, pending}, {}, now, true, false, true});
    update_pending();
}
Result Queue::result(const Request& r, bool release, uint64_t now, bool scope_current) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& attempt : attempts_) if (same(attempt.result.request, r)) {
        if (attempt.result.state == SC_DIAGNOSTIC_QUEUED && !scope_current)
            attempt.result = {r, SC_DIAGNOSTIC_REJECTED, changed};
        if (attempt.result.state == SC_DIAGNOSTIC_QUEUED &&
            (release || now - attempt.admitted_at >= r.execution.deadline_ms))
            attempt.result = {r, SC_DIAGNOSTIC_REJECTED, no_active};
        if (attempt.result.state != SC_DIAGNOSTIC_QUEUED) attempt.binding = false;
        update_pending();
        return attempt.result;
    }
    return {r, SC_DIAGNOSTIC_REJECTED, unknown};
}
std::vector<Outcome> Queue::tick(uint64_t stamp, uint64_t now) {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<Outcome> console;
    while (displaced_console_) { console.push_back(unknown); --displaced_console_; }
    for (auto& attempt : attempts_) {
        auto& result = attempt.result;
        if (result.state == SC_DIAGNOSTIC_QUEUED && !attempt.binding) {
            result.state = SC_DIAGNOSTIC_EXECUTED;
            result.outcome = stamp != result.request.stamp || !matches(result.request) ? changed :
                now - attempt.admitted_at >= result.request.execution.deadline_ms || now - sampled_at_ > 250 || !current_.gameplay ? no_active :
                !current_.readable ? unknown : !current_.candidates ? no_active : execute(current_);
            if (result.outcome != changed) attempt.context.completion = current_.completion;
        }
        if (attempt.console && !attempt.reported && !attempt.binding) { console.push_back(result.outcome); attempt.reported = true; }
    }
    update_pending();
    return console;
}
std::deque<Attempt> Queue::history() const { std::lock_guard<std::mutex> guard(mutex_); return attempts_; }
}
