#include "arsenal.h"
#include "native_target.h"
#include "save_session.h"
#include <atomic>
#include <cstring>
#include <algorithm>

namespace sentinel::arsenal {
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

uint32_t ensure_mods(void*, uintptr_t p, uint32_t mods) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        native_facts.mods |= mods;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t select_mod(void*, uintptr_t p, uint8_t weapon_idx, uint8_t mod_idx) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        if (weapon_idx < 8) {
            native_facts.selected_mods[weapon_idx] = mod_idx;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t purchase_upgrade(void*, uintptr_t p, uint32_t upgrades) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        native_facts.normal_upgrades |= upgrades;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t project_mastery(void*, uintptr_t p, uint16_t masteries) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        native_facts.masteries_ap |= masteries;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t update_challenge(void*, uintptr_t p, uint16_t challenge_idx, uint32_t progress, uint8_t completed) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        if (challenge_idx < 32) {
            native_facts.mission_challenges_progress[challenge_idx] = progress;
            if (completed) {
                native_facts.mission_challenges_completed |= (1u << challenge_idx);
            }
        }
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
        ensure_mods(nullptr, p, native_facts.mods);
        for (unsigned i = 0; i < 8; ++i) {
            if (native_facts.selected_mods[i]) {
                select_mod(nullptr, p, static_cast<uint8_t>(i), native_facts.selected_mods[i]);
            }
        }
        purchase_upgrade(nullptr, p, native_facts.normal_upgrades);
        project_mastery(nullptr, p, native_facts.masteries_ap);
        refresh(nullptr, p);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

Calls calls{nullptr, player, read, ensure_mods, select_mod,
            purchase_upgrade, project_mastery, update_challenge,
            refresh, bind_run_state};

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif
} // namespace

bool available() { return ready.load(std::memory_order_acquire); }

bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}

void execute_native(const sc_arsenal_request& request, sc_arsenal_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_ARSENAL_OUTCOME_UNAVAILABLE; return; }
    out.flags |= SC_ARSENAL_FLAG_SHARED_STATE_BOUND;
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

} // namespace sentinel::arsenal
