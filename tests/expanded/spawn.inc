namespace sentinel::expanded::detail {

#ifdef SC_NATIVE_TESTING

void hide_detour(uintptr_t actor) { return hide_detour_impl(actor, _ReturnAddress()); }

bool test_supply_headroom() {
    const auto previous_resolve = resolve_actor;
    const auto previous_name = definition_name;
    const auto previous_admission = native_admission;
    static int32_t managed, maximum;
    static uintptr_t budget;
    managed = 0; maximum = 90;
    native_admission = [](uintptr_t candidate, int32_t adjustment) { return candidate == budget && managed < adjustment - 1 + maximum; };
    static uintptr_t owners[2];
    alignas(uintptr_t) uint8_t managers[2][0xcd0]{};
    int32_t command_count = 1;
    for (unsigned index = 0; index < 2; ++index) {
        owners[index] = reinterpret_cast<uintptr_t>(managers[index]);
        std::memcpy(managers[index] + 0xcc8, &command_count, sizeof(command_count));
    }
    resolve_actor = [](uintptr_t reference) { return *reinterpret_cast<uintptr_t*>(reference); };
    definition_name = [](uintptr_t entity) -> const char* {
        if (entity == owners[0]) return "ap_v070_supply_0123456789abcdefabcd_manager";
        if (entity == owners[1]) return "ap_v070_supply_0123456789abcdefabce_manager";
        if (entity == 1) return "ap_v070_supply_0123456789abcdefabcd_actors_ai__ap_v070_s00100";
        return "native_ai";
    };
    uintptr_t references[4][3]{{11}, {22}, {33}, {44}};
    struct References { uintptr_t data; int32_t count, capacity; uint32_t flags = 0x1050001; }
        collection{reinterpret_cast<uintptr_t>(references), 2, 4};
    uintptr_t manager_references[2][4]{{0, owners[0]}, {0, owners[1]}};
    auto world = std::make_unique<uint8_t[]>(0x126668);
    const auto collection_address = reinterpret_cast<uintptr_t>(&collection);
    const auto manager_address = reinterpret_cast<uintptr_t>(manager_references);
    const int32_t manager_count = 2;
    std::memcpy(world.get() + 0x62088, &collection_address, sizeof(collection_address));
    std::memcpy(world.get() + 0x126658, &manager_address, sizeof(manager_address));
    std::memcpy(world.get() + 0x126660, &manager_count, sizeof(manager_count));
    const auto address = reinterpret_cast<uintptr_t>(world.get());
    budget = address + 0xafd98;
    bool ok = !supply_admission(address);
    collection.count = 1;
    ok = ok && supply_admission(address);
    maximum = 3; managed = 1;
    ok = ok && !supply_admission(address);
    managed = 0;
    ok = ok && supply_admission(address);
    maximum = 90;
    collection.count = 2; references[0][0] = 1;
    ok = ok && supply_admission(address);
    collection.count = 3;
    ok = ok && !supply_admission(address);
    command_count = 0;
    std::memcpy(managers[1] + 0xcc8, &command_count, sizeof(command_count));
    ok = ok && supply_admission(address);
    collection.count = 4;
    ok = ok && !supply_admission(address);
    collection.flags = 0; references[0][0] = 11;
    ok = ok && supply_admission(address);
    resolve_actor = previous_resolve; definition_name = previous_name; native_admission = previous_admission;
    return ok;
}

#endif

}

namespace sentinel::expanded {
using namespace detail;

#ifdef SC_NATIVE_TESTING

    struct Definition { uintptr_t unused; const char* name; uint8_t padding[0x158 - 16]; Definition* parent; };
    struct Actor { uintptr_t* table; bool dead; uint8_t padding[0xa8 - 9]; Definition* definition; };
void inherited_population_cases(Actor& ordinary, Definition& strict_parent, SpawnLimits& child_limits, bool& hierarchy_ok) {
    inherit_population(child_limits, reinterpret_cast<uintptr_t>(&ordinary));
    hierarchy_ok &= child_limits.strict && child_limits.maximum == std::array<uint32_t, 4>{1,2,1,3} &&
        child_limits.population == 6 && child_limits.heavy == 2 && child_limits.superheavy == 2 &&
        !spawn_allowed(child_limits, {0,0,1});
    strict_parent.name =
        "ap_v070_ai_a_t00000000000000010000000040000000_v"
        "000000010000000200000001000000000000000000000000000000000000000000000000"
        "_a1234567812345678_p00000006000000020000000600000003_l00000001000000020000000100000003_ka__ap_v070_s00100";
    child_limits.kind = 3; child_limits.spirit_counter = true;
    inherit_population(child_limits, reinterpret_cast<uintptr_t>(&ordinary));
    hierarchy_ok &= child_limits.heavy == 1 && child_limits.species_maximum[2] == 0 &&
        spawn_allowed(child_limits, {}) && !spawn_allowed(child_limits, {0,0,0,1});
    strict_parent.name = "ap_v070_ai_a_a1234567812345678_p00000006000000020000000600000201_l00000001000000020000000100000003_ka__ap_v070_s00100";
    child_limits.maximum = {4,4,4,8};
    inherit_population(child_limits, reinterpret_cast<uintptr_t>(&ordinary));
    hierarchy_ok &= !child_limits.strict && child_limits.maximum == std::array<uint32_t, 4>{4,4,4,8};
}
void physical_species_cases(Actor& ordinary, Definition& strict_parent, SpawnLimits& child_limits, bool& hierarchy_ok) {
    constexpr const char* special_types[]{"ai/superheavy/marauder", "ai/superheavy/archvile", "ai/ambient/spirit",
        "ai/ambient/buffpod", "ai/fodder/zombie_t1_screecher", "ai/heavy/bloodangel",
        "ai/fodder/prowler_cursed", "ai/fodder/imp_stone", "ai/superheavy/baron_armored"};
    constexpr uint64_t special_masks[]{0x8000000ull, 0x40000000ull, 0x10000000000ull, 0x2000000000ull,
        0x2ull, 0x1000000ull, 0x100ull, 0x20ull, 0x20000000ull};
    for (unsigned index = 0; index < std::size(special_types); ++index) {
        strict_parent.name = special_types[index];
        unsigned physical = 9;
        special_kind(reinterpret_cast<uintptr_t>(&ordinary), &physical);
        hierarchy_ok &= physical == index;
        child_limits.physical = special_masks[index];
        reserve_admission(child_limits);
        hierarchy_ok &= reserved_species[index] == 1;
        release_admission(child_limits);
        hierarchy_ok &= reserved_species[index] == 0;
    }
    for (const auto type : {"ai/fodder/lostsoul", "ai/fodder/imp_stone", "ai/ambient/zombie_cueball",
            "ai/ambient/tentacle", "ai/ambient/tentacle_liquid", "ai/ambient/super_tentacle",
            "ai/ambient/turret", "ai/ambient/demonic_trooper"}) {
        strict_parent.name = type;
        unsigned physical = 9;
        hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary), &physical) == 8 &&
            special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    }
    strict_parent.name = "ai/boss/gladiator";
    unsigned boss_species = 9;
    hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary), &boss_species) == 9 &&
        special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    for (const auto type : {"ai/heavy/mancubus_fire", "ai/fodder/prowler", "ai/fodder/carcass"}) {
        strict_parent.name = type;
        hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary), &boss_species) == 3;
    }
}
bool countermeasure_mapping_case() {
    NativeRecipe counter;
    counter.blood_punch_alternatives.emplace(11, 22);
    counter.microwave_alternatives.emplace(33, 44);
    counter.stone_alternatives.emplace(55, 66);
    counter.armored_alternatives.emplace(77, 88);
    return countermeasure_body(counter, 11, false, false, false) == 22 && countermeasure_body(counter, 11, true, false, false) == 11 &&
                    countermeasure_body(counter, 33, false, false, false) == 44 && countermeasure_body(counter, 33, false, true, false) == 33 &&
                    countermeasure_body(counter, 55, false, false, false) == 66 && countermeasure_body(counter, 55, false, false, true) == 55 &&
                    countermeasure_body(counter, 77, false, false, false, false) == 88 &&
                    countermeasure_body(counter, 77, false, false, false, true) == 77 &&
                    countermeasure_body(counter, 99, false, false, false, false) == 99;
}
bool spawn_case(const unsigned& calls, uint64_t result, unsigned count) {
    return spawn_detour(7, 8, 9, 10, 11) == result && calls == count;
}
const char* limited_actor_definition(uintptr_t actor) {
        if (actor == 9) return "ap_v070_spawn_a_l00000001000000010000000100000002_km__ap_v070_s00100";
        const auto definition = *reinterpret_cast<Definition**>(actor + 0xa8);
        return definition ? definition->name : "native_ai";

}
bool fixture_spawn_abi(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth) {
    return manager == 7 && record == 8 && point == 9 && fourth == 10 && fifth == 11;
}
bool test_limit_inputs() {
    const auto previous_spawn = original_spawn;
    const auto previous_resolve = resolve_actor;
    const auto previous_name = definition_name;
    const auto previous_world = world_global;
    const auto was_stopped = stopped.exchange(false);
    static unsigned calls;
    static bool abi;
    calls = 0; abi = true;
    original_spawn = [](uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth) -> uint64_t {
        ++calls; abi = abi && fixture_spawn_abi(manager, record, point, fourth, fifth);
        return 1;
    };
    resolve_actor = [](uintptr_t record) { return *reinterpret_cast<const uintptr_t*>(record); };
    static_assert(offsetof(Definition, parent) == 0x158);
    Definition marauder_type{0, "ai/superheavy/marauder", {}, nullptr};
    Definition archvile_type{0, "ai/superheavy/archvile", {}, nullptr};
    Definition spirit_type{0, "ai/ambient/spirit", {}, nullptr};
    Definition boss_type{0, "ai/boss/darklord", {}, &marauder_type};
    Definition marauder_definition{0, "native_marauder", {}, &marauder_type};
    Definition archvile_definition{0, "native_archvile", {}, &archvile_type};
    Definition spirit_definition{0, "native_spirit", {}, &spirit_type};
    definition_name = limited_actor_definition;
    uintptr_t table[34]{};
    static_assert(offsetof(Actor, definition) == 0xa8);
    Actor marauder{table, false, {}, &marauder_definition}, archvile{table, false, {}, &archvile_definition},
          spirit{table, false, {}, &spirit_definition}, boss{table, false, {}, &boss_type};
    Definition default_type{0, "ai/default", {}, nullptr};
    default_type.parent = &default_type;
    Actor ordinary{table, false, {}, &default_type};
    bool hierarchy_ok = special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    Definition strict_parent{0,
        "ap_v070_ai_a_t00000000000000010000000040000000_v"
        "000000010000000200000001000000000000000000000000000000000000000000000000"
        "_a1234567812345678_p00000006000000020000000600000203_l00000001000000020000000100000003_ka__ap_v070_s00100", {}, nullptr};
    ordinary.definition = &strict_parent;
    auto child_limits = *spawn_limits("ap_v070_ai_a_p00000008000000030000000800000303_l00000004000000040000000400000008_ks__ap_v070_s00100");
    inherited_population_cases(ordinary, strict_parent, child_limits, hierarchy_ok);
    physical_species_cases(ordinary, strict_parent, child_limits, hierarchy_ok);
    ordinary.definition = &default_type;
    hierarchy_ok &= countermeasure_mapping_case();
    Definition first{0, "ai/fodder/first", {}, &default_type};
    default_type.parent = &first;
    hierarchy_ok &= special_kind(reinterpret_cast<uintptr_t>(&ordinary)) == 3;
    table[0x108 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(+[](uintptr_t actor) { return reinterpret_cast<Actor*>(actor)->dead; });
    uintptr_t references[4][3]{{reinterpret_cast<uintptr_t>(&marauder)}, {reinterpret_cast<uintptr_t>(&archvile)},
                             {reinterpret_cast<uintptr_t>(&spirit)}, {reinterpret_cast<uintptr_t>(&boss)}};
    struct References { uintptr_t data; int32_t count, capacity; uint32_t flags = 0x1050001; }
        collection{reinterpret_cast<uintptr_t>(references), 4, 5};
    uint64_t records[4][0x68 / sizeof(uint64_t)]{};
    for (unsigned index = 0; index < 4; ++index) {
        records[index][0] = uint64_t{1} << 27;
        records[index][1] = (uint64_t{1} << 32) | 1;
        records[index][2] = references[index][0];
    }
    alignas(uintptr_t) uint8_t manager[0xd20]{};
    const auto records_address = reinterpret_cast<uintptr_t>(records);
    const int32_t actor_count = 4;
    std::memcpy(manager + 0xc80, &records_address, sizeof(records_address));
    std::memcpy(manager + 0xc88, &actor_count, sizeof(actor_count));
    uintptr_t managers[2][4]{{0, reinterpret_cast<uintptr_t>(manager)}, {0, reinterpret_cast<uintptr_t>(manager)}};
    auto world = std::make_unique<uint8_t[]>(0x126668);
    const auto collection_address = reinterpret_cast<uintptr_t>(&collection);
    const auto managers_address = reinterpret_cast<uintptr_t>(managers);
    const int32_t manager_count = 2;
    std::memcpy(world.get() + 0x62088, &collection_address, sizeof(collection_address));
    std::memcpy(world.get() + 0x126658, &managers_address, sizeof(managers_address));
    std::memcpy(world.get() + 0x126660, &manager_count, sizeof(manager_count));
    const auto world_address = reinterpret_cast<uintptr_t>(world.get());
    world_global = reinterpret_cast<uintptr_t>(&world_address);
    bool ok = spawn_case(calls, 0, 0);
    marauder.dead = true; archvile.dead = true;
    ok = ok && spawn_case(calls, 0, 0);
    references[0][0] = 0;
    ok = ok && spawn_case(calls, 0, 0);
    references[1][0] = 0;
    ok = ok && spawn_case(calls, 1, 1);
    marauder.dead = false; references[0][0] = reinterpret_cast<uintptr_t>(&marauder);
    collection.data = reinterpret_cast<uintptr_t>(references + 1); collection.count = 3;
    ok = ok && spawn_case(calls, 1, 2);
    collection.data = reinterpret_cast<uintptr_t>(references); collection.count = 4;
    stopped.store(true);
    ok = ok && spawn_case(calls, 1, 3) && abi;
    uint64_t native_scope = 14695981039346656037ull;
    for (const unsigned char character : std::string_view("native_ai")) native_scope = (native_scope ^ character) * 1099511628211ull;
    SpawnLimits strict_scope{};
    strict_scope.kind = 3; strict_scope.population = strict_scope.pressure = 3;
    strict_scope.heavy = 1; strict_scope.strict = true; strict_scope.scope = native_scope;
    ordinary.definition = &strict_parent; strict_parent.name = "ai/heavy/mancubus_fire";
    references[0][0] = records[0][2] = reinterpret_cast<uintptr_t>(&ordinary);
    collection.count = 1;
    hierarchy_ok &= population_member(world_address, reinterpret_cast<uintptr_t>(&ordinary), native_scope) &&
        !special_admission(strict_scope);
    strict_scope.scope = native_scope + 1;
    hierarchy_ok &= !population_member(world_address, reinterpret_cast<uintptr_t>(&ordinary), strict_scope.scope) &&
        special_admission(strict_scope);
    strict_scope.scope = native_scope;
    strict_parent.name = "ai/ambient/turret";
    hierarchy_ok &= special_admission(strict_scope);
    original_spawn = previous_spawn; resolve_actor = previous_resolve;
    definition_name = previous_name; world_global = previous_world; stopped.store(was_stopped);
    return ok && hierarchy_ok && test_supply_headroom();
}

#endif

}
