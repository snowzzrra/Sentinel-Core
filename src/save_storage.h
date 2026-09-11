// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <array>
#include <vector>

namespace sentinel::storage {
// Explicit synthetic fixture identity, independent of product/protocol/process versions.
struct Identity {
    std::string seed;
    std::optional<uint32_t> team;
    std::optional<uint32_t> slot;
    std::string generation_fingerprint;
};
struct Descriptor { Identity identity; std::wstring root; };
enum class Outcome {
    ok, storage_prepared, storage_reopened, offline_backup_complete,
    invalid_descriptor, invalid_identity, invalid_root, unsafe_path,
    identity_mismatch, unsupported_manifest, corrupt_manifest,
    namespace_missing, interrupted_preparation, ownership_conflict, io_error, limit_exceeded,
    transport_backup_complete, payload_mismatch
};
struct Result {
    Outcome outcome = Outcome::ok;
    uint32_t win32_error = 0;
    bool ok() const;
};
const char* outcome_name(Outcome outcome);
struct Metadata {
    std::string namespace_id;
    std::wstring path;
    uint32_t schema = 1;
    // Preparation proves local metadata only. This component cannot attach a native writer.
    bool native_attached = false;
};
struct Backup {
    // Unique backup-attempt directory. A complete flushed receipt, not its name,
    // establishes completion. Also identifies retained partial output on failure.
    std::wstring path;
    uint32_t files = 0;
    uint64_t bytes = 0;
};
struct TransportFile {
    std::string name; // Native relative name, never a local output path.
    uint32_t size = 0;
    std::array<unsigned char, 32> sha256{};
};
struct TransportMetadata {
    std::string directory;
    uint32_t process_id = 0;
    uint64_t process_created = 0, operation_id = 0;
    std::vector<TransportFile> files;
};
// The native adapter owns the immutable streams for the entire synchronous call.
// Storage verifies the written bytes; native completion proof belongs to the adapter.
using ReadTransport = bool (*)(void*, size_t file, uint32_t offset, char*, uint32_t count);
class TransportArchive final {
public:
    ~TransportArchive();
    const TransportMetadata& metadata() const;
    Result read(size_t file, uint32_t offset, char*, uint32_t count);
private:
    struct Impl;
    explicit TransportArchive(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
    friend class Namespace;
};

// <=2048 bytes; ordered LF-terminated UTF-8 fields, no BOM/unknown/duplicate fields:
// sentinel-test-session-v1\nseed_hex=<lowercase UTF-8 hex>\nteam=<canonical decimal>\n
// slot=<positive canonical decimal>\ngeneration_fingerprint=<64 lowercase hex>\n
// provenance=synthetic-fixture\nroot=<existing absolute local Windows directory>\n
Result parse_descriptor(std::string_view text, Descriptor& descriptor);
// Shares the probe's bounded, pinned, local-file reader with prelaunch admission.
Result read_descriptor_file(const wchar_t* filename, Descriptor& descriptor);
Result namespace_id(const Identity& identity, std::string& id);

class Namespace final {
public:
    ~Namespace();
    Namespace(const Namespace&) = delete;
    Namespace& operator=(const Namespace&) = delete;
    const Metadata& metadata() const;
    // Requires this lifetime's exclusive namespace lease. No native writer can be
    // attached through this API; external writers must be stopped before opening it.
    // Copies a bounded, pinned offline file set, flushes files, then publishes a
    // create-only completion receipt after all payloads. Missing/malformed receipt
    // means incomplete. No rename, native completion or power-loss atomicity claim.
    // Root-level backup.receipt names (case-insensitive, file or directory) are
    // rejected from source payload before output creation; they cannot be imported.
    Result backup_offline(Backup& backup);
    // Explicit checkpoint of one campaign write. No automatic retention/purge.
    // Flat payload filenames are generated here; manifest publication is last.
    Result backup_transport(const TransportMetadata&, ReadTransport, void* source, Backup&);
    // Exact, bounded complete manifest and payload hashes; pins directories and
    // payload handles through later reads. Accepts only a generated basename.
    Result reopen_transport(std::wstring_view basename, std::unique_ptr<TransportArchive>&);
private:
    struct Impl;
    explicit Namespace(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend Result prepare(const Descriptor&, std::unique_ptr<Namespace>&);
    friend Result reopen(const Descriptor&, std::unique_ptr<Namespace>&);
};

// Root must already exist. New namespace creation is explicit; incomplete/unknown
// existing namespaces are never initialized, imported, reset, or repaired.
Result prepare(const Descriptor& descriptor, std::unique_ptr<Namespace>& result);
Result reopen(const Descriptor& descriptor, std::unique_ptr<Namespace>& result);
// Read-only: takes an existing lease without creating/modifying files; can report busy.
Result inspect(const Descriptor& descriptor, Metadata& metadata);
} // namespace sentinel::storage
