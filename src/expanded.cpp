#include "expanded.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <set>

namespace sentinel::expanded {
float spawn_lift(std::string_view name) noexcept {
    const auto tracking = spawn_tracking(name);
    constexpr uint64_t airborne = 0x40 | 0x2000 | 0x40000 | 0x80000 | 0x1000000;
    constexpr uint64_t floating = 0x2000 | 0x40000 | 0x80000;
    return tracking && (tracking->physical & floating) && !(tracking->source & airborne) ? 1.25f : 0.0f;
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

template<class T>
static bool parse_hex(std::string_view digits, T& out) noexcept {
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), out, 16);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size();
}
static bool parse_cap(std::string_view digits, uint32_t& out) noexcept {
    return parse_hex(digits, out) && out <= INT32_MAX;
}
static bool single_species(uint64_t mask) noexcept {
    return mask && !(mask & (mask - 1)) && !(mask & ~encounter_ai_mask);
}
static bool species_limits(std::string_view name, size_t marker, SpawnLimits& result) noexcept {
    if (!result.strict) return false;
    for (size_t index = 0; index < result.species_maximum.size(); ++index)
        if (!parse_cap(name.substr(marker - 124 + index * 8, 8), result.species_maximum[index])) return false;
    const auto tracking = name.rfind("_t", marker - 126);
    if (tracking == name.npos || tracking + 34 != marker - 126) return false;
    return parse_hex(name.substr(tracking + 18, 16), result.physical) && single_species(result.physical);
}
static bool population_flags(SpawnLimits& result) noexcept {
    result.heavy = result.mixed >> 8;
    const auto flags = result.mixed & 255;
    result.strict = (flags & 2) != 0;
    result.spirit_counter = (flags & 4) != 0;
    result.mixed = flags & 1;
    return result.population && result.pressure && result.pressure <= result.population &&
        !(flags & ~7u) && (!result.spirit_counter || result.strict) && result.heavy <= 128;
}
static bool population_limits(std::string_view name, size_t marker, SpawnLimits& result) noexcept {
    uint32_t* fields[]{&result.population, &result.superheavy, &result.pressure, &result.mixed};
    for (size_t i = 0; i < std::size(fields); ++i)
        if (!parse_cap(name.substr(marker - 32 + i * 8, 8), *fields[i])) return false;
    if (!population_flags(result)) return false;
    if (marker < 52 || name.substr(marker - 52, 2) != "_a") return true;
    if (!parse_hex(name.substr(marker - 50, 16), result.scope) || !result.scope) return false;
    if (marker < 126 || name.substr(marker - 126, 2) != "_v") return true;
    return species_limits(name, marker, result);
}
static bool limits_name(std::string_view name, size_t marker_size) noexcept {
    return name.size() < 512 && name.size() >= marker_size && name.substr(0, 8) == "ap_v070_";
}
static bool limits_markers(std::string_view name, size_t marker, std::string_view speed) noexcept {
    return name.substr(marker, 2) == "_l" && name.substr(marker + 34, 2) == "_k" &&
        name.substr(marker + 37, speed.size()) == speed;
}
std::optional<SpawnLimits> spawn_limits(std::string_view name) noexcept {
    constexpr std::string_view speed = "__ap_v070_s";
    constexpr std::size_t marker_size = 2 + 32 + 3 + speed.size() + 5;
    if (!limits_name(name, marker_size)) return {};
    const auto marker = name.size() - marker_size;
    if (!limits_markers(name, marker, speed)) return {};
    SpawnLimits result{};
    const auto kind = name[marker + 36];
    const auto kinds = std::string_view("masnbchfo").find(kind);
    if (kinds == std::string_view::npos) return {};
    result.kind = static_cast<unsigned>(kinds);
    for (std::size_t index = 0; index < result.maximum.size(); ++index)
        if (!parse_cap(name.substr(marker + 2 + index * 8, 8), result.maximum[index])) return {};
    if (marker >= 34 && name.substr(marker - 34, 2) == "_p" && !population_limits(name, marker, result)) return {};
    const auto extension = name.find("_v", 8);
    if (result.strict && extension < marker && !result.physical) return {};
    if (result.kind >= 6 && !result.population) return {};
    return result;
}
static bool tracking_suffix(std::string_view value) noexcept {
    if (value.substr(0, 2) == "_l") return true;
    if (value.size() >= 36 && value.substr(0, 2) == "_p" && value.substr(34, 2) == "_l") return true;
    return value.size() >= 54 && value.substr(0, 2) == "_a" && value.substr(18, 2) == "_p" && value.substr(52, 2) == "_l";
}
std::optional<SpawnTracking> spawn_tracking(std::string_view name) noexcept {
    if (!spawn_limits(name)) return {};
    const auto marker = name.rfind("_t");
    if (marker == std::string_view::npos || name.size() - marker < 36) return {};
    auto suffix = marker + 34;
    if (name.substr(suffix, 2) == "_v") {
        if (name.size() - suffix < 74) return {};
        suffix += 74;
    }
    const auto remaining = name.substr(suffix);
    if (!tracking_suffix(remaining)) return {};
    SpawnTracking result{};
    std::uint64_t* fields[] = {&result.source, &result.physical};
    for (std::size_t index = 0; index < 2; ++index) {
        const auto digits = name.substr(marker + 2 + index * 16, 16);
        if (!parse_hex(digits, *fields[index]) || !single_species(*fields[index])) return {};
    }
    return result;
}
unsigned special_species_index(std::uint64_t mask) noexcept {
    constexpr std::uint64_t masks[]{0x8000000ull, 0x40000000ull, 0x10000000000ull,
        0x2000000000ull, 0x2ull, 0x1000000ull, 0x100ull, 0x20ull, 0x20000000ull};
    for (unsigned index = 0; index < std::size(masks); ++index) if (mask == masks[index]) return index;
    return 9;
}
bool species_allowed(const SpawnLimits& limits, const std::array<std::uint32_t, 9>& active) noexcept {
    if (!limits.strict || !limits.physical) return true;
    const auto incoming = special_species_index(limits.physical);
    if (incoming >= active.size()) return true;
    uint64_t combined = 0;
    for (const auto count : active) combined += count;
    return active[incoming] < limits.species_maximum[incoming] && combined < limits.maximum[3];
}
static bool pressure_allowed(const SpawnLimits& limits, const std::array<std::uint32_t, 9>& active,
                             uint64_t superheavy, uint64_t special, bool incoming_superheavy, bool incoming_special) noexcept {
    if (!limits.mixed && ((incoming_superheavy && special) || (incoming_special && superheavy))) return false;
    uint64_t population = 0;
    for (const auto count : active) population += count;
    const auto ceiling = (special || incoming_special || superheavy || incoming_superheavy) ? limits.pressure : limits.population;
    if (population < ceiling) return true;
    return !limits.strict && limits.kind == 7 && active[7] < 2;
}
static bool population_allowed(const SpawnLimits& limits, const std::array<std::uint32_t, 9>& active) noexcept {
    const auto superheavy = uint64_t{active[0]} + active[1] + active[6];
    const auto special = uint64_t{active[0]} + active[1] + active[2] + active[4] + active[5];
    const bool incoming_superheavy = limits.kind <= 1 || limits.kind == 6;
    const bool incoming_special = limits.kind <= 2 || limits.kind == 4 || limits.kind == 5;
    if (!pressure_allowed(limits, active, superheavy, special, incoming_superheavy, incoming_special)) return false;
    if (incoming_superheavy && superheavy >= limits.superheavy) return false;
    if ((limits.heavy || limits.strict) && limits.kind == 3 && active[3] >= limits.heavy) return false;
    return true;
}
bool spawn_allowed(const SpawnLimits& limits, const std::array<std::uint32_t, 9>& active) noexcept {
    if (limits.population && !population_allowed(limits, active)) return false;
    if (limits.kind >= 6) return true;
    if (limits.kind == 3 || limits.kind >= active.size()) return true;
    const auto special = uint64_t{active[0]} + active[1] + active[2] + active[4] + active[5];
    const auto individual=limits.kind>=4 ? 1 : limits.kind;
    return active[limits.kind] < limits.maximum[individual] &&
        special < limits.maximum[3];
}
static bool recipe_credit(std::string_view name, size_t& end, SpawnRecipe& result) noexcept {
    result.credit_units = result.logical_units;
    if (name.substr(end, 2) != "_w") return true;
    const auto value = name.substr(end + 2, 8);
    if (value.size() != 8 || !parse_hex(value, result.credit_units) ||
        !result.credit_units || result.credit_units >= result.logical_units) return false;
    end += 10;
    return true;
}
static bool recipe_name(std::string_view name, std::string_view prefix, size_t offset) noexcept {
    return spawn_tracking(name).has_value() && name.substr(0, prefix.size()) == prefix && name.size() >= offset + 29 &&
        name[offset] == '_' && name.substr(offset + 9, 2) == "_q";
}
std::optional<SpawnRecipe> spawn_recipe(std::string_view name) noexcept {
    constexpr std::string_view prefix = "ap_v070_ai_r";
    constexpr auto offset = prefix.size() + 20;
    if (!recipe_name(name, prefix, offset)) return {};
    SpawnRecipe result{};
    const auto origin = name.substr(prefix.size(), result.origin.size());
    if (origin.find_first_not_of("0123456789abcdef") != origin.npos) return {};
    for (size_t i = 0; i < result.origin.size(); ++i) result.origin[i] = origin[i];
    uint32_t* fields[]{&result.body, &result.logical_units, &result.physical_units};
    constexpr size_t offsets[]{1, 11, 19};
    for (size_t i = 0; i < std::size(fields); ++i) {
        const auto value = name.substr(offset + offsets[i], 8);
        if (!parse_cap(value, *fields[i])) return {};
    }
    if (!result.logical_units || !result.physical_units || result.body >= result.physical_units) return {};
    auto end = offset + 27;
    if (!recipe_credit(name, end, result)) return {};
    if (name.substr(end, 2) != "_t" || name.rfind("_t") != end) return {};
    return result;
}
static std::string_view recipe_field(std::string_view& input, char delimiter) {
        const auto end = input.find(delimiter);
        if (end == input.npos) return std::string_view{};
        const auto value = input.substr(0, end); input.remove_prefix(end + 1); return value;
}
static bool lower_hex(std::string_view value, size_t size) {
        return value.size() == size && value.find_first_not_of("0123456789abcdef") == value.npos;
}
static bool recipe_identifier(std::string_view value) {
        return value.size() <= 191 && value.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-") == value.npos;
}
static bool recipe_number(std::string_view value, size_t size, uint64_t& out) {
        if (!lower_hex(value, size)) return false;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out, 16);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
static bool recipe_labels(std::string_view& line, RecipeLibraryEntry& entry) {
    for (auto* label : {&entry.first_label, &entry.second_label}) {
        const auto encoded = recipe_field(line, '|');
        if (encoded.size() > 2048 || encoded.size() % 2 || !lower_hex(encoded, encoded.size())) return false;
        for (size_t i = 0; i < encoded.size(); i += 2) {
            uint64_t byte = 0;
            if (!recipe_number(encoded.substr(i, 2), 2, byte) || !byte) return false;
            *label += static_cast<char>(byte);
        }
    }
    return true;
}
static bool recipe_health(std::string_view& line, std::string_view version, RecipeLibraryEntry& entry) {
    auto health_text = version == "EAP4" ? recipe_field(line, '|') : line;
    if (health_text.size() != uint64_t{entry.physical_count} * 16) return false;
    entry.hitpoints.reserve(entry.physical_count);
    for (uint32_t i = 0; i < entry.physical_count; ++i) {
        RecipeHitpoints health;
        for (float* value : {&health.maximum, &health.starting}) {
            uint64_t raw = 0;
            if (!recipe_number(health_text.substr(0, 8), 8, raw)) return false;
            const auto bits = static_cast<uint32_t>(raw);
            std::memcpy(value, &bits, sizeof(bits)); health_text.remove_prefix(8);
        }
        if (!std::isfinite(health.maximum) || health.maximum <= 0 ||
            !std::isfinite(health.starting) || health.starting < 0 || health.starting > health.maximum) return false;
        entry.hitpoints.push_back(health);
    }
    return true;
}
static bool recipe_quantity(std::string_view line, RecipeLibraryEntry& entry) {
    uint64_t numerator = 0, denominator = 0;
    if (line.size() != 16 || !recipe_number(line.substr(0, 8), 8, numerator) || numerator > INT32_MAX ||
        !recipe_number(line.substr(8), 8, denominator) || !denominator || denominator > INT32_MAX ||
        (numerator && !entry.physical_count)) return false;
    entry.quantity_numerator = static_cast<uint32_t>(numerator);
    entry.quantity_denominator = static_cast<uint32_t>(denominator);
    return true;
}
static bool recipe_counts(std::string_view& line, std::string_view version, RecipeLibraryEntry& entry) {
    uint32_t* values[]{&entry.logical_count, &entry.physical_count, &entry.maximum_emissions, &entry.final_credit_units};
    for (size_t i = 0; i < std::size(values); ++i) {
        uint64_t value = 0;
        if (!recipe_number(i + 1 == std::size(values) && version == "EAP1" ? line : recipe_field(line, '|'), 8, value) || value > INT32_MAX) return false;
        *values[i] = static_cast<uint32_t>(value);
    }
    if (!entry.logical_count || entry.final_credit_units > entry.logical_count ||
        bool(entry.final_credit_units) != bool(entry.maximum_emissions) || (!entry.physical_count && entry.maximum_emissions)) return false;
    if (entry.maximum_emissions && ((uint64_t{entry.maximum_emissions - 1} * entry.logical_count +
        entry.final_credit_units) % entry.physical_count)) return false;
    return true;
}
static bool recipe_extensions(std::string_view& line, std::string_view version, RecipeLibraryEntry& entry) {
    if (version == "EAP1") return true;
    const auto entity = recipe_field(line, '|');
    if (!recipe_identifier(entity)) return false;
    entry.call_entity = entity;
    if (!recipe_labels(line, entry)) return false;
    uint64_t flag = 0;
    if (!recipe_number(recipe_field(line, '|'), 16, entry.allowed_hosts) || entry.allowed_hosts >= uint64_t{1} << 45 ||
        !recipe_number(version == "EAP3" || version == "EAP4" ? recipe_field(line, '|') : line, 2, flag) || flag > 1) return false;
    entry.call_flag = flag != 0;
    if (version == "EAP3" || version == "EAP4") {
        if (!recipe_health(line, version, entry)) return false;
    }
    if (version == "EAP4") {
        if (!recipe_quantity(line, entry)) return false;
    }
    return true;
}
static bool recipe_entry(std::string_view line, std::string_view version, std::set<std::string>& origins, RecipeLibraryEntry& entry) {
    const auto origin = recipe_field(line, '|'), family = recipe_field(line, '|'), source = recipe_field(line, '|'), owner = recipe_field(line, '|');
    const auto logical = recipe_field(line, '|');
    if (!lower_hex(origin, 20) || family.size() != 1 || std::string_view("igtmaspbduf").find(family[0]) == family.npos ||
        !recipe_identifier(source) || !recipe_identifier(owner) ||
        !origins.emplace(origin).second) return false;
    uint64_t mask = 0;
    if (!recipe_number(logical, 16, mask) || !mask || (mask & (mask - 1)) || (mask & ~encounter_ai_mask)) return false;
    entry.origin = origin; entry.family = family[0]; entry.source = source; entry.owner = owner; entry.logical_mask = mask;
    return recipe_counts(line, version, entry) && recipe_extensions(line, version, entry);
}
bool parse_recipe_library(std::string_view text, RecipeLibrary& out) {
    if (text.empty() || text.size() > maximum_recipe_metadata || text.back() != '\n' || text.find('\0') != text.npos) return false;
    RecipeLibrary result;
    auto header = recipe_field(text, '\n');
    const auto version = recipe_field(header, '|');
    if (version != "EAP1" && version != "EAP2" && version != "EAP3" && version != "EAP4") return false;
    const auto plan = recipe_field(header, '|');
    if (!lower_hex(plan, 64) || header.empty() || !recipe_identifier(header)) return false;
    result.plan = plan; result.map = header;
    std::set<std::string> origins;
    while (!text.empty()) {
        auto line = recipe_field(text, '\n');
        RecipeLibraryEntry entry;
        if (result.recipes.size() >= 100000 || !recipe_entry(line, version, origins, entry)) return false;
        result.recipes.push_back(std::move(entry));
    }
    out = std::move(result); return true;
}
}
