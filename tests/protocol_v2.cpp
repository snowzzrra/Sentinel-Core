#include "protocol_v2.h"
#include "inspection_server.h"
#include "native_runtime.h"
#include "pipe_io.h"
#include "fault_injection.h"
#include "campaign_menu.h"
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
void admission_refusals(V2Connection& connection, std::atomic<uint64_t>& executions) {
    const auto before = executions.load();
    auto input = submission(connection, "not-admitted"); input.reference.command_type = 18; input.reference.namespace_id.fill('a');
    auto body = encode(input); const auto payload = encode(generated::weapon_points_payload{generated::weapon_points_grant_payload{3,0}});
    body.insert(body.end(),payload.begin(),payload.end());
    const auto refused = request(connection,2,body);
    CHECK(refused.error == v2::CommandError::none && refused.execution.state == SC_DIAGNOSTIC_REJECTED && refused.execution.reason == SC_NATIVE_BINDING_FAILED);
    CHECK(refused.envelope.dimensions.progress == 2 && refused.envelope.dimensions.execution == 0 && refused.envelope.dimensions.effect == 0);
    std::array<Reply, SC_DIAGNOSTIC_CAPACITY> held;
    for (size_t i = 0; i < held.size(); ++i) {
        const auto id = "held-" + std::to_string(i);
        held[i] = request(connection,2,submit_body(submission(connection,id.c_str())));
        CHECK(held[i].execution.state == SC_DIAGNOSTIC_QUEUED);
    }
    input = submission(connection,"queue-full");
    const auto full = request(connection,2,submit_body(input));
    CHECK(full.error == v2::CommandError::none && full.execution.state == SC_DIAGNOSTIC_REJECTED && full.execution.reason == SC_NATIVE_QUEUE_FULL);
    CHECK(full.envelope.dimensions.progress == 2 && full.envelope.dimensions.execution == 0 && full.envelope.dimensions.effect == 0);
    for (const auto& queued : held) {
        const auto cancelled = request(connection,4,encode(queued.envelope.reference));
        CHECK(cancelled.execution.state == SC_DIAGNOSTIC_CANCELLED);
        CHECK(request(connection,5,encode(generated::ack{queued.envelope.reference,cancelled.envelope.dimensions.result_token})).error == v2::CommandError::none);
    }
    CHECK(request(connection,5,encode(generated::ack{full.envelope.reference,full.envelope.dimensions.result_token})).error == v2::CommandError::none);
    CHECK(request(connection,2,submit_body(input)).execution.reason == SC_NATIVE_QUEUE_FULL);
    CHECK(request(connection,3,encode(full.envelope.reference)).envelope.dimensions.execution == 0);
    input.reference.command_id = "queue-full-replacement";
    const auto replacement = request(connection,2,submit_body(input));
    CHECK(replacement.execution.state == SC_DIAGNOSTIC_QUEUED);
    CHECK(request(connection,4,encode(replacement.envelope.reference)).execution.state == SC_DIAGNOSTIC_CANCELLED);
    CHECK(executions.load() == before);
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
void refused_v1() {
    Handle pipe(raw_pipe());
    const auto bytes = wire_fixtures::read(std::filesystem::path(SC_WIRE_V2_FIXTURE_FILE).parent_path() / "v1_refused.json").at("inspect_request").bytes;
    CHECK(transfer_exact(pipe.value, true, const_cast<uint8_t*>(bytes.data()), static_cast<DWORD>(bytes.size()), nullptr, GetTickCount64()+1000) == ERROR_SUCCESS);
    uint8_t byte = 0; DWORD count = 0;
    const auto error = transfer(pipe.value, false, &byte, 1, count, nullptr, 2000);
    CHECK(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED);
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
template<class Native, class Payload> Native converted(V2Connection& connection, uint16_t id, generated::Operation operation, const Payload& payload, uint16_t revision = 1) {
    auto envelope = submission(connection, "adapter"); envelope.reference.command_type = id;
    envelope.reference.payload_revision = revision;
    const auto* descriptor = registry::descriptor(id, revision); CHECK(descriptor);
    generated::NativeRequest native;
    CHECK(v2::native_payload(*descriptor, encode(payload), envelope, native, operation));
    return std::get<Native>(native);
}
void automap_revisions(V2Connection& c) {
    const auto submit = generated::Operation::submit;
    generated::automap_publish_payload legacy{1, 7, {1}};
    const auto legacy_request = converted<::sc_automap_request>(c, 45, submit, generated::automap_payload_revision1{legacy});
    CHECK(legacy_request.checked_locations[0] == 1 && legacy_request.checked_locations[78] == 0);
    generated::automap_publish_arena_payload arenas{}; arenas.known = 1; arenas.revision = 8;
    const auto arena_bit = 7774901u - SC_AUTOMAP_LOCATION_BASE;
    arenas.checked_locations[arena_bit/64] = uint64_t{1} << (arena_bit%64);
    const auto arena_request = converted<::sc_automap_request>(c, 45, submit, generated::automap_payload{arenas}, 2);
    CHECK(arena_request.revision == 8 && arena_request.checked_locations[arena_bit/64] == arenas.checked_locations[arena_bit/64]);
}

Bytes fixture(const char* name) {
    return wire_fixtures::read(SC_WIRE_V2_FIXTURE_FILE).at(name).bytes;
}
generated::NativeCampaign campaign_fixture(const char* name, uint16_t revision = 4, char namespace_first = 'a') {
    const auto bytes = fixture(name); generated::submission input; Bytes payload;
    CHECK(v2::split_submission(Bytes(bytes.begin() + 32, bytes.end()), input, payload));
    input.reference.payload_revision = revision;
    input.reference.namespace_id[0] = namespace_first;
    if (revision < 4) {
        if (input.reference.command_type == 23) payload.resize(12);
        else if (revision == 1) payload.resize(320);
        else if (revision == 2) payload.resize(612);
        else payload.erase(payload.begin() + 612, payload.begin() + 616);
    }
    const auto* descriptor = registry::descriptor(input.reference.command_type, revision); CHECK(descriptor);
    generated::NativeRequest native;
    CHECK(v2::native_payload(*descriptor, payload, input, native, generated::Operation::submit));
    return std::get<generated::NativeCampaign>(native);
}
void campaign_upload() {
    campaign_menu::Menu menu;
    const auto hub = campaign_fixture("campaign_row_unknown");
    const auto mission = campaign_fixture("campaign_row_known");
    const auto commit = campaign_fixture("campaign_commit_selection");
    CHECK(commit.request.count == 2 && commit.request.row.id == 2);
    CHECK(menu.request(campaign_row_operation, hub.request, true, &hub.summary, &hub.rewards).status == 0);
    const auto incomplete = menu.request(campaign_commit_operation, commit.request, true);
    CHECK(incomplete.status == SC_CAMPAIGN_REFUSED && incomplete.reason == SC_CAMPAIGN_INCOMPLETE);
    CHECK(menu.projection().count == 0);
    CHECK(menu.request(campaign_row_operation, mission.request, true, &mission.summary, &mission.rewards).status == 0);
    CHECK(menu.request(campaign_commit_operation, commit.request, true).committed_revision == 7);
    const auto wrong = campaign_fixture("campaign_commit_selection", 4, 'b');
    CHECK(wrong.request.namespace_id[0] == 'b');
    const bool same_namespace = !std::strcmp(wrong.request.namespace_id, hub.request.namespace_id);
    const auto refused = menu.request(campaign_commit_operation, wrong.request, same_namespace);
    CHECK(refused.status == SC_CAMPAIGN_REFUSED && refused.reason == SC_CAMPAIGN_SCOPE);
    CHECK(refused.committed_revision == 0 && refused.selected_id == 0);
    const auto shown = menu.projection();
    CHECK(shown.revision == 7 && shown.count == 2 && shown.focus_id == 2 && menu.focus_id() == 2);
    CHECK(shown.rewards_known[0] == 0 && shown.rewards[0].empty());
    CHECK(shown.rewards_known[1] == 1 && shown.rewards[1].size() == 1);
    CHECK(shown.rewards[1][0].location_id == 7770001);
    menu.selected(1);
    CHECK(menu.request(campaign_commit_operation, commit.request, true).status == 0 && menu.focus_id() == 1);
    for (uint16_t revision = 1; revision <= 3; ++revision) {
        const auto old = campaign_fixture("campaign_row_known", revision);
        CHECK(old.request.row.id == 2 && old.request.count == 2);
        CHECK(old.rewards.known == (revision == 3 ? 1u : 0u));
        CHECK(campaign_fixture("campaign_commit_selection", revision).request.revision == 7);
    }
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
    fixture_requests(); fixture_responses(); transient_model(); campaign_upload();
    park(); V2Connection connection; reconnect(connection);
    const auto hello = connection.exchange(6, {}, 2000); generated::hello_result supported;
    CHECK(hello.transport == ProbeResult::ok && generated::decode(hello.body, supported));
    CHECK(supported.max_frame == 16384 && supported.execution_slots == 8 && supported.full_result_ms == 30000 && supported.identity_capacity == 65536);
    CHECK(supported.supported.size() == generated::descriptor_count);
    automap_revisions(connection); refused_v1(); framing(); identity(connection); observations(connection); reconnect(connection);
    admission_refusals(connection, executions);
    dropped(connection, resume, park, executions);
    cancellation(connection, resume, park, entered, release, stage, executions);
    connection.close(); resume();
    std::puts("PASS v2 framing, identity, read safety, duplicate/cancel/ack, dropped admission/result/ack with one native entry");
}
