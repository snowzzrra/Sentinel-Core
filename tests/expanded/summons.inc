namespace sentinel::expanded::detail {

#ifdef SC_NATIVE_TESTING

bool darklord_fault_call(uintptr_t component) {
    __try { darklord_summon_detour(component); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode() == 0xe0700003; }
    return false;
}

bool test_darklord_summon_scope() {
    std::array<uintptr_t, 4> slots{0, 11, 0, 22};
    std::array<uint8_t, 0x1f0> declaration{};
    std::array<uintptr_t, 3> component{0, 0, reinterpret_cast<uintptr_t>(declaration.data())};
    *reinterpret_cast<uintptr_t*>(declaration.data() + 0x1e0) = reinterpret_cast<uintptr_t>(slots.data());
    *reinterpret_cast<int32_t*>(declaration.data() + 0x1e8) = 2;
    const auto previous = original_darklord_summon;
    const auto previous_stopped = stopped.load();
    const auto previous_fault = recipe_fault.exchange(false);
    static bool observed = false, fault = false;
    static unsigned calls = 0;
    calls = 0;
    original_darklord_summon = [](uintptr_t pointer) {
        const auto decl = *reinterpret_cast<const uintptr_t*>(pointer + 0x10);
        const auto values = *reinterpret_cast<const uintptr_t**>(decl + 0x1e0);
        observed = values[1] == 11 && values[3] == 22;
        ++calls;
        if (fault) RaiseException(0xe0700003, 0, 0, nullptr);
    };
    stopped.store(false);
    darklord_summon_detour(reinterpret_cast<uintptr_t>(component.data()));
    bool ok = observed && calls == 1 && !recipe_fault.load() && slots == std::array<uintptr_t, 4>{0, 11, 0, 22};
    stopped.store(true); observed = false; fault = true;
    ok &= darklord_fault_call(reinterpret_cast<uintptr_t>(component.data())) &&
        observed && calls == 2 && slots == std::array<uintptr_t, 4>{0, 11, 0, 22};
    stopped.store(true); fault = false; observed = false;
    darklord_summon_detour(reinterpret_cast<uintptr_t>(component.data()));
    ok &= observed && calls == 3 && slots == std::array<uintptr_t, 4>{0, 11, 0, 22};
    stopped.store(previous_stopped);
    recipe_fault.store(previous_fault);
    fault = false; original_darklord_summon = previous;
    return ok;
}

bool test_darklord_telemetry() {
    static std::vector<std::string> lines;
    static std::array<uintptr_t, 2> actors{};
    static std::array<uint32_t, 3> handles{9, 111, 222};
    std::vector<uint8_t> world(0x9a100), component(0x400);
    std::array<std::vector<uint8_t>, 2> actor_bytes{std::vector<uint8_t>(0x16830), std::vector<uint8_t>(0x16830)};
    std::array<std::vector<uint8_t>, 2> animator_bytes{std::vector<uint8_t>(0xa300), std::vector<uint8_t>(0xa300)};
    std::array<uintptr_t, 44> knight{}, pain{};
    knight[1] = reinterpret_cast<uintptr_t>("ai/heavy/hellknight");
    pain[1] = reinterpret_cast<uintptr_t>("ai/boss/darklord/summoned/painelemental");
    std::array<uintptr_t, 4> slots{0, reinterpret_cast<uintptr_t>(knight.data()), 0, reinterpret_cast<uintptr_t>(pain.data())};
    std::vector<uint8_t> declaration(0x1f0);
    *reinterpret_cast<uintptr_t*>(declaration.data() + 0x1e0) = reinterpret_cast<uintptr_t>(slots.data());
    *reinterpret_cast<int32_t*>(declaration.data() + 0x1e8) = 2;
    std::array<uintptr_t, 0x100 / 8> world_table{}, actor_table{}, animator_table{};
    world_table[0x88 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t, uint32_t* handle) -> uintptr_t {
        return *handle == 111 ? actors[0] : *handle == 222 ? actors[1] : uintptr_t{1};
    });
    actor_table[0x60 / 8] = reinterpret_cast<uintptr_t>(+[](uintptr_t) { return "fixture_actor"; });
    const auto previous_image = darklord_image, previous_world = world_global;
    const auto previous_sink = darklord_sink;
    const auto previous_clock = native_clock;
    const auto previous_original = original_darklord_summon;
    auto previous_calls = std::move(darklord_calls);
    darklord_calls.clear(); lines.clear();
    darklord_sink = +[](const char* line, bool) { lines.emplace_back(line); };
    darklord_image = world_table[0x88 / 8] - 0x6998e0;
    animator_table[0x98 / 8] = darklord_image + 0x4947b0;
    uintptr_t world_address = reinterpret_cast<uintptr_t>(world.data());
    world_global = reinterpret_cast<uintptr_t>(&world_address);
    *reinterpret_cast<uintptr_t*>(world.data()) = reinterpret_cast<uintptr_t>(world_table.data());
    *reinterpret_cast<uintptr_t*>(world.data() + 0x9a068) = reinterpret_cast<uintptr_t>("game/dlc2/e5m4_boss/e5m4_boss");
    *reinterpret_cast<uint32_t*>(world.data() + 0x9a038) = 0x1234;
    const auto address = reinterpret_cast<uintptr_t>(component.data());
    *reinterpret_cast<uintptr_t*>(address + 8) = 77;
    *reinterpret_cast<uintptr_t*>(address + 0x10) = reinterpret_cast<uintptr_t>(declaration.data());
    *reinterpret_cast<uintptr_t*>(address + 0x3d8) = reinterpret_cast<uintptr_t>(handles.data());
    *reinterpret_cast<int32_t*>(address + 0x3e0) = 1;
    for (size_t i = 0; i < actors.size(); ++i) {
        actors[i] = reinterpret_cast<uintptr_t>(actor_bytes[i].data());
        const auto actor = actors[i], animator = reinterpret_cast<uintptr_t>(animator_bytes[i].data());
        *reinterpret_cast<uintptr_t*>(actor) = reinterpret_cast<uintptr_t>(actor_table.data());
        *reinterpret_cast<int32_t*>(actor + 0x10) = static_cast<int32_t>(i + 7);
        *reinterpret_cast<uintptr_t*>(actor + 0xa8) = reinterpret_cast<uintptr_t>(i ? pain.data() : knight.data());
        *reinterpret_cast<uintptr_t*>(actor + 0x5960) = animator;
        *reinterpret_cast<float*>(actor + 0x65a4) = 100;
        *reinterpret_cast<uintptr_t*>(animator + 0x9890) = reinterpret_cast<uintptr_t>(animator_table.data());
        *reinterpret_cast<uint8_t*>(animator + 0x9890 + 0x370) = 1;
        *reinterpret_cast<uint16_t*>(animator + 0x9890 + 0x5ac) = 7;
        *reinterpret_cast<int32_t*>(animator + 0x9890 + 0x5ec) = 1;
        *reinterpret_cast<uintptr_t*>(world.data() + 0x1af8 + (i + 7) * 8) = actor;
        *reinterpret_cast<uint32_t*>(world.data() + 0x21af8 + (i + 7) * 4) = handles[i + 1];
    }
    native_clock = +[](uintptr_t, uint64_t* value, int32_t) -> uint64_t* { *value = 123456; return value; };
    original_darklord_summon = +[](uintptr_t pointer) { *reinterpret_cast<int32_t*>(pointer + 0x3e0) = 3; };
    const auto previous_stopped = stopped.exchange(false);
    darklord_summon_detour(address);
    stopped.store(previous_stopped);
    bool ok = darklord_calls.size() == 1;
    if (ok) {
        auto& call = *darklord_calls.begin()->second;
        ok &= slots[1] == reinterpret_cast<uintptr_t>(knight.data()) && call.actors.size() == 2;
        const auto now = call.actors.empty() ? GetTickCount64() : call.actors[0].created;
        for (auto& actor : call.actors) actor.created = actor.sampled = now;
        const auto before = lines.size();
        darklord_sample(world_address, now + 250); darklord_sample(world_address, now + 500);
        ok &= lines.size() == before;
        *reinterpret_cast<float*>(actors[0] + 0x74) = 1;
        *reinterpret_cast<uint16_t*>(animator_bytes[0].data() + 0x9890 + 0x5ac) = 8;
        for (const auto time : {1000, 2000, 3000, 5000, 8000, 12000}) darklord_sample(world_address, now + time);
        ok &= darklord_calls.empty();
    }
    auto broken = darklord_begin(1);
    if (broken) {
        darklord_slots(broken.get(), 1);
        darklord_call_start(broken.get()); darklord_end(broken.get(), false, 1);
        darklord_track(*broken, 333);
        broken->lines = 1023;
        darklord_emit(*broken, "sample", "unused=1"); darklord_emit(*broken, "sample", "unused=2");
    } else ok = false;
    const auto contains = [&](const char* a, const char* b) {
        return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
            return line.find(a) != line.npos && line.find(b) != line.npos;
        });
    };
    ok &= contains("event=begin", "rng_before=0x1234 count_before=1") &&
        contains("event=end", "count_before=1 count_after=3 original_us=") &&
        contains("event=slot", "slot=0 decl=ai/heavy/hellknight inherit=null") &&
        contains("event=slot", "slot=1 decl=ai/boss/darklord/summoned/painelemental") &&
        contains("event=identity", "slot=0 entity_name=fixture_actor decl=ai/heavy/hellknight inherit=null") &&
        contains("event=actor", "anim_read=ok anim_enabled=1") &&
        contains("event=sample", "recovered_at_ms=1000") &&
        contains("event=summary", "frozen_ms=12000 recovered_at_ms=-1 samples=8") &&
        contains("event=actor", "read_fault=1") && contains("event=read_fault", "field=handle_diff") &&
        contains("event=limit", "reason=invocation_line_cap");
    for (const auto& line : lines) ok &= line.rfind("AP_DARKLORD ", 0) == 0 && line.find(" seq=") != line.npos &&
        line.find(" at_ms=") != line.npos && line.find(" game_tick=") != line.npos && line.find(" map=e5m4_boss ") != line.npos;
    Snapshot identity; identity.pid = GetCurrentProcessId(); identity.process_created = 123456;
    startup_log::record(identity, 0);
    for (const auto& line : lines) ok &= startup_log::darklord(line.c_str()) == ERROR_SUCCESS;
    darklord_clear(); darklord_calls = std::move(previous_calls);
    original_darklord_summon = previous_original; native_clock = previous_clock;
    darklord_image = previous_image; world_global = previous_world; darklord_sink = previous_sink;
    return ok;
}

#endif

}
