// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_profile.h"
#include "save_catalog.h"
#include "save_collector.h"
#include "save_b_io_trace.h"
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
bool payload(engine::Memory& memory, uintptr_t data, const ProfileCalls& calls, Fields& fields, Session& trace, bool output = false) {
    ProfileRead acquisition{};
    const auto record = [&](ProfileStage stage, bool valid, const char* predicate) {
        trace.profile_step(output ? ProfileStage::output_validation : stage,
            valid ? ProfileStatus::succeeded : ProfileStatus::refused, predicate, false, 0, 0, 0, acquisition);
        return valid;
    };
    struct Files { uintptr_t entries; int32_t count, capacity; } files{};
    uintptr_t file = 0, vtable = 0, buffer = 0, name_address = 0;
    uint64_t length = 0, capacity = 0;
    if (!named(memory, data, "PROFILE") || !at(memory, data, 0x1c0, files) ||
        files.count != 1 || files.capacity < 1 || !at(memory, files.entries, 0, file) ||
        !at(memory, file, 0, vtable) || vtable != calls.image_base + 0x2a575a8 ||
        !engine::add(file, 8, sizeof(NativeString), name_address) || !named(memory, name_address, "profile.bin") ||
        !at(memory, file, 0x150, length) || !at(memory, file, 0x158, capacity) || !at(memory, file, 0x168, buffer))
        return record(ProfileStage::framing, false, "profile_file_layout_or_size");
    acquisition.requested = length;
    if (length < 14 || length > maximum_profile || length > capacity)
        return record(ProfileStage::framing, false, "profile_file_layout_or_size");
    uintptr_t checked = 0;
    if (!engine::add(buffer, 0, static_cast<size_t>(length), checked)) {
        acquisition.reason = SC_REASON_OUT_OF_RANGE;
        return record(ProfileStage::framing, false, "profile_buffer_address_range");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    // The completed read owns SaveData until calls.read consumes its references.
    // Output validation runs after synchronous encoding, with its retained strong
    // reference (or the provider caller's reference) still alive. No native call
    // can resize/release this file during acquisition; refcounts alone are not a
    // concurrency lock. Check the owned file descriptor again before parsing.
    for (size_t offset = 0; offset < bytes.size();) {
        const size_t size = std::min(engine::Memory::maximum_copy, bytes.size() - offset);
        acquisition.offset = offset; acquisition.size = size;
        const auto result = memory.copy(buffer + offset, bytes.data() + offset, size);
        acquisition.reason = result.reason; acquisition.error = result.error;
        if (result.reason) {
            const char* predicate = result.reason == SC_REASON_OUT_OF_RANGE ? "profile_copy_policy_rejected" :
                result.reason == SC_REASON_PARTIAL_READ ? "profile_buffer_partial_read" : "profile_buffer_inaccessible";
            return record(ProfileStage::framing, false, predicate); // Partial vector is discarded, never parsed.
        }
        offset += size; // Whole span was checked; offset <= bounded length.
    }
    Files after{}; uintptr_t next_file = 0, next_buffer = 0; uint64_t next_length = 0, next_capacity = 0;
    const auto stable_field = [&](uintptr_t base, size_t offset, auto& value) {
        uintptr_t address = 0;
        const auto result = engine::add(base, offset, sizeof(value), address) ? memory.copy(address, &value, sizeof(value)) :
            engine::ReadResult{SC_REASON_OUT_OF_RANGE, 0};
        acquisition.reason = result.reason; acquisition.error = result.error;
        return result.reason == SC_REASON_NONE;
    };
    if (!stable_field(data, 0x1c0, after) || !stable_field(files.entries, 0, next_file) ||
        !stable_field(file, 0x150, next_length) || !stable_field(file, 0x158, next_capacity) || !stable_field(file, 0x168, next_buffer))
        return record(ProfileStage::framing, false, "profile_source_metadata_unreadable");
    if (after.entries != files.entries || after.count != files.count || after.capacity != files.capacity ||
        next_file != file || next_length != length || next_capacity != capacity || next_buffer != buffer)
        return record(ProfileStage::framing, false, "profile_source_changed");
    constexpr uint8_t header[]{0xa9, 0x0d, 0x8d, 0xaa, 0, 0, 0, 2};
    if (!record(ProfileStage::framing, !std::memcmp(bytes.data(), header, sizeof(header)), "supported_binary_v2_header")) return false;
    const uint32_t checksum = uint32_t(bytes[8]) << 24 | uint32_t(bytes[9]) << 16 | uint32_t(bytes[10]) << 8 | bytes[11];
    if (!record(ProfileStage::checksum, static_cast<uint32_t>(calls.checksum(bytes.data() + 12, bytes.size() - 12)) == checksum,
        "native_checksum_match")) return false;
    return record(ProfileStage::parse, WireReader(bytes).read(fields), "bounded_native_tree_and_selection_fields");
}
bool callbacks(engine::Memory& memory, ProfileOwner& owner, const ProfileCalls& calls, Session& session, BStage stage) {
    const auto profile = owner.profile, shell = owner.shell;
    uintptr_t context = 0, major = 0, minor = 0, version = 0, serialize = 0, vtable = 0;
    uintptr_t backlink = 0, manager = 0;
    BIoTrace diagnostic{&session.btrace,stage,0,profile};
    const auto read=[&](uintptr_t base,size_t offset,auto& value) {
        return diagnostic.read(memory,base,offset,value,"profile_callback_field_unreadable");
    };
    const auto check=[&](bool valid,const char* why,size_t offset) {
        if(!valid) session.btrace.record(stage,BStatus::refused,why,0,{{"field_offset",offset}},profile);
        return valid;
    };
    return check(read(profile, 0x18, context) && context == shell,"profile_context_link",0x18) &&
        check(read(shell, 8, backlink) && backlink == profile,"profile_shell_backlink",8) &&
        check(read(shell, 16, manager) && manager == owner.manager && manager,"profile_manager_link",16) &&
        check(read(profile, 8, owner.user) && owner.user != UINT32_MAX,"profile_native_user_validity",8) &&
        check(read(shell, 0, vtable) && vtable == calls.image_base + 0x2dbbbe8,"profile_shell_vtable",0) &&
        check(read(profile, 0x20, major) && major == calls.image_base + 0x1416b50,"profile_major_callback",0x20) &&
        check(read(profile, 0x28, minor) && minor == calls.image_base + 0x1416f10,"profile_minor_callback",0x28) &&
        check(read(profile, 0x30, version) && version == calls.image_base + 0x1416ee0,"profile_version_callback",0x30) &&
        check(read(profile, 0x48, serialize) && serialize == calls.image_base + 0x141bdd0,"profile_serializer_callback",0x48);
}
struct ReadContext : ProfileOwner {
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
bool values(Session& owner, BStage stage, engine::Memory& memory, const ProfileHolder& view, const ProfileCalls& calls,
        ProfileValue*& name, ProfileValue*& index, std::string& text, int32_t& number, const char*& predicate,
        int cursor_state = 1) {
    ProfileValue root{}, nv{}, iv{}, mv{};
    bool name_present=false, index_present=false, magic_present=false;
    BIoTrace diagnostic{&owner.btrace,stage,0,reinterpret_cast<uintptr_t>(view.root)};
    const auto read=[&](uintptr_t base,auto& value) { return diagnostic.read(memory,base,0,value,"structured_selection_field_unreadable"); };
    const auto require = [&](bool valid, const char* why) {
        if (!valid) {
            predicate = why;
            owner.btrace.record(stage,BStatus::refused,why,0,{{"direction",view.direction},{"root_type",root.type},
                {"name_type",nv.type},{"index_type",iv.type},{"index",iv.payload},{"magic_type",mv.type},
                {"magic",mv.payload},{"expected_magic",profile_magic},{"name_length",text.size()},
                {"name_present",name_present},{"index_present",index_present},{"magic_present",magic_present},
                {"cursor_state",cursor_state}},reinterpret_cast<uintptr_t>(view.root));
        }
        return valid;
    };
    if (!require(read(reinterpret_cast<uintptr_t>(view.root), root), "serialized_root_unreadable") ||
        !require(root.type == 7, "serialized_root_not_object")) return false;
    name = calls.lookup(view.root, "lastSaveGameName"); index = calls.lookup(view.root, "lastUsedGameSlot");
    const auto magic = calls.lookup(view.root, "magicNumber");
    const auto absent = calls.image_base + 0x4275fb0;
    name_present=name && reinterpret_cast<uintptr_t>(name)!=absent;
    index_present=index && reinterpret_cast<uintptr_t>(index)!=absent;
    magic_present=magic && reinterpret_cast<uintptr_t>(magic)!=absent;
    if (!require(name_present && index_present && magic_present, "serialized_selection_member_missing") ||
        !require(read(reinterpret_cast<uintptr_t>(name), nv) && nv.type == 4, "serialized_name_type_or_read") ||
        !require(read(reinterpret_cast<uintptr_t>(index), iv) && iv.type == 1, "serialized_slot_type_or_read") ||
        !require(iv.payload < 12, "serialized_slot_out_of_range") ||
        !require(read(reinterpret_cast<uintptr_t>(magic), mv) && mv.type == 1 && mv.payload == profile_magic,
            "serialized_magic_type_or_value") ||
        !require(native_text(memory, static_cast<uintptr_t>(nv.payload), text), "serialized_name_unreadable_or_too_long")) return false;
    number = static_cast<int32_t>(iv.payload);
    owner.btrace.record(stage,BStatus::succeeded,"structured_selection_read",0,
        {{"direction",view.direction},{"root_type",root.type},{"name_type",nv.type},{"index_type",iv.type},
         {"index",number},{"magic",mv.payload},{"name_length",text.size()},{"cursor_state",cursor_state}},
        reinterpret_cast<uintptr_t>(view.root));
    return true;
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
    if (!owner.routed()) {
        owner.unrouted_import("profile_reader", "import");
        if (owner.state() == SessionState::disabled) return calls.read(reference, data);
        calls.release(reference); calls.release(data); return 0x10;
    }
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
            require(callbacks(memory, context, calls, owner, BStage::profile_read), "profile_callback_identity") &&
            require(at(memory, data->control, 8, payload_object), "profile_data_reference") &&
            require(payload(memory, payload_object, calls, fields, owner), "profile_payload_rejected") &&
            require(owner.capture_profile_baseline(context, std::move(fields.name), fields.index),
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
    if (!owner.routed()) {
        if (owner.state()==SessionState::disabled) return calls.serialize(manager, profile, holder);
        ProfileHolder view{};
        const bool write=at(memory,reinterpret_cast<uintptr_t>(holder),0,view) && view.direction==1;
        owner.unrouted_import(write?"profile_serializer_write":"profile_serializer_read",write?"mutate":"import");
        return 3;
    }
    auto stage = ProfileStage::overlay;
    const auto refuse = [&](const char* predicate) {
        owner.profile_step(stage, ProfileStatus::refused, predicate);
        owner.fail_profile(); return 3u;
    };
    try {
        ProfileHolder view{};
        if (!at(memory, reinterpret_cast<uintptr_t>(holder), 0, view) || view.direction > 1) {
            return refuse("serializer_holder_or_direction");
        }
        if (view.direction == 1) stage = ProfileStage::output_validation;
        const char* baseline = nullptr; int32_t baseline_index = -1;
        ProfileOwner identity{profile, manager};
        const auto bstage=view.direction==1 ? BStage::profile_output : BStage::profile_read;
        owner.btrace.record(bstage,BStatus::entered,"native_serializer_enter",0,
            {{"direction",view.direction},{"write_context",active_write!=nullptr},{"read_context",active_read!=nullptr}},profile);
        if (!at(memory, profile, 0x18, identity.shell) || !callbacks(memory, identity, calls, owner, bstage)) return refuse("serializer_callback_identity");
        if (!owner.profile_baseline(identity, baseline, baseline_index)) return refuse("serializer_baseline_unavailable");
        const auto shell = identity.shell;
        ProfileValue* name = nullptr; ProfileValue* index = nullptr;
        std::string current; int32_t current_index = -1;
        const char* predicate = "serialized_selection_invalid";
        if (view.direction == 0) {
            auto context = active_read;
            if (!context || context->applied || context->profile != profile || context->manager != manager ||
                context->shell != shell ||
                !values(owner, bstage, memory, view, calls, name, index, current, current_index, predicate) ||
                current != baseline || current_index != baseline_index) return refuse("structured_selection_or_read_context_mismatch");
            selection(name, index, context->choice.name.data(), context->choice.index, calls);
            context->applied = true;
            const auto result = calls.serialize(manager, profile, holder);
            owner.profile_step(ProfileStage::overlay, result ? ProfileStatus::refused : ProfileStatus::succeeded,
                result ? "native_serializer_result" : "ap_selection_only_applied", true, 0, result);
            return result;
        }
        const auto result = calls.serialize(manager, profile, holder);
        if (result) {
            owner.profile_step(stage, ProfileStatus::refused, "native_output_serializer_result", true, 0, result);
            owner.fail_profile(); return result;
        }
        // 1414978e0 passes its stack-local Json root to 14055a0c0 after
        // serialization. Holder+8 is a mutable traversal cursor, not ownership
        // of that document. Native 140e5d570 pushes idMasterLevelManager and
        // completionInfo but pops only once, leaving the cursor in the former
        // even for an empty list. Validate/restore the encoded entry document;
        // neither restore the native traversal cursor nor import its subtree.
        ProfileHolder returned{};
        const int cursor_state=at(memory,reinterpret_cast<uintptr_t>(holder),0,returned)?
            (returned.root==view.root?1:0):-1;
        if (!values(owner, bstage, memory, view, calls, name, index, current, current_index, predicate, cursor_state))
            return refuse(predicate);
        if (!owner.observe_profile_choice(current, current_index)) return refuse("serialized_selection_not_in_owned_catalog");
        if (active_write) {
            std::string campaign;
            owner.btrace.record(BStage::profile_capture,BStatus::entered,"serializer_capture_context",active_write->write.sequence,
                {{"profile_matches",active_write->profile==profile},{"manager_matches",active_write->manager==manager},
                 {"index",current_index},{"baseline_index",baseline_index}},profile);
            if (active_write->write.sequence) return refuse("profile_write_serializer_repeated");
            if (active_write->profile != profile || active_write->manager != manager) return refuse("profile_write_context_owner_mismatch");
            if (!read_campaign_prefix(memory, calls.image_base, campaign)) return refuse("profile_write_campaign_prefix_unreadable");
            if (!owner.capture_profile_write(current, current_index,
                static_cast<unsigned>(native_campaign_index(campaign)), active_write->write)) return refuse("profile_write_choice_or_campaign_mismatch");
        }
        selection(name, index, baseline, baseline_index, calls);
        owner.btrace.record(BStage::profile_output,BStatus::succeeded,"native_output_selection_restored",0,
            {{"serialized_index",current_index},{"baseline_index",baseline_index},{"write_context",active_write!=nullptr},
             {"cursor_state",cursor_state}},profile);
        return result;
    } catch (const std::bad_alloc&) { return refuse("serializer_allocation_failed"); }
}
bool profile_payload_valid(Session& owner, engine::Memory& memory, uintptr_t data, const ProfileCalls& calls) {
    const char* predicate = "profile_output_baseline_unavailable";
    try {
        const char* baseline = nullptr; int32_t index = -1; Fields fields;
        if (owner.profile_output_baseline(baseline, index)) {
            if (!payload(memory, data, calls, fields, owner, true)) { owner.fail_profile(); return false; }
            if (fields.name == baseline && fields.index == index) return true;
            predicate = "encoded_output_changed_vanilla_selection";
        }
    } catch (const std::bad_alloc&) { predicate = "profile_output_allocation_failed"; }
    owner.profile_step(ProfileStage::output_validation, ProfileStatus::refused, predicate);
    owner.fail_profile(); return false;
}
void prepare_profile_write(Session& owner, engine::Memory& memory, SaveReference* profile, SaveReference* data,
        uintptr_t native_user, const char* suffix, PrepareProfile original, RetainProfileReference retain, const ProfileCalls& calls) {
    if (!owner.routed()) {
        owner.unrouted_import("profile_write_prepare", "mutate", 0, native_user);
        original(profile, data, native_user, suffix); return; // Native caller still owns reference cleanup.
    }
    if (!owner.native_io()) owner.profile_step(ProfileStage::write_after_refusal, ProfileStatus::refused,
        "native_profile_encode_attempt_after_fault");
    WriteContext context; SaveReference held{}; uintptr_t object = 0, source = 0;
    owner.btrace.record(BStage::profile_prepare,BStatus::entered,"native_profile_prepare_enter",0,
        {{"reentrant",active_write!=nullptr},{"native_io",owner.native_io()},{"profile_reference",profile->control!=0},
         {"data_reference",data->control!=0},{"native_user_present",native_user!=0},{"suffix_present",suffix!=nullptr}});
    struct Held {
        SaveReference& value; ReleaseSaveReference release;
        ~Held() { if (value.control) release(&value); }
    } held_owner{held, calls.release};
    // 141496330 passes a native user context as argument 3, NOT the shell.
    // 1414978e0 obtains the shell exclusively from profile->control+8; arg3 is
    // unused by its encoder. Keep it opaque and forward it unchanged.
    const bool valid = !active_write && at(memory, profile->control, 8, source) &&
        at(memory, source, 8, context.profile) && at(memory, source, 16, context.manager) &&
        at(memory, data->control, 8, object) && object && retain(&held, data) && held.control;
    if (!valid) {
        owner.profile_step(ProfileStage::output_validation, ProfileStatus::refused, "profile_write_reference_context");
        owner.fail_profile();
    }
    Writing writing(context);
    // Original owns both input refs, its local JSON/file and all native cleanup.
    // A separate strong ref keeps SaveData alive through the post-encoding check.
    original(profile, data, native_user, suffix);
    owner.btrace.record(BStage::profile_prepare,BStatus::succeeded,"native_profile_prepare_return",context.write.sequence,
        {{"reference_context_valid",valid},{"profile_consumed",profile->control==0},{"data_consumed",data->control==0}},object);
    try {
        if (!valid) return;
        if (!context.write.sequence) {
            owner.profile_step(ProfileStage::output_validation, ProfileStatus::refused, "profile_write_capture_missing");
            owner.fail_profile(); return;
        }
        if (!profile_payload_valid(owner, memory, object, calls)) return;
        if (!owner.remember_profile_write(object, context.write)) {
            owner.profile_step(ProfileStage::output_validation, ProfileStatus::refused, "profile_write_source_registration_failed");
            owner.fail_profile();
        }
    } catch (const std::bad_alloc&) {
        owner.profile_step(ProfileStage::output_validation, ProfileStatus::refused, "profile_write_registration_allocation_failed");
        owner.fail_profile();
    }
}
} // namespace sentinel::save
