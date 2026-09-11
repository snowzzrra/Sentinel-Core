#pragma once
#include "sentinel_save.h"
#include "context_observer.h"

namespace sentinel::save {
sc_save_snapshot unavailable(uint32_t reason);
sc_save_snapshot sample(engine::Memory& memory, const engine::Binding& binding,
                        uint64_t sequence, HANDLE stop = nullptr,
                        const context::Clock* clock = nullptr);
sc_save_snapshot freshness(sc_save_snapshot snapshot, uint64_t now);
}
