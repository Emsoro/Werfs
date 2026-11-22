// SPDX-License-Identifier: MIT
#include "Controls.h"

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
    const Size inner{std::max(0.0f, available.w - margin.H() - pad.H()),
                     std::max(0.0f, available.h - margin.V() - pad.V())};
    float w = 0;
    float h = 0;
    for (auto& c : LayoutChildren()) {
        const Size s = c->Measure(inner);
        w = std::max(w, s.w);
        h = std::max(h, s.h);
    }
    // ApplySizeConstraints applies an explicit Width/Height on top of this.
    desired_ = Size{w + margin.H() + pad.H(), h + margin.V() + pad.V()};
    ApplySizeConstraints();
    return desired_;
}

void Border::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect content{finalRect.x + margin.l + pad.l, finalRect.y + margin.t + pad.t,
                       std::max(0.0f, finalRect.w - margin.H() - pad.H()),
                       std::max(0.0f, finalRect.h - margin.V() - pad.V())};
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
    const float borderW = Get<float>(SlotOf("BorderThickness"));
    if (border.a > 0 && borderW > 0) ctx.StrokeRect(box, border, borderW, radius);
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

void Page::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const Rect content{finalRect.x + pad.l, finalRect.y + pad.t, std::max(0.0f, finalRect.w - pad.H()),
                       std::max(0.0f, finalRect.h - pad.V())};
    for (auto& c : LayoutChildren()) c->Arrange(content);
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
    (void)available;
    ApplySizeConstraints();
    return desired_;
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
    if (text.empty() && !focused_) {
        ctx.DrawText(Get<std::wstring>(SlotOf("PlaceholderText")), textBox,
                     palette.colors[static_cast<int>(Token::TextSecondary)], fontSize, WeightNormal,
                     TextAlign::Left);
        return;
    }
    ctx.DrawText(text, textBox, ResolveColor(Get<Color>(SlotOf("Foreground")), palette), fontSize,
                 WeightNormal, TextAlign::Left);
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
ListBox::ListBox() {
    BindType(TypeRegistry::Instance().Find("wui/ListBox"));
    slotItems_ = SlotOf("ItemsSource");
    slotTemplate_ = SlotOf("ItemTemplate");
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

void ListBox::OnSlotChanged(int slot, const Value&) {
    if (slot == slotItems_) SubscribeSource();
    if (slot == slotTemplate_) RebuildItems(text_);
}

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
    for (auto& w : itemWidgets_) w->PaintTree(ctx, palette);
    ctx.PopClip();
}

Widget* ListBox::HitTest(const Point& p) {
    for (auto it = itemWidgets_.rbegin(); it != itemWidgets_.rend(); ++it) {
        if (Widget* hit = (*it)->HitTest(p)) return hit;
    }
    return bounds_.Contains(p) ? this : nullptr;
}

} // namespace wui
