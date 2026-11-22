// SPDX-License-Identifier: MIT
// Sample: the list page's ViewModel.
//
// SAMPLE code. Exists only to give samples/list/List.waml something to bind to.
#pragma once

#include "Controls.h"  // IBoundTitle: the tab strip reads a title from the model
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

// One document tab. `Title` is what the strip draws; the sample keeps it simple
// so the tab strip's own behaviour (selection, close button, dirty marker) is
// what stands out.
class TabDoc final : public wui::ObservableObject, public wui::IBoundTitle {
public:
    explicit TabDoc(std::wstring title) {
        Title.Set(std::move(title));
        Make("Title", Title);
    }

    // The tab strip reads this; WAML reads Title through a normal binding.
    // Both point at the same field, so renaming a document updates both.
    std::wstring BoundTitle() const override { return Title.Get(); }

    wui::ObservableProperty<std::wstring> Title{"Title"};
};

// Binds every named element in List.waml:
//   Name, Count, Items, ItemCount, AddCommand, RemoveCommand
//   Documents, ActiveTab, ActiveDocumentTitle, ActiveTabSummary, AddTabCommand
class ListViewVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    ListViewVm() {
        Add.Set(std::make_shared<wui::RelayCommand>([this] { AddRow(); }));
        Remove.Set(std::make_shared<wui::RelayCommand>([this] { RemoveRow(); }));
        AddTab.Set(std::make_shared<wui::RelayCommand>([this] { AddDoc(); }));

        Make("Name", Name);
        Make("Count", Count);
        Make("Items", Items);
        Make("ItemCount", ItemCount);
        Make("AddCommand", Add);
        Make("RemoveCommand", Remove);
        Make("Documents", Documents);
        Make("ActiveTab", ActiveTab);
        Make("ActiveDocumentTitle", ActiveDocumentTitle);
        Make("ActiveTabSummary", ActiveTabSummary);
        Make("AddTabCommand", AddTab);

        for (int i = 0; i < 5; ++i) AddRow();
        SeedDocuments();
        SeedDocuments();
        // Observe our own property so a click on the strip (which writes
        // through the TwoWay binding) updates the summary without the view
        // having to tell us what happened.
        ActiveTab.Subscribe(this);
    }

    void SeedDocuments() {
        docs_ = std::make_shared<wui::ObservableList<TabDoc>>();
        for (const wchar_t* title : {L"Overview", L"Details", L"History"}) {
            docs_->Add(std::make_shared<TabDoc>(title));
        }
        Documents.Set(std::static_pointer_cast<wui::RefObject>(docs_));
        ActiveTab.Set(0);
        SyncTabSummary();
    }

    void AddDoc() {
        if (!docs_) return;
        const int n = static_cast<int>(docs_->Count()) + 1;
        docs_->Add(std::make_shared<TabDoc>(L"Doc " + std::to_wstring(n)));
        // Selecting the tab we just created is what a user expects; leaving the
        // old one selected makes the + look broken.
        ActiveTab.Set(n - 1);
        SyncTabSummary();
    }

    // The ViewModel, not the control, decides what "active" means -- the tab
    // strip only reports which index was clicked.
    void SyncTabSummary() {
        const int index = ActiveTab.Get();
        std::wstring title = L"(none)";
        if (docs_ && index >= 0 && static_cast<size_t>(index) < docs_->Count()) {
            // ItemAt hands back the base pointer; the tab model carries the title.
            if (auto doc = std::dynamic_pointer_cast<TabDoc>(docs_->ItemAt(static_cast<size_t>(index)))) {
                title = doc->Title.Get();
            }
        }
        ActiveDocumentTitle.Set(title);
        ActiveTabSummary.Set(L"Active tab: " + std::to_wstring(index) + L" \u2014 " + title +
                             L". Click another tab, or + to add one.");
    }

    ~ListViewVm() override { ActiveTab.Unsubscribe(this); }

    // Reacts to ActiveTab changing from ANY source -- the control's click, or
    // AddDoc(). That is why this is a property observer rather than a call at
    // each assignment site: the summary cannot fall out of date.
    void OnSourceChanged(const wui::Value& value) override {
        if (value.type == wui::VType::Number) ActiveTab.Set(static_cast<int>(value.num));
        SyncTabSummary();
    }

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

    wui::ObservableProperty<wui::RefPtr> Documents{"Documents"};
    wui::ObservableProperty<int> ActiveTab{"ActiveTab", 0};
    wui::ObservableProperty<std::wstring> ActiveDocumentTitle{"ActiveDocumentTitle"};
    wui::ObservableProperty<std::wstring> ActiveTabSummary{"ActiveTabSummary"};
    wui::ObservableProperty<wui::RefPtr> AddTab{"AddTabCommand"};

private:
    std::shared_ptr<wui::ObservableList<ListRow>> list_;
    std::vector<std::shared_ptr<ListRow>> rows_;
    std::shared_ptr<wui::ObservableList<TabDoc>> docs_;
};
} // namespace sample
