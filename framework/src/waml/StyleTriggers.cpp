// SPDX-License-Identifier: MIT
#include "StyleTriggers.h"

#include <algorithm>
#include <cstdio>

#include "Widget.h"

namespace wui {

std::shared_ptr<StyleTriggerHost> StyleTriggerHost::Attach(
    Widget* owner, const std::vector<const StyleDesc*>& chain,
    const std::vector<int>& localSlots,
    std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styleOwner) {
    if (owner == nullptr || chain.empty()) return nullptr;
    // Count first: an element whose style has no triggers must not pay for a host
    // at all.
    size_t triggerCount = 0;
    for (const StyleDesc* s : chain) triggerCount += s->triggers.size();
    if (triggerCount == 0) return nullptr;

    std::shared_ptr<StyleTriggerHost> host(new StyleTriggerHost(owner));
    host->localSlots_ = localSlots;
    host->styleOwner_ = std::move(styleOwner);
    // Base-most first, so a derived style's trigger runs after its parent's and
    // can win -- the same rule the plain setters follow.
    for (const StyleDesc* style : chain) {
        for (const auto& trigger : style->triggers) {
            TriggerState state;
            state.def = &trigger;
            state.baseline.resize(trigger.setters.size());
            state.hasBaseline.assign(trigger.setters.size(), false);
            host->states_.push_back(std::move(state));
            // Watch each distinct property once.
            if (std::find(host->watchedSlots_.begin(), host->watchedSlots_.end(),
                          trigger.watchedSlot) == host->watchedSlots_.end()) {
                host->watchedSlots_.push_back(trigger.watchedSlot);
            }
        }
    }
    for (int slot : host->watchedSlots_) {
        owner->AddSlotObserver(slot, host.get());
    }
    // Apply the initial state so an already-true trigger is visible on first paint.
    host->Refresh();
    return host;
}

StyleTriggerHost::~StyleTriggerHost() {
    if (owner_ != nullptr) {
        for (int slot : watchedSlots_) {
            owner_->RemoveSlotObserver(slot, this);
        }
    }
}

void StyleTriggerHost::OnSlotChanged(int slot, const Value&) {
    // Our own writes come back through the same path. Without this guard a trigger
    // that writes a property it also watches would re-enter forever.
    if (writing_) return;
    if (std::find(watchedSlots_.begin(), watchedSlots_.end(), slot) == watchedSlots_.end()) return;
    Refresh();
}

void StyleTriggerHost::Refresh() {
    if (owner_ == nullptr) return;
    // Two passes: deactivate everything that no longer matches, then activate what
    // does. Doing it in one pass would let trigger A's output flip trigger B's
    // condition mid-loop, and the result would depend on declaration order.
    std::vector<size_t> wantActive;
    for (size_t i = 0; i < states_.size(); ++i) {
        const TriggerState& t = states_[i];
        if (t.def->watchedSlot < 0) continue;
        const Value current = owner_->RawSlot(t.def->watchedSlot);
        if (current.Equals(t.def->when)) wantActive.push_back(i);
    }

    for (size_t i = 0; i < states_.size(); ++i) {
        const bool should = std::find(wantActive.begin(), wantActive.end(), i) != wantActive.end();
        if (should && !states_[i].active) Activate(states_[i]);
        else if (!should && states_[i].active) Deactivate(states_[i]);
    }
}

void StyleTriggerHost::Activate(TriggerState& t) {
    t.active = true;
    writing_ = true;
    for (size_t i = 0; i < t.def->setters.size(); ++i) {
        const StyleSetter& setter = t.def->setters[i];
        // Local > style, and a trigger is part of the style: an inline attribute
        // wins even while the trigger is active. Skipping here (rather than letting
        // the write through and trying to undo it later) is what keeps the rule
        // simple to reason about.
        const bool isLocal =
            std::find(localSlots_.begin(), localSlots_.end(), setter.slot) != localSlots_.end();
        if (isLocal) continue;
        // Record what was there the FIRST time this trigger fires, not on every
        // activation: after the second activation the slot already holds the
        // triggered value, and remembering that as the baseline would make
        // deactivation a no-op.
        if (!t.hasBaseline[i]) {
            t.baseline[i] = owner_->RawSlot(setter.slot);
            t.hasBaseline[i] = true;
        }
        owner_->SetSlotValue(setter.slot, setter.value);
    }
    writing_ = false;
}
void StyleTriggerHost::Deactivate(TriggerState& t) {
    t.active = false;
    writing_ = true;
    for (size_t i = 0; i < t.def->setters.size(); ++i) {
        if (!t.hasBaseline[i]) continue;
        // Restore the baseline only if this trigger's own value is still in place.
        // If the user or a binding wrote something else in the meantime, that newer
        // value is not ours to overwrite.
        const StyleSetter& setter = t.def->setters[i];
        if (owner_->RawSlot(setter.slot).Equals(setter.value)) {
            owner_->SetSlotValue(setter.slot, t.baseline[i]);
        }
        t.hasBaseline[i] = false;
    }
    writing_ = false;
}

size_t StyleTriggerHost::ActiveCount() const {
    size_t n = 0;
    for (const auto& s : states_) {
        if (s.active) ++n;
    }
    return n;
}

} // namespace wui
