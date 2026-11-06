#include "client_payload.h"
#include "commands.h"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace sentinel {
namespace {
using namespace generated;
template<class V, class T> bool pack(const T& value, Bytes& bytes) { return encode(V{value}, bytes); }
bool weapon_points(const ::sc_weapon_points_request& v, Bytes& bytes) {
    switch (v.kind) {
    case 0: {
        weapon_points_observe_payload out{};
        return pack<weapon_points_payload>(out, bytes);
    }
    case 1: {
        weapon_points_grant_payload out{};
        out.amount = v.amount;
        out.expected_gained = v.expected_gained;
        return pack<weapon_points_payload>(out, bytes);
    }
    default: return false;
    }
}
bool inventory(const ::sc_inventory_request& v, Bytes& bytes) {
    switch (v.kind) {
    case 0: {
        inventory_observe_payload out{};
        return pack<inventory_payload>(out, bytes);
    }
    case 1: {
        inventory_ensure_payload out{};
        out.weapons = v.weapons;
        out.equipment = v.equipment;
        return pack<inventory_payload>(out, bytes);
    }
    case 2: {
        inventory_set_capacity_payload out{};
        out.health_tier = v.health_tier;
        out.armor_tier = v.armor_tier;
        out.ammo_tier = v.ammo_tier;
        return pack<inventory_payload>(out, bytes);
    }
    default: return false;
    }
}
bool arsenal(const ::sc_arsenal_request& v, Bytes& bytes) {
    switch (v.kind) {
    case 0: {
        arsenal_observe_payload out{};
        return pack<arsenal_payload>(out, bytes);
    }
    case 1: {
        arsenal_ensure_mods_payload out{};
        out.mods = v.mods;
        return pack<arsenal_payload>(out, bytes);
    }
    case 2: {
        arsenal_select_mod_payload out{};
        out.weapon = v.select_weapon;
        out.mod = v.select_mod;
        return pack<arsenal_payload>(out, bytes);
    }
    case 4: {
        arsenal_project_mastery_payload out{};
        out.masteries = v.masteries;
        return pack<arsenal_payload>(out, bytes);
    }
    default: return false;
    }
}
bool runes(const ::sc_runes_request& v, Bytes& bytes) {
    switch (v.kind) {
    case 0: {
        runes_observe_payload out{};
        return pack<runes_payload>(out, bytes);
    }
    case 1: {
        runes_ensure_normal_payload out{};
        out.normal_runes = v.normal_runes;
        return pack<runes_payload>(out, bytes);
    }
    case 2: {
        runes_ensure_support_payload out{};
        out.support_runes = v.support_runes;
        return pack<runes_payload>(out, bytes);
    }
    case 3: {
        runes_select_normal_payload out{};
        out.slot = v.select_slot;
        out.rune = v.select_rune;
        return pack<runes_payload>(out, bytes);
    }
    case 4: {
        runes_select_support_payload out{};
        out.rune = v.select_rune;
        return pack<runes_payload>(out, bytes);
    }
    default: return false;
    }
}
bool special(const ::sc_special_request& v, Bytes& bytes) {
    switch (v.kind) {
    case 0: {
        special_observe_payload out{};
        return pack<special_payload>(out, bytes);
    }
    case 1: {
        special_ensure_ownership_payload out{};
        out.crucible = v.own_crucible;
        out.hammer = v.own_hammer;
        out.hammer_tier = v.hammer_tier;
        return pack<special_payload>(out, bytes);
    }
    case 2: {
        special_select_payload out{};
        out.selected = v.selected;
        return pack<special_payload>(out, bytes);
    }
    case 3: {
        special_refill_status_payload out{};
        return pack<special_payload>(out, bytes);
    }
    case 4: {
        special_refill_publish_payload out{};
        out.balance = v.refill_balance;
        out.flags = v.refill_flags;
        return pack<special_payload>(out, bytes);
    }
    case 5: {
        special_refill_take_payload out{};
        return pack<special_payload>(out, bytes);
    }
    case 6: {
        special_refill_execute_payload out{};
        out.request_id = v.refill_request_id;
        out.authorize = v.refill_authorize;
        return pack<special_payload>(out, bytes);
    }
    default: return false;
    }
}
bool deathlink(const ::sc_deathlink_request& v, Bytes& bytes) {
    switch (v.kind) {
    case 0: {
        deathlink_observe_payload out{};
        return pack<deathlink_payload>(out, bytes);
    }
    case 1: {
        deathlink_configure_payload out{};
        out.enabled = v.enabled;
        out.mode = v.mode;
        return pack<deathlink_payload>(out, bytes);
    }
    case 2: {
        deathlink_apply_remote_payload out{};
        out.event_id = v.event_id;
        std::copy(std::begin(v.event_hash), std::end(v.event_hash), out.event_hash.begin());
        return pack<deathlink_payload>(out, bytes);
    }
    case 3: {
        deathlink_poll_local_payload out{};
        return pack<deathlink_payload>(out, bytes);
    }
    case 4: {
        deathlink_ack_local_payload out{};
        out.sequence = v.ack_sequence;
        return pack<deathlink_payload>(out, bytes);
    }
    case 5: {
        deathlink_cancel_remote_payload out{};
        out.event_id = v.event_id;
        return pack<deathlink_payload>(out, bytes);
    }
    default: return false;
    }
}
bool automap(const ::sc_automap_request& v, uint16_t revision, Bytes& bytes) {
    switch (v.kind) {
    case 1: {
        if (revision == 1) {
            automap_publish_payload out{};
            if (std::any_of(std::begin(v.checked_locations) + out.checked_locations.size(),
                    std::end(v.checked_locations), [](uint64_t bits) { return bits != 0; })) return false;
            out.known = v.known; out.revision = v.revision;
            std::copy_n(v.checked_locations, out.checked_locations.size(), out.checked_locations.begin());
            return pack<automap_payload_revision1>(out, bytes);
        }
        automap_publish_arena_payload out{};
        out.known = v.known;
        out.revision = v.revision;
        std::copy(std::begin(v.checked_locations), std::end(v.checked_locations), out.checked_locations.begin());
        return pack<automap_payload>(out, bytes);
    }
    case 2: {
        automap_observe_payload out{};
        return pack<automap_payload>(out, bytes);
    }
    default: return false;
    }
}
bool command(const ::sc_command_request& v, Bytes& bytes) {
    if (!commands::valid(v)) return false;
    std::istringstream words(v.text); std::string name, first, second, third;
    words >> name >> first >> second >> third;
    switch (v.kind) {
    case SC_COMMAND_ACTIVATE: return pack<command_payload>(command_activate_payload{first}, bytes);
    case SC_COMMAND_PLAYER_PERK: return pack<command_payload>(command_player_perk_payload{third}, bytes);
    case SC_COMMAND_AMMO_REFILL: return pack<command_payload>(command_ammo_refill_payload{}, bytes);
    case SC_COMMAND_CRUCIBLE_REFILL: return pack<command_payload>(command_crucible_refill_payload{}, bytes);
    case SC_COMMAND_ECHO: return pack<command_payload>(command_echo_payload{std::string(v.text + 5)}, bytes);
    case SC_COMMAND_CONDUMP: return pack<command_payload>(command_condump_payload{first}, bytes);
    case SC_COMMAND_LIST_INVENTORY: return pack<command_payload>(command_list_inventory_payload{}, bytes);
    case SC_COMMAND_TRANSIENT_POLICY: {
        const uint32_t variable = name == "g_damageScaleAllToAI" ? 0 : (name == "g_damageScaleAllToSlayer" ? 1 : 2);
        return pack<command_payload>(command_transient_policy_payload{variable, first}, bytes);
    }
    default: return false;
    }
}
bool campaign_commit(uint16_t revision,const sc_campaign_request& request,Bytes& bytes) {
    if (revision<=3) return pack<campaign_commit_payload_revision1>(campaign_commit_campaign_commit_payload{request.revision},bytes);
    if (revision==4) return pack<campaign_commit_payload_revision4>(campaign_commit_selection_payload{request.revision,request.count,request.row.id},bytes);
    return pack<campaign_commit_payload>(campaign_commit_stage_payload{request.revision,request.count,request.row.id},bytes);
}
bool campaign(uint16_t type, uint16_t revision, const NativeCampaign& v, Bytes& bytes) {
    const auto& r = v.request;
    if (type == 24) return pack<campaign_payload>(campaign_campaign_payload{r.revision}, bytes);
    if (type == 23) return campaign_commit(revision,r,bytes);
    campaign_row_stage_payload out{r.revision, r.index, r.count, to_wire(r.row), to_wire(v.summary), v.rewards.known, {}};
    if (v.rewards.count > std::size(v.rewards.entries)) return false;
    for (size_t i = 0; i < v.rewards.count; ++i) {
        const auto& from = v.rewards.entries[i]; campaign_reward reward{};
        reward.location_id = from.location_id; reward.kind = from.kind; reward.checked = from.checked;
        std::copy(std::begin(from.unlockable), std::end(from.unlockable), reward.unlockable.begin());
        std::copy(std::begin(from.name), std::end(from.name), reward.name.begin());
        std::copy(std::begin(from.text), std::end(from.text), reward.text.begin()); out.rewards.push_back(reward);
    }
    if (revision == 1) return pack<campaign_row_payload_revision1>(campaign_row_base_payload{r.revision,r.index,r.count,to_wire(r.row)}, bytes);
    if (revision == 2) return pack<campaign_row_payload_revision2>(campaign_row_presentation_payload{r.revision,r.index,r.count,to_wire(r.row),to_wire(v.summary)}, bytes);
    if (revision == 3) return pack<campaign_row_payload_revision3>(campaign_row_campaign_row_payload{r.revision,r.index,r.count,to_wire(r.row),to_wire(v.summary),out.rewards}, bytes);
    if (revision == 4) return pack<campaign_row_payload_revision4>(campaign_row_known_rewards_payload{r.revision,r.index,r.count,to_wire(r.row),to_wire(v.summary),out.rewards_known,out.rewards}, bytes);
    return pack<campaign_row_payload>(out, bytes);
}
template<class T> bool restore(const Bytes& bytes, T& out) {
    decltype(to_wire(out)) value{};
    if (!decode(bytes, value)) return false;
    out = from_wire(value); return true;
}
}
namespace {
bool combat_payload_bytes(const generated::NativeRequest& value, generated::Bytes& bytes) {
    const auto& v = std::get<::sc_combat_query>(value); generated::combat_combat_payload out{};
    if (v.map_count > std::size(v.maps)) return false;
    for (size_t i = 0; i < v.map_count; ++i) {
        generated::combat_map_identifier map{};
        map.value.assign(v.maps[i], strnlen(v.maps[i], SC_CONTEXT_MAP_CAPACITY)); out.maps.push_back(map);
    }
    return pack<generated::combat_payload>(out, bytes);
}
using Encode = bool (*)(uint16_t, uint16_t, const generated::NativeRequest&, generated::Bytes&);
struct Encoder { uint16_t type; Encode encode; };
const Encoder encoders[] = {
    {1, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<inspect_payload>(inspect_inspect_payload{}, bytes);
    }},
    {2, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<engine_payload>(engine_engine_payload{}, bytes);
    }},
    {3, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<context_payload>(context_context_payload{}, bytes);
    }},
    {4, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<native_payload>(native_native_payload{std::get<NativeRead>(value).cursor}, bytes);
    }},
    {5, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<diagnostic_payload>(diagnostic_diagnostic_payload{}, bytes);
    }},
    {8, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<diagnostic_detail_payload>(diagnostic_detail_diagnostic_detail_payload{}, bytes);
    }},
    {11, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<save_payload>(save_save_payload{}, bytes);
    }},
    {12, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<save_admission_payload>(save_admission_save_admission_payload{}, bytes);
    }},
    {13, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<save_write_payload>(save_write_save_write_payload{std::get<NativeRead>(value).cursor}, bytes);
    }},
    {14, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<save_backup_payload>(save_backup_save_backup_payload{std::get<::sc_save_backup_request>(value).campaign, std::get<::sc_save_backup_request>(value).slot, std::get<::sc_save_backup_request>(value).work_deadline_ms}, bytes);
    }},
    {17, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<save_installation_payload>(save_installation_save_installation_payload{}, bytes);
    }},
    {18, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return weapon_points(std::get<::sc_weapon_points_request>(value), bytes);
    }},
    {22, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return campaign(type, revision, std::get<NativeCampaign>(value), bytes);
    }},
    {23, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return campaign(type, revision, std::get<NativeCampaign>(value), bytes);
    }},
    {24, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return campaign(type, revision, std::get<NativeCampaign>(value), bytes);
    }},
    {25, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return inventory(std::get<::sc_inventory_request>(value), bytes);
    }},
    {29, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return arsenal(std::get<::sc_arsenal_request>(value), bytes);
    }},
    {33, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return runes(std::get<::sc_runes_request>(value), bytes);
    }},
    {37, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return special(std::get<::sc_special_request>(value), bytes);
    }},
    {41, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return deathlink(std::get<::sc_deathlink_request>(value), bytes);
    }},
    {45, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; return automap(std::get<::sc_automap_request>(value), revision, bytes);
    }},
    {46, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return command(std::get<::sc_command_request>(value), bytes);
    }},
    {51, +[](uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
        (void)type; (void)revision; (void)value; return pack<combat_presentation_payload>(combat_presentation_combat_presentation_payload{to_wire(std::get<::sc_combat_presentation_request>(value))}, bytes);
    }},
    {50, +[](uint16_t, uint16_t, const generated::NativeRequest& value, generated::Bytes& bytes) { return combat_payload_bytes(value, bytes); }},
};
using Decode = bool (*)(const generated::Bytes&, Inspection&);
struct Decoder { uint16_t type; Decode decode; };
const Decoder decoders[] = {
    {1, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.snapshot.core); }},
    {2, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.engine); }},
    {3, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.context); }},
    {4, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.native); }},
    {5, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.diagnostic); }},
    {11, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.save); }},
    {13, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.write); }},
    {14, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.backup); }},
    {17, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.installation); }},
    {18, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.weapon_points); }},
    {22, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.campaign); }},
    {23, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.campaign); }},
    {24, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.campaign); }},
    {25, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.inventory); }},
    {29, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.arsenal); }},
    {33, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.runes); }},
    {37, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.special); }},
    {41, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.deathlink); }},
    {45, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.automap); }},
    {46, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.command); }},
    {50, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.combat); }},
    {51, +[](const generated::Bytes& bytes, Inspection& out) { return restore(bytes, out.combat_presentation); }},
    {8, +[](const generated::Bytes& bytes, Inspection& out) {
        generated::diagnostic_detail_result value{};
        if (!generated::decode(bytes, value)) return false;
        out.diagnostic = generated::from_wire(value.execution); out.detail = generated::from_wire(value.detail); return true;
    }},
    {12, +[](const generated::Bytes& bytes, Inspection& out) {
        generated::save_admission_result value{};
        if (!generated::decode(bytes, value)) return false;
        out.admission = generated::from_wire(value.observation); return true;
    }},
};
}
bool client_payload(uint16_t type, uint16_t revision, const generated::NativeRequest& value, generated::Bytes& bytes) {
    const auto end = std::end(encoders);
    const auto found = std::find_if(std::begin(encoders), end, [=](const auto& entry) { return entry.type == type; });
    return found != end && found->encode(type, revision, value, bytes);
}
bool client_result(uint16_t type, const generated::Bytes& bytes, Inspection& out) {
    const auto end = std::end(decoders);
    const auto found = std::find_if(std::begin(decoders), end, [=](const auto& entry) { return entry.type == type; });
    return found != end && found->decode(bytes, out);
}
bool client_publication(uint16_t type) {
    return std::any_of(generated::descriptors, generated::descriptors + generated::descriptor_count,
        [=](const auto& entry) { return entry.id == type && entry.execution == generated::Execution::publication; });
}
ProbeResult client_transport(const V2Response& response) {
    if (response.transport != ProbeResult::ok) return response.transport;
    switch (response.wire) {
    case WireResult::ok: return ProbeResult::ok;
    case WireResult::incompatible_protocol: return ProbeResult::incompatible_protocol;
    case WireResult::capability_unavailable: case WireResult::unsupported_operation: return ProbeResult::capability_unavailable;
    default: return ProbeResult::invalid_response;
    }
}
}
