#pragma once
#include "sentinel_inspection.h"

namespace sentinel {
using Message = std::array<uint8_t, max_message>;
constexpr size_t header_size = 16;
size_t encode_request(Message& out, uint64_t required, uint16_t version = wire_version,
                      uint16_t operation = inspect_operation);
WireResult decode_request(const Message& in, size_t size, uint16_t* operation = nullptr,
                          sc_diagnostic_request* diagnostic = nullptr, uint64_t* after_event = nullptr);
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
}
