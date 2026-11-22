// SPDX-License-Identifier: MIT
// Sample: the Gallery page's ViewModel.
//
// This is SAMPLE code, not framework code. It exists only to give
// samples/gallery/Gallery.waml something to bind to, and it is the single
// definition shared by every tool (demo, snapshot, interact, dpishot, bench,
// tests) instead of each tool carrying its own private copy.
//
// Framework headers are included by their framework name; this sample only
// depends on the public surface of the MVVM layer.
#pragma once

#include "Mvvm.h"

#include <memory>
#include <string>
#include <utility>

namespace sample {

// One row in the ListBox: {Binding Index} and {Binding Title}.
class GalleryItem final : public wui::ObservableObject {
public:
    explicit GalleryItem(int index, std::wstring title = {}) {
        Title.Set(title.empty() ? L"Item " + std::to_wstring(index) : std::move(title));
        Index.Set(index);
        Make("Title", Title);
        Make("Index", Index);
    }

    wui::ObservableProperty<std::wstring> Title{"Title"};
    wui::ObservableProperty<int> Index{"Index"};
};

// Binds every named element in samples/gallery/Gallery.waml:
//   Title, Count, Name, ShowSuggestions, Log, Items,
//   IncrementCommand, DecrementCommand
//
// Item objects expose Title and Index (see GalleryItem).
class GalleryVm final : public wui::ObservableObject {
public:
    GalleryVm() {
        Inc.Set(std::make_shared<wui::RelayCommand>([this] {
            Count.Set(Count.Get() + 1);
            Log.Set(L"increment -> " + std::to_wstring(Count.Get()));
        }));
        Dec.Set(std::make_shared<wui::RelayCommand>([this] {
            Count.Set(Count.Get() - 1);
            Log.Set(L"decrement -> " + std::to_wstring(Count.Get()));
        }));

        Make("Title", Title);
        Make("Count", Count);
        Make("Name", Name);
        Make("ShowSuggestions", Enabled);
        Make("Log", Log);
        Make("Items", Items);
        Make("IncrementCommand", Inc);
        Make("DecrementCommand", Dec);
    }

    // Fills the list with `count` rows, numbered from 1, titled "Item N".
    void SeedItems(int count) {
        auto list = std::make_shared<wui::ObservableList<GalleryItem>>();
        for (int i = 0; i < count; ++i) list->Add(std::make_shared<GalleryItem>(i + 1));
        list_ = list;
        Items.Set(std::static_pointer_cast<wui::RefObject>(list));
    }

    wui::ObservableProperty<std::wstring> Title{"Title", L"WAML + MVVM sample"};
    wui::ObservableProperty<int> Count{"Count", 0};
    wui::ObservableProperty<std::wstring> Name{"Name", L""};
    wui::ObservableProperty<bool> Enabled{"ShowSuggestions", true};
    wui::ObservableProperty<std::wstring> Log{"Log", L"(interact with the controls)"};
    wui::ObservableProperty<wui::RefPtr> Items{"Items"};
    wui::ObservableProperty<wui::RefPtr> Inc{"IncrementCommand"};
    wui::ObservableProperty<wui::RefPtr> Dec{"DecrementCommand"};

private:
    std::shared_ptr<wui::ObservableList<GalleryItem>> list_;
};

} // namespace sample
