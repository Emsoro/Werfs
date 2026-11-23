// SPDX-License-Identifier: MIT
#include "D2DRender.h"

#include "Widget.h"

#include <windows.h>
#include <wincodec.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

// winuser.h maps DrawText to DrawTextW under UNICODE; our paint context uses
// the name as a plain virtual method, so drop the macro here.
#undef DrawText

using Microsoft::WRL::ComPtr;

namespace wui {

// ---------------------------------------------------------------------------
// COM / factory bootstrap
// ---------------------------------------------------------------------------
namespace {
struct ComScope {
    HRESULT hr = E_FAIL;
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};
} // namespace

D2D1_COLOR_F ToD2DColor(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

// ---------------------------------------------------------------------------
// DWriteTextService
// ---------------------------------------------------------------------------
struct DWriteTextService::Impl {
    ComPtr<IDWriteFactory> factory;
    std::map<std::wstring, ComPtr<IDWriteTextLayout>> layouts;
};

DWriteTextService::DWriteTextService() : impl_(std::make_unique<Impl>()) {
    ComScope com;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(impl_->factory.GetAddressOf()));
    if (FAILED(hr)) {
        impl_.reset();
        return;
    }
    factory_ = impl_->factory.Get();
}

DWriteTextService::~DWriteTextService() = default;

Size DWriteTextService::Measure(const std::wstring& text, float fontSize, int weight) {
    if (!impl_ || !impl_->factory) {
        return FakeTextService{}.Measure(text, fontSize, weight);
    }
    if (text.empty()) return Size{0, fontSize * 1.33f};
    const std::wstring key = text + L"|" + std::to_wstring(static_cast<int>(fontSize)) + L"|" +
                             std::to_wstring(weight);
    auto it = impl_->layouts.find(key);
    if (it == impl_->layouts.end()) {
        if (impl_->layouts.size() > 512) impl_->layouts.clear(); // Simple LRU-ish bound.
        ComPtr<IDWriteTextFormat> format;
        HRESULT hr = impl_->factory->CreateTextFormat(L"Segoe UI", nullptr,
                                                      static_cast<DWRITE_FONT_WEIGHT>(FontWeightToDWrite(weight)),
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                      fontSize, L"", format.GetAddressOf());
        if (FAILED(hr)) return FakeTextService{}.Measure(text, fontSize, weight);
        ComPtr<IDWriteTextLayout> layout;
        hr = impl_->factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                              format.Get(), 10000.0f, 1000.0f, layout.GetAddressOf());
        if (FAILED(hr)) return FakeTextService{}.Measure(text, fontSize, weight);
        it = impl_->layouts.emplace(key, layout).first;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(it->second->GetMetrics(&metrics))) return FakeTextService{}.Measure(text, fontSize, weight);
    return Size{metrics.width, metrics.height};
}

float DWriteTextService::LineHeight(float fontSize) { return fontSize * 1.33f; }

// ---------------------------------------------------------------------------
// D2DSnapshotRenderer
// ---------------------------------------------------------------------------
struct D2DSnapshotRenderer::Impl {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<IDWriteFactory> dwrite;
    std::map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes;
    std::map<std::wstring, ComPtr<IDWriteTextFormat>> formats;
    int width = 0;
    int height = 0;
    ThemeKind theme = ThemeKind::Light;
    std::string error;

    bool Init(int w, int h) {
        width = w;
        height = h;
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
        if (FAILED(hr)) {
            error = "D2D1CreateFactory failed";
            return false;
        }
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(wic.GetAddressOf()));
        if (FAILED(hr)) {
            error = "WIC factory creation failed";
            return false;
        }
        hr = wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapCacheOnLoad, bitmap.GetAddressOf());
        if (FAILED(hr)) {
            error = "WIC CreateBitmap failed";
            return false;
        }
        hr = d2d->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                       D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                         D2D1_ALPHA_MODE_PREMULTIPLIED)),
            target.GetAddressOf());
        if (FAILED(hr)) {
            error = "CreateWicBitmapRenderTarget failed";
            return false;
        }
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
        if (FAILED(hr)) {
            error = "DWrite create failed";
            return false;
        }
        return true;
    }

    ID2D1SolidColorBrush* Brush(const Color& c) {
        const uint32_t key = (static_cast<uint32_t>(c.a * 255.0f + 0.5f) << 24) |
                             (static_cast<uint32_t>(c.r * 255.0f + 0.5f) << 16) |
                             (static_cast<uint32_t>(c.g * 255.0f + 0.5f) << 8) |
                             static_cast<uint32_t>(c.b * 255.0f + 0.5f);
        auto it = brushes.find(key);
        if (it != brushes.end()) return it->second.Get();
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(target->CreateSolidColorBrush(ToD2DColor(c), brush.GetAddressOf()))) return nullptr;
        auto inserted = brushes.emplace(key, brush).first;
        return inserted->second.Get();
    }

    IDWriteTextFormat* Format(float fontSize, int weight) {
        const std::wstring key =
            std::to_wstring(static_cast<int>(fontSize * 4)) + L"/" + std::to_wstring(weight);
        auto it = formats.find(key);
        if (it != formats.end()) return it->second.Get();
        ComPtr<IDWriteTextFormat> format;
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, static_cast<DWRITE_FONT_WEIGHT>(FontWeightToDWrite(weight)),
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"",
                format.GetAddressOf())))
            return nullptr;
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        auto inserted = formats.emplace(key, format).first;
        return inserted->second.Get();
    }
};

namespace {

class D2DPaintContext final : public PaintCtx {
public:
    D2DPaintContext(D2DSnapshotRenderer::Impl& impl, const ThemePalette& palette)
        : impl_(impl), palette_(palette) {}

    void FillRect(const Rect& r, const Color& c) override {
        auto* brush = impl_.Brush(ResolveColor(c, palette_));
        if (!brush) return;
        impl_.target->FillRectangle(RectF(r), brush);
    }

    void FillRoundRect(const Rect& r, float radius, const Color& c) override {
        auto* brush = impl_.Brush(ResolveColor(c, palette_));
        if (!brush) return;
        impl_.target->FillRoundedRectangle(D2D1::RoundedRect(RectF(r), radius, radius), brush);
    }

    void StrokeRect(const Rect& r, const Color& c, float width, float radius) override {
        auto* brush = impl_.Brush(ResolveColor(c, palette_));
        if (!brush) return;
        impl_.target->DrawRoundedRectangle(D2D1::RoundedRect(RectF(r), radius, radius), brush, width);
    }

    void FillEllipse(const Rect& r, const Color& c) override {
        auto* brush = impl_.Brush(ResolveColor(c, palette_));
        if (!brush) return;
        impl_.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.x + r.w * 0.5f, r.y + r.h * 0.5f),
                                                r.w * 0.5f, r.h * 0.5f),
                                  brush);
    }

    void DrawBitmap(const Bitmap* bitmap, const Rect& dest, const Rect* clip) override {
        if (bitmap == nullptr || !bitmap->Valid()) return;
        ID2D1RenderTarget* target = impl_.target.Get();
        if (target == nullptr) return;
        // Built per call: images are painted rarely compared to text, and caching
        // D2D bitmaps per source would need a lifetime story the service already owns.
        ComPtr<ID2D1Bitmap> d2dBitmap;
        const D2D1_BITMAP_PROPERTIES props =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                     D2D1_ALPHA_MODE_PREMULTIPLIED));
        // The decoder hands out straight (non-premultiplied) alpha; D2D wants
        // premultiplied, so convert on the way in. Getting this wrong shows up as bright
        // fringes around transparent edges.
        std::vector<uint32_t> premul(bitmap->pixels.size());
        for (size_t i = 0; i < bitmap->pixels.size(); ++i) {
            const uint32_t p = bitmap->pixels[i];
            const uint32_t a = (p >> 24) & 0xFF;
            const uint32_t r = ((p >> 16) & 0xFF) * a / 255;
            const uint32_t g = ((p >> 8) & 0xFF) * a / 255;
            const uint32_t b = (p & 0xFF) * a / 255;
            premul[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        if (FAILED(target->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(bitmap->width),
                                                    static_cast<UINT32>(bitmap->height)),
                                        premul.data(),
                                        static_cast<UINT32>(bitmap->width) * 4, props,
                                        d2dBitmap.GetAddressOf()))) {
            return;
        }
        const D2D1_RECT_F dst = RectF(dest);
        // A circular avatar needs the bitmap masked to an ellipse. D2D does that with a
        // layer + geometry mask rather than a clip, because an elliptical clip is not the
        // same thing as an axis-aligned one -- clipping to the bounding box would leave
        // the corners of the picture visible.
        if (clip != nullptr) {
            ComPtr<ID2D1Factory> factory;
            target->GetFactory(factory.GetAddressOf());
            if (factory) {
                ComPtr<ID2D1EllipseGeometry> ellipse;
                const D2D1_ELLIPSE e =
                    D2D1::Ellipse(D2D1::Point2F(dst.left + (dst.right - dst.left) * 0.5f,
                                                dst.top + (dst.bottom - dst.top) * 0.5f),
                                  (dst.right - dst.left) * 0.5f,
                                  (dst.bottom - dst.top) * 0.5f);
                if (SUCCEEDED(factory->CreateEllipseGeometry(e, ellipse.GetAddressOf()))) {
                    ComPtr<ID2D1Layer> layer;
                    D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters();
                    params.geometricMask = ellipse.Get();
                    params.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
                    params.contentBounds = dst;
                    if (SUCCEEDED(target->CreateLayer(nullptr, layer.GetAddressOf()))) {
                        target->PushLayer(params, layer.Get());
                        target->DrawBitmap(d2dBitmap.Get(), dst, 1.0f,
                                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                        target->PopLayer();
                        return;
                    }
                }
            }
        }
        target->DrawBitmap(d2dBitmap.Get(), dst, 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    void DrawLine(const Point& a, const Point& b, const Color& c, float width) override {
        auto* brush = impl_.Brush(ResolveColor(c, palette_));
        if (!brush) return;
        impl_.target->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush, width);
    }

    void DrawText(const std::wstring& text, const Rect& r, const Color& c, float fontSize, int weight,
                  TextAlign align) override {
        auto* brush = impl_.Brush(ResolveColor(c, palette_));
        auto* format = impl_.Format(fontSize, weight);
        if (!brush || !format) return;
        format->SetTextAlignment(align == TextAlign::Center   ? DWRITE_TEXT_ALIGNMENT_CENTER
                                 : align == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                             : DWRITE_TEXT_ALIGNMENT_LEADING);
        impl_.target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, RectF(r), brush,
                                D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void PushClip(const Rect& r) override { impl_.target->PushAxisAlignedClip(RectF(r), D2D1_ANTIALIAS_MODE_ALIASED); }
    void PopClip() override { impl_.target->PopAxisAlignedClip(); }

private:
    static D2D1_RECT_F RectF(const Rect& r) {
        return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
    }

    D2DSnapshotRenderer::Impl& impl_;
    const ThemePalette& palette_;
};

} // namespace

// COM is process-wide and refcounted. Initialise once and never uninitialise
// while any COM smart pointer may still be alive: pairing CoInitializeEx with
// CoUninitialize per instance tore COM down before the previous instance's
// ComPtr members ran their destructors (observed as an access violation when
// creating the second renderer).
namespace {
void EnsureCom() {
    static const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    (void)hr;
}
} // namespace

D2DSnapshotRenderer::D2DSnapshotRenderer(int width, int height, ThemeKind theme)
    : impl_(std::make_unique<Impl>()) {
    impl_->theme = theme;
    EnsureCom();
    impl_->Init(width, height);
}

D2DSnapshotRenderer::~D2DSnapshotRenderer() = default;

bool D2DSnapshotRenderer::Valid() const { return impl_ && impl_->target && impl_->error.empty(); }
const char* D2DSnapshotRenderer::ErrorMessage() const {
    return impl_ ? impl_->error.c_str() : "no impl";
}

bool D2DSnapshotRenderer::Render(Widget& root, std::vector<uint32_t>& pixels) {
    if (!Valid()) return false;
    const ThemePalette palette = MakePalette(impl_->theme);
    impl_->target->BeginDraw();
    // Premultiplied target: clear to the page background so alpha==255 everywhere.
    impl_->target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    D2DPaintContext ctx(*impl_, palette);
    root.PaintTree(ctx, palette);
    const HRESULT hr = impl_->target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) return false;
    if (FAILED(hr)) return false;

    // Copy pixels out through a WIC lock.
    ComPtr<IWICBitmapLock> lock;
    WICRect rect{0, 0, impl_->width, impl_->height};
    if (FAILED(impl_->bitmap->Lock(&rect, WICBitmapLockRead, lock.GetAddressOf()))) return false;
    UINT stride = 0;
    UINT size = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &data))) return false;
    pixels.resize(static_cast<size_t>(impl_->width) * static_cast<size_t>(impl_->height));
    for (int y = 0; y < impl_->height; ++y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < impl_->width; ++x) {
            const uint32_t bgra = row[x];
            // Reorder to 0xAARRGGBB for a stable hash.
            const uint32_t a = (bgra >> 24) & 0xFF;
            const uint32_t r = (bgra >> 16) & 0xFF;
            const uint32_t g = (bgra >> 8) & 0xFF;
            const uint32_t b = bgra & 0xFF;
            pixels[static_cast<size_t>(y) * impl_->width + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return true;
}

uint64_t D2DSnapshotRenderer::HashPixels(const std::vector<uint32_t>& pixels) const {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t px : pixels) {
        for (int i = 0; i < 4; ++i) {
            h ^= (px >> (i * 8)) & 0xFF;
            h *= 1099511628211ull;
        }
    }
    return h;
}

bool D2DSnapshotRenderer::SavePng(const std::wstring& path) {
    if (!Valid()) return false;
    ComPtr<IWICStream> stream;
    if (FAILED(impl_->wic->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(impl_->wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())))
        return false;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf()))) return false;
    if (FAILED(frame->Initialize(props.Get()))) return false;
    if (FAILED(frame->SetSize(static_cast<UINT>(impl_->width), static_cast<UINT>(impl_->height))))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(frame->SetPixelFormat(&format))) return false;
    if (FAILED(frame->WriteSource(impl_->bitmap.Get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    if (FAILED(encoder->Commit())) return false;
    return true;
}

bool D2DSnapshotRenderer::VerifyPng(const std::wstring& path, int& width, int& height) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic.GetAddressOf()))))
        return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return false;
    UINT w = 0;
    UINT h = 0;
    if (FAILED(frame->GetSize(&w, &h))) return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

} // namespace wui
