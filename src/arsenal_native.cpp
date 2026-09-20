#include "arsenal.h"
#include "native_target.h"
#include "native_runtime.h"
#include "save_session.h"
#include "MinHook.h"
#include <atomic>
#include <cstring>
#include <algorithm>

namespace sentinel::arsenal {
namespace {

std::atomic<bool> ready{false};
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;


#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif

bool active() {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return ready.load(std::memory_order_acquire);
#endif
    return ready.load(std::memory_order_acquire) && save::session().routed();
}

using IsReadyToFire = bool(*)(uintptr_t, int32_t);
using FireFn = bool(*)(uintptr_t, uintptr_t, uintptr_t, int32_t*, uintptr_t);
using UpdateAimFn = void(*)(uintptr_t);

IsReadyToFire original_is_ready = nullptr;
FireFn original_fire = nullptr;
UpdateAimFn original_update_aim = nullptr;

bool is_ready_hook(uintptr_t self, int32_t fire_mode) {
    if (active() && fire_mode == 1) {
        const uint32_t mods = native::gameplay_admitted() ? shared_mods() : 0;
        if (!(mods & SC_ARSENAL_ATTACHMENT_MEAT_HOOK)) {
            return false;
        }
    }
    return original_is_ready ? original_is_ready(self, fire_mode) : false;
}

bool fire_hook(uintptr_t self, uintptr_t fire_mode_def, uintptr_t p3, int32_t* p4, uintptr_t p5) {
    if (active() && fire_mode_def) {
        const auto mode = *reinterpret_cast<const int32_t*>(fire_mode_def + 0x28c);
        if (mode == 1) {
            const uint32_t mods = native::gameplay_admitted() ? shared_mods() : 0;
            if (!(mods & SC_ARSENAL_ATTACHMENT_MEAT_HOOK)) {
                return false;
            }
        }
    }
    return original_fire ? original_fire(self, fire_mode_def, p3, p4, p5) : false;
}

void update_aim_hook(uintptr_t self) {
    if (active()) {
        const uint32_t mods = native::gameplay_admitted() ? shared_mods() : 0;
        if (!(mods & SC_ARSENAL_ATTACHMENT_MEAT_HOOK)) {
            __try {
                *reinterpret_cast<uint8_t*>(self + 0x3d9a) = 1;
                *reinterpret_cast<uint32_t*>(self + 0x3dd0) = 0x1fffffe;
                *reinterpret_cast<uintptr_t*>(self + 0x3dd8) = 0;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            return;
        }
    }
    if (original_update_aim) {
        original_update_aim(self);
    }
}

native::Target make_target(uintptr_t base, uint32_t offset, const char* hex) {
    native::Target out{}; out.address = base + offset;
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t n = 0; n < out.bytes.size(); ++n)
        out.bytes[n] = static_cast<uint8_t>(digit(hex[n * 2]) * 16 + digit(hex[n * 2 + 1]));
    return out;
}

uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(image_base + 0x69af70)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Native Arsenal primitives are absent; quarantine instead of cache-as-readback.
bool read(void*, uintptr_t, SnapshotFacts&) { return false; }
uint32_t ensure_mods(void*, uintptr_t, uint32_t) { return 1; }
uint32_t select_mod(void*, uintptr_t, uint8_t, uint8_t) { return 1; }
uint32_t purchase_upgrade(void*, uintptr_t, uint32_t) { return 1; }
uint32_t project_mastery(void*, uintptr_t, uint16_t) { return 1; }
uint32_t update_challenge(void*, uintptr_t, uint16_t, uint32_t, uint8_t) { return 1; }

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

void bind_run_state(void*, uintptr_t) {}

Calls calls{nullptr, player, read, ensure_mods, select_mod,
            purchase_upgrade, project_mastery, update_challenge,
            refresh, bind_run_state};

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
    if (!valid(request)) { out.outcome = SC_ARSENAL_OUTCOME_REJECTED; return; }
    if (!admitted(request.namespace_id)) { out.outcome = SC_ARSENAL_OUTCOME_UNAVAILABLE; return; }
    out.flags |= SC_ARSENAL_FLAG_SHARED_STATE_BOUND;
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) { execute(request, out, calls); return; }
#endif
    // The intrinsic Super Shotgun detours consume this shared authorization.
    if (request.kind == SC_ARSENAL_ENSURE_MODS &&
        request.mods == SC_ARSENAL_ATTACHMENT_MEAT_HOOK) {
        out.mods_before = shared_mods();
        authorize_hook(request.mods);
        out.mods_after = shared_mods();
        out.flags |= SC_ARSENAL_FLAG_BEFORE_VALID | SC_ARSENAL_FLAG_AFTER_VALID |
            SC_ARSENAL_FLAG_SELECTION_PRESERVED;
        if (out.mods_after != out.mods_before) out.flags |= SC_ARSENAL_FLAG_MUTATED;
        out.outcome = (out.mods_after & SC_ARSENAL_ATTACHMENT_MEAT_HOOK) ?
            SC_ARSENAL_OUTCOME_OK : SC_ARSENAL_OUTCOME_UNAVAILABLE;
        return;
    }
    out.outcome = SC_ARSENAL_OUTCOME_UNAVAILABLE;
}

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id) {
    calls = value;
    std::memcpy(fixture_namespace, id, 65);
    ready.store(true, std::memory_order_release);
}
#endif

void install(const engine::Binding& binding, HANDLE stop) {
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;

    engine::LocalMemory memory;
    struct Site { uint32_t offset; const char* bytes; void* detour; void** original; };
    const Site sites[] = {
        {0x1670850, "4889742410574883ec208bf2488bf983fa01756c80b9a83b000000755648895c", reinterpret_cast<void*>(is_ready_hook), reinterpret_cast<void**>(&original_is_ready)},
        {0x166e750, "4c8bdc5556498dab48fdffff4881eca8030000488b056e02b4024833c4488985", reinterpret_cast<void*>(fire_hook), reinterpret_cast<void**>(&original_fire)},
        {0x166f5e0, "48895c2418574881ec00010000488db9a8000000488bd98b07488d57043b0275", reinterpret_cast<void*>(update_aim_hook), reinterpret_cast<void**>(&original_update_aim)},
    };
    const auto deadline = GetTickCount64() + 10000;
    for (const auto& s : sites) {
        if (native::validate_target(memory, binding.image, make_target(image_base, s.offset, s.bytes), stop, deadline)) return;
    }
    for (const auto& s : sites) {
        if (s.detour) {
            if (MH_CreateHook(reinterpret_cast<void*>(image_base + s.offset), s.detour, s.original) != MH_OK) return;
        }
    }
    for (const auto& s : sites) {
        if (s.detour) {
            if (MH_EnableHook(reinterpret_cast<void*>(image_base + s.offset)) != MH_OK) return;
        }
    }
    ready.store(true, std::memory_order_release);
}

} // namespace sentinel::arsenal
