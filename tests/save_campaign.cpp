// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Synthetic native/provider boundary. The controller runs create and resume in
// separate host processes; no game, Steam, original save or gameplay is used.
#include "save_session.h"
#include "save_collector.h"
#include "save_native_hooks.h"
#include "save_write.h"
#include "save_readback.h"
#include "save_submission.h"
#include "save_campaign_native.h"
#include "native_test_adapter.h"
#include "native_runtime.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <functional>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"campaign:%d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
using namespace sentinel;
using namespace sentinel::save;
namespace {
constexpr uintptr_t image=0x10000000;
struct Remote { uintptr_t* table; std::map<std::string,std::string> files; };
bool put(uintptr_t p,const char* name,const void* bytes,int32_t length) {
    reinterpret_cast<Remote*>(p)->files[name]=std::string(static_cast<const char*>(bytes),static_cast<size_t>(length)); return true;
}
int32_t get(uintptr_t p,const char* name,void* bytes,int32_t length) {
    const auto& files=reinterpret_cast<Remote*>(p)->files; const auto found=files.find(name);
    if (found==files.end() || length!=static_cast<int32_t>(found->second.size())) return 0;
    std::memcpy(bytes,found->second.data(),found->second.size()); return length;
}
bool exists(uintptr_t p,const char* name) { return reinterpret_cast<Remote*>(p)->files.count(name)!=0; }
int32_t size(uintptr_t p,const char* name) { return static_cast<int32_t>(reinterpret_cast<Remote*>(p)->files[name].size()); }
int32_t count(uintptr_t p) { return static_cast<int32_t>(reinterpret_cast<Remote*>(p)->files.size()); }
const char* entry(uintptr_t p,int32_t index,int32_t* length) {
    const auto& files=reinterpret_cast<Remote*>(p)->files; auto it=files.begin(); std::advance(it,index);
    *length=static_cast<int32_t>(it->second.size()); return it->first.c_str();
}
template<class T,size_t N> void store(std::array<unsigned char,N>& bytes,size_t offset,const T& value) {
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
NativeString text(std::string& value) { NativeString s{}; s.data=value.data(); s.length=static_cast<int32_t>(value.size()); s.capacity_flags=static_cast<uint32_t>(value.size()+1); return s; }
std::wstring transition_defect;
std::function<void()> initial_checkpoint;
bool nested_change=false;
uint64_t observed_change(uintptr_t root,uintptr_t descriptor) {
    __try { return native::test_change(root,descriptor,0); }
    __except(GetExceptionCode()==0xe0420042?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return 0; }
}
uint64_t native_load(uintptr_t self,uintptr_t descriptor,uintptr_t files) {
    if (transition_defect==L"abnormal") RaiseException(0xe0420042,0,0,nullptr);
    if (transition_defect==L"nested" && !nested_change) {
        nested_change=true;
        native::test_free(self,[](uintptr_t,uintptr_t) {});
        CHECK(native::test_change(self,descriptor,files)==1);
    }
    *reinterpret_cast<uint32_t*>(self+0x44)=transition_defect==L"partial_save"?SC_GAME_LOADING:SC_GAME_IN_GAME;
    if (initial_checkpoint && (transition_defect==L"initial_save" || transition_defect==L"initial_save_return_failed" || transition_defect==L"save_failure" || transition_defect==L"partial_save")) initial_checkpoint();
    if (transition_defect==L"generation") native::test_generation_gap();
    if (transition_defect==L"state_read") { DWORD previous=0; CHECK(VirtualProtect(reinterpret_cast<void*>(self),0x1000,PAGE_NOACCESS,&previous)); }
    if (transition_defect==L"pending_transition" || transition_defect==L"unrelated") *reinterpret_cast<uint32_t*>(self+0x44)=SC_GAME_LOADING;
    return transition_defect==L"native_return" || transition_defect==L"initial_save_return_failed"?0:1;
}
int native_transition(uint32_t difficulty,const std::wstring& defect=L"",std::function<void()> checkpoint={}) {
    transition_defect=defect; initial_checkpoint=checkpoint;
    auto* root=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); CHECK(root);
    std::array<unsigned char,0x1970> request{};
    std::vector<unsigned char> map(0xafd00);
    std::array<unsigned char,16> cvar{};
    std::string map_name="game/sp/initial";
    const auto native_name=text(map_name);
    const uintptr_t map_address=reinterpret_cast<uintptr_t>(map.data());
    const uintptr_t setting=reinterpret_cast<uintptr_t>(cvar.data());
    std::memcpy(root+0x50,&map_address,sizeof(map_address)); store(cvar,8,defect==L"difficulty"?(difficulty+1)%4:difficulty);
    store(request,0x10,native_name);
    std::memcpy(map.data()+0x9a060,&native_name,sizeof(native_name));
    engine::Binding binding{}; binding.root=reinterpret_cast<uintptr_t>(root);
    binding.image.base=reinterpret_cast<uintptr_t>(&setting)-0x45f8590;
    test_campaign_binding(binding.image.base,binding.root);
    native::test_events(binding,native_load);
    CHECK(observed_change(binding.root,reinterpret_cast<uintptr_t>(request.data()))==
        (defect==L"native_return" || defect==L"abnormal" || defect==L"initial_save_return_failed"?0u:1u));
    CHECK(native::inspect().game_state==SC_GAME_MAIN_MENU);
    if (defect==L"state_read") { DWORD previous=0; CHECK(VirtualProtect(root,0x1000,PAGE_READWRITE,&previous)); }
    if (defect==L"pending_transition" || defect==L"unrelated") {
        CHECK(!session().campaign_run.snapshot().map_active && session().campaign_run.snapshot().reason=="none");
        *reinterpret_cast<uint32_t*>(root+0x44)=SC_GAME_IN_GAME;
        CHECK(!session().campaign_run.snapshot().map_active); // Fresh memory alone never certifies the event.
        if (defect==L"unrelated") native::test_free(binding.root,[](uintptr_t,uintptr_t) {});
    }
    if (checkpoint && defect!=L"initial_save" && defect!=L"initial_save_return_failed" && defect!=L"save_failure" && defect!=L"partial_save") checkpoint();
    const auto result=session().campaign_run.snapshot();
    if (defect==L"pending_transition") {
        CHECK(result.transition.game==SC_GAME_LOADING && result.checkpoint_boundary.game==SC_GAME_IN_GAME);
        CHECK(result.transition.at_ms<=result.checkpoint_boundary.at_ms && result.map_active);
    }
    const std::map<std::wstring,std::string> failures{{L"native_return","native_transition_return_failed"},
        {L"abnormal","native_transition_abnormal"},{L"initial_save_return_failed","native_transition_return_failed"},
        {L"state_read","native_transition_state_unreadable"},{L"generation","native_transition_generation_mismatch"},
        {L"difficulty","native_transition_difficulty_mismatch"},{L"unrelated","native_checkpoint_transition_unassociated"},
        {L"partial_save","native_checkpoint_map_not_ready"},{L"save_failure","native_checkpoint_completion_unproven"}};
    auto expected=failures.find(defect);
    if (expected!=failures.end()) { CHECK(result.reason==expected->second); CHECK(!result.continuity_persisted); }
    else CHECK(result.map_active);
    initial_checkpoint={}; CHECK(VirtualFree(root,0,MEM_RELEASE));
    return 0;
}
#include "campaign_navigation_fixture.h"
#include "campaign_writer_fixture.h"
}
int wmain(int argc,wchar_t** argv) {
    CHECK(argc==4 || argc==5);
    const std::wstring mode=argv[1]; const bool resume=mode==L"resume";
    const auto difficulty=static_cast<uint32_t>(std::wcstoul(argv[3],nullptr,10));
    const std::wstring defect=argc==5?argv[4]:L"";
    navigation_fixture::vanilla();
    storage::Descriptor descriptor{{"synthetic-campaign-host",0,1,std::string(64,'b')},argv[2],
        {resume?storage::CampaignIntent::resume:storage::CampaignIntent::create,difficulty}};
    std::unique_ptr<storage::Namespace> lease;
    CHECK((resume?storage::reopen(descriptor,lease):storage::prepare(descriptor,lease)).ok());
    auto& owner=session(); const auto configured=owner.configure(descriptor,std::move(lease));
    if (defect==L"refuse_configuration") { CHECK(!configured.ok()); std::puts("PASS refused immutable or incomplete campaign contract"); return 0; }
    CHECK(configured.ok());
    std::array<uintptr_t,20> table{}; Remote remote{table.data(),{}};
    table[0]=reinterpret_cast<uintptr_t>(put); table[1]=reinterpret_cast<uintptr_t>(get);
    table[13]=reinterpret_cast<uintptr_t>(exists); table[15]=reinterpret_cast<uintptr_t>(size);
    table[18]=reinterpret_cast<uintptr_t>(count); table[19]=reinterpret_cast<uintptr_t>(entry);
    const auto provider=reinterpret_cast<uintptr_t>(&remote);
    const auto directory=owner.native_root()+"/GAME-AUTOSAVE0";
    remote.files[owner.native_root()+"/sentinel-owner-"+owner.namespace_id()+".txt"]=owner.ownership_record();
    owner.install(0x1000,0x2000,steam_20260818_routes);
    CHECK(owner.startup_enter(0x1000,0x2000,GetCurrentThreadId()));
    CHECK(owner.bind_provider(owner.native_root(),provider,owner.ownership_record())); owner.startup_leave(false);
    CHECK(owner.publish_profile_catalog(provider,owner.ownership_record(),resume?std::vector<std::string>{"AUTOSAVE0"}:std::vector<std::string>{},"AUTOSAVE0",0,!resume,0));
    CHECK(owner.capture_profile_baseline(0x4000,0x5000,"AUTOSAVE7",2));
    CHECK(owner.profile_read_completed() && owner.accepts_requests());
    std::string payload="synthetic encoded checkpoint payload", relative="game.details", native_directory=directory;
    std::array<unsigned char,0x180> file{}; std::array<unsigned char,0x280> data{};
    store(file,0,image+0x2a575a8); store(file,8,text(relative));
    const uint64_t length=payload.size(); const uintptr_t buffer=reinterpret_cast<uintptr_t>(payload.data());
    store(file,0x150,length); store(file,0x158,length); store(file,0x168,buffer); file[0x178]=1;
    const uintptr_t files=reinterpret_cast<uintptr_t>(file.data()), source=reinterpret_cast<uintptr_t>(data.data());
    const std::array<uintptr_t,1> file_list{files};
    store(data,0,text(native_directory)); store(data,0x1c0,reinterpret_cast<uintptr_t>(file_list.data())); store(data,0x1c8,int32_t{1}); store(data,0x1cc,int32_t{1});
    engine::LocalMemory memory;
    if (resume) {
        CHECK(owner.campaign_run.snapshot().source_checkpoint==1);
        CHECK(owner.campaign_run.begin_resume());
        CHECK(owner.campaign_run.allow_access(source,directory,false,false));
        if (defect==L"wrong_source") {
            payload[0]='X'; CHECK(!owner.campaign_run.verify_source(memory,source,image));
            CHECK(!owner.campaign_run.parser_enter(source)); std::puts("PASS source hash refusal before parser"); return 0;
        }
        CHECK(owner.campaign_run.verify_source(memory,source,image));
        CHECK(owner.campaign_run.parser_enter(source));
        owner.campaign_run.parser_leave(defect==L"failed_parser"?0x10:0);
        if (defect==L"failed_parser") { CHECK(!owner.campaign_run.map_begin("game/sp/initial",1)); std::puts("PASS original parser failure retained"); return 0; }
        if (defect==L"wrong_difficulty") { CHECK(!owner.campaign_run.allow_difficulty((difficulty+1)%4)); CHECK(!owner.accepts_requests()); return 0; }
        if (defect==L"missing_difficulty") { CHECK(!owner.campaign_run.map_begin("game/sp/initial",1)); return 0; }
        CHECK(owner.campaign_run.allow_difficulty(difficulty)); // Native consumer restores the parsed save field.
        if (defect==L"wrong_map") { CHECK(!owner.campaign_run.map_begin("other/map",1)); return 0; }
        CHECK(native_transition(difficulty)==0);
        CHECK(owner.campaign_run.snapshot().phase=="reopened");
        std::printf("PASS separate-process native source/parser/lifecycle reopen, difficulty=%u pid=%lu\n",difficulty,GetCurrentProcessId()); return 0;
    }
    if (defect==L"dirty") {
        CHECK(!owner.campaign_run.begin_create(false,"AUTOSAVE0",0,true));
        CHECK(!owner.campaign_run.snapshot().map_active); std::puts("PASS dirty-process refusal before reservation"); return 0;
    }
    navigation_fixture::create(difficulty,defect);
    if (defect==L"extra_life" || defect==L"ultra") return 0;
    const bool transition_failure=defect==L"native_return" || defect==L"abnormal" || defect==L"state_read" || defect==L"generation" || defect==L"difficulty";
    if(transition_failure) { CHECK(native_transition(difficulty,defect)==0); return 0; }
    writer_fixture::Model writer{remote,source,files,payload,directory};
    CHECK(native_transition(difficulty,defect==L"pending"?L"pending_save":defect,[&] { writer_fixture::save(writer,defect==L"pending"?L"pending_save":defect); })==0);
    if(defect==L"pending" || defect==L"initial_save_return_failed" || defect==L"unassociated" || defect==L"save_failure" || defect==L"partial_save" || defect==L"unrelated") {
        CHECK(!owner.campaign_run.snapshot().continuity_persisted); return 0;
    }
    const auto observed=owner.campaign_run.snapshot();
    CHECK(observed.native_saved && observed.readback_verified && observed.continuity_persisted && observed.checkpoint==1);
    CHECK(observed.operation==writer.operation && observed.effective_difficulty==difficulty && observed.native_factory_matched);
    std::printf("PASS native checkpoint correlation/continuity, difficulty=%u pid=%lu\n",difficulty,GetCurrentProcessId()); return 0;
}
