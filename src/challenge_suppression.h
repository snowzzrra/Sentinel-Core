#pragma once
#include "challenge_match.h"
#include "engine_observer.h"

namespace sentinel::challenge {
// check every target before enabling the completion, group and currency hooks; one failed check keeps all hooks off
void install(const engine::Binding&, HANDLE stop);
bool available();
bool consume_qualified_battery_toast();
}
