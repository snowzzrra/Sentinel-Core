// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_collector.h"
#include <array>
#include <cstring>

namespace sentinel::save {
namespace {
bool text(engine::Memory& memory, const NativeString& value, std::string& out) {
    if (!value.data || value.length < 0 || value.length > 255) return false;
    std::array<char, 256> buffer{};
    const auto length = static_cast<size_t>(value.length);
    if (memory.copy(reinterpret_cast<uintptr_t>(value.data), buffer.data(), length + 1).reason ||
        buffer[length] || std::memchr(buffer.data(), 0, length)) return false;
    out.assign(buffer.data(), length);
    return true;
}
bool slot_key(std::string_view key, std::string_view prefix) {
    const std::string head = std::string(prefix) + "AUTOSAVE";
    constexpr std::string_view tail = "/game.details";
    if (key.size() <= head.size() + tail.size() || !steam_name_equal(key.substr(0, head.size()), head) ||
        !steam_name_equal(key.substr(key.size() - tail.size()), tail)) return false;
    const auto number = key.substr(head.size(), key.size() - head.size() - tail.size());
    return (number.size() == 1 && number[0] >= '0' && number[0] <= '9') ||
        number == "10" || number == "11";
}
CollectorResult* refuse(Session& owner, CollectorResult* out, const CollectorCalls& calls,
                        SessionFault why, bool produced) {
    owner.fail(why);
    if (produced && out->tag == 0) calls.release(&out->files);
    // Established collector error alternative: tag=1 / DWORD error at+8. The
    // wrapper propagates it without constructing the reader. 0x40 is not used.
    *out = {}; out->tag = 1;
    const uint32_t error = 1;
    std::memcpy(&out->files, &error, sizeof(error));
    return out;
}
}
EnumerationFuture** enumerate_scoped(Session& owner, engine::Memory& memory, EnumerationFuture** out,
        SaveReference* identity, const char* root, const char* prefix, CreateEnumeration create) {
    if (!owner.routed()) return create(out, identity, root, prefix);
    std::array<char, 64> captured{};
    bool terminated = false;
    for (size_t i = 0; root && i < captured.size(); ++i) {
        if (memory.copy(reinterpret_cast<uintptr_t>(root) + i, &captured[i], 1).reason) break;
        if (!captured[i]) { terminated = true; break; }
    }
    if (!terminated || (captured[0] && owner.native_root() != captured.data()))
        owner.fail(SessionFault::foreign_collector);
    // The original copies both strings and consumes its identity reference. On
    // refusal it still owns the job/future cleanup; collect_scoped emits the
    // native enum error before RemoteStorage calls. Never invent a bool future
    // for this list-valued operation, nor return the original vanilla root.
    return create(out, identity, owner.native_root().c_str(), prefix);
}
CollectorResult* collect_scoped(Session& owner, engine::Memory& memory,
    const CollectorContext* context, CollectorResult* out, const CollectorCalls& calls) {
    if (!owner.routed()) return calls.collect(context, out);
    CollectorContext captured{};
    std::string root, prefix;
    if (memory.copy(reinterpret_cast<uintptr_t>(context), &captured, sizeof(captured)).reason ||
        !text(memory, captured.root, root) || !text(memory, captured.prefix, prefix) ||
        !owner.collecting(captured.provider, root) ||
        (!steam_name_equal(prefix, "GAME-") && !steam_name_equal(prefix, "DLC1-") && !steam_name_equal(prefix, "DLC2-")))
        return refuse(owner, out, calls, SessionFault::foreign_collector, false);

    calls.collect(context, out);
    if (out->tag != 0)
        return refuse(owner, out, calls, SessionFault::native_collection, true);
    // The original owns the vector allocation/count invariant. Our additional
    // bound is the supported game's 12-slot selection domain, not pointer repair.
    if (out->files.count > 12)
        return refuse(owner, out, calls, SessionFault::malformed_entry, true);
    std::array<std::string, 12> names;
    for (size_t i = 0; i < out->files.count; ++i) {
        const auto& entry = out->files.entries[i];
        std::string key;
        if (!text(memory, entry.name, names[i]) || !text(memory, entry.read_key, key) ||
            names[i] != root + "/" + key || !slot_key(key, prefix) ||
            entry.name.data == entry.read_key.data)
            return refuse(owner, out, calls, SessionFault::malformed_entry, true);
        for (size_t previous = 0; previous < i; ++previous)
            if (steam_name_equal(names[previous], names[i]))
                return refuse(owner, out, calls, SessionFault::malformed_entry, true);
    }
    for (size_t i = 0; i < out->files.count; ++i) {
        auto& entry = out->files.entries[i];
        calls.assign(&entry.read_key, names[i].c_str());
        std::string key, name;
        if (!text(memory, entry.read_key, key) || key != names[i] ||
            !text(memory, entry.name, name) || name != names[i] || entry.name.data == entry.read_key.data)
            return refuse(owner, out, calls, SessionFault::native_copy, true);
    }
    return out;
}
} // namespace sentinel::save
