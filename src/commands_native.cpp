#include "engine/builds/build_descriptor.h"
#include "commands_native.h"
#include "native_target.h"
#include "save_session.h"
#include "special.h"
#include <atomic>
#include <cstring>

namespace sentinel::commands {
namespace {
uintptr_t image_base = 0;
std::atomic<bool> ready{false};
using Execute = void (*)(uintptr_t, const char*);
using Level = uint32_t (*)();
using SetLevel = void (*)(uintptr_t, uint32_t);
Execute execute = nullptr;
Level get_level = nullptr;
SetLevel set_level = nullptr;
#ifdef SC_NATIVE_TESTING
uintptr_t test_system = 0;
#endif
constexpr uint32_t command_global = 0x4271ba8, command_vtable = 0x2e453e0;
void dispatch(uintptr_t system, const char* text) {
    const auto level = get_level();
    // restriction level belongs to this engine thread. restore it on every exit; only the closed ap operation grammar can enter this scope
    __try { set_level(system, 0); execute(system, text); }
    __finally { set_level(system, level); }
}
uint32_t dispatch_safely(uintptr_t system, const char* text) {
    __try { dispatch(system, text); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}
}
uint32_t prepare(const engine::Binding& binding, HANDLE stop) {
    ready.store(false, std::memory_order_release);
    image_base = binding.image.base;
    engine::LocalMemory memory;
    const auto command = builds::resolve(image_base, builds::steam_20260818().commands[0]);
    const auto level = builds::resolve(image_base, builds::steam_20260818().commands[1]);
    const auto setter = builds::resolve(image_base, builds::steam_20260818().commands[2]);
    const auto deadline = GetTickCount64() + 5000;
    auto why = native::validate_target(memory, binding.image, command, stop, deadline);
    if (!why) why = native::validate_leaf_target(memory, binding.image, level, 28, stop, deadline);
    if (!why) why = native::validate_leaf_target(memory, binding.image, setter, 30, stop, deadline);
    if (why) return why;
    execute = reinterpret_cast<Execute>(command.address);
    get_level = reinterpret_cast<Level>(level.address);
    set_level = reinterpret_cast<SetLevel>(setter.address);
    ready.store(true, std::memory_order_release);
    if (const auto local_reason = prepare_local(binding, stop)) { ready.store(false); return local_reason; }
    return SC_NATIVE_NONE;
}
#ifdef SC_NATIVE_TESTING
void test_command_calls(Execute callback, Level level, SetLevel assign, uintptr_t system) {
    execute = callback; get_level = level; set_level = assign; test_system = system;
    ready.store(true, std::memory_order_release);
}
#endif
bool available() { return ready.load(std::memory_order_acquire); }
void disable() { ready.store(false); }
bool admitted(const sc_command_request& request) {
    if (process_scoped(request)) return available() &&
        std::memcmp(request.namespace_id, SC_COMMAND_PROCESS_NAMESPACE, 65) == 0;
    const auto* namespace_id = request.namespace_id;
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && save::session().namespace_id() == namespace_id;
}
static void refill_command(const sc_command_request& request, sc_command_result& result, uintptr_t player) {
    if (!special::available() || !special::calls.refill) {
        result.outcome = SC_COMMAND_UNAVAILABLE;
        return;
    }
    result.native_exception = request.kind == SC_COMMAND_CRUCIBLE_REFILL
                                  ? special::refill_crucible(player)
                                  : special::calls.refill(special::calls.context, player);
    result.outcome = result.native_exception ? SC_COMMAND_NATIVE_FAILED : SC_COMMAND_DISPATCHED;
    return;
}

void execute_native(const sc_command_request& request, sc_command_result& result, uintptr_t player) {
    if (!available()) { result.outcome = SC_COMMAND_UNAVAILABLE; return; }
    if (!player && request.kind <= SC_COMMAND_CRUCIBLE_REFILL) { result.outcome = SC_COMMAND_NO_PLAYER; return; }
    if (request.kind == SC_COMMAND_AMMO_REFILL || request.kind == SC_COMMAND_CRUCIBLE_REFILL)
        return refill_command(request, result, player);
    if (execute_boon(request.text, result, player)) return;
    if (execute_visual_cleanup(request.text, result)) return;
    engine::LocalMemory memory;
    uintptr_t system = 0, table = 0;
#ifdef SC_NATIVE_TESTING
    if (test_system) system = test_system;
    else
#endif
    if (memory.copy(image_base + command_global, &system, sizeof(system)).reason || !system ||
        memory.copy(system, &table, sizeof(table)).reason || table != image_base + command_vtable) {
        result.outcome = SC_COMMAND_UNAVAILABLE; return;
    }
    result.native_exception = dispatch_safely(system, request.text);
    if (result.native_exception) ready.store(false, std::memory_order_release);
    result.outcome = result.native_exception ? SC_COMMAND_NATIVE_FAILED : SC_COMMAND_DISPATCHED;
}
}
