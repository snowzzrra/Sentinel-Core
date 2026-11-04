#include "engine/builds/build_descriptor.h"
#ifdef SC_NATIVE_TESTING
#undef validate_target
#undef validate_leaf_target
#undef validate_recorded
#endif
#include "native_target.h"
#include "save_installation.h"
#include "hde/hde64.h"
#include <algorithm>
#include <cstring>

namespace sentinel::native {
Target profile_target(uintptr_t base, unsigned index) {
    return builds::steam_20260818().profile_target(base, index);
}
Target save_target(uintptr_t base, unsigned index) {
    return builds::steam_20260818().save_target(base, index);
}
namespace {
// Windows x64 may split a function into chained RUNTIME_FUNCTION fragments.
// A secondary window must belong to the exact entry's unwind owner throughout;
// adjacent executable bytes alone never establish ownership.
bool same_unwind_owner(engine::Memory& memory, const engine::Image& image,
                       RUNTIME_FUNCTION part, const RUNTIME_FUNCTION& entry,
                       ValidationDetail* detail) {
    for (unsigned depth = 0; depth < 8; ++depth) {
        if (part.BeginAddress == entry.BeginAddress && part.EndAddress == entry.EndAddress &&
            part.UnwindData == entry.UnwindData) return true;
        if (part.EndAddress <= part.BeginAddress ||
            !image.contains(part.BeginAddress, part.EndAddress - part.BeginAddress,
                IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE) ||
            !image.contains(part.UnwindData, 4, IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE)) return false;
        uint8_t header[4]{};
        auto read = memory.copy(image.base + part.UnwindData, header, sizeof(header));
        if (detail) { detail->read_attempted = 1; detail->read = read; }
        if (read.reason || (header[0] & 7) != 1 || (header[0] >> 3) != UNW_FLAG_CHAININFO) return false;
        const uint64_t chained = uint64_t{part.UnwindData} + 4 + ((header[2] + 1u) & ~1u) * 2;
        if (chained + sizeof(part) > image.size || !image.contains(static_cast<uint32_t>(chained), sizeof(part),
                IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE)) return false;
        read = memory.copy(image.base + static_cast<uintptr_t>(chained), &part, sizeof(part));
        if (detail) { detail->read_attempted = 1; detail->read = read; }
        if (read.reason) return false;
    }
    return false;
}
bool owned_window(engine::Memory& memory, const engine::Image& image, uintptr_t address,
                  size_t length, const RUNTIME_FUNCTION& entry, ValidationDetail* detail) {
    const uintptr_t end = address + length;
    while (address < end) {
        DWORD64 base = 0;
        const auto part = RtlLookupFunctionEntry(address, &base, nullptr);
        if (!part || base != image.base || part->EndAddress <= address - base ||
            !same_unwind_owner(memory, image, *part, entry, detail)) return false;
        address = std::min(end, static_cast<uintptr_t>(base + part->EndAddress));
    }
    return true;
}
uint32_t unique_signature(engine::Memory& memory, const engine::Image& image,
                          const std::array<uint8_t, 32>& signature, HANDLE stop,
                          uint64_t deadline, ValidationDetail* detail) {
    std::array<uint8_t, 65536> chunk{};
    unsigned matches = 0;
    for (const auto& section : image.sections) {
        if (!(section.flags & IMAGE_SCN_MEM_EXECUTE)) continue;
        for (size_t offset = 0; offset + signature.size() <= section.size;) {
            if (GetTickCount64() >= deadline || (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT))
                return SC_NATIVE_BUDGET;
            const auto count = std::min(chunk.size(), static_cast<size_t>(section.size) - offset);
            const auto read = memory.copy(image.base + section.rva + offset, chunk.data(), count);
            if (detail) { detail->read_attempted = 1; detail->read = read; }
            if (read.reason != SC_REASON_NONE) return SC_NATIVE_READ_FAILED;
            auto cursor = chunk.begin(); const auto end = chunk.begin() + count;
            while ((cursor = std::search(cursor, end, signature.begin(), signature.end())) != end) {
                if (++matches > 1) {
                    if (detail) detail->collision_rva = section.rva + static_cast<uint32_t>(offset + (cursor - chunk.begin()));
                    return SC_NATIVE_TARGET_NOT_UNIQUE;
                }
                ++cursor;
            }
            if (count <= signature.size()) break;
            offset += count - signature.size() + 1;
        }
    }
    return matches == 1 ? SC_NATIVE_NONE : SC_NATIVE_TARGET_NOT_UNIQUE;
}
}
bool function_window(engine::Memory& memory, const engine::Image& image, uintptr_t entry_address,
                     uintptr_t address, size_t length, ValidationDetail* detail) {
    if (address < image.base || address - image.base > UINT32_MAX ||
        !image.contains(static_cast<uint32_t>(address - image.base), length,
            IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE)) return false;
    DWORD64 base = 0;
    const auto entry = RtlLookupFunctionEntry(entry_address, &base, nullptr);
    return entry && base == image.base && base + entry->BeginAddress == entry_address &&
        owned_window(memory, image, address, length, *entry, detail);
}
uint32_t validate_target(engine::Memory& memory, const engine::Image& image,
                         const Target& target, HANDLE stop, uint64_t deadline, ValidationDetail* detail) {
    if (detail) *detail = {};
    constexpr size_t signature_size = 32;
    if (target.address < image.base || target.address - image.base > UINT32_MAX ||
        !image.contains(static_cast<uint32_t>(target.address - image.base), 32,
                        IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE))
        return SC_NATIVE_TARGET_BOUNDARY;
    std::array<uint8_t, 32> actual{};
    auto read = memory.copy(target.address, actual.data(), actual.size());
    if (detail) { detail->read_attempted = 1; detail->read = read; detail->byte_count = 32;
        detail->expected = target.bytes; detail->actual = actual; }
    if (read.reason != SC_REASON_NONE ||
        std::memcmp(actual.data(), target.bytes.data(), signature_size)) return SC_NATIVE_TARGET_BYTES;
    DWORD64 base = 0;
    const auto unwind = RtlLookupFunctionEntry(target.address, &base, nullptr);
    if (!unwind || base != image.base || base + unwind->BeginAddress != target.address)
        return SC_NATIVE_TARGET_BOUNDARY;
    const auto& signature = target.signature_offset ? target.signature : target.bytes;
    if (target.signature_offset) {
        const auto rva = static_cast<uint32_t>(target.address - image.base);
        if (uint64_t{rva} + target.signature_offset > UINT32_MAX ||
            !image.contains(rva + target.signature_offset, signature_size,
                IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE) ||
            !owned_window(memory, image, target.address + target.signature_offset, signature_size, *unwind, detail))
            return SC_NATIVE_TARGET_BOUNDARY;
        std::array<uint8_t, 32> actual_signature{};
        read = memory.copy(target.address + target.signature_offset, actual_signature.data(), actual_signature.size());
        if (detail) { detail->window_offset = target.signature_offset; detail->read_attempted = 1; detail->read = read;
            detail->expected = signature; detail->actual = actual_signature; }
        if (read.reason ||
            actual_signature != signature) return SC_NATIVE_TARGET_BYTES;
    }
    size_t length = 0;
    while (length < 5) {
        hde64s instruction{};
        const auto size = hde64_disasm(actual.data() + length, &instruction);
        if (!size || instruction.flags & F_ERROR) return SC_NATIVE_TARGET_BOUNDARY;
        length += size;
    }
    return unique_signature(memory, image, signature, stop, deadline, detail);
}
uint32_t validate_leaf_target(engine::Memory& memory, const engine::Image& image,
                              const Target& target, size_t length, HANDLE stop,
                              uint64_t deadline, ValidationDetail* detail) {
    if (detail) *detail = {};
    if (!length || length > 1024 || target.address < image.base ||
        target.address - image.base > UINT32_MAX ||
        !image.contains(static_cast<uint32_t>(target.address - image.base), std::max(length, target.bytes.size()),
                        IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE))
        return SC_NATIVE_TARGET_BOUNDARY;
    std::array<uint8_t, 1024> actual{};
    const auto read = memory.copy(target.address, actual.data(), std::max(length, target.bytes.size()));
    if (detail) {
        detail->read_attempted = 1; detail->read = read; detail->byte_count = static_cast<uint32_t>(length);
        detail->expected = target.bytes; std::copy_n(actual.begin(), target.bytes.size(), detail->actual.begin());
    }
    if (read.reason) return SC_NATIVE_READ_FAILED;
    if (std::memcmp(actual.data(), target.bytes.data(), target.bytes.size())) return SC_NATIVE_TARGET_BYTES;
    DWORD64 base = 0;
    if (RtlLookupFunctionEntry(target.address, &base, nullptr)) return SC_NATIVE_TARGET_BOUNDARY;
    size_t offset = 0, last = 0;
    while (offset < length) {
        last = offset;
        hde64s instruction{};
        const auto size = hde64_disasm(actual.data() + offset, &instruction);
        if (!size || instruction.flags & F_ERROR || offset + size > length)
            return SC_NATIVE_TARGET_BOUNDARY;
        offset += size;
    }
    if (offset != length || actual[last] != 0xc3) return SC_NATIVE_TARGET_BOUNDARY;
    return unique_signature(memory, image, target.bytes, stop, deadline, detail);
}
uint32_t validate_recorded(save::Installation& record, engine::Memory& memory, const engine::Image& image,
                            const Target& target, HANDLE stop, uint64_t deadline, uint32_t group, uint32_t index) {
    auto event = record.begin(group == 1 ? SC_INSTALL_NATIVE_TARGET : SC_INSTALL_SAVE_TARGET,
        group, index, static_cast<uint32_t>(target.address - image.base), target.signature_offset);
    ValidationDetail detail;
    const auto reason = validate_target(memory, image, target, stop, deadline, &detail);
    if (detail.read_attempted) save::attach_read(event, detail.read);
    event.byte_count = detail.byte_count; event.collision_rva = detail.collision_rva; event.byte_window_offset = detail.window_offset;
    std::memcpy(event.expected_bytes, detail.expected.data(), 32);
    std::memcpy(event.actual_bytes, detail.actual.data(), 32);
    record.finish(event, reason);
    return reason;
}
}
