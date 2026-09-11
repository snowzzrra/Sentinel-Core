// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
// Owned synthetic invocations; never calls the executable or a real save API.
#include "save_submission.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "FAIL submission:%d: %s\n", __LINE__, #v); std::exit(1); } } while (0)
using namespace sentinel::save;
namespace {
constexpr uintptr_t caller = 0x674744, task = 0x3344;
constexpr char directory[] = "ap-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/GAME-AUTOSAVE0";
struct Model {
    NativeWrites writes;
    unsigned mode = 0, releases = 0, native_calls = 0, writes_seen = 0;
    uint64_t captured = 0;
};
thread_local Model* active = nullptr;
bool write(Model& m, const char* name = directory) {
    const auto operation = m.writes.open_provider(0x4000 + ++m.writes_seen, name);
    CHECK(operation);
    const bool captured = capture_submission(m.writes, operation, name);
    if (captured && m.writes.backup(operation)) m.captured = operation;
    return captured;
}
SaveReference* empty_factory(uintptr_t, SaveReference* out, uint32_t, uintptr_t) { return out; }
SaveReference* make(uintptr_t manager, SaveReference* out, uint32_t user, uintptr_t request) {
    auto& m = *active; ++m.native_calls;
    CHECK(manager == 0x111 && user == 7 && request == 0x222);
    if (m.mode == 13) RaiseException(0xe0424444, 0, 0, nullptr);
    if (m.mode == 12) native_save_factory(m.writes, caller, caller, manager, out, user, request, empty_factory);
    const bool accepted = write(m, m.mode == 2 ? "ap-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/DLC1-AUTOSAVE0" : directory);
    CHECK(accepted == (m.mode != 1 && m.mode != 2 && m.mode != 12 && !(m.mode == 3 && m.native_calls == 2)));
    if (m.mode == 4) CHECK(!write(m));
    if (m.mode == 10) {
        // TLS correlation never crosses onto an unrelated worker thread.
        std::thread worker([&] { const auto id = m.writes.open_provider(0x9999, directory);
            CHECK(capture_submission(m.writes, id, directory) && !m.writes.backup(id)); });
        worker.join();
    }
    if (m.mode == 11) {
        NativeWrites other; const auto id = other.open_provider(0x9999, directory);
        CHECK(capture_submission(other, id, directory) && !other.backup(id));
    }
    if (m.mode != 5) out->control = task;
    if (m.mode == 14) RaiseException(0xe0424444, 0, 0, nullptr);
    return out;
}
void release(SaveReference* ref) { CHECK(ref->control == task); ++active->releases; ref->control = 0; }
SaveReference* root(uintptr_t root_value, SaveReference* out, uint8_t a, uint8_t b, uint8_t c) {
    auto& m = *active; CHECK(root_value == 0x123 && !a && !b && !c);
    if (m.mode == 6) RaiseException(0xe0424444, 0, 0, nullptr);
    // Native helper work preceding the slot factory must remain unassociated.
    if (m.mode == 9) CHECK(write(m) && !m.captured);
    if (m.mode != 8) native_save_factory(m.writes, m.mode == 1 ? caller + 1 : caller, caller,
        0x111, out, 7, 0x222, make);
    if (m.mode == 3) native_save_factory(m.writes, caller, caller, 0x111, out, 7, 0x222, make);
    if (m.mode == 7) RaiseException(0xe0424444, 0, 0, nullptr);
    return out;
}
}
void run_submission_contracts() {
    for (unsigned mode = 0; mode < 15; ++mode) {
        Model m; m.mode = mode; active = &m;
        auto job = std::make_shared<BackupJob>(GetCurrentProcessId(), 123456, GetTickCount64() + 10000);
        const auto result = submit_native_save(m.writes, job, directory, {root, release, 0x123, caller});
        CHECK(result.entered && result.operation == m.captured);
        CHECK(result.exception == (mode == 6 || mode == 7 || mode == 13 || mode == 14 ? 0xe0424444u : 0u));
        CHECK(result.matched == (mode == 0 || mode == 9 || mode == 10 || mode == 11));
        CHECK(m.releases == (mode == 5 || mode == 6 || mode == 8 || mode == 13 ? 0u : 1u));
        const auto progress = job->progress();
        CHECK(!progress.storage_attempted && progress.output.path.empty());
        if (result.matched) CHECK(progress.state == BackupState::pending && progress.operation == result.operation);
        else if (result.operation) CHECK(progress.cancel_requested);
        else CHECK(progress.state == BackupState::failed);
        // Includes the SEH paths: neither TLS pointer may reference a dead frame.
        const auto id = m.writes.open_provider(0x8888, directory);
        CHECK(capture_submission(m.writes, id, directory) && !m.writes.backup(id));
        for (uint64_t n = 1; n <= id; ++n) m.writes.close_provider(n);
    }
    active = nullptr;
    std::puts("PASS synchronous native submission identity, task ownership, refusal, TLS and SEH contracts");
}
