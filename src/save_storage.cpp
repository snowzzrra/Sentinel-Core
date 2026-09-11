// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_storage.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace sentinel::storage {
namespace {
struct ReadHandles {
    std::vector<HANDLE> values;
    ~ReadHandles() { for (HANDLE value : values) CloseHandle(value); }
};

Result read_descriptor_text(const wchar_t* filename, std::string& text) {
    // Only local drive paths; no device names, alternate streams or UNC providers.
    const size_t length = std::wcslen(filename);
    if (length < 4 || length > 32760) return {Outcome::invalid_descriptor, ERROR_INVALID_NAME};
    std::wstring path(filename);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (!((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path[1] != L':' || path[2] != L'\\' || path.find_first_of(L":*?", 2) != std::wstring::npos)
        return {Outcome::invalid_descriptor, ERROR_INVALID_NAME};
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed) return {Outcome::io_error, GetLastError()};
    if (needed > 32761) return {Outcome::limit_exceeded, ERROR_FILENAME_EXCED_RANGE};
    std::wstring absolute(needed, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), needed, absolute.data(), nullptr);
    if (!written) return {Outcome::io_error, GetLastError()};
    if (written >= needed) return {Outcome::io_error, ERROR_INSUFFICIENT_BUFFER};
    absolute.resize(written);
    if (GetDriveTypeW(absolute.substr(0, 3).c_str()) != DRIVE_FIXED)
        return {Outcome::unsafe_path, ERROR_NOT_SUPPORTED};

    // Pin every parent against replacement, then the file against concurrent writes.
    ReadHandles handles;
    size_t end = 3;
    for (;;) {
        const bool leaf = end == absolute.size();
        const std::wstring current = absolute.substr(0, end);
        HANDLE handle = CreateFileW(current.c_str(), leaf ? GENERIC_READ : FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return {Outcome::io_error, GetLastError()};
        handles.values.push_back(handle);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info)) return {Outcome::io_error, GetLastError()};
        if (GetFileType(handle) != FILE_TYPE_DISK ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            (leaf ? ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nNumberOfLinks != 1)
                  : !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)))
            return {Outcome::unsafe_path, ERROR_ACCESS_DENIED};
        if (leaf) {
            if (info.nFileSizeHigh || info.nFileSizeLow > 2048)
                return {Outcome::limit_exceeded, ERROR_FILE_TOO_LARGE};
            if (!info.nFileSizeLow) return {Outcome::invalid_descriptor, ERROR_INVALID_DATA};
            text.resize(info.nFileSizeLow);
            DWORD count = 0;
            if (!ReadFile(handle, text.data(), info.nFileSizeLow, &count, nullptr))
                return {Outcome::io_error, GetLastError()};
            if (count != info.nFileSizeLow) return {Outcome::io_error, ERROR_HANDLE_EOF};
            return {Outcome::ok, ERROR_SUCCESS};
        }
        end = absolute.find(L'\\', end + (end == 3 ? 0 : 1));
        if (end == std::wstring::npos) end = absolute.size();
    }
}

constexpr size_t metadata_limit = 2048, path_limit = 240;
constexpr size_t backup_entry_limit = 1024;
constexpr uint64_t backup_byte_limit = 1024ULL * 1024 * 1024;
constexpr wchar_t manifest_name[] = L"session.manifest";
constexpr wchar_t lock_name[] = L"owner.lock";
constexpr wchar_t receipt_name[] = L"backup.receipt";
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = std::exchange(other.value, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};
struct Lease {
    Descriptor descriptor;
    Metadata metadata;
    std::vector<Handle> directories;
    Handle owner;
    Handle manifest;
};
Result io_error(DWORD error = GetLastError()) {
    return {error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ?
        Outcome::ownership_conflict : Outcome::io_error, error};
}
bool lower_hex(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
std::string hex(std::string_view value) {
    std::string result;
    result.reserve(value.size() * 2);
    for (const unsigned char c : value) {
        result.push_back("0123456789abcdef"[c >> 4]);
        result.push_back("0123456789abcdef"[c & 15]);
    }
    return result;
}
bool unhex(std::string_view value, std::string& result) {
    if (value.size() % 2 || !lower_hex(value)) return false;
    result.clear();
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < value.size(); i += 2)
        result.push_back(static_cast<char>((digit(value[i]) << 4) | digit(value[i + 1])));
    return true;
}
bool utf8(std::string_view value, std::wstring& result) {
    if (value.empty() || value.size() > metadata_limit || value.find('\0') != std::string_view::npos)
        return false;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!length) return false;
    result.resize(static_cast<size_t>(length));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length) == length;
}
bool decimal(std::string_view value, std::optional<uint32_t>& number) {
    if (value.empty() || value.size() > 10 || (value.size() > 1 && value.front() == '0')) return false;
    uint64_t total = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
        total = total * 10 + static_cast<uint64_t>(c - '0');
    }
    if (total > UINT32_MAX) return false;
    number = static_cast<uint32_t>(total);
    return true;
}
bool line(std::string_view& text, std::string_view prefix, std::string_view& value) {
    const auto end = text.find('\n');
    if (end == std::string_view::npos || text.substr(0, prefix.size()) != prefix) return false;
    value = text.substr(prefix.size(), end - prefix.size());
    text.remove_prefix(end + 1);
    return true;
}
bool identity_fields(std::string_view& text, Identity& identity) {
    std::string_view value;
    if (!line(text, "seed_hex=", value) || !unhex(value, identity.seed) ||
        !line(text, "team=", value) || !decimal(value, identity.team) ||
        !line(text, "slot=", value) || !decimal(value, identity.slot) ||
        !line(text, "generation_fingerprint=", value)) return false;
    identity.generation_fingerprint.assign(value);
    return true;
}
std::string identity_text(const Identity& identity) {
    return "seed_hex=" + hex(identity.seed) + "\nteam=" + std::to_string(*identity.team) +
        "\nslot=" + std::to_string(*identity.slot) + "\ngeneration_fingerprint=" +
        identity.generation_fingerprint + "\n";
}
Result digest(std::string_view input, std::string& output) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash_handle = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash_handle, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash_handle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())), static_cast<ULONG>(input.size()), 0);
    std::array<unsigned char, 32> bytes{};
    if (status >= 0) status = BCryptFinishHash(hash_handle, bytes.data(), static_cast<ULONG>(bytes.size()), 0);
    if (hash_handle) BCryptDestroyHash(hash_handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {Outcome::io_error, static_cast<uint32_t>(status)};
    output = hex(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    return {};
}
void canonical_field(std::string& canonical, std::string_view field) {
    const auto length = static_cast<uint32_t>(field.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        canonical.push_back(static_cast<char>((length >> shift) & 255));
    canonical.append(field);
}
bool component(std::wstring_view name) {
    if (name.empty() || name.size() > 128 || name == L"." || name == L".." ||
        name.back() == L'.' || name.back() == L' ') return false;
    for (wchar_t c : name)
        if (c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos) return false;
    std::wstring base(name.substr(0, name.find(L'.')));
    for (auto& c : base) if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - (L'a' - L'A'));
    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
        base == L"CONIN$" || base == L"CONOUT$") return false;
    if (base.size() == 4 && (base.substr(0, 3) == L"COM" || base.substr(0, 3) == L"LPT") &&
        ((base[3] >= L'0' && base[3] <= L'9') || base[3] == 0xB9 || base[3] == 0xB2 || base[3] == 0xB3))
        return false;
    return true;
}
bool valid_root(std::wstring_view path) {
    // Retain room for namespace and backup suffixes inside the Win32 path limit.
    if (path.size() < 4 || path.size() > 110 || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path.back() == L'\\') return false;
    size_t start = 3;
    while (start < path.size()) {
        const size_t end = path.find(L'\\', start);
        if (!component(path.substr(start, end == std::wstring_view::npos ? path.size() - start : end - start)))
            return false;
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    return true;
}
Result validate_handle(HANDLE handle, const std::wstring& path, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return io_error();
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
        (!directory && info.nNumberOfLinks != 1)) return {Outcome::unsafe_path};
    std::array<wchar_t, 512> resolved{};
    const DWORD length = GetFinalPathNameByHandleW(handle, resolved.data(), static_cast<DWORD>(resolved.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= resolved.size()) return io_error();
    const std::wstring expected = L"\\\\?\\" + path;
    if (CompareStringOrdinal(resolved.data(), static_cast<int>(length), expected.data(),
        static_cast<int>(expected.size()), TRUE) != CSTR_EQUAL) return {Outcome::unsafe_path};
    return {};
}
Result validate_streams(HANDLE handle, bool directory) {
    // Query the retained handle: path-based enumeration reopens the file and
    // conflicts with exclusive ownership/publication handles.
    alignas(FILE_STREAM_INFO) std::array<unsigned char, 512> buffer{};
    if (!GetFileInformationByHandleEx(handle, FileStreamInfo, buffer.data(), static_cast<DWORD>(buffer.size()))) {
        const DWORD error = GetLastError();
        if (directory && error == ERROR_HANDLE_EOF) return {};
        return error == ERROR_MORE_DATA || error == ERROR_INSUFFICIENT_BUFFER ?
            Result{Outcome::unsafe_path, error} : io_error(error);
    }
    const auto stream = reinterpret_cast<const FILE_STREAM_INFO*>(buffer.data());
    if (directory && !stream->NextEntryOffset && !stream->StreamNameLength) return {};
    constexpr wchar_t unnamed[] = L"::$DATA";
    constexpr DWORD name_bytes = sizeof(unnamed) - sizeof(wchar_t);
    if (stream->NextEntryOffset || stream->StreamNameLength != name_bytes ||
        std::memcmp(stream->StreamName, unnamed, name_bytes) != 0) return {Outcome::unsafe_path};
    return {};
}
Result open_directory(const std::wstring& path, Handle& handle, bool payload = false) {
    if (path.size() > path_limit) return {Outcome::limit_exceeded};
    // Deny write as well as delete sharing: retaining a read-only directory handle
    // must also exclude a concurrent write handle used to set a reparse point.
    handle = Handle(CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle) return io_error();
    auto result = validate_handle(handle.value, path, true);
    return result.ok() && payload ? validate_streams(handle.value, true) : result;
}
Result admit_root(const std::wstring& root, std::vector<Handle>& directories) {
    if (!valid_root(root)) return {Outcome::invalid_root};
    const std::wstring drive = root.substr(0, 3);
    if (GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) return {Outcome::invalid_root};
    size_t end = 3;
    for (;;) {
        const auto path = root.substr(0, end);
        Handle handle;
        auto result = open_directory(path, handle);
        if (!result.ok()) return result;
        directories.push_back(std::move(handle));
        if (end == root.size()) break;
        const auto separator = root.find(L'\\', end == 3 ? end : end + 1);
        end = separator == std::wstring::npos ? root.size() : separator;
    }
    return {};
}
Result open_file(const std::wstring& path, DWORD access, DWORD sharing, DWORD disposition, Handle& handle) {
    if (path.size() > path_limit) return {Outcome::limit_exceeded};
    handle = Handle(CreateFileW(path.c_str(), access | FILE_READ_ATTRIBUTES, sharing, nullptr, disposition,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle) return io_error();
    auto result = validate_handle(handle.value, path, false);
    if (!result.ok()) return result;
    // The bounded storage format only admits ordinary unnamed data. Silently
    // omitting an alternate stream would make the offline backup incomplete.
    return validate_streams(handle.value, false);
}
Result read_metadata(HANDLE file, std::string& text) {
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length)) return io_error();
    if (length.QuadPart <= 0 || length.QuadPart > static_cast<LONGLONG>(metadata_limit))
        return {Outcome::corrupt_manifest};
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return io_error();
    text.resize(static_cast<size_t>(length.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) return io_error();
    if (read != text.size()) return io_error(ERROR_READ_FAULT);
    return {};
}
std::string manifest_text(const Identity& identity, const std::string& id) {
    return "sentinel-ap-storage-v1\nnamespace=" + id + "\n" + identity_text(identity) +
        "provenance=synthetic-fixture\nstate=prepared-native-unbound\n";
}
Result validate_manifest(std::string_view text, const Descriptor& descriptor, const std::string& id) {
    constexpr std::string_view magic = "sentinel-ap-storage-v1\n";
    if (text.substr(0, magic.size()) != magic)
        return {text.substr(0, 21) == "sentinel-ap-storage-v" ? Outcome::unsupported_manifest : Outcome::corrupt_manifest};
    text.remove_prefix(magic.size());
    std::string_view stored_id;
    Identity identity;
    if (!line(text, "namespace=", stored_id) || stored_id.size() != 64 || !lower_hex(stored_id) ||
        !identity_fields(text, identity) || text != "provenance=synthetic-fixture\nstate=prepared-native-unbound\n")
        return {Outcome::corrupt_manifest};
    std::string computed;
    if (!namespace_id(identity, computed).ok() || computed != stored_id) return {Outcome::corrupt_manifest};
    if (computed != id || identity_text(identity) != identity_text(descriptor.identity))
        return {Outcome::identity_mismatch};
    return {};
}
Result write_bytes(HANDLE file, const char* bytes, DWORD count) {
    DWORD written = 0;
    if (!WriteFile(file, bytes, count, &written, nullptr)) return io_error();
    if (written != count) return io_error(ERROR_WRITE_FAULT);
    return {};
}
Result publish_metadata(const std::wstring& directory, const std::wstring& name, const std::string& text) {
    // The caller retains its exclusive namespace lease. Create once, then flush;
    // missing/malformed data fails closed on reopen. Renaming would reopen the
    // parent for writing and conflict with its required reparse-mutation guard.
    Handle file;
    auto result = open_file(directory + L"\\" + name, GENERIC_WRITE, 0, CREATE_NEW, file);
    if (!result.ok()) return result;
    result = write_bytes(file.value, text.data(), static_cast<DWORD>(text.size()));
    if (!result.ok()) return result;
    if (!FlushFileBuffers(file.value)) return io_error();
    return {};
}
Result open_namespace(const Descriptor& descriptor, bool create, Lease& lease) {
    std::string id;
    auto result = namespace_id(descriptor.identity, id);
    if (!result.ok()) return result;
    result = admit_root(descriptor.root, lease.directories);
    if (!result.ok()) return result;
    lease.descriptor = descriptor;
    lease.metadata = {id, descriptor.root + L"\\" + std::wstring(id.begin(), id.end()), 1, false};
    bool created = false;
    if (create) {
        created = CreateDirectoryW(lease.metadata.path.c_str(), nullptr) != FALSE;
        if (!created && GetLastError() != ERROR_ALREADY_EXISTS) return io_error();
    }
    Handle directory;
    result = open_directory(lease.metadata.path, directory, true);
    if (!result.ok()) {
        if (result.win32_error == ERROR_FILE_NOT_FOUND || result.win32_error == ERROR_PATH_NOT_FOUND)
            return {Outcome::namespace_missing, result.win32_error};
        return result;
    }
    lease.directories.push_back(std::move(directory));
    result = open_file(lease.metadata.path + L"\\" + lock_name, GENERIC_READ | GENERIC_WRITE, 0,
        created ? CREATE_NEW : OPEN_EXISTING, lease.owner);
    if (!result.ok()) {
        if (result.win32_error == ERROR_FILE_NOT_FOUND) return {Outcome::interrupted_preparation, result.win32_error};
        return result;
    }
    if (created) {
        result = publish_metadata(lease.metadata.path, manifest_name, manifest_text(descriptor.identity, id));
        if (!result.ok()) return result;
    }
    result = open_file(lease.metadata.path + L"\\" + manifest_name, GENERIC_READ, FILE_SHARE_READ,
        OPEN_EXISTING, lease.manifest);
    if (!result.ok()) {
        if (result.win32_error == ERROR_FILE_NOT_FOUND) return {Outcome::interrupted_preparation, result.win32_error};
        return result;
    }
    std::string text;
    result = read_metadata(lease.manifest.value, text);
    if (!result.ok()) return result;
    result = validate_manifest(text, descriptor, id);
    if (!result.ok()) return result;
    return {created ? Outcome::storage_prepared : Outcome::storage_reopened};
}
struct Entry {
    std::wstring relative;
    Handle handle;
    bool directory = false;
    uint64_t bytes = 0;
};
Result collect(const std::wstring& base, const std::wstring& relative, unsigned depth,
    std::vector<Entry>& entries, uint64_t& total, bool transport = false) {
    if (depth > 8) return {Outcome::limit_exceeded};
    const auto directory = relative.empty() ? base : base + L"\\" + relative;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return io_error();
    Result result;
    do {
        const std::wstring name(data.cFileName);
        if (name == L"." || name == L".." || (!transport && relative.empty() && name == lock_name)) continue;
        // A copied old completion receipt could falsely mark a failed attempt as
        // complete. Reserve this root entry before any output directory exists.
        if (relative.empty() && CompareStringOrdinal(name.c_str(), -1, receipt_name, -1, TRUE) == CSTR_EQUAL) {
            result = {Outcome::unsafe_path}; break;
        }
        if (!component(name) || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            result = {Outcome::unsafe_path}; break;
        }
        if (entries.size() >= (transport ? 17 : backup_entry_limit)) { result = {Outcome::limit_exceeded}; break; }
        Entry entry;
        entry.relative = relative.empty() ? name : relative + L"\\" + name;
        const auto path = base + L"\\" + entry.relative;
        entry.directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (transport && entry.directory) { result = {Outcome::corrupt_manifest}; break; }
        result = entry.directory ? open_directory(path, entry.handle, true) :
            open_file(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, entry.handle);
        if (!result.ok()) break;
        if (!entry.directory) {
            LARGE_INTEGER length{};
            if (!GetFileSizeEx(entry.handle.value, &length)) { result = io_error(); break; }
            const auto limit = backup_byte_limit + (transport ? 32768 : 0);
            if (length.QuadPart < 0 || static_cast<uint64_t>(length.QuadPart) > limit - total) {
                result = {Outcome::limit_exceeded}; break;
            }
            entry.bytes = static_cast<uint64_t>(length.QuadPart);
            total += entry.bytes;
        }
        const bool recurse = entry.directory;
        const auto child = entry.relative;
        entries.push_back(std::move(entry));
        if (recurse) {
            result = collect(base, child, depth + 1, entries, total);
            if (!result.ok()) break;
        }
    } while (FindNextFileW(find, &data));
    const DWORD error = GetLastError();
    FindClose(find);
    if (result.ok() && error != ERROR_NO_MORE_FILES) return io_error(error);
    return result;
}
Result random_suffix(std::wstring& output) {
    std::array<unsigned char, 16> bytes{};
    const auto status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) return {Outcome::io_error, static_cast<uint32_t>(status)};
    const auto encoded = hex(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    output.assign(encoded.begin(), encoded.end());
    return {};
}
} // namespace

bool Result::ok() const {
    return outcome == Outcome::ok || outcome == Outcome::storage_prepared ||
        outcome == Outcome::storage_reopened || outcome == Outcome::offline_backup_complete ||
        outcome == Outcome::transport_backup_complete;
}
const char* outcome_name(Outcome outcome) {
    switch (outcome) {
#define STORAGE_CASE(name) case Outcome::name: return #name
    STORAGE_CASE(ok); STORAGE_CASE(storage_prepared); STORAGE_CASE(storage_reopened);
    STORAGE_CASE(offline_backup_complete); STORAGE_CASE(transport_backup_complete); STORAGE_CASE(payload_mismatch);
    STORAGE_CASE(invalid_descriptor); STORAGE_CASE(invalid_identity);
    STORAGE_CASE(invalid_root); STORAGE_CASE(unsafe_path); STORAGE_CASE(identity_mismatch);
    STORAGE_CASE(unsupported_manifest); STORAGE_CASE(corrupt_manifest); STORAGE_CASE(namespace_missing);
    STORAGE_CASE(interrupted_preparation); STORAGE_CASE(ownership_conflict); STORAGE_CASE(io_error);
    STORAGE_CASE(limit_exceeded);
#undef STORAGE_CASE
    }
    return "unknown";
}
Result parse_descriptor(std::string_view text, Descriptor& descriptor) {
    descriptor = {};
    constexpr std::string_view magic = "sentinel-test-session-v1\n";
    if (text.size() > metadata_limit || text.substr(0, magic.size()) != magic ||
        text.find('\r') != std::string_view::npos || text.find('\0') != std::string_view::npos)
        return {Outcome::invalid_descriptor};
    text.remove_prefix(magic.size());
    Descriptor parsed;
    std::string_view value;
    if (!identity_fields(text, parsed.identity) || !line(text, "provenance=", value) ||
        value != "synthetic-fixture" || !line(text, "root=", value) || !utf8(value, parsed.root) || !text.empty())
        return {Outcome::invalid_descriptor};
    std::string id;
    auto result = namespace_id(parsed.identity, id);
    if (!result.ok()) return result;
    if (!valid_root(parsed.root)) return {Outcome::invalid_root};
    descriptor = std::move(parsed);
    return {};
}
Result namespace_id(const Identity& identity, std::string& id) {
    id.clear();
    std::wstring seed;
    if (identity.seed.empty() || identity.seed.size() > 256 || !identity.team || !identity.slot ||
        *identity.slot == 0 || identity.generation_fingerprint.size() != 64 ||
        !lower_hex(identity.generation_fingerprint) || !utf8(identity.seed, seed) ||
        std::any_of(seed.begin(), seed.end(), [](wchar_t c) { return c < 32 || c == 127; }))
        return {Outcome::invalid_identity};
    // Fixed identity-domain tag, followed by four 32-bit big-endian length-prefixed
    // UTF-8/canonical decimal fields. Product, protocol and process IDs are absent.
    std::string canonical = "sentinel-ap-session-identity";
    canonical_field(canonical, identity.seed);
    canonical_field(canonical, std::to_string(*identity.team));
    canonical_field(canonical, std::to_string(*identity.slot));
    canonical_field(canonical, identity.generation_fingerprint);
    return digest(canonical, id);
}

struct Namespace::Impl { Lease lease; };
Namespace::Namespace(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Namespace::~Namespace() = default;
const Metadata& Namespace::metadata() const { return impl_->lease.metadata; }
Result prepare(const Descriptor& descriptor, std::unique_ptr<Namespace>& output) {
    output.reset();
    auto impl = std::make_unique<Namespace::Impl>();
    auto result = open_namespace(descriptor, true, impl->lease);
    if (result.ok()) output.reset(new Namespace(std::move(impl)));
    return result;
}
Result reopen(const Descriptor& descriptor, std::unique_ptr<Namespace>& output) {
    output.reset();
    auto impl = std::make_unique<Namespace::Impl>();
    auto result = open_namespace(descriptor, false, impl->lease);
    if (result.ok()) output.reset(new Namespace(std::move(impl)));
    return result;
}
Result inspect(const Descriptor& descriptor, Metadata& metadata) {
    metadata = {};
    Lease lease;
    auto result = open_namespace(descriptor, false, lease);
    if (result.ok()) { metadata = lease.metadata; return {}; }
    return result;
}
Result Namespace::backup_offline(Backup& backup) {
    backup = {};
    auto& lease = impl_->lease;
    std::vector<Entry> entries;
    uint64_t total = 0;
    auto result = validate_streams(lease.directories.back().value, true);
    if (!result.ok()) return result;
    result = collect(lease.metadata.path, L"", 0, entries, total);
    if (!result.ok()) return result;
    std::wstring suffix;
    result = random_suffix(suffix);
    if (!result.ok()) return result;
    const auto name = L"backup-attempt-" + std::wstring(lease.metadata.namespace_id.begin(), lease.metadata.namespace_id.begin() + 16) +
        L"-" + suffix;
    backup.path = lease.descriptor.root + L"\\" + name;
    if (backup.path.size() > path_limit) return {Outcome::limit_exceeded};
    if (!CreateDirectoryW(backup.path.c_str(), nullptr)) return io_error();
    Handle target;
    result = open_directory(backup.path, target, true);
    if (!result.ok()) return result;
    std::vector<Handle> target_directories;
    std::array<char, 64 * 1024> buffer{};
    for (const auto& entry : entries) {
        const auto destination = backup.path + L"\\" + entry.relative;
        if (entry.directory) {
            if (!CreateDirectoryW(destination.c_str(), nullptr)) return io_error();
            Handle directory;
            result = open_directory(destination, directory, true);
            if (!result.ok()) return result;
            target_directories.push_back(std::move(directory));
            continue;
        }
        Handle file;
        result = open_file(destination, GENERIC_WRITE, 0, CREATE_NEW, file);
        if (!result.ok()) return result;
        uint64_t remaining = entry.bytes;
        while (remaining) {
            const DWORD count = static_cast<DWORD>(std::min<uint64_t>(remaining, buffer.size()));
            DWORD read = 0;
            if (!ReadFile(entry.handle.value, buffer.data(), count, &read, nullptr)) return io_error();
            if (read != count) return io_error(ERROR_READ_FAULT);
            result = write_bytes(file.value, buffer.data(), count);
            if (!result.ok()) return result;
            remaining -= count;
            backup.bytes += count;
        }
        if (!FlushFileBuffers(file.value)) return io_error();
        ++backup.files;
    }
    // Receipt is separate from the copied storage manifest and explicitly offline.
    result = publish_metadata(backup.path, receipt_name, "sentinel-offline-backup-v1\nnamespace=" +
        lease.metadata.namespace_id + "\nfiles=" + std::to_string(backup.files) + "\nbytes=" +
        std::to_string(backup.bytes) + "\nprovenance=synthetic-fixture\nnative_completion=unproven\nstate=complete\n");
    if (!result.ok()) return result;
    return {Outcome::offline_backup_complete};
}
namespace {
constexpr std::string_view transport_magic = "sentinel-transport-backup-v1\n";
constexpr wchar_t transport_manifest[] = L"transport.manifest";
constexpr uint32_t transport_file_limit = 100u * 1024u * 1024u;
std::string folded(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return value;
}
bool transport_name(std::string_view value) {
    if (value.empty() || value.size() > 191) return false;
    size_t start = 0;
    for (size_t i = 0; i <= value.size(); ++i) {
        if (i == value.size() || value[i] == '/') {
            const auto part = value.substr(start, i - start);
            if (part.empty() || part == "." || part == "..") return false;
            start = i + 1;
        } else {
            const char c = value[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-' || c == '.')) return false;
        }
    }
    return true;
}
bool transport_metadata(const TransportMetadata& value, const std::string& id) {
    if (!value.process_id || !value.process_created || !value.operation_id || value.files.empty() || value.files.size() > 16)
        return false;
    const auto directory = folded(value.directory), root = "ap-" + id.substr(0, 40) + "/";
    if (directory.compare(0, root.size(), root)) return false;
    const auto slot = directory.substr(root.size()); bool known = false;
    for (const char* campaign : {"game-autosave", "dlc1-autosave", "dlc2-autosave"})
        for (unsigned i = 0; i < 12; ++i) known |= slot == campaign + std::to_string(i);
    if (!known) return false;
    uint64_t total = 0;
    for (size_t i = 0; i < value.files.size(); ++i) {
        const auto& file = value.files[i];
        if (!transport_name(file.name) || !file.size || file.size > transport_file_limit) return false;
        total += file.size; if (total > backup_byte_limit) return false;
        for (size_t j = 0; j < i; ++j) if (folded(file.name) == folded(value.files[j].name)) return false;
    }
    return true;
}
std::wstring payload_name(size_t i) { return L"payload-" + std::to_wstring(i) + L".bin"; }
std::string hash_text(const TransportFile& file) {
    return hex(std::string_view(reinterpret_cast<const char*>(file.sha256.data()), file.sha256.size()));
}
std::string transport_text(const TransportMetadata& value, const Descriptor& descriptor, const std::string& id) {
    std::string text(transport_magic);
    text += "namespace=" + id + "\n" + identity_text(descriptor.identity) + "provenance=synthetic-fixture\nprocess_id=" +
        std::to_string(value.process_id) + "\nprocess_created=" + std::to_string(value.process_created) +
        "\nwrite_operation=" + std::to_string(value.operation_id) + "\nnative_directory=" + value.directory +
        "\nfiles=" + std::to_string(value.files.size()) + "\n";
    for (const auto& file : value.files)
        text += "file=" + hex(file.name) + "," + std::to_string(file.size) + "," + hash_text(file) + "\n";
    return text + "state=complete\n";
}
bool number64(std::string_view value, uint64_t& out) {
    if (value.empty() || value.size() > 20 || (value.size() > 1 && value.front() == '0')) return false;
    out = 0;
    for (char c : value) {
        if (c < '0' || c > '9' || out > (UINT64_MAX - static_cast<unsigned>(c - '0')) / 10) return false;
        out = out * 10 + static_cast<unsigned>(c - '0');
    }
    return true;
}
Result parse_transport(std::string_view text, const Descriptor& descriptor, const std::string& id, TransportMetadata& out) {
    if (text.substr(0, transport_magic.size()) != transport_magic)
        return {text.substr(0, 27) == "sentinel-transport-backup-v" ? Outcome::unsupported_manifest : Outcome::corrupt_manifest};
    const auto original = text; text.remove_prefix(transport_magic.size());
    std::string_view value, stored_id; Identity identity; uint64_t pid = 0, count = 0;
    if (!line(text, "namespace=", stored_id) || !identity_fields(text, identity) ||
        !line(text, "provenance=", value) || value != "synthetic-fixture") return {Outcome::corrupt_manifest};
    std::string computed;
    if (!namespace_id(identity, computed).ok() || computed != stored_id) return {Outcome::corrupt_manifest};
    if (computed != id || identity_text(identity) != identity_text(descriptor.identity)) return {Outcome::identity_mismatch};
    if (!line(text, "process_id=", value) || !number64(value, pid) || pid > UINT32_MAX ||
        !line(text, "process_created=", value) || !number64(value, out.process_created) ||
        !line(text, "write_operation=", value) || !number64(value, out.operation_id) ||
        !line(text, "native_directory=", value)) return {Outcome::corrupt_manifest};
    out.process_id = static_cast<uint32_t>(pid); out.directory.assign(value);
    if (!line(text, "files=", value) || !number64(value, count) || !count || count > 16) return {Outcome::corrupt_manifest};
    for (uint64_t i = 0; i < count; ++i) {
        if (!line(text, "file=", value)) return {Outcome::corrupt_manifest};
        const auto first = value.find(','), last = value.rfind(',');
        TransportFile file; std::string hash; uint64_t size = 0;
        if (first == std::string_view::npos || first == last || !unhex(value.substr(0, first), file.name) ||
            !number64(value.substr(first + 1, last - first - 1), size) || size > UINT32_MAX ||
            !unhex(value.substr(last + 1), hash) || hash.size() != file.sha256.size()) return {Outcome::corrupt_manifest};
        file.size = static_cast<uint32_t>(size); std::memcpy(file.sha256.data(), hash.data(), hash.size());
        out.files.push_back(std::move(file));
    }
    if (text != "state=complete\n" || !transport_metadata(out, id) || transport_text(out, descriptor, id) != original)
        return {Outcome::corrupt_manifest};
    return {};
}
Result verify_transport_file(HANDLE file, const TransportFile& expected, uint64_t deadline) {
    LARGE_INTEGER length{}, start{};
    if (!GetFileSizeEx(file, &length) || !SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return io_error();
    if (length.QuadPart != expected.size) return {Outcome::payload_mismatch};
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    Result result; std::array<unsigned char, 65536> buffer{}; std::array<unsigned char, 32> output{};
    for (uint32_t offset = 0; status >= 0 && offset < expected.size;) {
        if (GetTickCount64() >= deadline) { result = {Outcome::limit_exceeded, ERROR_TIMEOUT}; break; }
        const auto count = (std::min)(static_cast<uint32_t>(buffer.size()), expected.size - offset); DWORD read = 0;
        if (!ReadFile(file, buffer.data(), count, &read, nullptr)) { result = io_error(); break; }
        if (read != count) { result = io_error(ERROR_READ_FAULT); break; }
        status = BCryptHashData(hash, buffer.data(), count, 0); offset += count;
    }
    if (result.ok() && status >= 0) status = BCryptFinishHash(hash, output.data(), 32, 0);
    if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!result.ok()) return result;
    if (status < 0) return {Outcome::io_error, static_cast<uint32_t>(status)};
    return std::memcmp(output.data(), expected.sha256.data(), 32) ? Result{Outcome::payload_mismatch} : Result{};
}
}
struct TransportArchive::Impl {
    TransportMetadata metadata; std::vector<Handle> directories, files; Handle manifest; std::mutex mutex;
};
TransportArchive::TransportArchive(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TransportArchive::~TransportArchive() = default;
const TransportMetadata& TransportArchive::metadata() const { return impl_->metadata; }
Result TransportArchive::read(size_t index, uint32_t offset, char* bytes, uint32_t count) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (index >= impl_->files.size() || !bytes || offset > impl_->metadata.files[index].size ||
        count > impl_->metadata.files[index].size - offset) return {Outcome::limit_exceeded};
    LARGE_INTEGER position{}; position.QuadPart = offset; DWORD read = 0;
    if (!SetFilePointerEx(impl_->files[index].value, position, nullptr, FILE_BEGIN) ||
        !ReadFile(impl_->files[index].value, bytes, count, &read, nullptr)) return io_error();
    return read == count ? Result{} : io_error(ERROR_READ_FAULT);
}
Result Namespace::backup_transport(const TransportMetadata& metadata, ReadTransport read, void* source, Backup& backup) {
    backup = {}; auto& lease = impl_->lease;
    if (!read || !transport_metadata(metadata, lease.metadata.namespace_id)) return {Outcome::invalid_identity};
    std::wstring suffix; auto result = random_suffix(suffix); if (!result.ok()) return result;
    const auto prefix = "transport-backup-" + lease.metadata.namespace_id.substr(0, 16) + "-";
    backup.path = lease.descriptor.root + L"\\" + std::wstring(prefix.begin(), prefix.end()) + suffix;
    if (backup.path.size() > path_limit) return {Outcome::limit_exceeded};
    if (!CreateDirectoryW(backup.path.c_str(), nullptr)) return io_error();
    Handle directory; result = open_directory(backup.path, directory, true); if (!result.ok()) return result;
    const auto deadline = GetTickCount64() + 10000; // Checked between chunks; not a system-call latency guarantee.
    std::array<char, 65536> buffer{};
    std::vector<Handle> written_files; written_files.reserve(metadata.files.size());
    for (size_t i = 0; i < metadata.files.size(); ++i) {
        Handle file; result = open_file(backup.path + L"\\" + payload_name(i), GENERIC_READ | GENERIC_WRITE, 0, CREATE_NEW, file);
        if (!result.ok()) return result;
        for (uint32_t offset = 0; offset < metadata.files[i].size;) {
            if (GetTickCount64() >= deadline) return {Outcome::limit_exceeded, ERROR_TIMEOUT};
            const auto count = (std::min)(static_cast<uint32_t>(buffer.size()), metadata.files[i].size - offset);
            if (!read(source, i, offset, buffer.data(), count)) return io_error(ERROR_READ_FAULT);
            result = write_bytes(file.value, buffer.data(), count); if (!result.ok()) return result;
            offset += count; backup.bytes += count;
        }
        if (!FlushFileBuffers(file.value)) return io_error();
        result = verify_transport_file(file.value, metadata.files[i], deadline); if (!result.ok()) return result;
        written_files.push_back(std::move(file)); // Keep earlier verified files immutable until publication.
        ++backup.files;
    }
    result = publish_metadata(backup.path, transport_manifest, transport_text(metadata, lease.descriptor, lease.metadata.namespace_id));
    return result.ok() ? Result{Outcome::transport_backup_complete} : result;
}
Result Namespace::reopen_transport(std::wstring_view basename, std::unique_ptr<TransportArchive>& archive) {
    archive.reset(); const auto& lease = impl_->lease;
    const auto prefix = "transport-backup-" + lease.metadata.namespace_id.substr(0, 16) + "-";
    const std::wstring start(prefix.begin(), prefix.end());
    if (basename.size() != start.size() + 32 || basename.substr(0, start.size()) != start) return {Outcome::unsafe_path};
    for (wchar_t c : basename.substr(start.size())) if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return {Outcome::unsafe_path};
    auto owned = std::make_unique<TransportArchive::Impl>();
    auto result = admit_root(lease.descriptor.root, owned->directories); if (!result.ok()) return result;
    const auto path = lease.descriptor.root + L"\\" + std::wstring(basename);
    Handle directory; result = open_directory(path, directory, true); if (!result.ok()) return result;
    owned->directories.push_back(std::move(directory));
    result = open_file(path + L"\\" + transport_manifest, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, owned->manifest);
    if (!result.ok()) return result.win32_error == ERROR_FILE_NOT_FOUND ? Result{Outcome::interrupted_preparation} : result;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(owned->manifest.value, &size)) return io_error();
    if (size.QuadPart < 1 || size.QuadPart > 32768) return {Outcome::corrupt_manifest};
    std::string text(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
    if (!ReadFile(owned->manifest.value, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) return io_error();
    if (read != text.size()) return io_error(ERROR_READ_FAULT);
    result = parse_transport(text, lease.descriptor, lease.metadata.namespace_id, owned->metadata); if (!result.ok()) return result;
    std::vector<Entry> entries; uint64_t total = 0;
    result = collect(path, L"", 0, entries, total, true); if (!result.ok()) return result;
    if (entries.size() != owned->metadata.files.size() + 1) return {Outcome::corrupt_manifest};
    const auto deadline = GetTickCount64() + 10000;
    for (size_t i = 0; i < owned->metadata.files.size(); ++i) {
        const auto found = std::find_if(entries.begin(), entries.end(), [i](const auto& entry) { return entry.relative == payload_name(i); });
        if (found == entries.end() || found->directory) return {Outcome::corrupt_manifest};
        result = verify_transport_file(found->handle.value, owned->metadata.files[i], deadline); if (!result.ok()) return result;
        owned->files.push_back(std::move(found->handle));
    }
    archive.reset(new TransportArchive(std::move(owned))); return {};
}
Result read_descriptor_file(const wchar_t* filename, Descriptor& descriptor) {
    std::string text;
    auto result = read_descriptor_text(filename, text);
    return result.ok() ? parse_descriptor(text, descriptor) : result;
}
Result read_control_text(const wchar_t* filename, std::string& text) {
    return read_descriptor_text(filename, text);
}
} // namespace sentinel::storage
