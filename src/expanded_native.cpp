#include "expanded_native.h"
#include "expanded.h"
#include "native_target.h"
#include "save_session.h"
#include <MinHook.h>
#include <atomic>

namespace sentinel::expanded {
namespace {
using Movement = float(__fastcall*)(uintptr_t);
using DefinitionName = const char*(__fastcall*)(uintptr_t);
Movement original_movement = nullptr;
DefinitionName definition_name = nullptr;
std::atomic<bool> stopped{false};

float scale_movement(uintptr_t ai, float value) noexcept {
    __try {
        const auto* name = definition_name(ai);
        if (!name) return value;
        char copy[512];
        for (size_t size = 0; size < sizeof(copy); ++size) {
            copy[size] = name[size];
            if (!copy[size]) return movement_speed(value, {copy, size});
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return value;
}
float __fastcall movement_detour(uintptr_t ai) {
    const auto value = original_movement(ai);
    return stopped.load(std::memory_order_acquire) ? value : scale_movement(ai, value);
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
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    if (native::validate_leaf_target(memory, binding.image, movement, 36, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, name, 25, stop_event, deadline)) return false;
    definition_name = reinterpret_cast<DefinitionName>(name.address);
    const auto at = reinterpret_cast<void*>(movement.address);
    if (MH_CreateHook(at, reinterpret_cast<void*>(movement_detour),
                      reinterpret_cast<void**>(&original_movement)) != MH_OK) return false;
    if (MH_EnableHook(at) != MH_OK) {
        MH_RemoveHook(at);
        return false;
    }
    return true;
}
void stop() { stopped.store(true, std::memory_order_release); }
}
