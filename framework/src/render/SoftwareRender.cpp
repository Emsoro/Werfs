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

// Render state helpers shared by the recording and raster painters. Opacity
// multiplies the alpha of literal colours; a token colour is left for the real
// backend to resolve, so its alpha must not be changed here.
Color TintColor(const std::vector<float>& opacities, Color c) {
    if (c.token != Token::None) return c;
    for (float o : opacities) c.a *= o;
    return c;
}

Point MapPoint(const std::vector<PaintTransform>& transforms, Point p) {
    for (const auto& t : transforms) p = t.Apply(p);
    return p;
}

Rect MapRect(const std::vector<PaintTransform>& transforms, Rect r) {
    for (const auto& t : transforms) r = t.ApplyRect(r);
    return r;
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
    text_ += "fill " + RectText(MapRect(transforms_, r)) + " " +
             ColorText(TintColor(opacities_, c)) + "\n";
    ++ops_;
}

void RecordingPaintContext::FillRoundRect(const Rect& r, float radius, const Color& c) {
    text_ += "fillrr " + RectText(MapRect(transforms_, r)) + " r=" + Num(radius) + " " +
             ColorText(TintColor(opacities_, c)) + "\n";
    ++ops_;
}

void RecordingPaintContext::StrokeRect(const Rect& r, const Color& c, float width, float radius) {
    text_ += "stroke " + RectText(MapRect(transforms_, r)) + " w=" + Num(width) + " r=" + Num(radius) +
             " " + ColorText(TintColor(opacities_, c)) + "\n";
    ++ops_;
}

void RecordingPaintContext::FillEllipse(const Rect& r, const Color& c) {
    text_ += "ellipse " + RectText(MapRect(transforms_, r)) + " " +
             ColorText(TintColor(opacities_, c)) + "\n";
    ++ops_;
}

void RecordingPaintContext::DrawLine(const Point& a, const Point& b, const Color& c, float width) {
    const Point p0 = MapPoint(transforms_, a);
    const Point p1 = MapPoint(transforms_, b);
    text_ += "line (" + Num(p0.x) + "," + Num(p0.y) + ")-(" + Num(p1.x) + "," + Num(p1.y) +
             ") w=" + Num(width) + " " + ColorText(TintColor(opacities_, c)) + "\n";
    ++ops_;
}

void RecordingPaintContext::DrawText(const std::wstring& text, const Rect& r, const Color& c,
                                 float fontSize, int weight, TextAlign align,
                                 const std::wstring& family) {
    const std::string fontPart = family.empty() ? std::string() : (" font=" + WideToUtf8(family));
    text_ += "text \"" + WideToUtf8(text) + "\" " + RectText(MapRect(transforms_, r)) +
             " size=" + Num(fontSize) +
             " weight=" + std::to_string(weight) + " align=" + std::to_string(static_cast<int>(align)) +
             fontPart + " " + ColorText(TintColor(opacities_, c)) + "\n";
    ++ops_;
}

void RecordingPaintContext::PushClip(const Rect& r) {
    text_ += "pushclip " + RectText(MapRect(transforms_, r)) + "\n";
    ++ops_;
}

void RecordingPaintContext::PopClip() {
    text_ += "popclip\n";
    ++ops_;
}

void RecordingPaintContext::PushOpacity(float multiplier) {
    opacities_.push_back(multiplier);
    text_ += "pushopacity " + Num(multiplier) + "\n";
}

void RecordingPaintContext::PopOpacity() {
    if (!opacities_.empty()) opacities_.pop_back();
    text_ += "popopacity\n";
}

void RecordingPaintContext::PushTransform(const PaintTransform& t) {
    transforms_.push_back(t);
    text_ += "pushtransform " + Num(t.scaleX) + " " + Num(t.scaleY) + " " + Num(t.rotation) +
             " " + Num(t.translate.x) + " " + Num(t.translate.y) + "\n";
}

void RecordingPaintContext::PopTransform() {
    if (!transforms_.empty()) transforms_.pop_back();
    text_ += "poptransform\n";
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
void RasterPaintContext::FillRect(const Rect& r, const Color& c) {
    target_.FillRectF(MapRect(transforms_, r), TintColor(opacities_, c));
}

void RasterPaintContext::FillRoundRect(const Rect& r, float, const Color& c) {
    target_.FillRectF(MapRect(transforms_, r), TintColor(opacities_, c));
}

void RasterPaintContext::StrokeRect(const Rect& r, const Color& c, float width, float) {
    const Rect box = MapRect(transforms_, r);
    const Color tint = TintColor(opacities_, c);
    const float w = std::max(1.0f, width);
    target_.FillRectF(Rect{box.x, box.y, box.w, w}, tint);
    target_.FillRectF(Rect{box.x, box.y + box.h - w, box.w, w}, tint);
    target_.FillRectF(Rect{box.x, box.y, w, box.h}, tint);
    target_.FillRectF(Rect{box.x + box.w - w, box.y, w, box.h}, tint);
}

void RasterPaintContext::FillEllipse(const Rect& in, const Color& c) {
    // Scanline fill with a squared distance test. Deterministic and cheap: every
    // pixel is decided by integer offsets from the centre, so two runs agree exactly.
    const Rect r = MapRect(transforms_, in);
    const Color tint = TintColor(opacities_, c);
    const float cx = r.x + r.w * 0.5f;
    const float cy = r.y + r.h * 0.5f;
    const float rx = r.w * 0.5f;
    const float ry = r.h * 0.5f;
    if (rx <= 0.0f || ry <= 0.0f) return;
    const int y0 = static_cast<int>(std::floor(cy - ry));
    const int y1 = static_cast<int>(std::ceil(cy + ry));
    const int x0 = static_cast<int>(std::floor(cx - rx));
    const int x1 = static_cast<int>(std::ceil(cx + rx));
    for (int y = y0; y <= y1; ++y) {
        const float ny = (static_cast<float>(y) + 0.5f - cy) / ry;
        const float ny2 = ny * ny;
        if (ny2 > 1.0f) continue;
        // Half-chord length at this scanline.
        const float span = rx * std::sqrt(1.0f - ny2);
        const int sx = static_cast<int>(std::ceil(cx - span));
        const int ex = static_cast<int>(std::floor(cx + span));
        if (ex < sx) continue;
        target_.FillRectF(Rect{static_cast<float>(sx), static_cast<float>(y),
                               static_cast<float>(ex - sx + 1), 1.0f},
                          tint);
    }
    (void)x0;
    (void)x1;
}

void RasterPaintContext::DrawLine(const Point& a, const Point& b, const Color& c, float width) {
    // Bresenham-ish with a square brush: enough to make a diagonal visibly a line
    // in the determinism snapshots. Not anti-aliased on purpose -- this raster is a
    // CI gate, not the production renderer, and a stepped line is deterministic
    // where coverage blending invites float-order differences.
    const float w = std::max(1.0f, width);
    const Color tint = TintColor(opacities_, c);
    const Point from = MapPoint(transforms_, a);
    const Point to = MapPoint(transforms_, b);
    float x0 = from.x;
    float y0 = from.y;
    const float x1 = to.x;
    const float y1 = to.y;
    const float dx = std::abs(x1 - x0);
    const float dy = std::abs(y1 - y0);
    const int steps = static_cast<int>(std::max(dx, dy));
    if (steps <= 0) {
        target_.FillRectF(Rect{x0, y0, w, w}, tint);
        return;
    }
    const float sx = (x1 - x0) / static_cast<float>(steps);
    const float sy = (y1 - y0) / static_cast<float>(steps);
    for (int i = 0; i <= steps; ++i) {
        target_.FillRectF(Rect{x0, y0, w, w}, tint);
        x0 += sx;
        y0 += sy;
    }
}

void RasterPaintContext::DrawText(const std::wstring& text, const Rect& in, const Color& c,
                                  float fontSize, int, TextAlign align, const std::wstring&) {
    const Rect r = MapRect(transforms_, in);
    const Color tint = TintColor(opacities_, c);
    const float advance = fontSize * 0.55f;
    const float total = advance * static_cast<float>(text.size());
    float x = r.x;
    if (align == TextAlign::Center) x = r.x + (r.w - total) * 0.5f;
    else if (align == TextAlign::Right) x = r.x + r.w - total;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L' ') continue;
        target_.FillRectF(Rect{x + static_cast<float>(i) * advance, r.y + (r.h - fontSize) * 0.5f,
                               advance * 0.7f, fontSize * 0.8f},
                          tint);
    }
}

void RasterPaintContext::PushClip(const Rect& r) { clips_.push_back(MapRect(transforms_, r)); }
void RasterPaintContext::PopClip() {
    if (!clips_.empty()) clips_.pop_back();
}

void RasterPaintContext::PushOpacity(float multiplier) { opacities_.push_back(multiplier); }
void RasterPaintContext::PopOpacity() {
    if (!opacities_.empty()) opacities_.pop_back();
}
void RasterPaintContext::PushTransform(const PaintTransform& t) { transforms_.push_back(t); }
void RasterPaintContext::PopTransform() {
    if (!transforms_.empty()) transforms_.pop_back();
}

// ---------------------------------------------------------------------------
// FakeTextService
// ---------------------------------------------------------------------------
Size FakeTextService::Measure(const std::wstring& text, float fontSize, int weight,
                              const std::wstring& family) {
    // CJK glyphs are twice as wide as Latin ones; this keeps layout tests
    // meaningful without pulling in DWrite.
    float units = 0;
    for (wchar_t c : text) units += (c >= 0x2E80) ? 2.0f : 1.0f;
    const float weightScale = 1.0f + 0.02f * static_cast<float>(weight);
    // A monospace family is 0.6 em per glyph instead of 0.55, so a layout test CAN tell
    // the two apart without DWrite. Without this, FontFamily would be untestable in the
    // headless harness -- which is exactly the kind of "covered but never exercised"
    // feature that breaks silently.
    const float advance = IsMonospaceFamily(family) ? 0.6f : 0.55f;
    return Size{units * fontSize * advance * weightScale, fontSize * 1.33f};
}

void RecordingPaintContext::DrawBitmap(const Bitmap* bitmap, const Rect& dest, const Rect* clip) {
    const Rect d = MapRect(transforms_, dest);
    const Rect c = clip ? MapRect(transforms_, *clip) : Rect{};
    text_ += "bitmap " + RectText(d) + " " +
             (bitmap ? std::to_string(bitmap->width) + "x" + std::to_string(bitmap->height) : "none") +
             (clip ? (" clip " + RectText(c)) : std::string()) + "\n";
    ++ops_;
}

void RasterPaintContext::DrawBitmap(const Bitmap* bitmap, const Rect& dest, const Rect* clip) {
    if (bitmap == nullptr || !bitmap->Valid()) return;
    // Nearest-neighbour scaling, clipped to the destination and (when asked) to an
    // ellipse. Deterministic by construction: each destination pixel maps to exactly one
    // source pixel through integer arithmetic, so the snapshot hash cannot drift.
    const int x0 = std::max(static_cast<int>(std::floor(dest.x)), 0);
    const int y0 = std::max(static_cast<int>(std::floor(dest.y)), 0);
    const int x1 = std::min(static_cast<int>(std::ceil(dest.x + dest.w)), target_.Width());
    const int y1 = std::min(static_cast<int>(std::ceil(dest.y + dest.h)), target_.Height());
    if (dest.w <= 0 || dest.h <= 0) return;
    const float cx = dest.x + dest.w * 0.5f;
    const float cy = dest.y + dest.h * 0.5f;
    const float rx = dest.w * 0.5f;
    const float ry = dest.h * 0.5f;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (clip != nullptr) {
                // Inside the inscribed ellipse only, for the circular avatar crop.
                const float nx = (static_cast<float>(x) + 0.5f - cx) / rx;
                const float ny = (static_cast<float>(y) + 0.5f - cy) / ry;
                if (nx * nx + ny * ny > 1.0f) continue;
            }
            const float u = (static_cast<float>(x) + 0.5f - dest.x) / dest.w;
            const float v = (static_cast<float>(y) + 0.5f - dest.y) / dest.h;
            const int sx = std::clamp(static_cast<int>(u * static_cast<float>(bitmap->width)), 0,
                                      bitmap->width - 1);
            const int sy = std::clamp(static_cast<int>(v * static_cast<float>(bitmap->height)), 0,
                                      bitmap->height - 1);
            const uint32_t p = bitmap->pixels[static_cast<size_t>(sy) * bitmap->width +
                                              static_cast<size_t>(sx)];
            const float a = static_cast<float>((p >> 24) & 0xFF) / 255.0f;
            if (a <= 0.0f) continue;
            const Color c{static_cast<float>((p >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((p >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(p & 0xFF) / 255.0f, a, Token::None};
            target_.FillRectF(Rect{static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f}, c);
        }
    }
}

} // namespace wui