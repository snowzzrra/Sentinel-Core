#include "native_target.h"
#include "hde/hde64.h"
#include <algorithm>
#include <cstring>

namespace sentinel::native {
Target profile_target(uintptr_t base, unsigned index) {
    constexpr uint32_t rvas[] = {0x43d1f0, 0x666aa0, 0x668400};
    constexpr const char* bytes[] = {
        "405556415441554156488dac24c0c2ffffb8403e0000e895eb4202482be0488b",
        "48895c24205556574154415541564157488dac24a058fbffb860a80400e8de52",
        "48895c2408574883ec20488bf9488bda488b0a4885c9747f488b01ff5058f640"};
    Target out{}; out.address = base + rvas[index];
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    // Exact 32-byte prefixes retained from the supported Ghidra image.
    for (size_t n = 0; n < std::strlen(bytes[index]) / 2; ++n)
        out.bytes[n] = static_cast<uint8_t>(digit(bytes[index][n * 2]) * 16 + digit(bytes[index][n * 2 + 1]));
    return out;
}
uint32_t validate_target(engine::Memory& memory, const engine::Image& image,
                         const Target& target, HANDLE stop, uint64_t deadline) {
    constexpr size_t signature_size = 32;
    if (target.address < image.base || target.address - image.base > UINT32_MAX ||
        !image.contains(static_cast<uint32_t>(target.address - image.base), 32,
                        IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE))
        return SC_NATIVE_TARGET_BOUNDARY;
    std::array<uint8_t, 32> actual{};
    if (memory.copy(target.address, actual.data(), actual.size()).reason != SC_REASON_NONE ||
        std::memcmp(actual.data(), target.bytes.data(), signature_size)) return SC_NATIVE_TARGET_BYTES;
    DWORD64 base = 0;
    const auto unwind = RtlLookupFunctionEntry(target.address, &base, nullptr);
    if (!unwind || base != image.base || base + unwind->BeginAddress != target.address)
        return SC_NATIVE_TARGET_BOUNDARY;
    size_t length = 0;
    while (length < 5) {
        hde64s instruction{};
        const auto size = hde64_disasm(actual.data() + length, &instruction);
        if (!size || instruction.flags & F_ERROR) return SC_NATIVE_TARGET_BOUNDARY;
        length += size;
    }
    std::array<uint8_t, 65536> chunk{};
    unsigned matches = 0;
    for (const auto& section : image.sections) {
        if (!(section.flags & IMAGE_SCN_MEM_EXECUTE)) continue;
        for (size_t offset = 0; offset + signature_size <= section.size;) {
            if (GetTickCount64() >= deadline || (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT))
                return SC_NATIVE_BUDGET;
            const auto count = std::min(chunk.size(), static_cast<size_t>(section.size) - offset);
            if (memory.copy(image.base + section.rva + offset, chunk.data(), count).reason != SC_REASON_NONE)
                return SC_NATIVE_READ_FAILED;
            auto cursor = chunk.begin(); const auto end = chunk.begin() + count;
            while ((cursor = std::search(cursor, end, target.bytes.begin(), target.bytes.begin() + signature_size)) != end) {
                if (++matches > 1) return SC_NATIVE_TARGET_NOT_UNIQUE;
                ++cursor;
            }
            if (count <= signature_size) break;
            offset += count - signature_size + 1;
        }
    }
    return matches == 1 ? SC_NATIVE_NONE : SC_NATIVE_TARGET_NOT_UNIQUE;
}
}
