#include "weapon_points.h"
#include <cstring>
#include <climits>

namespace sentinel::weapon_points {
Source direct_source(uint32_t rva) {
    switch (rva) {
    case 0xb97fc5: case 0xb98238: case 0xb98497: case 0xb98779: case 0xb98ef7: return Source::encounter;
    case 0xc82e05: case 0xc82ad5: case 0xc82c02: return Source::pickup;
    case 0xd6d273: case 0xd6d4b4: return Source::target;
    case 0x105cbf3: return Source::script;
    case 0x147546e: return Source::unlockable;
    default: return Source::unknown;
    }
}
Source wrapper_source(uint32_t rva) {
    switch (rva) {
    case 0x1474d7f: return Source::unlockable;
    case 0x143404f: return Source::devinv;
    case 0xc98c2c: return Source::slayer_gate;
    case 0xd0feee: case 0xd101ff: return Source::useable;
    default: return Source::unknown;
    }
}
bool suppress(bool active, Source source, uint32_t currency, int32_t delta, bool ap_origin) {
    return active && !ap_origin && source != Source::unknown && currency == 0 && delta > 0;
}
bool valid(const sc_weapon_points_request& r) {
    if (r.namespace_id[64] || r.kind > SC_WUP_GRANT) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = r.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    return r.kind == SC_WUP_OBSERVE ? !r.amount && !r.expected_gained :
        r.amount > 0 && r.amount <= SC_WEAPON_POINTS_MAX_GRANT && r.expected_gained <= INT_MAX-r.amount;
}
bool same(const sc_weapon_points_request& a, const sc_weapon_points_request& b) {
    return a.kind == b.kind && a.amount == b.amount && a.expected_gained == b.expected_gained &&
        !std::memcmp(a.namespace_id, b.namespace_id, sizeof(a.namespace_id));
}
sc_weapon_points_result initial(const sc_weapon_points_request& r) {
    sc_weapon_points_result out{}; out.size = sizeof(out); out.abi_version = SC_WEAPON_POINTS_ABI_VERSION;
    out.kind = r.kind; out.amount = r.amount; out.expected_gained = r.expected_gained;
    std::memcpy(out.namespace_id, r.namespace_id, sizeof(out.namespace_id));
    return out;
}
void execute(const sc_weapon_points_request& r, sc_weapon_points_result& out, const Calls& calls) {
    const auto player = calls.player(calls.context);
    if (!player) { out.outcome = SC_WUP_NO_PLAYER; return; }
    if (!calls.read(calls.context, player, out.balance_before, out.gained_before)) {
        out.outcome = SC_WUP_READ_FAILED; return;
    }
    out.flags |= SC_WUP_BEFORE_VALID;
    out.balance_after = out.balance_before; out.gained_after = out.gained_before;
    if (r.kind == SC_WUP_OBSERVE) {
        out.flags |= SC_WUP_AFTER_VALID; out.outcome = SC_WUP_OBSERVED; return;
    }
    if (out.gained_before != r.expected_gained || out.balance_before > INT_MAX-r.amount) {
        out.outcome = SC_WUP_PRECONDITION; return;
    }
    out.flags |= SC_WUP_NATIVE_ENTERED;
    out.native_exception = calls.grant(calls.context, player, r.amount);
    const bool read = calls.read(calls.context, player, out.balance_after, out.gained_after);
    if (read) out.flags |= SC_WUP_AFTER_VALID;
    if (out.native_exception || !read || out.balance_after != out.balance_before+r.amount ||
        out.gained_after != out.gained_before+r.amount) { out.outcome = SC_WUP_NATIVE_FAILED; return; }
    if (!calls.refresh(calls.context, player)) { out.outcome = SC_WUP_REFRESH_FAILED; return; }
    out.flags |= SC_WUP_REFRESHED; out.outcome = SC_WUP_GRANTED;
}
}
