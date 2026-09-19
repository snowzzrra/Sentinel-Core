#pragma once
#include "sentinel_runes.h"
#include "engine_observer.h"
#include <windows.h>
#include <cstdint>

namespace sentinel::runes {

struct SnapshotFacts {
    uint32_t owned_normal = 0;
    uint32_t owned_support = 0;
    int8_t selected_slots[3]{-1, -1, -1};
    int8_t selected_support = -1;
    uint8_t unlocked_slots = 0;
    uint8_t derived_pairs = 0;
    uint8_t health_tier = 0;
    uint8_t armor_tier = 0;
    uint8_t ammo_tier = 0;
};

uint8_t compute_derived_crystal_pairs(uint8_t health, uint8_t armor, uint8_t ammo);

struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void* context) = nullptr;
    bool (*read)(void* context, uintptr_t player_ptr, SnapshotFacts& facts) = nullptr;
    uint32_t (*ensure_normal_runes)(void* context, uintptr_t player_ptr, uint32_t normal_mask) = nullptr;
    uint32_t (*ensure_support_runes)(void* context, uintptr_t player_ptr, uint32_t support_mask) = nullptr;
    uint32_t (*select_normal_rune)(void* context, uintptr_t player_ptr, uint8_t slot_idx, int8_t rune_idx) = nullptr;
    uint32_t (*select_support_rune)(void* context, uintptr_t player_ptr, int8_t support_idx) = nullptr;
    uint32_t (*sync_crystal_pairs)(void* context, uintptr_t player_ptr, uint8_t pair_mask) = nullptr;
    bool (*refresh)(void* context, uintptr_t player_ptr) = nullptr;
    void (*bind_run_state)(void* context, uintptr_t player_ptr) = nullptr;
};

extern Calls calls;

bool valid(const sc_runes_request& request);
bool same(const sc_runes_request& a, const sc_runes_request& b);
sc_runes_result initial(const sc_runes_request& request);
void execute(const sc_runes_request& request, sc_runes_result& out, const Calls& calls);

void install(const engine::Binding& binding, HANDLE stop);
bool available();
bool admitted(const char* id);
void execute_native(const sc_runes_request& request, sc_runes_result& out);
void bind_run_state_if_needed(uintptr_t player, uint64_t generation = 0);
void reset_session(const char* namespace_id);

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id);
#endif

} // namespace sentinel::runes
