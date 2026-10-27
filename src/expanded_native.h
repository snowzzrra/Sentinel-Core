#pragma once
#include "engine_observer.h"

namespace sentinel::expanded {
bool install(const engine::Binding&, HANDLE stop);
void stop();
void map_freed();
void observe(uintptr_t world);
#ifdef SC_NATIVE_TESTING
bool test_limit_inputs();
bool test_recipe_inputs();
bool test_recipe_transport();
bool test_quantity_inputs();
#endif
}
