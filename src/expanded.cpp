#include "expanded.h"
#include <charconv>

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

std::optional<SpawnLimits> spawn_limits(std::string_view name) noexcept {
    constexpr std::string_view speed = "__ap_v070_s";
    constexpr std::size_t marker_size = 2 + 32 + 3 + speed.size() + 5;
    if (name.size() >= 512 || name.size() < marker_size || name.substr(0, 8) != "ap_v070_") return {};
    const auto marker = name.size() - marker_size;
    if (name.substr(marker, 2) != "_l" || name.substr(marker + 34, 2) != "_k" ||
        name.substr(marker + 37, speed.size()) != speed) return {};
    SpawnLimits result{};
    const auto kind = name[marker + 36];
    const auto kinds = std::string_view("masn").find(kind);
    if (kinds == std::string_view::npos) return {};
    result.kind = static_cast<unsigned>(kinds);
    for (std::size_t index = 0; index < result.maximum.size(); ++index) {
        const auto digits = name.substr(marker + 2 + index * 8, 8);
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result.maximum[index], 16);
        if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || result.maximum[index] > INT32_MAX) return {};
    }
    return result;
}
std::optional<SpawnTracking> spawn_tracking(std::string_view name) noexcept {
    if (!spawn_limits(name)) return {};
    const auto marker = name.rfind("_t");
    if (marker == std::string_view::npos || name.size() - marker < 36 || name.substr(marker + 34, 2) != "_l") return {};
    SpawnTracking result{};
    std::uint64_t* fields[] = {&result.source, &result.physical};
    for (std::size_t index = 0; index < 2; ++index) {
        const auto digits = name.substr(marker + 2 + index * 16, 16);
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), *fields[index], 16);
        const auto value = *fields[index];
        if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
            value == 0 || (value & (value - 1)) != 0 || (value & ~encounter_ai_mask) != 0) return {};
    }
    return result;
}
bool spawn_allowed(const SpawnLimits& limits, const std::array<std::uint32_t, 3>& active) noexcept {
    if (limits.kind >= active.size()) return true;
    return active[limits.kind] < limits.maximum[limits.kind] &&
        static_cast<std::uint64_t>(active[0]) + active[1] + active[2] < limits.maximum[3];
}
}
