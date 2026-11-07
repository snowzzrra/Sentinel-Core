#pragma once
#include <cstddef>

namespace sentinel {
inline bool valid_namespace_id(const char (&id)[65]) noexcept {
    if (id[64]) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    return true;
}
} // namespace sentinel
