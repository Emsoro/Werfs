// SPDX-License-Identifier: MIT
// Sample: the component-scope page's ViewModel.
//
// SAMPLE code. The point of the page is the shape of the data, not the amount
// of it: one page object, and two server objects that become the DataContext of
// two card subtrees. Nothing here knows that the cards exist, and the cards do
// not know about the page -- each side only sees its own object.
#pragma once

#include "Mvvm.h"

#include <iterator>
#include <memory>
#include <string>

namespace sample {

// The badge's own object. The card hands this down through a second
// DataContext binding, which is what makes the badge a component inside a
// component rather than another copy of the card's bindings.
class StatusVm final : public wui::ObservableObject {
public:
    StatusVm() {
        Make("Text", Text);
        Make("Color", Color);
    }

    // A literal tone: a status light is not a themed surface, so it does not
    // follow the palette. Tokens are for surfaces and text, not for meaning.
    static wui::Color Tone(bool online) {
        return online ? wui::Color{0.18f, 0.62f, 0.32f, 1.0f, wui::Token::None}
                      : wui::Color{0.78f, 0.30f, 0.24f, 1.0f, wui::Token::None};
    }

    wui::ObservableProperty<std::wstring> Text{"Text", L"online"};
    wui::ObservableProperty<wui::Color> Color{"Color", Tone(true)};
};

// One server card's data source. This is what `DataContext="{Binding ...}"`
// installs on the card subtree, so every plain `{Binding Name}` inside the card
// resolves here instead of on the page.
class ServerVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    ServerVm(std::wstring name, int port, bool enabled) {
        bump_.Set(std::make_shared<wui::RelayCommand>([this] { Port.Set(Port.Get() + 1); }));
        toggle_.Set(std::make_shared<wui::RelayCommand>([this] { Enabled.Set(!Enabled.Get()); }));

        Name.Set(std::move(name));
        Port.Set(port);
        Enabled.Set(enabled);

        status_ = std::make_shared<StatusVm>();
        Status.Set(std::static_pointer_cast<wui::RefObject>(status_));

        Make("Name", Name);
        Make("Port", Port);
        Make("Enabled", Enabled);
        Make("Status", Status);
        Make("BumpPortCommand", bump_);
        Make("ToggleCommand", toggle_);

        // The badge is derived from Enabled, so it is kept in step from the one
        // source of truth. A Toggle click and a programmatic change both end up
        // in SyncStatus, so the badge cannot go stale.
        Enabled.Subscribe(this);
        SyncStatus();
    }

    ~ServerVm() override { Enabled.Unsubscribe(this); }

private:
    void OnSourceChanged(const wui::Value&) override { SyncStatus(); }

    void SyncStatus() {
        const bool online = Enabled.Get();
        status_->Text.Set(online ? L"online" : L"offline");
        status_->Color.Set(StatusVm::Tone(online));
    }

public:
    wui::ObservableProperty<std::wstring> Name{"Name"};
    wui::ObservableProperty<int> Port{"Port", 0};
    wui::ObservableProperty<bool> Enabled{"Enabled", false};
    wui::ObservableProperty<wui::RefPtr> Status{"Status"};

private:
    wui::ObservableProperty<wui::RefPtr> bump_{"BumpPortCommand"};
    wui::ObservableProperty<wui::RefPtr> toggle_{"ToggleCommand"};
    std::shared_ptr<StatusVm> status_;
};

// Binds every named element in UserControl.waml:
//   PageTitle, Blurb, Summary, AlphaServer, BetaServer,
//   CyclePageTitleCommand
class UserControlVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    UserControlVm() {
        alpha_ = std::make_shared<ServerVm>(L"alpha", 8080, true);
        beta_ = std::make_shared<ServerVm>(L"beta", 9090, false);
        AlphaServer.Set(std::static_pointer_cast<wui::RefObject>(alpha_));
        BetaServer.Set(std::static_pointer_cast<wui::RefObject>(beta_));

        cycleTitle_.Set(std::make_shared<wui::RelayCommand>([this] { CyclePageTitle(); }));

        Make("PageTitle", PageTitle);
        Make("Blurb", Blurb);
        Make("Summary", Summary);
        Make("AlphaServer", AlphaServer);
        Make("BetaServer", BetaServer);
        Make("CyclePageTitleCommand", cycleTitle_);

        // The summary is the only thing that looks at both cards. It observes
        // their ports and enabled flags so an edit on either side is reflected;
        // the cards themselves never talk to the page.
        alpha_->Port.Subscribe(this);
        alpha_->Enabled.Subscribe(this);
        beta_->Port.Subscribe(this);
        beta_->Enabled.Subscribe(this);
        SyncSummary();
    }

    ~UserControlVm() override {
        alpha_->Port.Unsubscribe(this);
        alpha_->Enabled.Unsubscribe(this);
        beta_->Port.Unsubscribe(this);
        beta_->Enabled.Unsubscribe(this);
    }

private:
    void OnSourceChanged(const wui::Value&) override { SyncSummary(); }

    void CyclePageTitle() {
        titleIndex_ = (titleIndex_ + 1) % static_cast<int>(std::size(kPageTitles));
        PageTitle.Set(kPageTitles[titleIndex_]);
    }

    void SyncSummary() {
        auto line = [](const wchar_t* name, const ServerVm& server) {
            return std::wstring(name) + L" :" + std::to_wstring(server.Port.Get()) +
                   (server.Enabled.Get() ? L" online" : L" offline");
        };
        Summary.Set(line(L"alpha", *alpha_) + L"  \u00b7  " + line(L"beta", *beta_));
    }

    static constexpr const wchar_t* kPageTitles[] = {
        L"Server fleet", L"Theme: midnight", L"Theme: daylight"};

    std::shared_ptr<ServerVm> alpha_;
    std::shared_ptr<ServerVm> beta_;
    wui::ObservableProperty<wui::RefPtr> cycleTitle_{"CyclePageTitleCommand"};
    int titleIndex_ = 0;

public:
    // Read-only handles for the harness. A real page would not expose its
    // children's ViewModels, but asserting independence needs to see both sides.
    const std::shared_ptr<ServerVm>& Alpha() const { return alpha_; }
    const std::shared_ptr<ServerVm>& Beta() const { return beta_; }

    wui::ObservableProperty<std::wstring> PageTitle{"PageTitle", L"Server fleet"};
    wui::ObservableProperty<std::wstring> Blurb{
        "Blurb",
        L"The page owns the title. Each card below owns a server object and "
        L"installs it as its own DataContext, so the same bindings resolve "
        L"against three independent sources. Change one and watch the others."};
    // Derived from both cards, for the reader: it is not the cards' source.
    wui::ObservableProperty<std::wstring> Summary{"Summary"};
    wui::ObservableProperty<wui::RefPtr> AlphaServer{"AlphaServer"};
    wui::ObservableProperty<wui::RefPtr> BetaServer{"BetaServer"};
};

} // namespace sample
