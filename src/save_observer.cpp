// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_observer.h"
#include <array>
#include <algorithm>

namespace sentinel::save {
namespace {
// Target SHA256 9809708c...1247. Root+9b38 ProfileManager and request witnesses
// corroborated by native scheduling functions RVA 6744e0/66add0. Provider vtables
// corroborated by RTTI and constructors; external SentinelDocs phase44-45 tables.
// Never call platform selector/profile initializer: even its getter path mutates.
constexpr uint32_t root_vtable_rva = 0x2aaa730;
constexpr size_t observed_count = SC_SAVE_SELECTED_SLOT;
sc_save_field unknown(uint32_t reason, uint32_t error = 0) { return {SC_OBSERVATION_UNKNOWN, reason, 0, error, 0}; }
sc_save_field observed(uint64_t value) { return {SC_OBSERVATION_OBSERVED, SC_REASON_NONE, value, 0, 0}; }
template<class T> engine::ReadResult read(engine::Memory& memory, uintptr_t owner, size_t offset, T& value) {
    value = {}; uintptr_t address = 0;
    if (!engine::add(owner, offset, sizeof(value), address)) return {SC_REASON_OUT_OF_RANGE, 0};
    const auto status = memory.copy(address, &value, sizeof(value));
    if (status.reason) value = {};
    return status;
}
engine::ReadResult pointer(engine::Memory& memory, uintptr_t owner, size_t offset, uintptr_t& value) {
    auto status = read(memory, owner, offset, value); uintptr_t checked = 0;
    if (!status.reason && value && (value % 8 || !engine::add(value, 0, sizeof(uintptr_t), checked)))
        status.reason = SC_REASON_OUT_OF_RANGE;
    return status;
}
struct Frame {
    // Owned pointer chains kept internal solely to detect changed parents.
    std::array<uintptr_t, 12> pointers{};
    uint32_t state = 0, state_changed = 0;
    int32_t queued = 0;
    uint8_t pending = 0;
    std::array<sc_save_field, observed_count> fields{};
    engine::ReadResult status{};
};
Frame frame(engine::Memory& memory, const engine::Binding& b) {
    Frame f;
    auto& p = f.pointers;
    f.status = read(memory, b.root, 0, p[0]);
    if (!f.status.reason && p[0] != b.image.base + root_vtable_rva) f.status.reason = SC_REASON_INVALID_VALUE;
    if (f.status.reason) return f;
    f.fields[SC_SAVE_ROOT] = observed(1);
    struct State { uint32_t value, changed; } state{};
    f.status = read(memory, b.root, 0x44, state);
    if (f.status.reason) return f;
    f.state = state.value; f.state_changed = state.changed;
    if (f.state > SC_GAME_IN_GAME) { f.status.reason = SC_REASON_INVALID_VALUE; return f; }
    if (f.state == SC_GAME_LOADING) { f.status.reason = SC_REASON_TRANSITION; return f; }
    f.status = read(memory, b.root, 0xb8, f.pending);
    if (f.status.reason) return f;
    if (f.pending > 1) { f.status.reason = SC_REASON_INVALID_VALUE; return f; }
    if (f.pending) { f.status.reason = SC_REASON_TRANSITION; return f; }
    f.fields[SC_SAVE_PENDING_MAP_LOAD] = observed(f.pending);
    f.status = read(memory, b.root, 0x9ca8, f.queued);
    if (f.status.reason) return f;
    if (f.queued < 0) { f.status.reason = SC_REASON_INVALID_VALUE; return f; }
    f.fields[SC_SAVE_QUEUED_REQUESTS] = observed(static_cast<uint32_t>(f.queued));
    f.status = pointer(memory, b.root, 0x9b38, p[1]);
    if (f.status.reason) return f;
    f.fields[SC_SAVE_MANAGER] = observed(p[1] != 0);
    for (const auto i : {SC_SAVE_PROVIDER, SC_SAVE_JOB_WITNESS, SC_SAVE_REQUEST58_WITNESS, SC_SAVE_REQUEST50_WITNESS})
        f.fields[i] = unknown(SC_REASON_PARENT_NULL);
    if (!p[1]) return f;
    f.status = pointer(memory, p[1], 0, p[2]);
    if (f.status.reason) return f;
    if (p[2]) {
        f.status = pointer(memory, p[2], 8, p[3]);
        if (f.status.reason) return f;
        if (p[3]) {
            f.status = pointer(memory, p[3], 0, p[4]);
            if (f.status.reason) return f;
            if (!p[4]) { f.status.reason = SC_REASON_INVALID_VALUE; return f; }
            const auto provider = p[4] == b.image.base + 0x2e90658 ? SC_SAVE_PROVIDER_STEAM :
                (p[4] == b.image.base + 0x2e87db0 ? SC_SAVE_PROVIDER_LOCAL_ENCRYPTED :
                (p[4] == b.image.base + 0x2e88198 ? SC_SAVE_PROVIDER_LOCAL : SC_SAVE_PROVIDER_FOREIGN));
            f.fields[SC_SAVE_PROVIDER] = observed(provider);
        }
    }
    // The +58 request has an extra nested reference object. Null at any stage is
    // only absence of this witness, never a scheduler-idle/completion conclusion.
    constexpr size_t offsets[] = {0x60, 0x58, 0x50}, starts[] = {5, 7, 10};
    for (size_t i = 0; i < 3; ++i) {
        const auto at = starts[i];
        f.status = pointer(memory, p[1], offsets[i], p[at]);
        if (f.status.reason) return f;
        if (p[at]) {
            f.status = pointer(memory, p[at], 8, p[at + 1]);
            if (f.status.reason) return f;
            if (i == 1 && p[at + 1]) {
                f.status = pointer(memory, p[at + 1], 8, p[at + 2]);
                if (f.status.reason) return f;
            }
        }
        f.fields[SC_SAVE_JOB_WITNESS + i] = observed(p[at + (i == 1 ? 2 : 1)] != 0);
    }
    return f;
}
void suppress(sc_save_snapshot& s, uint32_t reason, uint32_t error = 0) {
    s.sample_reason = reason;
    for (size_t i = 0; i < observed_count; ++i) s.fields[i] = unknown(reason, error);
}
}
sc_save_snapshot unavailable(uint32_t reason) {
    sc_save_snapshot s{}; s.size = sizeof(s); s.abi_version = SC_SAVE_ABI_VERSION;
    s.root_locator_reason = reason; suppress(s, reason);
    s.mutation_reason = SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN;
    s.fields[SC_SAVE_SELECTED_SLOT] = unknown(SC_SAVE_SELECTED_SLOT_UNPROVEN);
    s.fields[SC_SAVE_NAMESPACE_ROUTE] = unknown(SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN);
    s.fields[SC_SAVE_NATIVE_OPERATION_ID] = unknown(SC_SAVE_COMPLETION_UNPROVEN);
    s.fields[SC_SAVE_NATIVE_COMPLETION] = unknown(SC_SAVE_COMPLETION_UNPROVEN);
    return s;
}
sc_save_snapshot sample(engine::Memory& memory, const engine::Binding& b, uint64_t sequence,
                        HANDLE stop, const context::Clock* supplied_clock) {
    const auto& clock = supplied_clock ? *supplied_clock : context::observation_clock();
    const auto start = clock.uptime();
    int64_t begin = 0, end = 0;
    const bool started = clock.frequency > 0 && clock.counter(begin);
    auto s = unavailable(SC_REASON_NOT_SAMPLED);
    s.sequence = sequence; s.profile = b.metadata.profile;
    s.root_locator_reason = b.metadata.root_locator_reason;
    uint32_t reason = b.metadata.pe_reason;
    if (!reason && s.profile != SC_PROFILE_STEAM_20260818) reason = SC_REASON_PROFILE_UNRECOGNIZED;
    if (!reason && (!b.root || s.root_locator_reason)) reason = s.root_locator_reason ? s.root_locator_reason : SC_REASON_PARENT_UNAVAILABLE;
    if (!reason) s.layout_revision = 1;
    if (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) reason = SC_REASON_CANCELLED;
    if (!reason) {
        const auto first = frame(memory, b), second = frame(memory, b);
        auto status = first.status.reason ? first.status : second.status;
        if (!status.reason && (first.pointers != second.pointers || first.state != second.state ||
            first.state_changed != second.state_changed || first.queued != second.queued || first.pending != second.pending))
            status.reason = SC_REASON_TRANSITION;
        if (!started || !clock.counter(end) || end < begin) status = {SC_REASON_INTERNAL_ERROR, ERROR_INVALID_DATA};
        else if (static_cast<double>(end - begin) > static_cast<double>(clock.frequency) * 0.05)
            status = {SC_REASON_BUDGET, 0};
        if (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) status = {SC_REASON_CANCELLED, 0};
        if (status.reason) suppress(s, status.reason, status.error);
        else { s.sample_reason = SC_REASON_NONE; std::copy(second.fields.begin(), second.fields.end(), s.fields); }
    } else suppress(s, reason);
    s.sampled_at_ms = clock.uptime();
    s.duration_ms = static_cast<uint32_t>(std::min<uint64_t>(s.sampled_at_ms >= start ? s.sampled_at_ms - start : 0, UINT32_MAX));
    return s;
}
sc_save_snapshot freshness(sc_save_snapshot s, uint64_t now) {
    if (s.sampled_at_ms && (now < s.sampled_at_ms || now - s.sampled_at_ms > 1000)) suppress(s, SC_REASON_STALE);
    return s;
}
}
