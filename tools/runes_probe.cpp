#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

int runes_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1], L"--runes")) return -1;
    if (argc != 2) {
        std::puts("--runes reads one fixed SCIP runes request on binary stdin and returns JSON.");
        return 2;
    }
    _setmode(_fileno(stdin), _O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(), 1, message.size(), stdin);
    uint16_t op = 0; sc_runes_request request{};
    if (sentinel::decode_request(message, count, &op, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &request) != sentinel::WireResult::ok ||
        op < sentinel::runes_submit_operation || op > sentinel::runes_release_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto r = sentinel::query_runes(request.execution.expected.pid, 2000, op, request);
    if (r.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n", sentinel::result_name(r.result), r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& rn = r.runes; const auto& e = rn.execution;
    std::printf("{\"result\":\"ok\",\"core_version\":\"%s\",\"build_id\":\"%s\","
        "\"namespace\":\"%s\",\"state\":%u,\"reason\":%u,\"request_id\":\"%llu\","
        "\"kind\":%u,\"outcome\":%u,\"flags\":%u,\"native_exception\":%u,"
        "\"owned_normal_before\":%u,\"owned_normal_after\":%u,"
        "\"owned_support_before\":%u,\"owned_support_after\":%u,"
        "\"selected_slots_before\":[%d,%d,%d],"
        "\"selected_slots_after\":[%d,%d,%d],"
        "\"selected_support_before\":%d,\"selected_support_after\":%d,"
        "\"unlocked_slots_before\":%u,\"unlocked_slots_after\":%u,"
        "\"derived_pairs_before\":%u,\"derived_pairs_after\":%u,"
        "\"operations_applied\":\"%llu\"}\n",
        r.snapshot.core.version, r.snapshot.core.build_id, rn.namespace_id, e.state, e.reason, e.request_id,
        rn.kind, rn.outcome, rn.flags, rn.native_exception,
        rn.owned_normal_before, rn.owned_normal_after,
        rn.owned_support_before, rn.owned_support_after,
        rn.selected_slots_before[0], rn.selected_slots_before[1], rn.selected_slots_before[2],
        rn.selected_slots_after[0], rn.selected_slots_after[1], rn.selected_slots_after[2],
        rn.selected_support_before, rn.selected_support_after,
        rn.unlocked_slots_before, rn.unlocked_slots_after,
        rn.derived_pairs_before, rn.derived_pairs_after,
        rn.operations_applied);
    return 0;
}
