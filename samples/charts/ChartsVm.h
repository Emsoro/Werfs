// SPDX-License-Identifier: MIT
// Sample: the charts page's ViewModel -- a LIVE signal, a mixed bar/line figure
// and a K-line (candlestick) figure.
//
// The page demonstrates three different questions:
//  1. a million-point ring buffer that keeps sliding while you look at it;
//  2. fifty discrete measurements with a fifty-point trend line over them,
//     where the reader chooses which encoding to show and what colour it is;
//  3. OHLC candles over a time range the reader picks with the two sliders.
//
// Only the figure actually inside the ScrollViewer viewport is painted. The
// control publishes that fact back through Chart.ViewportVisible, so this
// ViewModel stops advancing the live stream when figure 1 is scrolled away --
// a paused chart is not merely clipped, it is not doing work.
#pragma once

#include "Chart.h"
#include "ChartData.h"
#include "Mvvm.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace sample {

class ChartsVm final : public wui::ObservableObject {
public:
    ChartsVm() {
        ToggleStream.Set(std::make_shared<wui::RelayCommand>([this] { SetStreaming(!streaming_); }));
        DownsampleOn.Set(std::make_shared<wui::RelayCommand>([this] { SetDownsample(true); }));
        DownsampleOff.Set(std::make_shared<wui::RelayCommand>([this] { SetDownsample(false); }));
        Reset.Set(std::make_shared<wui::RelayCommand>([this] { ResetStream(); }));
        RangeAll.Set(std::make_shared<wui::RelayCommand>([this] {
            RangeStart.Set(0);
            RangeEnd.Set(kCandleCount - 1);
        }));
        Range30.Set(std::make_shared<wui::RelayCommand>([this] {
            RangeStart.Set(kCandleCount - 30);
            RangeEnd.Set(kCandleCount - 1);
        }));
        Range7.Set(std::make_shared<wui::RelayCommand>([this] {
            RangeStart.Set(kCandleCount - 7);
            RangeEnd.Set(kCandleCount - 1);
        }));

        Make("ToggleStreamCommand", ToggleStream);
        Make("DownsampleOnCommand", DownsampleOn);
        Make("DownsampleOffCommand", DownsampleOff);
        Make("ResetCommand", Reset);
        Make("RangeAllCommand", RangeAll);
        Make("Range30Command", Range30);
        Make("Range7Command", Range7);
        Make("Stream", Stream);
        Make("DownsampleMode", DownsampleModeProp);
        Make("Streaming", Streaming);
        Make("ShowFps", ShowFpsProp);
        Make("DrawnPoints", DrawnPoints);
        Make("SourcePoints", SourcePoints);
        Make("Timing", Timing);
        Make("ModeLabel", ModeLabel);
        Make("StreamLabel", StreamLabel);
        Make("StreamStateLabel", StreamStateLabel);
        Make("PointsLabel", PointsLabel);
        Make("FrameBudgetNote", FrameBudgetNote);
        Make("ToggleStreamText", ToggleStreamText);

        Make("BarSeries", BarSeries);
        Make("LineSeries", LineSeries);
        Make("BarColor", BarColor);
        Make("LineColor", LineColor);
        Make("BarR", BarR);
        Make("BarG", BarG);
        Make("BarB", BarB);
        Make("LineR", LineR);
        Make("LineG", LineG);
        Make("LineB", LineB);
        Make("ShowBars", ShowBars);
        Make("ShowLine", ShowLine);
        Make("ComparisonLabel", ComparisonLabel);

        Make("CandleSeries", CandleSeries);
        Make("CandleUpColor", CandleUpColor);
        Make("CandleDownColor", CandleDownColor);
        Make("RangeStart", RangeStart);
        Make("RangeEnd", RangeEnd);
        Make("RangeLabel", RangeLabel);
        Make("CandleNote", CandleNote);

        Make("Chart1Visible", Chart1Visible);
        Make("Chart2Visible", Chart2Visible);
        Make("Chart3Visible", Chart3Visible);
        Make("ViewportLabel", ViewportLabel);

        // The window counters are written by the chart from its own paint/layout
        // path, so the labels have to follow them.
        counterSync_ = std::make_unique<CounterSync>(this);
        DrawnPoints.Subscribe(counterSync_.get());
        SourcePoints.Subscribe(counterSync_.get());

        // Colour sliders, the two visibility switches and the range sliders all
        // feed the same reconciliation step. Subscribing to the sources (rather
        // than putting logic in each WAML command) means a keyboard edit, a drag
        // and a preset button cannot disagree about what the chart shows.
        controlSync_ = std::make_unique<ControlSync>(this);
        for (wui::IPropertySource* p : ControlSources()) p->Subscribe(controlSync_.get());

        // Stream first: the page measures nothing from an empty source.
        ResetStream();
        BuildComparison();
        GenerateCandles();
        BuildCandles(RangeStart.Get(), RangeEnd.Get());
        SyncControls();
        SetStreaming(true);
    }

    struct CounterSync final : wui::IPropertyObserver {
        explicit CounterSync(ChartsVm* vm) : vm_(vm) {}
        void OnSourceChanged(const wui::Value&) override { vm_->UpdateLabels(); }
        ChartsVm* vm_;
    };

    struct ControlSync final : wui::IPropertyObserver {
        explicit ControlSync(ChartsVm* vm) : vm_(vm) {}
        void OnSourceChanged(const wui::Value&) override { vm_->SyncControls(); }
        ChartsVm* vm_;
    };

    ~ChartsVm() override {
        if (counterSync_) {
            DrawnPoints.Unsubscribe(counterSync_.get());
            SourcePoints.Unsubscribe(counterSync_.get());
        }
        if (controlSync_) {
            for (wui::IPropertySource* p : ControlSources()) p->Unsubscribe(controlSync_.get());
        }
    }

    // --- the live signal ---------------------------------------------------
    // A slow carrier plus noise plus a periodically repeating spike. The spike
    // matters: uniform sampling drops it, MinMax and LTTB keep it.
    double SampleAt(size_t index) const {
        const double x = static_cast<double>(index) * 0.0002;
        double y = std::sin(x * 0.10) * 60.0 + std::sin(x * 0.031) * 25.0;
        y += Noise(index) * 1.2;
        if (index % 120000 == 60000) y += 150.0;
        if (index % 190000 == 30000) y -= 120.0;
        return y;
    }

    static double Noise(size_t index) {
        uint32_t state = static_cast<uint32_t>(index) * 2654435761u + 0x9E3779B9u;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state % 2001) / 1000.0 - 1.0;
    }

    void ResetStream() {
        auto stream = std::make_shared<wui::ChartStream>(kWindowPoints);
        std::vector<wui::ChartPoint> seed;
        seed.reserve(kWindowPoints);
        for (size_t i = 0; i < kWindowPoints; ++i) {
            seed.push_back(wui::ChartPoint{static_cast<double>(i), SampleAt(i)});
        }
        stream->PushPoints(std::move(seed));
        Stream.Set(std::static_pointer_cast<wui::RefObject>(stream));
        UpdateLabels();
    }

    // Called by the host once per frame. Returning false means "nothing changed,
    // do not repaint" -- which is exactly what a scrolled-away chart needs.
    bool Tick(float seconds) {
        if (!streaming_ || !Chart1Visible.Get()) return false;
        auto stream = std::dynamic_pointer_cast<wui::ChartStream>(Stream.Get());
        if (!stream) return false;

        samplesCarry_ += static_cast<double>(kSamplesPerSecond) * std::clamp(seconds, 0.0f, 0.1f);
        size_t add = static_cast<size_t>(samplesCarry_);
        if (add == 0) return false;
        samplesCarry_ -= static_cast<double>(add);
        add = std::min<size_t>(add, kMaxPerFrame);
        for (size_t i = 0; i < add; ++i) {
            const size_t index = nextIndex_++;
            stream->Push(SampleAt(index));
        }
        UpdateLabels();
        return true;
    }

    void SetStreaming(bool on) {
        streaming_ = on;
        Streaming.Set(on);
        ToggleStreamText.Set(on ? L"Pause stream" : L"Resume stream");
        StreamStateLabel.Set(on ? L"live — new samples push the window left"
                                : L"paused — the window is frozen");
        SamplesPerSecondNote();
    }

    void SetDownsample(bool on) {
        DownsampleModeProp.Set(on ? kLttbIndex : kNoneIndex);
        UpdateLabels();
    }

    // --- reconciliation of the interactive controls ------------------------
    void SyncControls() {
        if (syncing_) return;
        syncing_ = true;

        BarColor.Set(Rgb(BarR.Get(), BarG.Get(), BarB.Get()));
        LineColor.Set(Rgb(LineR.Get(), LineG.Get(), LineB.Get()));

        int first = std::clamp(RangeStart.Get(), 0, kCandleCount - 1);
        int last = std::clamp(RangeEnd.Get(), 0, kCandleCount - 1);
        if (first > last) {
            std::swap(first, last);
            RangeStart.Set(first);
            RangeEnd.Set(last);
        }
        if (first != rangeFirst_ || last != rangeLast_) {
            rangeFirst_ = first;
            rangeLast_ = last;
            BuildCandles(first, last);
        }
        RangeLabel.Set(DateLabel(first) + L"  —  " + DateLabel(last) + L"  ·  " +
                       std::to_wstring(last - first + 1) + L" days");

        ComparisonLabel.Set(
            L"50 bars and 50 line points on one x axis · bars " +
            std::wstring(ShowBars.Get() ? L"shown" : L"hidden") + L" · line " +
            std::wstring(ShowLine.Get() ? L"shown" : L"hidden") +
            L" · drag the six sliders to change each series colour");

        std::wstring active;
        if (Chart1Visible.Get()) active += L"1 (live), ";
        if (Chart2Visible.Get()) active += L"2 (bar + line), ";
        if (Chart3Visible.Get()) active += L"3 (K-line), ";
        if (!active.empty()) active.erase(active.size() - 2);
        ViewportLabel.Set(L"painting: " + (active.empty() ? std::wstring(L"none") : active) +
                          L" · every other figure is paused while offscreen");

        syncing_ = false;
    }

    static wui::Color Rgb(int r, int g, int b) {
        auto unit = [](int v) { return std::clamp(v, 0, 255) / 255.0f; };
        return wui::Color{unit(r), unit(g), unit(b), 1.0f, wui::Token::None};
    }

    // --- labels ------------------------------------------------------------
    void UpdateLabels() {
        const bool on = DownsampleEnabled();
        ModeLabel.Set(on ? L"LTTB downsampling — 1 point per horizontal pixel"
                         : L"Downsampling OFF — every point is drawn");
        const int drawn = DrawnPoints.Get();
        const int source = SourcePoints.Get() > 0 ? SourcePoints.Get() : kWindowPoints;
        if (drawn <= 0) {
            Timing.Set(L"awaiting the first layout — the counters come from the chart itself");
        } else {
            const double ratio = source > 0 ? static_cast<double>(source) / drawn : 0.0;
            Timing.Set(L"drawn " + GroupDigits(drawn) + L" of " + GroupDigits(source) +
                       L" points (" + ToWide(ratio, 0) + L"× reduction)");
        }
        FrameBudgetNote.Set(
            on ? L"With the reduction the plot does one DrawLine per output point, so the cost "
                 L"follows the viewport width — and the window can keep moving while it draws."
               : L"Without it the plot does one DrawLine per SOURCE point: " +
                     GroupDigits(static_cast<long long>(kWindowPoints)) +
                     L" line segments per frame. This is the honest slow case, not a simulated one.");
        PointsLabel.Set(L"window: " + GroupDigits(static_cast<long long>(kWindowPoints)) +
                        L" points · " +
                        ToWide(static_cast<double>(kWindowPoints) * sizeof(double) /
                                   (1024.0 * 1024.0),
                               1) +
                        L" MB · capacity is fixed, oldest samples are overwritten");
    }

    void SamplesPerSecondNote() {
        StreamLabel.Set(GroupDigits(static_cast<long long>(kSamplesPerSecond)) +
                        L" samples/s · window holds the most recent " +
                        GroupDigits(static_cast<long long>(kWindowPoints)) +
                        L" · a full window scrolls past in " +
                        ToWide(static_cast<double>(kWindowPoints) /
                                   static_cast<double>(kSamplesPerSecond),
                               1) +
                        L" s");
    }

    // --- the 50 + 50 figure ------------------------------------------------
    void BuildComparison() {
        std::vector<wui::ChartPoint> bars;
        std::vector<wui::ChartPoint> line;
        bars.reserve(kBarCount);
        line.reserve(kBarCount);
        double trend = 0;
        for (int i = 0; i < kBarCount; ++i) {
            const double value = 42.0 + 12.0 * std::sin(i * 0.31) +
                                 6.0 * std::sin(i * 0.87 + 1.1) + Noise(1000 + i) * 2.5;
            bars.push_back(wui::ChartPoint{static_cast<double>(i), value});
            trend = i == 0 ? value : trend * 0.68 + value * 0.32;
            line.push_back(wui::ChartPoint{static_cast<double>(i),
                                           trend + 4.0 * std::cos(i * 0.17)});
        }
        auto barSeries = std::make_shared<wui::ChartSeries>();
        barSeries->SetPoints(std::move(bars));
        BarSeries.Set(std::static_pointer_cast<wui::RefObject>(barSeries));
        auto lineSeries = std::make_shared<wui::ChartSeries>();
        lineSeries->SetPoints(std::move(line));
        LineSeries.Set(std::static_pointer_cast<wui::RefObject>(lineSeries));
    }

    // --- the K-line figure -------------------------------------------------
    struct CandleRecord {
        wui::Candle candle;
        std::wstring date;
    };

    void GenerateCandles() {
        allCandles_.clear();
        allCandles_.reserve(kCandleCount);
        double price = 100.0;
        for (int i = 0; i < kCandleCount; ++i) {
            const double open = price;
            const double drift = 0.42 * std::sin(i * 0.11) + Noise(5000 + i) * 2.1;
            const double close = std::max(5.0, open + drift);
            const double high = std::max(open, close) + 0.6 + std::abs(Noise(7000 + i)) * 1.8;
            const double low = std::min(open, close) - 0.6 - std::abs(Noise(9000 + i)) * 1.8;
            allCandles_.push_back(CandleRecord{
                wui::Candle{static_cast<double>(i), open, high, low, close}, DateLabel(i)});
            price = close;
        }
    }

    void BuildCandles(int first, int last) {
        std::vector<wui::Candle> slice;
        slice.reserve(static_cast<size_t>(last - first + 1));
        for (int i = first; i <= last && i < static_cast<int>(allCandles_.size()); ++i) {
            slice.push_back(allCandles_[static_cast<size_t>(i)].candle);
        }
        auto series = std::make_shared<wui::CandleSeries>();
        series->SetCandles(std::move(slice));
        CandleSeries.Set(std::static_pointer_cast<wui::RefObject>(series));
    }

    static std::wstring DateLabel(int dayIndex) {
        const std::chrono::sys_days base{std::chrono::year{2026} / 1 / 1};
        const auto date = base + std::chrono::days{dayIndex};
        const std::chrono::year_month_day ymd{date};
        wchar_t buf[32];
        std::swprintf(buf, std::size(buf), L"%04u-%02u-%02u",
                      static_cast<unsigned>(static_cast<int>(ymd.year())),
                      static_cast<unsigned>(ymd.month()),
                      static_cast<unsigned>(ymd.day()));
        return buf;
    }

    // --- constants ---------------------------------------------------------
    static constexpr size_t kWindowPoints = 1000000;
    static constexpr size_t kSamplesPerSecond = 250000;
    static constexpr size_t kMaxPerFrame = 20000;
    static constexpr int kBarCount = 50;
    static constexpr int kCandleCount = 120;
    static constexpr int kNoneIndex = 0;
    static constexpr int kLttbIndex = 3;

    static std::wstring GroupDigits(long long value) {
        const bool negative = value < 0;
        std::wstring digits = ToWide(static_cast<double>(negative ? -value : value), 0);
        std::wstring out;
        out.reserve(digits.size() + digits.size() / 3 + (negative ? 1 : 0));
        if (negative) out.push_back(L'-');
        const size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i == lead || (i > lead && (i - lead) % 3 == 0)) out.push_back(L',');
            out.push_back(digits[i]);
        }
        return out;
    }

    static std::wstring ToWide(double value, int decimals) {
        wchar_t buf[64];
        if (decimals > 0) std::swprintf(buf, std::size(buf), L"%.*f", decimals, value);
        else std::swprintf(buf, std::size(buf), L"%.0f", value);
        return buf;
    }

    // --- properties --------------------------------------------------------
    wui::ObservableProperty<wui::RefPtr> Stream{"Stream"};
    wui::ObservableProperty<wui::RefPtr> ToggleStream{"ToggleStreamCommand"};
    wui::ObservableProperty<wui::RefPtr> DownsampleOn{"DownsampleOnCommand"};
    wui::ObservableProperty<wui::RefPtr> DownsampleOff{"DownsampleOffCommand"};
    wui::ObservableProperty<wui::RefPtr> Reset{"ResetCommand"};

    wui::ObservableProperty<int> DownsampleModeProp{"DownsampleMode", kLttbIndex};
    wui::ObservableProperty<bool> Streaming{"Streaming", true};
    wui::ObservableProperty<bool> ShowFpsProp{"ShowFps", true};
    wui::ObservableProperty<int> DrawnPoints{"DrawnPoints", 0};
    wui::ObservableProperty<int> SourcePoints{"SourcePoints", 0};
    wui::ObservableProperty<std::wstring> Timing{"Timing"};
    wui::ObservableProperty<std::wstring> ModeLabel{"ModeLabel"};
    wui::ObservableProperty<std::wstring> StreamLabel{"StreamLabel"};
    wui::ObservableProperty<std::wstring> StreamStateLabel{"StreamStateLabel"};
    wui::ObservableProperty<std::wstring> PointsLabel{"PointsLabel"};
    wui::ObservableProperty<std::wstring> FrameBudgetNote{"FrameBudgetNote"};
    wui::ObservableProperty<std::wstring> ToggleStreamText{"ToggleStreamText"};

    wui::ObservableProperty<wui::RefPtr> BarSeries{"BarSeries"};
    wui::ObservableProperty<wui::RefPtr> LineSeries{"LineSeries"};
    wui::ObservableProperty<wui::Color> BarColor{"BarColor", Rgb(79, 139, 214)};
    wui::ObservableProperty<wui::Color> LineColor{"LineColor", Rgb(232, 102, 61)};
    wui::ObservableProperty<int> BarR{"BarR", 79};
    wui::ObservableProperty<int> BarG{"BarG", 139};
    wui::ObservableProperty<int> BarB{"BarB", 214};
    wui::ObservableProperty<int> LineR{"LineR", 232};
    wui::ObservableProperty<int> LineG{"LineG", 102};
    wui::ObservableProperty<int> LineB{"LineB", 61};
    wui::ObservableProperty<bool> ShowBars{"ShowBars", true};
    wui::ObservableProperty<bool> ShowLine{"ShowLine", true};
    wui::ObservableProperty<std::wstring> ComparisonLabel{"ComparisonLabel"};

    wui::ObservableProperty<wui::RefPtr> CandleSeries{"CandleSeries"};
    wui::ObservableProperty<wui::RefPtr> RangeAll{"RangeAllCommand"};
    wui::ObservableProperty<wui::RefPtr> Range30{"Range30Command"};
    wui::ObservableProperty<wui::RefPtr> Range7{"Range7Command"};
    wui::ObservableProperty<wui::Color> CandleUpColor{"CandleUpColor", Rgb(36, 158, 92)};
    wui::ObservableProperty<wui::Color> CandleDownColor{"CandleDownColor", Rgb(209, 71, 61)};
    wui::ObservableProperty<int> RangeStart{"RangeStart", 0};
    wui::ObservableProperty<int> RangeEnd{"RangeEnd", kCandleCount - 1};
    wui::ObservableProperty<std::wstring> RangeLabel{"RangeLabel"};
    wui::ObservableProperty<std::wstring> CandleNote{
        "CandleNote", L"Green closes above the open, red below · the thin line is the "
                      L"high–low range of that day"};

    wui::ObservableProperty<bool> Chart1Visible{"Chart1Visible", true};
    wui::ObservableProperty<bool> Chart2Visible{"Chart2Visible", true};
    wui::ObservableProperty<bool> Chart3Visible{"Chart3Visible", true};
    wui::ObservableProperty<std::wstring> ViewportLabel{"ViewportLabel"};

private:
    bool DownsampleEnabled() const { return DownsampleModeProp.Get() == kLttbIndex; }

    std::vector<wui::IPropertySource*> ControlSources() {
        return {&BarR, &BarG, &BarB, &LineR, &LineG, &LineB, &ShowBars, &ShowLine,
                &RangeStart, &RangeEnd, &Chart1Visible, &Chart2Visible, &Chart3Visible};
    }

    std::vector<CandleRecord> allCandles_;
    std::unique_ptr<CounterSync> counterSync_;
    std::unique_ptr<ControlSync> controlSync_;
    bool streaming_ = true;
    bool syncing_ = false;
    int rangeFirst_ = -1;
    int rangeLast_ = -1;
    double samplesCarry_ = 0;
    size_t nextIndex_ = kWindowPoints;
};

} // namespace sample
