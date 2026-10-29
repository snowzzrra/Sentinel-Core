#pragma once
#include "protocol.h"
#include "sentinel_protocol_generated.h"
#include <algorithm>

namespace sentinel {
using MessageHandler = size_t (*)(Message&, const DecodedRequest&);
using MessageHandlers = std::array<MessageHandler, std::size(generated::v1_descriptors)>;

inline size_t process_message(Message& data, size_t size, const MessageHandlers& handlers) {
    DecodedRequest request;
    const auto result = decode_request(data, size, request);
    if (result != WireResult::ok) {
        generated::Bytes bytes;
        const generated::wire_header_1 header{generated::wire_magic, wire_version, request.operation, 0,
                                              static_cast<uint32_t>(result)};
        if (!generated::encode(header, bytes)) return 0;
        std::copy(bytes.begin(), bytes.end(), data.begin());
        return bytes.size();
    }
    return handlers[request.operation - 1](data, request);
}

size_t process_message(Message& data, size_t size);
}
