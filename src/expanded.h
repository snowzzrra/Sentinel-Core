#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace sentinel::expanded {
inline constexpr std::size_t maximum_recipe_metadata = 64 * 1024 * 1024;
float movement_speed(float native_speed, std::string_view entity_definition) noexcept;
inline constexpr std::uint64_t encounter_ai_mask = (std::uint64_t{1} << 44) - 1;
struct SpawnTracking { std::uint64_t source, physical; };
std::optional<SpawnTracking> spawn_tracking(std::string_view definition) noexcept;
float spawn_lift(std::string_view definition) noexcept;
struct SpawnRecipe {
    std::array<char, 20> origin;
    std::uint32_t body, logical_units, physical_units, credit_units;
};
std::optional<SpawnRecipe> spawn_recipe(std::string_view definition) noexcept;
struct RecipeHitpoints { float maximum = 0, starting = 0; };
struct RecipeLibraryEntry {
    std::string origin, source, owner;
    char family = 0;
    std::uint64_t logical_mask = 0;
    std::uint32_t logical_count = 0, physical_count = 0, maximum_emissions = 0, final_credit_units = 0;
    std::string call_entity, first_label, second_label;
    std::uint64_t allowed_hosts = 0;
    bool call_flag = false;
    std::vector<RecipeHitpoints> hitpoints;
    uint32_t quantity_numerator = 0, quantity_denominator = 0;
};
struct RecipeLibrary {
    std::string plan, map;
    std::vector<RecipeLibraryEntry> recipes;
};
bool parse_recipe_library(std::string_view, RecipeLibrary&);
struct SpawnLimits {
    std::array<std::uint32_t, 4> maximum;
    unsigned kind;
    std::uint32_t population = 0, superheavy = 0, pressure = 0, mixed = 0, heavy = 0;
    std::uint64_t scope = 0;
};
std::optional<SpawnLimits> spawn_limits(std::string_view definition) noexcept;
bool spawn_allowed(const SpawnLimits&, const std::array<std::uint32_t, 9>& active) noexcept;
}
