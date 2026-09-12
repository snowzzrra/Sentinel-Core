#pragma once
#include "save_provider.h"
#include "save_startup_gate.h"
#include <array>
#include <cstring>
#include <thread>
#include <stdexcept>
namespace startup_route_fixture {
using namespace sentinel; using namespace sentinel::save;
constexpr uintptr_t image=0x10000000, callback=image+0x1be4b3a;
inline unsigned native_queries=0, releases=0;
struct Result : SaveFuture {};
inline SaveFuture* destroy(SaveFuture* p,uint32_t) { delete static_cast<Result*>(p); return p; }
inline SaveResult* poll(SaveFuture*,SaveResult* out,void*) { *out={0,0,0,0}; return out; }
inline void release(SaveReference* ref) {
    auto* counts=reinterpret_cast<uint32_t*>(ref->control);
    CHECK(counts && counts[0]==2 && counts[1]==2); --counts[0]; --counts[1]; ref->control=0; ++releases;
}
inline SaveFuture** query(SaveFuture** out,SaveReference* identity,const char* name) {
    CHECK(std::strcmp(name,"PROFILE")==0); ++native_queries; release(identity);
    static const SaveFutureVtable table{destroy,poll}; auto* result=new Result; result->vtable=&table; *out=result; return out;
}
inline void exercise_gate() {
    // Use the same event/scope guard as the real RootInit detour and installer.
    // A waiting native invocation must observe complete route publication on
    // success; failure/cancellation must release it without startup admission.
    for (unsigned mode=0;mode<4;++mode) {
        StartupGate gate; BTrace trace; CHECK(gate.arm());
        StartupGate::Arm arm{}; arm.at_ms=GetTickCount64();
        arm.manager_present=mode==1; arm.root_equals_binding=true; arm.init_equals_target=true;
        gate.observe_arm(arm);
        std::atomic<uint32_t> routes{0},forwarded{0},admitted{0};
        StartupGate::Result observed{StartupGate::State::unarmed};
        HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(done);
        std::thread native([&] {
            gate.entered(true,true,0x4323fc);
            observed=gate.wait();
            gate.record(trace,observed.state==StartupGate::State::ready?BStatus::succeeded:BStatus::refused,
                "fixture_root_released",observed.state,observed.error);
            if (observed.state==StartupGate::State::ready) {
                CHECK(routes.load(std::memory_order_acquire)==steam_20260818_routes); ++admitted;
            }
            ++forwarded; SetEvent(done);
        });
        const auto deadline=GetTickCount64()+2000;
        while (!gate.waiting() && GetTickCount64()<deadline) Sleep(1);
        CHECK(gate.waiting()==1 && forwarded==0 && admitted==0);
        try {
            StartupGate::Completion completion(gate,&trace);
            routes.store(1,std::memory_order_release); // Partial installation.
            CHECK(forwarded==0 && WaitForSingleObject(done,0)==WAIT_TIMEOUT);
            if (mode==0) {
                routes.store(steam_20260818_routes,std::memory_order_release);
                completion.ready();
                CHECK(forwarded==0); // ready() commits only on scope completion.
            } else if (mode==2) completion.cancel();
            else if (mode==3) throw std::runtime_error("fixture_install_exception");
        } catch (const std::runtime_error&) { CHECK(mode==3); }
        CHECK(WaitForSingleObject(done,2000)==WAIT_OBJECT_0); native.join(); CloseHandle(done);
        CHECK(forwarded==1 && admitted==(mode==0?1u:0u));
        CHECK(observed.state==(mode==0?StartupGate::State::ready:mode==2?StartupGate::State::cancelled:StartupGate::State::failed));
        const auto snapshot=trace.snapshot();
        const auto event=snapshot.stages[static_cast<size_t>(BStage::startup_gate)];
        CHECK(diagnostic_fact(event,"startup_enter_ms")>=static_cast<int64_t>(gate.begin_ms()));
        CHECK(diagnostic_fact(event,"first_wait_ms")>=diagnostic_fact(event,"startup_enter_ms"));
        CHECK(diagnostic_fact(event,"install_complete_ms")>=diagnostic_fact(event,"first_wait_ms"));
        CHECK(diagnostic_fact(event,"startup_thread")!=0 && diagnostic_fact(event,"caller_rva")==0x4323fc);
        CHECK(diagnostic_fact(event,"manager_present")==static_cast<int64_t>(mode==1));
        CHECK(diagnostic_fact(event,"root_equals_binding")==1 && diagnostic_fact(event,"init_slot_equals_target0")==1);
        if (mode==0) CHECK(!snapshot.first_failure.sequence);
        else {
            CHECK(snapshot.first_failure.stage==BStage::startup_gate);
            CHECK(std::strcmp(snapshot.first_failure.predicate,mode==2?
                "startup_gate_installation_cancelled":"startup_gate_installation_failed")==0);
            trace.record(BStage::session,BStatus::refused,"fixture_later_refusal");
            CHECK(trace.snapshot().first_failure.sequence==snapshot.first_failure.sequence);
        }
    }
    StartupGate late; BTrace trace; CHECK(late.arm());
    { StartupGate::Completion complete(late,&trace); complete.ready(); }
    late.entered(true,true,0x4323fc);
    const auto immediate=late.wait(); CHECK(immediate.state==StartupGate::State::ready);
    late.record(trace,BStatus::succeeded,"fixture_root_after_ready",immediate.state);
    const auto event=trace.snapshot().stages[static_cast<size_t>(BStage::startup_gate)];
    CHECK(!diagnostic_fact(event,"first_wait_ms"));
    CHECK(diagnostic_fact(event,"startup_enter_ms")>=diagnostic_fact(event,"install_complete_ms"));
    CHECK(!trace.snapshot().first_failure.sequence);
}
// Actual adapter and consumed-reference contract. Only the native transport is substituted.
inline void exercise(Session& owner,unsigned defect=0) {
    if (!defect) exercise_gate();
    CHECK(owner.state()==SessionState::prepared);
    owner.install(0x1000,0x2000,steam_20260818_routes);
    owner.installation.finish(owner.installation.begin(SC_INSTALL_READY));
    if (defect==3) owner.install(0x1000,0x2000,0);
    if (defect==4) owner.install(0,0x2000,steam_20260818_routes);
    if (defect==5 || defect==7) CHECK(owner.startup_enter(0x1000,0x2000,GetCurrentThreadId()+(defect==5?1:0)));
    std::array<uintptr_t,3> control{0x200000002,defect==2?uintptr_t{0}:uintptr_t{0x7788},0}; SaveReference identity{reinterpret_cast<uintptr_t>(control.data())};
    engine::LocalMemory memory; SaveFuture* future=nullptr;
    const auto before=native_queries, before_release=releases;
    { NativeRouteScope route(defect==1?callback+1:callback,image);
      CHECK(query_exists_scoped(owner,memory,&future,&identity,defect==6?"GAME-AUTOSAVE7":"PROFILE",{{image,nullptr},query,release})==&future); }
    CHECK(!identity.control && releases==before_release+1 && future);
    SaveResult result{}; future->vtable->poll(future,&result,nullptr); future->vtable->destroy(future,1);
    const auto trace=owner.unrouted_trace(); CHECK(trace.at_ms && std::strcmp(trace.route,"presence_query")==0);
    CHECK(trace.caller_rva==0x1be4b3a+(defect==1?1:0));
    if (!defect) std::printf("STARTUP adapter=%s state=%u fault=%u root_observed=%u qualified=%u hooks_last=%u account_query=%u\n",
        trace.route,static_cast<unsigned>(owner.state()),static_cast<unsigned>(owner.fault()),trace.startup_entered?1u:0u,
        trace.root_qualified?1u:0u,owner.installation.inspect().last_completed_stage,trace.delegated_account_query?1u:0u);
    CHECK(!owner.routed() && !owner.native_io() && !owner.accepts_requests() && owner.profile_trace().request==0);
    CHECK(native_queries==before+(defect==0 || defect==7?1:0));
    if (!defect || defect==7) {
        CHECK(owner.state()==(defect==7?SessionState::starting:SessionState::prepared) && owner.fault()==SessionFault::none && trace.delegated_account_query);
        CHECK(trace.startup_entered==(defect==7) && trace.root_qualified==(defect==7) && result.state==0 && result.outcome==0 && result.value==0);
        CHECK(owner.installation.inspect().last_completed_stage==SC_INSTALL_READY);
        CHECK(!owner.btrace.snapshot().first_failure.sequence);
    } else {
        CHECK(owner.state()==SessionState::rejected && owner.fault()==SessionFault::missed_startup && !trace.delegated_account_query);
        CHECK(owner.installation.inspect().primary_failure.stage==SC_INSTALL_STARTUP);
        CHECK(result.state==0 && result.outcome!=0);
        const auto first=owner.btrace.snapshot().first_failure;
        CHECK(first.stage==BStage::session && first.status==BStatus::refused);
        CHECK(std::strcmp(first.predicate,defect==5?"unrouted_import_before_provider_binding":"unrouted_import_before_root_observation")==0);
        CHECK(diagnostic_fact(first,"previous_state")==static_cast<int64_t>(defect==5?SessionState::starting:SessionState::prepared));
        CHECK(diagnostic_fact(first,"requested_fault")==static_cast<int64_t>(SessionFault::missed_startup));
        CHECK(diagnostic_fact(first,"caller_rva")==static_cast<int64_t>(trace.caller_rva));
        CHECK(diagnostic_fact(first,"routes_equal")==static_cast<int64_t>(defect!=3));
        CHECK(diagnostic_fact(first,"root_available")==static_cast<int64_t>(defect!=4));
        CHECK(diagnostic_fact(first,"thread_equal")==0);
        owner.unrouted_import("later_operation"); CHECK(owner.unrouted_trace().at_ms==trace.at_ms);
        owner.stop_requests(); CHECK(owner.btrace.snapshot().first_failure.sequence==first.sequence);
    }
}
}
