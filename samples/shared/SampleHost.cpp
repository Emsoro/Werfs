// SPDX-License-Identifier: MIT
// Sample host implementation. See SampleHost.h for the contract.
#include "SampleHost.h"

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>  // CommandLineToArgvW
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// winuser.h maps DrawText to DrawTextW under UNICODE; PaintCtx declares it as a
// method, so the macro has to go BEFORE the framework headers are included --
// including it after them leaves SoftwareRender.h unable to override.
#undef DrawText

#include "Controls.h"
#include "D2DRender.h"
#include "Diagnostics.h"
#include "Mvvm.h"
#include "Resources.h"
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

using Microsoft::WRL::ComPtr;
using namespace wui;

namespace sample {
namespace {

constexpr wchar_t kClassName[] = L"GridformSampleHost";

struct AppState {
    WidgetPtr root;
    std::shared_ptr<RefObject> vm;
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
    bool mouseTracking = false;
    // DIP scale factor (dpi / 96). Layout and hit testing run in DIPs; window
    // messages carry physical pixels, so every coordinate crosses this scale.
    float scale = 1.0f;
};

AppState g;
HostOptions g_options;

// Physical pixels -> logical units for layout / hit testing.
float ToDip(int pixels) { return static_cast<float>(pixels) / g.scale; }
// Logical units -> physical pixels.
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
        // The real D2D method is DrawText; the windows.h macro of that name is
        // #undef'd above, so calling it plainly is correct.
        app_.target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), f, R(r), b,
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void PushClip(const Rect& r) override {
        app_.target->PushAxisAlignedClip(R(r), D2D1_ANTIALIAS_MODE_ALIASED);
    }
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
    // `scale` on the way in. Mixing these made the UI look unresponsive at 200%.
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
    if (!g.target || !g.root) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    // Layout runs in DIPs, so the available size is the client size in DIPs.
    g.root->ForceLayout(Size{ToDip(rc.right - rc.left), ToDip(rc.bottom - rc.top)});

    const ThemePalette palette = MakePalette(g.theme);
    const Color bg = palette.colors[static_cast<int>(Token::AppBackground)];
    g.target->BeginDraw();
    g.target->Clear(D2D1::ColorF(bg.r, bg.g, bg.b, 1.0f));
    HwndPaintContext ctx(g, palette);
    g.root->PaintTree(ctx, palette);
    const HRESULT hr = g.target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        g.target.Reset();
        g.brushes.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

Point LocalPoint(Widget*, int sx, int sy) { return Point{ToDip(sx), ToDip(sy)}; }

Widget* Hit(Widget* root, int x, int y) {
    return root ? root->HitTest(LocalPoint(root, x, y)) : nullptr;
}

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
        SetTimer(hwnd, 1, 16, nullptr);
        return 0;

    case WM_TIMER:
        // Repaint only when something is actually dirty; a free-running repaint
        // wastes a core and hides invalidation bugs.
        if (g.root && (g.root->LayoutDirty() || g.root->PaintDirty()))
            InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED: {
        // DIP layout stays; only the scale factor and the render target change.
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
        UpdateHover(hwnd, Hit(g.root.get(), x, y), x, y);
        return 0;
    }

    case WM_MOUSELEAVE:
        g.mouseTracking = false;
        UpdateHover(hwnd, nullptr, 0, 0);
        return 0;

    case WM_MOUSEWHEEL: {
        // Wheel goes to the widget under the cursor so nested scrollers get it.
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        Widget* target = Hit(g.root.get(), pt.x, pt.y);
        if (!target) target = g.root.get();
        const int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        if (target && target->OnPointerWheel(static_cast<float>(notches) * 3.0f)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        SetFocus(nullptr);
        Widget* target = Hit(g.root.get(), x, y);
        // Focus the nearest focusable ancestor.
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

    case WM_KEYDOWN:
        if (g.focused && g.focused->OnKeyDown(static_cast<int>(wp))) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_CHAR:
        if (g.focused && g.focused->OnChar(static_cast<wchar_t>(wp))) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Collects a command-line path argument (anything that is not a switch).
std::string ExplicitPathArgument() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (!arg.empty() && arg[0] != L'-') {
                path = WideToUtf8(arg);
                break;
            }
        }
        LocalFree(argv);
    }
    return path;
}

int RunInternal(HINSTANCE instance, const HostOptions& options, std::shared_ptr<RefObject> vm) {
    const bool selfTest = wcsstr(GetCommandLineW(), L"--selftest") != nullptr;
    if (selfTest) {
        // GUI subsystem: redirect the standard streams to a log file so the
        // result survives outside a console.
        FILE* dummy = nullptr;
        freopen_s(&dummy, "selftest.log", "w", stdout);
        freopen_s(&dummy, "selftest.log", "a", stderr);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);

    g_options = options;
    g.vm = std::move(vm);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // DPI must be read after the awareness call, otherwise it reports a
    // virtualised 96 and the scale factor is silently wrong.
    {
        const UINT dpi = GetDpiForSystem();
        g.scale = (dpi == 0) ? 1.0f : static_cast<float>(dpi) / 96.0f;
        if (selfTest) std::printf("system dpi=%u, scale=%.2f\n", dpi, g.scale);
    }

    // Markup delivery: an explicit path wins, then the build configuration.
    const std::string explicitPath = ExplicitPathArgument();
    WamlLoadResult loaded;
    if (!explicitPath.empty()) {
        const std::string resolved = ResolveWamlPath(explicitPath);
        loaded = LoadWamlFile(resolved.empty() ? explicitPath : resolved);
    } else {
        loaded = LoadWaml(options.resourceType, options.resourceId, options.relativePath);
    }

    const wchar_t* modeName =
        PrefersLooseWamlFiles() ? L"loose files (debug)" : L"embedded resource (release)";
    if (selfTest) {
        std::printf("markup mode: %ls\n", modeName);
        std::printf("markup origin: %s (%s)\n", loaded.origin.c_str(),
                    loaded.ok ? "ok" : loaded.error.c_str());
    }
    if (!loaded.ok) {
        if (selfTest) std::printf("SELFTEST FAIL: %s\n", loaded.error.c_str());
        else
            MessageBoxW(nullptr,
                        (L"Cannot load the WAML markup.\n\nreason: " + Utf8ToWide(loaded.error))
                            .c_str(),
                        L"Gridform sample", MB_ICONERROR);
        return 1;
    }

    ParseOptions opt;
    opt.fileName = loaded.FileName();
    DiagnosticBag bag;
    auto doc = CompileWaml(loaded.text, opt, bag);
    if (!doc) {
        if (selfTest) std::printf("SELFTEST FAIL: compile\n%s", bag.ToText().c_str());
        else
            MessageBoxW(nullptr, Utf8ToWide(bag.ToText()).c_str(), L"WAML compile error",
                        MB_ICONERROR);
        return 1;
    }
    if (selfTest) {
        std::printf("compiled: %zu nodes, %zu bindings, %zu props\n", doc->nodeCount,
                    doc->bindingCount, doc->propertyCount);
        std::printf("binding errors: %zu\n", BindingErrorCount());
    }

    static DWriteTextService text; // outlives the window
    g.text = &text;
    TextSystem::Set(&text);

    g.root = doc->Instantiate(&text);
    if (g.vm) g.root->SetDataContext(std::static_pointer_cast<RefObject>(g.vm));

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
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        if (selfTest) std::printf("SELFTEST FAIL: RegisterClassExW %lu\n", GetLastError());
        return 1;
    }

    // Window size in DIPs: a fixed pixel size would be cramped at 200%.
    const int winW = ToPixels(options.widthDip) + 16;
    const int winH = ToPixels(options.heightDip) + 39;
    const std::wstring title = options.title + L"  " + ModuleVersionString();

    HWND hwnd = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, nullptr, nullptr,
                                instance, nullptr);
    if (!hwnd) {
        if (selfTest) std::printf("SELFTEST FAIL: CreateWindowExW %lu\n", GetLastError());
        return 1;
    }

    if (selfTest) {
        // Build the real HWND + D2D target and paint one frame: this proves the
        // windowed path without needing an interactive desktop.
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        Paint(hwnd);
        std::printf("window %dx%d, d2d target %s\n", winW, winH, g.target ? "ok" : "MISSING");
        const bool ok = g.target != nullptr && BindingErrorCount() == 0;
        std::printf("%s\n", ok ? "SELFTEST OK" : "SELFTEST FAIL: bindings or target");
        DestroyWindow(hwnd);
        return ok ? 0 : 1;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

} // namespace

int RunSample(HINSTANCE instance, const HostOptions& options,
              std::shared_ptr<RefObject> (*makeVm)()) {
    std::shared_ptr<RefObject> vm;
    if (makeVm) vm = makeVm();
    return RunInternal(instance, options, std::move(vm));
}

int RunSample(HINSTANCE instance, const HostOptions& options, std::shared_ptr<RefObject> vm) {
    return RunInternal(instance, options, std::move(vm));
}

} // namespace sample
