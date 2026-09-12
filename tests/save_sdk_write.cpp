// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Native layouts and SDK calls are synthetic. No game or Steam process is used.
#include "save_write.h"
#include "save_collector.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>

#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "FAIL sdk_write:%d: %s\n", __LINE__, #v); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::save;
namespace {
constexpr uintptr_t image = 0x10000000;
struct Remote { uintptr_t* table; unsigned writes = 0; bool zero = false; uint64_t next = 0x100000001; };
struct Utilities { uintptr_t* table; unsigned queries = 0; bool ready = false, failed = false; };
struct Context {
    uint64_t index = 0; uintptr_t callback = 0, remote = 0; NativeString name{};
    uintptr_t files = 0; uint64_t count = 2;
};
struct Preparation { uintptr_t remote; NativeString name; uint64_t clear = 1, files, count = 2, capacity = 2; };
static_assert(offsetof(Context, remote) == 0x10 && offsetof(Context, name) == 0x18 &&
    offsetof(Context, files) == 0x48 && offsetof(Preparation, files) == 0x40);
struct Memory : engine::Memory {
    engine::LocalMemory local; uintptr_t deny = 0; bool wrong_initializer = false;
    engine::ReadResult copy(uintptr_t p, void* out, size_t size) override {
        if (p == image + 0x3891468 && size == sizeof(uintptr_t)) {
            const auto value = wrong_initializer ? uintptr_t{0} : image + 0x6765c0;
            std::memcpy(out, &value, size); return {};
        }
        if (p == deny) return {SC_REASON_READ_FAILED, 0};
        return local.copy(p, out, size);
    }
};
struct Model {
    Session& owner; Memory memory;
    std::array<uintptr_t, 3> remote_table{}; Remote remote{remote_table.data()};
    std::array<uintptr_t, 12> utility_table{}; Utilities utilities{utility_table.data()};
    uintptr_t utilities_pointer = reinterpret_cast<uintptr_t>(&utilities);
    std::array<unsigned char, 0x300> storage{};
    std::array<std::array<unsigned char, 0x48>, 2> callbacks{};
    std::array<uint64_t, 2> handles{};
    std::string directory, first = "abc", second = "";
    std::array<char, 4> alternative{'a', 'b', 'c', '\0'};
    Context context;
    unsigned polls = 0, callback_calls = 0, destructions = 0, contexts = 0, mode = 0;
    bool thrown = false;
    explicit Model(Session& s) : owner(s), directory(s.native_root() + "/GAME-AUTOSAVE0") {
        context.remote = reinterpret_cast<uintptr_t>(&remote);
        context.name.data = directory.data(); context.name.length = static_cast<int32_t>(directory.size());
        context.files = reinterpret_cast<uintptr_t>(storage.data());
        prepared_files();
    }
    void prepared_files() {
        for (unsigned i = 0; i < 2; ++i) {
            auto* entry = storage.data() + i * 0x180;
            const auto table = image + 0x2a575a8;
            NativeString name{}; name.data = const_cast<char*>(i ? "SlotFile" : "game.details"); name.length = i ? 8 : 12;
            const auto& bytes = i ? second : first;
            const uint64_t size = bytes.size(), capacity = size + 1;
            const auto buffer = reinterpret_cast<uintptr_t>(bytes.data());
            std::memcpy(entry, &table, 8); std::memcpy(entry + 8, &name, sizeof(name));
            std::memcpy(entry + 0x150, &size, 8); std::memcpy(entry + 0x158, &capacity, 8);
            std::memcpy(entry + 0x168, &buffer, 8); entry[0x178] = 1;
        }
    }
};
Model* active = nullptr;
uint64_t write_async(uintptr_t remote, const char* name, const void* bytes, uint32_t size) {
    auto& model = *active; CHECK(remote == reinterpret_cast<uintptr_t>(&model.remote));
    const auto index = model.remote.writes++;
    CHECK(index < 2);
    CHECK(std::string(name) == model.directory + (index ? "/SlotFile" : model.mode == 18 ? "/GAME.details" : "/game.details"));
    const auto& expected = index ? model.second : model.first;
    CHECK(size == expected.size() && (size == 0 || std::memcmp(bytes, expected.data(), size) == 0));
    return model.remote.zero ? 0 : model.remote.next++;
}
bool completed(uintptr_t utilities, uint64_t handle, bool* failed) {
    auto& model = *active; CHECK(utilities == model.utilities_pointer && handle != 0);
    ++model.utilities.queries; *failed = model.utilities.failed; return model.utilities.ready;
}
uintptr_t context_init(uintptr_t descriptor) {
    CHECK(descriptor == image + 0x3891468); ++active->contexts;
    return reinterpret_cast<uintptr_t>(&active->utilities_pointer);
}
void native_callback(uintptr_t, const int32_t*, bool) { ++active->callback_calls; }
void callback(Model& model, unsigned index, int32_t code = 1, bool failed = false, uint32_t type = 0x533) {
    auto& object = model.callbacks[index];
    std::memcpy(object.data() + 0xc, &type, sizeof(type));
    std::memcpy(object.data() + 0x38, &model.handles[index], sizeof(uint64_t));
    // No waiter/control reference exists: production must observe before the
    // original callback's early return, without manufacturing a native waker.
    sdk_write_callback(model.owner, model.memory, reinterpret_cast<uintptr_t>(object.data()),
        failed ? nullptr : &code, failed, native_callback);
}
void issue(Model& model, unsigned index) {
    using Async = uint64_t (*)(uintptr_t, const char*, const void*, uint32_t);
    const auto table = *reinterpret_cast<uintptr_t**>(model.context.remote);
    const auto operation = reinterpret_cast<Async>(table[2]);
    const auto name = model.directory + (index ? "/SlotFile" : model.mode == 18 ? "/GAME.details" : "/game.details");
    const auto& bytes = index ? model.second : model.first;
    const void* input = model.mode == 16 && !index ? model.alternative.data() : bytes.data();
    model.handles[index] = operation(model.context.remote, name.c_str(), input, static_cast<uint32_t>(bytes.size()));
}
SdkWriteResult* native_poll(uintptr_t raw, SdkWriteResult* out, void*) {
    auto& model = *active; CHECK(raw == reinterpret_cast<uintptr_t>(&model.context));
    if (model.polls++ == 0) {
        issue(model, 0);
        if (model.mode == 10) { model.thrown = true; throw std::runtime_error("native poll fixture"); }
        *out = {-1, 0, 0, 0}; return out;
    }
    if (model.mode == 2) { *out = {1, 0, 0, 0}; return out; }
    if (model.mode == 3 || model.mode == 4) {
        callback(model, 0, model.mode == 4 ? 2 : 1, model.mode == 3);
        *out = {0, 1, 0, model.mode == 3 ? 0x10u : 0x40u}; return out;
    }
    callback(model, 0); issue(model, 1); callback(model, 1);
    // Match native bool success: inactive variant bytes need not be initialized.
    out->state = 0; out->outcome = 0; *reinterpret_cast<uint8_t*>(&out->detail) = 1; return out;
}
WritePreflightResult* prepare(uintptr_t raw, WritePreflightResult* out) {
    auto& value = *reinterpret_cast<Preparation*>(raw);
    *out = {0, value.files, value.count, value.capacity}; value.files = value.count = value.capacity = 0;
    return out;
}
void destroy(uintptr_t vector) {
    ++active->destructions;
    std::memset(reinterpret_cast<void*>(vector), 0, sizeof(uint64_t) * 3);
}
void release_vector(Model& model, uintptr_t files) {
    std::array<uint64_t, 3> vector{files, 2, 2};
    destroy_sdk_vector(model.owner, model.memory, reinterpret_cast<uintptr_t>(vector.data()), destroy);
    CHECK(vector == (std::array<uint64_t, 3>{}));
}
SdkWriteObservation inspect(Model& model, uint64_t sequence = 1) {
    SdkWriteObservation out; CHECK(model.owner.native_writes.inspect(sequence, out)); return out;
}
uint64_t source(Model& model, uint64_t count = 2) {
    const auto id = model.owner.native_writes.open_provider(reinterpret_cast<uintptr_t>(&model), model.directory);
    CHECK(id && model.owner.native_writes.attach_files(id, model.context.files, count)); return id;
}
void hash_is(const std::array<unsigned char, 32>& hash, const char* expected) {
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < hash.size(); ++i) CHECK(expected[2*i] == digits[hash[i] >> 4] && expected[2*i+1] == digits[hash[i] & 15]);
}
struct Control { uint32_t strong = 1, weak = 1; uintptr_t object = 0, destructor = 0; };
struct JobModel {
    Model& sdk;
    std::array<unsigned char, 0x280> data{};
    std::array<unsigned char, 0x190> file{};
    std::array<unsigned char, 0x58> preparation{};
    std::array<unsigned char, 0xa0> preflight{};
    std::array<unsigned char, 0xb8> context_future{};
    uintptr_t file_pointer;
    Control source{2, 2}, prepared{}, checked{};
    unsigned mode = 0, step = 0;
    uint64_t operation = 0;
    bool preparation_worker = false, preflight_worker = false;
    bool preparation_parent = false, preflight_parent = false;
    explicit JobModel(Model& model) : sdk(model), file_pointer(reinterpret_cast<uintptr_t>(file.data())) {
        source.object = reinterpret_cast<uintptr_t>(data.data());
        prepared.object = reinterpret_cast<uintptr_t>(preparation.data()); prepared.destructor = image + 0x1bde610;
        checked.object = reinterpret_cast<uintptr_t>(preflight.data()); checked.destructor = image + 0x1bd6e80;
        std::memcpy(data.data(), &sdk.context.name, sizeof(NativeString));
        const auto input_files = reinterpret_cast<uintptr_t>(&file_pointer);
        std::memcpy(data.data() + 0x1c0, &input_files, 8);
        const int32_t one = 1;
        std::memcpy(data.data() + 0x1c8, &one, 4); std::memcpy(data.data() + 0x1cc, &one, 4);
        const auto table = image + 0x2a575a8; std::memcpy(file.data(), &table, 8);
        NativeString name{}; name.data = const_cast<char*>("game.details"); name.length = 12;
        std::memcpy(file.data() + 8, &name, sizeof(name));
    }
};
JobModel* jobs = nullptr;
void drop_source(SaveReference* reference) {
    auto* control = reinterpret_cast<Control*>(reference->control);
    CHECK(control && control->strong && control->weak);
    --control->strong; --control->weak; reference->control = 0;
}
void destroy_preparation(uintptr_t object) {
    CHECK(object == jobs->prepared.object);
    SaveReference source{}; std::memcpy(&source, jobs->preparation.data(), 8);
    drop_source(&source); jobs->preparation.fill(0);
}
void destroy_preflight(uintptr_t object) {
    CHECK(object == jobs->checked.object);
    destroy_sdk_vector(jobs->sdk.owner, jobs->sdk.memory, object + 0x40, destroy);
    destroy_sdk_vector(jobs->sdk.owner, jobs->sdk.memory, object + 0x68, destroy);
}
void drop_job(bool preflight) {
    auto& control = preflight ? jobs->checked : jobs->prepared;
    CHECK(control.strong && control.weak); --control.weak;
    if (--control.strong == 0) destroy_write_job(jobs->sdk.owner, control.object,
        preflight ? destroy_preflight : destroy_preparation);
}
uintptr_t* native_prepare_job(const SaveReference* source, uintptr_t* out, const SaveReference*) {
    out[0] = 0; out[1] = 0;
    if (jobs->mode == 4) return out;
    CHECK(source->control == reinterpret_cast<uintptr_t>(&jobs->source));
    ++jobs->source.strong; ++jobs->source.weak;
    std::memcpy(jobs->preparation.data(), source, 8);
    if (jobs->mode == 6) jobs->prepared.destructor = 0;
    out[1] = reinterpret_cast<uintptr_t>(&jobs->prepared); return out;
}
SaveFuture** native_write_context(SaveFuture** out, SaveReference* identity, const char* name, uint8_t clear, uint64_t* files) {
    CHECK(identity->control == 0 && name == jobs->sdk.context.name.data && clear == 1);
    *out = nullptr;
    if (jobs->mode == 5) return out;
    Preparation context{jobs->sdk.context.remote, jobs->sdk.context.name, clear, files[0], files[1], files[2]};
    std::memcpy(jobs->preflight.data(), &context, sizeof(context));
    files[0] = files[1] = files[2] = 0;
    const uintptr_t fields[]{jobs->mode == 7 ? 0 : image + 0x2e90628, 0, reinterpret_cast<uintptr_t>(&jobs->checked)};
    std::memcpy(jobs->context_future.data(), fields, sizeof(fields));
    *out = reinterpret_cast<SaveFuture*>(jobs->context_future.data()); return out;
}
struct Pipeline : SaveFuture { SaveReference source; explicit Pipeline(SaveReference value) : source(value) {} };
SaveFuture* destroy_pipeline(SaveFuture* raw, uint32_t) {
    auto* value = static_cast<Pipeline*>(raw);
    if (jobs->preparation_parent) { jobs->preparation_parent = false; drop_job(false); }
    if (jobs->preflight_parent) { jobs->preflight_parent = false; drop_job(true); }
    drop_source(&value->source); delete value; return raw;
}
SaveResult* poll_pipeline(SaveFuture* raw, SaveResult* out, void*) {
    auto& model = *jobs; auto& owner = model.sdk.owner;
    const auto id = WritePollScope::current(owner.native_writes); CHECK(id != 0);
    if (!model.operation) model.operation = id; else CHECK(model.operation == id);
    if (model.step++ == 0) {
        std::array<uintptr_t, 8> prepared{}; SaveReference identity{};
        prepare_write_job(owner, model.sdk.memory, &static_cast<Pipeline*>(raw)->source, prepared.data(), &identity, native_prepare_job, image);
        if (!prepared[1]) { *out = {0, 1, 0x40, 0}; return out; }
        model.preparation_parent = model.preparation_worker = true;
        ++model.prepared.strong; ++model.prepared.weak; // The scheduled worker owns an independent reference.
        *out = {-1, 0, 0, 0}; return out;
    }
    if (model.step == 2) {
        model.preparation_worker = false; drop_job(false);
        std::array<uint64_t, 3> files{model.sdk.context.files, 2, 2}; SaveReference identity{}; SaveFuture* next = nullptr;
        create_write_context(owner, model.sdk.memory, &next, &identity, model.sdk.context.name.data, 1, files.data(), native_write_context, image);
        model.preparation_parent = false; drop_job(false);
        if (!next) { *out = {0, 1, 0x10, 0}; return out; }
        CHECK(files == (std::array<uint64_t, 3>{}));
        model.preflight_parent = model.preflight_worker = true;
        ++model.checked.strong; ++model.checked.weak;
        *out = {-1, 0, 0, 0}; return out;
    }
    if (model.preflight_parent) {
        WritePreflightResult result{}; std::memcpy(&result, model.preflight.data() + 0x60, sizeof(result));
        if (result.tag) { *out = {0, 1, 1, 0}; return out; }
        CHECK(result.first == model.sdk.context.files && result.second == 2);
        std::memset(model.preflight.data() + 0x68, 0, 24); // Move to SDK state before context release.
        model.preflight_parent = false; drop_job(true);
    }
    SdkWriteResult sdk{};
    poll_sdk_write(owner, model.sdk.memory, reinterpret_cast<uintptr_t>(&model.sdk.context), &sdk, nullptr, {native_poll, image, context_init});
    if (sdk.state == -1) { *out = {-1, 0, 0, 0}; return out; }
    release_vector(model.sdk, model.sdk.context.files); // Native teardown precedes outward result mapping.
    NativeWriteOperation observed; CHECK(owner.native_writes.inspect_operation(id, observed) && !observed.provider_terminal);
    *out = {0, 0, 1, 0}; return out;
}
const SaveFutureVtable pipeline_table{destroy_pipeline, poll_pipeline};
SaveFuture** create_pipeline(uintptr_t, SaveFuture** out, uintptr_t, SaveReference* source) {
    auto* value = new Pipeline(*source); value->vtable = &pipeline_table; source->control = 0; *out = value; return out;
}
void run_write_owner_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    for (unsigned test = 0; test < 8; ++test) {
        auto owner = make(); Model sdk(*owner); active = &sdk; JobModel model(sdk); jobs = &model; model.mode = test;
        sdk.remote_table[2] = reinterpret_cast<uintptr_t>(write_async); sdk.utility_table[11] = reinterpret_cast<uintptr_t>(completed);
        CHECK(owner->bind_provider(owner->native_root(), sdk.context.remote, owner->ownership_record())); owner->startup_leave(false);
        SaveReference source{reinterpret_cast<uintptr_t>(&model.source)}; SaveFuture* future = nullptr;
        WriteCalls calls{create_pipeline, nullptr, drop_source, nullptr, image};
        write_scoped(*owner, sdk.memory, 0, &future, 0, &source, calls); CHECK(future && !source.control);
        SaveResult result{}; future->vtable->poll(future, &result, nullptr);
        NativeWriteOperation observed; CHECK(owner->native_writes.inspect_operation(1, observed));
        CHECK(observed.data == model.source.object && observed.directory == sdk.directory && !observed.sdk_sequence);
        if (test == 4) {
            CHECK(result.outcome == 1 && result.value == 1 && observed.provider_terminal && observed.provider_value == 0x40);
        } else {
            CHECK(result.state == -1 && !observed.provider_terminal && observed.provider_alive);
            CHECK(observed.preparation_jobs == (test == 6 ? 0u : 1u));
            if (test == 1 || test == 6) {
                future->vtable->destroy(future, 1); future = nullptr;
                CHECK(owner->native_writes.inspect_operation(1, observed) && !observed.provider_alive && !observed.provider_terminal);
                CHECK(observed.preparation_jobs == (test == 6 ? 0u : 1u));
                model.preparation_worker = false; drop_job(false);
            } else {
                future->vtable->poll(future, &result, nullptr);
                CHECK(owner->native_writes.inspect_operation(1, observed) && !observed.preparation_jobs);
                if (test == 5) CHECK(result.outcome == 1 && observed.provider_value == 0x10);
                else {
                    CHECK(result.state == -1 && observed.preflight_jobs == (test == 7 ? 0u : 1u));
                    if (test == 2 || test == 7) { future->vtable->destroy(future, 1); future = nullptr; }
                    if (test == 3) owner->forget_save_data(model.source.object);
                    // A native worker has no caller stack or polling TLS to borrow.
                    std::thread worker([&] {
                        CHECK(WritePollScope::current(owner->native_writes) == 0);
                        auto* out = reinterpret_cast<WritePreflightResult*>(model.preflight.data() + 0x60);
                        preflight_scoped(*owner, sdk.memory, model.checked.object, out, prepare, image);
                        model.preflight_worker = false; drop_job(true);
                    }); worker.join();
                    if (future) {
                        future->vtable->poll(future, &result, nullptr);
                        if (test == 0) { CHECK(result.state == -1); future->vtable->poll(future, &result, nullptr); CHECK(!result.outcome && result.value == 1); }
                        else CHECK(result.outcome == 1 && sdk.remote.writes == 0);
                    }
                }
            }
        }
        if (future) future->vtable->destroy(future, 1);
        CHECK(owner->native_writes.inspect_operation(1, observed) && !observed.provider_alive &&
            !observed.preparation_jobs && !observed.preflight_jobs && !observed.pending_handles && observed.vector_released);
        CHECK(observed.provider_terminal == (test == 0 || test == 3 || test == 4 || test == 5));
        CHECK(model.source.strong == 1 && model.source.weak == 1);
        CHECK(owner->native_writes.lost() == (test == 6 || test == 7));
        const auto public_state = owner->native_writes.snapshot(1);
        CHECK(public_state.operation_id == 1 && public_state.sdk_sequence == observed.sdk_sequence &&
            !public_state.pending_handles && !public_state.preparation_jobs && !public_state.preflight_jobs &&
            !(public_state.flags & SC_SAVE_WRITE_PROVIDER_ALIVE));
        CHECK(public_state.state == static_cast<uint32_t>(test == 0 ? SC_SAVE_WRITE_SDK_CONFIRMED :
            (test == 3 || test == 4 || test == 5 ? SC_SAVE_WRITE_NATIVE_FAILED : SC_SAVE_WRITE_INDETERMINATE)));
        if (test == 0) CHECK(public_state.file_count == 2 && public_state.submitted == 2 && public_state.completed == 2);
        const auto latest = owner->native_writes.snapshot();
        CHECK(std::memcmp(&latest, &public_state, sizeof(latest)) == 0);
        CHECK(owner->native_writes.snapshot(2).state == SC_SAVE_WRITE_NOT_RETAINED);
        if (test == 0) { CHECK(observed.sdk_sequence == 1); CHECK(inspect(sdk).operation == observed.id); }
    }
    // Native allocators may reuse addresses before an original destructor returns.
    NativeWrites owners; const auto old = owners.open_provider(1, "a"), next = owners.open_provider(2, "b");
    CHECK(owners.attach_job(old, 123, false)); const auto token = owners.detach_job(123);
    CHECK(owners.attach_job(next, 123, false)); owners.finish_job(token);
    NativeWriteOperation a, b;
    CHECK(owners.inspect_operation(old, a) && owners.inspect_operation(next, b) && !a.preparation_jobs && b.preparation_jobs == 1);
    CHECK(owners.attach_files(old, 456, 1) && owners.begin(1, 456, 1, "a") == 1);
    const auto release = owners.detach_vector(456);
    CHECK(owners.attach_files(next, 456, 1) && owners.begin(1, 456, 1, "b") == 2); owners.finish_vector(release);
    SdkWriteObservation first, second;
    CHECK(owners.inspect(1, first) && owners.inspect(2, second) && first.released && !second.released);
    {
        WritePollScope outer(owners, old); CHECK(WritePollScope::current(owners) == old);
        std::thread other([&] { CHECK(WritePollScope::current(owners) == 0); }); other.join();
        { WritePollScope inner(owners, next); CHECK(WritePollScope::current(owners) == next); }
        CHECK(WritePollScope::current(owners) == old);
    }
    CHECK(!WritePollScope::current(owners)); jobs = nullptr;
    std::puts("PASS provider operation, native job lifetime, worker and address reuse contracts (11 cases)");
}
}
void run_sdk_write_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    { // Retirement must preserve a released vector whose provider is still pending.
        NativeWrites writes; CHECK(writes.snapshot().state == SC_SAVE_WRITE_NONE);
        for (uintptr_t i = 1; i <= 64; ++i) {
            CHECK(writes.open_provider(i, "PROFILE") == i && writes.attach_files(i, i + 100, 1));
            CHECK(writes.begin(1, i + 100, 1, "PROFILE") == i);
            SdkFileWrite file{}; writes.submitted(i, writes.capture(i, file), i);
            writes.callback(i, false, 1); writes.released(i + 100);
            if (i == 2) writes.close_provider(i);
        }
        CHECK(writes.open_provider(65, "PROFILE") == 65 && writes.attach_files(65, 165, 1));
        CHECK(writes.begin(1, 165, 1, "PROFILE") == 65);
        SdkWriteObservation retained;
        CHECK(writes.inspect(1, retained) && !writes.inspect(2, retained));
        CHECK(writes.snapshot(1).state == SC_SAVE_WRITE_PENDING && writes.snapshot(2).state == SC_SAVE_WRITE_NOT_RETAINED);
        writes.provider_result(1, {0, 0, 1, 0});
        CHECK(writes.snapshot(1).state == SC_SAVE_WRITE_NATIVE_SUCCEEDED); // No prepared/captured hashes or SDK result.
    }
    for (unsigned test = 0; test < 9; ++test) {
        auto owner = make(); Model model(*owner); active = &model;
        CHECK(owner->bind_provider(owner->native_root(), model.context.remote, owner->ownership_record()));
        owner->startup_leave(false); CHECK(source(model) == 1);
        auto* entry = model.storage.data();
        if (test == 0) entry[0x178] = 0; // A borrowed nonempty buffer is not a stable prepared payload.
        if (test == 1) { const uint64_t size = 100u * 1024u * 1024u + 1u; std::memcpy(entry + 0x150, &size, 8); }
        if (test == 2) { const uint64_t capacity = 2; std::memcpy(entry + 0x158, &capacity, 8); }
        if (test == 3) std::memcpy(entry + 0x180 + 8, entry + 8, sizeof(NativeString));
        if (test == 4) model.memory.deny = reinterpret_cast<uintptr_t>(model.first.data());
        if (test == 5) std::memset(entry, 0, 8);
        if (test == 6) { const uintptr_t pointer = UINTPTR_MAX - 1; std::memcpy(entry + 0x168, &pointer, 8); }
        if (test == 7) { const int32_t length = 260; std::memcpy(entry + 0x18, &length, 4); }
        if (test == 8) { model.first.assign(65547, 'a'); model.prepared_files(); }
        Preparation input{model.context.remote, model.context.name, 1, model.context.files};
        const auto before = input; const auto original = model.storage;
        WritePreflightResult result{};
        preflight_scoped(*owner, model.memory, reinterpret_cast<uintptr_t>(&input), &result, prepare, image);
        CHECK(model.storage == original && model.remote.writes == 0);
        const auto observed = inspect(model);
        CHECK(!observed.submitted && !observed.released);
        if (test == 8) {
            CHECK(result.tag == 0 && !input.files && observed.payloads[0].prepared && !observed.payloads[0].captured);
            hash_is(observed.payloads[0].sha256, "8c207e91abdef683361565eb210b0c53584fc1a1ea9ff0ece5988a8ca5d61eb4");
        } else {
            CHECK(result.tag == 1 && result.second == 1 && owner->fault() == SessionFault::native_write);
            CHECK(input.files == before.files && input.count == before.count && input.capacity == before.capacity);
            CHECK(!observed.payloads[0].prepared); // Failed preparation never publishes a partial manifest.
            constexpr const char* expected[]{"payload_not_owned", "payload_size_limit", "payload_capacity_short",
                "payload_duplicate_name", "payload_hash_bytes_unreadable", "payload_vtable_mismatch",
                "payload_hash_extent_invalid", "payload_name_extent_invalid"};
            const auto first = owner->btrace.snapshot().first_failure;
            CHECK(first.sequence && first.operation == 1 && std::strcmp(first.predicate, expected[test]) == 0);
            // Downstream refusals cannot replace the actionable original field.
            owner->btrace.record(BStage::provider, BStatus::refused, "fixture_later_provider_failure", 1);
            CHECK(owner->btrace.snapshot().first_failure.sequence == first.sequence);
            if (test == 4) {
                bool reason = false;
                for (const auto& fact : first.facts)
                    if (fact.key && !std::strcmp(fact.key, "memory_reason")) reason = fact.value == SC_REASON_READ_FAILED;
                CHECK(reason);
            }
        }
    }
    std::puts("PASS prepared native payload ownership, worker hashing and pre-deletion refusals (9 cases)");
    for (unsigned test = 0; test < 19; ++test) {
        auto owner = make(); Model model(*owner); active = &model; model.mode = test;
        if (test == 12) {
            model.directory = "PROFILE"; model.context.name.data = model.directory.data();
            model.context.name.length = static_cast<int32_t>(model.directory.size());
        }
        model.remote_table[2] = reinterpret_cast<uintptr_t>(write_async);
        model.utility_table[11] = reinterpret_cast<uintptr_t>(completed);
        CHECK(owner->bind_provider(owner->native_root(), model.context.remote, owner->ownership_record()));
        owner->startup_leave(false);
        Preparation prepared{model.context.remote, model.context.name, 1, model.context.files};
        WritePreflightResult preflight{};
        if (test != 11) {
            CHECK(source(model) == 1);
            preflight_scoped(*owner, model.memory, reinterpret_cast<uintptr_t>(&prepared), &preflight, prepare, image);
            CHECK(preflight.tag == 0 && preflight.first == model.context.files && !prepared.files);
            CHECK(inspect(model).submitted == 0 && !inspect(model).released);
        }
        const auto real_remote = model.context.remote;
        const SdkWriteCalls calls{native_poll, image, context_init};
        if (test == 8) model.memory.deny = reinterpret_cast<uintptr_t>(model.first.data());
        if (test == 17) model.first = "abcd";
        if (test == 15) model.memory.wrong_initializer = true;
        if (test == 5) model.remote.zero = true;
        if (test == 9) model.context.remote = 0x1234;
        struct { SdkWriteResult result{}; uint64_t canary = 0xd00dbeefcafef00d; } output;
        try { poll_sdk_write(*owner, model.memory, reinterpret_cast<uintptr_t>(&model.context), &output.result, nullptr, calls); }
        catch (const std::runtime_error&) {
            CHECK(test == 10 && model.thrown);
            const auto diagnostic = owner->btrace.snapshot().first_failure;
            CHECK(diagnostic.operation == 1 && !std::strcmp(diagnostic.predicate, "sdk_native_poll_exception"));
        }
        CHECK(output.canary == 0xd00dbeefcafef00d);
        CHECK(model.context.remote == (test == 9 ? 0x1234 : real_remote));
        if (test == 9 || test == 11) {
            CHECK(model.polls == 0 && model.remote.writes == 0 && output.result.state == 0 &&
                output.result.outcome == 1 && output.result.detail == 0 && output.result.value == 1);
            CHECK(owner->native_writes.lost()); continue;
        }
        auto first = inspect(model);
        if (test == 0) {
            const auto diagnostic = owner->btrace.snapshot().first_failure;
            if (diagnostic.sequence) {
                std::fprintf(stderr, "sdk success first failure stage=%s predicate=%s operation=%llu\n",
                    b_stage_names[static_cast<size_t>(diagnostic.stage)], diagnostic.predicate,
                    static_cast<unsigned long long>(diagnostic.operation));
                for (const auto& fact : diagnostic.facts) if (fact.key)
                    std::fprintf(stderr, " %s=%lld\n", fact.key, static_cast<long long>(fact.value));
            }
            CHECK(!diagnostic.sequence);
        }
        if (test == 5) CHECK(!std::strcmp(owner->btrace.snapshot().first_failure.predicate, "sdk_submission_zero_handle"));
        if (test >= 16) CHECK(!std::strcmp(owner->btrace.snapshot().first_failure.predicate, "sdk_payload_changed_since_preflight"));
        CHECK(first.submitted == 1 && !first.terminal && !first.released);
        CHECK(first.payloads[0].handle == model.handles[0] && !first.payloads[0].completed);
        CHECK(first.payloads[0].size == (test == 17 ? 4u : 3u) && std::string(first.payloads[0].name.data()) ==
            model.directory + (test == 18 ? "/GAME.details" : "/game.details"));
        CHECK(first.unproven == (test >= 16) && first.payloads[0].captured == (test < 16));
        if (test < 16) hash_is(first.payloads[0].sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        if (test == 13) {
            callback(model, 0, 1, false, 0x532);
            CHECK(!inspect(model).payloads[0].completed && model.callback_calls == 1);
        }
        if (test == 14) {
            release_vector(model, model.context.files + 0x180);
            CHECK(!inspect(model).released);
        }
        if (test == 6) owner->stop_requests();
        if (test == 7) owner->fail(SessionFault::native_write);
        if (test != 1 && test != 5 && test != 10 && test != 15) {
            std::memset(&output.result, 0xd5, sizeof(output.result));
            poll_sdk_write(*owner, model.memory, reinterpret_cast<uintptr_t>(&model.context), &output.result, nullptr, calls);
            CHECK(inspect(model).terminal && model.context.remote == real_remote);
        }
        release_vector(model, model.context.files);
        auto last = inspect(model); CHECK(last.released);
        if (test == 15) {
            model.memory.wrong_initializer = false; model.utilities.ready = true;
            poll_released_sdk_writes(*owner, model.memory, calls);
            CHECK(model.utilities.queries == 0 && !inspect(model).payloads[0].completed && !last.utilities);
            callback(model, 0); CHECK(inspect(model).payloads[0].completed);
            continue;
        }
        if (test == 1 || test == 2 || test == 10) {
            CHECK(!last.payloads[0].completed);
            model.utilities.failed = true; // Invalid/unavailable handle returning false is not completion.
            poll_released_sdk_writes(*owner, model.memory, calls);
            CHECK(!inspect(model).payloads[0].completed && model.utilities.queries > 0);
            model.memory.wrong_initializer = true; const auto before = model.contexts;
            poll_released_sdk_writes(*owner, model.memory, calls); CHECK(model.contexts == before);
            model.memory.wrong_initializer = false;
            model.utilities.ready = true; model.utilities.failed = false;
            Utilities replacement{model.utility_table.data()};
            const auto queried = model.utilities.queries;
            model.utilities_pointer = reinterpret_cast<uintptr_t>(&replacement);
            poll_released_sdk_writes(*owner, model.memory, calls);
            CHECK(model.utilities.queries == queried && !inspect(model).payloads[0].completed);
            model.utilities_pointer = reinterpret_cast<uintptr_t>(&model.utilities);
            poll_released_sdk_writes(*owner, model.memory, calls);
            last = inspect(model); CHECK(last.payloads[0].completed && !last.payloads[0].callback && last.payloads[0].sdk_result == 0);
            CHECK(last.terminal == (test == 2));
            if (test == 2) CHECK(last.result.state == 1);
            // The same vector address can be reused while the old capture remains immutable.
            CHECK(source(model, 1) == 2);
            const auto next = owner->native_writes.begin(real_remote, model.context.files, 1, model.directory);
            CHECK(next == 2 && !inspect(model, next).released);
            callback(model, 0); CHECK(inspect(model).payloads[0].callback && inspect(model, next).submitted == 0);
        } else if (test == 5) {
            CHECK(last.payloads[0].failed && !last.payloads[0].handle && !last.terminal);
        } else if (test == 3 || test == 4) {
            CHECK(last.submitted == 1 && last.payloads[0].failed && last.payloads[0].callback);
            CHECK(last.result.outcome == 1 && last.result.value == (test == 3 ? 0x10u : 0x40u));
        } else {
            CHECK(last.submitted == 2 && last.result.state == 0 && last.result.outcome == 0 && last.result.detail == 1 && last.result.value == 0);
            CHECK(last.payloads[0].completed && last.payloads[1].completed && last.payloads[1].callback);
            hash_is(last.payloads[1].sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
            const auto contexts = model.contexts, queries = model.utilities.queries;
            CHECK(owner->native_writes.snapshot(1).state == SC_SAVE_WRITE_PENDING); // SDK alone is not the provider result.
            owner->native_writes.provider_result(1, {0, 0, 1, 0});
            const auto observed = owner->native_writes.snapshot(1);
            CHECK(observed.state == static_cast<uint32_t>(test < 16 ? SC_SAVE_WRITE_SDK_CONFIRMED : SC_SAVE_WRITE_NATIVE_SUCCEEDED));
            CHECK(observed.completed == 2 && observed.submitted == 2 && !observed.pending_handles);
            CHECK(model.contexts == contexts && model.utilities.queries == queries); // Read-only snapshots never enter Steam.
        }
        CHECK(!owner->native_writes.lost());
        if (test == 0) {
            owner->native_writes.invalidate();
            CHECK(owner->native_writes.snapshot(1).state == SC_SAVE_WRITE_NATIVE_SUCCEEDED);
        }
    }
    {
        NativeWrites writes;
        CHECK(!writes.begin(1, 2, 0, "root") && !writes.begin(1, 2, 1025, "root"));
        for (uint64_t i = 0; i < 64; ++i) {
            const auto operation = writes.open_provider(999, "root"); CHECK(operation == i + 1);
            CHECK(writes.attach_files(operation, static_cast<uintptr_t>(i + 1), 1));
            const auto id = writes.begin(1, static_cast<uintptr_t>(i + 1), 1, "root"); CHECK(id == i + 1);
            const auto file = writes.capture(id, {}); writes.submitted(id, file, i + 0x100);
            writes.released(static_cast<uintptr_t>(i + 1));
            writes.close_provider(operation);
        }
        CHECK(!writes.open_provider(999, "root")); // Never evict pending SDK ownership.
        writes.callback(0x100, false, 1);
        CHECK(writes.open_provider(999, "root") == 65 && writes.attach_files(65, 1000, 1));
        CHECK(writes.begin(1, 1000, 1, "root") == 65);
        CHECK(!writes.begin(1, 1000, 1, "root")); // No live vector alias.
    }
    {
        Session off; Model model(off); active = &model;
        model.remote_table[2] = reinterpret_cast<uintptr_t>(write_async);
        SdkWriteResult out{}; poll_sdk_write(off, model.memory, reinterpret_cast<uintptr_t>(&model.context), &out, nullptr, {native_poll});
        SdkWriteObservation absent;
        CHECK(out.state == -1 && model.remote.writes == 1 && !off.native_writes.inspect(1, absent));
        callback(model, 0); release_vector(model, model.context.files);
        CHECK(model.callback_calls == 1 && model.destructions == 1);
    }
    run_write_owner_contracts(make);
    active = nullptr;
    std::puts("PASS native SDK payload, handle, callback, release and correlated observation contracts (22 cases)");
}
