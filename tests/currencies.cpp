#include "currencies.h"
#include "protocol_v2.h"
#include "wire_fixtures.h"
#include <cassert>
#include <climits>

using namespace sentinel;
void grant_cases() {
    struct State { uint32_t balance = 0, gained = 7, writes = 0, error = 0, extra = 0; bool partial = false, read = true, after = true; } state;
    currencies::Calls calls{&state, [](void*) -> uintptr_t { return 1; },
        [](void* p, uintptr_t, uint32_t currency, uint32_t& b, uint32_t& g) {
            auto& s = *static_cast<State*>(p); assert(currency == 4);
            b = s.balance; g = s.gained; return s.read && (!s.writes || s.after);
        },
        [](void* p, uintptr_t, uint32_t currency, uint32_t amount, bool& entered) -> uint32_t {
            auto& s = *static_cast<State*>(p); assert(currency == 4); entered = true; ++s.writes;
            s.balance += amount + s.extra; if (!s.partial) s.gained += amount + s.extra; return s.error;
        }};
    sc_currency_request r{}; r.kind = SC_CURRENCY_GRANT; r.currency = 4; r.amount = 2; r.expected_gained = 7;
    auto run = [&] { auto out = currencies::initial(r); currencies::execute(r, out, calls); return out; };
    assert(currencies::valid(r));
    auto out = run(); assert(currencies::exact(out) && out.balance_before == 0 && out.balance_after == 2);
    out = run(); assert(out.outcome == SC_CURRENCY_PRECONDITION && state.writes == 1);
    state = {}; r.amount = 1;
    out = run(); assert(currencies::exact(out)); assert(out.balance_after == 1); assert(out.gained_after == 8);
    r.amount = 2;
    for (unsigned mode = 0; mode < 4; ++mode) {
        state = {}; state.partial = mode == 0; state.after = mode != 1; state.error = mode == 2 ? 5 : 0; state.extra = mode == 3 ? 1 : 0;
        out = run(); assert(!currencies::exact(out) && out.outcome == SC_CURRENCY_UNKNOWN && state.writes == 1);
    }
    state = {}; state.balance = INT_MAX;
    assert(run().outcome == SC_CURRENCY_PRECONDITION && state.writes == 0);
    state = {}; state.read = false;
    assert(run().outcome == SC_CURRENCY_READ_FAILED && state.writes == 0);
}
void valid_inputs() {
    sc_currency_request r{};
    for (auto currency : {0u, 1u, 2u, 4u}) {
        r = {}; r.currency = currency; assert(currencies::valid(r));
        r.kind = SC_CURRENCY_GRANT; r.amount = 1; assert(currencies::valid(r) == (currency == 4));
    }
    r.currency = 3; assert(!currencies::valid(r));
    r.currency = 4; r.expected_gained = INT_MAX; assert(!currencies::valid(r));
}
int main(int argc, char** argv) {
    assert(argc == 2);
    grant_cases(); valid_inputs();
    const auto fixtures = wire_fixtures::read(argv[1]);
    for (const auto& [name, fixture] : fixtures) {
        generated::currencies_payload payload;
        assert(generated::decode(fixture.bytes, payload));
        generated::Bytes bytes; assert(generated::encode(payload, bytes) && bytes == fixture.bytes);
        if (name == "battery_bundle") assert(std::get<generated::currencies_grant_payload>(payload).amount == 2);
    }
}
