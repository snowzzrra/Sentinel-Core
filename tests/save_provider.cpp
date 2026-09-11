// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Actual policy and interface-014 call layout, synthetic file storage only.
#include "save_provider.h"
#include "save_catalog.h"
#include "save_profile_prerequisite.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <thread>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "FAIL save_provider:%d %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::save;
void exercise_profile_caller(Session&, const std::function<bool(SaveReference&)>&, bool);
void exercise_selection_writes(Session&, unsigned, const std::function<void(SaveFuture*, SaveFuture*)>&);
namespace {
constexpr uintptr_t image = 0x10000000;
struct Remote {
    uintptr_t* table;
    std::vector<std::pair<std::string, std::string>> files;
    unsigned reads = 0, queries = 0, writes = 0;
    bool short_read = false, mutate = false, bad_alloc = false, wrong_size = false, bad_count = false;
    bool fail_write = false, partial_write = false, add_foreign = false, discard_write = false, replace_selection = false;
};
Remote& get(uintptr_t self) { return *reinterpret_cast<Remote*>(self); }
auto find(Remote& remote, const char* key) {
    return std::find_if(remote.files.begin(), remote.files.end(), [key](const auto& item) { return steam_name_equal(item.first, key); });
}
int32_t read(uintptr_t self, const char* key, void* out, int32_t capacity) {
    auto& remote = get(self); ++remote.reads;
    const auto file = find(remote, key); REQUIRE(file != remote.files.end());
    if (remote.bad_alloc) throw std::bad_alloc();
    const auto length = std::min(static_cast<size_t>(capacity), file->second.size());
    std::memcpy(out, file->second.data(), length);
    if (remote.mutate && remote.reads == 2) static_cast<char*>(out)[0] = '?';
    return static_cast<int32_t>(length) - (remote.short_read ? 1 : 0);
}
bool write(uintptr_t self, const char* key, const void* bytes, int32_t length) {
    auto& remote = get(self); ++remote.writes;
    auto existing = find(remote, key);
    REQUIRE(existing == remote.files.end() || (remote.replace_selection && std::string(key).find("/sentinel-selection-") != std::string::npos));
    if (remote.fail_write && !remote.partial_write) return false;
    const std::string value(static_cast<const char*>(bytes), static_cast<size_t>(length));
    if (!remote.discard_write) {
        if (existing == remote.files.end()) remote.files.emplace_back(key, remote.partial_write ? value.substr(0, 10) : value);
        else existing->second = remote.partial_write ? value.substr(0, 10) : value;
    }
    if (remote.add_foreign) {
        const auto slash = std::string(key).find('/');
        remote.files.emplace_back(std::string(key).substr(0, slash + 1) + "sentinel-owner-foreign.txt", "foreign");
    }
    return !remote.fail_write;
}
bool exists(uintptr_t self, const char* key) { auto& remote = get(self); ++remote.queries; return find(remote, key) != remote.files.end(); }
int32_t size(uintptr_t self, const char* key) {
    auto& remote = get(self); const auto file = find(remote, key);
    return file == remote.files.end() ? 0 : static_cast<int32_t>(file->second.size()) + (remote.wrong_size ? 1 : 0);
}
int32_t count(uintptr_t self) { return get(self).bad_count ? 100001 : static_cast<int32_t>(get(self).files.size()); }
const char* name(uintptr_t self, int32_t index, int32_t* length) {
    auto& file = get(self).files.at(static_cast<size_t>(index));
    *length = static_cast<int32_t>(file.second.size()); return file.first.c_str();
}
uintptr_t remote_pointer = 0;
unsigned context_calls = 0;
uintptr_t context(uintptr_t record) {
    REQUIRE(record == image + 0x397fb88); ++context_calls;
    return reinterpret_cast<uintptr_t>(&remote_pointer);
}
struct Memory final : engine::Memory {
    engine::LocalMemory local;
    uintptr_t manager = 0;
    const char* campaign = "GAME-";
    engine::ReadResult copy(uintptr_t address, void* out, size_t length) override {
        if (address == 0x1000 + 0x9b38 && length == sizeof(manager)) {
            std::memcpy(out, &manager, length); return {};
        }
        if (address == image + 0x397f4a8 && length == sizeof(campaign)) {
            std::memcpy(out, &campaign, length); return {};
        }
        return local.copy(address, out, length);
    }
};
unsigned query_native_calls = 0, query_releases = 0;
std::string queried_native_name;
void release_query(SaveReference* identity) {
    auto* counts = reinterpret_cast<uint32_t*>(identity->control);
    REQUIRE(counts && counts[0] == 2 && counts[1] == 2);
    --counts[0]; --counts[1]; identity->control = 0; ++query_releases;
}
SaveFuture** original_query(SaveFuture** out, SaveReference* identity, const char* name) {
    ++query_native_calls; queried_native_name = name;
    release_query(identity); *out = refused_save_future(); return out;
}
}
void run_provider_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    std::array<uintptr_t, 20> table{};
    table[0] = reinterpret_cast<uintptr_t>(write);
    table[1] = reinterpret_cast<uintptr_t>(read); table[13] = reinterpret_cast<uintptr_t>(exists);
    table[15] = reinterpret_cast<uintptr_t>(size); table[18] = reinterpret_cast<uintptr_t>(count);
    table[19] = reinterpret_cast<uintptr_t>(name);
    const ProviderCalls calls{image, context};
    for (unsigned test = 0; test < 9; ++test) {
        auto owner = make(); Remote remote{table.data()}; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
        uintptr_t provider = image + 0x2e90658;
        std::array<uintptr_t, 3> control{0x100000001, reinterpret_cast<uintptr_t>(&provider), 0};
        uintptr_t manager = reinterpret_cast<uintptr_t>(control.data());
        Memory memory; memory.manager = reinterpret_cast<uintptr_t>(&manager);
        REQUIRE(provider_initialized(*owner, memory, memory.manager, calls));
        REQUIRE(owner->provider_operation(reinterpret_cast<uintptr_t>(&provider), 0x123456));
        owner->startup_leave(false);
        REQUIRE(provider_initialized(*owner, memory, memory.manager, calls)); // Ordinary lazy getter after startup.
        REQUIRE(owner->provider_operation(reinterpret_cast<uintptr_t>(&provider), 0x123456));
        const auto files = remote.files;
        if (test == 1) remote_pointer = 0x9999;
        if (test == 2) provider = image + 0x2e90a60; // Native fallback stub, never an AP migration.
        if (test == 3) control[1] = 0;
        if (test == 4) owner->provider_reset(memory.manager);
        if (test == 5) REQUIRE(!owner->provider_operation(reinterpret_cast<uintptr_t>(&provider), 0x654321));
        if (test == 6) REQUIRE(!owner->provider_operation(0xdead, 0x123456));
        if (test == 7) owner->provider_reset(0xbeef); // Unrelated manager.
        if (test == 8) {
            std::thread worker([&] { REQUIRE(provider_initialized(*owner, memory, memory.manager, calls)); });
            worker.join(); // Continuity does not require replaying qualified startup.
        }
        const bool valid = test == 0 || test >= 7;
        REQUIRE(provider_initialized(*owner, memory, memory.manager, calls) == valid);
        REQUIRE(owner->routed() && owner->native_io() == valid && remote.files == files);
        if (!valid) {
            uintptr_t unavailable = 0;
            REQUIRE(owner->fault() == SessionFault::provider_identity && !owner->native_provider(unavailable));
            provider = image + 0x2e90658; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
            REQUIRE(!provider_initialized(*owner, memory, memory.manager, calls)); // No resurrection/unrouting.
        }
    }
    for (unsigned test = 0; test < 35; ++test) {
        auto owner = make();
        Remote remote{table.data()}; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
        const auto marker = owner->native_root() + "/sentinel-owner-" + owner->namespace_id() + ".txt";
        remote.files = {{"PROFILE/profile.bin", "unrelated vanilla"}, {marker, owner->ownership_record()}};
        uintptr_t provider = image + 0x2e90658;
        std::array<uintptr_t, 3> control{0x100000001, reinterpret_cast<uintptr_t>(&provider), 0};
        uintptr_t manager = reinterpret_cast<uintptr_t>(control.data());
        Memory memory; memory.manager = reinterpret_cast<uintptr_t>(&manager);
        if (test == 1) remote.files.pop_back(); // Empty remote root may be prepared, then read back.
        if (test == 2) remote.files.back().second.back() = '?';
        if (test == 3) remote.files.emplace_back(owner->native_root() + "/sentinel-owner-foreign.txt", "foreign");
        if (test == 4) remote.short_read = true;
        if (test == 5) remote.wrong_size = true;
        if (test == 6) remote.mutate = true;
        if (test == 7) remote.bad_count = true;
        if (test == 8) remote.bad_alloc = true;
        if (test == 9) provider = image + 0x2e88198; // Unsupported local provider.
        if (test == 10) remote_pointer = 0;
        if (test == 11) remote.files.emplace_back(marker, owner->ownership_record());
        if (test == 12) for (char& c : remote.files.back().first) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
        if (test >= 15 && test <= 21) remote.files.pop_back();
        if (test == 15) remote.files.emplace_back(owner->native_root() + "/GAME-AUTOSAVE0/game.details", "orphaned native payload");
        if (test == 16 || test == 17) remote.fail_write = true;
        if (test == 17) remote.partial_write = true;
        if (test == 18) remote.short_read = true;
        if (test == 19) remote.add_foreign = true;
        if (test == 20) remote.files.clear(); // Entire RemoteStorage empty.
        if (test == 21) remote.discard_write = true;
        if (test >= 22 && test <= 27) {
            remote.files.emplace_back(owner->native_root() + "/game-autosave10/game.details", "native payload");
            std::string record = "sentinel-native-selection-v1\nnamespace_id=" + owner->namespace_id() + "\ncampaign=GAME-\nname=AUTOSAVE10\n";
            if (test == 23) record[record.find(owner->namespace_id())] = '?';
            if (test == 24) record.replace(record.find("AUTOSAVE10"), 10, "AUTOSAVE01");
            if (test == 25) remote.files.emplace_back(owner->native_root() + "/GAME-AUTOSAVE10/game.details", "alias");
            if (test == 26) record.replace(0, 1, "?");
            if (test == 27) remote.files.emplace_back(owner->native_root() + "/GAME-AUTOSAVE12/game.details", "unsupported slot");
            remote.files.emplace_back(owner->native_root() + "/sentinel-selection-GAME.txt", std::move(record));
        }
        if (test == 28 || test == 29 || test == 34)
            remote.files.emplace_back(owner->native_root() + "/GAME-AUTOSAVE0/SlotFile", "native payload");
        if (test == 29 || test == 32 || test == 34)
            remote.files.emplace_back(owner->native_root() + "/game-autosave0/game.details", "native metadata");
        if (test == 30) remote.files.emplace_back(owner->native_root() + "/DLC1-AUTOSAVE0/SlotFile", "partial");
        if (test == 31) remote.files.emplace_back(owner->native_root() + "/GAME-AUTOSAVE01/SlotFile", "invalid slot");
        if (test == 32) remote.files.emplace_back(owner->native_root() + "/DLC2-AUTOSAVE0/SlotFile", "partial");
        if (test == 33) remote.files.emplace_back("GAME-AUTOSAVE0/SlotFile", "unrelated vanilla");
        if (test == 34) remote.files.emplace_back(owner->native_root() + "/game-autosave0/auxiliary/extra.bin", "auxiliary payload");
        const auto before = remote.files;
        const auto before_context = context_calls;
        if (test == 13) {
            REQUIRE(!provider_initialized(*owner, memory, 0x7777, calls));
            REQUIRE(owner->state() == SessionState::starting && context_calls == before_context);
        }
        if (test == 14) {
            std::thread other([&] {
                REQUIRE(!provider_initialized(*owner, memory, memory.manager, calls));
            }); other.join();
            REQUIRE(owner->state() == SessionState::starting && context_calls == before_context);
        }
        const bool accepted = provider_initialized(*owner, memory, memory.manager, calls);
        REQUIRE(accepted == (test == 0 || test == 1 || (test >= 12 && test <= 14) || test == 20 || test == 22 ||
            test == 29 || test == 33 || test == 34));
        const bool creation = test == 1 || (test >= 16 && test <= 21);
        REQUIRE(remote.writes == (creation ? 1u : 0u));
        if (!creation || test == 16 || test == 21) REQUIRE(remote.files == before);
        for (const auto& file : before) REQUIRE(std::find(remote.files.begin(), remote.files.end(), file) != remote.files.end());
        if (accepted) {
            REQUIRE(owner->state() == SessionState::binding && !owner->accepts_requests());
            REQUIRE(remote.reads == (test == 22 ? 3u : 2u) && owner->collecting(remote_pointer, owner->native_root()));
            NativeCampaignCatalog source; uintptr_t source_provider = 0;
            REQUIRE(read_native_catalog(*owner, memory, "GAME-", source, source_provider) && source_provider == remote_pointer);
            if (test == 22) REQUIRE(source.slots == std::vector<std::string>{"AUTOSAVE10"} && source.selected == "AUTOSAVE10");
            else if (test == 29 || test == 34) REQUIRE(source.slots == std::vector<std::string>{"AUTOSAVE0"} && source.selected.empty());
            else REQUIRE(source.slots.empty() && source.selected.empty());
            owner->startup_leave(false); owner->stop_requests();
            REQUIRE(owner->routed() && !owner->accepts_requests());
            const std::wstring mutex = L"Local\\SentinelCore-Steam-782330-" +
                std::wstring(owner->native_root().begin(), owner->native_root().end());
            std::thread other([&] {
                HANDLE lock = OpenMutexW(SYNCHRONIZE, FALSE, mutex.c_str()); REQUIRE(lock);
                REQUIRE(WaitForSingleObject(lock, 0) == WAIT_TIMEOUT); CloseHandle(lock);
            }); other.join(); // Stop retains the actual-root mutex as well as the offline lease.
        } else {
            REQUIRE(!owner->routed() && owner->fault() == SessionFault::provider_identity);
        }
    }
    for (unsigned test = 0; test < 8; ++test) {
        auto owner = make(); Remote remote{table.data()}; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
        uintptr_t provider = image + 0x2e90658;
        std::array<uintptr_t, 3> control{0x100000001, reinterpret_cast<uintptr_t>(&provider), 0};
        uintptr_t manager = reinterpret_cast<uintptr_t>(control.data());
        const auto manager_address = reinterpret_cast<uintptr_t>(&manager);
        Memory memory; memory.manager = 0; const auto before = context_calls;
        REQUIRE(!provider_initialized(*owner, memory, manager_address, calls));
        REQUIRE(owner->state() == SessionState::starting && context_calls == before && remote.writes == 0);
        // Model the constructor return followed by RootInit's root+9b38 store.
        memory.manager = manager_address;
        if (test == 1) owner->unrouted_import();
        if (test == 2) owner->stop_requests();
        if (test == 3) owner->startup_leave(true);
        if (test == 5) memory.manager = 0;
        if (test == 6) provider = image + 0x2e87db0;
        if (test == 7) REQUIRE(provider_initialized(*owner, memory, manager_address, calls));
        bool bound = false;
        if (test == 4) { std::thread other([&] { bound = bind_root_provider(*owner, memory, calls); }); other.join(); }
        else bound = bind_root_provider(*owner, memory, calls);
        const bool ready = test == 0 || test == 7;
        REQUIRE(bound == (test == 0) && owner->routed() == ready && !owner->accepts_requests());
        REQUIRE(remote.writes == (ready ? 1u : 0u));
        REQUIRE(context_calls == before + (ready ? 1 : 0));
        owner->startup_leave(false);
        REQUIRE(!owner->accepts_requests()); // Root/provider readiness still requires native PROFILE import.
        if (ready) REQUIRE(owner->state() == SessionState::binding);
        else if (test != 4) REQUIRE(owner->state() == SessionState::rejected);
    }
    std::puts("PASS selected provider binding after native RootInit publication (8 cases)");
    for (unsigned test = 0; test < 13; ++test) {
        auto owner = make(); const auto root = owner->native_root();
        Remote remote{table.data()}; remote.replace_selection = true; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
        remote.files = {{root + "/sentinel-owner-" + owner->namespace_id() + ".txt", owner->ownership_record()},
            {root + "/GAME-AUTOSAVE3/game.details", "native3"}, {root + "/GAME-AUTOSAVE10/game.details", "native10"},
            {"PROFILE/profile.bin", "shared native output"}, {"GAME-AUTOSAVE3/game.details", "vanilla"}};
        uintptr_t provider = image + 0x2e90658;
        std::array<uintptr_t, 3> control{0x100000001, reinterpret_cast<uintptr_t>(&provider), 0};
        uintptr_t manager = reinterpret_cast<uintptr_t>(control.data());
        Memory memory; memory.manager = reinterpret_cast<uintptr_t>(&manager);
        REQUIRE(provider_initialized(*owner, memory, memory.manager, calls)); owner->startup_leave(false);
        REQUIRE(owner->publish_profile_catalog(remote_pointer, owner->ownership_record(),
            {"AUTOSAVE3", "AUTOSAVE10"}, "AUTOSAVE10", 1, false, 0));
        const auto original = remote.files;
        exercise_selection_writes(*owner, test >= 10 ? test - 9 : 0, [&](SaveFuture* older, SaveFuture* newer) {
            REQUIRE(remote.writes == 0 && remote.files == original);
            SaveResult result{};
            older->vtable->poll(older, &result, nullptr); REQUIRE(result.state == -1 && remote.writes == 0);
            newer->vtable->poll(newer, &result, nullptr); REQUIRE(result.state == -1 && remote.writes == 0);
            if (test == 2) return; // Destruction while pending cannot publish either record.
            if (test == 3) remote.fail_write = true;
            if (test == 4) remote.partial_write = true;
            if (test == 5) remote.discard_write = true;
            if (test == 6) remote.files[0].second.back() = '?';
            if (test == 7) remote.files.erase(remote.files.begin());
            if (test == 8) remote.files.erase(remote.files.begin() + 1); // Serialized selection was deleted.
            if (test == 9) owner->stop_requests(); // Already-owned writes still finalize under retained routing.
            const bool reverse = test == 1;
            auto first = reverse ? newer : older; auto second = reverse ? older : newer;
            first->vtable->poll(first, &result, nullptr);
            const bool failed = (test >= 3 && test <= 8) || test >= 10;
            REQUIRE(result.state == 0 && result.outcome == (failed ? 1 : 0));
            if (failed) {
                REQUIRE(owner->fault() == (test >= 10 ? SessionFault::native_write : SessionFault::native_profile));
                REQUIRE(remote.writes == (test <= 5 ? 1u : 0u)); return;
            }
            NativeCampaignCatalog catalog; uintptr_t actual = 0;
            REQUIRE(read_native_catalog(*owner, memory, "GAME-", catalog, actual));
            REQUIRE(catalog.selected == (reverse ? "AUTOSAVE10" : "AUTOSAVE3") && remote.writes == 1);
            second->vtable->poll(second, &result, nullptr); REQUIRE(result.state == 0 && result.outcome == 0);
            REQUIRE(read_native_catalog(*owner, memory, "GAME-", catalog, actual) && catalog.selected == "AUTOSAVE10");
            REQUIRE(remote.writes == (reverse ? 1u : 2u));
            second->vtable->poll(second, &result, nullptr); REQUIRE(result.state == 1);
            for (const auto& file : original) REQUIRE(*find(remote, file.first.c_str()) == file);
        });
    }
    std::puts("PASS correlated PROFILE selection storage, ordering, cancellation and write refusal (13 cases)");
    for (unsigned test = 0; test < 24; ++test) {
        auto owner = test == 16 ? std::make_unique<Session>() : make();
        const auto root = owner->native_root();
        Remote remote{table.data()}, foreign{table.data()}; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
        remote.files = {{root + "/sentinel-owner-" + owner->namespace_id() + ".txt", owner->ownership_record()},
            {"PROFILE/profile.bin", "account"}, {"GAME-AUTOSAVE10/game.details", "vanilla"}};
        if (test != 2 && test != 3 && test != 4 && test != 5)
            remote.files.emplace_back(root + "/GAME-AUTOSAVE3/game.details", "AP3");
        if (test == 3) remote.files.emplace_back("ap-other/GAME-AUTOSAVE3/game.details", "foreign AP");
        if (test == 4 || test == 5 || test == 23)
            remote.files.emplace_back(root + "/DLC1-AUTOSAVE10/game.details", "AP DLC1");
        uintptr_t provider = image + 0x2e90658;
        std::array<uintptr_t, 3> control{0x100000001, reinterpret_cast<uintptr_t>(&provider), 0};
        uintptr_t manager = reinterpret_cast<uintptr_t>(control.data());
        Memory memory; memory.manager = reinterpret_cast<uintptr_t>(&manager);
        if (test != 16 && test != 17) REQUIRE(provider_initialized(*owner, memory, memory.manager, calls));
        std::string input = "GAME-AUTOSAVE3";
        if (test == 1 || test == 23) input = "GAME-AUTOSAVE10";
        if (test == 4) { input = "DLC1-AUTOSAVE10"; memory.campaign = "DLC1-"; }
        if (test == 6) input = root + "/game-autosave3";
        if (test == 7) input = "GAME-AUTOSAVE12";
        if (test == 8) input = "GAME-AUTOSAVE3/../PROFILE";
        if (test == 9) input = "ap-other/GAME-AUTOSAVE3";
        if (test == 10) remote.files.erase(remote.files.begin());
        if (test == 11 || test == 22) remote_pointer = reinterpret_cast<uintptr_t>(&foreign);
        if (test == 12) memory.campaign = "OTHER-";
        if (test == 13) remote.files.pop_back();
        if (test == 14) { remote.files.pop_back(); remote.files.emplace_back(root + "/GAME-AUTOSAVE3/SlotFile", "partial"); }
        if (test == 15 || test == 22) input = "PROFILE";
        if (test == 18) owner->stop_requests();
        if (test == 19) remote.files[0].second.back() = '?';
        if (test == 20) input.assign(260, 'X');
        if (test == 23) memory.campaign = "DLC1-";
        const auto before_files = remote.files, foreign_files = foreign.files;
        const auto before_native = query_native_calls, before_releases = query_releases, before_context = context_calls;
        std::array<uint32_t, 6> identity_control{2, 2}; SaveReference identity{reinterpret_cast<uintptr_t>(identity_control.data())};
        SaveFuture* future = nullptr;
        REQUIRE(query_exists_scoped(*owner, memory, &future, &identity, test == 21 ? nullptr : input.c_str(),
            {calls, original_query, release_query}) == &future && future && !identity.control);
        REQUIRE(identity_control[0] == 1 && identity_control[1] == 1 && query_releases == before_releases + 1);
        const bool native = test == 15 || test == 16 || test == 17;
        REQUIRE(query_native_calls == before_native + (native ? 1 : 0));
        if (native) REQUIRE(queried_native_name == input && future == refused_save_future());
        if (test == 16 || test == 17) REQUIRE(context_calls == before_context);
        const bool invalid = (test >= 7 && test <= 12) || test == 14 || (test >= 19 && test <= 22);
        const bool empty = test == 2 || test == 3 || test == 5 || test == 13;
        SaveResult result{}; future->vtable->poll(future, &result, nullptr);
        REQUIRE(result.state == 0 && result.outcome == (invalid || empty || native ? 1 : 0));
        REQUIRE(result.value == (empty ? 8u : invalid || native ? 1u : test == 1 || test == 23 ? 0u : 1u));
        if (invalid) REQUIRE(owner->fault() == SessionFault::native_collection);
        else if (test != 17) REQUIRE(owner->fault() == SessionFault::none);
        if (!invalid && !native) { future->vtable->poll(future, &result, nullptr); REQUIRE(result.state == 1); }
        future->vtable->destroy(future, 1);
        REQUIRE(remote.files == before_files && foreign.files == foreign_files && remote.writes == 0 && foreign.writes == 0);
        REQUIRE(foreign.reads == 0 && foreign.queries == 0);
        if (test == 17) REQUIRE(owner->state() == SessionState::rejected && owner->fault() == SessionFault::missed_startup);
    }
    std::puts("PASS scoped native existence results, account delegation and consumed identity (24 cases)");
    Session disabled; Memory memory; const auto before = context_calls;
    REQUIRE(!provider_initialized(disabled, memory, 0, calls) && context_calls == before);
    std::puts("PASS production native namespace preparation/identity/catalog source and retained root lease (35 synthetic interface-014 cases)");
}

namespace {
struct Entry {
    unsigned char fields[0x70]{};
    NativeString name{};
    char text[64]{};
    explicit Entry(const std::string& value) { name.data = text; name.capacity_flags = 64; set(value.c_str()); }
    void set(const char* value) { REQUIRE(std::strlen(value) < 64); std::memcpy(text, value, std::strlen(value) + 1); name.length = static_cast<int32_t>(std::strlen(value)); }
};
static_assert(sizeof(Entry) == 0xe0 && offsetof(Entry, name) == 0x70);
struct CatalogData {
    NativeString name{}; char text[64]{};
    unsigned char padding[0x258 - sizeof(NativeString) - 64]{};
    Entry* entries = nullptr; int32_t count = 0, capacity = 0;
    unsigned char tail[0x280 - 0x268]{};
    CatalogData() { name.data = text; name.capacity_flags = 64; }
};
static_assert(sizeof(CatalogData) == 0x280 && offsetof(CatalogData, entries) == 0x258);
struct Control { uint32_t strong = 2, weak = 2; uintptr_t object = 0, destructor = 0; };
unsigned private_controls_freed = 0;
SaveReference* retain(SaveReference* out, const SaveReference* from) {
    auto* control = reinterpret_cast<Control*>(from->control); REQUIRE(control && control->strong);
    ++control->strong; ++control->weak; *out = *from; return out;
}
void release_reference(SaveReference* reference) {
    if (!reference->control) return;
    auto* control = reinterpret_cast<Control*>(reference->control); REQUIRE(control->strong && control->weak);
    --control->strong; --control->weak; reference->control = 0;
    if (!control->strong && control->destructor) {
        reinterpret_cast<DestroySaveData>(control->destructor)(control->object);
        REQUIRE(!control->weak); std::free(control); ++private_controls_freed;
    }
}
struct CatalogModel {
    CatalogData data; Control control;
    std::array<Entry, 2> entries;
    unsigned creates = 0, destroys = 0, assigns = 0;
    int32_t published_count = 2; bool fail_assign = false, throw_assign = false;
    SaveResult terminal{0, 0, 1, 0};
    explicit CatalogModel(const std::string& root) : entries{Entry(root + "/game-autosave10"), Entry(root + "/game-autosave3")} {
        control.object = reinterpret_cast<uintptr_t>(&data);
    }
};
CatalogModel* catalog_model = nullptr;
const char* current_campaign = "GAME-";
struct NativeEnumeration : SaveFuture { CatalogModel* model; SaveReference captured{}; unsigned polls = 0; };
SaveFuture* destroy_enumeration(SaveFuture* value, uint32_t) {
    auto* future = static_cast<NativeEnumeration*>(value); ++future->model->destroys;
    release_reference(&future->captured); delete future; return value;
}
SaveResult* poll_enumeration(SaveFuture* value, SaveResult* out, void*) {
    auto& future = *static_cast<NativeEnumeration*>(value);
    if (!future.polls++) { *out = {-1, 0, 0, 0}; return out; }
    auto& data = *reinterpret_cast<CatalogData*>(reinterpret_cast<Control*>(future.captured.control)->object);
    data.entries = future.model->entries.data();
    data.count = future.model->published_count;
    data.capacity = 2;
    *out = future.model->terminal; return out;
}
const SaveFutureVtable enumeration_vtable{destroy_enumeration, poll_enumeration};
SaveFuture** enumeration(uintptr_t, SaveFuture** out, uintptr_t, SaveReference* data, const char*) {
    ++catalog_model->creates;
    auto* future = new NativeEnumeration; future->vtable = &enumeration_vtable; future->model = catalog_model;
    retain(&future->captured, data); release_reference(data); *out = future; return out;
}
void set_data_name(uintptr_t object, const char* value) {
    auto& data = *reinterpret_cast<CatalogData*>(object); REQUIRE(std::strlen(value) < 64);
    std::memcpy(data.text, value, std::strlen(value) + 1); data.name.length = static_cast<int32_t>(std::strlen(value));
}
void set_entry_name(NativeString* name_value, const char* value) {
    ++catalog_model->assigns;
    if (catalog_model->throw_assign) throw std::bad_alloc();
    if (catalog_model->fail_assign) return;
    REQUIRE(std::strlen(value) < 64); std::memcpy(name_value->data, value, std::strlen(value) + 1);
    name_value->length = static_cast<int32_t>(std::strlen(value));
}
SaveFuture* create_catalog(Session& owner, SaveReference& data, const CatalogCalls& calls, const char* prefix = "GAME-") {
    engine::LocalMemory temporary_memory; SaveFuture* out = nullptr;
    REQUIRE(enumerate_provider(owner, temporary_memory, 0x1234, &out, 0x5678, &data, prefix, calls) == &out);
    return out; // The adapter must not retain this stack Memory object or the argument reference.
}
}
void run_catalog_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    std::array<uintptr_t, 20> table{};
    table[0] = reinterpret_cast<uintptr_t>(write); table[1] = reinterpret_cast<uintptr_t>(read);
    table[13] = reinterpret_cast<uintptr_t>(exists); table[15] = reinterpret_cast<uintptr_t>(size);
    table[18] = reinterpret_cast<uintptr_t>(count); table[19] = reinterpret_cast<uintptr_t>(name);
    const ProviderCalls provider_calls{image, context};
    const CatalogCalls calls{enumeration, retain, release_reference, set_data_name, set_entry_name,
        reinterpret_cast<uintptr_t>(&current_campaign) - 0x397f4a8};
    for (unsigned test = 0; test < 27; ++test) {
        auto owner = make(); Remote remote{table.data()}; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
        const auto root = owner->native_root();
        remote.files = {{root + "/sentinel-owner-" + owner->namespace_id() + ".txt", owner->ownership_record()}};
        if (test != 1 && test != 25) {
            remote.files.emplace_back(root + "/game-autosave3/game.details", "native details 3");
            remote.files.emplace_back(root + "/game-autosave10/game.details", "native details 10");
            if (test != 10) remote.files.emplace_back(root + "/sentinel-selection-GAME.txt",
                "sentinel-native-selection-v1\nnamespace_id=" + owner->namespace_id() + "\ncampaign=GAME-\nname=AUTOSAVE10\n");
        }
        if (test == 25) remote.files.emplace_back(root + "/sentinel-selection-GAME.txt",
            "sentinel-native-selection-v1\nnamespace_id=" + owner->namespace_id() + "\ncampaign=GAME-\nname=AUTOSAVE0\n");
        uintptr_t provider = image + 0x2e90658;
        std::array<uintptr_t, 3> provider_control{0x100000001, reinterpret_cast<uintptr_t>(&provider), 0};
        uintptr_t manager = reinterpret_cast<uintptr_t>(provider_control.data());
        Memory memory; memory.manager = reinterpret_cast<uintptr_t>(&manager);
        REQUIRE(provider_initialized(*owner, memory, memory.manager, provider_calls));
        owner->startup_leave(false); current_campaign = "GAME-";
        CatalogModel model(root); catalog_model = &model;
        if (test == 1 || test == 25) model.published_count = 0;
        if (test == 2) model.published_count = 1; // Native read dropped an expected entry.
        if (test == 3) model.entries[0].set("foreign-root/GAME-AUTOSAVE10");
        if (test == 4) model.entries[1].set((root + "/GAME-AUTOSAVE10").c_str());
        if (test == 5) model.entries[0].fields[0x58] = 1;
        if (test == 6) model.terminal = {0, 1, 0x40, 0};
        if (test == 8) model.fail_assign = true;
        if (test == 9) model.throw_assign = true;
        if (test == 12) model.published_count = 13;
        if (test == 13) model.terminal = {0, 0, 0, 0};
        if (test == 14) set_data_name(reinterpret_cast<uintptr_t>(&model.data), "foreign-root");
        SaveReference argument{reinterpret_cast<uintptr_t>(&model.control)};
        auto* future = create_catalog(*owner, argument, calls);
        REQUIRE(!argument.control);
        SaveResult result{}; ProfileChoice choice{};
        if (test == 14) {
            future->vtable->poll(future, &result, nullptr);
            REQUIRE(model.creates == 0 && result.state == 0 && result.outcome == 1);
        } else {
            REQUIRE(model.creates == 1 && model.control.strong == 3 && std::string(model.data.text) == root);
            future->vtable->poll(future, &result, nullptr);
            REQUIRE(result.state == -1 && !owner->profile_choice(choice) && model.assigns == 0);
            if (test == 7) {
                future->vtable->destroy(future, 1);
                REQUIRE(model.destroys == 1 && model.control.strong == 1 && !owner->profile_choice(choice));
                continue; // Abandoning a future is neither catalog publication nor completion.
            }
            if (test == 11) current_campaign = "DLC2-";
            // Changes after provider binding must be observed at the native
            // enumeration's terminal publication, never from a startup cache.
            if (test == 15) {
                remote.files.erase(find(remote, (root + "/game-autosave3/game.details").c_str()));
                model.published_count = 1;
            }
            if (test == 26) {
                remote.files.emplace_back(root + "/GAME-AUTOSAVE3/SlotFile", "remaining payload");
                remote.files.erase(remote.files.begin() + 1); model.published_count = 1;
            }
            if (test == 16) {
                auto file = find(remote, (root + "/game-autosave3/game.details").c_str());
                file->first = root + "/game-autosave0/game.details";
                model.entries[1].set((root + "/game-autosave0").c_str());
            }
            if (test == 16 || test == 17) {
                auto& record = find(remote, (root + "/sentinel-selection-GAME.txt").c_str())->second;
                record.replace(record.find("AUTOSAVE10"), 10, test == 16 ? "AUTOSAVE0" : "AUTOSAVE3");
            }
            if (test == 18) remote.files.erase(remote.files.begin()); // Never recreate a lost marker.
            if (test == 19) remote.files[0].second.back() = '?';
            if (test == 20) find(remote, (root + "/sentinel-selection-GAME.txt").c_str())->second = "bad record";
            if (test == 21) remote.files.emplace_back(root + "/sentinel-owner-foreign.txt", "foreign");
            if (test == 22) remote.files.erase(find(remote, (root + "/sentinel-selection-GAME.txt").c_str()));
            if (test == 23) remote.files.emplace_back(root + "/GAME-AUTOSAVE0/game.details", "not in native result");
            if (test == 24) remote.files.emplace_back("GAME-AUTOSAVE0/game.details", "vanilla is outside the inventory");
            const auto physical_before = remote.files;
            future->vtable->poll(future, &result, nullptr);
            REQUIRE(remote.files == physical_before && remote.writes == 0);
        }
        const bool success = test == 0 || test == 1 || test == 15 || test == 16 || test == 17 || test == 24 || test == 25;
        REQUIRE(result.state == 0 && result.outcome == (success ? 0 : 1) && result.value == 1);
        REQUIRE(owner->profile_choice(choice) == success);
        if (success) {
            const auto expected = test == 1 || test == 16 || test == 25 ? "AUTOSAVE0" : test == 17 ? "AUTOSAVE3" : "AUTOSAVE10";
            REQUIRE(std::string(choice.name.data()) == expected && choice.index == (test == 16 || test == 17 ? 1 : 0));
            if (test == 0) {
                REQUIRE(model.assigns == 2 && std::string(model.entries[0].text) == root + "/GAME-AUTOSAVE10");
                REQUIRE(std::string(model.entries[1].text) == root + "/GAME-AUTOSAVE3");
                // Native case-sensitive prefix removal/comparison now sees the
                // selected name at the actual metadata position, not suffix 10.
                REQUIRE(std::string(model.entries[0].text).substr(root.size() + 6) == choice.name.data());
            }
        } else REQUIRE(owner->fault() == SessionFault::native_collection && owner->routed());
        future->vtable->destroy(future, 1);
        REQUIRE(model.control.strong == 1 && model.control.weak == 1 && model.destroys == model.creates);
    }
    {
        Session disabled; CatalogModel model("unscoped"); catalog_model = &model;
        set_data_name(reinterpret_cast<uintptr_t>(&model.data), "vanilla-root");
        SaveReference argument{reinterpret_cast<uintptr_t>(&model.control)};
        auto* future = create_catalog(disabled, argument, calls);
        SaveResult result{}; future->vtable->poll(future, &result, nullptr); future->vtable->poll(future, &result, nullptr);
        REQUIRE(result.state == 0 && result.outcome == 0 && model.control.strong == 2 && model.assigns == 0);
        REQUIRE(std::string(model.data.text) == "vanilla-root");
        future->vtable->destroy(future, 1); REQUIRE(model.control.strong == 1 && model.destroys == 1);
    }
    catalog_model = nullptr;
    std::puts("PASS production live native catalog, metadata order, ownership and consumed refusals (28 cases)");
}

namespace {
struct PrerequisiteModel {
    Session* owner; unsigned allocations = 0, allocated = 0, constructed = 0, data_freed = 0, fail_allocation = 0;
    unsigned profile_created = 0, profile_polled = 0, profile_destroyed = 0;
    SaveResult result{0, 0, 1, 0};
};
PrerequisiteModel* prerequisite_model = nullptr;
void* allocate_private(size_t size_value) {
    auto& model = *prerequisite_model; ++model.allocations;
    REQUIRE(size_value == sizeof(Control) || size_value == sizeof(CatalogData));
    if (model.allocations == model.fail_allocation) return nullptr;
    void* result = std::malloc(size_value); REQUIRE(result); ++model.allocated; return result;
}
void* construct_private(void* value) { ++prerequisite_model->constructed; return new (value) CatalogData; }
void destroy_private(uintptr_t value) {
    if (!value) return;
    ++prerequisite_model->data_freed; reinterpret_cast<CatalogData*>(value)->~CatalogData();
    std::free(reinterpret_cast<void*>(value));
}
struct ProfileReadFuture : SaveFuture { SaveReference data{}; };
SaveFuture* destroy_profile_read(SaveFuture* value, uint32_t) {
    ++prerequisite_model->profile_destroyed;
    auto* future = static_cast<ProfileReadFuture*>(value); release_reference(&future->data); delete future; return value;
}
SaveResult* poll_profile_read(SaveFuture*, SaveResult* out, void*) {
    auto& model = *prerequisite_model; ProfileChoice choice{};
    REQUIRE(!model.owner->routed() || !model.owner->profile_choice(choice)); // Transport progresses before AP selection/import.
    *out = model.profile_polled++ ? model.result : SaveResult{-1, 0, 0, 0}; return out;
}
const SaveFutureVtable profile_read_vtable{destroy_profile_read, poll_profile_read};
SaveFuture** create_profile_read(uintptr_t, SaveFuture** out, uintptr_t, SaveReference* input) {
    ++prerequisite_model->profile_created;
    auto* future = new ProfileReadFuture; future->vtable = &profile_read_vtable;
    retain(&future->data, input); release_reference(input); *out = future; return out;
}
const ProfilePrerequisiteCalls* active_prerequisite_calls = nullptr;
SaveFuture** prepare_profile(uintptr_t p, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    engine::LocalMemory local;
    return profile_read_prerequisite(*prerequisite_model->owner, local, p, out, identity, data, *active_prerequisite_calls);
}
}
void run_prerequisite_contracts(const std::function<std::unique_ptr<Session>()>& make) {
    std::array<uintptr_t, 20> table{};
    table[0] = reinterpret_cast<uintptr_t>(write); table[1] = reinterpret_cast<uintptr_t>(read);
    table[13] = reinterpret_cast<uintptr_t>(exists); table[15] = reinterpret_cast<uintptr_t>(size);
    table[18] = reinterpret_cast<uintptr_t>(count); table[19] = reinterpret_cast<uintptr_t>(name);
    const ProviderCalls provider_calls{image, context};
    const CatalogCalls catalog{enumeration, retain, release_reference, set_data_name, set_entry_name,
        reinterpret_cast<uintptr_t>(&current_campaign) - 0x397f4a8};
    const ProfilePrerequisiteCalls calls{allocate_private, construct_private, destroy_private, catalog, create_profile_read};
    active_prerequisite_calls = &calls;
    for (unsigned test = 0; test < 20; ++test) {
        auto owner = test == 14 ? std::make_unique<Session>() : make();
        Remote remote{table.data()}; remote_pointer = reinterpret_cast<uintptr_t>(&remote);
        const auto root = owner->native_root();
        uintptr_t provider = image + 0x2e90658;
        std::array<uintptr_t, 3> provider_control{0x100000001, reinterpret_cast<uintptr_t>(&provider), 0};
        uintptr_t manager = reinterpret_cast<uintptr_t>(provider_control.data());
        Memory memory; memory.manager = reinterpret_cast<uintptr_t>(&manager);
        if (test != 14) {
            remote.files = {{root + "/sentinel-owner-" + owner->namespace_id() + ".txt", owner->ownership_record()}};
            if (test != 11) {
                remote.files.emplace_back(root + "/game-autosave3/game.details", "native details 3");
                remote.files.emplace_back(root + "/game-autosave10/game.details", "native details 10");
                remote.files.emplace_back(root + "/sentinel-selection-GAME.txt", "sentinel-native-selection-v1\nnamespace_id=" +
                    owner->namespace_id() + "\ncampaign=GAME-\nname=AUTOSAVE10\n");
            }
            if (test == 16 || test == 18) owner->startup_leave(false);
            if (test != 18) REQUIRE(provider_initialized(*owner, memory, memory.manager, provider_calls));
            if (test != 15 && test != 16 && test != 17 && test != 18) owner->startup_leave(false);
        }
        CatalogModel model(root); catalog_model = &model;
        PrerequisiteModel prerequisite{owner.get()}; prerequisite_model = &prerequisite;
        private_controls_freed = 0; current_campaign = "GAME-";
        if (test == 1 || test == 2) prerequisite.fail_allocation = test;
        if (test == 3) model.terminal = {0, 1, 0x40, 0};
        if (test == 6) prerequisite.result = {0, 1, 0x40, 0};
        if (test == 8) model.published_count = 1;
        if (test == 11) model.published_count = 0;
        if (test == 12) current_campaign = "unsupported";
        exercise_profile_caller(*owner, [&](SaveReference& input) {
            WriteCalls read_calls{create_profile_read, set_data_name, release_reference, nullptr, 0, prepare_profile};
            SaveFuture* future = nullptr;
            {
                engine::LocalMemory temporary;
                const auto native_provider = reinterpret_cast<uintptr_t>(&provider);
                if (owner->routed()) REQUIRE(owner->provider_operation(native_provider, 0x5678));
                REQUIRE(read_scoped(*owner, temporary, native_provider, &future, 0x5678, &input, read_calls) == &future);
            }
            REQUIRE(!input.control && prerequisite.profile_polled == 0);
            if (test == 7) current_campaign = "DLC2-";
            if (test == 9) owner->stop_requests();
            SaveResult result{};
            if (test == 19) Sleep(10020); // Would expire the old construction-time deadline before any work.
            future->vtable->poll(future, &result, nullptr);
            if (test == 4) {
                REQUIRE(result.state == -1 && prerequisite.profile_polled == 1);
                future->vtable->destroy(future, 1); return false;
            }
            if (test == 19) Sleep(10020); // Native pending retains its waiter beyond a guessed Core budget.
            if (result.state == -1) future->vtable->poll(future, &result, nullptr);
            if (test == 5) {
                REQUIRE(result.state == -1 && prerequisite.profile_polled == 2);
                future->vtable->destroy(future, 1); return false;
            }
            if (test == 13) current_campaign = "DLC1-";
            if (result.state == -1) future->vtable->poll(future, &result, nullptr);
            const bool accepted = test == 0 || test == 9 || test == 10 || test == 11 || test >= 14;
            REQUIRE(result.state == 0 && result.outcome == (accepted ? 0 : 1) && result.value == 1);
            future->vtable->destroy(future, 1); return accepted;
        }, test == 10);
        if (test == 15 || test == 17) {
            REQUIRE(owner->state() == SessionState::binding && !owner->accepts_requests());
            owner->startup_leave(test == 17);
        }
        REQUIRE(owner->accepts_requests() == (test == 0 || test == 11 || test == 15 || test == 16 || test == 19));
        const auto trace = owner->profile_trace();
        if (test == 3) REQUIRE(trace.failed_stage == ProfileStage::catalog && trace.failure.native_attempted && trace.failure.native_value == 0x40);
        if (test == 6) REQUIRE(trace.failed_stage == ProfileStage::transport && trace.failure.native_attempted && trace.failure.native_value == 0x40);
        if (test == 19) {
            REQUIRE(trace.failed_stage == ProfileStage::count);
            REQUIRE(trace.steps[static_cast<size_t>(ProfileStage::first_poll)].first_ms - trace.steps[0].first_ms >= 10000);
            REQUIRE(trace.steps[static_cast<size_t>(ProfileStage::transport)].changed_ms - trace.steps[static_cast<size_t>(ProfileStage::transport)].first_ms >= 10000);
        }
        if (test == 0) {
            REQUIRE(provider_initialized(*owner, memory, memory.manager, provider_calls));
            REQUIRE(owner->provider_operation(reinterpret_cast<uintptr_t>(&provider), 0x5678));
            REQUIRE(owner->state() == SessionState::admitted && owner->inspect().prepared_routes == required_routes);
            const auto before = remote.files;
            owner->provider_reset(memory.manager); // Reset/account removal after full PROFILE/catalog admission.
            REQUIRE(owner->routed() && !owner->accepts_requests() && !owner->native_io());
            REQUIRE(!provider_initialized(*owner, memory, memory.manager, provider_calls) && remote.files == before);
        }
        if (test == 9) REQUIRE(owner->state() == SessionState::admitted); // Shutdown stays nonaccepting after admission finishes.
        if (test == 17) REQUIRE(owner->state() == SessionState::faulted && owner->routed());
        if (test == 18) {
            REQUIRE(owner->state() == SessionState::rejected && owner->fault() == SessionFault::missed_startup);
            REQUIRE(!owner->bind_provider(owner->native_root(), remote_pointer, owner->ownership_record()));
        }
        REQUIRE(model.destroys == model.creates && prerequisite.profile_destroyed == prerequisite.profile_created);
        REQUIRE(prerequisite.data_freed == prerequisite.constructed &&
            prerequisite.allocated == prerequisite.data_freed + private_controls_freed);
        if (test == 1 || test == 2 || test == 12 || test == 14 || test == 18) REQUIRE(!model.creates);
        if (test == 1 || test == 2 || test == 7 || test == 12)
            REQUIRE(prerequisite.profile_polled == 0);
    }
    active_prerequisite_calls = nullptr; prerequisite_model = nullptr; catalog_model = nullptr;
    std::puts("PASS production PROFILE prerequisite, delayed scheduling, native pending, admission and cleanup (20 cases)");
}
