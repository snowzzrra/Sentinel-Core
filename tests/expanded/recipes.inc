#include <filesystem>
#include <fstream>

namespace sentinel::expanded::detail {
void invoke_summon_creation(RecipeCreation*, uintptr_t, const SummonSource*);
}
namespace sentinel::commands {
void report_recovery_selection(const recovery::Attempt&);
}

namespace sentinel::expanded {
using namespace detail;

#ifdef SC_NATIVE_TESTING

std::filesystem::path recipe_fault_fixture_log() {
    wchar_t temporary[32768]{}, previous[32768]{};
    GetTempPathW(32768, temporary);
    GetEnvironmentVariableW(L"LOCALAPPDATA", previous, 32768);
    const auto root = std::filesystem::path(temporary) / ("sentinel-recipe-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    Snapshot identity; identity.pid = GetCurrentProcessId(); identity.process_created = 123456;
    startup_log::record(identity);
    SetEnvironmentVariableW(L"LOCALAPPDATA", *previous ? previous : nullptr);
    return root / "SentinelCore/diagnostics" / (std::to_string(identity.pid) + "-123456.recipe.log");
}

bool test_recovery_selection_log(const std::filesystem::path& directory) {
    recovery::Attempt attempt; attempt.id = 1; attempt.started = true;
    recovery::Manager manager; manager.name = "arena"; manager.exclusion = "request_closed";
    attempt.context.managers.push_back(manager);
    manager.name = "other"; manager.exclusion = "outside_empty_encounter";
    attempt.context.managers.push_back(manager);
    commands::report_recovery_selection(attempt);
    commands::report_recovery_selection(attempt);
    std::ifstream input(directory / (std::to_string(GetCurrentProcessId()) + "-123456.recovery.log"));
    const std::string persisted{std::istreambuf_iterator<char>(input), {}};
    bool ok = persisted.find("manager=arena reason=request_closed") != std::string::npos;
    ok &= persisted.find("manager=other reason=outside_empty_encounter") != std::string::npos;
    ok &= std::count(persisted.begin(), persisted.end(), '\n') == 2;
    return ok;
}

bool test_recipe_failure_policy() {
    const auto log = recipe_fault_fixture_log();
    const bool recovery_log_ok = test_recovery_selection_log(log.parent_path());
    map_freed();
    bool ok = recipe_state.reset(std::string(64, 'a'), "game/test");
    ok &= recipe_state.request("retry", {"arena", "origin", "", 1, 1, 2, 2, 2, 1});
    RecipeCreation creation; creation.key = "retry";
    creation.emission = *recipe_state.reserve(creation.key);
    const std::string name = "ai/test/body";
    auto body = recipe_text(name); creation.body = reinterpret_cast<uintptr_t>(&body);
    recipe_creation = &creation;
    const auto database = definition_database;
    const auto lookup = lookup_definition;
    definition_database = []() -> uintptr_t { return 1; };
    lookup_definition = [](uintptr_t, const char*, int32_t) -> uintptr_t { return 0; };
    const auto reservations = reserved_actors;
    reserve_admission(creation.limits); creation.reserved = true;
    SummonSource source;
    invoke_summon_creation(&creation, 0, &source);
    definition_database = database; lookup_definition = lookup;
    ok &= !recipe_fault.load();
    ok &= creation.failed;
    ok &= !recipe_state.requests().at("retry").reserved;
    ok &= !recipe_state.requests().at("retry").emitted;
    ok &= reserved_actors == reservations;
    ok &= recipe_creation == nullptr;
    const auto retry = recipe_state.reserve("retry");
    ok &= retry.has_value();
    ok &= retry->ordinal == creation.emission.ordinal;
    ok &= recipe_state.created("retry", *retry);
    recipe_creation = &creation;
    recipe_creation_failed(creation, __func__, __LINE__, "credit_inconsistent");
    ok &= recipe_fault.load();
    const auto first = recipe_fault_diagnostic();
    set_recipe_fault(__func__, __LINE__, "later_fault", "retry");
    ok &= recipe_fault_diagnostic() == first;
    recipe_creation = nullptr;
    map_freed();
    std::ifstream input(log);
    const std::string persisted{std::istreambuf_iterator<char>(input), {}};
    for (const auto* field : {"function=test_recipe_failure_policy", "reason=credit_inconsistent", "request=retry", "actor=0",
            "body=ai/test/body", "map=game/test", "generation=", "stamp=", "line="}) ok &= persisted.find(field) != std::string::npos;
    ok &= persisted.find("later_fault") == std::string::npos;
    ok &= recipe_fault_diagnostic().empty();
    creation.restoring = true;
    recipe_creation_failed(creation, __func__, __LINE__, "checkpoint_inconsistent");
    ok &= recipe_fault.load();
    map_freed();
    creation.restoring = false; creation.actor = 0x1234;
    recipe_creation_failed(creation, __func__, __LINE__, "actor_inconsistent");
    ok &= recipe_fault.load();
    ok &= recovery_log_ok;
    map_freed();
    return ok;
}

namespace recipe_transport_fixture {
    uintptr_t root, library;
    std::vector<std::array<uint8_t, 0xbe0 + sizeof(save::NativeString)>> chunks;
    bool missing = false;
uintptr_t find(uintptr_t, const char* name) {
        const std::string_view value(name);
        if (value == "ap_v070_recipe_plan") return root;
        if (value.rfind("ap_v070_bodies_r", 0) == 0) return library;
        if (value.rfind("ap_v070_recipe_plan_", 0) != 0) return 0;
        size_t index = 0; const auto at = value.find_last_of('_') + 1;
        const auto parsed = std::from_chars(value.data() + at, value.data() + value.size(), index);
        return !missing && parsed.ec == std::errc{} && index < chunks.size() ?
            reinterpret_cast<uintptr_t>(chunks[index].data()) : 0;

}
}
bool load_transport_checkpoint(uintptr_t address, uintptr_t root, const std::string& metadata, std::string& decoded, std::string& checkpoint) {
    RecipeLibrary parsed;
    return metadata.size() > 65536 && read_recipe_metadata(address, root, decoded) && decoded == metadata &&
        parse_recipe_library(decoded, parsed) && parsed.recipes.size() == 414 && recipe_library_point(root) &&
        load_native_recipes() && native_recipes.size() == 414 && capture_recipe_checkpoint(checkpoint);
}
bool test_recipe_transport() {
    const bool policy_ok = test_recipe_failure_policy();
    using namespace recipe_transport_fixture;
    const auto previous_world = world_global;
    const auto previous_find = find_entity;
    const auto previous_name = definition_name;
    auto world = std::make_unique<std::array<uint8_t, 0x126680>>();
    uintptr_t address = reinterpret_cast<uintptr_t>(world->data());
    world_global = reinterpret_cast<uintptr_t>(&address);
    std::array<uint8_t, 0xc50> root_storage{}, library_storage{};
    root = reinterpret_cast<uintptr_t>(root_storage.data());
    library = reinterpret_cast<uintptr_t>(library_storage.data());
    std::string metadata = "EAP1|" + std::string(64, 'a') + "|game/test\n";
    for (unsigned i = 0; i < 414; ++i) {
        char origin[21]; sprintf_s(origin, "%020x", i);
        metadata += std::string(origin) + "|i|" + std::string(128, 's') + std::to_string(i) +
            "|arena|0000000000000010|00000001|00000000|00000000|00000000\n";
    }
    std::vector<std::string> texts;
    for (size_t at = 0; at < metadata.size(); at += 1800) texts.push_back(metadata.substr(at, 1800));
    chunks.resize(texts.size());
    for (size_t i = 0; i < texts.size(); ++i)
        *reinterpret_cast<save::NativeString*>(chunks[i].data() + 0xbe0) = recipe_text(texts[i]);
    char header[41];
    sprintf_s(header, "EAPC1|%08x|%08x|%016llx", static_cast<unsigned>(texts.size()),
        static_cast<unsigned>(metadata.size()), functional_token(metadata));
    std::string transport = header;
    const auto publish = [&] { *reinterpret_cast<save::NativeString*>(root + 0xbe0) = recipe_text(transport); };
    publish();
    find_entity = recipe_transport_fixture::find;
    definition_name = [](uintptr_t) { return "ap_v070_recipe_plan_0"; };
    map_freed(); session_recipe_plan.clear();
    std::string decoded, checkpoint;
    bool ok = load_transport_checkpoint(address, root, metadata, decoded, checkpoint);
    map_freed();
    ok &= restore_recipe_checkpoint(checkpoint, "game/test") && load_native_recipes() && native_recipes.size() == 414;
    missing = true; ok &= !read_recipe_metadata(address, root, decoded); missing = false;
    std::swap(texts[0][0], texts[1][0]); ok &= !read_recipe_metadata(address, root, decoded);
    std::swap(texts[0][0], texts[1][0]);
    --reinterpret_cast<save::NativeString*>(chunks[0].data() + 0xbe0)->length;
    ok &= !read_recipe_metadata(address, root, decoded);
    ++reinterpret_cast<save::NativeString*>(chunks[0].data() + 0xbe0)->length;
    for (const size_t offset : {6u, 15u, 24u}) {
        transport = header; transport[offset] = 'f'; publish();
        ok &= !read_recipe_metadata(address, root, decoded);
    }
    transport = metadata.substr(0, 2046); publish(); map_freed(); session_recipe_plan.clear();
    ok &= !load_native_recipes() && recipe_fault.load() && !capture_recipe_checkpoint(checkpoint);
    const auto first_fault = recipe_fault_diagnostic();
    ok &= first_fault.find("function=load_recipe_metadata reason=metadata_invalid") != std::string::npos;
    set_recipe_fault(__func__, __LINE__, "later_failure");
    ok &= recipe_fault_diagnostic() == first_fault;
    uintptr_t result = 0;
    ok &= dispatch_recipe_locked(0, 0x1234, 0, 0, 0, false, "unused", result) && result == 0x1234;
    transport = metadata; publish(); map_freed(); session_recipe_plan.clear();
    ok &= recipe_fault_diagnostic().empty();
    ok &= load_native_recipes() && native_recipes.size() == 414;
    map_freed(); session_recipe_plan.clear();
    world_global = previous_world; find_entity = previous_find; definition_name = previous_name;
    chunks.clear();
    ok &= policy_ok;
    return ok && test_darklord_summon_scope() && test_darklord_telemetry();
}

#endif

}

namespace sentinel::expanded::detail {

#ifdef SC_NATIVE_TESTING

bool recipe_fault_call(uintptr_t worker) {
    __try { maintain_tick_detour(worker); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode() == 0xe0700002; }
    return false;
}

#endif

}

namespace sentinel::expanded {
using namespace detail;

#ifdef SC_NATIVE_TESTING

struct RecipeInputTest {
    struct Text {
        uintptr_t table;
        const char* data;
        int32_t length;
        uint32_t flags;
    };
    struct Collection {
        uintptr_t records;
        int32_t count, capacity;
        uint32_t flags;
    };
    inline static unsigned delay_count{};
    inline static std::array<uint8_t, 0xb8> native_delay{};
    inline static QueueCommand pair_queue{};
    inline static SpawnSingle host_single{};
    inline static Collection* host_collection{};
    inline static unsigned breach_group_calls{};
    inline static unsigned bridge_calls{};
    inline static unsigned retries, helper_records{};
    inline static bool allocation_allowed, native_allowed{};
    inline static uintptr_t queued_commands[1]{};
    inline static std::array<uint8_t, 0x110> queued_worker{};
    inline static const char* spirit_alias_text{};
    inline static const char* alias_text{};
    inline static uint32_t expected_override, expected_maintenance_population{};
    inline static uint64_t expected_logical, expected_physical{};
    inline static bool fail, valid, nested, fault, unique_bodies, missing_source, breach_fallback_available{};
    inline static unsigned calls, attempts, registrations, nested_calls{};
    inline static const char* arena_point_name{};
    inline static std::string metadata, origin, selected_name{};
    inline static uintptr_t original_definitions{};
    inline static std::array<uintptr_t, 2> actor_addresses{};
    inline static uintptr_t root, library, alias, source_group, source_point, owner, destination, spirit_alias,
        spirit_library, spirit_point, counter_library{};
    std::array<uint8_t, 0xe30> root_storage{}, library_storage{}, alias_storage{}, group_storage{}, point_storage{},
        manager{};
    std::array<uint8_t, 0x140> worker{};
    std::array<uint8_t, 0x80 * 4> bodies{};
    std::array<uintptr_t, 0x90 / 8> actor_table{};
    std::array<uintptr_t, 2> actor_storage{};
    std::array<std::string, 4> names;
    std::array<uint8_t, 0x140> failed_worker{};
    std::array<uint8_t, 0xe30> destination_storage{};
    std::array<uint8_t, 0x40> manager_references{};
    std::array<uint8_t, 24> actor_reference{};
    std::array<uint8_t, 0x68> migrated_record{};
    std::array<uint8_t, 0xe30> spirit_alias_storage{}, spirit_library_storage{}, spirit_point_storage{};
    std::array<uint8_t, 0x80> spirit_body{};
    std::array<uint8_t, 0xe30> counter_storage{};
    std::unique_ptr<std::array<uint8_t, 0x126680>> world = std::make_unique<std::array<uint8_t, 0x126680>>();
    uintptr_t world_address = reinterpret_cast<uintptr_t>(world->data());
    Collection collection{0, 0, 32, 0x1050001};
    std::string first;
    std::string alias_name;
    const std::string label = "public_label";
    const std::string migrated_label = "migrated";
    std::string checkpoint;
    std::string snapshot;
    RecipeState restored;
    const std::string spirit_origin = std::string(20, 'c'), spirit_alias_name = "ap_v070_request_r" + spirit_origin;
    std::string pair_metadata;
    std::array<std::string, 4> pair_names;
    Text counter_body{0, nullptr, 0, 0};
    std::string counter_name;
    bool ok = false;
    static void refresh_actor(uintptr_t reference, uintptr_t) {
        const auto id = *reinterpret_cast<const uint32_t*>(reference);
        if (id == 1 || id == 2) *reinterpret_cast<uintptr_t*>(reference + 8) = actor_addresses[id - 1];
    }
    static const char* entity_definition(uintptr_t entity) {
        if (entity == alias) return alias_text;
        if (spirit_alias && entity == spirit_alias) return spirit_alias_text;
        if (entity == owner) return "arena";
        if (entity == destination) return "destination";
        if (entity == source_point && arena_point_name) return arena_point_name;
        if (entity == actor_addresses[0] || entity == actor_addresses[1]) return selected_name.c_str();
        return "native_point";
    }
    static uintptr_t find_fixture_source(const char* name) {
        if (!std::strcmp(name, "breach_wasteland_arena_target_spawn_60")) return missing_source ? 0 : source_point;
        if (missing_source && breach_fallback_available && !std::strcmp(name, "ap_v070_source_413b7abe2818a6b7"))
            return alias;
        if (!std::strcmp(name, "original_point")) return missing_source ? 0 : source_point;
        if (missing_source && !std::strcmp(name, "ap_v070_source_a9f183750900d6cf")) return alias;
        return 0;
    }
    static uintptr_t find_fixture_entity(uintptr_t, const char* name) {
        if (!std::strcmp(name, "ap_v070_recipe_plan")) return root;
        if (std::string_view(name) == "ap_v070_bodies_r" + origin) return library;
        if (counter_library && std::string_view(name) == "ap_v070_counter_r" + origin) return counter_library;
        if (spirit_library && std::string_view(name) == "ap_v070_bodies_r" + std::string(20, 'c'))
            return spirit_library;
        if (!std::strcmp(name, "original_group")) return source_group;
        if (!std::strcmp(name, "ai_encounter_spawn_group_zone_skyscraper_upper_last")) return source_group;
        if (const auto source = find_fixture_source(name)) return source;
        if (!std::strcmp(name, "arch_template")) return source_group;
        if (!std::strcmp(name, "original_spirit_point")) return spirit_point;
        return 0;
    }
    static uintptr_t expected_creation_point() {
        return immora_breach_group(*recipe_creation->recipe) && missing_source     ? alias
               : recipe_creation->recipe->definition.family == 's' && spirit_point ? spirit_point
                                                                                   : source_point;
    }
    static bool creation_point_matches(uintptr_t point, uintptr_t group, uintptr_t expected_point) {
        return point == expected_point && group == 0 &&
               *reinterpret_cast<uint32_t*>(point + 0xd00) == expected_override &&
               !*reinterpret_cast<int32_t*>(point + 0xcd0) && *reinterpret_cast<int32_t*>(point + 0xc40) == 1 &&
               !*reinterpret_cast<int32_t*>(point + 0xc54);
    }
    static uintptr_t create_fixture_body(uintptr_t point, uintptr_t group) {
        if (!recipe_creation) {
            ++nested_calls;
            valid = valid && *reinterpret_cast<uintptr_t*>(point + 0xc38) == original_definitions &&
                    *reinterpret_cast<int32_t*>(point + 0xc40) == 2 && *reinterpret_cast<int32_t*>(point + 0xd00) == 7;
            return 0x7777;
        }
        ++attempts;
        if (recipe_creation->recipe->definition.family == 'm')
            valid &= recipe_creation->limits.population == expected_maintenance_population;
        const auto expected_point = expected_creation_point();
        valid = valid && creation_point_matches(point, group, expected_point);
        native_text(*reinterpret_cast<uintptr_t*>(point + 0xc38), selected_name, 511);
        if (fault) RaiseException(0xe0700002, 0, 0, nullptr);
        if (fail) return 0;
        if (nested) {
            nested = false;
            valid = valid && point_factory_detour(point, group) == 0x7777 &&
                    *reinterpret_cast<int32_t*>(point + 0xc40) == 1 && *reinterpret_cast<int32_t*>(point + 0xd00) == 12;
        }
        ++calls;
        return actor_addresses[unique_bodies ? recipe_creation->emission.ordinal % actor_addresses.size() : 0];
    }
    static void register_fixture_body(uintptr_t manager_address, uint64_t* record) {
        const auto logical = recipe_creation ? recipe_creation->recipe->definition.logical_mask : expected_logical;
        valid = valid && manager_address == owner && record[0] == logical;
        ++registrations;
    }
    static uint64_t choose_physical_candidate(uintptr_t manager_address, uintptr_t point, uint64_t type) {
        uint64_t selected = 0;
        bool population_entry = false;
        for (uint64_t candidate = 1; candidate <= (uint64_t{1} << 43); candidate <<= 1) {
            if (!(type & candidate) || !point_candidate_detour(manager_address, point, candidate)) continue;
            if (!selected) selected = candidate;
            population_entry |= candidate == 16;
        }
        if (!selected || !population_entry) return false;
        return selected;
    }
    static bool select_fixture_point(uintptr_t manager_address, uintptr_t point, uint64_t type, bool immediate) {
        uint64_t selected = type;
        if (immediate || type == encounter_ai_mask) {
            selected = choose_physical_candidate(manager_address, point, type);
            if (!selected) return false;
            const auto unrelated = expected_physical == 1 ? 2 : 1;
            valid = valid && !point_candidate_detour(manager_address + 1, point, unrelated) &&
                    point_index_detour(manager_address + 1, point, unrelated) == -1 &&
                    !point_candidate_detour(manager_address, source_group, selected) &&
                    point_index_detour(manager_address, source_group, selected) == -1;
        } else if (!point_candidate_detour(manager_address, point, type))
            return false;
        const auto index = point_index_detour(manager_address, point, selected);
        *reinterpret_cast<int32_t*>(point + 0xc54) =
            index >= 0 && index < *reinterpret_cast<int32_t*>(point + 0xc40) ? index : -1;
        valid = valid && *reinterpret_cast<int32_t*>(point + 0xc54) >= 0;
        return true;
    }
    static bool fixture_spawn_matches(uintptr_t manager_address, uintptr_t point, uint64_t type, bool supplied) {
        const auto expected_point = recipe_creation ? expected_creation_point() : source_point;
        const auto logical = recipe_creation ? recipe_creation->recipe->definition.logical_mask : expected_logical;
        const auto physical = recipe_creation
                                  ? spawn_tracking(*reinterpret_cast<const char**>(recipe_creation->body + 8))->physical
                                  : expected_physical;
        return manager_address == owner && point == expected_point &&
               (type == (supplied ? physical : logical) || type == encounter_ai_mask);
    }
    static uint64_t spawn_fixture_body(uintptr_t manager_address, uintptr_t record, uintptr_t point, uintptr_t,
                                       uintptr_t) {
        const auto type = *reinterpret_cast<uint64_t*>(record);
        const bool supplied = recipe_creation && *reinterpret_cast<uintptr_t*>(point + 0xc38) == recipe_creation->body;
        const bool immediate = recipe_creation && recipe_creation->recipe->definition.family == 'i';
        valid = valid && fixture_spawn_matches(manager_address, point, type, supplied);
        if (immediate && !supplied) return 0;
        if (supplied) {
            if (!select_fixture_point(manager_address, point, type, immediate)) return 0;
        }
        const auto actor = point_factory_detour(point, 0);
        if (!actor) return 0;
        const auto id = actor_identifier(actor);
        reinterpret_cast<uint64_t*>(record)[1] = uint64_t{id} << 32 | id;
        reinterpret_cast<uint64_t*>(record)[2] = 0;
        register_spawn_detour(manager_address, reinterpret_cast<uint64_t*>(record));
        return 1;
    }
    static uint64_t spawn_fixture_group(uintptr_t manager_address, uint64_t* record, uintptr_t group) {
        valid = valid && group == source_group;
        if ((*record & encounter_ai_mask) != encounter_ai_mask) return 0;
        return spawn_detour(manager_address, reinterpret_cast<uintptr_t>(record), source_point, 0, 0);
    }
    static void maintain_fixture(uintptr_t command) {
        valid = valid && *reinterpret_cast<int32_t*>(command + 0x90) == 3 &&
                *reinterpret_cast<int32_t*>(command + 0x94) == 5;
        alignas(8) uint8_t record[0x68]{};
        *reinterpret_cast<uint64_t*>(record) = 16;
        *reinterpret_cast<Text*>(record + 0x28) = *reinterpret_cast<Text*>(command + 0x58);
        if (group_detour(owner, reinterpret_cast<uint64_t*>(record), alias))
            ++*reinterpret_cast<int32_t*>(command + 0xc8);
        if (*reinterpret_cast<int32_t*>(command + 0xc8) == 5) *reinterpret_cast<uint8_t*>(command + 0xcc) = 1;
    }
    static uintptr_t construct_fixture_stagger(uintptr_t memory, int32_t count, uint64_t minimum, uint64_t maximum,
                                               uintptr_t target, uint64_t mask, const char* text) {
        valid = valid && (count == 1 || count == 2) && !minimum && !maximum &&
                (target == alias || target == spirit_alias) && (mask == expected_logical || mask == uint64_t{1} << 40);
        queued_worker.fill(0);
        *reinterpret_cast<int32_t*>(memory + 0x90) = count;
        *reinterpret_cast<int32_t*>(memory + 0xa8) = count;
        *reinterpret_cast<uintptr_t*>(memory + 0xc8) = target;
        *reinterpret_cast<uint64_t*>(memory + 0x88) = mask;
        *reinterpret_cast<Text*>(memory + 0x58) = {0, text, static_cast<int32_t>(std::strlen(text)), 0};
        return memory;
    }
    static void queue_fixture_command(uintptr_t manager_address, uintptr_t command) {
        if (command == reinterpret_cast<uintptr_t>(queued_worker.data()))
            valid = valid && !*reinterpret_cast<const int32_t*>(manager_address + 0xd90);
        queued_commands[0] = command;
        *reinterpret_cast<uintptr_t*>(manager_address + 0xcc0) = reinterpret_cast<uintptr_t>(queued_commands);
        *reinterpret_cast<int32_t*>(manager_address + 0xcc8) = 1;
    }
    static uintptr_t spawn_fixture_single(uintptr_t manager_address, uintptr_t output, uint64_t type, uintptr_t point,
                                          uintptr_t text) {
        if (!native_allowed) {
            retry_immediate(manager_address);
            return output;
        }
        alignas(8) uint8_t record[0x68]{};
        *reinterpret_cast<uint64_t*>(record) = type;
        const auto native_label = reinterpret_cast<const char*>(text);
        *reinterpret_cast<Text*>(record + 0x28) = {0, native_label, static_cast<int32_t>(std::strlen(native_label)), 0};
        spawn_detour(manager_address, reinterpret_cast<uintptr_t>(record), point, 0, 0);
        return output;
    }
    static uintptr_t spawn_fixture_archvile(uintptr_t manager_address, uintptr_t output, uintptr_t point,
                                            uintptr_t arch_template, uintptr_t text, const char* group) {
        valid = valid && arch_template == source_group && !std::strcmp(group, "raw_group");
        return original_single(manager_address, output, expected_logical, point, text);
    }
    static uintptr_t spawn_fixture_buff(uintptr_t manager_address, uintptr_t output, uintptr_t point, uintptr_t text,
                                        const char* group) {
        valid = valid && !std::strcmp(group, "raw_group");
        return original_single(manager_address, output, expected_logical, point, text);
    }
    static void spawn_fixture_spirit(uintptr_t manager_address, uint64_t hosts, const char* text, uintptr_t point,
                                     uint8_t first_flag, uint8_t flag, uintptr_t group, uintptr_t parent) {
        valid = valid && manager_address == owner && hosts == 16 && !first_flag && flag && !group && !parent && text &&
                std::strcmp(text, "host | filter") == 0;
        if (point_factory_detour(point, 0)) ++helper_records;
    }
    static void stagger_fixture(uintptr_t command) {
        alignas(8) uint8_t record[0x68]{};
        *reinterpret_cast<uint64_t*>(record) = *reinterpret_cast<uint64_t*>(command + 0x88);
        *reinterpret_cast<save::NativeString*>(record + 0x28) = *reinterpret_cast<save::NativeString*>(command + 0x58);
        if (group_detour(owner, reinterpret_cast<uint64_t*>(record), *reinterpret_cast<uintptr_t*>(command + 0xc8)))
            --*reinterpret_cast<int32_t*>(command + 0xa8);
        if (!*reinterpret_cast<int32_t*>(command + 0xa8)) *reinterpret_cast<uint8_t*>(command + 0xb9) = 1;
    }
    void prepare_actor_layout() {
        actor_table[0x88 / 8] = reinterpret_cast<uintptr_t>(
            +[](uintptr_t actor) -> uint32_t { return actor == actor_addresses[0] ? 1 : 2; });
        for (size_t i = 0; i < actor_storage.size(); ++i) {
            actor_storage[i] = reinterpret_cast<uintptr_t>(actor_table.data());
            actor_addresses[i] = reinterpret_cast<uintptr_t>(&actor_storage[i]);
        }
        root = reinterpret_cast<uintptr_t>(root_storage.data());
        library = reinterpret_cast<uintptr_t>(library_storage.data());
        alias = reinterpret_cast<uintptr_t>(alias_storage.data());
        source_group = reinterpret_cast<uintptr_t>(group_storage.data());
        source_point = reinterpret_cast<uintptr_t>(point_storage.data());
        owner = reinterpret_cast<uintptr_t>(manager.data());

        *reinterpret_cast<uintptr_t*>(world->data() + 0x62088) = reinterpret_cast<uintptr_t>(&collection);
    }
    void bind_recipe_fixture() {
        world_global = reinterpret_cast<uintptr_t>(&world_address);
        refresh_entity = refresh_actor;
        calls = attempts = registrations = nested_calls = 0;
        fail = nested = fault = unique_bodies = missing_source = false;
        valid = true;
        arena_point_name = nullptr;
        breach_fallback_available = false;
        expected_logical = 16;
        expected_override = 12;
        expected_maintenance_population = 3;
        spirit_alias = spirit_library = spirit_point = counter_library = 0;
        origin.assign(20, 'b');
        metadata = "EAP1|" + std::string(64, 'a') + "|game/test\n" + origin +
                   "|m|original_group|arena|0000000000000010|00000002|00000003|00000005|00000001\n";
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        for (size_t i = 0; i < names.size(); ++i) {
            char index[9];
            sprintf_s(index, "%08x", static_cast<unsigned>(i == 3 ? 1 : i));
            names[i] = "ap_v070_ai_r" + origin + "_" + index + "_q0000000200000003" + (i == 3 ? "_w00000001" : "") +
                       "_t00000000000000100000000000000020_p00000005000000010000000300000100_"
                       "l00000001000000010000000100000003_kn__ap_v070_s00100";
            *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(),
                                                                  static_cast<int32_t>(names[i].size()), 0};
        }
        *reinterpret_cast<uintptr_t*>(library + 0xc38) = reinterpret_cast<uintptr_t>(bodies.data());
        *reinterpret_cast<int32_t*>(library + 0xc40) = 4;
        original_definitions = 0x9911;
        *reinterpret_cast<uintptr_t*>(source_point + 0xc38) = original_definitions;
        *reinterpret_cast<int32_t*>(source_point + 0xc40) = 2;
        *reinterpret_cast<int32_t*>(source_point + 0xc54) = 1;
        *reinterpret_cast<int32_t*>(source_point + 0xd00) = 7;
        *reinterpret_cast<int32_t*>(source_point + 0xcd0) = 9;
        alias_name = "ap_v070_request_r" + origin;
        alias_text = alias_name.c_str();

        definition_name = entity_definition;
        find_entity = find_fixture_entity;
        original_point_factory = create_fixture_body;
        original_register_spawn = register_fixture_body;
        original_spawn = spawn_fixture_body;
        original_group = spawn_fixture_group;

        *reinterpret_cast<uintptr_t*>(worker.data() + 0x40) = owner;
        *reinterpret_cast<uintptr_t*>(worker.data() + 0xd8) = alias;
        *reinterpret_cast<Text*>(worker.data() + 0x58) = {0, label.c_str(), static_cast<int32_t>(label.size()), 0};
        *reinterpret_cast<int32_t*>(worker.data() + 0x90) = 2;
        *reinterpret_cast<int32_t*>(worker.data() + 0x94) = 3;
        original_maintain_tick = maintain_fixture;
    }
    void maintenance_retry_case() {
        stopped.store(false);
        fail = true;
        maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
        ok = attempts == 1 && !calls && recipe_spawn_traces.size() == 1 && recipe_state.requests().size() == 1 &&
             !recipe_state.requests().begin()->second.emitted && !recipe_state.requests().begin()->second.reserved &&
             !reserved_actors;
        first = selected_name;
        const std::string finite_origin(20, 'e');
        auto finite_recipe = native_recipes.at(origin);
        finite_recipe.definition.family = 'i';
        finite_recipe.definition.origin = finite_origin;
        native_recipes.emplace(finite_origin, std::move(finite_recipe));
        ok &= recipe_state.request("finite_priority", {"arena", finite_origin, "", 16, 2, 3, 3});
        worker_recipes[1] = "finite_priority";
        fail = false;
        maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
        ok &= attempts == 1 && !calls;
        recipe_state.cancel("finite_priority");
        worker_recipes.erase(1);
        native_recipes.erase(finite_origin);
        fail = true;
        collection.count = collection.capacity;
        maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
        ok = ok && attempts == 1 && !recipe_state.requests().begin()->second.emitted;
        collection.count = 0;
        fail = false;
        nested = true;
        maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
        ok = ok && calls == 1 && selected_name == first && nested_calls == 1;
    }
    void restore_maintenance_checkpoint() {
        ok = ok && recipe_state.actor_changed(recipe_actors.begin()->second, RecipeActorState::dead) &&
             !credit_at_most(recipe_state.deaths("arena", 16, label), 0) &&
             !restore_recipe_checkpoint("EAPC1|" + std::string(64, 'c') + "\n", "game/test") &&
             !restore_recipe_checkpoint("EAPC1|invalid\n", "game/test") &&
             restore_recipe_checkpoint(checkpoint, "game/test");
    }
    bool completed_maintenance_snapshot() {
        return valid && calls == 5 && registrations == 5 && selected_name == names[3] && !reserved_actors &&
               recipe_state.requests().begin()->second.emitted == 5 && recipe_state.requests().begin()->second.closed &&
               recipe_state.capture(snapshot) && restored.restore(snapshot, recipe_library.plan, recipe_library.map) &&
               *reinterpret_cast<uintptr_t*>(source_point + 0xc38) == original_definitions &&
               *reinterpret_cast<int32_t*>(source_point + 0xc40) == 2 &&
               *reinterpret_cast<int32_t*>(source_point + 0xc54) == 1 &&
               *reinterpret_cast<int32_t*>(source_point + 0xd00) == 7 &&
               *reinterpret_cast<int32_t*>(source_point + 0xcd0) == 9;
    }
    void maintenance_checkpoint_case() {
        ok = ok && capture_recipe_checkpoint(checkpoint);
        for (int i = 0; i < 4; ++i) maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));

        ok = ok && completed_maintenance_snapshot();
        restore_maintenance_checkpoint();
    }
    void maintenance_population_case() {
        map_freed();
        for (size_t i = 0; i < names.size(); ++i) {
            names[i].replace(names[i].find("00000100_l"), 8, "00000102");
            *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(),
                                                                  static_cast<int32_t>(names[i].size()), 0};
        }
        expected_maintenance_population = 5;
        *reinterpret_cast<int32_t*>(worker.data() + 0x90) = 2;
        *reinterpret_cast<int32_t*>(worker.data() + 0x94) = 3;
        *reinterpret_cast<int32_t*>(worker.data() + 0xc8) = 0;
        *reinterpret_cast<uint8_t*>(worker.data() + 0xcc) = 0;
        calls = attempts = registrations = 0;
        maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
        ok &= valid && calls == 1 && registrations == 1;
        for (size_t i = 0; i < names.size(); ++i) {
            names[i].replace(names[i].find("00000102_l"), 8, "00000100");
            *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(),
                                                                  static_cast<int32_t>(names[i].size()), 0};
        }
    }
    void faulted_maintenance_case() {
        *reinterpret_cast<uintptr_t*>(failed_worker.data() + 0x40) = owner;
        *reinterpret_cast<uintptr_t*>(failed_worker.data() + 0xd8) = alias;
        *reinterpret_cast<Text*>(failed_worker.data() + 0x58) = {0, label.c_str(), static_cast<int32_t>(label.size()),
                                                                 0};
        fault = true;
        ok = ok && recipe_fault_call(reinterpret_cast<uintptr_t>(failed_worker.data())) && !reserved_actors &&
             !recipe_creation && !recipe_state.requests().rbegin()->second.reserved &&
             !recipe_state.requests().rbegin()->second.emitted &&
             *reinterpret_cast<uintptr_t*>(source_point + 0xc38) == original_definitions;
        fault = false;
    }
    void maintenance_replay_case() {
        expected_maintenance_population = 3;
        map_freed();
        *reinterpret_cast<int32_t*>(worker.data() + 0x90) = 2;
        *reinterpret_cast<int32_t*>(worker.data() + 0x94) = 3;
        *reinterpret_cast<int32_t*>(worker.data() + 0xc8) = 0;
        *reinterpret_cast<uint8_t*>(worker.data() + 0xcc) = 0;
        calls = attempts = registrations = 0;
        maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
        ok = ok && calls == 1 && selected_name == first && recipe_state.requests().begin()->second.emitted == 1 &&
             credit_at_most(recipe_state.deaths("arena", 16, label), 0);
        for (int i = 0; i < 4; ++i) maintain_tick_detour(reinterpret_cast<uintptr_t>(worker.data()));
        ok = ok && valid && calls == 5 && registrations == 5 && selected_name == names[3] &&
             recipe_state.requests().size() == 1 && recipe_state.requests().begin()->second.closed;
    }
    void maintenance_fault_case() {
        maintenance_replay_case();
        faulted_maintenance_case();
        bool unlocked = false;
        const auto probe = CreateThread(
            nullptr, 0,
            [](void* output) -> DWORD {
                const auto acquired = admission_mutex.try_lock();
                if (acquired) admission_mutex.unlock();
                *static_cast<bool*>(output) = acquired;
                return 0;
            },
            &unlocked, 0, nullptr);
        ok = ok && probe && WaitForSingleObject(probe, 1000) == WAIT_OBJECT_0 && unlocked;
        if (probe) CloseHandle(probe);
    }
    void migration_case() {
        destination = reinterpret_cast<uintptr_t>(destination_storage.data());
        *reinterpret_cast<uintptr_t*>(manager_references.data() + 8) = owner;
        *reinterpret_cast<uintptr_t*>(manager_references.data() + 0x20 + 8) = destination;
        *reinterpret_cast<uintptr_t*>(world->data() + 0x126658) =
            reinterpret_cast<uintptr_t>(manager_references.data());
        *reinterpret_cast<int32_t*>(world->data() + 0x126660) = 2;
        collection.records = reinterpret_cast<uintptr_t>(actor_reference.data());
        collection.count = 1;
        *reinterpret_cast<uintptr_t*>(destination + 0xc80) = reinterpret_cast<uintptr_t>(migrated_record.data());
        *reinterpret_cast<int32_t*>(destination + 0xc88) = 1;
        *reinterpret_cast<uint32_t*>(migrated_record.data() + 8) = 1;
        *reinterpret_cast<uint32_t*>(migrated_record.data() + 12) = 1;
        *reinterpret_cast<uintptr_t*>(migrated_record.data() + 16) = actor_addresses[0];

        *reinterpret_cast<Text*>(migrated_record.data() + 0x28) = {0, migrated_label.c_str(),
                                                                   static_cast<int32_t>(migrated_label.size()), 0};
        resolve_actor = [](uintptr_t) -> uintptr_t { return actor_addresses[0]; };
        const auto cancelled_key = worker_recipes.at(reinterpret_cast<uintptr_t>(failed_worker.data()));
        const uintptr_t active_commands[]{reinterpret_cast<uintptr_t>(failed_worker.data())};
        *reinterpret_cast<uintptr_t*>(owner + 0xcc0) = reinterpret_cast<uintptr_t>(active_commands);
        *reinterpret_cast<int32_t*>(owner + 0xcc8) = 1;
        observe(world_address);
        ok = ok && credit_at_most(recipe_state.alive("arena", 16, "public_label"), 0) &&
             !credit_at_most(recipe_state.alive("destination", 16, "migrated"), 0) &&
             recipe_state.requests().begin()->second.request.owner == "arena" && !recipe_fault.load() &&
             !recipe_state.requests().at(cancelled_key).closed;
        *reinterpret_cast<int32_t*>(owner + 0xcc8) = 0;
        observe(world_address);
        ok = ok && recipe_state.requests().at(cancelled_key).closed &&
             !recipe_state.requests().at(cancelled_key).emitted && worker_recipes.empty() &&
             credit_at_most(recipe_state.deaths("arena", 16, "public_label"), 0);
        collection.count = 0;
        observe(world_address);
        ok = ok && recipe_state.requests().begin()->second.actors.empty() &&
             credit_at_most(recipe_state.deaths("destination", 16, "migrated"), 0) && recipe_state.capture(snapshot);
    }
    void bind_immediate_fixture() {
        native_allowed = true;
        native_admission = [](uintptr_t, int32_t) { return native_allowed; };
        allocate_worker = [](size_t size, uint32_t tag) -> uintptr_t {
            valid = valid && size == 0x110 && tag == 0x54;
            return allocation_allowed ? reinterpret_cast<uintptr_t>(queued_worker.data()) : 0;
        };
        construct_stagger = construct_fixture_stagger;
        assign_manager = [](uintptr_t reference, uintptr_t manager_address) {
            *reinterpret_cast<uintptr_t*>(reference + 8) = manager_address;
        };
        original_queue_command = queue_fixture_command;
        rewind_timeline = [](uintptr_t timeline, int32_t) {
            valid = valid && timeline == owner + 0xe20;
            ++retries;
        };
        original_single = spawn_fixture_single;
        original_archvile = spawn_fixture_archvile;
        original_buff = spawn_fixture_buff;
        original_spirit_helper = spawn_fixture_spirit;
        original_stagger_tick = stagger_fixture;
    }
    void queued_first_tick_case() {
        map_freed();
        native_allowed = true;
        allocation_allowed = false;
        calls = attempts = registrations = retries = helper_records = 0;
        const auto previous_cast = cast_stagger;
        const auto previous_remaining = original_remaining;
        cast_stagger = [](uintptr_t command) -> uintptr_t {
            return command == reinterpret_cast<uintptr_t>(queued_worker.data()) ? command : 0;
        };
        original_remaining = [](uintptr_t) -> uint64_t { return 1; };
        const auto queued = construct_stagger(reinterpret_cast<uintptr_t>(queued_worker.data()), 2, 0, 0, alias,
                                              expected_logical, label.c_str());
        assign_manager(queued + 0x38, owner);
        queue_command_detour(owner, queued);
        alignas(8) std::array<uint8_t, 0xd0> wait{};
        const auto waiting = reinterpret_cast<uintptr_t>(wait.data());
        *reinterpret_cast<uintptr_t*>(waiting + 0x40) = owner;
        *reinterpret_cast<Text*>(waiting + 0x58) = {0, "", 0, 0};
        *reinterpret_cast<uint64_t*>(waiting + 0x88) = encounter_ai_mask;
        const bool waits_before_first_tick =
            worker_recipes.size() == 1 && recipe_state.requests().size() == 1 && !calls && !remaining_result(waiting);
        if (!waits_before_first_tick) std::fprintf(stderr, "queued wave completed before its first tick\n");
        ok &= waits_before_first_tick;
        for (unsigned tick = 0; tick < 2; ++tick) stagger_tick_detour(queued);
        ok &= calls == 2 && recipe_state.requests().size() == 1 && recipe_state.requests().begin()->second.emitted == 2;
        cast_stagger = previous_cast;
        original_remaining = previous_remaining;
        map_freed();
        calls = attempts = registrations = retries = helper_records = 0;
    }
    void first_tick_cases(char family, int definition_count) {
        if (family == 'i' && !definition_count) {
            native_allowed = false;
            allocation_allowed = true;
            collection.count = 0;
            uintptr_t refused_result = 0;
            for (unsigned retry = 0; retry < 3; ++retry)
                dispatch_recipe_immediate(owner, 88, expected_logical, alias,
                                          reinterpret_cast<uintptr_t>(label.c_str()), false, refused_result);
            const bool deferred_once = recipe_state.requests().size() == 1 && pending_recipes.size() == 1 &&
                                       worker_recipes.empty() && !attempts && retries == 3;
            if (!deferred_once)
                std::fprintf(stderr, "refused spawn: requests=%zu pending=%zu workers=%zu attempts=%u retries=%u\n",
                             recipe_state.requests().size(), pending_recipes.size(), worker_recipes.size(), attempts,
                             retries);
            ok &= deferred_once;
            native_allowed = true;
            dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()),
                                      false, refused_result);
            ok &= recipe_state.requests().size() == 1 && pending_recipes.empty() && worker_recipes.size() == 1 &&
                  calls == 1;
            stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
            ok &= calls == 2 && recipe_state.requests().size() == 1 &&
                  recipe_state.requests().begin()->second.emitted == 2;
            queued_first_tick_case();
        }
    }
    static bool point_state_matches(const RecipeCreation::PointState& source_state) {
        const auto current = read_point_state(source_point);
        return current.definitions == source_state.definitions && current.count == source_state.count &&
               current.index == source_state.index && current.animation == source_state.animation &&
               current.state == source_state.state;
    }
    void immediate_failed_retry_case(const RecipeCreation::PointState& source_state, uintptr_t& result) {
        collection.count = collection.capacity;
        ok = ok &&
             dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()),
                                       false, result) &&
             result == 88 && !attempts && retries == 1 && !recipe_state.requests().begin()->second.emitted;
        collection.count = 0;
        fail = true;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false,
                                  result);
        const auto chosen = selected_name;
        ok = ok && attempts == 1 && !calls && retries == 2 && point_state_matches(source_state);
        fail = false;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false,
                                  result);
        ok = ok && calls == 1 && selected_name == chosen && retries == 3 &&
             recipe_state.requests().begin()->second.emitted == 1;
    }
    void immediate_queued_retry_case(const RecipeCreation::PointState& source_state, uintptr_t& result) {
        map_freed();
        calls = attempts = registrations = retries = helper_records = 0;
        collection.count = collection.capacity;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false,
                                  result);
        ok &= !attempts && !retries && pending_recipes.empty() && worker_recipes.size() == 1 &&
              !recipe_state.requests().begin()->second.emitted;
        collection.count = 0;
        for (int i = 0; i < 2; ++i) stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        ok &=
            valid && calls == 2 && recipe_state.requests().begin()->second.closed && point_state_matches(source_state);
    }
    bool immediate_emission_complete(const RecipeCreation::PointState& source_state) {
        return valid && calls == 2 && attempts == 3 && recipe_state.requests().begin()->second.emitted == 2 &&
               recipe_state.requests().begin()->second.closed && registrations == 2 && !helper_records &&
               !reserved_actors && !recipe_creation && !recipe_fault.load() && point_state_matches(source_state);
    }
    void immediate_admission_cases(const RecipeCreation::PointState& source_state) {
        uintptr_t result = 0;
        immediate_failed_retry_case(source_state, result);
        allocation_allowed = true;
        *reinterpret_cast<int32_t*>(owner + 0xd90) = 2;
        dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()), false,
                                  result);
        ok = ok && calls == 1 && pending_recipes.empty() && worker_recipes.size() == 1 &&
             *reinterpret_cast<const int32_t*>(owner + 0xd90) == 2;
        *reinterpret_cast<int32_t*>(owner + 0xd90) = 0;
        stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        ok = ok && immediate_emission_complete(source_state);
        immediate_queued_retry_case(source_state, result);
    }
    static uint32_t family_override(char family) {
        if (family == 'i') return 1;
        if (family == 'a') return 3;
        if (family == 'b') return 8;
        return 9;
    }
    static uint64_t family_logical_mask(char family, int definition_count) {
        if (family == 's') return uint64_t{1} << 40;
        if (family == 'a') return uint64_t{1} << 30;
        if (family == 'b') return uint64_t{1} << 37;
        if (definition_count == 2) return uint64_t{1} << 21;
        return uint64_t{1} << (definition_count ? 24 : 26);
    }
    static uint64_t family_physical_mask(char family, int definition_count) {
        if (family != 'i') return 32;
        if (definition_count == 2) return 2;
        return definition_count ? 16 : 1;
    }
    static RecipeCreation::PointState configure_family_types(char family, int definition_count) {
        const auto source_override = family_override(family);
        expected_override = family == 's' ? 10 : source_override;
        *reinterpret_cast<uint32_t*>(source_point + 0xd00) = source_override;
        const auto source_state = read_point_state(source_point);
        expected_logical = family_logical_mask(family, definition_count);
        expected_physical = family_physical_mask(family, definition_count);
        return source_state;
    }
    void immediate_family_case(char family, int definition_count) {
        map_freed();
        unique_bodies = true;
        *reinterpret_cast<int32_t*>(source_point + 0xc40) = definition_count;
        *reinterpret_cast<int32_t*>(source_point + 0xc54) = definition_count ? definition_count - 1 : -1;
        original_point_candidate = [](uintptr_t, uintptr_t point, uint64_t type) -> uint64_t {
            if (*reinterpret_cast<int32_t*>(point + 0xc40) != 1) return 0;
            const auto name = *reinterpret_cast<const char**>(*reinterpret_cast<uintptr_t*>(point + 0xc38) + 8);
            const auto tracking = spawn_tracking(name);
            return tracking && tracking->physical == type;
        };
        original_point_index = [](uintptr_t manager_address, uintptr_t point, uint64_t type) -> int32_t {
            return original_point_candidate(manager_address, point, type) ? 0 : -1;
        };
        const auto source_state = configure_family_types(family, definition_count);
        char logical[17];
        sprintf_s(logical, "%016llx", expected_logical);
        char physical[17];
        sprintf_s(physical, "%016llx", expected_physical);
        metadata = "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin + "|" + family + "|original_point|arena|" +
                   logical + "|00000001|00000002|00000002|00000001|" + (family == 'a' ? "arch_template" : "") + "|" +
                   (family == 's' ? "686f7374207c2066696c746572" : "7075626c69635f6c6162656c") +
                   (family == 's' ? "||0000000000000010|01\n" : "|7261775f67726f7570|0000000000000000|00\n");
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        *reinterpret_cast<int32_t*>(library + 0xc40) = 2;
        for (unsigned i = 0; i < 2; ++i) {
            names[i] = "ap_v070_ai_r" + origin + "_0000000" + std::to_string(i) + "_q0000000100000002_t" + logical +
                       physical + std::string("_l00000001000000010000000100000003_kn__ap_v070_s00100");
            *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(),
                                                                  static_cast<int32_t>(names[i].size()), 0};
        }
        calls = attempts = registrations = retries = helper_records = 0;
        fail = false;
        allocation_allowed = false;
        first_tick_cases(family, definition_count);
        immediate_admission_cases(source_state);
    }
    void empty_bridge_dispatch(unsigned empty_attempts) {
        uintptr_t empty_result = 0;
        ok &= dispatch_recipe_immediate(owner, 88, expected_logical, alias, reinterpret_cast<uintptr_t>(label.c_str()),
                                        true, empty_result) &&
              empty_result == 88 && recipe_state.requests().empty() && pending_recipes.empty() &&
              worker_recipes.empty() && attempts == empty_attempts && bridge_calls == 1;
    }
    void empty_bridge_replay(uintptr_t empty_worker, unsigned empty_attempts, NativeRecipe& empty_group) {
        bool bridge_marked = false;
        ok &= !prepare_recipe_worker(empty_worker, false, bridge_marked) && bridge_marked && bridge_calls == 2;
        std::string empty_key;
        ok &= open_native_request(owner, empty_group, label, empty_key);
        worker_recipes[empty_worker] = empty_key;
        *reinterpret_cast<int32_t*>(empty_worker + 0xa8) = 1;
        *reinterpret_cast<uint8_t*>(empty_worker + 0xb9) = 0;
        stagger_tick_detour(empty_worker);
        ok &= recipe_state.requests().at(empty_key).closed && !recipe_state.requests().at(empty_key).emitted &&
              worker_recipes.empty() && attempts == empty_attempts && bridge_calls == 3 && valid &&
              credit_at_most(recipe_state.deaths("arena", expected_logical, label), 0);
    }
    void empty_bridge_case() {
        map_freed();
        ok &= load_native_recipes();
        auto& empty_group = native_recipes.at(origin);
        empty_group.definition.family = 'g';
        empty_group.definition.source.clear();
        empty_group.definition.owner = "game_encounter_manager_traindepot_1";
        const auto empty_multiple = original_multiple;

        bridge_calls = 0;
        original_multiple = [](uintptr_t manager, uintptr_t output, uint64_t mask, int32_t count, uintptr_t group,
                               uintptr_t label) -> uintptr_t {
            valid &= manager == owner && mask == expected_logical && count == 1 && !group &&
                     !std::strcmp(reinterpret_cast<const char*>(label), "public_label");
            ++bridge_calls;
            return output;
        };
        const auto empty_attempts = attempts;
        empty_bridge_dispatch(empty_attempts);
        const auto empty_worker = construct_stagger(reinterpret_cast<uintptr_t>(queued_worker.data()), 1, 0, 0, alias,
                                                    expected_logical, label.c_str());
        assign_manager(empty_worker + 0x38, owner);
        stagger_tick_detour(empty_worker);
        ok &= *reinterpret_cast<const uint8_t*>(empty_worker + 0xb9) &&
              !*reinterpret_cast<const int32_t*>(empty_worker + 0xa8) && recipe_state.requests().empty() &&
              attempts == empty_attempts && bridge_calls == 2;
        stagger_tick_detour(empty_worker);
        ok &= bridge_calls == 2;
        empty_bridge_replay(empty_worker, empty_attempts, empty_group);
        original_multiple = empty_multiple;
    }
    void arena_eligibility_case() {
        map_freed();
        calls = attempts = registrations = nested_calls = 0;
        fail = nested = fault = unique_bodies = false;
        collection.count = 0;
        ok &= load_native_recipes();
        auto& arena_group = native_recipes.at(origin);
        arena_group.definition.family = 'g';
        arena_group.definition.owner = "ai_encounter_manager_dlc1_skyscraper_upper_last";
        arena_group.definition.source = "ai_encounter_spawn_group_zone_skyscraper_upper_last";
        const auto arena_candidate = original_point_candidate;
        const auto arena_index = original_point_index;
        original_point_candidate = [](uintptr_t, uintptr_t, uint64_t) -> uint64_t { return 1; };
        original_point_index = [](uintptr_t, uintptr_t, uint64_t) -> int32_t { return 3; };
        RecipeCreation arena_creation;
        ok &= begin_recipe_creation(arena_creation, owner, origin, label);
        const auto arena_key = arena_creation.key;
        const auto arena_attempts = attempts;
        for (const auto point : {"ai_target_spawn_35", "ai_target_spawn_85", "ai_target_spawn_86",
                                 "ai_target_spawn_146", "ai_target_spawn_147"}) {
            arena_point_name = point;
            ok &= !point_candidate_detour(owner, source_point, expected_logical) &&
                  point_index_detour(owner, source_point, expected_logical) == -1 && !recipe_factory(source_point, 0);
        }
        for (const auto point : {"ai_target_spawn_96", "ai_target_spawn_97", "ai_target_spawn_98", "ai_target_spawn_99",
                                 "ai_target_spawn_100", "ai_target_spawn_101", "ai_target_spawn_102"}) {
            arena_point_name = point;
            ok &= point_candidate_detour(owner, source_point, expected_logical) == 1 &&
                  point_index_detour(owner, source_point, expected_logical) == 3;
        }
        arena_point_name = "ai_target_spawn_147";
        arena_group.definition.family = 'm';
        ok &= point_candidate_detour(owner, source_point, expected_logical) == 1;
        arena_group.definition.family = 'g';
        arena_group.definition.owner = "another_arena";
        ok &= point_candidate_detour(owner, source_point, expected_logical) == 1;
        arena_group.definition.owner = "ai_encounter_manager_dlc1_skyscraper_upper_last";
        end_recipe_creation(arena_creation);
        ok &= attempts == arena_attempts && !recipe_state.requests().at(arena_key).emitted &&
              !recipe_state.requests().at(arena_key).closed && !recipe_state.requests().at(arena_key).reserved;
        arena_point_name = "ai_target_spawn_97";
        ok &= begin_recipe_creation(arena_creation, owner, origin, label) && recipe_factory(source_point, 0);
        end_recipe_creation(arena_creation);
        ok &= recipe_state.requests().at(arena_key).emitted == 1 && !reserved_actors && !recipe_fault.load();
        arena_point_name = nullptr;
        original_point_candidate = arena_candidate;
        original_point_index = arena_index;
    }
    static bool breach_point_matches(const RecipeCreation::PointState& breach_state, const uint8_t* breach_record) {
        for (const auto point : {source_point, alias}) {
            const auto state = read_point_state(point);
            if (state.definitions != breach_state.definitions || state.count != breach_state.count ||
                state.index != breach_state.index || state.animation != breach_state.animation ||
                state.state != breach_state.state)
                return false;
        }
        return *reinterpret_cast<const uint64_t*>(breach_record) == expected_logical;
    }
    void recover_breach_source(uint8_t* breach_record, const RecipeCreation::PointState& breach_state,
                               const std::string& breach_key) {
        fail = false;
        ok &= create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin) == 1 &&
              calls == 1 && !breach_group_calls && recipe_state.requests().at(breach_key).emitted == 1 &&
              !reserved_actors && !recipe_fault.load() && breach_point_matches(breach_state, breach_record) && valid;
        ok &= !create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin) && calls == 1;
    }
    void breach_fallback_case() {
        map_freed();
        expected_logical = 0x100;
        expected_physical = 0x800000;
        metadata = "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin +
                   "|g|breach_wasteland_arena_encounter_spawn_group_zone_2|_example_encounter_manager_0|"
                   "0000000000000100|00000001|00000001|00000001|00000001||||0000000000000000|00\n";
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        names[0] = "ap_v070_ai_r" + origin +
                   "_00000000_q0000000100000001_t00000000000001000000000000800000_l00000001000000010000000100000003_kn_"
                   "_ap_v070_s00100";
        *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
        *reinterpret_cast<int32_t*>(library + 0xc40) = 1;
        calls = attempts = registrations = 0;
        unique_bodies = false;
        alias_storage = point_storage;
        alignas(8) uint8_t breach_record[0x68]{};
        *reinterpret_cast<uint64_t*>(breach_record) = expected_logical;
        *reinterpret_cast<Text*>(breach_record + 0x28) = {0, label.c_str(), static_cast<int32_t>(label.size()), 0};
        const auto breach_state = read_point_state(source_point);

        const auto breach_group = original_group;

        breach_group_calls = 0;
        original_group = [](uintptr_t, uint64_t*, uintptr_t) -> uint64_t {
            ++breach_group_calls;
            return 0;
        };
        ok &= load_native_recipes();
        auto& breach_recipe = native_recipes.at(origin);
        breach_recipe.definition.owner = "another_arena";
        missing_source = breach_fallback_available = true;
        RecipeCreation unrelated_group;
        ok &= begin_recipe_creation(unrelated_group, owner, origin, label) && !unrelated_group.source;
        end_recipe_creation(unrelated_group);
        breach_recipe.definition.owner = "_example_encounter_manager_0";
        breach_fallback_available = false;
        missing_source = true;
        ok &= !create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin);
        const auto breach_key = pending_recipes.at({owner, origin});
        ok &= !attempts && !breach_group_calls && !recipe_state.requests().at(breach_key).emitted &&
              !recipe_state.requests().at(breach_key).reserved && breach_point_matches(breach_state, breach_record);
        breach_fallback_available = true;
        fail = true;
        ok &= !create_from_recipe(owner, reinterpret_cast<uintptr_t>(breach_record), 0, 0, true, origin) &&
              attempts == 1 && !calls && !recipe_state.requests().at(breach_key).emitted &&
              !recipe_state.requests().at(breach_key).reserved && breach_point_matches(breach_state, breach_record);
        recover_breach_source(breach_record, breach_state, breach_key);
        missing_source = breach_fallback_available = false;
        original_group = breach_group;
    }
    void check_possessed_child() {
        const auto child_key = worker_recipes.begin()->second;
        const auto host_key = recipe_state.requests().at(child_key).request.parent_request;
        ok = ok && !host_key.empty() && recipe_state.requests().at(child_key).request.parent_actor == host_key + "/0";
        stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        ok = ok && calls == 1 && !recipe_state.requests().at(child_key).emitted;
        collection.count = 0;
        ok = ok && recipe_state.actor_changed(host_key + "/0", RecipeActorState::dead);
        stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        ok = ok && valid && calls == 2 && registrations == 2 && !helper_records &&
             recipe_state.requests().at(child_key).emitted == 1 && recipe_state.requests().at(child_key).closed &&
             recipe_state.capture(snapshot) && restored.restore(snapshot, recipe_library.plan, recipe_library.map) &&
             !recipe_fault.load();
    }
    void possessed_host_case() {
        map_freed();
        expected_logical = 16;

        spirit_point_storage = point_storage;
        spirit_alias = reinterpret_cast<uintptr_t>(spirit_alias_storage.data());
        spirit_alias_text = spirit_alias_name.c_str();
        spirit_library = reinterpret_cast<uintptr_t>(spirit_library_storage.data());
        spirit_point = reinterpret_cast<uintptr_t>(spirit_point_storage.data());
        metadata = "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin +
                   "|p|original_point|arena|0000000000000010|00000001|00000001|00000001|00000001||"
                   "7075626c69635f6c6162656c||0000000000000000|00\n" +
                   spirit_origin +
                   "|s|original_spirit_point|arena|0000010000000000|00000001|00000001|00000001|00000001||"
                   "686f7374207c2066696c746572||0000000000000010|01\n";
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        names[0] = "ap_v070_ai_r" + origin +
                   "_00000000_q0000000100000001_t00000000000000100000000000000020_l00000001000000010000000100000003_kn_"
                   "_ap_v070_s00100";
        names[1] = "ap_v070_ai_r" + spirit_origin +
                   "_00000000_q0000000100000001_t00000100000000000000000000000020_l00000001000000010000000100000003_kn_"
                   "_ap_v070_s00100";
        *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
        *reinterpret_cast<int32_t*>(library + 0xc40) = 1;
        *reinterpret_cast<Text*>(spirit_body.data()) = {0, names[1].data(), static_cast<int32_t>(names[1].size()), 0};
        *reinterpret_cast<uintptr_t*>(spirit_library + 0xc38) = reinterpret_cast<uintptr_t>(spirit_body.data());
        *reinterpret_cast<int32_t*>(spirit_library + 0xc40) = 1;

        host_collection = &collection;
        host_single = original_single;
        original_single = [](uintptr_t manager_address, uintptr_t output, uint64_t type, uintptr_t point,
                             uintptr_t text) -> uintptr_t {
            const auto result = host_single(manager_address, output, type, point, text);
            if (calls) host_collection->count = host_collection->capacity;
            return result;
        };
        calls = attempts = registrations = retries = helper_records = 0;
        allocation_allowed = true;
        collection.count = collection.capacity - 1;
        const auto compound = possessed_detour(owner, 99, 16, alias, reinterpret_cast<uintptr_t>(label.c_str()),
                                               spirit_alias, 16, reinterpret_cast<uintptr_t>(label.c_str()), 1);
        ok = ok && compound == 99 && calls == 1 && registrations == 1 && !helper_records && pending_recipes.empty() &&
             worker_recipes.size() == 1 && !retries && !reserved_actors;
        check_possessed_child();
    }
    std::string pair_child, pair_parent;
    unsigned pair_completed_calls = 0, pair_retry_count = 0;
    uint64_t invoke_pair() {
        return possessed_detour(owner, 99, 16, alias, reinterpret_cast<uintptr_t>(label.c_str()), spirit_alias, 16,
                                reinterpret_cast<uintptr_t>(label.c_str()), 1);
    }
    void defer_possessed_parent() {
        native_allowed = false;
        for (unsigned retry = 0; retry < 3; ++retry)
            ok &= invoke_pair() == 99 && !calls && !attempts && pending_recipes.size() == 2 &&
                  recipe_state.requests().size() == 2;
    }
    void failed_possessed_retries(unsigned body_count) {
        allocation_allowed = false;
        collection.count = 0;
        fail = true;
        pair_completed_calls = calls;
        pair_retry_count = retries;
        for (unsigned retry = 0; retry < 3; ++retry) {
            collection.count = 0;
            ok &= invoke_pair() == 99 && calls == pair_completed_calls && recipe_state.requests().size() == 2 &&
                  pending_recipes.at({owner, spirit_origin}) == pair_child &&
                  recipe_state.requests().at(pair_child).request.parent_request == pair_parent &&
                  recipe_state.requests().at(pair_child).request.parent_actor == pair_parent + "/0" &&
                  !recipe_state.requests().at(pair_child).emitted &&
                  recipe_state.requests().at(pair_parent).emitted == body_count &&
                  recipe_state.requests().at(pair_parent).dead == body_count &&
                  recipe_state.requests().at(pair_parent).closed == (body_count == 2) &&
                  credit_at_least(recipe_state.deaths("arena", 16, label), 1);
        }
        ok &= retries == pair_retry_count + 3 && !reserved_actors;
    }
    void partial_child_retries(unsigned body_count) {
        if (body_count == 2) {
            fail = false;
            collection.count = 0;
            ok &= invoke_pair() == 99 && calls == pair_completed_calls + 1 &&
                  pending_recipes.at({owner, spirit_origin}) == pair_child &&
                  recipe_state.requests().at(pair_child).actors.at(pair_child + "/0").ordinal == 0 &&
                  recipe_state.actor_changed(pair_child + "/0", RecipeActorState::dead);
            for (unsigned retry = 0; retry < 3; ++retry) {
                collection.count = 0;
                ok &= invoke_pair() == 99 && calls == pair_completed_calls + 1 &&
                      pending_recipes.at({owner, spirit_origin}) == pair_child &&
                      recipe_state.requests().at(pair_child).request.parent_request == pair_parent &&
                      recipe_state.requests().at(pair_child).emitted == 1 &&
                      recipe_state.requests().at(pair_child).dead == 1 &&
                      !credit_at_most(recipe_state.deaths("arena", uint64_t{1} << 40, ""), 0);
            }
        }
    }
    void recover_possessed_pair(unsigned body_count) {
        fail = false;
        allocation_allowed = true;
        collection.count = 0;
        const auto recovery_calls = calls;
        ok &= invoke_pair() == 99 && calls == recovery_calls + (body_count == 1 ? 1u : 0u) && pending_recipes.empty();
        collection.count = 0;
        if (body_count == 2) stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
        const auto& recovered = recipe_state.requests().at(pair_child);
        ok &= calls == body_count * 2 && recovered.emitted == body_count && recovered.closed == (body_count == 2) &&
              recovered.dead == (body_count == 2 ? 1u : 0u) && recovered.actors.size() == 1 &&
              recovered.actors.at(pair_child + (body_count == 1 ? "/0" : "/1")).ordinal == body_count - 1 &&
              recipe_state.requests().at(pair_parent).dead == body_count && valid && !recipe_fault.load();
    }
    bool possessed_pair_case(unsigned body_count) {
        map_freed();
        metadata =
            "EAP2|" + std::string(64, 'a') + "|game/test\n" + origin +
            "|p|original_point|arena|0000000000000010|00000001|0000000" + std::to_string(body_count) + "|0000000" +
            std::to_string(body_count) + "|00000001||7075626c69635f6c6162656c||0000000000000000|00\n" + spirit_origin +
            "|s|original_spirit_point|arena|0000010000000000|00000001|0000000" + std::to_string(body_count) +
            "|0000000" + std::to_string(body_count) + "|00000001||686f7374207c2066696c746572||0000000000000010|01\n";
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        std::array<uint8_t, 0x100> child_bodies{};
        for (unsigned i = 0; i < body_count; ++i) {
            names[i] = "ap_v070_ai_r" + origin + "_0000000" + std::to_string(i) + "_q000000010000000" +
                       std::to_string(body_count) +
                       "_t00000000000000100000000000000020_l00000001000000010000000100000003_kn__ap_v070_s00100";
            names[i + 2] = "ap_v070_ai_r" + spirit_origin + "_0000000" + std::to_string(i) + "_q000000010000000" +
                           std::to_string(body_count) +
                           "_t00000100000000000000000000000020_l00000001000000010000000100000003_kn__ap_v070_s00100";
            *reinterpret_cast<Text*>(bodies.data() + i * 0x80) = {0, names[i].data(),
                                                                  static_cast<int32_t>(names[i].size()), 0};
            *reinterpret_cast<Text*>(child_bodies.data() + i * 0x80) = {0, names[i + 2].data(),
                                                                        static_cast<int32_t>(names[i + 2].size()), 0};
        }
        *reinterpret_cast<int32_t*>(library + 0xc40) = static_cast<int32_t>(body_count);
        *reinterpret_cast<uintptr_t*>(spirit_library + 0xc38) = reinterpret_cast<uintptr_t>(child_bodies.data());
        *reinterpret_cast<int32_t*>(spirit_library + 0xc40) = static_cast<int32_t>(body_count);
        *reinterpret_cast<int32_t*>(owner + 0xcc8) = 0;

        pair_queue = original_queue_command;
        original_queue_command = [](uintptr_t manager_address, uintptr_t command) {
            pair_queue(manager_address, command);
            allocation_allowed = false;
        };
        calls = attempts = registrations = retries = helper_records = 0;
        allocation_allowed = body_count == 2;
        native_allowed = true;
        unique_bodies = true;
        collection.count = 0;

        defer_possessed_parent();
        const auto refused_parent = pending_recipes.at({owner, origin});
        const auto refused_child = pending_recipes.at({owner, spirit_origin});
        native_allowed = true;
        const auto initial_pair = invoke_pair();
        ok &= initial_pair == 99 && calls == 1 && pending_recipes.count({owner, spirit_origin}) == 1;
        if (!pending_recipes.count({owner, spirit_origin})) {
            original_queue_command = pair_queue;
            return false;
        }
        pair_child = pending_recipes.at({owner, spirit_origin});
        pair_parent = recipe_state.requests().at(pair_child).request.parent_request;
        ok &= pair_parent == refused_parent && pair_child == refused_child;
        ok &= recipe_state.actor_changed(pair_parent + "/0", RecipeActorState::dead);
        if (body_count == 2) {
            collection.count = 0;
            stagger_tick_detour(reinterpret_cast<uintptr_t>(queued_worker.data()));
            ok &= calls == 2 && recipe_state.actor_changed(pair_parent + "/1", RecipeActorState::dead);
            *reinterpret_cast<int32_t*>(owner + 0xcc8) = 0;
        }
        failed_possessed_retries(body_count);
        partial_child_retries(body_count);
        recover_possessed_pair(body_count);
        original_queue_command = pair_queue;

        return true;
    }
    void check_empty_possessed_host() {
        calls = attempts = registrations = retries = helper_records = 0;
        collection.count = 0;
        ok = ok &&
             possessed_detour(owner, 99, 16, alias, reinterpret_cast<uintptr_t>(label.c_str()), spirit_alias, 16,
                              reinterpret_cast<uintptr_t>(label.c_str()), 1) == 99 &&
             valid && calls == 1 && delay_count == 1 && registrations == 1 && !helper_records &&
             pending_recipes.empty() && recipe_state.requests().begin()->second.empty_completed &&
             !recipe_state.requests().rbegin()->second.request.parent_actor.size();
    }
    void empty_possessed_host_case() {
        map_freed();
        *reinterpret_cast<uintptr_t*>(spirit_library + 0xc38) = reinterpret_cast<uintptr_t>(spirit_body.data());
        *reinterpret_cast<int32_t*>(spirit_library + 0xc40) = 1;
        metadata = pair_metadata;
        names = pair_names;
        *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
        *reinterpret_cast<Text*>(spirit_body.data()) = {0, names[1].data(), static_cast<int32_t>(names[1].size()), 0};
        metadata.replace(metadata.find("00000001|00000001|00000001|00000001"), 35,
                         "00000001|00000000|00000000|00000000");
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        *reinterpret_cast<int32_t*>(library + 0xc40) = 0;

        delay_count = 0;
        allocate_worker = [](size_t size, uint32_t tag) -> uintptr_t {
            valid = valid && size == 0xb8 && tag == 0x54;
            return reinterpret_cast<uintptr_t>(native_delay.data());
        };
        native_clock = [](uintptr_t, uint64_t* value, int32_t channel) -> uint64_t* {
            valid = valid && channel == 1;
            *value = 43;
            return value;
        };
        native_step = [](uintptr_t, int32_t channel) {
            valid = valid && channel == 1;
            return 0.125f;
        };
        native_duration = [](uint64_t* value, float seconds) -> uint64_t* {
            valid = valid && seconds == 0.25f;
            *value = 250;
            return value;
        };
        construct_time = [](uintptr_t memory, uint64_t current, uint64_t duration, uint64_t initial) -> uintptr_t {
            valid = valid && current == 43 && duration == 250 && initial == 43;
            ++delay_count;
            return memory;
        };
        check_empty_possessed_host();
    }
    void countermeasure_library_cases() {
        original_single = host_single;
        collection.count = 0;
        map_freed();
        metadata = "EAP1|" + std::string(64, 'a') + "|game/test\n" + origin +
                   "|p|original_point|arena|0000000000000010|00000001|00000001|00000001|00000001\n";
        *reinterpret_cast<Text*>(root + 0xbe0) = {0, metadata.data(), static_cast<int32_t>(metadata.size()), 0};
        *reinterpret_cast<int32_t*>(library + 0xc40) = 1;

        counter_name = names[0];
        counter_name.replace(counter_name.find("_t00000000000000100000000000000020"), 34,
                             "_t00000000000000100000000000000040");
        counter_body = {0, counter_name.c_str(), static_cast<int32_t>(counter_name.size()), 0};
        counter_library = reinterpret_cast<uintptr_t>(counter_storage.data());
        *reinterpret_cast<uintptr_t*>(counter_library + 0xc38) = reinterpret_cast<uintptr_t>(&counter_body);
        *reinterpret_cast<int32_t*>(counter_library + 0xc40) = 1;
        ok &= load_native_recipes() && native_recipes.at(origin).stone_alternatives.size() == 1 &&
              countermeasure_body(native_recipes.at(origin), reinterpret_cast<uintptr_t>(bodies.data()), false, false,
                                  false) == reinterpret_cast<uintptr_t>(&counter_body);
        map_freed();
        const auto stone_primary = names[0], stone_alternate = counter_name;
        names[0].replace(names[0].find("_t00000000000000100000000000000020"), 34, "_t00000000000000100000000020000000");
        counter_name.replace(counter_name.find("_t00000000000000100000000000000040"), 34,
                             "_t00000000000000100000000010000000");
        *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
        counter_body = {0, counter_name.data(), static_cast<int32_t>(counter_name.size()), 0};
        ok &= load_native_recipes() && native_recipes.at(origin).armored_alternatives.size() == 1 &&
              native_recipes.at(origin).stone_alternatives.empty() &&
              countermeasure_body(native_recipes.at(origin), reinterpret_cast<uintptr_t>(bodies.data()), false, false,
                                  false, false) == reinterpret_cast<uintptr_t>(&counter_body) &&
              countermeasure_body(native_recipes.at(origin), reinterpret_cast<uintptr_t>(bodies.data()), false, false,
                                  false, true) == reinterpret_cast<uintptr_t>(bodies.data());
        map_freed();
        names[0] = stone_primary;
        counter_name = stone_alternate;
        *reinterpret_cast<Text*>(bodies.data()) = {0, names[0].data(), static_cast<int32_t>(names[0].size()), 0};
        counter_body = {0, counter_name.data(), static_cast<int32_t>(counter_name.size()), 0};
        ok &= load_native_recipes();
        alias_storage = point_storage;
        missing_source = true;
        RecipeCreation fallback;
        ok &= begin_recipe_creation(fallback, owner, origin, label) && fallback.source == alias;
        end_recipe_creation(fallback);
        *reinterpret_cast<int32_t*>(alias + 0xc40) = 0;
        RecipeCreation empty_alias;
        ok &= begin_recipe_creation(empty_alias, owner, origin, label) && !empty_alias.source;
        end_recipe_creation(empty_alias);
        missing_source = false;
        auto& bounded = native_recipes.at(origin);
        bounded.definition.family = 'm';
        bounded.definition.maximum_emissions = bounded.definition.final_credit_units = 0;
        bounded.refill_limit = 7;
        bool marked = false;
        const auto previous_worker = worker_recipe;
        ok &= prepare_recipe_worker(reinterpret_cast<uintptr_t>(failed_worker.data()), true, marked) && marked &&
              *reinterpret_cast<int32_t*>(failed_worker.data() + 0x94) == 7 &&
              !recipe_state.requests().at(*worker_recipe).request.maximum_emissions;
        worker_recipe = previous_worker;
        map_freed();
        *reinterpret_cast<int32_t*>(counter_library + 0xc40) = 0;
        ok &= !load_native_recipes();
    }
    bool run() {

        prepare_actor_layout();
        const auto previous_world = world_global;
        const auto previous_name = definition_name;
        const auto previous_find = find_entity;
        const auto previous_factory = original_point_factory;
        const auto previous_group = original_group;
        const auto previous_spawn = original_spawn;
        const auto previous_register = original_register_spawn;
        const auto previous_candidate = original_point_candidate;
        const auto previous_index = original_point_index;
        const auto previous_tick = original_maintain_tick;
        const auto previous_resolve = resolve_actor;
        const auto previous_refresh = refresh_entity;
        const auto previous_single = original_single;
        const auto previous_archvile = original_archvile;
        const auto previous_buff = original_buff;
        const auto previous_helper = original_spirit_helper;
        const auto previous_allocate = allocate_worker;
        const auto previous_construct = construct_stagger;
        const auto previous_assign = assign_manager;
        const auto previous_queue = original_queue_command;
        const auto previous_stagger = original_stagger_tick;
        const auto previous_rewind = rewind_timeline;
        const auto previous_clock = native_clock;
        const auto previous_step = native_step;
        const auto previous_duration = native_duration;
        const auto previous_time = construct_time;
        bind_recipe_fixture();
        maintenance_retry_case();
        maintenance_checkpoint_case();
        maintenance_population_case();
        maintenance_fault_case();
        migration_case();
        const auto previous_admission = native_admission;
        bind_immediate_fixture();
        for (const auto [family, definition_count] :
             {std::pair{'i', 0}, {'i', 1}, {'i', 2}, {'a', 2}, {'b', 2}, {'s', 2}}) {
            immediate_family_case(family, definition_count);
        }
        empty_bridge_case();
        arena_eligibility_case();
        breach_fallback_case();
        possessed_host_case();
        pair_metadata = metadata;
        pair_names = names;
        for (unsigned body_count : {1u, 2u}) {
            if (!possessed_pair_case(body_count)) break;
        }
        empty_possessed_host_case();
        countermeasure_library_cases();
        map_freed();
        stopped.store(true);
        world_global = previous_world;
        definition_name = previous_name;
        find_entity = previous_find;
        original_point_factory = previous_factory;
        original_group = previous_group;
        original_spawn = previous_spawn;
        original_register_spawn = previous_register;
        original_maintain_tick = previous_tick;
        resolve_actor = previous_resolve;
        refresh_entity = previous_refresh;
        original_point_candidate = previous_candidate;
        original_point_index = previous_index;
        original_single = previous_single;
        original_archvile = previous_archvile;
        original_buff = previous_buff;
        original_spirit_helper = previous_helper;
        allocate_worker = previous_allocate;
        construct_stagger = previous_construct;
        assign_manager = previous_assign;
        original_queue_command = previous_queue;
        original_stagger_tick = previous_stagger;
        rewind_timeline = previous_rewind;
        native_admission = previous_admission;
        native_clock = previous_clock;
        native_step = previous_step;
        native_duration = previous_duration;
        construct_time = previous_time;
        return ok;
    }
};
bool test_recipe_inputs() { RecipeInputTest test; return test.run(); }

#endif

}
