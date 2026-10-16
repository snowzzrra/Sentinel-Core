#ifndef SENTINEL_COMBAT_NATIVE_H
#define SENTINEL_COMBAT_NATIVE_H
#include "combat.h"
#include "engine_observer.h"
#include "sentinel_inspection.h"

namespace sentinel::combat {
void prepare(const Snapshot&);
void install(const engine::Binding&, HANDLE stop);
void transition(uint64_t generation);
void observe(const Context&);
sc_combat_snapshot inspect(const sc_combat_query&);
void stop();
}
#endif
