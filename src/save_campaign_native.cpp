// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_campaign_native.h"
#include "save_session.h"
#include "save_provider.h"
#include "save_catalog.h"
#include "save_write.h"
#include "native_runtime.h"
#include "native_target.h"
#include "MinHook.h"
#include <cstring>

namespace sentinel::save {
namespace {
uintptr_t image=0, root=0;
bool prior_campaign=false;
using NewGame=void(*)(uintptr_t,uint32_t,uint8_t);
using NewInternal=void(*)(uintptr_t,uint32_t,uint8_t,uint32_t);
using LoadGame=void(*)(uintptr_t,uintptr_t);
using ParseGame=uint64_t(*)(SaveReference*,uintptr_t,uintptr_t,uintptr_t);
using SetCvar=uint64_t(*)(uintptr_t,const char*,uint8_t);
using SetInteger=void(*)(uintptr_t,uint32_t,uint8_t);
using CampaignAction=uint64_t(*)(uintptr_t,uintptr_t);
using SelectedSlot=uint32_t(*)(uintptr_t);
using SelectSlot=void(*)(uintptr_t,uint32_t);
using Menu=uintptr_t(*)();
NewGame original_new=nullptr; NewInternal original_internal=nullptr;
LoadGame original_load=nullptr; ParseGame original_parse=nullptr; SetCvar original_cvar=nullptr;
CampaignAction original_action=nullptr;
SelectedSlot selected_slot=nullptr; SelectSlot select_slot=nullptr; Menu main_menu=nullptr;
SetInteger set_integer=nullptr;
thread_local bool navigation_owned=false;
engine::LocalMemory memory;
template<class T> bool read(uintptr_t p,size_t offset,T& out) {
    uintptr_t at=0; return engine::add(p,offset,sizeof(out),at) && !memory.copy(at,&out,sizeof(out)).reason;
}
bool name(uintptr_t p,size_t offset,std::string& value) {
    NativeString text{}; char bytes[260]{};
    if (!read(p,offset,text) || !text.data || text.length<=0 || text.length>=260 ||
        memory.copy(reinterpret_cast<uintptr_t>(text.data),bytes,static_cast<size_t>(text.length)+1).reason ||
        bytes[text.length] || std::memchr(bytes,0,static_cast<size_t>(text.length))) return false;
    value.assign(bytes,static_cast<size_t>(text.length)); return true;
}
void set_difficulty() {
    set_integer(image+0x45f8590,session().campaign_run.snapshot().difficulty,1);
}
bool effective_difficulty(uintptr_t map,int32_t& value) {
    // Native getter RVA 0x699e50 is a leaf without .pdata. Observe the
    // supported-build fields; do not relax executable entry validation or
    // introduce a call to an unvalidated native boundary.
    uintptr_t setting=0, override_data=0; uint32_t mode=0;
    if (!read(image+0x45f8590,0,setting) || !setting || !read(setting,8,value) || !read(map,0xafbd8,mode)) return false;
    if (mode-1u<4) {
        if (!read(map,0xafc00,override_data)) return false;
        if (override_data && value>1) { value=1; return true; }
    }
    return value>=0 || read(map,0xaf368,value);
}
void new_game(uintptr_t menu,uint32_t requested,uint8_t extra_life) {
    if (!navigation_owned || extra_life || requested>3) {
        session().campaign_run.refuse("ap_new_game_navigation_or_mode_invalid"); return;
    }
    auto& owner=session(); ProfileChoice choice{}; NativeCampaignCatalog catalog; uintptr_t remote=0; std::string prefix;
    const auto lifetime=native::inspect();
    const bool clean=!prior_campaign && !extra_life && lifetime.lifecycle==SC_LIFETIME_MENU && !lifetime.depth &&
        !lifetime.event_gap_count && !lifetime.history_overwritten && lifetime.game_state==0 &&
        (owner.inspect().flags&SC_SAVE_SESSION_STARTUP_QUALIFIED) &&
        read_campaign_prefix(memory,image,prefix) && prefix=="GAME-" &&
        read_native_catalog(owner,memory,"GAME-",catalog,remote) && catalog.slots.empty();
    if (!owner.profile_choice(choice) || !owner.campaign_run.begin_create(clean,choice.name.data(),choice.index,catalog.slots.empty())) return;
    set_difficulty(); original_new(menu,owner.campaign_run.snapshot().difficulty,0);
}
void create_from_action(uintptr_t screen) {
    // Exact case3 of Campaign::HandleAction_Impl: retain native slot selection
    // before entering NewGame. NewGame retains its permission/task/ref ownership;
    // the skipped screen only supplies the ordinary difficulty argument.
    const auto slot=selected_slot(screen);
    if (slot!=0) { session().campaign_run.refuse("ap_new_game_slot_not_prospective"); return; }
    const auto menu=main_menu();
    if (!menu) { session().campaign_run.refuse("native_menu_unavailable"); return; }
    select_slot(menu,slot);
    new_game(menu,session().campaign_run.snapshot().difficulty,0);
}
void invoke_navigation(uintptr_t screen) {
    navigation_owned=true;
    __try { create_from_action(screen); }
    __finally { navigation_owned=false; }
}
uint64_t campaign_action(uintptr_t screen,uintptr_t action) {
    if (!session().campaign_run.enabled()) return original_action(screen,action);
    uint32_t kind=0,count=0,tag=0,value=0; uintptr_t arguments=0;
    // The empty-slot widget installs an integer ScriptVar (tag5,value3).
    // Preserve every unrelated native action and vanilla screen path.
    if (!read(action,0,kind) || kind!=1 || !read(action,0x10,count) || !count ||
        !read(action,8,arguments) || !read(arguments,0,tag) || tag!=5 ||
        !read(arguments,8,value) || value!=3) return original_action(screen,action);
    if (!session().accepts_requests() || navigation_owned || session().campaign_run.snapshot().phase!="armed") return 1;
    invoke_navigation(screen);
    return 1; // Consume this explicit New Game action before navigation to Difficulty.
}
void new_internal(uintptr_t menu,uint32_t difficulty,uint8_t extra_life,uint32_t policy) {
    std::string prefix;
    if (!read_campaign_prefix(memory,image,prefix) || prefix!="GAME-") { session().campaign_run.refuse("base_campaign_required"); return; }
    if (!session().campaign_run.start_internal(difficulty,extra_life!=0)) return;
    original_internal(menu,difficulty,extra_life,policy);
}
void load_game(uintptr_t self,uintptr_t request) {
    std::string prefix;
    if (!read_campaign_prefix(memory,image,prefix) || prefix!="GAME-") { session().campaign_run.refuse("base_campaign_required"); return; }
    if (self!=root || !session().campaign_run.begin_resume()) return;
    set_difficulty(); original_load(self,request);
}
uint64_t set_cvar(uintptr_t self,const char* value,uint8_t force) {
    uintptr_t target=0,actual=0;
    if (self==image+0x45f8590 || (read(image+0x45f8590,0,target) && target && read(self,0,actual) && target==actual)) {
        char bytes[2]{};
        const bool valid=value && !memory.copy(reinterpret_cast<uintptr_t>(value),bytes,2).reason &&
            bytes[0]>='0' && bytes[0]<='3' && !bytes[1];
        if (!session().campaign_run.allow_difficulty(valid ? static_cast<uint32_t>(bytes[0]-'0') : 4)) return 0;
    }
    return original_cvar(self,value,force);
}
uint64_t parse_game(SaveReference* data,uintptr_t files,uintptr_t prepared,uintptr_t request) {
    uintptr_t object=0; const bool valid=data && read(data->control,8,object) && session().campaign_run.parser_enter(object);
    prior_campaign=true;
    if (!valid) {
        reinterpret_cast<ReleaseSaveReference>(image+0x367770)(data);
        return 0x10; // Same parser refusal, consumed reference; no reset/create fallback.
    }
    const auto result=original_parse(data,files,prepared,request);
    session().campaign_run.parser_leave(static_cast<uint32_t>(result)); return result;
}
}
bool campaign_change_begin(uintptr_t self,uintptr_t descriptor,CampaignTransition& transition) {
    if (!session().campaign_run.enabled()) return true;
    if (session().campaign_run.snapshot().phase=="armed") return true; // Native menu startup is observed below.
    if (!transition.observed) { session().campaign_run.refuse("native_transition_not_observed"); return false; }
    if (transition.depth>1) return true; // Outer native operation owns the campaign result.
    std::string map;
    if (self!=root || !name(descriptor,0x10,map)) { session().campaign_run.refuse("native_map_descriptor_unreadable"); return false; }
    transition.campaign=session().campaign_run.map_begin(std::move(map),transition.generation_before,transition.event_id);
    return transition.campaign;
}
namespace {
void map_facts(CampaignTransition& result) {
    uintptr_t map=0; int32_t difficulty=-1; std::string actual;
    result.map_read=read(root,0x50,map) && map && name(map,0x9a060,actual) && actual.size()<result.map.size();
    if (result.map_read) std::memcpy(result.map.data(),actual.c_str(),actual.size()+1);
    result.difficulty_read=map && effective_difficulty(map,difficulty);
    result.difficulty=static_cast<uint32_t>(difficulty);
}
}
void campaign_change_end(CampaignTransition& transition) {
    if (!session().campaign_run.enabled()) return;
    if (!transition.campaign) {
        if (transition.depth==1) prior_campaign|=!transition.native_return || (transition.state_read && transition.game==SC_GAME_IN_GAME);
        return;
    }
    prior_campaign=true; map_facts(transition);
    session().campaign_run.map_end(transition);
}
void campaign_checkpoint_boundary(CampaignTransition transition) {
    if (!session().campaign_run.enabled()) return;
    // Reached only from the exact SaveGame -> mode1 factory call. SaveGame has
    // already checked map::IsRunning and native user/slot readiness. Cutscene
    // player control and diagnostic publication are not writer prerequisites.
    transition.state_read=read(root,0x44,transition.game); map_facts(transition);
    session().campaign_run.checkpoint_ready(transition);
}
SaveReference* campaign_save_factory(uintptr_t caller,uintptr_t expected_caller,uintptr_t manager,
        SaveReference* out,uint32_t user,uintptr_t request,NativeSaveFactory original) {
    if (caller==expected_caller) campaign_checkpoint_boundary(native::checkpoint_transition());
    return native_save_factory(session().native_writes,caller,expected_caller,manager,out,user,request,original);
}
std::array<native::Target,10> campaign_targets(uintptr_t base) {
    constexpr uint32_t rvas[]={0x17538a0,0x1753b80,0x66cec0,0x14940b0,0x376020,0x10a6110,0x376250,0x10a5bb0,0x17515c0,0x174ef70};
    constexpr const char* bytes[]={
        "48895c2408488974241048897c24184c8974242055488dac2460feffff4881ec",
        "4055535741554156488dac2460c0ffffb8a0400000e806821101482be0488b05",
        "4883ec28488b059d0c17044c8b40184d85c07478498b40084885c0746f448b40",
        "40555356574154415541564157488dac24c8bdffffb838430000e8d17c3d0148",
        "48895c24104889742418574883ec20488bd9488b09410fb6f0488bfa4885d275",
        "48895c241848897c242055488dac2400ffffff4881ec00020000488b05a78810",
        "48895c2418574883ec60488b057787e3034833c44889442450488bf9410fb6d8",
        "48895c2408574883ec20488b01488bf9bbffffffffff90e00100004885c07434",
        "4055564156488d6c24a04881ec60010000488b0500d4a5024833c4488945308b",
        "4883ec28488b05c5f1f5024885c0752d8d5053b9800d0000e8b382c0fe4885c0"};
    std::array<native::Target,10> targets{};
    for (unsigned i=0;i<targets.size();++i) {
        targets[i].address=base+rvas[i];
        if (std::strlen(bytes[i])!=64) return {};
        const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for (size_t n=0;n<32;++n) targets[i].bytes[n]=static_cast<uint8_t>(digit(bytes[i][2*n])*16+digit(bytes[i][2*n+1]));
        if (i==0) {
            constexpr const char* signature="008bda4c8d0dced86e01ba8a000000488d4de041b809000000e8926df0fec685";
            targets[i].signature_offset=64;
            for (size_t n=0;n<32;++n) targets[i].signature[n]=static_cast<uint8_t>(digit(signature[2*n])*16+digit(signature[2*n+1]));
        }
    }
    return targets;
}
bool install_campaign_hooks(const engine::Binding& binding,HANDLE stop) {
    if (!session().campaign_run.enabled()) return true;
    image=binding.image.base; root=binding.root;
    const auto targets=campaign_targets(image);
    for (unsigned i=0;i<targets.size();++i)
        if (native::validate_recorded(session().installation,memory,binding.image,targets[i],stop,GetTickCount64()+3000,4,i)) return false;
    void* detours[]={reinterpret_cast<void*>(new_game),reinterpret_cast<void*>(new_internal),reinterpret_cast<void*>(load_game),reinterpret_cast<void*>(parse_game),reinterpret_cast<void*>(set_cvar),reinterpret_cast<void*>(campaign_action)};
    void* originals[6]{};
    for (unsigned i=0;i<6;++i) if (session().installation.hook(SC_INSTALL_SAVE_CREATE,4,i,static_cast<uint32_t>(targets[i].address-image),[&]{
        return MH_CreateHook(reinterpret_cast<void*>(targets[i].address),detours[i],&originals[i]);})!=MH_OK) return false;
    original_new=reinterpret_cast<NewGame>(originals[0]); original_internal=reinterpret_cast<NewInternal>(originals[1]);
    original_load=reinterpret_cast<LoadGame>(originals[2]); original_parse=reinterpret_cast<ParseGame>(originals[3]); original_cvar=reinterpret_cast<SetCvar>(originals[4]);
    original_action=reinterpret_cast<CampaignAction>(originals[5]);
    set_integer=reinterpret_cast<SetInteger>(targets[6].address); selected_slot=reinterpret_cast<SelectedSlot>(targets[7].address);
    select_slot=reinterpret_cast<SelectSlot>(targets[8].address); main_menu=reinterpret_cast<Menu>(targets[9].address);
    for (unsigned i=0;i<6;++i) if (session().installation.hook(SC_INSTALL_SAVE_ENABLE,4,i,static_cast<uint32_t>(targets[i].address-image),[&]{
        return MH_EnableHook(reinterpret_cast<void*>(targets[i].address));})!=MH_OK) return false;
    return true;
}
#ifdef SC_NATIVE_TESTING
void test_campaign_binding(uintptr_t base, uintptr_t object) { image=base; root=object; prior_campaign=false; }
void test_campaign_calls(const CampaignNativeTestCalls& calls) {
    original_new=calls.new_game; original_internal=calls.internal; original_action=calls.action;
    selected_slot=calls.slot; select_slot=calls.select; main_menu=calls.menu; set_integer=calls.integer; original_cvar=calls.cvar;
}
uint64_t test_campaign_action(uintptr_t screen,uintptr_t action) { return campaign_action(screen,action); }
void test_campaign_internal(uintptr_t menu,uint32_t difficulty,uint8_t extra,uint32_t policy) { new_internal(menu,difficulty,extra,policy); }
void test_campaign_new(uintptr_t menu,uint32_t difficulty,uint8_t extra) { new_game(menu,difficulty,extra); }
uint64_t test_campaign_cvar(uintptr_t object,const char* value,uint8_t force) { return set_cvar(object,value,force); }
#endif
}
