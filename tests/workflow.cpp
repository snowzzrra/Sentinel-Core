#include "prelaunch.h"
#include "startup_log.h"
#include "save_session.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <iterator>
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
        sentinel::storage::Descriptor route_descriptor{{"synthetic-route-diagnostics",0,1,std::string(64,'d')},root.wstring()};
        std::unique_ptr<sentinel::storage::Namespace> route_lease;
        check(sentinel::storage::prepare(route_descriptor,route_lease).ok(),"fixture-only route namespace");
        check(session.configure(route_descriptor,std::move(route_lease)).ok(),"fixture-only route owner");
        session.begin_profile(0x1234);
        session.profile_step(sentinel::save::ProfileStage::decode, sentinel::save::ProfileStatus::refused,
            "native_authentication_refused", true, 0, 1, 4);
        // Exercise the actual C++ route producer after its first failure is set.
        // The unusual source spelling must remain hex-encoded and private.
        sentinel::save::UnroutedSource source;
        constexpr char source_name[]="PROFILE\"\\fixture\n";
        std::memcpy(source.name.data(),source_name,sizeof(source_name)-1);
        source.data=0xfeed0000; source.length=static_cast<int32_t>(sizeof(source_name)-1);
        source.kind=3; source.step=4; source.reason=SC_REASON_PARTIAL_READ; source.error=ERROR_PARTIAL_COPY;
        const auto image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        {
            sentinel::save::NativeRouteScope route(image+0x148e225,image);
            session.unrouted_import("provider_read","import",0x12345000,0x12346000,source);
        }
        const auto unrouted=session.unrouted_trace();
        check(unrouted.caller_rva==0x148e225 && unrouted.source.name==source.name,"exact synthetic source route captured");
        check(unrouted.stack_count>0 && unrouted.stack_count<=8,"supported-image fixture stack retained");
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
        const auto latest = root / "SentinelCore/diagnostics" / (std::to_string(core.pid)+"-"+std::to_string(created)+".latest.json");
        const auto read_latest = [&] {
            std::ifstream snapshot(latest,std::ios::binary);
            check(snapshot.good(),"independent latest snapshot exists");
            return std::string(std::istreambuf_iterator<char>(snapshot),std::istreambuf_iterator<char>());
        };
        using sentinel::save::BStage;
        using sentinel::save::BStatus;
        const auto first = session.btrace.snapshot().first_failure;
        check(first.sequence && first.status==BStatus::refused,"native PROFILE refusal has precise first B cause");
        // Exercise the real producer, including every bounded stage and full
        // numeric fact capacity, before repeatedly changing post-startup facts.
        for(size_t stage=0;stage<static_cast<size_t>(BStage::count);++stage) {
            session.btrace.record(static_cast<BStage>(stage),BStatus::succeeded,"fixture_stage_completed",stage,
                {{"fact_01",INT64_MIN},{"fact_02",INT64_MAX},{"fact_03",INT64_MIN},{"fact_04",INT64_MAX},
                 {"fact_05",INT64_MIN},{"fact_06",INT64_MAX},{"fact_07",INT64_MIN},{"fact_08",INT64_MAX},
                 {"fact_09",INT64_MIN},{"fact_10",INT64_MAX},{"fact_11",INT64_MIN},{"fact_12",INT64_MAX},
                 {"fact_13",INT64_MIN},{"fact_14",INT64_MAX},{"fact_15",INT64_MIN},{"fact_16",INT64_MAX}});
        }
        uintmax_t capped_size=0;
        constexpr const char* pending_predicate="fixture_checkpoint_pending";
        constexpr const char* checkpoint_key="checkpoint_index";
        for(int i=0;i<300;++i) {
            session.btrace.record(BStage::sdk_callback,BStatus::pending,pending_predicate,i,{{checkpoint_key,i}});
            sentinel::startup_log::record(core);
            if(i==150) capped_size=fs::file_size(log);
        }
        check(fs::file_size(log)==capped_size && capped_size<=1024*1024,"history byte budget stops growth while later changes continue");
        auto content=read_latest();
        check(!content.empty() && content.size()<=65536 && content.front()=='{' && content.substr(content.size()-2)=="}\n",
            "complete bounded latest JSON envelope");
        check(content.find("\"checkpoint_index\":299")!=std::string::npos,"latest observes changes beyond history cap");
        check(content.find("\"build_id\":\""+std::string(core.core.build_id)+"\"")!=std::string::npos &&
            content.find("\"pid\":"+std::to_string(core.pid)+",")!=std::string::npos &&
            content.find("\"process_created\":\""+std::to_string(created)+"\"")!=std::string::npos,"latest exact producer identity");
        std::ifstream bounded_history(log); lines=0;
        while(std::getline(bounded_history,line)) {
            ++lines;
            check(line.find("\"checkpoint_index\":299")==std::string::npos,"late event is independently retained in latest");
        }
        check(lines<=128,"history record budget enforced");
        const auto sequence=session.btrace.snapshot().sequence;
        for(int i=0;i<300;++i) {
            session.btrace.record(BStage::sdk_callback,BStatus::pending,pending_predicate,299,{{checkpoint_key,299}});
            sentinel::startup_log::record(core);
        }
        check(session.btrace.snapshot().sequence==sequence && read_latest()==content,"identical polling coalesces after history is full");
        session.btrace.record(BStage::sdk_result,BStatus::refused,"fixture_late_native_save_failed",400,{{"native_result",-9}},0x12345678);
        sentinel::startup_log::record(core);
        content=read_latest();
        const auto first_begin=content.find("\"first_failure\":{\"sequence\":"+std::to_string(first.sequence)+",");
        const auto first_end=content.find("\"stages\":",first_begin);
        check(first_begin!=std::string::npos && first_end!=std::string::npos &&
            content.substr(first_begin,first_end-first_begin).find("native_authentication_refused")!=std::string::npos,
            "original precise cause survives later errors and all stage rollover");
        check(content.find("fixture_late_native_save_failed")!=std::string::npos && content.find("\"native_result\":-9")!=std::string::npos,
            "late failed stage retains exact native result after history limit");
        check(content.size()<=65536 && fs::file_size(log)==capped_size,"independent snapshot and history remain bounded after failure");
        check(!fs::exists(latest.wstring()+L".pending"),"latest publication completed atomic replacement");
        // The process owns its open log handle until exit; retain fixture evidence.
        std::cout << "workflow handoff and automatic logging fixtures passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
