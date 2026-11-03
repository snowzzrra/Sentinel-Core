#pragma once
#include "commands.h"
#include "engine_observer.h"
#include <string>
#include <vector>

namespace sentinel::commands {
uint32_t prepare(const engine::Binding&, HANDLE stop);
bool available();
void disable();
uint32_t prepare_local(const engine::Binding&, HANDLE);
bool admitted(const sc_command_request& request);
void execute_native(const sc_command_request&, sc_command_result&, uintptr_t player);
bool execute_boon(const char* text, sc_command_result&, uintptr_t player);
bool execute_visual_cleanup(const char* text, sc_command_result&);
void console_tick(uint64_t generation);
void manual_tick(uintptr_t player, uint64_t generation, uint64_t (*current_generation)(), bool recovery_pending);
void enemy_spawn_trace(const char* state, const char* origin, const char* body, const char* point, uintptr_t actor,
                       const char* request = "", const char* parent = "", uint64_t ordinal = 0);
void enemy_worker_trace(const char* state, const char* manager, uintptr_t worker, uint64_t mask, const char* label,
                        const char* request, uint64_t emitted, bool closed);
void darklord_trace(const char* line, bool console_allowed) noexcept;
void diagnostic_trace(const char* format, ...) noexcept;
std::vector<std::string> diagnostic_history();
void checkpoint_player_position(uintptr_t player, uint64_t generation);
void enemy_kill_wait_trace(const char* owner, const char* label, uint64_t mask, int32_t target, uint32_t seen, bool complete);
void enemy_quantity_wait_trace(char kind, const char* owner, uint64_t mask, double target, int32_t count, bool complete, bool fault);
}
