#include "commands_native.h"
#include "commands.h"
#include "native_target.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace sentinel::commands {
namespace {
engine::Image image{};
SRWLOCK manual_lock = SRWLOCK_INIT;
ManualRequest pending{};
uint64_t pending_generation = 0;
std::atomic<uint64_t> generation{0};
uintptr_t registered_system = 0;
using Print = void (*)(const char*, ...);
Print print = nullptr;
struct NativeArgs { int count; int padding; const char* argv[512]; };
struct alignas(8) EventArg { char type; char padding[7]; float value[4]; };
static_assert(sizeof(EventArg) == 24);
void callback(const NativeArgs* args) {
    if (!available()) { print("Sentinel console: provider unavailable.\n"); return; }
    ManualRequest request{};
    if (!args || !parse_manual(args->count, args->argv, request)) {
        print("Usage: noclip [on|off]; chrispy <entitydef> [x y z] (three finite numbers).\n"); return;
    }
    if (!TryAcquireSRWLockExclusive(&manual_lock)) { print("Sentinel console: busy.\n"); return; }
    if (pending.kind) print("Sentinel console: previous command pending.\n");
    else { pending = request; pending_generation = generation.load(); }
    ReleaseSRWLockExclusive(&manual_lock);
}
bool executable(uintptr_t address) {
    return address >= image.base && address - image.base <= UINT32_MAX && image.contains(static_cast<uint32_t>(address - image.base), 1,
        IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE);
}
const char* effect(const ManualRequest& r, uintptr_t player, uint64_t expected, uint64_t (*current)()) {
    if (r.kind <= 3) {
        const uint32_t rva = r.kind == 1 ? 0x60b410u : r.kind == 2 ? 0x60b4f0u : 0x60b560u;
        reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(image.base + rva)(0, player); return nullptr;
    }
    using Type = uintptr_t (*)();
    using Lookup = uintptr_t (*)(uintptr_t, const char*, uint32_t);
    const auto def = reinterpret_cast<Lookup>(image.base + 0x17aa5d0)(
        reinterpret_cast<Type>(image.base + 0x17a3380)(), r.entity_def, 1);
    if (!def) return "entity definition absent or not loaded";
    EventArg position{}; position.type = 'v';
    if (r.explicit_position) memcpy(position.value, r.position, sizeof(r.position));
    else memcpy(position.value, reinterpret_cast<const void*>(player + 0x167f8 + 0x1f8 + 0xc), 12);
    for (size_t i = 0; i < 3; ++i) if (!std::isfinite(position.value[i])) return "invalid aim trace position";
    const auto event = image.base + 0x6c1e0b0;
    if (*reinterpret_cast<const uintptr_t*>(event) != image.base + 0x3009868)
        return "setWorldOrigin event unavailable";
    const auto world = *reinterpret_cast<const uintptr_t*>(image.base + 0x45f7370);
    if (!world) return "game context unavailable";
    const auto table = *reinterpret_cast<const uintptr_t*>(world);
    if (table < image.base || table - image.base > UINT32_MAX || !image.contains(static_cast<uint32_t>(table - image.base), 0x48,
        IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE)) return "unqualified game context vtable";
    const auto factory = *reinterpret_cast<const uintptr_t*>(table + 0x40);
    if (!executable(factory)) return "unqualified entity factory";
    if (current() != expected) return "map generation changed";
    using Spawn = uintptr_t (*)(uintptr_t, uintptr_t, uint32_t, uint32_t, int32_t);
    const auto entity = reinterpret_cast<Spawn>(factory)(world, def, UINT32_MAX, 0x1fffffe, -1);
    if (!entity) return "native entity creation failed";
    if (current() != expected) return "entity created; position refused after map transition";
    EventArg result{};
    using Event = uintptr_t (*)(uintptr_t, EventArg*, uintptr_t, EventArg*);
    reinterpret_cast<Event>(image.base + 0x17deb40)(entity, &result, event, &position);
    return nullptr;
}
const char* safe_effect(const ManualRequest& r, uintptr_t player, uint64_t expected, uint64_t (*current)()) {
    __try { return effect(r, player, expected, current); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return "native fault; effect unknown, automatic retry disabled"; }
}
}
uint32_t prepare_local(const engine::Binding& binding, HANDLE stop) {
    image = binding.image; engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 5000;
    struct Entry { uint32_t rva; std::array<uint8_t, 32> bytes; size_t leaf; };
    const Entry entries[] = {
        {0x179baf0, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20,0x48,0x8b}, 0},
        {0x179cbc0, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xea}, 0},
        {0x363590, {0x48,0x89,0x4c,0x24,0x08,0x48,0x89,0x54,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x4c,0x89,0x4c,0x24,0x20,0x48,0x83,0xec,0x28,0xba,0x01,0x00,0x00,0x00,0x4c,0x8d,0x4c}, 0},
        {0x60b410, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x48,0x8b,0xca,0x48,0x8b,0xda,0xff,0x90,0xb0}, 0},
        {0x60b4f0, {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x48,0x8b,0xca,0x48,0x8b,0xda,0xff,0x90,0xb0,0x07,0x00,0x00,0x48,0x8b,0xc8,0xe8,0xdf,0xae,0xbb}, 0},
        {0x60b560, {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x48,0x8b,0xca,0x48,0x8b,0xda,0xff,0x90,0xb0,0x07,0x00,0x00,0x48,0x8b,0xc8,0xe8,0x6f,0xae,0xbb}, 0},
        {0x17a3380, {0x48,0x8d,0x05,0x69,0x01,0xf1,0x02,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x57,0x41,0x56,0x48,0x81}, 8},
        {0x17aa5d0, {0x40,0x55,0x56,0x57,0x41,0x57,0x48,0x8d,0xac,0x24,0x48,0xfe,0xff,0xff,0x48,0x81,0xec,0xb8,0x02,0x00,0x00,0x48,0x8b,0x05,0xec,0x43,0xa0,0x02,0x48,0x33,0xc4,0x48}, 0},
        {0x17deb40, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x49,0x8b,0xe9,0x49,0x8b,0xd8,0x48,0x8b,0xfa}, 0},
        {0x9907b0, {0x48,0x89,0x4c,0x24,0x08,0x56,0x48,0x81,0xec,0xe0,0x00,0x00,0x00,0x48,0x8b,0x05,0x9c,0x37,0xc8,0x03,0x48,0x8b,0xf1,0x83,0x78,0x08,0x00,0x0f,0x84,0x19,0x05,0x00}, 0},
    };
    for (const auto& e : entries) {
        native::Target target{}; target.address = image.base + e.rva; target.bytes = e.bytes;
        if (e.rva == 0x179baf0) {
            target.signature_offset = 32;
            target.signature = {0x41,0x10,0x4d,0x8b,0xe1,0x4d,0x8b,0xf8,0x4c,0x8b,0xf2,0x48,0x8b,0xe9,0x48,0x63,0x70,0x08,0x48,0x85,0xf6,0x7e,0x2f,0x33,0xdb,0x0f,0x1f,0x80,0x00,0x00,0x00,0x00};
        } else if (e.rva == 0x179cbc0) {
            target.signature_offset = 32;
            target.signature = {0xff,0x50,0x68,0x4c,0x8b,0xf0,0x48,0x63,0x70,0x08,0x48,0x85,0xf6,0x7e,0x20,0x33,0xdb,0x49,0x8b,0x0e,0x48,0x8b,0x3c,0xd9,0x48,0x8b,0xcd,0x48,0x8b,0x17,0xe8,0x7d};
        }
        if (e.rva == 0x9907b0) {
            target.signature_offset = 0x404;
            target.signature = {0x48,0x8b,0x0d,0xb5,0x67,0xc6,0x03,0x41,0xb9,0xfe,0xff,0xff,0x01,0x41,0xb8,0xff,0xff,0xff,0xff,0xc7,0x44,0x24,0x20,0xff,0xff,0xff,0xff,0x48,0x8b,0x01,0xff,0x50};
        }
        const auto why = e.leaf ? native::validate_leaf_target(memory, image, target, e.leaf, stop, deadline)
            : native::validate_target(memory, image, target, stop, deadline);
        if (why) return why;
    }
    print = reinterpret_cast<Print>(image.base + 0x363590); return SC_NATIVE_NONE;
}
void console_tick(uint64_t epoch) {
    generation.store(epoch);
    if (!available()) return;
    __try {
        const auto system = *reinterpret_cast<const uintptr_t*>(image.base + 0x4271ba8);
        if (!system || *reinterpret_cast<const uintptr_t*>(system) != image.base + 0x2e453e0 ||
            registered_system == system) return;
        using Remove = void (*)(uintptr_t, const char*);
        using Add = void (*)(uintptr_t, const char*, void (*)(const NativeArgs*), const char*, uintptr_t, int);
        const auto remove = reinterpret_cast<Remove>(image.base + 0x179cbc0);
        const auto add = reinterpret_cast<Add>(image.base + 0x179baf0);
        remove(system, "noclip"); remove(system, "chrispy");
        add(system, "noclip", callback, "Sentinel: noclip [on|off]", 0, 2);
        add(system, "chrispy", callback, "Sentinel: chrispy <loaded entitydef> [x y z]", 0, 2);
        registered_system = system;
    } __except (EXCEPTION_EXECUTE_HANDLER) { disable(); }
}
void manual_tick(uintptr_t player, uint64_t epoch, uint64_t (*current)()) {
    if (!TryAcquireSRWLockExclusive(&manual_lock)) return;
    const auto r = pending; const auto expected = pending_generation;
    pending = {}; ReleaseSRWLockExclusive(&manual_lock);
    if (!r.kind) return;
    const char* error = !player || epoch != expected || !available()
        ? "player absent, loading, menu, or stale map context" : safe_effect(r, player, epoch, current);
    if (error) print("Sentinel console: %s.\n", error);
    else print("Sentinel console: native command returned.\n");
}
}
