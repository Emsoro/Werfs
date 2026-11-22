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

private:
    // Filled by Measure when TextWrapping="Wrap"; empty for single-line text.
    std::vector<std::wstring> wrappedLines_;
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
    // Claims hits inside its own viewport even when no child lands there.
    // Without this the base class returns null for gaps between children, so a
    // wheel over blank space fell through to the page and never reached the
    // scroller.
    Widget* HitTest(const Point& p) override;
    bool OnPointerWheel(float deltaLines) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

    void ScrollTo(float offset);
    float ScrollOffset() const { return offset_; }
    float MaxScroll() const { return maxScroll_; }
    // Test/profiling hook: how far the content extends past the viewport.
    float ContentHeight() const { return contentHeight_; }

protected:
    // Clips to the viewport and shifts content up by offset_.
    void PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) override;
    // Lets Widget::ToLocal undo the scroll when converting pointer coordinates.
    float ScrollShiftForPointers() const override { return offset_; }

private:
    float offset_ = 0;
    float contentHeight_ = 0;
    float maxScroll_ = 0;
};

// ---------------------------------------------------------------------------
// Canvas: absolute positioning. Children place themselves with Left/Top.
// ---------------------------------------------------------------------------
class Canvas : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// ---------------------------------------------------------------------------
// Grid: rows and columns with per-child spanning.
//
// Columns are described by ColumnDefinitions ("1*,2*,1*") and rows by
// RowDefinitions ("Auto,*,80"). A child picks its cell with Grid.Row /
// Grid.Column / Grid.RowSpan / Grid.ColumnSpan, written as properties on the
// child itself because this parser has no attached-property syntax.
// ---------------------------------------------------------------------------
struct GridLength {
    float value = 1;   // pixels for Fixed, weight for Star, ignored for Auto
    enum class Kind { Auto, Star, Fixed } kind = Kind::Star;
};

class Grid : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;

    // Exposed for tests: resolved track sizes from the last layout pass.
    const std::vector<float>& ColumnOffsets() const { return colOffsets_; }
    const std::vector<float>& RowOffsets() const { return rowOffsets_; }

private:
    void ParseDefinitions();
    std::vector<GridLength> columns_;
    std::vector<GridLength> rows_;
    std::vector<float> colOffsets_;
    std::vector<float> rowOffsets_;
    bool parsed_ = false;
    std::wstring parsedCols_;
    std::wstring parsedRows_;
};

// ---------------------------------------------------------------------------
// WrapPanel: flows children along one axis and wraps to the next line.
// ---------------------------------------------------------------------------
class WrapPanel : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// ---------------------------------------------------------------------------
// VariableSizedWrapGrid: flows children, and a child may span several cells.
// ---------------------------------------------------------------------------
class VariableSizedWrapGrid : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;

    // Where a child sits in the cell grid.
    struct Cell {
        int row = 0;
        int col = 0;
        int colSpan = 1;
        int rowSpan = 1;
    };

private:
    // Assigns cells honouring spans; used by both Measure and Arrange so the two
    // passes can never disagree about placement.
    std::vector<Cell> PlanCells(int perRow) const;
};

// ---------------------------------------------------------------------------
// Viewbox: scales its single child to fit the available space.
//
// PaintCtx has no transform stack, so this scales by shrinking the child's
// arranged rect -- correct for the uniform "fit the box" case the gallery needs.
// ---------------------------------------------------------------------------
class Viewbox : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// ---------------------------------------------------------------------------
// Expander: a header plus a body that shows or hides.
// ---------------------------------------------------------------------------
class Expander : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

    bool IsExpanded() const { return expanded_; }
    void SetExpanded(bool value);
    // Test hook: the header strip is what the user clicks.
    Rect HeaderBounds() const;

private:
    bool expanded_ = false;
};

// ---------------------------------------------------------------------------
// ListBox
// ---------------------------------------------------------------------------
class ListBox : public Widget, public IListObserver {public:
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
