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

// One log line for the virtualized list. Deliberately cheap and self-contained:
// the demonstration is about how many WIDGETS exist, so the model must not be the
// reason a big list is expensive.
class LogRow final : public wui::ObservableObject {
public:
    LogRow(int seq, std::wstring level) {
        Seq.Set(seq);
        // The row template prints `Seq` as "#N" in its own column, so the line text
        // must not repeat it: the first version rendered "#1  #1 line_height fixed".
        Line.Set(L"row_height fixed \u2014 content does not affect layout");
        Level.Set(std::move(level));
        Make("Seq", Seq);
        Make("Line", Line);
        Make("Level", Level);
    }

    wui::ObservableProperty<int> Seq{"Seq"};
    wui::ObservableProperty<std::wstring> Line{"Line"};
    wui::ObservableProperty<std::wstring> Level{"Level"};
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
//   LogItems, LogCount, LogRealized, LogVirtualized, LogFirstVisible,
//   AddManyLogsCommand, ClearLogsCommand, ScrollLogsCommand
class ListViewVm final : public wui::ObservableObject, private wui::IPropertyObserver {
public:
    ListViewVm() {
        Add.Set(std::make_shared<wui::RelayCommand>([this] { AddRow(); }));
        Remove.Set(std::make_shared<wui::RelayCommand>([this] { RemoveRow(); }));
        AddTab.Set(std::make_shared<wui::RelayCommand>([this] { AddDoc(); }));
        AddManyLogs.Set(std::make_shared<wui::RelayCommand>([this] { AddLogs(1000); }));
        ClearLogs.Set(std::make_shared<wui::RelayCommand>([this] { ClearLogRows(); }));
        ScrollLogs.Set(std::make_shared<wui::RelayCommand>([this] { NudgeLogScroll(); }));

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

        Make("LogItems", LogItems);
        Make("LogCount", LogCount);
        Make("LogRealized", LogRealized);
        Make("LogVirtualized", LogVirtualized);
        Make("LogFirstVisible", LogFirstVisible);
        Make("LogScroll", LogScroll);
        Make("LogScrollMax", LogScrollMax);
        Make("LogSummary", LogSummary);
        Make("AddManyLogsCommand", AddManyLogs);
        Make("ClearLogsCommand", ClearLogs);
        Make("ScrollLogsCommand", ScrollLogs);

        for (int i = 0; i < 5; ++i) AddRow();
        SeedDocuments();
        SeedDocuments();
        // Observe our own properties so a change made by ANY source refreshes the
        // labels: the tab strip writes ActiveTab, and the list control writes its
        // window properties. Doing it here rather than at each assignment site is
        // what keeps the summary from drifting (§7.16's lesson, applied forwards).
        ActiveTab.Subscribe(this);
        logSync_ = std::make_unique<LogSync>(this);
        LogRealized.Subscribe(logSync_.get());
        LogFirstVisible.Subscribe(logSync_.get());
        // IsVirtualized MUST be in this list. Leaving it out produced a summary that
        // read "realized widgets = 12 · virtualized = no" -- the two facts came from
        // the same control and disagreed, because the label was refreshed when
        // RealizedCount arrived (still the old virtualized flag) and never again when
        // the flag itself arrived. A stale label next to live data is worse than no
        // label: it reads as evidence.
        LogVirtualized.Subscribe(logSync_.get());
        SeedLogs();
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

    ~ListViewVm() override {
        ActiveTab.Unsubscribe(this);
        // Unsubscribe before the observer dies: the list control may outlive us in
        // teardown order, and a dangling observer is exactly the §7.18 defect.
        LogRealized.Unsubscribe(logSync_.get());
        LogFirstVisible.Unsubscribe(logSync_.get());
        LogVirtualized.Unsubscribe(logSync_.get());
    }

    // Reacts to ActiveTab changing from ANY source -- the control's click, or
    // AddDoc(). That is why this is a property observer rather than a call at
    // each assignment site: the summary cannot fall out of date.
    void OnSourceChanged(const wui::Value& value) override {
        if (value.type == wui::VType::Number) ActiveTab.Set(static_cast<int>(value.num));
        SyncTabSummary();
    }

    // Same idea for the virtualized list's window counters: the control writes them
    // every time it scrolls, and the summary text has to follow. A named nested type
    // rather than making ListViewVm observe itself twice, because ActiveTab's
    // callback means something different from these.
    struct LogSync final : wui::IPropertyObserver {
        explicit LogSync(ListViewVm* vm) : vm_(vm) {}
        void OnSourceChanged(const wui::Value&) override { vm_->SyncLogState(); }
        ListViewVm* vm_;
    };

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

    // --- the virtualised list ------------------------------------------------
    //
    // 1000 log rows, drawn inside a 260px-tall ListBox. The point of the whole
    // section is that this costs the same as a 20-row list: the control only holds
    // the rows inside the viewport. The Log* properties below are the evidence, and
    // the ones describing the window come FROM the control -- the ViewModel never
    // counts widgets itself, because a ViewModel that knows about widgets is not a
    // ViewModel.
    //
    // The labels are refreshed from the page's layout pass (see ListApp.cpp), not
    // from a timer: the numbers are only meaningful after the control has laid out,
    // and pretending otherwise would show "0" under a heading that claims otherwise.
    void SeedLogs() {
        logRows_ = std::make_shared<wui::ObservableList<LogRow>>();
        for (int i = 1; i <= 1000; ++i) logRows_->Add(MakeLogRow(i));
        LogItems.Set(std::static_pointer_cast<wui::RefObject>(logRows_));
        SyncLogState();
    }

    static std::shared_ptr<LogRow> MakeLogRow(int seq) {
        static const wchar_t* levels[] = {L"info", L"warn", L"error"};
        return std::make_shared<LogRow>(seq, levels[seq % 3]);
    }

    void AddLogs(int count) {
        if (!logRows_) return;
        const int start = static_cast<int>(logRows_->Count()) + 1;
        for (int i = 0; i < count; ++i) logRows_->Add(MakeLogRow(start + i));
        SyncLogState();
    }

    void ClearLogRows() {
        if (!logRows_) return;
        logRows_->Clear();
        SyncLogState();
    }

    // "Scroll +400" is a ViewModel command writing a bound property -- the page
    // never touches the control. ScrollOffset is two-way, so the control both reads
    // it (an instruction) and writes it back (its real position after clamping), and
    // ScrollMax tells the ViewModel where the end is without it re-deriving the
    // viewport height.
    void NudgeLogScroll() {
        // Doubles throughout: the slot stores a double, and narrowing at the
        // assignment is exactly the /W4 warning a sample should not be shipping.
        const double next = LogScroll.Get() + 400.0;
        LogScroll.Set(next > LogScrollMax.Get() ? 0.0 : next);
    }

    // Recomputes the labels from whatever the CONTROL last published. Called after
    // a layout, and whenever the model changes.
    void SyncLogState() {
        LogCount.Set(logRows_ ? static_cast<int>(logRows_->Count()) : 0);
        const int total = LogCount.Get();
        if (total == 0) {
            LogSummary.Set(L"No rows. The one case worth not crashing on.");
            return;
        }
        const int realized = LogRealized.Get();
        const int first = LogFirstVisible.Get();
        LogSummary.Set(L"model = " + std::to_wstring(total) + L" rows · realized widgets = " +
                       std::to_wstring(realized) + L" (model rows " + std::to_wstring(first) +
                       L".." + std::to_wstring(first + realized) + L") · virtualized = " +
                       (LogVirtualized.Get() ? L"yes" : L"no (no bounded height)"));
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

    wui::ObservableProperty<wui::RefPtr> LogItems{"LogItems"};
    wui::ObservableProperty<int> LogCount{"LogCount", 0};
    wui::ObservableProperty<int> LogRealized{"LogRealized", 0};
    wui::ObservableProperty<bool> LogVirtualized{"LogVirtualized", false};
    wui::ObservableProperty<int> LogFirstVisible{"LogFirstVisible", 0};
    wui::ObservableProperty<double> LogScroll{"LogScroll", 0.0};
    wui::ObservableProperty<double> LogScrollMax{"LogScrollMax", 0.0};
    wui::ObservableProperty<std::wstring> LogSummary{"LogSummary"};
    wui::ObservableProperty<wui::RefPtr> AddManyLogs{"AddManyLogsCommand"};
    wui::ObservableProperty<wui::RefPtr> ClearLogs{"ClearLogsCommand"};
    wui::ObservableProperty<wui::RefPtr> ScrollLogs{"ScrollLogsCommand"};

private:
    std::shared_ptr<wui::ObservableList<ListRow>> list_;
    std::vector<std::shared_ptr<ListRow>> rows_;
    std::shared_ptr<wui::ObservableList<TabDoc>> docs_;
    std::shared_ptr<wui::ObservableList<LogRow>> logRows_;
    std::unique_ptr<LogSync> logSync_;
};
} // namespace sample
