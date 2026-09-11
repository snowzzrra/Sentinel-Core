// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_backup.h"
#include "save_session.h"
#include "save_collector.h"
#include <windows.h>
#include <new>

namespace sentinel::save {
bool BackupJob::bind(uint64_t operation) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!operation || progress_.operation || progress_.state != BackupState::pending || !pid_ || !created_) return false;
    progress_.operation = operation; return true;
}
BackupProgress BackupJob::progress() const {
    std::lock_guard<std::mutex> guard(mutex_);
    auto out = progress_; out.cancel_requested = cancel_.load(std::memory_order_acquire); return out;
}
BackupFailure BackupJob::stopped() const {
    if (abandoned_.load(std::memory_order_acquire)) return BackupFailure::native;
    if (cancel_.load(std::memory_order_acquire)) return BackupFailure::cancelled;
    return GetTickCount64() >= deadline_ ? BackupFailure::deadline : BackupFailure::none;
}
void BackupJob::settle() {
    if (!progress_.readback_terminal || progress_.state == BackupState::copying) return;
    if (!progress_.readback_success) {
        progress_.state = BackupState::failed;
        if (progress_.failure == BackupFailure::none) progress_.failure = BackupFailure::native;
    } else if (progress_.storage_complete) progress_.state = BackupState::complete;
    else if (progress_.state == BackupState::pending) {
        // Nominal native read success without running the verified copy is not
        // backup success. No delayed request can attach to this consumed stage.
        progress_.state = BackupState::failed; progress_.failure = BackupFailure::native;
    }
}
void BackupJob::readback_finished(bool success) {
    std::lock_guard<std::mutex> guard(mutex_);
    progress_.readback_success = success && (!progress_.readback_terminal || progress_.readback_success);
    progress_.readback_terminal = true;
    if (!progress_.readback_success) abandoned_.store(true, std::memory_order_release);
    settle();
}
void BackupJob::copy(Session& owner, engine::Memory& memory, const SdkWriteObservation& manifest,
        const std::vector<uintptr_t>& buffers) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (progress_.state != BackupState::pending) return;
        if (!progress_.operation || progress_.operation != manifest.operation || buffers.size() != manifest.payloads.size()) {
            progress_.state = BackupState::failed; progress_.failure = BackupFailure::native; return;
        }
        progress_.state = BackupState::copying;
    }
    storage::Backup output; storage::Result result{}; bool attempted = false;
    auto failure = stopped();
    if (failure == BackupFailure::none) try {
        storage::TransportMetadata metadata;
        metadata.directory = manifest.directory; metadata.process_id = pid_;
        metadata.process_created = created_; metadata.operation_id = manifest.operation;
        const std::string prefix = manifest.directory + "/";
        for (const auto& file : manifest.payloads) {
            const std::string full(file.name.data());
            if (full.compare(0, prefix.size(), prefix)) { failure = BackupFailure::native; break; }
            metadata.files.push_back({full.substr(prefix.size()), file.size, file.sha256});
        }
        if (failure == BackupFailure::none) {
            struct Source {
                BackupJob& job; engine::Memory& memory;
                const std::vector<uintptr_t>& buffers;
                BackupFailure stopped = BackupFailure::none;
            } source{*this, memory, buffers};
            const auto read = [](void* opaque, size_t index, uint32_t offset, char* bytes, uint32_t count) -> bool {
                auto& reader = *static_cast<Source*>(opaque);
                reader.stopped = reader.job.stopped();
                // Storage owns the validated index/extent loop. These addresses
                // are the verified private read buffers, not old writer buffers.
                return reader.stopped == BackupFailure::none &&
                    !reader.memory.copy(reader.buffers[index] + offset, bytes, count).reason;
            };
            attempted = true;
            result = owner.lease_ ? owner.lease_->backup_transport(metadata, read, &source, output) :
                storage::Result{storage::Outcome::namespace_missing};
            if (!result.ok()) failure = source.stopped != BackupFailure::none ? source.stopped : BackupFailure::storage;
        }
    } catch (const std::bad_alloc&) {
        failure = BackupFailure::storage;
        result = {storage::Outcome::io_error, ERROR_NOT_ENOUGH_MEMORY};
    }
    std::lock_guard<std::mutex> guard(mutex_);
    progress_.storage_attempted = attempted; progress_.storage_result = result;
    progress_.storage_complete = attempted && failure == BackupFailure::none && result.outcome == storage::Outcome::transport_backup_complete;
    progress_.output = std::move(output); progress_.failure = failure;
    progress_.state = progress_.storage_complete ? BackupState::copied : BackupState::failed;
    // Cancellation arriving after publication can coexist with completed output.
    // Failed/abandoned native readback still prevents full request confirmation.
    settle();
}
} // namespace sentinel::save
