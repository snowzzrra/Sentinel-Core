// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Synthetic native/provider boundary. The controller runs create and resume in
// separate host processes; no game, Steam, original save or gameplay is used.
#include "save_session.h"
#include "save_collector.h"
#include "save_native_hooks.h"
#include "save_write.h"
#include "save_submission.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"campaign:%d: %s\n",__LINE__,#x); return 1; } } while(0)
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
struct CheckpointRequest { Session& owner; uint64_t operation; const std::string& directory; bool matched=false; };
SaveReference* checkpoint_factory(uintptr_t,SaveReference* out,uint32_t,uintptr_t request) {
    auto& item=*reinterpret_cast<CheckpointRequest*>(request);
    item.matched=item.owner.campaign_run.write_started(item.operation,item.directory,capture_native_checkpoint(item.owner.native_writes));
    return out;
}
}
int wmain(int argc,wchar_t** argv) {
    CHECK(argc==4 || argc==5);
    const std::wstring mode=argv[1]; const bool resume=mode==L"resume";
    const auto difficulty=static_cast<uint32_t>(std::wcstoul(argv[3],nullptr,10));
    const std::wstring defect=argc==5?argv[4]:L"";
    storage::Descriptor descriptor{{"synthetic-campaign-host",0,1,std::string(64,'b')},argv[2],
        {resume?storage::CampaignIntent::resume:storage::CampaignIntent::create,difficulty}};
    std::unique_ptr<storage::Namespace> lease;
    CHECK((resume?storage::reopen(descriptor,lease):storage::prepare(descriptor,lease)).ok());
    Session owner; const auto configured=owner.configure(descriptor,std::move(lease));
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
        CHECK(owner.campaign_run.map_begin("game/sp/initial",1)); owner.campaign_run.map_end(true,2,difficulty);
        CHECK(owner.campaign_run.snapshot().phase=="reopened");
        std::printf("PASS separate-process native source/parser/lifecycle reopen, difficulty=%u pid=%lu\n",difficulty,GetCurrentProcessId()); return 0;
    }
    if (defect==L"dirty") {
        CHECK(!owner.campaign_run.begin_create(false,"AUTOSAVE0",0,true));
        CHECK(!owner.campaign_run.snapshot().map_active); std::puts("PASS dirty-process refusal before reservation"); return 0;
    }
    CHECK(owner.campaign_run.begin_create(true,"AUTOSAVE0",0,true));
    CHECK(!owner.campaign_run.allow_difficulty((difficulty+1)%4) && owner.accepts_requests());
    CHECK(owner.campaign_run.start_internal(difficulty,false));
    CHECK(owner.campaign_run.map_begin("game/sp/initial",1)); owner.campaign_run.map_end(true,2,difficulty);
    CHECK(owner.campaign_run.allow_access(source,directory,true,false));
    auto& writes=owner.native_writes;
    const auto operation=writes.open_provider(source,directory);
    if (defect==L"unassociated") { CHECK(!owner.campaign_run.write_started(operation,directory,false)); return 0; }
    CheckpointRequest request{owner,operation,directory}; SaveReference task{};
    native_save_factory(writes,0x674744,0x674744,0x1000,&task,0,reinterpret_cast<uintptr_t>(&request),checkpoint_factory);
    CHECK(request.matched && !capture_native_checkpoint(writes));
    if (defect==L"pending") { CHECK(!owner.campaign_run.snapshot().continuity_persisted); return 0; }
    CHECK(writes.attach_files(operation,files,1));
    CHECK(writes.attach_readback(operation,source+0x10000,1));
    const auto sequence=writes.begin(provider,files,1,directory); CHECK(sequence);
    CHECK(prepare_sdk_payloads(writes,memory,sequence,files,1,directory,image));
    SdkWriteObservation prepared; CHECK(writes.inspect(sequence,prepared)); auto captured=prepared.payloads[0]; captured.captured=true;
    CHECK(writes.capture(sequence,captured)==0); writes.submitted(sequence,0,123); writes.callback(123,false,1);
    writes.result(sequence,{0,0,1,0}); writes.provider_result(operation,{0,0,1});
    owner.campaign_run.write_observed(operation,false,false,memory);
    CHECK(owner.campaign_run.snapshot().native_saved && !owner.campaign_run.snapshot().readback_verified && !owner.campaign_run.snapshot().continuity_persisted);
    writes.readback_hashes(operation); writes.readback_result(operation,true);
    CHECK(!writes.backup(operation)); // Ordinary save has no archive dependency.
    remote.files[directory+"/game.details"]=payload;
    owner.campaign_run.write_observed(operation+1,true,true,memory); CHECK(!owner.campaign_run.snapshot().continuity_persisted);
    owner.campaign_run.write_observed(operation,true,true,memory);
    const auto observed=owner.campaign_run.snapshot();
    CHECK(observed.native_saved && observed.readback_verified && observed.continuity_persisted && observed.checkpoint==1);
    CHECK(observed.operation==operation && observed.effective_difficulty==difficulty && observed.native_factory_matched);
    std::printf("PASS native checkpoint correlation/continuity, difficulty=%u pid=%lu\n",difficulty,GetCurrentProcessId()); return 0;
}
