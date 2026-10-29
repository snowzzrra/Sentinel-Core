#include "protocol.h"
#include "commands.h"
#include "combat_presentation.h"
#include "combat.h"
#include <cstring>
#include <algorithm>
#include "sentinel_protocol_generated.h"

namespace sentinel {
namespace {
constexpr uint32_t magic = 0x50494353; // "scip", little endian
struct Writer {
    Message& data;
    size_t pos = 0;
    bool valid = true;
    void number(uint64_t n, size_t width) {
        if (pos + width > data.size()) { valid = false; return; }
        for (size_t i = 0; i < width; ++i) { data[pos++] = static_cast<uint8_t>(n); n >>= 8; }
    }
    template<class T> void scalar(T& n, size_t width) { number(static_cast<uint64_t>(n), width); }
    void u16(uint16_t& n) { number(n, 2); }
    void u32(uint32_t& n) { number(n, 4); }
    void u64(uint64_t& n) { number(n, 8); }
    void byte(uint8_t& n) { number(n, 1); }
    void text(const char* value) {
        const size_t size = std::strlen(value);
        number(size, 2);
        for (size_t i = 0; i < size; ++i) number(static_cast<uint8_t>(value[i]), 1);
    }
};
struct Reader {
    const Message& data;
    size_t size, pos = 0;
    bool valid = true;
    uint64_t number(size_t width) {
        if (pos + width > size) { valid = false; return 0; }
        uint64_t result = 0;
        for (size_t i = 0; i < width; ++i) result |= uint64_t(data[pos++]) << (i * 8);
        return result;
    }
    template<class T> void scalar(T& n, size_t width) { n = static_cast<T>(number(width)); }
    void u16(uint16_t& n) { n = static_cast<uint16_t>(number(2)); }
    void u32(uint32_t& n) { n = static_cast<uint32_t>(number(4)); }
    void u64(uint64_t& n) { n = number(8); }
    void byte(uint8_t& n) { n = static_cast<uint8_t>(number(1)); }
    template<size_t N> void text(char (&out)[N]) {
        const size_t count = static_cast<size_t>(number(2));
        if (count == 0 || count >= N || pos + count > size) { valid = false; return; }
        for (size_t i = 0; i < count; ++i) {
            const auto c = data[pos++];
            if (c < 32 || c > 126) valid = false;
            out[i] = static_cast<char>(c);
        }
        out[count] = 0;
    }
};
void header(Writer& w, uint16_t version, uint16_t op, size_t payload, WireResult result) {
    generated::wire_header_1 value{magic, version, op, static_cast<uint32_t>(payload), static_cast<uint32_t>(result)};
    generated::v1_transfer_header(w, value);
}
bool response_header(Reader& r, uint16_t op, WireResult& result) {
    generated::wire_header_1 h{}; generated::v1_transfer_header(r, h);
    if (h.magic != magic || h.version != wire_version || h.operation != op || h.payload_length != r.size - header_size ||
        h.result > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(h.result); return true;
}

}
size_t encode_request(Message& out, uint64_t required, uint16_t version, uint16_t op) {
    Writer w{out}; header(w, version, op, 8, WireResult::ok); w.number(required, 8); return w.pos;
}
size_t encode_save_write_request(Message& out, uint64_t operation_id) {
    Writer w{out}; header(w, wire_version, save_write_operation, 16, WireResult::ok);
    w.number(save_write_capability, 8); w.number(operation_id, 8); return w.pos;
}
namespace {
bool namespace_valid(const char* value) {
    return !value[64] && std::all_of(value, value + 64, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool envelope_valid(const sc_diagnostic_request& e, bool allow_zero_generation) {
    const auto nonzero = [](const auto& bytes) {
        return std::any_of(std::begin(bytes), std::end(bytes), [](auto b) { return b != 0; });
    };
    return e.expected.pid && e.expected.process_created && e.request_id && nonzero(e.expected.instance_id) &&
        nonzero(e.nonce) && (allow_zero_generation || e.expected.lifecycle_generation) &&
        e.deadline_ms && e.deadline_ms <= SC_DIAGNOSTIC_MAX_DEADLINE_MS;
}
template<class T> bool request_valid(const T&) { return true; }
bool request_valid(const sc_save_backup_request& v) {
    return v.campaign <= 2 && v.slot <= 11 && v.work_deadline_ms && v.work_deadline_ms <= SC_SAVE_BACKUP_MAX_WORK_MS;
}
bool request_valid(const sc_automap_request& v) {
    return v.known <= 1 && v.kind >= SC_AUTOMAP_PUBLISH && v.kind <= SC_AUTOMAP_OBSERVE;
}
bool request_valid(const sc_command_request& v) { return commands::valid(v); }
bool request_valid(const sc_combat_presentation_request& v) { return combat::valid_presentation(v); }
template<class T, void (*Transfer)(Reader&, T&)>
WireResult decode_family(Reader& r, DecodedRequest& out) {
    T value{}; value.execution = out.execution;
    Transfer(r, value);
    if (!namespace_valid(value.namespace_id) || !request_valid(value)) return WireResult::malformed;
    out.payload = value;
    return WireResult::ok;
}
WireResult decode_empty(Reader&, DecodedRequest&) { return WireResult::ok; }
WireResult decode_cursor(Reader& r, DecodedRequest& out) {
    out.cursor = r.number(8); return WireResult::ok;
}
WireResult decode_combat(Reader& r, DecodedRequest& out) {
    out.execution.expected.pid = static_cast<uint32_t>(r.number(4));
    sc_combat_query value{};
    generated::v1_transfer_combat_query(r, value);
    if (value.map_count > SC_COMBAT_MAX_MAPS) return WireResult::malformed;
    generated::v1_transfer_combat_maps(r, value);
    if (!out.execution.expected.pid || !combat::valid_query(value)) return WireResult::malformed;
    out.payload = value; return WireResult::ok;
}
bool challenge_valid(const sc_physical_challenge& c, const sc_campaign_summary& s) {
    if (c.unlockable[79] || c.found > c.required || c.checked > 1 ||
        (bool(c.unlockable[0]) != bool(c.required)) || (s.known != 1 && c.required) ||
        (c.checked && !c.required)) return false;
    return std::all_of(std::begin(c.unlockable), std::end(c.unlockable), [](char b) {
        return !b || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '_' || b == '/';
    });
}
bool campaign_band_valid(uint32_t band) {
    return (band & 255) <= 5 && (band >> 8) <= 10001 &&
        (!(band >> 8) || ((band & 255) && (band & 255) <= 4));
}
bool summary_fields_valid(const sc_campaign_summary& s, uint32_t flags) {
    if (s.known > 2 || s.found > s.total || !campaign_band_valid(s.band)) return false;
    if ((!s.known && s.band) || (s.known != 1 && (s.found || s.total)) ||
        (!(flags & SC_CAMPAIGN_REVEALED) && (s.known == 1 || s.band))) return false;
    return true;
}
bool summary_valid(const sc_campaign_summary& s, uint32_t flags) {
    if (!summary_fields_valid(s, flags)) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (!challenge_valid(s.challenges[i], s)) return false;
        for (size_t j = 0; j < i; ++j)
            if (s.challenges[i].required && !std::strcmp(s.challenges[i].unlockable, s.challenges[j].unlockable)) return false;
    }
    return true;
}
unsigned utf8_continuations(uint8_t c) {
    if (c >= 0xc2 && c <= 0xdf) return 1;
    if (c >= 0xe0 && c <= 0xef) return 2;
    if (c >= 0xf0 && c <= 0xf4) return 3;
    return 0;
}
bool reward_codepoint(const char* text, size_t size, size_t& i, uint8_t c) {
    const unsigned n = utf8_continuations(c);
    if (!n || i + n >= size) return false;
    uint32_t cp = c & ((1u << (6 - n)) - 1);
    for (unsigned j = 0; j < n; ++j) {
        const auto b = static_cast<uint8_t>(text[i++]);
        if ((b & 0xc0) != 0x80) return false;
        cp = (cp << 6) | (b & 0x3f);
    }
    constexpr uint32_t minimum[] = {0, 0x80, 0x800, 0x10000};
    return cp >= minimum[n] && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}
bool reward_utf8(const char* text, size_t size) {
    if (!text[0] || text[size - 1]) return false;
    for (size_t i = 0; i < size && text[i];) {
        const auto c = static_cast<uint8_t>(text[i++]);
        if (c < 32 || c == 127) return false;
        if (c >= 128 && !reward_codepoint(text, size, i, c)) return false;
    }
    return true;
}
bool reward_kind_valid(const sc_campaign_reward& e, uint32_t flags) {
    return !(e.kind == SC_REWARD_AGGREGATE && std::strncmp(e.unlockable, "mission_challenge/", 18)) &&
        !(e.kind == SC_REWARD_ENCOUNTER && std::strncmp(e.unlockable, "encounter/", 10)) &&
        ((e.kind == SC_REWARD_MASTERY) == bool(flags & SC_CAMPAIGN_HUB));
}
bool campaign_identifier(const char* text, size_t size) {
    return std::all_of(text, text + size, [](char c) {
        return !c || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '/';
    });
}
bool reward_valid(const sc_campaign_reward& e, const CampaignRequest& v) {
    if (!e.location_id || e.checked > 1 || e.kind < 1 || e.kind > 4 ||
        (e.checked && !v.rewards.known) || e.unlockable[79] || !e.unlockable[0]) return false;
    if (!reward_kind_valid(e, v.request.row.flags)) return false;
    if (!reward_utf8(e.name, sizeof(e.name)) || !reward_utf8(e.text, sizeof(e.text))) return false;
    return campaign_identifier(e.unlockable, sizeof(e.unlockable));
}
bool rewards_valid(const CampaignRequest& v) {
    for (uint32_t i = 0; i < v.rewards.count; ++i) {
        const auto& e = v.rewards.entries[i];
        if (!reward_valid(e, v)) return false;
        for (uint32_t j = 0; j < i; ++j) {
            const auto& other = v.rewards.entries[j];
            if (e.location_id == other.location_id || !std::strcmp(e.unlockable, other.unlockable)) return false;
        }
    }
    return true;
}
bool campaign_flags_valid(const sc_campaign_row& row) {
    const auto flags = row.flags;
    if (!row.id || !row.title[0] || (flags & ~(63u | SC_CAMPAIGN_SLAYER_GATE | SC_CAMPAIGN_GATE_KEY | SC_CAMPAIGN_GATE_COMPLETE))) return false;
    if (((flags & SC_CAMPAIGN_UNLOCKED) && (!(flags & SC_CAMPAIGN_REVEALED) || !row.map[0])) ||
        ((flags & SC_CAMPAIGN_COMPLETED) && !(flags & SC_CAMPAIGN_UNLOCKED)) ||
        ((flags & SC_CAMPAIGN_DETAILS) && !(flags & SC_CAMPAIGN_UNLOCKED))) return false;
    if (!(flags & SC_CAMPAIGN_REVEALED) && (row.map[0] || std::strcmp(row.title, "???"))) return false;
    return true;
}
bool campaign_row_valid(const sc_campaign_row& row) {
    if (!campaign_flags_valid(row)) return false;
    const bool map = std::all_of(std::begin(row.map), std::end(row.map), [](char c) {
        return !c || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '/';
    });
    return map && std::all_of(std::begin(row.title), std::end(row.title), [](char c) { return !c || (c >= 32 && c <= 126); });
}
WireResult decode_campaign_rewards(Reader& r, CampaignRequest& v) {
    generated::v1_transfer_campaign_rewards_header(r, v.rewards);
    if (v.rewards.known > 1 || v.rewards.count > 17 || r.size != 769u + 732u * v.rewards.count ||
        (!(v.request.row.flags & SC_CAMPAIGN_REVEALED) && (v.rewards.known || v.rewards.count))) return WireResult::malformed;
    for (uint32_t i = 0; i < v.rewards.count; ++i) generated::v1_transfer_campaign_reward(r, v.rewards.entries[i]);
    return rewards_valid(v) ? WireResult::ok : WireResult::malformed;
}
WireResult decode_campaign_family(Reader& r, DecodedRequest& out) {
    CampaignRequest v{}; v.request.execution = out.execution;
    generated::v1_transfer_campaign_request(r, v.request);
    const auto capability = out.capability;
    if (capability != campaign_menu_capability) {
        generated::v1_transfer_campaign_summary(r, v.summary);
        for (auto& c : v.summary.challenges) generated::v1_transfer_campaign_challenge(r, c);
        if (!summary_valid(v.summary, v.request.row.flags)) return WireResult::malformed;
    }
    if (capability == campaign_rewards_capability) {
        v.request.reward_presentation = 1;
        if (decode_campaign_rewards(r, v) != WireResult::ok) return WireResult::malformed;
    }
    if (!namespace_valid(v.request.namespace_id) || v.request.row.map[191] || v.request.row.title[95]) return WireResult::malformed;
    if (out.operation == campaign_row_operation && !campaign_row_valid(v.request.row)) return WireResult::malformed;
    out.payload = v; return WireResult::ok;
}
using FamilyDecoder = WireResult (*)(Reader&, DecodedRequest&);
constexpr FamilyDecoder family_decoders[] = {
    decode_empty, decode_cursor, decode_empty, decode_cursor,
    decode_family<sc_save_backup_request, generated::v1_transfer_backup_request>,
    decode_family<sc_weapon_points_request, generated::v1_transfer_points_request>, decode_campaign_family,
    decode_family<sc_inventory_request, generated::v1_transfer_inventory_request>,
    decode_family<sc_arsenal_request, generated::v1_transfer_arsenal_request>,
    decode_family<sc_runes_request, generated::v1_transfer_runes_request>,
    decode_family<sc_special_request, generated::v1_transfer_special_request>,
    decode_family<sc_deathlink_request, generated::v1_transfer_deathlink_request>,
    decode_family<sc_automap_request, generated::v1_transfer_automap_request>,
    decode_family<sc_command_request, generated::v1_transfer_command_request>, decode_combat,
    decode_family<sc_combat_presentation_request, generated::v1_transfer_presentation_request>
};
static_assert(std::size(family_decoders) == size_t(generated::V1Family::presentation) + 1);
const generated::V1Descriptor* descriptor(uint16_t version, uint16_t op) {
    const auto end = std::end(generated::v1_descriptors);
    const auto it = std::find_if(std::begin(generated::v1_descriptors), end, [=](const auto& d) {
        return d.version == version && d.opcode == op;
    });
    return it == end ? nullptr : it;
}
WireResult request_layout(Reader& r, const generated::V1Descriptor& d, uint64_t& capability) {
    if (d.family != generated::V1Family::campaign) {
        if (d.payload && r.size - header_size != d.payload) return WireResult::malformed;
        capability = r.number(8);
        return (d.subset ? (capability & ~d.capability) == 0 : capability == d.capability) ?
            WireResult::ok : WireResult::capability_unavailable;
    }
    capability = r.number(8);
    const auto length = r.size - header_size;
    if (capability == campaign_menu_capability) return length == 453 ? WireResult::ok : WireResult::malformed;
    if (capability == campaign_presentation_capability) return length == 745 ? WireResult::ok : WireResult::malformed;
    if (capability == campaign_rewards_capability) return length >= 753 ? WireResult::ok : WireResult::malformed;
    return WireResult::capability_unavailable;
}
template<class T> void copy_request(const RequestPayload& payload, T* out) {
    if (const auto* value = std::get_if<T>(&payload); value && out) *out = *value;
}
void copy_campaign(const RequestPayload& payload, sc_campaign_request* request, sc_campaign_summary* summary,
                   sc_campaign_rewards* rewards) {
    const auto* value = std::get_if<CampaignRequest>(&payload);
    if (!value) return;
    if (request) *request = value->request;
    if (summary) *summary = value->summary;
    if (rewards) *rewards = value->rewards;
}
}
namespace {
WireResult request_header(size_t size, DecodedRequest& out, Reader& r, uint16_t& opcode) {
    out = {};
    if (size < header_size || size > max_request) return WireResult::malformed;
    generated::wire_header_1 h{}; generated::v1_transfer_header(r, h);
    if (h.magic != magic || h.payload_length != size - header_size || h.result) return WireResult::malformed;
    const auto version = h.version, op = h.operation;
    if (op >= engine_operation && op <= combat_presentation_operation) out.operation = static_cast<uint16_t>(op);
    if (version != wire_version) return WireResult::incompatible_protocol;
    opcode = static_cast<uint16_t>(op);
    return WireResult::ok;
}
}
WireResult decode_request(const Message& in, size_t size, DecodedRequest& out) {
    Reader r{in, size}; uint16_t opcode = 0;
    const auto checked = request_header(size, out, r, opcode);
    if (checked != WireResult::ok) return checked;
    const auto* d = descriptor(wire_version, opcode);
    if (!d) return WireResult::unsupported_operation;
    const auto layout = request_layout(r, *d, out.capability);
    if (layout != WireResult::ok) return layout;
    if (d->envelope) {
        generated::v1_transfer_envelope(r, out.execution);
        if (!envelope_valid(out.execution, d->allow_zero_generation)) return WireResult::malformed;
    }
    if (d->input_abi && r.number(4) != d->input_abi) return WireResult::incompatible_protocol;
    const auto decoded = family_decoders[size_t(d->family)](r, out);
    return r.valid && r.pos == size ? decoded : WireResult::malformed;
}
WireResult decode_request(const Message& in, size_t size, uint16_t* operation,
                          sc_diagnostic_request* diagnostic, uint64_t* after_event, uint64_t* write_id,
                          sc_save_backup_request* backup, sc_weapon_points_request* points, sc_campaign_request* campaign,
                          sc_inventory_request* inventory, sc_arsenal_request* arsenal,
                          sc_runes_request* runes, sc_special_request* special,
                          sc_deathlink_request* deathlink, sc_automap_request* automap, sc_campaign_summary* summary,
                          sc_campaign_rewards* rewards, sc_command_request* command,
                          sc_combat_presentation_request* combat_presentation, sc_combat_query* combat) {
    DecodedRequest value{};
    const auto result = decode_request(in, size, value);
    if (operation) *operation = value.operation;
    if (result != WireResult::ok) return result;
    if (diagnostic && value.operation >= diagnostic_submit_operation && value.operation <= diagnostic_detail_cancel_operation)
        *diagnostic = value.execution;
    if (diagnostic && value.operation == combat_operation) diagnostic->expected.pid = value.execution.expected.pid;
    if (after_event && value.operation == native_operation) *after_event = value.cursor;
    if (write_id && value.operation == save_write_operation) *write_id = value.cursor;
    copy_request(value.payload, backup);
    copy_request(value.payload, points);
    copy_request(value.payload, inventory);
    copy_request(value.payload, arsenal);
    copy_request(value.payload, runes);
    copy_request(value.payload, special);
    copy_request(value.payload, deathlink);
    copy_request(value.payload, automap);
    copy_request(value.payload, command);
    copy_request(value.payload, combat_presentation);
    copy_request(value.payload, combat);
    copy_campaign(value.payload, campaign, summary, rewards);
    return result;
}
size_t encode_response(Message& out, WireResult result, const Snapshot& s) {
    Writer w{out}; header(w, wire_version, inspect_operation, 0, result);
    if (result == WireResult::ok) {
        generated::v1_transfer_inspect_core(w, s);
        w.number(inspect_capability, 8); generated::v1_transfer_process_identity(w, s);
        generated::v1_transfer_inspect_state(w, s);
        w.number(0, 4); // engine integration: unavailable
        w.number(0, 4); // gameplay safety: unprobed
        w.number(0, 4); // game build compatibility: unprobed
        generated::v1_transfer_inspect_tail(w, s);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.pos;
}
bool decode_response(const Message& in, size_t size, WireResult& result, Snapshot& s) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, inspect_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    s = {};
    s.core.size = sizeof(sc_status);
    generated::v1_transfer_inspect_core(r, s);
    if (r.number(8) != inspect_capability) return false;
    generated::v1_transfer_process_identity(r, s);
    generated::v1_transfer_inspect_state(r, s);
    if (s.core.state > SC_STOPPED || static_cast<uint32_t>(s.service) > static_cast<uint32_t>(ServiceState::failed)) return false;
    // wire v1 defines only these factual unavailable/unprobed states
    if (r.number(4) != 0 || r.number(4) != 0 || r.number(4) != 0) return false;
    generated::v1_transfer_inspect_tail(r, s);
    return r.valid && r.pos == size;
}
// op 2 has its own exact payload. op 1 and its strict abi/engine placeholder bytes stay intact
size_t encode_engine_response(Message& out, WireResult result, const Snapshot& s, const sc_engine_snapshot& e) {
    Writer w{out}; header(w, wire_version, engine_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(engine_capability, 8);
        generated::v1_transfer_response_identity(w, s);
        generated::v1_transfer_engine_result(w, e);
        for (const auto& f : e.fields) generated::v1_transfer_engine_field(w, f);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.pos;
}
bool decode_engine_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_engine_snapshot& e) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, engine_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    s = {}; e = {}; s.core.size = sizeof(sc_status); e.size = sizeof(e);
    if (r.number(8) != engine_capability) return false;
    generated::v1_transfer_response_identity(r, s);
    generated::v1_transfer_engine_result(r, e);
    if (e.abi_version != SC_ENGINE_ABI_VERSION) return false;
    for (size_t i = 0; i < 64; ++i) {
        const char ch = e.disk_sha256[i];
        if (e.disk_hash_reason == SC_REASON_NONE ? !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) : ch != 0) return false;
    }
    if (e.profile > SC_PROFILE_STEAM_20260818 || e.locator_revision > 1 || e.profile_runtime_validated != 0 ||
        e.sample_reason > SC_REASON_STALE || e.pe_reason > SC_REASON_STALE ||
        e.disk_hash_reason > SC_REASON_STALE || e.root_locator_reason > SC_REASON_STALE) return false;
    for (size_t i = 0; i < SC_ENGINE_FIELD_COUNT; ++i) {
        auto& f = e.fields[i]; generated::v1_transfer_engine_field(r, f);
        if (f.validity > SC_OBSERVATION_PROVISIONAL || f.reason > SC_REASON_STALE) return false;
        if (f.validity == SC_OBSERVATION_UNKNOWN) { if (f.reason == SC_REASON_NONE || f.value != 0) return false; }
        else if (f.reason != SC_REASON_NONE || f.win32_error != 0 || f.value < 0 ||
                 f.value > (i == SC_ENGINE_CUTSCENE_ID ? 100000 : 1)) return false;
    }
    return r.valid && r.pos == size;
}
const char* field_name(size_t field) {
    constexpr const char* names[] = {"root_available", "loading", "in_game", "map_present", "player_present", "cutscene_id"};
    return field < SC_ENGINE_FIELD_COUNT ? names[field] : "invalid_field";
}
// op 11 is additive. its field records use explicit widths, not c struct layout
size_t encode_save_response(Message& out, WireResult result, const Snapshot& s, const sc_save_snapshot& v) {
    Writer w{out}; header(w, wire_version, save_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(save_capability, 8); generated::v1_transfer_response_identity(w, s);
        generated::v1_transfer_save_result(w, v);
        for (const auto& f : v.fields) generated::v1_transfer_save_field(w, f);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_save_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_save_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, save_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(sc_status); v.size = sizeof(v);
    auto u32 = [&] { return static_cast<uint32_t>(r.number(4)); };
    if (r.number(8) != save_capability) return false;
    v.pid = s.pid = u32(); v.process_created = s.process_created = r.number(8);
    for (size_t i = 0; i < s.instance.size(); ++i) v.instance[i] = s.instance[i] = static_cast<uint8_t>(r.number(1));
    s.core.abi_version = u32(); r.text(s.core.version); r.text(s.core.build_id);
    generated::v1_transfer_save_result(r, v);
    if (s.core.abi_version != SC_ABI_VERSION || v.abi_version != SC_SAVE_ABI_VERSION ||
        v.profile > SC_PROFILE_STEAM_20260818 || v.sample_reason > SC_REASON_STALE ||
        v.root_locator_reason > SC_REASON_STALE || v.layout_revision > 1 ||
        (v.layout_revision && v.profile != SC_PROFILE_STEAM_20260818) ||
        v.mutation_available || v.mutation_reason != SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN) return false;
    for (size_t i = 0; i < SC_SAVE_FIELD_COUNT; ++i) {
        auto& f = v.fields[i]; generated::v1_transfer_save_field(r, f);
        if (i >= SC_SAVE_SELECTED_SLOT) {
            const uint32_t expected = i == SC_SAVE_SELECTED_SLOT ? SC_SAVE_SELECTED_SLOT_UNPROVEN :
                (i == SC_SAVE_NAMESPACE_ROUTE ? SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN : SC_SAVE_COMPLETION_UNPROVEN);
            if (f.validity != SC_OBSERVATION_UNKNOWN || f.reason != expected || f.value || f.win32_error) return false;
        } else {
            if (f.validity > SC_OBSERVATION_OBSERVED || f.reason > SC_REASON_STALE) return false;
            if (f.validity == SC_OBSERVATION_UNKNOWN) { if (!f.reason || f.value) return false; }
            else if (f.reason || f.win32_error || !v.layout_revision || v.root_locator_reason ||
                f.value > (i == SC_SAVE_QUEUED_REQUESTS ? INT32_MAX :
                    (i == SC_SAVE_PROVIDER ? SC_SAVE_PROVIDER_FOREIGN : 1)) ||
                (i == SC_SAVE_PROVIDER && f.value == SC_SAVE_PROVIDER_UNKNOWN)) return false;
        }
    }
    return r.valid && r.pos == size;
}
size_t encode_save_admission_response(Message& out, WireResult result, const Snapshot& s,
                                      const sc_save_admission_snapshot& v) {
    Writer w{out}; header(w, wire_version, save_admission_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(save_admission_capability, 8); generated::v1_transfer_response_identity(w, s);
        generated::v1_transfer_admission_result(w, v);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_save_admission_response(const Message& in, size_t size, WireResult& result,
                                     Snapshot& s, sc_save_admission_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, save_admission_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(sc_status); v.size = sizeof(v);
    if (r.number(8) != save_admission_capability) return false;
    generated::v1_transfer_response_identity(r, s);
    generated::v1_transfer_admission_result(r, v);
    if (!r.valid || r.pos != size || s.core.abi_version != SC_ABI_VERSION ||
        v.abi_version != SC_SAVE_ADMISSION_ABI_VERSION || v.state > SC_SAVE_SESSION_BINDING ||
        v.fault > 17 || v.required_routes != 63 || (v.prepared_routes & ~63u) || (v.flags & ~7u)) return false;
    const bool has_id = v.namespace_id[0] != 0;
    if (has_id) {
        for (size_t i = 0; i < 64; ++i)
            if (!((v.namespace_id[i] >= '0' && v.namespace_id[i] <= '9') ||
                  (v.namespace_id[i] >= 'a' && v.namespace_id[i] <= 'f'))) return false;
        if (v.namespace_id[64] || std::memcmp(v.native_root, "ap-", 3) ||
            v.native_root[43]) return false;
        for (size_t i = 3; i < 43; ++i)
            if (!((v.native_root[i] >= '0' && v.native_root[i] <= '9') ||
                  (v.native_root[i] >= 'a' && v.native_root[i] <= 'f'))) return false;
    } else {
        for (auto c : v.namespace_id) if (c) return false;
        for (auto c : v.native_root) if (c) return false;
    }
    const bool routed = (v.flags & SC_SAVE_SESSION_ROUTED) != 0;
    const bool qualified = (v.flags & SC_SAVE_SESSION_STARTUP_QUALIFIED) != 0;
    if (v.state == SC_SAVE_SESSION_DISABLED && (has_id || v.flags || v.prepared_routes)) return false;
    if (v.state != SC_SAVE_SESSION_DISABLED && v.state != SC_SAVE_SESSION_REJECTED && !has_id) return false;
    if ((v.state == SC_SAVE_SESSION_REJECTED || v.state == SC_SAVE_SESSION_FAULTED) != (v.fault != 0)) return false;
    if (routed != (v.state == SC_SAVE_SESSION_ADMITTED || v.state == SC_SAVE_SESSION_FAULTED || v.state == SC_SAVE_SESSION_BINDING)) return false;
    if (routed && (!qualified || v.prepared_routes != v.required_routes)) return false;
    if ((v.flags & SC_SAVE_SESSION_ACCEPTING) && (!routed || v.state != SC_SAVE_SESSION_ADMITTED)) return false;
    if (qualified && v.state <= SC_SAVE_SESSION_PREPARED) return false;
    return true;
}
namespace {
template<class Codec> void installation_fields(Codec& c, sc_save_installation_snapshot& v) {
    generated::v1_transfer_installation_result(c, v);
    for (auto e : {&v.active, &v.primary_failure, &v.cleanup_failure}) generated::v1_transfer_installation_event(c, *e);
}
}
size_t encode_installation_response(Message& out, WireResult result, const Snapshot& s, const sc_save_installation_snapshot& value) {
    Writer w{out}; header(w, wire_version, save_installation_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(save_installation_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto copy = value; installation_fields(w, copy);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_installation_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_save_installation_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, save_installation_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    if (r.number(8) != save_installation_capability) return false;
    generated::v1_transfer_response_identity(r, s);
    installation_fields(r, v);
    if (!r.valid || r.pos != size || s.core.abi_version != SC_ABI_VERSION || v.abi_version != 1 ||
        v.attempt > 1 || v.phase > 3 || v.startup_observation > 3 || v.last_completed_stage > SC_INSTALL_UPSTREAM) return false;
    for (const auto e : {&v.active, &v.primary_failure, &v.cleanup_failure})
        if (e->sequence > v.sequence || e->stage > SC_INSTALL_UPSTREAM || e->target_group > 6 || e->byte_count > 32 || e->result > 2) return false;
    return true;
}
size_t encode_save_write_response(Message& out, WireResult result, const Snapshot& s,
                                  const sc_save_write_snapshot& v) {
    Writer w{out}; header(w, wire_version, save_write_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(save_write_capability, 8); generated::v1_transfer_response_identity(w, s);
        generated::v1_transfer_write_result(w, v);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_save_write_response(const Message& in, size_t size, WireResult& result,
                                 Snapshot& s, sc_save_write_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, save_write_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(sc_status); v.size = sizeof(v);
    if (r.number(8) != save_write_capability) return false;
    generated::v1_transfer_response_identity(r, s);
    generated::v1_transfer_write_result(r, v);
    if (!r.valid || r.pos != size || s.core.abi_version != SC_ABI_VERSION ||
        v.abi_version != SC_SAVE_WRITE_ABI_VERSION || v.state > SC_SAVE_WRITE_READBACK_FAILED || (v.flags & ~32767u) ||
        v.reserved || v.file_count > 1024 || v.submitted > v.file_count || v.completed > v.submitted ||
        v.pending_handles > v.submitted - v.completed || v.preparation_jobs > 128 ||
        v.preflight_jobs > 128 - v.preparation_jobs) return false;
    for (auto c : v.reserved_bytes) if (c) return false;
    bool ended = false;
    for (unsigned char c : v.directory) {
        if (!c) ended = true;
        else if (ended || c < 32 || c > 126) return false;
    }
    if (!ended) return false;
    if (v.state == SC_SAVE_WRITE_NONE || v.state == SC_SAVE_WRITE_NOT_RETAINED) {
        return (v.state == SC_SAVE_WRITE_NONE ? !v.operation_id : v.operation_id != 0) &&
            !(v.flags & ~SC_SAVE_WRITE_TRACKING_LOST) && !v.sdk_sequence && !v.file_count && !v.submitted &&
            !v.completed && !v.pending_handles && !v.preparation_jobs && !v.preflight_jobs &&
            !v.native_state && !v.native_outcome && !v.native_value && !v.directory[0];
    }
    if (!v.operation_id || !v.directory[0]) return false;
    constexpr auto readback = SC_SAVE_WRITE_READBACK_ACTIVE | SC_SAVE_WRITE_READBACK_HASHES |
        SC_SAVE_WRITE_READBACK_TERMINAL | SC_SAVE_WRITE_READBACK_ERROR;
    if ((v.flags & readback) && !(v.flags & SC_SAVE_WRITE_READBACK_REQUIRED)) return false;
    if ((v.flags & SC_SAVE_WRITE_READBACK_TERMINAL) &&
        !(v.flags & (SC_SAVE_WRITE_READBACK_HASHES | SC_SAVE_WRITE_READBACK_ERROR))) return false;
    constexpr auto proof = SC_SAVE_WRITE_PAYLOADS_PREPARED | SC_SAVE_WRITE_PAYLOADS_CAPTURED |
        SC_SAVE_WRITE_CALLBACKS_SUCCEEDED | SC_SAVE_WRITE_SDK_SUCCEEDED;
    if (!v.sdk_sequence && (v.submitted || v.completed || (v.flags & (proof | SC_SAVE_WRITE_UNPROVEN)))) return false;
    if ((v.flags & proof) && !v.file_count) return false;
    if ((v.flags & SC_SAVE_WRITE_PAYLOADS_CAPTURED) &&
        (!(v.flags & SC_SAVE_WRITE_PAYLOADS_PREPARED) || v.submitted != v.file_count)) return false;
    if ((v.flags & SC_SAVE_WRITE_CALLBACKS_SUCCEEDED) && (v.completed != v.file_count || v.pending_handles)) return false;
    uint32_t expected = SC_SAVE_WRITE_PENDING;
    if (!(v.flags & SC_SAVE_WRITE_PROVIDER_TERMINAL)) {
        if (v.native_state || v.native_outcome || v.native_value) return false;
        if (!(v.flags & SC_SAVE_WRITE_PROVIDER_ALIVE)) expected = SC_SAVE_WRITE_INDETERMINATE;
    } else {
        if (v.native_state == -1 || (v.native_state && (v.native_outcome || v.native_value))) return false;
        if (v.native_state || v.native_outcome || v.native_value != 1) expected = SC_SAVE_WRITE_NATIVE_FAILED;
        else expected = (v.flags & proof) == proof && !(v.flags & (SC_SAVE_WRITE_UNPROVEN | SC_SAVE_WRITE_TRACKING_LOST)) ?
            SC_SAVE_WRITE_SDK_CONFIRMED : SC_SAVE_WRITE_NATIVE_SUCCEEDED;
        if (expected == SC_SAVE_WRITE_SDK_CONFIRMED && (v.flags & SC_SAVE_WRITE_READBACK_REQUIRED))
            expected = (v.flags & SC_SAVE_WRITE_READBACK_ERROR) ? SC_SAVE_WRITE_READBACK_FAILED :
                (v.flags & SC_SAVE_WRITE_READBACK_TERMINAL) ? SC_SAVE_WRITE_READBACK_CONFIRMED : SC_SAVE_WRITE_READBACK_PENDING;
    }
    return v.state == expected;
}
const char* save_write_state_name(uint32_t state) {
    constexpr const char* names[] = {"none", "pending", "native_failed", "native_succeeded",
        "sdk_confirmed", "indeterminate", "not_retained", "readback_pending", "readback_confirmed", "readback_failed"};
    return state < std::size(names) ? names[state] : "invalid";
}
const char* save_session_state_name(uint32_t state) {
    constexpr const char* names[] = {"disabled", "prepared", "starting", "admitted", "rejected", "faulted", "binding"};
    return state < std::size(names) ? names[state] : "invalid";
}
const char* save_session_fault_name(uint32_t fault) {
    constexpr const char* names[] = {"none", "descriptor", "installation", "incomplete_routes", "startup_context",
        "repeated_startup", "missed_startup", "provider_identity", "foreign_collector", "malformed_entry",
        "native_collection", "native_copy", "unscoped_delete", "delete_indeterminate", "native_write", "native_profile", "native_read", "native_campaign"};
    return fault < std::size(names) ? names[fault] : "invalid";
}
const char* save_field_name(size_t field) {
    constexpr const char* names[] = {"root_present", "profile_manager_present", "provider_kind", "queued_requests",
        "pending_map_load", "save_job_witness", "request58_witness", "request50_witness", "selected_slot",
        "namespace_route", "native_operation_id", "native_completion"};
    return field < SC_SAVE_FIELD_COUNT ? names[field] : "invalid_field";
}
const char* save_reason_name(uint32_t reason) {
    switch (reason) {
    case SC_SAVE_UNSUPPORTED: return "unsupported";
    case SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN: return "native_namespace_route_unproven";
    case SC_SAVE_SELECTED_SLOT_UNPROVEN: return "selected_slot_unproven";
    case SC_SAVE_COMPLETION_UNPROVEN: return "native_completion_unproven";
    default: return reason_name(reason);
    }
}
const char* save_provider_name(uint64_t provider) {
    constexpr const char* names[] = {"unknown", "steam", "local_encrypted", "local", "foreign"};
    return provider <= SC_SAVE_PROVIDER_FOREIGN ? names[provider] : "invalid_provider";
}
size_t encode_context_response(Message& out, WireResult result, const Snapshot& s, const sc_context_snapshot& c) {
    Writer w{out}; header(w, wire_version, context_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(context_capability, 8);
        generated::v1_transfer_response_identity(w, s);
        generated::v1_transfer_context_result(w, c);
        const auto& map = c.current_map;
        generated::v1_transfer_context_map(w, map);
        generated::v1_transfer_map_bytes(w, map);
        for (const auto& f : c.fields) generated::v1_transfer_save_field(w, f);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_context_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_context_snapshot& c) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, context_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    s = {}; c = {}; s.core.size = sizeof(sc_status); c.size = sizeof(c);
    auto u32 = [&] { return static_cast<uint32_t>(r.number(4)); };
    if (r.number(8) != context_capability) return false;
    c.pid = s.pid = u32(); c.process_created = s.process_created = r.number(8);
    for (size_t i = 0; i < s.instance.size(); ++i) c.instance[i] = s.instance[i] = static_cast<uint8_t>(r.number(1));
    s.core.abi_version = u32(); r.text(s.core.version); r.text(s.core.build_id);
    generated::v1_transfer_context_result(r, c);
    if (s.core.abi_version != SC_ABI_VERSION || c.abi_version != SC_CONTEXT_ABI_VERSION) return false;
    if (c.sample_reason > SC_CONTEXT_EMPTY_NAME || c.profile > SC_PROFILE_STEAM_20260818 ||
        c.locator_revision > 1 || c.layout_revision > 1 || c.root_locator_reason > SC_REASON_STALE ||
        c.disk_hash_reason > SC_REASON_STALE || (c.layout_revision && c.profile != SC_PROFILE_STEAM_20260818)) return false;
    for (size_t i = 0; i < 64; ++i) {
        const char ch = c.disk_sha256[i];
        if (c.disk_hash_reason == SC_REASON_NONE ? !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) : ch != 0) return false;
    }
    auto& map = c.current_map;
    generated::v1_transfer_context_map(r, map);
    if (map.validity > SC_OBSERVATION_OBSERVED || map.reason > SC_CONTEXT_EMPTY_NAME || map.length >= SC_CONTEXT_MAP_CAPACITY) return false;
    if (map.validity == SC_OBSERVATION_UNKNOWN) { if (!map.reason || map.length) return false; }
    else if (map.reason || map.win32_error || !map.length) return false;
    generated::v1_transfer_map_bytes(r, map);
    if (!std::all_of(map.bytes, map.bytes + map.length, [](char c) { return c >= 32 && c <= 126; })) return false;
    for (size_t i = 0; i < SC_CONTEXT_FIELD_COUNT; ++i) {
        auto& f = c.fields[i]; generated::v1_transfer_save_field(r, f);
        if (f.validity > SC_OBSERVATION_OBSERVED || f.reason > SC_CONTEXT_EMPTY_NAME) return false;
        if (f.validity == SC_OBSERVATION_UNKNOWN) { if (!f.reason || f.value) return false; }
        else if (f.reason || f.win32_error || !c.layout_revision || c.root_locator_reason ||
                 i >= SC_CONTEXT_LOAD_SERIAL || f.value > (i == SC_CONTEXT_GAME_STATE ? SC_GAME_IN_GAME : UINT32_MAX)) return false;
    }
    if (map.validity == SC_OBSERVATION_OBSERVED && (!c.layout_revision || c.root_locator_reason ||
        c.fields[SC_CONTEXT_GAME_STATE].validity != SC_OBSERVATION_OBSERVED ||
        c.fields[SC_CONTEXT_GAME_STATE].value != SC_GAME_IN_GAME)) return false;
    return r.valid && r.pos == size;
}
namespace {
template<class C> bool map_values(C& c, sc_context_map& m) {
    generated::v1_transfer_native_map(c, m);
    if (m.length >= SC_CONTEXT_MAP_CAPACITY || m.reason > SC_CONTEXT_EMPTY_NAME ||
        (m.validity != SC_OBSERVATION_UNKNOWN && m.validity != SC_OBSERVATION_OBSERVED)) return false;
    if (m.validity == SC_OBSERVATION_UNKNOWN ? m.length != 0 : (!m.length || m.reason || m.win32_error)) return false;
    generated::v1_transfer_map_bytes(c, m);
    if (!std::all_of(m.bytes, m.bytes + m.length, [](char b) { return b >= 32 && b <= 126; })) return false;
    m.bytes[m.length] = 0; return true;
}
template<class C> bool native_values(C& c, sc_native_snapshot& n) {
    generated::v1_transfer_native_result(c, n);
    if (!map_values(c, n.current_map)) return false;
    generated::v1_transfer_native_counts(c, n);
    if (n.event_count > SC_NATIVE_EVENT_PAGE || n.availability > SC_NATIVE_RETAINED || n.reason > SC_NATIVE_EXCEPTION ||
        n.context_reason > SC_NATIVE_EXCEPTION || n.lifecycle > SC_LIFETIME_INVALID || n.site_revision > 1 || n.phase > 1 ||
        n.installed_hooks > 7 || n.coverage > 7 || n.retained_module > 1 || n.checkpoint_flag_known > 1 || n.checkpoint_flag > 1 ||
        (n.checkpoint_flag && !n.checkpoint_flag_known) || n.history_gap > 1 || n.game_state > SC_GAME_IN_GAME ||
        n.queued > SC_DIAGNOSTIC_CAPACITY || n.claimed > SC_DIAGNOSTIC_CAPACITY || n.retained_results > SC_DIAGNOSTIC_CAPACITY ||
        n.queued + n.claimed + n.retained_results > SC_DIAGNOSTIC_CAPACITY) return false;
    for (auto v : n.validator_reasons) if (v > SC_NATIVE_EXCEPTION) return false;
    if (n.context_generation && (n.context_generation != n.scope.lifecycle_generation || n.lifecycle != SC_LIFETIME_ACTIVE ||
        n.depth || n.context_reason || n.game_state != SC_GAME_IN_GAME || n.current_map.validity != SC_OBSERVATION_OBSERVED ||
        n.event_gap_count || n.availability != SC_NATIVE_ENABLED)) return false;
    if (!n.context_generation && n.current_map.length) return false;
    for (uint32_t i = 0; i < n.event_count; ++i) {
        auto& e = n.events[i]; generated::v1_transfer_native_event(c, e);
        if (!e.sequence || e.sequence > n.event_sequence || e.generation > n.scope.lifecycle_generation ||
            e.kind < SC_EVENT_CHANGE_BEGIN || e.kind > SC_EVENT_GAP || e.lifecycle > SC_LIFETIME_INVALID ||
            (i && e.sequence <= n.events[i - 1].sequence)) return false;
    }
    return true;
}
template<class C> bool diagnostic_values(C& c, sc_diagnostic_result& d, bool backup = false, bool process_scoped = false) {
    generated::v1_transfer_diagnostic_result(c, d);
    if (!map_values(c, d.current_map) || d.state > SC_DIAGNOSTIC_CANCELLED || d.reason > SC_NATIVE_EXCEPTION ||
        d.cancel_requested > 1 || d.retrieved > 1 || d.site_revision > 1 || d.phase > 1 ||
        d.lifecycle > SC_LIFETIME_INVALID || d.game_state > SC_GAME_IN_GAME) return false;
    if (backup) {
        if (!d.executed_at_ms) return !d.observed_at_ms && !d.current_map.length && d.state != SC_DIAGNOSTIC_EXECUTED;
        return d.observed_at_ms && d.claimed_at_ms && d.admitted_at_ms && d.thread_id &&
            d.scope.lifecycle_generation && d.phase == 1 && d.site_revision == 1 && d.lifecycle == SC_LIFETIME_ACTIVE &&
            (process_scoped || (d.current_map.validity == SC_OBSERVATION_OBSERVED && d.game_state == SC_GAME_IN_GAME)) &&
            d.observed_at_ms >= d.claimed_at_ms && d.executed_at_ms >= d.observed_at_ms &&
            d.claimed_at_ms >= d.admitted_at_ms && d.executed_at_ms < d.deadline_at_ms &&
            (d.state == SC_DIAGNOSTIC_CLAIMED ? !d.completed_at_ms : d.completed_at_ms >= d.executed_at_ms);
    }
    if (d.state == SC_DIAGNOSTIC_EXECUTED) {
        if (d.reason || !d.observed_at_ms || !d.executed_at_ms || !d.claimed_at_ms || !d.admitted_at_ms || !d.thread_id ||
            !d.scope.lifecycle_generation || d.phase != 1 || d.site_revision != 1 || d.lifecycle != SC_LIFETIME_ACTIVE ||
            d.current_map.validity != SC_OBSERVATION_OBSERVED || d.game_state != SC_GAME_IN_GAME ||
            d.observed_at_ms < d.claimed_at_ms || d.executed_at_ms < d.observed_at_ms || d.claimed_at_ms < d.admitted_at_ms ||
            d.executed_at_ms >= d.deadline_at_ms || d.completed_at_ms < d.executed_at_ms) return false;
    } else if (d.observed_at_ms || d.executed_at_ms || d.current_map.length) return false;
    return true;
}
}
namespace {
uint64_t native_wire_capability(uint16_t op) {
    return op == native_operation ? native_capability :
        (op >= diagnostic_detail_submit_operation ? diagnostic_detail_capability : diagnostic_capability);
}
template<class C> bool detail_values(C& c, sc_diagnostic_detail& d, const sc_diagnostic_result& result) {
    generated::v1_transfer_detail(c, d);
    if (d.revision != SC_DIAGNOSTIC_DETAIL_REVISION || d.stage > SC_STAGE_EXECUTED ||
        d.observation_attempted > 1 || d.observation_accepted > d.observation_attempted || d.timing_valid > 1 ||
        d.claim_lock_missed > 1 || d.sample_reason > SC_CONTEXT_EMPTY_NAME || d.state_reason > SC_CONTEXT_EMPTY_NAME ||
        d.map_reason > SC_CONTEXT_EMPTY_NAME || d.state_validity > SC_OBSERVATION_OBSERVED ||
        d.map_validity > SC_OBSERVATION_OBSERVED || d.pending_before > 3 || d.pending_after > 3 ||
        d.pending_before_reason > SC_REASON_STALE || d.pending_after_reason > SC_REASON_STALE) return false;
    if ((d.pending_before == 3) != (d.pending_before_reason != 0) ||
        (d.pending_after == 3) != (d.pending_after_reason != 0)) return false;
    if (!d.observation_attempted && (d.observation_started_at_ms || d.observation_elapsed_ns ||
        d.observation_budget_ns || d.timing_valid || d.sample_reason || d.state_validity || d.map_validity)) return false;
    if (d.observation_attempted && (!d.observation_started_at_ms || d.observation_budget_ns != 2000000)) return false;
    if (d.observation_accepted && (!d.timing_valid || d.sample_reason ||
        d.state_validity != SC_OBSERVATION_OBSERVED || d.map_validity != SC_OBSERVATION_OBSERVED)) return false;
    if (result.state == SC_DIAGNOSTIC_CLAIMED && (d.stage || d.observation_attempted)) return false;
    if (result.state == SC_DIAGNOSTIC_EXECUTED && (!d.observation_accepted || d.stage != SC_STAGE_EXECUTED)) return false;
    return true;
}
}
namespace {
template<class C> bool backup_values(C& c, sc_save_backup_snapshot& v) {
    c.u32(v.abi_version); c.u32(v.state); c.u32(v.failure);
    if (!diagnostic_values(c, v.execution, true)) return false;
    generated::v1_transfer_backup_result_tail(c, v);
    if (v.abi_version != SC_SAVE_BACKUP_ABI_VERSION || v.state > SC_BACKUP_EXPIRED ||
        v.failure > SC_BACKUP_FAILURE_STORAGE || (v.flags & ~255u) || v.files > 16 || v.bytes > (1ull << 30) ||
        v.storage_outcome > 17 || v.campaign > 2 || v.slot > 11 || v.namespace_id[64]) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto b = v.namespace_id[i];
        if (!(b >= '0' && b <= '9') && !(b >= 'a' && b <= 'f')) return false;
    }
    if (v.basename[0]) {
        constexpr char prefix[] = "transport-backup-";
        constexpr size_t count = sizeof(prefix) - 1;
        if (std::memcmp(v.basename, prefix, count) || std::memcmp(v.basename + count, v.namespace_id, 16) ||
            v.basename[count + 16] != '-') return false;
        for (size_t i = count + 17; i < count + 49; ++i) {
            const auto b = v.basename[i];
            if (!(b >= '0' && b <= '9') && !(b >= 'a' && b <= 'f')) return false;
        }
        for (size_t i = count + 49; i < sizeof(v.basename); ++i) if (v.basename[i]) return false;
    } else for (auto b : v.basename) if (b) return false;
    const bool matched = (v.flags & SC_BACKUP_SOURCE_MATCHED) != 0;
    const bool stored = (v.flags & SC_BACKUP_STORAGE_COMPLETE) != 0;
    if (matched && (!(v.flags & SC_BACKUP_NATIVE_ENTERED) || !(v.flags & SC_BACKUP_TASK_RETURNED) ||
        !v.operation_id || v.native_exception)) return false;
    if ((v.flags & SC_BACKUP_READ_SUCCESS) && (!(v.flags & SC_BACKUP_READ_TERMINAL) || !v.operation_id)) return false;
    if (stored && (!(v.flags & SC_BACKUP_STORAGE_ATTEMPTED) || !v.basename[0] || !v.files ||
        v.bytes < v.files || v.storage_outcome != 16 || v.storage_error)) return false;
    if (!(v.flags & SC_BACKUP_STORAGE_ATTEMPTED) && (v.files || v.bytes || v.basename[0] || v.storage_outcome || v.storage_error)) return false;
    const auto state = v.execution.state;
    switch (v.state) {
    case SC_BACKUP_UNKNOWN: return state == SC_DIAGNOSTIC_UNKNOWN && !v.operation_id && !v.flags;
    case SC_BACKUP_QUEUED: return state == SC_DIAGNOSTIC_QUEUED && !v.operation_id && !v.flags;
    case SC_BACKUP_CLAIMED: return state == SC_DIAGNOSTIC_CLAIMED && !v.operation_id && !(v.flags & ~SC_BACKUP_CANCEL_REQUESTED);
    case SC_BACKUP_WAITING_NATIVE: return state == SC_DIAGNOSTIC_CLAIMED && v.execution.executed_at_ms;
    case SC_BACKUP_COPYING: return state == SC_DIAGNOSTIC_CLAIMED && v.operation_id && !stored;
    case SC_BACKUP_COPIED: return state == SC_DIAGNOSTIC_CLAIMED && stored;
    case SC_BACKUP_COMPLETE: return state == SC_DIAGNOSTIC_EXECUTED && !v.execution.reason && !v.failure &&
        (v.flags & 247u) == 247u && v.operation_id && !v.native_exception;
    case SC_BACKUP_FAILED: return state == SC_DIAGNOSTIC_EXECUTED && v.failure != SC_BACKUP_FAILURE_NONE;
    case SC_BACKUP_REJECTED: return state == SC_DIAGNOSTIC_REJECTED;
    case SC_BACKUP_CANCELLED: return state == SC_DIAGNOSTIC_CANCELLED && !v.operation_id;
    case SC_BACKUP_EXPIRED: return state == SC_DIAGNOSTIC_EXPIRED && !v.operation_id;
    default: return false;
    }
}
}
namespace {
template<class Codec> bool points_values(Codec& c, sc_weapon_points_result& v) {
    c.u32(v.abi_version);
    if (!diagnostic_values(c,v.execution,true)) return false;
    generated::v1_transfer_points_result_tail(c, v);
    if (v.abi_version != SC_WEAPON_POINTS_ABI_VERSION || v.namespace_id[64] ||
        v.outcome > SC_WUP_UNAVAILABLE || (v.flags & ~31u)) return false;
    if (v.outcome == SC_WUP_GRANTED && (v.execution.state != SC_DIAGNOSTIC_EXECUTED ||
        v.native_exception || v.kind != SC_WUP_GRANT || !v.amount || v.amount > SC_WEAPON_POINTS_MAX_GRANT ||
        (v.flags & 31u) != 31u || v.gained_before != v.expected_gained ||
        uint64_t(v.balance_before)+v.amount != v.balance_after || uint64_t(v.gained_before)+v.amount != v.gained_after)) return false;
    return true;
}
}
size_t encode_weapon_points_request(Message& out, uint16_t op, const sc_weapon_points_request& request) {
    if (op < weapon_points_submit_operation || op > weapon_points_release_operation) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(weapon_points_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_points_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_weapon_points_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_weapon_points_result& value) {
    Writer w{out}; header(w,wire_version,op,0,result);
    if (result == WireResult::ok) {
        w.number(weapon_points_capability,8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!points_values(w,v)) return 0;
    }
    Writer length{out,8}; length.number(w.pos-header_size,4); return w.valid ? w.pos : 0;
}
bool decode_weapon_points_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_weapon_points_result& v) {
    if (size < header_size || size > max_message || op < weapon_points_submit_operation || op > weapon_points_release_operation) return false;
    Reader r{in,size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != weapon_points_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && points_values(r,v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id,s.instance.data(),16);
}
namespace {
template<class Codec> bool runes_values(Codec& c, sc_runes_result& v) {
    c.u32(v.abi_version);
    if (!diagnostic_values(c, v.execution, true)) return false;
    generated::v1_transfer_runes_result_tail(c, v);
    if (v.abi_version != SC_RUNES_ABI_VERSION || v.namespace_id[64] ||
        v.outcome > SC_RUNES_OUTCOME_REFRESH_FAILED || (v.flags & ~31u)) return false;
    return true;
}
}
size_t encode_runes_request(Message& out, uint16_t op, const sc_runes_request& request) {
    if (op < runes_submit_operation || op > runes_release_operation) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(runes_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_runes_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_runes_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_runes_result& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(runes_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!runes_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_runes_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_runes_result& v) {
    if (size < header_size || size > max_message || op < runes_submit_operation || op > runes_release_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != runes_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && runes_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
namespace {
template<class Codec> bool arsenal_values(Codec& c, sc_arsenal_result& v) {
    c.u32(v.abi_version);
    if (!diagnostic_values(c, v.execution, true)) return false;
    generated::v1_transfer_arsenal_result_tail(c, v);
    if (v.abi_version != SC_ARSENAL_ABI_VERSION || v.namespace_id[64] ||
        v.outcome > SC_ARSENAL_OUTCOME_CRASH_PROTECTED || (v.flags & ~127u)) return false;
    return true;
}
}
size_t encode_arsenal_request(Message& out, uint16_t op, const sc_arsenal_request& request) {
    if (op < arsenal_submit_operation || op > arsenal_release_operation) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(arsenal_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_arsenal_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_arsenal_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_arsenal_result& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(arsenal_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!arsenal_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_arsenal_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_arsenal_result& v) {
    if (size < header_size || size > max_message || op < arsenal_submit_operation || op > arsenal_release_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != arsenal_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && arsenal_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
namespace {
template<class Codec> bool inventory_values(Codec& c, sc_inventory_result& v) {
    c.u32(v.abi_version);
    if (!diagnostic_values(c, v.execution, true)) return false;
    generated::v1_transfer_inventory_result_tail(c, v);
    if (v.abi_version != SC_INVENTORY_ABI_VERSION || v.namespace_id[64] ||
        v.outcome > SC_INV_UNAVAILABLE || (v.flags & ~31u)) return false;
    const auto mask = [](uint32_t value, uint32_t allowed) {
        return value == SC_INVENTORY_UNKNOWN_MASK || !(value & ~allowed);
    };
    const auto tier = [](uint8_t value) {
        return value == SC_INVENTORY_UNKNOWN_TIER || value <= SC_INVENTORY_MAX_CAPACITY_TIER;
    };
    if (!mask(v.weapons_before, SC_INV_ALL_WEAPONS) || !mask(v.weapons_after, SC_INV_ALL_WEAPONS) ||
        !mask(v.equipment_before, SC_INV_ALL_EQUIPMENT) || !mask(v.equipment_after, SC_INV_ALL_EQUIPMENT) ||
        !tier(v.health_tier_before) || !tier(v.health_tier_after) ||
        !tier(v.armor_tier_before) || !tier(v.armor_tier_after) ||
        !tier(v.ammo_tier_before) || !tier(v.ammo_tier_after) ||
        v.special_before || v.special_after ||
        !mask(v.upgrades_before, SC_INV_BLOOD_PUNCH_MASK) || !mask(v.upgrades_after, SC_INV_BLOOD_PUNCH_MASK) ||
        v.reserved_before || v.reserved_after ||
        (v.ice_bomb_before > 1 && v.ice_bomb_before != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.ice_bomb_after > 1 && v.ice_bomb_after != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.dash_before > 1 && v.dash_before != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.dash_after > 1 && v.dash_after != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.slayer_key_before > 1 && v.slayer_key_before != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.slayer_key_after > 1 && v.slayer_key_after != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.frag_grenade_before > 1 && v.frag_grenade_before != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.frag_grenade_after > 1 && v.frag_grenade_after != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.flame_belch_before > 1 && v.flame_belch_before != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.flame_belch_after > 1 && v.flame_belch_after != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.blood_punch_before > 1 && v.blood_punch_before != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.blood_punch_after > 1 && v.blood_punch_after != SC_INVENTORY_UNKNOWN_ITEM) ||
        (v.perks_owned & ~v.perks_known)) return false;
    return true;
}
}
size_t encode_inventory_request(Message& out, uint16_t op, const sc_inventory_request& request) {
    if (op < inventory_submit_operation || op > inventory_release_operation) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(inventory_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_inventory_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_inventory_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_inventory_result& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(inventory_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!inventory_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_inventory_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_inventory_result& v) {
    if (size < header_size || size > max_message || op < inventory_submit_operation || op > inventory_release_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != inventory_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && inventory_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
namespace {
template<class C> bool campaign_values(C& c,sc_campaign_result& v) {
    generated::v1_transfer_campaign_result(c, v);
    if (v.abi_version==2) {
        generated::v1_transfer_campaign_intent_count(c, v);
        if (v.intent_count>96) return false;
        generated::v1_transfer_campaign_intents(c, v);
        for (uint32_t i=0;i<v.intent_count;++i) {
            if (!v.intent_ids[i]) return false;
            for (uint32_t j=0;j<i;++j) if (v.intent_ids[i]==v.intent_ids[j]) return false;
        }
    }
    return (v.abi_version==SC_CAMPAIGN_MENU_ABI_VERSION || v.abi_version==2) && !v.namespace_id[64] &&
        v.status<=SC_CAMPAIGN_REFUSED && v.reason<=SC_CAMPAIGN_NATIVE &&
        (v.status==SC_CAMPAIGN_ACCEPTED)==(v.reason==SC_CAMPAIGN_OK) &&
        v.rendered_revision<=v.committed_revision;
}
}
size_t encode_campaign_request(Message& out,uint16_t op,const sc_campaign_request& request,const sc_campaign_summary* summary,
                               const sc_campaign_rewards* rewards) {
    if (op<campaign_row_operation || op>campaign_inspect_operation) return 0;
    if (rewards && (!summary || rewards->count>17)) return 0;
    Writer w{out}; header(w,wire_version,op,rewards ? 753+732*rewards->count : summary ? 745 : 453,WireResult::ok);
    w.number(rewards ? campaign_rewards_capability : summary ? campaign_presentation_capability : campaign_menu_capability,8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_campaign_request(w, request);
    if (summary) {
        generated::v1_transfer_campaign_summary(w, *summary);
        for (const auto& challenge : summary->challenges) generated::v1_transfer_campaign_challenge(w, challenge);
    }
    if (rewards) {
        generated::v1_transfer_campaign_rewards_header(w, *rewards);
        for (uint32_t i = 0; i < rewards->count; ++i) generated::v1_transfer_campaign_reward(w, rewards->entries[i]);
    }
    return w.valid ? w.pos : 0;
}
size_t encode_campaign_response(Message& out,WireResult result,uint16_t op,const Snapshot& s,const sc_campaign_result& value) {
    Writer w{out}; header(w,wire_version,op,0,result);
    if (result==WireResult::ok) {
        w.number(campaign_menu_capability,8); generated::v1_transfer_response_identity(w, s);
        auto v=value; if (!campaign_values(w,v)) return 0;
    }
    Writer length{out,8}; length.number(w.pos-header_size,4); return w.valid ? w.pos : 0;
}
bool decode_campaign_response(const Message& in,size_t size,WireResult& result,uint16_t op,Snapshot& s,sc_campaign_result& v) {
    if (size<header_size || size>max_message || op<campaign_row_operation || op>campaign_inspect_operation) return false;
    Reader r{in,size};
    if (!response_header(r, op, result)) return false;
    if (result!=WireResult::ok) return size==header_size;
    if (r.number(8)!=campaign_menu_capability) return false;
    s={}; v={}; s.core.size=sizeof(s.core); v.size=sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version==SC_ABI_VERSION && campaign_values(r,v) && r.valid && r.pos==size &&
        v.scope.pid==s.pid && v.scope.process_created==s.process_created &&
        !std::memcmp(v.scope.instance_id,s.instance.data(),16);
}
namespace {
template<class C> bool command_values(C& c, sc_command_result& v) {
    c.u32(v.abi_version);
    if (!diagnostic_values(c, v.execution, true, true)) return false;
    generated::v1_transfer_command_result_tail(c, v);
    if (v.abi_version != SC_COMMAND_ABI_VERSION || v.namespace_id[64] ||
        v.kind < SC_COMMAND_ACTIVATE || v.kind > SC_COMMAND_TRANSIENT_POLICY || v.outcome > SC_COMMAND_NATIVE_FAILED) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto ch = v.namespace_id[i];
        if (!(ch >= '0' && ch <= '9') && !(ch >= 'a' && ch <= 'f')) return false;
    }
    if (v.native_exception && v.outcome != SC_COMMAND_NATIVE_FAILED) return false;
    if (v.execution.state == SC_DIAGNOSTIC_EXECUTED &&
        (v.execution.current_map.validity != SC_OBSERVATION_OBSERVED || v.execution.game_state != SC_GAME_IN_GAME) &&
        (std::memcmp(v.namespace_id, SC_COMMAND_PROCESS_NAMESPACE, 65) != 0 || v.kind != SC_COMMAND_CONDUMP)) return false;
    return v.execution.state == SC_DIAGNOSTIC_EXECUTED ? v.outcome != SC_COMMAND_NOT_EXECUTED :
        v.outcome == SC_COMMAND_NOT_EXECUTED && !v.native_exception;
}
}
size_t encode_command_request(Message& out, uint16_t op, const sc_command_request& request) {
    if (op < command_submit_operation || op > command_release_operation || !commands::valid(request)) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(command_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    w.number(SC_COMMAND_ABI_VERSION, 4);
    generated::v1_transfer_command_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_command_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_command_result& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(command_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!command_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_command_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_command_result& v) {
    if (size < header_size || size > max_message || op < command_submit_operation || op > command_release_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != command_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && command_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
size_t encode_backup_request(Message& out, uint16_t op, const sc_save_backup_request& request) {
    if (op < save_backup_submit_operation || op > save_backup_cancel_operation) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(save_backup_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_backup_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_backup_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_save_backup_snapshot& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(save_backup_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!backup_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_backup_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_save_backup_snapshot& v) {
    if (size < header_size || size > max_message || op < save_backup_submit_operation || op > save_backup_cancel_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != save_backup_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && backup_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
const char* backup_state_name(uint32_t state) {
    constexpr const char* names[] = {"unknown", "queued", "claimed", "waiting_native", "copying", "copied",
        "complete", "failed", "rejected", "cancelled", "expired"};
    return state <= SC_BACKUP_EXPIRED ? names[state] : "invalid";
}
namespace {
template<class C> bool combat_values(C& c, sc_combat_snapshot& value) {
    generated::v1_transfer_combat_result(c, value);
    if (value.availability > SC_NATIVE_RETAINED || value.reason > SC_NATIVE_EXCEPTION ||
        std::strlen(value.namespace_id) != 64) return false;
    for (size_t n = 0; n < 64; ++n) {
        const auto ch = value.namespace_id[n];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return true;
}
}
size_t encode_combat_request(Message& out, uint32_t pid, const sc_combat_query& query) {
    if (!pid || !combat::valid_query(query)) return 0;
    Writer w{out}; header(w, wire_version, combat_operation, 0, WireResult::ok);
    w.number(combat_capability, 8); w.number(SC_COMBAT_ABI_VERSION, 4); w.number(pid, 4);
    generated::v1_transfer_combat_query(w, query);
    generated::v1_transfer_combat_maps(w, query);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_combat_response(Message& out, WireResult result, const Snapshot& source, const sc_combat_snapshot& value) {
    Writer w{out}; header(w, wire_version, combat_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(combat_capability, 8);
        generated::v1_transfer_response_identity(w, source);
        w.number(SC_COMBAT_ABI_VERSION, 4);
        auto copy = value;
        if (!combat_values(w, copy)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
bool decode_combat_response(const Message& in, size_t size, WireResult& result, Snapshot& source, sc_combat_snapshot& value) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, combat_operation, result)) return false;
    if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != combat_capability) return false;
    source = {}; value = {};
    source.core.size = sizeof(source.core); value.size = sizeof(value); value.abi_version = SC_COMBAT_ABI_VERSION;
    generated::v1_transfer_response_identity(r, source);
    if (r.number(4) != SC_COMBAT_ABI_VERSION || source.core.abi_version != SC_ABI_VERSION ||
        !source.pid || !source.process_created || std::strlen(source.core.build_id) != 64) return false;
    for (size_t n = 0; n < 64; ++n) {
        const auto ch = source.core.build_id[n];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    if (!combat_values(r, value) || value.scope.pid != source.pid ||
        value.scope.process_created != source.process_created ||
        std::memcmp(value.scope.instance_id, source.instance.data(), 16)) return false;
    return r.valid && r.pos == size;
}
namespace {
template<class C> bool combat_presentation_values(C& c, sc_combat_presentation_result& value) {
    generated::v1_transfer_presentation_result(c, value);
    return value.abi_version == SC_COMBAT_PRESENTATION_ABI_VERSION && value.status <= 1 &&
        value.reason <= SC_NATIVE_EXCEPTION && value.visible <= 1 &&
        (value.status == 0) == (value.reason == SC_NATIVE_NONE);
}
}
size_t encode_combat_presentation_request(Message& out, const sc_combat_presentation_request& request) {
    if (!combat::valid_presentation(request)) return 0;
    Writer w{out}; header(w, wire_version, combat_presentation_operation, 0, WireResult::ok);
    w.number(combat_presentation_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    w.number(SC_COMBAT_PRESENTATION_ABI_VERSION, 4);
    generated::v1_transfer_presentation_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_combat_presentation_response(Message& out, WireResult result, const Snapshot& source,
                                         const sc_combat_presentation_result& value) {
    Writer w{out}; header(w, wire_version, combat_presentation_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(combat_presentation_capability, 8); generated::v1_transfer_response_identity(w, source);
        auto copy = value; if (!combat_presentation_values(w, copy)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_combat_presentation_response(const Message& in, size_t size, WireResult& result, Snapshot& source,
                                       sc_combat_presentation_result& value) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, combat_presentation_operation, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != combat_presentation_capability) return false;
    source = {}; value = {}; source.core.size = sizeof(source.core); value.size = sizeof(value);
    generated::v1_transfer_response_identity(r, source);
    return source.core.abi_version == SC_ABI_VERSION && combat_presentation_values(r, value) &&
        r.valid && r.pos == size && value.scope.pid == source.pid &&
        value.scope.process_created == source.process_created &&
        !std::memcmp(value.scope.instance_id, source.instance.data(), 16);
}
size_t encode_native_request(Message& out, uint16_t op, const sc_diagnostic_request& request, uint64_t after) {
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(native_wire_capability(op), 8);
    if (op == native_operation) w.number(after, 8);
    else {
        auto r = request; generated::v1_transfer_envelope(w, r);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
size_t encode_native_response(Message& out, WireResult result, uint16_t op, const Snapshot& s,
                              const sc_native_snapshot& native, const sc_diagnostic_result& diagnostic,
                              const sc_diagnostic_detail& detail) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(native_wire_capability(op), 8);
        generated::v1_transfer_response_identity(w, s); w.number(SC_NATIVE_ABI_VERSION, 4);
        auto n = native; auto d = diagnostic;
        if (!(op == native_operation ? native_values(w, n) : diagnostic_values(w, d))) return 0;
        if (op >= diagnostic_detail_submit_operation) { auto e = detail; if (!detail_values(w, e, d)) return 0; }
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_native_response(const Message& in, size_t size, WireResult& result, uint16_t op,
                             Snapshot& s, sc_native_snapshot& n, sc_diagnostic_result& d, sc_diagnostic_detail* detail) {
    if (detail) *detail = {};
    if (size < header_size || size > max_message || op < native_operation || op > diagnostic_detail_cancel_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != native_wire_capability(op)) return false;
    s = {}; n = {}; d = {}; s.core.size = sizeof(sc_status); n.size = sizeof(n); n.abi_version = SC_NATIVE_ABI_VERSION;
    generated::v1_transfer_response_identity(r, s);
    if (s.core.abi_version != SC_ABI_VERSION || r.number(4) != SC_NATIVE_ABI_VERSION || !s.pid || !s.process_created ||
        std::strlen(s.core.build_id) != 64) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto ch = s.core.build_id[i]; if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    if (!(op == native_operation ? native_values(r, n) : diagnostic_values(r, d))) return false;
    if (op >= diagnostic_detail_submit_operation) {
        sc_diagnostic_detail e{}; if (!detail_values(r, e, d)) return false;
        if (detail) *detail = e;
    }
    if (op == native_operation && (n.scope.pid != s.pid || n.scope.process_created != s.process_created ||
        std::memcmp(n.scope.instance_id, s.instance.data(), s.instance.size()))) return false;
    if (op != native_operation && d.state == SC_DIAGNOSTIC_EXECUTED &&
        (d.scope.pid != s.pid || d.scope.process_created != s.process_created ||
         std::memcmp(d.scope.instance_id, s.instance.data(), s.instance.size()))) return false;
    return r.valid && r.pos == size;
}
const char* native_reason_name(uint32_t reason) {
    constexpr const char* names[] = {"none", "not_started", "unknown_build", "target_bytes", "target_not_unique",
        "target_boundary", "binding_failed", "hook_failed", "pin_failed", "wrong_thread", "wrong_caller", "reentrant",
        "event_gap", "unobserved", "transition", "context_unavailable", "stale", "scope_mismatch", "queue_full",
        "duplicate_mismatch", "deadline", "cancelled", "stopped", "not_found", "read_failed", "budget", "exception"};
    return reason <= SC_NATIVE_EXCEPTION ? names[reason] : "invalid_reason";
}
const char* diagnostic_state_name(uint32_t state) {
    constexpr const char* names[] = {"unknown", "queued", "claimed", "callback_executed", "rejected", "expired", "cancelled"};
    return state <= SC_DIAGNOSTIC_CANCELLED ? names[state] : "invalid_state";
}
const char* diagnostic_stage_name(uint32_t stage) {
    constexpr const char* names[] = {"none", "admission", "queue", "claim_context", "scope", "pending_before",
        "fresh_context", "observation_budget", "pending_after", "event_stamp", "map_binding", "native_fault",
        "cancellation", "deadline", "executed"};
    return stage <= SC_STAGE_EXECUTED ? names[stage] : "invalid_stage";
}
const char* context_field_name(size_t field) {
    constexpr const char* names[] = {"game_state", "state_changed_ms", "native_load_serial", "cutscene_active", "native_entry_mode"};
    return field < SC_CONTEXT_FIELD_COUNT ? names[field] : "invalid_field";
}
const char* context_reason_name(uint32_t reason) {
    switch (reason) {
    case SC_CONTEXT_UNSUPPORTED: return "unsupported";
    case SC_CONTEXT_MENU_WORLD: return "menu_world";
    case SC_CONTEXT_NAME_TOO_LONG: return "name_too_long";
    case SC_CONTEXT_EMPTY_NAME: return "empty_name";
    default: return reason_name(reason);
    }
}
const char* validity_name(uint32_t validity) {
    return validity == SC_OBSERVATION_OBSERVED ? "observed" :
        (validity == SC_OBSERVATION_PROVISIONAL ? "provisional" : "unknown");
}
const char* reason_name(uint32_t reason) {
    constexpr const char* names[] = {"none", "not_sampled", "stopped", "invalid_pe", "read_failed",
        "partial_read", "out_of_range", "signature_missing", "signature_ambiguous", "profile_unrecognized",
        "parent_unavailable", "parent_null", "invalid_value", "transition", "cancelled", "budget", "internal_error", "stale"};
    return reason <= SC_REASON_STALE ? names[reason] : "invalid_reason";
}
const char* service_name(ServiceState state) {
    switch (state) {
    case ServiceState::stopped: return "stopped";
    case ServiceState::listening: return "listening";
    case ServiceState::stopping: return "stopping";
    default: return "failed";
    }
}
const char* result_name(ProbeResult result) {
    switch (result) {
    case ProbeResult::ok: return "ok";
    case ProbeResult::usage: return "usage";
    case ProbeResult::endpoint_absent: return "endpoint_absent";
    case ProbeResult::access_denied: return "access_denied";
    case ProbeResult::timeout: return "timeout";
    case ProbeResult::process_mismatch: return "process_mismatch";
    case ProbeResult::incompatible_protocol: return "incompatible_protocol";
    case ProbeResult::capability_unavailable: return "capability_unavailable";
    case ProbeResult::invalid_response: return "invalid_response";
    default: return "io_error";
    }
}
std::string instance_text(const std::array<uint8_t, 16>& id) {
    std::string value;
    for (auto b : id) { value += "0123456789abcdef"[b >> 4]; value += "0123456789abcdef"[b & 15]; }
    return value;
}
namespace {
template<class Codec> bool special_values(Codec& c, sc_special_result& v) {
    c.u32(v.abi_version);
    if (!diagnostic_values(c, v.execution, true)) return false;
    generated::v1_transfer_special_result_tail(c, v);
    if (v.abi_version != SC_SPECIAL_ABI_VERSION || v.namespace_id[64] ||
        v.outcome > SC_SPECIAL_OUTCOME_CRASH_PROTECTED ||
        (v.flags & ~SC_SPECIAL_FLAGS_SUPPORTED)) return false;
    if (v.owns_crucible > 1 || v.owns_hammer > 1 || v.hammer_tier > SC_SPECIAL_HAMMER_TIER_UPGRADED ||
        v.selected > SC_SPECIAL_WEAPON_HAMMER || v.native_crucible > 1 || v.native_hammer > 1 ||
        v.native_hammer_perks > 3 || v.native_selected > SC_SPECIAL_WEAPON_HAMMER ||
        (v.native_state_known & ~31u) || (v.refill_flags & ~7u) ||
        v.refill_request_state > SC_SPECIAL_REFILL_EXPIRED || v.refill_executed > 1 || v.reserved0) return false;
    return true;
}
}
size_t encode_special_request(Message& out, uint16_t op, const sc_special_request& request) {
    if (op < special_submit_operation || op > special_release_operation) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(special_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_special_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_special_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_special_result& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(special_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!special_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_special_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_special_result& v) {
    if (size < header_size || size > max_message || op < special_submit_operation || op > special_release_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != special_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && special_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
namespace {
template<class Codec> bool deathlink_values(Codec& c, sc_deathlink_result& v) {
    c.u32(v.abi_version);
    if (!diagnostic_values(c, v.execution, true)) return false;
    generated::v1_transfer_deathlink_result_tail(c, v);
    if (v.abi_version != SC_DEATHLINK_ABI_VERSION || v.namespace_id[64] ||
        v.outcome > SC_DEATHLINK_OUTCOME_DISABLED || (v.flags & ~8191u)) return false;
    if (v.enabled > 1 || v.mode > SC_DEATHLINK_MODE_HARDCORE ||
        v.remote_state > SC_DEATHLINK_REMOTE_CANCELLED ||
        v.remote_protection > SC_DEATHLINK_PROTECTION_OTHER ||
        v.remote_attempts > 8 || v.pending_count > 8 ||
        v.local_state > SC_DEATHLINK_LOCAL_AVAILABLE ||
        v.local_cause > SC_DEATHLINK_CAUSE_REMOTE ||
        v.local_protection > SC_DEATHLINK_PROTECTION_OTHER ||
        (v.local_flags & ~(SC_DEATHLINK_FLAG_SUPPRESSED | SC_DEATHLINK_FLAG_LOCAL_EVENT)) || v.reserved0) return false;
    return true;
}
}
size_t encode_deathlink_request(Message& out, uint16_t op, const sc_deathlink_request& request) {
    if (op < deathlink_submit_operation || op > deathlink_release_operation) return 0;
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(deathlink_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_deathlink_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_deathlink_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_deathlink_result& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(deathlink_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!deathlink_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_deathlink_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_deathlink_result& v) {
    if (size < header_size || size > max_message || op < deathlink_submit_operation || op > deathlink_release_operation) return false;
    Reader r{in, size};
    if (!response_header(r, op, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != deathlink_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && deathlink_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
}

namespace sentinel {
namespace {
template<class Codec> bool automap_values(Codec& c,sc_automap_result& v) {
    generated::v1_transfer_automap_result(c, v);
    return v.abi_version==SC_AUTOMAP_ABI_VERSION && !v.namespace_id[64] && v.known<=1 &&
        v.kind>=SC_AUTOMAP_PUBLISH && v.kind<=SC_AUTOMAP_OBSERVE && v.outcome<=SC_AUTOMAP_REGRESSION;
}
}
size_t encode_automap_request(Message& out,const sc_automap_request& request) {
    Writer w{out}; header(w, wire_version, automap_operation, 0, WireResult::ok);
    w.number(automap_capability, 8);
    generated::v1_transfer_envelope(w, request.execution);
    generated::v1_transfer_automap_request(w, request);
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_automap_response(Message& out, WireResult result, const Snapshot& s, const sc_automap_result& value) {
    Writer w{out}; header(w, wire_version, automap_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(automap_capability, 8); generated::v1_transfer_response_identity(w, s);
        auto v = value; if (!automap_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_automap_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_automap_result& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (!response_header(r, automap_operation, result)) return false; if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != automap_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    generated::v1_transfer_response_identity(r, s);
    return s.core.abi_version == SC_ABI_VERSION && automap_values(r, v) && r.valid && r.pos == size &&
        v.scope.pid == s.pid && v.scope.process_created == s.process_created &&
        !std::memcmp(v.scope.instance_id, s.instance.data(), 16);
}
}
