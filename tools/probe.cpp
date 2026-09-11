#include "sentinel_inspection.h"
#include "installation_probe.h"
#include "save_probe.h"
#include "pipe_io.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cerrno>
#include <algorithm>
#include <bcrypt.h>
#include <cstring>

namespace {
HANDLE interrupt_event = nullptr;
BOOL WINAPI interrupt(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    SetEvent(interrupt_event); return TRUE;
}
bool number(const wchar_t* value, uint32_t& result) {
    if (!*value) return false;
    for (const auto* p = value; *p; ++p) if (*p < L'0' || *p > L'9') return false;
    wchar_t* end = nullptr; errno = 0;
    const auto parsed = std::wcstoull(value, &end, 10);
    if (errno || *end || parsed > UINT32_MAX) return false;
    result = static_cast<uint32_t>(parsed); return true;
}
bool number64(const wchar_t* value, uint64_t& result) {
    if (!*value) return false;
    for (auto p = value; *p; ++p) if (*p < L'0' || *p > L'9') return false;
    wchar_t* end = nullptr; errno = 0; result = std::wcstoull(value, &end, 10);
    return !errno && !*end;
}
bool hex16(const wchar_t* value, uint8_t (&out)[16]) {
    if (std::wcslen(value) != 32) return false;
    for (unsigned i = 0; i < 32; ++i) {
        const auto c = value[i]; const int n = c >= L'0' && c <= L'9' ? c - L'0' :
            (c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1);
        if (n < 0) return false;
        if (i % 2) out[i / 2] |= static_cast<uint8_t>(n);
        else out[i / 2] = static_cast<uint8_t>(n << 4);
    }
    return true;
}
std::string hex_text(const uint8_t (&bytes)[16]) {
    std::array<uint8_t, 16> a{}; std::copy(std::begin(bytes), std::end(bytes), a.begin());
    return sentinel::instance_text(a);
}
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { char escaped[7]{}; sprintf_s(escaped, "\\u%04x", c); out += escaped; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
}
void print_native(const sentinel::Inspection& r, bool json) {
    const auto& n = r.native; const auto& s = r.snapshot;
    constexpr const char* availability[] = {"pending", "enabled", "disabled", "retained"};
    constexpr const char* lifecycle[] = {"unobserved", "transition", "active", "menu", "failed", "invalid"};
    const auto name = n.current_map.length ? quoted(std::string(n.current_map.bytes, n.current_map.length)) : "null";
    if (json) {
        std::printf("{\"result\":\"ok\",\"operation\":\"native\",\"native_abi\":1,\"core_version\":%s,\"build_id\":%s,"
            "\"target_pid\":%u,\"server_pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"%s\",\"host_path\":%s,"
            "\"availability\":\"%s\",\"reason\":\"%s\",\"site_revision\":%u,\"site_rva\":%u,\"phase\":%u,"
            "\"installed_hooks\":%u,\"validator_reasons\":[\"%s\",\"%s\",\"%s\"],\"retained_module\":%s,"
            "\"coverage_bits\":%u,\"coverage_complete\":false,\"native_load_serial\":null,\"gameplay_authorization\":\"unvalidated\","
            "\"lifecycle_generation\":\"%llu\",\"lifecycle\":\"%s\",\"depth\":%u,\"checkpoint_flag_known\":%s,\"checkpoint_flag\":%s,"
            "\"event_sequence\":\"%llu\",\"event_gap_count\":\"%llu\",\"history_oldest\":\"%llu\",\"history_overwritten\":\"%llu\","
            "\"callback_sequence\":\"%llu\",\"callback_at_ms\":\"%llu\",\"callback_thread_id\":%u,\"native_owner_thread_id\":%u,"
            "\"context_generation\":\"%llu\",\"context_sampled_at_ms\":\"%llu\",\"context_reason\":\"%s\",\"game_state\":%u,"
            "\"current_map\":%s,\"queued\":%u,\"claimed\":%u,\"retained_results\":%u,\"history_gap\":%s,\"events\":[",
            quoted(s.core.version).c_str(), quoted(s.core.build_id).c_str(), s.pid, r.server_pid, s.process_created,
            sentinel::instance_text(s.instance).c_str(), quoted(utf8(r.host_path)).c_str(), availability[n.availability],
            sentinel::native_reason_name(n.reason), n.site_revision, n.site_rva, n.phase, n.installed_hooks,
            sentinel::native_reason_name(n.validator_reasons[0]), sentinel::native_reason_name(n.validator_reasons[1]),
            sentinel::native_reason_name(n.validator_reasons[2]), n.retained_module ? "true" : "false", n.coverage,
            n.scope.lifecycle_generation, lifecycle[n.lifecycle], n.depth, n.checkpoint_flag_known ? "true" : "false",
            n.checkpoint_flag ? "true" : "false", n.event_sequence, n.event_gap_count, n.history_oldest, n.history_overwritten,
            n.callback_sequence, n.callback_at_ms, n.callback_thread_id, n.native_owner_thread_id,
            n.context_generation, n.context_sampled_at_ms, sentinel::native_reason_name(n.context_reason), n.game_state,
            name.c_str(), n.queued, n.claimed, n.retained_results, n.history_gap ? "true" : "false");
        for (uint32_t i = 0; i < n.event_count; ++i) {
            const auto& e = n.events[i];
            std::printf("%s{\"sequence\":\"%llu\",\"generation\":\"%llu\",\"at_ms\":\"%llu\",\"kind\":%u,\"lifecycle\":%u,\"thread_id\":%u,\"depth\":%u}",
                i ? "," : "", e.sequence, e.generation, e.at_ms, e.kind, e.lifecycle, e.thread_id, e.depth);
        }
        std::puts("]}");
    } else {
        std::printf("Sentinel %s | native ABI=1 | PID=%u (OS server verified)\nbuild=%s\n"
            "process_created=%llu instance_id=%s\nnative=%s reason=%s hooks=%u retained=%u\n"
            "site RVA=0x%x revision=%u phase=%u | callback=%llu thread=%u native_owner=%u\n"
            "generation=%llu lifecycle=%s depth=%u context_generation=%llu context=%s map=%s\n"
            "events=%llu gaps=%llu history_oldest=%llu overwritten=%llu consumer_gap=%u | queued=%u claimed=%u results=%u\n"
            "Coverage=%u (partial); native_load_serial unsupported; gameplay authorization unvalidated.\n",
            s.core.version, s.pid, s.core.build_id, s.process_created, sentinel::instance_text(s.instance).c_str(),
            availability[n.availability], sentinel::native_reason_name(n.reason), n.installed_hooks, n.retained_module,
            n.site_rva, n.site_revision, n.phase, n.callback_sequence, n.callback_thread_id, n.native_owner_thread_id,
            n.scope.lifecycle_generation, lifecycle[n.lifecycle], n.depth, n.context_generation,
            sentinel::native_reason_name(n.context_reason), name.c_str(), n.event_sequence, n.event_gap_count,
            n.history_oldest, n.history_overwritten, n.history_gap, n.queued, n.claimed, n.retained_results, n.coverage);
    }
}
void print_diagnostic(const sentinel::Inspection& r, const sc_diagnostic_request& request, bool json) {
    const auto& d = r.diagnostic;
    const auto name = d.current_map.length ? quoted(std::string(d.current_map.bytes, d.current_map.length)) : "null";
    if (json) {
        std::printf("{\"result\":\"ok\",\"operation\":\"diagnostic\",\"core_version\":%s,\"build_id\":%s,"
            "\"target_pid\":%u,\"server_pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"%s\","
            "\"request_id\":\"%llu\",\"nonce\":\"%s\",\"expected_generation\":\"%llu\",\"lifecycle_generation\":\"%llu\","
            "\"state\":\"%s\",\"reason\":\"%s\",\"cancel_requested\":%s,\"retrieved\":%s,\"deadline_ms\":%u,"
            "\"admitted_at_ms\":\"%llu\",\"deadline_at_ms\":\"%llu\",\"claimed_at_ms\":\"%llu\","
            "\"observed_at_ms\":\"%llu\",\"executed_at_ms\":\"%llu\",\"completed_at_ms\":\"%llu\",\"retrieved_at_ms\":\"%llu\","
            "\"thread_id\":%u,\"site_revision\":%u,\"phase\":%u,\"lifecycle\":%u,\"game_state\":%s,\"current_map\":%s",
            quoted(r.snapshot.core.version).c_str(), quoted(r.snapshot.core.build_id).c_str(), r.snapshot.pid, r.server_pid,
            r.snapshot.process_created, sentinel::instance_text(r.snapshot.instance).c_str(), d.request_id,
            hex_text(d.nonce).c_str(), request.expected.lifecycle_generation, d.scope.lifecycle_generation,
            sentinel::diagnostic_state_name(d.state), sentinel::native_reason_name(d.reason), d.cancel_requested ? "true" : "false",
            d.retrieved ? "true" : "false", request.deadline_ms, d.admitted_at_ms, d.deadline_at_ms, d.claimed_at_ms,
            d.observed_at_ms, d.executed_at_ms, d.completed_at_ms, d.retrieved_at_ms, d.thread_id, d.site_revision, d.phase, d.lifecycle,
            d.state == SC_DIAGNOSTIC_EXECUTED ? std::to_string(d.game_state).c_str() : "null", name.c_str());
        const auto& e = r.detail;
        std::printf(",\"game_state_observed\":%s,\"detail\":{\"revision\":%u,\"stage\":\"%s\","
            "\"observation_attempted\":%s,\"observation_accepted\":%s,\"timing_clock\":\"QPC_wall_elapsed\","
            "\"timing_valid\":%s,\"timing_error\":%u,\"claim_lock_missed_since_admission\":%s,"
            "\"observation_started_at_ms\":\"%llu\",\"observation_elapsed_ns\":\"%llu\",\"observation_budget_ns\":\"%llu\","
            "\"sample_reason\":\"%s\",\"state_validity\":\"%s\",\"state_reason\":\"%s\",\"state_win32_error\":%u,"
            "\"map_validity\":\"%s\",\"map_reason\":\"%s\",\"map_win32_error\":%u,"
            "\"pending_before\":%u,\"pending_before_reason\":\"%s\",\"pending_before_win32_error\":%u,"
            "\"pending_after\":%u,\"pending_after_reason\":\"%s\",\"pending_after_win32_error\":%u}}\n",
            d.state == SC_DIAGNOSTIC_EXECUTED ? "true" : "false", e.revision, sentinel::diagnostic_stage_name(e.stage),
            e.observation_attempted ? "true" : "false", e.observation_accepted ? "true" : "false",
            e.timing_valid ? "true" : "false", e.timing_error, e.claim_lock_missed ? "true" : "false",
            e.observation_started_at_ms, e.observation_elapsed_ns, e.observation_budget_ns,
            sentinel::context_reason_name(e.sample_reason), sentinel::validity_name(e.state_validity),
            sentinel::context_reason_name(e.state_reason), e.state_error, sentinel::validity_name(e.map_validity),
            sentinel::context_reason_name(e.map_reason), e.map_error,
            e.pending_before, sentinel::reason_name(e.pending_before_reason), e.pending_before_error,
            e.pending_after, sentinel::reason_name(e.pending_after_reason), e.pending_after_error);
    } else std::printf("diagnostic request=%llu nonce=%s state=%s reason=%s cancel_requested=%u retrieved=%u\n"
        "expected_generation=%llu observed_generation=%llu thread=%u phase=%u map=%s\n"
        "admitted=%llu claimed=%llu executed=%llu completed=%llu retrieved=%llu deadline=%llu ms\n"
        "accepted_observation_completed=%llu ms\n",
        d.request_id, hex_text(d.nonce).c_str(), sentinel::diagnostic_state_name(d.state), sentinel::native_reason_name(d.reason),
        d.cancel_requested, d.retrieved, request.expected.lifecycle_generation, d.scope.lifecycle_generation, d.thread_id, d.phase,
        name.c_str(), d.admitted_at_ms, d.claimed_at_ms, d.executed_at_ms, d.completed_at_ms, d.retrieved_at_ms, d.deadline_at_ms, d.observed_at_ms);
    if (!json) {
        const auto& e = r.detail;
        std::printf("detail revision=%u stage=%s observation_attempted=%u accepted=%u QPC_elapsed_ns=%llu budget_ns=%llu timing_valid=%u error=%u\n"
            "observation_started_uptime_ms=%llu claim_lock_missed=%u sample=%s state=%s/%s win32=%u map=%s/%s win32=%u\n"
            "pending_before=%u reason=%s win32=%u pending_after=%u reason=%s win32=%u (0=unknown,1=clear,2=set,3=unreadable); result game_state_observed=%u\n",
            e.revision, sentinel::diagnostic_stage_name(e.stage), e.observation_attempted, e.observation_accepted,
            e.observation_elapsed_ns, e.observation_budget_ns, e.timing_valid, e.timing_error,
            e.observation_started_at_ms, e.claim_lock_missed, sentinel::context_reason_name(e.sample_reason),
            sentinel::validity_name(e.state_validity), sentinel::context_reason_name(e.state_reason), e.state_error,
            sentinel::validity_name(e.map_validity), sentinel::context_reason_name(e.map_reason), e.map_error,
            e.pending_before, sentinel::reason_name(e.pending_before_reason), e.pending_before_error,
            e.pending_after, sentinel::reason_name(e.pending_after_reason), e.pending_after_error, d.state == SC_DIAGNOSTIC_EXECUTED);
    }
}
void print_context(const sentinel::Inspection& r, uint32_t pid, bool json) {
    const auto& s = r.snapshot; const auto& c = r.context; const auto& m = c.current_map;
    const auto id = sentinel::instance_text(s.instance);
    const auto now = GetTickCount64();
    const auto age = c.sampled_at_ms && now >= c.sampled_at_ms ? std::to_string(now - c.sampled_at_ms) : "null";
    const char* profile = c.profile == SC_PROFILE_STEAM_20260818 ? "steam_2026_08_18" : "unrecognized";
    const auto name = m.validity == SC_OBSERVATION_OBSERVED ? quoted(std::string(m.bytes, m.length)) : "null";
    SYSTEMTIME utc{}; GetSystemTime(&utc); char timestamp[32]{};
    sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", utc.wYear, utc.wMonth, utc.wDay,
              utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
    if (json) {
        std::printf("{\"result\":\"ok\",\"operation\":\"context\",\"wire_version\":1,\"context_abi\":%u,"
            "\"context_query_capability\":4,\"core_abi\":%u,\"core_version\":%s,\"build_id\":%s,"
            "\"target_pid\":%u,\"server_pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"%s\",\"host_path\":%s,"
            "\"queried_at_utc\":\"%s\",\"sequence\":\"%llu\",\"sampled_at_ms\":\"%llu\",\"sample_age_ms\":%s,"
            "\"sample_duration_ms\":%u,\"sample_reason\":\"%s\",\"disk_sha256\":%s,\"disk_hash_reason\":\"%s\","
            "\"profile\":\"%s\",\"locator_revision\":%u,\"layout_revision\":%u,\"root_locator_reason\":\"%s\","
            "\"observation_scope\":\"bounded_double_read\",\"evidence_basis\":\"static_reverse_engineering\","
            "\"gameplay_authorization\":\"unvalidated\",\"current_map\":{\"validity\":\"%s\",\"reason\":\"%s\","
            "\"value\":%s,\"length\":%u,\"win32_error\":%u},\"fields\":{",
            c.abi_version, s.core.abi_version, quoted(s.core.version).c_str(), quoted(s.core.build_id).c_str(),
            pid, r.server_pid, s.process_created, id.c_str(), quoted(utf8(r.host_path)).c_str(), timestamp,
            c.sequence, c.sampled_at_ms, age.c_str(), c.duration_ms, sentinel::context_reason_name(c.sample_reason),
            c.disk_hash_reason ? "null" : quoted(c.disk_sha256).c_str(), sentinel::reason_name(c.disk_hash_reason),
            profile, c.locator_revision, c.layout_revision, sentinel::reason_name(c.root_locator_reason),
            sentinel::validity_name(m.validity), sentinel::context_reason_name(m.reason), name.c_str(), m.length, m.win32_error);
    } else {
        std::printf("Sentinel %s | context ABI=%u wire=1 | PID=%u (OS server verified)\nbuild=%s\n"
            "process_created=%llu instance_id=%s\nhost=%s\nprofile=%s locator=%u layout=%u root=%s\n"
            "disk SHA256=%s (%s)\nsample=%llu tick_ms=%llu age_ms=%s duration_ms=%u reason=%s\n"
            "current_map=%s validity=%s reason=%s length=%u win32=%u\n",
            s.core.version, c.abi_version, r.server_pid, s.core.build_id, s.process_created, id.c_str(),
            utf8(r.host_path).c_str(), profile, c.locator_revision, c.layout_revision, sentinel::reason_name(c.root_locator_reason),
            c.disk_hash_reason ? "unavailable" : c.disk_sha256, sentinel::reason_name(c.disk_hash_reason),
            c.sequence, c.sampled_at_ms, age.c_str(), c.duration_ms, sentinel::context_reason_name(c.sample_reason),
            name.c_str(), sentinel::validity_name(m.validity), sentinel::context_reason_name(m.reason), m.length, m.win32_error);
    }
    for (size_t i = 0; i < SC_CONTEXT_FIELD_COUNT; ++i) {
        const auto& f = c.fields[i]; const auto value = f.validity == SC_OBSERVATION_UNKNOWN ? "null" : std::to_string(f.value);
        if (json) std::printf("%s\"%s\":{\"validity\":\"%s\",\"reason\":\"%s\",\"value\":%s,\"win32_error\":%u}",
            i ? "," : "", sentinel::context_field_name(i), sentinel::validity_name(f.validity),
            sentinel::context_reason_name(f.reason), value.c_str(), f.win32_error);
        else std::printf("  %s=%s validity=%s reason=%s win32=%u\n", sentinel::context_field_name(i), value.c_str(),
            sentinel::validity_name(f.validity), sentinel::context_reason_name(f.reason), f.win32_error);
    }
    if (json) std::puts("}}");
    else std::puts("game_state: 0=main menu, 1=loading, 2=in game. state_changed_ms wraps; it is not a load serial.\n"
        "Bounded observation; static RE basis, runtime validation recorded externally. Gameplay authorization unvalidated.");
}
void print_save(const sentinel::Inspection& r, bool json) {
    const auto& v = r.save; const auto& s = r.snapshot;
    const auto now = GetTickCount64();
    const auto age = v.sampled_at_ms && now >= v.sampled_at_ms ? std::to_string(now - v.sampled_at_ms) : "null";
    if (json) std::printf("{\"result\":\"ok\",\"operation\":\"save_context\",\"wire_version\":1,\"save_abi\":%u,"
        "\"save_query_capability\":64,\"core_version\":%s,\"build_id\":%s,\"target_pid\":%u,\"server_pid\":%u,"
        "\"process_created\":\"%llu\",\"instance_id\":\"%s\",\"host_path\":%s,\"sequence\":\"%llu\","
        "\"sampled_at_ms\":\"%llu\",\"sample_age_ms\":%s,\"sample_duration_ms\":%u,\"sample_reason\":\"%s\","
        "\"profile\":%u,\"layout_revision\":%u,\"root_locator_reason\":\"%s\",\"mutation_available\":false,"
        "\"mutation_reason\":\"%s\",\"provider\":\"%s\",\"native_runtime_evidence\":\"pending\",\"fields\":{",
        v.abi_version, quoted(s.core.version).c_str(), quoted(s.core.build_id).c_str(), s.pid, r.server_pid,
        s.process_created, sentinel::instance_text(s.instance).c_str(), quoted(utf8(r.host_path)).c_str(),
        v.sequence, v.sampled_at_ms, age.c_str(), v.duration_ms, sentinel::save_reason_name(v.sample_reason),
        v.profile, v.layout_revision, sentinel::reason_name(v.root_locator_reason), sentinel::save_reason_name(v.mutation_reason),
        sentinel::save_provider_name(v.fields[SC_SAVE_PROVIDER].value));
    else std::printf("Sentinel %s | save ABI=%u wire=1 | PID=%u (OS server verified)\nbuild=%s\n"
        "process_created=%llu instance_id=%s\nprofile=%u layout_revision=%u root_locator=%s\n"
        "sample=%llu tick_ms=%llu age_ms=%s duration_ms=%u reason=%s\nmutation_available=no reason=%s\nprovider=%s\n",
        s.core.version, v.abi_version, r.server_pid, s.core.build_id, s.process_created, sentinel::instance_text(s.instance).c_str(),
        v.profile, v.layout_revision, sentinel::reason_name(v.root_locator_reason), v.sequence, v.sampled_at_ms,
        age.c_str(), v.duration_ms, sentinel::save_reason_name(v.sample_reason), sentinel::save_reason_name(v.mutation_reason),
        sentinel::save_provider_name(v.fields[SC_SAVE_PROVIDER].value));
    for (size_t i = 0; i < SC_SAVE_FIELD_COUNT; ++i) {
        const auto& f = v.fields[i];
        const auto value = f.validity == SC_OBSERVATION_UNKNOWN ? "null" : std::to_string(f.value);
        if (json) std::printf("%s\"%s\":{\"validity\":\"%s\",\"reason\":\"%s\",\"value\":%s,\"win32_error\":%u}",
            i ? "," : "", sentinel::save_field_name(i), sentinel::validity_name(f.validity),
            sentinel::save_reason_name(f.reason), value.c_str(), f.win32_error);
        else std::printf("  %s=%s validity=%s reason=%s win32=%u\n", sentinel::save_field_name(i), value.c_str(),
            sentinel::validity_name(f.validity), sentinel::save_reason_name(f.reason), f.win32_error);
    }
    if (json) std::puts("}}");
    else std::puts("Zero queued requests or job witnesses does not prove idle, completion or persistence.\n"
        "Native selected slot, namespace routing and completion remain unproven; manual runtime evidence is pending.");
}
int print_admission(const sentinel::Inspection& r, bool json) {
    const auto& v = r.admission; const auto& s = r.snapshot;
    if (json) std::printf("{\"result\":\"ok\",\"operation\":\"save_admission\",\"wire_version\":1,"
        "\"admission_abi\":%u,\"core_version\":%s,\"build_id\":%s,\"target_pid\":%u,\"server_pid\":%u,"
        "\"process_created\":\"%llu\",\"instance_id\":\"%s\",\"state\":\"%s\",\"fault\":\"%s\","
        "\"prepared_routes\":%u,\"required_routes\":%u,\"startup_qualified\":%s,\"route_retained\":%s,"
        "\"accepting_requests\":%s,\"namespace_id\":\"%s\",\"native_root\":\"%s\"}\n",
        v.abi_version, quoted(s.core.version).c_str(), quoted(s.core.build_id).c_str(), s.pid, r.server_pid, s.process_created,
        sentinel::instance_text(s.instance).c_str(), sentinel::save_session_state_name(v.state), sentinel::save_session_fault_name(v.fault),
        v.prepared_routes, v.required_routes, v.flags & SC_SAVE_SESSION_STARTUP_QUALIFIED ? "true" : "false",
        v.flags & SC_SAVE_SESSION_ROUTED ? "true" : "false", v.flags & SC_SAVE_SESSION_ACCEPTING ? "true" : "false",
        v.namespace_id, v.native_root);
    else std::printf("Sentinel %s | admission ABI=%u | PID=%u (OS server verified)\nbuild=%s\n"
        "state=%s fault=%s prepared_routes=%u required_routes=%u flags=%u\nnamespace=%s native_root=%s\n",
        s.core.version, v.abi_version, s.pid, s.core.build_id, sentinel::save_session_state_name(v.state),
        sentinel::save_session_fault_name(v.fault), v.prepared_routes, v.required_routes, v.flags, v.namespace_id, v.native_root);
    // A readable status is not a successful preflight. Existing observation CLI exits are unchanged.
    const bool admitted = v.state == SC_SAVE_SESSION_ADMITTED && (v.flags & SC_SAVE_SESSION_ACCEPTING);
    if (!admitted) std::fputs("AP admission failed or is not ready; vanilla slots may remain visible. Do not create or load a campaign. Continue safe installation/context queries.\n", stderr);
    return admitted ? 0 : 8;
}
void print_save_write(const sentinel::Inspection& r, bool json) {
    const auto& v = r.write; const auto& s = r.snapshot;
    const auto native_result = v.flags & SC_SAVE_WRITE_PROVIDER_TERMINAL ?
        std::string("{\"state\":") + std::to_string(v.native_state) + ",\"outcome\":" +
        std::to_string(v.native_outcome) + ",\"value\":" + std::to_string(v.native_value) + "}" : "null";
    if (json) std::printf("{\"result\":\"ok\",\"operation\":\"save_write\",\"wire_version\":1,\"write_abi\":%u,"
        "\"core_version\":%s,\"build_id\":%s,\"target_pid\":%u,\"server_pid\":%u,\"process_created\":\"%llu\","
        "\"instance_id\":\"%s\",\"operation_id\":\"%llu\",\"sdk_sequence\":\"%llu\",\"state\":\"%s\","
        "\"directory\":%s,\"flags\":%u,\"file_count\":%u,\"submitted\":%u,\"completed\":%u,"
        "\"pending_handles\":%u,\"preparation_jobs\":%u,\"preflight_jobs\":%u,\"native_result\":%s,"
        "\"readback_verified\":%s,\"persistence_verified\":false,\"reopen_verified\":false}\n",
        v.abi_version, quoted(s.core.version).c_str(), quoted(s.core.build_id).c_str(), s.pid, r.server_pid,
        s.process_created, sentinel::instance_text(s.instance).c_str(), v.operation_id, v.sdk_sequence,
        sentinel::save_write_state_name(v.state), quoted(v.directory).c_str(), v.flags, v.file_count, v.submitted,
        v.completed, v.pending_handles, v.preparation_jobs, v.preflight_jobs, native_result.c_str(),
        v.state == SC_SAVE_WRITE_READBACK_CONFIRMED ? "true" : "false");
    else std::printf("Sentinel %s | write ABI=%u | PID=%u (OS server verified)\nbuild=%s\n"
        "operation=%llu sdk_sequence=%llu state=%s directory=%s flags=%u\n"
        "files=%u submitted=%u completed=%u pending_handles=%u preparation_jobs=%u preflight_jobs=%u native_result=%s\n"
        "Readback confirms transport bytes; playable reopen and global idle remain separate.\n",
        s.core.version, v.abi_version, s.pid, s.core.build_id, v.operation_id, v.sdk_sequence,
        sentinel::save_write_state_name(v.state), v.directory, v.flags, v.file_count, v.submitted, v.completed,
        v.pending_handles, v.preparation_jobs, v.preflight_jobs, native_result.c_str());
}
int print_result(const sentinel::Inspection& r, uint32_t pid, bool json, bool engine, bool context, bool save = false) {
    const char* result = sentinel::result_name(r.result);
    if (r.result != sentinel::ProbeResult::ok) {
        if (json) std::printf("{\"result\":\"%s\",\"target_pid\":%u,\"win32_error\":%u,\"failure_stage\":\"%s\","
            "\"target_state\":\"%s\",\"target_wait_error\":%u,\"verified_process_created\":\"%llu\"}\n",
            result, pid, r.win32_error, r.failure_stage, r.target_state, r.target_wait_error, r.verified_process_created);
        else std::printf("Sentinel inspection: %s (PID %u, win32=%u stage=%s target=%s wait_error=%u created=%llu). Use --help for usage.\n",
            result, pid, r.win32_error, r.failure_stage, r.target_state, r.target_wait_error, r.verified_process_created);
        return static_cast<int>(r.result);
    }
    const auto& s = r.snapshot;
    if (save) { print_save(r, json); return 0; }
    if (context) { print_context(r, pid, json); return 0; }
    const auto id = sentinel::instance_text(s.instance);
    if (engine) {
        const auto& e = r.engine;
        const char* profile = e.profile == SC_PROFILE_STEAM_20260818 ? "steam_2026_08_18" : "unrecognized";
        const auto now = GetTickCount64();
        const auto age = e.sampled_at_ms && now >= e.sampled_at_ms ? now - e.sampled_at_ms : 0;
        if (json) {
            std::printf("{\"result\":\"ok\",\"operation\":\"engine\",\"wire_version\":1,\"engine_abi\":%u,"
                "\"engine_query_capability\":2,\"core_abi\":%u,\"core_version\":%s,\"build_id\":%s,"
                "\"target_pid\":%u,\"server_pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"%s\",\"host_path\":%s,"
                "\"sequence\":\"%llu\",\"sampled_at_ms\":\"%llu\",\"sample_age_ms\":%llu,\"sample_duration_ms\":%u,"
                "\"sample_reason\":\"%s\",\"pe_reason\":\"%s\",\"pe_machine\":%u,\"pe_timestamp\":%u,\"pe_image_size\":%u,\"pe_entry_rva\":%u,"
                "\"disk_sha256\":%s,\"disk_hash_reason\":\"%s\",\"profile\":\"%s\",\"locator_revision\":%u,"
                "\"root_locator_reason\":\"%s\",\"root_signature_rva\":%u,\"root_target_rva\":%u,"
                "\"profile_runtime_validated\":false,\"gameplay_authorization\":\"unvalidated\",\"fields\":{",
                e.abi_version, s.core.abi_version, quoted(s.core.version).c_str(), quoted(s.core.build_id).c_str(),
                pid, r.server_pid, s.process_created, id.c_str(), quoted(utf8(r.host_path)).c_str(),
                e.sequence, e.sampled_at_ms, age, e.duration_ms, sentinel::reason_name(e.sample_reason),
                sentinel::reason_name(e.pe_reason), e.machine, e.timestamp, e.image_size, e.entry_rva,
                e.disk_hash_reason ? "null" : quoted(e.disk_sha256).c_str(), sentinel::reason_name(e.disk_hash_reason), profile,
                e.locator_revision, sentinel::reason_name(e.root_locator_reason), e.root_signature_rva, e.root_target_rva);
            for (size_t i = 0; i < SC_ENGINE_FIELD_COUNT; ++i) {
                const auto& f = e.fields[i];
                const auto value = f.validity == SC_OBSERVATION_UNKNOWN ? "null" : std::to_string(f.value);
                std::printf("%s\"%s\":{\"validity\":\"%s\",\"reason\":\"%s\",\"value\":%s,\"win32_error\":%u}",
                    i ? "," : "", sentinel::field_name(i), sentinel::validity_name(f.validity),
                    sentinel::reason_name(f.reason), value.c_str(), f.win32_error);
            }
            std::puts("}}");
        } else {
            std::printf("Sentinel %s | engine ABI=%u wire=1 | PID=%u (OS server verified)\nbuild=%s\n"
                "process_created=%llu instance_id=%s\nprofile=%s revision=%u runtime_validated=no\n"
                "loaded PE: reason=%s machine=0x%x timestamp=0x%x image_size=0x%x entry=0x%x\n"
                "disk SHA256=%s (%s; disk-file evidence only)\nroot locator=%s signature_rva=0x%x target_rva=0x%x\n"
                "sample=%llu tick_ms=%llu age_ms=%llu duration_ms=%u reason=%s\n",
                s.core.version, e.abi_version, r.server_pid, s.core.build_id, s.process_created, id.c_str(), profile, e.locator_revision,
                sentinel::reason_name(e.pe_reason), e.machine, e.timestamp, e.image_size, e.entry_rva,
                e.disk_hash_reason ? "unavailable" : e.disk_sha256, sentinel::reason_name(e.disk_hash_reason),
                sentinel::reason_name(e.root_locator_reason), e.root_signature_rva, e.root_target_rva,
                e.sequence, e.sampled_at_ms, age, e.duration_ms, sentinel::reason_name(e.sample_reason));
            for (size_t i = 0; i < SC_ENGINE_FIELD_COUNT; ++i) {
                const auto& f = e.fields[i];
                const auto value = f.validity == SC_OBSERVATION_UNKNOWN ? "unknown" : std::to_string(f.value);
                std::printf("  %s=%s validity=%s reason=%s win32=%u\n", sentinel::field_name(i), value.c_str(),
                    sentinel::validity_name(f.validity), sentinel::reason_name(f.reason), f.win32_error);
            }
            std::puts("Presence is pointer-slot telemetry; gameplay authorization remains unvalidated.");
        }
        return 0;
    }
    const auto slash = r.host_path.find_last_of(L"\\/");
    const auto name = r.host_path.substr(slash == std::wstring::npos ? 0 : slash + 1);
    const char* host = _wcsicmp(name.c_str(), L"DOOMEternalx64vk.exe") == 0 ? "doom_executable_name_match" : "non_game_host";
    const char* state = s.core.state == SC_READY ? "ready" : (s.core.state == SC_COLD ? "cold" : "stopped");
    if (json) {
        std::printf("{\"result\":\"ok\",\"wire_version\":%u,\"core_abi\":%u,\"core_version\":%s,\"build_id\":%s,"
            "\"target_pid\":%u,\"server_pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"%s\","
            "\"host_kind\":\"%s\",\"host_path\":%s,\"core_state\":\"%s\",\"ipc_state\":\"%s\",\"ipc_error\":%u,"
            "\"core_capabilities\":%llu,\"inspection_capabilities\":1,\"initialization_count\":%u,\"last_result\":%u,"
            "\"engine_integration\":\"unavailable\",\"gameplay_safety\":\"unprobed\",\"game_build\":\"unprobed\"}\n",
            sentinel::wire_version, s.core.abi_version, quoted(s.core.version).c_str(), quoted(s.core.build_id).c_str(),
            pid, r.server_pid, s.process_created, id.c_str(), host, quoted(utf8(r.host_path)).c_str(), state,
            sentinel::service_name(s.service), s.service_error, s.core.capabilities, s.core.initialization_count, s.core.last_result);
    } else {
        std::printf("Sentinel %s | wire=%u ABI=%u | PID=%u (OS server verified, %s)\n"
            "build=%s\nprocess_created=%llu instance_id=%s\nCore=%s IPC=%s | capabilities: core=%llu inspection=1\n"
            "Engine integration=unavailable | gameplay safety=unprobed | game build=unprobed\n",
            s.core.version, sentinel::wire_version, s.core.abi_version, r.server_pid, host, s.core.build_id,
            s.process_created, id.c_str(), state, sentinel::service_name(s.service), s.core.capabilities);
    }
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    const int storage_result = save_storage_command(argc, argv);
    if (storage_result >= 0) return storage_result;
    const int backup_result = native_backup_command(argc, argv);
    if (backup_result >= 0) return backup_result;
    uint32_t pid = 0, timeout = 2000, count = 1, interval = 1000;
    bool json = false, engine = false, context = false, native = false, save = false, admission = false, write = false, valid = true;
    bool installation = false;
    uint64_t write_id = 0; bool saw_write_id = false;
    uint16_t diagnostic_op = 0;
    sc_diagnostic_request request{}; request.deadline_ms = 1000;
    bool saw_created = false, saw_instance = false, saw_generation = false, saw_nonce = false, saw_id = false, saw_deadline = false;
    bool saw_pid = false, saw_timeout = false, saw_count = false, saw_interval = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--help") == 0 && argc == 2) {
            std::puts("sentinel_probe --pid PID [--timeout-ms 50..10000] [--json] [--engine | --context | --native | --save-context]\n"
                "Capture: --context, --engine, --native or --save-context --watch-count 1..600 [--interval-ms 100..10000]\n"
                "Save context: read-only provider/request witnesses.\n"
                "Admission preflight: --save-admission (one query; exit 8 unless admitted and accepting).\n"
                "Installation evidence: --save-installation (read-only, retained after refusal).\n"
                "Write evidence: --save-write [--write-id N] (latest if omitted); supports capture.\n"
                "Native backup: --native-backup --help (explicit owned campaign save, readback and local archive).\n"
                "  SDK confirmation is separate from persistence and reopen; observation exit 0 is not save success.\n"
                "Harmless diagnostic: --diagnostic [--deadline-ms 1..5000] (submit then retrieve; no engine commands).\n"
                "Retrieve/cancel: --diagnostic-result or --diagnostic-cancel, with --request-id N --nonce HEX32\n"
                "  --expect-created N --expect-instance HEX32 --expect-generation N (from the prepared request).\n"
                "Defaults: timeout 2000 ms, interval 1000 ms. Capture scheduling window <= 10 minutes.\n"
                "One fresh query per record; stop on first error, target exit/restart, or Ctrl+C.\n"
                "Uses an already loaded Core; --json capture emits JSON Lines. Native backup requires admitted AP ownership.");
            return 0;
        }
        if (wcscmp(argv[i], L"--json") == 0 && !json) json = true;
        else if (wcscmp(argv[i], L"--engine") == 0 && !engine) engine = true;
        else if (wcscmp(argv[i], L"--context") == 0 && !context) context = true;
        else if (wcscmp(argv[i], L"--native") == 0 && !native) native = true;
        else if (wcscmp(argv[i], L"--save-context") == 0 && !save) save = true;
        else if (wcscmp(argv[i], L"--save-admission") == 0 && !admission) admission = true;
        else if (wcscmp(argv[i], L"--save-installation") == 0 && !installation) installation = true;
        else if (wcscmp(argv[i], L"--save-write") == 0 && !write) write = true;
        else if (wcscmp(argv[i], L"--write-id") == 0 && !saw_write_id && i + 1 < argc) {
            saw_write_id = true; valid = number64(argv[++i], write_id) && write_id && valid;
        }
        else if (wcscmp(argv[i], L"--diagnostic") == 0 && !diagnostic_op) diagnostic_op = sentinel::diagnostic_detail_submit_operation;
        else if (wcscmp(argv[i], L"--diagnostic-result") == 0 && !diagnostic_op) diagnostic_op = sentinel::diagnostic_detail_result_operation;
        else if (wcscmp(argv[i], L"--diagnostic-cancel") == 0 && !diagnostic_op) diagnostic_op = sentinel::diagnostic_detail_cancel_operation;
        else if (wcscmp(argv[i], L"--deadline-ms") == 0 && !saw_deadline && i + 1 < argc) {
            saw_deadline = true; valid = number(argv[++i], request.deadline_ms) && valid;
        } else if (wcscmp(argv[i], L"--expect-created") == 0 && !saw_created && i + 1 < argc) {
            saw_created = true; valid = number64(argv[++i], request.expected.process_created) && valid;
        } else if (wcscmp(argv[i], L"--expect-instance") == 0 && !saw_instance && i + 1 < argc) {
            saw_instance = true; valid = hex16(argv[++i], request.expected.instance_id) && valid;
        } else if (wcscmp(argv[i], L"--expect-generation") == 0 && !saw_generation && i + 1 < argc) {
            saw_generation = true; valid = number64(argv[++i], request.expected.lifecycle_generation) && valid;
        } else if (wcscmp(argv[i], L"--request-id") == 0 && !saw_id && i + 1 < argc) {
            saw_id = true; valid = number64(argv[++i], request.request_id) && valid;
        } else if (wcscmp(argv[i], L"--nonce") == 0 && !saw_nonce && i + 1 < argc) {
            saw_nonce = true; valid = hex16(argv[++i], request.nonce) && valid;
        }
        else if (wcscmp(argv[i], L"--pid") == 0 && !saw_pid && i + 1 < argc) {
            saw_pid = true; valid = number(argv[++i], pid) && valid;
        } else if (wcscmp(argv[i], L"--timeout-ms") == 0 && !saw_timeout && i + 1 < argc) {
            saw_timeout = true; valid = number(argv[++i], timeout) && valid;
        } else if (wcscmp(argv[i], L"--watch-count") == 0 && !saw_count && i + 1 < argc) {
            saw_count = true; valid = number(argv[++i], count) && valid;
        } else if (wcscmp(argv[i], L"--interval-ms") == 0 && !saw_interval && i + 1 < argc) {
            saw_interval = true; valid = number(argv[++i], interval) && valid;
        } else valid = false;
    }
    if (!valid || !pid || timeout < sentinel::min_timeout_ms || timeout > sentinel::max_timeout_ms ||
        !count || count > 600 || interval < 100 || interval > 10000 || uint64_t(count) * interval > 600000 ||
        (unsigned(engine) + unsigned(context) + unsigned(native) + unsigned(save) + unsigned(admission) + unsigned(write) + unsigned(installation) + unsigned(diagnostic_op != 0) > 1) ||
        (saw_count && !engine && !context && !native && !save && !write) || (saw_interval && !saw_count) || (saw_write_id && !write) ||
        (saw_deadline && !diagnostic_op) || !request.deadline_ms || request.deadline_ms > SC_DIAGNOSTIC_MAX_DEADLINE_MS ||
        ((saw_created || saw_instance || saw_generation || saw_id || saw_nonce) && diagnostic_op <= sentinel::diagnostic_detail_submit_operation) ||
        (diagnostic_op > sentinel::diagnostic_detail_submit_operation && !(saw_created && saw_instance && saw_generation && saw_id && saw_nonce))) {
        sentinel::Inspection error; error.result = sentinel::ProbeResult::usage;
        return print_result(error, pid, json, engine, context);
    }
    sentinel::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stop) { sentinel::Inspection error; error.win32_error = GetLastError(); return print_result(error, pid, json, engine, context); }
    interrupt_event = stop.value;
    SetConsoleCtrlHandler(interrupt, TRUE);
    if (diagnostic_op) {
        request.expected.pid = pid;
        if (diagnostic_op == sentinel::diagnostic_detail_submit_operation) {
            const auto initial = sentinel::query_native(pid, timeout);
            if (initial.result != sentinel::ProbeResult::ok) return print_result(initial, pid, json, false, false);
            if (!initial.native.context_generation || initial.native.availability != SC_NATIVE_ENABLED) {
                print_native(initial, json); return static_cast<int>(sentinel::ProbeResult::capability_unavailable);
            }
            request.expected = initial.native.scope; request.request_id = GetTickCount64();
            if (BCryptGenRandom(nullptr, request.nonce, sizeof(request.nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 10;
            if (json) std::printf("{\"operation\":\"diagnostic_request\",\"state\":\"prepared\",\"target_pid\":%u,"
                "\"process_created\":\"%llu\",\"instance_id\":\"%s\",\"expected_generation\":\"%llu\",\"request_id\":\"%llu\",\"nonce\":\"%s\",\"deadline_ms\":%u}\n",
                pid, request.expected.process_created, hex_text(request.expected.instance_id).c_str(),
                request.expected.lifecycle_generation, request.request_id, hex_text(request.nonce).c_str(), request.deadline_ms);
            else std::printf("prepared request=%llu nonce=%s process_created=%llu instance=%s generation=%llu deadline_ms=%u\n",
                request.request_id, hex_text(request.nonce).c_str(), request.expected.process_created,
                hex_text(request.expected.instance_id).c_str(), request.expected.lifecycle_generation, request.deadline_ms);
            std::fflush(stdout);
        }
        auto r = sentinel::query_diagnostic(pid, timeout, diagnostic_op, request);
        const auto until = GetTickCount64() + request.deadline_ms + timeout;
        for (;;) {
            if (r.result != sentinel::ProbeResult::ok) return print_result(r, pid, json, false, false);
            print_diagnostic(r, request, json); std::fflush(stdout);
            if (diagnostic_op != sentinel::diagnostic_detail_submit_operation || r.diagnostic.state >= SC_DIAGNOSTIC_EXECUTED ||
                r.diagnostic.state == SC_DIAGNOSTIC_UNKNOWN || GetTickCount64() >= until) break;
            if (WaitForSingleObject(stop.value, 100) != WAIT_TIMEOUT) {
                r = sentinel::query_diagnostic(pid, timeout, sentinel::diagnostic_detail_cancel_operation, request);
                if (r.result != sentinel::ProbeResult::ok) return print_result(r, pid, json, false, false);
                print_diagnostic(r, request, json); break;
            }
            r = sentinel::query_diagnostic(pid, timeout, sentinel::diagnostic_detail_result_operation, request);
        }
        SetConsoleCtrlHandler(interrupt, FALSE);
        return r.diagnostic.state == SC_DIAGNOSTIC_EXECUTED ? 0 : 8;
    }
    sentinel::Snapshot first{};
    uint64_t after_event = 0;
    const auto deadline = GetTickCount64() + uint64_t(count) * interval;
    int result = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (WaitForSingleObject(stop.value, 0) != WAIT_TIMEOUT) break;
        const auto started = GetTickCount64();
        auto r = installation ? sentinel::query_save_installation(pid, timeout) : write ? sentinel::query_save_write(pid, timeout, write_id) :
            admission ? sentinel::query_save_admission(pid, timeout) : (save ? sentinel::query_save(pid, timeout) :
            (native ? sentinel::query_native(pid, timeout, after_event) :
            (context ? sentinel::query_context(pid, timeout) : (engine ? sentinel::query_engine(pid, timeout) : sentinel::query(pid, timeout)))));
        if (r.result == sentinel::ProbeResult::ok && i &&
            (r.snapshot.process_created != first.process_created || r.snapshot.instance != first.instance))
            r.result = sentinel::ProbeResult::process_mismatch;
        if (!i) first = r.snapshot;
        if (installation && r.result == sentinel::ProbeResult::ok) print_installation(r, json);
        else if (write && r.result == sentinel::ProbeResult::ok) print_save_write(r, json);
        else if (admission && r.result == sentinel::ProbeResult::ok) result = print_admission(r, json);
        else if (native && r.result == sentinel::ProbeResult::ok) {
            print_native(r, json);
            if (r.native.event_count) after_event = r.native.events[r.native.event_count - 1].sequence;
        } else result = print_result(r, pid, json, engine, context, save);
        std::fflush(stdout);
        if (result || i + 1 == count || GetTickCount64() >= deadline) break;
        const auto next = std::min(deadline, started + interval);
        WaitForSingleObject(stop.value, sentinel::remaining(next));
    }
    SetConsoleCtrlHandler(interrupt, FALSE);
    return result;
}
