// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_probe.h"
#include "save_storage.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace {
using sentinel::storage::Outcome;
using sentinel::storage::Result;

std::string quoted(const std::string& text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) {
            char escaped[7]{};
            sprintf_s(escaped, "\\u%04x", static_cast<unsigned>(c));
            result += escaped;
        } else result += static_cast<char>(c);
    }
    return result + '"';
}

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), length, nullptr, 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
        result.data(), size, nullptr, nullptr)) return {};
    return result;
}

std::string nullable(const std::string& text) { return text.empty() ? "null" : quoted(text); }

const char* state_name(Result result, bool inspected) {
    switch (result.outcome) {
    case Outcome::storage_prepared: return "metadata_prepared";
    case Outcome::storage_reopened: return "metadata_reopened";
    case Outcome::offline_backup_complete: return "offline_fixture_backup_complete";
    case Outcome::ok: return inspected ? "metadata_present" : "not_inspected";
    case Outcome::interrupted_preparation: return "interrupted";
    case Outcome::corrupt_manifest: return "corrupt_metadata";
    case Outcome::unsupported_manifest: return "unsupported_metadata";
    case Outcome::identity_mismatch: return "identity_mismatch";
    case Outcome::ownership_conflict: return "busy";
    case Outcome::namespace_missing: return "missing";
    default: return inspected ? "unknown" : "not_inspected";
    }
}

int print_result(const char* operation, Result result,
    const sentinel::storage::Metadata& metadata, const sentinel::storage::Backup& backup,
    bool inspected, bool usage = false) {
    const auto id = nullable(metadata.namespace_id);
    const auto path = nullable(utf8(metadata.path));
    const auto backup_path = nullable(utf8(backup.path));
    // Descriptor/parser and storage API bounds cap every emitted string and file count.
    std::printf("{\"operation\":\"%s\",\"outcome\":\"%s\",\"win32_error\":%lu,"
        "\"namespace_id\":%s,\"namespace_path\":%s,\"storage_state\":\"%s\","
        "\"backup_path\":%s,\"backup_files\":%lu,\"backup_bytes\":%llu,"
        "\"scope\":\"offline_synthetic_fixture_metadata\","
        "\"native_admitted\":false,\"native_completed\":false,"
        "\"native_persisted\":false,\"reconciliation\":\"not_performed\"}\n",
        operation, usage ? "usage" : sentinel::storage::outcome_name(result.outcome),
        static_cast<unsigned long>(result.win32_error), id.c_str(), path.c_str(),
        state_name(result, inspected), backup_path.c_str(), static_cast<unsigned long>(backup.files),
        static_cast<unsigned long long>(backup.bytes));
    return usage ? 2 : (result.ok() ? 0 : 1);
}
} // namespace

int save_storage_command(int argc, wchar_t** argv) {
    int mode_index = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::wcsncmp(argv[i], L"--save-session-", 15) == 0) { mode_index = i; break; }
    }
    if (!mode_index) return -1;
    const wchar_t* mode = argv[mode_index];
    const char* operation = std::wcscmp(mode, L"--save-session-plan") == 0 ? "save_session_plan" :
        std::wcscmp(mode, L"--save-session-prepare") == 0 ? "save_session_prepare" :
        std::wcscmp(mode, L"--save-session-backup") == 0 ? "save_session_backup" :
        std::wcscmp(mode, L"--save-session-reopen") == 0 ? "save_session_reopen" : nullptr;
    sentinel::storage::Metadata metadata;
    sentinel::storage::Backup backup;
    if (!operation || mode_index != 1 || argc != 3)
        return print_result(operation ? operation : "save_session", {Outcome::invalid_descriptor,
            ERROR_INVALID_PARAMETER}, metadata, backup, false, true);

    sentinel::storage::Descriptor descriptor;
    auto result = sentinel::storage::read_descriptor_file(argv[2], descriptor);
    if (result.ok()) result = sentinel::storage::namespace_id(descriptor.identity, metadata.namespace_id);
    if (!result.ok()) return print_result(operation, result, metadata, backup, false);
    metadata.path = descriptor.root + L"\\" + std::wstring(metadata.namespace_id.begin(), metadata.namespace_id.end());

    std::unique_ptr<sentinel::storage::Namespace> storage;
    if (std::wcscmp(mode, L"--save-session-plan") == 0) {
        sentinel::storage::Metadata observed;
        result = sentinel::storage::inspect(descriptor, observed);
        if (result.ok()) {
            metadata = observed;
            result.outcome = Outcome::ok; // Inspection is not a reopen operation.
        }
    } else {
        result = std::wcscmp(mode, L"--save-session-prepare") == 0 ?
            sentinel::storage::prepare(descriptor, storage) : sentinel::storage::reopen(descriptor, storage);
        if (result.ok()) {
            metadata = storage->metadata();
            if (std::wcscmp(mode, L"--save-session-backup") == 0) result = storage->backup_offline(backup);
        }
    }
    return print_result(operation, result, metadata, backup, true);
}
