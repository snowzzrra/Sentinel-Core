#pragma once

// Included in special_native.cpp's private namespace. The engine owns all clips.
namespace hud {
struct Value { uint32_t type, reserved; uintptr_t payload; };
struct alignas(8) String { unsigned char storage[0x30]; };
struct Calls {
    Value* (*lookup)(uintptr_t, Value*, const char*) = nullptr;
    uintptr_t (*sprite)(const Value*) = nullptr;
    void (*release)(Value*) = nullptr;
    void (*string_init)(String*) = nullptr;
    void (*string_set)(String*, const char*) = nullptr;
    void (*string_free)(String*) = nullptr;
    uintptr_t (*duplicate)(uintptr_t, const String*) = nullptr;
    uintptr_t (*entry)(uintptr_t, int) = nullptr;
    uintptr_t (*add)(uintptr_t, int, unsigned, const String*) = nullptr;
    void (*dirty)(uintptr_t) = nullptr;
    void (*start)(uintptr_t, int) = nullptr;
    void (*frame)(uintptr_t, int) = nullptr;
    void (*visible)(uintptr_t, bool, bool) = nullptr;
    void (*position)(uintptr_t, float, float) = nullptr;
    void (*color)(uintptr_t, int) = nullptr;
    void (*material)(uintptr_t, uintptr_t, int, int, unsigned) = nullptr;
    uintptr_t (*find_material)(uintptr_t, const char*, int) = nullptr;
} swf;
bool enabled = false;

void refuse(const char* reason, uintptr_t owner, uintptr_t element, uint64_t epoch) {
    hud_trace.record(save::BStage::profile_output, save::BStatus::refused,
        reason, 0, {{"owner", owner}, {"element", element}, {"generation", epoch}});
}

uintptr_t child(uintptr_t parent, const char* name) {
    if (!parent) return 0;
    Value value{};
    const auto result = swf.lookup(parent, &value, name);
    const auto object = swf.sprite(result);
    if (value.type == 2 || value.type == 8) swf.release(&value);
    return object;
}

struct Point { float x, y; };
bool position(uintptr_t sprite, Point& out) {
    if (!sprite || !*reinterpret_cast<uintptr_t*>(sprite + 0x40)) return false;
    const auto slot = *reinterpret_cast<int32_t*>(sprite + 0xc);
    const auto context = *reinterpret_cast<uintptr_t*>(sprite + 0x10);
    if (slot < 0 || !context) return false;
    const auto transforms = *reinterpret_cast<uintptr_t*>(context + 0x80);
    if (!transforms) return false;
    const auto transform = transforms + static_cast<uintptr_t>(slot) * 0x40;
    out = {*reinterpret_cast<float*>(transform + 0x14), *reinterpret_cast<float*>(transform + 0x18)};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

// Clone native sprites under their live SWF parent, without copying objects.
uintptr_t clone(uintptr_t source, uintptr_t parent, const char* name, bool& created) {
    if (const auto found = child(parent, name)) return found;
    if (!source || !parent) return 0;
    const auto source_parent = *reinterpret_cast<uintptr_t*>(source + 0x40);
    if (!source_parent || *reinterpret_cast<uintptr_t*>(source + 0x30) !=
                          *reinterpret_cast<uintptr_t*>(parent + 0x30)) return 0;
    String native_name{};
    swf.string_init(&native_name);
    uintptr_t result = 0;
    __try {
        swf.string_set(&native_name, name);
        if (source_parent == parent) result = swf.duplicate(source, &native_name);
        else {
            const auto entry = swf.entry(source_parent, *reinterpret_cast<int32_t*>(source + 0x48));
            if (entry && *reinterpret_cast<uintptr_t*>(entry + 0x30) == source) {
                const auto depth = *reinterpret_cast<int32_t*>(parent + 0x78) + 0x4001;
                const auto added = swf.add(parent, depth, *reinterpret_cast<uint16_t*>(entry), &native_name);
                result = added ? *reinterpret_cast<uintptr_t*>(added + 0x30) : 0;
                if (result) { swf.dirty(result); swf.start(result, 1); }
            }
        }
    } __finally { swf.string_free(&native_name); }
    if (result) swf.visible(result, false, true);
    created |= result != 0;
    return result;
}

bool project(uintptr_t element, const HudOwnerSnapshot& owner, bool valid, unsigned keys, uint64_t epoch) {
    if (!enabled || !valid) return false;
    const auto widget = *reinterpret_cast<uintptr_t*>(element + 0x1e8);
    const auto chainsaw = *reinterpret_cast<uintptr_t*>(element + 0x1e0);
    const auto source = widget ? *reinterpret_cast<uintptr_t*>(widget + 0x18) : 0;
    const auto neighbor = chainsaw ? *reinterpret_cast<uintptr_t*>(chainsaw + 0x18) : 0;
    const auto parent = source ? *reinterpret_cast<uintptr_t*>(source + 0x40) : 0;
    if (!parent) { refuse("hud_parent_absent", element, source, epoch); return false; }
    Point anchor{}, adjacent{};
    const auto movie = *reinterpret_cast<uintptr_t*>(parent + 0x30);
    if (!movie || !neighbor || *reinterpret_cast<uintptr_t*>(source + 0x30) != movie ||
        *reinterpret_cast<uintptr_t*>(neighbor + 0x30) != movie ||
        *reinterpret_cast<uintptr_t*>(neighbor + 0x40) != parent ||
        !position(source, anchor) || !position(neighbor, adjacent)) {
        refuse("hud_swf_context_unavailable", element, source, epoch); return false;
    }
    auto refill = child(parent, "apAmmoRefill");
    auto special_arrow = child(parent, "apSpecialSwitch");
    const bool visible = owner.namespace_valid && *reinterpret_cast<uint8_t*>(element + 0x209);
    // All references come from this update's live parent, including invalidation.
    if (refill) swf.visible(refill, false, true);
    if (special_arrow) swf.visible(special_arrow, false, true);
    if (!visible) {
        hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
            "hud_invalidated", 0, {{"owner", element}, {"parent", parent}, {"generation", epoch},
                                   {"snapshot_valid", owner.namespace_valid}, {"context_valid", valid}});
        return false;
    }
    const Point step{anchor.x - adjacent.x, anchor.y - adjacent.y};
    if (step.x * step.x + step.y * step.y < 1.0f) {
        refuse("hud_slot_spacing_unknown", element, source, epoch); return false;
    }
    const Point refill_at{anchor.x + step.x, anchor.y + step.y};
    const Point offset{-step.y * 0.35f, step.x * 0.35f};
    bool created = false;
    const bool switch_available = owner.owns_crucible && owner.owns_hammer;
    const auto arrow_source = switch_available ? child(parent, "swapEquipment") : 0;
    if (switch_available && !special_arrow)
        special_arrow = clone(arrow_source, parent, "apSpecialSwitch", created);
    if (switch_available && special_arrow) {
        swf.position(special_arrow, anchor.x + offset.x, anchor.y + offset.y);
        swf.visible(special_arrow, true, true);
    }
    const bool new_refill = !refill;
    if (!refill) refill = clone(source, parent, "apAmmoRefill", created);
    if (!refill) { refuse("hud_refill_clone_failed", element, source, epoch); return false; }
    hud_trace.record(save::BStage::profile_prepare, save::BStatus::succeeded,
        "hud_bound", 0, {{"owner", element}, {"parent", parent}, {"layout_source", source},
                         {"generation", epoch}, {"created", created}, {"refill", refill}});
    const bool known = owner.refill_balance <= 3;
    const bool pending = owner.refill_request_state == SC_SPECIAL_REFILL_PENDING;
    const bool connected = (owner.refill_flags & SC_SPECIAL_REFILL_CONNECTED) != 0;
    swf.frame(refill, owner.refill_enabled ? 1 : 2);
    const auto icon = child(child(refill, "icon"), "iconStatic");
    if (!icon) { refuse("hud_refill_icon_absent", element, refill, epoch); return false; }
    struct MaterialAttempt {
        uintptr_t owner, parent, clip, icon;
        uint64_t epoch, retry_at;
        bool applied;
    };
    thread_local MaterialAttempt attempt{};
    if (new_refill || attempt.owner != element || attempt.parent != parent || attempt.clip != refill ||
        attempt.icon != icon || attempt.epoch != epoch)
        attempt = {element, parent, refill, icon, epoch, 0, false};
    if (attempt.applied && !*reinterpret_cast<uintptr_t*>(icon + 0x60)) {
        attempt.applied = false;
        attempt.retry_at = 0;
    }
    if (!attempt.applied && GetTickCount64() >= attempt.retry_at) {
        attempt.retry_at = GetTickCount64() + 1000;
        const auto material = swf.find_material(image_base + 0x5e05200, "art/ui/icons/ammo/bullets", 0);
        if (material) {
            swf.material(icon, material, -1, -1, 0);
            attempt.applied = *reinterpret_cast<uintptr_t*>(icon + 0x60) == material;
        }
    }
    if (!attempt.applied) {
        refuse("hud_ammo_material_unavailable", element, icon, epoch); return false;
    }
    const auto pips = child(refill, "pips");
    if (!pips) { refuse("hud_pips_absent", element, refill, epoch); return false; }
    swf.frame(pips, 3);
    const auto three = child(pips, "pips3");
    if (!three) { refuse("hud_three_pip_template_absent", element, pips, epoch); return false; }
    swf.visible(pips, known, true);
    const auto frame_before = *reinterpret_cast<uint16_t*>(three + 0x58);
    const auto desired = static_cast<uint16_t>(owner.refill_balance + 1);
    if (known) {
        if (*reinterpret_cast<uint16_t*>(three + 0x58) != desired) {
            swf.frame(three, desired);
            swf.dirty(three);
        }
    }
    const auto palette = *reinterpret_cast<int32_t*>(widget + 0x1ec);
    if (const auto fill = child(three, "fill")) swf.color(fill, palette);
    if (const auto fill = child(three, "innerFill")) swf.color(fill, palette);
    swf.position(refill, refill_at.x, refill_at.y);
    swf.visible(refill, true, true);
    hud_trace.record(save::BStage::profile_output, save::BStatus::pending,
        "hud_graphics_applied_native_labels_pending", 0,
        {{"owner", element}, {"parent", parent}, {"generation", epoch}, {"created", created},
         {"snapshot_valid", owner.namespace_valid}, {"policy", owner.selected}, {"balance", owner.refill_balance},
         {"pending", pending}, {"connected", connected}, {"binds", keys},
         {"snapshot_revision", owner.revision}, {"request_revision", owner.request_revision},
         {"pip_frame_before", frame_before}, {"pip_frame_after", *reinterpret_cast<uint16_t*>(three + 0x58)},
         {"native_crucible_170", *reinterpret_cast<int32_t*>(element + 0x170)}, {"pixels_observed", 0},
         {"switch_source", arrow_source}, {"switch_clip", special_arrow}});
    return known && *reinterpret_cast<uint16_t*>(three + 0x58) == desired;
}
} // namespace hud
