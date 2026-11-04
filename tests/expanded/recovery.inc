namespace sentinel::expanded {
using namespace detail;

#ifdef SC_NATIVE_TESTING

bool test_recovery_kill(uintptr_t world, uintptr_t actor, TestRecoveryDeath death) {
    const auto saved_notify = recovery_notify;
    const auto saved_death = recovery_death;
    const auto saved_clock = recovery_clock;
    const auto saved_milliseconds = recovery_milliseconds;
    const auto saved_post = recovery_post;
    const auto saved_stop = recovery_stop_maintaining;
    const auto saved_resolve = resolve_actor;
    const auto saved_identifier = actor_identifier;
    static bool notified, removed, valid, owned;
    static unsigned stops;
    static uintptr_t expected_world, expected_actor;
    expected_world = world; expected_actor = actor;
    notified = removed = false; valid = owned = true; stops = 0;
    auto challenge = std::make_unique<std::array<uint8_t, 0x3600>>();
    std::array<uint8_t, 0x100> data{};
    std::array<uint8_t, 0xb00> declaration{};
    std::array<std::array<uint8_t, 0xe30>, 2> managers{};
    const auto owner = reinterpret_cast<uintptr_t>(managers[0].data());
    *reinterpret_cast<uintptr_t*>(world + 0x65b48) = reinterpret_cast<uintptr_t>(challenge->data());
    *reinterpret_cast<uintptr_t*>(challenge->data() + 0x35f8) = reinterpret_cast<uintptr_t>(data.data());
    *reinterpret_cast<uintptr_t*>(data.data() + 0xc0) = reinterpret_cast<uintptr_t>(declaration.data());
    *reinterpret_cast<uintptr_t*>(world + 0x126648) = 44;
    resolve_actor = [](uintptr_t reference) -> uintptr_t {
        valid &= reference == expected_actor + 0x167d0;
        return owned ? 77 : 0;
    };
    actor_identifier = [](uintptr_t object) -> uint32_t { valid &= object == expected_actor; return 7; };
    recovery_notify = [](uintptr_t dispatcher, uintptr_t player, uintptr_t victim, uintptr_t attacker, uintptr_t damage, uintptr_t direct, uint8_t flag) {
        valid &= stops == 2 && dispatcher == 44 && player == 1 && attacker == 1 && victim == expected_actor && damage == direct && !flag;
        notified = true;
    };
    recovery_death = death;
    recovery_clock = [](uintptr_t clock, int64_t* output, int32_t which) -> uintptr_t {
        valid &= notified && clock == expected_world + 0x125a60 && which == 1;
        *output = 1; return reinterpret_cast<uintptr_t>(output);
    };
    recovery_milliseconds = [](int32_t* output, int64_t value) { *output = static_cast<int32_t>(value * 1000); return output; };
    recovery_post = [](uintptr_t victim, uintptr_t event, int32_t delay, uintptr_t args) -> uintptr_t {
        valid &= notified && victim == expected_actor && event == recovery_image + 0x6bebf80 && delay == 2000 && !args;
        removed = true; return 1;
    };
    recovery_stop_maintaining = [](uintptr_t, uintptr_t output, uint64_t, const char*) { ++stops; return output; };
    recovery::Context context;
    context.world = world; context.player = 1; context.scope.lifecycle_generation = 1; context.stamp = native::observation_stamp();
    recovery::Manager target;
    target.address = owner; target.identifier = 9; target.name = "arena";
    target.actors.push_back({actor, 7});
    context.managers.push_back(target);
    auto overlap = target;
    overlap.address = reinterpret_cast<uintptr_t>(managers[1].data());
    context.managers.push_back(overlap);
    context.managers.push_back(target);
    context.managers.back().address = owner + 1;
    context.managers.back().actors = {{0xdead, 99}};
    recovery::Attempt attempt;
    attempt.stamp = context.stamp;
    attempt.targets.push_back(target);
    attempt.targets.push_back(overlap);
    bool ok = drain_recovery_impl(attempt, context) && !notified && !removed && !attempt.actors_killed && !attempt.kills_suppressed;
    owned = false; stops = 0;
    ok &= drain_recovery_impl(attempt, context) && valid && notified && removed &&
        attempt.actors_killed == 1 && attempt.kills_suppressed == 1;
    ok &= drain_recovery_impl(attempt, context) && attempt.actors_killed == 1 && attempt.kills_suppressed == 1;
    recovery_notify = saved_notify; recovery_death = saved_death; recovery_clock = saved_clock;
    recovery_milliseconds = saved_milliseconds; recovery_post = saved_post; recovery_stop_maintaining = saved_stop;
    resolve_actor = saved_resolve; actor_identifier = saved_identifier;
    return ok;
}

bool test_recovery_inputs() {
    const auto saved_name = definition_name;
    const auto saved_identifier = actor_identifier;
    const auto saved_maintain = cast_maintain, saved_stagger = cast_stagger;
    const auto saved_stop = recovery_stop_maintaining;
    const auto saved_image = recovery_image;
    auto world = std::make_unique<std::array<uint8_t, 0x126680>>();
    std::array<uint8_t, 0x800> image{};
    std::array<std::array<uint8_t, 0xe30>, 2> managers{};
    std::array<std::array<uint8_t, 0x28>, 3> actors{};
    std::array<std::array<uint8_t, 0x140>, 2> workers{};
    std::array<uintptr_t, 8> manager_records{};
    std::array<std::array<uint8_t, 0xd0>, 2> records{};
    std::array<uintptr_t, 2> commands{};
    const auto address = reinterpret_cast<uintptr_t>(world->data());
    const auto owner = reinterpret_cast<uintptr_t>(managers[0].data());
    const auto other = reinterpret_cast<uintptr_t>(managers[1].data());
    recovery_image = reinterpret_cast<uintptr_t>(image.data());
    const auto put = [](uintptr_t base, size_t offset, auto value) { std::memcpy(reinterpret_cast<void*>(base + offset), &value, sizeof(value)); };
    put(recovery_image, 0x1f8, recovery_image + 0x40);
    put(recovery_image, 0x4c, uint32_t{0x100});
    std::memcpy(image.data() + 0x110, ".?AVidEncounterManager@@", 24);
    const auto name = +[](uintptr_t object) { return *reinterpret_cast<const char**>(object + 0x18); };
    put(recovery_image, 0x260, reinterpret_cast<uintptr_t>(name));
    definition_name = name;
    actor_identifier = [](uintptr_t object) { return *reinterpret_cast<const uint32_t*>(object + 0x20); };
    cast_maintain = [](uintptr_t object) -> uintptr_t { return object && *reinterpret_cast<const uint32_t*>(object + 0x18) == 1 ? object : 0; };
    cast_stagger = [](uintptr_t object) -> uintptr_t { return object && *reinterpret_cast<const uint32_t*>(object + 0x18) == 2 ? object : 0; };
    for (size_t i = 0; i < managers.size(); ++i) {
        const auto manager = reinterpret_cast<uintptr_t>(managers[i].data());
        put(manager, 0, recovery_image + 0x200);
        put(manager, 0x18, i ? "other" : "arena");
        put(manager, 0x20, static_cast<uint32_t>(100 + i));
        put(manager, 0xdb2, uint8_t{1}); put(manager, 0xba0, uint8_t{1});
        manager_records[i * 4 + 1] = manager;
        put(manager, 0xc80, reinterpret_cast<uintptr_t>(records[i].data()));
        put(manager, 0xc88, int32_t{2});
        for (size_t j = 0; j < 2; ++j) {
            const auto actor = reinterpret_cast<uintptr_t>(actors[i + j].data());
            put(actor, 0, recovery_image + 0x200); put(actor, 0x18, "imp");
            put(actor, 0x20, static_cast<uint32_t>(i + j + 1));
            const auto record = reinterpret_cast<uintptr_t>(records[i].data()) + j * 0x68;
            put(record, 8, uint32_t{1}); put(record, 12, uint32_t{1}); put(record, 16, actor);
        }
    }
    put(address, 0x126658, reinterpret_cast<uintptr_t>(manager_records.data()));
    put(address, 0x126660, int32_t{2});
    for (size_t i = 0; i < workers.size(); ++i) {
        commands[i] = reinterpret_cast<uintptr_t>(workers[i].data());
        put(commands[i], 0x18, static_cast<uint32_t>(i + 1));
        put(commands[i], 0x40, owner); put(commands[i], 0xa8, int32_t{4});
    }
    put(owner, 0xcc0, reinterpret_cast<uintptr_t>(commands.data())); put(owner, 0xcc8, int32_t{2});
    static bool stopped_correctly;
    stopped_correctly = false;
    recovery_stop_maintaining = [](uintptr_t manager, uintptr_t output, uint64_t mask, const char* label) {
        stopped_correctly = mask == (uint64_t{1} << 44) && !*label;
        const auto data = *reinterpret_cast<uintptr_t*>(manager + 0xcc0);
        *reinterpret_cast<uintptr_t*>(data) = *reinterpret_cast<uintptr_t*>(data + 8);
        *reinterpret_cast<int32_t*>(manager + 0xcc8) = 1;
        return output;
    };
    map_freed();
    const std::string plan(64, 'a'), map = "game/test";
    bool ok = recipe_state.reset(plan, map);
    RecipeRequest request{"arena", "origin", "", 1, 1, 3, 3, 3, 1};
    for (const auto* key : {"worker", "pending", "direct/test", "summon/test", "independent/test"}) ok &= recipe_state.request(key, request);
    request.owner = "other"; ok &= recipe_state.request("unrelated", request);
    worker_recipes[commands[0]] = "worker"; pending_recipes[{owner, "origin"}] = "pending";
    component_summons[1].requests.push_back({"summon/test", "origin"});
    direct_points[2].requests.push_back({"direct/test", "origin"});
    native_recipes["origin"].definition.origin = "origin";
    native_recipes["origin"].definition.source = "hidden";
    native_recipes["origin"].bodies.push_back(1);
    pending_independent_shows["hidden"] = {};
    recovery::Context context;
    ok &= scan_recovery(address, context) && context.managers.size() == 2;
    if (context.managers.size() == 2) {
        ok &= context.managers[0].active && !context.managers[0].complete && context.managers[0].actors.size() == 2 &&
            context.managers[1].actors.size() == 2 && context.managers[0].actors[1].address == context.managers[1].actors[0].address;
        recovery::Attempt attempt;
        attempt.stamp = native::observation_stamp();
        stop_recovery_producers(attempt, context.managers[0]);
        ok &= stopped_correctly && workers[1][0xb9] && !*reinterpret_cast<int32_t*>(workers[1].data() + 0xa8) &&
            !*reinterpret_cast<int32_t*>(other + 0xcc8) && worker_recipes.empty() && pending_recipes.empty();
        for (const auto* key : {"worker", "pending", "direct/test", "summon/test", "independent/test"}) ok &= recipe_state.requests().at(key).closed;
        ok &= !recipe_state.requests().at("unrelated").closed && component_summons.at(1).completed && direct_points.at(2).completed;
        ok &= pending_independent_shows.empty() && independent_shows.count("hidden");
        std::string checkpoint;
        RecipeState restored;
        ok &= recipe_state.capture(checkpoint) && restored.restore(checkpoint, plan, map);
        for (const auto* key : {"direct/test", "summon/test", "independent/test"}) ok &= !restored.reserve(key);
    }
    std::array<uint8_t, 0x140> wait_storage{};
    const auto wait = reinterpret_cast<uintptr_t>(wait_storage.data());
    put(wait, 0, recovery_image + 0x200);
    put(wait, 0x40, owner);
    put(wait, 0x88, uint64_t{16});
    *reinterpret_cast<save::NativeString*>(wait + 0x58) = recipe_text("");
    recovery_stop_maintaining = [](uintptr_t, uintptr_t output, uint64_t mask, const char* label) {
        stopped_correctly = mask == (uint64_t{1} << 44) && !*label;
        return output;
    };
    for (const bool partial : {false, true}) {
        map_freed();
        ok &= recipe_state.reset(plan, map);
        recipe_library.plan = plan; recipe_library.map = map;
        recipe_stamp = native::observation_stamp();
        native_recipes["origin"].definition.origin = "origin";
        request.owner = "arena"; request.logical_mask = 16; ok &= recipe_state.request("blocked", request);
        request.owner = "other"; ok &= recipe_state.request("unrelated", request);
        if (partial) {
            const auto emission = recipe_state.reserve("blocked");
            ok &= emission && recipe_state.created("blocked", *emission) &&
                recipe_state.actor_changed(emission->actor, RecipeActorState::dead);
        }
        for (const auto manager : {owner, other}) put(manager, 0xc88, int32_t{0});
        commands = {wait, 0};
        put(owner, 0xcc0, reinterpret_cast<uintptr_t>(commands.data())); put(owner, 0xcc8, int32_t{1});
        remaining_wait_started[wait] = {GetTickCount64(), *reinterpret_cast<const uintptr_t*>(wait)};
        recovery::Context empty;
        empty.map = map;
        ok &= scan_recovery(address, empty) && empty.managers.size() == 2 &&
            empty.managers[0].recipe_blocked && !empty.managers[1].recipe_blocked;
        ++recipe_stamp;
        recovery::Context stale;
        stale.map = map;
        ok &= scan_recovery(address, stale) && !stale.managers[0].recipe_blocked;
        --recipe_stamp;
        put(wait, 0, recovery_image + 0x208);
        recovery::Context reused; reused.map = map;
        // the type descriptor stays readable while the observed wait identity changes.
        put(recovery_image, 0x200, recovery_image + 0x40);
        ok &= scan_recovery(address, reused) && !reused.managers[0].recipe_blocked;
        put(wait, 0, recovery_image + 0x200);
        const auto before_wait = wait_storage;
        const auto before_manager = managers[0];
        const auto deaths = recipe_state.deaths("arena", 16, "");
        empty.stamp = native::observation_stamp();
        recovery::Attempt attempt;
        attempt.stamp = empty.stamp; attempt.targets = {empty.managers[0]};
        ok &= !credit_at_most(recipe_state.outstanding("arena", 16, ""), 0) &&
            drain_recovery_impl(attempt, empty) && recipe_state.requests().at("blocked").closed &&
            !recipe_state.requests().at("unrelated").closed &&
            credit_at_most(recipe_state.outstanding("arena", 16, ""), 0) &&
            !credit_at_most(recipe_state.outstanding("other", 16, ""), 0) &&
            !attempt.actors_killed && !attempt.kills_suppressed && wait_storage == before_wait && managers[0] == before_manager;
        ok &= recipe_state.deaths("arena", 16, "").size() == deaths.size();
        recovery::Context closed;
        closed.map = map;
        ok &= scan_recovery(address, closed) && !closed.managers[0].recipe_blocked;
        put(owner, 0xdb2, uint8_t{0});
        recovery::Context inactive; inactive.map = map;
        ok &= scan_recovery(address, inactive);
        put(owner, 0xdb2, uint8_t{1});
        recovery::Context boss; boss.map = "game/sp/e1m4_boss/e1m4_boss";
        ok &= scan_recovery(address, boss);
        const auto diagnostics = commands::diagnostic_history();
        for (const auto* reason : {"inactive_flags", "no_live_ai_or_owned_combat_request", "boss"})
            ok &= std::any_of(diagnostics.begin(), diagnostics.end(), [&](const auto& line) {
                return line.find(std::string("reason=") + reason) != std::string::npos;
            });
        set_recipe_fault(__func__, "request_unavailable", "blocked", 0x1234, owner);
        const auto first = recipe_fault_diagnostic();
        ok &= first.find("request=blocked actor=1234 recipe=origin") != std::string::npos &&
            first.find("owner=arena") != std::string::npos;
        set_recipe_fault(__func__, "later_failure", "unrelated");
        ok &= recipe_fault_diagnostic() == first;
    }
    map_freed();
    definition_name = saved_name; actor_identifier = saved_identifier;
    cast_maintain = saved_maintain; cast_stagger = saved_stagger;
    recovery_stop_maintaining = saved_stop; recovery_image = saved_image;
    return ok;
}

#endif

}
