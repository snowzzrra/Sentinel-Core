// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Locator/layout facts adapted from DoomEternal-AP-Mod native/client/game_state_probe.cpp
// at 760b7d98e22a6aa67db36dd0533c794e9dd14032 (MIT, same copyright).
// New local checked reader; no external-process discovery or RPC policy is reused.
#include "engine_observer.h"
#include "pipe_io.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <utility>

namespace sentinel::engine {
namespace {
constexpr size_t chunk_size = 256 * 1024;
constexpr uintptr_t highest_address = 0x00007FFFFFFFFFFFULL;
constexpr uint32_t readable = IMAGE_SCN_MEM_READ, writable = IMAGE_SCN_MEM_WRITE;
constexpr uint32_t executable = IMAGE_SCN_MEM_EXECUTE;
constexpr char known_hash[] = "9809708c823f8db4304201ab0006ff2395d0d484ae9c846d98a56ce8c87c1247";
constexpr uint8_t signature[] = {0x48,0x8d,0x0d,0,0,0,0,0xe8,0,0,0,0,0x84,0xc0,0x48,0x8d,0x0d,0,0,0,0,0x49,0x8b,0xd4};
constexpr char mask[] = "xxx????x????xxxxx????xxx";
uint32_t interrupted(HANDLE stop, uint64_t deadline) {
    if (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) return SC_REASON_CANCELLED;
    return GetTickCount64() >= deadline ? SC_REASON_BUDGET : SC_REASON_NONE;
}
template<class T> ReadResult read(Memory& memory, uintptr_t address, T& value) {
    value = {};
    const auto result = memory.copy(address, &value, sizeof(value));
    if (result.reason != SC_REASON_NONE) value = {};
    return result;
}
sc_engine_field unknown(uint32_t reason, uint32_t error = 0) {
    return {SC_OBSERVATION_UNKNOWN, reason, 0, error};
}
sc_engine_field observed(int32_t value, bool provisional = false) {
    return {static_cast<uint32_t>(provisional ? SC_OBSERVATION_PROVISIONAL : SC_OBSERVATION_OBSERVED), SC_REASON_NONE, value, 0};
}
bool pointer(uintptr_t value) { return value >= 0x10000 && value <= highest_address && value % 8 == 0; }
bool image_pointer(const Image& image, uintptr_t address, size_t width, uint32_t required, uint32_t forbidden) {
    return address >= image.base && address - image.base <= UINT32_MAX &&
        image.contains(static_cast<uint32_t>(address - image.base), width, required, forbidden);
}
uint32_t disk_hash(const wchar_t* path, char (&out)[65], HANDLE stop, uint64_t deadline) {
    Handle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file) return SC_REASON_READ_FAILED;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > 256 * 1024 * 1024)
        return SC_REASON_OUT_OF_RANGE;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return SC_REASON_INTERNAL_ERROR;
    uint32_t reason = SC_REASON_NONE;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) reason = SC_REASON_INTERNAL_ERROR;
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) reason = SC_REASON_INTERNAL_ERROR;
    std::array<uint8_t, chunk_size> buffer{};
    for (uint64_t offset = 0; reason == SC_REASON_NONE && offset < static_cast<uint64_t>(size.QuadPart);) {
        reason = interrupted(stop, deadline);
        if (reason != SC_REASON_NONE) break;
        const DWORD width = static_cast<DWORD>(std::min<uint64_t>(buffer.size(), size.QuadPart - offset));
        ResetEvent(event.value);
        OVERLAPPED ov{}; ov.hEvent = event.value;
        ov.Offset = static_cast<DWORD>(offset); ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD count = 0;
        const BOOL ok = ReadFile(file.value, buffer.data(), width, &count, &ov);
        const DWORD error = finish_io(file.value, ov, ok, ok ? ERROR_SUCCESS : GetLastError(),
                                     stop, remaining(deadline), count);
        if (error != ERROR_SUCCESS) reason = error == ERROR_OPERATION_ABORTED ? SC_REASON_CANCELLED :
            (error == ERROR_TIMEOUT ? SC_REASON_BUDGET : SC_REASON_READ_FAILED);
        else if (count != width) reason = SC_REASON_PARTIAL_READ;
        else if (BCryptHashData(hash, buffer.data(), count, 0) < 0) reason = SC_REASON_INTERNAL_ERROR;
        offset += count;
    }
    uint8_t digest[32]{};
    if (reason == SC_REASON_NONE && BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
        reason = SC_REASON_INTERNAL_ERROR;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (reason == SC_REASON_NONE) {
        for (size_t i = 0; i < sizeof(digest); ++i) {
            out[i * 2] = "0123456789abcdef"[digest[i] >> 4];
            out[i * 2 + 1] = "0123456789abcdef"[digest[i] & 15];
        }
        out[64] = 0;
    }
    return reason;
}
}

bool add(uintptr_t address, size_t offset, size_t width, uintptr_t& result) {
    if (!width || address < 0x10000 || address > highest_address || offset > highest_address - address)
        return false;
    result = address + offset;
    return width - 1 <= highest_address - result;
}
ReadResult LocalMemory::copy(uintptr_t address, void* out, size_t size) {
    uintptr_t checked = 0;
    if (!out || size > chunk_size || !add(address, 0, size, checked)) return {SC_REASON_OUT_OF_RANGE, 0};
    const uintptr_t end = address + size;
    for (auto cursor = address; cursor < end;) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) != sizeof(region))
            return {SC_REASON_READ_FAILED, GetLastError()};
        const DWORD p = region.Protect & 0xff;
        const bool can_read = p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
            p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
        if (region.State != MEM_COMMIT || !can_read || (region.Protect & PAGE_GUARD))
            return {SC_REASON_READ_FAILED, ERROR_NOACCESS};
        const auto start = reinterpret_cast<uintptr_t>(region.BaseAddress);
        if (region.RegionSize > UINTPTR_MAX - start || start + region.RegionSize <= cursor)
            return {SC_REASON_OUT_OF_RANGE, 0};
        cursor = std::min(end, start + region.RegionSize);
    }
    SIZE_T count = 0;
    SetLastError(ERROR_SUCCESS);
    const BOOL ok = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &count);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok || count != size) {
        std::memset(out, 0, size);
        return {static_cast<uint32_t>(count != 0 || ok ? SC_REASON_PARTIAL_READ : SC_REASON_READ_FAILED), error};
    }
    return {};
}
bool Image::contains(uint32_t rva, size_t width, uint32_t required, uint32_t forbidden) const {
    if (!width || rva >= size || width > size - rva) return false;
    for (const auto& s : sections)
        if ((s.flags & required) == required && !(s.flags & forbidden) &&
            rva >= s.rva && rva - s.rva < s.size && width <= s.size - (rva - s.rva)) return true;
    return false;
}
ReadResult read_image(Memory& memory, uintptr_t base, Image& image) {
    image = {};
    IMAGE_DOS_HEADER dos{};
    auto status = read(memory, base, dos);
    if (status.reason) return status;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos) || dos.e_lfanew > 65536)
        return {SC_REASON_INVALID_PE, 0};
    uintptr_t nt_address = 0;
    if (!add(base, dos.e_lfanew, sizeof(IMAGE_NT_HEADERS64), nt_address)) return {SC_REASON_OUT_OF_RANGE, 0};
    IMAGE_NT_HEADERS64 nt{};
    status = read(memory, nt_address, nt);
    if (status.reason) return status;
    const auto& file = nt.FileHeader; const auto& opt = nt.OptionalHeader;
    if (nt.Signature != IMAGE_NT_SIGNATURE || file.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        opt.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || file.SizeOfOptionalHeader != sizeof(opt) ||
        !file.NumberOfSections || file.NumberOfSections > 32 || opt.SizeOfImage > 256 * 1024 * 1024 ||
        opt.SizeOfImage < 4096 || opt.SizeOfHeaders > 65536 || opt.SizeOfHeaders > opt.SizeOfImage ||
        opt.AddressOfEntryPoint >= opt.SizeOfImage) return {SC_REASON_INVALID_PE, 0};
    uintptr_t end = 0;
    if (!add(base, 0, opt.SizeOfImage, end)) return {SC_REASON_OUT_OF_RANGE, 0};
    const size_t section_offset = static_cast<size_t>(dos.e_lfanew) + sizeof(nt);
    if (section_offset + file.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > opt.SizeOfHeaders)
        return {SC_REASON_INVALID_PE, 0};
    Image parsed{base, opt.SizeOfImage, file.TimeDateStamp, opt.AddressOfEntryPoint, file.Machine, {}};
    for (unsigned i = 0; i < file.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        status = read(memory, base + section_offset + i * sizeof(section), section);
        if (status.reason) return status;
        const uint32_t rva = section.VirtualAddress, size = section.Misc.VirtualSize;
        if (!size || rva < opt.SizeOfHeaders || rva >= parsed.size || size > parsed.size - rva)
            return {SC_REASON_INVALID_PE, 0};
        for (const auto& old : parsed.sections)
            if (rva < old.rva + old.size && old.rva < rva + size) return {SC_REASON_INVALID_PE, 0};
        parsed.sections.push_back({rva, size, section.Characteristics});
    }
    if (!parsed.contains(parsed.entry, 1, readable | executable, 0)) return {SC_REASON_INVALID_PE, 0};
    image = std::move(parsed);
    return {};
}
sc_engine_snapshot unavailable(uint32_t reason) {
    sc_engine_snapshot s{}; s.size = sizeof(s); s.abi_version = SC_ENGINE_ABI_VERSION;
    s.sample_reason = s.pe_reason = s.disk_hash_reason = s.root_locator_reason = reason;
    for (auto& field : s.fields) field = unknown(reason);
    return s;
}
sc_engine_snapshot freshness(sc_engine_snapshot s, uint64_t now) {
    if (s.sampled_at_ms && (now < s.sampled_at_ms || now - s.sampled_at_ms > 1000)) {
        s.sample_reason = SC_REASON_STALE;
        for (auto& field : s.fields) field = unknown(SC_REASON_STALE);
    }
    return s;
}
Binding bind(Memory& memory, Image image, const char* hash, uint32_t hash_reason, HANDLE stop, uint64_t deadline) {
    Binding b{}; b.image = std::move(image);
    auto& s = b.metadata; s = unavailable(SC_REASON_NOT_SAMPLED);
    s.pe_reason = SC_REASON_NONE; s.machine = b.image.machine; s.timestamp = b.image.timestamp;
    s.image_size = b.image.size; s.entry_rva = b.image.entry;
    s.disk_hash_reason = hash_reason; s.locator_revision = 1;
    if (hash_reason == SC_REASON_NONE) strcpy_s(s.disk_sha256, hash);
    if (hash_reason == SC_REASON_NONE && std::strcmp(hash, known_hash) == 0 &&
        b.image.machine == IMAGE_FILE_MACHINE_AMD64 && b.image.size == 0x7431000 &&
        b.image.timestamp == 0x6a7b9b8c && b.image.entry == 0x286caa8)
        s.profile = SC_PROFILE_STEAM_20260818;
    constexpr uint32_t rvas[] = {0x5477298, 0x6b92f18, 0x5412948};
    for (size_t i = 0; i < b.globals.size(); ++i) {
        b.global_reasons[i] = s.profile == SC_PROFILE_NONE ? SC_REASON_PROFILE_UNRECOGNIZED :
            (b.image.contains(rvas[i], i == 2 ? 4 : 1, readable | writable, executable) ? SC_REASON_NONE : SC_REASON_OUT_OF_RANGE);
        if (b.global_reasons[i] == SC_REASON_NONE) b.globals[i] = b.image.base + rvas[i];
    }
    s.root_locator_reason = SC_REASON_SIGNATURE_MISSING;
    size_t matches = 0, scanned = 0;
    std::array<uint8_t, chunk_size> buffer{};
    for (const auto& section : b.image.sections) {
        if ((section.flags & (readable | executable)) != (readable | executable)) continue;
        for (size_t offset = 0; offset < section.size;) {
            const auto cancellation = interrupted(stop, deadline);
            if (cancellation) { s.root_locator_reason = cancellation; return b; }
            const size_t width = std::min(buffer.size(), section.size - offset);
            if (width < sizeof(signature)) break;
            scanned += width;
            if (scanned > 128 * 1024 * 1024) { s.root_locator_reason = SC_REASON_BUDGET; return b; }
            const auto status = memory.copy(b.image.base + section.rva + offset, buffer.data(), width);
            if (status.reason) { s.root_locator_reason = status.reason; return b; }
            for (size_t j = 0; j + sizeof(signature) <= width; ++j) {
                bool match = true;
                for (size_t k = 0; k < sizeof(signature); ++k)
                    if (mask[k] == 'x' && buffer[j + k] != signature[k]) { match = false; break; }
                if (!match) continue;
                if (++matches > 1) { s.root_locator_reason = SC_REASON_SIGNATURE_AMBIGUOUS; return b; }
                s.root_signature_rva = section.rva + static_cast<uint32_t>(offset + j);
                int32_t displacement = 0; std::memcpy(&displacement, buffer.data() + j + 3, sizeof(displacement));
                const int64_t target = int64_t(s.root_signature_rva) + 7 + displacement;
                s.root_target_rva = target >= 0 && target <= UINT32_MAX ? static_cast<uint32_t>(target) : 0;
            }
            if (width == section.size - offset) break;
            offset += width - (sizeof(signature) - 1);
        }
    }
    if (!matches) return b;
    if (s.profile == SC_PROFILE_STEAM_20260818 &&
        (s.root_signature_rva != 0x42f370 || s.root_target_rva != 0x45ea6f0)) {
        s.root_locator_reason = SC_REASON_INVALID_VALUE; return b;
    }
    if (!b.image.contains(s.root_target_rva, 8, readable | writable, executable) || s.root_target_rva % 8)
        s.root_locator_reason = SC_REASON_OUT_OF_RANGE;
    else {
        s.root_locator_reason = SC_REASON_NONE;
        b.root = b.image.base + s.root_target_rva;
    }
    return b;
}
Binding bind_host(Memory& memory, HANDLE stop) {
    Binding b{};
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Image image;
    const auto pe = read_image(memory, base, image);
    if (pe.reason) { b.metadata = unavailable(pe.reason); return b; }
    wchar_t path[32768]{};
    char hash[65]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    const auto deadline = GetTickCount64() + 2500;
    const auto reason = length && length < std::size(path) ? disk_hash(path, hash, stop, deadline) : SC_REASON_READ_FAILED;
    // Separate finite scan budget so unavailable disk evidence does not skip unknown-build validation.
    return bind(memory, std::move(image), hash, reason, stop, GetTickCount64() + 2000);
}

namespace {
struct Frame {
    std::array<sc_engine_field, SC_ENGINE_FIELD_COUNT> fields{};
    uintptr_t vtable = 0, first_function = 0, map = 0, player = 0;
    bool failed = false;
};
Frame frame(Memory& memory, const Binding& b) {
    Frame f{};
    for (auto& field : f.fields) field = unknown(SC_REASON_PARENT_UNAVAILABLE);
    auto fail = [&](size_t index, ReadResult error) {
        f.fields[index] = unknown(error.reason, error.error); f.failed = true;
    };
    for (size_t i = 0; i < b.globals.size(); ++i) {
        const size_t field = i == 2 ? SC_ENGINE_CUTSCENE_ID : SC_ENGINE_LOADING + i;
        if (b.global_reasons[i]) { f.fields[field] = unknown(b.global_reasons[i]); continue; }
        int32_t value = 0;
        ReadResult status;
        if (i == 2) status = read(memory, b.globals[i], value);
        else { uint8_t byte = 0; status = read(memory, b.globals[i], byte); value = byte; }
        if (status.reason) fail(field, status);
        else if (value < 0 || value > (i == 2 ? 100000 : 1)) fail(field, {SC_REASON_INVALID_VALUE, 0});
        else f.fields[field] = observed(value);
    }
    if (b.metadata.root_locator_reason) { f.fields[SC_ENGINE_ROOT] = unknown(b.metadata.root_locator_reason); return f; }
    auto status = read(memory, b.root, f.vtable);
    if (status.reason) { fail(SC_ENGINE_ROOT, status); return f; }
    const bool provisional = b.metadata.profile == SC_PROFILE_NONE;
    if (!f.vtable) {
        f.fields[SC_ENGINE_ROOT] = observed(0, provisional);
        f.fields[SC_ENGINE_MAP_PRESENT] = f.fields[SC_ENGINE_PLAYER_PRESENT] = unknown(SC_REASON_PARENT_NULL);
        return f;
    }
    if (!pointer(f.vtable) || !image_pointer(b.image, f.vtable, 8, readable, writable | executable)) {
        fail(SC_ENGINE_ROOT, {SC_REASON_OUT_OF_RANGE, 0}); return f;
    }
    status = read(memory, f.vtable, f.first_function);
    if (status.reason) { fail(SC_ENGINE_ROOT, status); return f; }
    if (!image_pointer(b.image, f.first_function, 1, readable | executable, 0)) {
        fail(SC_ENGINE_ROOT, {SC_REASON_OUT_OF_RANGE, 0}); return f;
    }
    f.fields[SC_ENGINE_ROOT] = observed(1, provisional);
    if (provisional) {
        f.fields[SC_ENGINE_MAP_PRESENT] = f.fields[SC_ENGINE_PLAYER_PRESENT] = unknown(SC_REASON_PROFILE_UNRECOGNIZED);
        return f;
    }
    uintptr_t address = 0;
    if (!add(b.root, 0x50, 8, address) || !image_pointer(b.image, address, 8, readable | writable, executable)) {
        fail(SC_ENGINE_MAP_PRESENT, {SC_REASON_OUT_OF_RANGE, 0}); return f;
    }
    status = read(memory, address, f.map);
    if (status.reason) { fail(SC_ENGINE_MAP_PRESENT, status); return f; }
    if (f.map && !pointer(f.map)) { fail(SC_ENGINE_MAP_PRESENT, {SC_REASON_INVALID_VALUE, 0}); return f; }
    f.fields[SC_ENGINE_MAP_PRESENT] = observed(f.map ? 1 : 0);
    if (!f.map) { f.fields[SC_ENGINE_PLAYER_PRESENT] = unknown(SC_REASON_PARENT_NULL); return f; }
    if (!add(f.map, 0x1af8, 8, address)) { fail(SC_ENGINE_PLAYER_PRESENT, {SC_REASON_OUT_OF_RANGE, 0}); return f; }
    status = read(memory, address, f.player);
    if (status.reason) { fail(SC_ENGINE_PLAYER_PRESENT, status); return f; }
    if (f.player && !pointer(f.player)) { fail(SC_ENGINE_PLAYER_PRESENT, {SC_REASON_INVALID_VALUE, 0}); return f; }
    f.fields[SC_ENGINE_PLAYER_PRESENT] = observed(f.player ? 1 : 0);
    return f;
}
bool same(const Frame& a, const Frame& b) {
    if (a.vtable != b.vtable || a.first_function != b.first_function || a.map != b.map || a.player != b.player) return false;
    for (size_t i = 0; i < a.fields.size(); ++i) {
        const auto& x = a.fields[i]; const auto& y = b.fields[i];
        if (x.validity != y.validity || x.reason != y.reason || x.value != y.value || x.win32_error != y.win32_error) return false;
    }
    return true;
}
}
sc_engine_snapshot sample(Memory& memory, const Binding& b, uint64_t sequence) {
    const auto start = GetTickCount64();
    auto s = b.metadata; s.sequence = sequence;
    if (s.pe_reason == SC_REASON_NONE) {
        const auto first = frame(memory, b), second = frame(memory, b);
        const bool coherent = !first.failed && !second.failed && same(first, second) && GetTickCount64() - start <= 50;
        s.sample_reason = coherent ? SC_REASON_NONE : SC_REASON_TRANSITION;
        for (size_t i = 0; i < SC_ENGINE_FIELD_COUNT; ++i) {
            s.fields[i] = second.fields[i];
            if (!coherent) {
                // Keep precise read errors; invalidate every observed value on any failed/changed pass.
                if (s.fields[i].validity != SC_OBSERVATION_UNKNOWN) s.fields[i] = unknown(SC_REASON_TRANSITION);
                if (first.fields[i].validity == SC_OBSERVATION_UNKNOWN && first.fields[i].reason != SC_REASON_PARENT_UNAVAILABLE)
                    s.fields[i] = first.fields[i];
            }
        }
    }
    s.sampled_at_ms = GetTickCount64(); s.duration_ms = static_cast<uint32_t>(s.sampled_at_ms - start);
    return s;
}
}
