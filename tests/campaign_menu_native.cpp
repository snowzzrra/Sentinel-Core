#include "../src/campaign_menu_native.cpp"

namespace sentinel::campaign_menu {
bool test_stage_presentation(uint32_t stage,bool end) {
    const auto projection=menu().projection();
    for (uint32_t i=0;i<projection.count;++i) if (projection.rows[i].id==stage) {
        const auto& expected=projection.summaries[i];
        const auto challenge=challenge_projection(unlockable_id("shared/challenge"),end);
        const auto mission=mission_reward(unlockable_id("shared/mission"),end);
        const auto aggregate=aggregate_reward(end);
        if (challenge.found!=expected.challenges[0].found ||
            mission.location_id!=projection.rewards[i][0].location_id ||
            aggregate.location_id!=projection.rewards[i][1].location_id ||
            end_items_summary(projection,stage).found!=expected.found) return false;
        if (end) return true;
        std::array<unsigned char,0x660> screen{};
        std::array<unsigned char,0x2c0> widget{};
        const auto address=reinterpret_cast<uintptr_t>(screen.data());
        *reinterpret_cast<uintptr_t*>(screen.data()+0x640)=reinterpret_cast<uintptr_t>(widget.data());
        *reinterpret_cast<uint32_t*>(widget.data()+0x2b0)=unlockable_id("shared/mission");
        widget[0x2b4]='x';
        mission_hint_intents(address);
        sc_campaign_request request{}; request.reward_presentation=1;
        strcpy_s(request.namespace_id,projection.namespace_id.c_str());
        const auto hints=menu().request(campaign_inspect_operation,request,true);
        return hints.intent_count==1 && hints.intent_ids[0]==mission.location_id;
    }
    return false;
}
bool test_hud_score_root() {
    std::array<unsigned char,0x180> score{},swf{},widget{},root{};
    const auto owner=reinterpret_cast<uintptr_t>(score.data());
    const auto expected=reinterpret_cast<uintptr_t>(root.data());
    *reinterpret_cast<uintptr_t*>(score.data()+0x10)=reinterpret_cast<uintptr_t>(swf.data());
    *reinterpret_cast<uintptr_t*>(score.data()+0x88)=reinterpret_cast<uintptr_t>(widget.data());
    *reinterpret_cast<uintptr_t*>(swf.data()+0x18)=expected;
    static uintptr_t observed=0;
    observed=0;
    const auto saved=swf_lookup;
    swf_lookup=+[](uintptr_t parent,SwfValue*,const char*)->SwfValue* {
        observed=parent; return nullptr;
    };
    present_score_found(owner);
    swf_lookup=saved;
    return observed==expected && !*reinterpret_cast<uintptr_t*>(widget.data()+0x18) &&
        !*reinterpret_cast<uintptr_t*>(score.data()+0xf8);
}
static bool check_encounter_reward_text() {
    const auto saved_text=cached_text;
    const auto saved_sprite=cached_sprite;
    const auto saved_set_text=swf_set_text;
    static uintptr_t card=0;
    static unsigned written=0;
    std::array<uint8_t,0x398> reward_widget{};
    card=reinterpret_cast<uintptr_t>(reward_widget.data());
    written=0;
    cached_text=[](uintptr_t field,const char*)->uintptr_t {
        return field==card+0x210 ? 1 : field==card+0x240 ? 2 : 0;
    };
    cached_sprite=[](uintptr_t)->uintptr_t { return 0; };
    swf_set_text=[](uintptr_t field,const char* text) {
        if (field==1 && !std::strcmp(text,"Ballista")) written|=1;
        if (field==2 && !std::strcmp(text,player_strings::menu_reward_category)) written|=2;
    };
    sc_campaign_reward reward{};
    reward.location_id=7770001;
    std::memcpy(reward.text,"Ballista",9);
    present_encounter_card(card,reward);
    cached_text=saved_text;
    cached_sprite=saved_sprite;
    swf_set_text=saved_set_text;
    return written == 3;
}
bool test_question_material() {
    uintptr_t sprite[0x130/sizeof(uintptr_t)]{};
    std::array<uint8_t,0x130> owner{};
    sprite[0x30/sizeof(uintptr_t)]=reinterpret_cast<uintptr_t>(owner.data());
    owner[0x128]=1;
    const auto previous=swf_create_material;
    const auto previous_frame=swf_frame;
    static unsigned frames=0;
    frames=0;
    swf_frame=[](uintptr_t instance,uint32_t frame) {
        ++frames;
        if (frame==1) {
            *reinterpret_cast<uint8_t*>(instance+0x50)=0;
            *reinterpret_cast<uint32_t*>(instance+0x58)=1;
        }
    };
    *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(sprite)+0x50)=1;
    swf_create_material=[](uintptr_t swf,const int* type,const char*,const char* texture)->uintptr_t {
        return !*reinterpret_cast<const uint8_t*>(swf+0x128) && *type==1 &&
            !std::strcmp(texture,"textures/swf_images/dossier/codex/hud_codex_questionmark.png") ? 0x123 : 0;
    };
    const bool created=question_material(reinterpret_cast<uintptr_t>(sprite))==0x123 && owner[0x128]==1 &&
        !*reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(sprite)+0x50);
    swf_create_material=[](uintptr_t,const int*,const char*,const char*)->uintptr_t { return 0; };
    const bool refused=!question_material(reinterpret_cast<uintptr_t>(sprite)) && owner[0x128]==1;
    swf_create_material=previous;
    swf_frame=previous_frame;
    const bool reward_text = check_encounter_reward_text();
    return created && refused && frames==1 && reward_text;
}
bool test_physical_completion_edges() {
    CompletionToast toast{};
    if (completion_edge(toast,1,true) || completion_edge(toast,1,true)) return false;
    toast={};
    if (completion_edge(toast,1,false) || completion_edge(toast,1,false) || !completion_edge(toast,1,true)) return false;
    if (!completion_edge(toast,1,true)) return false;
    toast.checked=true;
    if (completion_edge(toast,1,true) || completion_edge(toast,2,true)) return false;
    toast={};
    return !completion_edge(toast,1,true);
}
}
