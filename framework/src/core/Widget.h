// SPDX-License-Identifier: MIT
// Widget tree: typed property slots, layout, paint and input dispatch.
#pragma once

#include "Core.h"
#include "TextInput.h"
#include "Mvvm.h"
#include "Types.h"

#include <string>
#include <vector>

namespace wui {

class ITextService;
// Owns the runtime state of <Trigger> for one widget (see waml/StyleTriggers.h).
// Forward-declared at namespace scope so Widget can hold a shared_ptr to it without
// core depending on the WAML layer.
class StyleTriggerHost;
class AnimationHost;
class VisualStateHost;


// Process-wide text measurement service. Controls that need to measure text
// read it here; tests can install a deterministic implementation.
class TextSystem {
public:
    static void Set(ITextService* service);
    static ITextService* Get();
};

// A DataTemplate is produced by the markup layer and consumed by list controls.
// It carries no rendering knowledge: instantiating it yields a detached subtree.
class DataTemplate : public RefObject {
public:
    std::function<WidgetPtr()> instantiate;
    // Debug name of the template source (file/line) for diagnostics.
    std::string origin;
};

class Widget : public std::enable_shared_from_this<Widget>, public ISlotObserver {
public:
    Widget();
    ~Widget() override;

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // --- type binding -------------------------------------------------------
    void BindType(const TypeInfo* type);
    const TypeInfo* Type() const { return type_; }
    const char* TypeName() const { return type_ ? type_->Name().c_str() : "?"; }
    int SlotOf(std::string_view name) const;

    // The element's FontFamily, or empty for the system default. A helper rather than an
    // inline SlotOf/Get pair at each call site, because nine controls need it and a
    // missing slot at any one of them would silently fall back to the wrong metrics.
    std::wstring FontFamily() const;

    // --- slots --------------------------------------------------------------
    template <class T>
    T Get(int slot) const {
        T out{};
        if (slot >= 0 && slot < static_cast<int>(values_.size())) ValueCodec<T>::From(values_[static_cast<size_t>(slot)], out);
        return out;
    }
    const Value& RawSlot(int slot) const;
    // Writes a slot from the markup layer (load time). Applies default values too.
    void SetSlotValue(int slot, const Value& value, bool fromBinding = false);
    int SlotCount() const { return static_cast<int>(values_.size()); }

    // --- tree ---------------------------------------------------------------
    void AddChild(WidgetPtr child);
    void InsertChild(size_t index, WidgetPtr child);
    void RemoveChildAt(size_t index);
    void ClearChildren();
    const std::vector<WidgetPtr>& Children() const { return children_; }
    Widget* Parent() const { return parent_; }
    Widget* FindByName(std::string_view name) const; // depth-first by x:Name

    // --- data context -------------------------------------------------------
    // Sets this widget's local data context. `own` means "this widget chose the
    // object" (a WAML DataContext binding, or a host/list assigning a row model);
    // an owned context wins over inherited values and is not overwritten when an
    // ancestor changes. Passing an owned null context deliberately BLOCKS
    // inheritance -- a card whose selected server became null shows nothing
    // rather than quietly falling back to the page's ViewModel.
    void SetDataContext(RefPtr dc, bool own = true);
    RefPtr DataContext() const { return dataContext_; }
    RefPtr EffectiveDataContext() const;

    // --- bindings -----------------------------------------------------------
    void AddBinding(std::unique_ptr<Binding> binding);
    void AttachBindings(ITextService* text);
    void DetachBindings();
    // Pushes deferred TwoWay values (UpdateSourceTrigger=LostFocus). Called when
    // this widget loses focus, which is the event the trigger is named after.
    void FlushBindings();
    void RebindAll();
    size_t BindingCount() const { return bindings_.size(); }

    // --- layout -------------------------------------------------------------
    virtual Size Measure(const Size& available);
    virtual void Arrange(const Rect& finalRect);
    const Rect& Bounds() const { return bounds_; }
    const Size& DesiredSize() const { return desired_; }
    void Invalidate(uint32_t flags);
    bool LayoutDirty() const { return layoutDirty_; }
    bool PaintDirty() const { return paintDirty_; }
    void ClearDirty() {
        layoutDirty_ = false;
        paintDirty_ = false;
    }
    void ForceLayout(const Size& available);

    // --- paint --------------------------------------------------------------
    virtual void OnPaint(PaintCtx& ctx, const ThemePalette& palette) = 0;
    void PaintTree(PaintCtx& ctx, const ThemePalette& palette);
    // Same, with an extra translation applied to this subtree. Used to scroll
    // content: PaintCtx has no transform stack, so the only way to move pixels
    // is to move the rects while painting.
    void PaintTreeAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift);

    // --- input --------------------------------------------------------------
    virtual Widget* HitTest(const Point& p); // p in this widget's local space
    virtual void OnPointerEnter();
    virtual void OnPointerLeave();
    virtual void OnPointerDown(const Point& local);
    virtual void OnPointerUp(const Point& local);
    virtual void OnPointerMove(const Point& local);
    virtual bool OnKeyDown(int vk);
    virtual bool OnChar(wchar_t ch);
    // Wheel input, in "line" units (one notch = 3 lines by convention here).
    // Returns true when the widget consumed it, so the caller can stop walking
    // up the tree. Default: bubble to the parent.
    virtual bool OnPointerWheel(float deltaLines);
    virtual bool Focusable() const { return false; }

    // A Collapsed widget occupies no space and receives no input. Panels check
    // this and skip the child entirely, so collapsing something reflows the
    // layout instead of leaving a hole.
    bool IsVisible() const {
        const int slot = SlotOf("Visibility");
        return slot < 0 || Get<int>(slot) == 0; // 0 = Visible
    }

    // Children that participate in layout and painting. Every panel iterates
    // this instead of children_ so Visibility is honoured in one place -- with
    // twenty-odd loops across the controls, one forgotten check is a hole in a
    // layout that looks like a mystery.
    std::vector<WidgetPtr> LayoutChildren() const {
        std::vector<WidgetPtr> out;
        out.reserve(children_.size());
        for (const auto& c : children_) {
            if (c->IsVisible()) out.push_back(c);
        }
        return out;
    }

    bool Hovered() const { return hovered_; }
    bool Pressed() const { return pressed_; }
    bool Focused() const { return focused_; }
    void SetFocused(bool focused);
    // Setting press through a helper (rather than touching pressed_ directly) is what
    // keeps the visual state honest for every control: a control that toggles the flag
    // itself would silently skip the Pressed state.
    void SetPressed(bool pressed);
    void SetHovered(bool hovered);
    void SetName(std::string name) { name_ = std::move(name); }
    const std::string& Name() const { return name_; }

    // --- overlays (menus, dialogs) ------------------------------------------
    // The overlay is owned by the ROOT and painted after the whole tree, so it
    // is never clipped by a ScrollViewer and never scrolls with content. A
    // control passes a layer it owns; the tree does NOT take ownership, because
    // a menu is normally a member of the control that opened it.
    //
    // Calling this on any widget forwards to the root, and reading it reads the
    // root's value, so `anyWidget->Overlay()` is the same everywhere.
    void SetOverlay(OverlayLayer* layer);
    OverlayLayer* Overlay() const;
    // Where this widget is on screen, in window space (bounds + paint offset).
    Rect WindowBounds() const { return VisualBounds(); }

    Widget* ChildAt(size_t index) const {
        return index < children_.size() ? children_[index].get() : nullptr;
    }

    // --- scrolled content ---------------------------------------------------
    // A ScrollViewer arranges its child in unscrolled coordinates and sets this
    // offset so paint moves up and hit testing moves down by the same amount.
    // Keeping it separate from bounds_ means Arrange never has to know about
    // scrolling, and a re-layout does not fight the scroll position.
    void SetPaintOffset(const Point& offset) { paintOffset_ = offset; }
    const Point& PaintOffset() const { return paintOffset_; }
    // Bounds as they appear on screen (bounds plus paint offset).
    Rect VisualBounds() const {
        return Rect{bounds_.x + paintOffset_.x, bounds_.y + paintOffset_.y, bounds_.w, bounds_.h};
    }

    // Converts a window-space point into the space this widget's bounds_ live in.
    //
    // Pointer events arrive in window coordinates, while every control tests its
    // own bounds_, which are unscrolled. Inside a ScrollViewer those differ by
    // exactly the scroll offset, so dispatching the raw point made controls
    // ignore clicks the moment the page scrolled.
    //
    // This walks up the ancestor chain asking each ScrollViewer how much it has
    // scrolled, so nothing needs to be pushed down into the tree.
    Point ToLocal(const Point& p) const;

    // Inverse of ToLocal: a point in this widget's (unscrolled) bounds space is
    // moved UP by every scroller between here and the root, giving the position
    // where it actually appears in the window. Overlays live in window space, so
    // a popup anchored to a control inside a ScrollViewer must use this.
    Point ToWindow(const Point& p) const;

    // --- invalidation counters (for tests / profiling) -----------------------
    static inline uint64_t LayoutCount = 0;
    static inline uint64_t PaintCount = 0;

    // ISlotObserver (used by TwoWay bindings)
    void OnSlotChanged(int slot, const Value& value) override;

    void AddSlotObserver(int slot, ISlotObserver* o);
    void RemoveSlotObserver(int slot, ISlotObserver* o);

    // Attaches the runtime half of <Trigger>. Called by the WAML instantiator after
    // the style chain, the element's own attributes and its bindings have been
    // applied, so a trigger sees final values and never fights load-time precedence.
    void SetTriggerHost(std::shared_ptr<StyleTriggerHost> host);
    StyleTriggerHost* TriggerHost() const { return triggerHost_.get(); }

    // Animation state, attached lazily: a widget that never animates pays one null
    // pointer, and the frame loop never has to walk the whole tree looking for motion.
    // Accessed through free functions in Animation.h so this header does not need to know
    // the class; the member is private and friended there.
    std::shared_ptr<AnimationHost> AnimState() const { return animationHost_; }
    void SetAnimState(std::shared_ptr<AnimationHost> host) { animationHost_ = std::move(host); }

    // Visual states. The host decides WHICH state applies from the input flags, so a
    // control never has to write the pointer-over/pressed escalation itself -- and so all
    // controls escalate in the same order instead of each remembering its own rules.
    void SetVisualStateHost(std::shared_ptr<VisualStateHost> host) {
        visualStateHost_ = std::move(host);
    }
    VisualStateHost* VisualStates() const { return visualStateHost_.get(); }
    // Recomputes the visual state from Hovered/Pressed/Focused/IsEnabled.
    void UpdateVisualState(bool animate = true);

    // --- text composition ---------------------------------------------------
    // A widget that accepts text reports where its caret is, so the platform can put the
    // candidate window beside it. Base returns an empty caret at the widget origin: every
    // control that does not take text needs no override, and a host asking one of them is
    // harmless.
    virtual CompositionCaret ImeCaret() const;
    // Same three, defaulted to "this control takes no composition".
    virtual void SetComposition(const std::wstring&) {}
    virtual void CommitComposition(const std::wstring&) {}
    virtual void CancelComposition() {}
    virtual bool Composing() const { return false; }

protected:
    void SetDirtyUpwards(uint32_t flags);
    Widget* Root();
    // Re-resolves bindings that target DataContext. Their source is the PARENT's
    // effective context, not this widget's: after the binding assigns an object,
    // binding it again against that object would replace the answer with a
    // lookup on its own result (the "self-overwrite" failure mode).
    void RebindDataContextBindings();
    // How far this widget has scrolled its content, vertically. Only a
    // ScrollViewer returns anything but zero; ToLocal walks the ancestor chain
    // summing these to undo the scroll for pointer dispatch.
    virtual float ScrollShiftForPointers() const { return 0.0f; }
    // Applies Width/Height/MinWidth/MinHeight/MaxWidth/MaxHeight from slots to
    // desired_. Called at the end of each control's Measure().
    void ApplySizeConstraints();

    // Paints the children. Overridden by ScrollViewer to clip to its viewport and
    // shift the content by the scroll amount; every other widget forwards
    // unchanged. Called AFTER OnPaint, never from inside it -- a container that
    // painted its children itself would paint them twice.
    virtual void PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift);

    const TypeInfo* type_ = nullptr;
    // Slot of the WAML-visible "DataContext" property, or -1 for types that do
    // not carry it (the non-visual flyout types). Cached once BindType runs so
    // the hot path (every SetSlotValue) is an integer compare.
    int dataContextSlot_ = -1;
    std::vector<Value> values_;
    std::vector<WidgetPtr> children_;
    Widget* parent_ = nullptr;
    Widget* root_ = nullptr;
    RefPtr dataContext_;
    bool ownsDataContext_ = false;
    std::vector<std::unique_ptr<Binding>> bindings_;
    std::map<int, std::vector<ISlotObserver*>> slotObservers_;
    // Holds this widget's <Trigger> states when its style declares any. Null for the
    // overwhelming majority of widgets, which is why it is a pointer: paying for
    // trigger bookkeeping on every element would tax a feature most do not use.
    std::shared_ptr<StyleTriggerHost> triggerHost_;
    std::shared_ptr<AnimationHost> animationHost_;
    std::shared_ptr<VisualStateHost> visualStateHost_;
    Rect bounds_;
    Point paintOffset_{0, 0};
    // Owned by the root only; see SetOverlay.
    OverlayLayer* overlay_ = nullptr;
    Size desired_;
    bool layoutDirty_ = true;
    bool paintDirty_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
    std::string name_;
};

// Creates a widget of type T with its slots initialised from the registry.
template <class T>
std::shared_ptr<T> MakeBound() {
    auto w = std::make_shared<T>();
    return w;
}

WidgetPtr CreateWidget(const std::string& qualifiedTypeName);
void BindWidgetType(Widget& widget, const std::string& qualifiedTypeName);

} // namespace wui
