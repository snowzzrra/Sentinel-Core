// Explicit native callees only; action decoding, slot catalog, reservation and
// difficulty lock run through the production campaign adapter.
namespace navigation_fixture {
uintptr_t action=0,setting=0;
uintptr_t navigation_screen=1;
unsigned selected=0,created=0,internal=0,integer=0,choices=0;
uint32_t wanted=0;
unsigned pumped=0;
uint32_t pump_expected=1;
void configure_calls() {
    CampaignNativeTestCalls calls{};
    calls.action=[](uintptr_t,uintptr_t)->uint64_t { ++choices; return 77; };
    calls.slot=[](uintptr_t)->uint32_t { return 0; };
    calls.menu=[]()->uintptr_t { return 0x123; };
    calls.select=[](uintptr_t menu,uint32_t slot) { CHECK(menu==0x123 && slot==0); ++selected; CHECK(test_campaign_action(navigation_screen,action)==1); };
    calls.integer=[](uintptr_t self,uint32_t value,uint8_t force) {
        ++integer; CHECK(value==wanted && force==1); char bytes[]{static_cast<char>('0'+value),0};
        CHECK(test_campaign_cvar(self,bytes,force)==1);
    };
    calls.cvar=[](uintptr_t self,const char* value,uint8_t)->uint64_t {
        *reinterpret_cast<uint32_t*>(*reinterpret_cast<uintptr_t*>(self)+8)=static_cast<uint32_t>(value[0]-'0'); return 1;
    };
    calls.new_game=[](uintptr_t menu,uint32_t value,uint8_t extra) {
        ++created; CHECK(selected==1 && integer==1 && value==wanted && !extra);
        test_campaign_internal(menu,value,extra,0);
    };
    calls.internal=[](uintptr_t,uint32_t value,uint8_t extra,uint32_t) {
        CHECK(value==wanted && !extra); ++internal;
        const char* original="devmenuoption/new_campaign";
        std::array<uintptr_t,6> arguments{5,reinterpret_cast<uintptr_t>("loadDevMenuOption"),
            reinterpret_cast<uintptr_t>(original),reinterpret_cast<uintptr_t>("0"),
            reinterpret_cast<uintptr_t>("0"),reinterpret_cast<uintptr_t>("newCampaign")};
        test_campaign_devmenu(reinterpret_cast<uintptr_t>(arguments.data()));
        CHECK(arguments[2]==reinterpret_cast<uintptr_t>(original));
    };
    calls.pump=[](uintptr_t screen) {
        CHECK(*reinterpret_cast<uint32_t*>(screen+0x10c)==pump_expected); ++pumped;
        *reinterpret_cast<uint32_t*>(screen+0x108)=pump_expected;
    };
    calls.devmenu=[](uintptr_t arguments) {
        CHECK(!std::strcmp(*reinterpret_cast<const char**>(arguments+0x10),"devmenuoption/ap_unified_campaign"));
        CHECK(!std::strcmp(*reinterpret_cast<const char**>(arguments+0x28),"newCampaign"));
    };
    test_campaign_calls(calls);
}
void vanilla() {
    configure_calls(); CHECK(test_campaign_action(1,0)==77); CHECK(choices==1);
    std::array<unsigned char,0x110> screen{};
    *reinterpret_cast<uint32_t*>(screen.data()+0x10c)=1;
    test_campaign_pump(reinterpret_cast<uintptr_t>(screen.data()));
    CHECK(pumped==1 && !selected); // Vanilla retains native slot-picker state.
}
void create(uint32_t difficulty,const std::wstring& defect) {
    wanted=difficulty;
    auto* allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4600000,MEM_RESERVE,PAGE_NOACCESS)); CHECK(allocation);
    for (size_t offset:{size_t{0x397f000},size_t{0x45f8000}}) CHECK(VirtualAlloc(allocation+offset,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const auto base=reinterpret_cast<uintptr_t>(allocation);
    const char prefix[]="GAME-"; *reinterpret_cast<const char**>(base+0x397f4a8)=prefix;
    std::array<unsigned char,16> cvar{}; *reinterpret_cast<uintptr_t*>(base+0x45f8590)=reinterpret_cast<uintptr_t>(cvar.data());
    std::array<unsigned char,0xc0> root{}; engine::Binding binding{}; binding.image.base=base; binding.root=reinterpret_cast<uintptr_t>(root.data());
    test_campaign_binding(base,binding.root); native::test_events(binding,native_load);
    configure_calls();
    std::array<uint64_t,3> argument{5,3,0}, request{1,reinterpret_cast<uintptr_t>(argument.data()),1};
    action=reinterpret_cast<uintptr_t>(request.data());
    if (defect==L"extra_life" || defect==L"ultra") {
        test_campaign_new(0x123,defect==L"ultra"?4:difficulty,defect==L"extra_life"?1:0);
        CHECK(session().campaign_run.snapshot().reason=="ap_new_game_navigation_or_mode_invalid" && !created);
    } else {
        std::array<unsigned char,0x110> screen{};
        const auto screen_address=reinterpret_cast<uintptr_t>(screen.data());
        navigation_screen=screen_address;
        *reinterpret_cast<uint32_t*>(screen.data()+0x10c)=1;
        pump_expected=2;
        test_campaign_pump(screen_address);
        CHECK(selected==1 && !created && !internal);
        test_campaign_pump(screen_address);
        CHECK(selected==1); // Stable menu updates do not repeat native selection.
        selected=0;
        argument[1]=0; // Ordinary single-campaign NewGame, not a slot widget.
        CHECK(test_campaign_action(screen_address,action)==1);
        CHECK(created==1 && selected==1 && integer==1 && internal==1 && choices==1);
        CHECK(session().campaign_run.snapshot().phase=="native_start_queued");
        CHECK(*reinterpret_cast<uint32_t*>(cvar.data()+8)==difficulty);
        CHECK(test_campaign_action(screen_address,action)==1 && created==1); // Duplicate and nested callback reserve once.
        const auto boundaries=session().btrace.snapshot();
        CHECK(std::strcmp(boundaries.stages[static_cast<size_t>(BStage::native_start)].predicate,"native_start_internal_returned")==0);
        CHECK(std::strcmp(boundaries.stages[static_cast<size_t>(BStage::bootstrap)].predicate,"fortress_devmenu_returned")==0);
        char other[]{static_cast<char>('0'+(difficulty+1)%4),0};
        CHECK(test_campaign_cvar(base+0x45f8590,other,1)==0);
        CHECK(*reinterpret_cast<uint32_t*>(cvar.data()+8)==difficulty);
        const auto diagnostics=session().btrace.snapshot();
        const auto& blocked=diagnostics.stages[static_cast<size_t>(BStage::difficulty)];
        CHECK(blocked.status==BStatus::blocked && std::strcmp(blocked.predicate,"room_difficulty_change_blocked")==0);
        CHECK(diagnostic_fact(blocked,"actual")==(difficulty+1)%4 && diagnostic_fact(blocked,"expected")==difficulty);
        CHECK(!diagnostics.first_failure.sequence);
        argument[1]=8; CHECK(test_campaign_action(1,action)==77 && choices==2);
    }
    CHECK(VirtualFree(allocation,0,MEM_RELEASE));
}
}
