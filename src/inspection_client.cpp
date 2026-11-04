#include "sentinel_inspection.h"
#include "pipe_io.h"
#include "protocol_v2.h"
#include "client_payload.h"
#include "commands.h"
#include "combat.h"
#include <algorithm>
#include <cstring>
#include <type_traits>

namespace sentinel {
Inspection query_native_client(uint32_t, uint32_t, uint16_t, const sc_diagnostic_request&, uint64_t,
    generated::NativeRequest, const sc_campaign_summary*, const sc_campaign_rewards*, uint64_t);
namespace {
ProbeResult classify(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_PARAMETER:
        return ProbeResult::endpoint_absent;
    case ERROR_ACCESS_DENIED: return ProbeResult::access_denied;
    case ERROR_TIMEOUT: case ERROR_SEM_TIMEOUT: return ProbeResult::timeout;
    case ERROR_MORE_DATA: return ProbeResult::invalid_response;
    default: return ProbeResult::io_error;
    }
}
struct ClientRequest {
    uint16_t operation;
    uint64_t capability;
    sc_diagnostic_request execution{};
    uint64_t cursor = 0;
    generated::NativeRequest payload{};
    const sc_campaign_summary* summary = nullptr;
    const sc_campaign_rewards* rewards = nullptr;
};
void io_failure(Inspection& result, HANDLE process, DWORD error) {
    result.win32_error = error;
    result.result = classify(error);
    if (!process) return;
    const auto state = WaitForSingleObject(process, 0);
    result.target_state = state == WAIT_OBJECT_0 ? "exited" : (state == WAIT_TIMEOUT ? "live" : "unknown");
    if (state == WAIT_FAILED) result.target_wait_error = GetLastError();
}
bool identify_process(uint32_t pid, const sc_diagnostic_request* expected, Handle& process, Inspection& result) {
    result.failure_stage = "open_process";
    process.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) { io_failure(result, nullptr, GetLastError()); return false; }
    result.failure_stage = "process_alive";
    const auto wait = WaitForSingleObject(process.value, 0);
    if (wait == WAIT_FAILED) { io_failure(result, process.value, GetLastError()); return false; }
    if (wait == WAIT_OBJECT_0) { io_failure(result, process.value, ERROR_FILE_NOT_FOUND); return false; }
    result.failure_stage = "process_creation";
    if (!process_time(process.value, result.verified_process_created)) {
        io_failure(result, process.value, GetLastError()); return false;
    }
    if (expected && (expected->expected.pid != pid || expected->expected.process_created != result.verified_process_created)) {
        result.result = ProbeResult::process_mismatch; return false;
    }
    wchar_t path[32768]{}; DWORD path_size = static_cast<DWORD>(std::size(path));
    result.failure_stage = "process_image";
    if (!QueryFullProcessImageNameW(process.value, 0, path, &path_size)) {
        io_failure(result, process.value, GetLastError()); return false;
    }
    result.host_path.assign(path, path_size);
    return true;
}
DWORD open_endpoint(uint32_t pid, uint64_t deadline, Handle& pipe, Inspection& result) {
    const auto name = pipe_name_v2(pid);
    for (;;) {
        result.failure_stage = "pipe_open";
        pipe.value = CreateFileW(name.c_str(), client_access, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe) break;
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY) return error;
        const DWORD wait = remaining(deadline);
        if (!wait) return ERROR_TIMEOUT;
        result.failure_stage = "pipe_wait";
        if (!WaitNamedPipeW(name.c_str(), wait)) return GetLastError();
    }
    ULONG server_pid = 0;
    result.failure_stage = "pipe_server_identity";
    if (!GetNamedPipeServerProcessId(pipe.value, &server_pid)) return GetLastError();
    result.server_pid = server_pid;
    return ERROR_SUCCESS;
}
bool live_process(HANDLE process, Inspection& result) {
    const auto wait = WaitForSingleObject(process, 0);
    if (wait == WAIT_FAILED) result.target_wait_error = GetLastError();
    return wait == WAIT_TIMEOUT;
}

Inspection query_operation(uint32_t pid, uint32_t timeout_ms, const ClientRequest& request) {
    Inspection out;
    if (!pid || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms) { out.result = ProbeResult::usage; return out; }
    out = query_native_client(pid, timeout_ms, request.operation, request.execution, request.cursor,
                              request.payload, request.summary, request.rewards, request.capability);
    if (out.result == ProbeResult::ok) out.failure_stage = "none";
    return out;
}

template<class T>
Inspection query_family(uint32_t pid, uint32_t timeout_ms, uint16_t operation, uint16_t first, uint16_t last,
                        uint64_t capability, const T& request) {
    if (operation < first || operation > last) { Inspection result; result.result = ProbeResult::usage; return result; }
    return query_operation(pid, timeout_ms, {operation, capability, request.execution, 0, request});
}
}
Inspection query(uint32_t pid, uint32_t timeout_ms, uint64_t required) {
    return query_operation(pid, timeout_ms, {inspect_operation, required});
}
Inspection query_engine(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, {engine_operation, engine_capability});
}
Inspection query_context(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, {context_operation, context_capability});
}
Inspection query_save(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, {save_operation, save_capability});
}
Inspection query_save_admission(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, {save_admission_operation, save_admission_capability});
}
Inspection query_save_installation(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, {save_installation_operation, save_installation_capability});
}
Inspection query_save_write(uint32_t pid, uint32_t timeout_ms, uint64_t operation_id) {
    return query_operation(pid, timeout_ms, {save_write_operation, save_write_capability, {}, operation_id});
}
Inspection query_native(uint32_t pid, uint32_t timeout_ms, uint64_t after_event) {
    return query_operation(pid, timeout_ms, {native_operation, native_capability, {}, after_event});
}
Inspection query_save_backup(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_save_backup_request& request) {
    return query_family(pid, timeout_ms, operation, save_backup_submit_operation, save_backup_cancel_operation,
                        save_backup_capability, request);
}
Inspection query_weapon_points(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_weapon_points_request& request) {
    return query_family(pid, timeout_ms, operation, weapon_points_submit_operation, weapon_points_release_operation,
                        weapon_points_capability, request);
}
Inspection query_inventory(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_inventory_request& request) {
    return query_family(pid, timeout_ms, operation, inventory_submit_operation, inventory_release_operation,
                        inventory_capability, request);
}
Inspection query_arsenal(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_arsenal_request& request) {
    return query_family(pid, timeout_ms, operation, arsenal_submit_operation, arsenal_release_operation,
                        arsenal_capability, request);
}
Inspection query_runes(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_runes_request& request) {
    return query_family(pid, timeout_ms, operation, runes_submit_operation, runes_release_operation,
                        runes_capability, request);
}
Inspection query_special(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_special_request& request) {
    return query_family(pid, timeout_ms, operation, special_submit_operation, special_release_operation,
                        special_capability, request);
}
Inspection query_deathlink(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_deathlink_request& request) {
    return query_family(pid, timeout_ms, operation, deathlink_submit_operation, deathlink_release_operation,
                        deathlink_capability, request);
}
Inspection query_command(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_command_request& request) {
    if (!commands::valid(request)) { Inspection result; result.result = ProbeResult::usage; return result; }
    return query_family(pid, timeout_ms, operation, command_submit_operation, command_release_operation,
                        command_capability, request);
}
Inspection query_campaign(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_campaign_request& request,
                          const sc_campaign_summary* summary, const sc_campaign_rewards* rewards) {
    if (operation < campaign_row_operation || operation > campaign_inspect_operation) {
        Inspection result; result.result = ProbeResult::usage; return result;
    }
    const auto capability = rewards ? campaign_rewards_capability : (summary ? campaign_presentation_capability : campaign_menu_capability);
    return query_operation(pid, timeout_ms, {operation, capability, request.execution, 0, CampaignRequest{request}, summary, rewards});
}
Inspection query_diagnostic(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_diagnostic_request& request) {
    if (operation < diagnostic_submit_operation || operation > diagnostic_detail_cancel_operation) {
        Inspection result; result.result = ProbeResult::usage; return result;
    }
    return query_operation(pid, timeout_ms, {operation, diagnostic_capability, request});
}
Inspection query_automap(uint32_t pid, uint32_t timeout_ms, const sc_automap_request& request) {
    return query_operation(pid, timeout_ms, {automap_operation, automap_capability, request.execution, 0, request});
}
Inspection query_combat_presentation(uint32_t pid, uint32_t timeout_ms, const sc_combat_presentation_request& request) {
    return query_operation(pid, timeout_ms, {combat_presentation_operation, combat_presentation_capability, request.execution, 0, request});
}
Inspection query_combat(uint32_t pid, uint32_t timeout_ms, const sc_combat_query& request) {
    if (!combat::valid_query(request)) { Inspection result; result.result = ProbeResult::usage; return result; }
    ClientRequest value{combat_operation, combat_capability}; value.payload = request;
    return query_operation(pid, timeout_ms, value);
}
}
namespace sentinel {
namespace {
bool v2_same_scope(const generated::scope& scope, const sc_native_scope& expected) {
    return scope.pid == expected.pid && scope.process_created == expected.process_created &&
        std::equal(scope.instance_id.begin(), scope.instance_id.end(), expected.instance_id);
}
bool v2_expected_reference(uint16_t operation, const generated::Bytes& body, generated::command_reference& reference) {
    if (operation == 2) {
        generated::submission submission; generated::Bytes payload;
        if (!v2::split_submission(body, submission, payload)) return false;
        v2_reference(submission, payload, reference); return true;
    }
    if (operation == 5) { generated::ack ack; if (!generated::decode(body, ack)) return false; reference = ack.reference; return true; }
    return generated::decode(body, reference);
}
bool v2_verify(uint16_t operation, const generated::Bytes& request, const generated::Bytes& response, const sc_native_scope& identity) {
    if (operation == 6) {
        generated::hello_result hello;
        return generated::decode(response, hello) && v2_same_scope(hello.scope, identity);
    }
    if (operation == 1) {
        if (response.size() < 36) return false;
        generated::scope scope;
        return generated::decode(generated::Bytes(response.begin(), response.begin() + 36), scope) && v2_same_scope(scope, identity);
    }
    generated::command_reference expected; generated::command_reply reply; generated::sc_diagnostic_result execution;
    v2::CommandError error; generated::Bytes value, left, right;
    if (!v2_expected_reference(operation, request, expected) || !v2::command_response(response, reply, error, execution, value)) return false;
    return std::equal(reply.actual_instance_id.begin(), reply.actual_instance_id.end(), identity.instance_id) &&
        generated::encode(expected, left) && generated::encode(reply.reference, right) && left == right;
}
DWORD v2_exchange_bytes(HANDLE pipe, generated::Bytes& bytes, generated::wire_header_2& header, uint64_t deadline) {
    auto error = transfer_exact(pipe, true, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, deadline);
    if (error != ERROR_SUCCESS) return error;
    bytes.resize(32);
    DWORD count = 0;
    error = transfer(pipe, false, bytes.data(), 1, count, nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return error;
    if (count != 1) return ERROR_BROKEN_PIPE;
    const auto frame_deadline = std::min<uint64_t>(deadline, GetTickCount64() + 1000);
    error = transfer_exact(pipe, false, bytes.data() + 1, 31, nullptr, frame_deadline);
    if (error != ERROR_SUCCESS) return error;
    if (!v2::header(bytes, header, true)) return ERROR_INVALID_DATA;
    bytes.resize(header.payload_length);
    return transfer_exact(pipe, false, bytes.data(), header.payload_length, nullptr, frame_deadline);
}
}
bool v2_reference(const generated::submission& submission, const generated::Bytes& payload, generated::command_reference& reference) {
    const auto& input = submission.reference;
    reference = {input.scope, input.namespace_id, input.command_id, input.command_type, input.payload_revision, {}};
    const auto end = generated::descriptors + generated::descriptor_count;
    const auto found = std::find_if(generated::descriptors, end, [&](const auto& v) { return v.id == input.command_type && v.revision == input.payload_revision; });
    const auto* descriptor = found == end ? nullptr : found;
    generated::NativeRequest value;
    return descriptor && v2::native_payload(*descriptor, payload, submission, value, generated::Operation::submit) &&
        v2::fingerprint(submission, value, reference.fingerprint);
}
V2Connection::~V2Connection() { close(); }
void V2Connection::close() {
    if (pipe_ && CloseHandle(pipe_)) pipe_ = nullptr;
    if (process_ && CloseHandle(process_)) process_ = nullptr;
}
ProbeResult V2Connection::connect(uint32_t pid, uint32_t timeout_ms, const std::array<uint8_t, 16>* expected_instance) {
    close();
    error_ = 0;
    if (!pid || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms) return ProbeResult::usage;
    const auto deadline = GetTickCount64() + timeout_ms;
    Handle process, pipe; Inspection result;
    if (!identify_process(pid, nullptr, process, result)) return result.result;
    const auto error = open_endpoint(pid, deadline, pipe, result);
    if (error != ERROR_SUCCESS) { error_ = error; return classify(error); }
    if (result.server_pid != pid || !live_process(process.value, result)) return ProbeResult::process_mismatch;
    pipe_ = pipe.value; pipe.value = nullptr; process_ = process.value; process.value = nullptr;
    identity_ = {}; identity_.pid = pid; identity_.process_created = result.verified_process_created;
    generated::Bytes bytes; generated::wire_header_2 header{generated::wire_magic, 2, 6, 0, 0, ++correlation_, 0, 0};
    if (!v2::frame(header, {}, bytes)) { close(); return ProbeResult::invalid_response; }
    generated::wire_header_2 response{};
    const auto io = v2_exchange_bytes(pipe_, bytes, response, deadline);
    if (io != ERROR_SUCCESS) { error_ = io; close(); return classify(io); }
    return accept_hello(bytes, response.operation, response.correlation_id, response.result, expected_instance);
}
ProbeResult V2Connection::accept_hello(const std::vector<uint8_t>& bytes, uint16_t operation, uint64_t correlation, uint32_t wire, const std::array<uint8_t, 16>* expected_instance) {
    Inspection result;
    generated::hello_result hello;
    if (operation != 6 || correlation != correlation_ || wire || !generated::decode(bytes, hello)) {
        close(); return ProbeResult::invalid_response;
    }
    if (hello.scope.pid != identity_.pid || hello.scope.process_created != identity_.process_created ||
        (expected_instance && hello.scope.instance_id != *expected_instance) || !live_process(process_, result)) {
        close(); return ProbeResult::process_mismatch;
    }
    std::copy(hello.scope.instance_id.begin(), hello.scope.instance_id.end(), identity_.instance_id);
    identity_.lifecycle_generation = hello.scope.generation;
    return ProbeResult::ok;
}
V2Response V2Connection::exchange(uint16_t operation, const generated::Bytes& body, uint32_t timeout_ms) {
    V2Response result;
    if (!pipe_ || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms) { result.transport = ProbeResult::usage; return result; }
    generated::Bytes bytes;
    generated::wire_header_2 header{generated::wire_magic, 2, operation, 0, 0, ++correlation_, 0, 0}, response{};
    if (!v2::frame(header, body, bytes)) { result.transport = ProbeResult::usage; return result; }
    const auto error = v2_exchange_bytes(pipe_, bytes, response, GetTickCount64() + timeout_ms);
    if (error != ERROR_SUCCESS) { result.win32_error = error; result.transport = classify(error); close(); return result; }
    Inspection state;
    if (!live_process(process_, state)) { result.transport = ProbeResult::process_mismatch; close(); return result; }
    if (response.operation != operation || response.correlation_id != correlation_ || response.result > 4 ||
        !v2_verify(operation, body, bytes, identity_)) { result.transport = ProbeResult::invalid_response; close(); return result; }
    result.transport = ProbeResult::ok; result.wire = static_cast<WireResult>(response.result); result.body = std::move(bytes); return result;
}
}

namespace sentinel {
namespace {
struct ClientRoute { uint16_t selector, type, operation; };
constexpr ClientRoute client_routes[] = {
    {1, 1, 1},
    {2, 2, 1},
    {3, 3, 1},
    {4, 4, 1},
    {5, 5, 2},
    {6, 5, 3},
    {7, 5, 4},
    {8, 8, 2},
    {9, 8, 3},
    {10, 8, 4},
    {11, 11, 1},
    {12, 12, 1},
    {13, 13, 1},
    {14, 14, 2},
    {15, 14, 3},
    {16, 14, 4},
    {17, 17, 1},
    {18, 18, 2},
    {19, 18, 3},
    {20, 18, 4},
    {21, 18, 5},
    {22, 22, 2},
    {23, 23, 2},
    {24, 24, 1},
    {25, 25, 2},
    {26, 25, 3},
    {27, 25, 4},
    {28, 25, 5},
    {29, 29, 2},
    {30, 29, 3},
    {31, 29, 4},
    {32, 29, 5},
    {33, 33, 2},
    {34, 33, 3},
    {35, 33, 4},
    {36, 33, 5},
    {37, 37, 2},
    {38, 37, 3},
    {39, 37, 4},
    {40, 37, 5},
    {41, 41, 2},
    {42, 41, 3},
    {43, 41, 4},
    {44, 41, 5},
    {45, 45, 2},
    {46, 46, 2},
    {47, 46, 3},
    {48, 46, 4},
    {49, 46, 5},
    {50, 50, 1},
    {51, 51, 2},
};
bool read_value(V2Connection& connection, uint16_t type, const generated::Bytes& payload,
                const generated::submission& submission, uint32_t timeout, Inspection& out) {
    generated::Bytes body;
    generated::read_envelope envelope{submission.reference.scope, submission.reference.namespace_id,
        type, submission.reference.payload_revision, submission.save_binding};
    if (!generated::encode(envelope, body)) { out.result = ProbeResult::usage; return false; }
    body.insert(body.end(), payload.begin(), payload.end());
    const auto response = connection.exchange(1, body, timeout);
    out.result = client_transport(response); out.win32_error = response.win32_error;
    if (out.result != ProbeResult::ok) return false;
    if (response.body.size() < 36 || !client_result(type, generated::Bytes(response.body.begin() + 36, response.body.end()), out)) {
        out.result = ProbeResult::invalid_response; return false;
    }
    return true;
}
}
void client_execution(uint16_t type, const sc_diagnostic_result& execution, Inspection& out) {
    switch (type) {
    case 5: case 8: out.diagnostic = execution; break;
    case 14:
        out.backup.execution = execution;
        if (!out.backup.state) out.backup.state = execution.state == SC_DIAGNOSTIC_REJECTED ? SC_BACKUP_REJECTED : SC_BACKUP_UNKNOWN;
        break;
    case 18: out.weapon_points.execution = execution; break;
    case 25: out.inventory.execution = execution; break;
    case 29: out.arsenal.execution = execution; break;
    case 33: out.runes.execution = execution; break;
    case 37: out.special.execution = execution; break;
    case 41: out.deathlink.execution = execution; break;
    case 46: out.command.execution = execution; break;
    default: break;
    }
}
namespace {
bool prepare_client(uint16_t type, uint32_t pid, const sc_native_scope& scope, const sc_diagnostic_request& execution,
        uint64_t cursor, generated::NativeRequest value, const sc_campaign_summary* summary,
        const sc_campaign_rewards* rewards, Inspection& out, generated::submission& input, generated::Bytes& payload) {
    input.reference.scope = {pid, scope.process_created, out.snapshot.instance, scope.lifecycle_generation};
    input.reference.namespace_id.fill('0'); input.reference.command_type = type; input.reference.payload_revision = 1;
    input.deadline_ms = execution.deadline_ms ? execution.deadline_ms : 1000;
    std::array<uint8_t,16> nonce{}; std::copy(std::begin(execution.nonce), std::end(execution.nonce), nonce.begin());
    input.reference.command_id = instance_text(nonce) + ":" + std::to_string(execution.request_id);
    if (execution.expected.pid) input.reference.scope.generation = execution.expected.lifecycle_generation;
    if (type == 5 || type == 8) value = execution;
    if (std::holds_alternative<std::monostate>(value)) value = generated::NativeRead{cursor};
    if (auto* campaign = std::get_if<CampaignRequest>(&value)) {
        if (summary) campaign->summary = *summary;
        if (rewards) campaign->rewards = *rewards;
        input.reference.payload_revision = 4;
    }
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, CampaignRequest>)
            std::copy_n(v.request.namespace_id, 64, input.reference.namespace_id.begin());
        else if constexpr (!std::is_same_v<T,std::monostate> && !std::is_same_v<T,generated::NativeRead> && !std::is_same_v<T,sc_diagnostic_request> && !std::is_same_v<T,sc_combat_presentation_request>)
            std::copy_n(v.namespace_id, 64, input.reference.namespace_id.begin());
    }, value);
    if (!client_payload(type, input.reference.payload_revision, value, payload)) return false;
    return true;
}
bool client_binding(V2Connection& connection, generated::submission& input, uint32_t timeout, Inspection& out) {
    generated::Bytes admission_payload; generated::encode(generated::save_admission_payload{generated::save_admission_save_admission_payload{}}, admission_payload);
    generated::read_envelope admission{input.reference.scope, {}, 12, 1, {}}; admission.namespace_id.fill('0');
    generated::Bytes admission_body; generated::encode(admission, admission_body);
    admission_body.insert(admission_body.end(), admission_payload.begin(), admission_payload.end());
    const auto admitted = connection.exchange(1, admission_body, timeout); generated::save_admission_result binding;
    if (admitted.transport != ProbeResult::ok || admitted.body.size() < 36 ||
        !generated::decode(generated::Bytes(admitted.body.begin()+36, admitted.body.end()), binding)) { out.result = admitted.transport == ProbeResult::ok ? ProbeResult::invalid_response : admitted.transport; return false; }
    input.save_binding = binding.save_binding;
    return true;
}
bool client_is_read(const generated::submission& input, const generated::Bytes& payload) {
    const auto* descriptor = generated::descriptors;
    const auto descriptor_end = descriptor + generated::descriptor_count;
    descriptor = std::find_if(descriptor, descriptor_end, [&](const auto& d) { return d.id == input.reference.command_type && d.revision == input.reference.payload_revision; });
    generated::Payload decoded;
    if (descriptor == descriptor_end || !descriptor->decode_payload(payload, decoded)) return false;
    const auto tag = uint32_t(payload[0]);
    const auto variant = std::find_if(descriptor->variants, descriptor->variants + descriptor->variant_count, [=](const auto& v) { return v.tag == tag; });
    if (variant == descriptor->variants + descriptor->variant_count) return false;
    return variant->operation == generated::Operation::read;
}
Inspection client_command(V2Connection& connection, const ClientRoute& route, const generated::submission& input,
        const generated::Bytes& payload, const sc_diagnostic_request& execution, uint32_t timeout, Inspection out) {
    generated::command_reference reference;
    if (!v2_reference(input, payload, reference)) { out.result = ProbeResult::usage; return out; }
    generated::Bytes body;
    if (route.operation == 2) { generated::encode(input, body); body.insert(body.end(), payload.begin(), payload.end()); }
    else generated::encode(reference, body);
    auto response = connection.exchange(route.operation == 5 ? 3 : route.operation, body, timeout);
    generated::command_reply reply; generated::sc_diagnostic_result facts; generated::Bytes result; v2::CommandError error;
    out.result = client_transport(response); out.win32_error = response.win32_error;
    if (out.result != ProbeResult::ok) return out;
    if (!v2::command_response(response.body, reply, error, facts, result)) { out.result = ProbeResult::invalid_response; return out; }
    if (result.empty() && client_publication(route.type)) { out.result = ProbeResult::capability_unavailable; return out; }
    if (!result.empty() && !client_result(route.type, result, out)) { out.result = ProbeResult::invalid_response; return out; }
    auto native_facts = generated::from_wire(facts); native_facts.request_id = execution.request_id;
    std::copy(std::begin(execution.nonce), std::end(execution.nonce), native_facts.nonce);
    client_execution(route.type, native_facts, out);
    if (route.operation == 5 && error == v2::CommandError::none) {
        generated::encode(generated::ack{reference,reply.dimensions.result_token}, body);
        out.result = connection.exchange(5, body, timeout).transport;
    }
    return out;
}
}
Inspection query_native_client(uint32_t pid, uint32_t timeout, uint16_t selector,
        const sc_diagnostic_request& execution, uint64_t cursor, generated::NativeRequest value,
        const sc_campaign_summary* summary, const sc_campaign_rewards* rewards, uint64_t capability) {
    Inspection out; V2Connection connection;
    const auto end = std::end(client_routes);
    const auto route = std::find_if(std::begin(client_routes), end, [=](const auto& r) { return r.selector == selector; });
    if (route == end || (selector == inspect_operation && (capability & ~inspect_capability))) {
        out.result = ProbeResult::capability_unavailable; return out;
    }
    Handle process;
    if (!identify_process(pid, execution.expected.pid ? &execution : nullptr, process, out)) return out;
    const std::array<uint8_t,16> instance = [&] { std::array<uint8_t,16> a{}; std::copy(std::begin(execution.expected.instance_id), std::end(execution.expected.instance_id), a.begin()); return a; }();
    out.failure_stage = "pipe_open"; out.target_state = "live";
    out.result = connection.connect(pid, timeout, execution.expected.pid ? &instance : nullptr);
    out.win32_error = connection.win32_error();
    if (out.result != ProbeResult::ok) return out;
    const auto& scope = connection.identity();
    out.server_pid = pid; out.snapshot.pid = pid; out.snapshot.process_created = scope.process_created;
    out.snapshot.service = ServiceState::listening;
    std::copy(std::begin(scope.instance_id), std::end(scope.instance_id), out.snapshot.instance.begin());
    generated::submission input; generated::Bytes payload;
    if (!prepare_client(route->type,pid,scope,execution,cursor,std::move(value),summary,rewards,out,input,payload)) {
        out.result = ProbeResult::usage; return out;
    }
    if (route->type != 1) {
        generated::Bytes inspect_payload; generated::encode(generated::inspect_payload{generated::inspect_inspect_payload{}}, inspect_payload);
        if (!read_value(connection, 1, inspect_payload, input, timeout, out)) return out;
    }
    if (!client_binding(connection,input,timeout,out)) return out;
    if (client_is_read(input,payload)) {
        if (!read_value(connection,route->type,payload,input,timeout,out) && out.result == ProbeResult::ok) out.result = ProbeResult::invalid_response;
        return out;
    }
    return client_command(connection,*route,input,payload,execution,timeout,out);
}
}
