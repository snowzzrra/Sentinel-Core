#include "native_model.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace sentinel::native {
static_assert(sizeof(sc_save_backup_request) == 152);
static_assert(sizeof(sc_save_backup_snapshot) == 648);
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
bool same_backup(const Diagnostics::Slot& s, const sc_save_backup_request* b) {
    return s.is_backup == (b != nullptr) && (!b ||
        (s.backup_request.campaign == b->campaign && s.backup_request.slot == b->slot &&
         s.backup_request.work_deadline_ms == b->work_deadline_ms &&
         std::memcmp(s.backup_request.namespace_id, b->namespace_id, sizeof(b->namespace_id)) == 0));
}
bool backup_options(const sc_save_backup_request& b) {
    if (b.campaign > 2 || b.slot > 11 || !b.work_deadline_ms ||
        b.work_deadline_ms > SC_SAVE_BACKUP_MAX_WORK_MS || b.namespace_id[64]) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = b.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    return true;
}
bool same_points(const Diagnostics::Slot& s, const sc_weapon_points_request* p) {
    return s.is_weapon_points == (p != nullptr) && (!p || weapon_points::same(s.weapon_points_request, *p));
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
        if (state == SC_DIAGNOSTIC_CLAIMED && s.awaiting_backup.load(std::memory_order_acquire)) {
            sc_save_backup_snapshot progress{}; s.backup->inspect(progress);
            // A failed copy can precede the outer native read completion. Keep
            // the slot while either that read or the actual copy still runs.
            if ((progress.flags & SC_BACKUP_READ_TERMINAL) && progress.state != SC_BACKUP_COPYING) {
                auto done = s.result; done.completed_at_ms = now;
                done.state = s.submission.entered ? SC_DIAGNOSTIC_EXECUTED : SC_DIAGNOSTIC_REJECTED;
                done.reason = s.submission.exception ? SC_NATIVE_EXCEPTION :
                    (s.submission.matched ? SC_NATIVE_NONE : SC_NATIVE_BINDING_FAILED);
                finish(s, done, s.detail);
            }
        } else if (state == SC_DIAGNOSTIC_QUEUED && now >= s.result.deadline_at_ms) {
            queue_detail(s);
            if (s.backup) s.backup->readback_finished(false);
            s.result.state = SC_DIAGNOSTIC_EXPIRED; s.result.reason = SC_NATIVE_DEADLINE;
            s.result.completed_at_ms = now; s.state.store(SC_DIAGNOSTIC_EXPIRED, std::memory_order_release);
        } else if (state >= SC_DIAGNOSTIC_EXECUTED && now >= s.result.completed_at_ms &&
                   now - s.result.completed_at_ms >= SC_DIAGNOSTIC_RETENTION_MS) {
            s.backup.reset();
            s.state.store(SC_DIAGNOSTIC_UNKNOWN, std::memory_order_release);
        }
    }
}
sc_diagnostic_result Diagnostics::submit(const sc_diagnostic_request& r, uint32_t reject, uint64_t now,
        sc_diagnostic_detail* detail, const sc_save_backup_request* backup, const sc_weapon_points_request* points) {
    if (detail) { *detail = {}; detail->revision = SC_DIAGNOSTIC_DETAIL_REVISION; detail->stage = SC_STAGE_ADMISSION; }
    collect(now);
    auto out = initial(r);
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) != SC_DIAGNOSTIC_UNKNOWN && key(s.request, r)) {
        if (!same_scope(s.request.expected, r.expected) || s.request.deadline_ms != r.deadline_ms ||
            !same_backup(s, backup) || !same_points(s, points)) {
            out.state = SC_DIAGNOSTIC_REJECTED; out.reason = SC_NATIVE_DUPLICATE_MISMATCH; return out;
        }
        return retrieve(r, false, now, detail, backup, points);
    }
    if (!reject && backup && !backup_options(*backup)) reject = SC_NATIVE_SCOPE_MISMATCH;
    if (!reject && points && (backup || !weapon_points::valid(*points))) reject = SC_NATIVE_SCOPE_MISMATCH;
    if (!reject && (r.deadline_ms == 0 || r.deadline_ms > SC_DIAGNOSTIC_MAX_DEADLINE_MS)) reject = SC_NATIVE_DEADLINE;
    if (reject) { out.state = SC_DIAGNOSTIC_REJECTED; out.reason = reject; return out; }
    for (auto& s : slots_) if (s.state.load(std::memory_order_acquire) == SC_DIAGNOSTIC_UNKNOWN) {
        std::shared_ptr<save::BackupJob> job;
        if (backup) try {
            job = std::make_shared<save::BackupJob>(r.expected.pid, r.expected.process_created, now + backup->work_deadline_ms);
        } catch (const std::bad_alloc&) {
            out.state = SC_DIAGNOSTIC_REJECTED; out.reason = SC_NATIVE_BINDING_FAILED; return out;
        }
        s.is_backup = backup != nullptr; s.backup_request = backup ? *backup : sc_save_backup_request{};
        s.is_weapon_points = points != nullptr;
        s.weapon_points_request = points ? *points : sc_weapon_points_request{};
        s.weapon_points_result = points ? weapon_points::initial(*points) : sc_weapon_points_result{};
        s.backup = std::move(job); s.submission = {};
        s.awaiting_backup.store(false, std::memory_order_relaxed);
        s.request = r; s.cancel.store(false, std::memory_order_relaxed);
        s.detail = {}; s.detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
        s.admitted_lock_misses = claim_lock_misses_.load(std::memory_order_relaxed);
        s.detail.stage = SC_STAGE_QUEUE; if (detail) *detail = s.detail;
        out.state = SC_DIAGNOSTIC_QUEUED; out.admitted_at_ms = now; out.deadline_at_ms = now + r.deadline_ms;
        s.result = out; s.state.store(SC_DIAGNOSTIC_QUEUED, std::memory_order_release); return out;
    }
    out.state = SC_DIAGNOSTIC_REJECTED; out.reason = SC_NATIVE_QUEUE_FULL; return out;
}
sc_diagnostic_result Diagnostics::retrieve(const sc_diagnostic_request& r, bool cancel, uint64_t now,
        sc_diagnostic_detail* detail, const sc_save_backup_request* backup, const sc_weapon_points_request* points) {
    if (detail) { *detail = {}; detail->revision = SC_DIAGNOSTIC_DETAIL_REVISION; }
    collect(now);
    for (auto& s : slots_) {
        auto state = s.state.load(std::memory_order_acquire);
        if (state == SC_DIAGNOSTIC_UNKNOWN || !key(s.request, r) || !same_scope(s.request.expected, r.expected) ||
            !same_backup(s, backup) || !same_points(s, points) ||
            ((backup || points) && s.request.deadline_ms != r.deadline_ms)) continue;
        if (cancel && (state == SC_DIAGNOSTIC_QUEUED || state == SC_DIAGNOSTIC_CLAIMED)) {
            s.cancel.store(true, std::memory_order_release);
            if (s.backup) s.backup->cancel();
            if (state == SC_DIAGNOSTIC_QUEUED) {
                queue_detail(s);
                s.result.state = SC_DIAGNOSTIC_CANCELLED; s.result.reason = SC_NATIVE_CANCELLED;
                s.result.completed_at_ms = now; state = SC_DIAGNOSTIC_CANCELLED;
                if (s.backup) s.backup->readback_finished(false);
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
sc_weapon_points_result Diagnostics::points_result(const sc_weapon_points_request& r, bool cancel, uint64_t now, bool release) {
    auto out = weapon_points::initial(r);
    const auto execution = retrieve(r.execution, cancel, now, nullptr, nullptr, &r);
    if (execution.state >= SC_DIAGNOSTIC_EXECUTED) {
        for (auto& s : slots_) {
            // Native facts belong exclusively to the callback until terminal release.
            if (s.state.load(std::memory_order_acquire) >= SC_DIAGNOSTIC_EXECUTED &&
                same_points(s,&r) && key(s.request,r.execution) && same_scope(s.request.expected,r.execution.expected)) {
                out = s.weapon_points_result;
                if (release) s.state.store(SC_DIAGNOSTIC_UNKNOWN, std::memory_order_release);
                break;
            }
        }
    }
    out.execution = execution; return out;
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
    if (s.backup && !s.awaiting_backup.load(std::memory_order_acquire)) s.backup->readback_finished(false);
    result.cancel_requested = s.cancel.load(std::memory_order_acquire);
    detail.revision = SC_DIAGNOSTIC_DETAIL_REVISION;
    s.detail = detail; s.result = result; s.state.store(result.state, std::memory_order_release);
}
void Diagnostics::await_backup(Slot& s, sc_diagnostic_result result, sc_diagnostic_detail detail,
        save::SubmissionResult submission) {
    result.state = SC_DIAGNOSTIC_CLAIMED; result.completed_at_ms = 0;
    s.submission = submission; s.result = result; s.detail = detail;
    s.awaiting_backup.store(true, std::memory_order_release);
    // No caller access to s is permitted after publication: retrieval may now
    // observe completion and eventually reuse the slot. Workers own only the job.
}
sc_save_backup_snapshot Diagnostics::backup_result(const sc_save_backup_request& r, bool cancel, uint64_t now) {
    sc_save_backup_snapshot out{}; out.size = sizeof(out); out.abi_version = SC_SAVE_BACKUP_ABI_VERSION;
    out.execution = retrieve(r.execution, cancel, now, nullptr, &r);
    if (out.execution.cancel_requested) out.flags = SC_BACKUP_CANCEL_REQUESTED;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id)); out.campaign = r.campaign; out.slot = r.slot;
    switch (out.execution.state) {
    case SC_DIAGNOSTIC_UNKNOWN: return out;
    case SC_DIAGNOSTIC_QUEUED: out.state = SC_BACKUP_QUEUED; break;
    case SC_DIAGNOSTIC_CLAIMED: out.state = SC_BACKUP_CLAIMED; break;
    case SC_DIAGNOSTIC_REJECTED: out.state = SC_BACKUP_REJECTED; break;
    case SC_DIAGNOSTIC_EXPIRED: out.state = SC_BACKUP_EXPIRED; break;
    case SC_DIAGNOSTIC_CANCELLED: out.state = SC_BACKUP_CANCELLED; break;
    default: out.state = SC_BACKUP_FAILED; break;
    }
    for (const auto& s : slots_) {
        if (s.state.load(std::memory_order_acquire) == SC_DIAGNOSTIC_UNKNOWN || !same_backup(s, &r) ||
            !key(s.request, r.execution) || !same_scope(s.request.expected, r.execution.expected)) continue;
        // Callback-owned submission/result fields are invisible until handoff.
        if (!s.awaiting_backup.load(std::memory_order_acquire)) return out;
        out.execution = s.result; out.execution.cancel_requested = s.cancel.load(std::memory_order_acquire);
        out.execution.retrieved = 1; out.execution.retrieved_at_ms = now;
        out.native_exception = s.submission.exception;
        out.flags = (s.submission.entered ? SC_BACKUP_NATIVE_ENTERED : 0u) |
            (s.submission.task_returned ? SC_BACKUP_TASK_RETURNED : 0u) |
            (s.submission.matched ? SC_BACKUP_SOURCE_MATCHED : 0u);
        s.backup->inspect(out);
        if (out.execution.state == SC_DIAGNOSTIC_CLAIMED) {
            if (out.state != SC_BACKUP_COPYING && out.state != SC_BACKUP_COPIED) out.state = SC_BACKUP_WAITING_NATIVE;
        } else if (out.execution.state == SC_DIAGNOSTIC_EXECUTED) {
            out.state = s.submission.matched && out.state == SC_BACKUP_COMPLETE ? SC_BACKUP_COMPLETE : SC_BACKUP_FAILED;
            if (!s.submission.matched) out.failure = SC_BACKUP_FAILURE_NATIVE;
        } else if (out.execution.state == SC_DIAGNOSTIC_REJECTED) out.state = SC_BACKUP_REJECTED;
        return out;
    }
    return out;
}
void Diagnostics::cancel_pending(uint64_t now) {
    for (auto& s : slots_) {
        const auto state = s.state.load(std::memory_order_acquire);
        if (s.backup && (state == SC_DIAGNOSTIC_CLAIMED || state == SC_DIAGNOSTIC_QUEUED)) s.backup->cancel();
        if (state == SC_DIAGNOSTIC_CLAIMED) s.cancel.store(true, std::memory_order_release);
        if (state == SC_DIAGNOSTIC_QUEUED) {
            if (s.backup) s.backup->readback_finished(false);
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
