// SPDX-License-Identifier: MIT
//
// WIC-backed image decoding.
//
// WIC (Windows Imaging Component) rather than a hand-rolled PNG reader: it decodes every
// format Windows itself supports (PNG, JPEG, BMP, GIF, TIFF, ICO) and it is already on
// every target machine. The alternative is a vendored decoder for one format, which is
// more code and fewer formats.
#include <windows.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <map>

#include "Image.h"

namespace wui {
namespace {

using Microsoft::WRL::ComPtr;

// Converts a UTF-8 path to UTF-16 for the Win32 layer. The UI deals in UTF-8 strings
// (that is what the WAML document contains), while the platform deals in wide strings.
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

class WicImageService final : public IImageService {
public:
    WicImageService() = default;
    ~WicImageService() override = default;

    std::shared_ptr<const Bitmap> Load(const std::string& source) override {
        // Cache first. A picture referenced from several places in one document is
        // decoded once, which is the only caching this needs: a UI's images are few and
        // live as long as the app.
        auto it = cache_.find(source);
        if (it != cache_.end()) return it->second;

        auto bitmap = Decode(source);
        if (bitmap) cache_[source] = bitmap;
        // A FAILED load is not cached as nullptr: the file may appear later (the user
        // saves it and triggers a re-layout), and caching the failure would make that
        // permanent for the process lifetime.
        return bitmap;
    }

private:
    std::shared_ptr<const Bitmap> Decode(const std::string& source) {
        if (factory_ == nullptr) {
            ComPtr<IWICImagingFactory> factory;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(factory.GetAddressOf())))) {
                return nullptr;
            }
            factory_ = factory;
        }

        const std::wstring path = Widen(source);
        if (path.empty()) return nullptr;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnDemand,
                                                       decoder.GetAddressOf()))) {
            return nullptr;
        }
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return nullptr;

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory_->CreateFormatConverter(converter.GetAddressOf()))) return nullptr;
        // 32bppBGRA is the one format guaranteed to convert from anything, and it maps
        // straight onto the painter's 0xAARRGGBB word with a channel swap.
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom))) {
            return nullptr;
        }

        UINT w = 0;
        UINT h = 0;
        if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0) return nullptr;
        // A guard against a decompression bomb or a corrupt header claiming 20k x 20k:
        // MAX_PATH-sized images are plenty for a UI, and refusing beats an OOM abort.
        if (w > 8192 || h > 8192) return nullptr;

        auto bitmap = std::make_shared<Bitmap>();
        bitmap->width = static_cast<int>(w);
        bitmap->height = static_cast<int>(h);
        bitmap->pixels.resize(static_cast<size_t>(w) * h);

        const UINT stride = w * 4;
        const UINT size = stride * h;
        if (FAILED(converter->CopyPixels(nullptr, stride, size,
                                         reinterpret_cast<BYTE*>(bitmap->pixels.data())))) {
            return nullptr;
        }
        return bitmap;
    }

    ComPtr<IWICImagingFactory> factory_;
    std::map<std::string, std::shared_ptr<const Bitmap>> cache_;
};

} // namespace

IImageService* CreateWicImageService() { return new WicImageService(); }

} // namespace wui
