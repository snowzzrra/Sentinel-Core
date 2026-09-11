#pragma once
#include "sentinel_engine.h"
#include <windows.h>
#include <array>
#include <vector>

namespace sentinel::engine {
struct ReadResult { uint32_t reason = SC_REASON_NONE, error = 0; };
/* Internal fixed-field adapter only; never accepts protocol-supplied addresses. */
struct Memory {
    static constexpr size_t maximum_copy = 256 * 1024;
    virtual ~Memory() = default;
    virtual ReadResult copy(uintptr_t address, void* out, size_t size) = 0;
};
struct LocalMemory final : Memory {
    ReadResult copy(uintptr_t address, void* out, size_t size) override;
};
struct Section { uint32_t rva, size, flags; };
struct Image {
    uintptr_t base = 0;
    uint32_t size = 0, timestamp = 0, entry = 0, machine = 0;
    std::vector<Section> sections;
    bool contains(uint32_t rva, size_t width, uint32_t required, uint32_t forbidden) const;
};
bool add(uintptr_t address, size_t offset, size_t width, uintptr_t& result);
ReadResult read_image(Memory& memory, uintptr_t base, Image& image);
struct Binding {
    sc_engine_snapshot metadata{};
    Image image;
    uintptr_t root = 0;
    std::array<uintptr_t, 3> globals{};
    std::array<uint32_t, 3> global_reasons{};
};
/* The signature is applicable to unknown PE identities, but it never authorizes
   the known profile's child offsets or global RVAs on those identities. */
Binding bind(Memory& memory, Image image, const char* disk_hash,
             uint32_t hash_reason, HANDLE stop, uint64_t deadline);
Binding bind_host(Memory& memory, HANDLE stop);
sc_engine_snapshot sample(Memory& memory, const Binding& binding, uint64_t sequence);
sc_engine_snapshot unavailable(uint32_t reason);
sc_engine_snapshot freshness(sc_engine_snapshot snapshot, uint64_t now);
}
