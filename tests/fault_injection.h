#pragma once
// Test targets own deterministic failures; successful calls still reach Windows and MinHook.
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <string>
#include <string_view>
#include "native_target.h"

namespace sentinel::testing {
void drop_reply(uint16_t operation);
bool drop_pipe_reply(uint16_t operation);
enum class FileFault { none, short_write, truncate, flush, after_write, after_truncate, after_flush };
constexpr DWORD interrupted_exit = 73;
void file_fault(FileFault, const std::wstring& suffix, HANDLE entered = nullptr, HANDLE release = nullptr);
unsigned file_fault_hits();
BOOL WINAPI write_file(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI truncate_file(HANDLE);
BOOL WINAPI flush_file(HANDLE);
void begin_hooks(unsigned create_failure, unsigned enable_failure, bool initialize = true);
bool hooks_active();
bool real_hook_targets();
unsigned hook_creates();
unsigned hook_enables();
bool end_hooks(bool expect_cleanup);
MH_STATUS WINAPI create_hook(LPVOID, LPVOID, LPVOID*);
MH_STATUS WINAPI enable_hook(LPVOID);
MH_STATUS WINAPI queue_enable_hook(LPVOID);
MH_STATUS WINAPI disable_hook(LPVOID);
MH_STATUS WINAPI remove_hook(LPVOID);
int hook_failure(const std::wstring&);
void hook_cleanup(const std::wstring&);
}

namespace sentinel::native {
uint32_t fault_validate_target(engine::Memory&, const engine::Image&, const Target&, HANDLE, uint64_t, ValidationDetail* = nullptr);
uint32_t fault_validate_leaf_target(engine::Memory&, const engine::Image&, const Target&, size_t, HANDLE, uint64_t, ValidationDetail* = nullptr);
uint32_t fault_validate_recorded(save::Installation&, engine::Memory&, const engine::Image&, const Target&, HANDLE, uint64_t, uint32_t, uint32_t);
}

#ifdef SC_FAULT_REDIRECT
#define WriteFile sentinel::testing::write_file
#define SetEndOfFile sentinel::testing::truncate_file
#define FlushFileBuffers sentinel::testing::flush_file
#define MH_CreateHook sentinel::testing::create_hook
#define MH_EnableHook sentinel::testing::enable_hook
#define MH_QueueEnableHook sentinel::testing::queue_enable_hook
#define MH_DisableHook sentinel::testing::disable_hook
#define MH_RemoveHook sentinel::testing::remove_hook
#define validate_target fault_validate_target
#define validate_leaf_target fault_validate_leaf_target
#define validate_recorded fault_validate_recorded
#endif
