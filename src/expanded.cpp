#include "expanded.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <set>

namespace sentinel::expanded {
float spawn_lift(std::string_view name) noexcept {
    const auto tracking = spawn_tracking(name);
    constexpr uint64_t airborne = 0x40 | 0x2000 | 0x40000 | 0x80000 | 0x1000000;
    return tracking && tracking->physical == 0x40000 && !(tracking->source & airborne) ? 1.25f : 0.0f;
}
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
std::optional<SpawnRecipe> spawn_recipe(std::string_view name) noexcept {
    constexpr std::string_view prefix = "ap_v070_ai_r";
    constexpr auto offset = prefix.size() + 20;
    if (!spawn_tracking(name) || name.substr(0, prefix.size()) != prefix || name.size() < offset + 29 ||
        name[offset] != '_' || name.substr(offset + 9, 2) != "_q") return {};
    SpawnRecipe result{};
    const auto origin = name.substr(prefix.size(), result.origin.size());
    if (origin.find_first_not_of("0123456789abcdef") != origin.npos) return {};
    for (size_t i = 0; i < result.origin.size(); ++i) result.origin[i] = origin[i];
    uint32_t* fields[]{&result.body, &result.logical_units, &result.physical_units};
    constexpr size_t offsets[]{1, 11, 19};
    for (size_t i = 0; i < std::size(fields); ++i) {
        const auto value = name.substr(offset + offsets[i], 8);
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), *fields[i], 16);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || *fields[i] > INT32_MAX) return {};
    }
    if (!result.logical_units || !result.physical_units || result.body >= result.physical_units) return {};
    result.credit_units = result.logical_units;
    auto end = offset + 27;
    if (name.substr(end, 2) == "_w") {
        const auto value = name.substr(end + 2, 8);
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result.credit_units, 16);
        if (value.size() != 8 || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            !result.credit_units || result.credit_units >= result.logical_units) return {};
        end += 10;
    }
    if (name.substr(end, 2) != "_t" || name.rfind("_t") != end) return {};
    return result;
}
bool parse_recipe_library(std::string_view text, RecipeLibrary& out) {
    if (text.empty() || text.size() > maximum_recipe_metadata || text.back() != '\n' || text.find('\0') != text.npos) return false;
    auto field = [](std::string_view& input, char delimiter) {
        const auto end = input.find(delimiter);
        if (end == input.npos) return std::string_view{};
        const auto value = input.substr(0, end); input.remove_prefix(end + 1); return value;
    };
    auto lower_hex = [](std::string_view value, size_t size) {
        return value.size() == size && value.find_first_not_of("0123456789abcdef") == value.npos;
    };
    auto identifier = [](std::string_view value) {
        return value.size() <= 191 && value.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-") == value.npos;
    };
    auto number = [&](std::string_view value, size_t size, uint64_t& out) {
        if (!lower_hex(value, size)) return false;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out, 16);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    };
    RecipeLibrary result;
    auto header = field(text, '\n');
    const auto version = field(header, '|');
    if (version != "EAP1" && version != "EAP2" && version != "EAP3" && version != "EAP4") return false;
    const auto plan = field(header, '|');
    if (!lower_hex(plan, 64) || header.empty() || !identifier(header)) return false;
    result.plan = plan; result.map = header;
    std::set<std::string> origins;
    while (!text.empty()) {
        auto line = field(text, '\n');
        const auto origin = field(line, '|'), family = field(line, '|'), source = field(line, '|'), owner = field(line, '|');
        const auto logical = field(line, '|');
        if (!lower_hex(origin, 20) || family.size() != 1 || std::string_view("igtmaspbduf").find(family[0]) == family.npos ||
            !identifier(source) || !identifier(owner) || result.recipes.size() >= 100000 ||
            !origins.emplace(origin).second) return false;
        RecipeLibraryEntry entry; uint64_t mask = 0;
        if (!number(logical, 16, mask) || !mask || (mask & (mask - 1)) || (mask & ~encounter_ai_mask)) return false;
        entry.origin = origin; entry.family = family[0]; entry.source = source; entry.owner = owner; entry.logical_mask = mask;
        uint32_t* values[]{&entry.logical_count, &entry.physical_count, &entry.maximum_emissions, &entry.final_credit_units};
        for (size_t i = 0; i < std::size(values); ++i) {
            uint64_t value = 0;
            if (!number(i + 1 == std::size(values) && version == "EAP1" ? line : field(line, '|'), 8, value) || value > INT32_MAX) return false;
            *values[i] = static_cast<uint32_t>(value);
        }
        if (!entry.logical_count || entry.final_credit_units > entry.logical_count ||
            bool(entry.final_credit_units) != bool(entry.maximum_emissions) || (!entry.physical_count && entry.maximum_emissions)) return false;
        if (entry.maximum_emissions && ((uint64_t{entry.maximum_emissions - 1} * entry.logical_count +
            entry.final_credit_units) % entry.physical_count)) return false;
        if (version != "EAP1") {
            const auto entity = field(line, '|');
            if (!identifier(entity)) return false;
            entry.call_entity = entity;
            for (auto* label : {&entry.first_label, &entry.second_label}) {
                const auto encoded = field(line, '|');
                if (encoded.size() > 2048 || encoded.size() % 2 || !lower_hex(encoded, encoded.size())) return false;
                for (size_t i = 0; i < encoded.size(); i += 2) {
                    uint64_t byte = 0;
                    if (!number(encoded.substr(i, 2), 2, byte) || !byte) return false;
                    *label += static_cast<char>(byte);
                }
            }
            uint64_t flag = 0;
            if (!number(field(line, '|'), 16, entry.allowed_hosts) || entry.allowed_hosts >= uint64_t{1} << 45 ||
                !number(version == "EAP3" || version == "EAP4" ? field(line, '|') : line, 2, flag) || flag > 1) return false;
            entry.call_flag = flag != 0;
            if (version == "EAP3" || version == "EAP4") {
                auto health_text = version == "EAP4" ? field(line, '|') : line;
                if (health_text.size() != uint64_t{entry.physical_count} * 16) return false;
                entry.hitpoints.reserve(entry.physical_count);
                for (uint32_t i = 0; i < entry.physical_count; ++i) {
                    RecipeHitpoints health;
                    for (float* value : {&health.maximum, &health.starting}) {
                        uint64_t raw = 0;
                        if (!number(health_text.substr(0, 8), 8, raw)) return false;
                        const auto bits = static_cast<uint32_t>(raw);
                        std::memcpy(value, &bits, sizeof(bits)); health_text.remove_prefix(8);
                    }
                    if (!std::isfinite(health.maximum) || health.maximum <= 0 ||
                        !std::isfinite(health.starting) || health.starting < 0 || health.starting > health.maximum) return false;
                    entry.hitpoints.push_back(health);
                }
            }
            if (version == "EAP4") {
                uint64_t numerator = 0, denominator = 0;
                if (line.size() != 16 || !number(line.substr(0, 8), 8, numerator) || numerator > INT32_MAX ||
                    !number(line.substr(8), 8, denominator) || !denominator || denominator > INT32_MAX ||
                    (numerator && !entry.physical_count)) return false;
                entry.quantity_numerator = static_cast<uint32_t>(numerator);
                entry.quantity_denominator = static_cast<uint32_t>(denominator);
            }
        }
        result.recipes.push_back(std::move(entry));
    }
    out = std::move(result); return true;
}
}
