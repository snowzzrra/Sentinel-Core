#include "engine/builds/build_descriptor.h"
#include "combat_native.h"
#include "native_runtime.h"
#include "native_target.h"
#include "save_session.h"
#include "engine/hooks/hook_set.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#ifdef SC_NATIVE_TESTING
#include "expanded_native.h"
#include <memory>
#include <thread>
#include <vector>
#endif

namespace sentinel::combat {
namespace {
hooks::HookSet hook_set("combat");
SRWLOCK lock = SRWLOCK_INIT;
Model model;
save::BTrace death_trace;
sc_native_scope identity{};
uintptr_t root_address = 0;
std::atomic<bool> enabled{false};
std::atomic<uint32_t> install_reason{SC_NATIVE_NOT_STARTED};
std::atomic<uint64_t> observation_gaps{0};
thread_local const Capture* pending_death = nullptr;
using RegisterObject = void(*)(uintptr_t, uintptr_t, int32_t, uint32_t, uint8_t, uint8_t);
using RegisterDeath = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, int32_t, int32_t,
                             uintptr_t, const void*, uint8_t, float);
using NotifyDeath = void(*)(uintptr_t, const void*);
RegisterObject original_register = nullptr;
RegisterDeath original_dispatch = nullptr;
NotifyDeath original_notify = nullptr;

template<class T> bool read(uintptr_t address, T& value) {
    engine::LocalMemory memory;
    return address && !memory.copy(address, &value, sizeof(value)).reason;
}
uintptr_t primary_world() {
    uintptr_t world = 0;
    read(root_address + 0x50, world);
    return world;
}
bool object_identity(uintptr_t world, uintptr_t object, uint32_t& slot, uint32_t& spawn_id) {
    uintptr_t registered_object = 0;
    return object && read(object + 0x10, slot) && slot < managed_slots &&
        read(world + 0x1af8 + size_t(slot) * 8, registered_object) && registered_object == object &&
        read(world + 0x21af8 + size_t(slot) * 4, spawn_id);
}
void register_detour(uintptr_t world, uintptr_t object, int32_t requested_slot,
                     uint32_t requested_id, uint8_t network, uint8_t flag) {
    original_register(world, object, requested_slot, requested_id, network, flag);
    if (!enabled.load(std::memory_order_acquire) || world != primary_world()) return;
    uint32_t slot = 0, spawn_id = 0;
    if (!object_identity(world, object, slot, spawn_id)) { ++observation_gaps; return; }
    AcquireSRWLockExclusive(&lock);
    model.registered(world, object, slot, spawn_id);
    ReleaseSRWLockExclusive(&lock);
}
Capture capture(uintptr_t compiler, uintptr_t victim) {
    Capture value{};
    if (!enabled.load(std::memory_order_acquire) || save::session().campaign_run.snapshot().native_subtype==6) return value;
    const auto world = primary_world();
    if (!world || compiler != world + 0xac660) {
        death_trace.record(save::BStage::combat_capture, save::BStatus::pending, "death_compiler_scope",
            0, {{"world_present", world != 0}, {"compiler_matches", world && compiler == world + 0xac660}});
        return value;
    }
    uint32_t slot = 0, spawn_id = 0;
    if (!object_identity(world, victim, slot, spawn_id)) {
        ++observation_gaps;
        death_trace.record(save::BStage::combat_capture, save::BStatus::refused, "victim_registration_unavailable");
        return value;
    }
    AcquireSRWLockExclusive(&lock);
    value = model.capture(world, victim, slot, spawn_id, GetTickCount64(),
                          native::observation_stamp(), GetCurrentThreadId());
    death_trace.record(save::BStage::combat_capture, value.registration_serial ? save::BStatus::succeeded : save::BStatus::pending,
        "death_callback_capture", 0, {{"context_ready", model.ready(GetTickCount64())},
        {"generation", model.generation()}, {"callback_thread", GetCurrentThreadId()},
        {"context_thread", value.context.owner_thread}, {"registered", value.registration_serial != 0}});
    ReleaseSRWLockExclusive(&lock);
    return value;
}
void dispatch_detour(uintptr_t compiler, uintptr_t inflictor, uintptr_t attacker,
                     uintptr_t victim, int32_t group, int32_t attack_type,
                     uintptr_t declaration, const void* weapon_tag, uint8_t listener_flag,
                     float damage_amount) {
    const auto saved = capture(compiler, victim);
    const auto previous = pending_death;
    pending_death = &saved;
    __try {
        original_dispatch(compiler, inflictor, attacker, victim, group, attack_type,
                          declaration, weapon_tag, listener_flag, damage_amount);
    } __finally {
        pending_death = previous;
        AcquireSRWLockExclusive(&lock);
        model.release(saved);
        ReleaseSRWLockExclusive(&lock);
    }
}
void notify_detour(uintptr_t analytics, const void* record) {
    if (enabled.load(std::memory_order_acquire) && pending_death &&
        pending_death->context.world && analytics == pending_death->context.world + 0xac678) {
        std::array<uint8_t, 0x128> bytes{};
        if (!read(reinterpret_cast<uintptr_t>(record), bytes)) ++observation_gaps;
        else {
            AcquireSRWLockExclusive(&lock);
            const auto decoded = decode_record(bytes);
            const auto result = model.publish(*pending_death, decoded, GetTickCount64(),
                                               native::observation_stamp(), GetCurrentThreadId());
            death_trace.record(save::BStage::combat_publication, result == Publication::published ?
                save::BStatus::succeeded : save::BStatus::pending, "death_analytics_publication", 0,
                {{"result", static_cast<unsigned>(result)}, {"victim_type", decoded.victim_type},
                 {"game_mode", decoded.game_mode}, {"attack_type", decoded.attack_type},
                 {"spawn_matches", decoded.victim_spawn_id == pending_death->victim_spawn_id},
                 {"callback_thread", GetCurrentThreadId()}, {"context_thread", pending_death->context.owner_thread}});
            if (result == Publication::missing_registration) ++observation_gaps;
            ReleaseSRWLockExclusive(&lock);
        }
    }
    original_notify(analytics, record);
}
void register_entry(uintptr_t world, uintptr_t object, int32_t slot, uint32_t id, uint8_t network, uint8_t flag) {
    const auto lease = hook_set.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<RegisterObject>(lease.original)(world, object, slot, id, network, flag);
        register_detour(world, object, slot, id, network, flag);
    } __finally { hook_set.leave(lease); }
}
void dispatch_entry(uintptr_t compiler, uintptr_t inflictor, uintptr_t attacker, uintptr_t victim,
                    int32_t group, int32_t attack_type, uintptr_t declaration, const void* tag, uint8_t flag, float damage) {
    const auto lease = hook_set.enter(1);
    __try {
        if (!lease.committed) return reinterpret_cast<RegisterDeath>(lease.original)(compiler, inflictor, attacker,
            victim, group, attack_type, declaration, tag, flag, damage);
        dispatch_detour(compiler, inflictor, attacker, victim, group, attack_type, declaration, tag, flag, damage);
    } __finally { hook_set.leave(lease); }
}
void notify_entry(uintptr_t analytics, const void* record) {
    const auto lease = hook_set.enter(2);
    __try {
        if (!lease.committed) return reinterpret_cast<NotifyDeath>(lease.original)(analytics, record);
        notify_detour(analytics, record);
    } __finally { hook_set.leave(lease); }
}

}
void prepare(const Snapshot& source) {
    AcquireSRWLockExclusive(&lock);
    identity.pid = source.pid;
    identity.process_created = source.process_created;
    std::memcpy(identity.instance_id, source.instance.data(), sizeof(identity.instance_id));
    ReleaseSRWLockExclusive(&lock);
}
void install(const engine::Binding& binding, HANDLE stop_event) {
    if (enabled.load() || install_reason.load() == SC_NATIVE_STOPPED) return;
    install_reason.store(SC_NATIVE_UNKNOWN_BUILD);
    engine::LocalMemory memory;
    const native::Target sites[] = {
        native::target_from_hex(binding.image.base + builds::steam_20260818().combat[0].rva, builds::steam_20260818().combat[0].prefix),
        native::target_from_hex(binding.image.base + builds::steam_20260818().combat[1].rva, builds::steam_20260818().combat[1].prefix),
        native::target_from_hex(binding.image.base + builds::steam_20260818().combat[2].rva, builds::steam_20260818().combat[2].prefix)
    };
    const auto deadline = GetTickCount64() + 10000;
    for (const auto& site : sites) {
        const auto why = native::validate_target(memory, binding.image, site, stop_event, deadline);
        if (why) { install_reason.store(why); return; }
    }
    root_address = binding.root;
    const hooks::HookSpec specs[]{
        hooks::bind(sites[0].address, register_entry, original_register),
        hooks::bind(sites[1].address, dispatch_entry, original_dispatch),
        hooks::bind(sites[2].address, notify_entry, original_notify)};
    if (!hook_set.install(specs, stop_event)) {
        install_reason.store(SC_NATIVE_HOOK_FAILED); return;
    }
    install_reason.store(SC_NATIVE_NONE);
    enabled.store(true, std::memory_order_release);
}
void transition(uint64_t generation) {
    AcquireSRWLockExclusive(&lock);
    model.transition(generation);
    ReleaseSRWLockExclusive(&lock);
}
void observe(const Context& context) {
    if (!enabled.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&lock);
    model.context(context);
    ReleaseSRWLockExclusive(&lock);
}
sc_combat_snapshot inspect(const sc_combat_query& query) {
    sc_combat_snapshot out{};
    out.size = sizeof(out);
    out.abi_version = SC_COMBAT_ABI_VERSION;
    AcquireSRWLockShared(&lock);
    out.scope = identity;
    out.observation_gaps = observation_gaps.load();
    out.availability = enabled.load() ? SC_NATIVE_ENABLED : SC_NATIVE_DISABLED;
    out.reason = install_reason.load();
    model.snapshot(query, out);
    ReleaseSRWLockShared(&lock);
    return out;
}
save::BSnapshot diagnostics() { return death_trace.snapshot(); }
bool confirmed_dead(uintptr_t world, uintptr_t actor) {
    if (!enabled.load(std::memory_order_acquire)) return false;
    uint32_t slot = 0, spawn_id = 0;
    if (!object_identity(world, actor, slot, spawn_id)) return false;
    AcquireSRWLockShared(&lock);
    const auto dead = model.confirmed_dead(world, actor, slot, spawn_id);
    ReleaseSRWLockShared(&lock);
    return dead;
}
bool suppress(uint64_t generation, uintptr_t world, uintptr_t actor) {
    if (!enabled.load(std::memory_order_acquire)) return false;
    uint32_t slot = 0, spawn_id = 0;
    if (!object_identity(world, actor, slot, spawn_id)) return false;
    AcquireSRWLockExclusive(&lock);
    const auto marked = model.suppress(generation, world, actor, slot, spawn_id);
    ReleaseSRWLockExclusive(&lock);
    return marked;
}
void stop() {
    enabled.store(false, std::memory_order_release);
    install_reason.store(SC_NATIVE_STOPPED);
}
}
