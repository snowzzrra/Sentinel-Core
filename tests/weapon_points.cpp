#include "weapon_points.h"
#include "native_model.h"
#include "protocol.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace sentinel;
int main() {
    for (auto source : {weapon_points::Source::encounter,weapon_points::Source::pickup,weapon_points::Source::target,
            weapon_points::Source::script,weapon_points::Source::unlockable,weapon_points::Source::devinv,
            weapon_points::Source::slayer_gate,weapon_points::Source::useable}) {
        assert(weapon_points::suppress(true,source,0,3,false));
        assert(!weapon_points::suppress(false,source,0,3,false));
        assert(!weapon_points::suppress(true,source,0,3,true));
        assert(!weapon_points::suppress(true,source,0,-3,false));
        for (uint32_t type = 1; type < 9; ++type) assert(!weapon_points::suppress(true,source,type,3,false));
    }
    assert(!weapon_points::suppress(true,weapon_points::Source::unknown,0,3,false));
    assert(weapon_points::direct_source(0x14333d7) == weapon_points::Source::unknown);
    assert(weapon_points::wrapper_source(0xd0feee) == weapon_points::Source::useable);
    sc_weapon_points_request r{}; r.execution.expected.pid=12; r.execution.expected.process_created=13;
    r.execution.expected.instance_id[0]=14; r.execution.expected.lifecycle_generation=15;
    r.execution.request_id=16; r.execution.nonce[0]=17; r.execution.deadline_ms=2000;
    std::memset(r.namespace_id,'a',64); r.kind=SC_WUP_GRANT; r.amount=3;
    Message wire{}; const auto size=encode_weapon_points_request(wire,weapon_points_submit_operation,r);
    assert(size == 165); sc_weapon_points_request decoded{}; uint16_t op=0;
    assert(decode_request(wire,size,&op,nullptr,nullptr,nullptr,nullptr,&decoded) == WireResult::ok);
    assert(weapon_points::same(r,decoded));
    native::Diagnostics queue;
    assert(queue.submit(r.execution,0,100,nullptr,nullptr,&r).state == SC_DIAGNOSTIC_QUEUED);
    auto mismatch=r; mismatch.amount=6;
    assert(queue.submit(mismatch.execution,0,101,nullptr,nullptr,&mismatch).state == SC_DIAGNOSTIC_REJECTED);
    assert(queue.points_result(r,true,102).execution.state == SC_DIAGNOSTIC_CANCELLED);
    r.execution.request_id++; r.amount=0;
    assert(queue.submit(r.execution,0,103,nullptr,nullptr,&r).state == SC_DIAGNOSTIC_REJECTED);
    struct State { uint32_t balance=3,gained=6,error=0; bool refresh=true,read=true; } state;
    weapon_points::Calls calls{&state,[](void*) -> uintptr_t{return 1;},
        [](void* p,uintptr_t,uint32_t& b,uint32_t& g) { const auto& s=*static_cast<State*>(p); b=s.balance;g=s.gained;return s.read; },
        [](void* p,uintptr_t,uint32_t n)->uint32_t {auto& s=*static_cast<State*>(p);if(!s.error){s.balance+=n;s.gained+=n;}return s.error;},
        [](void* p,uintptr_t){return static_cast<State*>(p)->refresh;}};
    r.amount=3; r.expected_gained=0;
    auto out=weapon_points::initial(r); weapon_points::execute(r,out,calls);
    assert(out.outcome == SC_WUP_PRECONDITION && state.balance == 3);
    r.expected_gained=6; out=weapon_points::initial(r); weapon_points::execute(r,out,calls);
    assert(out.outcome == SC_WUP_GRANTED && state.balance == 6 && state.gained == 9);
    out=weapon_points::initial(r); weapon_points::execute(r,out,calls);
    assert(out.outcome == SC_WUP_PRECONDITION && state.balance == 6);
    r.expected_gained=9; state.error=5; out=weapon_points::initial(r); weapon_points::execute(r,out,calls);
    assert(out.outcome == SC_WUP_NATIVE_FAILED && out.native_exception == 5 && state.balance == 6);
    state.error=0; state.refresh=false; out=weapon_points::initial(r); weapon_points::execute(r,out,calls);
    assert(out.outcome == SC_WUP_REFRESH_FAILED && out.gained_after == 12);
    std::puts("PASS typed wire/queue, invalid grant, provenance, AP bypass, spend/other currencies, native success/failure");
}
