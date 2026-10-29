#include <windows.h>
#include <tlhelp32.h>

extern int sc_test_hooks_active(void);

// Synthetic hook pages are never executed by another thread.
static HANDLE WINAPI hook_snapshot(DWORD flags, DWORD pid) {
    if (sc_test_hooks_active()) return CreateEventW(NULL, FALSE, FALSE, NULL);
    return CreateToolhelp32Snapshot(flags, pid);
}
static BOOL WINAPI hook_first(HANDLE snapshot, LPTHREADENTRY32 entry) {
    if (!sc_test_hooks_active()) return Thread32First(snapshot, entry);
    entry->th32OwnerProcessID = GetCurrentProcessId();
    entry->th32ThreadID = GetCurrentThreadId();
    return TRUE;
}
static BOOL WINAPI hook_next(HANDLE snapshot, LPTHREADENTRY32 entry) {
    if (!sc_test_hooks_active()) return Thread32Next(snapshot, entry);
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

#define CreateToolhelp32Snapshot hook_snapshot
#define Thread32First hook_first
#define Thread32Next hook_next
#include "../third_party/minhook/src/hook.c"
