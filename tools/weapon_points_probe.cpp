#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

// Binary stdin is the fixed typed wire request, not a command or memory endpoint.
// The ordinary inspection client still authenticates the exact live process.
int weapon_points_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1],L"--weapon-points")) return -1;
    if (argc != 2) {
        std::puts("--weapon-points reads one fixed SCIP WUP request on binary stdin and returns JSON."); return 2;
    }
    _setmode(_fileno(stdin),_O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(),1,message.size(),stdin);
    uint16_t op = 0; sc_weapon_points_request request{};
    if (sentinel::decode_request(message,count,&op,nullptr,nullptr,nullptr,nullptr,&request) != sentinel::WireResult::ok ||
        op < sentinel::weapon_points_submit_operation || op > sentinel::weapon_points_release_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto r = sentinel::query_weapon_points(request.execution.expected.pid,2000,op,request);
    if (r.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n",sentinel::result_name(r.result),r.win32_error);
        return static_cast<int>(r.result);
    }
    const auto& p = r.weapon_points; const auto& e = p.execution;
    std::printf("{\"result\":\"ok\",\"core_version\":\"%s\",\"build_id\":\"%s\","
        "\"namespace\":\"%s\",\"state\":%u,\"reason\":%u,\"request_id\":\"%llu\","
        "\"kind\":%u,\"amount\":%u,\"expected_gained\":%u,\"outcome\":%u,\"flags\":%u,"
        "\"balance_before\":%u,\"balance_after\":%u,\"gained_before\":%u,\"gained_after\":%u,"
        "\"native_exception\":%u,\"suppressed_grants\":\"%llu\"}\n",
        r.snapshot.core.version,r.snapshot.core.build_id,p.namespace_id,e.state,e.reason,e.request_id,
        p.kind,p.amount,p.expected_gained,p.outcome,p.flags,p.balance_before,p.balance_after,p.gained_before,p.gained_after,
        p.native_exception,p.suppressed_grants);
    return 0;
}
