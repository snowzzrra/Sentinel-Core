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
    else if (!std::wcscmp(argv[3], L"reset") && argc == 5) strcpy_s(token, sentinel::controls::token(reset_defaults[action]).c_str());
    else return 2;
    const auto vk = key(token);
    if (vk < 0) return 2;
    const auto result = write(directory, static_cast<unsigned>(action), vk);
    if (result == WriteResult::conflict) { std::fprintf(stderr, "Key already assigned to the other local action.\n"); return 2; }
    if (result != WriteResult::saved) return 1;
    std::printf("%s=%s; native_conflict=UNKNOWN\n", action ? "toggle" : "refill", token);
    return 0;
}
