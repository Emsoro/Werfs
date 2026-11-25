// SPDX-License-Identifier: MIT
#include "Widget.h"

#include <algorithm>

// Needed for the complete StyleTriggerHost type: ~Widget resets a shared_ptr to it,
// and shared_ptr's deleter must see the full definition.
#include "VisualStates.h"
#include "../waml/StyleTriggers.h"

namespace wui {

// ---------------------------------------------------------------------------
// TextSystem
// ---------------------------------------------------------------------------
namespace {
ITextService* g_textService = nullptr;
}

void TextSystem::Set(ITextService* service) { g_textService = service; }
ITextService* TextSystem::Get() { return g_textService; }

// ---------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------
Widget::Widget() = default;
Widget::~Widget() {
    DetachBindings();
    // Release the trigger host while this widget is still a valid object: its
    // destructor unsubscribes from our slot observers, which needs `this`.
    triggerHost_.reset();
    for (auto& c : children_) {
        if (c) c->parent_ = nullptr;
    }
}

void Widget::SetTriggerHost(std::shared_ptr<StyleTriggerHost> host) {
    triggerHost_ = std::move(host);
}

void Widget::BindType(const TypeInfo* type) {
    type_ = type;
    if (!type_) {
        dataContextSlot_ = -1;
        return;
    }
    dataContextSlot_ = SlotOf("DataContext");
    values_.clear();
    values_.resize(static_cast<size_t>(type_->SlotCount()));
    for (int i = 0; i < type_->SlotCount(); ++i) values_[static_cast<size_t>(i)] = type_->Properties()[static_cast<size_t>(i)].defaultValue;
}

int Widget::SlotOf(std::string_view name) const {
    if (!type_) return -1;
    const auto* d = type_->FindProperty(name);
    return d ? d->slot : -1;
}

const Value& Widget::RawSlot(int slot) const {
    static const Value empty{};
    if (slot < 0 || slot >= static_cast<int>(values_.size())) return empty;
    return values_[static_cast<size_t>(slot)];
}

void Widget::SetSlotValue(int slot, const Value& value, bool fromBinding) {
    if (slot < 0 || slot >= static_cast<int>(values_.size())) return;
    if (slot == dataContextSlot_) {
        // DataContext is a real WAML slot so markup can write it, but the tree
        // lives on Widget::dataContext_, not in the slot. The slot is a mirror.
        // An explicit null is a local value: it blocks inheritance (see
        // EffectiveDataContext), which is what a binding whose source object
        // disappeared should do.
        const RefPtr dc = value.type == VType::Ref ? value.ref : nullptr;
        SetDataContext(dc, true);
        return;
    }
    if (!fromBinding && value.Equals(values_[static_cast<size_t>(slot)])) {
        // Still notify slot observers so TwoWay initialisation can push values.
    }
    values_[static_cast<size_t>(slot)] = value;
    const auto* desc = type_ ? type_->PropertyAt(slot) : nullptr;
    const uint32_t flags = desc ? desc->flags : (AffectsMeasure | AffectsRender);
    if (flags & (AffectsMeasure | AffectsRender)) SetDirtyUpwards(flags);
    // Notify slot observers (bindings listening for TwoWay updates).
    auto it = slotObservers_.find(slot);
    if (it != slotObservers_.end()) {
        auto copy = it->second;
        for (auto* o : copy) {
            if (o != nullptr) o->OnSlotChanged(slot, values_[static_cast<size_t>(slot)]);
        }
    }
    OnSlotChanged(slot, values_[static_cast<size_t>(slot)]);
}

void Widget::AddSlotObserver(int slot, ISlotObserver* o) {
    if (slot < 0) return;
    auto& list = slotObservers_[slot];
    if (std::find(list.begin(), list.end(), o) == list.end()) list.push_back(o);
}

void Widget::RemoveSlotObserver(int slot, ISlotObserver* o) {
    auto it = slotObservers_.find(slot);
    if (it == slotObservers_.end()) return;
    auto& list = it->second;
    list.erase(std::remove(list.begin(), list.end(), o), list.end());
}

void Widget::AddChild(WidgetPtr child) {
    if (!child) return;
    child->parent_ = this;
    child->root_ = root_ ? root_ : Root();
    children_.push_back(std::move(child));
    SetDirtyUpwards(AffectsMeasure);
}

void Widget::InsertChild(size_t index, WidgetPtr child) {
    if (!child) return;
    child->parent_ = this;
    if (index > children_.size()) index = children_.size();
    children_.insert(children_.begin() + static_cast<long>(index), std::move(child));
    SetDirtyUpwards(AffectsMeasure);
}

void Widget::RemoveChildAt(size_t index) {
    if (index >= children_.size()) return;
    children_[index]->parent_ = nullptr;
    children_.erase(children_.begin() + static_cast<long>(index));
    SetDirtyUpwards(AffectsMeasure);
}

void Widget::ClearChildren() {
    for (auto& c : children_) c->parent_ = nullptr;
    children_.clear();
    SetDirtyUpwards(AffectsMeasure);
}

Widget* Widget::FindByName(std::string_view name) const {
    if (name_ == name) return const_cast<Widget*>(this);
    for (const auto& c : children_) {
        if (Widget* found = c->FindByName(name)) return found;
    }
    return nullptr;
}

void Widget::SetDataContext(RefPtr dc, bool own) {
    if (dataContext_ == dc && ownsDataContext_ == own) return;
    dataContext_ = std::move(dc);
    ownsDataContext_ = own;
    // Keep the WAML-visible mirror in step. Assign the vector directly rather
    // than calling SetSlotValue, which would route straight back here.
    if (dataContextSlot_ >= 0) {
        values_[static_cast<size_t>(dataContextSlot_)] =
            dataContext_ ? Value::Ref(dataContext_) : Value::Null();
    }

    // Resolve THIS widget's DataContext binding before pushing anything down.
    // Its source is the parent's effective context, so it does not depend on the
    // value just written -- and if it produces an object, the recursive
    // SetDataContext call below pushes the real object down. Pushing the
    // inherited value first would let children briefly resolve against the
    // wrong context, which used to surface as a transient binding error for a
    // perfectly valid nested card (page -> card -> badge).
    const RefPtr beforeBindings = dataContext_;
    const bool beforeOwn = ownsDataContext_;
    const RefPtr parentDc = parent_ ? parent_->EffectiveDataContext() : nullptr;
    for (auto& b : bindings_) {
        if (b->Desc().hasSource || b->Slot() != dataContextSlot_) continue;
        b->SetSource(parentDc);
    }
    if (dataContext_ != beforeBindings || ownsDataContext_ != beforeOwn) {
        // The binding wrote a different context; that recursive call has already
        // pushed and rebound the subtree. Do not overwrite it with `dc`.
        return;
    }

    // Children that own their own context keep it (local value wins), but their
    // DataContext BINDINGS still read this widget's effective context, so they
    // must be re-resolved even though their object does not change.
    for (auto& c : children_) {
        if (!c->ownsDataContext_) c->SetDataContext(dataContext_, false);
        else c->RebindDataContextBindings();
    }
    RebindAll();
    SetDirtyUpwards(AffectsMeasure | AffectsRender);
}

RefPtr Widget::EffectiveDataContext() const {
    // An owned context wins even when it is null: owning null is how a subtree
    // says "I have no data source" without falling back to an ancestor's.
    if (ownsDataContext_ || dataContext_) return dataContext_;
    return parent_ ? parent_->EffectiveDataContext() : nullptr;
}

void Widget::AddBinding(std::unique_ptr<Binding> binding) {
    if (binding) bindings_.push_back(std::move(binding));
}

void Widget::AttachBindings(ITextService* text) {
    for (auto& b : bindings_) b->Attach(text);
}

void Widget::DetachBindings() {
    for (auto& b : bindings_) b->Detach();
}

void Widget::FlushBindings() {
    // Only bindings with UpdateSourceTrigger=LostFocus have anything pending;
    // Flush is a no-op for the rest, so this can run on every focus loss.
    for (auto& b : bindings_) b->Flush();
}

void Widget::RebindAll() {
    // DataContext bindings resolve against the PARENT's effective context. If
    // they used this widget's own context, the first successful write would
    // become the source for the second resolution and the path would be looked
    // up on the wrong object (e.g. "{Binding SelectedServer}" asked of the
    // server instead of the page).
    for (auto& b : bindings_) {
        if (b->Desc().hasSource || b->Slot() != dataContextSlot_) continue;
        b->SetSource(parent_ ? parent_->EffectiveDataContext() : nullptr);
    }
    // Everything else -- including the bindings of a subtree whose DataContext
    // the pass above just replaced -- resolves against this widget's context.
    // Two passes matter: the context may change in the first one, and a single
    // cached value would then bind the rest of the page to the old object.
    for (auto& b : bindings_) {
        if (b->Desc().hasSource || b->Slot() == dataContextSlot_) continue;
        b->SetSource(EffectiveDataContext());
    }
    for (auto& c : children_) c->RebindAll();
}

void Widget::RebindDataContextBindings() {
    const RefPtr parentDc = parent_ ? parent_->EffectiveDataContext() : nullptr;
    for (auto& b : bindings_) {
        if (b->Desc().hasSource || b->Slot() != dataContextSlot_) continue;
        b->SetSource(parentDc);
    }
    for (auto& c : children_) c->RebindDataContextBindings();
}

std::wstring Widget::FontFamily() const {
    const int slot = SlotOf("FontFamily");
    return slot >= 0 ? Get<std::wstring>(slot) : std::wstring();
}
void Widget::ApplySizeConstraints() {
    // Explicit Width/Height win over content; min/max clamp all of them.
    //
    // An explicit size means the element's CONTENT box, so the margin is added on top --
    // the same convention every Measure() uses ("content + margin"). Treating it as the
    // total instead made `Height="6" Margin="0,6,0,10"` produce a widget whose own paint
    // rect was -10 high: present in layout, invisible on screen, and no diagnostic said
    // so.
    const Thickness margin = SlotOf("Margin") >= 0 ? Get<Thickness>(SlotOf("Margin"))
                                                  : Thickness{};
    struct Spec {
        const char* size;
        const char* min;
        const char* max;
        float* target;
        float marginExtra;
    };
    const Spec specs[2] = {
        {"Width", "MinWidth", "MaxWidth", &desired_.w, margin.H()},
        {"Height", "MinHeight", "MaxHeight", &desired_.h, margin.V()},
    };
    for (const auto& spec : specs) {
        const int sSize = SlotOf(spec.size);
        const int sMin = SlotOf(spec.min);
        const int sMax = SlotOf(spec.max);
        float v = *spec.target;
        if (sSize >= 0) {
            const float explicitSize = Get<float>(sSize);
            if (explicitSize > 0) v = explicitSize + spec.marginExtra;
        }
        if (sMax >= 0) {
            const float maxValue = Get<float>(sMax);
            if (maxValue > 0) v = std::min(v, maxValue);
        }
        if (sMin >= 0) {
            const float minValue = Get<float>(sMin);
            if (minValue > 0) v = std::max(v, minValue);
        }
        *spec.target = v;
    }
}

Widget* Widget::Root() {
    Widget* w = this;
    while (w->parent_) w = w->parent_;
    return w;
}

void Widget::SetDirtyUpwards(uint32_t flags) {
    Widget* w = this;
    while (w != nullptr) {
        if (flags & AffectsMeasure) w->layoutDirty_ = true;
        if (flags & AffectsRender) w->paintDirty_ = true;
        w = w->parent_;
    }
}

void Widget::Invalidate(uint32_t flags) { SetDirtyUpwards(flags); }

void Widget::ForceLayout(const Size& available) {
    Measure(available);
    Arrange(Rect{0, 0, available.w, available.h});
    ClearDirty();
}

Size Widget::Measure(const Size& available) {
    ++LayoutCount;
    const int margin = SlotOf("Margin");
    const int pad = SlotOf("Padding");
    Thickness m = margin >= 0 ? Get<Thickness>(margin) : Thickness{};
    Thickness p = pad >= 0 ? Get<Thickness>(pad) : Thickness{};
    const Size inner{std::max(0.0f, available.w - m.H() - p.H()),
                     std::max(0.0f, available.h - m.V() - p.V())};
    float maxW = 0;
    float maxH = 0;
    for (auto& c : children_) {
        const Size s = c->Measure(inner);
        maxW = std::max(maxW, s.w);
        maxH = std::max(maxH, s.h);
    }
    const int spacing = SlotOf("Spacing");
    (void)spacing;
    desired_ = Size{maxW + m.H() + p.H(), maxH + m.V() + p.V()};
    ApplySizeConstraints();
    return desired_;
}

void Widget::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const int margin = SlotOf("Margin");
    Thickness m = margin >= 0 ? Get<Thickness>(margin) : Thickness{};
    const Rect content{finalRect.x + m.l, finalRect.y + m.t, std::max(0.0f, finalRect.w - m.H()),
                       std::max(0.0f, finalRect.h - m.V())};
    for (auto& c : children_) c->Arrange(content);
}

void Widget::PaintTree(PaintCtx& ctx, const ThemePalette& palette) {
    PaintTreeAt(ctx, palette, Point{0, 0});
}

void Widget::PaintTreeAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) {
    if (bounds_.w <= 0 || bounds_.h <= 0) return;
    // Collapsed means invisible as well as spaceless: a hidden widget that still
    // painted would show a ghost at its stale bounds.
    if (!IsVisible()) return;

    // PaintCtx has no transform stack and every control draws from its own
    // bounds_, so moving the rect for the duration of the paint is the only way
    // to move pixels. The rect is restored immediately afterwards: bounds_ is
    // layout geometry, and leaving a scroll shift in it would corrupt Arrange.
    const Point savedPosition{bounds_.x, bounds_.y};
    bounds_.x += shift.x;
    bounds_.y += shift.y;

    OnPaint(ctx, palette);
    // The shift is passed DOWN, not just applied to this rect. bounds_ holds
    // absolute coordinates, so translating only the direct child would leave its
    // grandchildren exactly where they were -- which is why scrolling moved the
    // panel but nothing inside it.
    PaintChildrenAt(ctx, palette, shift);

    bounds_.x = savedPosition.x;
    bounds_.y = savedPosition.y;

    // Overlays are painted last, by the root, and never inside this subtree's
    // clip. Doing it here (rather than in PaintChildrenAt) is what lets a menu
    // opened from inside a ScrollViewer draw over the whole window.
    if (overlay_ && overlay_->Active() && parent_ == nullptr) {
        overlay_->Paint(ctx, palette);
    }
}

void Widget::PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) {
    for (auto& c : children_) {
        c->PaintTreeAt(ctx, palette,
                       Point{shift.x + c->paintOffset_.x, shift.y + c->paintOffset_.y});
    }
}

Point Widget::ToLocal(const Point& p) const {
    // Sum the scroll amount contributed by every scroller between this widget and
    // the root. A point in window space is ABOVE the content by that much, so it
    // moves DOWN by the same amount to reach content space.
    float scrolled = 0.0f;
    for (const Widget* w = this; w != nullptr; w = w->parent_) {
        scrolled += w->ScrollShiftForPointers();
    }
    return Point{p.x, p.y + scrolled};
}

Point Widget::ToWindow(const Point& p) const {
    // Exactly the inverse of ToLocal. bounds_ holds unscrolled layout coordinates;
    // painting temporarily shifts them up by the scroll amount. An overlay is
    // positioned in window space, so its anchor has to undo that shift -- using
    // bounds_ directly put popups in a scrolled page at the bottom of the window.
    float scrolled = 0.0f;
    for (const Widget* w = this; w != nullptr; w = w->parent_) {
        scrolled += w->ScrollShiftForPointers();
    }
    return Point{p.x, p.y - scrolled};
}

Widget* Widget::HitTest(const Point& p) {
    if (!IsVisible()) return nullptr;   // A hidden widget cannot be clicked.
    // Scrolled content: `p` arrives in this widget's unscrolled space, so undo
    // the paint offset before descending. Descending without this would make
    // every control inside a scrolled page unclickable.
    const Point local{p.x - paintOffset_.x, p.y - paintOffset_.y};
    // Reverse z-order: last child is on top.
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        Widget* hit = (*it)->HitTest(local);
        if (hit) return hit;
    }
    if (!children_.empty()) return nullptr;
    return bounds_.Contains(local) ? this : nullptr;
}

void Widget::OnPointerEnter() {
    hovered_ = true;
    UpdateVisualState();
    SetDirtyUpwards(AffectsRender);
}
void Widget::OnPointerLeave() {
    hovered_ = false;
    // Leaving also clears press: the pointer-up that would have cleared it went somewhere
    // else, and a control stuck in "Pressed" after a drag-off is the classic bug here.
    pressed_ = false;
    UpdateVisualState();
    SetDirtyUpwards(AffectsRender);
}

void Widget::UpdateVisualState(bool animate) {
    if (!visualStateHost_) return;
    // Escalation order, decided in ONE place so every control agrees:
    //   Disabled > Pressed > PointerOver > Focused > Normal
    //
    // Pressed outranks hover because a press implies the pointer is over it. Focus ranks
    // below hover because a focused control not being pointed at should not look identical
    // to one that is.
    const int slotEnabled = SlotOf("IsEnabled");
    const bool enabled = slotEnabled < 0 || Get<bool>(slotEnabled);
    const char* target = "Normal";
    if (!enabled) target = "Disabled";
    else if (pressed_) target = "Pressed";
    else if (hovered_) target = "PointerOver";
    else if (focused_) target = "Focused";
    if (animate) visualStateHost_->GoTo(target);
    else visualStateHost_->SnapTo(target);
}
void Widget::OnPointerDown(const Point&) {}
void Widget::OnPointerUp(const Point&) {}
void Widget::OnPointerMove(const Point&) {}
bool Widget::OnKeyDown(int) { return false; }
bool Widget::OnChar(wchar_t) { return false; }

// Default: bubble the wheel to the parent, so a nested control inside a
// ScrollViewer still scrolls the page.
bool Widget::OnPointerWheel(float deltaLines) {
    if (parent_) return parent_->OnPointerWheel(deltaLines);
    return false;
}

CompositionCaret Widget::ImeCaret() const {
    // A control that takes no text still has a position; returning its top-left means a host
    // that asks anyway puts the candidate window somewhere sane rather than at (0,0).
    CompositionCaret caret;
    caret.x = bounds_.x;
    caret.y = bounds_.y;
    caret.lineHeight = bounds_.h > 0 ? bounds_.h : 16.0f;
    return caret;
}

void Widget::SetPressed(bool pressed) {
    if (pressed_ == pressed) return;
    pressed_ = pressed;
    UpdateVisualState();
    SetDirtyUpwards(AffectsRender);
}

void Widget::SetHovered(bool hovered) {
    if (hovered_ == hovered) return;
    hovered_ = hovered;
    UpdateVisualState();
    SetDirtyUpwards(AffectsRender);
}

void Widget::SetFocused(bool focused) {
    if (focused_ == focused) return;
    focused_ = focused;
    UpdateVisualState();
    // Losing focus is what UpdateSourceTrigger=LostFocus names. Flushing here
    // (rather than in the host) keeps the rule true for every focus owner,
    // including keyboard focus moved by a control itself.
    if (!focused_) FlushBindings();
    SetDirtyUpwards(AffectsRender);
}

void Widget::SetOverlay(OverlayLayer* layer) {
    // The root owns it, so the overlay survives painting as a child of any
    // control and is drawn after the whole tree (never inside a clip).
    Widget* root = Root();
    if (!root) return;
    root->overlay_ = layer;
    root->SetDirtyUpwards(AffectsRender);
}

OverlayLayer* Widget::Overlay() const {
    const Widget* root = this;
    while (root->parent_) root = root->parent_;
    return root->overlay_;
}

void Widget::OnSlotChanged(int, const Value&) {}

// ---------------------------------------------------------------------------
// Registry helpers
// ---------------------------------------------------------------------------
WidgetPtr CreateWidget(const std::string& qualifiedTypeName) {
    const TypeInfo* info = TypeRegistry::Instance().Find(qualifiedTypeName);
    if (!info) return nullptr;
    WidgetPtr w = info->Create();
    if (w) BindWidgetType(*w, qualifiedTypeName);
    return w;
}

void BindWidgetType(Widget& widget, const std::string& qualifiedTypeName) {
    const TypeInfo* info = TypeRegistry::Instance().Find(qualifiedTypeName);
    if (info) widget.BindType(info);
}

} // namespace wui
