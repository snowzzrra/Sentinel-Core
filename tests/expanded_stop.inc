namespace {
template<class Result,class... Args> struct StoppedOriginal {
    inline static std::tuple<Args...> arguments{};
    inline static unsigned calls=0;
    static Result call(Args... args) {
        arguments={args...}; ++calls;
        if constexpr (!std::is_void_v<Result>) return static_cast<Result>(7);
    }
};
template<class Result,class... Params,class... Args>
bool stopped_call(Result (*&original)(Params...),Result (*detour)(Params...),Args... args) {
    using Stub=StoppedOriginal<Result,Params...>;
    const auto saved=original;
    original=Stub::call; Stub::calls=0;
    bool result=true;
    if constexpr (std::is_void_v<Result>) detour(args...);
    else result=detour(args...)==static_cast<Result>(7);
    original=saved;
    return result && Stub::calls==1 && Stub::arguments==std::tuple<Params...>{args...};
}
}
bool test_stopped_hooks() {
    alignas(8) std::array<uint8_t,0x200> worker;
    worker.fill(0x5a);
    const auto before=worker;
    const auto address=reinterpret_cast<uintptr_t>(worker.data());
    const auto previous_creation=recipe_creation;
    const auto previous_collection=summon_collection;
    const auto previous_traversal=traversal_entry;
    const auto previous_parasite=samuel_without_parasite;
    const auto previous_worker=worker_recipe;
    const auto previous_ai=active_ai;
    const auto previous_speed=active_speed;
    recipe_creation=reinterpret_cast<RecipeCreation*>(1);
    summon_collection=reinterpret_cast<SummonCollection*>(1);
    traversal_entry=reinterpret_cast<TraversalEntry*>(1);
    samuel_without_parasite=true;
    const std::string key="stopped-worker";
    worker_recipe=&key;
    worker_recipes[address]=key;
    kill_progress[address].seen=42;
    health_progress[address].tracked=true;
    active_ai=address; active_speed=3;
    recipe_fault.store(false);
    stop();
    bool ok=true;
#define STOP_CALL(original,detour,...) do { \
    if (!stopped_call(original,detour,__VA_ARGS__)) { \
        std::fprintf(stderr,"FAIL stopped hook: %s\n",#detour); ok=false; \
    } \
} while (0)
    STOP_CALL(original_maintain_tick,maintain_tick_detour,address);
    STOP_CALL(original_stagger_tick,stagger_tick_detour,address);
    STOP_CALL(original_queue_command,queue_command_detour,uintptr_t{2},address);
    STOP_CALL(original_movement,movement_detour,address);
    STOP_CALL(original_haste,haste_detour,address);
    STOP_CALL(original_controller,controller_detour,address,uintptr_t{2},uintptr_t{3},4.0f);
    STOP_CALL(original_hosts,occupied_hosts_detour,static_cast<HostList*>(nullptr));
    STOP_CALL(original_spawn,spawn_detour,uintptr_t{1},address,uintptr_t{3},uintptr_t{4},uintptr_t{5});
    STOP_CALL(original_group,group_detour,uintptr_t{1},reinterpret_cast<uint64_t*>(worker.data()),uintptr_t{3});
    STOP_CALL(original_register_spawn,register_spawn_detour,uintptr_t{1},reinterpret_cast<uint64_t*>(worker.data()));
    STOP_CALL(original_single,single_detour,uintptr_t{1},address,uint64_t{3},uintptr_t{4},uintptr_t{5});
    STOP_CALL(original_multiple,multiple_detour,uintptr_t{1},address,uint64_t{3},int32_t{1},uintptr_t{5},uintptr_t{6});
    STOP_CALL(original_spirit,spirit_detour,uintptr_t{1},address,uintptr_t{3},uint64_t{4},uintptr_t{5},uint8_t{6});
    STOP_CALL(original_point_factory,point_factory_detour,address,uintptr_t{2});
    STOP_CALL(original_point_candidate,point_candidate_detour,uintptr_t{1},address,uint64_t{3});
    STOP_CALL(original_point_index,point_index_detour,uintptr_t{1},address,uint64_t{3});
    STOP_CALL(original_point_available,point_available_detour,address,uintptr_t{2},uint8_t{3},uint8_t{4},uint8_t{5},uint8_t{6});
    STOP_CALL(original_remaining,remaining_detour,address);
    STOP_CALL(original_remaining_added,remaining_added_detour,address,uintptr_t{2});
    STOP_CALL(original_remaining_dead,remaining_dead_detour,address,uintptr_t{2});
    STOP_CALL(original_kill_construct,kill_construct_detour,address,int32_t{2},uintptr_t{3},uint64_t{4},uint64_t{5});
    STOP_CALL(original_kill,kill_detour,address);
    STOP_CALL(original_kill_dead,kill_dead_detour,address,uintptr_t{2});
    STOP_CALL(original_health,health_detour,address);
    STOP_CALL(original_health_added,health_added_detour,address,uintptr_t{2});
    STOP_CALL(original_health_damage,health_damage_detour,address,uintptr_t{2},3.0f);
    STOP_CALL(original_archvile,archvile_detour,uintptr_t{1},address,uintptr_t{3},uintptr_t{4},uintptr_t{5},"group");
    STOP_CALL(original_buff,buff_detour,uintptr_t{1},address,uintptr_t{3},uintptr_t{4},"group");
    STOP_CALL(original_possessed,possessed_detour,uintptr_t{1},address,uint64_t{3},uintptr_t{4},uintptr_t{5},uintptr_t{6},uint64_t{7},uintptr_t{8},uint8_t{9});
    STOP_CALL(original_summon_start,summon_start_detour,address);
    STOP_CALL(original_summon_create,summon_create_detour,address,uintptr_t{2},uintptr_t{3},uint64_t{4},uintptr_t{5});
    STOP_CALL(original_summon_update,summon_update_detour,address,uintptr_t{2});
    STOP_CALL(original_summon_complete,summon_complete_detour,address);
    STOP_CALL(original_actor_factory,actor_factory_detour,address);
    STOP_CALL(original_summon_request_complete,summon_request_complete_detour,address);
    STOP_CALL(original_summon_cleanup,summon_cleanup_detour,address);
    STOP_CALL(original_death_actions,death_actions_detour,address,uintptr_t{2},uintptr_t{3},uintptr_t{4});
    STOP_CALL(original_post_one,post_one_detour,address,uintptr_t{2},int32_t{3},uintptr_t{4});
    STOP_CALL(assign_native_text,role_text_detour,uintptr_t{0x88},"role");
    STOP_CALL(original_samuel_setup,samuel_setup_detour,address,uintptr_t{2});
    STOP_CALL(original_show,show_detour,address);
    STOP_CALL(original_show_now,show_now_detour,address);
    STOP_CALL(original_hide,hide_detour,address);
    STOP_CALL(original_traversal_enter,traversal_enter_detour,address,uintptr_t{2},uintptr_t{3},uintptr_t{4});
    STOP_CALL(original_animation_valid,animation_valid_detour,address,"AI waiting at traversal it has no anim for");
    STOP_CALL(original_resurrect_factory,resurrect_detour,address,int32_t{2},static_cast<const float*>(nullptr),static_cast<const float*>(nullptr));
    STOP_CALL(original_darklord_summon,darklord_summon_detour,address);
#undef STOP_CALL
    ok &= worker==before && !recipe_fault.load() && worker_recipe==&key &&
        worker_recipes.at(address)==key && kill_progress.at(address).seen==42 && health_progress.at(address).tracked &&
        recipe_creation==reinterpret_cast<RecipeCreation*>(1) && summon_collection==reinterpret_cast<SummonCollection*>(1) &&
        traversal_entry==reinterpret_cast<TraversalEntry*>(1) && samuel_without_parasite &&
        active_ai==address && active_speed==3;
    recipe_creation=previous_creation; summon_collection=previous_collection; traversal_entry=previous_traversal;
    samuel_without_parasite=previous_parasite; worker_recipe=previous_worker;
    active_ai=previous_ai; active_speed=previous_speed;
    worker_recipes.erase(address); kill_progress.erase(address); health_progress.erase(address);
    return ok;
}
bool test_hook_faults() {
    const auto previous_stopped=stopped.exchange(false);
    const auto previous_fault=recipe_fault.exchange(false);
    const auto previous_tick=original_maintain_tick;
    const auto previous_queue=original_queue_command;
    const auto previous_cast=cast_maintain;
    const auto previous_worker=worker_recipe;
    alignas(8) std::array<uint8_t,0x100> worker{};
    const auto address=reinterpret_cast<uintptr_t>(worker.data());
    const std::string key="outer-worker";
    worker_recipe=&key;
    static unsigned engine_calls=0;
    static bool locked=false;
    engine_calls=0;
    original_maintain_tick=+[](uintptr_t) {
        ++engine_calls;
        std::thread probe([] {
            locked=!admission_mutex.try_lock();
            if (!locked) admission_mutex.unlock();
        });
        probe.join();
        throw 7;
    };
    bool propagated=false;
    try { maintain_tick_detour(address); }
    catch (int error) { propagated=error==7; }
    bool ok=propagated && locked && engine_calls==1 && worker_recipe==&key && !recipe_fault.load();
    original_queue_command=+[](uintptr_t,uintptr_t) { ++engine_calls; };
    cast_maintain=+[](uintptr_t)->uintptr_t { throw 8; };
    queue_command_detour(0,address);
    ok &= engine_calls==2 && recipe_fault.load() && worker_recipe==&key && worker==std::array<uint8_t,0x100>{};
    bool released=false;
    std::thread probe([&] {
        released=admission_mutex.try_lock();
        if (released) admission_mutex.unlock();
    });
    probe.join();
    original_maintain_tick=previous_tick; original_queue_command=previous_queue; cast_maintain=previous_cast;
    worker_recipe=previous_worker; recipe_fault.store(previous_fault); stopped.store(previous_stopped);
    return ok && released;
}
