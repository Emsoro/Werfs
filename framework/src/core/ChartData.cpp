// SPDX-License-Identifier: MIT
#include "ChartData.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wui {

// ---------------------------------------------------------------------------
// ChartBounds
// ---------------------------------------------------------------------------
ChartBounds ChartBounds::FromPoints(const ChartPoint* pts, size_t count) {
    ChartBounds b;
    if (pts == nullptr || count == 0) return b;
    b.empty = false;
    b.minX = b.maxX = pts[0].x;
    b.minY = b.maxY = pts[0].y;
    for (size_t i = 1; i < count; ++i) {
        b.minX = std::min(b.minX, pts[i].x);
        b.maxX = std::max(b.maxX, pts[i].x);
        b.minY = std::min(b.minY, pts[i].y);
        b.maxY = std::max(b.maxY, pts[i].y);
    }
    return b;
}

void ChartBounds::Surround(const ChartBounds& other) {
    if (other.empty) return;
    if (empty) {
        *this = other;
        return;
    }
    minX = std::min(minX, other.minX);
    maxX = std::max(maxX, other.maxX);
    minY = std::min(minY, other.minY);
    maxY = std::max(maxY, other.maxY);
}

ChartBounds ChartBounds::Inflated(double fraction) const {
    if (empty) return *this;
    ChartBounds out = *this;
    const double dx = Width() * fraction;
    const double dy = Height() * fraction;
    // A flat series (a constant y, or a single point) has zero height. Padding it
    // by a fraction of zero leaves zero, and every later division by the range
    // would then produce inf/NaN -- which shows up as a line drawn at no
    // particular place rather than as an error. Give such a range an absolute
    // margin instead.
    const double padY = dy > 0 ? dy : (std::abs(maxY) > 0 ? std::abs(maxY) * 0.1 : 0.5);
    out.minX -= dx;
    out.maxX += dx;
    out.minY -= padY;
    out.maxY += padY;
    return out;
}

// ---------------------------------------------------------------------------
// Downsampling
// ---------------------------------------------------------------------------
const char* DownsampleModeName(DownsampleMode mode) {
    switch (mode) {
    case DownsampleMode::None: return "None";
    case DownsampleMode::Uniform: return "Uniform";
    case DownsampleMode::MinMax: return "MinMax";
    case DownsampleMode::Lttb: return "LTTB";
    }
    return "?";
}

namespace {

// Every k-th point. Kept because it is the obvious first answer, and because the
// sample page shows what it costs: a spike that falls between two samples simply
// disappears, so a chart drawn this way can report a flat line where the data has
// a 40% excursion.
void DownsampleUniform(const ChartPoint* in, size_t count, size_t maxPoints,
                       std::vector<ChartPoint>& out) {
    out.push_back(in[0]);
    if (maxPoints <= 2) {
        out.push_back(in[count - 1]);
        return;
    }
    const size_t interior = maxPoints - 2;
    const size_t span = count - 2; // points strictly between first and last
    for (size_t i = 0; i < interior; ++i) {
        // Spread the picks evenly over the interior. `span * i / interior + 1`
        // never leaves [1, count-2] because i < interior.
        const size_t idx = 1 + span * i / interior;
        if (idx < count - 1) out.push_back(in[idx]);
    }
    out.push_back(in[count - 1]);
}

// Per-bucket extremes. Each bucket contributes the (min y) and (max y) points in
// x order, so the drawn envelope touches every peak and trough: a spike cannot be
// hidden, only its exact position within one bucket is lost. This is what
// oscilloscopes and audio meters do, and it is the right default for "is there
// anything wrong in here" questions.
void DownsampleMinMax(const ChartPoint* in, size_t count, size_t maxPoints,
                      std::vector<ChartPoint>& out) {
    // Two points per bucket plus the two endpoints.
    const size_t buckets = std::max<size_t>(1, (maxPoints - 2) / 2);
    const size_t interior = count - 2;
    out.push_back(in[0]);
    for (size_t b = 0; b < buckets; ++b) {
        const size_t start = 1 + interior * b / buckets;
        size_t end = 1 + interior * (b + 1) / buckets;
        if (end <= start) continue;
        end = std::min(end, count - 1);
        size_t iMin = start;
        size_t iMax = start;
        for (size_t i = start + 1; i < end; ++i) {
            if (in[i].y < in[iMin].y) iMin = i;
            if (in[i].y > in[iMax].y) iMax = i;
        }
        // Emit in x order so the polyline never doubles back on itself. Emitting
        // max-then-min regardless of position produced visible zig-zags, which is
        // the classic way this algorithm gets implemented wrong.
        if (iMin <= iMax) {
            out.push_back(in[iMin]);
            if (iMax != iMin) out.push_back(in[iMax]);
        } else {
            out.push_back(in[iMax]);
            out.push_back(in[iMin]);
        }
    }
    out.push_back(in[count - 1]);
}

// Largest-Triangle-Three-Buckets (Steinarsson 2013).
//
// For each bucket, keep the point that forms the largest triangle with the
// previously kept point and the average of the NEXT bucket. The triangle area is
// a cheap proxy for "how much does this point contribute to the silhouette", so
// the result looks like the original at any zoom level -- unlike uniform
// sampling, which keeps whatever happens to land on the sample grid.
void DownsampleLttb(const ChartPoint* in, size_t count, size_t maxPoints,
                    std::vector<ChartPoint>& out) {
    out.push_back(in[0]);
    // Every bucket after the first, leaving room for the final point.
    const double every = static_cast<double>(count - 2) / static_cast<double>(maxPoints - 2);
    size_t a = 0; // index of the last point we kept
    for (size_t i = 0; i + 3 <= maxPoints; ++i) {
        // Average of the next bucket, used as the third vertex of the triangle.
        size_t avgStart = static_cast<size_t>(std::floor(every * static_cast<double>(i + 1))) + 1;
        size_t avgEnd = static_cast<size_t>(std::floor(every * static_cast<double>(i + 2))) + 1;
        avgEnd = std::min(avgEnd, count);
        avgStart = std::min(avgStart, count - 1);
        if (avgEnd <= avgStart) avgEnd = std::min(avgStart + 1, count);

        double avgX = 0;
        double avgY = 0;
        const size_t avgCount = avgEnd - avgStart;
        for (size_t j = avgStart; j < avgEnd; ++j) {
            avgX += in[j].x;
            avgY += in[j].y;
        }
        if (avgCount > 0) {
            avgX /= static_cast<double>(avgCount);
            avgY /= static_cast<double>(avgCount);
        }

        // This bucket: keep the point forming the largest triangle with (a, avg).
        size_t rangeStart = static_cast<size_t>(std::floor(every * static_cast<double>(i))) + 1;
        size_t rangeEnd = static_cast<size_t>(std::floor(every * static_cast<double>(i + 1))) + 1;
        rangeStart = std::min(rangeStart, count - 1);
        rangeEnd = std::min(std::max(rangeEnd, rangeStart + 1), count);

        const double ax = in[a].x;
        const double ay = in[a].y;
        double bestArea = -1;
        size_t chosen = rangeStart;
        for (size_t j = rangeStart; j < rangeEnd; ++j) {
            const double area = std::abs((ax - avgX) * (in[j].y - ay) -
                                         (ax - in[j].x) * (avgY - ay)) *
                                0.5;
            if (area > bestArea) {
                bestArea = area;
                chosen = j;
            }
        }
        out.push_back(in[chosen]);
        a = chosen;
    }
    out.push_back(in[count - 1]);
}

} // namespace

size_t Downsample(const ChartPoint* in, size_t count, size_t maxPoints, DownsampleMode mode,
                  std::vector<ChartPoint>& out) {
    out.clear();
    if (in == nullptr || count == 0) return 0;
    // Never let the downsampler be the reason a small chart is wrong: with fewer
    // points than the budget, or no budget at all, the input is the answer.
    if (mode == DownsampleMode::None || maxPoints < 3 || count <= maxPoints) {
        out.assign(in, in + count);
        return out.size();
    }
    out.reserve(maxPoints);
    switch (mode) {
    case DownsampleMode::Uniform: DownsampleUniform(in, count, maxPoints, out); break;
    case DownsampleMode::MinMax: DownsampleMinMax(in, count, maxPoints, out); break;
    case DownsampleMode::Lttb: DownsampleLttb(in, count, maxPoints, out); break;
    case DownsampleMode::None: out.assign(in, in + count); break;
    }
    return out.size();
}

// ---------------------------------------------------------------------------
// ChartSeries
// ---------------------------------------------------------------------------
void ChartSeries::SetPoints(std::vector<ChartPoint> points) {
    points_ = std::move(points);
    boundsValid_ = false;
    InvalidateDownsample();
}

void ChartSeries::SetUniformX(std::vector<double> ys) {
    points_.clear();
    points_.resize(ys.size());
    for (size_t i = 0; i < ys.size(); ++i) {
        points_[i].x = static_cast<double>(i);
        points_[i].y = ys[i];
    }
    boundsValid_ = false;
    InvalidateDownsample();
}

const ChartBounds& ChartSeries::Bounds() const {
    if (!boundsValid_) {
        bounds_ = ChartBounds::FromPoints(points_.data(), points_.size());
        boundsValid_ = true;
    }
    return bounds_;
}

std::shared_ptr<const std::vector<ChartPoint>> ChartSeries::Downsampled(size_t maxPoints,
                                                                        DownsampleMode mode) const {
    const Key key{points_.size(), maxPoints, mode};
    // A fresh allocation per key, handed out by shared_ptr: two live views of the
    // same series stay independent (see the header note on why this is not a
    // reference).
    if (downsampleKey_ == key && downsampled_) return downsampled_;
    auto out = std::make_shared<std::vector<ChartPoint>>();
    Downsample(points_.data(), points_.size(), maxPoints, mode, *out);
    downsampled_ = out;
    downsampleKey_ = key;
    return downsampled_;
}

// ---------------------------------------------------------------------------
// CandleSeries
// ---------------------------------------------------------------------------
void CandleSeries::SetCandles(std::vector<Candle> candles) {
    candles_ = std::move(candles);
    boundsValid_ = false;
}

const ChartBounds& CandleSeries::Bounds() const {
    if (!boundsValid_) {
        bounds_ = ChartBounds{};
        if (!candles_.empty()) {
            bounds_.empty = false;
            bounds_.minX = bounds_.maxX = candles_[0].x;
            bounds_.minY = candles_[0].low;
            bounds_.maxY = candles_[0].high;
            for (size_t i = 1; i < candles_.size(); ++i) {
                const Candle& c = candles_[i];
                bounds_.minX = std::min(bounds_.minX, c.x);
                bounds_.maxX = std::max(bounds_.maxX, c.x);
                bounds_.minY = std::min(bounds_.minY, c.low);
                bounds_.maxY = std::max(bounds_.maxY, c.high);
            }
        }
        boundsValid_ = true;
    }
    return bounds_;
}

// ---------------------------------------------------------------------------
// ChartStream
// ---------------------------------------------------------------------------
ChartStream::ChartStream(size_t capacity) { SetCapacity(capacity); }

void ChartStream::SetCapacity(size_t capacity) {
    const size_t newCap = std::max<size_t>(2, capacity);
    if (newCap == capacity_ && ys_.size() == newCap) return;
    // Copy the live window out BEFORE reallocating: `old` has to survive the
    // assign() below, and the ring's head/count describe `old`, not the new buffer.
    std::vector<double> window;
    window.reserve(count_);
    for (size_t i = 0; i < count_; ++i) window.push_back(ys_[(head_ + i) % capacity_]);

    capacity_ = newCap;
    // Keep the MOST RECENT samples; a smaller window drops from the front.
    const size_t keep = std::min(window.size(), capacity_);
    const size_t drop = window.size() - keep;
    ys_.assign(capacity_, 0.0);
    for (size_t i = 0; i < keep; ++i) ys_[i] = window[drop + i];
    head_ = 0;
    count_ = keep;
    key_ = Key{};
}

void ChartStream::Push(double y) {
    if (ys_.size() != capacity_) ys_.assign(capacity_, 0.0);
    if (count_ < capacity_) {
        ys_[(head_ + count_) % capacity_] = y;
        ++count_;
    } else {
        // Full: overwrite the oldest and advance the window.
        ys_[head_] = y;
        head_ = (head_ + 1) % capacity_;
    }
    ++totalPushed_;
}

void ChartStream::PushPoints(std::vector<ChartPoint> points) {
    if (ys_.size() != capacity_) ys_.assign(capacity_, 0.0);
    if (points.empty()) return;
    // Only the LAST `capacity_` samples can survive, so drop the head up front rather
    // than pushing samples that will immediately be evicted.
    const size_t skip = points.size() > capacity_ ? points.size() - capacity_ : 0;
    for (size_t i = skip; i < points.size(); ++i) {
        if (count_ < capacity_) {
            ys_[(head_ + count_) % capacity_] = points[i].y;
            ++count_;
        } else {
            ys_[head_] = points[i].y;
            head_ = (head_ + 1) % capacity_;
        }
        // x is the absolute sample index, so the counter advances once per accepted
        // sample -- including the ones skipped above, which still happened.
        ++totalPushed_;
    }
}

void ChartStream::Clear() {
    head_ = 0;
    count_ = 0;
    totalPushed_ = 0;
    snapshot_.clear();
    downsampled_.reset();
    key_ = Key{};
}

void ChartStream::RebuildSnapshot() const {
    snapshot_.clear();
    snapshot_.reserve(count_);
    // The x value of the OLDEST live sample. Using the absolute push index means the
    // window slides left over time instead of every sample renumbering itself from
    // zero on each frame -- renumbering would make a live chart look static.
    const size_t firstX = totalPushed_ - count_;
    for (size_t i = 0; i < count_; ++i) {
        const size_t idx = (head_ + i) % capacity_;
        snapshot_.push_back(ChartPoint{static_cast<double>(firstX + i), ys_[idx]});
    }
}

void ChartStream::Snapshot(std::vector<ChartPoint>& out) const {
    RebuildSnapshot();
    out = snapshot_;
}

ChartBounds ChartStream::Bounds() const {
    RebuildSnapshot();
    return ChartBounds::FromPoints(snapshot_.data(), snapshot_.size());
}

std::shared_ptr<const std::vector<ChartPoint>> ChartStream::Downsampled(
    size_t maxPoints, DownsampleMode mode) const {
    const Key key{totalPushed_, maxPoints, mode};
    if (key_ == key && downsampled_) return downsampled_;
    RebuildSnapshot();
    auto out = std::make_shared<std::vector<ChartPoint>>();
    Downsample(snapshot_.data(), snapshot_.size(), maxPoints, mode, *out);
    downsampled_ = out;
    key_ = key;
    return downsampled_;
}

} // namespace wui
