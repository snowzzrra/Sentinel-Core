#include "sentinel_inspection.h"
#include "pipe_io.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cwchar>
#include <cerrno>
#include <cstring>

namespace {
HANDLE backup_stop = nullptr;
int usage(int argc, wchar_t** argv) {
    bool json = false;
    for (int i = 1; i < argc; ++i) json |= std::wcscmp(argv[i], L"--json") == 0;
    if (json) std::puts("{\"operation\":\"native_backup\",\"result\":\"usage\"}");
    else std::puts("Invalid native backup arguments. Use --native-backup --help.");
    return 2;
}
BOOL WINAPI stop_backup(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    SetEvent(backup_stop); return TRUE;
}
bool number(const wchar_t* text, uint64_t& out) {
    if (!*text) return false;
    for (auto p = text; *p; ++p) if (*p < L'0' || *p > L'9') return false;
    errno = 0; wchar_t* end = nullptr; out = std::wcstoull(text, &end, 10);
    return !errno && !*end;
}
template<size_t N> bool hex(const wchar_t* text, uint8_t (&out)[N]) {
    if (std::wcslen(text) != N * 2) return false;
    for (size_t i = 0; i < N * 2; ++i) {
        const auto c = text[i];
        const int value = c >= L'0' && c <= L'9' ? c - L'0' : (c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1);
        if (value < 0) return false;
        if (i % 2) out[i / 2] |= static_cast<uint8_t>(value);
        else out[i / 2] = static_cast<uint8_t>(value << 4);
    }
    return true;
}
std::string hex_text(const uint8_t (&bytes)[16]) {
    std::array<uint8_t, 16> value{}; std::memcpy(value.data(), bytes, 16);
    return sentinel::instance_text(value);
}
void identity(const sc_save_backup_request& r, bool json) {
    const auto& e = r.execution;
    if (json) std::printf("{\"operation\":\"native_backup_request\",\"state\":\"prepared\",\"pid\":%u,"
        "\"process_created\":\"%llu\",\"instance_id\":\"%s\",\"generation\":\"%llu\","
        "\"request_id\":\"%llu\",\"nonce\":\"%s\",\"namespace\":\"%s\",\"campaign\":%u,\"slot\":%u,"
        "\"deadline_ms\":%u,\"work_deadline_ms\":%u}\n", e.expected.pid, e.expected.process_created,
        hex_text(e.expected.instance_id).c_str(), e.expected.lifecycle_generation, e.request_id,
        hex_text(e.nonce).c_str(), r.namespace_id, r.campaign, r.slot, e.deadline_ms, r.work_deadline_ms);
    else std::printf("prepared backup: pid=%u request=%llu nonce=%s created=%llu instance=%s generation=%llu\n"
        "namespace=%s campaign=%u slot=%u admission_ms=%u work_ms=%u\n", e.expected.pid, e.request_id,
        hex_text(e.nonce).c_str(), e.expected.process_created, hex_text(e.expected.instance_id).c_str(),
        e.expected.lifecycle_generation, r.namespace_id, r.campaign, r.slot, e.deadline_ms, r.work_deadline_ms);
    std::fflush(stdout);
}
int print(const sentinel::Inspection& r, bool json) {
    if (r.result != sentinel::ProbeResult::ok) {
        if (json) std::printf("{\"operation\":\"native_backup\",\"result\":\"%s\",\"win32_error\":%u,"
            "\"failure_stage\":\"%s\",\"target_state\":\"%s\"}\n", sentinel::result_name(r.result),
            r.win32_error, r.failure_stage, r.target_state);
        else std::printf("backup query: %s, win32=%u, stage=%s; timeout does not prove nonexecution.\n",
            sentinel::result_name(r.result), r.win32_error, r.failure_stage);
        return static_cast<int>(r.result);
    }
    const auto& b = r.backup;
    if (json) std::printf("{\"operation\":\"native_backup\",\"result\":\"ok\",\"state\":\"%s\","
        "\"request_id\":\"%llu\",\"write_operation\":\"%llu\",\"failure\":%u,\"native_reason\":\"%s\","
        "\"flags\":%u,\"native_exception\":%u,\"storage_outcome\":%u,\"storage_error\":%u,"
        "\"basename\":\"%s\",\"files\":%u,\"bytes\":\"%llu\",\"playable_reopen_verified\":false}\n",
        sentinel::backup_state_name(b.state), b.execution.request_id, b.operation_id, b.failure,
        sentinel::native_reason_name(b.execution.reason), b.flags, b.native_exception, b.storage_outcome,
        b.storage_error, b.basename, b.files, b.bytes);
    else std::printf("backup %s: request=%llu write=%llu failure=%u reason=%s flags=%u\n"
        "output=%s files=%u bytes=%llu storage=%u error=%u; playable reopen remains unverified.\n",
        sentinel::backup_state_name(b.state), b.execution.request_id, b.operation_id, b.failure,
        sentinel::native_reason_name(b.execution.reason), b.flags, b.basename, b.files, b.bytes,
        b.storage_outcome, b.storage_error);
    std::fflush(stdout);
    return b.state == SC_BACKUP_COMPLETE ? 0 : 8;
}
}
int native_backup_command(int argc, wchar_t** argv) {
    uint16_t operation = 0;
    for (int i = 1; i < argc; ++i) {
        uint16_t next = 0;
        if (!std::wcscmp(argv[i], L"--native-backup")) next = sentinel::save_backup_submit_operation;
        if (!std::wcscmp(argv[i], L"--native-backup-result")) next = sentinel::save_backup_result_operation;
        if (!std::wcscmp(argv[i], L"--native-backup-cancel")) next = sentinel::save_backup_cancel_operation;
        if (next) { if (operation) return usage(argc, argv); operation = next; }
    }
    if (!operation) return -1;
    if (argc == 3 && (!std::wcscmp(argv[1], L"--help") || !std::wcscmp(argv[2], L"--help"))) {
        std::puts("sentinel_probe --native-backup --pid PID --namespace HEX64 --campaign game|dlc1|dlc2 --slot 0..11 [--json]\n"
            "Optional: --deadline-ms 1..5000 (default 1000), --work-deadline-ms 1..60000 (default 10000), --timeout-ms 50..10000.\n"
            "Result/cancel: --native-backup-result or --native-backup-cancel with the same namespace/campaign/slot/deadlines,\n"
            "  plus --request-id N --nonce HEX32 --expect-created N --expect-instance HEX32 --expect-generation N.\n"
            "Submit records identity before sending. Timeout/UNKNOWN is not nonexecution. Ctrl+C requests cancellation.\n"
            "The native save may already have run; retained partial/complete archives are not deleted. No slot selection or reset.");
        return 0;
    }
    sc_save_backup_request request{}; request.execution.deadline_ms = 1000; request.work_deadline_ms = 10000;
    uint32_t timeout = 2000, seen = 0; bool json = false, valid = true;
    for (int i = 1; i < argc && valid; ++i) {
        const auto arg = argv[i];
        if (!std::wcscmp(arg, L"--native-backup") || !std::wcscmp(arg, L"--native-backup-result") ||
            !std::wcscmp(arg, L"--native-backup-cancel")) continue;
        if (!std::wcscmp(arg, L"--json")) { valid = !json; json = true; continue; }
        constexpr const wchar_t* names[] = {L"--pid", L"--namespace", L"--campaign", L"--slot", L"--deadline-ms",
            L"--work-deadline-ms", L"--timeout-ms", L"--request-id", L"--nonce", L"--expect-created", L"--expect-instance", L"--expect-generation"};
        unsigned field = 0; while (field < std::size(names) && std::wcscmp(arg, names[field])) ++field;
        if (field == std::size(names) || (seen & (1u << field)) || i + 1 == argc) { valid = false; break; }
        seen |= 1u << field; const auto value = argv[++i]; uint64_t n = 0;
        if (field == 1) {
            uint8_t bytes[32]{}; valid = hex(value, bytes);
            if (valid) for (size_t k = 0; k < 64; ++k) request.namespace_id[k] = static_cast<char>(value[k]);
        } else if (field == 2) {
            valid = !std::wcscmp(value, L"game") || !std::wcscmp(value, L"dlc1") || !std::wcscmp(value, L"dlc2");
            request.campaign = !std::wcscmp(value, L"game") ? 0u : (!std::wcscmp(value, L"dlc1") ? 1u : 2u);
        } else if (field == 8) valid = hex(value, request.execution.nonce);
        else if (field == 10) valid = hex(value, request.execution.expected.instance_id);
        else {
            valid = number(value, n) && (field >= 7 || n <= UINT32_MAX);
            switch (field) {
            case 0: request.execution.expected.pid = static_cast<uint32_t>(n); break;
            case 3: request.slot = static_cast<uint32_t>(n); break;
            case 4: request.execution.deadline_ms = static_cast<uint32_t>(n); break;
            case 5: request.work_deadline_ms = static_cast<uint32_t>(n); break;
            case 6: timeout = static_cast<uint32_t>(n); break;
            case 7: request.execution.request_id = n; break;
            case 9: request.execution.expected.process_created = n; break;
            case 11: request.execution.expected.lifecycle_generation = n; break;
            }
        }
    }
    constexpr uint32_t identity_fields = (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11);
    const bool submit = operation == sentinel::save_backup_submit_operation;
    if (!valid || (seen & 15u) != 15u || !request.execution.expected.pid || request.slot > 11 ||
        !request.execution.deadline_ms || request.execution.deadline_ms > SC_DIAGNOSTIC_MAX_DEADLINE_MS ||
        !request.work_deadline_ms || request.work_deadline_ms > SC_SAVE_BACKUP_MAX_WORK_MS ||
        timeout < sentinel::min_timeout_ms || timeout > sentinel::max_timeout_ms ||
        (submit ? (seen & identity_fields) != 0 : (seen & identity_fields) != identity_fields)) return usage(argc, argv);
    const auto pid = request.execution.expected.pid;
    if (!submit) {
        bool nonce = false, instance = false;
        for (auto b : request.execution.nonce) nonce |= b != 0;
        for (auto b : request.execution.expected.instance_id) instance |= b != 0;
        if (!nonce || !instance || !request.execution.request_id || !request.execution.expected.process_created ||
            !request.execution.expected.lifecycle_generation) return usage(argc, argv);
    }
    if (submit) {
        const auto initial = sentinel::query_native(pid, timeout);
        if (initial.result != sentinel::ProbeResult::ok) return print(initial, json);
        if (!initial.native.context_generation || initial.native.availability != SC_NATIVE_ENABLED) {
            if (json) std::puts("{\"operation\":\"native_backup\",\"result\":\"capability_unavailable\",\"native_entered\":false}");
            else std::puts("Native context unavailable; no backup was submitted.");
            return 8;
        }
        request.execution.expected = initial.native.scope; request.execution.request_id = GetTickCount64();
        if (BCryptGenRandom(nullptr, request.execution.nonce, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 10;
        identity(request, json);
    }
    sentinel::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)); if (!stop) return 10;
    backup_stop = stop.value; SetConsoleCtrlHandler(stop_backup, TRUE);
    const auto until = GetTickCount64() + request.work_deadline_ms + timeout;
    auto response = sentinel::query_save_backup(pid, timeout, operation, request);
    int code = 8;
    for (;;) {
        code = print(response, json);
        if (!submit || response.result != sentinel::ProbeResult::ok || response.backup.state == SC_BACKUP_UNKNOWN ||
            response.backup.state >= SC_BACKUP_COMPLETE || GetTickCount64() >= until) break;
        if (WaitForSingleObject(stop.value, 100) != WAIT_TIMEOUT) {
            code = print(sentinel::query_save_backup(pid, timeout, sentinel::save_backup_cancel_operation, request), json);
            break;
        }
        response = sentinel::query_save_backup(pid, timeout, sentinel::save_backup_result_operation, request);
    }
    SetConsoleCtrlHandler(stop_backup, FALSE); backup_stop = nullptr;
    return code;
}
