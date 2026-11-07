#include "native_model.h"
#include "challenge_match.h"
#include "runes.h"
#include "special.h"
#include "inventory.h"
#include <Windows.h>
#include "sentinel_protocol_generated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL native model line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::native;
namespace sentinel::runes { Calls calls{}; }
namespace sentinel::special { Calls calls{}; }
sc_diagnostic_request request(uint64_t id, uint32_t deadline = 100) {
    sc_diagnostic_request r{}; r.expected.pid = 7; r.expected.process_created = 13;
    r.expected.instance_id[0] = 1; r.expected.lifecycle_generation = 1;
    r.request_id = id; r.nonce[0] = 42; r.deadline_ms = deadline; return r;
}
void run_backup_request_contracts();
void run_command_identity_tests();
void grenade_observation_cases(inventory::SnapshotFacts& inventory_facts, const inventory::Calls& inventory_calls, const sc_inventory_request& inventory_request) {
    inventory_facts.slayer_key = 1;
    for (const auto frag : {uint8_t{0}, uint8_t{1}, uint8_t{255}}) {
        inventory_facts.frag_grenade = frag;
        inventory_facts.ice_bomb = 0;
        auto result = sentinel::inventory::initial(inventory_request);
        sentinel::inventory::execute(inventory_request, result, inventory_calls);
        CHECK(result.outcome == SC_INV_OBSERVED && result.frag_grenade_before == frag &&
              result.frag_grenade_after == frag && result.ice_bomb_after == 0 && result.operations_applied == 0);
    }
    inventory_facts.frag_grenade = 2;
    auto invalid_inventory = sentinel::inventory::initial(inventory_request);
    sentinel::inventory::execute(inventory_request, invalid_inventory, inventory_calls);
    CHECK(invalid_inventory.outcome == SC_INV_READ_FAILED &&
          invalid_inventory.frag_grenade_after == SC_INVENTORY_UNKNOWN_ITEM);
    inventory_facts.frag_grenade = 1;
}
void inventory_observation_cases() {
    for (int stage = 0; stage <= 4; ++stage) {
        const auto mask = (1u << stage) - 1;
        CHECK(inventory::capacity_stage(mask, mask, stage) == stage);
        CHECK(inventory::capacity_stage(mask, mask, stage + 1) == 255);
    }
    CHECK(inventory::capacity_stage(5, 5, 2) == 255);
    CHECK(inventory::capacity_stage(3, 1, 2) == 255);
    CHECK(inventory::capacity_stage(0, 0, -1) == 255);
    CHECK(inventory::capacity_stage(31, 31, 5) == 255);
    sentinel::inventory::SnapshotFacts inventory_facts{};
    sentinel::inventory::Calls inventory_calls{
        &inventory_facts,
        [](void*) { return uintptr_t{0x1234}; },
        [](void* context, uintptr_t, sentinel::inventory::SnapshotFacts& facts) {
            facts = *static_cast<sentinel::inventory::SnapshotFacts*>(context); return true;
        }
    };
    sc_inventory_request inventory_request{};
    std::memset(inventory_request.namespace_id, 'a', 64);
    for (const auto dash : {uint8_t{0}, uint8_t{1}, uint8_t{255}}) {
        inventory_facts.dash = dash;
        auto result = sentinel::inventory::initial(inventory_request);
        sentinel::inventory::execute(inventory_request, result, inventory_calls);
        CHECK(result.abi_version == SC_INVENTORY_ABI_VERSION && result.outcome == SC_INV_OBSERVED &&
              result.dash_before == dash && result.dash_after == dash &&
              result.equipment_after == SC_INVENTORY_UNKNOWN_MASK);
    }
    inventory_facts.dash = 2;
    auto invalid_inventory = sentinel::inventory::initial(inventory_request);
    sentinel::inventory::execute(inventory_request, invalid_inventory, inventory_calls);
    CHECK(invalid_inventory.outcome == SC_INV_READ_FAILED &&
          invalid_inventory.dash_after == SC_INVENTORY_UNKNOWN_ITEM);
    inventory_facts.dash = 1;
    for (const auto key : {uint8_t{0}, uint8_t{1}, uint8_t{255}}) {
        inventory_facts.slayer_key = key;
        auto result = sentinel::inventory::initial(inventory_request);
        sentinel::inventory::execute(inventory_request, result, inventory_calls);
        CHECK(result.outcome == SC_INV_OBSERVED && result.slayer_key_before == key &&
              result.slayer_key_after == key && result.operations_applied == 0);
    }
    inventory_facts.slayer_key = 2;
    invalid_inventory = sentinel::inventory::initial(inventory_request);
    sentinel::inventory::execute(inventory_request, invalid_inventory, inventory_calls);
    CHECK(invalid_inventory.outcome == SC_INV_READ_FAILED &&
          invalid_inventory.slayer_key_after == SC_INVENTORY_UNKNOWN_ITEM);
    grenade_observation_cases(inventory_facts, inventory_calls, inventory_request);
    const auto microwave = 1ull << 6;
    inventory_facts.weapons = SC_INV_WEAPON_PLASMA_RIFLE;
    CHECK(!sentinel::inventory::microwave_counter(inventory_facts));
    inventory_facts.perks_known = microwave;
    CHECK(!sentinel::inventory::microwave_counter(inventory_facts));
    inventory_facts.perks_owned = microwave;
    CHECK(sentinel::inventory::microwave_counter(inventory_facts));
    inventory_facts.weapons = SC_INVENTORY_UNKNOWN_MASK;
    CHECK(!sentinel::inventory::microwave_counter(inventory_facts));
    inventory_facts.weapons = 0;
    CHECK(!sentinel::inventory::microwave_counter(inventory_facts));
    inventory_facts.flame_belch = 1;
    sentinel::inventory::SnapshotFacts stone_facts{};
    CHECK(!sentinel::inventory::stone_counter(stone_facts, false));
    CHECK(sentinel::inventory::stone_counter(stone_facts, true));
    stone_facts.weapons = SC_INV_WEAPON_CHAINSAW;
    CHECK(sentinel::inventory::stone_counter(stone_facts, false));
    stone_facts.weapons = SC_INV_WEAPON_SHOTGUN;
    stone_facts.perks_known = 1ull << 1;
    CHECK(!sentinel::inventory::stone_counter(stone_facts, false));
    stone_facts.perks_owned = 1ull << 1;
    CHECK(sentinel::inventory::stone_counter(stone_facts, false));
    stone_facts.weapons = 0;
    CHECK(!sentinel::inventory::stone_counter(stone_facts, false));
    sentinel::inventory::SnapshotFacts armored_facts{};
    CHECK(!sentinel::inventory::armored_counter(armored_facts));
    armored_facts.weapons = SC_INV_WEAPON_BALLISTA;
    CHECK(sentinel::inventory::armored_counter(armored_facts));
    armored_facts.weapons = SC_INV_WEAPON_HEAVY_CANNON;
    CHECK(!sentinel::inventory::armored_counter(armored_facts));
    armored_facts.perks_known = armored_facts.perks_owned = 1ull << 2;
    CHECK(sentinel::inventory::armored_counter(armored_facts));
    armored_facts.weapons = SC_INV_WEAPON_SHOTGUN;
    CHECK(!sentinel::inventory::armored_counter(armored_facts));
    armored_facts.perks_known = armored_facts.perks_owned = 1ull;
    CHECK(sentinel::inventory::armored_counter(armored_facts));
    armored_facts.perks_owned = 0;
    CHECK(!sentinel::inventory::armored_counter(armored_facts));
    inventory_facts.blood_punch = 0;
    inventory_facts.persistent_upgrades = 1u << SC_INV_BLOOD_PUNCH_SHIFT;
    auto punch_observation = sentinel::inventory::initial(inventory_request);
    sentinel::inventory::execute(inventory_request, punch_observation, inventory_calls);
    CHECK(sentinel::inventory::blood_punch_observed());
    sentinel::inventory::reset_session(inventory_request.namespace_id);
    CHECK(!sentinel::inventory::blood_punch_observed());
    inventory_facts.persistent_upgrades = SC_INVENTORY_UNKNOWN_MASK;
    punch_observation = sentinel::inventory::initial(inventory_request);
    sentinel::inventory::execute(inventory_request, punch_observation, inventory_calls);
    CHECK(!sentinel::inventory::blood_punch_observed());
    inventory_facts.persistent_upgrades = 0;
}
void rune_binding_case() {
    struct RuneFixture { sentinel::runes::SnapshotFacts facts{}; int bound = 0, reads = 0; bool active = true; } rune;
    sentinel::runes::calls = {
        &rune, nullptr,
        [](void* context, uintptr_t, sentinel::runes::SnapshotFacts& facts) {
            auto& state = *static_cast<RuneFixture*>(context);
            ++state.reads; facts = state.facts; return true;
        },
        [](void*, uintptr_t, uint32_t) { return 0u; },
        [](void*, uintptr_t, uint32_t) { return 0u; },
        nullptr, nullptr, nullptr, nullptr,
        [](void* context, uintptr_t) {
            auto& state = *static_cast<RuneFixture*>(context);
            ++state.bound;
            if (!state.active) return;
            state.facts.owned_support = 1u << 2;
            state.facts.selected_support = 2;
        }
    };
    sentinel::runes::bind_run_state_if_needed(0x1234, 1);
    CHECK(rune.bound == 1 && rune.reads == 2 && rune.facts.owned_support == 4 &&
          rune.facts.selected_support == 2 && rune.facts.selected_slots[0] == -1);
    sentinel::runes::reset_session(nullptr);
    rune.active = false;
    rune.facts = {};
    rune.facts.selected_support = -1;
    rune.facts.selected_slots[0] = rune.facts.selected_slots[1] = rune.facts.selected_slots[2] = -1;
    sentinel::runes::bind_run_state_if_needed(0x1234, 2);
    CHECK(rune.facts.owned_support == 0 && rune.facts.selected_support == -1);
    rune.active = true;
    Sleep(510);
    sentinel::runes::bind_run_state_if_needed(0x1234, 2);
    CHECK(rune.facts.owned_support == 4 && rune.facts.selected_support == 2 &&
          rune.facts.selected_slots[0] == -1 && rune.facts.selected_slots[1] == -1 &&
          rune.facts.selected_slots[2] == -1);

}
void challenge_suppression_cases() {
    sentinel::challenge::ScopeFacts challenge_scope{};
    challenge_scope.active = challenge_scope.admitted = challenge_scope.map_qualified =
        challenge_scope.record_mission = challenge_scope.canonical_group = true;
    challenge_scope.epoch = 1; challenge_scope.thread = challenge_scope.owner_thread = 2;
    challenge_scope.player = 3; challenge_scope.map = 4;
    sentinel::challenge::CallFacts challenge_call{};
    challenge_call.admitted = challenge_call.session_admitted = true;
    challenge_call.epoch = 1; challenge_call.thread = 2; challenge_call.player = 3; challenge_call.map = 4;
    challenge_call.return_site = challenge_call.expected_return_site = 5;
    challenge_call.currency = sentinel::challenge::sentinel_battery_currency;
    challenge_call.delta = sentinel::challenge::sentinel_battery_delta;
    CHECK(sentinel::challenge::suppression_mismatch(challenge_scope, challenge_call) == nullptr);
    CHECK(sentinel::challenge::suppression_diagnostic_candidate(challenge_scope, challenge_call));
    challenge_scope.active = false;
    CHECK(std::strcmp(sentinel::challenge::suppression_mismatch(challenge_scope, challenge_call), "active") == 0);
    CHECK(sentinel::challenge::suppression_diagnostic_candidate(challenge_scope, challenge_call));
    challenge_scope.active = true; challenge_call.return_site = 6;
    CHECK(std::strcmp(sentinel::challenge::suppression_mismatch(challenge_scope, challenge_call), "return_site") == 0);
    CHECK(sentinel::challenge::suppression_diagnostic_candidate(challenge_scope, challenge_call));
    challenge_call.return_site = 5; challenge_call.currency = 5;
    CHECK(std::strcmp(sentinel::challenge::suppression_mismatch(challenge_scope, challenge_call), "currency") == 0);
    CHECK(!sentinel::challenge::suppression_diagnostic_candidate(challenge_scope, challenge_call));
}
void lifecycle_cases() {
    Lifecycle life;
    CHECK(life.generation == 0 && life.state == SC_LIFETIME_UNOBSERVED);
    life.begin(true, true, false, 10, 9);
    life.begin(false, false, false, 11, 9); // nested primary destruction
    life.end(false, true, SC_GAME_LOADING, 12, 9);
    CHECK(life.generation == 1 && life.depth == 1 && life.state == SC_LIFETIME_TRANSITION);
    life.end(true, true, SC_GAME_IN_GAME, 13, 9);
    CHECK(life.generation == 1 && life.state == SC_LIFETIME_ACTIVE);
    // Same map name AND same allocation address are deliberately absent from
    // generation inputs; another accepted native attempt advances it regardless.
    life.begin(true, true, true, 14, 9); life.end(true, true, SC_GAME_IN_GAME, 15, 9);
    CHECK(life.generation == 2 && life.checkpoint_known && life.checkpoint);
    life.begin(true, false, false, 16, 9); life.end(true, false, SC_GAME_MAIN_MENU, 17, 9);
    CHECK(life.generation == 3 && life.state == SC_LIFETIME_FAILED && !life.checkpoint_known);
    life.begin(true, true, false, 18, 9); life.end(true, true, SC_GAME_MAIN_MENU, 19, 9);
    CHECK(life.state == SC_LIFETIME_MENU);
    life.begin(false, false, false, 20, 9); life.end(false, true, SC_GAME_MAIN_MENU, 21, 9);
    CHECK(life.generation == 5 && life.state == SC_LIFETIME_INVALID);
    const auto gen = life.generation; life.event(SC_EVENT_BACKGROUND_FREE, 22, 9); CHECK(life.generation == gen);
    for (unsigned i = 0; i < 20; ++i) life.event(SC_EVENT_BACKGROUND_FREE, 23 + i, 9);
    sc_native_snapshot page{}; life.page(page, 1);
    CHECK(page.history_gap && page.event_count == SC_NATIVE_EVENT_PAGE && page.history_overwritten);
    CHECK(page.events[0].sequence == page.history_oldest);
    life.end(true, true, SC_GAME_IN_GAME, 99, 9); CHECK(life.state == SC_LIFETIME_INVALID);

}
void claimed_cancellation_case() {
    // Deterministic claimed/cancel race: only the callback thread writes terminal
    // results. A cancellation while CLAIMED is a request, not a false guarantee.
    Diagnostics race;
    sc_diagnostic_detail detail{};
    auto r = request(50, 1000); race.submit(r, 0, 1000); auto* slot = race.claim(1001); CHECK(slot);
    std::mutex mutex; std::condition_variable cv; bool ready = false, finish = false;
    std::thread callback([&] {
        auto value = slot->result;
        { std::unique_lock<std::mutex> hold(mutex); ready = true; cv.notify_one(); cv.wait(hold, [&] { return finish; }); }
        value.state = SC_DIAGNOSTIC_EXECUTED; value.executed_at_ms = 1002; value.completed_at_ms = 1003;
        Diagnostics::finish(*slot, value);
    });
    { std::unique_lock<std::mutex> hold(mutex); cv.wait(hold, [&] { return ready; }); }
    auto claimed = race.retrieve(r, true, 1002, &detail);
    CHECK(detail.revision == 1 && detail.stage == SC_STAGE_NONE && !detail.observation_attempted);
    CHECK(claimed.state == SC_DIAGNOSTIC_CLAIMED && claimed.cancel_requested && !claimed.executed_at_ms);
    { std::lock_guard<std::mutex> hold(mutex); finish = true; cv.notify_one(); }
    callback.join();
    auto done = race.retrieve(r, false, 1004);
    CHECK(done.state == SC_DIAGNOSTIC_EXECUTED && done.cancel_requested && done.retrieved_at_ms == 1004);

}
int main() {
    inventory_observation_cases();
    rune_binding_case();
    challenge_suppression_cases();
    lifecycle_cases();
    Diagnostics queue;
    auto r = request(1);
    CHECK(queue.submit(r, SC_NATIVE_NONE, 100).state == SC_DIAGNOSTIC_QUEUED);
    CHECK(queue.submit(r, SC_NATIVE_NONE, 110).admitted_at_ms == 100);
    auto mismatch = r; ++mismatch.expected.lifecycle_generation;
    CHECK(queue.submit(mismatch, SC_NATIVE_NONE, 111).reason == SC_NATIVE_DUPLICATE_MISMATCH);
    CHECK(!same_scope(r.expected, mismatch.expected));
    CHECK(queue.retrieve(r, true, 112).state == SC_DIAGNOSTIC_CANCELLED);
    CHECK(queue.claim(113) == nullptr);
    r = request(2, 5); CHECK(queue.submit(r, 0, 120).state == SC_DIAGNOSTIC_QUEUED);
    queue.note_claim_contention();
    sc_diagnostic_detail detail{};
    CHECK(queue.retrieve(r, false, 125, &detail).state == SC_DIAGNOSTIC_EXPIRED);
    CHECK(detail.stage == SC_STAGE_QUEUE && detail.claim_lock_missed && !detail.observation_attempted);
    const auto terminal_detail = detail;
    queue.note_claim_contention(); queue.retrieve(r, false, 126, &detail);
    CHECK(std::memcmp(&terminal_detail, &detail, sizeof(detail)) == 0);
    CHECK(!queue.claim(125));
    for (uint64_t i = 3; i <= SC_DIAGNOSTIC_CAPACITY; ++i) CHECK(queue.submit(request(i), 0, 130).state == SC_DIAGNOSTIC_QUEUED);
    CHECK(queue.submit(request(9), 0, 130).reason == SC_NATIVE_QUEUE_FULL);
    queue.cancel_pending(131);
    CHECK(queue.submit(request(10), 0, 30200).state == SC_DIAGNOSTIC_QUEUED); // limited retention, not durable exactly-once
    CHECK(queue.retrieve(request(1), false, 30200).state == SC_DIAGNOSTIC_UNKNOWN);

    claimed_cancellation_case();
    run_backup_request_contracts();
    run_command_identity_tests();
    std::puts("PASS native lifecycle, nested/free/failure/menu, same-name generation, history gaps, queue bounds/deadlines/retention, claimed cancellation race, strict bounded wire; HARNESS ONLY");
}
