// SPDX-License-Identifier: MIT
// Chart controls: Cartesian axes with a title, plus the plot area that draws into
// them. Modelled on InteractiveDataDisplay.WPF (Chart / PlotBase / LineGraph), but
// deliberately smaller: this framework has no attached-property syntax, no
// dependency-property system and no visual-tree template, so the composition is
// done by the chart control itself rather than by a control template.
//
// What was kept from the reference:
//  * the plot transform is (ScaleX, OffsetX, ScaleY, OffsetY) with y flipped, so
//    screen position comes from six plane arithmetic operations and nothing else;
//  * auto-fit computes the data bounds and fits them into the plot area minus
//    padding, with an aspect-ratio constraint;
//  * an axis generates "nice" ticks (1/2/5 x 10^n) rather than an arbitrary split,
//    because a grid labelled 0, 333.3, 666.7 is unreadable.
//
// What was added, because the reference does not do it:
//  * DOWNSAMPLING. The reference hands every point to a WPF Polyline and lets the
//    platform deal with it. Here the plot area reduces the series to at most one
//    point per horizontal pixel (or a user-chosen budget) before drawing, which is
//    what makes a 10-million-point series a 60 fps chart instead of a hang.
#pragma once

#include "ChartData.h"
#include "Widget.h"

#include <string>
#include <vector>

namespace wui {

// ---------------------------------------------------------------------------
// Axis ticks
//
// "Nice numbers" only: the step is always 1, 2 or 5 times a power of ten, which is
// what makes a reader able to interpolate between two grid lines. Choosing the
// step from the requested count keeps the tick count stable as the plot resizes
// instead of changing on every pixel.
// ---------------------------------------------------------------------------
struct AxisTicks {
    std::vector<double> values;
    double step = 1.0;
};

// Returns at most `maxCount` ticks covering [min, max], step on a nice number.
AxisTicks ComputeNiceTicks(double min, double max, int maxCount);

// Formats a tick label: no exponent for everyday magnitudes, and no trailing
// zeros, so "0.5" does not print as "0.500000".
std::wstring FormatTickLabel(double value, double step);

// ---------------------------------------------------------------------------
// ChartPlot: the drawing area.
//
// One control, three plot kinds. Three separate controls would each need their own
// axes, padding and transform code -- and then disagree about them, which is how a
// bar chart ends up with grid lines that do not line up with a line chart beside
// it in the same figure.
//
// A ChartPlot owns no data of its own: it is pointed at ChartSeries, which the
// ViewModel owns. The ViewModel is free to replace or mutate a series and then
// invalidate the plot.
// ---------------------------------------------------------------------------
enum class PlotKind {
    Line,
    Bar,
    Area,
};

class ChartPlot : public Widget {
public:
    ChartPlot();

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;

    // --- data -------------------------------------------------------------
    // Non-owning by intent: the widget draws what the ViewModel published, and a
    // series that is replaced mid-frame does not leave a dangling widget.
    void SetSeries(std::shared_ptr<ChartSeries> series);
    const std::shared_ptr<ChartSeries>& Series() const { return series_; }

    // A LIVE source. A stream is a separate type rather than a mode flag on the
    // series because the two have different shapes: a series is a fixed array that
    // gets replaced, a stream is a ring buffer whose contents the control must read
    // fresh every frame (and must NOT cache, or the chart freezes).
    void SetStream(std::shared_ptr<ChartStream> stream);
    const std::shared_ptr<ChartStream>& Stream() const { return stream_; }
    bool IsLive() const { return stream_ != nullptr; }

    // A second series drawn under/over the primary one. The bar chart uses it
    // for the 50-point line: one x range, two different visual encodings.
    void SetOverlaySeries(std::shared_ptr<ChartSeries> series);
    const std::shared_ptr<ChartSeries>& OverlaySeries() const { return overlaySeries_; }
    void SetCandles(std::shared_ptr<CandleSeries> candles);
    const std::shared_ptr<CandleSeries>& Candles() const { return candles_; }

    // --- series appearance ------------------------------------------------
    void SetBarColor(const Color& color) { barColor_ = color; Invalidate(AffectsRender); }
    void SetLineColor(const Color& color) { lineColor_ = color; Invalidate(AffectsRender); }
    void SetCandleUpColor(const Color& color) { candleUpColor_ = color; Invalidate(AffectsRender); }
    void SetCandleDownColor(const Color& color) { candleDownColor_ = color; Invalidate(AffectsRender); }
    void SetShowBars(bool show);
    void SetShowLine(bool show);
    bool ShowBars() const { return showBars_; }
    bool ShowLine() const { return showLine_; }

    // Published by the enclosing ScrollViewer-aware paint path. A chart that is
    // scrolled out of view neither paints nor (for the live series) advances.
    void SetViewportVisible(bool visible) { viewportVisible_ = visible; }
    bool ViewportVisible() const { return viewportVisible_; }

    // --- fit and transform ------------------------------------------------
    // Defaults so a plot can be created and measured with no data and no series.
    void SetAutoFit(bool enabled);
    bool AutoFit() const { return autoFit_; }    // Pins the visible data range. Only meaningful when auto-fit is off.
    void SetBounds(const ChartBounds& bounds);
    const ChartBounds& VisibleBounds() const { return visible_; }
    // Recomputes the visible range from the data (or from the pinned bounds) and
    // the plot area. Called from Arrange and whenever a slot changes.
    void Fit();

    // --- virtualisation ---------------------------------------------------
    // The downsampling budget. Zero (the default) means "one point per horizontal
    // pixel plus a little", computed from the plot width at draw time -- the
    // budget should follow the PIXELS, not a constant, or a wide window would draw
    // a coarser line than a narrow one.
    void SetMaxPoints(int maxPoints);
    int MaxPoints() const { return maxPoints_; }
    void SetDownsampleMode(DownsampleMode mode);
    DownsampleMode Mode() const { return mode_; }

    // --- what the last paint actually did (read by tests and by the sample) ---
    size_t LastDrawnPoints() const { return lastDrawnPoints_; }
    size_t LastSourcePoints() const { return lastSourcePoints_; }
    double LastFitUs() const { return lastFitUs_; }
    double LastDownsampleUs() const { return lastDownsampleUs_; }
    double LastPaintUs() const { return lastPaintUs_; }
    int LastBudget() const { return lastBudget_; }

    // --- live frame rate ---------------------------------------------------
    // Measured from the interval between consecutive paints, smoothed so the number
    // is readable instead of jittering every frame. The overlay is drawn by the
    // chart itself (top-right) because a page cannot know when a frame happened --
    // asking the ViewModel to compute fps would mean it needs a timer that has
    // nothing to do with rendering, and the number would then describe the timer.
    void SetShowFps(bool show);
    bool ShowFps() const { return showFps_; }
    double Fps() const { return fps_; }
    // Optional: lets a host feed its own clock. The overlay does NOT depend on it --
    // paints are observed directly (AccumulateFrame), because a feature that only
    // works when the host remembers to call something reads as implemented but does
    // nothing.
    void TickFrame(float seconds);

    // Grid line positions in data coordinates; paint and hit testing share them.
    const AxisTicks& XTicks() const { return xTicks_; }
    const AxisTicks& YTicks() const { return yTicks_; }

    // Data <-> screen. Exposed because the Chart draws the axis labels outside the
    // plot area and must use the same transform, not a second copy of it.
    double LeftFromX(double x) const { return plot_.x + (x - visible_.minX) * scaleX_; }
    double TopFromY(double y) const {
        // y is flipped: maxY is at the top of the plot area.
        return plot_.y + plot_.h - (y - visible_.minY) * scaleY_;
    }
    // The inner rectangle (bounds minus margin) the plot draws into.
    //
    // Computed from the CURRENT bounds_ rather than returned from the cached plot_.
    // The framework paints by temporarily shifting bounds_ and passing the shift
    // down (Widget::PaintTreeAt), because controls draw from their own bounds_ and
    // there is no transform stack. A cached absolute rect therefore stays at the
    // pre-scroll position while everything around it moves -- which is exactly what
    // "the chart is fixed in place and detaches from the rest of the page when you
    // scroll" looks like.
    Rect PlotArea() const { return PlotRectFrom(bounds_); }
    // What the transform was built from. Used by Fit(), where only the size matters.
    const Rect& LayoutPlotArea() const { return plot_; }

private:
    Rect PlotRectFrom(const Rect& outer) const;
    void ComputeTicks();
    // Adds one frame interval to the smoothed rate. Shared by TickFrame and by the
    // paint observer, so the two can never disagree about how the number is derived.
    void AccumulateFrame(double seconds);
    // Resolves LastDrawnPoints/LastBudget from the data and the plot width. Runs in
    // Fit(), so the numbers describe the frame that is ABOUT to be drawn rather than
    // the one that just finished.
    void PrepareDrawCounts();
    // Every draw function takes the frame's origin shift: the transform is built from
    // the cached layout rect, while what gets painted must sit at the CURRENT
    // (possibly scrolled) position.
    void DrawLinePlot(PaintCtx& ctx, const std::vector<ChartPoint>& pts, const Color& color,
                      double originX, double originY);
    void DrawBarPlot(PaintCtx& ctx, const std::vector<ChartPoint>& pts, const Color& color,
                     double originX, double originY);
    void DrawAreaPlot(PaintCtx& ctx, const std::vector<ChartPoint>& pts, const Color& color,
                      double originX, double originY);
    void DrawCandlePlot(PaintCtx& ctx, const ThemePalette& palette, double originX, double originY);

    std::shared_ptr<ChartSeries> series_;
    std::shared_ptr<ChartStream> stream_;
    std::shared_ptr<ChartSeries> overlaySeries_;
    std::shared_ptr<CandleSeries> candles_;
    Color barColor_{0, 0, 0, 1, Token::Accent};
    Color lineColor_{0, 0, 0, 1, Token::Accent};
    Color candleUpColor_{0.11f, 0.62f, 0.36f, 1.0f, Token::None};
    Color candleDownColor_{0.82f, 0.28f, 0.24f, 1.0f, Token::None};
    bool showBars_ = true;
    bool showLine_ = true;
    bool viewportVisible_ = true;
    ChartBounds visible_;
    ChartBounds pinned_;
    bool autoFit_ = true;
    bool hasPinned_ = false;
    int maxPoints_ = 0;
    DownsampleMode mode_ = DownsampleMode::Lttb;

    Rect plot_;
    double scaleX_ = 1;
    double scaleY_ = 1;
    AxisTicks xTicks_;
    AxisTicks yTicks_;

    size_t lastDrawnPoints_ = 0;
    size_t lastSourcePoints_ = 0;
    int lastBudget_ = 0;
    double lastFitUs_ = 0;
    double lastDownsampleUs_ = 0;
    double lastPaintUs_ = 0;

    bool showFps_ = false;
    // Timestamp of the previous paint, in microseconds. Used to measure the interval
    // directly so the overlay does not depend on a host calling TickFrame.
    double lastPaintStampUs_ = 0;
    // Exponentially smoothed frames per second. Smoothing is what makes the number
    // legible: raw per-frame values swing between 40 and 400 on the same series.
    double fps_ = 0;
    double fpsAccumSeconds_ = 0;
    int fpsAccumFrames_ = 0;
};

// ---------------------------------------------------------------------------
// Chart: a titled figure with axes, grid and an optional plot area.
//
// A Chart owns exactly one ChartPlot as its child. The plot is exposed so WAML can
// not have to write it, and so the sample can read the timings back out. Axes and
// labels are drawn by the Chart, in the padding around the plot, using the plot's
// transform -- the plot is the single source of truth for "where is data point N
// on screen".
// ---------------------------------------------------------------------------
class Chart : public Widget {
public:
    Chart();

    Size Measure(const Size& available) override;
    void Arrange(const Rect& finalRect) override;
    void OnPaint(PaintCtx& ctx, const ThemePalette& palette) override;
    void OnSlotChanged(int slot, const Value& value) override;

protected:
    // Publishes the plot's draw counters once the plot has painted. Overridden
    // because the counters are only meaningful after the child's OnPaint, and
    // PaintChildrenAt is the first hook that runs after it.
    void PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) override;

public:

    ChartPlot* Plot() const { return plot_.get(); }

    // Slot indices, resolved once in the constructor. Public because the plot area
    // reads the chart's axis colour and the tests drive slots directly.
    int SlotSeries() const { return slotSeries_; }
    int SlotStream() const { return slotStream_; }
    int SlotShowFps() const { return slotShowFps_; }
    int SlotAutoFit() const { return slotAutoFit_; }
    int SlotDownsample() const { return slotDownsample_; }
    int SlotMaxPoints() const { return slotMaxPoints_; }
    int SlotPlotKind() const { return slotPlotKind_; }
    int SlotViewportVisible() const { return slotViewportVisible_; }
    // Publishes the plot's counters and current fps back to the bound ViewModel.
    // Called from Arrange; also callable after a manual TickFrame.
    void RefreshPublishedCounters() { PublishCounts(); }

private:
    void PublishCounts();
    // Axis labels, tick marks and the row of x labels. Called from PaintChildrenAt
    // (after the plot has painted) so the offset is the frame's real scroll delta.
    void DrawAxisLabels(PaintCtx& ctx, const ThemePalette& palette, const Point& shift);
    // The plot area is the child. Keeping it in the child list (rather than
    // painting it from Chart::OnPaint) is what lets the framework's clip, dirty
    // propagation and hit testing apply to it without the chart re-implementing
    // any of them -- and it is why the plot gets painted after this widget's own
    // OnPaint, so the axis labels underneath cannot be overdrawn by the plot.
    std::shared_ptr<ChartPlot> plot_;

    double paddingLeft_ = 64;
    double paddingBottom_ = 30;
    double paddingRight_ = 14;
    double paddingTop_ = 12;

    int slotTitle_ = -1;
    int slotSeries_ = -1;
    int slotStream_ = -1;
    int slotShowFps_ = -1;
    int slotAutoFit_ = -1;
    int slotDownsample_ = -1;
    int slotMaxPoints_ = -1;
    int slotPlotKind_ = -1;
    int slotViewportVisible_ = -1;
};

} // namespace wui
