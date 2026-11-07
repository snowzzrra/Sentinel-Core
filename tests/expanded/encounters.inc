namespace sentinel::expanded::detail {

#ifdef SC_NATIVE_TESTING

uintptr_t post_one_detour(uintptr_t target, uintptr_t event, int32_t delay, uintptr_t argument) { return post_one_detour_impl(target, event, delay, argument, _ReturnAddress()); }

#endif

}

namespace sentinel::expanded {
using namespace detail;

#ifdef SC_NATIVE_TESTING

struct QuantityInputTest {
    struct Actor {
        std::array<uintptr_t, 224> table{};
        std::array<uint8_t, 0xb0> bytes{};
        bool dead = false, ignored = false;
        std::string name;
        float health = 100;
    };
    inline static std::array<Actor, 6> actors{};
    inline static unsigned native_calls = 0;
    std::array<uint8_t, 0xdb8> manager{};
    std::array<uint8_t, 0xc8> command{};
    std::array<uint8_t, 0x68 * 6> records{};
    std::array<uint8_t, 0x140> future_worker{};
    uintptr_t future_commands[1]{reinterpret_cast<uintptr_t>(future_worker.data())};
    std::array<uint8_t, 0xb0> kill_command{};
    std::array<uint8_t, 0xd0> health_command{};
    std::array<uint8_t, 0x68 * 6> health_records{};
    std::array<uint8_t, 0xc8> class_wait{}, any_wait{};
    std::array<std::array<uint64_t, 13>, 6> class_records{}, any_records{};
    uintptr_t cmd = reinterpret_cast<uintptr_t>(command.data());
    uintptr_t kill = reinterpret_cast<uintptr_t>(kill_command.data());
    uintptr_t health = reinterpret_cast<uintptr_t>(health_command.data());
    uintptr_t class_command = reinterpret_cast<uintptr_t>(class_wait.data());
    uintptr_t any_command = reinterpret_cast<uintptr_t>(any_wait.data());
    bool ok = false, scope_ok = true;
    static constexpr uint64_t arachnotron = uint64_t{1} << 22, spectre = uint64_t{1} << 17,
                              cacodemon = uint64_t{1} << 18, hell_knight = uint64_t{1} << 14,
                              spirit = uint64_t{1} << 40, flag = uint64_t{1} << 60;
    const std::string encounter_label = "class_selection", other_label = "other_label";
    static save::NativeString text(const std::string& value) {
        return {0, const_cast<char*>(value.c_str()), static_cast<int32_t>(value.size()), 0x40000000U, {}};
    }
    static std::string body_name(unsigned origin_index, unsigned body, unsigned count, uint64_t source,
                                 uint64_t physical) {
        char suffix[160];
        sprintf_s(suffix, "_%08x_q00000001%08x_t%016llx%016llx_l00000001000000010000000100000002_kn__ap_v070_s00100",
                  body, count, source, physical);
        return "ap_v070_ai_r" + std::string(20, static_cast<char>('a' + origin_index)) + suffix;
    }
    void bind_remaining_callbacks() {
        definition_name = [](uintptr_t actor) -> const char* {
            for (const auto& value : actors)
                if (actor == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.name.c_str();
            return "ai/fodder/imp";
        };
        refresh_entity = refresh_manager = [](uintptr_t, uintptr_t) {};
        ignored_actor = [](uintptr_t actor) {
            for (const auto& value : actors)
                if (actor == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.ignored;
            return false;
        };
        original_remaining = [](uintptr_t) -> uint64_t {
            ++native_calls;
            return 42;
        };
        original_remaining_dead = [](uintptr_t command, uintptr_t record) {
            auto& count = *reinterpret_cast<int32_t*>(command + 0xb0);
            auto* data = *reinterpret_cast<uint8_t**>(command + 0xa8);
            const auto identifier = *reinterpret_cast<const uint32_t*>(record + 12);
            for (int32_t i = count; i-- > 0;) {
                if (*reinterpret_cast<const uint32_t*>(data + i * 0x68 + 12) != identifier) continue;
                --count;
                std::memmove(data + i * 0x68, data + (i + 1) * 0x68, static_cast<size_t>(count - i) * 0x68);
            }
        };
    }
    void prepare_remaining_records() {
        *reinterpret_cast<uintptr_t*>(command.data() + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
        *reinterpret_cast<uintptr_t*>(command.data() + 0xa8) = reinterpret_cast<uintptr_t>(records.data());
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 6;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
        for (size_t i = 0; i < actors.size(); ++i) {
            auto& actor = actors[i];
            actor.dead = actor.ignored = false;
            actor.bytes.fill(0);
            actor.table[0x88 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) -> uint32_t {
                for (uint32_t index = 0; index < actors.size(); ++index)
                    if (address == reinterpret_cast<uintptr_t>(actors[index].bytes.data())) return index + 1;
                return 0;
            });
            actor.table[0x108 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) {
                for (const auto& value : actors)
                    if (address == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.dead;
                return false;
            });
            *reinterpret_cast<uintptr_t*>(actor.bytes.data()) = reinterpret_cast<uintptr_t>(actor.table.data());
            actor.name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i) +
                         "_q0000000200000006_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__"
                         "ap_v070_s00100";
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = static_cast<uint32_t>(i + 1);
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = static_cast<uint32_t>(i + 1);
            *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) =
                reinterpret_cast<uintptr_t>(actor.bytes.data());
        }
    }
    void optional_zombie_pending_case() {
        std::array<uint8_t, 0xb0> zombie_kill{};
        const auto zombie_kill_address = reinterpret_cast<uintptr_t>(zombie_kill.data());
        *reinterpret_cast<uintptr_t*>(zombie_kill_address + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
        *reinterpret_cast<uint64_t*>(zombie_kill_address + 0x88) = 16;
        *reinterpret_cast<int32_t*>(zombie_kill_address + 0xac) = 1;
        ok =
            ok && kill_result(zombie_kill_address) == 1 && *reinterpret_cast<int32_t*>(zombie_kill_address + 0xa8) == 0;
        recipe_state.reset(std::string(64, 'a'), "fixture");
        RecipeRequest pending{"ai/fodder/imp", "pending", "", 16, 1, 2, 2};
        ok = ok && recipe_state.request("pending", pending);
        native_recipes["pending"].bodies = {reinterpret_cast<uintptr_t>(actors[0].bytes.data()),
                                            reinterpret_cast<uintptr_t>(actors[1].bytes.data())};
        ok = ok && remaining_detour(cmd) == 0 && !recipe_state.requests().at("pending").closed;
        actors[1].name = actors[0].name;
        ok = ok && remaining_detour(cmd) == 1 && recipe_state.requests().at("pending").closed;
        ok = ok && recipe_state.deaths("ai/fodder/imp", 16, "").empty();
    }
    void optional_zombie_cases() {
        {
            const auto old_world = world_global;
            const auto old_find = find_entity;
            auto old_recipes = std::move(native_recipes);
            auto old_state = std::move(recipe_state);
            auto old_kills = std::move(kill_progress);
            const auto first_name = actors[0].name, second_name = actors[1].name;
            static uintptr_t fixture_world = 1;
            static bool marker = true;
            world_global = reinterpret_cast<uintptr_t>(&fixture_world);
            find_entity = [](uintptr_t, const char* name) -> uintptr_t {
                return marker && std::string_view(name).rfind("ap_v070_optional_zombies_r", 0) == 0 ? 1 : 0;
            };
            actors[0].name = "ap_v070_ai_r0123456789abcdef0123_00000000_q0000000100000001_"
                             "t00000000000000100000000000000001_l00000001000000010000000100000002_kn__ap_v070_s00100";
            actors[1].name = "ap_v070_ai_r0123456789abcdef0123_00000000_q0000000100000001_"
                             "t00000000000000100000000000004000_l00000001000000010000000100000002_kn__ap_v070_s00100";
            *reinterpret_cast<uintptr_t*>(manager.data() + 0xc80) = reinterpret_cast<uintptr_t>(records.data());
            *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 2;
            *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16;
            *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 2;
            *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 0;
            for (size_t i = 0; i < 2; ++i) *reinterpret_cast<uint64_t*>(records.data() + i * 0x68) = 16;
            remaining_weights.erase(cmd);
            ok = ok && remaining_detour(cmd) == 0;
            actors[1].dead = true;
            ok = ok && remaining_detour(cmd) == 1 && !actors[0].dead;
            const auto previous_handle = *reinterpret_cast<uint64_t*>(records.data() + 0x68 + 8);
            *reinterpret_cast<uint64_t*>(records.data() + 0x68 + 8) = uint64_t{0x1fffffe} << 32 | 0x1fffffe;
            *reinterpret_cast<uintptr_t*>(records.data() + 0x68 + 16) = 0;
            ok = ok && optional_zombie_completion_safe(cmd);
            *reinterpret_cast<uint64_t*>(records.data() + 0x68 + 8) = previous_handle;
            *reinterpret_cast<uintptr_t*>(records.data() + 0x68 + 16) =
                reinterpret_cast<uintptr_t>(actors[1].bytes.data());
            marker = false;
            ok = ok && remaining_detour(cmd) == 0;
            marker = true;
            actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000004");
            ok = ok && remaining_detour(cmd) == 1;
            actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000002");
            ok = ok && remaining_detour(cmd) == 0;
            actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000008");
            ok = ok && remaining_detour(cmd) == 0;
            actors[0].name.replace(actors[0].name.find("_t") + 18, 16, "0000000000000001");
            manager[0xdaf] = 1;
            ok = ok && remaining_detour(cmd) == 0;
            manager[0xdaf] = 0;
            optional_zombie_pending_case();
            native_recipes = std::move(old_recipes);
            recipe_state = std::move(old_state);
            kill_progress = std::move(old_kills);
            world_global = old_world;
            find_entity = old_find;
            actors[0].name = first_name;
            actors[1].name = second_name;
            actors[1].dead = false;
            *reinterpret_cast<uintptr_t*>(manager.data() + 0xc80) = 0;
            *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 0;
            *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 0;
            *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 6;
            *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
            for (size_t i = 0; i < 2; ++i) *reinterpret_cast<uint64_t*>(records.data() + i * 0x68) = 0;
            remaining_weights.erase(cmd);
        }
    }
    void remaining_lifetime_cases() {
        actors[5].dead = actors[1].dead = actors[3].dead = true;
        ok = ok && remaining_detour(cmd) == 1;
        actors[3].ignored = true;
        ok = ok && remaining_detour(cmd) == 0;
        actors[3].ignored = false;
        manager[0xdaf] = 1;
        ok = ok && remaining_detour(cmd) == 0;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
        ok = ok && remaining_detour(cmd) == 1;
        *reinterpret_cast<uintptr_t*>(records.data() + 5 * 0x68 + 16) = 0;
        *reinterpret_cast<uint32_t*>(records.data() + 5 * 0x68 + 8) = 0x1fffffe;
        *reinterpret_cast<uint32_t*>(records.data() + 5 * 0x68 + 12) = 0x1fffffe;
        actors[0].name = "ai/phase_transition";
        ok = ok && remaining_detour(cmd) == 1;
        std::array<uint8_t, 0x68> dead_record{};
        std::memcpy(dead_record.data(), records.data() + 0x68, dead_record.size());
        remaining_dead_detour(cmd, reinterpret_cast<uintptr_t>(dead_record.data()));
        ok = ok && remaining_detour(cmd) == 1 && *reinterpret_cast<int32_t*>(command.data() + 0xb0) == 5;
        manager[0xdaf] = 0;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
        ok = ok && remaining_detour(cmd) == 1;
    }
    void remaining_ratio_cases() {
        remaining_weights.erase(cmd);
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 2;
        for (unsigned i = 0; i < 2; ++i) {
            actors[i].dead = false;
            actors[i].name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i) +
                             "_q0000000600000002_t00000000000000100000000000000020_l00000001000000010000000100000002_"
                             "kn__ap_v070_s00100";
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 1;
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 1;
            *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) =
                reinterpret_cast<uintptr_t>(actors[i].bytes.data());
        }
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
        ok = ok && remaining_detour(cmd) == 0;
        actors[0].dead = true;
        ok = ok && remaining_detour(cmd) == 0;
        actors[1].dead = true;
        ok = ok && remaining_detour(cmd) == 1;
        remaining_weights.erase(cmd);
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 5;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 3;
        for (unsigned i = 0; i < 5; ++i) {
            actors[i].dead = false;
            actors[i].name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i % 3) + "_q0000000200000003" +
                             (i == 4 ? "_w00000001" : "") +
                             "_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100";
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 1;
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 1;
            *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) =
                reinterpret_cast<uintptr_t>(actors[i].bytes.data());
        }
        ok = ok && remaining_detour(cmd) == 1;
        actors[4].dead = true;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
        ok = ok && remaining_detour(cmd) == 0;
        actors[1].dead = true;
        ok = ok && remaining_detour(cmd) == 1;
        stopped.store(true);
        ok = ok && remaining_detour(cmd) == 42;
        stopped.store(false);
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 2;
        actors[0].name = actors[1].name = "ai/fodder/imp";
        for (unsigned i = 0; i < 2; ++i) {
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 101;
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 101;
        }
        ok = ok && remaining_detour(cmd) == 42 && native_calls == 2;
    }
    void prepare_kill_records() {
        *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = 16;
        cast_maintain = [](uintptr_t worker) { return worker; };
        cast_stagger = [](uintptr_t) -> uintptr_t { return 0; };
        original_kill = [](uintptr_t address) -> uint64_t {
            return native_future(*reinterpret_cast<uintptr_t*>(address + 0x40)) ? 0 : 42;
        };
        original_kill_dead = [](uintptr_t command_address, uintptr_t record) {
            if (*reinterpret_cast<uint64_t*>(record) & *reinterpret_cast<uint64_t*>(command_address + 0x88))
                ++*reinterpret_cast<int32_t*>(command_address + 0xa8);
        };
        *reinterpret_cast<uintptr_t*>(kill + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
        *reinterpret_cast<uint64_t*>(kill + 0x88) = 16;
        *reinterpret_cast<int32_t*>(kill + 0xac) = 1;
        *reinterpret_cast<uintptr_t*>(manager.data() + 0xc80) = reinterpret_cast<uintptr_t>(records.data());
        *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 6;
        *reinterpret_cast<uintptr_t*>(manager.data() + 0xcc0) = reinterpret_cast<uintptr_t>(future_commands);
        *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 1;
        for (unsigned i = 0; i < 6; ++i) {
            actors[i].dead = false;
            actors[i].health = 100;
            actors[i].name = "ap_v070_ai_r0123456789abcdef0123_0000000" + std::to_string(i) +
                             "_q0000000200000006_t00000000000000100000000000000020_l00000001000000010000000100000002_"
                             "kn__ap_v070_s00100";
            *reinterpret_cast<uint64_t*>(records.data() + i * 0x68) = 16;
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 8) = i + 1;
            *reinterpret_cast<uint32_t*>(records.data() + i * 0x68 + 12) = i + 1;
            *reinterpret_cast<uintptr_t*>(records.data() + i * 0x68 + 16) =
                reinterpret_cast<uintptr_t>(actors[i].bytes.data());
        }
    }
    void kill_lifetime_cases() {
        initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
        ok = ok && kill_detour(kill) == 0;
        for (unsigned i : {5u, 1u, 3u}) {
            auto* record = records.data() + i * 0x68;
            *reinterpret_cast<uint64_t*>(record) = 32;
            kill_dead_detour(kill, reinterpret_cast<uintptr_t>(record));
            actors[i].dead = true;
            *reinterpret_cast<uint64_t*>(record) = 16;
            ok = ok && kill_detour(kill) == (i == 3 ? 1u : 0u);
        }
        *reinterpret_cast<uint64_t*>(kill + 0x88) = 32;
        const auto seen = *reinterpret_cast<int32_t*>(kill + 0xa8);
        *reinterpret_cast<uint64_t*>(records.data()) = 32;
        kill_dead_detour(kill, reinterpret_cast<uintptr_t>(records.data()));
        ok = ok && *reinterpret_cast<int32_t*>(kill + 0xa8) == seen;
        *reinterpret_cast<uint64_t*>(records.data()) = 16;
        *reinterpret_cast<uint64_t*>(kill + 0x88) = 16;
        *reinterpret_cast<int32_t*>(kill + 0xac) = 2;
        ok = ok && kill_detour(kill) == 0;
        *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 0;
        ok = ok && kill_detour(kill) == 0;
        *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 0;
        ok = ok && kill_detour(kill) == 1;
    }
    void renewable_kill_cases() {
        recipe_state.reset("plan", "map");
        kill_progress.erase(kill);
        *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
        *reinterpret_cast<int32_t*>(kill + 0xac) = 5;
        *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 1;
        initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
        ok &= recipe_state.request("torch", {"ai/fodder/imp", "origin", "", 16, 3, 3, 0});
        for (unsigned i = 0; i < 5; ++i) {
            const auto emitted = recipe_state.reserve("torch");
            ok &= emitted && recipe_state.created("torch", *emitted) &&
                  recipe_state.actor_changed(emitted->actor, RecipeActorState::dead) &&
                  !recipe_state.actor_changed(emitted->actor, RecipeActorState::dead);
            if (emitted && i % 2 == 0) {
                const auto actor = reinterpret_cast<uintptr_t>(actors[0].bytes.data());
                recipe_actors[actor] = emitted->actor;
                recipe_actor_credits[actor] = {1, {1, 1}, "torch"};
                *reinterpret_cast<uint64_t*>(records.data()) = 32;
                kill_dead_detour(kill, reinterpret_cast<uintptr_t>(records.data()));
                recipe_actors.erase(actor);
                recipe_actor_credits.erase(actor);
                *reinterpret_cast<uint64_t*>(records.data()) = 16;
            }
            ok &= kill_detour(kill) == (i == 4 ? 1u : 0u) &&
                  *reinterpret_cast<int32_t*>(kill + 0xa8) == static_cast<int32_t>(i + 1);
        }
        kill_progress.erase(kill);
        initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
        ok &= kill_detour(kill) == 1;
        *reinterpret_cast<int32_t*>(kill + 0xac) = 2;
        *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
        recipe_state.reset("plan", "map");
        kill_progress.erase(kill);
        *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
        initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
        ok = ok && recipe_state.request("empty", {"ai/fodder/imp", "origin", "", 16, 2, 0, 0}) &&
             recipe_state.complete_empty("empty");
        *reinterpret_cast<int32_t*>(manager.data() + 0xcc8) = 1;
        ok = ok && kill_detour(kill) == 1;
        kill_progress.erase(kill);
        *reinterpret_cast<int32_t*>(kill + 0xa8) = 0;
        initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
        ok = ok && kill_detour(kill) == 0;
    }
    void kill_scope_cases() {
        recipe_state.reset("plan", "map");
        const uint64_t hunter_mask = uint64_t{1} << 26;
        scope_ok &= recipe_state.request("hunter", {"ai/fodder/imp", "origin", "", hunter_mask, 1, 1, 1});
        const auto hunter = recipe_state.reserve("hunter");
        scope_ok &= hunter && recipe_state.created("hunter", *hunter) &&
                    recipe_state.actor_changed(hunter->actor, RecipeActorState::dead);
        *reinterpret_cast<uint64_t*>(kill + 0x88) = hunter_mask;
        *reinterpret_cast<int32_t*>(kill + 0xac) = 2;
        *reinterpret_cast<int32_t*>(kill + 0xa8) = 1;
        *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = 16;
        *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 1;
        actors[0].dead = false;
        *reinterpret_cast<uint64_t*>(records.data()) = 16;
        kill_progress.erase(kill);
        initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
        scope_ok &= kill_detour(kill) == 1;
        *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = hunter_mask;
        scope_ok &= kill_detour(kill) == 0;
        *reinterpret_cast<uint64_t*>(future_worker.data() + 0x88) = 16;
        *reinterpret_cast<uint64_t*>(records.data()) = hunter_mask;
        scope_ok &= kill_detour(kill) == 0;
        actors[0].dead = true;
        scope_ok &= kill_detour(kill) == 1;
        *reinterpret_cast<uint64_t*>(records.data()) = 16;
        *reinterpret_cast<save::NativeString*>(kill + 0x58) = {0, const_cast<char*>("unrelated"), 9, 0x40000000U, {}};
        kill_progress.erase(kill);
        initialize_kill(kill, reinterpret_cast<uintptr_t>(manager.data()));
        scope_ok &= kill_detour(kill) == 0;
        *reinterpret_cast<save::NativeString*>(kill + 0x58) = {};
        *reinterpret_cast<uint64_t*>(kill + 0x88) = 16;
        *reinterpret_cast<uint64_t*>(records.data()) = 16;
        *reinterpret_cast<int32_t*>(kill + 0xac) = 1;
        recipe_state.reset("plan", "map");
    }
    void bind_health_callbacks() {
        *reinterpret_cast<uintptr_t*>(health + 0xa8) = reinterpret_cast<uintptr_t>(health_records.data());
        *reinterpret_cast<float*>(health + 0xc0) = 0.5f;
        original_health_added = [](uintptr_t command_address, uintptr_t record) {
            auto& count = *reinterpret_cast<int32_t*>(command_address + 0xb0);
            auto* data = *reinterpret_cast<uint8_t**>(command_address + 0xa8);
            std::memcpy(data + count * 0x68, reinterpret_cast<void*>(record), 0x68);
            *reinterpret_cast<float*>(data + count * 0x68 + 100) = 100;
            *reinterpret_cast<float*>(command_address + 0xc8) += 100;
            ++count;
        };
        for (auto& actor : actors) {
            actor.table[0x6f0 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t) { return 100.0f; });
            actor.table[0x6e8 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t address) {
                for (const auto& value : actors)
                    if (address == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.health;
                return 0.0f;
            });
        }
        original_health_damage = [](uintptr_t command_address, uintptr_t record, float amount) {
            auto* data = *reinterpret_cast<uint8_t**>(command_address + 0xa8);
            const auto count = *reinterpret_cast<int32_t*>(command_address + 0xb0);
            for (int32_t i = 0; i < count; ++i)
                if (*reinterpret_cast<uint32_t*>(data + i * 0x68 + 12) == *reinterpret_cast<uint32_t*>(record + 12)) {
                    *reinterpret_cast<float*>(data + i * 0x68 + 100) -= amount;
                    *reinterpret_cast<float*>(command_address + 0xc4) += amount;
                    return;
                }
        };
        original_health = [](uintptr_t command_address) -> uint64_t {
            auto* data = *reinterpret_cast<uint8_t**>(command_address + 0xa8);
            auto& count = *reinterpret_cast<int32_t*>(command_address + 0xb0);
            for (int32_t i = count; i-- > 0;)
                if (!*reinterpret_cast<uintptr_t*>(data + i * 0x68 + 16)) {
                    *reinterpret_cast<float*>(command_address + 0xc4) +=
                        *reinterpret_cast<float*>(data + i * 0x68 + 100);
                    --count;
                    std::memmove(data + i * 0x68, data + (i + 1) * 0x68, static_cast<size_t>(count - i) * 0x68);
                }
            const auto initial = *reinterpret_cast<float*>(command_address + 0xc8);
            return (initial - *reinterpret_cast<float*>(command_address + 0xc4)) / initial <=
                   *reinterpret_cast<float*>(command_address + 0xc0);
        };
    }
    void health_credit_cases() {
        *reinterpret_cast<uintptr_t*>(health + 0x40) = reinterpret_cast<uintptr_t>(manager.data());
        *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 6;
        for (unsigned i = 0; i < 6; ++i)
            health_added_detour(health, reinterpret_cast<uintptr_t>(records.data() + i * 0x68));
        ok = ok && *reinterpret_cast<float*>(health + 0xc8) == 600;
        actors[0].health = 0;
        health_damage_detour(health, reinterpret_cast<uintptr_t>(records.data()), 100);
        actors[1].health = 0;
        health_damage_detour(health, reinterpret_cast<uintptr_t>(records.data() + 0x68), 100);
        ok = ok && health_detour(health) == 0 &&
             *reinterpret_cast<float*>(health_records.data() + 2 * 0x68 + 100) == 100;
        *reinterpret_cast<uintptr_t*>(health_records.data() + 5 * 0x68 + 16) = 0;
        *reinterpret_cast<uint32_t*>(health_records.data() + 5 * 0x68 + 8) = 0x1fffffe;
        *reinterpret_cast<uint32_t*>(health_records.data() + 5 * 0x68 + 12) = 0x1fffffe;
        ok = ok && health_detour(health) == 1 && *reinterpret_cast<int32_t*>(health + 0xb0) == 5 &&
             *reinterpret_cast<float*>(health_records.data() + 2 * 0x68 + 100) == 100;
        for (auto& [identifier, item] : health_progress[health].actors) {
            (void)identifier;
            item.weight.credit = {0, 1};
        }
        health_progress[health].retired.clear();
        health_progress[health].retired[{0, 1, 0}] = 1;
        ok = ok && health_detour(health) == 1;
    }
    inline static std::array<Actor, 9> gate_actors{};
    inline static std::array<std::array<uint64_t, 13>, 9> gate_registered{};
    inline static uintptr_t gate_owner, gate_alias{};
    inline static std::string gate_alias_name{};
    inline static unsigned gate_emissions{};
    inline static bool gate_valid{};
    static constexpr uint64_t soldier = uint64_t{1} << 9, gargoyle = uint64_t{1} << 6, imp = uint64_t{1} << 4,
                              shotgun_soldier = uint64_t{1} << 10, cyber_mancubus = uint64_t{1} << 21,
                              pinky = uint64_t{1} << 16, baron = uint64_t{1} << 28;
    const uint64_t gate_physical[9]{soldier, gargoyle,        imp,     gargoyle, cyber_mancubus,
                                    soldier, shotgun_soldier, spectre, baron};
    const std::string pool_origin = std::string(20, 'd'), knight_origin = std::string(20, 'e'),
                      pinky_origin = std::string(20, 'f');
    std::array<uint8_t, 0xe30> gate_manager{};
    std::array<uint8_t, 0x140> gate_worker{};
    uintptr_t alias_storage = 0;
    std::array<std::array<uint64_t, 3>, 9> gate_references{};
    std::array<std::array<uint64_t, 13>, 9> gate_admitted{};
    std::unique_ptr<std::array<uint8_t, 0x126680>> gate_world;
    uintptr_t gate_world_address = 0;
    struct GateCollection {
        uintptr_t data;
        int32_t count, capacity;
        uint32_t flags;
    };
    GateCollection gate_collection{reinterpret_cast<uintptr_t>(gate_references.data()), 9, 9, 0x1050001};
    std::array<uintptr_t, 4> gate_managers{};
    uintptr_t gate_commands[1]{reinterpret_cast<uintptr_t>(gate_worker.data())};
    uintptr_t gate_worker_address = reinterpret_cast<uintptr_t>(gate_worker.data());
    std::string pool_key;
    void tick_gate_workers() {
        for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(gate_owner + 0xcc8); ++i)
            maintain_tick_detour(gate_commands[i]);
    }
    void build_gate_wait(uintptr_t wait, uint64_t mask, uintptr_t data) {
        *reinterpret_cast<uintptr_t*>(wait + 0x40) = gate_owner;
        *reinterpret_cast<uint64_t*>(wait + 0x88) = mask;
        *reinterpret_cast<uintptr_t*>(wait + 0xa8) = data;
        for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(gate_owner + 0xc88); ++i)
            remaining_added_detour(wait, reinterpret_cast<uintptr_t>(gate_admitted[i].data()));
    }
    void kill_gate_body(unsigned index, uintptr_t wait) {
        const auto actor = reinterpret_cast<uintptr_t>(gate_actors[index].bytes.data());
        gate_actors[index].dead = true;
        gate_references[index][0] = 0;
        record_recipe_death(actor);
        remaining_dead_detour(wait, reinterpret_cast<uintptr_t>(gate_registered[index].data()));
        auto& count = *reinterpret_cast<int32_t*>(gate_owner + 0xc88);
        for (int32_t i = 0; i < count; ++i)
            if (gate_admitted[i][1] == gate_registered[index][1]) {
                --count;
                std::memmove(gate_admitted.data() + i, gate_admitted.data() + i + 1,
                             static_cast<size_t>(count - i) * 0x68);
                break;
            }
    }
    void bind_gate_callbacks() {
        gate_owner = reinterpret_cast<uintptr_t>(gate_manager.data());
        gate_alias = reinterpret_cast<uintptr_t>(&alias_storage);
        gate_alias_name = "ap_v070_request_r" + pool_origin;
        gate_emissions = 0;
        gate_valid = true;
        definition_name = [](uintptr_t entity) -> const char* {
            if (entity == gate_owner) return "slayer_gate";
            if (entity == gate_alias) return gate_alias_name.c_str();
            for (const auto& actor : gate_actors)
                if (entity == reinterpret_cast<uintptr_t>(actor.bytes.data())) return actor.name.c_str();
            return "native_ai";
        };
        refresh_entity = [](uintptr_t reference, uintptr_t) {
            const auto id = *reinterpret_cast<const uint32_t*>(reference);
            if (id && id <= gate_actors.size())
                *reinterpret_cast<uintptr_t*>(reference + 8) =
                    reinterpret_cast<uintptr_t>(gate_actors[id - 1].bytes.data());
        };
        resolve_actor = [](uintptr_t reference) -> uintptr_t {
            const auto id = *reinterpret_cast<const uint32_t*>(reference);
            return id && id <= gate_actors.size() ? reinterpret_cast<uintptr_t>(gate_actors[id - 1].bytes.data()) : 0;
        };
        find_entity = [](uintptr_t, const char*) -> uintptr_t { return 0; };
        original_remaining = [](uintptr_t wait) -> uint64_t {
            return !*reinterpret_cast<const int32_t*>(wait + 0xb0) &&
                   !native_future(*reinterpret_cast<const uintptr_t*>(wait + 0x40),
                                  *reinterpret_cast<const uint64_t*>(wait + 0x88), wait + 0x58);
        };
    }
    static uint32_t gate_actor_identifier(uintptr_t address) {
        for (uint32_t index = 0; index < gate_actors.size(); ++index)
            if (address == reinterpret_cast<uintptr_t>(gate_actors[index].bytes.data())) return index + 1;
        return 0;
    }
    static bool gate_actor_dead(uintptr_t address) {
        for (const auto& value : gate_actors)
            if (address == reinterpret_cast<uintptr_t>(value.bytes.data())) return value.dead;
        return false;
    }
    void prepare_gate_actor(unsigned i) {
        auto& actor = gate_actors[i];
        actor.dead = actor.ignored = false;
        actor.bytes.fill(0);
        actor.table[0x88 / 8] = reinterpret_cast<uintptr_t>(&gate_actor_identifier);
        actor.table[0x108 / 8] = reinterpret_cast<uintptr_t>(&gate_actor_dead);
        *reinterpret_cast<uintptr_t*>(actor.bytes.data()) = reinterpret_cast<uintptr_t>(actor.table.data());
        const auto base = i < 4 ? 0u : i < 7 ? 4u : 7u;
        const auto count = i < 4 ? 4u : i < 7 ? 3u : 2u;
        const auto source = i < 4 ? soldier : i < 7 ? hell_knight : pinky;
        actor.name = body_name(i < 4 ? 3 : i < 7 ? 4 : 5, i - base, count, source, gate_physical[i]);
        char units[9];
        sprintf_s(units, "%08x", count);
        actor.name.replace(actor.name.find("_q") + 2, 8, units);
        auto& record = gate_registered[i];
        record.fill(0);
        record[0] = gate_physical[i] | flag;
        record[1] = uint64_t{i + 1} << 32 | (i + 1);
        *reinterpret_cast<save::NativeString*>(reinterpret_cast<uintptr_t>(record.data()) + 0x28) =
            text(encounter_label);
        gate_references[i][0] = i + 1;
    }
    void prepare_gate_actors() {
        auto& pool = native_recipes[pool_origin];
        pool.definition.origin = pool_origin;
        pool.definition.owner = "slayer_gate";
        pool.definition.family = 'm';
        pool.definition.logical_mask = soldier;
        pool.definition.logical_count = pool.definition.physical_count = 4;
        pool.bodies.resize(4);
        ok &= recipe_state.reset("plan", "slayer_gate") &&
              recipe_state.request("gate_knights",
                                   {"slayer_gate", knight_origin, encounter_label, hell_knight, 3, 3, 3}) &&
              recipe_state.request("gate_pinkies", {"slayer_gate", pinky_origin, encounter_label, pinky, 2, 2, 2});
        for (unsigned i = 0; i < gate_actors.size(); ++i) {
            prepare_gate_actor(i);
        }
    }
    inline static bool (*emit_gate_body)(const std::string&, size_t) = nullptr;
    void bind_gate_emission() {

        emit_gate_body = +[](const std::string& key, size_t index) {
            const auto emission = recipe_state.reserve(key);
            if (!emission) return false;
            const auto actor = reinterpret_cast<uintptr_t>(gate_actors[index].bytes.data());
            gate_valid &= recipe_state.created(key, *emission) && !gate_registered[index][2];
            recipe_actors[actor] = emission->actor;
            recipe_actor_credits[actor] = {static_cast<uint32_t>(index + 1), {1, 1}, key, "", emission->ordinal};
            register_spawn_detour(gate_owner, gate_registered[index].data());
            ++gate_emissions;
            return true;
        };
        original_maintain_tick = [](uintptr_t) {
            const auto& progress = recipe_state.requests().at(*worker_recipe);
            if (progress.actors.size() >= 4) return;
            if (progress.emitted >= 4) {
                gate_valid = false;
                return;
            }
            gate_valid &= emit_gate_body(*worker_recipe, static_cast<size_t>(progress.emitted));
        };
    }
    void gate_emission_case() {
        for (unsigned i = 0; i < 4; ++i) tick_gate_workers();
        pool_key = worker_recipes.at(gate_worker_address);
        for (unsigned i = 4; i < 9; ++i) ok &= emit_gate_body(i < 7 ? "gate_knights" : "gate_pinkies", i);
        ok &= gate_valid && gate_emissions == 9 && recipe_state.requests().at(pool_key).emitted == 4 &&
              !recipe_state.requests().at(pool_key).request.maximum_emissions &&
              !recipe_state.requests().at(pool_key).closed &&
              *reinterpret_cast<int32_t*>(gate_worker_address + 0x90) == 4 &&
              !*reinterpret_cast<int32_t*>(gate_worker_address + 0x94) &&
              *reinterpret_cast<uint64_t*>(gate_worker_address + 0x88) == soldier;
        for (unsigned i = 0; i < 9; ++i)
            ok &= gate_registered[i][2] == reinterpret_cast<uintptr_t>(gate_actors[i].bytes.data()) &&
                  gate_registered[i][0] == (flag | (i < 4   ? soldier
                                                    : i < 7 ? hell_knight
                                                            : pinky));
    }
    void gate_class_wait_case() {
        class_wait.fill(0);
        any_wait.fill(0);
        remaining_weights.erase(class_command);
        remaining_weights.erase(any_command);

        build_gate_wait(class_command, hell_knight | pinky, reinterpret_cast<uintptr_t>(class_records.data()));
        ok &= *reinterpret_cast<int32_t*>(class_command + 0xb0) == 5 && remaining_detour(class_command) == 0 &&
              !native_future(gate_owner, hell_knight | pinky) && native_future(gate_owner);

        for (unsigned i : {4u, 7u, 5u, 8u, 6u}) {
            kill_gate_body(i, class_command);
            tick_gate_workers();
            ok &= remaining_detour(class_command) == (i == 6 ? 1u : 0u) && gate_emissions == 9;
        }
        ok &=
            *reinterpret_cast<int32_t*>(gate_owner + 0xc88) == 4 &&
            std::none_of(gate_actors.begin(), gate_actors.begin() + 4, [](const auto& actor) { return actor.dead; }) &&
            !recipe_state.requests().at(pool_key).closed && native_future(gate_owner) &&
            worker_recipes.at(gate_worker_address) == pool_key;
    }
    void check_gate_retirement() {
        observe(gate_world_address);
        tick_gate_workers();
        ok &= worker_recipes.empty() && recipe_state.requests().at(pool_key).closed &&
              recipe_state.requests().at(pool_key).emitted == 4 && !emit_gate_body(pool_key, 0) &&
              gate_emissions == 9 && recipe_state.requests().size() == 3 && !native_future(gate_owner) &&
              credit_at_least(recipe_state.deaths("slayer_gate", hell_knight | pinky, encounter_label), 5);
    }
    void gate_retirement_case() {
        *reinterpret_cast<int32_t*>(gate_owner + 0xcc8) = 0;
        for (unsigned retry = 0; retry < 3; ++retry) {
            check_gate_retirement();
        }
        build_gate_wait(any_command, uint64_t{1} << 44, reinterpret_cast<uintptr_t>(any_records.data()));
        ok &= *reinterpret_cast<int32_t*>(any_command + 0xb0) == 4 && remaining_detour(any_command) == 0;
        for (unsigned i = 0; i < 4; ++i) {
            kill_gate_body(i, any_command);
            observe(gate_world_address);
            tick_gate_workers();
            ok &= remaining_detour(any_command) == (i == 3 ? 1u : 0u) && gate_emissions == 9;
        }
        ok &= !recipe_fault.load() && gate_valid && recipe_state.requests().at(pool_key).dead == 4 &&
              credit_at_least(recipe_state.deaths("slayer_gate", soldier | hell_knight | pinky, encounter_label), 9);
    }
    void gate_population_cases() {
        {

            const auto gate_name = definition_name;
            const auto gate_refresh = refresh_entity;
            const auto gate_world_global = world_global;
            const auto gate_find = find_entity;
            const auto gate_resolve = resolve_actor;
            const auto gate_tick = original_maintain_tick;
            auto gate_state = std::move(recipe_state);
            auto gate_recipes = std::move(native_recipes);
            auto gate_workers = std::move(worker_recipes);
            auto gate_roles = std::move(recipe_actors);
            auto gate_credits = std::move(recipe_actor_credits);
            auto gate_diagnostics = std::move(worker_diagnostics);
            const auto gate_next = next_recipe_request;
            const auto gate_trace_count = worker_trace_count;
            const auto gate_fault = recipe_fault.exchange(false);

            bind_gate_callbacks();
            gate_world = std::make_unique<std::array<uint8_t, 0x126680>>();
            gate_world_address = reinterpret_cast<uintptr_t>(gate_world->data());
            world_global = reinterpret_cast<uintptr_t>(&gate_world_address);

            gate_managers = {0, gate_owner};

            *reinterpret_cast<uintptr_t*>(gate_world_address + 0x62088) = reinterpret_cast<uintptr_t>(&gate_collection);
            *reinterpret_cast<uintptr_t*>(gate_world_address + 0x126658) =
                reinterpret_cast<uintptr_t>(gate_managers.data());
            *reinterpret_cast<int32_t*>(gate_world_address + 0x126660) = 1;
            *reinterpret_cast<uintptr_t*>(gate_owner + 0xc80) = reinterpret_cast<uintptr_t>(gate_admitted.data());
            *reinterpret_cast<uintptr_t*>(gate_owner + 0xcc0) = reinterpret_cast<uintptr_t>(gate_commands);
            *reinterpret_cast<int32_t*>(gate_owner + 0xcc8) = 1;

            *reinterpret_cast<uintptr_t*>(gate_worker_address + 0x40) = gate_owner;
            *reinterpret_cast<uintptr_t*>(gate_worker_address + 0xd8) = gate_alias;
            *reinterpret_cast<uint64_t*>(gate_worker_address + 0x88) = soldier;
            *reinterpret_cast<save::NativeString*>(gate_worker_address + 0x58) = text(encounter_label);
            prepare_gate_actors();
            bind_gate_emission();
            gate_emission_case();
            gate_class_wait_case();
            gate_retirement_case();
            recipe_state = std::move(gate_state);
            native_recipes = std::move(gate_recipes);
            worker_recipes = std::move(gate_workers);
            recipe_actors = std::move(gate_roles);
            recipe_actor_credits = std::move(gate_credits);
            worker_diagnostics = std::move(gate_diagnostics);
            next_recipe_request = gate_next;
            worker_trace_count = gate_trace_count;
            recipe_fault.store(gate_fault);
            definition_name = gate_name;
            refresh_entity = gate_refresh;
            world_global = gate_world_global;
            find_entity = gate_find;
            resolve_actor = gate_resolve;
            original_maintain_tick = gate_tick;
        }
    }
    void bind_selection_callbacks(RemainingEvent saved_health_added, RemainingEvent saved_dead) {
        original_remaining = [](uintptr_t wait) -> uint64_t { return !*reinterpret_cast<const int32_t*>(wait + 0xb0); };
        static RemainingEvent append_health;
        append_health = saved_health_added;
        refresh_entity = [](uintptr_t reference, uintptr_t) {
            const auto id = *reinterpret_cast<const uint32_t*>(reference);
            if (id && id <= actors.size())
                *reinterpret_cast<uintptr_t*>(reference + 8) = reinterpret_cast<uintptr_t>(actors[id - 1].bytes.data());
        };
        original_register_spawn = [](uintptr_t owner, uint64_t* record) {
            const auto actor = record_actor(reinterpret_cast<uintptr_t>(record));
            if (!actor) return;
            auto& count = *reinterpret_cast<int32_t*>(owner + 0xc88);
            const auto data = *reinterpret_cast<uintptr_t*>(owner + 0xc80);
            std::memcpy(reinterpret_cast<void*>(data + count++ * 0x68), record, 0x68);
        };
        const auto selected = +[](uintptr_t wait, uintptr_t record) {
            const auto mask = *reinterpret_cast<const uint64_t*>(wait + 0x88);
            const auto filter = reinterpret_cast<const save::NativeString*>(wait + 0x58);
            std::string label;
            return (*reinterpret_cast<const uint64_t*>(record) &
                    (mask & (uint64_t{1} << 44) ? encounter_ai_mask : mask)) &&
                   (!filter->length || (native_text(record + 0x28, label, 1024) && label == filter->data));
        };
        static bool (*select_record)(uintptr_t, uintptr_t);
        static RemainingEvent remove_record;
        select_record = selected;
        remove_record = saved_dead;
        original_remaining_added = [](uintptr_t wait, uintptr_t record) {
            if (!select_record(wait, record)) return;
            auto& count = *reinterpret_cast<int32_t*>(wait + 0xb0);
            const auto data = *reinterpret_cast<uintptr_t*>(wait + 0xa8);
            std::memcpy(reinterpret_cast<void*>(data + count++ * 0x68), reinterpret_cast<const void*>(record), 0x68);
        };
        original_remaining_dead = [](uintptr_t wait, uintptr_t record) {
            if (select_record(wait, record)) remove_record(wait, record);
        };
        original_health_added = [](uintptr_t wait, uintptr_t record) {
            if (select_record(wait, record)) append_health(wait, record);
        };
    }
    void prepare_selection_actor(unsigned i, const uint64_t* physical) {
        actors[i].dead = false;
        actors[i].health = 100;
        actors[i].name =
            body_name(i < 2   ? i
                      : i < 4 ? 2
                              : i,
                      i == 3 ? 1 : 0, i == 2 || i == 3 ? 2 : 1, i == 4 ? spirit : arachnotron, physical[i]);
    }
    void register_selection_actors(uintptr_t owner, uintptr_t other_owner,
                                   std::array<std::array<uint64_t, 13>, 7>& registered) {
        const uint64_t physical[]{spectre, cacodemon, spectre, cacodemon, arachnotron, arachnotron};
        for (unsigned i = 0; i < actors.size(); ++i) {
            prepare_selection_actor(i, physical);
        }
        for (unsigned i = 0; i < registered.size(); ++i) {
            auto& record = registered[i];
            const auto actor_index = std::min(i, 5u);
            record[0] = physical[actor_index] | flag;
            record[1] = uint64_t{actor_index + 1} << 32 | (actor_index + 1);
            *reinterpret_cast<save::NativeString*>(reinterpret_cast<uintptr_t>(record.data()) + 0x28) =
                text(i == 5 ? other_label : encounter_label);
            ok &= !record[2];
            register_spawn_detour(i == 6 ? other_owner : owner, record.data());
            ok &= record[2] == reinterpret_cast<uintptr_t>(actors[actor_index].bytes.data()) &&
                  record[0] == (flag | (i == 4 ? spirit : arachnotron));
        }
    }
    void selection_wait_case(std::array<std::array<uint64_t, 13>, 6>& admitted) {
        ok &= *reinterpret_cast<int32_t*>(class_command + 0xb0) == 4 &&
              *reinterpret_cast<int32_t*>(any_command + 0xb0) == 5 && remaining_detour(class_command) == 0 &&
              remaining_detour(any_command) == 0;
        std::vector<RecipeCredit> selected_total, selected_living;
        std::vector<RemainingItem> selected_items;
        bool tracked = false, blocked = false;
        int32_t threshold = 0;
        ok &= remaining_inputs(class_command, selected_total, selected_living, selected_items, tracked, blocked,
                               threshold) &&
              tracked && credit_at_least(selected_total, 3) && credit_at_most(selected_total, 3);
        for (unsigned i : {0u, 2u, 1u, 3u}) {
            actors[i].dead = true;
            const auto record = reinterpret_cast<uintptr_t>(admitted[i].data());
            remaining_dead_detour(class_command, record);
            remaining_dead_detour(any_command, record);
            ok &= remaining_detour(class_command) == (i == 3 ? 1u : 0u) && remaining_detour(any_command) == 0;
        }
        ok &= !actors[4].dead && !actors[5].dead && !*reinterpret_cast<int32_t*>(class_command + 0xb0);
        actors[4].dead = true;
        remaining_dead_detour(any_command, reinterpret_cast<uintptr_t>(admitted[4].data()));
        ok &= remaining_detour(any_command) == 1;
    }
    void selection_health_case(uintptr_t owner, std::array<std::array<uint64_t, 13>, 7>& registered,
                               std::array<std::array<uint64_t, 13>, 6>& admitted) {
        health_progress.erase(health);
        health_records.fill(0);
        *reinterpret_cast<int32_t*>(health + 0xb0) = 0;
        *reinterpret_cast<float*>(health + 0xc8) = *reinterpret_cast<float*>(health + 0xc4) = 0;
        *reinterpret_cast<float*>(health + 0xc0) = 0.25f;
        *reinterpret_cast<uint64_t*>(health + 0x88) = hell_knight;
        *reinterpret_cast<save::NativeString*>(health + 0x58) = text(encounter_label);
        *reinterpret_cast<int32_t*>(owner + 0xc88) = 0;
        for (unsigned i = 0; i < 2; ++i) {
            actors[i].dead = false;
            actors[i].health = 100;
            actors[i].name = body_name(i, 0, 1, i ? spirit : hell_knight, i ? hell_knight : spectre);
            auto& record = registered[i];
            record[0] = i ? hell_knight : spectre;
            record[2] = 0;
            register_spawn_detour(owner, record.data());
            health_added_detour(health, reinterpret_cast<uintptr_t>(admitted[i].data()));
        }
        ok &= *reinterpret_cast<int32_t*>(health + 0xb0) == 1 && health_progress[health].actors.count(1) == 1 &&
              health_progress[health].actors.count(2) == 0 && health_detour(health) == 0;
        actors[0].health = 25;
        health_damage_detour(health, reinterpret_cast<uintptr_t>(admitted[0].data()), 75);
        ok &= health_detour(health) == 1 && actors[1].health == 100;
    }
    void native_selection_cases() {
        {
            const auto saved_register = original_register_spawn;
            const auto saved_added = original_remaining_added;
            const auto saved_dead = original_remaining_dead;
            const auto saved_refresh = refresh_entity;
            const auto saved_health_added = original_health_added;
            const auto saved_remaining = original_remaining;
            bind_selection_callbacks(saved_health_added, saved_dead);
            std::array<uint8_t, 0xdb8> other_manager{};
            std::array<std::array<uint64_t, 13>, 7> registered{};
            std::array<std::array<uint64_t, 13>, 6> admitted{};
            std::array<uint64_t, 13> other_admitted{};
            const auto owner = reinterpret_cast<uintptr_t>(manager.data());
            const auto other_owner = reinterpret_cast<uintptr_t>(other_manager.data());
            const auto old_records = *reinterpret_cast<uintptr_t*>(owner + 0xc80);
            const auto old_count = *reinterpret_cast<int32_t*>(owner + 0xc88);
            *reinterpret_cast<uintptr_t*>(owner + 0xc80) = reinterpret_cast<uintptr_t>(admitted.data());
            *reinterpret_cast<int32_t*>(owner + 0xc88) = 0;
            *reinterpret_cast<uintptr_t*>(other_owner + 0xc80) = reinterpret_cast<uintptr_t>(other_admitted.data());
            register_selection_actors(owner, other_owner, registered);
            const auto build_wait = [&](uintptr_t wait, uint64_t mask, uintptr_t data) {
                *reinterpret_cast<uintptr_t*>(wait + 0x40) = owner;
                *reinterpret_cast<uint64_t*>(wait + 0x88) = mask;
                *reinterpret_cast<save::NativeString*>(wait + 0x58) = text(encounter_label);
                *reinterpret_cast<uintptr_t*>(wait + 0xa8) = data;
                for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(owner + 0xc88); ++i)
                    remaining_added_detour(wait, reinterpret_cast<uintptr_t>(admitted[i].data()));
            };
            build_wait(class_command, arachnotron, reinterpret_cast<uintptr_t>(class_records.data()));
            build_wait(any_command, uint64_t{1} << 44, reinterpret_cast<uintptr_t>(any_records.data()));
            selection_wait_case(admitted);
            selection_health_case(owner, registered, admitted);
            gate_population_cases();
            remaining_weights.erase(class_command);
            remaining_weights.erase(any_command);
            health_progress.erase(health);
            *reinterpret_cast<uintptr_t*>(owner + 0xc80) = old_records;
            *reinterpret_cast<int32_t*>(owner + 0xc88) = old_count;
            original_register_spawn = saved_register;
            original_remaining_added = saved_added;
            original_remaining_dead = saved_dead;
            refresh_entity = saved_refresh;
            original_health_added = saved_health_added;
            original_remaining = saved_remaining;
        }
    }
    void future_remainder_cases(const std::string& remainder_origin, const std::string& remainder_label) {
        native_recipes[remainder_origin].definition.family = 'g';
        native_recipes[remainder_origin].bodies.resize(6);
        recipe_state.reset("plan", "map");
        ok = ok &&
             recipe_state.request("remainder", {"ai/fodder/imp", remainder_origin, remainder_label, 16, 2, 6, 6, 6, 2});
        const auto emitted = recipe_state.reserve("remainder");
        ok = ok && emitted && recipe_state.created("remainder", *emitted);
        worker_recipes[reinterpret_cast<uintptr_t>(future_worker.data())] = "remainder";
        remaining_weights.erase(cmd);
        actors[0].dead = false;
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 1;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 1;
        *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16;
        *reinterpret_cast<save::NativeString*>(command.data() + 0x58) = {0,
                                                                         const_cast<char*>(remainder_label.c_str()),
                                                                         static_cast<int32_t>(remainder_label.size()),
                                                                         0x40000000U,
                                                                         {}};
        ok = ok && remaining_detour(cmd) == 0;
        actors[0].dead = true;
        ok = ok && remaining_detour(cmd) == 0;
        remaining_weights.erase(cmd);
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 0;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 0;
        ok = ok && remaining_detour(cmd) == 0;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 2;
        ok = ok && remaining_detour(cmd) == 1;
        *reinterpret_cast<save::NativeString*>(command.data() +
                                               0x58) = {0, const_cast<char*>("unrelated"), 9, 0x40000000U, {}};
        ok = ok && remaining_detour(cmd) == 42;
        *reinterpret_cast<save::NativeString*>(command.data() + 0x58) = {0,
                                                                         const_cast<char*>(remainder_label.c_str()),
                                                                         static_cast<int32_t>(remainder_label.size()),
                                                                         0x40000000U,
                                                                         {}};
        health_records.fill(0);
        *reinterpret_cast<int32_t*>(health + 0xb0) = 0;
        *reinterpret_cast<float*>(health + 0xc8) = *reinterpret_cast<float*>(health + 0xc4) = 0;
        *reinterpret_cast<float*>(health + 0xc0) = 0.9f;
        *reinterpret_cast<uint64_t*>(health + 0x88) = 16;
        *reinterpret_cast<save::NativeString*>(health + 0x58) = {0,
                                                                 const_cast<char*>(remainder_label.c_str()),
                                                                 static_cast<int32_t>(remainder_label.size()),
                                                                 0x40000000U,
                                                                 {}};
        *reinterpret_cast<int32_t*>(manager.data() + 0xc88) = 1;
        actors[0].dead = false;
        actors[0].health = 100;
        native_recipes[remainder_origin].definition.hitpoints.assign(6, {1000, 1000});
        native_recipes[remainder_origin].definition.hitpoints[0] = {100, 100};
        ok = ok && health_detour(health) == 0;
        health_added_detour(health, reinterpret_cast<uintptr_t>(records.data()));
        actors[0].health = 0;
        health_damage_detour(health, reinterpret_cast<uintptr_t>(records.data()), 100);
        ok = ok && health_detour(health) == 0;
        *reinterpret_cast<float*>(health + 0xc0) = 1;
        ok = ok && health_detour(health) == 1;
        *reinterpret_cast<float*>(health + 0xc0) = 0.9f;
        ok = ok && recipe_state.cancel("remainder") && remaining_detour(cmd) == 42 && health_detour(health) == 1;
    }
    void outside_heavy_cases() {
        const auto previous_world = world_global;
        const auto previous_find = find_entity;
        uintptr_t arena_world = 77;
        world_global = reinterpret_cast<uintptr_t>(&arena_world);
        find_entity = [](uintptr_t, const char* name) -> uintptr_t {
            return std::strcmp(name, "extra_heavy") == 0 ? reinterpret_cast<uintptr_t>(actors[0].bytes.data()) : 0;
        };
        recipe_state.reset(std::string(64, 'a'), "game/test");
        ok &= recipe_state.request("outside", {"ai/fodder/imp", "0123456789abcdef0123", "maintenance", 1, 1, 1, 1});
        const auto outside = recipe_state.reserve("outside");
        ok &= outside && recipe_state.created("outside", *outside, "extra_heavy");
        actors[0].name = "ap_v070_ai_r0123456789abcdef0123_00000000_q0000000100000001_"
                         "t00000000000000010000000000080000_l00000001000000010000000100000002_kn__ap_v070_s00100";
        *reinterpret_cast<int32_t*>(command.data() + 0xb0) = 0;
        *reinterpret_cast<int32_t*>(command.data() + 0xc0) = 0;
        *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16 | 32;
        *reinterpret_cast<save::NativeString*>(command.data() + 0x58) = {};
        ok &= remaining_detour(cmd) == 0;
        *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16;
        ok &= remaining_detour(cmd) == 42;
        *reinterpret_cast<uint64_t*>(command.data() + 0x88) = 16 | 32;
        actors[0].name.replace(actors[0].name.find("0000000000080000"), 16, "0000000000000020");
        ok &= remaining_detour(cmd) == 42;
        actors[0].name.replace(actors[0].name.find("0000000000000020", actors[0].name.find("_t")), 16,
                               "0000000000080000");
        ok &= recipe_state.actor_changed(outside->actor, RecipeActorState::dead) && remaining_detour(cmd) == 42;
        world_global = previous_world;
        find_entity = previous_find;
    }
    bool run() {
        const auto previous_name = definition_name;
        const auto previous_remaining = original_remaining;
        const auto previous_dead = original_remaining_dead;
        const auto previous_entity = refresh_entity, previous_manager = refresh_manager;
        const auto previous_ignored = ignored_actor;
        auto previous_weights = std::move(remaining_weights);
        const auto was_stopped = stopped.exchange(false);
        bind_remaining_callbacks();
        prepare_remaining_records();
        ok = remaining_detour(cmd) == 0;
        optional_zombie_cases();
        remaining_lifetime_cases();
        remaining_ratio_cases();
        const auto previous_kill = original_kill;
        const auto previous_kill_dead = original_kill_dead;
        const auto previous_maintain_cast = cast_maintain, previous_stagger_cast = cast_stagger;
        const auto previous_health = original_health;
        const auto previous_health_added = original_health_added;
        const auto previous_health_damage = original_health_damage;
        auto previous_health_progress = std::move(health_progress);
        auto previous_kills = std::move(kill_progress);
        auto previous_recipe_state = std::move(recipe_state);
        prepare_kill_records();
        kill_lifetime_cases();
        renewable_kill_cases();
        kill_scope_cases();
        bind_health_callbacks();
        health_credit_cases();
        native_selection_cases();
        auto previous_recipes = std::move(native_recipes);
        auto previous_workers = std::move(worker_recipes);
        const std::string remainder_origin = "0123456789abcdef0123", remainder_label = "remainder";
        future_remainder_cases(remainder_origin, remainder_label);
        outside_heavy_cases();
        native_recipes = std::move(previous_recipes);
        worker_recipes = std::move(previous_workers);
        kill_progress = std::move(previous_kills);
        recipe_state = std::move(previous_recipe_state);
        original_kill = previous_kill;
        original_kill_dead = previous_kill_dead;
        cast_maintain = previous_maintain_cast;
        cast_stagger = previous_stagger_cast;
        original_health = previous_health;
        original_health_added = previous_health_added;
        original_health_damage = previous_health_damage;
        health_progress = std::move(previous_health_progress);
        remaining_weights = std::move(previous_weights);
        definition_name = previous_name;
        original_remaining = previous_remaining;
        original_remaining_dead = previous_dead;
        refresh_entity = previous_entity;
        refresh_manager = previous_manager;
        ignored_actor = previous_ignored;
        stopped.store(was_stopped);
        return ok && scope_ok;
    }
};
bool test_quantity_inputs() { QuantityInputTest test; return test.run(); }

#endif

}
