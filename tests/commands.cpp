#undef NDEBUG
#include "commands.h"
#include "native_model.h"
#include "sentinel_protocol_generated.h"
#include "sentinel_version.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace sentinel;
namespace sentinel::runes { Calls calls{}; }
namespace sentinel::special { Calls calls{}; }
int main() {
    assert(commands::checked_visual_location("ai_ScriptCmdEnt ap_hide_location_visual_7770302 activate player1") == 7770302);
    assert(commands::checked_visual_location("ai_ScriptCmdEnt ap_hide_location_visual_7770302 activate") == 7770302);
    for (const char* text : {"ai_ScriptCmdEnt ap_hide_location_visual_7780000 activate player1",
            "ai_ScriptCmdEnt ap_hide_location_visual_7770302 activate player2",
            "ai_ScriptCmdEnt ap_hide_location_visual_7770302 activate player1 extra",
            "ai_ScriptCmdEnt ap_hide_location_visual_7770302 remove player1"})
        assert(commands::checked_visual_location(text) == 0);
    assert(commands::compatible_product(SC_PRODUCT_VERSION));
    for (const char* version : {"1.0.0", "1.0.1", "1.0.2", "1.0.3", "1.0.4", "1.0.0-rc-1", "1.0.1-rc-1", "1.0.2-rc-10", "1.0.3-rc-1", "1.1.0", "1.1.1", "1.1.0-rc-1", "2.0.0", "2.0.0-rc-1"})
        assert(commands::compatible_product(version));
    for (const char* version : {"", "1.2.0", "2.1.0", "1.0.2-rc-01", "1.0.2-rc-0",
            "1.0.2-rc-", "1.0.2-rc-1extra", "1.0.2+build", "1.0.02"})
        assert(!commands::compatible_product(version));
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
    auto* slot = queue.claim(11); assert(slot && slot->is<sc_command_request>()); assert(!queue.claim(12));
    assert(queue.command_result(r, false, 12).execution.state == SC_DIAGNOSTIC_CLAIMED);
    auto terminal = slot->result; terminal.state = SC_DIAGNOSTIC_CANCELLED;
    terminal.reason = SC_NATIVE_CANCELLED; terminal.completed_at_ms = 13;
    queue.finish(*slot, terminal);
    auto out = queue.command_result(r, false, 14); assert(out.execution.state == SC_DIAGNOSTIC_CANCELLED);
    std::puts("PASS closed commands, bounded codec, scope/dedup/claim/cancel/query/release");
}
