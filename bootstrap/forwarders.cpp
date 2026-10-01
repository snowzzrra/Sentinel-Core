#include <windows.h>
#include <array>
#include <cstdint>

namespace {
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
std::array<FARPROC, 5> exports{};
wchar_t system_path[MAX_PATH]{};
DWORD error = ERROR_SUCCESS;

BOOL CALLBACK load_system(PINIT_ONCE, void*, void**) {
    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length >= MAX_PATH - 14) { error = ERROR_BAD_PATHNAME; return TRUE; }
    wcscat_s(path, L"\\msimg32.dll");
    HMODULE system = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system) { error = GetLastError(); return TRUE; }
    wchar_t actual[MAX_PATH]{};
    const DWORD actual_length = GetModuleFileNameW(system, actual, MAX_PATH);
    if (!actual_length || actual_length >= MAX_PATH || _wcsicmp(path, actual)) {
        error = ERROR_INVALID_DLL; FreeLibrary(system); return TRUE;
    }
    constexpr const char* names[] = {"vSetDdrawflag", "AlphaBlend", "DllInitialize", "GradientFill", "TransparentBlt"};
    for (size_t i = 0; i < exports.size(); ++i) {
        exports[i] = GetProcAddress(system, names[i]);
        if (!exports[i] || exports[i] != GetProcAddress(system, MAKEINTRESOURCEA(i + 1))) {
            exports = {}; error = ERROR_PROC_NOT_FOUND; FreeLibrary(system); return TRUE;
        }
    }
    wcscpy_s(system_path, actual);
    // System forwarding and native hooks retain their modules until process exit.
    return TRUE;
}
}

extern "C" FARPROC sc_resolve_system_export(unsigned index) {
    InitOnceExecuteOnce(&once, load_system, nullptr, nullptr);
    if (index >= exports.size() || !exports[index]) {
        SetLastError(error ? error : ERROR_PROC_NOT_FOUND);
        RaiseException(0xc0000135u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return nullptr;
    }
    return exports[index];
}

extern "C" uint32_t sc_bootstrap_system_path(uint32_t capacity, wchar_t* path) {
    if (!path || capacity < MAX_PATH) return ERROR_INSUFFICIENT_BUFFER;
    InitOnceExecuteOnce(&once, load_system, nullptr, nullptr);
    if (error) return error;
    wcscpy_s(path, capacity, system_path);
    return ERROR_SUCCESS;
}
