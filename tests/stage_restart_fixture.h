namespace restart_fixture {
uint32_t stage_id=7771000,variant=2;
std::string mission="game/sp/e1m1_intro/e1m1_intro";
uintptr_t find_decl(uintptr_t,const char* path,int) {
    return !std::strcmp(path,"masterl_level/e3m1_slayer")?0x1234:0;
}
void parser(Session& owner,uintptr_t source,const std::string& directory,engine::Memory& memory) {
    CHECK(owner.campaign_run.begin_resume(mission,stage_id,variant));
    CHECK(owner.campaign_run.allow_access(source,directory,false,false));
    CHECK(owner.campaign_run.verify_source(memory,source,image));
    CHECK(owner.campaign_run.parser_enter(source));
    owner.campaign_run.parser_leave(0);
    CHECK(owner.campaign_run.allow_difficulty(3));
}
void stage_projection(Session& owner) {
    using namespace campaign_menu;
    sc_campaign_request projection{}; projection.count=variant==6?3:2; projection.revision=1;
    strcpy_s(projection.namespace_id,owner.namespace_id().c_str());
    projection.row.id=1; projection.row.flags=SC_CAMPAIGN_HUB|SC_CAMPAIGN_REVEALED|SC_CAMPAIGN_UNLOCKED;
    strcpy_s(projection.row.map,"game/hub/hub"); strcpy_s(projection.row.title,"Fortress");
    CHECK(!menu().request(campaign_row_operation,projection,true).status);
    projection.index=1; projection.row.id=stage_id;
    projection.row.flags=SC_CAMPAIGN_REVEALED|SC_CAMPAIGN_UNLOCKED|SC_CAMPAIGN_DETAILS;
    projection.row.native_index=99;
    strcpy_s(projection.row.map,mission.c_str()); strcpy_s(projection.row.title,"Hell on Earth");
    sc_campaign_summary summary{}; summary.known=1; summary.found=1; summary.total=20;
    strcpy_s(summary.challenges[0].unlockable,"shared/challenge"); summary.challenges[0].found=1; summary.challenges[0].required=3;
    sc_campaign_rewards rewards{}; rewards.known=1; rewards.count=2;
    rewards.entries[0].kind=SC_REWARD_MISSION; rewards.entries[0].location_id=7770001;
    strcpy_s(rewards.entries[0].unlockable,"shared/mission");
    rewards.entries[1].kind=SC_REWARD_AGGREGATE; rewards.entries[1].location_id=7770002;
    if (variant==6) {
        projection.row.id=7771008;
        CHECK(!menu().request(campaign_row_operation,projection,true,&summary,&rewards).status);
        projection.index=2; projection.row.id=stage_id;
        summary.found=2; summary.challenges[0].found=2;
        rewards.entries[0].location_id=7770003; rewards.entries[1].location_id=7770004;
    }
    CHECK(!menu().request(campaign_row_operation,projection,true,&summary,&rewards).status);
    CHECK(!menu().request(campaign_commit_operation,projection,true).status);
    menu().selected(stage_id);
}
void request(Session& owner) {
    using namespace campaign_menu;
    std::array<unsigned char,0x150> definitions{};
    std::array<unsigned char,0x1b0> campaign{};
    std::array<unsigned char,0xa0> list{};
    std::array<unsigned char,0x50> entry{};
    menu_fixture::root_definitions=reinterpret_cast<uintptr_t>(definitions.data());
    store(definitions,0x148,reinterpret_cast<uintptr_t>(campaign.data()));
    store(campaign,0x1a8,reinterpret_cast<uintptr_t>(list.data()));
    store(list,0x88,reinterpret_cast<uintptr_t>(entry.data())); store(list,0x90,int32_t{1});
    menu_fixture::assign(reinterpret_cast<uintptr_t>(entry.data()),mission.c_str());
    NativeCalls calls{};
    calls.string_init=+[](uintptr_t) {};
    calls.string_assign=menu_fixture::assign;
    calls.campaign_definitions=+[] { return menu_fixture::root_definitions; };
    calls.find_decl=find_decl;
    test_calls(calls);
    stage_projection(owner);
    std::array<unsigned char,192> bytes{};
    const auto address=reinterpret_cast<uintptr_t>(bytes.data());
    menu_fixture::at<uint8_t>(address,0xb9)=1;
    menu_fixture::assign(address+0x60,"mid_mission_checkpoint");
    std::string destination;
    CHECK(restart_mission_request(address,destination));
    CHECK(destination==mission && menu_fixture::at<NativeString>(address,0x30).data==mission);
    CHECK(menu_fixture::at<uint32_t>(address,0x98)==variant && !menu_fixture::at<uint8_t>(address,0xb9));
    CHECK(menu_fixture::at<uintptr_t>(address,variant==6?0xb0:0xa8)==
        (variant==6?0x1234:reinterpret_cast<uintptr_t>(entry.data())));
    CHECK(menu_fixture::at<NativeString>(address,0x60).length==0);
    if (variant==6) {
        CHECK(checkpoint_request(address,stage_id));
        CHECK(!checkpoint_request(address,7771008));
        std::array<unsigned char,0x1a00> prepared{};
        store(prepared,0x19d8,uintptr_t{0x1234});
        CHECK(descriptor_stage(reinterpret_cast<uintptr_t>(prepared.data()),mission,6)==stage_id);
        CHECK(descriptor_stage(reinterpret_cast<uintptr_t>(prepared.data()),mission,2)==7771008);
        store(prepared,0x19d8,uintptr_t{0x4321});
        CHECK(!descriptor_stage(reinterpret_cast<uintptr_t>(prepared.data()),mission,6));
    }
}
void return_to_hub(Campaign& campaign) {
    auto arrival=campaign.snapshot().transition;
    arrival.generation_before=arrival.generation_after;
    ++arrival.generation_after; ++arrival.event_id;
    strcpy_s(arrival.map.data(),arrival.map.size(),"game/hub/hub");
    CHECK(campaign.map_begin("game/hub/hub",arrival.generation_before,arrival.event_id,1,1));
    campaign.map_end(arrival);
    CHECK(campaign.snapshot().map_active && campaign.snapshot().end_summary_stage_id==stage_id);
    CHECK(campaign_menu::test_stage_presentation(stage_id,true));
}
int run(Session& owner,bool resume,const std::wstring& mode,uintptr_t source,const std::string& directory,
        engine::Memory& memory,const std::function<void()>& save_checkpoint) {
    if (mode.find(L"master")!=mode.npos) {
        stage_id=7776006; variant=6; mission="game/sp/e3m1_slayer/e3m1_slayer";
        campaign_menu::NativeCalls calls{};
        calls.find_decl=find_decl;
        campaign_menu::test_calls(calls);
    }
    auto& campaign=owner.campaign_run;
    if (mode==L"restart_normal") {
        CHECK(!campaign.snapshot().restart_pending); return 0;
    }
    if (!resume) {
        navigation_fixture::create(3,L"");
        CHECK(native_transition(3,L"",save_checkpoint,[&] {
            CHECK(campaign.final_death(0) && campaign.final_death(1));
            CHECK(!campaign.snapshot().restart_pending);
            CHECK(!std::filesystem::exists(recovery_namespace+L"\\stage.restart.pending"));
        })==0);
        parser(owner,source,directory,memory);
        CHECK(native_transition(3,L"",{},[&] {
            CHECK(!campaign.snapshot().restart_pending);
            stage_projection(owner);
            CHECK(campaign_menu::test_stage_presentation(stage_id));
            deathlink::use_fixture({},owner.namespace_id().c_str());
            CHECK(deathlink::test_extra_life()==1 && !campaign.snapshot().restart_pending);
            deathlink::test_terminal_death();
            if (mode.find(L"postsave")!=mode.npos) save_checkpoint();
        },{},mission)==0);
        CHECK(campaign.snapshot().restart_pending && campaign.snapshot().restart_status==1);
        return 0;
    }
    CHECK(campaign.snapshot().restart_pending && campaign.snapshot().restart_map==mission);
    request(owner);
    CHECK(campaign.begin_restart());
    parser(owner,source,directory,memory);
    CHECK(campaign.snapshot().restart_pending && !campaign.restoring_mission_checkpoint());
    if (mode==L"restart_failed") {
        campaign.restart_tick(UINT64_MAX);
        CHECK(campaign.snapshot().restart_reason=="restart_load_timeout");
        CHECK(campaign.snapshot().restart_pending && campaign.snapshot().restart_status==3); return 0;
    }
    CHECK(native_transition(3,L"",{},[&] {
        CHECK(!campaign.snapshot().restart_pending && campaign.snapshot().restart_status==2);
        if (mode==L"restart_save") save_checkpoint();
        if (variant==6) return_to_hub(campaign);
    },{},mission)==0);
    CHECK(!campaign.snapshot().restart_pending);
    return 0;
}
}
