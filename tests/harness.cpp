#include "sentinel_bootstrap.h"
#include "sentinel_engine.h"
#include "sentinel_save.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" int sentinel_c_status_size(void);
extern "C" int sentinel_c_engine_size(void);
extern "C" int sentinel_c_save_write_size(void);
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s (win32=%lu)\n", __LINE__, #condition, GetLastError()); \
    std::exit(1); } } while (0)

template<class T> T symbol(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    CHECK(address != nullptr);
    static_assert(sizeof(T) == sizeof(address));
    T result;
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

void core_test(HMODULE module) {
    CHECK(sentinel_c_save_write_size() == sizeof(sc_save_write_snapshot));
    const auto inspect = symbol<decltype(&sc_inspect)>(module, "sc_inspect");
    const auto initialize = symbol<decltype(&sc_initialize)>(module, "sc_initialize");
    const auto shutdown = symbol<decltype(&sc_shutdown)>(module, "sc_shutdown");
    const auto engine = symbol<decltype(&sc_engine_inspect)>(module, "sc_engine_inspect");
    sc_engine_snapshot observation{};
    CHECK(sentinel_c_engine_size() == sizeof(observation));
    CHECK(engine(99, sizeof(observation), &observation) == SC_ABI_MISMATCH);
    CHECK(engine(1, sizeof(observation) - 1, &observation) == SC_INVALID_ARGUMENT);
    CHECK(engine(1, sizeof(observation), nullptr) == SC_INVALID_ARGUMENT);
    CHECK(engine(1, sizeof(observation), &observation) == SC_OK && observation.fields[0].reason == SC_REASON_NOT_SAMPLED);
    sc_status status{};
    CHECK(sentinel_c_status_size() == sizeof(status));
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), &status) == SC_OK);
    CHECK(status.state == SC_COLD && status.initialization_count == 0);
    CHECK(status.capabilities == (SC_CAP_INSPECTION | SC_CAP_LIFECYCLE));
    CHECK(std::strlen(status.build_id) == 64 && std::strlen(status.version) != 0);
    CHECK(inspect(99, sizeof(status), &status) == SC_ABI_MISMATCH);
    CHECK(inspect(SC_ABI_VERSION, sizeof(status) - 1, &status) == SC_INVALID_ARGUMENT);
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), nullptr) == SC_INVALID_ARGUMENT);
    CHECK(initialize(99, 0) == SC_ABI_MISMATCH);
    CHECK(initialize(SC_ABI_VERSION, UINT64_C(1) << 63) == SC_CAPABILITY_UNAVAILABLE);
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), &status) == SC_OK);
    CHECK(status.state == SC_COLD && status.last_result == SC_CAPABILITY_UNAVAILABLE);
    CHECK(shutdown() == SC_OK && shutdown() == SC_OK);
    std::vector<std::thread> callers;
    for (int i = 0; i < 8; ++i) callers.emplace_back([&] {
        CHECK(initialize(SC_ABI_VERSION, SC_CAP_LIFECYCLE) == SC_OK);
        sc_status snapshot{};
        CHECK(inspect(SC_ABI_VERSION, sizeof(snapshot), &snapshot) == SC_OK);
        CHECK(snapshot.state == SC_READY);
    });
    for (auto& caller : callers) caller.join();
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), &status) == SC_OK);
    CHECK(status.initialization_count == 1);
    CHECK(initialize(99, 0) == SC_ABI_MISMATCH);
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), &status) == SC_OK && status.state == SC_READY);
    for (int i = 0; i < 24; ++i) {
        CHECK(shutdown() == SC_OK);
        CHECK(initialize(SC_ABI_VERSION, 0) == SC_OK);
    }
    CHECK(shutdown() == SC_OK && shutdown() == SC_OK);
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), &status) == SC_OK);
    CHECK(status.state == SC_STOPPED && status.initialization_count == 25);
    CHECK(engine(1, sizeof(observation), &observation) == SC_OK);
    CHECK(observation.sampled_at_ms == 0 && observation.fields[0].reason == SC_REASON_STOPPED);
    CHECK(status.diagnostic_count == SC_DIAGNOSTIC_LIMIT);
    std::printf("PASS core ABI, failures, concurrent/idempotent init, restart, diagnostic cap; version=%s build=%s\n",
                status.version, status.build_id);
}

void proxy_test(HMODULE module, bool normal_exit) {
    const auto inspect = symbol<decltype(&sc_bootstrap_inspect)>(module, "sc_bootstrap_inspect");
    const auto shutdown = symbol<decltype(&sc_bootstrap_shutdown)>(module, "sc_bootstrap_shutdown");
    sc_boot_status status{};
    CHECK(inspect(99, sizeof(status), &status) == SC_ABI_MISMATCH);
    CHECK(inspect(SC_ABI_VERSION, 0, &status) == SC_INVALID_ARGUMENT);
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), nullptr) == SC_INVALID_ARGUMENT);
    const ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        CHECK(inspect(SC_ABI_VERSION, sizeof(status), &status) == SC_OK);
        if (status.state != SC_BOOT_STARTING) break;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    CHECK(status.state == SC_BOOT_READY && status.win32_error == ERROR_SUCCESS);
    CHECK(status.core.state == SC_READY && status.core.initialization_count == 1);
    wchar_t system_path[MAX_PATH]{};
    CHECK(GetSystemDirectoryW(system_path, MAX_PATH) != 0);
    CHECK(wcscat_s(system_path, L"\\msimg32.dll") == 0);
    HMODULE real = LoadLibraryExW(system_path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(real != nullptr && real != module);
    const char* names[] = {"vSetDdrawflag", "AlphaBlend", "DllInitialize", "GradientFill", "TransparentBlt"};
    for (WORD i = 0; i < 5; ++i) {
        const auto expected = GetProcAddress(real, names[i]);
        CHECK(expected != nullptr);
        CHECK(GetProcAddress(module, names[i]) == expected);
        CHECK(GetProcAddress(module, MAKEINTRESOURCEA(i + 1)) == expected);
    }
    // Pointer equality proves every name/ordinal reaches the actual implementation,
    // including private exports whose calling convention/signature we do not guess.
    CHECK(FreeLibrary(real));
    std::printf("PASS proxy auto-init, five exact System32 name/ordinal forwarders; core=%s build=%s\n",
                status.core.version, status.core.build_id);
    if (normal_exit) {
        std::puts("PASS host normal exit path selected (no explicit detach work)");
        return;
    }
    CHECK(shutdown() == SC_OK && shutdown() == SC_OK);
    CHECK(inspect(SC_ABI_VERSION, sizeof(status), &status) == SC_OK);
    CHECK(status.state == SC_BOOT_STOPPED && status.core.state == SC_STOPPED);
    CHECK(FreeLibrary(module));
    std::puts("PASS proxy join, idempotent shutdown, unload");
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::fputs("usage: sentinel_harness --core|--proxy|--exit|--missing ABSOLUTE_DLL_PATH\n", stderr);
        return 2;
    }
    const bool core = wcscmp(argv[1], L"--core") == 0;
    const bool proxy = wcscmp(argv[1], L"--proxy") == 0;
    const bool normal_exit = wcscmp(argv[1], L"--exit") == 0;
    const bool missing = wcscmp(argv[1], L"--missing") == 0;
    if (!core && !proxy && !normal_exit && !missing) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    HMODULE module = LoadLibraryExW(argv[2], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (missing) {
        CHECK(!module && GetLastError() == ERROR_MOD_NOT_FOUND);
        std::puts("PASS missing DLL/dependency fails with ERROR_MOD_NOT_FOUND");
        return 0;
    }
    CHECK(module != nullptr);
    if (core) {
        core_test(module);
        CHECK(FreeLibrary(module));
    } else {
        proxy_test(module, normal_exit);
    }
    return 0;
}
