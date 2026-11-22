// SPDX-License-Identifier: MIT
// Sample: the list page's ViewModel.
//
// SAMPLE code. Exists only to give samples/list/List.waml something to bind to.
#pragma once

#include "Mvvm.h"

#include <memory>
#include <string>

namespace sample {

// One row. `Kind` exists so the item template can show three different columns
// and prove each row resolves its own data.
class ListRow final : public wui::ObservableObject {
public:
    ListRow(int index, std::wstring title, std::wstring kind) {
        Index.Set(index);
        Title.Set(std::move(title));
        Kind.Set(std::move(kind));
        Make("Index", Index);
        Make("Title", Title);
        Make("Kind", Kind);
    }

    wui::ObservableProperty<int> Index{"Index"};
    wui::ObservableProperty<std::wstring> Title{"Title"};
    wui::ObservableProperty<std::wstring> Kind{"Kind"};
};

// Binds every named element in List.waml:
//   Name, Count, Items, ItemCount, AddCommand, RemoveCommand
class ListViewVm final : public wui::ObservableObject {
public:
    ListViewVm() {
        Add.Set(std::make_shared<wui::RelayCommand>([this] { AddRow(); }));
        Remove.Set(std::make_shared<wui::RelayCommand>([this] { RemoveRow(); }));

        Make("Name", Name);
        Make("Count", Count);
        Make("Items", Items);
        Make("ItemCount", ItemCount);
        Make("AddCommand", Add);
        Make("RemoveCommand", Remove);

        for (int i = 0; i < 5; ++i) AddRow();
    }

    // Appends a row, alternating the Kind column so the list visibly grows in
    // more than one dimension.
    void AddRow() {
        static const wchar_t* kinds[] = {L"layout", L"text", L"input", L"panel"};
        const int index = static_cast<int>(rows_.size()) + 1;
        const int kindIndex = (index - 1) % 4;
        auto row = std::make_shared<ListRow>(index, L"Item " + std::to_wstring(index),
                                             kinds[kindIndex]);
        rows_.push_back(row);
        if (list_) {
            list_->Add(row);
        } else {
            list_ = std::make_shared<wui::ObservableList<ListRow>>();
            for (auto& r : rows_) list_->Add(r);
            Items.Set(std::static_pointer_cast<wui::RefObject>(list_));
        }
        Count.Set(static_cast<int>(rows_.size()));
        ItemCount.Set(static_cast<int>(rows_.size()));
    }

    void RemoveRow() {
        if (rows_.empty()) return;
        rows_.pop_back();
        if (list_) list_->RemoveAt(rows_.size());
        Count.Set(static_cast<int>(rows_.size()));
        ItemCount.Set(static_cast<int>(rows_.size()));
    }

    wui::ObservableProperty<std::wstring> Name{"Name", L"Mixed layouts"};
    wui::ObservableProperty<int> Count{"Count", 0};
    wui::ObservableProperty<int> ItemCount{"ItemCount", 0};
    wui::ObservableProperty<wui::RefPtr> Items{"Items"};
    wui::ObservableProperty<wui::RefPtr> Add{"AddCommand"};
    wui::ObservableProperty<wui::RefPtr> Remove{"RemoveCommand"};

private:
    std::shared_ptr<wui::ObservableList<ListRow>> list_;
    std::vector<std::shared_ptr<ListRow>> rows_;
};

} // namespace sample
