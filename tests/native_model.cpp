#include "native_model.h"
#include "challenge_match.h"
#include "runes.h"
#include "special.h"
#include <Windows.h>
#include "protocol.h"
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
int main() {
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
    Lifecycle life;
    CHECK(life.generation == 0 && life.state == SC_LIFETIME_UNOBSERVED);
    life.begin(true, true, false, 10, 9);
    life.begin(false, false, false, 11, 9); // Nested primary destruction.
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
    CHECK(queue.submit(request(10), 0, 30200).state == SC_DIAGNOSTIC_QUEUED); // bounded retention, not durable exactly-once
    CHECK(queue.retrieve(request(1), false, 30200).state == SC_DIAGNOSTIC_UNKNOWN);

    // Deterministic claimed/cancel race: only the callback thread writes terminal
    // results. A cancellation while CLAIMED is a request, not a false guarantee.
    Diagnostics race;
    r = request(50, 1000); race.submit(r, 0, 1000); auto* slot = race.claim(1001); CHECK(slot);
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

    Message wire{}; uint16_t op = 0; sc_diagnostic_request decoded{};
    auto size = encode_native_request(wire, diagnostic_submit_operation, r);
    CHECK(size == 88 && decode_request(wire, size, &op, &decoded) == WireResult::ok && op == diagnostic_submit_operation);
    CHECK(same_scope(r.expected, decoded.expected) && decoded.request_id == r.request_id);
    CHECK(decode_request(wire, size - 1) == WireResult::malformed);
    wire[size] = 0; CHECK(decode_request(wire, size + 1) == WireResult::malformed);
    size = encode_native_request(wire, native_operation, {}, 123);
    uint64_t after = 0; CHECK(decode_request(wire, size, &op, nullptr, &after) == WireResult::ok && after == 123);
    Snapshot host{}; host.pid = 7; host.process_created = 13; host.instance[0] = 1;
    host.core.abi_version = SC_ABI_VERSION; strcpy_s(host.core.version, "0.5.0");
    std::memset(host.core.build_id, 'a', 64);
    sc_native_snapshot n{}; n.scope = r.expected; n.availability = SC_NATIVE_ENABLED;
    n.site_revision = n.phase = 1; n.site_rva = 0x43d1f0; n.lifecycle = SC_LIFETIME_ACTIVE;
    n.context_generation = n.scope.lifecycle_generation; n.game_state = SC_GAME_IN_GAME;
    n.current_map.validity = SC_OBSERVATION_OBSERVED; n.current_map.length = 255;
    std::memset(n.current_map.bytes, 'm', 255); n.event_sequence = 6; n.event_count = 6;
    for (uint32_t i = 0; i < 6; ++i) n.events[i] = {i + 1ull, 1, 100, SC_EVENT_CHANGE_END, SC_LIFETIME_ACTIVE, 9, 0};
    size = encode_native_response(wire, WireResult::ok, native_operation, host, n, {});
    CHECK(size && size <= max_message);
    Snapshot s{}; sc_native_snapshot roundtrip{}; sc_diagnostic_result d{}; WireResult code{};
    CHECK(decode_native_response(wire, size, code, native_operation, s, roundtrip, d));
    CHECK(roundtrip.current_map.length == 255 && roundtrip.event_count == 6);
    CHECK(!decode_native_response(wire, size - 1, code, native_operation, s, roundtrip, d));
    ++n.context_generation; CHECK(encode_native_response(wire, WireResult::ok, native_operation, host, n, {}) == 0);
    d = {}; d.scope = r.expected; d.request_id = r.request_id; std::memcpy(d.nonce, r.nonce, 16);
    d.state = SC_DIAGNOSTIC_REJECTED; d.reason = SC_NATIVE_BUDGET;
    detail = {}; detail.revision = 1; detail.stage = SC_STAGE_OBSERVATION_BUDGET;
    detail.observation_attempted = detail.timing_valid = 1;
    detail.observation_started_at_ms = 1000; detail.observation_elapsed_ns = 2000001;
    detail.observation_budget_ns = 2000000; detail.sample_reason = SC_REASON_BUDGET;
    size = encode_native_request(wire, diagnostic_detail_submit_operation, r);
    CHECK(size == 88 && decode_request(wire, size, &op, &decoded) == WireResult::ok && op == diagnostic_detail_submit_operation);
    const auto old_size = encode_native_response(wire, WireResult::ok, diagnostic_result_operation, host, {}, d);
    size = encode_native_response(wire, WireResult::ok, diagnostic_detail_result_operation, host, {}, d, detail);
    CHECK(size > old_size && size <= max_message);
    sc_diagnostic_detail restored{};
    CHECK(decode_native_response(wire, size, code, diagnostic_detail_result_operation, s, roundtrip, d, &restored));
    CHECK(restored.observation_elapsed_ns == 2000001 && restored.stage == SC_STAGE_OBSERVATION_BUDGET);
    CHECK(!decode_native_response(wire, size, code, diagnostic_result_operation, s, roundtrip, d));
    CHECK(!decode_native_response(wire, size - 1, code, diagnostic_detail_result_operation, s, roundtrip, d));
    wire[size] = 0; CHECK(!decode_native_response(wire, size + 1, code, diagnostic_detail_result_operation, s, roundtrip, d));
    ++detail.revision; CHECK(!encode_native_response(wire, WireResult::ok, diagnostic_detail_result_operation, host, {}, d, detail));
    detail = {}; detail.revision = 1; detail.stage = SC_STAGE_PENDING_BEFORE;
    detail.pending_before = 3; detail.pending_before_reason = SC_REASON_PARTIAL_READ;
    detail.pending_before_error = ERROR_PARTIAL_COPY; d.reason = SC_NATIVE_READ_FAILED;
    size = encode_native_response(wire, WireResult::ok, diagnostic_detail_result_operation, host, {}, d, detail);
    CHECK(size && decode_native_response(wire, size, code, diagnostic_detail_result_operation, s, roundtrip, d, &restored));
    CHECK(restored.pending_before_reason == SC_REASON_PARTIAL_READ && restored.pending_before_error == ERROR_PARTIAL_COPY);
    sc_special_result special_result{};
    special_result.abi_version = SC_SPECIAL_ABI_VERSION;
    special_result.execution.scope = r.expected;
    special_result.execution.state = SC_DIAGNOSTIC_REJECTED;
    special_result.kind = SC_SPECIAL_ENSURE_OWNERSHIP;
    special_result.outcome = SC_SPECIAL_OUTCOME_OK;
    special_result.owns_crucible = special_result.owns_hammer = 1;
    special_result.hammer_tier = SC_SPECIAL_HAMMER_TIER_UPGRADED;
    special_result.native_crucible = special_result.native_hammer = 1;
    special_result.native_hammer_perks = 0;
    special_result.flags = SC_SPECIAL_FLAG_HAMMER_LOOT_PROJECTED | SC_SPECIAL_FLAG_AFTER_VALID |
        SC_SPECIAL_FLAG_OWNERSHIP_CUMULATIVE;
    size = encode_special_response(wire, WireResult::ok, special_result_operation, host, special_result);
    CHECK(size && decode_special_response(wire, size, code, special_result_operation, s, special_result));
    CHECK(special_result.native_hammer_perks == 0 &&
        (special_result.flags & SC_SPECIAL_FLAG_HAMMER_LOOT_PROJECTED));
    const uint8_t projected_flags[]{0x22, 0x40, 0, 0};
    auto flag_bytes = std::search(wire.begin(), wire.begin() + size,
                                  std::begin(projected_flags), std::end(projected_flags));
    CHECK(flag_bytes != wire.begin() + size);
    flag_bytes[1] |= 0x80;
    CHECK(!decode_special_response(wire, size, code, special_result_operation, s, special_result));
    special_result.flags = SC_SPECIAL_FLAG_AFTER_VALID | SC_SPECIAL_FLAG_OWNERSHIP_CUMULATIVE;
    size = encode_special_response(wire, WireResult::ok, special_result_operation, host, special_result);
    CHECK(size && decode_special_response(wire, size, code, special_result_operation, s, special_result));
    special_result.flags |= 0x8000;
    CHECK(!encode_special_response(wire, WireResult::ok, special_result_operation, host, special_result));
    sc_arsenal_result mastery_result{};
    mastery_result.abi_version = SC_ARSENAL_ABI_VERSION;
    mastery_result.execution.scope = r.expected;
    mastery_result.execution.state = SC_DIAGNOSTIC_REJECTED;
    mastery_result.kind = SC_ARSENAL_PROJECT_MASTERY;
    mastery_result.outcome = SC_ARSENAL_OUTCOME_DEFERRED;
    mastery_result.flags = SC_ARSENAL_FLAG_DEFERRED | SC_ARSENAL_FLAG_EFFECTIVE_UNOBSERVED;
    mastery_result.masteries_ap_after = 1296;
    size = encode_arsenal_response(wire, WireResult::ok, arsenal_result_operation, host, mastery_result);
    CHECK(size && decode_arsenal_response(wire, size, code, arsenal_result_operation, s, mastery_result));
    CHECK((mastery_result.flags & SC_ARSENAL_FLAG_EFFECTIVE_UNOBSERVED) &&
        mastery_result.masteries_ap_after == 1296 && mastery_result.masteries_effective_after == 0);
    mastery_result.flags |= 0x80;
    CHECK(!encode_arsenal_response(wire, WireResult::ok, arsenal_result_operation, host, mastery_result));
    run_backup_request_contracts();
    std::puts("PASS native lifecycle, nested/free/failure/menu, same-name generation, history gaps, queue bounds/deadlines/retention, claimed cancellation race, strict bounded wire; HARNESS ONLY");
}
