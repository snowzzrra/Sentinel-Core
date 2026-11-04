#include "currencies.h"
#include <climits>

namespace sentinel::currencies {
bool valid(const sc_currency_request& r) {
    const bool readable = r.currency == 0 || r.currency == 1 || r.currency == 2 || r.currency == 4;
    if (!readable || r.kind > SC_CURRENCY_GRANT) return false;
    return r.kind == SC_CURRENCY_OBSERVE ? !r.amount && !r.expected_gained :
        r.currency == 4 && r.amount >= 1 && r.amount <= 2 && r.expected_gained <= INT_MAX - r.amount;
}
sc_currency_result initial(const sc_currency_request& r) {
    sc_currency_result out{};
    out.kind = r.kind; out.currency = r.currency; out.amount = r.amount; out.expected_gained = r.expected_gained;
    return out;
}
bool exact(const sc_currency_result& out) {
    constexpr auto required = SC_CURRENCY_BEFORE_VALID | SC_CURRENCY_AFTER_VALID | SC_CURRENCY_WRITER_ENTERED;
    return out.kind == SC_CURRENCY_GRANT && (out.flags & required) == required && !out.native_exception &&
        out.gained_before == out.expected_gained &&
        uint64_t(out.balance_before) + out.amount == out.balance_after &&
        uint64_t(out.gained_before) + out.amount == out.gained_after;
}
void execute(const sc_currency_request& r, sc_currency_result& out, const Calls& calls) {
    const auto player = calls.player(calls.context);
    if (!player || !calls.read(calls.context, player, r.currency, out.balance_before, out.gained_before)) {
        out.outcome = SC_CURRENCY_READ_FAILED; return;
    }
    out.flags |= SC_CURRENCY_BEFORE_VALID;
    if (r.kind == SC_CURRENCY_OBSERVE) {
        out.balance_after = out.balance_before; out.gained_after = out.gained_before;
        out.flags |= SC_CURRENCY_AFTER_VALID; out.outcome = SC_CURRENCY_OBSERVED; return;
    }
    if (out.gained_before != r.expected_gained || out.balance_before > INT_MAX - r.amount) {
        out.outcome = SC_CURRENCY_PRECONDITION; return;
    }
    bool entered = false;
    out.native_exception = calls.grant(calls.context, player, r.currency, r.amount, entered);
    if (entered) out.flags |= SC_CURRENCY_WRITER_ENTERED;
    if (calls.read(calls.context, player, r.currency, out.balance_after, out.gained_after))
        out.flags |= SC_CURRENCY_AFTER_VALID;
    out.outcome = exact(out) ? SC_CURRENCY_GRANTED : SC_CURRENCY_UNKNOWN;
}
}
