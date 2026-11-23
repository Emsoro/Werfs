// SPDX-License-Identifier: MIT
//
// Animation core: a frame clock, easing, and per-slot timelines.
//
// Two decisions shape everything here:
//
// 1. TIME IS INJECTED, NEVER READ FROM THE SYSTEM. `Animator::Advance(dt)` takes a
//    delta the caller supplies. The host passes real elapsed time from its 16 ms timer;
//    a test passes 0.016 and asserts exact intermediate values. A clock that read
//    QueryPerformanceCounter internally would make every animation test flaky by
//    construction, and would make the determinism snapshots non-deterministic the
//    moment anything animated.
//
// 2. AN ANIMATION OWNS A SLOT, NOT A WIDGET. An animated property is "slot N on widget W
//    is moving from A to B". That is the same unit the style/trigger system already
//    writes, so a trigger firing mid-animation and an animation finishing cannot
//    disagree about who wrote what last -- the timeline wins while it is running, then
//    hands the slot back.
#pragma once

#include "Core.h"
#include "Widget.h"

#include <functional>
#include <memory>
#include <vector>

namespace wui {

// ---------------------------------------------------------------------------
// Easing
//
// A small closed set rather than arbitrary Bezier control points: these four cover what
// UI transitions actually use, and a cubic-bezier parser would be a feature nobody asked
// for sitting in the hot path of every frame.
// ---------------------------------------------------------------------------
enum class Easing {
    Linear,
    EaseIn,    // slow start
    EaseOut,   // slow end -- the one to use for something appearing
    EaseInOut, // slow at both ends
};

float Ease(Easing easing, float t);

// ---------------------------------------------------------------------------
// A timeline: one property slot moving from one value to another over a duration.
// ---------------------------------------------------------------------------
struct SlotAnimation {
    int slot = -1;
    Value from;
    Value to;
    float duration = 0.0f; // seconds; 0 means apply `to` immediately
    float delay = 0.0f;    // seconds before it starts
    Easing easing = Easing::Linear;
};

// Owns the animation state of one widget. Attached lazily: a widget that never animates
// pays one null pointer.
class AnimationHost {
public:
    static std::shared_ptr<AnimationHost> Attach(Widget* owner);
    static std::shared_ptr<AnimationHost> Get(Widget* owner);

    // Advances this widget's timelines by `dt` seconds, writing the interpolated value
    // into each target slot. One frame per call; the host calls AdvanceAll once, and a
    // test calls Advance with the step it wants to observe.
    void Advance(float dt);

    // Starts (or restarts) a set of slot animations, freeing any timelines they replace.
    void Start(const std::vector<SlotAnimation>& animations);
    // Stops every animation, leaving the slots at their current values.
    void Stop();
    // Drops the timelines targeting these slots (used when a state no longer applies).
    void StopSlots(const std::vector<int>& slots);
    bool Busy() const { return busy_; }
    size_t Count() const { return tracks_.size(); }

    // Advances every attached widget. Called once per frame by the host.
    static void AdvanceAll(float dt);
    // True while anything anywhere is animating, so the host can stop repainting.
    static bool AnyBusy();
    // Stops and forgets every animation. Used by tests between cases so one case cannot
    // leak motion into the next.
    static void ResetAll();

private:
    struct Track {
        SlotAnimation anim;
        float elapsed = 0.0f;
        Value current;
        bool done = false;
    };

    explicit AnimationHost(Widget* owner) : owner_(owner) {}

    Widget* owner_ = nullptr;
    std::vector<Track> tracks_;
    bool busy_ = false;
};

} // namespace wui
