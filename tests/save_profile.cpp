// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_profile.h"
#include "save_collector.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <new>

using namespace sentinel;
using namespace sentinel::save;
namespace {
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "profile:%d: %s\n", __LINE__, #x); std::abort(); } } while (false)
constexpr uintptr_t image_base = 0x10000000;
constexpr uintptr_t manager = 0x8000;
constexpr uint32_t magic = 0x44357301;
using Bytes = std::vector<uint8_t>;
template<class T, size_t N> void put(std::array<uint8_t, N>& bytes, size_t offset, const T& value) {
    REQUIRE(offset + sizeof(value) <= bytes.size()); std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
void number(Bytes& bytes, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void count(Bytes& bytes, size_t length) { number(bytes, (length << 1) | (length >= 128 ? 1 : 0), length >= 128 ? 4 : 1); }
void text(Bytes& bytes, std::string_view value) { count(bytes, value.size()); bytes.insert(bytes.end(), value.begin(), value.end()); }
struct Frame;
Frame* active = nullptr;
void destroy_value(ProfileValue* value);
struct Json {
    std::map<std::string, ProfileValue> members;
    ProfileValue root{reinterpret_cast<uintptr_t>(&members), 7, 0, {}, 0};
    Json(const std::string& name, int index, bool comment = true) {
        auto copy = static_cast<char*>(std::malloc(name.size() + 1)); REQUIRE(copy);
        std::memcpy(copy, name.c_str(), name.size() + 1);
        members["lastSaveGameName"] = {reinterpret_cast<uintptr_t>(copy), 4, 1, {},
            comment ? reinterpret_cast<uintptr_t>(std::malloc(1)) : 0};
        members["lastUsedGameSlot"] = {static_cast<uint64_t>(index), 1, 0, {}, 0};
        members["magicNumber"] = {magic, 1, 0, {}, 0};
        members["musicVolume"] = {27, 1, 0, {}, 0};
        members["s_volume"] = {reinterpret_cast<uintptr_t>("0.02"), 4, 0, {}, 0};
        members["s_musicvolume"] = {reinterpret_cast<uintptr_t>("1.0"), 4, 0, {}, 0};
    }
    ~Json() { for (auto& entry : members) destroy_value(&entry.second); }
    std::string name() const { return reinterpret_cast<const char*>(members.at("lastSaveGameName").payload); }
    int index() const { return static_cast<int>(members.at("lastUsedGameSlot").payload); }
};
struct Control { uint32_t strong = 2, weak = 2; uintptr_t object, destructor = 0; };
struct ProfileMemory : engine::Memory {
    const char* campaign = "GAME-";
    engine::LocalMemory local;
    engine::ReadResult copy(uintptr_t address, void* out, size_t size) override {
        if (address == image_base + 0x397f4a8 && size == sizeof(campaign)) {
            std::memcpy(out, &campaign, size); return {};
        }
        return local.copy(address, out, size);
    }
};
struct Frame {
    Session& owner;
    ProfileMemory memory;
    std::function<void()> preparation;
    std::array<uint8_t, 0x12400> shell{};
    std::array<uint8_t, 0x88> state{};
    std::array<uint8_t, 0x280> data{};
    std::array<uint8_t, 0x190> file{};
    char data_name[8] = "PROFILE", file_name[12] = "profile.bin";
    uintptr_t file_pointer = reinterpret_cast<uintptr_t>(file.data());
    Control profile_control{2, 2, reinterpret_cast<uintptr_t>(shell.data()), 0};
    Control data_control{2, 2, reinterpret_cast<uintptr_t>(data.data()), 0};
    SaveReference profile_ref{reinterpret_cast<uintptr_t>(&profile_control)}, data_ref{reinterpret_cast<uintptr_t>(&data_control)};
    Bytes bytes;
    std::string vanilla = "AUTOSAVE7", selected;
    int vanilla_index = 2, selected_index = -1;
    unsigned readers = 0, serializers = 0, creates = 0, releases = 0, strings_freed = 0, comments_freed = 0;
    uint64_t read_result = 0;
    uint32_t serialize_result = 0;
    bool allocation_failure = false;
    ProfileCalls calls{};
    explicit Frame(Session& value) : owner(value) {
        put(shell, 0, uintptr_t(image_base + 0x2dbbbe8)); put(shell, 8, reinterpret_cast<uintptr_t>(state.data())); put(shell, 16, manager);
        put(state, 0x18, reinterpret_cast<uintptr_t>(shell.data())); put(state, 0x20, uintptr_t(image_base + 0x1416b50));
        put(state, 0x28, uintptr_t(image_base + 0x1416f10)); put(state, 0x30, uintptr_t(image_base + 0x1416ee0));
        put(state, 0x48, uintptr_t(image_base + 0x141bdd0));
        put(file, 0, uintptr_t(image_base + 0x2a575a8));
        NativeString dn{0, data_name, 7, 8, {}}, fn{0, file_name, 11, 12, {}};
        put(data, 0, dn); put(file, 8, fn);
        put(data, 0x1c0, reinterpret_cast<uintptr_t>(&file_pointer)); put(data, 0x1c8, int32_t(1)); put(data, 0x1cc, int32_t(1));
    }
    void refresh_refs() {
        profile_control.strong = profile_control.weak = data_control.strong = data_control.weak = 2;
        profile_ref.control = reinterpret_cast<uintptr_t>(&profile_control); data_ref.control = reinterpret_cast<uintptr_t>(&data_control);
    }
};
void destroy_value(ProfileValue* value) {
    if (value->type == 4 && value->owned) { std::free(reinterpret_cast<void*>(value->payload)); ++active->strings_freed; }
    if (value->comments) { std::free(reinterpret_cast<void*>(value->comments)); ++active->comments_freed; }
    *value = {};
}
ProfileValue* lookup(ProfileValue* root, const char* name) {
    static ProfileValue shared_null{};
    REQUIRE(root->type == 7);
    auto& members = *reinterpret_cast<std::map<std::string, ProfileValue>*>(root->payload);
    const auto it = members.find(name); return it == members.end() ? &shared_null : &it->second;
}
// Fixture checksum is deliberately independent of the binary scanner. Production
// calls the validated native 141ddab90; this host tests framing and failure flow.
uint64_t checksum(const void* data, uint64_t length) {
    if (active->allocation_failure) throw std::bad_alloc();
    uint32_t hash = 2166136261u;
    auto bytes = static_cast<const uint8_t*>(data);
    for (uint64_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
void release_reference(SaveReference* ref) {
    REQUIRE(ref->control);
    auto control = reinterpret_cast<Control*>(ref->control);
    REQUIRE(control->strong && control->weak);
    --control->strong; --control->weak; ref->control = 0; ++active->releases;
}
SaveReference* retain_reference(SaveReference* out, const SaveReference* from) {
    auto* control = reinterpret_cast<Control*>(from->control); REQUIRE(control && control->strong);
    ++control->strong; ++control->weak; *out = *from; return out;
}
void native_prepare(SaveReference* profile, SaveReference* data, uintptr_t shell, const char*) {
    auto& f = *active; REQUIRE(shell == reinterpret_cast<uintptr_t>(f.shell.data()));
    REQUIRE(!f.owner.routed() || f.data_control.strong == 3);
    f.owner.forget_save_data(reinterpret_cast<uintptr_t>(f.data.data())); // Native Clear detour.
    f.preparation(); // Serializer followed by encoder; its status is ignored.
    release_reference(profile); release_reference(data);
}
void prepare(Frame& f, const std::function<void()>& body) {
    f.refresh_refs(); f.preparation = body;
    prepare_profile_write(f.owner, f.memory, &f.profile_ref, &f.data_ref, reinterpret_cast<uintptr_t>(f.shell.data()),
        "", native_prepare, retain_reference, f.calls);
    REQUIRE(!f.profile_ref.control && !f.data_ref.control && f.data_control.strong == 1 && f.data_control.weak == 1);
    f.preparation = {};
}
uint32_t native_serialize(uintptr_t context, uintptr_t profile, ProfileHolder* holder) {
    auto& f = *active;
    REQUIRE(context == manager && profile == reinterpret_cast<uintptr_t>(f.state.data())); ++f.serializers;
    if (!holder->direction) {
        auto name = lookup(holder->root, "lastSaveGameName"); auto index = lookup(holder->root, "lastUsedGameSlot");
        f.selected = reinterpret_cast<const char*>(name->payload); f.selected_index = static_cast<int>(index->payload);
        REQUIRE(lookup(holder->root, "musicVolume")->payload == 27);
        REQUIRE(lookup(holder->root, "s_volume")->type == 4 &&
            std::strcmp(reinterpret_cast<const char*>(lookup(holder->root, "s_volume")->payload), "0.02") == 0);
        REQUIRE(std::strcmp(reinterpret_cast<const char*>(lookup(holder->root, "s_musicvolume")->payload), "1.0") == 0);
    }
    return f.serialize_result;
}
uint64_t native_read(SaveReference* profile, SaveReference* data) {
    auto& f = *active; ++f.readers;
    uint32_t result = 0;
    {
        Json root(f.vanilla, f.vanilla_index); ProfileHolder holder{0, {}, &root.root};
        const auto comment = root.members.at("lastSaveGameName").comments;
        result = serialize_profile(f.owner, f.memory, manager, reinterpret_cast<uintptr_t>(f.state.data()), &holder, f.calls);
        REQUIRE(root.members.at("lastSaveGameName").comments == comment);
    } // Native reader destroys JSON before its wrapper's borrowed AP string expires.
    release_reference(profile); release_reference(data);
    return result ? 4 : f.read_result;
}
void frame(Frame& f, const std::string& name, int index, unsigned variant = 0) {
    Bytes body{14};
    count(body, variant == 1 || variant == 2 ? 3 : variant == 10 ? 5 : 4);
    text(body, "magicNumber"); body.push_back(3); number(body, variant == 6 ? magic + 256 : magic, 4);
    if (variant != 1) {
        text(body, "lastSaveGameName"); body.push_back(variant == 3 ? 0 : 10);
        if (variant != 3) text(body, name);
    }
    if (variant != 2) {
        text(body, "lastUsedGameSlot"); body.push_back(variant == 4 ? 10 : variant == 19 ? 5 : 1);
        if (variant == 4) text(body, "2"); else number(body, variant == 5 ? 12 : static_cast<uint64_t>(index), 1);
    }
    text(body, "preferences");
    if (variant == 11) { for (unsigned i = 0; i < 66; ++i) { body.push_back(13); count(body, 1); } body.push_back(11); }
    else {
        body.push_back(14); count(body, variant == 20 ? 2 : 1);
        text(body, "fixture"); body.push_back(10); text(body, std::string(130, 'x'));
        if (variant == 20) { text(body, "fixture"); body.push_back(12); }
    }
    if (variant == 10) { text(body, "lastSaveGameName"); body.push_back(10); text(body, name); }
    if (variant == 9) body.pop_back();
    const auto hash = static_cast<uint32_t>(checksum(body.data(), body.size()));
    f.bytes = {0xa9, 0x0d, 0x8d, 0xaa, 0, 0, 0, static_cast<uint8_t>(variant == 7 ? 1 : 2)};
    for (int shift : {24, 16, 8, 0}) f.bytes.push_back(static_cast<uint8_t>((hash ^ (variant == 8 ? 1u : 0u)) >> shift));
    f.bytes.insert(f.bytes.end(), body.begin(), body.end());
    put(f.file, 0x150, uint64_t(f.bytes.size())); put(f.file, 0x158, uint64_t(f.bytes.size()));
    put(f.file, 0x168, reinterpret_cast<uintptr_t>(f.bytes.data()));
}
bool check_payload(Session& owner, engine::Memory& memory, uintptr_t data) { return profile_payload_valid(owner, memory, data, active->calls); }
SaveFuture** create(uintptr_t, SaveFuture** out, uintptr_t, SaveReference* ref) {
    ++active->creates; release_reference(ref); *out = refused_save_future(); return out; // Reached-provider boundary; no persistence receipt.
}
void set_name(uintptr_t, const char*) { REQUIRE(false); }
void publish(Frame& f, bool expected, bool cancel = false) {
    f.refresh_refs(); SaveFuture* future = nullptr;
    WriteCalls calls{create, set_name, release_reference, check_payload};
    REQUIRE(write_scoped(f.owner, f.memory, 0x9876, &future, 0xabcd, &f.data_ref, calls) == &future);
    REQUIRE(f.data_ref.control == 0 && f.data_control.strong == 1);
    if (expected) { REQUIRE(f.creates == 1); future->vtable->destroy(future, 1); return; }
    REQUIRE(f.creates == 0 && future);
    bool task_published = true, lock_held = true;
    unsigned notifications = 0; uint32_t completion_error = 0;
    if (!cancel) {
        SaveResult result{}; future->vtable->poll(future, &result, &task_published);
        REQUIRE(result.state == 0 && result.outcome == 1 && result.value == 1);
        future->vtable->destroy(future, 1);
        REQUIRE(task_published && lock_held && notifications == 0);
        completion_error = 0x10; ++notifications;
    } else future->vtable->destroy(future, 1);
    task_published = false; lock_held = false; --f.data_control.strong; --f.data_control.weak;
    REQUIRE(!task_published && !lock_held && f.data_control.strong == 0);
    REQUIRE(cancel ? notifications == 0 : notifications == 1 && completion_error == 0x10);
}
}
void run_profile_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    for (unsigned test = 0; test < 32; ++test) {
        auto owner = make(); Frame f(*owner); active = &f;
        f.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
        REQUIRE(owner->publish_profile_catalog(0x1234, owner->ownership_record(), {"AUTOSAVE3", "AUTOSAVE10"}, "AUTOSAVE10", 1, false, 0));
        if (test == 26)
            REQUIRE(owner->publish_profile_catalog(0x1234, owner->ownership_record(), {"autosave3", "autosave10"}, "AUTOSAVE10", 1, false, 0));
        if (test == 24) {
            REQUIRE(!owner->publish_profile_catalog(0x1234, owner->ownership_record(), {"AUTOSAVE3", "AUTOSAVE3"}, "AUTOSAVE3", 1, false, 0));
            REQUIRE(!owner->publish_profile_catalog(0x1234, owner->ownership_record(), {"AUTOSAVE3", "autosave3"}, "AUTOSAVE3", 1, false, 0));
            REQUIRE(!owner->publish_profile_catalog(0x1234, owner->ownership_record(), {"AUTOSAVE3", "AUTOSAVE10"}, "AUTOSAVE10", 0, false, 0));
        }
        const unsigned malformed = test <= 11 || test == 19 || test == 20 ? test : 0;
        frame(f, f.vanilla, f.vanilla_index, malformed);
        const auto original_bytes = f.bytes;
        REQUIRE(!owner->accepts_requests()); // Catalog/binding alone is not admission.
        if (test == 12) f.read_result = 4;
        if (test == 18) f.allocation_failure = true;
        if (test == 23) put(f.data, 0x1c8, int32_t(0));
        const auto result = read_profile(*owner, f.memory, &f.profile_ref, &f.data_ref, f.calls);
        const bool refused = (test >= 1 && test <= 12) || test == 18 || test == 19 || test == 20 || test == 23;
        REQUIRE(f.releases == 2 && !f.profile_ref.control && !f.data_ref.control && f.bytes == original_bytes);
        REQUIRE(result == (refused ? 0x10u : 0u));
        // Model the governing native load callback: only zero imports cache;
        // error 4 would reset and 0x100 would re-save. Neither can escape here.
        REQUIRE(result != 4 && result != 0x100);
        if (refused) {
            REQUIRE(!owner->accepts_requests() && owner->routed()); REQUIRE(f.readers == (test == 12 ? 1u : 0u));
            const auto trace = owner->profile_trace();
            REQUIRE(trace.failed_stage != ProfileStage::count);
            if (test == 7 || test == 23) REQUIRE(trace.failed_stage == ProfileStage::framing);
            if (test == 8) REQUIRE(trace.failed_stage == ProfileStage::checksum);
            if (test == 12) REQUIRE(trace.failed_stage == ProfileStage::reader && trace.failure.native_attempted && trace.failure.native_outcome == 4);
            continue;
        }
        REQUIRE(owner->state() == SessionState::admitted && owner->accepts_requests());
        REQUIRE(f.selected == "AUTOSAVE10" && f.selected_index == 1 && f.serializers == 1);
        REQUIRE(f.strings_freed == 1 && f.comments_freed == 1);
        const char* baseline = nullptr; int32_t baseline_index = -1;
        REQUIRE(owner->profile_baseline(0, 0, baseline, baseline_index) && std::strcmp(baseline, "AUTOSAVE7") == 0 && baseline_index == 2);
        if (test >= 30) {
            // A delayed PROFILE callback still owns valid inputs after an
            // unrelated route failed. No native import may run at that point.
            f.refresh_refs();
            const auto fault = test == 30 ? SessionFault::native_write : SessionFault::native_collection;
            owner->fail(fault);
            REQUIRE(read_profile(*owner, f.memory, &f.profile_ref, &f.data_ref, f.calls) == 0x10);
            REQUIRE(f.readers == 1 && f.serializers == 1 && f.bytes == original_bytes);
            REQUIRE(f.releases == 4 && !f.profile_ref.control && !f.data_ref.control);
            REQUIRE(f.profile_control.strong == 1 && f.profile_control.weak == 1 &&
                f.data_control.strong == 1 && f.data_control.weak == 1);
            REQUIRE(owner->state() == SessionState::faulted && owner->fault() == fault && !owner->accepts_requests());
            continue;
        }
        if (test == 21) {
            f.refresh_refs(); frame(f, "AUTOSAVE4", 3);
            REQUIRE(read_profile(*owner, f.memory, &f.profile_ref, &f.data_ref, f.calls) == 0x10 && f.readers == 1);
            REQUIRE(std::strcmp(baseline, "AUTOSAVE7") == 0); continue;
        }
        const bool failure = test == 14 || test == 15 || test == 16 || test == 25 || test == 29;
        if (test == 29) f.memory.campaign = "DLC1-"; // Shell choice must belong to the enumerated campaign.
        prepare(f, [&] {
            Json output(test == 26 ? "autosave3" : "AUTOSAVE3", test == 14 || test == 25 ? 1 : 0);
            output.members.at("musicVolume").payload = 99;
            const auto comments = output.members.at("lastSaveGameName").comments;
            ProfileHolder holder{1, {}, &output.root};
            if (test == 15) f.serialize_result = 3;
            if (test == 16) put(f.state, 0x20, uintptr_t(0));
            const auto status = serialize_profile(*owner, f.memory, manager, reinterpret_cast<uintptr_t>(f.state.data()), &holder, f.calls);
            REQUIRE((status != 0) == failure);
            REQUIRE(output.members.at("musicVolume").payload == 99 && output.members.at("lastSaveGameName").comments == comments);
            REQUIRE(failure ? output.name() == "AUTOSAVE3" : output.name() == "AUTOSAVE7" && output.index() == 2);
            frame(f, test == 17 ? "AUTOSAVE10" : output.name(), output.index());
            // The real writer ignores status and encodes. Exercise that exact
            // ordering: publication must independently refuse its unsafe output.
        });
        if (test == 27 || test == 28) {
            const auto data = reinterpret_cast<uintptr_t>(f.data.data());
            owner->forget_save_data(data); // Clear or physical destructor, before provider transfer.
            ProfileWrite absent; REQUIRE(!owner->take_profile_write(data, absent));
            if (test == 28) { // Reuse that exact address for a different operation.
                prepare(f, [&] {
                    Json output("AUTOSAVE10", 1); ProfileHolder holder{1, {}, &output.root};
                    REQUIRE(serialize_profile(*owner, f.memory, manager, reinterpret_cast<uintptr_t>(f.state.data()), &holder, f.calls) == 0);
                    frame(f, output.name(), output.index());
                });
                REQUIRE(owner->take_profile_write(data, absent) && std::strcmp(absent.choice.name.data(), "AUTOSAVE10") == 0);
            }
        }
        publish(f, !failure && test != 17 && test != 27 && test != 28, test == 25);
        REQUIRE(f.strings_freed == (test == 28 ? 3u : 2u) && f.comments_freed == (test == 28 ? 3u : 2u));
    }
    {
        auto owner = make(); Frame f(*owner); active = &f;
        f.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
        REQUIRE(owner->publish_profile_catalog(0x1234, owner->ownership_record(), {}, "AUTOSAVE0", 0, true, 0));
        f.vanilla = "AUTOSAVE2"; f.vanilla_index = 2;
        // Body comes from independently authenticated/parsed native evidence,
        // with only allowlisted selection fields and synthetic unrelated types.
        const uint8_t body[]{
#include "profile_evidence_body.inc"
        };
        f.bytes = {0xa9,0x0d,0x8d,0xaa,0,0,0,2};
        const auto hash = static_cast<uint32_t>(checksum(body, sizeof(body)));
        for (int shift : {24,16,8,0}) f.bytes.push_back(static_cast<uint8_t>(hash >> shift));
        f.bytes.insert(f.bytes.end(), std::begin(body), std::end(body));
        put(f.file, 0x150, uint64_t(f.bytes.size())); put(f.file, 0x158, uint64_t(f.bytes.size()));
        put(f.file, 0x168, reinterpret_cast<uintptr_t>(f.bytes.data()));
        const auto before = f.bytes;
        REQUIRE(read_profile(*owner, f.memory, &f.profile_ref, &f.data_ref, f.calls) == 0);
        REQUIRE(f.selected == "AUTOSAVE0" && f.selected_index == 0 && before == f.bytes);
        REQUIRE(owner->accepts_requests() && owner->profile_trace().failed_stage == ProfileStage::count);
    }
    active = nullptr;
    std::puts("PASS production PROFILE reader/serializer, minimized native format and non-default settings (33 cases)");
}
void exercise_profile_caller(Session& owner, const std::function<bool(SaveReference&)>& provider, bool malformed) {
    Frame f(owner); active = &f;
    f.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
    frame(f, f.vanilla, f.vanilla_index, malformed ? 8 : 0);
    const auto input = f.bytes;
    REQUIRE(!owner.accepts_requests());
    const bool backend_ok = provider(f.data_ref);
    REQUIRE(!owner.accepts_requests()); // File-job success still precedes native PROFILE import.
    REQUIRE(!f.data_ref.control && f.data_control.strong == 1 && f.data_control.weak == 1);
    if (backend_ok) {
        // Same SaveData/control as the factory argument, now copied by the
        // native parent for its synchronous reader after provider destruction.
        ++f.data_control.strong; ++f.data_control.weak;
        f.data_ref.control = reinterpret_cast<uintptr_t>(&f.data_control);
        ProfileChoice expected{};
        if (owner.routed()) REQUIRE(owner.profile_choice(expected));
        else { std::memcpy(expected.name.data(), f.vanilla.c_str(), f.vanilla.size() + 1); expected.index = f.vanilla_index; }
        const auto result = read_profile(owner, f.memory, &f.profile_ref, &f.data_ref, f.calls);
        REQUIRE(result == (malformed ? 0x10u : 0u) && f.bytes == input);
        if (!malformed) {
            REQUIRE(f.selected == expected.name.data() && f.selected_index == expected.index);
            prepare(f, [&] {
                Json output(f.selected, f.selected_index); ProfileHolder holder{1, {}, &output.root};
                output.members.at("musicVolume").payload = 99;
                REQUIRE(serialize_profile(owner, f.memory, manager, reinterpret_cast<uintptr_t>(f.state.data()), &holder, f.calls) == 0);
                REQUIRE(output.name() == f.vanilla && output.index() == f.vanilla_index && output.members.at("musicVolume").payload == 99);
                frame(f, output.name(), output.index());
            });
            publish(f, true);
            if (owner.routed()) REQUIRE(f.strings_freed == 2 && f.comments_freed == 2);
        } else REQUIRE(f.readers == 0 && !owner.accepts_requests());
    } else {
        REQUIRE(f.readers == 0 && f.serializers == 0 && f.bytes == input);
        release_reference(&f.profile_ref);
    }
    active = nullptr;
}
namespace {
struct ProfileSuccess : SaveFuture { unsigned polls = 0; SaveResult result{0, 0, 1, 0}; };
SaveResult selection_terminal{0, 0, 1, 0};
SaveFuture* destroy_success(SaveFuture* future, uint32_t) { delete static_cast<ProfileSuccess*>(future); return future; }
SaveResult* poll_success(SaveFuture* future, SaveResult* out, void*) {
    auto& model = *static_cast<ProfileSuccess*>(future);
    *out = model.polls++ ? model.result : SaveResult{-1, 0, 0, 0}; return out;
}
const SaveFutureVtable success_vtable{destroy_success, poll_success};
SaveFuture** create_success(uintptr_t, SaveFuture** out, uintptr_t, SaveReference* data) {
    release_reference(data);
    auto* model = new ProfileSuccess; model->result = selection_terminal; model->vtable = &success_vtable; *out = model; return out;
}
}
void exercise_selection_writes(Session& owner, unsigned outcome, const std::function<void(SaveFuture*, SaveFuture*)>& complete) {
    selection_terminal = outcome == 1 ? SaveResult{0, 1, 0x40, 0} : outcome == 2 ? SaveResult{0, 0, 0, 0} :
        outcome == 3 ? SaveResult{1, 0, 0, 0} : SaveResult{0, 0, 1, 0};
    Frame f(owner); active = &f;
    f.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
    frame(f, f.vanilla, f.vanilla_index);
    REQUIRE(read_profile(owner, f.memory, &f.profile_ref, &f.data_ref, f.calls) == 0 && owner.accepts_requests());
    const WriteCalls calls{create_success, set_name, release_reference, check_payload};
    const auto produce = [&](const char* name, int index) {
        prepare(f, [&] {
            Json output(name, index); ProfileHolder holder{1, {}, &output.root};
            REQUIRE(serialize_profile(owner, f.memory, manager, reinterpret_cast<uintptr_t>(f.state.data()), &holder, f.calls) == 0);
            REQUIRE(output.name() == f.vanilla && output.index() == f.vanilla_index);
            frame(f, output.name(), output.index());
        });
        f.refresh_refs(); SaveFuture* future = nullptr;
        write_scoped(owner, f.memory, 0x9876, &future, 0xabcd, &f.data_ref, calls);
        REQUIRE(future && !f.data_ref.control);
        ProfileWrite absent;
        REQUIRE(!owner.take_profile_write(reinterpret_cast<uintptr_t>(f.data.data()), absent));
        return future;
    };
    auto* older = produce("AUTOSAVE3", 0);
    auto* newer = produce("AUTOSAVE10", 1);
    // The source SaveData can clear/die after transfer. Its older future must
    // retain AUTOSAVE3 even though the current shell choice is already 10.
    owner.forget_save_data(reinterpret_cast<uintptr_t>(f.data.data()));
    complete(older, newer);
    older->vtable->destroy(older, 1); newer->vtable->destroy(newer, 1);
    active = nullptr;
}
