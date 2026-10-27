#include "expanded.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
}
int main() {
    using sentinel::expanded::movement_speed;
    require(near(movement_speed(1, "ap_v070_ai_a__ap_v070_s00001"), 0.01f), "minimum speed");
    require(near(movement_speed(1, "ap_v070_ai_a__ap_v070_s10000"), 100), "maximum speed");
    require(near(movement_speed(2, "ap_v070_ai_a__ap_v070_s00150"), 3), "native buff composes with frozen speed");
    require(near(movement_speed(1, "ap_v070_ai_a__ap_v070_s00150"), 1.5f), "buff removal preserves frozen speed");
    require(near(movement_speed(1, "ap_v070_ai_b__ap_v070_s00045"), 0.45f), "new definition uses its own speed");
    for (const auto* name : {"ai/fodder/imp", "", "x__ap_v070_s00150", "ap_v070_ai___ap_v070_s00150",
            "ap_v070_ai_a__ap_v069_s00150", "ap_v070_ai_a__ap_v070_s00000", "ap_v070_ai_a__ap_v070_s10001",
            "ap_v070_ai_a__ap_v070_s99999", "ap_v070_ai_a__ap_v070_s0015x", "ap_v070_ai_a__ap_v070_s0015",
            "ap_v070_ai_a__ap_v070_s000150", "ap_v070_ai_a__ap_v070_s00150_x"})
        require(movement_speed(2.5f, name) == 2.5f, "unmarked or invalid definition preserves native speed");
    const std::string prefix = "ap_v070_ai_", suffix = "__ap_v070_s00250";
    const auto longest = prefix + std::string(511 - prefix.size() - suffix.size(), 'a') + suffix;
    require(near(movement_speed(1, longest), 2.5f), "bounded definition name");
    const auto oversized = prefix + std::string(512 - prefix.size() - suffix.size(), 'a') + suffix;
    require(movement_speed(3, oversized) == 3, "oversized definition preserves native speed");
    using sentinel::expanded::spawn_limits;
    using sentinel::expanded::spawn_allowed;
    const auto marauder = spawn_limits("ap_v070_spawn_a_l00000001000000020000000300000004_km__ap_v070_s00100");
    require(marauder.has_value(), "frozen limit marker");
    require(spawn_allowed(*marauder, {0, 0, 0}), "free special slot");
    require(!spawn_allowed(*marauder, {1, 0, 0}), "native special actor consumes its species slot");
    require(!spawn_allowed(*marauder, {0, 2, 2}), "combined limit includes other species");
    require(spawn_allowed(*marauder, {0, 1, 2}), "actor death frees a combined slot");
    const auto totem=spawn_limits("ap_v070_spawn_a_l00000001000000010000000100000002_kb__ap_v070_s00100");
    require(totem && spawn_allowed(*totem,{0,0,0,0,0}), "buff totem has a free special slot");
    require(!spawn_allowed(*totem,{0,0,0,0,1}), "buff totem respects its individual cap");
    require(!spawn_allowed(*totem,{1,1,0,0,0}), "buff totem shares the combined special cap");
    require(!spawn_allowed(*marauder,{0,1,2,0,1}), "buff totem counts against other special admissions");
    const auto screecher=spawn_limits("ap_v070_ai_test_l00000001000000010000000100000002_kc__ap_v070_s00100");
    require(screecher && spawn_allowed(*screecher,{0,0,0,0,0,0}), "screecher has a free special slot");
    require(!spawn_allowed(*screecher,{0,0,0,0,0,1}), "screecher respects its individual cap");
    require(!spawn_allowed(*totem,{1,0,0,0,0,1}), "screecher shares the combined special cap");
    const auto banned = spawn_limits("ap_v070_spawn_a_l00000000000000020000000300000004_km__ap_v070_s00100");
    require(banned && !spawn_allowed(*banned, {0, 0, 0}), "zero species limit");
    const auto ordinary = spawn_limits("ap_v070_ai_a_l00000001000000020000000300000004_kn__ap_v070_s00100");
    require(ordinary && spawn_allowed(*ordinary, {1, 2, 3}), "ordinary actors use native global admission");
    const auto bounded = spawn_limits("ap_v070_ai_a_p00000008000000020000000500000001_l00000001000000010000000100000002_kh__ap_v070_s00100");
    require(bounded && spawn_allowed(*bounded, {0,0,0,2,0,0,1,1}), "one superheavy leaves room for another");
    require(!spawn_allowed(*bounded, {1,0,0,1,0,0,1,1}), "marauders and ordinary superheavies share the same cap");
    require(!spawn_allowed(*bounded, {0,1,0,3,0,0,0,1}), "disruptive combinations wait for lower surrounding pressure");
    require(spawn_allowed(*bounded, {0,1,0,1,0,0,0,1}), "reduced pressure permits an approved combination");
    auto separate = *bounded; separate.mixed = 0;
    require(!spawn_allowed(separate, {0,0,0,0,1,0,0,0}), "ordinary arenas separate superheavies and support enemies");
    auto fodder = *bounded; fodder.kind = 7;
    require(spawn_allowed(fodder, {0,0,0,8,0,0,0,0}), "ammunition fodder can enter a full arena");
    require(!spawn_allowed(fodder, {0,0,0,6,0,0,0,2}), "ammunition exception does not create a fodder swarm");
    auto normal = *bounded; normal.kind = 3;
    require(!spawn_allowed(normal, {0,0,0,8,0,0,0,0}), "ordinary generated bodies respect the population cap");
    const auto heavy = spawn_limits("ap_v070_ai_a_p00000006000000010000000300000200_l00000001000000010000000100000002_kn__ap_v070_s00100");
    require(heavy && heavy->heavy == 2 && !heavy->mixed, "heavy ceiling decodes independently of mixed encounters");
    require(spawn_allowed(*heavy, {0,0,0,1,0,0,0,2}), "one ordinary heavy leaves a free heavy slot");
    require(!spawn_allowed(*heavy, {0,0,0,2,0,0,0,1}), "ordinary heavies cannot exceed their simultaneous ceiling");
    auto ammunition = *heavy; ammunition.kind = 7;
    require(spawn_allowed(ammunition, {0,0,0,2,0,0,0,1}), "heavy ceiling preserves ammunition access");
    const auto strict_zero = spawn_limits("ap_v070_ai_a_p00000003000000000000000300000002_l00000000000000000000000000000000_kn__ap_v070_s00100");
    require(strict_zero && strict_zero->strict && !strict_zero->mixed &&
            !spawn_allowed(*strict_zero, {}), "strict zero heavy slots reject heavy admission");
    auto strict_fodder = *strict_zero; strict_fodder.kind = 7;
    require(spawn_allowed(strict_fodder, {}) && !spawn_allowed(strict_fodder, {0,0,0,0,0,0,0,3}),
            "strict fodder admission preserves the authored total ceiling");
    auto strict_superheavy = *strict_zero; strict_superheavy.kind = 6;
    require(!spawn_allowed(strict_superheavy, {}), "strict zero superheavy slots reject superheavy admission");
    auto strict_special = *strict_zero; strict_special.kind = 0;
    require(!spawn_allowed(strict_special, {}), "strict zero special slots reject special admission");
    auto legacy_zero = *strict_zero; legacy_zero.strict = false;
    require(spawn_allowed(legacy_zero, {}), "legacy zero heavy limits preserve unrestricted heavy admission");
    const auto strict_mixed = spawn_limits("ap_v070_ai_a_p00000006000000020000000600000203_l00000002000000020000000200000003_kn__ap_v070_s00100");
    require(strict_mixed && strict_mixed->strict && strict_mixed->mixed &&
            spawn_allowed(*strict_mixed, {0,0,0,1}) && !spawn_allowed(*strict_mixed, {0,0,0,2}),
            "strict mixed encounters enforce the prescribed heavy ceiling");
    require(!spawn_limits("ap_v070_ai_a_p00000003000000000000000300000008_l00000000000000000000000000000000_kn__ap_v070_s00100"),
            "unknown population flags are refused");
    require(!spawn_limits("ap_v070_ai_a_p00000003000000000000000300000004_l00000000000000000000000000000000_kn__ap_v070_s00100"),
            "counter allowances require strict limits");
    std::string individual_caps;
    for (unsigned value = 1; value <= 9; ++value) {
        char cap[9]; std::snprintf(cap, sizeof(cap), "%08x", value); individual_caps += cap;
    }
    constexpr uint64_t masks[]{0x8000000ull, 0x40000000ull, 0x10000000000ull, 0x2000000000ull,
        0x2ull, 0x1000000ull, 0x100ull, 0x20ull, 0x20000000ull};
    for (unsigned index = 0; index < std::size(masks); ++index) {
        char physical[17]; std::snprintf(physical, sizeof(physical), "%016llx", masks[index]);
        const auto name = "ap_v070_ai_a_t0000000000000001" + std::string(physical) + "_v" + individual_caps +
            "_a1234567812345678_p00000020000000080000002000000803_l00000008000000080000000800000020_kn__ap_v070_s00100";
        const auto limits = spawn_limits(name);
        require(limits && limits->physical == masks[index] && sentinel::expanded::spawn_tracking(name),
                "strict species caps preserve physical tracking");
        std::array<uint32_t, 9> active{}; active[index] = index;
        require(sentinel::expanded::species_allowed(*limits, active), "a free individual special slot is admitted");
        ++active[index];
        require(!sentinel::expanded::species_allowed(*limits, active), "every special species has its own ceiling");
        auto closed = *limits; closed.species_maximum[index] = 0; active = {};
        require(!sentinel::expanded::species_allowed(closed, active), "zero closes each individual special category");
        auto combined = *limits; combined.maximum[3] = 1; active[(index + 1) % active.size()] = 1;
        require(!sentinel::expanded::species_allowed(combined, active), "all nine species consume combined special slots");
        auto invalid_caps = name;
        invalid_caps.replace(invalid_caps.find("_v", 8) + 2, 8, "80000000");
        require(!spawn_limits(invalid_caps), "overflowing individual special caps are refused");
        invalid_caps = name; invalid_caps.erase(invalid_caps.find("_v", 8) + 2, 8);
        require(!spawn_limits(invalid_caps) && !sentinel::expanded::spawn_tracking(invalid_caps),
                "truncated individual special caps are refused");
    }
    const auto scoped_name = "ap_v070_ai_a_t00000000000000010000000000000002_a1234567812345678_p00000008000000020000000500000001_l00000001000000010000000100000002_kh__ap_v070_s00100";
    const auto scoped = spawn_limits(scoped_name);
    require(scoped && scoped->scope == 0x1234567812345678ull && sentinel::expanded::spawn_tracking(scoped_name),
        "arena limits preserve native credit tracking");
    require(!spawn_limits("ap_v070_ai_a_p00000000000000020000000500000000_l00000001000000010000000100000002_kh__ap_v070_s00100"),
        "an empty population limit cannot replace valid limits");
    for (const auto* name : {"ai/fodder/imp", "ap_v070_spawn_a_l00000001000000020000000300000004_kx__ap_v070_s00100",
            "ap_v070_spawn_a_l80000000000000020000000300000004_km__ap_v070_s00100",
            "ap_v070_spawn_a_l0000000z000000020000000300000004_km__ap_v070_s00100"})
        require(!spawn_limits(name), "unmarked or malformed limit marker");
    using sentinel::expanded::spawn_tracking;
    using sentinel::expanded::spawn_lift;
    for (const auto* physical : {"0000000000002000", "0000000000040000", "0000000000080000"}) {
        const std::string body = "ap_v070_ai_a_t0000000000000001" + std::string(physical) +
            "_l00000001000000010000000100000002_kn__ap_v070_s00100";
        require(spawn_lift(body) > 0, "ground replacement gives floating demons floor clearance");
    }
    require(spawn_lift("ap_v070_ai_a_t00000000000400000000000000040000_l00000001000000010000000100000002_kn__ap_v070_s00100") == 0,
        "native airborne points preserve their height");
    require(spawn_lift("ai/fodder/zombie") == 0, "unmarked actors preserve spawn height");
    const std::string slot = "ap_v070_group_a_t00000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100";
    const auto tracking = spawn_tracking(slot);
    require(tracking && tracking->source == 16 && tracking->physical == 32, "original slot and physical species are distinct");
    require(spawn_limits(slot)->kind == 3, "tracking preserves the independent simultaneous-limit marker");
    require(!spawn_tracking("ap_v070_group_a_t00000000000000300000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100"),
        "candidate mask is not a selected native source slot");
    for (const auto* name : {"ap_v070_group_a_l00000001000000010000000100000002_kn__ap_v070_s00100",
            "ap_v070_group_a_t00000000000000000000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100",
            "ap_v070_group_a_t00000000000000100000000000000030_l00000001000000010000000100000002_kn__ap_v070_s00100",
            "ap_v070_group_a_t00001000000000000000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100",
            "ap_v070_group_a_tz0000000000000100000000000000020_l00000001000000010000000100000002_kn__ap_v070_s00100",
            "ap_v070_group_a_l00000001000000010000000100000002_kn__ap_v070_s00_t0"})
        require(!spawn_tracking(name), "invalid tracking marker never changes native species selection");
    const auto recipe_name = "ap_v070_ai_r0123456789abcdef0123_00000004_q0000000200000006" + slot.substr(slot.find("_t"));
    const auto recipe = sentinel::expanded::spawn_recipe(recipe_name);
    require(recipe && recipe->body == 4 && recipe->logical_units == 2 && recipe->physical_units == 6,
        "body definitions carry exact logical credit without a native point counter");
    require(near(movement_speed(2, recipe_name), 2), "recipe metadata preserves native speed composition");
    auto invalid = recipe_name;
    invalid.replace(invalid.find("_q") + 10, 8, "00000000");
    require(!sentinel::expanded::spawn_recipe(invalid), "zero physical credit denominator is refused");
    invalid = recipe_name;
    invalid.replace(std::string("ap_v070_ai_r").size() + 21, 8, "00000006");
    require(!sentinel::expanded::spawn_recipe(invalid), "body ordinal must belong to the recipe");
    auto partial = recipe_name;
    partial.insert(partial.find("_t"), "_w00000001");
    const auto tail = sentinel::expanded::spawn_recipe(partial);
    require(tail && tail->credit_units == 1 && tail->logical_units == 2 && tail->physical_units == 6,
        "finite tail credit is explicit in the restored body's definition");
    partial.replace(partial.find("_w") + 2, 8, "00000000");
    require(!sentinel::expanded::spawn_recipe(partial), "empty tail credit is refused");
    const std::string metadata = "EAP1|" + std::string(64, 'a') + "|game/test\n" +
        "0123456789abcdef0123|m|original_group|arena|0000000000000010|00000002|00000003|00000005|00000001\n" +
        "0123456789abcdef0124|i|original_point|arena|0000000000000020|00000001|00000000|00000000|00000000\n";
    sentinel::expanded::RecipeLibrary library;
    require(sentinel::expanded::parse_recipe_library(metadata, library) && library.map == "game/test" &&
        library.recipes.size() == 2 && library.recipes[0].maximum_emissions == 5 && library.recipes[0].final_credit_units == 1 &&
        library.recipes[1].physical_count == 0, "executable library carries native sources, quotas, empty requests and checkpoint identity");
    const auto duplicate = metadata + metadata.substr(metadata.find('\n') + 1);
    require(!sentinel::expanded::parse_recipe_library(duplicate, library) && library.recipes.size() == 2,
        "ambiguous origins are rejected transactionally");
    auto corrupt = metadata;
    corrupt.replace(corrupt.find("00000005"), 8, "80000000");
    require(!sentinel::expanded::parse_recipe_library(corrupt, library), "native quota overflow is refused");
    const std::string extended = "EAP2|" + std::string(64, 'a') + "|game/test\n" +
        "0123456789abcdef0123|s|original_point|arena|0000010000000000|00000001|00000001|00000001|00000001|"
        "|686f7374207c2066696c746572||0000100000000000|01\n";
    require(sentinel::expanded::parse_recipe_library(extended, library) && library.recipes.size() == 1 &&
        library.recipes[0].first_label == "host | filter" && library.recipes[0].call_flag &&
        library.recipes[0].allowed_hosts == (uint64_t{1} << 44), "native special call arguments preserve masks and labels");
    corrupt = extended;
    corrupt.replace(corrupt.find("686f7374"), 8, "006f7374");
    require(!sentinel::expanded::parse_recipe_library(corrupt, library) && library.recipes[0].first_label == "host | filter",
        "invalid call strings cannot replace a valid library");
    auto with_health = extended;
    with_health.replace(0, 4, "EAP3");
    with_health.insert(with_health.size() - 1, "|42c8000042a00000");
    require(sentinel::expanded::parse_recipe_library(with_health, library) && library.recipes[0].hitpoints.size() == 1 &&
        library.recipes[0].hitpoints[0].maximum == 100 && library.recipes[0].hitpoints[0].starting == 80,
        "unemitted bodies retain their native health inputs");
    corrupt = with_health;
    corrupt.replace(corrupt.find("42c80000"), 8, "7f800000");
    require(!sentinel::expanded::parse_recipe_library(corrupt, library) && library.recipes[0].hitpoints[0].maximum == 100,
        "non-finite future health cannot replace a valid library");
    auto ratio = with_health;
    ratio.replace(0, 4, "EAP4"); ratio.insert(ratio.size() - 1, "|0000002100000064");
    require(sentinel::expanded::parse_recipe_library(ratio, library) && library.recipes[0].quantity_numerator == 33 &&
        library.recipes[0].quantity_denominator == 100, "summon quantity is independent of its finite body pool");
    corrupt = ratio; corrupt.replace(corrupt.size() - 9, 8, "00000000");
    require(!sentinel::expanded::parse_recipe_library(corrupt, library) && library.recipes[0].quantity_denominator == 100,
        "invalid summon ratios cannot replace a valid library");
    return 0;
}
