#pragma once
#include "sentinel_special.h"
#include "engine_observer.h"
#include <windows.h>
#include <cstdint>

namespace sentinel::special {

bool valid(const sc_special_request&);
bool same(const sc_special_request&, const sc_special_request&);
sc_special_result initial(const sc_special_request&);

struct SnapshotFacts {
    uint8_t native_crucible = 0;
    uint8_t native_hammer = 0;
    uint8_t native_hammer_perks = 0;   /* 0..2 permanent upgrade perks present */
    uint8_t native_selected = SC_SPECIAL_WEAPON_NONE;
    uint32_t known = 0;                /* SC_SPECIAL_KNOWN_* */
    uint32_t crucible_charge = 0;
    uint32_t crucible_charge_max = 0;
};

// Internal production seam; never externally supplied or serialized.
struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void* context) = nullptr;
    bool (*read)(void* context, uintptr_t player_ptr, SnapshotFacts& facts) = nullptr;
    uint32_t (*ensure)(void* context, uintptr_t player_ptr, uint32_t own_crucible, uint32_t own_hammer, uint32_t hammer_tier) = nullptr;
    uint32_t (*select)(void* context, uintptr_t player_ptr, uint32_t selected) = nullptr;
    uint32_t (*refill)(void* context, uintptr_t player_ptr) = nullptr;
    bool (*present)(void* context, uintptr_t player_ptr, uint32_t balance, uint32_t flags, uint32_t used) = nullptr;
    void (*bind_run_state)(void* context, uintptr_t player_ptr, uint32_t own_crucible, uint32_t own_hammer,
                           uint32_t hammer_tier, uint32_t selected) = nullptr;
};

// Called by the native input seam on the validated game thread. Creates one
// pending use-request at most; a press while a live request is pending is
// coalesced and never queued twice.
bool create_refill_request(uint64_t now_ms);

extern Calls calls;

void execute(const sc_special_request&, sc_special_result&, const Calls&);
void install(const engine::Binding&, HANDLE stop);
bool available();
bool admitted(const char* namespace_id);
void execute_native(const sc_special_request&, sc_special_result&);
void bind_run_state_if_needed(uintptr_t player, uint64_t generation = 0);
void refresh_input_config();
void poll_input(uintptr_t player, bool safe_gameplay);
void reset_session(const char* namespace_id);

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls, const char* namespace_id);
#endif
}
