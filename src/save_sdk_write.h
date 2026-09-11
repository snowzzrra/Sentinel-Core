// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include "sentinel_save.h"

namespace sentinel::engine { struct Memory; }
namespace sentinel::save {
class Session;
class BackupJob;
struct SaveReference;
struct SaveFuture;
struct SaveResult;
// Internal SDK-stage variant. The provider later maps this to its 24-byte result.
struct SdkWriteResult { int64_t state, outcome; uint64_t detail, value; };
using PollSdkWrite = SdkWriteResult* (*)(uintptr_t, SdkWriteResult*, void*);
using SdkWriteCallback = void (*)(uintptr_t, const int32_t*, bool);
using DestroySdkVector = void (*)(uintptr_t);
struct SdkWriteCalls {
    PollSdkWrite poll = nullptr;
    uintptr_t image_base = 0;
    uintptr_t (*context)(uintptr_t) = nullptr;
};
struct SdkFileWrite {
    std::array<char, 260> name{};
    std::array<unsigned char, 32> sha256{};
    uint64_t handle = 0;
    uintptr_t buffer = 0;
    uint32_t size = 0;
    int32_t sdk_result = 0;
    bool prepared = false, captured = false, completed = false, failed = false, callback = false;
};
struct SdkWriteObservation {
    uint64_t sequence = 0, operation = 0;
    uintptr_t remote = 0, files = 0, utilities = 0;
    std::string directory;
    std::vector<SdkFileWrite> payloads;
    uint32_t submitted = 0;
    bool released = false, retiring = false, terminal = false, unproven = false, utilities_captured = false;
    SdkWriteResult result{};
};
struct NativeWriteOperation {
    uint64_t id = 0, sdk_sequence = 0;
    uintptr_t data = 0, files = 0;
    std::string directory;
    uint64_t file_count = 0;
    uint32_t preparation_jobs = 0, preflight_jobs = 0, pending_handles = 0;
    bool provider_alive = true, provider_terminal = false, source_valid = true, vector_released = true;
    bool vector_retiring = false;
    uintptr_t readback_data = 0;
    uint32_t readback_capacity = 0;
    bool readback_required = false, readback_hashes = false, readback_terminal = false, readback_failed = false;
    std::shared_ptr<BackupJob> backup;
    int64_t provider_state = 0, provider_outcome = 0;
    uint32_t provider_value = 0;
};
struct NativeWriteJob { uint64_t operation = 0; bool preflight = false; };
// Tracks the actual preflight vector and SDK submissions. It does not assert
// provider completion, gameplay reopen, global idle, or cloud quiescence.
class NativeWrites final {
public:
    uint64_t open_provider(uintptr_t data, const std::string& directory);
    void close_provider(uint64_t);
    void provider_result(uint64_t, const SaveResult&);
    bool inspect_operation(uint64_t, NativeWriteOperation&) const;
    sc_save_write_snapshot snapshot(uint64_t operation_id = 0) const;
    bool source_matches(uint64_t, uintptr_t data) const;
    void invalidate_source(uintptr_t data);
    bool attach_files(uint64_t, uintptr_t files, uint64_t count);
    bool attach_job(uint64_t, uintptr_t job, bool preflight);
    NativeWriteJob detach_job(uintptr_t job);
    void finish_job(NativeWriteJob);
    uint64_t begin(uintptr_t remote, uintptr_t files, uint64_t count, const std::string& directory);
    bool identify(uintptr_t files, uintptr_t remote, uint64_t count, const std::string&, uint64_t& sequence);
    bool prepared(uint64_t sequence, std::vector<SdkFileWrite>);
    uint32_t capture(uint64_t sequence, const SdkFileWrite&);
    void submitted(uint64_t sequence, uint32_t index, uint64_t handle);
    void utilities(uint64_t sequence, uintptr_t);
    void callback(uint64_t handle, bool failed, int32_t result);
    void result(uint64_t sequence, const SdkWriteResult&);
    uint64_t detach_vector(uintptr_t files);
    void finish_vector(uint64_t operation);
    void released(uintptr_t files);
    void invalidate();
    bool lost() const;
    bool inspect(uint64_t sequence, SdkWriteObservation&) const;
    void poll_released(uintptr_t utilities, bool (*completed)(uintptr_t, uint64_t, bool*));
    bool attach_readback(uint64_t operation, uintptr_t private_data, int32_t capacity);
    uint64_t readback_operation(uintptr_t private_data) const;
    bool readback_manifest(uint64_t operation, SdkWriteObservation&) const;
    void readback_hashes(uint64_t operation);
    void readback_result(uint64_t operation, bool success);
    bool request_backup(uint64_t operation, const std::shared_ptr<BackupJob>&);
    std::shared_ptr<BackupJob> backup(uint64_t operation) const;
private:
    // Helpers below run only under mutex_.
    NativeWriteOperation* operation(uint64_t);
    void complete(SdkWriteObservation&, SdkFileWrite&);
    mutable std::mutex mutex_;
    std::deque<SdkWriteObservation> writes_;
    std::deque<NativeWriteOperation> operations_;
    std::map<uintptr_t, NativeWriteJob> jobs_;
    uint64_t next_ = 0, next_operation_ = 0;
    bool lost_ = false;
};
// Lexical ownership of the exact native child being polled. Nothing from this
// stack scope is retained by a job; constructors copy its stable operation ID.
class WritePollScope final {
public:
    WritePollScope(NativeWrites&, uint64_t);
    ~WritePollScope();
    WritePollScope(const WritePollScope&) = delete;
    WritePollScope& operator=(const WritePollScope&) = delete;
    static uint64_t current(const NativeWrites&);
private:
    const NativeWrites& owner_;
    uint64_t operation_;
    WritePollScope* previous_;
};
using PrepareWriteJob = uintptr_t* (*)(const SaveReference*, uintptr_t*, const SaveReference*);
using CreateWriteContext = SaveFuture** (*)(SaveFuture**, SaveReference*, const char*, uint8_t, uint64_t*);
uintptr_t* prepare_write_job(Session&, engine::Memory&, const SaveReference*, uintptr_t*, const SaveReference*, PrepareWriteJob, uintptr_t image);
SaveFuture** create_write_context(Session&, engine::Memory&, SaveFuture**, SaveReference*, const char*, uint8_t, uint64_t*, CreateWriteContext, uintptr_t image);
void destroy_write_job(Session&, uintptr_t, DestroySdkVector);
// Runs on the native preflight worker, before deletion. Prepared files are
// privately owned until vector teardown; SDK polling only reads these buffers.
bool digest_payload(engine::Memory&, uintptr_t bytes, uint32_t size,
    std::array<unsigned char, 32>&, uint64_t deadline);
bool prepare_sdk_payloads(NativeWrites&, engine::Memory&, uint64_t sequence,
    uintptr_t files, uint64_t count, const std::string& directory, uintptr_t image);
SdkWriteResult* poll_sdk_write(Session&, engine::Memory&, uintptr_t, SdkWriteResult*, void*, const SdkWriteCalls&);
void sdk_write_callback(Session&, engine::Memory&, uintptr_t, const int32_t*, bool, SdkWriteCallback);
void destroy_sdk_vector(Session&, engine::Memory&, uintptr_t, DestroySdkVector);
// Non-consuming SteamUtils009 query, only for handles whose native vector was
// released. False/invalid/unavailable never establishes completion.
void poll_released_sdk_writes(Session&, engine::Memory&, const SdkWriteCalls&);
} // namespace sentinel::save
