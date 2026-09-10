#pragma once
#include "sentinel_context.h"
#include "engine_observer.h"

namespace sentinel::context {
sc_context_snapshot unavailable(uint32_t reason);
sc_context_snapshot sample(engine::Memory& memory, const engine::Binding& binding,
                           uint64_t sequence, HANDLE stop = nullptr);
sc_context_snapshot freshness(sc_context_snapshot snapshot, uint64_t now);
}
