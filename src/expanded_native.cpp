// Installation and map lifecycle for expanded native features.
#include "features/expanded/expanded_internal.h"

namespace sentinel::expanded::detail {
struct ObserveCosts {
    uint64_t calls=0,total=0,maximum=0,slow=0,wait=0,wait_max=0,sync=0,sync_max=0,scan=0,scan_max=0,next=0;
};
extern ObserveCosts observe_costs;
struct ObserveCost {
    CostClock::time_point start=CostClock::now();
    uint64_t wait=0,sync=0,scan=0;
    uint64_t living=0;
    uintptr_t world=0;
    ~ObserveCost() {
        auto& value=observe_costs;
        const auto elapsed=elapsed_us(start);
        ++value.calls; value.total+=elapsed; value.maximum=std::max(value.maximum,elapsed);
        value.slow+=elapsed>=16000; value.wait+=wait; value.wait_max=std::max(value.wait_max,wait);
        value.sync+=sync; value.sync_max=std::max(value.sync_max,sync);
        value.scan+=scan; value.scan_max=std::max(value.scan_max,scan);
        static uint64_t peak=0;
        peak=std::max(peak,living);
        const auto now=GetTickCount64();
        if (now<value.next) return;
        value.next=now+5000;
        save::session().btrace.record(save::BStage::enemy_population,save::BStatus::succeeded,"enemy_population",0,
            {{"living",living},{"peak",peak}},world);
        save::session().btrace.record(save::BStage::enemy_motion_cost,save::BStatus::succeeded,"enemy_motion_cost_us",0,
            {{"queries",motion_queries.load(std::memory_order_relaxed)},{"lookup_us",motion_lookup_us.load(std::memory_order_relaxed)},
             {"controllers",motion_controllers.load(std::memory_order_relaxed)},{"native_us",motion_native_us.load(std::memory_order_relaxed)}},world);
        save::session().btrace.record(save::BStage::enemy_runtime,save::BStatus::succeeded,"enemy_runtime_cost_us",0,
            {{"calls",value.calls},{"total_us",value.total},{"max_us",value.maximum},{"over_16ms",value.slow},
             {"lock_us",value.wait},{"lock_max_us",value.wait_max},{"sync_us",value.sync},{"sync_max_us",value.sync_max},
             {"scan_us",value.scan},{"scan_max_us",value.scan_max}},world);
    }
};
}

namespace sentinel::expanded::detail {

hooks::HookSet expanded_hooks("expanded");

uint64_t elapsed_us(CostClock::time_point start) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(CostClock::now()-start).count());
}

ObserveCosts observe_costs;

std::atomic<bool> stopped{true};

}

namespace sentinel::expanded {
using namespace detail;

bool install(const engine::Binding& binding, HANDLE stop_event) {
    if (!save::session().campaign_run.enabled()) return true;
    if (binding.metadata.profile != SC_PROFILE_STEAM_20260818) return false;
    death_target_returns = {binding.image.base + 0xa1364a, binding.image.base + 0xa136f8, binding.image.base + 0xa137b4};
    const auto movement = builds::resolve(binding.image.base, builds::steam_20260818().expanded[0]);
    const auto haste = builds::resolve(binding.image.base, builds::steam_20260818().expanded[1]);
    const auto name = builds::resolve(binding.image.base, builds::steam_20260818().expanded[2]);
    const auto controller = builds::resolve(binding.image.base, builds::steam_20260818().expanded[3]);
    const auto hosts = builds::resolve(binding.image.base, builds::steam_20260818().expanded[4]);
    const auto spawn = builds::resolve(binding.image.base, builds::steam_20260818().expanded[5]);
    const auto group = builds::resolve(binding.image.base, builds::steam_20260818().expanded[6]);
    const auto registered = builds::resolve(binding.image.base, builds::steam_20260818().expanded[7]);
    const auto single = builds::resolve(binding.image.base, builds::steam_20260818().expanded[8]);
    const auto multiple = builds::resolve(binding.image.base, builds::steam_20260818().expanded[9]);
    const auto spirit = builds::resolve(binding.image.base, builds::steam_20260818().expanded[10]);
    const auto point_factory = builds::resolve(binding.image.base, builds::steam_20260818().expanded[11]);
    const auto point_candidate = builds::resolve(binding.image.base, builds::steam_20260818().expanded[12]);
    const auto point_index = builds::resolve(binding.image.base, builds::steam_20260818().expanded[13]);
    const auto rewind = builds::resolve(binding.image.base, builds::steam_20260818().expanded[14]);
    const auto admission = builds::resolve(binding.image.base, builds::steam_20260818().expanded[15]);
    const auto remaining = builds::resolve(binding.image.base, builds::steam_20260818().expanded[16]);
    const auto remaining_added = builds::resolve(binding.image.base, builds::steam_20260818().expanded[17]);
    const auto remaining_dead = builds::resolve(binding.image.base, builds::steam_20260818().expanded[18]);
    const auto entity_reference = builds::resolve(binding.image.base, builds::steam_20260818().expanded[19]);
    const auto manager_reference = builds::resolve(binding.image.base, builds::steam_20260818().expanded[20]);
    const auto ignored = builds::resolve(binding.image.base, builds::steam_20260818().expanded[21]);
    const auto lookup = builds::resolve(binding.image.base, builds::steam_20260818().expanded[22]);
    const auto& recipe_rvas = builds::steam_20260818().recipes_rvas;
    const auto& recipe_prefixes = builds::steam_20260818().recipes_bytes;
    std::array<native::Target,builds::BuildDescriptor::recipes_rvas.size()> recipe_targets{};
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    for (size_t i = 0; i < std::size(recipe_targets); ++i) {
        auto& target = recipe_targets[i]; target.address = binding.image.base + recipe_rvas[i];
        for (size_t byte = 0; byte < target.bytes.size(); ++byte) {
            unsigned value = 0;
            if (sscanf_s(recipe_prefixes[i] + byte * 2, "%2x", &value) != 1) return false;
            target.bytes[byte] = static_cast<uint8_t>(value);
        }
        if (i == 5) {
            target.signature_offset = 0x20;
            target.signature = {0x92,0x00,0x00,0x00,0x48,0x85,0xc9,0x74,0x21,0xe8,0xd2,0xa5,0xb7,0x01,0x48,0x85,
                0xc0,0x74,0x0b,0x48,0x8b,0xd3,0x48,0x8b,0xc8,0xe8,0x02,0x74,0x6b,0x00,0x48,0x8b};
        }
        if (i == 31) {
            target.signature_offset = 0x20;
            target.signature = {0xf2,0x48,0x8b,0xe9,0xb2,0x01,0x48,0x81,0xc1,0x98,0x0c,0x00,0x00,0x33,0xff,0xe8,
                0xfc,0xd3,0x83,0xff,0x84,0xc0,0x44,0x8b,0xff,0xb9,0x01,0x00,0x00,0x00,0x44,0x0f};
        }
        if (i == 20 || i == 22 || i == 28 || i == 33 || i == 38 || i == 49) {
            if (native::validate_leaf_target(memory, binding.image, target, i == 49 ? 39 : i == 38 ? 774 : i == 20 ? 47 : i == 28 || i == 33 ? 8 : 32, stop_event, deadline)) return false;
        } else if (native::validate_target(memory, binding.image, target, stop_event, deadline)) return false;
    }
    if (native::validate_leaf_target(memory, binding.image, movement, 36, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, haste, 52, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, name, 25, stop_event, deadline) ||
        native::validate_target(memory, binding.image, controller, stop_event, deadline) ||
        native::validate_target(memory, binding.image, hosts, stop_event, deadline) ||
        native::validate_target(memory, binding.image, spawn, stop_event, deadline) ||
        native::validate_target(memory, binding.image, group, stop_event, deadline) ||
        native::validate_target(memory, binding.image, registered, stop_event, deadline) ||
        native::validate_target(memory, binding.image, single, stop_event, deadline) ||
        native::validate_target(memory, binding.image, multiple, stop_event, deadline) ||
        native::validate_target(memory, binding.image, spirit, stop_event, deadline) ||
        native::validate_target(memory, binding.image, point_factory, stop_event, deadline) ||
        native::validate_target(memory, binding.image, point_candidate, stop_event, deadline) ||
        native::validate_target(memory, binding.image, point_index, stop_event, deadline) ||
        native::validate_target(memory, binding.image, admission, stop_event, deadline) ||
        native::validate_target(memory, binding.image, remaining, stop_event, deadline) ||
        native::validate_target(memory, binding.image, remaining_added, stop_event, deadline) ||
        native::validate_target(memory, binding.image, remaining_dead, stop_event, deadline) ||
        native::validate_target(memory, binding.image, entity_reference, stop_event, deadline) ||
        native::validate_target(memory, binding.image, manager_reference, stop_event, deadline) ||
        native::validate_target(memory, binding.image, lookup, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, ignored, 18, stop_event, deadline) ||
        native::validate_leaf_target(memory, binding.image, rewind, 10, stop_event, deadline)) return false;
    prepare_recovery(binding, stop_event);
    definition_name = reinterpret_cast<DefinitionName>(name.address);
    resolve_actor = reinterpret_cast<ResolveActor>(binding.image.base + 0x6cb100);
    grow_hosts = reinterpret_cast<GrowHosts>(binding.image.base + 0x357040);
    native_admission = reinterpret_cast<NativeAdmission>(admission.address);
    rewind_timeline = reinterpret_cast<RewindTimeline>(rewind.address);
    refresh_entity = reinterpret_cast<RefreshEntity>(entity_reference.address);
    refresh_manager = reinterpret_cast<RefreshEntity>(manager_reference.address);
    ignored_actor = reinterpret_cast<IgnoredActor>(ignored.address);
    find_entity = reinterpret_cast<FindEntity>(lookup.address);
    construct_stagger = reinterpret_cast<ConstructStagger>(recipe_targets[3].address);
    allocate_worker = reinterpret_cast<AllocateWorker>(recipe_targets[4].address);
    assign_manager = reinterpret_cast<AssignManager>(recipe_targets[5].address);
    cast_maintain = reinterpret_cast<WorkerCast>(recipe_targets[9].address);
    cast_stagger = reinterpret_cast<WorkerCast>(recipe_targets[10].address);
    compare_text = reinterpret_cast<CompareText>(recipe_targets[11].address);
    original_spirit_helper = reinterpret_cast<SpiritHelper>(recipe_targets[17].address);
    construct_time = reinterpret_cast<ConstructTime>(recipe_targets[19].address);
    native_clock = reinterpret_cast<NativeClock>(recipe_targets[20].address);
    native_step = reinterpret_cast<NativeStep>(recipe_targets[21].address);
    native_duration = reinterpret_cast<NativeDuration>(recipe_targets[22].address);
    definition_database = reinterpret_cast<DefinitionDatabase>(recipe_targets[28].address);
    lookup_definition = reinterpret_cast<LookupDefinition>(recipe_targets[29].address);
    destroy_native_text = reinterpret_cast<NativeUnary>(recipe_targets[30].address);
    manager_contains = reinterpret_cast<ManagerContains>(recipe_targets[31].address);
    monster_mask = reinterpret_cast<MonsterMask>(recipe_targets[38].address);
    summon_label_symbol = binding.image.base + 0x463c8d8;
    activate_event = binding.image.base + 0x6c1cf70;
    show_event = binding.image.base + 0x6c1dc90;
    post_event = reinterpret_cast<PostEvent>(recipe_targets[43].address);
    cancel_event = reinterpret_cast<CancelEvent>(recipe_targets[44].address);
    grow_summon_records = reinterpret_cast<GrowSummonRecords>(recipe_targets[48].address);
    construct_native_string = reinterpret_cast<ConstructNativeString>(recipe_targets[49].address);
    animation_path_table = binding.image.base + 0x2a9a1f8;
    darklord_image = binding.image.base;

    world_global = binding.image.base + 0x45f7370;
    void* sites[] = {reinterpret_cast<void*>(movement.address), reinterpret_cast<void*>(haste.address),
                     reinterpret_cast<void*>(controller.address),
                     reinterpret_cast<void*>(hosts.address), reinterpret_cast<void*>(spawn.address),
                     reinterpret_cast<void*>(group.address), reinterpret_cast<void*>(registered.address),
                     reinterpret_cast<void*>(single.address), reinterpret_cast<void*>(multiple.address),
                     reinterpret_cast<void*>(spirit.address), reinterpret_cast<void*>(point_factory.address),
                     reinterpret_cast<void*>(point_candidate.address), reinterpret_cast<void*>(point_index.address),
                     reinterpret_cast<void*>(remaining.address), reinterpret_cast<void*>(remaining_added.address),
                     reinterpret_cast<void*>(remaining_dead.address), reinterpret_cast<void*>(recipe_targets[0].address),
                     reinterpret_cast<void*>(recipe_targets[1].address), reinterpret_cast<void*>(recipe_targets[2].address),
                     reinterpret_cast<void*>(recipe_targets[6].address), reinterpret_cast<void*>(recipe_targets[7].address),
                     reinterpret_cast<void*>(recipe_targets[8].address), reinterpret_cast<void*>(recipe_targets[12].address),
                     reinterpret_cast<void*>(recipe_targets[13].address), reinterpret_cast<void*>(recipe_targets[14].address),
                     reinterpret_cast<void*>(recipe_targets[15].address), reinterpret_cast<void*>(recipe_targets[16].address),
                     reinterpret_cast<void*>(recipe_targets[18].address), reinterpret_cast<void*>(recipe_targets[23].address),
                     reinterpret_cast<void*>(recipe_targets[24].address), reinterpret_cast<void*>(recipe_targets[25].address),
                     reinterpret_cast<void*>(recipe_targets[26].address), reinterpret_cast<void*>(recipe_targets[27].address),
                     reinterpret_cast<void*>(recipe_targets[33].address), reinterpret_cast<void*>(recipe_targets[34].address),
                     reinterpret_cast<void*>(recipe_targets[35].address), reinterpret_cast<void*>(recipe_targets[36].address),
                     reinterpret_cast<void*>(recipe_targets[37].address), reinterpret_cast<void*>(recipe_targets[32].address),
                     reinterpret_cast<void*>(recipe_targets[39].address), reinterpret_cast<void*>(recipe_targets[40].address),
                     reinterpret_cast<void*>(recipe_targets[41].address), reinterpret_cast<void*>(recipe_targets[42].address),
                     reinterpret_cast<void*>(recipe_targets[45].address), reinterpret_cast<void*>(recipe_targets[46].address),
                     reinterpret_cast<void*>(recipe_targets[47].address), reinterpret_cast<void*>(recipe_targets[50].address)};
    void* detours[] = {reinterpret_cast<void*>(movement_detour_entry), reinterpret_cast<void*>(haste_detour_entry),
                       reinterpret_cast<void*>(controller_detour_entry),
                       reinterpret_cast<void*>(occupied_hosts_detour_entry), reinterpret_cast<void*>(spawn_detour_entry),
                       reinterpret_cast<void*>(group_detour_entry), reinterpret_cast<void*>(register_spawn_detour_entry),
                       reinterpret_cast<void*>(single_detour_entry), reinterpret_cast<void*>(multiple_detour_entry),
                       reinterpret_cast<void*>(spirit_detour_entry), reinterpret_cast<void*>(point_factory_detour_entry),
                       reinterpret_cast<void*>(point_candidate_detour_entry), reinterpret_cast<void*>(point_index_detour_entry),
                       reinterpret_cast<void*>(remaining_detour_entry), reinterpret_cast<void*>(remaining_added_detour_entry),
                       reinterpret_cast<void*>(remaining_dead_detour_entry), reinterpret_cast<void*>(maintain_tick_detour_entry),
                       reinterpret_cast<void*>(stagger_tick_detour_entry), reinterpret_cast<void*>(queue_command_detour_entry),
                       reinterpret_cast<void*>(kill_construct_detour_entry), reinterpret_cast<void*>(kill_detour_entry),
                       reinterpret_cast<void*>(kill_dead_detour_entry), reinterpret_cast<void*>(health_detour_entry),
                       reinterpret_cast<void*>(health_added_detour_entry), reinterpret_cast<void*>(health_damage_detour_entry),
                       reinterpret_cast<void*>(archvile_detour_entry), reinterpret_cast<void*>(buff_detour_entry),
                       reinterpret_cast<void*>(possessed_detour_entry), reinterpret_cast<void*>(summon_start_detour_entry),
                       reinterpret_cast<void*>(summon_create_detour_entry), reinterpret_cast<void*>(summon_update_detour_entry),
                       reinterpret_cast<void*>(summon_complete_detour_entry), reinterpret_cast<void*>(actor_factory_detour_entry),
                       reinterpret_cast<void*>(summon_request_complete_detour_entry), reinterpret_cast<void*>(summon_cleanup_detour_entry),
                       reinterpret_cast<void*>(point_available_detour_entry), reinterpret_cast<void*>(death_actions_detour_entry),
                       reinterpret_cast<void*>(post_one_detour_entry), reinterpret_cast<void*>(role_text_detour_entry),
                       reinterpret_cast<void*>(samuel_setup_detour_entry), reinterpret_cast<void*>(show_detour_entry),
                       reinterpret_cast<void*>(show_now_detour_entry), reinterpret_cast<void*>(hide_detour_entry),
                       reinterpret_cast<void*>(traversal_enter_detour_entry), reinterpret_cast<void*>(animation_valid_detour_entry),
                       reinterpret_cast<void*>(resurrect_detour_entry), reinterpret_cast<void*>(darklord_summon_detour_entry)};
    void** originals[] = {reinterpret_cast<void**>(&original_movement), reinterpret_cast<void**>(&original_haste),
                           reinterpret_cast<void**>(&original_controller),
                           reinterpret_cast<void**>(&original_hosts), reinterpret_cast<void**>(&original_spawn),
                           reinterpret_cast<void**>(&original_group), reinterpret_cast<void**>(&original_register_spawn),
                           reinterpret_cast<void**>(&original_single), reinterpret_cast<void**>(&original_multiple),
                           reinterpret_cast<void**>(&original_spirit), reinterpret_cast<void**>(&original_point_factory),
                           reinterpret_cast<void**>(&original_point_candidate), reinterpret_cast<void**>(&original_point_index),
                           reinterpret_cast<void**>(&original_remaining), reinterpret_cast<void**>(&original_remaining_added),
                           reinterpret_cast<void**>(&original_remaining_dead), reinterpret_cast<void**>(&original_maintain_tick),
                           reinterpret_cast<void**>(&original_stagger_tick), reinterpret_cast<void**>(&original_queue_command),
                           reinterpret_cast<void**>(&original_kill_construct), reinterpret_cast<void**>(&original_kill),
                           reinterpret_cast<void**>(&original_kill_dead), reinterpret_cast<void**>(&original_health),
                           reinterpret_cast<void**>(&original_health_added), reinterpret_cast<void**>(&original_health_damage),
                           reinterpret_cast<void**>(&original_archvile), reinterpret_cast<void**>(&original_buff),
                           reinterpret_cast<void**>(&original_possessed), reinterpret_cast<void**>(&original_summon_start),
                           reinterpret_cast<void**>(&original_summon_create), reinterpret_cast<void**>(&original_summon_update),
                           reinterpret_cast<void**>(&original_summon_complete), reinterpret_cast<void**>(&original_actor_factory),
                           reinterpret_cast<void**>(&original_summon_request_complete), reinterpret_cast<void**>(&original_summon_cleanup),
                           reinterpret_cast<void**>(&original_point_available), reinterpret_cast<void**>(&original_death_actions),
                           reinterpret_cast<void**>(&original_post_one), reinterpret_cast<void**>(&assign_native_text),
                           reinterpret_cast<void**>(&original_samuel_setup), reinterpret_cast<void**>(&original_show),
                           reinterpret_cast<void**>(&original_show_now), reinterpret_cast<void**>(&original_hide),
                           reinterpret_cast<void**>(&original_traversal_enter), reinterpret_cast<void**>(&original_animation_valid),
                           reinterpret_cast<void**>(&original_resurrect_factory), reinterpret_cast<void**>(&original_darklord_summon)};
    std::array<hooks::HookSpec,std::size(sites)> specs{};
    for (size_t i = 0; i < specs.size(); ++i)
        specs[i] = {reinterpret_cast<uintptr_t>(sites[i]), detours[i], originals[i]};
    if (!expanded_hooks.install(specs.data(), specs.size(), stop_event)) return false;
    if (!install_recipe_checkpoint(binding, stop_event,
            {lock_recipe_checkpoint, unlock_recipe_checkpoint, capture_recipe_checkpoint, restore_recipe_checkpoint})) {
        const auto clean = expanded_hooks.uninstall();
        save::session().btrace.record(save::BStage::native_start, save::BStatus::refused,
            "expanded_checkpoint_installation_failed", 0, {{"cleanup_complete", clean}});
        return false;
    }
    stopped.store(false, std::memory_order_release);
    return true;
}

void stop() { stopped.store(true, std::memory_order_release); stop_recipe_checkpoint(); }

void observe(uintptr_t world) {
    if (stopped.load(std::memory_order_acquire)) return;
    ObserveCost cost; cost.world=world;
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    cost.wait=elapsed_us(cost.start);
    ++darklord_frame;
    darklord_sample(world, GetTickCount64());
    if (restored_independent && !load_native_recipes()) return;
    try {
        if (!restore_autonomous_summons(world)) { set_recipe_fault(__func__, "summon_restore_failed"); return; }
        if (pending_summon_checkpoint) return;
        std::vector<uintptr_t> points;
        for (const auto& [point, producer] : direct_points) { (void)producer; points.push_back(point); }
        for (const auto point : points) drain_direct_point(point);
    } catch (...) { set_recipe_fault(__func__, "exception"); return; }
    if (independent_memberships_pending) {
        const auto start=CostClock::now();
        try { if (!synchronize_independent_memberships_safe(world)) set_recipe_fault(__func__, "membership_sync_failed"); }
        catch (...) { set_recipe_fault(__func__, "exception"); }
        cost.sync=elapsed_us(start);
        if (recipe_fault.load()) return;
    }
    if (recipe_actors.empty() && worker_recipes.empty() && pending_recipes.empty() && component_summons.empty() &&
        recipe_actor_credits.empty() && deferred_shows.empty() && pending_independent_shows.empty()) return;
    try {
        for (auto pending = pending_independent_shows.begin(); pending != pending_independent_shows.end();) {
            const auto actor = find_entity(world, pending->first.c_str());
            char origin[21];
            if (!actor) { ++pending; continue; }
            if (!independent_origin(actor, origin)) { set_recipe_fault(__func__, "independent_origin_invalid", nullptr, actor); return; }
            const auto recipe = native_recipes.find(origin);
            if (recipe == native_recipes.end()) { set_recipe_fault(__func__, "independent_recipe_missing", nullptr, actor); return; }
            if (recipe->second.bodies.empty()) {
                independent_shows.insert(pending->first);
                pending = pending_independent_shows.erase(pending);
                continue;
            }
            independent_pose(actor, pending->second.data());
            independent_shows.insert(pending->first);
            if (!defer_show(actor)) { set_recipe_fault(__func__, "defer_show_failed", nullptr, actor); return; }
            pending = pending_independent_shows.erase(pending);
        }
        for (auto pending = deferred_shows.begin(); pending != deferred_shows.end();) {
            const auto actor = find_entity(world, pending->second.name.c_str());
            const auto identifier = actor ? actor_identifier(actor) : 0;
            if (actor == pending->first && identifier == pending->second.identifier) ++pending;
            else pending = deferred_shows.erase(pending);
        }
        std::map<uintptr_t, bool> actors;
        std::map<std::string, std::vector<RecipeMembership>> memberships;
        std::set<uintptr_t> workers, owners;
        const std::vector<RecipeMembership> none;
        const auto scan_start=CostClock::now();
        const auto scanned=read_recipe_actors(world, actors, memberships, workers, owners,&cost.living);
        cost.scan=elapsed_us(scan_start);
        if (!scanned) { set_recipe_fault(__func__, "actors_unreadable"); return; }
        const auto now = GetTickCount64();
        if (now >= actor_diagnostic_next) {
            actor_diagnostic_next = now + 250;
            for (const auto& [actor, dead] : actors) if (!dead) trace_actor(actor, "large_jump", 0, true);
        }
        for (auto credit = recipe_actor_credits.begin(); credit != recipe_actor_credits.end();) {
            if (actors.find(credit->first) != actors.end()) { ++credit; continue; }
            trace_actor(credit->first, "absent_from_observation_cause_unobserved");
            actor_diagnostics.erase(credit->first);
            credit = recipe_actor_credits.erase(credit);
        }
        for (auto worker = worker_recipes.begin(); worker != worker_recipes.end();) {
            if (workers.find(worker->first) != workers.end()) { ++worker; continue; }
            const auto diagnostic = worker_diagnostics.find(worker->first);
            if (diagnostic != worker_diagnostics.end()) trace_recipe_worker("worker_removed", worker->first, diagnostic->second, worker->second);
            recipe_state.cancel(worker->second);
            if (diagnostic != worker_diagnostics.end()) {
                trace_recipe_worker("request_cancelled", worker->first, diagnostic->second, worker->second);
                worker_diagnostics.erase(diagnostic);
            }
            worker = worker_recipes.erase(worker);
        }
        for (auto pending = pending_recipes.begin(); pending != pending_recipes.end();) {
            if (owners.find(pending->first.first) != owners.end()) { ++pending; continue; }
            recipe_state.cancel(pending->second); pending = pending_recipes.erase(pending);
        }
        for (auto pending = component_summons.begin(); pending != component_summons.end();) {
            const auto parent = actors.find(pending->second.parent);
            if (parent != actors.end() && !parent->second && actor_identifier(pending->second.parent) == pending->second.identifier) {
                ++pending; continue;
            }
            for (const auto& request : pending->second.requests) recipe_state.cancel(request.key);
            complete_summon_requests(pending->second);
            pending = component_summons.erase(pending);
        }
        for (auto actor = recipe_actors.begin(); actor != recipe_actors.end();) {
            const auto present = actors.find(actor->first);
            const auto found = memberships.find(actor->second);
            if (found != memberships.end()) reconcile_recipe_memberships(actor->second, found->second);
            if (present == actors.end() || present->second) {
                trace_actor(actor->first, present == actors.end() ? "absent_from_observation_cause_unobserved" : "dead_observed");
                actor_diagnostics.erase(actor->first);
                recipe_state.actor_changed(actor->second, present == actors.end() ? RecipeActorState::removed : RecipeActorState::dead);
                if (present == actors.end()) recipe_actor_credits.erase(actor->first);
                actor = recipe_actors.erase(actor); continue;
            }
            if (found == memberships.end()) reconcile_recipe_memberships(actor->second, none);
            ++actor;
        }
    } catch (...) { set_recipe_fault(__func__, "exception"); }
}

void map_freed() {
    std::lock_guard<std::recursive_mutex> admission(admission_mutex);
    std::lock_guard<std::recursive_mutex> lock(quantity_mutex);
    darklord_clear();
    remaining_weights.clear();
    kill_progress.clear();
    health_progress.clear();
    recipe_library = {}; native_recipes.clear(); pending_recipes.clear(); worker_recipes.clear(); recipe_actors.clear();
    component_summons.clear(); direct_points.clear(); summon_children.clear(); pending_summon_checkpoint.reset();
    deferred_shows.clear();
    independent_shows.clear();
    pending_independent_shows.clear();
    independent_memberships_pending = false;
    recipe_actor_credits.clear();
    restored_actor_identities.clear();
    death_action_credits.clear();
    point_credits.clear();
    recipe_spawn_traces.clear();
    worker_diagnostics.clear(); worker_trace_count = 0;
    actor_diagnostics.clear(); actor_diagnostic_next = 0; registration_trace_count = 0;
    quantity_wait_traces.clear();
    remaining_wait_started.clear();
    recovery_rejections.clear();
    recipe_state = {}; recipe_stamp = 0; next_recipe_request = 0; native_string_table = 0; recipe_fault.store(false);
    AcquireSRWLockExclusive(&recipe_fault_lock);
    first_recipe_fault[0] = 0;
    ReleaseSRWLockExclusive(&recipe_fault_lock);
}

}

#ifdef SC_NATIVE_TESTING
namespace sentinel::expanded {
using namespace detail;
#include "../tests/expanded_stop.inc"
}
#endif
