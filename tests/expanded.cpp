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
    const auto banned = spawn_limits("ap_v070_spawn_a_l00000000000000020000000300000004_km__ap_v070_s00100");
    require(banned && !spawn_allowed(*banned, {0, 0, 0}), "zero species limit");
    const auto ordinary = spawn_limits("ap_v070_ai_a_l00000001000000020000000300000004_kn__ap_v070_s00100");
    require(ordinary && spawn_allowed(*ordinary, {1, 2, 3}), "ordinary actors use native global admission");
    for (const auto* name : {"ai/fodder/imp", "ap_v070_spawn_a_l00000001000000020000000300000004_kx__ap_v070_s00100",
            "ap_v070_spawn_a_l80000000000000020000000300000004_km__ap_v070_s00100",
            "ap_v070_spawn_a_l0000000z000000020000000300000004_km__ap_v070_s00100"})
        require(!spawn_limits(name), "unmarked or malformed limit marker");
    using sentinel::expanded::spawn_tracking;
    using sentinel::expanded::spawn_lift;
    require(spawn_lift("ap_v070_ai_a_t00000000000000010000000000040000_l00000001000000010000000100000002_kn__ap_v070_s00100") > 0,
        "ground replacement gives a cacodemon floor clearance");
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
