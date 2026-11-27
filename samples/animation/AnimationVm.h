// SPDX-License-Identifier: MIT
// Sample: the animation page's ViewModel.
//
// The page demonstrates TWO families of motion with ONE clock:
//
//   1. waiting indicators -- four LoadingSpinner shapes sharing a Phase slot;
//   2. component transitions -- a damped spring (TranslateX), a fade (Opacity)
//      and a size ease (ScaleX/ScaleY). These are ordinary render-only WAML
//      properties now, so none of them reflows the page.
//
// The host hands every sample the same measured `dt` through
// HostOptions::onFrame; Tick turns it into all of the above and OneWay bindings
// carry each value into the control. Nothing here reads a wall clock: a
// snapshot freezes every animation simply by not ticking.
#pragma once

#include "Mvvm.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>
#include <string>

namespace sample {

class AnimationVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    AnimationVm() {
        toggle_.Set(std::make_shared<wui::RelayCommand>([this] { Toggle(); }));
        reset_.Set(std::make_shared<wui::RelayCommand>([this] { Reset(); }));
        kick_.Set(std::make_shared<wui::RelayCommand>([this] { Kick(); }));
        fade_.Set(std::make_shared<wui::RelayCommand>([this] { ToggleFade(); }));
        scale_.Set(std::make_shared<wui::RelayCommand>([this] { ToggleScale(); }));

        Make("Phase", Phase);
        Make("Running", Running);
        Make("Speed", Speed);
        Make("SpeedLabel", SpeedLabel);
        Make("PhaseText", PhaseText);
        Make("ToggleText", ToggleText);
        Make("StateNote", StateNote);
        Make("ToggleCommand", toggle_);
        Make("ResetCommand", reset_);

        Make("SpringOffset", SpringOffset);
        Make("SpringVelocity", SpringVelocity);
        Make("SpringNote", SpringNote);
        Make("FadeOpacity", FadeOpacity);
        Make("FadeText", FadeText);
        Make("FadeNote", FadeNote);
        Make("ScaleFactor", ScaleFactor);
        Make("ScaleText", ScaleText);
        Make("ScaleNote", ScaleNote);
        Make("KickCommand", kick_);
        Make("ToggleFadeCommand", fade_);
        Make("ToggleScaleCommand", scale_);

        // Speed is written by the Slider through a TwoWay binding. Observing it
        // keeps the caption honest no matter which side changed the value.
        Speed.Subscribe(this);
        UpdateLabels();
        UpdateNotes();
    }

    ~AnimationVm() override { Speed.Unsubscribe(this); }

    // Called once per frame by the host. `seconds` is measured, not assumed; the
    // host already clamps it after a window drag or a resume. Returning false
    // while paused means "no repaint needed" -- a paused page costs nothing.
    bool Tick(float seconds) {
        if (!Running.Get()) return false;
        const double dt = static_cast<double>(std::clamp(seconds, 0.0f, 0.1f));

        AdvancePhase(dt);
        AdvanceSpring(dt);
        AdvanceFade(dt);
        AdvanceScale(dt);
        UpdateNotes();
        return true;
    }

    void Toggle() {
        Running.Set(!Running.Get());
        UpdateLabels();
    }

    void Reset() {
        Phase.Set(0.0);
        PhaseText.Set(FormatPhase(Phase.Get()));
    }

    // A still frame for snapshot/interact: fixed, non-zero poses so the shapes
    // are recognisable instead of sitting at t=0. 0.1 puts the fifth ball at the
    // top of its arc; the component values are parked mid-transition.
    void SetStillFrame() {
        Phase.Set(0.1);
        PhaseText.Set(FormatPhase(Phase.Get()));
        SpringOffset.Set(78.0);
        SpringVelocity.Set(0.0);
        FadeOpacity.Set(0.55);
        ScaleFactor.Set(1.55);
        UpdateNotes();
    }

    // --- shared clock ------------------------------------------------------
    wui::ObservableProperty<double> Phase{"Phase", 0.0};
    wui::ObservableProperty<bool> Running{"Running", true};
    wui::ObservableProperty<int> Speed{"Speed", 1};
    wui::ObservableProperty<std::wstring> SpeedLabel{"SpeedLabel"};
    wui::ObservableProperty<std::wstring> PhaseText{"PhaseText"};
    wui::ObservableProperty<std::wstring> ToggleText{"ToggleText"};
    wui::ObservableProperty<std::wstring> StateNote{"StateNote"};

    // --- damped movement: TranslateX follows a spring integration ----------
    wui::ObservableProperty<double> SpringOffset{"SpringOffset", 0.0};
    wui::ObservableProperty<double> SpringVelocity{"SpringVelocity", 0.0};
    wui::ObservableProperty<std::wstring> SpringNote{"SpringNote"};

    // --- fade: Opacity eased toward a target -------------------------------
    wui::ObservableProperty<double> FadeOpacity{"FadeOpacity", 1.0};
    wui::ObservableProperty<std::wstring> FadeText{"FadeText", L"Fade out"};
    wui::ObservableProperty<std::wstring> FadeNote{"FadeNote"};

    // --- scale: ScaleX/ScaleY eased toward a target ------------------------
    wui::ObservableProperty<double> ScaleFactor{"ScaleFactor", 1.0};
    wui::ObservableProperty<std::wstring> ScaleText{"ScaleText", L"Grow"};
    wui::ObservableProperty<std::wstring> ScaleNote{"ScaleNote"};

private:
    static constexpr double kCyclesPerSecond = 0.42;
    // Spring constants: stiffness 38, damping 7 -> damping ratio ~0.57, so the
    // card overshoots a couple of times before settling instead of snapping.
    static constexpr double kStiffness = 38.0;
    static constexpr double kDamping = 7.0;

    void OnSourceChanged(const wui::Value&) override { UpdateLabels(); }

    void AdvancePhase(double dt) {
        double next = Phase.Get() + dt * kCyclesPerSecond * Speed.Get();
        if (next > 4096.0) next = std::fmod(next, 4096.0);
        Phase.Set(next);
    }

    void AdvanceSpring(double dt) {
        // Semi-implicit Euler at dt <= 0.1 is stable for these constants.
        const double force =
            -kStiffness * (SpringOffset.Get() - springTarget_) - kDamping * SpringVelocity.Get();
        const double v = SpringVelocity.Get() + force * dt;
        SpringVelocity.Set(v);
        SpringOffset.Set(SpringOffset.Get() + v * dt);
    }

    void AdvanceFade(double dt) {
        FadeOpacity.Set(FadeOpacity.Get() +
                        (fadeTarget_ - FadeOpacity.Get()) * std::min(1.0, dt * 4.2));
    }

    void AdvanceScale(double dt) {
        ScaleFactor.Set(ScaleFactor.Get() +
                        (scaleTarget_ - ScaleFactor.Get()) * std::min(1.0, dt * 3.0));
    }

    void Kick() {
        springTarget_ = springTarget_ < 50.0 ? 98.0 : 0.0;
        SpringVelocity.Set(SpringVelocity.Get() + (springTarget_ > SpringOffset.Get() ? 240.0 : -240.0));
    }

    void ToggleFade() {
        fadeOut_ = !fadeOut_;
        fadeTarget_ = fadeOut_ ? 0.12 : 1.0;
        FadeText.Set(fadeOut_ ? L"Fade in" : L"Fade out");
    }

    void ToggleScale() {
        grow_ = !grow_;
        scaleTarget_ = grow_ ? 1.8 : 1.0;
        ScaleText.Set(grow_ ? L"Shrink" : L"Grow");
    }

    void UpdateLabels() {
        SpeedLabel.Set(L"Speed " + std::to_wstring(Speed.Get()) + L"\u00d7");
        ToggleText.Set(Running.Get() ? L"Pause animations" : L"Resume animations");
        StateNote.Set(Running.Get()
                          ? L"running \u2014 the host advances every value below with the same measured dt"
                          : L"paused \u2014 all four shapes and all three component transitions are frozen");
    }

    void UpdateNotes() {
        PhaseText.Set(FormatPhase(Phase.Get()));
        SpringNote.Set(L"offset " + Format0(SpringOffset.Get()) + L"  \u00b7  velocity " +
                       Format0(SpringVelocity.Get()) + L" px/s");
        FadeNote.Set(L"opacity " + Format2(FadeOpacity.Get()) + L"  \u2192  " + Format2(fadeTarget_));
        ScaleNote.Set(Format2(ScaleFactor.Get()) + L"\u00d7  \u2192  " + Format2(scaleTarget_) + L"\u00d7");
    }

    static std::wstring FormatPhase(double value) {
        wchar_t buffer[64];
        std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"phase %.2f", value);
        return std::wstring(buffer);
    }

    static std::wstring Format0(double value) {
        wchar_t buffer[64];
        std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.0f", value);
        return std::wstring(buffer);
    }

    static std::wstring Format2(double value) {
        wchar_t buffer[64];
        std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.2f", value);
        return std::wstring(buffer);
    }

    wui::ObservableProperty<wui::RefPtr> toggle_{"ToggleCommand"};
    wui::ObservableProperty<wui::RefPtr> reset_{"ResetCommand"};
    wui::ObservableProperty<wui::RefPtr> kick_{"KickCommand"};
    wui::ObservableProperty<wui::RefPtr> fade_{"ToggleFadeCommand"};
    wui::ObservableProperty<wui::RefPtr> scale_{"ToggleScaleCommand"};
    double springTarget_ = 0.0;
    double fadeTarget_ = 1.0;
    double scaleTarget_ = 1.0;
    bool fadeOut_ = false;
    bool grow_ = false;
};

} // namespace sample
