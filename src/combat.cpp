#include "combat.h"
#include <algorithm>
#include <cstring>

namespace sentinel::combat {
namespace {
template<class T> T field(const std::array<uint8_t, 0x128>& bytes, size_t offset) noexcept {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
}
DeathRecord decode_record(const std::array<uint8_t, 0x128>& bytes) noexcept {
    DeathRecord r{};
    r.victim_spawn_id = field<uint32_t>(bytes, 0x48);
    r.victim_type = field<int32_t>(bytes, 0x4c);
    r.victim_ai_type = field<uint64_t>(bytes, 0x50);
    r.possessed_by_spirit = field<uint8_t>(bytes, 0x6c);
    r.spawned_by_archvile = field<uint8_t>(bytes, 0x6d);
    r.attacker_spawn_id = field<uint32_t>(bytes, 0x80);
    r.attacker_type = field<int32_t>(bytes, 0x84);
    r.attacker_player = field<uint64_t>(bytes, 0x98);
    r.inflictor_spawn_id = field<uint32_t>(bytes, 0xb8);
    r.attack_type = field<int32_t>(bytes, 0x38);
    r.death_time = field<int64_t>(bytes, 0xf0);
    r.game_mode = field<int32_t>(bytes, 0x124);
    return r;
}
void Model::transition(uint64_t generation) noexcept {
    generation_ = generation;
    context_ = {};
    current_counter_ = counter_capacity;
    std::fill(registrations_.begin(), registrations_.end(), Registration{});
    std::fill(claims_.begin(), claims_.end(), Claim{});
}
void Model::context(const Context& value) noexcept {
    context_ = value.generation == generation_ ? value : Context{};
    if (!context_.namespace_id[0] || context_.map.validity != SC_OBSERVATION_OBSERVED ||
        context_.map.reason != SC_REASON_NONE || !context_.map.length ||
        context_.map.length >= SC_CONTEXT_MAP_CAPACITY) {
        current_counter_ = counter_capacity;
        return;
    }
    const auto matches = [&](const Counter& counter) {
        return !std::memcmp(counter.namespace_id, context_.namespace_id, 65) &&
            !std::strcmp(counter.map, context_.map.bytes);
    };
    if (current_counter_ < counter_count_ && matches(counters_[current_counter_])) return;
    for (size_t n = 0; n < counter_count_; ++n) {
        if (matches(counters_[n])) { current_counter_ = n; return; }
    }
    current_counter_ = counter_count_;
    if (counter_count_ == counters_.size()) return;
    auto& counter = counters_[counter_count_++];
    std::memcpy(counter.namespace_id, context_.namespace_id, 65);
    std::memcpy(counter.map, context_.map.bytes, context_.map.length);
}
bool Model::ready(uint64_t now) const noexcept {
    return context_.generation == generation_ && context_.world && context_.player &&
        context_.owner_thread && context_.namespace_id[0] &&
        context_.map.validity == SC_OBSERVATION_OBSERVED && context_.map.reason == SC_REASON_NONE &&
        context_.map.length && context_.map.length < SC_CONTEXT_MAP_CAPACITY &&
        now >= context_.sampled_at_ms && now - context_.sampled_at_ms <= 1000;
}
bool Model::confirmed_dead(uintptr_t world, uintptr_t object, uint32_t slot, uint32_t spawn_id) const noexcept {
    if (slot >= registrations_.size()) return false;
    const auto& entry = registrations_[slot];
    return entry.emitted && entry.world == world && entry.object == object && entry.spawn_id == spawn_id;
}
void Model::registered(uintptr_t world, uintptr_t object, uint32_t slot, uint32_t spawn_id) noexcept {
    if (!world || !object || slot >= registrations_.size() || spawn_id == 0x1fffffeu) return;
    registrations_[slot] = {world, object, spawn_id, ++serial_};
}
Capture Model::capture(uintptr_t world, uintptr_t object, uint32_t slot, uint32_t spawn_id,
                       uint64_t now, uint64_t stamp, uint32_t thread) noexcept {
    if (!ready(now) || world != context_.world || stamp != context_.stamp ||
        !thread || slot >= registrations_.size()) return {};
    auto& entry = registrations_[slot];
    if (!entry.serial || entry.world != world || entry.object != object || entry.spawn_id != spawn_id)
        return {context_, 0, spawn_id, UINT32_MAX, thread};
    auto claim_index = entry.claim_index;
    if (claim_index >= claims_.size()) {
        for (claim_index = 0; claim_index < claims_.size(); ++claim_index)
            if (!claims_[claim_index].references) break;
        if (claim_index == claims_.size()) return {context_, 0, spawn_id, UINT32_MAX, thread};
        claims_[claim_index] = {entry.serial, slot, 0, entry.emitted};
        entry.claim_index = claim_index;
    }
    ++claims_[claim_index].references;
    Capture value{context_, entry.serial, spawn_id, claim_index, thread};
    return value;
}
void Model::release(const Capture& saved) noexcept {
    if (saved.claim_index >= claims_.size()) return;
    auto& claim = claims_[saved.claim_index];
    if (claim.serial != saved.registration_serial || !claim.references) return;
    if (--claim.references) return;
    auto& entry = registrations_[claim.slot];
    if (entry.serial == claim.serial) {
        entry.emitted = claim.emitted;
        entry.claim_index = UINT32_MAX;
    }
    claim = {};
}
Publication Model::publish(const Capture& saved, const DeathRecord& record, uint64_t now,
                           uint64_t stamp, uint32_t thread) noexcept {
    if (!ready(now) || saved.context.generation != generation_ ||
        saved.context.stamp != stamp || stamp != context_.stamp || !saved.callback_thread || thread != saved.callback_thread ||
        saved.context.world != context_.world || saved.context.player != context_.player ||
        std::memcmp(saved.context.namespace_id, context_.namespace_id, 65) ||
        record.victim_spawn_id != saved.victim_spawn_id || record.victim_type != 1 ||
        record.game_mode != 1 ||
        record.attack_type == 2) return Publication::ignored;
    if (!saved.registration_serial) return Publication::missing_registration;
    if (saved.claim_index >= claims_.size()) return Publication::missing_registration;
    auto& claim = claims_[saved.claim_index];
    if (claim.serial != saved.registration_serial || !claim.references) return Publication::missing_registration;
    if (claim.emitted) return Publication::duplicate;
    claim.emitted = true;
    auto& entry = registrations_[claim.slot];
    if (entry.serial == claim.serial) entry.emitted = true;
    if (current_counter_ >= counter_count_ || counters_[current_counter_].kills == UINT64_MAX) {
        if (counter_gaps_ != UINT64_MAX) ++counter_gaps_;
        return Publication::ignored;
    }
    ++counters_[current_counter_].kills;
    return Publication::published;
}
void Model::snapshot(const sc_combat_query& query, sc_combat_snapshot& out) const noexcept {
    out.scope.lifecycle_generation = generation_;
    std::memcpy(out.namespace_id, query.namespace_id, 65);
    out.observation_gaps = counter_gaps_ > UINT64_MAX - out.observation_gaps ?
        UINT64_MAX : out.observation_gaps + counter_gaps_;
    out.qualified_kills = 0;
    for (size_t n = 0; n < counter_count_; ++n) {
        const auto& counter = counters_[n];
        if (std::memcmp(counter.namespace_id, query.namespace_id, 65)) continue;
        for (size_t map = 0; map < query.map_count; ++map) {
            if (std::strcmp(counter.map, query.maps[map])) continue;
            if (counter.kills > UINT64_MAX - out.qualified_kills) {
                out.reason = SC_NATIVE_EXCEPTION;
                out.qualified_kills = 0;
                return;
            }
            out.qualified_kills += counter.kills;
            break;
        }
    }
}
}
