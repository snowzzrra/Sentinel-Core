#include "sentinel_inspection.h"
#include <cstdio>

namespace sentinel {
const char* field_name(size_t field) {
    constexpr const char* names[] = {"root_available", "loading", "in_game", "map_present", "player_present", "cutscene_id"};
    return field < SC_ENGINE_FIELD_COUNT ? names[field] : "invalid_field";
}
const char* save_write_state_name(uint32_t state) {
    constexpr const char* names[] = {"none", "pending", "native_failed", "native_succeeded",
        "sdk_confirmed", "indeterminate", "not_retained", "readback_pending", "readback_confirmed", "readback_failed"};
    return state < std::size(names) ? names[state] : "invalid";
}
const char* save_session_state_name(uint32_t state) {
    constexpr const char* names[] = {"disabled", "prepared", "starting", "admitted", "rejected", "faulted", "binding"};
    return state < std::size(names) ? names[state] : "invalid";
}
const char* save_session_fault_name(uint32_t fault) {
    constexpr const char* names[] = {"none", "descriptor", "installation", "incomplete_routes", "startup_context",
        "repeated_startup", "missed_startup", "provider_identity", "foreign_collector", "malformed_entry",
        "native_collection", "native_copy", "unscoped_delete", "delete_indeterminate", "native_write", "native_profile", "native_read", "native_campaign"};
    return fault < std::size(names) ? names[fault] : "invalid";
}
const char* save_field_name(size_t field) {
    constexpr const char* names[] = {"root_present", "profile_manager_present", "provider_kind", "queued_requests",
        "pending_map_load", "save_job_witness", "request58_witness", "request50_witness", "selected_slot",
        "namespace_route", "native_operation_id", "native_completion"};
    return field < SC_SAVE_FIELD_COUNT ? names[field] : "invalid_field";
}
const char* save_reason_name(uint32_t reason) {
    switch (reason) {
    case SC_SAVE_UNSUPPORTED: return "unsupported";
    case SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN: return "native_namespace_route_unproven";
    case SC_SAVE_SELECTED_SLOT_UNPROVEN: return "selected_slot_unproven";
    case SC_SAVE_COMPLETION_UNPROVEN: return "native_completion_unproven";
    default: return reason_name(reason);
    }
}
const char* save_provider_name(uint64_t provider) {
    constexpr const char* names[] = {"unknown", "steam", "local_encrypted", "local", "foreign"};
    return provider <= SC_SAVE_PROVIDER_FOREIGN ? names[provider] : "invalid_provider";
}
const char* backup_state_name(uint32_t state) {
    constexpr const char* names[] = {"unknown", "queued", "claimed", "waiting_native", "copying", "copied",
        "complete", "failed", "rejected", "cancelled", "expired"};
    return state <= SC_BACKUP_EXPIRED ? names[state] : "invalid";
}
const char* native_reason_name(uint32_t reason) {
    constexpr const char* names[] = {"none", "not_started", "unknown_build", "target_bytes", "target_not_unique",
        "target_boundary", "binding_failed", "hook_failed", "pin_failed", "wrong_thread", "wrong_caller", "reentrant",
        "event_gap", "unobserved", "transition", "context_unavailable", "stale", "scope_mismatch", "queue_full",
        "duplicate_mismatch", "deadline", "cancelled", "stopped", "not_found", "read_failed", "budget", "exception"};
    return reason <= SC_NATIVE_EXCEPTION ? names[reason] : "invalid_reason";
}
const char* diagnostic_state_name(uint32_t state) {
    constexpr const char* names[] = {"unknown", "queued", "claimed", "callback_executed", "rejected", "expired", "cancelled"};
    return state <= SC_DIAGNOSTIC_CANCELLED ? names[state] : "invalid_state";
}
const char* diagnostic_stage_name(uint32_t stage) {
    constexpr const char* names[] = {"none", "admission", "queue", "claim_context", "scope", "pending_before",
        "fresh_context", "observation_budget", "pending_after", "event_stamp", "map_binding", "native_fault",
        "cancellation", "deadline", "executed"};
    return stage <= SC_STAGE_EXECUTED ? names[stage] : "invalid_stage";
}
const char* context_field_name(size_t field) {
    constexpr const char* names[] = {"game_state", "state_changed_ms", "native_load_serial", "cutscene_active", "native_entry_mode"};
    return field < SC_CONTEXT_FIELD_COUNT ? names[field] : "invalid_field";
}
const char* context_reason_name(uint32_t reason) {
    switch (reason) {
    case SC_CONTEXT_UNSUPPORTED: return "unsupported";
    case SC_CONTEXT_MENU_WORLD: return "menu_world";
    case SC_CONTEXT_NAME_TOO_LONG: return "name_too_long";
    case SC_CONTEXT_EMPTY_NAME: return "empty_name";
    default: return reason_name(reason);
    }
}
const char* validity_name(uint32_t validity) {
    return validity == SC_OBSERVATION_OBSERVED ? "observed" :
        (validity == SC_OBSERVATION_PROVISIONAL ? "provisional" : "unknown");
}
const char* reason_name(uint32_t reason) {
    constexpr const char* names[] = {"none", "not_sampled", "stopped", "invalid_pe", "read_failed",
        "partial_read", "out_of_range", "signature_missing", "signature_ambiguous", "profile_unrecognized",
        "parent_unavailable", "parent_null", "invalid_value", "transition", "cancelled", "budget", "internal_error", "stale"};
    return reason <= SC_REASON_STALE ? names[reason] : "invalid_reason";
}
const char* service_name(ServiceState state) {
    switch (state) {
    case ServiceState::stopped: return "stopped";
    case ServiceState::listening: return "listening";
    case ServiceState::stopping: return "stopping";
    default: return "failed";
    }
}
const char* result_name(ProbeResult result) {
    switch (result) {
    case ProbeResult::ok: return "ok";
    case ProbeResult::usage: return "usage";
    case ProbeResult::endpoint_absent: return "endpoint_absent";
    case ProbeResult::access_denied: return "access_denied";
    case ProbeResult::timeout: return "timeout";
    case ProbeResult::process_mismatch: return "process_mismatch";
    case ProbeResult::incompatible_protocol: return "incompatible_protocol";
    case ProbeResult::capability_unavailable: return "capability_unavailable";
    case ProbeResult::invalid_response: return "invalid_response";
    default: return "io_error";
    }
}
std::string instance_text(const std::array<uint8_t, 16>& id) {
    std::string value;
    for (auto b : id) { value += "0123456789abcdef"[b >> 4]; value += "0123456789abcdef"[b & 15]; }
    return value;
}
}
