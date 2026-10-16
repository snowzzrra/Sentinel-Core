#pragma once
namespace hud {
uintptr_t text_transform(uintptr_t parent, uintptr_t entry) {
    const auto context = parent ? *reinterpret_cast<uintptr_t*>(parent + 0x10) : 0;
    const auto index = entry ? *reinterpret_cast<int32_t*>(entry + 8) : -1;
    const auto pool = context ? *reinterpret_cast<uintptr_t*>(context + 0x80) : 0;
    return pool && index >= 0 ? pool + static_cast<uintptr_t>(index) * 0x40 : 0;
}
void project_progress(const GraphicsSource& source, const combat::ProgressView& progress) {
    if (!graphics_ready || !keycap_ready || !source.parent) return;
    auto value = text_child(source.parent, "apCombatProgress");
    const auto hide = [&] {
        if (value && !text_is(value, "")) { swf.set_text(value, ""); swf.dirty(source.parent); }
    };
    if (!progress.visible || !progress.target) { hide(); return; }
    const auto donor = swf.keycap(source.primary_root);
    const auto keyboard = child(donor, "kbm");
    const auto donor_text = text_child(keyboard, "txtVal");
    const auto donor_entry = donor_text ? swf.entry(keyboard, *reinterpret_cast<int32_t*>(donor_text + 0x30)) : 0;
    if (!donor_entry || *reinterpret_cast<uintptr_t*>(donor_entry + 0x38) != donor_text) { hide(); return; }
    Rect primary{};
    Point origin{}, x{}, y{};
    const auto donor_transform = text_transform(keyboard, donor_entry);
    if (!donor_transform || !bounds(source.primary_root, source.parent, primary)) { hide(); return; }
    const auto map = [&](Point point, Point& out) {
        const auto t = donor_transform;
        return affine_to(keyboard, source.parent, source.movie,
            {*reinterpret_cast<float*>(t + 4) * point.x + *reinterpret_cast<float*>(t + 0xc) * point.y,
             *reinterpret_cast<float*>(t + 0x10) * point.x + *reinterpret_cast<float*>(t + 8) * point.y}, out);
    };
    if (!map({0, 0}, origin) || !map({1, 0}, x) || !map({0, 1}, y)) { hide(); return; }
    if (!value) {
        String name{}; swf.string_init(&name);
        __try {
            swf.string_set(&name, "apCombatProgress");
            const auto depth = *reinterpret_cast<int32_t*>(source.parent + 0x78) + 0x4001;
            const auto added = swf.add(source.parent, depth, *reinterpret_cast<uint16_t*>(donor_entry), &name);
            value = added ? *reinterpret_cast<uintptr_t*>(added + 0x38) : 0;
        } __finally { swf.string_free(&name); }
    }
    const auto entry = value ? swf.entry(source.parent, *reinterpret_cast<int32_t*>(value + 0x30)) : 0;
    const auto transform = text_transform(source.parent, entry);
    if (!transform) { hide(); return; }
    float top = primary.tl.y;
    for (const auto name : {"apAmmoRefill", "apSpecialSwitch", "apAmmoRefillBind", "apSpecialToggleBind"}) {
        const auto clip = child(source.parent, name); Rect rect{};
        if (clip && *reinterpret_cast<uint8_t*>(clip + 0x51) && bounds(clip, source.parent, rect))
            top = std::min(top, rect.tl.y);
    }
    const auto definition = *reinterpret_cast<uintptr_t*>(donor_text + 0x20);
    const float configured = *reinterpret_cast<float*>(donor_text + 0x38);
    const float font = configured > 0 ? configured : *reinterpret_cast<uint16_t*>(definition + 0x16) * 0.05f;
    const float height = std::hypot(y.x - origin.x, y.y - origin.y) * font;
    if (!std::isfinite(height) || height <= 0) { hide(); return; }
    const float matrix[]{x.x - origin.x, y.y - origin.y, y.x - origin.x, x.y - origin.y,
                         primary.tl.x, top - height * 2.0f};
    bool changed = false;
    for (unsigned i = 0; i < 6; ++i) {
        auto& target = *reinterpret_cast<float*>(transform + 4 + i * 4);
        if (target != matrix[i]) { target = matrix[i]; changed = true; }
    }
    if (!*reinterpret_cast<uint8_t*>(value + 0x34)) {
        *reinterpret_cast<uint8_t*>(value + 0x34) = 1;
        *reinterpret_cast<uint8_t*>(value + 0x168) = 0;
        changed = true;
    }
    if (*reinterpret_cast<float*>(value + 0x38) != configured) {
        *reinterpret_cast<float*>(value + 0x38) = configured;
        *reinterpret_cast<uint8_t*>(value + 0x168) = 0;
        changed = true;
    }
    char text[80]{};
    std::snprintf(text, sizeof(text), "KILLS %llu / %llu", progress.total, progress.target);
    if (!text_is(value, text)) { swf.set_text(value, text); changed = true; }
    if (changed) swf.dirty(source.parent);
}
}
