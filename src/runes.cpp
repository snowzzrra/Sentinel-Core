#include "runes.h"
#include <cstring>
#include <algorithm>

namespace sentinel::runes {
namespace {
SRWLOCK state_lock = SRWLOCK_INIT;
SnapshotFacts shared_state{};
uint64_t total_operations = 0;
char bound_namespace[65]{};
uintptr_t last_bound_player = 0;
uint64_t last_bound_generation = 0, next_bind_at = 0;

void reset_state_locked(const char* id) {
    shared_state = {};
    shared_state.selected_slots[0] = -1;
    shared_state.selected_slots[1] = -1;
    shared_state.selected_slots[2] = -1;
    shared_state.selected_support = -1;
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

void record_observed_locked(const SnapshotFacts& facts) {
    shared_state.owned_normal |= (facts.owned_normal & SC_RUNES_ALL_NORMAL);
    shared_state.owned_support |= (facts.owned_support & SC_RUNES_ALL_SUPPORT);
    for (int i = 0; i < 3; ++i) {
        if (facts.selected_slots[i] >= -1 && facts.selected_slots[i] < 9) {
            shared_state.selected_slots[i] = facts.selected_slots[i];
        }
    }
    if (facts.selected_support >= -1 && facts.selected_support < 3) {
        shared_state.selected_support = facts.selected_support;
    }
    shared_state.unlocked_slots = facts.unlocked_slots;
    shared_state.health_tier = facts.health_tier;
    shared_state.armor_tier = facts.armor_tier;
    shared_state.ammo_tier = facts.ammo_tier;
    shared_state.derived_pairs = facts.derived_pairs;
}
} // namespace

uint8_t compute_derived_crystal_pairs(uint8_t health, uint8_t armor, uint8_t ammo) {
    uint8_t mask = 0;
    // Family 0 (Quickdraw Belch): health >= 1 && armor >= 1
    if (health >= 1 && armor >= 1) mask |= SC_CRYSTAL_PAIR_QUICKDRAW_BELCH;
    // Family 1 (Loot Magnet): armor >= 2 && ammo >= 3
    if (armor >= 2 && ammo >= 3) mask |= SC_CRYSTAL_PAIR_LOOT_MAGNET;
    // Family 2 (Napalm Belch): health >= 2 && ammo >= 1
    if (health >= 2 && ammo >= 1) mask |= SC_CRYSTAL_PAIR_NAPALM_BELCH;
    // Family 3 (Health for Blood): health >= 4 && armor >= 3
    if (health >= 4 && armor >= 3) mask |= SC_CRYSTAL_PAIR_HEALTH_FOR_BLOOD;
    // Family 4 (Belch Armor Boost): health >= 3 && ammo >= 2
    if (health >= 3 && ammo >= 2) mask |= SC_CRYSTAL_PAIR_BELCH_ARMOR_BOOST;
    // Family 5 (Armor for Blood): armor >= 4 && ammo >= 4
    if (armor >= 4 && ammo >= 4) mask |= SC_CRYSTAL_PAIR_ARMOR_FOR_BLOOD;
    return mask;
}

bool valid(const sc_runes_request& r) {
    if (r.namespace_id[64] || r.kind > SC_RUNES_SELECT_SUPPORT) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = r.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    if (r.normal_runes & ~SC_RUNES_ALL_NORMAL) return false;
    if (r.support_runes & ~SC_RUNES_ALL_SUPPORT) return false;
    if (r.kind == SC_RUNES_OBSERVE) {
        return !r.normal_runes && !r.support_runes && r.select_slot == 0 && r.select_rune == 0;
    }
    if (r.kind == SC_RUNES_SELECT_NORMAL) {
        if (r.select_slot >= 3) return false;
        if (r.select_rune < -1 || r.select_rune >= 9) return false;
    }
    if (r.kind == SC_RUNES_SELECT_SUPPORT) {
        if (r.select_rune < -1 || r.select_rune >= 3) return false;
    }
    return true;
}

bool same(const sc_runes_request& a, const sc_runes_request& b) {
    return a.kind == b.kind &&
           a.normal_runes == b.normal_runes &&
           a.support_runes == b.support_runes &&
           a.select_slot == b.select_slot &&
           a.select_rune == b.select_rune &&
           !std::memcmp(a.namespace_id, b.namespace_id, sizeof(a.namespace_id));
}

sc_runes_result initial(const sc_runes_request& r) {
    sc_runes_result out{};
    out.size = sizeof(out);
    out.abi_version = SC_RUNES_ABI_VERSION;
    out.kind = r.kind;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id));
    out.selected_slots_before[0] = -1;
    out.selected_slots_before[1] = -1;
    out.selected_slots_before[2] = -1;
    out.selected_slots_after[0] = -1;
    out.selected_slots_after[1] = -1;
    out.selected_slots_after[2] = -1;
    out.selected_support_before = -1;
    out.selected_support_after = -1;
    return out;
}

void reset_session(const char* namespace_id) {
    AcquireSRWLockExclusive(&state_lock);
    reset_state_locked(namespace_id);
    ReleaseSRWLockExclusive(&state_lock);
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
    SnapshotFacts before{}, after{};
    if (!calls.read(calls.context, player, before)) return;
    if (calls.ensure_normal_runes(calls.context, player, desired.owned_normal & ~before.owned_normal) ||
        calls.ensure_support_runes(calls.context, player, desired.owned_support & ~before.owned_support) ||
        !calls.read(calls.context, player, after) ||
        (after.owned_normal & desired.owned_normal) != desired.owned_normal ||
        (after.owned_support & desired.owned_support) != desired.owned_support) return;
    // Native save owns selections. Never replay stale cached choices or pair(0).
    AcquireSRWLockExclusive(&state_lock);
    last_bound_player = player;
    last_bound_generation = generation;
    ReleaseSRWLockExclusive(&state_lock);
}

void execute(const sc_runes_request& r, sc_runes_result& out, const Calls& c) {
    const auto player = c.player(c.context);
    if (!player) { out.outcome = SC_RUNES_OUTCOME_NO_PLAYER; return; }

    SnapshotFacts before{};
    if (!c.read(c.context, player, before)) {
        out.outcome = SC_RUNES_OUTCOME_READ_FAILED; return;
    }
    out.flags |= SC_RUNES_FLAG_BEFORE_VALID;
    out.owned_normal_before = before.owned_normal;
    out.owned_support_before = before.owned_support;
    for (int i = 0; i < 3; ++i) out.selected_slots_before[i] = before.selected_slots[i];
    out.selected_support_before = before.selected_support;
    out.unlocked_slots_before = before.unlocked_slots;
    out.derived_pairs_before = before.derived_pairs;

    AcquireSRWLockExclusive(&state_lock);
    check_namespace(r.namespace_id);
    record_observed_locked(before);

    if (r.kind == SC_RUNES_OBSERVE) {
        out.owned_normal_after = before.owned_normal;
        out.owned_support_after = before.owned_support;
        for (int i = 0; i < 3; ++i) out.selected_slots_after[i] = before.selected_slots[i];
        out.selected_support_after = before.selected_support;
        out.unlocked_slots_after = before.unlocked_slots;
        out.derived_pairs_after = before.derived_pairs;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_RUNES_FLAG_AFTER_VALID;
        out.outcome = SC_RUNES_OUTCOME_OK;
        return;
    }

    if (r.kind == SC_RUNES_ENSURE_NORMAL) {
        shared_state.owned_normal |= (r.normal_runes & SC_RUNES_ALL_NORMAL);
        const uint32_t needed = shared_state.owned_normal;
        const uint32_t delta = needed & ~before.owned_normal;

        if (!delta) {
            // Already registered; idempotent completion without altering slots
            out.owned_normal_after = before.owned_normal;
            out.owned_support_after = before.owned_support;
            for (int i = 0; i < 3; ++i) out.selected_slots_after[i] = before.selected_slots[i];
            out.selected_support_after = before.selected_support;
            out.unlocked_slots_after = before.unlocked_slots;
            out.derived_pairs_after = before.derived_pairs;
            out.operations_applied = total_operations;
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_RUNES_FLAG_AFTER_VALID | SC_RUNES_FLAG_SELECTION_PRESERVED;
            out.outcome = SC_RUNES_OUTCOME_OK;
            return;
        }

        ReleaseSRWLockExclusive(&state_lock);
        out.native_exception = c.ensure_normal_runes(c.context, player, delta);

        SnapshotFacts after{};
        const bool read_ok = c.read(c.context, player, after);
        if (read_ok) {
            out.flags |= SC_RUNES_FLAG_AFTER_VALID;
            out.owned_normal_after = after.owned_normal;
            out.owned_support_after = after.owned_support;
            for (int i = 0; i < 3; ++i) out.selected_slots_after[i] = after.selected_slots[i];
            out.selected_support_after = after.selected_support;
            out.unlocked_slots_after = after.unlocked_slots;
            out.derived_pairs_after = after.derived_pairs;
            if (after.selected_slots[0] == before.selected_slots[0] &&
                after.selected_slots[1] == before.selected_slots[1] &&
                after.selected_slots[2] == before.selected_slots[2]) {
                out.flags |= SC_RUNES_FLAG_SELECTION_PRESERVED;
            }
        }
        if (out.native_exception || !read_ok || (after.owned_normal & needed) != needed) {
            out.outcome = SC_RUNES_OUTCOME_NATIVE_FAILED; return;
        }
        if (c.refresh && !c.refresh(c.context, player)) {
            out.outcome = SC_RUNES_OUTCOME_REFRESH_FAILED; return;
        }
        out.flags |= SC_RUNES_FLAG_MUTATED;
        out.outcome = SC_RUNES_OUTCOME_OK;

        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    if (r.kind == SC_RUNES_ENSURE_SUPPORT) {
        shared_state.owned_support |= (r.support_runes & SC_RUNES_ALL_SUPPORT);
        const uint32_t needed = shared_state.owned_support;
        const uint32_t delta = needed & ~before.owned_support;

        if (!delta) {
            // Already registered; idempotent completion without selecting
            out.owned_normal_after = before.owned_normal;
            out.owned_support_after = before.owned_support;
            for (int i = 0; i < 3; ++i) out.selected_slots_after[i] = before.selected_slots[i];
            out.selected_support_after = before.selected_support;
            out.unlocked_slots_after = before.unlocked_slots;
            out.derived_pairs_after = before.derived_pairs;
            out.operations_applied = total_operations;
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_RUNES_FLAG_AFTER_VALID | SC_RUNES_FLAG_SELECTION_PRESERVED;
            out.outcome = SC_RUNES_OUTCOME_OK;
            return;
        }

        ReleaseSRWLockExclusive(&state_lock);
        out.native_exception = c.ensure_support_runes(c.context, player, delta);

        SnapshotFacts after{};
        const bool read_ok = c.read(c.context, player, after);
        if (read_ok) {
            out.flags |= SC_RUNES_FLAG_AFTER_VALID;
            out.owned_normal_after = after.owned_normal;
            out.owned_support_after = after.owned_support;
            for (int i = 0; i < 3; ++i) out.selected_slots_after[i] = after.selected_slots[i];
            out.selected_support_after = after.selected_support;
            out.unlocked_slots_after = after.unlocked_slots;
            out.derived_pairs_after = after.derived_pairs;
            if (after.selected_support == before.selected_support) {
                out.flags |= SC_RUNES_FLAG_SELECTION_PRESERVED;
            }
        }
        if (out.native_exception || !read_ok || (after.owned_support & needed) != needed) {
            out.outcome = SC_RUNES_OUTCOME_NATIVE_FAILED; return;
        }
        if (c.refresh && !c.refresh(c.context, player)) {
            out.outcome = SC_RUNES_OUTCOME_REFRESH_FAILED; return;
        }
        out.flags |= SC_RUNES_FLAG_MUTATED;
        out.outcome = SC_RUNES_OUTCOME_OK;

        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    if (r.kind == SC_RUNES_SELECT_NORMAL) {
        const auto slot = r.select_slot;
        const auto rune = r.select_rune;
        if (slot >= 3) {
            ReleaseSRWLockExclusive(&state_lock);
            out.outcome = SC_RUNES_OUTCOME_REJECTED; return;
        }

        ReleaseSRWLockExclusive(&state_lock);
        out.native_exception = c.select_normal_rune(c.context, player, slot, rune);

        SnapshotFacts after{};
        const bool read_ok = c.read(c.context, player, after);
        if (read_ok) {
            out.flags |= SC_RUNES_FLAG_AFTER_VALID;
            out.owned_normal_after = after.owned_normal;
            out.owned_support_after = after.owned_support;
            for (int i = 0; i < 3; ++i) out.selected_slots_after[i] = after.selected_slots[i];
            out.selected_support_after = after.selected_support;
            out.unlocked_slots_after = after.unlocked_slots;
            out.derived_pairs_after = after.derived_pairs;
        }
        if (out.native_exception || !read_ok || after.selected_slots[slot] != rune) {
            out.outcome = SC_RUNES_OUTCOME_NATIVE_FAILED; return;
        }
        if (c.refresh && !c.refresh(c.context, player)) {
            out.outcome = SC_RUNES_OUTCOME_REFRESH_FAILED; return;
        }
        out.flags |= SC_RUNES_FLAG_MUTATED;
        out.outcome = SC_RUNES_OUTCOME_OK;

        AcquireSRWLockExclusive(&state_lock);
        if (rune >= 0 && rune < 9) shared_state.selected_slots[slot] = rune;
        else shared_state.selected_slots[slot] = -1;
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    if (r.kind == SC_RUNES_SELECT_SUPPORT) {
        const auto rune = r.select_rune;

        ReleaseSRWLockExclusive(&state_lock);
        out.native_exception = c.select_support_rune(c.context, player, rune);

        SnapshotFacts after{};
        const bool read_ok = c.read(c.context, player, after);
        if (read_ok) {
            out.flags |= SC_RUNES_FLAG_AFTER_VALID;
            out.owned_normal_after = after.owned_normal;
            out.owned_support_after = after.owned_support;
            for (int i = 0; i < 3; ++i) out.selected_slots_after[i] = after.selected_slots[i];
            out.selected_support_after = after.selected_support;
            out.unlocked_slots_after = after.unlocked_slots;
            out.derived_pairs_after = after.derived_pairs;
        }
        if (out.native_exception || !read_ok || after.selected_support != rune) {
            out.outcome = SC_RUNES_OUTCOME_NATIVE_FAILED; return;
        }
        if (c.refresh && !c.refresh(c.context, player)) {
            out.outcome = SC_RUNES_OUTCOME_REFRESH_FAILED; return;
        }
        out.flags |= SC_RUNES_FLAG_MUTATED;
        out.outcome = SC_RUNES_OUTCOME_OK;

        AcquireSRWLockExclusive(&state_lock);
        if (rune >= 0 && rune < 3) shared_state.selected_support = rune;
        else shared_state.selected_support = -1;
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    ReleaseSRWLockExclusive(&state_lock);
    out.outcome = SC_RUNES_OUTCOME_REJECTED;
}

} // namespace sentinel::runes
