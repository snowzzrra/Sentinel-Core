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
    return 0;
}
