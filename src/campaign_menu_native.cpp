#include "campaign_menu_native.h"
#include "campaign_menu.h"
#include "fast_travel.h"
#include "special.h"
#include "native_target.h"
#include "native_runtime.h"
#include "save_collector.h"
#include "save_session.h"
#include "MinHook.h"
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <string>

namespace sentinel::campaign_menu {
namespace {
using Populate=void(*)(uintptr_t,uintptr_t);
using Update=void(*)(uintptr_t);
using Load=void(*)(uintptr_t,int);
using Available=bool(*)(uintptr_t);
using Completed=bool(*)(uintptr_t,uintptr_t);
using StringInit=void(*)(uintptr_t);
using StringAssign=void(*)(uintptr_t,const char*);
using ListAssign=void(*)(uintptr_t,uintptr_t);
using WidgetState=void(*)(uintptr_t,int);
using RootNavigation=void(*)(uintptr_t,uint8_t);
using CampaignDefinitions=uintptr_t(*)();
using MeterAllocate=void(*)(uintptr_t,uintptr_t,uintptr_t);
using SpriteVisibility=void(*)(uintptr_t,uint32_t,uint32_t);
RootNavigation original_root_navigation=nullptr;
Populate original_root_campaign=nullptr;
CampaignDefinitions campaign_definitions=nullptr;
Populate original_populate=nullptr;
Update original_focus=nullptr,original_update=nullptr,details_update=nullptr;
using UserInfoPoint=void(*)(uintptr_t,int,uintptr_t,int,int,int);
UserInfoPoint original_userinfo_point=nullptr;
Update original_dossier_map=nullptr;
Update original_challenge_card_update=nullptr;
Update original_hud_challenge_update=nullptr,original_eol_challenge_update=nullptr;
Update original_category_counts=nullptr;
Update original_start_show=nullptr;
Populate original_boss_update=nullptr;
Update original_hud_score_init=nullptr;
Update original_end_items=nullptr;
Update original_end_combat_init=nullptr;
MeterAllocate original_meter_allocate=nullptr;
SpriteVisibility original_sprite_visibility=nullptr;
struct SwfValue { uint32_t type=0,reserved=0; uintptr_t payload=0; };
using SwfLookup=SwfValue*(*)(uintptr_t,SwfValue*,const char*);
using SwfGet=uintptr_t(*)(const SwfValue*);
using SwfRelease=void(*)(SwfValue*);
using SwfText=void(*)(uintptr_t,const char*);
using SwfMaterial=void(*)(uintptr_t,uintptr_t,uint32_t);
using SwfLabel=uint32_t(*)(uintptr_t,const char*,uint32_t);
using SwfFrame=void(*)(uintptr_t,uint32_t);
using FindMaterial=uintptr_t(*)(uintptr_t,const char*,int);
using MapManager=uintptr_t(*)();
using MapLookup=uintptr_t(*)(uintptr_t,const char*,uint32_t,uint8_t);
using Localize=const char*(*)(const uint32_t*);
SwfLookup swf_lookup=nullptr;
SwfGet swf_sprite=nullptr,swf_text=nullptr;
SwfRelease swf_release=nullptr;
SwfText swf_set_text=nullptr;
SwfMaterial swf_set_material=nullptr;
SwfLabel swf_label=nullptr;
SwfFrame swf_frame=nullptr;
FindMaterial find_material=nullptr;
uintptr_t material_manager=0;
uintptr_t end_items_category_vtable=0;
MapManager map_manager=nullptr;
MapLookup map_lookup=nullptr;
Localize localize=nullptr;
using MeterUpdate = void (*)(uintptr_t);
MeterUpdate original_meter_update=nullptr;
Load original_load=nullptr;
Available original_available=nullptr;
Completed native_completed=nullptr;
StringInit string_init=nullptr;
StringAssign string_assign=nullptr;
ListAssign list_assign=nullptr;
WidgetState widget_state=nullptr;
Update sprite_changed=nullptr;
uintptr_t meter_visibility_return=0;
thread_local uintptr_t current_ap_details=0;
std::atomic<uintptr_t> ap_meter_published{0};
std::atomic<uint64_t> meter_update_ticks{0};
std::atomic<uint64_t> meter_update_suppressed{0};
std::atomic<uint64_t> meter_update_delegated{0};
std::atomic<uint64_t> ap_meter_retire_count{0};
void retire_ap_meter() {
    if (ap_meter_published.exchange(0,std::memory_order_acq_rel)!=0) {
        ap_meter_retire_count.fetch_add(1,std::memory_order_relaxed);
    }
}
engine::LocalMemory memory;
std::atomic<bool> ready{false};
struct List { uintptr_t data=0; int32_t count=0,capacity=0; uint32_t flags=0x50000,padding=0; };
struct Entry { save::NativeString name{}; List dependencies{}; uint32_t reserved=0; uint8_t visible=1,padding[3]{}; };
struct MapList { uint8_t prefix[0x88]{}; List entries{}; };
static_assert(sizeof(Entry)==0x50 && offsetof(Entry,visible)==0x4c);
std::array<Entry,SC_CAMPAIGN_MENU_MAX_ROWS> entries;
MapList map_list;
Projection shown;
uintptr_t shown_screen=0;
bool initialized=false;
thread_local bool building=false;
template<class T> bool read(uintptr_t base,size_t offset,T& out) {
    uintptr_t address=0; return engine::add(base,offset,sizeof(out),address) && !memory.copy(address,&out,sizeof(out)).reason;
}
bool active() { return save::session().campaign_run.enabled(); }
void fault(const char* reason);
void present_logo(uintptr_t screen,bool start);
struct RootLayout {
    std::array<uintptr_t,9> sprites{};
    std::array<std::array<float,2>,9> positions{};
} root_layout;
void compact_root(uintptr_t screen) {
    // Named Root widgets retain their authored SWF positions when hidden.
    // Keep their native bindings/actions and fill the two vacated positions.
    constexpr size_t offsets[]{0x140,0x148,0x150,0x158,0x160,0x168,0x178,0x180,0x188};
    RootLayout observed;
    std::array<uintptr_t,9> translations{};
    for (size_t i=0;i<observed.sprites.size();++i) {
        uintptr_t widget=0,renderer=0,transforms=0; int32_t index=-1;
        if (!read(screen,offsets[i],widget) || !read(widget,0x18,observed.sprites[i]) || !observed.sprites[i] ||
            !read(observed.sprites[i],0xc,index) || index<0 ||
            !read(observed.sprites[i],0x10,renderer) || !read(renderer,0x80,transforms) || !transforms) {
            save::session().btrace.record(save::BStage::root_layout,save::BStatus::pending,"root_layout_widget_pending",0,
                {{"row",i},{"widget_offset",offsets[i]},{"widget_present",widget!=0},
                 {"sprite_present",observed.sprites[i]!=0},{"transform_index",index}},screen);
            return;
        }
        // Native SET_x/SET_y use this same local translation and invalidate
        // the sprite's render/hit-test descendants with 0x1857110.
        if (!engine::add(transforms,size_t(index)*0x40+0x14,sizeof(observed.positions[i]),translations[i]) ||
            memory.copy(translations[i],observed.positions[i].data(),sizeof(observed.positions[i])).reason ||
            !std::isfinite(observed.positions[i][0]) || !std::isfinite(observed.positions[i][1])) {
            save::session().btrace.record(save::BStage::root_layout,save::BStatus::pending,"root_layout_transform_pending",0,{{"row",i}},screen);
            return;
        }
    }
    if (observed.sprites!=root_layout.sprites) root_layout=observed;
    constexpr size_t remaining[]{0,3,4,5,6,7,8};
    for (size_t slot=1;slot<std::size(remaining);++slot) {
        const auto i=remaining[slot];
        if (observed.positions[i]==root_layout.positions[slot]) continue;
        std::memcpy(reinterpret_cast<void*>(translations[i]),root_layout.positions[slot].data(),sizeof(root_layout.positions[slot]));
        sprite_changed(observed.sprites[i]);
    }
    save::session().btrace.record(save::BStage::root_layout,save::BStatus::succeeded,"root_layout_compacted",0,{{"visible_rows",7}},screen);
}
void root_navigation(uintptr_t screen,uint8_t reset) {
    retire_ap_meter();
    if (active()) save::session().campaign_run.cancel_menu_save();
    shown_screen=0;
    if (active()) {
        // State 0 is the native hidden state. Root navigation skips it when
        // restoring focus; native widgets and their ownership remain intact.
        for (const size_t offset : {0x148u,0x150u}) {
            uintptr_t widget=0;
            if (!read(screen,offset,widget) || !widget) { fault("campaign_root_widget_unreadable"); return; }
            widget_state(widget,0);
        }
    }
    original_root_navigation(screen,reset);
    if (active()) compact_root(screen);
    __try { present_logo(screen,false); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "main_logo_widget_fault",0,{},screen);
    }
}
void root_campaign(uintptr_t screen,uintptr_t declaration) {
    retire_ap_meter();
    if (active()) save::session().campaign_run.cancel_menu_save();
    shown_screen=0;
    if (active()) {
        const auto definitions=campaign_definitions();
        if (!definitions || !read(definitions,0x148,declaration) || !declaration) {
            fault("campaign_root_main_declaration_unavailable"); return;
        }
        *reinterpret_cast<uint32_t*>(screen+0x1c8)=0;
    }
    original_root_campaign(screen,declaration);
    __try { present_logo(screen,false); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "main_logo_widget_fault",0,{},screen);
    }
}
uintptr_t list_for(uintptr_t screen) {
    uintptr_t table=0,method=0;
    if (!read(screen,0,table) || !read(table,0x1e8,method) || !method) return 0;
    return reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
}
void fault(const char* reason) { save::session().campaign_run.refuse(reason,save::BStage::transition); }
bool ap_details(uintptr_t details) {
    uintptr_t owner=0;
    return active() && shown_screen && read(shown_screen,0x118,owner) && owner==details;
}
uintptr_t swf_child(uintptr_t parent,const char* name,bool text=false,uint32_t* type=nullptr) {
    if (!parent) return 0;
    SwfValue value{};
    const auto found=swf_lookup(parent,&value,name);
    if (type) *type=value.type;
    const auto child=found?(text?swf_text(found):swf_sprite(found)):0;
    if (value.type==2 || value.type==8) swf_release(&value);
    return child;
}
int challenge_slot(const sc_physical_challenge& challenge) {
    const auto suffix=std::strrchr(challenge.unlockable,'_');
    return challenge.required && suffix && suffix[1]>='1' && suffix[1]<='3' && !suffix[2]
        ? suffix[1]-'1' : -1;
}
void present_logo(uintptr_t screen,bool start) {
    const auto stage=start?save::BStage::start_logo:save::BStage::main_logo;
    uintptr_t table=0,method=0;
    if (!read(screen,0,table) || !read(table,0x70,method) || !method) {
        save::session().btrace.record(stage,save::BStatus::pending,"logo_root_method_unavailable"); return;
    }
    const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
    const auto group=swf_child(swf_child(root,"_center"),"logo");
    // Packaged bitmap resources supply the composition in the authored geometry.
    save::session().btrace.record(stage,group?save::BStatus::succeeded:save::BStatus::pending,
        group?"logo_container_bound":"logo_container_unavailable",0,{{"container",group!=0}});
}
void start_show(uintptr_t screen) {
    original_start_show(screen);
    __try { present_logo(screen,true); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "start_logo_widget_fault",0,{},screen);
    }
}
void present_save_preview(uintptr_t screen,const save::CampaignSnapshot& snapshot) {
    uintptr_t table=0,method=0;
    if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
    const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
    const auto preview=swf_child(swf_child(root,"_center"),"saveInfo");
    if (!preview) return;
    uintptr_t declaration=0,material=0,registry=0;
    if (snapshot.checkpoint && !snapshot.map.empty()) {
        const auto manager=map_manager();
        if (manager && read(manager,8,registry) && registry)
            declaration=map_lookup(registry,snapshot.map.c_str(),1,0);
        if (declaration) read(declaration,0x138,material);
    }
    original_sprite_visibility(preview,material!=0,1);
    if (!material) return;
    if (const auto image=swf_child(preview,"mapImage")) {
        original_sprite_visibility(image,1,1);
        swf_set_material(image,material,0);
    }
    const auto bar=swf_child(preview,"saveInfoBar");
    if (bar) original_sprite_visibility(bar,1,1);
    const auto name=swf_child(swf_child(bar,"mapName"),"txtVal",true);
    const auto row_projection=menu().projection();
    const char* title=nullptr;
    for (uint32_t i=0;i<row_projection.count;++i) {
        if (!std::strcmp(row_projection.rows[i].map,snapshot.map.c_str())) {
            title=row_projection.rows[i].title;
            break;
        }
    }
    if (name && title && *title) {
        swf_set_text(name,title);
        save::NativeString actual{};
        const bool verified=read(name,0x40,actual) && actual.data &&
            actual.length==static_cast<int32_t>(std::strlen(title)) &&
            !std::memcmp(actual.data,title,static_cast<size_t>(actual.length));
        save::session().btrace.record(save::BStage::profile_output,
            verified?save::BStatus::succeeded:save::BStatus::pending,
            verified?"checkpoint_title_bound":"checkpoint_title_readback_pending",0,
            {{"title",name!=0},{"row",title!=nullptr},{"readback",verified}},screen);
    } else {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::pending,
            "checkpoint_title_binding_pending",0,{{"title",name!=0},{"row",title!=nullptr}},screen);
    }
}
void safe_present_save_preview(uintptr_t screen,const save::CampaignSnapshot& snapshot) {
    __try { present_save_preview(screen,snapshot); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "campaign_save_preview_widget_fault",0,{},screen);
    }
}
void userinfo_point(uintptr_t widget,int kind,uintptr_t material,int count,int width,int delta) {
    // Native Dossier/Pause currency IDs: 1 Praetor, 2 Mastery.
    // The widget lays out positions and dividers from retained rows.
    if (active() && (kind==1 || kind==2)) {
        save::session().btrace.record(save::BStage::dossier_points,save::BStatus::succeeded,
            "native_currency_row_suppressed",0,{{"kind",kind}},widget);
        return;
    }
    original_userinfo_point(widget,kind,material,count,width,delta);
}
void present_hud_found(uintptr_t meter);
void present_dossier_map(uintptr_t screen) {
    uintptr_t meter=0,widget=0,content=0;
    if (read(screen,0x150,meter) && meter) present_hud_found(meter);
    if (read(screen,0x138,widget) && read(widget,0x18,content) && content)
        original_sprite_visibility(content,0,1);
}
void dossier_map(uintptr_t screen) {
    original_dossier_map(screen);
    if (!active()) return;
    __try { present_dossier_map(screen); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "dossier_items_widget_fault",0,{},screen);
    }
}
sc_physical_challenge challenge_projection(uint32_t id,bool end=false) {
    if (!active() || !id) return {};
    const auto campaign=save::session().campaign_run.snapshot();
    const auto& map=end && campaign.map=="game/hub/hub" ? campaign.end_summary_map : campaign.map;
    const auto projection=menu().projection();
    for (uint32_t i=0;i<projection.count;++i) {
        if (!(projection.rows[i].flags&SC_CAMPAIGN_DETAILS) || map!=projection.rows[i].map ||
            projection.summaries[i].known!=1) continue;
        for (const auto& challenge:projection.summaries[i].challenges) {
            // idDeclUnlockable::GetDisplayInfo hashes its exact name with unsigned 31*x+c.
            uint32_t key=0;
            for (const auto* p=challenge.unlockable;*p;++p) key=key*31+static_cast<uint8_t>(*p);
            if (challenge.required && key==id) return challenge;
        }
    }
    return {};
}
sc_campaign_reward mission_reward(uint32_t id,bool end=false) {
    if (!active() || !id) return {};
    const auto campaign=save::session().campaign_run.snapshot();
    const auto& map=end && campaign.map=="game/hub/hub" && !campaign.end_summary_map.empty() ? campaign.end_summary_map : campaign.map;
    const auto projection=menu().projection();
    if (projection.namespace_id!=save::session().namespace_id()) return {};
    for (uint32_t i=0;i<projection.count;++i) {
        if (!(projection.rows[i].flags&SC_CAMPAIGN_REVEALED) || map!=projection.rows[i].map) continue;
        for (const auto& reward:projection.rewards[i]) {
            uint32_t key=0;
            for (const auto* p=reward.unlockable;*p;++p) key=key*31+static_cast<uint8_t>(*p);
            if (reward.kind==SC_REWARD_MISSION && key==id) return reward;
        }
    }
    return {};
}
bool reward_represented(uintptr_t screen,const sc_campaign_reward& reward) {
    for (unsigned i=0;i<3;++i) {
        uintptr_t widget=0; uint32_t id=0;
        if (!read(screen,0x640+i*8,widget) || !read(widget,0x2b0,id) || !id) continue;
        for (unsigned n=1;n<=(reward.kind==SC_REWARD_AGGREGATE ? 3u : 1u);++n) {
            const auto identity=std::string(reward.unlockable)+(reward.kind==SC_REWARD_AGGREGATE ? "/challenge_"+std::to_string(n) : "");
            uint32_t key=0;
            for (const auto c:identity) key=31*key+static_cast<uint8_t>(c);
            if (key==id) return true;
        }
    }
    return false;
}
void mission_hint_intents(uintptr_t screen) {
    if (!active()) return;
    const auto campaign=save::session().campaign_run.snapshot();
    const auto projection=menu().projection();
    const auto& ns=save::session().namespace_id();
    if (projection.namespace_id!=ns) return;
    for (uint32_t i=0;i<projection.count;++i)
        if ((projection.rows[i].flags&SC_CAMPAIGN_REVEALED) && campaign.map==projection.rows[i].map)
            for (const auto& reward:projection.rewards[i])
                if (reward.kind==SC_REWARD_MISSION && reward_represented(screen,reward))
                    menu().hint_intent(ns.c_str(),reward.location_id);
}
void hide_battery_region(uintptr_t battery) {
    if (active() && battery) original_sprite_visibility(battery,0,1);
}
void hide_mission_battery(uintptr_t screen) {
    uintptr_t widget=0,root=0;
    if (read(screen,0x638,widget) && read(widget,0x18,root)) {
        hide_battery_region(swf_child(root,"sentinelBattery"));
        if (const auto arrow=swf_child(root,"arrow")) original_sprite_visibility(arrow,0,1);
    }
}
Update original_end_challenges_show=nullptr;
void end_challenges_show(uintptr_t screen) {
    original_end_challenges_show(screen);
    if (!active()) return;
    __try {
        uintptr_t table=0,method=0;
        if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
        const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
        hide_battery_region(swf_child(swf_child(root,"_center"),"sentinelBattery"));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::end_challenges,save::BStatus::blocked,"aggregate_reward_fault");
    }
}
Update original_challenges_show=nullptr,original_mission_tab=nullptr;
using PageAction=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
PageAction original_challenges_action=nullptr;
thread_local bool explicit_challenges_page=false;
void publish_mission_page(uintptr_t screen) {
    if (!active()) return;
    __try {
        uintptr_t table=0,widget=0,root=0; uint8_t visible=0;
        if (!read(screen,0,table) || table!=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x2d0ba68 ||
            !read(screen,0x638,widget) || !read(widget,0x18,root) || !read(root,0x51,visible) || !visible) return;
        uintptr_t method=0;
        if (!read(table,0x70,method) || !method) return;
        const auto center=swf_child(reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen),"main");
        if (read(center,0x51,visible) && visible) mission_hint_intents(screen);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void challenges_show(uintptr_t screen) {
    const bool previous=explicit_challenges_page;
    explicit_challenges_page=true;
    __try { original_challenges_show(screen); }
    __finally { explicit_challenges_page=previous; }
    if (!previous) publish_mission_page(screen);
}
uintptr_t challenges_action(uintptr_t screen,uintptr_t action,uintptr_t event,uintptr_t source) {
    const bool previous=explicit_challenges_page;
    explicit_challenges_page=true;
    uintptr_t result=0;
    __try { result=original_challenges_action(screen,action,event,source); }
    __finally { explicit_challenges_page=previous; }
    if (!previous) publish_mission_page(screen);
    return result;
}
void mission_tab(uintptr_t screen) {
    original_mission_tab(screen);
    __try {
        hide_mission_battery(screen);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void present_card_reward(uintptr_t widget,uint32_t id) {
    if (!active()) return;
    const auto reward=mission_reward(id);
    uintptr_t row=0;
    if (!read(widget,0x18,row)) return;
    const auto card=swf_child(row,"card");
    const auto text=swf_child(swf_child(card,"challengeDesc"),"txtVal",true);
    save::NativeString description{};
    if (text && reward.location_id && read(widget,0x2f4,description) && description.data &&
        description.length>=0 && description.length<2048) {
        const auto value=std::string(description.data,description.length)+"\nREWARD: "+reward.text;
        swf_set_text(text,value.c_str());
        reward_scroll(text);
    }
    if (const auto token=swf_child(card,"apNativeToken")) original_sprite_visibility(token,0,1);
}
void challenge_card_update(uintptr_t widget) {
    uint32_t id=0; read(widget,0x2b0,id);
    const auto challenge=challenge_projection(id);
    uint32_t found=0,required=0; uint8_t checked=0;
    if (!challenge.required || !read(widget,0x448,found) || !read(widget,0x44c,required) ||
        !read(widget,0x450,checked)) {
        original_challenge_card_update(widget);
        __try { present_card_reward(widget,id); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        return;
    }
    // Only the presenter's values are borrowed. Native records and celebration flags remain owned by the game.
    *reinterpret_cast<uint32_t*>(widget+0x448)=challenge.found;
    *reinterpret_cast<uint32_t*>(widget+0x44c)=challenge.required;
    *reinterpret_cast<uint8_t*>(widget+0x450)=challenge.checked!=0;
    __try { original_challenge_card_update(widget); }
    __finally {
        *reinterpret_cast<uint32_t*>(widget+0x448)=found;
        *reinterpret_cast<uint32_t*>(widget+0x44c)=required;
        *reinterpret_cast<uint8_t*>(widget+0x450)=checked;
    }
    uintptr_t row=0; read(widget,0x18,row);
    const auto card=swf_child(row,"card");
    const bool bound=swf_child(card,"challengeName") && swf_child(card,"challengeDesc");
    save::session().btrace.record(save::BStage::dossier_challenges,bound?save::BStatus::succeeded:save::BStatus::pending,
        bound?"dossier_native_card_bound":"dossier_native_card_pending",0,{{"slot",static_cast<uint64_t>(challenge_slot(challenge))},
        {"found",challenge.found},{"required",challenge.required},{"checked",challenge.checked}},widget);
    __try { present_card_reward(widget,id); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
bool challenge_icon(uintptr_t icon,uintptr_t material) {
    if (!icon || !material) return false;
    swf_set_material(icon,material,0);
    return true;
}
struct CompletionToast { uint32_t id=0; bool seen=false,checked=false; };
std::array<CompletionToast,3> completion_toasts{};
std::string completion_scope;
bool completion_edge(CompletionToast& toast,uint32_t id,bool checked) {
    if (!toast.seen || toast.id!=id) { toast={id,true,checked}; return false; }
    return checked && !toast.checked;
}
void present_physical_completion(uintptr_t widget,uint32_t id,const sc_physical_challenge& challenge) {
    const auto scope=save::session().namespace_id()+"/"+save::session().campaign_run.snapshot().map;
    if (completion_scope!=scope) { completion_scope=scope; completion_toasts={}; }
    const auto slot=challenge_slot(challenge);
    if (slot<0) return;
    auto& toast=completion_toasts[slot];
    if (!completion_edge(toast,id,challenge.checked!=0)) return;
    save::NativeString name{};
    const auto reward=mission_reward(id);
    if (!reward.location_id || !read(widget,0x1c4,name) || !name.data || name.length<=0 || name.length>=1024) return;
    const auto title=std::string("CHALLENGE COMPLETED: ")+std::string(name.data,name.length);
    if (special::present_challenge_completion(widget,title.c_str(),reward.text)) {
        toast.checked=true;
        save::session().btrace.record(save::BStage::hud_challenges,save::BStatus::succeeded,
            "physical_challenge_completion_presented",0,{{"id",id}},widget);
    }
}
void present_hud_challenge(uintptr_t widget) {
    uint32_t id=0; uintptr_t row=0;
    if (!read(widget,0x180,id) || !read(widget,0x18,row) || !row) return;
    const auto challenge=challenge_projection(id);
    if (!challenge.required) return;
    present_physical_completion(widget,id,challenge);
    uintptr_t normal=0; read(widget,0x310,normal);
    const bool graphic=challenge_icon(swf_child(row,"icon"),normal);
    const auto counter=swf_child(swf_child(row,"challengeCounter"),"txtVal",true);
    auto indicator=swf_child(row,"point");
    if (!indicator) indicator=swf_child(row,"progress");
    if (!counter || !indicator || !graphic) {
        save::session().btrace.record(save::BStage::hud_challenges,save::BStatus::pending,
            "hud_challenge_bindings_pending",0,{{"counter",counter!=0},{"indicator",indicator!=0},{"graphic",graphic}},widget);
        return;
    }
    const auto percent=challenge.found*100/challenge.required;
    // The native meter uses frame 100 for completion; pending 100% retains frame 99.
    swf_frame(indicator,challenge.checked?100:(percent<100?percent:99));
    char value[32]{};
    std::snprintf(value,sizeof(value),"%u/%u",challenge.found,challenge.required);
    swf_set_text(counter,value);
    *reinterpret_cast<uint8_t*>(counter+0x128)=1;
    if (const auto field=swf_child(swf_child(indicator,"txtPercent"),"txtVal",true)) {
        std::snprintf(value,sizeof(value),"%u%%",percent); swf_set_text(field,value);
    }
    save::session().btrace.record(save::BStage::hud_challenges,save::BStatus::succeeded,
        "hud_challenge_bound",0,{{"id",id},{"found",challenge.found},{"required",challenge.required},
        {"checked",challenge.checked}},widget);
}
void hud_challenge_update(uintptr_t widget) {
    original_hud_challenge_update(widget);
    if (!active()) return;
    __try { present_hud_challenge(widget); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::hud_challenges,save::BStatus::blocked,"hud_challenge_fault");
    }
}
void present_eol_challenge(uintptr_t widget) {
    uint32_t id=0; uintptr_t row=0;
    if (!read(widget,0x180,id) || !read(widget,0x18,row) || !row) return;
    const auto challenge=challenge_projection(id,true);
    if (!challenge.required) return;
    const auto category=swf_child(row,"categoryInfo");
    const auto values=swf_child(swf_child(row,"progress"),"valueText");
    const auto current=swf_child(swf_child(values,"current"),"txtVal",true);
    const auto total=swf_child(swf_child(values,"total"),"txtVal",true);
    if (!category || !current || !total) {
        save::session().btrace.record(save::BStage::end_challenges,save::BStatus::pending,
            "end_challenge_bindings_pending",0,{{"category",category!=0},{"current",current!=0},{"total",total!=0}},widget);
        return;
    }
    uintptr_t normal=0; read(widget,0x310,normal);
    if (!challenge_icon(swf_child(category,"icon"),normal)) {
        save::session().btrace.record(save::BStage::end_challenges,save::BStatus::pending,"end_challenge_graphic_pending",0,{},widget);
        return;
    }
    char value[32]{};
    std::snprintf(value,sizeof(value),"%u",challenge.found); swf_set_text(current,value);
    std::snprintf(value,sizeof(value),"/%u",challenge.required); swf_set_text(total,value);
    save::session().btrace.record(save::BStage::end_challenges,save::BStatus::succeeded,
        "end_challenge_bound",0,{{"id",id},{"found",challenge.found},{"required",challenge.required},
        {"checked",challenge.checked}},widget);
}
void eol_challenge_update(uintptr_t widget) {
    original_eol_challenge_update(widget);
    if (!active()) return;
    __try {
        present_eol_challenge(widget);
        uint32_t id=0; uintptr_t root=0;
        if (read(widget,0x180,id) && read(widget,0x18,root)) {
            const auto reward=mission_reward(id,true);
            const auto region=swf_child(root,"apReward");
            if (const auto text=swf_child(region,"txtVal",true)) {
                swf_set_text(text,reward.location_id ? reward.text : "");
                if (reward.location_id) reward_scroll(text);
            }
            if (region) original_sprite_visibility(region,reward.location_id!=0,1);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::end_challenges,save::BStatus::blocked,"end_challenge_fault");
    }
}
void present_hud_found(uintptr_t meter);
void hud_score_init(uintptr_t score) {
    original_hud_score_init(score);
    if (!active()) return;
    __try {
        uintptr_t meter=0;
        if (read(score,0xf8,meter) && meter) present_hud_found(meter);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void present_hud_found(uintptr_t meter) {
    uintptr_t root=0;
    if (!read(meter,0x18,root) || !root) return;
    const auto title=swf_child(swf_child(root,"apFoundLabel"),"txtVal",true);
    if (!title) return;
    const auto campaign=save::session().campaign_run.snapshot();
    const auto projection=menu().projection();
    sc_campaign_summary summary{};
    if (projection.namespace_id==save::session().namespace_id())
        for (uint32_t i=0;i<projection.count;++i)
            if (campaign.map==projection.rows[i].map && (projection.rows[i].flags&SC_CAMPAIGN_REVEALED))
                summary=projection.summaries[i];
    if (summary.known!=1) { original_sprite_visibility(root,0,1); return; }
    const auto point=swf_child(root,"pointCount");
    const auto count=swf_child(swf_child(point,"apFoundCounter"),"txtVal",true);
    if (!count) { original_sprite_visibility(root,0,1); return; }
    for (const auto leaf:{"background","corruption","glow_burst","header"})
        if (const auto sprite=swf_child(root,leaf)) original_sprite_visibility(sprite,0,1);
    for (const auto leaf:{"earned","total","apNativeSlash","iconBurst","icon"})
        if (const auto sprite=swf_child(point,leaf)) original_sprite_visibility(sprite,0,1);
    char value[32]; std::snprintf(value,sizeof(value),"%u/%u",summary.found,summary.total);
    swf_set_text(title,"ITEMS FOUND"); swf_set_text(count,value);
    original_sprite_visibility(root,1,1);
}
bool text_matches(uintptr_t field,const char* expected) {
    save::NativeString value{};
    if (!field || !read(field,0x40,value) || !value.data || value.length!=static_cast<int32_t>(std::strlen(expected))) return false;
    return std::memcmp(value.data,expected,static_cast<size_t>(value.length))==0;
}
bool present_found_counts(uintptr_t root,const sc_campaign_summary& summary,const char* label_name="header",bool nested_label=true) {
    uint32_t label_type=0,current_type=0,max_type=0;
    const auto header=swf_child(root,"header");
    const auto label_container=swf_child(header,label_name);
    const auto label=swf_child(nested_label ? swf_child(label_container,"txt") : label_container,"txtVal",true,&label_type);
    const auto current=swf_child(swf_child(swf_child(header,"headerValueCurrent"),"txt"),"txtVal",true,&current_type);
    const auto maximum=swf_child(swf_child(header,"headerValueMax"),"txtVal",true,&max_type);
    const auto stage=nested_label?save::BStage::mission_count_fields:save::BStage::end_count_fields;
    if (!header || !label || !current || !maximum) {
        save::session().btrace.record(stage,save::BStatus::pending,"count_fields_missing",shown.revision,
            {{"header",header!=0},{"label",label!=0},{"current",current!=0},{"maximum",maximum!=0},
             {"label_type",label_type},{"current_type",current_type},{"max_type",max_type}},root);
        return false;
    }
    if (summary.known!=1) { original_sprite_visibility(header,0,1); return false; }
    char value[24]{},max_value[24]{};
    std::snprintf(value,sizeof(value),"%u",summary.found);
    std::snprintf(max_value,sizeof(max_value),"/%u",summary.total);
    const unsigned before=(text_matches(label,"ITEMS FOUND")?1u:0u)|(text_matches(current,value)?2u:0u)|(text_matches(maximum,max_value)?4u:0u);
    static uintptr_t last_header=0;
    static uint64_t last_revision=0;
    if (nested_label && header==last_header && shown.revision==last_revision && before!=7)
        save::session().btrace.record(save::BStage::mission_count_rewrite,save::BStatus::entered,"native_text_rewrite_observed",shown.revision,
            {{"before_matches",before},{"found",summary.found},{"total",summary.total}},header);
    swf_set_text(label,"ITEMS FOUND");
    swf_set_text(current,value);
    swf_set_text(maximum,max_value);
    original_sprite_visibility(header,1,1);
    const bool label_ok=text_matches(label,"ITEMS FOUND"),current_ok=text_matches(current,value),max_ok=text_matches(maximum,max_value);
    const bool verified=label_ok && current_ok && max_ok;
    save::session().btrace.record(stage,verified?save::BStatus::succeeded:save::BStatus::pending,"count_text_readback",shown.revision,
        {{"label",label_ok},{"current",current_ok},{"maximum",max_ok},{"label_type",label_type},
         {"current_type",current_type},{"max_type",max_type},{"found",summary.found},{"total",summary.total}},header);
    if (nested_label && verified) { last_header=header; last_revision=shown.revision; }
    return verified;
}
void category_counts(uintptr_t category) {
    original_category_counts(category);
    __try {
        uintptr_t details=0,owned=0,root=0; int32_t index=-1;
        if (!shown_screen || !read(shown_screen,0x118,details) || !ap_details(details) ||
            !read(details,0x1b8,owned) || owned!=category || !read(category,0x18,root)) return;
        const auto list=list_for(shown_screen);
        if (list && read(list,0x150,index) && index>=0 && static_cast<uint32_t>(index)<shown.count &&
            (shown.rows[index].flags&SC_CAMPAIGN_DETAILS)) present_found_counts(root,shown.summaries[index]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::mission_counts,save::BStatus::blocked,"count_writer_fault");
    }
}
void present_end_items(uintptr_t screen) {
    if (!active()) return;
    const auto campaign=save::session().campaign_run.snapshot();
    const auto& map=campaign.map=="game/hub/hub" ? campaign.end_summary_map : campaign.map;
    if (map.empty()) return;
    const auto projection=menu().projection();
    const sc_campaign_summary* summary=nullptr;
    for (uint32_t i=0;i<projection.count;++i)
        if ((projection.rows[i].flags&SC_CAMPAIGN_DETAILS) && map==projection.rows[i].map) {
            summary=&projection.summaries[i]; break;
        }
    const sc_campaign_summary unknown{};
    if (!summary) summary=&unknown;
    uintptr_t category=0,vtable=0,root=0,screen_table=0,method=0;
    if (!read(screen,0x118,category) || !category || !read(category,0,vtable) ||
        vtable!=end_items_category_vtable || !read(category,0x18,root) || !root ||
        !read(screen,0,screen_table) || !read(screen_table,0x70,method) || !method) {
        save::session().btrace.record(save::BStage::end_counts,save::BStatus::pending,"end_counts_owner_pending",0,{},screen);
        return;
    }
    const auto screen_root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
    const bool bound=present_found_counts(root,*summary,"itemsFound",false);
    if (bound) {
        const auto main=swf_child(screen_root,"main");
        if (const auto item_list=swf_child(swf_child(main,"itemsFound"),"itemList"))
            original_sprite_visibility(item_list,0,1);
        if (const auto categories=swf_child(root,"itemsFound")) original_sprite_visibility(categories,0,1);
        original_sprite_visibility(root,1,1);
    }
    save::session().btrace.record(save::BStage::end_counts,bound?save::BStatus::succeeded:save::BStatus::pending,
        bound?"end_counts_bound":"end_counts_pending",projection.revision,
        {{"retained_map",!campaign.end_summary_map.empty()},{"found",summary->found},{"total",summary->total}},screen);
}
void end_items(uintptr_t screen) {
    original_end_items(screen);
    __try { present_end_items(screen); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "end_items_presentation_widget_fault",0,{},screen);
    }
}
void end_combat_init(uintptr_t screen) {
    original_end_combat_init(screen);
    if (!active()) return;
    __try {
        uintptr_t table=0,method=0;
        if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
        const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
        const auto earned=swf_child(swf_child(swf_child(root,"_center"),"demonicCorruption"),"earnedInfo");
        if (earned) original_sprite_visibility(earned,0,1);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "end_combat_points_widget_fault",0,{},screen);
    }
}
bool boss_points_scope(uintptr_t screen) {
    const auto map=save::session().campaign_run.snapshot().map;
    uint32_t count=0;
    return read(screen,0x120,count) && count==5 &&
        (map=="game/sp/e1m4_boss/e1m4_boss" || map=="game/sp/e2m4_boss/e2m4_boss" ||
         map=="game/sp/e3m3_maykr/e3m3_maykr");
}
void boss_update(uintptr_t screen,uintptr_t event) {
    original_boss_update(screen,event);
    if (!active() || !boss_points_scope(screen)) return;
    __try {
        uintptr_t table=0,method=0;
        if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
        const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
        const auto tier=swf_child(root,"tier1_5");
        const auto reward=swf_child(tier,"reward5count_c");
        const auto punch=swf_child(root,"tier2_connected");
        for (const auto leaf:{"info","description","icon_c","combat_icon","cta","bmp_hex_grid_b"})
            if (const auto sprite=swf_child(punch,leaf)) original_sprite_visibility(sprite,0,1);
        std::array<uintptr_t,40> artwork{}; unsigned leaves=0;
        // The tier and count card visibility drive native completion; only item artwork is hidden.
        for (unsigned i=1;i<=5;++i) {
            char name[]="item1_c"; name[4]+=static_cast<char>(i-1);
            const auto item=swf_child(reward,name);
            if (!item || !swf_child(item,"icon_g")) {
                save::session().btrace.record(save::BStage::boss_presentation,save::BStatus::pending,
                    "boss_points_art_unavailable",0,{{"item",i}},screen);
                return;
            }
            for (const char* leaf:{"completed","hex_bg_gold_b","bg_b","gradient_g","glow_b","fill","icon_g","pulse_b"})
                if (const auto child=swf_child(item,leaf)) artwork[leaves++]=child;
        }
        for (unsigned i=0;i<leaves;++i) original_sprite_visibility(artwork[i],0,1);
        save::session().btrace.record(save::BStage::boss_presentation,
            leaves?save::BStatus::succeeded:save::BStatus::pending,
            leaves?"boss_points_art_hidden":"boss_points_art_unavailable",0,{{"leaves",leaves}},screen);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "boss_reward_widget_fault",0,{},screen);
    }
}
void present_details(uintptr_t details) {
    const auto list=list_for(shown_screen);
    int32_t index=-1;
    if (!list || !read(list,0x150,index) || index<0 || static_cast<uint32_t>(index)>=shown.count ||
        !(shown.rows[index].flags&SC_CAMPAIGN_DETAILS)) {
        save::session().btrace.record(save::BStage::mission_details,save::BStatus::blocked,
                                      "details_focus_unavailable",0,{{"index",static_cast<uint64_t>(index)},
                                                                       {"rows",shown.count}}); return;
    }
    const auto& summary=shown.summaries[index];
    if (!summary.known) {
        save::session().btrace.record(save::BStage::mission_details,save::BStatus::blocked,
                                      "details_summary_unknown",0,{{"index",static_cast<uint64_t>(index)}}); return;
    }
    uintptr_t details_root=0;
    if (!read(details,0x18,details_root) || !details_root) {
        save::session().btrace.record(save::BStage::mission_details,save::BStatus::blocked,
                                      "details_root_unavailable",0,{{"index",static_cast<uint64_t>(index)}}); return;
    }
    if (const auto battery=swf_child(details_root,"batteries"))
        original_sprite_visibility(battery,0,1);
    const auto challenge_rows=swf_child(swf_child(details_root,"challenges"),"list");
    for (unsigned slot=0;slot<3;++slot) {
        char name[]="item0"; name[4]+=static_cast<char>(slot);
        const auto region=swf_child(swf_child(challenge_rows,name),"apReward");
        sc_campaign_reward placement{};
        uint32_t id=0;
        if (shown.namespace_id==save::session().namespace_id() && read(details,0x1d0+slot*0x1b0,id))
            for (const auto& reward:shown.rewards[index]) if (reward.kind==SC_REWARD_MISSION) {
                uint32_t key=0;
                for (const unsigned char* c=reinterpret_cast<const unsigned char*>(reward.unlockable);*c;++c) key=31*key+*c;
                if (key==id) placement=reward;
            }
        if (const auto text=swf_child(region,"txtVal",true)) {
            swf_set_text(text,placement.location_id ? placement.text : "");
            if (placement.location_id) reward_scroll(text);
        }
        if (region) original_sprite_visibility(region,placement.location_id!=0,1);
    }
    if (const auto completion=swf_child(details_root,"completionInfo"))
        original_sprite_visibility(completion,0,1);
    uintptr_t category=0,root=0,vtable=0;
    if (read(details,0x1b8,category) && category && read(category,0,vtable) &&
        vtable==end_items_category_vtable) read(category,0x18,root);
    const bool counts=present_found_counts(root,summary);
    save::session().btrace.record(save::BStage::mission_counts,
        counts?save::BStatus::succeeded:save::BStatus::pending,
        counts?"mission_counts_bound":"mission_counts_pending",shown.revision,
        {{"index",static_cast<uint64_t>(index)},{"known",summary.known},{"root",root!=0},{"found",summary.found},{"total",summary.total}},details);

    const auto icon=swf_child(details_root,"apDifficulty");
    const auto tier=summary.band&255;
    static constexpr const char* donor[]={"",
        "swf/main_menu/screens/mission_select_textures/swf_images/difficulty/Too_Young_",
        "swf/main_menu/screens/mission_select_textures/swf_images/difficulty/Hurt_Me_Plenty_",
        "swf/main_menu/screens/mission_select_textures/swf_images/difficulty/Ultra_Violence_",
        "swf/main_menu/screens/mission_select_textures/swf_images/difficulty/Nightmare_"};
    const auto material=tier>=1 && tier<=4 ? find_material(material_manager,donor[tier],1) : 0;
    const bool rating_bound=icon && material;
    if (rating_bound) {
        swf_frame(icon,tier);
        swf_set_material(icon,material,0);
    }
    if (icon) original_sprite_visibility(icon,rating_bound,1);
    if (const auto item_list=swf_child(root,"itemsFound")) original_sprite_visibility(item_list,0,1);
    if (root) original_sprite_visibility(root,counts,1);
    uintptr_t applied_material=0;
    read(icon,0x60,applied_material);
    uint16_t applied_width=0,applied_height=0;
    const bool sized=read(icon,0x68,applied_width) && read(icon,0x6a,applied_height) &&
        applied_width==150 && applied_height==150;
    const bool applied=rating_bound && applied_material==material && sized;
    save::session().btrace.record(save::BStage::mission_rating,
        applied?save::BStatus::succeeded:save::BStatus::pending,
        applied?"preview_skull_bound":"preview_skull_pending",shown.revision,
        {{"index",static_cast<uint64_t>(index)},{"tier",tier},{"icon",icon!=0},
          {"material",material!=0},{"readback",material!=0 && applied_material==material},
          {"sized",sized}},details);
    const auto challenge_root=swf_child(swf_child(details_root,"challenges"),"list");
    if (summary.known!=1) return;
    unsigned requested=0,bound=0;
    for (const auto& challenge:summary.challenges) {
        const auto slot=challenge_slot(challenge);
        if (slot<0) continue;
        ++requested;
        uint32_t present=0; char title=0;
        if (!read(details,0x1d0+slot*0x1b0,present) || !present ||
            !read(details,0x1d4+slot*0x1b0,title) || !title) continue;
        char row_name[]="item0"; row_name[4]+=static_cast<char>(slot);
        const auto row=swf_child(challenge_root,row_name);
        const auto field=swf_child(swf_child(swf_child(row,"progress"),"txt"),"txtVal",true);
        if (!field) continue;
        char progress[32]{};
        std::snprintf(progress,sizeof(progress),"%u/%u",challenge.found,challenge.required);
        swf_set_text(field,progress);
        uintptr_t normal=0; read(details,0x1d0+slot*0x1b0+0x190,normal);
        if (!challenge_icon(swf_child(row,"icon"),normal)) continue;
        ++bound;
    }
    save::session().btrace.record(save::BStage::mission_challenges,bound==requested?save::BStatus::succeeded:save::BStatus::pending,
        bound==requested?"mission_challenges_bound":"mission_challenges_pending",shown.revision,
        {{"requested",requested},{"bound",bound}},details);
}
bool is_ap_meter(uintptr_t meter) {
    if (!meter) return false;
    if (current_ap_details) return true;
    return meter == ap_meter_published.load(std::memory_order_acquire);
}
void meter_allocate_detour(uintptr_t meter,uintptr_t encounters,uintptr_t decl) {
    if (is_ap_meter(meter)) return;
    if (original_meter_allocate) original_meter_allocate(meter,encounters,decl);
}
void meter_update_detour(uintptr_t meter) {
    meter_update_ticks.fetch_add(1,std::memory_order_relaxed);
    if (is_ap_meter(meter)) {
        meter_update_suppressed.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    meter_update_delegated.fetch_add(1,std::memory_order_relaxed);
    if (original_meter_update) original_meter_update(meter);
    if (active()) {
        __try { present_hud_found(meter); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}
void sprite_visibility_detour(uintptr_t sprite,uint32_t visible,uint32_t flag) {
    if (!sprite) return;
    if (current_ap_details) {
        uintptr_t meter=0, meter_sprite=0;
        read(current_ap_details,0x6f8,meter);
        if (meter) read(meter,0x18,meter_sprite);
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool is_meter=(caller==meter_visibility_return) || (meter_sprite && sprite==meter_sprite);
        if (is_meter) {
            if (original_sprite_visibility) original_sprite_visibility(sprite,0,flag);
            return;
        }
    }
    if (original_sprite_visibility) original_sprite_visibility(sprite,visible,flag);
}
void render_details(uintptr_t details) {
    if (!ap_details(details)) {
        if (active() && shown_screen)
            save::session().btrace.record(save::BStage::mission_details,save::BStatus::blocked,
                                          "details_owner_mismatch");
        retire_ap_meter();
        details_update(details); return;
    }
    uintptr_t meter=0;
    if (read(details,0x6f8,meter) && meter) {
        ap_meter_published.store(meter,std::memory_order_release);
    }
    current_ap_details=details;
    __try { details_update(details); }
    __finally { current_ap_details=0; }
    __try { present_details(details); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "campaign_presentation_widget_fault",0,{},details);
    }
    uintptr_t sprite=0;
    if (meter && read(meter,0x18,sprite) && sprite) {
        if (original_sprite_visibility) original_sprite_visibility(sprite,0,1);
    }
}
void hide_details(uintptr_t screen) {
    uintptr_t details=0;
    if (!read(screen,0x118,details) || !details) return;
    string_assign(details+0x180,"");
    *reinterpret_cast<uintptr_t*>(details+0x1b0)=0;
    // The native details owner hides its entire SWF sprite for an empty map.
    render_details(details);
}
void focus(uintptr_t screen) {
    if (!active()) { original_focus(screen); return; }
    const auto list=list_for(screen);
    int32_t index=-1,count=0; uintptr_t rows=0;
    if (building || screen!=shown_screen || !read(list,0x150,index) || index<0 ||
        static_cast<uint32_t>(index)>=shown.count || !(shown.rows[index].flags&SC_CAMPAIGN_DETAILS) ||
        !read(screen,0x128,count) || index>=count || !read(screen,0x120,rows) || !rows) {
        hide_details(screen); return;
    }
    // Populate deliberately includes every row without inventing completion.
    // Read real native statistics only when an unlocked row is focused.
    native_completed(screen,reinterpret_cast<uintptr_t>(&entries[index]));
    const auto row=rows+static_cast<size_t>(index)*0x550;
    list_assign(row+8,screen+0x328);
    std::memcpy(reinterpret_cast<void*>(row+0x20),reinterpret_cast<void*>(screen+0x340),0x510);
    std::memcpy(reinterpret_cast<void*>(row+0x530),reinterpret_cast<void*>(screen+0x850),0x20);
    original_focus(screen);
}
void populate_owned(uintptr_t screen,uintptr_t list,uintptr_t campaign,uintptr_t previous) {
    building=true;
    *reinterpret_cast<uintptr_t*>(campaign+0x1a8)=reinterpret_cast<uintptr_t>(&map_list);
    __try { original_populate(screen,list); }
    __finally { *reinterpret_cast<uintptr_t*>(campaign+0x1a8)=previous; building=false; }
}
void populate(uintptr_t screen,uintptr_t list) {
    if (!active()) { original_populate(screen,list); return; }
    if (!save::session().accepts_requests()) { hide_details(screen); return; }
    const auto projection=menu().projection();
    uintptr_t campaign=0,previous=0,pending=0;
    if (!read(screen,0x100,campaign) || !campaign || !read(campaign,0x1a8,previous) ||
        !read(screen,0x870,pending)) { fault("native_campaign_list_unreadable"); return; }
    // LaunchMission retains the entry through its owned save continuation.
    // Never move/overwrite a row while that native continuation owns it.
    if (pending) return;
    if (!initialized) {
        for (auto& entry:entries) string_init(reinterpret_cast<uintptr_t>(&entry.name));
        initialized=true;
    }
    uintptr_t source_entries=0; int32_t source_count=0;
    if (!read(previous,0x88,source_entries) || !read(previous,0x90,source_count) || source_count<1) {
        fault("native_campaign_source_roster_unreadable"); return;
    }
    for (uint32_t i=0;i<projection.count;++i) {
        entries[i].dependencies={};
        const auto& row=projection.rows[i];
        if (row.flags&SC_CAMPAIGN_REVEALED) {
            uintptr_t source=0; save::NativeString name{}; char map[192]{};
            if (row.native_index>=static_cast<uint32_t>(source_count) ||
                !engine::add(source_entries,static_cast<size_t>(row.native_index)*sizeof(Entry),sizeof(Entry),source) ||
                !read(source,0,name) || name.length<1 || name.length>=192 ||
                memory.copy(reinterpret_cast<uintptr_t>(name.data),map,static_cast<size_t>(name.length)+1).reason ||
                map[name.length] || std::strcmp(map,row.map) || !read(source,0x30,entries[i].dependencies)) {
                fault("native_campaign_source_entry_mismatch"); return;
            }
        }
        // Borrow the native DECL's persistent active-layer list. LaunchMission
        // copies it into its own request; no AP policy or guessed layer names.
        string_assign(reinterpret_cast<uintptr_t>(&entries[i].name),projection.rows[i].map);
    }
    map_list.entries.data=reinterpret_cast<uintptr_t>(entries.data());
    map_list.entries.count=static_cast<int32_t>(projection.count);
    map_list.entries.capacity=static_cast<int32_t>(entries.size());
    shown=projection; shown_screen=screen;
    uintptr_t details=0,meter=0;
    if (read(screen,0x118,details) && details && read(details,0x6f8,meter) && meter) {
        ap_meter_published.store(meter,std::memory_order_release);
    }
    populate_owned(screen,list,campaign,previous);
    uintptr_t widgets=0; int32_t count=0;
    if (!read(list,0xa0,widgets) || !read(list,0xa8,count) || count!=static_cast<int32_t>(shown.count)) {
        fault("native_campaign_widget_count_mismatch"); hide_details(screen); return;
    }
    for (uint32_t i=0;i<shown.count;++i) {
        uintptr_t widget=0;
        if (!read(widgets,static_cast<size_t>(i)*8,widget) || !widget) {
            fault("native_campaign_widget_missing"); return;
        }
        std::string label=shown.rows[i].title;
        if (shown.rows[i].flags&SC_CAMPAIGN_GOAL) label+=" / GOAL";
        if (!(shown.rows[i].flags&SC_CAMPAIGN_UNLOCKED)) label+=" / LOCKED";
        else if (shown.rows[i].flags&SC_CAMPAIGN_COMPLETED) label+=" / COMPLETED";
        string_assign(widget+0x188,label.c_str());
        if (!(shown.rows[i].flags&SC_CAMPAIGN_UNLOCKED)) widget_state(widget,5);
    }
    focus(screen);
}
void load(uintptr_t screen,int index) {
    if (!active()) { original_load(screen,index); return; }
    auto& session=save::session();
    const bool row_valid=index>=0 && static_cast<uint32_t>(index)<shown.count;
    const bool unlocked=row_valid && (shown.rows[index].flags&SC_CAMPAIGN_UNLOCKED);
    if (!session.accepts_requests() || screen!=shown_screen || !unlocked) {
        session.btrace.record(save::BStage::checkpoint_factory,save::BStatus::blocked,"mission_load_row_blocked",0,
            {{"accepting",session.accepts_requests()},{"screen_equal",screen==shown_screen},
             {"index",static_cast<uint64_t>(index)},{"row_valid",row_valid},{"unlocked",unlocked}},screen);
        return;
    }
    // Permission/save/checkpoint/loading remain in LoadMission/LaunchMission.
    uintptr_t pending=0;
    const auto boundary=native::checkpoint_transition();
    if (!read(screen,0x870,pending) || pending) {
        session.btrace.record(save::BStage::checkpoint_factory,save::BStatus::blocked,"mission_native_entry_pending",0,
            {{"pending_entry",pending!=0}},screen);
        return;
    }
    session.campaign_run.cancel_menu_save();
    if (!session.campaign_run.prepare_menu_save(boundary)) {
        session.btrace.record(save::BStage::checkpoint_factory,save::BStatus::blocked,"mission_presave_unavailable",0,
            {{"generation",boundary.generation_after},{"observed",boundary.observed},
             {"observation_reason",boundary.observation_reason},{"depth",boundary.depth}},screen);
        return;
    }
    bool completion_known=false,completed=false;
    __try { completed=native_completed(screen,reinterpret_cast<uintptr_t>(&entries[index])); completion_known=true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    const auto& namespace_id=session.namespace_id();
    fast_travel::entry_policy().selected(namespace_id.c_str(),shown.rows[index].map,
        boundary.generation_after,completion_known,completed);
    menu().selected(shown.rows[index].id);
    session.btrace.record(save::BStage::checkpoint_factory,save::BStatus::entered,"mission_native_load_invoked",0,
        {{"index",static_cast<uint64_t>(index)},{"row_id",shown.rows[index].id},
         {"generation",boundary.generation_after}},screen);
    original_load(screen,index);
    session.btrace.record(save::BStage::checkpoint_factory,save::BStatus::succeeded,"mission_native_load_returned",0,
        {{"index",static_cast<uint64_t>(index)},{"row_id",shown.rows[index].id}},screen);
}
bool is_available(uintptr_t screen) {
    if (!active()) return original_available(screen);
    return save::session().accepts_requests() && menu().projection().count!=0;
}
void update(uintptr_t screen) {
    int32_t before=0;
    read(screen,0x108,before);
    bool rebuild=before==-1;
    if (active() && screen==shown_screen && shown.revision!=menu().projection().revision) {
        uintptr_t pending=0; int32_t state=-1;
        if (read(screen,0x870,pending) && !pending && read(screen,0x108,state) && state==0) {
            *reinterpret_cast<int32_t*>(screen+0x108)=-1; // Native Update owns clear/repopulate/focus.
            rebuild=true;
        }
    }
    original_update(screen);
    if (active() && rebuild && screen==shown_screen) {
        const auto wanted=menu().focus_id();
        const auto list=list_for(screen);
        uintptr_t table=0,method=0;
        if (list && read(list,0,table) && read(table,0x68,method) && method)
            for (uint32_t i=0;i<shown.count;++i) if (shown.rows[i].id==wanted) {
                reinterpret_cast<void(*)(uintptr_t,int,int)>(method)(list,static_cast<int>(i),0); break;
            }
    }
    int32_t state=-1;
    if (active() && save::session().accepts_requests() && screen==shown_screen &&
        read(screen,0x108,state) && state==0) menu().rendered(shown.revision);
}
}
bool available() { return ready.load(std::memory_order_acquire); }
void reward_scroll(uintptr_t text) {
    // Native TextField.mode stores SWF_TEXT_RENDER_AUTOSCROLL (4) at +0x174.
    uint32_t mode=0;
    if (text && read(text,0x174,mode) && mode<=5) *reinterpret_cast<uint32_t*>(text+0x174)=4;
}
sc_campaign_reward mastery_reward(const char* perk) {
    if (!active() || !perk) return {};
    const auto projection=menu().projection();
    if (projection.namespace_id!=save::session().namespace_id()) return {};
    for (uint32_t i=0;i<projection.count;++i) {
        if (!(projection.rows[i].flags&SC_CAMPAIGN_HUB)) continue;
        for (const auto& reward:projection.rewards[i])
            if (reward.kind==SC_REWARD_MASTERY && !std::strcmp(reward.unlockable,perk)) return reward;
    }
    return {};
}
MeterDiagnostics meter_diagnostics() {
    MeterDiagnostics d{};
    d.update_ticks=meter_update_ticks.load(std::memory_order_relaxed);
    d.update_suppressed=meter_update_suppressed.load(std::memory_order_relaxed);
    d.update_delegated=meter_update_delegated.load(std::memory_order_relaxed);
    d.published_ptr=ap_meter_published.load(std::memory_order_acquire);
    d.retire_count=ap_meter_retire_count.load(std::memory_order_relaxed);
    return d;
}
bool request_map(uintptr_t request,const std::string& map) {
    if (!available() || map.empty() || map.size()>=192) return false;
    string_assign(request+0x30,map.c_str());
    save::NativeString value{};
    char bytes[192]{};
    return read(request,0x30,value) && value.length==static_cast<int32_t>(map.size()) &&
        !memory.copy(reinterpret_cast<uintptr_t>(value.data),bytes,map.size()+1).reason &&
        !std::strcmp(bytes,map.c_str());
}
bool mission_request(uintptr_t request,std::string& destination) {
    int32_t subtype=0;
    if (!read(request,0x98,subtype)) return false;
    if (subtype!=2) return true; // Ordinary Continue keeps its saved destination.
    uintptr_t entry=0;
    save::NativeString name{};
    char map[192]{};
    if (!read(request,0xa8,entry) || !read(request,0x30,name) || name.length<1 || name.length>=192 ||
        memory.copy(reinterpret_cast<uintptr_t>(name.data),map,size_t(name.length)+1).reason || map[name.length]) return false;
    const auto selected=menu().focus_id();
    for (uint32_t i=0;i<shown.count;++i) {
        if (entry==reinterpret_cast<uintptr_t>(&entries[i]) && shown.rows[i].id==selected &&
            (shown.rows[i].flags&SC_CAMPAIGN_UNLOCKED) && !std::strcmp(map,shown.rows[i].map)) {
            destination=map; return true;
        }
    }
    return false;
}
void present_campaign_actions(uintptr_t screen,bool entered) {
    if (!available() || !active() || !save::session().accepts_requests()) return;
    int32_t state=-1;
    if (!read(screen,0x108,state) || state!=2) return;
    const auto snapshot=save::session().campaign_run.snapshot();
    safe_present_save_preview(screen,snapshot);
    if (!snapshot.checkpoint) return;
    const auto list=list_for(screen);
    uintptr_t resume=0,choose=0,children=0,table=0,select=0;
    int32_t count=0,index=-1,choose_state=0;
    if (!read(screen,0x118,resume) || !resume || !read(screen,0x120,choose) || !choose ||
        !read(list,0xa0,children) || !read(list,0xa8,count) || !read(list,0x150,index)) return;
    bool contains_choose=false;
    uintptr_t focused=0;
    for (int32_t i=0;i<count;++i) {
        uintptr_t child=0; if (!read(children,size_t(i)*8,child)) return;
        contains_choose|=child==choose;
        if (i==index) focused=child;
    }
    if (!contains_choose) return; // Retain native action membership and permissions.
    const bool hub=snapshot.map=="game/hub/hub";
    const auto label=[&](uintptr_t widget,const char* wanted) {
        save::NativeString current{}; char text[96]{};
        if (read(widget,0x188,current) && current.length>=0 && current.length<96 && current.data &&
            !memory.copy(reinterpret_cast<uintptr_t>(current.data),text,size_t(current.length)+1).reason &&
            !std::strcmp(text,wanted)) return;
        string_assign(widget+0x188,wanted);
    };
    label(resume,hub?"RETURN TO FORTRESS":"RESUME CHECKPOINT");
    label(choose,"CHOOSE MISSION");
    const bool projected=menu().projection().count!=0;
    if (!read(choose,0x154,choose_state)) return;
    const bool became_available=projected && choose_state==5;
    if (became_available) widget_state(choose,focused==choose?3:1);
    if (projected && hub && (entered || (became_available && focused==resume)) &&
        read(list,0,table) && read(table,0x70,select) && select)
        reinterpret_cast<void(*)(uintptr_t,uintptr_t)>(select)(list,choose);
}
#ifdef SC_NATIVE_TESTING
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
void test_calls(const NativeCalls& c) {
    original_populate=c.populate; original_focus=c.focus; original_load=c.load; original_available=c.available;
    original_update=c.update; string_init=c.string_init; string_assign=c.string_assign;
    native_completed=c.completed; list_assign=c.list_assign; details_update=c.details_update; widget_state=c.widget_state;
    original_root_navigation=c.root_navigation; original_root_campaign=c.root_campaign;
    campaign_definitions=c.campaign_definitions;
    sprite_changed=c.sprite_changed; root_layout={};
    original_meter_allocate=c.meter_allocate;
    original_sprite_visibility=c.sprite_visibility;
    original_meter_update=c.meter_update;
    ready.store(true,std::memory_order_release);
}
void test_populate(uintptr_t screen,uintptr_t list) { populate(screen,list); }
void test_focus(uintptr_t screen) { focus(screen); }
void test_details_update(uintptr_t details) { render_details(details); }
void test_load(uintptr_t screen,int index) { load(screen,index); }
void test_update(uintptr_t screen) { update(screen); }
void test_root_navigation(uintptr_t screen,uint8_t reset) { root_navigation(screen,reset); }
void test_root_campaign(uintptr_t screen,uintptr_t declaration) { root_campaign(screen,declaration); }
void test_meter_allocate(uintptr_t meter,uintptr_t encounters,uintptr_t decl) { meter_allocate_detour(meter,encounters,decl); }
void test_sprite_visibility(uintptr_t sprite,uint32_t visible,uint32_t flag) {
    sprite_visibility_detour(sprite,visible,flag);
}
void test_meter_update(uintptr_t meter) { meter_update_detour(meter); }
#endif
#include "physical_contact_observer.h"

std::array<native::Target,43> native_targets(uintptr_t base) {
    constexpr uint32_t rvas[]={0x10d2c00,0x10d42f0,0x10d27b0,0x10d1c50,0x10d3140,
        0x3fa8e0,0x3faff0,0x10d1cf0,0x43c070,0x1116c50,0x159c280,
        0x10e08f0,0x10dc420,0x18071d0,0x1857110,0x0f9bd70,0x1864430,
        0x0f9c000,0x185c150,0x184e470,0x184e4b0,0x184e3b0,0x186db00,
        0x17a9660,0x1d69e00,0x360bb0,0x1863b00,0x15b2680,0x0effea0,0x0f5cfe0,0x0f59270,
        0x0f489c0,0x0f4f990,0x17aa5d0,0x10ef3e0,0x0f30810,0x1859e20,0x1865280,0xf98fc0,0xf62bc0,0x1863c30,0xfaa270,0xd9d010};
    constexpr const char* bytes[]={
        "4053565741554881ec98000000488b05c4bd0d034833c4488944247033db488d",
        "40574883ec60488b05dba60d034833c44889442450488bf9488d4c2420e8ce65",
        "40534881ecf0010000488b0518c20d034833c448898424e00100004c8b811001",
        "48895c240848896c24104889742418574883ec20488b0505c55703488be98378",
        "48895c2410564883ec208bb108010000488bd98b810c0100003bf00f84f90000",
        "488d0591cb6602c7411414000080488901488d411848894108c7411000000000",
        "48895c2410488974241848897c242041564883ec304c8bf2488bd94885d20f85",
        "48895c2418488974242057b890400000e89ba07901482be0488b05c9cc0d0348",
        "48895c2408574883ec20488bfa488bd9483bca747e8b410c39420c74370fb641",
        "405557488d6c24c84881ec38010000488b05727d09034833c448894520488bf9",
        "488b01448bc28b91540100004489815401000048ffa0c0000000cccccccccccc",
        "48895c2418554883ec200fb6ea488bd984d2751081b9bc010000bf0300000f84",
        "405541564157488dac24e0fdffff4881ec20030000488b059c250d034833c448",
        "40534883ec20488b05c308ec024885c07570e879b6b5fe83f803742285c0741e",
        "4c8bdc574883ec70488b05b97895024833c448894424584863410c488bf983f8",
        "48895c24184889742420574883ec20488b829801000033db488bfa488981c001",
        "440fb6d23851517457807952007551488b41104c6349088851514d03c9488b10",
        "405553488dac2408c0ffffb8f8400000e88bfd8c01482be0488b05b929210348",
        "40534883ec20488b4928488bda488b01ff5028488bc34883c4205bc3cccccccc",
        "40534883ec20833908752b488b59084885db74224c8b03488bcb488b150f5c06",
        "40534883ec20833908752b488b59084885db74224c8b03488bcb488b15d75b06",
        "4883ec288b0183f8027527488b4908b8fffffffff00fc1413083f80175544885",
        "40534883ec20488bd94883c140e8ded4b8fe488bcb4883c4205be9e1cbffffcc",
        "488d0549a1f002c3cccccccccccccccc488d05e9acf002c3cccccccccccccccc",
        "4055535657415441554156488d6c24804881ec80010000488b05ba4b44024833",
        "4883ec58488b150d10f103488d0526c76e0248894424604c8d4c24208b01488d",
        "48895c24084889742410574883ec20488bd9418bf0488b4960488bfa483bca0f",
        "48895c2408574883ec40486381880100004533db488bf985c07e28488b898001",
        "40534883ec300fb791d00000004c8d0d2cffffff488bd948894c2420488b0d45",
        "48895c240848896c2410488974241848897c242041564883ec20488bf9e8ce00",
        "48895c241048896c2418488974242057b850400000e8162b9101482be0488b05",
        "80b98102000000750e488b8928010000488b0148ff6020c3cccccccccccccccc",
        "405553488dac2478c0ffffb888400000e8fbc39101482be0488b0529f0250348",
        "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448",
        "40534883ec20488bd9e8e2dc0d00488b8bd8000000488d1524d09501e81fc30d",
        "48895c241848896c242057b870400000e87bb59301482be0488b05a9e1270348",
        "48895c2420555641564883ec60488b05a44b95024833c448894424580fb60245",
        "48895c2408574883ec200fb74158bf010000003bd7488bd90f4ffa3bf8742c7d",
        "40555341544157488dac2448c0ffffb8b8400000e8c72d8d01482be0488b05f5",
        "40555356574154488bec4881ec800000004533e4488bf9418bf4448965304839",
        "48895c240848896c24104889742418574883ec20488bd9418bf1488b4960418b",
        "4053b8a0400000e8241b8c01482be0488b05524720034833c448898424804000",
        "40534883ec20488b01488bd9ff90480d000084c00f8582000000488b03488bcb"};
    std::array<native::Target,43> targets{};
    const auto digit=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
    for (unsigned i=0;i<targets.size();++i) {
        targets[i].address=base+rvas[i];
        for (size_t n=0;n<32;++n) targets[i].bytes[n]=static_cast<uint8_t>(digit(bytes[i][2*n])*16+digit(bytes[i][2*n+1]));
    }
    // idList template instances share this prologue. Use the existing secondary
    // signature contract: unique bytes inside the same exact unwind owner.
    constexpr char list_copy_signature[]="d27413488b0dee5ae30341b810000000488b01ff503848c70300000000c7430c";
    targets[8].signature_offset=48;
    for (size_t n=0;n<32;++n) targets[8].signature[n]=static_cast<uint8_t>(digit(list_copy_signature[2*n])*16+digit(list_copy_signature[2*n+1]));
    return targets;
}
bool validate_native_targets(save::Installation& record,engine::Memory& source_memory,
                              const engine::Image& image,HANDLE stop,const std::array<native::Target,43>& targets) {
    if (!image.contains(0x5e05200,sizeof(uintptr_t),IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_EXECUTE)) return false;
    // Populate's actual CALL owns this list-copy contract. Record failures here
    // as well as in the target validator so startup refusal remains actionable.
    auto call_event=record.begin(SC_INSTALL_SAVE_TARGET,6,8,0x10d2d99);
    native::ValidationDetail detail;
    if (!native::function_window(source_memory,image,image.base+0x10d2c00,image.base+0x10d2d99,5,&detail)) {
        if (detail.read_attempted) save::attach_read(call_event,detail.read);
        record.finish(call_event,SC_NATIVE_TARGET_BOUNDARY); return false;
    }
    std::array<uint8_t,5> call{};
    const auto read_result=source_memory.copy(image.base+0x10d2d99,call.data(),call.size());
    save::attach_read(call_event,read_result); call_event.byte_count=5;
    call_event.expected_bytes[0]=0xe8;
    const auto expected_displacement=static_cast<int32_t>(targets[8].address-(image.base+0x10d2d9e));
    std::memcpy(call_event.expected_bytes+1,&expected_displacement,sizeof(expected_displacement));
    std::memcpy(call_event.actual_bytes,call.data(),call.size());
    const bool call_matches=!read_result.reason && !std::memcmp(call_event.expected_bytes,call.data(),call.size());
    record.finish(call_event,call_matches ? SC_NATIVE_NONE : SC_NATIVE_TARGET_BYTES);
    if (!call_matches) return false;
    for (unsigned i=0;i<targets.size();++i) {
        const auto rva=static_cast<uint32_t>(targets[i].address-image.base);
        if (i==5 || i==10 || i==16 || i==23 || i==31) {
            // These leaf callees have no unwind entry and are not detoured.
            std::array<uint8_t,32> actual{};
            if (!image.contains(rva,actual.size(),IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ,0) ||
                source_memory.copy(targets[i].address,actual.data(),actual.size()).reason || actual!=targets[i].bytes) return false;
        } else if (native::validate_recorded(record,source_memory,image,targets[i],stop,GetTickCount64()+3000,6,i)) return false;
    }
    return true;
}
bool install(const engine::Binding& binding,HANDLE stop) {
    if (!save::session().campaign_run.enabled()) return true;
    const auto targets=native_targets(binding.image.base);
    meter_visibility_return=binding.image.base+0x1116e07;
    void* detours[]={reinterpret_cast<void*>(populate),reinterpret_cast<void*>(focus),reinterpret_cast<void*>(load),
        reinterpret_cast<void*>(is_available),reinterpret_cast<void*>(update),
        reinterpret_cast<void*>(root_navigation),reinterpret_cast<void*>(root_campaign),
        reinterpret_cast<void*>(render_details),
        reinterpret_cast<void*>(meter_allocate_detour),
        reinterpret_cast<void*>(sprite_visibility_detour),
        reinterpret_cast<void*>(meter_update_detour),reinterpret_cast<void*>(userinfo_point),
        reinterpret_cast<void*>(hud_score_init),reinterpret_cast<void*>(end_items),reinterpret_cast<void*>(end_combat_init),
        reinterpret_cast<void*>(dossier_map),reinterpret_cast<void*>(challenge_card_update),
        reinterpret_cast<void*>(start_show),reinterpret_cast<void*>(boss_update),
        reinterpret_cast<void*>(hud_challenge_update),reinterpret_cast<void*>(eol_challenge_update),reinterpret_cast<void*>(category_counts),
        reinterpret_cast<void*>(trigger_gate)};
    constexpr unsigned hooked[]={0,1,2,3,4,11,12,9,15,16,17,27,28,29,30,31,32,34,35,38,39,41,42};
    void* originals[23]{};
    for (unsigned i=0;i<23;++i) if (save::session().installation.hook(SC_INSTALL_SAVE_CREATE,6,hooked[i],static_cast<uint32_t>(targets[hooked[i]].address-binding.image.base),[&] {
        return MH_CreateHook(reinterpret_cast<void*>(targets[hooked[i]].address),detours[i],&originals[i]); })!=MH_OK) return false;
    original_populate=reinterpret_cast<Populate>(originals[0]); original_focus=reinterpret_cast<Update>(originals[1]);
    original_load=reinterpret_cast<Load>(originals[2]); original_available=reinterpret_cast<Available>(originals[3]);
    original_update=reinterpret_cast<Update>(originals[4]);
    original_root_navigation=reinterpret_cast<RootNavigation>(originals[5]);
    original_root_campaign=reinterpret_cast<Populate>(originals[6]);
    campaign_definitions=reinterpret_cast<CampaignDefinitions>(targets[13].address);
    sprite_changed=reinterpret_cast<Update>(targets[14].address);
    string_init=reinterpret_cast<StringInit>(targets[5].address); string_assign=reinterpret_cast<StringAssign>(targets[6].address);
    native_completed=reinterpret_cast<Completed>(targets[7].address); list_assign=reinterpret_cast<ListAssign>(targets[8].address);
    details_update=reinterpret_cast<Update>(originals[7]);
    widget_state=reinterpret_cast<WidgetState>(targets[10].address);
    original_meter_allocate=reinterpret_cast<MeterAllocate>(originals[8]);
    original_sprite_visibility=reinterpret_cast<SpriteVisibility>(originals[9]);
    swf_lookup=reinterpret_cast<SwfLookup>(targets[18].address);
    swf_sprite=reinterpret_cast<SwfGet>(targets[19].address);
    swf_text=reinterpret_cast<SwfGet>(targets[20].address);
    swf_release=reinterpret_cast<SwfRelease>(targets[21].address);
    swf_set_text=reinterpret_cast<SwfText>(targets[22].address);
    map_manager=reinterpret_cast<MapManager>(targets[23].address);
    map_lookup=reinterpret_cast<MapLookup>(targets[24].address);
    localize=reinterpret_cast<Localize>(targets[25].address);
    swf_set_material=reinterpret_cast<SwfMaterial>(targets[26].address);
    swf_label=reinterpret_cast<SwfLabel>(targets[36].address);
    swf_frame=reinterpret_cast<SwfFrame>(targets[37].address);
    find_material=reinterpret_cast<FindMaterial>(targets[33].address);
    material_manager=binding.image.base+0x5e05200;
    original_meter_update=reinterpret_cast<MeterUpdate>(originals[10]);
    original_userinfo_point=reinterpret_cast<UserInfoPoint>(originals[11]);
    original_hud_score_init=reinterpret_cast<Update>(originals[12]);
    original_end_items=reinterpret_cast<Update>(originals[13]);
    original_end_combat_init=reinterpret_cast<Update>(originals[14]);
    original_dossier_map=reinterpret_cast<Update>(originals[15]);
    original_challenge_card_update=reinterpret_cast<Update>(originals[16]);
    original_start_show=reinterpret_cast<Update>(originals[17]);
    original_boss_update=reinterpret_cast<Populate>(originals[18]);
    original_hud_challenge_update=reinterpret_cast<Update>(originals[19]);
    original_eol_challenge_update=reinterpret_cast<Update>(originals[20]);
    original_category_counts=reinterpret_cast<Update>(originals[21]);
    original_trigger_gate=reinterpret_cast<Available>(originals[22]);
    end_items_category_vtable=binding.image.base+0x2d10d78;
    for (unsigned i=0;i<23;++i) if (save::session().installation.hook(SC_INSTALL_SAVE_ENABLE,6,hooked[i],static_cast<uint32_t>(targets[hooked[i]].address-binding.image.base),[&] {
        return MH_EnableHook(reinterpret_cast<void*>(targets[hooked[i]].address)); })!=MH_OK) return false;
    struct OptionalPageHook { uint32_t rva; const char* bytes; void* hook; void** original; };
    const OptionalPageHook pages[]={
        {0xf42930,"488bc4565741554883ec7048895810488bf14c896020e885a72800488bcee85d",reinterpret_cast<void*>(challenges_show),reinterpret_cast<void**>(&original_challenges_show)},
        {0xf40bf0,"40535556574881ece8010000488b05d5dd26034833c448898424d0010000488b",reinterpret_cast<void*>(challenges_action),reinterpret_cast<void**>(&original_challenges_action)},
        {0xf42de0,"405741564157b8b0400000e8b08f9201482be0488b05debb26034833c4488984",reinterpret_cast<void*>(mission_tab),reinterpret_cast<void**>(&original_mission_tab)},
        {0xf58890,"40574883ec20488bf9e832482700488b07488bcfff50704885c00f84e1000000",reinterpret_cast<void*>(end_challenges_show),reinterpret_cast<void**>(&original_end_challenges_show)}};
    engine::LocalMemory optional_memory;
    for (const auto& page:pages) {
        native::Target target; target.address=binding.image.base+page.rva;
        for (size_t n=0;n<32;++n) {
            unsigned value=0; sscanf_s(page.bytes+n*2,"%2x",&value); target.bytes[n]=static_cast<uint8_t>(value);
        }
        auto reason=native::validate_target(optional_memory,binding.image,target,stop,GetTickCount64()+3000);
        if (!reason) {
            reason=MH_CreateHook(reinterpret_cast<void*>(target.address),page.hook,page.original);
            if (!reason) reason=MH_EnableHook(reinterpret_cast<void*>(target.address));
        }
        save::session().btrace.record(save::BStage::dossier_challenges,
            reason ? save::BStatus::refused : save::BStatus::succeeded,"phase9d_page_binding",0,
            {{"rva",page.rva},{"reason",reason}});
    }
    ready.store(true,std::memory_order_release); return true;
}
}
