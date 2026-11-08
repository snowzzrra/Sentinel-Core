#pragma once
#include "sentinel_inventory.h"
#include "sentinel_evidence.h"
#include "engine_observer.h"

namespace sentinel::inventory {

bool valid(const sc_inventory_request&);
bool same(const sc_inventory_request&, const sc_inventory_request&);
sc_inventory_result initial(const sc_inventory_request&);
uint8_t capacity_stage(uint32_t available, uint32_t active, int host_level);

struct SnapshotFacts {
    uint32_t weapons = SC_INVENTORY_UNKNOWN_MASK;
    uint32_t equipment = SC_INVENTORY_UNKNOWN_MASK;
    uint32_t special_weapons = 0;
    uint32_t persistent_upgrades = 0;
    uint8_t health_tier = SC_INVENTORY_UNKNOWN_TIER;
    uint8_t armor_tier = SC_INVENTORY_UNKNOWN_TIER;
    uint8_t ammo_tier = SC_INVENTORY_UNKNOWN_TIER;
    uint8_t reserved = 0;
    uint8_t ice_bomb = SC_INVENTORY_UNKNOWN_ITEM;
    uint8_t dash = SC_INVENTORY_UNKNOWN_ITEM;
    uint8_t slayer_key = SC_INVENTORY_UNKNOWN_ITEM;
    uint8_t frag_grenade = SC_INVENTORY_UNKNOWN_ITEM;
    uint8_t flame_belch = SC_INVENTORY_UNKNOWN_ITEM;
    uint8_t blood_punch = SC_INVENTORY_UNKNOWN_ITEM;
    uint64_t perks_known = 0, perks_owned = 0;
    uint8_t pickup_radius = SC_INVENTORY_UNKNOWN_ITEM;
    int32_t slayer_key_quantity = -1;
};

// internal code only; don't accept or serialize it as input
struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void*) = nullptr;
    bool (*read)(void*, uintptr_t, SnapshotFacts&) = nullptr;
};

void execute(const sc_inventory_request&, sc_inventory_result&, const Calls&);
void install(const engine::Binding&);
bool available();
bool blood_punch_observed();
bool microwave_observed();
bool microwave_counter(const SnapshotFacts&);
bool stone_counter_observed();
bool stone_counter(const SnapshotFacts&, bool hammer);
bool armored_counter_observed();
bool armored_counter(const SnapshotFacts&);
bool admitted(const char* namespace_id);
uint8_t grenade_indicator_mask(uintptr_t player); // 1 frag, 2 ice, 0 none, 255 unknown
void execute_native(const sc_inventory_request&, sc_inventory_result&);
void progression_facts(const SnapshotFacts&, sc_progression_result&);
void read_progression(uintptr_t player, sc_progression_result&);
void reset_session(const char* namespace_id);

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls, const char* namespace_id);
#endif
}
