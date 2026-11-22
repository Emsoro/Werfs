// SPDX-License-Identifier: MIT
// Real backend: DirectWrite text metrics + Direct2D rendering into a WIC
// bitmap. Used by the snapshot tool to prove the abstraction works with the
// production graphics stack (no HWND required).
#pragma once

#include "Core.h"
#include "SoftwareRender.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wui {

// DirectWrite-backed text service.
class DWriteTextService final : public ITextService {
public:
    DWriteTextService();
    ~DWriteTextService() override;
    bool Valid() const { return factory_ != nullptr; }

    Size Measure(const std::wstring& text, float fontSize, int weight) override;
    float LineHeight(float fontSize) override;

    struct Impl;
    Impl* GetImpl() { return impl_.get(); }

private:
    struct ImplHandle;
    std::unique_ptr<struct Impl> impl_;
    void* factory_ = nullptr;
};

// Direct2D renderer that draws a widget tree into a WIC bitmap and saves PNG.
class D2DSnapshotRenderer {
public:
    D2DSnapshotRenderer(int width, int height, ThemeKind theme);
    ~D2DSnapshotRenderer();
    bool Valid() const;
    const char* ErrorMessage() const;

    // Renders and returns the raw BGRA pixels (top-down).
    bool Render(class Widget& root, std::vector<uint32_t>& pixels);
    bool SavePng(const std::wstring& path);
    uint64_t HashPixels(const std::vector<uint32_t>& pixels) const;
    // Returns true when WIC reports the saved file can be decoded.
    static bool VerifyPng(const std::wstring& path, int& width, int& height);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace wui
