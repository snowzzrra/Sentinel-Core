// Included only by the native test build, inside sentinel::special.
struct alignas(8) HudTestClip {
        std::array<uint8_t, 0x100> bytes{};
        std::array<uint8_t, 0x88> context{};
        std::array<uint8_t, 0x40> transform{};
        hud::Rect local_bounds{};
        bool visible = false;
        std::string text;
};
struct HudTestFixture {
        std::vector<std::unique_ptr<HudTestClip>> storage;
        std::map<uintptr_t, HudTestClip*> clips;
        std::map<std::pair<uintptr_t, std::string>, uintptr_t> children;
        std::array<uint8_t, 0x40> entry{};
        uintptr_t entry_source = 0;
        unsigned lookups = 0, clones = 0, frames = 0, visibility = 0, positions = 0, text_writes = 0;
        unsigned materials = 0;
        uintptr_t last_material_clip = 0;
        std::string material_name;
        unsigned updates = 0, earnings = 0;
        uintptr_t earnings_owner = 0;
        bool unrendered_clones = false;

        template<class T> static void put(uintptr_t address, size_t offset, T value) {
            std::memcpy(reinterpret_cast<void*>(address + offset), &value, sizeof(value));
        }
        template<class T> static T get(uintptr_t address, size_t offset) {
            T value{};
            std::memcpy(&value, reinterpret_cast<void*>(address + offset), sizeof(value));
            return value;
        }
        void set_text(uintptr_t address, const char* value) {
            auto* clip = clips.at(address);
            clip->text = value;
            put(address, 0x48, clip->text.c_str());
            put(address, 0x50, static_cast<int32_t>(clip->text.size()));
            const auto parent = get<uintptr_t>(address, 0x40);
            if (parent && find(parent, "txtVal") == address) {
                const float width = 4.0f + 3.0f * static_cast<float>(clip->text.size());
                set_bounds(parent, width, 4, (width - 10.0f) * 0.5f, -22);
            }
        }
        void set_visible(uintptr_t address, bool visible) {
            clips.at(address)->visible = visible;
            put(address, 0x51, static_cast<uint8_t>(visible));
        }
        void set_bounds(uintptr_t address, float width, float height,
                        float dx = 0, float dy = 0) {
            clips.at(address)->local_bounds = {{dx - width * 0.5f, dy - height * 0.5f},
                                               {dx + width * 0.5f, dy + height * 0.5f}};
            for (const auto offset : {0x90u, 0x94u, 0x98u, 0x9cu, 0xa0u, 0xa4u})
                put(address, offset, offset < 0x98 ? 1.0f : 0.0f);
            put(address, 0xa8, dx - width * 0.5f);
            put(address, 0xac, dy - height * 0.5f);
            put(address, 0xb0, dx + width * 0.5f);
            put(address, 0xb4, dy + height * 0.5f);
        }
        void render(uintptr_t root) {
            for (const auto& [address, clip] : clips) {
                auto ancestor = address;
                while (ancestor && ancestor != root) ancestor = get<uintptr_t>(ancestor, 0x40);
                if (!ancestor) continue;
                const auto project = [&](hud::Point point) {
                    for (auto node = address; node; node = get<uintptr_t>(node, 0x40)) {
                        const auto t = reinterpret_cast<uintptr_t>(clips.at(node)->transform.data());
                        point = {get<float>(t, 4) * point.x + get<float>(t, 12) * point.y + get<float>(t, 20),
                                 get<float>(t, 16) * point.x + get<float>(t, 8) * point.y + get<float>(t, 24)};
                    }
                    return point;
                };
                const auto origin = project({0, 0}), x = project({1, 0}), y = project({0, 1});
                const float matrix[]{x.x - origin.x, y.y - origin.y, y.x - origin.x,
                                     x.y - origin.y, origin.x, origin.y};
                std::memcpy(reinterpret_cast<void*>(address + 0x90), matrix, sizeof(matrix));
                const auto rect = clip->local_bounds;
                auto lo = project(rect.tl), hi = lo;
                for (auto point : {project({rect.br.x, rect.tl.y}), project({rect.tl.x, rect.br.y}), project(rect.br)}) {
                    lo = {std::min(lo.x, point.x), std::min(lo.y, point.y)};
                    hi = {std::max(hi.x, point.x), std::max(hi.y, point.y)};
                }
                put(address, 0xa8, lo);
                put(address, 0xb0, hi);
            }
        }
        uintptr_t make(uintptr_t parent, uintptr_t movie, float x = 0, float y = 0) {
            auto clip = std::make_unique<HudTestClip>();
            const auto address = reinterpret_cast<uintptr_t>(clip->bytes.data());
            put(address, 0x10, reinterpret_cast<uintptr_t>(clip->context.data()));
            put(reinterpret_cast<uintptr_t>(clip->context.data()), 0x80,
                reinterpret_cast<uintptr_t>(clip->transform.data()));
            put(reinterpret_cast<uintptr_t>(clip->transform.data()), 0x4, 1.0f);
            put(reinterpret_cast<uintptr_t>(clip->transform.data()), 0x8, 1.0f);
            put(reinterpret_cast<uintptr_t>(clip->transform.data()), 0x14, x);
            put(reinterpret_cast<uintptr_t>(clip->transform.data()), 0x18, y);
            put(address, 0x30, movie);
            put(address, 0x40, parent);
            put(address, 0x48, int32_t{1});
            clips[address] = clip.get();
            storage.push_back(std::move(clip));
            set_bounds(address, 2, 2);
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
            target->local_bounds = original->local_bounds;
            put(clone, 0x58, get<uint16_t>(source, 0x58));
            put(clone, 0x50, get<uint8_t>(source, 0x50));
            put(clone, 0x5c, get<uint16_t>(source, 0x5c));
            for (const auto offset : {0x90u, 0x94u, 0x98u, 0x9cu, 0xa0u, 0xa4u, 0xa8u, 0xacu, 0xb0u, 0xb4u})
                put(clone, offset, get<float>(source, offset));
            set_visible(clone, original->visible);
            if (std::strcmp(name, "txtVal") == 0) set_text(clone, original->text.c_str());
            else target->text = original->text;
            ++clones;
            for (const auto& [key, child] : children)
                if (key.first == source) copy(child, clone, key.second.c_str());
            if (unrendered_clones) {
                put(clone, 0xa8, hud::Point{});
                put(clone, 0xb0, hud::Point{});
            }
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
                Fixture::get<int32_t>(address, 0x48) == depth) {
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
        ++active->visibility; active->set_visible(clip, shown);
    };
    hud::swf.set_text = [](uintptr_t clip, const char* label) {
        ++active->text_writes; active->set_text(clip, label);
    };
    hud::swf.position = [](uintptr_t clip, float x, float y) {
        ++active->positions;
        auto* target = active->clips.at(clip);
        Fixture::put(reinterpret_cast<uintptr_t>(target->transform.data()), 0x14, x);
        Fixture::put(reinterpret_cast<uintptr_t>(target->transform.data()), 0x18, y);
    };
    hud::swf.color = [](uintptr_t, int) {};
    hud::swf.material = [](uintptr_t clip, uintptr_t material, int, int, unsigned) {
        ++active->materials;
        active->last_material_clip = clip;
        Fixture::put(clip, 0x60, material);
        Fixture::put(clip, 0x68, uint16_t{16});
        Fixture::put(clip, 0x6a, uint16_t{16});
    };
    hud::swf.find_material = [](uintptr_t, const char* name, int) -> uintptr_t {
        active->material_name = name;
        return 9;
    };
    hud::graphics_ready = true;
    hud::keycap_ready = false;

    struct Scene {
        alignas(8) std::array<uint8_t, 0x300> element{};
        alignas(8) std::array<uint8_t, 0x220> crucible{}, hammer{}, quickuse{}, flame{}, bfg{};
        uintptr_t parent = 0, primary = 0, flame_root = 0, bfg_root = 0, ammo_panel = 0;
        uintptr_t address() { return reinterpret_cast<uintptr_t>(element.data()); }
    } first{}, rebuilt{};
    const auto build = [&](Scene& scene, uintptr_t movie) {
        const auto e = scene.address();
        Fixture::put(e, 0, image_base + rva_weapon_info_vtable);
        Fixture::put(e, 0x209, uint8_t{1});
        Fixture::put(e, 0x170, int32_t{3});
        scene.parent = fixture.make(0, movie);
        scene.primary = fixture.make(scene.parent, movie, 100, 100);
        scene.flame_root = fixture.make(scene.parent, movie, 80, 100);
        fixture.set_bounds(scene.primary, 16, 16);
        fixture.set_bounds(scene.flame_root, 16, 16);
        fixture.set_bounds(fixture.add(scene.flame_root, movie, "icon"), 12, 12);
        scene.bfg_root = fixture.make(scene.parent, movie);
        fixture.set_bounds(scene.bfg_root, 0, 0);
        scene.ammo_panel = fixture.make(scene.parent, movie, 100, 130);
        fixture.set_bounds(scene.ammo_panel, 100, 12);
        const auto source = fixture.add(scene.parent, movie, "crucible_source", 60, 100);
        const auto other = fixture.add(scene.parent, movie, "hammer_source", 60, 100);
        for (const auto root : {source, other}) {
            fixture.set_bounds(root, 16, 16);
            const auto icon = fixture.add(root, movie, "icon");
            fixture.set_bounds(icon, 12, 12);
            fixture.add(icon, movie, "cta");
            const auto static_icon = fixture.add(icon, movie, "iconStatic");
            Fixture::put(static_icon, 0x58, static_cast<uint16_t>(root == source ? 3 : 5));
            const auto small = fixture.add(icon, movie, "iconSmall");
            fixture.set_visible(small, true);
            const auto pips = fixture.add(root, movie, "pips");
            const auto three = fixture.add(pips, movie, "pips3");
            fixture.add(three, movie, "fill");
            fixture.add(three, movie, "innerFill");
        }
        const auto swap = fixture.add(scene.parent, movie, "swapEquipment", 120, 100);
        const auto equipped_weapon = fixture.add(scene.parent, movie, "equippedWeapon");
        const auto ammo_icon = fixture.add(fixture.add(equipped_weapon, movie, "ammoIcon"), movie, "image");
        Fixture::put(e, 0x258, equipped_weapon);
        Fixture::put(ammo_icon, 0x20, int32_t{-831019681});
        Fixture::put(ammo_icon, 0x60, uintptr_t{13});
        fixture.set_bounds(ammo_icon, 10, 10);
        const auto backer = fixture.add(swap, movie, "backer");
        const auto swap_cta = fixture.add(swap, movie, "cta");
        const auto swap_arrow = fixture.add(swap, movie, "arrow");
        fixture.set_bounds(swap_arrow, 6, 12);
        fixture.set_visible(backer, true);
        fixture.set_visible(swap_cta, true);
        fixture.set_visible(swap_arrow, true);
        const auto swap_icon = fixture.add(swap, movie, "icon");
        fixture.add(swap_icon, movie, "cta");
        fixture.clips.at(fixture.add(scene.parent, movie, "vanilla_bind_v"))->text = "V";
        const auto donor = fixture.make(scene.primary, movie, 1, 1);
        fixture.set_bounds(donor, 10, 4, 0, -22);
        const auto kbm = fixture.add(donor, movie, "kbm");
        fixture.set_text(fixture.add(kbm, movie, "txtVal"), "[BIND]");
        fixture.add(donor, movie, "joy");
        Fixture::put(reinterpret_cast<uintptr_t>(scene.crucible.data()), 0x18, source);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.hammer.data()), 0x18, other);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.quickuse.data()), 0x18, scene.primary);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.quickuse.data()), 0x1f0, donor);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.flame.data()), 0x18, scene.flame_root);
        Fixture::put(reinterpret_cast<uintptr_t>(scene.bfg.data()), 0x18, scene.bfg_root);
        Fixture::put(e, 0x1e8, reinterpret_cast<uintptr_t>(scene.crucible.data()));
        Fixture::put(e, 0x1f8, reinterpret_cast<uintptr_t>(scene.hammer.data()));
        Fixture::put(e, 0x1d0, reinterpret_cast<uintptr_t>(scene.quickuse.data()));
        Fixture::put(e, 0x1d8, reinterpret_cast<uintptr_t>(scene.flame.data()));
        Fixture::put(e, 0x1f0, reinterpret_cast<uintptr_t>(scene.bfg.data()));
    };
    build(first, 1);
    build(rebuilt, 2);
    const auto scaled_parent = fixture.make(0, 4);
    const auto scaled_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(scaled_parent)->transform.data());
    Fixture::put(scaled_transform, 0x4, 0.001f);
    Fixture::put(scaled_transform, 0x8, 0.001f);
    hud::Point scaled_stage{}, scaled_local{};
    const bool scaled_inverse = hud::affine_to(scaled_parent, 0, 4, {10, 20}, scaled_stage) &&
        hud::from_space(scaled_parent, 0, 4, scaled_stage, scaled_local) &&
        std::fabs(scaled_local.x - 10) < 0.001f &&
        std::fabs(scaled_local.y - 20) < 0.001f;
    const auto local_parent = fixture.make(0, 5);
    const auto local_root = fixture.make(local_parent, 5, 2700, 1150);
    fixture.set_bounds(local_root, 147.8618f, 120.246f);
    const auto local_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(local_root)->transform.data());
    Fixture::put(local_transform, 0x4, 0.001f);
    Fixture::put(local_transform, 0x8, 0.001f);
    hud::NativePosition local_saved{};
    hud::Point local_center{};
    hud::Rect local_stage_bounds{};
    const bool local_projection = hud::remember_native(local_saved, local_root, 5, 1) &&
        hud::native_center(local_saved, 0, local_center) &&
        hud::bounds(local_root, 0, local_stage_bounds) &&
        std::fabs(local_center.x - 2700) < 0.01f &&
        std::fabs(local_center.y - 1150) < 0.01f &&
        std::fabs(hud::center(local_stage_bounds).x - 2700) < 0.01f;
    const auto first_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(first.primary)->transform.data());
    Fixture::put(first_transform, 0x4, 2.0f);
    Fixture::put(first_transform, 0x8, 1.5f);
    Fixture::put(first_transform, 0xc, 0.5f);
    Fixture::put(first_transform, 0x10, 0.25f);
    const auto first_swap = fixture.find(first.parent, "swapEquipment");
    const auto swap_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(first_swap)->transform.data());
    Fixture::put(swap_transform, 0x4, 0.8f);
    Fixture::put(swap_transform, 0x8, 1.1f);
    Fixture::put(swap_transform, 0xc, 0.12f);
    Fixture::put(swap_transform, 0x10, -0.07f);
    fixture.set_bounds(fixture.find(first_swap, "arrow"), 6, 12);
    const auto first_donor_bounds = Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(first.quickuse.data()), 0x1f0);
    fixture.set_bounds(first_donor_bounds, 10, 4, 0, -22);
    const auto rebuilt_parent_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(rebuilt.parent)->transform.data());
    Fixture::put(rebuilt_parent_transform, 0x4, 1.2f);
    Fixture::put(rebuilt_parent_transform, 0x8, 0.8f);
    Fixture::put(rebuilt_parent_transform, 0xc, 0.2f);
    Fixture::put(rebuilt_parent_transform, 0x10, 0.1f);
    for (const auto root : {rebuilt.primary, rebuilt.flame_root,
            Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(rebuilt.crucible.data()), 0x18),
            Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(rebuilt.hammer.data()), 0x18)})
        fixture.set_bounds(root, 16, 16);
    const auto rebuilt_swap = fixture.find(rebuilt.parent, "swapEquipment");
    fixture.set_bounds(fixture.find(rebuilt_swap, "arrow"), 6, 12);
    fixture.set_bounds(Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(rebuilt.quickuse.data()), 0x1f0),
                       10, 4, 0, -22);
    hud::Point affine_offset{};
    const auto first_donor = Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(first.quickuse.data()), 0x1f0);
    const bool affine_ok = hud::key_offset(first_donor, first.primary, 1, affine_offset) &&
        affine_offset.x == 2.5f && affine_offset.y == 1.75f;
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
    const auto observe_selected = [&](uint32_t selected) {
        model_facts.native_selected = static_cast<uint8_t>(selected);
        request.kind = SC_SPECIAL_OBSERVE;
        auto result = initial(request); execute(request, result, model);
        return result.outcome == SC_SPECIAL_OUTCOME_OK;
    };
    constexpr uint32_t known = SC_SPECIAL_REFILL_CONNECTED | SC_SPECIAL_REFILL_AUTHORITATIVE |
        SC_SPECIAL_REFILL_BALANCE_KNOWN;
    hud::Rect bfg_bounds{};
    bool ok = affine_ok && scaled_inverse && local_projection &&
         !hud::raw_bounds(first.bfg_root, bfg_bounds) &&
         old_guard_blocks_b && warmup_refused &&
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
    const auto ammo_glyph = fixture.find(first.parent, "apAmmoGlyph");
    const auto arrow = fixture.find(first.parent, "apSpecialSwitch");
    const auto pips = fixture.find(refill, "pips");
    const auto three = fixture.find(pips, "pips3");
    ok &= refill && ammo_glyph && arrow && fixture.clips.at(refill)->visible &&
        fixture.clips.at(ammo_glyph)->visible &&
        Fixture::get<uint16_t>(three, 0x58) == 4 &&
        Fixture::get<uint16_t>(refill, 0x58) == 1 &&
        Fixture::get<uintptr_t>(ammo_glyph, 0x60) == 9 &&
        fixture.last_material_clip == ammo_glyph &&
        fixture.material_name == "art/ui/icons/ammo/bullets" &&
        fixture.find(first.parent, "apAmmoRefillBind") == 0;
    const auto stage_center = [&](uintptr_t clip) {
        hud::Rect rect{};
        hud::bounds(clip, 0, rect);
        return hud::center(rect);
    };
    const auto close_to = [](float a, float b) { return std::fabs(a - b) < 0.02f; };
    const auto crucible_source = Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(first.crucible.data()), 0x18);
    const auto hammer_source = Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(first.hammer.data()), 0x18);
    const auto arrow_leaf = fixture.find(arrow, "arrow");
    const auto arrow_backer = fixture.find(arrow, "backer");
    const auto native_arrow = fixture.find(first_swap, "arrow");
    ok &= close_to(stage_center(crucible_source).x, 59.76f) &&
        close_to(stage_center(hammer_source).x, 59.76f) &&
        close_to(stage_center(arrow_leaf).x, 69.88f) &&
        close_to(stage_center(first.flame_root).x, 80) &&
        close_to(stage_center(first.primary).x, 100) &&
        close_to(stage_center(native_arrow).x, 120) &&
        close_to(stage_center(refill).x, 130.12f) &&
        close_to(stage_center(ammo_glyph).x, 130.12f) &&
        !fixture.clips.at(arrow_backer)->visible &&
        fixture.clips.at(arrow_leaf)->visible && fixture.clips.at(native_arrow)->visible;
    fixture.set_bounds(hammer_source, 0, 16);
    update_on_hud(b);
    ok &= fixture.clips.at(arrow)->visible &&
        close_to(stage_center(crucible_source).x, 59.76f) &&
        close_to(stage_center(arrow_leaf).x, 69.88f);
    fixture.set_bounds(hammer_source, 16, 16);
    update_on_hud(b);
    ok &= close_to(stage_center(hammer_source).x, 59.76f);
    ok &= weapon_info_element.load() == b && challenge_element.load() == a;
    const auto clips_before = fixture.clones;
    hud::keycap_ready = true;
    update_on_hud(b);
    const auto refill_bind = fixture.find(first.parent, "apAmmoRefillBind");
    const auto toggle_bind = fixture.find(first.parent, "apSpecialToggleBind");
    ok &= refill_bind && toggle_bind && fixture.clones > clips_before;
    ok &= fixture.clips.at(fixture.find(fixture.find(refill_bind, "kbm"), "txtVal"))->text == "F9";
    ok &= fixture.clips.at(fixture.find(fixture.find(toggle_bind, "kbm"), "txtVal"))->text == "F10";
    const auto f9_kbm = fixture.find(refill_bind, "kbm");
    const auto f10_kbm = fixture.find(toggle_bind, "kbm");
    ok &= close_to(stage_center(f9_kbm).x, stage_center(refill).x) &&
        close_to(stage_center(f10_kbm).x, stage_center(arrow_leaf).x) &&
        stage_center(f9_kbm).y < stage_center(refill).y &&
        stage_center(f10_kbm).y < stage_center(arrow_leaf).y;
    hud::Rect special_rect{}, ap_arrow_rect{}, f10_rect{}, flame_rect{}, ammo_rect{}, refill_rect{},
              native_arrow_rect{}, f9_rect{}, refill_icon_rect{}, donor_key_rect{};
    hud::bounds(fixture.find(crucible_source, "icon"), 0, special_rect);
    hud::bounds(arrow_leaf, 0, ap_arrow_rect);
    hud::bounds(f10_kbm, 0, f10_rect);
    hud::bounds(fixture.find(first.flame_root, "icon"), 0, flame_rect);
    hud::bounds(first.ammo_panel, 0, ammo_rect);
    hud::bounds(refill, 0, refill_rect);
    hud::bounds(native_arrow, 0, native_arrow_rect);
    hud::bounds(f9_kbm, 0, f9_rect);
    hud::bounds(fixture.find(refill, "icon"), 0, refill_icon_rect);
    hud::bounds(fixture.find(first_donor, "kbm"), 0, donor_key_rect);
    ok &= special_rect.br.x < ap_arrow_rect.tl.x &&
        ap_arrow_rect.br.x < flame_rect.tl.x &&
        f10_rect.br.y < flame_rect.tl.y &&
        f10_rect.br.y < ammo_rect.tl.y &&
        native_arrow_rect.br.x < refill_icon_rect.tl.x &&
        close_to(refill_icon_rect.tl.x - native_arrow_rect.br.x, 1.0f) &&
        close_to(f9_rect.tl.y, donor_key_rect.tl.y) &&
        close_to(f10_rect.tl.y, donor_key_rect.tl.y) &&
        close_to(f10_rect.br.x - f10_rect.tl.x, 28.0f) &&
        close_to(f9_rect.br.x - f9_rect.tl.x, 22.0f);
    const auto f9_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(refill_bind)->transform.data());
    const auto f10_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(toggle_bind)->transform.data());
    const auto arrow_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(arrow)->transform.data());
    ok &= close_to(Fixture::get<float>(f9_transform, 0x4), 2.0f) &&
        close_to(Fixture::get<float>(f9_transform, 0x8), 1.5f) &&
        close_to(Fixture::get<float>(f9_transform, 0xc), 0.5f) &&
        close_to(Fixture::get<float>(f9_transform, 0x10), 0.25f) &&
        close_to(Fixture::get<float>(f10_transform, 0x4), 2.0f) &&
        close_to(Fixture::get<float>(f10_transform, 0xc), 0.5f) &&
        close_to(Fixture::get<float>(arrow_transform, 0x4), 0.8f) &&
        close_to(Fixture::get<float>(arrow_transform, 0x8), 1.1f) &&
        close_to(Fixture::get<float>(arrow_transform, 0xc), 0.12f) &&
        close_to(Fixture::get<float>(arrow_transform, 0x10), -0.07f);
    const auto clones_stable = fixture.clones, frames_stable = fixture.frames;
    const auto visibility_stable = fixture.visibility, positions_stable = fixture.positions;
    const auto text_stable = fixture.text_writes, material_stable = fixture.materials;
    const auto layout_sequence = hud_trace.snapshot().stages[
        static_cast<size_t>(save::BStage::profile_prepare)].sequence;
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
        fixture.visibility == visibility_stable && fixture.positions == positions_stable &&
        fixture.text_writes == text_stable && fixture.materials == material_stable &&
        close_to(stage_center(crucible_source).x, 59.76f) && close_to(stage_center(hammer_source).x, 59.76f) &&
        hud_trace.snapshot().stages[
            static_cast<size_t>(save::BStage::profile_prepare)].sequence == layout_sequence;
    fixture.set_bounds(crucible_source, 0, 16);
    ok &= observe_selected(SC_SPECIAL_WEAPON_HAMMER);
    update_on_hud(b);
    ok &= fixture.clips.at(arrow)->visible && fixture.clips.at(toggle_bind)->visible &&
        close_to(stage_center(arrow_leaf).x, 69.88f) &&
        close_to(stage_center(fixture.find(toggle_bind, "kbm")).x, 69.88f) &&
        close_to(stage_center(hammer_source).x, 59.76f) &&
        Fixture::get<uintptr_t>(ammo_glyph, 0x60) == 9;
    fixture.set_bounds(crucible_source, 16, 16);
    ok &= observe_selected(SC_SPECIAL_WEAPON_CRUCIBLE);
    update_on_hud(b);
    ok &= close_to(stage_center(crucible_source).x, 59.76f) &&
        close_to(stage_center(arrow_leaf).x, 69.88f) &&
        Fixture::get<uintptr_t>(ammo_glyph, 0x60) == 9;
    fixture.set_bounds(crucible_source, 0, 16);
    update_on_hud(b);
    const auto missing_active_layout = hud_trace.snapshot().stages[
        static_cast<size_t>(save::BStage::profile_prepare)];
    ok &= !fixture.clips.at(arrow)->visible && !fixture.clips.at(toggle_bind)->visible;
    ok &= std::strcmp(missing_active_layout.predicate,
        "switch_active_special_bounds_missing") == 0;
    fixture.set_bounds(crucible_source, 16, 16);
    update_on_hud(b);
    ok &= fixture.clips.at(arrow)->visible && fixture.clips.at(toggle_bind)->visible;
    const auto donor_kbm = fixture.find(first_donor, "kbm");
    fixture.set_bounds(donor_kbm, 0, 4);
    update_on_hud(b);
    ok &= !fixture.clips.at(refill_bind)->visible && !fixture.clips.at(toggle_bind)->visible;
    fixture.set_bounds(donor_kbm, 22, 4, 6, -22);
    update_on_hud(b);
    ok &= fixture.clips.at(refill_bind)->visible && fixture.clips.at(toggle_bind)->visible;
    const auto refill_icon = fixture.find(fixture.find(refill, "icon"), "iconStatic");
    const auto arrow_cta = fixture.find(arrow, "cta");
    const auto nested_arrow_cta = fixture.find(fixture.find(arrow, "icon"), "cta");
    Fixture::put(refill, 0x5c, uint16_t{7});
    Fixture::put(pips, 0x58, uint16_t{2});
    Fixture::put(three, 0x5c, uint16_t{8});
    Fixture::put(arrow, 0x50, uint8_t{1});
    Fixture::put(f9_kbm, 0x50, uint8_t{1});
    Fixture::put(ammo_glyph, 0x60, uintptr_t{11});
    Fixture::put(refill_icon, 0x58, uint16_t{7});
    const auto refill_small = fixture.find(fixture.find(refill, "icon"), "iconSmall");
    fixture.set_visible(refill_small, true);
    fixture.set_visible(arrow_cta, true);
    fixture.set_visible(nested_arrow_cta, true);
    update_on_hud(b);
    ok &= Fixture::get<uint16_t>(refill, 0x5c) == 0 &&
        Fixture::get<uint16_t>(pips, 0x58) == 3 &&
        Fixture::get<uint16_t>(three, 0x5c) == 0 &&
        Fixture::get<uint8_t>(arrow, 0x50) == 0 &&
        Fixture::get<uint8_t>(f9_kbm, 0x50) == 0 &&
        Fixture::get<uintptr_t>(ammo_glyph, 0x60) == 9 &&
        Fixture::get<uint16_t>(refill_icon, 0x58) == 7 &&
        !fixture.clips.at(refill_icon)->visible &&
        !fixture.clips.at(refill_small)->visible &&
        !fixture.clips.at(arrow_cta)->visible &&
        !fixture.clips.at(nested_arrow_cta)->visible && fixture.clips.at(refill)->visible;
    const auto clones_before_text_mutation = fixture.clones;
    const auto f9_replacement = fixture.add(f9_kbm, 1, "txtVal");
    const auto f10_replacement = fixture.add(f10_kbm, 1, "txtVal");
    fixture.set_text(f9_replacement, "[BIND]");
    fixture.set_text(f10_replacement, "[BIND]");
    update_on_hud(b);
    ok &= fixture.clips.at(f9_replacement)->text == "F9" &&
        fixture.clips.at(f10_replacement)->text == "F10" &&
        fixture.clones == clones_before_text_mutation;
    fixture.set_text(f9_replacement, "[BIND]");
    fixture.set_text(f10_replacement, "[BIND]");
    update_on_hud(b);
    ok &= fixture.clips.at(f9_replacement)->text == "F9" &&
        fixture.clips.at(f10_replacement)->text == "F10";
    const auto stable_text_writes = fixture.text_writes;
    update_on_hud(b);
    ok &= fixture.text_writes == stable_text_writes;
    configured_keys.store(VK_F10 << 8);
    update_on_hud(b);
    ok &= !fixture.clips.at(refill_bind)->visible && fixture.clips.at(toggle_bind)->visible;
    configured_keys.store(VK_F9);
    update_on_hud(b);
    ok &= fixture.clips.at(refill_bind)->visible && !fixture.clips.at(toggle_bind)->visible &&
        !fixture.clips.at(arrow)->visible;
    configured_keys.store(VK_F9 | (VK_F10 << 8));
    update_on_hud(b);
    ok &= fixture.clips.at(refill_bind)->visible && fixture.clips.at(toggle_bind)->visible;
    hud::keycap_ready = false;
    update_on_hud(b);
    ok &= !fixture.clips.at(refill_bind)->visible && !fixture.clips.at(toggle_bind)->visible;
    hud::keycap_ready = true;
    update_on_hud(b);
    ok &= fixture.clips.at(refill_bind)->visible && fixture.clips.at(toggle_bind)->visible;
    for (uint32_t balance : {2u, 1u, 0u}) {
        ok &= publish(balance, known);
        update_on_hud(b);
        ok &= Fixture::get<uint16_t>(three, 0x58) == balance + 1 &&
            Fixture::get<uint16_t>(refill, 0x58) == (balance ? 1 : 2) &&
            fixture.clips.at(pips)->visible;
    }
    ok &= publish(0, SC_SPECIAL_REFILL_CONNECTED);
    update_on_hud(b);
    ok &= !fixture.clips.at(pips)->visible && Fixture::get<uint16_t>(refill, 0x58) == 2;
    configured_keys.store(VK_F8 | (VK_F10 << 8));
    const auto text_before = fixture.text_writes;
    update_on_hud(b);
    ok &= fixture.text_writes == text_before + 1;
    ok &= fixture.clips.at(fixture.find(fixture.find(toggle_bind, "kbm"), "txtVal"))->text == "F10";
    ok &= fixture.clips.at(fixture.find(first.parent, "vanilla_bind_v"))->text == "V";
    const auto refill_icon_parent = fixture.find(refill, "icon");
    const auto clones_before_partial = fixture.clones;
    fixture.children.erase({refill_icon_parent, "iconStatic"});
    update_on_hud(b);
    ok &= fixture.clips.at(refill)->visible && fixture.clips.at(refill_bind)->visible &&
        fixture.clips.at(ammo_glyph)->visible;
    fixture.children[{refill_icon_parent, "iconStatic"}] = refill_icon;
    update_on_hud(b);
    ok &= fixture.clips.at(refill)->visible && fixture.clips.at(refill_bind)->visible &&
        fixture.clones == clones_before_partial;
    const auto crucible_icon = fixture.find(crucible_source, "icon");
    const auto hammer_icon = fixture.find(hammer_source, "icon");
    fixture.children.erase({crucible_source, "icon"});
    fixture.children.erase({hammer_source, "icon"});
    update_on_hud(b);
    ok &= !fixture.clips.at(refill)->visible && !fixture.clips.at(arrow)->visible &&
        !fixture.clips.at(refill_bind)->visible && !fixture.clips.at(toggle_bind)->visible &&
        close_to(stage_center(crucible_source).x, 60) && close_to(stage_center(hammer_source).x, 60);
    fixture.children[{crucible_source, "icon"}] = crucible_icon;
    fixture.children[{hammer_source, "icon"}] = hammer_icon;
    update_on_hud(b);
    ok &= fixture.clips.at(refill)->visible && fixture.clips.at(arrow)->visible &&
        close_to(stage_center(crucible_source).x, 59.76f) && close_to(stage_center(hammer_source).x, 59.76f);
    Fixture::put(b, 0x209, uint8_t{0});
    update_on_hud(b);
    fixture.set_visible(refill, true);
    update_on_hud(b);
    ok &= !fixture.clips.at(refill)->visible && !fixture.clips.at(refill_bind)->visible &&
        close_to(stage_center(crucible_source).x, 60) && close_to(stage_center(hammer_source).x, 60);
    Fixture::put(b, 0x209, uint8_t{1});
    update_on_hud(b);
    ok &= fixture.clips.at(refill)->visible && fixture.clips.at(refill_bind)->visible &&
        close_to(stage_center(crucible_source).x, 59.76f) && close_to(stage_center(hammer_source).x, 59.76f);
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
    Scene missing{};
    build(missing, 3);
    fixture.children.erase({missing.parent, "swapEquipment"});
    update_on_hud(missing.address());
    const auto missing_output = hud_trace.snapshot().stages[static_cast<size_t>(save::BStage::profile_output)];
    ok &= std::strcmp(missing_output.predicate, "switch_source_missing") == 0 &&
        fixture.find(missing.parent, "apAmmoRefillBind") == 0 &&
        fixture.find(missing.parent, "apSpecialSwitch") == 0 &&
        fixture.find(missing.parent, "apSpecialToggleBind") == 0;
    update_on_hud(b2);
    const auto rebuilt_crucible = Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(rebuilt.crucible.data()), 0x18);
    const auto rebuilt_hammer = Fixture::get<uintptr_t>(reinterpret_cast<uintptr_t>(rebuilt.hammer.data()), 0x18);
    const auto rebuilt_arrow = fixture.find(rebuilt.parent, "apSpecialSwitch");
    const auto rebuilt_refill = fixture.find(rebuilt.parent, "apAmmoRefill");
    ok &= weapon_info_element.load() == b2 && fixture.find(rebuilt.parent, "apAmmoRefill") != 0 &&
        fixture.find(rebuilt.parent, "apAmmoRefillBind") != 0 &&
        fixture.find(first.parent, "apAmmoRefill") == refill &&
        close_to(stage_center(rebuilt_crucible).x, 92.0f) &&
        close_to(stage_center(rebuilt_hammer).x, 92.0f) &&
        close_to(stage_center(fixture.find(rebuilt_arrow, "arrow")).x, 104.0f) &&
        close_to(stage_center(rebuilt.flame_root).x, 116) &&
        close_to(stage_center(rebuilt.primary).x, 140) &&
        close_to(stage_center(fixture.find(rebuilt_swap, "arrow")).x, 164) &&
        close_to(stage_center(rebuilt_refill).x, 176.0f);
    const auto rebuilt_clones = fixture.clones, rebuilt_frames = fixture.frames;
    update_on_hud(b2);
    ok &= fixture.clones == rebuilt_clones && fixture.frames == rebuilt_frames;
    ok &= publish(2, known) && create_refill_request(GetTickCount64());
    update_on_hud(b2);
    const auto pending_refill = fixture.find(rebuilt.parent, "apAmmoRefill");
    const auto pending_pips = fixture.find(pending_refill, "pips");
    const auto pending_three = fixture.find(pending_pips, "pips3");
    ok &= Fixture::get<uint16_t>(pending_refill, 0x58) == 2 &&
        Fixture::get<uint16_t>(pending_three, 0x58) == 3 &&
        fixture.clips.at(pending_pips)->visible;
    Scene foreign{};
    build(foreign, 6);
    const auto foreign_transform = reinterpret_cast<uintptr_t>(fixture.clips.at(foreign.parent)->transform.data());
    Fixture::put(foreign_transform, 0x4, 2.0f);
    Fixture::put(foreign_transform, 0x8, 1.5f);
    Fixture::put(foreign_transform, 0x14, 1600.0f);
    Fixture::put(foreign_transform, 0x18, 500.0f);
    Fixture::put(reinterpret_cast<uintptr_t>(fixture.clips.at(foreign.primary)->transform.data()),
                 0x14, 94.0f); // Native adjacent equipment bounds can overlap.
    const auto foreign_crucible = fixture.find(foreign.parent, "crucible_source");
    const auto foreign_hammer = fixture.find(foreign.parent, "hammer_source");
    const auto foreign_swap = fixture.find(foreign.parent, "swapEquipment");
    Fixture::put(reinterpret_cast<uintptr_t>(fixture.clips.at(foreign_swap)->transform.data()),
                 0x18, 106.0f);
    fixture.set_bounds(foreign_hammer, 18, 16, 3, 0);
    fixture.render(foreign.parent);
    const auto foreign_ancestor = fixture.make(0, 7);
    Fixture::put(foreign.parent, 0x40, foreign_ancestor);
    const auto foreign_native_arrow = fixture.find(foreign_swap, "arrow");
    hud::Rect foreign_raw{}, foreign_arrow_bounds{}, foreign_refill_bounds{};
    hud::Point rejected_stage{};
    ok &= hud::raw_bounds(foreign_native_arrow, foreign_raw) &&
        !hud::affine_to(foreign_native_arrow, 0, 6, {0, 0}, rejected_stage) &&
        hud::bounds(foreign_native_arrow, foreign.parent, foreign_arrow_bounds);
    configured_keys.store(VK_F9 | (VK_F10 << 8));
    fixture.unrendered_clones = true;
    update_on_hud(foreign.address());
    fixture.render(foreign.parent);
    update_on_hud(foreign.address());
    const auto foreign_refill = fixture.find(foreign.parent, "apAmmoRefill");
    const auto foreign_arrow = fixture.find(foreign.parent, "apSpecialSwitch");
    const auto foreign_f9 = fixture.find(foreign.parent, "apAmmoRefillBind");
    const auto foreign_f10 = fixture.find(foreign.parent, "apSpecialToggleBind");
    const auto foreign_refill_icon = fixture.find(foreign_refill, "icon");
    ok &= foreign_refill && foreign_arrow && foreign_f9 && foreign_f10 &&
        fixture.clips.at(foreign_refill)->visible && fixture.clips.at(foreign_arrow)->visible &&
        fixture.clips.at(foreign_f9)->visible && fixture.clips.at(foreign_f10)->visible &&
        hud::bounds(foreign_refill_icon, foreign.parent, foreign_refill_bounds) &&
        close_to(foreign_refill_bounds.tl.x - foreign_arrow_bounds.br.x, 1.0f) &&
        fixture.clips.at(fixture.find(fixture.find(foreign_f9, "kbm"), "txtVal"))->text == "F9" &&
        fixture.clips.at(fixture.find(fixture.find(foreign_f10, "kbm"), "txtVal"))->text == "F10";
    const auto rendered_center = [&](uintptr_t clip) {
        const auto lo = Fixture::get<hud::Point>(clip, 0xa8), hi = Fixture::get<hud::Point>(clip, 0xb0);
        return hud::Point{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
    };
    bool retail_cache_ok = true;
    const auto foreign_glyph = fixture.find(foreign.parent, "apAmmoGlyph");
    hud::Rect glyph_before{};
    fixture.render(foreign.parent);
    ok &= hud::raw_bounds(foreign_glyph, glyph_before);
    fixture.set_bounds(foreign_crucible, 28, 16);
    fixture.set_bounds(foreign_hammer, 28, 16, 3, 0);
    fixture.set_bounds(foreign.flame_root, 28, 16);
    fixture.render(foreign.parent);
    for (unsigned toggle = 0; toggle < 6; ++toggle) {
        if (toggle == 3) {
            fixture.set_bounds(foreign_hammer, 28, 16, -2, 0);
            fixture.render(foreign.parent);
        }
        retail_cache_ok &= observe_selected(toggle % 2 ? SC_SPECIAL_WEAPON_CRUCIBLE : SC_SPECIAL_WEAPON_HAMMER);
        update_on_hud(foreign.address()); // Cache still describes the preceding render.
        fixture.render(foreign.parent);
        update_on_hud(foreign.address());
        fixture.render(foreign.parent);
        const auto selected = toggle % 2 ? foreign_crucible : foreign_hammer;
        hud::Rect glyph_after{};
        retail_cache_ok &= hud::raw_bounds(foreign_glyph, glyph_after) &&
            close_to(glyph_after.br.x - glyph_after.tl.x, glyph_before.br.x - glyph_before.tl.x) &&
            close_to(glyph_after.br.y - glyph_after.tl.y, glyph_before.br.y - glyph_before.tl.y) &&
            Fixture::get<uintptr_t>(foreign_glyph, 0x60) == 9;
        retail_cache_ok &= close_to(rendered_center(fixture.find(selected, "icon")).x, 1720) &&
            close_to(rendered_center(fixture.find(selected, "icon")).y, 650) &&
            close_to(rendered_center(fixture.find(foreign_arrow, "arrow")).x, 1740) &&
            close_to(rendered_center(foreign_refill).x, 1860) &&
            close_to(rendered_center(fixture.find(foreign_f9, "kbm")).x, 1860) &&
            close_to(rendered_center(fixture.find(foreign_f10, "kbm")).x, 1740) &&
            close_to(rendered_center(fixture.find(foreign_f9, "kbm")).y,
                     rendered_center(fixture.find(foreign_f10, "kbm")).y) &&
            fixture.clips.at(foreign_refill)->visible && fixture.clips.at(foreign_f9)->visible &&
            fixture.clips.at(foreign_f10)->visible &&
            fixture.clips.at(fixture.find(foreign.parent, "apAmmoGlyph"))->visible;
        const auto rendered_bounds = [&](uintptr_t clip) {
            hud::Rect rect{};
            retail_cache_ok &= hud::raw_bounds(clip, rect);
            return rect;
        };
        const auto special_icon_rect = rendered_bounds(fixture.find(selected, "icon"));
        const auto arrow_icon_rect = rendered_bounds(fixture.find(foreign_arrow, "arrow"));
        const auto flame_icon_rect = rendered_bounds(fixture.find(foreign.flame_root, "icon"));
        const auto native_arrow_icon_rect = rendered_bounds(foreign_native_arrow);
        const auto rendered_refill_icon_rect = rendered_bounds(foreign_refill_icon);
        const auto refill_root_rect = rendered_bounds(foreign_refill);
        const auto ammo_glyph_rect = rendered_bounds(foreign_glyph);
        const auto f9_key_rect = rendered_bounds(fixture.find(foreign_f9, "kbm"));
        const auto f10_key_rect = rendered_bounds(fixture.find(foreign_f10, "kbm"));
        const auto donor_key = Fixture::get<uintptr_t>(
            reinterpret_cast<uintptr_t>(foreign.quickuse.data()), 0x1f0);
        const auto native_key_rect = rendered_bounds(fixture.find(donor_key, "kbm"));
        const float special_gap = arrow_icon_rect.tl.x - special_icon_rect.br.x;
        const float flame_gap = flame_icon_rect.tl.x - arrow_icon_rect.br.x;
        const float refill_gap = rendered_refill_icon_rect.tl.x - native_arrow_icon_rect.br.x;
        retail_cache_ok &= special_gap > 0 && flame_gap > 0 && refill_gap > 0 &&
            close_to(special_gap, flame_gap) && close_to(flame_gap, refill_gap) &&
            special_gap < (arrow_icon_rect.br.x - arrow_icon_rect.tl.x) * 0.2f &&
            close_to(hud::center(special_icon_rect).y, hud::center(flame_icon_rect).y) &&
            close_to(hud::center(arrow_icon_rect).y, hud::center(flame_icon_rect).y) &&
            close_to(hud::center(rendered_refill_icon_rect).y, hud::center(flame_icon_rect).y) &&
            close_to(hud::center(f10_key_rect).x, hud::center(arrow_icon_rect).x) &&
            close_to(hud::center(f9_key_rect).x, hud::center(refill_root_rect).x) &&
            close_to(f9_key_rect.tl.y, native_key_rect.tl.y) &&
            close_to(f10_key_rect.tl.y, native_key_rect.tl.y) &&
            close_to(f9_key_rect.br.y - f9_key_rect.tl.y,
                     native_key_rect.br.y - native_key_rect.tl.y) &&
            close_to(f10_key_rect.br.y - f10_key_rect.tl.y,
                     native_key_rect.br.y - native_key_rect.tl.y) &&
            f9_key_rect.br.x - f9_key_rect.tl.x <=
                f10_key_rect.br.x - f10_key_rect.tl.x &&
            close_to(hud::center(ammo_glyph_rect).x, hud::center(refill_root_rect).x) &&
            close_to(hud::center(ammo_glyph_rect).y, hud::center(refill_root_rect).y) &&
            ammo_glyph_rect.br.x - ammo_glyph_rect.tl.x <=
                (rendered_refill_icon_rect.br.x - rendered_refill_icon_rect.tl.x) * 0.81f &&
            ammo_glyph_rect.br.y - ammo_glyph_rect.tl.y <=
                (rendered_refill_icon_rect.br.y - rendered_refill_icon_rect.tl.y) * 0.81f;
    }
    fixture.set_bounds(fixture.find(foreign.flame_root, "icon"), 32, 12);
    fixture.render(foreign.parent);
    update_on_hud(foreign.address());
    retail_cache_ok &= fixture.clips.at(foreign_arrow)->visible &&
        fixture.clips.at(foreign_refill)->visible &&
        fixture.clips.at(foreign_f9)->visible && fixture.clips.at(foreign_f10)->visible;
    if (!retail_cache_ok) std::fprintf(stderr, "HUD rendered-cache / F10 fixed-slot regression failed\n");
    ok &= retail_cache_ok;
    Scene hammer_start{};
    build(hammer_start, 8);
    const auto dormant_crucible = fixture.find(hammer_start.parent, "crucible_source");
    const auto starting_hammer = fixture.find(hammer_start.parent, "hammer_source");
    fixture.set_bounds(dormant_crucible, 0, 0);
    fixture.set_bounds(fixture.find(dormant_crucible, "icon"), 0, 0);
    fixture.children.erase({starting_hammer, "pips"});
    fixture.render(hammer_start.parent);
    ok &= observe_selected(SC_SPECIAL_WEAPON_HAMMER);
    update_on_hud(hammer_start.address());
    fixture.render(hammer_start.parent);
    update_on_hud(hammer_start.address());
    const auto startup_refill = fixture.find(hammer_start.parent, "apAmmoRefill");
    const auto startup_glyph = fixture.find(hammer_start.parent, "apAmmoGlyph");
    bool startup_ok = startup_refill && startup_glyph &&
        fixture.find(fixture.find(startup_refill, "pips"), "pips3") &&
        Fixture::get<uintptr_t>(startup_glyph, 0x60) == 9;
    for (const auto name : {"apAmmoRefill", "apAmmoGlyph", "apSpecialSwitch",
                            "apAmmoRefillBind", "apSpecialToggleBind"}) {
        const auto clip = fixture.find(hammer_start.parent, name);
        startup_ok &= clip && fixture.clips.at(clip)->visible;
    }
    if (!startup_ok) std::fprintf(stderr, "HUD Hammer startup / unrendered Crucible regression failed\n");
    ok &= startup_ok;
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
        std::strcmp(clips.facts[15].key, "pixels_observed") == 0 && clips.facts[15].value == 0;
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
