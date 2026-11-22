// SPDX-License-Identifier: MIT
// Built-in control set: Page, StackPanel, Border, TextBlock, Button, CheckBox,
// TextBox, ListBox, ScrollViewer, TabView (+ DataTemplate) and the application
// shell pieces: MenuBar, CommandBar and ContentDialog.
#pragma once

#include "Widget.h"

#include <functional>
#include <memory>
#include <vector>

namespace wui {

struct PopupMenuItem;

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
    Size Measure(const Size& available) override;
    // Stacks children vertically; the last one absorbs the leftover height.
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
    void Arrange(const Rect& finalRect) override;
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
    // Splits the current text into display lines. A single-line box keeps one
    // entry; a box taller than one line wraps on explicit newlines and then on
    // width, so a document paragraph is readable instead of running off the edge.
    std::vector<std::wstring> BuildDisplayLines(float wrapWidth, float fontSize, int weight) const;
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

    // Which tab the user asked to close. Published as a bindable property so the
    // ViewModel hears it: a control cannot know whether there is unsaved work,
    // and a close that happened without asking is data loss.
    int closeRequested_ = -1;
    int slotCloseRequested_ = -1;

    // Measured widths, recomputed on layout so hit testing matches painting.
    std::vector<float> tabWidths_;
    float stripScroll_ = 0.0f;
    float contentTop_ = 0.0f;
};

// ---------------------------------------------------------------------------
// MenuBar / MenuBarItem
//
// A horizontal set of titles, each of which drops a menu. The bar does not own
// the item data: a MenuBarItem builds its model from its own child elements once,
// at load time, and the bar only knows where the titles are and which is open.
//
// Why the menu itself is an overlay (not a child): see OverlayLayer. The bar can
// sit anywhere -- including inside a scroller -- and its dropdown must still paint
// over everything.
// ---------------------------------------------------------------------------
class MenuBarItem;

class MenuBar : public Widget {
public:
    MenuBar();
    ~MenuBar() override;

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerMove(const Point& local) override;
    bool Focusable() const override { return true; }

    // Title geometry, shared by painting and hit testing.
    Rect TitleBounds(size_t index) const;

    // Opens the menu of `index` at the title's bottom-left. Exposed so a test can
    // open a menu without guessing coordinates.
    void OpenMenu(size_t index);
    void CloseMenu();
    int OpenIndex() const { return openIndex_; }

    // The items of a title, in the form the menu layer draws. Built by
    // MenuBarItem::BuildItems() once and refreshed for enabled state on open.
    std::vector<class MenuBarItem*> Items() const;

private:
    // Retires the layer instead of destroying it.
    //
    // A layer closes itself from INSIDE its own OnPointerDown (the item was chosen),
    // and the close callback is what tells the owner. Destroying `ownedLayer_` there
    // would free the object whose method is still on the stack -- a use-after-free
    // that only shows up as a crash on the next access. So closing only moves the
    // layer here, and it is actually released at the next safe point (the next open,
    // or destruction). One frame of extra retained memory is the whole cost.
    void RetireLayer();

    void UpdateWidths();
    std::vector<float> titleWidths_;
    int openIndex_ = -1;
    OverlayLayer* layer_ = nullptr;
    std::unique_ptr<OverlayLayer> ownedLayer_;
    std::unique_ptr<OverlayLayer> retiredLayer_;
};

// One title in a MenuBar. Owns its children as item descriptors; the actual
// PopupMenuItem tree is built on demand so CanExecute is read at open time.
class MenuBarItem : public Widget {
public:
    void OnPaint(PaintCtx&, const ThemePalette&) override {}
    Size Measure(const Size& available) override;

    // RETURNED BY VALUE, deliberately. Returning `const std::wstring&` from a
    // subclass whose title lives in a property slot means binding a reference to a
    // temporary converted string -- a dangling reference the moment the expression
    // ends. It survived long enough to be measured once and crashed on the next
    // pass, which is exactly the kind of bug that looks like "layout is broken".
    virtual std::wstring MenuTitle() const = 0;
    // Builds the menu content. Called once per open, so command enabled state is
    // current and a command created after load still appears.
    virtual std::vector<struct PopupMenuItem> BuildItems() = 0;
};

// A concrete menu container used in markup:
//
//   <MenuBarItem Title="File">
//     <MenuFlyout>
//       <MenuFlyoutItem Text="New" Hint="Ctrl+N" Command="{Binding NewCommand}"/>
//       <MenuFlyoutSeparator/>
//       <MenuFlyoutSubItem Text="Export"> ... </MenuFlyoutSubItem>
//     </MenuFlyout>
//   </MenuBarItem>
//
// The <MenuFlyout> level exists because it is the WinUI shape authors already
// know, and because it gives a place for a future MenuFlyout placement property.
class MenuFlyoutItem;
class MenuFlyoutSeparator;
class MenuFlyoutSubItem;

class MenuFlyout : public Widget {
public:
    void OnPaint(PaintCtx&, const ThemePalette&) override {}
    Size Measure(const Size& available) override;
    std::vector<PopupMenuItem> BuildItems();
};

class MenuBarItemImpl final : public MenuBarItem {
public:
    // By value: see MenuBarItem::MenuTitle. Title() returns a slot-decoded
    // temporary, so returning a reference to it would dangle.
    std::wstring MenuTitle() const override { return Title(); }
    std::wstring Title() const { return Get<std::wstring>(SlotOf("Title")); }
    std::vector<PopupMenuItem> BuildItems() override;
};

// A single clickable command row.
class MenuFlyoutItem : public Widget {
public:
    void OnPaint(PaintCtx&, const ThemePalette&) override {}
    Size Measure(const Size& available) override;
    PopupMenuItem Build();
};

class MenuFlyoutSeparator : public Widget {
public:
    void OnPaint(PaintCtx&, const ThemePalette&) override {}
    Size Measure(const Size& available) override;
};

class MenuFlyoutSubItem : public Widget {
public:
    void OnPaint(PaintCtx&, const ThemePalette&) override {}
    Size Measure(const Size& available) override;
    PopupMenuItem Build();
};

// ---------------------------------------------------------------------------
// CommandBar / AppBarButton
//
// A horizontal row of icon buttons. Overflow is the reason this is not just a
// StackPanel: when the window narrows, the trailing buttons collect behind a
// single "..." that opens them as a menu. That behaviour is the part with rules
// (which buttons collapse, what happens when even "..." does not fit), so it is
// a control rather than a layout the sample arranges by hand.
// ---------------------------------------------------------------------------
class CommandBar : public Widget {
public:
    CommandBar();
    ~CommandBar() override;

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    void OnPointerMove(const Point& local) override;

    bool IsOverflowing() const { return overflow_ > 0; }
    // How many trailing buttons are hidden behind the overflow button.
    int OverflowCount() const { return overflow_; }
    Rect ButtonBounds(size_t index) const;
    Rect OverflowBounds() const;

    // Test/automation hook: opens the overflow menu.
    void OpenOverflow();

private:
    // Buttons are the CommandBar's child AppBarButtons plus separators. Widths are
    // resolved in one pass, exactly like the tab strip, so paint and hit testing
    // cannot disagree.
    void UpdateLayoutOfButtons();
    std::vector<WidgetPtr> Buttons() const;
    float ButtonWidth(const Widget& button) const;

    std::vector<float> widths_;
    float naturalWidth_ = 0.0f;
    int overflow_ = 0;
    std::unique_ptr<OverlayLayer> ownedLayer_;
    // See MenuBar::RetireLayer: a layer that closed itself is kept alive one more
    // beat so its own stack frame can unwind safely.
    std::unique_ptr<OverlayLayer> retiredLayer_;
};

// One toolbar button: glyph + optional label. Reports itself through Command, so
// a toolbar and a menu item can drive the same ViewModel action.
class AppBarButton : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool Focusable() const override { return true; }
    bool OnKeyDown(int vk) override;

    void Invoke();
    // What the button looks like in an overflow menu.
    PopupMenuItem BuildItem();
    float ToolbarWidth() const;
    bool IsSeparator() const;
};

// ---------------------------------------------------------------------------
// ContentDialog
//
// Declarative modal. The element supplies the text and the button captions, and
// publishes the chosen result through a bindable `Result` slot, so a ViewModel
// binds to it exactly as it binds to SelectedIndex on a TabView.
//
// Shown by the host when `IsOpen` becomes true; the host owns the overlay so a
// dialog declared inside a scroller still covers the window.
// ---------------------------------------------------------------------------
class ContentDialog : public Widget {
public:
    ContentDialog();
    ~ContentDialog() override;

    void OnPaint(PaintCtx&, const ThemePalette&) override {}
    Size Measure(const Size& available) override;
    void OnSlotChanged(int slot, const Value& value) override;

    bool IsOpen() const { return open_; }
    void Show();
    void Close(int result);
    // The chosen button's Result, published for binding.
    int ResultValue() const { return result_; }

    static constexpr int ResultNone = -100;

private:
    bool open_ = false;
    int result_ = ResultNone;
    int slotIsOpen_ = -1;
    int slotResult_ = -1;
    int slotPrimary_ = -1;
    int slotSecondary_ = -1;
    std::unique_ptr<OverlayLayer> ownedLayer_;
    // See MenuBar::RetireLayer: a layer that closed itself is kept alive one more
    // beat so its own stack frame can unwind safely.
    std::unique_ptr<OverlayLayer> retiredLayer_;
};

} // namespace wui
