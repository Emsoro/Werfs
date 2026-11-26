// SPDX-License-Identifier: MIT
#include "Chart.h"
#include "Controls.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace wui {

namespace {

double NowUs() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::micro>(clock::now().time_since_epoch()).count();
}

// Font sizes chosen so the axis labels are legible without crowding: an axis that
// needs to shrink its labels to fit is showing too many of them, and ComputeNiceTicks
// is what decides that.
constexpr float kAxisFontSize = 11.0f;
constexpr float kLabelGap = 4.0f;

// A chart is "being seen" when its painted rect intersects the nearest
// enclosing ScrollViewer's viewport. Rects are compared in the shifted space
// used during paint: ScrollViewer passes shift - offset_ to its children while
// its own bounds_ stay in window space, so both sides are already comparable.
// With no scroller ancestor the widget is assumed visible (headless tests,
// dialogs, and standalone ChartPlots all take this path).
bool InViewport(const Widget* widget) {
    const Widget* scroller = nullptr;
    for (const Widget* p = widget->Parent(); p != nullptr; p = p->Parent()) {
        if (dynamic_cast<const ScrollViewer*>(p) != nullptr) {
            scroller = p;
            break;
        }
    }
    if (scroller == nullptr) return true;
    const Rect viewport = scroller->Bounds();
    const Rect r = widget->Bounds();
    if (viewport.w <= 0 || viewport.h <= 0 || r.w <= 0 || r.h <= 0) return false;
    return !(r.x + r.w <= viewport.x || r.x >= viewport.x + viewport.w ||
             r.y + r.h <= viewport.y || r.y >= viewport.y + viewport.h);
}

} // namespace

// ---------------------------------------------------------------------------
// Ticks
// ---------------------------------------------------------------------------
AxisTicks ComputeNiceTicks(double min, double max, int maxCount) {
    AxisTicks out;
    if (maxCount < 1) maxCount = 1;
    if (!(max > min)) {
        // A degenerate range still gets one tick: an axis with no labels reads as
        // "the chart failed", not as "the data has no range".
        out.values.push_back(min);
        return out;
    }
    const double span = max - min;
    // Start from a step that would give the requested count, then snap it UP to
    // the nearest 1/2/5 x 10^n. Snapping up can only reduce the count, never
    // increase it, so the axis never ends up denser than asked.
    const double rough = span / static_cast<double>(maxCount);
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double norm = rough / magnitude;
    double nice = 10.0;
    if (norm <= 1.0) nice = 1.0;
    else if (norm <= 2.0) nice = 2.0;
    else if (norm <= 5.0) nice = 5.0;
    out.step = nice * magnitude;
    if (!(out.step > 0)) out.step = span;

    const double first = std::ceil(min / out.step) * out.step;
    // Guard against an unbounded loop if step came out denormal: the count is
    // capped at maxCount + 2 by construction, so this is belt and braces.
    for (double v = first; v <= max + out.step * 1e-9 && out.values.size() < static_cast<size_t>(maxCount) + 2;
         v += out.step) {
        // Rewrite -0 as 0: "-0" as a label is a defect nobody reports but everybody
        // notices.
        out.values.push_back(std::abs(v) < out.step * 1e-9 ? 0.0 : v);
    }
    return out;
}

std::wstring FormatTickLabel(double value, double step) {
    // Decimals follow the STEP, not the value: 0.5 with a step of 0.1 needs one
    // decimal, and 1000 with a step of 100 needs none.
    int decimals = 0;
    if (step > 0 && step < 1) {
        decimals = static_cast<int>(std::ceil(-std::log10(step)));
        decimals = std::min(decimals, 6);
    }
    wchar_t buf[64];
    if (std::abs(value) >= 1e7 || (value != 0 && std::abs(value) < 1e-4)) {
        std::swprintf(buf, std::size(buf), L"%.1e", value);
    } else {
        std::swprintf(buf, std::size(buf), L"%.*f", decimals, value);
    }
    std::wstring s(buf);
    // Trim trailing zeros the format necessarily added ("1.500" -> "1.5").
    if (s.find(L'e') == std::wstring::npos && s.find(L'.') != std::wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    return s;
}

// ---------------------------------------------------------------------------
// ChartPlot
// ---------------------------------------------------------------------------
ChartPlot::ChartPlot() {
    BindType(TypeRegistry::Instance().Find("wui/ChartPlot"));
}

void ChartPlot::SetSeries(std::shared_ptr<ChartSeries> series) {
    series_ = std::move(series);
    stream_.reset();
    Fit();
    // Fit resolves the counts, but a reader can legitimately ask before the first
    // Arrange; make the published numbers correct from the moment the data arrives.
    PrepareDrawCounts();
    Invalidate(AffectsMeasure | AffectsRender);
}

void ChartPlot::SetStream(std::shared_ptr<ChartStream> stream) {
    stream_ = std::move(stream);
    series_.reset();
    Fit();
    PrepareDrawCounts();
    Invalidate(AffectsMeasure | AffectsRender);
}

void ChartPlot::SetOverlaySeries(std::shared_ptr<ChartSeries> series) {
    overlaySeries_ = std::move(series);
    Fit();
    Invalidate(AffectsMeasure | AffectsRender);
}

void ChartPlot::SetCandles(std::shared_ptr<CandleSeries> candles) {
    candles_ = std::move(candles);
    Fit();
    Invalidate(AffectsMeasure | AffectsRender);
}

void ChartPlot::SetShowBars(bool show) {
    if (showBars_ == show) return;
    showBars_ = show;
    Invalidate(AffectsRender);
}

void ChartPlot::SetShowLine(bool show) {
    if (showLine_ == show) return;
    showLine_ = show;
    Invalidate(AffectsRender);
}

// Point count of whichever source is active.
static size_t SourceCount(const std::shared_ptr<ChartSeries>& series,
                          const std::shared_ptr<ChartStream>& stream) {
    if (stream) return stream->Count();
    return series ? series->Count() : 0;
}

void ChartPlot::SetAutoFit(bool enabled) {
    if (autoFit_ == enabled) return;
    autoFit_ = enabled;
    Fit();
    Invalidate(AffectsRender);
}

void ChartPlot::SetBounds(const ChartBounds& bounds) {
    pinned_ = bounds;
    hasPinned_ = !bounds.empty;
    Fit();
    Invalidate(AffectsRender);
}

void ChartPlot::SetMaxPoints(int maxPoints) {
    const int clamped = std::max(0, maxPoints);
    if (maxPoints_ == clamped) return;
    maxPoints_ = clamped;
    // Recompute immediately: the draw count depends only on the budget and the
    // data, so leaving it stale until the next Arrange meant a page that flipped
    // the mode showed the PREVIOUS mode's numbers. That is exactly how the reported
    // count and the visible chart disagreed.
    PrepareDrawCounts();
    Invalidate(AffectsRender);
}

void ChartPlot::SetDownsampleMode(DownsampleMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    PrepareDrawCounts();
    Invalidate(AffectsRender);
}

void ChartPlot::SetShowFps(bool show) {
    if (showFps_ == show) return;
    showFps_ = show;
    Invalidate(AffectsRender);
}

void ChartPlot::TickFrame(float seconds) {
    // Kept for hosts that have a real frame clock, but no longer the only source:
    // ObservePaint() below measures the interval between paints, so the overlay works
    // even when nobody calls this. The old design depended on the sample wiring an
    // AttachChart() call that was never made, and the result was a chart that never
    // displayed a rate at all -- a feature that silently does nothing is worse than a
    // missing one, because the property is there and reads as implemented.
    if (seconds > 0) AccumulateFrame(seconds);
}

void ChartPlot::AccumulateFrame(double seconds) {
    if (!showFps_ || seconds <= 0) return;
    fpsAccumSeconds_ += seconds;
    ++fpsAccumFrames_;
    // Update about five times a second: faster is unreadable, slower hides stalls.
    if (fpsAccumSeconds_ >= 0.2) {
        const double measured = static_cast<double>(fpsAccumFrames_) / fpsAccumSeconds_;
        // Weight the new measurement lightly so one hiccup does not dominate.
        fps_ = fps_ <= 0 ? measured : fps_ * 0.6 + measured * 0.4;
        fpsAccumSeconds_ = 0;
        fpsAccumFrames_ = 0;
    }
}
void ChartPlot::Fit() {
    const double t0 = NowUs();
    // A live stream's bounds change on every push, so they are read fresh; a series
    // caches its own. Auto-fit is ON for both by default, and a live chart with a
    // pinned range would grow off-scale within seconds.
    ChartBounds dataBounds = stream_ ? stream_->Bounds()
                                     : (series_ ? series_->Bounds() : ChartBounds{});
    // The overlay and the candles share the x axis with the primary series, so
    // their whole range has to participate in the fit. Fitting only the bars
    // makes the 50-point line drift off the top of the figure.
    if (candles_) dataBounds.Surround(candles_->Bounds());
    if (overlaySeries_) dataBounds.Surround(overlaySeries_->Bounds());
    if (autoFit_ && (stream_ || series_ || candles_ || overlaySeries_)) {
        visible_ = dataBounds;
    } else if (hasPinned_) {
        visible_ = pinned_;
    } else if (stream_ || series_ || candles_ || overlaySeries_) {
        visible_ = dataBounds;
    }
    if (visible_.empty) {
        // No data: show 0..1 so the grid still has somewhere to sit, rather than
        // an axis from 0 to 0 which would divide by zero when projecting.
        visible_ = ChartBounds{0, 1, 0, 1, false};
    }
    // 2% breathing room so the top and bottom peaks are not drawn exactly on the
    // frame -- a peak lying on the border is indistinguishable from one clipped.
    visible_ = visible_.Inflated(0.02);

    const double w = std::max(1.0, static_cast<double>(plot_.w));
    const double h = std::max(1.0, static_cast<double>(plot_.h));
    scaleX_ = w / std::max(1e-12, visible_.Width());
    scaleY_ = h / std::max(1e-12, visible_.Height());
    ComputeTicks();
    // Resolve the draw counts HERE, not in OnPaint. How many points will be drawn
    // depends only on the budget and the data -- both known now -- so waiting for
    // the paint would make the count lag one frame. That lag is not cosmetic: the
    // page displays these numbers, so a first frame that reports "0 of 10000000"
    // reads as "downsampling is broken", and two snapshot passes could disagree
    // about the text depending on which painted first.
    PrepareDrawCounts();
    lastFitUs_ = NowUs() - t0;
}

void ChartPlot::PrepareDrawCounts() {
    lastSourcePoints_ = SourceCount(series_, stream_);
    lastDrawnPoints_ = 0;
    lastBudget_ = maxPoints_ > 0 ? maxPoints_ : static_cast<int>(plot_.w);
    if (lastSourcePoints_ == 0) return;
    const size_t budget = static_cast<size_t>(std::max(3, lastBudget_));
    lastDrawnPoints_ = stream_ ? stream_->Downsampled(budget, mode_)->size()
                               : series_->Downsampled(budget, mode_)->size();
}

void ChartPlot::ComputeTicks() {
    // The tick budget follows the pixels: ~90 px per label on x, ~34 px on y, so
    // labels never overlap at any window size.
    const int wantX = std::max(2, static_cast<int>(plot_.w / 90.0f));
    const int wantY = std::max(2, static_cast<int>(plot_.h / 34.0f));
    xTicks_ = ComputeNiceTicks(visible_.minX, visible_.maxX, wantX);
    yTicks_ = ComputeNiceTicks(visible_.minY, visible_.maxY, wantY);
}

Size ChartPlot::Measure(const Size& available) {
    // The plot fills whatever it is given; the Chart has already subtracted its
    // axis padding. A chart that sized itself to its data would jump around as the
    // series changed.
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Thickness pad = Get<Thickness>(SlotOf("Padding"));
    const float w = available.w > 0 && available.w < 1.0e6f ? available.w : 320.0f;
    const float h = available.h > 0 && available.h < 1.0e6f ? available.h : 200.0f;
    desired_ = Size{w, h};
    (void)margin;
    (void)pad;
    ApplySizeConstraints();
    plot_ = PlotRectFrom(Rect{bounds_.x, bounds_.y, std::max(1.0f, desired_.w),
                              std::max(1.0f, desired_.h)});
    return desired_;
}

void ChartPlot::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    plot_ = PlotRectFrom(finalRect);
    // The transform depends on the plot size, so it has to be recomputed here:
    // Arrange is the first moment the real size is known, and a stale scale draws
    // a line that does not match its own grid.
    Fit();
}

Rect ChartPlot::PlotRectFrom(const Rect& outer) const {
    // Derived from the rect we are handed, never from an earlier one: a paint pass
    // shifts bounds_ by the scroll offset, and reading a cached rect would draw the
    // figure at its unscrolled position.
    const Thickness margin = SlotOf("Margin") >= 0 ? Get<Thickness>(SlotOf("Margin"))
                                                   : Thickness{};
    return Rect{outer.x + margin.l, outer.y + margin.t,
                std::max(1.0f, outer.w - margin.H()),
                std::max(1.0f, outer.h - margin.V())};
}

void ChartPlot::DrawLinePlot(PaintCtx& ctx, const std::vector<ChartPoint>& pts,
                             const Color& color, double originX, double originY) {
    if (pts.size() < 2) return;
    // One DrawLine per segment. PaintCtx has no path primitive, and adding one for
    // a single control would mean touching all three backends (§10a.7.1 kept the
    // primitive set deliberately at six). The cost is one call per output point,
    // and the point count is what downsampling just bounded.
    const Rect frame = PlotRectFrom(bounds_);
    double px = LeftFromX(pts[0].x) + originX;
    double py = TopFromY(pts[0].y) + originY;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double cx = LeftFromX(pts[i].x) + originX;
        const double cy = TopFromY(pts[i].y) + originY;
        // Skip segments entirely outside the plot area vertically: with an auto-fit
        // range this cannot happen, but a pinned range can put a spike far outside,
        // and a line drawn to ±1e6 pixels is slow for no benefit.
        const bool bothAbove = py < frame.y - 4 && cy < frame.y - 4;
        const bool bothBelow = py > frame.y + frame.h + 4 && cy > frame.y + frame.h + 4;
        if (!bothAbove && !bothBelow && std::isfinite(px) && std::isfinite(cx) &&
            std::isfinite(py) && std::isfinite(cy)) {
            ctx.DrawLine(Point{static_cast<float>(px), static_cast<float>(py)},
                         Point{static_cast<float>(cx), static_cast<float>(cy)}, color, 1.5f);
        }
        px = cx;
        py = cy;
    }
}

void ChartPlot::DrawAreaPlot(PaintCtx& ctx, const std::vector<ChartPoint>& pts,
                             const Color& color, double originX, double originY) {
    if (pts.size() < 2) return;
    const double baseY = TopFromY(visible_.minY) + originY;
    Color fill = color;
    fill.a = 0.22f;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double x0 = LeftFromX(pts[i - 1].x) + originX;
        const double x1 = LeftFromX(pts[i].x) + originX;
        const double y0 = TopFromY(pts[i - 1].y) + originY;
        const double y1 = TopFromY(pts[i].y) + originY;
        if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1))
            continue;
        const float left = static_cast<float>(x0);
        const float right = static_cast<float>(x1);
        if (right <= left) continue;
        const float topL = static_cast<float>(std::min(y0, y1));
        // Approximate the trapezoid with two stacked rectangles. Cheap, and the
        // error is bounded by the segment's own slope over one pixel column.
        const float mid = static_cast<float>((y0 + y1) * 0.5);
        ctx.FillRect(Rect{left, topL, right - left, static_cast<float>(baseY) - topL}, fill);
        ctx.FillRect(Rect{left, std::min(topL, mid), right - left,
                          std::abs(static_cast<float>(baseY) - std::min(topL, mid))},
                     fill);
    }
    // Then the outline on top, so the area reads as "under a line" rather than as a
    // silhouette that happens to have a straight top edge.
    DrawLinePlot(ctx, pts, color, originX, originY);
}

void ChartPlot::DrawBarPlot(PaintCtx& ctx, const std::vector<ChartPoint>& pts,
                            const Color& color, double originX, double originY) {
    if (pts.empty()) return;
    const double baseY = TopFromY(visible_.minY) + originY;
    const Rect frame = PlotRectFrom(bounds_);
    // Bar width from the spacing between the first two points, falling back to a
    // pixel when there is only one. 70% leaves the gap that makes bars read as
    // discrete rather than as a filled block.
    double spacing = frame.w;
    if (pts.size() >= 2) {
        spacing = std::abs((LeftFromX(pts[1].x) + originX) - (LeftFromX(pts[0].x) + originX));
    }
    const double barW = std::max(1.0, spacing * 0.7);
    for (const auto& p : pts) {
        const double cx = LeftFromX(p.x) + originX;
        const double cy = TopFromY(p.y) + originY;
        if (!std::isfinite(cx) || !std::isfinite(cy)) continue;
        const double top = std::min(cy, baseY);
        double h = std::abs(baseY - cy);
        // A minimum visible height. Without it a series whose values are all equal
        // -- a perfectly ordinary set of measurements -- reduces to a zero range and
        // every bar is skipped as "shorter than half a pixel", so the figure renders
        // as empty axes. A bar chart is about RELATIVE comparison; drawing a 1px
        // baseline is honest, drawing nothing is not.
        h = std::max(h, 1.5);
        ctx.FillRect(Rect{static_cast<float>(cx - barW * 0.5), static_cast<float>(top),
                          static_cast<float>(barW), static_cast<float>(h)},
                     color);
    }
}

void ChartPlot::DrawCandlePlot(PaintCtx& ctx, const ThemePalette& palette, double originX,
                               double originY) {
    if (!candles_ || candles_->Count() == 0) return;
    const std::vector<Candle>& list = candles_->Candles();
    const Color up = ResolveColor(candleUpColor_, palette);
    const Color down = ResolveColor(candleDownColor_, palette);
    double spacing = plot_.w;
    if (list.size() >= 2) {
        spacing = std::abs((LeftFromX(list[1].x) + originX) -
                           (LeftFromX(list[0].x) + originX));
    }
    const double bodyW = std::clamp(spacing * 0.62, 2.0, 14.0);
    for (const Candle& c : list) {
        const double cx = LeftFromX(c.x) + originX;
        const double high = TopFromY(c.high) + originY;
        const double low = TopFromY(c.low) + originY;
        const double open = TopFromY(c.open) + originY;
        const double close = TopFromY(c.close) + originY;
        if (!std::isfinite(cx) || !std::isfinite(high) || !std::isfinite(low) ||
            !std::isfinite(open) || !std::isfinite(close)) {
            continue;
        }
        const Color& color = c.close >= c.open ? up : down;
        // The wick shows the whole high-low range; the body shows open-close.
        ctx.DrawLine(Point{static_cast<float>(cx), static_cast<float>(high)},
                     Point{static_cast<float>(cx), static_cast<float>(low)}, color, 1.0f);
        const double top = std::min(open, close);
        const double h = std::max(1.5, std::abs(close - open));
        ctx.FillRect(Rect{static_cast<float>(cx - bodyW * 0.5), static_cast<float>(top),
                          static_cast<float>(bodyW), static_cast<float>(h)},
                     color);
    }
}

void ChartPlot::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    // Offscreen figures are not drawn at all. The enclosing clip would discard the
    // pixels anyway; skipping here also stops the live stream from paying for a
    // downsample of a plot nobody can see.
    if (!viewportVisible_) return;
    const double t0 = NowUs();

    // Observe the interval between paints. This is what makes the overlay work on its
    // own: the control knows exactly when it painted, so it does not need a host to
    // tell it. The FIRST paint has no predecessor and is skipped.
    if (showFps_) {
        if (lastPaintStampUs_ > 0) {
            const double dt = (t0 - lastPaintStampUs_) / 1000000.0;
            // Ignore implausible gaps: a paused window or a breakpoint can leave a
            // seconds-long hole, and folding that in would report a rate of 0.2 fps for
            // several seconds after the page resumes.
            if (dt > 0 && dt < 0.5) AccumulateFrame(dt);
        }
        lastPaintStampUs_ = t0;
    }

    // The rect to draw into THIS frame. bounds_ has already been shifted by the
    // enclosing scroller, so recomputing from it is what keeps the figure attached
    // to the page. plot_ (the cached rect) is layout geometry and is only valid in
    // unscrolled space.
    const Rect frame = PlotRectFrom(bounds_);
    const double originX = frame.x - plot_.x;
    const double originY = frame.y - plot_.y;

    const Color gridColor = palette.colors[static_cast<int>(Token::CardBorder)];
    const Color axisColor = palette.colors[static_cast<int>(Token::TextSecondary)];
    // Token-coloured defaults follow the theme; a literal colour set by a page
    // wins, so a colour picker can actually change what is drawn.
    const Color seriesColor = ResolveColor(lineColor_, palette);
    const Color barColor = ResolveColor(barColor_, palette);

    // Grid first, so the data is drawn over it rather than behind it.
    for (double v : xTicks_.values) {
        const double x = LeftFromX(v) + originX;
        if (x < frame.x - 1 || x > frame.x + frame.w + 1) continue;
        ctx.FillRect(Rect{static_cast<float>(x), frame.y, 1.0f, frame.h},
                     Color{gridColor.r, gridColor.g, gridColor.b, 0.35f});
    }
    for (double v : yTicks_.values) {
        const double y = TopFromY(v) + originY;
        if (y < frame.y - 1 || y > frame.y + frame.h + 1) continue;
        ctx.FillRect(Rect{frame.x, static_cast<float>(y), frame.w, 1.0f},
                     Color{gridColor.r, gridColor.g, gridColor.b, 0.35f});
    }

    // Frame on the left and bottom only: a full box around the plot competes with
    // the grid for attention.
    ctx.FillRect(Rect{frame.x, frame.y, 1.0f, frame.h}, axisColor);
    ctx.FillRect(Rect{frame.x, frame.y + frame.h - 1.0f, frame.w, 1.0f}, axisColor);

    // --- the point of the whole file: reduce before drawing -------------------
    // The budget and the resulting count were resolved in Fit(), so this only
    // fetches the (cached) reduced view and times the fetch.
    lastDownsampleUs_ = 0;
    if (SourceCount(series_, stream_) > 0) {
        const double d0 = NowUs();
        // A zero budget means "one per horizontal pixel": beyond one point per
        // column, extra points are literally invisible.
        const size_t budget = static_cast<size_t>(std::max(3, lastBudget_));
        const std::shared_ptr<const std::vector<ChartPoint>> pts =
            stream_ ? stream_->Downsampled(budget, mode_)
                    : series_->Downsampled(budget, mode_);
        lastDownsampleUs_ = NowUs() - d0;
        lastDrawnPoints_ = pts->size();

        ctx.PushClip(frame);
        const int kind = SlotOf("PlotKind") >= 0 ? Get<int>(SlotOf("PlotKind")) : 0;
        if (kind == 1) {
            if (showBars_) DrawBarPlot(ctx, *pts, barColor, originX, originY);
        } else if (kind == 2) {
            if (showLine_) DrawAreaPlot(ctx, *pts, seriesColor, originX, originY);
        } else if (showLine_) {
            DrawLinePlot(ctx, *pts, seriesColor, originX, originY);
        }
        ctx.PopClip();
    }

    // The overlay is a second x/y series drawn on top of the primary encoding.
    // Bars and line therefore share one transform and one set of axes.
    if (overlaySeries_ && overlaySeries_->Count() > 0 && showLine_) {
        const size_t budget = static_cast<size_t>(std::max(3, lastBudget_));
        const std::shared_ptr<const std::vector<ChartPoint>> pts =
            overlaySeries_->Downsampled(budget, mode_);
        ctx.PushClip(frame);
        DrawLinePlot(ctx, *pts, seriesColor, originX, originY);
        ctx.PopClip();
    }

    if (candles_) {
        ctx.PushClip(frame);
        DrawCandlePlot(ctx, palette, originX, originY);
        ctx.PopClip();
    }

    lastPaintUs_ = NowUs() - t0;

    // --- frame rate, top-right ------------------------------------------------
    // Drawn last so nothing overlaps it. In the plot's own coordinates (shifted by
    // the frame origin) so it travels with the figure when the page scrolls.
    // Drawing the overlay only once a rate exists. A "0 fps" plate is worse than no
    // plate: it reads as "the chart is frozen" when the truth is "no frame has been
    // measured yet" -- which is the state of a headless single-pass render.
    if (showFps_ && fps_ > 0) {
        ITextService* text = TextSystem::Get();
        wchar_t buf[48];
        std::swprintf(buf, std::size(buf), L"%.0f fps", fps_);
        const std::wstring label(buf);
        const float w = text ? text->Measure(label, 12.0f, WeightSemiBold).w : 46.0f;
        const Rect box{frame.x + frame.w - w - 14.0f, frame.y + 4.0f, w + 10.0f, 18.0f};
        // A translucent plate: white text directly over a dense line is unreadable.
        const Color plate = palette.colors[static_cast<int>(Token::CardBackground)];
        ctx.FillRoundRect(box, 4.0f, Color{plate.r, plate.g, plate.b, 0.82f});
        const Color accent = palette.colors[static_cast<int>(Token::Accent)];
        ctx.DrawText(label, Rect{box.x + 5.0f, box.y + 2.0f, w + 2.0f, 14.0f}, accent, 12.0f,
                     WeightSemiBold, TextAlign::Left);
    }
}

// ---------------------------------------------------------------------------
// Chart
// ---------------------------------------------------------------------------
Chart::Chart() {
    BindType(TypeRegistry::Instance().Find("wui/Chart"));
    slotTitle_ = SlotOf("Title");
    slotSeries_ = SlotOf("Series");
    slotStream_ = SlotOf("Stream");
    slotShowFps_ = SlotOf("ShowFps");
    slotAutoFit_ = SlotOf("AutoFit");
    slotDownsample_ = SlotOf("Downsample");
    slotMaxPoints_ = SlotOf("MaxPoints");
    slotPlotKind_ = SlotOf("PlotKind");
    slotViewportVisible_ = SlotOf("ViewportVisible");

    plot_ = std::make_shared<ChartPlot>();
    // The plot is a real child, so the framework's paint order, clip and dirty
    // propagation apply to it -- and so a slot change on the chart can forward to
    // it in one place (OnSlotChanged) instead of the chart drawing it by hand.
    AddChild(plot_);

    // Replay whatever is ALREADY in the data-bound slots.
    //
    // A WAML binding can write Series/Stream/ShowFps before this constructor has
    // finished (the binding is applied during instantiation, and OnSlotChanged runs
    // as soon as the slot is written). At that moment plot_ is null, so the write is
    // dropped and NOT retried -- the chart then renders empty data over a heading
    // that claims otherwise. Pushing the current values into the freshly built plot
    // makes the control correct regardless of the order it was populated in.
    for (int slot : {slotSeries_, slotStream_, slotShowFps_, slotAutoFit_, slotDownsample_,
                     slotMaxPoints_, slotPlotKind_, SlotOf("OverlaySeries"), SlotOf("Candles"),
                     SlotOf("BarColor"), SlotOf("LineColor"), SlotOf("CandleUpColor"),
                     SlotOf("CandleDownColor"), SlotOf("ShowBars"), SlotOf("ShowLine")}) {
        // RawSlot, not Get<Value>: Get<> goes through the codec table, which has no
        // entry for Value itself.
        if (slot >= 0) OnSlotChanged(slot, RawSlot(slot));
    }
}

// Reads an enum-valued slot. Enum slots carry VType::Enum (not Number), so a check
// written as `type == VType::Number` silently takes the fallback branch and the
// property appears to be ignored -- which is exactly what made Downsample="None"
// behave like the default. The default is the caller's, so a malformed value has a
// defined meaning instead of being clamped to zero by accident.
namespace {
int EnumSlotValue(const Value& value, int fallback) {
    if (value.type == VType::Enum) return static_cast<int>(value.enumValue);
    if (value.type == VType::Number) return static_cast<int>(value.num);
    return fallback;
}
} // namespace

void Chart::OnSlotChanged(int slot, const Value& value) {
    if (!plot_) return;
    if (slot == slotSeries_) {
        // Series accepts EITHER source, because a page writes the same obvious
        // `Series="{Binding ...}"` for both and should not have to care which kind its
        // ViewModel produced. A static cast made a live stream silently bind to
        // nothing: the chart rendered empty over a heading claiming it was live.
        const RefPtr& ref = Get<RefPtr>(slotSeries_);
        if (auto stream = std::dynamic_pointer_cast<ChartStream>(ref)) {
            plot_->SetStream(stream);
        } else if (auto series = std::dynamic_pointer_cast<ChartSeries>(ref)) {
            plot_->SetSeries(series);
        }
    } else if (slot == slotStream_) {
        if (auto stream = std::dynamic_pointer_cast<ChartStream>(Get<RefPtr>(slotStream_))) {
            plot_->SetStream(stream);
        }
    } else if (slot == slotShowFps_) {
        plot_->SetShowFps(value.type != VType::Bool || value.flag);
    } else if (slot == slotAutoFit_) {
        plot_->SetAutoFit(value.type != VType::Bool || value.flag);
    } else if (slot == slotDownsample_) {
        const int mode = EnumSlotValue(value, static_cast<int>(DownsampleMode::Lttb));
        plot_->SetDownsampleMode(static_cast<DownsampleMode>(
            std::clamp(mode, 0, static_cast<int>(DownsampleMode::Lttb))));
    } else if (slot == slotMaxPoints_) {
        plot_->SetMaxPoints(value.type == VType::Number ? static_cast<int>(value.num) : 0);
    } else if (slot == slotPlotKind_) {
        // PlotKind lives on the plot, but is authored on the chart: writing it on
        // the chart is what lets one element describe the whole figure.
        plot_->SetSlotValue(plot_->SlotOf("PlotKind"),
                            Value::Enum(EnumSlotValue(value, 0)));
    } else if (slot == SlotOf("OverlaySeries")) {
        plot_->SetOverlaySeries(std::dynamic_pointer_cast<ChartSeries>(Get<RefPtr>(slot)));
    } else if (slot == SlotOf("Candles")) {
        plot_->SetCandles(std::dynamic_pointer_cast<CandleSeries>(Get<RefPtr>(slot)));
    } else if (slot == SlotOf("BarColor")) {
        plot_->SetBarColor(Get<Color>(slot));
    } else if (slot == SlotOf("LineColor")) {
        plot_->SetLineColor(Get<Color>(slot));
    } else if (slot == SlotOf("CandleUpColor")) {
        plot_->SetCandleUpColor(Get<Color>(slot));
    } else if (slot == SlotOf("CandleDownColor")) {
        plot_->SetCandleDownColor(Get<Color>(slot));
    } else if (slot == SlotOf("ShowBars")) {
        plot_->SetShowBars(value.type != VType::Bool || value.flag);
    } else if (slot == SlotOf("ShowLine")) {
        plot_->SetShowLine(value.type != VType::Bool || value.flag);
    }
    // ViewportVisible is published BY the chart; a binding writing it back must
    // not kill the paint path's own decision.
}

Size Chart::Measure(const Size& available) {
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const float availW = available.w > 0 && available.w < 1.0e6f ? available.w : 480.0f;
    const float availH = available.h > 0 && available.h < 1.0e6f ? available.h : 260.0f;
    const float innerW = std::max(1.0f, availW - margin.H());
    const float innerH = std::max(1.0f, availH - margin.V());
    // The plot gets the area left over after the axis gutters. Padding is not a
    // slot: it is a property of THIS control's drawing, and exposing it would
    // invite a page to shrink the plot until the labels overlap.
    const float plotW = std::max(1.0f, innerW - static_cast<float>(paddingLeft_ + paddingRight_));
    const float plotH = std::max(1.0f, innerH - static_cast<float>(paddingTop_ + paddingBottom_));
    plot_->Measure(Size{plotW, plotH});
    desired_ = Size{innerW, innerH};
    ApplySizeConstraints();
    return desired_;
}

void Chart::Arrange(const Rect& finalRect) {
    bounds_ = finalRect;
    const Thickness margin = Get<Thickness>(SlotOf("Margin"));
    const Rect inner{finalRect.x + margin.l, finalRect.y + margin.t,
                     std::max(1.0f, finalRect.w - margin.H()),
                     std::max(1.0f, finalRect.h - margin.V())};
    plot_->Arrange(Rect{inner.x + static_cast<float>(paddingLeft_),
                        inner.y + static_cast<float>(paddingTop_),
                        std::max(1.0f, inner.w - static_cast<float>(paddingLeft_ + paddingRight_)),
                        std::max(1.0f, inner.h - static_cast<float>(paddingTop_ + paddingBottom_))});
    // Arrange is where the plot resolves its budget and draw count (see
    // ChartPlot::Fit), so this is the first moment a page can quote them -- and
    // doing it here rather than after painting is what makes the numbers agree
    // between "just laid out" and "already painted". The sample's headless tools
    // only lay out, and the snapshot renders text DURING paint, so a count that
    // only existed after painting would show zero in exactly the cases a reader
    // checks.
    PublishCounts();
    // Measuring the label strings happens during paint, so a re-arrange must
    // repaint even if only the size changed.
    Invalidate(AffectsRender);
}

void Chart::OnPaint(PaintCtx& ctx, const ThemePalette& palette) {
    ++PaintCount;
    if (!plot_) return;
    // Decided HERE, before the child paints, so the plot can skip its own work
    // and a ViewModel bound to ViewportVisible can pause the live data source.
    const bool inView = InViewport(this);
    plot_->SetViewportVisible(inView);
    if (slotViewportVisible_ >= 0 && RawSlot(slotViewportVisible_).flag != inView) {
        SetSlotValue(slotViewportVisible_, Value::Bool(inView), true);
    }
    if (!inView) return;
    const Color titleColor = palette.colors[static_cast<int>(Token::TextPrimary)];

    // Only the title is drawn here. Axis labels, tick marks and the grid belong to
    // DrawAxisLabels, called from PaintChildrenAt -- see the comment there for why
    // drawing them at THIS point put them at the wrong position once the page
    // scrolled.

    const std::wstring title = slotTitle_ >= 0 ? Get<std::wstring>(slotTitle_) : std::wstring();
    if (!title.empty()) {
        const float w = std::max(1.0f, bounds_.w - 8.0f);
        ctx.DrawText(title, Rect{bounds_.x + 4.0f, bounds_.y - 2.0f, w, kAxisFontSize + 6},
                     titleColor, kAxisFontSize + 1, WeightSemiBold, TextAlign::Left);
    }
    // NOTE: the counters are NOT published here. The count is resolved in Arrange
    // (see ChartPlot::Fit) and published from Chart::Arrange, which is both earlier
    // and stable -- publishing it mid-paint made the text depend on which frame a
    // reader sampled, and two snapshot passes then disagreed.
}

void Chart::PaintChildrenAt(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) {
    Widget::PaintChildrenAt(ctx, palette, shift);
    // Axis labels are drawn HERE, not in OnPaint.
    //
    // OnPaint runs BEFORE the children do, so at that moment the plot's bounds_ have
    // not been shifted yet: any offset derived from them is zero, and the labels end
    // up where the figure was before the page scrolled. That is exactly the reported
    // symptom -- the line slid with the page while its tick labels stayed behind.
    //
    // `shift` is the amount that will be added to the child's bounds_ (the base
    // PaintChildrenAt passes shift + the child's own paintOffset down), so it is the
    // authoritative scroll delta for this frame.
    DrawAxisLabels(ctx, palette, shift);
}

void Chart::DrawAxisLabels(PaintCtx& ctx, const ThemePalette& palette, const Point& shift) {
    if (!plot_) return;
    if (!plot_->ViewportVisible()) return;
    ITextService* text = TextSystem::Get();
    const Color labelColor = palette.colors[static_cast<int>(Token::TextSecondary)];
    const Color axisColor = palette.colors[static_cast<int>(Token::TextSecondary)];

    const Rect layoutArea = plot_->PlotArea();
    const Rect area{layoutArea.x + shift.x, layoutArea.y + shift.y, layoutArea.w, layoutArea.h};
    const double originX = shift.x;
    const double originY = shift.y;

    // --- x labels, centred under their grid line ------------------------------
    for (double v : plot_->XTicks().values) {
        const double x = plot_->LeftFromX(v) + originX;
        if (x < area.x - 1 || x > area.x + area.w + 1) continue;
        const std::wstring label = FormatTickLabel(v, plot_->XTicks().step);
        const float w = text ? text->Measure(label, kAxisFontSize, WeightNormal).w : 24.0f;
        ctx.DrawText(label,
                     Rect{static_cast<float>(x) - w * 0.5f, area.y + area.h + kLabelGap, w,
                          kAxisFontSize + 4},
                     labelColor, kAxisFontSize, WeightNormal, TextAlign::Center);
    }

    // --- y labels, right-aligned against the axis ----------------------------
    for (double v : plot_->YTicks().values) {
        const double y = plot_->TopFromY(v) + originY;
        if (y < area.y - 1 || y > area.y + area.h + 1) continue;
        const std::wstring label = FormatTickLabel(v, plot_->YTicks().step);
        const float w = text ? text->Measure(label, kAxisFontSize, WeightNormal).w : 24.0f;
        ctx.DrawText(label,
                     Rect{area.x - kLabelGap - w, static_cast<float>(y) - kAxisFontSize * 0.5f, w,
                          kAxisFontSize + 3},
                     labelColor, kAxisFontSize, WeightNormal, TextAlign::Right);
    }

    // Tick marks: 4 px outward. Without them the labels float at an ambiguous
    // distance from the grid and the eye cannot tell which line they belong to.
    for (double v : plot_->XTicks().values) {
        const double x = plot_->LeftFromX(v) + originX;
        if (x < area.x - 1 || x > area.x + area.w + 1) continue;
        ctx.FillRect(Rect{static_cast<float>(x), area.y + area.h, 1.0f, 4.0f}, axisColor);
    }
}

void Chart::PublishCounts() {
    if (!plot_) return;
    auto publish = [this](int slot, double v) {
        if (slot < 0) return;
        const Value value = Value::Number(v);
        if (RawSlot(slot).Equals(value)) return;
        SetSlotValue(slot, value, true);
    };
    publish(SlotOf("DrawnPoints"), static_cast<double>(plot_->LastDrawnPoints()));
    publish(SlotOf("SourcePoints"), static_cast<double>(plot_->LastSourcePoints()));
}

} // namespace wui
