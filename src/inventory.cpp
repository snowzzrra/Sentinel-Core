#include "namespace_id.h"
#include "inventory.h"
#include <cstring>
#include <algorithm>
#include <atomic>

namespace sentinel::inventory {
void progression_facts(const SnapshotFacts& facts, sc_progression_result& out) {
    const unsigned items[] = {7770098, 7770099, 7770097, 0, 7770106, 7770107};
    for (unsigned stage = 0; stage < 6; ++stage) {
        const auto bit = items[stage] ? 1ull << (items[stage] - 7770058) : 0;
        const bool known = bit ? (facts.perks_known & bit) != 0 : facts.pickup_radius <= 1;
        const bool owned = bit ? (facts.perks_owned & bit) != 0 : facts.pickup_radius == 1;
        if (known) out.suit_known |= 1u << stage;
        if (known && owned) out.suit_owned |= 1u << stage;
    }
    out.slayer_key_quantity = facts.slayer_key_quantity;
    const auto hook = 1ull << (7770083 - 7770058);
    if (facts.perks_known & hook) out.meat_hook = (facts.perks_owned & hook) ? SC_EFFECT_PRESENT : SC_EFFECT_ABSENT;
}

uint8_t capacity_stage(uint32_t available, uint32_t active, int host_level) {
    if (host_level < 0 || host_level > SC_INVENTORY_MAX_CAPACITY_TIER ||
        available != ((1u << host_level) - 1) || active != available)
        return SC_INVENTORY_UNKNOWN_TIER;
    return static_cast<uint8_t>(host_level);
}

namespace {
SRWLOCK state_lock = SRWLOCK_INIT;
SnapshotFacts shared_state{0, 0, 0, 0, 0, 0, 0, 0};
std::atomic<uint32_t> observed_upgrades{0};
uint64_t total_operations = 0;
char bound_namespace[65]{};

void reset_state_locked(const char* id) {
    shared_state = {0, 0, 0, 0, 0, 0, 0, 0};
    observed_upgrades.store(0, std::memory_order_release);
    total_operations = 0;
    if (id) std::memcpy(bound_namespace, id, sizeof(bound_namespace));
    else std::memset(bound_namespace, 0, sizeof(bound_namespace));
}

void check_namespace(const char* id) {
    if (std::memcmp(bound_namespace, id, sizeof(bound_namespace)) != 0) {
        reset_state_locked(id);
    }
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

bool blood_punch_observed() {
    const auto observed = observed_upgrades.load(std::memory_order_acquire);
    return observed != SC_INVENTORY_UNKNOWN_MASK && (observed & (1u << SC_INV_BLOOD_PUNCH_SHIFT)) != 0;
}

bool microwave_counter(const SnapshotFacts& facts) {
    return facts.weapons != SC_INVENTORY_UNKNOWN_MASK &&
        (facts.weapons & SC_INV_WEAPON_PLASMA_RIFLE) &&
        (facts.perks_known & facts.perks_owned & (1ull << 6));
}

bool stone_counter(const SnapshotFacts& facts, bool hammer) {
    return hammer || (facts.weapons != SC_INVENTORY_UNKNOWN_MASK &&
        ((facts.weapons & SC_INV_WEAPON_CHAINSAW) ||
         ((facts.weapons & SC_INV_WEAPON_SHOTGUN) && (facts.perks_known & facts.perks_owned & (1ull << 1)))));
}

bool armored_counter(const SnapshotFacts& facts) {
    if (facts.weapons == SC_INVENTORY_UNKNOWN_MASK) return false;
    const auto perks = facts.perks_known & facts.perks_owned;
    return (facts.weapons & SC_INV_WEAPON_BALLISTA) ||
        ((facts.weapons & SC_INV_WEAPON_HEAVY_CANNON) && (perks & (1ull << 2))) ||
        ((facts.weapons & SC_INV_WEAPON_SHOTGUN) && (perks & 1ull));
}

static bool valid_observation(const sc_inventory_request& r) {
    if (r.kind == SC_INV_OBSERVE) {
        return !r.weapons && !r.equipment && !r.special_weapons && !r.persistent_upgrades && !r.health_tier &&
               !r.armor_tier && !r.ammo_tier;
    }
    return true;
}

static bool valid_capacity_request(const sc_inventory_request& r) {
    if (r.kind == SC_INV_ENSURE && (r.health_tier || r.armor_tier || r.ammo_tier)) return false;
    if (r.kind == SC_INV_SET_CAPACITY && (r.weapons || r.equipment)) return false;
    if (r.health_tier > SC_INVENTORY_MAX_CAPACITY_TIER) return false;
    if (r.armor_tier > SC_INVENTORY_MAX_CAPACITY_TIER) return false;
    if (r.ammo_tier > SC_INVENTORY_MAX_CAPACITY_TIER) return false;
    return valid_observation(r);
}

bool valid(const sc_inventory_request& r) {
    if (!valid_namespace_id(r.namespace_id) || r.kind > SC_INV_SET_CAPACITY) return false;
    if (r.weapons & ~SC_INV_ALL_WEAPONS) return false;
    if (r.equipment & ~SC_INV_ALL_EQUIPMENT) return false;
    if (r.special_weapons || r.persistent_upgrades || r.reserved) return false;
    return valid_capacity_request(r);
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
    out.ice_bomb_before = out.ice_bomb_after = SC_INVENTORY_UNKNOWN_ITEM;
    out.dash_before = out.dash_after = SC_INVENTORY_UNKNOWN_ITEM;
    out.slayer_key_before = out.slayer_key_after = SC_INVENTORY_UNKNOWN_ITEM;
    out.frag_grenade_before = out.frag_grenade_after = SC_INVENTORY_UNKNOWN_ITEM;
    out.flame_belch_before = out.flame_belch_after = SC_INVENTORY_UNKNOWN_ITEM;
    out.blood_punch_before = out.blood_punch_after = SC_INVENTORY_UNKNOWN_ITEM;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id));
    return out;
}

void reset_session(const char* namespace_id) {
    AcquireSRWLockExclusive(&state_lock);
    reset_state_locked(namespace_id);
    ReleaseSRWLockExclusive(&state_lock);
}

static bool valid_mask(uint32_t value, uint32_t allowed) {
    return value == SC_INVENTORY_UNKNOWN_MASK || !(value & ~allowed);
}
static bool valid_item(uint8_t value) { return value <= 1 || value == SC_INVENTORY_UNKNOWN_ITEM; }
static bool valid_tier(uint8_t value) {
    return value == SC_INVENTORY_UNKNOWN_TIER || value <= SC_INVENTORY_MAX_CAPACITY_TIER;
}

bool valid_facts(const SnapshotFacts& f) {
    return valid_mask(f.weapons, SC_INV_ALL_WEAPONS) && valid_mask(f.equipment, SC_INV_ALL_EQUIPMENT) &&
           !f.special_weapons && valid_mask(f.persistent_upgrades, SC_INV_BLOOD_PUNCH_MASK) && !f.reserved &&
           valid_tier(f.health_tier) && valid_tier(f.armor_tier) && valid_tier(f.ammo_tier) &&
           valid_item(f.ice_bomb) && valid_item(f.dash) && valid_item(f.slayer_key) &&
           valid_item(f.frag_grenade) && valid_item(f.flame_belch) && valid_item(f.blood_punch) &&
           !(f.perks_owned & ~f.perks_known);
}

static bool verified_items(const SnapshotFacts& needed, const SnapshotFacts& after, bool read_ok,
                           uint32_t native_exception) {
    if (native_exception || !read_ok ||
        (needed.weapons && (after.weapons == SC_INVENTORY_UNKNOWN_MASK ||
                            (after.weapons & needed.weapons) != needed.weapons)) ||
        (needed.equipment && (after.equipment == SC_INVENTORY_UNKNOWN_MASK ||
                              (after.equipment & needed.equipment) != needed.equipment))) {
        return false;
    }
    return true;
}

static void ensure_locked(const sc_inventory_request& r, sc_inventory_result& out, const Calls& calls,
                          uintptr_t player, const SnapshotFacts& before) {
    update_items_locked(r.weapons, r.equipment, r.special_weapons, r.persistent_upgrades);
    const auto needed = shared_state;
    if ((needed.weapons && before.weapons == SC_INVENTORY_UNKNOWN_MASK) ||
        (needed.equipment && before.equipment == SC_INVENTORY_UNKNOWN_MASK)) {
        ReleaseSRWLockExclusive(&state_lock);
        out.outcome = SC_INV_READ_FAILED;
        return;
    }
    const uint32_t delta_w = needed.weapons & ~before.weapons;
    const uint32_t delta_e = needed.equipment & ~before.equipment;
    const uint32_t delta_s = needed.special_weapons & ~before.special_weapons;
    const uint32_t delta_u = needed.persistent_upgrades & ~before.persistent_upgrades;

    if (!delta_w && !delta_e && !delta_s && !delta_u) {
        out.weapons_after = before.weapons;
        out.equipment_after = before.equipment;
        out.ice_bomb_after = before.ice_bomb;
        out.dash_after = before.dash;
        out.slayer_key_after = before.slayer_key;
        out.frag_grenade_after = before.frag_grenade;
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

    ReleaseSRWLockExclusive(&state_lock);
    out.flags |= SC_INV_NATIVE_ENTERED;
    out.native_exception = calls.ensure_items(calls.context, player, delta_w, delta_e, delta_s, delta_u);

    SnapshotFacts after{};
    const bool read_ok = calls.read(calls.context, player, after) && valid_facts(after);
    if (read_ok) {
        out.flags |= SC_INV_AFTER_VALID;
        out.weapons_after = after.weapons;
        out.equipment_after = after.equipment;
        out.ice_bomb_after = after.ice_bomb;
        out.dash_after = after.dash;
        out.slayer_key_after = after.slayer_key;
        out.frag_grenade_after = after.frag_grenade;
        out.special_after = after.special_weapons;
        out.upgrades_after = after.persistent_upgrades;
        out.health_tier_after = after.health_tier;
        out.armor_tier_after = after.armor_tier;
        out.ammo_tier_after = after.ammo_tier;
    }
    if (!verified_items(needed, after, read_ok, out.native_exception)) {
        out.outcome = SC_INV_NATIVE_FAILED;
        return;
    }
    if (!calls.refresh(calls.context, player)) {
        out.outcome = SC_INV_REFRESH_FAILED;
        return;
    }
    out.flags |= SC_INV_REFRESHED;
    out.outcome = SC_INV_MUTATED;

    AcquireSRWLockExclusive(&state_lock);
    ++total_operations;
    out.operations_applied = total_operations;
    ReleaseSRWLockExclusive(&state_lock);
    return;
}

static bool verified_capacity(const SnapshotFacts& needed, const SnapshotFacts& after, bool read_ok,
                              uint32_t native_exception) {
    if (native_exception || !read_ok || after.health_tier == SC_INVENTORY_UNKNOWN_TIER ||
        after.health_tier < needed.health_tier || after.armor_tier == SC_INVENTORY_UNKNOWN_TIER ||
        after.armor_tier < needed.armor_tier || after.ammo_tier == SC_INVENTORY_UNKNOWN_TIER ||
        after.ammo_tier < needed.ammo_tier) {
        return false;
    }
    return true;
}

static void set_capacity_locked(const sc_inventory_request& r, sc_inventory_result& out, const Calls& calls,
                                uintptr_t player, const SnapshotFacts& before) {
    update_capacity_locked(r.health_tier, r.armor_tier, r.ammo_tier);
    const auto needed = shared_state;

    if (before.health_tier == SC_INVENTORY_UNKNOWN_TIER || before.armor_tier == SC_INVENTORY_UNKNOWN_TIER ||
        before.ammo_tier == SC_INVENTORY_UNKNOWN_TIER) {
        ReleaseSRWLockExclusive(&state_lock);
        out.outcome = SC_INV_READ_FAILED;
        return;
    }
    if (before.health_tier >= needed.health_tier && before.armor_tier >= needed.armor_tier &&
        before.ammo_tier >= needed.ammo_tier) {
        out.weapons_after = before.weapons;
        out.equipment_after = before.equipment;
        out.ice_bomb_after = before.ice_bomb;
        out.dash_after = before.dash;
        out.slayer_key_after = before.slayer_key;
        out.frag_grenade_after = before.frag_grenade;
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

    ReleaseSRWLockExclusive(&state_lock);
    out.flags |= SC_INV_NATIVE_ENTERED;
    out.native_exception =
        calls.set_capacity(calls.context, player, needed.health_tier, needed.armor_tier, needed.ammo_tier);

    SnapshotFacts after{};
    const bool read_ok = calls.read(calls.context, player, after) && valid_facts(after);
    if (read_ok) {
        out.flags |= SC_INV_AFTER_VALID;
        out.weapons_after = after.weapons;
        out.equipment_after = after.equipment;
        out.ice_bomb_after = after.ice_bomb;
        out.dash_after = after.dash;
        out.slayer_key_after = after.slayer_key;
        out.frag_grenade_after = after.frag_grenade;
        out.special_after = after.special_weapons;
        out.upgrades_after = after.persistent_upgrades;
        out.health_tier_after = after.health_tier;
        out.armor_tier_after = after.armor_tier;
        out.ammo_tier_after = after.ammo_tier;
    }
    if (!verified_capacity(needed, after, read_ok, out.native_exception)) {
        out.outcome = SC_INV_NATIVE_FAILED;
        return;
    }
    if (!calls.refresh(calls.context, player)) {
        out.outcome = SC_INV_REFRESH_FAILED;
        return;
    }
    out.flags |= SC_INV_REFRESHED;
    out.outcome = SC_INV_MUTATED;

    AcquireSRWLockExclusive(&state_lock);
    ++total_operations;
    out.operations_applied = total_operations;
    ReleaseSRWLockExclusive(&state_lock);
    return;
}

void execute(const sc_inventory_request& r, sc_inventory_result& out, const Calls& calls) {
    if (!valid(r)) { out.outcome = SC_INV_PRECONDITION; return; }
    const auto player = calls.player(calls.context);
    if (!player) { out.outcome = SC_INV_NO_PLAYER; return; }

    SnapshotFacts before{};
    if (!calls.read(calls.context, player, before) || !valid_facts(before)) {
        out.outcome = SC_INV_READ_FAILED; return;
    }
    out.flags |= SC_INV_BEFORE_VALID;
    out.weapons_before = before.weapons;
    out.equipment_before = before.equipment;
    out.ice_bomb_before = before.ice_bomb;
    out.dash_before = before.dash;
    out.slayer_key_before = before.slayer_key;
    out.frag_grenade_before = before.frag_grenade;
    out.flame_belch_before = before.flame_belch;
    out.blood_punch_before = before.blood_punch;
    out.perks_known = before.perks_known;
    out.perks_owned = before.perks_owned;
    out.special_before = before.special_weapons;
    out.upgrades_before = before.persistent_upgrades;
    out.health_tier_before = before.health_tier;
    out.armor_tier_before = before.armor_tier;
    out.ammo_tier_before = before.ammo_tier;

    AcquireSRWLockExclusive(&state_lock);
    check_namespace(r.namespace_id);
    observed_upgrades.store(before.persistent_upgrades, std::memory_order_release);

    if (r.kind == SC_INV_OBSERVE) {
        out.weapons_after = before.weapons;
        out.equipment_after = before.equipment;
        out.ice_bomb_after = before.ice_bomb;
        out.dash_after = before.dash;
        out.slayer_key_after = before.slayer_key;
        out.frag_grenade_after = before.frag_grenade;
        out.flame_belch_after = before.flame_belch;
        out.blood_punch_after = before.blood_punch;
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
        return ensure_locked(r, out, calls, player, before);
    }

    if (r.kind == SC_INV_SET_CAPACITY) {
        return set_capacity_locked(r, out, calls, player, before);
    }

    ReleaseSRWLockExclusive(&state_lock);
    out.outcome = SC_INV_PRECONDITION;
}
}
