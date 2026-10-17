#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sentinel::expanded {
float movement_speed(float native_speed, std::string_view entity_definition) noexcept;
inline constexpr std::uint64_t encounter_ai_mask = (std::uint64_t{1} << 44) - 1;
struct SpawnTracking { std::uint64_t source, physical; };
std::optional<SpawnTracking> spawn_tracking(std::string_view definition) noexcept;
struct SpawnLimits {
    std::array<std::uint32_t, 4> maximum;
    unsigned kind;
};
std::optional<SpawnLimits> spawn_limits(std::string_view definition) noexcept;
bool spawn_allowed(const SpawnLimits&, const std::array<std::uint32_t, 3>& active) noexcept;
}
