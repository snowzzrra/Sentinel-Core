#include "protocol.h"
#include <cstring>

namespace sentinel {
namespace {
constexpr uint32_t magic = 0x50494353; // "SCIP", little endian.
struct Writer {
    Message& data;
    size_t pos = 0;
    void number(uint64_t n, size_t width) {
        for (size_t i = 0; i < width; ++i) { data[pos++] = static_cast<uint8_t>(n); n >>= 8; }
    }
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
WireResult decode_request(const Message& in, size_t size) {
    if (size < header_size || size > max_message) return WireResult::malformed;
    Reader r{in, size};
    if (r.number(4) != magic) return WireResult::malformed;
    const auto version = r.number(2), op = r.number(2), length = r.number(4), result = r.number(4);
    if (length != size - header_size || result != 0) return WireResult::malformed;
    if (version != wire_version) return WireResult::incompatible_protocol;
    if (op != inspect_operation) return WireResult::unsupported_operation;
    if (length != 8) return WireResult::malformed;
    return (r.number(8) & ~inspect_capability) ? WireResult::capability_unavailable : WireResult::ok;
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
