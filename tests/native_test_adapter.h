#pragma once
#include "native_target.h"
#include "sentinel_inspection.h"
#include "context_observer.h"

// Compiled only into the task-owned test host, never the candidate Core DLL.
// These explicit fixture facts exercise hook/queue mechanics, not DOOM offsets.
namespace sentinel::native {
struct TestAdapter {
    std::array<Target, 3> targets;
    uintptr_t root, common, caller;
    uint32_t (*owner)();
    uint32_t (*game)();
    uint8_t (*pending)();
    sc_context_snapshot (*context)();
    sc_context_snapshot (*observe)(context::Evidence&);
    uintptr_t (*map)();
    bool (*checkpoint)(uintptr_t descriptor, uint8_t& flag);
    bool (*primary)(uintptr_t slot, uintptr_t& map);
    void (*gate)(bool executed); // Deterministic race gate in this test build only.
};
void test_start(const TestAdapter& adapter, const Snapshot& identity, HANDLE stop);
void test_events(const engine::Binding&, uint64_t (*change)(uintptr_t,uintptr_t,uintptr_t));
uint64_t test_change(uintptr_t root, uintptr_t descriptor, uintptr_t files);
void test_free(uintptr_t root,void (*free)(uintptr_t,uintptr_t));
void test_generation_gap();
}
