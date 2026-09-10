#include "native_model.h"
#include <algorithm>
#include <cstring>

namespace sentinel::native {
bool same_scope(const sc_native_scope& a, const sc_native_scope& b) {
    return a.pid == b.pid && a.process_created == b.process_created &&
        a.lifecycle_generation == b.lifecycle_generation &&
        std::memcmp(a.instance_id, b.instance_id, sizeof(a.instance_id)) == 0;
}
void Lifecycle::event(uint32_t kind, uint64_t now, uint32_t tid) {
    ++sequence;
    if (sequence > history.size()) ++overwritten;
    history[(sequence - 1) % history.size()] = {sequence, generation, now, kind, state, tid, depth};
}
void Lifecycle::begin(bool change, bool known, bool flag, uint64_t now, uint32_t tid) {
    if (!depth) {
        ++generation;
        checkpoint_known = change && known; checkpoint = checkpoint_known && flag;
    }
    ++depth; state = SC_LIFETIME_TRANSITION;
    event(change ? SC_EVENT_CHANGE_BEGIN : SC_EVENT_PRIMARY_FREE_BEGIN, now, tid);
}
void Lifecycle::end(bool change, bool success, uint32_t game, uint64_t now, uint32_t tid) {
    if (!depth) { state = SC_LIFETIME_INVALID; event(SC_EVENT_GAP, now, tid); return; }
    --depth;
    if (!depth) state = !change ? SC_LIFETIME_INVALID : (!success ? SC_LIFETIME_FAILED :
        (game == SC_GAME_IN_GAME ? SC_LIFETIME_ACTIVE :
         (game == SC_GAME_MAIN_MENU ? SC_LIFETIME_MENU : SC_LIFETIME_INVALID)));
    event(change ? SC_EVENT_CHANGE_END : SC_EVENT_PRIMARY_FREE_END, now, tid);
}
void Lifecycle::page(sc_native_snapshot& out, uint64_t after) const {
    out.event_sequence = sequence; out.history_overwritten = overwritten;
    out.history_oldest = sequence ? (sequence > history.size() ? sequence - history.size() + 1 : 1) : 0;
    out.history_gap = after && out.history_oldest && after < out.history_oldest - 1;
    const auto first = after ? std::max(after + (after != UINT64_MAX), out.history_oldest) :
        std::max(out.history_oldest, sequence >= SC_NATIVE_EVENT_PAGE ? sequence - SC_NATIVE_EVENT_PAGE + 1 : 1);
    for (uint64_t n = first; n && n <= sequence && out.event_count < SC_NATIVE_EVENT_PAGE; ++n)
        out.events[out.event_count++] = history[(n - 1) % history.size()];
}
namespace {
bool key(const sc_diagnostic_request& a, const sc_diagnostic_request& b) {
    return a.request_id == b.request_id &&
        std::memcmp(a.nonce, b.nonce, sizeof(a.nonce)) == 0;
}
sc_diagnostic_result initial(const sc_diagnostic_request& r) {
    sc_diagnostic_result out{}; out.scope = r.expected; out.request_id = r.request_id;
    std::memcpy(out.nonce, r.nonce, sizeof(out.nonce)); return out;
}
}
void Diagnostics::queue_detail(Slot& s) {
    s.detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
    s.detail.stage = SC_STAGE_QUEUE;
    s.detail.claim_lock_missed = claim_lock_misses_.load(std::memory_order_relaxed) != s.admitted_lock_misses;
}
void Diagnostics::collect(uint64_t now) {
    for (auto& s : slots_) {
        auto state = s.state.load(std::memory_order_acquire);
        if (state == SC_DIAGNOSTIC_QUEUED && now >= s.result.deadline_at_ms) {
            queue_detail(s);
            s.result.state = SC_DIAGNOSTIC_EXPIRED; s.result.reason = SC_NATIVE_DEADLINE;
            s.result.completed_at_ms = now; s.state.store(SC_DIAGNOSTIC_EXPIRED, std::memory_order_release);
        } else if (state >= SC_DIAGNOSTIC_EXECUTED && now >= s.result.completed_at_ms &&
                   now - s.result.completed_at_ms >= SC_DIAGNOSTIC_RETENTION_MS) {
            s.state.store(SC_DIAGNOSTIC_UNKNOWN, std::memory_order_release);
        }
    }
}
sc_diagnostic_result Diagnostics::submit(const sc_diagnostic_request& r, uint32_t reject, uint64_t now, sc_diagnostic_detail* detail) {
    if (detail) { *detail = {}; detail->revision = SC_DIAGNOSTIC_DETAIL_REVISION; detail->stage = SC_STAGE_ADMISSION; }
    collect(now);
    auto out = initial(r);
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) != SC_DIAGNOSTIC_UNKNOWN && key(s.request, r)) {
        if (!same_scope(s.request.expected, r.expected) || s.request.deadline_ms != r.deadline_ms) {
            out.state = SC_DIAGNOSTIC_REJECTED; out.reason = SC_NATIVE_DUPLICATE_MISMATCH; return out;
        }
        return retrieve(r, false, now, detail);
    }
    if (!reject && (r.deadline_ms == 0 || r.deadline_ms > SC_DIAGNOSTIC_MAX_DEADLINE_MS)) reject = SC_NATIVE_DEADLINE;
    if (reject) { out.state = SC_DIAGNOSTIC_REJECTED; out.reason = reject; return out; }
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) == SC_DIAGNOSTIC_UNKNOWN) {
        s.request = r; s.cancel.store(false, std::memory_order_relaxed);
        s.detail = {}; s.detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
        s.admitted_lock_misses = claim_lock_misses_.load(std::memory_order_relaxed);
        s.detail.stage = SC_STAGE_QUEUE; if (detail) *detail = s.detail;
        out.state = SC_DIAGNOSTIC_QUEUED; out.admitted_at_ms = now; out.deadline_at_ms = now + r.deadline_ms;
        s.result = out; s.state.store(SC_DIAGNOSTIC_QUEUED, std::memory_order_release); return out;
    }
    out.state = SC_DIAGNOSTIC_REJECTED; out.reason = SC_NATIVE_QUEUE_FULL; return out;
}
sc_diagnostic_result Diagnostics::retrieve(const sc_diagnostic_request& r, bool cancel, uint64_t now, sc_diagnostic_detail* detail) {
    if (detail) { *detail = {}; detail->revision = SC_DIAGNOSTIC_DETAIL_REVISION; }
    collect(now);
    for (auto& s : slots_) {
        auto state = s.state.load(std::memory_order_acquire);
        if (state == SC_DIAGNOSTIC_UNKNOWN || !key(s.request, r) || !same_scope(s.request.expected, r.expected)) continue;
        if (cancel && (state == SC_DIAGNOSTIC_QUEUED || state == SC_DIAGNOSTIC_CLAIMED)) {
            s.cancel.store(true, std::memory_order_release);
            if (state == SC_DIAGNOSTIC_QUEUED) {
                queue_detail(s);
                s.result.state = SC_DIAGNOSTIC_CANCELLED; s.result.reason = SC_NATIVE_CANCELLED;
                s.result.completed_at_ms = now; state = SC_DIAGNOSTIC_CANCELLED;
                s.state.store(state, std::memory_order_release);
            }
        }
        // During CLAIMED, result belongs to the callback. Return only immutable
        // admission fields copied from the request; never race its result writes.
        auto out = state == SC_DIAGNOSTIC_CLAIMED ? initial(s.request) : s.result;
        if (detail && state != SC_DIAGNOSTIC_CLAIMED) *detail = s.detail;
        out.state = state; out.cancel_requested = s.cancel.load(std::memory_order_acquire);
        out.retrieved = 1; out.retrieved_at_ms = now; return out;
    }
    auto out = initial(r); out.reason = SC_NATIVE_NOT_FOUND; out.retrieved = 1; out.retrieved_at_ms = now; return out;
}
Diagnostics::Slot* Diagnostics::claim(uint64_t now) {
    collect(now);
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) == SC_DIAGNOSTIC_QUEUED) {
        queue_detail(s);
        s.result.state = SC_DIAGNOSTIC_CLAIMED; s.result.claimed_at_ms = now;
        s.state.store(SC_DIAGNOSTIC_CLAIMED, std::memory_order_release); return &s;
    }
    return nullptr;
}
void Diagnostics::finish(Slot& s, sc_diagnostic_result result, sc_diagnostic_detail detail) {
    result.cancel_requested = s.cancel.load(std::memory_order_acquire);
    detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
    s.detail = detail; s.result = result; s.state.store(result.state, std::memory_order_release);
}
void Diagnostics::cancel_pending(uint64_t now) {
    for (auto& s : slots_) {
        const auto state = s.state.load(std::memory_order_acquire);
        if (state == SC_DIAGNOSTIC_CLAIMED) s.cancel.store(true, std::memory_order_release);
        if (state == SC_DIAGNOSTIC_QUEUED) {
            queue_detail(s);
            s.result.state = SC_DIAGNOSTIC_CANCELLED; s.result.reason = SC_NATIVE_STOPPED;
            s.result.completed_at_ms = now; s.state.store(SC_DIAGNOSTIC_CANCELLED, std::memory_order_release);
        }
    }
}
void Diagnostics::counts(sc_native_snapshot& out, uint64_t now) {
    collect(now);
    for (auto& s : slots_) {
        const auto state = s.state.load(std::memory_order_acquire);
        out.queued += state == SC_DIAGNOSTIC_QUEUED;
        out.claimed += state == SC_DIAGNOSTIC_CLAIMED;
        out.retained_results += state >= SC_DIAGNOSTIC_EXECUTED;
    }
}
}
