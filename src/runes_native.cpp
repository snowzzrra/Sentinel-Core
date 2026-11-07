#include "engine/builds/build_descriptor.h"
#include "runes.h"
#include "native_target.h"
#include "save_session.h"
#include <atomic>
#include <cstring>
#include <algorithm>

namespace sentinel::runes {
namespace {

std::atomic<bool> ready{false};
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;
SnapshotFacts native_facts{};

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif

const char* const NORMAL_RUNE_PATHS[9] = {
    "perk/player/runes/glory_kill_speed",                  /* 0: Savagery (7770085) */
    "perk/player/runes/glory_kill_dash",                   /* 1: Seek and Destroy (7770086) */
    "perk/player/runes/speed_boost_on_glory_kill",         /* 2: Blood Fueled (7770087) */
    "perk/player/runes/double_jump_air_control",          /* 3: Air Control (7770089) */
    "perk/player/runes/modify_enemy_stagger_duration",     /* 4: Dazed and Confused (7770090) */
    "perk/player/runes/activate_focus_on_death_blow",      /* 5: Saving Throw (7770091) */
    "perk/player/runes/target_strike",                     /* 6: Chrono Strike (7770093) */
    "perk/player/runes/decrease_equipment_recharge",       /* 7: Equipment Fiend (7770094) */
    "perk/player/runes/blood_punch_loot_on_damage"         /* 8: Punch and Reave (7770095) */
};

const char* const SUPPORT_RUNE_PATHS[3] = {
    "perk/player/runes/dlc/weakpoint_concussive_blast",             /* 0: Break Blast (7770145) */
    "perk/player/runes/dlc/blood_punch_low_health_bonus_damage",    /* 1: Desperate Punch (7770146) */
    "perk/player/runes/dlc/extra_life_refund"                      /* 2: Take Back (7770147) */
};

const char* const CRYSTAL_PAIR_PATHS[6] = {
    "perk/player/equipment/flame_reduce_cooldown",             /* 0: Quickdraw Belch (H>=1, A>=1) */
    "perk/player/suit/fundamentals/increase_pickup_radius",   /* 1: Loot Magnet (A>=2, Am>=3) */
    "perk/player/equipment/flame_extend_duration",            /* 2: Napalm Belch (H>=2, Am>=1) */
    "perk/player/suit/self_preservation/overhealth",          /* 3: Health for Blood (H>=4, A>=3) */
    "perk/player/equipment/flame_more_loot",                  /* 4: Belch Armor Boost (H>=3, Am>=2) */
    "perk/player/suit/self_preservation/overarmor"            /* 5: Armor for Blood (A>=4, Am>=4) */
};

uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(image_base + 0x69af70)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t find_perk(uintptr_t p, const char* target_name) {
    if (!p || !target_name) return 0;
    __try {
        const auto type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1631f90)();
        if (!type) return 0;
        const auto perk = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(image_base + 0x17aa5d0)(type, target_name, 0);
        if (!perk) return 0;
        const auto name = *reinterpret_cast<const char**>(perk + 8);
        if (name && std::strcmp(name, target_name) == 0) return perk;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

int rune_index(uintptr_t perk, const char* const* paths, int count) {
    if (!perk) return -1;
    const auto name = *reinterpret_cast<const char**>(perk + 8);
    if (!name) return -1;
    for (int i = 0; i < count; ++i)
        if (!std::strcmp(name, paths[i])) return i;
    return -1;
}

bool read_registered_runes(uintptr_t manager, unsigned offset, int limit, const char* const* paths,
                           int path_count, uint32_t& owned, int& count) {
    const auto list = *reinterpret_cast<const uintptr_t**>(manager + offset);
    count = *reinterpret_cast<const int*>(manager + offset + 8);
    if (count < 0 || count > limit || (count && !list)) return false;
    for (int i = 0; i < count; ++i) {
        if (!list[i]) return false;
        const auto index = rune_index(list[i], paths, path_count);
        if (index >= 0) owned |= 1u << index;
    }
    return true;
}

bool read(void*, uintptr_t p, SnapshotFacts& facts) {
    if (!p) return false;
    __try {
        const uintptr_t rm = p + 0x19168;
        facts = {};
        facts.derived_pairs = native_facts.derived_pairs;

        int reg_count = 0, support_count = 0;
        if (!read_registered_runes(rm, 0x50, 64, NORMAL_RUNE_PATHS, 9, facts.owned_normal, reg_count))
            return false;
        if (!read_registered_runes(rm, 0x68, 16, SUPPORT_RUNE_PATHS, 3, facts.owned_support, support_count))
            return false;
        for (int slot = 0; slot < 3; ++slot) {
            const auto perk = *reinterpret_cast<const uintptr_t*>(rm + 0x80 + slot * 8);
            const auto index = rune_index(perk, NORMAL_RUNE_PATHS, 9);
            if (index >= 0) facts.selected_slots[slot] = static_cast<int8_t>(index);
        }
        const auto support = *reinterpret_cast<const uintptr_t*>(rm + 0x98);
        const auto index = rune_index(support, SUPPORT_RUNE_PATHS, 3);
        if (index >= 0) facts.selected_support = static_cast<int8_t>(index);

        facts.unlocked_slots = 0;
        const auto req0 = *reinterpret_cast<const int*>(rm + 0x40);
        const auto req1 = *reinterpret_cast<const int*>(rm + 0x44);
        const auto req2 = *reinterpret_cast<const int*>(rm + 0x48);
        if (reg_count >= req0) facts.unlocked_slots |= (1u << 0);
        if (reg_count >= req1) facts.unlocked_slots |= (1u << 1);
        if (reg_count >= req2) facts.unlocked_slots |= (1u << 2);

        native_facts = facts;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint32_t append_registered_rune(uintptr_t manager, uintptr_t perk, int limit, int count, int capacity,
                                uintptr_t* list) {
    const auto assure_size = reinterpret_cast<char (*)(uintptr_t)>(image_base + 0x357040);
    if (count >= limit) return 3;
    if (count >= capacity && assure_size) {
        if (!assure_size(manager)) return 4;
        list = *reinterpret_cast<uintptr_t**>(manager);
    }
    capacity = *reinterpret_cast<int*>(manager + 12);
    if (!list || *reinterpret_cast<int*>(manager + 8) != count || capacity <= count) return 4;
    list[count] = perk;
    *reinterpret_cast<int*>(manager + 8) = count + 1;
    return 0;
}

uint32_t register_rune(uintptr_t manager, uintptr_t perk, int limit, bool& found) {
    const auto list = *reinterpret_cast<uintptr_t**>(manager);
    const auto count = *reinterpret_cast<int*>(manager + 8);
    const auto capacity = *reinterpret_cast<int*>(manager + 12);
    if (count < 0 || count > limit || capacity < count || (count && !list)) return 3;
    found = false;
    for (int i = 0; i < count; ++i) {
        if (list[i] == perk) {
            found = true;
            break;
        }
    }
    return found ? 0 : append_registered_rune(manager, perk, limit, count, capacity, list);
}

uint32_t ensure_rune_list(uintptr_t p, uint32_t mask, const char* const* paths, int path_count,
                          unsigned offset, int limit, uint32_t& owned) {
    const uintptr_t manager = p + 0x19168 + offset;
    const auto unlock_perk =
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, char, char, uintptr_t, char)>(image_base + 0xfe2500);
    const auto level_event = reinterpret_cast<void (*)(uintptr_t, uint64_t)>(image_base + 0x13ef6a0);
    for (int i = 0; i < path_count; ++i) {
        if (!(mask & (1u << i))) continue;
        const auto perk = find_perk(p, paths[i]);
        if (!perk) return 2;
        bool found = false;
        const auto reason = register_rune(manager, perk, limit, found);
        if (reason) return reason;
        if (unlock_perk) unlock_perk(p + 0x3b40, perk, 0, 0, 0, 0);
        if (!found && level_event) level_event(p, 0x457);
        owned |= 1u << i;
    }
    return 0;
}

uint32_t ensure_normal_runes(void*, uintptr_t p, uint32_t mask) {
    if (!p) return 1;
    __try {
        return ensure_rune_list(p, mask, NORMAL_RUNE_PATHS, 9, 0x50, 64, native_facts.owned_normal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

uint32_t ensure_support_runes(void*, uintptr_t p, uint32_t mask) {
    if (!p) return 1;
    __try {
        return ensure_rune_list(p, mask, SUPPORT_RUNE_PATHS, 3, 0x68, 16, native_facts.owned_support);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

uint32_t select_normal_rune(void*, uintptr_t p, uint8_t slot_idx, int8_t rune_idx) {
    if (!p || slot_idx >= 3) return 1;
    uint32_t error = 0;
    __try {
        const uintptr_t rm = p + 0x19168;
        const auto unequip_rune = reinterpret_cast<void(*)(uintptr_t, int, char)>(image_base + 0x127aa30);
        const auto equip_rune = reinterpret_cast<uint64_t(*)(uintptr_t, uintptr_t, uint32_t*, char, char)>(image_base + 0x1279be0);

        if (rune_idx < 0) {
            if (unequip_rune) unequip_rune(rm, slot_idx, 0);
            native_facts.selected_slots[slot_idx] = -1;
        } else if (rune_idx < 9) {
            const auto perk = find_perk(p, NORMAL_RUNE_PATHS[rune_idx]);
            if (!perk) return 2;
            if (equip_rune) {
                uint32_t slot = slot_idx;
                equip_rune(rm, perk, &slot, 0, 0);
                native_facts.selected_slots[slot_idx] = rune_idx;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t select_support_rune(void*, uintptr_t p, int8_t support_idx) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const uintptr_t rm = p + 0x19168;
        const auto unequip_rune = reinterpret_cast<void(*)(uintptr_t, int, char)>(image_base + 0x127aa30);
        const auto equip_support = reinterpret_cast<void(*)(uintptr_t, uintptr_t, char)>(image_base + 0x1279eb0);

        if (support_idx < 0) {
            if (unequip_rune) unequip_rune(rm, 3, 0);
            native_facts.selected_support = -1;
        } else if (support_idx < 3) {
            const auto perk = find_perk(p, SUPPORT_RUNE_PATHS[support_idx]);
            if (!perk) return 2;
            if (equip_support) {
                equip_support(rm, perk, 0);
                native_facts.selected_support = support_idx;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t sync_crystal_pairs(void*, uintptr_t p, uint8_t pair_mask) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const auto activate_perk = reinterpret_cast<void(*)(uintptr_t, uintptr_t, char, char, char, char, char)>(image_base + 0xfe19b0);
        const auto unlock_perk = reinterpret_cast<void(*)(uintptr_t, uintptr_t, char, char, uintptr_t, char)>(image_base + 0xfe2500);
        const auto deactivate_perk = reinterpret_cast<void(*)(uintptr_t, uintptr_t, char)>(image_base + 0xfe2c20);

        for (int i = 0; i < 6; ++i) {
            const auto perk = find_perk(p, CRYSTAL_PAIR_PATHS[i]);
            if (!perk) return 2;
            if (pair_mask & (1u << i)) {
                if (unlock_perk) unlock_perk(p + 0x3b40, perk, 0, 0, 0, 0);
                if (activate_perk) activate_perk(p + 0x3b40, perk, 1, 0, 0, 0, 1);
            } else {
                if (deactivate_perk) deactivate_perk(p + 0x3b40, perk, 0);
            }
        }
        native_facts.derived_pairs = pair_mask;
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

bool register_active_support(uintptr_t rm, uintptr_t perk) {
    const auto assure_size = reinterpret_cast<char (*)(uintptr_t)>(image_base + 0x357040);
    auto list = *reinterpret_cast<uintptr_t**>(rm + 0x68);
    auto count = *reinterpret_cast<int*>(rm + 0x70);
    auto capacity = *reinterpret_cast<int*>(rm + 0x74);
    if (count < 0 || count > 16 || capacity < count || (count && !list)) return false;
    bool registered = false;
    for (int entry = 0; entry < count; ++entry) registered |= list[entry] == perk;
    if (!registered) {
        if (count == 16 || (count >= capacity && (!assure_size || !assure_size(rm + 0x68)))) return false;
        list = *reinterpret_cast<uintptr_t**>(rm + 0x68);
        capacity = *reinterpret_cast<int*>(rm + 0x74);
        if (!list || *reinterpret_cast<int*>(rm + 0x70) != count || capacity <= count) return false;
        list[count] = perk;
        *reinterpret_cast<int*>(rm + 0x70) = count + 1;
    }
    return true;
}

void bind_run_state(void*, uintptr_t p) {
    if (!p) return;
    __try {
        const uintptr_t rm = p + 0x19168;
        const auto active_list = *reinterpret_cast<const uintptr_t**>(p + 0x3ba8);
        const auto active_entries = *reinterpret_cast<const int*>(p + 0x3bb0);
        if (active_entries < 0 || active_entries > 4000 || (active_entries && !active_list)) return;
        uintptr_t active_support = 0;
        int active_count = 0;
        uint8_t seen = 0;
        for (int i = 0; i < active_entries; ++i) {
            const auto perk = active_list[i * 2];
            if (!perk) continue;
            const auto name = *reinterpret_cast<const char**>(perk + 8);
            if (!name) continue;
            for (int s = 0; s < 3; ++s) {
                if (std::strcmp(name, SUPPORT_RUNE_PATHS[s]) != 0 || (seen & (1u << s))) continue;
                seen |= static_cast<uint8_t>(1u << s);
                active_support = perk;
                ++active_count;
                if (!register_active_support(rm, perk)) return;
                break;
            }
        }
        if (active_count == 1 && !*reinterpret_cast<uintptr_t*>(rm + 0x98))
            *reinterpret_cast<uintptr_t*>(rm + 0x98) = active_support;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

} // namespace

Calls calls{nullptr, player, read, ensure_normal_runes, ensure_support_runes,
            select_normal_rune, select_support_rune, sync_crystal_pairs,
            refresh, bind_run_state};

bool available() { return ready.load(std::memory_order_acquire); }

bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}

void execute_native(const sc_runes_request& request, sc_runes_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_RUNES_OUTCOME_UNAVAILABLE; return; }
    out.flags |= SC_RUNES_FLAG_SHARED_STATE_BOUND;
    execute(request, out, calls);
}

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id) {
    calls = value;
    std::memcpy(fixture_namespace, id, 65);
    ready.store(true, std::memory_order_release);
}
#endif

void install(const engine::Binding& binding) {
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;
    engine::LocalMemory memory;
    const auto& sites = builds::steam_20260818().runes;
    for (const auto& site : sites) {
        std::array<uint8_t, 32> actual{};
        const auto target=native::target_from_hex(image_base+site.rva,site.prefix);
        if (!binding.image.contains(site.rva, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0) ||
            memory.copy(target.address, actual.data(), actual.size()).reason || actual != target.bytes) return;
    }
    ready.store(true, std::memory_order_release);
}

} // namespace sentinel::runes
