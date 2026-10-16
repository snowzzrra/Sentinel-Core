#pragma once
#include "engine_observer.h"

namespace sentinel::expanded {
bool install(const engine::Binding&, HANDLE stop);
void stop();
}
