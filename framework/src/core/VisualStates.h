// SPDX-License-Identifier: MIT
//
// Visual states: hover, pressed, focus -- the states a control is in because the INPUT
// layer put it there, not because a property changed.
//
// How this differs from a Trigger, which already exists:
//
//   Trigger      watched property equals a value -> apply setters. The element itself
//                knows when the condition holds.
//   VisualState  something outside the property system CHOOSES the state. The pointer
//                being down is not a property (no binding can observe it), so hover and
//                press would be impossible to express with triggers -- which is exactly
//                the case this file exists for.
//
// Precedence, and why:
//
//   local > visual state > style setters > default
//
// A state wins over the style because it is the more specific, later decision ("this
// button, right now"). It loses to a local value for the same reason a trigger does: if
// the author wrote FontSize="11" inline, a hover must not quietly change it.
//
// When a state deactivates, the slots it touched are RESTORED to the value they held
// before it engaged -- captured per state, so Normal does not need to restate every
// property the other states set. That mirrors StyleTriggerHost, deliberately: two
// mechanisms that look alike should behave alike.
#pragma once

#include "Animation.h"
#include "Waml.h"
#include "Widget.h"

#include <memory>
#include <string>
#include <vector>

namespace wui {

class VisualStateHost final {
public:
    // `chain` is the style chain base-first. Returns nullptr when no style in the chain
    // declares a state, so elements without states pay nothing.
    static std::shared_ptr<VisualStateHost> Attach(
        Widget* owner, const std::vector<const StyleDesc*>& chain,
        std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styleOwner);

    // Switches to `name`. Unknown names are ignored rather than fatal: the input layer
    // asks for "PointerOver" on every control, and most controls do not define it.
    void GoTo(const std::string& name);
    // Switches to `name` with NO animation. Used for the first paint, where a fade-in from
    // nothing would delay a control appearing correctly.
    void SnapTo(const std::string& name);

    const std::string& Current() const { return current_; }
    // The states this element actually declared, for diagnostics and tests.
    const std::vector<const VisualStateDesc*>& States() const { return states_; }

private:
    VisualStateHost(Widget* owner, std::vector<const VisualStateDesc*> states);

    struct SlotSave {
        int slot = -1;
        Value value;
        bool saved = false;
    };

    void Enter(const VisualStateDesc* state, bool animate);
    void Leave();

    Widget* owner_;
    std::vector<const VisualStateDesc*> states_;
    std::shared_ptr<const std::vector<std::shared_ptr<StyleDesc>>> styleOwner_;
    std::string current_ = "Normal";
    // Values captured the first time each state touched a slot, so leaving restores
    // rather than leaving the state's colour behind.
    std::vector<SlotSave> saves_;
};

} // namespace wui
