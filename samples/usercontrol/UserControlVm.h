// SPDX-License-Identifier: MIT
// Sample: the component-scope page's ViewModels.
//
// The point of the page is the SHAPE of the data: one page object, a list of server
// objects that each become a subtree's DataContext, and a third object inside each of
// those for the status badge. Every plain {Binding X} in the markup resolves against the
// object its nearest DataContext installed, so the same template renders three rows that
// agree on nothing.
//
//   UserControlVm            the page: title, blurb, the summary line
//    └ Cards[i]: ServerVm    one per row: name, port, enabled
//         └ StatusVm         the badge's OWN source: text and tone
//
// The third level is the part worth looking at. A badge that simply bound the card's
// Enabled would work, but it would not demonstrate nesting; this one is a component with
// its own data source, which is the harder case and the one that actually breaks when the
// DataContext hand-off is wrong.
#pragma once

#include "Mvvm.h"

#include <memory>
#include <string>

namespace sample {

// The badge's own object. The card hands this down through a second DataContext binding,
// which is what makes the badge a component inside a component rather than another copy of
// the card's bindings.
class StatusVm final : public wui::ObservableObject {
public:
    StatusVm() {
        Make("Text", Text);
        Make("Color", Color);
    }

    // A literal tone: a status light is not a themed surface, so it does not follow the
    // palette. Tokens are for surfaces and text, not for meaning.
    static wui::Color Tone(bool online) {
        return online ? wui::Color{0.18f, 0.62f, 0.32f, 1.0f, wui::Token::None}
                      : wui::Color{0.78f, 0.30f, 0.24f, 1.0f, wui::Token::None};
    }

    wui::ObservableProperty<std::wstring> Text{"Text", L"online"};
    wui::ObservableProperty<wui::Color> Color{"Color", Tone(true)};
};

// One server card's data source. This is what the item template's DataContext installs, so
// every plain {Binding Name} inside a card resolves here instead of on the page.
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

        PortText.Set(std::to_wstring(port));

        Make("Name", Name);
        Make("Port", Port);
        Make("PortText", PortText);
        Make("Enabled", Enabled);
        Make("Status", Status);
        Make("BumpPortCommand", bump_);
        Make("ToggleCommand", toggle_);

        // The badge is derived from Enabled, so it is kept in step from the one source of
        // truth. A command click and a programmatic change both land in SyncStatus, so the
        // badge cannot go stale.
        Enabled.Subscribe(this);
        Port.Subscribe(this);
        SyncStatus();
    }

    ~ServerVm() override {
        Enabled.Unsubscribe(this);
        Port.Unsubscribe(this);
    }

public:
    wui::ObservableProperty<std::wstring> Name{"Name"};
    wui::ObservableProperty<int> Port{"Port", 0};
    // A string mirror of Port so the caption can carry text without the control's own value
    // becoming a string.
    wui::ObservableProperty<std::wstring> PortText{"PortText"};
    wui::ObservableProperty<bool> Enabled{"Enabled", false};
    wui::ObservableProperty<wui::RefPtr> Status{"Status"};

private:
    void OnSourceChanged(const wui::Value&) override { SyncStatus(); }

    void SyncStatus() {
        PortText.Set(std::to_wstring(Port.Get()));
        const bool online = Enabled.Get();
        status_->Text.Set(online ? L"online" : L"offline");
        status_->Color.Set(StatusVm::Tone(online));
    }

    wui::ObservableProperty<wui::RefPtr> bump_{"BumpPortCommand"};
    wui::ObservableProperty<wui::RefPtr> toggle_{"ToggleCommand"};
    std::shared_ptr<StatusVm> status_;
};

// Binds every named element in UserControl.waml:
//   PageTitle, Blurb, Summary, Cards, CyclePageTitleCommand
class UserControlVm final : public wui::ObservableObject,
                            private wui::IPropertyObserver,
                            private wui::IListObserver {
public:
    UserControlVm() {
        // Three servers that agree on nothing. If a card or its badge ever read the page's
        // context, all three rows would look the same and the interaction check would catch
        // it.
        alpha_ = std::make_shared<ServerVm>(L"alpha", 8080, true);
        beta_ = std::make_shared<ServerVm>(L"beta", 9090, false);
        auto gamma = std::make_shared<ServerVm>(L"gamma", 7070, true);
        cardsStorage_->Add(std::static_pointer_cast<wui::RefObject>(alpha_));
        cardsStorage_->Add(std::static_pointer_cast<wui::RefObject>(beta_));
        cardsStorage_->Add(std::static_pointer_cast<wui::RefObject>(gamma));

        CardsProperty.Set(cardsStorage_);
        cycleTitle_.Set(std::make_shared<wui::RelayCommand>([this] { CyclePageTitle(); }));

        Make("PageTitle", PageTitle);
        Make("Blurb", Blurb);
        Make("Summary", Summary);
        Make("Cards", CardsProperty);
        Make("OnlineCount", OnlineCount);
        Make("CyclePageTitleCommand", cycleTitle_);

        // The summary is the only thing that looks at every card. It observes them so an
        // edit on any card is reflected; the cards themselves never talk to the page.
        alpha_->Port.Subscribe(this);
        alpha_->Enabled.Subscribe(this);
        beta_->Port.Subscribe(this);
        beta_->Enabled.Subscribe(this);
        cardsStorage_->Subscribe(this);
        SyncSummary();
    }

    ~UserControlVm() override {
        alpha_->Port.Unsubscribe(this);
        alpha_->Enabled.Unsubscribe(this);
        beta_->Port.Unsubscribe(this);
        beta_->Enabled.Unsubscribe(this);
        cardsStorage_->Unsubscribe(this);
    }

    // Read-only handles for the harness. A real page would not expose its children's
    // ViewModels, but asserting independence needs to see both sides.
    const std::shared_ptr<ServerVm>& Alpha() const { return alpha_; }
    const std::shared_ptr<ServerVm>& Beta() const { return beta_; }
    const std::shared_ptr<wui::ObservableList<wui::RefObject>>& CardList() const {
        return cardsStorage_;
    }

    wui::ObservableProperty<std::wstring> PageTitle{"PageTitle", L"Server fleet"};
    wui::ObservableProperty<std::wstring> Blurb{
        "Blurb",
        L"The page owns the title. Each card below owns a server object and installs it as "
        L"its own DataContext, and the badge inside a card installs a third. Change one and "
        L"watch the others stay put."};
    // Derived from every card, for the reader: it is not the cards' source.
    wui::ObservableProperty<std::wstring> Summary{"Summary"};
    wui::ObservableProperty<std::wstring> OnlineCount{"OnlineCount"};
    wui::ObservableProperty<wui::RefPtr> CardsProperty{"Cards"};

private:
    void OnSourceChanged(const wui::Value&) override { SyncSummary(); }
    void OnListChanged(const wui::ListChange&) override { SyncSummary(); }

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

        int online = 0;
        for (size_t i = 0; i < cardsStorage_->Count(); ++i) {
            if (auto* card = dynamic_cast<ServerVm*>(cardsStorage_->At(i).get())) {
                if (card->Enabled.Get()) ++online;
            }
        }
        OnlineCount.Set(std::to_wstring(online) + L" of " +
                        std::to_wstring(cardsStorage_->Count()) + L" online");
    }

    static constexpr const wchar_t* kPageTitles[] = {
        L"Server fleet", L"Theme: midnight", L"Theme: daylight"};

    std::shared_ptr<wui::ObservableList<wui::RefObject>> cardsStorage_ =
        std::make_shared<wui::ObservableList<wui::RefObject>>();
    std::shared_ptr<ServerVm> alpha_;
    std::shared_ptr<ServerVm> beta_;
    wui::ObservableProperty<wui::RefPtr> cycleTitle_{"CyclePageTitleCommand"};
    int titleIndex_ = 0;
};

} // namespace sample
