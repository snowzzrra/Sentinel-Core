// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_catalog.h"
#include "save_provider.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <new>

namespace sentinel::save {
namespace {
template<class T> bool at(engine::Memory& memory, uintptr_t base, size_t offset, T& out) {
    return base && base <= UINTPTR_MAX - offset && !memory.copy(base + offset, &out, sizeof(out)).reason;
}
bool text_at(engine::Memory& memory, uintptr_t address, std::string& out) {
    NativeString value{}; char text[64]{};
    if (!at(memory, address, 0, value) || value.length < 0 || value.length >= 64 || !value.data ||
        memory.copy(reinterpret_cast<uintptr_t>(value.data), text, static_cast<size_t>(value.length) + 1).reason || text[value.length] ||
        std::memchr(text, 0, static_cast<size_t>(value.length))) return false;
    out.assign(text, static_cast<size_t>(value.length)); return true;
}
bool prefix_at(engine::Memory& memory, const char* address, std::string& out) {
    char prefix[8]{};
    for (size_t i = 0; address && i < sizeof(prefix); ++i) {
        if (memory.copy(reinterpret_cast<uintptr_t>(address) + i, &prefix[i], 1).reason) return false;
        if (!prefix[i]) { out = prefix; return native_campaign_index(out) >= 0; }
    }
    return false;
}
struct CatalogFuture : SaveFuture {
    Session& owner; engine::LocalMemory memory; CatalogCalls calls;
    SaveReference data{}; SaveFuture* native = nullptr;
    std::string prefix;
    bool terminal = false;
    CatalogFuture(Session& s, const CatalogCalls& c) : owner(s), calls(c) {}
    ~CatalogFuture() { if (native) native->vtable->destroy(native, 1); calls.release(&data); }
    bool reconcile() {
        NativeCampaignCatalog source; uintptr_t remote = 0;
        if (!read_native_catalog(owner, memory, prefix, source, remote)) return false;
        std::string current_prefix;
        if (!read_campaign_prefix(memory, calls.image_base, current_prefix) ||
            !steam_name_equal(prefix, current_prefix)) return false;
        uintptr_t object = 0, entries = 0; int32_t count = 0, capacity = 0;
        if (!at(memory, data.control, 8, object) || !at(memory, object, 0x258, entries) ||
            !at(memory, object, 0x260, count) || !at(memory, object, 0x264, capacity) ||
            count < 0 || count > 12 || capacity < count || (count && !entries) ||
            static_cast<size_t>(count) != source.slots.size()) return false;
        std::vector<std::string> ordered, full_names;
        ordered.reserve(static_cast<size_t>(count)); full_names.reserve(static_cast<size_t>(count));
        const auto stem = owner.native_root() + "/" + prefix;
        for (int32_t i = 0; i < count; ++i) {
            const auto entry = entries + static_cast<size_t>(i) * 0xe0;
            uint8_t corrupt = 0; std::string name;
            if (!at(memory, entry, 0x58, corrupt) || corrupt || !text_at(memory, entry + 0x70, name) ||
                !steam_name_equal(std::string_view(name).substr(0, stem.size()), stem)) return false;
            const auto candidate = std::string_view(name).substr(stem.size());
            const auto found = std::find_if(source.slots.begin(), source.slots.end(),
                [&](const std::string& value) { return steam_name_equal(candidate, value); });
            if (found == source.slots.end() || std::find(ordered.begin(), ordered.end(), *found) != ordered.end()) return false;
            ordered.push_back(*found); full_names.push_back(stem + *found);
        }
        std::string selected = source.selected;
        int32_t index = 0; const bool prospective = ordered.empty() && (selected.empty() || selected == "AUTOSAVE0");
        if (prospective) selected = "AUTOSAVE0"; // Native empty-cache format "%s0", index 0.
        else {
            const auto found = std::find(ordered.begin(), ordered.end(), selected);
            if (found == ordered.end()) return false;
            index = static_cast<int32_t>(found - ordered.begin());
        }
        // The source belongs to this completed native operation. Validate the
        // entire set first; failure returns an error through the normal finalizer.
        for (int32_t i = 0; i < count; ++i) {
            const auto address = entries + static_cast<size_t>(i) * 0xe0 + 0x70;
            calls.assign(reinterpret_cast<NativeString*>(address), full_names[static_cast<size_t>(i)].c_str());
            std::string actual;
            if (!text_at(memory, address, actual) || actual != full_names[static_cast<size_t>(i)]) return false;
        }
        return owner.publish_profile_catalog(remote, owner.ownership_record(), std::move(ordered), selected, index, prospective,
            static_cast<unsigned>(native_campaign_index(prefix)));
    }
};
SaveFuture* destroy(SaveFuture* future, uint32_t) { delete static_cast<CatalogFuture*>(future); return future; }
SaveResult* poll(SaveFuture* base, SaveResult* out, void* executor) {
    auto& future = *static_cast<CatalogFuture*>(base);
    if (future.terminal) { *out = {1, 0, 0, 0}; return out; }
    SaveResult result{};
    future.native->vtable->poll(future.native, &result, executor);
    if (result.state == -1 && future.owner.native_io()) {
        future.owner.profile_step(ProfileStage::catalog, ProfileStatus::pending, "native_catalog_pending",
            true, result.state, result.outcome, result.value);
        *out = result; return out;
    }
    bool valid = false;
    try {
        valid = result.state == 0 && result.outcome == 0 && result.value == 1 &&
            future.owner.native_io() && future.reconcile();
    } catch (const std::bad_alloc&) {}
    future.terminal = true;
    future.owner.profile_step(ProfileStage::catalog, valid ? ProfileStatus::succeeded : ProfileStatus::refused,
        valid ? "native_order_and_owned_selection_reconciled" :
        (result.state || result.outcome || result.value != 1) ? "native_catalog_result" : "catalog_reconciliation",
        true, result.state, result.outcome, result.value);
    if (!valid) { future.owner.fail(SessionFault::native_collection); result = {0, 1, 1, 0}; }
    *out = result; return out;
}
const SaveFutureVtable vtable{destroy, poll};
}
bool read_campaign_prefix(engine::Memory& memory, uintptr_t image_base, std::string& out) {
    const char* current = nullptr;
    return at(memory, image_base, 0x397f4a8, current) && prefix_at(memory, current, out);
}
SaveFuture** enumerate_provider(Session& owner, engine::Memory& memory, uintptr_t provider, SaveFuture** out,
    uintptr_t identity, SaveReference* data, const char* prefix, const CatalogCalls& calls) {
    if (!owner.routed()) { owner.unrouted_import(); return calls.enumerate(provider, out, identity, data, prefix); }
    auto future = std::unique_ptr<CatalogFuture>(new (std::nothrow) CatalogFuture(owner, calls));
    bool valid = false;
    try {
        uintptr_t object = 0; std::string root;
        valid = future && owner.native_io() && at(memory, data->control, 8, object) &&
            text_at(memory, object, root) && (root.empty() || root == owner.native_root()) && prefix_at(memory, prefix, future->prefix);
        if (valid) {
            future->prefix = native_campaign_prefix(static_cast<unsigned>(native_campaign_index(future->prefix)));
            calls.set_name(object, owner.native_root().c_str());
            valid = text_at(memory, object, root) && root == owner.native_root();
        }
        if (valid) { calls.retain(&future->data, data); valid = future->data.control != 0; }
    } catch (const std::bad_alloc&) { valid = false; }
    if (!valid) { owner.fail(SessionFault::native_collection); calls.release(data); *out = refused_save_future(); return out; }
    // The original factory still consumes its argument and owns the native job.
    calls.enumerate(provider, &future->native, identity, data, future->prefix.c_str());
    if (!future->native) { owner.fail(SessionFault::native_collection); *out = refused_save_future(); return out; }
    future->vtable = &vtable; *out = future.release(); return out;
}
} // namespace sentinel::save
