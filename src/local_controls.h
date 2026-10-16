#pragma once
#include <windows.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>

namespace sentinel::controls {
inline constexpr const wchar_t* files[] = {L"ammo_refill_hotkey.state", L"special_toggle_hotkey.state"};
inline constexpr const char* headers[] = {"AP_AMMO_REFILL_HOTKEY_V1", "AP_SPECIAL_TOGGLE_HOTKEY_V1"};
inline constexpr int defaults[] = {0, 0};
inline constexpr int reset_defaults[] = {VK_F9, VK_F10};
struct KeyName { const char* name; int vk; };
inline constexpr KeyName key_names[] = {
    {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
    {"HOME", VK_HOME}, {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PGUP", VK_PRIOR},
    {"PAGEDOWN", VK_NEXT}, {"PGDN", VK_NEXT}, {"BACKSPACE", VK_BACK},
    {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}};

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
    for (const auto& n : key_names) if (!std::strcmp(token, n.name)) return n.vk;
    return -1;
}

inline std::string token(int vk) {
    if (!vk) return "UNBOUND";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
    for (const auto& n : key_names) if (n.vk == vk) return n.name;
    return {};
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

inline int from_native_key(int native) {
    int vk = 0;
    switch (native) {
    case 199: vk = VK_HOME; break;
    case 200: vk = VK_UP; break;
    case 201: vk = VK_PRIOR; break;
    case 203: vk = VK_LEFT; break;
    case 205: vk = VK_RIGHT; break;
    case 207: vk = VK_END; break;
    case 208: vk = VK_DOWN; break;
    case 209: vk = VK_NEXT; break;
    case 210: vk = VK_INSERT; break;
    case 211: vk = VK_DELETE; break;
    default:
        if (!((native >= 2 && native <= 11) || native == 14 || native == 15 ||
              (native >= 16 && native <= 25) || (native >= 30 && native <= 38) ||
              (native >= 44 && native <= 50) || native == 57 ||
              (native >= 59 && native <= 68) || native == 87 || native == 88)) return -1;
        vk = static_cast<int>(MapVirtualKeyA(static_cast<UINT>(native), MAPVK_VSC_TO_VK_EX));
        break;
    }
    return vk && !token(vk).empty() ? vk : -1;
}
enum class WriteResult { saved, invalid, conflict, io_error, rollback_error };

namespace detail {
inline bool publish(const std::wstring& path, const char* data, size_t count) {
    const auto temporary = path + L".native." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetCurrentThreadId()) + L"." + std::to_wstring(GetTickCount64());
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool complete = WriteFile(file, data, static_cast<DWORD>(count), &written, nullptr) &&
        written == count && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
    if (complete && closed && MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temporary.c_str());
    return false;
}

inline bool publish_key(const std::wstring& directory, unsigned action, int vk) {
    const auto value = std::string(headers[action]) + " " + token(vk) + "\n";
    return publish(directory + L"\\" + files[action], value.data(), value.size());
}

inline bool directory_ready(const std::wstring& directory) {
    return !directory.empty() && (CreateDirectoryW(directory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
}
} // namespace detail

inline WriteResult write(const std::wstring& directory, unsigned action, int vk) {
    if (action >= 2 || token(vk).empty()) return WriteResult::invalid;
    auto bindings = read(directory);
    bindings.keys[action] = vk;
    if (bindings.conflict()) return WriteResult::conflict;
    return detail::directory_ready(directory) && detail::publish_key(directory, action, vk)
        ? WriteResult::saved : WriteResult::io_error;
}

inline WriteResult reset(const std::wstring& directory) {
    if (!detail::directory_ready(directory)) return WriteResult::io_error;
    const auto first = directory + L"\\" + files[0];
    char previous[128]{};
    size_t previous_count = 0;
    FILE* file = nullptr;
    const auto error = _wfopen_s(&file, first.c_str(), L"rb");
    const bool existed = error != ENOENT;
    if (existed) {
        if (error || !file) return WriteResult::io_error;
        previous_count = std::fread(previous, 1, sizeof(previous), file);
        const bool complete = !std::ferror(file) && std::feof(file);
        std::fclose(file);
        if (!complete) return WriteResult::io_error;
    }
    if (!detail::publish_key(directory, 0, reset_defaults[0])) return WriteResult::io_error;
    if (detail::publish_key(directory, 1, reset_defaults[1])) return WriteResult::saved;
    const bool restored = existed ? detail::publish(first, previous, previous_count) :
        DeleteFileW(first.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
    return restored ? WriteResult::io_error : WriteResult::rollback_error;
}
} // namespace sentinel::controls
