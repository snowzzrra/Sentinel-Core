// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_storage.h"
#include "save_sdk_write.h"
#include <atomic>
#include <mutex>

namespace sentinel::save {
class Session;
enum class BackupState { pending, copying, copied, complete, failed };
enum class BackupFailure { none, cancelled, deadline, native, storage };
struct BackupProgress {
    BackupState state = BackupState::pending;
    BackupFailure failure = BackupFailure::none;
    uint64_t operation = 0;
    bool cancel_requested = false, readback_terminal = false, readback_success = false;
    bool storage_attempted = false, storage_complete = false;
    storage::Result storage_result{};
    storage::Backup output;
};
// One explicit request, attached before its provider factory starts. The native
// read worker owns its private SaveData throughout copy(); no engine pointer is
// retained in this request or its progress. Completion also needs the outer read.
class BackupJob final {
public:
    BackupJob(uint32_t pid, uint64_t created, uint64_t deadline)
        : pid_(pid), created_(created), deadline_(deadline) {}
    bool bind(uint64_t operation);
    void cancel() { cancel_.store(true, std::memory_order_release); }
    void readback_finished(bool success);
    BackupProgress progress() const;
    void copy(Session&, engine::Memory&, const SdkWriteObservation&, const std::vector<uintptr_t>& buffers);
private:
    BackupFailure stopped() const;
    void settle(); // mutex_ held; never completes a still-running copy.
    const uint32_t pid_;
    const uint64_t created_, deadline_;
    std::atomic<bool> cancel_{false}, abandoned_{false};
    mutable std::mutex mutex_;
    BackupProgress progress_;
};
} // namespace sentinel::save
