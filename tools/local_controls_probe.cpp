#include "local_controls.h"
#include <cwchar>

int local_controls_command(int argc, wchar_t** argv) {
    if (argc < 2 || std::wcscmp(argv[1], L"--local-controls")) return -1;
    if (argc < 4) {
        std::fprintf(stderr, "--local-controls <ap_queue directory> show | bind <refill|toggle> <key> | clear <refill|toggle> | reset <refill|toggle>\n");
        return 2;
    }
    using namespace sentinel::controls;
    const std::wstring directory = argv[2];
    auto bindings = read(directory);
    if (!std::wcscmp(argv[3], L"show") && argc == 4) {
        std::printf("refill_vk=%d toggle_vk=%d invalid_mask=%u local_conflict=%s native_conflict=UNKNOWN\n",
            bindings.keys[0], bindings.keys[1], bindings.invalid, bindings.conflict() ? "true" : "false");
        return bindings.invalid || bindings.conflict() ? 2 : 0;
    }
    if (argc < 5) return 2;
    const int action = !std::wcscmp(argv[4], L"refill") ? 0 : !std::wcscmp(argv[4], L"toggle") ? 1 : -1;
    if (action < 0) return 2;
    char token[16]{};
    if (!std::wcscmp(argv[3], L"bind") && argc == 6) {
        if (std::wcslen(argv[5]) >= sizeof(token)) return 2;
        for (size_t i = 0; argv[5][i]; ++i) {
            if (argv[5][i] > 127) return 2;
            token[i] = static_cast<char>(argv[5][i]);
        }
    } else if (!std::wcscmp(argv[3], L"clear") && argc == 5) strcpy_s(token, "UNBOUND");
    else if (!std::wcscmp(argv[3], L"reset") && argc == 5) strcpy_s(token, action ? "UNBOUND" : "F9");
    else return 2;
    const auto vk = key(token);
    if (vk < 0) return 2;
    bindings.keys[action] = vk;
    if (bindings.conflict()) { std::fprintf(stderr, "Key already assigned to the other local action.\n"); return 2; }
    const auto path = directory + L"\\" + files[action];
    const auto temporary = path + L".tmp";
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") || !file) return 1;
    const bool written = std::fprintf(file, "%s %s\n", headers[action], token) > 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); return 1;
    }
    std::printf("%s=%s; native_conflict=UNKNOWN\n", action ? "toggle" : "refill", token);
    return 0;
}
