#include "sentinel_inspection.h"
#include "pipe_io.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cerrno>
#include <algorithm>

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
int print_result(const sentinel::Inspection& r, uint32_t pid, bool json, bool engine) {
    const char* result = sentinel::result_name(r.result);
    if (r.result != sentinel::ProbeResult::ok) {
        if (json) std::printf("{\"result\":\"%s\",\"target_pid\":%u,\"win32_error\":%u}\n", result, pid, r.win32_error);
        else std::printf("Sentinel inspection: %s (PID %u, win32=%u). Use --help for usage.\n", result, pid, r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& s = r.snapshot;
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
    uint32_t pid = 0, timeout = 2000, count = 1, interval = 1000;
    bool json = false, engine = false, valid = true;
    bool saw_pid = false, saw_timeout = false, saw_count = false, saw_interval = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--help") == 0 && argc == 2) {
            std::puts("sentinel_probe --pid PID [--timeout-ms 50..10000] [--json] [--engine]\n"
                "Engine capture: --engine --watch-count 1..600 [--interval-ms 100..10000]\n"
                "Defaults: timeout 2000 ms, interval 1000 ms. Capture scheduling window <= 10 minutes.\n"
                "One fresh query per record; stop on first error, target exit/restart, or Ctrl+C.\n"
                "Read-only query of an already loaded Core; --json capture emits JSON Lines.");
            return 0;
        }
        if (wcscmp(argv[i], L"--json") == 0 && !json) json = true;
        else if (wcscmp(argv[i], L"--engine") == 0 && !engine) engine = true;
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
        (saw_count && !engine) || (saw_interval && !saw_count)) {
        sentinel::Inspection error; error.result = sentinel::ProbeResult::usage;
        return print_result(error, pid, json, engine);
    }
    sentinel::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stop) { sentinel::Inspection error; error.win32_error = GetLastError(); return print_result(error, pid, json, engine); }
    interrupt_event = stop.value;
    SetConsoleCtrlHandler(interrupt, TRUE);
    sentinel::Snapshot first{};
    const auto deadline = GetTickCount64() + uint64_t(count) * interval;
    int result = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (WaitForSingleObject(stop.value, 0) != WAIT_TIMEOUT) break;
        const auto started = GetTickCount64();
        auto r = engine ? sentinel::query_engine(pid, timeout) : sentinel::query(pid, timeout);
        if (r.result == sentinel::ProbeResult::ok && i &&
            (r.snapshot.process_created != first.process_created || r.snapshot.instance != first.instance))
            r.result = sentinel::ProbeResult::process_mismatch;
        if (!i) first = r.snapshot;
        result = print_result(r, pid, json, engine); std::fflush(stdout);
        if (result || i + 1 == count || GetTickCount64() >= deadline) break;
        const auto next = std::min(deadline, started + interval);
        WaitForSingleObject(stop.value, sentinel::remaining(next));
    }
    SetConsoleCtrlHandler(interrupt, FALSE);
    return result;
}
