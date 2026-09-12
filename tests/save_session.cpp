// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Controlled native-layout/provider fixtures; never a DOOM persistence claim.
#include "save_collector.h"
#include "save_delete.h"
#include "save_native_hooks.h"
#include "save_write.h"
#include <algorithm>
#include <functional>

void run_profile_contracts(const std::function<std::unique_ptr<sentinel::save::Session>()>&);
void exercise_unowned_profile(sentinel::save::Session&,unsigned);
void run_provider_contracts(const std::function<std::unique_ptr<sentinel::save::Session>()>&);
void run_catalog_contracts(const std::function<std::unique_ptr<sentinel::save::Session>()>&);
void run_prerequisite_contracts(const std::function<std::unique_ptr<sentinel::save::Session>()>&);
void run_sdk_write_contracts(const std::function<std::unique_ptr<sentinel::save::Session>()>&);
void run_readback_contracts(const std::function<std::unique_ptr<sentinel::save::Session>()>&);
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL save_session:%d: %s\n", \
    __LINE__, #value); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::save;
namespace {
int64_t diagnostic_fact(const BEvent& event,const char* key) {
    for (const auto& fact:event.facts) if (fact.key && std::strcmp(fact.key,key)==0) return fact.value;
    CHECK(false); return 0;
}
std::vector<std::string> supplied;
unsigned collections = 0, releases = 0, assignments = 0;
bool fail_copy = false;
NativeString native_text(const std::string& text) {
    NativeString value{};
    value.data = static_cast<char*>(std::malloc(text.size() + 1));
    CHECK(value.data);
    std::memcpy(value.data, text.c_str(), text.size() + 1);
    value.length = static_cast<int32_t>(text.size());
    value.capacity_flags = static_cast<uint32_t>(text.size() + 1) | 0xc0000000u;
    return value;
}
void release(CollectorVector* vector) {
    ++releases;
    for (size_t i = 0; i < vector->count; ++i) {
        std::free(vector->entries[i].read_key.data);
        std::free(vector->entries[i].name.data);
    }
    delete[] vector->entries; *vector = {};
}
void assign(NativeString* value, const char* text) {
    ++assignments;
    if (fail_copy) return;
    std::free(value->data); *value = native_text(text);
}
CollectorResult* collect(const CollectorContext* context, CollectorResult* out) {
    ++collections;
    *out = {}; out->files.count = out->files.capacity = supplied.size();
    out->files.entries = new CollectorEntry[supplied.size()]{};
    for (size_t i = 0; i < supplied.size(); ++i) {
        auto& entry = out->files.entries[i];
        entry.name = native_text(supplied[i]);
        const auto slash = supplied[i].find('/');
        const std::string key = context->root.length &&
            supplied[i].substr(0, static_cast<size_t>(context->root.length)) == context->root.data ?
            supplied[i].substr(slash + 1) : supplied[i];
        entry.read_key = native_text(key); entry.size = 123;
    }
    return out;
}
struct Context {
    CollectorContext value;
    explicit Context(const Session& owner, const char* prefix = "GAME-")
        : value{0x1234, native_text(prefix), native_text(owner.native_root())} {}
    ~Context() { std::free(value.prefix.data); std::free(value.root.data); }
};
struct Fixture {
    std::filesystem::path path;
    uint32_t slot = 0;
    Fixture() {
        wchar_t temp[MAX_PATH]{};
        CHECK(GetTempPathW(MAX_PATH, temp));
        path = std::filesystem::path(temp) / (L"sentinel-session-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()));
        CHECK(CreateDirectoryW(path.c_str(), nullptr));
    }
    ~Fixture() {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        CHECK(absolute == path && path.filename().wstring().find(L"sentinel-session-") == 0);
        std::error_code error; std::filesystem::remove_all(path, error); CHECK(!error);
    }
    storage::Descriptor prepare(Session& owner) {
        storage::Descriptor descriptor{{"synthetic-native-owner", 0, ++slot, std::string(64, 'a')}, path.wstring()};
        std::unique_ptr<storage::Namespace> lease;
        const auto prepared = storage::prepare(descriptor, lease);
        if (!prepared.ok()) std::fprintf(stderr, "prepare: %s (win32=%u)\n",
            storage::outcome_name(prepared.outcome), prepared.win32_error);
        CHECK(prepared.ok());
        CHECK(owner.configure(descriptor, std::move(lease)).ok());
        return descriptor;
    }
    void admit(Session& owner, bool profile_owner = false) {
        prepare(owner);
        owner.install(0x1000, 0x2000, steam_20260818_routes);
        CHECK(owner.startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
        if (profile_owner) CHECK(owner.observe_provider_objects(0x9000, 0x9100, 0x9876));
        CHECK(owner.bind_provider(owner.native_root(), 0x1234, owner.ownership_record()));
        owner.startup_leave(false);
        if (profile_owner) CHECK(owner.provider_operation(0x9876, 0xabcd));
        CHECK(owner.routed() && owner.native_io() && !owner.accepts_requests());
    }
};
}
namespace {
struct DeleteFixture {
    struct Context {
        uintptr_t storage = 0x1234, data = 0, task = 0;
        int64_t result = -1;
        uint64_t payload = 0, job = 0;
    } context;
    struct DirectoryContext {
        uintptr_t storage = 0x1234;
        NativeString directory{};
        uintptr_t task = 0;
        int64_t result = -1;
        uint64_t payload = 0, job = 0;
    } directory_context;
    static_assert(offsetof(DirectoryContext, result) == 0x40 && offsetof(DirectoryContext, job) == 0x50);
    struct Control { uint32_t strong = 1, weak = 1; uintptr_t object = 0, destructor = 0; } control;
    DeleteFuture future{};
    std::vector<std::string> files{"GAME-AUTOSAVE0/game.details", "PROFILE/profile.bin", "ap-fixture/GAME-AUTOSAVE0/game.details"};
    std::string directory_text;
    unsigned polls = 0, launches = 0, released = 0, destroyed = 0;
    bool finish = false, scoped = false;
    DeleteResult completed{0, 0, 1, 0};
    DeleteFixture() {
        control.object = reinterpret_cast<uintptr_t>(&context);
        future.control = reinterpret_cast<uintptr_t>(&control);
    }
    explicit DeleteFixture(const std::string& directory) : DeleteFixture() {
        scoped = true; directory_text = directory;
        directory_context.directory.data = directory_text.data();
        directory_context.directory.length = static_cast<int32_t>(directory_text.size());
        directory_context.directory.capacity_flags = static_cast<uint32_t>(directory_text.size() + 1);
        control.object = reinterpret_cast<uintptr_t>(&directory_context);
    }
};
DeleteFixture* active_delete = nullptr;
void release_delete(DeleteFuture* future) {
    auto& fixture = *active_delete;
    CHECK(future == &fixture.future && future->control && fixture.control.strong && fixture.control.weak);
    if (--fixture.control.strong == 0) { fixture.control.object = 0; ++fixture.destroyed; }
    --fixture.control.weak; ++fixture.released; future->control = 0;
}
DeleteResult* poll_delete(DeleteFuture* future, DeleteResult* out, void* executor) {
    auto& fixture = *active_delete;
    CHECK(future == &fixture.future && future->control); ++fixture.polls;
    if (executor && static_cast<uint8_t*>(executor)[9]) { *out = {1, 0, 0, 0}; return out; }
    auto& job = fixture.scoped ? fixture.directory_context.job : fixture.context.job;
    if (!job) {
        ++fixture.launches; job = 123;
        if (fixture.scoped) {
            const auto prefix = fixture.directory_text + "/";
            fixture.files.erase(std::remove_if(fixture.files.begin(), fixture.files.end(), [&](const auto& key) {
                return key.size() >= prefix.size() && steam_name_equal(std::string_view(key).substr(0, prefix.size()), prefix);
            }), fixture.files.end());
        } else fixture.files.clear();
    }
    if (fixture.finish) { job = 0; release_delete(future); *out = fixture.completed; }
    else *out = {-1, 0, 0, 0};
    return out;
}
void scoped_delete_contracts(Fixture& fixtures, engine::Memory& memory) {
    const DeleteCalls calls{poll_delete, release_delete};
    for (unsigned mode = 0; mode < 19; ++mode) {
        Session owner; fixtures.admit(owner);
        std::string directory = owner.native_root() + "/GAME-AUTOSAVE0";
        if (mode == 1) directory = owner.native_root() + "/DLC1-AUTOSAVE11";
        if (mode == 2) directory = owner.native_root() + "/dlc2-autosave5";
        if (mode == 3 || mode == 18) directory = "PROFILE";
        if (mode == 4) directory = "GAME-AUTOSAVE0";
        if (mode == 5) directory = owner.native_root();
        if (mode == 6) directory = owner.native_root() + "x/GAME-AUTOSAVE0";
        if (mode == 7) directory = "foreign/GAME-AUTOSAVE0";
        if (mode == 8) directory = owner.native_root() + "/GAME-AUTOSAVE12";
        if (mode == 9) directory = owner.native_root() + "/GAME-AUTOSAVE01";
        if (mode == 10) directory += "/child";
        if (mode == 11) directory = owner.native_root() + "/PROFILE";
        if (mode == 17) directory[owner.native_root().size() + 3] = '\0';
        DeleteFixture fixture(directory); active_delete = &fixture; DeleteResult result{};
        // These changes happen after the native factory captured its context.
        if (mode == 12) fixture.directory_context.storage = 0x5678;
        if (mode == 13) owner.fail(SessionFault::native_write);
        if (mode == 14) fixture.directory_context.storage = 0;
        if (mode == 15) fixture.directory_context.directory.length = 64;
        if (mode == 16) fixture.directory_context.directory.data = reinterpret_cast<char*>(1);
        if (mode == 18) fixture.control.strong = fixture.control.weak = 2;
        fixture.files.push_back(directory + "0/game.details");
        fixture.files.push_back(owner.native_root() + "/sentinel-owner.bin");
        const auto protected_files = fixture.files;
        fixture.files.push_back(directory + "/game.details");
        fixture.files.push_back(directory + "/nested/stream.bin");
        const auto before = fixture.files;
        poll_scoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        if (mode < 3) {
            CHECK(result.state == -1 && fixture.polls == 1 && fixture.launches == 1 && !fixture.released);
            CHECK(fixture.files == protected_files && fixture.control.strong == 1 && fixture.control.object);
            fixture.finish = true;
            poll_scoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
            CHECK(!result.state && !result.outcome && result.error == 1 && fixture.polls == 2 && fixture.launches == 1);
            CHECK(fixture.released == 1 && fixture.destroyed == 1 && !fixture.future.control && !fixture.control.strong && !fixture.control.weak);
            CHECK(owner.native_io() && owner.fault() == SessionFault::none);
        } else {
            CHECK(!result.state && result.outcome == 1 && result.error == 1);
            CHECK(!fixture.polls && !fixture.launches && fixture.released == 1 && !fixture.future.control && fixture.files == before);
            CHECK(fixture.control.strong == (mode == 18 ? 1u : 0u) && fixture.control.weak == (mode == 18 ? 1u : 0u));
            CHECK(fixture.destroyed == (mode == 18 ? 0u : 1u));
            CHECK(owner.state() == SessionState::faulted && owner.routed() &&
                owner.fault() == (mode == 13 ? SessionFault::native_write : SessionFault::unscoped_delete));
            poll_scoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
            CHECK(result.state == 1 && !fixture.polls && !fixture.launches && fixture.released == 1);
        }
    }
    for (bool failed : {false, true}) {
        Session owner; fixtures.admit(owner); DeleteFixture fixture(owner.native_root() + "/GAME-AUTOSAVE0");
        active_delete = &fixture; DeleteResult result{}; std::array<uint8_t, 16> executor{};
        fixture.files = {fixture.directory_text + "/game.details", "PROFILE/profile.bin"};
        poll_scoped_delete(owner, memory, &fixture.future, &result, executor.data(), calls);
        CHECK(fixture.files == std::vector<std::string>{"PROFILE/profile.bin"}); // Deletion already occurred in this model.
        owner.fail(SessionFault::native_write); executor[9] = 1;
        poll_scoped_delete(owner, memory, &fixture.future, &result, executor.data(), calls);
        CHECK(result.state == 1 && fixture.directory_context.job && fixture.future.control && !fixture.released && fixture.launches == 1);
        executor[9] = 0;
        poll_scoped_delete(owner, memory, &fixture.future, &result, executor.data(), calls);
        CHECK(result.state == -1 && fixture.control.strong == 1 && !fixture.destroyed && fixture.launches == 1);
        fixture.finish = true; if (failed) fixture.completed = {0, 1, 4, 0};
        poll_scoped_delete(owner, memory, &fixture.future, &result, executor.data(), calls);
        CHECK(!result.state && result.outcome == (failed ? 1 : 0) && result.error == (failed ? 4u : 1u));
        CHECK(fixture.released == 1 && fixture.destroyed == 1 && !fixture.future.control && fixture.launches == 1);
        CHECK(owner.state() == SessionState::faulted && owner.fault() == SessionFault::native_write);
    }
    {
        Session owner; fixtures.admit(owner); DeleteFixture fixture("foreign/GAME-AUTOSAVE0");
        active_delete = &fixture; DeleteResult result{}; fixture.directory_context.job = 999;
        poll_scoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(result.state == -1 && fixture.polls == 1 && !fixture.launches && !fixture.released);
        CHECK(owner.fault() == SessionFault::delete_indeterminate);
        fixture.finish = true;
        poll_scoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(!result.state && !result.outcome && fixture.released == 1 && !fixture.launches);
        CHECK(owner.state() == SessionState::faulted && !fixture.future.control);
    }
    for (bool invalid_control : {false, true}) {
        Session owner; fixtures.admit(owner); DeleteFixture fixture(owner.native_root() + "/GAME-AUTOSAVE0");
        active_delete = &fixture; DeleteResult result{};
        if (invalid_control) fixture.future.control = 1; else fixture.directory_context.result = 0;
        poll_scoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(result.state == 1 && !fixture.polls && !fixture.launches && !fixture.released && fixture.control.strong == 1);
        CHECK(owner.fault() == SessionFault::delete_indeterminate && owner.routed());
    }
    {
        Session disabled; DeleteFixture fixture("PROFILE"); active_delete = &fixture; DeleteResult result{};
        fixture.finish = true;
        poll_scoped_delete(disabled, memory, &fixture.future, &result, nullptr, calls);
        CHECK(!result.state && !result.outcome && fixture.polls == 1 && fixture.launches == 1 && fixture.released == 1);
        CHECK(disabled.state() == SessionState::disabled && disabled.fault() == SessionFault::none);
    }
}
struct AuxiliaryContext {
    uintptr_t remote = 0x1234;
    NativeString directory{}, prefix{}, suffix{};
    uintptr_t names = 0; uint64_t count = 0, capacity = 0;
    uintptr_t task = 42; int64_t result = -1; uint64_t payload = 0, job = 17;
};
unsigned auxiliary_runs = 0;
std::vector<std::string> auxiliary_files;
bool auxiliary_delete_fails = false;
DeleteOperationResult* auxiliary_operation(uintptr_t value, DeleteOperationResult* out) {
    ++auxiliary_runs;
    const auto& context = *reinterpret_cast<AuxiliaryContext*>(value);
    const std::string prefix = std::string(context.directory.data) + "/";
    if (!auxiliary_delete_fails) auxiliary_files.erase(std::remove_if(auxiliary_files.begin(), auxiliary_files.end(),
        [&](const auto& name) { return name.compare(0, prefix.size(), prefix) == 0; }), auxiliary_files.end());
    *out = {0, 1}; return out; // Native aggregate ignores individual deletion failures.
}
void auxiliary_contracts(Fixture& fixtures, engine::Memory& memory) {
    for (unsigned test = 0; test < 14; ++test) {
        Session owner; fixtures.admit(owner);
        std::string path = owner.native_root() + (test == 1 ? "/DLC2-AUTOSAVE11" : "/GAME-AUTOSAVE0");
        std::string name = "game.details";
        if (test == 2) path = "GAME-AUTOSAVE0";
        if (test == 3) path = owner.native_root() + "x/GAME-AUTOSAVE0";
        if (test == 4) name = "../PROFILE/profile.bin";
        if (test == 5) name = "..\\PROFILE\\profile.bin";
        if (test == 6) name = "..";
        if (test == 7) name = std::string("game\0.details", 13);
        auto native_name = native_text(name);
        AuxiliaryContext context;
        context.directory = native_text(path); context.names = reinterpret_cast<uintptr_t>(&native_name);
        context.count = context.capacity = 1;
        if (test == 8) context.remote = 0x4321;
        if (test == 9) context.capacity = 0;
        if (test == 10) owner.fail(SessionFault::provider_identity);
        if (test == 11) context.names = 1;
        if (test == 12) { context.names = 0; context.count = context.capacity = 0; }
        const auto before_context = context;
        auxiliary_runs = 0; auxiliary_delete_fails = test == 13;
        auxiliary_files = {"GAME-AUTOSAVE0/game.details", "PROFILE/profile.bin", path + "/game.details"};
        const auto before = auxiliary_files;
        DeleteOperationResult result{};
        CHECK(delete_auxiliary_scoped(owner, memory, reinterpret_cast<uintptr_t>(&context), &result, auxiliary_operation) == &result);
        const bool allowed = test <= 1 || test >= 12;
        CHECK(auxiliary_runs == (allowed ? 1u : 0u) && result.outcome == (allowed ? 0 : 1) && result.value == 1);
        CHECK(std::memcmp(&context, &before_context, sizeof(context)) == 0); // Job/ref/notification ownership untouched.
        CHECK(auxiliary_files[0] == before[0] && auxiliary_files[1] == before[1]);
        if (!allowed || test == 13) CHECK(auxiliary_files == before);
        if (!allowed) CHECK(owner.routed() && !owner.native_io());
        std::free(native_name.data); std::free(context.directory.data);
    }
}
void delete_contracts(Fixture& fixtures, engine::Memory& memory) {
    const DeleteCalls calls{poll_delete, release_delete};
    {
        Session disabled; DeleteFixture fixture; active_delete = &fixture; DeleteResult result{};
        refuse_unscoped_delete(disabled, memory, &fixture.future, &result, nullptr, calls);
        CHECK(fixture.launches == 1 && fixture.polls == 1 && result.state == -1); // Native default path.
    }
    {
        Session owner; fixtures.admit(owner); DeleteFixture fixture; active_delete = &fixture; DeleteResult result{};
        const auto protected_files = fixture.files;
        refuse_unscoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(result.state == 0 && result.outcome == 1 && result.error == 1);
        CHECK(fixture.polls == 0 && fixture.launches == 0 && fixture.released == 1 && !fixture.future.control);
        CHECK(fixture.files == protected_files && owner.routed() && !owner.accepts_requests());
        CHECK(owner.fault() == SessionFault::unscoped_delete);
        refuse_unscoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(fixture.polls == 0 && fixture.released == 1 && result.state == 1); // No second release/restart.
    }
    {
        Session owner; fixtures.admit(owner); DeleteFixture fixture; active_delete = &fixture; DeleteResult result{};
        fixture.context.job = 999; // Execution may already have happened; zero I/O is not asserted.
        refuse_unscoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(result.state == -1 && fixture.polls == 1 && !fixture.launches && !fixture.released);
        CHECK(owner.fault() == SessionFault::delete_indeterminate && owner.routed() && !owner.accepts_requests());
        fixture.finish = true;
        refuse_unscoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(result.state == 0 && result.outcome == 0 && fixture.released == 1 && !fixture.launches);
        CHECK(owner.state() == SessionState::faulted && owner.routed()); // Native result cannot reauthorize.
    }
    for (unsigned invalid = 0; invalid != 2; ++invalid) {
        Session owner; fixtures.admit(owner); DeleteFixture fixture; active_delete = &fixture; DeleteResult result{};
        if (invalid) fixture.future.control = 1;
        else fixture.context.result = 0; // A completed-looking payload with no job is not fresh ownership.
        refuse_unscoped_delete(owner, memory, &fixture.future, &result, nullptr, calls);
        CHECK(result.state == 1 && !fixture.polls && !fixture.released && !fixture.launches);
        CHECK(owner.fault() == SessionFault::delete_indeterminate && owner.routed());
    }
    scoped_delete_contracts(fixtures, memory); active_delete = nullptr;
}
}
namespace {
struct WriteFixture {
    NativeString name{};
    char text[64]{};
    NativeString mask_prefix{}; char prefix_text[8]{};
    NativeString mask_suffix{}; char suffix_text[8]{};
    unsigned char data_padding[0x1c0 - 0xe0]{};
    uintptr_t files = 0;
    int32_t file_count = 0, file_capacity = 0;
    struct { uint32_t strong = 2, weak = 2; uintptr_t data = 0, destructor = 0; } control;
    SaveReference reference{};
    unsigned creates = 0, assigns = 0, released = 0;
    bool fail_assign = false, throw_assign = false;
    SaveFuture* backend = nullptr; bool missing_future = false;
    explicit WriteFixture(const std::string& path) {
        CHECK(path.size() < sizeof(text));
        std::memcpy(text, path.c_str(), path.size() + 1);
        name.data = text; name.length = static_cast<int32_t>(path.size()); name.capacity_flags = 64;
        mask_prefix.data = prefix_text; mask_prefix.capacity_flags = 8;
        mask_suffix.data = suffix_text; mask_suffix.capacity_flags = 8;
        control.data = reinterpret_cast<uintptr_t>(&name);
        reference.control = reinterpret_cast<uintptr_t>(&control);
    }
};
static_assert(offsetof(WriteFixture, files) == 0x1c0 && offsetof(WriteFixture, mask_prefix) == 0x70 &&
    offsetof(WriteFixture, mask_suffix) == 0xa8);
WriteFixture* active_write = nullptr;
SaveFuture* success_destroy(SaveFuture* future, uint32_t) { return future; }
SaveResult* success_poll(SaveFuture*, SaveResult* out, void*) { *out = {0, 0, 1, 0}; return out; }
const SaveFutureVtable success_vtable{success_destroy, success_poll};
SaveFuture success_future{&success_vtable};
struct BackendFuture : SaveFuture { SaveResult result{}; unsigned polls = 0, destroys = 0; };
SaveFuture* destroy_backend(SaveFuture* value, uint32_t) { ++static_cast<BackendFuture*>(value)->destroys; return value; }
SaveResult* poll_backend(SaveFuture* value, SaveResult* out, void*) {
    auto& future = *static_cast<BackendFuture*>(value);
    *out = future.polls++ == 0 ? SaveResult{-1, 0, 0, 0} : future.result; return out;
}
const SaveFutureVtable backend_vtable{destroy_backend, poll_backend};
void release_write(SaveReference* reference) {
    auto& f = *active_write;
    CHECK(reference == &f.reference && reference->control && f.control.strong == 2);
    ++f.released; --f.control.strong; --f.control.weak; reference->control = 0;
}
void set_write_name(uintptr_t data, const char* name) {
    auto& f = *active_write;
    CHECK(data == f.control.data); ++f.assigns;
    if (f.throw_assign) throw std::bad_alloc();
    if (f.fail_assign) return;
    const auto count = std::strlen(name); CHECK(count < sizeof(f.text));
    std::memcpy(f.text, name, count + 1); f.name.length = static_cast<int32_t>(count);
}
SaveFuture** create_write(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* ref) {
    CHECK(provider == 0x9876 && identity == 0xabcd);
    ++active_write->creates; release_write(ref);
    *out = active_write->missing_future ? nullptr : active_write->backend ? active_write->backend : &success_future;
    return out;
}
bool successful(SaveFuture* future) {
    SaveResult result{}; future->vtable->poll(future, &result, nullptr);
    const bool okay = result.state == 0 && result.outcome == 0 && result.value == 1;
    future->vtable->destroy(future, 1); return okay;
}
Session* enumeration_owner = nullptr;
CollectorResult enumeration_result{};
unsigned enumeration_creates = 0;
EnumerationFuture** create_enumeration(EnumerationFuture** out, SaveReference* reference, const char* root, const char* prefix) {
    ++enumeration_creates;
    Context context(*enumeration_owner, prefix);
    std::free(context.value.root.data); context.value.root = native_text(root);
    engine::LocalMemory memory;
    collect_scoped(*enumeration_owner, memory, &context.value, &enumeration_result, {collect, assign, release});
    release_write(reference);
    // Opaque fixture token only; production leaves construction/polling to native code.
    *out = reinterpret_cast<EnumerationFuture*>(&enumeration_creates);
    return out;
}
struct AuxiliaryFile { uintptr_t vtable = 0x12a575a8; NativeString name{}; };
std::vector<std::string> remote_names;
bool erase_auxiliary = false;
SaveFuture** create_erase(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* reference) {
    auto& f = *active_write; const std::string stem = std::string(f.text) + "/";
    // Model only the recovered worker's path matching. Its returned bool is not
    // treated as proof that real Steam deletions completed successfully.
    remote_names.erase(std::remove_if(remote_names.begin(), remote_names.end(), [&](const std::string& key) {
        if (!erase_auxiliary) return key.compare(0, stem.size(), stem) == 0;
        for (int32_t i = 0; i < f.file_count; ++i) {
            auto file = reinterpret_cast<const AuxiliaryFile*>(reinterpret_cast<uintptr_t*>(f.files)[i]);
            if (key == stem + file->name.data) return true;
        }
        if (key.compare(0, stem.size(), stem) == 0 && f.mask_prefix.length && f.mask_suffix.length) {
            const auto name = std::string_view(key).substr(key.find_last_of('/') + 1);
            const auto prefix = std::string_view(f.mask_prefix.data), suffix = std::string_view(f.mask_suffix.data);
            if (name.size() >= suffix.size() && steam_name_equal(name.substr(0, prefix.size()), prefix) &&
                steam_name_equal(name.substr(name.size() - suffix.size()), suffix)) return true;
        }
        return false;
    }), remote_names.end());
    return create_write(provider, out, identity, reference);
}
void route_contracts(Fixture& fixtures, engine::Memory& memory) {
    for (auto root : {"", "foreign-root"}) for (auto stem : {"GAME-AUTOSAVE0", "game-autosave0"}) {
        Session owner; fixtures.admit(owner); enumeration_owner = &owner;
        WriteFixture f("unused"); active_write = &f;
        supplied = {owner.native_root() + "/" + stem + "/game.details"};
        EnumerationFuture* future = nullptr; const auto before = collections;
        CHECK(enumerate_scoped(owner, memory, &future, &f.reference, root, "GAME-", create_enumeration) == &future);
        CHECK(future && f.released == 1 && f.control.strong == 1);
        if (!*root) {
            CHECK(enumeration_result.tag == 0 && collections == before + 1);
            CHECK(enumeration_result.files.entries[0].read_key.data == supplied[0]);
            release(&enumeration_result.files);
        } else {
            CHECK(enumeration_result.tag == 1 && collections == before && owner.routed());
            CHECK(owner.fault() == SessionFault::foreign_collector);
        }
    }
    const WriteCalls calls{create_erase, set_write_name, release_write, nullptr, 0x10000000};
    for (bool auxiliary : {false, true}) for (auto path : {"GAME-AUTOSAVE1", "PROFILE", "foreign/GAME-AUTOSAVE1"}) {
        Session owner; fixtures.admit(owner); WriteFixture f(path); active_write = &f;
        AuxiliaryFile file; file.name = native_text("game.details");
        uintptr_t files[]{reinterpret_cast<uintptr_t>(&file)};
        f.files = reinterpret_cast<uintptr_t>(files); f.file_count = f.file_capacity = 1;
        const std::string vanilla = "GAME-AUTOSAVE1/game.details";
        const auto owned = owner.native_root() + "/" + vanilla;
        remote_names = {vanilla, owned, "foreign/" + vanilla}; erase_auxiliary = auxiliary;
        SaveFuture* future = nullptr;
        delete_scoped(owner, memory, 0x9876, &future, 0xabcd, &f.reference, calls, auxiliary);
        CHECK(future && f.released == 1 && f.control.strong == 1);
        if (std::strcmp(path, "GAME-AUTOSAVE1") == 0) {
            CHECK(f.creates == 1 && remote_names == std::vector<std::string>({vanilla, "foreign/" + vanilla}));
        } else {
            CHECK(!f.creates && !f.assigns && remote_names.size() == 3 && owner.routed());
            SaveResult result{}; future->vtable->poll(future, &result, nullptr);
            CHECK(result.state == 0 && result.outcome == 1 && result.value == 1);
        }
        future->vtable->destroy(future, 1); std::free(file.name.data);
    }
    for (auto name : {"GAME-AUTOSAVE1/game.details", "../PROFILE/profile.bin", "/PROFILE/profile.bin",
                     "game//details", "C:evil", "game\\details", ".", "game/../details",
                     "sentinel-owner-other.txt", "SENTINEL-selection-GAME.txt"}) {
        Session owner; fixtures.admit(owner); WriteFixture f(""); active_write = &f;
        AuxiliaryFile file; file.name = native_text(name); uintptr_t files[]{reinterpret_cast<uintptr_t>(&file)};
        f.files = reinterpret_cast<uintptr_t>(files); f.file_count = f.file_capacity = 1;
        remote_names = {"GAME-AUTOSAVE1/game.details", owner.native_root() + "/GAME-AUTOSAVE1/game.details"};
        erase_auxiliary = true; SaveFuture* future = nullptr;
        delete_scoped(owner, memory, 0x9876, &future, 0xabcd, &f.reference, calls, true);
        const bool supported = std::strcmp(name, "GAME-AUTOSAVE1/game.details") == 0;
        CHECK(f.creates == (supported ? 1u : 0u) && f.assigns == (supported ? 1u : 0u));
        CHECK(remote_names.size() == (supported ? 1u : 2u) && remote_names[0] == "GAME-AUTOSAVE1/game.details");
        CHECK(f.released == 1 && f.control.strong == 1);
        future->vtable->destroy(future, 1); std::free(file.name.data);
    }
    for (auto prefix : {"s", "Sentine", "game", ""}) for (auto suffix : {".txt", ""}) {
        Session owner; fixtures.admit(owner); WriteFixture f(""); active_write = &f;
        std::memcpy(f.prefix_text, prefix, std::strlen(prefix) + 1); f.mask_prefix.length = static_cast<int32_t>(std::strlen(prefix));
        std::memcpy(f.suffix_text, suffix, std::strlen(suffix) + 1); f.mask_suffix.length = static_cast<int32_t>(std::strlen(suffix));
        const auto marker = owner.native_root() + "/sentinel-owner-current.txt";
        const auto native = owner.native_root() + "/GAME-AUTOSAVE0/game.txt";
        remote_names = {marker, native, "GAME-AUTOSAVE0/game.txt"}; erase_auxiliary = true;
        SaveFuture* future = nullptr;
        delete_scoped(owner, memory, 0x9876, &future, 0xabcd, &f.reference, calls, true);
        const bool refused = *suffix && (std::strcmp(prefix, "s") == 0 || std::strcmp(prefix, "Sentine") == 0);
        CHECK(f.creates == (refused ? 0u : 1u) && f.assigns == (refused ? 0u : 1u));
        CHECK(remote_names.front() == marker && remote_names.back() == "GAME-AUTOSAVE0/game.txt");
        CHECK(remote_names.size() == ((*suffix && std::strcmp(prefix, "game") == 0) ? 2u : 3u));
        CHECK(f.released == 1 && f.control.strong == 1);
        future->vtable->destroy(future, 1);
    }
    supplied.clear(); active_write = nullptr; enumeration_owner = nullptr;
}
struct PreflightContext {
    uintptr_t remote = 0x1234; NativeString name{};
    uint64_t clear = 1, files = 0xfeed, count = 2, capacity = 4;
};
static_assert(offsetof(PreflightContext, clear) == 0x38 && offsetof(PreflightContext, files) == 0x40);
unsigned preflights = 0;
WritePreflightResult* native_preflight(uintptr_t raw, WritePreflightResult* out) {
    auto& context = *reinterpret_cast<PreflightContext*>(raw); ++preflights;
    const std::string prefix = std::string(context.name.data) + "/";
    if (context.clear) remote_names.erase(std::remove_if(remote_names.begin(), remote_names.end(), [&](const auto& name) {
        return steam_name_equal(std::string_view(name).substr(0, prefix.size()), prefix);
    }), remote_names.end());
    *out = {0, context.files, context.count, context.capacity};
    context.files = context.count = context.capacity = 0; // Native success moves its owned vector.
    return out;
}
void preflight_contracts(Fixture& fixtures, engine::Memory& memory) {
    for (unsigned test = 0; test < 24; ++test) {
        Session owner;
        if (test == 1) fixtures.prepare(owner);
        else if (test != 0 && test != 19) fixtures.admit(owner);
        const auto root = owner.native_root();
        std::string name = test <= 1 || test == 19 ? "vanilla-other" : root + "/GAME-AUTOSAVE0";
        if (test == 3 || test == 14) name = "PROFILE";
        if (test == 4) name = "foreign/GAME-AUTOSAVE0";
        if (test == 5) name = "GAME-AUTOSAVE0";
        if (test == 6) name = root + "/GAME-AUTOSAVE12";
        if (test == 7) name = root;
        PreflightContext context; context.name = native_text(name);
        std::array<unsigned char, 0x300> payloads{};
        const uintptr_t table = 0x2a57348; // Native prepared idFile_Memory vector.
        for (unsigned i = 0; i < 2; ++i) {
            auto* entry = payloads.data() + i * 0x180;
            NativeString file{}; file.data = const_cast<char*>(i ? "SlotFile" : "game.details");
            file.length = i ? 8 : 12;
            std::memcpy(entry, &table, 8); std::memcpy(entry + 8, &file, sizeof(file));
        }
        context.files = reinterpret_cast<uintptr_t>(payloads.data());
        if (test == 8) context.remote = 0x4567;
        if (test == 9) owner.stop_requests();
        if (test == 10) owner.fail(SessionFault::native_write);
        if (test == 11) context.name.data[context.name.length] = 'x';
        if (test == 13 || test == 14 || test == 18 || test == 19) context.count = 0;
        if (test == 15) context.files = 0;
        if (test == 16) context.capacity = 1;
        if (test == 17) context.count = context.capacity = 1;
        if (test == 18) context.clear = 0;
        if (test == 20) context.count = context.capacity = UINT64_MAX;
        if (test == 21) context.count = context.capacity = 1025;
        const auto files = context.files, count = context.count, capacity = context.capacity;
        if (owner.routed() && test != 22) {
            const auto operation = owner.native_writes.open_provider(0x123, name);
            owner.native_writes.attach_files(operation, files, count);
            if (test == 23) owner.forget_save_data(0x123);
        }
        remote_names = {name + "/game.details", "other/GAME-AUTOSAVE0/game.details"};
        const auto original_names = remote_names;
        const auto before = preflights; WritePreflightResult result{};
        CHECK(preflight_scoped(owner, memory, test == 12 ? 0 : reinterpret_cast<uintptr_t>(&context), &result, native_preflight, 0) == &result);
        const bool accepted = test == 0 || test == 2 || test == 3 || test == 9 || test == 17 || test == 19;
        CHECK(preflights == before + (accepted ? 1u : 0u));
        CHECK(remote_names.size() == (accepted ? 1u : 2u));
        if (accepted) CHECK(result.tag == 0 && result.first == files && result.second == count && result.third == capacity && !context.files && !context.count);
        else CHECK(result.tag == 1 && !result.first && result.second == 1 && !result.third &&
            context.files == files && context.count == count && context.capacity == capacity && remote_names == original_names);
        if (test == 1) CHECK(owner.state() == SessionState::rejected && owner.fault() == SessionFault::missed_startup);
        if (!accepted && test != 1) CHECK(owner.routed() && owner.fault() == SessionFault::native_write);
        std::free(context.name.data);
    }
}
void write_contracts(Fixture& fixtures, engine::Memory& memory) {
    const WriteCalls calls{create_write, set_write_name, release_write,
        [](Session&, engine::Memory&, uintptr_t) { return true; }}; // PROFILE delegation; full policy has its own host.
    const auto invoke = [&](Session& owner, WriteFixture& f, bool reading = false) {
        active_write = &f; SaveFuture* future = nullptr;
        const auto result = reading ? read_scoped(owner, memory, 0x9876, &future, 0xabcd, &f.reference, calls) :
            write_scoped(owner, memory, 0x9876, &future, 0xabcd, &f.reference, calls);
        CHECK(result == &future);
        CHECK(future && f.released == 1 && !f.reference.control && f.control.strong == 1);
        return future;
    };
    {
        Session off; WriteFixture f("native-other-domain");
        CHECK(invoke(off, f) == &success_future && f.creates == 1 && !f.assigns);
    }
    for (unsigned defect=0;defect<6;++defect) {
        Session owner; fixtures.prepare(owner);
        WriteFixture f(defect==1?"GAME-AUTOSAVE0":defect==2?"ap-fixture/GAME-AUTOSAVE0":defect==3?"foreign-name":"PROFILE");
        if (defect==4) f.name.length=64;
        if (defect==5) f.control.data=1;
        NativeRouteScope route(0x1148e225,0x10000000);
        auto future=invoke(owner,f,true); SaveResult result{};
        future->vtable->poll(future,&result,nullptr); future->vtable->destroy(future,1);
        CHECK(!f.creates && !f.assigns && result.state==0 && result.outcome==1);
        CHECK(owner.fault()==SessionFault::missed_startup && !owner.routed());
        const auto trace=owner.unrouted_trace();
        CHECK(trace.caller_rva==0x148e225 && trace.source.kind==(defect>=4?1u:defect+2));
        CHECK(trace.source.step==(defect>=4?3u:5u));
        CHECK((trace.source.reason!=0)==(defect>=4));
        if (defect<4) CHECK(std::strcmp(trace.source.name.data(),f.text)==0);
        const auto first=owner.btrace.snapshot().first_failure;
        CHECK(std::strcmp(first.predicate,"unrouted_import_before_root_observation")==0);
        CHECK(diagnostic_fact(first,"source_kind")==trace.source.kind);
        owner.stop_requests(); CHECK(owner.unrouted_trace().source.kind==trace.source.kind);
    }
    for (auto name : {"GAME-AUTOSAVE7", "game-autosave7", "DLC1-AUTOSAVE11", "DLC1-autosave11",
                     "PROFILE", "foreign/GAME-AUTOSAVE0"}) {
        Session owner; fixtures.admit(owner); WriteFixture f(name);
        auto future = invoke(owner, f, true);
        if (std::strcmp(name, "foreign/GAME-AUTOSAVE0") == 0) {
            CHECK(!f.creates && !f.assigns && owner.fault() == SessionFault::native_read && owner.routed());
            SaveResult result{}; future->vtable->poll(future, &result, nullptr);
            CHECK(result.state == 0 && result.outcome == 1 && result.value == 1);
            future->vtable->destroy(future, 1);
        } else {
            CHECK(successful(future) && f.creates == 1);
            const bool profile = std::strcmp(name, "PROFILE") == 0;
            CHECK(f.assigns == (profile ? 0u : 1u));
            CHECK(f.text == (profile ? std::string(name) : owner.native_root() + "/" + name));
        }
    }
    for (auto slot : {"GAME-AUTOSAVE0", "DLC1-AUTOSAVE10", "DLC2-AUTOSAVE11", "game-autosave0", "DLC2-autosave11"}) {
        Session owner; fixtures.admit(owner); WriteFixture f(slot);
        CHECK(successful(invoke(owner, f)) && f.creates == 1 && f.assigns == 1);
        CHECK(f.text == owner.native_root() + "/" + slot && owner.native_io() && !owner.accepts_requests());
        WriteFixture already(f.text);
        CHECK(successful(invoke(owner, already)) && !already.assigns && already.creates == 1);
    }
    {
        Session owner; fixtures.admit(owner); owner.stop_requests();
        WriteFixture f("GAME-AUTOSAVE7");
        CHECK(successful(invoke(owner, f)) && f.assigns == 1); // Retained native route.
        WriteFixture profile("PROFILE");
        auto refused = invoke(owner, profile); SaveResult outcome{};
        refused->vtable->poll(refused, &outcome, nullptr);
        CHECK(outcome.state == 0 && outcome.outcome == 1 && !profile.creates && !profile.assigns);
        refused->vtable->destroy(refused, 1); // Output validation alone cannot invent an operation's selection.
        CHECK(std::strcmp(profile.text, "PROFILE") == 0); // Shared settings domain unchanged here.
    }
    const SaveResult outcomes[]{{0, 1, 0x40, 0}, {0, 1, 0x100, 0}, {0, 0, 0, 0},
        {1, 0, 0, 0}, {0, 0, 1, 0}, {0, 1, 1, 0}};
    for (bool reading : {false, true}) for (unsigned test = 0; test < 8; ++test) {
        Session owner; fixtures.admit(owner); WriteFixture f("GAME-AUTOSAVE0");
        BackendFuture backend{{&backend_vtable}, outcomes[test < 6 ? test : 4]};
        f.backend = &backend; f.missing_future = test == 7;
        auto future = invoke(owner, f, reading); SaveResult result{};
        CHECK(!f.reference.control && f.released == 1 && f.control.strong == 1);
        future->vtable->poll(future, &result, nullptr);
        if (test == 7) CHECK(result.state == 0 && result.outcome == 1 && result.value == 1 && !backend.polls);
        else {
            CHECK(result.state == -1 && backend.polls == 1 && owner.fault() == SessionFault::none);
            if (test != 6) {
                owner.stop_requests(); // Existing native work remains polled and confined.
                future->vtable->poll(future, &result, nullptr);
                CHECK(result.state == 0 && result.outcome == (test == 4 ? 0 : 1) && result.value == 1);
                CHECK(backend.polls == 2 && !backend.destroys);
                future->vtable->poll(future, &result, nullptr);
                CHECK(result.state == 1 && backend.polls == 2);
            }
        }
        future->vtable->destroy(future, 1);
        CHECK(backend.destroys == (test == 7 ? 0u : 1u));
        CHECK(owner.fault() == (test == 4 || test == 6 ? SessionFault::none :
            reading ? SessionFault::native_read : SessionFault::native_write));
    }
    for (bool reading : {false, true}) for (auto name : {"game.details", "../PROFILE/profile.bin", "/profile.bin", "game//details"}) {
        Session owner; fixtures.admit(owner); WriteFixture f("GAME-AUTOSAVE0"); active_write = &f;
        AuxiliaryFile file; file.name = native_text(name); uintptr_t files[]{reinterpret_cast<uintptr_t>(&file)};
        f.files = reinterpret_cast<uintptr_t>(files); f.file_count = f.file_capacity = 1;
        WriteCalls file_calls = calls; file_calls.image_base = 0x10000000;
        SaveFuture* future = nullptr;
        if (reading) read_scoped(owner, memory, 0x9876, &future, 0xabcd, &f.reference, file_calls);
        else write_scoped(owner, memory, 0x9876, &future, 0xabcd, &f.reference, file_calls);
        const bool valid = std::strcmp(name, "game.details") == 0;
        CHECK(f.creates == (valid ? 1u : 0u) && f.assigns == (valid ? 1u : 0u));
        CHECK(successful(future) == valid && f.released == 1 && f.control.strong == 1);
        std::free(file.name.data);
    }
    for (auto path : {"ap-foreign/GAME-AUTOSAVE0", "GAME-AUTOSAVE01", "GAME-AUTOSAVE12",
                     "../GAME-AUTOSAVE0", "PROFILE/../GAME-AUTOSAVE0"}) {
        Session owner; fixtures.admit(owner); WriteFixture f(path);
        auto future = invoke(owner, f);
        CHECK(!f.creates && !f.assigns && std::strcmp(f.text, path) == 0);
        CHECK(owner.routed() && owner.fault() == SessionFault::native_write && !owner.accepts_requests());

        // Native-modeled 14148e670 -> 14149a120 -> 14148d970 ordering: the caller
        // still publishes its task and owns the lock before polling the provider
        // result. Failure finalization happens through that task, never early.
        bool published = true, saving = true, lock_owned = true;
        unsigned notifications = 0, finalizer_runs = 0; uint32_t error = 0;
        CHECK(saving && lock_owned && notifications == 0 && f.control.strong == 1);
        SaveResult result{-1, -1, 0, 0};
        CHECK(future->vtable->poll(future, &result, &published) == &result);
        CHECK(result.state == 0 && result.outcome == 1 && result.value == 1);
        future->vtable->destroy(future, 1);
        CHECK(published && saving && notifications == 0); // Destructor is not a receipt.
        if (result.state == 0 && result.outcome == 1) error = 0x10;
        ++finalizer_runs; published = false; saving = false; ++notifications;
        --f.control.strong; --f.control.weak; lock_owned = false;
        CHECK(error == 0x10 && !published && !saving && !lock_owned && notifications == 1 &&
              finalizer_runs == 1 && f.control.strong == 0 && f.control.weak == 0);
    }
    for (bool allocation_failure : {false, true}) {
        Session owner; fixtures.admit(owner); WriteFixture f("GAME-AUTOSAVE1");
        f.throw_assign = allocation_failure; f.fail_assign = !allocation_failure;
        auto future = invoke(owner, f);
        CHECK(!f.creates && f.assigns == 1 && std::strcmp(f.text, "GAME-AUTOSAVE1") == 0);
        // An abandoned provider future owns neither the caller's lock nor data.
        future->vtable->destroy(future, 1);
        CHECK(f.control.strong == 1 && f.released == 1);
    }
    {
        Session owner; fixtures.admit(owner); owner.fail(SessionFault::native_write);
        WriteFixture f("GAME-AUTOSAVE0"); auto future = invoke(owner, f);
        CHECK(!f.creates && !f.assigns); future->vtable->destroy(future, 1);
    }
    active_write = nullptr;
}
}
#include "startup_route_fixture.h"
int main(int argc, char** argv) {
    Fixture fixture;
    if (argc==2 && std::strcmp(argv[1],"--startup-route")==0) {
        for (unsigned defect=0;defect<8;++defect) { Session owner; fixture.prepare(owner); startup_route_fixture::exercise(owner,defect); }
        for (unsigned mode=0;mode<3;++mode) { Session owner; fixture.prepare(owner); exercise_unowned_profile(owner,mode); }
        std::puts("PASS unowned PROFILE reader and serializers refuse before import (3 cases)");
        std::puts("PASS pre-root PROFILE query and first unowned boundary (8 production adapter cases)"); return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--profile") == 0) {
        run_profile_contracts([&fixture] {
            auto owner = std::make_unique<Session>(); fixture.admit(*owner, true); return owner;
        });
        return 0;
    }
    if (argc == 2 && (std::strcmp(argv[1], "--provider") == 0 || std::strcmp(argv[1], "--catalog") == 0 || std::strcmp(argv[1], "--prerequisite") == 0 || std::strcmp(argv[1], "--sdk-write") == 0 || std::strcmp(argv[1], "--readback") == 0)) {
        const auto run = std::strcmp(argv[1], "--provider") == 0 ? run_provider_contracts :
            std::strcmp(argv[1], "--catalog") == 0 ? run_catalog_contracts :
            std::strcmp(argv[1], "--sdk-write") == 0 ? run_sdk_write_contracts :
            std::strcmp(argv[1], "--readback") == 0 ? run_readback_contracts : run_prerequisite_contracts;
        run([&fixture] {
            auto owner = std::make_unique<Session>(); fixture.prepare(*owner);
            owner->install(0x1000, 0x2000, steam_20260818_routes);
            CHECK(owner->startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
            return owner;
        });
        return 0;
    }
    engine::LocalMemory memory;
    write_contracts(fixture, memory);
    preflight_contracts(fixture, memory);
    route_contracts(fixture, memory);
    auxiliary_contracts(fixture, memory);
    const CollectorCalls calls{collect, assign, release};
    {
        Session off; CollectorResult result{}; Context context(off);
        supplied = {"GAME-AUTOSAVE0/game.details"};
        const auto before = assignments;
        CHECK(collect_scoped(off, memory, &context.value, &result, calls) == &result);
        CHECK(result.tag == 0 && assignments == before && !off.routed());
        CHECK(supplied[0] == result.files.entries[0].read_key.data);
        release(&result.files);
    }
    {
        Session partial; fixture.prepare(partial);
        partial.install(0x1000, 0x2000, startup_route | collector_route);
        CHECK(!partial.startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
        CHECK(partial.fault() == SessionFault::incomplete_routes && !partial.routed());
        const auto first=partial.btrace.snapshot().first_failure;
        CHECK(first.stage==BStage::session && std::strcmp(first.predicate,"startup_routes_incomplete")==0);
        CHECK(diagnostic_fact(first,"routes")==3 && diagnostic_fact(first,"required_routes")==required_routes);
        CHECK(diagnostic_fact(first,"qualified")==1 && diagnostic_fact(first,"startup_entered")==0);
        const auto status = partial.inspect();
        CHECK(status.size == 160 && status.abi_version == SC_SAVE_ADMISSION_ABI_VERSION);
        CHECK(status.state == SC_SAVE_SESSION_REJECTED && status.prepared_routes == 3 && status.required_routes == 63);
        CHECK(status.flags == SC_SAVE_SESSION_STARTUP_QUALIFIED);
        partial.install(0x1000, 0x2000, required_routes);
        CHECK(!partial.startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
        CHECK(partial.btrace.snapshot().first_failure.sequence==first.sequence);
    }
    {
        Session missed; fixture.prepare(missed);
        missed.install(0x1000, 0x2000, required_routes); missed.stop_requests();
        const auto first=missed.btrace.snapshot().first_failure;
        CHECK(first.stage==BStage::session && std::strcmp(first.predicate,"requests_stopped_before_startup")==0);
        CHECK(diagnostic_fact(first,"requested_fault")==static_cast<int64_t>(SessionFault::missed_startup));
        CHECK(!missed.startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
        CHECK(missed.fault() == SessionFault::missed_startup && !missed.routed());
        CHECK(missed.btrace.snapshot().first_failure.sequence==first.sequence);
    }
    {
        Session wrong; fixture.prepare(wrong); wrong.install(0x1000, 0x2000, required_routes);
        CHECK(!wrong.startup_enter(0x1000, 0x2001, GetCurrentThreadId()));
        CHECK(wrong.fault() == SessionFault::startup_context && !wrong.routed());
        CHECK(!(wrong.inspect().flags & SC_SAVE_SESSION_STARTUP_QUALIFIED));
        const auto first=wrong.btrace.snapshot().first_failure;
        CHECK(first.stage==BStage::session && std::strcmp(first.predicate,"startup_caller_mismatch")==0);
        CHECK(diagnostic_fact(first,"root_equal")==1 && diagnostic_fact(first,"caller_equal")==0);
    }
    for (bool started : {false, true}) {
        Session late; fixture.prepare(late); late.install(0x1000, 0x2000, required_routes);
        if (started) {
            CHECK(late.startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
            late.startup_leave(false);
            CHECK(late.state() == SessionState::starting && !late.accepts_requests());
        }
        late.unrouted_import();
        const auto first=late.btrace.snapshot().first_failure;
        CHECK(first.stage==BStage::session && std::strcmp(first.predicate,started?
            "unrouted_import_before_provider_binding":"unrouted_import_before_root_observation")==0);
        CHECK(diagnostic_fact(first,"startup_entered")==static_cast<int64_t>(started) &&
            diagnostic_fact(first,"qualified")==static_cast<int64_t>(started));
        CHECK(late.installation.inspect().primary_failure.stage == SC_INSTALL_STARTUP && late.installation.inspect().startup_observation == 2);
        CHECK(!late.bind_provider(late.native_root(), 0x1234, late.ownership_record()));
        CHECK(late.fault() == SessionFault::missed_startup && !late.routed());
        CHECK(late.btrace.snapshot().first_failure.sequence==first.sequence);
    }
    {
        Session pending; fixture.admit(pending);
        CHECK(pending.state() == SessionState::binding && !pending.profile_read_completed());
        CHECK(pending.inspect().state == SC_SAVE_SESSION_BINDING && !pending.accepts_requests());
        pending.startup_leave(true);
        CHECK(pending.state() == SessionState::faulted && pending.routed() && !pending.native_io());
        const auto first=pending.btrace.snapshot().first_failure;
        CHECK(first.stage==BStage::session && std::strcmp(first.predicate,"startup_root_return_abnormal")==0);
        CHECK(diagnostic_fact(first,"abnormal")==1 && diagnostic_fact(first,"routed")==1);
    }
    {
        Session foreign; fixture.prepare(foreign); foreign.install(0x1000, 0x2000, required_routes);
        CHECK(foreign.startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
        CHECK(!foreign.bind_provider(foreign.native_root(), 0x1234, "foreign native ownership"));
        const auto first=foreign.btrace.snapshot().first_failure;
        CHECK(std::strcmp(first.predicate,"provider_binding_ownership_mismatch")==0);
        CHECK(diagnostic_fact(first,"ownership_equal")==0 && diagnostic_fact(first,"thread_equal")==1);
        foreign.startup_leave(false); CHECK(!foreign.routed());
    }
    {
        Session owner;
        const auto descriptor = fixture.prepare(owner);
        owner.install(0x1000, 0x2000, required_routes);
        CHECK(owner.startup_enter(0x1000, 0x2000, GetCurrentThreadId()));
        CHECK(owner.bind_provider(owner.native_root(), 0x1234, owner.ownership_record()));
        owner.startup_leave(false); owner.stop_requests();
        CHECK(owner.routed() && !owner.accepts_requests());
        const auto stopped=owner.btrace.snapshot(); CHECK(!stopped.first_failure.sequence);
        const auto& session_event=stopped.stages[static_cast<size_t>(BStage::session)];
        CHECK(session_event.status==BStatus::succeeded && std::strcmp(session_event.predicate,"session_requests_stopped")==0);
        CHECK(owner.inspect().flags == (SC_SAVE_SESSION_ROUTED | SC_SAVE_SESSION_STARTUP_QUALIFIED));
        storage::Metadata metadata;
        CHECK(storage::inspect(descriptor, metadata).outcome == storage::Outcome::ownership_conflict);
        Context context(owner); CollectorResult result{};
        const auto name = owner.native_root() + "/GAME-AUTOSAVE0/game.details";
        supplied = {name}; const auto before = assignments;
        CHECK(collect_scoped(owner, memory, &context.value, &result, calls) == &result);
        CHECK(result.tag == 0 && assignments == before + 1);
        CHECK(result.files.entries[0].name.data != result.files.entries[0].read_key.data);
        CHECK(name == result.files.entries[0].name.data && name == result.files.entries[0].read_key.data);
        CHECK(std::string(result.files.entries[0].read_key.data) != "GAME-AUTOSAVE0/game.details");
        release(&result.files);
        supplied.clear();
        collect_scoped(owner, memory, &context.value, &result, calls);
        CHECK(result.tag == 0 && result.files.count == 0); release(&result.files);
    }
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        Session owner; fixture.admit(owner); Context context(owner); CollectorResult result{};
        const auto valid = owner.native_root() + "/GAME-AUTOSAVE0/game.details";
        supplied = {valid}; fail_copy = false;
        if (scenario == 0) supplied.push_back("GAME-AUTOSAVE0/game.details");
        if (scenario == 1) supplied.push_back(valid);
        if (scenario == 2) supplied = {owner.native_root() + "/GAME-AUTOSAVE12/game.details"};
        if (scenario == 3) fail_copy = true;
        if (scenario == 4) context.value.provider = 0x5678;
        if (scenario == 5) supplied.push_back(owner.native_root() + "/game-autosave0/game.details");
        const auto before_collect = collections, before_release = releases;
        collect_scoped(owner, memory, &context.value, &result, calls);
        uint32_t code = 0; std::memcpy(&code, &result.files, sizeof(code));
        CHECK(result.tag == 1 && code == 1 && owner.state() == SessionState::faulted);
        CHECK(owner.routed() && !owner.accepts_requests());
        CHECK(releases == before_release + (scenario == 4 ? 0u : 1u));
        CHECK(collections == before_collect + (scenario == 4 ? 0u : 1u));
        const auto count = collections;
        collect_scoped(owner, memory, &context.value, &result, calls);
        CHECK(result.tag == 1 && collections == count); // No vanilla fallback after fault.
    }
    fail_copy = false;
    delete_contracts(fixture, memory);
    std::puts("PASS production session/enumeration/read/write/delete policies with synthetic native layouts");
}
