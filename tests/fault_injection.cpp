#include "fault_injection.h"
#undef WriteFile
#undef SetEndOfFile
#undef FlushFileBuffers
#undef MH_CreateHook
#undef MH_EnableHook
#undef MH_QueueEnableHook
#undef MH_DisableHook
#undef MH_RemoveHook
#undef validate_target
#undef validate_leaf_target
#undef validate_recorded
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <algorithm>
#include <atomic>

extern "C" int sc_test_hooks_active() { return sentinel::testing::hooks_active(); }

namespace sentinel::testing {
std::atomic<uint16_t> dropped_operation{0};
void drop_reply(uint16_t operation) { dropped_operation.store(operation); }
bool drop_pipe_reply(uint16_t operation) {
    return dropped_operation.compare_exchange_strong(operation, 0);
}
namespace {
FileFault file_mode = FileFault::none;
std::wstring file_suffix;
HANDLE file_entered = nullptr, file_release = nullptr;
unsigned file_hits = 0, creates = 0, enables = 0, fail_create = 0, fail_enable = 0;
bool active = false;
std::wstring hook_installer;
unsigned failed_group = 0;
uint8_t* pages = nullptr;
struct Hook { void* site; unsigned group; bool owned = false, foreign = false; };
std::map<void*, Hook> hooks;
unsigned hook_group(uintptr_t key) {
    const auto rva = static_cast<uint32_t>(key - 0x180000000);
    static const std::map<std::wstring, std::vector<std::vector<uint32_t>>> groups{
        {L"arsenal", {{0x1670850, 0x166e750, 0x166f5e0}, {0xf1f420},
                      {0xf937a0, 0xf94ac0, 0xf94590, 0xf3b870, 0xf3dc70}, {0x164e9b0, 0x1651040}}},
        {L"special", {{0x145d640, 0x146b850, 0x1438f00, 0x143c7f0, 0x14644e0, 0x1456310, 0x146be40},
                      {0x1859e20, 0xf0b3b0}}}};
    const auto found = groups.find(hook_installer);
    if (found == groups.end()) return 0;
    for (unsigned i = 0; i < found->second.size(); ++i) {
        const auto& group = found->second[i];
        if (std::find(group.begin(), group.end(), rva) != group.end()) return i + 1;
    }
    return rva;
}
void require(bool ok) {
    if (ok) return;
    std::fprintf(stderr, "FAIL fault injection setup (win32=%lu)\n", GetLastError());
    std::abort();
}
bool selected(HANDLE file) {
    if (file_mode == FileFault::none) return false;
    std::array<wchar_t, 1024> path{};
    const auto length = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()), 0);
    require(length && length < path.size());
    const std::wstring_view name(path.data(), length);
    return name.size() >= file_suffix.size() && name.substr(name.size() - file_suffix.size()) == file_suffix;
}
void interrupt() {
    ++file_hits;
    require(file_entered && file_release && SetEvent(file_entered));
    require(WaitForSingleObject(file_release, 10000) == WAIT_OBJECT_0);
    std::exit(1);
}
void* mapped(void* key) {
    const auto found = hooks.find(key);
    return found == hooks.end() ? key : found->second.site;
}
void foreign_detour() {}
}
void file_fault(FileFault mode, const std::wstring& suffix, HANDLE entered, HANDLE release) {
    file_mode = mode; file_suffix = suffix; file_hits = 0;
    file_entered = entered; file_release = release;
}
unsigned file_fault_hits() { return file_hits; }
BOOL WINAPI write_file(HANDLE file, LPCVOID bytes, DWORD count, LPDWORD written, LPOVERLAPPED overlap) {
    const bool match = selected(file);
    if (match && file_mode == FileFault::short_write) {
        ++file_hits;
        return WriteFile(file, bytes, count ? count - 1 : 0, written, overlap);
    }
    const auto ok = WriteFile(file, bytes, count, written, overlap);
    if (ok && match && file_mode == FileFault::after_write) interrupt();
    return ok;
}
BOOL WINAPI truncate_file(HANDLE file) {
    const bool match = selected(file);
    if (match && file_mode == FileFault::truncate) {
        ++file_hits; SetLastError(ERROR_WRITE_FAULT); return FALSE;
    }
    const auto ok = SetEndOfFile(file);
    if (ok && match && file_mode == FileFault::after_truncate) interrupt();
    return ok;
}
BOOL WINAPI flush_file(HANDLE file) {
    const bool match = selected(file);
    if (match && file_mode == FileFault::flush) {
        ++file_hits; SetLastError(ERROR_WRITE_FAULT); return FALSE;
    }
    const auto ok = FlushFileBuffers(file);
    if (ok && match && file_mode == FileFault::after_flush) interrupt();
    return ok;
}
void begin_hooks(unsigned create_failure, unsigned enable_failure, bool initialize) {
    require(!active);
    if (initialize) require(MH_Initialize() == MH_OK);
    pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    require(pages != nullptr);
    std::memset(pages, 0x90, 0x10000);
    creates = enables = 0; fail_create = create_failure; fail_enable = enable_failure; active = true;
}
bool hooks_active() { return active; }
bool real_hook_targets() { return !active; }
unsigned hook_creates() { return creates; }
unsigned hook_enables() { return enables; }
MH_STATUS WINAPI create_hook(void* key, void* detour, void** original) {
    if (!active) return MH_CreateHook(key, detour, original);
    ++creates;
    if (!hooks.count(key)) {
        require(hooks.size() < 256);
        auto* site = pages + hooks.size() * 0x100;
        site[31] = 0xc3;
        hooks.emplace(key, Hook{site, hook_group(reinterpret_cast<uintptr_t>(key))});
    }
    auto& hook = hooks.at(key);
    if (creates == fail_create) {
        failed_group = hook.group;
        require(MH_CreateHook(hook.site, reinterpret_cast<void*>(foreign_detour), nullptr) == MH_OK);
        hook.foreign = true;
        return MH_CreateHook(hook.site, detour, original);
    }
    const auto status = MH_CreateHook(hook.site, detour, original);
    if (status == MH_OK) hook.owned = true;
    return status;
}
MH_STATUS WINAPI enable_hook(void* key) {
    if (active && ++enables == fail_enable) {
        failed_group = hooks.at(key).group;
        return MH_ERROR_MEMORY_PROTECT;
    }
    return MH_EnableHook(active ? mapped(key) : key);
}
MH_STATUS WINAPI queue_enable_hook(void* key) {
    if (active && ++enables == fail_enable) {
        failed_group = hooks.at(key).group;
        return MH_ERROR_MEMORY_PROTECT;
    }
    return MH_QueueEnableHook(active ? mapped(key) : key);
}
MH_STATUS WINAPI disable_hook(void* key) { return MH_DisableHook(active ? mapped(key) : key); }
MH_STATUS WINAPI remove_hook(void* key) {
    const auto status = MH_RemoveHook(active ? mapped(key) : key);
    if (active && status == MH_OK) hooks.at(key).owned = false;
    return status;
}
bool end_hooks(bool expect_cleanup) {
    bool clean = true;
    for (const auto& entry : hooks) {
        const auto& hook = entry.second;
        const auto status = MH_RemoveHook(hook.site);
        require(status == MH_OK || status == MH_ERROR_NOT_CREATED || status == MH_ERROR_NOT_INITIALIZED);
        // Independent optional groups remain installed when another group fails.
        if (hook.owned && expect_cleanup && hook.group == failed_group) clean &= status != MH_OK;
        if (hook.foreign) clean &= status == MH_OK;
        for (unsigned byte = 0; byte < 31; ++byte)
            require(static_cast<uint8_t*>(hook.site)[byte] == 0x90);
    }
    const auto status = MH_Uninitialize();
    require(status == MH_OK || status == MH_ERROR_NOT_INITIALIZED);
    require(VirtualFree(pages, 0, MEM_RELEASE) != FALSE);
    hooks.clear(); pages = nullptr; active = false;
    return clean;
}
}

#ifdef SC_NATIVE_TESTING
namespace sentinel::native {
uint32_t fault_validate_target(engine::Memory& memory, const engine::Image& image, const Target& target,
                               HANDLE stop, uint64_t deadline, ValidationDetail* detail) {
    if (testing::hooks_active()) return SC_NATIVE_NONE;
    return validate_target(memory, image, target, stop, deadline, detail);
}
uint32_t fault_validate_leaf_target(engine::Memory& memory, const engine::Image& image, const Target& target,
                                    size_t length, HANDLE stop, uint64_t deadline, ValidationDetail* detail) {
    if (testing::hooks_active()) return SC_NATIVE_NONE;
    return validate_leaf_target(memory, image, target, length, stop, deadline, detail);
}
uint32_t fault_validate_recorded(save::Installation& record, engine::Memory& memory, const engine::Image& image,
                                 const Target& target, HANDLE stop, uint64_t deadline, uint32_t group, uint32_t index) {
    if (testing::hooks_active()) return SC_NATIVE_NONE;
    return validate_recorded(record, memory, image, target, stop, deadline, group, index);
}
}
#endif

#ifdef SC_FAULT_HOOK_RUNNER
#include "native_test_adapter.h"
#include "native_runtime.h"
#include "campaign_menu_native.h"
#include "save_campaign_native.h"
#include "save_native_hooks.h"
#include "ap_controls_native.h"
#include "arsenal.h"
#include "challenge_suppression.h"
#include "combat_native.h"
#include "deathlink.h"
#include "enemy_checkpoint_native.h"
#include "expanded_native.h"
#include "special.h"
#include "weapon_points.h"
#include "inventory.h"
#include <filesystem>
#include <string_view>

namespace sentinel::testing {
namespace {
void install_native(const engine::Binding& binding, HANDLE stop) {
    native::TestAdapter adapter{};
    auto* bytes = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    require(bytes != nullptr);
    std::memset(bytes, 0x90, 0x1000);
    for (unsigned i = 0; i < adapter.targets.size(); ++i) {
        auto* site = bytes + i * 0x100;
        site[31] = 0xc3;
        adapter.targets[i].address = reinterpret_cast<uintptr_t>(site);
        std::memcpy(adapter.targets[i].bytes.data(), site, 32);
    }
    adapter.root = binding.root;
    Snapshot identity{}; identity.pid = GetCurrentProcessId(); identity.process_created = 1; identity.instance[0] = 1;
    native::prepare(identity);
    native::test_start(adapter, identity, stop);
    require(VirtualFree(bytes, 0, MEM_RELEASE) != FALSE);
}
bool needs_session(const std::wstring& name) {
    return name == L"ap_controls" || name == L"expanded" || name == L"save" || name == L"save_campaign";
}
void prepare_session() {
    wchar_t temp[MAX_PATH]{};
    require(GetTempPathW(MAX_PATH, temp) != 0);
    const auto root = std::wstring(temp) + L"hook-fixture-" + std::to_wstring(GetCurrentProcessId());
    require(CreateDirectoryW(root.c_str(), nullptr) != FALSE);
    storage::Descriptor descriptor{{"hook-fixture", 0, 1, std::string(64, 'a')}, root,
                                  {storage::CampaignIntent::create, 1}};
    std::unique_ptr<storage::Namespace> lease;
    require(storage::prepare(descriptor, lease).ok());
    require(save::session().configure(descriptor, std::move(lease)).ok());
}
void install_menu(const engine::Binding& binding, HANDLE) {
    std::array<native::Target, 46> targets{};
    for (unsigned i = 0; i < targets.size(); ++i) targets[i].address = binding.image.base + 0x100 * (i + 1);
    (void)campaign_menu::test_install_hooks(targets, enable_hook);
}
void install_checkpoint(const engine::Binding& binding, HANDLE stop) {
    (void)expanded::install_recipe_checkpoint(binding, stop,
        {+[] {}, +[] {}, +[](std::string&) { return true; },
         +[](std::string_view, std::string_view) { return true; }});
}
void install(const std::wstring& name) {
    hook_installer = name;
    engine::Binding binding{};
    binding.image.base = 0x180000000; binding.image.size = 0x8000000;
    binding.image.sections = {{0, 0x4000000, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE},
                              {0x4000000, 0x4000000, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE}};
    binding.root = binding.image.base + 0x45ea6f0;
    binding.metadata.profile = SC_PROFILE_STEAM_20260818;
    const std::map<std::wstring, void (*)(const engine::Binding&, HANDLE)> installers{
        {L"native", install_native}, {L"ap_controls", +[](const auto& b, HANDLE s) { (void)ap_controls::install(b, s); }},
        {L"arsenal", arsenal::install}, {L"challenge", challenge::install}, {L"combat", combat::install},
        {L"deathlink", deathlink::install}, {L"expanded", +[](const auto& b, HANDLE s) { (void)expanded::install(b, s); }},
        {L"special", special::install}, {L"weapon_points", weapon_points::install},
        {L"save", save::install_native_hooks},
        {L"save_campaign", +[](const auto& b, HANDLE s) { (void)save::install_campaign_hooks(b, s); }},
        {L"enemy_checkpoint", install_checkpoint}, {L"campaign_menu", install_menu}};
    if (needs_session(name)) prepare_session();
    if (name == L"arsenal") inventory::use_fixture({}, std::string(64, 'a').c_str());
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr); require(stop != nullptr);
    installers.at(name)(binding, stop);
    require(CloseHandle(stop) != FALSE);
    // The session owns its lease until process exit; the parent removes this fixture after the child ends.
}
DWORD hook_child(const std::wstring& name, unsigned create, unsigned enable) {
    wchar_t executable[MAX_PATH]{};
    require(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0);
    std::wstring line = L"\"" + std::wstring(executable) + L"\" create . 1 hook-fault:" + name + L":" +
        std::to_wstring(create) + L":" + std::to_wstring(enable);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE);
    require(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
    DWORD code = 0; require(GetExitCodeProcess(process.hProcess, &code) != FALSE);
    require(CloseHandle(process.hThread) && CloseHandle(process.hProcess));
    require(code <= 1 || (code > 1000 && code < 1256));
    if (needs_session(name)) {
        wchar_t temp[MAX_PATH]{}; require(GetTempPathW(MAX_PATH, temp) != 0);
        const auto root = std::filesystem::path(temp) / (L"hook-fixture-" + std::to_wstring(process.dwProcessId));
        std::filesystem::remove_all(root);
    }
    return code;
}
}
int hook_failure(const std::wstring& defect) {
    const auto first = defect.find(L':'), second = defect.find(L':', first + 1), third = defect.find(L':', second + 1);
    require(first != defect.npos && second != defect.npos && third != defect.npos);
    const auto name = defect.substr(first + 1, second - first - 1);
    const auto create = static_cast<unsigned>(std::wcstoul(defect.c_str() + second + 1, nullptr, 10));
    const auto enable = static_cast<unsigned>(std::wcstoul(defect.c_str() + third + 1, nullptr, 10));
    begin_hooks(create, enable, name != L"native");
    install(name);
    const auto create_count = hook_creates(), enable_count = hook_enables();
    const bool clean = end_hooks(create || enable);
    if (!create && !enable) { require(create_count > 0 && enable_count == create_count); return 1000 + static_cast<int>(create_count); }
    require(create ? create_count >= create : enable_count >= enable);
    if (!clean) std::fwprintf(stderr, L"FAIL partial hooks installer=%s create=%u enable=%u\n", name.c_str(), create, enable);
    return clean ? 0 : 1;
}
void hook_cleanup(const std::wstring& name) {
    hook_installer = name;
    if (name == L"arsenal") {
        require(hook_group(0x181670850) == hook_group(0x18166e750));
        require(hook_group(0x181670850) != hook_group(0x180f1f420));
    }
    if (name == L"special") {
        require(hook_group(0x18145d640) == hook_group(0x18146b850));
        require(hook_group(0x18145d640) != hook_group(0x18145a410));
    }
    const std::map<std::wstring, unsigned> expected{
        {L"native", 3}, {L"ap_controls", 11}, {L"arsenal", 11}, {L"campaign_menu", 33},
        {L"challenge", 3}, {L"combat", 3}, {L"deathlink", 2}, {L"enemy_checkpoint", 2},
        {L"expanded", 49}, {L"save", 39}, {L"save_campaign", 8}, {L"special", 14}, {L"weapon_points", 5}};
    const auto baseline = hook_child(name, 0, 0);
    require(baseline == 1000 + expected.at(name));
    bool clean = true;
    for (unsigned nth = 1; nth <= baseline - 1000; ++nth) {
        const auto create = hook_child(name, nth, 0), enable = hook_child(name, 0, nth);
        if (create || enable)
            std::fwprintf(stderr, L"FAIL hook cleanup installer=%s nth=%u create_exit=%lu enable_exit=%lu\n", name.c_str(), nth, create, enable);
        require(create <= 1 && enable <= 1);
        clean &= create == 0 && enable == 0;
    }
    if (!clean) {
        std::puts("XFAIL failed hook installation retains owned hooks or removes a foreign hook");
        std::exit(1);
    }
    std::puts("PASS failed hook installation releases owned hooks and preserves foreign hooks");
}
}

#endif
