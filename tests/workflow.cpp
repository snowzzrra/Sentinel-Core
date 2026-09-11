#include "prelaunch.h"
#include "startup_log.h"
#include "save_session.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
namespace fs = std::filesystem;
void check(bool value, const char* label) { if (!value) throw std::runtime_error(label); }
int main() {
    const fs::path root = fs::current_path() / ("workflow-fixture-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    try {
        fs::create_directory(root);
        SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
        SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION",nullptr);
        FILETIME creation{}, exited{}, kernel{}, user{};
        check(GetProcessTimes(GetCurrentProcess(),&creation,&exited,&kernel,&user)!=0,"owner time");
        const uint64_t created = (static_cast<uint64_t>(creation.dwHighDateTime)<<32) | creation.dwLowDateTime;
        const std::string run(32,'a'), reference(64,'b');
        const auto text = "sentinel-run-v1\nrun=" + run + "\nprotection=" + reference + "\nowner=" + std::to_string(GetCurrentProcessId()) +
            "\ncreated=" + std::to_string(created) + "\nsentinel-test-session-v1\nseed_hex=74657374\nteam=0\nslot=1\ngeneration_fingerprint=" +
            std::string(64,'c') + "\nprovenance=synthetic-fixture\nroot=" + root.u8string() + "\n";
        const auto file = root / "prelaunch.txt";
        sentinel::storage::Descriptor descriptor;
        bool requested = false;
        check(sentinel::prelaunch::resolve(file.c_str(),descriptor,requested).ok() && !requested,"absent is inactive");
        { std::ofstream out(file,std::ios::binary); out << text; }
        check(!sentinel::prelaunch::resolve(file.c_str(),descriptor,requested).ok() && requested,"stale is refused");
        SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION",file.c_str());
        check(sentinel::prelaunch::resolve(file.c_str(),descriptor,requested).win32_error == ERROR_DUP_NAME,"configuration conflict");
        SetEnvironmentVariableW(L"SENTINEL_AP_TEST_SESSION",nullptr);
        std::wstring wide;
        check(sentinel::prelaunch::lease_key(text,wide).ok(),"exact control lease identity");
        HANDLE lease = CreateSemaphoreW(nullptr,1,1,wide.c_str());
        check(lease != nullptr,"live lease");
        auto changed = text; changed.replace(changed.find("74657374"),8,"74657375");
        { std::ofstream out(file,std::ios::binary); out << changed; }
        check(!sentinel::prelaunch::resolve(file.c_str(),descriptor,requested).ok(),"changed descriptor cannot consume another preparation lease");
        { std::ofstream out(file,std::ios::binary); out << text; }
        check(sentinel::prelaunch::resolve(file.c_str(),descriptor,requested).ok() && requested,"live one-use handoff");
        check(sentinel::prelaunch::resolve(file.c_str(),descriptor,requested).win32_error == ERROR_BUSY,"second consumption refused");
        CloseHandle(lease);
        check(!sentinel::prelaunch::resolve(file.c_str(),descriptor,requested).ok(),"closed lease is stale");
        sentinel::Snapshot core{};
        core.pid=GetCurrentProcessId(); core.process_created=created;
        strcpy_s(core.core.build_id,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        auto& install = sentinel::save::session().installation;
        install.initialize();
        sentinel::startup_log::record(core);
        auto event=install.begin(SC_INSTALL_PRELAUNCH);
        install.finish(event,SC_NATIVE_BINDING_FAILED,SC_INSTALL_UNKNOWN,ERROR_DUP_NAME);
        sentinel::startup_log::record(core);
        event=install.begin(SC_INSTALL_UPSTREAM); install.finish(event);
        auto& session = sentinel::save::session();
        session.begin_profile(0x1234);
        session.profile_step(sentinel::save::ProfileStage::decode, sentinel::save::ProfileStatus::refused,
            "native_authentication_refused", true, 0, 1, 4);
        session.fail_profile();
        session.profile_step(sentinel::save::ProfileStage::reader, sentinel::save::ProfileStatus::refused,
            "session_already_faulted");
        session.profile_step(sentinel::save::ProfileStage::output_validation, sentinel::save::ProfileStatus::refused,
            "profile_buffer_partial_read", false, 0, 0, 0, {SC_REASON_PARTIAL_READ, ERROR_PARTIAL_COPY, 560873, 262144, 262144});
        for (int i=0;i<300;++i) sentinel::startup_log::record(core);
        const auto log = root / "SentinelCore/diagnostics" / (std::to_string(core.pid)+"-"+std::to_string(created)+".jsonl");
        std::ifstream input(log); std::string line; unsigned lines=0;
        while (std::getline(input,line)) {
            ++lines;
            check(line.find(root.u8string())==std::string::npos,"no private path");
            if (lines>1) check(line.find("\"win32_error\":52")!=std::string::npos,"first failure retained");
            if (lines==3) {
                check(line.find("\"first_failed_stage\":\"decode\"")!=std::string::npos,"first PROFILE failure survives downstream refusal");
                check(line.find("native_authentication_refused")!=std::string::npos,"native result and predicate retained");
                check(line.find("\"native_value\":4")!=std::string::npos,"original native error retained");
                check(line.find("\"read_requested\":560873,\"read_offset\":262144,\"read_size\":262144")!=std::string::npos,
                    "bounded acquisition diagnostics retained without pointers");
                check(line.find("\"read_error\":299")!=std::string::npos,"original partial read error retained");
            }
        }
        check(lines==3,"bounded transition-only automatic log");
        // The process owns its open log handle until exit; retain fixture evidence.
        std::cout << "workflow handoff and automatic logging fixtures passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
