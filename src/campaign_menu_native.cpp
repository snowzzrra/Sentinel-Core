#include "campaign_menu_native.h"
#include "campaign_menu.h"
#include "fast_travel.h"
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
Update original_dossier_points=nullptr;
Update original_dossier_map=nullptr;
Update original_dossier_challenges=nullptr;
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
using FindMaterial=uintptr_t(*)(uintptr_t,const char*,int);
using MapManager=uintptr_t(*)();
using MapLookup=uintptr_t(*)(uintptr_t,const char*,uint32_t,uint8_t);
using Localize=const char*(*)(const uint32_t*);
SwfLookup swf_lookup=nullptr;
SwfGet swf_sprite=nullptr,swf_text=nullptr;
SwfRelease swf_release=nullptr;
SwfText swf_set_text=nullptr;
SwfMaterial swf_set_material=nullptr;
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
uintptr_t swf_child(uintptr_t parent,const char* name,bool text=false) {
    if (!parent) return 0;
    SwfValue value{};
    const auto found=swf_lookup(parent,&value,name);
    const auto child=found?(text?swf_text(found):swf_sprite(found)):0;
    if (value.type==2 || value.type==8) swf_release(&value);
    return child;
}
void present_logo(uintptr_t screen,bool start) {
    uintptr_t table=0,method=0;
    if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
    const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
    const auto logo=swf_child(root,start?"Logo_Base_Gradient":"doom_logo_base");
    if (!logo) return;
    original_sprite_visibility(logo,1,1);
    const char* const start_layers[]={"Logo_Base_Glow","Logo_Base_Scanlines_A",
        "Logo_Base_Scanlines_B","Logo_Base_White","eternal"};
    const char* const main_layers[]={"doom_outer_glow","doom_scan_lines","eternal",
        "eternal_backplate_base","eternal_backplate_inner_glow",
        "eternal_backplate_outer_glow","eternal_backplate_scanlines",
        "eternal_font_base","eternal_font_glow"};
    if (start) {
        for (const auto name:start_layers)
            if (const auto layer=swf_child(root,name)) original_sprite_visibility(layer,0,1);
    } else {
        for (const auto name:main_layers)
            if (const auto layer=swf_child(root,name)) original_sprite_visibility(layer,0,1);
    }
    static uintptr_t adjusted_start=0,adjusted_main=0;
    auto& adjusted=start?adjusted_start:adjusted_main;
    if (adjusted==logo) return;
    int32_t slot=-1; uintptr_t context=0,transforms=0;
    if (!read(logo,0xc,slot) || slot<0 || !read(logo,0x10,context) || !context ||
        !read(context,0x80,transforms) || !transforms) return;
    const auto matrix=transforms+static_cast<uintptr_t>(slot)*0x40;
    float xx=0,yy=0,tx=0,min_x=0,max_x=0;
    if (!read(matrix,0x4,xx) || !read(matrix,0x8,yy) || !read(matrix,0x14,tx) ||
        !read(logo,0xa8,min_x) || !read(logo,0xb0,max_x) ||
        !std::isfinite(xx) || !std::isfinite(yy) || !std::isfinite(tx) ||
        !std::isfinite(min_x) || !std::isfinite(max_x) || xx<=0 || yy<=0 || max_x<=min_x) return;
    const float correction=start?(1080.f/550.f)/(2000.f/980.f):(724.f/1360.f);
    *reinterpret_cast<float*>(matrix+0x4)=xx*correction;
    *reinterpret_cast<float*>(matrix+0x14)=tx+xx*(1.f-correction)*(min_x+max_x)*0.5f;
    adjusted=logo;
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
    const auto name=swf_child(bar,"mapName",true);
    uint32_t title_id=0;
    const auto title=read(declaration,0x88,title_id) && title_id
        ? localize(reinterpret_cast<const uint32_t*>(declaration+0x88)) : "";
    if (name) swf_set_text(name,title ? title : "");
}
void safe_present_save_preview(uintptr_t screen,const save::CampaignSnapshot& snapshot) {
    __try { present_save_preview(screen,snapshot); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "campaign_save_preview_widget_fault",0,{},screen);
    }
}
void dossier_points(uintptr_t screen) {
    original_dossier_points(screen);
    if (!active()) return;
    __try {
        uintptr_t table=0,method=0;
        if (read(screen,0,table) && read(table,0x70,method) && method) {
            const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
            if (const auto mastery=swf_child(root,"masteryTokens"))
                original_sprite_visibility(mastery,0,1);
            if (const auto praetor=swf_child(root,"suitPoints"))
                original_sprite_visibility(praetor,0,1);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "dossier_mastery_widget_fault",0,{},screen);
    }
}
void present_dossier_map(uintptr_t screen) {
        uintptr_t table=0,method=0;
        if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
        const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
        const auto content=swf_child(root,"items_collected_content");
        if (!content) return;
        const auto campaign=save::session().campaign_run.snapshot();
        const auto projection=menu().projection();
        const sc_campaign_summary* summary=nullptr;
        for (uint32_t i=0;i<projection.count;++i)
            if ((projection.rows[i].flags&SC_CAMPAIGN_DETAILS) && campaign.map==projection.rows[i].map) {
                summary=&projection.summaries[i]; break;
            }
        original_sprite_visibility(content,summary && summary->known==1,1);
        if (!summary || summary->known!=1) return;
        if (const auto items=swf_child(content,"items")) original_sprite_visibility(items,0,1);
        auto title=swf_child(content,"txtVal",true);
        if (!title) title=swf_child(swf_child(content,"title"),"txtVal",true);
        if (title) {
            char label[64]{};
            std::snprintf(label,sizeof(label),"ITEMS FOUND  %u/%u",summary->found,summary->total);
            swf_set_text(title,label);
        }
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
void present_dossier_challenges(uintptr_t screen) {
        uintptr_t table=0,method=0;
        if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
        const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
        const auto mission=swf_child(root,"mission");
        if (!mission) return;
        const auto campaign=save::session().campaign_run.snapshot();
        const auto projection=menu().projection();
        const sc_campaign_summary* summary=nullptr;
        for (uint32_t i=0;i<projection.count;++i)
            if ((projection.rows[i].flags&SC_CAMPAIGN_DETAILS) && campaign.map==projection.rows[i].map) {
                summary=&projection.summaries[i]; break;
            }
        original_sprite_visibility(mission,summary && summary->known==1,1);
        if (!summary || summary->known!=1) return;
        for (unsigned i=0;i<3;++i) {
            char name[24]{};
            std::snprintf(name,sizeof(name),"mission_challenge_%u",i);
            const auto row=swf_child(mission,name);
            if (!row) continue;
            const auto& challenge=summary->challenges[i];
            original_sprite_visibility(row,challenge.required!=0,1);
            if (!challenge.required) continue;
            const auto card=swf_child(row,"card");
            if (const auto complete=swf_child(card,"complete"))
                original_sprite_visibility(complete,challenge.checked!=0,1);
            if (const auto incomplete=swf_child(card,"incomplete"))
                original_sprite_visibility(incomplete,challenge.checked==0,1);
            const auto progress=swf_child(card,"progress");
            const auto current=swf_child(swf_child(progress,"currentText"),"txtVal",true);
            const auto maximum=swf_child(swf_child(progress,"maxText"),"txtVal",true);
            char value[32]{};
            if (current && maximum) {
                std::snprintf(value,sizeof(value),"%u%s",challenge.found,challenge.checked ? " \xe2\x9c\x93" : "");
                swf_set_text(current,value);
                std::snprintf(value,sizeof(value),"%u",challenge.required);
                swf_set_text(maximum,value);
            } else if (const auto description=swf_child(swf_child(card,"challengeDesc"),"txtVal",true)) {
                std::snprintf(value,sizeof(value),"%u/%u%s",challenge.found,challenge.required,
                              challenge.checked ? "  \xe2\x9c\x93" : "");
                swf_set_text(description,value);
            }
        }
}
void dossier_challenges(uintptr_t screen) {
    original_dossier_challenges(screen);
    if (!active()) return;
    __try { present_dossier_challenges(screen); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "dossier_challenges_widget_fault",0,{},screen);
    }
}
void hud_score_init(uintptr_t score) {
    original_hud_score_init(score);
    if (!active()) return;
    __try {
        uintptr_t widget=0,sprite=0;
        if (read(score,0xf8,widget) && widget && read(widget,0x18,sprite) && sprite)
            original_sprite_visibility(sprite,0,1);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "hud_corruption_widget_fault",0,{},score);
    }
}
void present_found_counts(uintptr_t root,const sc_campaign_summary& summary) {
    const auto header=swf_child(root,"header");
    if (!header) return;
    original_sprite_visibility(header,summary.known==1,1);
    if (summary.known!=1) return;
    const auto write_count=[&](const char* name,uint32_t count) {
        const auto field=swf_child(swf_child(header,name),"txtVal",true);
        if (!field) return;
        char value[24]{};
        std::snprintf(value,sizeof(value),"%u",count);
        swf_set_text(field,value);
    };
    write_count("headerValueCurrent",summary.found);
    write_count("headerValueMax",summary.total);
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
    if (!summary || !summary->known) return;
    uintptr_t category=0,vtable=0,root=0,screen_table=0,method=0;
    if (!read(screen,0x118,category) || !category || !read(category,0,vtable) ||
        vtable!=end_items_category_vtable || !read(category,0x18,root) || !root ||
        !read(screen,0,screen_table) || !read(screen_table,0x70,method) || !method) return;
    const auto screen_root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
    if (const auto item_list=swf_child(swf_child(screen_root,"itemList"),"itemsFound"))
        original_sprite_visibility(item_list,0,1);
    present_found_counts(root,*summary);
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
void boss_update(uintptr_t screen,uintptr_t event) {
    original_boss_update(screen,event);
    if (!active()) return;
    __try {
        uintptr_t table=0,method=0;
        if (!read(screen,0,table) || !read(table,0x70,method) || !method) return;
        const auto root=reinterpret_cast<uintptr_t(*)(uintptr_t)>(method)(screen);
        const auto tier=swf_child(root,"tier1_5");
        for (const auto parent:{root,tier,swf_child(root,"tier2")}) {
            for (const char* name:{"reward10count_c","reward5count_c","reward3count_c",
                                   "description","icon_c","combat_icon","icon_g","frameicon_b"})
                if (const auto child=swf_child(parent,name)) original_sprite_visibility(child,0,1);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        save::session().btrace.record(save::BStage::profile_output,save::BStatus::blocked,
                                      "boss_reward_widget_fault",0,{},screen);
    }
}
void present_details(uintptr_t details) {
    const auto list=list_for(shown_screen);
    int32_t index=-1;
    if (!list || !read(list,0x150,index) || index<0 || static_cast<uint32_t>(index)>=shown.count ||
        !(shown.rows[index].flags&SC_CAMPAIGN_DETAILS)) return;
    const auto& summary=shown.summaries[index];
    if (!summary.known) return;
    uintptr_t details_root=0;
    if (!read(details,0x18,details_root) || !details_root) return;
    const auto challenge_root=swf_child(details_root,"challenges");
    if (const auto title=swf_child(swf_child(challenge_root,"_Hheader"),"txtVal",true)) {
        const uint32_t encoded=summary.band>>8;
        char label[64]{};
        if (encoded) {
            const uint32_t centi_cr=encoded-1;
            std::snprintf(label,sizeof(label),"CHALLENGES  |  LOADOUT CR %u.%02u",
                          centi_cr/100,centi_cr%100);
        } else {
            std::snprintf(label,sizeof(label),"CHALLENGES");
        }
        swf_set_text(title,label);
    }
    uintptr_t category=0,root=0; uint32_t kind=0;
    if (read(details,0x1b8,category) && category && read(category,0x188,kind) && kind==6 &&
        read(category,0x18,root) && root) {
        original_sprite_visibility(root,summary.known==1 || (summary.band&255)!=0,1);
        {
            if (const auto rating=swf_child(swf_child(root,"itemList"),"itemsFound")) {
                const uint32_t tier=summary.band&255;
                static constexpr const char* donor[]={"",
                    "textures/swf_images/difficulty/too_young.png",
                    "textures/swf_images/difficulty/hurt_me_plenty.png",
                    "textures/swf_images/difficulty/ultra_violence.png",
                    "textures/swf_images/difficulty/nightmare.png"};
                const auto icon=tier && tier<=4 && find_material ?
                    find_material(material_manager,donor[tier],0) : 0;
                original_sprite_visibility(rating,icon!=0,1);
                if (icon) {
                    if (const auto sprite=swf_child(rating,"itemIcon"))
                        swf_set_material(sprite,icon,0);
                    auto name=swf_child(rating,"itemName",true);
                    if (!name) name=swf_child(swf_child(rating,"itemName"),"txtVal",true);
                    if (name)
                        swf_set_text(name,"RELATIVE DIFFICULTY");
                    auto value=swf_child(rating,"itemValue",true);
                    if (!value) value=swf_child(swf_child(rating,"itemValue"),"txtVal",true);
                    if (value) {
                        char label[12]{};
                        std::snprintf(label,sizeof(label),"%u/4",tier);
                        swf_set_text(value,label);
                    }
                }
            }
            present_found_counts(root,summary);
        }
    }
    if (summary.known!=1) return;
    for (const auto& challenge:summary.challenges) {
        const auto suffix=std::strrchr(challenge.unlockable,'_');
        if (!suffix || (std::strcmp(suffix,"_1") && std::strcmp(suffix,"_2") &&
                        std::strcmp(suffix,"_3"))) continue;
        const unsigned slot=static_cast<unsigned>(suffix[1]-'1');
        uint32_t present=0; char title=0;
        if (!read(details,0x1d0+slot*0x1b0,present) || !present ||
            !read(details,0x1d4+slot*0x1b0,title) || !title) continue;
        char name[]="item0"; name[4]+=static_cast<char>(slot);
        const auto field=swf_child(swf_child(swf_child(challenge_root,name),"progress"),"txtVal",true);
        if (!field) continue;
        char progress[32]{};
        std::snprintf(progress,sizeof(progress),"%s%u/%u",
                      challenge.checked ? "\xE2\x9C\x93 " : "",challenge.found,challenge.required);
        swf_set_text(field,progress);
    }
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
        retire_ap_meter();
        details_update(details); return;
    }
    uintptr_t meter=0;
    if (read(details,0x6f8,meter) && meter) {
        ap_meter_published.store(meter,std::memory_order_release);
    }
    int32_t map_length=0;
    if (read(details,0x190,map_length) && map_length==0) {
        details_update(details); return;
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
std::array<native::Target,36> native_targets(uintptr_t base) {
    constexpr uint32_t rvas[]={0x10d2c00,0x10d42f0,0x10d27b0,0x10d1c50,0x10d3140,
        0x3fa8e0,0x3faff0,0x10d1cf0,0x43c070,0x1116c50,0x159c280,
        0x10e08f0,0x10dc420,0x18071d0,0x1857110,0x0f9bd70,0x1864430,
        0x0f9c000,0x185c150,0x184e470,0x184e4b0,0x184e3b0,0x186db00,
        0x17a9660,0x1d69e00,0x360bb0,0x1863b00,0x0f402d0,0x0effea0,0x0f5cfe0,0x0f59270,
        0x0f489c0,0x0f42930,0x17aa5d0,0x10ef3e0,0x0f30810};
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
        "4053b880400000e8c4ba9201482be0488b05f2e626034833c448898424704000",
        "40534883ec300fb791d00000004c8d0d2cffffff488bd948894c2420488b0d45",
        "48895c240848896c2410488974241848897c242041564883ec20488bf9e8ce00",
        "48895c241048896c2418488974242057b850400000e8162b9101482be0488b05",
        "80b98102000000750e488b8928010000488b0148ff6020c3cccccccccccccccc",
        "488bc4565741554883ec7048895810488bf14c896020e885a72800488bcee85d",
        "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448",
        "40534883ec20488bd9e8e2dc0d00488b8bd8000000488d1524d09501e81fc30d",
        "48895c241848896c242057b870400000e87bb59301482be0488b05a9e1270348"};
    std::array<native::Target,36> targets{};
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
                              const engine::Image& image,HANDLE stop,const std::array<native::Target,36>& targets) {
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
    if (!validate_native_targets(save::session().installation,memory,binding.image,stop,targets)) return false;
    meter_visibility_return=binding.image.base+0x1116e07;
    void* detours[]={reinterpret_cast<void*>(populate),reinterpret_cast<void*>(focus),reinterpret_cast<void*>(load),
        reinterpret_cast<void*>(is_available),reinterpret_cast<void*>(update),
        reinterpret_cast<void*>(root_navigation),reinterpret_cast<void*>(root_campaign),
        reinterpret_cast<void*>(render_details),
        reinterpret_cast<void*>(meter_allocate_detour),
        reinterpret_cast<void*>(sprite_visibility_detour),
        reinterpret_cast<void*>(meter_update_detour),reinterpret_cast<void*>(dossier_points),
        reinterpret_cast<void*>(hud_score_init),reinterpret_cast<void*>(end_items),reinterpret_cast<void*>(end_combat_init),
        reinterpret_cast<void*>(dossier_map),reinterpret_cast<void*>(dossier_challenges),
        reinterpret_cast<void*>(start_show),reinterpret_cast<void*>(boss_update)};
    constexpr unsigned hooked[]={0,1,2,3,4,11,12,9,15,16,17,27,28,29,30,31,32,34,35};
    void* originals[19]{};
    for (unsigned i=0;i<19;++i) if (save::session().installation.hook(SC_INSTALL_SAVE_CREATE,6,hooked[i],static_cast<uint32_t>(targets[hooked[i]].address-binding.image.base),[&] {
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
    find_material=reinterpret_cast<FindMaterial>(targets[33].address);
    material_manager=binding.image.base+0x5e05200;
    original_meter_update=reinterpret_cast<MeterUpdate>(originals[10]);
    original_dossier_points=reinterpret_cast<Update>(originals[11]);
    original_hud_score_init=reinterpret_cast<Update>(originals[12]);
    original_end_items=reinterpret_cast<Update>(originals[13]);
    original_end_combat_init=reinterpret_cast<Update>(originals[14]);
    original_dossier_map=reinterpret_cast<Update>(originals[15]);
    original_dossier_challenges=reinterpret_cast<Update>(originals[16]);
    original_start_show=reinterpret_cast<Update>(originals[17]);
    original_boss_update=reinterpret_cast<Populate>(originals[18]);
    end_items_category_vtable=binding.image.base+0x2d10d78;
    for (unsigned i=0;i<19;++i) if (save::session().installation.hook(SC_INSTALL_SAVE_ENABLE,6,hooked[i],static_cast<uint32_t>(targets[hooked[i]].address-binding.image.base),[&] {
        return MH_EnableHook(reinterpret_cast<void*>(targets[hooked[i]].address)); })!=MH_OK) return false;
    ready.store(true,std::memory_order_release); return true;
}
}
