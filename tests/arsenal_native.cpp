#include "../src/arsenal_native.cpp"

namespace sentinel::arsenal {
bool test_invalid_fire_mode() {
    const auto saved_ready = ready.load();
    const auto saved_fixture = fixture_namespace[0];
    fixture_namespace[0] = 'a'; ready.store(true);
    const bool invalid_mode_refused = !fire_hook(0, 1, 0, nullptr, 0) && !ready.load();
    ready.store(saved_ready); fixture_namespace[0] = saved_fixture;
    return invalid_mode_refused;
}
bool test_mastery_selection() {
    const auto previous_screen=menu::clicked_mod_screen;
    uintptr_t widget[0x2c0/sizeof(uintptr_t)]{},family[6]{},perk[2]{},base[2]{};
    const auto address=reinterpret_cast<uintptr_t>(widget);
    widget[0x2b8/sizeof(uintptr_t)]=reinterpret_cast<uintptr_t>(family);
    family[0]=reinterpret_cast<uintptr_t>(base); family[5]=reinterpret_cast<uintptr_t>(perk);
    menu::clicked_mod_screen=0;
    bool selection=!menu::clicked_mastery(1,address);
    menu::clicked_mod_screen=1;
    for (const auto& mod:mastery_families) {
        perk[1]=reinterpret_cast<uintptr_t>(mod.mastery); base[1]=reinterpret_cast<uintptr_t>(mod.base);
        selection&=menu::clicked_mastery(1,address)==mod.mastery;
        selection&=!menu::clicked_mastery(2,address) && !menu::clicked_mastery(1,0);
        base[1]=reinterpret_cast<uintptr_t>("foreign/mod");
        selection&=!menu::clicked_mastery(1,address);
    }
    family[5]=0; selection&=!menu::clicked_mastery(1,address);
    menu::clicked_mod_screen=previous_screen;
    if (!selection) { std::fprintf(stderr,"mastery canonical selection failed\n"); return false; }
    return true;
}
static bool mastery_masks_match(uint16_t target, uint16_t failed, uint16_t applied, uintptr_t component) {
        return mastery_state.target_unavailable == target && mastery_state.apply_failed == failed &&
            mastery_state.applied == applied && mastery_state.components[0] == component;
}

bool test_mastery_masks() {
    if (!test_invalid_fire_mode()) return false;
    if (!test_mastery_selection()) return false;
    uintptr_t family[6]{}, perk[2]{}, base[2]{};
    family[0] = reinterpret_cast<uintptr_t>(base);
    const auto saved = mastery_state;
    mastery_state = {};
    std::array<unsigned char,0x20> ui{};
    const auto ui_address=reinterpret_cast<uintptr_t>(ui.data());
    perk[1]=reinterpret_cast<uintptr_t>(mastery_families[0].mastery);
    base[1]=reinterpret_cast<uintptr_t>(mastery_families[0].base);
    family[5]=reinterpret_cast<uintptr_t>(perk);
    *reinterpret_cast<uint32_t*>(ui.data()+0x18)=2;
    *reinterpret_cast<uint16_t*>(ui.data()+0x12)=0;
    *reinterpret_cast<uint16_t*>(ui.data()+0x14)=1;
    mastery_state.desired=593;
    project_mastery_ui(ui_address,reinterpret_cast<uintptr_t>(family));
    bool ui_ok=ui[0x1c]==0;
    mastery_state.applied=593;
    project_mastery_ui(ui_address,reinterpret_cast<uintptr_t>(family));
    ui_ok &= ui[0x1c]==1 && *reinterpret_cast<uint32_t*>(ui.data()+0x18)==2 &&
        *reinterpret_cast<uint16_t*>(ui.data()+0x12)==0 && *reinterpret_cast<uint16_t*>(ui.data()+0x14)==1;
    ui[0x1c]=0;
    base[1]=reinterpret_cast<uintptr_t>("foreign/mod");
    project_mastery_ui(ui_address,reinterpret_cast<uintptr_t>(family));
    ui_ok &= ui[0x1c]==0;
    mastery_state = {};
    mastery_result(0, 0, false, false);
    const bool a = mastery_masks_match(1, 0, 0, 0);
    mastery_result(0, 0x100, true, false);
    const bool b = mastery_masks_match(0, 1, 0, 0);
    mastery_result(0, 0x100, true, true);
    const bool c = mastery_masks_match(0, 0, 1, 0x100);
    mastery_result(0, 0x100, true, false);
    const bool d = mastery_masks_match(0, 1, 0, 0);
    mastery_result(0, 0x100, true, true);
    const bool e = mastery_masks_match(0, 0, 1, 0x100);
    mastery_result(0, 0x200, true, true);
    const bool f = mastery_masks_match(0, 0, 1, 0x200);
    mastery_result(0, 0, false, false);
    const bool g = mastery_masks_match(1, 0, 0, 0);
    mastery_state = saved;
    const bool all = ui_ok && a && b && c && d && e && f && g;
    if (!all)
        std::fprintf(stderr,"mastery UI=%d masks=%d%d%d%d%d%d%d\n",ui_ok,a,b,c,d,e,f,g);
    return all;
}
}
