#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

int special_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1], L"--special")) return -1;
    if (argc != 2) {
        std::puts("--special reads one fixed SCIP special request on binary stdin and returns JSON.");
        return 2;
    }
    _setmode(_fileno(stdin), _O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(), 1, message.size(), stdin);
    uint16_t op = 0; sc_special_request request{};
    if (sentinel::decode_request(message, count, &op, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &request) != sentinel::WireResult::ok ||
        op < sentinel::special_submit_operation || op > sentinel::special_release_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto r = sentinel::query_special(request.execution.expected.pid, 2000, op, request);
    if (r.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n", sentinel::result_name(r.result), r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& sp = r.special; const auto& e = sp.execution;
    std::printf("{\"result\":\"ok\",\"core_version\":\"%s\",\"build_id\":\"%s\","
        "\"namespace\":\"%s\",\"state\":%u,\"reason\":%u,\"request_id\":\"%llu\","
        "\"kind\":%u,\"outcome\":%u,\"flags\":%u,\"native_exception\":%u,"
        "\"owns_crucible\":%u,\"owns_hammer\":%u,\"hammer_tier\":%u,\"selected\":%u,"
        "\"native_crucible\":%u,\"native_hammer\":%u,\"native_hammer_perks\":%u,"
        "\"native_selected\":%u,\"native_state_known\":%u,\"selection_source\":\"%s\","
        "\"crucible_charge\":%u,\"crucible_charge_max\":%u,"
        "\"refill_balance\":%u,\"refill_flags\":%u,\"refill_request_id\":\"%llu\","
        "\"refill_request_state\":%u,\"refill_executed\":%u,\"refill_execution_id\":\"%llu\","
        "\"refill_sequence\":%u,\"operations_applied\":\"%llu\"}\n",
        r.snapshot.core.version, r.snapshot.core.build_id, sp.namespace_id, e.state, e.reason, e.request_id,
        sp.kind, sp.outcome, sp.flags, sp.native_exception,
        sp.owns_crucible, sp.owns_hammer, sp.hammer_tier, sp.selected,
        sp.native_crucible, sp.native_hammer, sp.native_hammer_perks,
        sp.native_selected, sp.native_state_known,
        (sp.flags & SC_SPECIAL_FLAG_SELECTION_POLICY) ? "core_policy" : "native_observation",
        sp.crucible_charge, sp.crucible_charge_max,
        sp.refill_balance, sp.refill_flags, sp.refill_request_id,
        sp.refill_request_state, sp.refill_executed, sp.refill_execution_id,
        sp.refill_sequence, sp.operations_applied);
    return 0;
}
