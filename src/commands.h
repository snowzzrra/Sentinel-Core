#pragma once
#include "sentinel_commands.h"
#include <string_view>

namespace sentinel::commands {
struct ManualRequest {
    uint32_t kind = 0; // 1 noclip toggle, 2 off, 3 on, 4 spawn.
    char entity_def[256]{};
    bool explicit_position = false;
    float position[3]{};
};
bool parse_manual(int argc, const char* const* argv, ManualRequest&);
bool compatible_product(std::string_view version);
bool process_scoped(const sc_command_request& request);
bool parse(std::string_view text, sc_command_request& request);
bool valid(const sc_command_request& request);
bool same(const sc_command_request& a, const sc_command_request& b);
sc_command_result initial(const sc_command_request& request);
}
