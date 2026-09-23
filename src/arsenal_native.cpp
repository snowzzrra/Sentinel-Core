#include "arsenal.h"
#include "inventory.h"
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
save::BTrace installation_trace;
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;

constexpr const char* mastery_perks[13] = {
    "perk/player/weapons/shotgun/pop_rocket_more_bombs",
    "perk/player/weapons/shotgun/secondary_full_auto_ammo_giveback",
    "perk/player/weapons/heavy_cannon/bolt_action_mastery_upgrades",
    "perk/player/weapons/heavy_cannon/burst_detonate_mastery",
    "perk/player/weapons/plasma_rifle/secondary_aoe_mastery",
    "perk/player/weapons/plasma_rifle/secondary_microwave_mastery",
    "perk/player/weapons/rocket_launcher/detonate_explosive_array_horizontal",
    "perk/player/weapons/rocket_launcher/lockon_mastery",
    "perk/player/weapons/double_barrel/meat_hook_mastery",
    "perk/player/weapons/gauss_cannon/ballista_mastery",
    "perk/player/weapons/gauss_cannon/destroyer_charge_levels",
    "perk/player/weapons/chaingun/turret_mastery",
    "perk/player/weapons/chaingun/energy_shell_mastery",
};

struct MasteryState {
    char namespace_id[65]{};
    uint64_t generation = 0;
    uintptr_t player = 0;
    uint16_t desired = 0;
    uint16_t applied = 0;
    uintptr_t components[13]{};
    uint64_t next_check_ms = 0;
};
MasteryState mastery_state;
std::atomic<bool> mastery_ready{false};
thread_local bool applying_mastery = false;
thread_local unsigned activation_depth = 0;
using UpgradeReplay = void(*)(uintptr_t);
using UpgradeActivate = void(*)(uintptr_t, uintptr_t, uint8_t, uint8_t);
UpgradeReplay original_upgrade_replay = nullptr;
UpgradeActivate original_upgrade_activate = nullptr;


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

// Resolve one authored upgrade without giving a perk, creating an inventory
// item, or inserting anything into the normal active-upgrade list.
bool mastery_target(uintptr_t p, unsigned index, uintptr_t& component, uintptr_t& upgrade) {
    __try {
        const auto type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1631f90)();
        const auto perk = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(image_base + 0x17aa5d0)(type, mastery_perks[index], 1);
        if (!perk || std::strcmp(*reinterpret_cast<const char* const*>(perk + 8), mastery_perks[index])) return false;
        const auto item_decl = *reinterpret_cast<uintptr_t*>(perk + 0x108);
        const auto upgrades = *reinterpret_cast<uintptr_t*>(perk + 0x118);
        if (!item_decl || !upgrades || *reinterpret_cast<int32_t*>(perk + 0x120) != 1) return false;
        upgrade = *reinterpret_cast<uintptr_t*>(upgrades);
        const auto inv = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x763080)(p);
        const auto item = inv ? reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(image_base + 0x1690660)(inv, item_decl) : 0;
        if (!item || !upgrade) return false;
        const auto item_vtable = *reinterpret_cast<uintptr_t*>(item);
        component = reinterpret_cast<uintptr_t(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(item_vtable + 0x1c8))(item);
        if (!component || !*reinterpret_cast<uintptr_t*>(component + 0x28)) return false;
        const auto component_vtable = *reinterpret_cast<uintptr_t*>(component);
        return *reinterpret_cast<uintptr_t*>(component_vtable + 0x20) == image_base + 0x164fc20;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool apply_mastery(uintptr_t component, uintptr_t upgrade) {
    if (applying_mastery) return false;
    applying_mastery = true;
    bool applied = false;
    __try {
        reinterpret_cast<void(*)(uintptr_t, uintptr_t)>(image_base + 0x164fc20)(component, upgrade);
        applied = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    applying_mastery = false;
    return applied;
}

void reapply_masteries(uintptr_t component) {
    if (!mastery_ready.load(std::memory_order_acquire) || applying_mastery ||
        !active() || !native::gameplay_admitted() || !mastery_state.desired ||
        std::memcmp(mastery_state.namespace_id, save::session().namespace_id().c_str(), 65)) return;
    const auto p = player(nullptr);
    if (!p || p != mastery_state.player) return;
    for (unsigned i = 0; i < 13; ++i) {
        const auto bit = static_cast<uint16_t>(1u << i);
        if (!(mastery_state.desired & bit)) continue;
        uintptr_t owned_component = 0, upgrade = 0;
        if (mastery_target(p, i, owned_component, upgrade) && owned_component == component) {
            mastery_state.applied &= ~bit;
            if (apply_mastery(component, upgrade)) {
                mastery_state.applied |= bit;
                mastery_state.components[i] = component;
            }
        }
    }
}

void upgrade_replay_hook(uintptr_t component) {
    if (original_upgrade_replay) original_upgrade_replay(component);
    if (!activation_depth) reapply_masteries(component);
}

void upgrade_activate_hook(uintptr_t component, uintptr_t upgrade, uint8_t a, uint8_t b) {
    ++activation_depth;
    if (original_upgrade_activate) original_upgrade_activate(component, upgrade, a, b);
    --activation_depth;
    if (!activation_depth) reapply_masteries(component);
}

// Other native Arsenal primitives remain quarantined.
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

void tick_masteries(uint64_t generation, uintptr_t p) {
    if (!mastery_ready.load(std::memory_order_acquire) || !p ||
        !native::gameplay_admitted() || !active() ||
        std::memcmp(mastery_state.namespace_id, save::session().namespace_id().c_str(), 65)) return;
    if (mastery_state.generation != generation || mastery_state.player != p) {
        mastery_state.generation = generation;
        mastery_state.player = p;
        mastery_state.applied = 0;
        std::fill_n(mastery_state.components, 13, uintptr_t{0});
        mastery_state.next_check_ms = 0;
    }
    const auto now = GetTickCount64();
    if (now < mastery_state.next_check_ms) return;
    mastery_state.next_check_ms = now + 500;
    for (unsigned i = 0; i < 13; ++i) {
        const auto bit = static_cast<uint16_t>(1u << i);
        if (!(mastery_state.desired & bit)) continue;
        uintptr_t component = 0, upgrade = 0;
        if (!mastery_target(p, i, component, upgrade)) {
            mastery_state.applied &= ~bit;
            mastery_state.components[i] = 0;
            continue;
        }
        if ((mastery_state.applied & bit) && mastery_state.components[i] == component) continue;
        mastery_state.applied &= ~bit;
        if (apply_mastery(component, upgrade)) {
            mastery_state.applied |= bit;
            mastery_state.components[i] = component;
        }
    }
}

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
    if (request.kind == SC_ARSENAL_PROJECT_MASTERY) {
        if (!mastery_ready.load(std::memory_order_acquire) || !inventory::available() ||
            !request.masteries || request.mods || request.upgrades || request.select_weapon ||
            request.select_mod || request.challenge_index || request.challenge_progress ||
            request.challenge_completed) {
            out.outcome = SC_ARSENAL_OUTCOME_UNAVAILABLE;
            return;
        }
        if (std::memcmp(mastery_state.namespace_id, request.namespace_id, 65)) {
            mastery_state = {};
            std::memcpy(mastery_state.namespace_id, request.namespace_id, 65);
        }
        out.masteries_ap_before = mastery_state.desired;
        mastery_state.desired |= static_cast<uint16_t>(request.masteries);
        mastery_state.next_check_ms = 0;
        tick_masteries(request.execution.expected.lifecycle_generation, player(nullptr));
        out.masteries_ap_after = mastery_state.desired;
        out.flags |= SC_ARSENAL_FLAG_BEFORE_VALID | SC_ARSENAL_FLAG_AFTER_VALID |
            SC_ARSENAL_FLAG_SELECTION_PRESERVED;
        if (out.masteries_ap_before != out.masteries_ap_after) out.flags |= SC_ARSENAL_FLAG_MUTATED;
        if ((mastery_state.applied & request.masteries) != request.masteries) {
            out.flags |= SC_ARSENAL_FLAG_DEFERRED;
            out.outcome = SC_ARSENAL_OUTCOME_DEFERRED;
        } else {
            out.outcome = out.masteries_ap_before == out.masteries_ap_after ?
                SC_ARSENAL_OUTCOME_NOOP : SC_ARSENAL_OUTCOME_OK;
        }
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
    installation_trace.record(save::BStage::native_start, save::BStatus::entered, "validating");
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
        const auto target = make_target(image_base, s.offset, s.bytes);
        const auto reason = native::validate_target(memory, binding.image, target, stop, deadline);
        std::array<uint8_t, 32> actual{};
        const auto read = memory.copy(target.address, actual.data(), actual.size());
        uint64_t expected_words[4]{}, actual_words[4]{};
        std::memcpy(expected_words, target.bytes.data(), 32);
        std::memcpy(actual_words, actual.data(), 32);
        installation_trace.record(save::BStage::native_start, reason ? save::BStatus::refused : save::BStatus::entered,
            reason ? "site_refused" : "site_validated", 0,
            {{"rva", s.offset}, {"reason", reason}, {"read_reason", read.reason}, {"read_error", read.error},
             {"expected0", expected_words[0]}, {"expected1", expected_words[1]},
             {"expected2", expected_words[2]}, {"expected3", expected_words[3]},
             {"observed0", actual_words[0]}, {"observed1", actual_words[1]},
             {"observed2", actual_words[2]}, {"observed3", actual_words[3]}});
        if (reason) return;
    }
    for (const auto& s : sites) {
        if (s.detour) {
            const auto status = MH_CreateHook(reinterpret_cast<void*>(image_base + s.offset), s.detour, s.original);
            if (status != MH_OK) {
                installation_trace.record(save::BStage::native_start, save::BStatus::refused, "hook_create_failed", 0,
                    {{"rva", s.offset}, {"native_error", status}});
                return;
            }
        }
    }
    for (const auto& s : sites) {
        if (s.detour) {
            const auto status = MH_EnableHook(reinterpret_cast<void*>(image_base + s.offset));
            if (status != MH_OK) {
                installation_trace.record(save::BStage::native_start, save::BStatus::refused, "hook_enable_failed", 0,
                    {{"rva", s.offset}, {"native_error", status}});
                return;
            }
        }
    }
    ready.store(true, std::memory_order_release);
    installation_trace.record(save::BStage::native_start, save::BStatus::succeeded, "installed", 0, {{"hooks", 3}});

    // Q3 is independent: a changed mastery site must never disable the three
    // retail-qualified Meat Hook detours above.
    mastery_ready.store(false, std::memory_order_release);
    if (!inventory::available()) return;
    const auto mastery_deadline = GetTickCount64() + 10000;
    const Site mastery_sites[] = {
        {0x1631f90, "488d05c9c70603c3cccccccccccccccc488d05f9950603c3cccccccccccccccc", nullptr, nullptr},
        {0x164fc20, "488bc44889480855488d68a14881ecf000000048895820488970f0488978e84c", nullptr, nullptr},
        {0x164e9b0, "488bc44889480855488d68e84881ec10010000488958f0488970e8488978e04c", reinterpret_cast<void*>(upgrade_replay_hook), reinterpret_cast<void**>(&original_upgrade_replay)},
        {0x1651040, "4885d20f8450030000448844241853415541574883ec4048896c2460450fb6e9", reinterpret_cast<void*>(upgrade_activate_hook), reinterpret_cast<void**>(&original_upgrade_activate)},
    };
    for (const auto& s : mastery_sites) {
        const auto reason = native::validate_target(memory, binding.image,
            make_target(image_base, s.offset, s.bytes), stop, mastery_deadline);
        if (reason) {
            installation_trace.record(save::BStage::native_start, save::BStatus::refused,
                "mastery_site_refused", 0, {{"rva", s.offset}, {"reason", reason}});
            return;
        }
    }
    for (const auto& s : mastery_sites) {
        if (!s.detour) continue;
        const auto status = MH_CreateHook(reinterpret_cast<void*>(image_base + s.offset), s.detour, s.original);
        if (status != MH_OK) {
            installation_trace.record(save::BStage::native_start, save::BStatus::refused,
                "mastery_hook_create_failed", 0, {{"rva", s.offset}, {"native_error", status}});
            return;
        }
    }
    for (const auto& s : mastery_sites) {
        if (!s.detour) continue;
        const auto status = MH_EnableHook(reinterpret_cast<void*>(image_base + s.offset));
        if (status != MH_OK) {
            MH_DisableHook(reinterpret_cast<void*>(image_base + 0x164e9b0));
            installation_trace.record(save::BStage::native_start, save::BStatus::refused,
                "mastery_hook_enable_failed", 0, {{"rva", s.offset}, {"native_error", status}});
            return;
        }
    }
    mastery_ready.store(true, std::memory_order_release);
    installation_trace.record(save::BStage::native_start, save::BStatus::succeeded,
        "mastery_installed", 0, {{"hooks", 2}});
}

save::BSnapshot installation_diagnostics() { return installation_trace.snapshot(); }

} // namespace sentinel::arsenal
