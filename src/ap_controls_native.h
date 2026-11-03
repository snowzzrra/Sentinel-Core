#pragma once
#include "engine_observer.h"

namespace sentinel::ap_controls {
bool install(const engine::Binding&, HANDLE stop);
void stop();
bool input_inhibited() noexcept;
bool recovery_input_allowed() noexcept;
}
