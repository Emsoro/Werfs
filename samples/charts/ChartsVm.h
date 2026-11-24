// SPDX-License-Identifier: MIT
// Sample: the charts page's ViewModel -- a LIVE signal.
//
// SAMPLE code. Exists only to give samples/charts/Charts.waml something to bind to.
//
// The page demonstrates one thing: a chart of a million points is only impressive
// if the data is still moving. So the series is not a fixed array -- it is a
// ring buffer that receives new samples every frame and drops the oldest, holding
// a constant one million points. The scene scrolls left because the x values are
// absolute sample indices: sample N keeps value N forever, so the window slides
// across the signal instead of every point renumbering itself from zero (which
// would look frozen no matter how many samples arrived).
#pragma once

#include "Chart.h"
#include "ChartData.h"
#include "Mvvm.h"
#include "SoftwareRender.h"

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

// One bar of the category chart. A separate, small type: the bar chart shows
// discrete categories, which is a different shape of data from the dense live
// signal, and pretending they are the same would make one of the two dishonest.
class CategoryBar final : public wui::ObservableObject {
public:
    CategoryBar(std::wstring label, double value) {
        Label.Set(std::move(label));
        Value.Set(value);
        Make("Label", Label);
        Make("Value", Value);
    }
    wui::ObservableProperty<std::wstring> Label{"Label"};
    wui::ObservableProperty<double> Value{"Value"};
};

class ChartsVm final : public wui::ObservableObject {
public:
    ChartsVm() {
        ToggleStream.Set(std::make_shared<wui::RelayCommand>([this] { SetStreaming(!streaming_); }));
        DownsampleOn.Set(std::make_shared<wui::RelayCommand>([this] { SetDownsample(true); }));
        DownsampleOff.Set(std::make_shared<wui::RelayCommand>([this] { SetDownsample(false); }));
        Reset.Set(std::make_shared<wui::RelayCommand>([this] { ResetStream(); }));

        Make("Stream", Stream);
        Make("StaticSeries", StaticSeries);
        Make("CategorySeries", CategorySeries);
        Make("ToggleStreamCommand", ToggleStream);
        Make("DownsampleOnCommand", DownsampleOn);
        Make("DownsampleOffCommand", DownsampleOff);
        Make("ResetCommand", Reset);
        Make("DownsampleMode", DownsampleModeProp);
        Make("Streaming", Streaming);
        Make("ShowFps", ShowFpsProp);
        Make("PointCount", PointCount);
        Make("RealPoints", RealPoints);
        Make("DrawnPoints", DrawnPoints);
        Make("SourcePoints", SourcePoints);
        Make("Timing", Timing);
        Make("ModeLabel", ModeLabel);
        Make("StreamLabel", StreamLabel);
        Make("StreamStateLabel", StreamStateLabel);
        Make("PointsLabel", PointsLabel);
        Make("FrameBudgetNote", FrameBudgetNote);
        Make("FrameRateLabel", FrameRateLabel);
        Make("FrameRateDetail", FrameRateDetail);
        Make("CategoryLabel", CategoryLabel);
        Make("ToggleStreamText", ToggleStreamText);

        // Stream FIRST, then the bars: the bar chart is a measurement OF the stream,
        // and it silently measured an empty source (all zeros) when it ran before the
        // window was seeded.
        ResetStream();
        BuildBars();
        Streaming.Set(true);
        SetStreaming(true);

        // The window counters are written BY the chart from its own paint/layout
        // path, so the labels have to follow them. Without this the page showed
        // "drawn 0 of 1000000" directly under a heading claiming the reduction
        // works -- a stale label beside live data reads as evidence.
        counterSync_ = std::make_unique<CounterSync>(this);
        DrawnPoints.Subscribe(counterSync_.get());
        SourcePoints.Subscribe(counterSync_.get());
    }

    struct CounterSync final : wui::IPropertyObserver {
        explicit CounterSync(ChartsVm* vm) : vm_(vm) {}
        void OnSourceChanged(const wui::Value&) override { vm_->UpdateLabels(); }
        ChartsVm* vm_;
    };

    ~ChartsVm() override {
        if (counterSync_) {
            DrawnPoints.Unsubscribe(counterSync_.get());
            SourcePoints.Unsubscribe(counterSync_.get());
        }
    }

    // --- the live signal ---------------------------------------------------
    //
    // The shape is a slow carrier plus noise plus a periodically repeating spike.
    // The spike matters: uniform sampling drops it, MinMax and LTTB keep it, and
    // that difference is the visible argument for anything beyond "take every k-th
    // point".
    double SampleAt(size_t index) const {
        // x spans 0..200 across the window (index * 0.0002 for a million samples).
        //
        // The carrier frequency is chosen so only a handful of cycles fit in the
        // window: at 6 rad/unit the same span held ~190 cycles, which at one pixel per
        // column is indistinguishable from a solid band -- the waveform was there and
        // invisible. 0.10 rad/unit puts about three cycles across the plot, so the
        // shape is unmistakable and a reader can actually see it slide left.
        const double x = static_cast<double>(index) * 0.0002;
        double y = std::sin(x * 0.10) * 60.0 + std::sin(x * 0.031) * 25.0;
        // Noise is what makes downsampling non-trivial, but at amplitude 3 it buried the
        // carrier completely. Small enough that the wave stays legible between spikes.
        y += Noise(index) * 1.2;
        // Spikes every so often, so one is always inside the window and a reader can
        // watch it travel across the plot and leave.
        if (index % 120000 == 60000) y += 150.0;
        if (index % 190000 == 30000) y -= 120.0;
        return y;
    }

    // Deterministic hash, not rand(): the same index must always produce the same
    // value, or the "signal" would flicker independently of the scrolling and the
    // snapshot gate could never reproduce a frame.
    static double Noise(size_t index) {
        uint32_t state = static_cast<uint32_t>(index) * 2654435761u + 0x9E3779B9u;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state % 2001) / 1000.0 - 1.0;
    }

    // Fills the window so the first frame is a full chart rather than a chart
    // growing from the left edge.
    void ResetStream() {
        auto stream = std::make_shared<wui::ChartStream>(kWindowPoints);
        // Seed a full window instantly. Pushing one sample at a time would take
        // 1 000 000 frames to fill.
        std::vector<wui::ChartPoint> seed;
        seed.reserve(kWindowPoints);
        for (size_t i = 0; i < kWindowPoints; ++i) {
            seed.push_back(wui::ChartPoint{static_cast<double>(i), SampleAt(i)});
        }
        stream->PushPoints(std::move(seed));
        Stream.Set(std::static_pointer_cast<wui::RefObject>(stream));
        PointCount.Set(static_cast<int>(kWindowPoints));
        UpdateLabels();
    }

    // Called by the host once per frame with the real elapsed time.
    //
    // The number of samples added follows `seconds`, so the signal advances at a
    // fixed rate in SIGNAL time regardless of the frame rate. Appending a fixed
    // count per frame instead would make the scroll speed depend on how fast the
    // machine is, and the fps readout would then be measuring its own workload.
    bool Tick(float seconds) {
        if (!streaming_) return false;
        auto stream = std::dynamic_pointer_cast<wui::ChartStream>(Stream.Get());
        if (!stream) return false;

        // ~250 000 samples per second of wall time: a full window scrolls past in
        // four seconds, which is fast enough to see and slow enough to read.
        samplesCarry_ += static_cast<double>(kSamplesPerSecond) * std::clamp(seconds, 0.0f, 0.1f);
        size_t add = static_cast<size_t>(samplesCarry_);
        if (add == 0) return false;
        samplesCarry_ -= static_cast<double>(add);
        // Cap a single frame's work: after a stall (window drag, breakpoint) catching
        // up thousands of frames at once would freeze the UI for another stall.
        add = std::min<size_t>(add, kMaxPerFrame);
        for (size_t i = 0; i < add; ++i) {
            const size_t index = nextIndex_++;
            stream->Push(SampleAt(index));
        }
        UpdateLabels();
        return true;
    }

    // Drives the plot's fps overlay from the same clock.
    void TickChartFrame(float seconds) {
        if (chart_) chart_->Plot()->TickFrame(seconds);
    }

    void AttachChart(wui::Chart* chart) { chart_ = chart; }

    // --- toggles -----------------------------------------------------------
    void SetStreaming(bool on) {
        streaming_ = on;
        Streaming.Set(on);
        ToggleStreamText.Set(on ? L"Pause stream" : L"Resume stream");
        StreamStateLabel.Set(on ? L"live \u2014 new samples push the window left"
                                : L"paused \u2014 the window is frozen");
        SamplesPerSecondNote();
    }

    void SetDownsample(bool on) {
        DownsampleModeProp.Set(on ? kLttbIndex : kNoneIndex);
        UpdateLabels();
    }
    bool DownsampleEnabled() const { return DownsampleModeProp.Get() == kLttbIndex; }

    static double NowUs() {
        return std::chrono::duration<double, std::micro>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // --- labels ------------------------------------------------------------
    void UpdateLabels() {
        const bool on = DownsampleEnabled();
        ModeLabel.Set(on ? L"LTTB downsampling \u2014 1 point per horizontal pixel"
                         : L"Downsampling OFF \u2014 every point is drawn");
        const int drawn = DrawnPoints.Get();
        const int source = SourcePoints.Get() > 0 ? SourcePoints.Get() : PointCount.Get();
        if (drawn <= 0) {
            Timing.Set(L"awaiting the first layout \u2014 the counters are resolved from the "
                       L"budget and the data, so they appear once the chart has been laid out");
        } else {
            const double ratio = source > 0 ? static_cast<double>(source) / drawn : 0.0;
            Timing.Set(L"drawn " + GroupDigits(drawn) + L" of " + GroupDigits(source) +
                       L" points (" + ToWide(ratio, 0) + L"\u00d7 reduction)");
        }
        FrameBudgetNote.Set(
            on ? L"With the reduction the plot does one DrawLine per output point, so the cost "
                 L"follows the viewport width \u2014 and the window can keep moving while it draws."
               : L"Without it the plot does one DrawLine per SOURCE point: " +
                     GroupDigits(static_cast<long long>(kWindowPoints)) +
                     L" line segments per frame. This is the honest slow case, not a simulated one.");
        PointsLabel.Set(L"window: " + GroupDigits(static_cast<long long>(kWindowPoints)) +
                        L" points \u00b7 " +
                        ToWide(static_cast<double>(kWindowPoints) * sizeof(double) /
                                   (1024.0 * 1024.0),
                               1) +
                        L" MB \u00b7 capacity is fixed, oldest samples are overwritten");
    }

    void SamplesPerSecondNote() {
        StreamLabel.Set(GroupDigits(static_cast<long long>(kSamplesPerSecond)) +
                        L" samples/s \u00b7 window holds the most recent " +
                        GroupDigits(static_cast<long long>(kWindowPoints)) +
                        L" \u00b7 a full window scrolls past in " +
                        ToWide(static_cast<double>(kWindowPoints) /
                                   static_cast<double>(kSamplesPerSecond),
                               1) +
                        L" s");
    }

    // --- bars --------------------------------------------------------------
    //
    // Same data, two different questions, so the chart asks the one bars can answer.
    // The naive version plotted points-drawn (1 000 000 vs 700), and on a linear axis
    // the three reducing modes collapsed onto the baseline and looked like zero --
    // which reads as "these modes draw nothing" rather than "these modes are cheap".
    // Plotting the ms per frame instead keeps all four readable and says what a reader
    // actually cares about.
    void BuildBars() {
        // Measured once at construction from the live window into a 700x300 software
        // raster, so the bars are not hand-typed numbers.
        std::array<double, 4> ms = MeasurePerMode();
        const wchar_t* labels[] = {L"None", L"Uniform", L"MinMax", L"LTTB"};
        categoryBars_ = std::make_shared<wui::ObservableList<CategoryBar>>();
        std::vector<wui::ChartPoint> pts;
        for (size_t i = 0; i < ms.size(); ++i) {
            categoryBars_->Add(std::make_shared<CategoryBar>(labels[i], ms[i]));
            pts.push_back(wui::ChartPoint{static_cast<double>(i), ms[i]});
        }
        auto category = std::make_shared<wui::ChartSeries>();
        category->SetPoints(std::move(pts));
        CategorySeries.Set(std::static_pointer_cast<wui::RefObject>(category));
        FrameRateLabel.Set(L"ms per frame, 700\u00d7300 software raster: None " +
                           ToWide(ms[0], 1) + L" ms \u00b7 Uniform " + ToWide(ms[1], 2) +
                           L" \u00b7 MinMax " + ToWide(ms[2], 2) + L" \u00b7 LTTB " +
                           ToWide(ms[3], 2) + L" ms");
        FrameRateDetail.Set(L"Time to lay out and paint one frame of the live " +
                            GroupDigits(static_cast<long long>(kWindowPoints)) +
                            L"-point window, median of 8 frames after 2 warm-up frames. "
                            L"Measured here, at startup, from the same plot code the page draws with.");
        CategoryLabel.Set(L"milliseconds per frame at a 700 px viewport \u00b7 lower is better "
                          L"\u00b7 all three reducing modes finish; None does not");
    }

    // Times one pass per mode on a dedicated probe plot so the visible chart is not
    // disturbed. Returns milliseconds, in DownsampleMode order.
    std::array<double, 4> MeasurePerMode() const {
        std::array<double, 4> out{};
        auto stream = std::dynamic_pointer_cast<wui::ChartStream>(Stream.Get());
        if (!stream) return out;
        auto probe = std::make_shared<wui::ChartPlot>();
        probe->SetStream(stream);
        probe->SetAutoFit(true);
        wui::SoftwareRasterizer raster(700, 300);
        const wui::ThemePalette palette = wui::MakePalette(wui::ThemeKind::Light);
        const wui::DownsampleMode modes[] = {wui::DownsampleMode::None, wui::DownsampleMode::Uniform,
                                             wui::DownsampleMode::MinMax, wui::DownsampleMode::Lttb};
        for (size_t m = 0; m < 4; ++m) {
            std::vector<double> samples;
            probe->SetDownsampleMode(modes[m]);
            for (int i = 0; i < 10; ++i) {
                const double t0 = NowUs();
                probe->ForceLayout(wui::Size{700.0f, 300.0f});
                wui::RasterPaintContext ctx(raster);
                probe->PaintTree(ctx, palette);
                samples.push_back(NowUs() - t0);
            }
            std::sort(samples.begin(), samples.end());
            double total = 0;
            // 2 warm-up, then take the MEDIAN-of-the-tail: the mean is dominated by
            // whichever frame lost the CPU.
            for (size_t i = 2; i < samples.size(); ++i) total += samples[i];
            out[m] = total / static_cast<double>(samples.size() - 2) / 1000.0;
        }
        return out;
    }

    // --- constants ---------------------------------------------------------
    // The window size the page advertises. Kept as a named constant so the text and
    // the actual buffer cannot drift apart.
    static constexpr size_t kWindowPoints = 1000000;
    static constexpr size_t kSamplesPerSecond = 250000;
    static constexpr size_t kMaxPerFrame = 20000;
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

    // Two format strings, not a ternary inside one: with decimals == 0 the "%.*f"
    // branch consumes two varargs and the "%.0f" branch one, so choosing between the
    // strings without choosing the matching argument list reads the double as the
    // format -- every label printed "0".
    static std::wstring ToWide(double value, int decimals) {
        wchar_t buf[64];
        if (decimals > 0) std::swprintf(buf, std::size(buf), L"%.*f", decimals, value);
        else std::swprintf(buf, std::size(buf), L"%.0f", value);
        return buf;
    }

    wui::ObservableProperty<wui::RefPtr> Stream{"Stream"};
    wui::ObservableProperty<wui::RefPtr> StaticSeries{"StaticSeries"};
    wui::ObservableProperty<wui::RefPtr> CategorySeries{"CategorySeries"};
    wui::ObservableProperty<wui::RefPtr> ToggleStream{"ToggleStreamCommand"};
    wui::ObservableProperty<wui::RefPtr> DownsampleOn{"DownsampleOnCommand"};
    wui::ObservableProperty<wui::RefPtr> DownsampleOff{"DownsampleOffCommand"};
    wui::ObservableProperty<wui::RefPtr> Reset{"ResetCommand"};

    // Mode indices match the Downsample enum order: None, Uniform, MinMax, Lttb.
    wui::ObservableProperty<int> DownsampleModeProp{"DownsampleMode", kLttbIndex};
    wui::ObservableProperty<bool> Streaming{"Streaming", true};
    wui::ObservableProperty<bool> ShowFpsProp{"ShowFps", true};
    wui::ObservableProperty<int> PointCount{"PointCount", 0};
    wui::ObservableProperty<int> RealPoints{"RealPoints", 0};
    // Written by the control through the TwoWay bound chart properties.
    wui::ObservableProperty<int> DrawnPoints{"DrawnPoints", 0};
    wui::ObservableProperty<int> SourcePoints{"SourcePoints", 0};
    wui::ObservableProperty<std::wstring> Timing{"Timing"};
    wui::ObservableProperty<std::wstring> ModeLabel{"ModeLabel"};
    wui::ObservableProperty<std::wstring> StreamLabel{"StreamLabel"};
    wui::ObservableProperty<std::wstring> StreamStateLabel{"StreamStateLabel"};
    wui::ObservableProperty<std::wstring> PointsLabel{"PointsLabel"};
    wui::ObservableProperty<std::wstring> FrameBudgetNote{"FrameBudgetNote"};
    wui::ObservableProperty<std::wstring> FrameRateLabel{"FrameRateLabel"};
    wui::ObservableProperty<std::wstring> FrameRateDetail{"FrameRateDetail"};
    wui::ObservableProperty<std::wstring> CategoryLabel{"CategoryLabel"};
    wui::ObservableProperty<std::wstring> ToggleStreamText{"ToggleStreamText"};

private:
    std::shared_ptr<wui::ObservableList<CategoryBar>> categoryBars_;
    std::unique_ptr<CounterSync> counterSync_;
    wui::Chart* chart_ = nullptr;    // not owned; the widget tree owns it
    bool streaming_ = true;
    double samplesCarry_ = 0;
    size_t nextIndex_ = kWindowPoints;
};
} // namespace sample
