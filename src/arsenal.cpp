#include "arsenal.h"
#include <cstring>
#include <algorithm>

namespace sentinel::arsenal {
namespace {

struct RunState {
    uint32_t weapons = 0;
    uint32_t mods = 0;
    uint8_t selected_mods[8]{};
    uint32_t normal_upgrades = 0;
    uint16_t masteries_ap = 0;
    uint16_t mastery_challenges_completed = 0;
    uint32_t mission_challenges_completed = 0;
    uint64_t operations_applied = 0;
};

RunState shared_state{};
SRWLOCK state_lock = SRWLOCK_INIT;
uintptr_t last_bound_player = 0;

} // namespace

uint32_t weapon_for_mod(uint32_t mod_mask) {
    if (mod_mask & (SC_ARSENAL_MOD_SHOTGUN_STICKY_BOMBS | SC_ARSENAL_MOD_SHOTGUN_FULL_AUTO))
        return SC_ARSENAL_WEAPON_SHOTGUN;
    if (mod_mask & SC_ARSENAL_MOD_SSG_MEAT_HOOK)
        return SC_ARSENAL_WEAPON_SUPER_SHOTGUN;
    if (mod_mask & (SC_ARSENAL_MOD_HEAVY_PRECISION_BOLT | SC_ARSENAL_MOD_HEAVY_MICRO_MISSILES))
        return SC_ARSENAL_WEAPON_HEAVY_CANNON;
    if (mod_mask & (SC_ARSENAL_MOD_CHAINGUN_TURRET | SC_ARSENAL_MOD_CHAINGUN_SHIELD))
        return SC_ARSENAL_WEAPON_CHAINGUN;
    if (mod_mask & (SC_ARSENAL_MOD_PLASMA_HEAT_BLAST | SC_ARSENAL_MOD_PLASMA_MICROWAVE))
        return SC_ARSENAL_WEAPON_PLASMA_RIFLE;
    if (mod_mask & (SC_ARSENAL_MOD_BALLISTA_ARBALEST | SC_ARSENAL_MOD_BALLISTA_DESTROYER))
        return SC_ARSENAL_WEAPON_BALLISTA;
    if (mod_mask & (SC_ARSENAL_MOD_ROCKET_REMOTE_DET | SC_ARSENAL_MOD_ROCKET_LOCK_ON))
        return SC_ARSENAL_WEAPON_ROCKET_LAUNCHER;
    return 0;
}

uint32_t mod_for_upgrade(uint32_t upgrade_mask) {
    if (upgrade_mask & (SC_ARSENAL_UPG_SHOTGUN_STICKY_RECHARGE | SC_ARSENAL_UPG_SHOTGUN_STICKY_EXPLOSION))
        return SC_ARSENAL_MOD_SHOTGUN_STICKY_BOMBS;
    if (upgrade_mask & (SC_ARSENAL_UPG_SHOTGUN_AUTO_RECOVERY | SC_ARSENAL_UPG_SHOTGUN_AUTO_CHARGE | SC_ARSENAL_UPG_SHOTGUN_AUTO_SPEED))
        return SC_ARSENAL_MOD_SHOTGUN_FULL_AUTO;
    if (upgrade_mask & (SC_ARSENAL_UPG_HEAVY_BOLT_MOVEMENT | SC_ARSENAL_UPG_HEAVY_BOLT_RELOAD))
        return SC_ARSENAL_MOD_HEAVY_PRECISION_BOLT;
    if (upgrade_mask & (SC_ARSENAL_UPG_HEAVY_BURST_RECHARGE | SC_ARSENAL_UPG_HEAVY_BURST_CHARGE | SC_ARSENAL_UPG_HEAVY_BURST_PRIMARY))
        return SC_ARSENAL_MOD_HEAVY_MICRO_MISSILES;
    if (upgrade_mask & (SC_ARSENAL_UPG_PLASMA_AOE_NO_DELAY | SC_ARSENAL_UPG_PLASMA_AOE_CHARGE))
        return SC_ARSENAL_MOD_PLASMA_HEAT_BLAST;
    if (upgrade_mask & (SC_ARSENAL_UPG_PLASMA_MICRO_CHARGE | SC_ARSENAL_UPG_PLASMA_MICRO_RANGE))
        return SC_ARSENAL_MOD_PLASMA_MICROWAVE;
    if (upgrade_mask & (SC_ARSENAL_UPG_ROCKET_DET_FLARE | SC_ARSENAL_UPG_ROCKET_DET_CONCUSSIVE))
        return SC_ARSENAL_MOD_ROCKET_REMOTE_DET;
    if (upgrade_mask & (SC_ARSENAL_UPG_ROCKET_LOCK_RECOVERY | SC_ARSENAL_UPG_ROCKET_LOCK_TIME))
        return SC_ARSENAL_MOD_ROCKET_LOCK_ON;
    if (upgrade_mask & (SC_ARSENAL_UPG_SSG_HOOK_RELOAD | SC_ARSENAL_UPG_SSG_DEFAULT_RELOAD))
        return SC_ARSENAL_MOD_SSG_MEAT_HOOK;
    if (upgrade_mask & (SC_ARSENAL_UPG_BALLISTA_ARBALEST_MOVE | SC_ARSENAL_UPG_BALLISTA_ARBALEST_EXPLODE))
        return SC_ARSENAL_MOD_BALLISTA_ARBALEST;
    if (upgrade_mask & (SC_ARSENAL_UPG_BALLISTA_DESTROY_AOE | SC_ARSENAL_UPG_BALLISTA_DESTROY_CHARGE))
        return SC_ARSENAL_MOD_BALLISTA_DESTROYER;
    if (upgrade_mask & (SC_ARSENAL_UPG_CHAINGUN_TURRET_EQUIP | SC_ARSENAL_UPG_CHAINGUN_TURRET_MOVE))
        return SC_ARSENAL_MOD_CHAINGUN_TURRET;
    if (upgrade_mask & (SC_ARSENAL_UPG_CHAINGUN_SHIELD_RECHARGE | SC_ARSENAL_UPG_CHAINGUN_SHIELD_SMASH))
        return SC_ARSENAL_MOD_CHAINGUN_SHIELD;
    return 0;
}

uint32_t mod_for_selection(uint8_t w_idx, uint8_t m_idx) {
    if (m_idx == 0) return 0;
    switch (w_idx) {
        case SC_ARSENAL_WINDEX_SHOTGUN:
            return (m_idx == 1) ? SC_ARSENAL_MOD_SHOTGUN_FULL_AUTO : SC_ARSENAL_MOD_SHOTGUN_STICKY_BOMBS;
        case SC_ARSENAL_WINDEX_HEAVY:
            return (m_idx == 1) ? SC_ARSENAL_MOD_HEAVY_PRECISION_BOLT : SC_ARSENAL_MOD_HEAVY_MICRO_MISSILES;
        case SC_ARSENAL_WINDEX_CHAINGUN:
            return (m_idx == 1) ? SC_ARSENAL_MOD_CHAINGUN_SHIELD : SC_ARSENAL_MOD_CHAINGUN_TURRET;
        case SC_ARSENAL_WINDEX_PLASMA:
            return (m_idx == 1) ? SC_ARSENAL_MOD_PLASMA_HEAT_BLAST : SC_ARSENAL_MOD_PLASMA_MICROWAVE;
        case SC_ARSENAL_WINDEX_BALLISTA:
            return (m_idx == 1) ? SC_ARSENAL_MOD_BALLISTA_ARBALEST : SC_ARSENAL_MOD_BALLISTA_DESTROYER;
        case SC_ARSENAL_WINDEX_ROCKET:
            return (m_idx == 1) ? SC_ARSENAL_MOD_ROCKET_REMOTE_DET : SC_ARSENAL_MOD_ROCKET_LOCK_ON;
        default:
            return 0;
    }
}

uint32_t shared_mods() {
    AcquireSRWLockShared(&state_lock);
    const uint32_t m = shared_state.mods;
    ReleaseSRWLockShared(&state_lock);
    return m;
}

void authorize_hook(uint32_t mods) {
    AcquireSRWLockExclusive(&state_lock);
    shared_state.mods |= mods & SC_ARSENAL_ATTACHMENT_MEAT_HOOK;
    ReleaseSRWLockExclusive(&state_lock);
}

uint16_t compute_effective_masteries(uint32_t weapons, uint32_t mods,
                                     uint16_t ap_masteries, uint16_t /*challenges_completed*/) {
    uint16_t effective = 0;
    for (unsigned i = 0; i < 13; ++i) {
        const uint32_t mod_bit = 1u << i;
        const uint32_t weapon_bit = weapon_for_mod(mod_bit);
        const bool weapon_owned = (weapons & weapon_bit) != 0;
        const bool mod_owned = (mods & mod_bit) != 0;
        const bool mastery_granted = (ap_masteries & (1u << i)) != 0;
        if (weapon_owned && mod_owned && mastery_granted) {
            effective |= static_cast<uint16_t>(1u << i);
        }
    }
    return effective;
}

bool valid(const sc_arsenal_request& request) {
    if (request.kind > SC_ARSENAL_UPDATE_CHALLENGE) return false;
    if (request.mods & ~SC_ARSENAL_ALL_MODS) return false;
    if (request.upgrades & ~SC_ARSENAL_ALL_NORMAL_UPGRADES) return false;
    if (request.masteries & ~SC_ARSENAL_ALL_MASTERIES) return false;
    if (request.kind == SC_ARSENAL_SELECT_MOD) {
        if (request.select_weapon >= SC_ARSENAL_WINDEX_COUNT) return false;
        if (request.select_weapon == SC_ARSENAL_WINDEX_SSG) return false;
        if (request.select_mod > 2) return false;
    }
    if (request.kind == SC_ARSENAL_UPDATE_CHALLENGE) {
        if (request.challenge_index >= 32) return false;
    }
    return true;
}

bool same(const sc_arsenal_request& a, const sc_arsenal_request& b) {
    return a.kind == b.kind && a.mods == b.mods &&
           a.upgrades == b.upgrades && a.masteries == b.masteries &&
           a.select_weapon == b.select_weapon && a.select_mod == b.select_mod &&
           a.challenge_index == b.challenge_index &&
           a.challenge_progress == b.challenge_progress &&
           a.challenge_completed == b.challenge_completed &&
           !std::memcmp(a.namespace_id, b.namespace_id, sizeof(a.namespace_id));
}

void initial(const sc_arsenal_request& request, sc_arsenal_result& out) {
    out.size = sizeof(sc_arsenal_result);
    out.abi_version = SC_ARSENAL_ABI_VERSION;
    std::memcpy(out.namespace_id, request.namespace_id, sizeof(out.namespace_id));
    out.kind = request.kind;
    out.outcome = SC_ARSENAL_OUTCOME_OK;
    out.flags = 0;
    out.native_exception = 0;
    out.weapons_before = 0;
    out.weapons_after = 0;
    out.mods_before = 0;
    out.mods_after = 0;
    std::memset(out.selected_mods_before, 0, sizeof(out.selected_mods_before));
    std::memset(out.selected_mods_after, 0, sizeof(out.selected_mods_after));
    out.normal_upgrades_before = 0;
    out.normal_upgrades_after = 0;
    out.masteries_ap_before = 0;
    out.masteries_ap_after = 0;
    out.mastery_challenges_active_before = 0;
    out.mastery_challenges_active_after = 0;
    out.mastery_challenges_completed_before = 0;
    out.mastery_challenges_completed_after = 0;
    out.masteries_effective_before = 0;
    out.masteries_effective_after = 0;
    out.mission_challenges_active_before = 0;
    out.mission_challenges_active_after = 0;
    out.mission_challenges_completed_before = 0;
    out.mission_challenges_completed_after = 0;
    out.operations_applied = 0;
}

void execute(const sc_arsenal_request& request, sc_arsenal_result& out, const Calls& calls) {
    initial(request, out);
    if (!valid(request)) {
        out.outcome = SC_ARSENAL_OUTCOME_REJECTED;
        return;
    }

    const auto p = calls.player ? calls.player(calls.context) : 0;
    SnapshotFacts before{};
    if (calls.read && calls.read(calls.context, p, before)) {
        out.flags |= SC_ARSENAL_FLAG_BEFORE_VALID;
    }

    if (!(out.flags & SC_ARSENAL_FLAG_BEFORE_VALID)) {
        out.outcome = SC_ARSENAL_OUTCOME_UNAVAILABLE;
        return;
    }
    AcquireSRWLockExclusive(&state_lock);
    out.weapons_before = before.weapons;
    out.mods_before = before.mods;
    std::memcpy(out.selected_mods_before, before.selected_mods, sizeof(out.selected_mods_before));
    out.normal_upgrades_before = before.normal_upgrades;
    out.masteries_ap_before = before.masteries_ap;
    out.mastery_challenges_active_before = before.mastery_challenges_active;
    out.mastery_challenges_completed_before = before.mastery_challenges_completed;
    out.masteries_effective_before = before.masteries_effective;
    out.mission_challenges_active_before = before.mission_challenges_active;
    out.mission_challenges_completed_before = before.mission_challenges_completed;

    SnapshotFacts after = before;
    bool mutated = false;

    switch (request.kind) {
    case SC_ARSENAL_OBSERVE:
        out.outcome = SC_ARSENAL_OUTCOME_OK;
        break;

    case SC_ARSENAL_ENSURE_MODS: {
        const uint32_t missing = request.mods & ~before.mods;
        if (!missing) {
            out.outcome = SC_ARSENAL_OUTCOME_NOOP;
            break;
        }
        // Ensure that mod ownership does NOT alter existing mod selection
        out.flags |= SC_ARSENAL_FLAG_SELECTION_PRESERVED;
        shared_state.mods |= request.mods;
        after.mods |= request.mods;
        if (p && calls.ensure_mods) {
            ReleaseSRWLockExclusive(&state_lock);
            const auto err = calls.ensure_mods(calls.context, p, missing);
            AcquireSRWLockExclusive(&state_lock);
            if (err) { out.native_exception = err; out.outcome = SC_ARSENAL_OUTCOME_CRASH_PROTECTED; }
        }
        mutated = true;
        break;
    }

    case SC_ARSENAL_SELECT_MOD: {
        const uint8_t w_idx = request.select_weapon;
        const uint8_t m_idx = request.select_mod;
        if (m_idx != 0) {
            const uint32_t target_mod = mod_for_selection(w_idx, m_idx);
            if (!target_mod || !(after.mods & target_mod)) {
                out.outcome = SC_ARSENAL_OUTCOME_REJECTED;
                ReleaseSRWLockExclusive(&state_lock);
                return;
            }
        }
        if (before.selected_mods[w_idx] == m_idx) {
            out.outcome = SC_ARSENAL_OUTCOME_NOOP;
            break;
        }
        shared_state.selected_mods[w_idx] = m_idx;
        after.selected_mods[w_idx] = m_idx;
        if (p && calls.select_mod) {
            ReleaseSRWLockExclusive(&state_lock);
            const auto err = calls.select_mod(calls.context, p, w_idx, m_idx);
            AcquireSRWLockExclusive(&state_lock);
            if (err) { out.native_exception = err; out.outcome = SC_ARSENAL_OUTCOME_CRASH_PROTECTED; }
        }
        mutated = true;
        break;
    }

    case SC_ARSENAL_PURCHASE_UPGRADE: {
        const uint32_t missing = request.upgrades & ~before.normal_upgrades;
        if (!missing) {
            out.outcome = SC_ARSENAL_OUTCOME_NOOP;
            break;
        }
        // Invariant: Mod must be owned for its normal upgrades to be purchased
        for (unsigned i = 0; i < 28; ++i) {
            const uint32_t upg_bit = 1u << i;
            if (missing & upg_bit) {
                const uint32_t req_mod = mod_for_upgrade(upg_bit);
                if (!(after.mods & req_mod)) {
                    out.outcome = SC_ARSENAL_OUTCOME_REJECTED;
                    ReleaseSRWLockExclusive(&state_lock);
                    return;
                }
            }
        }
        shared_state.normal_upgrades |= request.upgrades;
        after.normal_upgrades |= request.upgrades;
        if (p && calls.purchase_upgrade) {
            ReleaseSRWLockExclusive(&state_lock);
            const auto err = calls.purchase_upgrade(calls.context, p, missing);
            AcquireSRWLockExclusive(&state_lock);
            if (err) { out.native_exception = err; out.outcome = SC_ARSENAL_OUTCOME_CRASH_PROTECTED; }
        }
        mutated = true;
        break;
    }

    case SC_ARSENAL_PROJECT_MASTERY: {
        // Invariant: AP Mastery ownership does NOT grant base mod or normal upgrades,
        // and does NOT complete the native challenge.
        const uint16_t missing = static_cast<uint16_t>(request.masteries & ~before.masteries_ap);
        if (!missing) {
            out.outcome = SC_ARSENAL_OUTCOME_NOOP;
            break;
        }
        shared_state.masteries_ap |= static_cast<uint16_t>(request.masteries);
        after.masteries_ap |= static_cast<uint16_t>(request.masteries);
        if (p && calls.project_mastery) {
            ReleaseSRWLockExclusive(&state_lock);
            const auto err = calls.project_mastery(calls.context, p, missing);
            AcquireSRWLockExclusive(&state_lock);
            if (err) { out.native_exception = err; out.outcome = SC_ARSENAL_OUTCOME_CRASH_PROTECTED; }
        }
        mutated = true;
        break;
    }

    case SC_ARSENAL_UPDATE_CHALLENGE: {
        const uint16_t c_idx = request.challenge_index;
        if (request.challenge_completed) {
            shared_state.mission_challenges_completed |= (1u << c_idx);
            after.mission_challenges_completed |= (1u << c_idx);
        }
        after.mission_challenges_progress[c_idx] = request.challenge_progress;
        if (p && calls.update_challenge) {
            ReleaseSRWLockExclusive(&state_lock);
            const auto err = calls.update_challenge(calls.context, p, c_idx,
                                                   request.challenge_progress, request.challenge_completed);
            AcquireSRWLockExclusive(&state_lock);
            if (err) { out.native_exception = err; out.outcome = SC_ARSENAL_OUTCOME_CRASH_PROTECTED; }
        }
        mutated = true;
        break;
    }
    }

    ReleaseSRWLockExclusive(&state_lock);
    const SnapshotFacts expected = after;
    SnapshotFacts actual{};
    const bool read_ok = calls.read && calls.read(calls.context, p, actual);
    after = actual;
    bool postcondition = read_ok;
    switch (request.kind) {
    case SC_ARSENAL_ENSURE_MODS: postcondition = postcondition && (after.mods & request.mods) == request.mods; break;
    case SC_ARSENAL_SELECT_MOD: postcondition = postcondition && after.selected_mods[request.select_weapon] == request.select_mod; break;
    case SC_ARSENAL_PURCHASE_UPGRADE: postcondition = postcondition && (after.normal_upgrades & request.upgrades) == request.upgrades; break;
    case SC_ARSENAL_PROJECT_MASTERY: postcondition = postcondition && (after.masteries_ap & request.masteries) == request.masteries; break;
    case SC_ARSENAL_UPDATE_CHALLENGE:
        postcondition = postcondition && after.mastery_challenges_completed == expected.mastery_challenges_completed &&
            after.mission_challenges_completed == expected.mission_challenges_completed; break;
    default: break;
    }
    if (read_ok) out.flags |= SC_ARSENAL_FLAG_AFTER_VALID;
    if (!postcondition || out.native_exception) {
        out.outcome = out.native_exception ? SC_ARSENAL_OUTCOME_CRASH_PROTECTED : SC_ARSENAL_OUTCOME_UNAVAILABLE;
        out.flags &= ~SC_ARSENAL_FLAG_SELECTION_PRESERVED;
    } else if (mutated) {
        out.flags |= SC_ARSENAL_FLAG_MUTATED;
        if (p && calls.refresh) calls.refresh(calls.context, p);
    }
    AcquireSRWLockExclusive(&state_lock);
    if (mutated && postcondition && !out.native_exception) ++shared_state.operations_applied;

    out.weapons_after = after.weapons;
    out.mods_after = after.mods;
    std::memcpy(out.selected_mods_after, after.selected_mods, sizeof(out.selected_mods_after));
    out.normal_upgrades_after = after.normal_upgrades;
    out.masteries_ap_after = after.masteries_ap;
    out.mastery_challenges_active_after = after.mastery_challenges_active;
    out.mastery_challenges_completed_after = after.mastery_challenges_completed;
    out.masteries_effective_after = after.masteries_effective;
    out.mission_challenges_active_after = after.mission_challenges_active;
    out.mission_challenges_completed_after = after.mission_challenges_completed;
    out.operations_applied = shared_state.operations_applied;

    ReleaseSRWLockExclusive(&state_lock);
}

void bind_run_state_if_needed(uintptr_t player_ptr) {
    if (!player_ptr) return;
    AcquireSRWLockExclusive(&state_lock);
    if (last_bound_player == player_ptr) {
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }
    last_bound_player = player_ptr;
    ReleaseSRWLockExclusive(&state_lock);
}

void reset_session() {
    AcquireSRWLockExclusive(&state_lock);
    shared_state = RunState{};
    last_bound_player = 0;
    ReleaseSRWLockExclusive(&state_lock);
}

} // namespace sentinel::arsenal
