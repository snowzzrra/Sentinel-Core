#pragma once
#include "sentinel_inspection.h"

namespace sentinel {
using Message = std::array<uint8_t, max_message>;
constexpr size_t header_size = 16;
size_t encode_request(Message& out, uint64_t required, uint16_t version = wire_version,
                      uint16_t operation = inspect_operation);
WireResult decode_request(const Message& in, size_t size);
size_t encode_response(Message& out, WireResult result, const Snapshot& snapshot);
bool decode_response(const Message& in, size_t size, WireResult& result, Snapshot& snapshot);
}
