#include "protocol_v2.h"
#include "inspection_server.h"
#include "native_runtime.h"
#include "pipe_io.h"
#include "fault_injection.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "wire_fixtures.h"
#include <filesystem>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL v2 line %d: %s (win32=%lu)\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
namespace {
using namespace sentinel;
using generated::Bytes;
struct Reply {
    generated::command_reply envelope{};
    v2::CommandError error{};
    generated::sc_diagnostic_result execution{};
    Bytes value;
};
Reply decode_reply(const V2Response& result) {
    CHECK(result.transport == ProbeResult::ok);
    Reply out;
    CHECK(v2::command_response(result.body, out.envelope, out.error, out.execution, out.value)); return out;
}
template<class T> Bytes encode(const T& value) {
    Bytes out; CHECK(generated::encode(value, out)); return out;
}
generated::submission submission(V2Connection& connection, const char* id) {
    const auto& scope = connection.identity(); generated::submission out{};
    out.reference.scope = {scope.pid, scope.process_created, {}, scope.lifecycle_generation};
    std::copy(std::begin(scope.instance_id), std::end(scope.instance_id), out.reference.scope.instance_id.begin());
    out.reference.namespace_id.fill('0'); out.reference.command_id = id;
    out.reference.command_type = 5; out.reference.payload_revision = 1; out.deadline_ms = 3000; return out;
}
Bytes submit_body(const generated::submission& input) {
    auto out = encode(input); const auto payload = encode(generated::diagnostic_payload{generated::diagnostic_diagnostic_payload{}});
    out.insert(out.end(), payload.begin(), payload.end()); return out;
}
Reply request(V2Connection& connection, uint16_t operation, const Bytes& bytes) {
    const auto result = connection.exchange(operation, bytes, 2000);
    if (result.transport != ProbeResult::ok) std::fprintf(stderr, "v2 operation %u transport %d error %u\n", operation, static_cast<int>(result.transport), result.win32_error);
    return decode_reply(result);
}
Reply terminal(V2Connection& connection, const generated::command_reference& reference) {
    const auto deadline = GetTickCount64() + 4000;
    for (;;) {
        const auto out = request(connection, 3, encode(reference));
        if (out.envelope.dimensions.progress == 2) return out;
        CHECK(remaining(deadline)); Sleep(1);
    }
}
void reconnect(V2Connection& connection) {
    const auto instance = current_snapshot().instance;
    CHECK(connection.connect(GetCurrentProcessId(), 2000, &instance) == ProbeResult::ok);
}
void dropped(V2Connection& connection, void (*resume)(), void (*park)(), std::atomic<uint64_t>& executions) {
    const auto original = submission(connection, "dropped");
    generated::command_reference reference;
    const auto payload = encode(generated::diagnostic_payload{generated::diagnostic_diagnostic_payload{}});
    CHECK(v2_reference(original, payload, reference));
    const auto before = executions.load();
    testing::drop_reply(2);
    CHECK(connection.exchange(2, submit_body(original), 2000).transport != ProbeResult::ok);
    reconnect(connection);
    auto out = request(connection, 3, encode(reference));
    CHECK(out.execution.state == SC_DIAGNOSTIC_QUEUED && out.error == v2::CommandError::none);
    const auto duplicate = request(connection, 2, submit_body(original));
    CHECK(duplicate.execution.admitted_at_ms == out.execution.admitted_at_ms);
    auto altered = original; ++altered.deadline_ms;
    CHECK(request(connection, 2, submit_body(altered)).error == v2::CommandError::duplicate_mismatch);
    auto wrong = reference; wrong.namespace_id[0] = 'a';
    CHECK(request(connection, 3, encode(wrong)).error == v2::CommandError::duplicate_mismatch);
    CHECK(request(connection, 5, encode(generated::ack{reference, {}})).error == v2::CommandError::not_terminal);
    resume(); out = terminal(connection, reference); park();
    CHECK(executions.load() == before + 1 && out.envelope.dimensions.execution == 1);
    testing::drop_reply(3);
    CHECK(connection.exchange(3, encode(reference), 2000).transport != ProbeResult::ok);
    reconnect(connection); out = request(connection, 3, encode(reference));
    CHECK(out.execution.state == SC_DIAGNOSTIC_EXECUTED && executions.load() == before + 1);
    const auto ack = generated::ack{reference, out.envelope.dimensions.result_token};
    auto wrong_ack = ack; ++wrong_ack.result_token[0];
    CHECK(request(connection, 5, encode(wrong_ack)).error == v2::CommandError::token_mismatch);
    testing::drop_reply(5);
    CHECK(connection.exchange(5, encode(ack), 2000).transport != ProbeResult::ok);
    reconnect(connection); out = request(connection, 3, encode(reference));
    CHECK(out.envelope.dimensions.retention == 1 && out.execution.state == SC_DIAGNOSTIC_EXECUTED);
    CHECK(request(connection, 5, encode(ack)).error == v2::CommandError::none);
    CHECK(request(connection, 2, submit_body(original)).envelope.dimensions.retention == 1);
    CHECK(executions.load() == before + 1);
}
void cancellation(V2Connection& connection, void (*resume)(), void (*park)(), HANDLE entered, HANDLE release,
                  std::atomic<unsigned>& stage, std::atomic<uint64_t>& executions) {
    auto input = submission(connection, "cancel-before");
    auto admitted = request(connection, 2, submit_body(input));
    const auto before = executions.load();
    auto cancelled = request(connection, 4, encode(admitted.envelope.reference));
    CHECK(cancelled.execution.state == SC_DIAGNOSTIC_CANCELLED && cancelled.envelope.dimensions.execution == 0 && cancelled.envelope.dimensions.effect == 0);
    input.reference.command_id = "cancel-after";
    admitted = request(connection, 2, submit_body(input));
    CHECK(ResetEvent(entered)); CHECK(ResetEvent(release)); stage.store(2); resume();
    CHECK(WaitForSingleObject(entered, 4000) == WAIT_OBJECT_0);
    cancelled = request(connection, 4, encode(admitted.envelope.reference));
    CHECK(cancelled.execution.state == SC_DIAGNOSTIC_CLAIMED && cancelled.execution.cancel_requested);
    CHECK(request(connection, 5, encode(generated::ack{admitted.envelope.reference, {}})).error == v2::CommandError::not_terminal);
    CHECK(SetEvent(release));
    const auto done = terminal(connection, admitted.envelope.reference); park();
    CHECK(done.execution.state == SC_DIAGNOSTIC_EXECUTED && done.envelope.dimensions.execution == 1 && executions.load() == before + 1);
}
HANDLE raw_pipe() {
    const auto deadline = GetTickCount64() + 2000; const auto name = pipe_name_v2(GetCurrentProcessId());
    for (;;) {
        const auto pipe = CreateFileW(name.c_str(), client_access, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) return pipe;
        CHECK(GetLastError() == ERROR_PIPE_BUSY && remaining(deadline));
        CHECK(WaitNamedPipeW(name.c_str(), remaining(deadline)));
    }
}
generated::wire_header_2 read_frame(HANDLE pipe, Bytes& body) {
    Bytes bytes(32); generated::wire_header_2 header;
    CHECK(transfer_exact(pipe, false, bytes.data(), 32, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    CHECK(v2::header(bytes, header, true)); body.resize(header.payload_length);
    CHECK(transfer_exact(pipe, false, body.data(), header.payload_length, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS); return header;
}
void framing() {
    Handle pipe(raw_pipe()); Bytes frame, body;
    generated::wire_header_2 header{generated::wire_magic, 2, 6, 0, 0, 7, 0, 0};
    CHECK(v2::frame(header, {}, frame));
    for (auto& byte : frame) CHECK(transfer_exact(pipe.value, true, &byte, 1, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    CHECK(read_frame(pipe.value, body).correlation_id == 7);
    auto combined = frame; header.correlation_id = 8; CHECK(v2::frame(header, {}, frame));
    combined.insert(combined.end(), frame.begin(), frame.end());
    CHECK(transfer_exact(pipe.value, true, combined.data(), static_cast<DWORD>(combined.size()), nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    CHECK(read_frame(pipe.value, body).correlation_id == 7); CHECK(read_frame(pipe.value, body).correlation_id == 8);
    frame[8] = 0xe1; frame[9] = 0x3f;
    CHECK(transfer_exact(pipe.value, true, frame.data(), 32, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    uint8_t byte = 0; DWORD count = 0;
    const auto error = transfer(pipe.value, false, &byte, 1, count, nullptr, 1500);
    CHECK(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED);
}
void identity(V2Connection& connection) {
    auto expected = current_snapshot().instance; ++expected[0];
    V2Connection wrong; CHECK(wrong.connect(GetCurrentProcessId(), 2000, &expected) == ProbeResult::process_mismatch);
    auto input = submission(connection, "wrong-instance"); ++input.reference.scope.instance_id[0];
    auto out = request(connection, 2, submit_body(input));
    CHECK(out.error == v2::CommandError::effect_unknown && out.envelope.actual_instance_id == current_snapshot().instance);
    input = submission(connection, "wrong-pid"); ++input.reference.scope.pid;
    CHECK(request(connection, 2, submit_body(input)).error == v2::CommandError::effect_unknown);
    input = submission(connection, "unsupported"); input.reference.payload_revision = 99;
    CHECK(request(connection, 2, submit_body(input)).error == v2::CommandError::unsupported_revision);
    reconnect(connection);
    input = submission(connection, "missing"); generated::command_reference reference;
    CHECK(v2_reference(input, encode(generated::diagnostic_payload{generated::diagnostic_diagnostic_payload{}}), reference));
    out = request(connection, 3, encode(reference));
    CHECK(out.error == v2::CommandError::not_found && out.envelope.dimensions.effect == 4);
}
void observations(V2Connection& connection) {
    const auto s = submission(connection, "unused");
    generated::read_envelope input{s.reference.scope, s.reference.namespace_id, 1, 1, {}};
    auto body = encode(input); const auto payload = encode(generated::inspect_payload{generated::inspect_inspect_payload{}});
    body.insert(body.end(), payload.begin(), payload.end());
    const auto result = connection.exchange(1, body, 2000);
    CHECK(result.transport == ProbeResult::ok && result.wire == WireResult::ok);
    generated::sc_status status; CHECK(generated::decode(Bytes(result.body.begin() + 36, result.body.end()), status));
    CHECK(status.abi_version == SC_ABI_VERSION);
    input.command_type = 18; body = encode(input);
    const auto grant = encode(generated::weapon_points_payload{generated::weapon_points_grant_payload{3, 0}});
    body.insert(body.end(), grant.begin(), grant.end());
    const auto rejected = connection.exchange(1, body, 2000);
    CHECK(rejected.transport == ProbeResult::ok && rejected.wire == WireResult::unsupported_operation);
}

void snapshot_reads(V2Connection& connection) {
    const auto s = submission(connection, "unused");
    constexpr uint16_t ids[] = {1,2,3,4,11,12,13,17,50};
    for (const auto id : ids) {
        generated::read_envelope input{s.reference.scope, s.reference.namespace_id, id, 1, {}};
        auto body = encode(input); body.insert(body.end(), 4, 0);
        if (id == 4 || id == 13) body.insert(body.end(), 8, 0);
        if (id == 50) body.insert(body.end(), 4, 0);
        if (id == 13) body[body.size() - 8] = 1;
        const auto response = connection.exchange(1, body, 2000);
        if (response.wire != WireResult::ok) std::fprintf(stderr, "snapshot %u wire %u\n", id, static_cast<uint32_t>(response.wire));
        CHECK(response.transport == ProbeResult::ok && response.wire == WireResult::ok && response.body.size() > 36);
        generated::Result result;
        const auto* descriptor = registry::descriptor(id, 1);
        CHECK(descriptor->decode_result(Bytes(response.body.begin() + 36, response.body.end()), result));
    }
}
void transient_model() {
    native::Diagnostics queue; ::sc_weapon_points_request payload{};
    std::memset(payload.namespace_id, 'a', 64);
    ::sc_diagnostic_request execution{}; execution.request_id = 1; execution.nonce[0] = 1; execution.deadline_ms = 1000;
    auto* slot = queue.transient(execution, payload, 0, 100); CHECK(slot);
    CHECK(queue.identity_count() == 0 && queue.claim(101) == slot);
    auto done = slot->result; done.state = SC_DIAGNOSTIC_EXECUTED; done.completed_at_ms = 102;
    native::Diagnostics::finish(*slot, done); native::Diagnostics::Reply reply;
    CHECK(queue.collect_transient(*slot, reply, 103));
    CHECK(reply.execution.state == SC_DIAGNOSTIC_EXECUTED && queue.identity_count() == 0 && !queue.claim(104));
    CHECK(std::get<::sc_weapon_points_result>(reply.value).execution.state == SC_DIAGNOSTIC_EXECUTED);
}
template<class Native, class Payload> Native converted(V2Connection& connection, uint16_t id, generated::Operation operation, const Payload& payload) {
    auto envelope = submission(connection, "adapter"); envelope.reference.command_type = id;
    const auto* descriptor = registry::descriptor(id, 1); CHECK(descriptor);
    generated::NativeRequest native;
    CHECK(v2::native_payload(*descriptor, encode(payload), envelope, native, operation));
    return std::get<Native>(native);
}
void adapters(V2Connection& c) {
    const auto read = generated::Operation::read, submit = generated::Operation::submit;
    CHECK(runes::valid(converted<::sc_runes_request>(c, 33, read, generated::runes_payload{generated::runes_observe_payload{}})));
    CHECK(converted<::sc_runes_request>(c, 33, submit, generated::runes_payload{generated::runes_select_normal_payload{2,-1}}).select_rune == -1);
    CHECK(converted<::sc_inventory_request>(c, 25, submit, generated::inventory_payload{generated::inventory_ensure_payload{3,4}}).equipment == 4);
    CHECK(converted<::sc_inventory_request>(c, 25, submit, generated::inventory_payload{generated::inventory_set_capacity_payload{1,2,3}}).ammo_tier == 3);
    CHECK(converted<::sc_arsenal_request>(c, 29, submit, generated::arsenal_payload{generated::arsenal_select_mod_payload{2,1}}).select_weapon == 2);
    CHECK(converted<::sc_arsenal_request>(c, 29, submit, generated::arsenal_payload{generated::arsenal_project_mastery_payload{7}}).masteries == 7);
    CHECK(converted<::sc_special_request>(c, 37, submit, generated::special_payload{generated::special_refill_execute_payload{17,1}}).refill_request_id == 17);
    CHECK(converted<::sc_deathlink_request>(c, 41, submit, generated::deathlink_payload{generated::deathlink_ack_local_payload{17}}).ack_sequence == 17);
    CHECK(converted<::sc_automap_request>(c, 45, read, generated::automap_payload{generated::automap_observe_payload{}}).kind == SC_AUTOMAP_OBSERVE);
    CHECK(commands::valid(converted<::sc_command_request>(c, 46, submit, generated::command_payload{generated::command_echo_payload{"fixture"}})));
    CHECK(commands::process_scoped(converted<::sc_command_request>(c, 52, submit, generated::support_dump_payload{generated::support_dump_support_dump_payload{}})));
}
Bytes fixture(const char* name) {
    return wire_fixtures::read(SC_WIRE_V2_FIXTURE_FILE).at(name).bytes;
}
void fixture_requests() {
    const auto expected = fixture("queued_query");
    generated::command_reference reference;
    CHECK(generated::decode(Bytes(expected.begin() + 32, expected.end()), reference));
    for (const auto name : {"submit", "matching_duplicate", "conflicting_duplicate", "wrong_namespace", "wrong_binding", "unsupported_revision"}) {
        const auto bytes = fixture(name); generated::submission input; Bytes payload;
        CHECK(v2::split_submission(Bytes(bytes.begin() + 32, bytes.end()), input, payload));
        const auto prefix = encode(input); Bytes actual = prefix; actual.insert(actual.end(), payload.begin(), payload.end());
        CHECK(Bytes(bytes.begin() + 32, bytes.end()) == actual);
        generated::weapon_points_payload decoded; CHECK(generated::decode(payload, decoded));
        CHECK(encode(decoded) == payload);
        if (std::string(name) == "submit") {
            generated::command_reference computed; CHECK(v2_reference(input, payload, computed));
            CHECK(computed.fingerprint == reference.fingerprint);
        }
    }
    for (const auto name : {"queued_query", "terminal_query", "cancel_before_entry", "cancel_after_entry"}) {
        const auto bytes = fixture(name); Bytes body(bytes.begin() + 32, bytes.end());
        generated::command_reference ref; CHECK(generated::decode(body, ref) && encode(ref) == body);
    }
    for (const auto name : {"ack", "repeated_ack"}) {
        const auto bytes = fixture(name); Bytes body(bytes.begin() + 32, bytes.end());
        generated::ack ack; CHECK(generated::decode(body, ack) && encode(ack) == body);
    }
}
void fixture_responses() {
    struct Expected { const char* name; uint32_t progress, execution, effect, retention, error; };
    const Expected cases[] = {{"queued_result",0,0,0,0,0}, {"terminal_result",2,1,1,0,0},
        {"cancelled_before_entry",2,0,0,0,0}, {"cancel_requested_after_entry",1,1,4,0,0},
        {"acked_result",2,1,1,1,0}, {"repeated_ack_result",2,1,1,1,0}, {"pruned_result",2,1,1,1,0},
        {"effect_unknown",3,2,4,2,8}, {"conflicting_duplicate_result",3,0,0,0,1}};
    for (const auto& expected : cases) {
        const auto bytes = fixture(expected.name); Bytes body(bytes.begin() + 32, bytes.end());
        generated::command_reply reply; generated::sc_diagnostic_result execution; v2::CommandError error; Bytes value;
        CHECK(v2::command_response(body, reply, error, execution, value));
        const auto& d = reply.dimensions;
        CHECK(d.progress == expected.progress && d.execution == expected.execution && d.effect == expected.effect);
        CHECK(d.retention == expected.retention && static_cast<uint32_t>(error) == expected.error);
        auto actual = encode(generated::command_response{reply, static_cast<uint32_t>(error), execution});
        if (!value.empty()) {
            generated::sc_weapon_points_result points; CHECK(generated::decode(value, points));
            CHECK(points.balance_after == 3 && encode(points) == value);
        }
        actual.insert(actual.end(), value.begin(), value.end()); CHECK(actual == body);
    }
    const auto bytes = fixture("hello_result"); generated::hello_result hello;
    const Bytes body(bytes.begin() + 32, bytes.end()); CHECK(generated::decode(body, hello));
    CHECK(hello.max_frame == 16384 && hello.supported.size() == 3 && encode(hello) == body);
}

}
void protocol_v2_contract(const wchar_t* mode, void (*park)(), void (*resume)(), HANDLE entered, HANDLE release,
                          std::atomic<unsigned>& stage, std::atomic<uint64_t>& executions) {
    using namespace sentinel;
    if (std::wcscmp(mode, L"v2")) return;
    fixture_requests(); fixture_responses(); transient_model();
    park(); V2Connection connection; reconnect(connection);
    const auto hello = connection.exchange(6, {}, 2000); generated::hello_result supported;
    CHECK(hello.transport == ProbeResult::ok && generated::decode(hello.body, supported));
    CHECK(supported.max_frame == 16384 && supported.execution_slots == 8 && supported.full_result_ms == 30000 && supported.identity_capacity == 65536);
    CHECK(supported.supported.size() == generated::descriptor_count);
    adapters(connection); framing(); identity(connection); observations(connection); reconnect(connection); snapshot_reads(connection);
    dropped(connection, resume, park, executions);
    cancellation(connection, resume, park, entered, release, stage, executions);
    connection.close(); resume();
    std::puts("PASS v2 framing, identity, read safety, duplicate/cancel/ack, dropped admission/result/ack with one native entry");
}
