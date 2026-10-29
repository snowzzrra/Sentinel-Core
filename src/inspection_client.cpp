#include "sentinel_inspection.h"
#include "pipe_io.h"
#include "protocol_v2.h"
#include "protocol.h"
#include "commands.h"
#include "combat.h"
#include <algorithm>
#include <cstring>
#include <type_traits>

namespace sentinel {
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
    RequestPayload payload{};
    const sc_campaign_summary* summary = nullptr;
    const sc_campaign_rewards* rewards = nullptr;
};
struct ClientCodec {
    uint16_t operation;
    size_t (*encode)(Message&, const ClientRequest&);
    bool (*decode)(const Message&, size_t, WireResult&, uint16_t, Inspection&);
    bool (*identity)(const ClientRequest&, const Inspection&);
    bool envelope;
};
size_t encode_plain(Message& data, const ClientRequest& request) {
    return encode_request(data, request.capability, wire_version, request.operation);
}
size_t encode_native(Message& data, const ClientRequest& request) {
    return encode_native_request(data, request.operation, request.execution, request.cursor);
}
size_t encode_write(Message& data, const ClientRequest& request) {
    return encode_save_write_request(data, request.cursor);
}
size_t encode_campaign(Message& data, const ClientRequest& request) {
    const auto& value = std::get<CampaignRequest>(request.payload);
    return encode_campaign_request(data, request.operation, value.request, request.summary, request.rewards);
}
size_t encode_combat(Message& data, const ClientRequest& request) {
    return encode_combat_request(data, request.execution.expected.pid, std::get<sc_combat_query>(request.payload));
}
template<class T, auto Encode> size_t encode_family(Message& data, const ClientRequest& request) {
    const auto& value = std::get<T>(request.payload);
    if constexpr (std::is_invocable_v<decltype(Encode), Message&, uint16_t, const T&>)
        return Encode(data, request.operation, value);
    else return Encode(data, value);
}
bool decode_inspect(const Message& data, size_t size, WireResult& code, uint16_t, Inspection& result) {
    return decode_response(data, size, code, result.snapshot);
}
bool decode_native(const Message& data, size_t size, WireResult& code, uint16_t operation, Inspection& result) {
    return decode_native_response(data, size, code, operation, result.snapshot, result.native, result.diagnostic, &result.detail);
}
template<auto Decode, auto Field>
bool decode_member(const Message& data, size_t size, WireResult& code, uint16_t operation, Inspection& result) {
    if constexpr (std::is_invocable_v<decltype(Decode), const Message&, size_t, WireResult&, uint16_t,
                                     Snapshot&, decltype(result.*Field)>)
        return Decode(data, size, code, operation, result.snapshot, result.*Field);
    else return Decode(data, size, code, result.snapshot, result.*Field);
}
template<class T> bool correlation(const sc_diagnostic_request& request, const T& result) {
    return result.request_id == request.request_id && !std::memcmp(result.nonce, request.nonce, sizeof(request.nonce));
}
template<class T, class U> bool namespace_scope(const T& request, const U& result) {
    return result.execution.scope.lifecycle_generation == request.execution.expected.lifecycle_generation &&
        !std::memcmp(result.namespace_id, request.namespace_id, sizeof(request.namespace_id));
}
bool identity_none(const ClientRequest&, const Inspection&) { return true; }
bool identity_diagnostic(const ClientRequest& request, const Inspection& result) {
    return correlation(request.execution, result.diagnostic);
}
bool identity_write(const ClientRequest& request, const Inspection& result) {
    return !request.cursor || result.write.operation_id == request.cursor;
}
template<class T, auto Field> bool identity_kind(const ClientRequest& request, const Inspection& result) {
    const auto& value = std::get<T>(request.payload);
    const auto& response = result.*Field;
    return correlation(request.execution, response.execution) && namespace_scope(value, response) && response.kind == value.kind;
}
bool identity_backup(const ClientRequest& request, const Inspection& result) {
    const auto& value = std::get<sc_save_backup_request>(request.payload);
    return correlation(request.execution, result.backup.execution) && namespace_scope(value, result.backup) &&
        result.backup.campaign == value.campaign && result.backup.slot == value.slot;
}
bool identity_points(const ClientRequest& request, const Inspection& result) {
    const auto& value = std::get<sc_weapon_points_request>(request.payload);
    return identity_kind<sc_weapon_points_request, &Inspection::weapon_points>(request, result) &&
        result.weapon_points.amount == value.amount && result.weapon_points.expected_gained == value.expected_gained;
}
bool identity_campaign(const ClientRequest& request, const Inspection& result) {
    const auto& value = std::get<CampaignRequest>(request.payload).request;
    return correlation(request.execution, result.campaign) &&
        result.campaign.scope.lifecycle_generation == value.execution.expected.lifecycle_generation &&
        !std::memcmp(result.campaign.namespace_id, value.namespace_id, sizeof(value.namespace_id));
}
bool identity_presentation(const ClientRequest& request, const Inspection& result) {
    return correlation(request.execution, result.combat_presentation) &&
        result.combat_presentation.scope.lifecycle_generation == request.execution.expected.lifecycle_generation;
}
bool identity_automap(const ClientRequest& request, const Inspection& result) {
    const auto& value = std::get<sc_automap_request>(request.payload);
    const auto& response = result.automap;
    if (!correlation(request.execution, response) || response.scope.lifecycle_generation != value.execution.expected.lifecycle_generation ||
        response.kind != value.kind || std::memcmp(response.namespace_id, value.namespace_id, sizeof(value.namespace_id))) return false;
    if (value.kind == SC_AUTOMAP_PUBLISH && response.outcome == SC_AUTOMAP_ACCEPTED)
        return response.revision == value.revision && response.known == value.known;
    return true;
}
const ClientCodec codecs[] = {
    {inspect_operation, encode_plain,
        decode_inspect, identity_none, false},
    {engine_operation, encode_plain,
        decode_member<decode_engine_response, &Inspection::engine>, identity_none, false},
    {context_operation, encode_plain,
        decode_member<decode_context_response, &Inspection::context>, identity_none, false},
    {native_operation, encode_native,
        decode_native, identity_none, false},
    {diagnostic_submit_operation, encode_native,
        decode_native, identity_diagnostic, true},
    {diagnostic_result_operation, encode_native,
        decode_native, identity_diagnostic, true},
    {diagnostic_cancel_operation, encode_native,
        decode_native, identity_diagnostic, true},
    {diagnostic_detail_submit_operation, encode_native,
        decode_native, identity_diagnostic, true},
    {diagnostic_detail_result_operation, encode_native,
        decode_native, identity_diagnostic, true},
    {diagnostic_detail_cancel_operation, encode_native,
        decode_native, identity_diagnostic, true},
    {save_operation, encode_plain,
        decode_member<decode_save_response, &Inspection::save>, identity_none, false},
    {save_admission_operation, encode_plain,
        decode_member<decode_save_admission_response, &Inspection::admission>, identity_none, false},
    {save_write_operation, encode_write,
        decode_member<decode_save_write_response, &Inspection::write>, identity_write, false},
    {save_backup_submit_operation, encode_family<sc_save_backup_request, encode_backup_request>,
        decode_member<decode_backup_response, &Inspection::backup>, identity_backup, true},
    {save_backup_result_operation, encode_family<sc_save_backup_request, encode_backup_request>,
        decode_member<decode_backup_response, &Inspection::backup>, identity_backup, true},
    {save_backup_cancel_operation, encode_family<sc_save_backup_request, encode_backup_request>,
        decode_member<decode_backup_response, &Inspection::backup>, identity_backup, true},
    {save_installation_operation, encode_plain,
        decode_member<decode_installation_response, &Inspection::installation>, identity_none, false},
    {weapon_points_submit_operation, encode_family<sc_weapon_points_request, encode_weapon_points_request>,
        decode_member<decode_weapon_points_response, &Inspection::weapon_points>, identity_points, true},
    {weapon_points_result_operation, encode_family<sc_weapon_points_request, encode_weapon_points_request>,
        decode_member<decode_weapon_points_response, &Inspection::weapon_points>, identity_points, true},
    {weapon_points_cancel_operation, encode_family<sc_weapon_points_request, encode_weapon_points_request>,
        decode_member<decode_weapon_points_response, &Inspection::weapon_points>, identity_points, true},
    {weapon_points_release_operation, encode_family<sc_weapon_points_request, encode_weapon_points_request>,
        decode_member<decode_weapon_points_response, &Inspection::weapon_points>, identity_points, true},
    {campaign_row_operation, encode_campaign,
        decode_member<decode_campaign_response, &Inspection::campaign>, identity_campaign, true},
    {campaign_commit_operation, encode_campaign,
        decode_member<decode_campaign_response, &Inspection::campaign>, identity_campaign, true},
    {campaign_inspect_operation, encode_campaign,
        decode_member<decode_campaign_response, &Inspection::campaign>, identity_campaign, true},
    {inventory_submit_operation, encode_family<sc_inventory_request, encode_inventory_request>,
        decode_member<decode_inventory_response, &Inspection::inventory>, identity_kind<sc_inventory_request, &Inspection::inventory>, true},
    {inventory_result_operation, encode_family<sc_inventory_request, encode_inventory_request>,
        decode_member<decode_inventory_response, &Inspection::inventory>, identity_kind<sc_inventory_request, &Inspection::inventory>, true},
    {inventory_cancel_operation, encode_family<sc_inventory_request, encode_inventory_request>,
        decode_member<decode_inventory_response, &Inspection::inventory>, identity_kind<sc_inventory_request, &Inspection::inventory>, true},
    {inventory_release_operation, encode_family<sc_inventory_request, encode_inventory_request>,
        decode_member<decode_inventory_response, &Inspection::inventory>, identity_kind<sc_inventory_request, &Inspection::inventory>, true},
    {arsenal_submit_operation, encode_family<sc_arsenal_request, encode_arsenal_request>,
        decode_member<decode_arsenal_response, &Inspection::arsenal>, identity_kind<sc_arsenal_request, &Inspection::arsenal>, true},
    {arsenal_result_operation, encode_family<sc_arsenal_request, encode_arsenal_request>,
        decode_member<decode_arsenal_response, &Inspection::arsenal>, identity_kind<sc_arsenal_request, &Inspection::arsenal>, true},
    {arsenal_cancel_operation, encode_family<sc_arsenal_request, encode_arsenal_request>,
        decode_member<decode_arsenal_response, &Inspection::arsenal>, identity_kind<sc_arsenal_request, &Inspection::arsenal>, true},
    {arsenal_release_operation, encode_family<sc_arsenal_request, encode_arsenal_request>,
        decode_member<decode_arsenal_response, &Inspection::arsenal>, identity_kind<sc_arsenal_request, &Inspection::arsenal>, true},
    {runes_submit_operation, encode_family<sc_runes_request, encode_runes_request>,
        decode_member<decode_runes_response, &Inspection::runes>, identity_kind<sc_runes_request, &Inspection::runes>, true},
    {runes_result_operation, encode_family<sc_runes_request, encode_runes_request>,
        decode_member<decode_runes_response, &Inspection::runes>, identity_kind<sc_runes_request, &Inspection::runes>, true},
    {runes_cancel_operation, encode_family<sc_runes_request, encode_runes_request>,
        decode_member<decode_runes_response, &Inspection::runes>, identity_kind<sc_runes_request, &Inspection::runes>, true},
    {runes_release_operation, encode_family<sc_runes_request, encode_runes_request>,
        decode_member<decode_runes_response, &Inspection::runes>, identity_kind<sc_runes_request, &Inspection::runes>, true},
    {special_submit_operation, encode_family<sc_special_request, encode_special_request>,
        decode_member<decode_special_response, &Inspection::special>, identity_kind<sc_special_request, &Inspection::special>, true},
    {special_result_operation, encode_family<sc_special_request, encode_special_request>,
        decode_member<decode_special_response, &Inspection::special>, identity_kind<sc_special_request, &Inspection::special>, true},
    {special_cancel_operation, encode_family<sc_special_request, encode_special_request>,
        decode_member<decode_special_response, &Inspection::special>, identity_kind<sc_special_request, &Inspection::special>, true},
    {special_release_operation, encode_family<sc_special_request, encode_special_request>,
        decode_member<decode_special_response, &Inspection::special>, identity_kind<sc_special_request, &Inspection::special>, true},
    {deathlink_submit_operation, encode_family<sc_deathlink_request, encode_deathlink_request>,
        decode_member<decode_deathlink_response, &Inspection::deathlink>, identity_kind<sc_deathlink_request, &Inspection::deathlink>, true},
    {deathlink_result_operation, encode_family<sc_deathlink_request, encode_deathlink_request>,
        decode_member<decode_deathlink_response, &Inspection::deathlink>, identity_kind<sc_deathlink_request, &Inspection::deathlink>, true},
    {deathlink_cancel_operation, encode_family<sc_deathlink_request, encode_deathlink_request>,
        decode_member<decode_deathlink_response, &Inspection::deathlink>, identity_kind<sc_deathlink_request, &Inspection::deathlink>, true},
    {deathlink_release_operation, encode_family<sc_deathlink_request, encode_deathlink_request>,
        decode_member<decode_deathlink_response, &Inspection::deathlink>, identity_kind<sc_deathlink_request, &Inspection::deathlink>, true},
    {automap_operation, encode_family<sc_automap_request, encode_automap_request>,
        decode_member<decode_automap_response, &Inspection::automap>, identity_automap, true},
    {command_submit_operation, encode_family<sc_command_request, encode_command_request>,
        decode_member<decode_command_response, &Inspection::command>, identity_kind<sc_command_request, &Inspection::command>, true},
    {command_result_operation, encode_family<sc_command_request, encode_command_request>,
        decode_member<decode_command_response, &Inspection::command>, identity_kind<sc_command_request, &Inspection::command>, true},
    {command_cancel_operation, encode_family<sc_command_request, encode_command_request>,
        decode_member<decode_command_response, &Inspection::command>, identity_kind<sc_command_request, &Inspection::command>, true},
    {command_release_operation, encode_family<sc_command_request, encode_command_request>,
        decode_member<decode_command_response, &Inspection::command>, identity_kind<sc_command_request, &Inspection::command>, true},
    {combat_operation, encode_combat,
        decode_member<decode_combat_response, &Inspection::combat>, identity_none, false},
    {combat_presentation_operation, encode_family<sc_combat_presentation_request, encode_combat_presentation_request>,
        decode_member<decode_combat_presentation_response, &Inspection::combat_presentation>, identity_presentation, true},
};
const ClientCodec* find_codec(uint16_t operation) {
    const auto end = std::end(codecs);
    const auto it = std::find_if(std::begin(codecs), end, [=](const auto& codec) { return codec.operation == operation; });
    return it == end ? nullptr : it;
}
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
DWORD open_endpoint(uint32_t pid, uint64_t deadline, Handle& pipe, Inspection& result, bool version2 = false) {
    const auto name = version2 ? pipe_name_v2(pid) : pipe_name(pid);
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
DWORD exchange_bytes(HANDLE pipe, uint64_t deadline, Message& data, DWORD size, DWORD& count, Inspection& result) {
    result.failure_stage = "pipe_write";
    const auto error = transfer(pipe, true, data.data(), size, count, nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return error;
    result.failure_stage = "pipe_read";
    return transfer(pipe, false, data.data(), static_cast<DWORD>(data.size()), count, nullptr, remaining(deadline));
}
ProbeResult wire_failure(WireResult code) {
    switch (code) {
    case WireResult::capability_unavailable: case WireResult::unsupported_operation: return ProbeResult::capability_unavailable;
    case WireResult::incompatible_protocol: return ProbeResult::incompatible_protocol;
    default: return ProbeResult::invalid_response;
    }
}
bool decode_expected(const ClientCodec& codec, const Message& data, DWORD size, Inspection& result) {
    result.failure_stage = "decode_response";
    WireResult code{};
    bool decoded = codec.decode(data, size, code, codec.operation, result);
    if (!decoded && codec.operation != inspect_operation && size == header_size)
        decoded = decode_response(data, size, code, result.snapshot) && code != WireResult::ok;
    if (!decoded) { result.result = ProbeResult::invalid_response; return false; }
    if (code != WireResult::ok) { result.result = wire_failure(code); return false; }
    return true;
}
bool verify_response(uint32_t pid, HANDLE process, const ClientRequest& request, const ClientCodec& codec, Inspection& result) {
    result.failure_stage = "response_identity";
    if (result.snapshot.pid != pid || result.snapshot.process_created != result.verified_process_created ||
        (codec.envelope && std::memcmp(request.execution.expected.instance_id, result.snapshot.instance.data(), result.snapshot.instance.size())) ||
        !live_process(process, result)) {
        result.result = ProbeResult::process_mismatch; return false;
    }
    if (!codec.identity(request, result)) { result.result = ProbeResult::invalid_response; return false; }
    result.result = ProbeResult::ok;
    result.failure_stage = "none"; result.target_state = "live";
    return true;
}
Inspection query_operation(uint32_t pid, uint32_t timeout_ms, const ClientRequest& request) {
    Inspection result;
    const auto* codec = find_codec(request.operation);
    if (!pid || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms || !codec) {
        result.result = ProbeResult::usage; return result;
    }
    const auto deadline = GetTickCount64() + timeout_ms;
    Handle process, pipe;
    if (!identify_process(pid, codec->envelope ? &request.execution : nullptr, process, result)) return result;
    auto error = open_endpoint(pid, deadline, pipe, result);
    if (error != ERROR_SUCCESS) { io_failure(result, process.value, error); return result; }
    if (result.server_pid != pid || !live_process(process.value, result)) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    Message data{};
    const auto size = static_cast<DWORD>(codec->encode(data, request));
    if (!size) { result.result = ProbeResult::usage; return result; }
    DWORD count = 0;
    error = exchange_bytes(pipe.value, deadline, data, size, count, result);
    if (error != ERROR_SUCCESS) { io_failure(result, process.value, error); return result; }
    if (decode_expected(*codec, data, count, result)) verify_response(pid, process.value, request, *codec, result);
    return result;
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
    ClientRequest value{combat_operation, combat_capability}; value.execution.expected.pid = pid; value.payload = request;
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
    if (!pid || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms) return ProbeResult::usage;
    const auto deadline = GetTickCount64() + timeout_ms;
    Handle process, pipe; Inspection result;
    if (!identify_process(pid, nullptr, process, result)) return result.result;
    const auto error = open_endpoint(pid, deadline, pipe, result, true);
    if (error != ERROR_SUCCESS) return classify(error);
    if (result.server_pid != pid || !live_process(process.value, result)) return ProbeResult::process_mismatch;
    pipe_ = pipe.value; pipe.value = nullptr; process_ = process.value; process.value = nullptr;
    identity_ = {}; identity_.pid = pid; identity_.process_created = result.verified_process_created;
    generated::Bytes bytes; generated::wire_header_2 header{generated::wire_magic, 2, 6, 0, 0, ++correlation_, 0, 0};
    if (!v2::frame(header, {}, bytes)) { close(); return ProbeResult::invalid_response; }
    generated::wire_header_2 response{};
    const auto io = v2_exchange_bytes(pipe_, bytes, response, deadline);
    if (io != ERROR_SUCCESS) { close(); return classify(io); }
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
