// SPDX-License-Identifier: MIT
// Sample: the theming page's ViewModel.
//
// SAMPLE code. The interesting part is what is NOT here: no colour values and no
// paint code. The page uses semantic tokens, and the host listens to one bool
// (`IsDarkTheme`) to pick the palette. That is the entire contract.
#pragma once

#include "Mvvm.h"

#include <memory>
#include <string>

namespace sample {

// Binds every named element in Theming.waml:
//   Heading, Blurb, IsDarkTheme, ThemeLabel, ToggleLabel, ToggleThemeCommand
class ThemingVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    ThemingVm() {
        toggle_.Set(std::make_shared<wui::RelayCommand>([this] { IsDarkTheme.Set(!IsDarkTheme.Get()); }));

        Make("Heading", Heading);
        Make("Blurb", Blurb);
        Make("IsDarkTheme", IsDarkTheme);
        Make("ThemeLabel", ThemeLabel);
        Make("ToggleLabel", ToggleLabel);
        Make("ToggleThemeCommand", toggle_);

        // The labels are derived, so they are updated from the one source of
        // truth rather than at each assignment site.
        IsDarkTheme.Subscribe(this);
        SyncLabels();
    }

    ~ThemingVm() override { IsDarkTheme.Unsubscribe(this); }

private:
    void OnSourceChanged(const wui::Value& value) override {
        bool dark = false;
        if (!wui::ValueCodec<bool>::From(value, dark)) return;
        SyncLabels();
    }

    void SyncLabels() {
        const bool dark = IsDarkTheme.Get();
        ThemeLabel.Set(dark ? L"Dark theme — token colours come from the dark palette."
                            : L"Light theme — token colours come from the light palette.");
        ToggleLabel.Set(dark ? L"Switch to light" : L"Switch to dark");
    }

public:
    wui::ObservableProperty<std::wstring> Heading{"Heading", L"Theming and tokens"};
    wui::ObservableProperty<std::wstring> Blurb{
        "Blurb",
        L"One page, two palettes. Colours written as tokens follow the theme; "
        L"colours written as literals do not. Switch and watch which swatches move."};
    // The host subscribes to this exact name (HostOptions::themeProperty), so a
    // checkbox or command can change the theme without knowing a host exists.
    wui::ObservableProperty<bool> IsDarkTheme{"IsDarkTheme", false};
    wui::ObservableProperty<std::wstring> ThemeLabel{"ThemeLabel"};
    wui::ObservableProperty<std::wstring> ToggleLabel{"ToggleLabel"};

private:
    wui::ObservableProperty<wui::RefPtr> toggle_{"ToggleThemeCommand"};
};

} // namespace sample
