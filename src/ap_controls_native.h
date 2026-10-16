#pragma once
#include "engine_observer.h"

namespace sentinel::ap_controls {
bool install(const engine::Binding&, HANDLE stop);
void stop();
bool input_inhibited() noexcept;
#ifdef SC_NATIVE_TESTING
bool test_text_capture();
#endif
}
