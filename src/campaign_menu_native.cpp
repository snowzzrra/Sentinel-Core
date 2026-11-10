#include "engine/builds/build_descriptor.h"
#include "campaign_menu_native.h"
#include "player_strings_generated.h"
#include "campaign_menu.h"
#include "campaign_stages_generated.h"
#include "fast_travel.h"
#include "special.h"
#include "native_target.h"
#include "native_runtime.h"
#include "save_collector.h"
#include "save_session.h"
#include "MinHook.h"
#include "engine/hooks/hook_set.h"
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>

namespace sentinel::campaign_menu {
namespace {
decltype(&MH_EnableHook) menu_enable_operation = MH_EnableHook;
MH_STATUS WINAPI enable_menu_hook(void* target) { return menu_enable_operation(target); }
hooks::HookSet menu_hooks("campaign_menu", {MH_CreateHook, enable_menu_hook, MH_DisableHook, MH_RemoveHook}, &save::session().installation, 6);
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
using CardUpdate=void(*)(uintptr_t,uintptr_t,uintptr_t);
CardUpdate original_challenge_card_update=nullptr;
Update original_hud_challenge_update=nullptr,original_eol_challenge_update=nullptr;
Update original_category_counts=nullptr;
Update original_start_show=nullptr;
Populate original_boss_update=nullptr;
Update original_secret_rewards_show=nullptr,original_escalation_rewards_show=nullptr;
Populate original_reward_card=nullptr,original_escalation_card=nullptr;
using CachedText=uintptr_t(*)(uintptr_t,const char*);
using CachedSprite=uintptr_t(*)(uintptr_t);
CachedText cached_text=nullptr;
CachedSprite cached_sprite=nullptr;
using RewardPack=void(*)(uintptr_t,const char*,const void*);
RewardPack original_escalation_perk=nullptr;
Update original_hud_score_init=nullptr;
Update original_hud_score_update=nullptr;
uintptr_t hud_score_vtable=0;
std::atomic<uint64_t> hud_score_frame_ticks{0};
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
uintptr_t mapload_registry=0;
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
    if (ap_meter_published.exchange(0,std::memory_order_acq_rel)!=0) { ap_meter_retire_count.fetch_add(1,std::memory_order_relaxed); }
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
bool shown_verified = false;
bool active() { return save::session().campaign_run.enabled(); }
void fault(const char* reason);
void present_logo(uintptr_t screen,bool start);
struct RootLayout {
    std::array<uintptr_t,9> sprites{};
    std::array<std::array<float,2>,9> positions{};
} root_layout;
bool read_root_layout_row(uintptr_t screen,size_t offset,size_t i,RootLayout& observed,std::array<uintptr_t,9>& translations) {
    uintptr_t widget=0,renderer=0,transforms=0; int32_t index=-1;
    if (!read(screen,offset,widget) || !read(widget,0x18,observed.sprites[i]) || !observed.sprites[i] || !read(observed.sprites[i],0xc,index) || index<0 ||
        !read(observed.sprites[i],0x10,renderer) || !read(renderer,0x80,transforms) || !transforms) {
        save::session().btrace.record(save::BStage::root_layout,save::BStatus::pending,"root_layout_widget_pending",0,
            {{"row",i},{"widget_offset",offset},{"widget_present",widget!=0},
             {"sprite_present",observed.sprites[i]!=0},{"transform_index",index}},screen);
        return false;
    }
    // native x/y setters use this translation and refresh sprite render/hit-test children with 0x1857110
    if (!engine::add(transforms,size_t(index)*0x40+0x14,sizeof(observed.positions[i]),translations[i]) ||
        memory.copy(translations[i],observed.positions[i].data(),sizeof(observed.positions[i])).reason ||
        !std::isfinite(observed.positions[i][0]) || !std::isfinite(observed.positions[i][1])) {
        save::session().btrace.record(save::BStage::root_layout,save::BStatus::pending,"root_layout_transform_pending",0,{{"row",i}},screen);
        return false;
    }
    return true;
}
void compact_root(uintptr_t screen) {
    // hidden root widgets keep their positions, bindings and actions; fill the two empty spots
    constexpr size_t offsets[]{0x140,0x148,0x150,0x158,0x160,0x168,0x178,0x180,0x188};
    RootLayout observed;
    std::array<uintptr_t,9> translations{};
    for (size_t i=0;i<observed.sprites.size();++i) { if (!read_root_layout_row(screen,offsets[i],i,observed,translations)) return; }
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
        // state 0 hides the widget; root navigation skips it when restoring focus and keeps its native data
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
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "main_logo_widget_fault",0,{},screen);
    }
}
void root_campaign(uintptr_t screen,uintptr_t declaration) {
    retire_ap_meter();
    if (active()) save::session().campaign_run.cancel_menu_save();
    shown_screen=0;
    if (active()) {
        const auto definitions=campaign_definitions();
        if (!definitions || !read(definitions,0x148,declaration) || !declaration) { fault("campaign_root_main_declaration_unavailable"); return; }
        *reinterpret_cast<uint32_t*>(screen+0x1c8)=0;
    }
    original_root_campaign(screen,declaration);
    __try { present_logo(screen,false); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "main_logo_widget_fault",0,{},screen);
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
    return challenge.required && suffix && suffix[1]>='1' && suffix[1]<='3' && !suffix[2] ? suffix[1]-'1' : -1;
}
void present_logo(uintptr_t screen,bool start) {
    const auto stage=start?save::BStage::start_logo:save::BStage::main_logo;
    uintptr_t table=0,method=0;
    if (!read(screen,0,table) || !read(table,0x70,method) || !method) {
        save::session().btrace.record(stage,save::BStatus::pending,"logo_root_method_unavailable"); return;
    }
    const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
    const auto group=swf_child(swf_child(root,"_center"),"logo");
    // use the packaged bitmaps with their original layout
    save::session().btrace.record(stage,group?save::BStatus::succeeded:save::BStatus::pending,
        group?"logo_container_bound":"logo_container_unavailable",0,{{"container",group!=0}});
}
void start_show(uintptr_t screen) {
    original_start_show(screen);
    __try { present_logo(screen,true); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "start_logo_widget_fault",0,{},screen);
    }
}
uint32_t unlockable_id(std::string_view name) {
    uint32_t key=0;
    for (const auto c:name) key=31*key+static_cast<uint8_t>(c);
    return key;
}
void present_save_title(uintptr_t screen,uintptr_t name,const save::CampaignSnapshot& snapshot) {
    const auto row_projection=menu().projection();
    const char* title=nullptr;
    for (uint32_t i=0;i<row_projection.count;++i) {
        if (row_projection.rows[i].id==snapshot.stage_id) {
            title=row_projection.rows[i].title;
            break;
        }
    }
    if (name && title && *title) {
        swf_set_text(name,title);
        save::NativeString actual{};
        const bool verified=read(name,0x40,actual) && actual.data && actual.length==static_cast<int32_t>(std::strlen(title)) &&
            !std::memcmp(actual.data,title,static_cast<size_t>(actual.length));
        save::session().btrace.record(save::BStage::profile_output, verified?save::BStatus::succeeded:save::BStatus::pending,
            verified?"checkpoint_title_bound":"checkpoint_title_readback_pending",0,
            {{"title",name!=0},{"row",title!=nullptr},{"readback",verified}},screen);
    } else {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::pending,
            "checkpoint_title_binding_pending",0,{{"title",name!=0},{"row",title!=nullptr}},screen);
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
        if (manager && read(manager,8,registry) && registry) declaration=map_lookup(registry,snapshot.map.c_str(),1,0);
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
    present_save_title(screen,name,snapshot);
}
void safe_present_save_preview(uintptr_t screen,const save::CampaignSnapshot& snapshot) {
    __try { present_save_preview(screen,snapshot); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "campaign_save_preview_widget_fault",0,{},screen);
    }
}
void userinfo_point(uintptr_t widget,int kind,uintptr_t material,int count,int width,int delta) {
    // dossier/pause currency ids are 1 for praetor and 2 for mastery; kept rows set positions and dividers
    if (active() && (kind==1 || kind==2)) {
        save::session().btrace.record(save::BStage::dossier_points,save::BStatus::succeeded, "native_currency_row_suppressed",0,{{"kind",kind}},widget);
        return;
    }
    original_userinfo_point(widget,kind,material,count,width,delta);
}
bool present_hud_found(uintptr_t meter);
void present_dossier_map(uintptr_t screen) {
    uintptr_t meter=0,widget=0,content=0;
    if (read(screen,0x150,meter) && meter) present_hud_found(meter);
    if (read(screen,0x138,widget) && read(widget,0x18,content) && content) original_sprite_visibility(content,0,1);
}
void dossier_map(uintptr_t screen) {
    original_dossier_map(screen);
    if (!active()) return;
    __try { present_dossier_map(screen); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "dossier_items_widget_fault",0,{},screen);
    }
}
sc_physical_challenge challenge_projection(uint32_t id,bool end=false) {
    if (!active() || !id) return {};
    const auto campaign=save::session().campaign_run.snapshot();
    const auto stage=end && campaign.stage_id==1 ? campaign.end_summary_stage_id : campaign.stage_id;
    const auto projection=menu().projection();
    for (uint32_t i=0;i<projection.count;++i) {
        if (!(projection.rows[i].flags&SC_CAMPAIGN_DETAILS) || stage!=projection.rows[i].id || projection.summaries[i].known!=1) continue;
        for (const auto& challenge:projection.summaries[i].challenges) {
            // iddeclunlockable::getdisplayinfo hashes its exact name with unsigned 31*x+c
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
    const auto stage=end && campaign.stage_id==1 ? campaign.end_summary_stage_id : campaign.stage_id;
    const auto projection=menu().projection();
    if (projection.namespace_id!=save::session().namespace_id()) return {};
    for (uint32_t i=0;i<projection.count;++i) {
        if (!(projection.rows[i].flags&SC_CAMPAIGN_REVEALED) || stage!=projection.rows[i].id) continue;
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
        uintptr_t widget=0; uint32_t id=0; char name=0;
        if (!read(screen,0x640+i*8,widget) || !read(widget,0x2b0,id) || !id || !read(widget,0x2b4,name) || !name) continue;
        for (unsigned n=1;n<=(reward.kind==SC_REWARD_AGGREGATE ? 3u : 1u);++n) {
            const auto identity=std::string(reward.unlockable)+(reward.kind==SC_REWARD_AGGREGATE ? "/challenge_"+std::to_string(n) : "");
            const auto key=unlockable_id(identity);
            if (key==id) return true;
        }
    }
    return false;
}
bool aggregate_visible(uintptr_t screen) {
    uintptr_t widget=0,root=0; uint8_t visible=0;
    return read(screen,0x638,widget) && read(widget,0x18,root) && read(swf_child(root,"apAggregate"),0x51,visible) && visible;
}
void mission_hint_intents(uintptr_t screen) {
    if (!active()) return;
    const auto campaign=save::session().campaign_run.snapshot();
    const auto projection=menu().projection();
    const auto& ns=save::session().namespace_id();
    if (projection.namespace_id!=ns) return;
    for (uint32_t i=0;i<projection.count;++i) if ((projection.rows[i].flags&SC_CAMPAIGN_REVEALED) && campaign.stage_id==projection.rows[i].id)
            for (const auto& reward:projection.rewards[i]) if ((reward.kind==SC_REWARD_MISSION && reward_represented(screen,reward)) ||
                    (reward.kind==SC_REWARD_AGGREGATE && aggregate_visible(screen)))
                    menu().hint_intent(ns.c_str(),reward.location_id);
}
using SwfCreateMaterial=uintptr_t(*)(uintptr_t,const int*,const char*,const char*);
SwfCreateMaterial swf_create_material=nullptr;
using SwfColor=void(*)(uintptr_t,uint32_t);
SwfColor swf_color=nullptr;
uintptr_t question_material(uintptr_t sprite) {
    uintptr_t swf=0;
    if (!sprite || !read(sprite,0x30,swf) || !swf) return 0;
    if (*reinterpret_cast<const uint32_t*>(sprite+0x58)!=1 || *reinterpret_cast<const uint8_t*>(sprite+0x50)) swf_frame(sprite,1);
    const int image_type=1;
    // the shared codex question texture belongs to the native ui cache class
    const auto cache_class=reinterpret_cast<uint8_t*>(swf+0x128);
    const auto legacy=*cache_class;
    uintptr_t material=0;
    __try {
        *cache_class=0;
        material=swf_create_material(swf,&image_type,"guiBlend","textures/swf_images/dossier/codex/hud_codex_questionmark.png");
    } __finally { *cache_class=legacy; }
    return material;
}
struct EncounterPopup {
    uintptr_t screen=0,widget=0;
    std::string map,namespace_id;
    uint32_t stage_id=0;
    uint64_t since=0;
};
std::array<EncounterPopup,2> encounter_popups;
void capture_encounter_popup(uintptr_t screen,bool escalation) {
    auto& popup=encounter_popups[escalation ? 1 : 0];
    popup={};
    if (!active() || !save::session().accepts_requests()) return;
    const auto campaign=save::session().campaign_run.snapshot();
    if (campaign.map.compare(0,escalation ? 10 : 9,escalation ? "game/dlc2/" : "game/dlc/")!=0) return;
    const auto& ns=save::session().namespace_id();
    sc_campaign_reward reward{};
    uintptr_t widget=0;
    const auto now=GetTickCount64();
    const bool known=menu().encounter_reward(campaign.stage_id,ns,now,reward);
    if (known && read(screen,0x158,widget) && widget) popup={screen,widget,campaign.map,ns,campaign.stage_id,now};
    save::session().btrace.record(save::BStage::encounter_rewards, popup.screen ? save::BStatus::succeeded : save::BStatus::pending,"encounter_popup_binding",0,
        {{"projection",known},{"widget",widget},{"location",reward.location_id}},screen);
}
bool popup_reward(uintptr_t owner,bool screen,sc_campaign_reward& reward) {
    const auto campaign=save::session().campaign_run.snapshot();
    const auto& ns=save::session().namespace_id();
    if (!active() || !save::session().accepts_requests()) return false;
    for (const auto& popup:encounter_popups) if (owner==(screen ? popup.screen : popup.widget) && popup.since &&
            GetTickCount64()-popup.since<15000 && campaign.stage_id==popup.stage_id && ns==popup.namespace_id)
            return menu().encounter_reward(popup.stage_id,ns,popup.since,reward);
    return false;
}
const char* encounter_text(const sc_campaign_reward& reward) { return reward.location_id ? reward.text : player_strings::menu_reward_title; }
void present_encounter_card(uintptr_t widget,const sc_campaign_reward& reward) {
    if (!cached_text || !cached_sprite) return;
    const auto title=cached_text(widget+0x210,"txtVal");
    const auto category=cached_text(widget+0x240,"txtVal");
    if (!title || !category) {
        save::session().btrace.record(save::BStage::encounter_rewards,save::BStatus::pending,
            "encounter_card_fields_missing",0,{{"title",title},{"category",category}},widget);
        return;
    }
    swf_set_text(title,encounter_text(reward));
    swf_set_text(category,player_strings::menu_reward_category);
    for (const size_t offset:{0x1b0u,0x1e0u,0x270u,0x300u}) if (const auto child=cached_sprite(widget+offset)) original_sprite_visibility(child,0,1);
    for (const size_t offset:{0x380u,0x388u,0x390u}) {
        uintptr_t child=0;
        if (read(widget,offset,child) && child) widget_state(child,0);
    }
    const auto image=cached_sprite(widget+0x2a0);
    const auto material=question_material(image);
    if (image) {
        if (material) swf_set_material(image,material,0);
        original_sprite_visibility(image,material!=0,1);
    }
    save::session().btrace.record(save::BStage::encounter_rewards, reward.location_id ? save::BStatus::succeeded : save::BStatus::pending,
        "encounter_reward_card",0,{{"location",reward.location_id},{"image",material!=0}},widget);
}
void secret_rewards_show(uintptr_t screen) {
    capture_encounter_popup(screen,false);
    original_secret_rewards_show(screen);
}
void escalation_rewards_show(uintptr_t screen) {
    capture_encounter_popup(screen,true);
    original_escalation_rewards_show(screen);
}
void reward_card(uintptr_t widget,uintptr_t declaration) {
    original_reward_card(widget,declaration);
    sc_campaign_reward reward{};
    if (popup_reward(widget,false,reward)) present_encounter_card(widget,reward);
}
void escalation_card(uintptr_t widget,uintptr_t declaration) {
    original_escalation_card(widget,declaration);
    sc_campaign_reward reward{};
    if (popup_reward(widget,false,reward)) present_encounter_card(widget,reward);
}
void escalation_perk(uintptr_t screen,const char* root_name,const void* pack) {
    sc_campaign_reward reward{};
    if (!pack || !popup_reward(screen,true,reward)) { original_escalation_perk(screen,root_name,pack); return; }
    std::array<char,0x340> replacement{};
    std::memcpy(replacement.data(),pack,replacement.size());
    strcpy_s(replacement.data(),0xc0,player_strings::menu_reward_category);
    strcpy_s(replacement.data()+0xc0,0x200,encounter_text(reward));
    original_escalation_perk(screen,root_name,replacement.data());
    uintptr_t widget=0,root=0;
    if (read(screen,0x290,widget) && read(widget,0x18,root)) {
        const auto image=swf_child(swf_child(root,"icon"),"image");
        const auto material=question_material(image);
        if (image) {
            if (material) swf_set_material(image,material,0);
            original_sprite_visibility(image,material!=0,1);
        }
    }
    save::session().btrace.record(save::BStage::encounter_rewards, reward.location_id ? save::BStatus::succeeded : save::BStatus::pending,
        "encounter_reward_perk",0,{{"location",reward.location_id}},screen);
}
bool present_gate_icon(uintptr_t icon,uintptr_t material,bool owned) {
    const bool bound=icon && material;
    if (bound) {
        swf_frame(icon,1); swf_set_material(icon,material,0);
        swf_color(icon,owned ? 54 : 57);
    }
    return bound;
}
void present_gate_status(uintptr_t root,uint32_t flags) {
    const auto region=swf_child(root,"apSlayerStatus");
    const bool known=(flags&SC_CAMPAIGN_SLAYER_GATE)!=0;
    const auto key=swf_child(region,"apSlayerKey");
    const auto gate=swf_child(region,"apSlayerGate");
    if (region) original_sprite_visibility(region,known,1);
    const auto key_material=known ? find_material(material_manager,(flags&SC_CAMPAIGN_GATE_KEY)
        ? "art/ui/dossier/icons/ico_slayerkey_on" : "art/ui/dossier/icons/ico_slayerkey_off",1) : 0;
    const auto gate_material=known ? find_material(material_manager, "art/ui/icons/end_of_level/ico_eol_encounter_slayer_gate",1) : 0;
    const bool key_bound=present_gate_icon(key,key_material,(flags&SC_CAMPAIGN_GATE_KEY)!=0);
    const bool gate_bound=present_gate_icon(gate,gate_material,(flags&SC_CAMPAIGN_GATE_COMPLETE)!=0);
    if (key) original_sprite_visibility(key,key_material!=0,1);
    if (gate) original_sprite_visibility(gate,gate_material!=0,1);
    save::session().btrace.record(save::BStage::mission_details, !known || (key_bound && gate_bound) ? save::BStatus::succeeded : save::BStatus::pending,
        "mission_gate_status",shown.revision,{{"available",known},{"key_owned",(flags&SC_CAMPAIGN_GATE_KEY)!=0},
        {"completed",(flags&SC_CAMPAIGN_GATE_COMPLETE)!=0},{"key_bound",key_bound},{"gate_bound",gate_bound}},root);
}
void present_aggregate(uintptr_t root,const sc_campaign_reward& reward,const char* battery_name="sentinelBattery") {
    const bool completed=reward.checked!=0;
    if (const auto battery=swf_child(root,battery_name)) original_sprite_visibility(battery,0,1);
    const auto region=swf_child(root,"apAggregate");
    if (const auto background=swf_child(region,"incomplete")) original_sprite_visibility(background,!completed,1);
    if (const auto background=swf_child(region,"complete")) original_sprite_visibility(background,completed,1);
    if (const auto heading=swf_child(region,"apCompleteHeading")) {
        original_sprite_visibility(heading,completed,1);
        if (const auto normal=swf_child(region,"heading")) original_sprite_visibility(normal,!completed,1);
    }
    const auto text=swf_child(swf_child(region,"apReward"),"txtVal",true);
    const auto image=swf_child(region,"question");
    const auto question=question_material(image);
    if (image) {
        if (question) swf_set_material(image,question,0);
        original_sprite_visibility(image,question!=0,1);
    }
    if (text) { swf_set_text(text,reward.location_id ? reward.text : ""); reward_scroll(text); }
    if (region) original_sprite_visibility(region,reward.location_id!=0 && text,1);
    save::session().btrace.record(save::BStage::dossier_challenges,
        text && question ? save::BStatus::succeeded : save::BStatus::pending,"aggregate_reward_binding",0,
        {{"location",reward.location_id},{"text",text!=0},{"image",image!=0},{"material",question!=0}});
}
sc_campaign_reward aggregate_reward(bool end=false) {
    const auto campaign=save::session().campaign_run.snapshot();
    const auto stage=end && campaign.stage_id==1 ? campaign.end_summary_stage_id : campaign.stage_id;
    const auto projection=menu().projection();
    if (projection.namespace_id!=save::session().namespace_id()) return {};
    for (uint32_t i=0;i<projection.count;++i) if ((projection.rows[i].flags&SC_CAMPAIGN_REVEALED) && stage==projection.rows[i].id)
            for (const auto& reward:projection.rewards[i]) if (reward.kind==SC_REWARD_AGGREGATE) return reward;
    return {};
}
void present_card_reward(uintptr_t widget,uint32_t id);
void present_mission_rewards(uintptr_t screen) {
    uintptr_t widget=0,root=0;
    if (read(screen,0x638,widget) && read(widget,0x18,root)) {
        const auto reward=aggregate_reward();
        bool discovered=true;
        for (unsigned i=0;i<3;++i) {
            uintptr_t card=0; uint32_t id=0; char name=0;
            discovered=discovered && read(screen,0x640+i*8,card) && read(card,0x2b0,id) && read(card,0x2b4,name) && name && mission_reward(id).location_id;
        }
        present_aggregate(root,discovered ? reward : sc_campaign_reward{},"sentinelBattery");
        if (const auto arrow=swf_child(root,"arrow")) original_sprite_visibility(arrow,0,1);
    }
    for (unsigned i=0;i<3;++i) {
        uint32_t id=0;
        if (read(screen,0x640+i*8,widget) && read(widget,0x2b0,id)) present_card_reward(widget,id);
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
        present_aggregate(swf_child(root,"_center"),aggregate_reward(true));
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
    __try { present_mission_rewards(screen); }
    __except(EXCEPTION_EXECUTE_HANDLER) { save::session().btrace.record(save::BStage::dossier_challenges,save::BStatus::blocked,"dossier_reward_fault"); }
    if (!previous) publish_mission_page(screen);
}
uintptr_t challenges_action(uintptr_t screen,uintptr_t action,uintptr_t event,uintptr_t source) {
    const bool previous=explicit_challenges_page;
    explicit_challenges_page=true;
    uintptr_t result=0;
    __try { result=original_challenges_action(screen,action,event,source); }
    __finally { explicit_challenges_page=previous; }
    __try { present_mission_rewards(screen); }
    __except(EXCEPTION_EXECUTE_HANDLER) { save::session().btrace.record(save::BStage::dossier_challenges,save::BStatus::blocked,"dossier_reward_fault"); }
    if (!previous) publish_mission_page(screen);
    return result;
}
void mission_tab(uintptr_t screen) {
    original_mission_tab(screen);
    __try { present_mission_rewards(screen); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
uintptr_t native_card(uintptr_t widget) {
    uintptr_t table=0,method=0;
    if (!read(widget,0,table) || !read(table,0xe0,method) || !method) return 0;
    return reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(widget);
}
void present_card_reward(uintptr_t widget,uint32_t id) {
    if (!active()) return;
    const auto reward=mission_reward(id);
    const auto card=native_card(widget);
    const auto region=swf_child(card,"apReward");
    const auto text=swf_child(region,"txtVal",true);
    if (text) { swf_set_text(text,reward.location_id ? reward.text : ""); reward_scroll(text); }
    if (region) original_sprite_visibility(region,reward.location_id!=0 && text,1);
    const bool written=text && reward.location_id;
    save::session().btrace.record(save::BStage::dossier_challenges, written ? save::BStatus::succeeded : save::BStatus::pending,
        written ? "dossier_reward_bound" : "dossier_reward_pending",0,
        {{"id",id},{"location",reward.location_id},{"card",card!=0},{"text",text!=0},
         {"region",region!=0}},widget);
}
void challenge_card_update(uintptr_t widget,uintptr_t context,uintptr_t flags) {
    uint32_t id=0; read(widget,0x2b0,id);
    const auto challenge=challenge_projection(id);
    uint32_t found=0,required=0; uint8_t checked=0;
    if (!challenge.required || !read(widget,0x448,found) || !read(widget,0x44c,required) || !read(widget,0x450,checked)) {
        original_challenge_card_update(widget,context,flags);
        __try { present_card_reward(widget,id); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        return;
    }
    // borrow just the display values; the game keeps its records and celebration flags
    *reinterpret_cast<uint32_t*>(widget+0x448)=challenge.found;
    *reinterpret_cast<uint32_t*>(widget+0x44c)=challenge.required;
    *reinterpret_cast<uint8_t*>(widget+0x450)=challenge.checked!=0;
    __try { original_challenge_card_update(widget,context,flags); }
    __finally {
        *reinterpret_cast<uint32_t*>(widget+0x448)=found;
        *reinterpret_cast<uint32_t*>(widget+0x44c)=required;
        *reinterpret_cast<uint8_t*>(widget+0x450)=checked;
    }
    const auto card=native_card(widget);
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
    const auto reward=mission_reward(id);
    if (!reward.location_id) return;
    const auto title=player_strings::menu_challenge_completed(reward.name);
    if (special::present_challenge_completion(widget,title.c_str(),reward.text)) {
        toast.checked=true;
        save::session().btrace.record(save::BStage::hud_challenges,save::BStatus::succeeded, "physical_challenge_completion_presented",0,{{"id",id}},widget);
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
    // the native meter uses frame 100 for completion; pending 100% keeps frame 99
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
    __except(EXCEPTION_EXECUTE_HANDLER) { save::session().btrace.record(save::BStage::hud_challenges,save::BStatus::blocked,"hud_challenge_fault"); }
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
    __except(EXCEPTION_EXECUTE_HANDLER) { save::session().btrace.record(save::BStage::end_challenges,save::BStatus::blocked,"end_challenge_fault"); }
}
bool text_matches(uintptr_t field,const char* expected);
bool present_found_root(uintptr_t root,uintptr_t meter_root,bool independent) {
    const auto campaign=save::session().campaign_run.snapshot();
    const auto stage=campaign.map=="game/hub/hub" ? save::BStage::hud_found_hub : save::BStage::hud_found_fields;
    if (independent && meter_root) original_sprite_visibility(meter_root,0,1);
    for (const auto leaf:{"background","corruption","glow_burst","pointCount","header"})
        if (const auto sprite=swf_child(meter_root,leaf)) original_sprite_visibility(sprite,0,1);
    const auto label=swf_child(root,"apFoundLabel");
    const auto text_root=swf_child(label,"text");
    const auto title=swf_child(text_root,"txtVal",true);
    if (!title) {
        special::present_combat_progress(root,0,false);
        save::session().btrace.record(stage,save::BStatus::blocked, "hud_found_text_missing",0,{{"root",root},{"label",label},{"text",text_root}},root);
        return false;
    }
    sc_campaign_summary summary{};
    uint64_t revision=0;
    const bool row_found=menu().summary(campaign.stage_id,save::session().namespace_id(),summary,&revision);
    if (summary.known!=1) {
        original_sprite_visibility(label,0,1);
        special::present_combat_progress(root,label,false);
        if (meter_root) original_sprite_visibility(meter_root,0,1);
        save::session().btrace.record(stage,save::BStatus::pending,
            "hud_found_summary_unknown",0,{{"revision",revision},{"row_found",row_found},{"known",summary.known}},root);
        return true;
    }
    char value[64]; std::snprintf(value,sizeof(value),player_strings::hud_items_found,summary.found,summary.total);
    if (!text_matches(title,value)) swf_set_text(title,value);
    constexpr uint32_t slayer_green=54;
    constexpr uint32_t white=57;
    swf_color(label,white);
    swf_color(text_root,slayer_green);
    const auto image=swf_child(label,"question");
    const auto question=question_material(image);
    if (image) {
        if (question) swf_set_material(image,question,0);
        swf_color(image,slayer_green);
        original_sprite_visibility(image,question!=0,1);
    }
    original_sprite_visibility(label,1,1);
    if (!independent) original_sprite_visibility(root,1,1);
    special::present_combat_progress(root,label,true);
    save::session().btrace.record(stage, question ? save::BStatus::succeeded : save::BStatus::pending,"hud_found_presentation",0,
        {{"named_color",*reinterpret_cast<const uint32_t*>(text_root+0x6c)},{"material",question!=0},
         {"found",summary.found},{"total",summary.total},{"playing",*reinterpret_cast<const uint8_t*>(label+0x50)},
         {"question_color",image?*reinterpret_cast<const uint32_t*>(image+0x6c):0},
         {"revision",revision},{"root",root},{"label",label},
         {"root_visible",*reinterpret_cast<const uint8_t*>(root+0x51)},
         {"label_visible",*reinterpret_cast<const uint8_t*>(label+0x51)},
         {"text_matches",text_matches(title,value)}},root);
    return true;
}
void present_score_found(uintptr_t score) {
    uintptr_t swf=0,root=0;
    uint8_t visible=0;
    const bool hub=save::session().campaign_run.snapshot().map=="game/hub/hub";
    const auto stage=hub ? save::BStage::hud_found_hub : save::BStage::hud_found_fields;
    const bool fortress=hub && native::gameplay_admitted();
    static uintptr_t fortress_root=0;
    if (!read(score,0x10,swf) || !read(swf,0x18,root) || !root) {
        save::session().btrace.record(stage,save::BStatus::blocked, "hud_score_root_missing",0,{{"swf",swf},{"root",root}},score);
        return;
    }
    if (!active() || !read(score,0xb8,visible) || (!visible && !fortress)) {
        if (fortress_root==root && !visible) { original_sprite_visibility(root,0,1); fortress_root=0; }
        if (const auto label=swf_child(root,"apFoundLabel")) original_sprite_visibility(label,0,1);
        special::present_combat_progress(root,0,false);
        save::session().btrace.record(stage,save::BStatus::pending,
            "hud_score_visibility_gate",0,{{"active",active()},{"visible",visible},{"root",root}},score);
        return;
    }
    if (fortress && !visible) { original_sprite_visibility(root,1,1); fortress_root=root; }
    present_found_root(root,swf_child(root,"demonic_corruption"),true);
}
void hud_score_init(uintptr_t score) {
    original_hud_score_init(score);
    save::session().btrace.record(save::BStage::hud_score_init,save::BStatus::entered, "hud_score_initialized",0,{},score);
    __try { present_score_found(score); } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::hud_score_init,save::BStatus::blocked, "hud_score_init_fault",0,{},score);
    }
}
void hud_score_update(uintptr_t score) {
    original_hud_score_update(score);
    LARGE_INTEGER start{}; QueryPerformanceCounter(&start);
    __try {
        uintptr_t vtable=0;
        if (!read(score,0,vtable) || vtable!=hud_score_vtable) return;
        hud_score_frame_ticks.fetch_add(1,std::memory_order_relaxed);
        present_score_found(score);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::hud_score_frame,save::BStatus::blocked, "hud_score_update_fault",0,{},score);
    }
    LARGE_INTEGER end{},frequency{}; QueryPerformanceCounter(&end); QueryPerformanceFrequency(&frequency);
    static uint64_t total=0,maximum=0,next=0;
    const auto elapsed=static_cast<uint64_t>((end.QuadPart-start.QuadPart)*1000000/frequency.QuadPart);
    total+=elapsed; maximum=std::max(maximum,elapsed);
    const auto now=GetTickCount64();
    if (now>=next) {
        next=now+5000;
        save::session().btrace.record(save::BStage::hud_score_frame,save::BStatus::succeeded,"hud_score_cost_us",0,
            {{"ticks",hud_score_frame_ticks.load(std::memory_order_relaxed)},{"total_us",total},{"max_us",maximum}},score);
    }
}
bool present_hud_found(uintptr_t meter) {
    uintptr_t root=0;
    if (!read(meter,0x18,root) || !root) return false;
    const auto meter_root=root;
    if (swf_child(root,"apFoundLabel")) return present_found_root(root,meter_root,false);
    if (!read(meter_root,0x10,root) || !root) return false;
    return present_found_root(root,meter_root,true);
}
bool text_matches(uintptr_t field,const char* expected) {
    save::NativeString value{};
    if (!field || !read(field,0x40,value) || !value.data || value.length!=static_cast<int32_t>(std::strlen(expected))) return false;
    return std::memcmp(value.data,expected,static_cast<size_t>(value.length))==0;
}
unsigned count_text_matches(uintptr_t label,uintptr_t current,uintptr_t maximum,const char* value,const char* max_value) {
    const bool label_ok=text_matches(label,player_strings::menu_items_found);
    const bool current_ok=text_matches(current,value);
    const bool maximum_ok=text_matches(maximum,max_value);
    return unsigned(label_ok)|(unsigned(current_ok)<<1)|(unsigned(maximum_ok)<<2);
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
    const unsigned before=count_text_matches(label,current,maximum,value,max_value);
    static uintptr_t last_header=0;
    static uint64_t last_revision=0;
    if (nested_label && header==last_header && shown.revision==last_revision && before!=7)
        save::session().btrace.record(save::BStage::mission_count_rewrite,save::BStatus::entered,"native_text_rewrite_observed",shown.revision,
            {{"before_matches",before},{"found",summary.found},{"total",summary.total}},header);
    swf_set_text(label,player_strings::menu_items_found);
    swf_set_text(current,value);
    swf_set_text(maximum,max_value);
    original_sprite_visibility(header,1,1);
    const auto after=count_text_matches(label,current,maximum,value,max_value);
    const bool verified=after==7;
    save::session().btrace.record(stage,verified?save::BStatus::succeeded:save::BStatus::pending,"count_text_readback",shown.revision,
        {{"label",(after&1)!=0},{"current",(after&2)!=0},{"maximum",(after&4)!=0},{"label_type",label_type},
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
const sc_campaign_summary& end_items_summary(const Projection& projection,uint32_t stage) {
    const sc_campaign_summary* summary=nullptr;
    for (uint32_t i=0;i<projection.count;++i) if ((projection.rows[i].flags&SC_CAMPAIGN_DETAILS) && stage==projection.rows[i].id) {
            summary=&projection.summaries[i]; break;
        }
    static const sc_campaign_summary unknown{};
    if (!summary) summary=&unknown;
    return *summary;
}
bool end_items_owner(uintptr_t screen,uintptr_t& root,uintptr_t& screen_root) {
    uintptr_t category=0,vtable=0,screen_table=0,method=0;
    if (!read(screen,0x118,category) || !category || !read(category,0,vtable) || vtable!=end_items_category_vtable || !read(category,0x18,root) || !root ||
        !read(screen,0,screen_table) || !read(screen_table,0x70,method) || !method) {
        save::session().btrace.record(save::BStage::end_counts,save::BStatus::pending,"end_counts_owner_pending",0,{},screen);
        return false;
    }
    screen_root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
    return true;
}
void present_end_items(uintptr_t screen) {
    if (!active()) return;
    const auto campaign=save::session().campaign_run.snapshot();
    const auto& map=campaign.map=="game/hub/hub" ? campaign.end_summary_map : campaign.map;
    if (map.empty()) return;
    const auto projection=menu().projection();
    const auto stage=campaign.stage_id==1 ? campaign.end_summary_stage_id : campaign.stage_id;
    const auto* summary=&end_items_summary(projection,stage);
    uintptr_t root=0,screen_root=0;
    if (!end_items_owner(screen,root,screen_root)) return;
    const bool bound=present_found_counts(root,*summary,"itemsFound",false);
    if (bound) {
        const auto main=swf_child(screen_root,"main");
        if (const auto item_list=swf_child(swf_child(main,"itemsFound"),"itemList")) original_sprite_visibility(item_list,0,1);
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
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "end_items_presentation_widget_fault",0,{},screen);
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
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "end_combat_points_widget_fault",0,{},screen);
    }
}
bool boss_points_scope(uintptr_t screen) {
    const auto map=save::session().campaign_run.snapshot().map;
    uint32_t count=0;
    return read(screen,0x120,count) && count==5 && (map=="game/sp/e1m4_boss/e1m4_boss" || map=="game/sp/e2m4_boss/e2m4_boss" ||
         map=="game/sp/e3m3_maykr/e3m3_maykr");
}
void present_boss_artwork(uintptr_t screen,uintptr_t reward) {
    std::array<uintptr_t,40> artwork{}; unsigned leaves=0;
    // the tier and count card visibility drive native completion; only item artwork is hidden
    for (unsigned i=1;i<=5;++i) {
        char name[]="item1_c"; name[4]+=static_cast<char>(i-1);
        const auto item=swf_child(reward,name);
        if (!item || !swf_child(item,"icon_g")) {
            save::session().btrace.record(save::BStage::boss_presentation,save::BStatus::pending, "boss_points_art_unavailable",0,{{"item",i}},screen);
            return;
        }
        for (const char* leaf:{"completed","hex_bg_gold_b","bg_b","gradient_g","glow_b","fill","icon_g","pulse_b"})
            if (const auto child=swf_child(item,leaf)) artwork[leaves++]=child;
    }
    for (unsigned i=0;i<leaves;++i) original_sprite_visibility(artwork[i],0,1);
    save::session().btrace.record(save::BStage::boss_presentation, leaves?save::BStatus::succeeded:save::BStatus::pending,
        leaves?"boss_points_art_hidden":"boss_points_art_unavailable",0,{{"leaves",leaves}},screen);
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
        present_boss_artwork(screen,reward);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "boss_reward_widget_fault",0,{},screen);
    }
}
sc_campaign_reward detail_mission_reward(uintptr_t details,int32_t index,unsigned slot) {
    sc_campaign_reward placement{};
    uint32_t id=0;
    char native_name=0;
    if (shown.namespace_id==save::session().namespace_id() && read(details,0x1d0+slot*0x1b0,id) && read(details,0x1d4+slot*0x1b0,native_name) && native_name)
        for (const auto& reward:shown.rewards[index]) if (reward.kind==SC_REWARD_MISSION) {
            const auto key=unlockable_id(reward.unlockable);
            if (key==id) placement=reward;
        }
    return placement;
}
void present_detail_rewards(uintptr_t details,uintptr_t details_root,int32_t index) {
    const auto challenge_rows=swf_child(swf_child(details_root,"challenges"),"list");
    for (unsigned slot=0;slot<3;++slot) {
        char name[]="item0"; name[4]+=static_cast<char>(slot);
        const auto item=swf_child(challenge_rows,name);
        const auto region=swf_child(item,"apReward");
        const auto description=swf_child(item,"desc");
        const auto native_text=swf_child(description,"txtVal",true);
        const auto placement=detail_mission_reward(details,index,slot);
        save::NativeString base{};
        const auto text=swf_child(region,"txtVal",true);
        const bool combined=placement.location_id && text && read(native_text,0x40,base) && base.data;
        if (combined) {
            char value[768]{};
            std::snprintf(value,sizeof(value),"%.*s\n%s",base.length,base.data,placement.text);
            swf_set_text(text,value);
            reward_scroll(text);
        }
        if (region) original_sprite_visibility(region,combined,1);
        if (description) original_sprite_visibility(description,!combined,1);
    }
}
struct DetailCounts { uintptr_t root; bool bound; };
DetailCounts present_detail_counts(uintptr_t details,int32_t index,const sc_campaign_summary& summary) {
    uintptr_t category=0,root=0,vtable=0;
    if (read(details,0x1b8,category) && category && read(category,0,vtable) && vtable==end_items_category_vtable) read(category,0x18,root);
    const bool counts=present_found_counts(root,summary);
    save::session().btrace.record(save::BStage::mission_counts, counts?save::BStatus::succeeded:save::BStatus::pending,
        counts?"mission_counts_bound":"mission_counts_pending",shown.revision,
        {{"index",static_cast<uint64_t>(index)},{"known",summary.known},{"root",root!=0},{"found",summary.found},{"total",summary.total}},details);

    return {root,counts};
}
bool detail_icon_sized(uintptr_t icon) {
    uint16_t width=0,height=0;
    return read(icon,0x68,width) && read(icon,0x6a,height) && width==150 && height==150;
}
void present_detail_rating(uintptr_t details,uintptr_t details_root,int32_t index,const sc_campaign_summary& summary,DetailCounts count_view) {
    const auto icon=swf_child(details_root,"apDifficulty");
    const auto tier=summary.band&255;
    static constexpr const char* donor[]={"", "swf/main_menu/screens/mission_select_textures/swf_images/difficulty/Too_Young_",
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
    if (const auto item_list=swf_child(count_view.root,"itemsFound")) original_sprite_visibility(item_list,0,1);
    if (count_view.root) original_sprite_visibility(count_view.root,count_view.bound,1);
    uintptr_t applied_material=0;
    read(icon,0x60,applied_material);
    const bool sized=detail_icon_sized(icon);
    const bool applied=rating_bound && applied_material==material && sized;
    save::session().btrace.record(save::BStage::mission_rating, applied?save::BStatus::succeeded:save::BStatus::pending,
        applied?"preview_skull_bound":"preview_skull_pending",shown.revision,
        {{"index",static_cast<uint64_t>(index)},{"tier",tier},{"icon",icon!=0},
          {"material",material!=0},{"readback",material!=0 && applied_material==material},
          {"sized",sized}},details);
}
void present_detail_challenges(uintptr_t details,uintptr_t details_root,const sc_campaign_summary& summary) {
    const auto challenge_root=swf_child(swf_child(details_root,"challenges"),"list");
    if (summary.known!=1) return;
    unsigned requested=0,bound=0;
    for (const auto& challenge:summary.challenges) {
        const auto slot=challenge_slot(challenge);
        if (slot<0) continue;
        ++requested;
        uint32_t present=0; char title=0;
        if (!read(details,0x1d0+slot*0x1b0,present) || !present || !read(details,0x1d4+slot*0x1b0,title) || !title) continue;
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
void present_details(uintptr_t details) {
    const auto list=list_for(shown_screen);
    int32_t index=-1;
    if (!list || !read(list,0x150,index) || index<0 || static_cast<uint32_t>(index)>=shown.count || !(shown.rows[index].flags&SC_CAMPAIGN_DETAILS)) {
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
    sc_campaign_reward aggregate{};
    for (const auto& reward:shown.rewards[index]) if (reward.kind==SC_REWARD_AGGREGATE) aggregate=reward;
    present_detail_rewards(details,details_root,index);
    present_aggregate(details_root,aggregate,"batteries");
    present_gate_status(details_root,shown.rows[index].flags);
    if (const auto completion=swf_child(details_root,"completionInfo")) original_sprite_visibility(completion,0,1);
    const auto count_view=present_detail_counts(details,index,summary);

    present_detail_rating(details,details_root,index,summary,count_view);
    present_detail_challenges(details,details_root,summary);
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
    if (active()) {
        __try { if (present_hud_found(meter)) return; }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    meter_update_delegated.fetch_add(1,std::memory_order_relaxed);
    if (original_meter_update) original_meter_update(meter);
}
void sprite_visibility_detour(uintptr_t sprite,uint32_t visible,uint32_t flag, void* hook_caller) {
    if (!sprite) return;
    if (current_ap_details) {
        uintptr_t meter=0, meter_sprite=0;
        read(current_ap_details,0x6f8,meter);
        if (meter) read(meter,0x18,meter_sprite);
        const auto caller=reinterpret_cast<uintptr_t>(hook_caller);
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
        if (active() && shown_screen) save::session().btrace.record(save::BStage::mission_details,save::BStatus::blocked, "details_owner_mismatch");
        retire_ap_meter();
        details_update(details); return;
    }
    uintptr_t meter=0;
    if (read(details,0x6f8,meter) && meter) { ap_meter_published.store(meter,std::memory_order_release); }
    current_ap_details=details;
    __try { details_update(details); }
    __finally { current_ap_details=0; }
    __try { present_details(details); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked, "campaign_presentation_widget_fault",0,{},details);
    }
    uintptr_t sprite=0;
    if (meter && read(meter,0x18,sprite) && sprite) { if (original_sprite_visibility) original_sprite_visibility(sprite,0,1); }
}
void hide_details(uintptr_t screen) {
    uintptr_t details=0;
    if (!read(screen,0x118,details) || !details) return;
    string_assign(details+0x180,"");
    *reinterpret_cast<uintptr_t*>(details+0x1b0)=0;
    // the native details owner hides its entire swf sprite for an empty map
    render_details(details);
}
void focus(uintptr_t screen) {
    if (!active()) { original_focus(screen); return; }
    const auto list=list_for(screen);
    int32_t index=-1,count=0; uintptr_t rows=0;
    if (building || screen!=shown_screen || !read(list,0x150,index) || index<0 ||
        static_cast<uint32_t>(index)>=shown.count || !(shown.rows[index].flags&SC_CAMPAIGN_DETAILS) ||
        !read(screen,0x128,count) || index>=count || !read(screen,0x120,rows) || !rows) { hide_details(screen); return; }
    // list every row without marking it complete; read native stats only for the focused unlocked row
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
bool bind_campaign_source(const sc_campaign_row& row,Entry& entry,uintptr_t source_entries,int32_t source_count) {
    uintptr_t source=0; save::NativeString name{}; char map[192]{};
    const auto route=stage_route(row.id);
    const auto source_index=route?route->mission_index:row.native_index;
    if (source_index>=static_cast<uint32_t>(source_count) ||
        !engine::add(source_entries,static_cast<size_t>(source_index)*sizeof(Entry),sizeof(Entry),source) ||
        !read(source,0,name) || name.length<1 || name.length>=192 ||
        memory.copy(reinterpret_cast<uintptr_t>(name.data),map,static_cast<size_t>(name.length)+1).reason ||
        map[name.length] || std::strcmp(map,row.map) || !read(source,0x30,entry.dependencies)) return false;
    if (route && route->variant==6) entry.dependencies={};
    return true;
}
bool prepare_campaign_rows(const Projection& projection,uintptr_t previous) {
    if (!initialized) {
        for (auto& entry:entries) string_init(reinterpret_cast<uintptr_t>(&entry.name));
        initialized=true;
    }
    uintptr_t source_entries=0; int32_t source_count=0;
    if (!read(previous,0x88,source_entries) || !read(previous,0x90,source_count) || source_count<1) {
        fault("native_campaign_source_roster_unreadable"); return false;
    }
    for (uint32_t i=0;i<projection.count;++i) {
        entries[i].dependencies={};
        const auto& row=projection.rows[i];
        if (row.flags&SC_CAMPAIGN_REVEALED) {
            if (!bind_campaign_source(row,entries[i],source_entries,source_count)) { fault("native_campaign_source_entry_mismatch"); return false; }
        }
        string_assign(reinterpret_cast<uintptr_t>(&entries[i].name),projection.rows[i].map);
    }
    map_list.entries.data=reinterpret_cast<uintptr_t>(entries.data());
    map_list.entries.count=static_cast<int32_t>(projection.count);
    map_list.entries.capacity=static_cast<int32_t>(entries.size());
    return true;
}
bool label_campaign_rows(uintptr_t widgets) {
    for (uint32_t i=0;i<shown.count;++i) {
        uintptr_t widget=0;
        if (!read(widgets,static_cast<size_t>(i)*8,widget) || !widget) { fault("native_campaign_widget_missing"); return false; }
        std::string label=shown.rows[i].title;
        if (shown.rows[i].flags&SC_CAMPAIGN_GOAL) label+=" / GOAL";
        if (!(shown.rows[i].flags&SC_CAMPAIGN_UNLOCKED)) label+=" / LOCKED";
        else if (shown.rows[i].flags&SC_CAMPAIGN_COMPLETED) label+=" / COMPLETED";
        string_assign(widget+0x188,label.c_str());
        if (!(shown.rows[i].flags&SC_CAMPAIGN_UNLOCKED)) widget_state(widget,5);
    }
    return true;
}
void publish_campaign_meter(uintptr_t screen) {
    uintptr_t details=0,meter=0;
    if (read(screen,0x118,details) && details && read(details,0x6f8,meter) && meter) { ap_meter_published.store(meter,std::memory_order_release); }
}
void populate(uintptr_t screen,uintptr_t list) {
    shown_verified = false;
    if (!active()) { original_populate(screen,list); return; }
    if (!save::session().accepts_requests()) { hide_details(screen); return; }
    const auto projection=menu().projection();
    uintptr_t campaign=0,previous=0,pending=0;
    if (!read(screen,0x100,campaign) || !campaign || !read(campaign,0x1a8,previous) ||
        !read(screen,0x870,pending)) { fault("native_campaign_list_unreadable"); return; }
    // keep the mission row in place while its native save callback holds it
    if (pending) return;
    if (!prepare_campaign_rows(projection,previous)) return;
    shown=projection; shown_screen=screen;
    publish_campaign_meter(screen);
    populate_owned(screen,list,campaign,previous);
    uintptr_t widgets=0; int32_t count=0;
    if (!read(list,0xa0,widgets) || !read(list,0xa8,count) || count!=static_cast<int32_t>(shown.count)) {
        fault("native_campaign_widget_count_mismatch"); hide_details(screen); return;
    }
    if (!label_campaign_rows(widgets)) return;
    shown_verified = true;
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
    // permission/save/checkpoint/loading remain in loadmission/launchmission
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
    __try {
        const auto* route=stage_route(shown.rows[index].id);
        completed=route && route->variant==6 ? (shown.rows[index].flags&SC_CAMPAIGN_COMPLETED)!=0 :
            native_completed(screen,reinterpret_cast<uintptr_t>(&entries[index]));
        completion_known=true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    const auto& namespace_id=session.namespace_id();
    fast_travel::entry_policy().selected(namespace_id.c_str(),shown.rows[index].map, boundary.generation_after,completion_known,completed);
    menu().selected(shown.rows[index].id);
    menu().load_requested(shown.rows[index].id, shown, native::evidence_scope());
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
void observe_menu_entries() { if (shown_verified) menu().entries_usable(shown, native::evidence_scope()); }
void restore_campaign_focus(uintptr_t screen) {
    const auto wanted=menu().focus_id();
    const auto list=list_for(screen);
    uintptr_t table=0,method=0;
    if (list && read(list,0,table) && read(table,0x68,method) && method) for (uint32_t i=0;i<shown.count;++i) if (shown.rows[i].id==wanted) {
            reinterpret_cast<void(*)(uintptr_t,int,int)>(method)(list,static_cast<int>(i),0); break;
        }
}
bool request_campaign_rebuild(uintptr_t screen) {
    if (active() && screen==shown_screen && shown.revision!=menu().projection().revision) {
        uintptr_t pending=0; int32_t state=-1;
        if (read(screen,0x870,pending) && !pending && read(screen,0x108,state) && state==0) {
            *reinterpret_cast<int32_t*>(screen+0x108)=-1; // native update owns clear/repopulate/focus
            return true;
        }
    }
    return false;
}
void update(uintptr_t screen) {
    int32_t before=0;
    read(screen,0x108,before);
    bool rebuild=before==-1;
    if (request_campaign_rebuild(screen)) rebuild=true;
    original_update(screen);
    if (active() && rebuild && screen==shown_screen) { restore_campaign_focus(screen); }
    int32_t state=-1;
    if (active() && save::session().accepts_requests() && screen==shown_screen && read(screen,0x108,state) && state==0) {
        menu().rendered(shown.revision);
        observe_menu_entries();
    }
}
}
sc_mission_access_result observe_access(const sc_mission_access_request& request) {
    const auto scope = native::evidence_scope();
    if (!available() || !native::same_scope(scope.process, request.execution.expected) ||
        std::memcmp(scope.namespace_id, request.namespace_id, 65) || std::memcmp(scope.save_binding, request.save_binding, 16)) return {};
    auto out = menu().access(request.stage_id, scope);
    const auto after = native::evidence_scope();
    if (!native::same_scope(scope.process, after.process) || std::memcmp(scope.save_binding, after.save_binding, 16) ||
        std::memcmp(scope.namespace_id, after.namespace_id, 65) || after.readiness != SC_EVIDENCE_READY) return {};
    return out;
}
bool available() { return ready.load(std::memory_order_acquire); }
void reward_scroll(uintptr_t text) {
    // text mode at +0x174 uses flag 4 for auto-scroll
    uint32_t mode=0;
    if (text && read(text,0x174,mode) && mode<=5) *reinterpret_cast<uint32_t*>(text+0x174)=4;
}
sc_campaign_reward mastery_reward(const char* perk) {
    if (!active() || !perk) return {};
    const auto projection=menu().projection();
    if (projection.namespace_id!=save::session().namespace_id()) return {};
    for (uint32_t i=0;i<projection.count;++i) {
        if (!(projection.rows[i].flags&SC_CAMPAIGN_HUB)) continue;
        for (const auto& reward:projection.rewards[i]) if (reward.kind==SC_REWARD_MASTERY && !std::strcmp(reward.unlockable,perk)) return reward;
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
        !memory.copy(reinterpret_cast<uintptr_t>(value.data),bytes,map.size()+1).reason && !std::strcmp(bytes,map.c_str());
}
uintptr_t mapload_for(const StageRoute& route) { return find_material(mapload_registry,route.entry,0); }
void configure_stage_request(uintptr_t request,uint32_t variant,uintptr_t entry) {
    string_assign(request+0x60,"");
    *reinterpret_cast<uint32_t*>(request+0x90)=0;
    *reinterpret_cast<uint32_t*>(request+0x94)=1;
    *reinterpret_cast<uint32_t*>(request+0x98)=variant;
    *reinterpret_cast<uintptr_t*>(request+0xa8)=variant==6?0:entry;
    *reinterpret_cast<uintptr_t*>(request+0xb0)=variant==6?entry:0;
    *reinterpret_cast<uint16_t*>(request+0xb8)=0;
}
bool checkpoint_request(uintptr_t request,uint32_t stage_id) {
    const auto route=stage_route(stage_id);
    if (!route || route->variant!=6) return false;
    const auto declaration=mapload_for(*route);
    if (!declaration || !request_map(request,route->map)) return false;
    configure_stage_request(request,6,declaration);
    return true;
}
uint32_t descriptor_stage(uintptr_t descriptor,const std::string& map,uint32_t variant) {
    if (map=="game/hub/hub" && variant!=6) return 1;
    uintptr_t declaration=0;
    if (variant==6 && (!read(descriptor,0x19d8,declaration) || !declaration)) return 0;
    for (const auto& route:stage_routes) {
        if (map!=route.map || (route.variant==6)!=(variant==6)) continue;
        if (variant!=6 || declaration==mapload_for(route)) return route.id;
    }
    return 0;
}
uintptr_t restart_entry(const sc_campaign_row& row) {
    const auto route=stage_route(row.id);
    if (!route) return 0;
    uintptr_t campaign=0,list=0,source=0; int32_t count=0;
    const auto definitions=campaign_definitions();
    if (!definitions || !read(definitions,0x148,campaign) || !read(campaign,0x1a8,list) ||
        !read(list,0x88,source) || !read(list,0x90,count) || count<1) return 0;
    if (route->mission_index>=static_cast<uint32_t>(count)) return 0;
    const auto entry=source+static_cast<size_t>(route->mission_index)*sizeof(Entry);
    const std::string map=row.map;
    save::NativeString native_name{}; char actual[192]{};
    if (!read(entry,0,native_name) || native_name.length!=static_cast<int32_t>(map.size()) ||
        memory.copy(reinterpret_cast<uintptr_t>(native_name.data),actual,map.size()+1).reason || std::strcmp(actual,map.c_str())) return 0;
    return entry;
}
bool restart_request(uintptr_t request,uint32_t stage_id,const std::string& map) {
    if (!available()) return false;
    const auto projection=menu().projection();
    if (projection.namespace_id!=save::session().namespace_id()) return false;
    for (uint32_t i=0;i<projection.count;++i) {
        const auto& row=projection.rows[i];
        if (row.id!=stage_id || map!=row.map || !(row.flags&SC_CAMPAIGN_UNLOCKED)) continue;
        const auto route=stage_route(stage_id);
        const bool master=route && route->variant==6;
        const auto entry=master?mapload_for(*route):restart_entry(row);
        if (!entry || !request_map(request,map)) return false;
        configure_stage_request(request,master?6:2,entry);
        return true;
    }
    return false;
}
uintptr_t restart_load_request() {
    if (!available()) return 0;
    alignas(16) static std::array<uint8_t,192> request{};
    static bool request_initialized=false;
    const auto address=reinterpret_cast<uintptr_t>(request.data());
    if (!request_initialized) {
        for (size_t offset:{size_t(0),size_t(0x30),size_t(0x60)}) string_init(address+offset);
        string_assign(address,"AUTOSAVE0");
        request_initialized=true;
    }
    return address;
}
bool restart_mission_request(uintptr_t request,std::string& destination,uint32_t* stage_id) {
    const auto restart=save::session().campaign_run.snapshot();
    if (!restart_request(request,restart.restart_stage_id,restart.restart_map)) return false;
    destination=restart.restart_map;
    if (stage_id) *stage_id=restart.restart_stage_id;
    return true;
}
bool admit_mission_entry(uintptr_t request,int32_t subtype,uintptr_t entry,uint32_t index,const sc_campaign_row& row) {
    const auto route=stage_route(row.id);
    if (subtype==6) return route && route->variant==6 && entry==mapload_for(*route);
    if (entry!=reinterpret_cast<uintptr_t>(&entries[index])) return false;
    return !route || route->variant!=6 || checkpoint_request(request,row.id);
}
bool read_mission_destination(uintptr_t request,int32_t subtype,uintptr_t& entry,char (&map)[192]) {
    save::NativeString name{};
    if (!read(request,subtype==6?0xb0:0xa8,entry) || !read(request,0x30,name) || name.length<1 || name.length>=192 ||
        memory.copy(reinterpret_cast<uintptr_t>(name.data),map,size_t(name.length)+1).reason || map[name.length]) return false;
    return true;
}
bool mission_request(uintptr_t request,std::string& destination,uint32_t* stage_id) {
    int32_t subtype=0;
    if (!read(request,0x98,subtype)) return false;
    if (subtype!=2 && subtype!=6) return true;
    uintptr_t entry=0;
    char map[192]{};
    if (!read_mission_destination(request,subtype,entry,map)) return false;
    const auto selected=menu().focus_id();
    for (uint32_t i=0;i<shown.count;++i) {
        const auto& row=shown.rows[i];
        if (row.id!=selected || !(row.flags&SC_CAMPAIGN_UNLOCKED) || std::strcmp(map,row.map)) continue;
        if (!admit_mission_entry(request,subtype,entry,i,row)) return false;
        if (stage_id) *stage_id=row.id;
        destination=map; return true;
    }
    return false;
}
const char* resume_label(const save::CampaignSnapshot& snapshot) {
    if (snapshot.restart_pending) return player_strings::menu_restart_stage;
    return snapshot.map=="game/hub/hub"?player_strings::menu_return_to_fortress:player_strings::menu_resume_checkpoint;
}
bool campaign_action_widgets(uintptr_t screen,uintptr_t list,uintptr_t& resume,uintptr_t& choose,uintptr_t& focused) {
    uintptr_t children=0;
    int32_t count=0,index=-1;
    if (!read(screen,0x118,resume) || !resume || !read(screen,0x120,choose) || !choose ||
        !read(list,0xa0,children) || !read(list,0xa8,count) || !read(list,0x150,index)) return false;
    bool contains_choose=false;
    for (int32_t i=0;i<count;++i) {
        uintptr_t child=0; if (!read(children,size_t(i)*8,child)) return false;
        contains_choose|=child==choose;
        if (i==index) focused=child;
    }
    return contains_choose;
}
void label_campaign_action(uintptr_t widget,const char* wanted) {
        save::NativeString current{}; char text[96]{};
        if (read(widget,0x188,current) && current.length>=0 && current.length<96 && current.data &&
            !memory.copy(reinterpret_cast<uintptr_t>(current.data),text,size_t(current.length)+1).reason && !std::strcmp(text,wanted)) return;
        string_assign(widget+0x188,wanted);
}
void select_campaign_action(uintptr_t list,uintptr_t choose,uintptr_t focused,uintptr_t resume,bool hub,bool entered) {
    uintptr_t table=0,select=0;
    int32_t choose_state=0;
    const bool projected=menu().projection().count!=0;
    if (!read(choose,0x154,choose_state)) return;
    const bool became_available=projected && choose_state==5;
    if (became_available) widget_state(choose,focused==choose?3:1);
    if (projected && hub && (entered || (became_available && focused==resume)) && read(list,0,table) && read(table,0x70,select) && select)
        reinterpret_cast<void(*)(uintptr_t,uintptr_t)>(select)(list,choose);
}
void present_campaign_actions(uintptr_t screen,bool entered) {
    if (!available() || !active() || !save::session().accepts_requests()) return;
    int32_t state=-1;
    if (!read(screen,0x108,state) || state!=2) return;
    auto snapshot=save::session().campaign_run.snapshot();
    if (snapshot.restart_pending) snapshot.map=snapshot.restart_map;
    safe_present_save_preview(screen,snapshot);
    if (!snapshot.checkpoint) return;
    const auto list=list_for(screen);
    uintptr_t resume=0,choose=0,focused=0;
    if (!campaign_action_widgets(screen,list,resume,choose,focused)) return;
    const bool hub=snapshot.map=="game/hub/hub";
    label_campaign_action(resume,resume_label(snapshot));
    label_campaign_action(choose,player_strings::menu_choose_mission);
    select_campaign_action(list,choose,focused,resume,hub,entered);
}
void pump_campaign(uintptr_t screen,void (*pump)(uintptr_t)) {
    uintptr_t declaration=0;
    uint32_t master_levels=0;
    const bool hide=active() && read(screen,0x100,declaration) && declaration && read(declaration,0x1b8,master_levels);
    __try {
        if (hide) *reinterpret_cast<uint32_t*>(declaration+0x1b8)=0;
        pump(screen);
    } __finally {
        if (hide) *reinterpret_cast<uint32_t*>(declaration+0x1b8)=master_levels;
    }
}
#ifdef SC_NATIVE_TESTING
Update test_pump=nullptr;
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
    test_pump=c.campaign_pump;
    if (c.find_decl) find_material=c.find_decl;
    ready.store(true,std::memory_order_release);
}
void test_populate(uintptr_t screen,uintptr_t list) { populate(screen,list); }
void test_focus(uintptr_t screen) { focus(screen); }
void test_details_update(uintptr_t details) { render_details(details); }
void test_load(uintptr_t screen,int index) { load(screen,index); }
void test_update(uintptr_t screen) { update(screen); }
void test_root_navigation(uintptr_t screen,uint8_t reset) { root_navigation(screen,reset); }
void test_root_campaign(uintptr_t screen,uintptr_t declaration) { root_campaign(screen,declaration); }
void test_campaign_pump(uintptr_t screen) { pump_campaign(screen,test_pump); }
void test_meter_allocate(uintptr_t meter,uintptr_t encounters,uintptr_t decl) { meter_allocate_detour(meter,encounters,decl); }
void test_sprite_visibility(uintptr_t sprite,uint32_t visible,uint32_t flag) { sprite_visibility_detour(sprite,visible,flag,_ReturnAddress()); }
void test_meter_update(uintptr_t meter) { meter_update_detour(meter); }
#endif
#include "physical_contact_observer.h"

void populate_entry(uintptr_t screen,uintptr_t list) {
    const auto lease = menu_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_populate)>(lease.original)(screen, list);
        return populate(screen, list);
    } __finally { menu_hooks.leave(lease); }
}
void focus_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(1);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_focus)>(lease.original)(screen);
        return focus(screen);
    } __finally { menu_hooks.leave(lease); }
}
void load_entry(uintptr_t screen,int index) {
    const auto lease = menu_hooks.enter(2);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_load)>(lease.original)(screen, index);
        return load(screen, index);
    } __finally { menu_hooks.leave(lease); }
}
bool is_available_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(3);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_available)>(lease.original)(screen);
        return is_available(screen);
    } __finally { menu_hooks.leave(lease); }
}
void update_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(4);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_update)>(lease.original)(screen);
        return update(screen);
    } __finally { menu_hooks.leave(lease); }
}
void root_navigation_entry(uintptr_t screen,uint8_t reset) {
    const auto lease = menu_hooks.enter(5);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_root_navigation)>(lease.original)(screen, reset);
        return root_navigation(screen, reset);
    } __finally { menu_hooks.leave(lease); }
}
void root_campaign_entry(uintptr_t screen,uintptr_t declaration) {
    const auto lease = menu_hooks.enter(6);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_root_campaign)>(lease.original)(screen, declaration);
        return root_campaign(screen, declaration);
    } __finally { menu_hooks.leave(lease); }
}
void render_details_entry(uintptr_t details) {
    const auto lease = menu_hooks.enter(7);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(details_update)>(lease.original)(details);
        return render_details(details);
    } __finally { menu_hooks.leave(lease); }
}
void meter_allocate_detour_entry(uintptr_t meter,uintptr_t encounters,uintptr_t decl) {
    const auto lease = menu_hooks.enter(8);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_meter_allocate)>(lease.original)(meter, encounters, decl);
        return meter_allocate_detour(meter, encounters, decl);
    } __finally { menu_hooks.leave(lease); }
}
void sprite_visibility_detour_entry(uintptr_t sprite,uint32_t visible,uint32_t flag) {
    const auto caller = _ReturnAddress();
    const auto lease = menu_hooks.enter(9);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_sprite_visibility)>(lease.original)(sprite, visible, flag);
        return sprite_visibility_detour(sprite, visible, flag, caller);
    } __finally { menu_hooks.leave(lease); }
}
void meter_update_detour_entry(uintptr_t meter) {
    const auto lease = menu_hooks.enter(10);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_meter_update)>(lease.original)(meter);
        return meter_update_detour(meter);
    } __finally { menu_hooks.leave(lease); }
}
void userinfo_point_entry(uintptr_t widget,int kind,uintptr_t material,int count,int width,int delta) {
    const auto lease = menu_hooks.enter(11);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_userinfo_point)>(lease.original)(widget, kind, material, count, width, delta);
        return userinfo_point(widget, kind, material, count, width, delta);
    } __finally { menu_hooks.leave(lease); }
}
void hud_score_init_entry(uintptr_t score) {
    const auto lease = menu_hooks.enter(12);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hud_score_init)>(lease.original)(score);
        return hud_score_init(score);
    } __finally { menu_hooks.leave(lease); }
}
void end_items_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(13);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_end_items)>(lease.original)(screen);
        return end_items(screen);
    } __finally { menu_hooks.leave(lease); }
}
void end_combat_init_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(14);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_end_combat_init)>(lease.original)(screen);
        return end_combat_init(screen);
    } __finally { menu_hooks.leave(lease); }
}
void dossier_map_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(15);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_dossier_map)>(lease.original)(screen);
        return dossier_map(screen);
    } __finally { menu_hooks.leave(lease); }
}
void challenge_card_update_entry(uintptr_t widget,uintptr_t context,uintptr_t flags) {
    const auto lease = menu_hooks.enter(16);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_challenge_card_update)>(lease.original)(widget, context, flags);
        return challenge_card_update(widget, context, flags);
    } __finally { menu_hooks.leave(lease); }
}
void start_show_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(17);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_start_show)>(lease.original)(screen);
        return start_show(screen);
    } __finally { menu_hooks.leave(lease); }
}
void boss_update_entry(uintptr_t screen,uintptr_t event) {
    const auto lease = menu_hooks.enter(18);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_boss_update)>(lease.original)(screen, event);
        return boss_update(screen, event);
    } __finally { menu_hooks.leave(lease); }
}
void hud_challenge_update_entry(uintptr_t widget) {
    const auto lease = menu_hooks.enter(19);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hud_challenge_update)>(lease.original)(widget);
        return hud_challenge_update(widget);
    } __finally { menu_hooks.leave(lease); }
}
void eol_challenge_update_entry(uintptr_t widget) {
    const auto lease = menu_hooks.enter(20);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_eol_challenge_update)>(lease.original)(widget);
        return eol_challenge_update(widget);
    } __finally { menu_hooks.leave(lease); }
}
void category_counts_entry(uintptr_t category) {
    const auto lease = menu_hooks.enter(21);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_category_counts)>(lease.original)(category);
        return category_counts(category);
    } __finally { menu_hooks.leave(lease); }
}
void hud_score_update_entry(uintptr_t score) {
    const auto lease = menu_hooks.enter(23);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hud_score_update)>(lease.original)(score);
        return hud_score_update(score);
    } __finally { menu_hooks.leave(lease); }
}
void challenges_show_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(24);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_challenges_show)>(lease.original)(screen);
        return challenges_show(screen);
    } __finally { menu_hooks.leave(lease); }
}
uintptr_t challenges_action_entry(uintptr_t screen,uintptr_t action,uintptr_t event,uintptr_t source) {
    const auto lease = menu_hooks.enter(25);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_challenges_action)>(lease.original)(screen, action, event, source);
        return challenges_action(screen, action, event, source);
    } __finally { menu_hooks.leave(lease); }
}
void mission_tab_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(26);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_mission_tab)>(lease.original)(screen);
        return mission_tab(screen);
    } __finally { menu_hooks.leave(lease); }
}
void end_challenges_show_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(27);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_end_challenges_show)>(lease.original)(screen);
        return end_challenges_show(screen);
    } __finally { menu_hooks.leave(lease); }
}
void secret_rewards_show_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(28);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_secret_rewards_show)>(lease.original)(screen);
        return secret_rewards_show(screen);
    } __finally { menu_hooks.leave(lease); }
}
void escalation_rewards_show_entry(uintptr_t screen) {
    const auto lease = menu_hooks.enter(29);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_escalation_rewards_show)>(lease.original)(screen);
        return escalation_rewards_show(screen);
    } __finally { menu_hooks.leave(lease); }
}
void reward_card_entry(uintptr_t widget,uintptr_t declaration) {
    const auto lease = menu_hooks.enter(30);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_reward_card)>(lease.original)(widget, declaration);
        return reward_card(widget, declaration);
    } __finally { menu_hooks.leave(lease); }
}
void escalation_card_entry(uintptr_t widget,uintptr_t declaration) {
    const auto lease = menu_hooks.enter(31);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_escalation_card)>(lease.original)(widget, declaration);
        return escalation_card(widget, declaration);
    } __finally { menu_hooks.leave(lease); }
}
void escalation_perk_entry(uintptr_t screen,const char* root_name,const void* pack) {
    const auto lease = menu_hooks.enter(32);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_escalation_perk)>(lease.original)(screen, root_name, pack);
        return escalation_perk(screen, root_name, pack);
    } __finally { menu_hooks.leave(lease); }
}
bool trigger_gate_entry(uintptr_t entity) {
    const auto lease = menu_hooks.enter(22);
    __try {
        if (!lease.committed) return reinterpret_cast<Available>(lease.original)(entity);
        return trigger_gate(entity);
    } __finally { menu_hooks.leave(lease); }
}

bool validate_native_targets(save::Installation& record,engine::Memory& source_memory,
                              const engine::Image& image,HANDLE stop,const std::array<native::Target,46>& targets) {
    if (!image.contains(0x5e05200,sizeof(uintptr_t),IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_EXECUTE)) return false;
    // the populate call must target the qualified list-copy function
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
        constexpr unsigned leaves[]{5,10,16,23,31};
        if (std::find(std::begin(leaves),std::end(leaves),i)!=std::end(leaves)) {
            // these leaf callees have no unwind entry and are not detoured
            std::array<uint8_t,32> actual{};
            if (!image.contains(rva,actual.size(),IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ,0) ||
                source_memory.copy(targets[i].address,actual.data(),actual.size()).reason || actual!=targets[i].bytes) return false;
        } else if (native::validate_recorded(record,source_memory,image,targets[i],stop,GetTickCount64()+3000,6,i)) return false;
    }
    return true;
}
static bool install_hooks(const engine::Binding& binding,HANDLE stop, const std::array<native::Target,46>& targets,
                          decltype(&MH_EnableHook) enable_hook=MH_EnableHook) {
    menu_enable_operation = enable_hook;
    meter_visibility_return=binding.image.base+0x1116e07;
    hud_score_vtable=binding.image.base+0x2d01de8;
    void* detours[]={reinterpret_cast<void*>(populate_entry),reinterpret_cast<void*>(focus_entry),reinterpret_cast<void*>(load_entry),
        reinterpret_cast<void*>(is_available_entry),reinterpret_cast<void*>(update_entry),
        reinterpret_cast<void*>(root_navigation_entry),reinterpret_cast<void*>(root_campaign_entry),
        reinterpret_cast<void*>(render_details_entry), reinterpret_cast<void*>(meter_allocate_detour_entry),
        reinterpret_cast<void*>(sprite_visibility_detour_entry),
        reinterpret_cast<void*>(meter_update_detour_entry),reinterpret_cast<void*>(userinfo_point_entry),
        reinterpret_cast<void*>(hud_score_init_entry),reinterpret_cast<void*>(end_items_entry),reinterpret_cast<void*>(end_combat_init_entry),
        reinterpret_cast<void*>(dossier_map_entry),reinterpret_cast<void*>(challenge_card_update_entry),
        reinterpret_cast<void*>(start_show_entry),reinterpret_cast<void*>(boss_update_entry),
        reinterpret_cast<void*>(hud_challenge_update_entry),reinterpret_cast<void*>(eol_challenge_update_entry),reinterpret_cast<void*>(category_counts_entry),
        reinterpret_cast<void*>(trigger_gate_entry),reinterpret_cast<void*>(hud_score_update_entry)};
    constexpr unsigned hooked[]={0,1,2,3,4,11,12,9,15,16,17,27,28,29,30,31,32,34,35,38,39,41,42,45};
    void** originals[]{reinterpret_cast<void**>(&original_populate), reinterpret_cast<void**>(&original_focus),
        reinterpret_cast<void**>(&original_load), reinterpret_cast<void**>(&original_available),
        reinterpret_cast<void**>(&original_update), reinterpret_cast<void**>(&original_root_navigation),
        reinterpret_cast<void**>(&original_root_campaign), reinterpret_cast<void**>(&details_update),
        reinterpret_cast<void**>(&original_meter_allocate), reinterpret_cast<void**>(&original_sprite_visibility),
        reinterpret_cast<void**>(&original_meter_update), reinterpret_cast<void**>(&original_userinfo_point),
        reinterpret_cast<void**>(&original_hud_score_init), reinterpret_cast<void**>(&original_end_items),
        reinterpret_cast<void**>(&original_end_combat_init), reinterpret_cast<void**>(&original_dossier_map),
        reinterpret_cast<void**>(&original_challenge_card_update), reinterpret_cast<void**>(&original_start_show),
        reinterpret_cast<void**>(&original_boss_update), reinterpret_cast<void**>(&original_hud_challenge_update),
        reinterpret_cast<void**>(&original_eol_challenge_update), reinterpret_cast<void**>(&original_category_counts),
        reinterpret_cast<void**>(&original_trigger_gate), reinterpret_cast<void**>(&original_hud_score_update)};
    std::vector<hooks::HookSpec> specs;
    for (unsigned i = 0; i < 24; ++i) specs.push_back({targets[hooked[i]].address, detours[i], originals[i], hooked[i],
            static_cast<uint32_t>(targets[hooked[i]].address - binding.image.base)});

    campaign_definitions=reinterpret_cast<CampaignDefinitions>(targets[13].address);
    sprite_changed=reinterpret_cast<Update>(targets[14].address);
    string_init=reinterpret_cast<StringInit>(targets[5].address); string_assign=reinterpret_cast<StringAssign>(targets[6].address);
    native_completed=reinterpret_cast<Completed>(targets[7].address); list_assign=reinterpret_cast<ListAssign>(targets[8].address);

    widget_state=reinterpret_cast<WidgetState>(targets[10].address);

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
    mapload_registry=binding.image.base+builds::steam_20260818().mapload_registry;
    swf_create_material=reinterpret_cast<SwfCreateMaterial>(targets[43].address);
    swf_color=reinterpret_cast<SwfColor>(targets[44].address);

    end_items_category_vtable=binding.image.base+0x2d10d78;
    struct OptionalPageHook { uint32_t rva; const char* bytes; void* hook; void** original; };
    const OptionalPageHook pages[]={
        {0xf42930,"488bc4565741554883ec7048895810488bf14c896020e885a72800488bcee85d",reinterpret_cast<void*>(challenges_show_entry),reinterpret_cast<void**>(&original_challenges_show)},
        {0xf40bf0,"40535556574881ece8010000488b05d5dd26034833c448898424d0010000488b",reinterpret_cast<void*>(challenges_action_entry),reinterpret_cast<void**>(&original_challenges_action)},
        {0xf42de0,"405741564157b8b0400000e8b08f9201482be0488b05debb26034833c4488984",reinterpret_cast<void*>(mission_tab_entry),reinterpret_cast<void**>(&original_mission_tab)},
        {0xf58890,"40574883ec20488bf9e832482700488b07488bcfff50704885c00f84e1000000",reinterpret_cast<void*>(end_challenges_show_entry),reinterpret_cast<void**>(&original_end_challenges_show)},
        {0xf79720,"40555356488d6c24f04881ec10010000488b05a15223034833c4488945f0488b",reinterpret_cast<void*>(secret_rewards_show_entry),reinterpret_cast<void**>(&original_secret_rewards_show)},
        {0xf6b340,"405556488dac2488fcffff4881ec78040000488b057f3624034833c448898560",reinterpret_cast<void*>(escalation_rewards_show_entry),reinterpret_cast<void**>(&original_escalation_rewards_show)},
        {0xf67a20,"b868400000e876439001482be0488b05a46f24034833c4488984243040000048",reinterpret_cast<void*>(reward_card_entry),reinterpret_cast<void**>(&original_reward_card)},
        {0xf6c7f0,"48895c240848896c24104889742418574883ec50488bda488bf9e811b2ffff8b",reinterpret_cast<void*>(escalation_card_entry),reinterpret_cast<void**>(&original_escalation_card)},
        {0xf6ba10,"48895c2420564883ec50488bd9498bf0488b89900200004885c90f843b010000",reinterpret_cast<void*>(escalation_perk_entry),reinterpret_cast<void**>(&original_escalation_perk)},
        {0x185d670,"48895c2408574883ec30488bd9488bfa488b49104885c97516394b207e56488b",nullptr,reinterpret_cast<void**>(&cached_text)},
        {0x185d1a0,"40534883ec20488b4110488bd94885c075143941207f064883c4205bc3e8ae1f",nullptr,reinterpret_cast<void**>(&cached_sprite)}};
    engine::LocalMemory optional_memory;
    for (const auto& page:pages) {
        native::Target target; target.address=binding.image.base+page.rva;
        for (size_t n=0;n<32;++n) { unsigned value=0; sscanf_s(page.bytes+n*2,"%2x",&value); target.bytes[n]=static_cast<uint8_t>(value); }
        auto reason=native::validate_target(optional_memory,binding.image,target,stop,GetTickCount64()+3000);
        if (page.hook) specs.push_back({target.address, page.hook, page.original, SC_INSTALL_UNKNOWN, page.rva, !reason});
        else if (!reason) *page.original = reinterpret_cast<void*>(target.address);
        save::session().btrace.record(save::BStage::dossier_challenges, reason ? save::BStatus::refused : save::BStatus::succeeded,"campaign_page_binding",0,
            {{"rva",page.rva},{"reason",reason}});
    }
    if (!menu_hooks.install(specs.data(), specs.size(), stop)) { ready.store(false, std::memory_order_release); return false; }
    ready.store(true,std::memory_order_release); return true;
}
bool install(const engine::Binding& binding,HANDLE stop) {
    if (!save::session().campaign_requested()) return true;
    return install_hooks(binding,stop,builds::steam_20260818().menu_targets(binding.image.base));
}
#ifdef SC_NATIVE_TESTING
bool test_install_hooks(const std::array<native::Target,46>& targets,decltype(&MH_EnableHook) enable_hook) {
    engine::Binding binding{};
    return install_hooks(binding,nullptr,targets,enable_hook);
}
#endif
}
