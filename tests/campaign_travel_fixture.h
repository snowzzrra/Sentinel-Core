// Same Session, lifecycle hooks, queued native writer/SDK/readback and WUP queue.
// Native memory/callees alone are synthetic; no game or real save is touched.
#include "weapon_points.h"
namespace travel_fixture {
uintptr_t root=0;
uint32_t balance=0, gained=0;
sc_context_snapshot facts() {
    sc_context_snapshot value{}; value.sampled_at_ms=GetTickCount64();
    value.fields[SC_CONTEXT_GAME_STATE]={SC_OBSERVATION_OBSERVED,SC_REASON_NONE,*reinterpret_cast<uint32_t*>(root+0x44),0};
    const auto map=*reinterpret_cast<uintptr_t*>(root+0x50);
    const auto name=*reinterpret_cast<NativeString*>(map+0x9a060);
    value.current_map.validity=SC_OBSERVATION_OBSERVED;
    strcpy_s(value.current_map.bytes,name.data); value.current_map.length=name.length;
    return value;
}
int run(uint32_t difficulty,const std::wstring& defect,const std::function<void()>& save) {
    auto& campaign=session().campaign_run;
    std::array<unsigned char,0xc0> root_bytes{};
    std::array<unsigned char,0x1970> descriptor{};
    std::vector<unsigned char> map_bytes(0xafd00);
    std::array<unsigned char,16> setting_bytes{};
    root=reinterpret_cast<uintptr_t>(root_bytes.data());
    const auto map=reinterpret_cast<uintptr_t>(map_bytes.data());
    const auto setting=reinterpret_cast<uintptr_t>(setting_bytes.data());
    store(root_bytes,0x50,map); store(setting_bytes,8,difficulty);
    engine::Binding binding{}; binding.root=root; binding.image.base=reinterpret_cast<uintptr_t>(&setting)-0x45f8590;
    test_campaign_binding(binding.image.base,root); native::test_events(binding,native_load);
    native::TestAdapter adapter{};
    adapter.owner=[]()->uint32_t { return GetCurrentThreadId(); };
    adapter.map=[] { return *reinterpret_cast<uintptr_t*>(root+0x50); };
    adapter.context=facts; adapter.observe=[](context::Evidence&) { return facts(); };
    adapter.pending=[]()->uint8_t { return 0; }; adapter.gate=[](bool) {};
    sc_native_scope scope{}; scope.pid=GetCurrentProcessId(); scope.process_created=1; scope.instance_id[0]=1;
    native::test_dispatch_adapter(adapter,scope);
    const char no_namespace_override[65]{};
    weapon_points::use_fixture({nullptr,[](void*)->uintptr_t { return 1; },
        [](void*,uintptr_t,uint32_t& current,uint32_t& total) { current=balance; total=gained; return true; },
        [](void*,uintptr_t,uint32_t amount)->uint32_t { balance+=amount; gained+=amount; return 0; },
        [](void*,uintptr_t) { return true; }},no_namespace_override);
    uint64_t sequence=0;
    auto request=[&](sc_native_scope expected) {
        sc_weapon_points_request r{};
        r.execution.expected=expected; r.execution.request_id=++sequence; r.execution.nonce[0]=1; r.execution.deadline_ms=2000;
        strcpy_s(r.namespace_id,session().namespace_id().c_str()); return r;
    };
    auto points=[&] {
        native::publish_context({},native::observation_stamp());
        auto r=request(native::inspect().scope);
        r.kind=SC_WUP_GRANT; r.amount=3; r.expected_gained=gained;
        auto result=native::submit_weapon_points(r);
        if(result.execution.state!=SC_DIAGNOSTIC_QUEUED)
            std::printf("WUP refused reason=%u campaign=%s\n",result.execution.reason,campaign.snapshot().reason.c_str());
        CHECK(result.execution.state==SC_DIAGNOSTIC_QUEUED);
        native::test_post_frame(); result=native::weapon_points_result(r,false,false);
        CHECK(result.execution.state==SC_DIAGNOSTIC_EXECUTED && result.outcome==SC_WUP_GRANTED);
        CHECK(result.balance_after-result.balance_before==3);
        native::weapon_points_result(r,false,true);
    };
    const auto initial=campaign.snapshot();
    std::vector<std::string> maps{initial.resumed?initial.map:"game/sp/e1m1_intro/e1m1_intro"};
    if(initial.resumed) maps.push_back("game/sp/e1m3_cult/e1m3_cult");
    else { maps.push_back("game/hub/hub"); maps.push_back("game/sp/e1m2_battle/e1m2_battle"); }
    transition_defect=L"travel";
    for(size_t i=0;i<maps.size();++i) {
        // Outgoing checkpoint completes BEFORE ExecuteMapChange, as in retail.
        if(i) save();
        const auto previous=native::inspect().scope;
        store(descriptor,0x10,text(maps[i]));
        if(i==1 && defect==L"cross_map_gap") native::test_generation_gap();
        CHECK(observed_change(root,reinterpret_cast<uintptr_t>(descriptor.data()))==1);
        const auto observed=campaign.snapshot();
        if(i==1 && defect==L"cross_map_gap") {
            CHECK(observed.reason=="lifecycle_continuation_generation_mismatch" && !session().accepts_requests());
            std::puts("PASS unobserved generation gap refused"); return 0;
        }
        CHECK(session().accepts_requests() && observed.map_active && observed.map==maps[i]);
        CHECK(observed.generation_after==previous.lifecycle_generation+1); // Nested primary free is not a second generation.
        points();
        if(i) {
            auto stale=request(previous);
            CHECK(native::submit_weapon_points(stale).execution.reason==SC_NATIVE_SCOPE_MISMATCH);
        }
        save(); // First destination checkpoint: real provider + SDK + readback.
        const auto saved=campaign.snapshot();
        CHECK(saved.continuity_persisted && saved.native_saved && saved.readback_verified);
        std::printf("PASS map=%s generation=%llu checkpoint=%llu WUP=%d session=admitted\n",
            maps[i].c_str(),saved.generation_after,saved.checkpoint,balance);
    }
    CHECK(!session().btrace.snapshot().first_failure.sequence);
    return 0;
}
}
