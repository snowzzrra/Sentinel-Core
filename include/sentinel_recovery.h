#pragma once
#include "sentinel_native.h"

namespace sentinel::recovery {
enum Outcome : uint32_t { pending, finished, no_active, changed, unsupported, unknown };
struct Request {
    sc_diagnostic_request execution{};
    char namespace_id[65]{};
    char save[96]{};
    uint64_t token = 0, stamp = 0;
};
struct Result {
    Request request{};
    uint32_t state = SC_DIAGNOSTIC_UNKNOWN;
    Outcome outcome = unknown;
};
inline const char* message(Outcome value) {
    switch (value) {
    case finished: return "Encounter finished";
    case no_active: return "No active encounter";
    case changed: return "Encounter changed; request cancelled";
    case unsupported: return "Unsupported encounter";
    default: return "Recovery outcome unknown";
    }
}
inline bool valid(const Request& r) {
    if (r.namespace_id[64] || r.save[95]) return false;
    for (size_t i = 0; i < 64; ++i)
        if (!(r.namespace_id[i] >= '0' && r.namespace_id[i] <= '9') &&
            !(r.namespace_id[i] >= 'a' && r.namespace_id[i] <= 'f')) return false;
    for (const unsigned char c : r.save)
        if (c && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '/' && c != '-' && c != '_') return false;
    return true;
}
}
