// SPDX-License-Identifier: MIT
#include "Animation.h"

#include "Types.h"

#include <algorithm>
#include <cmath>

namespace wui {

float Ease(Easing easing, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case Easing::EaseIn:
        return t * t;
    case Easing::EaseOut:
        // The mirror of EaseIn, not a different curve: a value that appears should decelerate
        // by the same rule it would have accelerated leaving.
        return 1.0f - (1.0f - t) * (1.0f - t);
    case Easing::EaseInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    case Easing::Linear:
    default:
        return t;
    }
}

namespace {

// Interpolates two Values. Only the types that can be animated smoothly are handled;
// anything else (text, refs, bools) snaps at the end, because interpolating a string has no
// meaning and pretending otherwise would produce garbage for half a second.
Value Lerp(const Value& a, const Value& b, float t) {
    if (a.type == VType::Number && b.type == VType::Number) {
        return Value::Number(a.num + (b.num - a.num) * static_cast<double>(t));
    }
    if (a.type == VType::Color && b.type == VType::Color) {
        // Tokens are resolved by the painter, so a token-to-token move cannot be blended
        // numerically. Snapping at the end keeps the theme-correct colours rather than
        // inventing one that belongs to neither theme.
        if (a.color.token != Token::None || b.color.token != Token::None) {
            return t >= 1.0f ? b : a;
        }
        Color out;
        out.r = a.color.r + (b.color.r - a.color.r) * t;
        out.g = a.color.g + (b.color.g - a.color.g) * t;
        out.b = a.color.b + (b.color.b - a.color.b) * t;
        out.a = a.color.a + (b.color.a - a.color.a) * t;
        out.token = Token::None;
        return Value::MakeColor(out);
    }
    if (a.type == VType::Thickness && b.type == VType::Thickness) {
        Thickness out;
        out.l = a.thickness.l + (b.thickness.l - a.thickness.l) * t;
        out.t = a.thickness.t + (b.thickness.t - a.thickness.t) * t;
        out.r = a.thickness.r + (b.thickness.r - a.thickness.r) * t;
        out.b = a.thickness.b + (b.thickness.b - a.thickness.b) * t;
        return Value::MakeThickness(out);
    }
    return t >= 1.0f ? b : a;
}

} // namespace

namespace {

// Every host that has live animations. A vector of weak handles rather than per-widget
// storage because AdvanceAll walks it once per frame, and it only ever holds widgets that
// are actually moving; a destroyed widget simply expires.
std::vector<std::weak_ptr<AnimationHost>>& Registry() {
    static std::vector<std::weak_ptr<AnimationHost>> registry;
    return registry;
}

void Register(const std::shared_ptr<AnimationHost>& host) { Registry().push_back(host); }

} // namespace

std::shared_ptr<AnimationHost> AnimationHost::Attach(Widget* owner) {
    if (owner == nullptr) return nullptr;
    auto host = std::shared_ptr<AnimationHost>(new AnimationHost(owner));
    owner->SetAnimState(host);
    Register(host);
    return host;
}

std::shared_ptr<AnimationHost> AnimationHost::Get(Widget* owner) {
    return owner != nullptr ? owner->AnimState() : nullptr;
}

void AnimationHost::Advance(float dt) {
    if (tracks_.empty()) return;
    bool moving = false;
    for (Track& track : tracks_) {
        if (track.done) continue;
        track.elapsed += dt;
        const float active = track.elapsed - track.anim.delay;
        if (active <= 0.0f) {
            moving = true; // still waiting out its delay
            continue;
        }
        const float raw = track.anim.duration > 0.0f
                              ? std::min(1.0f, active / track.anim.duration)
                              : 1.0f;
        const float t = Ease(track.anim.easing, raw);
        track.current = Lerp(track.anim.from, track.anim.to, t);
        if (owner_ != nullptr) owner_->SetSlotValue(track.anim.slot, track.current);
        if (raw >= 1.0f) track.done = true;
        else moving = true;
    }
    if (!moving) {
        // Land exactly on the declared destination: float accumulation that stops at
        // 0.9987 leaves a control one shade off its declared colour forever, and nothing
        // would ever correct it.
        for (Track& track : tracks_) {
            if (owner_ != nullptr && track.anim.duration > 0.0f) {
                owner_->SetSlotValue(track.anim.slot, track.anim.to);
            }
        }
        tracks_.clear();
        busy_ = false;
    }
}

void AnimationHost::Start(const std::vector<SlotAnimation>& animations) {
    for (const SlotAnimation& anim : animations) {
        if (anim.slot < 0) continue;
        const Value from =
            anim.from.type != VType::Null ? anim.from : owner_->RawSlot(anim.slot);
        // Replace any existing timeline on this slot, starting from the CURRENT value.
        // Taking over from wherever the old timeline had reached (rather than from the
        // declared `from`) is what keeps a fast hover-in/hover-out on a straight path
        // instead of jumping back to the origin first.
        bool replaced = false;
        for (Track& track : tracks_) {
            if (track.anim.slot != anim.slot) continue;
            track.anim = anim;
            track.anim.from = from;
            track.elapsed = 0.0f;
            track.done = false;
            track.current = from;
            replaced = true;
            break;
        }
        if (replaced) continue;
        Track track;
        track.anim = anim;
        track.anim.from = from;
        track.current = from;
        tracks_.push_back(track);
    }
    busy_ = !tracks_.empty();
}

void AnimationHost::Stop() {
    tracks_.clear();
    busy_ = false;
}

void AnimationHost::StopSlots(const std::vector<int>& slots) {
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [&](const Track& t) {
                                     return std::find(slots.begin(), slots.end(), t.anim.slot) !=
                                            slots.end();
                                 }),
                  tracks_.end());
    busy_ = !tracks_.empty();
}

void AnimationHost::AdvanceAll(float dt) {
    auto& registry = Registry();
    // Drop dead entries as we go: a widget destroyed mid-animation must not be advanced.
    registry.erase(std::remove_if(registry.begin(), registry.end(),
                                  [](const std::weak_ptr<AnimationHost>& w) { return w.expired(); }),
                   registry.end());
    for (auto& weak : registry) {
        if (auto host = weak.lock()) {
            if (host->busy_) {
                if (dt > 0.0f) host->Advance(dt);
            }
        }
    }
}

bool AnimationHost::AnyBusy() {
    for (auto& weak : Registry()) {
        if (auto host = weak.lock()) {
            if (host->busy_) return true;
        }
    }
    return false;
}

void AnimationHost::ResetAll() {
    for (auto& weak : Registry()) {
        if (auto host = weak.lock()) host->Stop();
    }
    Registry().clear();
}


} // namespace wui