#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

int arsenal_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1], L"--arsenal")) return -1;
    if (argc != 2) {
        std::puts("--arsenal reads one fixed SCIP arsenal request on binary stdin and returns JSON.");
        return 2;
    }
    _setmode(_fileno(stdin), _O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(), 1, message.size(), stdin);
    uint16_t op = 0; sc_arsenal_request request{};
    if (sentinel::decode_request(message, count, &op, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &request) != sentinel::WireResult::ok ||
        op < sentinel::arsenal_submit_operation || op > sentinel::arsenal_release_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto r = sentinel::query_arsenal(request.execution.expected.pid, 2000, op, request);
    if (r.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n", sentinel::result_name(r.result), r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& ars = r.arsenal; const auto& e = ars.execution;
    std::printf("{\"result\":\"ok\",\"core_version\":\"%s\",\"build_id\":\"%s\","
        "\"namespace\":\"%s\",\"state\":%u,\"reason\":%u,\"request_id\":\"%llu\","
        "\"kind\":%u,\"outcome\":%u,\"flags\":%u,\"native_exception\":%u,"
        "\"weapons_before\":%u,\"weapons_after\":%u,"
        "\"mods_before\":%u,\"mods_after\":%u,"
        "\"selected_mods_before\":[%u,%u,%u,%u,%u,%u,%u,%u],"
        "\"selected_mods_after\":[%u,%u,%u,%u,%u,%u,%u,%u],"
        "\"normal_upgrades_before\":%u,\"normal_upgrades_after\":%u,"
        "\"masteries_ap_before\":%u,\"masteries_ap_after\":%u,"
        "\"mastery_challenges_active_before\":%u,\"mastery_challenges_active_after\":%u,"
        "\"mastery_challenges_completed_before\":%u,\"mastery_challenges_completed_after\":%u,"
        "\"masteries_effective_before\":%u,\"masteries_effective_after\":%u,"
        "\"masteries_effective_observed\":%s,"
        "\"mission_challenges_active_before\":%u,\"mission_challenges_active_after\":%u,"
        "\"mission_challenges_completed_before\":%u,\"mission_challenges_completed_after\":%u,"
        "\"operations_applied\":\"%llu\"}\n",
        r.snapshot.core.version, r.snapshot.core.build_id, ars.namespace_id, e.state, e.reason, e.request_id,
        ars.kind, ars.outcome, ars.flags, ars.native_exception,
        ars.weapons_before, ars.weapons_after,
        ars.mods_before, ars.mods_after,
        ars.selected_mods_before[0], ars.selected_mods_before[1], ars.selected_mods_before[2], ars.selected_mods_before[3],
        ars.selected_mods_before[4], ars.selected_mods_before[5], ars.selected_mods_before[6], ars.selected_mods_before[7],
        ars.selected_mods_after[0], ars.selected_mods_after[1], ars.selected_mods_after[2], ars.selected_mods_after[3],
        ars.selected_mods_after[4], ars.selected_mods_after[5], ars.selected_mods_after[6], ars.selected_mods_after[7],
        ars.normal_upgrades_before, ars.normal_upgrades_after,
        ars.masteries_ap_before, ars.masteries_ap_after,
        ars.mastery_challenges_active_before, ars.mastery_challenges_active_after,
        ars.mastery_challenges_completed_before, ars.mastery_challenges_completed_after,
        ars.masteries_effective_before, ars.masteries_effective_after,
        e.state == SC_DIAGNOSTIC_EXECUTED && ars.kind == SC_ARSENAL_OBSERVE &&
        (ars.outcome == SC_ARSENAL_OUTCOME_OK || ars.outcome == SC_ARSENAL_OUTCOME_NOOP) &&
        (ars.flags & (SC_ARSENAL_FLAG_BEFORE_VALID | SC_ARSENAL_FLAG_AFTER_VALID)) ==
            (SC_ARSENAL_FLAG_BEFORE_VALID | SC_ARSENAL_FLAG_AFTER_VALID) &&
        !(ars.flags & SC_ARSENAL_FLAG_EFFECTIVE_UNOBSERVED) ? "true" : "false",
        ars.mission_challenges_active_before, ars.mission_challenges_active_after,
        ars.mission_challenges_completed_before, ars.mission_challenges_completed_after,
        ars.operations_applied);
    return 0;
}
