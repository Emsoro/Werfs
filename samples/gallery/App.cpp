// SPDX-License-Identifier: MIT
// Interactive Win32 demo: a real window, real D2D swap chain target, real input.
// Proves the WAML/MVVM path works end to end outside the headless snapshot.
#include "Controls.h"
#include "Core.h"
#include "D2DRender.h"
#include "Diagnostics.h"
#include "GalleryVm.h"  // sample: the Gallery page's ViewModel
#include "Mvvm.h"
#include "Resource.h"  // WAML_RESOURCE_TYPE / IDR_WAML_GALLERY
#include "Resources.h" // LoadWaml / version info
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <shellapi.h> // CommandLineToArgvW
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#undef DrawText // winuser.h maps DrawText to DrawTextW; PaintCtx declares it as a method.

using Microsoft::WRL::ComPtr;
using namespace wui;

namespace {

constexpr wchar_t kClassName[] = L"WuiWamlSpikeDemo";

// The ViewModel itself lives with the sample (samples/gallery/GalleryVm.h), so
// the demo, snapshot, interact and dpishot tools all bind the exact same object
// to the exact same markup.
using DemoVm = sample::GalleryVm;

struct AppState {
    WidgetPtr root;
    std::shared_ptr<DemoVm> vm;
    DWriteTextService* text = nullptr;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<IDWriteFactory> dwrite;
    std::map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes;
    std::map<std::wstring, ComPtr<IDWriteTextFormat>> formats;
    Widget* hovered = nullptr;
    Widget* pressed = nullptr;
    Widget* focused = nullptr;
    ThemeKind theme = ThemeKind::Light;
    int mouseDownX = 0;
    int mouseDownY = 0;
    bool mouseTracking = false;
    // DIP scale factor (dpi / 96). Layout and hit testing run in DIPs; window
    // messages carry physical pixels, so every coordinate crosses this scale.
    float scale = 1.0f;
};

AppState g;

// Physical pixels -> logical units for layout / hit testing.
float ToDip(int pixels) { return static_cast<float>(pixels) / g.scale; }
// Logical units -> physical pixels (used by the selftest to synthesize input
// the same way a real user's click arrives).
int ToPixels(float dip) { return static_cast<int>(dip * g.scale + 0.5f); }

class HwndPaintContext final : public PaintCtx {
public:
    HwndPaintContext(AppState& app, const ThemePalette& palette) : app_(app), palette_(palette) {}

    void FillRect(const Rect& r, const Color& c) override {
        auto* b = Brush(ResolveColor(c, palette_));
        if (b) app_.target->FillRectangle(R(r), b);
    }
    void FillRoundRect(const Rect& r, float radius, const Color& c) override {
        auto* b = Brush(ResolveColor(c, palette_));
        if (b) app_.target->FillRoundedRectangle(D2D1::RoundedRect(R(r), radius, radius), b);
    }
    void StrokeRect(const Rect& r, const Color& c, float width, float radius) override {
        auto* b = Brush(ResolveColor(c, palette_));
        if (b) app_.target->DrawRoundedRectangle(D2D1::RoundedRect(R(r), radius, radius), b, width);
    }
    void DrawText(const std::wstring& text, const Rect& r, const Color& c, float fontSize, int weight,
                  TextAlign align) override {
        auto* b = Brush(ResolveColor(c, palette_));
        auto* f = Format(fontSize, weight);
        if (!b || !f) return;
        f->SetTextAlignment(align == TextAlign::Center   ? DWRITE_TEXT_ALIGNMENT_CENTER
                            : align == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                        : DWRITE_TEXT_ALIGNMENT_LEADING);
        app_.target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), f, R(r), b,
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void PushClip(const Rect& r) override { app_.target->PushAxisAlignedClip(R(r), D2D1_ANTIALIAS_MODE_ALIASED); }
    void PopClip() override { app_.target->PopAxisAlignedClip(); }

private:
    static D2D1_RECT_F R(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }

    ID2D1SolidColorBrush* Brush(const Color& c) {
        const uint32_t key = (static_cast<uint32_t>(c.a * 255 + 0.5f) << 24) |
                             (static_cast<uint32_t>(c.r * 255 + 0.5f) << 16) |
                             (static_cast<uint32_t>(c.g * 255 + 0.5f) << 8) |
                             static_cast<uint32_t>(c.b * 255 + 0.5f);
        auto it = app_.brushes.find(key);
        if (it != app_.brushes.end()) return it->second.Get();
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(app_.target->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a),
                                                      brush.GetAddressOf())))
            return nullptr;
        return app_.brushes.emplace(key, brush).first->second.Get();
    }

    IDWriteTextFormat* Format(float fontSize, int weight) {
        const std::wstring key =
            std::to_wstring(static_cast<int>(fontSize * 4)) + L"/" + std::to_wstring(weight);
        auto it = app_.formats.find(key);
        if (it != app_.formats.end()) return it->second.Get();
        ComPtr<IDWriteTextFormat> format;
        if (FAILED(app_.dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, static_cast<DWRITE_FONT_WEIGHT>(FontWeightToDWrite(weight)),
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"",
                format.GetAddressOf())))
            return nullptr;
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return app_.formats.emplace(key, format).first->second.Get();
    }

    AppState& app_;
    const ThemePalette& palette_;
};

void EnsureTarget(HWND hwnd) {
    if (g.target) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                                   static_cast<UINT32>(rc.bottom - rc.top));
    // dpi 0 lets D2D use the system DPI, so one DIP is drawn as `scale` pixels.
    // Layout and hit testing stay in DIPs; input coordinates are divided by
    // `scale` on the way in. Previously this target used dpi 0 (scaled drawing)
    // while layout used raw pixels, so on a 200% display everything rendered
    // twice as large and clicks landed on the wrong widget -- the UI looked
    // completely unresponsive.
    g.d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                  D2D1::HwndRenderTargetProperties(hwnd, size),
                                  g.target.GetAddressOf());
    if (g.target) {
        g.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        g.brushes.clear();
    }
}

void Paint(HWND hwnd) {
    EnsureTarget(hwnd);
    if (!g.target) return;
    if (!g.root) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    // Layout runs in DIPs, so the available size is the client size in DIPs.
    const float w = ToDip(rc.right - rc.left);
    const float h = ToDip(rc.bottom - rc.top);
    g.root->ForceLayout(Size{w, h});

    const ThemePalette palette = MakePalette(g.theme);
    g.target->BeginDraw();
    g.target->Clear(D2D1::ColorF(palette.colors[static_cast<int>(Token::AppBackground)].r,
                                 palette.colors[static_cast<int>(Token::AppBackground)].g,
                                 palette.colors[static_cast<int>(Token::AppBackground)].b, 1.0f));
    HwndPaintContext ctx(g, palette);
    g.root->PaintTree(ctx, palette);
    const HRESULT hr = g.target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        g.target.Reset();
        g.brushes.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

Point LocalPoint(Widget* /*root*/, int sx, int sy) {
    return Point{ToDip(sx), ToDip(sy)};
}

Widget* Hit(Widget* root, int x, int y) { return root ? root->HitTest(LocalPoint(root, x, y)) : nullptr; }

void UpdateHover(HWND hwnd, Widget* target, int x, int y) {
    if (g.hovered == target) {
        if (target && target->Hovered()) target->OnPointerMove(LocalPoint(target, x, y));
        return;
    }
    if (g.hovered) g.hovered->OnPointerLeave();
    g.hovered = target;
    if (g.hovered) g.hovered->OnPointerEnter();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void SetFocus(Widget* target) {
    if (g.focused == target) return;
    if (g.focused) g.focused->SetFocused(false);
    g.focused = target;
    if (g.focused) g.focused->SetFocused(true);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 16, nullptr); // Idle tick: keeps the demo simple.
        return 0;

    case WM_TIMER: {
        // While typing, the caret does not blink in this spike; the tick only
        // repaints when something is actually dirty.
        if (g.root && (g.root->LayoutDirty() || g.root->PaintDirty())) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        // 0.3 ADR-4: DPI change keeps DIP layout, so only the scale factor and
        // the render target change. Re-read DPI and drop cached resources.
        g.scale = static_cast<float>(HIWORD(wp)) / 96.0f;
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (g.target) {
            g.target->SetDpi(static_cast<float>(HIWORD(wp)), static_cast<float>(HIWORD(wp)));
            g.brushes.clear();
            g.formats.clear();
        }
        if (g.root) g.root->Invalidate(AffectsMeasure | AffectsRender);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_SIZE:
        if (g.target) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            g.target->Resize(D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                                         static_cast<UINT32>(rc.bottom - rc.top)));
            g.brushes.clear();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        Paint(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        if (!g.mouseTracking) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g.mouseTracking = true;
        }
        Widget* target = Hit(g.root.get(), x, y);
        UpdateHover(hwnd, target, x, y);
        return 0;
    }

    case WM_MOUSELEAVE:
        g.mouseTracking = false;
        UpdateHover(hwnd, nullptr, 0, 0);
        return 0;

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        g.mouseDownX = x;
        g.mouseDownY = y;
        SetFocus(nullptr);
        Widget* target = Hit(g.root.get(), x, y);
        // Focus the nearest focusable ancestor (a TextBox hit test returns itself).
        for (Widget* w = target; w != nullptr; w = w->Parent()) {
            if (w->Focusable()) {
                SetFocus(w);
                break;
            }
        }
        g.pressed = target;
        if (target) target->OnPointerDown(LocalPoint(target, x, y));
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        if (g.pressed) {
            g.pressed->OnPointerUp(LocalPoint(g.pressed, x, y));
            g.pressed = nullptr;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (g.focused && g.focused->OnKeyDown(static_cast<int>(wp))) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_CHAR: {
        if (g.focused && g.focused->OnChar(static_cast<wchar_t>(wp))) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // --selftest: build the real HWND + D2D hardware render target, paint one
    // frame, and report success through the exit code. This is what proves the
    // windowed path works without needing an interactive desktop.
    const bool selfTest = wcsstr(GetCommandLineW(), L"--selftest") != nullptr;
    if (selfTest) {
        // GUI subsystem: redirect the standard streams to a log file next to the
        // executable so the selftest result survives outside a console.
        FILE* dummy = nullptr;
        freopen_s(&dummy, "selftest.log", "w", stdout);
        freopen_s(&dummy, "selftest.log", "a", stderr);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // One DIP is `scale` physical pixels. Must be read after the DPI awareness
    // call, otherwise the process reports the virtualised 96 DPI.
    {
        const UINT dpi = GetDpiForSystem();
        g.scale = (dpi == 0) ? 1.0f : static_cast<float>(dpi) / 96.0f;
        if (selfTest) std::printf("system dpi=%u, scale=%.2f\n", dpi, g.scale);
    }

    // Markup delivery follows the build configuration:
    //   debug   -> samples\gallery\Gallery.waml on disk (edit and rerun)
    //   release -> the RCDATA payload compiled into this exe
    // An explicit command-line path always wins, for both modes.
    std::string explicitPath;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                const std::wstring arg = argv[i];
                if (!arg.empty() && arg[0] != L'-') {
                    explicitPath = WideToUtf8(arg);
                    break;
                }
            }
            LocalFree(argv);
        }
    }

    WamlLoadResult loaded;
    if (!explicitPath.empty()) {
        loaded = LoadWamlFile(ResolveWamlPath(explicitPath).empty() ? explicitPath
                                                                   : ResolveWamlPath(explicitPath));
    } else {
        loaded = LoadWaml(WAML_RESOURCE_TYPE, IDR_WAML_GALLERY, "samples\\gallery\\Gallery.waml");
    }

    const wchar_t* modeName = PrefersLooseWamlFiles() ? L"loose files (debug)" : L"embedded resource (release)";
    if (selfTest) {
        std::printf("markup mode: %ls\n", modeName);
        std::printf("markup origin: %s (%s)\n", loaded.origin.c_str(), loaded.ok ? "ok" : loaded.error.c_str());
    }

    if (!loaded.ok) {
        const std::wstring message =
            L"Cannot load the WAML markup.\n\nmode: " + std::wstring(modeName) +
            L"\nreason: " + Utf8ToWide(loaded.error) +
            L"\n\nPass a path explicitly:\n  waml_demo.exe <file.waml>";
        if (selfTest) std::printf("SELFTEST FAIL: %s\n", loaded.error.c_str());
        else MessageBoxW(nullptr, message.c_str(), L"WAML spike demo", MB_ICONERROR);
        return 1;
    }

    ParseOptions opt;
    opt.fileName = loaded.FileName();
    DiagnosticBag bag;
    auto doc = CompileWaml(loaded.text, opt, bag);
    if (!doc) {
        if (selfTest) std::printf("SELFTEST FAIL: compile\n%s", bag.ToText().c_str());
        else MessageBoxW(nullptr, Utf8ToWide(bag.ToText()).c_str(), L"WAML compile error", MB_ICONERROR);
        return 1;
    }

    static DWriteTextService text; // outlives the window
    g.text = &text;
    TextSystem::Set(&text);

    g.vm = std::make_shared<DemoVm>();
    g.vm->SeedItems(4);
    g.root = doc->Instantiate(&text);
    g.root->SetDataContext(std::static_pointer_cast<RefObject>(g.vm));

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g.d2d.GetAddressOf());
    if (FAILED(hr)) {
        if (selfTest) std::printf("SELFTEST FAIL: D2D1CreateFactory 0x%08lx\n", hr);
        return 1;
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(g.dwrite.GetAddressOf()));
    if (FAILED(hr)) {
        if (selfTest) std::printf("SELFTEST FAIL: DWriteCreateFactory 0x%08lx\n", hr);
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        if (selfTest) std::printf("SELFTEST FAIL: RegisterClassExW %lu\n", GetLastError());
        return 1;
    }

    // Size the window in DIPs, not pixels: a fixed 780x960 px window is only
    // 390x480 DIP on a 200% display, which cramps the layout. 420x560 DIP maps
    // back to roughly the same physical size on a standard 100% desktop.
    const int winW = ToPixels(420.0f) + 16; // + frame allowance
    const int winH = ToPixels(560.0f) + 39;
    // Version and window title come from VERSIONINFO, so the title bar, the
    // About text and the file-properties dialog can never disagree. This also
    // exercises the version-reading path on every launch instead of only in the
    // resource probe.
    const std::wstring version = ModuleVersionString();
    const wchar_t* modeTag = PrefersLooseWamlFiles() ? L"debug/loose" : L"release/embedded";
    const std::wstring windowTitle =
        L"WAML + MVVM spike (WUI) " + version + L" [" + modeTag + L"]";
    const HICON appIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));

    HWND hwnd = CreateWindowExW(0, kClassName, windowTitle.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, nullptr, nullptr, hInstance,
                                nullptr);
    if (!hwnd) {
        if (selfTest) std::printf("SELFTEST FAIL: CreateWindowExW %lu\n", GetLastError());
        return 1;
    }
    // Big icon for Alt+Tab and the window, small icon for the title bar.
    if (appIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP))));
    }
    ShowWindow(hwnd, selfTest ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(hwnd);

    if (selfTest) {
        Paint(hwnd); // First frame: creates the hwnd render target.
        if (!g.target) {
            std::printf("SELFTEST FAIL: no D2D hwnd render target\n");
            DestroyWindow(hwnd);
            return 1;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const float w = static_cast<float>(rc.right - rc.left);
        const float h = static_cast<float>(rc.bottom - rc.top);
        std::printf("window client %.0fx%.0f, D2D hwnd target ok\n", w, h);

        // Drive real input through the window procedure, exactly like a user:
        // click the "+" button and check the ViewModel changed.
        Widget* plus = nullptr;
        for (auto& c : g.root->Children()[0]->Children()) {
            if (std::string(c->TypeName()) == "wui/Border") {
                for (auto& b : c->Children()[0]->Children()) {
                    if (std::string(b->TypeName()) == "wui/Button" &&
                        b->Get<std::wstring>(b->SlotOf("Content")) == L"+")
                        plus = b.get();
                }
            }
        }
        if (!plus) {
            std::printf("SELFTEST FAIL: could not locate the '+' button\n");
            DestroyWindow(hwnd);
            return 1;
        }
        const Rect& bounds = plus->Bounds();
        // bounds are DIPs; window messages carry physical pixels. Sending DIPs
        // straight to MAKELPARAM is what made the old selftest pass on a 200%
        // machine while the real UI was unclickable -- it tested nothing.
        const int x = ToPixels(bounds.x + bounds.w * 0.5f);
        const int y = ToPixels(bounds.y + bounds.h * 0.5f);
        const int before = g.vm->Count.Get();
        RECT client{};
        GetClientRect(hwnd, &client);
        std::printf("button dip=(%.0f,%.0f) -> pixel=(%d,%d), client=%ldx%ld\n", bounds.x, bounds.y, x, y,
                    client.right - client.left, client.bottom - client.top);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        std::printf("click at (%d,%d): count %d -> %d\n", x, y, before, g.vm->Count.Get());
        const bool clicked = g.vm->Count.Get() == before + 1;

        // A click landing on empty space must NOT change the count: if the DPI
        // mapping is wrong, the previous check can pass by coincidence, so probe
        // a corner pixel that cannot be over the button.
        const int beforeCorner = g.vm->Count.Get();
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 4));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(4, 4));
        const bool cornerIgnored = g.vm->Count.Get() == beforeCorner;

        // Text input through the real window procedure: click the TextBox, type,
        // and verify the ViewModel received every character. This exercises the
        // same DPI mapping as the button click, plus focus and WM_CHAR.
        const int beforeCorner2 = g.vm->Count.Get();
        Widget* box = nullptr;
        Widget* check = nullptr;
        std::function<void(Widget*)> scan = [&](Widget* w) {
            if (!w) return;
            const std::string t = w->TypeName();
            if (t == "wui/TextBox") box = w;
            if (t == "wui/CheckBox") check = w;
            for (auto& c : w->Children()) scan(c.get());
        };
        scan(g.root.get());
        bool typed = false;
        bool toggled = false;
        if (box) {
            const Rect& b = box->Bounds();
            const int bx = ToPixels(b.x + b.w * 0.5f);
            const int by = ToPixels(b.y + b.h * 0.5f);
            g.vm->Name.Set(L"");
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
            SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
            const bool focused = g.focused == box;
            SendMessageW(hwnd, WM_CHAR, L'h', 0);
            SendMessageW(hwnd, WM_CHAR, L'i', 0);
            typed = focused && g.vm->Name.Get() == L"hi";
            std::printf("textbox at pixel (%d,%d): focused=%s vm.Name='%s'\n", bx, by,
                        focused ? "yes" : "NO", WideToUtf8(g.vm->Name.Get()).c_str());
        }
        if (check) {
            const Rect& b = check->Bounds();
            const int cx = ToPixels(b.x + 8);
            const int cy = ToPixels(b.y + b.h * 0.5f);
            const bool wasChecked = g.vm->Enabled.Get();
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
            SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
            toggled = g.vm->Enabled.Get() != wasChecked;
            std::printf("checkbox at pixel (%d,%d): toggled=%s\n", cx, cy, toggled ? "yes" : "NO");
        }
        (void)beforeCorner2;

        Paint(hwnd); // Second frame via the same target.
        std::printf("negative probe (4,4): count stayed %d = %s\n", beforeCorner,
                    cornerIgnored ? "yes" : "NO");
        if (!cornerIgnored) std::printf("SELFTEST FAIL: a click on empty space hit the button\n");
        if (!typed) std::printf("SELFTEST FAIL: text input did not reach the ViewModel\n");
        if (!toggled) std::printf("SELFTEST FAIL: checkbox did not toggle\n");
        const bool ok = clicked && cornerIgnored && typed && toggled;
        std::printf("%s\n", ok ? "SELFTEST OK" : "SELFTEST FAIL");
        DestroyWindow(hwnd);
        return ok ? 0 : 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g.root.reset();
    g.vm.reset();
    return 0;
}
