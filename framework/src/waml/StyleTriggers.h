// SPDX-License-Identifier: MIT
//
// Runtime half of <Trigger>.
//
// A style's plain setters are applied once and forgotten -- load-time work. A
// trigger cannot be: whether its setters belong depends on a property value that
// changes while the app runs. So each styled widget that has triggers gets one
// StyleTriggerHost attached, which:
//
//   * records the value each triggered property had BEFORE the trigger first fired
//     (its "baseline"), so deactivating restores it rather than leaving the last
//     triggered value stuck;
//   * watches the trigger's own property through the widget's slot-observer
//     mechanism, so no polling and no invalidation of the whole tree;
//   * refuses to override a locally-set property, because local > style is the
//     precedence users rely on.
//
// Why a separate object instead of logic in Widget: a widget must be usable with no
// style at all, and the overwhelming majority are. Paying for trigger bookkeeping in
// every Widget would tax every element for a feature most do not use.
#pragma once

#include <memory>
#include <vector>

#include "Waml.h"
// ISlotObserver (the interface implemented below) and Widget are both declared in
// Widget.h, so this header needs the full definition rather than a forward
// declaration: it inherits from one and calls methods on the other.
#include "Widget.h"

namespace wui {

class Widget;

class StyleTriggerHost final : public ISlotObserver {
public:
    // `chain` is the style chain base-first, exactly as applied at instantiation.
    // `localSlots` are the slots the element set INLINE; a trigger must not write
    // them, because local > style also means local > trigger. Takes effect
    // immediately, so a style whose trigger is already true looks right on the first
    // paint rather than the second.
    static std::shared_ptr<StyleTriggerHost> Attach(
        Widget* owner, const std::vector<const StyleDesc*>& chain,
        const std::vector<int>& localSlots,
        std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styleOwner);

    ~StyleTriggerHost() override;

    // Re-evaluates every trigger. Cheap when nothing changed: the comparison is the
    // first thing it does, and it returns before writing anything.
    void Refresh();

    // ISlotObserver: a watched property changed.
    void OnSlotChanged(int slot, const Value& value) override;

    // For tests: how many triggers are currently active.
    size_t ActiveCount() const;

private:
    explicit StyleTriggerHost(Widget* owner) : owner_(owner) {}

    struct TriggerState {
        const StyleTrigger* def = nullptr;
        // Baseline per setter slot: the value the property had before this trigger
        // first wrote to it. Empty until the trigger first activates.
        std::vector<Value> baseline;
        std::vector<bool> hasBaseline;
        bool active = false;
    };

    void Activate(TriggerState& t);
    void Deactivate(TriggerState& t);
    // True when this host's own write must not be treated as a user edit.
    bool writing_ = false;

    Widget* owner_;
    std::vector<TriggerState> states_;
    std::vector<int> watchedSlots_;
    // Keeps the style chain alive for as long as this host refers into it. A widget
    // tree routinely outlives the CompiledDocument it was built from, and every
    // state above points into StyleDesc::triggers; holding the owning vector here
    // makes the host self-sufficient instead of silently reading freed memory (which
    // presents as "the trigger has no setters").
    std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styleOwner_;
    // Slots the element set inline. A trigger never writes these: local value beats
    // style, and a trigger is part of the style. Without this an inline
    // `FontSize="11"` would be overwritten the moment its style's trigger fired,
    // which reads as "my attribute randomly stopped working".
    std::vector<int> localSlots_;
};

} // namespace wui
