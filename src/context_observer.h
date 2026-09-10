#pragma once
#include "sentinel_context.h"
#include "engine_observer.h"

namespace sentinel::context {
// Uptime timestamps keep their historical epoch. Only elapsed acceptance uses QPC.
// Explicit clock injection is an internal test seam, never an IPC control.
struct Clock {
    bool (*counter)(int64_t&);
    uint64_t (*uptime)();
    int64_t frequency;
};
const Clock& observation_clock();
struct Evidence {
    uint64_t started_at_ms = 0, elapsed_ns = 0, budget_ns = 0;
    uint32_t timing_valid = 0, timing_error = 0;
    engine::ReadResult state{}, map{}; // Before budget/cancellation suppression.
};
sc_context_snapshot unavailable(uint32_t reason);
sc_context_snapshot sample(engine::Memory& memory, const engine::Binding& binding,
                           uint64_t sequence, HANDLE stop = nullptr, uint32_t budget_ms = 50,
                           Evidence* evidence = nullptr, const Clock* clock = nullptr);
sc_context_snapshot freshness(sc_context_snapshot snapshot, uint64_t now);
}
