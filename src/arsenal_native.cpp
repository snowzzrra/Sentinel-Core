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
save::BTrace heat_blast_ui_trace;
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;

struct MasteryFamily { const char* mastery; const char* base; };
constexpr MasteryFamily mastery_families[13] = {
    {"perk/player/weapons/shotgun/pop_rocket_more_bombs", "perk/player/weapons/shotgun/pop_rocket"},
    {"perk/player/weapons/shotgun/secondary_full_auto_ammo_giveback", "perk/player/weapons/shotgun/secondary_full_auto"},
    {"perk/player/weapons/heavy_cannon/bolt_action_mastery_upgrades", "perk/player/weapons/heavy_cannon/bolt_action"},
    {"perk/player/weapons/heavy_cannon/burst_detonate_mastery", "perk/player/weapons/heavy_cannon/burst_detonate"},
    {"perk/player/weapons/plasma_rifle/secondary_aoe_mastery", "perk/player/weapons/plasma_rifle/secondary_aoe"},
    {"perk/player/weapons/plasma_rifle/secondary_microwave_mastery", "perk/player/weapons/plasma_rifle/secondary_microwave"},
    {"perk/player/weapons/rocket_launcher/detonate_explosive_array_horizontal", "perk/player/weapons/rocket_launcher/detonate"},
    {"perk/player/weapons/rocket_launcher/lockon_mastery", "perk/player/weapons/rocket_launcher/lock_on"},
    {"perk/player/weapons/double_barrel/meat_hook_mastery", "perk/player/weapons/double_barrel/meat_hook"},
    {"perk/player/weapons/gauss_cannon/ballista_mastery", "perk/player/weapons/gauss_cannon/ballista"},
    {"perk/player/weapons/gauss_cannon/destroyer_charge_levels", "perk/player/weapons/gauss_cannon/destroyer"},
    {"perk/player/weapons/chaingun/turret_mastery", "perk/player/weapons/chaingun/turret"},
    {"perk/player/weapons/chaingun/energy_shell_mastery", "perk/player/weapons/chaingun/energy_shell"},
};

enum class TargetStatus { ready, host_missing, mod_missing, unresolved };

struct MasteryState {
    char namespace_id[65]{};
    uint64_t generation = 0;
    uintptr_t player = 0;
    uint16_t desired = 0;
    uint16_t applied = 0;
    uint16_t target_unavailable = 0;
    uint16_t host_missing = 0;
    uint16_t mod_missing = 0;
    uint16_t apply_failed = 0;
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
using ArsenalUi = uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
ArsenalUi original_arsenal_ui = nullptr;


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
TargetStatus mastery_target(uintptr_t p, unsigned index, uintptr_t& component, uintptr_t& upgrade) {
    __try {
        const auto type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1631f90)();
        const auto find = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(image_base + 0x17aa5d0);
        const auto perk = find(type, mastery_families[index].mastery, 1);
        const auto base = find(type, mastery_families[index].base, 1);
        if (!perk || !base ||
            std::strcmp(*reinterpret_cast<const char* const*>(perk + 8), mastery_families[index].mastery) ||
            std::strcmp(*reinterpret_cast<const char* const*>(base + 8), mastery_families[index].base))
            return TargetStatus::unresolved;
        const auto item_decl = *reinterpret_cast<uintptr_t*>(perk + 0x108);
        const auto upgrades = *reinterpret_cast<uintptr_t*>(perk + 0x118);
        if (!item_decl || !upgrades || *reinterpret_cast<int32_t*>(perk + 0x120) != 1)
            return TargetStatus::unresolved;
        upgrade = *reinterpret_cast<uintptr_t*>(upgrades);
        const auto inv = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x763080)(p);
        const auto item = inv ? reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(image_base + 0x1690660)(inv, item_decl) : 0;
        if (!inv || !upgrade) return TargetStatus::unresolved;
        if (!item) return TargetStatus::host_missing;
        const auto base_owned = reinterpret_cast<bool(*)(uintptr_t, uintptr_t)>(image_base + 0xfe3830)(p + 0x3b40, base);
        if (!base_owned && !(index == 8 && (shared_mods() & SC_ARSENAL_ATTACHMENT_MEAT_HOOK)))
            return TargetStatus::mod_missing;
        const auto item_vtable = *reinterpret_cast<uintptr_t*>(item);
        component = reinterpret_cast<uintptr_t(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(item_vtable + 0x1c8))(item);
        if (!component || !*reinterpret_cast<uintptr_t*>(component + 0x28)) return TargetStatus::unresolved;
        const auto component_vtable = *reinterpret_cast<uintptr_t*>(component);
        return *reinterpret_cast<uintptr_t*>(component_vtable + 0x20) == image_base + 0x164fc20 ?
            TargetStatus::ready : TargetStatus::unresolved;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return TargetStatus::unresolved; }
}

void target_status(unsigned index, TargetStatus status) {
    const auto bit = static_cast<uint16_t>(1u << index);
    mastery_state.host_missing = (mastery_state.host_missing & ~bit) |
        (status == TargetStatus::host_missing ? bit : 0);
    mastery_state.mod_missing = (mastery_state.mod_missing & ~bit) |
        (status == TargetStatus::mod_missing ? bit : 0);
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

void mastery_result(unsigned index, uintptr_t component, bool target_found, bool applied) {
    const auto bit = static_cast<uint16_t>(1u << index);
    mastery_state.applied = (mastery_state.applied & ~bit) | (applied ? bit : 0);
    mastery_state.target_unavailable = (mastery_state.target_unavailable & ~bit) | (!target_found ? bit : 0);
    mastery_state.apply_failed = (mastery_state.apply_failed & ~bit) | (target_found && !applied ? bit : 0);
    mastery_state.components[index] = applied ? component : 0;
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
        const auto status = mastery_target(p, i, owned_component, upgrade);
        target_status(i, status);
        if (status != TargetStatus::ready) mastery_result(i, 0, false, false);
        else if (owned_component == component) {
            mastery_result(i, component, true, apply_mastery(component, upgrade));
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

uintptr_t arsenal_ui_hook(uintptr_t out, uintptr_t weapon, uintptr_t family, uintptr_t p) {
    const auto result = original_arsenal_ui(out, weapon, family, p);
    if (!active() || !native::gameplay_admitted() || !result || !family || !p) return result;
    __try {
        const auto base = *reinterpret_cast<uintptr_t*>(family);
        if (!base || std::strcmp(*reinterpret_cast<const char* const*>(base + 8),
                                  mastery_families[4].base)) return result;
        const auto count = *reinterpret_cast<const int32_t*>(family + 0x18);
        const auto upgrades = *reinterpret_cast<const uintptr_t*>(family + 0x10);
        const auto first = count == 2 && upgrades ?
            *reinterpret_cast<const char* const*>(*reinterpret_cast<const uintptr_t*>(upgrades) + 8) : nullptr;
        const auto second = first ?
            *reinterpret_cast<const char* const*>(*reinterpret_cast<const uintptr_t*>(upgrades + 8) + 8) : nullptr;
        const bool delay_first = first && second &&
            !std::strcmp(first, "perk/player/weapons/plasma_rifle/secondary_aoe_no_primary_delay") &&
            !std::strcmp(second, "perk/player/weapons/plasma_rifle/secondary_aoe_faster_charge");
        const bool charge_first = first && second &&
            !std::strcmp(first, "perk/player/weapons/plasma_rifle/secondary_aoe_faster_charge") &&
            !std::strcmp(second, "perk/player/weapons/plasma_rifle/secondary_aoe_no_primary_delay");
        const auto mastery = *reinterpret_cast<const uintptr_t*>(family + 0x28);
        const bool identity = (delay_first || charge_first) && mastery &&
            !std::strcmp(*reinterpret_cast<const char* const*>(mastery + 8),
                         mastery_families[4].mastery);
        const bool namespace_match = mastery_state.namespace_id[0] &&
            !std::memcmp(mastery_state.namespace_id, save::session().namespace_id().c_str(), 65);
        const bool player_match = mastery_state.player == p;
        if (!identity || !namespace_match || !player_match || !mastery_state.generation) {
            heat_blast_ui_trace.record(save::BStage::native_start, save::BStatus::blocked,
                "heat_blast_ui_unqualified", 0,
                {{"family_identity", identity}, {"namespace_match", namespace_match},
                 {"player_match", player_match}, {"generation", mastery_state.generation}}, p);
            return result;
        }
        heat_blast_ui_trace.record(save::BStage::native_start, save::BStatus::succeeded,
            "heat_blast_ui_facts", 0,
            {{"generation", mastery_state.generation}, {"player", p},
             {"base_available", *reinterpret_cast<const uint8_t*>(result + 0x10)},
             {"base_active", *reinterpret_cast<const uint8_t*>(result + 0x11)},
             {"normal_delay", *reinterpret_cast<const uint16_t*>(result + (delay_first ? 0x12 : 0x14))},
             {"normal_charge", *reinterpret_cast<const uint16_t*>(result + (delay_first ? 0x14 : 0x12))},
             {"mastery_available", *reinterpret_cast<const uint8_t*>(result + 0x1c)},
             {"challenge_decl_present", *reinterpret_cast<const uintptr_t*>(family + 0x30) != 0},
             {"challenge_field", *reinterpret_cast<const uint32_t*>(result + 0x18)},
             {"ap_mastery_requested", (mastery_state.desired & (1u << 4)) != 0}}, p);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        heat_blast_ui_trace.record(save::BStage::native_start, save::BStatus::blocked,
            "heat_blast_ui_read_fault", 0, {}, p);
    }
    return result;
}

// Arsenal clip corrections preserve native ownership and challenge state.
namespace menu {
struct Value { uint32_t type, reserved; uintptr_t payload; };
struct Calls {
    Value* (*lookup)(uintptr_t, Value*, const char*) = nullptr;
    uintptr_t (*sprite)(const Value*) = nullptr;
    uintptr_t (*text)(const Value*) = nullptr;
    void (*release)(Value*) = nullptr;
    void (*frame)(uintptr_t, int) = nullptr;
    void (*visible)(uintptr_t, bool, bool) = nullptr;
    void (*set_text)(uintptr_t, const char*) = nullptr;
    const char* (*localize)(uintptr_t) = nullptr;
    const char* (*format)(char*, const char*, ...) = nullptr;
    void (*material)(uintptr_t, uintptr_t, int) = nullptr;
} swf;
using Update = void(*)(uintptr_t);
Update original_mod = nullptr, original_mastery = nullptr;
bool (*original_bypass)(uintptr_t) = nullptr;
std::atomic<bool> installed{false};

bool admitted() {
    return installed.load(std::memory_order_acquire) && active() && native::gameplay_admitted();
}

int mastery_index(uintptr_t perk) {
    if (!perk) return -1;
    const auto name = *reinterpret_cast<const char* const*>(perk + 8);
    if (!name) return -1;
    for (int i = 0; i < 13; ++i)
        if (!std::strcmp(name, mastery_families[i].mastery)) return i;
    return -1;
}

uintptr_t child(uintptr_t parent, const char* name, bool text = false) {
    if (!parent) return 0;
    Value value{};
    const auto found = swf.lookup(parent, &value, name);
    const auto result = text ? swf.text(found) : swf.sprite(found);
    if (value.type == 2 || value.type == 8) swf.release(&value);
    return result;
}

uintptr_t root(uintptr_t widget) {
    const auto vtable = *reinterpret_cast<const uintptr_t*>(widget);
    return reinterpret_cast<uintptr_t(*)(uintptr_t)>(
        *reinterpret_cast<const uintptr_t*>(vtable + 0xe0))(widget);
}

void show(uintptr_t clip, bool visible) {
    if (clip) swf.visible(clip, visible, true);
}

void correct_mod(uintptr_t widget) {
    const auto family = *reinterpret_cast<const uintptr_t*>(widget + 0x2b8);
    if (!family) return;
    const auto index = mastery_index(*reinterpret_cast<const uintptr_t*>(family + 0x28));
    const auto base = *reinterpret_cast<const uintptr_t*>(family);
    if (index < 0 || !base || std::strcmp(*reinterpret_cast<const char* const*>(base + 8),
                                        mastery_families[index].base)) return;
    if (!*reinterpret_cast<const uint8_t*>(widget + 0x2cc)) return;
    const auto pips = child(root(widget), "pips");
    unsigned corrected = 0;
    for (unsigned i = 0; pips && i < 3; ++i) {
        // Native frame 3 denotes mastery; zero is an independently unowned upgrade.
        if (*reinterpret_cast<const int16_t*>(widget + 0x2c2 + i * 2) != 0) continue;
        char name[] = "pip0";
        name[3] += static_cast<char>(i);
        if (const auto pip = child(pips, name)) { swf.frame(pip, 1); corrected |= 1u << i; }
    }
    if (index == 4) heat_blast_ui_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
        "heat_blast_pips_rendered", 0, {{"generation", native::inspect().scope.lifecycle_generation},
        {"player", player(nullptr)}, {"corrected_zero_slots", corrected}}, widget);
}

bool canonical_mastery(uintptr_t widget) {
    return mastery_index(*reinterpret_cast<const uintptr_t*>(widget + 0x2b0)) >= 0;
}

void correct_mastery(uintptr_t widget) {
    if (!canonical_mastery(widget)) return;
    const auto clip = root(widget);
    show(child(clip, "purchaseInfo"), false);
    show(child(clip, "lockedPurchaseInfo"), false);

    const auto decl = *reinterpret_cast<const uintptr_t*>(widget + 0x2d8);
    const auto progress = *reinterpret_cast<const int32_t*>(widget + 0x2e0);
    if (!decl || progress < 0) return;
    const auto conditions = *reinterpret_cast<const uintptr_t*>(decl + 0x158);
    if (!conditions) return;
    const auto goal = *reinterpret_cast<const int32_t*>(conditions + 8);
    if (goal <= 0) return;
    const auto challenge = child(clip, "challenge");
    const auto description = child(child(challenge, "desc"), "txtVal", true);
    const auto bar = child(child(challenge, "info"), "progress");
    const auto count = child(child(bar, "count"), "txtVal", true);
    if (!challenge || !description || !bar || !count) return;

    // Use the same localized description and authored arguments as 0xf94ac0.
    char buffer[0x4010]{};
    const auto text = swf.localize(decl + 0x94);
    const auto counter = *reinterpret_cast<const uintptr_t*>(decl + 0x110);
    const auto amount = counter ? *reinterpret_cast<const int32_t*>(counter + 0x220) : 0;
    swf.set_text(description, amount >= 2 ? swf.format(buffer, text, amount, goal) :
                                           swf.format(buffer, text, goal));
    swf.set_text(count, swf.format(buffer, "%d/%d", progress, goal));
    swf.frame(bar, 1 + static_cast<int>(100.0f * std::min(progress, goal) / goal));
    const auto icon = child(challenge, "icon");
    const auto material = *reinterpret_cast<const uintptr_t*>(decl + 0xf8);
    if (icon && material) swf.material(icon, material, 0);
    show(child(clip, "warning"), false);
    show(challenge, true);
    if (mastery_index(*reinterpret_cast<const uintptr_t*>(widget + 0x2b0)) == 4)
        heat_blast_ui_trace.record(save::BStage::profile_capture, save::BStatus::succeeded,
            "heat_blast_challenge_rendered", 0, {{"generation", native::inspect().scope.lifecycle_generation},
            {"player", player(nullptr)}, {"progress", progress}, {"goal", goal}}, widget);
}

void mod_hook(uintptr_t widget) {
    original_mod(widget);
    if (!admitted()) return;
    __try { correct_mod(widget); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        installation_trace.record(save::BStage::profile_output, save::BStatus::blocked,
                                  "arsenal_mod_presentation_fault");
    }
}

void mastery_hook(uintptr_t widget) {
    original_mastery(widget);
    if (!admitted()) return;
    __try { correct_mastery(widget); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        installation_trace.record(save::BStage::profile_output, save::BStatus::blocked,
                                  "arsenal_challenge_presentation_fault");
    }
}

bool bypass_hook(uintptr_t widget) {
    if (admitted()) {
        __try { if (canonical_mastery(widget)) return false; }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return original_bypass(widget);
}

void install(const engine::Binding& binding, HANDLE stop) {
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    struct Site { uint32_t rva; const char* bytes; size_t leaf; void* hook; void** target; };
    const Site sites[] = {
        {0xf937a0, "405553488dac2408c0ffffb8f8400000e8eb858d01482be0488b0519b2210348", 0, reinterpret_cast<void*>(mod_hook), reinterpret_cast<void**>(&original_mod)},
        {0xf94ac0, "4055534155488dac2440c0ffffb8c0400000e8c9728d01482be0488b05f79e21", 0, reinterpret_cast<void*>(mastery_hook), reinterpret_cast<void**>(&original_mastery)},
        {0xf94590, "8b81e40200003981e80200000f94c0c3488991d8020000448981e0020000c3cc", 16, reinterpret_cast<void*>(bypass_hook), reinterpret_cast<void**>(&original_bypass)},
        {0x185c150, "40534883ec20488b4928488bda488b01ff5028488bc34883c4205bc3cccccccc", 0, nullptr, reinterpret_cast<void**>(&swf.lookup)},
        {0x184e470, "40534883ec20833908752b488b59084885db74224c8b03488bcb488b150f5c06", 0, nullptr, reinterpret_cast<void**>(&swf.sprite)},
        {0x184e4b0, "40534883ec20833908752b488b59084885db74224c8b03488bcb488b15d75b06", 0, nullptr, reinterpret_cast<void**>(&swf.text)},
        {0x184e3b0, "4883ec288b0183f8027527488b4908b8fffffffff00fc1413083f80175544885", 0, nullptr, reinterpret_cast<void**>(&swf.release)},
        {0x1865280, "48895c2408574883ec200fb74158bf010000003bd7488bd90f4ffa3bf8742c7d", 0, nullptr, reinterpret_cast<void**>(&swf.frame)},
        {0x1864430, "440fb6d23851517457807952007551488b41104c6349088851514d03c9488b10", 97, nullptr, reinterpret_cast<void**>(&swf.visible)},
        {0x186db00, "40534883ec20488bd94883c140e8ded4b8fe488bcb4883c4205be9e1cbffffcc", 0, nullptr, reinterpret_cast<void**>(&swf.set_text)},
        {0x360bb0, "4883ec58488b150d10f103488d0526c76e0248894424604c8d4c24208b01488d", 0, nullptr, reinterpret_cast<void**>(&swf.localize)},
        {0x362bc0, "48895424104c894424184c894c2420534883ec204c8bc24c8d4c2440ba004000", 0, nullptr, reinterpret_cast<void**>(&swf.format)},
        {0x1863b00, "48895c24084889742410574883ec20488bd9418bf0488b4960488bfa483bca0f", 0, nullptr, reinterpret_cast<void**>(&swf.material)},
    };
    for (const auto& site : sites) {
        const auto target = make_target(image_base, site.rva, site.bytes);
        const auto reason = site.leaf ? native::validate_leaf_target(memory, binding.image, target, site.leaf, stop, deadline) :
                                        native::validate_target(memory, binding.image, target, stop, deadline);
        if (reason) {
            installation_trace.record(save::BStage::profile_prepare, save::BStatus::refused,
                "arsenal_menu_site_refused", 0, {{"rva", site.rva}, {"reason", reason}});
            return;
        }
    }
    for (const auto& site : sites) {
        if (!site.hook) { *site.target = reinterpret_cast<void*>(image_base + site.rva); continue; }
        const auto status = MH_CreateHook(reinterpret_cast<void*>(image_base + site.rva), site.hook, site.target);
        if (status != MH_OK) {
            installation_trace.record(save::BStage::profile_prepare, save::BStatus::refused,
                "arsenal_menu_hook_refused", 0, {{"rva", site.rva}, {"native_error", status}});
            return;
        }
    }
    for (const auto& site : sites) {
        if (!site.hook) continue;
        const auto status = MH_EnableHook(reinterpret_cast<void*>(image_base + site.rva));
        if (status != MH_OK) {
            installation_trace.record(save::BStage::profile_prepare, save::BStatus::refused,
                "arsenal_menu_enable_refused", 0, {{"rva", site.rva}, {"native_error", status}});
            return;
        }
    }
    installed.store(true, std::memory_order_release);
    installation_trace.record(save::BStage::profile_prepare, save::BStatus::succeeded,
                              "arsenal_menu_installed", 0, {{"hooks", 3}});
}
} // namespace menu

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
        mastery_state.target_unavailable = 0;
        mastery_state.host_missing = 0;
        mastery_state.mod_missing = 0;
        mastery_state.apply_failed = 0;
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
        const auto status = mastery_target(p, i, component, upgrade);
        target_status(i, status);
        if (status != TargetStatus::ready) {
            mastery_result(i, 0, false, false);
            continue;
        }
        if ((mastery_state.applied & bit) && mastery_state.components[i] == component) {
            mastery_result(i, component, true, true);
            continue;
        }
        mastery_result(i, component, true, apply_mastery(component, upgrade));
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
            SC_ARSENAL_FLAG_SELECTION_PRESERVED | SC_ARSENAL_FLAG_EFFECTIVE_UNOBSERVED;
        if (out.masteries_ap_before != out.masteries_ap_after) out.flags |= SC_ARSENAL_FLAG_MUTATED;
        if ((mastery_state.applied & request.masteries) != request.masteries) {
            out.flags |= SC_ARSENAL_FLAG_DEFERRED;
            out.outcome = SC_ARSENAL_OUTCOME_DEFERRED;
        } else {
            out.outcome = out.masteries_ap_before == out.masteries_ap_after ?
                SC_ARSENAL_OUTCOME_NOOP : SC_ARSENAL_OUTCOME_OK;
        }
        save::session().btrace.record(save::BStage::native_start,
            out.outcome == SC_ARSENAL_OUTCOME_DEFERRED ? save::BStatus::refused : save::BStatus::succeeded,
            "mastery_projection_state", 0,
            {{"requested", request.masteries}, {"desired", mastery_state.desired},
             {"applied", mastery_state.applied}, {"target_unavailable", mastery_state.target_unavailable},
             {"host_missing", mastery_state.host_missing}, {"mod_missing", mastery_state.mod_missing},
             {"apply_failed", mastery_state.apply_failed}, {"player", mastery_state.player},
             {"generation", mastery_state.generation}});
        return;
    }
    out.outcome = SC_ARSENAL_OUTCOME_UNAVAILABLE;
}

#ifdef SC_NATIVE_TESTING
bool test_mastery_masks() {
    const auto saved = mastery_state;
    mastery_state = {};
    const auto check = [](uint16_t target, uint16_t failed, uint16_t applied, uintptr_t component) {
        return mastery_state.target_unavailable == target && mastery_state.apply_failed == failed &&
            mastery_state.applied == applied && mastery_state.components[0] == component;
    };
    mastery_result(0, 0, false, false);
    const bool a = check(1, 0, 0, 0);
    mastery_result(0, 0x100, true, false);
    const bool b = check(0, 1, 0, 0);
    mastery_result(0, 0x100, true, true);
    const bool c = check(0, 0, 1, 0x100);
    mastery_result(0, 0x100, true, false);
    const bool d = check(0, 1, 0, 0);
    mastery_result(0, 0x100, true, true);
    const bool e = check(0, 0, 1, 0x100);
    mastery_result(0, 0x200, true, true);
    const bool f = check(0, 0, 1, 0x200);
    mastery_result(0, 0, false, false);
    const bool g = check(1, 0, 0, 0);
    mastery_state = saved;
    return a && b && c && d && e && f && g;
}
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

    auto ui_target = make_target(image_base, 0xf1f420,
        "48895c240848896c241048897424184c894c24205741544155415641574883ec");
    ui_target.signature_offset = 0x20;
    ui_target.signature = make_target(image_base, 0,
        "20488911498da9403b00004c8941084c8bf9498b10488bcd4d8bf0e8d0430c00").bytes;
    const auto ui_reason = native::validate_target(memory, binding.image, ui_target, stop, GetTickCount64() + 10000);
    if (ui_reason) {
        heat_blast_ui_trace.record(save::BStage::native_start, save::BStatus::refused,
            "heat_blast_ui_site_refused", 0, {{"reason", ui_reason}});
    } else {
        const auto created = MH_CreateHook(reinterpret_cast<void*>(ui_target.address),
            reinterpret_cast<void*>(arsenal_ui_hook), reinterpret_cast<void**>(&original_arsenal_ui));
        const auto enabled = created == MH_OK ? MH_EnableHook(reinterpret_cast<void*>(ui_target.address)) : created;
        heat_blast_ui_trace.record(save::BStage::native_start,
            enabled == MH_OK ? save::BStatus::succeeded : save::BStatus::refused,
            enabled == MH_OK ? "heat_blast_ui_observer_installed" : "heat_blast_ui_hook_refused", 0,
            {{"native_error", enabled}});
    }

    menu::install(binding, stop);

    mastery_ready.store(false, std::memory_order_release);
    if (!inventory::available()) return;
    const auto mastery_deadline = GetTickCount64() + 10000;
    const Site mastery_sites[] = {
        {0x1631f90, "488d05c9c70603c3cccccccccccccccc488d05f9950603c3cccccccccccccccc", nullptr, nullptr},
        {0xfe3830, "4c8bc24885d2742b4863515833c085d27e21488b49504c8bca8bd00f1f440000", nullptr, nullptr},
        {0x164fc20, "488bc44889480855488d68a14881ecf000000048895820488970f0488978e84c", nullptr, nullptr},
        {0x164e9b0, "488bc44889480855488d68e84881ec10010000488958f0488970e8488978e04c", reinterpret_cast<void*>(upgrade_replay_hook), reinterpret_cast<void**>(&original_upgrade_replay)},
        {0x1651040, "4885d20f8450030000448844241853415541574883ec4048896c2460450fb6e9", reinterpret_cast<void*>(upgrade_activate_hook), reinterpret_cast<void**>(&original_upgrade_activate)},
    };
    for (const auto& s : mastery_sites) {
        const auto reason = s.offset == 0x1631f90 || s.offset == 0xfe3830 ?
            native::validate_leaf_target(memory, binding.image,
                make_target(image_base, s.offset, s.bytes),
                s.offset == 0x1631f90 ? 8 : 63, stop, mastery_deadline) :
            native::validate_target(memory, binding.image,
                make_target(image_base, s.offset, s.bytes), stop, mastery_deadline);
        if (reason) {
            const char* diagnostic = s.offset == 0x1631f90 ? "mastery_typeinfo_leaf_refused" :
                s.offset == 0xfe3830 ? "mastery_base_reader_refused" :
                s.offset == 0x164fc20 ? "mastery_apply_target_refused" :
                s.offset == 0x164e9b0 ? "mastery_replay_hook_refused" : "mastery_activation_hook_refused";
            installation_trace.record(save::BStage::native_start, save::BStatus::refused,
                diagnostic, 0, {{"rva", s.offset}, {"reason", reason}});
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
save::BSnapshot heat_blast_ui_diagnostics() { return heat_blast_ui_trace.snapshot(); }

} // namespace sentinel::arsenal
