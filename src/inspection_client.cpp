#include "sentinel_inspection.h"
#include "pipe_io.h"
#include "protocol.h"
#include <cstring>

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
}
static Inspection query_operation(uint32_t pid, uint32_t timeout_ms, uint64_t required, uint16_t operation,
                                   const sc_diagnostic_request* request = nullptr, uint64_t after_event = 0,
                                   uint64_t write_id = 0, const sc_save_backup_request* backup = nullptr,
                                   const sc_weapon_points_request* points = nullptr, const sc_campaign_request* campaign=nullptr,
                                   const sc_inventory_request* inventory = nullptr,
                                   const sc_arsenal_request* arsenal = nullptr,
                                   const sc_runes_request* runes = nullptr,
                                   const sc_special_request* special = nullptr) {
    Inspection result;
    Handle process;
    auto fail = [&](DWORD error) {
        result.win32_error = error; result.result = classify(error);
        if (process) {
            const auto state = WaitForSingleObject(process.value, 0);
            result.target_state = state == WAIT_OBJECT_0 ? "exited" : (state == WAIT_TIMEOUT ? "live" : "unknown");
            if (state == WAIT_FAILED) result.target_wait_error = GetLastError();
        }
        // Keep the original API error/classification, even if exit is confirmed.
        // An OpenProcess denial supplies no handle and cannot prove process exit.
        return result;
    };
    if (!pid || timeout_ms < min_timeout_ms || timeout_ms > max_timeout_ms) {
        result.result = ProbeResult::usage; return result;
    }
    const auto deadline = GetTickCount64() + timeout_ms;
    result.failure_stage = "open_process";
    process.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return fail(GetLastError());
    result.failure_stage = "process_alive";
    if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) return fail(ERROR_FILE_NOT_FOUND);
    uint64_t created = 0;
    result.failure_stage = "process_creation";
    if (!process_time(process.value, created)) return fail(GetLastError());
    result.verified_process_created = created;
    if (request && (request->expected.pid != pid || request->expected.process_created != created)) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    wchar_t path[32768]{}; DWORD path_size = static_cast<DWORD>(std::size(path));
    result.failure_stage = "process_image";
    if (!QueryFullProcessImageNameW(process.value, 0, path, &path_size)) return fail(GetLastError());
    result.host_path.assign(path, path_size);
    Handle pipe;
    const auto name = pipe_name(pid);
    for (;;) {
        result.failure_stage = "pipe_open";
        pipe.value = CreateFileW(name.c_str(), client_access, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe) break;
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY) return fail(error);
        const DWORD wait = remaining(deadline);
        if (!wait) return fail(ERROR_TIMEOUT);
        result.failure_stage = "pipe_wait";
        if (!WaitNamedPipeW(name.c_str(), wait)) return fail(GetLastError());
    }
    ULONG server_pid = 0;
    result.failure_stage = "pipe_server_identity";
    if (!GetNamedPipeServerProcessId(pipe.value, &server_pid)) return fail(GetLastError());
    result.server_pid = server_pid;
    if (server_pid != pid || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    Message data{};
    const DWORD size = static_cast<DWORD>(special ? encode_special_request(data, operation, *special) :
        runes ? encode_runes_request(data, operation, *runes) :
        arsenal ? encode_arsenal_request(data, operation, *arsenal) :
        inventory ? encode_inventory_request(data, operation, *inventory) :
        campaign ? encode_campaign_request(data,operation,*campaign) :
        points ? encode_weapon_points_request(data, operation, *points) :
        backup ? encode_backup_request(data, operation, *backup) :
        operation == save_write_operation ? encode_save_write_request(data, write_id) :
        operation >= native_operation && operation <= diagnostic_detail_cancel_operation ?
        encode_native_request(data, operation, request ? *request : sc_diagnostic_request{}, after_event) :
        encode_request(data, required, wire_version, operation));
    DWORD count = 0;
    result.failure_stage = "pipe_write";
    DWORD error = transfer(pipe.value, true, data.data(), size, count, nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return fail(error);
    result.failure_stage = "pipe_read";
    error = transfer(pipe.value, false, data.data(), static_cast<DWORD>(data.size()), count,
                     nullptr, remaining(deadline));
    if (error != ERROR_SUCCESS) return fail(error);
    WireResult code{};
    result.failure_stage = "decode_response";
    // Old wire-v1 servers reject op 2 with their unchanged op-1 error envelope.
    bool decoded = special ? decode_special_response(data, count, code, operation, result.snapshot, result.special) :
        runes ? decode_runes_response(data, count, code, operation, result.snapshot, result.runes) :
        arsenal ? decode_arsenal_response(data, count, code, operation, result.snapshot, result.arsenal) :
        inventory ? decode_inventory_response(data, count, code, operation, result.snapshot, result.inventory) :
        campaign ? decode_campaign_response(data,count,code,operation,result.snapshot,result.campaign) :
        points ? decode_weapon_points_response(data, count, code, operation, result.snapshot, result.weapon_points) :
        backup ? decode_backup_response(data, count, code, operation, result.snapshot, result.backup) :
        operation == save_installation_operation ? decode_installation_response(data, count, code, result.snapshot, result.installation) :
        operation == save_write_operation ? decode_save_write_response(data, count, code, result.snapshot, result.write) :
        operation == save_admission_operation ? decode_save_admission_response(data, count, code, result.snapshot, result.admission) :
        (operation == save_operation ? decode_save_response(data, count, code, result.snapshot, result.save) :
        (operation >= native_operation ? decode_native_response(data, count, code, operation, result.snapshot, result.native, result.diagnostic, &result.detail) :
        (operation == context_operation ? decode_context_response(data, count, code, result.snapshot, result.context) :
        (operation == engine_operation ? decode_engine_response(data, count, code, result.snapshot, result.engine) :
        decode_response(data, count, code, result.snapshot)))));
    if (!decoded && operation != inspect_operation && count == header_size)
        decoded = decode_response(data, count, code, result.snapshot) && code != WireResult::ok;
    if (!decoded) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (code != WireResult::ok) {
        result.result = code == WireResult::capability_unavailable || code == WireResult::unsupported_operation ? ProbeResult::capability_unavailable :
            (code == WireResult::incompatible_protocol ? ProbeResult::incompatible_protocol : ProbeResult::invalid_response);
        return result;
    }
    result.failure_stage = "response_identity";
    if (result.snapshot.pid != pid || result.snapshot.process_created != created ||
        (request && std::memcmp(request->expected.instance_id, result.snapshot.instance.data(), result.snapshot.instance.size())) ||
        WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) {
        result.result = ProbeResult::process_mismatch; return result;
    }
    const auto& execution = special ? result.special.execution : (runes ? result.runes.execution : (arsenal ? result.arsenal.execution : (inventory ? result.inventory.execution : (points ? result.weapon_points.execution : (backup ? result.backup.execution : result.diagnostic)))));
    if (request && !campaign && (execution.request_id != request->request_id ||
        std::memcmp(execution.nonce, request->nonce, sizeof(request->nonce)))) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (backup && (execution.scope.lifecycle_generation != backup->execution.expected.lifecycle_generation ||
        result.backup.campaign != backup->campaign || result.backup.slot != backup->slot ||
        std::memcmp(result.backup.namespace_id, backup->namespace_id, sizeof(backup->namespace_id)))) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (operation == save_write_operation && write_id && result.write.operation_id != write_id) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (points && (execution.scope.lifecycle_generation != points->execution.expected.lifecycle_generation ||
        result.weapon_points.kind != points->kind || result.weapon_points.amount != points->amount ||
        result.weapon_points.expected_gained != points->expected_gained ||
        std::memcmp(result.weapon_points.namespace_id, points->namespace_id, sizeof(points->namespace_id)))) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (campaign && (result.campaign.scope.lifecycle_generation!=campaign->execution.expected.lifecycle_generation ||
        result.campaign.request_id!=campaign->execution.request_id ||
        std::memcmp(result.campaign.nonce,campaign->execution.nonce,16) ||
        std::memcmp(result.campaign.namespace_id,campaign->namespace_id,65))) {
        result.result=ProbeResult::invalid_response; return result;
    }
    if (inventory && (execution.scope.lifecycle_generation != inventory->execution.expected.lifecycle_generation ||
        result.inventory.kind != inventory->kind ||
        std::memcmp(result.inventory.namespace_id, inventory->namespace_id, sizeof(inventory->namespace_id)))) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (arsenal && (execution.scope.lifecycle_generation != arsenal->execution.expected.lifecycle_generation ||
        result.arsenal.kind != arsenal->kind ||
        std::memcmp(result.arsenal.namespace_id, arsenal->namespace_id, sizeof(arsenal->namespace_id)))) {
        result.result = ProbeResult::invalid_response; return result;
    }
    if (special && (execution.scope.lifecycle_generation != special->execution.expected.lifecycle_generation ||
        result.special.kind != special->kind ||
        std::memcmp(result.special.namespace_id, special->namespace_id, sizeof(special->namespace_id)))) {
        result.result = ProbeResult::invalid_response; return result;
    }
    result.result = ProbeResult::ok;
    result.failure_stage = "none"; result.target_state = "live";
    return result;
}
Inspection query(uint32_t pid, uint32_t timeout_ms, uint64_t required) {
    return query_operation(pid, timeout_ms, required, inspect_operation);
}
Inspection query_campaign(uint32_t pid,uint32_t timeout_ms,uint16_t operation,const sc_campaign_request& r) {
    if (operation<campaign_row_operation || operation>campaign_inspect_operation) {
        Inspection out; out.result=ProbeResult::usage; return out;
    }
    return query_operation(pid,timeout_ms,campaign_menu_capability,operation,&r.execution,0,0,nullptr,nullptr,&r);
}
Inspection query_engine(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, engine_capability, engine_operation);
}
Inspection query_context(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, context_capability, context_operation);
}
Inspection query_save(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, save_capability, save_operation);
}
Inspection query_save_admission(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, save_admission_capability, save_admission_operation);
}
Inspection query_save_installation(uint32_t pid, uint32_t timeout_ms) {
    return query_operation(pid, timeout_ms, save_installation_capability, save_installation_operation);
}
Inspection query_save_write(uint32_t pid, uint32_t timeout_ms, uint64_t operation_id) {
    return query_operation(pid, timeout_ms, save_write_capability, save_write_operation, nullptr, 0, operation_id);
}
Inspection query_save_backup(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_save_backup_request& request) {
    if (operation < save_backup_submit_operation || operation > save_backup_cancel_operation) {
        Inspection out; out.result = ProbeResult::usage; return out;
    }
    return query_operation(pid, timeout_ms, save_backup_capability, operation, &request.execution, 0, 0, &request);
}
Inspection query_native(uint32_t pid, uint32_t timeout_ms, uint64_t after_event) {
    return query_operation(pid, timeout_ms, native_capability, native_operation, nullptr, after_event);
}
Inspection query_weapon_points(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_weapon_points_request& r) {
    if (operation < weapon_points_submit_operation || operation > weapon_points_release_operation) {
        Inspection out; out.result = ProbeResult::usage; return out;
    }
    return query_operation(pid,timeout_ms,weapon_points_capability,operation,&r.execution,0,0,nullptr,&r);
}
Inspection query_arsenal(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_arsenal_request& r) {
    if (operation < arsenal_submit_operation || operation > arsenal_release_operation) {
        Inspection out; out.result = ProbeResult::usage; return out;
    }
    return query_operation(pid, timeout_ms, arsenal_capability, operation, &r.execution, 0, 0, nullptr, nullptr, nullptr, nullptr, &r);
}
Inspection query_runes(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_runes_request& r) {
    if (operation < runes_submit_operation || operation > runes_release_operation) {
        Inspection out; out.result = ProbeResult::usage; return out;
    }
    return query_operation(pid, timeout_ms, runes_capability, operation, &r.execution, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, &r);
}
Inspection query_special(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_special_request& r) {
    if (operation < special_submit_operation || operation > special_release_operation) {
        Inspection out; out.result = ProbeResult::usage; return out;
    }
    return query_operation(pid, timeout_ms, special_capability, operation, &r.execution, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &r);
}
Inspection query_inventory(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_inventory_request& r) {
    if (operation < inventory_submit_operation || operation > inventory_release_operation) {
        Inspection out; out.result = ProbeResult::usage; return out;
    }
    return query_operation(pid, timeout_ms, inventory_capability, operation, &r.execution, 0, 0, nullptr, nullptr, nullptr, &r);
}
Inspection query_diagnostic(uint32_t pid, uint32_t timeout_ms, uint16_t operation, const sc_diagnostic_request& request) {
    if (operation < diagnostic_submit_operation || operation > diagnostic_detail_cancel_operation) {
        Inspection result; result.result = ProbeResult::usage; return result;
    }
    return query_operation(pid, timeout_ms, diagnostic_capability, operation, &request);
}
}
