#include "special.h"
#include <cstring>
#include <algorithm>

namespace sentinel::special {
namespace {
constexpr uint64_t refill_request_lifetime_ms = 5000;
SRWLOCK state_lock = SRWLOCK_INIT;

struct State {
    uint32_t owns_crucible = 0, owns_hammer = 0, hammer_tier = 0, selected = 0;
    uint32_t refill_balance = 0, refill_flags = 0;
    uint32_t refill_request_state = SC_SPECIAL_REFILL_IDLE;
    uint64_t pending_request_id = 0, pending_at_ms = 0;
    uint64_t last_executed_id = 0, last_execution_id = 0;
    uint32_t last_executed = 0;
    uint32_t refill_sequence = 0;
    uint64_t request_serial = 0;
};
State shared_state{};
uint64_t total_operations = 0;
char bound_namespace[65]{};
uintptr_t last_bound_player = 0;
uint64_t last_bound_generation = 0, next_bind_at = 0;

void reset_state_locked(const char* id) {
    shared_state = {};
    total_operations = 0;
    last_bound_player = 0;
    last_bound_generation = 0; next_bind_at = 0;
    if (id) std::memcpy(bound_namespace, id, sizeof(bound_namespace));
    else std::memset(bound_namespace, 0, sizeof(bound_namespace));
}

void check_namespace(const char* id) {
    if (std::memcmp(bound_namespace, id, sizeof(bound_namespace)) != 0) {
        reset_state_locked(id);
    }
}

uint64_t next_request_id_locked() {
    ++shared_state.request_serial;
    const auto st = GetTickCount64();
    auto id = (st << 16) ^ (shared_state.request_serial * 0x9E3779B97F4A7C15ull);
    if (!id) id = 1;
    return id;
}

uint32_t tier_for(uint32_t native_hammer, uint32_t native_perks) {
    if (!native_hammer) return SC_SPECIAL_HAMMER_TIER_NONE;
    return native_perks >= 2 ? SC_SPECIAL_HAMMER_TIER_UPGRADED : SC_SPECIAL_HAMMER_TIER_BASE;
}

void adopt_native_locked(const SnapshotFacts& facts) {
    if (facts.known & SC_SPECIAL_KNOWN_CRUCIBLE)
        shared_state.owns_crucible |= (facts.native_crucible != 0);
    if (facts.known & SC_SPECIAL_KNOWN_HAMMER)
        shared_state.owns_hammer |= (facts.native_hammer != 0);
    if ((facts.known & SC_SPECIAL_KNOWN_HAMMER) && (facts.known & SC_SPECIAL_KNOWN_HAMMER_PERKS)) {
        const auto tier = tier_for(facts.native_hammer, facts.native_hammer_perks);
        shared_state.hammer_tier = std::max(shared_state.hammer_tier, tier);
    }
    if ((facts.known & SC_SPECIAL_KNOWN_SELECTION) &&
        facts.native_selected >= SC_SPECIAL_WEAPON_NONE && facts.native_selected <= SC_SPECIAL_WEAPON_HAMMER) {
        shared_state.selected = facts.native_selected;
    }
}

void fill_facts(const SnapshotFacts& before, const State& state, sc_special_result& out) {
    out.owns_crucible = state.owns_crucible;
    out.owns_hammer = state.owns_hammer;
    out.hammer_tier = state.hammer_tier;
    out.selected = state.selected;
    out.native_crucible = before.native_crucible;
    out.native_hammer = before.native_hammer;
    out.native_hammer_perks = before.native_hammer_perks;
    out.native_selected = before.native_selected;
    if (before.selection_policy) out.flags |= SC_SPECIAL_FLAG_SELECTION_POLICY;
    out.native_state_known = before.known;
    if (before.known & SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE) {
        out.crucible_charge = before.crucible_charge;
        out.crucible_charge_max = before.crucible_charge_max;
    } else {
        out.crucible_charge = UINT32_MAX;
        out.crucible_charge_max = UINT32_MAX;
    }
    out.refill_balance = state.refill_balance;
    out.refill_flags = state.refill_flags;
    out.refill_request_id = state.pending_request_id ? state.pending_request_id : state.last_executed_id;
    out.refill_request_state = state.refill_request_state;
    out.refill_executed = state.last_executed;
    out.refill_execution_id = state.last_execution_id;
    out.refill_sequence = state.refill_sequence;
    out.operations_applied = total_operations;
}

bool expire_pending_locked(uint64_t now_ms) {
    if (shared_state.refill_request_state != SC_SPECIAL_REFILL_PENDING) return false;
    if (now_ms - shared_state.pending_at_ms <= refill_request_lifetime_ms) return false;
    shared_state.refill_request_state = SC_SPECIAL_REFILL_EXPIRED;
    shared_state.pending_request_id = 0;
    return true;
}
} // namespace

// The acquisition may intrinsically switch the weapon in hands. A changed
// declaration is only restored when the prior item was freshly validated
// against the current inventory; otherwise the caller fails closed instead of
// fabricating or replaying a prior selection.
AcquisitionRestore acquisition_restore(uintptr_t before_decl, uintptr_t before_item, uintptr_t after_decl) {
    if (!before_decl || after_decl == before_decl) return AcquisitionRestore::none;
    return before_item ? AcquisitionRestore::equip_prior : AcquisitionRestore::fail_closed;
}

bool valid(const sc_special_request& r) {
    if (r.namespace_id[64] || r.kind > SC_SPECIAL_REFILL_EXECUTE) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = r.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    if (r.own_crucible > 1 || r.own_hammer > 1) return false;
    if (r.hammer_tier > SC_SPECIAL_HAMMER_TIER_UPGRADED) return false;
    if (r.selected > SC_SPECIAL_WEAPON_HAMMER) return false;
    if (r.refill_flags & ~(SC_SPECIAL_REFILL_CONNECTED | SC_SPECIAL_REFILL_AUTHORITATIVE | SC_SPECIAL_REFILL_BALANCE_KNOWN)) return false;
    if (r.refill_authorize & ~SC_SPECIAL_REFILL_AUTHORIZED) return false;
    if (r.refill_balance > 1000) return false;
    if (r.reserved0) return false;
    const bool no_ownership = !r.own_crucible && !r.own_hammer && !r.hammer_tier && !r.selected;
    const bool no_refill = !r.refill_balance && !r.refill_flags && !r.refill_request_id && !r.refill_authorize;
    switch (r.kind) {
    case SC_SPECIAL_OBSERVE:
    case SC_SPECIAL_REFILL_STATUS:
        return no_ownership && no_refill;
    case SC_SPECIAL_ENSURE_OWNERSHIP:
        return no_refill;
    case SC_SPECIAL_SELECT:
        return !r.own_crucible && !r.own_hammer && !r.hammer_tier && no_refill && r.selected != SC_SPECIAL_WEAPON_NONE;
    case SC_SPECIAL_REFILL_PUBLISH:
        return no_ownership && !r.refill_request_id && !r.refill_authorize;
    case SC_SPECIAL_REFILL_TAKE:
        return no_ownership && !r.refill_balance && !r.refill_flags && !r.refill_request_id && !r.refill_authorize;
    case SC_SPECIAL_REFILL_EXECUTE:
        return no_ownership && !r.refill_balance && !r.refill_flags && r.refill_request_id != 0;
    default:
        return false;
    }
}

bool same(const sc_special_request& a, const sc_special_request& b) {
    return a.kind == b.kind &&
           a.own_crucible == b.own_crucible && a.own_hammer == b.own_hammer &&
           a.hammer_tier == b.hammer_tier && a.selected == b.selected &&
           a.refill_balance == b.refill_balance && a.refill_flags == b.refill_flags &&
           a.refill_request_id == b.refill_request_id && a.refill_authorize == b.refill_authorize &&
           !std::memcmp(a.namespace_id, b.namespace_id, sizeof(a.namespace_id));
}

sc_special_result initial(const sc_special_request& r) {
    sc_special_result out{};
    out.size = sizeof(out);
    out.abi_version = SC_SPECIAL_ABI_VERSION;
    out.kind = r.kind;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id));
    out.native_state_known = 0;
    out.crucible_charge = UINT32_MAX;
    out.crucible_charge_max = UINT32_MAX;
    out.native_selected = SC_SPECIAL_WEAPON_NONE;
    return out;
}

void reset_session(const char* namespace_id) {
    AcquireSRWLockExclusive(&state_lock);
    reset_state_locked(namespace_id);
    ReleaseSRWLockExclusive(&state_lock);
}

bool create_refill_request(uint64_t now_ms) {
    AcquireSRWLockExclusive(&state_lock);
    expire_pending_locked(now_ms);
    if (shared_state.refill_request_state == SC_SPECIAL_REFILL_PENDING) {
        ReleaseSRWLockExclusive(&state_lock);
        return false;
    }
    shared_state.pending_request_id = next_request_id_locked();
    shared_state.pending_at_ms = now_ms;
    shared_state.refill_request_state = SC_SPECIAL_REFILL_PENDING;
    ReleaseSRWLockExclusive(&state_lock);
    return true;
}

void bind_run_state_if_needed(uintptr_t player, uint64_t generation) {
    if (!player) return;
    const auto now = GetTickCount64();
    AcquireSRWLockExclusive(&state_lock);
    if ((player == last_bound_player && generation == last_bound_generation) || now < next_bind_at) {
        ReleaseSRWLockExclusive(&state_lock); return;
    }
    next_bind_at = now + 500;
    const auto desired = shared_state;
    ReleaseSRWLockExclusive(&state_lock);
    // Selection/consumed resources belong to native save reconstruction.
    if (!desired.owns_crucible && !desired.owns_hammer) return;
    if (calls.ensure(calls.context, player, desired.owns_crucible, desired.owns_hammer, desired.hammer_tier)) return;
    SnapshotFacts after{};
    if (!calls.read(calls.context, player, after) ||
        (desired.owns_crucible && (!(after.known & SC_SPECIAL_KNOWN_CRUCIBLE) || !after.native_crucible)) ||
        (desired.owns_hammer && (!(after.known & SC_SPECIAL_KNOWN_HAMMER) || !after.native_hammer)) ||
        (desired.hammer_tier >= SC_SPECIAL_HAMMER_TIER_UPGRADED &&
         (!(after.known & SC_SPECIAL_KNOWN_HAMMER_PERKS) || after.native_hammer_perks < 2))) return;
    AcquireSRWLockExclusive(&state_lock);
    last_bound_player = player;
    last_bound_generation = generation;
    ReleaseSRWLockExclusive(&state_lock);
}

void execute(const sc_special_request& r, sc_special_result& out, const Calls& c) {
    const auto player = c.player(c.context);
    if (!player) { out.outcome = SC_SPECIAL_OUTCOME_NO_PLAYER; return; }

    SnapshotFacts before{};
    if (!c.read(c.context, player, before)) {
        out.outcome = SC_SPECIAL_OUTCOME_READ_FAILED; return;
    }
    out.flags |= SC_SPECIAL_FLAG_BEFORE_VALID;

    AcquireSRWLockExclusive(&state_lock);
    check_namespace(r.namespace_id);
    adopt_native_locked(before);

    if (r.kind == SC_SPECIAL_OBSERVE) {
        fill_facts(before, shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_SPECIAL_FLAG_AFTER_VALID | SC_SPECIAL_FLAG_OWNERSHIP_CUMULATIVE;
        out.outcome = SC_SPECIAL_OUTCOME_OK;
        return;
    }

    if (r.kind == SC_SPECIAL_ENSURE_OWNERSHIP) {
        const uint32_t desired_crucible = shared_state.owns_crucible | (r.own_crucible ? 1u : 0u);
        const uint32_t desired_hammer = shared_state.owns_hammer | (r.own_hammer ? 1u : 0u);
        const uint32_t desired_tier = std::max(shared_state.hammer_tier, r.hammer_tier);
        shared_state.owns_crucible = desired_crucible;
        shared_state.owns_hammer = desired_hammer;
        shared_state.hammer_tier = desired_tier;

        const bool native_satisfied =
            (!desired_crucible || ((before.known & SC_SPECIAL_KNOWN_CRUCIBLE) && before.native_crucible)) &&
            (!desired_hammer || ((before.known & SC_SPECIAL_KNOWN_HAMMER) && before.native_hammer &&
             (desired_tier < SC_SPECIAL_HAMMER_TIER_UPGRADED ||
              ((before.known & SC_SPECIAL_KNOWN_HAMMER_PERKS) && before.native_hammer_perks >= 2))));
        if (native_satisfied) {
            fill_facts(before, shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_SPECIAL_FLAG_AFTER_VALID | SC_SPECIAL_FLAG_OWNERSHIP_CUMULATIVE |
                SC_SPECIAL_FLAG_RESOURCE_PRESERVED;
            if (before.known & SC_SPECIAL_KNOWN_SELECTION)
                out.flags |= SC_SPECIAL_FLAG_SELECTION_PRESERVED;
            out.outcome = SC_SPECIAL_OUTCOME_NOOP;
            return;
        }

        ReleaseSRWLockExclusive(&state_lock);
        out.native_exception = c.ensure(c.context, player, desired_crucible, desired_hammer, desired_tier);

        SnapshotFacts after{};
        const bool read_ok = c.read(c.context, player, after);
        if (read_ok) {
            out.flags |= SC_SPECIAL_FLAG_AFTER_VALID;
            AcquireSRWLockExclusive(&state_lock);
            adopt_native_locked(after);
            fill_facts(after, shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            if ((before.known & after.known & SC_SPECIAL_KNOWN_SELECTION) &&
                after.native_selected == before.native_selected) out.flags |= SC_SPECIAL_FLAG_SELECTION_PRESERVED;
            if (before.held_weapon_decl && before.held_weapon_decl == after.held_weapon_decl)
                out.flags |= SC_SPECIAL_FLAG_HELD_WEAPON_PRESERVED;
            if ((before.known & after.known & SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE) &&
                after.crucible_charge == before.crucible_charge &&
                after.crucible_charge_max == before.crucible_charge_max)
                out.flags |= SC_SPECIAL_FLAG_RESOURCE_PRESERVED;
        }
        out.flags |= SC_SPECIAL_FLAG_OWNERSHIP_CUMULATIVE;
        if (out.native_exception == ERROR_NOT_SUPPORTED) {
            out.outcome = SC_SPECIAL_OUTCOME_UNAVAILABLE; return;
        }
        if (read_ok && (after.native_crucible != before.native_crucible ||
            after.native_hammer != before.native_hammer || after.native_hammer_perks != before.native_hammer_perks))
            out.flags |= SC_SPECIAL_FLAG_MUTATED;
        if (out.native_exception || !read_ok ||
            (desired_crucible && (!(after.known & SC_SPECIAL_KNOWN_CRUCIBLE) || !after.native_crucible)) ||
            (desired_hammer && (!(after.known & SC_SPECIAL_KNOWN_HAMMER) || !after.native_hammer)) ||
            (desired_hammer && desired_tier >= SC_SPECIAL_HAMMER_TIER_UPGRADED &&
             (!(after.known & SC_SPECIAL_KNOWN_HAMMER_PERKS) || after.native_hammer_perks < 2))) {
            out.outcome = SC_SPECIAL_OUTCOME_NATIVE_FAILED; return;
        }
        out.outcome = SC_SPECIAL_OUTCOME_OK;

        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    if (r.kind == SC_SPECIAL_SELECT) {
        const bool owned = (r.selected == SC_SPECIAL_WEAPON_CRUCIBLE &&
                            (before.known & SC_SPECIAL_KNOWN_CRUCIBLE) && before.native_crucible) ||
                           (r.selected == SC_SPECIAL_WEAPON_HAMMER &&
                            (before.known & SC_SPECIAL_KNOWN_HAMMER) && before.native_hammer);
        if (!owned) {
            ReleaseSRWLockExclusive(&state_lock);
            out.outcome = SC_SPECIAL_OUTCOME_REJECTED;
            return;
        }
        const bool already = (before.known & SC_SPECIAL_KNOWN_SELECTION) && before.native_selected == r.selected;
        ReleaseSRWLockExclusive(&state_lock);

        out.native_exception = c.select(c.context, player, r.selected);

        SnapshotFacts after{};
        const bool read_ok = c.read(c.context, player, after);
        if (read_ok) {
            out.flags |= SC_SPECIAL_FLAG_AFTER_VALID;
            AcquireSRWLockExclusive(&state_lock);
            adopt_native_locked(after);
            // The read callback reports the applied route and its authority.
            fill_facts(after, shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
        }
        if (read_ok && (after.known & SC_SPECIAL_KNOWN_SELECTION) &&
            after.native_selected == r.selected && !already) out.flags |= SC_SPECIAL_FLAG_MUTATED;
        if (out.native_exception || !read_ok || !(after.known & SC_SPECIAL_KNOWN_SELECTION) ||
            after.native_selected != r.selected) { out.outcome = SC_SPECIAL_OUTCOME_NATIVE_FAILED; return; }
        out.outcome = already ? SC_SPECIAL_OUTCOME_NOOP : SC_SPECIAL_OUTCOME_OK;

        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    if (r.kind == SC_SPECIAL_REFILL_PUBLISH) {
        const bool changed = shared_state.refill_balance != r.refill_balance ||
                             shared_state.refill_flags != r.refill_flags;
        shared_state.refill_balance = r.refill_balance;
        shared_state.refill_flags = r.refill_flags;
        const auto balance = shared_state.refill_balance;
        const auto flags = shared_state.refill_flags;
        fill_facts(before, shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);

        if (changed && c.present && c.present(c.context, player, balance, flags, 0))
            out.flags |= SC_SPECIAL_FLAG_HUD_PRESENTED;
        out.flags |= SC_SPECIAL_FLAG_AFTER_VALID;
        out.outcome = changed ? SC_SPECIAL_OUTCOME_OK : SC_SPECIAL_OUTCOME_NOOP;
        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    if (r.kind == SC_SPECIAL_REFILL_STATUS || r.kind == SC_SPECIAL_REFILL_TAKE) {
        expire_pending_locked(GetTickCount64());
        fill_facts(before, shared_state, out);
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_SPECIAL_FLAG_AFTER_VALID;
        if (out.refill_request_state == SC_SPECIAL_REFILL_EXPIRED) out.flags |= SC_SPECIAL_FLAG_REFILL_EXPIRED;
        out.outcome = SC_SPECIAL_OUTCOME_OK;
        return;
    }

    if (r.kind == SC_SPECIAL_REFILL_EXECUTE) {
        const auto now = GetTickCount64();
        expire_pending_locked(now);
        const bool matches_pending = shared_state.refill_request_state == SC_SPECIAL_REFILL_PENDING &&
                                     shared_state.pending_request_id == r.refill_request_id;
        const bool matches_executed = shared_state.last_executed_id == r.refill_request_id &&
                                      shared_state.last_executed != 0;
        if (matches_executed) {
            // Repeated ACK/retry never performs the refill twice.
            fill_facts(before, shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_SPECIAL_FLAG_AFTER_VALID | SC_SPECIAL_FLAG_REFILL_EXECUTED |
                SC_SPECIAL_FLAG_REFILL_AUTHORIZED;
            out.outcome = SC_SPECIAL_OUTCOME_NOOP;
            return;
        }
        if (!matches_pending || !(r.refill_authorize & SC_SPECIAL_REFILL_AUTHORIZED)) {
            if (shared_state.refill_request_state == SC_SPECIAL_REFILL_PENDING &&
                shared_state.pending_request_id == r.refill_request_id) {
                shared_state.refill_request_state = SC_SPECIAL_REFILL_REJECTED;
                shared_state.pending_request_id = 0;
            }
            fill_facts(before, shared_state, out);
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_SPECIAL_FLAG_AFTER_VALID;
            out.outcome = SC_SPECIAL_OUTCOME_REJECTED;
            return;
        }
        // One authorization owns exactly one native refill. The pending identity
        // is retired before the native call so a concurrent retry cannot match.
        shared_state.refill_request_state = SC_SPECIAL_REFILL_EXECUTED;
        shared_state.pending_request_id = 0;
        shared_state.last_executed_id = r.refill_request_id;
        shared_state.last_executed = 0;
        ReleaseSRWLockExclusive(&state_lock);

        out.flags |= SC_SPECIAL_FLAG_REFILL_AUTHORIZED;
        out.native_exception = c.refill(c.context, player);
        out.flags |= SC_SPECIAL_FLAG_REFILL_UNVERIFIED; // No capacity readback; partial effects are possible.

        SnapshotFacts after{};
        const bool read_ok = c.read(c.context, player, after);
        if (read_ok) out.flags |= SC_SPECIAL_FLAG_AFTER_VALID;

        AcquireSRWLockExclusive(&state_lock);
        if (out.native_exception) {
            // A failed native execution must not leave a retryable charge use.
            shared_state.last_executed = 0;
            shared_state.refill_request_state = SC_SPECIAL_REFILL_REJECTED;
        } else {
            shared_state.last_executed = 1;
            ++shared_state.refill_sequence;
            shared_state.last_execution_id = r.refill_request_id;
        }
        fill_facts(after, shared_state, out);
        const auto balance = shared_state.refill_balance;
        const auto flags = shared_state.refill_flags;
        ReleaseSRWLockExclusive(&state_lock);

        if (out.native_exception || !read_ok) { out.outcome = SC_SPECIAL_OUTCOME_NATIVE_FAILED; return; }
        out.flags |= SC_SPECIAL_FLAG_REFILL_EXECUTED;
        if (c.present && c.present(c.context, player, balance, flags, 1))
            out.flags |= SC_SPECIAL_FLAG_HUD_PRESENTED;
        out.outcome = SC_SPECIAL_OUTCOME_OK;

        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    ReleaseSRWLockExclusive(&state_lock);
    out.outcome = SC_SPECIAL_OUTCOME_REJECTED;
}

sc_special_result toggle_local(const char* namespace_id, const Calls& c) {
    sc_special_request request{};
    std::memcpy(request.namespace_id, namespace_id, sizeof(request.namespace_id));
    request.kind = SC_SPECIAL_OBSERVE;
    auto observed = initial(request);
    execute(request, observed, c);
    const auto known = SC_SPECIAL_KNOWN_CRUCIBLE | SC_SPECIAL_KNOWN_HAMMER;
    if (observed.outcome != SC_SPECIAL_OUTCOME_OK || (observed.native_state_known & known) != known)
        return observed;
    if (!observed.native_crucible && !observed.native_hammer) return observed;
    request.kind = SC_SPECIAL_SELECT;
    request.selected = observed.native_crucible &&
        (!observed.native_hammer || observed.selected != SC_SPECIAL_WEAPON_CRUCIBLE)
        ? SC_SPECIAL_WEAPON_CRUCIBLE : SC_SPECIAL_WEAPON_HAMMER;
    auto result = initial(request);
    execute(request, result, c);
    return result;
}

} // namespace sentinel::special
