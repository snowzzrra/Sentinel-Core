#include "native_model.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL command identity line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
using namespace sentinel;
using native::Diagnostics;
generated::submission submission(const char* id, uint16_t type = 18) {
    generated::submission out{};
    out.reference.scope = {7, 13, {}, 1}; out.reference.scope.instance_id[0] = 1;
    out.reference.namespace_id.fill('a'); out.reference.command_id = id;
    out.reference.command_type = type; out.reference.payload_revision = 1;
    out.save_binding[0] = 9; out.deadline_ms = 1000;
    return out;
}
sc_weapon_points_request grant() {
    sc_weapon_points_request out{}; std::memset(out.namespace_id, 'a', 64);
    out.kind = SC_WUP_GRANT; out.amount = 3; return out;
}
void mismatches(Diagnostics& queue, const generated::submission& original, const Diagnostics::Reply& admitted) {
    auto payload = grant(); ++payload.amount;
    CHECK(queue.submit(original, payload, 0, 102).error == Diagnostics::Error::duplicate_mismatch);
    for (unsigned field = 0; field < 8; ++field) {
        auto altered = original;
        switch (field) {
        case 0: altered.reference.command_type = 25; break;
        case 1: ++altered.reference.payload_revision; break;
        case 2: altered.reference.namespace_id[0] = 'b'; break;
        case 3: ++altered.reference.scope.pid; break;
        case 4: ++altered.reference.scope.process_created; break;
        case 5: ++altered.reference.scope.generation; break;
        case 6: ++altered.save_binding[0]; break;
        default: ++altered.deadline_ms; break;
        }
        CHECK(queue.submit(altered, grant(), 0, 102).error == Diagnostics::Error::duplicate_mismatch);
    }
    payload = grant(); payload.namespace_id[0] = 'b';
    CHECK(queue.submit(original, payload, 0, 102).error == Diagnostics::Error::duplicate_mismatch);
    CHECK(queue.query(admitted.reference, false, 102).execution.admitted_at_ms == 100);
}
Diagnostics::Reply execute_once(Diagnostics& queue, const generated::submission& original) {
    std::mutex admission;
    std::array<Diagnostics::Reply, 16> replies{};
    std::array<std::thread, 16> threads;
    for (size_t i = 0; i < threads.size(); ++i) threads[i] = std::thread([&, i] {
        std::lock_guard<std::mutex> lock(admission);
        replies[i] = queue.submit(original, grant(), 0, 100);
    });
    for (auto& thread : threads) thread.join();
    for (const auto& reply : replies) CHECK(reply.error == Diagnostics::Error::none && reply.execution.state == SC_DIAGNOSTIC_QUEUED);
    CHECK(queue.identity_count() == 1);
    auto* slot = queue.claim(101); CHECK(slot && !queue.claim(101));
    uint32_t calls = 0, balance = 0;
    const weapon_points::Calls native_calls{&calls,
        [](void*) -> uintptr_t { return 1; },
        [](void* context, uintptr_t, uint32_t& current, uint32_t& gained) {
            current = gained = *static_cast<uint32_t*>(context) * 3; return true;
        },
        [](void* context, uintptr_t, uint32_t) { ++*static_cast<uint32_t*>(context); return 0u; },
        [](void*, uintptr_t) { return true; }};
    weapon_points::execute(slot->get<sc_weapon_points_request>(), slot->output<sc_weapon_points_result>(), native_calls);
    balance = slot->output<sc_weapon_points_result>().balance_after;
    CHECK(calls == 1 && balance == 3);
    auto result = slot->result; result.state = SC_DIAGNOSTIC_EXECUTED;
    result.executed_at_ms = 101; result.completed_at_ms = 102;
    slot->evidence = {registry::ExecutionEvidence::entered, registry::EffectEvidence::observed_effect};
    Diagnostics::finish(*slot, result);
    CHECK(!queue.claim(103) && queue.submit(original, grant(), SC_NATIVE_STALE, 103).execution.state == SC_DIAGNOSTIC_EXECUTED);
    return replies[0];
}
void retention(bool acknowledge) {
    Diagnostics queue;
    const auto original = submission(acknowledge ? "ack" : "expire");
    const auto admitted = queue.submit(original, grant(), 0, 100);
    const std::array<uint8_t, 32> expected{0xdb,0x60,0xd2,0x48,0xab,0xda,0xd0,0x63,0xcd,0x4a,0x18,0xc1,0x64,0x94,0x30,0x01,
        0xc9,0x93,0xee,0x14,0x37,0xfc,0x40,0x3f,0x81,0x7f,0xe6,0x2c,0x9f,0x74,0x1b,0x33};
    CHECK(admitted.reference.fingerprint == expected);
    CHECK(queue.ack(admitted.reference, admitted.result_token, 100).error == Diagnostics::Error::not_terminal);
    mismatches(queue, original, admitted);
    const auto done = execute_once(queue, original);
    auto bad_token = done.result_token; ++bad_token[0];
    CHECK(queue.ack(done.reference, bad_token, 103).error == Diagnostics::Error::token_mismatch);
    const auto now = acknowledge ? 104u : 30102u;
    const auto compact = acknowledge ? queue.ack(done.reference, done.result_token, now) : queue.query(done.reference, false, now);
    CHECK(compact.retention == Diagnostics::Retention::compact_terminal);
    CHECK(compact.execution.state == SC_DIAGNOSTIC_EXECUTED && compact.evidence.effect == registry::EffectEvidence::observed_effect);
    CHECK(queue.submit(original, grant(), 0, now + 1).retention == Diagnostics::Retention::compact_terminal);
    CHECK(!queue.claim(now + 1));
    CHECK(queue.ack(done.reference, done.result_token, now + 1).error == Diagnostics::Error::none);
    auto changed = done.reference; ++changed.scope.generation;
    CHECK(queue.query(changed, false, now + 1).error == Diagnostics::Error::duplicate_mismatch);
    CHECK(queue.query(done.reference, false, now + 1).execution.scope.lifecycle_generation == 1);
}
void capacity() {
    Diagnostics queue;
    const sc_diagnostic_request diagnostic{};
    Diagnostics::Reply first{};
    for (size_t i = 0; i < generated::identity_records; ++i) {
        auto value = submission(std::to_string(i).c_str(), 5);
        const auto reply = queue.submit(value, diagnostic, SC_NATIVE_STALE, 100);
        CHECK(reply.error == Diagnostics::Error::none && reply.execution.reason == SC_NATIVE_STALE);
        if (!i) first = reply;
    }
    const auto refused = queue.submit(submission("overflow", 5), diagnostic, 0, 101);
    CHECK(refused.error == Diagnostics::Error::identity_capacity && !queue.claim(101));
    CHECK(queue.identity_count() == generated::identity_records);
    CHECK(queue.query(first.reference, false, 101).execution.reason == SC_NATIVE_STALE);
    CHECK(queue.submit(submission("0", 5), diagnostic, 0, 101).execution.reason == SC_NATIVE_STALE);
}
void publication_retention() {
    Diagnostics queue;
    sc_automap_request payload{}; std::memset(payload.namespace_id, 'a', 64);
    payload.kind = SC_AUTOMAP_PUBLISH; payload.known = 1; payload.revision = 1;
    const auto original = submission("publication", 45);
    const auto admitted = queue.submit(original, payload, 0, 100);
    auto* slot = queue.claim(admitted.reference, 101); CHECK(slot);
    sc_automap_result receipt{}; receipt.outcome = SC_AUTOMAP_ACCEPTED; receipt.revision = 1;
    slot->value = receipt;
    slot->evidence = {registry::ExecutionEvidence::not_entered, registry::EffectEvidence::accepted_publication};
    auto execution = slot->result; execution.state = SC_DIAGNOSTIC_EXECUTED; execution.completed_at_ms = 102;
    Diagnostics::finish(*slot, execution);
    CHECK(std::get<sc_automap_result>(queue.query(admitted.reference, false, 102).value).revision == 1);
    const auto compact = queue.ack(admitted.reference, admitted.result_token, 103);
    CHECK(compact.evidence.execution == registry::ExecutionEvidence::not_entered && compact.evidence.effect == registry::EffectEvidence::accepted_publication);
    CHECK(queue.submit(original, payload, 0, 104).retention == Diagnostics::Retention::compact_terminal && !queue.claim(104));
}
}
void run_command_identity_tests() {
    retention(false); retention(true); capacity(); publication_retention();
    std::puts("PASS command identity concurrency, immutable submission, compact results, ack, expiry and capacity");
}
