// SPDX-License-Identifier: MIT
// Generates the application icon as a multi-resolution .ico.
//
// Written as a build-time tool instead of committing an opaque binary: the
// artwork is a handful of numbers, so it stays reviewable and can be tweaked
// without an image editor. Produces 16/20/24/32/40/48/64/256 px frames, each
// drawn from signed-distance shapes and downsampled with box filtering so the
// small sizes stay legible.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

// Premultiplied-free "over" compositing.
Rgba Over(const Rgba& src, const Rgba& dst) {
    const float sa = src.a / 255.0f;
    const float da = dst.a / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f) return Rgba{};
    auto blend = [&](uint8_t s, uint8_t d) {
        const float v = (s * sa + d * da * (1.0f - sa)) / oa;
        return static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
    };
    Rgba out;
    out.r = blend(src.r, dst.r);
    out.g = blend(src.g, dst.g);
    out.b = blend(src.b, dst.b);
    out.a = static_cast<uint8_t>(std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
    return out;
}

// Fractional coverage of a rounded rectangle at one pixel centre.
float RoundRectCoverage(float px, float py, float x0, float y0, float x1, float y1, float radius) {
    const float halfW = (x1 - x0) * 0.5f;
    const float halfH = (y1 - y0) * 0.5f;
    const float cx = (x0 + x1) * 0.5f;
    const float cy = (y0 + y1) * 0.5f;
    const float qx = std::fabs(px - cx) - (halfW - radius);
    const float qy = std::fabs(py - cy) - (halfH - radius);
    const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                                    std::max(qy, 0.0f) * std::max(qy, 0.0f));
    const float d = outside + std::min(std::max(qx, qy), 0.0f) - radius;
    // Convert signed distance to ~1px antialiased coverage.
    return std::clamp(0.5f - d, 0.0f, 1.0f);
}

// Draws the icon at `size` x `size`. The design: a rounded card holding three
// stacked "WAML element" bars, with an accent bar on top -- a markup document.
std::vector<Rgba> Render(int size) {
    const float s = static_cast<float>(size);
    std::vector<Rgba> px(static_cast<size_t>(size) * size);
    const Rgba clear{0, 0, 0, 0};
    for (auto& p : px) p = clear;

    const Rgba accent{0x00, 0x67, 0xC0, 0xFF};
    const Rgba card{0xFF, 0xFF, 0xFF, 0xFF};
    const Rgba barDark{0x1A, 0x1A, 0x1A, 0xFF};
    const Rgba barLight{0x8A, 0x8A, 0x8A, 0xFF};

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float px0 = static_cast<float>(x) + 0.5f;
            const float py0 = static_cast<float>(y) + 0.5f;
            Rgba dst = clear;

            // Card: inset rounded square.
            const float inset = s * 0.06f;
            const float cardCov =
                RoundRectCoverage(px0, py0, inset, inset, s - inset, s - inset, s * 0.18f);
            if (cardCov > 0.0f) {
                Rgba c = card;
                c.a = static_cast<uint8_t>(cardCov * 255.0f);
                dst = Over(c, dst);
            }

            // Accent header bar.
            const float hx0 = s * 0.20f;
            const float hx1 = s * 0.80f;
            const float hy0 = s * 0.22f;
            const float hy1 = s * 0.38f;
            const float headCov = RoundRectCoverage(px0, py0, hx0, hy0, hx1, hy1, s * 0.055f);
            if (headCov > 0.0f) {
                Rgba c = accent;
                c.a = static_cast<uint8_t>(headCov * 255.0f);
                dst = Over(c, dst);
            }

            // Three text bars of decreasing width.
            struct Bar {
                float y0, y1, x1;
                bool dark;
            };
            const Bar bars[3] = {
                {0.48f, 0.55f, 0.80f, true},
                {0.62f, 0.69f, 0.68f, false},
                {0.76f, 0.83f, 0.56f, false},
            };
            for (const auto& bar : bars) {
                const float by0 = s * bar.y0;
                const float by1 = s * bar.y1;
                const float bx0 = s * 0.20f;
                const float bx1 = s * bar.x1;
                const float cov = RoundRectCoverage(px0, py0, bx0, by0, bx1, by1, (by1 - by0) * 0.5f);
                if (cov > 0.0f) {
                    Rgba c = bar.dark ? barDark : barLight;
                    c.a = static_cast<uint8_t>(cov * 255.0f);
                    dst = Over(c, dst);
                }
            }
            px[static_cast<size_t>(y) * size + x] = dst;
        }
    }
    return px;
}

// Super-samples by rendering at 4x and box-filtering down.
std::vector<Rgba> RenderAA(int size) {
    const int big = size * 4;
    const std::vector<Rgba> hi = Render(big);
    std::vector<Rgba> out(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 4; ++dx) {
                    const Rgba& p = hi[static_cast<size_t>(y * 4 + dy) * big + (x * 4 + dx)];
                    const float pa = p.a / 255.0f;
                    // Weight colour by alpha so edges do not darken.
                    r += p.r * pa;
                    g += p.g * pa;
                    b += p.b * pa;
                    a += pa;
                }
            }
            Rgba o;
            if (a > 0.0f) {
                o.r = static_cast<uint8_t>(std::clamp(r / a + 0.5f, 0.0f, 255.0f));
                o.g = static_cast<uint8_t>(std::clamp(g / a + 0.5f, 0.0f, 255.0f));
                o.b = static_cast<uint8_t>(std::clamp(b / a + 0.5f, 0.0f, 255.0f));
            }
            o.a = static_cast<uint8_t>(std::clamp(a / 16.0f * 255.0f + 0.5f, 0.0f, 255.0f));
            out[static_cast<size_t>(y) * size + x] = o;
        }
    }
    return out;
}

void WriteLE16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void WriteLE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

// 32bpp BMP (BITMAPINFOHEADER + bottom-up BGRA + AND mask), as .ico requires.
std::vector<uint8_t> EncodeDib(const std::vector<Rgba>& px, int size) {
    std::vector<uint8_t> dib;
    const uint32_t maskStride = ((static_cast<uint32_t>(size) + 31) / 32) * 4;
    const uint32_t imageSize = static_cast<uint32_t>(size) * static_cast<uint32_t>(size) * 4;

    WriteLE32(dib, 40);              // biSize
    WriteLE32(dib, static_cast<uint32_t>(size)); // biWidth
    WriteLE32(dib, static_cast<uint32_t>(size) * 2); // biHeight (XOR + AND)
    WriteLE16(dib, 1);               // biPlanes
    WriteLE16(dib, 32);              // biBitCount
    WriteLE32(dib, 0);               // biCompression = BI_RGB
    WriteLE32(dib, imageSize + maskStride * static_cast<uint32_t>(size));
    WriteLE32(dib, 0);
    WriteLE32(dib, 0);
    WriteLE32(dib, 0);
    WriteLE32(dib, 0);

    // XOR bitmap, bottom-up BGRA.
    for (int y = size - 1; y >= 0; --y) {
        for (int x = 0; x < size; ++x) {
            const Rgba& p = px[static_cast<size_t>(y) * size + x];
            dib.push_back(p.b);
            dib.push_back(p.g);
            dib.push_back(p.r);
            dib.push_back(p.a);
        }
    }
    // AND mask: all zero (alpha channel carries transparency).
    for (int y = 0; y < size; ++y) {
        for (uint32_t b = 0; b < maskStride; ++b) dib.push_back(0);
    }
    return dib;
}

// 256px frames are stored as PNG inside the .ico to keep the file small.
std::vector<uint8_t> EncodePng(const std::vector<Rgba>& px, int size);

} // namespace

#include <objidl.h>
#include <gdiplus.h>

int wmain(int argc, wchar_t** argv) {
    const std::wstring outPath = argc > 1 ? argv[1] : L"app.ico";
    setvbuf(stdout, nullptr, _IONBF, 0);

    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&token, &gdiInput, nullptr);

    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 256};
    struct Frame {
        int size;
        std::vector<uint8_t> data;
    };
    std::vector<Frame> frames;
    for (int size : sizes) {
        const std::vector<Rgba> px = RenderAA(size);
        if (size == 256) {
            frames.push_back({size, EncodePng(px, size)});
        } else {
            frames.push_back({size, EncodeDib(px, size)});
        }
        std::printf("frame %d: %zu bytes%s\n", size, frames.back().data.size(),
                    size == 256 ? " (png)" : " (dib)");
    }

    // ICO container.
    std::vector<uint8_t> ico;
    WriteLE16(ico, 0); // reserved
    WriteLE16(ico, 1); // type = icon
    WriteLE16(ico, static_cast<uint16_t>(frames.size()));
    uint32_t offset = 6 + 16 * static_cast<uint32_t>(frames.size());
    for (const auto& f : frames) {
        ico.push_back(f.size >= 256 ? 0 : static_cast<uint8_t>(f.size));
        ico.push_back(f.size >= 256 ? 0 : static_cast<uint8_t>(f.size));
        ico.push_back(0); // palette
        ico.push_back(0); // reserved
        WriteLE16(ico, 1);  // planes
        WriteLE16(ico, 32); // bpp
        WriteLE32(ico, static_cast<uint32_t>(f.data.size()));
        WriteLE32(ico, offset);
        offset += static_cast<uint32_t>(f.data.size());
    }
    for (const auto& f : frames) ico.insert(ico.end(), f.data.begin(), f.data.end());

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, outPath.c_str(), L"wb") != 0 || !fp) {
        std::printf("cannot open %ls\n", outPath.c_str());
        Gdiplus::GdiplusShutdown(token);
        return 1;
    }
    fwrite(ico.data(), 1, ico.size(), fp);
    fclose(fp);
    std::printf("wrote %ls: %zu bytes, %zu frames\n", outPath.c_str(), ico.size(), frames.size());

    Gdiplus::GdiplusShutdown(token);
    return 0;
}

namespace {

// GDI+ PNG encoding into a memory stream.
class MemStream final : public IStream {
public:
    std::vector<uint8_t> bytes;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IStream) || riid == __uuidof(ISequentialStream) || riid == __uuidof(IUnknown)) {
            *ppv = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* read) override {
        const size_t avail = bytes.size() - std::min(pos_, bytes.size());
        const size_t n = std::min<size_t>(cb, avail);
        if (n) memcpy(pv, bytes.data() + pos_, n);
        pos_ += n;
        if (read) *read = static_cast<ULONG>(n);
        return n == cb ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* pv, ULONG cb, ULONG* written) override {
        const auto* p = static_cast<const uint8_t*>(pv);
        if (pos_ + cb > bytes.size()) bytes.resize(pos_ + cb);
        memcpy(bytes.data() + pos_, p, cb);
        pos_ += cb;
        if (written) *written = cb;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos) override {
        int64_t base = 0;
        if (origin == STREAM_SEEK_CUR) base = static_cast<int64_t>(pos_);
        else if (origin == STREAM_SEEK_END) base = static_cast<int64_t>(bytes.size());
        const int64_t target = base + move.QuadPart;
        if (target < 0) return STG_E_INVALIDFUNCTION;
        pos_ = static_cast<size_t>(target);
        if (newPos) newPos->QuadPart = pos_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) override {
        bytes.resize(size.QuadPart);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return E_POINTER;
        memset(stat, 0, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = bytes.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

private:
    size_t pos_ = 0;
    ULONG refs_ = 1;
};

std::vector<uint8_t> EncodePng(const std::vector<Rgba>& px, int size) {
    // Build a 32bpp BGRA GDI+ bitmap directly from our buffer.
    Gdiplus::Bitmap bmp(size, size, PixelFormat32bppARGB);
    Gdiplus::Rect rect(0, 0, size, size);
    Gdiplus::BitmapData locked{};
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &locked) != Gdiplus::Ok)
        return {};
    for (int y = 0; y < size; ++y) {
        auto* row = static_cast<uint32_t*>(static_cast<void*>(static_cast<uint8_t*>(locked.Scan0) +
                                                             static_cast<ptrdiff_t>(y) * locked.Stride));
        for (int x = 0; x < size; ++x) {
            const Rgba& p = px[static_cast<size_t>(y) * size + x];
            row[x] = (static_cast<uint32_t>(p.a) << 24) | (static_cast<uint32_t>(p.r) << 16) |
                     (static_cast<uint32_t>(p.g) << 8) | p.b;
        }
    }
    bmp.UnlockBits(&locked);

    CLSID pngClsid{};
    {
        // PNG encoder CLSID without linking gdiplus.lib's helper tables.
        static const CLSID kPng = {0x557cf406,
                                   0x1a04,
                                   0x11d3,
                                   {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
        pngClsid = kPng;
    }
    auto* stream = new MemStream();
    std::vector<uint8_t> out;
    if (bmp.Save(stream, &pngClsid, nullptr) == Gdiplus::Ok) out = stream->bytes;
    stream->Release();
    return out;
}

} // namespace
