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
        // A link with a command that does nothing visible would be indistinguishable from
        // a broken one, so this one changes its own caption.
        openLinkCommand_.Set(std::make_shared<wui::RelayCommand>([this] {
            LinkText.Set(LinkText.Get() == L"Open the docs" ? L"Opened — the command ran"
                                                           : L"Open the docs");
        }));

        Make("Heading", Heading);
        Make("Blurb", Blurb);
        Make("ClickCount", ClickCount);
        Make("ClickSummary", ClickSummary);
        Make("Greeting", Greeting);
        Make("IsAgreed", IsAgreed);
        Make("AgreementText", AgreementText);
        Make("ClickCommand", clickCommand_);
        Make("ResetCommand", resetCommand_);

        // The value-holding controls. Each pair is a control plus the caption that
        // reports it: a control bound to nothing looks the same as a working one, and
        // a caption is what proves the write actually reached the ViewModel.
        Make("IsBold", IsBold);
        Make("BoldStatus", BoldStatus);
        Make("IsNotifying", IsNotifying);
        Make("NotifyStatus", NotifyStatus);
        Make("Volume", Volume);
        Make("VolumeText", VolumeText);
        Make("IsComfortable", IsComfortable);
        Make("IsCompact", IsCompact);
        Make("Quantity", Quantity);
        Make("IsQuantityValid", IsQuantityValid);
        Make("QuantityStatus", QuantityStatus);
        Make("Passphrase", Passphrase);
        Make("PassphraseStatus", PassphraseStatus);
        Make("Progress", Progress);
        Make("BadgeCount", BadgeCount);
        Make("UserName", UserName);
        Make("BarTitle", BarTitle);
        Make("BarMessage", BarMessage);
        Make("IsBarOpen", IsBarOpen);
        Make("LinkText", LinkText);
        Make("OpenLinkCommand", openLinkCommand_);
        Make("LanguageIndex", LanguageIndex);
        Make("LanguageText", LanguageText);
        Make("WrapLines", WrapLines);

        // Each control that holds state gets its own observer, so the caption is derived
        // in one place instead of every control writing a caption as a side effect.
        IsBold.Subscribe(this);
        IsNotifying.Subscribe(this);
        Volume.Subscribe(this);
        IsComfortable.Subscribe(this);
        IsCompact.Subscribe(this);
        Quantity.Subscribe(this);
        Passphrase.Subscribe(this);
        LanguageIndex.Subscribe(this);

        // Watch IsAgreed so the caption question tracks the box. An observer
        // (rather than the page calling back) keeps the derivation inside the
        // ViewModel, where it belongs -- the markup stays declarative.
        IsAgreed.Subscribe(this);

        // Seed the greeting so the TextBox starts with real content: a TextBox
        // bound TwoWay to an empty string is indistinguishable from a broken
        // binding, which makes a broken sample look like a working one.
        Greeting.Set(L"Hello, WAML");
        AgreementText.Set(L"Not agreed yet.");

        BoldStatus.Set(L"Off — click it and this line follows.");
        NotifyStatus.Set(L"Off.");
        VolumeText.Set(L"Volume 4 of 10.");
        QuantityStatus.Set(L"12 items is a valid number.");
        PassphraseStatus.Set(L"5 characters, masked above.");
        Progress.Set(0.45);
        BadgeCount.Set(L"3");
        UserName.Set(L"Ada Lovelace");
        BarTitle.Set(L"A bar collapses when dismissed");
        BarMessage.Set(L"Press the ✕ above; the controls below move up rather than "
                       L"leaving a gap.");
        LinkText.Set(L"Open the docs");
        LanguageText.Set(L"Selected: Rust (index 2).");
        Quantity.Set(L"12");
        Volume.Set(4);
        IsComfortable.Set(true);
        Passphrase.Set(L"secret");
    }

    ~BasicViewVm() override {
        IsAgreed.Unsubscribe(this);
        IsBold.Unsubscribe(this);
        IsNotifying.Unsubscribe(this);
        Volume.Unsubscribe(this);
        IsComfortable.Unsubscribe(this);
        IsCompact.Unsubscribe(this);
        Quantity.Unsubscribe(this);
        Passphrase.Unsubscribe(this);
        LanguageIndex.Unsubscribe(this);
    }

private:
    // One observer for many sources: the caption is chosen by which property arrived,
    // so adding a control does not mean adding another observer type.
    void OnSourceChanged(const wui::Value& value) override {
        bool agreed = false;
        if (wui::ValueCodec<bool>::From(value, agreed)) {
            AgreementText.Set(agreed ? L"Thanks — agreed." : L"Not agreed yet.");
        }
        BoldStatus.Set(IsBold.Get() ? L"On — the button stays down while the value is true."
                                    : L"Off — click it and this line follows.");
        NotifyStatus.Set(IsNotifying.Get() ? L"On — the ViewModel was written by the switch."
                                           : L"Off.");
        VolumeText.Set(L"Volume " + std::to_wstring(static_cast<int>(Volume.Get())) +
                       L" of 10 — one decimal step per notch.");
        // The radio pair is exclusive, so ONE of them is always true; deriving both
        // captions from a single source keeps them from disagreeing.
        if (IsCompact.Get()) IsComfortable.Set(false);
        // NumberBox's rule lives in the control; the sample only needs "is this a
        // number", so it asks the control rather than duplicating the parse.
        const std::wstring qtyText = Quantity.Get();
        double parsed = 0;
        // wcstod must consume the whole string or "12abc" would read as 12.
        wchar_t* end = nullptr;
        const double numeric = qtyText.empty() ? 0.0 : std::wcstod(qtyText.c_str(), &end);
        const bool valid = !qtyText.empty() && end != nullptr && *end == L'\0';
        parsed = numeric;
        IsQuantityValid.Set(valid);
        QuantityStatus.Set(valid ? qtyText + L" is a valid number."
                                 : L"\"" + qtyText + L"\" is not a number — the box shows a red edge.");
        {
            // Derived from the index the control owns, so the caption cannot disagree with
            // the box above it.
            static const wchar_t* kLangs[] = {L"C++", L"C#", L"Rust", L"Python", L"Go"};
            const int idx = LanguageIndex.Get();
            LanguageText.Set(idx >= 0 && idx < 5
                                 ? std::wstring(L"Selected: ") + kLangs[idx] + L" (index " +
                                       std::to_wstring(idx) + L")."
                                 : std::wstring(L"Nothing selected."));
        }
        PassphraseStatus.Set(std::to_wstring(Passphrase.Get().size()) +
                             L" characters, masked above. The text is readable here because "
                             L"the mask is a paint concern.");
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

    // Toggles and value-holding controls.
    wui::ObservableProperty<bool> IsBold{"IsBold", false};
    wui::ObservableProperty<std::wstring> BoldStatus{"BoldStatus", L""};
    wui::ObservableProperty<bool> IsNotifying{"IsNotifying", false};
    wui::ObservableProperty<std::wstring> NotifyStatus{"NotifyStatus", L""};
    wui::ObservableProperty<double> Volume{"Volume", 0.0};
    wui::ObservableProperty<std::wstring> VolumeText{"VolumeText", L""};
    wui::ObservableProperty<bool> IsComfortable{"IsComfortable", true};
    wui::ObservableProperty<bool> IsCompact{"IsCompact", false};
    // Text, not a number: the point of NumberBox is that the FIELD may hold something
    // that is not a number, and the ViewModel has to be able to see that.
    wui::ObservableProperty<std::wstring> Quantity{"Quantity", L""};
    wui::ObservableProperty<bool> IsQuantityValid{"IsQuantityValid", true};
    wui::ObservableProperty<std::wstring> QuantityStatus{"QuantityStatus", L""};
    wui::ObservableProperty<std::wstring> Passphrase{"Passphrase", L""};
    wui::ObservableProperty<std::wstring> PassphraseStatus{"PassphraseStatus", L""};

    // Status and decoration.
    wui::ObservableProperty<double> Progress{"Progress", 0.0};
    wui::ObservableProperty<std::wstring> BadgeCount{"BadgeCount", L"0"};
    wui::ObservableProperty<std::wstring> UserName{"UserName", L""};
    wui::ObservableProperty<std::wstring> BarTitle{"BarTitle", L""};
    wui::ObservableProperty<std::wstring> BarMessage{"BarMessage", L""};
    wui::ObservableProperty<bool> IsBarOpen{"IsBarOpen", true};
    wui::ObservableProperty<std::wstring> LinkText{"LinkText", L"Link"};
    // The index is what the control owns; the text is derived for the caption. Both are
    // exposed so the demo can show that they cannot disagree.
    wui::ObservableProperty<int> LanguageIndex{"LanguageIndex", 2};
    wui::ObservableProperty<std::wstring> LanguageText{"LanguageText", L""};
    wui::ObservableProperty<bool> WrapLines{"WrapLines", true};

private:
    wui::ObservableProperty<wui::RefPtr> clickCommand_{"ClickCommand"};
    wui::ObservableProperty<wui::RefPtr> resetCommand_{"ResetCommand"};
    wui::ObservableProperty<wui::RefPtr> openLinkCommand_{"OpenLinkCommand"};
};

} // namespace sample
