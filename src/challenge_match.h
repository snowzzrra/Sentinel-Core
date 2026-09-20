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
inline bool suppression_match(const ScopeFacts& scope, const CallFacts& call) {
    return scope.active && scope.admitted && scope.map_qualified && scope.record_mission &&
        scope.canonical_group && scope.epoch != 0 && scope.thread != 0 &&
        scope.owner_thread == scope.thread && scope.player != 0 && scope.map != 0 &&
        call.admitted && call.session_admitted && call.thread == scope.thread &&
        call.epoch == scope.epoch && call.player == scope.player && call.map == scope.map &&
        call.return_site == call.expected_return_site &&
        call.currency == sentinel_battery_currency && call.delta == sentinel_battery_delta &&
        call.notify == 0;
}
}
