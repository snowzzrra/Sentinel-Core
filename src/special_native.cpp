#include "special.h"
#include "native_target.h"
#include "native_runtime.h"
#include "local_controls.h"
#include "save_session.h"
#include "MinHook.h"
#include "hde/hde64.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <intrin.h>
#include <cmath>

namespace sentinel::special {
namespace {

std::atomic<bool> ready{false};
save::BTrace installation_trace, input_trace, route_trace;
SRWLOCK directory_lock = SRWLOCK_INIT;
std::wstring controls_directory;
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;
SnapshotFacts native_facts{};

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif

// Verified against the supported Steam image (9809708c). All addresses are RVAs.
constexpr uint32_t rva_inventory_typeinfo = 0x1617cd0;   // returns idDeclTypeInfo for inventory items
constexpr uint32_t rva_find_decl = 0x17aa5d0;            // FindDecl(typeinfo, path, flags)
constexpr uint32_t rva_find_item = 0x1690660;            // idInventoryCollection::FindItem(inventory, decl)
constexpr uint32_t rva_give_item = 0x1691cd0;            // idInventoryCollection::GiveItem(...)
constexpr uint32_t rva_item_count = 0x398510;            // idInventoryCollection::Num()
constexpr uint32_t rva_item_at = 0x1691450;              // idInventoryCollection::GetItem(index)
constexpr uint32_t rva_unlock_perk = 0xfe2500;           // perk registration/unlock
constexpr uint32_t rva_activate_perk = 0xfe19b0;         // idPerkComponent::ActivatePerk
constexpr uint32_t rva_active_perk = 0xfe37f0;           // exact active-perk reader
constexpr uint32_t rva_perk_typeinfo = 0x1631f90;        // returns idDeclTypeInfo for perks
constexpr uint32_t rva_current_weapon = 0xbd7740;        // current idWeapon of the player
constexpr uint32_t rva_hud_earnings = 0xeea070;          // idHUD_MissionChallenge earnings append
constexpr uint32_t rva_hud_element_setup = 0xeeac40;     // idHUD_MissionChallenge construction
constexpr uint32_t rva_player = 0x69af70;
constexpr uint32_t rva_crucible_resolver = 0x145d640;
constexpr uint32_t rva_input_down = 0x146b850;
constexpr uint32_t rva_hammer_input_return = 0x14433f0;
constexpr uint32_t rva_weapon_cast = 0x21107c0;
constexpr uint64_t special_button = UINT64_C(0x400000000);
using CrucibleResolver = bool(*)(uintptr_t);
using InputDown = uint64_t(*)(uintptr_t, uint64_t);
CrucibleResolver original_crucible_resolver = nullptr;
InputDown original_input_down = nullptr;
std::atomic<bool> route_ready{false};
std::atomic<DWORD> route_thread{0};
uintptr_t route_player = 0;
char route_namespace[65]{};
uint32_t route_requested = SC_SPECIAL_WEAPON_NONE;
bool special_down = false;
constexpr uint32_t rva_equipment_upgrade_vtable = 0x2e04ba0;
constexpr uintptr_t perk_component_offset = 0x3b40;
constexpr uintptr_t equipment_upgrade_offset = 0x26568;
constexpr uintptr_t hammer_loot_modifier_offset = 0x2d0;

const char* const CRUCIBLE_PATH = "weapon/player/crucible";
const char* const HAMMER_PATH = "weapon/player/hammer";
const char* const CRUCIBLE_AMMO_PATH = "ammo/sharedammopool/crucible";
const char* const HAMMER_PERK_PATHS[2] = {
    "perk/player/weapons/hammer/ammo_drops_upgraded",
    "perk/player/weapons/hammer/armor_and_health_drops_upgraded"
};

using FindDecl = uintptr_t(*)(uintptr_t, const char*, int);
using FindItem = uintptr_t(*)(uintptr_t, uintptr_t);
using GiveItem = uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, int, uint8_t, uint8_t, uint8_t, uint8_t);
using ItemCount = uint32_t(*)(uintptr_t);
using ItemAt = uintptr_t(*)(uintptr_t, int);
using UnlockPerk = void(*)(uintptr_t, uintptr_t, uint8_t, uint8_t, uintptr_t, uint8_t);
using ActivatePerk = void(*)(uintptr_t, uintptr_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
using ActivePerk = uint8_t(*)(uintptr_t, uintptr_t);
using CurrentWeapon = uintptr_t(*)(uintptr_t);
using EarningsAppend = void(*)(uintptr_t, const char*, const char*, uint32_t, uint64_t, uint32_t);
using HudElementSetup = char(*)(uintptr_t);

HudElementSetup original_hud_element_setup = nullptr;
std::atomic<uintptr_t> hud_element{0};
std::atomic<uintptr_t> hud_element_vtable{0};
std::atomic<uintptr_t> hud_player{0};

uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(image_base + rva_player)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t inventory_of(uintptr_t p) {
    if (!p) return 0;
    const auto vtable = *reinterpret_cast<uintptr_t**>(p);
    if (!vtable) return 0;
    const auto get_inventory = reinterpret_cast<uintptr_t(*)(uintptr_t)>(vtable[0x540 / 8]);
    return get_inventory ? get_inventory(p) : 0;
}

const char* decl_path(uintptr_t decl) {
    if (!decl) return nullptr;
    __try { return *reinterpret_cast<const char**>(decl + 8); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

uintptr_t find_decl(const char* path) {
    if (!path) return 0;
    const auto typeinfo = reinterpret_cast<uintptr_t(*)()>(image_base + rva_inventory_typeinfo)();
    if (!typeinfo) return 0;
    return reinterpret_cast<FindDecl>(image_base + rva_find_decl)(typeinfo, path, 1);
}

uintptr_t find_perk_decl(const char* path) {
    if (!path) return 0;
    const auto typeinfo = reinterpret_cast<uintptr_t(*)()>(image_base + rva_perk_typeinfo)();
    if (!typeinfo) return 0;
    const auto decl = reinterpret_cast<FindDecl>(image_base + rva_find_decl)(typeinfo, path, 1);
    const auto resolved = decl_path(decl);
    return resolved && std::strcmp(resolved, path) == 0 ? decl : 0;
}

bool hammer_loot_modifier(uintptr_t p, bool& effective) {
    const auto handler = p + equipment_upgrade_offset;
    if (*reinterpret_cast<uintptr_t*>(handler) != image_base + rva_equipment_upgrade_vtable) return false;
    effective = *reinterpret_cast<uintptr_t*>(handler + hammer_loot_modifier_offset) != 0;
    return true;
}

bool hammer_perks(uintptr_t p, uintptr_t (&decls)[2], uint8_t& effective_count) {
    for (size_t i = 0; i < 2; ++i) {
        decls[i] = find_perk_decl(HAMMER_PERK_PATHS[i]);
        if (!decls[i]) return false;
    }
    bool modifier_effective = false;
    if (!hammer_loot_modifier(p, modifier_effective)) return false;
    const auto component = p + perk_component_offset;
    const auto active = reinterpret_cast<ActivePerk>(image_base + rva_active_perk);
    uint8_t active_count = 0;
    for (const auto decl : decls) active_count += active(component, decl) ? 1 : 0;
    effective_count = modifier_effective ? active_count : 0;
    return true;
}

uintptr_t find_item(uintptr_t inventory, uintptr_t decl) {
    if (!inventory || !decl) return 0;
    return reinterpret_cast<FindItem>(image_base + rva_find_item)(inventory, decl);
}

// Match the native resolver's typed presence checks, independently of charges.
bool route_ownership(uintptr_t p, bool& crucible, bool& hammer) {
    __try {
        const auto c = *reinterpret_cast<uintptr_t*>(p + 0x4d198);
        const auto h = *reinterpret_cast<uintptr_t*>(p + 0x4d1d8);
        if (!c || !h) return false;
        const auto table = *reinterpret_cast<uintptr_t*>(p);
        const auto inv = reinterpret_cast<uintptr_t(*)(uintptr_t)>(
            *reinterpret_cast<uintptr_t*>(table + 0x538))(p);
        if (!inv) return false;
        const auto cast = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + rva_weapon_cast);
        crucible = cast(find_item(inv, c)) != 0;
        hammer = cast(find_item(inv, h)) != 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool route_scope(uintptr_t p) {
    return route_ready.load(std::memory_order_acquire) &&
        GetCurrentThreadId() == route_thread.load(std::memory_order_acquire) && p && p == route_player &&
        native::gameplay_admitted() && p == player(nullptr) &&
        !std::memcmp(route_namespace, save::session().namespace_id().c_str(), 65);
}

uint32_t applied_mode(bool crucible, bool hammer) {
    if (!route_requested) route_requested = hammer ? SC_SPECIAL_WEAPON_HAMMER :
        crucible ? SC_SPECIAL_WEAPON_CRUCIBLE : SC_SPECIAL_WEAPON_NONE;
    if (crucible && hammer) return route_requested;
    return crucible ? SC_SPECIAL_WEAPON_CRUCIBLE : hammer ? SC_SPECIAL_WEAPON_HAMMER : SC_SPECIAL_WEAPON_NONE;
}

bool crucible_resolver_detour(uintptr_t p) {
    if (!route_scope(p)) return original_crucible_resolver(p);
    bool c = false, h = false;
    if (!route_ownership(p, c, h)) return original_crucible_resolver(p);
    // Resource/action guards remain in the original callers and ActivateCrucible.
    return c && applied_mode(c, h) == SC_SPECIAL_WEAPON_CRUCIBLE;
}

struct RouteObservation {
    uintptr_t held = 0;
    uint32_t known = 0, charge = UINT32_MAX, meter_milli = UINT32_MAX, action = UINT32_MAX, error = 0;
};
RouteObservation observe_route(uintptr_t p);

uint64_t filter_hammer_input(uintptr_t caller, uint64_t button, uint64_t raw) {
    if (caller != image_base + rva_hammer_input_return || button != special_button ||
        GetCurrentThreadId() != route_thread.load(std::memory_order_acquire) || !route_scope(route_player))
        return raw;
    bool c = false, h = false;
    if (!route_ownership(route_player, c, h)) return raw;
    const bool down = static_cast<uint8_t>(raw) != 0;
    const auto mode = applied_mode(c, h);
    const bool allowed = h && mode == SC_SPECIAL_WEAPON_HAMMER;
    if (down != special_down) {
        static uint64_t input_sequence = 0;
        if (down) ++input_sequence;
        const auto observed = observe_route(route_player);
        route_trace.record(save::BStage::special_input, save::BStatus::succeeded, "core_policy_input_gate", GetTickCount64(),
            {{"down", down}, {"requested", route_requested}, {"applied", mode}, {"hammer_allowed", allowed},
             {"crucible_owned", c}, {"hammer_owned", h}, {"caller_rva", rva_hammer_input_return},
             {"input_sequence", input_sequence}, {"native_known", observed.known},
             {"held_decl", observed.held}, {"crucible_charge", observed.charge},
             {"hammer_meter_milli", observed.meter_milli}, {"hands_action", observed.action},
             {"observation_error", observed.error}, {"native_handler_return_known", 0}});
    }
    special_down = down;
    // The query precedes Hammer meter/latch/animation. Preserve every other
    // caller and the upper return bits; do not queue or synthesize an input.
    return allowed ? raw : raw & ~UINT64_C(0xff);
}

uint64_t input_down_detour(uintptr_t input, uint64_t button) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    return filter_hammer_input(caller, button, original_input_down(input, button));
}

uintptr_t give_item(uintptr_t inventory, uintptr_t p, uintptr_t decl, int count, uint8_t count_is_amount) {
    if (!inventory || !p || !decl || count < 1) return 0;
    // Native acquisition has intrinsic effects. Flags alone do not establish
    // ammo/selection preservation; callers must independently verify them.
    return reinterpret_cast<GiveItem>(image_base + rva_give_item)(
        inventory, p, decl, count, count_is_amount, 0, 1, 0);
}

uintptr_t current_weapon_decl(uintptr_t p) {
    if (!p) return 0;
    const auto weapon = reinterpret_cast<CurrentWeapon>(image_base + rva_current_weapon)(p);
    if (!weapon) return 0;
    __try { return *reinterpret_cast<uintptr_t*>(weapon + 0x38); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uint32_t item_count(uintptr_t item) {
    if (!item) return 0;
    const auto vtable = *reinterpret_cast<uintptr_t**>(item);
    if (!vtable) return 0;
    const auto count = reinterpret_cast<int(*)(uintptr_t)>(vtable[0xd0 / 8]);
    if (!count) return 0;
    const auto value = count(item);
    return value > 0 ? static_cast<uint32_t>(value) : 0;
}

bool equip_item(uintptr_t p, uintptr_t item) {
    if (!p || !item) return false;
    const auto vtable = *reinterpret_cast<uintptr_t**>(p);
    if (!vtable) return false;
    const auto equip = vtable[0x1588 / 8];
    if (equip != image_base + 0x1432100) return false;
    return reinterpret_cast<char(*)(uintptr_t, uintptr_t)>(equip)(p, item) != 0;
}

char hud_element_setup_detour(uintptr_t element) {
    hud_player.store(player(nullptr), std::memory_order_release);
    if (element) {
        __try {
            const auto vtable = *reinterpret_cast<uintptr_t*>(element);
            if (vtable) {
                hud_element_vtable.store(vtable, std::memory_order_release);
                hud_element.store(element, std::memory_order_release);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return original_hud_element_setup ? original_hud_element_setup(element) : 0;
}

// The weapon actually in hands before a physical acquisition; separate from the
// preferred Special selection owned by the generic model. `item` is zero when
// the declaration does not belong to the current valid inventory, so the caller
// fails closed instead of restoring a stale or fabricated handle.
struct HeldWeapon {
    uintptr_t decl = 0;
    uintptr_t item = 0;
};

HeldWeapon held_weapon_snapshot(uintptr_t p, uintptr_t inv) {
    HeldWeapon result{};
    const auto decl = current_weapon_decl(p);
    if (!decl) return result;
    result.decl = decl;
    result.item = find_item(inv, decl);
    return result;
}

bool ensure_item(uintptr_t inv, uintptr_t p, const char* path, uintptr_t& decl_out, uintptr_t& item_out, bool& mutated) {
    decl_out = find_decl(path);
    if (!decl_out) return false;
    item_out = find_item(inv, decl_out);
    if (!item_out) {
        item_out = give_item(inv, p, decl_out, 1, 0);
        if (item_out) mutated = true;
    }
    return item_out != 0;
}
} // namespace

bool read(void*, uintptr_t p, SnapshotFacts& facts) {
    if (!p) return false;
    __try {
        // Observations are fresh: a fact that cannot be read on this sample is
        // reported unknown instead of being inherited from an older sample.
        facts = {};
        const auto inv = inventory_of(p);
        if (!inv) return false;

        const auto crucible_decl = find_decl(CRUCIBLE_PATH);
        const auto hammer_decl = find_decl(HAMMER_PATH);
        if (crucible_decl) {
            facts.known |= SC_SPECIAL_KNOWN_CRUCIBLE;
            facts.native_crucible = find_item(inv, crucible_decl) ? 1 : 0;
        }
        if (hammer_decl) {
            facts.known |= SC_SPECIAL_KNOWN_HAMMER;
            facts.native_hammer = find_item(inv, hammer_decl) ? 1 : 0;
        }
        uintptr_t perk_decls[2]{};
        uint8_t effective_perks = 0;
        if (hammer_perks(p, perk_decls, effective_perks)) {
            facts.known |= SC_SPECIAL_KNOWN_HAMMER_PERKS;
            facts.native_hammer_perks = effective_perks;
        }

        facts.held_weapon_decl = current_weapon_decl(p);
        bool route_c = false, route_h = false;
        if (route_scope(p) && route_ownership(p, route_c, route_h)) {
            facts.native_selected = static_cast<uint8_t>(applied_mode(route_c, route_h));
            facts.known |= SC_SPECIAL_KNOWN_SELECTION;
            facts.selection_policy = true;
        }

        if (crucible_decl && facts.native_crucible) {
            const auto ammo_decl = find_decl(CRUCIBLE_AMMO_PATH);
            if (ammo_decl) {
                const auto ammo_item = find_item(inv, ammo_decl);
                if (ammo_item) {
                    facts.known |= SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE;
                    facts.crucible_charge = item_count(ammo_item);
                    facts.crucible_charge_max = UINT32_MAX; // natively owned capacity is not guessed
                }
            }
        }

        native_facts = facts;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint32_t ensure(void*, uintptr_t p, uint32_t own_crucible, uint32_t own_hammer, uint32_t hammer_tier) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const auto inv = inventory_of(p);
        if (!inv) return 2;
        const bool upgraded = own_hammer && hammer_tier >= SC_SPECIAL_HAMMER_TIER_UPGRADED;
        uintptr_t perk_decls[2]{};
        uint8_t effective_perks = 0;
        if (upgraded && !hammer_perks(p, perk_decls, effective_perks)) return ERROR_NOT_SUPPORTED;
        const auto before = held_weapon_snapshot(p, inv);

        uintptr_t decl = 0, item = 0;
        bool mutated = false;
        if (own_crucible && !ensure_item(inv, p, CRUCIBLE_PATH, decl, item, mutated)) return 3;
        if (own_hammer && !ensure_item(inv, p, HAMMER_PATH, decl, item, mutated)) return 4;

        if (upgraded) {
            const auto component = p + perk_component_offset;
            const auto active = reinterpret_cast<ActivePerk>(image_base + rva_active_perk);
            const auto unlock = reinterpret_cast<UnlockPerk>(image_base + rva_unlock_perk);
            const auto activate = reinterpret_cast<ActivatePerk>(image_base + rva_activate_perk);
            for (const auto perk_decl : perk_decls) {
                if (active(component, perk_decl)) continue;
                unlock(component, perk_decl, 0, 0, 0, 0);
                activate(component, perk_decl, 1, 0, 0, 0, 1);
            }
        }

        // An acquisition may intrinsically switch the active weapon. Restore the
        // exact prior weapon in hands, ordinary or Special, instead of inheriting
        // the acquisition; a prior item that cannot be freshly validated fails
        // closed instead of guessing.
        if (mutated) {
            const auto decision = acquisition_restore(before.decl, before.item, current_weapon_decl(p));
            if (decision == AcquisitionRestore::fail_closed) return 6;
            if (decision == AcquisitionRestore::equip_prior && !equip_item(p, before.item)) return 6;
            if (before.decl && current_weapon_decl(p) != before.decl) return 6;
        }

        SnapshotFacts facts{};
        if (!read(nullptr, p, facts)) return 7;
        native_facts = facts;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t select(void*, uintptr_t p, uint32_t selected) {
    if (!p) return 1;
    if (selected != SC_SPECIAL_WEAPON_CRUCIBLE && selected != SC_SPECIAL_WEAPON_HAMMER) return 2;
    uint32_t error = 0;
    __try {
        if (!route_scope(p)) return ERROR_NOT_SUPPORTED;
        bool c = false, h = false;
        if (!route_ownership(p, c, h)) return ERROR_READ_FAULT;
        if ((selected == SC_SPECIAL_WEAPON_CRUCIBLE && !c) || (selected == SC_SPECIAL_WEAPON_HAMMER && !h))
            return ERROR_NOT_FOUND;
        if (route_requested == selected) return 0;
        const auto held = current_weapon_decl(p);
        const auto action = *reinterpret_cast<uint32_t*>(p + 0x15f98);
        if (special_down || !held || held == find_decl(CRUCIBLE_PATH) || held == find_decl(HAMMER_PATH) || action > 2) {
            route_trace.record(save::BStage::special_toggle, save::BStatus::refused, "selection_busy", GetTickCount64(),
                {{"requested", selected}, {"applied", applied_mode(c, h)}, {"held_decl", held},
                 {"hands_action", action}, {"special_down", special_down}, {"error", ERROR_BUSY}});
            return ERROR_BUSY;
        }
        const auto before = applied_mode(c, h);
        route_requested = selected;
        route_trace.record(save::BStage::special_toggle, save::BStatus::succeeded, "core_policy_applied", GetTickCount64(),
            {{"requested", selected}, {"before", before}, {"applied", applied_mode(c, h)},
             {"held_before", held}, {"held_after", current_weapon_decl(p)}, {"error", 0}});
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t refill(void*, uintptr_t p) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const auto inv = inventory_of(p);
        if (!inv) return 2;
        const auto count_fn = reinterpret_cast<ItemCount>(image_base + rva_item_count);
        const auto at_fn = reinterpret_cast<ItemAt>(image_base + rva_item_at);

        uint32_t attempted = 0, confirmed = 0;
        const auto inventory_count = count_fn(inv);
        if (inventory_count > 4096) return 3;
        // Same ordinary-ammo contract as the established "give ammo" action:
        // every owned weapon's ammo pools are topped up, except the special
        // weapons whose spendable resources are explicitly excluded. Ammo items
        // use the native item writer; capacity clamping still requires retail proof.
        for (int i = 0; i < static_cast<int>(inventory_count); ++i) {
            const auto item = at_fn(inv, i);
            if (!item) continue;
            const auto decl = *reinterpret_cast<uintptr_t*>(item + 0x38);
            const auto path = decl_path(decl);
            if (!decl || !path) continue;
            if (std::strncmp(path, "weapon/player/", 14) != 0) continue;
            if (std::strcmp(path, CRUCIBLE_PATH) == 0 || std::strcmp(path, HAMMER_PATH) == 0) continue;

            const auto give_list = [&](uintptr_t list_decl) {
                if (!list_decl) return;
                const auto ammo_count = *reinterpret_cast<int*>(list_decl + 0x810);
                const auto ammo_list = *reinterpret_cast<uintptr_t*>(list_decl + 0x808);
                if (!ammo_list || ammo_count <= 0 || ammo_count > 64) return;
                for (int j = 0; j < ammo_count; ++j) {
                    const auto ammo_decl = *reinterpret_cast<uintptr_t*>(ammo_list + static_cast<size_t>(j) * 0x28);
                    if (!ammo_decl) continue;
                    const auto ammo_path = decl_path(ammo_decl);
                    if (ammo_path && std::strstr(ammo_path, "crucible")) continue;
                    ++attempted;
                    const auto before_item = find_item(inv, ammo_decl);
                    const auto before_count = before_item ? item_count(before_item) : 0;
                    const auto granted = give_item(inv, p, ammo_decl, 999, 1);
                    const auto after_item = find_item(inv, ammo_decl);
                    if (granted && after_item && item_count(after_item) >= before_count && item_count(after_item) > 0)
                        ++confirmed;
                }
            };
            give_list(decl);
            const auto nested = *reinterpret_cast<uintptr_t*>(decl + 0x7f0);
            if (nested) give_list(nested);
        }
        if (!attempted || confirmed != attempted) return 4;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

std::atomic<unsigned> configured_keys{VK_F9};

namespace {
RouteObservation observe_route(uintptr_t p) {
    RouteObservation observed{};
    __try {
        SnapshotFacts facts{};
        if (!read(nullptr, p, facts)) return observed;
        observed.held = facts.held_weapon_decl;
        observed.known = facts.known;
        if (facts.known & SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE) observed.charge = facts.crucible_charge;
        observed.action = *reinterpret_cast<uint32_t*>(p + 0x15f98);
        const auto component = p + 0x26568;
        if (*reinterpret_cast<uintptr_t*>(component + 0x2b8)) {
            const auto meter = reinterpret_cast<float(*)(uintptr_t)>(image_base + 0x1643260)(component) * 1000;
            if (std::isfinite(meter) && meter >= 0 && static_cast<double>(meter) <= UINT32_MAX)
                observed.meter_milli = static_cast<uint32_t>(meter);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { observed.error = GetExceptionCode(); }
    return observed;
}
}

bool present(void*, uintptr_t, uint32_t balance, uint32_t flags, uint32_t used) {
    const auto element = hud_element.load(std::memory_order_acquire);
    const auto expected_vtable = hud_element_vtable.load(std::memory_order_acquire);
    if (!element || !expected_vtable) return false;
    char reward[96]{}, key[32]{};
    const int vk = (configured_keys.load(std::memory_order_relaxed) & 0xff);
    if (!vk) std::snprintf(key, sizeof(key), "UNBOUND");
    else if (vk >= VK_F1 && vk <= VK_F12) std::snprintf(key, sizeof(key), "F%d", vk - VK_F1 + 1);
    else if (!GetKeyNameTextA(static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16), key, sizeof(key)))
        std::snprintf(key, sizeof(key), "VK %u", static_cast<unsigned>(vk));
    if (!(flags & SC_SPECIAL_REFILL_AUTHORITATIVE) || !(flags & SC_SPECIAL_REFILL_CONNECTED)) {
        std::snprintf(reward, sizeof(reward), "UNAVAILABLE");
    } else if (!(flags & SC_SPECIAL_REFILL_BALANCE_KNOWN)) {
        std::snprintf(reward, sizeof(reward), "-- CHARGES [%s]", key);
    } else if (used) {
        std::snprintf(reward, sizeof(reward), "USED; BALANCE PENDING");
    } else {
        std::snprintf(reward, sizeof(reward), "%u CHARGE%s [%s]", balance, balance == 1 ? "" : "S", key);
    }
    __try {
        // The captured element must still be the same live class; a destroyed and
        // reused address is rejected instead of receiving a foreign write.
        if (*reinterpret_cast<uintptr_t*>(element) != expected_vtable) {
            hud_element.store(0, std::memory_order_release);
            hud_element_vtable.store(0, std::memory_order_release);
            return false;
        }
        reinterpret_cast<EarningsAppend>(image_base + rva_hud_earnings)(
            element, "AMMO REFILL", reward, 3000, 0, 0);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void present_selection(uintptr_t p) {
    SnapshotFacts facts{};
    if (!read(nullptr, p, facts) || !facts.selection_policy || !facts.native_selected) return;
    const auto element = hud_element.load(std::memory_order_acquire);
    const auto table = hud_element_vtable.load(std::memory_order_acquire);
    if (!element || !table || hud_player.load(std::memory_order_acquire) != p) return;
    char text[96]{}, resource[32] = "RESOURCE UNKNOWN", key[32]{};
    const auto vk = static_cast<int>((configured_keys.load(std::memory_order_relaxed) >> 8) & 0xff);
    if (facts.native_crucible && facts.native_hammer && vk) {
        if (vk >= VK_F1 && vk <= VK_F12) std::snprintf(key, sizeof(key), " [F%d TOGGLE]", vk - VK_F1 + 1);
        else {
            char label[16]{};
            if (GetKeyNameTextA(static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16), label, sizeof(label)))
                std::snprintf(key, sizeof(key), " [%s TOGGLE]", label);
        }
    }
    if (facts.native_selected == SC_SPECIAL_WEAPON_CRUCIBLE && (facts.known & SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE))
        std::snprintf(resource, sizeof(resource), "%u CHARGES", facts.crucible_charge);
    __try {
        if (*reinterpret_cast<uintptr_t*>(element) != table) return;
        if (facts.native_selected == SC_SPECIAL_WEAPON_HAMMER &&
            *reinterpret_cast<uintptr_t*>(p + equipment_upgrade_offset + 0x2b8)) {
            const auto meter = reinterpret_cast<float(*)(uintptr_t)>(image_base + 0x1643260)(p + equipment_upgrade_offset);
            if (std::isfinite(meter) && meter >= 0)
                std::snprintf(resource, sizeof(resource), "%.0f%% METER", static_cast<double>(meter * 100));
        }
        std::snprintf(text, sizeof(text), "%s%s", resource, key);
        reinterpret_cast<EarningsAppend>(image_base + rva_hud_earnings)(element,
            facts.native_selected == SC_SPECIAL_WEAPON_CRUCIBLE ? "CRUCIBLE SELECTED" : "HAMMER SELECTED", text, 3000, 0, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void bind_run_state(void*, uintptr_t p, uint32_t own_crucible, uint32_t own_hammer,
                    uint32_t hammer_tier, uint32_t selected) {
    if (!p) return;
    __try {
        ensure(nullptr, p, own_crucible, own_hammer, hammer_tier);
        if (selected == SC_SPECIAL_WEAPON_CRUCIBLE || selected == SC_SPECIAL_WEAPON_HAMMER)
            select(nullptr, p, selected);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

Calls calls{nullptr, player, read, ensure, select, refill, present, bind_run_state};

namespace {
std::atomic<uint64_t> key_state_checked{0};
struct KeyLatch {
    bool down = false, latched = true;
    uint64_t released_at = 0;
    int key = -1;

    bool press(int vk, bool pressed, uint64_t now, bool enabled) {
        if (key != vk || !enabled) {
            key = vk; latched = true; released_at = 0; down = pressed;
            return false;
        }
        if (!vk) { down = false; return false; }
        if (latched) {
            down = pressed;
            if (pressed) released_at = 0;
            else if (!released_at) released_at = now;
            else if (now - released_at >= 120) { latched = false; released_at = 0; }
            return false;
        }
        const bool rising = pressed && !down;
        down = pressed;
        if (rising) { latched = true; released_at = 0; }
        return rising;
    }
};
KeyLatch input_latches[2];
} // namespace

void refresh_input_config() {
    const auto now = GetTickCount64();
    if (now - key_state_checked.load(std::memory_order_relaxed) < 250) return;
    key_state_checked.store(now, std::memory_order_relaxed);
    wchar_t absolute[MAX_PATH]{};
    const auto length = GetFullPathNameW(L"base\\ap_queue", MAX_PATH, absolute, nullptr);
    const bool path_valid = length && length < MAX_PATH;
    AcquireSRWLockExclusive(&directory_lock);
    controls_directory = path_valid ? absolute : L"";
    ReleaseSRWLockExclusive(&directory_lock);
    const auto keys = path_valid ? controls::read(absolute) : controls::Bindings{};
    // A shared key cannot dispatch two actions; conflicting files disable both.
    const unsigned packed = !path_valid || keys.conflict() ? 0u :
        static_cast<unsigned>(keys.keys[0] | (keys.keys[1] << 8));
    configured_keys.store(packed, std::memory_order_relaxed);
    input_trace.record(save::BStage::profile_read, save::BStatus::succeeded, "control_bindings", 0,
        {{"path_valid", path_valid}, {"refill_vk", keys.keys[0]}, {"toggle_vk", keys.keys[1]},
         {"invalid", keys.invalid}, {"conflict", keys.conflict()}});
}

void poll_input(uintptr_t p, bool safe_gameplay) {
    if (!safe_gameplay || !p) {
        hud_element.store(0, std::memory_order_release);
        hud_element_vtable.store(0, std::memory_order_release);
        hud_player.store(0, std::memory_order_release);
    }
    route_thread = GetCurrentThreadId();
    route_player = safe_gameplay ? p : 0;
    if (p && safe_gameplay) {
        const auto& id = save::session().namespace_id();
        if (std::memcmp(route_namespace, id.c_str(), 65)) {
            std::memcpy(route_namespace, id.c_str(), 65);
            route_requested = SC_SPECIAL_WEAPON_NONE;
            special_down = false;
        }
    }
    static bool observed_down[2]{};
    static uint32_t attempts[2]{}, dispatches[2]{}, gate_refusals[2]{}, admission_refusals[2]{};
    const auto count = [](uint32_t& value) { if (value != UINT32_MAX) ++value; };
    const bool installed = ready.load(std::memory_order_acquire);
    const bool selection_route = route_ready.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    const auto keys = configured_keys.load(std::memory_order_relaxed);
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    const bool enabled = installed && safe_gameplay && p && foreground_pid == GetCurrentProcessId();
    input_trace.record(save::BStage::special_input, enabled ? save::BStatus::succeeded : save::BStatus::pending,
        "input_gate", 0, {{"ready", installed}, {"safe_gameplay", safe_gameplay}, {"player", p != 0},
                         {"foreground", foreground_pid == GetCurrentProcessId()}, {"keys", keys}});
    for (unsigned i = 0; i < 2; ++i) {
        const auto vk = static_cast<int>((keys >> (i * 8)) & 0xff);
        const bool down = vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool attempted = down && !observed_down[i];
        const bool action_enabled = enabled && (!i || selection_route);
        observed_down[i] = down;
        if (attempted) { count(attempts[i]); if (!action_enabled) count(gate_refusals[i]); }
        const bool pressed = input_latches[i].press(vk, down, now, action_enabled);
        if (pressed) count(dispatches[i]);
        input_trace.record(i ? save::BStage::special_toggle : save::BStage::profile_choice,
            attempted && !action_enabled ? save::BStatus::refused : pressed ? save::BStatus::entered : save::BStatus::pending, "input_latch", 0,
            {{"vk", vk}, {"down", down}, {"enabled", action_enabled}, {"route_ready", selection_route},
             {"latched", input_latches[i].latched},
             {"dispatched", pressed}, {"attempts", attempts[i]}, {"dispatches", dispatches[i]},
             {"gate_refusals", gate_refusals[i]}, {"admission_refusals", admission_refusals[i]}});
        if (!pressed) continue;
        // Native reconstruction callbacks may invalidate the outer tick scope.
        const bool admitted = native::gameplay_admitted();
        if (!admitted) count(admission_refusals[i]);
        save::session().btrace.record(save::BStage::special_input,
            admitted ? save::BStatus::entered : save::BStatus::refused,
            admitted ? "local_control_admitted" : "local_control_not_admitted", now,
            {{"action", i}, {"vk", vk}, {"safe_gameplay", safe_gameplay},
             {"player", p != 0}, {"foreground", foreground_pid == GetCurrentProcessId()},
             {"gameplay_admitted", admitted}});
        if (!admitted) continue;
        if (!i) create_refill_request(now);
        else {
            const auto result = toggle_local(save::session().namespace_id().c_str(), calls);
            const auto known = SC_SPECIAL_KNOWN_CRUCIBLE | SC_SPECIAL_KNOWN_HAMMER;
            const bool selected = result.kind == SC_SPECIAL_SELECT &&
                result.outcome == SC_SPECIAL_OUTCOME_OK;
            if (selected) present_selection(p);
            const char* predicate = selected ? "local_toggle_selected" :
                result.kind != SC_SPECIAL_SELECT && result.outcome != SC_SPECIAL_OUTCOME_OK
                    ? "local_toggle_observe_failed" :
                result.kind != SC_SPECIAL_SELECT && (result.native_state_known & known) != known
                    ? "local_toggle_observe_incomplete" :
                result.kind != SC_SPECIAL_SELECT ? "local_toggle_no_owned_weapon" :
                "local_toggle_select_failed";
            save::session().btrace.record(save::BStage::special_toggle,
                selected ? save::BStatus::succeeded : save::BStatus::refused,
                predicate, now,
                {{"kind", result.kind}, {"outcome", result.outcome},
                 {"native_known", result.native_state_known},
                 {"native_crucible", result.native_crucible},
                 {"native_hammer", result.native_hammer},
                 {"selected", result.selected}, {"native_selected", result.native_selected},
                 {"native_error", result.native_exception}});
        }
    }
}

bool available() { return ready.load(std::memory_order_acquire); }
bool selection_route_available() { return route_ready.load(std::memory_order_acquire); }
save::BSnapshot installation_diagnostics() { return installation_trace.snapshot(); }
save::BSnapshot input_diagnostics() { return input_trace.snapshot(); }
save::BSnapshot route_diagnostics() { return route_trace.snapshot(); }
std::wstring input_directory() {
    AcquireSRWLockShared(&directory_lock);
    auto result = controls_directory;
    ReleaseSRWLockShared(&directory_lock);
    return result;
}

bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}

void execute_native(const sc_special_request& request, sc_special_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_SPECIAL_OUTCOME_UNAVAILABLE; return; }
    out.flags |= SC_SPECIAL_FLAG_SHARED_STATE_BOUND;
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
    installation_trace.record(save::BStage::native_start, save::BStatus::entered, "validating");
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;

    engine::LocalMemory memory;
    struct Site { uint32_t offset; const char* bytes; };
    const Site sites[] = {
        {0x1432100, "48895c241855565741544155415641574881ecb0000000488b05bac8d7024833"},
        {rva_find_decl, "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448"},
        {rva_find_item, "48895c240848896c2410488974241848897c242041564883ec2033ff488bea4c"},
        {rva_give_item, "40555356574154415541564157488d6c24f94881ecb8000000488b05e8ccb102"},
        {rva_item_at, "4883ec2885d2784f3b51087d4a48895c24204863da48c1e3054803198b03488d"},
        {rva_item_count, "8b4108c3cccccccccccccccccccccccc48896c2418574883ec204863790833ed"},
        {rva_unlock_perk, "4885d20f84c903000044884c2420448844241848894c24085356415441564883"},
        {rva_activate_perk, "44884c24204488442418488954241048894c2408555357415541564157488d6c"},
        {rva_active_perk, "4c8bc24885d2742b4863517033c085d27e21488b49684c8bca8bd00f1f440000"},
        {rva_perk_typeinfo, "488d05c9c70603c3cccccccccccccccc488d05f9950603c3cccccccccccccccc"},
        {rva_current_weapon, "40534883ec20488b01488bd9ff90b0000000488b13488bcb4885c07412ff92b0"},
        {rva_hud_earnings, "48895c240848896c2410488974241848897c242041564883ec20488db9700600"},
        {rva_player, "488bc183fa0b77104863ca488b8cc8f81a0000e97851a70133c0c3cccccccccc"},
        {rva_inventory_typeinfo, "488d0569210803c3cccccccccccccccc4883ec28ba33000000b9d8000000e84d"},
    };
    for (const auto& s : sites) {
        native::Target target{};
        target.address = image_base + s.offset;
        const auto digit = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
        for (size_t n = 0; n < target.bytes.size(); ++n)
            target.bytes[n] = static_cast<uint8_t>(digit(s.bytes[n * 2]) * 16 + digit(s.bytes[n * 2 + 1]));
        std::array<uint8_t, 32> actual{};
        const bool section = binding.image.contains(s.offset, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0);
        const auto read = memory.copy(target.address, actual.data(), actual.size());
        uint64_t expected_words[4]{}, actual_words[4]{};
        std::memcpy(expected_words, target.bytes.data(), 32);
        std::memcpy(actual_words, actual.data(), 32);
        const bool valid = section && !read.reason && actual == target.bytes;
        installation_trace.record(save::BStage::native_start, valid ? save::BStatus::entered : save::BStatus::refused,
            valid ? "site_validated" : "site_refused", 0,
            {{"rva", s.offset}, {"section", section}, {"read_reason", read.reason}, {"read_error", read.error},
             {"expected0", expected_words[0]}, {"expected1", expected_words[1]},
             {"expected2", expected_words[2]}, {"expected3", expected_words[3]},
             {"observed0", actual_words[0]}, {"observed1", actual_words[1]},
             {"observed2", actual_words[2]}, {"observed3", actual_words[3]}});
        if (!valid) return;
    }

    constexpr uint32_t expected_vtable_slots[] = {0x1641b80, 0x16466a0, 0x1647be0, 0x355140};
    std::array<uintptr_t, std::size(expected_vtable_slots)> actual_vtable{};
    if (!binding.image.contains(rva_equipment_upgrade_vtable, sizeof(actual_vtable), IMAGE_SCN_MEM_READ, 0) ||
        memory.copy(image_base + rva_equipment_upgrade_vtable, actual_vtable.data(), sizeof(actual_vtable)).reason)
    {
        installation_trace.record(save::BStage::root_layout, save::BStatus::refused, "vtable_unreadable", 0,
                                  {{"rva", rva_equipment_upgrade_vtable}});
        return;
    }
    for (size_t i = 0; i < actual_vtable.size(); ++i)
        if (actual_vtable[i] != image_base + expected_vtable_slots[i]) {
            installation_trace.record(save::BStage::root_layout, save::BStatus::refused, "vtable_mismatch", 0,
                {{"rva", rva_equipment_upgrade_vtable}, {"slot", i}, {"expected", image_base + expected_vtable_slots[i]},
                 {"observed", actual_vtable[i]}});
            return;
        }

    enum class RouteSiteKind : uint32_t { function_entry = 1, exact_leaf = 2, call_window = 3 };
    struct RouteSite { uint32_t offset; const char* bytes; RouteSiteKind kind; uint32_t owner; size_t length; };
    const RouteSite route_sites[] = {
        {rva_crucible_resolver, "48895c2408574883ec20488bb9d8d10400488bd94885ff7513488d15206b9601",
            RouteSiteKind::function_entry, rva_crucible_resolver, 32},
        {rva_input_down, "80b9a100000000740933c04885c20f95c0c3488b81a80000004885c20f95c0c3",
            RouteSiteKind::exact_leaf, 0, 32},
        {0x14433de, "48ba0000000004000000498bcee86084020084c0744f488d8e68650200e860fe",
            RouteSiteKind::call_window, 0x1442c14, rva_hammer_input_return - 0x14433de},
        {rva_weapon_cast, "40534883ec20488bd94885c97427488b01ff50104885c0741c8b40683b05a6cb",
            RouteSiteKind::function_entry, rva_weapon_cast, 32},
        {0x1643260, "488b81b80200004885c075040f57c0c3f30f108194040000f30f5e8090020000",
            RouteSiteKind::exact_leaf, 0, 33},
    };
    bool route_valid = true;
    const auto route_deadline = GetTickCount64() + 10000;
    size_t site_index = 0;
    for (const auto& site : route_sites) {
        native::Target target{};
        target.address = image_base + site.offset;
        const auto hex = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
        for (size_t i = 0; i < target.bytes.size(); ++i)
            target.bytes[i] = static_cast<uint8_t>(hex(site.bytes[2*i])*16 + hex(site.bytes[2*i+1]));
        native::ValidationDetail detail{};
        DWORD64 unwind_base = 0;
        const auto unwind = RtlLookupFunctionEntry(target.address, &unwind_base, nullptr);
        const uint32_t owner = unwind && unwind_base == image_base ? unwind->BeginAddress : 0;
        uint32_t error = SC_NATIVE_NONE, observed_call = 0;
        const char* predicate = "route_site_qualified";
        if (site.kind == RouteSiteKind::function_entry) {
            error = native::validate_target(memory, binding.image, target, stop, route_deadline, &detail);
            if (error == SC_NATIVE_TARGET_BYTES) predicate = "route_entry_bytes";
            else if (error == SC_NATIVE_TARGET_BOUNDARY) predicate = "route_entry_boundary";
            else if (error == SC_NATIVE_TARGET_NOT_UNIQUE) predicate = "route_entry_not_unique";
            else if (error) predicate = "route_entry_read_or_budget";
        } else if (site.kind == RouteSiteKind::exact_leaf) {
            error = native::validate_leaf_target(memory, binding.image, target, site.length, stop, route_deadline, &detail);
            if (error == SC_NATIVE_TARGET_BYTES) predicate = "route_leaf_bytes";
            else if (error == SC_NATIVE_TARGET_BOUNDARY) predicate = owner ? "route_leaf_unwind_owner" : "route_leaf_boundary";
            else if (error == SC_NATIVE_TARGET_NOT_UNIQUE) predicate = "route_leaf_not_unique";
            else if (error) predicate = "route_leaf_read_or_budget";
        } else {
            std::array<uint8_t, 32> actual{};
            const bool section = binding.image.contains(site.offset, actual.size(),
                IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE);
            engine::ReadResult read{};
            if (section) read = memory.copy(target.address, actual.data(), actual.size());
            else { read.reason = SC_REASON_READ_FAILED; read.error = ERROR_NOACCESS; }
            detail.read_attempted = section; detail.read = read; detail.byte_count = 32;
            detail.expected = target.bytes; detail.actual = actual;
            if (!section || read.reason) { error = SC_NATIVE_READ_FAILED; predicate = "route_window_read"; }
            else if (owner != site.owner || !native::function_window(memory, binding.image,
                         image_base + site.owner, target.address, site.length, &detail)) {
                error = SC_NATIVE_TARGET_BOUNDARY; predicate = "route_window_owner";
            } else {
                size_t offset = 0, last = 0;
                while (offset < site.length) {
                    last = offset;
                    hde64s instruction{};
                    const auto size = hde64_disasm(actual.data() + offset, &instruction);
                    if (!size || instruction.flags & F_ERROR || offset + size > site.length) break;
                    offset += size;
                }
                if (offset != site.length || last + 5 != site.length || actual[last] != 0xe8) {
                    error = SC_NATIVE_TARGET_BOUNDARY; predicate = "route_window_instructions";
                } else if (site.offset + site.length != rva_hammer_input_return) {
                    error = SC_NATIVE_TARGET_BOUNDARY; predicate = "route_window_return";
                } else {
                    int32_t relative = 0; std::memcpy(&relative, actual.data() + last + 1, sizeof(relative));
                    observed_call = static_cast<uint32_t>(site.offset + site.length + relative);
                    if (observed_call != rva_input_down) {
                        error = SC_NATIVE_TARGET_BOUNDARY; predicate = "route_window_call_target";
                    } else if (actual != target.bytes) {
                        error = SC_NATIVE_TARGET_BYTES; predicate = "route_window_bytes";
                    }
                }
            }
        }
        uint64_t expected0 = 0, actual0 = 0;
        std::memcpy(&expected0, detail.expected.data(), sizeof(expected0));
        std::memcpy(&actual0, detail.actual.data(), sizeof(actual0));
        installation_trace.record(save::BStage::special_toggle,
            error ? save::BStatus::refused : save::BStatus::entered, predicate, 0,
            {{"rva", site.offset}, {"site", site_index}, {"error", error},
             {"kind", static_cast<uint32_t>(site.kind)}, {"owner_rva", owner}, {"expected_owner", site.owner},
             {"read_attempted", detail.read_attempted}, {"read_reason", detail.read.reason},
             {"read_error", detail.read.error}, {"window_offset", detail.window_offset},
             {"byte_count", detail.byte_count}, {"collision_rva", detail.collision_rva},
             {"expected0", expected0}, {"actual0", actual0},
             {"expected_call", rva_input_down}, {"observed_call", observed_call}});
        if (error) { route_valid = false; break; }
        ++site_index;
    }
    if (route_valid) {
        const auto resolver = reinterpret_cast<void*>(image_base + rva_crucible_resolver);
        const auto input = reinterpret_cast<void*>(image_base + rva_input_down);
        const auto create_c = MH_CreateHook(resolver, reinterpret_cast<void*>(crucible_resolver_detour),
                                           reinterpret_cast<void**>(&original_crucible_resolver));
        const auto create_h = create_c == MH_OK ? MH_CreateHook(input, reinterpret_cast<void*>(input_down_detour),
                                           reinterpret_cast<void**>(&original_input_down)) : MH_UNKNOWN;
        const auto queue_c = create_h == MH_OK ? MH_QueueEnableHook(resolver) : MH_UNKNOWN;
        const auto queue_h = queue_c == MH_OK ? MH_QueueEnableHook(input) : MH_UNKNOWN;
        const auto enabled = queue_h == MH_OK ? MH_ApplyQueued() : MH_UNKNOWN;
        const bool installed = enabled == MH_OK;
        if (!installed) {
            if (create_h == MH_OK) { MH_DisableHook(input); MH_RemoveHook(input); }
            if (create_c == MH_OK) { MH_DisableHook(resolver); MH_RemoveHook(resolver); }
        }
        route_ready.store(installed, std::memory_order_release);
        installation_trace.record(save::BStage::special_toggle, installed ? save::BStatus::succeeded : save::BStatus::refused,
            "joint_route_install", 0, {{"create_c", create_c}, {"create_h", create_h},
                {"queue_c", queue_c}, {"queue_h", queue_h}, {"enabled", enabled}, {"ready", installed}});
    }

    // HUD capture is optional presentation: a failed hook validation or install
    // must not disable ownership/selection/refill, it only disables the on-screen
    // Ammo Refill projection.
    const auto deadline = GetTickCount64() + 10000;
    native::Target hud_target{};
    hud_target.address = image_base + rva_hud_element_setup;
    const char* hud_bytes = "4057b860400000e854119801482be0488b05823d2c034833c448898424404000";
    const auto digit = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t n = 0; n < hud_target.bytes.size(); ++n)
        hud_target.bytes[n] = static_cast<uint8_t>(digit(hud_bytes[n * 2]) * 16 + digit(hud_bytes[n * 2 + 1]));
    const auto hud_validation = native::validate_target(memory, binding.image, hud_target, stop, deadline);
    const auto hud_created = hud_validation ? MH_UNKNOWN :
        MH_CreateHook(reinterpret_cast<void*>(image_base + rva_hud_element_setup),
                      reinterpret_cast<void*>(hud_element_setup_detour),
                      reinterpret_cast<void**>(&original_hud_element_setup));
    const auto hud_enabled = hud_created == MH_OK ?
        MH_EnableHook(reinterpret_cast<void*>(image_base + rva_hud_element_setup)) : MH_UNKNOWN;
    installation_trace.record(save::BStage::profile_publish,
        hud_enabled == MH_OK ? save::BStatus::succeeded : save::BStatus::refused, "optional_hud_hook", 0,
        {{"rva", rva_hud_element_setup}, {"validation", hud_validation},
         {"create_status", static_cast<uint64_t>(hud_created)}, {"enable_status", static_cast<uint64_t>(hud_enabled)}});
    ready.store(true, std::memory_order_release);
    installation_trace.record(save::BStage::native_start, save::BStatus::succeeded, "ownership_ready", 0,
        {{"selection_route_ready", route_ready.load(std::memory_order_acquire)},
         {"hud_hook", hud_enabled == MH_OK}});
}

} // namespace sentinel::special
