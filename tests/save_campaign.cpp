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
#include <filesystem>
#include <fstream>
#include "save_provider.h"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"campaign:%d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
using namespace sentinel;
using namespace sentinel::save;
void exercise_campaign_profile(Session&, const std::function<void(const std::function<void()>&)>&);
namespace {
constexpr uintptr_t image=0x10000000;
int64_t diagnostic_fact(const BEvent& event,const char* key) {
    for (const auto& fact:event.facts) if (fact.key && std::strcmp(fact.key,key)==0) return fact.value;
    CHECK(false); return 0;
}
void diagnostic_stage_observed(const BSnapshot& trace,BStage stage) {
    const auto& event=trace.stages[static_cast<size_t>(stage)]; CHECK(event.sequence && event.thread && event.predicate);
}
struct Remote { uintptr_t* table; std::map<std::string,std::string> files; };
std::filesystem::path recovery_disk;
int recovery_interrupt=-1, recovery_writes=0;
std::wstring recovery_namespace;
uint64_t fixture_user=76561198000000001ull;
bool lose_provider=false, concurrent_target=false, concurrent_auxiliary=false;
unsigned owner_reads=0;
uint64_t steam_owner(uintptr_t remote) {
    if(concurrent_target && ++owner_reads==3) {
        auto& files=reinterpret_cast<Remote*>(remote)->files;
        const std::string suffix=concurrent_auxiliary?"/game_duration.dat-BACKUP":"/game_duration.dat";
        for(auto& [name,bytes]:files) if(name.size()>=suffix.size() && name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0) { bytes[0]^=1; break; }
    }
    return fixture_user;
}
uintptr_t recovery_context_remote=0;
uintptr_t recovery_context(uintptr_t record) { CHECK(record==image+0x397fb88); return reinterpret_cast<uintptr_t>(&recovery_context_remote); }
struct RecoveryMemory final:engine::Memory {
    engine::LocalMemory local;
    engine::ReadResult copy(uintptr_t address,void* out,size_t length) override {
        const std::map<uintptr_t,uintptr_t> native{{0x1000+0x9b38,0x9000},{0x9000,0x9100},{0x9108,0x9200},{0x9200,image+0x2e90658}};
        const auto it=native.find(address);
        if(it!=native.end() && length==sizeof(uintptr_t)) { std::memcpy(out,&it->second,length); return {}; }
        return local.copy(address,out,length);
    }
};
void persist_remote(const Remote& remote) {
    if(recovery_disk.empty()) return;
    for(const auto& [name,bytes]:remote.files) {
        const auto path=recovery_disk/name; std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path,std::ios::binary|std::ios::trunc); out.write(bytes.data(),bytes.size()); CHECK(out.good());
    }
}
void load_remote(Remote& remote) {
    if(!std::filesystem::exists(recovery_disk)) return;
    for(const auto& f:std::filesystem::recursive_directory_iterator(recovery_disk)) if(f.is_regular_file()) {
        std::ifstream in(f.path(),std::ios::binary);
        remote.files[std::filesystem::relative(f.path(),recovery_disk).generic_string()]={std::istreambuf_iterator<char>(in),{}};
    }
}
bool put(uintptr_t p,const char* name,const void* bytes,int32_t length) {
    if(recovery_interrupt==0) ExitProcess(77);
    auto& remote=*reinterpret_cast<Remote*>(p);
    remote.files[name]=std::string(static_cast<const char*>(bytes),static_cast<size_t>(length));
    persist_remote(remote);
    if(lose_provider) ++fixture_user;
    if(recovery_interrupt>0) {
        ++recovery_writes;
        if(recovery_interrupt<6 && recovery_writes==recovery_interrupt) ExitProcess(77);
        if(recovery_writes==5 && recovery_interrupt==6) CHECK(CreateFileW((recovery_namespace+L"\\campaign.checkpoint").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)!=INVALID_HANDLE_VALUE);
        if(recovery_writes==5 && recovery_interrupt==7) CHECK(CreateFileW((recovery_namespace+L"\\recovery.complete").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr)!=INVALID_HANDLE_VALUE);
    }
    return true;
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
std::function<void()> menu_transition;
std::function<void()> menu_profile;
bool nested_change=false;
unsigned native_cleanups=0;
uint64_t observed_change(uintptr_t root,uintptr_t descriptor) {
    __try { return native::test_change(root,descriptor,0); }
    __except(GetExceptionCode()==0xe0420042?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return 0; }
}
uint64_t native_load(uintptr_t self,uintptr_t descriptor,uintptr_t files) {
    if (transition_defect==L"terminal_cleanup" && !session().native_io()) {
        ++native_cleanups; *reinterpret_cast<uint32_t*>(self+0x44)=SC_GAME_MAIN_MENU; return 7;
    }
    if (*reinterpret_cast<uint8_t*>(descriptor+0x1960)&0x10) {
        if (menu_profile) menu_profile();
        *reinterpret_cast<uint32_t*>(self+0x44)=SC_GAME_MAIN_MENU; return 1;
    }
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
int native_transition(uint32_t difficulty,const std::wstring& defect=L"",std::function<void()> checkpoint={},
        std::function<void()> after_ready={},std::function<void()> on_menu={}) {
    transition_defect=defect; initial_checkpoint=checkpoint; menu_profile=on_menu;
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
    menu_transition=[&] {
        request[0x1960]=0x10;
        CHECK(observed_change(binding.root,reinterpret_cast<uintptr_t>(request.data()))==1);
    };
    CHECK(observed_change(binding.root,reinterpret_cast<uintptr_t>(request.data()))==
        (defect==L"native_return" || defect==L"abnormal" || defect==L"initial_save_return_failed"?0u:1u));
    CHECK(native::inspect().game_state==SC_GAME_MAIN_MENU);
    if (defect==L"terminal_cleanup") {
        session().fail_profile();
        const auto first=session().btrace.snapshot().first_failure;
        CHECK(first.sequence);
        CHECK(observed_change(binding.root,reinterpret_cast<uintptr_t>(request.data()))==7);
        CHECK(observed_change(binding.root,reinterpret_cast<uintptr_t>(request.data()))==7);
        CHECK(native_cleanups==2 && session().fault()==SessionFault::native_profile && !session().native_io());
        CHECK(session().btrace.snapshot().first_failure.sequence==first.sequence);
        CHECK(session().campaign_run.snapshot().reason=="lifecycle_after_session_refusal");
        CHECK(!session().campaign_run.snapshot().native_saved);
        CHECK(VirtualFree(root,0,MEM_RELEASE)); return 0;
    }
    if (defect==L"state_read") { DWORD previous=0; CHECK(VirtualProtect(root,0x1000,PAGE_READWRITE,&previous)); }
    if (defect==L"pending_transition" || defect==L"unrelated") {
        CHECK(!session().campaign_run.snapshot().map_active && session().campaign_run.snapshot().reason=="none");
        *reinterpret_cast<uint32_t*>(root+0x44)=SC_GAME_IN_GAME;
        CHECK(!session().campaign_run.snapshot().map_active); // Fresh memory alone never certifies the event.
        if (defect==L"unrelated") native::test_free(binding.root,[](uintptr_t,uintptr_t) {});
    }
    if (checkpoint && defect!=L"initial_save" && defect!=L"initial_save_return_failed" && defect!=L"save_failure" && defect!=L"partial_save") checkpoint();
    if (after_ready) after_ready();
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
    if (expected!=failures.end()) {
        CHECK(result.reason==expected->second); CHECK(!result.continuity_persisted);
        const auto trace=session().btrace.snapshot(); CHECK(trace.first_failure.sequence && trace.first_failure.status==BStatus::refused);
        diagnostic_stage_observed(trace,BStage::transition);
        if (defect==L"difficulty") {
            CHECK(std::strcmp(trace.first_failure.predicate,"native_transition_difficulty_mismatch")==0);
            CHECK(diagnostic_fact(trace.first_failure,"actual_difficulty")!=diagnostic_fact(trace.first_failure,"expected_difficulty"));
        }
    }
    else CHECK(result.map_active || defect==L"menu_pending_save");
    if (result.continuity_persisted && result.map_active) {
        menu_transition();
        const auto menu=session().campaign_run.snapshot();
        CHECK(session().accepts_requests() && menu.continuity_persisted && menu.checkpoint==result.checkpoint);
        CHECK(!menu.map_active && !menu.save_ready && menu.map==result.map && menu.phase==result.phase);
    }
    initial_checkpoint={}; menu_transition={}; menu_profile={}; CHECK(VirtualFree(root,0,MEM_RELEASE));
    return 0;
}
#include "campaign_navigation_fixture.h"
#include "campaign_writer_fixture.h"
}
#include "startup_route_fixture.h"
int wmain(int argc,wchar_t** argv) {
    CHECK(argc==4 || argc==5);
    const std::wstring mode=argv[1]; const bool resume=mode==L"resume",recover=mode==L"recover";
    const auto difficulty=static_cast<uint32_t>(std::wcstoul(argv[3],nullptr,10));
    const std::wstring defect=argc==5?argv[4]:L"";
    const bool profile_lifecycle=defect==L"profile_lifecycle";
    const bool native_read=defect.rfind(L"native_read",0)==0;
    const bool recovery_case=defect.rfind(L"native_read_c",0)==0;
    const auto retail_marker=std::filesystem::path(argv[2])/"retail-pair.fixture";
    const bool retail_pair=defect==L"native_read_c_retail_pair" || std::filesystem::exists(retail_marker);
    if(retail_pair && !resume && !recover) { std::filesystem::create_directories(argv[2]); std::ofstream(retail_marker)<<"primary operation; preserved rotation pair\n"; }
    if(recovery_case) recovery_disk=std::filesystem::path(argv[2])/"remote";
    navigation_fixture::vanilla();
    storage::Descriptor descriptor{{"synthetic-campaign-host",0,1,std::string(64,'b')},argv[2],
        {recover?storage::CampaignIntent::recover:resume?storage::CampaignIntent::resume:storage::CampaignIntent::create,difficulty,{}}};
    if(recover) {
        std::ifstream selected(std::filesystem::path(argv[2])/"selected-backup.txt");
        std::getline(selected,descriptor.campaign.recovery_basename); CHECK(!descriptor.campaign.recovery_basename.empty());
    }
    std::unique_ptr<storage::Namespace> lease;
    const auto opened=(resume||recover)?storage::reopen(descriptor,lease):storage::prepare(descriptor,lease);
    if(!opened.ok()) std::printf("storage=%s win32=%u\n",storage::outcome_name(opened.outcome),opened.win32_error);
    CHECK(opened.ok());
    recovery_namespace=lease->metadata().path;
    auto& owner=session(); const auto configured=owner.configure(descriptor,std::move(lease));
    if(recovery_case && !configured.ok()) { std::puts("REFUSED interrupted recovery or invalid campaign admission"); return 2; }
    if (defect==L"refuse_configuration") {
        CHECK(!configured.ok()); const auto trace=owner.btrace.snapshot(); CHECK(trace.first_failure.sequence);
        CHECK(trace.first_failure.stage==(resume?BStage::resume:BStage::creation));
        CHECK(std::strcmp(trace.first_failure.predicate,"resume_checkpoint_incomplete_or_invalid")!=0);
        std::puts("PASS refused immutable or incomplete campaign contract with exact diagnostic predicate"); return 0;
    }
    CHECK(configured.ok());
    std::array<uintptr_t,20> table{}; Remote remote{table.data(),{}};
    table[0]=reinterpret_cast<uintptr_t>(put); table[1]=reinterpret_cast<uintptr_t>(get);
    table[13]=reinterpret_cast<uintptr_t>(exists); table[15]=reinterpret_cast<uintptr_t>(size);
    table[18]=reinterpret_cast<uintptr_t>(count); table[19]=reinterpret_cast<uintptr_t>(entry);
    const auto provider=reinterpret_cast<uintptr_t>(&remote);
    const auto directory=owner.native_root()+"/GAME-AUTOSAVE0";
    if(recovery_case && (resume||recover)) load_remote(remote);
    else remote.files[owner.native_root()+"/sentinel-owner-"+owner.namespace_id()+".txt"]=owner.ownership_record();
    if (defect==L"startup_parser") {
        startup_route_fixture::exercise(owner,6);
        const auto first=owner.unrouted_trace(); const auto before=owner.campaign_run.snapshot();
        ParserObservation observed; observed.data=0x1234; observed.caller=image+0x148c1c1; observed.native_completion=true;
        observed.directory="GAME-AUTOSAVE7"; observed.directory_read=true; observed.prefix="GAME-"; observed.prefix_read=true;
        owner.campaign_run.observe_parser(observed);
        CHECK(!owner.campaign_run.parser_enter(observed.data));
        const auto after=owner.campaign_run.snapshot();
        CHECK(after.phase==before.phase && after.reason==before.reason && !after.parser_completed && !after.native_saved);
        CHECK(after.parser_observation.disposition=="downstream_of_terminal_session");
        CHECK(after.parser_observation.source=="foreign_or_unowned_campaign" && after.parser_observation.native_completion);
        CHECK(owner.fault()==SessionFault::missed_startup && owner.unrouted_trace().at_ms==first.at_ms);
        std::puts("PASS startup refusal remains first; downstream foreign campaign parser never imports"); return 0;
    }
    startup_route_fixture::exercise(owner);
    CHECK(owner.startup_enter(0x1000,0x2000,GetCurrentThreadId()));
    if(!recover) CHECK(owner.observe_provider_objects(0x9000,0x9100,provider));
    if(recover) {
        if(defect==L"native_read_c_wrong_user") ++fixture_user;
        if(defect.rfind(L"native_read_c_interrupt",0)==0) recovery_interrupt=std::stoi(defect.substr(23));
        if(defect==L"native_read_c_unavailable") fixture_user=0;
        lose_provider=defect==L"native_read_c_provider_loss";
        concurrent_auxiliary=defect==L"native_read_c_concurrent_auxiliary";
        concurrent_target=defect==L"native_read_c_concurrent" || concurrent_auxiliary;
        RecoveryMemory native_memory; recovery_context_remote=provider;
        const auto completed=provider_initialized(owner,native_memory,0x9000,{image,recovery_context,steam_owner});
        const auto trace=owner.btrace.snapshot();
        std::printf("%s recovery %s\n",completed?"PASS":"REFUSED",trace.stages[static_cast<size_t>(BStage::resume)].predicate);
        persist_remote(remote); return completed?0:2;
    }
    CHECK(owner.bind_provider(owner.native_root(),provider,owner.ownership_record())); owner.startup_leave(false);
    CHECK(owner.provider_operation(provider,0x7788));
    CHECK(owner.publish_profile_catalog(provider,owner.ownership_record(),resume?std::vector<std::string>{"AUTOSAVE0"}:std::vector<std::string>{},"AUTOSAVE0",0,!resume,0));
    int outcome = 0;
    exercise_campaign_profile(owner,[&](const std::function<void()>& profile_write) {
    outcome = [&]() -> int {
    std::string payload=resume && profile_lifecycle?"second encoded checkpoint payload":"synthetic encoded checkpoint payload";
    std::string relative="game.details", native_directory=directory;
    std::array<unsigned char,0x190> file{}; std::array<unsigned char,0x280> data{};
    store(file,0,image+0x2a575a8); store(file,8,text(relative));
    const uint64_t length=payload.size(); const uintptr_t buffer=reinterpret_cast<uintptr_t>(payload.data());
    store(file,0x150,length); store(file,0x158,length); store(file,0x168,buffer); file[0x178]=1;
    const uintptr_t files=reinterpret_cast<uintptr_t>(file.data()), source=reinterpret_cast<uintptr_t>(data.data());
    const std::array<uintptr_t,1> file_list{files};
    store(data,0,text(native_directory)); store(data,0x1c0,reinterpret_cast<uintptr_t>(file_list.data())); store(data,0x1c8,int32_t{1}); store(data,0x1cc,int32_t{1});
    std::array<std::string,3> extra_names{"game_duration.dat","game.details-BACKUP","game_duration.dat-BACKUP"};
    std::array<std::string,3> extra_payloads{"encoded duration checkpoint","older details checkpoint","older duration checkpoint"};
    std::array<std::array<unsigned char,0x190>,3> extra_files{};
    std::array<uintptr_t,4> all_files{files};
    if (native_read) {
        for (size_t i=0;i<extra_files.size();++i) {
            extra_files[i]=file; store(extra_files[i],8,text(extra_names[i]));
            store(extra_files[i],0x150,uint64_t(extra_payloads[i].size())); store(extra_files[i],0x158,uint64_t(extra_payloads[i].size()));
            store(extra_files[i],0x168,reinterpret_cast<uintptr_t>(extra_payloads[i].data()));
            all_files[i+1]=reinterpret_cast<uintptr_t>(extra_files[i].data());
            if (resume && !recovery_case) remote.files[directory+"/"+extra_names[i]]=extra_payloads[i];
        }
        const int32_t file_count=retail_pair?2:4;
        store(data,0x1c0,reinterpret_cast<uintptr_t>(all_files.data())); store(data,0x1c8,file_count); store(data,0x1cc,file_count);
        if (resume && !recovery_case) remote.files[directory+"/game.details"]=payload;
    }
    engine::LocalMemory memory;
    // A later save owns a different native SaveData/file and different bytes;
    // reopening checkpoint2 must not accidentally validate checkpoint1.
    std::string next_payload=resume?"third encoded checkpoint payload":"second encoded checkpoint payload";
    auto next_file=file; auto next_data=data;
    const uintptr_t next_stream=reinterpret_cast<uintptr_t>(next_file.data()), next_source=reinterpret_cast<uintptr_t>(next_data.data());
    const std::array<uintptr_t,1> next_list{next_stream};
    store(next_file,0x150,uint64_t{next_payload.size()}); store(next_file,0x158,uint64_t{next_payload.size()});
    store(next_file,0x168,reinterpret_cast<uintptr_t>(next_payload.data()));
    store(next_data,0x1c0,reinterpret_cast<uintptr_t>(next_list.data()));
    unsigned profile_writes=0;
    const auto write_profile=[&] {
        profile_write(); ++profile_writes;
        CHECK(owner.native_io() && owner.accepts_requests() && !owner.btrace.snapshot().first_failure.sequence);
    };
    if (resume) {
        CHECK(owner.campaign_run.snapshot().source_checkpoint==(profile_lifecycle?2u:1u));
        if (native_read) {
            writer_fixture::Model reader{remote,source,files,payload,directory};
            const bool corrupt=defect==L"native_read_hash";
            const bool metadata_ok=writer_fixture::load(reader,true,corrupt?L"native_read":defect);
            if (!metadata_ok) {
                const std::map<std::wstring,const char*> expected{
                    {L"native_read_missing","resume_file_count_mismatch"},
                    {L"native_read_duplicate","resume_file_duplicate"},
                    {L"native_read_mixed","resume_file_group_mixed"},
                    {L"native_read_failed","ordinary_native_decode_result"},
                    {L"native_read_wrong_mode","load_parser_source_not_correlated"},
                    {L"native_read_wrong_caller","load_parser_source_not_correlated"}};
                const auto first=owner.btrace.snapshot().first_failure;
                CHECK(expected.count(defect) && std::strcmp(first.predicate,expected.at(defect))==0);
                CHECK(first.operation==0 && !owner.campaign_run.snapshot().parser_completed);
                CHECK(owner.campaign_run.snapshot().checkpoint==1);
                std::puts("PASS precise ordinary-read/parser refusal without invented checkpoint completion"); return 0;
            }
            CHECK(owner.campaign_run.snapshot().phase=="armed");
            CHECK(owner.campaign_run.begin_resume());
            if (corrupt) remote.files[directory+"/game.details"][0]='X';
            const bool loaded=writer_fixture::load(reader,false,defect);
            if (corrupt) {
                CHECK(!loaded); const auto first=owner.btrace.snapshot().first_failure;
                CHECK(std::strcmp(first.predicate,"resume_payload_hash_mismatch")==0 && diagnostic_fact(first,"file_index")==1);
                CHECK(!owner.campaign_run.snapshot().parser_completed);
                std::puts("PASS explicit gameplay source hash failure after successful menu hydration"); return 0;
            }
            CHECK(loaded && owner.campaign_run.allow_difficulty(difficulty));
            payload="new primary checkpoint after native Continue";
            store(file,0x150,uint64_t(payload.size())); store(file,0x158,uint64_t(payload.size()));
            store(file,0x168,reinterpret_cast<uintptr_t>(payload.data()));
            CHECK(native_transition(difficulty,L"",[&] {
                CHECK(owner.campaign_run.snapshot().phase=="reopened");
                const auto next_backup=defect==L"native_read_c_backup_next"?
                    std::make_shared<BackupJob>(GetCurrentProcessId(),1,GetTickCount64()+30000,steam_owner):std::shared_ptr<BackupJob>{};
                writer_fixture::save(reader,L"queued_checkpoint",next_backup);
                if(next_backup) {
                    const auto progress=next_backup->progress(); CHECK(progress.state==BackupState::complete);
                    std::ofstream selected(std::filesystem::path(argv[2])/"selected-backup.txt");
                    selected<<std::filesystem::path(progress.output.path).filename().string()<<'\n';
                }
            })==0);
            const auto saved=owner.campaign_run.snapshot();
            CHECK(saved.checkpoint==2 && saved.source_checkpoint==1 && saved.native_saved && saved.readback_verified && saved.continuity_persisted);
            CHECK(remote.files.at(directory+"/game.details")==payload);
            std::printf("PASS separate-process menu/read/parser/Continue/save checkpoint=2 files=%u pid=%lu\n",retail_pair?2u:4u,GetCurrentProcessId()); return 0;
        }
        CHECK(owner.campaign_run.begin_resume());
        CHECK(owner.campaign_run.allow_access(source,directory,false,false));
        if (defect==L"wrong_source") {
            payload[0]='X'; CHECK(!owner.campaign_run.verify_source(memory,source,image));
            const auto failure=owner.btrace.snapshot().first_failure;
            CHECK(failure.stage==BStage::resume && std::strcmp(failure.predicate,"resume_payload_hash_mismatch")==0);
            CHECK(diagnostic_fact(failure,"file_index")==0 && diagnostic_fact(failure,"hash_equal")==0);
            CHECK(diagnostic_fact(failure,"actual_size")==static_cast<int64_t>(payload.size()));
            CHECK(diagnostic_fact(failure,"expected_size")==static_cast<int64_t>(payload.size()));
            CHECK(!owner.campaign_run.parser_enter(source));
            const auto after=owner.btrace.snapshot(); CHECK(after.first_failure.sequence==failure.sequence);
            CHECK(after.stages[static_cast<size_t>(BStage::parser)].status==BStatus::blocked);
            std::puts("PASS source hash refusal before parser, exact inputs and first cause retained"); return 0;
        }
        CHECK(owner.campaign_run.verify_source(memory,source,image));
        CHECK(owner.campaign_run.parser_enter(source));
        owner.campaign_run.parser_leave(defect==L"failed_parser"?0x10:0);
        if (defect==L"failed_parser") {
            const auto failure=owner.btrace.snapshot().first_failure;
            CHECK(failure.stage==BStage::parser && std::strcmp(failure.predicate,"native_load_parser_failed")==0);
            CHECK(diagnostic_fact(failure,"native_result")==0x10);
            CHECK(!owner.campaign_run.map_begin("game/sp/initial",1));
            CHECK(owner.btrace.snapshot().first_failure.sequence==failure.sequence);
            std::puts("PASS original parser failure and result retained"); return 0;
        }
        if (defect==L"wrong_difficulty") { CHECK(!owner.campaign_run.allow_difficulty((difficulty+1)%4)); CHECK(!owner.accepts_requests()); return 0; }
        if (defect==L"missing_difficulty") { CHECK(!owner.campaign_run.map_begin("game/sp/initial",1)); return 0; }
        CHECK(owner.campaign_run.allow_difficulty(difficulty)); // Native consumer restores the parsed save field.
        if (defect==L"wrong_map") { CHECK(!owner.campaign_run.map_begin("other/map",1)); return 0; }
        if (profile_lifecycle) {
            writer_fixture::Model next{remote,next_source,next_stream,next_payload,directory};
            CHECK(native_transition(difficulty,L"",[&] {
                CHECK(owner.campaign_run.snapshot().phase=="reopened");
                write_profile(); writer_fixture::save(next,L"queued_checkpoint");
                CHECK(owner.campaign_run.snapshot().checkpoint==3);
            },{},[&] {
                CHECK(!owner.campaign_run.snapshot().map_active && !owner.campaign_run.snapshot().save_ready);
                write_profile();
            })==0);
            const auto saved=owner.campaign_run.snapshot();
            CHECK(saved.source_checkpoint==2 && saved.checkpoint==3 && saved.parser_completed);
            CHECK(saved.native_saved && saved.readback_verified && saved.continuity_persisted && !saved.map_active);
            CHECK(saved.operation==next.operation && remote.files.at(directory+"/game.details")==next_payload);
            CHECK(profile_writes==2);
            std::printf("PASS profile lifecycle separate-process checkpoint=3 source_checkpoint=2 profile_writes=%u difficulty=%u pid=%lu\n",
                profile_writes,difficulty,GetCurrentProcessId()); return 0;
        }
        CHECK(native_transition(difficulty)==0);
        CHECK(owner.campaign_run.snapshot().phase=="reopened");
        const auto resumed_trace=owner.btrace.snapshot();
        for (auto stage:{BStage::resume,BStage::parser,BStage::transition,BStage::difficulty}) diagnostic_stage_observed(resumed_trace,stage);
        CHECK(!resumed_trace.first_failure.sequence);
        profile_write();
        std::printf("PASS separate-process native source/parser/lifecycle reopen, difficulty=%u pid=%lu\n",difficulty,GetCurrentProcessId()); return 0;
    }
    if (defect==L"dirty") {
        CHECK(!owner.campaign_run.begin_create(false,"AUTOSAVE0",0,true));
        CHECK(!owner.campaign_run.snapshot().map_active); std::puts("PASS dirty-process refusal before reservation"); return 0;
    }
    navigation_fixture::create(difficulty,defect);
    if (defect==L"extra_life" || defect==L"ultra") return 0;
    if (profile_lifecycle) {
        CHECK(!owner.campaign_run.snapshot().map_active && owner.campaign_run.snapshot().phase=="native_start_queued");
        write_profile(); // New Game's PROFILE save precedes map readiness.
    }
    const bool transition_failure=defect==L"native_return" || defect==L"abnormal" || defect==L"state_read" || defect==L"generation" || defect==L"difficulty" || defect==L"terminal_cleanup";
    if(transition_failure) { CHECK(native_transition(difficulty,defect)==0); return 0; }
    writer_fixture::Model writer{remote,source,files,payload,directory};
    std::shared_ptr<BackupJob> c_backup;
    if(recovery_case) c_backup=std::make_shared<BackupJob>(GetCurrentProcessId(),1,GetTickCount64()+30000,steam_owner);
    if (profile_lifecycle) {
        writer_fixture::Model next{remote,next_source,next_stream,next_payload,directory};
        CHECK(native_transition(difficulty,L"initial_save",[&] {
            CHECK(!owner.campaign_run.snapshot().map_active);
            write_profile(); // Initial cutscene: native map transition has not returned.
            writer_fixture::save(writer,L"");
            CHECK(!owner.campaign_run.snapshot().continuity_persisted);
        },[&] {
            CHECK(owner.campaign_run.snapshot().checkpoint==1 && owner.campaign_run.snapshot().continuity_persisted);
            CHECK(remote.files.at(directory+"/game.details")==payload);
            write_profile(); writer_fixture::save(next,L"queued_checkpoint");
            CHECK(next.operation && next.operation!=writer.operation);
            CHECK(owner.campaign_run.snapshot().checkpoint==2);
        },[&] {
            CHECK(!owner.campaign_run.snapshot().map_active && !owner.campaign_run.snapshot().save_ready);
            write_profile(); // ExitMainMenu still emits the native PROFILE save.
        })==0);
        const auto saved=owner.campaign_run.snapshot();
        CHECK(saved.checkpoint==2 && saved.native_saved && saved.readback_verified && saved.continuity_persisted);
        CHECK(!saved.map_active && !saved.save_ready && saved.operation==next.operation);
        CHECK(remote.files.at(directory+"/game.details")==next_payload && profile_writes==4);
        CHECK(!owner.btrace.snapshot().first_failure.sequence);
        std::printf("PASS profile lifecycle checkpoint=2 profile_writes=%u difficulty=%u pid=%lu\n",
            profile_writes,difficulty,GetCurrentProcessId()); return 0;
    }
    CHECK(native_transition(difficulty,defect==L"pending"?L"pending_save":defect,[&] {
        // Native PROFILE preparation receives the user context, then the exact
        // campaign factory/provider/readback chain reaches checkpoint 1.
        if (defect.empty() || defect==L"queued_checkpoint" || defect==L"menu_pending_save") profile_write();
        if(retail_pair) for(size_t i=1;i<extra_names.size();++i) remote.files[directory+"/"+extra_names[i]]=extra_payloads[i];
        writer_fixture::save(writer,defect==L"pending"?L"pending_save":defect,c_backup);
    })==0);
    if(defect==L"pending" || defect==L"initial_save_return_failed" || defect==L"unassociated" || defect==L"queued_foreign" || defect==L"save_failure" || defect==L"partial_save" || defect==L"unrelated") {
        CHECK(!owner.campaign_run.snapshot().continuity_persisted); return 0;
    }
    const auto observed=owner.campaign_run.snapshot();
    CHECK(observed.native_saved && observed.readback_verified && observed.continuity_persisted && observed.checkpoint==1);
    CHECK(observed.operation==writer.operation && observed.effective_difficulty==difficulty && observed.native_factory_matched);
    const auto trace=owner.btrace.snapshot();
    for (auto stage:{BStage::creation,BStage::difficulty,BStage::transition,BStage::checkpoint_factory,BStage::continuity}) diagnostic_stage_observed(trace,stage);
    CHECK(!trace.first_failure.sequence);
    const auto& continuity=trace.stages[static_cast<size_t>(BStage::continuity)];
    CHECK(continuity.status==BStatus::succeeded && std::strcmp(continuity.predicate,"checkpoint_continuity_persisted")==0);
    CHECK(diagnostic_fact(continuity,"checkpoint")==1 && diagnostic_fact(continuity,"continuity_persisted")==1);
    if(c_backup) {
        const auto progress=c_backup->progress();
        CHECK(progress.state==BackupState::complete && progress.operation==observed.operation);
        const auto basename=std::filesystem::path(progress.output.path).filename().string();
        std::ofstream selected(std::filesystem::path(argv[2])/"selected-backup.txt"); selected<<basename<<'\n';
        std::printf("PASS explicit verified backup operation=%llu basename=%s\n",static_cast<unsigned long long>(progress.operation),basename.c_str());
    }
    std::printf("PASS native checkpoint correlation/continuity, difficulty=%u pid=%lu\n",difficulty,GetCurrentProcessId()); return 0;
    }();
    });
    persist_remote(remote);
    return outcome;
}
