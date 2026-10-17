#pragma once
#include "engine_observer.h"

namespace sentinel::expanded {
bool install(const engine::Binding&, HANDLE stop);
void stop();
#ifdef SC_NATIVE_TESTING
bool test_movement_inputs();
bool test_supply_inputs();
bool test_limit_inputs();
bool test_tracking_inputs();
#endif
}
