// Generated from protocol/schema.json; regenerate: python -B tools/generate_protocol.py; schema-sha256: 6d1dd65bd4d2c3b57cc546d218544141143eb3797461a64e7b829556e0480a0c; generator-format: 1. DO NOT EDIT.
#include "sentinel_protocol_generated.h"
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>
namespace sentinel::generated {
namespace {
template<class C> bool transfer(C&, v1_command_submit&);
template<class C> bool transfer(C&, v1_command_result&);
template<class C> bool transfer(C&, v1_inventory_result&);
template<class C> bool transfer(C&, v1_inspect_request&);
template<class C> bool transfer(C&, v1_diagnostic_detail_submit&);
template<class C> bool transfer(C&, campaign_reward&);
template<class C> bool transfer(C&, v1_campaign_rewards_row&);
template<class C> bool transfer(C&, v1_automap_publish&);
template<class C> bool transfer(C&, v1_campaign_base&);
template<class C> bool transfer(C&, v1_campaign_presentation&);
template<class C> bool transfer(C&, v1_runes_request&);
template<class C> bool transfer(C&, scope&);
template<class C> bool transfer(C&, command_reference&);
template<class C> bool transfer(C&, submission_identity&);
template<class C> bool transfer(C&, submission&);
template<class C> bool transfer(C&, ack&);
template<class C> bool transfer(C&, result_dimensions&);
template<class C> bool transfer(C&, sc_status&);
template<class C> bool transfer(C&, sc_engine_field&);
template<class C> bool transfer(C&, sc_engine_snapshot&);
template<class C> bool transfer(C&, sc_context_map&);
template<class C> bool transfer(C&, sc_context_field&);
template<class C> bool transfer(C&, sc_context_snapshot&);
template<class C> bool transfer(C&, sc_native_scope&);
template<class C> bool transfer(C&, sc_native_event&);
template<class C> bool transfer(C&, sc_native_snapshot&);
template<class C> bool transfer(C&, sc_diagnostic_result&);
template<class C> bool transfer(C&, sc_save_field&);
template<class C> bool transfer(C&, sc_save_snapshot&);
template<class C> bool transfer(C&, sc_save_admission_snapshot&);
template<class C> bool transfer(C&, sc_save_write_snapshot&);
template<class C> bool transfer(C&, sc_save_backup_snapshot&);
template<class C> bool transfer(C&, sc_install_event&);
template<class C> bool transfer(C&, sc_save_installation_snapshot&);
template<class C> bool transfer(C&, sc_weapon_points_result&);
template<class C> bool transfer(C&, sc_campaign_result&);
template<class C> bool transfer(C&, sc_inventory_result&);
template<class C> bool transfer(C&, sc_arsenal_result&);
template<class C> bool transfer(C&, sc_runes_result&);
template<class C> bool transfer(C&, sc_special_result&);
template<class C> bool transfer(C&, sc_deathlink_result&);
template<class C> bool transfer(C&, sc_automap_result&);
template<class C> bool transfer(C&, sc_command_result&);
template<class C> bool transfer(C&, sc_combat_snapshot&);
template<class C> bool transfer(C&, sc_combat_presentation_result&);
template<class C> bool transfer(C&, inspect_inspect_payload&);
template<class C> bool transfer(C&, engine_engine_payload&);
template<class C> bool transfer(C&, context_context_payload&);
template<class C> bool transfer(C&, native_native_payload&);
template<class C> bool transfer(C&, diagnostic_diagnostic_payload&);
template<class C> bool transfer(C&, diagnostic_detail_diagnostic_detail_payload&);
template<class C> bool transfer(C&, save_save_payload&);
template<class C> bool transfer(C&, save_admission_save_admission_payload&);
template<class C> bool transfer(C&, save_write_save_write_payload&);
template<class C> bool transfer(C&, save_backup_save_backup_payload&);
template<class C> bool transfer(C&, save_installation_save_installation_payload&);
template<class C> bool transfer(C&, weapon_points_observe_payload&);
template<class C> bool transfer(C&, weapon_points_grant_payload&);
template<class C> bool transfer(C&, sc_campaign_row&);
template<class C> bool transfer(C&, sc_physical_challenge&);
template<class C> bool transfer(C&, sc_campaign_summary&);
template<class C> bool transfer(C&, campaign_row_campaign_row_payload&);
template<class C> bool transfer(C&, campaign_commit_campaign_commit_payload&);
template<class C> bool transfer(C&, campaign_campaign_payload&);
template<class C> bool transfer(C&, inventory_observe_payload&);
template<class C> bool transfer(C&, inventory_ensure_payload&);
template<class C> bool transfer(C&, inventory_set_capacity_payload&);
template<class C> bool transfer(C&, arsenal_observe_payload&);
template<class C> bool transfer(C&, arsenal_ensure_mods_payload&);
template<class C> bool transfer(C&, arsenal_select_mod_payload&);
template<class C> bool transfer(C&, arsenal_project_mastery_payload&);
template<class C> bool transfer(C&, runes_observe_payload&);
template<class C> bool transfer(C&, runes_ensure_normal_payload&);
template<class C> bool transfer(C&, runes_ensure_support_payload&);
template<class C> bool transfer(C&, runes_select_normal_payload&);
template<class C> bool transfer(C&, runes_select_support_payload&);
template<class C> bool transfer(C&, special_observe_payload&);
template<class C> bool transfer(C&, special_ensure_ownership_payload&);
template<class C> bool transfer(C&, special_select_payload&);
template<class C> bool transfer(C&, special_refill_status_payload&);
template<class C> bool transfer(C&, special_refill_publish_payload&);
template<class C> bool transfer(C&, special_refill_take_payload&);
template<class C> bool transfer(C&, special_refill_execute_payload&);
template<class C> bool transfer(C&, deathlink_observe_payload&);
template<class C> bool transfer(C&, deathlink_configure_payload&);
template<class C> bool transfer(C&, deathlink_apply_remote_payload&);
template<class C> bool transfer(C&, deathlink_poll_local_payload&);
template<class C> bool transfer(C&, deathlink_ack_local_payload&);
template<class C> bool transfer(C&, deathlink_cancel_remote_payload&);
template<class C> bool transfer(C&, automap_publish_payload&);
template<class C> bool transfer(C&, automap_observe_payload&);
template<class C> bool transfer(C&, command_activate_payload&);
template<class C> bool transfer(C&, command_player_perk_payload&);
template<class C> bool transfer(C&, command_ammo_refill_payload&);
template<class C> bool transfer(C&, command_crucible_refill_payload&);
template<class C> bool transfer(C&, command_echo_payload&);
template<class C> bool transfer(C&, command_condump_payload&);
template<class C> bool transfer(C&, command_list_inventory_payload&);
template<class C> bool transfer(C&, command_transient_policy_payload&);
template<class C> bool transfer(C&, combat_map_identifier&);
template<class C> bool transfer(C&, combat_combat_payload&);
template<class C> bool transfer(C&, sc_diagnostic_request&);
template<class C> bool transfer(C&, sc_combat_presentation_request&);
template<class C> bool transfer(C&, combat_presentation_combat_presentation_payload&);
template<class C> bool transfer(C&, support_dump_support_dump_payload&);
template<class C> bool transfer(C&, sc_diagnostic_detail&);
template<class C> bool transfer(C&, diagnostic_detail_result&);
template<class C> bool transfer(C&, save_admission_result&);
template<class C> bool transfer(C&, campaign_row_base_payload&);
template<class C> bool transfer(C&, campaign_row_presentation_payload&);
template<class C> bool transfer(C&, read_envelope&);
template<class C> bool transfer(C&, command_reply&);
template<class C> bool transfer(C&, supported_command&);
template<class C> bool transfer(C&, hello_result&);
template<class C> bool transfer(C&, immutable_submission&);
template<class C> bool transfer(C&, command_response&);
template<class C> bool transfer(C&, wire_header_1&);
template<class C> bool transfer(C&, wire_header_2&);
template<class C> bool transfer(C&, inspect_payload&);
template<class C> bool transfer(C&, engine_payload&);
template<class C> bool transfer(C&, context_payload&);
template<class C> bool transfer(C&, native_payload&);
template<class C> bool transfer(C&, diagnostic_payload&);
template<class C> bool transfer(C&, diagnostic_detail_payload&);
template<class C> bool transfer(C&, save_payload&);
template<class C> bool transfer(C&, save_admission_payload&);
template<class C> bool transfer(C&, save_write_payload&);
template<class C> bool transfer(C&, save_backup_payload&);
template<class C> bool transfer(C&, save_installation_payload&);
template<class C> bool transfer(C&, weapon_points_payload&);
template<class C> bool transfer(C&, campaign_row_payload&);
template<class C> bool transfer(C&, campaign_commit_payload&);
template<class C> bool transfer(C&, campaign_payload&);
template<class C> bool transfer(C&, inventory_payload&);
template<class C> bool transfer(C&, arsenal_payload&);
template<class C> bool transfer(C&, runes_payload&);
template<class C> bool transfer(C&, special_payload&);
template<class C> bool transfer(C&, deathlink_payload&);
template<class C> bool transfer(C&, automap_payload&);
template<class C> bool transfer(C&, command_payload&);
template<class C> bool transfer(C&, combat_payload&);
template<class C> bool transfer(C&, combat_presentation_payload&);
template<class C> bool transfer(C&, support_dump_payload&);
template<class C> bool transfer(C&, campaign_row_payload_revision1&);
template<class C> bool transfer(C&, campaign_row_payload_revision2&);
}

namespace {
template<class T, class Key> bool unique_by(const T& values, Key key) {
    for (size_t i = 0; i < values.size(); ++i)
        for (size_t j = 0; j < i; ++j) if (key(values[i]) == key(values[j])) return false;
    return true;
}
template<size_t N> bool hex_namespace(const std::array<uint8_t, N>& value, bool allow_empty = false) {
    if (allow_empty && std::all_of(value.begin(), value.end(), [](auto c) { return c == 0; })) return true;
    static_assert(N == 64 || N == 65);
    if constexpr (N == 65) { if (value[64]) return false; }
    return std::all_of(value.begin(), value.begin() + 64, [](auto c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool sum_fits(uint64_t maximum, std::initializer_list<uint64_t> values) {
    uint64_t sum = 0;
    for (const auto value : values) { if (value > maximum - sum) return false; sum += value; }
    return true;
}
template<class T> bool text_valid(const T& text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = p[i++];
        if (cp < 32 || cp == 127) return false;
        if (cp < 128) continue;
        unsigned n = cp >= 0xc2 && cp <= 0xdf ? 1 : cp >= 0xe0 && cp <= 0xef ? 2 : cp >= 0xf0 && cp <= 0xf4 ? 3 : 0;
        if (!n || i + n > text.size()) return false;
        cp &= (1u << (6 - n)) - 1;
        for (unsigned j = 0; j < n; ++j) {
            const auto b = p[i++]; if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3f);
        }
        if (cp < (n == 1 ? 0x80u : n == 2 ? 0x800u : 0x10000u) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp <= 0x9f)) return false;
    }
    return true;
}
struct Writer {
    static constexpr bool reading = false;
    Bytes data;
    template<class T> bool item(T& value) {
        if constexpr (std::is_integral_v<T>) {
            auto n = static_cast<uint64_t>(value);
            if (data.size() + sizeof(T) > max_frame) return false;
            for (size_t i = 0; i < sizeof(T); ++i) { data.push_back(uint8_t(n)); n >>= 8; }
            return true;
        } else return transfer(*this, value);
    }
    template<class T, size_t N> bool sequence(std::array<T, N>& values) {
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    template<class T> bool sequence(std::vector<T>& values, size_t maximum) {
        if (values.size() > maximum) return false;
        auto count = static_cast<uint32_t>(values.size());
        if (!item(count)) return false;
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    bool text(std::string& value, size_t minimum, size_t maximum, size_t length) {
        if (value.size() < minimum || value.size() > maximum || !text_valid(value)) return false;
        if (length == SIZE_MAX) { auto count = static_cast<uint16_t>(value.size()); if (!item(count)) return false; }
        else if (length != value.size()) return false;
        if (data.size() + value.size() > max_frame) return false;
        data.insert(data.end(), value.begin(), value.end()); return true;
    }
};
struct Reader {
    static constexpr bool reading = true;
    const Bytes& data;
    size_t pos = 0;
    template<class T> bool item(T& value) {
        if constexpr (std::is_integral_v<T>) {
            if (sizeof(T) > data.size() - pos) return false;
            std::make_unsigned_t<T> n = 0;
            for (size_t i = 0; i < sizeof(T); ++i) n |= std::make_unsigned_t<T>(uint64_t(data[pos++]) << (i * 8));
            std::memcpy(&value, &n, sizeof(T)); return true;
        } else return transfer(*this, value);
    }
    template<class T, size_t N> bool sequence(std::array<T, N>& values) {
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    template<class T> bool sequence(std::vector<T>& values, size_t maximum) {
        uint32_t count = 0; if (!item(count) || count > maximum) return false;
        values.resize(count);
        for (auto& value : values) if (!item(value)) return false;
        return true;
    }
    bool text(std::string& value, size_t minimum, size_t maximum, size_t length) {
        if (length == SIZE_MAX) { uint16_t count = 0; if (!item(count)) return false; length = count; }
        if (length < minimum || length > maximum || length > data.size() - pos) return false;
        value.assign(reinterpret_cast<const char*>(data.data() + pos), length); pos += length;
        return text_valid(value);
    }
};
}

namespace {
template<class C> bool transfer(C& c, v1_command_submit& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 2097152ULL &&
        c.item(v.expected_pid) &&
        v.expected_pid >= 1ULL &&
        c.item(v.expected_process_created) &&
        v.expected_process_created >= 1ULL &&
        c.sequence(v.expected_instance_id) && std::any_of(v.expected_instance_id.begin(), v.expected_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.expected_generation) &&
        v.expected_generation >= 1ULL &&
        c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.sequence(v.nonce) && std::any_of(v.nonce.begin(), v.nonce.end(), [](auto item) { return item != 0; }) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL &&
        c.item(v.command_abi) &&
        v.command_abi == 1ULL &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.sequence(v.text);
}
template<class C> bool transfer(C& c, v1_command_result& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 2097152ULL &&
        c.item(v.identity_pid) &&
        c.item(v.identity_created) &&
        c.sequence(v.identity_instance) &&
        c.item(v.core_abi) &&
        v.core_abi == 1ULL &&
        c.text(v.product_version, 1, 64, SIZE_MAX) &&
        c.text(v.build_id, 1, 64, SIZE_MAX) &&
        c.item(v.domain_abi) &&
        v.domain_abi == 1ULL &&
        c.item(v.execution_state) &&
        c.item(v.execution_reason) &&
        c.item(v.execution_cancel_requested) &&
        c.item(v.execution_retrieved) &&
        c.item(v.execution_scope_pid) &&
        c.item(v.execution_scope_process_created) &&
        c.sequence(v.execution_scope_instance_id) &&
        c.item(v.execution_scope_generation) &&
        c.item(v.execution_request_id) &&
        c.sequence(v.execution_nonce) &&
        c.item(v.execution_admitted_ms) &&
        c.item(v.execution_deadline_ms) &&
        c.item(v.execution_claimed_ms) &&
        c.item(v.execution_observed_ms) &&
        c.item(v.execution_executed_ms) &&
        c.item(v.execution_completed_ms) &&
        c.item(v.execution_retrieved_ms) &&
        c.item(v.execution_thread) &&
        c.item(v.execution_site_revision) &&
        c.item(v.execution_phase) &&
        c.item(v.execution_lifecycle) &&
        c.item(v.execution_game_state) &&
        c.item(v.execution_map_validity) &&
        c.item(v.execution_map_reason) &&
        c.item(v.execution_map_error) &&
        c.item(v.execution_map_length) &&
        c.text(v.execution_map, 0, 255, v.execution_map_length) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.native_exception);
}
template<class C> bool transfer(C& c, v1_inventory_result& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 8192ULL &&
        c.item(v.identity_pid) &&
        c.item(v.identity_created) &&
        c.sequence(v.identity_instance) &&
        c.item(v.core_abi) &&
        v.core_abi == 1ULL &&
        c.text(v.product_version, 1, 64, SIZE_MAX) &&
        c.text(v.build_id, 1, 64, SIZE_MAX) &&
        c.item(v.domain_abi) &&
        v.domain_abi == 8ULL &&
        c.item(v.execution_state) &&
        c.item(v.execution_reason) &&
        c.item(v.execution_cancel_requested) &&
        c.item(v.execution_retrieved) &&
        c.item(v.execution_scope_pid) &&
        c.item(v.execution_scope_process_created) &&
        c.sequence(v.execution_scope_instance_id) &&
        c.item(v.execution_scope_generation) &&
        c.item(v.execution_request_id) &&
        c.sequence(v.execution_nonce) &&
        c.item(v.execution_admitted_ms) &&
        c.item(v.execution_deadline_ms) &&
        c.item(v.execution_claimed_ms) &&
        c.item(v.execution_observed_ms) &&
        c.item(v.execution_executed_ms) &&
        c.item(v.execution_completed_ms) &&
        c.item(v.execution_retrieved_ms) &&
        c.item(v.execution_thread) &&
        c.item(v.execution_site_revision) &&
        c.item(v.execution_phase) &&
        c.item(v.execution_lifecycle) &&
        c.item(v.execution_game_state) &&
        c.item(v.execution_map_validity) &&
        c.item(v.execution_map_reason) &&
        c.item(v.execution_map_error) &&
        c.item(v.execution_map_length) &&
        c.text(v.execution_map, 0, 255, v.execution_map_length) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.weapons_before) &&
        c.item(v.weapons_after) &&
        c.item(v.equipment_before) &&
        c.item(v.equipment_after) &&
        c.item(v.special_before) &&
        c.item(v.special_after) &&
        c.item(v.upgrades_before) &&
        c.item(v.upgrades_after) &&
        c.item(v.health_before) &&
        c.item(v.health_after) &&
        c.item(v.armor_before) &&
        c.item(v.armor_after) &&
        c.item(v.ammo_before) &&
        c.item(v.ammo_after) &&
        c.item(v.reserved_before) &&
        c.item(v.reserved_after) &&
        c.item(v.operations_applied) &&
        c.item(v.ice_before) &&
        c.item(v.ice_after) &&
        c.item(v.dash_before) &&
        c.item(v.dash_after) &&
        c.item(v.key_before) &&
        c.item(v.key_after) &&
        c.item(v.frag_before) &&
        c.item(v.frag_after) &&
        c.item(v.flame_before) &&
        c.item(v.flame_after) &&
        c.item(v.punch_before) &&
        c.item(v.punch_after) &&
        c.item(v.perks_known) &&
        c.item(v.perks_owned);
}
template<class C> bool transfer(C& c, v1_inspect_request& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        (uint64_t(v.capability) & ~1ULL) == 0;
}
template<class C> bool transfer(C& c, v1_diagnostic_detail_submit& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 32ULL &&
        c.item(v.expected_pid) &&
        v.expected_pid >= 1ULL &&
        c.item(v.expected_process_created) &&
        v.expected_process_created >= 1ULL &&
        c.sequence(v.expected_instance_id) && std::any_of(v.expected_instance_id.begin(), v.expected_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.expected_generation) &&
        v.expected_generation >= 1ULL &&
        c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.sequence(v.nonce) && std::any_of(v.nonce.begin(), v.nonce.end(), [](auto item) { return item != 0; }) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL;
}
template<class C> bool transfer(C& c, campaign_reward& v) {
    (void)c; (void)v;
    return c.item(v.location_id) &&
        c.item(v.kind) &&
        c.item(v.checked) &&
        c.sequence(v.unlockable) &&
        c.sequence(v.name) &&
        c.sequence(v.text);
}
template<class C> bool transfer(C& c, v1_campaign_rewards_row& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 1048576ULL &&
        c.item(v.expected_pid) &&
        v.expected_pid >= 1ULL &&
        c.item(v.expected_process_created) &&
        v.expected_process_created >= 1ULL &&
        c.sequence(v.expected_instance_id) && std::any_of(v.expected_instance_id.begin(), v.expected_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.expected_generation) &&
        v.expected_generation >= 0ULL &&
        c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.sequence(v.nonce) && std::any_of(v.nonce.begin(), v.nonce.end(), [](auto item) { return item != 0; }) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.revision) &&
        c.item(v.index) &&
        c.item(v.count) &&
        c.item(v.row_id) &&
        c.item(v.row_flags) &&
        c.item(v.native_index) &&
        c.sequence(v.map) &&
        c.sequence(v.title) &&
        c.item(v.known) &&
        c.item(v.found) &&
        c.item(v.total) &&
        c.item(v.band) &&
        c.sequence(v.challenge_0_unlockable) &&
        c.item(v.challenge_0_found) &&
        c.item(v.challenge_0_required) &&
        c.item(v.challenge_0_checked) &&
        c.sequence(v.challenge_1_unlockable) &&
        c.item(v.challenge_1_found) &&
        c.item(v.challenge_1_required) &&
        c.item(v.challenge_1_checked) &&
        c.sequence(v.challenge_2_unlockable) &&
        c.item(v.challenge_2_found) &&
        c.item(v.challenge_2_required) &&
        c.item(v.challenge_2_checked) &&
        c.item(v.rewards_known) &&
        c.sequence(v.rewards, 17) &&
        unique_by(v.rewards, [](const auto& item) { return item.location_id; }) &&
        unique_by(v.rewards, [](const auto& item) { return item.unlockable; });
}
template<class C> bool transfer(C& c, v1_automap_publish& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 262144ULL &&
        c.item(v.expected_pid) &&
        v.expected_pid >= 1ULL &&
        c.item(v.expected_process_created) &&
        v.expected_process_created >= 1ULL &&
        c.sequence(v.expected_instance_id) && std::any_of(v.expected_instance_id.begin(), v.expected_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.expected_generation) &&
        v.expected_generation >= 1ULL &&
        c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.sequence(v.nonce) && std::any_of(v.nonce.begin(), v.nonce.end(), [](auto item) { return item != 0; }) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.known) &&
        c.item(v.revision) &&
        c.item(v.checked_0) &&
        c.item(v.checked_1) &&
        c.item(v.checked_2) &&
        c.item(v.checked_3) &&
        c.item(v.checked_4) &&
        c.item(v.checked_5) &&
        c.item(v.checked_6) &&
        c.item(v.checked_7) &&
        c.item(v.checked_8);
}
template<class C> bool transfer(C& c, v1_campaign_base& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 4096ULL &&
        c.item(v.expected_pid) &&
        v.expected_pid >= 1ULL &&
        c.item(v.expected_process_created) &&
        v.expected_process_created >= 1ULL &&
        c.sequence(v.expected_instance_id) && std::any_of(v.expected_instance_id.begin(), v.expected_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.expected_generation) &&
        v.expected_generation >= 0ULL &&
        c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.sequence(v.nonce) && std::any_of(v.nonce.begin(), v.nonce.end(), [](auto item) { return item != 0; }) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.revision) &&
        c.item(v.index) &&
        c.item(v.count) &&
        c.item(v.row_id) &&
        c.item(v.row_flags) &&
        c.item(v.native_index) &&
        c.sequence(v.map) &&
        c.sequence(v.title);
}
template<class C> bool transfer(C& c, v1_campaign_presentation& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 524288ULL &&
        c.item(v.expected_pid) &&
        v.expected_pid >= 1ULL &&
        c.item(v.expected_process_created) &&
        v.expected_process_created >= 1ULL &&
        c.sequence(v.expected_instance_id) && std::any_of(v.expected_instance_id.begin(), v.expected_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.expected_generation) &&
        v.expected_generation >= 0ULL &&
        c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.sequence(v.nonce) && std::any_of(v.nonce.begin(), v.nonce.end(), [](auto item) { return item != 0; }) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.revision) &&
        c.item(v.index) &&
        c.item(v.count) &&
        c.item(v.row_id) &&
        c.item(v.row_flags) &&
        c.item(v.native_index) &&
        c.sequence(v.map) &&
        c.sequence(v.title) &&
        c.item(v.known) &&
        c.item(v.found) &&
        c.item(v.total) &&
        c.item(v.band) &&
        c.sequence(v.challenge_0_unlockable) &&
        c.item(v.challenge_0_found) &&
        c.item(v.challenge_0_required) &&
        c.item(v.challenge_0_checked) &&
        c.sequence(v.challenge_1_unlockable) &&
        c.item(v.challenge_1_found) &&
        c.item(v.challenge_1_required) &&
        c.item(v.challenge_1_checked) &&
        c.sequence(v.challenge_2_unlockable) &&
        c.item(v.challenge_2_found) &&
        c.item(v.challenge_2_required) &&
        c.item(v.challenge_2_checked);
}
template<class C> bool transfer(C& c, v1_runes_request& v) {
    (void)c; (void)v;
    return c.item(v.capability) &&
        v.capability == 32768ULL &&
        c.item(v.expected_pid) &&
        v.expected_pid >= 1ULL &&
        c.item(v.expected_process_created) &&
        v.expected_process_created >= 1ULL &&
        c.sequence(v.expected_instance_id) && std::any_of(v.expected_instance_id.begin(), v.expected_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.expected_generation) &&
        v.expected_generation >= 1ULL &&
        c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.sequence(v.nonce) && std::any_of(v.nonce.begin(), v.nonce.end(), [](auto item) { return item != 0; }) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        (v.kind == 0 || v.kind == 1 || v.kind == 2 || v.kind == 3 || v.kind == 4) &&
        c.item(v.normal_runes) &&
        (uint64_t(v.normal_runes) & ~511ULL) == 0 &&
        c.item(v.support_runes) &&
        (uint64_t(v.support_runes) & ~7ULL) == 0 &&
        c.item(v.select_slot) &&
        v.select_slot <= 3ULL &&
        c.item(v.select_rune) &&
        v.select_rune >= -1 &&
        v.select_rune <= 8 &&
        c.sequence(v.reserved) && std::all_of(v.reserved.begin(), v.reserved.end(), [](auto item) { return item == 0ULL; });
}
template<class C> bool transfer(C& c, scope& v) {
    (void)c; (void)v;
    return c.item(v.pid) &&
        v.pid >= 1ULL &&
        c.item(v.process_created) &&
        v.process_created >= 1ULL &&
        c.sequence(v.instance_id) && std::any_of(v.instance_id.begin(), v.instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.generation);
}
template<class C> bool transfer(C& c, command_reference& v) {
    (void)c; (void)v;
    return c.item(v.scope) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.text(v.command_id, 1, 128, SIZE_MAX) &&
        c.item(v.command_type) &&
        v.command_type >= 1ULL &&
        c.item(v.payload_revision) &&
        v.payload_revision >= 1ULL &&
        c.sequence(v.fingerprint);
}
template<class C> bool transfer(C& c, submission_identity& v) {
    (void)c; (void)v;
    return c.item(v.scope) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.text(v.command_id, 1, 128, SIZE_MAX) &&
        c.item(v.command_type) &&
        v.command_type >= 1ULL &&
        c.item(v.payload_revision) &&
        v.payload_revision >= 1ULL;
}
template<class C> bool transfer(C& c, submission& v) {
    (void)c; (void)v;
    return c.item(v.reference) &&
        c.sequence(v.save_binding) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL;
}
template<class C> bool transfer(C& c, ack& v) {
    (void)c; (void)v;
    return c.item(v.reference) &&
        c.sequence(v.result_token);
}
template<class C> bool transfer(C& c, result_dimensions& v) {
    (void)c; (void)v;
    return c.item(v.progress) &&
        (v.progress == 0 || v.progress == 1 || v.progress == 2 || v.progress == 3) &&
        c.item(v.execution) &&
        (v.execution == 0 || v.execution == 1 || v.execution == 2) &&
        c.item(v.effect) &&
        (v.effect == 0 || v.effect == 1 || v.effect == 2 || v.effect == 3 || v.effect == 4) &&
        c.item(v.retention) &&
        (v.retention == 0 || v.retention == 1 || v.retention == 2) &&
        c.sequence(v.result_token);
}
template<class C> bool transfer(C& c, sc_status& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.capabilities) &&
        c.item(v.state) &&
        c.item(v.last_result) &&
        c.item(v.initialization_count) &&
        c.item(v.diagnostic_count) &&
        c.sequence(v.version) &&
        c.sequence(v.build_id);
}
template<class C> bool transfer(C& c, sc_engine_field& v) {
    (void)c; (void)v;
    return c.item(v.validity) &&
        c.item(v.reason) &&
        c.item(v.value) &&
        c.item(v.win32_error);
}
template<class C> bool transfer(C& c, sc_engine_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.sequence) &&
        c.item(v.sampled_at_ms) &&
        c.item(v.duration_ms) &&
        c.item(v.sample_reason) &&
        c.item(v.pe_reason) &&
        c.item(v.machine) &&
        c.item(v.timestamp) &&
        c.item(v.image_size) &&
        c.item(v.entry_rva) &&
        c.item(v.disk_hash_reason) &&
        c.sequence(v.disk_sha256) &&
        c.item(v.profile) &&
        c.item(v.locator_revision) &&
        c.item(v.root_locator_reason) &&
        c.item(v.root_signature_rva) &&
        c.item(v.root_target_rva) &&
        c.item(v.profile_runtime_validated) &&
        c.sequence(v.fields);
}
template<class C> bool transfer(C& c, sc_context_map& v) {
    (void)c; (void)v;
    return c.item(v.validity) &&
        c.item(v.reason) &&
        c.item(v.win32_error) &&
        c.item(v.length) &&
        c.sequence(v.bytes);
}
template<class C> bool transfer(C& c, sc_context_field& v) {
    (void)c; (void)v;
    return c.item(v.validity) &&
        c.item(v.reason) &&
        c.item(v.value) &&
        c.item(v.win32_error);
}
template<class C> bool transfer(C& c, sc_context_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.pid) &&
        c.item(v.process_created) &&
        c.sequence(v.instance) &&
        c.item(v.sequence) &&
        c.item(v.sampled_at_ms) &&
        c.item(v.duration_ms) &&
        c.item(v.sample_reason) &&
        c.item(v.profile) &&
        c.item(v.locator_revision) &&
        c.item(v.layout_revision) &&
        c.item(v.root_locator_reason) &&
        c.item(v.disk_hash_reason) &&
        c.sequence(v.disk_sha256) &&
        c.item(v.current_map) &&
        c.sequence(v.fields);
}
template<class C> bool transfer(C& c, sc_native_scope& v) {
    (void)c; (void)v;
    return c.item(v.pid) &&
        c.item(v.process_created) &&
        c.sequence(v.instance_id) &&
        c.item(v.lifecycle_generation);
}
template<class C> bool transfer(C& c, sc_native_event& v) {
    (void)c; (void)v;
    return c.item(v.sequence) &&
        c.item(v.generation) &&
        c.item(v.at_ms) &&
        c.item(v.kind) &&
        c.item(v.lifecycle) &&
        c.item(v.thread_id) &&
        c.item(v.depth);
}
template<class C> bool transfer(C& c, sc_native_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.scope) &&
        c.item(v.availability) &&
        c.item(v.reason) &&
        c.item(v.site_revision) &&
        c.item(v.site_rva) &&
        c.item(v.phase) &&
        c.item(v.installed_hooks) &&
        c.sequence(v.validator_reasons) &&
        c.item(v.retained_module) &&
        c.item(v.coverage) &&
        c.item(v.lifecycle) &&
        c.item(v.depth) &&
        c.item(v.checkpoint_flag_known) &&
        c.item(v.checkpoint_flag) &&
        c.item(v.event_sequence) &&
        c.item(v.event_gap_count) &&
        c.item(v.history_oldest) &&
        c.item(v.history_overwritten) &&
        c.item(v.callback_sequence) &&
        c.item(v.callback_at_ms) &&
        c.item(v.callback_thread_id) &&
        c.item(v.native_owner_thread_id) &&
        c.item(v.context_generation) &&
        c.item(v.context_sampled_at_ms) &&
        c.item(v.context_reason) &&
        c.item(v.game_state) &&
        c.item(v.current_map) &&
        c.item(v.queued) &&
        c.item(v.claimed) &&
        c.item(v.retained_results) &&
        c.item(v.history_gap) &&
        c.item(v.event_count) &&
        c.sequence(v.events);
}
template<class C> bool transfer(C& c, sc_diagnostic_result& v) {
    (void)c; (void)v;
    return c.item(v.state) &&
        c.item(v.reason) &&
        c.item(v.cancel_requested) &&
        c.item(v.retrieved) &&
        c.item(v.scope) &&
        c.item(v.request_id) &&
        c.sequence(v.nonce) &&
        c.item(v.admitted_at_ms) &&
        c.item(v.deadline_at_ms) &&
        c.item(v.claimed_at_ms) &&
        c.item(v.observed_at_ms) &&
        c.item(v.executed_at_ms) &&
        c.item(v.completed_at_ms) &&
        c.item(v.retrieved_at_ms) &&
        c.item(v.thread_id) &&
        c.item(v.site_revision) &&
        c.item(v.phase) &&
        c.item(v.lifecycle) &&
        c.item(v.game_state) &&
        c.item(v.current_map);
}
template<class C> bool transfer(C& c, sc_save_field& v) {
    (void)c; (void)v;
    return c.item(v.validity) &&
        c.item(v.reason) &&
        c.item(v.value) &&
        c.item(v.win32_error) &&
        c.item(v.reserved);
}
template<class C> bool transfer(C& c, sc_save_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.pid) &&
        c.item(v.profile) &&
        c.item(v.process_created) &&
        c.sequence(v.instance) &&
        c.item(v.sequence) &&
        c.item(v.sampled_at_ms) &&
        c.item(v.duration_ms) &&
        c.item(v.sample_reason) &&
        c.item(v.layout_revision) &&
        c.item(v.root_locator_reason) &&
        c.item(v.mutation_available) &&
        c.item(v.mutation_reason) &&
        c.sequence(v.fields);
}
template<class C> bool transfer(C& c, sc_save_admission_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.state) &&
        c.item(v.fault) &&
        c.item(v.prepared_routes) &&
        c.item(v.required_routes) &&
        c.item(v.flags) &&
        c.item(v.reserved) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id, true) &&
        c.sequence(v.native_root) &&
        c.sequence(v.reserved_bytes);
}
template<class C> bool transfer(C& c, sc_save_write_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.state) &&
        c.item(v.flags) &&
        c.item(v.operation_id) &&
        c.item(v.sdk_sequence) &&
        c.item(v.file_count) &&
        c.item(v.submitted) &&
        c.item(v.completed) &&
        c.item(v.pending_handles) &&
        c.item(v.preparation_jobs) &&
        c.item(v.preflight_jobs) &&
        c.item(v.native_value) &&
        c.item(v.reserved) &&
        c.item(v.native_state) &&
        c.item(v.native_outcome) &&
        c.sequence(v.directory) &&
        c.sequence(v.reserved_bytes);
}
template<class C> bool transfer(C& c, sc_save_backup_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.state) &&
        c.item(v.failure) &&
        c.item(v.execution) &&
        c.item(v.operation_id) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.storage_outcome) &&
        c.item(v.storage_error) &&
        c.item(v.files) &&
        c.item(v.bytes) &&
        c.sequence(v.basename) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.campaign) &&
        c.item(v.slot);
}
template<class C> bool transfer(C& c, sc_install_event& v) {
    (void)c; (void)v;
    return c.item(v.sequence) &&
        c.item(v.at_ms) &&
        c.item(v.duration_ms) &&
        c.item(v.stage) &&
        c.item(v.target_group) &&
        c.item(v.target_index) &&
        c.item(v.rva) &&
        c.item(v.signature_offset) &&
        c.item(v.result) &&
        c.item(v.reason) &&
        c.item(v.read_reason) &&
        c.item(v.win32_error) &&
        c.item(v.minhook_status) &&
        c.item(v.byte_count) &&
        c.item(v.collision_rva) &&
        c.item(v.byte_window_offset) &&
        c.sequence(v.expected_bytes) &&
        c.sequence(v.actual_bytes);
}
template<class C> bool transfer(C& c, sc_save_installation_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.attempt) &&
        c.item(v.phase) &&
        c.item(v.last_completed_stage) &&
        c.item(v.startup_observation) &&
        c.item(v.validated) &&
        c.item(v.created) &&
        c.item(v.enabled) &&
        c.item(v.cleanup_failures) &&
        c.item(v.gaps) &&
        c.item(v.reserved) &&
        c.item(v.sequence) &&
        c.item(v.active) &&
        c.item(v.primary_failure) &&
        c.item(v.cleanup_failure);
}
template<class C> bool transfer(C& c, sc_weapon_points_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.amount) &&
        c.item(v.expected_gained) &&
        c.item(v.outcome) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.balance_before) &&
        c.item(v.balance_after) &&
        c.item(v.gained_before) &&
        c.item(v.gained_after) &&
        c.item(v.suppressed_grants);
}
template<class C> bool transfer(C& c, sc_campaign_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.scope) &&
        c.item(v.request_id) &&
        c.sequence(v.nonce) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.status) &&
        c.item(v.reason) &&
        c.item(v.committed_revision) &&
        c.item(v.rendered_revision) &&
        c.item(v.selected_id) &&
        c.item(v.loaded_id) &&
        c.item(v.intent_count) &&
        c.sequence(v.intent_ids);
}
template<class C> bool transfer(C& c, sc_inventory_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.weapons_before) &&
        c.item(v.weapons_after) &&
        c.item(v.equipment_before) &&
        c.item(v.equipment_after) &&
        c.item(v.special_before) &&
        c.item(v.special_after) &&
        c.item(v.upgrades_before) &&
        c.item(v.upgrades_after) &&
        c.item(v.health_tier_before) &&
        c.item(v.health_tier_after) &&
        c.item(v.armor_tier_before) &&
        c.item(v.armor_tier_after) &&
        c.item(v.ammo_tier_before) &&
        c.item(v.ammo_tier_after) &&
        c.item(v.reserved_before) &&
        c.item(v.reserved_after) &&
        c.item(v.operations_applied) &&
        c.item(v.ice_bomb_before) &&
        c.item(v.ice_bomb_after) &&
        c.item(v.dash_before) &&
        c.item(v.dash_after) &&
        c.item(v.slayer_key_before) &&
        c.item(v.slayer_key_after) &&
        c.item(v.frag_grenade_before) &&
        c.item(v.frag_grenade_after) &&
        c.item(v.flame_belch_before) &&
        c.item(v.flame_belch_after) &&
        c.item(v.blood_punch_before) &&
        c.item(v.blood_punch_after) &&
        c.item(v.perks_known) &&
        c.item(v.perks_owned);
}
template<class C> bool transfer(C& c, sc_arsenal_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.weapons_before) &&
        c.item(v.weapons_after) &&
        c.item(v.mods_before) &&
        c.item(v.mods_after) &&
        c.sequence(v.selected_mods_before) &&
        c.sequence(v.selected_mods_after) &&
        c.item(v.normal_upgrades_before) &&
        c.item(v.normal_upgrades_after) &&
        c.item(v.masteries_ap_before) &&
        c.item(v.masteries_ap_after) &&
        c.item(v.mastery_challenges_active_before) &&
        c.item(v.mastery_challenges_active_after) &&
        c.item(v.mastery_challenges_completed_before) &&
        c.item(v.mastery_challenges_completed_after) &&
        c.item(v.masteries_effective_before) &&
        c.item(v.masteries_effective_after) &&
        c.item(v.mission_challenges_active_before) &&
        c.item(v.mission_challenges_active_after) &&
        c.item(v.mission_challenges_completed_before) &&
        c.item(v.mission_challenges_completed_after) &&
        c.item(v.operations_applied);
}
template<class C> bool transfer(C& c, sc_runes_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.owned_normal_before) &&
        c.item(v.owned_normal_after) &&
        c.item(v.owned_support_before) &&
        c.item(v.owned_support_after) &&
        c.sequence(v.selected_slots_before) &&
        c.sequence(v.selected_slots_after) &&
        c.item(v.selected_support_before) &&
        c.item(v.selected_support_after) &&
        c.item(v.unlocked_slots_before) &&
        c.item(v.unlocked_slots_after) &&
        c.item(v.derived_pairs_before) &&
        c.item(v.derived_pairs_after) &&
        c.item(v.reserved_before) &&
        c.item(v.reserved_after) &&
        c.item(v.operations_applied);
}
template<class C> bool transfer(C& c, sc_special_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.owns_crucible) &&
        c.item(v.owns_hammer) &&
        c.item(v.hammer_tier) &&
        c.item(v.selected) &&
        c.item(v.native_crucible) &&
        c.item(v.native_hammer) &&
        c.item(v.native_hammer_perks) &&
        c.item(v.native_selected) &&
        c.item(v.native_state_known) &&
        c.item(v.crucible_charge) &&
        c.item(v.crucible_charge_max) &&
        c.item(v.refill_balance) &&
        c.item(v.refill_flags) &&
        c.item(v.refill_request_id) &&
        c.item(v.refill_request_state) &&
        c.item(v.refill_executed) &&
        c.item(v.refill_execution_id) &&
        c.item(v.refill_sequence) &&
        c.item(v.reserved0) &&
        c.item(v.operations_applied);
}
template<class C> bool transfer(C& c, sc_deathlink_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.flags) &&
        c.item(v.native_exception) &&
        c.item(v.enabled) &&
        c.item(v.mode) &&
        c.item(v.remote_state) &&
        c.item(v.remote_protection) &&
        c.item(v.remote_attempts) &&
        c.item(v.pending_count) &&
        c.item(v.remote_event_id) &&
        c.item(v.local_candidates) &&
        c.item(v.local_suppressed) &&
        c.item(v.local_state) &&
        c.item(v.local_death_sequence) &&
        c.item(v.local_death_time_ms) &&
        c.item(v.local_cause) &&
        c.item(v.local_protection) &&
        c.item(v.local_flags) &&
        c.item(v.reserved0) &&
        c.item(v.operations_applied);
}
template<class C> bool transfer(C& c, sc_automap_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.scope) &&
        c.item(v.request_id) &&
        c.sequence(v.nonce) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.known) &&
        c.item(v.native_fault) &&
        c.item(v.revision) &&
        c.item(v.scanned) &&
        c.item(v.collected) &&
        c.item(v.completed_passes);
}
template<class C> bool transfer(C& c, sc_command_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.kind) &&
        c.item(v.outcome) &&
        c.item(v.native_exception);
}
template<class C> bool transfer(C& c, sc_combat_snapshot& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.scope) &&
        c.item(v.availability) &&
        c.item(v.reason) &&
        c.item(v.observation_gaps) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.qualified_kills);
}
template<class C> bool transfer(C& c, sc_combat_presentation_result& v) {
    (void)c; (void)v;
    return c.item(v.abi_version) &&
        c.item(v.scope) &&
        c.item(v.request_id) &&
        c.sequence(v.nonce) &&
        c.item(v.status) &&
        c.item(v.reason) &&
        c.item(v.total) &&
        c.item(v.target) &&
        c.item(v.milestone) &&
        c.item(v.visible);
}
template<class C> bool transfer(C& c, inspect_inspect_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, engine_engine_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, context_context_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, native_native_payload& v) {
    (void)c; (void)v;
    return c.item(v.after_event);
}
template<class C> bool transfer(C& c, diagnostic_diagnostic_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, diagnostic_detail_diagnostic_detail_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, save_save_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, save_admission_save_admission_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, save_write_save_write_payload& v) {
    (void)c; (void)v;
    return c.item(v.operation_id) &&
        v.operation_id >= 1ULL;
}
template<class C> bool transfer(C& c, save_backup_save_backup_payload& v) {
    (void)c; (void)v;
    return c.item(v.campaign) &&
        v.campaign <= 2ULL &&
        c.item(v.slot) &&
        v.slot <= 11ULL &&
        c.item(v.work_deadline_ms) &&
        v.work_deadline_ms >= 1ULL &&
        v.work_deadline_ms <= 60000ULL;
}
template<class C> bool transfer(C& c, save_installation_save_installation_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, weapon_points_observe_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, weapon_points_grant_payload& v) {
    (void)c; (void)v;
    return c.item(v.amount) &&
        v.amount >= 1ULL &&
        v.amount <= 117ULL &&
        c.item(v.expected_gained) &&
        v.expected_gained <= 2147483647ULL &&
        sum_fits(2147483647ULL, {uint64_t(v.amount), uint64_t(v.expected_gained)});
}
template<class C> bool transfer(C& c, sc_campaign_row& v) {
    (void)c; (void)v;
    return c.item(v.id) &&
        c.item(v.flags) &&
        c.item(v.native_index) &&
        c.sequence(v.map) &&
        c.sequence(v.title);
}
template<class C> bool transfer(C& c, sc_physical_challenge& v) {
    (void)c; (void)v;
    return c.sequence(v.unlockable) &&
        c.item(v.found) &&
        c.item(v.required) &&
        c.item(v.checked);
}
template<class C> bool transfer(C& c, sc_campaign_summary& v) {
    (void)c; (void)v;
    return c.item(v.known) &&
        c.item(v.found) &&
        c.item(v.total) &&
        c.item(v.band) &&
        c.sequence(v.challenges);
}
template<class C> bool transfer(C& c, campaign_row_campaign_row_payload& v) {
    (void)c; (void)v;
    return c.item(v.revision) &&
        c.item(v.index) &&
        v.index <= 20ULL &&
        c.item(v.count) &&
        v.count >= 1ULL &&
        v.count <= 21ULL &&
        c.item(v.row) &&
        c.item(v.summary) &&
        c.sequence(v.rewards, 17) &&
        unique_by(v.rewards, [](const auto& item) { return item.location_id; }) &&
        unique_by(v.rewards, [](const auto& item) { return item.unlockable; });
}
template<class C> bool transfer(C& c, campaign_commit_campaign_commit_payload& v) {
    (void)c; (void)v;
    return c.item(v.revision);
}
template<class C> bool transfer(C& c, campaign_campaign_payload& v) {
    (void)c; (void)v;
    return c.item(v.revision);
}
template<class C> bool transfer(C& c, inventory_observe_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, inventory_ensure_payload& v) {
    (void)c; (void)v;
    return c.item(v.weapons) &&
        (uint64_t(v.weapons) & ~1023ULL) == 0 &&
        c.item(v.equipment) &&
        (uint64_t(v.equipment) & ~31ULL) == 0;
}
template<class C> bool transfer(C& c, inventory_set_capacity_payload& v) {
    (void)c; (void)v;
    return c.item(v.health_tier) &&
        v.health_tier <= 4ULL &&
        c.item(v.armor_tier) &&
        v.armor_tier <= 4ULL &&
        c.item(v.ammo_tier) &&
        v.ammo_tier <= 4ULL;
}
template<class C> bool transfer(C& c, arsenal_observe_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, arsenal_ensure_mods_payload& v) {
    (void)c; (void)v;
    return c.item(v.mods) &&
        (uint64_t(v.mods) & ~8191ULL) == 0;
}
template<class C> bool transfer(C& c, arsenal_select_mod_payload& v) {
    (void)c; (void)v;
    return c.item(v.weapon) &&
        v.weapon <= 6ULL &&
        (v.weapon == 0 || v.weapon == 2 || v.weapon == 3 || v.weapon == 4 || v.weapon == 5 || v.weapon == 6) &&
        c.item(v.mod) &&
        v.mod <= 2ULL;
}
template<class C> bool transfer(C& c, arsenal_project_mastery_payload& v) {
    (void)c; (void)v;
    return c.item(v.masteries) &&
        (uint64_t(v.masteries) & ~8191ULL) == 0;
}
template<class C> bool transfer(C& c, runes_observe_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, runes_ensure_normal_payload& v) {
    (void)c; (void)v;
    return c.item(v.normal_runes) &&
        (uint64_t(v.normal_runes) & ~511ULL) == 0;
}
template<class C> bool transfer(C& c, runes_ensure_support_payload& v) {
    (void)c; (void)v;
    return c.item(v.support_runes) &&
        (uint64_t(v.support_runes) & ~7ULL) == 0;
}
template<class C> bool transfer(C& c, runes_select_normal_payload& v) {
    (void)c; (void)v;
    return c.item(v.slot) &&
        v.slot <= 2ULL &&
        c.item(v.rune) &&
        v.rune >= -1 &&
        v.rune <= 8;
}
template<class C> bool transfer(C& c, runes_select_support_payload& v) {
    (void)c; (void)v;
    return c.item(v.rune) &&
        v.rune >= -1 &&
        v.rune <= 2;
}
template<class C> bool transfer(C& c, special_observe_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, special_ensure_ownership_payload& v) {
    (void)c; (void)v;
    return c.item(v.crucible) &&
        v.crucible <= 1ULL &&
        c.item(v.hammer) &&
        v.hammer <= 1ULL &&
        c.item(v.hammer_tier) &&
        v.hammer_tier <= 2ULL;
}
template<class C> bool transfer(C& c, special_select_payload& v) {
    (void)c; (void)v;
    return c.item(v.selected) &&
        v.selected >= 1ULL &&
        v.selected <= 2ULL;
}
template<class C> bool transfer(C& c, special_refill_status_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, special_refill_publish_payload& v) {
    (void)c; (void)v;
    return c.item(v.balance) &&
        v.balance <= 1000ULL &&
        c.item(v.flags) &&
        (uint64_t(v.flags) & ~7ULL) == 0;
}
template<class C> bool transfer(C& c, special_refill_take_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, special_refill_execute_payload& v) {
    (void)c; (void)v;
    return c.item(v.request_id) &&
        v.request_id >= 1ULL &&
        c.item(v.authorize) &&
        (uint64_t(v.authorize) & ~1ULL) == 0;
}
template<class C> bool transfer(C& c, deathlink_observe_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, deathlink_configure_payload& v) {
    (void)c; (void)v;
    return c.item(v.enabled) &&
        v.enabled <= 1ULL &&
        c.item(v.mode) &&
        (v.mode == 0 || v.mode == 1);
}
template<class C> bool transfer(C& c, deathlink_apply_remote_payload& v) {
    (void)c; (void)v;
    return c.item(v.event_id) &&
        v.event_id >= 1ULL &&
        c.sequence(v.event_hash);
}
template<class C> bool transfer(C& c, deathlink_poll_local_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, deathlink_ack_local_payload& v) {
    (void)c; (void)v;
    return c.item(v.sequence) &&
        v.sequence >= 1ULL;
}
template<class C> bool transfer(C& c, deathlink_cancel_remote_payload& v) {
    (void)c; (void)v;
    return c.item(v.event_id) &&
        v.event_id >= 1ULL;
}
template<class C> bool transfer(C& c, automap_publish_payload& v) {
    (void)c; (void)v;
    return c.item(v.known) &&
        v.known <= 1ULL &&
        c.item(v.revision) &&
        v.revision >= 1ULL &&
        c.sequence(v.checked_locations);
}
template<class C> bool transfer(C& c, automap_observe_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, command_activate_payload& v) {
    (void)c; (void)v;
    return c.text(v.entity, 1, 255, SIZE_MAX);
}
template<class C> bool transfer(C& c, command_player_perk_payload& v) {
    (void)c; (void)v;
    return c.text(v.perk, 1, 255, SIZE_MAX);
}
template<class C> bool transfer(C& c, command_ammo_refill_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, command_crucible_refill_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, command_echo_payload& v) {
    (void)c; (void)v;
    return c.text(v.text, 1, 2042, SIZE_MAX);
}
template<class C> bool transfer(C& c, command_condump_payload& v) {
    (void)c; (void)v;
    return c.text(v.filename, 8, 128, SIZE_MAX);
}
template<class C> bool transfer(C& c, command_list_inventory_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, command_transient_policy_payload& v) {
    (void)c; (void)v;
    return c.item(v.variable) &&
        (v.variable == 0 || v.variable == 1 || v.variable == 2) &&
        c.text(v.value, 1, 32, SIZE_MAX);
}
template<class C> bool transfer(C& c, combat_map_identifier& v) {
    (void)c; (void)v;
    return c.text(v.value, 1, 255, SIZE_MAX);
}
template<class C> bool transfer(C& c, combat_combat_payload& v) {
    (void)c; (void)v;
    return c.sequence(v.maps, 64);
}
template<class C> bool transfer(C& c, sc_diagnostic_request& v) {
    (void)c; (void)v;
    return c.item(v.expected) &&
        c.item(v.request_id) &&
        c.sequence(v.nonce) &&
        c.item(v.deadline_ms);
}
template<class C> bool transfer(C& c, sc_combat_presentation_request& v) {
    (void)c; (void)v;
    return c.item(v.execution) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.total) &&
        c.item(v.target) &&
        c.item(v.first_milestone) &&
        c.item(v.milestone) &&
        c.item(v.milestone_step) &&
        c.item(v.visible);
}
template<class C> bool transfer(C& c, combat_presentation_combat_presentation_payload& v) {
    (void)c; (void)v;
    return c.item(v.presentation);
}
template<class C> bool transfer(C& c, support_dump_support_dump_payload& v) {
    (void)c; (void)v;
    return true;
}
template<class C> bool transfer(C& c, sc_diagnostic_detail& v) {
    (void)c; (void)v;
    return c.item(v.revision) &&
        c.item(v.stage) &&
        c.item(v.observation_attempted) &&
        c.item(v.observation_accepted) &&
        c.item(v.timing_valid) &&
        c.item(v.timing_error) &&
        c.item(v.claim_lock_missed) &&
        c.item(v.observation_started_at_ms) &&
        c.item(v.observation_elapsed_ns) &&
        c.item(v.observation_budget_ns) &&
        c.item(v.sample_reason) &&
        c.item(v.state_validity) &&
        c.item(v.state_reason) &&
        c.item(v.state_error) &&
        c.item(v.map_validity) &&
        c.item(v.map_reason) &&
        c.item(v.map_error) &&
        c.item(v.pending_before) &&
        c.item(v.pending_before_reason) &&
        c.item(v.pending_before_error) &&
        c.item(v.pending_after) &&
        c.item(v.pending_after_reason) &&
        c.item(v.pending_after_error);
}
template<class C> bool transfer(C& c, diagnostic_detail_result& v) {
    (void)c; (void)v;
    return c.item(v.execution) &&
        c.item(v.detail);
}
template<class C> bool transfer(C& c, save_admission_result& v) {
    (void)c; (void)v;
    return c.item(v.observation) &&
        c.sequence(v.save_binding);
}
template<class C> bool transfer(C& c, campaign_row_base_payload& v) {
    (void)c; (void)v;
    return c.item(v.revision) &&
        c.item(v.index) &&
        v.index <= 20ULL &&
        c.item(v.count) &&
        v.count >= 1ULL &&
        v.count <= 21ULL &&
        c.item(v.row);
}
template<class C> bool transfer(C& c, campaign_row_presentation_payload& v) {
    (void)c; (void)v;
    return c.item(v.revision) &&
        c.item(v.index) &&
        v.index <= 20ULL &&
        c.item(v.count) &&
        v.count >= 1ULL &&
        v.count <= 21ULL &&
        c.item(v.row) &&
        c.item(v.summary);
}
template<class C> bool transfer(C& c, read_envelope& v) {
    (void)c; (void)v;
    return c.item(v.scope) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.command_type) &&
        v.command_type >= 1ULL &&
        c.item(v.payload_revision) &&
        v.payload_revision >= 1ULL &&
        c.sequence(v.save_binding);
}
template<class C> bool transfer(C& c, command_reply& v) {
    (void)c; (void)v;
    return c.sequence(v.actual_instance_id) && std::any_of(v.actual_instance_id.begin(), v.actual_instance_id.end(), [](auto item) { return item != 0; }) &&
        c.item(v.reference) &&
        c.item(v.dimensions);
}
template<class C> bool transfer(C& c, supported_command& v) {
    (void)c; (void)v;
    return c.item(v.command_type) &&
        v.command_type >= 1ULL &&
        c.item(v.payload_revision) &&
        v.payload_revision >= 1ULL;
}
template<class C> bool transfer(C& c, hello_result& v) {
    (void)c; (void)v;
    return c.item(v.scope) &&
        c.sequence(v.supported, 256) &&
        c.item(v.max_frame) &&
        c.item(v.execution_slots) &&
        c.item(v.full_result_ms) &&
        c.item(v.identity_capacity);
}
template<class C> bool transfer(C& c, immutable_submission& v) {
    (void)c; (void)v;
    return c.item(v.scope) &&
        c.sequence(v.namespace_id) && hex_namespace(v.namespace_id) &&
        c.item(v.command_type) &&
        v.command_type >= 1ULL &&
        c.item(v.payload_revision) &&
        v.payload_revision >= 1ULL &&
        c.sequence(v.save_binding) &&
        c.item(v.deadline_ms) &&
        v.deadline_ms >= 1ULL &&
        v.deadline_ms <= 5000ULL;
}
template<class C> bool transfer(C& c, command_response& v) {
    (void)c; (void)v;
    return c.item(v.reply) &&
        c.item(v.error) &&
        (v.error == 0 || v.error == 1 || v.error == 2 || v.error == 3 || v.error == 4 || v.error == 5 || v.error == 6 || v.error == 7 || v.error == 8) &&
        c.item(v.execution);
}
template<class C> bool transfer(C& c, wire_header_1& v) {
    (void)c; (void)v;
    return c.item(v.magic) &&
        v.magic == 1346978643ULL &&
        c.item(v.version) &&
        v.version == 1ULL &&
        c.item(v.operation) &&
        c.item(v.payload_length) &&
        v.payload_length <= 16368ULL &&
        c.item(v.result) &&
        (v.result == 0 || v.result == 1 || v.result == 2 || v.result == 3 || v.result == 4);
}
template<class C> bool transfer(C& c, wire_header_2& v) {
    (void)c; (void)v;
    return c.item(v.magic) &&
        v.magic == 1346978643ULL &&
        c.item(v.version) &&
        v.version == 2ULL &&
        c.item(v.operation) &&
        c.item(v.payload_length) &&
        v.payload_length <= 16352ULL &&
        c.item(v.result) &&
        (v.result == 0 || v.result == 1 || v.result == 2 || v.result == 3 || v.result == 4) &&
        c.item(v.correlation_id) &&
        v.correlation_id >= 1ULL &&
        c.item(v.flags) &&
        (uint64_t(v.flags) & ~1ULL) == 0 &&
        c.item(v.reserved) &&
        v.reserved == 0ULL;
}
template<class C> bool transfer(C& c, inspect_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        inspect_inspect_payload value{};
        if constexpr (!C::reading) value = std::get<inspect_inspect_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, engine_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        engine_engine_payload value{};
        if constexpr (!C::reading) value = std::get<engine_engine_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, context_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        context_context_payload value{};
        if constexpr (!C::reading) value = std::get<context_context_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, native_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        native_native_payload value{};
        if constexpr (!C::reading) value = std::get<native_native_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, diagnostic_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        diagnostic_diagnostic_payload value{};
        if constexpr (!C::reading) value = std::get<diagnostic_diagnostic_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, diagnostic_detail_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        diagnostic_detail_diagnostic_detail_payload value{};
        if constexpr (!C::reading) value = std::get<diagnostic_detail_diagnostic_detail_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, save_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        save_save_payload value{};
        if constexpr (!C::reading) value = std::get<save_save_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, save_admission_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        save_admission_save_admission_payload value{};
        if constexpr (!C::reading) value = std::get<save_admission_save_admission_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, save_write_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        save_write_save_write_payload value{};
        if constexpr (!C::reading) value = std::get<save_write_save_write_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, save_backup_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        save_backup_save_backup_payload value{};
        if constexpr (!C::reading) value = std::get<save_backup_save_backup_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, save_installation_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        save_installation_save_installation_payload value{};
        if constexpr (!C::reading) value = std::get<save_installation_save_installation_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, weapon_points_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (v.index() == 1) tag = 1;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        weapon_points_observe_payload value{};
        if constexpr (!C::reading) value = std::get<weapon_points_observe_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 1: {
        weapon_points_grant_payload value{};
        if constexpr (!C::reading) value = std::get<weapon_points_grant_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, campaign_row_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        campaign_row_campaign_row_payload value{};
        if constexpr (!C::reading) value = std::get<campaign_row_campaign_row_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, campaign_commit_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        campaign_commit_campaign_commit_payload value{};
        if constexpr (!C::reading) value = std::get<campaign_commit_campaign_commit_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, campaign_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        campaign_campaign_payload value{};
        if constexpr (!C::reading) value = std::get<campaign_campaign_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, inventory_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (v.index() == 1) tag = 1;
    if (v.index() == 2) tag = 2;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        inventory_observe_payload value{};
        if constexpr (!C::reading) value = std::get<inventory_observe_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 1: {
        inventory_ensure_payload value{};
        if constexpr (!C::reading) value = std::get<inventory_ensure_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 2: {
        inventory_set_capacity_payload value{};
        if constexpr (!C::reading) value = std::get<inventory_set_capacity_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, arsenal_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (v.index() == 1) tag = 1;
    if (v.index() == 2) tag = 2;
    if (v.index() == 3) tag = 4;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        arsenal_observe_payload value{};
        if constexpr (!C::reading) value = std::get<arsenal_observe_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 1: {
        arsenal_ensure_mods_payload value{};
        if constexpr (!C::reading) value = std::get<arsenal_ensure_mods_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 2: {
        arsenal_select_mod_payload value{};
        if constexpr (!C::reading) value = std::get<arsenal_select_mod_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 4: {
        arsenal_project_mastery_payload value{};
        if constexpr (!C::reading) value = std::get<arsenal_project_mastery_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, runes_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (v.index() == 1) tag = 1;
    if (v.index() == 2) tag = 2;
    if (v.index() == 3) tag = 3;
    if (v.index() == 4) tag = 4;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        runes_observe_payload value{};
        if constexpr (!C::reading) value = std::get<runes_observe_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 1: {
        runes_ensure_normal_payload value{};
        if constexpr (!C::reading) value = std::get<runes_ensure_normal_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 2: {
        runes_ensure_support_payload value{};
        if constexpr (!C::reading) value = std::get<runes_ensure_support_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 3: {
        runes_select_normal_payload value{};
        if constexpr (!C::reading) value = std::get<runes_select_normal_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 4: {
        runes_select_support_payload value{};
        if constexpr (!C::reading) value = std::get<runes_select_support_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, special_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (v.index() == 1) tag = 1;
    if (v.index() == 2) tag = 2;
    if (v.index() == 3) tag = 3;
    if (v.index() == 4) tag = 4;
    if (v.index() == 5) tag = 5;
    if (v.index() == 6) tag = 6;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        special_observe_payload value{};
        if constexpr (!C::reading) value = std::get<special_observe_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 1: {
        special_ensure_ownership_payload value{};
        if constexpr (!C::reading) value = std::get<special_ensure_ownership_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 2: {
        special_select_payload value{};
        if constexpr (!C::reading) value = std::get<special_select_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 3: {
        special_refill_status_payload value{};
        if constexpr (!C::reading) value = std::get<special_refill_status_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 4: {
        special_refill_publish_payload value{};
        if constexpr (!C::reading) value = std::get<special_refill_publish_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 5: {
        special_refill_take_payload value{};
        if constexpr (!C::reading) value = std::get<special_refill_take_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 6: {
        special_refill_execute_payload value{};
        if constexpr (!C::reading) value = std::get<special_refill_execute_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, deathlink_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (v.index() == 1) tag = 1;
    if (v.index() == 2) tag = 2;
    if (v.index() == 3) tag = 3;
    if (v.index() == 4) tag = 4;
    if (v.index() == 5) tag = 5;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        deathlink_observe_payload value{};
        if constexpr (!C::reading) value = std::get<deathlink_observe_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 1: {
        deathlink_configure_payload value{};
        if constexpr (!C::reading) value = std::get<deathlink_configure_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 2: {
        deathlink_apply_remote_payload value{};
        if constexpr (!C::reading) value = std::get<deathlink_apply_remote_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 3: {
        deathlink_poll_local_payload value{};
        if constexpr (!C::reading) value = std::get<deathlink_poll_local_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 4: {
        deathlink_ack_local_payload value{};
        if constexpr (!C::reading) value = std::get<deathlink_ack_local_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 5: {
        deathlink_cancel_remote_payload value{};
        if constexpr (!C::reading) value = std::get<deathlink_cancel_remote_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, automap_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 1;
    if (v.index() == 1) tag = 2;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 1: {
        automap_publish_payload value{};
        if constexpr (!C::reading) value = std::get<automap_publish_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 2: {
        automap_observe_payload value{};
        if constexpr (!C::reading) value = std::get<automap_observe_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, command_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 1;
    if (v.index() == 1) tag = 2;
    if (v.index() == 2) tag = 3;
    if (v.index() == 3) tag = 4;
    if (v.index() == 4) tag = 5;
    if (v.index() == 5) tag = 6;
    if (v.index() == 6) tag = 7;
    if (v.index() == 7) tag = 8;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 1: {
        command_activate_payload value{};
        if constexpr (!C::reading) value = std::get<command_activate_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 2: {
        command_player_perk_payload value{};
        if constexpr (!C::reading) value = std::get<command_player_perk_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 3: {
        command_ammo_refill_payload value{};
        if constexpr (!C::reading) value = std::get<command_ammo_refill_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 4: {
        command_crucible_refill_payload value{};
        if constexpr (!C::reading) value = std::get<command_crucible_refill_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 5: {
        command_echo_payload value{};
        if constexpr (!C::reading) value = std::get<command_echo_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 6: {
        command_condump_payload value{};
        if constexpr (!C::reading) value = std::get<command_condump_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 7: {
        command_list_inventory_payload value{};
        if constexpr (!C::reading) value = std::get<command_list_inventory_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    case 8: {
        command_transient_policy_payload value{};
        if constexpr (!C::reading) value = std::get<command_transient_policy_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, combat_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        combat_combat_payload value{};
        if constexpr (!C::reading) value = std::get<combat_combat_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, combat_presentation_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        combat_presentation_combat_presentation_payload value{};
        if constexpr (!C::reading) value = std::get<combat_presentation_combat_presentation_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, support_dump_payload& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        support_dump_support_dump_payload value{};
        if constexpr (!C::reading) value = std::get<support_dump_support_dump_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, campaign_row_payload_revision1& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        campaign_row_base_payload value{};
        if constexpr (!C::reading) value = std::get<campaign_row_base_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
template<class C> bool transfer(C& c, campaign_row_payload_revision2& v) {
    uint32_t tag = 0;
    if (v.index() == 0) tag = 0;
    if (!c.item(tag)) return false;
    switch (tag) {
    case 0: {
        campaign_row_presentation_payload value{};
        if constexpr (!C::reading) value = std::get<campaign_row_presentation_payload>(v);
        if (!transfer(c, value)) return false;
        v = std::move(value); return true;
    }
    default: return false;
    }
}
}
bool encode(const v1_command_submit& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_command_submit& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_command_submit value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_command_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_command_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_command_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_inventory_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_inventory_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_inventory_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_inspect_request& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_inspect_request& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_inspect_request value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_diagnostic_detail_submit& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_diagnostic_detail_submit& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_diagnostic_detail_submit value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_reward& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_reward& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_reward value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_campaign_rewards_row& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_campaign_rewards_row& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_campaign_rewards_row value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_automap_publish& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_automap_publish& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_automap_publish value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_campaign_base& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_campaign_base& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_campaign_base value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_campaign_presentation& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_campaign_presentation& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_campaign_presentation value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const v1_runes_request& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, v1_runes_request& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; v1_runes_request value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const scope& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, scope& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; scope value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_reference& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_reference& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_reference value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const submission_identity& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, submission_identity& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; submission_identity value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const submission& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, submission& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; submission value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const ack& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, ack& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; ack value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const result_dimensions& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, result_dimensions& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; result_dimensions value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_status& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_status& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_status value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_engine_field& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_engine_field& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_engine_field value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_engine_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_engine_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_engine_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_context_map& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_context_map& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_context_map value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_context_field& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_context_field& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_context_field value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_context_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_context_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_context_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_native_scope& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_native_scope& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_native_scope value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_native_event& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_native_event& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_native_event value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_native_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_native_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_native_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_diagnostic_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_diagnostic_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_diagnostic_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_save_field& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_save_field& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_save_field value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_save_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_save_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_save_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_save_admission_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_save_admission_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_save_admission_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_save_write_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_save_write_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_save_write_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_save_backup_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_save_backup_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_save_backup_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_install_event& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_install_event& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_install_event value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_save_installation_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_save_installation_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_save_installation_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_weapon_points_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_weapon_points_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_weapon_points_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_campaign_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_campaign_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_campaign_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_inventory_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_inventory_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_inventory_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_arsenal_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_arsenal_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_arsenal_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_runes_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_runes_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_runes_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_special_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_special_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_special_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_deathlink_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_deathlink_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_deathlink_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_automap_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_automap_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_automap_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_command_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_command_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_command_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_combat_snapshot& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_combat_snapshot& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_combat_snapshot value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_combat_presentation_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_combat_presentation_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_combat_presentation_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const inspect_inspect_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, inspect_inspect_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; inspect_inspect_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const engine_engine_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, engine_engine_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; engine_engine_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const context_context_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, context_context_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; context_context_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const native_native_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, native_native_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; native_native_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const diagnostic_diagnostic_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, diagnostic_diagnostic_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; diagnostic_diagnostic_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const diagnostic_detail_diagnostic_detail_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, diagnostic_detail_diagnostic_detail_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; diagnostic_detail_diagnostic_detail_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_save_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_save_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_save_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_admission_save_admission_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_admission_save_admission_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_admission_save_admission_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_write_save_write_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_write_save_write_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_write_save_write_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_backup_save_backup_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_backup_save_backup_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_backup_save_backup_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_installation_save_installation_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_installation_save_installation_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_installation_save_installation_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const weapon_points_observe_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, weapon_points_observe_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; weapon_points_observe_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const weapon_points_grant_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, weapon_points_grant_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; weapon_points_grant_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_campaign_row& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_campaign_row& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_campaign_row value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_physical_challenge& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_physical_challenge& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_physical_challenge value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_campaign_summary& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_campaign_summary& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_campaign_summary value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_row_campaign_row_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_row_campaign_row_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_row_campaign_row_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_commit_campaign_commit_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_commit_campaign_commit_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_commit_campaign_commit_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_campaign_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_campaign_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_campaign_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const inventory_observe_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, inventory_observe_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; inventory_observe_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const inventory_ensure_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, inventory_ensure_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; inventory_ensure_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const inventory_set_capacity_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, inventory_set_capacity_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; inventory_set_capacity_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const arsenal_observe_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, arsenal_observe_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; arsenal_observe_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const arsenal_ensure_mods_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, arsenal_ensure_mods_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; arsenal_ensure_mods_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const arsenal_select_mod_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, arsenal_select_mod_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; arsenal_select_mod_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const arsenal_project_mastery_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, arsenal_project_mastery_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; arsenal_project_mastery_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const runes_observe_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, runes_observe_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; runes_observe_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const runes_ensure_normal_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, runes_ensure_normal_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; runes_ensure_normal_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const runes_ensure_support_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, runes_ensure_support_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; runes_ensure_support_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const runes_select_normal_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, runes_select_normal_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; runes_select_normal_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const runes_select_support_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, runes_select_support_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; runes_select_support_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_observe_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_observe_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_observe_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_ensure_ownership_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_ensure_ownership_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_ensure_ownership_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_select_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_select_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_select_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_refill_status_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_refill_status_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_refill_status_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_refill_publish_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_refill_publish_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_refill_publish_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_refill_take_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_refill_take_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_refill_take_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_refill_execute_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_refill_execute_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_refill_execute_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const deathlink_observe_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, deathlink_observe_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; deathlink_observe_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const deathlink_configure_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, deathlink_configure_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; deathlink_configure_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const deathlink_apply_remote_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, deathlink_apply_remote_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; deathlink_apply_remote_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const deathlink_poll_local_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, deathlink_poll_local_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; deathlink_poll_local_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const deathlink_ack_local_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, deathlink_ack_local_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; deathlink_ack_local_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const deathlink_cancel_remote_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, deathlink_cancel_remote_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; deathlink_cancel_remote_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const automap_publish_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, automap_publish_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; automap_publish_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const automap_observe_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, automap_observe_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; automap_observe_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_activate_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_activate_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_activate_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_player_perk_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_player_perk_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_player_perk_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_ammo_refill_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_ammo_refill_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_ammo_refill_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_crucible_refill_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_crucible_refill_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_crucible_refill_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_echo_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_echo_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_echo_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_condump_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_condump_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_condump_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_list_inventory_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_list_inventory_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_list_inventory_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_transient_policy_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_transient_policy_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_transient_policy_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const combat_map_identifier& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, combat_map_identifier& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; combat_map_identifier value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const combat_combat_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, combat_combat_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; combat_combat_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_diagnostic_request& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_diagnostic_request& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_diagnostic_request value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_combat_presentation_request& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_combat_presentation_request& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_combat_presentation_request value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const combat_presentation_combat_presentation_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, combat_presentation_combat_presentation_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; combat_presentation_combat_presentation_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const support_dump_support_dump_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, support_dump_support_dump_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; support_dump_support_dump_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const sc_diagnostic_detail& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, sc_diagnostic_detail& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; sc_diagnostic_detail value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const diagnostic_detail_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, diagnostic_detail_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; diagnostic_detail_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_admission_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_admission_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_admission_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_row_base_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_row_base_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_row_base_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_row_presentation_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_row_presentation_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_row_presentation_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const read_envelope& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, read_envelope& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; read_envelope value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_reply& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_reply& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_reply value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const supported_command& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, supported_command& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; supported_command value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const hello_result& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, hello_result& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; hello_result value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const immutable_submission& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, immutable_submission& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; immutable_submission value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_response& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_response& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_response value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const wire_header_1& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, wire_header_1& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const wire_header_2& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, wire_header_2& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; wire_header_2 value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const inspect_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, inspect_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; inspect_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const engine_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, engine_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; engine_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const context_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, context_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; context_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const native_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, native_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; native_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const diagnostic_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, diagnostic_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; diagnostic_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const diagnostic_detail_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, diagnostic_detail_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; diagnostic_detail_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_admission_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_admission_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_admission_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_write_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_write_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_write_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_backup_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_backup_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_backup_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const save_installation_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, save_installation_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; save_installation_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const weapon_points_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, weapon_points_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; weapon_points_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_row_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_row_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_row_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_commit_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_commit_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_commit_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const inventory_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, inventory_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; inventory_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const arsenal_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, arsenal_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; arsenal_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const runes_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, runes_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; runes_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const special_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, special_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; special_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const deathlink_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, deathlink_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; deathlink_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const automap_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, automap_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; automap_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const command_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, command_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; command_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const combat_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, combat_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; combat_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const combat_presentation_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, combat_presentation_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; combat_presentation_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const support_dump_payload& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, support_dump_payload& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; support_dump_payload value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_row_payload_revision1& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_row_payload_revision1& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_row_payload_revision1 value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode(const campaign_row_payload_revision2& source, Bytes& out) {
    Writer c; auto value = source;
    if (!transfer(c, value)) return false;
    out = std::move(c.data); return true;
}
bool decode(const Bytes& in, campaign_row_payload_revision2& out) {
    if (in.size() > max_frame) return false;
    Reader c{in}; campaign_row_payload_revision2 value{};
    if (!transfer(c, value) || c.pos != in.size()) return false;
    out = std::move(value); return true;
}
bool encode_command_submit(const v1_command_submit& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 46; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_command_submit(const Bytes& in, v1_command_submit& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 46 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_command_result(const v1_command_result& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!true && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 47; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_command_result(const Bytes& in, v1_command_result& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 47 || header.payload_length != in.size() - header_size_1 || (!true && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_inventory_result(const v1_inventory_result& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!true && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 26; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_inventory_result(const Bytes& in, v1_inventory_result& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 26 || header.payload_length != in.size() - header_size_1 || (!true && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_inspect_request(const v1_inspect_request& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 1; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_inspect_request(const Bytes& in, v1_inspect_request& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 1 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_diagnostic_detail_submit(const v1_diagnostic_detail_submit& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 8; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_diagnostic_detail_submit(const Bytes& in, v1_diagnostic_detail_submit& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 8 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_campaign_rewards_row(const v1_campaign_rewards_row& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 22; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_campaign_rewards_row(const Bytes& in, v1_campaign_rewards_row& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 22 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_automap_publish(const v1_automap_publish& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 45; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_automap_publish(const Bytes& in, v1_automap_publish& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 45 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_campaign_base(const v1_campaign_base& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 22; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_campaign_base(const Bytes& in, v1_campaign_base& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 22 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_campaign_presentation(const v1_campaign_presentation& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 22; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_campaign_presentation(const Bytes& in, v1_campaign_presentation& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 22 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
bool encode_runes_request(const v1_runes_request& value, Bytes& out, uint32_t result) {
    if (result > 4 || (!false && result)) return false;
    Bytes payload;
    if (!result && !encode(value, payload)) return false;
    if (payload.size() + header_size_1 > max_frame) return false;
    Writer c; wire_header_1 header{}; header.magic = 1346978643; header.version = 1;
    header.operation = 33; header.payload_length = static_cast<uint32_t>(payload.size()); header.result = result;
    if (!transfer(c, header)) return false;
    c.data.insert(c.data.end(), payload.begin(), payload.end()); out = std::move(c.data); return true;
}
bool decode_runes_request(const Bytes& in, v1_runes_request& out, uint32_t& result) {
    if (in.size() < header_size_1 || in.size() > max_frame) return false;
    Reader c{in}; wire_header_1 header{};
    if (!transfer(c, header)) return false;
    if (header.operation != 33 || header.payload_length != in.size() - header_size_1 || (!false && header.result)) return false;
    if (header.result) { if (header.payload_length) return false; out = {}; result = header.result; return true; }
    if (!decode(Bytes(in.begin() + header_size_1, in.end()), out)) return false;
    result = header.result; return true;
}
namespace {
template<class T> bool native_matches(const NativeRequest& request) { return std::holds_alternative<T>(request); }
template<class T, class V> bool decode_variant(const Bytes& bytes, V& out) { T value{}; if (!decode(bytes, value)) return false; out = std::move(value); return true; }
template<class T, class V> bool encode_variant(const V& value, Bytes& out) { const auto* typed = std::get_if<T>(&value); return typed && encode(*typed, out); }
const VariantPolicy policies_inspect[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "inspect"},
};
const VariantPolicy policies_engine[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "engine"},
};
const VariantPolicy policies_context[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "context"},
};
const VariantPolicy policies_native[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "native"},
};
const VariantPolicy policies_diagnostic[] = {
    {0, Operation::submit, Admission::D, Execution::native_frame, "feature_readback", "diagnostic"},
};
const VariantPolicy policies_diagnostic_detail[] = {
    {0, Operation::submit, Admission::D, Execution::native_frame, "feature_readback", "diagnostic_detail"},
};
const VariantPolicy policies_save[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "save"},
};
const VariantPolicy policies_save_admission[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "save_admission"},
};
const VariantPolicy policies_save_write[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "save_write"},
};
const VariantPolicy policies_save_backup[] = {
    {0, Operation::submit, Admission::G, Execution::native_frame_async, "feature_readback", "save_backup"},
};
const VariantPolicy policies_save_installation[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "save_installation"},
};
const VariantPolicy policies_weapon_points[] = {
    {0, Operation::read, Admission::G, Execution::native_frame, "observation", "weapon_points"},
    {1, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "weapon_points"},
};
const VariantPolicy policies_campaign_row[] = {
    {0, Operation::submit, Admission::S, Execution::publication, "accepted_publication", "campaign_row"},
};
const VariantPolicy policies_campaign_commit[] = {
    {0, Operation::submit, Admission::S, Execution::publication, "accepted_publication", "campaign_commit"},
};
const VariantPolicy policies_campaign[] = {
    {0, Operation::read, Admission::S, Execution::snapshot, "observation", "campaign"},
};
const VariantPolicy policies_inventory[] = {
    {0, Operation::read, Admission::G, Execution::native_frame, "observation", "inventory"},
    {1, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "inventory"},
    {2, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "inventory"},
};
const VariantPolicy policies_arsenal[] = {
    {0, Operation::read, Admission::G, Execution::native_frame, "observation", "arsenal"},
    {1, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "arsenal"},
    {2, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "arsenal"},
    {4, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "arsenal"},
};
const VariantPolicy policies_runes[] = {
    {0, Operation::read, Admission::G, Execution::native_frame, "observation", "runes"},
    {1, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "runes"},
    {2, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "runes"},
    {3, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "runes"},
    {4, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "runes"},
};
const VariantPolicy policies_special[] = {
    {0, Operation::read, Admission::G, Execution::native_frame, "observation", "special"},
    {1, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "special"},
    {2, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "special"},
    {3, Operation::read, Admission::G, Execution::native_frame, "observation", "special"},
    {4, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "special"},
    {5, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "special"},
    {6, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "special"},
};
const VariantPolicy policies_deathlink[] = {
    {0, Operation::read, Admission::G, Execution::native_frame, "observation", "deathlink"},
    {1, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "deathlink"},
    {2, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "deathlink"},
    {3, Operation::read, Admission::G, Execution::native_frame, "observation", "deathlink"},
    {4, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "deathlink"},
    {5, Operation::submit, Admission::G, Execution::native_frame, "feature_readback", "deathlink"},
};
const VariantPolicy policies_automap[] = {
    {1, Operation::submit, Admission::S, Execution::publication, "accepted_publication", "automap"},
    {2, Operation::read, Admission::S, Execution::snapshot, "observation", "automap"},
};
const VariantPolicy policies_command[] = {
    {1, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
    {2, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
    {3, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
    {4, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
    {5, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
    {6, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
    {7, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
    {8, Operation::submit, Admission::G, Execution::native_frame, "dispatch_only", "command"},
};
const VariantPolicy policies_combat[] = {
    {0, Operation::read, Admission::P, Execution::snapshot, "observation", "combat"},
};
const VariantPolicy policies_combat_presentation[] = {
    {0, Operation::submit, Admission::S, Execution::publication, "accepted_publication", "combat_presentation"},
};
const VariantPolicy policies_support_dump[] = {
    {0, Operation::submit, Admission::P, Execution::native_frame, "dispatch_only", "support_dump"},
};
}
const Descriptor descriptors[] = {
    {1, 1, "inspect", "{\"id\":1,\"name\":\"inspect\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"inspect\",\"type\":\"inspect_inspect_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"inspect\"}]},\"result\":\"sc_status\",\"admission\":\"P\",\"availability\":\"inspect\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[1],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"inspect_inspect_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_inspect, std::size(policies_inspect), &decode_variant<inspect_payload, Payload>, &encode_variant<inspect_payload, Payload>, &decode_variant<sc_status, Result>, &encode_variant<sc_status, Result>, &native_matches<NativeRead>},
    {2, 1, "engine", "{\"id\":2,\"name\":\"engine\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"engine\",\"type\":\"engine_engine_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"engine\"}]},\"result\":\"sc_engine_snapshot\",\"admission\":\"P\",\"availability\":\"engine\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[2],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"engine_engine_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_engine, std::size(policies_engine), &decode_variant<engine_payload, Payload>, &encode_variant<engine_payload, Payload>, &decode_variant<sc_engine_snapshot, Result>, &encode_variant<sc_engine_snapshot, Result>, &native_matches<NativeRead>},
    {3, 1, "context", "{\"id\":3,\"name\":\"context\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"context\",\"type\":\"context_context_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"context\"}]},\"result\":\"sc_context_snapshot\",\"admission\":\"P\",\"availability\":\"context\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[3],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"context_context_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_context, std::size(policies_context), &decode_variant<context_payload, Payload>, &encode_variant<context_payload, Payload>, &decode_variant<sc_context_snapshot, Result>, &encode_variant<sc_context_snapshot, Result>, &native_matches<NativeRead>},
    {4, 1, "native", "{\"id\":4,\"name\":\"native\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"native\",\"type\":\"native_native_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"native\"}]},\"result\":\"sc_native_snapshot\",\"admission\":\"P\",\"availability\":\"native\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[4],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"native_native_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_native, std::size(policies_native), &decode_variant<native_payload, Payload>, &encode_variant<native_payload, Payload>, &decode_variant<sc_native_snapshot, Result>, &encode_variant<sc_native_snapshot, Result>, &native_matches<NativeRead>},
    {5, 1, "diagnostic", "{\"id\":5,\"name\":\"diagnostic\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"diagnostic\",\"type\":\"diagnostic_diagnostic_payload\",\"operation\":\"submit\",\"admission\":\"D\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"diagnostic\"}]},\"result\":\"sc_diagnostic_result\",\"admission\":\"D\",\"availability\":\"diagnostic\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[5,6,7],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"diagnostic_diagnostic_payload\"]}]}", Admission::D, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_diagnostic, std::size(policies_diagnostic), &decode_variant<diagnostic_payload, Payload>, &encode_variant<diagnostic_payload, Payload>, &decode_variant<sc_diagnostic_result, Result>, &encode_variant<sc_diagnostic_result, Result>, &native_matches<::sc_diagnostic_request>},
    {8, 1, "diagnostic_detail", "{\"id\":8,\"name\":\"diagnostic_detail\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"diagnostic_detail\",\"type\":\"diagnostic_detail_diagnostic_detail_payload\",\"operation\":\"submit\",\"admission\":\"D\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"diagnostic_detail\"}]},\"result\":\"diagnostic_detail_result\",\"admission\":\"D\",\"availability\":\"diagnostic_detail\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[8,9,10],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"diagnostic_detail_diagnostic_detail_payload\"]}]}", Admission::D, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_diagnostic_detail, std::size(policies_diagnostic_detail), &decode_variant<diagnostic_detail_payload, Payload>, &encode_variant<diagnostic_detail_payload, Payload>, &decode_variant<diagnostic_detail_result, Result>, &encode_variant<diagnostic_detail_result, Result>, &native_matches<::sc_diagnostic_request>},
    {11, 1, "save", "{\"id\":11,\"name\":\"save\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"save\",\"type\":\"save_save_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"save\"}]},\"result\":\"sc_save_snapshot\",\"admission\":\"P\",\"availability\":\"save\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[11],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"save_save_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_save, std::size(policies_save), &decode_variant<save_payload, Payload>, &encode_variant<save_payload, Payload>, &decode_variant<sc_save_snapshot, Result>, &encode_variant<sc_save_snapshot, Result>, &native_matches<NativeRead>},
    {12, 1, "save_admission", "{\"id\":12,\"name\":\"save_admission\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"save_admission\",\"type\":\"save_admission_save_admission_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"save_admission\"}]},\"result\":\"save_admission_result\",\"admission\":\"P\",\"availability\":\"save_admission\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[12],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"save_admission_save_admission_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_save_admission, std::size(policies_save_admission), &decode_variant<save_admission_payload, Payload>, &encode_variant<save_admission_payload, Payload>, &decode_variant<save_admission_result, Result>, &encode_variant<save_admission_result, Result>, &native_matches<NativeRead>},
    {13, 1, "save_write", "{\"id\":13,\"name\":\"save_write\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"save_write\",\"type\":\"save_write_save_write_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"save_write\"}]},\"result\":\"sc_save_write_snapshot\",\"admission\":\"P\",\"availability\":\"save_write\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[13],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"save_write_save_write_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_save_write, std::size(policies_save_write), &decode_variant<save_write_payload, Payload>, &encode_variant<save_write_payload, Payload>, &decode_variant<sc_save_write_snapshot, Result>, &encode_variant<sc_save_write_snapshot, Result>, &native_matches<NativeRead>},
    {14, 1, "save_backup", "{\"id\":14,\"name\":\"save_backup\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"save_backup\",\"type\":\"save_backup_save_backup_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame_async\",\"availability\":\"save_backup\"}]},\"result\":\"sc_save_backup_snapshot\",\"admission\":\"G\",\"availability\":\"save_backup\",\"execution\":\"native_frame_async\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[14,15,16],\"release\":\"none\"},\"owned_slot_required\":true,\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"save_backup_save_backup_payload\"]}]}", Admission::G, Execution::native_frame_async, Evidence::feature_result, Ack::terminal_consumed, policies_save_backup, std::size(policies_save_backup), &decode_variant<save_backup_payload, Payload>, &encode_variant<save_backup_payload, Payload>, &decode_variant<sc_save_backup_snapshot, Result>, &encode_variant<sc_save_backup_snapshot, Result>, &native_matches<::sc_save_backup_request>},
    {17, 1, "save_installation", "{\"id\":17,\"name\":\"save_installation\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"save_installation\",\"type\":\"save_installation_save_installation_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"save_installation\"}]},\"result\":\"sc_save_installation_snapshot\",\"admission\":\"P\",\"availability\":\"save_installation\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[17],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"save_installation_save_installation_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_save_installation, std::size(policies_save_installation), &decode_variant<save_installation_payload, Payload>, &encode_variant<save_installation_payload, Payload>, &decode_variant<sc_save_installation_snapshot, Result>, &encode_variant<sc_save_installation_snapshot, Result>, &native_matches<NativeRead>},
    {18, 1, "weapon_points", "{\"id\":18,\"name\":\"weapon_points\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"read\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"observe\",\"type\":\"weapon_points_observe_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"weapon_points\"},{\"tag\":1,\"name\":\"grant\",\"type\":\"weapon_points_grant_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"weapon_points\"}]},\"result\":\"sc_weapon_points_result\",\"admission\":\"G\",\"availability\":\"weapon_points\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[18,19,20,21],\"release\":\"legacy_release\"},\"constraints\":[{\"kind\":\"checked_arithmetic\",\"fields\":[\"amount\",\"expected_gained\"],\"max\":2147483647}],\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"weapon_points_observe_payload\",\"weapon_points_grant_payload\"]}]}", Admission::G, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_weapon_points, std::size(policies_weapon_points), &decode_variant<weapon_points_payload, Payload>, &encode_variant<weapon_points_payload, Payload>, &decode_variant<sc_weapon_points_result, Result>, &encode_variant<sc_weapon_points_result, Result>, &native_matches<::sc_weapon_points_request>},
    {22, 1, "campaign_row", "{\"id\":22,\"name\":\"campaign_row\",\"payload_revisions\":[1,2,3],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign_row\",\"type\":\"campaign_row_campaign_row_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"campaign_row\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign_row\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[22],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_row_base_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_row_presentation_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_row_campaign_row_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_campaign_row, std::size(policies_campaign_row), &decode_variant<campaign_row_payload_revision1, Payload>, &encode_variant<campaign_row_payload_revision1, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {22, 2, "campaign_row", "{\"id\":22,\"name\":\"campaign_row\",\"payload_revisions\":[1,2,3],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign_row\",\"type\":\"campaign_row_campaign_row_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"campaign_row\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign_row\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[22],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_row_base_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_row_presentation_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_row_campaign_row_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_campaign_row, std::size(policies_campaign_row), &decode_variant<campaign_row_payload_revision2, Payload>, &encode_variant<campaign_row_payload_revision2, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {22, 3, "campaign_row", "{\"id\":22,\"name\":\"campaign_row\",\"payload_revisions\":[1,2,3],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign_row\",\"type\":\"campaign_row_campaign_row_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"campaign_row\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign_row\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[22],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_row_base_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_row_presentation_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_row_campaign_row_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_campaign_row, std::size(policies_campaign_row), &decode_variant<campaign_row_payload, Payload>, &encode_variant<campaign_row_payload, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {23, 1, "campaign_commit", "{\"id\":23,\"name\":\"campaign_commit\",\"payload_revisions\":[1,2,3],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign_commit\",\"type\":\"campaign_commit_campaign_commit_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"campaign_commit\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign_commit\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[23],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_campaign_commit, std::size(policies_campaign_commit), &decode_variant<campaign_commit_payload, Payload>, &encode_variant<campaign_commit_payload, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {23, 2, "campaign_commit", "{\"id\":23,\"name\":\"campaign_commit\",\"payload_revisions\":[1,2,3],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign_commit\",\"type\":\"campaign_commit_campaign_commit_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"campaign_commit\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign_commit\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[23],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_campaign_commit, std::size(policies_campaign_commit), &decode_variant<campaign_commit_payload, Payload>, &encode_variant<campaign_commit_payload, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {23, 3, "campaign_commit", "{\"id\":23,\"name\":\"campaign_commit\",\"payload_revisions\":[1,2,3],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign_commit\",\"type\":\"campaign_commit_campaign_commit_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"campaign_commit\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign_commit\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[23],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_commit_campaign_commit_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_campaign_commit, std::size(policies_campaign_commit), &decode_variant<campaign_commit_payload, Payload>, &encode_variant<campaign_commit_payload, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {24, 1, "campaign", "{\"id\":24,\"name\":\"campaign\",\"payload_revisions\":[1,2,3],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign\",\"type\":\"campaign_campaign_payload\",\"operation\":\"read\",\"admission\":\"S\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"campaign\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[24],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_campaign_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_campaign_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_campaign_payload\"]}]}", Admission::S, Execution::snapshot, Evidence::feature_result, Ack::none, policies_campaign, std::size(policies_campaign), &decode_variant<campaign_payload, Payload>, &encode_variant<campaign_payload, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {24, 2, "campaign", "{\"id\":24,\"name\":\"campaign\",\"payload_revisions\":[1,2,3],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign\",\"type\":\"campaign_campaign_payload\",\"operation\":\"read\",\"admission\":\"S\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"campaign\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[24],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_campaign_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_campaign_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_campaign_payload\"]}]}", Admission::S, Execution::snapshot, Evidence::feature_result, Ack::none, policies_campaign, std::size(policies_campaign), &decode_variant<campaign_payload, Payload>, &encode_variant<campaign_payload, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {24, 3, "campaign", "{\"id\":24,\"name\":\"campaign\",\"payload_revisions\":[1,2,3],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"campaign\",\"type\":\"campaign_campaign_payload\",\"operation\":\"read\",\"admission\":\"S\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"campaign\"}]},\"result\":\"sc_campaign_result\",\"admission\":\"S\",\"availability\":\"campaign\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[24],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"campaign_campaign_payload\"]},{\"revision\":2,\"payload_variants\":[\"campaign_campaign_payload\"]},{\"revision\":3,\"payload_variants\":[\"campaign_campaign_payload\"]}]}", Admission::S, Execution::snapshot, Evidence::feature_result, Ack::none, policies_campaign, std::size(policies_campaign), &decode_variant<campaign_payload, Payload>, &encode_variant<campaign_payload, Payload>, &decode_variant<sc_campaign_result, Result>, &encode_variant<sc_campaign_result, Result>, &native_matches<NativeCampaign>},
    {25, 1, "inventory", "{\"id\":25,\"name\":\"inventory\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"read\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"observe\",\"type\":\"inventory_observe_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"inventory\"},{\"tag\":1,\"name\":\"ensure\",\"type\":\"inventory_ensure_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"inventory\"},{\"tag\":2,\"name\":\"set_capacity\",\"type\":\"inventory_set_capacity_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"inventory\"}]},\"result\":\"sc_inventory_result\",\"admission\":\"G\",\"availability\":\"inventory\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[25,26,27,28],\"release\":\"legacy_release\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"inventory_observe_payload\",\"inventory_ensure_payload\",\"inventory_set_capacity_payload\"]}]}", Admission::G, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_inventory, std::size(policies_inventory), &decode_variant<inventory_payload, Payload>, &encode_variant<inventory_payload, Payload>, &decode_variant<sc_inventory_result, Result>, &encode_variant<sc_inventory_result, Result>, &native_matches<::sc_inventory_request>},
    {29, 1, "arsenal", "{\"id\":29,\"name\":\"arsenal\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"read\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"observe\",\"type\":\"arsenal_observe_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"arsenal\"},{\"tag\":1,\"name\":\"ensure_mods\",\"type\":\"arsenal_ensure_mods_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"arsenal\"},{\"tag\":2,\"name\":\"select_mod\",\"type\":\"arsenal_select_mod_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"arsenal\"},{\"tag\":4,\"name\":\"project_mastery\",\"type\":\"arsenal_project_mastery_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"arsenal\"}]},\"result\":\"sc_arsenal_result\",\"admission\":\"G\",\"availability\":\"arsenal\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[29,30,31,32],\"release\":\"legacy_release\"},\"refused_tags\":[3,5],\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"arsenal_observe_payload\",\"arsenal_ensure_mods_payload\",\"arsenal_select_mod_payload\",\"arsenal_project_mastery_payload\"]}]}", Admission::G, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_arsenal, std::size(policies_arsenal), &decode_variant<arsenal_payload, Payload>, &encode_variant<arsenal_payload, Payload>, &decode_variant<sc_arsenal_result, Result>, &encode_variant<sc_arsenal_result, Result>, &native_matches<::sc_arsenal_request>},
    {33, 1, "runes", "{\"id\":33,\"name\":\"runes\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"read\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"observe\",\"type\":\"runes_observe_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"runes\"},{\"tag\":1,\"name\":\"ensure_normal\",\"type\":\"runes_ensure_normal_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"runes\"},{\"tag\":2,\"name\":\"ensure_support\",\"type\":\"runes_ensure_support_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"runes\"},{\"tag\":3,\"name\":\"select_normal\",\"type\":\"runes_select_normal_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"runes\"},{\"tag\":4,\"name\":\"select_support\",\"type\":\"runes_select_support_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"runes\"}]},\"result\":\"sc_runes_result\",\"admission\":\"G\",\"availability\":\"runes\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[33,34,35,36],\"release\":\"legacy_release\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"runes_observe_payload\",\"runes_ensure_normal_payload\",\"runes_ensure_support_payload\",\"runes_select_normal_payload\",\"runes_select_support_payload\"]}]}", Admission::G, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_runes, std::size(policies_runes), &decode_variant<runes_payload, Payload>, &encode_variant<runes_payload, Payload>, &decode_variant<sc_runes_result, Result>, &encode_variant<sc_runes_result, Result>, &native_matches<::sc_runes_request>},
    {37, 1, "special", "{\"id\":37,\"name\":\"special\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"read\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"observe\",\"type\":\"special_observe_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"special\"},{\"tag\":1,\"name\":\"ensure_ownership\",\"type\":\"special_ensure_ownership_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"special\"},{\"tag\":2,\"name\":\"select\",\"type\":\"special_select_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"special\"},{\"tag\":3,\"name\":\"refill_status\",\"type\":\"special_refill_status_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"special\"},{\"tag\":4,\"name\":\"refill_publish\",\"type\":\"special_refill_publish_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"special\"},{\"tag\":5,\"name\":\"refill_take\",\"type\":\"special_refill_take_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"special\"},{\"tag\":6,\"name\":\"refill_execute\",\"type\":\"special_refill_execute_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"special\"}]},\"result\":\"sc_special_result\",\"admission\":\"G\",\"availability\":\"special\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[37,38,39,40],\"release\":\"legacy_release\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"special_observe_payload\",\"special_ensure_ownership_payload\",\"special_select_payload\",\"special_refill_status_payload\",\"special_refill_publish_payload\",\"special_refill_take_payload\",\"special_refill_execute_payload\"]}]}", Admission::G, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_special, std::size(policies_special), &decode_variant<special_payload, Payload>, &encode_variant<special_payload, Payload>, &decode_variant<sc_special_result, Result>, &encode_variant<sc_special_result, Result>, &native_matches<::sc_special_request>},
    {41, 1, "deathlink", "{\"id\":41,\"name\":\"deathlink\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"read\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"observe\",\"type\":\"deathlink_observe_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"deathlink\"},{\"tag\":1,\"name\":\"configure\",\"type\":\"deathlink_configure_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"deathlink\"},{\"tag\":2,\"name\":\"apply_remote\",\"type\":\"deathlink_apply_remote_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"deathlink\"},{\"tag\":3,\"name\":\"poll_local\",\"type\":\"deathlink_poll_local_payload\",\"operation\":\"read\",\"admission\":\"G\",\"effect\":\"observation\",\"execution\":\"native_frame\",\"availability\":\"deathlink\"},{\"tag\":4,\"name\":\"ack_local\",\"type\":\"deathlink_ack_local_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"deathlink\"},{\"tag\":5,\"name\":\"cancel_remote\",\"type\":\"deathlink_cancel_remote_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"feature_readback\",\"execution\":\"native_frame\",\"availability\":\"deathlink\"}]},\"result\":\"sc_deathlink_result\",\"admission\":\"G\",\"availability\":\"deathlink\",\"execution\":\"native_frame\",\"evidence\":\"feature_result\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[41,42,43,44],\"release\":\"legacy_release\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"deathlink_observe_payload\",\"deathlink_configure_payload\",\"deathlink_apply_remote_payload\",\"deathlink_poll_local_payload\",\"deathlink_ack_local_payload\",\"deathlink_cancel_remote_payload\"]}]}", Admission::G, Execution::native_frame, Evidence::feature_result, Ack::terminal_consumed, policies_deathlink, std::size(policies_deathlink), &decode_variant<deathlink_payload, Payload>, &encode_variant<deathlink_payload, Payload>, &decode_variant<sc_deathlink_result, Result>, &encode_variant<sc_deathlink_result, Result>, &native_matches<::sc_deathlink_request>},
    {45, 1, "automap", "{\"id\":45,\"name\":\"automap\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"read\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":1,\"name\":\"publish\",\"type\":\"automap_publish_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"automap\"},{\"tag\":2,\"name\":\"observe\",\"type\":\"automap_observe_payload\",\"operation\":\"read\",\"admission\":\"S\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"automap\"}]},\"result\":\"sc_automap_result\",\"admission\":\"S\",\"availability\":\"automap\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[45],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"automap_publish_payload\",\"automap_observe_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_automap, std::size(policies_automap), &decode_variant<automap_payload, Payload>, &encode_variant<automap_payload, Payload>, &decode_variant<sc_automap_result, Result>, &encode_variant<sc_automap_result, Result>, &native_matches<::sc_automap_request>},
    {46, 1, "command", "{\"id\":46,\"name\":\"command\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":1,\"name\":\"activate\",\"type\":\"command_activate_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"},{\"tag\":2,\"name\":\"player_perk\",\"type\":\"command_player_perk_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"},{\"tag\":3,\"name\":\"ammo_refill\",\"type\":\"command_ammo_refill_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"},{\"tag\":4,\"name\":\"crucible_refill\",\"type\":\"command_crucible_refill_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"},{\"tag\":5,\"name\":\"echo\",\"type\":\"command_echo_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"},{\"tag\":6,\"name\":\"condump\",\"type\":\"command_condump_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"},{\"tag\":7,\"name\":\"list_inventory\",\"type\":\"command_list_inventory_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"},{\"tag\":8,\"name\":\"transient_policy\",\"type\":\"command_transient_policy_payload\",\"operation\":\"submit\",\"admission\":\"G\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"command\"}]},\"result\":\"sc_command_result\",\"admission\":\"G\",\"availability\":\"command\",\"execution\":\"native_frame\",\"evidence\":\"dispatch_only\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[46,47,48,49],\"release\":\"legacy_release\"},\"adapter\":\"bounded_existing_console_allowlist\",\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"command_activate_payload\",\"command_player_perk_payload\",\"command_ammo_refill_payload\",\"command_crucible_refill_payload\",\"command_echo_payload\",\"command_condump_payload\",\"command_list_inventory_payload\",\"command_transient_policy_payload\"]}]}", Admission::G, Execution::native_frame, Evidence::dispatch_only, Ack::terminal_consumed, policies_command, std::size(policies_command), &decode_variant<command_payload, Payload>, &encode_variant<command_payload, Payload>, &decode_variant<sc_command_result, Result>, &encode_variant<sc_command_result, Result>, &native_matches<::sc_command_request>},
    {50, 1, "combat", "{\"id\":50,\"name\":\"combat\",\"payload_revisions\":[1],\"operations\":[\"read\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"combat\",\"type\":\"combat_combat_payload\",\"operation\":\"read\",\"admission\":\"P\",\"effect\":\"observation\",\"execution\":\"snapshot\",\"availability\":\"combat\"}]},\"result\":\"sc_combat_snapshot\",\"admission\":\"P\",\"availability\":\"combat\",\"execution\":\"snapshot\",\"evidence\":\"feature_result\",\"ack\":\"none\",\"v1\":{\"opcodes\":[50],\"release\":\"none\"},\"namespace_filter_required\":true,\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"combat_combat_payload\"]}]}", Admission::P, Execution::snapshot, Evidence::feature_result, Ack::none, policies_combat, std::size(policies_combat), &decode_variant<combat_payload, Payload>, &encode_variant<combat_payload, Payload>, &decode_variant<sc_combat_snapshot, Result>, &encode_variant<sc_combat_snapshot, Result>, &native_matches<::sc_combat_query>},
    {51, 1, "combat_presentation", "{\"id\":51,\"name\":\"combat_presentation\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"combat_presentation\",\"type\":\"combat_presentation_combat_presentation_payload\",\"operation\":\"submit\",\"admission\":\"S\",\"effect\":\"accepted_publication\",\"execution\":\"publication\",\"availability\":\"combat_presentation\"}]},\"result\":\"sc_combat_presentation_result\",\"admission\":\"S\",\"availability\":\"combat_presentation\",\"execution\":\"publication\",\"evidence\":\"accepted_publication\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[51],\"release\":\"none\"},\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"combat_presentation_combat_presentation_payload\"]}]}", Admission::S, Execution::publication, Evidence::accepted_publication, Ack::terminal_consumed, policies_combat_presentation, std::size(policies_combat_presentation), &decode_variant<combat_presentation_payload, Payload>, &encode_variant<combat_presentation_payload, Payload>, &decode_variant<sc_combat_presentation_result, Result>, &encode_variant<sc_combat_presentation_result, Result>, &native_matches<::sc_combat_presentation_request>},
    {52, 1, "support_dump", "{\"id\":52,\"name\":\"support_dump\",\"payload_revisions\":[1],\"operations\":[\"ack\",\"cancel\",\"query\",\"submit\"],\"payload\":{\"tag_type\":\"u32\",\"variants\":[{\"tag\":0,\"name\":\"support_dump\",\"type\":\"support_dump_support_dump_payload\",\"operation\":\"submit\",\"admission\":\"P\",\"effect\":\"dispatch_only\",\"execution\":\"native_frame\",\"availability\":\"support_dump\"}]},\"result\":\"sc_command_result\",\"admission\":\"P\",\"availability\":\"support_dump\",\"execution\":\"native_frame\",\"evidence\":\"dispatch_only\",\"ack\":\"terminal_consumed\",\"v1\":{\"opcodes\":[46,47,48,49],\"release\":\"legacy_release\"},\"render\":\"condump AP_SUPPORT_FILE.txt\",\"filename_argument\":false,\"revision_types\":[{\"revision\":1,\"payload_variants\":[\"support_dump_support_dump_payload\"]}]}", Admission::P, Execution::native_frame, Evidence::dispatch_only, Ack::terminal_consumed, policies_support_dump, std::size(policies_support_dump), &decode_variant<support_dump_payload, Payload>, &encode_variant<support_dump_payload, Payload>, &decode_variant<sc_command_result, Result>, &encode_variant<sc_command_result, Result>, &native_matches<::sc_command_request>},
};
const size_t descriptor_count = std::size(descriptors);
}
