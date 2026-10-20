#pragma once
namespace hud {
void project_progress(uintptr_t parent, uintptr_t anchor, const combat::ProgressView& progress) {
    if (!graphics_ready || !parent) return;
    auto value = child(parent, "apCombatProgress");
    const auto hide = [&] { if (value) show(value, false); };
    if (!anchor || !progress.visible || !progress.target) { hide(); return; }
    Rect rect{};
    Point origin{};
    if (!bounds(anchor, parent, rect) || !position(anchor, origin)) { hide(); return; }
    bool created = false;
    value = clone(anchor, parent, "apCombatProgress", created);
    if (!value) return;
    if (created) hold_frame(value, 1);
    const auto text = text_child(child(value, "text"), "txtVal");
    if (!text) { hide(); return; }
    if (const auto image = child(value, "question")) show(image, false);
    const auto movie = *reinterpret_cast<uintptr_t*>(parent + 0x30);
    if (!match_linear(value, anchor, parent, movie)) { hide(); return; }
    place(value, {origin.x, origin.y + (rect.br.y - rect.tl.y) * 1.25f});
    char label[80]{};
    std::snprintf(label, sizeof(label), "KILLS %llu / %llu", progress.total, progress.target);
    if (!text_is(text, label)) { swf.set_text(text, label); swf.dirty(value); }
    show(value, true);
}
}
