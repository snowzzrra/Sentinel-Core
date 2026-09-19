#pragma once
#include <windows.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>

namespace sentinel::controls {
inline constexpr const wchar_t* files[] = {L"ammo_refill_hotkey.state", L"special_toggle_hotkey.state"};
inline constexpr const char* headers[] = {"AP_AMMO_REFILL_HOTKEY_V1", "AP_SPECIAL_TOGGLE_HOTKEY_V1"};
inline constexpr int defaults[] = {VK_F9, 0};

inline int key(const char* text) {
    if (!text || std::strlen(text) > 15) return -1;
    char token[16]{};
    for (size_t i = 0; text[i]; ++i) token[i] = text[i] >= 'a' && text[i] <= 'z'
        ? static_cast<char>(text[i] - 'a' + 'A') : text[i];
    if (!std::strcmp(token, "UNBOUND")) return 0;
    if (token[0] == 'F' && token[1]) {
        int n = 0;
        for (size_t i = 1; token[i]; ++i) {
            if (token[i] < '0' || token[i] > '9' || n > 12) return -1;
            n = n * 10 + token[i] - '0';
        }
        return n >= 1 && n <= 12 ? VK_F1 + n - 1 : -1;
    }
    if (token[0] && !token[1] && ((token[0] >= 'A' && token[0] <= 'Z') ||
                                (token[0] >= '0' && token[0] <= '9'))) return token[0];
    const struct { const char* name; int vk; } names[] = {
        {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
        {"HOME", VK_HOME}, {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PGUP", VK_PRIOR},
        {"PAGEDOWN", VK_NEXT}, {"PGDN", VK_NEXT}};
    for (const auto& n : names) if (!std::strcmp(token, n.name)) return n.vk;
    return -1;
}

struct Bindings {
    int keys[2] = {defaults[0], defaults[1]};
    unsigned invalid = 0;
    bool conflict() const { return keys[0] && keys[0] == keys[1]; }
};

inline Bindings read(const std::wstring& directory) {
    Bindings result;
    for (unsigned i = 0; i < 2; ++i) {
        FILE* file = nullptr;
        const auto path = directory + L"\\" + files[i];
        const auto error = _wfopen_s(&file, path.c_str(), L"rb");
        if (error == ENOENT) continue;
        char data[128]{};
        const auto count = file ? std::fread(data, 1, sizeof(data) - 1, file) : 0;
        const bool complete = file && !std::ferror(file) && std::feof(file);
        if (file) std::fclose(file);
        char first[32]{}, second[32]{}, extra[2]{};
        const auto tokens = sscanf_s(data, "%31s %31s %1s", first, 32u, second, 32u, extra, 2u);
        const char* value = tokens == 1 ? first : tokens == 2 && !std::strcmp(first, headers[i]) ? second : "";
        const auto vk = key(value);
        if (!complete || std::strlen(data) != count || vk < 0) {
            result.invalid |= 1u << i;
            result.keys[i] = 0;
        } else result.keys[i] = vk;
    }
    return result;
}
} // namespace sentinel::controls
