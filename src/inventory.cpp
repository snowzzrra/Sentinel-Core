#include "inventory.h"
#include <cstring>
#include <algorithm>

namespace sentinel::inventory {
namespace {
SRWLOCK state_lock = SRWLOCK_INIT;
SnapshotFacts shared_state{};
uint64_t total_operations = 0;
char bound_namespace[65]{};
uintptr_t last_bound_player = 0;

void reset_state_locked(const char* id) {
    shared_state = {};
    total_operations = 0;
    last_bound_player = 0;
    if (id) std::memcpy(bound_namespace, id, sizeof(bound_namespace));
    else std::memset(bound_namespace, 0, sizeof(bound_namespace));
}

void check_namespace(const char* id) {
    if (std::memcmp(bound_namespace, id, sizeof(bound_namespace)) != 0) {
        reset_state_locked(id);
    }
}

void record_observed_locked(const SnapshotFacts& facts) {
    shared_state.weapons |= (facts.weapons & SC_INV_ALL_WEAPONS);
    shared_state.equipment |= (facts.equipment & SC_INV_ALL_EQUIPMENT);
    shared_state.special_weapons |= (facts.special_weapons & SC_INV_ALL_SPECIAL);
    shared_state.persistent_upgrades |= (facts.persistent_upgrades & SC_INV_ALL_UPGRADES);
    if (facts.health_tier <= SC_INVENTORY_MAX_CAPACITY_TIER)
        shared_state.health_tier = std::max(shared_state.health_tier, facts.health_tier);
    if (facts.armor_tier <= SC_INVENTORY_MAX_CAPACITY_TIER)
        shared_state.armor_tier = std::max(shared_state.armor_tier, facts.armor_tier);
    if (facts.ammo_tier <= SC_INVENTORY_MAX_CAPACITY_TIER)
        shared_state.ammo_tier = std::max(shared_state.ammo_tier, facts.ammo_tier);
}

void update_items_locked(uint32_t weapons, uint32_t equipment, uint32_t special, uint32_t upgrades) {
    shared_state.weapons |= (weapons & SC_INV_ALL_WEAPONS);
    shared_state.equipment |= (equipment & SC_INV_ALL_EQUIPMENT);
    shared_state.special_weapons |= (special & SC_INV_ALL_SPECIAL);
    shared_state.persistent_upgrades |= (upgrades & SC_INV_ALL_UPGRADES);
}

void update_capacity_locked(uint8_t health, uint8_t armor, uint8_t ammo) {
    if (health <= SC_INVENTORY_MAX_CAPACITY_TIER)
        shared_state.health_tier = std::max(shared_state.health_tier, health);
    if (armor <= SC_INVENTORY_MAX_CAPACITY_TIER)
        shared_state.armor_tier = std::max(shared_state.armor_tier, armor);
    if (ammo <= SC_INVENTORY_MAX_CAPACITY_TIER)
        shared_state.ammo_tier = std::max(shared_state.ammo_tier, ammo);
}
}

bool valid(const sc_inventory_request& r) {
    if (r.namespace_id[64] || r.kind > SC_INV_SET_CAPACITY) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = r.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    if (r.weapons & ~SC_INV_ALL_WEAPONS) return false;
    if (r.equipment & ~SC_INV_ALL_EQUIPMENT) return false;
    if (r.special_weapons & ~SC_INV_ALL_SPECIAL) return false;
    if (r.persistent_upgrades & ~SC_INV_ALL_UPGRADES) return false;
    if (r.health_tier > SC_INVENTORY_MAX_CAPACITY_TIER) return false;
    if (r.armor_tier > SC_INVENTORY_MAX_CAPACITY_TIER) return false;
    if (r.ammo_tier > SC_INVENTORY_MAX_CAPACITY_TIER) return false;
    if (r.kind == SC_INV_OBSERVE) {
        return !r.weapons && !r.equipment && !r.special_weapons && !r.persistent_upgrades &&
               !r.health_tier && !r.armor_tier && !r.ammo_tier;
    }
    return true;
}

bool same(const sc_inventory_request& a, const sc_inventory_request& b) {
    return a.kind == b.kind &&
           a.weapons == b.weapons &&
           a.equipment == b.equipment &&
           a.special_weapons == b.special_weapons &&
           a.persistent_upgrades == b.persistent_upgrades &&
           a.health_tier == b.health_tier &&
           a.armor_tier == b.armor_tier &&
           a.ammo_tier == b.ammo_tier &&
           !std::memcmp(a.namespace_id, b.namespace_id, sizeof(a.namespace_id));
}

sc_inventory_result initial(const sc_inventory_request& r) {
    sc_inventory_result out{};
    out.size = sizeof(out);
    out.abi_version = SC_INVENTORY_ABI_VERSION;
    out.kind = r.kind;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id));
    return out;
}

void reset_session(const char* namespace_id) {
    AcquireSRWLockExclusive(&state_lock);
    reset_state_locked(namespace_id);
    ReleaseSRWLockExclusive(&state_lock);
}

void bind_run_state_if_needed(uintptr_t player) {
    if (!player) return;
    AcquireSRWLockExclusive(&state_lock);
    if (player == last_bound_player) {
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }
    const auto needed = shared_state;
    last_bound_player = player;
    ReleaseSRWLockExclusive(&state_lock);

    if (!needed.weapons && !needed.equipment && !needed.special_weapons &&
        !needed.persistent_upgrades && !needed.health_tier && !needed.armor_tier && !needed.ammo_tier) {
        return;
    }
}

void execute(const sc_inventory_request& r, sc_inventory_result& out, const Calls& calls) {
    const auto player = calls.player(calls.context);
    if (!player) { out.outcome = SC_INV_NO_PLAYER; return; }

    SnapshotFacts before{};
    if (!calls.read(calls.context, player, before)) {
        out.outcome = SC_INV_READ_FAILED; return;
    }
    out.flags |= SC_INV_BEFORE_VALID;
    out.weapons_before = before.weapons;
    out.equipment_before = before.equipment;
    out.special_before = before.special_weapons;
    out.upgrades_before = before.persistent_upgrades;
    out.health_tier_before = before.health_tier;
    out.armor_tier_before = before.armor_tier;
    out.ammo_tier_before = before.ammo_tier;

    AcquireSRWLockExclusive(&state_lock);
    check_namespace(r.namespace_id);
    record_observed_locked(before);
    last_bound_player = player;

    if (r.kind == SC_INV_OBSERVE) {
        out.weapons_after = before.weapons;
        out.equipment_after = before.equipment;
        out.special_after = before.special_weapons;
        out.upgrades_after = before.persistent_upgrades;
        out.health_tier_after = before.health_tier;
        out.armor_tier_after = before.armor_tier;
        out.ammo_tier_after = before.ammo_tier;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_INV_AFTER_VALID;
        out.outcome = SC_INV_OBSERVED;
        return;
    }

    if (r.kind == SC_INV_ENSURE) {
        update_items_locked(r.weapons, r.equipment, r.special_weapons, r.persistent_upgrades);
        const auto needed = shared_state;
        const uint32_t delta_w = needed.weapons & ~before.weapons;
        const uint32_t delta_e = needed.equipment & ~before.equipment;
        const uint32_t delta_s = needed.special_weapons & ~before.special_weapons;
        const uint32_t delta_u = needed.persistent_upgrades & ~before.persistent_upgrades;

        if (!delta_w && !delta_e && !delta_s && !delta_u) {
            // Already owned; idempotent completion without side effects
            out.weapons_after = before.weapons;
            out.equipment_after = before.equipment;
            out.special_after = before.special_weapons;
            out.upgrades_after = before.persistent_upgrades;
            out.health_tier_after = before.health_tier;
            out.armor_tier_after = before.armor_tier;
            out.ammo_tier_after = before.ammo_tier;
            out.operations_applied = total_operations;
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_INV_AFTER_VALID;
            out.outcome = SC_INV_MUTATED;
            return;
        }

        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_INV_NATIVE_ENTERED;
        out.native_exception = calls.ensure_items(calls.context, player, delta_w, delta_e, delta_s, delta_u);

        SnapshotFacts after{};
        const bool read_ok = calls.read(calls.context, player, after);
        if (read_ok) {
            out.flags |= SC_INV_AFTER_VALID;
            out.weapons_after = after.weapons;
            out.equipment_after = after.equipment;
            out.special_after = after.special_weapons;
            out.upgrades_after = after.persistent_upgrades;
            out.health_tier_after = after.health_tier;
            out.armor_tier_after = after.armor_tier;
            out.ammo_tier_after = after.ammo_tier;
        }
        if (out.native_exception || !read_ok) {
            out.outcome = SC_INV_NATIVE_FAILED; return;
        }
        if (!calls.refresh(calls.context, player)) {
            out.outcome = SC_INV_REFRESH_FAILED; return;
        }
        out.flags |= SC_INV_REFRESHED;
        out.outcome = SC_INV_MUTATED;

        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    if (r.kind == SC_INV_SET_CAPACITY) {
        update_capacity_locked(r.health_tier, r.armor_tier, r.ammo_tier);
        const auto needed = shared_state;

        if (before.health_tier >= needed.health_tier &&
            before.armor_tier >= needed.armor_tier &&
            before.ammo_tier >= needed.ammo_tier) {
            // Already at or above requested capacity tiers; idempotent completion
            out.weapons_after = before.weapons;
            out.equipment_after = before.equipment;
            out.special_after = before.special_weapons;
            out.upgrades_after = before.persistent_upgrades;
            out.health_tier_after = before.health_tier;
            out.armor_tier_after = before.armor_tier;
            out.ammo_tier_after = before.ammo_tier;
            out.operations_applied = total_operations;
            ReleaseSRWLockExclusive(&state_lock);
            out.flags |= SC_INV_AFTER_VALID;
            out.outcome = SC_INV_MUTATED;
            return;
        }

        ReleaseSRWLockExclusive(&state_lock);
        out.flags |= SC_INV_NATIVE_ENTERED;
        out.native_exception = calls.set_capacity(calls.context, player, needed.health_tier, needed.armor_tier, needed.ammo_tier);

        SnapshotFacts after{};
        const bool read_ok = calls.read(calls.context, player, after);
        if (read_ok) {
            out.flags |= SC_INV_AFTER_VALID;
            out.weapons_after = after.weapons;
            out.equipment_after = after.equipment;
            out.special_after = after.special_weapons;
            out.upgrades_after = after.persistent_upgrades;
            out.health_tier_after = after.health_tier;
            out.armor_tier_after = after.armor_tier;
            out.ammo_tier_after = after.ammo_tier;
        }
        if (out.native_exception || !read_ok) {
            out.outcome = SC_INV_NATIVE_FAILED; return;
        }
        if (!calls.refresh(calls.context, player)) {
            out.outcome = SC_INV_REFRESH_FAILED; return;
        }
        out.flags |= SC_INV_REFRESHED;
        out.outcome = SC_INV_MUTATED;

        AcquireSRWLockExclusive(&state_lock);
        ++total_operations;
        out.operations_applied = total_operations;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }

    ReleaseSRWLockExclusive(&state_lock);
    out.outcome = SC_INV_PRECONDITION;
}
}
