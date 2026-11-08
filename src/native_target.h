#pragma once
#include "engine_observer.h"
#include "sentinel_native.h"

namespace sentinel::save { class Installation; }
namespace sentinel::native {
struct Target {
    uintptr_t address = 0;
    std::array<uint8_t, 32> bytes{};
    // Some Steam template factories share their entire prologue. Their separate
    // in-function signature must still be unique and belong to this unwind range.
    uint32_t signature_offset = 0;
    std::array<uint8_t, 32> signature{};
};
inline Target target_from_hex(uintptr_t address, const char* bytes) {
    Target target{};
    target.address = address;
    const auto digit = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t i = 0; i < target.bytes.size(); ++i)
        target.bytes[i] = static_cast<uint8_t>(digit(bytes[i * 2]) * 16 + digit(bytes[i * 2 + 1]));
    return target;
}
struct ValidationDetail {
    engine::ReadResult read{};
    uint32_t read_attempted = 0, window_offset = 0, byte_count = 0, collision_rva = 0;
    std::array<uint8_t, 32> expected{}, actual{};
};
// Startup only. Exact bytes, PE executable section, unwind entry, decodable
// first five bytes and unique signature are independent hook prerequisites.
uint32_t unique_signature(engine::Memory&, const engine::Image&, const std::array<uint8_t, 32>&, HANDLE stop,
                          uint64_t deadline, ValidationDetail* detail = nullptr);
uint32_t validate_target(engine::Memory& memory, const engine::Image& image,
                         const Target& target, HANDLE stop, uint64_t deadline,
                         ValidationDetail* detail = nullptr);
// Exact supported-image leaf without x64 unwind metadata. `length` proves the
// complete instruction stream and terminal return; the first 32 bytes remain
// the unique signature.
uint32_t validate_leaf_target(engine::Memory& memory, const engine::Image& image,
                              const Target& target, size_t length, HANDLE stop,
                              uint64_t deadline, ValidationDetail* detail = nullptr);
uint32_t validate_recorded(save::Installation&, engine::Memory&, const engine::Image&, const Target&,
                            HANDLE stop, uint64_t deadline, uint32_t group, uint32_t index);
bool function_window(engine::Memory&, const engine::Image&, uintptr_t entry, uintptr_t address,
                     size_t length, ValidationDetail* detail = nullptr);
}
