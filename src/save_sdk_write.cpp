// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_sdk_write.h"
#include "save_session.h"
#include "save_collector.h"
#include "save_write.h"
#include "save_backup.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <new>

namespace sentinel::save {
static_assert(sizeof(SdkWriteResult) == 32 && offsetof(SdkWriteResult, value) == 24);
namespace {
thread_local WritePollScope* polling = nullptr;
template<class T> bool at(engine::Memory& memory, uintptr_t p, size_t offset, T& out) {
    return p && p <= UINTPTR_MAX - offset && !memory.copy(p + offset, &out, sizeof(out)).reason;
}
bool native_name(engine::Memory& memory, uintptr_t p, std::string& name) {
    NativeString value{}; std::array<char, 64> text{};
    if (!at(memory, p, 0, value) || !value.data || value.length < 1 || value.length >= 64 ||
        memory.copy(reinterpret_cast<uintptr_t>(value.data), text.data(), static_cast<size_t>(value.length) + 1).reason ||
        text[static_cast<size_t>(value.length)] || std::memchr(text.data(), 0, static_cast<size_t>(value.length))) return false;
    name.assign(text.data(), static_cast<size_t>(value.length)); return true;
}
} // namespace
bool digest_payload(engine::Memory& memory, uintptr_t bytes, uint32_t size,
        std::array<unsigned char, 32>& out, uint64_t deadline) {
    // The selected SDK's 100 MiB file limit, plus a worker deadline between
    // guarded reads. Neither imposes a hard latency limit on a system call.
    if (size > 100u * 1024u * 1024u || (size && (!bytes || bytes > UINTPTR_MAX - size))) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    std::array<unsigned char, 65536> buffer{};
    for (uint32_t offset = 0; status >= 0 && offset < size;) {
        if (GetTickCount64() >= deadline) { status = -1; break; }
        const auto count = (std::min)(static_cast<uint32_t>(buffer.size()), size - offset);
        if (memory.copy(bytes + offset, buffer.data(), count).reason) { status = -1; break; }
        status = BCryptHashData(hash, buffer.data(), count, 0); offset += count;
    }
    if (status >= 0) status = BCryptFinishHash(hash, out.data(), static_cast<ULONG>(out.size()), 0);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0;
}
namespace {
using WriteAsync = uint64_t (*)(uintptr_t, const char*, const void*, uint32_t);
uintptr_t utilities(engine::Memory& memory, const SdkWriteCalls& calls) {
    uintptr_t initializer = 0, value = 0;
    if (!calls.context || !at(memory, calls.image_base, 0x3891468, initializer) ||
        initializer != calls.image_base + 0x6765c0) return 0;
    const auto context = calls.context(calls.image_base + 0x3891468);
    return at(memory, context, 0, value) ? value : 0;
}
struct RemoteProxy {
    const void* table;
    Session& owner; engine::Memory& memory;
    uintptr_t remote; WriteAsync write; uint64_t sequence;
};
uint64_t submit(uintptr_t self, const char* name, const void* bytes, uint32_t size) {
    auto& proxy = *reinterpret_cast<RemoteProxy*>(self);
    SdkFileWrite file{}; file.size = size; file.buffer = reinterpret_cast<uintptr_t>(bytes);
    bool named = false;
    if (name) for (size_t i = 0; i < file.name.size(); ++i) {
        if (proxy.memory.copy(reinterpret_cast<uintptr_t>(name) + i, &file.name[i], 1).reason) break;
        if (!file.name[i]) { named = i != 0; break; }
    }
    // Final buffers were hashed by their preflight worker. Native 141bd5b10
    // only reads them; require this exact buffer, extent and name at SDK issue.
    file.captured = named;
    const auto index = proxy.owner.native_writes.capture(proxy.sequence, file);
    // The scoped native writer remains the sole SDK issuer. A failed observation
    // cannot fabricate an SDK handle or abandon already-owned native execution.
    const auto handle = proxy.write(proxy.remote, name, bytes, size);
    proxy.owner.native_writes.submitted(proxy.sequence, index, handle);
    return handle;
}
// Only FileWriteAsync (+0x10) is used on this field by validated 141bd5b10.
const std::array<void*, 3> proxy_table{nullptr, nullptr, reinterpret_cast<void*>(submit)};
SdkWriteResult* invoke(PollSdkWrite original, uintptr_t context, SdkWriteResult* out,
        void* task, uintptr_t proxy, uintptr_t remote) {
    auto* field = reinterpret_cast<uintptr_t*>(context + 0x10);
    *field = proxy;
    // No borrowed proxy pointer may survive a native return or exception.
    __try { return original(context, out, task); }
    __finally { *field = remote; }
}
}
WritePollScope::WritePollScope(NativeWrites& owner, uint64_t operation)
    : owner_(owner), operation_(operation), previous_(polling) { polling = this; }
WritePollScope::~WritePollScope() { polling = previous_; }
uint64_t WritePollScope::current(const NativeWrites& owner) {
    return polling && &polling->owner_ == &owner ? polling->operation_ : 0;
}
NativeWriteOperation* NativeWrites::operation(uint64_t id) {
    for (auto& value : operations_) if (value.id == id) return &value;
    return nullptr;
}
uint64_t NativeWrites::open_provider(uintptr_t data, const std::string& directory) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (lost_ || !data || directory.empty() || directory.size() >= 64 || next_operation_ == UINT64_MAX) return 0;
    while (operations_.size() >= 64) {
        const auto old = std::find_if(operations_.begin(), operations_.end(), [](const auto& value) {
            return (!value.provider_alive || value.provider_terminal) && !value.preparation_jobs &&
                !value.preflight_jobs && !value.pending_handles && value.vector_released && !value.readback_data;
        });
        if (old == operations_.end()) return 0;
        const auto retired = old->id;
        writes_.erase(std::remove_if(writes_.begin(), writes_.end(),
            [retired](const auto& write) { return write.operation == retired; }), writes_.end());
        operations_.erase(old);
    }
    try {
        NativeWriteOperation value{}; value.id = next_operation_ + 1; value.data = data; value.directory = directory;
        operations_.push_back(std::move(value)); return ++next_operation_;
    } catch (const std::bad_alloc&) { return 0; }
}
void NativeWrites::close_provider(uint64_t id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (auto* value = operation(id)) {
        value->provider_alive = false;
        if (value->backup && (!value->provider_terminal || !value->readback_terminal)) value->backup->readback_finished(false);
    }
}
void NativeWrites::provider_result(uint64_t id, const SaveResult& result) {
    if (!id || result.state == -1) return;
    std::lock_guard<std::mutex> guard(mutex_);
    if (auto* value = operation(id)) {
        value->provider_terminal = true; value->provider_state = result.state;
        if (result.state == 0) { value->provider_outcome = result.outcome; value->provider_value = result.value; }
        if (value->backup && (result.state || result.outcome || result.value != 1)) value->backup->readback_finished(false);
    }
}
bool NativeWrites::inspect_operation(uint64_t id, NativeWriteOperation& out) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& value : operations_) if (value.id == id) { out = value; return true; }
    return false;
}
sc_save_write_snapshot NativeWrites::snapshot(uint64_t id) const {
    static_assert(sizeof(sc_save_write_snapshot) == 160 && offsetof(sc_save_write_snapshot, directory) == 80);
    std::lock_guard<std::mutex> guard(mutex_);
    sc_save_write_snapshot out{}; out.size = sizeof(out); out.abi_version = SC_SAVE_WRITE_ABI_VERSION;
    out.flags = lost_ ? SC_SAVE_WRITE_TRACKING_LOST : 0;
    out.operation_id = id; out.state = id ? SC_SAVE_WRITE_NOT_RETAINED : SC_SAVE_WRITE_NONE;
    const NativeWriteOperation* source = !id && !operations_.empty() ? &operations_.back() : nullptr;
    if (id) for (const auto& value : operations_) if (value.id == id) { source = &value; break; }
    if (!source) return out;
    out.operation_id = source->id; out.sdk_sequence = source->sdk_sequence;
    out.file_count = static_cast<uint32_t>(source->file_count); out.pending_handles = source->pending_handles;
    out.preparation_jobs = source->preparation_jobs; out.preflight_jobs = source->preflight_jobs;
    out.native_state = source->provider_state; out.native_outcome = source->provider_outcome; out.native_value = source->provider_value;
    std::memcpy(out.directory, source->directory.c_str(), source->directory.size() + 1);
    if (source->provider_alive) out.flags |= SC_SAVE_WRITE_PROVIDER_ALIVE;
    if (source->provider_terminal) out.flags |= SC_SAVE_WRITE_PROVIDER_TERMINAL;
    if (source->source_valid) out.flags |= SC_SAVE_WRITE_SOURCE_VALID;
    if (source->vector_released) out.flags |= SC_SAVE_WRITE_VECTOR_RELEASED;
    if (source->readback_required) out.flags |= SC_SAVE_WRITE_READBACK_REQUIRED;
    if (source->readback_data) out.flags |= SC_SAVE_WRITE_READBACK_ACTIVE;
    if (source->readback_hashes) out.flags |= SC_SAVE_WRITE_READBACK_HASHES;
    if (source->readback_terminal) out.flags |= SC_SAVE_WRITE_READBACK_TERMINAL;
    if (source->readback_failed) out.flags |= SC_SAVE_WRITE_READBACK_ERROR;
    for (const auto& write : writes_) if (write.operation == source->id) {
        out.submitted = write.submitted;
        bool prepared = !write.payloads.empty(), captured = prepared && write.submitted == write.payloads.size();
        bool callbacks = captured;
        for (const auto& file : write.payloads) {
            prepared &= file.prepared; captured &= file.captured;
            callbacks &= file.handle && file.completed && file.callback && !file.failed && file.sdk_result == 1;
            out.completed += file.completed ? 1u : 0u;
        }
        if (prepared) out.flags |= SC_SAVE_WRITE_PAYLOADS_PREPARED;
        if (captured) out.flags |= SC_SAVE_WRITE_PAYLOADS_CAPTURED;
        if (callbacks) out.flags |= SC_SAVE_WRITE_CALLBACKS_SUCCEEDED;
        if (write.terminal && !write.result.state && !write.result.outcome && write.result.detail == 1)
            out.flags |= SC_SAVE_WRITE_SDK_SUCCEEDED;
        if (write.unproven) out.flags |= SC_SAVE_WRITE_UNPROVEN;
        break;
    }
    if (!source->provider_terminal) out.state = source->provider_alive ? SC_SAVE_WRITE_PENDING : SC_SAVE_WRITE_INDETERMINATE;
    else if (source->provider_state || source->provider_outcome || source->provider_value != 1) out.state = SC_SAVE_WRITE_NATIVE_FAILED;
    else {
        constexpr auto proof = SC_SAVE_WRITE_PAYLOADS_PREPARED | SC_SAVE_WRITE_PAYLOADS_CAPTURED |
            SC_SAVE_WRITE_CALLBACKS_SUCCEEDED | SC_SAVE_WRITE_SDK_SUCCEEDED;
        out.state = (out.flags & proof) == proof && !(out.flags & (SC_SAVE_WRITE_UNPROVEN | SC_SAVE_WRITE_TRACKING_LOST)) ?
            SC_SAVE_WRITE_SDK_CONFIRMED : SC_SAVE_WRITE_NATIVE_SUCCEEDED;
        if (out.state == SC_SAVE_WRITE_SDK_CONFIRMED && source->readback_required)
            out.state = source->readback_failed ? SC_SAVE_WRITE_READBACK_FAILED : source->readback_terminal ?
                SC_SAVE_WRITE_READBACK_CONFIRMED : SC_SAVE_WRITE_READBACK_PENDING;
    }
    return out;
}
bool NativeWrites::source_matches(uint64_t id, uintptr_t data) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& value : operations_) if (value.id == id) return value.source_valid && value.data == data;
    return false;
}
void NativeWrites::invalidate_source(uintptr_t data) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& value : operations_) {
        if (value.data == data) value.source_valid = false;
        if (value.readback_data == data) {
            value.readback_data = 0;
            if (!value.readback_terminal) value.readback_failed = true;
        }
    }
}
bool NativeWrites::attach_readback(uint64_t id, uintptr_t data, int32_t capacity) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* value = operation(id);
    if (!value || !data || value->readback_required || capacity < 1 || capacity > 1024) return false;
    for (const auto& other : operations_) if (other.readback_data == data) return false;
    value->readback_data = data; value->readback_capacity = static_cast<uint32_t>(capacity);
    value->readback_required = true; return true;
}
uint64_t NativeWrites::readback_operation(uintptr_t data) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (data) for (const auto& value : operations_) if (value.readback_data == data) return value.id;
    return 0;
}
bool NativeWrites::readback_manifest(uint64_t id, SdkWriteObservation& out) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& value : operations_) if (value.id == id) {
        if (lost_ || !value.readback_data || value.readback_failed || !value.provider_terminal ||
            value.provider_state || value.provider_outcome || value.provider_value != 1) return false;
        for (const auto& write : writes_) if (write.operation == id) {
            if (write.unproven || !write.terminal || write.result.state || write.result.outcome ||
                write.result.detail != 1 || write.payloads.empty() || write.submitted != write.payloads.size()) return false;
            for (const auto& file : write.payloads) if (!file.prepared || !file.captured || !file.handle ||
                !file.completed || !file.callback || file.failed || file.sdk_result != 1) return false;
            out = write; return true;
        }
    }
    return false;
}
void NativeWrites::readback_hashes(uint64_t id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (auto* value = operation(id)) if (!value->readback_failed && !value->readback_terminal)
        value->readback_hashes = true;
}
void NativeWrites::readback_result(uint64_t id, bool success) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (auto* value = operation(id)) if (value->readback_required) {
        value->readback_failed |= !success || !value->readback_hashes;
        value->readback_terminal = true;
        if (value->backup) value->backup->readback_finished(!value->readback_failed);
    }
}
bool NativeWrites::request_backup(uint64_t id, const std::shared_ptr<BackupJob>& job) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* value = operation(id);
    if (!value || !job || value->backup || !value->provider_alive || value->provider_terminal ||
        value->readback_hashes || value->readback_terminal || !job->bind(id)) return false;
    value->backup = job; return true;
}
std::shared_ptr<BackupJob> NativeWrites::backup(uint64_t id) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& value : operations_) if (value.id == id) return value.backup;
    return {};
}
bool NativeWrites::attach_files(uint64_t id, uintptr_t files, uint64_t count) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* value = operation(id);
    if (!value || !value->source_valid || value->files || !files || !count || count > 1024) return false;
    for (const auto& other : operations_) if (other.files == files && !other.vector_released && !other.vector_retiring) return false;
    value->files = files; value->file_count = count; value->vector_released = false; return true;
}
bool NativeWrites::attach_job(uint64_t id, uintptr_t job, bool preflight) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* value = operation(id);
    if (!value || !job || jobs_.size() >= 128) return false;
    try {
        if (!jobs_.emplace(job, NativeWriteJob{id, preflight}).second) return false;
    } catch (const std::bad_alloc&) { return false; }
    if (preflight) ++value->preflight_jobs; else ++value->preparation_jobs;
    return true;
}
NativeWriteJob NativeWrites::detach_job(uintptr_t job) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found = jobs_.find(job);
    if (found == jobs_.end()) return {};
    const auto token = found->second; jobs_.erase(found); return token;
}
void NativeWrites::finish_job(NativeWriteJob token) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (auto* value = operation(token.operation)) {
        auto& count = token.preflight ? value->preflight_jobs : value->preparation_jobs;
        if (count) --count; else lost_ = true;
    }
}
void NativeWrites::complete(SdkWriteObservation& write, SdkFileWrite& file) {
    if (file.completed) return;
    file.completed = true;
    if (auto* value = operation(write.operation)) {
        if (value->pending_handles) --value->pending_handles; else lost_ = true;
    }
}
uint64_t NativeWrites::begin(uintptr_t remote, uintptr_t files, uint64_t count, const std::string& directory) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (lost_ || !remote || !files || !count || count > 1024 || next_ == UINT64_MAX) return 0;
    NativeWriteOperation* source = nullptr;
    for (auto& value : operations_) if (value.files == files && !value.vector_released && !value.vector_retiring) { source = &value; break; }
    if (!source || !source->source_valid || source->file_count != count ||
        !steam_name_equal(source->directory, directory)) return 0;
    for (const auto& write : writes_) if (!write.released && !write.retiring && write.files == files) return 0;
    // SDK evidence retires with its operation; vector release alone may precede
    // the provider result or a still-owned native job.
    if (writes_.size() >= 64) return 0;
    try {
        SdkWriteObservation value{}; value.remote = remote; value.files = files; value.directory = directory;
        value.operation = source->id;
        value.payloads.resize(static_cast<size_t>(count)); value.sequence = next_ + 1;
        writes_.push_back(std::move(value)); source->sdk_sequence = ++next_; return next_;
    } catch (const std::bad_alloc&) { return 0; }
}
bool NativeWrites::identify(uintptr_t files, uintptr_t remote, uint64_t count, const std::string& directory, uint64_t& sequence) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& write : writes_) if (!write.released && !write.retiring && write.files == files) {
        if (write.remote != remote || write.payloads.size() != count || !steam_name_equal(write.directory, directory)) return false;
        sequence = write.sequence; return true;
    }
    return false;
}
uint32_t NativeWrites::capture(uint64_t sequence, const SdkFileWrite& file) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& write : writes_) if (write.sequence == sequence && write.submitted < write.payloads.size()) {
        const auto index = write.submitted++;
        const auto& prepared = write.payloads[index];
        auto observed = file; observed.prepared = prepared.prepared;
        observed.captured = file.captured && prepared.prepared && file.buffer == prepared.buffer &&
            file.size == prepared.size && file.name == prepared.name;
        if (observed.captured) observed.sha256 = prepared.sha256;
        write.payloads[index] = observed; write.unproven |= !observed.captured; return index;
    }
    lost_ = true; return UINT32_MAX;
}
bool NativeWrites::prepared(uint64_t sequence, std::vector<SdkFileWrite> files) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& write : writes_) if (write.sequence == sequence && !write.submitted && !write.released &&
        !write.retiring && write.payloads.size() == files.size()) {
        const auto* value = operation(write.operation);
        // Native Load treats a zero size as missing. Refuse before deletion if
        // its required readback cannot represent an empty transport file.
        if (value && value->readback_required) {
            if (files.size() > value->readback_capacity) return false;
            for (const auto& file : files) if (!file.size) return false;
        }
        write.payloads = std::move(files); return true;
    }
    return false;
}
bool prepare_sdk_payloads(NativeWrites& writes, engine::Memory& memory, uint64_t sequence,
        uintptr_t files, uint64_t count, const std::string& directory, uintptr_t image) {
    const auto deadline = GetTickCount64() + 1000;
    if (!files || !count || count > 1024 || files > UINTPTR_MAX - count * 0x180) return false;
    std::vector<SdkFileWrite> captured; captured.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        const auto entry = files + i * 0x180;
        uintptr_t table = 0; uint64_t size = 0, capacity = 0; uint8_t owned = 0;
        NativeString name{}; std::array<char, 260> relative{}; SdkFileWrite file{};
        if (GetTickCount64() >= deadline || !at(memory, entry, 0, table) || table != image + 0x2a575a8 ||
            !at(memory, entry, 8, name) || !name.data || name.length < 1 || name.length >= 260 ||
            memory.copy(reinterpret_cast<uintptr_t>(name.data), relative.data(), static_cast<size_t>(name.length) + 1).reason ||
            relative[static_cast<size_t>(name.length)] || std::memchr(relative.data(), 0, static_cast<size_t>(name.length)) ||
            !at(memory, entry, 0x150, size) || size > 100u * 1024u * 1024u ||
            !at(memory, entry, 0x158, capacity) || size > capacity ||
            !at(memory, entry, 0x168, file.buffer) || !at(memory, entry, 0x178, owned) || (size && !owned)) return false;
        const auto full = directory + "/" + relative.data();
        if (full.size() >= file.name.size()) return false;
        for (const auto& previous : captured) if (steam_name_equal(previous.name.data(), full)) return false;
        std::memcpy(file.name.data(), full.c_str(), full.size() + 1); file.size = static_cast<uint32_t>(size);
        if (!digest_payload(memory, file.buffer, file.size, file.sha256, deadline)) return false;
        file.prepared = true; captured.push_back(file);
    }
    return GetTickCount64() < deadline && writes.prepared(sequence, std::move(captured));
}
void NativeWrites::submitted(uint64_t sequence, uint32_t index, uint64_t handle) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& write : writes_) if (write.sequence == sequence && index < write.submitted) {
        write.payloads[index].handle = handle;
        if (!handle) write.payloads[index].failed = true;
        else if (auto* value = operation(write.operation)) ++value->pending_handles;
        return;
    }
    lost_ = true;
}
void NativeWrites::callback(uint64_t handle, bool failed, int32_t result) {
    if (!handle) return;
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& write : writes_) for (uint32_t i = 0; i < write.submitted; ++i) {
        auto& file = write.payloads[i];
        if (file.handle != handle) continue;
        complete(write, file); file.failed = failed || result != 1;
        file.sdk_result = result; file.callback = true; return;
    }
}
void NativeWrites::utilities(uint64_t sequence, uintptr_t utilities) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& write : writes_) if (write.sequence == sequence && !write.utilities_captured) {
        write.utilities = utilities; write.utilities_captured = true; return;
    }
}
void NativeWrites::result(uint64_t sequence, const SdkWriteResult& value) {
    if (value.state == -1) return;
    SdkWriteResult observed{}; observed.state = value.state;
    if (value.state == 0) {
        observed.outcome = value.outcome;
        if (value.outcome == 0) observed.detail = *reinterpret_cast<const uint8_t*>(&value.detail);
        else if (value.outcome == 1) {
            observed.detail = value.detail;
            if (value.detail == 0) std::memcpy(&observed.value, &value.value, sizeof(uint32_t));
            else if (value.detail == 1) observed.value = value.value;
        }
    }
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& write : writes_) if (write.sequence == sequence) { write.terminal = true; write.result = observed; return; }
    lost_ = true;
}
uint64_t NativeWrites::detach_vector(uintptr_t files) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& value : operations_) if (value.files == files && !value.vector_released && !value.vector_retiring) {
        value.vector_retiring = true;
        for (auto& write : writes_) if (write.operation == value.id) write.retiring = true;
        return value.id;
    }
    return 0;
}
void NativeWrites::finish_vector(uint64_t id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (auto* value = operation(id)) { value->vector_released = true; value->vector_retiring = false; }
    for (auto& write : writes_) if (write.operation == id) { write.released = true; write.retiring = false; }
}
void NativeWrites::released(uintptr_t files) { finish_vector(detach_vector(files)); }
void NativeWrites::invalidate() { std::lock_guard<std::mutex> guard(mutex_); lost_ = true; }
bool NativeWrites::lost() const { std::lock_guard<std::mutex> guard(mutex_); return lost_; }
bool NativeWrites::inspect(uint64_t sequence, SdkWriteObservation& out) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& write : writes_) if (write.sequence == sequence) { out = write; return true; }
    return false;
}
void NativeWrites::poll_released(uintptr_t utilities, bool (*completed)(uintptr_t, uint64_t, bool*)) {
    // Capture one handle at a time; no Steam call runs under the trace mutex.
    uint64_t after = 0;
    for (unsigned checked = 0; checked < 64; ++checked) {
        uint64_t sequence = 0, handle = 0; uint32_t index = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            for (const auto& write : writes_) if (write.released && write.utilities == utilities && utilities)
                for (uint32_t i = 0; i < write.submitted; ++i) {
                const auto& file = write.payloads[i];
                if (file.handle > after && !file.completed && (!handle || file.handle < handle)) {
                    sequence = write.sequence; handle = file.handle; index = i;
                }
            }
        }
        if (!handle) break;
        after = handle; bool failed = false;
        if (!completed(utilities, handle, &failed)) continue;
        std::lock_guard<std::mutex> guard(mutex_);
        for (auto& write : writes_) if (write.sequence == sequence && index < write.submitted) {
            auto& file = write.payloads[index];
            if (!file.completed) { complete(write, file); file.failed = failed; }
        }
    }
}
namespace {
bool job_object(engine::Memory& memory, uintptr_t control, uintptr_t destructor, uintptr_t& object) {
    uintptr_t actual = 0;
    return at(memory, control, 8, object) && object && at(memory, control, 16, actual) && actual == destructor;
}
void lose_write(Session& owner) { owner.native_writes.invalidate(); owner.fail(SessionFault::native_write); }
}
uintptr_t* prepare_write_job(Session& owner, engine::Memory& memory, const SaveReference* source,
        uintptr_t* out, const SaveReference* identity, PrepareWriteJob original, uintptr_t image) {
    if (!owner.routed()) { owner.unrouted_import(); return original(source, out, identity); }
    const auto id = WritePollScope::current(owner.native_writes);
    auto* result = original(source, out, identity);
    uintptr_t tag = 0, control = 0, object = 0, data_control = 0, data = 0;
    // Returned ownership exists before any job can be scheduled by its poll.
    if (!at(memory, reinterpret_cast<uintptr_t>(out), 0, tag) ||
        !at(memory, reinterpret_cast<uintptr_t>(out), 8, control)) { lose_write(owner); return result; }
    if (tag != 0 || !control) return result; // Native failure has no job owner.
    if (!job_object(memory, control, image + 0x1bde610, object) ||
        !at(memory, object, 0, data_control) || !at(memory, data_control, 8, data) ||
        !owner.native_writes.source_matches(id, data) || !owner.native_writes.attach_job(id, object, false)) lose_write(owner);
    return result;
}
SaveFuture** create_write_context(Session& owner, engine::Memory& memory, SaveFuture** out,
        SaveReference* identity, const char* name, uint8_t clear, uint64_t* files,
        CreateWriteContext original, uintptr_t image) {
    if (!owner.routed()) { owner.unrouted_import(); return original(out, identity, name, clear, files); }
    const auto id = WritePollScope::current(owner.native_writes);
    auto* result = original(out, identity, name, clear, files);
    uintptr_t future = 0, table = 0, control = 0, object = 0;
    uint64_t state = 0; std::array<uint64_t, 3> vector{};
    if (!at(memory, reinterpret_cast<uintptr_t>(out), 0, future)) { lose_write(owner); return result; }
    if (!future) return result;
    if (!at(memory, future, 0, table) || table != image + 0x2e90628 ||
        !at(memory, future, 8, state) || state != 0 || !at(memory, future, 0x10, control) ||
        !job_object(memory, control, image + 0x1bd6e80, object) ||
        !owner.native_writes.attach_job(id, object, true)) { lose_write(owner); return result; }
    if (!at(memory, object, 0x40, vector) || vector[1] > vector[2] ||
        !owner.native_writes.attach_files(id, vector[0], vector[1])) lose_write(owner);
    return result;
}
void destroy_write_job(Session& owner, uintptr_t object, DestroySdkVector original) {
    const auto token = owner.native_writes.detach_job(object);
    original(object);
    // The address may already have been reused by another native constructor.
    // Retire the captured ID only after native payload/reference cleanup returns.
    owner.native_writes.finish_job(token);
}
SdkWriteResult* poll_sdk_write(Session& owner, engine::Memory& memory, uintptr_t context,
        SdkWriteResult* out, void* task, const SdkWriteCalls& calls) {
    if (!owner.routed()) { owner.unrouted_import(); return calls.poll(context, out, task); }
    uintptr_t remote = 0, files = 0, table = 0; uint64_t count = 0, sequence = 0;
    WriteAsync write = nullptr; std::string directory; bool valid = false;
    try {
        valid = context && context <= UINTPTR_MAX - 0x58 && at(memory, context, 0x10, remote) &&
            at(memory, context, 0x48, files) && at(memory, context, 0x50, count) &&
            native_name(memory, context + 0x18, directory) &&
            owner.native_writes.identify(files, remote, count, directory, sequence) &&
            at(memory, remote, 0, table) && at(memory, table, 0x10, write) && write;
    } catch (const std::bad_alloc&) {}
    if (!valid) {
        owner.native_writes.invalidate(); owner.fail(SessionFault::native_write);
        *out = {0, 1, 0, 1}; return out;
    }
    RemoteProxy proxy{proxy_table.data(), owner, memory, remote, write, sequence};
    owner.native_writes.utilities(sequence, utilities(memory, calls));
    const auto result = invoke(calls.poll, context, out, task, reinterpret_cast<uintptr_t>(&proxy), remote);
    owner.native_writes.result(sequence, *out);
    poll_released_sdk_writes(owner, memory, calls);
    return result;
}
void sdk_write_callback(Session& owner, engine::Memory& memory, uintptr_t callback,
        const int32_t* result, bool failed, SdkWriteCallback original) {
    uint32_t id = 0; uint64_t handle = 0; int32_t code = 0;
    if (owner.routed() && at(memory, callback, 0xc, id) && id == 0x533 &&
        at(memory, callback, 0x38, handle) && handle) {
        const bool readable = !failed && at(memory, reinterpret_cast<uintptr_t>(result), 0, code);
        owner.native_writes.callback(handle, !readable, code);
    }
    // Observe before the native callback's no-waiter early return. Never consume
    // an SDK result or replace the game's callback/waker handling.
    original(callback, result, failed);
}
void destroy_sdk_vector(Session& owner, engine::Memory& memory, uintptr_t vector, DestroySdkVector original) {
    uintptr_t files = 0;
    const bool tracked = owner.routed() && at(memory, vector, 0, files);
    const auto token = tracked && files ? owner.native_writes.detach_vector(files) : 0;
    original(vector);
    owner.native_writes.finish_vector(token);
}
void poll_released_sdk_writes(Session& owner, engine::Memory& memory, const SdkWriteCalls& calls) {
    if (!owner.routed() || !calls.context) return;
    const auto instance = utilities(memory, calls); uintptr_t table = 0;
    bool (*completed)(uintptr_t, uint64_t, bool*) = nullptr;
    if (at(memory, instance, 0, table) && at(memory, table, 0x58, completed) && completed)
        owner.native_writes.poll_released(instance, completed);
}
} // namespace sentinel::save
