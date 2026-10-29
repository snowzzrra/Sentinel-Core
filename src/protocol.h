#pragma once
#include "sentinel_inspection.h"
#include <variant>

namespace sentinel {
using Message = std::array<uint8_t, max_message>;
constexpr size_t header_size = 16;
struct CampaignRequest {
    sc_campaign_request request{};
    sc_campaign_summary summary{};
    sc_campaign_rewards rewards{};
};
using RequestPayload = std::variant<std::monostate, sc_save_backup_request, sc_weapon_points_request,
    CampaignRequest, sc_inventory_request, sc_arsenal_request, sc_runes_request, sc_special_request,
    sc_deathlink_request, sc_automap_request, sc_command_request, sc_combat_presentation_request, sc_combat_query>;
struct DecodedRequest {
    uint16_t operation = inspect_operation;
    uint64_t capability = 0;
    sc_diagnostic_request execution{};
    uint64_t cursor = 0;
    RequestPayload payload{};
};
WireResult decode_request(const Message&, size_t, DecodedRequest&);
size_t encode_request(Message& out, uint64_t required, uint16_t version = wire_version,
                      uint16_t operation = inspect_operation);
WireResult decode_request(const Message& in, size_t size, uint16_t* operation = nullptr,
                          sc_diagnostic_request* diagnostic = nullptr, uint64_t* after_event = nullptr,
                          uint64_t* write_id = nullptr, sc_save_backup_request* backup = nullptr,
                          sc_weapon_points_request* points = nullptr, sc_campaign_request* campaign = nullptr,
                          sc_inventory_request* inventory = nullptr, sc_arsenal_request* arsenal = nullptr,
                          sc_runes_request* runes = nullptr, sc_special_request* special = nullptr,
                          sc_deathlink_request* deathlink = nullptr, sc_automap_request* automap = nullptr,
                          sc_campaign_summary* summary = nullptr, sc_campaign_rewards* rewards = nullptr,
                          sc_command_request* command = nullptr,
                          sc_combat_presentation_request* combat_presentation = nullptr,
                          sc_combat_query* combat = nullptr);
size_t encode_combat_presentation_request(Message&, const sc_combat_presentation_request&);
size_t encode_combat_presentation_response(Message&, WireResult, const Snapshot&, const sc_combat_presentation_result&);
bool decode_combat_presentation_response(const Message&, size_t, WireResult&, Snapshot&, sc_combat_presentation_result&);
size_t encode_command_request(Message&, uint16_t, const sc_command_request&);
size_t encode_command_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_command_result&);
bool decode_command_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_command_result&);
size_t encode_runes_request(Message&, uint16_t, const sc_runes_request&);
size_t encode_runes_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_runes_result&);
bool decode_runes_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_runes_result&);
size_t encode_special_request(Message&, uint16_t, const sc_special_request&);
size_t encode_special_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_special_result&);
bool decode_special_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_special_result&);
size_t encode_deathlink_request(Message&, uint16_t, const sc_deathlink_request&);
size_t encode_deathlink_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_deathlink_result&);
bool decode_deathlink_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_deathlink_result&);
size_t encode_campaign_request(Message&, uint16_t, const sc_campaign_request&, const sc_campaign_summary* = nullptr,
                               const sc_campaign_rewards* = nullptr);
size_t encode_campaign_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_campaign_result&);
bool decode_campaign_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_campaign_result&);
size_t encode_arsenal_request(Message&, uint16_t, const sc_arsenal_request&);
size_t encode_arsenal_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_arsenal_result&);
bool decode_arsenal_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_arsenal_result&);
size_t encode_inventory_request(Message&, uint16_t, const sc_inventory_request&);
size_t encode_inventory_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_inventory_result&);
bool decode_inventory_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_inventory_result&);
size_t encode_weapon_points_request(Message&, uint16_t, const sc_weapon_points_request&);
size_t encode_weapon_points_response(Message&, WireResult, uint16_t, const Snapshot&, const sc_weapon_points_result&);
bool decode_weapon_points_response(const Message&, size_t, WireResult&, uint16_t, Snapshot&, sc_weapon_points_result&);
size_t encode_backup_request(Message&, uint16_t operation, const sc_save_backup_request&);
size_t encode_backup_response(Message&, WireResult, uint16_t operation, const Snapshot&, const sc_save_backup_snapshot&);
bool decode_backup_response(const Message&, size_t, WireResult&, uint16_t operation, Snapshot&, sc_save_backup_snapshot&);
size_t encode_save_write_request(Message&, uint64_t operation_id);
size_t encode_combat_request(Message&, uint32_t pid, const sc_combat_query&);
size_t encode_combat_response(Message&, WireResult, const Snapshot&, const sc_combat_snapshot&);
bool decode_combat_response(const Message&, size_t, WireResult&, Snapshot&, sc_combat_snapshot&);
size_t encode_native_request(Message& out, uint16_t operation,
                              const sc_diagnostic_request& request, uint64_t after_event = 0);
size_t encode_native_response(Message& out, WireResult result, uint16_t operation,
                              const Snapshot& snapshot, const sc_native_snapshot& native,
                              const sc_diagnostic_result& diagnostic, const sc_diagnostic_detail& detail = {});
bool decode_native_response(const Message& in, size_t size, WireResult& result,
                             uint16_t operation, Snapshot& snapshot,
                             sc_native_snapshot& native, sc_diagnostic_result& diagnostic,
                             sc_diagnostic_detail* detail = nullptr);
size_t encode_response(Message& out, WireResult result, const Snapshot& snapshot);
bool decode_response(const Message& in, size_t size, WireResult& result, Snapshot& snapshot);
size_t encode_engine_response(Message& out, WireResult result, const Snapshot& snapshot,
                              const sc_engine_snapshot& engine);
bool decode_engine_response(const Message& in, size_t size, WireResult& result,
                            Snapshot& snapshot, sc_engine_snapshot& engine);
size_t encode_context_response(Message& out, WireResult result, const Snapshot& snapshot,
                               const sc_context_snapshot& context);
bool decode_context_response(const Message& in, size_t size, WireResult& result,
                             Snapshot& snapshot, sc_context_snapshot& context);
size_t encode_save_response(Message& out, WireResult result, const Snapshot& snapshot,
                            const sc_save_snapshot& save);
bool decode_save_response(const Message& in, size_t size, WireResult& result,
                          Snapshot& snapshot, sc_save_snapshot& save);
size_t encode_save_admission_response(Message&, WireResult, const Snapshot&, const sc_save_admission_snapshot&);
bool decode_save_admission_response(const Message&, size_t, WireResult&, Snapshot&, sc_save_admission_snapshot&);
size_t encode_save_write_response(Message&, WireResult, const Snapshot&, const sc_save_write_snapshot&);
size_t encode_installation_response(Message&, WireResult, const Snapshot&, const sc_save_installation_snapshot&);
bool decode_installation_response(const Message&, size_t, WireResult&, Snapshot&, sc_save_installation_snapshot&);
bool decode_save_write_response(const Message&, size_t, WireResult&, Snapshot&, sc_save_write_snapshot&);
}

namespace sentinel {
size_t encode_automap_request(Message&, const sc_automap_request&);
size_t encode_automap_response(Message&, WireResult, const Snapshot&, const sc_automap_result&);
bool decode_automap_response(const Message&, size_t, WireResult&, Snapshot&, sc_automap_result&);
}
