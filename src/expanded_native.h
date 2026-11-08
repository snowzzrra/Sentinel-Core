#pragma once
#include "engine_observer.h"
#include "encounter_recovery.h"

namespace sentinel::expanded {
bool install(const engine::Binding&, HANDLE stop);
void stop();
void map_freed();
void observe(uintptr_t world);
recovery::Context recovery_context(uintptr_t world, const char* map);
bool drain_recovery(recovery::Attempt&, const recovery::Context&);
bool recovery_player_allowed(uintptr_t player);
std::string recipe_fault_diagnostic();
}
