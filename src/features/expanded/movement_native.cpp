// Movement speed, haste and controller hooks.
#include "expanded_internal.h"

namespace sentinel::expanded::detail {
float scale_movement(uintptr_t ai, float value) noexcept;
}

namespace sentinel::expanded::detail {

std::atomic<uint64_t> motion_queries{0},motion_lookup_us{0},motion_controllers{0},motion_native_us{0};

Movement original_movement = nullptr;

Movement original_haste = nullptr;

Controller original_controller = nullptr;

DefinitionName definition_name = nullptr;

thread_local uintptr_t active_ai = 0;

thread_local float active_speed = 1.0f;

float scale_movement(uintptr_t ai, float value) noexcept {
    const auto started=CostClock::now();
    char name[512];
    const auto scaled=read_recipe_definition(ai, name) || read_definition_name(ai, name) ? movement_speed(value, name) : value;
    motion_queries.fetch_add(1,std::memory_order_relaxed);
    motion_lookup_us.fetch_add(elapsed_us(started),std::memory_order_relaxed);
    return scaled;
}

float __fastcall movement_detour(uintptr_t ai) {
    const auto value = original_movement(ai);
    return !stopped.load(std::memory_order_acquire) && active_ai == ai ? value * active_speed : value;
}

float __fastcall haste_detour(uintptr_t ai) {
    const auto value = original_haste(ai);
    return stopped.load(std::memory_order_acquire) ? value : scale_movement(ai, value);
}

float __fastcall controller_detour(uintptr_t controller, uintptr_t frame, uintptr_t output, float distance) {
    if (stopped.load(std::memory_order_acquire)) return original_controller(controller, frame, output, distance);
    const auto previous_ai = active_ai;
    const auto previous_speed = active_speed;
    active_ai = *reinterpret_cast<const uintptr_t*>(controller);
    active_speed = scale_movement(active_ai, 1.0f);
    // haste scales animation displacement; only the controller limits need the same factor.
    const auto started=CostClock::now();
    const auto result = original_controller(controller, frame, output, distance);
    motion_controllers.fetch_add(1,std::memory_order_relaxed);
    motion_native_us.fetch_add(elapsed_us(started),std::memory_order_relaxed);
    active_ai = previous_ai;
    active_speed = previous_speed;
    return result;
}

float __fastcall movement_detour_entry(uintptr_t ai) {
    const auto lease = expanded_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_movement)>(lease.original)(ai);
        return movement_detour(ai);
    } __finally { expanded_hooks.leave(lease); }
}

float __fastcall haste_detour_entry(uintptr_t ai) {
    const auto lease = expanded_hooks.enter(1);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_haste)>(lease.original)(ai);
        return haste_detour(ai);
    } __finally { expanded_hooks.leave(lease); }
}

float __fastcall controller_detour_entry(uintptr_t controller, uintptr_t frame, uintptr_t output, float distance) {
    const auto lease = expanded_hooks.enter(2);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_controller)>(lease.original)(controller, frame, output, distance);
        return controller_detour(controller, frame, output, distance);
    } __finally { expanded_hooks.leave(lease); }
}

}
