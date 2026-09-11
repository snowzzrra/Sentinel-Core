// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_profile.h"
#include "save_catalog.h"
#include "save_collector.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <unordered_set>

namespace sentinel::save {
namespace {
constexpr uint32_t profile_magic = 0x44357301;
constexpr size_t maximum_profile = 0xfa000;
static_assert(sizeof(ProfileValue) == 24 && offsetof(ProfileValue, owned) == 12);
static_assert(offsetof(ProfileHolder, root) == 8);

template<class T> bool at(engine::Memory& memory, uintptr_t base, size_t offset, T& value) {
    return base && base <= UINTPTR_MAX - offset && !memory.copy(base + offset, &value, sizeof(value)).reason;
}
bool native_text(engine::Memory& memory, uintptr_t pointer, std::string& out) {
    out.clear();
    for (size_t i = 0; i < 64; ++i) {
        char c = 0;
        if (!at(memory, pointer, i, c)) return false;
        if (!c) return true;
        out += c;
    }
    return false;
}
bool named(engine::Memory& memory, uintptr_t object, std::string_view expected) {
    NativeString name{};
    if (!at(memory, object, 0, name) || name.length != static_cast<int32_t>(expected.size())) return false;
    std::array<char, 64> text{};
    return !memory.copy(reinterpret_cast<uintptr_t>(name.data), text.data(), expected.size() + 1).reason &&
        !text[expected.size()] && std::memcmp(text.data(), expected.data(), expected.size()) == 0;
}
struct Fields {
    bool name_present = false, index_present = false, magic_present = false;
    std::string name;
    int32_t index = -1;
};
// The bounded preflight recognizes the native binary Json framing. It never
// decodes through native coercing accessors or invokes a native parser on a
// truncated/unsupported tree. Unrelated member values remain native-owned.
class WireReader {
public:
    explicit WireReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}
    bool read(Fields& out) {
        fields_ = &out;
        return node(0, {}) && cursor_ == bytes_.size() && out.magic_present &&
            out.name_present && out.index_present;
    }
private:
    bool integer(size_t width, uint64_t& out) {
        if (width > bytes_.size() - cursor_) return false;
        out = 0; for (size_t i = 0; i < width; ++i) out |= uint64_t(bytes_[cursor_++]) << (i * 8);
        return true;
    }
    bool count(uint32_t& out) {
        if (cursor_ == bytes_.size()) return false;
        const bool wide = (bytes_[cursor_] & 1) != 0;
        uint64_t value = 0;
        if (!integer(wide ? 4 : 1, value)) return false;
        out = static_cast<uint32_t>(value >> 1);
        return out <= bytes_.size() - cursor_;
    }
    bool string(std::string_view& out) {
        uint32_t length = 0;
        if (!count(length)) return false;
        out = {reinterpret_cast<const char*>(bytes_.data() + cursor_), length}; cursor_ += length;
        return true;
    }
    bool node(unsigned depth, std::string_view field) {
        if (depth > 64 || ++nodes_ > maximum_profile || cursor_ == bytes_.size()) return false;
        const uint8_t tag = bytes_[cursor_++];
        const bool selection = field == "lastSaveGameName" || field == "lastUsedGameSlot" || field == "magicNumber";
        if (!depth && tag != 14) return false;
        if (selection && ((field == "lastSaveGameName" && tag != 10) ||
            (field != "lastSaveGameName" && (tag < 1 || tag > 4)))) return false;
        if (tag >= 1 && tag <= 9) {
            const size_t width = tag == 9 ? 8 : size_t(1) << ((tag - 1) % 4);
            uint64_t raw = 0;
            if (!integer(width, raw)) return false;
            if (selection) {
                if (width < 8 && (raw & (uint64_t(1) << (width * 8 - 1)))) raw |= UINT64_MAX << (width * 8);
                const auto value = static_cast<int64_t>(raw);
                if (field == "magicNumber") {
                    if (value != profile_magic) return false;
                    fields_->magic_present = true;
                } else {
                    if (value < 0 || value >= 12) return false;
                    fields_->index = static_cast<int32_t>(value); fields_->index_present = true;
                }
            }
            return true;
        }
        if (tag == 10) {
            std::string_view value;
            if (!string(value)) return false;
            if (selection) {
                if (value.size() >= 64 || value.find('\0') != std::string_view::npos) return false;
                fields_->name.assign(value); fields_->name_present = true;
            }
            return true;
        }
        if (tag == 13 || tag == 14) {
            uint32_t children = 0;
            if (!count(children)) return false;
            std::unordered_set<std::string_view> keys;
            for (uint32_t i = 0; i < children; ++i) {
                std::string_view key;
                if (tag == 14 && (!string(key) || key.find('\0') != std::string_view::npos || !keys.insert(key).second)) return false;
                if (!node(depth + 1, depth == 0 ? key : std::string_view{})) return false;
            }
            return true;
        }
        return tag == 0 || tag == 11 || tag == 12;
    }
    const std::vector<uint8_t>& bytes_;
    Fields* fields_ = nullptr;
    size_t cursor_ = 12, nodes_ = 0;
};
bool payload(engine::Memory& memory, uintptr_t data, const ProfileCalls& calls, Fields& fields, Session* trace = nullptr) {
    const auto record = [&](ProfileStage stage, bool valid, const char* predicate) {
        if (trace) trace->profile_step(stage, valid ? ProfileStatus::succeeded : ProfileStatus::refused, predicate);
        return valid;
    };
    struct Files { uintptr_t entries; int32_t count, capacity; } files{};
    uintptr_t file = 0, vtable = 0, buffer = 0;
    uint64_t length = 0, capacity = 0;
    if (!named(memory, data, "PROFILE") || !at(memory, data, 0x1c0, files) ||
        files.count != 1 || files.capacity < 1 || !at(memory, files.entries, 0, file) ||
        !at(memory, file, 0, vtable) || vtable != calls.image_base + 0x2a575a8 ||
        !named(memory, file + 8, "profile.bin") || !at(memory, file, 0x150, length) ||
        !at(memory, file, 0x158, capacity) || !at(memory, file, 0x168, buffer) ||
        length < 14 || length > maximum_profile || length > capacity)
        return record(ProfileStage::framing, false, "profile_file_layout_or_size");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (memory.copy(buffer, bytes.data(), bytes.size()).reason) return record(ProfileStage::framing, false, "profile_buffer_unreadable");
    constexpr uint8_t header[]{0xa9, 0x0d, 0x8d, 0xaa, 0, 0, 0, 2};
    if (!record(ProfileStage::framing, !std::memcmp(bytes.data(), header, sizeof(header)), "supported_binary_v2_header")) return false;
    const uint32_t checksum = uint32_t(bytes[8]) << 24 | uint32_t(bytes[9]) << 16 | uint32_t(bytes[10]) << 8 | bytes[11];
    if (!record(ProfileStage::checksum, static_cast<uint32_t>(calls.checksum(bytes.data() + 12, bytes.size() - 12)) == checksum,
        "native_checksum_match")) return false;
    return record(ProfileStage::parse, WireReader(bytes).read(fields), "bounded_native_tree_and_selection_fields");
}
bool callbacks(engine::Memory& memory, uintptr_t profile, uintptr_t shell, const ProfileCalls& calls) {
    uintptr_t context = 0, major = 0, minor = 0, version = 0, serialize = 0, vtable = 0;
    return at(memory, profile, 0x18, context) && context == shell &&
        at(memory, shell, 0, vtable) && vtable == calls.image_base + 0x2dbbbe8 &&
        at(memory, profile, 0x20, major) && major == calls.image_base + 0x1416b50 &&
        at(memory, profile, 0x28, minor) && minor == calls.image_base + 0x1416f10 &&
        at(memory, profile, 0x30, version) && version == calls.image_base + 0x1416ee0 &&
        at(memory, profile, 0x48, serialize) && serialize == calls.image_base + 0x141bdd0;
}
struct ReadContext {
    uintptr_t profile = 0, manager = 0, shell = 0;
    ProfileChoice choice{};
    bool applied = false;
};
thread_local ReadContext* active_read = nullptr;
struct WriteContext { uintptr_t profile = 0, manager = 0; ProfileWrite write{}; };
thread_local WriteContext* active_write = nullptr;
struct Writing {
    WriteContext* previous = active_write;
    explicit Writing(WriteContext& context) { active_write = &context; }
    ~Writing() { active_write = previous; }
};
struct Reading {
    explicit Reading(ReadContext& context) { active_read = &context; }
    ~Reading() { active_read = nullptr; }
};
bool values(engine::Memory& memory, ProfileHolder* holder, const ProfileCalls& calls,
        ProfileValue*& name, ProfileValue*& index, std::string& text, int32_t& number) {
    ProfileHolder view{}; ProfileValue root{}, nv{}, iv{}, mv{};
    if (!at(memory, reinterpret_cast<uintptr_t>(holder), 0, view) ||
        !at(memory, reinterpret_cast<uintptr_t>(view.root), 0, root) || root.type != 7) return false;
    name = calls.lookup(view.root, "lastSaveGameName"); index = calls.lookup(view.root, "lastUsedGameSlot");
    const auto magic = calls.lookup(view.root, "magicNumber");
    const auto absent = calls.image_base + 0x4275fb0;
    if (reinterpret_cast<uintptr_t>(name) == absent || reinterpret_cast<uintptr_t>(index) == absent ||
        reinterpret_cast<uintptr_t>(magic) == absent ||
        !at(memory, reinterpret_cast<uintptr_t>(name), 0, nv) || nv.type != 4 ||
        !at(memory, reinterpret_cast<uintptr_t>(index), 0, iv) || iv.type != 1 || iv.payload >= 12 ||
        !at(memory, reinterpret_cast<uintptr_t>(magic), 0, mv) || mv.type != 1 || mv.payload != profile_magic ||
        !native_text(memory, static_cast<uintptr_t>(nv.payload), text)) return false;
    number = static_cast<int32_t>(iv.payload); return true;
}
void replace(ProfileValue& member, ProfileValue& replacement) {
    // Exactly Json::Value::swap's three fields (14035a2c0). Comments retain their
    // native ownership. Both replacements are built before either member changes.
    std::swap(member.payload, replacement.payload);
    std::swap(member.type, replacement.type);
    std::swap(member.owned, replacement.owned);
}
void selection(ProfileValue* name, ProfileValue* index, const char* text, int32_t number, const ProfileCalls& calls) {
    // Proven borrowed-string and integer constructor layouts. Reader storage
    // belongs to its enclosing ReadContext; writer storage to the pinned Session.
    // Neither requires native allocation, and neither string can outlive its owner.
    ProfileValue next_name{reinterpret_cast<uintptr_t>(text), 4, 0, {}, 0};
    ProfileValue next_index{static_cast<uint64_t>(number), 1, 0, {}, 0};
    replace(*name, next_name); replace(*index, next_index);
    calls.destroy(&next_name); calls.destroy(&next_index);
}
}
uint64_t read_profile(Session& owner, engine::Memory& memory, SaveReference* reference, SaveReference* data, const ProfileCalls& calls) {
    if (!owner.routed()) { owner.unrouted_import(); return calls.read(reference, data); }
    ReadContext context{}; uintptr_t payload_object = 0;
    bool valid = false;
    const char* predicate = "allocation_failed";
    owner.profile_step(ProfileStage::reader, ProfileStatus::entered, "native_reader_boundary");
    try {
        Fields fields;
        // A completed provider read can reach this callback after another route
        // faulted. Refuse before native import, even with a valid cached baseline.
        const auto require = [&](bool ok, const char* why) { if (!ok) predicate = why; return ok; };
        valid = require(owner.native_io(), "session_already_faulted") && require(!active_read, "reentrant_profile_reader") &&
            require(owner.profile_choice(context.choice), "ap_catalog_choice_unavailable") &&
            require(at(memory, reference->control, 8, context.shell) && at(memory, context.shell, 8, context.profile) &&
                at(memory, context.shell, 16, context.manager), "profile_shell_layout") &&
            require(callbacks(memory, context.profile, context.shell, calls), "profile_callback_identity") &&
            require(at(memory, data->control, 8, payload_object), "profile_data_reference") &&
            require(payload(memory, payload_object, calls, fields, &owner), "profile_payload_rejected") &&
            require(owner.capture_profile_baseline(context.profile, context.manager, std::move(fields.name), fields.index),
                "vanilla_selection_baseline_mismatch");
    } catch (const std::bad_alloc&) {}
    if (!valid) {
        owner.profile_step(ProfileStage::reader, ProfileStatus::refused, predicate);
        owner.fail_profile(); calls.release(reference); calls.release(data);
        return 0x10; // Consumed by 14148cc10 without its error-4 reset/error-0x100 save fallback.
    }
    Reading reading(context);
    const auto result = calls.read(reference, data); // Consumes both references on every native exit.
    owner.profile_step(ProfileStage::reader, result == 0 && context.applied ? ProfileStatus::succeeded : ProfileStatus::refused,
        result ? "native_reader_result" : context.applied ? "native_reader_and_overlay_completed" : "overlay_not_called",
        true, 0, static_cast<int64_t>(result));
    if (result != 0 || !context.applied) {
        owner.fail_profile(); return 0x10; // Keep native cleanup; suppress reset-producing error remapping.
    }
    owner.profile_step(ProfileStage::application, ProfileStatus::succeeded, "native_reader_settings_cache_completed", true);
    if (!owner.profile_read_completed()) {
        owner.profile_step(ProfileStage::admission, ProfileStatus::refused, "profile_completion_gate");
        owner.fail_profile(); return 0x10;
    }
    return 0;
}
uint32_t serialize_profile(Session& owner, engine::Memory& memory, uintptr_t manager, uintptr_t profile,
        ProfileHolder* holder, const ProfileCalls& calls) {
    if (!owner.routed()) return calls.serialize(manager, profile, holder);
    const auto refuse = [&](const char* predicate) {
        owner.profile_step(ProfileStage::overlay, ProfileStatus::refused, predicate);
        owner.fail_profile(); return 3u;
    };
    try {
        ProfileHolder view{};
        if (!at(memory, reinterpret_cast<uintptr_t>(holder), 0, view) || view.direction > 1) {
            return refuse("serializer_holder_or_direction");
        }
        const char* baseline = nullptr; int32_t baseline_index = -1;
        if (!owner.profile_baseline(profile, manager, baseline, baseline_index)) return refuse("serializer_baseline_unavailable");
        uintptr_t shell = 0;
        if (!at(memory, profile, 0x18, shell) || !callbacks(memory, profile, shell, calls)) return refuse("serializer_callback_identity");
        ProfileValue* name = nullptr; ProfileValue* index = nullptr;
        std::string current; int32_t current_index = -1;
        if (view.direction == 0) {
            auto context = active_read;
            if (!context || context->applied || context->profile != profile || context->manager != manager ||
                context->shell != shell ||
                !values(memory, holder, calls, name, index, current, current_index) ||
                current != baseline || current_index != baseline_index) return refuse("structured_selection_or_read_context_mismatch");
            selection(name, index, context->choice.name.data(), context->choice.index, calls);
            context->applied = true;
            const auto result = calls.serialize(manager, profile, holder);
            owner.profile_step(ProfileStage::overlay, result ? ProfileStatus::refused : ProfileStatus::succeeded,
                result ? "native_serializer_result" : "ap_selection_only_applied", true, 0, result);
            return result;
        }
        const auto result = calls.serialize(manager, profile, holder);
        if (result || !values(memory, holder, calls, name, index, current, current_index) ||
            !owner.observe_profile_choice(current, current_index)) { owner.fail_profile(); return result ? result : 3; }
        if (active_write) {
            std::string campaign;
            if (active_write->write.sequence || active_write->profile != profile || active_write->manager != manager ||
                !read_campaign_prefix(memory, calls.image_base, campaign) ||
                !owner.capture_profile_write(current, current_index,
                    static_cast<unsigned>(native_campaign_index(campaign)), active_write->write)) {
                owner.fail_profile(); return 3;
            }
        }
        selection(name, index, baseline, baseline_index, calls);
        return result;
    } catch (const std::bad_alloc&) { return refuse("serializer_allocation_failed"); }
}
bool profile_payload_valid(Session& owner, engine::Memory& memory, uintptr_t data, const ProfileCalls& calls) {
    try {
        const char* baseline = nullptr; int32_t index = -1; Fields fields;
        if (owner.profile_baseline(0, 0, baseline, index) && payload(memory, data, calls, fields) &&
            fields.name == baseline && fields.index == index) return true;
    } catch (const std::bad_alloc&) {}
    owner.fail_profile(); return false;
}
void prepare_profile_write(Session& owner, engine::Memory& memory, SaveReference* profile, SaveReference* data,
        uintptr_t shell, const char* suffix, PrepareProfile original, RetainProfileReference retain, const ProfileCalls& calls) {
    if (!owner.routed()) { original(profile, data, shell, suffix); return; }
    if (!owner.native_io()) owner.profile_step(ProfileStage::write_after_refusal, ProfileStatus::refused,
        "native_profile_encode_attempt_after_fault");
    WriteContext context; SaveReference held{}; uintptr_t object = 0, source = 0;
    struct Held {
        SaveReference& value; ReleaseSaveReference release;
        ~Held() { if (value.control) release(&value); }
    } held_owner{held, calls.release};
    const bool valid = !active_write && at(memory, profile->control, 8, source) && source == shell &&
        at(memory, source, 8, context.profile) && at(memory, source, 16, context.manager) &&
        at(memory, data->control, 8, object) && object && retain(&held, data) && held.control;
    if (!valid) owner.fail_profile();
    Writing writing(context);
    // Original owns both input refs, its local JSON/file and all native cleanup.
    // A separate strong ref keeps SaveData alive through the post-encoding check.
    original(profile, data, shell, suffix);
    try {
        if (!valid || !context.write.sequence || !profile_payload_valid(owner, memory, object, calls) ||
            !owner.remember_profile_write(object, context.write)) owner.fail_profile();
    } catch (const std::bad_alloc&) { owner.fail_profile(); }
}
} // namespace sentinel::save
