// Bound native callees; wire decoding, namespace/scope admission, atomic commit,
// privacy, refresh, and native LoadMission ownership use production adapters.
namespace menu_fixture {
uintptr_t screen=0,list=0,details=0,campaign=0;
uintptr_t source_list=0;
uintptr_t combat_meter=0;
unsigned unrelated_meter_updates=0;
bool details_fault=false;
uintptr_t combat_sprite=0;
bool missing_meter_sprite=false;
unsigned details_render_calls=0;
uintptr_t root_definitions=0;
unsigned root_navigations=0,root_campaigns=0;
unsigned root_moves=0;
unsigned populations=0,focuses=0,loads=0,hidden=0,statistics=0;
std::array<std::array<unsigned char,0x2a0>,3> widgets{};
std::array<uintptr_t,3> widget_addresses{};
std::array<unsigned char,3*0x550> derived{};
std::map<uintptr_t,std::string> strings;
template<class T> T& at(uintptr_t p,size_t offset) { return *reinterpret_cast<T*>(p+offset); }
void verify_details_exception() {
    __try { campaign_menu::test_details_update(details); CHECK(false); }
    __except(GetExceptionCode()==0xe0420066 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        CHECK(at<uintptr_t>(combat_meter,0x168)==0x123456);
    }
}
void assign(uintptr_t address,const char* text) {
    auto& value=strings[address]; value=text;
    auto& name=*reinterpret_cast<NativeString*>(address);
    name.data=value.data(); name.length=static_cast<int32_t>(value.size());
}
void exercise() {
    using namespace campaign_menu;
    std::array<unsigned char,0x878> screen_bytes{};
    std::array<unsigned char,0x160> list_bytes{};
    std::array<unsigned char,0x700> details_bytes{};
    std::array<unsigned char,0x700> other_details_bytes{};
    std::array<unsigned char,0x180> combat_bytes{},other_combat_bytes{};
    std::array<unsigned char,0x60> combat_sprite_bytes{},other_sprite_bytes{};
    std::array<unsigned char,0x1b0> campaign_bytes{};
    std::array<unsigned char,0xa0> source_bytes{};
    std::array<unsigned char,3*0x50> source_entries{};
    NativeString source_layer{};
    std::array<uintptr_t,64> screen_table{},list_table{};
    screen=reinterpret_cast<uintptr_t>(screen_bytes.data()); list=reinterpret_cast<uintptr_t>(list_bytes.data());
    details=reinterpret_cast<uintptr_t>(details_bytes.data()); campaign=reinterpret_cast<uintptr_t>(campaign_bytes.data());
    combat_meter=reinterpret_cast<uintptr_t>(combat_bytes.data());
    combat_sprite=reinterpret_cast<uintptr_t>(combat_sprite_bytes.data());
    const auto other_details=reinterpret_cast<uintptr_t>(other_details_bytes.data());
    const auto other_combat=reinterpret_cast<uintptr_t>(other_combat_bytes.data());
    at<uintptr_t>(combat_meter,0x168)=0x123456;
    at<uintptr_t>(other_combat,0x168)=0x654321;
    at<uintptr_t>(other_combat,0x18)=reinterpret_cast<uintptr_t>(other_sprite_bytes.data());
    at<uintptr_t>(details,0x6f8)=combat_meter;
    at<uintptr_t>(other_details,0x6f8)=other_combat;
    screen_table[0x1e8/8]=reinterpret_cast<uintptr_t>(+[](uintptr_t)->uintptr_t { return list; });
    list_table[0x68/8]=reinterpret_cast<uintptr_t>(+[](uintptr_t owner,int index,int) {
        at<int>(owner,0x150)=index; test_focus(screen);
    });
    at<uintptr_t>(screen,0)=reinterpret_cast<uintptr_t>(screen_table.data());
    at<uintptr_t>(list,0)=reinterpret_cast<uintptr_t>(list_table.data());
    at<uintptr_t>(screen,0x100)=campaign; at<uintptr_t>(screen,0x118)=details;
    source_list=reinterpret_cast<uintptr_t>(source_bytes.data());
    at<uintptr_t>(source_list,0x88)=reinterpret_cast<uintptr_t>(source_entries.data());
    at<int>(source_list,0x90)=3;
    assign(reinterpret_cast<uintptr_t>(source_entries.data()),"game/hub/hub");
    assign(reinterpret_cast<uintptr_t>(source_entries.data()+0x50),"game/dlc2/e5m1_spear/e5m1_spear");
    assign(reinterpret_cast<uintptr_t>(&source_layer),"game/sp/dlc2/e5m1_spear/gameplay/e5m1_spear_mission_select");
    at<uintptr_t>(reinterpret_cast<uintptr_t>(source_entries.data()+0x50),0x30)=reinterpret_cast<uintptr_t>(&source_layer);
    at<int>(reinterpret_cast<uintptr_t>(source_entries.data()+0x50),0x38)=1;
    at<uintptr_t>(campaign,0x1a8)=source_list;
    NativeCalls calls{};
    calls.string_init=[](uintptr_t address) { *reinterpret_cast<NativeString*>(address)={}; assign(address,""); };
    calls.string_assign=assign;
    calls.populate=[](uintptr_t owner,uintptr_t target) {
        ++populations;
        const auto source=at<uintptr_t>(campaign,0x1a8);
        CHECK(source!=source_list && at<int>(source,0x90)==3);
        at<uintptr_t>(owner,0x110)=source;
        at<uintptr_t>(owner,0x120)=reinterpret_cast<uintptr_t>(derived.data()); at<int>(owner,0x128)=3;
        for (size_t i=0;i<widgets.size();++i) widget_addresses[i]=reinterpret_cast<uintptr_t>(widgets[i].data());
        at<uintptr_t>(target,0xa0)=reinterpret_cast<uintptr_t>(widget_addresses.data()); at<int>(target,0xa8)=3;
    };
    calls.focus=[](uintptr_t) {
        ++focuses; assign(details+0x180,"native mission details"); test_details_update(details);
    };
    calls.details_update=[](uintptr_t address) {
        ++details_render_calls;
        if (details_fault) { details_fault=false; RaiseException(0xe0420066,0,0,nullptr); }
        if (!at<NativeString>(address,0x180).length) {
            CHECK(!at<uintptr_t>(address,0x1b0)); ++hidden; return;
        }
        // Real native details rebuilds the meter, then forces its sprite visible.
        const auto meter=at<uintptr_t>(address,0x6f8);
        // Both native render paths call IsBound: unbound meter returns before
        // touching its SWF. The AP boundary must suspend it before either path.
        if (at<uintptr_t>(meter,0x168)) {
            CHECK(meter!=combat_meter); ++unrelated_meter_updates;
        }
        // Retail 220120: even with drawing skipped, native visibility still
        // dereferences sprite+51. Fresh widgets begin with sprite==nullptr.
        const auto sprite=at<uintptr_t>(meter,0x18);
        CHECK(sprite); at<uint8_t>(sprite,0x51)=1;
        at<int>(meter,0x154)=1;
    };
    calls.widget_bound=[](uintptr_t meter)->bool {
        CHECK(meter==combat_meter && at<uintptr_t>(meter,0x168)==0x123456);
        at<uintptr_t>(meter,0x18)=missing_meter_sprite ? 0 : combat_sprite;
        return !missing_meter_sprite;
    };
    calls.completed=[](uintptr_t,uintptr_t entry)->bool {
        CHECK(at<NativeString>(entry,0).length>0); ++statistics; return false;
    };
    calls.list_assign=[](uintptr_t,uintptr_t) {};
    calls.widget_state=[](uintptr_t address,int value) { at<int>(address,0x154)=value; };
    calls.load=[](uintptr_t owner,int index) {
        ++loads;
        at<uintptr_t>(owner,0x870)=at<uintptr_t>(at<uintptr_t>(owner,0x110),0x88)+static_cast<size_t>(index)*0x50;
    };
    calls.available=[](uintptr_t)->bool { return false; };
    calls.update=[](uintptr_t owner) {
        if (at<int>(owner,0x108)==-1) { at<int>(owner,0x108)=0; test_populate(owner,list); }
    };
    calls.root_navigation=[](uintptr_t owner,uint8_t) {
        CHECK(at<int>(at<uintptr_t>(owner,0x148),0x154)==0);
        CHECK(at<int>(at<uintptr_t>(owner,0x150),0x154)==0);
        ++root_navigations;
    };
    calls.root_campaign=[](uintptr_t owner,uintptr_t declaration) {
        CHECK(declaration==at<uintptr_t>(root_definitions,0x148));
        CHECK(at<uint32_t>(owner,0x1c8)==0); ++root_campaigns;
    };
    calls.campaign_definitions=[]()->uintptr_t { return root_definitions; };
    calls.sprite_changed=[](uintptr_t) { ++root_moves; };
    test_calls(calls);
    std::array<unsigned char,0x200> root_bytes{},definitions_bytes{};
    std::array<std::array<unsigned char,0x180>,9> root_widgets{};
    std::array<std::array<unsigned char,0x130>,9> sprites{};
    std::array<unsigned char,0x100> renderer{};
    std::array<unsigned char,9*0x40> transforms{};
    const auto root_screen=reinterpret_cast<uintptr_t>(root_bytes.data());
    root_definitions=reinterpret_cast<uintptr_t>(definitions_bytes.data());
    at<uintptr_t>(root_definitions,0x148)=campaign;
    constexpr size_t offsets[]{0x140,0x148,0x150,0x158,0x160,0x168,0x178,0x180,0x188};
    const auto render_address=reinterpret_cast<uintptr_t>(renderer.data());
    const auto transform_address=reinterpret_cast<uintptr_t>(transforms.data());
    at<uintptr_t>(render_address,0x80)=transform_address;
    for (size_t i=0;i<9;++i) {
        const auto widget=reinterpret_cast<uintptr_t>(root_widgets[i].data());
        const auto sprite=reinterpret_cast<uintptr_t>(sprites[i].data());
        at<uintptr_t>(root_screen,offsets[i])=widget;
        at<uintptr_t>(widget,0x18)=sprite;
        at<uintptr_t>(sprite,0x10)=render_address; at<int>(sprite,0xc)=static_cast<int>(i);
        at<float>(transform_address,i*0x40+0x14)=float(i%5)*30.f;
        at<float>(transform_address,i*0x40+0x18)=float(i)*57.f;
    }
    for (uint32_t source=0;source<3;++source) {
        at<uint32_t>(root_screen,0x1c8)=source;
        test_root_navigation(root_screen,1);
        test_root_campaign(root_screen,campaign+source*8);
    }
    CHECK(root_navigations==3 && root_campaigns==3);
    CHECK(root_moves==6); // Repeated native navigation must not shift again.
    for (size_t i=3;i<9;++i) {
        CHECK(at<float>(transform_address,i*0x40+0x14)==float((i-2)%5)*30.f);
        CHECK(at<float>(transform_address,i*0x40+0x18)==float(i-2)*57.f);
    }
    sc_native_scope scope{}; scope.pid=GetCurrentProcessId(); scope.process_created=123; scope.instance_id[0]=1;
    native::TestAdapter adapter{}; adapter.owner=[]()->uint32_t { return GetCurrentThreadId(); };
    native::test_dispatch_adapter(adapter,scope);
    sc_campaign_request request{};
    request.execution.expected=native::inspect().scope; request.execution.request_id=1;
    request.execution.nonce[0]=1; request.execution.deadline_ms=2000;
    std::memcpy(request.namespace_id,session().namespace_id().c_str(),65); request.revision=1; request.count=3;
    const auto exchange=[&](uint16_t op) {
        Message bytes{}; sc_campaign_request decoded{}; uint16_t operation=0;
        const auto size=encode_campaign_request(bytes,op,request);
        CHECK(size==469 && decode_request(bytes,size,&operation,nullptr,nullptr,nullptr,nullptr,nullptr,&decoded)==WireResult::ok);
        CHECK(operation==op);
        return native::campaign_request(operation,decoded);
    };
    request.namespace_id[0]=request.namespace_id[0]=='a'?'b':'a';
    CHECK(exchange(campaign_inspect_operation).reason==SC_CAMPAIGN_SCOPE);
    std::memcpy(request.namespace_id,session().namespace_id().c_str(),65);
    ++request.execution.expected.lifecycle_generation;
    CHECK(exchange(campaign_inspect_operation).reason==SC_CAMPAIGN_SCOPE);
    --request.execution.expected.lifecycle_generation;
    std::array<sc_campaign_row,3> rows{};
    rows[0].id=1; rows[0].flags=SC_CAMPAIGN_HUB|SC_CAMPAIGN_REVEALED|SC_CAMPAIGN_UNLOCKED;
    strcpy_s(rows[0].map,"game/hub/hub"); strcpy_s(rows[0].title,"FORTRESS OF DOOM");
    rows[1].id=2; rows[1].flags=SC_CAMPAIGN_REVEALED|SC_CAMPAIGN_UNLOCKED|SC_CAMPAIGN_DETAILS;
    rows[1].native_index=1;
    strcpy_s(rows[1].map,"game/dlc2/e5m1_spear/e5m1_spear"); strcpy_s(rows[1].title,"THE WORLD SPEAR");
    rows[2].id=3; strcpy_s(rows[2].title,"???");
    request.row=rows[0]; CHECK(exchange(campaign_row_operation).status==0);
    CHECK(exchange(campaign_commit_operation).reason==SC_CAMPAIGN_INCOMPLETE);
    CHECK(menu().projection().count==0);
    for (uint32_t i=1;i<3;++i) { request.index=i; request.row=rows[i]; CHECK(exchange(campaign_row_operation).status==0); }
    request.index=0; request.row={}; request.row.id=2;
    CHECK(exchange(campaign_commit_operation).committed_revision==1);
    CHECK(exchange(campaign_inspect_operation).rendered_revision==0); // ACK is not rendering.
    at<int>(screen,0x108)=-1; test_update(screen);
    CHECK(populations==1 && at<uintptr_t>(campaign,0x1a8)==source_list);
    CHECK(strings[widget_addresses[2]+0x188]=="??? / LOCKED" && at<int>(widget_addresses[2],0x154)==5);
    CHECK(at<int>(list,0x150)==1 && focuses==1 && statistics==1);
    CHECK(at<int>(combat_meter,0x154)==0 && unrelated_meter_updates==0);
    CHECK(strings[details+0x180]=="native mission details");
    CHECK(at<uintptr_t>(combat_meter,0x168)==0x123456);
    assign(other_details+0x180,"unrelated native details");
    test_details_update(other_details);
    CHECK(unrelated_meter_updates==1 && at<int>(other_combat,0x154)==1);
    CHECK(at<uintptr_t>(other_combat,0x168)==0x654321);
    details_fault=true; verify_details_exception();
    missing_meter_sprite=true;
    const auto rendered_before=details_render_calls;
    test_details_update(details);
    CHECK(details_render_calls==rendered_before && at<int>(details,0x154)==0);
    CHECK(at<uintptr_t>(combat_meter,0x168)==0x123456);
    missing_meter_sprite=false;
    test_details_update(details);
    CHECK(at<uintptr_t>(combat_meter,0x18)==combat_sprite);
    CHECK(at<uintptr_t>(combat_meter,0x168)==0x123456 && at<int>(combat_meter,0x154)==0);
    const auto before_hidden=hidden;
    at<int>(list,0x150)=2; test_focus(screen); test_load(screen,2);
    CHECK(hidden==before_hidden+1 && !loads && focuses==1 && statistics==1);
    test_load(screen,1); CHECK(loads==1);
    const auto retained=at<uintptr_t>(screen,0x870);
    std::array<unsigned char,0xc0> mission{};
    auto mission_address=reinterpret_cast<uintptr_t>(mission.data());
    at<int>(mission_address,0x98)=2;
    at<uintptr_t>(mission_address,0xa8)=retained;
    std::string target="game/dlc2/e5m1_spear/e5m1_spear",destination;
    at<NativeString>(mission_address,0x30)=text(target);
    CHECK(mission_request(mission_address,destination) && destination==target);
    at<uintptr_t>(mission_address,0xa8)=retained+0x50;
    CHECK(!mission_request(mission_address,destination));
    at<uintptr_t>(mission_address,0xa8)=retained;
    target="game/hub/hub"; at<NativeString>(mission_address,0x30)=text(target);
    CHECK(!mission_request(mission_address,destination));
    CHECK(at<int>(retained,0x38)==1);
    CHECK(at<uintptr_t>(retained,0x30)==reinterpret_cast<uintptr_t>(&source_layer));
    const std::string retained_map=at<NativeString>(retained,0).data;
    request.revision=2;
    std::swap(rows[1],rows[2]);
    for (uint32_t i=0;i<3;++i) { request.index=i; request.row=rows[i]; CHECK(exchange(campaign_row_operation).status==0); }
    request.index=0; request.row={}; request.row.id=2; CHECK(exchange(campaign_commit_operation).status==0);
    test_update(screen); CHECK(populations==1 && at<NativeString>(retained,0).data==retained_map);
    CHECK(exchange(campaign_inspect_operation).loaded_id==0); // Selection is not a completed load.
    at<uintptr_t>(screen,0x870)=0; test_update(screen);
    CHECK(populations==2 && at<int>(list,0x150)==2);
    CHECK(exchange(campaign_inspect_operation).rendered_revision==2);
    CHECK(at<int>(combat_meter,0x154)==0 && unrelated_meter_updates==1);
    request.revision=1; CHECK(exchange(campaign_commit_operation).reason==SC_CAMPAIGN_REVISION);
    std::puts("PASS typed native campaign projection/privacy/focus/pending-load entry ownership");
    std::puts("PASS AP mission details bypass crashing legacy combat meter; unrelated HUD/details forwarded");
}
void actions() {
    using namespace campaign_menu;
    std::array<unsigned char,0x140> screen_bytes{};
    std::array<unsigned char,0x160> list_bytes{};
    std::array<std::array<unsigned char,0x2a0>,2> buttons{};
    std::array<uintptr_t,2> children{reinterpret_cast<uintptr_t>(buttons[0].data()),reinterpret_cast<uintptr_t>(buttons[1].data())};
    std::array<uintptr_t,64> screen_table{},list_table{};
    auto owner=reinterpret_cast<uintptr_t>(screen_bytes.data());
    list=reinterpret_cast<uintptr_t>(list_bytes.data());
    at<uintptr_t>(owner,0)=reinterpret_cast<uintptr_t>(screen_table.data());
    at<int>(owner,0x108)=2;
    at<uintptr_t>(owner,0x118)=children[0]; at<uintptr_t>(owner,0x120)=children[1];
    at<uintptr_t>(list,0)=reinterpret_cast<uintptr_t>(list_table.data());
    at<uintptr_t>(list,0xa0)=reinterpret_cast<uintptr_t>(children.data()); at<int>(list,0xa8)=2;
    screen_table[0x1e8/8]=reinterpret_cast<uintptr_t>(+[](uintptr_t)->uintptr_t { return list; });
    list_table[0x70/8]=reinterpret_cast<uintptr_t>(+[](uintptr_t owner_list,uintptr_t child) {
        auto child_array=at<uintptr_t>(owner_list,0xa0); at<int>(owner_list,0x150)=child==at<uintptr_t>(child_array,0)?0:1;
    });
    NativeCalls calls{}; calls.string_assign=assign;
    calls.widget_state=[](uintptr_t widget,int state) { at<int>(widget,0x154)=state; };
    test_calls(calls);
    at<int>(children[1],0x154)=5;
    present_campaign_actions(owner,false); // Projection arrived after a disabled native button.
    CHECK(strings[children[0]+0x188]=="RETURN TO FORTRESS");
    CHECK(strings[children[1]+0x188]=="CHOOSE MISSION");
    CHECK(at<int>(children[1],0x154)==1 && at<int>(list,0x150)==1);
    at<int>(list,0x150)=0;
    present_campaign_actions(owner,false);
    CHECK(at<int>(list,0x150)==0); // Do not steal a subsequent manual focus change.
    present_campaign_actions(owner,true);
    CHECK(at<int>(list,0x150)==1);
    std::puts("PASS Hub action labels, late availability, entry focus and manual focus retention");
}
}
