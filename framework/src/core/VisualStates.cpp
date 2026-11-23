// SPDX-License-Identifier: MIT
#include "VisualStates.h"

#include <algorithm>

namespace wui {

std::shared_ptr<VisualStateHost> VisualStateHost::Attach(
    Widget* owner, const std::vector<const StyleDesc*>& chain,
    std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styleOwner) {
    if (owner == nullptr || chain.empty()) return nullptr;
    std::vector<const VisualStateDesc*> states;
    // Base-first, so a derived style's state of the same name overrides the parent's
    // rather than both applying.
    for (const StyleDesc* style : chain) {
        for (const VisualStateDesc& state : style->states) {
            auto it = std::find_if(states.begin(), states.end(),
                                   [&](const VisualStateDesc* s) { return s->name == state.name; });
            if (it != states.end()) {
                *it = &state;
            } else {
                states.push_back(&state);
            }
        }
    }
    if (states.empty()) return nullptr;
    std::shared_ptr<VisualStateHost> host(new VisualStateHost(owner, std::move(states)));
    host->styleOwner_ = std::move(styleOwner);
    return host;
}

VisualStateHost::VisualStateHost(Widget* owner, std::vector<const VisualStateDesc*> states)
    : owner_(owner), states_(std::move(states)) {}

void VisualStateHost::GoTo(const std::string& name) {
    if (name == current_) return;
    const VisualStateDesc* next = nullptr;
    for (const VisualStateDesc* state : states_) {
        if (state->name == name) {
            next = state;
            break;
        }
    }
    // "Normal" always exists implicitly -- it means "no state modifier applied", which is
    // the state a control is in when nothing is hovering or pressing it. Requiring an
    // author to declare an empty Normal state, or (worse) treating a missing one as an
    // unknown name and doing nothing, would leave the previous state's values stuck.
    if (name == "Normal") {
        Leave();
        current_ = name;
        owner_->Invalidate(AffectsRender);
        return;
    }
    // Any other unknown state means "this control does not define it", which is not an
    // error: the input layer asks every control for PointerOver and most have nothing to
    // say.
    if (next == nullptr) return;
    Leave();
    Enter(next, /*animate=*/true);
    current_ = name;
}

void VisualStateHost::SnapTo(const std::string& name) {
    if (name == current_) return;
    const VisualStateDesc* next = nullptr;
    for (const VisualStateDesc* state : states_) {
        if (state->name == name) {
            next = state;
            break;
        }
    }
    if (name == "Normal") {
        Leave();
        current_ = name;
        return;
    }
    if (next == nullptr) return;
    Leave();
    Enter(next, /*animate=*/false);
    current_ = name;
}

void VisualStateHost::Enter(const VisualStateDesc* state, bool animate) {
    std::shared_ptr<AnimationHost> animator = AnimationHost::Get(owner_);
    if (animate && state->transition > 0.0f && animator == nullptr) {
        // Created on first use so a control that never animates never allocates one.
        animator = AnimationHost::Attach(owner_);
    }
    std::vector<SlotAnimation> timeline;
    for (const StyleSetter& setter : state->setters) {
        if (setter.slot < 0) continue;
        // Capture the pre-state value the first time this slot is touched, so leaving can
        // restore it. Capturing on ENTER (not on leave) is what makes it the value the
        // style established, not whatever the last state left behind.
        auto it = std::find_if(saves_.begin(), saves_.end(),
                               [&](const SlotSave& s) { return s.slot == setter.slot; });
        if (it == saves_.end()) {
            SlotSave save;
            save.slot = setter.slot;
            save.value = owner_->RawSlot(setter.slot);
            save.saved = true;
            saves_.push_back(save);
        }
        if (animate && state->transition > 0.0f) {
            SlotAnimation anim;
            anim.slot = setter.slot;
            // `from` deliberately left Unknown: AnimationHost fills it with the current
            // value, which is how an interrupted transition continues smoothly instead of
            // restarting from the state's declared start.
            anim.to = setter.value;
            anim.duration = state->transition;
            anim.easing = Easing::EaseOut;
            timeline.push_back(anim);
        } else {
            owner_->SetSlotValue(setter.slot, setter.value);
        }
    }
    if (!timeline.empty() && animator) animator->Start(timeline);
    owner_->Invalidate(AffectsRender);
}

void VisualStateHost::Leave() {
    if (saves_.empty()) return;
    // Restore in one step, not animated: the next state animates FROM here, so animating
    // the restore too would fight it and produce a dip in the middle.
    for (const SlotSave& save : saves_) {
        if (save.saved) owner_->SetSlotValue(save.slot, save.value);
    }
    // Drop any in-flight timelines so they cannot overwrite the restore a frame later.
    if (auto animator = AnimationHost::Get(owner_)) {
        std::vector<int> slots;
        slots.reserve(saves_.size());
        for (const SlotSave& save : saves_) slots.push_back(save.slot);
        animator->StopSlots(slots);
    }
    saves_.clear();
}

} // namespace wui
