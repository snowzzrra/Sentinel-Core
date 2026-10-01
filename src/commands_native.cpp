#include "commands_native.h"
#include "native_target.h"
#include "save_session.h"
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
constexpr uint32_t command_global = 0x4271ba8, command_vtable = 0x2e453e0;
void dispatch(uintptr_t system, const char* text) {
    const auto level = get_level();
    // Restriction level belongs to this engine thread. Restore it on every exit;
    // only the closed AP operation grammar can enter this scope.
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
    const native::Target command{image_base + 0x179c4a0,
        {0x40,0x53,0xb8,0x40,0x20,0x00,0x00,0xe8,0xf4,0xf8,0x0c,0x01,0x48,0x2b,0xe0,0x48,
         0x8b,0x05,0x22,0x25,0xa1,0x02,0x48,0x33,0xc4,0x48,0x89,0x84,0x24,0x30,0x20,0x00}};
    const native::Target level{image_base + 0x179cc70,
        {0x8b,0x0d,0x22,0xc3,0x55,0x05,0x65,0x48,0x8b,0x04,0x25,0x58,0x00,0x00,0x00,0xba,
         0x18,0x00,0x00,0x00,0x48,0x8b,0x04,0xc8,0x8b,0x04,0x02,0xc3,0xcc,0xcc,0xcc,0xcc}};
    const native::Target setter{image_base + 0x179e240,
        {0x8b,0x0d,0x52,0xad,0x55,0x05,0x65,0x48,0x8b,0x04,0x25,0x58,0x00,0x00,0x00,0x41,
         0xb8,0x18,0x00,0x00,0x00,0x48,0x8b,0x04,0xc8,0x41,0x89,0x14,0x00,0xc3,0xcc,0xcc}};
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
bool available() { return ready.load(std::memory_order_acquire); }
void disable() { ready.store(false); }
bool admitted(const sc_command_request& request) {
    if (process_scoped(request)) return available() &&
        std::memcmp(request.namespace_id, SC_COMMAND_PROCESS_NAMESPACE, 65) == 0;
    const auto* namespace_id = request.namespace_id;
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && save::session().namespace_id() == namespace_id;
}
void execute_native(const sc_command_request& request, sc_command_result& result, uintptr_t player) {
    if (!available()) { result.outcome = SC_COMMAND_UNAVAILABLE; return; }
    if (!player && request.kind <= SC_COMMAND_CRUCIBLE_REFILL) { result.outcome = SC_COMMAND_NO_PLAYER; return; }
    engine::LocalMemory memory;
    uintptr_t system = 0, table = 0;
    if (memory.copy(image_base + command_global, &system, sizeof(system)).reason || !system ||
        memory.copy(system, &table, sizeof(table)).reason || table != image_base + command_vtable) {
        result.outcome = SC_COMMAND_UNAVAILABLE; return;
    }
    result.native_exception = dispatch_safely(system, request.text);
    if (result.native_exception) ready.store(false, std::memory_order_release);
    result.outcome = result.native_exception ? SC_COMMAND_NATIVE_FAILED : SC_COMMAND_DISPATCHED;
}
}
