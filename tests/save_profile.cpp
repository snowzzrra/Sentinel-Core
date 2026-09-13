// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_profile.h"
#include "save_collector.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
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
SaveFuture** create_success(uintptr_t, SaveFuture**, uintptr_t, SaveReference*);
struct Json {
    std::map<std::string, ProfileValue> members;
    std::unique_ptr<Json> preferences;
    std::unique_ptr<Json> master_level;
    ProfileValue root{reinterpret_cast<uintptr_t>(&members), 7, 0, {}, 0};
    Json() = default;
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
        preferences=std::make_unique<Json>();
        preferences->members["equippedSkin"]={reinterpret_cast<uintptr_t>("fixture-skin-7"),4,0,{},0};
        preferences->members["invertMouse"]={1,5,0,{},0};
        members["preferences"]=preferences->root;
        master_level=std::make_unique<Json>();
        master_level->members["completionInfo"]={}; // Native zero-resource case is null, not an array.
        members["idMasterLevelManager"]=master_level->root;
    }
    ~Json() { for (auto& entry : members) destroy_value(&entry.second); }
    std::string name() const { return reinterpret_cast<const char*>(members.at("lastSaveGameName").payload); }
    int index() const { return static_cast<int>(members.at("lastUsedGameSlot").payload); }
};
struct Control { uint32_t strong = 2, weak = 2; uintptr_t object, destructor = 0; };
struct ProfileMemory : engine::Memory {
    const char* campaign = "GAME-";
    engine::LocalMemory local;
    uintptr_t partial_address = 0;
    std::function<void(uintptr_t, size_t)> after_copy;
    engine::ReadResult copy(uintptr_t address, void* out, size_t size) override {
        if (address == image_base + 0x397f4a8 && size == sizeof(campaign)) {
            std::memcpy(out, &campaign, size); return {};
        }
        if (address == partial_address) {
            REQUIRE(size > 37 && !local.copy(address, out, 37).reason);
            return {SC_REASON_PARTIAL_READ, ERROR_PARTIAL_COPY};
        }
        const auto result = local.copy(address, out, size);
        if (after_copy) after_copy(address, size);
        return result;
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
    Json* native_output = nullptr;
    explicit Frame(Session& value) : owner(value) {
        put(state, 8, uint32_t{23}); // Native user handle, independent of allocation.
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
    REQUIRE(root->type == 7);
    auto& members = *reinterpret_cast<std::map<std::string, ProfileValue>*>(root->payload);
    const auto it = members.find(name); return it == members.end() ?
        reinterpret_cast<ProfileValue*>(image_base+0x4275fb0) : &it->second;
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
void native_prepare(SaveReference* profile, SaveReference* data, uintptr_t native_user, const char*) {
    auto& f = *active; REQUIRE(native_user == 0x24680); // Actual ABI: NOT the shell.
    REQUIRE(reinterpret_cast<Control*>(profile->control)->object == reinterpret_cast<uintptr_t>(f.shell.data()));
    REQUIRE(!f.owner.routed() || f.data_control.strong == 3);
    f.owner.forget_save_data(reinterpret_cast<uintptr_t>(f.data.data())); // Native Clear detour.
    f.preparation(); // Serializer followed by encoder; its status is ignored.
    release_reference(profile); release_reference(data);
}
void prepare(Frame& f, const std::function<void()>& body) {
    f.refresh_refs(); f.preparation = body;
    prepare_profile_write(f.owner, f.memory, &f.profile_ref, &f.data_ref, 0x24680,
        "", native_prepare, retain_reference, f.calls);
    REQUIRE(!f.profile_ref.control && !f.data_ref.control && f.data_control.strong == 1 && f.data_control.weak == 1);
    REQUIRE(f.profile_control.strong==1 && f.profile_control.weak==1);
    f.preparation = {};
}
uint32_t native_serialize(uintptr_t context, uintptr_t profile, ProfileHolder* holder) {
    auto& f = *active;
    REQUIRE(context == manager && profile == reinterpret_cast<uintptr_t>(f.state.data())); ++f.serializers;
    if (!holder->direction) {
        auto name = lookup(holder->root, "lastSaveGameName"); auto index = lookup(holder->root, "lastUsedGameSlot");
        f.selected = reinterpret_cast<const char*>(name->payload); f.selected_index = static_cast<int>(index->payload);
        put(f.shell, 0x1234c, f.selected_index);
        put(f.shell, 0x12350, NativeString{0, f.selected.data(), static_cast<int32_t>(f.selected.size()), 64, {}});
        REQUIRE(lookup(holder->root, "musicVolume")->payload == 27);
        REQUIRE(lookup(holder->root, "s_volume")->type == 4 &&
            std::strcmp(reinterpret_cast<const char*>(lookup(holder->root, "s_volume")->payload), "0.02") == 0);
        REQUIRE(std::strcmp(reinterpret_cast<const char*>(lookup(holder->root, "s_musicvolume")->payload), "1.0") == 0);
    }
    if (holder->direction && f.native_output) {
        // 1414978e0 begins with a null JSON root. The native serializer creates
        // fields from shell state (141417ff0), not fixture-provided output JSON.
        REQUIRE(holder->root->type == 0 && f.native_output->members.empty());
        NativeString name{}; int32_t index = -1;
        std::memcpy(&name, f.shell.data()+0x12350, sizeof(name));
        std::memcpy(&index, f.shell.data()+0x1234c, sizeof(index));
        Json serialized(std::string(name.data, name.length), index);
        serialized.members.at("musicVolume").payload = 99;
        f.native_output->members = std::move(serialized.members);
        f.native_output->preferences=std::move(serialized.preferences);
        f.native_output->master_level=std::move(serialized.master_level);
        *holder->root = {reinterpret_cast<uintptr_t>(&f.native_output->members), 7, 0, {}, 0};
    }
    // 140e5d570: push root -> idMasterLevelManager -> completionInfo, then
    // exactly one pop, even with no resources. The caller encodes the entry
    // document; the returned cursor stays in idMasterLevelManager (type7).
    if (holder->direction) {
        std::vector<ProfileValue*> stack{holder->root};
        holder->root=lookup(holder->root,"idMasterLevelManager");
        stack.push_back(holder->root);
        holder->root=lookup(holder->root,"completionInfo");
        REQUIRE(holder->root->type==0);
        holder->root=stack.back(); stack.pop_back();
        REQUIRE(stack.size()==1 && holder->root->type==7);
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
void frame(Frame& f, const std::string& name, int index, unsigned variant = 0, size_t target_size = 0) {
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
        text(body, "fixture"); body.push_back(10);
        const auto padding = target_size ? target_size - 12 - body.size() - 4 : 130;
        text(body, std::string(padding, 'x'));
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
void encode_value(Bytes& body,const ProfileValue& value) {
    switch(value.type) {
    case 0: body.push_back(0); break;
    case 1: body.push_back(4); number(body,value.payload,8); break;
    case 2: body.push_back(8); number(body,value.payload,8); break;
    case 4: body.push_back(10); text(body,reinterpret_cast<const char*>(value.payload)); break;
    case 5: body.push_back(value.payload?12:11); break;
    case 7: {
        const auto& fields=*reinterpret_cast<const std::map<std::string,ProfileValue>*>(value.payload);
        body.push_back(14); count(body,static_cast<uint32_t>(fields.size()));
        for(const auto& field:fields) { text(body,field.first); encode_value(body,field.second); }
        break;
    }
    default: REQUIRE(false);
    }
}
void encoded_profile(Frame& f,const Json& document) {
    Bytes body; encode_value(body,document.root);
    f.bytes={0xa9,0x0d,0x8d,0xaa,0,0,0,2};
    const auto hash=static_cast<uint32_t>(checksum(body.data(),body.size()));
    for(int shift:{24,16,8,0}) f.bytes.push_back(static_cast<uint8_t>(hash>>shift));
    f.bytes.insert(f.bytes.end(),body.begin(),body.end());
    put(f.file,0x150,uint64_t(f.bytes.size())); put(f.file,0x158,uint64_t(f.bytes.size()));
    put(f.file,0x168,reinterpret_cast<uintptr_t>(f.bytes.data()));
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
void exercise_unowned_profile(Session& owner,unsigned mode) {
    Frame f(owner); active=&f;
    f.calls={native_read,native_serialize,lookup,destroy_value,checksum,release_reference,image_base};
    frame(f,f.vanilla,f.vanilla_index);
    if (!mode) {
        REQUIRE(read_profile(owner,f.memory,&f.profile_ref,&f.data_ref,f.calls)==0x10);
        REQUIRE(f.releases==2);
    } else {
        Json json(f.vanilla,f.vanilla_index); ProfileHolder holder{static_cast<uint8_t>(mode-1),{},&json.root};
        REQUIRE(serialize_profile(owner,f.memory,manager,reinterpret_cast<uintptr_t>(f.state.data()),&holder,f.calls)==3);
        REQUIRE(json.name()==f.vanilla && json.index()==f.vanilla_index);
    }
    REQUIRE(f.readers==0 && f.serializers==0 && !owner.native_io() && owner.fault()==SessionFault::missed_startup);
    REQUIRE(!owner.profile_trace().ownership.baseline_ready && owner.profile_trace().request==0);
    active=nullptr;
}
void run_profile_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    // A live Session owns the vanilla selection, not a PROFILE allocation.
    for (unsigned defect = 0; defect < 14; ++defect) {
        auto owner = make(); Frame a(*owner), b(*owner); active = &a;
        a.calls = b.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
        REQUIRE(owner->publish_profile_catalog(0x1234, owner->ownership_record(), {}, "AUTOSAVE0", 0, true, 0));
        frame(a, a.vanilla, a.vanilla_index);
        if (defect != 7) REQUIRE(read_profile(*owner, a.memory, &a.profile_ref, &a.data_ref, a.calls) == 0);
        Frame& later = defect == 6 ? a : b; active = &later; // Reuse address with another user.
        uintptr_t later_manager = manager;
        if (defect == 1) { later_manager += 8; put(later.shell, 16, later_manager); }
        if (defect == 2 || defect == 6) put(later.state, 8, uint32_t{24});
        if (defect == 3) put(later.state, 0x48, uintptr_t{0});
        if (defect == 4) put(later.shell, 0, uintptr_t{0});
        if (defect == 5) put(later.shell, 8, reinterpret_cast<uintptr_t>(a.state.data()));
        if (defect == 8) owner->fail(SessionFault::native_collection);
        if (defect == 9) REQUIRE(!owner->provider_operation(0x9999, 0x1111));
        if (defect == 10) later.memory.campaign = "DLC1-";
        if (defect == 11) REQUIRE(!owner->provider_operation(0x9876, 0x2222));
        if (defect == 12) owner->provider_reset(0x9000);
        if (defect == 13) put(later.state, 8, UINT32_MAX);
        prepare(later, [&] {
            Json output("AUTOSAVE0", 0); ProfileHolder holder{1, {}, &output.root};
            const auto result = serialize_profile(*owner, later.memory, later_manager, reinterpret_cast<uintptr_t>(later.state.data()), &holder, later.calls);
            REQUIRE((result == 0) == (defect == 0));
            REQUIRE(output.members.at("musicVolume").payload == 27);
            REQUIRE(defect ? output.name() == "AUTOSAVE0" : output.name() == "AUTOSAVE7" && output.index() == 2);
            frame(later, output.name(), output.index());
        });
        REQUIRE(owner->native_io() == (defect == 0));
        if (!defect) {
            const auto trace = owner->profile_trace().ownership;
            REQUIRE(trace.stable_owner && trace.initial.profile != trace.latest.profile && trace.initial.shell != trace.latest.shell);
        }
    }
    for (size_t size : {size_t(262143), size_t(262144), size_t(262145), size_t(560873), size_t(0xfa000 - 1), size_t(0xfa000)}) {
        auto owner = make(); Frame f(*owner); active = &f;
        f.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
        REQUIRE(owner->publish_profile_catalog(0x1234, owner->ownership_record(), {}, "AUTOSAVE0", 0, true, 0));
        frame(f, f.vanilla, f.vanilla_index, 0, size);
        REQUIRE(f.bytes.size() == size);
        const auto before = f.bytes;
        // The complete production adapter uses LocalMemory for all actual addresses.
        engine::LocalMemory memory;
        const auto result = read_profile(*owner, memory, &f.profile_ref, &f.data_ref, f.calls);
        std::printf("PROFILE LocalMemory bytes=%zu result=%llu predicate=%s readers=%u releases=%u\n",
            f.bytes.size(), result, owner->profile_trace().failure.predicate, f.readers, f.releases);
        std::fflush(stdout);
        REQUIRE(result == 0 && owner->accepts_requests() && f.selected == "AUTOSAVE0");
        REQUIRE(f.bytes == before && f.releases == 2 && f.strings_freed == 1 && f.comments_freed == 1);
        prepare(f, [&] {
            Json output("AUTOSAVE0", 0); ProfileHolder holder{1, {}, &output.root};
            output.members.at("musicVolume").payload = 99;
            REQUIRE(serialize_profile(*owner, f.memory, manager, reinterpret_cast<uintptr_t>(f.state.data()), &holder, f.calls) == 0);
            REQUIRE(output.name() == f.vanilla && output.index() == f.vanilla_index && output.members.at("musicVolume").payload == 99);
            frame(f, output.name(), output.index(), 0, size);
        });
        publish(f, true);
        REQUIRE(owner->profile_trace().steps[static_cast<size_t>(ProfileStage::output_validation)].status == ProfileStatus::succeeded);
    }
    for (bool output : {false, true}) for (unsigned defect = 0; defect < 6; ++defect) {
        auto owner = make(); Frame f(*owner); active = &f;
        f.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
        REQUIRE(owner->publish_profile_catalog(0x1234, owner->ownership_record(), {}, "AUTOSAVE0", 0, true, 0));
        frame(f, f.vanilla, f.vanilla_index, 0, 560873);
        if (output) {
            REQUIRE(read_profile(*owner, f.memory, &f.profile_ref, &f.data_ref, f.calls) == 0);
            f.refresh_refs();
        }
        void* pages = nullptr;
        const auto corrupt = [&] {
            const uintptr_t buffer = reinterpret_cast<uintptr_t>(f.bytes.data());
            if (defect == 0) {
                pages = VirtualAlloc(nullptr, f.bytes.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); REQUIRE(pages);
                std::memcpy(pages, f.bytes.data(), f.bytes.size()); DWORD old = 0;
                REQUIRE(VirtualProtect(static_cast<uint8_t*>(pages) + engine::Memory::maximum_copy, 4096, PAGE_NOACCESS, &old));
                put(f.file, 0x168, reinterpret_cast<uintptr_t>(pages));
            }
            if (defect == 1) f.memory.partial_address = buffer + engine::Memory::maximum_copy;
            if (defect == 2) { put(f.file, 0x150, uint64_t(0xfa001)); put(f.file, 0x158, uint64_t(0xfa001)); }
            if (defect == 3) put(f.file, 0x158, uint64_t(f.bytes.size() - 1));
            if (defect == 4) put(f.file, 0x168, UINTPTR_MAX - 15);
            if (defect == 5) f.memory.after_copy = [&, buffer](uintptr_t address, size_t) {
                if (address == buffer) put(f.file, 0x158, uint64_t(f.bytes.size() + 1));
            };
        };
        const auto before = f.bytes;
        if (output) {
            prepare(f, [&] {
                Json root("AUTOSAVE0", 0); ProfileHolder holder{1, {}, &root.root};
                REQUIRE(serialize_profile(*owner, f.memory, manager, reinterpret_cast<uintptr_t>(f.state.data()), &holder, f.calls) == 0);
                corrupt();
            });
            publish(f, false);
        } else {
            corrupt();
            const auto result = read_profile(*owner, f.memory, &f.profile_ref, &f.data_ref, f.calls);
            std::printf("PROFILE acquisition defect=%u result=%llu predicate=%s\n", defect, result, owner->profile_trace().failure.predicate);
            std::fflush(stdout);
            REQUIRE(result == 0x10);
            REQUIRE(f.readers == 0 && f.serializers == 0 && f.releases == 2);
        }
        REQUIRE(f.bytes == before && !owner->accepts_requests() && owner->fault() == SessionFault::native_profile && !f.creates);
        const auto trace = owner->profile_trace();
        REQUIRE(trace.failed_stage == (output ? ProfileStage::output_validation : ProfileStage::framing));
        if (defect <= 1) {
            REQUIRE(trace.failure.read.requested == 560873 && trace.failure.read.offset == engine::Memory::maximum_copy &&
                trace.failure.read.size == engine::Memory::maximum_copy);
            REQUIRE(trace.failure.read.reason == uint32_t(defect ? SC_REASON_PARTIAL_READ : SC_REASON_READ_FAILED));
            REQUIRE(trace.failure.read.error == uint32_t(defect ? ERROR_PARTIAL_COPY : ERROR_NOACCESS));
        }
        if (defect == 5) REQUIRE(std::strcmp(trace.failure.predicate, "profile_source_changed") == 0);
        if (pages) REQUIRE(VirtualFree(pages, 0, MEM_RELEASE));
    }
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
        REQUIRE(owner->profile_output_baseline(baseline, baseline_index) && std::strcmp(baseline, "AUTOSAVE7") == 0 && baseline_index == 2);
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
        if (failure) {
            const auto trace = owner->profile_trace();
            REQUIRE(trace.failed_stage == ProfileStage::output_validation);
            const auto expected = test == 15 ? "native_output_serializer_result" :
                test == 16 ? "serializer_callback_identity" : test == 29 ? "profile_write_choice_or_campaign_mismatch" :
                "serialized_selection_not_in_owned_catalog";
            REQUIRE(std::strcmp(trace.failure.predicate, expected) == 0);
            const auto causal=owner->btrace.snapshot().first_failure;
            const auto cause=test==15 ? "native_output_serializer_result" : test==16 ? "profile_major_callback" :
                test==29 ? "profile_choice_capture" : "choice_pair_not_catalog_or_prospective";
            REQUIRE(causal.sequence && causal.status==BStatus::refused && std::strcmp(causal.predicate,cause)==0);
            owner->btrace.record(BStage::session,BStatus::refused,"fixture_downstream_refusal");
            REQUIRE(owner->btrace.snapshot().first_failure.sequence==causal.sequence);
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
    for(unsigned defect=0;defect<6;++defect) {
        auto owner=make(); Frame f(*owner); active=&f;
        f.calls={native_read,native_serialize,lookup,destroy_value,checksum,release_reference,image_base};
        REQUIRE(owner->publish_profile_catalog(0x1234,owner->ownership_record(),{},"AUTOSAVE0",0,true,0));
        frame(f,f.vanilla,f.vanilla_index);
        REQUIRE(read_profile(*owner,f.memory,&f.profile_ref,&f.data_ref,f.calls)==0);
        prepare(f,[&] {
            Json output("AUTOSAVE0",0);
            // A valid pair in the final cursor must never repair a broken entry
            // document. Native encoding still uses output.root after failure.
            auto& child=output.master_level->members;
            child["lastSaveGameName"]={reinterpret_cast<uintptr_t>("AUTOSAVE0"),4,0,{},0};
            child["lastUsedGameSlot"]={0,1,0,{},0}; child["magicNumber"]={magic,1,0,{},0};
            if(defect>=1 && defect<=3) {
                const char* key=defect==1?"lastSaveGameName":defect==2?"lastUsedGameSlot":"magicNumber";
                destroy_value(&output.members.at(key)); output.members.erase(key);
            } else if(defect==4) output.members.at("lastUsedGameSlot").type=2;
            else if(defect==5) output.members.at("magicNumber").payload^=0x100;
            Bytes settings_before; encode_value(settings_before,output.members.at("preferences"));
            ProfileHolder holder{1,{},&output.root};
            const auto result=serialize_profile(*owner,f.memory,manager,reinterpret_cast<uintptr_t>(f.state.data()),&holder,f.calls);
            REQUIRE((result==0)==(defect==0) && holder.root==&output.members.at("idMasterLevelManager"));
            Bytes settings_after; encode_value(settings_after,output.members.at("preferences"));
            REQUIRE(settings_before==settings_after);
            if(!defect) REQUIRE(output.name()==f.vanilla && output.index()==f.vanilla_index);
            else {
                const auto failure=owner->btrace.snapshot().first_failure;
                REQUIRE(failure.stage==BStage::profile_output && failure.status==BStatus::refused);
                const char* expected=defect<=3?"serialized_selection_member_missing":
                    defect==4?"serialized_slot_type_or_read":"serialized_magic_type_or_value";
                REQUIRE(std::strcmp(failure.predicate,expected)==0);
                bool cursor_fact=false,missing_fact=false;
                const char* missing=defect==1?"name_present":defect==2?"index_present":"magic_present";
                for(const auto& fact:failure.facts) if(fact.key) {
                    if(std::strcmp(fact.key,"cursor_state")==0) cursor_fact=fact.value==0;
                    if(std::strcmp(fact.key,missing)==0) missing_fact=fact.value==0;
                }
                REQUIRE(cursor_fact && (defect>3 || missing_fact));
            }
            // Like 1414978e0, encode after the callback even when it failed.
            encoded_profile(f,output);
        });
        publish(f,defect==0); // Refused structured output cannot reach the backend.
    }
    active = nullptr;
    std::puts("PASS production PROFILE reader/serializer and LocalMemory bounded acquisition (51 payload + 14 owner + 6 output document cases; synthetic host, not DOOM)");
}
void exercise_campaign_profile(Session& owner, const std::function<void(const std::function<void()>&)>& campaign) {
    Frame initial(owner), later(owner); active = &initial;
    initial.calls = later.calls = {native_read, native_serialize, lookup, destroy_value, checksum, release_reference, image_base};
    frame(initial, initial.vanilla, initial.vanilla_index);
    REQUIRE(read_profile(owner, initial.memory, &initial.profile_ref, &initial.data_ref, initial.calls) == 0);
    REQUIRE(owner.accepts_requests() && initial.selected == "AUTOSAVE0");
    later.selected = initial.selected;
    put(later.shell, 0x1234c, initial.selected_index);
    put(later.shell, 0x12350, NativeString{0, later.selected.data(), static_cast<int32_t>(later.selected.size()), 64, {}});
    active = &later;
    campaign([&] {
        REQUIRE(owner.native_io()); // PROFILE also saves before map-ready and during menu cleanup.
        const auto session_before=owner.btrace.snapshot().stages[static_cast<size_t>(BStage::session)].sequence;
        prepare(later, [&] {
            Json output; output.root = {}; later.native_output = &output;
            ProfileHolder holder{1, {}, &output.root};
            REQUIRE(serialize_profile(owner, later.memory, manager, reinterpret_cast<uintptr_t>(later.state.data()), &holder, later.calls) == 0);
            REQUIRE(holder.root==&output.members.at("idMasterLevelManager") && holder.root!=&output.root);
            later.native_output = nullptr;
            REQUIRE(output.name() == "AUTOSAVE7" && output.index() == 2 && output.members.at("musicVolume").payload == 99);
            REQUIRE(std::strcmp(reinterpret_cast<const char*>(output.members.at("s_volume").payload), "0.02") == 0);
            REQUIRE(std::strcmp(reinterpret_cast<const char*>(output.preferences->members.at("equippedSkin").payload),"fixture-skin-7")==0);
            REQUIRE(output.preferences->members.at("invertMouse").payload==1);
            encoded_profile(later,output); // Encode the complete entry tree, never the final cursor.
        });
        REQUIRE(owner.btrace.snapshot().stages[static_cast<size_t>(BStage::session)].sequence==session_before);
        const auto trace = owner.profile_trace().ownership;
        REQUIRE(trace.stable_owner && trace.initial.profile != trace.latest.profile && trace.initial.user == trace.latest.user);
        later.refresh_refs(); SaveFuture* future = nullptr; uintptr_t provider = 0;
        REQUIRE(owner.native_provider(provider));
        const WriteCalls calls{[](uintptr_t remote, SaveFuture** out, uintptr_t identity, SaveReference* data) {
            // Native encoder/Steam transport substitute; production scoped
            // future and selection completion still own exact correlation.
            using Put = bool(*)(uintptr_t, const char*, const void*, int32_t);
            const auto table = *reinterpret_cast<uintptr_t**>(remote);
            REQUIRE(reinterpret_cast<Put>(table[0])(remote, "PROFILE/profile.bin", active->bytes.data(), static_cast<int32_t>(active->bytes.size())));
            return create_success(remote, out, identity, data);
        }, set_name, release_reference, check_payload};
        write_scoped(owner, later.memory, provider, &future, 0xabcd, &later.data_ref, calls);
        REQUIRE(future && !later.data_ref.control);
        SaveResult result{}; future->vtable->poll(future, &result, nullptr); REQUIRE(result.state == -1);
        future->vtable->poll(future, &result, nullptr);
        if (result.state || result.outcome || result.value!=1)
            std::fprintf(stderr,"PROFILE completion first refusal: %s\n",owner.btrace.snapshot().first_failure.predicate);
        REQUIRE(!result.state && !result.outcome && result.value == 1);
        future->vtable->destroy(future, 1);
        REQUIRE(later.data_control.strong==1 && later.data_control.weak==1);
        REQUIRE(owner.native_io());
    });
    active = nullptr;
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
