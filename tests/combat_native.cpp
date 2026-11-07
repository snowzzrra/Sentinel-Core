#include "../src/combat_native.cpp"

namespace sentinel::combat {
namespace {
template<class F> void with_combat_reader(F operation) {
    const auto acquired = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const auto begun = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!acquired || !begun) std::abort();
    std::thread reader([&] {
        AcquireSRWLockShared(&lock);
        SetEvent(acquired);
        WaitForSingleObject(begun, INFINITE);
        ReleaseSRWLockShared(&lock);
    });
    WaitForSingleObject(acquired, INFINITE);
    SetEvent(begun);
    operation();
    reader.join();
    CloseHandle(acquired);
    CloseHandle(begun);
}
std::array<uint8_t, 0x128> test_death_record{};
void test_register(uintptr_t, uintptr_t, int32_t, uint32_t, uint8_t, uint8_t) {}
void test_notify(uintptr_t, const void*) {}
void test_dispatch(uintptr_t compiler, uintptr_t, uintptr_t, uintptr_t, int32_t, int32_t,
                   uintptr_t, const void*, uint8_t, float) {
    with_combat_reader([&] { notify_detour(compiler + 0x18, test_death_record.data()); });
}
void test_recovery_dispatch(uintptr_t compiler, uintptr_t inflictor, uintptr_t attacker, uintptr_t victim,
                            int32_t group, int32_t attack_type, uintptr_t declaration, uintptr_t tag, float amount) {
    dispatch_detour(compiler, inflictor, attacker, victim, group, attack_type, declaration,
        reinterpret_cast<const void*>(tag), 0, amount);
}
}
bool test_observation_contention() {
    const auto saved_model = std::make_unique<Model>(model);
    const auto saved_root = root_address;
    const auto saved_enabled = enabled.load();
    const auto saved_gaps = observation_gaps.load();
    const auto saved_register = original_register;
    const auto saved_dispatch = original_dispatch;
    const auto saved_notify = original_notify;
    std::vector<uint8_t> world(0x126680);
    std::array<uint8_t, 0x58> root{};
    std::array<uint8_t, 0x16800> victim{};
    const auto world_address = reinterpret_cast<uintptr_t>(world.data());
    const auto victim_address = reinterpret_cast<uintptr_t>(victim.data());
    const uint32_t slot = 1, spawn_id = 7;
    std::memcpy(root.data() + 0x50, &world_address, sizeof(world_address));
    std::memcpy(victim.data() + 0x10, &slot, sizeof(slot));
    std::memcpy(world.data() + 0x1af8 + slot * 8, &victim_address, sizeof(victim_address));
    std::memcpy(world.data() + 0x21af8 + slot * 4, &spawn_id, sizeof(spawn_id));
    root_address = reinterpret_cast<uintptr_t>(root.data());
    original_register = test_register;
    original_dispatch = test_dispatch;
    original_notify = test_notify;
    enabled.store(true);
    model.transition(1);
    Context context{};
    context.generation = 1;
    context.stamp = native::observation_stamp();
    context.sampled_at_ms = GetTickCount64();
    context.world = world_address;
    context.player = 1;
    context.owner_thread = GetCurrentThreadId();
    std::memset(context.namespace_id, 'f', 64);
    std::memcpy(context.map.bytes, "e1m1_intro", sizeof("e1m1_intro"));
    context.map.length = static_cast<uint32_t>(std::strlen(context.map.bytes));
    context.map.validity = SC_OBSERVATION_OBSERVED;
    sc_combat_query query{};
    std::memcpy(query.namespace_id, context.namespace_id, sizeof(query.namespace_id));
    query.map_count = 1;
    std::memcpy(query.maps[0], context.map.bytes, sizeof(query.maps[0]));
    const auto before = inspect(query).qualified_kills;
    with_combat_reader([&] { observe(context); });
    with_combat_reader([&] { register_detour(world_address, victim_address, slot, spawn_id, 0, 0); });
    const auto initially_alive = !confirmed_dead(world_address, victim_address);
    test_death_record = {};
    const auto field = [&](size_t offset, auto value) {
        std::memcpy(test_death_record.data() + offset, &value, sizeof(value));
    };
    field(0x48, spawn_id);
    field(0x4c, int32_t{1});
    field(0x38, int32_t{1});
    field(0x124, int32_t{1});
    const auto die = [&] { dispatch_detour(world_address + 0xac660, 0, 0, victim_address, 0, 0, 0, nullptr, 0, 1); };
    with_combat_reader(die);
    die();
    const auto result = inspect(query);
    const auto passed = initially_alive && confirmed_dead(world_address, victim_address) &&
        result.qualified_kills == before + 1 && result.observation_gaps == saved_gaps;
    register_detour(world_address, victim_address, slot, spawn_id, 0, 0);
    const auto reused = !confirmed_dead(world_address, victim_address);
    const auto recovery_before = inspect(query).qualified_kills;
    const auto recovery_passed = expanded::test_recovery_kill(world_address, victim_address, test_recovery_dispatch) &&
        confirmed_dead(world_address, victim_address) && inspect(query).qualified_kills == recovery_before;
    model = *saved_model;
    root_address = saved_root;
    enabled.store(saved_enabled);
    observation_gaps.store(saved_gaps);
    original_register = saved_register;
    original_dispatch = saved_dispatch;
    original_notify = saved_notify;
    return passed && reused && recovery_passed;
}
}
