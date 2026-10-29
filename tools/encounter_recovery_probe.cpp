#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>
#include <cstring>
#include <windows.h>

int encounter_recovery_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1], L"--finish-encounter")) return -1;
    if (argc != 2) return 2;
    _setmode(_fileno(stdin), _O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(), 1, message.size(), stdin);
    uint16_t op = 0; sentinel::recovery::Request request{};
    if (sentinel::decode_request(message, count, &op, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, &request) != sentinel::WireResult::ok ||
        op < sentinel::encounter_observe_operation || op > sentinel::encounter_release_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto deadline = GetTickCount64() + request.execution.deadline_ms;
    auto reply = sentinel::query_recovery(request.execution.expected.pid, op == sentinel::encounter_observe_operation ? 500 : 2000, op, request);
    while (op == sentinel::encounter_observe_operation && reply.result == sentinel::ProbeResult::ok &&
        reply.recovery.state == SC_DIAGNOSTIC_QUEUED) {
        if (GetTickCount64() >= deadline) {
            reply = sentinel::query_recovery(request.execution.expected.pid, 500, sentinel::encounter_release_operation, request);
            break;
        }
        Sleep(10);
        reply = sentinel::query_recovery(request.execution.expected.pid, 500, op, request);
    }
    if (reply.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\"}\n", sentinel::result_name(reply.result));
        return static_cast<int>(reply.result);
    }
    const auto& r = reply.recovery;
    std::array<uint8_t, 16> nonce{};
    std::memcpy(nonce.data(), r.request.execution.nonce, nonce.size());
    std::printf("{\"result\":\"ok\",\"build_id\":\"%s\",\"namespace\":\"%s\","
        "\"request_id\":\"%llu\",\"nonce\":\"%s\",\"state\":%u,\"outcome\":%u,"
        "\"token\":\"%llu\",\"stamp\":\"%llu\",\"save\":\"%s\",\"message\":\"%s\"}\n",
        reply.snapshot.core.build_id, r.request.namespace_id, r.request.execution.request_id,
        sentinel::instance_text(nonce).c_str(),
        r.state, r.outcome, r.request.token, r.request.stamp, r.request.save, sentinel::recovery::message(r.outcome));
    return 0;
}
