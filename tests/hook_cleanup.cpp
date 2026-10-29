#include "campaign_menu_native.h"
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL hook_cleanup.cpp:%d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
namespace {
unsigned enable_calls=0,fail_enable=0;
MH_STATUS WINAPI enable(void* site) {
    if (++enable_calls==fail_enable) {
        // batch thread suspension; earlier hooks are active when cleanup starts.
        CHECK(MH_ApplyQueued()==MH_OK);
        return MH_ERROR_MEMORY_PROTECT;
    }
    return MH_QueueEnableHook(site);
}
void detour() {}
}
void test_campaign_hook_cleanup() {
    constexpr unsigned hooked[]={0,1,2,3,4,11,12,9,15,16,17,27,28,29,30,31,32,34,35,38,39,41,42,45};
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(memory);
    std::array<sentinel::native::Target,46> targets{};
    for (unsigned i=0;i<targets.size();++i) {
        auto* site=memory+i*0x100+0x10;
        std::memset(site,0x90,32); site[31]=0xc3;
        targets[i].address=reinterpret_cast<uintptr_t>(site);
        std::memcpy(targets[i].bytes.data(),site,32);
    }
    DWORD previous=0;
    CHECK(VirtualProtect(memory,0x4000,PAGE_EXECUTE_READ,&previous));
    CHECK(FlushInstructionCache(GetCurrentProcess(),memory,0x4000));
    CHECK(MH_Initialize()==MH_OK);
    for (unsigned nth=1;nth<=std::size(hooked);++nth) {
        auto* foreign=reinterpret_cast<void*>(targets[hooked[nth-1]].address);
        CHECK(MH_CreateHook(foreign,reinterpret_cast<void*>(detour),nullptr)==MH_OK);
        CHECK(!sentinel::campaign_menu::test_install_hooks(targets,enable));
        for (unsigned i=0;i<std::size(hooked);++i) {
            auto* site=reinterpret_cast<void*>(targets[hooked[i]].address);
            CHECK(MH_RemoveHook(site)==(i==nth-1 ? MH_OK : MH_ERROR_NOT_CREATED));
            CHECK(!std::memcmp(site,targets[hooked[i]].bytes.data(),32));
        }
        enable_calls=0; fail_enable=nth;
        CHECK(!sentinel::campaign_menu::test_install_hooks(targets,enable));
        CHECK(enable_calls==nth);
        for (const auto index:hooked) {
            auto* site=reinterpret_cast<void*>(targets[index].address);
            CHECK(MH_RemoveHook(site)==MH_ERROR_NOT_CREATED);
            CHECK(!std::memcmp(site,targets[index].bytes.data(),32));
        }
    }
    CHECK(MH_Uninitialize()==MH_OK);
    CHECK(VirtualFree(memory,0,MEM_RELEASE));
}
