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
bool confirmed_dead(uintptr_t world, uintptr_t actor);
void stop();
#ifdef SC_NATIVE_TESTING
bool test_observation_contention();
#endif
}
#endif
