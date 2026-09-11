#include "native_model.h"
#include "protocol.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL backup request line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::native;
namespace {
sc_save_backup_request request(uint64_t id = 77) {
    sc_save_backup_request r{}; auto& e = r.execution;
    e.expected.pid = 7; e.expected.process_created = 13; e.expected.instance_id[0] = 1;
    e.expected.lifecycle_generation = 1; e.request_id = id; e.nonce[0] = 42; e.deadline_ms = 1000;
    std::memset(r.namespace_id, 'a', 64); r.campaign = 2; r.slot = 11; r.work_deadline_ms = 10000;
    return r;
}
sc_diagnostic_result execution(Diagnostics::Slot& slot) {
    auto e = slot.result;
    e.observed_at_ms = e.executed_at_ms = e.claimed_at_ms;
    e.thread_id = 9; e.site_revision = e.phase = 1; e.lifecycle = SC_LIFETIME_ACTIVE; e.game_state = SC_GAME_IN_GAME;
    e.current_map.validity = SC_OBSERVATION_OBSERVED; strcpy_s(e.current_map.bytes, "fixture/map");
    e.current_map.length = 11;
    return e;
}
}
void run_backup_request_contracts() {
    const auto r = request();
    Diagnostics queue;
    CHECK(queue.submit(r.execution, 0, 100, nullptr, &r).state == SC_DIAGNOSTIC_QUEUED);
    CHECK(queue.submit(r.execution, 0, 101).reason == SC_NATIVE_DUPLICATE_MISMATCH);
    CHECK(queue.retrieve(r.execution, true, 101).state == SC_DIAGNOSTIC_UNKNOWN);
    auto changed = r; ++changed.slot;
    CHECK(queue.submit(changed.execution, 0, 101, nullptr, &changed).reason == SC_NATIVE_DUPLICATE_MISMATCH);
    CHECK(queue.backup_result(changed, true, 101).state == SC_BACKUP_UNKNOWN);
    CHECK(queue.submit(r.execution, SC_NATIVE_STOPPED, 102, nullptr, &r).admitted_at_ms == 100);
    auto* slot = queue.claim(103); CHECK(slot && slot->is_backup && slot->backup);
    const auto job = slot->backup; CHECK(job->bind(91));
    const auto before = queue.backup_result(r, false, 103);
    CHECK(before.state == SC_BACKUP_CLAIMED && !before.operation_id && !before.execution.executed_at_ms);
    auto e = execution(*slot);
    Diagnostics::await_backup(*slot, e, {}, {91, 0, true, true, true});
    auto pending = queue.backup_result(r, false, 104);
    CHECK(pending.state == SC_BACKUP_WAITING_NATIVE && pending.operation_id == 91 && pending.execution.executed_at_ms == 103);
    // One shared capacity: outstanding backup survives both admission expiration
    // and ordinary-result retention, while seven diagnostic slots remain usable.
    for (uint64_t i = 0; i < 7; ++i) CHECK(queue.submit(request(i + 100).execution, 0, 104).state == SC_DIAGNOSTIC_QUEUED);
    CHECK(queue.submit(request(200).execution, 0, 104).reason == SC_NATIVE_QUEUE_FULL);
    CHECK(queue.backup_result(r, true, 50000).state == SC_BACKUP_WAITING_NATIVE);
    CHECK(job->progress().cancel_requested);
    CHECK(queue.backup_result(r, false, 90000).state == SC_BACKUP_WAITING_NATIVE);
    std::thread completion([job] { job->readback_finished(false); }); completion.join();
    auto failed = queue.backup_result(r, false, 90001);
    CHECK(failed.state == SC_BACKUP_FAILED && failed.failure == SC_BACKUP_FAILURE_NATIVE);
    CHECK(failed.execution.state == SC_DIAGNOSTIC_EXECUTED && failed.execution.completed_at_ms == 90001);
    CHECK(failed.flags & SC_BACKUP_CANCEL_REQUESTED);
    CHECK(queue.backup_result(r, false, 120001).state == SC_BACKUP_UNKNOWN);
    CHECK(queue.submit(r.execution, 0, 120002).state == SC_DIAGNOSTIC_QUEUED);
    CHECK(queue.backup_result(r, true, 120003).state == SC_BACKUP_UNKNOWN); // New diagnostic is not the retired backup.
    CHECK(queue.retrieve(r.execution, false, 120003).state == SC_DIAGNOSTIC_QUEUED);

    for (unsigned mode = 0; mode < 4; ++mode) {
        Diagnostics q; auto value = request(mode + 1);
        CHECK(q.submit(value.execution, 0, 100, nullptr, &value).state == SC_DIAGNOSTIC_QUEUED);
        if (mode == 0) CHECK(q.backup_result(value, true, 101).state == SC_BACKUP_CANCELLED && !q.claim(102));
        if (mode == 1) CHECK(q.backup_result(value, false, 1100).state == SC_BACKUP_EXPIRED && !q.claim(1101));
        if (mode == 2) { q.cancel_pending(101); CHECK(q.backup_result(value, false, 102).state == SC_BACKUP_CANCELLED); }
        if (mode == 3) {
            auto* s = q.claim(101); const auto owned = s->backup;
            q.cancel_pending(102); CHECK(owned->progress().cancel_requested);
            auto result = s->result; result.state = SC_DIAGNOSTIC_CANCELLED; result.reason = SC_NATIVE_CANCELLED; result.completed_at_ms = 103;
            Diagnostics::finish(*s, result);
            CHECK(q.backup_result(value, false, 104).state == SC_BACKUP_CANCELLED && owned->progress().readback_terminal);
        }
    }
    Message wire{}; sc_save_backup_request decoded{}; uint16_t op = 0;
    const auto size = encode_backup_request(wire, save_backup_submit_operation, r);
    CHECK(size == 165 && decode_request(wire, size, &op, nullptr, nullptr, nullptr, &decoded) == WireResult::ok);
    CHECK(op == save_backup_submit_operation && decoded.slot == 11 && decoded.campaign == 2 && decoded.execution.request_id == 77);
    CHECK(decode_request(wire, size - 1) == WireResult::malformed);
    CHECK(decode_request(wire, size + 1) == WireResult::malformed);
    for (unsigned mode = 0; mode < 7; ++mode) {
        auto bad = r;
        if (mode == 0) bad.namespace_id[0] = 'A';
        if (mode == 1) bad.namespace_id[64] = 'a';
        if (mode == 2) bad.slot = 12;
        if (mode == 3) bad.campaign = 3;
        if (mode == 4) bad.work_deadline_ms = SC_SAVE_BACKUP_MAX_WORK_MS + 1;
        if (mode == 5) bad.execution.request_id = 0;
        if (mode == 6) std::memset(bad.execution.nonce, 0, 16);
        CHECK(decode_request(wire, encode_backup_request(wire, save_backup_submit_operation, bad)) == WireResult::malformed);
    }
    Snapshot host{}; host.pid = 7; host.process_created = 13; host.instance[0] = 1;
    host.core.abi_version = SC_ABI_VERSION; strcpy_s(host.core.version, "0.6.0"); std::memset(host.core.build_id, 'a', 64);
    WireResult code{}; Snapshot restored{}; sc_save_backup_snapshot value{};
    for (const auto& record : {before, pending, failed}) {
        const auto length = encode_backup_response(wire, WireResult::ok, save_backup_result_operation, host, record);
        CHECK(length && length <= max_message);
        CHECK(decode_backup_response(wire, length, code, save_backup_result_operation, restored, value));
        CHECK(value.state == record.state && value.operation_id == record.operation_id);
        CHECK(!decode_backup_response(wire, length - 1, code, save_backup_result_operation, restored, value));
        CHECK(!decode_backup_response(wire, length, code, save_backup_cancel_operation, restored, value));
    }
    failed.flags |= 256u;
    CHECK(!encode_backup_response(wire, WireResult::ok, save_backup_result_operation, host, failed));
    std::puts("PASS shared backup request capacity, ownership handoff, cross-kind isolation, cancellation, retirement and strict wire");
}
