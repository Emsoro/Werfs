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

#include "Animation.h"
#include "Controls.h"
#include "D2DRender.h"
#include "Diagnostics.h"
#include "Mvvm.h"
#include "Overlay.h"
#include "Resources.h"
#include "TextInput.h"
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

using Microsoft::WRL::ComPtr;
using namespace wui;

namespace sample {
namespace {

constexpr wchar_t kClassName[] = L"WerfsSampleHost";

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
    HWND hwnd = nullptr;
    ThemeKind theme = ThemeKind::Light;
    bool mouseTracking = false;
    // An overlay (a menu, a dialog) owns the pointer between these two events.
    // Tracked here rather than inferred, because a press on a menu item must not
    // deliver its release to whatever is now underneath it.
    bool overlayCapture = false;
    // Modifier state for key dispatch. The framework keeps no keyboard state: it
    // is the host's, and passing it in is what lets Ctrl+S reach a shortcut table
    // without every control having to track the keyboard globally.
    int modifiers = 0;
    // ImmSet* dispatches WM_IME_* synchronously, so an IME can re-enter our
    // handler from inside UpdateImePosition(). Those nested messages echo the
    // state we just installed; treating them as new edits corrupted the
    // composition preview/caret pair and crashed the editor on Chinese input.
    bool inImeMessage = false;
    // DIP scale factor (dpi / 96). Layout and hit testing run in DIPs; window
    // messages carry physical pixels, so every coordinate crosses this scale.
    float scale = 1.0f;
};

AppState g;
HostOptions g_options;

// The page's theme property, when it has one. A bool in the ViewModel is the
// whole contract: the host does not care which control flipped it or why.
void ApplyTheme(bool dark);

class ThemeObserver final : public IPropertyObserver {
public:
    void OnSourceChanged(const Value& value) override {
        bool dark = false;
        if (ValueCodec<bool>::From(value, dark)) ApplyTheme(dark);
    }
};
ThemeObserver g_themeObserver;

void ApplyTheme(bool dark) {
    const ThemeKind next = dark ? ThemeKind::Dark : ThemeKind::Light;
    if (g.theme == next) return;
    g.theme = next;
    // Brushes are keyed by resolved colour, so a different palette naturally
    // misses the cache; no brush table needs clearing.
    if (g.root) g.root->Invalidate(AffectsRender);
    if (g.hwnd) InvalidateRect(g.hwnd, nullptr, FALSE);
}

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
    void FillEllipse(const Rect& r, const Color& c) override {
        auto* b = Brush(ResolveColor(c, palette_));
        if (!b) return;
        app_.target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(r.x + r.w * 0.5f, r.y + r.h * 0.5f), r.w * 0.5f,
                          r.h * 0.5f),
            b);
    }
    void DrawLine(const Point& a, const Point& b, const Color& c, float width) override {
        auto* brush = Brush(ResolveColor(c, palette_));
        if (brush) app_.target->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush, width);
    }
    void DrawBitmap(const Bitmap* bitmap, const Rect& dest, const Rect* clip) override {
        if (bitmap == nullptr || !bitmap->Valid()) return;
        // Premultiply on the way in: the decoder hands out straight alpha and D2D wants
        // premultiplied, or transparent edges show bright fringes.
        std::vector<uint32_t> premul(bitmap->pixels.size());
        for (size_t i = 0; i < bitmap->pixels.size(); ++i) {
            const uint32_t p = bitmap->pixels[i];
            const uint32_t a = (p >> 24) & 0xFF;
            const uint32_t r = ((p >> 16) & 0xFF) * a / 255;
            const uint32_t green = ((p >> 8) & 0xFF) * a / 255;
            const uint32_t bl = (p & 0xFF) * a / 255;
            premul[i] = (a << 24) | (r << 16) | (green << 8) | bl;
        }
        ComPtr<ID2D1Bitmap> d2d;
        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(app_.target->CreateBitmap(
                D2D1::SizeU(static_cast<UINT32>(bitmap->width), static_cast<UINT32>(bitmap->height)),
                premul.data(), static_cast<UINT32>(bitmap->width) * 4, props,
                d2d.GetAddressOf()))) {
            return;
        }
        if (clip != nullptr) {
            // Circular crop for PersonPicture: an ellipse mask, not an axis-aligned clip,
            // because clipping to the bounding box would leave the picture's corners.
            ComPtr<ID2D1Factory> factory;
            app_.target->GetFactory(factory.GetAddressOf());
            ComPtr<ID2D1EllipseGeometry> geom;
            const D2D1_RECT_F d = R(dest);
            if (factory && SUCCEEDED(factory->CreateEllipseGeometry(
                               D2D1::Ellipse(D2D1::Point2F(d.left + (d.right - d.left) * 0.5f,
                                                           d.top + (d.bottom - d.top) * 0.5f),
                                             (d.right - d.left) * 0.5f, (d.bottom - d.top) * 0.5f),
                               geom.GetAddressOf()))) {
                ComPtr<ID2D1Layer> layer;
                D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters();
                params.geometricMask = geom.Get();
                params.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
                params.contentBounds = d;
                if (SUCCEEDED(app_.target->CreateLayer(nullptr, layer.GetAddressOf()))) {
                    app_.target->PushLayer(params, layer.Get());
                    app_.target->DrawBitmap(d2d.Get(), d, 1.0f,
                                            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    app_.target->PopLayer();
                    return;
                }
            }
        }
        app_.target->DrawBitmap(d2d.Get(), R(dest), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    void DrawText(const std::wstring& text, const Rect& r, const Color& c, float fontSize, int weight,
                  TextAlign align, const std::wstring& family) override {
        auto* b = Brush(ResolveColor(c, palette_));
        auto* f = Format(fontSize, weight, family);
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

    IDWriteTextFormat* Format(float fontSize, int weight, const std::wstring& family) {
        // Family is part of the cache key: two families at one size are different formats.
        const std::wstring key = std::to_wstring(static_cast<int>(fontSize * 4)) + L"/" +
                                 std::to_wstring(weight) + L"/" + family;
        auto it = app_.formats.find(key);
        if (it != app_.formats.end()) return it->second.Get();
        ComPtr<IDWriteTextFormat> format;
        if (FAILED(app_.dwrite->CreateTextFormat(
                family.empty() ? L"Segoe UI" : family.c_str(), nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(FontWeightToDWrite(weight)),
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

// The overlay currently showing, sized to the window so it can place itself.
OverlayLayer* CurrentOverlay() {
    if (!g.root) return nullptr;
    OverlayLayer* layer = g.root->Overlay();
    if (!layer) return nullptr;
    if (!layer->Active()) {
        // The layer closed itself (an item was chosen, Escape, click outside).
        // Leaving the dangling pointer installed would paint a dead layer.
        g.root->SetOverlay(nullptr);
        return nullptr;
    }
    return layer;
}

// The overlay must be told the window size before it can decide whether it fits;
// a menu anchored near the right edge flips left only if it knows the width.
void PrepareOverlay(OverlayLayer* layer) {
    if (!layer || !g.root) return;
    const Rect rootBounds = g.root->Bounds();
    const Size available{rootBounds.w, rootBounds.h};
    PrepareMenuLayerSizing(layer, available);
    PrepareDialogLayerSizing(layer, available);
}

int CurrentModifiers() {
    int mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= ModCtrl;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= ModShift;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= ModAlt;
    return mods;
}

// Application-wide shortcuts, resolved once at startup. Parsing at startup rather
// than per keypress means a malformed spec is reported once and then ignored,
// instead of being silently re-parsed on every keystroke.
struct AcceleratorEntry {
    int vk = 0;
    int modifiers = 0;
    RefPtr command;
    std::wstring spec;
};
std::vector<AcceleratorEntry> g_accelerators;

void BuildAccelerators(const HostOptions& options) {
    g_accelerators.clear();
    for (const auto& pair : options.accelerators) {
        AcceleratorEntry entry;
        if (!ParseAccelerator(WideToUtf8(pair.first), entry.vk, entry.modifiers)) {
            OutputDebugStringW((L"[wui] accelerator '" + pair.first + L"' is not valid\n").c_str());
            continue;
        }
        entry.command = pair.second;
        entry.spec = pair.first;
        g_accelerators.push_back(std::move(entry));
    }
}

// Returns true when a shortcut ran, so the message is consumed.
bool HandleAccelerator(HWND hwnd, int vk, int modifiers) {
    for (const auto& entry : g_accelerators) {
        if (entry.vk != vk || entry.modifiers != modifiers) continue;
        auto* command = dynamic_cast<ICommand*>(entry.command.get());
        if (!command || !command->CanExecute()) return true; // consumed, no-op
        command->Execute();
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    return false;
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

// One composition message at a time. The IME sends replies to our own ImmSet* calls
// synchronously, from inside the call; a nested reply must NOT run the state machine
// again. RAII makes sure the flag is cleared even if a handler throws.
class ImeMessageScope {
public:
    explicit ImeMessageScope(bool& flag) : flag_(flag), active_(!flag) {
        if (active_) flag_ = true;
    }
    ~ImeMessageScope() {
        if (active_) flag_ = false;
    }
    bool Active() const { return active_; }
    ImeMessageScope(const ImeMessageScope&) = delete;
    ImeMessageScope& operator=(const ImeMessageScope&) = delete;

private:
    bool& flag_;
    bool active_;
};

// Reads a composition or result string out of the IME context.
//
// The two-pass ImmGetCompositionStringW is the documented contract: ask for the size, then
// ask for the bytes. It returns the length in BYTES, not characters, which is the usual
// source of buffer overruns here.
std::wstring ReadImeString(HWND hwnd, DWORD which) {
    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) return {};
    const LONG bytes = ImmGetCompositionStringW(context, which, nullptr, 0);
    std::wstring result;
    if (bytes > 0) {
        result.resize(static_cast<size_t>(bytes) / sizeof(wchar_t));
        ImmGetCompositionStringW(context, which, result.data(), bytes);
    }
    ImmReleaseContext(hwnd, context);
    return result;
}

// Tells the IME where the caret is, in the coordinates the platform service expects.
//
// The framework works in DIPs and knows nothing about windows; the service converts to
// screen pixels. A widget that cannot describe its caret simply leaves the candidate window
// where it was, which is the right failure: a slightly stale position beats no IME at all.
void UpdateImePosition() {
    ITextInputService* service = TextInputSystem::Get();
    if (service == nullptr || g.focused == nullptr) return;
    const CompositionCaret caret = g.focused->ImeCaret();
    service->MoveCandidateWindow(caret);
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
        // Install the IME service as soon as there is an HWND: the candidate window can
        // only be positioned once a window exists, and a composition that starts before this
        // would have nowhere to go.
        TextInputSystem::Set(CreateWin32TextInputService(hwnd));
        return 0;

    case WM_TIMER:
        // Advance animations with REAL elapsed time, measured rather than assumed: the
        // timer fires every 16 ms but Windows does not guarantee it, and easing against a
        // drifting clock makes a 200 ms fade take 300 ms under load.
        if (g.root) {
            static LARGE_INTEGER last{};
            static LARGE_INTEGER freq{};
            if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            float dt = 1.0f / 60.0f;
            if (last.QuadPart != 0 && freq.QuadPart != 0) {
                dt = static_cast<float>(static_cast<double>(now.QuadPart - last.QuadPart) /
                                        static_cast<double>(freq.QuadPart));
                // Clamp: a window that was dragged or resumed can report a seconds-long
                // delta, which would jump every animation straight to its end.
                dt = std::clamp(dt, 0.0f, 0.1f);
            }
            last = now;
            AnimationHost::AdvanceAll(dt);
            // The page's own per-frame work, after the framework's animation clock so a
            // page that appends samples sees the same dt the animations did. Its return
            // value is the page telling us "I changed something" -- without it a live
            // page would either never repaint or repaint every tick regardless.
            if (g_options.onFrame && g_options.onFrame(dt)) {
                if (g.root) g.root->Invalidate(AffectsRender);
            }
        }
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
        // While a layer has the pointer, hover belongs to it: the document must
        // not light up its buttons underneath an open menu.
        if (OverlayLayer* layer = CurrentOverlay()) {
            PrepareOverlay(layer);
            layer->OnPointerMove(LocalPoint(nullptr, x, y));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
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
        // The overlay gets the press FIRST. A menu sits on top, so a click on its
        // items must never reach the control that happens to be underneath -- and
        // a click outside it dismisses the menu instead of activating a button.
        if (OverlayLayer* layer = CurrentOverlay()) {
            PrepareOverlay(layer);
            const bool consumed = layer->OnPointerDown(LocalPoint(nullptr, x, y));
            g.overlayCapture = layer->Active();
            if (g.overlayCapture) SetCapture(hwnd);
            else ReleaseCapture();
            if (consumed) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        Widget* target = Hit(g.root.get(), x, y);
        // Focus the nearest focusable ancestor. Resolve the new focus before
        // touching the current one: clicking inside the box that already has
        // focus must not read as a focus loss, or a LostFocus binding would
        // commit while the caret is merely being moved.
        Widget* focusTarget = nullptr;
        for (Widget* w = target; w != nullptr; w = w->Parent()) {
            if (w->Focusable()) {
                focusTarget = w;
                break;
            }
        }
        SetFocus(focusTarget);
        g.pressed = target;
        // Convert to the target's own space. Pointer coordinates are window
        // space, but every control tests bounds_, which are unscrolled -- inside
        // a scrolled page the two differ by the scroll offset, so dispatching the
        // raw point made controls ignore clicks after scrolling.
        if (target) target->OnPointerDown(target->ToLocal(LocalPoint(target, x, y)));
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        // The release goes to whoever got the press. Not to whatever is under the
        // cursor NOW: choosing a menu item removes the layer before the release
        // arrives, and re-hit-testing would then click the page underneath.
        if (g.overlayCapture) {
            g.overlayCapture = false;
            ReleaseCapture();
            // The press went to the overlay, so the release must go there too. Flyout
            // layers (ComboBox / AutoSuggestBox) commit the chosen row in OnPointerUp;
            // dropping this event left the suggestion list selectable only with the
            // keyboard -- a mouse click highlighted a row but never selected it.
            if (OverlayLayer* layer = CurrentOverlay()) {
                layer->OnPointerUp(LocalPoint(nullptr, x, y));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        ReleaseCapture();
        if (g.pressed) {
            g.pressed->OnPointerUp(g.pressed->ToLocal(LocalPoint(g.pressed, x, y)));
            g.pressed = nullptr;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        g.modifiers = CurrentModifiers();
        if (OverlayLayer* layer = CurrentOverlay()) {
            if (layer->OnKeyDown(static_cast<int>(wp), g.modifiers)) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        // While a composition is open the IME owns the keyboard: Enter picks a
        // candidate, not "insert newline" or "leave the field". DefWindowProc is
        // what forwards these keys to the IME.
        if (g.focused && g.focused->Composing())
            return DefWindowProcW(hwnd, msg, wp, lp);
        // The focused control gets the key first, so Ctrl+A inside a TextBox is
        // text selection rather than a menu shortcut. Only an unhandled key falls
        // through to the accelerator table.
        if (g.focused && g.focused->OnKeyDown(static_cast<int>(wp))) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (HandleAccelerator(hwnd, static_cast<int>(wp), g.modifiers)) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_KEYUP:
        g.modifiers = CurrentModifiers();
        return 0;

    case WM_CHAR:
        // An IME delivers committed characters through the composition messages, not here.
        // If a composition is in progress this key belongs to the IME and must not also be
        // inserted as a literal, or "zhong" would appear as letters next to the candidate.
        if (g.focused && !g.focused->Composing()) {
            if (g.focused->OnChar(static_cast<wchar_t>(wp))) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_IME_STARTCOMPOSITION: {
        ImeMessageScope scope(g.inImeMessage);
        if (!scope.Active()) return 0; // nested echo from ImmSet*
        // Position the candidate window BEFORE the composition begins. If the first
        // position is wrong the list appears in a corner of the screen and never recovers,
        // because many IMEs only reposition on subsequent updates.
        UpdateImePosition();
        return 0;
    }

    case WM_IME_COMPOSITION: {
        ImeMessageScope scope(g.inImeMessage);
        if (!scope.Active()) return 0;
        // GCS_RESULTSTR: the user picked a candidate. That is a real edit.
        // GCS_COMPSTR:   the string is still being built. That is a preview.
        // Both can arrive together, and the result must win.
        if (lp & GCS_RESULTSTR) {
            const std::wstring text = ReadImeString(hwnd, GCS_RESULTSTR);
            if (g.focused) g.focused->CommitComposition(text);
        } else if (lp & GCS_COMPSTR) {
            const std::wstring text = ReadImeString(hwnd, GCS_COMPSTR);
            if (g.focused) g.focused->SetComposition(text);
        }
        UpdateImePosition();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_IME_ENDCOMPOSITION: {
        ImeMessageScope scope(g.inImeMessage);
        if (!scope.Active()) return 0;
        // The IME finished or gave up. If it never sent a result string, the preview must
        // not be left on screen looking like committed text.
        if (g.focused && g.focused->Composing()) g.focused->CancelComposition();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_IME_NOTIFY: {
        ImeMessageScope scope(g.inImeMessage);
        if (!scope.Active()) return 0;
        // Only the candidate-list notifications report something that actually moved. The
        // IMN_SETCOMPOSITIONWINDOW / IMN_SETCANDIDATEPOS ones are the IME echoing back the
        // ImmSet* calls UpdateImePosition just made, dispatched synchronously from inside
        // them; repositioning on those re-enters UpdateImePosition from within itself. The
        // service refuses the nested call (Microsoft Pinyin otherwise recursed until the
        // stack overflowed), but not asking for the echo keeps the intent clear: we move the
        // list because the user's composition changed, not because we moved it ourselves.
        const UINT notification = static_cast<UINT>(wp);
        if (notification == IMN_OPENCANDIDATE || notification == IMN_CHANGECANDIDATE)
            UpdateImePosition();
        return 0;
    }

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
    BuildAccelerators(g_options);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // DPI must be read after the awareness call, otherwise it reports a
    // virtualised 96 and the scale factor is silently wrong.
    {
        const UINT dpi = GetDpiForSystem();
        g.scale = (dpi == 0) ? 1.0f : static_cast<float>(dpi) / 96.0f;
        if (selfTest) std::printf("system dpi=%u, scale=%.2f\n", dpi, g.scale);
    }

    // Markup delivery: an explicit option wins, then a command-line path,
    // then the build configuration. The Python sample needs the first branch:
    // in `python main.py` the command line's first non-switch argument is
    // main.py itself, not a markup file.
    const std::string explicitPath = ExplicitPathArgument();
    WamlLoadResult loaded;
    if (!options.markupPath.empty()) {
        loaded = LoadWamlFile(options.markupPath);
    } else if (!explicitPath.empty()) {
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
                        L"Werfs sample", MB_ICONERROR);
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

    // A page that can switch themes names the bool property that decides it.
    // Resolving it here (rather than at first paint) turns a typo into a startup
    // error instead of a button that does nothing.
    if (!options.themeProperty.empty()) {
        PropertyTable* table = g.vm ? g.vm->GetPropertyTable() : nullptr;
        const auto* entry = table ? table->Find(WideToUtf8(options.themeProperty)) : nullptr;
        if (!entry || entry->source->Type() != VType::Bool) {
            const std::wstring message = L"themeProperty '" + options.themeProperty +
                                         L"' is not a bool ViewModel property";
            if (selfTest) std::printf("SELFTEST FAIL: %ls\n", message.c_str());
            else MessageBoxW(nullptr, message.c_str(), L"Werfs sample", MB_ICONERROR);
            return 1;
        }
        entry->source->Subscribe(&g_themeObserver);
        Value initial;
        bool dark = false;
        if (entry->source->Read(initial) && ValueCodec<bool>::From(initial, dark)) {
            g.theme = dark ? ThemeKind::Dark : ThemeKind::Light;
        }
        if (selfTest) std::printf("theme property: %ls\n", options.themeProperty.c_str());
    }

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
    // Titles name the sample, not the build: the version belongs in VERSIONINFO
    // and the README, where it does not cover up the sample's own label.
    const std::wstring title = options.title;

    HWND hwnd = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, nullptr, nullptr,
                                instance, nullptr);
    if (!hwnd) {
        if (selfTest) std::printf("SELFTEST FAIL: CreateWindowExW %lu\n", GetLastError());
        return 1;
    }
    g.hwnd = hwnd;

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

void Shutdown() {
    // Order matters: bindings in the tree hold raw pointers into the ViewModel's
    // property sources, so the tree goes first.
    g.root.reset();
    g_accelerators.clear();
    g.vm.reset();
    g.hovered = nullptr;
    g.pressed = nullptr;
    g.focused = nullptr;
    g.overlayCapture = false;
    g.target.Reset();
    g.d2d.Reset();
    g.dwrite.Reset();
    g.brushes.clear();
    g.formats.clear();
    g.text = nullptr;
    g.hwnd = nullptr;
}

} // namespace sample