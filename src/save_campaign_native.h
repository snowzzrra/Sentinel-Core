#pragma once
#include "engine_observer.h"
#include "save_campaign.h"
#include "save_submission.h"
#include "native_target.h"
namespace sentinel::save {
bool install_campaign_hooks(const engine::Binding&, HANDLE);
std::array<native::Target,10> campaign_targets(uintptr_t image);
bool campaign_change_begin(uintptr_t root, uintptr_t descriptor, CampaignTransition&);
void campaign_change_end(CampaignTransition&);
void campaign_checkpoint_boundary(CampaignTransition);
SaveReference* campaign_save_factory(uintptr_t caller,uintptr_t expected_caller,uintptr_t manager,
    SaveReference* out,uint32_t user,uintptr_t request,NativeSaveFactory original);
#ifdef SC_NATIVE_TESTING
struct CampaignNativeTestCalls {
    void (*new_game)(uintptr_t,uint32_t,uint8_t);
    void (*internal)(uintptr_t,uint32_t,uint8_t,uint32_t);
    uint64_t (*action)(uintptr_t,uintptr_t);
    uint32_t (*slot)(uintptr_t);
    void (*select)(uintptr_t,uint32_t);
    uintptr_t (*menu)();
    void (*integer)(uintptr_t,uint32_t,uint8_t);
    uint64_t (*cvar)(uintptr_t,const char*,uint8_t);
};
void test_campaign_binding(uintptr_t image, uintptr_t root);
void test_campaign_calls(const CampaignNativeTestCalls&);
uint64_t test_campaign_action(uintptr_t screen,uintptr_t action);
void test_campaign_internal(uintptr_t menu,uint32_t difficulty,uint8_t extra,uint32_t policy);
void test_campaign_new(uintptr_t menu,uint32_t difficulty,uint8_t extra);
uint64_t test_campaign_cvar(uintptr_t object,const char* value,uint8_t force);
#endif
}
