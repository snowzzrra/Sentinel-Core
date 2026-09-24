// Included only by the native test build, inside sentinel::special.
struct alignas(8) HudTestClip {
        std::array<uint8_t, 0x100> bytes{};
        std::array<uint8_t, 0x88> context{};
        std::array<uint8_t, 0x40> transform{};
        bool visible = false;
        std::string text;
};
struct HudTestFixture {
        std::vector<std::unique_ptr<HudTestClip>> storage;
        std::map<uintptr_t, HudTestClip*> clips;
        std::map<std::pair<uintptr_t, std::string>, uintptr_t> children;
        std::array<uint8_t, 0x40> entry{};
        uintptr_t entry_source = 0;
        unsigned lookups = 0, clones = 0, frames = 0, visibility = 0, text_writes = 0;
        unsigned updates = 0, earnings = 0;
        uintptr_t earnings_owner = 0;

        template<class T> static void put(uintptr_t address, size_t offset, T value) {
            std::memcpy(reinterpret_cast<void*>(address + offset), &value, sizeof(value));
        }
        template<class T> static T get(uintptr_t address, size_t offset) {
            T value{};
            std::memcpy(&value, reinterpret_cast<void*>(address + offset), sizeof(value));
            return value;
        }
        uintptr_t make(uintptr_t parent, uintptr_t movie, float x = 0, float y = 0) {
            auto clip = std::make_unique<HudTestClip>();
            const auto address = reinterpret_cast<uintptr_t>(clip->bytes.data());
            put(address, 0x10, reinterpret_cast<uintptr_t>(clip->context.data()));
            put(reinterpret_cast<uintptr_t>(clip->context.data()), 0x80,
                reinterpret_cast<uintptr_t>(clip->transform.data()));
            put(reinterpret_cast<uintptr_t>(clip->transform.data()), 0x14, x);
            put(reinterpret_cast<uintptr_t>(clip->transform.data()), 0x18, y);
            put(address, 0x30, movie);
            put(address, 0x40, parent);
            put(address, 0x48, int32_t{1});
            clips[address] = clip.get();
            storage.push_back(std::move(clip));
            return address;
        }
        uintptr_t add(uintptr_t parent, uintptr_t movie, const char* name, float x = 0, float y = 0) {
            const auto clip = make(parent, movie, x, y);
            children[{parent, name}] = clip;
            return clip;
        }
        uintptr_t find(uintptr_t parent, const char* name) const {
            const auto it = children.find({parent, name});
            return it == children.end() ? 0 : it->second;
        }
        uintptr_t copy(uintptr_t source, uintptr_t parent, const char* name) {
            const auto movie = get<uintptr_t>(source, 0x30);
            const auto clone = add(parent, movie, name);
            const auto* original = clips.at(source);
            auto* target = clips.at(clone);
            target->transform = original->transform;
            put(clone, 0x58, get<uint16_t>(source, 0x58));
            ++clones;
            for (const auto& [key, child] : children)
                if (key.first == source) copy(child, clone, key.second.c_str());
            return clone;
        }
};
bool test_hud_owner_path() {
    HudTestFixture fixture;
    using Fixture = HudTestFixture;
    static Fixture* active = nullptr;
    static SnapshotFacts model_facts{};
    active = &fixture;
    const auto saved_swf = hud::swf;
    const auto saved_setup = original_hud_element_setup;
    const auto saved_update = original_weapon_hud_update;
    const auto saved_crucible = project_crucible_hud;
    const auto saved_hammer = project_hammer_hud;
    const auto saved_image_base = image_base;
    const auto saved_graphics = hud::graphics_ready, saved_keycaps = hud::keycap_ready;
    const auto saved_keys = configured_keys.load();
    image_base = 0x140000000;
    original_hud_element_setup = [](uintptr_t) -> char { return 1; };
    original_weapon_hud_update = [](uintptr_t, uintptr_t) { ++active->updates; };
    project_crucible_hud = [](uintptr_t) {};
    project_hammer_hud = [](uintptr_t) {};
    hud::swf.lookup = [](uintptr_t parent, hud::Value* value, const char* name) -> hud::Value* {
        ++active->lookups;
        value->payload = active->find(parent, name);
        return value;
    };
    hud::swf.sprite = [](const hud::Value* value) { return value->payload; };
    hud::swf.text = [](const hud::Value* value) { return value->payload; };
    hud::swf.release = [](hud::Value*) {};
    hud::swf.string_init = [](hud::String* value) { std::memset(value, 0, sizeof(*value)); };
    hud::swf.string_set = [](hud::String* value, const char* name) {
        std::snprintf(reinterpret_cast<char*>(value->storage), sizeof(value->storage), "%s", name);
    };
    hud::swf.string_free = [](hud::String*) {};
    hud::swf.duplicate = [](uintptr_t source, const hud::String* name) {
        return active->copy(source, Fixture::get<uintptr_t>(source, 0x40),
            reinterpret_cast<const char*>(name->storage));
    };
    hud::swf.entry = [](uintptr_t parent, int depth) {
        active->entry_source = 0;
        for (const auto& [address, clip] : active->clips)
            if (Fixture::get<uintptr_t>(address, 0x40) == parent &&
                Fixture::get<int32_t>(address, 0x48) == depth && active->find(address, "kbm")) {
                active->entry_source = address;
                break;
            }
        if (!active->entry_source) return uintptr_t{0};
        Fixture::put(reinterpret_cast<uintptr_t>(active->entry.data()), 0x30, active->entry_source);
        return reinterpret_cast<uintptr_t>(active->entry.data());
    };
    hud::swf.add = [](uintptr_t parent, int, unsigned, const hud::String* name) {
        const auto copy = active->copy(active->entry_source, parent,
            reinterpret_cast<const char*>(name->storage));
        Fixture::put(reinterpret_cast<uintptr_t>(active->entry.data()), 0x30, copy);
        return reinterpret_cast<uintptr_t>(active->entry.data());
    };
    hud::swf.dirty = [](uintptr_t) {};
    hud::swf.start = [](uintptr_t, int) {};
    hud::swf.frame = [](uintptr_t clip, int frame) {
        ++active->frames;
        Fixture::put(clip, 0x58, static_cast<uint16_t>(frame));
        Fixture::put(clip, 0x50, uint8_t{0});
        Fixture::put(clip, 0x5c, uint16_t{0});
    };
    hud::swf.visible = [](uintptr_t clip, bool shown, bool) {
        ++active->visibility; active->clips.at(clip)->visible = shown;
    };
    hud::swf.set_text = [](uintptr_t clip, const char* label) {
        ++active->text_writes; active->clips.at(clip)->text = label;
    };
    hud::swf.position = [](uintptr_t clip, float x, float y) {
        auto* target = active->clips.at(clip);
        Fixture::put(reinterpret_cast<uintptr_t>(target->transform.data()), 0x14, x);
        Fixture::put(reinterpret_cast<uintptr_t>(target->transform.data()), 0x18, y);
    };
    hud::swf.color = [](uintptr_t, int) {};
    hud::swf.material = [](uintptr_t clip, uintptr_t material, int, int, unsigned) {
        Fixture::put(clip, 0x60, material);
    };
    hud::swf.find_material = [](uintptr_t, const char*, int) -> uintptr_t { return 9; };
    hud::graphics_ready = true;
    hud::keycap_ready = false;

    struct Scene {
        alignas(8) std::array<uint8_t, 0x300> element{};
        alignas(8) std::array<uint8_t, 0x220> crucible{}, hammer{}, quickuse{}, secondary{};
        uintptr_t parent = 0, primary = 0, adjacent = 0;
        uintptr_t address() { return reinterpret_cast<uintptr_t>(element.data()); }
    } first{}, rebuilt{};
    const auto build = [&](Scene& scene, uintptr_t movie) {
        const auto e = scene.address();
        Fixture::put(e, 0, image_base + rva_weapon_info_vtable);
        Fixture::put(e, 0x209, uint8_t{1});
        Fixture::put(e, 0x170, int32_t{3});
        scene.parent = fixture.make(0, movie);
        scene.primary = fixture.make(scene.parent, movie, 100, 100);
        scene.adjacent = fixture.make(scene.parent, movie, 80, 100);
        const auto source = fixture.add(scene.parent, movie, "crucible_source");
        const auto other = fixture.add(scene.parent, movie, "hammer_source");
        for (const auto root : {source, other}) {
            const auto icon = fixture.add(root, movie, "icon");
            fixture.add(icon, movie, "cta");
            fixture.add(icon, movie, "iconStatic");
            const auto pips = fixture.add(root, movie, "pips");
            const auto three = fixture.add(pips, movie, "pips3");
            fixture.add(three, movie, "fill");
            fixture.add(three, movie, "innerFill");
        }
        const auto swap = fixture.add(scene.parent, movie, "swapEquipment");
        fixture.add(swap, movie, "cta");
        const auto swap_icon = fixture.add(swap, movie, "icon");
        fixture.add(swap_icon, movie, "cta");
        fixture.clips.at(fixture.add(scene.parent, movie, "vanilla_bind_v"))->text = "V";
        const auto donor = fixture.make(scene.primary, movie, 1, 1);
        const auto kbm = fixture.add(donor, movie, "kbm");
        fixture.add(kbm, movie, "txtVal");
        fixture.add(donor, movie, "joy");
        Fixture::put(reinterpret_cast<uintptr_t>(scene.crucible.data()), 0x18, source);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.hammer.data()), 0x18, other);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.quickuse.data()), 0x18, scene.primary);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.quickuse.data()), 0x1f0, donor);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.secondary.data()), 0x18, scene.adjacent);
        Fixture::put(e, 0x1e8, reinterpret_cast<uintptr_t>(scene.crucible.data()));
        Fixture::put(e, 0x1f8, reinterpret_cast<uintptr_t>(scene.hammer.data()));
        Fixture::put(e, 0x1d0, reinterpret_cast<uintptr_t>(scene.quickuse.data()));
        Fixture::put(e, 0x1d8, reinterpret_cast<uintptr_t>(scene.secondary.data()));
    };
    build(first, 1);
    build(rebuilt, 2);
    alignas(8) std::array<uint8_t, 0x300> challenge{};
    const auto a = reinterpret_cast<uintptr_t>(challenge.data());
    Fixture::put(a, 0, image_base + rva_mission_challenge_vtable);
    const auto b = first.address(), b2 = rebuilt.address();
    const bool old_guard_blocks_b = b != a && Fixture::get<uintptr_t>(a, 0x1e8) == 0;
    const auto update_on_hud = [&](uintptr_t target) {
        DWORD callback = 0;
        std::thread worker([&] {
            callback = GetCurrentThreadId();
            weapon_hud_update_detour(target, 0);
        });
        worker.join();
        return callback;
    };
    route_thread = GetCurrentThreadId();
    route_player.store(0);
    route_epoch.store(0);
    test_hud_player = 0;
    hud_element_setup_detour(a);
    const auto warmup_lookups = fixture.lookups;
    const auto warmup_thread = update_on_hud(b);
    const bool warmup_refused = warmup_thread != route_thread.load() &&
        fixture.lookups == warmup_lookups && !fixture.find(first.parent, "apAmmoRefill");
    test_hud_player = 42;
    route_player.store(42);
    native::test_observation_epoch(77);
    route_epoch.store(77);
    route_ready.store(true);
    configured_keys.store(VK_F9 | (VK_F10 << 8));
    char id[65]; std::memset(id, 'a', 64); id[64] = 0;
    std::memcpy(route_namespace, id, 65);
    reset_session(id);
    model_facts.native_crucible = model_facts.native_hammer = 1;
    model_facts.native_selected = SC_SPECIAL_WEAPON_CRUCIBLE;
    model_facts.selection_policy = true;
    model_facts.known = SC_SPECIAL_KNOWN_CRUCIBLE | SC_SPECIAL_KNOWN_HAMMER | SC_SPECIAL_KNOWN_SELECTION;
    Calls model{};
    model.player = [](void*) -> uintptr_t { return test_hud_player; };
    model.read = [](void*, uintptr_t, SnapshotFacts& facts) { facts = model_facts; return true; };
    model.present = present;
    sc_special_request request{};
    std::memcpy(request.namespace_id, id, 65);
    request.kind = SC_SPECIAL_OBSERVE;
    auto response = initial(request); execute(request, response, model);
    const auto publish = [&](uint32_t balance, uint32_t flags) {
        request.kind = SC_SPECIAL_REFILL_PUBLISH;
        request.refill_balance = balance;
        request.refill_flags = flags;
        auto result = initial(request); execute(request, result, model);
        return result.outcome == SC_SPECIAL_OUTCOME_OK;
    };
    constexpr uint32_t known = SC_SPECIAL_REFILL_CONNECTED | SC_SPECIAL_REFILL_AUTHORITATIVE |
        SC_SPECIAL_REFILL_BALANCE_KNOWN;
    bool ok = old_guard_blocks_b && warmup_refused &&
        response.outcome == SC_SPECIAL_OUTCOME_OK && publish(3, known);
    hud_element_setup_detour(a);
    ok &= challenge_element.load() == a;
    const auto before_invalid = fixture.lookups;
    weapon_hud_update_detour(a, 0);
    ok &= fixture.lookups == before_invalid && weapon_info_element.load() == 0;
    DWORD hud_thread = 0;
    std::thread first_hud_update([&] {
        hud_thread = GetCurrentThreadId();
        weapon_hud_update_detour(b, 0);
    });
    first_hud_update.join();
    if (!fixture.find(first.parent, "apAmmoRefill")) {
        const auto diagnostic = hud_trace.snapshot().stages[static_cast<size_t>(save::BStage::profile_output)];
        std::fprintf(stderr, "hud_owner worker=%lu publisher=%lu refusal=%lld\n",
            static_cast<unsigned long>(hud_thread), static_cast<unsigned long>(route_thread.load()),
            static_cast<long long>(diagnostic.facts[15].value));
    }
    ok &= hud_thread != route_thread.load() && fixture.find(first.parent, "apAmmoRefill") != 0;
    weapon_hud_update_detour(b, 0);
    const auto refill = fixture.find(first.parent, "apAmmoRefill");
    const auto arrow = fixture.find(first.parent, "apSpecialSwitch");
    const auto pips = fixture.find(refill, "pips");
    const auto three = fixture.find(pips, "pips3");
    ok &= refill && arrow && fixture.clips.at(refill)->visible &&
        Fixture::get<uint16_t>(three, 0x58) == 4 && fixture.find(first.parent, "apAmmoRefillBind") == 0;
    ok &= weapon_info_element.load() == b && challenge_element.load() == a;
    const auto clips_before = fixture.clones;
    hud::keycap_ready = true;
    update_on_hud(b);
    const auto refill_bind = fixture.find(first.parent, "apAmmoRefillBind");
    const auto toggle_bind = fixture.find(first.parent, "apSpecialToggleBind");
    ok &= refill_bind && toggle_bind && fixture.clones > clips_before;
    ok &= fixture.clips.at(fixture.find(fixture.find(refill_bind, "kbm"), "txtVal"))->text == "F9";
    ok &= fixture.clips.at(fixture.find(fixture.find(toggle_bind, "kbm"), "txtVal"))->text == "F10";
    const auto clones_stable = fixture.clones, frames_stable = fixture.frames;
    const auto text_stable = fixture.text_writes;
    std::atomic<int> next_worker{0};
    DWORD hud_thread_one = 0, hud_thread_two = 0;
    std::thread hud_one([&] {
        hud_thread_one = GetCurrentThreadId();
        weapon_hud_update_detour(b, 0);
        next_worker.store(1, std::memory_order_release);
        while (next_worker.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    });
    while (next_worker.load(std::memory_order_acquire) != 1) std::this_thread::yield();
    std::thread hud_two([&] {
        hud_thread_two = GetCurrentThreadId();
        weapon_hud_update_detour(b, 0);
        next_worker.store(2, std::memory_order_release);
    });
    hud_two.join();
    hud_one.join();
    ok &= hud_thread_one != hud_thread_two && hud_thread_one != route_thread.load() &&
        hud_thread_two != route_thread.load();
    ok &= fixture.clones == clones_stable && fixture.frames == frames_stable &&
        fixture.text_writes == text_stable;
    const auto refill_icon = fixture.find(fixture.find(refill, "icon"), "iconStatic");
    const auto arrow_cta = fixture.find(arrow, "cta");
    Fixture::put(refill, 0x5c, uint16_t{7});
    Fixture::put(pips, 0x58, uint16_t{2});
    Fixture::put(refill_icon, 0x60, uintptr_t{11});
    fixture.clips.at(arrow_cta)->visible = true;
    update_on_hud(b);
    ok &= Fixture::get<uint16_t>(refill, 0x5c) == 0 &&
        Fixture::get<uint16_t>(pips, 0x58) == 3 &&
        Fixture::get<uintptr_t>(refill_icon, 0x60) == 9 &&
        !fixture.clips.at(arrow_cta)->visible && fixture.clips.at(refill)->visible;
    for (uint32_t balance : {2u, 1u, 0u}) {
        ok &= publish(balance, known);
        update_on_hud(b);
        ok &= Fixture::get<uint16_t>(three, 0x58) == balance + 1;
    }
    ok &= publish(0, SC_SPECIAL_REFILL_CONNECTED);
    update_on_hud(b);
    ok &= !fixture.clips.at(pips)->visible && Fixture::get<uint16_t>(three, 0x58) == 1;
    configured_keys.store(VK_F8 | (VK_F10 << 8));
    const auto text_before = fixture.text_writes;
    update_on_hud(b);
    ok &= fixture.text_writes == text_before + 1;
    ok &= fixture.clips.at(fixture.find(fixture.find(toggle_bind, "kbm"), "txtVal"))->text == "F10";
    ok &= fixture.clips.at(fixture.find(first.parent, "vanilla_bind_v"))->text == "V";
    test_selection_read = [](uintptr_t, SnapshotFacts& facts) { facts = model_facts; return true; };
    test_earnings_append = [](uintptr_t owner, const char*, const char*, uint32_t, uint64_t, uint32_t) {
        ++active->earnings; active->earnings_owner = owner;
    };
    present_selection(42);
    ok &= fixture.earnings == 1 && fixture.earnings_owner == a && !present(nullptr, 42, 0, 0, 0);
    const auto invalid_lookups = fixture.lookups;
    test_hud_player = 43;
    update_on_hud(b);
    ok &= fixture.lookups == invalid_lookups;
    test_hud_player = 42;
    project_special_hud(b);
    ok &= fixture.lookups == invalid_lookups;
    route_namespace[0] = 'b';
    update_on_hud(b);
    ok &= fixture.lookups == invalid_lookups;
    route_namespace[0] = 'a';
    route_epoch.store(78);
    update_on_hud(b);
    ok &= fixture.lookups == invalid_lookups;
    route_epoch.store(77);
    Fixture::put(first.primary, 0x30, uintptr_t{3});
    update_on_hud(b);
    ok &= fixture.lookups == invalid_lookups;
    update_on_hud(b2);
    ok &= weapon_info_element.load() == b2 && fixture.find(rebuilt.parent, "apAmmoRefill") != 0 &&
        fixture.find(rebuilt.parent, "apAmmoRefillBind") != 0 &&
        fixture.find(first.parent, "apAmmoRefill") == refill;
    const auto rebuilt_clones = fixture.clones, rebuilt_frames = fixture.frames;
    update_on_hud(b2);
    ok &= fixture.clones == rebuilt_clones && fixture.frames == rebuilt_frames;
    hud_element_setup_detour(b);
    present_selection(42);
    ok &= challenge_element.load() == 0 && fixture.earnings == 1;
    const auto trace = hud_trace.snapshot();
    const auto& admission = trace.stages[static_cast<size_t>(save::BStage::profile_read)];
    const auto& clips = trace.stages[static_cast<size_t>(save::BStage::profile_output)];
    const auto& presentations = trace.stages[static_cast<size_t>(save::BStage::profile_capture)];
    ok &= std::strcmp(trace.first_failure.predicate, "hud_weapon_info_update_context") == 0 &&
        std::strcmp(trace.first_failure.facts[15].key, "refusal") == 0 &&
        trace.first_failure.facts[15].value == 1;
    ok &= admission.sequence && admission.facts[5].value != admission.facts[6].value &&
        admission.facts[12].key &&
        std::strcmp(admission.facts[12].key, "source_evaluated") == 0 &&
        admission.facts[12].value == 1;
    ok &= std::strcmp(clips.predicate, "hud_native_keycaps_applied") == 0 &&
        clips.status == save::BStatus::succeeded &&
        std::strcmp(clips.facts[13].key, "pixels_observed") == 0 && clips.facts[13].value == 0;
    ok &= std::strcmp(presentations.predicate, "hud_projection_clips_applied") == 0 &&
        presentations.status == save::BStatus::succeeded && presentations.facts[5].value >= 1 &&
        presentations.facts[11].value == 0;
    hud::swf = saved_swf;
    original_hud_element_setup = saved_setup;
    original_weapon_hud_update = saved_update;
    project_crucible_hud = saved_crucible;
    project_hammer_hud = saved_hammer;
    image_base = saved_image_base;
    hud::graphics_ready = saved_graphics;
    hud::keycap_ready = saved_keycaps;
    configured_keys.store(saved_keys);
    test_hud_player = 0;
    test_earnings_append = nullptr;
    test_selection_read = nullptr;
    active = nullptr;
    return ok;
}
