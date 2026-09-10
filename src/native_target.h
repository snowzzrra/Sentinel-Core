#pragma once
#include "engine_observer.h"
#include "sentinel_native.h"

namespace sentinel::native {
struct Target {
    uintptr_t address = 0;
    std::array<uint8_t, 32> bytes{};
};
// Startup only. Exact bytes, PE executable section, unwind entry, decodable
// first five bytes and unique signature are independent hook prerequisites.
uint32_t validate_target(engine::Memory& memory, const engine::Image& image,
                         const Target& target, HANDLE stop, uint64_t deadline);
Target profile_target(uintptr_t base, unsigned index);
}
