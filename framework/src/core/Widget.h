// SPDX-License-Identifier: MIT
// Widget tree: typed property slots, layout, paint and input dispatch.
#pragma once

#include "Core.h"
#include "Mvvm.h"
#include "Types.h"

#include <string>
#include <vector>

namespace wui {

class ITextService;

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
    void SetDataContext(RefPtr dc, bool own = true);
    RefPtr DataContext() const { return dataContext_; }
    RefPtr EffectiveDataContext() const;

    // --- bindings -----------------------------------------------------------
    void AddBinding(std::unique_ptr<Binding> binding);
    void AttachBindings(ITextService* text);
    void DetachBindings();
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
    void SetName(std::string name) { name_ = std::move(name); }
    const std::string& Name() const { return name_; }

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

    // --- invalidation counters (for tests / profiling) -----------------------
    static inline uint64_t LayoutCount = 0;
    static inline uint64_t PaintCount = 0;

    // ISlotObserver (used by TwoWay bindings)
    void OnSlotChanged(int slot, const Value& value) override;

    void AddSlotObserver(int slot, ISlotObserver* o);
    void RemoveSlotObserver(int slot, ISlotObserver* o);

protected:
    void SetDirtyUpwards(uint32_t flags);
    Widget* Root();
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
    std::vector<Value> values_;
    std::vector<WidgetPtr> children_;
    Widget* parent_ = nullptr;
    Widget* root_ = nullptr;
    RefPtr dataContext_;
    bool ownsDataContext_ = false;
    std::vector<std::unique_ptr<Binding>> bindings_;
    std::map<int, std::vector<ISlotObserver*>> slotObservers_;
    Rect bounds_;
    Point paintOffset_{0, 0};
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
