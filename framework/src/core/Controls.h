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

// ---------------------------------------------------------------------------
// Status controls: ProgressBar, ProgressRing, LoadingSpinner, InfoBar, InfoBadge
// ---------------------------------------------------------------------------

// A determinate or indeterminate horizontal bar.
//
// `Value` is a 0..1 FRACTION, not a raw count, because a bar has no idea what it is
// measuring. Callers do the division once instead of every control inventing its own
// Minimum/Maximum/Value triple; those belong to a future Slider-like numeric base.
class ProgressBar : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// An arc that fills clockwise from 12 o'clock. Drawn from short segments so no
// backend needs a path language (see PaintCtx::DrawLine).
class ProgressRing : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// An INDETERMINATE ("waiting") animation: no value to report, just motion that
// says "still working". Four mainstream shapes share one control because the
// clock that drives them is the same -- a `Phase` slot in cycles, written by
// the page's frame callback (or by a binding). The control never reads a clock
// itself: a still frame must be reproducible, which is why the snapshot tool
// can freeze a spinner by simply not advancing Phase.
//
//   Ring   a bright comet head fading around a segment ring (Windows-style)
//   Bars   four vertical bars whose lengths run as a wave
//   Dots   five balls in a row; the fifth sweeps a semicircle above the queue and lands first
//   Bounce three dots bouncing in sequence (the classic typing indicator)
//
// Everything is drawn from FillRoundRect / FillEllipse / DrawLine, so every
// backend (D2D, software raster, recording) renders the same four shapes.
class LoadingSpinner : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// An inline message strip: a coloured left edge, a title, an optional message.
//
// Severity is an enum rather than four booleans because a bar is one of four things,
// never two. Ordering is deliberately Informational first so the default value 0 is
// the least alarming one.
class InfoBar : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    // Not focusable, but clickable: clicking the close affordance is the only
    // interaction, and it reports through IsOpen so a ViewModel stays in charge.
    void OnPointerUp(const Point& local) override;

private:
    Rect CloseBounds() const;
};

// A small count bubble. Sized for up to two digits and then grows, so "9" and "99"
// do not jump to wildly different widths.
class InfoBadge : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// ---------------------------------------------------------------------------
// Shapes: Line, Shape
// ---------------------------------------------------------------------------

// A straight segment defined by its endpoints in the parent's coordinate space.
//
// Line takes StartX/StartY/EndX/EndY rather than a start point plus a size, because a
// diagonal has no meaningful width/height pair. Its desired size is the bounding box,
// so it participates in layout like any other element.
class Line : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// A filled or outlined rectangle/ellipse primitive.
//
// A single Shape with a Kind enum rather than one class per geometry: the layout and
// paint paths are identical, and Splitting them would duplicate five methods to change
// two lines. Rectangles and ellipses cover what an SVG-free interface needs.
class Shape : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
};

// ---------------------------------------------------------------------------
// Toggles: ToggleButton, ToggleSwitch, HyperlinkButton
// ---------------------------------------------------------------------------

// A button that stays down. IsChecked is TwoWay for the same reason CheckBox's is:
// clicking it is the user changing the value, and the ViewModel has to hear about it.
class ToggleButton : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

private:
    void Toggle();
};

// An on/off switch with a sliding knob.
//
// A separate control from ToggleButton rather than a style of it: the switch is
// mostly a knob whose position carries the state, and sharing one paint path would
// mean branching on which shape to draw in every branch of the code.
class ToggleSwitch : public Widget {
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

// Text that behaves like a link: accent colour, underline on hover, and it forwards
// clicks to a command. No navigation of its own -- the ViewModel decides where to go.
class HyperlinkButton : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    void OnPointerEnter() override;
    void OnPointerLeave() override;
    bool Focusable() const override { return true; }

private:
    void Invoke();
};

// A horizontal slider.
//
// Value is a real number in [Minimum, Maximum] -- unlike ProgressBar's fraction,
// because a slider's range is part of what it means (volume 0..100). Clicking anywhere
// on the track jumps there, which is what users expect and what dragging needs anyway.
class Slider : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerMove(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

private:
    // Fraction of the track this value sits at, 0..1.
    float Fraction() const;
    void SetFromX(float x);
    void Step(int direction);
    Rect Track() const;
    bool dragging_ = false;
};

// A button that fires repeatedly while held.
//
// The host has no timer, so "while held" is expressed as repeated OnPointerMove
// notifications plus a press count that the control exposes: a click gives one, holding
// gives more as the pointer jitters. It is deliberately honest about being
// click-count based rather than time based -- a real repeat needs a frame clock, which
// is the animation iteration's job (see CODE_PLAN §10b.2).
class RepeatButton : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool Focusable() const override { return true; }

private:
    void Invoke();
};

// A radio button: mutually exclusive within its group.
//
// Group membership is by GroupName across the WHOLE tree, not by parent, because a
// group of radios is routinely split across several panels ("all three in a row,
// wrapped onto the next line"). Scoping to the direct parent would make re-laying-out a
// form silently change which radios are exclusive with each other.
class RadioButton : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

    // Unchecks every other radio in the same group. Public so a ViewModel-driven
    // IsChecked=True can also settle the group without a click.
    void Select();

private:
    void Toggle();
};

// A layout panel that positions children by their relationship to each other or to the
// panel itself.
//
// This is the only panel whose placement data lives on the CHILD (RelativePanel.RightOf
// ="other"), so it needs two things nothing else did: attached-property names the
// validator accepts on any widget, and a way to resolve an x:Name to a sibling widget at
// layout time. Both are implemented here rather than in the parser so the cost is paid
// only by the panel that needs it.
class RelativePanel : public Widget {
public:
    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;

private:
    // Positions one child, following its RightOf/LeftOf/Above/Below/AlignX anchors.
    // Recursive in effect (a child may be anchored to another anchored child) but
    // guarded by a visited set, because two children anchored to each other is a
    // mistake that must not recurse forever.
    Rect PlaceChild(size_t index, int depth);
    Size MeasureChild(size_t index, const Size& available);
    // Resolves "other" to an index in children_, or npos when it names nothing.
    size_t IndexOfName(const std::string& name) const;
    std::vector<Size> measured_;
    std::vector<Rect> placed_;
    std::vector<bool> done_;
};

// A picture.
//
// The source is decoded through IImageService, which is injectable: the headless harness
// installs a fake so layout and paint can be tested with no COM and no files. When the
// service is absent or returns nothing, the control paints a placeholder at its declared
// size -- a missing image is a blank area, never a crash or a zero-sized hole.
class Image : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    // Re-decodes after Source changes. Also the entry point a test uses to install a
    // bitmap without a service.
    void OnSlotChanged(int slot, const Value& value) override;

private:
    void EnsureDecoded();
    std::shared_ptr<const Bitmap> bitmap_;
    std::wstring decodedSource_;
};

// A circular avatar with initials or a picture.
//
// Sized from a single Diameter rather than Width/Height: a person picture that is not
// round looks like a bug, so the control does not offer the choice. Initials are derived
// from DisplayName when there is no image, which is what makes an empty avatar still
// readable.
class PersonPicture : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void OnSlotChanged(int slot, const Value& value) override;

    // First letter of the first two words, upper-cased. Public so it can be tested.
    static std::wstring Initials(const std::wstring& displayName);

private:
    void EnsureDecoded();
    std::shared_ptr<const Bitmap> bitmap_;
    std::wstring decodedSource_;
};

// ---------------------------------------------------------------------------
// Popups: Popup, Flyout, ToolTip
// ---------------------------------------------------------------------------

// The base every "opens a floating card" control shares.
//
// What it owns is exactly the part that is identical in all of them and that is easy to get
// subtly wrong: taking the declared content out of the tree, installing it as an overlay,
// and closing again through ONE path when the layer dismisses itself. Flyout and ComboBox
// both carried their own copy of that logic, including the same flag that could drift and
// leave a control permanently unopenable (CODE_PLAN §7.25); it lives here once now.
//
// Subclasses decide the appearance and WHEN to open.
class PopupBase : public Widget {
public:
    Widget* HitTest(const Point& p) override;
    void OnPointerEnter() override;
    void OnPointerLeave() override;
    void OnSlotChanged(int slot, const Value& value) override;

    bool IsOpen() const;
    // Installs `content` in an overlay anchored to this control. Ownership transfers.
    void OpenWith(WidgetPtr content, Size size = Size{0, 0}, int placement = 0);
    void ClosePopup();

protected:
    bool OwnsLayer() const { return ownLayer_; }
    // Where the card goes, in window space. Default: just below this control.
    virtual Point AnchorPoint() const;
    // Size the card should take; zero means "whatever the content wants".
    virtual Size PopupSize() const { return Size{0, 0}; }
    // Builds the content for a SLOT-DRIVEN open (a ViewModel writing IsOpen=true). Returning
    // null means this control cannot open that way. The base needs this hook because it can
    // clear the flag on close, but only a subclass knows what belongs in the card -- without
    // it, binding IsOpen would silently do nothing.
    virtual WidgetPtr BuildContentForSlotOpen() { return nullptr; }
    // Called right after the layer dismissed itself and the flag was cleared.
    virtual void OnPopupClosed() {}

private:
    bool ownLayer_ = false;
    // The declared card stays owned by the control for its whole life. The layer
    // only borrows it, so closing does not destroy it and reopening shows it again.
    WidgetPtr savedContent_;
    OverlayLayer* layer_ = nullptr;  // last installed layer; detached when replaced
};

// A bare popup: a button-like surface that shows whatever content you declare in it.
//
// This is the primitive the others could have been built from, kept as its own control
// because "open this card next to this thing" is a legitimate thing to want with no further
// opinion about filtering or selection.
class Popup : public PopupBase {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

private:
    WidgetPtr BuildContentForSlotOpen() override;
};

// A button that opens a popup containing real controls.
//
// The popup content is declared as the flyout's child in markup. It is reparented into the
// overlay when opened -- it cannot stay in the tree, because a closed flyout would then
// still be measured by its parent's layout and clipped by any ScrollViewer above it.
//
// IsOpen is TwoWay so a ViewModel can open it and so the layer can report that the user
// dismissed it.
class Flyout : public PopupBase {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }

private:
    void Toggle();
    WidgetPtr BuildContentForSlotOpen() override;
};

// Shows a short label when the pointer is over its content.
//
// Not an overlay, deliberately. A tooltip has to appear next to the element and vanishes
// when the pointer leaves; routing it through the overlay subsystem would mean the layer
// steals the hover state that decides when to hide it, and a tooltip that closes on
// pointer-press would flicker away exactly when the user acts. Overlays are for things the
// user must interact with; this one only labels.
class ToolTip : public Widget {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerEnter() override;
    void OnPointerLeave() override;
};

// A drop-down selector.
//
// Items are a plain string list rather than a templated collection: a combo box shows one
// string per row and that is the whole contract. A real item template needs the
// virtualising collection subsystem (CODE_PLAN P2), and pretending to support it here would
// ship a control whose object list silently ignores its template.
//
// SelectedIndex is TwoWay -- picking a row IS the user choosing -- and SelectedText is
// derived so markup can bind either one.
// The row list a ComboBox drops down.
//
// A widget rather than a menu model because it needs per-row hit testing and a distinct
// "currently selected" look, and the menu layer's row model has neither. Public so the
// flyout can host it and tests can drive it directly.
class FlyoutList : public Widget {
public:
    void Configure(std::vector<std::wstring> items, int selected,
                   std::function<void(int)> onChosen);
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    void OnPointerMove(const Point& local) override;
    void OnPointerUp(const Point& local) override;

    // Index the pointer is over, or -1.
    int HoveredIndex() const { return hovered_; }
    // The row painted as selected. A keyboard highlight must update this too,
    // otherwise Enter commits one row while the list keeps painting another.
    int Selected() const { return selected_; }
    void SetSelected(int index) { selected_ = index; Invalidate(AffectsRender); }

private:
    int IndexAt(const Point& local) const;

    std::vector<std::wstring> items_;
    int selected_ = -1;
    int hovered_ = -1;
    std::function<void(int)> onChosen_;
};

class ComboBox : public PopupBase {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }
    void OnSlotChanged(int slot, const Value& value) override;

    // The items, split on ';'. Public so the parser and tests agree on the separator.
    static std::vector<std::wstring> SplitItems(const std::wstring& items);

protected:
    // The list is exactly as wide as the box and sits under it.
    Point AnchorPoint() const override;
    Size PopupSize() const override;
    WidgetPtr BuildContentForSlotOpen() override;

private:
    void OpenDropDown();
    void ApplySelection();
    Rect DropDownBounds() const;

    bool ownLayer_ = false;
    bool hoveredRow_ = false;
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

    // --- IME composition ---------------------------------------------------
    //
    // A composition is the text an IME is still building ("zhong" on the way to 中). It is
    // a PREVIEW: it is drawn in the field, but it is NOT written to the Text slot, so
    // bindings and the ViewModel never see a half-typed candidate. Only CommitComposition
    // produces an edit.
    //
    // These are driven by the host from WM_IME_COMPOSITION. They are public so a headless
    // test can exercise the whole lifecycle with no IME installed.
    void SetComposition(const std::wstring& text) override;
    void CommitComposition(const std::wstring& finalText) override;
    void CancelComposition() override;
    bool Composing() const override { return !composition_.empty(); }
    CompositionCaret ImeCaret() const override;
    // Test hook: what the field is previewing.
    const std::wstring& CompositionText() const { return composition_; }

protected:
    // The text as it should be DISPLAYED. TextBox returns Text unchanged; PasswordBox
    // overrides it. Splitting display from value here is what lets the mask stay a
    // paint concern while the editing core keeps working on real characters -- if the
    // mask were stored in the slot, editing would fight it on every keystroke.
    virtual std::wstring DisplayText() const;

private:
    void CommitText();
    // Splits the current text into display lines. A single-line box keeps one
    // entry; a box taller than one line wraps on explicit newlines and then on
    // width, so a document paragraph is readable instead of running off the edge.
    std::vector<std::wstring> BuildDisplayLines(float wrapWidth, float fontSize, int weight) const;
    // Same splitter, parameterised so the caret can lay out the text BEFORE it,
    // using the identical wrapping rule. Painting and IME positioning must agree
    // on which visual line the caret is on, soft wraps included.
    static std::vector<std::wstring> BuildDisplayLines(const std::wstring& text, ITextService* svc,
                                                       float wrapWidth, float boxHeight,
                                                       float fontSize, int weight);
    // Caret position relative to the text area's top-left corner. x is the width
    // of the last caret line; y is that line's offset, so the painted caret and
    // the IME candidate window land on the same line in a multi-line box.
    Point CaretPosition() const;
    void RepaintWithPreview();
    int slotText_ = -1;
    size_t caret_ = 0;
    bool caretVisible_ = true;
    std::wstring editing_;
    // The uncommitted IME preview. Empty means no composition is in progress.
    std::wstring composition_;
};

// A text field that suggests completions as you type.
//
// Filtering is a plain substring match on a ';'-separated list, for the same reason ComboBox
// uses one: an item template needs the virtualising collection subsystem, and a control that
// silently ignored its template would be worse than one that never had it.
//
// Two keys are why this is not just a TextBox with a flyout: Down/Up move the highlight while
// the list is open, and Enter commits the highlighted suggestion instead of doing nothing.
// Escape dismisses without changing the text.
class AutoSuggestBox : public PopupBase {
public:
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool OnKeyDown(int vk) override;
    bool OnChar(wchar_t ch) override;
    bool Focusable() const override { return true; }

    // A suggest box takes text, so it must speak the whole IME contract: without
    // these the host's composition messages are no-ops and a Chinese IME can
    // never put a character into the box (and with nothing typed there is no
    // list for Up/Down/Enter to act on).
    void SetComposition(const std::wstring& text) override;
    void CommitComposition(const std::wstring& finalText) override;
    void CancelComposition() override;
    bool Composing() const override { return !composition_.empty(); }
    CompositionCaret ImeCaret() const override;

    // The suggestions for the current Text.
    std::vector<std::wstring> Filtered() const;
    // Index of the highlighted row, or -1.
    int Highlighted() const { return highlighted_; }

    // Splits on ';' exactly like ComboBox, sharing the rule so the two cannot drift.
    static std::vector<std::wstring> SplitItems(const std::wstring& items);

protected:
    Point AnchorPoint() const override;
    Size PopupSize() const override;
    WidgetPtr BuildContentForSlotOpen() override;

private:
    void Refresh();
    void OpenSuggestions();
    void Commit(int index);

    std::wstring editing_;
    std::wstring composition_;  // preview only; never written into the Text slot
    int highlighted_ = -1;
};

// A text field that renders bullets instead of characters.
//
// Subclasses TextBox rather than wrapping it: masking is exactly one paint-time
// substitution, and the editing behaviour (caret, insert, backspace, arrows) must stay
// identical -- a password field that handled keys differently from a text box would be a
// defect, not a feature. The real value stays in Text, so a ViewModel still reads what
// the user typed.
class PasswordBox : public TextBox {
public:
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    // What the box displays. Public so tests can assert on the mask without reading
    // pixels.
    static std::wstring Mask(size_t count);

protected:
    // The whole point of this class: the paint path asks for the display string and gets
    // bullets, while `Text` keeps the real characters for the ViewModel.
    std::wstring DisplayText() const override;
};

// A text field that only accepts numbers and reports whether what is typed is one.
//
// Validation is a PROPERTY (`IsValid`) rather than an exception or a refused keystroke:
// a user typing "-" or "1e" mid-way through a number must be allowed to type it. The
// control reports; the ViewModel decides whether that blocks a Save.
class NumberBox : public TextBox {
public:
    void OnSlotChanged(int slot, const Value& value) override;
    Size Measure(const Size& available) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;

    // Parses Text into Value; returns false when it is not a number. Public because the
    // rule has to be testable without a window.
    static bool ParseNumber(const std::wstring& text, double& out);

private:
    void Revalidate();
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
    virtual RefPtr ItemAt(size_t) const { return nullptr; }

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

// A vertical list of templated rows, with a FIXED row height.
//
// Virtualisation (0.11): when the list is given a bounded viewport -- an explicit
// Height, or a parent that offers a finite height -- only the rows inside the
// viewport (plus a small overscan) exist as widgets. Rows outside it are freed, and
// their bindings with them, so a 10 000-item list costs the same as a 20-item one.
//
// Two consequences worth stating up front, because both are contract, not
// implementation detail:
//
//  * Rows are fixed-height (`ItemHeight`). Windowing needs to know where row N is
//    without measuring rows 0..N-1; a variable-height row breaks that arithmetic.
//  * `ItemWidgetAt(i)` returns null for a row outside the window. It asks "is this
//    row realized", not "does this item exist" -- the item exists in the model and
//    `ItemAt(i)` still returns it. Anything that needs every row at once has to be
//    non-virtualized (put the list inside a ScrollViewer, which offers unbounded
//    height: the list then materializes every row and the OUTER scroller scrolls).
class ListBox : public Selector, public IListObserver {public:
    ListBox();
    ~ListBox() override;

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    Widget* HitTest(const Point& p) override;
    bool OnPointerWheel(float deltaLines) override;

    void OnListChanged(const ListChange& change) override;
    void OnSlotChanged(int slot, const Value& value) override;

    // Number of items in the MODEL, realized or not. Selection clamping and
    // keyboard navigation work against this, so a virtualized list still lets you
    // arrow to row 9 999.
    size_t ItemCount() const override;
    RefPtr ItemAt(size_t index) const override;
    void OnSelectionChanged() override;
    bool OnKeyDown(int vk) override;
    void OnPointerDown(const Point& local) override;
    void OnPointerUp(const Point& local) override;
    bool Focusable() const override { return true; }

    // The widget for MODEL index `i`, or null when that row is not realized.
    Widget* ItemWidgetAt(size_t i) const;
    // Model index of a realized row, or size_t(-1) when there is no such widget.
    size_t RealizedIndexAt(size_t i) const { return i < itemWidgets_.size() ? realizedFirst_ + i : size_t(-1); }
    void RebuildItems(ITextService* text);
    // --- virtualisation state (read by tests, tools and the sample page) ------
    bool Virtualized() const { return virtualized_; }
    size_t RealizedCount() const { return itemWidgets_.size(); }
    size_t RealizedFirst() const { return realizedFirst_; }
    // First model index that is fully or partially visible in the viewport.
    size_t FirstVisibleIndex() const;
    float ScrollOffset() const { return offset_; }
    float MaxScroll() const { return maxScroll_; }
    float ViewportHeight() const { return viewportH_; }
    // Clamped to [0, MaxScroll()].
    void ScrollTo(float offset);
    // Scrolls the minimum amount that brings `index` fully into view. Does
    // nothing when the list is not virtualized: its viewport is then the page's,
    // and who scrolls that is not the list's business.
    void ScrollIntoView(int index);

protected:
    // Clips to the viewport and shifts the rows up by offset_, exactly like
    // ScrollViewer, so the rows' own Arrange stays in unscrolled coordinates.
    void PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) override;
    float ScrollShiftForPointers() const override { return offset_; }

private:
    void SubscribeSource();
    void UnsubscribeSource();
    // Number of rows a single PageUp/PageDown moves: one viewport.
    int PageStep() const;
    // Content box (viewport) in the coordinates bounds_ currently has.
    Rect ViewportRect() const;
    WidgetPtr CreateItemWidget(size_t index);
    // Makes the realized rows exactly the model range [first, last).
    void SetWindow(size_t first, size_t last);
    // Recomputes the window from offset_, viewportH_ and the model count.
    void UpdateWindow();
    void ClampScroll();
    // Publishes the two read-only outputs (RealizedCount / Virtualized) for
    // anything bound to them. Written as target-side writes, like CloseRequested.
    void PublishState();

    ObservableListBase* list_ = nullptr;
    RefPtr listRef_;
    std::shared_ptr<DataTemplate> template_;
    // Realized rows, aligned with model indices [realizedFirst_, ...).
    std::vector<WidgetPtr> itemWidgets_;
    size_t realizedFirst_ = 0;
    size_t modelCount_ = 0;
    float itemHeight_ = 26.0f;
    float offset_ = 0;
    float maxScroll_ = 0;
    float viewportH_ = 0;
    bool virtualized_ = false;
    ITextService* text_ = nullptr;
    int slotItems_ = -1;
    int slotTemplate_ = -1;
    int slotRealized_ = -1;
    int slotVirtualized_ = -1;
    int slotFirstVisible_ = -1;
    int slotScrollOffset_ = -1;
    int slotScrollMax_ = -1;
    // True while PublishState writes ScrollOffset, so the write is not read back as
    // a scroll request.
    bool writingScrollOffset_ = false;
};

// ---------------------------------------------------------------------------
// TreeView
//
// A hierarchical sibling of ListBox: the model is a set of root nodes, and the
// control draws the EXPANDED subset as indented rows. Like TabView's strip, the
// rows are data rather than child widgets -- a tree that kept a widget per node
// would pay for the whole model, expanded or not.
//
// Nodes implement ITreeItem; TreeNode is the ordinary model. ItemsSource is an
// ObservableList of those nodes, and SelectedItem is a TwoWay binding to the
// selected node. Expand/collapse is a property of the NODE (IsExpanded), so a
// ViewModel can open branches programmatically just by setting a property.
// ---------------------------------------------------------------------------
class ITreeItem {
public:
    virtual ~ITreeItem() = default;
    virtual std::wstring TreeTitle() const = 0;
    virtual std::vector<std::shared_ptr<ITreeItem>> TreeChildren() const = 0;
    virtual bool TreeExpanded() const = 0;
    virtual void SetTreeExpanded(bool expanded) = 0;
};

// The model used by WAML samples: a title, an expand flag and child nodes. It is
// observable so a binding can show a node's title somewhere else and a command
// can expand a branch without reaching into the control.
class TreeNode final : public ObservableObject, public ITreeItem {
public:
    explicit TreeNode(std::wstring title);

    void Add(std::shared_ptr<TreeNode> child);

    std::wstring TreeTitle() const override { return Title.Get(); }
    std::vector<std::shared_ptr<ITreeItem>> TreeChildren() const override;
    bool TreeExpanded() const override { return IsExpanded.Get(); }
    void SetTreeExpanded(bool expanded) override { IsExpanded.Set(expanded); }

    const std::vector<std::shared_ptr<TreeNode>>& Nodes() const { return children_; }

    ObservableProperty<std::wstring> Title{"Title"};
    ObservableProperty<bool> IsExpanded{"IsExpanded", false};

private:
    std::vector<std::shared_ptr<TreeNode>> children_;
};

class TreeView : public Widget, public IListObserver {
public:
    TreeView();
    ~TreeView() override;

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void OnPointerDown(const Point& local) override;
    bool OnPointerWheel(float deltaLines) override;
    bool OnKeyDown(int vk) override;
    bool Focusable() const override { return true; }
    void OnSlotChanged(int slot, const Value& value) override;
    void OnListChanged(const ListChange& change) override;

    // The flattened, currently visible rows. `RowIndex` is the index into this
    // vector; a node outside it is collapsed away and has no row.
    struct Row {
        RefPtr item;
        int depth = 0;
        size_t parent = size_t(-1);
        bool expandable = false;
    };
    const std::vector<Row>& Rows() const { return rows_; }
    size_t ModelCount() const { return modelCount_; }
    RefPtr SelectedItem() const { return selected_; }
    size_t SelectedRow() const { return selectedRow_; }
    float ScrollOffset() const { return offset_; }
    float MaxScroll() const { return maxScroll_; }
    void ScrollTo(float offset);
    void SetSelectedItem(const RefPtr& item);
    // Expand or collapse every node in the tree; the ViewModel can invoke these
    // through a command without knowing how rows are drawn.
    void SetAllExpanded(bool expanded);

protected:
    float ScrollShiftForPointers() const override { return offset_; }

private:
    void SubscribeSource();
    void UnsubscribeSource();
    void Rebuild();
    void Flatten(const RefPtr& ref, int depth, size_t parent);
    void ClampScroll();
    Rect ViewportRect() const;
    void PublishState();
    void SelectRow(size_t row);
    bool ToggleAt(const Point& local);

    ObservableListBase* list_ = nullptr;
    RefPtr listRef_;
    std::vector<Row> rows_;
    size_t modelCount_ = 0;
    RefPtr selected_;
    size_t selectedRow_ = size_t(-1);
    float itemHeight_ = 26.0f;
    float indent_ = 16.0f;
    float offset_ = 0;
    float maxScroll_ = 0;
    float viewportH_ = 0;
    bool bounded_ = false;
    int slotItems_ = -1;
    int slotSelected_ = -1;
    int slotOffset_ = -1;
    bool writingOffset_ = false;
    bool writingSelection_ = false;
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
    // The dialog is shown as a ROOT overlay, never as an in-tree control. Its
    // layout box is not a hit target: without this override an unopened dialog
    // declared at the end of a page swallows every click on the page above it.
    Widget* HitTest(const Point&) override { return nullptr; }

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