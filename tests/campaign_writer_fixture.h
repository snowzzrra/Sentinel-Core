// Real scoped provider, SDK preparation/completion and owned readback adapters.
// Only native futures, allocations and transport are task-owned substitutes.
#include "save_readback.h"
namespace writer_fixture {
template<class T> T& field(uintptr_t p,size_t offset) { return *reinterpret_cast<T*>(p+offset); }
struct Control { uint32_t strong,weak; uintptr_t data; DestroySaveData destroy; };
struct Model {
    Remote& remote; uintptr_t source,file; std::string& payload; std::string directory;
    bool failure=false,pending=false,queued=false,foreign=false; unsigned creates=0,reads=0,writer_polls=0; uint64_t operation=0;
    SaveFuture* future=nullptr; SaveReference source_ref{};
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
    auto file=field<uintptr_t>(field<uintptr_t>(data,0x1c0),0);
    auto get_size=reinterpret_cast<int32_t(*)(uintptr_t,const char*)>(field<uintptr_t>(field<uintptr_t>(remote,0),0x78));
    auto bytes=get_size(remote,(model->directory+"/game.details").c_str()); CHECK(bytes==model->payload.size());
    field<uint64_t>(file,0x150)=bytes; field<uint64_t>(file,0x158)=bytes; field<uintptr_t>(file,0x168)=reinterpret_cast<uintptr_t>(allocate(bytes));
    waiter->control=0; *out={0,1,0}; return out;
}
ReadWorkerResult* decode(uintptr_t,ReadWorkerResult* out) { *out={0,1,0}; return out; }
SaveResult* poll_read(SaveFuture* base,SaveResult* out,void*) {
    auto* f=static_cast<Future*>(base); ReadWorkerResult result{};
    if (!f->stage++) {
        std::array<uintptr_t,2> context{reinterpret_cast<uintptr_t>(&model->remote),f->data.control}; SaveReference waiter{};
        prepare_readback(session(),memory,reinterpret_cast<uintptr_t>(context.data()),&result,&waiter,prepare);
        CHECK(!result.outcome); *out={-1,0,0,0}; return out;
    }
    auto data=reinterpret_cast<Control*>(f->data.control)->data;
    auto file=field<uintptr_t>(field<uintptr_t>(data,0x1c0),0);
    std::memcpy(reinterpret_cast<void*>(field<uintptr_t>(file,0x168)),model->remote.files.at(model->directory+"/game.details").data(),model->payload.size());
    std::array<uintptr_t,2> context{f->data.control,0};
    verify_readback(session(),memory,reinterpret_cast<uintptr_t>(context.data()),&result,decode,image);
    *out={0,result.outcome,result.value,0}; return out;
}
const SaveFutureVtable read_table{destroy,poll_read};
SaveFuture** read(uintptr_t,SaveFuture** out,uintptr_t identity,SaveReference* source) {
    CHECK(identity==0x7788); ++model->reads; auto* f=new Future; f->vtable=&read_table; f->data=*source; source->control=0; *out=f; return out;
}
SaveResult* poll_write(SaveFuture*,SaveResult* out,void*) {
    auto& m=*model; auto& writes=session().native_writes; m.operation=WritePollScope::current(writes); CHECK(m.operation);
    if(m.pending) { *out={-1,0,0,0}; return out; }
    CHECK(!m.writer_polls++); CHECK(writes.attach_files(m.operation,m.file,1));
    std::array<unsigned char,0x60> context{}; store(context,0,reinterpret_cast<uintptr_t>(&m.remote));
    auto native_name=text(m.directory); store(context,8,native_name); store(context,0x40,m.file); store(context,0x48,uint64_t{1}); store(context,0x50,uint64_t{1});
    WritePreflightResult result{};
    preflight_scoped(session(),memory,reinterpret_cast<uintptr_t>(context.data()),&result,
        [](uintptr_t,WritePreflightResult* value) { *value={}; return value; },image);
    CHECK(!result.tag);
    auto sequence=writes.snapshot(m.operation).sdk_sequence; SdkWriteObservation prepared; CHECK(writes.inspect(sequence,prepared));
    auto file=prepared.payloads.at(0); file.captured=true; CHECK(writes.capture(sequence,file)==0);
    // Each native async submission has its own handle. Reusing 123 would send
    // a later checkpoint's callback to the first operation's retained receipt.
    const auto handle=m.operation;
    writes.submitted(sequence,0,handle); writes.callback(handle,m.failure,m.failure?0:1);
    CHECK(writes.inspect(sequence,prepared) && prepared.payloads.at(0).handle==handle && prepared.payloads.at(0).callback);
    writes.result(sequence,{0,m.failure?1u:0u,1,0});
    if(!m.failure) m.remote.files[m.directory+"/game.details"]=m.payload;
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
