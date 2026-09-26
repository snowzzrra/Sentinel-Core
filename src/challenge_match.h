// Pure suppression predicate for the hardcoded campaign mission challenge
// Sentinel Battery grant. Facts only; every native read, admission check and
// lifetime comparison is supplied by the caller so the contract stays testable.
#pragma once
#include <cstdint>

namespace sentinel::challenge {
inline constexpr uint32_t sentinel_battery_currency = 4;
inline constexpr int32_t sentinel_battery_delta = 1;
inline constexpr uint32_t canonical_group_members = 3;
inline constexpr uint32_t max_member_witnesses = 8;

struct ScopeFacts {
    bool active = false;
    bool admitted = false;
    bool map_qualified = false;
    bool record_mission = false;
    bool canonical_group = false;
    uint64_t epoch = 0;
    uint32_t thread = 0;
    uint32_t owner_thread = 0;
    uintptr_t player = 0;
    uintptr_t map = 0;
};

struct CallFacts {
    bool admitted = false;
    bool session_admitted = false;
    uint64_t epoch = 0;
    uint32_t thread = 0;
    uintptr_t player = 0;
    uintptr_t map = 0;
    uintptr_t return_site = 0;
    uintptr_t expected_return_site = 0;
    uint32_t currency = 0;
    int32_t delta = 0;
    uint8_t notify = 0;
};

// A match requires the exact native player/map/thread/epoch identity, the
// canonical three-member campaign aggregate captured from the native group
// scan, the documented caller/return site and the exact currency arguments.
inline const char* suppression_mismatch(const ScopeFacts& scope, const CallFacts& call) {
    if (!scope.active) return "active";
    if (!scope.admitted) return "admitted";
    if (!scope.map_qualified) return "map_qualified";
    if (!scope.record_mission) return "record_mission";
    if (!scope.canonical_group) return "canonical_group";
    if (!scope.epoch) return "epoch";
    if (!scope.thread) return "scope_thread";
    if (!scope.player || !scope.map) return "scope_player_map";
    if (!call.admitted || !call.session_admitted) return "call_admission";
    if (call.thread != scope.thread || call.epoch != scope.epoch ||
        call.player != scope.player || call.map != scope.map) return "call_identity";
    if (call.return_site != call.expected_return_site) return "return_site";
    if (call.currency != sentinel_battery_currency) return "currency";
    if (call.delta != sentinel_battery_delta) return "delta";
    if (call.notify != 0) return "notify";
    return nullptr;
}

inline bool suppression_match(const ScopeFacts& scope, const CallFacts& call) {
    return suppression_mismatch(scope, call) == nullptr;
}

inline bool suppression_diagnostic_candidate(const ScopeFacts& scope, const CallFacts& call) {
    return call.currency == sentinel_battery_currency && call.delta == sentinel_battery_delta &&
        (call.return_site == call.expected_return_site || (scope.active && scope.record_mission));
}
}
