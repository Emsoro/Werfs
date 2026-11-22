// SPDX-License-Identifier: MIT
#include "Controls.h"

#include "Overlay.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>

namespace wui {

// ---------------------------------------------------------------------------
// TextBlock
// ---------------------------------------------------------------------------

// Greedy word wrap. Splits on spaces, keeps words whole, and honours explicit
// newlines. Falls back to per-character breaking for a single word that is
// wider than the line, so a long path never overflows.
static std::vector<std::wstring> WrapText(ITextService* svc, const std::wstring& text, float fontSize,
                                          int weight, float maxWidth) {
    std::vector<std::wstring> lines;
    if (!svc || maxWidth <= 0) {
        lines.push_back(text);
        return lines;
    }
    std::wstring current;
    size_t i = 0;
    while (i <= text.size()) {
        // Take the next word (up to a space or the explicit newline).
        size_t end = i;
        while (end < text.size() && text[end] != L' ' && text[end] != L'\n') ++end;
        const std::wstring word = text.substr(i, end - i);
        const bool newline = end < text.size() && text[end] == L'\n';

        const std::wstring candidate = current.empty() ? word : current + L" " + word;
        if (current.empty() || svc->Measure(candidate, fontSize, weight).w <= maxWidth) {
            current = candidate;
        } else {
            lines.push_back(current);
            current = word;
        }
        // A single word wider than the line: break it by character.
        if (!current.empty() && svc->Measure(current, fontSize, weight).w > maxWidth) {
            std::wstring piece;
            for (wchar_t ch : current) {
                const std::wstring test = piece + ch;
                if (!piece.empty() && svc->Measure(test, fontSize, weight).w > maxWidth) {
                    lines.push_back(piece);
                    piece = std::wstring(1, ch);
                } else {
                    piece = test;
                }
            }
            current = piece;
        }
        if (newline) {
            lines.push_back(current);
            current.clear();
        }
        if (end >= text.size()) break;
        i = end + 1;
    }
    if (!current.empty() || lines.empty()) lines.push_back(current);
    return lines;
}

Size TextBlock::Measure(const Size& available) {
    const int slotText = SlotOf("Text");
    const int slotSize = SlotOf("FontSize");
    const int slotPad = SlotOf("Padding");
    const int slotMargin = SlotOf("Margin");
    const int slotWeight = SlotOf("FontWeight");
    const std::wstring text = slotText >= 0 ? Get<std::wstring>(slotText) : L"";
    const float fontSize = slotSize >= 0 ? Get<float>(slotSize) : 14.0f;
    const int weight = slotWeight >= 0 ? Get<int>(slotWeight) : WeightNormal;
    const Thickness pad = slotPad >= 0 ? Get<Thickness>(slotPad) : Thickness{};
    const Thickness margin = slotMargin >= 0 ? Get<Thickness>(slotMargin) : Thickness{};
    const bool wrap = Get<int>(SlotOf("TextWrapping")) == 1;

    ITextService* svc = TextSystem::Get();
    // Unwrapped text ignores the constraint: it sizes to its content and the
    // parent decides whether to clip it. Wrapped text needs the width, so the
    // available space is what bounds it.
    const float maxWidth = std::max(0.0f, available.w - pad.H() - margin.H());

    if (wrap && maxWidth > 0 && !text.empty()) {
        wrappedLines_ = WrapText(svc, text, fontSize, weight, maxWidth);
        float w = 0;
        for (const auto& line : wrappedLines_) {
            const float lineW = svc ? svc->Measure(line, fontSize, weight).w
                                    : static_cast<float>(line.size()) * fontSize * 0.55f;
            w = std::max(w, lineW);
        }
        const float lineH = svc ? svc->LineHeight(fontSize) : fontSize * 1.33f;
        desired_ = Size{w + pad.H() + margin.H(),
                        lineH * static_cast<float>(wrappedLines_.size()) + pad.V() + margin.V()};
        ApplySizeConstraints();
        return desired_;
    }

    wrappedLines_.clear();
    Size t{0, fontSize * 1.33f};
    if (svc) {
        if (!text.empty()) t = svc->Measure(text, fontSize, weight);
    } else {
        t.w = static_cast<float>(text.size()) * fontSize * 0.55f;
    }
    desired_ = Size{t.w + pad.H() + margin.H(), std::max(t.h, fontSize * 1.33f) + pad.V() + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void TextBlock::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const int slotPad = SlotOf("Padding");
    const int slotMargin = SlotOf("Margin");
    const Thickness pad = slotPad >= 0 ? Get<Thickness>(slotPad) : Thickness{};
    const Thickness margin = slotMargin >= 0 ? Get<Thickness>(slotMargin) : Thickness{};
    Rect box{bounds_.x + margin.l + pad.l, bounds_.y + margin.t + pad.t,
             bounds_.w - margin.H() - pad.H(), bounds_.h - margin.V() - pad.V()};
    if (box.w <= 0) return;
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    if (text.empty()) return;
    const Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    const TextAlign align = static_cast<TextAlign>(Get<int>(SlotOf("TextAlignment")));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const int weight = Get<int>(SlotOf("FontWeight"));

    if (wrappedLines_.size() > 1) {
        ITextService* svc = TextSystem::Get();
        const float lineH = svc ? svc->LineHeight(fontSize) : fontSize * 1.33f;
        float y = box.y;
        for (const auto& line : wrappedLines_) {
            ctx.DrawText(line, Rect{box.x, y, box.w, lineH}, fg, fontSize, weight, align);
            y += lineH;
        }
        return;
    }
    ctx.DrawText(text, box, fg, fontSize, weight, align);
}

// ---------------------------------------------------------------------------
// Border
// ---------------------------------------------------------------------------
Size Border::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    // Border thickness takes space, exactly like padding. Ignoring it made a
    // bordered card clip its own content by the border width.
    const Thickness border = Get<Thickness>(SlotOf("BorderThickness"));
    const float chromeH = margin.H() + pad.H() + border.H();
    const float chromeV = margin.V() + pad.V() + border.V();
    const Size inner{std::max(0.0f, available.w - chromeH), std::max(0.0f, available.h - chromeV)};
    float w = 0;
    float h = 0;
    for (auto& c : LayoutChildren()) {
        const Size s = c->Measure(inner);
        w = std::max(w, s.w);
        h = std::max(h, s.h);
    }
    // ApplySizeConstraints applies an explicit Width/Height on top of this.
    desired_ = Size{w + chromeH, h + chromeV};
    ApplySizeConstraints();
    return desired_;
}

void Border::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Thickness border = Get<Thickness>(SlotOf("BorderThickness"));
    const Rect content{finalRect.x + margin.l + pad.l + border.l,
                       finalRect.y + margin.t + pad.t + border.t,
                       std::max(0.0f, finalRect.w - margin.H() - pad.H() - border.H()),
                       std::max(0.0f, finalRect.h - margin.V() - pad.V() - border.V())};
    for (auto& c : LayoutChildren()) c->Arrange(content);
}

void Border::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    Rect box{bounds_.x + margin.l, bounds_.y + margin.t, bounds_.w - margin.H(), bounds_.h - margin.V()};
    if (box.w <= 0 || box.h <= 0) return;
    const float radius = Get<float>(SlotOf("CornerRadius"));
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) {
        if (radius > 0) ctx.FillRoundRect(box, radius, bg);
        else ctx.FillRect(box, bg);
    }
    const Color border = ResolveColor(Get<Color>(SlotOf("BorderBrush")), palette);
    const Thickness thickness = Get<Thickness>(SlotOf("BorderThickness"));
    if (border.a <= 0) return;

    // Per-edge strokes, so "0,1,0,0" draws a single divider line. PaintCtx has no
    // per-edge stroke primitive, so the edges that are present are drawn as thin
    // filled strips -- correct for the 1px hairlines a divider actually uses, and
    // avoids a fourth drawing primitive for a case that only ever needs 1-2 px.
    if (thickness.t > 0) ctx.FillRect(Rect{box.x, box.y, box.w, thickness.t}, border);
    if (thickness.b > 0)
        ctx.FillRect(Rect{box.x, box.y + box.h - thickness.b, box.w, thickness.b}, border);
    if (thickness.l > 0) ctx.FillRect(Rect{box.x, box.y, thickness.l, box.h}, border);
    if (thickness.r > 0)
        ctx.FillRect(Rect{box.x + box.w - thickness.r, box.y, thickness.r, box.h}, border);
}

// ---------------------------------------------------------------------------
// ScrollViewer
//
// Layout model: the viewport width is whatever the parent hands us; the child
// is measured with an unbounded height so a vertical stack keeps its natural
// size. The difference between that and the viewport height is the scroll
// range. Painting clips to the viewport and offsets the child by -offset_, so
// the child's own Arrange stays in unscrolled coordinates -- hit testing then
// only has to add the offset back.
// ---------------------------------------------------------------------------
Size ScrollViewer::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float availW = std::max(0.0f, available.w - margin.H());
    // Unbounded height: this is what lets the content exceed the viewport.
    constexpr float kUnbounded = 1.0e6f;
    float w = 0;
    float h = 0;
    for (auto& c : LayoutChildren()) {
        const Size s = c->Measure(Size{availW, kUnbounded});
        w = std::max(w, s.w);
        h = std::max(h, s.h);
    }
    contentHeight_ = h;
    // An explicit Height wins; otherwise fill the height the parent offers.
    const float explicitH = Get<float>(SlotOf("Height"));
    const float viewportH = explicitH > 0 ? explicitH : std::max(0.0f, available.h - margin.V());
    desired_ = Size{w + margin.H(), std::min(contentHeight_, viewportH)};
    ApplySizeConstraints();
    maxScroll_ = std::max(0.0f, contentHeight_ - desired_.h);
    if (offset_ > maxScroll_) offset_ = maxScroll_;
    return desired_;
}

void ScrollViewer::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    // Respect our own measured height: a Page fills its child to the full
    // window, but a ScrollViewer with Height="200" must stay 200 tall and
    // scroll inside that, not expand to the whole page.
    const float h = desired_.h > 0 ? std::min(finalRect.h, desired_.h) : finalRect.h;
    const Rect viewport{finalRect.x + margin.l, finalRect.y + margin.t,
                        std::max(0.0f, finalRect.w - margin.H()),
                        std::max(0.0f, h - margin.V())};
    maxScroll_ = std::max(0.0f, contentHeight_ - viewport.h);
    if (offset_ > maxScroll_) offset_ = maxScroll_;
    if (offset_ < 0) offset_ = 0;
    // Children are arranged in their own (unscrolled) space; the paint offset
    // and HitTest move them by -offset_ / +offset_ respectively.
    const Rect content{viewport.x, viewport.y, viewport.w, std::max(contentHeight_, viewport.h)};
    for (auto& c : LayoutChildren()) c->Arrange(content);
}

void ScrollViewer::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    // Only the scrollbar is drawn here. The content is drawn by PaintChildrenAt,
    // which PaintTree calls right after this returns -- painting children from
    // inside OnPaint would paint them twice, once shifted and once not.
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect viewport{bounds_.x + margin.l, bounds_.y + margin.t,
                        std::max(0.0f, bounds_.w - margin.H()),
                        std::max(0.0f, bounds_.h - margin.V())};
    if (viewport.w <= 0 || viewport.h <= 0) return;
    if (maxScroll_ <= 0) return;

    constexpr float kBarW = 6.0f;
    constexpr float kBarInset = 2.0f;
    const float trackX = viewport.x + viewport.w - kBarW - kBarInset;
    const Rect track{trackX, viewport.y + kBarInset, kBarW, std::max(0.0f, viewport.h - kBarInset * 2)};
    const Color trackColor = ResolveColor(Color{0, 0, 0, 1, Token::CardBorder}, palette);
    ctx.FillRoundRect(track, kBarW * 0.5f, Color{trackColor.r, trackColor.g, trackColor.b, 0.35f});

    const float visible = viewport.h;
    const float thumbH = std::max(24.0f, track.h * (visible / (visible + maxScroll_)));
    const float travel = std::max(0.0f, track.h - thumbH);
    const float t = maxScroll_ > 0 ? offset_ / maxScroll_ : 0;
    const Rect thumb{track.x, track.y + travel * t, kBarW, thumbH};
    const Color thumbColor = ResolveColor(Color{0, 0, 0, 1, Token::TextSecondary}, palette);
    ctx.FillRoundRect(thumb, kBarW * 0.5f, Color{thumbColor.r, thumbColor.g, thumbColor.b, 0.55f});
}

void ScrollViewer::PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect viewport{bounds_.x + margin.l, bounds_.y + margin.t,
                        std::max(0.0f, bounds_.w - margin.H()),
                        std::max(0.0f, bounds_.h - margin.V())};
    if (viewport.w <= 0 || viewport.h <= 0) return;

    // Clip so content outside the viewport is never drawn, then offset the
    // subtree upward by the scroll amount. Both are needed: without the clip the
    // page paints over things below it, without the offset the page never
    // appears to move.
    //
    // The offset is added to the incoming shift rather than replacing it, so a
    // scroller nested inside another scrolled page still composes correctly.
    ctx.PushClip(viewport);
    Widget::PaintChildrenAt(ctx, palette, Point{shift.x, shift.y - offset_});
    ctx.PopClip();
}

void ScrollViewer::ScrollTo(float offset) {
    const float clamped = std::min(std::max(0.0f, offset), maxScroll_);
    if (clamped == offset_) return;
    offset_ = clamped;
    Invalidate(AffectsRender);
}

Widget* ScrollViewer::HitTest(const Point& p) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect viewport{bounds_.x + margin.l, bounds_.y + margin.t,
                        std::max(0.0f, bounds_.w - margin.H()),
                        std::max(0.0f, bounds_.h - margin.V())};

    // Content scrolled out of view must not receive clicks: the paint path clips
    // it, so a hit there would land on something the user cannot see.
    if (!viewport.Contains(p)) return nullptr;

    // Children are arranged in unscrolled coordinates, so a point in screen space
    // has to move DOWN by the scroll amount before descending. Without this every
    // control inside a scrolled page is unclickable the moment you scroll.
    const Point inContent{p.x, p.y + offset_};

    // Children first, so a button inside the page still gets its own click.
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (Widget* hit = (*it)->HitTest(inContent)) return hit;
    }
    // Nothing under the cursor, but it is inside the viewport: claim it. Scrolling
    // has to work over the gaps between children and over the empty area past the
    // content -- the base class returned null there, the wheel went to the page,
    // and scrolling looked dead unless the cursor happened to be over a control.
    return this;
}

bool ScrollViewer::OnPointerWheel(float deltaLines) {
    if (maxScroll_ <= 0) {
        // Nothing to scroll here; let an outer scroller handle it.
        return Widget::OnPointerWheel(deltaLines);
    }
    constexpr float kLineHeight = 48.0f;
    ScrollTo(offset_ - deltaLines * kLineHeight);
    return true;
}

bool ScrollViewer::OnKeyDown(int vk) {
    // Keyboard scrolling, so the gallery is usable without a wheel.
    constexpr float kPage = 0.9f;
    switch (vk) {
        case 0x21: // VK_PRIOR
            ScrollTo(offset_ - maxScroll_ * kPage * 0.5f - 100);
            return true;
        case 0x22: // VK_NEXT
            ScrollTo(offset_ + maxScroll_ * kPage * 0.5f + 100);
            return true;
        case 0x24: // VK_HOME
            ScrollTo(0);
            return true;
        case 0x23: // VK_END
            ScrollTo(maxScroll_);
            return true;
        case 0x26: // VK_UP
            ScrollTo(offset_ - 48);
            return true;
        case 0x28: // VK_DOWN
            ScrollTo(offset_ + 48);
            return true;
        default:
            return Widget::OnKeyDown(vk);
    }
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------
void Page::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
}

Size Page::Measure(const Size& available) {
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Size inner{std::max(0.0f, available.w - pad.H()),
                     std::max(0.0f, available.h - pad.V())};
    // A page is a vertical column of regions: menu bar, toolbar, content, status.
    // Every child is measured, and the tallest arrangement wins -- the page fills
    // what it is given, so a page taller than the window simply clips like a
    // window does rather than growing a scrollbar nobody asked for.
    float totalH = 0;
    float maxW = 0;
    for (auto& c : LayoutChildren()) {
        const Size s = c->Measure(Size{inner.w, inner.h});
        totalH += s.h;
        maxW = std::max(maxW, s.w);
    }
    desired_ = Size{available.w > 0 ? available.w : maxW + pad.H(), totalH + pad.V()};
    ApplySizeConstraints();
    return desired_;
}

void Page::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float contentW = std::max(0.0f, finalRect.w - pad.H());
    const float contentTop = finalRect.y + pad.t;
    const float contentH = std::max(0.0f, finalRect.h - pad.V());

    // Children stack vertically. Children WITH an explicit Height keep it; the
    // leftover height is shared equally among the children that have none.
    //
    // Explicit height is the signal because it is what markup already says: a menu
    // bar is Height="34" and must stay 34, while the content region declares no
    // height and is the one that should fill. "The last child grows" was the first
    // attempt and it is wrong for the ordinary page shape -- with a status line last,
    // the status line would absorb every spare pixel.
    //
    // When every child has a height, nothing grows and the page simply does not
    // fill the window; that is a deliberate page, not an accident.
    const auto kids = LayoutChildren();
    if (kids.empty()) return;

    float fixedH = 0;
    int flexible = 0;
    for (auto& c : kids) {
        const float explicitH = c->SlotOf("Height") >= 0 ? c->Get<float>(c->SlotOf("Height")) : 0.0f;
        if (explicitH > 0) fixedH += explicitH;
        else {
            fixedH += c->DesiredSize().h;
            ++flexible;
        }
    }
    const float slack = flexible > 0 ? std::max(0.0f, contentH - fixedH) / flexible : 0.0f;

    float y = contentTop;
    for (auto& c : kids) {
        const float explicitH = c->SlotOf("Height") >= 0 ? c->Get<float>(c->SlotOf("Height")) : 0.0f;
        const float h = explicitH > 0 ? explicitH : c->DesiredSize().h + slack;
        c->Arrange(Rect{finalRect.x + pad.l, y, contentW, h});
        y += h;
    }
}

// ---------------------------------------------------------------------------
// StackPanel
// ---------------------------------------------------------------------------
Size StackPanel::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float spacing = Get<float>(SlotOf("Spacing"));
    const int orientation = Get<int>(SlotOf("Orientation"));
    const Size inner{std::max(0.0f, available.w - margin.H() - pad.H()),
                     std::max(0.0f, available.h - margin.V() - pad.V())};

    float mainExtent = 0;
    float crossExtent = 0;
    bool first = true;
    for (auto& c : children_) {
        if (!c->IsVisible()) continue;   // Collapsed takes no space at all.
        const Size s = orientation == 0
                           ? c->Measure(Size{inner.w, std::max(0.0f, inner.h - mainExtent)})
                           : c->Measure(Size{std::max(0.0f, inner.w - mainExtent), inner.h});
        const float main = orientation == 0 ? s.h : s.w;
        const float cross = orientation == 0 ? s.w : s.h;
        if (!first) mainExtent += spacing;
        first = false;
        mainExtent += main;
        crossExtent = std::max(crossExtent, cross);
    }
    if (orientation == 0)
        desired_ = Size{crossExtent + margin.H() + pad.H(), mainExtent + margin.V() + pad.V()};
    else
        desired_ = Size{mainExtent + margin.H() + pad.H(), crossExtent + margin.V() + pad.V()};
    // Without this an explicit Width/Height on the panel was ignored outright:
    // a StackPanel Height="600" still reported its children's extent, so nothing
    // that sized itself against the panel (a ScrollViewer's scroll range, for
    // one) ever saw the declared size.
    ApplySizeConstraints();
    return desired_;
}

void StackPanel::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float spacing = Get<float>(SlotOf("Spacing"));
    const int orientation = Get<int>(SlotOf("Orientation"));
    const Rect content{finalRect.x + margin.l + pad.l, finalRect.y + margin.t + pad.t,
                       std::max(0.0f, finalRect.w - margin.H() - pad.H()),
                       std::max(0.0f, finalRect.h - margin.V() - pad.V())};
    float cursor = orientation == 0 ? content.y : content.x;
    bool first = true;
    for (auto& c : LayoutChildren()) {
        if (!first) cursor += spacing;
        first = false;
        if (orientation == 0) {
            const Size want = c->DesiredSize();
            c->Arrange(Rect{content.x, cursor, content.w, want.h});
            cursor += want.h;
        } else {
            const Size want = c->DesiredSize();
            c->Arrange(Rect{cursor, content.y, want.w, content.h});
            cursor += want.w;
        }
    }
}

void StackPanel::OnPaint(PaintCtx&, const ThemePalette&) { ++PaintCount; }

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
Size Button::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    Size t{static_cast<float>(text.size()) * fontSize * 0.55f, fontSize * 1.33f};
    if (ITextService* svc = TextSystem::Get()) t = svc->Measure(text, fontSize, Get<int>(SlotOf("FontWeight")));
    desired_ = Size{std::max(32.0f, t.w + pad.H() + margin.H()), std::max(28.0f, t.h + pad.V() + margin.V())};
    (void)available;
    ApplySizeConstraints();
    return desired_;
}

void Button::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t, bounds_.w - margin.H(),
                   bounds_.h - margin.V()};
    if (box.w <= 0 || box.h <= 0) return;
    Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (!Get<bool>(SlotOf("IsEnabled"))) {
        bg.a *= 0.4f;
    } else if (pressed_) {
        bg.r *= 0.85f;
        bg.g *= 0.85f;
        bg.b *= 0.85f;
    } else if (hovered_) {
        bg.r = std::min(1.0f, bg.r * 1.08f + 0.02f);
        bg.g = std::min(1.0f, bg.g * 1.08f + 0.02f);
        bg.b = std::min(1.0f, bg.b * 1.08f + 0.02f);
    }
    const float radius = 4.0f;
    ctx.FillRoundRect(box, radius, bg);
    if (focused_) ctx.StrokeRect(box.Inflate(1.0f), ResolveColor(Get<Color>(SlotOf("Foreground")), palette), 1.5f, radius + 1);

    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect textBox{box.x + pad.l, box.y + pad.t, box.w - pad.H(), box.h - pad.V()};
    Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    if (!Get<bool>(SlotOf("IsEnabled"))) fg.a *= 0.5f;
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")), textBox, fg, Get<float>(SlotOf("FontSize")),
                 Get<int>(SlotOf("FontWeight")), TextAlign::Center);
}

Widget* Button::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    return bounds_.Contains(p) ? this : nullptr;
}

void Button::OnPointerDown(const Point&) { pressed_ = true; Invalidate(AffectsRender); }

void Button::OnPointerUp(const Point& local) {
    const bool wasPressed = pressed_;
    pressed_ = false;
    Invalidate(AffectsRender);
    if (wasPressed && bounds_.Contains(local)) Invoke();
}

bool Button::OnKeyDown(int vk) {
    if (vk == KeySpace || vk == KeyReturn) {
        Invoke();
        return true;
    }
    return false;
}

void Button::Invoke() {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    ICommand* cmd = static_cast<ICommand*>(Get<RefPtr>(SlotOf("Command")).get());
    if (cmd != nullptr && cmd->CanExecute()) cmd->Execute();
}

// ---------------------------------------------------------------------------
// CheckBox
// ---------------------------------------------------------------------------
Size CheckBox::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    Size t{static_cast<float>(text.size()) * fontSize * 0.55f, fontSize * 1.33f};
    if (ITextService* svc = TextSystem::Get()) t = svc->Measure(text, fontSize, WeightNormal);
    desired_ = Size{t.w + 24.0f + pad.H() + margin.H(),
                    std::max(t.h, 20.0f) + pad.V() + margin.V()};
    (void)available;
    // Explicit Width/Height must be able to override the content size, the same
    // way every other control behaves.
    ApplySizeConstraints();
    return desired_;
}

void CheckBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t, bounds_.w - margin.H(),
                   bounds_.h - margin.V()};
    if (box.w <= 0 || box.h <= 0) return;
    const float size = 18.0f;
    const Rect glyph{box.x + 3, box.y + (box.h - size) * 0.5f, size, size};
    const bool checked = Get<bool>(SlotOf("IsChecked"));
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];
    ctx.FillRoundRect(glyph, 3.0f, checked ? accent : palette.colors[static_cast<int>(Token::ControlFill)]);
    if (!checked) ctx.StrokeRect(glyph, palette.colors[static_cast<int>(Token::TextSecondary)], 1.0f, 3.0f);
    if (checked) {
        // A check mark drawn as two thin strokes; good enough for the spike.
        ctx.DrawText(L"\u2713", Rect{glyph.x, glyph.y, glyph.w, glyph.h},
                     palette.colors[static_cast<int>(Token::TextOnAccent)], size * 0.8f, WeightBold,
                     TextAlign::Center);
    }
    const Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")),
                 Rect{glyph.Right() + 8, box.y, box.w - glyph.w - 11, box.h}, fg,
                 Get<float>(SlotOf("FontSize")), WeightNormal, TextAlign::Left);
    if (focused_) ctx.StrokeRect(box.Inflate(1.0f), palette.colors[static_cast<int>(Token::Accent)], 1.0f, 3.0f);
}

Widget* CheckBox::HitTest(const Point& p) { return bounds_.Contains(p) ? this : nullptr; }

void CheckBox::OnPointerUp(const Point& local) {
    if (bounds_.Contains(local)) Toggle();
}

bool CheckBox::OnKeyDown(int vk) {
    if (vk == KeySpace) {
        Toggle();
        return true;
    }
    return false;
}

void CheckBox::Toggle() { SetSlotValue(SlotOf("IsChecked"), Value::Bool(!Get<bool>(SlotOf("IsChecked")))); }

// ---------------------------------------------------------------------------
// TextBox
// ---------------------------------------------------------------------------
Size TextBox::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    std::wstring text = Get<std::wstring>(SlotOf("Text"));
    if (text.empty()) text = Get<std::wstring>(SlotOf("PlaceholderText"));
    Size t{static_cast<float>(text.size()) * fontSize * 0.55f, fontSize * 1.33f};
    if (ITextService* svc = TextSystem::Get()) t = svc->Measure(text, fontSize, WeightNormal);
    desired_ = Size{std::max(160.0f, t.w + pad.H() + margin.H()), std::max(30.0f, t.h + pad.V() + margin.V())};
    // Apply the caller's width, so a text box fills the column it is given instead
    // of sizing to its own text: a single-line measurement of a long paragraph is
    // metres wide, which made every dialog containing a text box overflow.
    if (available.w > 0 && available.w < 1.0e6f) desired_.w = available.w;
    // An unbounded height constraint is "as tall as you like", not "be a million
    // pixels tall". Taking it literally produced a 999927-pixel control inside a
    // star row: the box existed, was laid out, and drew its text a million pixels
    // down, so the editor area looked empty. A text box's natural height is one
    // line unless the markup says otherwise, which ApplySizeConstraints then
    // overrides.
    if (available.h > 0 && available.h < 1.0e6f) {
        const float explicitH = SlotOf("Height") >= 0 ? Get<float>(SlotOf("Height")) : 0.0f;
        if (explicitH <= 0) desired_.h = std::min(desired_.h, available.h);
    }
    ApplySizeConstraints();
    // MinHeight is re-applied after the clamp above: ApplySizeConstraints already did
    // it, but the clamp can have cut the value below the minimum.
    const int sMinH = SlotOf("MinHeight");
    if (sMinH >= 0) {
        const float minH = Get<float>(sMinH);
        if (minH > 0) desired_.h = std::max(desired_.h, minH);
    }
    return desired_;
}

void TextBox::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    // A multi-line editor box is the whole point of the sample: the document text
    // needs somewhere to be drawn. Drawing starts at the top and stops at the
    // bottom edge; lines beyond it are clipped rather than overflowing the card.
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    desired_ = Size{bounds_.w, bounds_.h};
    (void)margin;
    (void)pad;
}

std::vector<std::wstring> TextBox::BuildDisplayLines(float wrapWidth, float fontSize,
                                                    int weight) const {
    std::vector<std::wstring> lines;
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    ITextService* svc = TextSystem::Get();
    const float lineH = svc ? svc->LineHeight(fontSize) : fontSize * 1.33f;
    // A box only one line tall has no room to wrap into; wrapping there would
    // change what a single-line field looks like.
    const bool canWrap = bounds_.h >= lineH * 1.9f && wrapWidth > 0.0f;
    if (!canWrap) {
        // Single line: show as much as fits, which is the classic behaviour of a
        // one-line field with more text than room.
        lines.push_back(text);
        return lines;
    }

    std::wstring current;
    size_t i = 0;
    while (i <= text.size()) {
        // Take the next word, up to a space or an explicit newline.
        size_t end = i;
        while (end < text.size() && text[end] != L' ' && text[end] != L'\n') ++end;
        std::wstring word = text.substr(i, end - i);

        const bool hardBreak = end < text.size() && text[end] == L'\n';
        auto width = [&](const std::wstring& s) {
            return svc ? svc->Measure(s, fontSize, weight).w
                       : static_cast<float>(s.size()) * fontSize * 0.55f;
        };
        if (!current.empty() && width(current + L" " + word) > wrapWidth) {
            lines.push_back(current);
            current.clear();
        }
        // A single word longer than the line breaks by character, so a path or a
        // long identifier cannot overflow the box.
        while (width(word) > wrapWidth && wrapWidth > 0.0f) {
            size_t take = word.size();
            while (take > 1 && width(word.substr(0, take)) > wrapWidth) --take;
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
            }
            lines.push_back(word.substr(0, take));
            word = word.substr(take);
        }
        if (!word.empty()) {
            if (!current.empty()) current += L" ";
            current += word;
        }
        if (hardBreak) {
            lines.push_back(current);
            current.clear();
        }
        if (end >= text.size()) break;
        i = end + 1;
    }
    if (!current.empty()) lines.push_back(current);
    if (lines.empty()) lines.push_back(L"");
    return lines;
}

void TextBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t, bounds_.w - margin.H(),
                   bounds_.h - margin.V()};
    if (box.w <= 0 || box.h <= 0) return;
    ctx.FillRect(box, palette.colors[static_cast<int>(Token::ControlFill)]);
    ctx.StrokeRect(box, focused_ ? palette.colors[static_cast<int>(Token::Accent)]
                                 : palette.colors[static_cast<int>(Token::CardBorder)],
                   focused_ ? 1.5f : 1.0f, 4.0f);
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect textBox{box.x + pad.l, box.y + pad.t, box.w - pad.H(), box.h - pad.V()};
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const int weight = WeightNormal;

    if (text.empty() && !focused_) {
        ctx.DrawText(Get<std::wstring>(SlotOf("PlaceholderText")), textBox,
                     palette.colors[static_cast<int>(Token::TextSecondary)], fontSize, weight,
                     TextAlign::Left);
        return;
    }

    const float lineH = TextSystem::Get() ? TextSystem::Get()->LineHeight(fontSize)
                                          : fontSize * 1.33f;
    const Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    // Clip to the box: content past the bottom edge belongs to a scroll offset the
    // box does not have yet, and drawing it over the card below looks like a
    // rendering bug.
    ctx.PushClip(textBox);
    const std::vector<std::wstring> lines = BuildDisplayLines(textBox.w, fontSize, weight);
    float y = textBox.y;
    for (const auto& line : lines) {
        if (y + lineH > textBox.y + textBox.h + 0.5f) break;
        ctx.DrawText(line, Rect{textBox.x, y, textBox.w, lineH}, fg, fontSize, weight,
                     TextAlign::Left);
        y += lineH;
    }
    ctx.PopClip();
    if (focused_) {
        float caretX = textBox.x;
        if (ITextService* svc = TextSystem::Get()) {
            Size m = text.empty() ? Size{} : svc->Measure(text.substr(0, caret_), fontSize, WeightNormal);
            caretX += m.w;
        } else {
            caretX += static_cast<float>(caret_) * fontSize * 0.55f;
        }
        ctx.FillRect(Rect{caretX, textBox.y, 1.5f, textBox.h},
                     palette.colors[static_cast<int>(Token::TextPrimary)]);
    }
}

Widget* TextBox::HitTest(const Point& p) { return bounds_.Contains(p) ? this : nullptr; }

void TextBox::SetCaretToEnd() {
    editing_ = Get<std::wstring>(SlotOf("Text"));
    caret_ = editing_.size();
}

bool TextBox::OnChar(wchar_t ch) {
    if (ch < 32 && ch != L'\t') return false;
    if (editing_.empty()) editing_ = Get<std::wstring>(SlotOf("Text"));
    if (caret_ > editing_.size()) caret_ = editing_.size();
    editing_.insert(editing_.begin() + static_cast<long>(caret_), ch);
    ++caret_;
    SetSlotValue(SlotOf("Text"), Value::Text(editing_));
    return true;
}

bool TextBox::OnKeyDown(int vk) {
    if (editing_.empty()) editing_ = Get<std::wstring>(SlotOf("Text"));
    if (caret_ > editing_.size()) caret_ = editing_.size();
    switch (vk) {
    case KeyLeft:
        if (caret_ > 0) --caret_;
        Invalidate(AffectsRender);
        return true;
    case KeyRight:
        if (caret_ < editing_.size()) ++caret_;
        Invalidate(AffectsRender);
        return true;
    case KeyHome:
        caret_ = 0;
        Invalidate(AffectsRender);
        return true;
    case KeyEnd:
        caret_ = editing_.size();
        Invalidate(AffectsRender);
        return true;
    case KeyBack:
        if (caret_ > 0) {
            editing_.erase(editing_.begin() + static_cast<long>(caret_ - 1));
            --caret_;
            SetSlotValue(SlotOf("Text"), Value::Text(editing_));
        }
        return true;
    case KeyDelete:
        if (caret_ < editing_.size()) {
            editing_.erase(editing_.begin() + static_cast<long>(caret_));
            SetSlotValue(SlotOf("Text"), Value::Text(editing_));
        }
        return true;
    case KeyReturn:
        SetFocused(false);
        return true;
    default:
        return false;
    }
}

void TextBox::CommitText() { SetSlotValue(SlotOf("Text"), Value::Text(editing_)); }

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------
Size Canvas::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Size inner{std::max(0.0f, available.w - margin.H()),
                     std::max(0.0f, available.h - margin.V())};
    // Children size themselves; the canvas only needs their desired sizes for
    // arrange. Their extents do NOT grow the canvas: an absolutely positioned
    // child may sit anywhere, including outside the canvas bounds.
    float childW = 0;
    float childH = 0;
    for (auto& c : LayoutChildren()) {
        const Size s = c->Measure(inner);
        // Include the child's own offset, so a canvas without an explicit
        // Height still reports enough room for what is actually visible.
        const float left = c->SlotOf("Canvas.Left") >= 0 ? c->Get<float>(c->SlotOf("Canvas.Left")) : 0;
        const float top = c->SlotOf("Canvas.Top") >= 0 ? c->Get<float>(c->SlotOf("Canvas.Top")) : 0;
        childW = std::max(childW, left + s.w);
        childH = std::max(childH, top + s.h);
    }
    // An explicit Height wins; otherwise fill what the parent offers, falling
    // back to the children's extent when the parent offers nothing usable.
    const float explicitH = Get<float>(SlotOf("Height"));
    float h;
    if (explicitH > 0) h = explicitH;
    else if (available.h > 0 && available.h < 1.0e6f) h = available.h;
    else h = childH;
    desired_ = Size{available.w > 0 && available.w < 1.0e6f ? available.w : childW + margin.H(),
                    h + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void Canvas::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Point origin{finalRect.x + margin.l, finalRect.y + margin.t};
    for (auto& c : LayoutChildren()) {
        const float left = c->SlotOf("Canvas.Left") >= 0 ? c->Get<float>(c->SlotOf("Canvas.Left")) : 0;
        const float top = c->SlotOf("Canvas.Top") >= 0 ? c->Get<float>(c->SlotOf("Canvas.Top")) : 0;
        c->Arrange(Rect{origin.x + left, origin.y + top, c->DesiredSize().w, c->DesiredSize().h});
    }
}

void Canvas::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------
namespace {

// Parses "Auto,2*,1*,80" into lengths. Matching is case-insensitive: markup
// authors write both "auto" and "Auto", and a literal compare turns "auto"
// into a 0-pixel fixed track, which silently collapses the column and hides
// its content. Unknown tokens fall back to Star(1) so a typo degrades to an
// even split instead of vanishing.
std::vector<GridLength> ParseGridLengths(const std::wstring& spec) {
    std::vector<GridLength> out;
    std::wstring token;
    auto flush = [&] {
        if (token.empty()) return;
        std::wstring lower;
        lower.reserve(token.size());
        for (wchar_t ch : token) lower += static_cast<wchar_t>(towlower(ch));
        GridLength g;
        if (lower == L"auto" || lower == L"*") {
            g.kind = lower == L"*" ? GridLength::Kind::Star : GridLength::Kind::Auto;
            g.value = 1;
        } else if (!lower.empty() && lower.back() == L'*') {
            g.kind = GridLength::Kind::Star;
            const std::wstring num = lower.substr(0, lower.size() - 1);
            g.value = num.empty() ? 1.0f : std::max(0.0f, static_cast<float>(_wtof(num.c_str())));
            if (g.value <= 0) g.value = 1;
        } else {
            g.kind = GridLength::Kind::Fixed;
            g.value = std::max(0.0f, static_cast<float>(_wtof(lower.c_str())));
        }
        out.push_back(g);
        token.clear();
    };
    for (wchar_t ch : spec) {
        if (ch == L',' || ch == L' ') flush();
        else token += ch;
    }
    flush();
    return out;
}

} // namespace

void Grid::ParseDefinitions() {
    const std::wstring cols = Get<std::wstring>(SlotOf("ColumnDefinitions"));
    const std::wstring rows = Get<std::wstring>(SlotOf("RowDefinitions"));
    // Reparse only when the spec changed: strings are cheap but this runs on
    // every measure.
    if (parsed_ && cols == parsedCols_ && rows == parsedRows_) return;
    parsedCols_ = cols;
    parsedRows_ = rows;
    columns_ = ParseGridLengths(cols);
    rows_ = ParseGridLengths(rows);
    if (columns_.empty()) columns_.push_back(GridLength{1, GridLength::Kind::Star});
    if (rows_.empty()) rows_.push_back(GridLength{1, GridLength::Kind::Star});
    parsed_ = true;
}

Size Grid::Measure(const Size& available) {
    ParseDefinitions();
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float barW = Get<float>(SlotOf("ColumnSpacing"));
    const float barH = Get<float>(SlotOf("RowSpacing"));
    const float availW = std::max(0.0f, available.w - margin.H() - pad.H());
    const float availH = std::max(0.0f, available.h - margin.V() - pad.V());
    const size_t nCols = columns_.size();
    const size_t nRows = rows_.size();
    const float totalColSpacing = barW * (nCols > 1 ? static_cast<float>(nCols - 1) : 0.0f);
    const float totalRowSpacing = barH * (nRows > 1 ? static_cast<float>(nRows - 1) : 0.0f);

    // Pass 1: Auto tracks take the max desired extent of the children in them;
    // Star and Fixed tracks start from their fixed part.
    std::vector<float> colWidths(nCols, 0.0f);
    std::vector<float> rowHeights(nRows, 0.0f);
    float fixedW = 0;
    float starWeightW = 0;
    for (size_t i = 0; i < nCols; ++i) {
        if (columns_[i].kind == GridLength::Kind::Fixed) fixedW += columns_[i].value;
        else if (columns_[i].kind == GridLength::Kind::Star) starWeightW += columns_[i].value;
        else colWidths[i] = 0;
    }
    float fixedH = 0;
    float starWeightH = 0;
    for (size_t i = 0; i < nRows; ++i) {
        if (rows_[i].kind == GridLength::Kind::Fixed) fixedH += rows_[i].value;
        else if (rows_[i].kind == GridLength::Kind::Star) starWeightH += rows_[i].value;
    }

    // Provisional star sizes so children get a sane constraint to measure into.
    const float freeW = std::max(0.0f, availW - fixedW - totalColSpacing);
    const float freeH = std::max(0.0f, availH - fixedH - totalRowSpacing);

    for (auto& c : LayoutChildren()) {
        const int row = c->SlotOf("Grid.Row") >= 0 ? c->Get<int>(c->SlotOf("Grid.Row")) : 0;
        const int col = c->SlotOf("Grid.Column") >= 0 ? c->Get<int>(c->SlotOf("Grid.Column")) : 0;
        const int rowSpan = std::max(1, c->SlotOf("Grid.RowSpan") >= 0
                                            ? c->Get<int>(c->SlotOf("Grid.RowSpan"))
                                            : 1);
        const int colSpan = std::max(1, c->SlotOf("Grid.ColumnSpan") >= 0
                                            ? c->Get<int>(c->SlotOf("Grid.ColumnSpan"))
                                            : 1);
        const size_t r0 = static_cast<size_t>(std::min<int>(std::max(0, row), static_cast<int>(nRows) - 1));
        const size_t c0 = static_cast<size_t>(std::min<int>(std::max(0, col), static_cast<int>(nCols) - 1));
        const size_t r1 = std::min(nRows - 1, r0 + static_cast<size_t>(rowSpan) - 1);
        const size_t c1 = std::min(nCols - 1, c0 + static_cast<size_t>(colSpan) - 1);

        // Constraint available to this child in its spanned area.
        //
        // An Auto track has no size yet -- that is exactly what we are about to
        // measure. Passing its current 0 would measure the child into zero
        // width, so every Auto track would stay collapsed and its content would
        // never appear. Auto therefore measures unconstrained and the result
        // becomes the track size.
        bool hasAuto = false;
        float cw = 0;
        for (size_t i = c0; i <= c1; ++i) {
            if (columns_[i].kind == GridLength::Kind::Fixed) cw += columns_[i].value;
            else if (columns_[i].kind == GridLength::Kind::Star)
                cw += starWeightW > 0 ? freeW * (columns_[i].value / starWeightW) : freeW / nCols;
            else { hasAuto = true; cw += colWidths[i]; }
        }
        cw += barW * static_cast<float>(c1 - c0);
        if (hasAuto && cw <= 0) cw = availW;

        bool hasAutoRow = false;
        float ch = 0;
        for (size_t i = r0; i <= r1; ++i) {
            if (rows_[i].kind == GridLength::Kind::Fixed) ch += rows_[i].value;
            else if (rows_[i].kind == GridLength::Kind::Star)
                ch += starWeightH > 0 ? freeH * (rows_[i].value / starWeightH) : freeH / nRows;
            else { hasAutoRow = true; ch += rowHeights[i]; }
        }
        ch += barH * static_cast<float>(r1 - r0);
        if (hasAutoRow && ch <= 0) ch = availH;

        // An Auto track sizes to its child's natural width, so the child has to
        // be measured without a width limit. Using the column's current (zero)
        // size would collapse every Auto column, which is how list item
        // templates lost their right-hand column.
        const Size want = c->Measure(Size{hasAuto ? 1.0e6f : std::max(0.0f, cw),
                                          std::max(0.0f, ch)});

        // Auto tracks grow to the child's desired size, divided across the
        // tracks it spans (a spanning child does not get its whole width in
        // each track).
        const float spanCols = static_cast<float>(c1 - c0 + 1);
        const float spanRows = static_cast<float>(r1 - r0 + 1);
        for (size_t i = c0; i <= c1; ++i) {
            if (columns_[i].kind == GridLength::Kind::Auto)
                colWidths[i] = std::max(colWidths[i], std::ceil(want.w / spanCols));
        }
        for (size_t i = r0; i <= r1; ++i) {
            if (rows_[i].kind == GridLength::Kind::Auto)
                rowHeights[i] = std::max(rowHeights[i], std::ceil(want.h / spanRows));
        }
    }

    // Pass 2: final track sizes. Auto is now known; Star splits the remainder.
    float autoW = 0;
    for (size_t i = 0; i < nCols; ++i)
        if (columns_[i].kind == GridLength::Kind::Auto) autoW += colWidths[i];
    const float starSpaceW = std::max(0.0f, availW - fixedW - autoW - totalColSpacing);
    colOffsets_.assign(nCols, 0.0f);
    float totalW = totalColSpacing;
    for (size_t i = 0; i < nCols; ++i) {
        float w = 0;
        switch (columns_[i].kind) {
            case GridLength::Kind::Fixed: w = columns_[i].value; break;
            case GridLength::Kind::Auto: w = colWidths[i]; break;
            case GridLength::Kind::Star:
                w = starWeightW > 0 ? starSpaceW * (columns_[i].value / starWeightW) : 0;
                break;
        }
        colOffsets_[i] = w;
        totalW += w;
    }

    float autoH = 0;
    for (size_t i = 0; i < nRows; ++i)
        if (rows_[i].kind == GridLength::Kind::Auto) autoH += rowHeights[i];
    const float starSpaceH = std::max(0.0f, availH - fixedH - autoH - totalRowSpacing);
    rowOffsets_.assign(nRows, 0.0f);
    float totalH = totalRowSpacing;
    for (size_t i = 0; i < nRows; ++i) {
        float h = 0;
        switch (rows_[i].kind) {
            case GridLength::Kind::Fixed: h = rows_[i].value; break;
            case GridLength::Kind::Auto: h = rowHeights[i]; break;
            case GridLength::Kind::Star:
                h = starWeightH > 0 ? starSpaceH * (rows_[i].value / starWeightH) : 0;
                break;
        }
        rowOffsets_[i] = h;
        totalH += h;
    }

    desired_ = Size{totalW + margin.H() + pad.H(), totalH + margin.V() + pad.V()};
    ApplySizeConstraints();
    return desired_;
}

void Grid::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float barW = Get<float>(SlotOf("ColumnSpacing"));
    const float barH = Get<float>(SlotOf("RowSpacing"));
    const float x0 = finalRect.x + margin.l + pad.l;
    const float y0 = finalRect.y + margin.t + pad.t;
    const size_t nCols = colOffsets_.size();
    const size_t nRows = rowOffsets_.size();
    if (nCols == 0 || nRows == 0) return;

    // Star tracks absorb whatever extra the parent handed us beyond desired_.
    std::vector<float> colW = colOffsets_;
    std::vector<float> rowH = rowOffsets_;
    float usedW = 0;
    float starW = 0;
    for (size_t i = 0; i < nCols; ++i) {
        usedW += colW[i];
        if (columns_[i].kind == GridLength::Kind::Star) starW += columns_[i].value;
    }
    usedW += barW * (nCols > 1 ? static_cast<float>(nCols - 1) : 0.0f);
    const float availW = std::max(0.0f, finalRect.w - margin.H() - pad.H());
    if (starW > 0 && availW > usedW) {
        const float extra = availW - usedW;
        for (size_t i = 0; i < nCols; ++i)
            if (columns_[i].kind == GridLength::Kind::Star)
                colW[i] += extra * (columns_[i].value / starW);
    }
    float usedH = 0;
    float starH = 0;
    for (size_t i = 0; i < nRows; ++i) {
        usedH += rowH[i];
        if (rows_[i].kind == GridLength::Kind::Star) starH += rows_[i].value;
    }
    usedH += barH * (nRows > 1 ? static_cast<float>(nRows - 1) : 0.0f);
    const float availH = std::max(0.0f, finalRect.h - margin.V() - pad.H() * 0 - pad.V());
    if (starH > 0 && availH > usedH) {
        const float extra = availH - usedH;
        for (size_t i = 0; i < nRows; ++i)
            if (rows_[i].kind == GridLength::Kind::Star)
                rowH[i] += extra * (rows_[i].value / starH);
    }

    std::vector<float> colX(nCols, 0.0f);
    float cx = x0;
    for (size_t i = 0; i < nCols; ++i) {
        colX[i] = cx;
        cx += colW[i] + barW;
    }
    std::vector<float> rowY(nRows, 0.0f);
    float cy = y0;
    for (size_t i = 0; i < nRows; ++i) {
        rowY[i] = cy;
        cy += rowH[i] + barH;
    }

    for (auto& c : LayoutChildren()) {
        const int row = c->SlotOf("Grid.Row") >= 0 ? c->Get<int>(c->SlotOf("Grid.Row")) : 0;
        const int col = c->SlotOf("Grid.Column") >= 0 ? c->Get<int>(c->SlotOf("Grid.Column")) : 0;
        const int rowSpan = std::max(1, c->SlotOf("Grid.RowSpan") >= 0
                                            ? c->Get<int>(c->SlotOf("Grid.RowSpan"))
                                            : 1);
        const int colSpan = std::max(1, c->SlotOf("Grid.ColumnSpan") >= 0
                                            ? c->Get<int>(c->SlotOf("Grid.ColumnSpan"))
                                            : 1);
        const size_t r0 = static_cast<size_t>(std::min<int>(std::max(0, row), static_cast<int>(nRows) - 1));
        const size_t c0 = static_cast<size_t>(std::min<int>(std::max(0, col), static_cast<int>(nCols) - 1));
        const size_t r1 = std::min(nRows - 1, r0 + static_cast<size_t>(rowSpan) - 1);
        const size_t c1 = std::min(nCols - 1, c0 + static_cast<size_t>(colSpan) - 1);

        float w = barW * static_cast<float>(c1 - c0);
        for (size_t i = c0; i <= c1; ++i) w += colW[i];
        float h = barH * static_cast<float>(r1 - r0);
        for (size_t i = r0; i <= r1; ++i) h += rowH[i];
        c->Arrange(Rect{colX[c0], rowY[r0], w, h});
    }
}

void Grid::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
}

// ---------------------------------------------------------------------------
// WrapPanel
// ---------------------------------------------------------------------------
Size WrapPanel::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float spacing = Get<float>(SlotOf("Spacing"));
    const bool horizontal = Get<int>(SlotOf("Orientation")) == 0;
    const float availW = std::max(0.0f, available.w - margin.H() - pad.H());

    float lineExtent = 0;   // extent along the flow axis for the current line
    float crossExtent = 0;  // extent across the flow axis (max over the line)
    float totalCross = 0;
    float maxLineExtent = 0;
    int lines = 0;

    for (auto& c : LayoutChildren()) {
        const Size s = c->Measure(Size{horizontal ? availW : 1.0e6f, horizontal ? 1.0e6f : available.h});
        const float along = horizontal ? s.w : s.h;
        const float across = horizontal ? s.h : s.w;
        const bool wrap = lineExtent > 0 && (lineExtent + spacing + along) > availW && horizontal;
        if (wrap) {
            totalCross += crossExtent + spacing;
            maxLineExtent = std::max(maxLineExtent, lineExtent);
            lineExtent = 0;
            crossExtent = 0;
            ++lines;
        }
        lineExtent += (lineExtent > 0 ? spacing : 0) + along;
        crossExtent = std::max(crossExtent, across);
    }
    totalCross += crossExtent;
    maxLineExtent = std::max(maxLineExtent, lineExtent);
    if (!children_.empty()) ++lines;

    desired_ = horizontal
        ? Size{maxLineExtent + margin.H() + pad.H(), totalCross + margin.V() + pad.V()}
        : Size{totalCross + margin.H() + pad.H(), maxLineExtent + margin.V() + pad.V()};
    ApplySizeConstraints();
    return desired_;
}

void WrapPanel::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float spacing = Get<float>(SlotOf("Spacing"));
    const bool horizontal = Get<int>(SlotOf("Orientation")) == 0;
    const float x0 = finalRect.x + margin.l + pad.l;
    const float y0 = finalRect.y + margin.t + pad.t;
    const float availW = std::max(0.0f, finalRect.w - margin.H() - pad.H());

    float cursor = 0;   // along the flow axis
    float lineY = 0;    // across axis position of the current line
    float lineCross = 0;
    for (auto& c : LayoutChildren()) {
        const Size s = c->DesiredSize();
        const float along = horizontal ? s.w : s.h;
        const float across = horizontal ? s.h : s.w;
        if (horizontal && cursor > 0 && (cursor + spacing + along) > availW) {
            lineY += lineCross + spacing;
            cursor = 0;
            lineCross = 0;
        }
        if (horizontal) c->Arrange(Rect{x0 + cursor, y0 + lineY, along, across});
        else c->Arrange(Rect{x0 + lineY, y0 + cursor, across, along});
        cursor += along + spacing;
        lineCross = std::max(lineCross, across);
    }
}

void WrapPanel::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
}

// ---------------------------------------------------------------------------
// VariableSizedWrapGrid
//
// Cells of a fixed size, flow left-to-right and wrap; a child with
// VariableSizedWrapGrid.ColumnSpan / RowSpan occupies a rectangle of cells.
// Unlike Grid there is no track sizing -- cell size is declared and children
// are clipped into it, which is what makes the "size varies per item" case
// cheap.
// ---------------------------------------------------------------------------
Size VariableSizedWrapGrid::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float cellW = std::max(1.0f, Get<float>(SlotOf("ItemWidth")));
    const float cellH = std::max(1.0f, Get<float>(SlotOf("ItemHeight")));
    const float availW = std::max(0.0f, available.w - margin.H() - pad.H());
    const int perRow = std::max(1, static_cast<int>(availW / cellW));

    const auto cells = PlanCells(perRow);
    int maxRow = 0;
    int maxCol = 0;
    for (size_t i = 0; i < children_.size() && i < cells.size(); ++i) {
        const auto& cell = cells[i];
        children_[i]->Measure(Size{cellW * static_cast<float>(cell.colSpan),
                                   cellH * static_cast<float>(cell.rowSpan)});
        maxRow = std::max(maxRow, cell.row + cell.rowSpan);
        maxCol = std::max(maxCol, cell.col + cell.colSpan);
    }
    desired_ = Size{std::max(1, maxCol) * cellW + margin.H() + pad.H(),
                    std::max(1, maxRow) * cellH + margin.V() + pad.V()};
    ApplySizeConstraints();
    return desired_;
}

// Assigns each child a cell, honouring spans.
//
// A plain "advance a cursor" loop breaks as soon as a child spans rows: the
// next child lands on a column the tall child already occupies, and items
// overlap. This walks a boolean occupancy grid instead, which is what makes the
// mixed-size case actually correct.
std::vector<VariableSizedWrapGrid::Cell> VariableSizedWrapGrid::PlanCells(int perRow) const {
    std::vector<Cell> cells(children_.size());
    if (perRow < 1) perRow = 1;
    // Occupancy grid, grown on demand. Generous initial height: the row count is
    // bounded by children * maxRowSpan.
    const int estRows = static_cast<int>(children_.size()) * 4 + 4;
    std::vector<std::vector<bool>> used(static_cast<size_t>(estRows),
                                        std::vector<bool>(static_cast<size_t>(perRow), false));

    auto ensureRow = [&](int r) {
        while (static_cast<int>(used.size()) <= r) {
            used.emplace_back(static_cast<size_t>(perRow), false);
        }
    };
    auto fits = [&](int r, int c, int cs, int rs) {
        if (c + cs > perRow) return false;
        for (int y = r; y < r + rs; ++y) {
            ensureRow(y);
            for (int x = c; x < c + cs; ++x) {
                if (used[static_cast<size_t>(y)][static_cast<size_t>(x)]) return false;
            }
        }
        return true;
    };

    int cursorRow = 0;
    int cursorCol = 0;
    for (size_t i = 0; i < children_.size(); ++i) {
        const Widget& c = *children_[i];
        const int cs = std::max(1, c.SlotOf("VariableSizedWrapGrid.ColumnSpan") >= 0
                                       ? c.Get<int>(c.SlotOf("VariableSizedWrapGrid.ColumnSpan"))
                                       : 1);
        const int rs = std::max(1, c.SlotOf("VariableSizedWrapGrid.RowSpan") >= 0
                                       ? c.Get<int>(c.SlotOf("VariableSizedWrapGrid.RowSpan"))
                                       : 1);
        // Scan forward for the first free rectangle.
        int r = cursorRow;
        int col = cursorCol;
        for (;;) {
            if (col + cs > perRow) { col = 0; ++r; continue; }
            if (fits(r, col, cs, rs)) break;
            ++col;
            if (col >= perRow) { col = 0; ++r; }
        }
        for (int y = r; y < r + rs; ++y) {
            ensureRow(y);
            for (int x = col; x < col + cs; ++x) used[static_cast<size_t>(y)][static_cast<size_t>(x)] = true;
        }
        cells[i] = Cell{r, col, cs, rs};
        cursorRow = r;
        cursorCol = col + cs;
        if (cursorCol >= perRow) { cursorCol = 0; ++cursorRow; }
    }
    return cells;
}

void VariableSizedWrapGrid::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float cellW = std::max(1.0f, Get<float>(SlotOf("ItemWidth")));
    const float cellH = std::max(1.0f, Get<float>(SlotOf("ItemHeight")));
    const float x0 = finalRect.x + margin.l + pad.l;
    const float y0 = finalRect.y + margin.t + pad.t;
    const float availW = std::max(0.0f, finalRect.w - margin.H() - pad.H());
    const int perRow = std::max(1, static_cast<int>(availW / cellW));

    const auto cells = PlanCells(perRow);
    for (size_t i = 0; i < children_.size() && i < cells.size(); ++i) {
        const auto& cell = cells[i];
        children_[i]->Arrange(Rect{x0 + static_cast<float>(cell.col) * cellW,
                                   y0 + static_cast<float>(cell.row) * cellH,
                                   cellW * static_cast<float>(cell.colSpan),
                                   cellH * static_cast<float>(cell.rowSpan)});
    }
}

void VariableSizedWrapGrid::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
}

// ---------------------------------------------------------------------------
// Viewbox
// ---------------------------------------------------------------------------
Size Viewbox::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Size inner{std::max(0.0f, available.w - margin.H() - pad.H()),
                     std::max(0.0f, available.h - margin.V() - pad.V())};
    float w = 0;
    float h = 0;
    for (auto& c : LayoutChildren()) {
        const Size s = c->Measure(inner);
        w = std::max(w, s.w);
        h = std::max(h, s.h);
    }
    // The child keeps its natural size; the box reports what the child needs so
    // the scale factor is known at arrange time.
    desired_ = Size{w + margin.H() + pad.H(), h + margin.V() + pad.V()};
    ApplySizeConstraints();
    return desired_;
}

void Viewbox::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect box{finalRect.x + margin.l + pad.l, finalRect.y + margin.t + pad.t,
                   std::max(0.0f, finalRect.w - margin.H() - pad.H()),
                   std::max(0.0f, finalRect.h - margin.V() - pad.V())};
    for (auto& c : LayoutChildren()) {
        const Size natural = c->DesiredSize();
        if (natural.w <= 0 || natural.h <= 0) { c->Arrange(box); continue; }
        // Uniform fit: never stretch beyond natural size, only shrink to fit.
        const float sx = box.w > 0 ? box.w / natural.w : 1.0f;
        const float sy = box.h > 0 ? box.h / natural.h : 1.0f;
        const float scale = std::min({1.0f, sx, sy});
        const float w = natural.w * scale;
        const float h = natural.h * scale;
        const bool stretch = Get<bool>(SlotOf("Stretch"));
        const float cx = stretch ? box.x : box.x + (box.w - w) * 0.5f;
        const float cy = stretch ? box.y : box.y + (box.h - h) * 0.5f;
        c->Arrange(Rect{cx, cy, w, h});
    }
}

void Viewbox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
}

// ---------------------------------------------------------------------------
// Expander
// ---------------------------------------------------------------------------
void Expander::SetExpanded(bool value) {
    if (expanded_ == value) return;
    expanded_ = value;
    Invalidate(AffectsMeasure | AffectsRender);
}

Rect Expander::HeaderBounds() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float headerH = Get<float>(SlotOf("HeaderHeight"));
    return Rect{bounds_.x + margin.l, bounds_.y + margin.t,
                std::max(0.0f, bounds_.w - margin.H()), headerH};
}

Size Expander::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float headerH = Get<float>(SlotOf("HeaderHeight"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const std::wstring header = Get<std::wstring>(SlotOf("Header"));
    float headerW = static_cast<float>(header.size()) * fontSize * 0.55f;
    if (ITextService* svc = TextSystem::Get()) {
        if (!header.empty()) headerW = svc->Measure(header, fontSize, Get<int>(SlotOf("FontWeight"))).w;
    }
    const float indent = headerH + 8.0f;
    const Size inner{std::max(0.0f, available.w - margin.H() - pad.H() - indent), 1.0e6f};

    float bodyW = 0;
    float bodyH = 0;
    if (expanded_) {
        for (auto& c : LayoutChildren()) {
            const Size s = c->Measure(inner);
            bodyW = std::max(bodyW, s.w);
            bodyH += s.h;
        }
    }
    const float w = std::max(headerW + indent, bodyW + indent) + pad.H() + margin.H();
    const float h = headerH + (expanded_ ? bodyH + pad.V() : 0.0f) + margin.V();
    desired_ = Size{w, h};
    ApplySizeConstraints();
    return desired_;
}

void Expander::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float headerH = Get<float>(SlotOf("HeaderHeight"));
    const float indent = headerH + 8.0f;
    if (!expanded_) return;
    float y = finalRect.y + margin.t + headerH;
    const float x = finalRect.x + margin.l + indent;
    const float w = std::max(0.0f, finalRect.w - margin.H() - indent);
    for (auto& c : LayoutChildren()) {
        const float h = c->DesiredSize().h;
        c->Arrange(Rect{x, y, w, h});
        y += h;
    }
}

void Expander::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Rect header = HeaderBounds();
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRoundRect(header, 4.0f, bg);

    // Chevron: ">" collapsed, "v" expanded -- drawn as text so no new
    // primitive is needed for a glyph.
    const float headerH = Get<float>(SlotOf("HeaderHeight"));
    const Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    const float fontSize = Get<float>(SlotOf("FontSize"));
    ctx.DrawText(expanded_ ? L"v" : L">", Rect{header.x + 6, header.y, headerH - 8, headerH}, fg,
                 fontSize, Get<int>(SlotOf("FontWeight")), TextAlign::Left);
    const std::wstring text = Get<std::wstring>(SlotOf("Header"));
    if (!text.empty()) {
        ctx.DrawText(text, Rect{header.x + headerH, header.y,
                                std::max(0.0f, header.w - headerH - 8), headerH},
                     fg, fontSize, Get<int>(SlotOf("FontWeight")), TextAlign::Left);
    }
}

Widget* Expander::HitTest(const Point& p) {
    // Only the header toggles; the body behaves like a normal container.
    if (HeaderBounds().Contains(p)) return this;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (Widget* hit = (*it)->HitTest(p)) return hit;
    }
    return nullptr;
}

void Expander::OnPointerUp(const Point&) { SetExpanded(!expanded_); }

bool Expander::OnKeyDown(int vk) {
    if (vk == 0x20 || vk == 0x0D) { // VK_SPACE / VK_RETURN
        SetExpanded(!expanded_);
        return true;
    }
    return Widget::OnKeyDown(vk);
}


// ---------------------------------------------------------------------------
// ListBox
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Selector
//
// The index is the source of truth and SelectedItem is derived from it. Keeping
// two independent fields would let them disagree -- a classic bug where the
// programmatic path sets one and the click path sets the other.
// ---------------------------------------------------------------------------
int Selector::ClampIndex(int index) const {
    const int count = static_cast<int>(ItemCount());
    if (index < 0) return -1;
    if (index >= count) return count > 0 ? count - 1 : -1;
    return index;
}

RefPtr Selector::SelectedItem() const {
    if (selectedIndex_ < 0) return nullptr;
    return ItemAt(static_cast<size_t>(selectedIndex_));
}

void Selector::SetSelectedIndex(int index) {
    const int clamped = ClampIndex(index);
    if (clamped == selectedIndex_) return;
    selectedIndex_ = clamped;
    // Tell the ViewModel, unless we are already reacting to it. Without the
    // guard, a TwoWay binding would echo: target change -> source change ->
    // target change -> ...
    if (!settingIndex_ && slotSelectedIndex_ >= 0) {
        settingIndex_ = true;
        SetSlotValue(slotSelectedIndex_, Value::Number(clamped), true);
        settingIndex_ = false;
    }
    OnSelectionChanged();
    Invalidate(AffectsRender);
}

void Selector::OnSlotChanged(int slot, const Value& value) {
    if (slot == slotSelectedIndex_) {
        const int wanted = value.type == VType::Number ? static_cast<int>(value.num) : -1;
        if (settingIndex_) return; // came from us; do not bounce back
        const int clamped = ClampIndex(wanted);
        if (clamped != selectedIndex_) {
            selectedIndex_ = clamped;
            OnSelectionChanged();
            Invalidate(AffectsRender);
        }
        return;
    }
    if (slot == slotSelectedItem_) {
        // Resolve the item back to an index so both entry points agree.
        auto item = Get<RefPtr>(slotSelectedItem_);
        int resolved = -1;
        if (item) {
            const size_t count = ItemCount();
            for (size_t i = 0; i < count; ++i) {
                if (ItemAt(i) == item) {
                    resolved = static_cast<int>(i);
                    break;
                }
            }
        }
        if (resolved != selectedIndex_) {
            selectedIndex_ = resolved;
            // Push the index too, so a VM bound to SelectedIndex stays in sync.
            if (slotSelectedIndex_ >= 0) {
                settingIndex_ = true;
                SetSlotValue(slotSelectedIndex_, Value::Number(resolved), true);
                settingIndex_ = false;
            }
            OnSelectionChanged();
            Invalidate(AffectsRender);
        }
    }
}

ListBox::ListBox() {
    BindType(TypeRegistry::Instance().Find("wui/ListBox"));
    slotItems_ = SlotOf("ItemsSource");
    slotTemplate_ = SlotOf("ItemTemplate");
    slotSelectedIndex_ = SlotOf("SelectedIndex");
    slotSelectedItem_ = SlotOf("SelectedItem");
}

ListBox::~ListBox() { UnsubscribeSource(); }

void ListBox::SubscribeSource() {
    auto ref = Get<RefPtr>(slotItems_);
    auto* list = dynamic_cast<ObservableListBase*>(ref.get());
    if (list == list_) return;
    UnsubscribeSource();
    list_ = list;
    listRef_ = ref;
    if (list_) list_->Subscribe(this);
    RebuildItems(text_);
}

void ListBox::UnsubscribeSource() {
    if (list_) list_->Unsubscribe(this);
    list_ = nullptr;
    listRef_.reset();
}

void ListBox::OnSlotChanged(int slot, const Value& value) {
    if (slot == slotItems_) SubscribeSource();
    if (slot == slotTemplate_) RebuildItems(text_);
    Selector::OnSlotChanged(slot, value);
}

size_t ListBox::ItemCount() const { return itemWidgets_.size(); }

RefPtr ListBox::ItemAt(size_t index) const {
    if (!list_ || index >= list_->Count()) return nullptr;
    return list_->ItemAt(index);
}

void ListBox::OnSelectionChanged() {
    // Keep the visual highlight and any scroll position consistent with the
    // selection: a selection the user cannot see is indistinguishable from no
    // selection at all.
    Invalidate(AffectsRender);
}

bool ListBox::OnKeyDown(int vk) {
    const int count = static_cast<int>(ItemCount());
    if (count == 0) return Widget::OnKeyDown(vk);
    switch (vk) {
    case KeyDown:
        SetSelectedIndex(std::min(selectedIndex_ + 1, count - 1));
        return true;
    case KeyUp:
        SetSelectedIndex(std::max(selectedIndex_ - 1, 0));
        return true;
    case KeyHome:
        SetSelectedIndex(0);
        return true;
    case KeyEnd:
        SetSelectedIndex(count - 1);
        return true;
    default:
        return Widget::OnKeyDown(vk);
    }
}

void ListBox::OnPointerDown(const Point& local) {
    // Selection happens on press, matching how lists behave everywhere else --
    // press-and-drag-off still leaves the row selected.
    const size_t count = ItemCount();
    for (size_t i = 0; i < count; ++i) {
        Widget* item = ItemWidgetAt(i);
        if (item && item->Bounds().Contains(local)) {
            SetSelectedIndex(static_cast<int>(i));
            break;
        }
    }
}

void ListBox::OnPointerUp(const Point&) {}

void ListBox::OnListChanged(const ListChange&) { RebuildItems(text_); }

void ListBox::RebuildItems(ITextService* text) {
    text_ = text;
    for (auto& w : itemWidgets_) w->DetachBindings();
    itemWidgets_.clear();
    ClearChildren();
    if (!list_) return;
    auto tmpl = std::dynamic_pointer_cast<DataTemplate>(Get<RefPtr>(slotTemplate_));
    template_ = tmpl;
    for (size_t i = 0; i < list_->Count(); ++i) {
        RefPtr item = list_->ItemAt(i);
        WidgetPtr w;
        if (template_ && template_->instantiate) w = template_->instantiate();
        if (!w) {
            auto tb = std::make_shared<TextBlock>();
            tb->BindType(TypeRegistry::Instance().Find("wui/TextBlock"));
            tb->SetSlotValue(tb->SlotOf("Text"), Value::Text(FormatValueText(Value::Ref(item))));
            w = tb;
        }
        w->SetDataContext(item);
        w->AttachBindings(text_);
        itemWidgets_.push_back(w);
        AddChild(w);
    }
    Invalidate(AffectsMeasure | AffectsRender);
}

Size ListBox::Measure(const Size& available) {
    const float itemHeight = Get<float>(SlotOf("ItemHeight"));
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    if (itemWidgets_.empty()) RebuildItems(text_);
    const Size inner{std::max(0.0f, available.w - margin.H() - pad.H()), available.h};
    float maxW = 0;
    for (auto& w : itemWidgets_) maxW = std::max(maxW, w->Measure(inner).w);
    const float h = itemHeight * static_cast<float>(itemWidgets_.size());
    desired_ = Size{std::max(200.0f, maxW + margin.H() + pad.H()), h + margin.V() + pad.V()};
    ApplySizeConstraints();
    return desired_;
}

void ListBox::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float itemHeight = Get<float>(SlotOf("ItemHeight"));
    const Rect content{finalRect.x + margin.l + pad.l, finalRect.y + margin.t + pad.t,
                       std::max(0.0f, finalRect.w - margin.H() - pad.H()),
                       std::max(0.0f, finalRect.h - margin.V() - pad.V())};
    float cursor = content.y;
    for (auto& w : itemWidgets_) {
        w->Arrange(Rect{content.x, cursor, content.w, itemHeight});
        cursor += itemHeight;
    }
}

void ListBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
    ctx.PushClip(bounds_);
    // Selection highlight goes UNDER the row content so text stays readable.
    if (selectedIndex_ >= 0 && static_cast<size_t>(selectedIndex_) < itemWidgets_.size()) {
        const Rect row = itemWidgets_[static_cast<size_t>(selectedIndex_)]->Bounds();
        const Color accent = palette.colors[static_cast<int>(Token::Accent)];
        ctx.FillRoundRect(row, 4.0f, Color{accent.r, accent.g, accent.b, 0.18f});
    }
    for (auto& w : itemWidgets_) w->PaintTree(ctx, palette);
    ctx.PopClip();
}

Widget* ListBox::HitTest(const Point& p) {
    for (auto it = itemWidgets_.rbegin(); it != itemWidgets_.rend(); ++it) {
        if (Widget* hit = (*it)->HitTest(p)) return hit;
    }
    return bounds_.Contains(p) ? this : nullptr;
}


// ---------------------------------------------------------------------------
// TabView
//
// Geometry is computed in one place (EnsureTabWidths) and then read by both paint
// and hit testing. Computing it twice is how a close button ends up drawn in one
// spot and clickable in another.
// ---------------------------------------------------------------------------
namespace {
constexpr float kTabHeight = 32.0f;
constexpr float kTabPadding = 12.0f;
constexpr float kCloseSize = 14.0f;
constexpr float kDirtyDot = 6.0f;
constexpr float kStripGap = 2.0f;
constexpr float kAddWidth = 32.0f;
} // namespace

TabView::TabView() {
    BindType(TypeRegistry::Instance().Find("wui/TabView"));
    slotItems_ = SlotOf("ItemsSource");
    slotAddCommand_ = SlotOf("AddCommand");
    slotSelectedIndex_ = SlotOf("SelectedIndex");
    slotSelectedItem_ = SlotOf("SelectedItem");
    slotCloseRequested_ = SlotOf("CloseRequested");
}

TabView::~TabView() { UnsubscribeSource(); }

void TabView::SubscribeSource() {
    auto ref = Get<RefPtr>(slotItems_);
    auto* list = dynamic_cast<ObservableListBase*>(ref.get());
    if (list == list_) return;
    UnsubscribeSource();
    list_ = list;
    listRef_ = ref;
    if (list_) list_->Subscribe(this);
    Invalidate(AffectsMeasure);
}

void TabView::UnsubscribeSource() {
    if (list_) list_->Unsubscribe(this);
    list_ = nullptr;
    listRef_.reset();
}

void TabView::OnSlotChanged(int slot, const Value& value) {
    if (slot == slotItems_) SubscribeSource();
    Selector::OnSlotChanged(slot, value);
}

void TabView::OnListChanged(const ListChange&) {
    // A tab was added or removed: the strip geometry changes, and the selection
    // may now point past the end.
    if (selectedIndex_ >= static_cast<int>(ItemCount())) {
        selectedIndex_ = static_cast<int>(ItemCount()) - 1;
    }
    tabWidths_.clear();
    Invalidate(AffectsMeasure);
}

size_t TabView::ItemCount() const { return list_ ? list_->Count() : 0; }

RefPtr TabView::ItemAt(size_t index) const {
    if (!list_ || index >= list_->Count()) return nullptr;
    return list_->ItemAt(index);
}

void TabView::OnSelectionChanged() {
    Invalidate(AffectsMeasure | AffectsRender);
}

Rect TabView::StripBounds() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    return Rect{bounds_.x + margin.l, bounds_.y + margin.t,
                std::max(0.0f, bounds_.w - margin.H()), kTabHeight};
}

// Tab titles come from the model, not from stringifying the item: a tab strip
// shows the document's name, and only the model knows what that is. TabEntryModel
// is the supplied model; a sample with its own document class binds through the
// normal binding layer instead (see samples/list), so this path covers the
// ItemsSource-of-TabEntryModel case.
std::wstring TabView::TitleFor(size_t index) {
    auto item = ItemAt(index);
    if (!item) return L"Tab";
    if (auto entry = std::dynamic_pointer_cast<TabEntryModel>(item)) return entry->Title();
    if (auto* bound = dynamic_cast<IBoundTitle*>(item.get())) return bound->BoundTitle();
    return L"Tab";
}

float TabView::TabWidthFor(size_t index) {
    const std::wstring title = TitleFor(index);
    float w = kTabPadding * 2 + kCloseSize + kStripGap + kDirtyDot;
    if (ITextService* text = TextSystem::Get()) {
        const Size m = text->Measure(title, 13.0f, WeightNormal);
        w += m.w;
    }
    return w;
}

void TabView::EnsureTabWidths() {
    const size_t count = ItemCount();
    if (tabWidths_.size() != count) {
        tabWidths_.resize(count);
        for (size_t i = 0; i < count; ++i) tabWidths_[i] = TabWidthFor(i);
    }
}

Rect TabView::TabBounds(size_t index) {
    EnsureTabWidths();
    const Rect strip = StripBounds();
    if (index >= tabWidths_.size()) return Rect{strip.x, strip.y, 0, 0};
    float x = strip.x - stripScroll_;
    for (size_t i = 0; i < index; ++i) x += tabWidths_[i] + kStripGap;
    return Rect{x, strip.y, tabWidths_[index], kTabHeight};
}

Rect TabView::CloseButtonBounds(size_t index) {
    const Rect tab = TabBounds(index);
    return Rect{tab.x + tab.w - kTabPadding * 0.5f - kCloseSize,
                tab.y + (kTabHeight - kCloseSize) * 0.5f, kCloseSize, kCloseSize};
}

Rect TabView::AddButtonBounds() {
    const Rect strip = StripBounds();
    float x = strip.x - stripScroll_;
    for (float w : tabWidths_) x += w + kStripGap;
    return Rect{x, strip.y, kAddWidth, kTabHeight};
}

Size TabView::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float outerW = std::min(available.w, margin.H() + 320.0f);
    const float innerW = std::max(0.0f, outerW - margin.H());
    EnsureTabWidths();
    float desired = kTabHeight;
    // The content area takes the remaining height. Asking the selected item for
    // its own height keeps a short tab from being stretched to fill the window.
    if (selectedIndex_ >= 0) {
        if (Widget* content = TabContentAt(static_cast<size_t>(selectedIndex_))) {
            const Size s = content->Measure(Size{innerW, 1.0e6f});
            desired += s.h;
        }
    }
    // An explicit Height wins over the content height, and the content then gets the
    // difference. Without this a TabView declared to fill a page reported only
    // tab-strip-plus-content height, so it sat at the top of its grid row and the
    // content region had nothing to fill -- the editor area was a tall empty frame
    // with the text box stuck to its top edge. ApplySizeConstraints already honours
    // Height for the DESIRED size; this makes the same number available to Arrange
    // so the child is actually stretched into it.
    const float explicitH = SlotOf("Height") >= 0 ? Get<float>(SlotOf("Height")) : 0.0f;
    if (explicitH > 0) desired = std::max(desired, explicitH);
    desired_ = Size{outerW, desired};
    ApplySizeConstraints();
    return desired_;
}

void TabView::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float contentY = finalRect.y + margin.t + kTabHeight;
    contentTop_ = contentY;
    const float w = std::max(0.0f, finalRect.w - margin.H());
    // Only the selected tab's content participates in layout; the others are not
    // measured, so their children keep stale bounds and cannot steal clicks.
    for (size_t i = 0; i < ItemCount(); ++i) {
        if (Widget* content = TabContentAt(i)) {
            if (static_cast<int>(i) == selectedIndex_) {
                content->Arrange(Rect{finalRect.x + margin.l, contentY, w,
                                      std::max(0.0f, finalRect.h - margin.t - kTabHeight)});
            } else {
                content->Arrange(Rect{0, 0, 0, 0});
            }
        }
    }
}
Widget* TabView::TabContentAt(size_t index) {
    size_t childIndex = 0;
    for (const auto& c : children_) {
        if (!c->IsVisible()) continue;
        if (childIndex == index) return c.get();
        ++childIndex;
    }
    return nullptr;
}

void TabView::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    EnsureTabWidths();

    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRoundRect(bounds_, 6.0f, bg);

    const Rect strip = StripBounds();
    ctx.PushClip(Rect{bounds_.x, bounds_.y, bounds_.w, kTabHeight});

    ITextService* text = TextSystem::Get();
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];
    const Color textPrimary = palette.colors[static_cast<int>(Token::TextPrimary)];
    const Color textSecondary = palette.colors[static_cast<int>(Token::TextSecondary)];

    for (size_t i = 0; i < tabWidths_.size(); ++i) {
        const Rect tab = TabBounds(i);
        const bool active = static_cast<int>(i) == selectedIndex_;

        // Active tab is filled and sits "in front"; inactive ones are flat. This
        // is the whole affordance -- there are no borders to lean on.
        if (active) {
            ctx.FillRoundRect(Rect{tab.x, tab.y + 4.0f, tab.w, tab.h - 4.0f}, 6.0f,
                              palette.colors[static_cast<int>(Token::CardBackground)]);
            ctx.FillRect(Rect{tab.x + 4.0f, tab.y + tab.h - 2.0f, tab.w - 8.0f, 2.0f}, accent);
        }

        // Title comes from the model. A plain TabEntryModel is the common case;
        // a sample with its own document class supplies it through IBoundTitle.
        const std::wstring title = TitleFor(i);
        bool dirty = false;
        bool closable = true;
        if (auto entry = std::dynamic_pointer_cast<TabEntryModel>(ItemAt(i))) {
            dirty = entry->IsDirty();
            closable = entry->IsClosable();
        }
        float textX = tab.x + kTabPadding;
        if (text) {
            const Size m = text->Measure(title, 13.0f, WeightNormal);
            ctx.DrawText(title, Rect{textX, tab.y, m.w, tab.h},
                         active ? textPrimary : textSecondary, 13.0f, WeightNormal, TextAlign::Left);
            textX += m.w + 6.0f;
        }

        // Dirty marker: a dot where the close button would be. Showing both at
        // once would be ambiguous, so the dot replaces the × until saved.
        if (dirty) {
            // A circle drawn as a fully rounded rect -- no ellipse primitive yet.
            const Rect dot{textX, tab.y + (tab.h - kDirtyDot) * 0.5f, kDirtyDot, kDirtyDot};
            ctx.FillRoundRect(dot, kDirtyDot * 0.5f, accent);
        } else if (closable) {
            const Rect close = CloseButtonBounds(i);
            // Two strokes rather than a glyph: no icon font needed.
            const Color xColor = textSecondary;
            ctx.DrawText(L"\u00d7", close, xColor, 15.0f, WeightNormal, TextAlign::Center);
        }
    }

    // The + button, so "add a tab" is discoverable rather than only a menu item.
    const Rect add = AddButtonBounds();
    if (add.x + add.w <= bounds_.x + bounds_.w) {
        ctx.FillRoundRect(add, 4.0f, palette.colors[static_cast<int>(Token::ControlFill)]);
        ctx.DrawText(L"+", add, textPrimary, 16.0f, WeightNormal, TextAlign::Center);
    }
    ctx.PopClip();

    // Content background, so an empty tab still reads as a panel.
    if (contentTop_ > 0 && bounds_.y + bounds_.h > contentTop_) {
        ctx.FillRect(Rect{bounds_.x, contentTop_, bounds_.w, bounds_.y + bounds_.h - contentTop_},
                     palette.colors[static_cast<int>(Token::CardBackground)]);
    }
}

Widget* TabView::HitTest(const Point& p) {
    if (!bounds_.Contains(p)) return nullptr;

    EnsureTabWidths();
    // The strip is checked before the content, because a tab strip overlapping
    // its own content is normal.
    if (p.y >= bounds_.y && p.y < bounds_.y + kTabHeight) {
        if (AddButtonBounds().Contains(p)) return this;
        for (size_t i = 0; i < tabWidths_.size(); ++i) {
            const Rect tab = TabBounds(i);
            if (tab.Contains(p)) return this;
        }
        return this; // empty strip area still belongs to the TabView
    }

    if (selectedIndex_ >= 0) {
        if (Widget* content = TabContentAt(static_cast<size_t>(selectedIndex_))) {
            if (Widget* hit = content->HitTest(p)) return hit;
        }
    }
    return this;
}

void TabView::RequestClose(size_t index) {
    if (index >= ItemCount()) return;
    // closeRequested_ stays sticky so a caller (and a test) can ask "what was the
    // last close request?" after the fact.
    closeRequested_ = static_cast<int>(index);
    // Publish it. The ViewModel decides whether to remove the tab, because only
    // it knows about unsaved changes.
    //
    // Published as an edge, not a level: index first, then back to -1. A level
    // only signal breaks the second close of the same index -- the observable
    // property sees an equal value, suppresses the notification, and × appears to
    // stop working. The ViewModel's contract is "act on values >= 0, ignore the
    // reset", and doing the reset here keeps that from being something each
    // sample has to remember.
    if (slotCloseRequested_ >= 0) {
        SetSlotValue(slotCloseRequested_, Value::Number(index), true);
        SetSlotValue(slotCloseRequested_, Value::Number(-1), true);
    }
    Invalidate(AffectsRender);
}

void TabView::OnPointerUp(const Point& local) {
    // Tabs select on press (below); the strip's ×, the + and the inactive tab
    // body are handled on release so a press-and-drag-away cancels.
    if (local.y >= bounds_.y && local.y < bounds_.y + kTabHeight) {
        if (AddButtonBounds().Contains(local)) {
            PushAddCommand();
            return;
        }
        for (size_t i = 0; i < tabWidths_.size(); ++i) {
            if (!TabBounds(i).Contains(local)) continue;
            // The × sits inside the tab, so it must be tested first -- otherwise
            // closing a tab would just reselect it.
            if (CloseButtonBounds(i).Contains(local)) {
                RequestClose(i);
            }
            return;
        }
    }
}

void TabView::OnPointerDown(const Point& local) {
    if (local.y < bounds_.y || local.y >= bounds_.y + kTabHeight) return;
    for (size_t i = 0; i < tabWidths_.size(); ++i) {
        if (!TabBounds(i).Contains(local)) continue;
        // Pressing the × must not change the selection.
        if (!CloseButtonBounds(i).Contains(local)) SetSelectedIndex(static_cast<int>(i));
        return;
    }
}

void TabView::PushAddCommand() {
    if (slotAddCommand_ < 0) return;
    auto cmd = Get<RefPtr>(slotAddCommand_);
    if (!cmd) return;
    if (auto* relay = dynamic_cast<ICommand*>(cmd.get())) {
        if (relay->CanExecute()) relay->Execute();
    }
}

bool TabView::OnKeyDown(int vk) {
    // Left/Right move between tabs. Ctrl+Tab needs modifier state, which the
    // framework does not track yet (the host would have to pass it in) -- so the
    // arrows are the supported path for now, and Ctrl+Tab is a follow-up rather
    // than a silently dead shortcut.
    const int count = static_cast<int>(ItemCount());
    if (count == 0) return Widget::OnKeyDown(vk);
    if (vk == KeyLeft || vk == KeyRight) {
        int next = selectedIndex_ + (vk == KeyRight ? 1 : -1);
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
        SetSelectedIndex(next);
        return true;
    }
    return Widget::OnKeyDown(vk);
}

// ===========================================================================
// Application shell: MenuBar, CommandBar, ContentDialog
//
// These three all need the SAME missing thing -- a layer that draws above the
// document -- so they are grouped rather than scattered through the file. Their
// common shape: they live in the layout (so they reserve space), but what they
// open does not.
// ===========================================================================

// ---------------------------------------------------------------------------
// MenuBar
// ---------------------------------------------------------------------------
namespace {
constexpr float kMenuBarHeight = 34.0f;
constexpr float kMenuTitlePad = 14.0f;
} // namespace

MenuBar::MenuBar() {
    BindType(TypeRegistry::Instance().Find("wui/MenuBar"));
}

MenuBar::~MenuBar() {
    // The overlay is owned by the root, which may already be gone; clearing our
    // pointer before the layer dies keeps a repaint during teardown from reading
    // freed memory.
    if (layer_) {
        Widget* root = Root();
        if (root && root->Overlay() == layer_) root->SetOverlay(nullptr);
    }
}

std::vector<MenuBarItem*> MenuBar::Items() const {
    std::vector<MenuBarItem*> out;
    for (const auto& c : children_) {
        if (auto* item = dynamic_cast<MenuBarItem*>(c.get())) out.push_back(item);
    }
    return out;
}

void MenuBar::UpdateWidths() {
    const std::vector<MenuBarItem*> items = Items();
    titleWidths_.resize(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        // Hold the title in a local: MenuTitle() returns by value, and calling it
        // twice per title (once to measure, once to draw) would allocate twice in
        // the common path.
        const std::wstring title = items[i]->MenuTitle();
        float w = kMenuTitlePad * 2;
        if (ITextService* svc = TextSystem::Get()) {
            w += svc->Measure(title, 13.0f, WeightNormal).w;
        } else {
            w += static_cast<float>(title.size()) * 13.0f * 0.55f;
        }
        titleWidths_[i] = w;
    }
}

Size MenuBar::Measure(const Size& available) {
    UpdateWidths();
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float explicitH = Get<float>(SlotOf("Height"));
    const float h = explicitH > 0 ? explicitH : kMenuBarHeight;
    float w = margin.H();
    for (float tw : titleWidths_) w += tw;
    desired_ = Size{available.w > 0 ? available.w : w, h + margin.V()};
    ApplySizeConstraints();
    for (auto& c : children_) c->Measure(Size{0, h});
    return desired_;
}

void MenuBar::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    float x = finalRect.x + margin.l;
    const float y = finalRect.y + margin.t;
    const float h = std::max(0.0f, finalRect.h - margin.V());
    for (size_t i = 0; i < children_.size(); ++i) {
        // The bar's children are not drawn; they own the item models and are given
        // the title's rect so a templated item can use it later.
        const float w = i < titleWidths_.size() ? titleWidths_[i] : 0.0f;
        children_[i]->Arrange(Rect{x, y, w, h});
        x += w;
    }
}

Rect MenuBar::TitleBounds(size_t index) const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    float x = bounds_.x + margin.l;
    for (size_t i = 0; i < index && i < titleWidths_.size(); ++i) x += titleWidths_[i];
    const float w = index < titleWidths_.size() ? titleWidths_[index] : 0.0f;
    return Rect{x, bounds_.y + margin.t, w, std::max(0.0f, bounds_.h - margin.V())};
}

void MenuBar::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) {
        ctx.FillRect(bounds_, bg);
    } else {
        // With no background the bar still needs to be separated from the page,
        // otherwise the titles appear to float in the content.
        const Color line = palette.colors[static_cast<int>(Token::CardBorder)];
        ctx.FillRect(Rect{bounds_.x, bounds_.y + bounds_.h - 1.0f, bounds_.w, 1.0f},
                     Color{line.r, line.g, line.b, 0.7f});
    }

    const Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];
    const std::vector<MenuBarItem*> items = Items();
    for (size_t i = 0; i < items.size(); ++i) {
        const Rect title = TitleBounds(i);
        const bool isOpen = static_cast<int>(i) == openIndex_;
        if (isOpen) {
            ctx.FillRoundRect(Rect{title.x + 2, title.y + 4, title.w - 4, title.h - 8}, 4.0f,
                              palette.colors[static_cast<int>(Token::ControlFillHover)]);
        }
        ctx.DrawText(items[i]->MenuTitle(), title, fg, 13.0f, WeightNormal, TextAlign::Center);
        if (isOpen) {
            ctx.FillRect(Rect{title.x + 6.0f, title.y + title.h - 4.0f, title.w - 12.0f, 2.0f},
                         accent);
        }
    }
}

Widget* MenuBar::HitTest(const Point& p) { return bounds_.Contains(p) ? this : nullptr; }

void MenuBar::OnPointerDown(const Point& local) {
    const std::vector<MenuBarItem*> items = Items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (TitleBounds(i).Contains(local)) {
            if (openIndex_ == static_cast<int>(i)) CloseMenu();
            else OpenMenu(i);
            return;
        }
    }
    CloseMenu();
}

void MenuBar::OnPointerMove(const Point& local) {
    // Once a menu is open, sliding across the titles switches menus without a
    // click. This is the behaviour that makes a menu bar feel native.
    if (openIndex_ < 0) return;
    const std::vector<MenuBarItem*> items = Items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (TitleBounds(i).Contains(local)) {
            if (openIndex_ != static_cast<int>(i)) OpenMenu(i);
            return;
        }
    }
}

void MenuBar::OpenMenu(size_t index) {
    const std::vector<MenuBarItem*> items = Items();
    if (index >= items.size()) return;
    // Rebuilding on every open is what keeps enabled state honest: a menu built
    // once at load would show "Save" clickable with nothing to save.
    std::vector<PopupMenuItem> model = items[index]->BuildItems();
    RefreshMenuEnabled(model);
    if (model.empty()) return;

    const Rect title = TitleBounds(index);
    auto layer = MakeMenuLayer(std::move(model), Point{title.x, title.y + title.h},
                               [this] { CloseMenu(); });
    SetMenuHoverOnOpen(layer.get(), true);
    // Release the previous layer only NOW, at a safe point: the previous close
    // retired it precisely so this could not happen while its stack frame lived.
    retiredLayer_.reset();
    ownedLayer_ = std::move(layer);
    layer_ = ownedLayer_.get();
    openIndex_ = static_cast<int>(index);
    SetOverlay(layer_);
    Invalidate(AffectsRender);
}

void MenuBar::RetireLayer() {
    if (!ownedLayer_) return;
    // Do not free it here: CloseMenu may be running inside this very object's
    // OnPointerDown. Handing ownership to retiredLayer_ keeps the object alive until
    // the next open (or destruction), by which time no frame of ours is on the stack.
    retiredLayer_ = std::move(ownedLayer_);
    layer_ = nullptr;
}

void MenuBar::CloseMenu() {
    if (openIndex_ < 0 && !layer_) return;
    openIndex_ = -1;
    // Clear the root's overlay BEFORE retiring the layer: a repaint in between would
    // otherwise call into a layer that is on its way out.
    if (layer_) {
        if (Widget* root = Root()) {
            if (root->Overlay() == layer_) root->SetOverlay(nullptr);
        }
        RetireLayer();
    }
    Invalidate(AffectsRender);
}

// ---------------------------------------------------------------------------
// MenuBarItem / MenuFlyout / item leaves
// ---------------------------------------------------------------------------
Size MenuBarItem::Measure(const Size& available) {
    (void)available;
    desired_ = Size{0, 0};
    return desired_;
}

Size MenuFlyout::Measure(const Size& available) {
    (void)available;
    desired_ = Size{0, 0};
    return desired_;
}

std::vector<PopupMenuItem> MenuFlyout::BuildItems() {
    std::vector<PopupMenuItem> out;
    for (const auto& child : children_) {
        if (auto* item = dynamic_cast<MenuFlyoutItem*>(child.get())) {
            out.push_back(item->Build());
        } else if (auto* sub = dynamic_cast<MenuFlyoutSubItem*>(child.get())) {
            out.push_back(sub->Build());
        } else if (dynamic_cast<MenuFlyoutSeparator*>(child.get())) {
            PopupMenuItem sep;
            sep.kind = PopupMenuItem::Kind::Separator;
            out.push_back(std::move(sep));
        }
    }
    return out;
}

std::vector<PopupMenuItem> MenuBarItemImpl::BuildItems() {
    for (const auto& child : children_) {
        if (auto* flyout = dynamic_cast<MenuFlyout*>(child.get())) return flyout->BuildItems();
    }
    return {};
}

Size MenuFlyoutItem::Measure(const Size& available) {
    (void)available;
    desired_ = Size{0, 0};
    return desired_;
}

PopupMenuItem MenuFlyoutItem::Build() {
    PopupMenuItem item;
    item.text = Get<std::wstring>(SlotOf("Text"));
    item.hint = Get<std::wstring>(SlotOf("Hint"));
    item.icon = WideToUtf8(Get<std::wstring>(SlotOf("Icon")));
    item.command = Get<RefPtr>(SlotOf("Command"));
    item.checked = Get<bool>(SlotOf("IsChecked"));
    item.kind = Get<int>(SlotOf("Kind")) == 1 ? PopupMenuItem::Kind::Toggle
                                              : PopupMenuItem::Kind::Command;
    return item;
}

Size MenuFlyoutSeparator::Measure(const Size& available) {
    (void)available;
    desired_ = Size{0, 0};
    return desired_;
}

Size MenuFlyoutSubItem::Measure(const Size& available) {
    (void)available;
    desired_ = Size{0, 0};
    return desired_;
}

PopupMenuItem MenuFlyoutSubItem::Build() {
    PopupMenuItem item;
    item.kind = PopupMenuItem::Kind::Submenu;
    item.text = Get<std::wstring>(SlotOf("Text"));
    for (const auto& child : children_) {
        if (auto* flyout = dynamic_cast<MenuFlyout*>(child.get())) {
            item.children = flyout->BuildItems();
            break;
        }
    }
    return item;
}

// ---------------------------------------------------------------------------
// AppBarButton
// ---------------------------------------------------------------------------
namespace {
constexpr float kAppBarButtonHeight = 32.0f;
constexpr float kAppBarIconOnly = 38.0f;
constexpr float kAppBarSeparatorWidth = 13.0f;
} // namespace

bool AppBarButton::IsSeparator() const {
    const int slot = SlotOf("IsSeparator");
    return slot >= 0 && Get<bool>(slot);
}

float AppBarButton::ToolbarWidth() const {
    if (IsSeparator()) return kAppBarSeparatorWidth;
    const std::wstring label = Get<std::wstring>(SlotOf("Label"));
    if (label.empty()) return kAppBarIconOnly;
    // The icon gutter (8 for inset + 16 for the glyph + 2 breathing room) plus the
    // label plus the same inset on the right. Computing this from the same numbers
    // OnPaint uses is what keeps a label from wrapping inside its own button: the
    // first version reserved only 16 px for the gutter while the paint path started
    // the label at 26, so "Reopen" wrapped to "Reo/pen".
    constexpr float kIconGutter = 26.0f; // 8 inset + 16 glyph + 2 gap
    constexpr float kTrailing = 8.0f;
    float labelW = static_cast<float>(label.size()) * 12.0f * 0.55f;
    if (ITextService* svc = TextSystem::Get()) labelW = svc->Measure(label, 12.0f, WeightNormal).w;
    return std::max(kAppBarIconOnly, kIconGutter + labelW + kTrailing);
}

Size AppBarButton::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    desired_ = Size{ToolbarWidth() + margin.H(), kAppBarButtonHeight + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void AppBarButton::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    if (IsSeparator()) {
        const float cx = bounds_.x + bounds_.w * 0.5f;
        const Color line = palette.colors[static_cast<int>(Token::CardBorder)];
        ctx.FillRect(Rect{cx, bounds_.y + 4.0f, 1.0f, std::max(0.0f, bounds_.h - 8.0f)}, line);
        return;
    }
    const bool enabled = Get<bool>(SlotOf("IsEnabled"));
    if (enabled && (hovered_ || pressed_)) {
        ctx.FillRoundRect(Rect{bounds_.x, bounds_.y, bounds_.w, bounds_.h}, 4.0f,
                          palette.colors[static_cast<int>(
                              pressed_ ? Token::ControlFillPressed : Token::ControlFillHover)]);
    }
    const Color fg = enabled ? palette.colors[static_cast<int>(Token::TextPrimary)]
                             : palette.colors[static_cast<int>(Token::TextDisabled)];

    const std::wstring icon = Get<std::wstring>(SlotOf("Icon"));
    const std::wstring label = Get<std::wstring>(SlotOf("Label"));
    if (label.empty()) {
        if (!icon.empty()) {
            ctx.DrawText(icon, bounds_, fg, 15.0f, WeightNormal, TextAlign::Center);
        }
        return;
    }
    // Icon and label share the row; the label is small so the toolbar stays a bar
    // rather than growing into a second menu.
    ctx.DrawText(icon, Rect{bounds_.x + 8.0f, bounds_.y, 16.0f, bounds_.h}, fg, 14.0f,
                 WeightNormal, TextAlign::Center);
    ctx.DrawText(label, Rect{bounds_.x + 26.0f, bounds_.y,
                             std::max(0.0f, bounds_.w - 30.0f), bounds_.h},
                 fg, 12.0f, WeightNormal, TextAlign::Left);
}

Widget* AppBarButton::HitTest(const Point& p) {
    if (IsSeparator()) return nullptr;     // cannot be clicked
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr; // disabled = not a hit
    return bounds_.Contains(p) ? this : nullptr;
}

void AppBarButton::OnPointerDown(const Point& local) { (void)local; }

void AppBarButton::OnPointerUp(const Point& local) {
    if (bounds_.Contains(local)) Invoke();
}

void AppBarButton::Invoke() {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    auto command = Get<RefPtr>(SlotOf("Command"));
    if (!command) return;
    if (auto* cmd = dynamic_cast<ICommand*>(command.get())) {
        if (cmd->CanExecute()) cmd->Execute();
    }
}

bool AppBarButton::OnKeyDown(int vk) {
    if (vk == KeySpace || vk == KeyReturn) {
        Invoke();
        return true;
    }
    return Widget::OnKeyDown(vk);
}

PopupMenuItem AppBarButton::BuildItem() {
    PopupMenuItem item;
    item.kind = PopupMenuItem::Kind::Command;
    // The overflow menu shows the text label; an icon-only button would be
    // anonymous there, so fall back to the tooltip.
    item.text = Get<std::wstring>(SlotOf("Label"));
    if (item.text.empty()) item.text = Get<std::wstring>(SlotOf("ToolTip"));
    item.icon = WideToUtf8(Get<std::wstring>(SlotOf("Icon")));
    item.command = Get<RefPtr>(SlotOf("Command"));
    item.enabled = Get<bool>(SlotOf("IsEnabled"));
    return item;
}

// ---------------------------------------------------------------------------
// CommandBar
// ---------------------------------------------------------------------------
CommandBar::CommandBar() {
    BindType(TypeRegistry::Instance().Find("wui/CommandBar"));
}

CommandBar::~CommandBar() {
    if (Widget* root = Root()) {
        if (ownedLayer_ && root->Overlay() == ownedLayer_.get()) root->SetOverlay(nullptr);
    }
}

std::vector<WidgetPtr> CommandBar::Buttons() const {
    std::vector<WidgetPtr> out;
    for (const auto& c : children_) {
        if (dynamic_cast<AppBarButton*>(c.get())) out.push_back(c);
    }
    return out;
}

float CommandBar::ButtonWidth(const Widget& button) const {
    if (auto* bar = dynamic_cast<const AppBarButton*>(&button)) return bar->ToolbarWidth();
    return 0.0f;
}

void CommandBar::UpdateLayoutOfButtons() {
    const std::vector<WidgetPtr> buttons = Buttons();
    widths_.resize(buttons.size());
    naturalWidth_ = 0.0f;
    for (size_t i = 0; i < buttons.size(); ++i) {
        widths_[i] = buttons[i]->DesiredSize().w > 0 ? buttons[i]->DesiredSize().w
                                                     : ButtonWidth(*buttons[i]);
        naturalWidth_ += widths_[i];
    }
}

Size CommandBar::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float explicitH = Get<float>(SlotOf("Height"));
    const float h = explicitH > 0 ? explicitH : 44.0f;
    const float availW = std::max(0.0f, available.w - margin.H() - pad.H());

    const std::vector<WidgetPtr> buttons = Buttons();
    for (const auto& b : buttons) b->Measure(Size{kAppBarIconOnly, h});
    UpdateLayoutOfButtons();

    // Overflow: reserve room for the "..." button and collapse trailing buttons
    // until the rest fits. Reserving first is what stops a bar that barely
    // overflows from immediately overflowing again by one button.
    const float overflowW = kAppBarIconOnly;
    const bool needsOverflow = naturalWidth_ > availW;
    const float budget = needsOverflow ? std::max(0.0f, availW - overflowW) : availW;
    overflow_ = 0;
    float used = 0.0f;
    for (size_t i = 0; i < widths_.size(); ++i) {
        if (used + widths_[i] > budget) {
            overflow_ = static_cast<int>(widths_.size() - i);
            break;
        }
        used += widths_[i];
    }

    desired_ = Size{available.w > 0 ? available.w : naturalWidth_ + pad.H() + margin.H(),
                    h + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void CommandBar::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float h = std::max(0.0f, finalRect.h - margin.V() - pad.V());
    float x = finalRect.x + margin.l + pad.l;
    const float y = finalRect.y + margin.t + pad.t;
    const size_t visible = widths_.size() - static_cast<size_t>(std::max(0, overflow_));
    for (size_t i = 0; i < children_.size(); ++i) {
        if (i >= visible) {
            // Hidden behind the overflow button: arranged to nothing so it cannot
            // steal a click or paint over the bar.
            children_[i]->Arrange(Rect{0, 0, 0, 0});
            continue;
        }
        const float w = i < widths_.size() ? widths_[i] : 0.0f;
        children_[i]->Arrange(Rect{x, y, w, h});
        x += w;
    }
}

Rect CommandBar::ButtonBounds(size_t index) const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    float x = bounds_.x + margin.l + pad.l;
    const float y = bounds_.y + margin.t + pad.t;
    const float h = std::max(0.0f, bounds_.h - margin.V() - pad.V());
    for (size_t i = 0; i < index && i < widths_.size(); ++i) x += widths_[i];
    return Rect{x, y, index < widths_.size() ? widths_[index] : 0.0f, h};
}

Rect CommandBar::OverflowBounds() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float h = std::max(0.0f, bounds_.h - margin.V() - pad.V());
    const float right = bounds_.x + margin.l + bounds_.w - margin.H() - pad.r;
    return Rect{right - kAppBarIconOnly, bounds_.y + margin.t + pad.t, kAppBarIconOnly, h};
}

void CommandBar::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
    const Color line = palette.colors[static_cast<int>(Token::CardBorder)];
    ctx.FillRect(Rect{bounds_.x, bounds_.y + bounds_.h - 1.0f, bounds_.w, 1.0f},
                 Color{line.r, line.g, line.b, 0.6f});

    // Visible buttons are painted by the tree; only the overflow affordance is
    // drawn here.
    if (overflow_ > 0) {
        const Rect box = OverflowBounds();
        ctx.FillRoundRect(box, 4.0f, palette.colors[static_cast<int>(Token::ControlFill)]);
        ctx.DrawText(L"\u22ef", box, palette.colors[static_cast<int>(Token::TextPrimary)],
                     16.0f, WeightNormal, TextAlign::Center);
    }
}

Widget* CommandBar::HitTest(const Point& p) {
    if (!bounds_.Contains(p)) return nullptr;
    if (overflow_ > 0 && OverflowBounds().Contains(p)) return this;
    // Fall through to the visible buttons so their own hit rules (disabled,
    // separator) apply instead of being overridden here.
    const size_t visible = widths_.size() - static_cast<size_t>(std::max(0, overflow_));
    for (size_t i = 0; i < visible && i < children_.size(); ++i) {
        if (Widget* hit = children_[i]->HitTest(p)) return hit;
    }
    return this; // the bar itself, so clicks on padding do not reach the page
}

void CommandBar::OnPointerDown(const Point& local) {
    if (overflow_ > 0 && OverflowBounds().Contains(local)) OpenOverflow();
}

void CommandBar::OnPointerUp(const Point&) {}

void CommandBar::OnPointerMove(const Point&) {}

void CommandBar::OpenOverflow() {
    if (overflow_ <= 0) return;
    const size_t visible = widths_.size() - static_cast<size_t>(overflow_);
    std::vector<PopupMenuItem> model;
    for (size_t i = visible; i < children_.size(); ++i) {
        if (auto* button = dynamic_cast<AppBarButton*>(children_[i].get())) {
            PopupMenuItem item = button->BuildItem();
            item.enabled = Get<bool>(SlotOf("IsEnabled"));
            model.push_back(std::move(item));
        }
    }
    if (model.empty()) return;
    RefreshMenuEnabled(model);

    const Rect box = OverflowBounds();
    auto layer = MakeMenuLayer(std::move(model), Point{box.x, box.y + box.h}, [this] {
        if (Widget* root = Root()) {
            if (ownedLayer_ && root->Overlay() == ownedLayer_.get()) root->SetOverlay(nullptr);
        }
        // Same hazard as the menu bar: this callback runs inside the layer's own
        // OnPointerDown, so the layer must not be freed from here. Retiring it hands
        // ownership to retiredLayer_ until the next open.
        retiredLayer_ = std::move(ownedLayer_);
        Invalidate(AffectsRender);
    });
    retiredLayer_.reset();
    ownedLayer_ = std::move(layer);
    SetOverlay(ownedLayer_.get());
    Invalidate(AffectsRender);
}

// ---------------------------------------------------------------------------
// ContentDialog
// ---------------------------------------------------------------------------
namespace {
// Result codes published to the ViewModel. Deliberately small numbers so a
// sample can map them onto its own enum without an out-of-band table.
constexpr int kDialogPrimary = 1;
constexpr int kDialogSecondary = 2;
constexpr int kDialogClose = 0;
} // namespace

ContentDialog::ContentDialog() {
    BindType(TypeRegistry::Instance().Find("wui/ContentDialog"));
    slotIsOpen_ = SlotOf("IsOpen");
    slotResult_ = SlotOf("Result");
    slotPrimary_ = SlotOf("PrimaryButtonText");
    slotSecondary_ = SlotOf("SecondaryButtonText");
}

ContentDialog::~ContentDialog() {
    if (Widget* root = Root()) {
        if (ownedLayer_ && root->Overlay() == ownedLayer_.get()) root->SetOverlay(nullptr);
    }
}

Size ContentDialog::Measure(const Size& available) {
    (void)available;
    // A dialog is not in the flow: it reserves no space where it is declared.
    desired_ = Size{0, 0};
    return desired_;
}

void ContentDialog::OnSlotChanged(int slot, const Value& value) {
    if (slot == slotIsOpen_) {
        const bool want = value.type == VType::Bool ? value.flag : false;
        if (want && !open_) Show();
        else if (!want && open_) Close(ResultNone);
    }
}

void ContentDialog::Show() {
    if (open_) return;
    std::vector<DialogButton> buttons;
    const std::wstring primary = Get<std::wstring>(slotPrimary_);
    DialogButton p;
    p.text = primary.empty() ? L"OK" : primary;
    p.result = kDialogPrimary;
    p.isDefault = true;
    buttons.push_back(p);

    if (int secondarySlot = SlotOf("SecondaryButtonText"); secondarySlot >= 0) {
        const std::wstring secondary = Get<std::wstring>(secondarySlot);
        if (!secondary.empty()) {
            DialogButton s;
            s.text = secondary;
            s.result = kDialogSecondary;
            buttons.push_back(s);
        }
    }
    if (int closeSlot = SlotOf("CloseButtonText"); closeSlot >= 0) {
        const std::wstring close = Get<std::wstring>(closeSlot);
        if (!close.empty()) {
            DialogButton c;
            c.text = close;
            c.result = kDialogClose;
            c.isCancel = true;
            buttons.push_back(c);
        }
    }

    open_ = true;
    // Close() is invoked from the layer's own button handler, so the layer must
    // survive the call: retire it (see MenuBar::RetireLayer) rather than freeing it,
    // and release it on the next Show() or on destruction.
    retiredLayer_.reset();
    ownedLayer_ = MakeDialogLayer(Get<std::wstring>(SlotOf("Title")),
                                  Get<std::wstring>(SlotOf("Message")), std::move(buttons),
                                  [this](int result) { Close(result); },
                                  [this] { open_ = false; });
    SetOverlay(ownedLayer_.get());
    Invalidate(AffectsRender);
}

void ContentDialog::Close(int result) {
    if (!open_ && result == ResultNone) return;
    open_ = false;
    result_ = result;
    if (slotResult_ >= 0) SetSlotValue(slotResult_, Value::Number(result), true);
    if (slotIsOpen_ >= 0) SetSlotValue(slotIsOpen_, Value::Bool(false), true);
    if (ownedLayer_) {
        if (Widget* root = Root()) {
            if (root->Overlay() == ownedLayer_.get()) root->SetOverlay(nullptr);
        }
        // Not reset(): we are inside this layer's OnPointerDown. Keep it alive until
        // the next Show(), which is guaranteed not to be inside its stack frame.
        retiredLayer_ = std::move(ownedLayer_);
    }
    Invalidate(AffectsRender);
}
} // namespace wui
