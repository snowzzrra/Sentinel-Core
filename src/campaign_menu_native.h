#pragma once
#include "engine_observer.h"

namespace sentinel::campaign_menu {
bool install(const engine::Binding&,HANDLE stop);
bool available();
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
