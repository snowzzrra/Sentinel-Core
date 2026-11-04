#include "inventory.h"
#include "runes.h"
#include "special.h"
#include "protocol_v2.h"
#include <cassert>

namespace sentinel::runes { Calls calls{}; }
namespace sentinel::special { Calls calls{}; }
using namespace sentinel;
int main() {
    inventory::SnapshotFacts facts{};
    sc_progression_result out{}; out.slayer_key_quantity = -1;
    inventory::progression_facts(facts, out);
    assert(!out.suit_known && !out.suit_owned && out.slayer_key_quantity == -1 && out.meat_hook == SC_EFFECT_UNPROVEN);
    const unsigned items[] = {7770098, 7770099, 7770097, 0, 7770106, 7770107};
    for (unsigned stage = 0; stage < 6; ++stage) {
        facts = {};
        if (items[stage]) facts.perks_known = facts.perks_owned = 1ull << (items[stage] - 7770058);
        else facts.pickup_radius = 1;
        out = {}; inventory::progression_facts(facts, out);
        assert(out.suit_known == (1u << stage) && out.suit_owned == (1u << stage));
    }
    facts = {}; facts.pickup_radius = 0; facts.slayer_key_quantity = 0;
    facts.perks_known = 1ull << (7770083 - 7770058);
    out = {}; inventory::progression_facts(facts, out);
    assert(out.suit_known == 8 && !out.suit_owned && !out.slayer_key_quantity && out.meat_hook == SC_EFFECT_ABSENT);
    facts.perks_owned = facts.perks_known; facts.slayer_key_quantity = 2;
    out = {}; inventory::progression_facts(facts, out);
    assert(out.meat_hook == SC_EFFECT_PRESENT && out.slayer_key_quantity == 2);
    for (const auto proof : out.mastery_proof) assert(proof == SC_EFFECT_UNPROVEN);
    generated::Bytes bytes;
    assert(generated::encode(generated::to_wire(out), bytes));
    generated::sc_progression_result decoded;
    assert(generated::decode(bytes, decoded) && decoded.slayer_key_quantity == 2 && decoded.meat_hook == SC_EFFECT_PRESENT);
}
