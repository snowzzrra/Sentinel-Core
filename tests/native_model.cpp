#include "native_model.h"
#include "protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL native model line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::native;
sc_diagnostic_request request(uint64_t id, uint32_t deadline = 100) {
    sc_diagnostic_request r{}; r.expected.pid = 7; r.expected.process_created = 13;
    r.expected.instance_id[0] = 1; r.expected.lifecycle_generation = 1;
    r.request_id = id; r.nonce[0] = 42; r.deadline_ms = deadline; return r;
}
int main() {
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
    CHECK(queue.retrieve(r, false, 125).state == SC_DIAGNOSTIC_EXPIRED);
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
    auto claimed = race.retrieve(r, true, 1002);
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
    std::puts("PASS native lifecycle, nested/free/failure/menu, same-name generation, history gaps, queue bounds/deadlines/retention, claimed cancellation race, strict bounded wire; HARNESS ONLY");
}
