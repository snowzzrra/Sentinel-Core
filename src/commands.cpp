#include "commands.h"
#include <array>
#include <cstring>
#include <charconv>
#include <cmath>

namespace sentinel::commands {
bool process_scoped(const sc_command_request& request) {
    return request.kind == SC_COMMAND_CONDUMP &&
        std::string_view(request.text) == "condump AP_SUPPORT_FILE.txt";
}
bool compatible_product(std::string_view version) {
    const auto rc = version.find("-rc-");
    const auto base = version.substr(0, rc);
    if (base != "1.0.0" && base != "1.0.1" && base != "1.0.2") return false;
    if (rc == std::string_view::npos) return true;
    version.remove_prefix(rc + 4);
    return !version.empty() && version.front() >= '1' && version.front() <= '9' &&
        version.find_first_not_of("0123456789") == std::string_view::npos;
}
namespace {
bool identifier(std::string_view value, bool path = false) {
    if (value.empty() || value.size() > 255) return false;
    for (const unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || (path && c == '/'))) return false;
    return true;
}
uint32_t kind(std::string_view text) {
    if (text.empty() || text.size() >= SC_COMMAND_TEXT_CAPACITY) return 0;
    for (const unsigned char c : text) if (c < 32 || c > 126 || c == ';' || c == '"' || c == '\\') return 0;
    if (text.substr(0, 5) == "echo ") return text.size() > 5 ? SC_COMMAND_ECHO : 0;
    std::array<std::string_view, 5> words{};
    size_t count = 0, start = 0;
    while (start < text.size()) {
        if (count == words.size()) return 0;
        const auto end = text.find(' ', start);
        words[count++] = text.substr(start, end == text.npos ? text.npos : end - start);
        if (words[count - 1].empty()) return 0;
        if (end == text.npos) break;
        start = end + 1;
        if (start == text.size()) return 0;
    }
    if (text == "give ammo") return SC_COMMAND_AMMO_REFILL;
    if (text == "judgementMeter_Set 3") return SC_COMMAND_CRUCIBLE_REFILL;
    if (text == "listInventory player1") return SC_COMMAND_LIST_INVENTORY;
    if (count == 2 && (words[0] == "g_damageScaleAllToAI" || words[0] == "g_damageScaleAllToSlayer" || words[0] == "g_infiniteAmmo")) {
        double value = 0;
        const auto number = words[1];
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !std::isfinite(value)) return 0;
        if (words[0] == "g_infiniteAmmo") return number == "0" || number == "1" ? SC_COMMAND_TRANSIENT_POLICY : 0;
        return value >= 0.01 && value <= 100.0 ? SC_COMMAND_TRANSIENT_POLICY : 0;
    }
    if (count == 2 && words[0] == "condump") {
        const auto file = words[1];
        if (file.size() < 8 || file.size() > 128 || file.substr(0, 3) != "AP_" ||
            file.substr(file.size() - 4) != ".txt") return 0;
        return identifier(file.substr(0, file.size() - 4)) ? SC_COMMAND_CONDUMP : 0;
    }
    if (count >= 3 && words[0] == "ai_ScriptCmdEnt" && identifier(words[1])) {
        if (words[2] == "activate" && (count == 3 || (count == 4 && words[3] == "player1")))
            return SC_COMMAND_ACTIVATE;
        if (count == 4 && words[1] == "player1" && words[2] == "givePlayerPerk" && identifier(words[3], true))
            return SC_COMMAND_PLAYER_PERK;
    }
    return 0;
}
}
bool parse(std::string_view text, sc_command_request& request) {
    const auto operation = kind(text);
    if (!operation) return false;
    request.kind = operation;
    std::memset(request.text, 0, sizeof(request.text));
    std::memcpy(request.text, text.data(), text.size());
    return true;
}
bool parse_manual(int argc, const char* const* argv, ManualRequest& out) {
    out = {};
    if (argc < 1 || argc > 5 || !argv) return false;
    for (int i = 0; i < argc; ++i) if (!argv[i]) return false;
    const std::string_view command = argv[0];
    if (command == "noclip") {
        if (argc == 1) out.kind = 1;
        else if (argc == 2 && (std::string_view(argv[1]) == "off" || std::string_view(argv[1]) == "0")) out.kind = 2;
        else if (argc == 2 && (std::string_view(argv[1]) == "on" || std::string_view(argv[1]) == "1")) out.kind = 3;
        return out.kind != 0;
    }
    if (command != "chrispy" || (argc != 2 && argc != 5) || !identifier(argv[1], true)) return false;
    out.kind = 4; std::memcpy(out.entity_def, argv[1], std::strlen(argv[1]) + 1);
    out.explicit_position = argc == 5;
    if (out.explicit_position) for (size_t i = 0; i < 3; ++i) {
        const std::string_view text = argv[i + 2];
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out.position[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(out.position[i])) return false;
    }
    return true;
}
bool valid(const sc_command_request& request) {
    if (request.namespace_id[64]) return false;
    for (size_t i = 0; i < 64; ++i) {
        const auto c = request.namespace_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    const auto end = static_cast<const char*>(std::memchr(request.text, 0, sizeof(request.text)));
    if (!end || kind({request.text, static_cast<size_t>(end - request.text)}) != request.kind) return false;
    for (const char* p = end; p < request.text + sizeof(request.text); ++p) if (*p) return false;
    return true;
}
bool same(const sc_command_request& a, const sc_command_request& b) {
    return a.kind == b.kind && std::memcmp(a.namespace_id, b.namespace_id, sizeof(a.namespace_id)) == 0 &&
        std::memcmp(a.text, b.text, sizeof(a.text)) == 0;
}
sc_command_result initial(const sc_command_request& request) {
    sc_command_result out{}; out.size = sizeof(out); out.abi_version = SC_COMMAND_ABI_VERSION;
    out.kind = request.kind; std::memcpy(out.namespace_id, request.namespace_id, sizeof(out.namespace_id));
    return out;
}
}
