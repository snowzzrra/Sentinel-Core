#include "inventory.h"
#include "native_target.h"
#include "save_session.h"
#include <atomic>
#include <cstring>
#include <algorithm>

namespace sentinel::inventory {
namespace {
std::atomic<bool> ready{false};
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;
SnapshotFacts native_facts{};

uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(image_base + 0x69af70)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool read(void*, uintptr_t p, SnapshotFacts& facts) {
    if (!p) return false;
    __try {
        facts = native_facts;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint32_t ensure_items(void*, uintptr_t p, uint32_t weapons, uint32_t equip, uint32_t special, uint32_t upgrades) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        native_facts.weapons |= weapons;
        native_facts.equipment |= equip;
        native_facts.special_weapons |= special;
        native_facts.persistent_upgrades |= upgrades;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t set_capacity(void*, uintptr_t p, uint8_t health, uint8_t armor, uint8_t ammo) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        if (health <= SC_INVENTORY_MAX_CAPACITY_TIER)
            native_facts.health_tier = std::max(native_facts.health_tier, health);
        if (armor <= SC_INVENTORY_MAX_CAPACITY_TIER)
            native_facts.armor_tier = std::max(native_facts.armor_tier, armor);
        if (ammo <= SC_INVENTORY_MAX_CAPACITY_TIER)
            native_facts.ammo_tier = std::max(native_facts.ammo_tier, ammo);
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

bool refresh(void*, uintptr_t p) {
    if (!p) return false;
    __try {
        const auto hud = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x143c350)(p);
        if (!hud) return true;
        const auto id = *reinterpret_cast<int16_t*>(hud + 0xc);
        const auto manager = *reinterpret_cast<uintptr_t*>(image_base + 0x47dd908);
        if (id != -1 && manager) {
            int32_t values[5]{};
            reinterpret_cast<int32_t*(*)(int32_t*, uintptr_t)>(image_base + 0xf22f00)(values, p);
            reinterpret_cast<void(*)(uintptr_t, int16_t, uint16_t, const void*)>(image_base + 0x17c1180)(manager, id, 0x11e, values);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void bind_run_state(void*, uintptr_t p) {
    if (!p) return;
    __try {
        ensure_items(nullptr, p, native_facts.weapons, native_facts.equipment,
                     native_facts.special_weapons, native_facts.persistent_upgrades);
        set_capacity(nullptr, p, native_facts.health_tier, native_facts.armor_tier, native_facts.ammo_tier);
        refresh(nullptr, p);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

Calls calls{nullptr, player, read, ensure_items, set_capacity, refresh, bind_run_state};

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif
}

bool available() { return ready.load(std::memory_order_acquire); }

bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}

void execute_native(const sc_inventory_request& request, sc_inventory_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_INV_UNAVAILABLE; return; }
    out.flags |= SC_INV_SHARED_STATE_BOUND;
    execute(request, out, calls);
}

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id) {
    calls = value;
    std::memcpy(fixture_namespace, id, 65);
    ready.store(true, std::memory_order_release);
}
#endif

void install(const engine::Binding& binding, HANDLE stop) {
    (void)stop;
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;
    ready.store(true, std::memory_order_release);
}
}
