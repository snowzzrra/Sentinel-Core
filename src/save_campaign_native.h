#pragma once
#include "engine_observer.h"
namespace sentinel::save {
bool install_campaign_hooks(const engine::Binding&, HANDLE);
bool campaign_change_begin(uintptr_t root, uintptr_t descriptor, uint64_t generation);
void campaign_change_end(bool success, uint64_t generation, uint32_t game);
}
