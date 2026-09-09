#include "sentinel_bootstrap.h"
#include <windows.h>
#include <cstdio>

// This executable's import table loads the candidate before main, like DOOM's
// MSIMG32 import. No explicit LoadLibrary call is used for the bootstrap here.
int main() {
    sc_boot_status status{};
    const ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        if (sc_bootstrap_inspect(SC_ABI_VERSION, sizeof(status), &status) != SC_OK) return 1;
        if (status.state != SC_BOOT_STARTING) break;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    if (status.state != SC_BOOT_READY || status.core.state != SC_READY) return 2;
    // Exercise the same two named drawing imports observed in DOOM using an
    // isolated in-memory GDI fixture. Private exports are tested by pointer only.
    HDC source = CreateCompatibleDC(nullptr);
    HDC target = CreateCompatibleDC(nullptr);
    HBITMAP source_bitmap = CreateBitmap(2, 2, 1, 32, nullptr);
    HBITMAP target_bitmap = CreateBitmap(2, 2, 1, 32, nullptr);
    if (!source || !target || !source_bitmap || !target_bitmap) return 3;
    HGDIOBJ old_source = SelectObject(source, source_bitmap);
    HGDIOBJ old_target = SelectObject(target, target_bitmap);
    const COLORREF red = RGB(255, 0, 0);
    SetPixel(source, 0, 0, red);
    const BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, 0};
    const bool alpha = AlphaBlend(target, 0, 0, 1, 1, source, 0, 0, 1, 1, blend) != FALSE
        && GetPixel(target, 0, 0) == red;
    SetPixel(target, 0, 0, RGB(0, 0, 0));
    const bool transparent = TransparentBlt(target, 0, 0, 1, 1, source, 0, 0, 1, 1, RGB(0, 255, 0)) != FALSE
        && GetPixel(target, 0, 0) == red;
    SelectObject(source, old_source);
    SelectObject(target, old_target);
    DeleteObject(source_bitmap);
    DeleteObject(target_bitmap);
    DeleteDC(source);
    DeleteDC(target);
    if (!alpha || !transparent) return 4;
    std::printf("PASS static import, deferred init and AlphaBlend/TransparentBlt pixel results; core=%s build=%s\n",
                status.core.version, status.core.build_id);
    // Deliberate ordinary process exit, without explicit shutdown or hot unload.
    return 0;
}
