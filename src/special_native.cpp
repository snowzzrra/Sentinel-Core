#include "special.h"
#include "native_target.h"
#include "save_session.h"
#include "MinHook.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace sentinel::special {
namespace {

std::atomic<bool> ready{false};
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
constexpr uint32_t rva_perk_typeinfo = 0x1631f90;        // returns idDeclTypeInfo for perks
constexpr uint32_t rva_current_weapon = 0xbd7740;        // current idWeapon of the player
constexpr uint32_t rva_hud_earnings = 0xeea070;          // idHUD_MissionChallenge earnings append
constexpr uint32_t rva_hud_element_setup = 0xeeac40;     // idHUD_MissionChallenge construction
constexpr uint32_t rva_player = 0x69af70;

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
using CurrentWeapon = uintptr_t(*)(uintptr_t);
using EarningsAppend = void(*)(uintptr_t, const char*, const char*, uint32_t, uint64_t, uint32_t);
using HudElementSetup = char(*)(uintptr_t);

HudElementSetup original_hud_element_setup = nullptr;
std::atomic<uintptr_t> hud_element{0};
std::atomic<uintptr_t> hud_element_vtable{0};

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
    return reinterpret_cast<FindDecl>(image_base + rva_find_decl)(typeinfo, path, 1);
}

uintptr_t find_item(uintptr_t inventory, uintptr_t decl) {
    if (!inventory || !decl) return 0;
    return reinterpret_cast<FindItem>(image_base + rva_find_item)(inventory, decl);
}

uintptr_t give_item(uintptr_t inventory, uintptr_t p, uintptr_t decl, int count, uint8_t count_is_amount) {
    if (!inventory || !p || !decl || count < 1) return 0;
    // Receipt grants are silent acquisition only: no forced equip, no ammo,
    // and no resource initialization beyond what the engine performs on a
    // first legitimate acquisition.
    return reinterpret_cast<GiveItem>(image_base + rva_give_item)(
        inventory, p, decl, count, count_is_amount, 0, 1, 0);
}

uintptr_t find_perk(uintptr_t p, const char* target_name) {
    if (!p || !target_name) return 0;
    __try {
        const auto perk_list = *reinterpret_cast<const uintptr_t**>(p + 0x3b90);
        const auto perk_count = *reinterpret_cast<const int*>(p + 0x3b98);
        if (!perk_list || perk_count <= 0 || perk_count > 4000) return 0;
        for (int i = 0; i < perk_count; ++i) {
            const auto perk = perk_list[i];
            if (!perk) continue;
            const auto name = *reinterpret_cast<const char**>(perk + 8);
            if (name && std::strcmp(name, target_name) == 0) return perk;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

void unlock_perk(uintptr_t p, uintptr_t perk) {
    if (!p || !perk) return;
    reinterpret_cast<UnlockPerk>(image_base + rva_unlock_perk)(p + 0x3b40, perk, 0, 0, 0, 0);
}

bool valid_code_pointer(uintptr_t address) {
    return address >= image_base && address - image_base < image_size;
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
    if (!valid_code_pointer(equip)) return false;
    reinterpret_cast<void(*)(uintptr_t, uintptr_t)>(equip)(p, item);
    return true;
}

char hud_element_setup_detour(uintptr_t element) {
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

struct PersistedSelection {
    uint32_t selected = SC_SPECIAL_WEAPON_NONE;
    uintptr_t item = 0;
};

PersistedSelection selection_snapshot(uintptr_t p, uintptr_t inv, uintptr_t crucible_decl, uintptr_t hammer_decl) {
    PersistedSelection result{};
    const auto decl = current_weapon_decl(p);
    if (!decl) return result;
    if (crucible_decl && decl == crucible_decl) result.selected = SC_SPECIAL_WEAPON_CRUCIBLE;
    else if (hammer_decl && decl == hammer_decl) result.selected = SC_SPECIAL_WEAPON_HAMMER;
    else return result;
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
    return true;
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
        if (hammer_decl) {
            uint8_t perks = 0;
            for (const auto* path : HAMMER_PERK_PATHS)
                if (find_perk(p, path)) ++perks;
            facts.known |= SC_SPECIAL_KNOWN_HAMMER_PERKS;
            facts.native_hammer_perks = perks;
        }

        const auto active_decl = current_weapon_decl(p);
        if (active_decl) {
            uint8_t selected = SC_SPECIAL_WEAPON_NONE;
            if (crucible_decl && active_decl == crucible_decl) selected = SC_SPECIAL_WEAPON_CRUCIBLE;
            else if (hammer_decl && active_decl == hammer_decl) selected = SC_SPECIAL_WEAPON_HAMMER;
            facts.native_selected = selected;
            facts.known |= SC_SPECIAL_KNOWN_SELECTION;
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
        const auto crucible_decl = find_decl(CRUCIBLE_PATH);
        const auto hammer_decl = find_decl(HAMMER_PATH);
        const auto before = selection_snapshot(p, inv, crucible_decl, hammer_decl);

        uintptr_t decl = 0, item = 0;
        bool mutated = false;
        if (own_crucible) ensure_item(inv, p, CRUCIBLE_PATH, decl, item, mutated);
        if (own_hammer) ensure_item(inv, p, HAMMER_PATH, decl, item, mutated);

        if (own_hammer && hammer_tier >= SC_SPECIAL_HAMMER_TIER_UPGRADED) {
            for (const auto* path : HAMMER_PERK_PATHS) {
                if (find_perk(p, path)) continue;
                const auto perk_decl = find_perk_decl(path);
                if (!perk_decl) continue;
                unlock_perk(p, perk_decl);
                mutated = true;
            }
        }

        // An acquisition may intrinsically switch the active weapon. Restore the
        // player's prior valid selection instead of inheriting the acquisition.
        if (mutated && before.selected != SC_SPECIAL_WEAPON_NONE) {
            const auto after = selection_snapshot(p, inv, crucible_decl, hammer_decl);
            if (after.selected != before.selected && before.item)
                equip_item(p, before.item);
        }

        SnapshotFacts facts{};
        read(nullptr, p, facts);
        native_facts = facts;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t select(void*, uintptr_t p, uint32_t selected) {
    if (!p) return 1;
    if (selected != SC_SPECIAL_WEAPON_CRUCIBLE && selected != SC_SPECIAL_WEAPON_HAMMER) return 2;
    uint32_t error = 0;
    __try {
        const auto inv = inventory_of(p);
        if (!inv) return 3;
        const auto path = selected == SC_SPECIAL_WEAPON_CRUCIBLE ? CRUCIBLE_PATH : HAMMER_PATH;
        const auto decl = find_decl(path);
        if (!decl) return 4;
        auto item = find_item(inv, decl);
        if (!item) item = give_item(inv, p, decl, 1, 0);
        if (!item) return 5;
        if (!equip_item(p, item)) return 6;
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

        // Same ordinary-ammo contract as the established "give ammo" action:
        // every owned weapon's ammo pools are topped up, except the special
        // weapons whose spendable resources are explicitly excluded. Ammo items
        // clamp to the player's native capacity; no weapon is granted here.
        for (int i = 0; i < static_cast<int>(count_fn(inv)); ++i) {
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
                    give_item(inv, p, ammo_decl, 999, 1);
                }
            };
            give_list(decl);
            const auto nested = *reinterpret_cast<uintptr_t*>(decl + 0x7f0);
            if (nested) give_list(nested);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

bool present(void*, uintptr_t, uint32_t balance, uint32_t flags, uint32_t used) {
    const auto element = hud_element.load(std::memory_order_acquire);
    const auto expected_vtable = hud_element_vtable.load(std::memory_order_acquire);
    if (!element || !expected_vtable) return false;
    char reward[64]{};
    if (!(flags & SC_SPECIAL_REFILL_AUTHORITATIVE) || !(flags & SC_SPECIAL_REFILL_CONNECTED)) {
        std::snprintf(reward, sizeof(reward), "UNAVAILABLE");
    } else if (!(flags & SC_SPECIAL_REFILL_BALANCE_KNOWN)) {
        std::snprintf(reward, sizeof(reward), "-- CHARGES [F9]");
    } else if (used) {
        std::snprintf(reward, sizeof(reward), "%u LEFT [F9]", balance);
    } else {
        std::snprintf(reward, sizeof(reward), "%u CHARGE%s [F9]", balance, balance == 1 ? "" : "S");
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
// Input seam. The configured local key state is written by the launcher at the
// existing ammo-refill hotkey path; an absent file means the F9 default.
std::atomic<int> configured_key{VK_F9};
std::atomic<uint64_t> key_state_checked{0};
bool key_was_down = false, key_latched = false;
uint64_t key_release_at = 0;
constexpr uint64_t key_release_stabilize_ms = 120;

int token_to_vk(const char* token) {
    if (!token || !token[0]) return 0;
    char upper[16]{};
    size_t n = 0;
    for (; token[n] && n < sizeof(upper) - 1; ++n) {
        const auto c = token[n];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        upper[n] = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    upper[n] = 0;
    if (n >= 2 && upper[0] == 'F') {
        int value = 0; bool digits = true;
        for (size_t i = 1; i < n; ++i) {
            if (upper[i] < '0' || upper[i] > '9') { digits = false; break; }
            value = value * 10 + (upper[i] - '0');
        }
        if (digits && value >= 1 && value <= 12) return VK_F1 + value - 1;
    }
    if (n == 1) {
        if (upper[0] >= 'A' && upper[0] <= 'Z') return upper[0];
        if (upper[0] >= '0' && upper[0] <= '9') return upper[0];
    }
    if (std::strcmp(upper, "SPACE") == 0) return VK_SPACE;
    if (std::strcmp(upper, "TAB") == 0) return VK_TAB;
    if (std::strcmp(upper, "INSERT") == 0) return VK_INSERT;
    if (std::strcmp(upper, "DELETE") == 0) return VK_DELETE;
    if (std::strcmp(upper, "HOME") == 0) return VK_HOME;
    if (std::strcmp(upper, "END") == 0) return VK_END;
    if (std::strcmp(upper, "PAGEUP") == 0 || std::strcmp(upper, "PGUP") == 0) return VK_PRIOR;
    if (std::strcmp(upper, "PAGEDOWN") == 0 || std::strcmp(upper, "PGDN") == 0) return VK_NEXT;
    return 0;
}

void refresh_configured_key(uint64_t now) {
    if (now - key_state_checked.load(std::memory_order_relaxed) < 250) return;
    key_state_checked.store(now, std::memory_order_relaxed);
    char token[24]{};
    FILE* file = nullptr;
    if (fopen_s(&file, "base\\ap_queue\\ammo_refill_hotkey.state", "r") == 0 && file) {
        char first[24]{}, second[24]{};
        const auto read = fscanf_s(file, "%23s %23s", first, static_cast<unsigned>(_countof(first)),
                                   second, static_cast<unsigned>(_countof(second)));
        std::fclose(file);
        if (read >= 1) {
            const char* value = first;
            if (std::strcmp(first, "AP_AMMO_REFILL_HOTKEY_V1") == 0) value = read >= 2 ? second : "";
            std::snprintf(token, sizeof(token), "%s", value);
        }
    }
    if (!token[0]) { configured_key.store(VK_F9, std::memory_order_relaxed); return; }
    const auto vk = token_to_vk(token);
    configured_key.store(vk, std::memory_order_relaxed);
}
} // namespace

void poll_input(uintptr_t p, bool safe_gameplay) {
    if (!ready.load(std::memory_order_acquire)) return;
    const auto now = GetTickCount64();
    refresh_configured_key(now);
    const auto vk = configured_key.load(std::memory_order_relaxed);
    if (!vk) {
        key_was_down = false; key_latched = false; key_release_at = 0;
        return;
    }
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (key_latched) {
        key_was_down = down;
        if (down) { key_release_at = 0; return; }
        if (!key_release_at) { key_release_at = now; return; }
        if (now - key_release_at < key_release_stabilize_ms) return;
        key_latched = false; key_release_at = 0; key_was_down = false;
        return;
    }
    const bool rising = down && !key_was_down;
    key_was_down = down;
    if (!rising) return;
    key_latched = true;
    key_release_at = 0;
    // Unsafe gameplay or an absent player creates no request at all: there is
    // no delayed or hidden refill to execute later.
    if (!safe_gameplay || !p) return;
    create_refill_request(now);
}

bool available() { return ready.load(std::memory_order_acquire); }

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
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;

    engine::LocalMemory memory;
    struct Site { uint32_t offset; const char* bytes; };
    const Site sites[] = {
        {rva_find_decl, "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448"},
        {rva_find_item, "48895c240848896c2410488974241848897c242041564883ec2033ff488bea4c"},
        {rva_give_item, "40555356574154415541564157488d6c24f94881ecb8000000488b05e8ccb102"},
        {rva_item_at, "4883ec2885d2784f3b51087d4a48895c24204863da48c1e3054803198b03488d"},
        {rva_item_count, "8b4108c3cccccccccccccccccccccccc48896c2418574883ec204863790833ed"},
        {rva_unlock_perk, "4885d20f84c903000044884c2420448844241848894c24085356415441564883"},
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
        if (!binding.image.contains(s.offset, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0) ||
            memory.copy(target.address, actual.data(), actual.size()).reason || actual != target.bytes) return;
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
    if (!native::validate_target(memory, binding.image, hud_target, stop, deadline) &&
        MH_CreateHook(reinterpret_cast<void*>(image_base + rva_hud_element_setup),
                      reinterpret_cast<void*>(hud_element_setup_detour),
                      reinterpret_cast<void**>(&original_hud_element_setup)) == MH_OK &&
        MH_EnableHook(reinterpret_cast<void*>(image_base + rva_hud_element_setup)) == MH_OK) {
        // Hook armed; the element pointer arrives when the HUD is constructed.
    }
    ready.store(true, std::memory_order_release);
}

} // namespace sentinel::special
