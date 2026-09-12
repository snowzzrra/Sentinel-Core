// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Controlled native layouts and owned synthetic bytes; no game/Steam/save access.
#include "save_readback.h"
#include "native_model.h"
#include "protocol.h"
#include "save_backup.h"
#include "save_submission.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>

#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "FAIL readback:%d: %s\n", __LINE__, #v); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::save;
void run_submission_contracts();
namespace {
constexpr uintptr_t image = 0x10000000;
template<class T> T& field(uintptr_t p, size_t offset) { return *reinterpret_cast<T*>(p + offset); }
struct Control { uint32_t strong, weak; uintptr_t data; DestroySaveData destroy; };
struct Model {
    Session& owner; engine::LocalMemory memory;
    std::array<uintptr_t, 16> table{}; uintptr_t remote = reinterpret_cast<uintptr_t>(table.data());
    SaveReference job{};
    std::string directory, bytes = "native transport bytes";
    uint64_t operation = 0, sequence = 0; uintptr_t private_data = 0;
    unsigned mode = 0, allocations = 0, freed_data = 0, streams = 0, freed_streams = 0;
    unsigned reads = 0, writes = 0, read_polls = 0, writer_polls = 0, decoded = 0, prepared = 0, sizes = 0;
    unsigned fail_allocation = 0;
    bool identity_alive = true;
    explicit Model(Session& s) : owner(s), directory(s.native_root() + "/GAME-AUTOSAVE0") {}
};
Model* active = nullptr;
struct WriterSubmission { const WriteCalls* calls; SaveReference* source; SaveFuture** future; unsigned releases = 0; };
WriterSubmission* submitted_writer = nullptr;
SaveReference* submitted_factory(uintptr_t manager, SaveReference* task, uint32_t user, uintptr_t request) {
    CHECK(manager == 0x111 && user == 7 && request == 0x222);
    auto& m = *active; auto& submitted = *submitted_writer;
    write_scoped(m.owner, m.memory, reinterpret_cast<uintptr_t>(&m.remote), submitted.future,
        0x7788, submitted.source, *submitted.calls);
    task->control = 0x3344; return task;
}
SaveReference* submitted_root(uintptr_t root, SaveReference* out, uint8_t a, uint8_t b, uint8_t c) {
    CHECK(root == 0x123 && !a && !b && !c);
    return native_save_factory(active->owner.native_writes, 0x674744, 0x674744,
        0x111, out, 7, 0x222, submitted_factory);
}
void submitted_release(SaveReference* out) {
    CHECK(out->control == 0x3344); out->control = 0; ++submitted_writer->releases;
    // The modeled native manager keeps owning its provider future.
}
void* allocate(size_t size) {
    auto& m = *active;
    if (++m.allocations == m.fail_allocation) return nullptr;
    return std::calloc(1, size);
}
void set_name(uintptr_t data, const char* name) {
    auto& text = field<NativeString>(data, 0);
    text.data = reinterpret_cast<char*>(data + 0x30); text.length = static_cast<int32_t>(std::strlen(name));
    CHECK(text.length < 64); std::memcpy(text.data, name, static_cast<size_t>(text.length) + 1);
}
void* construct(void* object) {
    const auto data = reinterpret_cast<uintptr_t>(object);
    set_name(data, ""); field<uintptr_t>(data, 0x1c0) = data + 0x1d8; field<int32_t>(data, 0x1cc) = 16;
    return object;
}
void destroy_data(uintptr_t data) {
    if (!data) return;
    auto& m = *active; m.owner.forget_save_data(data);
    const auto files = field<uintptr_t>(data, 0x1c0);
    for (int32_t i = 0; i < field<int32_t>(data, 0x1c8); ++i) {
        const auto stream = field<uintptr_t>(files, static_cast<size_t>(i) * 8);
        CHECK(field<uint32_t>(stream, 0x180) & 0x40);
        std::free(reinterpret_cast<void*>(field<uintptr_t>(stream, 0x168)));
        std::free(reinterpret_cast<void*>(stream)); ++m.freed_streams;
    }
    std::free(reinterpret_cast<void*>(data)); ++m.freed_data;
}
void release(SaveReference* reference) {
    auto* control = reinterpret_cast<Control*>(reference->control); reference->control = 0;
    if (control && --control->strong == 0) { control->destroy(control->data); std::free(control); }
}
SaveReference* retain(SaveReference* out, const SaveReference* in) {
    *out = *in; if (in->control) ++reinterpret_cast<Control*>(in->control)->strong; return out;
}
void* construct_stream(void* object, const char* name, uint32_t flags) {
    auto& m = *active; ++m.streams;
    const auto stream = reinterpret_cast<uintptr_t>(object);
    field<uintptr_t>(stream, 0) = image + 0x2a575a8;
    auto& text = field<NativeString>(stream, 8); text.data = reinterpret_cast<char*>(stream + 0x38);
    text.length = static_cast<int32_t>(std::strlen(name)); std::memcpy(text.data, name, static_cast<size_t>(text.length) + 1);
    field<uint32_t>(stream, 0x180) = flags; return object;
}
int32_t size(uintptr_t remote, const char* name) {
    auto& m = *active; CHECK(remote == reinterpret_cast<uintptr_t>(&m.remote));
    CHECK(m.directory + "/game.details" == name); ++m.sizes;
    if (m.mode == 2) return INT32_MAX;
    if (m.mode == 3) return -1;
    if (m.mode == 4) return 0;
    if (m.mode == 12) throw std::runtime_error("synthetic size failure");
    return static_cast<int32_t>(m.bytes.size());
}
ReadWorkerResult* native_prepare(uintptr_t context, ReadWorkerResult* out, SaveReference* waiter) {
    auto& m = *active; ++m.prepared; waiter->control = 0;
    const auto remote = field<uintptr_t>(context, 0), table = field<uintptr_t>(remote, 0);
    const auto get_size = reinterpret_cast<int32_t (*)(uintptr_t, const char*)>(field<uintptr_t>(table, 0x78));
    const auto data = reinterpret_cast<Control*>(field<uintptr_t>(context, 8))->data;
    const auto files = field<uintptr_t>(data, 0x1c0);
    for (int32_t i = 0; i < field<int32_t>(data, 0x1c8); ++i) {
        const auto stream = field<uintptr_t>(files, static_cast<size_t>(i) * 8);
        const auto name = m.directory + "/" + field<NativeString>(stream, 8).data;
        const auto bytes = get_size(remote, name.c_str());
        if (bytes <= 0) { *out = {1, 0x100, 0}; return out; }
        CHECK(static_cast<size_t>(bytes) == m.bytes.size()); // Oversized native allocations never occur.
        field<uint64_t>(stream, 0x150) = static_cast<uint64_t>(bytes);
        field<uint64_t>(stream, 0x158) = static_cast<uint64_t>(bytes);
        field<uintptr_t>(stream, 0x168) = reinterpret_cast<uintptr_t>(std::calloc(1, static_cast<size_t>(bytes)));
    }
    *out = {0, 1, 0}; return out;
}
ReadWorkerResult* native_decode(uintptr_t, ReadWorkerResult* out) {
    ++active->decoded; *out = {0, active->mode == 9 ? 0u : 1u, 0};
    if (active->mode >= 19) {
        *out = {active->mode == 21 ? 1 : 0, 0x800091aau, 0xa5a5a5a5u};
        *reinterpret_cast<uint8_t*>(&out->value) = active->mode == 20 ? 0 : 1;
    }
    return out;
}
struct ReadFuture : SaveFuture { SaveReference data{}; unsigned stage = 0; };
SaveFuture* destroy_read(SaveFuture* base, uint32_t) {
    auto* future = static_cast<ReadFuture*>(base); release(&future->data); delete future; return base;
}
SaveResult* poll_read(SaveFuture* base, SaveResult* out, void* task) {
    auto& m = *active; auto& future = *static_cast<ReadFuture*>(base); ++m.read_polls;
    if (m.mode == 17) { *out = {0, 0, 1, 0}; return out; } // Native success without the verification worker.
    if (!future.stage++) {
        std::array<uintptr_t, 2> context{reinterpret_cast<uintptr_t>(&m.remote), future.data.control};
        SaveReference waiter{}; ReadWorkerResult result{};
        try {
            prepare_readback(m.owner, m.memory, reinterpret_cast<uintptr_t>(context.data()), &result, &waiter, native_prepare);
        } catch (const std::runtime_error&) {
            CHECK(context[0] == reinterpret_cast<uintptr_t>(&m.remote)); *out = {0, 1, 1, 0}; return out;
        }
        CHECK(context[0] == reinterpret_cast<uintptr_t>(&m.remote));
        if (result.outcome) { *out = {0, result.outcome, result.value, 0}; return out; }
        retain(&m.job, &future.data); static_cast<uint8_t*>(task)[8] = 1;
        *out = {-1, 0, 0, 0}; return out;
    }
    const auto data = reinterpret_cast<Control*>(future.data.control)->data;
    const auto stream = field<uintptr_t>(field<uintptr_t>(data, 0x1c0), 0);
    const auto buffer = field<uintptr_t>(stream, 0x168);
    std::memcpy(reinterpret_cast<void*>(buffer), m.bytes.data(), m.bytes.size());
    if (m.mode == 1) field<uint8_t>(buffer, 0) ^= 1;
    if (m.mode == 5) field<uint32_t>(stream, 0x184) = 4;
    if (m.mode == 6) field<NativeString>(stream, 8).data[0] = 'x';
    if (m.mode == 7) field<uint8_t>(data, 0x27f) = 1;
    if (m.mode == 8) field<uint64_t>(stream, 0x150) -= 1;
    if (m.mode == 10) field<uint32_t>(stream, 0x180) = 0x40;
    if (m.mode == 11) field<uintptr_t>(stream, 0x168) = 1;
    std::array<uintptr_t, 2> context{future.data.control, 0}; ReadWorkerResult result{};
    verify_readback(m.owner, m.memory, reinterpret_cast<uintptr_t>(context.data()), &result, native_decode, image);
    field<uintptr_t>(stream, 0x168) = buffer;
    release(&m.job); static_cast<uint8_t*>(task)[8] = 0;
    if (m.mode >= 19) CHECK(result.value == (m.mode == 20 ? 0x80009100u : 0x80009101u) && result.padding == 0xa5a5a5a5u);
    *out = {0, result.outcome, result.active_value(), 0}; return out;
}
const SaveFutureVtable read_table{destroy_read, poll_read};
SaveFuture** create_read(uintptr_t, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    auto& m = *active; CHECK(m.identity_alive && identity == 0x7788); ++m.reads;
    m.private_data = reinterpret_cast<Control*>(data->control)->data;
    CHECK(field<int32_t>(m.private_data, 0x1c8) == 0);
    if (m.mode == 15) { release(data); *out = nullptr; return out; }
    auto* future = new ReadFuture; future->vtable = &read_table; future->data = *data; data->control = 0;
    *out = future; return out;
}
ReadbackCalls calls() {
    return {{allocate, construct, destroy_data, {nullptr, retain, release, set_name, nullptr, image}, create_read}, construct_stream};
}
void proof(Model& m, bool callback = true) {
    auto& writes = m.owner.native_writes;
    CHECK(writes.attach_files(m.operation, 0x5566, 1));
    m.sequence = writes.begin(reinterpret_cast<uintptr_t>(&m.remote), 0x5566, 1, m.directory); CHECK(m.sequence);
    SdkFileWrite file{}; const auto name = m.directory + "/game.details";
    std::memcpy(file.name.data(), name.c_str(), name.size() + 1);
    file.buffer = reinterpret_cast<uintptr_t>(m.bytes.data()); file.size = static_cast<uint32_t>(m.bytes.size());
    CHECK(digest_payload(m.memory, file.buffer, file.size, file.sha256, GetTickCount64() + 1000));
    file.prepared = true; CHECK(writes.prepared(m.sequence, {file}));
    file.captured = true; const auto index = writes.capture(m.sequence, file);
    writes.submitted(m.sequence, index, 0x100000001);
    if (callback) writes.callback(0x100000001, false, 1);
    writes.result(m.sequence, {0, 0, 1, 0}); writes.released(0x5566);
}
struct Writer : SaveFuture { SaveReference source{}; };
SaveFuture* destroy_writer(SaveFuture* base, uint32_t) {
    auto* writer = static_cast<Writer*>(base); release(&writer->source); delete writer; return base;
}
SaveResult* poll_writer(SaveFuture*, SaveResult* out, void*) {
    auto& m = *active; CHECK(!m.writer_polls++); proof(m); *out = {0, 0, 1, 0}; return out;
}
const SaveFutureVtable writer_table{destroy_writer, poll_writer};
ReadWorkerResult* ordinary_prepare(uintptr_t context, ReadWorkerResult* out, SaveReference* waiter) {
    auto& m = *active; ++m.prepared;
    const auto remote = field<uintptr_t>(context, 0);
    if (!remote) *out = {1, 0x40, 0};
    else {
        const auto table = field<uintptr_t>(remote, 0);
        const auto get_size = reinterpret_cast<int32_t (*)(uintptr_t, const char*)>(field<uintptr_t>(table, 0x78));
        CHECK(get_size(remote, (m.directory + "/game.details").c_str()) > 0);
        *out = {0, 1, 0};
    }
    // Both native exits consume exactly one weak reference, not a strong one.
    auto* control = reinterpret_cast<Control*>(waiter->control);
    CHECK(control && control->weak == 2); --control->weak; waiter->control = 0;
    return out;
}
SaveFuture** create_writer(uintptr_t, SaveFuture** out, uintptr_t, SaveReference* source) {
    auto& m = *active; ++m.writes; auto* writer = new Writer;
    writer->vtable = &writer_table; writer->source = *source; source->control = 0;
    m.operation = m.owner.native_writes.snapshot().operation_id; *out = writer; return out;
}
struct GatedMemory : engine::Memory {
    engine::LocalMemory actual;
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE proceed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool first = true;
    ~GatedMemory() override { CloseHandle(entered); CloseHandle(proceed); }
    engine::ReadResult copy(uintptr_t address, void* out, size_t count) override {
        if (first) {
            first = false; SetEvent(entered);
            CHECK(WaitForSingleObject(proceed, 3000) == WAIT_OBJECT_0);
        }
        return actual.copy(address, out, count);
    }
};
void backup_worker_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    for (unsigned mode = 0; mode != 6; ++mode) {
        auto owner = make(); engine::LocalMemory memory;
        const std::string private_bytes(131072, 'b');
        SdkWriteObservation manifest; manifest.operation = 77;
        manifest.directory = owner->native_root() + "/DLC1-AUTOSAVE4";
        SdkFileWrite file; const auto name = manifest.directory + "/game.details";
        std::memcpy(file.name.data(), name.c_str(), name.size() + 1);
        file.size = static_cast<uint32_t>(private_bytes.size());
        file.buffer = 1; // Old writer memory is deliberately unusable.
        CHECK(digest_payload(memory, reinterpret_cast<uintptr_t>(private_bytes.data()), file.size,
            file.sha256, GetTickCount64() + 1000));
        manifest.payloads.push_back(file);
        const std::vector<uintptr_t> buffers{reinterpret_cast<uintptr_t>(private_bytes.data())};
        BackupJob job(GetCurrentProcessId(), 654321, mode == 4 ? 0 : GetTickCount64() + 10000);
        CHECK(job.bind(77) && !job.bind(78));
        if (mode == 5) job.cancel();
        if (mode == 1 || mode == 2) {
            GatedMemory gated; CHECK(gated.entered && gated.proceed);
            std::thread worker([&] { job.copy(*owner, gated, manifest, buffers); });
            CHECK(WaitForSingleObject(gated.entered, 3000) == WAIT_OBJECT_0);
            if (mode == 1) job.cancel(); else job.readback_finished(false);
            CHECK(job.progress().state == BackupState::copying); // No premature completion/cleanup.
            SetEvent(gated.proceed); worker.join();
        } else job.copy(*owner, memory, manifest, buffers);
        auto progress = job.progress();
        if (mode == 0 || mode == 3) CHECK(progress.state == BackupState::copied && progress.storage_complete);
        else CHECK(progress.state == BackupState::failed && !progress.storage_complete);
        job.readback_finished(mode != 3);
        progress = job.progress();
        if (mode == 0) {
            CHECK(progress.state == BackupState::complete);
            job.cancel(); CHECK(job.progress().state == BackupState::complete && job.progress().cancel_requested);
        } else {
            CHECK(progress.state == BackupState::failed);
            CHECK(progress.failure == (mode == 1 || mode == 5 ? BackupFailure::cancelled :
                mode == 4 ? BackupFailure::deadline : BackupFailure::native));
        }
        if (mode == 1 || mode == 2) {
            CHECK(progress.storage_attempted && !progress.output.path.empty());
            CHECK(GetFileAttributesW((progress.output.path + L"\\transport.manifest").c_str()) == INVALID_FILE_ATTRIBUTES);
        }
        if (mode == 3) CHECK(progress.storage_complete && !progress.readback_success && !progress.output.path.empty());
        if (mode >= 4) CHECK(!progress.storage_attempted && progress.output.path.empty());
    }
}
}
static void ordinary_worker_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    for (unsigned mode = 0; mode < 13; ++mode) {
        const bool disabled = mode >= 11;
        auto owner = disabled ? std::make_unique<Session>() : make(); Model m(*owner); active = &m;
        m.table[15] = reinterpret_cast<uintptr_t>(size);
        if (!disabled) CHECK(owner->bind_provider(owner->native_root(), reinterpret_cast<uintptr_t>(&m.remote), owner->ownership_record()));
        if (mode == 1) m.directory = owner->native_root() + "/DLC1-AUTOSAVE11";
        if (mode == 2) m.directory = "PROFILE";
        if (mode == 4 || disabled) m.directory = "GAME-AUTOSAVE0";
        if (mode == 5) m.directory = "foreign/GAME-AUTOSAVE0";
        if (mode == 8) m.directory = owner->native_root() + "/DLC2-AUTOSAVE12";
        auto* control = static_cast<Control*>(allocate(sizeof(Control)));
        auto* object = construct(allocate(0x280)); *control = {1, 1, reinterpret_cast<uintptr_t>(object), destroy_data};
        set_name(control->data, m.directory.c_str()); SaveReference source{reinterpret_cast<uintptr_t>(control)};
        SaveFuture* future = nullptr;
        create_read(0x777, &future, 0x7788, &source);
        CHECK(future && !source.control && control->strong == 1 && m.reads == 1);
        CHECK(!owner->native_writes.readback_operation(control->data));
        m.identity_alive = false; // Factory already captured its borrowed argument.
        std::array<uintptr_t, 2> context{reinterpret_cast<uintptr_t>(&m.remote), reinterpret_cast<uintptr_t>(control)};
        uintptr_t replacement = reinterpret_cast<uintptr_t>(m.table.data());
        if (mode == 3) context[0] = reinterpret_cast<uintptr_t>(&replacement); // Replaced after the factory.
        if (mode == 6) owner->fail(SessionFault::native_write);
        if (mode == 7 || mode == 12) context[0] = 0;
        if (mode == 9) context[1] = 0;
        if (mode == 10) field<NativeString>(control->data, 0).length = 64;
        const auto remote = context[0];
        Control weak{3, 2, 0, nullptr}; SaveReference waiter{reinterpret_cast<uintptr_t>(&weak)};
        ReadWorkerResult result{};
        prepare_readback(*owner, m.memory, reinterpret_cast<uintptr_t>(context.data()), &result, &waiter, ordinary_prepare);
        const bool accepted = mode <= 2 || mode == 11;
        CHECK(m.prepared == 1 && m.sizes == (accepted ? 1u : 0u) && !m.read_polls && !m.decoded && !m.streams);
        CHECK(context[0] == remote && !waiter.control && weak.weak == 1 && weak.strong == 3 && control->strong == 1);
        CHECK(result.outcome == (accepted ? 0 : 1) && result.value == (mode == 12 ? 0x40u : 1u));
        if (disabled) CHECK(owner->state() == SessionState::disabled && owner->fault() == SessionFault::none);
        else if (!accepted) CHECK(owner->state() == SessionState::faulted &&
            owner->fault() == (mode == 6 ? SessionFault::native_write : SessionFault::native_read));
        if (accepted) {
            std::array<uintptr_t, 2> decode_context{reinterpret_cast<uintptr_t>(control), 0};
            verify_readback(*owner, m.memory, reinterpret_cast<uintptr_t>(decode_context.data()), &result, native_decode, image);
            CHECK(!result.outcome && result.value == 1 && m.decoded == 1);
        }
        future->vtable->destroy(future, 1); CHECK(m.freed_data == 1);
    }
}
void run_readback_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    for (unsigned mode = 0; mode <= 21; ++mode) {
        const bool verified = mode == 0 || mode == 18 || mode == 19;
        auto owner = make(); Model m(*owner); active = &m; m.mode = mode;
        m.table[15] = reinterpret_cast<uintptr_t>(size);
        m.operation = owner->native_writes.open_provider(0x123, m.directory); CHECK(m.operation);
        auto backup = std::make_shared<BackupJob>(GetCurrentProcessId(), 123456, GetTickCount64() + 10000);
        CHECK(owner->native_writes.request_backup(m.operation, backup));
        CHECK(!owner->native_writes.request_backup(m.operation, backup));
        if (mode == 16) m.fail_allocation = 3; // First private stream allocation, after native write.
        SaveFuture* future = create_write_readback(*owner, m.operation, 0x777, 0x7788, m.directory, calls());
        m.identity_alive = false;
        if (mode == 15) {
            CHECK(!future && m.freed_data == 1 && backup->progress().state == BackupState::failed);
            continue;
        }
        CHECK(future && m.read_polls == 0);
        proof(m, mode != 13);
        owner->native_writes.provider_result(m.operation, {0, 0, 1, 0});
        if (mode == 18) {
            CHECK(owner->bind_provider(owner->native_root(), reinterpret_cast<uintptr_t>(&m.remote), owner->ownership_record()));
            owner->fail(SessionFault::native_write); CHECK(owner->state() == SessionState::faulted);
        }
        std::array<uint8_t, 16> task{}; SaveResult result{};
        future->vtable->poll(future, &result, task.data());
        if (mode == 14) {
            CHECK(result.state == -1 && m.job.control && task[8]);
            const auto data = m.private_data;
            future->vtable->destroy(future, 1); owner->native_writes.close_provider(m.operation);
            CHECK(backup->progress().state == BackupState::failed && !backup->progress().storage_attempted);
            CHECK(!m.freed_data && owner->native_writes.readback_operation(data) == m.operation);
            for (unsigned i = 0; i < 80; ++i) {
                const auto id = owner->native_writes.open_provider(i + 0x900, "another"); CHECK(id);
                owner->native_writes.close_provider(id);
            }
            CHECK(owner->native_writes.snapshot(m.operation).state == SC_SAVE_WRITE_READBACK_FAILED);
            std::array<uintptr_t, 2> context{m.job.control, 0}; ReadWorkerResult late{};
            verify_readback(*owner, m.memory, reinterpret_cast<uintptr_t>(context.data()), &late, native_decode, image);
            CHECK(late.outcome == 1 && !m.decoded); release(&m.job);
            CHECK(m.freed_data == 1 && !owner->native_writes.readback_operation(data)); continue;
        }
        if (result.state == -1) { CHECK(task[8]); future->vtable->poll(future, &result, task.data()); }
        CHECK(result.state == 0 && (verified ? result.outcome == 0 && result.value == 1 : result.outcome == 1));
        CHECK(m.decoded == (verified || mode == 9 || mode >= 20 ? 1u : 0u));
        constexpr const char* failures[]{nullptr, "readback_sha256_mismatch", "readback_storage_file_size",
            "readback_storage_file_size", "readback_storage_file_size", "readback_stream_error",
            "readback_manifest_name_mismatch", "readback_native_cancelled", "readback_loaded_size_mismatch",
            "readback_native_decode_result", "readback_stream_flags_mismatch", "payload_hash_bytes_unreadable",
            "readback_native_prepare_exception", "readback_file_sdk_proof_missing", nullptr, nullptr,
            "readback_stream_allocation_failed", "readback_terminal_hash_proof_missing", nullptr, nullptr,
            "readback_native_decode_result", "readback_native_decode_result"};
        const auto diagnostic = owner->btrace.snapshot().first_failure;
        if (mode == 0 || mode == 19) {
            if (diagnostic.sequence) {
                std::fprintf(stderr, "readback success first failure stage=%s predicate=%s operation=%llu\n",
                    b_stage_names[static_cast<size_t>(diagnostic.stage)], diagnostic.predicate,
                    static_cast<unsigned long long>(diagnostic.operation));
                for (const auto& fact : diagnostic.facts) if (fact.key)
                    std::fprintf(stderr, " %s=%lld\n", fact.key, static_cast<long long>(fact.value));
            }
            CHECK(!diagnostic.sequence); // Pending, successful native calls and readback are not faults.
        }
        else if (mode < std::size(failures) && failures[mode]) {
            CHECK(diagnostic.sequence && diagnostic.operation == m.operation);
            CHECK(!std::strcmp(diagnostic.predicate, failures[mode]));
            owner->btrace.record(BStage::provider, BStatus::refused, "fixture_later_provider_failure", m.operation);
            CHECK(owner->btrace.snapshot().first_failure.sequence == diagnostic.sequence);
        }
        const auto snapshot = owner->native_writes.snapshot(m.operation);
        CHECK(snapshot.state == static_cast<uint32_t>(verified ? SC_SAVE_WRITE_READBACK_CONFIRMED :
            mode == 13 ? SC_SAVE_WRITE_NATIVE_SUCCEEDED : SC_SAVE_WRITE_READBACK_FAILED));
        future->vtable->destroy(future, 1);
        CHECK(m.freed_data == 1 && m.freed_streams == m.streams && !m.job.control);
        const auto saved = backup->progress();
        CHECK(saved.operation == m.operation && saved.readback_terminal);
        if (verified) {
            CHECK(saved.state == BackupState::complete && saved.readback_success && saved.storage_complete);
            CHECK(saved.output.files == 1 && saved.output.bytes == m.bytes.size());
            // The output remains usable after every native stream has died.
            const auto file = CreateFileW((saved.output.path + L"\\payload-0.bin").c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            CHECK(file != INVALID_HANDLE_VALUE);
            std::array<char, 64> bytes{}; DWORD count = 0;
            CHECK(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr));
            CloseHandle(file); CHECK(std::string(bytes.data(), count) == m.bytes);
        } else CHECK(saved.state == BackupState::failed && !saved.storage_attempted && saved.output.path.empty());
    }
    for (unsigned allocation = 1; allocation <= 2; ++allocation) {
        auto owner = make(); Model m(*owner); active = &m; m.fail_allocation = allocation;
        m.operation = owner->native_writes.open_provider(0x123, m.directory); CHECK(m.operation);
        CHECK(!create_write_readback(*owner, m.operation, 0x777, 0x7788, m.directory, calls()));
        CHECK(!m.reads && !m.streams && !m.freed_data);
        const auto first = owner->btrace.snapshot().first_failure;
        CHECK(first.operation == m.operation && !std::strcmp(first.predicate, allocation == 1 ?
            "readback_control_allocation_failed" : "readback_data_allocation_failed"));
    }
    ordinary_worker_contracts(make);
    for (const auto count : {1u, 17u}) {
        auto owner = make(); auto& writes = owner->native_writes;
        const auto operation = writes.open_provider(0x123, "PROFILE"); CHECK(operation);
        CHECK(writes.attach_readback(operation, 0x456, 16)); CHECK(writes.attach_files(operation, 0x789, count));
        const auto sequence = writes.begin(0xabc, 0x789, count, "PROFILE"); CHECK(sequence);
        std::vector<SdkFileWrite> files(count);
        for (auto& file : files) { file.prepared = true; file.size = count == 1 ? 0u : 1u; }
        CHECK(!writes.prepared(sequence, files)); // Refused before the preflight invokes quota/deletion.
    }
    for (const bool armed : {false, true}) {
        auto owner = make(); Model m(*owner); active = &m; m.table[15] = reinterpret_cast<uintptr_t>(size);
        CHECK(owner->bind_provider(owner->native_root(), reinterpret_cast<uintptr_t>(&m.remote), owner->ownership_record()));
        auto* control = static_cast<Control*>(allocate(sizeof(Control)));
        auto* object = construct(allocate(0x280)); *control = {1, 1, reinterpret_cast<uintptr_t>(object), destroy_data};
        set_name(control->data, "GAME-AUTOSAVE0"); SaveReference source{reinterpret_cast<uintptr_t>(control)};
        const auto readback = calls(); const WriteCalls write{create_writer, set_name, release, nullptr, image, nullptr, &readback};
        SaveFuture* future = nullptr;
        auto backup = std::make_shared<BackupJob>(GetCurrentProcessId(), 123456, GetTickCount64() + 10000);
        native::Diagnostics queue; sc_save_backup_request request{};
        if (armed) {
            auto& e = request.execution;
            e.expected.pid = GetCurrentProcessId(); e.expected.process_created = 123456; e.expected.instance_id[0] = 1;
            e.expected.lifecycle_generation = 1; e.request_id = 88; e.nonce[0] = 42; e.deadline_ms = 1000;
            strcpy_s(request.namespace_id, owner->namespace_id().c_str()); request.work_deadline_ms = 10000;
            const auto now = GetTickCount64();
            CHECK(queue.submit(e, 0, now, nullptr, &request).state == SC_DIAGNOSTIC_QUEUED);
            auto* slot = queue.claim(now + 1); CHECK(slot); backup = slot->backup;
            WriterSubmission submission{&write, &source, &future}; submitted_writer = &submission;
            const auto invoked = submit_native_save(owner->native_writes, backup, m.directory,
                {submitted_root, submitted_release, 0x123, 0x674744});
            CHECK(invoked.entered && invoked.matched && !invoked.exception && submission.releases == 1);
            CHECK(invoked.operation == backup->progress().operation && !backup->progress().storage_attempted);
            submitted_writer = nullptr;
            auto facts = slot->result;
            facts.observed_at_ms = facts.executed_at_ms = now + 1; facts.thread_id = GetCurrentThreadId();
            facts.phase = facts.site_revision = 1; facts.lifecycle = SC_LIFETIME_ACTIVE; facts.game_state = SC_GAME_IN_GAME;
            facts.current_map.validity = SC_OBSERVATION_OBSERVED; strcpy_s(facts.current_map.bytes, "fixture/map");
            facts.current_map.length = 11;
            native::Diagnostics::await_backup(*slot, facts, {}, invoked);
            CHECK(queue.backup_result(request, false, now + 1).state == SC_BACKUP_WAITING_NATIVE);
        } else write_scoped(*owner, m.memory, reinterpret_cast<uintptr_t>(&m.remote), &future, 0x7788, &source, write);
        CHECK(future && !source.control && m.writes == 1 && m.reads == 1 && m.read_polls == 0);
        m.identity_alive = false; std::array<uint8_t, 16> task{}; SaveResult result{};
        future->vtable->poll(future, &result, task.data());
        CHECK(result.state == -1 && m.writer_polls == 1 && owner->native_writes.snapshot().state == SC_SAVE_WRITE_READBACK_PENDING);
        future->vtable->poll(future, &result, task.data());
        CHECK(!result.state && !result.outcome && result.value == 1 && m.writer_polls == 1);
        future->vtable->destroy(future, 1); CHECK(m.freed_data == 2 && m.freed_streams == 1);
        CHECK(backup->progress().state == (armed ? BackupState::complete : BackupState::pending));
        if (armed) {
            const auto record = queue.backup_result(request, false, GetTickCount64() + 3);
            CHECK(record.state == SC_BACKUP_COMPLETE && record.operation_id == owner->native_writes.snapshot().operation_id);
            CHECK(record.files == 1 && record.bytes == m.bytes.size() && record.basename[0]);
            Snapshot host{}; host.pid = request.execution.expected.pid; host.process_created = 123456; host.instance[0] = 1;
            host.core.abi_version = SC_ABI_VERSION; strcpy_s(host.core.version, "0.6.0"); std::memset(host.core.build_id, 'a', 64);
            Message wire{}; WireResult code{}; Snapshot decoded_host{}; sc_save_backup_snapshot decoded{};
            const auto length = encode_backup_response(wire, WireResult::ok, save_backup_result_operation, host, record);
            CHECK(length && decode_backup_response(wire, length, code, save_backup_result_operation, decoded_host, decoded));
            CHECK(decoded.state == SC_BACKUP_COMPLETE && decoded.operation_id == record.operation_id &&
                !std::strcmp(decoded.basename, record.basename));
            auto missing = record; missing.flags &= ~SC_BACKUP_SOURCE_MATCHED;
            CHECK(!encode_backup_response(wire, WireResult::ok, save_backup_result_operation, host, missing));
            missing = record; missing.flags &= ~SC_BACKUP_READ_SUCCESS;
            CHECK(!encode_backup_response(wire, WireResult::ok, save_backup_result_operation, host, missing));
            missing = record; missing.flags &= ~SC_BACKUP_STORAGE_COMPLETE;
            CHECK(!encode_backup_response(wire, WireResult::ok, save_backup_result_operation, host, missing));
        }
    }
    active = nullptr; backup_worker_contracts(make); run_submission_contracts();
    std::puts("PASS native readback, owned transport backup, cancellation/retention and writer completion contracts");
}
