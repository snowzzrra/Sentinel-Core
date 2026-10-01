#undef NDEBUG
#include "commands.h"
#include "native_model.h"
#include "protocol.h"
#include "sentinel_version.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace sentinel;
namespace sentinel::runes { Calls calls{}; }
namespace sentinel::special { Calls calls{}; }
int main() {
    assert(commands::compatible_product("1.0.0-rc-1") && commands::compatible_product("1.0.0-rc-10"));
    assert(commands::compatible_product("1.0.0") && !commands::compatible_product("1.1.0-rc-1"));
    assert(!commands::compatible_product("1.0.0-rc-01") && !commands::compatible_product("1.0.0-rc-0"));
    commands::ManualRequest manual{};
    const char* spawn[] = {"chrispy", "items/pickup/ammo", "-1.25", "0", "3e2"};
    assert(commands::parse_manual(5, spawn, manual) && manual.explicit_position && manual.position[2] == 300);
    assert(!commands::parse_manual(4, spawn, manual));
    const char* invalid[] = {"chrispy", "items/pickup/ammo", "nan", "0", "1"};
    assert(!commands::parse_manual(5, invalid, manual));
    invalid[2] = "1junk"; assert(!commands::parse_manual(5, invalid, manual));
    const char* toggle[] = {"noclip", "off"};
    assert(commands::parse_manual(2, toggle, manual) && manual.kind == 2);
    sc_command_request r{};
    r.execution.expected.pid = 7; r.execution.expected.process_created = 13;
    r.execution.expected.instance_id[0] = 1; r.execution.expected.lifecycle_generation = 1;
    r.execution.request_id = 1; r.execution.nonce[0] = 42; r.execution.deadline_ms = 100;
    std::memset(r.namespace_id, 'a', 64);
    for (const char* text : {"give ammo", "judgementMeter_Set 3", "echo AP_ACTIVE_MAP_V1 map/intro",
            "condump AP_SUPPORT_FILE.txt", "condump AP_REFILL_REQUEST.txt", "listInventory player1",
            "ai_ScriptCmdEnt ap_rpc_v3_1043 activate", "ai_ScriptCmdEnt ap_fortress_phase_1 activate player1",
            "ai_ScriptCmdEnt player1 givePlayerPerk player/perks/bloodpunch", "g_damageScaleAllToAI 1.50",
            "g_damageScaleAllToSlayer 0.65", "g_infiniteAmmo 0"}) {
        assert(commands::parse(text, r) && commands::valid(r));
        assert(commands::process_scoped(r) == (std::strcmp(text, "condump AP_SUPPORT_FILE.txt") == 0));
        Message data{}; const auto size = encode_command_request(data, command_submit_operation, r);
        assert(size == 2209); uint16_t op = 0; sc_command_request decoded{};
        assert(decode_request(data, size, &op, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &decoded) == WireResult::ok);
        assert(op == command_submit_operation && commands::same(decoded, r));
        assert(decode_request(data, size - 1) == WireResult::malformed);
        data[size - 1] = 1; assert(decode_request(data, size) == WireResult::malformed);
    }
    for (const char* text : {"give ammo; quit", "give ammo\nquit", "quit", "chrispy ai/zombie",
            "condump ../AP_FILE.txt", "condump C:/AP_FILE.txt", "condump save.txt", "condump AP_FILE.txt extra",
            "ai_ScriptCmdEnt player1 remove", "g_infiniteAmmo 2", "g_damageScaleAllToAI nan",
            "g_damageScaleAllToAI inf", "g_damageScaleAllToAI 1junk", "g_giveExtraLives 99"}) assert(!commands::parse(text, r));
    assert(commands::parse("give ammo", r));
    native::Diagnostics queue;
    auto submit = [&](const sc_command_request& value) {
        return queue.submit(value.execution, 0, 10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &value);
    };
    assert(submit(r).state == SC_DIAGNOSTIC_QUEUED);
    assert(submit(r).state == SC_DIAGNOSTIC_QUEUED);
    auto changed = r; assert(commands::parse("judgementMeter_Set 3", changed));
    assert(submit(changed).reason == SC_NATIVE_DUPLICATE_MISMATCH);
    changed = r; ++changed.execution.expected.lifecycle_generation;
    assert(submit(changed).reason == SC_NATIVE_DUPLICATE_MISMATCH);
    auto* slot = queue.claim(11); assert(slot && slot->is_command); assert(!queue.claim(12));
    assert(queue.command_result(r, false, 12).execution.state == SC_DIAGNOSTIC_CLAIMED);
    auto terminal = slot->result; terminal.state = SC_DIAGNOSTIC_CANCELLED;
    terminal.reason = SC_NATIVE_CANCELLED; terminal.completed_at_ms = 13;
    queue.finish(*slot, terminal);
    auto out = queue.command_result(r, false, 14); assert(out.execution.state == SC_DIAGNOSTIC_CANCELLED);
    Snapshot identity{}; identity.pid = 7; identity.process_created = 13; identity.instance[0] = 1;
    identity.core.abi_version = SC_ABI_VERSION;
    std::memcpy(identity.core.version, SC_PRODUCT_VERSION, sizeof(SC_PRODUCT_VERSION));
    std::memset(identity.core.build_id, 'a', 64);
    Message reply{}; const auto size = encode_command_response(reply, WireResult::ok, command_result_operation, identity, out);
    assert(size); WireResult code{}; Snapshot decoded{}; sc_command_result result{};
    assert(decode_command_response(reply, size, code, command_result_operation, decoded, result));
    assert(result.execution.request_id == r.execution.request_id && result.outcome == SC_COMMAND_NOT_EXECUTED);
    auto support = r; assert(commands::parse("condump AP_SUPPORT_FILE.txt", support));
    std::memcpy(support.namespace_id, SC_COMMAND_PROCESS_NAMESPACE, 65);
    auto process_result = commands::initial(support);
    process_result.execution = out.execution;
    process_result.execution.state = SC_DIAGNOSTIC_EXECUTED;
    process_result.execution.reason = SC_NATIVE_NONE;
    process_result.execution.thread_id = 1;
    process_result.execution.lifecycle = SC_LIFETIME_ACTIVE;
    process_result.execution.site_revision = 1; process_result.execution.phase = 1;
    process_result.execution.observed_at_ms = 12; process_result.execution.executed_at_ms = 12;
    process_result.outcome = SC_COMMAND_DISPATCHED;
    const auto process_size = encode_command_response(reply, WireResult::ok, command_result_operation, identity, process_result);
    assert(process_size && decode_command_response(reply, process_size, code, command_result_operation, decoded, result));
    process_result.kind = SC_COMMAND_ECHO;
    assert(!encode_command_response(reply, WireResult::ok, command_result_operation, identity, process_result));
    process_result.kind = SC_COMMAND_CONDUMP; std::memset(process_result.namespace_id, 'a', 64);
    assert(!encode_command_response(reply, WireResult::ok, command_result_operation, identity, process_result));
    queue.command_result(r, false, 15, true);
    assert(queue.command_result(r, false, 16).execution.reason == SC_NATIVE_NOT_FOUND);
    std::puts("PASS closed commands, bounded codec, scope/dedup/claim/cancel/query/release");
}
