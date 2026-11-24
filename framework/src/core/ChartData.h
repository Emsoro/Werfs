// SPDX-License-Identifier: MIT
// Chart data and downsampling.
//
// Split from the controls on purpose: the algorithms here are pure functions over
// plain arrays, so they can be tested and benchmarked with no widget, no paint
// context and no window. That is what makes "10 million points, 60 fps" a claim
// that can be measured instead of asserted.
//
// Why a dense vector and not ObservableList<RefObject>:
// the MVVM list used by ListBox stores a shared_ptr per item. Ten million items
// would be ten million heap allocations and >200 MB of object headers alone --
// the chart would be measuring the allocator, not the renderer. A chart series is
// a packed numeric array (the shape every real time-series source produces), and
// keeping it packed is what lets the downsampler read it cache-line by cache-line.
#pragma once

#include "Core.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace wui {

// ---------------------------------------------------------------------------
// ChartPoint / ChartBounds
// ---------------------------------------------------------------------------
struct ChartPoint {
    double x = 0;
    double y = 0;
};

// An axis-aligned data range. `empty` is tracked explicitly rather than inferred
// from min > max, because a series that is a single point has min == max and is
// still a perfectly good range.
struct ChartBounds {
    double minX = 0;
    double maxX = 1;
    double minY = 0;
    double maxY = 1;
    bool empty = true;

    static ChartBounds FromPoints(const ChartPoint* pts, size_t count);
    // Grows to include `other`. An empty other changes nothing.
    void Surround(const ChartBounds& other);
    // Adds a margin of `fraction` on each side; a zero-height range (a flat line)
    // is given a small absolute margin so it does not divide by zero downstream.
    ChartBounds Inflated(double fraction) const;
    double Width() const { return maxX - minX; }
    double Height() const { return maxY - minY; }
};

// ---------------------------------------------------------------------------
// Downsampling
//
// A chart of 10 000 000 points drawn into 700 px cannot show more than ~700
// distinct x positions: everything beyond that is sub-pixel. Drawing all of them
// is not "more accurate", it is pixel-identical work done thousands of times.
// These functions reduce a series to at most `maxPoints` representatives while
// keeping the SHAPE -- which is the only thing the user can actually see.
// ---------------------------------------------------------------------------
enum class DownsampleMode {
    None,       // every point, in order (the baseline the sample compares against)
    Uniform,    // every k-th point. Cheapest, loses peaks -- included to show that.
    MinMax,     // per-bucket (min,max) pair. Keeps the envelope, 2 points per bucket.
    Lttb,       // Largest-Triangle-Three-Buckets. Keeps the visual silhouette.
};

const char* DownsampleModeName(DownsampleMode mode);

// Reduces `in` to at most `maxPoints` points, appending to `out` (which is
// cleared first). Returns the number of points written.
//
// Contract shared by all modes:
//  * the first and last point are always kept, so the line starts and ends where
//    the data does;
//  * x stays monotonically non-decreasing;
//  * `maxPoints` < 3 or a series shorter than `maxPoints` copies the input
//    unchanged -- a downsampler must never be the reason a small chart is wrong.
size_t Downsample(const ChartPoint* in, size_t count, size_t maxPoints, DownsampleMode mode,
                  std::vector<ChartPoint>& out);

// ---------------------------------------------------------------------------
// ChartSeries: the data a series carries.
//
// Refcounted so a ViewModel can hand one to a widget without copying 80 MB, and
// so the widget does not have to own the lifetime of the data it draws.
// ---------------------------------------------------------------------------
class ChartSeries final : public RefObject {
public:
    ChartSeries() = default;

    // Replaces the data. `stride` lets a caller publish interleaved arrays
    // (x0,y0,x1,y1,...) without copying them apart first.
    void SetPoints(std::vector<ChartPoint> points);
    // Builds x = 0,1,2,... and y = `ys`, the shape most time-series sources have.
    // Moves the vector: publishing 10M points must not copy 160 MB.
    void SetUniformX(std::vector<double> ys);

    const std::vector<ChartPoint>& Points() const { return points_; }
    size_t Count() const { return points_.size(); }

    // Bounds of the FULL data, not of a downsampled view: auto-fit has to be
    // stable while the viewport resizes, or the plot would rescale as the window
    // gets narrower, which reads as the chart "moving by itself".
    const ChartBounds& Bounds() const;
    // Cached bounds are dropped whenever the points change.
    void InvalidateBounds() { boundsValid_ = false; }

    // Cached downsampled view. Keyed on (count, maxPoints, mode), so scrolling a
    // static series does not re-run the algorithm every frame -- the cache is the
    // reason the "on" case can be faster than the eye can follow.
    //
    // Returns a shared_ptr to an IMMUTABLE buffer rather than a reference to an
    // internal one. A reference looked cheaper, but it aliased: asking for a second
    // budget recomputed in place, silently changing the buffer a caller was still
    // holding. Two views of the same series is a legitimate thing to want (compare a
    // coarse and a fine reduction), and the cost of supporting it is one
    // allocation per key change, amortised to zero by the cache.
    std::shared_ptr<const std::vector<ChartPoint>> Downsampled(size_t maxPoints,
                                                               DownsampleMode mode) const;
    // How many points the last Downsampled() call actually produced.
    size_t LastDownsampledCount() const { return downsampled_ ? downsampled_->size() : 0; }
    void InvalidateDownsample() { downsampleKey_ = {0, 0, DownsampleMode::None}; }

private:
    std::vector<ChartPoint> points_;
    mutable ChartBounds bounds_;
    mutable bool boundsValid_ = false;

    mutable std::shared_ptr<const std::vector<ChartPoint>> downsampled_;
    struct Key {
        size_t count = 0;
        size_t maxPoints = 0;
        DownsampleMode mode = DownsampleMode::None;
        bool operator==(const Key& o) const {
            return count == o.count && maxPoints == o.maxPoints && mode == o.mode;
        }
    } mutable downsampleKey_;
};

// ---------------------------------------------------------------------------
// ChartStream: a fixed-capacity sliding window of samples.
//
// A live signal is not "a series that gets longer" -- it is a series that stays the
// same SIZE while its contents move. Modelling it as an ever-growing vector would
// make the cost climb with the uptime, and the downsampler would have more work to
// do every second; modelling it as a ring buffer keeps both constant, which is the
// only way "1 000 000 points at a steady frame rate" stays true an hour later.
//
// Pushing past capacity overwrites the oldest sample, so the window is always the
// most recent `Capacity()` points.
// ---------------------------------------------------------------------------
class ChartStream final : public RefObject {
public:
    explicit ChartStream(size_t capacity = 1000000);

    void SetCapacity(size_t capacity);
    size_t Capacity() const { return capacity_; }
    size_t Count() const { return count_; }
    // Total samples ever pushed, including ones already overwritten. This is what
    // the x axis labels itself with: a live chart showing "sample 1 234 567" says
    // more than one showing "0..999 999" every frame.
    size_t TotalPushed() const { return totalPushed_; }

    // Appends one sample at x = TotalPushed(), evicting the oldest if full.
    void Push(double y);
    // Seeds many samples at once. One call, so a page can fill the window without
    // the plot observing a half-filled state (and without 1 000 000 individual
    // function calls on startup). Samples beyond capacity keep only the tail.
    void PushPoints(std::vector<ChartPoint> points);
    // Appends `count` samples produced by `gen(i)` (i = 0..count-1). One call, so a
    // page can add a batch without the plot observing a half-filled window.
    template <class Gen>
    void PushBatch(size_t count, Gen&& gen) {
        for (size_t i = 0; i < count; ++i) Push(gen(i));
    }
    void Clear();

    // Copies the live window into `out` in oldest-to-newest order. The ring makes
    // this a two-range copy rather than a per-sample loop.
    void Snapshot(std::vector<ChartPoint>& out) const;

    // Bounds of the current window. Recomputed on demand; a sliding window changes
    // every push, so caching would need invalidating on every frame anyway.
    ChartBounds Bounds() const;

    // Downsample of the current window, cached by (push counter, budget, mode).
    std::shared_ptr<const std::vector<ChartPoint>> Downsampled(size_t maxPoints,
                                                               DownsampleMode mode) const;

private:
    void RebuildSnapshot() const;

    size_t capacity_;
    std::vector<double> ys_;   // ring storage, size == capacity_
    size_t head_ = 0;          // index of the OLDEST sample
    size_t count_ = 0;
    size_t totalPushed_ = 0;

    mutable std::vector<ChartPoint> snapshot_;
    mutable std::shared_ptr<const std::vector<ChartPoint>> downsampled_;
    struct Key {
        size_t version = 0;
        size_t maxPoints = 0;
        DownsampleMode mode = DownsampleMode::None;
        bool operator==(const Key& o) const {
            return version == o.version && maxPoints == o.maxPoints && mode == o.mode;
        }
    } mutable key_;
};

} // namespace wui
