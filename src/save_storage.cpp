// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_storage.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
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

bool wine_mount_point(HANDLE handle) {
    if (!GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) return false;
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) ||
        !(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.ReparseTag != IO_REPARSE_TAG_MOUNT_POINT)
        return false;
    // wine labels Unix mounts as reparse points without reparse data
    std::array<unsigned char, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> data{};
    DWORD returned = 0;
    return !DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0, data.data(),
        static_cast<DWORD>(data.size()), &returned, nullptr) && GetLastError() == ERROR_NOT_A_REPARSE_POINT;
}

Result read_descriptor_text(const wchar_t* filename, std::string& text) {
    // only local drive paths; no device names, alternate streams or unc providers
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

    // pin every parent against replacement, then the file against concurrent writes
    ReadHandles handles;
    size_t end = 3;
    for (;;) {
        const bool leaf = end == absolute.size();
        const std::wstring current = absolute.substr(0, end);
        // wine maps drive roots through reparse points
        HANDLE handle = CreateFileW(current.c_str(), leaf ? GENERIC_READ : FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            (end == 3 ? 0 : FILE_FLAG_OPEN_REPARSE_POINT) | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return {Outcome::io_error, GetLastError()};
        handles.values.push_back(handle);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info)) return {Outcome::io_error, GetLastError()};
        if (GetFileType(handle) != FILE_TYPE_DISK ||
            ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && (leaf || !wine_mount_point(handle))) ||
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
    Handle native_root;
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
std::string ascii(std::wstring_view value) {
    std::string out;
    for (wchar_t c : value) out += static_cast<char>(c);
    return out;
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
    // keep room for namespace and backup suffixes inside the win32 path limit
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
Result validate_handle(HANDLE handle, const std::wstring& path, bool directory, bool ancestor = false) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return io_error();
    if (((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !(ancestor && wine_mount_point(handle))) ||
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
    // query the held handle; reopening the path conflicts with exclusive read/write handles
    alignas(FILE_STREAM_INFO) std::array<unsigned char, 512> buffer{};
    if (!GetFileInformationByHandleEx(handle, FileStreamInfo, buffer.data(), static_cast<DWORD>(buffer.size()))) {
        const DWORD error = GetLastError();
        if (directory && error == ERROR_HANDLE_EOF) return {};
        if (error == ERROR_INVALID_FUNCTION) {
            DWORD flags = 0;
            if (GetVolumeInformationByHandleW(handle, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) &&
                !(flags & FILE_NAMED_STREAMS)) return {};
        }
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
    // block write and delete sharing with the read-only directory handle
    handle = Handle(CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | (path.size() == 3 ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), nullptr));
    if (!handle) return io_error();
    auto result = validate_handle(handle.value, path, true, !payload);
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
Result read_metadata(HANDLE file, std::string& text, size_t limit = metadata_limit) {
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length)) return io_error();
    if (length.QuadPart <= 0 || length.QuadPart > static_cast<LONGLONG>(limit))
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
    lease.metadata.native_root = "ap-" + id.substr(0, 40);
    const auto root_path = lease.metadata.path + L"\\native.root";
    result = open_file(root_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, lease.native_root);
    if (!result.ok() && result.win32_error == ERROR_FILE_NOT_FOUND && create &&
        descriptor.campaign.intent == CampaignIntent::create) {
        // a fresh campaign gets its own provider path, even in the same room
        for (const auto* record : {L"campaign.contract", L"campaign.checkpoint"}) {
            Handle file;
            const auto absent = open_file(lease.metadata.path + L"\\" + record, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, file);
            if (absent.ok() || absent.win32_error != ERROR_FILE_NOT_FOUND)
                return {Outcome::interrupted_preparation, absent.win32_error};
        }
        std::array<unsigned char, 20> random{};
        const auto status = BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0) return {Outcome::io_error, static_cast<uint32_t>(status)};
        text = "sentinel-native-root-v1\nnamespace=" + id + "\nroot=ap-" +
            hex(std::string_view(reinterpret_cast<const char*>(random.data()), random.size())) + "\n";
        result = publish_metadata(lease.metadata.path, L"native.root", text);
        if (!result.ok()) return result;
        result = open_file(root_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, lease.native_root);
    }
    if (result.ok()) {
        result = read_metadata(lease.native_root.value, text);
        if (!result.ok()) return result;
        const auto header = "sentinel-native-root-v1\nnamespace=" + id + "\nroot=";
        if (text.size() != header.size() + 44) return {Outcome::corrupt_manifest};
        const auto root = std::string_view(text).substr(header.size());
        if (text.compare(0, header.size(), header) || root.size() != 44 || root.substr(0, 3) != "ap-" ||
            root.back() != '\n' || !lower_hex(root.substr(3, 40))) return {Outcome::corrupt_manifest};
        lease.metadata.native_root.assign(root.substr(0, 43));
    } else if (result.win32_error != ERROR_FILE_NOT_FOUND) return result;
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
    constexpr std::string_view magic = "sentinel-test-session-v3\n";
    if (text.size() > metadata_limit || text.substr(0, magic.size()) != magic ||
        text.find('\r') != std::string_view::npos || text.find('\0') != std::string_view::npos)
        return {Outcome::invalid_descriptor};
    text.remove_prefix(magic.size());
    Descriptor parsed;
    std::string_view value;
    if (!identity_fields(text, parsed.identity) || !line(text, "slot_data_revision=", value) || value != "0.7.0" ||
        !line(text, "session_mod_contract_revision=", value) || value != "2" || !line(text, "provenance=", value) ||
        value != "synthetic-fixture" || !line(text, "root=", value) || !utf8(value, parsed.root))
        return {Outcome::invalid_descriptor};
    if (!text.empty()) {
        if (!line(text, "campaign=", value) || (value != "base" && value != "unified"))
            return {Outcome::invalid_descriptor};
        const bool unified = value == "unified";
        if (!line(text, "starting_stage=", value) || value != (unified ? "hub" : "base_start") ||
            !line(text, "difficulty=", value) || value.size() != 1 || value[0] < '0' || value[0] > '3')
            return {Outcome::invalid_descriptor};
        parsed.campaign.difficulty = static_cast<uint32_t>(value[0] - '0');
        if (!line(text, "intent=", value) || (value != "create" && value != "resume" && value != "recover")) return {Outcome::invalid_descriptor};
        parsed.campaign.intent = value == "create" ? CampaignIntent::create : value == "resume" ? CampaignIntent::resume : CampaignIntent::recover;
        if (parsed.campaign.intent == CampaignIntent::recover) {
            if (!line(text, "backup=", value) || value.empty() || value.size() > 128 ||
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != value.npos)
                return {Outcome::invalid_descriptor};
            parsed.campaign.recovery_basename = value;
        }
    }
    if (!text.empty()) return {Outcome::invalid_descriptor};
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
    // hash the fixed identity tag and four length-prefixed utf-8/decimal fields with big-endian 32-bit lengths; leave out product, protocol and process ids
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
Result Namespace::campaign_record(bool checkpoint, std::string& text) const {
    text.clear(); Handle file;
    const auto path = impl_->lease.metadata.path + (checkpoint ? L"\\campaign.checkpoint" : L"\\campaign.contract");
    auto result = open_file(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, file);
    if (!result.ok()) return result;
    return read_metadata(file.value, text, 32768);
}
Result Namespace::publish_campaign_record(bool checkpoint, std::string_view text, bool create) {
    if (text.empty() || text.size() > 32768 || (!checkpoint && !create)) return {Outcome::invalid_descriptor};
    Handle file;
    const auto path = impl_->lease.metadata.path + (checkpoint ? L"\\campaign.checkpoint" : L"\\campaign.contract");
    auto result = open_file(path, GENERIC_WRITE, 0, create ? CREATE_NEW : OPEN_EXISTING, file);
    if (!result.ok()) return result;
    result = write_bytes(file.value, text.data(), static_cast<DWORD>(text.size()));
    if (!result.ok()) return result;
    if (!SetEndOfFile(file.value) || !FlushFileBuffers(file.value)) return io_error();
    return {};
}
Result Namespace::recovery_record(bool complete, std::string& text) const {
    text.clear(); Handle file;
    const auto path = impl_->lease.metadata.path + (complete ? L"\\recovery.complete" : L"\\recovery.pending");
    auto result = open_file(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, file);
    return result.ok() ? read_metadata(file.value, text, 32768) : result;
}
Result Namespace::publish_recovery_record(bool complete, std::string_view text, bool replace) {
    if (text.empty() || text.size() > 32768) return {Outcome::invalid_descriptor};
    if (replace) {
        Handle file;
        auto r = open_file(metadata().path + (complete ? L"\\recovery.complete" : L"\\recovery.pending"),
            GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS, file);
        if (!r.ok()) return r;
        r = write_bytes(file.value, text.data(), static_cast<DWORD>(text.size())); if (!r.ok()) return r;
        if (!SetEndOfFile(file.value) || !FlushFileBuffers(file.value)) return io_error();
        std::string actual; r = read_metadata(file.value, actual, 32768);
        return r.ok() && actual != text ? Result{Outcome::payload_mismatch} : r;
    }
    return publish_metadata(impl_->lease.metadata.path, complete ? L"recovery.complete" : L"recovery.pending", std::string(text));
}
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
    // receipt is separate from the copied storage manifest and explicitly offline
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
bool transport_slot(const std::string& slot) {
    bool known = false;
    for (const char* campaign : {"game-autosave", "dlc1-autosave", "dlc2-autosave"})
        for (unsigned i = 0; i < 12; ++i) known |= slot == campaign + std::to_string(i);
    return known;
}
bool transport_metadata(const TransportMetadata& value, const std::string& native_root) {
    if (value.quarantine && (!value.steam_user || value.operation_id)) return false;
    if (!value.process_id || !value.process_created || (!value.operation_id && !value.quarantine) || value.files.empty() || value.files.size() > 16)
        return false;
    const auto directory = folded(value.directory), root = native_root + "/";
    if (directory.compare(0, root.size(), root)) return false;
    if (!transport_slot(directory.substr(root.size()))) return false;
    if (value.provider.size() > 96 || value.contract.size() > 2048 || value.checkpoint.size() > 12000) return false;
    uint64_t total = 0;
    for (size_t i = 0; i < value.files.size(); ++i) {
        const auto& file = value.files[i];
        if (!transport_name(file.name) || (!file.size && !value.quarantine) || file.size > transport_file_limit) return false;
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
        "\nwrite_operation=" + std::to_string(value.operation_id) + "\nnative_directory=" + value.directory + "\n";
    if (value.steam_user) text += "steam_user=" + std::to_string(value.steam_user) +
        "\nrole=" + (value.quarantine ? "quarantine" : value.automatic ? "automatic" : "checkpoint") +
        "\nprovider_hex=" + hex(value.provider) + "\ncontract_hex=" + hex(value.contract) +
        "\ncheckpoint_hex=" + hex(value.checkpoint) + "\n";
    text += "files=" + std::to_string(value.files.size()) + "\n";
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
bool parse_transport_owner(std::string_view& text, TransportMetadata& out) {
    std::string_view value;
    if (text.substr(0, 11) == "steam_user=") {
        if (!line(text, "steam_user=", value) || !number64(value, out.steam_user) || !out.steam_user ||
            !line(text, "role=", value) || (value != "checkpoint" && value != "quarantine" && value != "automatic"))
            return false;
        out.quarantine = value == "quarantine";
        out.automatic = value == "automatic";
        if (!line(text, "provider_hex=", value) || !unhex(value, out.provider) || !line(text, "contract_hex=", value) ||
            !unhex(value, out.contract) || !line(text, "checkpoint_hex=", value) || !unhex(value, out.checkpoint))
            return false;
    }
    return true;
}
Result parse_transport(std::string_view text, const Descriptor& descriptor, const std::string& id, const std::string& native_root, TransportMetadata& out) {
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
    if (!parse_transport_owner(text, out)) return {Outcome::corrupt_manifest};
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
    if (text != "state=complete\n" || !transport_metadata(out, native_root) || transport_text(out, descriptor, id) != original)
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
Result copy_transport_payload(HANDLE file, const TransportFile& expected, size_t index, ReadTransport read,
                              void* source, uint64_t deadline, uint64_t& bytes) {
    Result result;
    std::array<char, 65536> buffer{};
    for (uint32_t offset = 0; offset < expected.size;) {
        if (GetTickCount64() >= deadline) return {Outcome::limit_exceeded, ERROR_TIMEOUT};
        const auto count = (std::min)(static_cast<uint32_t>(buffer.size()), expected.size - offset);
        if (!read(source, index, offset, buffer.data(), count)) return io_error(ERROR_READ_FAULT);
        result = write_bytes(file, buffer.data(), count);
        if (!result.ok()) return result;
        offset += count;
        bytes += count;
    }
    if (!FlushFileBuffers(file)) return io_error();
    result = verify_transport_file(file, expected, deadline);
    if (!result.ok()) return result;
    return {};
}
Result Namespace::backup_transport(const TransportMetadata& metadata, ReadTransport read, void* source, Backup& backup) {
    backup = {}; auto& lease = impl_->lease;
    if (!read || !transport_metadata(metadata, lease.metadata.native_root)) return {Outcome::invalid_identity};
    std::wstring suffix; auto result = random_suffix(suffix); if (!result.ok()) return result;
    const auto prefix = "transport-backup-" + lease.metadata.namespace_id.substr(0, 16) + "-";
    backup.path = lease.descriptor.root + L"\\" + std::wstring(prefix.begin(), prefix.end()) + suffix;
    if (backup.path.size() > path_limit) return {Outcome::limit_exceeded};
    if (metadata.automatic) {
        result = publish_metadata(lease.metadata.path, L"automatic-" + suffix,
            prefix + ascii(suffix) + "\n");
        if (!result.ok()) return result;
    }
    if (!CreateDirectoryW(backup.path.c_str(), nullptr)) return io_error();
    Handle directory; result = open_directory(backup.path, directory, true); if (!result.ok()) return result;
    const auto deadline = GetTickCount64() + 10000; // checked between chunks; not a system-call latency guarantee
    std::vector<Handle> written_files;
    written_files.reserve(metadata.files.size());
    for (size_t i = 0; i < metadata.files.size(); ++i) {
        Handle file; result = open_file(backup.path + L"\\" + payload_name(i), GENERIC_READ | GENERIC_WRITE, 0, CREATE_NEW, file);
        if (!result.ok()) return result;
        result = copy_transport_payload(file.value, metadata.files[i], i, read, source, deadline, backup.bytes);
        if (!result.ok()) return result;
        written_files.push_back(std::move(file)); // keep earlier checked files fixed until publication
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
    result = parse_transport(text, lease.descriptor, lease.metadata.namespace_id, lease.metadata.native_root, owned->metadata); if (!result.ok()) return result;
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
Result parse_checkpoint_file(std::string_view part, const std::string& prefix, TransportFile& file) {
    const auto first = part.find(','), last = part.rfind(',');
    uint64_t size = 0;
    std::string hash;
    if (first == part.npos || first == last || part.substr(0, prefix.size()) != prefix || first <= prefix.size() ||
        first >= 260 || !transport_name(part.substr(prefix.size(), first - prefix.size())) ||
        !number64(part.substr(first + 1, last - first - 1), size) || !size || size > transport_file_limit ||
        !unhex(part.substr(last + 1), hash) || hash.size() != 32)
        return {Outcome::corrupt_manifest};
    file.name = part.substr(prefix.size(), first - prefix.size());
    file.size = static_cast<uint32_t>(size);
    std::memcpy(file.sha256.data(), hash.data(), hash.size());
    return {};
}
Result parse_checkpoint_files(std::string_view& text, uint64_t count, std::string_view directory,
                              std::vector<TransportFile>& files) {
    std::string_view part;
    const std::string prefix = std::string(directory) + "/";
    uint64_t total = 0;
    for (uint64_t i = 0; i < count; ++i) {
        if (!line(text, "file=", part)) return {Outcome::corrupt_manifest};
        TransportFile file;
        const auto parsed = parse_checkpoint_file(part, prefix, file);
        if (!parsed.ok()) return parsed;
        total += file.size;
        if (total > backup_byte_limit ||
            std::any_of(files.begin(), files.end(), [&](const auto& f) { return folded(f.name) == folded(file.name); }))
            return {Outcome::corrupt_manifest};
        files.push_back(std::move(file));
    }
    return {};
}
bool checkpoint_header(std::string_view& text, CheckpointRecord& record, uint64_t& count) {
    std::string_view part;
    uint64_t subtype = 1;
    if (!line(text, "checkpoint=", part) || !number64(part, record.number) || !record.number ||
        !line(text, "map=", part) || !transport_name(part))
        return false;
    record.map = part;
    if (text.substr(0, 8) == "subtype=") {
        if (!line(text, "subtype=", part) || !number64(part, subtype) || (subtype != 1 && subtype != 2)) return false;
    }
    record.subtype = static_cast<uint32_t>(subtype);
    if (!line(text, "files=", part) || !number64(part, count) || !count || count > 64) return false;
    return true;
}
Result parse_checkpoint(std::string_view text, std::string_view contract, std::string_view directory, CheckpointRecord& out) {
    out = {};
    if (text.size() > 32768 || contract.empty() || text.size() < contract.size()) return {Outcome::corrupt_manifest};
    if (text.substr(0, contract.size()) != contract)
        return {Outcome::identity_mismatch};
    text.remove_prefix(contract.size());
    if (text == "state=native_save_pending\n") return {Outcome::interrupted_preparation};
    CheckpointRecord record;
    uint64_t count = 0;
    if (!checkpoint_header(text, record, count)) return {Outcome::corrupt_manifest};
    const auto parsed = parse_checkpoint_files(text, count, directory, record.files);
    if (!parsed.ok()) return parsed;
    if (text != "state=native_saved_readback_verified\n") return {Outcome::corrupt_manifest};
    out = std::move(record); return {};
}
std::string checkpoint_contract(const Descriptor& descriptor, const std::string& id) {
    return "sentinel-campaign-v2\nnamespace=" + id + "\ngeneration=" +
        descriptor.identity.generation_fingerprint + "\nprovenance=synthetic-fixture\ncampaign=unified\nstarting_stage=hub\ndifficulty=" +
        std::to_string(descriptor.campaign.difficulty) + "\nslot=AUTOSAVE0\n";
}
const char* checkpoint_state_name(CheckpointState state) {
    switch (state) {
    case CheckpointState::missing: return "missing";
    case CheckpointState::confirmed: return "confirmed";
    case CheckpointState::interrupted: return "save_interrupted";
    case CheckpointState::receipt_ready: return "receipt_ready";
    case CheckpointState::conflict: return "conflict";
    case CheckpointState::identity_mismatch: return "identity_mismatch";
    case CheckpointState::unsupported: return "unsupported";
    default: return "invalid";
    }
}
namespace {
constexpr std::string_view ab_marker = "sentinel-checkpoint-ab-v1\n";
bool checksum(std::string_view text, std::string_view magic, std::string_view& body, std::string& hash) {
    if (text.size() > 32768 || text.substr(0, magic.size()) != magic) return false;
    const auto offset = text.rfind("sha256=");
    if (offset == text.npos || offset < magic.size() || text.size() - offset != 72 || text.back() != '\n') return false;
    hash = text.substr(offset + 7, 64);
    std::string actual;
    if (!lower_hex(hash) || !digest(text.substr(0, offset), actual).ok() || actual != hash) return false;
    body = text.substr(magic.size(), offset - magic.size()); return true;
}
struct Generation {
    CheckpointRecord record;
    std::string pending_hash, receipt_hash, commit_hash, predecessor, transaction, archive, text;
    bool present = false, receipt = false, committed = false;
    bool started = false;
    uint64_t steam_user = 0;
};
bool hash_field(std::string_view& text, std::string_view key, std::string& out, size_t length = 64) {
    std::string_view value;
    if (!line(text, key, value) || value.size() != length || !lower_hex(value)) return false;
    out = value; return true;
}
bool identity_field(std::string_view& text, std::string_view key, std::string_view expected, bool& mismatch) {
    std::string_view value;
    if (!line(text, key, value)) return false;
    if (value == expected) return true;
    mismatch = true; return false;
}
}
namespace {
void checkpoint_recovery_action(const std::string& journal, const std::string& complete, const Result& finished_journal,
                                const std::string& id, CheckpointPlan& plan) {
    std::string_view body, value;
    std::string hash;
    if (checksum(journal, "sentinel-checkpoint-recovery-v1\n", body, hash) && line(body, "namespace=", value) &&
        value == id && line(body, "source=", value) && line(body, "quarantine=", value) &&
        line(body, "selection_present=", value) && line(body, "selection_hex=", value) &&
        line(body, "action=", value)) {
        plan.recovery_id = hash;
        if (finished_journal.ok() && complete == journal)
            plan.recovery = value == "commit" ? CheckpointRecovery::promoted : CheckpointRecovery::restored;
    }
}
void checkpoint_recovery_status(Namespace& owner, const std::string& id, CheckpointPlan& plan) {
    std::string journal, complete;
    const auto pending_journal = owner.recovery_record(false, journal);
    if (pending_journal.ok() || pending_journal.outcome == Outcome::corrupt_manifest) {
        const auto finished_journal = owner.recovery_record(true, complete);
        if (!finished_journal.ok() || complete != journal) plan.recovery = CheckpointRecovery::pending;
        checkpoint_recovery_action(journal, complete, finished_journal, id, plan);
    }
}
struct CheckpointReader {
    Namespace& owner;
    const Descriptor& descriptor;
    const Metadata& metadata;
    const std::wstring& archive_root;
    CheckpointPlan& plan;
    std::string contract, directory;
    Generation generations[2];
    bool damaged = false, identity_error = false;

    CheckpointReader(Namespace& lease, const Descriptor& value, const std::wstring& root, CheckpointPlan& output)
        : owner(lease), descriptor(value), metadata(lease.metadata()), archive_root(root), plan(output),
          contract(checkpoint_contract(value, metadata.namespace_id)),
          directory(metadata.native_root + "/GAME-AUTOSAVE0") {}
    Result read(char side, const wchar_t* suffix, std::string& text) {
        return owner.checkpoint_record(side, suffix, text);
    }
    bool recover_legacy_marker(const std::string& text) {
        Handle legacy; std::string original;
        const auto saved = open_file(metadata.path + L"\\campaign.checkpoint.legacy", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, legacy);
        CheckpointRecord record;
        bool interrupted_marker = text.empty();
        if (saved.ok() && read_metadata(legacy.value, original, 32768).ok()) {
            for (size_t n = 1; n <= ab_marker.size(); ++n) {
                const std::string prefix(ab_marker.substr(0,n));
                interrupted_marker = interrupted_marker || text == prefix || text == prefix + original.substr(n);
            }
        }
        if (interrupted_marker && parse_checkpoint(original, contract, directory, record).ok()) {
            plan.state = CheckpointState::confirmed;
            plan.text = original;
            plan.checkpoint = record.number;
            return true;
        }
        return false;
    }
    void legacy_state(const std::string& text) {
        CheckpointRecord record;
        const auto result = parse_checkpoint(text, contract, directory, record);
        plan.state = result.ok()                                          ? CheckpointState::confirmed
                     : result.outcome == Outcome::interrupted_preparation ? CheckpointState::interrupted
                     : text.substr(0, 20) == "sentinel-checkpoint-"       ? CheckpointState::unsupported
                     : result.outcome == Outcome::identity_mismatch       ? CheckpointState::identity_mismatch
                                                                          : CheckpointState::invalid;
        plan.checkpoint = record.number;
        plan.text = text;
    }
    Result missing_contract(Result result) {
        std::string text;

        plan.state = result.win32_error == ERROR_FILE_NOT_FOUND ? CheckpointState::missing : CheckpointState::invalid;
        if (result.win32_error != ERROR_FILE_NOT_FOUND) return result;
        result = owner.campaign_record(true, text);
        if (result.ok() || result.win32_error != ERROR_FILE_NOT_FOUND) plan.state = CheckpointState::invalid;
        return result.ok() || result.win32_error == ERROR_FILE_NOT_FOUND ? Result{} : result;
    }
    Result read_legacy(bool& complete) {
        complete = true;
        std::string stored, text;
        auto result = owner.campaign_record(false, stored);
        if (!result.ok()) return missing_contract(result);
        if (stored != contract) {
            plan.state = CheckpointState::identity_mismatch;
            return {};
        }
        result = owner.campaign_record(true, text);
        if ((!result.ok() && result.outcome == Outcome::corrupt_manifest) ||
            (result.ok() && text != ab_marker && text.substr(0, 20) == "sentinel-checkpoint-")) {
            if (recover_legacy_marker(text)) return {};
        }
        if (!result.ok()) {
            plan.state =
                result.win32_error == ERROR_FILE_NOT_FOUND ? CheckpointState::interrupted : CheckpointState::invalid;
            return result.win32_error == ERROR_FILE_NOT_FOUND ? Result{} : result;
        }
        if (text != ab_marker) {
            legacy_state(text);
            return {};
        }
        plan.ab = true;
        complete = false;
        return {};
    }
    bool pending_identity(std::string_view& body, Generation& generation, bool& mismatch) {
        std::string_view value;
        std::string expected_hash;
        return identity_field(body, "namespace=", metadata.namespace_id, mismatch) &&
               identity_field(body, "generation=", descriptor.identity.generation_fingerprint, mismatch) &&
               digest(contract, expected_hash).ok() &&
               identity_field(body, "contract_sha256=", expected_hash, mismatch) &&
               identity_field(body, "native_root=", metadata.native_root, mismatch) &&
               line(body, "steam_user=", value) && number64(value, generation.steam_user) && generation.steam_user &&
               identity_field(body, "provider=", "Steam/782330/ISteamRemoteStorage014", mismatch);
    }
    bool pending_fields(const std::string& text, char side, Generation& generation, std::string_view& body,
                        bool& started) {
        std::string_view value;
        uint64_t number = 0;
        constexpr std::string_view started_magic = "sentinel-checkpoint-started-v1\n";
        started = text.substr(0, started_magic.size()) == started_magic;
        const bool valid =
            checksum(text, started ? "sentinel-checkpoint-started-v1\n" : "sentinel-checkpoint-pending-v1\n", body,
                     generation.pending_hash) &&
            pending_identity(body, generation, identity_error) && line(body, "checkpoint=", value) &&
            number64(value, number) && number && hash_field(body, "transaction=", generation.transaction, 32) &&
            generation.transaction != std::string(32, '0') && line(body, "side=", value) && value.size() == 1 &&
            value[0] == side && hash_field(body, "predecessor=", generation.predecessor);
        if (valid) generation.record.number = number;
        return valid;
    }
    Result missing_pending(char side, Generation& generation, Result result) {
        std::string text;
        if (!result.ok()) {
            if (result.outcome == Outcome::corrupt_manifest) {
                generation.present = true;
                damaged = true;
                return {};
            }
            if (result.win32_error != ERROR_FILE_NOT_FOUND) return result;
            for (const auto* suffix : {L".receipt", L".commit"}) {
                result = read(side, suffix, text);
                if (result.ok()) damaged = true;
                else if (result.win32_error != ERROR_FILE_NOT_FOUND) return result;
            }
            return {};
        }
        return {};
    }
    bool archive_fields(std::string_view& body, Generation& generation, std::string& manifest_hash) {
        std::string_view value;
        std::string checkpoint_text;
        const auto number = generation.record.number;
        if (!line(body, "archive=", value)) {
            return false;
        }
        generation.archive = value;
        if (!hash_field(body, "manifest_sha256=", manifest_hash) || !line(body, "checkpoint_hex=", value) ||
            !unhex(value, checkpoint_text) || !body.empty() ||
            !parse_checkpoint(checkpoint_text, contract, directory, generation.record).ok() ||
            generation.record.number != number || (number == 1 && generation.predecessor != std::string(64, '0'))) {
            return false;
        }
        generation.text = checkpoint_text;
        return true;
    }
    bool archive_identity(const TransportMetadata& transport, const Generation& generation) {
        if (transport.steam_user != generation.steam_user ||
            transport.provider != "Steam/782330/ISteamRemoteStorage014" || transport.quarantine ||
            transport.contract != contract || transport.checkpoint != generation.text ||
            transport.directory != directory)
            return false;
        return true;
    }
    Result verify_archive(Generation& generation, const std::string& manifest_hash, bool& valid) {
        std::unique_ptr<TransportArchive> archive;
        auto result =
            owner.reopen_transport(std::wstring(generation.archive.begin(), generation.archive.end()), archive);
        if (!result.ok()) {
            damaged = true;
            return {};
        }
        const auto& transport = archive->metadata();
        Handle manifest;
        result = open_file(archive_root + L"\\" + std::wstring(generation.archive.begin(), generation.archive.end()) +
                               L"\\transport.manifest",
                           GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, manifest);
        std::string manifest_text, actual_hash;
        if (!result.ok() || !read_metadata(manifest.value, manifest_text, 32768).ok() || !digest(manifest_text, actual_hash).ok()) return {Outcome::io_error};
        if (!archive_identity(transport, generation)) {
            identity_error = true;
            return {};
        }
        if (actual_hash != manifest_hash || transport.files.size() != generation.record.files.size() ||
            !std::all_of(transport.files.begin(), transport.files.end(), [&](const auto& f) {
                return std::any_of(generation.record.files.begin(), generation.record.files.end(), [&](const auto& p) {
                    return p.name == f.name && p.size == f.size && p.sha256 == f.sha256;
                });
            })) {
            damaged = true;
            return {};
        }
        valid = true;
        return {};
    }
    Result read_commit(char side, Generation& generation) {
        std::string text, pending_hash;
        std::string_view body;
        auto result = read(side, L".commit", text);
        if (!result.ok()) {
            if (result.outcome == Outcome::corrupt_manifest) {
                damaged = true;
                return {};
            }
            if (result.win32_error != ERROR_FILE_NOT_FOUND) return result;
            return {};
        }
        std::string receipt_hash;
        if (!checksum(text, "sentinel-checkpoint-commit-v1\n", body, generation.commit_hash) ||
            !hash_field(body, "pending_sha256=", pending_hash) || pending_hash != generation.pending_hash ||
            !hash_field(body, "receipt_sha256=", receipt_hash) || receipt_hash != generation.receipt_hash ||
            !body.empty()) {
            damaged = true;
            return {};
        }
        generation.committed = true;
        return {};
    }
    Result read_receipt(char side, Generation& generation) {
        std::string text, pending_hash;
        std::string_view body;
        auto result = read(side, L".receipt", text);
        if (!result.ok()) {
            if (result.outcome == Outcome::corrupt_manifest) {
                damaged = true;
                return {};
            }
            if (result.win32_error != ERROR_FILE_NOT_FOUND) return result;
            return {};
        }
        if (!checksum(text, "sentinel-checkpoint-receipt-v1\n", body, generation.receipt_hash) ||
            !hash_field(body, "pending_sha256=", pending_hash) || pending_hash != generation.pending_hash ||
            body != "native_success=1\ncallbacks=1\nreadback_terminal=1\ntransition=1\n") {
            damaged = true;
            return {};
        }
        generation.receipt = true;
        return read_commit(side, generation);
    }
    Result read_generation(char side, Generation& generation) {
        std::string text;
        auto result = read(side, L".pending", text);
        if (!result.ok()) return missing_pending(side, generation, result);
        generation.present = true;
        std::string_view body;
        bool started = false;
        if (!pending_fields(text, side, generation, body, started)) {
            damaged = true;
            return {};
        }
        if (started) {
            generation.started = body == "state=native_save_pending\n";
            if (!generation.started) damaged = true;
            return {};
        }
        std::string manifest_hash;
        if (!archive_fields(body, generation, manifest_hash)) {
            damaged = true;
            return {};
        }
        bool valid = false;
        result = verify_archive(generation, manifest_hash, valid);
        if (!result.ok() || !valid) return result;
        return read_receipt(side, generation);
    }
    Result read_generations() {
        for (unsigned i = 0; i < 2; ++i) {
            const auto result = read_generation(static_cast<char>('A' + i), generations[i]);
            if (!result.ok()) return result;
        }
        return {};
    }
    bool matching_generations() {
        auto* a = &generations[0];
        auto* b = &generations[1];
        if (a->receipt && b->receipt) {
            if (a->steam_user != b->steam_user) {
                plan.state = CheckpointState::identity_mismatch;
                return false;
            }
            if (a->record.number > b->record.number) std::swap(a, b);
            if (a->record.number == UINT64_MAX || b->record.number != a->record.number + 1 || !a->committed ||
                b->predecessor != a->commit_hash || a->transaction == b->transaction) {
                plan.state = CheckpointState::conflict;
                return false;
            }
        }
        return true;
    }
    bool next_checkpoint_number(std::string_view& body, const Generation& chosen) {
        std::string_view value;
        uint64_t number = 0;
        return line(body, "checkpoint=", value) && number64(value, number) && chosen.record.number != UINT64_MAX &&
               number == chosen.record.number + 1;
    }
    bool intent_identity(std::string_view& body, const Generation& chosen, bool& mismatch) {
        std::string_view value;
        std::string expected_hash;
        uint64_t user = 0;
        return identity_field(body, "namespace=", metadata.namespace_id, mismatch) &&
               identity_field(body, "generation=", descriptor.identity.generation_fingerprint, mismatch) &&
               digest(contract, expected_hash).ok() &&
               identity_field(body, "contract_sha256=", expected_hash, mismatch) &&
               identity_field(body, "native_root=", metadata.native_root, mismatch) &&
               line(body, "steam_user=", value) && number64(value, user) && user == chosen.steam_user &&
               identity_field(body, "provider=", "Steam/782330/ISteamRemoteStorage014", mismatch);
    }
    bool intent_fields(const std::string& intent, const Generation* chosen, char inactive, bool& mismatch) {
        std::string_view body, value;
        std::string hash, transaction, predecessor;
        const bool valid = checksum(intent, "sentinel-checkpoint-started-v1\n", body, hash) &&
                           intent_identity(body, *chosen, mismatch) && next_checkpoint_number(body, *chosen) &&
                           hash_field(body, "transaction=", transaction, 32) && transaction != std::string(32, '0') &&
                           line(body, "side=", value) && value == std::string(1, inactive) &&
                           hash_field(body, "predecessor=", predecessor) && predecessor == chosen->commit_hash &&
                           body == "state=native_save_pending\n";
        return valid;
    }
    Result inspect_intent(const Generation* chosen, bool& complete) {
        std::string intent;
        const char inactive = plan.side == 'A' ? 'B' : 'A';
        auto result = owner.checkpoint_record(inactive, L".started", intent);
        if (result.ok()) {
            bool mismatch = false;
            const bool valid = intent_fields(intent, chosen, inactive, mismatch);
            if (mismatch) {
                plan.state = CheckpointState::identity_mismatch;
                complete = true;
                return {};
            }
            plan.interrupted_write = plan.interrupted_write || (valid && chosen->committed);
        } else if (result.win32_error != ERROR_FILE_NOT_FOUND && result.outcome != Outcome::corrupt_manifest)
            return result;
        return {};
    }
    Result finish_plan(const Generation* chosen) {
        auto& previous = generations[plan.side == 'A' ? 1 : 0];
        if (previous.committed) {
            plan.previous_side = plan.side == 'A' ? 'B' : 'A';
            plan.previous_archive = previous.archive;
        }
        if (!chosen->committed && chosen->record.number > 1 &&
            (!previous.committed || chosen->predecessor != previous.commit_hash)) {
            plan.state = CheckpointState::invalid;
            return {};
        }
        const bool pending = damaged || plan.interrupted_write ||
                             std::any_of(std::begin(generations), std::end(generations),
                                         [](const auto& g) { return g.present && !g.committed; });
        plan.state = !chosen->committed ? CheckpointState::receipt_ready
                     : pending          ? CheckpointState::interrupted
                                        : CheckpointState::confirmed;
        return {};
    }
    void observe_interrupted_write(const Generation* chosen) {
        auto& previous = generations[plan.side == 'A' ? 1 : 0];
        plan.interrupted_write = previous.started && previous.steam_user == chosen->steam_user && chosen->committed &&
                                 chosen->record.number != UINT64_MAX &&
                                 previous.record.number == chosen->record.number + 1 &&
                                 previous.predecessor == chosen->commit_hash;
    }
    Result select() {
        if (identity_error) {
            plan.state = CheckpointState::identity_mismatch;
            return {};
        }
        if (!matching_generations()) return {};
        Generation* chosen = nullptr;
        for (auto& generation : generations)
            if (generation.receipt && (!chosen || generation.record.number > chosen->record.number))
                chosen = &generation;
        if (!chosen) {
            plan.state = damaged ? CheckpointState::invalid : CheckpointState::interrupted;
            return {};
        }
        plan.side = chosen == &generations[0] ? 'A' : 'B';
        plan.checkpoint = chosen->record.number;
        plan.archive = chosen->archive;
        plan.text = chosen->text;
        plan.steam_user = chosen->steam_user;
        plan.committed = chosen->committed;
        plan.commit_hash = chosen->commit_hash;
        plan.pending_hash = chosen->pending_hash;
        plan.receipt_hash = chosen->receipt_hash;
        observe_interrupted_write(chosen);
        bool complete = false;
        const auto result = inspect_intent(chosen, complete);
        if (!result.ok() || complete) return result;
        return finish_plan(chosen);
    }
};
} // namespace
Result Namespace::checkpoint_plan(const Descriptor& descriptor, CheckpointPlan& plan) {
    plan = {};
    std::string id;
    if (!namespace_id(descriptor.identity, id).ok() || id != metadata().namespace_id) {
        plan.state = CheckpointState::identity_mismatch;
        return {};
    }
    checkpoint_recovery_status(*this, id, plan);
    CheckpointReader reader(*this, descriptor, impl_->lease.descriptor.root, plan);
    bool complete = false;
    auto result = reader.read_legacy(complete);
    if (!result.ok() || complete) return result;
    result = reader.read_generations();
    return result.ok() ? reader.select() : result;
}

namespace {
std::string signed_checkpoint(std::string body, std::string* hash = nullptr) {
    std::string value;
    if (!digest(body, value).ok()) return {};
    if (hash) *hash = value;
    return body + "sha256=" + value + "\n";
}
Result erase_handle(HANDLE handle) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) ? Result{} : io_error();
}
}
Result Namespace::checkpoint_record(char side, const wchar_t* suffix, std::string& text) const {
    if (side != 'A' && side != 'B') return {Outcome::invalid_descriptor};
    text.clear(); Handle file;
    auto r = open_file(metadata().path + L"\\checkpoint-" + static_cast<wchar_t>(side) + suffix,
        GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, file);
    return r.ok() ? read_metadata(file.value, text, 32768) : r;
}
Result Namespace::write_checkpoint_record(char side, const wchar_t* suffix, std::string_view text) {
    if ((side != 'A' && side != 'B') || text.empty() || text.size() > 32768) return {Outcome::invalid_descriptor};
    Handle file;
    auto r = open_file(metadata().path + L"\\checkpoint-" + static_cast<wchar_t>(side) + suffix,
        GENERIC_WRITE | GENERIC_READ, 0, OPEN_ALWAYS, file);
    if (!r.ok()) return r;
    r = write_bytes(file.value, text.data(), static_cast<DWORD>(text.size()));
    if (!r.ok()) return r;
    if (!SetEndOfFile(file.value) || !FlushFileBuffers(file.value)) return io_error();
    std::string actual; r = read_metadata(file.value, actual, 32768);
    return r.ok() && actual != text ? Result{Outcome::payload_mismatch} : r;
}
bool checkpoint_write_allowed(const CheckpointPlan& plan, bool fresh, uint64_t user) {
    if (!user || (!fresh && plan.state != CheckpointState::confirmed) || (plan.ab && user != plan.steam_user) ||
        (plan.ab && plan.checkpoint == UINT64_MAX))
        return false;
    return true;
}
Result Namespace::begin_checkpoint(const Descriptor& descriptor, uint64_t user, CheckpointWrite& write) {
    CheckpointPlan plan; auto r = checkpoint_plan(descriptor, plan);
    if (!r.ok()) return r;
    std::string current; const auto existing = campaign_record(true, current);
    const bool fresh = !existing.ok() && existing.win32_error == ERROR_FILE_NOT_FOUND;
    if (!checkpoint_write_allowed(plan, fresh, user)) return {Outcome::interrupted_preparation};
    write = {}; write.side = plan.side == 'A' ? 'B' : 'A';
    write.number = fresh ? 1 : plan.ab ? plan.checkpoint + 1 : plan.checkpoint;
    std::wstring nonce; r = random_suffix(nonce); if (!r.ok()) return r;
    std::string contract_hash; r = digest(checkpoint_contract(descriptor, metadata().namespace_id), contract_hash);
    if (!r.ok()) return r;
    write.prefix = "namespace=" + metadata().namespace_id + "\ngeneration=" + descriptor.identity.generation_fingerprint +
        "\ncontract_sha256=" + contract_hash + "\nnative_root=" + metadata().native_root + "\nsteam_user=" + std::to_string(user) +
        "\nprovider=Steam/782330/ISteamRemoteStorage014\ncheckpoint=" + std::to_string(write.number) +
        "\ntransaction=" + ascii(nonce) + "\nside=" + write.side +
        "\npredecessor=" + (plan.ab ? plan.commit_hash : std::string(64, '0')) + "\n";
    if (fresh) { r = activate_checkpoints(); if (!r.ok()) return r; }
    const auto intent = signed_checkpoint("sentinel-checkpoint-started-v1\n" + write.prefix + "state=native_save_pending\n");
    r = write_checkpoint_record(write.side, L".started", intent); if (!r.ok()) return r;
    return write_checkpoint_record(write.side, L".pending", intent);
}
Result Namespace::stage_checkpoint(CheckpointWrite& write, std::wstring_view basename) {
    std::unique_ptr<TransportArchive> archive;
    auto r = reopen_transport(basename, archive); if (!r.ok()) return r;
    const auto& source = archive->metadata();
    CheckpointRecord record;
    if (!parse_checkpoint(source.checkpoint, source.contract, source.directory, record).ok() || record.number != write.number ||
        write.prefix.find("steam_user=" + std::to_string(source.steam_user) + "\n") == std::string::npos || source.quarantine)
        return {Outcome::identity_mismatch};
    Handle manifest;
    r = open_file(impl_->lease.descriptor.root + L"\\" + std::wstring(basename) + L"\\transport.manifest",
        GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, manifest);
    std::string text, manifest_hash;
    if (!r.ok()) return r;
    r = read_metadata(manifest.value, text, 32768); if (!r.ok()) return r;
    r = digest(text, manifest_hash); if (!r.ok()) return r;
    return write_checkpoint_record(write.side, L".pending", signed_checkpoint("sentinel-checkpoint-pending-v1\n" + write.prefix +
        "archive=" + ascii(basename) + "\nmanifest_sha256=" + manifest_hash +
        "\ncheckpoint_hex=" + hex(source.checkpoint) + "\n", &write.pending_hash));
}
Result Namespace::receipt_checkpoint(CheckpointWrite& write) {
    if (write.pending_hash.size() != 64) return {Outcome::invalid_descriptor};
    return write_checkpoint_record(write.side, L".receipt", signed_checkpoint("sentinel-checkpoint-receipt-v1\npending_sha256=" +
        write.pending_hash + "\nnative_success=1\ncallbacks=1\nreadback_terminal=1\ntransition=1\n", &write.receipt_hash));
}
Result Namespace::commit_checkpoint(const CheckpointWrite& write) {
    if (write.pending_hash.size() != 64 || write.receipt_hash.size() != 64) return {Outcome::invalid_descriptor};
    return write_checkpoint_record(write.side, L".commit", signed_checkpoint("sentinel-checkpoint-commit-v1\npending_sha256=" +
        write.pending_hash + "\nreceipt_sha256=" + write.receipt_hash + "\n"));
}
Result repair_legacy_checkpoint(Namespace& owner, HANDLE file, std::string_view legacy, const std::string& old) {
    std::string current;
    if (!owner.campaign_record(true, current).ok() || current != legacy || old.size() >= legacy.size() ||
        legacy.substr(0, old.size()) != old)
        return {Outcome::payload_mismatch};
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return io_error();
    auto r = write_bytes(file, legacy.data(), static_cast<DWORD>(legacy.size()));
    if (!r.ok()) return r;
    if (!SetEndOfFile(file) || !FlushFileBuffers(file)) return io_error();
    return {};
}
Result preserve_legacy_checkpoint(Namespace& owner, std::string_view legacy) {
    if (!legacy.empty()) {
        const auto path = owner.metadata().path + L"\\campaign.checkpoint.legacy";
        Handle file; auto r = open_file(path, GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS, file);
        if (!r.ok()) return r;
        LARGE_INTEGER size{}; if (!GetFileSizeEx(file.value, &size)) return io_error();
        if (size.QuadPart) {
            std::string old; r = read_metadata(file.value, old, 32768);
            if (!r.ok() || old != legacy) {
                r = repair_legacy_checkpoint(owner, file.value, legacy, old);
                if (!r.ok()) return r;
            }
        } else {
            r = write_bytes(file.value, legacy.data(), static_cast<DWORD>(legacy.size())); if (!r.ok()) return r;
            if (!FlushFileBuffers(file.value)) return io_error();
        }
        std::string preserved; r = read_metadata(file.value,preserved,32768);
        if (!r.ok() || preserved != legacy) return {Outcome::payload_mismatch};
    }
    return {};
}
Result Namespace::activate_checkpoints(std::string_view legacy) {
    if (!legacy.empty()) {
        const auto preserved = preserve_legacy_checkpoint(*this, legacy);
        if (!preserved.ok()) return preserved;
    }
    std::string current; const auto found = campaign_record(true, current);
    auto r = publish_campaign_record(true, ab_marker, !found.ok() && found.win32_error == ERROR_FILE_NOT_FOUND);
    if (!r.ok()) return r;
    r = campaign_record(true, current);
    return r.ok() && current != ab_marker ? Result{Outcome::payload_mismatch} : r;
}
Result Namespace::discard_checkpoint(char side) {
    for (const auto* suffix : {L".pending", L".receipt", L".commit", L".started"}) {
        Handle file;
        auto r = open_file(metadata().path + L"\\checkpoint-" + static_cast<wchar_t>(side) + suffix, DELETE, 0, OPEN_EXISTING, file);
        if (!r.ok()) { if (r.win32_error == ERROR_FILE_NOT_FOUND) continue; return r; }
        r = erase_handle(file.value); if (!r.ok()) return r;
    }
    return {};
}
Result retained_checkpoint_archives(Namespace& owner, std::vector<std::string>& retained) {
    for (const char side : {'A', 'B'}) {
        std::string text, hash; std::string_view body, name;
        auto r = owner.checkpoint_record(side, L".pending", text);
        if (!r.ok()) { if (r.win32_error == ERROR_FILE_NOT_FOUND) continue; return r; }
        if (!checksum(text, "sentinel-checkpoint-pending-v1\n", body, hash)) return {Outcome::corrupt_manifest};
        const auto at = body.find("archive=");
        if (at == body.npos) return {Outcome::corrupt_manifest};
        body.remove_prefix(at);
        if (!line(body, "archive=", name)) return {Outcome::corrupt_manifest};
        retained.emplace_back(name);
    }
    return {};
}
Result erase_archive_files(const std::wstring& path, std::vector<Handle>& files) {
    Result result;
    WIN32_FIND_DATAW child{};
    HANDLE scan = FindFirstFileW((path + L"\\*").c_str(), &child);
    if (scan == INVALID_HANDLE_VALUE) return io_error();
    do {
        const std::wstring leaf(child.cFileName);
        if (leaf == L"." || leaf == L"..") continue;
        bool known = leaf == transport_manifest;
        for (size_t i = 0; i < 16; ++i) known |= leaf == payload_name(i);
        if (!known || files.size() == 17) {
            result = {Outcome::unsafe_path};
            break;
        }
        Handle file;
        result = open_file(path + L"\\" + leaf, DELETE, 0, OPEN_EXISTING, file);
        if (!result.ok()) break;
        files.push_back(std::move(file));
    } while (FindNextFileW(scan, &child));
    const auto error = GetLastError();
    FindClose(scan);
    if (!result.ok()) return result;
    if (error != ERROR_NO_MORE_FILES) {
        result = io_error(error);
        return result;
    }
    for (auto& file : files) {
        result = erase_handle(file.value);
        if (!result.ok()) return result;
    }
    if (!result.ok()) return result;
    return {};
}
bool automatic_nonce(const std::wstring& nonce) {
    if (nonce.size() != 32 || !lower_hex(ascii(nonce)) || nonce.find_first_not_of(L"0123456789abcdef") != nonce.npos)
        return false;
    return true;
}
Result prune_automatic_archive(const Metadata& metadata, const std::wstring& archive_root,
                               const std::vector<std::string>& retained, const std::wstring& tag) {
    Result result;
    const auto& base = metadata.path;
    const auto nonce = tag.substr(10);
    if (!automatic_nonce(nonce)) return {Outcome::unsafe_path};
    const auto name = "transport-backup-" + metadata.namespace_id.substr(0, 16) + "-" + ascii(nonce);
    if (std::find(retained.begin(), retained.end(), name) != retained.end()) return {};
    Handle registry;
    result = open_file(base + L"\\" + tag, GENERIC_READ | DELETE, 0, OPEN_EXISTING, registry);
    std::string registered;
    if (!result.ok()) return result;
    const auto registration = read_metadata(registry.value, registered);
    if (!registration.ok() && registration.outcome != Outcome::corrupt_manifest) {
        result = registration;
        return result;
    }
    const auto path = archive_root + L"\\" + std::wstring(name.begin(), name.end());
    Handle directory(CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                 nullptr));
    if (!directory) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            result = io_error();
            return result;
        }
    } else {
        if (registered != name + "\n") {
            result = {Outcome::unsafe_path};
            return result;
        }
        result = validate_handle(directory.value, path, true);
        if (!result.ok() || !validate_streams(directory.value, true).ok()) {
            result = {Outcome::unsafe_path};
            return result;
        }
        std::vector<Handle> files;
        result = erase_archive_files(path, files);
        if (!result.ok()) return result;
        files.clear();
        result = erase_handle(directory.value);
        if (!result.ok()) return result;
    }
    result = erase_handle(registry.value);
    if (!result.ok()) return result;
    return {};
}
Result Namespace::prune_checkpoints() {
    std::string journal;
    const auto p = recovery_record(false, journal);
    if (p.ok()) {
        std::string complete;
        if (!recovery_record(true, complete).ok() || journal != complete) return {Outcome::interrupted_preparation};
    } else if (p.win32_error != ERROR_FILE_NOT_FOUND)
        return p;
    std::vector<std::string> retained;
    auto ready = retained_checkpoint_archives(*this, retained);
    if (!ready.ok()) return ready;
    WIN32_FIND_DATAW data{};
    const auto base = metadata().path;
    HANDLE find = FindFirstFileW((base + L"\\automatic-*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? Result{} : io_error();
    Result result;
    do {
        result = prune_automatic_archive(metadata(), impl_->lease.descriptor.root, retained, data.cFileName);
        if (!result.ok()) break;
    } while (FindNextFileW(find, &data));
    FindClose(find); return result;
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
