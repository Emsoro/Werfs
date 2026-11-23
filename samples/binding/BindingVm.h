// SPDX-License-Identifier: MIT
// Sample: the binding-modes page's ViewModel.
//
// SAMPLE code. One property (`Name`) is bound four ways; that is the whole
// point of the page, so the ViewModel stays as small as it can be. Nothing here
// knows which mode a control chose -- the mode lives in the markup.
#pragma once

#include "Mvvm.h"

#include <iterator>
#include <memory>
#include <string>

namespace sample {

// Binds every named element in Binding.waml:
//   Heading, Blurb, Name, Count, RenameCommand, ResetCommand
//
// `Count` exists so StringFormat has something to decorate and so the page can
// show that a command ran even when the text happened to stay the same.
class BindingVm final : public wui::ObservableObject {
public:
    BindingVm() {
        rename_.Set(std::make_shared<wui::RelayCommand>([this] { OnRename(); }));
        reset_.Set(std::make_shared<wui::RelayCommand>([this] { OnReset(); }));

        Make("Heading", Heading);
        Make("Blurb", Blurb);
        Make("Name", Name);
        Make("Count", Count);
        Make("RenameCommand", rename_);
        Make("ResetCommand", reset_);
    }

    void OnRename() {
        nameIndex_ = (nameIndex_ + 1) % static_cast<int>(std::size(kNames));
        Name.Set(kNames[nameIndex_]);
        Count.Set(Count.Get() + 1);
    }

    void OnReset() {
        nameIndex_ = 0;
        Name.Set(kNames[0]);
        Count.Set(0);
    }

    wui::ObservableProperty<std::wstring> Heading{"Heading", L"Binding modes"};
    wui::ObservableProperty<std::wstring> Blurb{
        "Blurb",
        L"One ViewModel property, four binding modes, four different behaviours. "
        L"Change the source and see which columns move; type into a box and see "
        L"which ones write back."};
    wui::ObservableProperty<std::wstring> Name{"Name", L"Ada Lovelace"};
    wui::ObservableProperty<int> Count{"Count", 0};

private:
    static constexpr const wchar_t* kNames[] = {L"Ada Lovelace", L"Grace Hopper", L"Alan Turing"};

    wui::ObservableProperty<wui::RefPtr> rename_{"RenameCommand"};
    wui::ObservableProperty<wui::RefPtr> reset_{"ResetCommand"};
    int nameIndex_ = 0;
};

} // namespace sample
