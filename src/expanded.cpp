#include "expanded.h"

namespace sentinel::expanded {
float movement_speed(float native_speed, std::string_view name) noexcept {
    constexpr std::string_view prefix = "ap_v070_ai_";
    constexpr std::string_view suffix = "__ap_v070_s";
    if (name.size() >= 512 || name.size() <= prefix.size() + suffix.size() + 5 ||
        name.substr(0, prefix.size()) != prefix) return native_speed;
    const auto marker = name.size() - suffix.size() - 5;
    if (name.substr(marker, suffix.size()) != suffix) return native_speed;
    unsigned percent = 0;
    for (const char digit : name.substr(name.size() - 5)) {
        if (digit < '0' || digit > '9') return native_speed;
        percent = percent * 10 + static_cast<unsigned>(digit - '0');
    }
    if (percent < 1 || percent > 10000) return native_speed;
    return native_speed * (static_cast<float>(percent) / 100.0f);
}
}
