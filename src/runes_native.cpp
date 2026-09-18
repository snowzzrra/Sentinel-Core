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
        const auto perk_list = *reinterpret_cast<const uintptr_t**>(p + 0x3b90);
        const auto perk_count = *reinterpret_cast<const int*>(p + 0x3b98);
        if (!perk_list || perk_count <= 0 || perk_count > 4000) return 0;
        for (int i = 0; i < perk_count; ++i) {
            const auto perk = perk_list[i];
            if (!perk) continue;
            const auto name = *reinterpret_cast<const char**>(perk + 8);
            if (name && std::strcmp(name, target_name) == 0) {
                return perk;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

bool read(void*, uintptr_t p, SnapshotFacts& facts) {
    if (!p) return false;
    __try {
        const uintptr_t rm = p + 0x19168;
        facts = native_facts;

        // 1. Registered normal runes
        const auto reg_list = *reinterpret_cast<const uintptr_t**>(rm + 0x50);
        const auto reg_count = *reinterpret_cast<const int*>(rm + 0x58);
        if (reg_list && reg_count > 0 && reg_count <= 64) {
            for (int i = 0; i < reg_count; ++i) {
                const auto perk = reg_list[i];
                if (!perk) continue;
                const auto name = *reinterpret_cast<const char**>(perk + 8);
                if (!name) continue;
                for (int r = 0; r < 9; ++r) {
                    if (std::strcmp(name, NORMAL_RUNE_PATHS[r]) == 0) {
                        facts.owned_normal |= (1u << r);
                        break;
                    }
                }
            }
        }

        // 2. Registered support runes
        const auto supp_list = *reinterpret_cast<const uintptr_t**>(rm + 0x68);
        const auto supp_count = *reinterpret_cast<const int*>(rm + 0x70);
        if (supp_list && supp_count > 0 && supp_count <= 16) {
            for (int i = 0; i < supp_count; ++i) {
                const auto perk = supp_list[i];
                if (!perk) continue;
                const auto name = *reinterpret_cast<const char**>(perk + 8);
                if (!name) continue;
                for (int s = 0; s < 3; ++s) {
                    if (std::strcmp(name, SUPPORT_RUNE_PATHS[s]) == 0) {
                        facts.owned_support |= (1u << s);
                        break;
                    }
                }
            }
        }

        // 3. Equipped normal slots
        for (int slot = 0; slot < 3; ++slot) {
            const auto equipped_perk = *reinterpret_cast<const uintptr_t*>(rm + 0x80 + slot * 8);
            if (equipped_perk) {
                const auto name = *reinterpret_cast<const char**>(equipped_perk + 8);
                if (name) {
                    for (int r = 0; r < 9; ++r) {
                        if (std::strcmp(name, NORMAL_RUNE_PATHS[r]) == 0) {
                            facts.selected_slots[slot] = static_cast<int8_t>(r);
                            break;
                        }
                    }
                }
            }
        }

        // 4. Equipped support slot
        const auto equipped_supp = *reinterpret_cast<const uintptr_t*>(rm + 0x98);
        if (equipped_supp) {
            const auto name = *reinterpret_cast<const char**>(equipped_supp + 8);
            if (name) {
                for (int s = 0; s < 3; ++s) {
                    if (std::strcmp(name, SUPPORT_RUNE_PATHS[s]) == 0) {
                        facts.selected_support = static_cast<int8_t>(s);
                        break;
                    }
                }
            }
        }

        // 5. Unlocked slots
        facts.unlocked_slots = 0;
        const auto req0 = *reinterpret_cast<const int*>(rm + 0x40);
        const auto req1 = *reinterpret_cast<const int*>(rm + 0x44);
        const auto req2 = *reinterpret_cast<const int*>(rm + 0x48);
        if (reg_count >= req0) facts.unlocked_slots |= (1u << 0);
        if (reg_count >= req1) facts.unlocked_slots |= (1u << 1);
        if (reg_count >= req2) facts.unlocked_slots |= (1u << 2);

        // 6. Derived crystal pairs
        facts.derived_pairs = compute_derived_crystal_pairs(facts.health_tier, facts.armor_tier, facts.ammo_tier);
        native_facts = facts;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint32_t ensure_normal_runes(void*, uintptr_t p, uint32_t normal_mask) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const uintptr_t rm = p + 0x19168;
        const auto unlock_perk = reinterpret_cast<void(*)(uintptr_t, uintptr_t, char, char, uintptr_t, char)>(image_base + 0xfe2500);
        const auto assure_size = reinterpret_cast<char(*)(uintptr_t, uint32_t)>(image_base + 0x357040);
        const auto level_event = reinterpret_cast<void(*)(uintptr_t, uint64_t)>(image_base + 0x13ef6a0);

        for (int r = 0; r < 9; ++r) {
            if (!(normal_mask & (1u << r))) continue;
            const auto perk = find_perk(p, NORMAL_RUNE_PATHS[r]);
            if (!perk) continue;

            auto reg_list = *reinterpret_cast<uintptr_t**>(rm + 0x50);
            auto reg_count = *reinterpret_cast<int*>(rm + 0x58);
            const auto reg_cap = *reinterpret_cast<int*>(rm + 0x5c);
            bool found = false;
            if (reg_list && reg_count > 0) {
                for (int i = 0; i < reg_count; ++i) {
                    if (reg_list[i] == perk) { found = true; break; }
                }
            }
            if (!found) {
                if (reg_count >= reg_cap && assure_size) {
                    assure_size(rm + 0x50, reg_count + 1);
                    reg_list = *reinterpret_cast<uintptr_t**>(rm + 0x50);
                }
                if (reg_list) {
                    reg_list[reg_count] = perk;
                    *reinterpret_cast<int*>(rm + 0x58) = reg_count + 1;
                }
            }
            if (unlock_perk) unlock_perk(p + 0x3b40, perk, 0, 0, 0, 0);
            if (level_event) level_event(p, 0x457);
            native_facts.owned_normal |= (1u << r);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}

uint32_t ensure_support_runes(void*, uintptr_t p, uint32_t support_mask) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const uintptr_t rm = p + 0x19168;
        const auto unlock_perk = reinterpret_cast<void(*)(uintptr_t, uintptr_t, char, char, uintptr_t, char)>(image_base + 0xfe2500);
        const auto assure_size = reinterpret_cast<char(*)(uintptr_t, uint32_t)>(image_base + 0x357040);
        const auto level_event = reinterpret_cast<void(*)(uintptr_t, uint64_t)>(image_base + 0x13ef6a0);

        for (int s = 0; s < 3; ++s) {
            if (!(support_mask & (1u << s))) continue;
            const auto perk = find_perk(p, SUPPORT_RUNE_PATHS[s]);
            if (!perk) continue;

            auto supp_list = *reinterpret_cast<uintptr_t**>(rm + 0x68);
            auto supp_count = *reinterpret_cast<int*>(rm + 0x70);
            const auto supp_cap = *reinterpret_cast<int*>(rm + 0x74);
            bool found = false;
            if (supp_list && supp_count > 0) {
                for (int i = 0; i < supp_count; ++i) {
                    if (supp_list[i] == perk) { found = true; break; }
                }
            }
            if (!found) {
                if (supp_count >= supp_cap && assure_size) {
                    assure_size(rm + 0x68, supp_count + 1);
                    supp_list = *reinterpret_cast<uintptr_t**>(rm + 0x68);
                }
                if (supp_list) {
                    supp_list[supp_count] = perk;
                    *reinterpret_cast<int*>(rm + 0x70) = supp_count + 1;
                }
            }
            if (unlock_perk) unlock_perk(p + 0x3b40, perk, 0, 0, 0, 0);
            if (level_event) level_event(p, 0x457);
            native_facts.owned_support |= (1u << s);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
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
            if (perk && equip_rune) {
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
            if (perk && equip_support) {
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
            if (!perk) continue;
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

void bind_run_state(void*, uintptr_t p) {
    if (!p) return;
    __try {
        ensure_normal_runes(nullptr, p, native_facts.owned_normal);
        ensure_support_runes(nullptr, p, native_facts.owned_support);
        for (int slot = 0; slot < 3; ++slot) {
            if (native_facts.selected_slots[slot] >= 0) {
                select_normal_rune(nullptr, p, static_cast<uint8_t>(slot), native_facts.selected_slots[slot]);
            }
        }
        if (native_facts.selected_support >= 0) {
            select_support_rune(nullptr, p, native_facts.selected_support);
        }
        sync_crystal_pairs(nullptr, p, native_facts.derived_pairs);
        refresh(nullptr, p);
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

void install(const engine::Binding& binding, HANDLE stop) {
    (void)stop;
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;
    ready.store(true, std::memory_order_release);
}

} // namespace sentinel::runes
