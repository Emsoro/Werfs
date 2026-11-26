// SPDX-License-Identifier: MIT
#include "Overlay.h"

#include "Mvvm.h"
#include "Widget.h"

#include <algorithm>

namespace wui {

// ---------------------------------------------------------------------------
// PopupMenuItem
//
// Declared in the header but defined here so Overlay.h stays free of Mvvm.h:
// the struct needs ICommand only to call it, not to describe itself.
// ---------------------------------------------------------------------------
bool PopupMenuItem::CanInvoke() const {
    if (kind != Kind::Command && kind != Kind::Toggle) return false;
    if (!enabled) return false;
    if (!command) return false;
    auto* cmd = dynamic_cast<ICommand*>(command.get());
    return cmd != nullptr && cmd->CanExecute();
}

void PopupMenuItem::Invoke() const {
    if (!command) return;
    if (auto* cmd = dynamic_cast<ICommand*>(command.get())) {
        if (cmd->CanExecute()) cmd->Execute();
    }
}

void RefreshMenuEnabled(std::vector<PopupMenuItem>& items) {
    for (auto& item : items) {
        if (item.kind == PopupMenuItem::Kind::Command) {
            if (auto* cmd = dynamic_cast<ICommand*>(item.command.get())) {
                item.enabled = cmd->CanExecute();
                continue;
            }
        }
        if (item.kind == PopupMenuItem::Kind::Submenu) RefreshMenuEnabled(item.children);
    }
}

namespace {

// Geometry shared by painting and hit testing. Computing it twice is how a menu
// item ends up drawn in one place and clickable in another.
constexpr float kItemHeight = 30.0f;
constexpr float kSeparatorHeight = 9.0f;
constexpr float kHeaderHeight = 26.0f;
constexpr float kPadX = 12.0f;
constexpr float kPadY = 6.0f;
constexpr float kHintGap = 28.0f;
constexpr float kMinWidth = 170.0f;
constexpr float kMaxWidth = 380.0f;

float ItemHeight(const PopupMenuItem& item) {
    switch (item.kind) {
    case PopupMenuItem::Kind::Separator: return kSeparatorHeight;
    case PopupMenuItem::Kind::Header: return kHeaderHeight;
    default: return kItemHeight;
    }
}

bool IsInteractive(const PopupMenuItem& item) {
    return item.kind != PopupMenuItem::Kind::Separator && item.kind != PopupMenuItem::Kind::Header;
}

float TextWidth(const std::wstring& text, float fontSize, int weight) {
    if (ITextService* svc = TextSystem::Get()) return svc->Measure(text, fontSize, weight).w;
    return static_cast<float>(text.size()) * fontSize * 0.55f;
}

float MenuWidth(const std::vector<PopupMenuItem>& items) {
    float w = kMinWidth;
    for (const auto& item : items) {
        float row = kPadX * 2 + 20.0f; // reserve the icon gutter
        row += TextWidth(item.text, 13.0f, WeightNormal);
        if (!item.hint.empty()) row += kHintGap + TextWidth(item.hint, 12.0f, WeightNormal);
        if (item.kind == PopupMenuItem::Kind::Submenu) row += 16.0f;
        w = std::max(w, row);
    }
    return std::min(w, kMaxWidth);
}

float MenuHeight(const std::vector<PopupMenuItem>& items) {
    float h = kPadY * 2;
    for (const auto& item : items) h += ItemHeight(item);
    return h;
}

// A menu that would run past the window edge is flipped to open the other way.
// A menu taller than the window is clamped rather than flipped, because flipping
// an overflowing menu only moves the overflow.
// The first focusable widget in a subtree, depth-first in child order. Used by the flyout
// to decide where a key goes without a focus stack.
Widget* FirstFocusable(Widget* w) {
    if (w == nullptr) return nullptr;
    if (w->Focusable()) return w;
    for (auto& child : w->Children()) {
        if (Widget* found = FirstFocusable(child.get())) return found;
    }
    return nullptr;
}

Point PlaceNear(Point anchor, Size want, Size available) {
    float x = anchor.x;
    float y = anchor.y;
    if (x + want.w > available.w) x = std::max(0.0f, available.w - want.w);
    if (y + want.h > available.h) {
        // Prefer opening upward from the anchor when there is more room above.
        const float above = anchor.y - want.h;
        y = above >= 0 ? above : std::max(0.0f, available.h - want.h);
    }
    return Point{std::max(0.0f, x), std::max(0.0f, y)};
}

} // namespace

// ---------------------------------------------------------------------------
// MenuLayer
// ---------------------------------------------------------------------------
class MenuLayer final : public OverlayLayer {
public:
    MenuLayer(std::vector<PopupMenuItem> items, Point anchor, std::function<void()> onClosed)
        : items_(std::move(items)), anchor_(anchor), onClosed_(std::move(onClosed)) {
        // The window size is not known until the first paint, so the geometry is
        // computed lazily against a generous default and corrected on paint.
        ComputeGeometry(Size{1920.0f, 1080.0f});
    }

    // The area available for placing the menu. The host sets the real window
    // size before showing so a menu near the right edge flips left.
    void SetAvailable(Size available) {
        ComputeGeometry(available);
    }

    Rect Bounds() const override { return bounds_; }

    void Paint(PaintCtx& ctx, const ThemePalette& palette) override {
        // Every level uses the same row renderer. The previous version had a
        // separate, text-only PaintSubmenu, which made a third level invisible:
        // there was no arrow to tell the reader that "Font size" opened another
        // menu, and no way to highlight the row that was waiting for a click.
        PaintMenu(ctx, palette, items_, bounds_, 0);
    }

    bool OnPointerDown(const Point& p) override {
        // Find the deepest menu under the pointer, then act on THAT menu's row.
        // Checking the deepest first is what makes a third-level click land on
        // the third-level item instead of being swallowed by its parent.
        const int depth = MenuAtPoint(p);
        if (depth < 0) {
            if (LightDismiss()) {
                Close();
                return true;
            }
            return true; // modal-ish menu: swallow, do not touch the page
        }
        const size_t d = static_cast<size_t>(depth);
        const int row = RowAt(MenuAt(d), MenuBounds(d), p);
        if (row < 0) return true; // padding or separator
        const PopupMenuItem& item = MenuAt(d)[static_cast<size_t>(row)];
        if (item.kind == PopupMenuItem::Kind::Submenu) {
            OpenSubmenu(d, row);
            return true;
        }
        Activate(item);
        return true;
    }

    void OnPointerUp(const Point&) override {}

    void OnPointerMove(const Point& p) override {
        // Hover works at every depth: sweeping from a parent row into its submenu
        // (and from there into a third level) keeps the chain open.
        const int depth = MenuAtPoint(p);
        if (depth < 0) {
            hoveredRow_ = -1;
            openPath_.clear();
            return;
        }
        const size_t d = static_cast<size_t>(depth);
        const int row = RowAt(MenuAt(d), MenuBounds(d), p);
        if (row < 0) {
            hoveredRow_ = -1;
            return;
        }
        // A deeper menu that is no longer the parent under the pointer is stale.
        // Keeping the row that actually opened it is deliberate: hovering over
        // the parent must not close the child it just revealed.
        if (openPath_.size() > d && row != openPath_[d]) openPath_.resize(d);
        hoveredRow_ = row;
        const PopupMenuItem& item = MenuAt(d)[static_cast<size_t>(row)];
        if (hoverOnOpen_ && item.kind == PopupMenuItem::Kind::Submenu &&
            (d >= openPath_.size() || openPath_[d] != row)) {
            OpenSubmenu(d, row);
        }
    }

    bool OnKeyDown(int vk, int /*modifiers*/) override {
        const size_t depth = openPath_.size();
        const std::vector<PopupMenuItem>& items = MenuAt(depth);
        switch (vk) {
        case KeyEscape:
            if (!openPath_.empty()) {
                // Close one level at a time, leaving the highlight on the row that
                // opened it. Collapsing the whole chain would lose the reader's place.
                hoveredRow_ = openPath_.back();
                openPath_.pop_back();
                return true;
            }
            Close();
            return true;
        case KeyLeft:
            if (!openPath_.empty()) {
                hoveredRow_ = openPath_.back();
                openPath_.pop_back();
                return true;
            }
            return false;
        case KeyRight:
            if (hoveredRow_ >= 0 && hoveredRow_ < static_cast<int>(items.size()) &&
                items[static_cast<size_t>(hoveredRow_)].kind == PopupMenuItem::Kind::Submenu) {
                OpenSubmenu(depth, hoveredRow_);
                return true;
            }
            return false;
        case KeyDown:
        case KeyUp: {
            const int step = vk == KeyDown ? 1 : -1;
            int row = hoveredRow_;
            const int count = static_cast<int>(items.size());
            for (int guard = 0; guard < count; ++guard) {
                row += step;
                if (row < 0) row = count - 1;
                if (row >= count) row = 0;
                if (IsInteractive(items[static_cast<size_t>(row)]) &&
                    items[static_cast<size_t>(row)].enabled) {
                    hoveredRow_ = row;
                    return true;
                }
            }
            return true;
        }
        case KeyReturn:
            if (hoveredRow_ >= 0 && hoveredRow_ < static_cast<int>(items.size())) {
                const PopupMenuItem& item = items[static_cast<size_t>(hoveredRow_)];
                if (item.kind == PopupMenuItem::Kind::Submenu) {
                    OpenSubmenu(depth, hoveredRow_);
                    return true;
                }
                Activate(item);
            }
            return true;
        default:
            return false;
        }
    }

    bool Active() const override { return active_; }

    // Lets the bar hand hover-opening behaviour to the layer. A menu opened by
    // clicking its title should follow the pointer; that is the whole point of a
    // menu bar as opposed to a button that opens a popup.
    void SetHoverOnOpen(bool on) { hoverOnOpen_ = on; }

    // Test hooks: what a click at a point would do, without doing it.
    // OpenSubmenuRow keeps its old meaning (the first level that is open), while
    // Depth/Bounds let a test walk into a third level without knowing our geometry.
    int HoveredRow() const { return hoveredRow_; }
    int OpenSubmenuRow() const { return openPath_.empty() ? -1 : openPath_[0]; }
    size_t OpenSubmenuDepth() const { return openPath_.size(); }
    Rect RowBounds(size_t index) const { return RowBounds(items_, bounds_, index); }
    Rect MenuBoundsAt(size_t depth) const {
        return depth <= openPath_.size() ? MenuBounds(depth) : Rect{};
    }

private:
    void ComputeGeometry(Size available) {
        const Size want{MenuWidth(items_), MenuHeight(items_)};
        const Point origin = PlaceNear(anchor_, want, available);
        bounds_ = Rect{origin.x, origin.y, want.w, want.h};
        available_ = available;
    }

    void Close() {
        if (!active_) return;
        active_ = false;
        if (onClosed_) {
            auto cb = onClosed_;
            onClosed_ = nullptr;
            cb();
        }
    }

    void Activate(const PopupMenuItem& item) {
        if (item.kind == PopupMenuItem::Kind::Submenu) {
            // Opening a submenu is the caller's job: it knows which level the row
            // belongs to, whereas Activate only sees the item itself.
            return;
        }
        if (!item.CanInvoke()) {
            // A disabled item swallows the click but leaves the menu open, which
            // is how a menu tells you "not now" without losing your place.
            return;
        }
        // Close FIRST, then invoke: a command may open a dialog or reenter the
        // menu system, and doing it the other way round leaves this layer half
        // closed when the callback reends the tree.
        const PopupMenuItem chosen = item;
        Close();
        chosen.Invoke();
    }

    // The menu at `depth` is the child opened by openPath_[depth-1]. Keeping the
    // whole path (not just one index) is what turns "one submenu" into "submenus
    // all the way down" without a second overlay layer.
    const std::vector<PopupMenuItem>& MenuAt(size_t depth) const {
        const std::vector<PopupMenuItem>* items = &items_;
        for (size_t i = 0; i < depth; ++i) {
            items = &(*items)[static_cast<size_t>(openPath_[i])].children;
        }
        return *items;
    }

    Rect MenuBounds(size_t depth) const {
        if (depth == 0) return bounds_;
        if (depth > openPath_.size()) return Rect{};
        const Rect parent = MenuBounds(depth - 1);
        const size_t row = static_cast<size_t>(openPath_[depth - 1]);
        const Rect anchor = RowBounds(MenuAt(depth - 1), parent, row);
        const auto& children = MenuAt(depth);
        const Size want{MenuWidth(children), MenuHeight(children)};
        Point origin{anchor.x + anchor.w - 4.0f, anchor.y - kPadY};
        // Flip left when the child would run past the window, then lift it if it
        // would run past the bottom. Same rule at every depth.
        if (origin.x + want.w > available_.w) origin.x = std::max(0.0f, anchor.x - want.w + 4.0f);
        if (origin.y + want.h > available_.h) origin.y = std::max(0.0f, available_.h - want.h);
        return Rect{std::max(0.0f, origin.x), std::max(0.0f, origin.y), want.w, want.h};
    }

    Rect RowBounds(const std::vector<PopupMenuItem>& items, const Rect& box, size_t index) const {
        float y = box.y + kPadY;
        for (size_t i = 0; i < items.size() && i < index; ++i) y += ItemHeight(items[i]);
        const float h = index < items.size() ? ItemHeight(items[index]) : 0.0f;
        return Rect{box.x, y, box.w, h};
    }

    int RowAt(const std::vector<PopupMenuItem>& items, const Rect& box, const Point& p) const {
        if (!box.Contains(p)) return -1;
        float y = box.y + kPadY;
        for (size_t i = 0; i < items.size(); ++i) {
            const float h = ItemHeight(items[i]);
            if (p.y >= y && p.y < y + h) return static_cast<int>(i);
            y += h;
        }
        return -1;
    }

    int MenuAtPoint(const Point& p) const {
        // Deepest first: a grandchild overlaps its parent by four pixels, and a
        // click there must belong to the grandchild the reader can see.
        for (int d = static_cast<int>(openPath_.size()); d >= 0; --d) {
            if (MenuBounds(static_cast<size_t>(d)).Contains(p)) return d;
        }
        return -1;
    }

    void OpenSubmenu(size_t depth, int row) {
        openPath_.resize(depth);
        openPath_.push_back(row);
        hoveredRow_ = -1;
    }

    bool IsRowHovered(size_t depth, size_t row) const {
        // A level with an open child highlights the row that opened it; the
        // deepest level highlights whatever the pointer/keyboard is on.
        if (depth < openPath_.size()) return static_cast<int>(row) == openPath_[depth];
        return static_cast<int>(row) == hoveredRow_;
    }

    void PaintMenu(PaintCtx& ctx, const ThemePalette& palette,
                   const std::vector<PopupMenuItem>& items, const Rect& box, size_t depth) {
        const Color card = palette.colors[static_cast<int>(Token::CardBackground)];
        const Color border = palette.colors[static_cast<int>(Token::CardBorder)];
        const Color textPrimary = palette.colors[static_cast<int>(Token::TextPrimary)];
        const Color textSecondary = palette.colors[static_cast<int>(Token::TextSecondary)];
        const Color textDisabled = palette.colors[static_cast<int>(Token::TextDisabled)];
        const Color accent = palette.colors[static_cast<int>(Token::Accent)];

        // One layer, recursive panels. A stack of overlay layers would need focus
        // and dismissal rules; this keeps the whole chain under one light-dismiss.
        ctx.FillRoundRect(box.Inflate(2.0f), 7.0f, Color{0, 0, 0, 0.10f});
        ctx.FillRoundRect(box, 6.0f, card);
        ctx.StrokeRect(box, border, 1.0f, 6.0f);

        float y = box.y + kPadY;
        for (size_t i = 0; i < items.size(); ++i) {
            const PopupMenuItem& item = items[i];
            const float h = ItemHeight(item);
            const Rect row{box.x + 2.0f, y, box.w - 4.0f, h};

            if (item.kind == PopupMenuItem::Kind::Separator) {
                ctx.FillRect(Rect{row.x + kPadX, y + h * 0.5f, row.w - kPadX * 2, 1.0f}, border);
                y += h;
                continue;
            }
            if (item.kind == PopupMenuItem::Kind::Header) {
                ctx.DrawText(item.text, Rect{row.x + kPadX, row.y, row.w - kPadX * 2, h},
                             textSecondary, 11.0f, WeightSemiBold, TextAlign::Left);
                y += h;
                continue;
            }

            const bool hovered = IsRowHovered(depth, i);
            const bool canClick = item.CanInvoke() || item.kind == PopupMenuItem::Kind::Submenu;
            if (hovered && canClick) {
                ctx.FillRoundRect(row, 4.0f,
                                  palette.colors[static_cast<int>(Token::ControlFillHover)]);
            }

            const Color fg = !canClick ? textDisabled : textPrimary;
            float x = row.x + kPadX;
            if (item.kind == PopupMenuItem::Kind::Toggle) {
                if (item.checked) {
                    ctx.DrawText(L"\u2713", Rect{x - 4.0f, row.y, 16.0f, h}, accent, 12.0f,
                                 WeightNormal, TextAlign::Center);
                }
                x += 16.0f;
            } else if (!item.icon.empty()) {
                ctx.DrawText(Utf8ToWide(item.icon), Rect{x - 4.0f, row.y, 16.0f, h}, fg, 12.0f,
                             WeightNormal, TextAlign::Center);
                x += 16.0f;
            } else {
                x += 16.0f;
            }

            float hintW = 0.0f;
            if (!item.hint.empty()) {
                hintW = TextWidth(item.hint, 12.0f, WeightNormal);
                ctx.DrawText(item.hint,
                             Rect{row.x + row.w - kPadX - hintW - 8.0f, row.y, hintW, h},
                             canClick ? textSecondary : textDisabled, 12.0f, WeightNormal,
                             TextAlign::Right);
            }
            const float submenuRoom = item.kind == PopupMenuItem::Kind::Submenu ? 16.0f : 0.0f;
            const float labelW = std::max(
                0.0f, row.x + row.w - kPadX - x - hintW - submenuRoom - (hintW > 0 ? kHintGap : 0.0f));
            ctx.DrawText(item.text, Rect{x, row.y, labelW, h}, fg, 13.0f, WeightNormal,
                         TextAlign::Left);
            // The chevron is what tells the reader "Font size" has a third level.
            if (item.kind == PopupMenuItem::Kind::Submenu) {
                ctx.DrawText(L"\u203a", Rect{row.x + row.w - kPadX - 10.0f, row.y, 10.0f, h}, fg,
                             13.0f, WeightNormal, TextAlign::Right);
            }
            y += h;
        }

        if (depth < openPath_.size()) {
            const size_t row = static_cast<size_t>(openPath_[depth]);
            PaintMenu(ctx, palette, items[row].children, MenuBounds(depth + 1), depth + 1);
        }
    }

    std::vector<PopupMenuItem> items_;
    Point anchor_;
    Rect bounds_;
    Size available_{1920.0f, 1080.0f};
    std::function<void()> onClosed_;
    bool active_ = true;
    bool hoverOnOpen_ = false;
    int hoveredRow_ = -1;
    // One index per open menu level: [0] is a second-level menu, [1] a third, and
    // so on. Replacing the single openSub_ is what removed the hard depth limit.
    std::vector<int> openPath_;
};

// ---------------------------------------------------------------------------
// DialogLayer
// ---------------------------------------------------------------------------
namespace {
constexpr float kDialogWidth = 400.0f;
constexpr float kDialogPad = 22.0f;
constexpr float kButtonHeight = 32.0f;
constexpr float kButtonMinWidth = 88.0f;
} // namespace

class DialogLayer final : public OverlayLayer {
public:
    DialogLayer(std::wstring title, std::wstring message, std::vector<DialogButton> buttons,
                std::function<void(int)> onResult, std::function<void()> onClosed)
        : title_(std::move(title)),
          message_(std::move(message)),
          buttons_(std::move(buttons)),
          onResult_(std::move(onResult)),
          onClosed_(std::move(onClosed)) {}

    void SetAvailable(Size available) { available_ = available; }

    Rect Bounds() const override {
        // Centred, and the card is what the pointer is measured against; the
        // scrim covers the window (see Paint) but never claims a hit.
        const Size want{MessageWidth(), CardHeight()};
        const Point origin{std::max(0.0f, (available_.w - want.w) * 0.5f),
                           std::max(0.0f, (available_.h - want.h) * 0.5f)};
        return Rect{origin.x, origin.y, want.w, want.h};
    }

    void Paint(PaintCtx& ctx, const ThemePalette& palette) override {
        const Rect card = Bounds();
        // The scrim is what makes a dialog modal to the eye: without it the page
        // behind looks equally available and users click it.
        ctx.FillRect(Rect{0, 0, available_.w, available_.h}, Color{0, 0, 0, 0.32f});
        ctx.FillRoundRect(card.Inflate(3.0f), 9.0f, Color{0, 0, 0, 0.18f});
        ctx.FillRoundRect(card, 8.0f, palette.colors[static_cast<int>(Token::CardBackground)]);
        ctx.StrokeRect(card, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f, 8.0f);

        const Color textPrimary = palette.colors[static_cast<int>(Token::TextPrimary)];
        const Color textSecondary = palette.colors[static_cast<int>(Token::TextSecondary)];

        float y = card.y + kDialogPad;
        if (!title_.empty()) {
            const float titleH = 26.0f;
            ctx.DrawText(title_, Rect{card.x + kDialogPad, y, MessageWidth(), titleH}, textPrimary,
                         16.0f, WeightSemiBold, TextAlign::Left);
            y += titleH + 8.0f;
        }
        // The message is drawn line by line: PaintCtx has no wrapping primitive,
        // and a data-loss prompt that silently truncates its own question is worse
        // than no prompt.
        const float lineH = 19.0f;
        for (const auto& line : messageLines_) {
            ctx.DrawText(line, Rect{card.x + kDialogPad, y, MessageWidth(), lineH}, textSecondary,
                         13.0f, WeightNormal, TextAlign::Left);
            y += lineH;
        }

        for (size_t i = 0; i < buttons_.size(); ++i) {
            const Rect b = ButtonBounds(i);
            const DialogButton& button = buttons_[i];
            if (button.isDefault) {
                ctx.FillRoundRect(b, 4.0f, palette.colors[static_cast<int>(Token::Accent)]);
                ctx.DrawText(button.text, b,
                             palette.colors[static_cast<int>(Token::TextOnAccent)], 13.0f,
                             WeightNormal, TextAlign::Center);
            } else {
                ctx.FillRoundRect(b, 4.0f,
                                  palette.colors[static_cast<int>(Token::ControlFill)]);
                ctx.StrokeRect(b, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f, 4.0f);
                ctx.DrawText(button.text, b, textPrimary, 13.0f, WeightNormal, TextAlign::Center);
            }
            if (button.isDefault && hoveredButton_ == static_cast<int>(i)) {
                ctx.StrokeRect(b.Inflate(1.0f), palette.colors[static_cast<int>(Token::Accent)],
                               1.5f, 5.0f);
            }
        }
    }

    bool OnPointerDown(const Point& p) override {
        const Rect card = Bounds();
        if (!card.Contains(p)) {
            // Swallowed, not dismissed: for a "save before closing?" prompt there
            // is no safe guess about what a click outside meant.
            return true;
        }
        for (size_t i = 0; i < buttons_.size(); ++i) {
            if (ButtonBounds(i).Contains(p)) {
                Choose(static_cast<int>(i));
                return true;
            }
        }
        return true;
    }

    void OnPointerUp(const Point&) override {}
    void OnPointerMove(const Point& p) override {
        hoveredButton_ = -1;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            if (ButtonBounds(i).Contains(p)) hoveredButton_ = static_cast<int>(i);
        }
    }

    bool OnKeyDown(int vk, int) override {
        if (vk == KeyEscape) {
            // Escape maps to the cancel button, never the default. A prompt
            // whose Enter and Escape both meant "discard" would be a trap.
            for (size_t i = 0; i < buttons_.size(); ++i) {
                if (buttons_[i].isCancel) {
                    Choose(static_cast<int>(i));
                    return true;
                }
            }
            Close();
            return true;
        }
        if (vk == KeyReturn) {
            for (size_t i = 0; i < buttons_.size(); ++i) {
                if (buttons_[i].isDefault) {
                    Choose(static_cast<int>(i));
                    return true;
                }
            }
        }
        if (vk == KeyLeft || vk == KeyRight) {
            if (buttons_.empty()) return true;
            const int step = vk == KeyRight ? 1 : -1;
            hoveredButton_ = hoveredButton_ < 0
                                 ? 0
                                 : (hoveredButton_ + step +
                                    static_cast<int>(buttons_.size())) %
                                       static_cast<int>(buttons_.size());
            return true;
        }
        if (vk == KeyTab) {
            if (buttons_.empty()) return true;
            hoveredButton_ = (hoveredButton_ + 1) % static_cast<int>(buttons_.size());
            return true;
        }
        return true; // a modal consumes everything else: Tab must not escape
    }

    bool Active() const override { return active_; }
    bool LightDismiss() const override { return false; }

    // Test hooks.
    Rect ButtonBounds(size_t index) const {
        const Rect card = Bounds();
        const int count = static_cast<int>(buttons_.size());
        float totalW = 0;
        std::vector<float> widths(buttons_.size(), 0.0f);
        for (size_t i = 0; i < buttons_.size(); ++i) {
            widths[i] = std::max(kButtonMinWidth,
                                 TextWidth(buttons_[i].text, 13.0f, WeightNormal) + 28.0f);
            totalW += widths[i];
        }
        totalW += count > 1 ? static_cast<float>(count - 1) * 8.0f : 0.0f;
        float x = card.x + card.w - kDialogPad - totalW;
        // Buttons sit at the bottom-right, in the order given.
        const float y = card.y + card.h - kDialogPad - kButtonHeight;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            if (i == index) return Rect{x, y, widths[i], kButtonHeight};
            x += widths[i] + 8.0f;
        }
        return Rect{};
    }

    int HoveredButton() const { return hoveredButton_; }
    // Number of wrapped message lines, so a test can prove wrapping happened.
    size_t MessageLineCount() const { return messageLines_.size(); }

private:
    float MessageWidth() const { return kDialogWidth - kDialogPad * 2; }

    float CardHeight() const {
        float h = kDialogPad * 2 + kButtonHeight;
        if (!title_.empty()) h += 26.0f + 8.0f;
        h += static_cast<float>(messageLines_.size()) * 19.0f + 18.0f;
        return h;
    }

    // Greedy word wrap against the card width. Long words break by character so
    // a path or a file name cannot overflow the card.
    void WrapMessage() {
        messageLines_.clear();
        const float maxW = MessageWidth();
        std::wstring line;
        std::wstring word;

        auto fits = [&](const std::wstring& candidate) {
            return TextWidth(candidate, 13.0f, WeightNormal) <= maxW;
        };
        auto flushWord = [&] {
            if (word.empty()) return;
            if (line.empty()) {
                // A single word wider than the card: break it by character rather
                // than let it overflow. Breaking inside a word is ugly; drawing
                // outside the card is broken.
                if (!fits(word)) {
                    std::wstring chunk;
                    for (wchar_t ch : word) {
                        if (!chunk.empty() && !fits(chunk + ch)) {
                            messageLines_.push_back(chunk);
                            chunk.clear();
                        }
                        chunk += ch;
                    }
                    line = chunk;
                    word.clear();
                    return;
                }
                line = word;
                word.clear();
                return;
            }
            if (fits(line + L" " + word)) {
                line += L" ";
                line += word;
                word.clear();
                return;
            }
            messageLines_.push_back(line);
            line.clear();
            // Re-evaluate the word against the now-empty line.
            if (!fits(word)) {
                std::wstring chunk;
                for (wchar_t ch : word) {
                    if (!chunk.empty() && !fits(chunk + ch)) {
                        messageLines_.push_back(chunk);
                        chunk.clear();
                    }
                    chunk += ch;
                }
                line = chunk;
            } else {
                line = word;
            }
            word.clear();
        };

        for (wchar_t ch : message_) {
            if (ch == L'\n') {
                flushWord();
                messageLines_.push_back(line);
                line.clear();
                continue;
            }
            if (ch == L' ') {
                flushWord();
                continue;
            }
            word += ch;
        }
        flushWord();
        if (!line.empty()) messageLines_.push_back(line);
        if (messageLines_.empty()) messageLines_.push_back(L"");
    }

    void Choose(int index) {
        if (index < 0 || index >= static_cast<int>(buttons_.size())) return;
        const int result = buttons_[static_cast<size_t>(index)].result;
        Close();
        // Report AFTER closing so a handler that opens another dialog finds the
        // overlay slot free.
        if (onResult_) onResult_(result);
    }

    void Close() {
        if (!active_) return;
        active_ = false;
        if (onClosed_) {
            auto cb = onClosed_;
            onClosed_ = nullptr;
            cb();
        }
    }

    std::wstring title_;
    std::wstring message_;
    std::vector<DialogButton> buttons_;
    std::function<void(int)> onResult_;
    std::function<void()> onClosed_;
    Size available_{1920.0f, 1080.0f};
    bool active_ = true;
    int hoveredButton_ = -1;
    std::vector<std::wstring> messageLines_;

public:
    void Prepare() { WrapMessage(); }
};

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------
std::unique_ptr<OverlayLayer> MakeMenuLayer(std::vector<PopupMenuItem> items, Point anchor,
                                            std::function<void()> onClosed) {
    return std::make_unique<MenuLayer>(std::move(items), anchor, std::move(onClosed));
}

std::unique_ptr<OverlayLayer> MakeDialogLayer(std::wstring title, std::wstring message,
                                              std::vector<DialogButton> buttons,
                                              std::function<void(int)> onResult,
                                              std::function<void()> onClosed) {
    auto layer = std::make_unique<DialogLayer>(std::move(title), std::move(message),
                                               std::move(buttons), std::move(onResult),
                                               std::move(onClosed));
    layer->Prepare();
    return layer;
}

// Concrete handles so the host can size the layer to the window before showing.
// These keep the concrete types out of the header without exposing them.
void PrepareMenuLayerSizing(OverlayLayer* layer, Size available) {
    if (auto* menu = dynamic_cast<MenuLayer*>(layer)) menu->SetAvailable(available);
}

void PrepareDialogLayerSizing(OverlayLayer* layer, Size available) {
    if (auto* dialog = dynamic_cast<DialogLayer*>(layer)) dialog->SetAvailable(available);
}

void SetMenuHoverOnOpen(OverlayLayer* layer, bool on) {
    if (auto* menu = dynamic_cast<MenuLayer*>(layer)) menu->SetHoverOnOpen(on);
}

// ---------------------------------------------------------------------------
// FlyoutLayer
//
// Hosts a widget subtree. The interesting part is that it must forward events INTO the
// tree rather than interpret them itself: a flyout containing a button has to let the
// button be clicked, which means hit-testing the tree and calling its handlers -- while a
// press outside still dismisses the layer.
// ---------------------------------------------------------------------------
class FlyoutLayer final : public OverlayLayer {
public:
    explicit FlyoutLayer(FlyoutRequest request)
        : content_(request.content), anchor_(request.anchor),
          fixedSize_(request.size), lightDismiss_(request.lightDismiss),
          onClosed_(std::move(request.onClosed)), placement_(request.placement) {
        bounds_ = Rect{anchor_.x, anchor_.y, 0, 0};
        if (content_ == nullptr) {
            // An empty card beats silently doing nothing: a missing child is visible.
            fallback_ = CreateWidget("wui/StackPanel");
            content_ = fallback_.get();
        }
    }

    void DetachContent() override {
        content_ = nullptr;
        active_ = false;
    }

    Widget* Content() override { return content_; }

    void SetAvailable(Size available) {
        available_ = available;
        Relayout();
    }

    Rect Bounds() const override { return bounds_; }

    void Paint(PaintCtx& ctx, const ThemePalette& palette) override {
        if (!laied_) Relayout();
        // Shadow first, so a flyout over a same-coloured card is still locatable.
        ctx.FillRoundRect(bounds_.Inflate(2.0f), 9.0f, Color{0, 0, 0, 0.12f});
        ctx.FillRoundRect(bounds_, 8.0f, palette.colors[static_cast<int>(Token::CardBackground)]);
        ctx.StrokeRect(bounds_, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f, 8.0f);
        if (!content_) return;
        // The content paints in the layer's own space; PaintTree translates from its
        // bounds, which Relayout placed at the card's content origin.
        content_->PaintTree(ctx, palette);
    }

    bool OnPointerDown(const Point& p) override {
        if (bounds_.Contains(p)) {
            if (content_) {
                if (Widget* hit = content_->HitTest(p)) {
                    hit->OnPointerDown(p);
                    pressed_ = hit;
                }
            }
            return true;
        }
        // A press outside a light-dismiss flyout closes it AND consumes the click: the
        // user's intent was "get rid of this", not "activate what I hit".
        if (lightDismiss_) {
            Close();
            return true;
        }
        return true; // a modal flyout swallows the press
    }

    void OnPointerUp(const Point& p) override {
        if (pressed_ != nullptr) {
            Widget* target = pressed_;
            pressed_ = nullptr;
            target->OnPointerUp(p);
            return;
        }
        if (content_ && bounds_.Contains(p)) {
            if (Widget* hit = content_->HitTest(p)) hit->OnPointerUp(p);
        }
    }

    void OnPointerMove(const Point& p) override {
        if (!content_) return;
        // Hover state lives in the widgets, so a flyout holding a button must still
        // deliver moves or the button never highlights.
        if (Widget* hit = content_->HitTest(p)) hit->OnPointerMove(p);
    }

    bool OnKeyDown(int vk, int) override {
        // Escape closes without asking; the content is a popup's payload, not a document,
        // so there is nothing to lose by dismissing it.
        if (vk == KeyEscape) {
            Close();
            return true;
        }
        if (content_) {
            // No focus subsystem for overlays: the first focusable control in the content
            // takes the key. That covers a one-input flyout (the common case) without
            // inventing a focus stack the host would have to maintain.
            if (Widget* focus = FirstFocusable(content_)) {
                if (focus->OnKeyDown(vk)) return true;
            }
        }
        return false;
    }

    bool Active() const override { return active_; }
    bool LightDismiss() const override { return lightDismiss_; }

private:
    void Relayout() {
        laied_ = true;
        if (!content_) {
            bounds_ = Rect{anchor_.x, anchor_.y, 0, 0};
            active_ = false;
            return;
        }
        const bool measured = !(fixedSize_.w > 0 || fixedSize_.h > 0);
        const Size want = measured ? content_->Measure(Size{available_.w, available_.h})
                                   : fixedSize_;
        Size size{want.w > 0 ? want.w : 160.0f, want.h > 0 ? want.h : 40.0f};
        // A measured size is the CONTENT's size. The card adds padding around it,
        // so the padding has to be added to the card as well -- arranging the
        // content into size - 2*kPad squeezed its last row out of the card.
        if (measured) {
            size.w += kPad * 2;
            size.h += kPad * 2;
        }

        // Placement: below by default, flipping above when there is no room, which is what
        // every OS popup does and what makes a flyout near the bottom edge usable.
        Point origin{anchor_.x, anchor_.y};
        if (placement_ == 1) {
            origin.y = anchor_.y - size.h - kGap;
        } else if (placement_ == 2) {
            origin.x = anchor_.x + kGap;
            origin.y = anchor_.y;
        } else if (placement_ == 3) {
            origin.x = anchor_.x - size.w - kGap;
            origin.y = anchor_.y;
        } else {
            origin.y = anchor_.y + kGap;
            if (origin.y + size.h > available_.h && anchor_.y - size.h - kGap >= 0) {
                origin.y = anchor_.y - size.h - kGap; // flip above
            }
        }
        origin.x = std::clamp(origin.x, 0.0f, std::max(0.0f, available_.w - size.w));
        origin.y = std::clamp(origin.y, 0.0f, std::max(0.0f, available_.h - size.h));

        bounds_ = Rect{origin.x, origin.y, size.w, size.h};
        // The card's 8px padding is where the content lives.
        content_->Arrange(Rect{origin.x + kPad, origin.y + kPad,
                              std::max(0.0f, size.w - kPad * 2),
                              std::max(0.0f, size.h - kPad * 2)});
        active_ = true;
    }

    void Close() {
        active_ = false;
        if (onClosed_) {
            auto cb = onClosed_;
            onClosed_ = nullptr; // fire once: a second Close must not double-report
            cb();
        }
    }

    static constexpr float kGap = 6.0f;
    static constexpr float kPad = 8.0f;

    WidgetPtr fallback_;  // only when the caller supplied no content
    Widget* content_ = nullptr;  // borrowed; owned by the popup control
    Point anchor_;
    Size fixedSize_;
    Size available_{1280.0f, 720.0f};
    bool lightDismiss_ = true;
    bool laied_ = false;
    bool active_ = true;
    int placement_ = 0;
    Widget* pressed_ = nullptr;
    std::function<void()> onClosed_;
    Rect bounds_;
};

// ---------------------------------------------------------------------------
// ToolTipLayer
// ---------------------------------------------------------------------------
class ToolTipLayer final : public OverlayLayer {
public:
    ToolTipLayer(std::wstring text, Point anchor, Size available)
        : text_(std::move(text)), anchor_(anchor) {
        ITextService* svc = TextSystem::Get();
        const float lineH = svc ? svc->LineHeight(kFontSize) : kFontSize * 1.33f;
        float w = svc ? svc->Measure(text_, kFontSize, WeightNormal).w
                      : static_cast<float>(text_.size()) * kFontSize * 0.55f;
        w += kPadX * 2;
        const float h = lineH + kPadY * 2;
        // Offset below-right of the pointer, the way every tooltip behaves: directly under
        // the cursor it would cover what the user is looking at.
        Point origin{anchor.x + 12.0f, anchor.y + 20.0f};
        if (origin.x + w > available.w) origin.x = std::max(0.0f, available.w - w);
        if (origin.y + h > available.h) origin.y = std::max(0.0f, anchor.y - h - 8.0f);
        bounds_ = Rect{std::max(0.0f, origin.x), std::max(0.0f, origin.y), w, h};
        active_ = !text_.empty();
    }

    Rect Bounds() const override { return bounds_; }

    void Paint(PaintCtx& ctx, const ThemePalette& palette) override {
        ctx.FillRoundRect(bounds_.Inflate(1.0f), 5.0f, Color{0, 0, 0, 0.10f});
        ctx.FillRoundRect(bounds_, 4.0f,
                          palette.colors[static_cast<int>(Token::CardBackground)]);
        ctx.StrokeRect(bounds_, palette.colors[static_cast<int>(Token::CardBorder)], 1.0f, 4.0f);
        ctx.DrawText(text_, bounds_, palette.colors[static_cast<int>(Token::TextPrimary)],
                     kFontSize, WeightNormal, TextAlign::Center);
    }

    // A tooltip is not clickable and must not swallow clicks: if it consumed the press,
    // clicking a control while its tooltip was up would do nothing.
    bool OnPointerDown(const Point&) override { return false; }
    void OnPointerUp(const Point&) override {}
    bool OnKeyDown(int, int) override { return false; }
    bool Active() const override { return active_; }
    bool LightDismiss() const override { return false; }

private:
    static constexpr float kFontSize = 11.0f;
    static constexpr float kPadX = 8.0f;
    static constexpr float kPadY = 4.0f;

    std::wstring text_;
    Point anchor_;
    Rect bounds_;
    bool active_ = false;
};

std::unique_ptr<OverlayLayer> MakeFlyoutLayer(FlyoutRequest request) {
    auto layer = std::make_unique<FlyoutLayer>(std::move(request));
    layer->SetAvailable(Size{1280.0f, 720.0f});
    return layer;
}

std::unique_ptr<OverlayLayer> MakeToolTipLayer(std::wstring text, Point anchor,
                                               Size available) {
    return std::make_unique<ToolTipLayer>(std::move(text), anchor, available);
}

void PrepareFlyoutLayerSizing(OverlayLayer* layer, Size available) {
    if (auto* flyout = dynamic_cast<FlyoutLayer*>(layer)) flyout->SetAvailable(available);
}

} // namespace wui
