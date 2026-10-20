#pragma once
#include "engine_observer.h"

namespace sentinel::expanded {
bool install(const engine::Binding&, HANDLE stop);
void stop();
void map_freed();
void observe(uintptr_t world);
#ifdef SC_NATIVE_TESTING
bool test_movement_inputs();
bool test_supply_inputs();
bool test_limit_inputs();
bool test_tracking_inputs();
bool test_immediate_inputs();
bool test_spirit_inputs();
bool test_quantity_inputs();
bool test_recipe_inputs();
bool test_recipe_transport();
bool test_summon_inputs();
bool test_samuel_inputs();
bool test_show_inputs();
bool test_independent_inputs();
bool test_traversal_inputs();
bool test_point_inputs();
bool test_resurrection_inputs();
bool test_direct_inputs();
bool test_death_actions();
bool test_functional_inputs();
#endif
}
