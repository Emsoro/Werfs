// SPDX-License-Identifier: MIT
// Sample: the basic controls page's ViewModel.
//
// SAMPLE code. Deliberately the smallest ViewModel that can drive the page:
// one of everything Basic.waml binds, nothing more. If this file starts needing
// a helper the framework ought to provide, that is a signal the framework is
// missing something.
#pragma once

#include "Mvvm.h"

#include <memory>
#include <string>

namespace sample {

// Binds every named element in Basic.waml:
//   Heading, Blurb, ClickCount, ClickSummary, Greeting, IsAgreed,
//   AgreementText, ClickCommand, ResetCommand
//
// Derived text (ClickSummary, AgreementText) is pushed explicitly whenever its
// input changes, instead of subscribing an observer to another property. With
// one ViewModel and one page, an explicit push is clearer to read and has no
// lifetime to get wrong.
class BasicViewVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    BasicViewVm() {
        clickCommand_.Set(std::make_shared<wui::RelayCommand>([this] { OnClick(); }));
        resetCommand_.Set(std::make_shared<wui::RelayCommand>([this] { OnReset(); }));

        Make("Heading", Heading);
        Make("Blurb", Blurb);
        Make("ClickCount", ClickCount);
        Make("ClickSummary", ClickSummary);
        Make("Greeting", Greeting);
        Make("IsAgreed", IsAgreed);
        Make("AgreementText", AgreementText);
        Make("ClickCommand", clickCommand_);
        Make("ResetCommand", resetCommand_);

        // Watch IsAgreed so the caption question tracks the box. An observer
        // (rather than the page calling back) keeps the derivation inside the
        // ViewModel, where it belongs -- the markup stays declarative.
        IsAgreed.Subscribe(this);

        // Seed the greeting so the TextBox starts with real content: a TextBox
        // bound TwoWay to an empty string is indistinguishable from a broken
        // binding, which makes a broken sample look like a working one.
        Greeting.Set(L"Hello, WAML");
        AgreementText.Set(L"Not agreed yet.");
    }

    ~BasicViewVm() override { IsAgreed.Unsubscribe(this); }

private:
    // Only IsAgreed is observed; any other source reaching here is a bug.
    void OnSourceChanged(const wui::Value& value) override {
        bool agreed = false;
        if (wui::ValueCodec<bool>::From(value, agreed)) {
            AgreementText.Set(agreed ? L"Thanks — agreed." : L"Not agreed yet.");
        }
    }

public:

    // Button -> ViewModel. The count and its caption are separate properties
    // because a ViewModel that only exposes a number leaves the page with
    // nothing to say.
    void OnClick() {
        const int next = ClickCount.Get() + 1;
        ClickCount.Set(next);
        if (next == 1) {
            ClickSummary.Set(L"First click — the button wrote a value into the ViewModel.");
        } else if (next < 5) {
            ClickSummary.Set(L"Still counting. OneWay pushes VM changes to the label.");
        } else {
            ClickSummary.Set(L"Five or more. Nothing is rebuilt; only text changed.");
        }
    }

    void OnReset() {
        ClickCount.Set(0);
        ClickSummary.Set(L"Reset — the count is back to zero.");
    }

    wui::ObservableProperty<std::wstring> Heading{"Heading", L"Basic controls"};
    wui::ObservableProperty<std::wstring> Blurb{
        "Blurb",
        L"The smallest useful page: five controls, one data source. Everything "
        L"here binds to a single ViewModel — no lists, no templates, no layout "
        L"panels doing anything clever."};
    wui::ObservableProperty<int> ClickCount{"ClickCount", 0};
    wui::ObservableProperty<std::wstring> ClickSummary{
        "ClickSummary",
        L"Press the button to send a value from the control into the ViewModel."};
    wui::ObservableProperty<std::wstring> Greeting{"Greeting", L""};
    wui::ObservableProperty<bool> IsAgreed{"IsAgreed", false};
    wui::ObservableProperty<std::wstring> AgreementText{"AgreementText", L""};

private:
    wui::ObservableProperty<wui::RefPtr> clickCommand_{"ClickCommand"};
    wui::ObservableProperty<wui::RefPtr> resetCommand_{"ResetCommand"};
};

} // namespace sample
