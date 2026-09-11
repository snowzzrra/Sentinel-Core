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
bool prior_campaign=false, campaign_transition=false;
using NewGame=void(*)(uintptr_t,uint32_t,uint8_t);
using NewInternal=void(*)(uintptr_t,uint32_t,uint8_t,uint32_t);
using LoadGame=void(*)(uintptr_t,uintptr_t);
using ParseGame=uint64_t(*)(SaveReference*,uintptr_t,uintptr_t,uintptr_t);
using SetCvar=uint64_t(*)(uintptr_t,const char*,uint8_t);
using SetInteger=void(*)(uintptr_t,uint32_t,uint8_t);
NewGame original_new=nullptr; NewInternal original_internal=nullptr;
LoadGame original_load=nullptr; ParseGame original_parse=nullptr; SetCvar original_cvar=nullptr;
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
    reinterpret_cast<SetInteger>(image+0x376250)(image+0x45f8590,session().campaign_run.snapshot().difficulty,1);
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
void new_game(uintptr_t menu,uint32_t,uint8_t extra_life) {
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
bool campaign_change_begin(uintptr_t self,uintptr_t descriptor,uint64_t generation) {
    if (!session().campaign_run.enabled()) return true;
    if (session().campaign_run.snapshot().phase=="armed") return true; // Native menu startup is observed below.
    std::string map;
    if (self!=root || !name(descriptor,0x10,map)) { session().campaign_run.refuse("native_map_descriptor_unreadable"); return false; }
    campaign_transition=session().campaign_run.map_begin(std::move(map),generation); return campaign_transition;
}
void campaign_change_end(bool success,uint64_t generation,uint32_t game) {
    if (!session().campaign_run.enabled()) return;
    if (!campaign_transition) { prior_campaign|=!success || game!=0; return; }
    campaign_transition=false; prior_campaign=true;
    uintptr_t map=0; int32_t difficulty=-1;
    success=success && read(root,0x50,map) && map && effective_difficulty(map,difficulty);
    session().campaign_run.map_end(success && game!=0,generation,static_cast<uint32_t>(difficulty));
}
bool install_campaign_hooks(const engine::Binding& binding,HANDLE stop) {
    if (!session().campaign_run.enabled()) return true;
    image=binding.image.base; root=binding.root;
    constexpr uint32_t rvas[]={0x17538a0,0x1753b80,0x66cec0,0x14940b0,0x376020,0x376250};
    constexpr const char* bytes[]={
        "48895c2408488974241048897c24184c8974242055488dac2460feffff4881ec",
        "4055535741554156488dac2460c0ffffb8a0400000e806821101482be0488b05",
        "4883ec28488b059d0c17044c8b40184d85c07478498b40084885c0746f448b40",
        "40555356574154415541564157488dac24c8bdffffb838430000e8d17c3d0148",
        "48895c24104889742418574883ec20488bd9488b09410fb6f0488bfa4885d275",
        "48895c2418574883ec60488b057787e3034833c44889442450488bf9410fb6d8"};
    std::array<native::Target,6> targets{};
    for (unsigned i=0;i<targets.size();++i) {
        targets[i].address=image+rvas[i];
        if (std::strlen(bytes[i])!=64) return false;
        const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for (size_t n=0;n<32;++n) targets[i].bytes[n]=static_cast<uint8_t>(digit(bytes[i][2*n])*16+digit(bytes[i][2*n+1]));
        if (i==0) {
            constexpr const char* signature="008bda4c8d0dced86e01ba8a000000488d4de041b809000000e8926df0fec685";
            targets[i].signature_offset=64;
            for (size_t n=0;n<32;++n) targets[i].signature[n]=static_cast<uint8_t>(digit(signature[2*n])*16+digit(signature[2*n+1]));
        }
        if (native::validate_recorded(session().installation,memory,binding.image,targets[i],stop,GetTickCount64()+3000,4,i)) return false;
    }
    void* detours[]={reinterpret_cast<void*>(new_game),reinterpret_cast<void*>(new_internal),reinterpret_cast<void*>(load_game),reinterpret_cast<void*>(parse_game),reinterpret_cast<void*>(set_cvar)};
    void* originals[5]{};
    for (unsigned i=0;i<5;++i) if (session().installation.hook(SC_INSTALL_SAVE_CREATE,4,i,rvas[i],[&]{
        return MH_CreateHook(reinterpret_cast<void*>(targets[i].address),detours[i],&originals[i]);})!=MH_OK) return false;
    original_new=reinterpret_cast<NewGame>(originals[0]); original_internal=reinterpret_cast<NewInternal>(originals[1]);
    original_load=reinterpret_cast<LoadGame>(originals[2]); original_parse=reinterpret_cast<ParseGame>(originals[3]); original_cvar=reinterpret_cast<SetCvar>(originals[4]);
    for (unsigned i=0;i<5;++i) if (session().installation.hook(SC_INSTALL_SAVE_ENABLE,4,i,rvas[i],[&]{
        return MH_EnableHook(reinterpret_cast<void*>(targets[i].address));})!=MH_OK) return false;
    return true;
}
}
