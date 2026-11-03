#include "commands_native.h"
#include "commands.h"
#include "native_target.h"
#include "native_runtime.h"
#include "startup_log.h"
#include "encounter_recovery.h"
#include "expanded_native.h"
#include "save_session.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <string_view>
#include <charconv>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <mutex>

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
std::mutex diagnostic_mutex;
std::deque<std::string> diagnostic_events, diagnostic_spawns;
uint64_t diagnostic_second = 0;
unsigned diagnostic_rate = 0;
uint64_t diagnostic_console_limited = 0;
void print_diagnostic(const char* line) noexcept {
    __try { if (print) print("%s\n", line); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
struct NativeArgs { int count; int padding; const char* argv[512]; };
struct alignas(8) EventArg { char type; char padding[7]; float value[4]; };
static_assert(sizeof(EventArg) == 24);
void recovery_callback(const NativeArgs* args) {
    if (args && args->count == 1) recovery::queue().enqueue_console(generation.load(), GetTickCount64());
}
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
bool player_position(uintptr_t player, float (&position)[3]) {
    engine::LocalMemory memory;
    uintptr_t physics_table = 0;
    // the player's Havok origin moves independently of its entity render transform.
    if (memory.copy(player + 0x8a50, &physics_table, sizeof(physics_table)).reason ||
        physics_table != image.base + 0x2a86e18 ||
        memory.copy(player + 0x8b00, position, sizeof(position)).reason) return false;
    for (const auto value : position) if (!std::isfinite(value)) return false;
    return true;
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
uint32_t hide_visual(const char* name) noexcept {
    __try {
        const auto world = *reinterpret_cast<const uintptr_t*>(image.base + 0x45f7370);
        if (!world) return SC_COMMAND_UNAVAILABLE;
        using Find = uintptr_t (*)(uintptr_t, const char*);
        const auto entity = reinterpret_cast<Find>(image.base + 0x6ca290)(world, name);
        if (!entity) return 0;
        const auto table = *reinterpret_cast<const uintptr_t*>(entity);
        if (table < image.base || table - image.base > UINT32_MAX || !image.contains(
                static_cast<uint32_t>(table - image.base), 0x728, IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE))
            return SC_COMMAND_NATIVE_FAILED;
        const auto hide = *reinterpret_cast<const uintptr_t*>(table + 0x720);
        if (!executable(hide)) return SC_COMMAND_NATIVE_FAILED;
        reinterpret_cast<void (*)(uintptr_t)>(hide)(entity);
        return SC_COMMAND_DISPATCHED;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return SC_COMMAND_NATIVE_FAILED; }
}
}
bool execute_visual_cleanup(const char* text, sc_command_result& result) {
    const auto location = checked_visual_location(text);
    if (!location) return false;
    char name[64]; sprintf_s(name, "ap_location_visual_%u", location);
    // streamed pickup layers can leave the helper's cached target unresolved.
    const auto outcome = hide_visual(name);
    if (!outcome) return false;
    result.outcome = outcome;
    return true;
}
bool execute_boon(const char* text, sc_command_result& result, uintptr_t player) {
    const char* pickup = nullptr;
    if (!std::strcmp(text, "ai_ScriptCmdEnt ap_rpc_v3_7770910 activate")) pickup = "ap_rpc_v3_7770910_pickup";
    else if (!std::strcmp(text, "ai_ScriptCmdEnt ap_rpc_v3_7770911 activate")) pickup = "ap_rpc_v3_7770911_pickup";
    else if (!std::strcmp(text, "ai_ScriptCmdEnt ap_rpc_v3_7770912 activate")) pickup = "ap_rpc_v3_7770912_pickup";
    if (!pickup) return false;
    if (!player) { result.outcome = SC_COMMAND_NO_PLAYER; return true; }
    ManualRequest request{}; request.kind = 4; request.explicit_position = true;
    strcpy_s(request.entity_def, pickup);
    if (!player_position(player, request.position)) {
        result.outcome = SC_COMMAND_NO_PLAYER; return true;
    }
    request.position[2] += 1.0f;
    // prop spawners ignore spawnAt, so use the qualified entity factory and world-position event.
    const auto error = safe_effect(request, player, native::observation_stamp(), native::observation_stamp);
    result.outcome = error ? SC_COMMAND_NATIVE_FAILED : SC_COMMAND_DISPATCHED;
    diagnostic_trace("AP_BOON_SPAWN pickup=%s effect=unverified swimming=unavailable underwater=unavailable outcome=%s x=%g y=%g z=%g detail=%s", pickup,
        error ? "failed" : "created", request.position[0], request.position[1], request.position[2], error ? error : "ok");
    return true;
}
void diagnostic_trace(const char* format, ...) noexcept {
    char line[768];
    va_list args; va_start(args, format);
    vsnprintf(line, sizeof(line), format, args); va_end(args);
    try {
        std::lock_guard<std::mutex> lock(diagnostic_mutex);
        const auto now = GetTickCount64();
        if (diagnostic_second != now / 1000) { diagnostic_second = now / 1000; diagnostic_rate = 0; }
        auto& events = std::strncmp(line, "AP_ENEMY_SPAWN", 14) == 0 ? diagnostic_spawns : diagnostic_events;
        events.push_back("at_ms=" + std::to_string(now) + " " + line);
        if (events.size() > 16) events.pop_front();
        if (diagnostic_rate++ >= 32) { ++diagnostic_console_limited; return; }
        print_diagnostic(events.back().c_str());
    } catch (...) {}
}
std::vector<std::string> diagnostic_history() {
    std::lock_guard<std::mutex> lock(diagnostic_mutex);
    std::vector<std::string> result(diagnostic_events.begin(), diagnostic_events.end());
    result.insert(result.end(), diagnostic_spawns.begin(), diagnostic_spawns.end());
    result.push_back("console_rate_limited=" + std::to_string(diagnostic_console_limited));
    return result;
}
void checkpoint_player_position(uintptr_t player, uint64_t lifecycle) {
    static uint64_t observed = UINT64_MAX;
    if (!player || observed == lifecycle) return;
    observed = lifecycle;
    float xyz[3]{};
    const bool readable = player_position(player, xyz);
    const auto campaign = save::session().campaign_run.snapshot();
    diagnostic_trace("AP_CHECKPOINT stage=player_position generation=%llu checkpoint=%llu source_checkpoint=%llu resumed=%d readable=%d xyz=%g,%g,%g reason=%s",
        static_cast<unsigned long long>(lifecycle), static_cast<unsigned long long>(campaign.checkpoint),
        static_cast<unsigned long long>(campaign.source_checkpoint), campaign.resumed, readable, xyz[0], xyz[1], xyz[2],
        readable ? "first_eligible_player_observation" : "player_position_unreadable");
}
void enemy_spawn_trace(const char* state, const char* origin, const char* body, const char* point, uintptr_t actor,
                       const char* request, const char* parent, uint64_t ordinal) {
    diagnostic_trace("AP_ENEMY_SPAWN state=%.32s origin=%.80s body=%.160s point=%.160s actor=%llx request=%.96s parent=%.64s ordinal=%llu",
        state, origin, body, point, static_cast<unsigned long long>(actor), request, parent, static_cast<unsigned long long>(ordinal));
}
void enemy_worker_trace(const char* state, const char* manager, uintptr_t worker, uint64_t mask, const char* label,
                        const char* request, uint64_t emitted, bool closed) {
    if (print) print("AP_ENEMY_WORKER state=%.32s manager=%.511s worker=%llx mask=%llx label=%.191s request=%.192s emitted=%llu closed=%d\n",
        state, manager, static_cast<unsigned long long>(worker), static_cast<unsigned long long>(mask), label, request,
        static_cast<unsigned long long>(emitted), closed ? 1 : 0);
}
void darklord_trace(const char* line, bool console_allowed) noexcept {
    const auto error = startup_log::darklord(line);
    __try {
        if (console_allowed && print) {
            if (error) print("%s log_io_error=%u\n", line, error);
            else print("%s\n", line);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void enemy_kill_wait_trace(const char* owner, const char* label, uint64_t mask, int32_t target, uint32_t seen, bool complete) {
    if (print) print("AP_ENEMY_KILL_WAIT owner=%s label=%s mask=%llx target=%d seen=%u complete=%d\n",
        owner, label, static_cast<unsigned long long>(mask), target, seen, complete ? 1 : 0);
}
void enemy_quantity_wait_trace(char kind, const char* owner, uint64_t mask, double target, int32_t count, bool complete, bool fault) {
    diagnostic_trace("AP_ENEMY_WAIT kind=%c owner=%.192s mask=%llx target=%g count=%d complete=%d fault=%d",
        kind, owner, static_cast<unsigned long long>(mask), target, count, complete ? 1 : 0, fault ? 1 : 0);
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
        {0x6ca290, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x4c,0x8b,0xf2,0x48,0x8b,0x0d,0xee,0x70}, 0},
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
        remove(system, "noclip"); remove(system, "chrispy"); remove(system, "ap_finish_encounter");
        add(system, "noclip", callback, "Sentinel: noclip [on|off]", 0, 2);
        add(system, "chrispy", callback, "Sentinel: chrispy <loaded entitydef> [x y z]", 0, 2);
        add(system, "ap_finish_encounter", recovery_callback, "Sentinel: finish current encounter", 0, 2);
        registered_system = system;
    } __except (EXCEPTION_EXECUTE_HANDLER) { disable(); }
}
void manual_tick(uintptr_t player, uint64_t epoch, uint64_t (*current)(), bool recovery_pending) {
    if (recovery_pending) {
        const auto outcomes = recovery::queue().tick(current(), GetTickCount64(), expanded::drain_recovery);
        for (const auto outcome : outcomes) {
            if (print) print("%s\n", recovery::message(outcome));
            const auto attempt = recovery::queue().history().back();
            diagnostic_trace("AP_ENCOUNTER_RECOVERY origin=console id=%llu map=%s generation=%llu state=%s producers_stopped=%llu actors_killed=%llu kills_suppressed=%llu elapsed_ms=%llu wait=%s",
                static_cast<unsigned long long>(attempt.id), attempt.context.map.c_str(),
                static_cast<unsigned long long>(attempt.context.scope.lifecycle_generation), recovery::message(outcome),
                static_cast<unsigned long long>(attempt.producers_stopped), static_cast<unsigned long long>(attempt.actors_killed),
                static_cast<unsigned long long>(attempt.kills_suppressed), static_cast<unsigned long long>(attempt.elapsed), attempt.wait.c_str());
            for (const auto& manager : attempt.targets)
                diagnostic_trace("AP_ENCOUNTER_RECOVERY manager=%s kind=%s", manager.name.c_str(), manager.kind.c_str());
        }
    }
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
