#ifndef SENTINEL_COMBAT_MODEL_H
#define SENTINEL_COMBAT_MODEL_H
#include "sentinel_combat.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sentinel::combat {
constexpr size_t managed_slots = 0x3ffe;
struct Context {
    uint64_t generation = 0, stamp = 0, sampled_at_ms = 0;
    uintptr_t world = 0, player = 0;
    uint32_t owner_thread = 0;
    char namespace_id[65]{};
    sc_context_map map{};
};
struct Capture {
    Context context{};
    uint64_t registration_serial = 0;
    uint32_t victim_spawn_id = 0;
    uint32_t claim_index = UINT32_MAX;
    uint32_t callback_thread = 0;
};
struct DeathRecord {
    uint32_t victim_spawn_id = 0, attacker_spawn_id = 0, inflictor_spawn_id = 0;
    int32_t victim_type = -1, attacker_type = -1, attack_type = -1, game_mode = 0;
    uint64_t victim_ai_type = 0;
    uintptr_t attacker_player = 0;
    int64_t death_time = 0;
    uint8_t possessed_by_spirit = 0, spawned_by_archvile = 0;
};
DeathRecord decode_record(const std::array<uint8_t, 0x128>&) noexcept;
inline bool valid_query(const sc_combat_query& query) noexcept {
    if (query.namespace_id[64] || !query.map_count || query.map_count > SC_COMBAT_MAX_MAPS) return false;
    for (size_t n = 0; n < 64; ++n) {
        const auto ch = query.namespace_id[n];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    for (size_t n = 0; n < query.map_count; ++n) {
        const auto* end = static_cast<const char*>(std::memchr(query.maps[n], 0, SC_CONTEXT_MAP_CAPACITY));
        if (!end || end == query.maps[n]) return false;
        for (auto* ch = query.maps[n]; ch != end; ++ch)
            if (static_cast<unsigned char>(*ch) < 32 || static_cast<unsigned char>(*ch) > 126) return false;
        for (size_t previous = 0; previous < n; ++previous)
            if (!std::strcmp(query.maps[n], query.maps[previous])) return false;
    }
    return true;
}
constexpr size_t counter_capacity = 1024;
enum class Publication { ignored, missing_registration, duplicate, published };
class Model {
public:
    void transition(uint64_t generation) noexcept;
    void context(const Context&) noexcept;
    void registered(uintptr_t world, uintptr_t object, uint32_t slot, uint32_t spawn_id) noexcept;
    Capture capture(uintptr_t world, uintptr_t object, uint32_t slot, uint32_t spawn_id,
                    uint64_t now, uint64_t stamp, uint32_t thread) noexcept;
    void release(const Capture&) noexcept;
    Publication publish(const Capture&, const DeathRecord&, uint64_t now,
                        uint64_t stamp, uint32_t thread) noexcept;
    bool ready(uint64_t now) const noexcept;
    bool confirmed_dead(uintptr_t world, uintptr_t object, uint32_t slot, uint32_t spawn_id) const noexcept;
    bool suppress(uint64_t generation, uintptr_t world, uintptr_t object, uint32_t slot, uint32_t spawn_id) noexcept;
    uint64_t generation() const noexcept { return generation_; }
    void snapshot(const sc_combat_query&, sc_combat_snapshot&) const noexcept;
private:
    struct Registration {
        uintptr_t world = 0, object = 0;
        uint32_t spawn_id = 0;
        uint64_t serial = 0;
        uint32_t claim_index = UINT32_MAX;
        bool emitted = false, suppressed = false;
    };
    struct Claim {
        uint64_t serial = 0;
        uint32_t slot = 0, references = 0;
        bool emitted = false, suppressed = false;
    };
    struct Counter {
        char namespace_id[65]{};
        char map[SC_CONTEXT_MAP_CAPACITY]{};
        uint64_t kills = 0;
    };
    uint64_t generation_ = 0, serial_ = 0, counter_gaps_ = 0;
    Context context_{};
    std::array<Registration, managed_slots> registrations_{};
    std::array<Claim, 64> claims_{};
    std::array<Counter, counter_capacity> counters_{};
    size_t counter_count_ = 0, current_counter_ = counter_capacity;
};
}
#endif
