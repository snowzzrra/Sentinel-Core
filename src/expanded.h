#pragma once
#include <string_view>

namespace sentinel::expanded {
float movement_speed(float native_speed, std::string_view entity_definition) noexcept;
}
