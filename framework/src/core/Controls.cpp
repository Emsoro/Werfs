// SPDX-License-Identifier: MIT
#include "Controls.h"

#include <algorithm>

namespace wui {

// ---------------------------------------------------------------------------
// TextBlock
// ---------------------------------------------------------------------------
Size TextBlock::Measure(const Size& available) {
    (void)available; // TextBlock sizes to its content; the constraint is unused.
    const int slotText = SlotOf("Text");
    const int slotSize = SlotOf("FontSize");
    const int slotPad = SlotOf("Padding");
    const int slotMargin = SlotOf("Margin");
    const std::wstring text = slotText >= 0 ? Get<std::wstring>(slotText) : L"";
    const float fontSize = slotSize >= 0 ? Get<float>(slotSize) : 14.0f;
    const Thickness pad = slotPad >= 0 ? Get<Thickness>(slotPad) : Thickness{};
    const Thickness margin = slotMargin >= 0 ? Get<Thickness>(slotMargin) : Thickness{};

    Size t{0, fontSize * 1.33f};
    if (ITextService* svc = TextSystem::Get()) {
        if (!text.empty()) t = svc->Measure(text, fontSize, Get<int>(SlotOf("FontWeight")));
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
    ctx.DrawText(text, box, fg, Get<float>(SlotOf("FontSize")), Get<int>(SlotOf("FontWeight")), align);
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
    for (auto& c : children_) {
        const Size s = c->Measure(inner);
        w = std::max(w, s.w);
        h = std::max(h, s.h);
    }
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
    for (auto& c : children_) c->Arrange(content);
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
    for (auto& c : children_) {
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
    for (auto& c : children_) c->Arrange(content);
}

void ScrollViewer::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect viewport{bounds_.x + margin.l, bounds_.y + margin.t,
                        std::max(0.0f, bounds_.w - margin.H()),
                        std::max(0.0f, bounds_.h - margin.V())};
    if (viewport.w <= 0 || viewport.h <= 0) return;

    ctx.PushClip(viewport);
    // Children are arranged in unscrolled space; the paint offset is what moves
    // them up. HitTest undoes the same offset.
    for (auto& c : children_) {
        const Point saved = c->PaintOffset();
        c->SetPaintOffset(Point{0, -offset_});
        c->PaintTree(ctx, palette);
        c->SetPaintOffset(saved);
    }
    ctx.PopClip();

    // Scrollbar: a thin track on the right with a proportional thumb. Only when
    // there is something to scroll -- an always-visible bar on a short page
    // reads as a rendering bug.
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

void ScrollViewer::ScrollTo(float offset) {
    const float clamped = std::min(std::max(0.0f, offset), maxScroll_);
    if (clamped == offset_) return;
    offset_ = clamped;
    Invalidate(AffectsRender);
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
    for (auto& c : children_) c->Arrange(content);
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
    for (auto& c : children_) {
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
