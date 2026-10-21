#pragma once
#include "commands.h"
#include "engine_observer.h"

namespace sentinel::commands {
uint32_t prepare(const engine::Binding&, HANDLE stop);
bool available();
void disable();
uint32_t prepare_local(const engine::Binding&, HANDLE);
bool admitted(const sc_command_request& request);
void execute_native(const sc_command_request&, sc_command_result&, uintptr_t player);
void console_tick(uint64_t generation);
void manual_tick(uintptr_t player, uint64_t generation, uint64_t (*current_generation)());
void enemy_spawn_trace(const char* state, const char* origin, const char* body, const char* point, uintptr_t actor);
void enemy_kill_wait_trace(const char* owner, const char* label, uint64_t mask, int32_t target, uint32_t seen, bool complete);
}
