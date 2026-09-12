#pragma once
#include "save_provider.h"
#include <array>
#include <cstring>
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
// Actual adapter and consumed-reference contract. Only the native transport is substituted.
inline void exercise(Session& owner,unsigned defect=0) {
    CHECK(owner.state()==SessionState::prepared);
    owner.install(0x1000,0x2000,steam_20260818_routes);
    owner.installation.finish(owner.installation.begin(SC_INSTALL_READY));
    if (defect==3) owner.install(0x1000,0x2000,0);
    if (defect==4) owner.install(0,0x2000,steam_20260818_routes);
    if (defect==5 || defect==7) CHECK(owner.startup_enter(0x1000,0x2000,GetCurrentThreadId()+(defect==5?1:0)));
    std::array<uintptr_t,3> control{0x200000002,defect==2?uintptr_t{0}:uintptr_t{0x7788},0}; SaveReference identity{reinterpret_cast<uintptr_t>(control.data())};
    engine::LocalMemory memory; SaveFuture* future=nullptr;
    const auto before=native_queries, before_release=releases;
    { NativeRouteScope route(defect==1?callback+1:callback);
      CHECK(query_exists_scoped(owner,memory,&future,&identity,defect==6?"GAME-AUTOSAVE7":"PROFILE",{{image,nullptr},query,release})==&future); }
    CHECK(!identity.control && releases==before_release+1 && future);
    SaveResult result{}; future->vtable->poll(future,&result,nullptr); future->vtable->destroy(future,1);
    const auto trace=owner.unrouted_trace(); CHECK(trace.at_ms && std::strcmp(trace.route,"presence_query")==0);
    if (!defect) std::printf("STARTUP adapter=%s state=%u fault=%u root_observed=%u qualified=%u hooks_last=%u account_query=%u\n",
        trace.route,static_cast<unsigned>(owner.state()),static_cast<unsigned>(owner.fault()),trace.startup_entered?1u:0u,
        trace.root_qualified?1u:0u,owner.installation.inspect().last_completed_stage,trace.delegated_account_query?1u:0u);
    CHECK(!owner.routed() && !owner.native_io() && !owner.accepts_requests() && owner.profile_trace().request==0);
    CHECK(native_queries==before+(defect==0 || defect==7?1:0));
    if (!defect || defect==7) {
        CHECK(owner.state()==(defect==7?SessionState::starting:SessionState::prepared) && owner.fault()==SessionFault::none && trace.delegated_account_query);
        CHECK(trace.startup_entered==(defect==7) && trace.root_qualified==(defect==7) && result.state==0 && result.outcome==0 && result.value==0);
        CHECK(owner.installation.inspect().last_completed_stage==SC_INSTALL_READY);
    } else {
        CHECK(owner.state()==SessionState::rejected && owner.fault()==SessionFault::missed_startup && !trace.delegated_account_query);
        CHECK(owner.installation.inspect().primary_failure.stage==SC_INSTALL_STARTUP);
        CHECK(result.state==0 && result.outcome!=0);
        owner.unrouted_import("later_operation"); CHECK(owner.unrouted_trace().at_ms==trace.at_ms);
    }
}
}
