// SPDX-License-Identifier: MIT
#include "SoftwareRender.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wui {

namespace {

std::string Num(float v) {
    char buf[32];
    // Fixed precision keeps the recording stable across runs and compilers.
    snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s(buf);
    if (s == "-0.00") s = "0.00";
    return s;
}

std::string ColorText(const Color& c) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.3f", c.r, c.g, c.b, c.a);
    return buf;
}

std::string RectText(const Rect& r) {
    return Num(r.x) + " " + Num(r.y) + " " + Num(r.w) + " " + Num(r.h);
}

uint32_t PackColor(const Color& c) {
    const uint32_t a = static_cast<uint32_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
    const uint32_t r = static_cast<uint32_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    const uint32_t g = static_cast<uint32_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    const uint32_t b = static_cast<uint32_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

} // namespace

// ---------------------------------------------------------------------------
// RecordingPaintContext
// ---------------------------------------------------------------------------
void RecordingPaintContext::FillRect(const Rect& r, const Color& c) {
    text_ += "fill " + RectText(r) + " " + ColorText(c) + "\n";
    ++ops_;
}

void RecordingPaintContext::FillRoundRect(const Rect& r, float radius, const Color& c) {
    text_ += "fillrr " + RectText(r) + " r=" + Num(radius) + " " + ColorText(c) + "\n";
    ++ops_;
}

void RecordingPaintContext::StrokeRect(const Rect& r, const Color& c, float width, float radius) {
    text_ += "stroke " + RectText(r) + " w=" + Num(width) + " r=" + Num(radius) + " " + ColorText(c) +
             "\n";
    ++ops_;
}

void RecordingPaintContext::DrawText(const std::wstring& text, const Rect& r, const Color& c,
                                     float fontSize, int weight, TextAlign align) {
    text_ += "text \"" + WideToUtf8(text) + "\" " + RectText(r) + " size=" + Num(fontSize) +
             " weight=" + std::to_string(weight) + " align=" + std::to_string(static_cast<int>(align)) +
             " " + ColorText(c) + "\n";
    ++ops_;
}

void RecordingPaintContext::PushClip(const Rect& r) {
    text_ += "pushclip " + RectText(r) + "\n";
    ++ops_;
}

void RecordingPaintContext::PopClip() {
    text_ += "popclip\n";
    ++ops_;
}

// ---------------------------------------------------------------------------
// SoftwareRasterizer
// ---------------------------------------------------------------------------
SoftwareRasterizer::SoftwareRasterizer(int width, int height)
    : width_(std::max(1, width)), height_(std::max(1, height)),
      pixels_(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0xFF000000u) {}

void SoftwareRasterizer::Clear(uint32_t argb) { std::fill(pixels_.begin(), pixels_.end(), argb); }

void SoftwareRasterizer::FillRectF(const Rect& r, const Color& c) {
    const int x0 = std::max(0, static_cast<int>(std::floor(r.x + 0.5f)));
    const int y0 = std::max(0, static_cast<int>(std::floor(r.y + 0.5f)));
    const int x1 = std::min(width_, static_cast<int>(std::floor(r.x + r.w + 0.5f)));
    const int y1 = std::min(height_, static_cast<int>(std::floor(r.y + r.h + 0.5f)));
    const uint32_t src = PackColor(c);
    const uint32_t sa = (src >> 24) & 0xFF;
    const uint32_t sr = (src >> 16) & 0xFF;
    const uint32_t sg = (src >> 8) & 0xFF;
    const uint32_t sb = src & 0xFF;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint32_t& dst = pixels_[static_cast<size_t>(y) * width_ + x];
            if (sa == 255) {
                dst = src;
            } else {
                const uint32_t da = (dst >> 24) & 0xFF;
                const uint32_t dr = (dst >> 16) & 0xFF;
                const uint32_t dg = (dst >> 8) & 0xFF;
                const uint32_t db = dst & 0xFF;
                const uint32_t inv = 255 - sa;
                const uint32_t oa = sa + (da * inv) / 255;
                const uint32_t orr = (sr * sa + dr * inv) / 255;
                const uint32_t og = (sg * sa + dg * inv) / 255;
                const uint32_t ob = (sb * sa + db * inv) / 255;
                dst = (oa << 24) | (orr << 16) | (og << 8) | ob;
            }
        }
    }
}

uint64_t SoftwareRasterizer::Hash() const {
    uint64_t h = 1469598103934665603ull; // FNV-1a 64 offset basis
    for (uint32_t px : pixels_) {
        const uint8_t bytes[4] = {static_cast<uint8_t>(px >> 24), static_cast<uint8_t>(px >> 16),
                                  static_cast<uint8_t>(px >> 8), static_cast<uint8_t>(px)};
        for (uint8_t b : bytes) {
            h ^= b;
            h *= 1099511628211ull;
        }
    }
    return h;
}

// ---------------------------------------------------------------------------
// RasterPaintContext
// ---------------------------------------------------------------------------
void RasterPaintContext::FillRect(const Rect& r, const Color& c) { target_.FillRectF(r, c); }

void RasterPaintContext::FillRoundRect(const Rect& r, float, const Color& c) {
    target_.FillRectF(r, c);
}

void RasterPaintContext::StrokeRect(const Rect& r, const Color& c, float width, float) {
    const float w = std::max(1.0f, width);
    target_.FillRectF(Rect{r.x, r.y, r.w, w}, c);
    target_.FillRectF(Rect{r.x, r.y + r.h - w, r.w, w}, c);
    target_.FillRectF(Rect{r.x, r.y, w, r.h}, c);
    target_.FillRectF(Rect{r.x + r.w - w, r.y, w, r.h}, c);
}

void RasterPaintContext::DrawText(const std::wstring& text, const Rect& r, const Color& c, float fontSize,
                                  int, TextAlign align) {
    // A glyph-block placeholder: proportional to the deterministic fake metrics.
    const float advance = fontSize * 0.55f;
    const float total = advance * static_cast<float>(text.size());
    float x = r.x;
    if (align == TextAlign::Center) x = r.x + (r.w - total) * 0.5f;
    else if (align == TextAlign::Right) x = r.x + r.w - total;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L' ') continue;
        target_.FillRectF(Rect{x + static_cast<float>(i) * advance, r.y + (r.h - fontSize) * 0.5f,
                               advance * 0.7f, fontSize * 0.8f},
                          c);
    }
}

void RasterPaintContext::PushClip(const Rect& r) { clips_.push_back(r); }
void RasterPaintContext::PopClip() {
    if (!clips_.empty()) clips_.pop_back();
}

// ---------------------------------------------------------------------------
// FakeTextService
// ---------------------------------------------------------------------------
Size FakeTextService::Measure(const std::wstring& text, float fontSize, int weight) {
    // CJK glyphs are twice as wide as Latin ones; this keeps layout tests
    // meaningful without pulling in DWrite.
    float units = 0;
    for (wchar_t c : text) units += (c >= 0x2E80) ? 2.0f : 1.0f;
    const float weightScale = 1.0f + 0.02f * static_cast<float>(weight);
    return Size{units * fontSize * 0.55f * weightScale, fontSize * 1.33f};
}

} // namespace wui
