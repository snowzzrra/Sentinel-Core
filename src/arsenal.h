#ifndef SENTINEL_INTERNAL_ARSENAL_H
#define SENTINEL_INTERNAL_ARSENAL_H
#include "sentinel_arsenal.h"
#include "engine_observer.h"
#include <windows.h>
#include <cstdint>

namespace sentinel::arsenal {

struct SnapshotFacts {
    uint32_t weapons = 0;
    uint32_t mods = 0;
    uint8_t selected_mods[8]{};
    uint32_t normal_upgrades = 0;
    uint16_t masteries_ap = 0;
    uint16_t mastery_challenges_active = 0;
    uint16_t mastery_challenges_completed = 0;
    uint16_t masteries_effective = 0;
    uint32_t mission_challenges_active = 0;
    uint32_t mission_challenges_completed = 0;
    uint32_t mission_challenges_progress[32]{};
};

uint16_t compute_effective_masteries(uint32_t weapons, uint32_t mods,
                                     uint16_t ap_masteries, uint16_t challenges_completed);
uint32_t mod_for_upgrade(uint32_t upgrade_mask);
uint32_t weapon_for_mod(uint32_t mod_mask);

struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void* context) = nullptr;
    bool (*read)(void* context, uintptr_t player_ptr, SnapshotFacts& facts) = nullptr;
    uint32_t (*ensure_mods)(void* context, uintptr_t player_ptr, uint32_t mods) = nullptr;
    uint32_t (*select_mod)(void* context, uintptr_t player_ptr, uint8_t weapon_idx, uint8_t mod_idx) = nullptr;
    uint32_t (*purchase_upgrade)(void* context, uintptr_t player_ptr, uint32_t upgrades) = nullptr;
    uint32_t (*project_mastery)(void* context, uintptr_t player_ptr, uint16_t masteries) = nullptr;
    uint32_t (*update_challenge)(void* context, uintptr_t player_ptr, uint16_t challenge_idx, uint32_t progress, uint8_t completed) = nullptr;
    bool (*refresh)(void* context, uintptr_t player_ptr) = nullptr;
    void (*bind_run_state)(void* context, uintptr_t player_ptr) = nullptr;
};

bool valid(const sc_arsenal_request& request);
bool same(const sc_arsenal_request& a, const sc_arsenal_request& b);
void initial(const sc_arsenal_request& request, sc_arsenal_result& out);
void execute(const sc_arsenal_request& request, sc_arsenal_result& out, const Calls& calls);

void install(const engine::Binding& binding, HANDLE stop);
bool available();
bool admitted(const char* id);
void execute_native(const sc_arsenal_request& request, sc_arsenal_result& out);
void bind_run_state_if_needed(uintptr_t player_ptr);
void reset_session();

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id);
#endif

} // namespace sentinel::arsenal

#endif
