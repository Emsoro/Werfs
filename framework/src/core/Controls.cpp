// SPDX-License-Identifier: MIT
#include "Controls.h"

#include "Animation.h" // Ease: the dots' wrap hop uses the same curve as transitions.
#include "Image.h" // IImageService: the control asks for a decoder, it never decodes.
#include "Overlay.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

#include <algorithm>
#include <cmath>

namespace wui {

// ---------------------------------------------------------------------------
// TextBlock
// ---------------------------------------------------------------------------

// Greedy word wrap. Splits on spaces, keeps words whole, and honours explicit
// newlines. Falls back to per-character breaking for a single word that is
// wider than the line, so a long path never overflows.
static std::vector<std::wstring> WrapText(ITextService* svc, const std::wstring& text, float fontSize,
                                          int weight, float maxWidth,
                                          const std::wstring& family = L"") {
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
        if (current.empty() || svc->Measure(candidate, fontSize, weight, family).w <= maxWidth) {
            current = candidate;
        } else {
            lines.push_back(current);
            current = word;
        }
        // A single word wider than the line: break it by character.
        if (!current.empty() && svc->Measure(current, fontSize, weight, family).w > maxWidth) {
            std::wstring piece;
            for (wchar_t ch : current) {
                const std::wstring test = piece + ch;
                if (!piece.empty() && svc->Measure(test, fontSize, weight, family).w > maxWidth) {
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
    // The wrapped lines must be measured with the same family that will draw them;
    // measuring with the default and drawing with Consolas breaks mid-word.
    const std::wstring family = FontFamily();
    const Thickness pad = slotPad >= 0 ? Get<Thickness>(slotPad) : Thickness{};
    const Thickness margin = slotMargin >= 0 ? Get<Thickness>(slotMargin) : Thickness{};
    const bool wrap = Get<int>(SlotOf("TextWrapping")) == 1;

    ITextService* svc = TextSystem::Get();
    // Unwrapped text ignores the constraint: it sizes to its content and the
    // parent decides whether to clip it. Wrapped text needs the width, so the
    // available space is what bounds it.
    const float maxWidth = std::max(0.0f, available.w - pad.H() - margin.H());

    if (wrap && maxWidth > 0 && !text.empty()) {
        wrappedLines_ = WrapText(svc, text, fontSize, weight, maxWidth, family);
        float w = 0;
        for (const auto& line : wrappedLines_) {
            const float lineW = svc ? svc->Measure(line, fontSize, weight, family).w
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
        if (!text.empty()) t = svc->Measure(text, fontSize, weight, family);
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
    const std::wstring family = FontFamily();
    if (wrappedLines_.size() > 1) {
        ITextService* svc = TextSystem::Get();
        const float lineH = svc ? svc->LineHeight(fontSize, family) : fontSize * 1.33f;
        float y = box.y;
        for (const auto& line : wrappedLines_) {
            ctx.DrawText(line, Rect{box.x, y, box.w, lineH}, fg, fontSize, weight, align, family);
            y += lineH;
        }
        return;
    }
    ctx.DrawText(text, box, fg, fontSize, weight, align, family);
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
    // Card shadow: concentric translucent round rects behind the background.
    // Kept in the widget layer so all PaintCtx backends work unchanged.
    const float shadowRadius = Get<float>(SlotOf("ShadowRadius"));
    const Color shadow = ResolveColor(Get<Color>(SlotOf("ShadowColor")), palette);
    if (shadowRadius > 0 && shadow.a > 0) {
        const float sx = Get<float>(SlotOf("ShadowOffsetX"));
        const float sy = Get<float>(SlotOf("ShadowOffsetY"));
        for (int i = 5; i >= 1; --i) {
            const float spread = shadowRadius * static_cast<float>(i) / 5.0f;
            Color layer = shadow;
            layer.a = shadow.a * (0.10f + 0.06f * static_cast<float>(5 - i));
            ctx.FillRoundRect(Rect{box.x + sx - spread, box.y + sy - spread,
                                   box.w + 2 * spread, box.h + 2 * spread},
                              radius + spread, layer);
        }
    }
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
        const Size want = c->DesiredSize();
        // Alignment is applied by the PARENT, because only the parent knows how much room
        // is left over. Stretch (0) keeps the old behaviour exactly, so adding this did
        // not move anything that was already laid out.
        const int hAlign = c->SlotOf("HorizontalAlignment") >= 0
                               ? c->Get<int>(c->SlotOf("HorizontalAlignment"))
                               : 0;
        const int vAlign = c->SlotOf("VerticalAlignment") >= 0
                               ? c->Get<int>(c->SlotOf("VerticalAlignment"))
                               : 0;
        auto alignX = [&](float slotW, float childW) {
            // Left already means "at the slot's start"; Center and Right shift.
            if (hAlign == 2) return (slotW - childW) * 0.5f;
            if (hAlign == 3) return slotW - childW;
            return 0.0f;
        };
        auto alignY = [&](float slotH, float childH) {
            if (vAlign == 2) return (slotH - childH) * 0.5f;
            if (vAlign == 3) return slotH - childH;
            return 0.0f;
        };
        if (orientation == 0) {
            // Cross axis is horizontal here: the child gets its natural width unless
            // stretched.
            const float childW = hAlign == 0 ? content.w : want.w;
            const float dx = alignX(content.w, childW);
            c->Arrange(Rect{content.x + dx, cursor, childW, want.h});
            cursor += want.h;
        } else {
            // Cross axis is vertical.
            const float childH = vAlign == 0 ? content.h : want.h;
            const float dy = alignY(content.h, childH);
            c->Arrange(Rect{cursor, content.y + dy, want.w, childH});
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
    const float radius = Get<float>(SlotOf("CornerRadius"));
    ctx.FillRoundRect(box, radius, bg);
    const Color border = ResolveColor(Get<Color>(SlotOf("BorderBrush")), palette);
    const Thickness borderThickness = Get<Thickness>(SlotOf("BorderThickness"));
    const float borderWidth = std::max({borderThickness.l, borderThickness.t,
                                        borderThickness.r, borderThickness.b});
    if (border.a > 0 && borderWidth > 0) ctx.StrokeRect(box, border, borderWidth, radius);
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

void Button::OnPointerDown(const Point&) { SetPressed(true); Invalidate(AffectsRender); }

void Button::OnPointerUp(const Point& local) {
    const bool wasPressed = pressed_;
    SetPressed(false);
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
    // Background is painted so a style or trigger can tint the whole row. Without
    // it, "highlight the checked item" -- the most obvious use of a trigger -- has
    // nowhere to draw, and the property would have to be faked with a Border.
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0.0f) ctx.FillRoundRect(box, 4.0f, bg);
    const float size = 18.0f;
    const Rect glyph{box.x + 3, box.y + (box.h - size) * 0.5f, size, size};
    const bool checked = Get<bool>(SlotOf("IsChecked"));
    const Color checkColor = ResolveColor(Get<Color>(SlotOf("CheckColor")), palette);
    ctx.FillRoundRect(glyph, 3.0f, checked ? checkColor
                                           : palette.colors[static_cast<int>(Token::ControlFill)]);
    if (!checked) ctx.StrokeRect(glyph, palette.colors[static_cast<int>(Token::TextSecondary)], 1.0f, 3.0f);
    if (checked) {
        // Keep the mark legible on both dark and light custom check colours.
        const float luminance = checkColor.r * 0.299f + checkColor.g * 0.587f +
                                checkColor.b * 0.114f;
        const Color mark = luminance > 0.6f ? Color{0, 0, 0, 1, Token::None}
                                            : Color{1, 1, 1, 1, Token::None};
        ctx.DrawText(L"\u2713", Rect{glyph.x, glyph.y, glyph.w, glyph.h},
                     mark, size * 0.8f, WeightBold, TextAlign::Center);
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
namespace {
// The scrollbar is painted inside the text area rather than in a separate
// widget: a TextBox owns its own scroll state, and a nested ScrollViewer would
// fight it for the wheel and for the layout height.
constexpr float kTextScrollbarWidth = 10.0f;
constexpr float kTextThumbMinHeight = 16.0f;
} // namespace

Size TextBox::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    std::wstring text = Get<std::wstring>(SlotOf("Text"));
    if (text.empty()) text = Get<std::wstring>(SlotOf("PlaceholderText"));
    Size t{static_cast<float>(text.size()) * fontSize * 0.55f, fontSize * 1.33f};
    if (ITextService* svc = TextSystem::Get()) t = svc->Measure(text, fontSize, WeightNormal);
    // A text box always needs at least ONE visible line. The natural height below
    // can be many lines for a document, but the floor is one line + padding.
    const float oneLine = std::max(30.0f, fontSize * 1.33f + pad.V() + margin.V());
    desired_ = Size{std::max(160.0f, t.w + pad.H() + margin.H()),
                    std::max(oneLine, t.h + pad.V() + margin.V())};
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
        // Clamp a many-line document to what the parent offers, but NEVER below
        // one line: a tight StackPanel used to squeeze a single-line box to 23 px
        // and clip its placeholder/typed text, which looked like an empty field.
        if (explicitH <= 0) desired_.h = std::max(oneLine, std::min(desired_.h, available.h));
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
    // DisplayText(), not Text: this is the ONLY place the paint path reads the content,
    // so routing it through the virtual is what makes PasswordBox's mask apply to the
    // whole drawing path instead of just the placeholder branch.
    return BuildDisplayLines(DisplayText(), TextSystem::Get(), wrapWidth, bounds_.h, fontSize,
                             weight, Wrapping());
}

std::vector<std::wstring> TextBox::BuildDisplayLines(const std::wstring& text, ITextService* svc,
                                                    float wrapWidth, float boxHeight,
                                                    float fontSize, int weight, bool wrap) {
    std::vector<std::wstring> lines;
    const float lineH = svc ? svc->LineHeight(fontSize) : fontSize * 1.33f;
    if (!wrap) {
        // NoWrap still honours the lines the author typed. Only SOFT wrapping is
        // disabled: returning one giant line with '\n' inside it would paint the
        // newline as a glyph and break caret/IME row maths.
        size_t start = 0;
        while (start <= text.size()) {
            const size_t nl = text.find(L'\n', start);
            lines.push_back(nl == std::wstring::npos ? text.substr(start)
                                                     : text.substr(start, nl - start));
            if (nl == std::wstring::npos) break;
            start = nl + 1;
        }
        return lines;
    }
    // A box only one line tall has no room to wrap into; wrapping there would
    // change what a single-line field looks like.
    const bool canWrap = boxHeight >= lineH * 1.9f && wrapWidth > 0.0f;
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

std::wstring TextBox::DisplayText() const {
    // The composition is spliced in HERE rather than written to the slot, so every paint
    // path shows it while the value stays untouched. That is the whole reason it is a
    // display concern: a preview that reached the slot would fire the binding and the
    // ViewModel on each keystroke of a word that does not exist yet.
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    if (composition_.empty()) return text;
    // caret_ counts the preview, so the preview begins BEFORE the caret: [at, caret_).
    // Using caret_ itself as the insertion point put a composition after the wrong
    // character whenever the caret was in the middle of the text (or on a later
    // line), and the commit then inserted somewhere else entirely.
    const size_t at = std::min(caret_ > composition_.size() ? caret_ - composition_.size() : 0,
                               text.size());
    return text.substr(0, at) + composition_ + text.substr(at);
}

bool TextBox::Wrapping() const {
    const int slot = SlotOf("TextWrapping");
    return slot < 0 || Get<int>(slot) == 1; // 0 = NoWrap, 1 = Wrap
}

float TextBox::LineHeight() const {
    ITextService* svc = TextSystem::Get();
    const float fontSize = Get<float>(SlotOf("FontSize"));
    return svc ? svc->LineHeight(fontSize) : fontSize * 1.33f;
}

float TextBox::MaxScroll() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float h = bounds_.h - margin.V() - pad.V();
    if (h <= 0.0f) return 0.0f;
    const float wrapWidth = lastWrapWidth_ > 0.0f ? lastWrapWidth_
                                                  : std::max(0.0f, bounds_.w - margin.H() - pad.H());
    const size_t rows = BuildDisplayLines(wrapWidth, Get<float>(SlotOf("FontSize")), WeightNormal)
                            .size();
    return std::max(0.0f, LineHeight() * static_cast<float>(rows) - h);
}

Rect TextBox::ScrollbarTrack(const Rect& textBox) const {
    return Rect{textBox.x + textBox.w - kTextScrollbarWidth, textBox.y, kTextScrollbarWidth,
                textBox.h};
}

void TextBox::SetScrollOffset(float value, bool publish) {
    const float clamped = std::clamp(value, 0.0f, MaxScroll());
    if (std::abs(clamped - scrollOffset_) < 0.01f) return;
    scrollOffset_ = clamped;
    if (publish) {
        const int slot = SlotOf("ScrollOffset");
        if (slot >= 0) SetSlotValue(slot, Value::Number(static_cast<double>(scrollOffset_)), true);
    }
    Invalidate(AffectsRender);
}

void TextBox::UpdateFollowFromScroll() {
    // Touching the scrollbar is an explicit statement of intent. Reaching the
    // bottom re-arms follow; any other position detaches it.
    followEnd_ = scrollOffset_ >= MaxScroll() - 0.5f;
}

void TextBox::OnSlotChanged(int slot, const Value& value) {
    Widget::OnSlotChanged(slot, value);
    if (slot == SlotOf("Text")) {
        pendingAutoScroll_ = true;
        Invalidate(AffectsMeasure | AffectsRender);
    } else if (slot == SlotOf("ScrollOffset")) {
        double v = 0;
        if (ValueCodec<double>::From(value, v)) {
            const float clamped = std::clamp(static_cast<float>(v), 0.0f, MaxScroll());
            if (std::abs(clamped - scrollOffset_) >= 0.01f) {
                scrollOffset_ = clamped;
                Invalidate(AffectsRender);
            }
            UpdateFollowFromScroll();
        }
    } else if (slot == SlotOf("AutoScroll")) {
        followEnd_ = Get<int>(slot) == 1;
        pendingAutoScroll_ = true;
        Invalidate(AffectsRender);
    } else if (slot == SlotOf("TextWrapping")) {
        Invalidate(AffectsMeasure | AffectsRender);
    }
}

bool TextBox::OnPointerWheel(float deltaLines) {
    const float maxScroll = MaxScroll();
    if (maxScroll <= 0.0f) return Widget::OnPointerWheel(deltaLines);
    // Same convention as ScrollViewer: negative deltaLines means "wheel down",
    // which moves the content UP, i.e. increases the scroll offset.
    SetScrollOffset(scrollOffset_ - deltaLines * LineHeight(), true);
    UpdateFollowFromScroll();
    return true; // consumed: the page under the log must not scroll as well
}

void TextBox::OnPointerDown(const Point& local) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect textBox{bounds_.x + margin.l + pad.l, bounds_.y + margin.t + pad.t,
                       std::max(0.0f, bounds_.w - margin.H() - pad.H()),
                       std::max(0.0f, bounds_.h - margin.V() - pad.V())};
    const float maxScroll = MaxScroll();
    if (maxScroll <= 0.0f) return;
    const Rect track = ScrollbarTrack(textBox);
    if (!track.Contains(local)) return;
    const float thumbH = std::max(kTextThumbMinHeight,
                                  track.h * track.h / (maxScroll + track.h));
    const float thumbY = track.y + (scrollOffset_ / maxScroll) * (track.h - thumbH);
    if (local.y < thumbY) {
        SetScrollOffset(scrollOffset_ - track.h, true);
        UpdateFollowFromScroll();
    } else if (local.y > thumbY + thumbH) {
        SetScrollOffset(scrollOffset_ + track.h, true);
        UpdateFollowFromScroll();
    } else {
        draggingThumb_ = true;
        thumbGrabDelta_ = local.y - thumbY;
    }
}

void TextBox::OnPointerMove(const Point& local) {
    if (!draggingThumb_) return;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect textBox{bounds_.x + margin.l + pad.l, bounds_.y + margin.t + pad.t,
                       std::max(0.0f, bounds_.w - margin.H() - pad.H()),
                       std::max(0.0f, bounds_.h - margin.V() - pad.V())};
    const Rect track = ScrollbarTrack(textBox);
    const float maxScroll = MaxScroll();
    if (maxScroll <= 0.0f || track.h <= kTextThumbMinHeight) return;
    const float thumbH = std::max(kTextThumbMinHeight,
                                  track.h * track.h / (maxScroll + track.h));
    const float y = local.y - thumbGrabDelta_ - track.y;
    const float ratio = std::clamp(y / (track.h - thumbH), 0.0f, 1.0f);
    SetScrollOffset(ratio * maxScroll, true);
    UpdateFollowFromScroll();
}

void TextBox::OnPointerUp(const Point&) { draggingThumb_ = false; }

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
    const std::wstring text = DisplayText();
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
    // Two passes: the scrollbar steals a few pixels, which can add one more
    // wrapped line, so decide from the full width and then re-wrap on the final
    // width. Without the second pass a long word disappears under the thumb.
    float wrapWidth = std::max(0.0f, textBox.w);
    std::vector<std::wstring> lines = BuildDisplayLines(wrapWidth, fontSize, weight);
    float contentH = lineH * static_cast<float>(lines.size());
    bool needBar = contentH > textBox.h + 0.5f;
    if (needBar) {
        wrapWidth = std::max(0.0f, textBox.w - kTextScrollbarWidth - 2.0f);
        lines = BuildDisplayLines(wrapWidth, fontSize, weight);
        contentH = lineH * static_cast<float>(lines.size());
        needBar = contentH > textBox.h + 0.5f;
    }
    lastWrapWidth_ = wrapWidth;
    const float maxScroll = std::max(0.0f, contentH - textBox.h);
    const float beforeClamp = scrollOffset_;
    scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);
    bool offsetChanged = std::abs(scrollOffset_ - beforeClamp) >= 0.01f;

    const int autoSlot = SlotOf("AutoScroll");
    const bool toEnd = autoSlot >= 0 && Get<int>(autoSlot) == 1;
    // Sticky follow, not a perpetual yank: a live caption stream changes Text
    // every few frames. Following unconditionally made the wheel look dead --
    // every incoming line pulled the view back to the bottom. Only follow while
    // the reader is still at the bottom; a wheel/drag away detaches, and coming
    // back to the bottom re-attaches.
    const bool shouldFollow = toEnd && followEnd_;
    if (shouldFollow && std::abs(scrollOffset_ - maxScroll) >= 0.01f) {
        scrollOffset_ = maxScroll;
        offsetChanged = true;
    }
    if (offsetChanged || pendingAutoScroll_) {
        const int slot = SlotOf("ScrollOffset");
        if (slot >= 0) SetSlotValue(slot, Value::Number(static_cast<double>(scrollOffset_)), true);
    }
    pendingAutoScroll_ = false;

    ctx.PushClip(textBox);
    float y = textBox.y - scrollOffset_;
    for (const auto& line : lines) {
        if (y + lineH <= textBox.y) { y += lineH; continue; }
        if (y >= textBox.y + textBox.h) break;
        ctx.DrawText(line, Rect{textBox.x, y, wrapWidth, lineH}, fg, fontSize, weight,
                     TextAlign::Left);
        y += lineH;
    }
    if (focused_) {
        // The caret uses the painter's own splitter, so soft wraps do not move it
        // to the wrong row. Keeping it inside the viewport is what makes typing
        // at the end of a long log visible without a separate command.
        Point cp = CaretPosition();
        if (cp.y < scrollOffset_) {
            scrollOffset_ = cp.y;
        } else if (cp.y + lineH > scrollOffset_ + textBox.h) {
            scrollOffset_ = cp.y + lineH - textBox.h;
        }
        scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);
        const int slot = SlotOf("ScrollOffset");
        if (slot >= 0) SetSlotValue(slot, Value::Number(static_cast<double>(scrollOffset_)), true);
        ctx.FillRect(Rect{textBox.x + cp.x, textBox.y + cp.y - scrollOffset_, 1.5f, lineH},
                     palette.colors[static_cast<int>(Token::TextPrimary)]);
    }
    ctx.PopClip();

    if (needBar) {
        const Rect track = ScrollbarTrack(textBox);
        ctx.FillRect(track, palette.colors[static_cast<int>(Token::CardBorder)]);
        const float thumbH = std::max(kTextThumbMinHeight,
                                      track.h * track.h / (maxScroll + track.h));
        const float thumbY = track.y +
                             (maxScroll > 0 ? (scrollOffset_ / maxScroll) * (track.h - thumbH) : 0);
        ctx.FillRoundRect(Rect{track.x + 2.0f, thumbY, track.w - 4.0f, thumbH}, 3.0f,
                          palette.colors[static_cast<int>(Token::TextSecondary)]);
    }
}

Widget* TextBox::HitTest(const Point& p) { return bounds_.Contains(p) ? this : nullptr; }

void TextBox::SetCaretToEnd() {
    editing_ = Get<std::wstring>(SlotOf("Text"));
    caret_ = editing_.size();
}

bool TextBox::OnChar(wchar_t ch) {
    if (ch < 32 && ch != L'\t') return false;
    // Re-read from the slot for the same reason as OnKeyDown above.
    editing_ = Get<std::wstring>(SlotOf("Text"));
    if (caret_ > editing_.size()) caret_ = editing_.size();
    editing_.insert(editing_.begin() + static_cast<long>(caret_), ch);
    ++caret_;
    SetSlotValue(SlotOf("Text"), Value::Text(editing_));
    return true;
}

bool TextBox::OnKeyDown(int vk) {
    // Re-read from the SLOT on every key, not only when the buffer is empty. "Empty" conflates
    // "never initialised" with "stale", and a value written from outside (a binding, a reset)
    // is invisible to a buffer refreshed only in the first case -- the same class of bug as
    // CODE_PLAN §7.27. The slot is the single source of truth here.
    editing_ = Get<std::wstring>(SlotOf("Text"));
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
    case KeyReturn: {
        const int slotReturn = SlotOf("AcceptsReturn");
        if (slotReturn >= 0 && Get<bool>(slotReturn)) {
            // WM_CHAR filters out control characters, so Enter is the only way
            // a document editor can create a new line. Caret stays after it.
            editing_.insert(caret_, L"\n");
            ++caret_;
            SetSlotValue(SlotOf("Text"), Value::Text(editing_));
            Invalidate(AffectsMeasure | AffectsRender);
            return true;
        }
        SetFocused(false);
        return true;
    }
    default:
        return false;
    }
}

void TextBox::CommitText() { SetSlotValue(SlotOf("Text"), Value::Text(editing_)); }

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// IME composition
//
// A composition is the text an IME is still building ("zhong" on the way to 中). It is a
// PREVIEW: spliced into the displayed text, but never written to the Text slot, so bindings
// and the ViewModel only ever see committed characters.
// ---------------------------------------------------------------------------
void TextBox::RepaintWithPreview() {
    // The preview changes the DISPLAYED text, so it can change the measured width (and, in
    // a wrapping box, the line count). Invalidate measure too: repainting without
    // re-measuring leaves a single-line box sized for the old string.
    Invalidate(AffectsMeasure | AffectsRender);
}

Point TextBox::CaretPosition() const {
    // Lay out the text UP TO the caret with the same splitter and width the painter
    // uses for the whole field. The last display line is the caret's line: its width
    // is the caret's column and its index is the row. Counting only '\n' (the old
    // ImeCaret rule) lost soft-wrapped lines, so the candidate list and the bar
    // drifted away from the text in a wrapped multi-line box.
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    ITextService* svc = TextSystem::Get();
    const float lineH = svc ? svc->LineHeight(fontSize) : fontSize * 1.33f;
    const float wrapWidth = std::max(0.0f, bounds_.w - margin.H() - pad.H());

    const std::wstring display = DisplayText();
    const size_t at = std::min(caret_, display.size());
    const std::vector<std::wstring> lines =
        BuildDisplayLines(display.substr(0, at), svc, lastWrapWidth_ > 0.0f ? lastWrapWidth_
                                                                           : wrapWidth,
                          bounds_.h, fontSize, WeightNormal, Wrapping());
    Point p{0.0f, lineH * static_cast<float>(lines.empty() ? 0 : lines.size() - 1)};
    if (!lines.empty() && !lines.back().empty()) {
        p.x = svc ? svc->Measure(lines.back(), fontSize, WeightNormal).w
                  : static_cast<float>(lines.back().size()) * fontSize * 0.55f;
    }
    return p;
}

CompositionCaret TextBox::ImeCaret() const {
    // The candidate window must sit at the CARET, not at the field's corner, and it must
    // reuse the painter's own padding, splitter and line height -- computing the position a
    // second, slightly different way is how a candidate window ends up one line too high.
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const float lineH =
        TextSystem::Get() ? TextSystem::Get()->LineHeight(fontSize) : fontSize * 1.33f;

    CompositionCaret caret;
    caret.lineHeight = lineH;
    caret.x = bounds_.x + margin.l + pad.l;
    caret.y = bounds_.y + margin.t + pad.t;
    const Point cp = CaretPosition();
    caret.x += cp.x;
    caret.y += cp.y;
    return caret;
}

void TextBox::SetComposition(const std::wstring& text) {
    if (composition_ == text) return;
    // caret_ is the index AFTER the preview, so the insertion point is caret_ minus
    // the old preview. Recomputing it from the slot instead of adding a delta also
    // repairs a stale caret left behind when a binding replaced the text (a tab
    // switch in the editor): an out-of-range caret used to throw from OnPaint.
    const std::wstring base = Get<std::wstring>(SlotOf("Text"));
    const size_t oldLength = composition_.size();
    size_t insertAt = caret_ > oldLength ? caret_ - oldLength : 0;
    insertAt = std::min(insertAt, base.size());
    composition_ = text;
    caret_ = insertAt + composition_.size();
    RepaintWithPreview();
}

void TextBox::CommitComposition(const std::wstring& finalText) {
    if (composition_.empty() && finalText.empty()) return;
    // Take the preview back out of the caret arithmetic, then insert the real
    // characters at that same point: display and commit must agree on WHERE the
    // word was being typed, including in the middle of a multi-line document.
    const size_t previewLength = composition_.size();
    composition_.clear();
    size_t insertAt = caret_ > previewLength ? caret_ - previewLength : 0;

    // Re-read from the SLOT rather than trusting editing_. A buffer refreshed only when
    // empty cannot see a value written from outside (a binding), so committing would
    // append the IME result to a stale string and replace the model's text with
    // something the user never had -- the same class of bug as CODE_PLAN §7.27.
    editing_ = Get<std::wstring>(SlotOf("Text"));
    insertAt = std::min(insertAt, editing_.size());
    if (!finalText.empty()) {
        editing_.insert(insertAt, finalText);
        SetSlotValue(SlotOf("Text"), Value::Text(editing_));
    }
    caret_ = insertAt + finalText.size();
    RepaintWithPreview();
}

void TextBox::CancelComposition() {
    if (composition_.empty()) return;
    // The value was never touched, so cancelling is just dropping the preview. This is the
    // payoff for keeping the preview out of the slot: there is nothing to undo.
    const size_t previewLength = composition_.size();
    caret_ = caret_ > previewLength ? caret_ - previewLength : 0;
    caret_ = std::min(caret_, Get<std::wstring>(SlotOf("Text")).size());
    composition_.clear();
    RepaintWithPreview();
}
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
    if (starW > 0 && availW != usedW) {
        // Both directions matter. Measure passes a provisional star size (auto
        // tracks are not yet known), so Arrange can be handed *less* space than
        // the child desired. Only growing here left a star row at its measured
        // height and pushed everything below it out of the window.
        const float delta = availW - usedW;
        for (size_t i = 0; i < nCols; ++i)
            if (columns_[i].kind == GridLength::Kind::Star)
                colW[i] = std::max(0.0f, colW[i] + delta * (columns_[i].value / starW));
    }
    float usedH = 0;
    float starH = 0;
    for (size_t i = 0; i < nRows; ++i) {
        usedH += rowH[i];
        if (rows_[i].kind == GridLength::Kind::Star) starH += rows_[i].value;
    }
    usedH += barH * (nRows > 1 ? static_cast<float>(nRows - 1) : 0.0f);
    const float availH = std::max(0.0f, finalRect.h - margin.V() - pad.H() * 0 - pad.V());
    if (starH > 0 && availH != usedH) {
        const float delta = availH - usedH;
        for (size_t i = 0; i < nRows; ++i)
            if (rows_[i].kind == GridLength::Kind::Star)
                rowH[i] = std::max(0.0f, rowH[i] + delta * (rows_[i].value / starH));
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
    const int slot = SlotOf("IsExpanded");
    // fromBinding=true: the user toggled the header, so a TwoWay binding must
    // publish it. OnSlotChanged sees the value already adopted and stops.
    if (slot >= 0) SetSlotValue(slot, Value::Bool(value), true);
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
    slotRealized_ = SlotOf("RealizedCount");
    slotVirtualized_ = SlotOf("IsVirtualized");
    slotFirstVisible_ = SlotOf("FirstVisibleIndex");
    slotScrollOffset_ = SlotOf("ScrollOffset");
    slotScrollMax_ = SlotOf("ScrollMax");
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
    // A different row height moves every row, so the window has to be recomputed
    // as well as re-measured.
    if (slot == SlotOf("ItemHeight")) {
        itemHeight_ = std::max(1.0f, Get<float>(slot));
        ClampScroll();
        UpdateWindow();
        Invalidate(AffectsMeasure | AffectsRender);
    }
    // ScrollOffset is the one virtualisation property that is also an input: a
    // ViewModel may scroll the list by setting it. Writing it from inside ScrollTo
    // would otherwise echo straight back here and fight the value we just applied.
    if (slot == slotScrollOffset_ && !writingScrollOffset_) {
        ScrollTo(static_cast<float>(value.num));
    }
    Selector::OnSlotChanged(slot, value);
}

size_t ListBox::ItemCount() const {
    // The MODEL count, not the realized count: a virtualized list reports 10 000
    // items while holding twenty widgets. Reporting the widget count would make
    // selection clamp at the end of the window, so arrowing past the bottom of the
    // viewport would silently stop.
    return modelCount_;
}

RefPtr ListBox::ItemAt(size_t index) const {
    if (!list_ || index >= list_->Count()) return nullptr;
    return list_->ItemAt(index);
}

Widget* ListBox::ItemWidgetAt(size_t i) const {
    // `i` is a MODEL index, not an offset into the realized window: callers ask
    // "row 4 000", and a virtualized list has no widget for it. Returning null is
    // the answer -- see the contract in Controls.h.
    if (i < realizedFirst_) return nullptr;
    const size_t local = i - realizedFirst_;
    return local < itemWidgets_.size() ? itemWidgets_[local].get() : nullptr;
}

void ListBox::OnSelectionChanged() {
    // Keep the visual highlight and any scroll position consistent with the
    // selection: a selection the user cannot see is indistinguishable from no
    // selection at all. On a virtualized list the selected row may not even be
    // realized yet, so this is also what brings it into the window.
    ScrollIntoView(selectedIndex_);
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
    case KeyPageDown:
        SetSelectedIndex(PageStep());
        return true;
    case KeyPageUp:
        SetSelectedIndex(std::max(0, selectedIndex_ - PageStep()));
        return true;
    default:
        return Widget::OnKeyDown(vk);
    }
}

int ListBox::PageStep() const {
    // At least 1: a viewport shorter than one row would otherwise make PageDown a
    // no-op, which reads as "the key is broken".
    const float usable = viewportH_ > 0 ? viewportH_ : 0.0f;
    return std::max(1, static_cast<int>(usable / std::max(1.0f, itemHeight_)));
}

void ListBox::OnPointerDown(const Point& local) {
    // Selection happens on press, matching how lists behave everywhere else --
    // press-and-drag-off still leaves the row selected.
    //
    // `local` is already in CONTENT space: Widget::ToLocal adds back every scroll
    // shift between the root and here, and HitTest did the same on the way down.
    // Rows are arranged in that same space, so no further adjustment belongs here
    // -- adding offset_ a second time would offset the click by twice the scroll.
    for (size_t i = 0; i < itemWidgets_.size(); ++i) {
        Widget* item = itemWidgets_[i].get();
        if (item && item->Bounds().Contains(local)) {
            SetSelectedIndex(static_cast<int>(realizedFirst_ + i));
            break;
        }
    }
}

void ListBox::OnPointerUp(const Point&) {}

void ListBox::OnListChanged(const ListChange& change) {
    RebuildItems(text_);
    // Reselecting through the model keeps SelectedIndex valid after a removal in
    // front of it; without this the highlight would stay on whatever row now
    // occupies the old index.
    (void)change;
}

void ListBox::RebuildItems(ITextService* text) {
    text_ = text;
    for (auto& w : itemWidgets_) w->DetachBindings();
    itemWidgets_.clear();
    ClearChildren();
    realizedFirst_ = 0;
    modelCount_ = list_ ? list_->Count() : 0;
    template_ = std::dynamic_pointer_cast<DataTemplate>(Get<RefPtr>(slotTemplate_));
    itemHeight_ = std::max(1.0f, Get<float>(SlotOf("ItemHeight")));
    // The window cannot be computed before there is a viewport; UpdateWindow()
    // falls back to "realize everything" until one is known, which is what keeps
    // a list that has never been laid out (the common case in tests) usable.
    ClampScroll();
    UpdateWindow();
    Invalidate(AffectsMeasure | AffectsRender);
}

WidgetPtr ListBox::CreateItemWidget(size_t index) {
    RefPtr item = list_ ? list_->ItemAt(index) : nullptr;
    WidgetPtr w;
    if (template_ && template_->instantiate) w = template_->instantiate();
    if (!w) {
        // No template: show the item itself. A list with no template is still a
        // list, and a blank row would be a hole rather than a fallback.
        auto tb = std::make_shared<TextBlock>();
        tb->BindType(TypeRegistry::Instance().Find("wui/TextBlock"));
        tb->SetSlotValue(tb->SlotOf("Text"), Value::Text(FormatValueText(Value::Ref(item))));
        w = tb;
    }
    w->SetDataContext(item);
    w->AttachBindings(text_);
    return w;
}

void ListBox::SetWindow(size_t first, size_t last) {
    first = std::min(first, modelCount_);
    last = std::min(std::max(last, first), modelCount_);
    // Nothing to do for the overwhelmingly common case: the window did not move.
    if (first == realizedFirst_ && last - first == itemWidgets_.size()) return;

    for (auto& w : itemWidgets_) w->DetachBindings();
    itemWidgets_.clear();
    ClearChildren();
    realizedFirst_ = first;
    itemWidgets_.reserve(last - first);
    for (size_t i = first; i < last; ++i) {
        WidgetPtr w = CreateItemWidget(i);
        itemWidgets_.push_back(w);
        AddChild(w);
    }
    PublishState();
    Invalidate(AffectsMeasure | AffectsRender);
}

void ListBox::UpdateWindow() {
    if (!virtualized_) {
        // Not windowed: every row exists. Rebuild items also lands here when the
        // viewport is unknown.
        SetWindow(0, modelCount_);
        return;
    }
    if (modelCount_ == 0) {
        SetWindow(0, 0);
        return;
    }
    constexpr size_t kOverscan = 1;
    const float rowH = std::max(1.0f, itemHeight_);
    const size_t firstVisible = static_cast<size_t>(offset_ / rowH);
    const size_t visible = static_cast<size_t>((viewportH_ > 0 ? viewportH_ : rowH) / rowH) + 1;
    size_t first = firstVisible >= kOverscan ? firstVisible - kOverscan : 0;
    size_t last = std::min(modelCount_, firstVisible + visible + kOverscan);
    SetWindow(first, last);
}

void ListBox::ClampScroll() {
    const float rowH = std::max(1.0f, itemHeight_);
    const float contentH = rowH * static_cast<float>(modelCount_);
    maxScroll_ = std::max(0.0f, contentH - viewportH_);
    if (offset_ > maxScroll_) offset_ = maxScroll_;
    if (offset_ < 0) offset_ = 0;
}

void ListBox::PublishState() {
    // Published to whoever binds them. These are diagnostics surfaces for the sample
    // page and the tests: the control is the source of truth for "where is my window
    // and how big is it", so it writes them, not the ViewModel.
    auto publish = [this](int slot, const Value& v) {
        if (slot < 0) return;
        if (RawSlot(slot).Equals(v)) return;
        SetSlotValue(slot, v, true);
    };
    publish(slotRealized_, Value::Number(static_cast<double>(itemWidgets_.size())));
    publish(slotVirtualized_, Value::Bool(virtualized_));
    publish(slotFirstVisible_, Value::Number(static_cast<double>(FirstVisibleIndex())));
    publish(slotScrollMax_, Value::Number(static_cast<double>(maxScroll_)));
    // Guarded: this write must not come back as a scroll request (see OnSlotChanged).
    if (slotScrollOffset_ >= 0) {
        const Value v = Value::Number(static_cast<double>(offset_));
        if (!RawSlot(slotScrollOffset_).Equals(v)) {
            writingScrollOffset_ = true;
            SetSlotValue(slotScrollOffset_, v, true);
            writingScrollOffset_ = false;
        }
    }
}

size_t ListBox::FirstVisibleIndex() const {
    if (!virtualized_) return 0;
    const float rowH = std::max(1.0f, itemHeight_);
    const size_t first = static_cast<size_t>(offset_ / rowH);
    return std::min(first, modelCount_);
}

void ListBox::ScrollTo(float offset) {
    const float target = std::min(std::max(0.0f, offset), maxScroll_);
    if (target == offset_) return;
    offset_ = target;
    UpdateWindow();
    Invalidate(AffectsRender);
}

void ListBox::ScrollIntoView(int index) {
    if (!virtualized_ || index < 0) return;
    const float rowH = std::max(1.0f, itemHeight_);
    const float top = rowH * static_cast<float>(index);
    const float bottom = top + rowH;
    float target = offset_;
    if (top < offset_) target = top;
    else if (bottom > offset_ + viewportH_) target = bottom - viewportH_;
    ScrollTo(target);
}

bool ListBox::OnPointerWheel(float deltaLines) {
    // Only a windowed list scrolls itself. A non-virtualized one has no viewport of
    // its own (its height is its content), so the wheel belongs to the scroller
    // around it -- bubbling up is what makes a page with a long list scroll.
    if (!virtualized_) return Widget::OnPointerWheel(deltaLines);
    if (maxScroll_ <= 0) return Widget::OnPointerWheel(deltaLines);
    constexpr float kLineHeight = 48.0f;
    ScrollTo(offset_ - deltaLines * kLineHeight);
    return true;
}

Size ListBox::Measure(const Size& available) {
    const float marginH = Get<Thickness>(SlotOf("Margin")).H();
    const float marginV = Get<Thickness>(SlotOf("Margin")).V();
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    itemHeight_ = std::max(1.0f, Get<float>(SlotOf("ItemHeight")));
    if (list_) modelCount_ = list_->Count();

    // A bounded height means "I am the viewport": window the rows. An unbounded
    // one (a ScrollViewer measures its child at 1e6) means "grow to fit": realize
    // everything and let the outer scroller do the scrolling. Deriving this from
    // the constraint rather than from a flag is what keeps existing pages working
    // unchanged -- list inside a ScrollViewer, as every sample is, still shows
    // every row.
    constexpr float kUnbounded = 1.0e6f;
    const float offeredH = std::max(0.0f, available.h - marginV - pad.V());
    const float explicitH = Get<float>(SlotOf("Height"));
    const bool bounded = explicitH > 0 || (available.h > 0 && available.h < kUnbounded * 0.5f);
    const Size inner{std::max(0.0f, available.w - marginH - pad.H()),
                     bounded ? offeredH : available.h};

    if (bounded) {
        // The viewport is what the parent offered, so there is no need to measure
        // an unknowable number of rows: measure the representative rows that are
        // realized (the window is refreshed from the new viewport right after).
        viewportH_ = explicitH > 0 ? explicitH : offeredH;
        virtualized_ = true;
        ClampScroll();
        UpdateWindow();
    } else {
        viewportH_ = 0;
        const bool wasVirtualized = virtualized_;
        virtualized_ = false;
        if (wasVirtualized) UpdateWindow();
    }

    float maxW = 0;
    for (auto& w : itemWidgets_) maxW = std::max(maxW, w->Measure(inner).w);
    if (virtualized_) {
        // Windowed: the height the parent offered IS the viewport, and the width is
        // the offered width rather than the widest realized row. Measuring only what
        // is realized would otherwise make the list's width (and therefore every
        // row's) jump as different rows scroll through the window.
        const float h = explicitH > 0 ? explicitH : offeredH;
        const float w = inner.w > 0 ? inner.w : maxW;
        desired_ = Size{std::max(200.0f, w + marginH + pad.H()), h + marginV + pad.V()};
    } else {
        // Not windowed: report the honest content height (every row's height), not
        // just the rows that happen to exist. The rows do all exist here, but saying
        // "rowHeight * modelCount" is the same statement without depending on the
        // window having been refreshed first.
        const float h = itemHeight_ * static_cast<float>(modelCount_);
        desired_ = Size{std::max(200.0f, maxW + marginH + pad.H()), h + marginV + pad.V()};
    }
    ApplySizeConstraints();
    if (virtualized_) {
        // The viewport is the CONTENT box, so an explicit Height still leaves room
        // for margin and padding; reading it back from desired_ keeps the two in step
        // when ApplySizeConstraints clamped the height.
        viewportH_ = std::max(0.0f, desired_.h - marginV - pad.V());
        ClampScroll();
        UpdateWindow();
    }
    PublishState();
    return desired_;
}

void ListBox::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    // An explicit Height declares the VIEWPORT. A parent is free to hand a taller
    // slot (a Grid star row, the last child of a Page, a host that fills the
    // window), and honouring that instead would silently turn Height="300" into
    // "300 requested, as tall as you like" -- the window would then hold twice the
    // rows and MaxScroll would be wrong by the difference. ScrollViewer already
    // does exactly this, for the same reason.
    if (virtualized_ && desired_.h > 0 && desired_.h < bounds_.h) bounds_.h = desired_.h;
    const Rect content = ViewportRect();
    viewportH_ = content.h;
    ClampScroll();
    // Refresh before arranging: the viewport height is only known now, so a list
    // whose first layout happens here must still get the right rows.
    UpdateWindow();
    const float rowH = std::max(1.0f, itemHeight_);
    for (size_t i = 0; i < itemWidgets_.size(); ++i) {
        // Rows are laid out in unscrolled coordinates; paint and hit testing move
        // them by offset_, the same split ScrollViewer uses.
        const float y = content.y + rowH * static_cast<float>(realizedFirst_ + i);
        itemWidgets_[i]->Arrange(Rect{content.x, y, content.w, rowH});
    }
}

Rect ListBox::ViewportRect() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    return Rect{bounds_.x + margin.l + pad.l, bounds_.y + margin.t + pad.t,
                std::max(0.0f, bounds_.w - margin.H() - pad.H()),
                std::max(0.0f, bounds_.h - margin.V() - pad.V())};
}

void ListBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);
    const Rect viewport = ViewportRect();
    ctx.PushClip(viewport);
    // Selection highlight goes UNDER the row content so text stays readable. On a
    // virtualized list the row may have left the window, in which case there is
    // nothing to draw -- the selection is still correct, it is simply elsewhere.
    if (selectedIndex_ >= 0) {
        const size_t local = static_cast<size_t>(selectedIndex_) - realizedFirst_;
        if (local < itemWidgets_.size()) {
            const Rect row = itemWidgets_[local]->Bounds();
            const Color accent = palette.colors[static_cast<int>(Token::Accent)];
            ctx.FillRoundRect(Rect{row.x, row.y - offset_, row.w, row.h}, 4.0f,
                              Color{accent.r, accent.g, accent.b, 0.18f});
        }
    }
    ctx.PopClip();

    // Rows are drawn by PaintChildrenAt (below), so that the scroll shift is
    // applied once to the whole window and the clip is installed in one place.
}

void ListBox::PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) {
    const Rect viewport = ViewportRect();
    if (viewport.w <= 0 || viewport.h <= 0) return;
    if (maxScroll_ <= 0) {
        // Nothing to scroll: skip the clip, which also keeps the non-virtualized
        // path pixel-identical to what it was before virtualisation existed.
        Widget::PaintChildrenAt(ctx, palette, shift);
        return;
    }
    ctx.PushClip(viewport);
    Widget::PaintChildrenAt(ctx, palette, Point{shift.x, shift.y - offset_});
    ctx.PopClip();
}

Widget* ListBox::HitTest(const Point& p) {
    const Rect viewport = ViewportRect();
    if (!viewport.Contains(p)) return nullptr;

    // Rows live at content coordinates, so a point in screen space has to move
    // DOWN by the scroll amount before descending -- exactly the inverse of what
    // PaintChildrenAt does going up.
    const Point inContent{p.x, p.y + offset_};
    for (auto it = itemWidgets_.rbegin(); it != itemWidgets_.rend(); ++it) {
        if (Widget* hit = (*it)->HitTest(inContent)) return hit;
    }
    // Inside the viewport but between/after the rows: claim it, so a click in the
    // empty area past the last row does not fall through to whatever is behind the
    // list -- and so the wheel over blank space still reaches this scroller.
    return this;
}


// ---------------------------------------------------------------------------
// TreeView
//
// A tree is a list of the nodes that happen to be visible. The flattened row
// vector is rebuilt on layout, so an IsExpanded change made by a command is
// picked up on the next paint without the control having to observe every node.
// ---------------------------------------------------------------------------
TreeNode::TreeNode(std::wstring title) {
    Title.Set(std::move(title));
    Make("Title", Title);
    Make("IsExpanded", IsExpanded);
}

void TreeNode::Add(std::shared_ptr<TreeNode> child) {
    if (child) children_.push_back(std::move(child));
}

std::vector<std::shared_ptr<ITreeItem>> TreeNode::TreeChildren() const {
    std::vector<std::shared_ptr<ITreeItem>> out;
    out.reserve(children_.size());
    for (const auto& child : children_) out.push_back(child);
    return out;
}

TreeView::TreeView() {
    BindType(TypeRegistry::Instance().Find("wui/TreeView"));
    slotItems_ = SlotOf("ItemsSource");
    slotSelected_ = SlotOf("SelectedItem");
    slotOffset_ = SlotOf("ScrollOffset");
}

TreeView::~TreeView() { UnsubscribeSource(); }

void TreeView::SubscribeSource() {
    auto ref = Get<RefPtr>(slotItems_);
    auto* list = dynamic_cast<ObservableListBase*>(ref.get());
    if (list == list_) return;
    UnsubscribeSource();
    list_ = list;
    listRef_ = ref;
    if (list_) list_->Subscribe(this);
    Rebuild();
}

void TreeView::UnsubscribeSource() {
    if (list_) list_->Unsubscribe(this);
    list_ = nullptr;
    listRef_.reset();
}

void TreeView::OnListChanged(const ListChange&) { Rebuild(); }

void TreeView::OnSlotChanged(int slot, const Value& value) {
    if (slot == slotItems_) {
        SubscribeSource();
        return;
    }
    if (slot == slotSelected_ && !writingSelection_) {
        SetSelectedItem(Get<RefPtr>(slot));
        return;
    }
    if (slot == slotOffset_ && !writingOffset_) {
        ScrollTo(static_cast<float>(value.num));
        return;
    }
    if (slot == SlotOf("ItemHeight") || slot == SlotOf("Indent")) {
        Invalidate(AffectsMeasure | AffectsRender);
    }
}

void TreeView::Flatten(const RefPtr& ref, int depth, size_t parent) {
    // Both limits are defensive: a cyclic model must produce a diagnostic-worthy
    // finite tree, not an infinite loop on the next layout pass.
    if (!ref || depth > 64 || rows_.size() >= 4096) return;
    auto* item = dynamic_cast<ITreeItem*>(ref.get());
    if (!item) return;
    auto children = item->TreeChildren();
    const size_t index = rows_.size();
    rows_.push_back(Row{ref, depth, parent, !children.empty()});
    if (!item->TreeExpanded()) return;
    for (auto& child : children) {
        RefPtr childRef = std::dynamic_pointer_cast<RefObject>(child);
        if (childRef) Flatten(childRef, depth + 1, index);
    }
}

void TreeView::Rebuild() {
    rows_.clear();
    modelCount_ = list_ ? list_->Count() : 0;
    for (size_t i = 0; i < modelCount_; ++i) Flatten(list_->ItemAt(i), 0, size_t(-1));

    // A collapsed branch takes its selection off screen, but does not change
    // WHAT is selected: the selection comes back when the branch is reopened.
    selectedRow_ = size_t(-1);
    if (selected_) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].item == selected_) {
                selectedRow_ = i;
                break;
            }
        }
    }
    ClampScroll();
    Invalidate(AffectsMeasure | AffectsRender);
}

void TreeView::ClampScroll() {
    const float contentH = itemHeight_ * static_cast<float>(rows_.size());
    maxScroll_ = std::max(0.0f, contentH - viewportH_);
    offset_ = std::clamp(offset_, 0.0f, maxScroll_);
}

Rect TreeView::ViewportRect() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    return Rect{bounds_.x + margin.l + pad.l, bounds_.y + margin.t + pad.t,
                std::max(0.0f, bounds_.w - margin.H() - pad.H()),
                std::max(0.0f, bounds_.h - margin.V() - pad.V())};
}

void TreeView::PublishState() {
    if (slotOffset_ < 0) return;
    const Value v = Value::Number(static_cast<double>(offset_));
    if (RawSlot(slotOffset_).Equals(v)) return;
    writingOffset_ = true;
    SetSlotValue(slotOffset_, v, true);
    writingOffset_ = false;
}

void TreeView::ScrollTo(float offset) {
    const float target = std::clamp(offset, 0.0f, maxScroll_);
    if (target == offset_) return;
    offset_ = target;
    PublishState();
    Invalidate(AffectsRender);
}

void TreeView::SelectRow(size_t row) {
    if (row >= rows_.size()) return;
    selectedRow_ = row;
    const RefPtr item = rows_[row].item;
    if (item != selected_) {
        selected_ = item;
        if (!writingSelection_ && slotSelected_ >= 0) {
            writingSelection_ = true;
            SetSlotValue(slotSelected_, Value::Ref(selected_), true);
            writingSelection_ = false;
        }
    }
    const float top = itemHeight_ * static_cast<float>(row);
    const float bottom = top + itemHeight_;
    if (top < offset_) ScrollTo(top);
    else if (bottom > offset_ + viewportH_) ScrollTo(bottom - viewportH_);
    Invalidate(AffectsRender);
}

void TreeView::SetSelectedItem(const RefPtr& item) {
    if (item == selected_) return;
    selected_ = item;
    selectedRow_ = size_t(-1);
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].item == item) {
            selectedRow_ = i;
            break;
        }
    }
    if (!writingSelection_ && slotSelected_ >= 0) {
        writingSelection_ = true;
        SetSlotValue(slotSelected_, Value::Ref(selected_), true);
        writingSelection_ = false;
    }
    Invalidate(AffectsRender);
}

void TreeView::SetAllExpanded(bool expanded) {
    std::vector<RefPtr> stack;
    if (list_) {
        for (size_t i = 0; i < modelCount_; ++i) stack.push_back(list_->ItemAt(i));
    }
    while (!stack.empty()) {
        const RefPtr ref = stack.back();
        stack.pop_back();
        auto* item = dynamic_cast<ITreeItem*>(ref.get());
        if (!item) continue;
        item->SetTreeExpanded(expanded);
        for (auto& child : item->TreeChildren()) {
            if (RefPtr childRef = std::dynamic_pointer_cast<RefObject>(child)) stack.push_back(childRef);
        }
    }
    Rebuild();
}

Size TreeView::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    itemHeight_ = std::max(1.0f, Get<float>(SlotOf("ItemHeight")));
    indent_ = std::max(0.0f, Get<float>(SlotOf("Indent")));
    // Rebuilt here rather than only on list changes: IsExpanded is a property of
    // the NODE, and a command may have flipped it since the last layout.
    if (list_) Rebuild();

    constexpr float kUnbounded = 1.0e6f;
    const float explicitH = Get<float>(SlotOf("Height"));
    const float contentH = itemHeight_ * static_cast<float>(rows_.size());
    const float offeredH = std::max(0.0f, available.h - margin.V() - pad.V());
    bounded_ = explicitH > 0 || (available.h > 0 && available.h < kUnbounded * 0.5f);

    if (bounded_) {
        viewportH_ = explicitH > 0 ? explicitH : offeredH;
        ClampScroll();
        desired_ = Size{std::max(200.0f, available.w), viewportH_ + margin.V() + pad.V()};
    } else {
        viewportH_ = contentH;
        offset_ = 0;
        maxScroll_ = 0;
        desired_ = Size{std::max(200.0f, available.w), contentH + margin.V() + pad.V()};
    }
    ApplySizeConstraints();
    if (bounded_) {
        viewportH_ = std::max(0.0f, desired_.h - margin.V() - pad.V());
        ClampScroll();
    }
    PublishState();
    return desired_;
}

void TreeView::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    if (bounded_ && desired_.h > 0 && desired_.h < bounds_.h) bounds_.h = desired_.h;
    const Rect content = ViewportRect();
    viewportH_ = content.h;
    ClampScroll();
}

void TreeView::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Color bg = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (bg.a > 0) ctx.FillRect(bounds_, bg);

    const Rect view = ViewportRect();
    if (view.w <= 0 || view.h <= 0) return;
    ctx.PushClip(view);

    const Color textColor = palette.colors[static_cast<int>(Token::TextPrimary)];
    const Color muted = palette.colors[static_cast<int>(Token::TextSecondary)];
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];
    const float fontSize = std::max(1.0f, Get<float>(SlotOf("FontSize")));
    const int weight = Get<int>(SlotOf("FontWeight"));

    for (size_t i = 0; i < rows_.size(); ++i) {
        const float y = view.y + itemHeight_ * static_cast<float>(i) - offset_;
        if (y + itemHeight_ < view.y) continue;
        if (y > view.y + view.h) break;

        if (i == selectedRow_) {
            ctx.FillRect(Rect{view.x, y, view.w, itemHeight_},
                         Color{accent.r, accent.g, accent.b, 0.18f});
        }
        const float x = view.x + indent_ * static_cast<float>(rows_[i].depth);
        if (rows_[i].expandable) {
            auto* item = dynamic_cast<ITreeItem*>(rows_[i].item.get());
            const float cx = x + 8.0f;
            const float cy = y + itemHeight_ * 0.5f;
            if (item && item->TreeExpanded()) {
                ctx.DrawLine(Point{cx - 3.0f, cy - 1.0f}, Point{cx, cy + 2.0f}, muted);
                ctx.DrawLine(Point{cx, cy + 2.0f}, Point{cx + 3.0f, cy - 1.0f}, muted);
            } else {
                ctx.DrawLine(Point{cx - 2.0f, cy - 3.0f}, Point{cx + 2.0f, cy}, muted);
                ctx.DrawLine(Point{cx + 2.0f, cy}, Point{cx - 2.0f, cy + 3.0f}, muted);
            }
        }
        const float textX = x + 20.0f;
        auto* item = dynamic_cast<ITreeItem*>(rows_[i].item.get());
        if (!item) continue;
        ctx.DrawText(item->TreeTitle(),
                     Rect{textX, y, std::max(0.0f, view.x + view.w - textX), itemHeight_},
                     textColor, fontSize, weight, TextAlign::Left);
    }
    ctx.PopClip();
}

bool TreeView::ToggleAt(const Point& local) {
    const Rect view = ViewportRect();
    const float relY = local.y - view.y;
    if (relY < 0) return false;
    const size_t row = static_cast<size_t>(relY / itemHeight_);
    if (row >= rows_.size() || !rows_[row].expandable) return false;
    const float cx = view.x + indent_ * static_cast<float>(rows_[row].depth) + 8.0f;
    if (local.x < cx - 9.0f || local.x > cx + 9.0f) return false;
    auto* item = dynamic_cast<ITreeItem*>(rows_[row].item.get());
    if (!item) return false;
    item->SetTreeExpanded(!item->TreeExpanded());
    Rebuild();
    return true;
}

void TreeView::OnPointerDown(const Point& local) {
    if (ToggleAt(local)) return;
    const Rect view = ViewportRect();
    const float relY = local.y - view.y;
    if (relY < 0 || relY >= itemHeight_ * static_cast<float>(rows_.size())) return;
    SelectRow(static_cast<size_t>(relY / itemHeight_));
}

bool TreeView::OnPointerWheel(float deltaLines) {
    if (maxScroll_ <= 0) return Widget::OnPointerWheel(deltaLines);
    ScrollTo(offset_ - deltaLines * 48.0f);
    return true;
}

bool TreeView::OnKeyDown(int vk) {
    switch (vk) {
    case KeyDown:
        if (!rows_.empty()) SelectRow(std::min(selectedRow_ + 1, rows_.size() - 1));
        return true;
    case KeyUp:
        if (!rows_.empty() && selectedRow_ != size_t(-1)) SelectRow(selectedRow_ - 1);
        return true;
    case KeyHome:
        if (!rows_.empty()) SelectRow(0);
        return true;
    case KeyEnd:
        if (!rows_.empty()) SelectRow(rows_.size() - 1);
        return true;
    case KeyLeft: {
        if (selectedRow_ >= rows_.size()) return true;
        const Row row = rows_[selectedRow_]; // copy: Rebuild invalidates rows_
        auto* item = dynamic_cast<ITreeItem*>(row.item.get());
        if (item && row.expandable && item->TreeExpanded()) {
            item->SetTreeExpanded(false);
            Rebuild();
        } else if (row.parent != size_t(-1)) {
            SelectRow(row.parent);
        }
        return true;
    }
    case KeyRight: {
        if (selectedRow_ >= rows_.size()) return true;
        const Row row = rows_[selectedRow_];
        auto* item = dynamic_cast<ITreeItem*>(row.item.get());
        if (item && row.expandable && !item->TreeExpanded()) {
            item->SetTreeExpanded(true);
            Rebuild();
        } else if (selectedRow_ + 1 < rows_.size()) {
            SelectRow(selectedRow_ + 1);
        }
        return true;
    }
    default:
        return Widget::OnKeyDown(vk);
    }
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
        SetSlotValue(slotCloseRequested_, Value::Number(static_cast<double>(index)), true);
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
    auto layer = MakeMenuLayer(std::move(model), ToWindow(Point{title.x, title.y + title.h}),
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
    // Measured at the control's own FontSize, so a style that shrinks the text also
    // shrinks the button instead of leaving it padded out for text that is no
    // longer there.
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float labelW = static_cast<float>(label.size()) * fontSize * 0.55f;
    if (ITextService* svc = TextSystem::Get()) labelW = svc->Measure(label, fontSize, WeightNormal).w;
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
    // rather than growing into a second menu. Both sizes follow FontSize so a style
    // can shrink the whole button, not just its label.
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const Color labelFg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    const Color iconFg = enabled ? fg : labelFg;
    ctx.DrawText(icon, Rect{bounds_.x + 8.0f, bounds_.y, 16.0f, bounds_.h},
                 enabled ? iconFg : fg, fontSize + 2.0f, WeightNormal, TextAlign::Center);
    ctx.DrawText(label, Rect{bounds_.x + 26.0f, bounds_.y,
                             std::max(0.0f, bounds_.w - 30.0f), bounds_.h},
                 enabled ? labelFg : fg, fontSize, WeightNormal, TextAlign::Left);
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
    auto layer = MakeMenuLayer(std::move(model), ToWindow(Point{box.x, box.y + box.h}), [this] {
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
// Expander: bindable IsExpanded
// ---------------------------------------------------------------------------
void Expander::OnSlotChanged(int slot, const Value& value) {
    Widget::OnSlotChanged(slot, value);
    const int own = SlotOf("IsExpanded");
    if (own < 0 || slot != own) return;
    // A binding (or markup) set the property: adopt it without echoing it back.
    const bool next = value.type == VType::Bool ? value.flag : false;
    if (expanded_ == next) return;
    expanded_ = next;
    Invalidate(AffectsMeasure | AffectsRender);
}

// ---------------------------------------------------------------------------
// AppBarToggleButton
// ---------------------------------------------------------------------------
void AppBarToggleButton::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    AppBarButton::OnPaint(ctx, palette);
    const int slot = SlotOf("IsChecked");
    if (slot < 0 || !Get<bool>(slot)) return;
    // A checked toggle keeps the toolbar's compact look but gains an accent rule
    // along the bottom edge -- the same affordance the tab strip uses.
    ctx.FillRect(Rect{bounds_.x + 4.0f, bounds_.Bottom() - 3.0f,
                      std::max(0.0f, bounds_.w - 8.0f), 2.0f},
                 palette.colors[static_cast<int>(Token::Accent)]);
}

void AppBarToggleButton::OnPointerDown(const Point& local) {
    (void)local;
    SetPressed(true);
    Invalidate(AffectsRender);
}

void AppBarToggleButton::OnPointerUp(const Point& local) {
    const bool wasPressed = pressed_;
    SetPressed(false);
    if (wasPressed && bounds_.Contains(local)) {
        Toggle();
        AppBarButton::OnPointerUp(local); // the command, if any, still runs
    }
    Invalidate(AffectsRender);
}

bool AppBarToggleButton::OnKeyDown(int vk) {
    if (vk == KeySpace || vk == KeyReturn) {
        Toggle();
        AppBarButton::Invoke();
        return true;
    }
    return AppBarButton::OnKeyDown(vk);
}

void AppBarToggleButton::OnSlotChanged(int slot, const Value& value) {
    Widget::OnSlotChanged(slot, value);
    const int own = SlotOf("IsChecked");
    if (own >= 0 && slot == own) Invalidate(AffectsRender);
}

void AppBarToggleButton::Toggle() {
    const int slot = SlotOf("IsChecked");
    if (slot < 0 || !Get<bool>(SlotOf("IsEnabled"))) return;
    SetSlotValue(slot, Value::Bool(!Get<bool>(slot)), true);
}

// ---------------------------------------------------------------------------
// RatingControl
// ---------------------------------------------------------------------------
namespace {
int RatingMax(const Widget& w) {
    const int slot = w.SlotOf("MaxRating");
    const int max = slot >= 0 ? w.Get<int>(slot) : 5;
    return std::clamp(max, 1, 10);
}

float RatingGlyphWidth(const Widget& w, float fontSize) {
    (void)w; // kept in the signature so callers read as "this widget's font metrics"
    if (ITextService* svc = TextSystem::Get()) return svc->Measure(L"★", fontSize, WeightNormal).w;
    return fontSize * 0.9f;
}

constexpr float kRatingGap = 2.0f;
} // namespace

Size RatingControl::Measure(const Size& /*available*/) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const int max = RatingMax(*this);
    const float glyph = RatingGlyphWidth(*this, fontSize);
    desired_ = Size{static_cast<float>(max) * glyph + static_cast<float>(max - 1) * kRatingGap +
                        margin.H(),
                    fontSize * 1.4f + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

int RatingControl::RatingAt(const Point& p) const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (!box.Contains(p)) return -1;
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const float glyph = RatingGlyphWidth(*this, fontSize);
    const float step = glyph + kRatingGap;
    const int index = static_cast<int>((p.x - box.x) / step);
    const int max = RatingMax(*this);
    return std::clamp(index, 0, max - 1) + 1;
}

int RatingControl::DisplayedRating() const {
    if (hovered_ > 0) return hovered_;
    const int slot = SlotOf("Value");
    const double value = slot >= 0 ? Get<double>(slot) : 0.0;
    return std::clamp(static_cast<int>(std::floor(value + 0.5)), 0, RatingMax(*this));
}

void RatingControl::Commit(int rating) {
    const int slot = SlotOf("Value");
    if (slot < 0) return;
    // fromBinding=true: clicking is the user changing the value.
    SetSlotValue(slot, Value::Number(rating), true);
    Invalidate(AffectsRender);
}

void RatingControl::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool enabled = Get<bool>(SlotOf("IsEnabled"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const float glyph = RatingGlyphWidth(*this, fontSize);
    const Color filled = enabled ? ResolveColor(Get<Color>(SlotOf("Foreground")), palette)
                                 : palette.colors[static_cast<int>(Token::TextDisabled)];
    const Color empty = palette.colors[static_cast<int>(Token::CardBorder)];
    const int shown = DisplayedRating();
    const int max = RatingMax(*this);
    for (int i = 0; i < max; ++i) {
        const Rect r{box.x + i * (glyph + kRatingGap), box.y, glyph, box.h};
        ctx.DrawText(i < shown ? L"\u2605" : L"\u2606", r, i < shown ? filled : empty,
                     fontSize, WeightNormal, TextAlign::Center);
    }
}

Widget* RatingControl::HitTest(const Point& p) {
    return Get<bool>(SlotOf("IsEnabled")) && bounds_.Contains(p) ? this : nullptr;
}

void RatingControl::OnPointerMove(const Point& local) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    const int next = RatingAt(local);
    if (next == hovered_) return;
    hovered_ = next;
    Invalidate(AffectsRender);
}

void RatingControl::OnPointerUp(const Point& local) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    const int rating = RatingAt(local);
    if (rating > 0) Commit(rating);
}

void RatingControl::OnPointerLeave() {
    Widget::OnPointerLeave();
    hovered_ = -1;
    Invalidate(AffectsRender);
}

bool RatingControl::OnKeyDown(int vk) {
    if (vk != KeyLeft && vk != KeyRight) return Widget::OnKeyDown(vk);
    const int slot = SlotOf("Value");
    if (slot < 0) return false;
    const int current = DisplayedRating();
    Commit(std::clamp(current + (vk == KeyRight ? 1 : -1), 0, RatingMax(*this)));
    return true;
}

// ---------------------------------------------------------------------------
// BreadcrumbBar
// ---------------------------------------------------------------------------
namespace {
constexpr float kBreadcrumbSeparatorWidth = 18.0f;
} // namespace

std::vector<std::wstring> BreadcrumbBar::SplitItems(const std::wstring& items) {
    return ComboBox::SplitItems(items);
}

Size BreadcrumbBar::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const auto items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    float total = 0.0f;
    for (size_t i = 0; i < items.size(); ++i) {
        float w = static_cast<float>(items[i].size()) * fontSize * 0.55f;
        if (ITextService* svc = TextSystem::Get()) w = svc->Measure(items[i], fontSize, WeightNormal).w;
        total += w;
        if (i + 1 < items.size()) total += kBreadcrumbSeparatorWidth;
    }
    desired_ = Size{total + margin.H(), fontSize * 1.5f + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

Rect BreadcrumbBar::CrumbRect(size_t index) const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const auto items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    const float height = std::max(0.0f, bounds_.h - margin.V());
    float x = bounds_.x + margin.l;
    for (size_t i = 0; i < items.size(); ++i) {
        float w = static_cast<float>(items[i].size()) * fontSize * 0.55f;
        if (ITextService* svc = TextSystem::Get()) w = svc->Measure(items[i], fontSize, WeightNormal).w;
        if (i == index) return Rect{x, bounds_.y + margin.t, w, height};
        x += w + kBreadcrumbSeparatorWidth;
    }
    return Rect{};
}

int BreadcrumbBar::IndexAt(const Point& p) const {
    const auto items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    for (size_t i = 0; i < items.size(); ++i) {
        if (CrumbRect(i).Contains(p)) return static_cast<int>(i);
    }
    return -1;
}

void BreadcrumbBar::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const auto items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    if (items.empty()) return;
    const int selected = Get<int>(SlotOf("SelectedIndex"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const Color normal = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];
    const Color muted = palette.colors[static_cast<int>(Token::TextSecondary)];
    for (size_t i = 0; i < items.size(); ++i) {
        const Rect r = CrumbRect(i);
        const bool current = static_cast<int>(i) == selected;
        ctx.DrawText(items[i], r, current ? accent : normal, fontSize, WeightNormal, TextAlign::Left);
        if (current) {
            ctx.FillRect(Rect{r.x, r.Bottom() - 2.0f, r.w, 2.0f}, accent);
        }
        if (i + 1 < items.size()) {
            ctx.DrawText(L"\u203A", Rect{r.Right(), r.y, kBreadcrumbSeparatorWidth, r.h}, muted,
                         fontSize, WeightNormal, TextAlign::Center);
        }
    }
}

Widget* BreadcrumbBar::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    return bounds_.Contains(p) ? this : nullptr;
}

void BreadcrumbBar::OnPointerMove(const Point& local) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    const int next = IndexAt(local);
    if (next == hovered_) return;
    hovered_ = next;
    Invalidate(AffectsRender);
}

void BreadcrumbBar::Commit(int index) {
    const int slot = SlotOf("SelectedIndex");
    if (slot >= 0) SetSlotValue(slot, Value::Number(index), true);
    auto command = Get<RefPtr>(SlotOf("Command"));
    if (auto* cmd = dynamic_cast<ICommand*>(command.get())) {
        if (cmd->CanExecute()) cmd->Execute();
    }
    Invalidate(AffectsRender);
}

void BreadcrumbBar::OnPointerUp(const Point& local) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    const int index = IndexAt(local);
    if (index >= 0) Commit(index);
}

void BreadcrumbBar::OnPointerLeave() {
    Widget::OnPointerLeave();
    hovered_ = -1;
    Invalidate(AffectsRender);
}

bool BreadcrumbBar::OnKeyDown(int vk) {
    if (vk != KeyLeft && vk != KeyRight) return Widget::OnKeyDown(vk);
    const int slot = SlotOf("SelectedIndex");
    if (slot < 0) return false;
    const auto items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    if (items.empty()) return false;
    const int current = std::clamp(Get<int>(slot), 0, static_cast<int>(items.size()) - 1);
    Commit(std::clamp(current + (vk == KeyRight ? 1 : -1), 0,
                      static_cast<int>(items.size()) - 1));
    return true;
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
// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------
namespace {
// Bar geometry shared by Measure and OnPaint so the two cannot disagree about how
// tall a bar is.
constexpr float kProgressBarHeight = 6.0f;
constexpr float kProgressRingSize = 32.0f;
constexpr float kProgressThickness = 3.0f;
} // namespace

Size ProgressBar::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    // Fills the width it is given. A progress bar that sized to its content would be
    // a 6-pixel-wide dot.
    const float w = available.w > 0 && available.w < 1.0e6f ? available.w : 160.0f;
    desired_ = Size{w, kProgressBarHeight + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void ProgressBar::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect track{bounds_.x + margin.l, bounds_.y + margin.t,
                     std::max(0.0f, bounds_.w - margin.H()),
                     std::max(0.0f, bounds_.h - margin.V())};
    if (track.w <= 0 || track.h <= 0) return;

    const Color trackColor = palette.colors[static_cast<int>(Token::ControlFill)];
    ctx.FillRoundRect(track, track.h * 0.5f, trackColor);

    const float value = Get<float>(SlotOf("Value"));
    // Clamped, not trusted: a ViewModel that briefly reports 1.2 must not paint
    // outside its own track and cover the neighbouring control.
    const float fraction = std::clamp(value, 0.0f, 1.0f);
    if (fraction <= 0.0f) return;
    const Color fill = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    const float w = track.w * fraction;
    ctx.FillRoundRect(Rect{track.x, track.y, w, track.h}, track.h * 0.5f, fill);
}

// ---------------------------------------------------------------------------
// ProgressRing
// ---------------------------------------------------------------------------
Size ProgressRing::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float size = Get<float>(SlotOf("Size"));
    const float side = size > 0 ? size : kProgressRingSize;
    desired_ = Size{side + margin.H(), side + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void ProgressRing::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;

    const float thickness = Get<float>(SlotOf("Thickness"));
    const float stroke = thickness > 0 ? thickness : kProgressThickness;
    const float radius = std::min(box.w, box.h) * 0.5f - stroke * 0.5f;
    if (radius <= 0) return;

    const Point center{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
    const float value = std::clamp(Get<float>(SlotOf("Value")), 0.0f, 1.0f);
    Color fill = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    Color track = ResolveColor(Get<Color>(SlotOf("TrackColor")), palette);
    if (track.a <= 0) track = palette.colors[static_cast<int>(Token::ControlFill)];

    // A full ring is two anti-aliased ellipses -- no stepped-line jaggies.
    if (value >= 0.999f) {
        const float outerR = radius + stroke * 0.5f;
        const float innerR = std::max(0.0f, radius - stroke * 0.5f);
        ctx.FillEllipse(Rect{center.x - outerR, center.y - outerR, 2 * outerR, 2 * outerR}, fill);
        if (innerR > 0)
            ctx.FillEllipse(Rect{center.x - innerR, center.y - innerR, 2 * innerR, 2 * innerR}, track);
        return;
    }

    // Partial arcs: many short chords keep the edge visually round.
    constexpr int kSegments = 180;
    auto pointAt = [&](float fraction) {
        const float angle = -3.14159265f * 0.5f + fraction * 6.28318530f;
        return Point{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    };
    for (int i = 0; i < kSegments; ++i) {
        const float f0 = static_cast<float>(i) / kSegments;
        const float f1 = static_cast<float>(i + 1) / kSegments;
        ctx.DrawLine(pointAt(f0), pointAt(f1), track, stroke);
    }
    if (value <= 0.0f) return;
    const int arcSegments = static_cast<int>(static_cast<float>(kSegments) * value + 0.5f);
    for (int i = 0; i < arcSegments; ++i) {
        const float f0 = static_cast<float>(i) / kSegments;
        const float f1 = static_cast<float>(i + 1) / kSegments;
        ctx.DrawLine(pointAt(f0), pointAt(f1), fill, stroke);
    }
}

// ---------------------------------------------------------------------------
// LoadingSpinner
// ---------------------------------------------------------------------------
namespace {
constexpr float kSpinnerPi = 3.14159265f;
constexpr float kSpinnerTwoPi = 6.28318530f;
constexpr float kSpinnerDefaultSize = 44.0f;
constexpr float kSpinnerDefaultThickness = 3.0f;

// Positive modulo. A phase may be bound to a wall clock or wrapped at 4096 by
// the page; neither a negative number nor a very large one should reverse a
// spinner or eat floating-point precision.
float Wrap(float value, float period) {
    float out = std::fmod(value, period);
    if (out < 0.0f) out += period;
    return out;
}

// The palette resolves a token once; the fading shapes then need per-primitive
// alpha on that concrete colour. Writing a token back into the slot would ask
// the painter to resolve it a second time.
Color Faded(Color c, float alpha) {
    c.token = Token::None;
    c.a = std::clamp(c.a * std::clamp(alpha, 0.0f, 1.0f), 0.0f, 1.0f);
    return c;
}
} // namespace

Size LoadingSpinner::Measure(const Size& /*available*/) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    float side = Get<float>(SlotOf("Size"));
    if (side <= 0.0f) side = kSpinnerDefaultSize;
    desired_ = Size{side + margin.H(), side + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void LoadingSpinner::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0.0f || box.h <= 0.0f) return;

    const Color base = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    // A paused spinner stays visible but dimmed: a still frame should not look
    // broken, and the pause button should have an obvious effect.
    const float dim = Get<bool>(SlotOf("IsActive")) ? 1.0f : 0.35f;
    float thickness = Get<float>(SlotOf("Thickness"));
    if (thickness <= 0.0f) thickness = kSpinnerDefaultThickness;

    const float phase = Get<float>(SlotOf("Phase"));
    const int kind = Get<int>(SlotOf("Kind"));
    const Point center{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
    const float side = std::min(box.w, box.h);

    switch (kind) {
    case 1: { // Bars: four vertical rounded bars, lengths offset like a wave.
        const float barW = std::max(2.0f, thickness * 2.4f);
        const float gap = barW * 0.85f;
        const float startX = center.x - (4.0f * barW + 3.0f * gap) * 0.5f;
        const float minH = side * 0.16f;
        const float maxH = side * 0.88f;
        for (int i = 0; i < 4; ++i) {
            const float wave = 0.5f + 0.5f * std::sin(kSpinnerTwoPi * (phase * 2.0f + i * 0.17f));
            const float h = minH + (maxH - minH) * wave;
            ctx.FillRoundRect(Rect{startX + i * (barW + gap), center.y - h * 0.5f, barW, h},
                              barW * 0.5f, Faded(base, (0.42f + 0.58f * wave) * dim));
        }
        break;
    }
    case 2: { // Dots: the fifth ball arcs OVER the row and lands at its front.
        const float r = std::max(2.5f, side * 0.075f);
        const float pitch = r * 2.0f + std::max(3.0f, r * 0.95f);
        const float startX = center.x - (4.0f * pitch) * 0.5f;
        // The arc must clear the four balls still queueing on the row, so its
        // height comes from the ball radius, not from the control's box.
        const float arc = r * 3.6f;
        const float steps = phase * 5.0f;
        const int step = static_cast<int>(std::floor(steps));
        const float t = Ease(Easing::EaseInOut, steps - static_cast<float>(step));
        for (int i = 0; i < 5; ++i) {
            const int from = (i + step) % 5;
            float slot = static_cast<float>(from) + static_cast<float>((from + 1) % 5 - from) * t;
            float y = center.y;
            float scale = 0.85f + 0.3f * std::max(0.0f, 1.0f - std::fabs(slot));
            if (from == 4) {
                // The rightmost ball leaves the queue, sweeps a half circle above
                // it and arrives as the new first ball. x goes 4 -> 0 while y
                // follows sqrt(1-u^2): a real semicircle, not a sine hump or a
                // fade-out/fade-in teleport. It stays visible the whole way.
                slot = 4.0f * (1.0f - t);
                const float u = 2.0f * t - 1.0f;
                y = center.y - arc * std::sqrt(std::max(0.0f, 1.0f - u * u));
                scale = 1.0f + 0.12f * std::sin(kSpinnerPi * t);
            }
            const float rr = r * scale;
            ctx.FillEllipse(Rect{startX + slot * pitch - rr, y - rr, rr * 2.0f, rr * 2.0f},
                            Faded(base, dim));
        }
        break;
    }
    case 3: { // Bounce: three dots rising in turn (the typing indicator).
        const float r = std::max(3.0f, side * 0.085f);
        const float pitch = r * 2.8f;
        const float startX = center.x - pitch;
        const float amplitude = side * 0.24f;
        for (int i = 0; i < 3; ++i) {
            const float rise = std::max(0.0f, std::sin(kSpinnerTwoPi * (phase * 2.0f - i * 0.22f)));
            const float y = center.y + side * 0.10f - rise * amplitude;
            ctx.FillEllipse(Rect{startX + i * pitch - r, y - r, r * 2.0f, r * 2.0f},
                            Faded(base, (0.3f + 0.7f * rise) * dim));
        }
        break;
    }
    case 0:
    default: { // Ring: a comet head with a fading tail, Windows-style.
        constexpr int kSegments = 12;
        const float radius = side * 0.5f - thickness * 0.5f - 1.0f;
        if (radius <= 0.0f) break;
        const float head = Wrap(phase, 1.0f) * kSpinnerTwoPi;
        auto pointAt = [&](float theta) {
            const float angle = -kSpinnerPi * 0.5f + theta;
            return Point{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        };
        for (int i = 0; i < kSegments; ++i) {
            const float theta = static_cast<float>(i) / kSegments * kSpinnerTwoPi;
            const float behind = Wrap(head - theta, kSpinnerTwoPi) / kSpinnerTwoPi;
            const float brightness = 0.07f + 0.93f * std::pow(1.0f - behind, 6.0f);
            const float a0 = theta + 0.04f;
            const float a1 = theta + kSpinnerTwoPi / kSegments * 0.72f;
            ctx.DrawLine(pointAt(a0), pointAt(a1), Faded(base, brightness * dim), thickness);
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// InfoBar
// ---------------------------------------------------------------------------
namespace {
// Severity -> the accent used for the left edge. Order matches the enum:
// Informational, Success, Warning, Error.
Token SeverityToken(int severity) {
    switch (severity) {
    case 1: return Token::Accent;  // Success -> accent in this palette
    case 2: return Token::Accent;  // Warning
    case 3: return Token::Accent;  // Error
    default: return Token::TextSecondary; // Informational
    }
}
constexpr float kInfoBarPadX = 12.0f;
constexpr float kInfoBarPadY = 10.0f;
constexpr float kInfoBarEdge = 4.0f;
constexpr float kInfoBarTitleSize = 13.0f;
constexpr float kInfoBarBodySize = 12.0f;
} // namespace

Rect InfoBar::CloseBounds() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float size = 24.0f;
    return Rect{bounds_.x + bounds_.w - margin.r - size, bounds_.y + margin.t + 4.0f, size, size};
}

Size InfoBar::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    // A dismissed bar takes no space. Doing it in Measure (rather than only skipping
    // the paint) is what makes the controls below it move up -- a bar that stayed
    // reserved while invisible leaves a gap nobody can explain.
    if (!Get<bool>(SlotOf("IsOpen"))) {
        desired_ = Size{0, 0};
        return desired_;
    }
    const float w = available.w > 0 && available.w < 1.0e6f ? available.w : 320.0f;
    const std::wstring title = Get<std::wstring>(SlotOf("Title"));
    const std::wstring message = Get<std::wstring>(SlotOf("Message"));
    ITextService* svc = TextSystem::Get();
    float inner = std::max(0.0f, w - margin.H() - kInfoBarEdge - kInfoBarPadX * 2 - 28.0f);
    float h = kInfoBarPadY * 2 + margin.V();
    if (!title.empty()) h += svc ? svc->LineHeight(kInfoBarTitleSize) : kInfoBarTitleSize * 1.33f;
    if (!message.empty()) {
        // Wrapped height, estimated from the measured width so a long message makes
        // the bar taller instead of running past its own right edge.
        const float lineH = svc ? svc->LineHeight(kInfoBarBodySize) : kInfoBarBodySize * 1.33f;
        const float textW = svc ? svc->Measure(message, kInfoBarBodySize, WeightNormal).w
                                : static_cast<float>(message.size()) * kInfoBarBodySize * 0.55f;
        const int lines = inner > 0 ? std::max(1, static_cast<int>(std::ceil(textW / inner))) : 1;
        h += lineH * static_cast<float>(lines);
    }
    desired_ = Size{w, std::max(h, 40.0f)};
    ApplySizeConstraints();
    return desired_;
}

void InfoBar::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    if (!Get<bool>(SlotOf("IsOpen"))) return;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;

    ctx.FillRoundRect(box, 4.0f, palette.colors[static_cast<int>(Token::CardBackground)]);
    // Left edge carries the severity. A 4px strip rather than a full tinted
    // background: an error bar tinted edge to edge makes the body text hard to read.
    const Color edge = palette.colors[static_cast<int>(SeverityToken(Get<int>(SlotOf("Severity"))))];
    ctx.FillRoundRect(Rect{box.x, box.y, kInfoBarEdge, box.h}, 2.0f, edge);

    const float textX = box.x + kInfoBarEdge + kInfoBarPadX;
    const float textW = std::max(0.0f, box.w - (textX - box.x) - kInfoBarPadX - 28.0f);
    float y = box.y + kInfoBarPadY;
    const std::wstring title = Get<std::wstring>(SlotOf("Title"));
    const std::wstring message = Get<std::wstring>(SlotOf("Message"));
    const Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    if (!title.empty()) {
        const float lineH = TextSystem::Get() ? TextSystem::Get()->LineHeight(kInfoBarTitleSize)
                                              : kInfoBarTitleSize * 1.33f;
        ctx.DrawText(title, Rect{textX, y, textW, lineH}, fg, kInfoBarTitleSize, WeightSemiBold,
                     TextAlign::Left);
        y += lineH;
    }
    if (!message.empty()) {
        ctx.DrawText(message, Rect{textX, y, textW, box.Bottom() - y},
                     palette.colors[static_cast<int>(Token::TextSecondary)], kInfoBarBodySize,
                     WeightNormal, TextAlign::Left);
    }

    if (Get<bool>(SlotOf("IsClosable"))) {
        const Rect close = CloseBounds();
        ctx.DrawText(L"\u2715", close, palette.colors[static_cast<int>(Token::TextSecondary)],
                     12.0f, WeightNormal, TextAlign::Center);
    }
}

Widget* InfoBar::HitTest(const Point& p) {
    if (!bounds_.Contains(p)) return nullptr;
    return this;
}

void InfoBar::OnPointerUp(const Point& local) {
    if (!Get<bool>(SlotOf("IsClosable"))) return;
    // Only the close affordance closes it. Clicking the message text must not dismiss
    // a warning the user was still reading.
    const Rect close = CloseBounds();
    if (!close.Contains(local)) return;
    // Reports through IsOpen (TwoWay) instead of hiding itself: whether the bar comes
    // back is the ViewModel's decision.
    const int slot = SlotOf("IsOpen");
    if (slot >= 0) SetSlotValue(slot, Value::Bool(false), true);
}

// ---------------------------------------------------------------------------
// InfoBadge
// ---------------------------------------------------------------------------
Size InfoBadge::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    // A dot when there is no text, a pill otherwise. The minimum width keeps a single
    // digit round instead of squashed.
    if (text.empty()) {
        const float d = 8.0f;
        desired_ = Size{d + margin.H(), d + margin.V()};
        ApplySizeConstraints();
        return desired_;
    }
    float textW = static_cast<float>(text.size()) * fontSize * 0.55f;
    if (ITextService* svc = TextSystem::Get()) textW = svc->Measure(text, fontSize, WeightSemiBold).w;
    const float h = std::max(18.0f, fontSize + 8.0f);
    const float w = std::max(h, textW + 10.0f);
    desired_ = Size{w + margin.H(), h + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void InfoBadge::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const Color fill = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    ctx.FillRoundRect(box, std::min(box.w, box.h) * 0.5f, fill);
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    if (text.empty()) return;
    ctx.DrawText(text, box, ResolveColor(Get<Color>(SlotOf("Foreground")), palette),
                 Get<float>(SlotOf("FontSize")), WeightSemiBold, TextAlign::Center);
}

// ---------------------------------------------------------------------------
// Line
// ---------------------------------------------------------------------------
Size Line::Measure(const Size& available) {
    (void)available;
    // The bounding box of the two endpoints, so a diagonal reserves the space it
    // actually covers instead of reporting zero and being laid out as a point.
    const float x0 = Get<float>(SlotOf("StartX"));
    const float y0 = Get<float>(SlotOf("StartY"));
    const float x1 = Get<float>(SlotOf("EndX"));
    const float y1 = Get<float>(SlotOf("EndY"));
    const float stroke = std::max(1.0f, Get<float>(SlotOf("StrokeThickness")));
    // Origin of the coordinate space is the parent's content box, so the box starts at
    // min(x) and the extents are the absolute deltas.
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    desired_ = Size{std::abs(x1 - x0) + stroke + margin.H(),
                    std::abs(y1 - y0) + stroke + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void Line::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    // Endpoints are relative to the parent content box, and this widget's bounds start
    // where the smaller coordinate is. Converting through the bounds origin is what
    // makes a line work without knowing its own layout position.
    const float x0 = Get<float>(SlotOf("StartX"));
    const float y0 = Get<float>(SlotOf("StartY"));
    const float x1 = Get<float>(SlotOf("EndX"));
    const float y1 = Get<float>(SlotOf("EndY"));
    const float minX = std::min(x0, x1);
    const float minY = std::min(y0, y1);
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float stroke = std::max(1.0f, Get<float>(SlotOf("StrokeThickness")));
    // Half the stroke sits on either side of the mathematical segment, so shift by half
    // a stroke to keep a horizontal line inside its box.
    const Point a{bounds_.x + margin.l + (x0 - minX) + stroke * 0.5f,
                  bounds_.y + margin.t + (y0 - minY) + stroke * 0.5f};
    const Point b{bounds_.x + margin.l + (x1 - minX) + stroke * 0.5f,
                  bounds_.y + margin.t + (y1 - minY) + stroke * 0.5f};
    ctx.DrawLine(a, b, ResolveColor(Get<Color>(SlotOf("Stroke")), palette), stroke);
}

// ---------------------------------------------------------------------------
// Shape
// ---------------------------------------------------------------------------
Size Shape::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    // A shape has no content to size to; an unspecified one is a 40x40 placeholder so
    // it is visible in a layout rather than invisible.
    const float w = Get<float>(SlotOf("ShapeWidth"));
    const float h = Get<float>(SlotOf("ShapeHeight"));
    desired_ = Size{(w > 0 ? w : 40.0f) + margin.H(), (h > 0 ? h : 40.0f) + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void Shape::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const Color fill = ResolveColor(Get<Color>(SlotOf("Fill")), palette);
    const bool isEllipse = Get<int>(SlotOf("Kind")) == 1;
    if (isEllipse) ctx.FillEllipse(box, fill);
    else ctx.FillRoundRect(box, Get<float>(SlotOf("CornerRadius")), fill);

    const float stroke = Get<float>(SlotOf("StrokeThickness"));
    if (stroke > 0.0f) {
        const Color strokeColor = ResolveColor(Get<Color>(SlotOf("Stroke")), palette);
        // An ellipse outline is approximated by the fill of a slightly larger ellipse
        // minus the inner one; StrokeRect cannot express it and an outline built from
        // segments would need a second primitive for no benefit here.
        if (isEllipse) {
            ctx.FillEllipse(box, strokeColor);
            ctx.FillEllipse(box.Deflate(Thickness{stroke, stroke, stroke, stroke}), fill);
        } else {
            ctx.StrokeRect(box, strokeColor, stroke, Get<float>(SlotOf("CornerRadius")));
        }
    }
}

// ---------------------------------------------------------------------------
// ToggleButton
// ---------------------------------------------------------------------------
namespace {
constexpr float kToggleButtonPadX = 16.0f;
constexpr float kToggleButtonPadY = 7.0f;
} // namespace

Size ToggleButton::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float textW = static_cast<float>(text.size()) * fontSize * 0.55f;
    float lineH = fontSize * 1.33f;
    if (ITextService* svc = TextSystem::Get()) {
        const Size m = svc->Measure(text, fontSize, WeightNormal);
        textW = m.w;
        lineH = std::max(lineH, m.h);
    }
    desired_ = Size{textW + kToggleButtonPadX * 2 + margin.H(),
                    std::max(lineH, 16.0f) + kToggleButtonPadY * 2 + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void ToggleButton::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool enabled = Get<bool>(SlotOf("IsEnabled"));
    const bool checked = Get<bool>(SlotOf("IsChecked"));

    // Checked uses the accent fill, which is the whole point of a toggle: the state is
    // readable at a glance from a row of buttons. Hover/press only matter unchecked,
    // because a checked button already looks "down".
    Color fill = ResolveColor(Get<Color>(SlotOf("Background")), palette);
    if (fill.a <= 0) {
        fill = palette.colors[static_cast<int>(Token::ControlFill)];
        if (!enabled) {
            fill = palette.colors[static_cast<int>(Token::AppBackground)];
        } else if (checked) {
            fill = palette.colors[static_cast<int>(Token::Accent)];
        } else if (pressed_) {
            fill = palette.colors[static_cast<int>(Token::ControlFillPressed)];
        } else if (hovered_) {
            fill = palette.colors[static_cast<int>(Token::ControlFillHover)];
        }
    }
    const float radius = Get<float>(SlotOf("CornerRadius"));
    ctx.FillRoundRect(box, radius, fill);
    ctx.StrokeRect(box, palette.colors[static_cast<int>(checked ? Token::Accent : Token::CardBorder)],
                   focused_ ? 1.5f : 1.0f, radius);

    Color fg = palette.colors[static_cast<int>(Token::TextPrimary)];
    if (!enabled) fg = palette.colors[static_cast<int>(Token::TextDisabled)];
    else if (checked) fg = palette.colors[static_cast<int>(Token::TextOnAccent)];
    fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    if (!enabled) fg = palette.colors[static_cast<int>(Token::TextDisabled)];
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect textBox{box.x + pad.l, box.y + pad.t,
                       std::max(0.0f, box.w - pad.H()), std::max(0.0f, box.h - pad.V())};
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")), textBox, fg,
                 Get<float>(SlotOf("FontSize")), Get<int>(SlotOf("FontWeight")),
                 TextAlign::Center);
}

Widget* ToggleButton::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr; // disabled = not a hit
    return bounds_.Contains(p) ? this : nullptr;
}

void ToggleButton::OnPointerDown(const Point&) { SetPressed(true); Invalidate(AffectsRender); }

void ToggleButton::OnPointerUp(const Point& local) {
    const bool wasPressed = pressed_;
    SetPressed(false);
    // Only a press that both started and ended inside counts, so dragging off cancels
    // -- the behaviour every button has and users notice immediately when missing.
    if (wasPressed && bounds_.Contains(local)) Toggle();
    Invalidate(AffectsRender);
}

bool ToggleButton::OnKeyDown(int vk) {
    if (vk == KeySpace || vk == KeyReturn) {
        Toggle();
        return true;
    }
    return false;
}

void ToggleButton::Toggle() {
    const int slot = SlotOf("IsChecked");
    if (slot < 0) return;
    // Published as a user edit (fromBinding=true) so a TwoWay binding pushes it to the
    // ViewModel: this is the user changing the value, not us echoing it back.
    SetSlotValue(slot, Value::Bool(!Get<bool>(slot)), true);
}

// ---------------------------------------------------------------------------
// ToggleSwitch
// ---------------------------------------------------------------------------
namespace {
constexpr float kSwitchTrackW = 40.0f;
constexpr float kSwitchTrackH = 20.0f;
constexpr float kSwitchKnobInset = 3.0f;
} // namespace

Size ToggleSwitch::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float textW = 0.0f;
    if (!text.empty()) {
        textW = static_cast<float>(text.size()) * fontSize * 0.55f;
        if (ITextService* svc = TextSystem::Get()) textW = svc->Measure(text, fontSize, WeightNormal).w;
        textW += 10.0f; // gap between label and track
    }
    desired_ = Size{kSwitchTrackW + textW + margin.H(),
                    std::max(kSwitchTrackH, fontSize * 1.33f) + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void ToggleSwitch::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool enabled = Get<bool>(SlotOf("IsEnabled"));
    const bool on = Get<bool>(SlotOf("IsChecked"));

    // Track is vertically centred against the label.
    const Rect track{box.x, box.y + (box.h - kSwitchTrackH) * 0.5f, kSwitchTrackW, kSwitchTrackH};
    Color trackColor = ResolveColor(Get<Color>(SlotOf(on ? "OnColor" : "OffColor")), palette);
    if (trackColor.a <= 0) {
        trackColor = palette.colors[static_cast<int>(on ? Token::Accent : Token::ControlFillPressed)];
        if (!enabled) trackColor = palette.colors[static_cast<int>(Token::ControlFill)];
    }
    ctx.FillRoundRect(track, kSwitchTrackH * 0.5f, trackColor);
    ctx.StrokeRect(track, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f,
                   kSwitchTrackH * 0.5f);

    // The knob's position is the state. Sliding it is what makes a switch read faster
    // than a checkbox at small sizes.
    const float knobD = kSwitchTrackH - kSwitchKnobInset * 2;
    const float knobX = on ? track.x + track.w - knobD - kSwitchKnobInset
                           : track.x + kSwitchKnobInset;
    Color knobColor = ResolveColor(Get<Color>(SlotOf("KnobColor")), palette);
    if (knobColor.a <= 0) {
        knobColor = enabled ? palette.colors[static_cast<int>(Token::CardBackground)]
                            : palette.colors[static_cast<int>(Token::AppBackground)];
    }
    ctx.FillEllipse(Rect{knobX, track.y + kSwitchKnobInset, knobD, knobD}, knobColor);

    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    if (!text.empty()) {
        Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
        if (!enabled) fg = palette.colors[static_cast<int>(Token::TextDisabled)];
        ctx.DrawText(text, Rect{track.Right() + 10.0f, box.y, box.Right() - track.Right() - 10.0f, box.h},
                     fg, Get<float>(SlotOf("FontSize")), WeightNormal, TextAlign::Left);
    }
    if (focused_) {
        ctx.StrokeRect(track.Inflate(2.0f), palette.colors[static_cast<int>(Token::Accent)], 1.0f, 12.0f);
    }
}

Widget* ToggleSwitch::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    return bounds_.Contains(p) ? this : nullptr;
}

void ToggleSwitch::OnPointerUp(const Point& local) {
    if (bounds_.Contains(local)) Toggle();
}

bool ToggleSwitch::OnKeyDown(int vk) {
    if (vk == KeySpace || vk == KeyReturn) {
        Toggle();
        return true;
    }
    return false;
}

void ToggleSwitch::Toggle() {
    const int slot = SlotOf("IsChecked");
    if (slot < 0) return;
    SetSlotValue(slot, Value::Bool(!Get<bool>(slot)), true);
}

// ---------------------------------------------------------------------------
// HyperlinkButton
// ---------------------------------------------------------------------------
Size HyperlinkButton::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float textW = static_cast<float>(text.size()) * fontSize * 0.55f;
    float lineH = fontSize * 1.33f;
    if (ITextService* svc = TextSystem::Get()) {
        const Size m = svc->Measure(text, fontSize, WeightNormal);
        textW = m.w;
        lineH = std::max(lineH, m.h);
    }
    desired_ = Size{textW + margin.H(), lineH + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void HyperlinkButton::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool enabled = Get<bool>(SlotOf("IsEnabled"));
    Color fg = enabled ? palette.colors[static_cast<int>(Token::Accent)]
                       : palette.colors[static_cast<int>(Token::TextDisabled)];
    fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    if (!enabled) fg = palette.colors[static_cast<int>(Token::TextDisabled)];

    const float fontSize = Get<float>(SlotOf("FontSize"));
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")), box, fg, fontSize, WeightNormal,
                 TextAlign::Left);
    // Underline on hover and while focused, not always: a permanent underline makes a
    // page of links look like a wall of rules, and the accent colour already carries
    // the affordance.
    if ((hovered_ || focused_) && enabled) {
        const float lineH = TextSystem::Get() ? TextSystem::Get()->LineHeight(fontSize)
                                              : fontSize * 1.33f;
        float textW = static_cast<float>(Get<std::wstring>(SlotOf("Content")).size()) * fontSize * 0.55f;
        if (ITextService* svc = TextSystem::Get()) {
            textW = svc->Measure(Get<std::wstring>(SlotOf("Content")), fontSize, WeightNormal).w;
        }
        const float y = box.y + (box.h - lineH) * 0.5f + lineH - 1.0f;
        ctx.DrawLine(Point{box.x, y}, Point{box.x + std::min(textW, box.w), y}, fg, 1.0f);
    }
}

Widget* HyperlinkButton::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    return bounds_.Contains(p) ? this : nullptr;
}

void HyperlinkButton::OnPointerDown(const Point&) { SetPressed(true); Invalidate(AffectsRender); }

void HyperlinkButton::OnPointerUp(const Point& local) {
    const bool wasPressed = pressed_;
    SetPressed(false);
    if (wasPressed && bounds_.Contains(local)) Invoke();
    Invalidate(AffectsRender);
}

void HyperlinkButton::OnPointerEnter() {
    hovered_ = true;
    Invalidate(AffectsRender);
}

void HyperlinkButton::OnPointerLeave() {
    hovered_ = false;
    SetPressed(false);
    Invalidate(AffectsRender);
}

void HyperlinkButton::Invoke() {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    // Same contract as Button: the command is optional, and a link with no command is
    // legal (it still looks and focuses like one).
    ICommand* cmd = static_cast<ICommand*>(Get<RefPtr>(SlotOf("Command")).get());
    if (cmd != nullptr && cmd->CanExecute()) cmd->Execute();
}

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------
namespace {
constexpr float kSliderHeight = 28.0f;
constexpr float kSliderTrackH = 4.0f;
constexpr float kSliderThumbD = 14.0f;
constexpr float kSliderPadX = 8.0f;
} // namespace

Rect Slider::Track() const {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float w = std::max(0.0f, bounds_.w - margin.H() - kSliderPadX * 2);
    return Rect{bounds_.x + margin.l + kSliderPadX,
                bounds_.y + margin.t + (bounds_.h - margin.V()) * 0.5f - kSliderTrackH * 0.5f, w,
                kSliderTrackH};
}

float Slider::Fraction() const {
    const float minV = Get<float>(SlotOf("Minimum"));
    const float maxV = Get<float>(SlotOf("Maximum"));
    // A zero-width range is treated as "at minimum" rather than dividing by zero.
    if (maxV <= minV) return 0.0f;
    const float v = Get<float>(SlotOf("Value"));
    return std::clamp((v - minV) / (maxV - minV), 0.0f, 1.0f);
}

Size Slider::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    // Fills the width it is given, like a progress bar: a slider sized to nothing is
    // not adjustable.
    const float w = available.w > 0 && available.w < 1.0e6f ? available.w : 200.0f;
    desired_ = Size{w, std::max(kSliderHeight, kSliderThumbD + 4.0f) + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void Slider::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Rect track = Track();
    if (track.w <= 0) return;
    ctx.FillRoundRect(track, kSliderTrackH * 0.5f,
                      palette.colors[static_cast<int>(Token::ControlFillPressed)]);

    // The filled portion up to the thumb is what makes the value readable without
    // doing arithmetic on the thumb position.
    const float fraction = Fraction();
    if (fraction > 0.0f) {
        ctx.FillRoundRect(Rect{track.x, track.y, track.w * fraction, track.h},
                          kSliderTrackH * 0.5f, palette.colors[static_cast<int>(Token::Accent)]);
    }

    const float cx = track.x + track.w * fraction;
    const float cy = track.y + track.h * 0.5f;
    const Color thumb = Get<bool>(SlotOf("IsEnabled"))
                            ? palette.colors[static_cast<int>(Token::Accent)]
                            : palette.colors[static_cast<int>(Token::TextDisabled)];
    // A ring around the thumb: the accent fill alone disappears against the filled
    // track it sits on.
    ctx.FillEllipse(Rect{cx - kSliderThumbD * 0.5f, cy - kSliderThumbD * 0.5f, kSliderThumbD,
                         kSliderThumbD},
                    palette.colors[static_cast<int>(Token::CardBackground)]);
    ctx.FillEllipse(Rect{cx - kSliderThumbD * 0.5f + 2.0f, cy - kSliderThumbD * 0.5f + 2.0f,
                         kSliderThumbD - 4.0f, kSliderThumbD - 4.0f},
                    thumb);
    if (focused_) {
        ctx.StrokeRect(Rect{cx - kSliderThumbD * 0.5f - 2.0f, cy - kSliderThumbD * 0.5f - 2.0f,
                            kSliderThumbD + 4.0f, kSliderThumbD + 4.0f},
                       palette.colors[static_cast<int>(Token::Accent)], 1.0f, 12.0f);
    }
}

Widget* Slider::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    // The whole row is grabbable, not just the 4px track: hunting for a 14px thumb with
    // the mouse is the most common complaint about slider implementations.
    return bounds_.Contains(p) ? this : nullptr;
}

void Slider::SetFromX(float x) {
    const Rect track = Track();
    if (track.w <= 0) return;
    const float fraction = std::clamp((x - track.x) / track.w, 0.0f, 1.0f);
    const float minV = Get<float>(SlotOf("Minimum"));
    const float maxV = Get<float>(SlotOf("Maximum"));
    float value = minV + (maxV - minV) * fraction;
    // Snap to StepFrequency when one is given, so a slider bound to a frequency list
    // cannot produce 3.7841.
    const float step = Get<float>(SlotOf("StepFrequency"));
    if (step > 0.0f) value = minV + std::round((value - minV) / step) * step;
    value = std::clamp(value, minV, maxV);
    // fromBinding=true: this is the user moving the control, so a TwoWay binding must
    // push it outward rather than have us echo it back.
    SetSlotValue(SlotOf("Value"), Value::Number(value), true);
}

void Slider::OnPointerDown(const Point& local) {
    dragging_ = true;
    SetFromX(local.x);
}

void Slider::OnPointerMove(const Point& local) {
    if (!dragging_) return;
    SetFromX(local.x);
}

void Slider::OnPointerUp(const Point& local) {
    if (dragging_) SetFromX(local.x);
    dragging_ = false;
}

bool Slider::OnKeyDown(int vk) {
    const float step = Get<float>(SlotOf("StepFrequency")) > 0
                           ? Get<float>(SlotOf("StepFrequency"))
                           : std::max(1.0f, (Get<float>(SlotOf("Maximum")) - Get<float>(SlotOf("Minimum"))) / 20.0f);
    if (vk == KeyLeft) {
        Step(-1);
        return true;
    }
    if (vk == KeyRight) {
        Step(1);
        return true;
    }
    if (vk == KeyHome) {
        SetSlotValue(SlotOf("Value"), Value::Number(Get<float>(SlotOf("Minimum"))), true);
        return true;
    }
    if (vk == KeyEnd) {
        SetSlotValue(SlotOf("Value"), Value::Number(Get<float>(SlotOf("Maximum"))), true);
        return true;
    }
    (void)step;
    return false;
}

void Slider::Step(int direction) {
    const float step = Get<float>(SlotOf("StepFrequency")) > 0
                           ? Get<float>(SlotOf("StepFrequency"))
                           : std::max(1.0f, (Get<float>(SlotOf("Maximum")) - Get<float>(SlotOf("Minimum"))) / 20.0f);
    const float value = Get<float>(SlotOf("Value")) + step * static_cast<float>(direction);
    SetSlotValue(SlotOf("Value"),
                 Value::Number(std::clamp(value, Get<float>(SlotOf("Minimum")),
                                          Get<float>(SlotOf("Maximum")))),
                 true);
}

// ---------------------------------------------------------------------------
// RepeatButton
// ---------------------------------------------------------------------------
Size RepeatButton::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float textW = static_cast<float>(text.size()) * fontSize * 0.55f;
    float lineH = fontSize * 1.33f;
    if (ITextService* svc = TextSystem::Get()) {
        const Size m = svc->Measure(text, fontSize, WeightNormal);
        textW = m.w;
        lineH = std::max(lineH, m.h);
    }
    desired_ = Size{textW + 32.0f + margin.H(), std::max(lineH, 16.0f) + 14.0f + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void RepeatButton::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool enabled = Get<bool>(SlotOf("IsEnabled"));
    Color fill = palette.colors[static_cast<int>(Token::ControlFill)];
    if (!enabled) fill = palette.colors[static_cast<int>(Token::AppBackground)];
    else if (pressed_) fill = palette.colors[static_cast<int>(Token::ControlFillPressed)];
    else if (hovered_) fill = palette.colors[static_cast<int>(Token::ControlFillHover)];
    ctx.FillRoundRect(box, 4.0f, fill);
    ctx.StrokeRect(box, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f, 4.0f);
    Color fg = palette.colors[static_cast<int>(enabled ? Token::TextPrimary : Token::TextDisabled)];
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")), box, fg, Get<float>(SlotOf("FontSize")),
                 WeightNormal, TextAlign::Center);
    if (focused_) {
        ctx.StrokeRect(box.Inflate(1.0f), palette.colors[static_cast<int>(Token::Accent)], 1.0f, 4.0f);
    }
}

Widget* RepeatButton::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    return bounds_.Contains(p) ? this : nullptr;
}

void RepeatButton::OnPointerDown(const Point&) {
    SetPressed(true);
    // Fires on press, not release: a repeat button is for stepping a value, and waiting
    // for release makes holding pointless.
    Invoke();
    Invalidate(AffectsRender);
}

void RepeatButton::OnPointerUp(const Point&) {
    SetPressed(false);
    Invalidate(AffectsRender);
}

void RepeatButton::Invoke() {
    if (!Get<bool>(SlotOf("IsEnabled"))) return;
    ICommand* cmd = static_cast<ICommand*>(Get<RefPtr>(SlotOf("Command")).get());
    if (cmd != nullptr && cmd->CanExecute()) cmd->Execute();
}


// ---------------------------------------------------------------------------
// PasswordBox
// ---------------------------------------------------------------------------
std::wstring PasswordBox::Mask(size_t count) {
    // U+2022 BULLET: present in every UI font, and unlike '*' it sits centred on the line
    // instead of raised, so a masked field does not look like it has a different baseline.
    return std::wstring(count, L'\u2022');
}

std::wstring PasswordBox::DisplayText() const {
    return Mask(Get<std::wstring>(SlotOf("Text")).size());
}

void PasswordBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    // Everything except the character source is TextBox's: the frame, padding, focus
    // ring, clipping and caret all stay identical by construction. Only DisplayText
    // differs, which is why masking is one override rather than a parallel control.
    TextBox::OnPaint(ctx, palette);
}

// ---------------------------------------------------------------------------
// NumberBox
// ---------------------------------------------------------------------------
bool NumberBox::ParseNumber(const std::wstring& text, double& out) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    // The WHOLE string must be consumed: wcstod happily parses "12abc" as 12, which
    // would silently accept a typo as a valid number.
    if (end == nullptr || *end != L'\0') return false;
    out = value;
    return true;
}

void NumberBox::Revalidate() {
    double parsed = 0;
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    const bool ok = ParseNumber(text, parsed);
    const int slotValid = SlotOf("IsValid");
    if (slotValid >= 0) {
        // Range is checked too, but an out-of-range number is still a NUMBER: the value
        // is published either way so a ViewModel can clamp it, while IsValid reports that
        // it needs attention.
        const bool inRange = ok && parsed >= Get<float>(SlotOf("Minimum")) &&
                             parsed <= Get<float>(SlotOf("Maximum"));
        if (Get<bool>(slotValid) != inRange) SetSlotValue(slotValid, Value::Bool(inRange));
    }
    const int slotValue = SlotOf("Value");
    if (slotValue >= 0 && ok) SetSlotValue(slotValue, Value::Number(parsed));
    Invalidate(AffectsRender);
}

void NumberBox::OnSlotChanged(int slot, const Value& value) {
    TextBox::OnSlotChanged(slot, value);
    // Recompute whenever Text changes, including when a BINDING pushes a value in. Not
    // only on keystrokes: a ViewModel setting Text="abc" must also mark it invalid.
    if (slot == SlotOf("Text")) Revalidate();
}

Size NumberBox::Measure(const Size& available) {
    // Same frame as a text box; the narrow default comes from MaxWidth in the type table
    // rather than from a special case here.
    return TextBox::Measure(available);
}

void NumberBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    TextBox::OnPaint(ctx, palette);
    // An invalid entry gets an accent border, because IsValid alone is invisible: the
    // whole point of reporting validity is that the user can see it.
    if (Get<bool>(SlotOf("IsValid"))) return;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t, bounds_.w - margin.H(),
                   bounds_.h - margin.V()};
    if (box.w <= 0 || box.h <= 0) return;
    ctx.StrokeRect(box, palette.colors[static_cast<int>(Token::Accent)], 1.5f, 4.0f);
}

// ---------------------------------------------------------------------------
// RadioButton
// ---------------------------------------------------------------------------
Size RadioButton::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring text = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float textW = static_cast<float>(text.size()) * fontSize * 0.55f;
    float lineH = fontSize * 1.33f;
    if (ITextService* svc = TextSystem::Get()) {
        const Size m = svc->Measure(text, fontSize, WeightNormal);
        textW = m.w;
        lineH = std::max(lineH, m.h);
    }
    desired_ = Size{24.0f + textW + margin.H(), std::max(lineH, 20.0f) + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void RadioButton::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool checked = Get<bool>(SlotOf("IsChecked"));
    const float size = 16.0f;
    const Rect ring{box.x + 2.0f, box.y + (box.h - size) * 0.5f, size, size};
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];

    // A ring plus a filled centre dot: the checked state has to be distinguishable from
    // a checkbox's tick at a glance, which a rounded square would not be.
    ctx.FillEllipse(ring, palette.colors[static_cast<int>(Token::ControlFill)]);
    ctx.StrokeRect(ring, palette.colors[static_cast<int>(checked ? Token::Accent : Token::TextSecondary)],
                   checked ? 2.0f : 1.0f, size * 0.5f);
    if (checked) {
        const float dot = size - 8.0f;
        ctx.FillEllipse(Rect{ring.x + 4.0f, ring.y + 4.0f, dot, dot}, accent);
    }

    const Color fg = ResolveColor(Get<Color>(SlotOf("Foreground")), palette);
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")),
                 Rect{ring.Right() + 8.0f, box.y, box.Right() - ring.Right() - 8.0f, box.h}, fg,
                 Get<float>(SlotOf("FontSize")), WeightNormal, TextAlign::Left);
    if (focused_) {
        ctx.StrokeRect(box.Inflate(1.0f), accent, 1.0f, 4.0f);
    }
}

Widget* RadioButton::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    return bounds_.Contains(p) ? this : nullptr;
}

void RadioButton::OnPointerUp(const Point& local) {
    if (bounds_.Contains(local)) Toggle();
}

bool RadioButton::OnKeyDown(int vk) {
    if (vk == KeySpace) {
        Toggle();
        return true;
    }
    return false;
}

void RadioButton::Toggle() {
    // Clicking an already-checked radio does NOT uncheck it: a radio group always has a
    // selection, which is the whole difference from a checkbox.
    Select();
}

void RadioButton::Select() {
    const int slot = SlotOf("IsChecked");
    if (slot < 0) return;
    SetSlotValue(slot, Value::Bool(true), true);

    // Clear the rest of the group. Walking the whole tree rather than the parent is
    // deliberate: radios in one group are routinely spread across panels.
    const std::wstring group = Get<std::wstring>(SlotOf("GroupName"));
    Widget* root = Root();
    if (root == nullptr) return;
    std::vector<RadioButton*> peers;
    std::function<void(Widget*)> collect = [&](Widget* w) {
        if (w == nullptr) return;
        if (auto* radio = dynamic_cast<RadioButton*>(w)) {
            if (radio != this && radio->Get<std::wstring>(radio->SlotOf("GroupName")) == group) {
                peers.push_back(radio);
            }
        }
        for (auto& c : w->Children()) collect(c.get());
    };
    collect(root);
    for (RadioButton* peer : peers) {
        const int peerSlot = peer->SlotOf("IsChecked");
        if (peerSlot >= 0 && peer->Get<bool>(peerSlot)) {
            peer->SetSlotValue(peerSlot, Value::Bool(false), true);
        }
    }
}

// ---------------------------------------------------------------------------
// RelativePanel
// ---------------------------------------------------------------------------
namespace {
constexpr float kRelativePad = 12.0f;
} // namespace

size_t RelativePanel::IndexOfName(const std::string& name) const {
    if (name.empty()) return static_cast<size_t>(-1);
    for (size_t i = 0; i < children_.size(); ++i) {
        if (children_[i] && children_[i]->Name() == name) return i;
    }
    return static_cast<size_t>(-1);
}

Size RelativePanel::MeasureChild(size_t index, const Size& available) {
    if (index >= children_.size() || !children_[index]) return Size{0, 0};
    return children_[index]->Measure(available);
}

Size RelativePanel::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float innerW = std::max(0.0f, available.w - margin.H() - pad.H());
    measured_.assign(children_.size(), Size{0, 0});
    placed_.assign(children_.size(), Rect{});
    done_.assign(children_.size(), false);

    // Measure every child with the available space, then place them to find the extent.
    // Placement is needed during Measure because a panel's desired size depends on where
    // its children landed -- RelativePanel is unique in that respect.
    for (size_t i = 0; i < children_.size(); ++i) {
        measured_[i] = MeasureChild(i, Size{innerW, available.h});
    }
    std::fill(done_.begin(), done_.end(), false);
    float maxX = 0.0f;
    float maxY = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        const Rect r = PlaceChild(i, 0);
        placed_[i] = r;
        maxX = std::max(maxX, r.Right());
        maxY = std::max(maxY, r.Bottom());
    }
    desired_ = Size{maxX + pad.H() + margin.H(), maxY + pad.V() + margin.V()};
    // Fill the width it is given: a panel anchored inside a card should span the card.
    if (available.w > 0 && available.w < 1.0e6f) desired_.w = available.w;
    ApplySizeConstraints();
    return desired_;
}

Rect RelativePanel::PlaceChild(size_t index, int depth) {
    if (index >= children_.size() || !children_[index]) return Rect{};
    if (done_[index]) return placed_[index];
    // A cycle (A right-of B, B right-of A) would recurse forever. Past a small depth the
    // child is placed at the origin and left there: a wrong position is a bug the author
    // can see, an infinite loop is one they cannot diagnose.
    if (depth > 8) {
        done_[index] = true;
        placed_[index] = Rect{0, 0, measured_[index].w, measured_[index].h};
        return placed_[index];
    }
    Widget* self = children_[index].get();
    const Size size = measured_[index];
    float x = 0.0f;
    float y = 0.0f;
    auto slot = [&](const char* name) { return self->SlotOf(name); };

    // LeftOf / RightOf establish x; Above / Below establish y. Align* overrides the other
    // axis. An unknown anchor name is ignored rather than fatal: the panel keeps a usable
    // position and the validator has already reported the typo.
    // WideToUtf8, not a raw iterator range: copying wchar_t into char silently
    // truncates non-ASCII names (C4244) and breaks Chinese x:Name anchors.
    const std::string rightOf = WideToUtf8(self->Get<std::wstring>(slot("RelativePanel.RightOf")));
    const std::string leftOf = WideToUtf8(self->Get<std::wstring>(slot("RelativePanel.LeftOf")));
    const std::string below = WideToUtf8(self->Get<std::wstring>(slot("RelativePanel.Below")));
    const std::string above = WideToUtf8(self->Get<std::wstring>(slot("RelativePanel.Above")));

    const size_t kNoAnchor = static_cast<size_t>(-1);
    size_t other = IndexOfName(rightOf);
    if (other != kNoAnchor) {
        const Rect r = PlaceChild(other, depth + 1);
        done_[other] = true;
        x = r.Right() + kRelativePad;
        y = r.y;
    } else if ((other = IndexOfName(leftOf)) != kNoAnchor) {
        const Rect r = PlaceChild(other, depth + 1);
        done_[other] = true;
        x = r.x - size.w - kRelativePad;
        y = r.y;
    }
    other = IndexOfName(below);
    if (other != kNoAnchor) {
        const Rect r = PlaceChild(other, depth + 1);
        done_[other] = true;
        y = r.Bottom() + kRelativePad;
        if (rightOf.empty() && leftOf.empty()) x = r.x;
    } else if ((other = IndexOfName(above)) != kNoAnchor) {
        const Rect r = PlaceChild(other, depth + 1);
        done_[other] = true;
        y = r.y - size.h - kRelativePad;
        if (rightOf.empty() && leftOf.empty()) x = r.x;
    }

    if (self->Get<bool>(slot("RelativePanel.AlignRightWithPanel"))) {
        x = std::max(0.0f, bounds_.w - size.w);
    } else if (self->Get<bool>(slot("RelativePanel.AlignHorizontalCenterWithPanel"))) {
        x = std::max(0.0f, (bounds_.w - size.w) * 0.5f);
    }
    if (self->Get<bool>(slot("RelativePanel.AlignBottomWithPanel"))) {
        y = std::max(0.0f, bounds_.h - size.h);
    } else if (self->Get<bool>(slot("RelativePanel.AlignVerticalCenterWithPanel"))) {
        y = std::max(0.0f, (bounds_.h - size.h) * 0.5f);
    }

    placed_[index] = Rect{x, y, size.w, size.h};
    done_[index] = true;
    return placed_[index];
}

void RelativePanel::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect content{finalRect.x + margin.l + pad.l, finalRect.y + margin.t + pad.t,
                       std::max(0.0f, finalRect.w - margin.H() - pad.H()),
                       std::max(0.0f, finalRect.h - margin.V() - pad.V())};
    // Re-place against the FINAL size: the panel fills its width, so AlignRight anchors
    // computed during Measure (at a possibly smaller width) would otherwise sit in the
    // wrong place.
    bounds_ = content;
    std::fill(done_.begin(), done_.end(), false);
    for (size_t i = 0; i < children_.size(); ++i) {
        const Rect r = PlaceChild(i, 0);
        if (!children_[i]) continue;
        children_[i]->Arrange(Rect{content.x + r.x, content.y + r.y, r.w, r.h});
    }
}

void RelativePanel::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    (void)ctx;
    (void)palette;
    // A panel is transparent: drawing a fill here would hide whatever it is placed on.
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------
namespace {
// The placeholder is a light grey box with a diagonal: recognisably "no picture" rather
// than an invisible hole, and it needs no asset.
constexpr float kImagePlaceholder = 48.0f;
} // namespace

void Image::EnsureDecoded() {
    const std::wstring source = Get<std::wstring>(SlotOf("Source"));
    if (source == decodedSource_ && bitmap_) return;
    decodedSource_ = source;
    bitmap_.reset();
    if (source.empty()) return;
    IImageService* service = ImageService();
    if (service == nullptr) return;
    // Source is a UTF-8 string on the wire; narrow it once here rather than in the
    // service, which deals only in paths-and-bytes. WideToUtf8, not a raw iterator
    // range: the latter truncates every non-ASCII path (and warns on C4244).
    const std::string narrow = WideToUtf8(source);
    bitmap_ = service->Load(narrow);
}

void Image::OnSlotChanged(int slot, const Value& value) {
    Widget::OnSlotChanged(slot, value);
    if (slot == SlotOf("Source")) {
        // Drop the cache so the next paint re-decodes. Doing it here rather than in paint
        // means a ViewModel that swaps the Source gets a fresh picture on the next frame.
        decodedSource_.clear();
        bitmap_.reset();
    }
}

Size Image::Measure(const Size& available) {
    (void)available;
    EnsureDecoded();
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float w = Get<float>(SlotOf("Width"));
    const float h = Get<float>(SlotOf("Height"));
    // An explicit Width/Height wins (ApplySizeConstraints also honours it, but the natural
    // size has to be right first, or a missing image would report the placeholder size).
    float naturalW = kImagePlaceholder;
    float naturalH = kImagePlaceholder;
    if (bitmap_ && bitmap_->Valid()) {
        naturalW = static_cast<float>(bitmap_->width);
        naturalH = static_cast<float>(bitmap_->height);
        // Stretch fills its box; the default keeps the aspect ratio, because a squashed
        // photo is the most visible layout bug a picture can have.
        if (Get<int>(SlotOf("Stretch")) == 0 && w > 0) {
            naturalH = w * naturalH / naturalW;
            naturalW = w;
        } else if (Get<int>(SlotOf("Stretch")) == 0 && h > 0) {
            naturalW = h * naturalW / naturalH;
            naturalH = h;
        }
    }
    desired_ = Size{(w > 0 ? w : naturalW) + margin.H(), (h > 0 ? h : naturalH) + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void Image::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    EnsureDecoded();
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    if (bitmap_ && bitmap_->Valid()) {
        ctx.DrawBitmap(bitmap_.get(), box);
        return;
    }
    // Placeholder: a muted fill with a diagonal, so an unloaded image is visible while
    // being obviously not the picture.
    const Color fill = palette.colors[static_cast<int>(Token::ControlFill)];
    ctx.FillRoundRect(box, 4.0f, fill);
    ctx.StrokeRect(box, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f, 4.0f);
    ctx.DrawLine(Point{box.x + 4.0f, box.Bottom() - 4.0f},
                 Point{box.Right() - 4.0f, box.y + 4.0f},
                 palette.colors[static_cast<int>(Token::TextDisabled)], 1.0f);
}

// ---------------------------------------------------------------------------
// PersonPicture
// ---------------------------------------------------------------------------
std::wstring PersonPicture::Initials(const std::wstring& displayName) {
    std::wstring out;
    size_t i = 0;
    while (i < displayName.size() && out.size() < 2) {
        while (i < displayName.size() && displayName[i] == L' ') ++i;
        if (i >= displayName.size()) break;
        wchar_t ch = displayName[i];
        // Upper-case via towupper so a non-ASCII name gets a sensible glyph instead of a
        // raw byte.
        out.push_back(static_cast<wchar_t>(std::towupper(ch)));
        while (i < displayName.size() && displayName[i] != L' ') ++i;
    }
    if (out.empty()) out = L"?";
    return out;
}

void PersonPicture::EnsureDecoded() {
    const std::wstring source = Get<std::wstring>(SlotOf("Source"));
    if (source == decodedSource_ && bitmap_) return;
    decodedSource_ = source;
    bitmap_.reset();
    if (source.empty()) return;
    IImageService* service = ImageService();
    if (service == nullptr) return;
    const std::string narrow = WideToUtf8(source);
    bitmap_ = service->Load(narrow);
}

void PersonPicture::OnSlotChanged(int slot, const Value& value) {
    Widget::OnSlotChanged(slot, value);
    if (slot == SlotOf("Source")) {
        decodedSource_.clear();
        bitmap_.reset();
    }
}

Size PersonPicture::Measure(const Size& available) {
    (void)available;
    EnsureDecoded();
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float d = Get<float>(SlotOf("Diameter"));
    const float side = d > 0 ? d : 40.0f;
    desired_ = Size{side + margin.H(), side + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void PersonPicture::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    EnsureDecoded();
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float d = std::min(std::max(0.0f, bounds_.w - margin.H()),
                             std::max(0.0f, bounds_.h - margin.V()));
    if (d <= 0) return;
    const Rect circle{bounds_.x + margin.l, bounds_.y + margin.t, d, d};
    if (bitmap_ && bitmap_->Valid()) {
        // The circle IS the control: passing the crop rect tells the backend to mask the
        // bitmap to an ellipse rather than clip to a square.
        ctx.DrawBitmap(bitmap_.get(), circle, &circle);
        return;
    }
    ctx.FillEllipse(circle, palette.colors[static_cast<int>(Token::Accent)]);
    const std::wstring initials = Initials(Get<std::wstring>(SlotOf("DisplayName")));
    ctx.DrawText(initials, circle, palette.colors[static_cast<int>(Token::TextOnAccent)],
                 d * 0.4f, WeightSemiBold, TextAlign::Center);
}

// ---------------------------------------------------------------------------
// PopupBase
//
// The shared half of every "opens a floating card" control. Both Flyout and ComboBox used
// to carry their own copy of this, including the flag that drifted out of sync and left the
// control permanently unopenable (CODE_PLAN §7.25). It lives here once now.
// ---------------------------------------------------------------------------
namespace {
constexpr float kPopupPadX = 14.0f;
constexpr float kPopupPadY = 7.0f;
} // namespace

bool PopupBase::IsOpen() const {
    const int slot = SlotOf("IsOpen");
    return slot >= 0 && Get<bool>(slot);
}

Point PopupBase::AnchorPoint() const {
    // bounds_ is layout geometry; the page may be scrolled. The overlay lives in
    // window space, so convert before handing it the anchor.
    return ToWindow(Point{bounds_.x, bounds_.y + bounds_.h});
}

Widget* PopupBase::HitTest(const Point& p) {
    if (!Get<bool>(SlotOf("IsEnabled"))) return nullptr;
    return bounds_.Contains(p) ? this : nullptr;
}

void PopupBase::OnPointerEnter() {
    hovered_ = true;
    Invalidate(AffectsRender);
}

void PopupBase::OnPointerLeave() {
    hovered_ = false;
    SetPressed(false);
    Invalidate(AffectsRender);
}

void PopupBase::OnSlotChanged(int slot, const Value& value) {
    Widget::OnSlotChanged(slot, value);
    if (slot != SlotOf("IsOpen")) return;
    bool open = false;
    if (!ValueCodec<bool>::From(value, open)) return;
    // EVERY close path converges here: a click on the control, the layer dismissing itself,
    // and a ViewModel writing the binding. Keeping ownLayer_ in this one place is what makes
    // "close then reopen" work; each path clearing its own copy is how it broke before.
    if (open && !ownLayer_) {
        // A ViewModel set IsOpen=true. Ask the subclass what belongs in the card; if it has
        // nothing to say the write is ignored rather than opening an empty popup.
        OpenWith(BuildContentForSlotOpen(), PopupSize());
        return;
    }
    if (!open && ownLayer_) {
        ownLayer_ = false;
        OnPopupClosed();
        Invalidate(AffectsRender);
    }
}

void PopupBase::OpenWith(WidgetPtr content, Size size, int placement) {
    if (ownLayer_) return;
    Widget* root = Root();
    if (root == nullptr || root == this) return;

    // A reopen arrives with no freshly extracted child (the tree left the control
    // on the first open). Reuse the saved one instead of showing an empty card.
    if (content == nullptr) content = savedContent_;
    if (content == nullptr) content = CreateWidget("wui/StackPanel");
    savedContent_ = content;
    if (layer_ != nullptr) layer_->DetachContent();

    FlyoutRequest request;
    request.content = content.get();  // borrowed; savedContent_ owns it
    request.anchor = AnchorPoint();
    request.size = size.w > 0.0f || size.h > 0.0f ? size : PopupSize();
    request.placement = placement;
    request.lightDismiss = true;
    request.onClosed = [self = this] {
        // The layer closed itself (click outside, Escape). Report through the same slot the
        // control uses, so its appearance and any TwoWay binding agree with reality.
        self->ownLayer_ = false;
        if (self->Root() != nullptr && self->Root()->Overlay() != nullptr) {
            self->Root()->SetOverlay(nullptr);
        }
        self->OnPopupClosed();
        const int slot = self->SlotOf("IsOpen");
        if (slot >= 0) self->SetSlotValue(slot, Value::Bool(false), true);
        self->Invalidate(AffectsRender);
    };

    auto layer = MakeFlyoutLayer(std::move(request));
    ownLayer_ = true;
    layer_ = layer.get();
    root->SetOverlay(layer.release());
    const int slot = SlotOf("IsOpen");
    if (slot >= 0 && !Get<bool>(slot)) SetSlotValue(slot, Value::Bool(true), true);
    Invalidate(AffectsRender);
}

void PopupBase::ClosePopup() {
    if (!ownLayer_) return;
    ownLayer_ = false;
    OnPopupClosed();
    // Detach the layer here, not just lower the flag. Relying on the host to notice an
    // inactive layer meant the overlay stayed installed until the next input event, so an
    // immediate reopen (type two characters in a suggest box) installed nothing while the
    // stale card was still on screen.
    //
    // The comment this replaces worried about freeing the layer inside its own event handler.
    // That concern is real but belongs to the layer's own dismissal path (onClosed, which
    // clears the overlay itself); this function is only ever called from OUR code, where
    // returning into a destroyed layer is impossible.
    if (Widget* root = Root()) {
        if (root->Overlay() != nullptr) root->SetOverlay(nullptr);
    }
    const int slot = SlotOf("IsOpen");
    if (slot >= 0 && Get<bool>(slot)) SetSlotValue(slot, Value::Bool(false), true);
    Invalidate(AffectsRender);
}

// ---------------------------------------------------------------------------
// Popup -- the bare primitive
// ---------------------------------------------------------------------------
Size Popup::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring label = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float textW = static_cast<float>(label.size()) * fontSize * 0.55f;
    float lineH = fontSize * 1.33f;
    if (ITextService* svc = TextSystem::Get()) {
        const Size m = svc->Measure(label, fontSize, WeightNormal);
        textW = m.w;
        lineH = std::max(lineH, m.h);
    }
    // Same shape as the flyout button: a label plus a caret, so the two read as the same
    // kind of affordance.
    desired_ = Size{textW + kPopupPadX * 2 + 14.0f + margin.H(),
                    std::max(lineH, 16.0f) + kPopupPadY * 2 + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void Popup::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool open = IsOpen();
    Color fill = palette.colors[static_cast<int>(Token::ControlFill)];
    if (Pressed()) fill = palette.colors[static_cast<int>(Token::ControlFillPressed)];
    else if (open || Hovered()) fill = palette.colors[static_cast<int>(Token::ControlFillHover)];
    ctx.FillRoundRect(box, 4.0f, fill);
    ctx.StrokeRect(box, palette.colors[static_cast<int>(open ? Token::Accent : Token::CardBorder)],
                   open ? 1.5f : 1.0f, 4.0f);

    const float fontSize = Get<float>(SlotOf("FontSize"));
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")),
                 Rect{box.x + kPopupPadX, box.y, box.w - kPopupPadX * 2 - 12.0f, box.h},
                 palette.colors[static_cast<int>(Token::TextPrimary)], fontSize, WeightNormal,
                 TextAlign::Left);
    ctx.DrawText(open ? L"\u25B2" : L"\u25BC", Rect{box.Right() - 16.0f, box.y, 12.0f, box.h},
                 palette.colors[static_cast<int>(Token::TextSecondary)], 9.0f, WeightNormal,
                 TextAlign::Center);
    if (Focused()) {
        ctx.StrokeRect(box.Inflate(1.0f), palette.colors[static_cast<int>(Token::Accent)], 1.0f,
                       4.0f);
    }
}

WidgetPtr Popup::BuildContentForSlotOpen() {
    // Same extract-the-declared-child rule as the click path, so binding IsOpen and clicking
    // end up opening the same thing.
    if (Children().empty()) return nullptr;
    WidgetPtr content = Children().front();
    ClearChildren();
    return content;
}

void Popup::OnPointerDown(const Point&) { SetPressed(true); }

void Popup::OnPointerUp(const Point& local) {
    SetPressed(false);
    // A toggle only needs a release inside: a synthetic click or a keyboard activation
    // arrives without a matching press and must still work.
    if (!bounds_.Contains(local)) return;
    if (IsOpen()) {
        ClosePopup();
        return;
    }
    // The declared children ARE the card. One extraction path for clicks and for
    // ViewModel-driven opens; on a reopen there is no child left, and OpenWith
    // reuses the one the control kept alive.
    OpenWith(BuildContentForSlotOpen());
}

bool Popup::OnKeyDown(int vk) {
    if (vk == KeySpace || vk == KeyReturn) {
        if (IsOpen()) ClosePopup();
        else OnPointerUp(Point{bounds_.x + 1.0f, bounds_.y + 1.0f});
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Flyout
// ---------------------------------------------------------------------------

Size Flyout::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring label = Get<std::wstring>(SlotOf("Content"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    float textW = static_cast<float>(label.size()) * fontSize * 0.55f;
    float lineH = fontSize * 1.33f;
    if (ITextService* svc = TextSystem::Get()) {
        const Size m = svc->Measure(label, fontSize, WeightNormal);
        textW = m.w;
        lineH = std::max(lineH, m.h);
    }
    // A caret is drawn after the label, so the button is wide enough for both.
    desired_ = Size{textW + kPopupPadX * 2 + 14.0f + margin.H(),
                    std::max(lineH, 16.0f) + kPopupPadY * 2 + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void Flyout::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool open = Get<bool>(SlotOf("IsOpen"));
    Color fill = palette.colors[static_cast<int>(Token::ControlFill)];
    if (pressed_) fill = palette.colors[static_cast<int>(Token::ControlFillPressed)];
    else if (open || hovered_) fill = palette.colors[static_cast<int>(Token::ControlFillHover)];
    ctx.FillRoundRect(box, 4.0f, fill);
    ctx.StrokeRect(box, palette.colors[static_cast<int>(open ? Token::Accent : Token::CardBorder)],
                   open ? 1.5f : 1.0f, 4.0f);

    const float fontSize = Get<float>(SlotOf("FontSize"));
    ctx.DrawText(Get<std::wstring>(SlotOf("Content")),
                 Rect{box.x + kPopupPadX, box.y, box.w - kPopupPadX * 2 - 12.0f, box.h},
                 palette.colors[static_cast<int>(Token::TextPrimary)], fontSize, WeightNormal,
                 TextAlign::Left);
    // A caret, flipped while open: the button has to say which way it is pointing or the
    // user cannot tell whether the popup is already showing.
    ctx.DrawText(open ? L"\u25B2" : L"\u25BC",
                 Rect{box.Right() - 16.0f, box.y, 12.0f, box.h},
                 palette.colors[static_cast<int>(Token::TextSecondary)], 9.0f, WeightNormal,
                 TextAlign::Center);
    if (focused_) {
        ctx.StrokeRect(box.Inflate(1.0f), palette.colors[static_cast<int>(Token::Accent)], 1.0f, 4.0f);
    }
}

void Flyout::OnPointerDown(const Point&) { SetPressed(true); }

void Flyout::OnPointerUp(const Point& local) {
    SetPressed(false);
    // Only a release inside toggles. A release outside is the drag-off cancel.
    if (bounds_.Contains(local)) Toggle();
    Invalidate(AffectsRender);
}

bool Flyout::OnKeyDown(int vk) {
    if (vk == KeySpace || vk == KeyReturn) {
        Toggle();
        return true;
    }
    return false;
}

WidgetPtr Flyout::BuildContentForSlotOpen() {
    if (Children().empty()) return nullptr;
    WidgetPtr content = Children().front();
    ClearChildren();
    return content;
}

void Flyout::Toggle() {
    if (IsOpen()) {
        ClosePopup();
        return;
    }
    // Same extraction as the slot-driven path; on a reopen OpenWith reuses the
    // saved card instead of an empty one.
    OpenWith(BuildContentForSlotOpen());
}

// ---------------------------------------------------------------------------
// ToolTip
// ---------------------------------------------------------------------------
namespace {
constexpr float kTipFontSize = 11.0f;
constexpr float kTipPadX = 8.0f;
constexpr float kTipPadY = 4.0f;
} // namespace

Size ToolTip::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    float textW = static_cast<float>(text.size()) * kTipFontSize * 0.55f;
    float lineH = kTipFontSize * 1.33f;
    if (ITextService* svc = TextSystem::Get()) {
        const Size m = svc->Measure(text, kTipFontSize, WeightNormal);
        textW = m.w;
        lineH = std::max(lineH, m.h);
    }
    // Line height only: the bubble is drawn ON TOP of the content in the space the content
    // reserved, so a tooltip must not make its element taller than it would be without one.
    desired_ = Size{textW + kTipPadX * 2 + margin.H(), lineH + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void ToolTip::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    // Children are painted by PaintTree after this returns, so only the bubble is drawn
    // here. Drawing the child here too would paint it twice.
    if (!hovered_) return;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    if (text.empty()) return;
    const float textW = [&] {
        if (ITextService* svc = TextSystem::Get()) return svc->Measure(text, kTipFontSize, WeightNormal).w;
        return static_cast<float>(text.size()) * kTipFontSize * 0.55f;
    }();
    const float lineH = TextSystem::Get() ? TextSystem::Get()->LineHeight(kTipFontSize)
                                          : kTipFontSize * 1.33f;
    const float w = textW + kTipPadX * 2;
    const float h = lineH + kTipPadY * 2;
    // Below the content, inside the space the element already occupies. It may be clipped
    // by a tight parent, which is the acceptable trade for not stealing hover state.
    const Rect bubble{bounds_.x + margin.l, bounds_.y + margin.t + bounds_.h - margin.V() + 2.0f,
                      std::min(w, std::max(w, bounds_.w)), h};
    ctx.FillRoundRect(bubble, 4.0f, palette.colors[static_cast<int>(Token::CardBackground)]);
    ctx.StrokeRect(bubble, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f, 4.0f);
    ctx.DrawText(text, bubble, palette.colors[static_cast<int>(Token::TextPrimary)], kTipFontSize,
                 WeightNormal, TextAlign::Center);
}

Widget* ToolTip::HitTest(const Point& p) {
    // Hits pass THROUGH to whatever is underneath: a tooltip labels a control, and making
    // it intercept clicks would break the control it describes.
    (void)p;
    return nullptr;
}

void ToolTip::OnPointerEnter() {
    hovered_ = true;
    Invalidate(AffectsRender);
}

void ToolTip::OnPointerLeave() {
    hovered_ = false;
    Invalidate(AffectsRender);
}

// ---------------------------------------------------------------------------
// ComboBox
// ---------------------------------------------------------------------------
namespace {
constexpr float kComboPadX = 10.0f;
constexpr float kComboPadY = 6.0f;
constexpr float kComboRowH = 28.0f;
constexpr float kComboMaxRows = 8.0f;
} // namespace

std::vector<std::wstring> ComboBox::SplitItems(const std::wstring& items) {
    std::vector<std::wstring> out;
    std::wstring current;
    for (wchar_t ch : items) {
        // ';' rather than ',' because item text routinely contains commas ("Paris,
        // France") and a separator that appears in real data is worse than a less pretty
        // one.
        if (ch == L';') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

Size ComboBox::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const std::vector<std::wstring> items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    // Wide enough for the widest item, so a longer choice is not clipped when selected.
    float widest = 0.0f;
    if (ITextService* svc = TextSystem::Get()) {
        for (const auto& item : items) {
            widest = std::max(widest, svc->Measure(item, fontSize, WeightNormal).w);
        }
    }
    if (widest <= 0.0f) widest = 80.0f;
    const float lineH = TextSystem::Get() ? TextSystem::Get()->LineHeight(fontSize)
                                          : fontSize * 1.33f;
    desired_ = Size{widest + kComboPadX * 2 + 24.0f + margin.H(),
                    std::max(lineH, 18.0f) + kComboPadY * 2 + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void ComboBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const bool open = Get<bool>(SlotOf("IsOpen"));
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];
    Color fill = palette.colors[static_cast<int>(Token::ControlFill)];
    if (hovered_ || open) fill = palette.colors[static_cast<int>(Token::ControlFillHover)];
    ctx.FillRoundRect(box, 4.0f, fill);
    ctx.StrokeRect(box, focused_ || open ? accent : palette.colors[static_cast<int>(Token::CardBorder)],
                   focused_ || open ? 1.5f : 1.0f, 4.0f);

    const std::vector<std::wstring> items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    const int index = Get<int>(SlotOf("SelectedIndex"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const std::wstring text =
        index >= 0 && index < static_cast<int>(items.size())
            ? items[static_cast<size_t>(index)]
            : Get<std::wstring>(SlotOf("PlaceholderText"));
    const Color fg = palette.colors[static_cast<int>(
        index >= 0 && index < static_cast<int>(items.size()) ? Token::TextPrimary
                                                             : Token::TextSecondary)];
    ctx.DrawText(text, Rect{box.x + kComboPadX, box.y, box.w - kComboPadX * 2 - 20.0f, box.h}, fg,
                 fontSize, WeightNormal, TextAlign::Left);
    ctx.DrawText(open ? L"\u25B2" : L"\u25BC", Rect{box.Right() - 20.0f, box.y, 14.0f, box.h},
                 palette.colors[static_cast<int>(Token::TextSecondary)], 9.0f, WeightNormal,
                 TextAlign::Center);
}


void ComboBox::OnPointerDown(const Point&) { SetPressed(true); }

void ComboBox::OnPointerUp(const Point& local) {
    if (!bounds_.Contains(local)) return;
    // Hit testing, hover and press all come from PopupBase; this only decides whether the
    // release opens or closes the list.
    if (IsOpen()) ClosePopup();
    else OpenDropDown();
}

bool ComboBox::OnKeyDown(int vk) {
    const std::vector<std::wstring> items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    if (items.empty()) return false;
    const int current = Get<int>(SlotOf("SelectedIndex"));
    // Arrows move the SELECTION directly when closed (the Windows convention), so a
    // keyboard user does not have to open the list to change the value.
    if (vk == KeyDown) {
        const int next = std::min(current + 1, static_cast<int>(items.size()) - 1);
        SetSlotValue(SlotOf("SelectedIndex"), Value::Number(next), true);
        ApplySelection();
        return true;
    }
    if (vk == KeyUp) {
        const int next = std::max(current - 1, 0);
        SetSlotValue(SlotOf("SelectedIndex"), Value::Number(next), true);
        ApplySelection();
        return true;
    }
    if (vk == KeyReturn || vk == KeySpace) {
        if (Get<bool>(SlotOf("IsOpen"))) ClosePopup();
        else OpenDropDown();
        return true;
    }
    if (vk == KeyEscape && Get<bool>(SlotOf("IsOpen"))) {
        ClosePopup();
        return true;
    }
    return false;
}
void ComboBox::OnSlotChanged(int slot, const Value& value) {
    // The base owns IsOpen, including the ownLayer_ bookkeeping that used to drift and leave
    // the box unopenable (CODE_PLAN §7.25). This only adds the derived text.
    PopupBase::OnSlotChanged(slot, value);
    if (slot == SlotOf("SelectedIndex")) ApplySelection();
}

void ComboBox::ApplySelection() {
    // SelectedText is DERIVED, so markup can bind either the index (stable across a list
    // edit) or the text (readable in a caption). Deriving it here means the two can never
    // disagree.
    const std::vector<std::wstring> items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    const int index = Get<int>(SlotOf("SelectedIndex"));
    const int slotText = SlotOf("SelectedText");
    if (slotText < 0) return;
    const std::wstring text = index >= 0 && index < static_cast<int>(items.size())
                                  ? items[static_cast<size_t>(index)]
                                  : std::wstring();
    if (Get<std::wstring>(slotText) != text) SetSlotValue(slotText, Value::Text(text));
    // The open drop-down paints its own selected row. Keyboard arrows change the
    // slot while the list stays up, so keep that row in step with the value.
    if (IsOpen()) {
        if (Widget* root = Root()) {
            if (OverlayLayer* layer = root->Overlay()) {
                if (auto* list = dynamic_cast<FlyoutList*>(layer->Content())) {
                    list->SetSelected(index);
                }
            }
        }
    }
    Invalidate(AffectsRender);
}

Rect ComboBox::DropDownBounds() const {
    const std::vector<std::wstring> items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    const float rows = std::min(static_cast<float>(items.size()), kComboMaxRows);
    // Never taller than the list needs, and never so tall it cannot fit: eight rows is the
    // usual cap before a drop-down should be a scrollable list instead.
    return Rect{bounds_.x, bounds_.y + bounds_.h + 2.0f, bounds_.w,
                std::max(kComboRowH, rows * kComboRowH) + 8.0f};
}

void ComboBox::OpenDropDown() {
    const std::vector<std::wstring> items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    if (items.empty()) return;
    // A list of rows is a real widget tree rather than a menu model: the rows need per-row
    // hit testing and the selected row needs a distinct look, and the menu layer's row model
    // has neither.
    auto list = std::make_shared<FlyoutList>();
    list->Configure(items, Get<int>(SlotOf("SelectedIndex")), [this](int chosen) {
        // Choosing is a user edit, so TwoWay bindings push it outward.
        SetSlotValue(SlotOf("SelectedIndex"), Value::Number(chosen), true);
        ApplySelection();
        // Close through the base so ownLayer_ and the slot stay in step.
        ClosePopup();
    });
    // Anchor and size come from the overrides, so the geometry lives in DropDownBounds only.
    OpenWith(list);
}

WidgetPtr ComboBox::BuildContentForSlotOpen() {
    // A ViewModel opening the list is a legitimate thing to bind, so build the same row list
    // the click path builds rather than ignoring the write.
    const std::vector<std::wstring> items = SplitItems(Get<std::wstring>(SlotOf("Items")));
    if (items.empty()) return nullptr;
    auto list = std::make_shared<FlyoutList>();
    list->Configure(items, Get<int>(SlotOf("SelectedIndex")), [this](int chosen) {
        SetSlotValue(SlotOf("SelectedIndex"), Value::Number(chosen), true);
        ApplySelection();
        ClosePopup();
    });
    return list;
}

Point ComboBox::AnchorPoint() const {
    const Rect drop = DropDownBounds();
    // One pixel of overlap with the box, so the list reads as attached to it.
    return ToWindow(Point{drop.x, drop.y - 2.0f});
}

Size ComboBox::PopupSize() const {
    const Rect drop = DropDownBounds();
    return Size{drop.w, drop.h};
}

// ---------------------------------------------------------------------------
// FlyoutList
// ---------------------------------------------------------------------------
void FlyoutList::Configure(std::vector<std::wstring> items, int selected,
                           std::function<void(int)> onChosen) {
    items_ = std::move(items);
    selected_ = selected;
    onChosen_ = std::move(onChosen);
    Invalidate(AffectsMeasure | AffectsRender);
}

Size FlyoutList::Measure(const Size& available) {
    (void)available;
    float widest = 0.0f;
    if (ITextService* svc = TextSystem::Get()) {
        for (const auto& item : items_) {
            widest = std::max(widest, svc->Measure(item, 13.0f, WeightNormal).w);
        }
    }
    desired_ = Size{widest + 24.0f, static_cast<float>(items_.size()) * kComboRowH};
    ApplySizeConstraints();
    return desired_;
}

void FlyoutList::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Rect box = bounds_.Deflate(Thickness{});
    if (box.w <= 0 || box.h <= 0) return;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Rect row{box.x, box.y + static_cast<float>(i) * kComboRowH, box.w, kComboRowH};
        const bool isSelected = static_cast<int>(i) == selected_;
        const bool isHovered = static_cast<int>(i) == hovered_;
        if (isHovered) {
            ctx.FillRoundRect(row.Deflate(Thickness{2, 0, 2, 0}), 3.0f,
                              palette.colors[static_cast<int>(Token::ControlFillHover)]);
        } else if (isSelected) {
            // Selected and hovered must be distinguishable, or the pointer cannot tell the
            // user what they are about to change to.
            ctx.FillRoundRect(row.Deflate(Thickness{2, 0, 2, 0}), 3.0f,
                              palette.colors[static_cast<int>(Token::ControlFill)]);
        }
        const Color fg = palette.colors[static_cast<int>(
            isSelected ? Token::Accent : Token::TextPrimary)];
        ctx.DrawText(items_[i], Rect{row.x + 10.0f, row.y, row.w - 20.0f, row.h}, fg, 13.0f,
                     isSelected ? WeightSemiBold : WeightNormal, TextAlign::Left);
    }
}

Widget* FlyoutList::HitTest(const Point& p) {
    // Claims hits anywhere in its own rect, including the gaps between rows: a click in the
    // list must not fall through and dismiss the flyout as "outside".
    return bounds_.Contains(p) ? this : nullptr;
}

int FlyoutList::IndexAt(const Point& local) const {
    if (!bounds_.Contains(local)) return -1;
    const int index = static_cast<int>((local.y - bounds_.y) / kComboRowH);
    if (index < 0 || index >= static_cast<int>(items_.size())) return -1;
    return index;
}

void FlyoutList::OnPointerMove(const Point& local) {
    const int index = IndexAt(local);
    if (index == hovered_) return;
    hovered_ = index;
    Invalidate(AffectsRender);
}

void FlyoutList::OnPointerUp(const Point& local) {
    const int index = IndexAt(local);
    if (index < 0) return;
    if (onChosen_) onChosen_(index);
}

// ---------------------------------------------------------------------------
// AutoSuggestBox
// ---------------------------------------------------------------------------
namespace {
constexpr float kSuggestPadX = 10.0f;
constexpr float kSuggestPadY = 6.0f;
constexpr float kSuggestRowH = 26.0f;
constexpr float kSuggestFontSize = 13.0f;
constexpr float kSuggestMaxRows = 6.0f;
} // namespace

std::vector<std::wstring> AutoSuggestBox::SplitItems(const std::wstring& items) {
    // Delegated so the two controls cannot drift on what a separator is.
    return ComboBox::SplitItems(items);
}

Size AutoSuggestBox::Measure(const Size& available) {
    (void)available;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const float lineH = TextSystem::Get() ? TextSystem::Get()->LineHeight(fontSize)
                                          : fontSize * 1.33f;
    // Wide enough for the widest suggestion, so the box does not jump when the list opens.
    float widest = 120.0f;
    if (ITextService* svc = TextSystem::Get()) {
        for (const auto& item : SplitItems(Get<std::wstring>(SlotOf("Items")))) {
            widest = std::max(widest, svc->Measure(item, fontSize, WeightNormal).w);
        }
    }
    desired_ = Size{widest + kSuggestPadX * 2 + 24.0f + margin.H(),
                    std::max(lineH, 18.0f) + kSuggestPadY * 2 + margin.V()};
    ApplySizeConstraints();
    return desired_;
}

void AutoSuggestBox::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect box{bounds_.x + margin.l, bounds_.y + margin.t,
                   std::max(0.0f, bounds_.w - margin.H()),
                   std::max(0.0f, bounds_.h - margin.V())};
    if (box.w <= 0 || box.h <= 0) return;
    const Color accent = palette.colors[static_cast<int>(Token::Accent)];
    const bool open = IsOpen();
    ctx.FillRoundRect(box, 4.0f, palette.colors[static_cast<int>(Token::ControlFill)]);
    ctx.StrokeRect(box, Focused() || open ? accent : palette.colors[static_cast<int>(Token::CardBorder)],
                   Focused() || open ? 1.5f : 1.0f, 4.0f);

    const std::wstring text = Get<std::wstring>(SlotOf("Text"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const Rect textBox{box.x + kSuggestPadX, box.y,
                       std::max(0.0f, box.w - kSuggestPadX * 2 - 20.0f), box.h};
    if (text.empty() && composition_.empty()) {
        ctx.DrawText(Get<std::wstring>(SlotOf("PlaceholderText")),
                     Rect{box.x + kSuggestPadX, box.y, box.w - kSuggestPadX * 2, box.h},
                     palette.colors[static_cast<int>(Token::TextSecondary)], fontSize,
                     WeightNormal, TextAlign::Left);
    } else {
        // The composition preview is drawn but never written into Text: cancelling
        // must leave the model untouched, and suggestions must not react to a
        // half-finished candidate.
        if (!text.empty()) {
            ctx.DrawText(text, textBox,
                         palette.colors[static_cast<int>(Token::TextPrimary)], fontSize,
                         WeightNormal, TextAlign::Left);
        }
        if (!composition_.empty()) {
            float x = textBox.x;
            if (ITextService* svc = TextSystem::Get()) x += svc->Measure(text, fontSize, WeightNormal).w;
            ctx.DrawText(composition_, Rect{x, textBox.y, std::max(0.0f, textBox.w - (x - textBox.x)),
                                            textBox.h},
                         palette.colors[static_cast<int>(Token::Accent)], fontSize,
                         WeightNormal, TextAlign::Left);
        }
    }
    // A search glyph on the right: the box has to say it suggests, not just accept input.
    ctx.DrawText(L"\u2315", Rect{box.Right() - 22.0f, box.y, 16.0f, box.h},
                 palette.colors[static_cast<int>(Token::TextSecondary)], 12.0f, WeightNormal,
                 TextAlign::Center);
}

std::vector<std::wstring> AutoSuggestBox::Filtered() const {
    const std::wstring query = Get<std::wstring>(SlotOf("Text"));
    const std::vector<std::wstring> all = SplitItems(Get<std::wstring>(SlotOf("Items")));
    std::vector<std::wstring> out;
    if (query.empty()) return out; // nothing to suggest before the user types

    std::wstring lowerQuery = query;
    for (wchar_t& c : lowerQuery) c = static_cast<wchar_t>(std::towlower(c));
    for (const auto& item : all) {
        std::wstring lower = item;
        for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
        // Substring rather than prefix: users type a distinctive middle word ("gal" for
        // "Seattle Galleria") far more often than they type the first letters.
        if (lower.find(lowerQuery) != std::wstring::npos) out.push_back(item);
    }
    return out;
}

WidgetPtr AutoSuggestBox::BuildContentForSlotOpen() {
    const std::vector<std::wstring> matches = Filtered();
    if (matches.empty()) return nullptr;
    auto list = std::make_shared<FlyoutList>();
    list->Configure(matches, highlighted_, [this](int chosen) { Commit(chosen); });
    return list;
}

Point AutoSuggestBox::AnchorPoint() const {
    return ToWindow(Point{bounds_.x, bounds_.y + bounds_.h + 2.0f});
}

Size AutoSuggestBox::PopupSize() const {
    const float rows = std::min(static_cast<float>(Filtered().size()), kSuggestMaxRows);
    return Size{bounds_.w, std::max(kSuggestRowH, rows * kSuggestRowH) + 8.0f};
}

void AutoSuggestBox::OnPointerDown(const Point&) { SetPressed(true); }

void AutoSuggestBox::OnPointerUp(const Point& local) {
    SetPressed(false);
    // The box does not toggle from a click: clicking into it is just focusing it, which is
    // what a text field does. The list appears as the user types.
    (void)local;
    Invalidate(AffectsRender);
}

bool AutoSuggestBox::OnChar(wchar_t ch) {
    if (ch < 32 && ch != L'\t') return false;
    // The slot is the source of truth, re-read on EVERY keystroke. A buffer refreshed only
    // when empty meant an externally written value (a binding, a reset) was invisible: the
    // text on screen changed, but the next keystroke appended to the stale buffer and
    // silently produced a string the user never typed.
    //
    // Insert at the END rather than at a caret: this control deliberately has no caret
    // model, because a suggest box you cannot see the caret in still types correctly, and a
    // caret would mean reimplementing TextBox for one feature.
    editing_ = Get<std::wstring>(SlotOf("Text"));
    editing_.push_back(ch);
    SetSlotValue(SlotOf("Text"), Value::Text(editing_));
    Refresh();
    return true;
}

bool AutoSuggestBox::OnKeyDown(int vk) {
    // Re-read for the same reason as OnChar: the slot may have been written by a binding.
    editing_ = Get<std::wstring>(SlotOf("Text"));

    if (vk == KeyEscape) {
        if (!IsOpen()) return false;
        // Dismiss WITHOUT touching the text: Escape means "stop suggesting", not "undo what
        // I typed".
        ClosePopup();
        return true;
    }
    if (vk == KeyBack) {
        if (editing_.empty()) return false;
        editing_.pop_back();
        SetSlotValue(SlotOf("Text"), Value::Text(editing_));
        Refresh();
        return true;
    }

    const std::vector<std::wstring> matches = Filtered();
    if (vk == KeyDown || vk == KeyUp) {
        if (matches.empty()) return false;
        highlighted_ = vk == KeyDown
                           ? std::min(highlighted_ + 1, static_cast<int>(matches.size()) - 1)
                           : std::max(highlighted_ - 1, 0);
        // Opening on the first arrow key is what makes the list reachable by
        // keyboard alone. Open AFTER moving so the freshly built list starts on
        // the row the user just selected.
        if (!IsOpen()) OpenSuggestions();
        // The list paints its OWN selected_ row, set when the layer was built.
        // Without this sync the highlight moves internally while the card keeps
        // painting row 0 -- Enter then commits a different row than the one the
        // user saw highlighted.
        if (Widget* root = Root()) {
            if (OverlayLayer* layer = root->Overlay()) {
                if (auto* list = dynamic_cast<FlyoutList*>(layer->Content())) {
                    list->SetSelected(highlighted_);
                }
            }
        }
        Invalidate(AffectsRender);
        return true;
    }
    if (vk == KeyReturn) {
        if (!IsOpen()) return false;
        // Enter commits the HIGHLIGHTED row; with nothing highlighted it takes the first
        // match, so typing a full word and pressing Enter does what the user expects.
        Commit(highlighted_ >= 0 ? highlighted_ : 0);
        return true;
    }
    return false;
}

// IME: this control has no caret model, so the preview is shown at the end of the
// text and a committed result is appended there. The host forwards WM_CHAR only
// when Composing() is false, so without these overrides an IME (Chinese, Japanese,
// even Latin input in a Chinese IME) could never reach the Text slot at all.
void AutoSuggestBox::SetComposition(const std::wstring& text) {
    if (composition_ == text) return;
    composition_ = text;
    Invalidate(AffectsRender);
}

void AutoSuggestBox::CommitComposition(const std::wstring& finalText) {
    if (composition_.empty() && finalText.empty()) return;
    composition_.clear();
    // Re-read the slot: a binding may have replaced the text while the user was
    // composing. Appending to a stale buffer would resurrect a value nobody typed.
    editing_ = Get<std::wstring>(SlotOf("Text"));
    if (!finalText.empty()) {
        editing_ += finalText;
        SetSlotValue(SlotOf("Text"), Value::Text(editing_));
        Refresh();  // new text means new suggestions
    }
    Invalidate(AffectsRender);
}

void AutoSuggestBox::CancelComposition() {
    if (composition_.empty()) return;
    composition_.clear();
    Invalidate(AffectsRender);
}

CompositionCaret AutoSuggestBox::ImeCaret() const {
    // Candidate window at the end of the visible text, reusing the same padding
    // and font metrics as OnPaint so it lands on the field, not at its corner.
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float fontSize = Get<float>(SlotOf("FontSize"));
    const float lineH =
        TextSystem::Get() ? TextSystem::Get()->LineHeight(fontSize) : fontSize * 1.33f;
    float x = bounds_.x + margin.l + pad.l + kSuggestPadX;
    if (ITextService* svc = TextSystem::Get()) {
        x += svc->Measure(Get<std::wstring>(SlotOf("Text")), fontSize, WeightNormal).w;
    }
    CompositionCaret caret;
    caret.x = x;
    caret.y = bounds_.y + margin.t + pad.t;
    caret.lineHeight = lineH;
    return caret;
}

void AutoSuggestBox::Refresh() {
    const std::vector<std::wstring> matches = Filtered();
    highlighted_ = matches.empty() ? -1 : 0;
    if (matches.empty()) {
        // Nothing matches: close rather than showing an empty card. An empty popup reads as
        // a bug, and there is nothing to choose from anyway.
        if (IsOpen()) ClosePopup();
        return;
    }
    if (IsOpen()) {
        // Already open: only the contents changed, so re-open with the new list rather than
        // leaving the old rows on screen.
        ClosePopup();
    }
    OpenSuggestions();
}

void AutoSuggestBox::OpenSuggestions() {
    const std::vector<std::wstring> matches = Filtered();
    if (matches.empty()) return;
    auto list = std::make_shared<FlyoutList>();
    // The list highlights `highlighted_`, so the keyboard and the pointer agree on which row
    // is "current" instead of maintaining two independent ideas.
    list->Configure(matches, highlighted_, [this](int chosen) { Commit(chosen); });
    OpenWith(list);
}

void AutoSuggestBox::Commit(int index) {
    const std::vector<std::wstring> matches = Filtered();
    if (index < 0 || index >= static_cast<int>(matches.size())) {
        ClosePopup();
        return;
    }
    const std::wstring chosen = matches[static_cast<size_t>(index)];
    // The chosen text replaces what was typed: committing IS the point of the control, and
    // leaving the partial query in place would make Enter look like it did nothing.
    editing_ = chosen;
    SetSlotValue(SlotOf("Text"), Value::Text(chosen), true);
    highlighted_ = -1;
    ClosePopup();
    Invalidate(AffectsRender);
}

} // namespace wui