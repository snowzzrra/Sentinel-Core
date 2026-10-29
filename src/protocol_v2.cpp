#include "protocol_v2.h"
#include "commands.h"
#include <algorithm>
#include <cstring>
#include <windows.h>
#include <bcrypt.h>

namespace sentinel::v2 {
namespace {
uint32_t number(const Bytes& bytes, size_t offset, size_t width) {
    uint32_t out = 0;
    for (size_t i = 0; i < width; ++i) out |= uint32_t(bytes[offset + i]) << (i * 8);
    return out;
}
bool split(const Bytes& bytes, size_t size, Bytes& prefix, Bytes& suffix) {
    if (size > bytes.size()) return false;
    prefix.assign(bytes.begin(), bytes.begin() + size);
    suffix.assign(bytes.begin() + size, bytes.end()); return true;
}
template<class T> void namespace_copy(T& out, const submission& input) {
    std::copy(input.reference.namespace_id.begin(), input.reference.namespace_id.end(), out.namespace_id);
}
::sc_diagnostic_request execution(const submission& input) {
    ::sc_diagnostic_request out{};
    const auto& scope = input.reference.scope;
    out.expected = {scope.pid, scope.process_created, {}, scope.generation};
    std::copy(scope.instance_id.begin(), scope.instance_id.end(), out.expected.instance_id);
    out.request_id = 1; out.nonce[0] = 1; out.deadline_ms = input.deadline_ms;
    return out;
}
template<class T> T request(const submission& input, uint32_t kind) {
    T out{}; out.execution = execution(input); out.kind = kind; namespace_copy(out, input); return out;
}
NativeRequest points(const Bytes& bytes, const submission& input) {
    weapon_points_payload payload; decode(bytes, payload);
    auto out = request<::sc_weapon_points_request>(input, static_cast<uint32_t>(payload.index()));
    if (const auto* value = std::get_if<weapon_points_grant_payload>(&payload)) {
        out.amount = value->amount; out.expected_gained = value->expected_gained;
    }
    return out;
}
NativeRequest inventory(const Bytes& bytes, const submission& input) {
    inventory_payload payload; decode(bytes, payload);
    auto out = request<::sc_inventory_request>(input, static_cast<uint32_t>(payload.index()));
    if (const auto* value = std::get_if<inventory_ensure_payload>(&payload)) {
        out.weapons = value->weapons; out.equipment = value->equipment;
    }
    if (const auto* value = std::get_if<inventory_set_capacity_payload>(&payload)) {
        out.health_tier = value->health_tier; out.armor_tier = value->armor_tier; out.ammo_tier = value->ammo_tier;
    }
    return out;
}
NativeRequest arsenal(const Bytes& bytes, const submission& input) {
    arsenal_payload payload; decode(bytes, payload);
    auto out = request<::sc_arsenal_request>(input, number(bytes, 0, 4));
    if (const auto* value = std::get_if<arsenal_ensure_mods_payload>(&payload)) out.mods = value->mods;
    if (const auto* value = std::get_if<arsenal_select_mod_payload>(&payload)) { out.select_weapon = value->weapon; out.select_mod = value->mod; }
    if (const auto* value = std::get_if<arsenal_project_mastery_payload>(&payload)) out.masteries = value->masteries;
    return out;
}
NativeRequest runes(const Bytes& bytes, const submission& input) {
    runes_payload payload; decode(bytes, payload);
    auto out = request<::sc_runes_request>(input, number(bytes, 0, 4));
    if (const auto* value = std::get_if<runes_ensure_normal_payload>(&payload)) out.normal_runes = value->normal_runes;
    if (const auto* value = std::get_if<runes_ensure_support_payload>(&payload)) out.support_runes = value->support_runes;
    if (const auto* value = std::get_if<runes_select_normal_payload>(&payload)) { out.select_slot = value->slot; out.select_rune = value->rune; }
    if (const auto* value = std::get_if<runes_select_support_payload>(&payload)) out.select_rune = value->rune;
    return out;
}
NativeRequest special(const Bytes& bytes, const submission& input) {
    special_payload payload; decode(bytes, payload);
    auto out = request<::sc_special_request>(input, number(bytes, 0, 4));
    if (const auto* v = std::get_if<special_ensure_ownership_payload>(&payload)) { out.own_crucible = v->crucible; out.own_hammer = v->hammer; out.hammer_tier = v->hammer_tier; }
    if (const auto* v = std::get_if<special_select_payload>(&payload)) out.selected = v->selected;
    if (const auto* v = std::get_if<special_refill_publish_payload>(&payload)) { out.refill_balance = v->balance; out.refill_flags = v->flags; }
    if (const auto* v = std::get_if<special_refill_execute_payload>(&payload)) { out.refill_request_id = v->request_id; out.refill_authorize = v->authorize; }
    return out;
}
NativeRequest deathlink(const Bytes& bytes, const submission& input) {
    deathlink_payload payload; decode(bytes, payload);
    auto out = request<::sc_deathlink_request>(input, number(bytes, 0, 4));
    if (const auto* v = std::get_if<deathlink_configure_payload>(&payload)) { out.enabled = v->enabled; out.mode = v->mode; }
    if (const auto* v = std::get_if<deathlink_apply_remote_payload>(&payload)) { out.event_id = v->event_id; std::copy(v->event_hash.begin(), v->event_hash.end(), out.event_hash); }
    if (const auto* v = std::get_if<deathlink_ack_local_payload>(&payload)) out.ack_sequence = v->sequence;
    if (const auto* v = std::get_if<deathlink_cancel_remote_payload>(&payload)) out.event_id = v->event_id;
    return out;
}
std::string command_text(const command_payload& payload) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, command_activate_payload>) return "ai_ScriptCmdEnt " + v.entity + " activate";
        else if constexpr (std::is_same_v<T, command_player_perk_payload>) return "ai_ScriptCmdEnt player1 givePlayerPerk " + v.perk;
        else if constexpr (std::is_same_v<T, command_ammo_refill_payload>) return "give ammo";
        else if constexpr (std::is_same_v<T, command_crucible_refill_payload>) return "judgementMeter_Set 3";
        else if constexpr (std::is_same_v<T, command_echo_payload>) return "echo " + v.text;
        else if constexpr (std::is_same_v<T, command_condump_payload>) return "condump " + v.filename;
        else if constexpr (std::is_same_v<T, command_list_inventory_payload>) return "listInventory player1";
        else {
            constexpr const char* names[] = {"g_damageScaleAllToAI", "g_damageScaleAllToSlayer", "g_infiniteAmmo"};
            return std::string(names[v.variable]) + " " + v.value;
        }
    }, payload);
}
NativeRequest command(const Bytes& bytes, const submission& input) {
    auto out = request<::sc_command_request>(input, 0);
    bool valid = false;
    if (input.reference.command_type == 52) valid = commands::parse("condump AP_SUPPORT_FILE.txt", out);
    else { command_payload payload; decode(bytes, payload); valid = commands::parse(command_text(payload), out); }
    return valid ? NativeRequest{out} : NativeRequest{};
}
struct CanonicalWriter {
    Bytes& bytes; bool valid = true;
    template<class T> void scalar(const T& value, size_t width) {
        auto n = static_cast<uint64_t>(value);
        for (size_t i = 0; i < width; ++i) { bytes.push_back(static_cast<uint8_t>(n)); n >>= 8; }
    }
    template<size_t N> void text(const char (&value)[N]) {
        const auto count = strnlen(value, N);
        if (count == N) { valid = false; return; }
        scalar(count, 2); bytes.insert(bytes.end(), value, value + count);
    }
};
template<class T, class = void> struct HasTransfer : std::false_type {};
template<class T> struct HasTransfer<T, std::void_t<decltype(native_transfer(std::declval<CanonicalWriter&>(), std::declval<const T&>()))>> : std::true_type {};
bool sha256(const Bytes& bytes, std::array<uint8_t, 32>& out) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, out.data(), static_cast<ULONG>(out.size()), 0);
    if (hash && BCryptDestroyHash(hash) < 0) status = -1;
    if (BCryptCloseAlgorithmProvider(algorithm, 0) < 0) status = -1;
    return status >= 0;
}
}
bool split_submission(const Bytes& bytes, submission& out, Bytes& payload) {
    if (bytes.size() < 102) return false;
    Bytes prefix;
    return split(bytes, submission_fixed + number(bytes, 100, 2), prefix, payload) && decode(prefix, out);
}
bool split_read(const Bytes& bytes, read_envelope& out, Bytes& payload) {
    Bytes prefix; return split(bytes, 120, prefix, payload) && decode(prefix, out);
}
bool header(const Bytes& bytes, wire_header_2& out, bool response) {
    return decode(bytes, out) && out.magic == wire_magic && out.version == 2 &&
        out.flags == (response ? 1u : 0u) && (response || !out.result) && out.payload_length <= max_frame - header_size_2;
}
bool frame(const wire_header_2& input, const Bytes& body, Bytes& out) {
    if (body.size() > max_frame - header_size_2) return false;
    auto head = input; head.payload_length = static_cast<uint32_t>(body.size());
    if (!encode(head, out)) return false;
    out.insert(out.end(), body.begin(), body.end()); return true;
}
bool fingerprint(const submission& input, const NativeRequest& payload, std::array<uint8_t, 32>& out) {
    const auto& ref = input.reference;
    immutable_submission immutable{ref.scope, ref.namespace_id, ref.command_type, ref.payload_revision, input.save_binding, input.deadline_ms};
    Bytes bytes; if (!encode(immutable, bytes)) return false;
    CanonicalWriter writer{bytes};
    const bool supported = std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (HasTransfer<T>::value) { native_transfer(writer, value); return true; }
        else return std::is_same_v<T, ::sc_diagnostic_request>;
    }, payload);
    return supported && writer.valid && sha256(bytes, out);
}
bool encode_native_result(const NativeResult& result, Bytes& bytes) {
    return std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) { bytes.clear(); return true; }
        else return encode(to_wire(value), bytes);
    }, result);
}
bool command_response(const Bytes& bytes, command_reply& reply, CommandError& error, generated::sc_diagnostic_result& execution, Bytes& value) {
    if (bytes.size() < 118) return false;
    const auto length = reply_fixed + number(bytes, 116, 2);
    Bytes prefix, tail;
    if (!split(bytes, length, prefix, tail) || !decode(prefix, reply) || tail.size() < 4) return false;
    const auto status = number(tail, 0, 4);
    if (status > static_cast<uint32_t>(CommandError::effect_unknown)) return false;
    error = static_cast<CommandError>(status);
    // Execution facts have a fixed schema width including the bounded map array.
    generated::sc_diagnostic_result empty{}; Bytes encoded;
    if (!encode(empty, encoded) || tail.size() < 4 + encoded.size()) return false;
    prefix.assign(tail.begin() + 4, tail.begin() + 4 + encoded.size());
    value.assign(tail.begin() + 4 + encoded.size(), tail.end());
    return decode(prefix, execution);
}
}
namespace sentinel::v2 {
namespace {
NativeRequest backup(const Bytes& bytes, const submission& input) {
    save_backup_payload payload; decode(bytes, payload); const auto& v = std::get<0>(payload);
    ::sc_save_backup_request out{}; out.execution = execution(input); namespace_copy(out, input);
    out.campaign = v.campaign; out.slot = v.slot; out.work_deadline_ms = v.work_deadline_ms; return out;
}
NativeRequest automap(const Bytes& bytes, const submission& input) {
    automap_payload payload; decode(bytes, payload);
    auto out = request<::sc_automap_request>(input, number(bytes, 0, 4));
    if (const auto* v = std::get_if<automap_publish_payload>(&payload)) {
        out.known = v->known; out.revision = v->revision;
        std::copy(v->checked_locations.begin(), v->checked_locations.end(), out.checked_locations);
    }
    return out;
}
template<class V> void campaign_row(const V& v, NativeCampaign& out) {
    out.request.revision = v.revision; out.request.index = v.index; out.request.count = v.count;
    out.request.row = from_wire(v.row);
}
NativeRequest campaign(const Bytes& bytes, const submission& input) {
    NativeCampaign out{}; out.request.execution = execution(input); namespace_copy(out.request, input);
    if (input.reference.command_type != 22) {
        campaign_payload payload; decode(bytes, payload); out.request.revision = std::get<0>(payload).revision; return out;
    }
    if (input.reference.payload_revision == 1) {
        campaign_row_payload_revision1 payload; decode(bytes, payload); campaign_row(std::get<0>(payload), out);
    } else if (input.reference.payload_revision == 2) {
        campaign_row_payload_revision2 payload; decode(bytes, payload); const auto& v = std::get<0>(payload);
        campaign_row(v, out); out.summary = from_wire(v.summary);
    } else {
        campaign_row_payload payload; decode(bytes, payload); const auto& v = std::get<0>(payload);
        campaign_row(v, out); out.summary = from_wire(v.summary); out.request.reward_presentation = 1; out.rewards.known = 1;
        out.rewards.count = static_cast<uint32_t>(v.rewards.size());
        for (size_t i = 0; i < v.rewards.size(); ++i) {
            const auto& row = v.rewards[i]; auto& to = out.rewards.entries[i];
            to.location_id = row.location_id; to.kind = row.kind; to.checked = row.checked;
            std::copy(row.unlockable.begin(), row.unlockable.end(), to.unlockable);
            std::copy(row.name.begin(), row.name.end(), to.name); std::copy(row.text.begin(), row.text.end(), to.text);
        }
    }
    return out;
}
NativeRequest combat(const Bytes& bytes, const submission& input) {
    combat_payload payload; decode(bytes, payload); const auto& v = std::get<0>(payload);
    ::sc_combat_query out{}; namespace_copy(out, input); out.map_count = static_cast<uint32_t>(v.maps.size());
    for (size_t i = 0; i < v.maps.size(); ++i) std::copy(v.maps[i].value.begin(), v.maps[i].value.end(), out.maps[i]);
    return out;
}
NativeRequest presentation(const Bytes& bytes, const submission& input) {
    combat_presentation_payload payload; decode(bytes, payload);
    auto out = from_wire(std::get<0>(payload).presentation);
    out.execution = execution(input); namespace_copy(out, input); return out;
}
using Adapter = NativeRequest (*)(const Bytes&, const submission&);
struct PayloadAdapter { uint16_t id; Adapter convert; };
const PayloadAdapter adapters[] = {{14, backup}, {18, points}, {22, campaign}, {23, campaign}, {24, campaign},
    {25, inventory}, {29, arsenal}, {33, runes}, {37, special}, {41, deathlink}, {45, automap}, {46, command},
    {50, combat}, {51, presentation}, {52, command}};
}
bool native_payload(const Descriptor& descriptor, const Bytes& bytes, const submission& input, NativeRequest& out, Operation operation) {
    Payload payload;
    if (!descriptor.decode_payload(bytes, payload) || bytes.size() < 4) return false;
    const auto tag = number(bytes, 0, 4);
    const auto end = descriptor.variants + descriptor.variant_count;
    const auto policy = std::find_if(descriptor.variants, end, [=](const auto& v) { return v.tag == tag; });
    if (policy == end || policy->operation != operation) return false;
    const auto adapter = std::find_if(std::begin(adapters), std::end(adapters), [&](const auto& v) { return v.id == descriptor.id; });
    if (adapter != std::end(adapters)) {
        out = adapter->convert(bytes, input);
        return !std::holds_alternative<std::monostate>(out);
    }
    else if (descriptor.id == 5 || descriptor.id == 8) out = execution(input);
    else {
        uint64_t cursor = 0;
        if (descriptor.id == 4) cursor = std::get<0>(std::get<generated::native_payload>(payload)).after_event;
        if (descriptor.id == 13) cursor = std::get<0>(std::get<save_write_payload>(payload)).operation_id;
        out = NativeRead{cursor};
    }
    return true;
}
}
