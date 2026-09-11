#include "startup_log.h"
#include "save_session.h"
#include "prelaunch.h"
#include <windows.h>
#include <string>
#include <cstdio>
namespace sentinel::startup_log {
namespace {
SRWLOCK guard = SRWLOCK_INIT;
HANDLE file = INVALID_HANDLE_VALUE;
bool opened = false;
unsigned count = 0;
std::string last;
std::string quoted(const char* text) {
    std::string out="\"";
    for (const auto* p=reinterpret_cast<const unsigned char*>(text);*p;++p) {
        if (*p=='"' || *p=='\\' || *p<32 || *p>=127) {
            char escaped[7]{}; std::snprintf(escaped,sizeof(escaped),"\\u%04x",static_cast<unsigned>(*p)); out+=escaped;
        } else out+=static_cast<char>(*p);
    }
    return out+'"';
}
std::string profile_step(const save::ProfileStep& s) {
    return "{\"first_ms\":" + std::to_string(s.first_ms) + ",\"changed_ms\":" + std::to_string(s.changed_ms) +
        ",\"elapsed_ms\":" + std::to_string(s.changed_ms - s.first_ms) + ",\"status\":" + std::to_string(static_cast<uint32_t>(s.status)) +
        ",\"predicate\":\"" + s.predicate + "\",\"native_attempted\":" + (s.native_attempted ? "true" : "false") +
        ",\"native_state\":" + std::to_string(s.native_state) + ",\"native_outcome\":" + std::to_string(s.native_outcome) +
        ",\"native_value\":" + std::to_string(s.native_value) +
        ",\"read_reason\":" + std::to_string(s.read.reason) + ",\"read_error\":" + std::to_string(s.read.error) +
        ",\"read_requested\":" + std::to_string(s.read.requested) + ",\"read_offset\":" + std::to_string(s.read.offset) +
        ",\"read_size\":" + std::to_string(s.read.size) + "}";
}
std::string profile(const save::ProfileTrace& trace) {
    constexpr const char* names[]{"request", "profile_created", "catalog_created", "first_poll", "prepare", "decode", "transport",
        "catalog_poll", "catalog", "reader", "framing", "checksum", "parse", "overlay", "application", "root", "admission", "write_after_refusal", "output_validation"};
    static_assert(std::size(names) == static_cast<size_t>(save::ProfileStage::count));
    auto out = "{\"request_id\":" + std::to_string(trace.request) + ",\"identity_kind\":\"native_steam_identity\",\"identity_matched\":" +
        (trace.identity_matched ? "true" : "false") + ",\"deadline_basis\":\"native_lifetime\",\"account_network_state\":\"not_observed\",\"downstream_refusals\":" +
        std::to_string(trace.downstream_refusals) + ",\"first_failed_stage\":\"" +
        (trace.failed_stage == save::ProfileStage::count ? "none" : names[static_cast<size_t>(trace.failed_stage)]) +
        "\",\"first_failure\":" + profile_step(trace.failure) + ",\"steps\":{";
    for (size_t i = 0; i < std::size(names); ++i) {
        if (i) out += ',';
        out += "\"" + std::string(names[i]) + "\":" + profile_step(trace.steps[i]);
    }
    return out + "}}";
}
std::string event(const sc_install_event& e) {
    if (!e.sequence) return "null";
    char expected[65]{}, actual[65]{};
    for (unsigned i=0; i<e.byte_count && i<32; ++i) {
        std::snprintf(expected+i*2,3,"%02x",e.expected_bytes[i]);
        std::snprintf(actual+i*2,3,"%02x",e.actual_bytes[i]);
    }
    return "{\"sequence\":" + std::to_string(e.sequence) + ",\"stage\":" + std::to_string(e.stage) +
        ",\"reason\":" + std::to_string(e.reason) + ",\"at_ms\":" + std::to_string(e.at_ms) +
        ",\"duration_ms\":" + std::to_string(e.duration_ms) + ",\"target_group\":" + std::to_string(e.target_group) +
        ",\"target_index\":" + std::to_string(e.target_index) + ",\"rva\":" + std::to_string(e.rva) +
        ",\"result\":" + std::to_string(e.result) + ",\"win32_error\":" + std::to_string(e.win32_error) +
        ",\"minhook_status\":" + std::to_string(e.minhook_status) + ",\"read_reason\":" + std::to_string(e.read_reason) +
        ",\"expected_bytes\":\"" + expected + "\",\"actual_bytes\":\"" + actual + "\"}";
}
void open(const Snapshot& core) {
    opened = true;
    wchar_t root[32761]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA",root,32761);
    if (!size || size >= 32761) return;
    std::wstring directory(root);
    directory += L"\\SentinelCore";
    if (!CreateDirectoryW(directory.c_str(),nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    directory += L"\\diagnostics";
    if (!CreateDirectoryW(directory.c_str(),nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    const auto path = directory + L"\\" + std::to_wstring(core.pid) + L"-" + std::to_wstring(core.process_created) + L".jsonl";
    file = CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
}
}
void record(const Snapshot& core, uint32_t engine_reason) noexcept {
    AcquireSRWLockExclusive(&guard);
    try {
        if (!opened) open(core);
        if (file != INVALID_HANDLE_VALUE && count < 128) {
            const auto install = save::session().installation.inspect();
            const auto session = save::session().inspect();
            const auto campaign = save::session().campaign_run.snapshot();
            const auto flag=[](bool v){return v?"true":"false";};
            const auto campaign_json = std::string("{\"enabled\":")+flag(campaign.enabled)+
                ",\"resumed\":"+flag(campaign.resumed)+",\"phase\":\""+campaign.phase+"\",\"reason\":\""+campaign.reason+
                "\",\"slot\":\""+campaign.slot+"\",\"map\":\""+campaign.map+"\",\"difficulty\":"+std::to_string(campaign.difficulty)+
                ",\"effective_difficulty\":"+std::to_string(campaign.effective_difficulty)+",\"changes_blocked\":"+std::to_string(campaign.changes_blocked)+
                ",\"source_verified\":"+flag(campaign.source_verified)+",\"parser_completed\":"+flag(campaign.parser_completed)+
                ",\"parser_result\":"+std::to_string(campaign.parser_result)+",\"loaded_difficulty\":"+std::to_string(campaign.loaded_difficulty)+",\"native_saved\":"+flag(campaign.native_saved)+
                ",\"readback_verified\":"+flag(campaign.readback_verified)+",\"continuity_persisted\":"+flag(campaign.continuity_persisted)+
                ",\"native_factory_matched\":"+flag(campaign.native_factory_matched)+
                ",\"operation\":"+std::to_string(campaign.operation)+",\"checkpoint\":"+std::to_string(campaign.checkpoint)+
                ",\"source_checkpoint\":"+std::to_string(campaign.source_checkpoint)+
                ",\"generation_before\":"+std::to_string(campaign.generation_before)+",\"generation_after\":"+std::to_string(campaign.generation_after)+
                ",\"map_active\":"+flag(campaign.map_active)+",\"save_ready\":"+flag(campaign.save_ready)+
                ",\"failure_at_ms\":"+std::to_string(campaign.failure_at_ms)+
                ",\"transition_event\":"+std::to_string(campaign.transition.event_id)+
                ",\"transition_at_ms\":"+std::to_string(campaign.transition.at_ms)+
                ",\"native_return\":"+std::to_string(campaign.transition.native_return)+
                ",\"transition_depth\":"+std::to_string(campaign.transition.depth)+
                ",\"transition_observation_reason\":"+std::to_string(campaign.transition.observation_reason)+
                ",\"transition_observed\":"+flag(campaign.transition.observed)+
                ",\"transition_ended\":"+flag(campaign.transition.ended)+
                ",\"transition_abnormal\":"+flag(campaign.transition.abnormal)+
                ",\"transition_state\":"+std::to_string(campaign.transition.game)+
                ",\"transition_state_read\":"+flag(campaign.transition.state_read)+
                ",\"transition_map_read\":"+flag(campaign.transition.map_read)+
                ",\"transition_difficulty_read\":"+flag(campaign.transition.difficulty_read)+
                ",\"transition_map\":"+quoted(campaign.transition.map.data())+
                ",\"checkpoint_at_ms\":"+std::to_string(campaign.checkpoint_boundary.at_ms)+
                ",\"checkpoint_generation\":"+std::to_string(campaign.checkpoint_boundary.generation_after)+
                ",\"checkpoint_depth\":"+std::to_string(campaign.checkpoint_boundary.depth)+
                ",\"checkpoint_state\":"+std::to_string(campaign.checkpoint_boundary.game)+
                ",\"checkpoint_difficulty\":"+std::to_string(campaign.checkpoint_boundary.difficulty)+
                ",\"checkpoint_state_read\":"+flag(campaign.checkpoint_boundary.state_read)+
                ",\"checkpoint_map_read\":"+flag(campaign.checkpoint_boundary.map_read)+
                ",\"checkpoint_difficulty_read\":"+flag(campaign.checkpoint_boundary.difficulty_read)+"}";
            const auto facts = "\"engine_reason\":" + std::to_string(engine_reason) + ",\"installation\":{\"phase\":" + std::to_string(install.phase) +
                ",\"sequence\":" + std::to_string(install.sequence) + ",\"startup_observation\":" + std::to_string(install.startup_observation) +
                ",\"last_completed_stage\":" + std::to_string(install.last_completed_stage) + ",\"validated\":" + std::to_string(install.validated) +
                ",\"created\":" + std::to_string(install.created) + ",\"enabled\":" + std::to_string(install.enabled) +
                ",\"primary_failure\":" + event(install.primary_failure) + ",\"cleanup_failure\":" + event(install.cleanup_failure) +
                ",\"active\":" + event(install.active) + "},\"admission\":{\"state\":" + std::to_string(session.state) +
                ",\"fault\":" + std::to_string(session.fault) + ",\"flags\":" + std::to_string(session.flags) +
                ",\"prepared_routes\":" + std::to_string(session.prepared_routes) + ",\"required_routes\":" + std::to_string(session.required_routes) +
                ",\"namespace_id\":\"" + session.namespace_id + "\"},\"profile\":" + profile(save::session().profile_trace())+",\"campaign\":"+campaign_json;
            if (facts != last) {
                const auto& wide_key = prelaunch::diagnostic_key();
                std::string control;
                if (wide_key.size() >= 64) for (auto digit : wide_key.substr(wide_key.size()-64)) control.push_back(static_cast<char>(digit));
                const auto line = "{\"schema\":\"sentinel-startup-v1\",\"control_sha256\":\"" + control + "\",\"pid\":" + std::to_string(core.pid) +
                    ",\"process_created\":\"" + std::to_string(core.process_created) + "\",\"build_id\":\"" + core.core.build_id +
                    "\",\"at_ms\":" + std::to_string(GetTickCount64()) + "," + facts + "}\n";
                DWORD written = 0;
                if (!WriteFile(file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr) || written != line.size()) {
                    CloseHandle(file); file = INVALID_HANDLE_VALUE;
                } else { FlushFileBuffers(file); last = facts; ++count; }
            }
        }
    } catch (...) { /* Diagnostics never change admission or inspection availability. */ }
    ReleaseSRWLockExclusive(&guard);
}
}
