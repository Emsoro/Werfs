// SPDX-License-Identifier: MIT
// Captures one frame of the demo's real HWND render target to a PNG so the
// on-screen result (including DPI scaling) can be inspected without a screenshot.
#include "Controls.h"
#include "Core.h"
#include "D2DRender.h"
#include "Diagnostics.h"
#include "BasicVm.h"    // sample: the basic controls page's ViewModel
#include "Mvvm.h"
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#undef DrawText

using Microsoft::WRL::ComPtr;
using namespace wui;

namespace {

// Sample ViewModel, shared with the other tools.
using Vm = sample::BasicViewVm;

std::string ReadFile(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Ctx final : PaintCtx {
    ID2D1RenderTarget* rt;
    ThemePalette pal;
    std::map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes;
    std::map<std::wstring, ComPtr<IDWriteTextFormat>> formats;
    IDWriteFactory* dw = nullptr;

    ID2D1SolidColorBrush* Brush(const Color& c) {
        const uint32_t k = (uint32_t(c.a * 255) << 24) | (uint32_t(c.r * 255) << 16) |
                           (uint32_t(c.g * 255) << 8) | uint32_t(c.b * 255);
        auto it = brushes.find(k);
        if (it != brushes.end()) return it->second.Get();
        ComPtr<ID2D1SolidColorBrush> b;
        if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), b.GetAddressOf())))
            return nullptr;
        return brushes.emplace(k, b).first->second.Get();
    }
    IDWriteTextFormat* Format(float size, int weight) {
        const std::wstring k = std::to_wstring(int(size * 4)) + L"/" + std::to_wstring(weight);
        auto it = formats.find(k);
        if (it != formats.end()) return it->second.Get();
        ComPtr<IDWriteTextFormat> f;
        if (FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr,
                                        (DWRITE_FONT_WEIGHT)FontWeightToDWrite(weight),
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                        L"", f.GetAddressOf())))
            return nullptr;
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return formats.emplace(k, f).first->second.Get();
    }
    static D2D1_RECT_F R(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }

    void FillRect(const Rect& r, const Color& c) override {
        if (auto* b = Brush(ResolveColor(c, pal))) rt->FillRectangle(R(r), b);
    }
    void FillRoundRect(const Rect& r, float rad, const Color& c) override {
        if (auto* b = Brush(ResolveColor(c, pal))) rt->FillRoundedRectangle(D2D1::RoundedRect(R(r), rad, rad), b);
    }
    void StrokeRect(const Rect& r, const Color& c, float w, float rad) override {
        if (auto* b = Brush(ResolveColor(c, pal))) rt->DrawRoundedRectangle(D2D1::RoundedRect(R(r), rad, rad), b, w);
    }
    void FillEllipse(const Rect& r, const Color& c) override {
        if (auto* b = Brush(ResolveColor(c, pal))) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.x + r.w * 0.5f, r.y + r.h * 0.5f),
                                          r.w * 0.5f, r.h * 0.5f),
                            b);
        }
    }
    void DrawLine(const Point& a, const Point& b, const Color& c, float w) override {
        if (auto* brush = Brush(ResolveColor(c, pal))) {
            rt->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush, w);
        }
    }
    void DrawBitmap(const Bitmap* bitmap, const Rect& dest, const Rect* clip) override {
        // This tool renders the page at a chosen DPI for inspection; a picture failing to
        // draw here is acceptable (it never declares one), so this is intentionally a
        // no-op rather than a second copy of the premultiply-and-blit path.
        (void)bitmap;
        (void)dest;
        (void)clip;
    }
    void DrawText(const std::wstring& t, const Rect& r, const Color& c, float size, int weight,
                  TextAlign a) override {
        auto* b = Brush(ResolveColor(c, pal));
        auto* f = Format(size, weight);
        if (!b || !f) return;
        f->SetTextAlignment(a == TextAlign::Center   ? DWRITE_TEXT_ALIGNMENT_CENTER
                            : a == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                    : DWRITE_TEXT_ALIGNMENT_LEADING);
        rt->DrawTextW(t.c_str(), (UINT32)t.size(), f, R(r), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void PushClip(const Rect& r) override { rt->PushAxisAlignedClip(R(r), D2D1_ANTIALIAS_MODE_ALIASED); }
    void PopClip() override { rt->PopAxisAlignedClip(); }
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Same numbers as the demo window: 754x889 physical client at 200% DPI
    // means 377x444 DIP of layout area.
    const UINT dpi = GetDpiForSystem();
    const float scale = dpi / 96.0f;
    const int clientW = argc > 3 ? _wtoi(argv[3]) : 830;
    const int clientH = argc > 4 ? _wtoi(argv[4]) : 1088;
    std::printf("dpi=%u scale=%.2f client=%dx%d px -> %.0fx%.0f dip\n", dpi, scale, clientW, clientH,
                clientW / scale, clientH / scale);

    const std::string src = ReadFile(argc > 1 ? WideToUtf8(argv[1]) : "..\\samples\\basic\\Basic.waml");
    if (src.empty()) {
        std::printf("cannot read waml\n");
        return 1;
    }
    ParseOptions opt;
    opt.fileName = "Basic.waml";
    DiagnosticBag bag;
    auto doc = CompileWaml(src, opt, bag);
    if (!doc) {
        std::printf("%s", bag.ToText().c_str());
        return 1;
    }

    DWriteTextService text;
    TextSystem::Set(&text);
    auto vm = std::make_shared<Vm>();
    
    WidgetPtr root = doc->Instantiate(&text);
    root->SetDataContext(std::static_pointer_cast<RefObject>(vm));

    ComPtr<ID2D1Factory> d2d;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
    ComPtr<IDWriteFactory> dw;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(wic.GetAddressOf()));
    ComPtr<IWICBitmap> bmp;
    wic->CreateBitmap(clientW, clientH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                      bmp.GetAddressOf());
    ComPtr<ID2D1RenderTarget> rt;
    d2d->CreateWicBitmapRenderTarget(
        bmp.Get(),
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                       D2D1_ALPHA_MODE_PREMULTIPLIED),
                                     static_cast<float>(dpi), static_cast<float>(dpi)),
        rt.GetAddressOf());

    // Layout in DIPs, exactly like the window does.
    root->ForceLayout(Size{clientW / scale, clientH / scale});
    const ThemePalette pal = LightPalette();
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(pal.colors[(int)Token::AppBackground].r,
                           pal.colors[(int)Token::AppBackground].g,
                           pal.colors[(int)Token::AppBackground].b, 1.0f));
    Ctx ctx;
    ctx.rt = rt.Get();
    ctx.pal = pal;
    ctx.dw = dw.Get();
    root->PaintTree(ctx, pal);
    rt->EndDraw();

    const std::wstring out = argc > 2 ? argv[2] : L"demo-200pct.png";
    ComPtr<IWICStream> stream;
    wic->CreateStream(stream.GetAddressOf());
    stream->InitializeFromFilename(out.c_str(), GENERIC_WRITE);
    ComPtr<IWICBitmapEncoder> enc;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.GetAddressOf());
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf());
    frame->Initialize(props.Get());
    frame->SetSize(clientW, clientH);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WriteSource(bmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();
    std::printf("wrote %s\n", WideToUtf8(out).c_str());
    return 0;
}
