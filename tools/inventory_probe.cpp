#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

int inventory_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1], L"--inventory")) return -1;
    if (argc != 2) {
        std::puts("--inventory reads one fixed SCIP inventory request on binary stdin and returns JSON.");
        return 2;
    }
    _setmode(_fileno(stdin), _O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(), 1, message.size(), stdin);
    uint16_t op = 0; sc_inventory_request request{};
    if (sentinel::decode_request(message, count, &op, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &request) != sentinel::WireResult::ok ||
        op < sentinel::inventory_submit_operation || op > sentinel::inventory_release_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto r = sentinel::query_inventory(request.execution.expected.pid, 2000, op, request);
    if (r.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n", sentinel::result_name(r.result), r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& inv = r.inventory; const auto& e = inv.execution;
    std::printf("{\"result\":\"ok\",\"core_version\":\"%s\",\"build_id\":\"%s\","
        "\"namespace\":\"%s\",\"state\":%u,\"reason\":%u,\"request_id\":\"%llu\","
        "\"inventory_abi\":%u,\"kind\":%u,\"outcome\":%u,\"flags\":%u,\"native_exception\":%u,"
        "\"weapons_before\":%u,\"weapons_after\":%u,"
        "\"equipment_before\":%u,\"equipment_after\":%u,"
        "\"special_before\":%u,\"special_after\":%u,"
        "\"upgrades_before\":%u,\"upgrades_after\":%u,"
        "\"health_tier_before\":%u,\"health_tier_after\":%u,"
        "\"armor_tier_before\":%u,\"armor_tier_after\":%u,"
        "\"ammo_tier_before\":%u,\"ammo_tier_after\":%u,"
        "\"operations_applied\":\"%llu\"}\n",
        r.snapshot.core.version, r.snapshot.core.build_id, inv.namespace_id, e.state, e.reason, e.request_id,
        inv.abi_version, inv.kind, inv.outcome, inv.flags, inv.native_exception,
        inv.weapons_before, inv.weapons_after,
        inv.equipment_before, inv.equipment_after,
        inv.special_before, inv.special_after,
        inv.upgrades_before, inv.upgrades_after,
        static_cast<uint32_t>(inv.health_tier_before), static_cast<uint32_t>(inv.health_tier_after),
        static_cast<uint32_t>(inv.armor_tier_before), static_cast<uint32_t>(inv.armor_tier_after),
        static_cast<uint32_t>(inv.ammo_tier_before), static_cast<uint32_t>(inv.ammo_tier_after),
        inv.operations_applied);
    return 0;
}
