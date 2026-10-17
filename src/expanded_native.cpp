#include "expanded_native.h"
#include "expanded.h"
#include "combat_native.h"
#include "native_target.h"
#include "save_session.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cstddef>
#include <memory>

namespace sentinel::expanded {
namespace {
using Movement = float(__fastcall*)(uintptr_t);
using Controller = float(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, float);
using DefinitionName = const char*(__fastcall*)(uintptr_t);
Movement original_movement = nullptr;
Controller original_controller = nullptr;
DefinitionName definition_name = nullptr;
struct HostList { uintptr_t* data; int32_t count, capacity; uint8_t allocator[8]; };
using CollectHosts = void(__fastcall*)(HostList*);
using ResolveActor = uintptr_t(__fastcall*)(uintptr_t);
using GrowHosts = void(__fastcall*)(HostList*, int32_t);
CollectHosts original_hosts = nullptr;
ResolveActor resolve_actor = nullptr;
GrowHosts grow_hosts = nullptr;
uintptr_t world_global = 0;
using SpawnActor = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
SpawnActor original_spawn = nullptr;
using SpawnGroup = uint64_t(__fastcall*)(uintptr_t, uint64_t*, uintptr_t);
using RegisterSpawn = void(__fastcall*)(uintptr_t, uint64_t*);
SpawnGroup original_group = nullptr;
RegisterSpawn original_register_spawn = nullptr;
using NativeAdmission = bool(__fastcall*)(uintptr_t, int32_t);
NativeAdmission native_admission = nullptr;

std::atomic<bool> stopped{true};
thread_local uintptr_t active_ai = 0;
thread_local float active_speed = 1.0f;

bool read_definition_name(uintptr_t ai, char (&copy)[512]) noexcept {
    __try {
        const auto* name = definition_name(ai);
        if (!name) return false;
        for (size_t size = 0; size < sizeof(copy); ++size) {
            copy[size] = name[size];
            if (!copy[size]) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
float scale_movement(uintptr_t ai, float value) noexcept {
    char name[512];
    return read_definition_name(ai, name) ? movement_speed(value, name) : value;
}
bool private_supply(std::string_view name, std::string_view suffix) noexcept {
    constexpr std::string_view prefix = "ap_v070_supply_";
    if (name.size() != prefix.size() + 20 + suffix.size() || name.substr(0, prefix.size()) != prefix ||
        name.substr(prefix.size() + 20) != suffix) return false;
    for (const auto digit : name.substr(prefix.size(), 20))
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) return false;
    return true;
}
bool private_supplier(uintptr_t ai) noexcept {
    char name[512];
    return read_definition_name(ai, name) && private_supply(name, "_actors_ai__ap_v070_s00100");
}
void __fastcall occupied_hosts_detour(HostList* out) {
    original_hosts(out);
    if (stopped.load(std::memory_order_acquire)) return;
    const auto world = *reinterpret_cast<const uintptr_t*>(world_global);
    const auto collection = *reinterpret_cast<const uintptr_t*>(world + 0x62088);
    const auto records = *reinterpret_cast<const uintptr_t*>(collection);
    const auto count = *reinterpret_cast<const int32_t*>(collection + 8);
    for (int32_t i = 0; i < count; ++i) {
        const auto actor = resolve_actor(records + static_cast<uintptr_t>(i) * 24);
        if (!actor || !private_supplier(actor)) continue;
        bool present = false;
        for (int32_t j = 0; j < out->count; ++j) present |= out->data[j] == actor;
        if (present) continue;
        // The engine owns this scratch list and its allocator flags.
        if (out->count == out->capacity) grow_hosts(out, out->count);
        if (out->count < out->capacity) out->data[out->count++] = actor;
    }
}

unsigned special_kind(uintptr_t actor) {
    char name[512];
    if (read_definition_name(actor, name)) {
        const auto frozen = spawn_limits(name);
        if (frozen) return frozen->kind;
    }
    // Native summon masks can identify Spirit templates or boss proxies.
    constexpr std::string_view kinds[]{"ai/superheavy/marauder", "ai/superheavy/archvile", "ai/ambient/spirit"};
    for (auto definition = *reinterpret_cast<uintptr_t*>(actor + 0xa8); definition;
         definition = *reinterpret_cast<uintptr_t*>(definition + 0x158)) {
        const auto value = *reinterpret_cast<const char**>(definition + 8);
        if (!value) continue;
        const std::string_view inherited(value);
        if (inherited.substr(0, 8) == "ai/boss/") return 3;
        for (unsigned kind = 0; kind < std::size(kinds); ++kind) if (inherited == kinds[kind]) return kind;
    }
    return 3;
}
bool special_admission(const SpawnLimits& limits) {
    __try {
        const auto world = *reinterpret_cast<uintptr_t*>(world_global);
        const auto collection = *reinterpret_cast<uintptr_t*>(world + 0x62088);
        const auto references = *reinterpret_cast<uintptr_t*>(collection);
        const auto count = *reinterpret_cast<int32_t*>(collection + 8);
        std::array<uint32_t, 3> active{};
        for (int32_t index = 0; index < count; ++index) {
            const auto actor = resolve_actor(references + index * 24);
            if (!actor || combat::confirmed_dead(world, actor)) continue;
            const auto kind = special_kind(actor);
            if (kind < active.size()) ++active[kind];
        }
        return spawn_allowed(limits, active);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool supply_admission(uintptr_t world) {
    const auto collection = *reinterpret_cast<uintptr_t*>(world + 0x62088);
    const auto references = *reinterpret_cast<uintptr_t*>(collection);
    const auto count = *reinterpret_cast<int32_t*>(collection + 8);
    const auto capacity = *reinterpret_cast<int32_t*>(collection + 12);
    const auto managers = *reinterpret_cast<uintptr_t*>(world + 0x126658);
    const auto manager_count = *reinterpret_cast<int32_t*>(world + 0x126660);
    int32_t reserved = 0;
    for (int32_t index = 0; index < manager_count; ++index) {
        const auto manager = *reinterpret_cast<uintptr_t*>(managers + index * 0x20 + 8);
        char owner[512];
        if (!manager || !read_definition_name(manager, owner) || !private_supply(owner, "_manager")) continue;
        if (*reinterpret_cast<int32_t*>(manager + 0xcc8) == 0) continue;
        bool present = false;
        for (int32_t entry = 0; entry < count && !present; ++entry) {
            const auto actor = resolve_actor(references + entry * 24);
            char name[512];
            present = actor && read_definition_name(actor, name) && private_supply(name, "_actors_ai__ap_v070_s00100") &&
                std::memcmp(owner, name, 35) == 0 && !combat::confirmed_dead(world, actor);
        }
        reserved += !present;
    }
    if (*reinterpret_cast<const uint8_t*>(collection + 0x13) && static_cast<int64_t>(count) + reserved >= capacity) return false;
    return reserved == 0 || native_admission(world + 0xafd98, 1 - reserved);
}
bool generated_admission(const SpawnLimits& limits) {
    __try {
        const auto world = *reinterpret_cast<uintptr_t*>(world_global);
        return supply_admission(world) && (limits.kind >= 3 || special_admission(limits));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint64_t __fastcall spawn_detour(uintptr_t manager, uintptr_t record, uintptr_t point, uintptr_t fourth, uintptr_t fifth) {
    if (!stopped.load(std::memory_order_acquire)) {
        char name[512];
        if (read_definition_name(point, name)) {
            const auto limits = spawn_limits(name);
            if (limits && !generated_admission(*limits)) return 0;
        }
    }
    return original_spawn(manager, record, point, fourth, fifth);
}

uint64_t __fastcall group_detour(uintptr_t manager, uint64_t* record, uintptr_t group) {
    if (!stopped.load(std::memory_order_acquire)) {
        char name[512];
        if (read_definition_name(group, name)) {
            const auto tracking = spawn_tracking(name);
            if (tracking) {
                const auto source = *record;
                *record = (source & ~encounter_ai_mask) | tracking->physical;
                uint64_t result;
                __try { result = original_group(manager, record, group); }
                __finally { *record = source; }
                return result;
            }
        }
    }
    return original_group(manager, record, group);
}
void __fastcall register_spawn_detour(uintptr_t manager, uint64_t* record) {
    if (!stopped.load(std::memory_order_acquire)) {
        char name[512];
        if (read_definition_name(static_cast<uintptr_t>(record[2]), name)) {
            const auto tracking = spawn_tracking(name);
            // The native callback consumes the record's string and reference members.
            if (tracking) *record = (*record & ~encounter_ai_mask) | tracking->source;
        }
    }
    original_register_spawn(manager, record);
}
float __fastcall movement_detour(uintptr_t ai) {
    const auto value = original_movement(ai);
    return !stopped.load(std::memory_order_acquire) && active_ai == ai ? value * active_speed : value;
}
float __fastcall controller_detour(uintptr_t controller, uintptr_t frame, uintptr_t output, float distance) {
    if (stopped.load(std::memory_order_acquire)) return original_controller(controller, frame, output, distance);
    const auto previous_ai = active_ai;
    const auto previous_speed = active_speed;
    active_ai = *reinterpret_cast<const uintptr_t*>(controller);
    active_speed = scale_movement(active_ai, 1.0f);
    // scale distance and limits together; animation playback keeps its native factor.
    const auto result = original_controller(controller, frame, output, distance * active_speed);
    active_ai = previous_ai;
    active_speed = previous_speed;
    return result;
}
}
bool install(const engine::Binding& binding, HANDLE stop_event) {
    if (!save::session().campaign_run.enabled()) return true;
    const native::Target movement{binding.image.base + 0x7c7940,
        {0x48,0x8b,0x05,0x09,0xf6,0xe3,0x03,0x0f,0x57,0xc9,0xf3,0x0f,0x10,0x40,0x0c,0x0f,
         0x2f,0xc1,0x73,0x0f,0x48,0x8b,0x81,0x60,0x59,0x00,0x00,0xf3,0x0f,0x10,0x80,0xe8}};
    const native::Target name{binding.image.base + 0x6e4e90,
        {0x48,0x8b,0x81,0xa8,0x00,0x00,0x00,0x48,0x85,0xc0,0x75,0x08,0x48,0x8d,0x05,0x1d,
         0x2b,0x37,0x02,0xc3,0x48,0x8b,0x40,0x08,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}};
    const native::Target controller{binding.image.base + 0x8c5cb0,
        {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x60,0x44,
         0x0f,0x29,0x44,0x24,0x30,0x48,0x8b,0xd9,0x48,0x8b,0x09,0x44,0x0f,0x28,0xc3,0xf3}};
    const native::Target hosts{binding.image.base + 0x974070,
        {0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20,0x0f,0xb6,0x41,0x13,0x48,
         0x8b,0xf9,0x84,0xc0,0x74,0x06,0x2c,0x03,0x3c,0x01,0x77,0x29,0x48,0x8b,0x11,0x48}};
    const native::Target spawn{binding.image.base + 0xbba950,
        {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,
         0x24,0x48,0xff,0xff,0xff,0x48,0x81,0xec,0xb8,0x01,0x00,0x00,0x48,0x8b,0x05,0x65}};
    const native::Target group{binding.image.base + 0xbb9c50,
        {0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55,
         0x56,0x41,0x54,0x41,0x56,0x48,0x8d,0x6c,0x24,0xb8,0x48,0x81,0xec,0x48,0x01,0x00}};
    const native::Target registered{binding.image.base + 0xb94f40,
        {0x4c,0x8b,0xdc,0x55,0x53,0x56,0x57,0x49,0x8d,0x6b,0xa1,0x48,0x81,0xec,0xd8,0x00,
         0x00,0x00,0x48,0x8b,0x05,0x7f,0x9a,0x61,0x03,0x48,0x33,0xc4,0x48,0x89,0x45,0x1f}};
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    if (native::validate_leaf_target(memory, binding.image, movement, 36, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, name, 25, stop_event, deadline) ||
        native::validate_target(memory, binding.image, controller, stop_event, deadline) ||
        native::validate_target(memory, binding.image, hosts, stop_event, deadline) ||
        native::validate_target(memory, binding.image, spawn, stop_event, deadline) ||
        native::validate_target(memory, binding.image, group, stop_event, deadline) ||
        native::validate_target(memory, binding.image, registered, stop_event, deadline)) return false;
    definition_name = reinterpret_cast<DefinitionName>(name.address);
    resolve_actor = reinterpret_cast<ResolveActor>(binding.image.base + 0x6cb100);
    grow_hosts = reinterpret_cast<GrowHosts>(binding.image.base + 0x357040);
    native_admission = reinterpret_cast<NativeAdmission>(binding.image.base + 0xb99c80);

    world_global = binding.image.base + 0x45f7370;
    void* sites[] = {reinterpret_cast<void*>(movement.address), reinterpret_cast<void*>(controller.address),
                     reinterpret_cast<void*>(hosts.address), reinterpret_cast<void*>(spawn.address),
                     reinterpret_cast<void*>(group.address), reinterpret_cast<void*>(registered.address)};
    void* detours[] = {reinterpret_cast<void*>(movement_detour), reinterpret_cast<void*>(controller_detour),
                       reinterpret_cast<void*>(occupied_hosts_detour), reinterpret_cast<void*>(spawn_detour),
                       reinterpret_cast<void*>(group_detour), reinterpret_cast<void*>(register_spawn_detour)};
    void** originals[] = {reinterpret_cast<void**>(&original_movement), reinterpret_cast<void**>(&original_controller),
                           reinterpret_cast<void**>(&original_hosts), reinterpret_cast<void**>(&original_spawn),
                           reinterpret_cast<void**>(&original_group), reinterpret_cast<void**>(&original_register_spawn)};
    size_t created = 0;
    for (; created < std::size(sites); ++created)
        if (MH_CreateHook(sites[created], detours[created], originals[created]) != MH_OK) break;
    if (created != std::size(sites)) {
        for (size_t i = 0; i < created; ++i) MH_RemoveHook(sites[i]);
        return false;
    }
    for (size_t i = 0; i < created; ++i) {
        if (MH_EnableHook(sites[i]) == MH_OK) continue;
        // Retain reachable trampolines until process exit.
        for (auto site : sites) MH_DisableHook(site);
        return false;
    }
    stopped.store(false, std::memory_order_release);
    return true;
}
void stop() { stopped.store(true, std::memory_order_release); }
#ifdef SC_NATIVE_TESTING
#include <memory>
bool test_movement_inputs() {
    static float distances[4]{}, limits[4]{};
    static bool abi = true;
    const auto previous_movement = original_movement;
    const auto previous_controller = original_controller;
    const auto previous_name = definition_name;
    const auto was_stopped = stopped.exchange(false);
    original_movement = [](uintptr_t) { return 2.0f; };
    definition_name = [](uintptr_t ai) -> const char* {
        if (ai == 1) return "ap_v070_ai_slow__ap_v070_s00050";
        if (ai == 2) return "ap_v070_ai_fast__ap_v070_s00200";
        return "ai_fodder_imp";
    };
    original_controller = [](uintptr_t controller, uintptr_t frame, uintptr_t output, float distance) {
        const auto ai = *reinterpret_cast<const uintptr_t*>(controller);
        abi = abi && frame == 11 && output == 12;
        if (ai == 1) {
            uintptr_t child = 2;
            controller_detour(reinterpret_cast<uintptr_t>(&child), frame, output, 0.25f);
        }
        distances[ai] = distance;
        limits[ai] = movement_detour(ai);
        return distance;
    };
    uintptr_t ai = 1;
    const auto result = controller_detour(reinterpret_cast<uintptr_t>(&ai), 11, 12, 0.5f);
    bool ok = result == 0.25f && distances[1] == 0.25f && limits[1] == 1.0f &&
        distances[2] == 0.5f && limits[2] == 4.0f && movement_detour(ai) == 2.0f;
    ai = 3;
    ok = ok && controller_detour(reinterpret_cast<uintptr_t>(&ai), 11, 12, 0.5f) == 0.5f && limits[3] == 2.0f;
    stopped.store(true);
    ai = 1;
    ok = ok && controller_detour(reinterpret_cast<uintptr_t>(&ai), 11, 12, 0.5f) == 0.5f && limits[1] == 2.0f && abi;
    original_movement = previous_movement; original_controller = previous_controller; definition_name = previous_name;
    stopped.store(was_stopped);
    return ok;
}
bool test_supply_inputs() {
    const auto previous_hosts = original_hosts;
    const auto previous_resolve = resolve_actor;
    const auto previous_grow = grow_hosts;
    const auto previous_name = definition_name;
    const auto previous_world = world_global;
    const auto was_stopped = stopped.exchange(false);
    static uintptr_t initial[1], grown[8];
    static unsigned growths, calls;
    growths = calls = 0;
    original_hosts = [](HostList* out) {
        ++calls; initial[0] = 11;
        out->data = initial; out->count = out->capacity = 1;
    };
    resolve_actor = [](uintptr_t record) { return *reinterpret_cast<const uintptr_t*>(record); };
    grow_hosts = [](HostList* out, int32_t index) {
        if (index != out->count) return;
        ++growths; std::memcpy(grown, out->data, out->count * sizeof(uintptr_t));
        out->data = grown; out->capacity = 8;
    };
    definition_name = [](uintptr_t ai) -> const char* {
        if (ai == 22) return "ap_v070_supply_0123456789abcdef0123_actors_ai__ap_v070_s00100";
        if (ai == 55) return "ap_v070_supply_fedcba9876543210fedc_actors_ai__ap_v070_s00100";
        if (ai == 44) return "ap_v070_supply_0123456789abcdef0123_actors_ai__ap_v070_s00200";
        return "ai_fodder_imp";
    };
    uintptr_t records[5][3]{{22}, {22}, {33}, {44}, {55}};
    struct References { uintptr_t data; int32_t count, capacity; } references{reinterpret_cast<uintptr_t>(records), 5, 5};
    auto world = std::make_unique<uint8_t[]>(0x62090);
    const auto collection = reinterpret_cast<uintptr_t>(&references);
    std::memcpy(world.get() + 0x62088, &collection, sizeof(collection));
    const auto world_address = reinterpret_cast<uintptr_t>(world.get());
    world_global = reinterpret_cast<uintptr_t>(&world_address);
    HostList out{}; out.allocator[3] = 0xa5;
    occupied_hosts_detour(&out);
    bool ok = out.count == 3 && out.data[0] == 11 && out.data[1] == 22 && out.data[2] == 55 &&
              growths == 1 && out.allocator[3] == 0xa5;
    stopped.store(true);
    occupied_hosts_detour(&out);
    ok = ok && out.count == 1 && out.data[0] == 11 && calls == 2;
    stopped.store(false);
    references.count = 0;
    occupied_hosts_detour(&out);
    ok = ok && out.count == 1 && calls == 3;
    original_hosts = previous_hosts; resolve_actor = previous_resolve; grow_hosts = previous_grow;
    definition_name = previous_name; world_global = previous_world; stopped.store(was_stopped);
    return ok;
}


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
        ++calls; abi = abi && manager == 7 && record == 8 && point == 9 && fourth == 10 && fifth == 11;
        return 1;
    };
    resolve_actor = [](uintptr_t record) { return *reinterpret_cast<const uintptr_t*>(record); };
    struct Definition { uintptr_t unused; const char* name; uint8_t padding[0x158 - 16]; Definition* parent; };
    static_assert(offsetof(Definition, parent) == 0x158);
    Definition marauder_type{0, "ai/superheavy/marauder", {}, nullptr};
    Definition archvile_type{0, "ai/superheavy/archvile", {}, nullptr};
    Definition spirit_type{0, "ai/ambient/spirit", {}, nullptr};
    Definition boss_type{0, "ai/boss/darklord", {}, &marauder_type};
    Definition marauder_definition{0, "native_marauder", {}, &marauder_type};
    Definition archvile_definition{0, "native_archvile", {}, &archvile_type};
    Definition spirit_definition{0, "native_spirit", {}, &spirit_type};
    definition_name = [](uintptr_t actor) -> const char* {
        if (actor == 9) return "ap_v070_spawn_a_l00000001000000010000000100000002_km__ap_v070_s00100";
        const auto definition = *reinterpret_cast<Definition**>(actor + 0xa8);
        return definition ? definition->name : "native_ai";
    };
    uintptr_t table[34]{};
    struct Actor { uintptr_t* table; bool dead; uint8_t padding[0xa8 - 9]; Definition* definition; };
    static_assert(offsetof(Actor, definition) == 0xa8);
    Actor marauder{table, false, {}, &marauder_definition}, archvile{table, false, {}, &archvile_definition},
          spirit{table, false, {}, &spirit_definition}, boss{table, false, {}, &boss_type};
    table[0x108 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(+[](uintptr_t actor) { return reinterpret_cast<Actor*>(actor)->dead; });
    uintptr_t references[4][3]{{reinterpret_cast<uintptr_t>(&marauder)}, {reinterpret_cast<uintptr_t>(&archvile)},
                             {reinterpret_cast<uintptr_t>(&spirit)}, {reinterpret_cast<uintptr_t>(&boss)}};
    struct References { uintptr_t data; int32_t count, capacity; uint32_t flags = 0x1050001; }
        collection{reinterpret_cast<uintptr_t>(references), 4, 5};
    uint64_t records[4][0x68 / sizeof(uint64_t)]{};
    for (unsigned index = 0; index < 4; ++index) {
        records[index][0] = uint64_t{1} << 27;
        records[index][1] = references[index][0];
    }
    alignas(uintptr_t) uint8_t manager[0xcd0]{};
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
    bool ok = spawn_detour(7, 8, 9, 10, 11) == 0 && calls == 0;
    marauder.dead = true; archvile.dead = true;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 0 && calls == 0;
    references[0][0] = 0;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 0 && calls == 0;
    references[1][0] = 0;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 1 && calls == 1;
    marauder.dead = false; references[0][0] = reinterpret_cast<uintptr_t>(&marauder);
    collection.data = reinterpret_cast<uintptr_t>(references + 1); collection.count = 3;
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 1 && calls == 2;
    collection.data = reinterpret_cast<uintptr_t>(references); collection.count = 4;
    stopped.store(true);
    ok = ok && spawn_detour(7, 8, 9, 10, 11) == 1 && calls == 3 && abi;
    original_spawn = previous_spawn; resolve_actor = previous_resolve;
    definition_name = previous_name; world_global = previous_world; stopped.store(was_stopped);
    return ok && test_supply_headroom();
}

bool test_tracking_inputs() {
    const auto previous_group = original_group;
    const auto previous_register = original_register_spawn;
    const auto previous_name = definition_name;
    const auto previous_stopped = stopped.load();
    static uint64_t nested[13]{};
    static uint64_t observed, registered;
    static unsigned registrations;
    static bool fail_next, fault, nesting, nested_ok;
    constexpr uint64_t flag = uint64_t{1} << 60;
    definition_name = [](uintptr_t object) -> const char* {
        switch (object) {
        case 8: case 22:
            return "ap_v070_group_a_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100";
        case 18: case 44:
            return "ap_v070_group_b_t00000000000000040000000000000010_l00000001000000010000000100000002_kn__ap_v070_s00100";
        default: return "native_entity";
        }
    };
    original_register_spawn = [](uintptr_t manager, uint64_t* record) {
        nested_ok = nested_ok && (manager == 7 || manager == 17) && record[3] == 0x55;
        registered = record[0];
        ++registrations;
    };
    original_group = [](uintptr_t manager, uint64_t* record, uintptr_t group) -> uint64_t {
        observed = record[0];
        if (fault) RaiseException(0xe0700001, 0, 0, nullptr);
        if (fail_next) { fail_next = false; return 0; }
        if (nesting && group == 8) {
            const auto outer = record[0];
            nested_ok = nested_ok && group_detour(17, nested, 18) == 1 && nested[0] == (flag | 4) &&
                record[0] == outer && registered == (flag | 4);
            observed = outer;
        }
        register_spawn_detour(manager, record);
        return 1;
    };
    uint64_t record[13]{flag | 16, 0, 22, 0x55};
    nested[0] = flag | 4; nested[2] = 44; nested[3] = 0x55;
    registrations = 0; fault = nesting = false; nested_ok = fail_next = true;
    stopped.store(false);
    bool ok = group_detour(7, record, 8) == 0 && observed == (flag | 32) && record[0] == (flag | 16) && registrations == 0;
    nesting = true;
    ok = ok && group_detour(7, record, 8) == 1 && observed == (flag | 32) && registered == (flag | 16) &&
        record[0] == (flag | 16) && record[2] == 22 && record[3] == 0x55 && registrations == 2 && nested_ok;
    nesting = false;
    record[0] = flag | 32; record[2] = 33;
    ok = ok && group_detour(7, record, 9) == 1 && observed == (flag | 32) && registered == (flag | 32);
    record[0] = flag | 16; record[2] = 22;
    stopped.store(true);
    ok = ok && group_detour(7, record, 8) == 1 && observed == (flag | 16) && registered == (flag | 16);
    stopped.store(false); fault = true;
    bool caught = false;
    __try { group_detour(7, record, 8); }
    __except(EXCEPTION_EXECUTE_HANDLER) { caught = GetExceptionCode() == 0xe0700001; }
    ok = ok && caught && record[0] == (flag | 16);
    original_group = previous_group;
    original_register_spawn = previous_register;
    definition_name = previous_name;
    stopped.store(previous_stopped);
    return ok;
}
#endif
}
