#pragma once
#include "engine_observer.h"
#include "encounter_recovery.h"

namespace sentinel::expanded {
bool install(const engine::Binding&, HANDLE stop);
void stop();
void map_freed();
void observe(uintptr_t world);
recovery::Context recovery_context(uintptr_t world);
bool drain_recovery(recovery::Attempt&, const recovery::Context&);
bool recovery_player_allowed(uintptr_t player);
#ifdef SC_NATIVE_TESTING
bool test_stopped_hooks();
bool test_hook_faults();
bool test_limit_inputs();
bool test_recipe_inputs();
bool test_recipe_transport();
bool test_quantity_inputs();
bool test_recovery_inputs();
using TestRecoveryDeath = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, int32_t, int32_t, uintptr_t, uintptr_t, float);
bool test_recovery_kill(uintptr_t world, uintptr_t actor, TestRecoveryDeath);
#endif
}
