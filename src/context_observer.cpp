#include "context_observer.h"
#include <cstring>

namespace sentinel::context {
namespace {
// SHA256 9809708c...1247, Ghidra 12.1.3/ReVa 7.3.1. Native mappath handler
// RVA 0x437f10 -> root+0x50 -> map vtable+0x50 -> getter RVA 0x69a850.
// Constructor RVA 0x685f50 owns idStr at +0x9a060; setter 0x6abf00 and
// destructor 0x68acf0 corroborate. Evidence: external SentinelDocs Phase 4.2 report.
constexpr uint32_t root_vtable_rva = 0x2aaa730, map_vtable_rva = 0x2ab30c8;
constexpr size_t name_offset = 0x9a060;
sc_context_field unknown(uint32_t reason, uint32_t error = 0) {
    return {SC_OBSERVATION_UNKNOWN, reason, 0, error};
}
sc_context_field observed(uint64_t value) { return {SC_OBSERVATION_OBSERVED, SC_REASON_NONE, value, 0}; }
void invalidate_map(sc_context_map& map, uint32_t reason, uint32_t error = 0) {
    map = {}; map.reason = reason; map.win32_error = error;
}
template<class T> engine::ReadResult read(engine::Memory& memory, uintptr_t owner, size_t offset, T& value) {
    value = {};
    uintptr_t address = 0;
    if (!engine::add(owner, offset, sizeof(value), address)) return {SC_REASON_OUT_OF_RANGE, 0};
    const auto result = memory.copy(address, &value, sizeof(value));
    if (result.reason) value = {};
    return result;
}
struct State { uint32_t value = 0, changed_ms = 0; };
struct String { uintptr_t vtable = 0, data = 0; int32_t length = 0; uint32_t allocation = 0; };
static_assert(sizeof(State) == 8 && sizeof(String) == 24, "Native x64 layout");
struct Frame {
    uintptr_t root_vtable = 0, map = 0, map_vtable = 0;
    State state{};
    String string{};
    engine::ReadResult state_result{}, map_result{};
    sc_context_map name{};
};
Frame frame(engine::Memory& memory, const engine::Binding& b) {
    Frame f;
    auto status = read(memory, b.root, 0, f.root_vtable);
    if (!status.reason && f.root_vtable != b.image.base + root_vtable_rva)
        status.reason = SC_REASON_INVALID_VALUE;
    if (status.reason) { f.state_result = f.map_result = status; return f; }
    f.state_result = read(memory, b.root, 0x44, f.state);
    if (!f.state_result.reason && f.state.value > SC_GAME_IN_GAME)
        f.state_result.reason = SC_REASON_INVALID_VALUE;
    if (f.state_result.reason) { f.map_result = f.state_result; return f; }
    if (f.state.value != SC_GAME_IN_GAME) {
        f.map_result.reason = f.state.value == SC_GAME_LOADING ? SC_REASON_TRANSITION : SC_CONTEXT_MENU_WORLD;
        return f;
    }
    status = read(memory, b.root, 0x50, f.map);
    if (!status.reason && !f.map) status.reason = SC_REASON_PARENT_NULL;
    if (!status.reason && f.map % 8) status.reason = SC_REASON_INVALID_VALUE;
    if (!status.reason) status = read(memory, f.map, 0, f.map_vtable);
    if (!status.reason && f.map_vtable != b.image.base + map_vtable_rva)
        status.reason = SC_REASON_INVALID_VALUE;
    if (!status.reason) status = read(memory, f.map, name_offset, f.string);
    if (!status.reason) {
        // idStr capacity occupies bits 0..29; bit 30 owns the allocation and
        // bit 31 permits growth. Inline and heap storage use the same data pointer.
        const auto capacity = f.string.allocation & 0x3fffffff;
        if (f.string.length < 0 || static_cast<uint32_t>(f.string.length) >= capacity)
            status.reason = SC_REASON_INVALID_VALUE;
        else if (!f.string.length) status.reason = SC_CONTEXT_EMPTY_NAME;
        else if (static_cast<uint32_t>(f.string.length) >= SC_CONTEXT_MAP_CAPACITY) status.reason = SC_CONTEXT_NAME_TOO_LONG;
        else {
            uintptr_t address = 0;
            const auto width = static_cast<size_t>(f.string.length) + 1;
            if (!engine::add(f.string.data, 0, width, address)) status.reason = SC_REASON_OUT_OF_RANGE;
            else status = memory.copy(address, f.name.bytes, width);
            if (!status.reason) {
                if (f.name.bytes[f.string.length] != 0) status.reason = SC_REASON_INVALID_VALUE;
                for (int32_t i = 0; i < f.string.length; ++i)
                    if (static_cast<unsigned char>(f.name.bytes[i]) < 32 ||
                        static_cast<unsigned char>(f.name.bytes[i]) > 126) status.reason = SC_REASON_INVALID_VALUE;
                if (!status.reason) {
                    f.name.validity = SC_OBSERVATION_OBSERVED;
                    f.name.length = static_cast<uint32_t>(f.string.length);
                }
            }
        }
    }
    f.map_result = status;
    return f;
}
bool same_state(const Frame& a, const Frame& b) {
    return a.root_vtable == b.root_vtable && a.state.value == b.state.value && a.state.changed_ms == b.state.changed_ms;
}
bool same_map(const Frame& a, const Frame& b) {
    return same_state(a, b) && a.map == b.map && a.map_vtable == b.map_vtable &&
        a.string.vtable == b.string.vtable && a.string.data == b.string.data &&
        a.string.length == b.string.length && a.string.allocation == b.string.allocation &&
        a.name.length == b.name.length && std::memcmp(a.name.bytes, b.name.bytes, sizeof(a.name.bytes)) == 0;
}
}
sc_context_snapshot unavailable(uint32_t reason) {
    sc_context_snapshot s{}; s.size = sizeof(s); s.abi_version = SC_CONTEXT_ABI_VERSION;
    s.sample_reason = s.root_locator_reason = s.disk_hash_reason = reason;
    invalidate_map(s.current_map, reason);
    for (auto& field : s.fields) field = unknown(reason);
    return s;
}
sc_context_snapshot sample(engine::Memory& memory, const engine::Binding& b, uint64_t sequence, HANDLE stop) {
    const auto start = GetTickCount64();
    auto s = unavailable(SC_REASON_NOT_SAMPLED);
    s.sequence = sequence; s.profile = b.metadata.profile; s.locator_revision = b.metadata.locator_revision;
    s.root_locator_reason = b.metadata.root_locator_reason; s.disk_hash_reason = b.metadata.disk_hash_reason;
    std::memcpy(s.disk_sha256, b.metadata.disk_sha256, sizeof(s.disk_sha256));
    for (size_t i = SC_CONTEXT_LOAD_SERIAL; i < SC_CONTEXT_FIELD_COUNT; ++i) s.fields[i] = unknown(SC_CONTEXT_UNSUPPORTED);
    uint32_t reason = b.metadata.pe_reason;
    if (!reason && s.profile != SC_PROFILE_STEAM_20260818) reason = SC_REASON_PROFILE_UNRECOGNIZED;
    if (!reason && !b.root) reason = s.root_locator_reason ? s.root_locator_reason : SC_REASON_PARENT_UNAVAILABLE;
    if (!reason) s.layout_revision = 1;
    if (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) reason = SC_REASON_CANCELLED;
    if (!reason) {
        const auto first = frame(memory, b), second = frame(memory, b);
        auto state_error = first.state_result.reason ? first.state_result : second.state_result;
        auto map_error = first.map_result.reason ? first.map_result : second.map_result;
        if (!state_error.reason && !same_state(first, second)) state_error.reason = SC_REASON_TRANSITION;
        if (!map_error.reason && !same_map(first, second)) map_error.reason = SC_REASON_TRANSITION;
        if (GetTickCount64() - start > 50) state_error.reason = map_error.reason = SC_REASON_BUDGET;
        if (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) state_error.reason = map_error.reason = SC_REASON_CANCELLED;
        s.fields[SC_CONTEXT_GAME_STATE] = state_error.reason ? unknown(state_error.reason, state_error.error) : observed(second.state.value);
        s.fields[SC_CONTEXT_STATE_CHANGED_MS] = state_error.reason ? unknown(state_error.reason, state_error.error) : observed(second.state.changed_ms);
        if (map_error.reason) invalidate_map(s.current_map, map_error.reason, map_error.error);
        else s.current_map = second.name;
        s.sample_reason = state_error.reason ? state_error.reason : map_error.reason;
    } else {
        invalidate_map(s.current_map, reason);
        s.fields[SC_CONTEXT_GAME_STATE] = s.fields[SC_CONTEXT_STATE_CHANGED_MS] = unknown(reason);
        s.sample_reason = reason;
    }
    s.sampled_at_ms = GetTickCount64(); s.duration_ms = static_cast<uint32_t>(s.sampled_at_ms - start);
    return s;
}
sc_context_snapshot freshness(sc_context_snapshot s, uint64_t now) {
    if (s.sampled_at_ms && (now < s.sampled_at_ms || now - s.sampled_at_ms > 1000)) {
        s.sample_reason = SC_REASON_STALE; invalidate_map(s.current_map, SC_REASON_STALE);
        for (auto& field : s.fields) if (field.reason != SC_CONTEXT_UNSUPPORTED) field = unknown(SC_REASON_STALE);
    }
    return s;
}
}
