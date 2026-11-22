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
// ---------------------------------------------------------------------------
// Selection
//
// A Selector owns "which one is current" and lets it be bound two ways. Kept
// separate from ListBox because TabView needs exactly the same semantics, and
// because ComboBox will need it again -- three controls each inventing their own
// SelectedIndex is how they end up disagreeing about -1.
//
// The index is the single source of truth. SelectedItem is derived from it, so
// the two can never drift apart: setting an item resolves to its index, and
// setting an index resolves to an item.
// ---------------------------------------------------------------------------
class Selector : public Widget {
public:
    // -1 means "nothing selected", matching the usual convention.
    int SelectedIndex() const { return selectedIndex_; }
    void SetSelectedIndex(int index);

    RefPtr SelectedItem() const;

    // Number of selectable entries. Derived classes report their own.
    virtual size_t ItemCount() const { return 0; }
    virtual RefPtr ItemAt(size_t index) const { return nullptr; }

    // Called after the selection changes, so a subclass can repaint or re-layout.
    virtual void OnSelectionChanged() {}

    void OnSlotChanged(int slot, const Value& value) override;

protected:
    // Clamps to [-1, ItemCount()-1]. Returns the value actually applied.
    int ClampIndex(int index) const;

    int selectedIndex_ = -1;
    int slotSelectedIndex_ = -1;
    int slotSelectedItem_ = -1;
    bool settingIndex_ = false; // Guards the index <-> item echo.
};

class ListBox : public Selector, public IListObserver {public:
    ListBox();
    ~ListBox() override;

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;

    void OnListChanged(const ListChange& change) override;
    void OnSlotChanged(int slot, const Value& value) override;

    size_t ItemCount() const override;
    RefPtr ItemAt(size_t index) const override;
    void OnSelectionChanged() override;
    bool OnKeyDown(int vk) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool Focusable() const override { return true; }

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

// ---------------------------------------------------------------------------
// TabView
//
// A tab strip plus a content area. The strip is drawn by this control rather than
// built from child widgets, because tabs are data-driven (ItemsSource + template)
// and the content area holds exactly one item at a time.
//
// Deliberately NOT dependent on popups: overflow is handled by scrolling the
// strip horizontally, so tabs work before the overlay subsystem exists (§10c.4
// batch B). A dropdown list for overflow can be added later without changing the
// model.
// ---------------------------------------------------------------------------
// Lets any object supply a tab title without being a TabEntryModel. A sample that
// already has a document class should not have to inherit from a framework type
// just to be listed in a tab strip -- and pulling in the binding machinery here
// would couple the controls layer to the MVVM layer.
class IBoundTitle {
public:
    virtual ~IBoundTitle() = default;
    virtual std::wstring BoundTitle() const = 0;
};

// One tab's data. A plain struct would do for the fixed case, but tabs come from
// an ObservableList and the UI needs to react when a title changes (rename a
// document) or the dirty flag flips -- so the model is observable like every
// other bound object.
class TabEntryModel : public RefObject, public IBoundTitle {
public:
    std::wstring BoundTitle() const override { return title_; }
public:
    std::wstring Title() const { return title_; }
    void SetTitle(std::wstring title) { title_ = std::move(title); }

    bool IsClosable() const { return closable_; }
    void SetClosable(bool closable) { closable_ = closable; }

    // Drives the unsaved marker on the tab strip.
    bool IsDirty() const { return dirty_; }
    void SetDirty(bool dirty) { dirty_ = dirty; }

    void SetContent(RefPtr content) { content_ = std::move(content); }
    RefPtr Content() const { return content_; }

private:
    std::wstring title_ = L"Tab";
    bool closable_ = true;
    bool dirty_ = false;
    RefPtr content_;
};

class TabView : public Selector, public IListObserver {
public:
    TabView();
    ~TabView() override;

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    bool OnKeyDown(int vk) override;

    void OnListChanged(const ListChange& change) override;
    void OnSlotChanged(int slot, const Value& value) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;

    size_t ItemCount() const override;
    RefPtr ItemAt(size_t index) const override;
    void OnSelectionChanged() override;

    // Tab strip geometry, exposed for tests and for hit testing the close button.
    Rect StripBounds() const;
    Rect TabBounds(size_t index);
    Rect CloseButtonBounds(size_t index);
    Rect AddButtonBounds();

    // Clicking the × does not select; it requests a close. The host or ViewModel
    // decides whether to actually remove the tab, because only it knows about
    // unsaved changes.
    // Exposed for tests: the label the strip would draw for a tab.
    std::wstring TitleForTesting(size_t index) { return TitleFor(index); }

    void RequestClose(size_t index);
    int CloseRequested() const { return closeRequested_; }
    void ClearCloseRequest() { closeRequested_ = -1; }

private:
    void SubscribeSource();
    void UnsubscribeSource();
    void PushAddCommand();

    ObservableListBase* list_ = nullptr;
    RefPtr listRef_;
    ITextService* text_ = nullptr;
    int slotItems_ = -1;
    int slotAddCommand_ = -1;

    // Content widgets, parallel to items. Either supplied by TabEntryModel or by
    // TabViewItem children in markup.
    Widget* TabContentAt(size_t index);
    void EnsureTabWidths();
    std::wstring TitleFor(size_t index);
    float TabWidthFor(size_t index);

    // Set when a close was requested; read by the host after the input pass.
    int closeRequested_ = -1;

    // Measured widths, recomputed on layout so hit testing matches painting.
    std::vector<float> tabWidths_;
    float stripScroll_ = 0.0f;
    float contentTop_ = 0.0f;
};

} // namespace wui
