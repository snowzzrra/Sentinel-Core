#include "prelaunch.h"
#include <windows.h>
#include <charconv>
#include <sstream>
#include <bcrypt.h>
namespace sentinel::prelaunch {
namespace {
std::wstring observed_key;
struct Handle {
    HANDLE value;
    ~Handle() { if (value) CloseHandle(value); }
};
storage::Result refused(DWORD error) { return {storage::Outcome::invalid_descriptor, error}; }
bool hex(const std::string& text, size_t length) {
    return text.size() == length && text.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool number(const std::string& text, uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data()+text.size() && std::to_string(value) == text;
}
}
const std::wstring& diagnostic_key() { return observed_key; }
storage::Result lease_key(std::string_view control, std::wstring& key) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0) < 0) return refused(ERROR_GEN_FAILURE);
    unsigned char hash[32]{};
    BCRYPT_HASH_HANDLE handle = nullptr;
    auto status = BCryptCreateHash(algorithm,&handle,nullptr,0,nullptr,0,0);
    if (status >= 0) status = BCryptHashData(handle,reinterpret_cast<PUCHAR>(const_cast<char*>(control.data())),static_cast<ULONG>(control.size()),0);
    if (status >= 0) status = BCryptFinishHash(handle,hash,sizeof(hash),0);
    if (handle) BCryptDestroyHash(handle);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if (status < 0) return refused(ERROR_GEN_FAILURE);
    key = L"Local\\SentinelA-";
    for (auto byte : hash) { key += L"0123456789abcdef"[byte>>4]; key += L"0123456789abcdef"[byte&15]; }
    return {};
}
static bool read_control_field(std::istream& input, std::string& value, const char* prefix) {
    const std::string_view expected(prefix);
    return static_cast<bool>(std::getline(input, value)) && value.substr(0, expected.size()) == expected;
}

static bool valid_run_identity(const std::string& run, const std::string& reference, const std::string& owner,
                               const std::string& created, uint64_t& pid, uint64_t& creation) {
    return hex(run, 32) && hex(reference, 64) && number(owner, pid) && pid && pid <= MAXDWORD &&
           number(created, creation);
}

static storage::Result parse_run_control(const std::string& text, storage::Descriptor& descriptor,
                                         uint64_t& pid, uint64_t& creation) {
    std::istringstream input(text);
    std::string version, run, reference, owner, created;
    if (!std::getline(input, version) || version != "sentinel-run-v1" ||
        !read_control_field(input, run, "run=") || !read_control_field(input, reference, "protection=") ||
        !read_control_field(input, owner, "owner=") || !read_control_field(input, created, "created="))
        return refused(ERROR_INVALID_DATA);
    run.erase(0, 4);
    reference.erase(0, 11);
    owner.erase(0, 6);
    created.erase(0, 8);
    if (!valid_run_identity(run, reference, owner, created, pid, creation))
        return refused(ERROR_INVALID_DATA);
    const auto position = input.tellg();
    if (position < 0) return refused(ERROR_INVALID_DATA);
    const auto result =
        storage::parse_descriptor(std::string_view(text).substr(static_cast<size_t>(position)), descriptor);
    if (!result.ok()) return result;
    return {};
}

static storage::Result verify_run_owner(uint64_t pid, uint64_t creation) {
    Handle process{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid))};
    if (!process.value) return refused(GetLastError());
    FILETIME actual{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process.value, &actual, &exit, &kernel, &user)) return refused(GetLastError());
    const uint64_t actual_creation =
        (static_cast<uint64_t>(actual.dwHighDateTime) << 32) | actual.dwLowDateTime;
    if (actual_creation != creation || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT)
        return refused(ERROR_INVALID_OWNER);
    Handle lease{OpenSemaphoreW(SYNCHRONIZE, FALSE, observed_key.c_str())};
    if (!lease.value) return refused(GetLastError());
    if (WaitForSingleObject(lease.value, 0) != WAIT_OBJECT_0) return refused(ERROR_BUSY);
    return {};
}

static storage::Result read_environment_descriptor(DWORD needed, DWORD env_error,
                                                   storage::Descriptor& descriptor) {
    if (!needed || needed > 32761) return refused(env_error ? env_error : ERROR_INVALID_DATA);
    std::wstring path(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", path.data(), needed);
    if (!written || written >= needed) return refused(GetLastError());
    path.resize(written);
    return storage::read_descriptor_file(path.c_str(), descriptor);
}

storage::Result resolve(const wchar_t* local_file, storage::Descriptor& descriptor, bool& requested) {
    requested = false;
    SetLastError(ERROR_SUCCESS);
    const DWORD needed = GetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION", nullptr, 0);
    const DWORD env_error = GetLastError();
    const DWORD attributes = GetFileAttributesW(local_file);
    const DWORD file_error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    const bool local = attributes != INVALID_FILE_ATTRIBUTES || (file_error != ERROR_FILE_NOT_FOUND && file_error != ERROR_PATH_NOT_FOUND);
    const bool env = needed || env_error != ERROR_ENVVAR_NOT_FOUND;
    requested = local || env;
    if (!requested) return {};
    std::string text;
    storage::Result local_read;
    if (local) {
        local_read = storage::read_control_text(local_file,text);
        if (local_read.ok()) local_read = lease_key(text,observed_key);
    }
    if (local && env) return refused(ERROR_DUP_NAME);
    if (env) {
        return read_environment_descriptor(needed, env_error, descriptor);
    }
    auto result = local_read;
    if (!result.ok()) return result;
    uint64_t pid = 0, creation = 0;
    result = parse_run_control(text, descriptor, pid, creation);
    if (!result.ok()) return result;
    return verify_run_owner(pid, creation);
}
storage::Result resolve(storage::Descriptor& descriptor, bool& requested) {
    std::wstring path(32761,L'\0');
    const DWORD size = GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) { requested = true; return refused(ERROR_BAD_PATHNAME); }
    path.resize(size);
    path.resize(path.find_last_of(L"\\/")+1);
    path += L"sentinel-prelaunch.txt";
    return resolve(path.c_str(),descriptor,requested);
}
}
