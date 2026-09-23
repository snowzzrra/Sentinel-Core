#include "special.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL special model line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace sentinel::special { Calls calls{}; }

namespace {
struct Fixture {
    sentinel::special::SnapshotFacts facts{};
    uint32_t ensure_result = 0;
    unsigned ensure_calls = 0;
    unsigned select_calls = 0;
    unsigned present_calls = 0;
    bool materialize = false;
    bool player_present = true;
};

uintptr_t player(void* context) {
    return static_cast<Fixture*>(context)->player_present ? 1 : 0;
}
bool read(void* context, uintptr_t, sentinel::special::SnapshotFacts& facts) {
    facts = static_cast<Fixture*>(context)->facts;
    return true;
}
uint32_t ensure(void* context, uintptr_t, uint32_t crucible, uint32_t hammer, uint32_t tier) {
    auto& fixture = *static_cast<Fixture*>(context);
    ++fixture.ensure_calls;
    if (fixture.ensure_result) return fixture.ensure_result;
    if (fixture.materialize) {
        if (crucible) fixture.facts.native_crucible = 1;
        if (hammer) fixture.facts.native_hammer = 1;
        if (hammer && tier >= SC_SPECIAL_HAMMER_TIER_UPGRADED) fixture.facts.hammer_loot_projected = true;
    }
    return 0;
}
uint32_t select(void* context, uintptr_t, uint32_t selected) {
    auto& fixture = *static_cast<Fixture*>(context);
    ++fixture.select_calls;
    fixture.facts.native_selected = static_cast<uint8_t>(selected);
    return 0;
}
bool present(void* context, uintptr_t, uint32_t, uint32_t, uint32_t) {
    ++static_cast<Fixture*>(context)->present_calls;
    return true;
}

sentinel::special::Calls calls(Fixture& fixture) {
    sentinel::special::Calls result{};
    result.context = &fixture;
    result.player = player;
    result.read = read;
    result.ensure = ensure;
    result.select = select;
    result.present = present;
    return result;
}

sc_special_request request(const char* id) {
    sc_special_request result{};
    result.kind = SC_SPECIAL_ENSURE_OWNERSHIP;
    result.own_hammer = 1;
    result.hammer_tier = SC_SPECIAL_HAMMER_TIER_UPGRADED;
    strcpy_s(result.namespace_id, id);
    return result;
}
} // namespace

int main() {
    constexpr uint32_t known = SC_SPECIAL_KNOWN_CRUCIBLE | SC_SPECIAL_KNOWN_HAMMER |
        SC_SPECIAL_KNOWN_HAMMER_PERKS | SC_SPECIAL_KNOWN_SELECTION;

    Fixture fixture{};
    fixture.facts = {1, 1, 0, SC_SPECIAL_WEAPON_CRUCIBLE, known, 0, 0};
    fixture.materialize = true;
    auto command = request("special-materialize");
    sentinel::special::reset_session(command.namespace_id);
    auto result = sentinel::special::initial(command);
    sentinel::special::execute(command, result, calls(fixture));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_OK && fixture.ensure_calls == 1);
    CHECK(result.native_crucible == 1 && result.native_hammer_perks == 0);
    CHECK(fixture.facts.hammer_loot_projected);
    CHECK(result.native_selected == SC_SPECIAL_WEAPON_CRUCIBLE);
    CHECK(result.flags & SC_SPECIAL_FLAG_SELECTION_PRESERVED);

    result = sentinel::special::initial(command);
    sentinel::special::execute(command, result, calls(fixture));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_NOOP && fixture.ensure_calls == 1);

    Fixture native{};
    native.facts = {1, 1, 2, SC_SPECIAL_WEAPON_CRUCIBLE, known, 0, 0};
    command = request("special-native-hammer-upgraded");
    sentinel::special::reset_session(command.namespace_id);
    result = sentinel::special::initial(command);
    sentinel::special::execute(command, result, calls(native));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_NATIVE_FAILED && native.ensure_calls == 1);

    Fixture ineffective{};
    ineffective.facts = {1, 1, 0, SC_SPECIAL_WEAPON_CRUCIBLE, known, 0, 0};
    command = request("special-ineffective");
    sentinel::special::reset_session(command.namespace_id);
    result = sentinel::special::initial(command);
    sentinel::special::execute(command, result, calls(ineffective));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_NATIVE_FAILED && ineffective.ensure_calls == 1);

    Fixture unavailable{};
    unavailable.facts = {1, 1, 0, SC_SPECIAL_WEAPON_CRUCIBLE, known, 0, 0};
    unavailable.ensure_result = ERROR_NOT_SUPPORTED;
    command = request("special-provider-missing");
    sentinel::special::reset_session(command.namespace_id);
    result = sentinel::special::initial(command);
    sentinel::special::execute(command, result, calls(unavailable));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_UNAVAILABLE && unavailable.ensure_calls == 1);

    Fixture invalid{};
    invalid.facts = {1, 1, 0, SC_SPECIAL_WEAPON_CRUCIBLE, known, 0, 0};
    invalid.player_present = false;
    command = request("special-invalid-context");
    sentinel::special::reset_session(command.namespace_id);
    result = sentinel::special::initial(command);
    sentinel::special::execute(command, result, calls(invalid));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_NO_PLAYER && invalid.ensure_calls == 0);

    Fixture toggle{};
    toggle.facts = {1, 1, 0, SC_SPECIAL_WEAPON_CRUCIBLE, known, 0, 0};
    sentinel::special::reset_session("special-local-toggle");
    result = sentinel::special::toggle_local("special-local-toggle", calls(toggle));
    CHECK(result.kind == SC_SPECIAL_SELECT && result.outcome == SC_SPECIAL_OUTCOME_OK);
    CHECK(result.selected == SC_SPECIAL_WEAPON_HAMMER && toggle.select_calls == 1);

    Fixture unchanged{};
    unchanged.facts = {1, 0, 0, SC_SPECIAL_WEAPON_CRUCIBLE, known, 0, 0};
    sentinel::special::reset_session("special-local-unchanged");
    result = sentinel::special::toggle_local("special-local-unchanged", calls(unchanged));
    CHECK(result.kind == SC_SPECIAL_SELECT && result.outcome == SC_SPECIAL_OUTCOME_NOOP);
    CHECK(result.selected == SC_SPECIAL_WEAPON_CRUCIBLE && unchanged.select_calls == 1);
    CHECK(result.operations_applied == 0);

    Fixture no_owned{};
    no_owned.facts = {0, 0, 0, SC_SPECIAL_WEAPON_NONE, known, 0, 0};
    sentinel::special::reset_session("special-local-no-owned");
    result = sentinel::special::toggle_local("special-local-no-owned", calls(no_owned));
    CHECK(result.kind == SC_SPECIAL_OBSERVE && result.outcome == SC_SPECIAL_OUTCOME_OK);
    CHECK(no_owned.select_calls == 0);

    Fixture refill{};
    constexpr auto refill_flags = SC_SPECIAL_REFILL_AUTHORITATIVE |
        SC_SPECIAL_REFILL_BALANCE_KNOWN | SC_SPECIAL_REFILL_CONNECTED;
    sc_special_request refill_request{};
    refill_request.kind = SC_SPECIAL_REFILL_PUBLISH;
    refill_request.refill_balance = 2;
    refill_request.refill_flags = refill_flags;
    strcpy_s(refill_request.namespace_id, "special-refill-publish");
    sentinel::special::reset_session(refill_request.namespace_id);
    result = sentinel::special::initial(refill_request);
    sentinel::special::execute(refill_request, result, calls(refill));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_OK && result.operations_applied == 1);
    CHECK(refill.present_calls == 1);
    result = sentinel::special::initial(refill_request);
    sentinel::special::execute(refill_request, result, calls(refill));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_NOOP && result.operations_applied == 1);
    CHECK(refill.present_calls == 1);
    refill_request.refill_balance = 1;
    result = sentinel::special::initial(refill_request);
    sentinel::special::execute(refill_request, result, calls(refill));
    CHECK(result.outcome == SC_SPECIAL_OUTCOME_OK && result.operations_applied == 2);
    CHECK(refill.present_calls == 2);

    // Acquisition preservation: normal weapon held -> Hammer acquisition ->
    // normal weapon restored. The decision seam carries declaration/item values
    // only; the native caller owns the qualified EquipItem call and fail-closed
    // reporting. Crucible and Hammer held share the same equip_prior path.
    constexpr uintptr_t normal_weapon = 0x1000, other_weapon = 0x2000;
    constexpr uintptr_t crucible = 0x3000, hammer = 0x4000;
    CHECK(sentinel::special::acquisition_restore(normal_weapon, normal_weapon, hammer) ==
          sentinel::special::AcquisitionRestore::equip_prior);
    CHECK(sentinel::special::acquisition_restore(crucible, crucible, hammer) ==
          sentinel::special::AcquisitionRestore::equip_prior);
    CHECK(sentinel::special::acquisition_restore(hammer, hammer, crucible) ==
          sentinel::special::AcquisitionRestore::equip_prior);
    CHECK(sentinel::special::acquisition_restore(0, 0, hammer) ==
          sentinel::special::AcquisitionRestore::none);
    CHECK(sentinel::special::acquisition_restore(hammer, hammer, hammer) ==
          sentinel::special::AcquisitionRestore::none);
    CHECK(sentinel::special::acquisition_restore(other_weapon, 0, hammer) ==
          sentinel::special::AcquisitionRestore::fail_closed);
    CHECK(sentinel::special::acquisition_restore(crucible, 0, hammer) ==
          sentinel::special::AcquisitionRestore::fail_closed);

    std::puts("PASS special Hammer first acquisition, effective readback, provider, duplicate, selection, context and held-weapon restore contracts");
}
