#include "combat_native.h"
#include "native_runtime.h"
#include "native_target.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace sentinel::combat {
namespace {
SRWLOCK lock = SRWLOCK_INIT;
Model model;
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
    if (!TryAcquireSRWLockExclusive(&lock)) { ++observation_gaps; return; }
    model.registered(world, object, slot, spawn_id);
    ReleaseSRWLockExclusive(&lock);
}
Capture capture(uintptr_t compiler, uintptr_t victim) {
    Capture value{};
    if (!enabled.load(std::memory_order_acquire)) return value;
    const auto world = primary_world();
    if (!world || compiler != world + 0xac660) return value;
    uint32_t slot = 0, spawn_id = 0;
    if (!object_identity(world, victim, slot, spawn_id)) { ++observation_gaps; return value; }
    if (!TryAcquireSRWLockExclusive(&lock)) { ++observation_gaps; return value; }
    value = model.capture(world, victim, slot, spawn_id, GetTickCount64(),
                          native::observation_stamp(), GetCurrentThreadId());
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
        else if (!TryAcquireSRWLockExclusive(&lock)) ++observation_gaps;
        else {
            const auto result = model.publish(*pending_death, decode_record(bytes), GetTickCount64(),
                                               native::observation_stamp(), GetCurrentThreadId());
            if (result == Publication::missing_registration) ++observation_gaps;
            ReleaseSRWLockExclusive(&lock);
        }
    }
    original_notify(analytics, record);
}
native::Target target(uintptr_t base, uint32_t rva, const char* hex) {
    native::Target t{};
    t.address = base + rva;
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t n = 0; n < t.bytes.size(); ++n)
        t.bytes[n] = static_cast<uint8_t>((nibble(hex[n * 2]) << 4) | nibble(hex[n * 2 + 1]));
    return t;
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
        target(binding.image.base, 0x6cbc40, "4053565741564883ec5881b9f0e50a0000000001418bd94963f84c8bf2488bf1"),
        target(binding.image.base, 0x120e790, "4053555657415641574881ecb8010000488b053102fa024833c448898424a001"),
        target(binding.image.base, 0x1212290, "4889742420574881ecc0000000488b0534c7f9024833c448898424a800000048")
    };
    const auto deadline = GetTickCount64() + 10000;
    for (const auto& site : sites) {
        const auto why = native::validate_target(memory, binding.image, site, stop_event, deadline);
        if (why) { install_reason.store(why); return; }
    }
    root_address = binding.root;
    void* detours[] = {reinterpret_cast<void*>(register_detour), reinterpret_cast<void*>(dispatch_detour),
                       reinterpret_cast<void*>(notify_detour)};
    void** originals[] = {reinterpret_cast<void**>(&original_register),
                          reinterpret_cast<void**>(&original_dispatch), reinterpret_cast<void**>(&original_notify)};
    for (size_t n = 0; n < 3; ++n) {
        if (MH_CreateHook(reinterpret_cast<void*>(sites[n].address), detours[n], originals[n]) != MH_OK) {
            install_reason.store(SC_NATIVE_HOOK_FAILED); return;
        }
    }
    for (const auto& site : sites) {
        if (MH_EnableHook(reinterpret_cast<void*>(site.address)) != MH_OK) {
            install_reason.store(SC_NATIVE_HOOK_FAILED); return;
        }
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
    if (!TryAcquireSRWLockExclusive(&lock)) { ++observation_gaps; return; }
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
void stop() {
    enabled.store(false, std::memory_order_release);
    install_reason.store(SC_NATIVE_STOPPED);
}
}
