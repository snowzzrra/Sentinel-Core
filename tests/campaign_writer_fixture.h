// Real scoped provider, SDK preparation/completion and owned readback adapters.
// Only native futures, allocations and transport are task-owned substitutes.
#include "save_readback.h"
namespace writer_fixture {
template<class T> T& field(uintptr_t p,size_t offset) { return *reinterpret_cast<T*>(p+offset); }
struct Control { uint32_t strong,weak; uintptr_t data; DestroySaveData destroy; };
struct Model {
    Remote& remote; uintptr_t source,file; std::string& payload; std::string directory;
    bool failure=false,pending=false,queued=false,foreign=false,read_failure=false; unsigned creates=0,reads=0,writer_polls=0; uint64_t operation=0;
    SaveFuture* future=nullptr; SaveReference source_ref{};
    std::vector<unsigned char> sdk_file; std::vector<std::string> sdk_bytes;
};
Model* model=nullptr;
engine::LocalMemory memory;
void* allocate(size_t n) { return std::calloc(1,n); }
void name(uintptr_t data,const char* value) {
    auto& s=field<NativeString>(data,0); s.data=reinterpret_cast<char*>(data+0x30);
    s.length=static_cast<int32_t>(std::strlen(value)); CHECK(s.length<64); std::memcpy(s.data,value,s.length+1);
}
void* construct(void* p) {
    auto data=reinterpret_cast<uintptr_t>(p); name(data,""); field<uintptr_t>(data,0x1c0)=data+0x1d8; field<int32_t>(data,0x1cc)=16; return p;
}
void destroy_data(uintptr_t data) {
    session().forget_save_data(data);
    const auto vector=field<uintptr_t>(data,0x1c0);
    for (int i=0;i<field<int32_t>(data,0x1c8);++i) {
        auto stream=field<uintptr_t>(vector,i*8); std::free(reinterpret_cast<void*>(field<uintptr_t>(stream,0x168))); std::free(reinterpret_cast<void*>(stream));
    }
    std::free(reinterpret_cast<void*>(data));
}
void release(SaveReference* source) {
    auto* c=reinterpret_cast<Control*>(source->control); source->control=0;
    if (c && !--c->strong) { if(c->destroy) c->destroy(c->data); std::free(c); }
}
SaveReference* retain(SaveReference* out,const SaveReference* source) { *out=*source; if(out->control) ++reinterpret_cast<Control*>(out->control)->strong; return out; }
void* stream(void* p,const char* value,uint32_t flags) {
    auto data=reinterpret_cast<uintptr_t>(p); field<uintptr_t>(data,0)=image+0x2a575a8;
    auto& s=field<NativeString>(data,8); s.data=reinterpret_cast<char*>(data+0x38); s.length=static_cast<int32_t>(std::strlen(value));
    std::memcpy(s.data,value,s.length+1); field<uint32_t>(data,0x180)=flags; return p;
}
struct Future:SaveFuture { SaveReference data{}; unsigned stage=0; };
SaveFuture* destroy(SaveFuture* base,uint32_t) { auto* f=static_cast<Future*>(base); release(&f->data); delete f; return base; }
ReadWorkerResult* prepare(uintptr_t context,ReadWorkerResult* out,SaveReference* waiter) {
    auto remote=field<uintptr_t>(context,0),data=reinterpret_cast<Control*>(field<uintptr_t>(context,8))->data;
    auto get_size=reinterpret_cast<int32_t(*)(uintptr_t,const char*)>(field<uintptr_t>(field<uintptr_t>(remote,0),0x78));
    for (int i=0;i<field<int32_t>(data,0x1c8);++i) {
        auto file=field<uintptr_t>(field<uintptr_t>(data,0x1c0),i*8);
        const auto key=model->directory+"/"+field<NativeString>(file,8).data;
        auto bytes=get_size(remote,key.c_str()); CHECK(bytes>0);
        field<uint64_t>(file,0x150)=bytes; field<uint64_t>(file,0x158)=bytes;
        field<uintptr_t>(file,0x168)=reinterpret_cast<uintptr_t>(allocate(bytes));
    }
    waiter->control=0; *out={0,1,0}; return out;
}
ReadWorkerResult* decode(uintptr_t,ReadWorkerResult* out) {
    *out=model->read_failure?ReadWorkerResult{1,4,0}:ReadWorkerResult{0,0x80009101u,0xa5a5a5a5u};
    return out;
}
SaveResult* poll_read(SaveFuture* base,SaveResult* out,void*) {
    auto* f=static_cast<Future*>(base); ReadWorkerResult result{};
    if (!f->stage++) {
        std::array<uintptr_t,2> context{reinterpret_cast<uintptr_t>(&model->remote),f->data.control}; SaveReference waiter{};
        prepare_readback(session(),memory,reinterpret_cast<uintptr_t>(context.data()),&result,&waiter,prepare);
        CHECK(!result.outcome); *out={-1,0,0,0}; return out;
    }
    auto data=reinterpret_cast<Control*>(f->data.control)->data;
    for (int i=0;i<field<int32_t>(data,0x1c8);++i) {
        auto file=field<uintptr_t>(field<uintptr_t>(data,0x1c0),i*8);
        const auto& bytes=model->remote.files.at(model->directory+"/"+field<NativeString>(file,8).data);
        std::memcpy(reinterpret_cast<void*>(field<uintptr_t>(file,0x168)),bytes.data(),bytes.size());
    }
    std::array<uintptr_t,2> context{f->data.control,0};
    verify_readback(session(),memory,reinterpret_cast<uintptr_t>(context.data()),&result,decode,image);
    *out={0,result.outcome,result.active_value(),0}; return out;
}
const SaveFutureVtable read_table{destroy,poll_read};
SaveFuture** read(uintptr_t,SaveFuture** out,uintptr_t identity,SaveReference* source) {
    CHECK(identity==0x7788); ++model->reads; auto* f=new Future; f->vtable=&read_table; f->data=*source; source->control=0; *out=f; return out;
}
unsigned parser_calls=0;
uint64_t parsed(SaveReference* source,uintptr_t,uintptr_t,uintptr_t) {
    ++parser_calls; release(source); return 0;
}
bool load(Model& m,bool metadata_only,const std::wstring& defect) {
    model=&m; m.read_failure=defect==L"native_read_failed";
    const auto before=session().campaign_run.snapshot();
    const auto remote_before=m.remote.files;
    auto data=reinterpret_cast<uintptr_t>(construct(allocate(0x2c0)));
    name(data,m.directory.c_str());
    std::vector<std::string> names{"game_duration.dat","game.details"};
    if (defect==L"native_read_backup") for(auto& n:names) n+="-BACKUP";
    if (defect==L"native_read_missing") names.pop_back();
    if (defect==L"native_read_duplicate") names[1]=names[0];
    if (defect==L"native_read_mixed") names[1]+="-BACKUP";
    for(size_t i=0;i<names.size();++i)
        field<uintptr_t>(field<uintptr_t>(data,0x1c0),i*8)=reinterpret_cast<uintptr_t>(stream(allocate(0x190),names[i].c_str(),0x41));
    field<int32_t>(data,0x1c8)=static_cast<int32_t>(names.size());
    auto* control=static_cast<Control*>(allocate(sizeof(Control))); *control={1,1,data,destroy_data};
    SaveReference source_ref{reinterpret_cast<uintptr_t>(control)},parser_ref{}; retain(&parser_ref,&source_ref);
    WriteCalls calls{read,name,release,nullptr,image,nullptr,nullptr}; SaveFuture* future=nullptr;
    read_scoped(session(),memory,reinterpret_cast<uintptr_t>(&m.remote),&future,0x7788,&source_ref,calls);
    CHECK(!source_ref.control && future);
    SaveResult result{}; std::array<unsigned char,32> task{};
    future->vtable->poll(future,&result,task.data()); CHECK(result.state==-1);
    const auto pending=session().campaign_run.snapshot();
    CHECK(pending.operation==before.operation && pending.checkpoint==before.checkpoint && !pending.native_saved);
    future->vtable->poll(future,&result,task.data()); future->vtable->destroy(future,1);
    CHECK(m.remote.files==remote_before);
    if (result.outcome || result.state) { release(&parser_ref); return false; }
    CHECK(session().campaign_run.snapshot().operation==0 && session().native_writes.snapshot(0).flags==0);
    const auto decoded=session().campaign_run.snapshot();
    CHECK(decoded.source_verified==!metadata_only && !decoded.parser_completed);
    std::array<unsigned char,0xc0> request{}; request[0xb8]=metadata_only?1:0;
    if (defect==L"native_read_wrong_mode") request[0xb8]=0;
    test_campaign_binding(image,0x1000); test_campaign_parser(parsed,release);
    const auto count_before=parser_calls;
    const auto value=test_campaign_parse(image+(defect==L"native_read_wrong_caller"?0x148c1c2:0x148c1c1),&parser_ref,
        reinterpret_cast<uintptr_t>(request.data()));
    CHECK(!parser_ref.control && parser_calls==count_before+(value==0?1u:0u));
    if (value) return false;
    const auto completed=session().campaign_run.snapshot();
    if (metadata_only) {
        CHECK(completed.phase=="armed" && !completed.source_verified && !completed.parser_completed && !completed.map_active);
        CHECK(completed.parser_observation.disposition=="verified_metadata_read");
    }
    else CHECK(completed.phase=="parser_succeeded" && completed.source_verified && completed.parser_completed && !completed.map_active);
    CHECK(!session().btrace.snapshot().first_failure.sequence && completed.checkpoint==before.checkpoint);
    return true;
}
SaveResult* poll_write(SaveFuture*,SaveResult* out,void*) {
    auto& m=*model; auto& writes=session().native_writes; m.operation=WritePollScope::current(writes); CHECK(m.operation);
    if(m.pending) { *out={-1,0,0,0}; return out; }
    CHECK(!m.writer_polls++);
    // Native preparation constructs a separate idFile_Memory transport object;
    // the provider and subsequent readback retain their idFile_SaveGame type.
    const auto count=field<int32_t>(m.source,0x1c8);
    CHECK(count>0 && count<=64);
    m.sdk_file.resize(static_cast<size_t>(count)*0x180); m.sdk_bytes.resize(count);
    const auto prepared_file=reinterpret_cast<uintptr_t>(m.sdk_file.data());
    for (int i=0;i<count;++i) {
        auto source=field<uintptr_t>(field<uintptr_t>(m.source,0x1c0),i*8),entry=prepared_file+i*0x180;
        CHECK(field<uintptr_t>(source,0)==image+0x2a575a8);
        std::memcpy(reinterpret_cast<void*>(entry),reinterpret_cast<void*>(source),0x180);
        field<uintptr_t>(entry,0)=image+0x2a57348; name(entry+8,field<NativeString>(source,8).data);
        m.sdk_bytes[i].assign(reinterpret_cast<char*>(field<uintptr_t>(source,0x168)),field<uint64_t>(source,0x150));
        field<uintptr_t>(entry,0x168)=reinterpret_cast<uintptr_t>(m.sdk_bytes[i].data());
        field<uint8_t>(entry,0x178)=1;
    }
    CHECK(writes.attach_files(m.operation,prepared_file,count));
    std::array<unsigned char,0x60> context{}; store(context,0,reinterpret_cast<uintptr_t>(&m.remote));
    auto native_name=text(m.directory); store(context,8,native_name); store(context,0x40,prepared_file); store(context,0x48,uint64_t(count)); store(context,0x50,uint64_t(count));
    WritePreflightResult result{};
    preflight_scoped(session(),memory,reinterpret_cast<uintptr_t>(context.data()),&result,
        [](uintptr_t,WritePreflightResult* value) { *value={}; return value; },image);
    CHECK(!result.tag);
    auto sequence=writes.snapshot(m.operation).sdk_sequence; SdkWriteObservation prepared; CHECK(writes.inspect(sequence,prepared));
    // Each native async submission has its own handle. Reusing 123 would send
    // a later checkpoint's callback to the first operation's retained receipt.
    for (int i=0;i<count;++i) {
        auto file=prepared.payloads.at(i); file.captured=true; CHECK(writes.capture(sequence,file)==static_cast<uint32_t>(i));
        const auto handle=m.operation*64+i;
        writes.submitted(sequence,i,handle); writes.callback(handle,m.failure,m.failure?0:1);
        if (!m.failure) m.remote.files[file.name.data()]=m.sdk_bytes[i];
    }
    writes.result(sequence,{0,m.failure?1u:0u,1,0});
    writes.released(prepared_file);
    CHECK(field<uintptr_t>(m.file,0)==image+0x2a575a8);
    *out={0,m.failure?1:0,1,0}; return out;
}
const SaveFutureVtable write_table{destroy,poll_write};
SaveFuture** write(uintptr_t,SaveFuture** out,uintptr_t identity,SaveReference* source) {
    CHECK(identity==0x7788); ++model->creates; auto* f=new Future; f->vtable=&write_table; f->data=*source; source->control=0; *out=f; return out;
}
SaveReference* factory(uintptr_t,SaveReference* out,uint32_t,uintptr_t) {
    ReadbackCalls readback{{allocate,construct,destroy_data,{nullptr,retain,release,name,nullptr,image},read},stream};
    WriteCalls calls{write,name,release,nullptr,image,nullptr,&readback};
    if (model->queued) campaign_write_provider(image+(model->foreign?0x14897c4:0x14897c3),memory,
        reinterpret_cast<uintptr_t>(&model->remote),&model->future,0x7788,&model->source_ref,calls);
    else write_scoped(session(),memory,reinterpret_cast<uintptr_t>(&model->remote),&model->future,0x7788,&model->source_ref,calls);
    return out;
}
void save(Model& m,const std::wstring& defect) {
    const auto before=session().campaign_run.snapshot();
    model=&m; m.failure=defect==L"save_failure"; m.pending=defect==L"pending_save";
    m.foreign=defect==L"queued_foreign"; m.queued=defect==L"queued_checkpoint" || m.foreign || defect==L"menu_pending_save";
    auto* control=static_cast<Control*>(allocate(sizeof(Control))); *control={1,1,m.source,nullptr}; m.source_ref.control=reinterpret_cast<uintptr_t>(control);
    SaveReference task{};
    if (m.queued) {
        CHECK(!capture_native_checkpoint(session().native_writes));
        factory(0x1000,&task,0,0); // Earlier mode0 factory has returned; no TLS scope remains.
    } else campaign_save_factory(defect==L"unassociated"?0:0x674744,0x674744,0x1000,&task,0,0,factory);
    CHECK(!m.source_ref.control && m.future && !capture_native_checkpoint(session().native_writes));
    const bool rejected=defect==L"unrelated" || defect==L"partial_save" || defect==L"unassociated" || m.foreign;
    CHECK(m.creates==(rejected?0u:1u));
    const auto started=session().campaign_run.snapshot();
    if (!rejected) {
        CHECK(started.operation && started.operation!=before.operation && started.checkpoint==before.checkpoint);
        CHECK(!started.continuity_persisted && !started.native_saved && !started.readback_verified);
    }
    SaveResult result{}; std::array<unsigned char,32> native_task{};
    m.future->vtable->poll(m.future,&result,native_task.data());
    if(!rejected && !m.failure && !m.pending) {
        const auto waiting=session().campaign_run.snapshot();
        CHECK(result.state==-1);
        CHECK(waiting.operation==started.operation && waiting.operation==m.operation && waiting.checkpoint==before.checkpoint);
        CHECK(!waiting.continuity_persisted && waiting.native_saved && !waiting.readback_verified);
        if (defect==L"menu_pending_save") {
            menu_transition();
            CHECK(session().native_io() && !session().campaign_run.snapshot().map_active);
        }
        m.future->vtable->poll(m.future,&result,native_task.data()); CHECK(!result.state && !result.outcome && result.value==1);
        const auto completed=session().campaign_run.snapshot();
        CHECK(completed.operation==m.operation && completed.readback_verified);
        CHECK(completed.checkpoint==before.checkpoint+(completed.continuity_persisted?1u:0u));
        CHECK(!session().native_writes.backup(m.operation));
    }
    m.future->vtable->destroy(m.future,1); m.future=nullptr;
}
}
