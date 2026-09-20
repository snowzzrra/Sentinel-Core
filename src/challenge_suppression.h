#pragma once
#include "challenge_match.h"
#include "engine_observer.h"

namespace sentinel::challenge {
// Installs the scoped completion observer, the canonical group witness and the
// currency-writer seam adapter. Binding is all-or-nothing: available() stays
// false and no hook is reachable unless every documented target qualified.
void install(const engine::Binding&, HANDLE stop);
bool available();
}
