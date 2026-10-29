#pragma once
#include "sentinel_protocol_generated.h"

namespace sentinel::v2 {
using namespace generated;
// Command bodies carry the reference/dimensions, status, execution facts, then the typed result.
enum class CommandError : uint32_t { none, duplicate_mismatch, identity_capacity, unsupported_revision, not_found, malformed, not_terminal, token_mismatch, effect_unknown };
constexpr size_t submission_fixed = 126, reply_fixed = 186;
bool split_submission(const Bytes&, submission&, Bytes&);
bool split_read(const Bytes&, read_envelope&, Bytes&);
bool native_payload(const Descriptor&, const Bytes&, const submission&, NativeRequest&, Operation);
bool fingerprint(const submission&, const NativeRequest&, std::array<uint8_t, 32>&);
bool encode_native_result(const NativeResult&, Bytes&);
bool frame(const wire_header_2&, const Bytes&, Bytes&);
bool header(const Bytes&, wire_header_2&, bool response);
bool command_response(const Bytes&, command_reply&, CommandError&, generated::sc_diagnostic_result&, Bytes&);
}
