#include "protocol.h"
#include <cstring>

namespace sentinel {
namespace {
constexpr uint32_t magic = 0x50494353; // "SCIP", little endian.
struct Writer {
    Message& data;
    size_t pos = 0;
    bool valid = true;
    void number(uint64_t n, size_t width) {
        if (pos + width > data.size()) { valid = false; return; }
        for (size_t i = 0; i < width; ++i) { data[pos++] = static_cast<uint8_t>(n); n >>= 8; }
    }
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
    w.number(magic, 4); w.number(version, 2); w.number(op, 2);
    w.number(payload, 4); w.number(static_cast<uint32_t>(result), 4);
}
}
size_t encode_request(Message& out, uint64_t required, uint16_t version, uint16_t op) {
    Writer w{out}; header(w, version, op, 8, WireResult::ok); w.number(required, 8); return w.pos;
}
size_t encode_save_write_request(Message& out, uint64_t operation_id) {
    Writer w{out}; header(w, wire_version, save_write_operation, 16, WireResult::ok);
    w.number(save_write_capability, 8); w.number(operation_id, 8); return w.pos;
}
WireResult decode_request(const Message& in, size_t size, uint16_t* operation,
                          sc_diagnostic_request* diagnostic, uint64_t* after_event, uint64_t* write_id, sc_save_backup_request* backup) {
    if (operation) *operation = inspect_operation;
    if (size < header_size || size > max_request) return WireResult::malformed;
    Reader r{in, size};
    if (r.number(4) != magic) return WireResult::malformed;
    const auto version = r.number(2), op = r.number(2), length = r.number(4), result = r.number(4);
    if (length != size - header_size || result != 0) return WireResult::malformed;
    if (operation && op >= engine_operation && op <= save_installation_operation) *operation = static_cast<uint16_t>(op);
    if (version != wire_version) return WireResult::incompatible_protocol;
    if (op < inspect_operation || op > save_installation_operation) return WireResult::unsupported_operation;
    if (op == save_installation_operation) {
        if (length != 8) return WireResult::malformed;
        return r.number(8) == save_installation_capability ? WireResult::ok : WireResult::capability_unavailable;
    }
    if (op >= save_backup_submit_operation && op <= save_backup_cancel_operation) {
        // Reuse the exact existing request identity decoder, then consume only
        // this operation's fixed namespace/campaign/slot/deadline extension.
        if (length != 149) return WireResult::malformed;
        if (r.number(8) != save_backup_capability) return WireResult::capability_unavailable;
        Message identity = in;
        Writer fixed{identity, 6}; fixed.number(diagnostic_submit_operation, 2); fixed.number(72, 4);
        Writer capability{identity, header_size}; capability.number(diagnostic_capability, 8);
        sc_save_backup_request value{};
        const auto decoded = decode_request(identity, 88, nullptr, &value.execution);
        if (decoded != WireResult::ok) return decoded;
        r.pos = 88;
        for (auto& c : value.namespace_id) c = static_cast<char>(r.number(1));
        r.u32(value.campaign); r.u32(value.slot); r.u32(value.work_deadline_ms);
        if (value.namespace_id[64] || value.campaign > 2 || value.slot > 11 || !value.work_deadline_ms ||
            value.work_deadline_ms > SC_SAVE_BACKUP_MAX_WORK_MS) return WireResult::malformed;
        for (size_t i = 0; i < 64; ++i) {
            const auto c = value.namespace_id[i];
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return WireResult::malformed;
        }
        if (backup) *backup = value;
        return r.valid && r.pos == size ? WireResult::ok : WireResult::malformed;
    }
    if (op == save_write_operation) {
        if (length != 16) return WireResult::malformed;
        if (r.number(8) != save_write_capability) return WireResult::capability_unavailable;
        const auto id = r.number(8); if (write_id) *write_id = id;
        return r.valid && r.pos == size ? WireResult::ok : WireResult::malformed;
    }
    if (op >= native_operation && op <= diagnostic_detail_cancel_operation) {
        if (length != (op == native_operation ? 16u : 72u)) return WireResult::malformed;
        const auto supported = op == native_operation ? native_capability :
            (op >= diagnostic_detail_submit_operation ? diagnostic_detail_capability : diagnostic_capability);
        if (r.number(8) != supported) return WireResult::capability_unavailable;
        if (op == native_operation) {
            const auto after = r.number(8); if (after_event) *after_event = after;
        } else {
            sc_diagnostic_request request{};
            request.expected.pid = static_cast<uint32_t>(r.number(4)); request.expected.process_created = r.number(8);
            for (auto& b : request.expected.instance_id) r.byte(b);
            request.expected.lifecycle_generation = r.number(8); request.request_id = r.number(8);
            for (auto& b : request.nonce) r.byte(b);
            request.deadline_ms = static_cast<uint32_t>(r.number(4));
            bool nonce = false, instance = false;
            for (auto b : request.nonce) nonce |= b != 0;
            for (auto b : request.expected.instance_id) instance |= b != 0;
            if (!nonce || !instance || !request.request_id || !request.expected.pid || !request.expected.process_created ||
                !request.expected.lifecycle_generation || !request.deadline_ms || request.deadline_ms > SC_DIAGNOSTIC_MAX_DEADLINE_MS)
                return WireResult::malformed;
            if (diagnostic) *diagnostic = request;
        }
        return r.valid && r.pos == size ? WireResult::ok : WireResult::malformed;
    }
    if (length != 8) return WireResult::malformed;
    const auto supported = op == save_admission_operation ? save_admission_capability : (op == save_operation ? save_capability :
        (op == context_operation ? context_capability : (op == engine_operation ? engine_capability : inspect_capability)));
    return (r.number(8) & ~supported) ? WireResult::capability_unavailable : WireResult::ok;
}
size_t encode_response(Message& out, WireResult result, const Snapshot& s) {
    Writer w{out}; header(w, wire_version, inspect_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(s.core.abi_version, 4); w.number(s.core.capabilities, 8);
        w.number(inspect_capability, 8); w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto byte : s.instance) w.number(byte, 1);
        w.number(s.core.state, 4); w.number(static_cast<uint32_t>(s.service), 4);
        w.number(s.service_error, 4);
        w.number(0, 4); // Engine integration: unavailable.
        w.number(0, 4); // Gameplay safety: unprobed.
        w.number(0, 4); // Game build compatibility: unprobed.
        w.number(s.core.last_result, 4); w.number(s.core.initialization_count, 4);
        w.text(s.core.version); w.text(s.core.build_id);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.pos;
}
bool decode_response(const Message& in, size_t size, WireResult& result, Snapshot& s) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != inspect_operation) return false;
    if (r.number(4) != size - header_size) return false;
    const auto code = r.number(4);
    if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code);
    if (result != WireResult::ok) return size == header_size;
    s = {};
    s.core.size = sizeof(sc_status);
    s.core.abi_version = static_cast<uint32_t>(r.number(4));
    s.core.capabilities = r.number(8);
    if (r.number(8) != inspect_capability) return false;
    s.pid = static_cast<uint32_t>(r.number(4)); s.process_created = r.number(8);
    for (auto& byte : s.instance) byte = static_cast<uint8_t>(r.number(1));
    s.core.state = static_cast<uint32_t>(r.number(4));
    const auto service = r.number(4);
    if (s.core.state > SC_STOPPED || service > static_cast<uint32_t>(ServiceState::failed)) return false;
    s.service = static_cast<ServiceState>(service);
    s.service_error = static_cast<uint32_t>(r.number(4));
    // Wire v1 defines only these factual unavailable/unprobed states.
    if (r.number(4) != 0 || r.number(4) != 0 || r.number(4) != 0) return false;
    s.core.last_result = static_cast<uint32_t>(r.number(4));
    s.core.initialization_count = static_cast<uint32_t>(r.number(4));
    r.text(s.core.version); r.text(s.core.build_id);
    return r.valid && r.pos == size;
}
// Op 2 has its own exact payload. Op 1 and its strict ABI/engine placeholder bytes stay intact.
size_t encode_engine_response(Message& out, WireResult result, const Snapshot& s, const sc_engine_snapshot& e) {
    Writer w{out}; header(w, wire_version, engine_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(engine_capability, 8);
        w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto byte : s.instance) w.number(byte, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id);
        w.number(e.abi_version, 4); w.number(e.sequence, 8); w.number(e.sampled_at_ms, 8);
        w.number(e.duration_ms, 4); w.number(e.sample_reason, 4);
        w.number(e.pe_reason, 4); w.number(e.machine, 4); w.number(e.timestamp, 4);
        w.number(e.image_size, 4); w.number(e.entry_rva, 4); w.number(e.disk_hash_reason, 4);
        // Fixed 64 lowercase hex bytes, or 64 NULs when unavailable (no optional lengths).
        for (size_t i = 0; i < 64; ++i) w.number(static_cast<uint8_t>(e.disk_sha256[i]), 1);
        w.number(e.profile, 4); w.number(e.locator_revision, 4); w.number(e.root_locator_reason, 4);
        w.number(e.root_signature_rva, 4); w.number(e.root_target_rva, 4);
        w.number(e.profile_runtime_validated, 4);
        for (const auto& f : e.fields) {
            w.number(f.validity, 4); w.number(f.reason, 4);
            w.number(static_cast<uint32_t>(f.value), 4); w.number(f.win32_error, 4);
        }
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4);
    return w.pos;
}
bool decode_engine_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_engine_snapshot& e) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != engine_operation ||
        r.number(4) != size - header_size) return false;
    const auto code = r.number(4);
    if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code);
    if (result != WireResult::ok) return size == header_size;
    s = {}; e = {}; s.core.size = sizeof(sc_status); e.size = sizeof(e);
    if (r.number(8) != engine_capability) return false;
    s.pid = static_cast<uint32_t>(r.number(4)); s.process_created = r.number(8);
    for (auto& byte : s.instance) byte = static_cast<uint8_t>(r.number(1));
    s.core.abi_version = static_cast<uint32_t>(r.number(4)); r.text(s.core.version); r.text(s.core.build_id);
    e.abi_version = static_cast<uint32_t>(r.number(4));
    if (e.abi_version != SC_ENGINE_ABI_VERSION) return false;
    e.sequence = r.number(8); e.sampled_at_ms = r.number(8);
    auto u32 = [&] { return static_cast<uint32_t>(r.number(4)); };
    e.duration_ms = u32(); e.sample_reason = u32(); e.pe_reason = u32();
    e.machine = u32(); e.timestamp = u32(); e.image_size = u32(); e.entry_rva = u32(); e.disk_hash_reason = u32();
    for (size_t i = 0; i < 64; ++i) {
        const char c = static_cast<char>(r.number(1));
        if (e.disk_hash_reason == SC_REASON_NONE ? !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) : c != 0)
            return false;
        e.disk_sha256[i] = c;
    }
    e.profile = u32(); e.locator_revision = u32(); e.root_locator_reason = u32();
    e.root_signature_rva = u32(); e.root_target_rva = u32(); e.profile_runtime_validated = u32();
    if (e.profile > SC_PROFILE_STEAM_20260818 || e.locator_revision > 1 || e.profile_runtime_validated != 0 ||
        e.sample_reason > SC_REASON_STALE || e.pe_reason > SC_REASON_STALE ||
        e.disk_hash_reason > SC_REASON_STALE || e.root_locator_reason > SC_REASON_STALE) return false;
    for (size_t i = 0; i < SC_ENGINE_FIELD_COUNT; ++i) {
        auto& f = e.fields[i]; f.validity = u32(); f.reason = u32(); f.value = static_cast<int32_t>(u32()); f.win32_error = u32();
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
// Op 11 is additive. Its field records use explicit widths, not C struct layout.
size_t encode_save_response(Message& out, WireResult result, const Snapshot& s, const sc_save_snapshot& v) {
    Writer w{out}; header(w, wire_version, save_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(save_capability, 8); w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto byte : s.instance) w.number(byte, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id);
        w.number(v.abi_version, 4); w.number(v.profile, 4); w.number(v.sequence, 8); w.number(v.sampled_at_ms, 8);
        w.number(v.duration_ms, 4); w.number(v.sample_reason, 4); w.number(v.layout_revision, 4);
        w.number(v.root_locator_reason, 4); w.number(v.mutation_available, 4); w.number(v.mutation_reason, 4);
        for (const auto& f : v.fields) {
            w.number(f.validity, 4); w.number(f.reason, 4); w.number(f.value, 8); w.number(f.win32_error, 4);
        }
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_save_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_save_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != save_operation ||
        r.number(4) != size - header_size) return false;
    const auto code = r.number(4);
    if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code);
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(sc_status); v.size = sizeof(v);
    auto u32 = [&] { return static_cast<uint32_t>(r.number(4)); };
    if (r.number(8) != save_capability) return false;
    v.pid = s.pid = u32(); v.process_created = s.process_created = r.number(8);
    for (size_t i = 0; i < s.instance.size(); ++i) v.instance[i] = s.instance[i] = static_cast<uint8_t>(r.number(1));
    s.core.abi_version = u32(); r.text(s.core.version); r.text(s.core.build_id);
    v.abi_version = u32(); v.profile = u32(); v.sequence = r.number(8); v.sampled_at_ms = r.number(8);
    v.duration_ms = u32(); v.sample_reason = u32(); v.layout_revision = u32(); v.root_locator_reason = u32();
    v.mutation_available = u32(); v.mutation_reason = u32();
    if (s.core.abi_version != SC_ABI_VERSION || v.abi_version != SC_SAVE_ABI_VERSION ||
        v.profile > SC_PROFILE_STEAM_20260818 || v.sample_reason > SC_REASON_STALE ||
        v.root_locator_reason > SC_REASON_STALE || v.layout_revision > 1 ||
        (v.layout_revision && v.profile != SC_PROFILE_STEAM_20260818) ||
        v.mutation_available || v.mutation_reason != SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN) return false;
    for (size_t i = 0; i < SC_SAVE_FIELD_COUNT; ++i) {
        auto& f = v.fields[i]; f.validity = u32(); f.reason = u32(); f.value = r.number(8); f.win32_error = u32();
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
        w.number(save_admission_capability, 8); w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto byte : s.instance) w.number(byte, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id);
        w.number(v.abi_version, 4); w.number(v.state, 4); w.number(v.fault, 4);
        w.number(v.prepared_routes, 4); w.number(v.required_routes, 4); w.number(v.flags, 4);
        for (auto c : v.namespace_id) w.number(static_cast<uint8_t>(c), 1);
        for (auto c : v.native_root) w.number(static_cast<uint8_t>(c), 1);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_save_admission_response(const Message& in, size_t size, WireResult& result,
                                     Snapshot& s, sc_save_admission_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != save_admission_operation ||
        r.number(4) != size - header_size) return false;
    const auto code = r.number(4);
    if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code);
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(sc_status); v.size = sizeof(v);
    if (r.number(8) != save_admission_capability) return false;
    s.pid = static_cast<uint32_t>(r.number(4)); s.process_created = r.number(8);
    for (auto& byte : s.instance) r.byte(byte);
    r.u32(s.core.abi_version); r.text(s.core.version); r.text(s.core.build_id);
    r.u32(v.abi_version); r.u32(v.state); r.u32(v.fault);
    r.u32(v.prepared_routes); r.u32(v.required_routes); r.u32(v.flags);
    for (auto& c : v.namespace_id) c = static_cast<char>(r.number(1));
    for (auto& c : v.native_root) c = static_cast<char>(r.number(1));
    if (!r.valid || r.pos != size || s.core.abi_version != SC_ABI_VERSION ||
        v.abi_version != SC_SAVE_ADMISSION_ABI_VERSION || v.state > SC_SAVE_SESSION_BINDING ||
        v.fault > 16 || v.required_routes != 63 || (v.prepared_routes & ~63u) || (v.flags & ~7u)) return false;
    const bool has_id = v.namespace_id[0] != 0;
    if (has_id) {
        for (size_t i = 0; i < 64; ++i)
            if (!((v.namespace_id[i] >= '0' && v.namespace_id[i] <= '9') ||
                  (v.namespace_id[i] >= 'a' && v.namespace_id[i] <= 'f'))) return false;
        if (v.namespace_id[64] || std::memcmp(v.native_root, "ap-", 3) ||
            std::memcmp(v.native_root + 3, v.namespace_id, 40) || v.native_root[43]) return false;
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
    c.u32(v.abi_version); c.u32(v.attempt); c.u32(v.phase); c.u32(v.last_completed_stage); c.u32(v.startup_observation);
    c.u32(v.validated); c.u32(v.created); c.u32(v.enabled); c.u32(v.cleanup_failures); c.u32(v.gaps); c.u64(v.sequence);
    for (auto e : {&v.active, &v.primary_failure, &v.cleanup_failure}) {
        c.u64(e->sequence); c.u64(e->at_ms); c.u64(e->duration_ms);
        c.u32(e->stage); c.u32(e->target_group); c.u32(e->target_index); c.u32(e->rva); c.u32(e->signature_offset);
        c.u32(e->result); c.u32(e->reason); c.u32(e->read_reason); c.u32(e->win32_error); c.u32(e->minhook_status);
        c.u32(e->byte_count); c.u32(e->collision_rva); c.u32(e->byte_window_offset);
        for (auto& b : e->expected_bytes) c.byte(b);
        for (auto& b : e->actual_bytes) c.byte(b);
    }
}
}
size_t encode_installation_response(Message& out, WireResult result, const Snapshot& s, const sc_save_installation_snapshot& value) {
    Writer w{out}; header(w, wire_version, save_installation_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(save_installation_capability, 8); w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto b : s.instance) w.number(b, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id);
        auto copy = value; installation_fields(w, copy);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_installation_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_save_installation_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != save_installation_operation || r.number(4) != size - header_size) return false;
    const auto code = r.number(4);
    if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code);
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    if (r.number(8) != save_installation_capability) return false;
    r.u32(s.pid); r.u64(s.process_created); for (auto& b : s.instance) r.byte(b);
    r.u32(s.core.abi_version); r.text(s.core.version); r.text(s.core.build_id);
    installation_fields(r, v);
    if (!r.valid || r.pos != size || s.core.abi_version != SC_ABI_VERSION || v.abi_version != 1 ||
        v.attempt > 1 || v.phase > 3 || v.startup_observation > 3 || v.last_completed_stage > SC_INSTALL_UPSTREAM) return false;
    for (const auto e : {&v.active, &v.primary_failure, &v.cleanup_failure})
        if (e->sequence > v.sequence || e->stage > SC_INSTALL_UPSTREAM || e->target_group > 3 || e->byte_count > 32 || e->result > 2) return false;
    return true;
}
size_t encode_save_write_response(Message& out, WireResult result, const Snapshot& s,
                                  const sc_save_write_snapshot& v) {
    Writer w{out}; header(w, wire_version, save_write_operation, 0, result);
    if (result == WireResult::ok) {
        w.number(save_write_capability, 8); w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto byte : s.instance) w.number(byte, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id);
        w.number(v.abi_version, 4); w.number(v.state, 4); w.number(v.flags, 4);
        w.number(v.operation_id, 8); w.number(v.sdk_sequence, 8);
        w.number(v.file_count, 4); w.number(v.submitted, 4); w.number(v.completed, 4); w.number(v.pending_handles, 4);
        w.number(v.preparation_jobs, 4); w.number(v.preflight_jobs, 4); w.number(v.native_value, 4); w.number(v.reserved, 4);
        w.number(static_cast<uint64_t>(v.native_state), 8); w.number(static_cast<uint64_t>(v.native_outcome), 8);
        for (auto c : v.directory) w.number(static_cast<uint8_t>(c), 1);
        for (auto c : v.reserved_bytes) w.number(c, 1);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_save_write_response(const Message& in, size_t size, WireResult& result,
                                 Snapshot& s, sc_save_write_snapshot& v) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != save_write_operation ||
        r.number(4) != size - header_size) return false;
    const auto code = r.number(4);
    if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code);
    if (result != WireResult::ok) return size == header_size;
    s = {}; v = {}; s.core.size = sizeof(sc_status); v.size = sizeof(v);
    if (r.number(8) != save_write_capability) return false;
    r.u32(s.pid); r.u64(s.process_created); for (auto& byte : s.instance) r.byte(byte);
    r.u32(s.core.abi_version); r.text(s.core.version); r.text(s.core.build_id);
    r.u32(v.abi_version); r.u32(v.state); r.u32(v.flags); r.u64(v.operation_id); r.u64(v.sdk_sequence);
    r.u32(v.file_count); r.u32(v.submitted); r.u32(v.completed); r.u32(v.pending_handles);
    r.u32(v.preparation_jobs); r.u32(v.preflight_jobs); r.u32(v.native_value); r.u32(v.reserved);
    const auto state = r.number(8), outcome = r.number(8);
    std::memcpy(&v.native_state, &state, 8); std::memcpy(&v.native_outcome, &outcome, 8);
    for (auto& c : v.directory) c = static_cast<char>(r.number(1));
    for (auto& c : v.reserved_bytes) r.byte(c);
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
        w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto byte : s.instance) w.number(byte, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id);
        w.number(c.abi_version, 4); w.number(c.sequence, 8); w.number(c.sampled_at_ms, 8);
        w.number(c.duration_ms, 4); w.number(c.sample_reason, 4);
        w.number(c.profile, 4); w.number(c.locator_revision, 4); w.number(c.layout_revision, 4);
        w.number(c.root_locator_reason, 4); w.number(c.disk_hash_reason, 4);
        for (size_t i = 0; i < 64; ++i) w.number(static_cast<uint8_t>(c.disk_sha256[i]), 1);
        const auto& map = c.current_map;
        w.number(map.validity, 4); w.number(map.reason, 4); w.number(map.win32_error, 4); w.number(map.length, 2);
        for (size_t i = 0; i < map.length; ++i) w.number(static_cast<uint8_t>(map.bytes[i]), 1);
        for (const auto& f : c.fields) {
            w.number(f.validity, 4); w.number(f.reason, 4); w.number(f.value, 8); w.number(f.win32_error, 4);
        }
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.pos;
}
bool decode_context_response(const Message& in, size_t size, WireResult& result, Snapshot& s, sc_context_snapshot& c) {
    if (size < header_size || size > max_message) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != context_operation ||
        r.number(4) != size - header_size) return false;
    const auto code = r.number(4);
    if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code);
    if (result != WireResult::ok) return size == header_size;
    s = {}; c = {}; s.core.size = sizeof(sc_status); c.size = sizeof(c);
    auto u32 = [&] { return static_cast<uint32_t>(r.number(4)); };
    if (r.number(8) != context_capability) return false;
    c.pid = s.pid = u32(); c.process_created = s.process_created = r.number(8);
    for (size_t i = 0; i < s.instance.size(); ++i) c.instance[i] = s.instance[i] = static_cast<uint8_t>(r.number(1));
    s.core.abi_version = u32(); r.text(s.core.version); r.text(s.core.build_id);
    c.abi_version = u32();
    if (s.core.abi_version != SC_ABI_VERSION || c.abi_version != SC_CONTEXT_ABI_VERSION) return false;
    c.sequence = r.number(8); c.sampled_at_ms = r.number(8); c.duration_ms = u32(); c.sample_reason = u32();
    c.profile = u32(); c.locator_revision = u32(); c.layout_revision = u32();
    c.root_locator_reason = u32(); c.disk_hash_reason = u32();
    if (c.sample_reason > SC_CONTEXT_EMPTY_NAME || c.profile > SC_PROFILE_STEAM_20260818 ||
        c.locator_revision > 1 || c.layout_revision > 1 || c.root_locator_reason > SC_REASON_STALE ||
        c.disk_hash_reason > SC_REASON_STALE || (c.layout_revision && c.profile != SC_PROFILE_STEAM_20260818)) return false;
    for (size_t i = 0; i < 64; ++i) {
        const char ch = static_cast<char>(r.number(1));
        if (c.disk_hash_reason == SC_REASON_NONE ? !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) : ch != 0) return false;
        c.disk_sha256[i] = ch;
    }
    auto& map = c.current_map;
    map.validity = u32(); map.reason = u32(); map.win32_error = u32(); map.length = static_cast<uint32_t>(r.number(2));
    if (map.validity > SC_OBSERVATION_OBSERVED || map.reason > SC_CONTEXT_EMPTY_NAME || map.length >= SC_CONTEXT_MAP_CAPACITY) return false;
    if (map.validity == SC_OBSERVATION_UNKNOWN) { if (!map.reason || map.length) return false; }
    else if (map.reason || map.win32_error || !map.length) return false;
    for (size_t i = 0; i < map.length; ++i) {
        const auto ch = r.number(1); if (ch < 32 || ch > 126) return false; map.bytes[i] = static_cast<char>(ch);
    }
    for (size_t i = 0; i < SC_CONTEXT_FIELD_COUNT; ++i) {
        auto& f = c.fields[i]; f.validity = u32(); f.reason = u32(); f.value = r.number(8); f.win32_error = u32();
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
template<class C> void scope_values(C& c, sc_native_scope& s) {
    c.u32(s.pid); c.u64(s.process_created);
    for (auto& b : s.instance_id) c.byte(b);
    c.u64(s.lifecycle_generation);
}
template<class C> bool map_values(C& c, sc_context_map& m) {
    c.u32(m.validity); c.u32(m.reason); c.u32(m.win32_error); c.u32(m.length);
    if (m.length >= SC_CONTEXT_MAP_CAPACITY || m.reason > SC_CONTEXT_EMPTY_NAME ||
        (m.validity != SC_OBSERVATION_UNKNOWN && m.validity != SC_OBSERVATION_OBSERVED)) return false;
    if (m.validity == SC_OBSERVATION_UNKNOWN ? m.length != 0 : (!m.length || m.reason || m.win32_error)) return false;
    for (size_t i = 0; i < m.length; ++i) {
        auto b = static_cast<uint8_t>(m.bytes[i]); c.byte(b);
        if (b < 32 || b > 126) return false;
        m.bytes[i] = static_cast<char>(b);
    }
    m.bytes[m.length] = 0; return true;
}
template<class C> bool native_values(C& c, sc_native_snapshot& n) {
    scope_values(c, n.scope);
    c.u32(n.availability); c.u32(n.reason); c.u32(n.site_revision); c.u32(n.site_rva); c.u32(n.phase);
    c.u32(n.installed_hooks); for (auto& v : n.validator_reasons) c.u32(v);
    c.u32(n.retained_module); c.u32(n.coverage); c.u32(n.lifecycle); c.u32(n.depth);
    c.u32(n.checkpoint_flag_known); c.u32(n.checkpoint_flag);
    c.u64(n.event_sequence); c.u64(n.event_gap_count); c.u64(n.history_oldest); c.u64(n.history_overwritten);
    c.u64(n.callback_sequence); c.u64(n.callback_at_ms); c.u32(n.callback_thread_id); c.u32(n.native_owner_thread_id);
    c.u64(n.context_generation); c.u64(n.context_sampled_at_ms); c.u32(n.context_reason); c.u32(n.game_state);
    if (!map_values(c, n.current_map)) return false;
    c.u32(n.queued); c.u32(n.claimed); c.u32(n.retained_results); c.u32(n.history_gap); c.u32(n.event_count);
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
        auto& e = n.events[i]; c.u64(e.sequence); c.u64(e.generation); c.u64(e.at_ms);
        c.u32(e.kind); c.u32(e.lifecycle); c.u32(e.thread_id); c.u32(e.depth);
        if (!e.sequence || e.sequence > n.event_sequence || e.generation > n.scope.lifecycle_generation ||
            e.kind < SC_EVENT_CHANGE_BEGIN || e.kind > SC_EVENT_GAP || e.lifecycle > SC_LIFETIME_INVALID ||
            (i && e.sequence <= n.events[i - 1].sequence)) return false;
    }
    return true;
}
template<class C> bool diagnostic_values(C& c, sc_diagnostic_result& d, bool backup = false) {
    c.u32(d.state); c.u32(d.reason); c.u32(d.cancel_requested); c.u32(d.retrieved); scope_values(c, d.scope);
    c.u64(d.request_id); for (auto& b : d.nonce) c.byte(b);
    c.u64(d.admitted_at_ms); c.u64(d.deadline_at_ms); c.u64(d.claimed_at_ms); c.u64(d.observed_at_ms); c.u64(d.executed_at_ms);
    c.u64(d.completed_at_ms); c.u64(d.retrieved_at_ms);
    c.u32(d.thread_id); c.u32(d.site_revision); c.u32(d.phase); c.u32(d.lifecycle); c.u32(d.game_state);
    if (!map_values(c, d.current_map) || d.state > SC_DIAGNOSTIC_CANCELLED || d.reason > SC_NATIVE_EXCEPTION ||
        d.cancel_requested > 1 || d.retrieved > 1 || d.site_revision > 1 || d.phase > 1 ||
        d.lifecycle > SC_LIFETIME_INVALID || d.game_state > SC_GAME_IN_GAME) return false;
    if (backup) {
        if (!d.executed_at_ms) return !d.observed_at_ms && !d.current_map.length && d.state != SC_DIAGNOSTIC_EXECUTED;
        return d.observed_at_ms && d.claimed_at_ms && d.admitted_at_ms && d.thread_id &&
            d.scope.lifecycle_generation && d.phase == 1 && d.site_revision == 1 && d.lifecycle == SC_LIFETIME_ACTIVE &&
            d.current_map.validity == SC_OBSERVATION_OBSERVED && d.game_state == SC_GAME_IN_GAME &&
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
    c.u32(d.revision); c.u32(d.stage); c.u32(d.observation_attempted); c.u32(d.observation_accepted);
    c.u32(d.timing_valid); c.u32(d.timing_error); c.u32(d.claim_lock_missed);
    c.u64(d.observation_started_at_ms); c.u64(d.observation_elapsed_ns); c.u64(d.observation_budget_ns);
    c.u32(d.sample_reason); c.u32(d.state_validity); c.u32(d.state_reason); c.u32(d.state_error);
    c.u32(d.map_validity); c.u32(d.map_reason); c.u32(d.map_error);
    c.u32(d.pending_before); c.u32(d.pending_before_reason); c.u32(d.pending_before_error);
    c.u32(d.pending_after); c.u32(d.pending_after_reason); c.u32(d.pending_after_error);
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
    c.u64(v.operation_id); c.u32(v.flags); c.u32(v.native_exception);
    c.u32(v.storage_outcome); c.u32(v.storage_error); c.u32(v.files); c.u64(v.bytes);
    for (auto& b : v.basename) { auto n = static_cast<uint8_t>(b); c.byte(n); b = static_cast<char>(n); }
    for (auto& b : v.namespace_id) { auto n = static_cast<uint8_t>(b); c.byte(n); b = static_cast<char>(n); }
    c.u32(v.campaign); c.u32(v.slot);
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
size_t encode_backup_request(Message& out, uint16_t op, const sc_save_backup_request& request) {
    if (op < save_backup_submit_operation || op > save_backup_cancel_operation) return 0;
    const auto end = encode_native_request(out, diagnostic_submit_operation, request.execution);
    Writer h{out}; header(h, wire_version, op, 149, WireResult::ok); h.number(save_backup_capability, 8);
    Writer w{out, end};
    for (auto b : request.namespace_id) w.number(static_cast<uint8_t>(b), 1);
    w.number(request.campaign, 4); w.number(request.slot, 4); w.number(request.work_deadline_ms, 4);
    return w.valid ? w.pos : 0;
}
size_t encode_backup_response(Message& out, WireResult result, uint16_t op, const Snapshot& s, const sc_save_backup_snapshot& value) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(save_backup_capability, 8); w.number(s.pid, 4); w.number(s.process_created, 8);
        for (auto b : s.instance) w.number(b, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id);
        auto v = value; if (!backup_values(w, v)) return 0;
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
bool decode_backup_response(const Message& in, size_t size, WireResult& result, uint16_t op, Snapshot& s, sc_save_backup_snapshot& v) {
    if (size < header_size || size > max_message || op < save_backup_submit_operation || op > save_backup_cancel_operation) return false;
    Reader r{in, size};
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != op || r.number(4) != size - header_size) return false;
    const auto code = r.number(4); if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code); if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != save_backup_capability) return false;
    s = {}; v = {}; s.core.size = sizeof(s.core); v.size = sizeof(v);
    r.u32(s.pid); r.u64(s.process_created); for (auto& b : s.instance) r.byte(b);
    r.u32(s.core.abi_version); r.text(s.core.version); r.text(s.core.build_id);
    return s.core.abi_version == SC_ABI_VERSION && backup_values(r, v) && r.valid && r.pos == size &&
        v.execution.scope.pid == s.pid && v.execution.scope.process_created == s.process_created &&
        !std::memcmp(v.execution.scope.instance_id, s.instance.data(), 16);
}
const char* backup_state_name(uint32_t state) {
    constexpr const char* names[] = {"unknown", "queued", "claimed", "waiting_native", "copying", "copied",
        "complete", "failed", "rejected", "cancelled", "expired"};
    return state <= SC_BACKUP_EXPIRED ? names[state] : "invalid";
}
size_t encode_native_request(Message& out, uint16_t op, const sc_diagnostic_request& request, uint64_t after) {
    Writer w{out}; header(w, wire_version, op, 0, WireResult::ok);
    w.number(native_wire_capability(op), 8);
    if (op == native_operation) w.number(after, 8);
    else {
        auto r = request; scope_values(w, r.expected); w.u64(r.request_id);
        for (auto& b : r.nonce) w.byte(b); w.u32(r.deadline_ms);
    }
    Writer length{out, 8}; length.number(w.pos - header_size, 4); return w.valid ? w.pos : 0;
}
size_t encode_native_response(Message& out, WireResult result, uint16_t op, const Snapshot& s,
                              const sc_native_snapshot& native, const sc_diagnostic_result& diagnostic,
                              const sc_diagnostic_detail& detail) {
    Writer w{out}; header(w, wire_version, op, 0, result);
    if (result == WireResult::ok) {
        w.number(native_wire_capability(op), 8);
        w.number(s.pid, 4); w.number(s.process_created, 8); for (auto b : s.instance) w.number(b, 1);
        w.number(s.core.abi_version, 4); w.text(s.core.version); w.text(s.core.build_id); w.number(SC_NATIVE_ABI_VERSION, 4);
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
    if (r.number(4) != magic || r.number(2) != wire_version || r.number(2) != op || r.number(4) != size - header_size) return false;
    const auto code = r.number(4); if (code > static_cast<uint32_t>(WireResult::malformed)) return false;
    result = static_cast<WireResult>(code); if (result != WireResult::ok) return size == header_size;
    if (r.number(8) != native_wire_capability(op)) return false;
    s = {}; n = {}; d = {}; s.core.size = sizeof(sc_status); n.size = sizeof(n); n.abi_version = SC_NATIVE_ABI_VERSION;
    r.u32(s.pid); r.u64(s.process_created); for (auto& b : s.instance) r.byte(b);
    r.u32(s.core.abi_version); r.text(s.core.version); r.text(s.core.build_id);
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
}
