#include "arsenal.h"
#include <cstring>

namespace sentinel::arsenal {
namespace {

uint32_t authorized_mods = 0;
SRWLOCK state_lock = SRWLOCK_INIT;

} // namespace

uint32_t shared_mods() {
    AcquireSRWLockShared(&state_lock);
    const uint32_t m = authorized_mods;
    ReleaseSRWLockShared(&state_lock);
    return m;
}

void authorize_hook(uint32_t mods) {
    AcquireSRWLockExclusive(&state_lock);
    authorized_mods |= mods & SC_ARSENAL_ATTACHMENT_MEAT_HOOK;
    ReleaseSRWLockExclusive(&state_lock);
}

bool valid(const sc_arsenal_request& request) {
    if (request.kind == SC_ARSENAL_PURCHASE_UPGRADE || request.kind == SC_ARSENAL_UPDATE_CHALLENGE)
        return false;
    if (request.kind > SC_ARSENAL_UPDATE_CHALLENGE) return false;
    if (request.mods & ~SC_ARSENAL_ALL_MODS) return false;
    if (request.upgrades & ~SC_ARSENAL_ALL_NORMAL_UPGRADES) return false;
    if (request.masteries & ~SC_ARSENAL_ALL_MASTERIES) return false;
    if (request.kind == SC_ARSENAL_SELECT_MOD) {
        if (request.select_weapon >= SC_ARSENAL_WINDEX_COUNT) return false;
        if (request.select_weapon == SC_ARSENAL_WINDEX_SSG) return false;
        if (request.select_mod > 2) return false;
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

void reset_session() {
    AcquireSRWLockExclusive(&state_lock);
    authorized_mods = 0;
    ReleaseSRWLockExclusive(&state_lock);
}

} // namespace sentinel::arsenal
