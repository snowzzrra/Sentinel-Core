#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

int deathlink_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1], L"--deathlink")) return -1;
    if (argc != 2) {
        std::puts("--deathlink reads one fixed SCIP deathlink request on binary stdin and returns JSON.");
        return 2;
    }
    _setmode(_fileno(stdin), _O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(), 1, message.size(), stdin);
    uint16_t op = 0; sc_deathlink_request request{};
    if (sentinel::decode_request(message, count, &op, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &request) != sentinel::WireResult::ok ||
        op < sentinel::deathlink_submit_operation || op > sentinel::deathlink_release_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto r = sentinel::query_deathlink(request.execution.expected.pid, 2000, op, request);
    if (r.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n", sentinel::result_name(r.result), r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& dl = r.deathlink; const auto& e = dl.execution;
    std::printf("{\"result\":\"ok\",\"core_version\":\"%s\",\"build_id\":\"%s\","
        "\"namespace\":\"%s\",\"state\":%u,\"reason\":%u,\"request_id\":\"%llu\","
        "\"kind\":%u,\"outcome\":%u,\"flags\":%u,\"native_exception\":%u,"
        "\"enabled\":%u,\"mode\":%u,\"remote_state\":%u,\"remote_protection\":%u,"
        "\"remote_attempts\":%u,\"pending_count\":%u,\"remote_event_id\":\"%llu\","
        "\"local_candidates\":%u,\"local_suppressed\":%u,\"local_state\":%u,"
        "\"local_death_sequence\":\"%llu\",\"local_death_time_ms\":\"%llu\","
        "\"local_cause\":%u,\"local_protection\":%u,\"local_flags\":%u,"
        "\"operations_applied\":\"%llu\"}\n",
        r.snapshot.core.version, r.snapshot.core.build_id, dl.namespace_id, e.state, e.reason, e.request_id,
        dl.kind, dl.outcome, dl.flags, dl.native_exception,
        dl.enabled, dl.mode, dl.remote_state, dl.remote_protection,
        dl.remote_attempts, dl.pending_count, dl.remote_event_id,
        dl.local_candidates, dl.local_suppressed, dl.local_state,
        dl.local_death_sequence, dl.local_death_time_ms,
        dl.local_cause, dl.local_protection, dl.local_flags, dl.operations_applied);
    return 0;
}
