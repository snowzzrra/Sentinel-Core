#include "sentinel_inspection.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>

namespace {
void json_text(const char* value) {
    std::putchar('"');
    for (; *value; ++value) {
        if (*value == '"' || *value == '\\') std::putchar('\\');
        std::putchar(*value);
    }
    std::putchar('"');
}
}
int combat_command(int argc, wchar_t** argv) {
    if (argc == 2 && !std::wcscmp(argv[1], L"--combat-presentation")) {
        _setmode(_fileno(stdin), _O_BINARY);
        sentinel::Message message{};
        const auto count = std::fread(message.data(), 1, message.size(), stdin);
        uint16_t operation = 0; sc_combat_presentation_request request{};
        if (sentinel::decode_request(message, count, &operation, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            &request) != sentinel::WireResult::ok || operation != sentinel::combat_presentation_operation) {
            std::puts("{\"result\":\"malformed_request\"}"); return 2;
        }
        const auto result = sentinel::query_combat_presentation(request.execution.expected.pid, 2000, request);
        if (result.result != sentinel::ProbeResult::ok) {
            std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n",
                sentinel::result_name(result.result), result.win32_error); return static_cast<int>(result.result);
        }
        const auto& value = result.combat_presentation;
        std::printf("{\"result\":\"ok\",\"combat_presentation_abi\":%u,\"pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"",
            value.abi_version, value.scope.pid, value.scope.process_created);
        for (const auto b : value.scope.instance_id) std::printf("%02x", b);
        std::printf("\",\"build_id\":"); json_text(result.snapshot.core.build_id);
        std::printf(",\"core_version\":"); json_text(result.snapshot.core.version);
        std::printf(",\"lifecycle_generation\":\"%llu\",\"request_id\":\"%llu\",\"nonce\":\"",
            value.scope.lifecycle_generation, value.request_id);
        for (const auto b : value.nonce) std::printf("%02x", b);
        std::printf("\",\"status\":%u,\"reason\":%u,\"total\":\"%llu\",\"target\":\"%llu\",\"milestone\":\"%llu\",\"visible\":%u}\n",
            value.status, value.reason, value.total, value.target, value.milestone, value.visible);
        return 0;
    }
    if (argc < 2 || std::wcscmp(argv[1], L"--combat")) return -1;
    if (argc != 2) {
        std::puts("--combat reads one SCIP combat request on binary stdin and returns JSON."); return 2;
    }
    _setmode(_fileno(stdin), _O_BINARY);
    sentinel::Message message{};
    const auto count = std::fread(message.data(), 1, message.size(), stdin);
    uint16_t operation = 0; sc_diagnostic_request request{}; sc_combat_query query{};
    if (sentinel::decode_request(message, count, &operation, &request, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, &query) != sentinel::WireResult::ok ||
        operation != sentinel::combat_operation) {
        std::puts("{\"result\":\"malformed_request\"}"); return 2;
    }
    const auto result = sentinel::query_combat(request.expected.pid, 2000, query);
    if (result.result != sentinel::ProbeResult::ok) {
        std::printf("{\"result\":\"%s\",\"win32_error\":%u}\n",
                    sentinel::result_name(result.result), result.win32_error);
        return static_cast<int>(result.result);
    }
    const auto& page = result.combat;
    std::printf("{\"result\":\"ok\",\"combat_abi\":%u,\"pid\":%u,\"process_created\":\"%llu\",\"instance_id\":\"",
                page.abi_version, page.scope.pid, page.scope.process_created);
    for (const auto byte : page.scope.instance_id) std::printf("%02x", byte);
    std::printf("\",\"build_id\":"); json_text(result.snapshot.core.build_id);
    std::printf(",\"core_version\":"); json_text(result.snapshot.core.version);
    std::printf(",\"lifecycle_generation\":\"%llu\",\"availability\":%u,\"reason\":%u,"
                "\"observation_gaps\":\"%llu\",\"qualified_kills\":\"%llu\",\"namespace_id\":",
                page.scope.lifecycle_generation, page.availability, page.reason,
                page.observation_gaps, page.qualified_kills);
    json_text(page.namespace_id); std::puts("}");
    return 0;
}
