#include "inventory.h"
#include "runes.h"
#include "special.h"
#include "native_target.h"
#include "save_session.h"
#include <atomic>
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace sentinel::inventory {
namespace {
std::atomic<bool> ready{false};
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;


uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(image_base + 0x69af70)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uint8_t read_capacity(uintptr_t p, uintptr_t inv, uintptr_t type, uintptr_t perk_type,
                      const char* family, unsigned index) {
    __try {
        const auto suit = p + 0x192f0;
        const auto list = *reinterpret_cast<const uintptr_t**>(suit + 8);
        const auto count = *reinterpret_cast<const int*>(suit + 0x10);
        if (count < 0 || count > 4096 || (count && !list)) return SC_INVENTORY_UNKNOWN_TIER;
        const char* host_path = "abilities/environmentsuit";
        const auto host_decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
            image_base + 0x17aa5d0)(type, host_path, 1);
        if (!host_decl) return SC_INVENTORY_UNKNOWN_TIER;
        const auto host_name = *reinterpret_cast<const char**>(host_decl + 8);
        if (!host_name || std::strcmp(host_name, host_path)) return SC_INVENTORY_UNKNOWN_TIER;
        const auto host = reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(
            image_base + 0x1690660)(inv, host_decl);
        if (!host) return SC_INVENTORY_UNKNOWN_TIER;
        uint32_t owned = 0, active = 0;
        for (unsigned stage = 0; stage < 4; ++stage) {
            char path[80]{};
            std::snprintf(path, sizeof(path), "perk/player/argent/%s_capacity_%u", family, stage);
            const auto decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
                image_base + 0x17aa5d0)(perk_type, path, 1);
            if (!decl) return SC_INVENTORY_UNKNOWN_TIER;
            const auto name = *reinterpret_cast<const char**>(decl + 8);
            if (!name || std::strcmp(name, path)) return SC_INVENTORY_UNKNOWN_TIER;
            for (int i = 0; i < count; ++i)
                if (list[i] == decl) { owned |= 1u << stage; break; }
            if (reinterpret_cast<uint8_t(*)(uintptr_t, uintptr_t)>(image_base + 0xfe37f0)(p + 0x3b40, decl))
                active |= 1u << stage;
        }
        return capacity_stage(owned, active,
            *reinterpret_cast<const int*>(suit + 0x2b8 + 4 * index),
            *reinterpret_cast<const int*>(host + 0x318 + 4 * index));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return SC_INVENTORY_UNKNOWN_TIER; }
}

uint8_t read_perk(uintptr_t p, uintptr_t type, const char* path) {
    __try {
        const auto decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
            image_base + 0x17aa5d0)(type, path, 1);
        if (!decl) return SC_INVENTORY_UNKNOWN_ITEM;
        const auto name = *reinterpret_cast<const char**>(decl + 8);
        if (!name || std::strcmp(name, path)) return SC_INVENTORY_UNKNOWN_ITEM;
        const auto component = p + 0x3b40;
        const auto count = *reinterpret_cast<const int*>(component + 0x58);
        if (count < 0 || count > 4096 || (count && !*reinterpret_cast<uintptr_t*>(component + 0x50)))
            return SC_INVENTORY_UNKNOWN_ITEM;
        return reinterpret_cast<uint8_t(*)(uintptr_t, uintptr_t)>(image_base + 0xfe3830)(component, decl) ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return SC_INVENTORY_UNKNOWN_ITEM; }
}

void observe_perk(SnapshotFacts& facts, uint32_t item, uint8_t owned) {
    if (owned > 1) return;
    const auto index = item >= 7770145 ? item - 7770145 + 61 : item - 7770058;
    const auto bit = 1ull << index;
    facts.perks_known |= bit;
    if (owned) facts.perks_owned |= bit;
}

bool read(void*, uintptr_t p, SnapshotFacts& facts) {
    __try {
        facts = {};
        facts.persistent_upgrades = SC_INVENTORY_UNKNOWN_MASK;
        const auto inv = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x763080)(p);
        if (!inv) return false;
        const auto count = *reinterpret_cast<int*>(inv + 8);
        const auto capacity = *reinterpret_cast<int*>(inv + 12);
        if (count < 0 || count > 4096 || capacity < count ||
            (count && !*reinterpret_cast<uintptr_t*>(inv))) return false;
        const auto type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1617cd0)();
        if (!type) return false;
        const char* const paths[] = {
            "weapon/player/shotgun", "weapon/player/double_barrel", "weapon/player/heavy_cannon",
            "weapon/player/chaingun", "weapon/player/plasma_rifle", "weapon/player/gauss_rifle",
            "weapon/player/rocket_launcher", "weapon/player/bfg", "weapon/player/chainsaw",
            "weapon/player/unmaykr"};
        uint32_t weapons = 0;
        for (unsigned i = 0; i < _countof(paths); ++i) {
            const auto decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
                image_base + 0x17aa5d0)(type, paths[i], 1);
            if (!decl) return true;
            const auto path = *reinterpret_cast<const char**>(decl + 8);
            if (!path || std::strncmp(path, paths[i], std::strlen(paths[i]) + 1)) return true;
            if (reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(image_base + 0x1690660)(inv, decl))
                weapons |= 1u << i;
        }
        facts.weapons = weapons;
        const char* flame = "weapon/player/equipment_flame_belch";
        const auto flame_decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
            image_base + 0x17aa5d0)(type, flame, 1);
        if (flame_decl) {
            const auto path = *reinterpret_cast<const char**>(flame_decl + 8);
            if (path && !std::strcmp(path, flame))
                facts.flame_belch = reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(
                    image_base + 0x1690660)(inv, flame_decl) ? 1 : 0;
        }
        const char* dash = "ability_dash";
        const auto dash_decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
            image_base + 0x17aa5d0)(type, dash, 1);
        const auto dash_type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1631d90)();
        if (dash_decl && dash_type) {
            const auto path = *reinterpret_cast<const char**>(dash_decl + 8);
            const auto get_type = *reinterpret_cast<uintptr_t*>(
                *reinterpret_cast<uintptr_t*>(dash_decl) + 0xb0);
            if (path && !std::strcmp(path, dash) && get_type &&
                reinterpret_cast<uintptr_t(*)(uintptr_t)>(get_type)(dash_decl) == dash_type) {
                const auto item = reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(
                    image_base + 0x1690660)(inv, dash_decl);
                // idAbility_Dash inventory count is independent of charges, cooldown and CanUse.
                const auto quantity = item ? *reinterpret_cast<int*>(item + 0x40) : 0;
                if (quantity >= 0) facts.dash = quantity > 0 ? 1 : 0;
            }
        }
        const char* throwables[] = {"throwable/player/frag_grenade", "throwable/player/ice_bomb"};
        uint8_t* observations[] = {&facts.frag_grenade, &facts.ice_bomb};
        for (unsigned i = 0; i < _countof(throwables); ++i) {
            const auto decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
                image_base + 0x17aa5d0)(type, throwables[i], 1);
            if (!decl) continue;
            const auto path = *reinterpret_cast<const char**>(decl + 8);
            if (path && !std::strcmp(path, throwables[i]))
                *observations[i] = reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(
                    image_base + 0x1690660)(inv, decl) ? 1 : 0;
        }
        const char* key = "map_objects/keycard/slayer_key";
        const auto key_decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
            image_base + 0x17aa5d0)(type, key, 1);
        if (key_decl) {
            const auto path = *reinterpret_cast<const char**>(key_decl + 8);
            if (path && !std::strcmp(path, key)) {
                const auto item = reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(
                    image_base + 0x1690660)(inv, key_decl);
                const auto quantity = item ? *reinterpret_cast<int*>(item + 0x40) : 0;
                if (quantity >= 0) facts.slayer_key = quantity > 0 ? 1 : 0;
            }
        }
        const auto perk_type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1631f90)();
        if (!perk_type) return true;
        // availability includes unequipped mods; active-perk state depends on selection
        const struct { uint32_t item; const char* path; } permanent[] = {
            {7770058, "perk/player/weapons/shotgun/pop_rocket"},
            {7770059, "perk/player/weapons/shotgun/secondary_full_auto"},
            {7770060, "perk/player/weapons/heavy_cannon/bolt_action"},
            {7770061, "perk/player/weapons/heavy_cannon/burst_detonate"},
            {7770062, "perk/player/weapons/plasma_rifle/secondary_aoe"},
            {7770064, "perk/player/weapons/plasma_rifle/secondary_microwave"},
            {7770071, "perk/player/weapons/rocket_launcher/detonate"},
            {7770073, "perk/player/weapons/rocket_launcher/lock_on"},
            {7770075, "perk/player/weapons/gauss_cannon/ballista"},
            {7770077, "perk/player/weapons/gauss_cannon/destroyer"},
            {7770079, "perk/player/weapons/chaingun/energy_shell"},
            {7770081, "perk/player/weapons/chaingun/turret"},
            {7770083, "perk/player/weapons/double_barrel/meat_hook"},
            {7770097, "perk/player/suit/fundamentals/ledge_grab_speed"},
            {7770098, "perk/player/suit/fundamentals/weapon_change_speed"},
            {7770099, "perk/player/suit/dash/dash_regen_delay"},
            {7770100, "perk/player/suit/dash/dash_gk_restore_dash"},
            {7770101, "perk/player/suit/exploration/automap_shows_map_station"},
            {7770102, "perk/player/suit/exploration/automap_shows_progression_items"},
            {7770103, "perk/player/suit/exploration/automap_increased_fog_radius"},
            {7770104, "perk/player/suit/exploration/dossier_shows_progression_items"},
            {7770106, "perk/player/suit/self_preservation/reduce_hazard_damage"},
            {7770107, "perk/player/suit/self_preservation/reduce_self_weapon_damage"},
            {7770108, "perk/player/suit/extermination/barrels_respawn"},
            {7770109, "perk/player/suit/extermination/barrels_drop_ammo"},
            {7770110, "perk/player/suit/powerup/powerup_duration"},
            {7770111, "perk/player/equipment/frag_reduce_cooldown"},
            {7770112, "perk/player/equipment/frag_concussive_blast"},
            {7770113, "perk/player/equipment/frag_cluster_bombs"},
            {7770114, "perk/player/equipment/frag_max_capacity"},
            {7770115, "perk/player/equipment/ice_reduce_cooldown"},
            {7770116, "perk/player/equipment/ice_extend_duration"},
            {7770117, "perk/player/equipment/ice_health_drops"},
            {7770118, "perk/player/equipment/ice_melee_shatter"},
        };
        for (const auto& perk : permanent)
            observe_perk(facts, perk.item, read_perk(p, perk_type, perk.path));
        runes::SnapshotFacts rune_facts{};
        if (runes::available() && runes::calls.read && runes::calls.read(runes::calls.context, p, rune_facts)) {
            const uint32_t normal[] = {7770085, 7770086, 7770087, 7770089, 7770090, 7770091, 7770093, 7770094, 7770095};
            for (unsigned i = 0; i < _countof(normal); ++i)
                observe_perk(facts, normal[i], (rune_facts.owned_normal >> i) & 1);
            for (unsigned i = 0; i < 3; ++i)
                observe_perk(facts, 7770145 + i, (rune_facts.owned_support >> i) & 1);
        }
        facts.health_tier = read_capacity(p, inv, type, perk_type, "health", 0);
        facts.armor_tier = read_capacity(p, inv, type, perk_type, "armor", 1);
        facts.ammo_tier = read_capacity(p, inv, type, perk_type, "ammo", 2);
        const char* const blood_punch[] = {
            "perk/player/blood_punch/base", "perk/player/blood_punch/area_of_effect",
            "perk/player/blood_punch/ai_charge_rate", "perk/player/blood_punch/max_charges"};
        uint32_t perks = 0;
        for (unsigned i = 0; i < _countof(blood_punch); ++i) {
            const auto decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
                image_base + 0x17aa5d0)(perk_type, blood_punch[i], 1);
            if (!decl) return true;
            const auto path = *reinterpret_cast<const char**>(decl + 8);
            if (!path || std::strncmp(path, blood_punch[i], std::strlen(blood_punch[i]) + 1)) return true;
            if (reinterpret_cast<uint8_t(*)(uintptr_t, uintptr_t)>(image_base + 0xfe37f0)(p + 0x3b40, decl))
                perks |= 1u << (SC_INV_BLOOD_PUNCH_SHIFT + i);
        }
        facts.persistent_upgrades = perks;
        facts.blood_punch = (perks & (1u << SC_INV_BLOOD_PUNCH_SHIFT)) ? 1 : 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t ensure_items(void*, uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t) { return 1; }
uint32_t set_capacity(void*, uintptr_t, uint8_t, uint8_t, uint8_t) { return 1; }

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

Calls calls{nullptr, player, read, ensure_items, set_capacity, refresh, bind_run_state};

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif
}

bool available() { return ready.load(std::memory_order_acquire); }

bool microwave_observed() {
    if (!available() || !save::session().routed()) return false;
    const auto p = player(nullptr);
    SnapshotFacts facts{};
    return p && read(nullptr, p, facts) && microwave_counter(facts);
}

bool stone_counter_observed() {
    if (!available() || !save::session().routed()) return false;
    const auto p = player(nullptr);
    if (!p) return false;
    SnapshotFacts facts{};
    if (read(nullptr, p, facts) && stone_counter(facts, false)) return true;
    special::SnapshotFacts equipment{};
    return special::calls.read && special::calls.read(special::calls.context, p, equipment) &&
        (equipment.known & SC_SPECIAL_KNOWN_HAMMER) && equipment.native_hammer;
}

bool armored_counter_observed() {
    if (!available() || !save::session().routed()) return false;
    const auto p = player(nullptr);
    SnapshotFacts facts{};
    return p && read(nullptr, p, facts) && armored_counter(facts);
}

bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}

uint8_t grenade_indicator_mask(uintptr_t p) {
    if (!available() || !p) return SC_INVENTORY_UNKNOWN_ITEM;
    __try {
        const auto inv = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x763080)(p);
        const auto type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1617cd0)();
        if (!inv || !type) return SC_INVENTORY_UNKNOWN_ITEM;
        const char* paths[] = {"throwable/player/frag_grenade", "throwable/player/ice_bomb"};
        uint8_t mask = 0;
        for (unsigned i = 0; i < 2; ++i) {
            const auto decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
                image_base + 0x17aa5d0)(type, paths[i], 1);
            if (!decl) return SC_INVENTORY_UNKNOWN_ITEM;
            const auto path = *reinterpret_cast<const char**>(decl + 8);
            if (!path || std::strcmp(path, paths[i])) return SC_INVENTORY_UNKNOWN_ITEM;
            if (reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(image_base + 0x1690660)(inv, decl))
                mask |= static_cast<uint8_t>(1u << i);
        }
        return mask;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return SC_INVENTORY_UNKNOWN_ITEM; }
}

void execute_native(const sc_inventory_request& request, sc_inventory_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_INV_UNAVAILABLE; return; }
    if (request.kind != SC_INV_OBSERVE) { out.outcome = SC_INV_UNAVAILABLE; return; }
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
    ready.store(false, std::memory_order_release);
    engine::LocalMemory memory;
    const struct { uint32_t rva; const char* bytes; } sites[] = {
        {0x763080, "488d81d8350000c3ccccccccccccccccf20f1081b8280000f20f11028b81c028"},
        {0x69af70, "488bc183fa0b77104863ca488b8cc8f81a0000e97851a70133c0c3cccccccccc"},
        {0x1617cd0, "488d0569210803c3cccccccccccccccc4883ec28ba33000000b9d8000000e84d"},
        {0x17aa5d0, "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448"},
        {0x1690660, "48895c240848896c2410488974241848897c242041564883ec2033ff488bea4c"},
        {0x1631f90, "488d05c9c70603c3cccccccccccccccc488d05f9950603c3cccccccccccccccc"},
        {0x1631d90, "488d05a9d00603c3cccccccccccccccc488d05f9d10603c3cccccccccccccccc"},
        {0x4a1870, "8b4140c3cccccccccccccccccccccccc40534883ec20488bd9488b4a104883c1"},
        {0xfe37f0, "4c8bc24885d2742b4863517033c085d27e21488b49684c8bca8bd00f1f440000"},
        {0xfe3830, "4c8bc24885d2742b4863515833c085d27e21488b49504c8bca8bd00f1f440000"},
        {0x1284720, "48895c24185556574154415541564157488dac2450c0ffffb8b0400000e85e76"},
        {0xfd8330, "4056574883ec58488b059a661d034833c44889442448488bf24889ac24880000"},
    };
    for (const auto& site : sites) {
        std::array<uint8_t, 32> expected{}, actual{};
        const auto digit = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
        for (size_t i = 0; i < expected.size(); ++i)
            expected[i] = static_cast<uint8_t>(digit(site.bytes[2 * i]) * 16 + digit(site.bytes[2 * i + 1]));
        if (!binding.image.contains(site.rva, expected.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0) ||
            memory.copy(image_base + site.rva, actual.data(), actual.size()).reason || actual != expected) return;
    }
    ready.store(true, std::memory_order_release);
}
}
