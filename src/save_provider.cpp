// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_provider.h"
#include "save_catalog.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <new>

namespace sentinel::save {
void invalidate_provider(Session& owner, engine::Memory& memory, uintptr_t manager, uintptr_t image,
                         uintptr_t caller, uint32_t origin, const uintptr_t* frames, size_t count) {
    Session::ProviderInvalidation source{}; source.origin = origin;
    const auto rva = [image](uintptr_t address) -> uint32_t {
        return address >= image && address-image <= UINT32_MAX ? static_cast<uint32_t>(address-image) : 0;
    };
    source.caller_rva = rva(caller);
    size_t logged = 0;
    for (size_t i=0;i<count;++i) {
        const auto pc = rva(frames[i]);
        if (pc && logged < source.stack_rvas.size()) source.stack_rvas[logged++] = pc;
        // Supported PE .pdata owns [675290,675d23): native root shutdown.
        // A real unwind frame inside it proves destruction is already executing.
        if (pc > 0x675290 && pc <= 0x675d23) source.shutdown_rva = pc;
    }
    // Retail exit removes the master user BEFORE root destruction. Supported
    // common-frame quit -> common shutdown -> MoveToPressStart -> demotion ->
    // BroadcastNow, at these exact return sites. MoveToPressStart/sign-out alone
    // does not prove shutdown. No elapsed-time/menu-state exception.
    constexpr std::array<uint32_t,5> quit_chain{{0x17c1221,0x1a5b9b2,0x66d601,0x435cb9,0x43da41}};
    if (origin == 2 && source.caller_rva == quit_chain[0] &&
        std::equal(quit_chain.begin(),quit_chain.end(),source.stack_rvas.begin()))
        source.shutdown_rva = 0x435cb9;
    uintptr_t root=0, selected=0;
    source.root_manager_matches = owner.provider_root(root) &&
        !memory.copy(root+0x9b38,&selected,sizeof(selected)).reason && selected==manager;
    owner.provider_reset(manager,source);
}
namespace {
// ISteamRemoteStorage014, also identified by the selected native context
// initializer 141bd87d0. These are API methods, not native allocator/ref helpers.
using FileRead = int32_t (*)(uintptr_t, const char*, void*, int32_t);
using FileWrite = bool (*)(uintptr_t, const char*, const void*, int32_t);
using FileExists = bool (*)(uintptr_t, const char*);
using FileSize = int32_t (*)(uintptr_t, const char*);
using FileCount = int32_t (*)(uintptr_t);
using FileName = const char* (*)(uintptr_t, int32_t, int32_t*);
template<class T> bool at(engine::Memory& memory, uintptr_t base, size_t offset, T& out) {
    return base && base <= UINTPTR_MAX - offset && !memory.copy(base + offset, &out, sizeof(out)).reason;
}
bool name_at(engine::Memory& memory, const char* name, std::string& out) {
    std::array<char, 260> text{};
    for (size_t i = 0; name && i < text.size(); ++i) {
        if (memory.copy(reinterpret_cast<uintptr_t>(name) + i, &text[i], 1).reason) return false;
        if (!text[i]) { out.assign(text.data(), i); return i != 0; }
    }
    return false;
}
bool slot_name(std::string_view value, std::string& canonical) {
    if (value.size() < 9 || !steam_name_equal(value.substr(0, 8), "AUTOSAVE")) return false;
    const auto number = value.substr(8);
    if (!((number.size() == 1 && number[0] >= '0' && number[0] <= '9') || number == "10" || number == "11")) return false;
    canonical = "AUTOSAVE"; canonical.append(number); return true;
}
using NativeCatalogSource = std::array<NativeCampaignCatalog, 3>;
bool inspect_remote(Session& owner, engine::Memory& memory, uintptr_t remote,
        NativeCatalogSource& catalog, bool prepare) {
    uintptr_t table = 0;
    FileRead read = nullptr; FileWrite write = nullptr; FileExists exists = nullptr; FileSize size = nullptr;
    FileCount count = nullptr; FileName name = nullptr;
    if (!at(memory, remote, 0, table) || !at(memory, table, 0, write) || !write ||
        !at(memory, table, 0x08, read) || !read || !at(memory, table, 0x68, exists) || !exists ||
        !at(memory, table, 0x78, size) || !size || !at(memory, table, 0x90, count) || !count ||
        !at(memory, table, 0x98, name) || !name) return false;
    const auto prefix = owner.native_root() + "/";
    // The full digest in the marker's name avoids overwriting a different
    // identity's marker when shortened native roots collide. Payload ownership
    // still requires the complete record bytes, not this digest or local mutex.
    const auto marker = prefix + "sentinel-owner-" + owner.namespace_id() + ".txt";
    const auto deadline = GetTickCount64() + 250;
    const auto marker_prefix = prefix + "sentinel-owner-";
    const auto length = static_cast<int32_t>(owner.ownership_record().size());
    if (length <= 0 || length > 2048) return false;
    bool found = false; uint32_t root_files = 0;
    const auto scan = [&] {
        found = false; root_files = 0;
        for (auto& campaign : catalog) campaign.slots.clear();
        std::array<std::vector<std::string>, 3> payload_slots;
        const auto files = count(remote);
        if (files < 0 || files > 100000) return false;
        for (int32_t i = 0; i < files; ++i) {
            if (GetTickCount64() > deadline) return false;
            int32_t reported_size = 0; std::string key;
            if (!name_at(memory, name(remote, i, &reported_size), key)) return false;
            if (!steam_name_equal(std::string_view(key).substr(0, prefix.size()), prefix)) continue;
            ++root_files;
            const auto relative = std::string_view(key).substr(prefix.size());
            const auto slash = relative.find('/');
            if (slash != std::string_view::npos) {
                const bool details = steam_name_equal(relative.substr(slash + 1), "game.details");
                const auto stem = relative.substr(0, slash);
                const auto dash = stem.find('-');
                const int campaign = native_campaign_index(stem.substr(0, dash + 1));
                if (campaign >= 0 || details) {
                    std::string slot;
                    if (campaign < 0 || !slot_name(stem.substr(dash + 1), slot)) return false;
                    auto& payloads = payload_slots[static_cast<size_t>(campaign)];
                    if (std::find(payloads.begin(), payloads.end(), slot) == payloads.end()) payloads.push_back(slot);
                    if (details) {
                        auto& slots = catalog[static_cast<size_t>(campaign)].slots;
                        if (slots.size() == 12 || std::find(slots.begin(), slots.end(), slot) != slots.end()) return false;
                        slots.push_back(std::move(slot));
                    }
                }
            }
            if (!steam_name_equal(std::string_view(key).substr(0, marker_prefix.size()), marker_prefix)) continue;
            if (!steam_name_equal(key, marker) || found || reported_size != length) return false;
            found = true;
        }
        // Interrupted native writes/deletes can leave payloads without metadata.
        // Such a slot is not an empty namespace eligible for fresh creation.
        for (size_t campaign = 0; campaign < catalog.size(); ++campaign)
            if (payload_slots[campaign].size() != catalog[campaign].slots.size()) return false;
        return GetTickCount64() <= deadline;
    };
    if (!scan()) return false;
    if (!found) {
        if (!prepare || root_files || exists(remote, marker.c_str()) || GetTickCount64() > deadline) return false;
        // No rewrite, repair, copying of vanilla payloads or destructive rollback.
        // A failed/partial write stays unadmitted and cannot be replayed in-process.
        if (!write(remote, marker.c_str(), owner.ownership_record().data(), length) ||
            !scan() || !found || root_files != 1) return false;
    }
    if (!exists(remote, marker.c_str()) || size(remote, marker.c_str()) != length) return false;
    std::array<char, 2048> bytes{};
    if (read(remote, marker.c_str(), bytes.data(), length) != length ||
        std::string_view(bytes.data(), static_cast<size_t>(length)) != owner.ownership_record()) return false;
    // No hard latency claim for an individual synchronous Steam API call. A slow,
    // changed or partial observation cannot become an admission receipt.
    if (GetTickCount64() > deadline || size(remote, marker.c_str()) != length ||
        read(remote, marker.c_str(), bytes.data(), length) != length || GetTickCount64() > deadline) return false;
    if (std::string_view(bytes.data(), static_cast<size_t>(length)) != owner.ownership_record()) return false;
    for (unsigned campaign = 0; campaign < catalog.size(); ++campaign) {
        const std::string campaign_prefix = native_campaign_prefix(campaign);
        const auto key = prefix + "sentinel-selection-" + campaign_prefix.substr(0, campaign_prefix.size() - 1) + ".txt";
        if (!exists(remote, key.c_str())) continue;
        const auto n = size(remote, key.c_str());
        if (n < 1 || n > 256 || read(remote, key.c_str(), bytes.data(), n) != n || GetTickCount64() > deadline) return false;
        const std::string header = "sentinel-native-selection-v1\nnamespace_id=" + owner.namespace_id() +
            "\ncampaign=" + campaign_prefix + "\nname=";
        const std::string_view record(bytes.data(), static_cast<size_t>(n));
        if (record.substr(0, header.size()) != header || record.size() <= header.size() || record.back() != '\n' ||
            !slot_name(record.substr(header.size(), record.size() - header.size() - 1), catalog[campaign].selected)) return false;
    }
    return true;
}
struct ExistenceFuture : SaveFuture {
    SaveResult result{};
    bool delivered = false;
};
SaveFuture* destroy_existence(SaveFuture* future, uint32_t) { delete static_cast<ExistenceFuture*>(future); return future; }
SaveResult* poll_existence(SaveFuture* base, SaveResult* out, void*) {
    auto& future = *static_cast<ExistenceFuture*>(base);
    *out = future.delivered ? SaveResult{1, 0, 0, 0} : future.result;
    future.delivered = true; return out;
}
const SaveFutureVtable existence_vtable{destroy_existence, poll_existence};
}
SaveFuture** query_exists_scoped(Session& owner, engine::Memory& memory, SaveFuture** out,
        SaveReference* identity, const char* input, const ExistenceCalls& calls) {
    if (!owner.routed()) {
        std::string name; uintptr_t native_identity = 0;
        if (name_at(memory, input, name) && name == "PROFILE" && identity &&
            at(memory, identity->control, 8, native_identity) && native_identity &&
            owner.pre_root_profile_query(calls.provider.image_base + 0x1be4b3a, native_identity))
            return calls.query(out, identity, input);
        owner.unrouted_import("presence_query", "query", 0, native_identity);
        if (owner.state() == SessionState::disabled) return calls.query(out, identity, input);
        if (identity) calls.release(identity); *out = refused_save_future(); return out;
    }
    auto future = std::unique_ptr<ExistenceFuture>(new (std::nothrow) ExistenceFuture);
    bool valid = false, shared_profile = false;
    try {
        std::string name; uintptr_t context = 0, remote = 0;
        if (future && owner.native_io() && name_at(memory, input, name) && calls.provider.context) {
            context = calls.provider.context(calls.provider.image_base + 0x397fb88);
            if (at(memory, context, 0, remote) && owner.collecting(remote, owner.native_root())) {
                // The shared account query keeps the native implementation and
                // its consumed-reference/result semantics. No campaign redirect.
                if (name == "PROFILE") shared_profile = true;
                else {
                    const auto root = owner.native_root() + "/";
                    std::string_view relative(name);
                    if (steam_name_equal(relative.substr(0, root.size()), root)) relative.remove_prefix(root.size());
                    const auto dash = relative.find('-');
                    const int campaign = native_campaign_index(relative.substr(0, dash + 1));
                    std::string slot, current;
                    NativeCatalogSource catalog;
                    if (campaign >= 0 && slot_name(relative.substr(dash + 1), slot) &&
                        read_campaign_prefix(memory, calls.provider.image_base, current) &&
                        inspect_remote(owner, memory, remote, catalog, false) && owner.collecting(remote, owner.native_root())) {
                        const auto& slots = catalog[static_cast<size_t>(campaign)].slots;
                        const bool present = std::find(slots.begin(), slots.end(), slot) != slots.end();
                        // 141bd7a60 returns true for the named directory, false when
                        // only the current campaign has data, and error8 if neither
                        // exists. Preserve those distinctions within the owned root.
                        const bool current_data = !catalog[static_cast<size_t>(native_campaign_index(current))].slots.empty();
                        future->result = present ? SaveResult{0, 0, 1, 0} :
                            current_data ? SaveResult{0, 0, 0, 0} : SaveResult{0, 1, 8, 0};
                        valid = true;
                    }
                }
            }
        }
    } catch (const std::bad_alloc&) {}
    if (shared_profile) return calls.query(out, identity, input);
    calls.release(identity);
    if (!valid) { owner.fail(SessionFault::native_collection); *out = refused_save_future(); return out; }
    future->vtable = &existence_vtable; *out = future.release(); return out;
}
bool read_native_catalog(Session& owner, engine::Memory& memory, std::string_view prefix,
        NativeCampaignCatalog& out, uintptr_t& remote) {
    try {
        const auto campaign = native_campaign_index(prefix);
        uintptr_t provider = 0; NativeCatalogSource catalog;
        if (campaign < 0 || !owner.native_provider(provider) ||
            !inspect_remote(owner, memory, provider, catalog, false) ||
            !owner.collecting(provider, owner.native_root())) return false;
        out = std::move(catalog[static_cast<size_t>(campaign)]); remote = provider;
        return true;
    } catch (const std::bad_alloc&) { return false; }
}
bool Session::persist_profile_write(const ProfileWrite& write, engine::Memory& memory) {
    std::lock_guard<std::mutex> guard(selection_mutex_);
    btrace.record(BStage::profile_publish,BStatus::entered,"selection_persistence_enter",write.sequence,
        {{"campaign",write.campaign},{"index",write.choice.index}});
    const auto refuse=[&](const char* why,std::initializer_list<BFact> facts=std::initializer_list<BFact>{}) {
        btrace.record(BStage::profile_publish,BStatus::refused,why,write.sequence,facts); return false;
    };
    try {
        uintptr_t remote = 0;
        if (!native_provider(remote)) return refuse("selection_provider_unavailable");
        if (write.sequence <= persisted_selection_[write.campaign]) {
            btrace.record(BStage::profile_publish,BStatus::succeeded,"selection_older_completion_ignored",write.sequence,
                {{"persisted_sequence",persisted_selection_[write.campaign]}}); return true;
        }
        NativeCatalogSource catalog;
        if (!inspect_remote(*this, memory, remote, catalog, false)) return refuse("selection_owned_remote_catalog_invalid");
        const auto& inventory = catalog[write.campaign]; std::string selected;
        if (!slot_name(write.choice.name.data(), selected)) return refuse("selection_record_slot_shape");
        const bool prospective = inventory.slots.empty() && selected == "AUTOSAVE0";
        if (!prospective && std::none_of(inventory.slots.begin(), inventory.slots.end(),
                [&](const auto& slot) { return steam_name_equal(slot, selected); })) return refuse("selection_slot_absent_from_remote",{{"remote_slots",inventory.slots.size()}});
        if (inventory.selected != selected) {
            uintptr_t table = 0; FileWrite put = nullptr; FileRead read = nullptr; FileSize size = nullptr;
            if (!at(memory, remote, 0, table) || !at(memory, table, 0, put) || !put ||
                !at(memory, table, 8, read) || !read || !at(memory, table, 0x78, size) || !size) return refuse("selection_remote_callbacks_unreadable");
            std::string campaign(native_campaign_prefix(write.campaign));
            const auto key = native_root() + "/sentinel-selection-" + campaign.substr(0, campaign.size() - 1) + ".txt";
            const auto record = "sentinel-native-selection-v1\nnamespace_id=" + namespace_id() +
                "\ncampaign=" + campaign + "\nname=" + selected + "\n";
            std::string check(record.size(), '\0');
            if (!collecting(remote, native_root())) return refuse("selection_namespace_ownership_changed");
            if (!put(remote,key.c_str(),record.data(),static_cast<int32_t>(record.size()))) return refuse("selection_record_write_failed",{{"expected_size",record.size()}});
            const auto actual_size=size(remote,key.c_str());
            if(actual_size!=static_cast<int32_t>(record.size())) return refuse("selection_record_size_mismatch",{{"actual_size",actual_size},{"expected_size",record.size()}});
            const auto actual_read=read(remote,key.c_str(),check.data(),static_cast<int32_t>(check.size()));
            if(actual_read!=static_cast<int32_t>(check.size()) || check!=record) return refuse("selection_record_readback_mismatch",
                {{"actual_read",actual_read},{"expected_size",check.size()},{"bytes_equal",check==record}});
        }
        persisted_selection_[write.campaign] = write.sequence;
        btrace.record(BStage::profile_publish,BStatus::succeeded,"selection_record_verified",write.sequence,
            {{"campaign",write.campaign},{"index",write.choice.index},{"remote_slots",inventory.slots.size()},{"prospective",prospective}});
        // This is only the AP selection record for a completed PROFILE write.
        // It is not a gameplay-save, cloud-quiescence or matching-reopen receipt.
        return true;
    } catch (const std::bad_alloc&) { return refuse("selection_persistence_allocation_failed"); }
}
bool bind_root_provider(Session& owner, engine::Memory& memory, const ProviderCalls& calls) {
    uintptr_t root = 0, manager = 0;
    // An earlier unbound import, stop or abnormal return permanently closes this
    // qualification. No provider reinitialization or late routing upgrade occurs.
    if (!owner.startup_provider_root(root)) return false;
    if (!at(memory, root, 0x9b38, manager) || !manager) {
        owner.fail(SessionFault::provider_identity); return false;
    }
    return provider_initialized(owner, memory, manager, calls);
}
bool provider_initialized(Session& owner, engine::Memory& memory, uintptr_t manager, const ProviderCalls& calls) {
    uintptr_t root = 0;
    if (!owner.provider_root(root)) return false;
    try {
        uintptr_t selected_manager = 0, control = 0, provider = 0, table = 0;
        if (!at(memory, root, 0x9b38, selected_manager)) { owner.fail(SessionFault::provider_identity); return false; }
        if (manager != selected_manager) return false; // Another native account/service manager.
        if (at(memory, manager, 0, control) && at(memory, control, 8, provider) && at(memory, provider, 0, table) &&
            table == calls.image_base + 0x2e90658 && calls.context) {
            const auto context = calls.context(calls.image_base + 0x397fb88);
            uintptr_t remote = 0; NativeCatalogSource catalog;
            if (!owner.observe_provider_objects(manager, control, provider)) {
                owner.fail(SessionFault::provider_identity); return false;
            }
            if (owner.routed()) {
                if (at(memory, context, 0, remote) && owner.collecting(remote, owner.native_root())) return true;
                owner.fail(SessionFault::provider_identity); return false;
            }
            RecoveryTransport recovery{}; recovery.owner=calls.owner;
            if (owner.recovery_requested()) {
                uintptr_t remote_table=0;
                if (!at(memory,context,0,remote) || !at(memory,remote,0,remote_table) ||
                    !at(memory,remote_table,0,recovery.write) || !at(memory,remote_table,8,recovery.read) ||
                    !at(memory,remote_table,0x68,recovery.exists) || !at(memory,remote_table,0x78,recovery.size) ||
                    !at(memory,remote_table,0x90,recovery.count) || !at(memory,remote_table,0x98,recovery.name)) {
                    owner.fail(SessionFault::provider_identity); return false;
                }
                recovery.remote=remote;
                if (!owner.recover_startup(recovery)) return false;
            }
            if (at(memory, context, 0, remote) && owner.acquire_native_root_lock() &&
                inspect_remote(owner, memory, remote, catalog, true) &&
                owner.bind_provider(owner.native_root(), remote, owner.ownership_record())) return true;
        }
    } catch (const std::bad_alloc&) {}
    owner.fail(SessionFault::provider_identity); return false;
}
} // namespace sentinel::save
