#include "deathlink.h"
#include <cstring>
#include <algorithm>

namespace sentinel::deathlink {
namespace {
constexpr uint32_t max_pending = 8;
constexpr uint32_t seen_capacity = 64;
constexpr uint32_t local_capacity = 16;
constexpr uint32_t max_attempts = 8;
constexpr uint64_t max_lifetime_ms = 120000;
constexpr uint64_t retry_settle_ms = 500;
constexpr uint64_t post_death_settle_ms = 2000;
SRWLOCK state_lock = SRWLOCK_INIT;

struct RemoteEvent {
    uint64_t id = 0;
    uint8_t hash[16]{};
    uint32_t state = SC_DEATHLINK_REMOTE_IDLE;
    uint32_t attempts = 0;
    uint32_t protection = SC_DEATHLINK_PROTECTION_NONE;
    uint64_t received_at = 0;
    uint64_t last_applied_at = 0;
    uint32_t outcome = SC_DEATHLINK_OUTCOME_OK;
    uint32_t flags = 0;
};

struct LocalEvent {
    uint64_t sequence = 0;
    uint64_t time_ms = 0;
    uint32_t cause = SC_DEATHLINK_CAUSE_LOCAL;
    uint32_t protection = SC_DEATHLINK_PROTECTION_NONE;
    uint32_t flags = 0;
};

struct State {
    uint32_t enabled = 0, mode = SC_DEATHLINK_MODE_SOFT;
    RemoteEvent pending[max_pending]{};
    uint32_t pending_count = 0;
    RemoteEvent seen[seen_capacity]{};
    RemoteEvent terminal{};
    uint32_t seen_pos = 0, seen_count = 0;
    LocalEvent local[local_capacity]{};
    uint32_t local_count = 0;
    uint64_t local_next_sequence = 1;
    uint64_t local_acked_sequence = 0;
    uint64_t local_dropped = 0;
    uint64_t next_apply_allowed_at = 0;
    uint64_t operations = 0;
};
State shared_state{};
char bound_namespace[65]{};
uintptr_t last_bound_player = 0;

void reset_state_locked(const char* id) {
    shared_state = {};
    last_bound_player = 0;
    if (id) std::memcpy(bound_namespace, id, sizeof(bound_namespace));
    else std::memset(bound_namespace, 0, sizeof(bound_namespace));
}

void check_namespace(const char* id) {
    if (std::memcmp(bound_namespace, id, sizeof(bound_namespace)) != 0) {
        reset_state_locked(id);
    }
}

const RemoteEvent* seen_contains_locked(uint64_t id) {
    for (uint32_t i = 0; i < shared_state.seen_count; ++i)
        if (shared_state.seen[i].id == id) return &shared_state.seen[i];
    return nullptr;
}

void seen_add_locked(uint64_t id) {
    if (!id || seen_contains_locked(id)) return;
    shared_state.seen[shared_state.seen_pos] = shared_state.terminal;
    shared_state.seen[shared_state.seen_pos].id = id;
    shared_state.seen_pos = (shared_state.seen_pos + 1) % seen_capacity;
    if (shared_state.seen_count < seen_capacity) ++shared_state.seen_count;
}

void append_local_locked(const LocalEvent& event) {
    while (shared_state.local_count && shared_state.local[0].sequence <= shared_state.local_acked_sequence) {
        for (uint32_t i = 1; i < shared_state.local_count; ++i)
            shared_state.local[i - 1] = shared_state.local[i];
        --shared_state.local_count;
    }
    // Preserve unacknowledged events. Overflow is observable and never ACKed.
    if (shared_state.local_count >= local_capacity) {
        ++shared_state.local_dropped;
        return;
    }
    shared_state.local[shared_state.local_count++] = event;
}

void append_local(uint32_t cause, uint32_t protection, uint32_t flags, uint64_t now_ms) {
    AcquireSRWLockExclusive(&state_lock);
    if (!shared_state.enabled) {
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }
    LocalEvent event{};
    event.sequence = shared_state.local_next_sequence++;
    event.time_ms = now_ms;
    event.cause = cause;
    event.protection = protection;
    event.flags = flags;
    append_local_locked(event);
    ReleaseSRWLockExclusive(&state_lock);
}

const RemoteEvent* head_locked() {
    return shared_state.pending_count ? &shared_state.pending[0] : nullptr;
}

void pop_head_locked() {
    shared_state.terminal = shared_state.pending[0];
    for (uint32_t i = 1; i < shared_state.pending_count; ++i)
        shared_state.pending[i - 1] = shared_state.pending[i];
    if (shared_state.pending_count) --shared_state.pending_count;
    shared_state.pending[shared_state.pending_count] = {};
}

void fill(const State& state, sc_deathlink_result& out) {
    out.enabled = state.enabled;
    out.mode = state.mode;
    out.pending_count = state.pending_count;
    out.local_candidates = 0;
    out.local_suppressed = 0;
    out.local_state = SC_DEATHLINK_LOCAL_EMPTY;
    if (state.local_dropped) out.flags |= SC_DEATHLINK_FLAG_LOCAL_LOSS;
    for (uint32_t i = 0; i < state.local_count; ++i) {
        if (state.local[i].sequence <= state.local_acked_sequence) continue;
        if (state.local[i].flags & SC_DEATHLINK_FLAG_SUPPRESSED) ++out.local_suppressed;
        else ++out.local_candidates;
    }
    const RemoteEvent* head = state.terminal.id ? &state.terminal :
        (state.pending_count ? &state.pending[0] : nullptr);
    if (head) {
        out.remote_state = head->state;
        out.remote_protection = head->protection;
        out.remote_attempts = head->attempts;
        out.remote_event_id = head->id;
        out.flags |= head->flags;
        out.outcome = head->outcome;
    } else {
        out.remote_state = SC_DEATHLINK_REMOTE_IDLE;
    }
    for (uint32_t i = 0; i < state.local_count; ++i) {
        if (state.local[i].sequence <= state.local_acked_sequence) continue;
        out.local_state = SC_DEATHLINK_LOCAL_AVAILABLE;
        out.local_death_sequence = state.local[i].sequence;
        out.local_death_time_ms = state.local[i].time_ms;
        out.local_cause = state.local[i].cause;
        out.local_protection = state.local[i].protection;
        out.local_flags = state.local[i].flags;
        break;
    }
    out.operations_applied = state.operations;
}
} // namespace

bool valid(const sc_deathlink_request& r) {
    if (r.namespace_id[64] || r.kind > SC_DEATHLINK_CANCEL_REMOTE) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = r.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    if (r.enabled > 1 || r.mode > SC_DEATHLINK_MODE_HARDCORE) return false;
    if (r.flags) return false;
    if (r.reserved0) return false;
    const bool no_state = !r.enabled && !r.mode;
    switch (r.kind) {
    case SC_DEATHLINK_OBSERVE:
    case SC_DEATHLINK_POLL_LOCAL:
        return no_state && !r.event_id && !r.ack_sequence;
    case SC_DEATHLINK_CONFIGURE:
        return !r.event_id && !r.ack_sequence;
    case SC_DEATHLINK_APPLY_REMOTE:
        return no_state && r.event_id != 0 && !r.ack_sequence;
    case SC_DEATHLINK_ACK_LOCAL:
        return no_state && !r.event_id && r.ack_sequence != 0;
    case SC_DEATHLINK_CANCEL_REMOTE:
        return no_state && r.event_id != 0 && !r.ack_sequence;
    default:
        return false;
    }
}

bool same(const sc_deathlink_request& a, const sc_deathlink_request& b) {
    return a.kind == b.kind && a.enabled == b.enabled && a.mode == b.mode &&
           a.event_id == b.event_id && a.ack_sequence == b.ack_sequence &&
           !std::memcmp(a.event_hash, b.event_hash, sizeof(a.event_hash)) &&
           !std::memcmp(a.namespace_id, b.namespace_id, sizeof(a.namespace_id));
}

sc_deathlink_result initial(const sc_deathlink_request& r) {
    sc_deathlink_result out{};
    out.size = sizeof(out);
    out.abi_version = SC_DEATHLINK_ABI_VERSION;
    out.kind = r.kind;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id));
    out.remote_state = SC_DEATHLINK_REMOTE_IDLE;
    out.local_state = SC_DEATHLINK_LOCAL_EMPTY;
    return out;
}

void reset_session(const char* namespace_id) {
    AcquireSRWLockExclusive(&state_lock);
    reset_state_locked(namespace_id);
    ReleaseSRWLockExclusive(&state_lock);
}

void record_native_death(uint32_t cause, uint32_t protection, uint64_t now_ms) {
    const uint32_t flags = cause == SC_DEATHLINK_CAUSE_REMOTE ? SC_DEATHLINK_FLAG_SUPPRESSED : 0;
    append_local(cause, protection, flags, now_ms);
    if (cause != SC_DEATHLINK_CAUSE_LOCAL) return;
    AcquireSRWLockExclusive(&state_lock);
    if (shared_state.enabled && shared_state.mode == SC_DEATHLINK_MODE_HARDCORE &&
        shared_state.pending_count && shared_state.pending[0].attempts) {
        auto& head = shared_state.pending[0];
        head.state = SC_DEATHLINK_REMOTE_RESOLVED_DEATH;
        head.flags = SC_DEATHLINK_FLAG_TRUE_DEATH;
        const auto id = head.id;
        pop_head_locked();
        seen_add_locked(id);
        shared_state.next_apply_allowed_at = now_ms + post_death_settle_ms;
    }
    ReleaseSRWLockExclusive(&state_lock);
}

void bind_run_state_if_needed(uintptr_t player) {
    // DeathLink keeps no player-bound state: every application resolves a fresh
    // native player and never retains engine pointers across reconstruction.
    (void)player;
}

// Executes at most one application step. The native call happens with the state
// lock released so hooks can record telemetry without deadlocking.
void execute(const sc_deathlink_request& r, sc_deathlink_result& out, const Calls& c) {
    const auto now = GetTickCount64();

    AcquireSRWLockExclusive(&state_lock);
    check_namespace(r.namespace_id);
    last_bound_player = c.player ? c.player(c.context) : 0;
    const auto player = last_bound_player;

    if (r.kind == SC_DEATHLINK_CONFIGURE) {
        if (shared_state.enabled == r.enabled && shared_state.mode == r.mode) {
            fill(shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
            out.outcome = SC_DEATHLINK_OUTCOME_NOOP;
            return;
        }
        shared_state.enabled = r.enabled;
        shared_state.mode = r.mode;
        shared_state.pending_count = 0;
        shared_state.local_count = 0;
        shared_state.local_acked_sequence = shared_state.local_next_sequence ? shared_state.local_next_sequence - 1 : 0;
        shared_state.next_apply_allowed_at = 0;
        const uint32_t enabled = shared_state.enabled;
        const uint32_t mode = shared_state.mode;
        fill(shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID | SC_DEATHLINK_FLAG_MUTATED;
        if (enabled) out.flags |= SC_DEATHLINK_FLAG_ENABLED;
        if (mode == SC_DEATHLINK_MODE_HARDCORE) out.flags |= SC_DEATHLINK_FLAG_HARDCORE;
        out.outcome = SC_DEATHLINK_OUTCOME_OK;
        return;
    }

    if (!shared_state.enabled) {
        fill(shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
        out.outcome = SC_DEATHLINK_OUTCOME_DISABLED;
        return;
    }
    out.flags |= SC_DEATHLINK_FLAG_ENABLED;
    if (shared_state.mode == SC_DEATHLINK_MODE_HARDCORE) out.flags |= SC_DEATHLINK_FLAG_HARDCORE;

    if (r.kind == SC_DEATHLINK_APPLY_REMOTE) {
        if (const auto* seen = seen_contains_locked(r.event_id)) {
            const bool matches = !std::memcmp(seen->hash, r.event_hash, sizeof(seen->hash));
            fill(shared_state, out);
            out.remote_event_id = seen->id;
            out.remote_state = seen->state;
            out.remote_attempts = seen->attempts;
            out.remote_protection = seen->protection;
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
            out.outcome = matches ? SC_DEATHLINK_OUTCOME_DUPLICATE : SC_DEATHLINK_OUTCOME_REJECTED;
            return;
        }
        for (uint32_t i = 0; i < shared_state.pending_count; ++i) {
            if (shared_state.pending[i].id == r.event_id) {
                const bool matches = !std::memcmp(shared_state.pending[i].hash, r.event_hash, sizeof(r.event_hash));
                fill(shared_state, out);
                out.remote_event_id = shared_state.pending[i].id;
                out.remote_state = shared_state.pending[i].state;
                out.remote_attempts = shared_state.pending[i].attempts;
                out.remote_protection = shared_state.pending[i].protection;
                ReleaseSRWLockExclusive(&state_lock);
                out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
                out.outcome = matches ? SC_DEATHLINK_OUTCOME_DUPLICATE : SC_DEATHLINK_OUTCOME_REJECTED;
                return;
            }
        }
        if (shared_state.pending_count >= max_pending) {
            fill(shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
            out.outcome = SC_DEATHLINK_OUTCOME_QUEUE_FULL;
            return;
        }
        shared_state.terminal = {};
        auto& slot = shared_state.pending[shared_state.pending_count++];
        slot.id = r.event_id;
        std::memcpy(slot.hash, r.event_hash, sizeof(slot.hash));
        slot.state = player ? SC_DEATHLINK_REMOTE_RECEIVED : SC_DEATHLINK_REMOTE_WAITING_SAFE;
        slot.received_at = now;
        out.flags |= SC_DEATHLINK_FLAG_MUTATED;
    } else if (r.kind == SC_DEATHLINK_CANCEL_REMOTE) {
        bool removed = false;
        for (uint32_t i = 0; i < shared_state.pending_count; ++i) {
            if (shared_state.pending[i].id != r.event_id) continue;
            if (std::memcmp(shared_state.pending[i].hash, r.event_hash, sizeof(r.event_hash))) {
                ReleaseSRWLockExclusive(&state_lock);
                out.outcome = SC_DEATHLINK_OUTCOME_REJECTED;
                return;
            }
            shared_state.terminal = shared_state.pending[i];
            shared_state.terminal.state = SC_DEATHLINK_REMOTE_CANCELLED;
            shared_state.terminal.outcome = SC_DEATHLINK_OUTCOME_CANCELLED;
            for (uint32_t j = i + 1; j < shared_state.pending_count; ++j)
                shared_state.pending[j - 1] = shared_state.pending[j];
            --shared_state.pending_count;
            shared_state.pending[shared_state.pending_count] = {};
            seen_add_locked(r.event_id);
            removed = true;
            break;
        }
        fill(shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
        if (removed) out.flags |= SC_DEATHLINK_FLAG_MUTATED;
        out.outcome = removed ? SC_DEATHLINK_OUTCOME_CANCELLED : SC_DEATHLINK_OUTCOME_NOOP;
        return;
    } else if (r.kind == SC_DEATHLINK_ACK_LOCAL) {
        bool acked = false;
        for (uint32_t i = 0; i < shared_state.local_count; ++i) {
            if (shared_state.local[i].sequence <= shared_state.local_acked_sequence) continue;
            if (shared_state.local[i].sequence == r.ack_sequence) {
                if (r.ack_sequence > shared_state.local_acked_sequence)
                    shared_state.local_acked_sequence = r.ack_sequence;
                acked = true;
            }
            break;
        }
        if (!acked && r.ack_sequence <= shared_state.local_acked_sequence) acked = true;
        fill(shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
        if (acked) out.flags |= SC_DEATHLINK_FLAG_MUTATED;
        out.outcome = acked ? SC_DEATHLINK_OUTCOME_OK : SC_DEATHLINK_OUTCOME_REJECTED;
        return;
    } else if (r.kind != SC_DEATHLINK_OBSERVE && r.kind != SC_DEATHLINK_POLL_LOCAL) {
        ReleaseSRWLockExclusive(&state_lock);
        out.outcome = SC_DEATHLINK_OUTCOME_REJECTED;
        return;
    }

    fill(shared_state, out);
    ReleaseSRWLockExclusive(&state_lock);
    out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
}

void expire_pending(uint64_t now) {
    AcquireSRWLockExclusive(&state_lock);
    while (shared_state.pending_count && now - shared_state.pending[0].received_at >= max_lifetime_ms) {
        auto& head = shared_state.pending[0];
        head.state = SC_DEATHLINK_REMOTE_EXPIRED;
        head.outcome = SC_DEATHLINK_OUTCOME_EXPIRED;
        const auto id = head.id;
        pop_head_locked();
        seen_add_locked(id);
    }
    ReleaseSRWLockExclusive(&state_lock);
}

// Only the admitted native frame advances policy. Queries just read telemetry.
void tick(const Calls& c) {
    const auto now = GetTickCount64();
    const auto player = c.player ? c.player(c.context) : 0;
    sc_deathlink_result out{};
    AcquireSRWLockExclusive(&state_lock);
    if (!shared_state.enabled) { ReleaseSRWLockExclusive(&state_lock); return; }

    bool advanced = false;
    RemoteEvent head = {};
    uint32_t step = 0;
    if (const auto* current = head_locked()) {
        head = *current;
        if (head.state == SC_DEATHLINK_REMOTE_RECEIVED || head.state == SC_DEATHLINK_REMOTE_WAITING_SAFE) step = 1;
        else if (head.state == SC_DEATHLINK_REMOTE_WAITING_PROTECTION_END) step = 2;
    }
    if (step) {
        const bool expired = now - head.received_at > max_lifetime_ms;
        const bool exhausted = head.attempts >= max_attempts;
        if (expired || exhausted) {
            shared_state.pending[0].outcome = expired ? SC_DEATHLINK_OUTCOME_EXPIRED : SC_DEATHLINK_OUTCOME_REJECTED;
            shared_state.pending[0].state = expired ? SC_DEATHLINK_REMOTE_EXPIRED : SC_DEATHLINK_REMOTE_FAILED;
            pop_head_locked();
            seen_add_locked(head.id);
            fill(shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID | SC_DEATHLINK_FLAG_MUTATED;
            out.outcome = expired ? SC_DEATHLINK_OUTCOME_EXPIRED : SC_DEATHLINK_OUTCOME_REJECTED;
            return;
        }
    }
    if (step && !player) {
        shared_state.pending[0].state = SC_DEATHLINK_REMOTE_WAITING_SAFE;
        fill(shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
        out.outcome = SC_DEATHLINK_OUTCOME_NO_PLAYER;
        return;
    }
    if (step && now < shared_state.next_apply_allowed_at) {
        fill(shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
        out.outcome = SC_DEATHLINK_OUTCOME_NOOP;
        return;
    }
    if (step && head.attempts > 0) {
        // Missing/unreadable protection never authorizes lethal retry.
        if (!c.protection_active || c.protection_active(c.context, player)) {
            shared_state.pending[0].state = SC_DEATHLINK_REMOTE_WAITING_PROTECTION_END;
            fill(shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
            out.outcome = SC_DEATHLINK_OUTCOME_NOOP;
            return;
        }
    }
    if (step) {
        shared_state.pending[0].state = SC_DEATHLINK_REMOTE_APPLYING;
        shared_state.pending[0].last_applied_at = now;
        ++shared_state.pending[0].attempts;
        head = shared_state.pending[0];
    }
    ReleaseSRWLockExclusive(&state_lock);

    if (!step) {
        AcquireSRWLockExclusive(&state_lock);
        fill(shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
        out.outcome = SC_DEATHLINK_OUTCOME_OK;
        return;
    }

    ApplicationOutcome result{};
    out.native_exception = c.apply_lethal ? c.apply_lethal(c.context, player, result) : 1;

    AcquireSRWLockExclusive(&state_lock);
    ++shared_state.operations;
    advanced = true;
    bool resolved = false;
    if (shared_state.pending_count && shared_state.pending[0].id == head.id) {
        auto& slot = shared_state.pending[0];
        slot.attempts = head.attempts;
        if (out.native_exception) {
            slot.state = SC_DEATHLINK_REMOTE_FAILED;
            slot.outcome = SC_DEATHLINK_OUTCOME_NATIVE_FAILED;
            pop_head_locked();
            seen_add_locked(head.id);
            resolved = true;
            out.outcome = SC_DEATHLINK_OUTCOME_NATIVE_FAILED;
            shared_state.next_apply_allowed_at = now + retry_settle_ms;
        } else if (result.true_death) {
            slot.state = SC_DEATHLINK_REMOTE_RESOLVED_DEATH;
            slot.flags = SC_DEATHLINK_FLAG_APPLIED | SC_DEATHLINK_FLAG_TRUE_DEATH;
            slot.protection = SC_DEATHLINK_PROTECTION_NONE;
            pop_head_locked();
            seen_add_locked(head.id);
            resolved = true;
            out.flags |= SC_DEATHLINK_FLAG_TRUE_DEATH;
            shared_state.next_apply_allowed_at = now + post_death_settle_ms;
            out.outcome = SC_DEATHLINK_OUTCOME_OK;
        } else if (!result.extra_life) {
            slot.state = SC_DEATHLINK_REMOTE_FAILED;
            slot.outcome = SC_DEATHLINK_OUTCOME_NATIVE_FAILED;
            pop_head_locked();
            seen_add_locked(head.id);
        } else {
            slot.protection = result.extra_life ? SC_DEATHLINK_PROTECTION_EXTRA_LIFE
                                                : SC_DEATHLINK_PROTECTION_OTHER;
            if (shared_state.mode == SC_DEATHLINK_MODE_HARDCORE) {
                slot.state = SC_DEATHLINK_REMOTE_WAITING_PROTECTION_END;
                shared_state.next_apply_allowed_at = now + retry_settle_ms;
                out.outcome = SC_DEATHLINK_OUTCOME_OK;
            } else {
                slot.state = SC_DEATHLINK_REMOTE_RESOLVED_PROTECTED;
                slot.flags = SC_DEATHLINK_FLAG_APPLIED | SC_DEATHLINK_FLAG_PROTECTED;
                pop_head_locked();
                seen_add_locked(head.id);
                resolved = true;
                shared_state.next_apply_allowed_at = now + retry_settle_ms;
                out.outcome = SC_DEATHLINK_OUTCOME_OK;
            }
            out.flags |= SC_DEATHLINK_FLAG_PROTECTED;
        }
    } else {
        out.outcome = SC_DEATHLINK_OUTCOME_NOOP;
    }
    fill(shared_state, out);
    ReleaseSRWLockExclusive(&state_lock);
    out.flags |= SC_DEATHLINK_FLAG_AFTER_VALID;
    if (!out.native_exception) out.flags |= SC_DEATHLINK_FLAG_APPLIED;
    if (advanced) out.flags |= SC_DEATHLINK_FLAG_ADVANCED;
    (void)resolved;
}

} // namespace sentinel::deathlink
