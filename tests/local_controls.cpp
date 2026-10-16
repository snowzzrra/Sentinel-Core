#include "local_controls.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>

namespace fs = std::filesystem;
namespace controls = sentinel::controls;
std::string bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void raw(const fs::path& path, const char* value) {
    std::ofstream file(path, std::ios::binary);
    file << value;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const fs::path base(argv[1]);
    fs::create_directories(base);
    const auto directory = base / (L"native-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                   std::to_wstring(GetTickCount64()));
    if (!CreateDirectoryW(directory.c_str(), nullptr)) return 2;
    const auto first = directory / controls::files[0], second = directory / controls::files[1];
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "%s\n", what); ++failures; }
    };
    const auto saved = controls::WriteResult::saved;
    const auto initial = controls::read(directory.wstring());
    check(!initial.invalid && !initial.keys[0] && !initial.keys[1], "missing bindings must stay unbound");
    check(controls::write(directory.wstring(), 0, VK_F7) == saved, "write first binding");
    check(controls::write(directory.wstring(), 1, VK_F8) == saved, "write second binding");
    const auto previous_first = bytes(first), previous_second = bytes(second);
    check(controls::write(directory.wstring(), 1, VK_F7) == controls::WriteResult::conflict,
          "refuse duplicated AP key");
    check(bytes(first) == previous_first && bytes(second) == previous_second, "conflict must preserve both files");
    check(controls::write(directory.wstring(), 0, VK_SHIFT) == controls::WriteResult::invalid,
          "refuse unsupported key");
    check(bytes(first) == previous_first, "invalid key must preserve prior binding");
    check(controls::write(directory.wstring(), 0, 0) == saved, "clear first binding");
    auto state = controls::read(directory.wstring());
    check(!state.keys[0] && state.keys[1] == VK_F8, "clear must preserve other action");
    check(controls::reset(directory.wstring()) == saved, "reset shared defaults");
    state = controls::read(directory.wstring());
    check(state.keys[0] == VK_F9 && state.keys[1] == VK_F10 && !state.invalid, "reset must persist both defaults");
    for (int vk = 0; vk < 256; ++vk) {
        const auto token = controls::token(vk);
        if (!token.empty()) check(controls::key(token.c_str()) == vk, "canonical token must round-trip");
    }
    check(controls::from_native_key(67) == VK_F9 && controls::from_native_key(88) == VK_F12 &&
          controls::from_native_key(199) == VK_HOME && controls::from_native_key(211) == VK_DELETE,
          "translate native function and navigation keys");
    for (const auto native : {0,1,29,42,54,55,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,156,256,286})
        check(controls::from_native_key(native) == -1, "refuse modifiers, numpad and non-keyboard inputs");
    raw(first, "F4\n");
    DeleteFileW(second.c_str());
    check(CreateDirectoryW(second.c_str(), nullptr) != 0, "create isolated replacement blocker");
    check(controls::reset(directory.wstring()) == controls::WriteResult::io_error, "second replacement must fail");
    check(bytes(first) == "F4\n" && fs::is_directory(second), "failed reset must restore exact first bytes");
    DeleteFileW(first.c_str());
    check(controls::reset(directory.wstring()) == controls::WriteResult::io_error, "reset missing first with blocked second");
    check(!fs::exists(first) && fs::is_directory(second), "failed reset must restore missing first");
    RemoveDirectoryW(second.c_str());
    raw(first, "AP_AMMO_REFILL_HOTKEY_V1\nnot-a-key\n");
    check((controls::read(directory.wstring()).invalid & 1) != 0, "malformed shared binding must be disabled");
    DeleteFileW(first.c_str());
    check(fs::is_empty(directory), "atomic failures must clean temporary files");
    RemoveDirectoryW(directory.c_str());
    return failures ? 1 : 0;
}

