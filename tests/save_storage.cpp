// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Synthetic task-owned files only. No game/native serializer or real AP seed proof.
#include "save_storage.h"
#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL save_storage line %d: %s (win32=%lu)\n", \
    __LINE__, #c, GetLastError()); std::exit(1); } } while (0)
using namespace sentinel::storage;
namespace fs = std::filesystem;
namespace {
constexpr char expected_id[] = "5d4027889fe4c7d02584939525ef12fddf41770f2ea1b795d39249044257b08e";
Descriptor descriptor(const std::wstring& root) {
    return {{"fixture-seed", 0, 1, std::string(64, 'a')}, root};
}
void write(const std::wstring& path, const std::string& data) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    CHECK(WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr));
    CHECK(written == data.size());
    CHECK(CloseHandle(file));
}
std::string read(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    std::array<char, 4096> buffer{};
    DWORD count = 0;
    CHECK(ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr));
    CHECK(CloseHandle(file));
    return std::string(buffer.data(), count);
}
std::wstring path_for(const Descriptor& value) {
    std::string id;
    CHECK(namespace_id(value.identity, id).ok());
    return value.root + L"\\" + std::wstring(id.begin(), id.end());
}
struct Fixture {
    std::wstring path;
    Fixture() {
        std::array<wchar_t, MAX_PATH> temp{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
        CHECK(length > 3 && length < temp.size());
        path = std::wstring(temp.data(), length) + L"sentinel-storage-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64());
        CHECK(path.size() < 100 && CreateDirectoryW(path.c_str(), nullptr));
        CHECK(CreateDirectoryW((path + L"\\ap").c_str(), nullptr));
        CHECK(CreateDirectoryW((path + L"\\mock-vanilla").c_str(), nullptr));
        write(path + L"\\mock-vanilla\\protected.txt", "SYNTHETIC PROTECTED FIXTURE; NEVER A DOOM SAVE");
    }
    ~Fixture() {
        CHECK(read(path + L"\\mock-vanilla\\protected.txt") == "SYNTHETIC PROTECTED FIXTURE; NEVER A DOOM SAVE");
        // All links are removed explicitly by the attack tests before cleanup.
        // Verify this exact newly created temporary target before recursive removal.
        const auto absolute = fs::absolute(path).lexically_normal();
        CHECK(absolute == fs::path(path) && absolute.filename().wstring().find(L"sentinel-storage-") == 0);
        std::error_code error;
        fs::remove_all(absolute, error);
        CHECK(!error);
    }
};
void junction(const std::wstring& link, const std::wstring& target) {
    CHECK(CreateDirectoryW(link.c_str(), nullptr));
    const HANDLE directory = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    CHECK(directory != INVALID_HANDLE_VALUE);
    const std::wstring substitute = L"\\??\\" + target;
    const size_t chars = substitute.size() + 1 + target.size() + 1;
    std::vector<unsigned char> buffer(16 + chars * sizeof(wchar_t), 0);
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    const WORD data_length = static_cast<WORD>(buffer.size() - 8);
    const WORD substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    const WORD print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    const WORD print_length = static_cast<WORD>(target.size() * sizeof(wchar_t));
    std::memcpy(buffer.data(), &tag, sizeof(tag));
    std::memcpy(buffer.data() + 4, &data_length, sizeof(data_length));
    std::memcpy(buffer.data() + 10, &substitute_length, sizeof(substitute_length));
    std::memcpy(buffer.data() + 12, &print_offset, sizeof(print_offset));
    std::memcpy(buffer.data() + 14, &print_length, sizeof(print_length));
    std::memcpy(buffer.data() + 16, substitute.data(), substitute_length);
    std::memcpy(buffer.data() + 16 + print_offset, target.data(), print_length);
    DWORD returned = 0;
    CHECK(DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()),
        nullptr, 0, &returned, nullptr));
    CHECK(CloseHandle(directory));
}
void child(const wchar_t* mode, const std::wstring& root) {
    std::array<wchar_t, 512> exe{};
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    CHECK(length && length < exe.size());
    std::wstring command = L"\"" + std::wstring(exe.data()) + L"\" " + mode + L" \"" + root + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
        &startup, &process));
    CHECK(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
    DWORD exit_code = 1;
    CHECK(GetExitCodeProcess(process.hProcess, &exit_code) && exit_code == 0);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
void identities_and_descriptor() {
    auto a = descriptor(L"C:\\synthetic");
    std::string id, changed;
    CHECK(namespace_id(a.identity, id).ok() && id == expected_id);
    for (unsigned field = 0; field < 4; ++field) {
        auto b = a;
        if (field == 0) b.identity.seed += "-other";
        if (field == 1) b.identity.team = 1;
        if (field == 2) b.identity.slot = 2;
        if (field == 3) b.identity.generation_fingerprint[0] = 'b';
        CHECK(namespace_id(b.identity, changed).ok() && changed != id);
    }
    auto missing = a;
    missing.identity.team.reset(); CHECK(namespace_id(missing.identity, changed).outcome == Outcome::invalid_identity);
    missing = a; missing.identity.slot.reset(); CHECK(!namespace_id(missing.identity, changed).ok());
    missing = a; missing.identity.seed.clear(); CHECK(!namespace_id(missing.identity, changed).ok());
    missing = a; missing.identity.generation_fingerprint.clear(); CHECK(!namespace_id(missing.identity, changed).ok());
    const std::string text = "sentinel-test-session-v1\nseed_hex=666978747572652d73656564\nteam=0\nslot=1\n"
        "generation_fingerprint=" + std::string(64, 'a') + "\nprovenance=synthetic-fixture\nroot=C:\\synthetic\n";
    Descriptor parsed;
    CHECK(parse_descriptor(text, parsed).ok() && parsed.root == a.root);
    CHECK(namespace_id(parsed.identity, changed).ok() && changed == id);
    CHECK(!parse_descriptor(text + "root=C:\\other\n", parsed).ok());
    CHECK(!parse_descriptor(text.substr(0, text.size() - 1), parsed).ok());
    CHECK(!parse_descriptor(std::string(2049, 'a'), parsed).ok());
    auto malformed = text;
    malformed.replace(malformed.find("team=0"), 6, "team=00"); CHECK(!parse_descriptor(malformed, parsed).ok());
    malformed = text;
    malformed.replace(malformed.find("slot=1"), 6, "slot=0"); CHECK(!parse_descriptor(malformed, parsed).ok());
    malformed = text;
    malformed.replace(malformed.find("team=0"), 6, "team=4294967296"); CHECK(!parse_descriptor(malformed, parsed).ok());
    malformed = text;
    malformed.replace(malformed.find("seed_hex="), 33, "seed_hex=c080"); CHECK(!parse_descriptor(malformed, parsed).ok());
    malformed = text;
    malformed.replace(malformed.find("synthetic-fixture"), 17, "live-room"); CHECK(!parse_descriptor(malformed, parsed).ok());
}
struct TransportSource {
    std::vector<std::string> bytes{"abc", "synthetic transport payload"};
    Backup* attempt = nullptr;
    int fail_file = -1;
    static bool copy(void* value, size_t index, uint32_t offset, char* out, uint32_t count) {
        auto& self = *static_cast<TransportSource*>(value);
        CHECK(!fs::exists(self.attempt->path + L"\\transport.manifest"));
        if (index == 1) {
            const HANDLE writer = CreateFileW((self.attempt->path + L"\\payload-0.bin").c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            CHECK(writer == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
        }
        if (self.fail_file == static_cast<int>(index)) return false;
        CHECK(index < self.bytes.size() && offset + count <= self.bytes[index].size());
        std::memcpy(out, self.bytes[index].data() + offset, count); return true;
    }
};
TransportMetadata transport_metadata(const Namespace& owner, const TransportSource& source) {
    TransportMetadata metadata;
    metadata.directory = "ap-" + owner.metadata().namespace_id.substr(0, 40) + "/DLC2-AUTOSAVE11";
    metadata.process_id = GetCurrentProcessId(); metadata.process_created = 123456; metadata.operation_id = UINT64_MAX;
    for (size_t i = 0; i < source.bytes.size(); ++i) {
        TransportFile file; file.name = i == 0 ? "game.details" : "nested/SlotFile";
        file.size = static_cast<uint32_t>(source.bytes[i].size());
        BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
        CHECK(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0);
        CHECK(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0);
        CHECK(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(source.bytes[i].data())), file.size, 0) >= 0);
        CHECK(BCryptFinishHash(hash, file.sha256.data(), static_cast<ULONG>(file.sha256.size()), 0) >= 0);
        BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
        metadata.files.push_back(file);
    }
    return metadata;
}
void transport_backups(Fixture& fixture) {
    auto value = descriptor(fixture.path + L"\\ap"); value.identity.slot = 40;
    std::unique_ptr<Namespace> owner; CHECK(prepare(value, owner).ok());
    Backup attempt; TransportSource source; source.attempt = &attempt;
    auto metadata = transport_metadata(*owner, source);
    const auto save = [&] { return owner->backup_transport(metadata, &TransportSource::copy, &source, attempt); };
    CHECK(save().outcome == Outcome::transport_backup_complete);
    CHECK(attempt.files == 2 && attempt.bytes == source.bytes[0].size() + source.bytes[1].size());
    const auto complete = attempt.path, basename = fs::path(complete).filename().wstring();
    const auto manifest_path = complete + L"\\transport.manifest";
    const auto original = read(manifest_path);
    CHECK(original.find("write_operation=18446744073709551615\n") != std::string::npos);
    CHECK(!fs::exists(complete + L"\\nested"));
    std::unique_ptr<TransportArchive> archive;
    CHECK(owner->reopen_transport(basename, archive).ok());
    CHECK(archive->metadata().files[1].name == "nested/SlotFile" && archive->metadata().operation_id == UINT64_MAX);
    std::array<char, 64> buffer{};
    CHECK(archive->read(0, 1, buffer.data(), 2).ok() && std::string(buffer.data(), 2) == "bc");
    CHECK(!archive->read(0, 2, buffer.data(), 2).ok() && !archive->read(2, 0, buffer.data(), 1).ok());
    for (const auto& file : {manifest_path, complete + L"\\payload-0.bin"}) {
        const HANDLE writer = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(writer == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
        CHECK(!DeleteFileW(file.c_str()) && GetLastError() == ERROR_SHARING_VIOLATION);
    }
    CHECK(!MoveFileW(complete.c_str(), (complete + L"-moved").c_str()));
    archive.reset();
    source.fail_file = 1; CHECK(save().outcome == Outcome::io_error);
    CHECK(attempt.files == 1 && !fs::exists(attempt.path + L"\\transport.manifest"));
    CHECK(owner->reopen_transport(fs::path(attempt.path).filename().wstring(), archive).outcome == Outcome::interrupted_preparation);
    source.fail_file = -1; metadata.files[0].sha256[0] ^= 1;
    CHECK(save().outcome == Outcome::payload_mismatch && !fs::exists(attempt.path + L"\\transport.manifest"));
    metadata = transport_metadata(*owner, source);
    for (int mode = 0; mode < 8; ++mode) {
        auto bad = metadata;
        if (mode == 0) bad.directory = "PROFILE";
        if (mode == 1) bad.directory.back() = '2'; // Native slots end at 11.
        if (mode == 2) bad.files[1].name = "GAME.DETAILS";
        if (mode == 3) bad.files[0].name = "../game.details";
        if (mode == 4) bad.files[0].size = 0;
        if (mode == 5) bad.files[0].size = UINT32_MAX;
        if (mode == 6) bad.files.resize(17, bad.files[0]);
        if (mode == 7) bad.process_created = 0;
        CHECK(!owner->backup_transport(bad, &TransportSource::copy, &source, attempt).ok() && attempt.path.empty());
    }
    CHECK(read(manifest_path) == original && read(complete + L"\\payload-0.bin") == "abc");
    for (const auto& path : {L"../escape", L"C:\\escape", L"transport-backup-invalid"})
        CHECK(owner->reopen_transport(path, archive).outcome == Outcome::unsafe_path && !archive);
    write(complete + L"\\payload-0.bin", "abd");
    CHECK(owner->reopen_transport(basename, archive).outcome == Outcome::payload_mismatch && !archive);
    write(complete + L"\\payload-0.bin", "abc");
    for (const auto& extra : {L"owner.lock", L"extra.bin"}) {
        write(complete + L"\\" + extra, "unexpected");
        CHECK(owner->reopen_transport(basename, archive).outcome == Outcome::corrupt_manifest && !archive);
        CHECK(DeleteFileW((complete + L"\\" + extra).c_str()));
    }
    for (int mode = 0; mode < 5; ++mode) {
        auto text = original;
        if (mode == 0) text.replace(0, 28, "sentinel-transport-backup-v9");
        if (mode == 1) text += "unknown=1\n";
        if (mode == 2) text.replace(text.find("state=complete"), 14, "state=incomplete");
        if (mode == 3) text.replace(text.find("files=2"), 7, "files=02");
        if (mode == 4) text += std::string(32768, 'x');
        write(manifest_path, text);
        CHECK(!owner->reopen_transport(basename, archive).ok() && !archive);
    }
    auto other = value; other.identity.team = 1; std::unique_ptr<Namespace> foreign;
    CHECK(prepare(other, foreign).ok());
    auto foreign_metadata = transport_metadata(*foreign, source);
    CHECK(foreign->backup_transport(foreign_metadata, &TransportSource::copy, &source, attempt).ok());
    write(manifest_path, read(attempt.path + L"\\transport.manifest"));
    CHECK(owner->reopen_transport(basename, archive).outcome == Outcome::identity_mismatch && !archive);
    write(manifest_path, original);
    write(complete + L"\\payload-0.bin:hidden", "unexpected ADS");
    CHECK(owner->reopen_transport(basename, archive).outcome == Outcome::unsafe_path && !archive);
    CHECK(DeleteFileW((complete + L"\\payload-0.bin:hidden").c_str()));
    CHECK(DeleteFileW((complete + L"\\payload-0.bin").c_str()));
    CHECK(CreateHardLinkW((complete + L"\\payload-0.bin").c_str(), (fixture.path + L"\\mock-vanilla\\protected.txt").c_str(), nullptr));
    CHECK(owner->reopen_transport(basename, archive).outcome == Outcome::unsafe_path && !archive);
    CHECK(DeleteFileW((complete + L"\\payload-0.bin").c_str()));
    write(complete + L"\\payload-0.bin", "abc");
    CHECK(owner->reopen_transport(basename, archive).ok());
    owner.reset(); // Archive independently pins its read set through later recovery reads.
    CHECK(archive->read(0, 0, buffer.data(), 3).ok() && std::string(buffer.data(), 3) == "abc");
}
void lifecycle_and_backup(Fixture& f) {
    const auto a = descriptor(f.path + L"\\ap");
    Metadata info;
    const auto inspected = inspect(a, info);
    if (inspected.outcome != Outcome::namespace_missing)
        std::fprintf(stderr, "initial inspect outcome=%s error=%u\n", outcome_name(inspected.outcome), inspected.win32_error);
    CHECK(inspected.outcome == Outcome::namespace_missing && info.path.empty());
    std::unique_ptr<Namespace> owner, competing;
    const auto prepared = prepare(a, owner);
    if (prepared.outcome != Outcome::storage_prepared)
        std::fprintf(stderr, "initial prepare outcome=%s error=%u\n", outcome_name(prepared.outcome), prepared.win32_error);
    CHECK(prepared.outcome == Outcome::storage_prepared && owner && !owner->metadata().native_attached);
    const auto path = owner->metadata().path;
    CHECK(prepare(a, competing).outcome == Outcome::ownership_conflict && !competing);
    CHECK(inspect(a, info).outcome == Outcome::ownership_conflict);
    child(L"--busy", a.root);
    CHECK(!MoveFileW(a.root.c_str(), (a.root + L"-moved").c_str()));
    CHECK(!MoveFileW(path.c_str(), (path + L"-moved").c_str()));
    for (const auto& held_directory : {a.root, path}) {
        // FSCTL_SET_REPARSE_POINT needs a writable directory handle. Admission
        // must block acquiring one for the entire lease, not just reject an
        // already installed junction in the initial validation.
        const HANDLE attacker = CreateFileW(held_directory.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        CHECK(attacker == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
    }
    write(path + L"\\synthetic-payload.bin", "OFFLINE SYNTHETIC PAYLOAD A");
    CHECK(CreateDirectoryW((path + L"\\details").c_str(), nullptr));
    write(path + L"\\details\\index.bin", "SYNTHETIC INDEX");
    write(path + L"\\details\\backup.receipt", "SYNTHETIC NESTED PAYLOAD");
    const auto writer = CreateFileW((path + L"\\synthetic-payload.bin").c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(writer != INVALID_HANDLE_VALUE);
    Backup backup;
    CHECK(owner->backup_offline(backup).outcome == Outcome::ownership_conflict && backup.path.empty());
    CHECK(CloseHandle(writer));
    CHECK(owner->backup_offline(backup).outcome == Outcome::offline_backup_complete);
    CHECK(backup.files == 4 && backup.bytes > 30 && backup.path.find(a.root + L"\\backup-attempt-") == 0);
    CHECK(read(backup.path + L"\\synthetic-payload.bin") == "OFFLINE SYNTHETIC PAYLOAD A");
    CHECK(read(backup.path + L"\\details\\index.bin") == "SYNTHETIC INDEX");
    CHECK(read(backup.path + L"\\details\\backup.receipt") == "SYNTHETIC NESTED PAYLOAD");
    CHECK(read(backup.path + L"\\session.manifest") == read(path + L"\\session.manifest"));
    const auto completed_receipt = read(backup.path + L"\\backup.receipt");
    CHECK(completed_receipt.find("native_completion=unproven") != std::string::npos);
    CHECK(completed_receipt.find("\nstate=complete\n") != std::string::npos);
    CHECK(!fs::exists(backup.path + L"\\owner.lock"));
    owner.reset();
    CHECK(inspect(a, info).ok() && info.namespace_id == expected_id && !info.native_attached);
    child(L"--reopen", a.root);
    CHECK(reopen(a, owner).outcome == Outcome::storage_reopened);
    auto b = a; b.identity.slot = 2;
    CHECK(prepare(b, competing).outcome == Outcome::storage_prepared);
    CHECK(competing->metadata().path != path && !fs::exists(competing->metadata().path + L"\\synthetic-payload.bin"));
    // A valid older receipt must never enter an output as ordinary root payload,
    // including Windows case aliases and directories using the reserved name.
    const auto root_entries = std::distance(fs::directory_iterator(a.root), fs::directory_iterator());
    for (const auto* name : {L"backup.receipt", L"BACKUP.RECEIPT"}) {
        const auto reserved = path + L"\\" + name;
        write(reserved, completed_receipt);
        CHECK(owner->backup_offline(backup).outcome == Outcome::unsafe_path && backup.path.empty());
        CHECK(std::distance(fs::directory_iterator(a.root), fs::directory_iterator()) == root_entries);
        CHECK(read(reserved) == completed_receipt);
        CHECK(DeleteFileW(reserved.c_str()));
        CHECK(CreateDirectoryW(reserved.c_str(), nullptr));
        CHECK(owner->backup_offline(backup).outcome == Outcome::unsafe_path && backup.path.empty());
        CHECK(std::distance(fs::directory_iterator(a.root), fs::directory_iterator()) == root_entries);
        CHECK(RemoveDirectoryW(reserved.c_str()));
    }
}
void malformed_and_partial(Fixture& f) {
    auto a = descriptor(f.path + L"\\ap");
    a.identity.slot = 20;
    const auto path = path_for(a);
    CHECK(CreateDirectoryW(path.c_str(), nullptr));
    std::unique_ptr<Namespace> owner;
    CHECK(prepare(a, owner).outcome == Outcome::interrupted_preparation);
    CHECK(!fs::exists(path + L"\\owner.lock"));
    write(path + L"\\owner.lock", "");
    CHECK(prepare(a, owner).outcome == Outcome::interrupted_preparation);
    CHECK(!fs::exists(path + L"\\session.manifest"));
    write(path + L"\\session.manifest", "sentinel-ap-storage-v1\nnamespace=");
    CHECK(prepare(a, owner).outcome == Outcome::corrupt_manifest);
    CHECK(read(path + L"\\session.manifest") == "sentinel-ap-storage-v1\nnamespace=");
    write(path + L"\\session.manifest", "sentinel-ap-storage-v9\n");
    CHECK(reopen(a, owner).outcome == Outcome::unsupported_manifest);
    CHECK(read(path + L"\\session.manifest") == "sentinel-ap-storage-v9\n");
    write(path + L"\\session.manifest", "CORRUPT SYNTHETIC MANIFEST");
    CHECK(prepare(a, owner).outcome == Outcome::corrupt_manifest);
    write(path + L"\\session.manifest", read(path_for(descriptor(a.root)) + L"\\session.manifest"));
    CHECK(reopen(a, owner).outcome == Outcome::identity_mismatch);
    for (const auto& root : {L"relative", L"C:relative", L"\\\\server\\share", L"C:\\x\\..\\y",
        L"C:\\x\\.\\y", L"C:\\x\\CON", L"C:\\x\\trailing.", L"C:\\x\\stream:ads", L"\\\\?\\C:\\x"}) {
        auto invalid = a; invalid.root = root;
        CHECK(prepare(invalid, owner).outcome == Outcome::invalid_root && !owner);
    }
    auto missing = a; missing.root += L"\\missing";
    Metadata info;
    CHECK(inspect(missing, info).outcome == Outcome::io_error);
}
void redirect_and_links(Fixture& f) {
    const auto protected_path = f.path + L"\\mock-vanilla";
    auto a = descriptor(f.path + L"\\root-link");
    junction(a.root, protected_path);
    std::unique_ptr<Namespace> owner;
    CHECK(prepare(a, owner).outcome == Outcome::unsafe_path);
    CHECK(RemoveDirectoryW(a.root.c_str()));
    a = descriptor(f.path + L"\\ap"); a.identity.slot = 30;
    const auto namespace_path = path_for(a);
    junction(namespace_path, protected_path);
    CHECK(prepare(a, owner).outcome == Outcome::unsafe_path);
    CHECK(RemoveDirectoryW(namespace_path.c_str()));
    CHECK(prepare(a, owner).outcome == Outcome::storage_prepared);
    owner.reset();
    // Install each preexisting attack while unowned. Post-admission write handles
    // are independently rejected by the held-directory test above.
    const auto payload_link = namespace_path + L"\\payload-link";
    junction(payload_link, protected_path);
    Backup backup;
    CHECK(reopen(a, owner).outcome == Outcome::storage_reopened);
    CHECK(owner->backup_offline(backup).outcome == Outcome::unsafe_path);
    owner.reset();
    CHECK(RemoveDirectoryW(payload_link.c_str()));
    const auto hardlink = namespace_path + L"\\hardlink.bin";
    CHECK(CreateHardLinkW(hardlink.c_str(), (protected_path + L"\\protected.txt").c_str(), nullptr));
    CHECK(reopen(a, owner).outcome == Outcome::storage_reopened);
    CHECK(owner->backup_offline(backup).outcome == Outcome::unsafe_path);
    owner.reset();
    CHECK(DeleteFileW(hardlink.c_str()));
    const auto streamed = namespace_path + L"\\streamed.bin";
    write(streamed, "SYNTHETIC UNNAMED DATA");
    write(streamed + L":hidden", "SYNTHETIC ALTERNATE STREAM");
    CHECK(reopen(a, owner).outcome == Outcome::storage_reopened);
    CHECK(owner->backup_offline(backup).outcome == Outcome::unsafe_path);
    owner.reset();
    CHECK(DeleteFileW(streamed.c_str()));
    const auto streamed_directory = namespace_path + L"\\streamed-dir";
    CHECK(CreateDirectoryW(streamed_directory.c_str(), nullptr));
    write(streamed_directory + L":hidden", "SYNTHETIC DIRECTORY STREAM");
    CHECK(reopen(a, owner).outcome == Outcome::storage_reopened);
    CHECK(owner->backup_offline(backup).outcome == Outcome::unsafe_path && backup.path.empty());
    owner.reset();
    CHECK(RemoveDirectoryW(streamed_directory.c_str()));
    write(namespace_path + L":hidden", "SYNTHETIC NAMESPACE ROOT STREAM");
    CHECK(reopen(a, owner).outcome == Outcome::unsafe_path);
    CHECK(DeleteFileW((namespace_path + L":hidden").c_str()));
    CHECK(reopen(a, owner).outcome == Outcome::storage_reopened);
    owner.reset();
    CHECK(DeleteFileW((namespace_path + L"\\session.manifest").c_str()));
    CHECK(CreateHardLinkW((namespace_path + L"\\session.manifest").c_str(),
        (protected_path + L"\\protected.txt").c_str(), nullptr));
    CHECK(reopen(a, owner).outcome == Outcome::unsafe_path);
    CHECK(DeleteFileW((namespace_path + L"\\session.manifest").c_str()));
    CHECK(fs::directory_iterator(protected_path) != fs::directory_iterator());
    CHECK(std::distance(fs::directory_iterator(protected_path), fs::directory_iterator()) == 1);
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc == 3) {
        const auto a = descriptor(argv[2]);
        std::string id;
        CHECK(namespace_id(a.identity, id).ok() && id == expected_id);
        std::unique_ptr<Namespace> owner;
        if (std::wstring_view(argv[1]) == L"--busy")
            return prepare(a, owner).outcome == Outcome::ownership_conflict ? 0 : 1;
        if (std::wstring_view(argv[1]) == L"--reopen")
            return reopen(a, owner).outcome == Outcome::storage_reopened ? 0 : 1;
        return 2;
    }
    CHECK(argc == 1);
    identities_and_descriptor();
    Fixture fixture;
    lifecycle_and_backup(fixture);
    malformed_and_partial(fixture);
    redirect_and_links(fixture);
    transport_backups(fixture);
    std::puts("PASS synthetic offline storage: canonical identity, process lease/reopen, bounded metadata, backup/reserved-receipt rejection, corrupt/missing/partial/foreign manifest, reparse/hardlink protection; NO NATIVE SAVE PROOF");
    return 0;
}
