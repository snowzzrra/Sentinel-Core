#include "encounter_recovery.h"
#include <cassert>
#include <set>

using namespace sentinel::recovery;
namespace {
unsigned calls = 0;
std::set<uintptr_t> killed;
Queue* reporting_queue = nullptr;
bool drain(Attempt& attempt, const Context& context) {
    ++calls;
    if (reporting_queue) {
        assert(reporting_queue->history().size() == 1);
        reporting_queue->enqueue_console(context.stamp, attempt.admitted_at);
        reporting_queue = nullptr;
    }
    for (const auto& target : attempt.targets) for (const auto& manager : context.managers) {
        if (target.address != manager.address) continue;
        for (const auto& actor : manager.actors) killed.insert(actor.address);
    }
    return true;
}
Context context() {
    Context value;
    value.scope.lifecycle_generation = 4; value.stamp = 8; value.world = 12; value.player = 13;
    value.map = "game/sp/e1m1_intro/e1m1_intro"; value.gameplay = value.supported = true;
    value.managers = {{1, 11, true, false, false, false, "arena", "manager", "combat", {{20, 1}, {21, 2}}},
                      {2, 12, true, false, false, false, "overlap", "manager", "combat", {{21, 2}, {22, 3}}},
                      {3, 13, false, false, false, false, "future", "manager", "", {{30, 4}}},
                      {4, 14, true, true, false, false, "complete", "manager", "", {{40, 5}}}};
    return value;
}
}
int main() {
    Queue queue;
    auto current = context();
    assert(!queue.has_pending() && queue.tick(8, 100, drain).empty() && !calls);
    queue.enqueue_console(8, 100); queue.observe(current, 100);
    reporting_queue = &queue;
    assert(queue.tick(8, 100, drain) == std::vector<Outcome>{pending});
    assert(killed == std::set<uintptr_t>({20, 21, 22}));
    assert(queue.history().back().targets.size() == 2);
    queue.enqueue_console(8, 101); queue.enqueue_console(8, 102);
    queue.observe(current, 102);
    assert(queue.tick(8, 102, drain) == std::vector<Outcome>{pending} && queue.history().size() == 1);
    current.managers[0].actors = {{23, 5}}; current.managers[1].actors.clear();
    queue.observe(current, 200); assert(queue.tick(8, 200, drain).empty());
    assert(killed.count(23));
    current.managers[0].complete = true; current.managers[1].complete = true;
    queue.observe(current, 300); assert(queue.tick(8, 300, drain) == std::vector<Outcome>{finished});
    assert(!queue.has_pending());
    const auto done_calls = calls;
    assert(queue.tick(8, 301, drain).empty() && calls == done_calls);
    for (const auto result : {no_active, unsupported, unknown, changed}) {
        Queue attempt;
        current = context();
        attempt.enqueue_console(8, 100);
        if (result == no_active) current.managers.clear();
        if (result == unsupported) current.managers[0].actors[0].protected_actor = true;
        if (result == unknown) current.readable = false;
        attempt.observe(current, 100);
        const auto before = calls;
        assert(attempt.tick(result == changed ? 9 : 8, 100, drain) == std::vector<Outcome>{result});
        assert(calls == before && !attempt.has_pending());
    }
    for (const auto* map : {"e1m4_boss", "e2m4_boss", "e3m4_boss", "e5m4_boss"}) {
        Queue attempt; current = context(); current.map = std::string("game/sp/") + map + "/" + map;
        attempt.enqueue_console(8, 100); attempt.observe(current, 100);
        assert(attempt.tick(8, 100, drain) == std::vector<Outcome>{unsupported});
    }
    assert(protected_manager("game/dlc/e4m2_swamp/e4m2_swamp", "boss_encounter_manager_1"));
    assert(!protected_manager("game/dlc/e4m2_swamp/e4m2_swamp", "boss_encounter_manager_forest_arena_2"));
    assert(protected_manager("game/dlc/e4m3_mcity/e4m3_mcity", "platformarea_samuel_encounter_manager_dlc1_1"));
    assert(protected_actor("confront_samuel_ai", "idai"));
    assert(protected_actor("boss_intro_ai_superheavy_doomhunter_stage1_1", "idai"));
    assert(protected_actor("renamed", ".?AVidAI2_SamuelBoss@@"));
    assert(!protected_actor("regular_doomhunter", ".?AVidAI2_DoomHunter@@"));
    for (unsigned path = 0; path < 7; ++path) {
        Queue attempt; current = context();
        attempt.enqueue_console(8, 100); attempt.observe(current, 100); attempt.tick(8, 100, drain);
        switch (path) {
        case 0: current.gameplay = false; break;
        case 1: ++current.scope.lifecycle_generation; break;
        case 2: ++current.world; break;
        case 3: current.map += "other"; break;
        case 4: ++current.player; break;
        case 5: ++current.managers[0].identifier; break;
        case 6: current.namespace_id = "other"; break;
        }
        attempt.observe(current, 200);
        const auto before = calls;
        assert(attempt.tick(8, 200, drain) == std::vector<Outcome>{changed});
        assert(calls == before);
    }
    Queue timeout; current = context();
    timeout.enqueue_console(8, 100); timeout.observe(current, 100); timeout.tick(8, 100, drain);
    current.managers[0].wait = "player trigger";
    timeout.observe(current, 60100);
    assert(timeout.tick(8, 60100, drain) == std::vector<Outcome>{unknown});
    assert(timeout.history().back().wait.find("player trigger") != std::string::npos);
    Queue external;
    current = context(); external.enqueue_console(8, 100); external.observe(current, 100); external.tick(8, 100, drain);
    current.managers[0].actors.clear(); current.managers[0].noncombat_wait = true;
    current.managers[0].wait = "WaitForEventFlag";
    external.observe(current, 2100);
    assert(external.tick(8, 2100, drain) == std::vector<Outcome>{unknown});
    assert(external.history().back().wait.find("WaitForEventFlag") != std::string::npos);
    for (unsigned i = 0; i < 20; ++i) {
        current.gameplay = false; timeout.enqueue_console(8, 60200 + i); timeout.observe(current, 60200 + i);
        timeout.tick(8, 60200 + i, drain);
    }
    assert(timeout.history().size() == 16);
}
