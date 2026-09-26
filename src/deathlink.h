#pragma once
#include "sentinel_deathlink.h"
#include "engine_observer.h"
#include <windows.h>
#include <cstdint>

namespace sentinel::deathlink {

bool valid(const sc_deathlink_request&);
bool same(const sc_deathlink_request&, const sc_deathlink_request&);
sc_deathlink_result initial(const sc_deathlink_request&);

struct ApplicationOutcome {
    uint32_t true_death = 0;
    uint32_t extra_life = 0;
    uint32_t protection = SC_DEATHLINK_PROTECTION_NONE;
};

// Internal production seam; never externally supplied or serialized.
struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void* context) = nullptr;
    // One legitimate lethal application through Doom's normal damage pipeline.
    // Positive outcome counters are measured around the call by the native
    // module; a native exception is returned as the error code.
    uint32_t (*apply_lethal)(void* context, uintptr_t player_ptr, ApplicationOutcome& outcome) = nullptr;
    // Optional fixture reader; production never gates Hardcore on protection.
    bool (*protection_active)(void* context, uintptr_t player_ptr) = nullptr;
    // One direct native death transition, outside the damage/protection pipeline.
    uint32_t (*force_death)(void* context, uintptr_t player_ptr, ApplicationOutcome& outcome) = nullptr;
};

extern Calls calls;

// Called by native death hooks. `cause` distinguishes remote-caused deaths
// (suppressed from outbound) from normal local true deaths. Hooks run on the
// engine thread and must stay minimal.
void record_native_death(uint32_t cause, uint32_t protection, uint64_t now_ms);

void execute(const sc_deathlink_request&, sc_deathlink_result&, const Calls&);
void tick(const Calls&);
void tick_native();
void expire_pending(uint64_t now);
void install(const engine::Binding&, HANDLE stop);
bool available();
bool admitted(const char* namespace_id);
void execute_native(const sc_deathlink_request&, sc_deathlink_result&);
void reset_session(const char* namespace_id);
void bind_run_state_if_needed(uintptr_t player);

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls, const char* namespace_id);
#endif
}
