// SPDX-License-Identifier: MIT
// Micro benchmark driver for the spike report numbers.
#include "Controls.h"
#include "Core.h"
#include "Diagnostics.h"
#include "Mvvm.h"
#include "SoftwareRender.h"
#include "Types.h"
#include "Waml.h"
#include "Widget.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace wui;

namespace {

FakeTextService g_text;

double NowUs() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double, std::micro>(clock::now() - start).count();
}

struct Stats {
    double median = 0;
    double p99 = 0;
    double min = 0;
    double max = 0;
};

Stats Summarize(std::vector<double> v) {
    Stats s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.p99 = v[static_cast<size_t>(static_cast<double>(v.size()) * 0.99)];
    s.min = v.front();
    s.max = v.back();
    return s;
}

std::string BigDocument(int listItems, int repeatBlocks) {
    std::string s = "<Page Background=\"AppBackground\"><StackPanel Margin=\"24\" Spacing=\"12\">";
    for (int r = 0; r < repeatBlocks; ++r) {
        s += "<TextBlock Text=\"{Binding Title}\" FontSize=\"28\" FontWeight=\"SemiBold\"/>";
        s += "<StackPanel Orientation=\"Horizontal\" Spacing=\"8\">";
        s += "<Button Content=\"-\" Command=\"{Binding Dec}\"/>";
        s += "<TextBlock Text=\"{Binding Count, StringFormat='Count: {0}'}\"/>";
        s += "<Button Content=\"+\" Command=\"{Binding Inc}\"/>";
        s += "</StackPanel>";
        s += "<TextBox Text=\"{Binding Name, Mode=TwoWay}\"/>";
        s += "<CheckBox Content=\"Enabled\" IsChecked=\"{Binding Enabled, Mode=TwoWay}\"/>";
    }
    s += "<ListBox ItemsSource=\"{Binding Items}\">";
    s += "<ListBox.ItemTemplate><DataTemplate><TextBlock Text=\"{Binding Title}\"/></DataTemplate>";
    s += "</ListBox.ItemTemplate></ListBox>";
    s += "</StackPanel></Page>";
    (void)listItems;
    return s;
}

class BenchVm final : public ObservableObject {
public:
    BenchVm() {
        Make("Title", Title);
        Make("Count", Count);
        Make("Name", Name);
        Make("Enabled", Enabled);
        Make("Items", Items);
    }
    ObservableProperty<std::wstring> Title{"Title", L"bench"};
    ObservableProperty<int> Count{"Count", 0};
    ObservableProperty<std::wstring> Name{"Name", L"bench"};
    ObservableProperty<bool> Enabled{"Enabled", true};
    ObservableProperty<RefPtr> Items{"Items"};
};

class BenchItem final : public ObservableObject {
public:
    explicit BenchItem(int i) : Title{"Title", L"Item " + std::to_wstring(i)} {
        Make("Title", Title);
    }
    ObservableProperty<std::wstring> Title;
};

} // namespace

int main() {
    TextSystem::Set(&g_text);

    // ---- parse -------------------------------------------------------------
    const std::string small = BigDocument(0, 1);
    const std::string medium = BigDocument(0, 5);
    const std::string large = BigDocument(0, 20);
    const std::vector<std::pair<const char*, std::string>> docs = {
        {"small(1 block)", small}, {"medium(5 blocks)", medium}, {"large(20 blocks)", large}};
    std::printf("== parse+validate ==\n");
    for (const auto& [name, src] : docs) {
        std::vector<double> samples;
        for (int i = 0; i < 300; ++i) {
            ParseOptions opt;
            opt.fileName = "bench.waml";
            DiagnosticBag bag;
            const double t0 = NowUs();
            auto doc = CompileWaml(src, opt, bag);
            const double t1 = NowUs();
            if (!doc) {
                std::printf("compile failed for %s:\n%s", name, bag.ToText().c_str());
                return 1;
            }
            if (i >= 20) samples.push_back(t1 - t0);
        }
        const Stats s = Summarize(samples);
        std::printf("%-16s %6zu bytes  median %7.1f us  p99 %7.1f us  (%.2f MB/s)\n", name,
                    src.size(), s.median, s.p99, static_cast<double>(src.size()) / s.median);
    }

    // ---- instantiate -------------------------------------------------------
    std::printf("\n== instantiate ==\n");
    for (const auto& [name, src] : docs) {
        ParseOptions opt;
        opt.fileName = "bench.waml";
        DiagnosticBag bag;
        auto doc = CompileWaml(src, opt, bag);
        std::vector<double> samples;
        for (int i = 0; i < 300; ++i) {
            const double t0 = NowUs();
            WidgetPtr w = doc->Instantiate(&g_text);
            const double t1 = NowUs();
            if (i >= 20) samples.push_back(t1 - t0);
        }
        const Stats s = Summarize(samples);
        std::printf("%-16s %3zu nodes  median %7.1f us  p99 %7.1f us\n", name, doc->nodeCount,
                    s.median, s.p99);
    }

    // ---- binding updates ---------------------------------------------------
    std::printf("\n== binding ==\n");
    {
        ParseOptions opt;
        opt.fileName = "b.waml";
        DiagnosticBag bag;
        auto doc = CompileWaml("<TextBlock Text=\"{Binding Title}\"/>", opt, bag);
        WidgetPtr root = doc->Instantiate(&g_text);
        auto vm = std::make_shared<BenchVm>();
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        const int n = 2000000;
        for (int i = 0; i < 5000; ++i) vm->Title.Set(i % 2 ? L"A" : L"B");
        const double t0 = NowUs();
        for (int i = 0; i < n; ++i) vm->Title.Set(i % 2 ? L"A" : L"B");
        const double t1 = NowUs();
        std::printf("oneway      %.1f ns/update\n", (t1 - t0) * 1000.0 / n);
    }
    {
        ParseOptions opt;
        opt.fileName = "b.waml";
        DiagnosticBag bag;
        auto doc = CompileWaml("<TextBox Text=\"{Binding Name, Mode=TwoWay}\"/>", opt, bag);
        WidgetPtr root = doc->Instantiate(&g_text);
        auto vm = std::make_shared<BenchVm>();
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        const int slot = root->SlotOf("Text");
        const int n = 1000000;
        for (int i = 0; i < 2000; ++i) root->SetSlotValue(slot, Value::Text(L"warm"));
        const double t0 = NowUs();
        for (int i = 0; i < n; ++i) root->SetSlotValue(slot, Value::Text(i % 2 ? L"A" : L"B"));
        const double t1 = NowUs();
        std::printf("twoway      %.1f ns/update\n", (t1 - t0) * 1000.0 / n);
    }

    // ---- layout / paint / list --------------------------------------------
    std::printf("\n== layout & list ==\n");
    for (const auto& [name, src] : docs) {
        ParseOptions opt;
        opt.fileName = "l.waml";
        DiagnosticBag bag;
        auto doc = CompileWaml(src, opt, bag);
        WidgetPtr root = doc->Instantiate(&g_text);
        auto vm = std::make_shared<BenchVm>();
        auto list = std::make_shared<wui::ObservableList<BenchItem>>();
        for (int i = 0; i < 100; ++i) list->Add(std::make_shared<BenchItem>(i));
        vm->Items.Set(std::static_pointer_cast<RefObject>(list));
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        std::vector<double> samples;
        for (int i = 0; i < 200; ++i) {
            const double t0 = NowUs();
            root->ForceLayout(Size{800, 2000});
            const double t1 = NowUs();
            if (i >= 10) samples.push_back(t1 - t0);
        }
        const Stats s = Summarize(samples);
        std::printf("%-16s layout median %7.1f us  p99 %7.1f us\n", name, s.median, s.p99);
    }

    std::printf("\n== render (software raster, 1280x800) ==\n");
    {
        ParseOptions opt;
        opt.fileName = "r.waml";
        DiagnosticBag bag;
        auto doc = CompileWaml(medium, opt, bag);
        WidgetPtr root = doc->Instantiate(&g_text);
        auto vm = std::make_shared<BenchVm>();
        auto list = std::make_shared<wui::ObservableList<BenchItem>>();
        for (int i = 0; i < 100; ++i) list->Add(std::make_shared<BenchItem>(i));
        vm->Items.Set(std::static_pointer_cast<RefObject>(list));
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        root->ForceLayout(Size{1280, 800});
        SoftwareRasterizer raster(1280, 800);
        std::vector<double> samples;
        for (int i = 0; i < 120; ++i) {
            const double t0 = NowUs();
            RasterPaintContext ctx(raster);
            root->PaintTree(ctx, LightPalette());
            const double t1 = NowUs();
            if (i >= 10) samples.push_back(t1 - t0);
        }
        const Stats s = Summarize(samples);
        std::printf("paint       median %7.1f us  p99 %7.1f us\n", s.median, s.p99);
    }

    // ---- 100 item list rebuild --------------------------------------------
    std::printf("\n== DataTemplate 100 items ==\n");
    {
        ParseOptions opt;
        opt.fileName = "t.waml";
        DiagnosticBag bag;
        const std::string src =
            "<ListBox ItemsSource=\"{Binding Items}\">"
            "<ListBox.ItemTemplate><DataTemplate>"
            "<StackPanel Orientation=\"Horizontal\" Spacing=\"6\">"
            "<TextBlock Text=\"{Binding Title}\"/><TextBlock Text=\"x\"/></StackPanel>"
            "</DataTemplate></ListBox.ItemTemplate></ListBox>";
        auto doc = CompileWaml(src, opt, bag);
        WidgetPtr root = doc->Instantiate(&g_text);
        auto vm = std::make_shared<BenchVm>();
        root->SetDataContext(std::static_pointer_cast<RefObject>(vm));
        std::vector<double> builds;
        for (int round = 0; round < 60; ++round) {
            auto list = std::make_shared<wui::ObservableList<BenchItem>>();
            for (int i = 0; i < 100; ++i) list->Add(std::make_shared<BenchItem>(i));
            const double t0 = NowUs();
            vm->Items.Set(std::static_pointer_cast<RefObject>(list));
            const double t1 = NowUs();
            if (round >= 10) builds.push_back(t1 - t0);
        }
        const Stats s = Summarize(builds);
        std::printf("build 100 items: median %7.1f us  p99 %7.1f us\n", s.median, s.p99);
    }

    return 0;
}
