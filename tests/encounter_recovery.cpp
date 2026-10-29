#include "encounter_recovery.h"
#include "protocol.h"
#include <cassert>
#include <cstring>

using namespace sentinel;
recovery::Request bind(recovery::Queue& queue, const recovery::Request& request, const recovery::Context& context, uint64_t now) {
    assert(queue.inspect(request, now).state == SC_DIAGNOSTIC_QUEUED);
    assert(queue.has_pending());
    queue.observe(context, now);
    const auto result = queue.inspect(request, now);
    assert(result.state == SC_DIAGNOSTIC_EXECUTED);
    assert(!queue.has_pending());
    return result.request;
}
int main() {
    recovery::Queue queue;
    assert(!queue.has_pending());
    recovery::Context context;
    context.scope.pid = 12; context.scope.process_created = 13; context.scope.instance_id[0] = 1;
    context.scope.lifecycle_generation = 4; context.stamp = 8;
    context.namespace_id = std::string(64, 'a'); context.save = "ap-fixture/GAME-AUTOSAVE0";
    context.map = "game/base/arena"; context.gameplay = true;
    context.candidates = 1; context.fingerprint = 6; context.encounter = "encounter/arena";
    queue.observe(context, 100);
    recovery::Request request{};
    request.execution.expected = context.scope; request.execution.request_id = 1;
    request.execution.deadline_ms = 2000; request.execution.nonce[0] = 1;
    std::memcpy(request.namespace_id, context.namespace_id.c_str(), 65);
    request = bind(queue, request, context, 100);
    const auto submit = queue.submit(request, 101);
    assert(submit.state == SC_DIAGNOSTIC_QUEUED && queue.history().size() == 1);
    assert(queue.has_pending());
    assert(queue.submit(request, 102).state == SC_DIAGNOSTIC_QUEUED);
    assert(queue.history().size() == 1);
    queue.tick(context.stamp, 103);
    assert(!queue.has_pending());
    assert(queue.result(request, true, 104).outcome == recovery::unsupported);
    context.fingerprint = 7; queue.observe(context, 105);
    assert(queue.submit(request, 106).outcome == recovery::unsupported);
    assert(queue.submit(request, 106, false).outcome == recovery::unsupported);
    assert(queue.result(request, false, 106, false).outcome == recovery::unsupported);
    request.execution.request_id = 2;
    assert(queue.submit(request, 107).outcome == recovery::changed);
    request.execution.request_id = 3;
    request = bind(queue, request, context, 108); queue.submit(request, 109);
    ++context.scope.lifecycle_generation; ++context.stamp; queue.observe(context, 110);
    queue.tick(context.stamp, 111);
    assert(queue.result(request, false, 112).outcome == recovery::changed);
    request.execution.expected = context.scope;
    request.execution.request_id = 4; request = bind(queue, request, context, 113);
    queue.submit(request, 114); context.save += "-other"; queue.observe(context, 115);
    queue.tick(context.stamp, 116);
    assert(queue.result(request, false, 117).outcome == recovery::changed);
    for (uint64_t id = 5; id < 30; ++id) {
        context.gameplay = id % 2 == 0; context.candidates = 0; queue.observe(context, 120);
        request.execution.request_id = id;
        request = bind(queue, request, context, 120);
        queue.submit(request, 121); queue.tick(context.stamp, 122);
        assert(queue.result(request, false, 123).outcome == recovery::no_active);
    }
    assert(queue.history().size() == 16);
    context.gameplay = true; context.candidates = 2; queue.observe(context, 130);
    queue.enqueue_console(context.stamp, 131);
    assert(queue.has_pending());
    queue.observe(context, 131);
    assert(queue.tick(context.stamp, 132) == std::vector<recovery::Outcome>{recovery::unsupported});
    assert(!queue.has_pending());
    assert(queue.tick(context.stamp, 133).empty());
    queue.enqueue_console(context.stamp, 134);
    queue.observe(context, 134);
    assert(queue.tick(context.stamp + 1, 135) == std::vector<recovery::Outcome>{recovery::changed});
    request.execution.request_id = 39; request = bind(queue, request, context, 136);
    context.fingerprint = 20; queue.observe(context, 137);
    const auto repeated_observation = queue.inspect(request, 138);
    assert(repeated_observation.request.token == request.token);
    assert(queue.submit(repeated_observation.request, 139).outcome == recovery::changed);
    request.execution.request_id = 38; request = bind(queue, request, context, 140);
    queue.observe(context, 2141);
    assert(queue.submit(request, 2142).outcome == recovery::no_active);
    request.execution.request_id = 40; request = bind(queue, request, context, 2143);
    Message wire{}; const auto length = encode_recovery_request(wire, encounter_submit_operation, request);
    assert(length == 265);
    recovery::Request decoded{}; uint16_t operation = 0;
    assert(decode_request(wire, length, &operation, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, &decoded) == WireResult::ok);
    assert(decoded.token == request.token && decoded.stamp == request.stamp && operation == encounter_submit_operation);
    assert(decode_request(wire, length - 1) == WireResult::malformed);
    Snapshot identity{}; identity.pid = context.scope.pid; identity.process_created = context.scope.process_created;
    identity.instance[0] = 1; identity.core.abi_version = SC_ABI_VERSION;
    strcpy_s(identity.core.version, "fixture"); strcpy_s(identity.core.build_id, "fixture-build");
    recovery::Result response{request, SC_DIAGNOSTIC_EXECUTED, recovery::unsupported}, roundtrip{};
    const auto response_size = encode_recovery_response(wire, WireResult::ok, encounter_result_operation, identity, response);
    WireResult code{}; Snapshot actual{};
    assert(decode_recovery_response(wire, response_size, code, encounter_result_operation, actual, roundtrip));
    assert(roundtrip.outcome == recovery::unsupported && roundtrip.request.token == request.token);
    request.execution.request_id = 41; request = bind(queue, request, context, 2144);
    queue.submit(request, 2145);
    context.namespace_id = std::string(64, 'b'); queue.observe(context, 2146);
    queue.tick(context.stamp, 2147);
    assert(queue.result(request, false, 2148).outcome == recovery::changed);
    recovery::Queue flood;
    flood.observe(context, 2200);
    for (unsigned i = 0; i < 20; ++i) flood.enqueue_console(context.stamp, 2201);
    flood.observe(context, 2201);
    const auto replies = flood.tick(context.stamp, 2202);
    assert(replies.size() == 20 && flood.history().size() == 16);
    assert(replies.front() == recovery::unknown && replies.back() == recovery::unsupported);
    assert(!flood.has_pending());
    recovery::Queue idle;
    unsigned captures = 0;
    const auto frame = [&](uint64_t now) {
        if (idle.has_pending()) {
            ++captures;
            idle.observe(context, now);
            idle.tick(context.stamp, now);
        }
    };
    frame(3000); frame(3001);
    assert(captures == 0);
    idle.enqueue_console(context.stamp, 3002);
    frame(3003); frame(3004);
    assert(captures == 1 && idle.history().back().result.outcome == recovery::unsupported);
    idle.enqueue_console(context.stamp, 3005);
    ++context.stamp;
    frame(3006); frame(3007);
    assert(captures == 2 && idle.history().back().result.outcome == recovery::changed);
    request.execution.request_id = 42;
    request.execution.expected = context.scope;
    std::memcpy(request.namespace_id, context.namespace_id.c_str(), 65);
    request = bind(idle, request, context, 3100);
    assert(!idle.has_pending());
    assert(idle.submit(request, 3600).state == SC_DIAGNOSTIC_QUEUED);
    ++context.fingerprint;
    frame(3601); frame(3602);
    assert(captures == 3 && idle.result(request, false, 3603).outcome == recovery::changed);
    request.execution.request_id = 43;
    assert(idle.inspect(request, 3700).state == SC_DIAGNOSTIC_QUEUED);
    assert(idle.inspect(request, 5701).outcome == recovery::no_active);
    assert(!idle.has_pending());
    frame(5702);
    assert(captures == 3);
    request.execution.request_id = 44;
    assert(idle.inspect(request, 5800).state == SC_DIAGNOSTIC_QUEUED);
    ++context.scope.lifecycle_generation; ++context.stamp;
    frame(5801);
    assert(idle.inspect(request, 5802).outcome == recovery::changed && !idle.has_pending());
    request.execution.request_id = 45; request.execution.expected = context.scope;
    assert(idle.inspect(request, 5900).state == SC_DIAGNOSTIC_QUEUED);
    assert(idle.result(request, true, 5901).outcome == recovery::no_active && !idle.has_pending());
    idle.enqueue_console(context.stamp, 6001);
    frame(6000);
    assert(idle.has_pending());
    frame(6002);
    assert(!idle.has_pending() && idle.history().back().result.outcome == recovery::unsupported);
}
