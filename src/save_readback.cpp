// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_readback.h"
#include "save_backup.h"
#include <windows.h>
#include <cstring>
#include <new>

namespace sentinel::save {
namespace {
template<class T> bool at(engine::Memory& memory, uintptr_t p, size_t offset, T& out) {
    return p && p <= UINTPTR_MAX - offset && !memory.copy(p + offset, &out, sizeof(out)).reason;
}
struct NativeControl { uint32_t strong, weak; uintptr_t object; DestroySaveData destroy; };
static_assert(sizeof(NativeControl) == 24 && sizeof(ReadWorkerResult) == 16);
struct ReadbackFuture : SaveFuture {
    Session& owner; uint64_t operation; ReadbackCalls calls;
    SaveReference data{}; SaveFuture* native = nullptr;
    bool started = false, terminal = false;
    ReadbackFuture(Session& s, uint64_t id, const ReadbackCalls& c) : owner(s), operation(id), calls(c) {}
    ~ReadbackFuture() {
        if (!terminal) owner.native_writes.readback_result(operation, false);
        if (native) native->vtable->destroy(native, 1);
        // Worker references may outlive this wrapper. The actual SaveData Clear/
        // destructor detour removes the pointer mapping, never this wrapper.
        calls.native.catalog.release(&data);
    }
};
bool populate(ReadbackFuture& future, engine::Memory& memory) {
    SdkWriteObservation manifest;
    if (!future.owner.native_writes.readback_manifest(future.operation, manifest)) return false;
    auto* control = reinterpret_cast<NativeControl*>(future.data.control);
    const auto data = control->object;
    uintptr_t files = 0; int32_t count = 0, capacity = 0;
    if (!at(memory, data, 0x1c8, count) || count || !at(memory, data, 0x1cc, capacity)) return false;
    const auto needed = static_cast<int32_t>(manifest.payloads.size());
    if (capacity < needed) return false;
    if (!at(memory, data, 0x1c0, files) || !files) return false;
    const std::string prefix = manifest.directory + "/";
    for (const auto& file : manifest.payloads) {
        const std::string name(file.name.data());
        if (name.compare(0, prefix.size(), prefix) || name.size() == prefix.size()) return false;
        void* allocation = future.calls.native.allocate(0x190);
        if (!allocation) return false;
        // Native ownership bit 0x40 makes SaveData::Clear delete the stream;
        // bit 0x100 preserves storage bytes instead of decrypting them.
        future.calls.construct_stream(allocation, name.c_str() + prefix.size(), 0x140);
        const auto stream = reinterpret_cast<uintptr_t>(allocation);
        std::memcpy(reinterpret_cast<void*>(files + static_cast<uintptr_t>(count) * 8), &stream, 8);
        ++count;
        std::memcpy(reinterpret_cast<void*>(data + 0x1c8), &count, 4);
    }
    return true;
}
SaveFuture* destroy(SaveFuture* value, uint32_t) { delete static_cast<ReadbackFuture*>(value); return value; }
SaveResult* poll(SaveFuture* value, SaveResult* out, void* task) {
    auto& future = *static_cast<ReadbackFuture*>(value);
    if (future.terminal) { *out = {1, 0, 0, 0}; return out; }
    bool ready = true;
    engine::LocalMemory memory;
    if (!future.started) {
        try { ready = populate(future, memory); } catch (const std::bad_alloc&) { ready = false; }
        future.started = true;
    }
    SaveResult result{0, 1, 1, 0};
    if (ready) {
        future.native->vtable->poll(future.native, &result, task);
        if (result.state == -1) { *out = result; return out; }
    }
    future.terminal = true;
    future.owner.native_writes.readback_result(future.operation,
        !result.state && !result.outcome && result.value == 1);
    NativeWriteOperation operation;
    if (!future.owner.native_writes.inspect_operation(future.operation, operation) || operation.readback_failed) {
        future.owner.fail(SessionFault::native_read); result = {0, 1, 1, 0};
    }
    *out = result; return out;
}
const SaveFutureVtable vtable{destroy, poll};
using FileSize = int32_t (*)(uintptr_t, const char*);
struct SizeProxy { const void* table; uintptr_t remote; FileSize size; const SdkWriteObservation& manifest; };
int32_t file_size(uintptr_t self, const char* name) {
    const auto& proxy = *reinterpret_cast<const SizeProxy*>(self);
    for (const auto& file : proxy.manifest.payloads) if (!std::strcmp(name, file.name.data())) {
        const auto actual = proxy.size(proxy.remote, name);
        return actual > 0 && static_cast<uint32_t>(actual) == file.size ? actual : 0;
    }
    return 0;
}
const std::array<void*, 16> size_table{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(file_size)};
ReadWorkerResult* invoke_prepare(PrepareRead original, uintptr_t context, ReadWorkerResult* out,
        SaveReference* waiter, uintptr_t proxy, uintptr_t remote) {
    auto* field = reinterpret_cast<uintptr_t*>(context); remote = *field; *field = proxy;
    __try { return original(context, out, waiter); }
    __finally { *field = remote; }
}
}
SaveFuture* create_write_readback(Session& owner, uint64_t id, uintptr_t provider, uintptr_t identity,
        const std::string& directory, const ReadbackCalls& calls) {
    auto future = std::unique_ptr<ReadbackFuture>(new (std::nothrow) ReadbackFuture(owner, id, calls));
    if (!future) return nullptr;
    auto* control = static_cast<NativeControl*>(calls.native.allocate(sizeof(NativeControl)));
    if (!control) return nullptr;
    *control = {1, 1, 0, calls.native.destroy}; future->data.control = reinterpret_cast<uintptr_t>(control);
    void* object = calls.native.allocate(0x280);
    if (!object) return nullptr;
    calls.native.construct(object); control->object = reinterpret_cast<uintptr_t>(object);
    calls.native.catalog.set_name(control->object, directory.c_str());
    const int32_t loading = 1; const uint8_t enabled = 1;
    std::memcpy(static_cast<char*>(object) + 0x278, &loading, 4);
    std::memcpy(static_cast<char*>(object) + 0x27d, &enabled, 1);
    int32_t capacity = 0;
    std::memcpy(&capacity, static_cast<char*>(object) + 0x1cc, 4);
    if (!owner.native_writes.attach_readback(id, control->object, capacity)) return nullptr;
    SaveReference argument{}; calls.native.catalog.retain(&argument, &future->data);
    calls.native.read(provider, &future->native, identity, &argument);
    if (!future->native) return nullptr;
    future->vtable = &vtable; return future.release();
}
ReadWorkerResult* prepare_readback(Session& owner, engine::Memory& memory, uintptr_t context,
        ReadWorkerResult* out, SaveReference* waiter, PrepareRead original) {
    uintptr_t control = 0, data = 0;
    if (!at(memory, context, 8, control) || !at(memory, control, 8, data)) return original(context, out, waiter);
    const auto id = owner.native_writes.readback_operation(data);
    if (!id) return original(context, out, waiter);
    SdkWriteObservation manifest; uintptr_t remote = 0, table = 0; FileSize size = nullptr; bool valid = false;
    try {
        valid = owner.native_writes.readback_manifest(id, manifest) && at(memory, context, 0, remote) &&
            remote == manifest.remote && at(memory, remote, 0, table) && at(memory, table, 0x78, size) && size;
    } catch (const std::bad_alloc&) {}
    // A zero-size proxy result follows the native failure path and its consumed
    // weak waiter cleanup. No hand-built waiter release or allocation is needed.
    if (!valid) manifest.payloads.clear();
    SizeProxy proxy{size_table.data(), remote, size, manifest};
    return invoke_prepare(original, context, out, waiter, reinterpret_cast<uintptr_t>(&proxy), remote);
}
ReadWorkerResult* verify_readback(Session& owner, engine::Memory& memory, uintptr_t context,
        ReadWorkerResult* out, DecodeRead original, uintptr_t image) {
    uintptr_t control = 0, data = 0;
    if (!at(memory, context, 0, control) || !at(memory, control, 8, data)) return original(context, out);
    const auto id = owner.native_writes.readback_operation(data);
    if (!id) return original(context, out);
    bool valid = false; SdkWriteObservation manifest; std::vector<uintptr_t> buffers;
    try {
        uintptr_t files = 0; int32_t count = 0; uint8_t cancelled = 0;
        valid = owner.native_writes.readback_manifest(id, manifest) && at(memory, data, 0x1c0, files) && files &&
            at(memory, data, 0x1c8, count) && count > 0 && static_cast<size_t>(count) == manifest.payloads.size() &&
            at(memory, data, 0x27f, cancelled) && !cancelled;
        const auto deadline = GetTickCount64() + 1000;
        for (int32_t i = 0; valid && i < count; ++i) {
            uintptr_t stream = 0, table = 0, buffer = 0; uint64_t size = 0, capacity = 0;
            uint32_t flags = 0, status = 0; NativeString name{}; std::array<char, 260> text{};
            std::array<unsigned char, 32> digest{}; const auto& expected = manifest.payloads[static_cast<size_t>(i)];
            valid = at(memory, files, static_cast<size_t>(i) * 8, stream) && at(memory, stream, 0, table) &&
                table == image + 0x2a575a8 && at(memory, stream, 0x180, flags) && flags == 0x140 &&
                at(memory, stream, 0x184, status) && !status && at(memory, stream, 8, name) &&
                name.data && name.length > 0 && name.length < 260 &&
                !memory.copy(reinterpret_cast<uintptr_t>(name.data), text.data(), static_cast<size_t>(name.length) + 1).reason &&
                !text[static_cast<size_t>(name.length)] && !std::memchr(text.data(), 0, static_cast<size_t>(name.length)) &&
                manifest.directory + "/" + text.data() == expected.name.data() &&
                at(memory, stream, 0x150, size) && size == expected.size && at(memory, stream, 0x158, capacity) && size <= capacity &&
                at(memory, stream, 0x168, buffer) && digest_payload(memory, buffer, expected.size, digest, deadline) &&
                digest == expected.sha256;
            if (valid) buffers.push_back(buffer);
        }
    } catch (const std::bad_alloc&) { valid = false; }
    if (!valid) { owner.native_writes.readback_result(id, false); *out = {1, 1, 0}; return out; }
    owner.native_writes.readback_hashes(id);
    auto* result = original(context, out);
    if (!result->outcome && result->value == 1) {
        if (auto job = owner.native_writes.backup(id)) job->copy(owner, memory, manifest, buffers);
    }
    return result;
}
} // namespace sentinel::save
