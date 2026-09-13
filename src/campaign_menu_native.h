#pragma once
#include "engine_observer.h"
#include "native_target.h"
#include <string>

namespace sentinel::campaign_menu {
bool install(const engine::Binding&,HANDLE stop);
std::array<native::Target,15> native_targets(uintptr_t base);
bool validate_native_targets(save::Installation&,engine::Memory&,const engine::Image&,
                             HANDLE,const std::array<native::Target,15>&);
bool available();
void present_campaign_actions(uintptr_t screen, bool entered);
bool mission_request(uintptr_t request, std::string& destination);
bool request_map(uintptr_t request, const std::string& map);
#ifdef SC_NATIVE_TESTING
struct NativeCalls {
    void (*populate)(uintptr_t,uintptr_t);
    void (*focus)(uintptr_t);
    void (*load)(uintptr_t,int);
    bool (*available)(uintptr_t);
    void (*update)(uintptr_t);
    void (*string_init)(uintptr_t);
    void (*string_assign)(uintptr_t,const char*);
    bool (*completed)(uintptr_t,uintptr_t);
    void (*list_assign)(uintptr_t,uintptr_t);
    void (*details_update)(uintptr_t);
    void (*widget_state)(uintptr_t,int);
    void (*root_navigation)(uintptr_t,uint8_t);
    void (*root_campaign)(uintptr_t,uintptr_t);
    uintptr_t (*campaign_definitions)();
    void (*sprite_changed)(uintptr_t);
};
void test_calls(const NativeCalls&);
void test_populate(uintptr_t,uintptr_t);
void test_focus(uintptr_t);
void test_load(uintptr_t,int);
void test_update(uintptr_t);
void test_root_navigation(uintptr_t,uint8_t);
void test_root_campaign(uintptr_t,uintptr_t);
#endif
}
