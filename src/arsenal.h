#ifndef SENTINEL_INTERNAL_ARSENAL_H
#define SENTINEL_INTERNAL_ARSENAL_H
#include "sentinel_arsenal.h"
#include "engine_observer.h"
#include <windows.h>
#include <cstdint>
#include "save_b_trace.h"

namespace sentinel::arsenal {

uint32_t shared_mods();
void authorize_hook(uint32_t mods);

bool valid(const sc_arsenal_request& request);
bool same(const sc_arsenal_request& a, const sc_arsenal_request& b);
void initial(const sc_arsenal_request& request, sc_arsenal_result& out);

void install(const engine::Binding& binding, HANDLE stop);
bool available();
save::BSnapshot installation_diagnostics();
save::BSnapshot heat_blast_ui_diagnostics();
bool admitted(const char* id);
void execute_native(const sc_arsenal_request& request, sc_arsenal_result& out);
void reset_session();
void tick_masteries(uint64_t generation, uintptr_t player);

#ifdef SC_NATIVE_TESTING
bool test_mastery_masks();
#endif

} // namespace sentinel::arsenal

#endif
