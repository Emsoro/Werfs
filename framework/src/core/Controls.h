// SPDX-License-Identifier: MIT
// Built-in control set: Page, StackPanel, Border, TextBlock, Button, CheckBox,
// TextBox, ListBox, ScrollViewer (+ DataTemplate).
#pragma once

#include "Widget.h"

#include <functional>

namespace wui {

class TextBlock : public Widget {
public:
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Size Measure(const Size& available) override;
};

class Border : public Widget {
public:
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
};

class Page : public Widget {
public:
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void Arrange(const Rect& finalRect) override;
};

class StackPanel : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

class Button : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

private:
    void Invoke();
};

class CheckBox : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

private:
    void Toggle();
};

class TextBox : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    bool OnKeyDown(int vk) override;
    bool OnChar(wchar_t ch) override;
    bool Focusable() const override { return true; }
    void SetCaretToEnd();
    // Test hook: caret position for assertions.
    size_t CaretIndex() const { return caret_; }

private:
    void CommitText();
    int slotText_ = -1;
    size_t caret_ = 0;
    bool caretVisible_ = true;
    std::wstring editing_;
};

// A single-child viewport that scrolls its content vertically.
//
// Why this exists: a gallery page is a long vertical stack of example cards, so
// without scroll the page is simply cut off at the window edge. The control
// keeps the child's natural height, clips to its own bounds, and shifts the
// content up by `scrollOffset`.
//
// It renders its scrollbar itself (PaintCtx has no platform scrollbar), and
// handles the wheel rather than relying on the host to page the content.
class ScrollViewer : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    bool OnPointerWheel(float deltaLines) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

    void ScrollTo(float offset);
    float ScrollOffset() const { return offset_; }
    float MaxScroll() const { return maxScroll_; }
    // Test/profiling hook: how far the content extends past the viewport.
    float ContentHeight() const { return contentHeight_; }

private:
    float offset_ = 0;
    float contentHeight_ = 0;
    float maxScroll_ = 0;
};

// ---------------------------------------------------------------------------
// ListBox
// ---------------------------------------------------------------------------
class ListBox : public Widget, public IListObserver {
public:
    ListBox();
    ~ListBox() override;

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;

    void OnListChanged(const ListChange& change) override;
    void OnSlotChanged(int slot, const Value& value) override;

    size_t ItemCount() const { return itemWidgets_.size(); }
    Widget* ItemWidgetAt(size_t i) const { return i < itemWidgets_.size() ? itemWidgets_[i].get() : nullptr; }
    void RebuildItems(ITextService* text);

private:
    void SubscribeSource();
    void UnsubscribeSource();

    ObservableListBase* list_ = nullptr;
    RefPtr listRef_;
    std::shared_ptr<DataTemplate> template_;
    std::vector<WidgetPtr> itemWidgets_;
    ITextService* text_ = nullptr;
    int slotItems_ = -1;
    int slotTemplate_ = -1;
};

} // namespace wui
