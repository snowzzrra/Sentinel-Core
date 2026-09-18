#pragma once
#include "sentinel_inventory.h"
#include "engine_observer.h"

namespace sentinel::inventory {

bool valid(const sc_inventory_request&);
bool same(const sc_inventory_request&, const sc_inventory_request&);
sc_inventory_result initial(const sc_inventory_request&);

struct SnapshotFacts {
    uint32_t weapons = 0;
    uint32_t equipment = 0;
    uint32_t special_weapons = 0;
    uint32_t persistent_upgrades = 0;
    uint8_t health_tier = 0;
    uint8_t armor_tier = 0;
    uint8_t ammo_tier = 0;
    uint8_t reserved = 0;
};

// Internal production seam; never externally supplied or serialized.
struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void*) = nullptr;
    bool (*read)(void*, uintptr_t, SnapshotFacts&) = nullptr;
    uint32_t (*ensure_items)(void*, uintptr_t, uint32_t weapons, uint32_t equipment, uint32_t special, uint32_t upgrades) = nullptr;
    uint32_t (*set_capacity)(void*, uintptr_t, uint8_t health, uint8_t armor, uint8_t ammo) = nullptr;
    bool (*refresh)(void*, uintptr_t) = nullptr;
    void (*bind_run_state)(void*, uintptr_t) = nullptr;
};

void execute(const sc_inventory_request&, sc_inventory_result&, const Calls&);
void install(const engine::Binding&, HANDLE stop);
bool available();
bool admitted(const char* namespace_id);
void execute_native(const sc_inventory_request&, sc_inventory_result&);
void bind_run_state_if_needed(uintptr_t player);
void reset_session(const char* namespace_id);

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls, const char* namespace_id);
#endif
}
