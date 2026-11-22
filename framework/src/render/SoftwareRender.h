// SPDX-License-Identifier: MIT
// Deterministic software "renderer" used by snapshot tests. It records every
// drawing primitive into a stable text stream so that golden files are
// human-diffable, and it also rasterizes into an RGBA buffer for D2D/WIC
// comparison in the spike.
#pragma once

#include "Core.h"

#include <string>
#include <vector>

namespace wui {

// Ordered record of drawing calls, byte-for-byte reproducible.
class RecordingPaintContext final : public PaintCtx {
public:
    void FillRect(const Rect& r, const Color& c) override;
    void FillRoundRect(const Rect& r, float radius, const Color& c) override;
    void StrokeRect(const Rect& r, const Color& c, float width, float radius) override;
    void DrawText(const std::wstring& text, const Rect& r, const Color& c, float fontSize, int weight,
                  TextAlign align) override;
    void PushClip(const Rect& r) override;
    void PopClip() override;

    const std::string& Text() const { return text_; }
    size_t OpCount() const { return ops_; }
    void Clear() {
        text_.clear();
        ops_ = 0;
    }

private:
    std::string text_;
    size_t ops_ = 0;
};

// Minimal software rasterizer: solid fills only. Enough to prove that the
// render abstraction can produce a pixel-identical image across runs, which is
// what a CI snapshot gate needs. (The production target is D2D; see ADR.)
class SoftwareRasterizer {
public:
    SoftwareRasterizer(int width, int height);
    void Clear(uint32_t argb);
    void FillRectF(const Rect& r, const Color& c);
    // FNV-1a over the pixel buffer: cheap, stable across runs and machines.
    uint64_t Hash() const;
    const std::vector<uint32_t>& Pixels() const { return pixels_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    int width_;
    int height_;
    std::vector<uint32_t> pixels_;
};

class RasterPaintContext final : public PaintCtx {
public:
    explicit RasterPaintContext(SoftwareRasterizer& target) : target_(target) {}
    void FillRect(const Rect& r, const Color& c) override;
    void FillRoundRect(const Rect& r, float radius, const Color& c) override;
    void StrokeRect(const Rect& r, const Color& c, float width, float radius) override;
    void DrawText(const std::wstring& text, const Rect& r, const Color& c, float fontSize, int weight,
                  TextAlign align) override;
    void PushClip(const Rect& r) override;
    void PopClip() override;

private:
    SoftwareRasterizer& target_;
    std::vector<Rect> clips_;
};

// Deterministic text metrics for headless tests (no DWrite).
class FakeTextService final : public ITextService {
public:
    Size Measure(const std::wstring& text, float fontSize, int weight) override;
    float LineHeight(float fontSize) override { return fontSize * 1.33f; }
};

} // namespace wui
