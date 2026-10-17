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
    return 0;
}
