#include "sentinel_inspection.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cerrno>

namespace {
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
int wmain(int argc, wchar_t** argv) {
    uint32_t pid = 0, timeout = 2000;
    bool json = false, valid = true, saw_pid = false, saw_timeout = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--help") == 0 && argc == 2) {
            std::puts("sentinel_probe --pid PID [--timeout-ms 50..10000] [--json]\nRead-only query of an already loaded Core; default timeout 2000 ms.");
            return 0;
        }
        if (wcscmp(argv[i], L"--json") == 0 && !json) json = true;
        else if (wcscmp(argv[i], L"--pid") == 0 && !saw_pid && i + 1 < argc) {
            saw_pid = true; valid = number(argv[++i], pid) && valid;
        } else if (wcscmp(argv[i], L"--timeout-ms") == 0 && !saw_timeout && i + 1 < argc) {
            saw_timeout = true; valid = number(argv[++i], timeout) && valid;
        } else valid = false;
    }
    sentinel::Inspection r;
    if (!valid || !pid || timeout < sentinel::min_timeout_ms || timeout > sentinel::max_timeout_ms)
        r.result = sentinel::ProbeResult::usage;
    else r = sentinel::query(pid, timeout);
    const char* result = sentinel::result_name(r.result);
    if (r.result != sentinel::ProbeResult::ok) {
        if (json) std::printf("{\"result\":\"%s\",\"target_pid\":%u,\"win32_error\":%u}\n", result, pid, r.win32_error);
        else std::printf("Sentinel inspection: %s (PID %u, win32=%u). Use --help for usage.\n", result, pid, r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& s = r.snapshot;
    const auto id = sentinel::instance_text(s.instance);
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
